// Skate 3 for PS5 - PlayStation button prompts (ps5_button_prompts.cpp).
//
// The game draws each button prompt from a small picture of its own. When the
// game loads one of the Xbox pictures, its PlayStation version from
// hub/button_prompts/playstation/<id>.dds (<id>: the Xbox picture's FNV-1a
// 64 hash) is written over it in memory - for both renderers, nothing in the
// game's files changes. The hub's Button Prompts setting (skate3_button_prompts)
// picks PlayStation or the game's own Xbox buttons.
#pragma once

#include <cstdint>
#include <filesystem>

namespace skate3ps5::prompts {

// Finds the PlayStation pictures (app folder: /app0). Call before the game starts.
void Init(const std::filesystem::path& app_folder);

// A texture the game just loaded (cProcessArenaAsset::RegisterTexture's
// texture object, a guest address).
void OnTextureLoaded(uint32_t texture_object);

}  // namespace skate3ps5::prompts
