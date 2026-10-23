#pragma once
// fs_boot.h — Four Swords-specific runtime hooks.
//
// M2: boot straight into Four Swords. fs_boot.cpp hooks the "CHOOSE A GAME"
// mode handler (0x0803074C, declared in game.toml) so that picking a save file
// starts Four Swords instead of that screen; A Link to the Past is never shown
// or modified. FSR_STOCK_MENU=1 keeps the stock screen.
// See docs/DESIGN.md section 2.
//
// Section 8.1: set_boot_slot() also skips "CHOOSE A FILE" and starts the
// chosen cartridge file slot.

namespace gbarecomp { struct GbaInstance; }

namespace fsr {
// Boot straight into file slot 0-2 (-1 = show file select). Call before
// run_game(); applies to every virtual GBA.
void set_boot_slot(int slot);

// Called for every virtual GBA a netplay/local-co-op session creates.
void setup_netplay_instance(gbarecomp::GbaInstance& instance);
}
