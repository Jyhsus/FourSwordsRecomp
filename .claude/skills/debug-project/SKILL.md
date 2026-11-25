---
name: debug-project
description: FourSwordsRecomp debugging and reproduction - TCP debug loop, input replay, call-return/self-heal bridge problems, missing native entries in linked sessions, local-session replays, scripted multi-GBA checks, unit tests. Use when diagnosing a crash, abort, desync or stall, or replaying a recorded session. Not for building (build-project) or the 16:9 view (widescreen-debug).
---

# Debugging and reproducing runs

Moved from CLAUDE.md (25 Nov 2026), text kept as it was. Narrative and evidence: docs/DESIGN.md §2, §8.1, §9, §10. Build and regeneration: skill `build-project`. 16:9 view and probe replays of wide sessions: skill `widescreen-debug`. Test saves only on copies (`--save-path <tmp>.sav`), never `saves/`.

## TCP debug loop
`FourSwordsRecomp.exe --no-window --tcp 19890 --save-path <tmp>.sav game.toml`, then line-delimited JSON: `run_frames {n, keyinput}` (KEYINPUT active-low, A=bit0), `read_iwram/read_ewram {addr,len}`, `screenshot`, `savestate_save/load {path}` (Windows path), `quit`. Use a scratch save path so tests don't touch `saves/`.

## Long runs and replays
- `GBARECOMP_INPUT_REPLAY=<file>` (lines `frame,0xKEYINPUT`, value held until the next line) with `--frames N --dump-png --dump-state`. TCP `run_frames` stepping exposes bridge bugs that free running doesn't.
- Boot reaches the Nintendo/Capcom logos (frame ~300) and "CHOOSE A FILE" (frame ~900). The A Link to the Past / Four Swords choice comes **after** file select.
- A recorded local session replays exactly in the probe (`--save0 start.sav --save1 guest --inputs inputs.inputs`) and in the exe (`FSR_LOCAL_INPUTS`, `--save-path` copy). The item menu is held L (WIN1, does not pause).

## Call-return and self-heal bridge
- `evicted N host-less call-return entries` / `call-return stack overflow` = code reached through RAM function pointers running in the self-heal bridge (was the m4a sound driver). Check depth with the CPU0 section of a `--dump-state` file; a healthy run stays near 0. Seed the pointer table instead of ignoring the warning. (Case history: docs/DESIGN.md §2.)
- Self-heal bridge entered at a mid-function return address can run away (200M instructions, pc in BIOS IntrWait `0x348`) and abort. Cure: make that code static (seed the pointer table). Also seen: self-heal gcc overlays fail (`g_runtime_resume_pc` undeclared) and windowed `--frames` runs end with `terminate called without an active exception`. Both upstream.
- Interpreter misses go to `recomp_master_misses_AZLE.toml.frag`; review before merging into `game.toml`.

## Linked sessions
- Linked sessions (`GbaMultiplayerSession`, netplay) have **no interpreter fallback**: a missing native entry aborts with `multiplayer native dispatch entry is missing: <addr>`. Find RAM code sources by dumping RAM and searching the ROM (then `[[code_copy]]`); for Thumb code reached through pointers in the ROM rerun `tools/scan_code_pointers.ps1` after `tools/regen.ps1` (see build-project) and regenerate. In a `.ps1` with a `$Rom` parameter never assign to `$rom` (the script hangs). New over-sized auto jump tables: `GBARECOMP_JT_REPORT=1` + `tools/audit_jump_tables.py` (Python: `C:\msys64\mingw64\bin\python.exe`), append only the new tables to `symbols/AZLE_symbols.toml`.
- With one GBA the Four Swords lobby cannot be left (no key works). The quit step is `0x08030018` (lobby step 7, `0x03000BFC` = 7 in mode 9 sub 2 reproduces it); `src/fs_boot.cpp` ends a `--slot` session there via `gbarecomp::request_quit()`. To test the windowed loop, put that savestate in `--state-dir <dir>` as `state1` and press F1 in game.
- In the linked lobby, B pressed twice by any player quits to the launcher (lobby step 7).

