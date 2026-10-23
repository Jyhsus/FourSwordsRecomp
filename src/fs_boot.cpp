#include "fs_boot.h"

#include <cstdlib>

#include "mod_function_hooks.h"
#include "mod_runtime.h"

// Boot straight into Four Swords (M2).
//
// The AZLE main loop at 0x0800B1B2 calls game_mode_handlers[*(u8*)0x03006D10]
// every frame, and set_game_mode (0x0800B5EC) is the only writer: it stores r0
// and clears the sub-state bytes. After a file is picked, file select (mode 8)
// calls set_game_mode(6), the "CHOOSE A GAME" screen. When Four Swords is
// chosen there, sub-state 5 (0x08030DA8) calls start_four_swords (0x08030E0C)
// if more than one GBA is linked, or first shows the "Multi-Pak" notice. That
// function initialises Four Swords and calls set_game_mode(9).
//
// The hook replaces set_game_mode(6) with a tail call to start_four_swords:
// LR still holds the caller's return address, so the guest resumes as if the
// player had chosen Four Swords with a link cable attached. The stock screen
// stays reachable with FSR_STOCK_MENU=1 for validation.

namespace {

constexpr const char* kHookId = "fsr.direct_boot";
constexpr std::uint32_t kSetGameMode = 0x0800B5ECu;
constexpr std::uint32_t kStartFourSwords = 0x08030E0Cu;
constexpr std::uint32_t kModeChooseGame = 6u;

int on_set_game_mode(std::uint32_t /*addr*/, int /*thumb*/, ArmCpuState* cpu) {
    if (cpu->R[0] != kModeChooseGame) return 0;
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
    gba_mod_register_function_entry_plugin(kHookId, kSetGameMode, 1, &on_set_game_mode);
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
