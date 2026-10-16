// Skate 3 for PS5 - PlayStation button prompts (see ps5_button_prompts.h).
#include "ps5_button_prompts.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <rex/cvar.h>
#include <rex/graphics/pipeline/texture/info.h>
#include <rex/graphics/pipeline/texture/util.h>
#include <rex/graphics/xenos.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

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

// A PlayStation picture: the DDS's blocks (DXT1 / DXT5, little-endian, level 0
// first, then its mips).
struct Picture {
  uint32_t width = 0, height = 0, levels = 0;
  bool dxt5 = false;
  std::vector<uint8_t> data;
};

std::mutex g_mutex;
fs::path g_folder;          // hub/button_prompts/playstation
bool g_enabled = false;
std::unordered_map<uint64_t, fs::path> g_files;   // id -> its .dds
std::unordered_map<uint64_t, Picture> g_loaded;   // (read on first use)
std::unordered_set<uint64_t> g_seen;              // (guest address << 32 | size) already looked at
fs::path g_dump_folder;
std::unordered_set<uint64_t> g_dumped;

uint64_t FnvBytes(const uint8_t* p, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
  return h;
}
// (the same hash over 8-byte words, as the native renderer fingerprints guest data)
uint64_t FnvQwords(const uint8_t* p, size_t n) {
  uint64_t h = 1469598103934665603ull;
  for (size_t i = 0; i + 8 <= n; i += 8) {
    uint64_t v;
    std::memcpy(&v, p + i, 8);
    h = (h ^ v) * 1099511628211ull;
  }
  return h;
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
  out.levels = std::max(1u, u32(28));
  const uint32_t fourcc = u32(84);
  if (fourcc == 0x31545844u) out.dxt5 = false;       // "DXT1"
  else if (fourcc == 0x35545844u) out.dxt5 = true;   // "DXT5"
  else return false;
  if (!out.width || !out.height || (out.width & (out.width - 1)) || (out.height & (out.height - 1))) return false;
  out.data.assign(d.begin() + 128, d.end());
  const size_t bpb = out.dxt5 ? 16 : 8;
  const size_t level0 = size_t(std::max(1u, out.width / 4)) * std::max(1u, out.height / 4) * bpb;
  return out.data.size() >= level0;
}

// Byte order of the guest's texture data (the fetch constant's endianness).
void ToGuestEndian(uint8_t* p, size_t n, xenos::Endian e) {
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

// Level 0's blocks in the guest's layout: (bx, by) -> byte offset.
uint32_t BlockOffset(const rex::graphics::TextureInfo& info, uint32_t bx, uint32_t by, uint32_t bpb) {
  const uint32_t pitch = info.extent.block_pitch_h;
  if (info.is_tiled) {
    const uint32_t log2 = bpb == 16 ? 4 : 3;
    return uint32_t(rex::graphics::texture_util::GetTiledOffset2D(int32_t(bx), int32_t(by), pitch, log2));
  }
  return (by * pitch + bx) * bpb;
}

void Dump(const rex::graphics::TextureInfo& info, const uint8_t* guest, uint64_t id) {
  if (g_dump_folder.empty() || !g_dumped.insert(id).second) return;
  const bool dxt5 = info.format == xenos::TextureFormat::k_DXT4_5 || info.format == xenos::TextureFormat::k_DXT2_3;
  const uint32_t bpb = dxt5 ? 16 : 8;
  const uint32_t bw = std::max(1u, info.width / 4), bh = std::max(1u, info.height / 4);
  std::vector<uint8_t> blocks(size_t(bw) * bh * bpb);
  for (uint32_t by = 0; by < bh; ++by)
    for (uint32_t bx = 0; bx < bw; ++bx) {
      const uint32_t off = BlockOffset(info, bx, by, bpb);
      if (off + bpb > info.memory.base_size) return;
      std::memcpy(blocks.data() + (size_t(by) * bw + bx) * bpb, guest + off, bpb);
    }
  ToGuestEndian(blocks.data(), blocks.size(), info.endianness);  // (the same swap undoes itself)
  uint8_t header[128] = {};
  auto put = [&](size_t off, uint32_t v) { std::memcpy(header + off, &v, 4); };
  std::memcpy(header, "DDS ", 4);
  put(4, 124), put(8, 0x1007 | 0x80000), put(12, info.height), put(16, info.width);
  put(20, uint32_t(blocks.size())), put(28, 1), put(76, 32), put(80, 4);
  put(84, dxt5 ? 0x35545844u : 0x31545844u), put(108, 0x1000);
  char name[96];
  std::snprintf(name, sizeof(name), "hud_%ux%u_%s_s%03X_%016llX.dds", info.width, info.height, dxt5 ? "DXT5" : "DXT1",
                0u, static_cast<unsigned long long>(id));
  std::error_code ec;
  fs::create_directories(g_dump_folder, ec);
  std::ofstream out(g_dump_folder / name, std::ios::binary);
  out.write(reinterpret_cast<const char*>(header), sizeof(header));
  out.write(reinterpret_cast<const char*>(blocks.data()), std::streamsize(blocks.size()));
}

}  // namespace

