#pragma once
// fs_slots_ui.h — the launcher's file screen (docs/DESIGN.md section 8.1).
//
// Shown after the recomp-ui launcher's Play: lists the cartridge's 3 file
// slots with their names and lets the player create, rename, erase and play
// one. Every edit goes through fs_save (the same writes as the game) and is
// stored at once. Its own small SDL2 + OpenGL + Dear ImGui window, in the
// launcher's GBA colours; built only with GBAGAME_RECOMP_UI.

#include <filesystem>

namespace fsr {

struct SlotScreen {
    std::filesystem::path save_path;  // the .sav run_game() will use
    std::filesystem::path rom_path;   // the player's ROM, for a first save
};

constexpr int kSlotScreenBack = -1;   // back to the launcher
constexpr int kSlotScreenQuit = -2;   // window closed

// Returns the slot to play (0-2), kSlotScreenBack or kSlotScreenQuit.
int run_slot_screen(const SlotScreen& screen);

}  // namespace fsr
