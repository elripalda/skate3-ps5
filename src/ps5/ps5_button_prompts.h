// Skate 3 for PS5 - PlayStation button prompts (ps5_button_prompts.cpp).
//
// The game draws each button prompt from a small picture of its own. Right
// before either renderer reads one of the Xbox pictures from guest memory, its
// PlayStation version from hub/button_prompts/playstation/<id>.dds is written
// over it there (every mip level, in the guest's layout and byte order), so
// both renderers draw the PlayStation button and nothing in the game's files
// changes. The hub's Button Prompts setting (skate3_button_prompts) picks
// PlayStation or the game's own Xbox buttons.
//
// <id>: FNV-1a 64 of the Xbox picture's level 0 - its 4x4 blocks in row
// order, in DDS byte order - starting from 0x14650FB0739D0383 ^ (width << 32 |
// height).
#pragma once

#include <cstdint>

#include <rex/graphics/xenos.h>

namespace rex::memory {
class Memory;
}

namespace skate3ps5::prompts {

// Loads the PlayStation pictures (the app folder's hub/button_prompts/
// playstation) and hooks the emulated renderer's texture reads. Call once the
// runtime is up (OnPostSetup), before the game runs.
void Install(rex::memory::Memory* memory);

// Before a renderer reads a texture from guest memory: when it's one of the
// Xbox pictures, the PlayStation one goes over it first. Returns whether the
// texture is (now) a PlayStation picture.
bool BeforeTextureRead(const rex::graphics::xenos::xe_gpu_texture_fetch_t& fetch);

}  // namespace skate3ps5::prompts