void Init(const fs::path& app_folder) {
  std::lock_guard<std::mutex> lock(g_mutex);
  const std::string setting = REXCVAR_GET(skate3_button_prompts);
  g_folder = app_folder / "hub" / "button_prompts" / "playstation";
  g_files.clear();
  std::error_code ec;
  for (fs::directory_iterator it(g_folder, ec), end; !ec && it != end; it.increment(ec)) {
    const fs::path& p = it->path();
    if (p.extension() != ".dds") continue;
    const std::string stem = p.stem().string();
    char* endp = nullptr;
    const uint64_t id = std::strtoull(stem.c_str(), &endp, 16);
    if (endp && *endp == 0 && stem.size() == 16) g_files[id] = p;
  }
  g_enabled = setting == "playstation" && !g_files.empty();
  if (REXCVAR_GET(skate3_ps5_dump_hud_pictures)) g_dump_folder = app_folder / "logs" / "hud_pictures";
  REXLOG_INFO("button prompts: {} PlayStation pictures in {}; the setting says {}", g_files.size(), g_folder.string(),
              setting == "playstation" ? "PlayStation" : "Xbox");
  if (setting == "playstation" && g_files.empty()) {
    REXLOG_INFO("button prompts: no {} (the game's own buttons)", g_folder.string());
  }
}

