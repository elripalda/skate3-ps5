# Skate 3 for PS5

**Skate 3 (Xbox 360) running natively on a jailbroken PS5**, at up to 4K and 60 fps.

> ### Built on Skate 3 Recomp by mchughalex
> This port is based on **[Skate 3 Recomp](https://github.com/mchughalex/skate3recomp)**, mchughalex's native recompilation of Skate 3 for PC, and its native renderer. His work is the backbone of this project: this release takes it to the PS5. Ported with his permission. Go check out and support the original project.

## Get it on Recomp Store
The easiest way to play is **[Recomp Store](https://recompstore.com)**. It installs the app, sets the game up from your own disc image and Title Update, and keeps it up to date.

## What's in it
- Native renderer, up to 4K at 60 fps, with the emulated renderer as a fallback
- Graphics settings: resolution, frame rate, anti-aliasing, shadows, ambient occlusion, bloom, volumetric lighting, draw distance and field of view
- PlayStation button prompts throughout the game (or the Xbox originals, if you prefer)
- A hub made for the DualSense: start the game, change settings, pick from 12 languages
- DLC and saves

## What you need
- A jailbroken PS5 that runs homebrew apps from `/data/homebrew/`
- Your **own** copy of Skate 3 for Xbox 360 and its Title Update 3. No game files are included.

## Install by hand
1. Download `PPSA93003.zip` from [Releases](../../releases) and copy the `PPSA93003` folder to `/data/homebrew/` on your PS5.
2. Copy your game's files and the Title Update into its `Assets` folder:

```
/data/homebrew/PPSA93003/Assets/default.xex
/data/homebrew/PPSA93003/Assets/data/...
/data/homebrew/PPSA93003/Assets/TU_12K2276_000000C000000.00000000000O3
```

3. DLC packages go in `/data/homebrew/PPSA93003/Assets/dlc/`.

Launch it from your homebrew launcher. The hub opens first: press START GAME.

## Credits
- **[mchughalex](https://github.com/mchughalex)**: [Skate 3 Recomp](https://github.com/mchughalex/skate3recomp), the recompilation and native renderer this port is built on
- **[elripalda](https://github.com/elripalda)**: the PS5 port, the hub and the PlayStation buttons
- **[ReXGlue](https://github.com/rexglue/rexglue-sdk)**: the static recompiler and runtime; **[Xenia](https://github.com/xenia-project/xenia)**: the emulator its code comes from

*An unofficial fan project, not affiliated with or endorsed by EA, Microsoft or Sony. No game files are included.*
