// Skate 3 for PS5 - the game's simulation speed in the log (ps5_sim_probe.cpp).
#pragma once

namespace skate3ps5::simprobe {

// The game handed a finished frame to the GPU (VdSwap's caller).
void OnGuestSwap();

}  // namespace skate3ps5::simprobe
