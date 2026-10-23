#include "fs_boot.h"

#include <atomic>
#include <cstdlib>

#include "mod_function_hooks.h"
#include "mod_runtime.h"
#include "runtime.h"
#include "runtime_arm.h"

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
// start_four_swords, so every path into mode 6 starts Four Swords, from the
// same main-loop context the stock code uses. LR still holds the main loop's
// return address. Sub-state 0 (0x080308F8) still runs first: it reloads the
// current file from EEPROM (0x08011418) and copies its name into the system
// block (0x080681D0), which Four Swords uses; file select also does that, but
// --slot skips file select. The stock screen stays reachable with
// FSR_STOCK_MENU=1 for validation.
//
// Boot into a file slot (section 8.1). AgbMain loads the 16-byte system block
// (EEPROM entry 7) to 0x03000000; byte 4 is the current file. After a soft
// reset that left "GoGameS" at 0x03000009 (0x08068108), it skips file select
// and enters mode 6 directly through 0x0803072C. --slot reproduces that path
// on a cold boot: at AgbMain's call to the screen reset 0x0800BED8, right
// after the system block is loaded, it sets the file and the same mode bytes
// 0x0803072C writes. The hook declines, so the screen reset still runs.

namespace {

constexpr const char* kHookId = "fsr.direct_boot";
constexpr std::uint32_t kChooseGameHandler = 0x0803074Cu;
constexpr std::uint32_t kStartFourSwords = 0x08030E0Cu;

constexpr const char* kSlotHookId = "fsr.boot_slot";
constexpr std::uint32_t kScreenReset = 0x0800BED8u;
constexpr std::uint32_t kAgbMainScreenResetReturn = 0x0800B0CEu;
constexpr std::uint32_t kCurrentFile = 0x03000004u;
constexpr std::uint32_t kGameMode = 0x03006D10u;
constexpr std::uint32_t kGameSubState = 0x03005050u;
constexpr std::uint32_t kModeAux = 0x03000BFCu;
constexpr std::uint8_t kModeChooseGame = 6;

// Quitting Four Swords: mode 9 sub-state 2 (0x0802FAEC) dispatches the lobby
// steps by 0x03000BFC through 0x084278C0; step 7 (0x08030018) waits until the
// fade flag 0x03000520 clears, resets the screen and calls set_game_mode(6).
constexpr const char* kExitHookId = "fsr.exit";
constexpr std::uint32_t kFourSwordsExit = 0x08030018u;
constexpr std::uint32_t kFadeBusy = 0x03000520u;

std::atomic<int> g_boot_slot{-1};

int on_choose_game(std::uint32_t /*addr*/, int /*thumb*/, ArmCpuState* cpu) {
    if (bus_read_u8(kGameSubState) == 0) return 0;  // let sub-state 0 load the file
    cpu->R[15] = kStartFourSwords;
    return 1;
}

// Declines in every case; the stock body still runs this frame.
int on_four_swords_exit(std::uint32_t /*addr*/, int /*thumb*/, ArmCpuState* /*cpu*/) {
    if (bus_read_u8(kFadeBusy) != 0) return 0;
    if (g_boot_slot.load(std::memory_order_relaxed) >= 0) {
        // Started from the launcher: end the session at the frame boundary;
        // run_game() flushes the save on its way out.
        gbarecomp::request_quit();
    } else {
        // No launcher to return to: show the stock "CHOOSE A GAME" instead of
        // starting Four Swords again. Re-armed on the next reset.
        gba_mod_set_function_hook_enabled(kHookId, 0);
    }
    return 0;
}

// Only AgbMain's call matches, so every virtual GBA gets it once per boot.
int on_screen_reset(std::uint32_t /*addr*/, int /*thumb*/, ArmCpuState* cpu) {
    const int slot = g_boot_slot.load(std::memory_order_relaxed);
    if (slot < 0 || (cpu->R[14] & ~1u) != kAgbMainScreenResetReturn) return 0;
    bus_write_u8(kCurrentFile, static_cast<std::uint8_t>(slot));
    bus_write_u8(kGameMode, kModeChooseGame);
    bus_write_u8(kGameSubState, 0);
    bus_write_u8(kModeAux, 0);
    return 0;
}

bool direct_boot_wanted() {
    const char* stock = std::getenv("FSR_STOCK_MENU");
    return !(stock && *stock && *stock != '0');
}

void enable_hooks() {
    const bool direct = direct_boot_wanted();
    gba_mod_set_function_hook_enabled(kHookId, direct ? 1 : 0);
    gba_mod_set_function_hook_enabled(kExitHookId, direct ? 1 : 0);
    gba_mod_set_function_hook_enabled(kSlotHookId, g_boot_slot.load() >= 0 ? 1 : 0);
}

// Mod activation disables every function hook and then runs reset callbacks,
// so the hooks are re-armed there as well as at startup.
const bool registered = [] {
    gba_mod_register_function_entry_plugin(kHookId, kChooseGameHandler, 1, &on_choose_game);
    gba_mod_register_function_entry_plugin(kSlotHookId, kScreenReset, 1, &on_screen_reset);
    gba_mod_register_function_entry_plugin(kExitHookId, kFourSwordsExit, 1, &on_four_swords_exit);
    gba_mod_register_reset_callback(&enable_hooks);
    enable_hooks();
    return true;
}();

}  // namespace

namespace fsr {

void set_boot_slot(int slot) {
    g_boot_slot.store(slot >= 0 && slot < 3 ? slot : -1);
    enable_hooks();
}

void setup_netplay_instance(gbarecomp::GbaInstance& /*instance*/) {
    // Every virtual GBA shares the generated code and therefore the hook
    // above; nothing per-instance is needed for direct boot.
}

}  // namespace fsr
