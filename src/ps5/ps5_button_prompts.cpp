// Skate 3 for PS5 - PlayStation button prompts (see ps5_button_prompts.h).
#include "ps5_button_prompts.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/graphics/pipeline/texture/cache.h>
#include <rex/graphics/pipeline/texture/info.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/thread/mutex.h>

namespace skate3::native_scene {
// (skate3_native_guest_read.h) Guest memory copy that fails instead of faulting.
bool GuestTryCopy(void* dst, const void* src, size_t size);
}  // namespace skate3::native_scene

REXCVAR_DEFINE_STRING(skate3_button_prompts, "playstation", "Skate 3",
                      "Button prompts: playstation (the PlayStation pictures in hub/button_prompts/playstation) "
                      "or xbox (the game's own)")
    .allowed({"playstation", "xbox"})
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_BOOL(skate3_ps5_dump_hud_pictures, false, "Skate 3",
                    "Save every small menu/HUD picture the game loads, once, as .dds files in logs/hud_pictures "
                    "(to find the button prompts)");

namespace skate3ps5::prompts {
namespace {

namespace xenos = rex::graphics::xenos;
namespace fs = std::filesystem;
using rex::graphics::FormatInfo;
using rex::graphics::TextureInfo;

constexpr uint64_t kHashBasis = 0x14650FB0739D0383ull;
constexpr uint64_t kHashPrime = 0x100000001B3ull;
constexpr uint32_t kMaxLogged = 200;

uint64_t PictureHash(uint32_t width, uint32_t height, const uint8_t* p, size_t n) {
  uint64_t h = kHashBasis ^ (uint64_t(width) << 32 | height);
  for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * kHashPrime;
  return h;
}

// A PlayStation picture: each level's 4x4 blocks in row order (DDS byte order).
struct Picture {
  uint64_t id = 0;        // the Xbox picture it replaces
  uint64_t own_hash = 0;  // its own level 0's hash: the guest texture once it's written
  uint32_t width = 0, height = 0;
  xenos::TextureFormat format = xenos::TextureFormat::k_DXT1;
  std::vector<std::vector<uint8_t>> levels;
};

// Where one level of a guest texture is, in 4x4 blocks.
struct Level {
  uint32_t address = 0;       // physical
  uint32_t pitch = 0;         // blocks per row of the guest layout
  uint32_t offset_x = 0, offset_y = 0;  // (packed mips)
  uint32_t blocks_x = 0, blocks_y = 0;
  uint32_t region_base = 0, region_extent = 0;  // base or mip memory: where this level must stay
  uint32_t span = 0;          // bytes from address to the end of the last block
};

rex::memory::Memory* g_memory = nullptr;
bool g_enabled = false;
std::vector<Picture> g_pictures;
std::unordered_map<uint64_t, size_t> g_by_id;
std::unordered_set<uint64_t> g_own_hashes;
std::vector<std::pair<uint32_t, uint32_t>> g_sizes;
std::vector<xenos::TextureFormat> g_formats;
std::mutex g_mutex;
std::atomic<uint32_t> g_logged{0};
fs::path g_dump_folder;
std::mutex g_dump_mutex;
std::unordered_set<uint64_t> g_dumped;

thread_local std::vector<uint8_t> tl_guest;   // a level's guest memory
thread_local std::vector<uint8_t> tl_linear;  // level 0's blocks in row order

uint8_t* Guest(uint32_t physical) {
  return g_memory->TranslateVirtual<uint8_t*>(0xA0000000u | (physical & 0x1FFFFFFFu));
}

// Tells everything holding a copy of guest memory (the GPU's) that the range
// changed and stops watching it: in each physical view, as each keeps its
// own watches.
void MemoryChanged(uint32_t physical, uint32_t length) {
  physical &= 0x1FFFFFFFu;
  g_memory->TriggerPhysicalMemoryCallbacks(rex::thread::global_critical_region::AcquireDirect(),
                                           0xA0000000u + physical, length, true, true);
  g_memory->TriggerPhysicalMemoryCallbacks(rex::thread::global_critical_region::AcquireDirect(),
                                           0xC0000000u + physical, length, true, true);
  if (physical >= 0x1000) {
    g_memory->TriggerPhysicalMemoryCallbacks(rex::thread::global_critical_region::AcquireDirect(),
                                             0xE0000000u + physical - 0x1000, length, true, true);
  }
}

// The guest's byte order <-> DDS order (each swap undoes itself).
void SwapEndian(uint8_t* p, size_t n, xenos::Endian e) {
  switch (e) {
    case xenos::Endian::k8in16:
      for (size_t i = 0; i + 1 < n; i += 2) std::swap(p[i], p[i + 1]);
      break;
    case xenos::Endian::k8in32:
      for (size_t i = 0; i + 3 < n; i += 4) std::swap(p[i], p[i + 3]), std::swap(p[i + 1], p[i + 2]);
      break;
    case xenos::Endian::k16in32:
      for (size_t i = 0; i + 3 < n; i += 4) std::swap(p[i], p[i + 2]), std::swap(p[i + 1], p[i + 3]);
      break;
    default:
      break;
  }
}

uint32_t BlockOffset(const TextureInfo& info, const Level& l, uint32_t x, uint32_t y, uint32_t log2_bpb) {
  if (info.is_tiled) {
    return uint32_t(rex::graphics::texture_util::GetTiledOffset2D(int32_t(x + l.offset_x), int32_t(y + l.offset_y),
                                                                  l.pitch, log2_bpb));
  }
  return ((y + l.offset_y) * l.pitch + l.offset_x + x) << log2_bpb;
}

// The memory the texture's base and mips cover (the larger of the fetch's
// sizes and the guest layout's).
std::pair<uint32_t, uint32_t> Extents(const xenos::xe_gpu_texture_fetch_t& fetch, const TextureInfo& info) {
  const auto layout = rex::graphics::texture_util::GetGuestTextureLayout(
      xenos::DataDimension::k2DOrStacked, fetch.pitch, info.width + 1, info.height + 1, 1, info.is_tiled != 0,
      info.format, info.has_packed_mips != 0, true, info.memory.mip_address ? uint32_t(fetch.mip_max_level) : 0);
  return {std::max(info.memory.base_size, layout.base.level_data_extent_bytes),
          std::max(info.memory.mip_size, layout.mips_total_extent_bytes)};
}

bool LocateLevel(const TextureInfo& info, std::pair<uint32_t, uint32_t> extents, uint32_t level, Level& out) {
  uint32_t ox = 0, oy = 0;
  const uint32_t address = info.GetMipLocation(level, &ox, &oy, true);
  if (!address) return false;
  const FormatInfo* fi = info.format_info();
  const uint32_t w = std::max(1u, (info.width + 1) >> level);
  const uint32_t h = std::max(1u, (info.height + 1) >> level);
  out.address = address;
  out.pitch = level == 0 ? info.extent.block_pitch_h : info.GetMipExtent(level, true).block_pitch_h;
  out.offset_x = ox;
  out.offset_y = oy;
  out.blocks_x = (w + fi->block_width - 1) / fi->block_width;
  out.blocks_y = (h + fi->block_height - 1) / fi->block_height;
  out.region_base = level == 0 ? info.memory.base_address : info.memory.mip_address;
  out.region_extent = level == 0 ? extents.first : extents.second;
  return out.pitch != 0 && out.region_extent != 0 && address >= out.region_base;
}

// How far the level's blocks reach; false when that's past its memory.
bool MeasureLevel(const TextureInfo& info, Level& l, uint32_t bpb, uint32_t log2_bpb) {
  uint32_t span = 0;
  for (uint32_t y = 0; y < l.blocks_y; ++y)
    for (uint32_t x = 0; x < l.blocks_x; ++x) span = std::max(span, BlockOffset(info, l, x, y, log2_bpb) + bpb);
  l.span = span;
  return uint64_t(l.address - l.region_base) + span <= l.region_extent;
}

// A level's blocks in row order, DDS byte order (tl_linear).
bool ReadLevelLinear(const TextureInfo& info, const Level& l, uint32_t bpb, uint32_t log2_bpb) {
  tl_guest.resize(l.span);
  if (!skate3::native_scene::GuestTryCopy(tl_guest.data(), Guest(l.address), l.span)) return false;
  const size_t row = size_t(l.blocks_x) * bpb;
  tl_linear.resize(row * l.blocks_y);
  for (uint32_t y = 0; y < l.blocks_y; ++y) {
    uint8_t* dst = tl_linear.data() + row * y;
    for (uint32_t x = 0; x < l.blocks_x; ++x)
      std::memcpy(dst + size_t(x) * bpb, tl_guest.data() + BlockOffset(info, l, x, y, log2_bpb), bpb);
    SwapEndian(dst, row, info.endianness);
  }
  return true;
}

const char* FormatName(xenos::TextureFormat f) {
  switch (f) {
    case xenos::TextureFormat::k_DXT1: return "DXT1";
    case xenos::TextureFormat::k_DXT2_3: return "DXT2_3";
    case xenos::TextureFormat::k_DXT4_5: return "DXT4_5";
    case xenos::TextureFormat::k_DXN: return "DXN";
    case xenos::TextureFormat::k_DXT5A: return "DXT5A";
    default: return "other";
  }
}

uint32_t DdsFourCc(xenos::TextureFormat f) {
  switch (f) {
    case xenos::TextureFormat::k_DXT1: return 0x31545844u;    // DXT1
    case xenos::TextureFormat::k_DXT2_3: return 0x33545844u;  // DXT3
    case xenos::TextureFormat::k_DXT4_5: return 0x35545844u;  // DXT5
    case xenos::TextureFormat::k_DXN: return 0x32495441u;     // ATI2
    case xenos::TextureFormat::k_DXT5A: return 0x31495441u;   // ATI1
    default: return 0;
  }
}

// skate3_ps5_dump_hud_pictures: level 0 of a small block-compressed picture, once.
void Dump(const xenos::xe_gpu_texture_fetch_t& fetch, const TextureInfo& info, uint64_t id) {
  const uint32_t fourcc = DdsFourCc(info.format);
  if (!fourcc) return;
  {
    std::lock_guard<std::mutex> lock(g_dump_mutex);
    if (!g_dumped.insert(id).second) return;
  }
  const uint32_t w = info.width + 1, h = info.height + 1;
  uint8_t header[128] = {};
  auto put = [&](size_t off, uint32_t v) { std::memcpy(header + off, &v, 4); };
  std::memcpy(header, "DDS ", 4);
  put(4, 124), put(8, 0x1007 | 0x80000), put(12, h), put(16, w);
  put(20, uint32_t(tl_linear.size())), put(28, 1), put(76, 32), put(80, 4), put(84, fourcc), put(108, 0x1000);
  char name[96];
  std::snprintf(name, sizeof(name), "hud_%04ux%04u_%s_s%03X_%016llX.dds", w, h, FormatName(info.format),
                uint32_t(fetch.swizzle), static_cast<unsigned long long>(id));
  std::error_code ec;
  fs::create_directories(g_dump_folder, ec);
  std::ofstream out(g_dump_folder / name, std::ios::binary);
  out.write(reinterpret_cast<const char*>(header), sizeof(header));
  out.write(reinterpret_cast<const char*>(tl_linear.data()), std::streamsize(tl_linear.size()));
}

bool ReadDds(const fs::path& file, Picture& out) {
  std::ifstream in(file, std::ios::binary);
  std::vector<uint8_t> d((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (d.size() < 128 || std::memcmp(d.data(), "DDS ", 4) != 0) return false;
  auto u32 = [&](size_t off) {
    uint32_t v;
    std::memcpy(&v, d.data() + off, 4);
    return v;
  };
  out.height = u32(12);
  out.width = u32(16);
  const uint32_t mips = std::max(1u, u32(28));
  const uint32_t fourcc = u32(84);
  uint32_t bpb;
  if (fourcc == 0x31545844u) out.format = xenos::TextureFormat::k_DXT1, bpb = 8;
  else if (fourcc == 0x35545844u) out.format = xenos::TextureFormat::k_DXT4_5, bpb = 16;
  else return false;
  if (!out.width || !out.height || (out.width & (out.width - 1)) || (out.height & (out.height - 1))) return false;
  size_t pos = 128;
  for (uint32_t i = 0; i < mips; ++i) {
    const uint32_t w = std::max(1u, out.width >> i), h = std::max(1u, out.height >> i);
    const size_t bytes = size_t((w + 3) / 4) * ((h + 3) / 4) * bpb;
    if (pos + bytes > d.size()) break;
    out.levels.emplace_back(d.begin() + pos, d.begin() + pos + bytes);
    pos += bytes;
  }
  if (out.levels.empty()) return false;
  out.own_hash = PictureHash(out.width, out.height, out.levels[0].data(), out.levels[0].size());
  return true;
}

void OnTextureCacheRead(const xenos::xe_gpu_texture_fetch_t& fetch) { BeforeTextureRead(fetch); }

}  // namespace

void Install(rex::memory::Memory* memory) {
  g_memory = memory;
  const fs::path app = rex::filesystem::GetAppRootFolder();
  const fs::path folder = app / "hub" / "button_prompts" / "playstation";
  std::string setting = REXCVAR_GET(skate3_button_prompts);
  std::transform(setting.begin(), setting.end(), setting.begin(), [](unsigned char c) { return std::tolower(c); });
  const bool playstation = setting == "playstation";
  if (REXCVAR_GET(skate3_ps5_dump_hud_pictures)) g_dump_folder = app / "logs" / "hud_pictures";

  std::error_code ec;
  for (fs::directory_iterator it(folder, ec), end; playstation && !ec && it != end; it.increment(ec)) {
    const fs::path& p = it->path();
    if (p.extension() != ".dds") continue;
    const std::string stem = p.stem().string();
    char* endp = nullptr;
    const uint64_t id = std::strtoull(stem.c_str(), &endp, 16);
    if (stem.size() != 16 || !endp || *endp) continue;
    Picture pic;
    if (!ReadDds(p, pic)) {
      REXLOG_WARN("button prompts: {} isn't a DXT1/DXT5 DDS of a power-of-two size", p.string());
      continue;
    }
    pic.id = id;
    g_by_id[id] = g_pictures.size();
    g_own_hashes.insert(pic.own_hash);
    const std::pair<uint32_t, uint32_t> size{pic.width, pic.height};
    if (std::find(g_sizes.begin(), g_sizes.end(), size) == g_sizes.end()) g_sizes.push_back(size);
    if (std::find(g_formats.begin(), g_formats.end(), pic.format) == g_formats.end()) g_formats.push_back(pic.format);
    g_pictures.push_back(std::move(pic));
  }
  g_enabled = playstation && !g_pictures.empty();
  if (playstation && g_pictures.empty()) {
    REXLOG_INFO("button prompts: no {} (the game's own buttons)", folder.string());
  }
  REXLOG_INFO("button prompts: {} PlayStation pictures in {}; the setting says {}", g_pictures.size(),
              folder.string(), playstation ? "PlayStation" : "Xbox");
  if (g_enabled || !g_dump_folder.empty()) {
    rex::graphics::TextureCache::SetBeforeTextureReadCallback(&OnTextureCacheRead);
  }
}

bool BeforeTextureRead(const xenos::xe_gpu_texture_fetch_t& fetch) {
  const bool dump = !g_dump_folder.empty();
  if ((!g_enabled && !dump) || !g_memory) return false;
  if (fetch.type != xenos::FetchConstantType::kTexture || fetch.base_address == 0 || fetch.stacked ||
      fetch.dimension != xenos::DataDimension::k2DOrStacked) {
    return false;
  }
  const uint32_t width = fetch.size_2d.width + 1, height = fetch.size_2d.height + 1;
  const auto format = rex::graphics::GetBaseFormat(xenos::TextureFormat(uint32_t(fetch.format)));
  bool candidate = g_enabled &&
                   std::find(g_sizes.begin(), g_sizes.end(), std::make_pair(width, height)) != g_sizes.end() &&
                   std::find(g_formats.begin(), g_formats.end(), format) != g_formats.end();
  const bool dump_this = dump && width <= 256 && height <= 256 && DdsFourCc(format) != 0;
  if (!candidate && !dump_this) return false;

  TextureInfo info;
  if (!TextureInfo::Prepare(fetch, &info) || info.is_stacked) return false;
  const FormatInfo* fi = info.format_info();
  if (!fi || fi->block_width != 4 || fi->block_height != 4) return false;
  const uint32_t bpb = fi->bytes_per_block();
  if (bpb != 8 && bpb != 16) return false;
  const uint32_t log2_bpb = bpb == 16 ? 4 : 3;

  const auto t0 = std::chrono::steady_clock::now();
  std::unique_lock<std::mutex> lock(g_mutex);
  const auto extents = Extents(fetch, info);
  Level base;
  if (!LocateLevel(info, extents, 0, base) || !MeasureLevel(info, base, bpb, log2_bpb) ||
      !ReadLevelLinear(info, base, bpb, log2_bpb)) {
    return false;
  }
  const uint64_t id = PictureHash(width, height, tl_linear.data(), tl_linear.size());
  if (dump_this) Dump(fetch, info, id);
  if (!candidate) return false;
  if (g_own_hashes.count(id)) return true;  // (already the PlayStation picture)
  const auto found = g_by_id.find(id);
  if (found == g_by_id.end()) return false;
  const Picture& pic = g_pictures[found->second];
  if (pic.width != width || pic.height != height || pic.format != format ||
      pic.levels[0].size() != tl_linear.size()) {
    return false;
  }

  // Every level both have, each where the guest keeps it.
  std::vector<std::pair<Level, const std::vector<uint8_t>*>> levels;
  levels.emplace_back(base, &pic.levels[0]);
  if (info.memory.mip_address) {
    const uint32_t count = std::min(info.mip_max_level - info.mip_min_level + 1, info.GetMaxMipLevels());
    for (uint32_t i = 1; i < count && i < pic.levels.size(); ++i) {
      Level l;
      if (!LocateLevel(info, extents, i, l)) break;
      if (!MeasureLevel(info, l, bpb, log2_bpb) ||
          pic.levels[i].size() != size_t(l.blocks_x) * l.blocks_y * bpb) {
        return false;
      }
      levels.emplace_back(l, &pic.levels[i]);
    }
  }
  for (const auto& [l, data] : levels) {
    tl_guest.resize(l.span);
    if (!skate3::native_scene::GuestTryCopy(tl_guest.data(), Guest(l.address), l.span)) return false;
  }
  // (unwatch the pages first: the GPU's copy of them goes stale and the write
  // doesn't fault on a watched page)
  for (const auto& [l, data] : levels) {
    MemoryChanged(l.address, l.span);
  }
  // (each block through the guarded copy: a page the game has made
  // read-only, or freed meanwhile, stops the swap instead of the game)
  std::vector<uint8_t> row;
  bool written = true;
  for (const auto& [l, data] : levels) {
    if (!written) break;
    uint8_t* guest = Guest(l.address);
    const size_t row_bytes = size_t(l.blocks_x) * bpb;
    row.resize(row_bytes);
    for (uint32_t y = 0; y < l.blocks_y && written; ++y) {
      std::memcpy(row.data(), data->data() + row_bytes * y, row_bytes);
      SwapEndian(row.data(), row_bytes, info.endianness);
      for (uint32_t x = 0; x < l.blocks_x; ++x) {
        if (!skate3::native_scene::GuestTryCopy(guest + BlockOffset(info, l, x, y, log2_bpb),
                                                row.data() + size_t(x) * bpb, bpb)) {
          written = false;
          break;
        }
      }
    }
  }
  lock.unlock();
  for (const auto& [l, data] : levels) {
    MemoryChanged(l.address, l.span);
  }
  if (!written) {
    REXLOG_WARN("button prompts: {:016X} at {:08X} couldn't be written (the game's memory there isn't writable)",
                id, base.address);
    return false;
  }
  if (g_logged.fetch_add(1) < kMaxLogged) {
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    REXLOG_INFO("button prompts: {:016X} -> PlayStation at {:08X} ({}x{}, {} levels, {}, {:.2f} ms)", id,
                base.address, width, height, levels.size(), info.is_tiled ? "tiled" : "linear", ms);
  }
  return true;
}

}  // namespace skate3ps5::prompts
