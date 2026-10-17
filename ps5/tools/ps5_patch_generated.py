#!/usr/bin/env python3
"""Skate 3 for PS5: fix up the generated code for the PS5 after codegen.

Codegen runs on the build machine (a host build's generate-all) and writes the
same C++ for every platform; two things in its header template need the PS5:
- the 0x1000 host offset for guest addresses from 0xE0000000 (16 KB pages,
  as on Windows and arm64 macOS);
- the guest's interrupt-state check (REX_CHECK_GLOBAL_LOCK) without taking
  the global lock (the SDK patch's template has both already).
Idempotent: run it after every codegen.

    python3 ps5/tools/ps5_patch_generated.py [generated folder]
"""
import pathlib, sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "generated")
old = "#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && REX_ARCH_ARM64)\n"
new = "#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && REX_ARCH_ARM64) || REX_PLATFORM_PS5\n"
OLD_CHECK = r"""#define REX_CHECK_GLOBAL_LOCK()                                        \
  ([&]() -> u64 {                                                      \
    auto lock_ = rex::thread::global_critical_region::AcquireDirect(); \
    return ppc_global_lock_count_().load() ? 0 : 0x8000;               \
  }())"""
NEW_CHECK = r"""#if REX_PLATFORM_PS5
// [ps5 port] Without taking the lock: the region is "entered" only on the
// thread that holds it (another thread holding it: interrupts are on here).
#define REX_CHECK_GLOBAL_LOCK()                                                \
  ((rex::thread::global_critical_region::mutex().owned_by_current_thread() && \
    ppc_global_lock_count_().load())                                           \
       ? u64(0)                                                                \
       : u64(0x8000))
#else
#define REX_CHECK_GLOBAL_LOCK()                                        \
  ([&]() -> u64 {                                                      \
    auto lock_ = rex::thread::global_critical_region::AcquireDirect(); \
    return ppc_global_lock_count_().load() ? 0 : 0x8000;               \
  }())
#endif"""
headers = [root / "skate3_init.h", root / "eawebkit" / "eawebkit_init.h"]
for h in headers:
    if not h.exists():
        sys.exit(f"{h}: not found (run generate-all with a host build first)")
    t = h.read_text()
    if new in t:
        print(f"{h}: host offset already patched")
    elif old in t:
        t = t.replace(old, new)
        print(f"{h}: 0xE0000000 host offset on the PS5")
    else:
        sys.exit(f"{h}: the host offset line wasn't found")
    if NEW_CHECK in t:
        print(f"{h}: lock check already patched")
    elif OLD_CHECK in t:
        t = t.replace(OLD_CHECK, NEW_CHECK)
        print(f"{h}: lock check without the lock on the PS5")
    else:
        sys.exit(f"{h}: REX_CHECK_GLOBAL_LOCK wasn't found")
    h.write_text(t)
