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
- Save: 64 Kbit EEPROM (`EEPROM_V122` @ `0x08427298`).
- Boot reaches the Nintendo/Capcom logos (frame ~300) and "CHOOSE A FILE" (frame ~900). The A Link to the Past / Four Swords choice comes **after** file select.
- Interpreter misses are logged to `recomp_master_misses_AZLE.toml.frag`; review them before merging into `game.toml`.
- Netplay: `src/main.cpp` sets `max_players = 4`. Every peer runs all virtual GBAs, and only inputs cross the network.
- Reference engine: *The Minish Cap* (same studio). Use `zeldaret/tmc` names and MinishCapRecomp patterns; import names/addresses only, never decomp source.

## Milestones (see docs/DESIGN.md)
- M1 boot to file select ✅
- **M2 ✅** (launcher toggle pending): `src/fs_boot.cpp` hooks `set_game_mode` and turns mode 6 into a tail call to `start_four_swords`. With one GBA it reaches the lobby ("Linking with other systems…", mode 9 sub 2) and is stable to frame 40000. Details in docs/DESIGN.md §2.
- **M3 next:** IWRAM Thumb code `0x03006D18–0x03006E96` is still bridged (likely link/serial); find its ROM source and declare a `[[code_copy]]`.
- M3 two local consoles over `GbaLinkHub` → M4 Anniversary-style single player → M5 online 2–4 → M6 DSi content research (may be impossible).
