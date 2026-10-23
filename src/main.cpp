// main.cpp — FourSwordsRecomp entry point.
//
//   FourSwordsRecomp [--bios <path>] [--rom <path>] [--slot 1-3]
//                    [--local 1,guest,...] [game.toml]
//
// The player supplies their own GBA BIOS and their own USA cartridge dump
// ("A Link to the Past & Four Swords", game code AZLE). Both are hash-checked
// before any game code runs. Windowed play opens the recomp-ui launcher first,
// then the file screen (src/fs_slots_ui.cpp); quitting Four Swords comes back
// to the launcher (docs/DESIGN.md section 8.1). The file screen's Multiplayer
// button starts local split-screen play (src/fs_local.cpp, section 10).

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "runtime.h"
#include "fs_boot.h"
#include "fs_save.h"
#if defined(GBAGAME_NETPLAY)
#include "multiplayer_launch.h"
#include "gba_netplay_build_identity.h"
#endif
#if defined(GBAGAME_RECOMP_UI)
#include "game_launcher_boot.h"
#include "fs_slots_ui.h"
#endif

namespace {
constexpr const char* kRomSha1   = "a272055abbbf6c26b0cd54c87395d01699589161";
constexpr const char* kRomSha256 =
    "f328f8f07d736288a00c80d31cc1630f3aa02aaf20efdcba73d31dae832b5d76";
constexpr std::uint32_t kRomCrc32 = 0x8E91CD13u;
constexpr const char* kDefaultSave = "saves/four_swords_usa.sav";

// Removes "--slot N" from args. Returns the 0-based slot, -1 when absent and
// -2 on a bad value.
int take_slot_argument(std::vector<std::string>& args) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] != "--slot") continue;
        if (i + 1 >= args.size()) return -2;
        const std::string value = args[i + 1];
        args.erase(args.begin() + static_cast<std::ptrdiff_t>(i),
                   args.begin() + static_cast<std::ptrdiff_t>(i) + 2);
        if (value.size() != 1 || value[0] < '1' || value[0] > '3') return -2;
        return value[0] - '1';
    }
    return -1;
}

// Last value of a "--flag value" pair, or "".
std::string flag_value(const std::vector<std::string>& args, const char* flag) {
    std::string value;
    for (std::size_t i = 1; i + 1 < args.size(); ++i) {
        if (args[i] == flag) value = args[i + 1];
    }
    return value;
}

// The .sav run_game() will use: --save-path, else game.toml's [save] path
// (kDefaultSave, relative to the .toml), else the ROM's name with .sav.
std::filesystem::path save_path_argument(const std::vector<std::string>& args) {
    if (std::string path = flag_value(args, "--save-path"); !path.empty()) return path;
    std::filesystem::path toml = "game.toml";
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i].size() > 5 && args[i].compare(args[i].size() - 5, 5, ".toml") == 0 &&
            args[i - 1] != "--save-path") {
            toml = args[i];
        }
    }
    std::error_code ec;
    if (std::filesystem::exists(toml, ec)) {
        return (std::filesystem::absolute(toml, ec).parent_path() / kDefaultSave)
            .lexically_normal();
    }
    std::filesystem::path rom = flag_value(args, "--rom");
    return rom.empty() ? std::filesystem::path(kDefaultSave) : rom.replace_extension(".sav");
}

#if defined(GBAGAME_RECOMP_UI)
// Removes "--local LIST" from args (section 10): 2-4 comma-separated players,
// each a file 1-3 or "guest". plan.players stays 0 when absent; false on a
// bad value.
bool take_local_argument(std::vector<std::string>& args, fsr::LocalPlan& plan) {
    plan.players = 0;
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] != "--local") continue;
        if (i + 1 >= args.size()) return false;
        const std::string value = args[i + 1];
        args.erase(args.begin() + static_cast<std::ptrdiff_t>(i),
                   args.begin() + static_cast<std::ptrdiff_t>(i) + 2);
        std::size_t start = 0;
        int players = 0;
        while (start <= value.size()) {
            const std::size_t comma = std::min(value.find(',', start), value.size());
            const std::string item = value.substr(start, comma - start);
            if (players == fsr::kLocalMaxPlayers) return false;
            if (item == "guest") {
                plan.file[players++] = fsr::kGuestFile;
            } else if (item.size() == 1 && item[0] >= '1' && item[0] <= '3') {
                plan.file[players++] = item[0] - '1';
            } else {
                return false;
            }
            start = comma + 1;
        }
        plan.players = players;
        return players >= 2;
    }
    return true;
}

