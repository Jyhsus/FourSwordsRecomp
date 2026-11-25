---
name: build-project
description: FourSwordsRecomp build and regeneration procedures - gba_recompile, BIOS step, tools/regen, symbols and jump-table audits, game build, tools\play.cmd. Invoke manually (/build-project) before building, regenerating or running the game.
disable-model-invocation: true
---

# Build, regenerate and run

Moved from CLAUDE.md (25 Nov 2026), text kept as it was. Windows step-by-step for users: README.md ("Building (Windows)"). Design background: docs/DESIGN.md §1, §2, §9. Hard rules (never commit the BIOS step's output, never hand-edit `generated/`) stay in CLAUDE.md.

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

Figures here are as verified on the date given; other documents give other figures (see "Open discrepancies").

- The BIOS step rewrites a tracked file in the `gbarecomp` submodule (`src/runtime/generated_bios/`); `git status` shows `m gbarecomp`. Never commit it.
- `gbarecomp` carries local patches from `patches/gbarecomp/` (apply with `git -C gbarecomp apply`, never commit inside the submodule; CMake checks). Each is written as the upstream PR. List: patches/README.md.
- Start the game with `tools\play.cmd` (puts `C:\msys64\mingw64\bin` first on PATH; MinGW64 is not on the Windows PATH, and Git's ucrt64 DLLs are incompatible). Playtest guide: docs/PLAYTEST.md (+ .es.md).
- Python on this machine: `C:\msys64\mingw64\bin\python.exe`.

## Regeneration facts and gotchas
- Code ends near `0x08138000`; tables and data follow. `aot_scan` stays off: even limited to `0x08000000–0x08140000` it produced ~490k "functions" and ~20 GB RAM. (docs/DESIGN.md §1 says it "needs more than 6 GB of RAM": same fact, different figure, both kept.)
- Unbounded ("BX walked") auto jump tables get over-sized and seed ARM junk in data that runs into the `0xFF` padding at `0x0868_0000+`. Symptom: function count jumps from ~33k to ~490k. Fix: `GBARECOMP_JT_REPORT=1` recompile into a scratch dir, then `python tools/audit_jump_tables.py ROM <logs...> > symbols/AZLE_symbols.toml` (the overlay `regen.ps1` loads). Upstream finder bug. Narrative: docs/DESIGN.md §2.
- `[[jump_table]]` with Thumb pointers needs `entries_mode = "auto"`; `"thumb"` keeps bit 0 and decodes odd PCs.
- New over-sized auto jump tables: `GBARECOMP_JT_REPORT=1` + `tools/audit_jump_tables.py`, append only the new tables to `symbols/AZLE_symbols.toml`.
- Thumb code reached through pointers in the ROM: rerun `tools/scan_code_pointers.ps1` after `tools/regen.ps1` (writes `symbols/AZLE_data_pointers.toml`: PUSH-LR functions from any pointer, and pointer tables inside the code area as `[[jump_table]]`, e.g. `0x08009A08`) and regenerate. Details: docs/DESIGN.md §9, §10.
- In a `.ps1` with a `$Rom` parameter never assign to `$rom` (same variable, typed `[string]`: the script hangs).
- Interpreter misses are logged to `recomp_master_misses_AZLE.toml.frag` (git-ignored); review them before merging into `game.toml`.
- Never hand-edit `generated/`; fix `game.toml`, symbols or `src/` hooks, then regenerate.

## Open discrepancies (kept, not resolved; 25 Nov 2026)
- Function count: "1,537 functions, 32 shards" (this build recipe and README, 22 Oct 2026) vs ~33,000 / 33,465 (DESIGN §2) and ~47,150 (DESIGN §9; 47,077 Thumb entries after the `0x08009A08` fix, §10).
- `aot_scan` memory: ~20 GB (above) vs "more than 6 GB" (DESIGN §1).
- Regeneration time: ~1 min (above, DESIGN §2) vs ~2 min (README).
