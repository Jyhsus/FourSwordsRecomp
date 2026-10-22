# CLAUDE.md — FourSwordsRecomp

Context for AI assistants (and humans) continuing this project.
The maintainer (Jyhsus) writes in Spanish; reply in Spanish. Public docs are English-first with Spanish mirrors (`*.es.md`). Keep both in sync.

## What this is
Static recompilation of *The Legend of Zelda: Four Swords* from the USA cartridge
*A Link to the Past & Four Swords* (`AZLE`, SHA-1 `a272055abbbf6c26b0cd54c87395d01699589161`),
built on GBARecomp (submodule `gbarecomp/`, launcher `recomp-ui/`). Goals and plan: `docs/DESIGN.md`.

## Hard rules
- Never commit or distribute the ROM, the BIOS, `generated/` output, DSi dumps, extracted assets or box art. `.gitignore` enforces most of this; check `git status` before committing.
- Never hand-edit `generated/`. Fix `game.toml`, symbols or `src/` hooks, then regenerate.
- Engine (gbarecomp) bugs belong upstream; keep game-specific code in this repo.
- License: PolyForm Noncommercial (same as GBARecomp).

## Build (verified 22 Oct 2026 on Linux, headless)
```sh
cmake -S gbarecomp -B gbarecomp/build -G Ninja -DCMAKE_BUILD_TYPE=Release
ninja -C gbarecomp/build gba_recompile
gbarecomp/build/gba_recompile --bios gbarecomp/bios/gba_bios.bin --config gbarecomp/bios/gba_bios.toml   # once, needs the user's BIOS
tools/regen.sh                      # ~1 min, 1,537 functions, 32 shards
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DGBAGAME_RECOMP_UI=OFF   # OFF = no SDL/launcher
ninja -C build FourSwordsRecomp
./build/FourSwordsRecomp --no-window --quiet --frames 900 --dump-png f900.png game.toml
```
Windows: `tools/regen.ps1` + Visual Studio 2022 CMake (see README). The windowed build needs SDL2 and `GBAGAME_RECOMP_UI=ON`.

## Known facts / gotchas
- Code lives in `0x08000000–0x0813FFFF`; everything above is data. A whole-ROM `aot_scan` needs more than 6 GB RAM (OOM-killed) because it decodes data as ARM code, so it's disabled in `game.toml`. Re-enable it limited to the code range once there are seeds.
- Save: 64 Kbit EEPROM (`EEPROM_V122` @ `0x08427298`).
- Boot reaches the Nintendo/Capcom logos (frame ~300) and "CHOOSE A FILE" (frame ~900). The A Link to the Past / Four Swords choice comes **after** file select.
- Interpreter misses are logged to `recomp_master_misses_AZLE.toml.frag`; review them before merging into `game.toml`.
- Netplay: `src/main.cpp` sets `max_players = 4`. Every peer runs all virtual GBAs, and only inputs cross the network.
- Reference engine: *The Minish Cap* (same studio). Use `zeldaret/tmc` names and MinishCapRecomp patterns; import names/addresses only, never decomp source.

## Milestones (see docs/DESIGN.md)
- M1 boot to file select ✅
- **M2 next:** find the routine that picks LttP vs Four Swords after file select (TCP debugger `--tcp`, traces), then hook it in `src/fs_boot.cpp` via `[[mod_function_hook]]` so every instance boots into Four Swords.
- M3 two local consoles over `GbaLinkHub` → M4 Anniversary-style single player → M5 online 2–4 → M6 DSi content research (may be impossible).
