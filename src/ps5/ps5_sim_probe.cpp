// Skate 3 for PS5 - the game's simulation speed in the log (as 1.0 had it).
//
// Every 10 s one line: how many frames the game made, how many simulation
// ticks it ran and how much game time they covered, as a percentage of real
// time ("game speed 100%" = the game runs at its normal speed), plus the
// game's own frame time and its input queue. A log alone then shows whether
// the game ran slow.
//
// Counted by wrapping a few of the game's functions (addresses: Title
// Update 3):
//   826D78B0  the game's frame                    -> game frames
//   826D77E8  frame timing (r3+0x78: frame ms)    -> frame time avg/max
//   8285C928  "is a sim tick due?" (r3 bit 0 / 1) -> tick due, flag 2
//   8285A3C0  sim tick (r4 bit 0: runs)           -> ticks run
//   827442B0  input queue push (r3: taken)        -> inputs pushed
//   82744330  input queue pop (r3: got one)       -> inputs popped
//   82898920  mode input (r4: on)                 -> mode input
//   82857EC8  SetSimRate                          -> logged when it changes
// and the sim object (pointer at 830CFD94): +0x10 tick counter, +0x7C tick
// length (s), +0x80 slow-motion ticks left.
#include "ps5_sim_probe.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <thread>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>

REXCVAR_DEFINE_BOOL(skate3_ps5_sim_report, true, "Skate 3",
                    "Every 10 s, the game's simulation speed in the log (game speed 100% = normal speed)");

namespace skate3::native_scene {
// (skate3_native_guest_read.h) Guest memory copy that fails instead of faulting.
bool GuestTryCopy(void* dst, const void* src, size_t size);
}  // namespace skate3::native_scene

namespace {

constexpr uint32_t kSimObjectPointer = 0x830CFD94;

std::atomic<uint64_t> g_frames{0}, g_swaps{0};
std::atomic<uint64_t> g_due_asks{0}, g_due{0}, g_due_flag2{0};
std::atomic<uint64_t> g_tick_calls{0}, g_ticks_run{0};
std::atomic<uint64_t> g_pushes{0}, g_pushes_taken{0}, g_pops{0}, g_pops_got{0};
std::atomic<uint64_t> g_mode_inputs{0}, g_mode_inputs_on{0};
std::atomic<uint32_t> g_mode_object{0};
std::atomic<uint32_t> g_set_sim_rate_logged{0};
std::mutex g_frame_time_mutex;
double g_frame_time_sum = 0, g_frame_time_max = 0;
uint64_t g_frame_time_count = 0;
uint8_t* g_base = nullptr;
std::once_flag g_started;

bool Enabled() { return REXCVAR_GET(skate3_ps5_sim_report); }

uint8_t* Host(uint8_t* base, uint32_t address) {
  return base + address + (address >= 0xE0000000u ? 0x1000u : 0u);
}
// (on the game's thread, right after the game used the object)
uint32_t LoadU32(uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, Host(base, address), 4);
  return __builtin_bswap32(v);
}
float LoadF32(uint8_t* base, uint32_t address) {
  const uint32_t bits = LoadU32(base, address);
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}
// (from the report thread: the object may be gone)
bool TryU32(uint32_t address, uint32_t& out) {
  if (address < 0x10000u) return false;
  uint32_t v;
  if (!skate3::native_scene::GuestTryCopy(&v, Host(g_base, address), 4)) return false;
  out = __builtin_bswap32(v);
  return true;
}

