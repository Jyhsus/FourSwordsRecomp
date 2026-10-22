// main.cpp — FourSwordsRecomp entry point.
//
//   FourSwordsRecomp [--bios <path>] [--rom <path>] [game.toml]
//
// The player supplies their own GBA BIOS and their own USA cartridge dump
// ("A Link to the Past & Four Swords", game code AZLE). Both are hash-checked
// before any game code runs. Windowed play opens the recomp-ui launcher first.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "runtime.h"
#include "fs_boot.h"
#if defined(GBAGAME_NETPLAY)
#include "multiplayer_launch.h"
#include "gba_netplay_build_identity.h"
#endif
#if defined(GBAGAME_RECOMP_UI)
#include "game_launcher_boot.h"
#endif

namespace {
constexpr const char* kRomSha1   = "a272055abbbf6c26b0cd54c87395d01699589161";
constexpr const char* kRomSha256 =
    "f328f8f07d736288a00c80d31cc1630f3aa02aaf20efdcba73d31dae832b5d76";
constexpr std::uint32_t kRomCrc32 = 0x8E91CD13u;
}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--version") == 0) {
            std::printf("FourSwordsRecomp v%s\n", FSR_VERSION);
            return 0;
        }
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::printf("FourSwordsRecomp [--bios <path>] [--rom <path>] [game.toml]\n"
                        "Needs your own GBA BIOS and your own USA AZLE cartridge dump.\n");
            return 0;
        }
    }

    gbarecomp::RunOptions opts;
    std::vector<std::string> args(argv, argv + argc);

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
    opts.launcher_save_path = "saves/four_swords_usa.sav";

#if defined(GBAGAME_RECOMP_UI)
    if (game_launcher_preboot(args, opts)) return 0;   // user quit the launcher
#endif
    std::vector<char*> av;
    av.reserve(args.size());
    for (auto& a : args) av.push_back(a.data());
    return gbarecomp::run_game(static_cast<int>(av.size()), av.data(), opts);
}
