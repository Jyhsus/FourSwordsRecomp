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
Windows (verified 22 Oct 2026, windowed): `gba_recompile` with MSVC (VS 2026 Build Tools, generator "Visual Studio 18 2026"); the game with MSYS2 MinGW64 + SDL2 + Ninja (`C:\msys64\mingw64\bin` first on PATH, also at run time). Upstream GBARecomp only supports MinGW for playable exes. Steps in README.
- The BIOS step rewrites a tracked file in the `gbarecomp` submodule (`src/runtime/generated_bios/`); `git status` shows `m gbarecomp`. Never commit it.
- Debug loop: `FourSwordsRecomp.exe --no-window --tcp 19890 --save-path <tmp>.sav game.toml`, then line-delimited JSON: `run_frames {n, keyinput}` (KEYINPUT active-low, A=bit0), `read_iwram/read_ewram {addr,len}`, `screenshot`, `savestate_save/load {path}` (Windows path), `quit`. Use a scratch save path so tests don't touch `saves/`.

## Known facts / gotchas
- Code ends near `0x08138000`; tables and data follow. `aot_scan` stays off: even limited to `0x08000000–0x08140000` it produced ~490k "functions" and ~20 GB RAM.
- Unbounded ("BX walked") auto jump tables get over-sized and seed ARM junk in data that runs into the `0xFF` padding at `0x0868_0000+`. Symptom: function count jumps from ~33k to ~490k. Fix: `GBARECOMP_JT_REPORT=1` recompile into a scratch dir, then `python tools/audit_jump_tables.py ROM <logs...> > symbols/AZLE_symbols.toml` (the overlay `regen.ps1` loads). Upstream finder bug.
- `[[jump_table]]` with Thumb pointers needs `entries_mode = "auto"`; `"thumb"` keeps bit 0 and decodes odd PCs.
- `evicted N host-less call-return entries` / `call-return stack overflow` = code reached through RAM function pointers running in the self-heal bridge (was the m4a sound driver). Check depth with the CPU0 section of a `--dump-state` file; a healthy run stays near 0. Seed the pointer table instead of ignoring the warning.
- Long runs: `GBARECOMP_INPUT_REPLAY=<file>` (lines `frame,0xKEYINPUT`, value held until the next line) with `--frames N --dump-png --dump-state`. TCP `run_frames` stepping exposes bridge bugs that free running doesn't.
- Self-heal bridge entered at a mid-function return address can run away (200M instructions, pc in BIOS IntrWait `0x348`) and abort. Cure: make that code static (seed the pointer table). Also seen: self-heal gcc overlays fail (`g_runtime_resume_pc` undeclared) and windowed `--frames` runs end with `terminate called without an active exception`. Both upstream.
- Game modes: byte `0x03006D10`, table `0x0842731C`, setter `set_game_mode` `0x0800B5EC`; 8 = file select, 6 = CHOOSE A GAME, 9 = Four Swords, 13 = LttP. `start_four_swords` = `0x08030E0C`. Details in docs/DESIGN.md §2.
- Save: 64 Kbit EEPROM (`EEPROM_V122` @ `0x08427298`). The `.sav` stores each 8-byte block reversed. Slots, checksum, name charset (6 chars, cp1252) and the system block at `0x03000000` (`[4]` = current file): docs/DESIGN.md §8.1. Edit saves only through `src/fs_save.cpp` (`build/fs_save_test.exe` checks it). `--slot 1-3` boots a file directly. Test on a copied `.sav` via `--save-path`.
- With one GBA the Four Swords lobby cannot be left (no key works). The quit step is `0x08030018` (lobby step 7, `0x03000BFC` = 7 in mode 9 sub 2 reproduces it); `src/fs_boot.cpp` ends a `--slot` session there via `gbarecomp::request_quit()`. To test the windowed loop, put that savestate in `--state-dir <dir>` as `state1` and press F1 in game.
- Windowed flow (`src/main.cpp`): launcher → file screen (`src/fs_slots_ui.cpp`, own ImGui window in `game_launcher_ui`) → `run_game()`; quitting Four Swords (`fsr::quit_to_menu()`) loops back to the launcher, so `run_game()` runs more than once per process. Skipped for headless/scripted runs, explicit `--slot`/`--rom` and netplay.
- `gbarecomp` carries local patches from `patches/gbarecomp/` (apply with `git -C gbarecomp apply`, never commit inside the submodule; CMake checks). Each is written as the upstream PR.
- Boot reaches the Nintendo/Capcom logos (frame ~300) and "CHOOSE A FILE" (frame ~900). The A Link to the Past / Four Swords choice comes **after** file select.
- Interpreter misses are logged to `recomp_master_misses_AZLE.toml.frag`; review them before merging into `game.toml`.
- Linked sessions (`GbaMultiplayerSession`, netplay) have **no interpreter fallback**: a missing native entry aborts with `multiplayer native dispatch entry is missing: <addr>`. Find RAM code sources by dumping RAM and searching the ROM (then `[[code_copy]]`); for Thumb functions reached through pointers in ROM data rerun `tools/scan_code_pointers.ps1` after `tools/regen.ps1` (writes `symbols/AZLE_data_pointers.toml`) and regenerate. New over-sized auto jump tables: `GBARECOMP_JT_REPORT=1` + `tools/audit_jump_tables.py` (Python: `C:\msys64\mingw64\bin\python.exe`), append only the new tables to `symbols/AZLE_symbols.toml`.
- Local multiplayer reuses `run_game()`'s netplay hand-off: `fsr::make_local_launch()` returns a `GbaNetplayLaunch` with `enabled` and its own `run`, so the runtime resolves ROM/BIOS/window settings first. Test headless-ish with `--local ... --frames N --fullscreen=0 --save-path <tmp>` plus `FSR_LOCAL_INPUTS` (probe script) and `FSR_LOCAL_DUMP` (BMP). Each GBA boots the file in its own system block (`fsr::kBootSavedFile`).
- Netplay: `src/main.cpp` sets `max_players = 4`. The upstream online runner never writes the `.sav` (only paired checkpoints). Every peer runs all virtual GBAs, and only inputs cross the network.
- Reference engine: *The Minish Cap* (same studio). Use `zeldaret/tmc` names and MinishCapRecomp patterns; import names/addresses only, never decomp source.

