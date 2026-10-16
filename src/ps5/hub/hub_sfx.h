// Skate 3 for PS5 - the hub's menu sounds (hub_sfx.cpp):
// made in code at 48 kHz, stereo interleaved. hub/sfx/<name>.wav (or .mp3,
// .flac, .ogg) replaces one (ps5_hub.cpp).
#pragma once

#include <vector>

#include "hub_ui.h"

namespace skate3ps5::hub {

std::vector<float> SynthSound(Sound sound);
// Its file name in hub/sfx ("move", "select", ...).
const char* SoundName(Sound sound);

}  // namespace skate3ps5::hub
