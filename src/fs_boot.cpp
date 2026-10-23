#include "fs_boot.h"

#include <cstdlib>

#include "mod_function_hooks.h"
#include "mod_runtime.h"

// Boot straight into Four Swords (M2).
//
// The AZLE main loop at 0x0800B1B2 calls game_mode_handlers[*(u8*)0x03006D10]
// every frame. Mode 6 is the "CHOOSE A GAME" screen. It is entered through
// set_game_mode(6) (after a file is picked, among others) and also by an
// inline write at 0x0803072C, which AgbMain uses after a soft reset that left a
// handoff marker in IWRAM (0x08068108). When Four Swords is chosen there,
// sub-state 5 (0x08030DA8) calls start_four_swords (0x08030E0C) if more than
// one GBA is linked, or first shows the "Multi-Pak" notice. That function
// initialises Four Swords and calls set_game_mode(9). It reads no arguments.
//
// The hook replaces the mode-6 handler (0x0803074C) with a tail call to
// start_four_swords, so every path into mode 6 starts Four Swords on its first
// frame, from the same main-loop context the stock code uses. LR still holds
// the main loop's return address. The stock screen stays reachable with
// FSR_STOCK_MENU=1 for validation.

namespace {

constexpr const char* kHookId = "fsr.direct_boot";
constexpr std::uint32_t kChooseGameHandler = 0x0803074Cu;
constexpr std::uint32_t kStartFourSwords = 0x08030E0Cu;

int on_choose_game(std::uint32_t /*addr*/, int /*thumb*/, ArmCpuState* cpu) {
    cpu->R[15] = kStartFourSwords;
    return 1;
}

bool direct_boot_wanted() {
    const char* stock = std::getenv("FSR_STOCK_MENU");
    return !(stock && *stock && *stock != '0');
}

void enable_direct_boot() {
    gba_mod_set_function_hook_enabled(kHookId, direct_boot_wanted() ? 1 : 0);
}

// Mod activation disables every function hook and then runs reset callbacks,
// so the hook is re-armed there as well as at startup.
const bool registered = [] {
    gba_mod_register_function_entry_plugin(kHookId, kChooseGameHandler, 1, &on_choose_game);
    gba_mod_register_reset_callback(&enable_direct_boot);
    enable_direct_boot();
    return true;
}();

}  // namespace

namespace fsr {

void setup_netplay_instance(gbarecomp::GbaInstance& /*instance*/) {
    // Every virtual GBA shares the generated code and therefore the hook
    // above; nothing per-instance is needed for direct boot.
}

}  // namespace fsr
