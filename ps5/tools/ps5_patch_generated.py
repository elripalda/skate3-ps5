#!/usr/bin/env python3
"""Skate 3 for PS5: fix up the generated code for the PS5 after codegen.

Codegen runs on the build machine (a host build's generate-all) and writes the
same C++ for every platform, but its header template only gives Windows and
arm64 macOS the 0x1000 host offset for guest addresses from 0xE0000000 (16 KB
pages). The PS5 has 16 KB pages too. Idempotent: run it after every codegen.

    python3 ps5/tools/ps5_patch_generated.py [generated folder]
"""
import pathlib, sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "generated")
old = "#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && REX_ARCH_ARM64)\n"
new = "#if REX_PLATFORM_WIN32 || (REX_PLATFORM_MAC && REX_ARCH_ARM64) || REX_PLATFORM_PS5\n"
headers = [root / "skate3_init.h", root / "eawebkit" / "eawebkit_init.h"]
for h in headers:
    if not h.exists():
        sys.exit(f"{h}: not found (run generate-all with a host build first)")
    t = h.read_text()
    if new in t:
        print(f"{h}: already patched")
    elif old in t:
        h.write_text(t.replace(old, new))
        print(f"{h}: 0xE0000000 host offset on the PS5")
    else:
        sys.exit(f"{h}: the host offset line wasn't found")