// Same cases in which the launcher seam stays out of the way (headless and
// scripted runs); the file screen is part of the launcher, so it does too.
bool interactive(const std::vector<std::string>& args) {
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& s = args[i];
        if (s == "--tcp" || s == "--steps" || s == "--frames" || s == "--no-window" ||
            s == "--dump-bmp" || s == "--dump-png" || s == "--load-state" ||
            s == "--replay-diagnostic" || s == "--rom" || s == "--help" || s == "-h" ||
            s == "--no-launcher") {
            return false;
        }
    }
    const char* env = std::getenv("GBARECOMP_NO_LAUNCHER");
    return !(env && env[0] && env[0] != '0');
}
#endif
}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("FourSwordsRecomp v%s\n", FSR_VERSION);
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::printf("FourSwordsRecomp [--bios <path>] [--rom <path>] [--slot 1-3]\n"
                        "                 [--local 1,guest,...] [game.toml]\n"
                        "Needs your own GBA BIOS and your own USA AZLE cartridge dump.\n"
                        "--slot starts that cartridge file directly in Four Swords.\n"
                        "--local starts 2-4 linked GBAs in split screen, one file\n"
                        "(1-3, each at most once) or guest per player.\n");
            return 0;
        }
    }

    gbarecomp::RunOptions opts;
    std::vector<std::string> args(argv, argv + argc);

    // Section 8.1: start a cartridge file slot without "CHOOSE A FILE".
    const int slot = take_slot_argument(args);
    if (slot == -2) {
        std::fprintf(stderr, "--slot needs 1, 2 or 3\n");
        return 1;
    }
    if (slot >= 0) {
        std::string error;
        const auto image = fsr::save::Image::load(save_path_argument(args), &error);
        if (!image) {
            std::fprintf(stderr, "--slot: %s\n", error.c_str());
            return 1;
        }
        if (!image->slots()[slot].used) {
            std::fprintf(stderr, "--slot: file %d is empty\n", slot + 1);
            return 1;
        }
        fsr::set_boot_slot(slot);
    }

#if defined(GBAGAME_RECOMP_UI)
    // Section 10: local split-screen multiplayer without the launcher.
    fsr::LocalPlan local;
    if (!take_local_argument(args, local)) {
        std::fprintf(stderr, "--local needs 2 to 4 players: 1, 2, 3 or guest, comma-separated\n");
        return 1;
    }
    if (local.players) {
        local.save_path = save_path_argument(args);
        std::array<fsr::save::Slot, fsr::save::kSlotCount> slots{};
        if (const auto image = fsr::save::Image::load(local.save_path)) slots = image->slots();
        if (slot >= 0 || !fsr::valid_local_plan(local, slots)) {
            std::fprintf(stderr, "--local: every file must be in use, and played once; no --slot\n");
            return 1;
        }
    }
#endif

#if defined(GBAGAME_NETPLAY)
    // Online play: every peer runs all 2-4 virtual GBAs locally and only
    // controller input crosses the network (see docs/DESIGN.md).
    opts.netplay = gbarecomp::make_gba_netplay_launch(
        "four-swords-usa", GBARECOMP_NETPLAY_BUILD_ID, kRomSha256,
        &fsr::setup_netplay_instance);
    opts.netplay->max_players = 4;
    try { gbarecomp::parse_gba_netplay_arguments(args, *opts.netplay); }
    catch (const std::exception& e) { std::fprintf(stderr, "netplay: %s\n", e.what()); return 1; }
#endif

    opts.builtin_game_name  = "The Legend of Zelda: Four Swords (USA)";
    opts.builtin_rom_sha1   = kRomSha1;
    opts.builtin_rom_crc32  = kRomCrc32;
    opts.mod_game_id        = "four-swords-us";
    opts.launcher_region    = "USA";
    opts.launcher_game_config = "game.toml";
    opts.launcher_save_path = kDefaultSave;

#if defined(GBAGAME_RECOMP_UI)
    if (local.players) {
        // run_game() checks the assets, then hands them to the local runner.
        gbarecomp::RunOptions local_opts = opts;
        local_opts.netplay = fsr::make_local_launch(local);
        std::vector<char*> av;
        for (auto& a : args) av.push_back(a.data());
        return gbarecomp::run_game(static_cast<int>(av.size()), av.data(), local_opts);
    }

    // Launcher -> file screen -> game, and back to the launcher when the
    // player quits Four Swords. run_game() may run more than once per process.
    const bool from_launcher = slot < 0 && interactive(args);
    bool reopen_launcher = false;
    for (;;) {
        std::vector<std::string> run_args = args;
        if (reopen_launcher) run_args.push_back("--launcher");
        if (game_launcher_preboot(run_args, opts)) return 0;   // user quit the launcher
        if (from_launcher && !(opts.netplay && opts.netplay->enabled)) {
            fsr::SlotScreen screen;
            screen.save_path = save_path_argument(run_args);
            screen.rom_path = flag_value(run_args, "--rom");
            const int picked = fsr::run_slot_screen(screen);
            if (picked == fsr::kSlotScreenQuit) return 0;
            reopen_launcher = true;
            // Online rooms are the launcher's netplay page.
            if (picked == fsr::kSlotScreenBack || picked == fsr::kSlotScreenOnline) continue;
            if (picked == fsr::kSlotScreenLocal) {
                // Section 10: run_game() checks the assets and window settings,
                // then hands them to the local runner instead of single player.
                gbarecomp::RunOptions local_opts = opts;
                local_opts.netplay = fsr::make_local_launch(screen.local);
                std::vector<char*> av;
                for (auto& a : run_args) av.push_back(a.data());
                const int rc = gbarecomp::run_game(static_cast<int>(av.size()), av.data(), local_opts);
                if (!fsr::quit_to_menu()) return rc;   // window closed, as in single player
                fsr::set_boot_slot(-1);
                continue;
            }
            fsr::set_boot_slot(picked);
        }
        std::vector<char*> av;
        av.reserve(run_args.size());
        for (auto& a : run_args) av.push_back(a.data());
        const int rc = gbarecomp::run_game(static_cast<int>(av.size()), av.data(), opts);
        if (!from_launcher || !fsr::quit_to_menu()) return rc;
        fsr::set_boot_slot(-1);
    }
#else
    std::vector<char*> av;
    av.reserve(args.size());
    for (auto& a : args) av.push_back(a.data());
    return gbarecomp::run_game(static_cast<int>(av.size()), av.data(), opts);
#endif
}