## Local multiplayer runs
- Local multiplayer reuses `run_game()`'s netplay hand-off: `fsr::make_local_launch()` returns a `GbaNetplayLaunch` with `enabled` and its own `run`, so the runtime resolves ROM/BIOS/window settings first. Test headless-ish with `--local ... --frames N --fullscreen=0 --volume 0 --save-path <tmp>` plus `FSR_LOCAL_INPUTS` (probe script), `FSR_LOCAL_DUMP` (BMP) and `GBARECOMP_FORCE_DRAWABLE=WxH` (layout at that window size). Each GBA boots the file in its own system block (`fsr::kBootSavedFile`).
- A local session leaves `fsr_local_last.{log,inputs,sav}` next to the exe (overwritten each session; don't run sessions in parallel if you need them) and `build/sessions/YYYY-MM-DD_HH-MM-SS/` (session.log, inputs.inputs, start.sav, config.ini, keybinds.ini; never overwritten). The log starts with the commit/engine patches (`cmake/fsr_revision.cmake`, also `--version`), and every 600 frames gives each GBA's mode/state/stage/floor/camera and the frame time split into emulation/drawing/presenting. An error in the runner is logged there and shown in a dialog. The 4-player exit testers reported is not reproduced and not fixed.
- Local pacing uses `gbarecomp::FramePacer` (16.78 +- 0.75 ms between presents); the old `sleep_until` gave +-4.5 ms. `main()` calls `fsr::prefetch_own_image()` (`src/fs_sysinfo.cpp`, PrefetchVirtualMemory): the ~111 MB of code on the dev machine's HDD otherwise stalled first-time frames up to 0.7 s. Reproduce cold starts with `xcopy /J` of the exe. The session log and input record are written by their own thread (`SessionLog` in `src/fs_local.cpp`): a flushed line on the frame thread cost 20-140 ms on the HDD. Slow frames (>33.5 ms) are listed in the session log with a per-part breakdown and page faults. Link's shadow is drawn by the game on alternate frames (OAM) - original, keep it.
- Layout and scaler: `src/fs_layout.h`. The canvas is the window's pixels; local fullscreen is always desktop mode (exclusive mode switched the monitor to 1024x768).
- Windowed flow (`src/main.cpp`): launcher → file screen (`src/fs_slots_ui.cpp`, own ImGui window in `game_launcher_ui`) → `run_game()`; quitting Four Swords (`fsr::quit_to_menu()`) loops back to the launcher, so `run_game()` runs more than once per process. Skipped for headless/scripted runs, explicit `--slot`/`--rom` and netplay.

## Saves
- Edit saves only through `src/fs_save.cpp` (`build/fs_save_test.exe` checks it). `--slot 1-3` boots a file directly. Test on a copied `.sav` via `--save-path`. Layout and checksum: docs/DESIGN.md §8.1.
- No automated run has made the game write its EEPROM (not even after a boss); the save merge with real writes is only testable by playing (UNVERIFIED).

## Tests and scripted checks
- Unit tests: `ctest -R fs_pads` (`tests/fs_pads_test.cpp`, SDL virtual controllers, real drivers off), `ctest -R fs_layout` (`tests/fs_layout_test.cpp`), `build/fs_save_test.exe`; README: `ctest --test-dir build -R "^fs_"`.
- `src/fs_pads.{h,cpp}` (`PadSeats`, `local_pads()`): a pad connected at start but unassigned is never handed out; reconnection matches light, then model, then first waiting place. Local player N = machine N-1 = cable port N-1 = the game's P<N> = view N = plan entry N-1; controllers come from `fsr::local_pads()` (player light, then connection order), not SDL device order; `HostWindow`'s own controller is off in local play (patch 0003). UNVERIFIED: no controller was connected on the dev machine, so controller order, the "pressed" marker and hot-plug are untested on hardware.
- Scripted multi-GBA checks: `tools/make_local_inputs.py identity|soak N` → `tests/inputs/`.
- Forcing `0x03005050`=10 outside stage play (e.g. on stage select after a game over) jumps into data (`02016d90 ARM`): a test-device artifact, not a game bug.
