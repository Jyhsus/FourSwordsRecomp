#pragma once
// fs_boot.h — Four Swords-specific runtime hooks.
//
// M2: boot straight into Four Swords. fs_boot.cpp hooks set_game_mode
// (0x0800B5EC, declared in game.toml) so that picking a save file skips the
// cartridge's "CHOOSE A GAME" screen and starts Four Swords; A Link to the
// Past is never shown or modified. FSR_STOCK_MENU=1 keeps the stock screen.
// See docs/DESIGN.md section 2.

namespace gbarecomp { struct GbaInstance; }

namespace fsr {
// Called for every virtual GBA a netplay/local-co-op session creates.
void setup_netplay_instance(gbarecomp::GbaInstance& instance);
}