void Reporter() {
  using Clock = std::chrono::steady_clock;
  auto last = Clock::now();
  struct Counts {
    uint64_t frames, swaps, due_asks, due, due_flag2, tick_calls, ticks_run, pushes, pushes_taken, pops, pops_got,
        mode_inputs, mode_inputs_on;
  };
  auto snapshot = [] {
    return Counts{g_frames.load(),     g_swaps.load(),        g_due_asks.load(), g_due.load(),
                  g_due_flag2.load(),  g_tick_calls.load(),   g_ticks_run.load(), g_pushes.load(),
                  g_pushes_taken.load(), g_pops.load(),       g_pops_got.load(), g_mode_inputs.load(),
                  g_mode_inputs_on.load()};
  };
  Counts prev = snapshot();
  bool have_ticks = false;
  uint32_t prev_ticks = 0;
  for (;;) {
    std::this_thread::sleep_for(std::chrono::seconds(10));
    const auto now = Clock::now();
    const double secs = std::chrono::duration<double>(now - last).count();
    last = now;
    const Counts c = snapshot();
    auto rate = [secs](uint64_t a, uint64_t b) { return double(a - b) / secs; };

    // the sim object
    uint32_t sim = 0, ticks = 0, dt_bits = 0, slow_mo = 0;
    const bool have_sim = TryU32(kSimObjectPointer, sim) && sim && TryU32(sim + 0x10, ticks) &&
                          TryU32(sim + 0x7C, dt_bits) && TryU32(sim + 0x80, slow_mo);
    float dt = 0;
    std::memcpy(&dt, &dt_bits, 4);
    const double tick_rate = (have_sim && have_ticks) ? double(uint32_t(ticks - prev_ticks)) / secs : 0.0;
    have_ticks = have_sim;
    prev_ticks = ticks;

    double frame_avg = 0, frame_max = 0;
    {
      std::lock_guard<std::mutex> lock(g_frame_time_mutex);
      frame_avg = g_frame_time_count ? g_frame_time_sum / double(g_frame_time_count) : 0.0;
      frame_max = g_frame_time_max;
      g_frame_time_sum = g_frame_time_max = 0;
      g_frame_time_count = 0;
    }
    uint32_t mode = 0, provider_vt = 0;
    if (const uint32_t mode_object = g_mode_object.load()) {
      uint32_t provider = 0;
      TryU32(mode_object + 0x10, mode);
      if (TryU32(mode_object + 0x0C, provider) && provider) TryU32(provider, provider_vt);
    }
    REXLOG_INFO(
        "ps5 sim: game frames {:.1f}/s, tick due {:.1f}/s of {:.1f} asks (flag 2: {:.1f}/s), ticks run {:.1f}/s "
        "(of {:.1f} calls), tick counter {:.1f}/s => game speed {:.0f}% | sim dt {:.3f} ms, slow-mo ticks left {} | "
        "game's frame time avg {:.1f} ms max {:.1f} ms | inputs pushed {:.1f}/s (taken {:.1f}), popped {:.1f}/s "
        "(got {:.1f}), mode input {:.1f}/s (on {:.1f}) | guest swaps {:.1f}/s | mode {} provider vt {:08X}",
        rate(c.frames, prev.frames), rate(c.due, prev.due), rate(c.due_asks, prev.due_asks),
        rate(c.due_flag2, prev.due_flag2), rate(c.ticks_run, prev.ticks_run), rate(c.tick_calls, prev.tick_calls),
        tick_rate, double(dt) * tick_rate * 100.0, double(dt) * 1000.0, slow_mo, frame_avg, frame_max,
        rate(c.pushes, prev.pushes), rate(c.pushes_taken, prev.pushes_taken), rate(c.pops, prev.pops),
        rate(c.pops_got, prev.pops_got), rate(c.mode_inputs, prev.mode_inputs),
        rate(c.mode_inputs_on, prev.mode_inputs_on), rate(c.swaps, prev.swaps), mode, provider_vt);
    prev = c;
  }
}

void Start(uint8_t* base) {
  std::call_once(g_started, [base] {
    g_base = base;
    std::thread(Reporter).detach();
    REXLOG_INFO("ps5 sim: probe started");
  });
}

}  // namespace

namespace skate3ps5::simprobe {
void OnGuestSwap() {
  if (Enabled()) g_swaps.fetch_add(1, std::memory_order_relaxed);
}
}  // namespace skate3ps5::simprobe

