# Changelog

## 1.1

- Audio: when a sound's data runs past the end of one buffer, the decoder now
  carries on into the next buffer instead of dropping the rest (the XMA
  decoder errors in testers' logs, and short audio dropouts).
- Memory: the native renderer's texture and model caches are capped lower on
  the PS5 (1 GB of textures, 512 MB of models). They fill up and stay level
  over a long session instead of growing toward the PS5's limit.
- No first-use hitch for ambient occlusion and volumetric lighting: their
  shaders are built while the game loads instead of the first time they show
  up on screen.
- The source code is now in this repository (`ps5/BUILD.md`).

## 1.0

First release.
