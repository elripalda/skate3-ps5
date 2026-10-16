# Building Skate 3 for PS5

This is the source of the PS5 app: Skate 3 Recomp 2.0.2 (mchughalex) with the
PS5 port on top. You need your own copy of the game to build it, because the
recompiled code is generated from the game's executable.

Built on Linux (Ubuntu 24.04). Nothing here runs on Windows.

## What you need

- clang 19 and lld 19, CMake 3.25+, Ninja, Python 3, git
- Meson, flex, bison, glslang and the Python packages Mesa wants (for RADV)
- From your copy of the game: `default.xex` and `data/webkit/EAWebkit.xex`
- From Title Update 3: `default.xexp` and `data/webkit/EAWebkit.xexp`
  (an installed app's `Assets` folder has all four)

## 1. Source and SDK

```
git clone --recursive https://github.com/elripalda/skate3-ps5
cd skate3-ps5/third_party/rexglue-sdk
git apply ../../ps5/patches/rexglue-sdk-ps5.patch
git -C thirdparty/FFmpeg apply ../../patches/ffmpeg-ps5.patch
git -C thirdparty/imgui fetch --tags && git -C thirdparty/imgui checkout v1.92.5
git -C thirdparty/imgui apply ../../../../ps5/patches/imgui-rasterizer-gamma.patch
cd ../..
```

`third_party/rexglue-sdk` is mchughalex's Skate 3 branch of ReXGlue
(`skate3-sdk-clean`, 7eb0faf). The patch adds the PS5 runtime: the display,
DualSense, audio out, guest memory in direct memory, fibers, the exception
handler and the pad dialogs. ImGui is pinned to a commit that isn't on GitHub
any more, so it's v1.92.5 plus the one function the SDK needs.

## 2. PS5 toolchain

The compiler, libraries, RADV (Vulkan) and the eboot signer come from Nazky's
[ps5-plume-triangle](https://github.com/Nazky/ps5-plume-triangle):

```
git clone https://github.com/Nazky/ps5-plume-triangle ~/ps5-plume-triangle
cd ~/ps5-plume-triangle
tools/build.sh
```

That sets up the PS5 payload SDK and RADV (mihawk-99/PS5_Mesa 0b2d6d1) in
`.deps/`, and builds what the packaging uses: `app_crt.o`, the stubs,
`ps5-native-tool` and `libc.prx`. (It builds their triangle demo too.)

## 3. Generate the recompiled code (on the build machine)

```
cmake -S . -B out/host -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-19 -DCMAKE_CXX_COMPILER=clang++-19 -DCMAKE_LINKER=ld.lld-19 \
  -DSKATE3_GAME_DATA_ROOT=/path/to/game \
  -DSKATE3_TITLE_UPDATE_XEXP_ROOT=/path/to/tu-xexp
ninja -C out/host generate-all
python3 ps5/tools/ps5_patch_generated.py generated
```

`/path/to/game` holds `default.xex` and `data/webkit/EAWebkit.xex`;
`/path/to/tu-xexp` holds `default.xexp` and `data/webkit/EAWebkit.xexp`.
This writes `generated/` (about 290 MB of C++). Never commit it.

## 4. Build for the PS5

```
SDK=~/ps5-plume-triangle/.deps/native/ps5-payload-sdk
cmake -S . -B out/ps5 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$PWD/ps5/cmake/ps5-toolchain.cmake -DPS5_PAYLOAD_SDK=$SDK \
  -DREXSDK_DIR=$PWD/third_party/rexglue-sdk \
  -DSKATE3_GAME_DATA_ROOT=/path/to/game -DSKATE3_TITLE_UPDATE_XEXP_ROOT=/path/to/tu-xexp \
  -DREXGLUE_ENABLE_TRACY=OFF -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF \
  -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_OSS=OFF -DSDL_ALSA=OFF -DSDL_PULSEAUDIO=OFF \
  -DSDL_PIPEWIRE=OFF -DSDL_JACK=OFF -DSDL_SNDIO=OFF -DSDL_HIDAPI=OFF -DSDL_DBUS=OFF \
  -DSDL_IBUS=OFF -DSDL_LIBUDEV=OFF -DSDL_VULKAN=OFF -DSDL_DUMMYVIDEO=ON \
  -DSDL_DUMMYAUDIO=ON -DSDL_OFFSCREEN=OFF
ninja -C out/ps5 skate3
ps5/tools/ps5_package.sh out/ps5 ~/ps5-plume-triangle
```

The app folder ends up in `out/ps5/ps5-app/PPSA93003`. Copy it to
`/data/homebrew/` on the PS5 and put the game in its `Assets` folder
(`ps5/package/Assets/README.txt`).

## Where things are

| | |
|---|---|
| `src/ps5/hub/` | the hub (Home, Settings, About), its sounds and music |
| `src/ps5/ps5_button_prompts.*` | PlayStation button prompts |
| `src/skate3_app_common.cpp` | PS5 folders, first-run settings, the hub before the game |
| `ps5/package/` | home screen art, hub pictures, the button prompt pictures |
| `ps5/patches/` | the SDK's PS5 runtime and the ImGui fix |
| `ps5/tools/` | packaging and the generated-code fix |

Settings live in `UserData/settings.toml`, logs in `logs/`, both in the app folder.
PS5-only settings: `ps5_wait_spin_us` and `ps5_cs_spin_us` (how long a waiting
thread watches for its signal before sleeping), `skate3_button_prompts`,
`skate3_ps5_dump_hud_pictures`, `hub_music_volume`.
