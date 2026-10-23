#pragma once
// fs_local.h — local multiplayer (docs/DESIGN.md section 10).
//
// 2-4 virtual GBAs on one GbaLinkHub cable in this process, like
// FourSwordsLinkProbe, in one split-screen window: two side by side, three or
// four in a 2x2 grid. Player 1 plays with the keyboard and the first
// controller, player N with controller N; the audio is player 1's.
//
// Saves: every GBA starts from a copy of the shared .sav with its system block
// pointing at that player's file, and only that file's blocks are merged back
// (fs_save Image::copy_file). Two players never share a file; a guest gets a
// one-file first-boot save that is never written. Built with
// GBAGAME_RECOMP_UI only.

#include <array>
#include <filesystem>
#include <memory>

#include "fs_save.h"

namespace gbarecomp { struct GbaNetplayLaunch; }

namespace fsr {

constexpr int kGuestFile = -1;
constexpr int kLocalMaxPlayers = 4;

struct LocalPlan {
    int players = 2;                                   // 2-4
    std::array<int, kLocalMaxPlayers> file{0, 1, kGuestFile, kGuestFile};  // 0-2 or kGuestFile
    std::filesystem::path save_path;                   // the shared .sav
};

// 2-4 players, each on a file in use or a guest, no file twice.
bool valid_local_plan(const LocalPlan& plan, const std::array<save::Slot, save::kSlotCount>& slots);

// A launch for RunOptions::netplay with no network: run_game() resolves and
// checks the ROM, BIOS and window settings, then hands them to this runner
// instead of starting single player.
std::shared_ptr<gbarecomp::GbaNetplayLaunch> make_local_launch(const LocalPlan& plan);

}  // namespace fsr
