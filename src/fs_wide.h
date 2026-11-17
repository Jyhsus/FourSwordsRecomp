#pragma once
// fs_wide.h — 16:9 view of a Four Swords stage (docs/DESIGN.md section 11).
//
// The policy drives a host-only mirror PPU (gbarecomp::GbaNetplayPresentation)
// that draws 284 columns instead of 240. The extra columns come from the
// stage's own tile map in the GBA's RAM, so they show the real room, not a
// stretched or wrapped picture. Nothing is written to the GBA: the game, its
// collisions and the link cable see the same 240x160 console as always.

#include "multiplayer_view.h"

namespace fsr {

// 160 * 16 / 9, rounded down to an even number of pixels per side.
inline constexpr unsigned kWideViewWidth = 284;

gbarecomp::GbaNetplayViewPolicy wide_view_policy();

}  // namespace fsr