void OnTextureLoaded(uint32_t texture_object) {
  if ((!g_enabled && g_dump_folder.empty()) || texture_object == 0) return;
  auto* memory = rex::system::kernel_memory();
  if (!memory) return;
  // The texture object's fetch constant: six words at +0x1C (big-endian).
  uint32_t words[6];
  const uint8_t* obj = memory->TranslateVirtual<const uint8_t*>(texture_object + 0x1C);
  for (int i = 0; i < 6; ++i) {
    uint32_t w;
    std::memcpy(&w, obj + i * 4, 4);
    words[i] = __builtin_bswap32(w);
  }
  xenos::xe_gpu_texture_fetch_t fetch = {};
  fetch.dword_0 = words[0], fetch.dword_1 = words[1], fetch.dword_2 = words[2];
  fetch.dword_3 = words[3], fetch.dword_4 = words[4], fetch.dword_5 = words[5];
  if (fetch.type != xenos::FetchConstantType::kTexture || fetch.base_address == 0) return;
  const auto format = xenos::TextureFormat(uint32_t(fetch.format));
  const bool dxt5 = format == xenos::TextureFormat::k_DXT4_5 || format == xenos::TextureFormat::k_DXT2_3;
  if (!dxt5 && format != xenos::TextureFormat::k_DXT1) return;
  rex::graphics::TextureInfo info;
  if (!rex::graphics::TextureInfo::Prepare(fetch, &info)) return;
  // (button pictures are small: 32..128 pixels)
  if (info.width < 32 || info.width > 128 || info.height < 32 || info.height > 128 ||
      info.dimension != xenos::DataDimension::k2DOrStacked || info.is_stacked || !info.memory.base_size ||
      info.memory.base_size > 256 * 1024) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  const uint64_t seen_key = uint64_t(info.memory.base_address) << 32 | info.memory.base_size;
  uint8_t* guest = memory->TranslatePhysical(info.memory.base_address);
  const uint32_t bpb = dxt5 ? 16 : 8;
  const uint32_t bw = std::max(1u, info.width / 4), bh = std::max(1u, info.height / 4);
  const size_t tight = size_t(bw) * bh * bpb;

  // The id: FNV-1a 64 of the Xbox picture. (Which bytes the first builds
  // hashed is matched against the pictures' names: the guest's whole level-0
  // memory, or its blocks in order; bytes or 8-byte words.)
  std::vector<uint8_t> linear(tight);
  bool linear_ok = true;
  for (uint32_t by = 0; by < bh && linear_ok; ++by)
    for (uint32_t bx = 0; bx < bw; ++bx) {
      const uint32_t off = BlockOffset(info, bx, by, bpb);
      if (off + bpb > info.memory.base_size) {
        linear_ok = false;
        break;
      }
      std::memcpy(linear.data() + (size_t(by) * bw + bx) * bpb, guest + off, bpb);
    }
  const std::array<uint64_t, 6> ids = {
      FnvBytes(guest, info.memory.base_size),
      linear_ok ? FnvBytes(linear.data(), tight) : 0,
      FnvQwords(guest, info.memory.base_size),
      linear_ok ? FnvQwords(linear.data(), tight) : 0,
      FnvBytes(guest, std::min<size_t>(tight, info.memory.base_size)),
      FnvQwords(guest, std::min<size_t>(tight, info.memory.base_size)),
  };
  if (!g_seen.insert(seen_key ^ ids[0]).second) return;  // (the same picture at the same place: done)
  Dump(info, guest, ids[1] ? ids[1] : ids[0]);
  if (!g_enabled) return;

  int variant = -1;
  uint64_t id = 0;
  for (size_t k = 0; k < ids.size(); ++k) {
    if (ids[k] && g_files.count(ids[k])) {
      variant = int(k);
      id = ids[k];
      break;
    }
  }
  if (variant < 0) return;

  const auto t0 = std::chrono::steady_clock::now();
  auto it = g_loaded.find(id);
  if (it == g_loaded.end()) {
    Picture pic;
    if (!ReadDds(g_files[id], pic)) {
      REXLOG_WARN("button prompts: {} isn't a DXT1/DXT5 DDS of a power-of-two size", g_files[id].string());
      g_files.erase(id);
      return;
    }
    it = g_loaded.emplace(id, std::move(pic)).first;
  }
  const Picture& pic = it->second;
  if (pic.width != info.width || pic.height != info.height || pic.dxt5 != dxt5) {
    REXLOG_WARN("button prompts: {:016X} is {}x{} {}, the game's picture {}x{} {}", id, pic.width, pic.height,
                pic.dxt5 ? "DXT5" : "DXT1", info.width, info.height, dxt5 ? "DXT5" : "DXT1");
    return;
  }
  // Level 0 over the Xbox picture, block by block, in the guest's layout and byte order.
  for (uint32_t by = 0; by < bh; ++by)
    for (uint32_t bx = 0; bx < bw; ++bx) {
      const uint32_t off = BlockOffset(info, bx, by, bpb);
      if (off + bpb > info.memory.base_size) continue;
      uint8_t block[16];
      std::memcpy(block, pic.data.data() + (size_t(by) * bw + bx) * bpb, bpb);
      ToGuestEndian(block, bpb, info.endianness);
      std::memcpy(guest + off, block, bpb);
    }
  const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  REXLOG_INFO("button prompts: {:016X} -> PlayStation at {:08X} ({}x{}, {} levels, {}, {:.2f} ms; hash {})", id,
              info.memory.base_address, info.width, info.height, info.mip_levels(), info.is_tiled ? "tiled" : "linear",
              ms, variant);
}

}  // namespace skate3ps5::prompts
