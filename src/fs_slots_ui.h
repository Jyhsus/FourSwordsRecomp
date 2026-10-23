#pragma once
// fs_slots_ui.h — the launcher's file screen (docs/DESIGN.md section 8.1).
//
// Shown after the recomp-ui launcher's Play: lists the cartridge's 3 file
// slots with their names and lets the player create, rename, erase and play
// one. Every edit goes through fs_save (the same writes as the game) and is
// stored at once. Its own small SDL2 + OpenGL + Dear ImGui window, in the
// launcher's GBA colours; built only with GBAGAME_RECOMP_UI.
//
// Its Multiplayer button offers Local (2-4 players on this computer, each
// with a file or as a guest; docs/DESIGN.md section 10) or Online (the
// launcher's netplay rooms).

#include <filesystem>

#include "fs_local.h"

namespace fsr {

struct SlotScreen {
    std::filesystem::path save_path;  // the .sav run_game() will use
    std::filesystem::path rom_path;   // the player's ROM, for a first save
    LocalPlan local;                  // out: set when kSlotScreenLocal is returned
};

constexpr int kSlotScreenBack = -1;    // back to the launcher
constexpr int kSlotScreenQuit = -2;    // window closed
constexpr int kSlotScreenLocal = -3;   // start local multiplayer (screen.local)
constexpr int kSlotScreenOnline = -4;  // the launcher, for its online rooms

// Returns the slot to play (0-2) or one of the kSlotScreen codes.
int run_slot_screen(SlotScreen& screen);

}  // namespace fsr