## Milestones (see docs/DESIGN.md)
- M1 boot to file select ✅
- **M2 ✅** (launcher toggle pending): `src/fs_boot.cpp` hooks the mode-6 handler `0x0803074C` (not `set_game_mode`, which misses the soft-reset inline entry at `0x0803072C`) and turns it into a tail call to `start_four_swords`. With one GBA it reaches the lobby ("Linking with other systems…", mode 9 sub 2) and is stable to frame 40000. Details in docs/DESIGN.md §2.
- **Before M3, docs/DESIGN.md §8 in order:** 8.1 saves in 3 slots (✅ save model, `--slot`, launcher file screen, quit back to the launcher; `request_quit()` patch still to send upstream), 8.2 no BIOS (HLE), 8.3 Ship of Harkinian-style UI.
- **M3 ✅ (one process, docs/DESIGN.md §9):** two GBAs on one `GbaLinkHub` cable boot cold, link and enter a Four Swords game together (`FourSwordsLinkProbe` + `tests/m3_two_gba.inputs`). `0x03006D18` was m4a `SoundMainRAM`, not link code. Needs local patch 0002 (cable transfer time 5,243 cycles for 2–4 GBAs; root cause of the timing gap still open). Pending: netplay path (2 processes / launcher).
- **Local split-screen multiplayer ✅ (docs/DESIGN.md §10):** file screen → Multiplayer → Local, or `--local 2,guest,...`. `src/fs_local.cpp` runs 2–4 GBAs in one window (side by side / 2×2), controller N = player N, player 1's audio. Saves merge by file into the one `.sav` (`Image::copy_file`); a file is played once at most, extra players are guests (never saved). Merge with real game writes and the clicked-through UI flow still unchecked.
- Next: M4 Anniversary-style single player → M5 online 2–4 → M6 DSi content research (may be impossible).