extern "C" {
REX_EXTERN(__imp__sub_826D78B0);
REX_EXTERN(__imp__sub_826D77E8);
REX_EXTERN(__imp__sub_8285C928);
REX_EXTERN(__imp__sub_8285A3C0);
REX_EXTERN(__imp__sub_827442B0);
REX_EXTERN(__imp__sub_82744330);
REX_EXTERN(__imp__sub_82898920);
REX_EXTERN(__imp__sub_82857EC8);

// The game's frame.
REX_FUNC(sub_826D78B0) {
  if (Enabled()) {
    Start(base);
    g_frames.fetch_add(1, std::memory_order_relaxed);
    if (const uint32_t self = ctx.r3.u32) g_mode_object.store(LoadU32(base, self + 4), std::memory_order_relaxed);
  }
  __imp__sub_826D78B0(ctx, base);
}

// Frame timing: the frame's length (ms) at r3+0x78 afterwards.
REX_FUNC(sub_826D77E8) {
  if (!Enabled()) {
    __imp__sub_826D77E8(ctx, base);
    return;
  }
  const uint32_t self = ctx.r3.u32;
  __imp__sub_826D77E8(ctx, base);
  if (!self) return;
  const double ms = LoadF32(base, self + 0x78);
  std::lock_guard<std::mutex> lock(g_frame_time_mutex);
  g_frame_time_sum += ms;
  g_frame_time_max = std::max(g_frame_time_max, ms);
  ++g_frame_time_count;
}

// Is a sim tick due? (r3 bit 0: yes; bit 1: the other flag)
REX_FUNC(sub_8285C928) {
  __imp__sub_8285C928(ctx, base);
  if (!Enabled()) return;
  g_due_asks.fetch_add(1, std::memory_order_relaxed);
  const uint32_t r = ctx.r3.u32;
  if (r & 1) g_due.fetch_add(1, std::memory_order_relaxed);
  if (r & 2) g_due_flag2.fetch_add(1, std::memory_order_relaxed);
}

// A sim tick (r4 bit 0: it runs).
REX_FUNC(sub_8285A3C0) {
  if (Enabled()) {
    g_tick_calls.fetch_add(1, std::memory_order_relaxed);
    if (ctx.r4.u32 & 1) g_ticks_run.fetch_add(1, std::memory_order_relaxed);
  }
  __imp__sub_8285A3C0(ctx, base);
}

// Input queue push (r3: taken).
REX_FUNC(sub_827442B0) {
  __imp__sub_827442B0(ctx, base);
  if (!Enabled()) return;
  g_pushes.fetch_add(1, std::memory_order_relaxed);
  if (ctx.r3.u32) g_pushes_taken.fetch_add(1, std::memory_order_relaxed);
}

// Input queue pop (r3: got one).
REX_FUNC(sub_82744330) {
  __imp__sub_82744330(ctx, base);
  if (!Enabled()) return;
  g_pops.fetch_add(1, std::memory_order_relaxed);
  if (ctx.r3.u32) g_pops_got.fetch_add(1, std::memory_order_relaxed);
}

// Mode input (r4: on).
REX_FUNC(sub_82898920) {
  if (Enabled()) {
    g_mode_inputs.fetch_add(1, std::memory_order_relaxed);
    if (ctx.r4.u32 & 0xFF) g_mode_inputs_on.fetch_add(1, std::memory_order_relaxed);
  }
  __imp__sub_82898920(ctx, base);
}

// SetSimRate: r4 -> the request (+0x10 tick length in s, +0x14 ticks); the
// sim object (r3) has the result at +0x7C / +0x80.
REX_FUNC(sub_82857EC8) {
  if (!Enabled()) {
    __imp__sub_82857EC8(ctx, base);
    return;
  }
  const uint32_t self = ctx.r3.u32, request = ctx.r4.u32;
  const float asked = request ? LoadF32(base, request + 0x10) : 0.0f;
  const uint32_t asked_ticks = request ? LoadU32(base, request + 0x14) : 0;
  __imp__sub_82857EC8(ctx, base);
  if (!self || g_set_sim_rate_logged.fetch_add(1) >= 100) return;
  REXLOG_INFO("ps5 sim: SetSimRate asked {:.3f} ms for {} ticks -> dt {:.3f} ms, {} ticks", asked * 1000.0f,
              asked_ticks, LoadF32(base, self + 0x7C) * 1000.0f, LoadU32(base, self + 0x80));
}
}  // extern "C"
