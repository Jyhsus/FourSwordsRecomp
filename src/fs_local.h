#pragma once
// fs_local.h — local multiplayer (docs/DESIGN.md section 10).
//
// 2-4 virtual GBAs on one GbaLinkHub cable in this process, like
// FourSwordsLinkProbe, in one split-screen window laid out by fs_layout.h.
//
// One number means one player everywhere: player N is machine N-1, cable port
// N-1 (so the game calls that GBA "P<N>": port 0 is the cable's parent, P1),
// view N in the window, entry N-1 of the plan's files and controllers, and
// player 1's GBA is the one heard. Nothing is reordered after the plan.
//
// Saves: every GBA starts from a copy of the shared .sav with its system block
// pointing at that player's file, and only that file's blocks are merged back
// (fs_save Image::copy_file). Two players never share a file; a guest gets a
// one-file first-boot save that is never written. Built with
// GBAGAME_RECOMP_UI only.

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "fs_save.h"

namespace gbarecomp { struct GbaNetplayLaunch; }

namespace fsr {

constexpr int kGuestFile = -1;
constexpr int kLocalMaxPlayers = 4;
constexpr int kNoPad = -1;

struct LocalPlan {
    int players = 2;                                   // 2-4
    std::array<int, kLocalMaxPlayers> file{0, 1, kGuestFile, kGuestFile};  // 0-2 or kGuestFile
    // Each player's controller: a position in local_pads(), or kNoPad.
    std::array<int, kLocalMaxPlayers> pad{0, 1, 2, 3};
    int keyboard = 0;                                  // the player who also has the keyboard, or -1
    bool integer_scale = false;                        // whole-number scale, larger bars
    bool wide = false;                                 // 16:9 views (fs_wide.h); needs the netplay build
    std::filesystem::path save_path;                   // the shared .sav
};

// A connected game controller.
struct LocalPad {
    int instance = -1;   // SDL joystick instance ID (valid until SDL's controller subsystem quits)
    int light = -1;      // SDL player index: the number its player light shows, 0-based; -1 unknown
    std::string name;
    std::string guid;
};

// The connected controllers, in the order players count them: by player light
// (the XInput slot on Xbox pads, the LED on others) and, without one, in the
// order they were connected. SDL's own device order is not used: it depends
// on the driver and can run backwards. SDL's game controller subsystem must
// be initialised.
std::vector<LocalPad> local_pads();

// 2-4 players, each on a file in use or a guest, no file and no controller
// twice.
bool valid_local_plan(const LocalPlan& plan, const std::array<save::Slot, save::kSlotCount>& slots);

// A launch for RunOptions::netplay with no network: run_game() resolves and
// checks the ROM, BIOS and window settings, then hands them to this runner
// instead of starting single player.
std::shared_ptr<gbarecomp::GbaNetplayLaunch> make_local_launch(const LocalPlan& plan);

}  // namespace fsr
