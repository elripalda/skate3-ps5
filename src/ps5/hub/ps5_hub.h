// Skate 3 for PS5 - the hub on the console (ps5_hub.cpp).
#pragma once

#if defined(__PROSPERO__)
#include <filesystem>
#include <functional>
#include <string>

#include "hub_ui.h"

struct ImFontAtlas;
namespace rex::ui {
class ImGuiDrawer;
class ImmediateDrawer;
}  // namespace rex::ui

namespace skate3ps5::hub {
// The hub's fonts (from OnConfigureFonts).
void ConfigureFonts(ImFontAtlas* atlas);
// Shows the hub; `launch` runs (on the UI thread) on START GAME. `app`: the
// app's folder (/app0: hub/ pictures and music).
void Show(rex::ui::ImGuiDrawer* drawer, rex::ui::ImmediateDrawer* immediate, const std::filesystem::path& app,
          const GameState& game, Config config, std::string version, std::function<void()> launch);
}  // namespace skate3ps5::hub
#endif
