# Progress

*Versión en español: [PROGRESS.es.md](PROGRESS.es.md)*

Milestones, pending work, known problems and unverified results. It links to the
evidence in [docs/DESIGN.md](docs/DESIGN.md) and does not repeat it. Branch,
commits and tags: ask Git. Last reviewed: 25 Nov 2026.

## Milestones

| Milestone | Status | Details |
|---|---|---|
| M1 — boot to file select | ✅ | DESIGN §1 |
| M2 — direct boot into Four Swords, stable one-GBA lobby (frame 40000) | ✅ launcher toggle pending (`FSR_STOCK_MENU=1` shows the stock screen) | DESIGN §2 |
| Before M3, DESIGN §8 in order: 8.1 saves in 3 slots | ✅ (save model, `--slot`, launcher file screen, quit back to the launcher) | DESIGN §8.1 |
| 8.2 no BIOS needed by the player (HLE) | 📋 | DESIGN §8.2 |
| 8.3 Ship of Harkinian-style interface | 📋 | DESIGN §8.3 |
| M3 — two GBAs on one cable | ✅ in one process; netplay path (2 processes / launcher) pending | DESIGN §9 |
| Local split-screen multiplayer, 2–4 players | ✅ | DESIGN §10, [PLAYTEST](docs/PLAYTEST.md) |
| Widescreen 16:9 | 🔬 experimental, optional | DESIGN §11 |
| M4 — Anniversary-style single player | 📋 | DESIGN §3 |
| M5 — online 2 players, then 3–4 | 📋 | DESIGN §4 |
| M6 — Anniversary content research | 🔬 may be impossible | DESIGN §5 |

## Pending

- Send the `request_quit()` patch upstream; the other two local patches also say "PR pending" ([patches/README.md](patches/README.md)). Check the real quit with two GBAs (DESIGN §8.1).
- Root cause of the cable timing gap (5,243-cycle override, patch 0002), and the same check over real netplay (DESIGN §9).
- Online play never writes a player's `.sav` (upstream runner); progress made online is lost (DESIGN §10).
- Controller buttons are fixed to SDL's standard layout. The Local page keeps each player's controller between runs by position, not by device (DESIGN §10).
- Area-entry iris in 16:9: margins open row by row; alternatives not chosen (DESIGN §11).
- Widescreen: a sprite 64 pixels wide may appear late on the left; whether the game creates or removes objects by distance inside the margins is unknown (DESIGN §11).

## Known problems

- The exit some testers had with four players is not reproduced and not fixed (DESIGN §10; evidence in [PLAYTEST](docs/PLAYTEST.md) §6).
- Four players in 16:9 do not keep full speed on the development machine (DESIGN §10 table).
- Static coverage of Four Swords paths is not complete: linked sessions have no interpreter fallback, and one missing entry was found by a tester (`0x08009A08`); that no others remain is not proven (DESIGN §10).
- Washed-out colours and flickering sprites reported by a tester: not reproduced, no reference comparison done. `game.toml` default is `screen = "raw"` since 16 Nov 2026 (it was `"frontlit"`); not shown to be the cause (DESIGN §10).
- Upstream engine issues seen: self-heal gcc overlays failing (`g_runtime_resume_pc`), `terminate called without an active exception` at the end of windowed `--frames` runs, the jump-table finder over-sizing tables ([debug-project skill](.claude/skills/debug-project/SKILL.md), DESIGN §2).
- Local page argument priority (25 Nov 2026): one manual try of `--local-scale fit` over a saved `integer` was reported as failed; the repeat on the same executable passed. Not reproduced, cause unknown (DESIGN §10).
- The program aborts at startup when its folder has non-ASCII characters (`std::filesystem_error`, "Cannot convert character sequence"; 25 Nov 2026). Seen with a folder named with "ñ", `--no-window` and `--local`, in the build of `792d4da` and in the current one. The failing call is not located and the launcher start was not tried. `src/main.cpp` also builds a path from `argv[0]` (settings file), so a fix must cover it; that line adds one more failing point, with no regression observed. Unverified leads from review: `gbarecomp/src/runtime/asset_picker.cpp` (about line 29) may be the failing call, the launcher start probably fails too, and `SDL_GetBasePath()` (UTF-8) could replace `argv[0]` in `src/main.cpp`.

## Unverified

- Merge of the game's real EEPROM writes into the `.sav`: no automated run made the game write its EEPROM, not even after a boss (DESIGN §10, [PLAYTEST](docs/PLAYTEST.md) §3 F).
- Real controllers: order among several, two of one model, a saved one absent, the "pressed" marker. With one Xbox 360 controller (25 Nov 2026, manual) detection, disconnection and late connection passed; before that none had been connected on the development machine (DESIGN §10).
- A real four-person game, and the windowed flow with players on saved files: launcher → files → Multiplayer → Local was clicked through on 25 Nov 2026 with two guests (DESIGN §10).
- Local page settings kept between runs: manual checks passed on 25 Nov 2026 (see Known problems for argument priority). Not checked: controller positions with several controllers, a computer restart, `--local-keyboard` and `--local-view` in the launcher flow; no test covers the wiring in `src/main.cpp` or an older file surviving a failed write. Limits seen in review, not run, are listed in DESIGN §10.
- The testers' controller-order explanation (device order differing from the pads' player lights) is the likely cause, not confirmed (DESIGN §10).
- Widescreen: a boss fight, a stage finished by play, a HUD with more hearts or a longer rupee count, play with people, slower machines; "P1 is down!" not seen (DESIGN §11).
- Link's shadow blink on a 144 Hz monitor, and 60/120 Hz desktop modes: untested (DESIGN §10, [PLAYTEST](docs/PLAYTEST.md) §7).

## Discrepancies kept (not resolved)

Values below are as each document states them, with its date. None has been re-measured to decide between them.

- Generated functions: 1,537 and 32 shards (README, build recipe, 22 Oct 2026) vs about 33,000 / 33,465 (DESIGN §2) vs about 47,150 (DESIGN §9) and 47,077 Thumb entries (DESIGN §10).
- `aot_scan` memory: about 20 GB, ~490k functions (earlier CLAUDE.md) vs "more than 6 GB" (DESIGN §1).
- Regeneration time: about 1 min (earlier CLAUDE.md, DESIGN §2) vs about 2 min (README).
- Performance, four GBAs on the dev machine: about 11 ms in 3:2 and about 14 ms in 16:9 (earlier CLAUDE.md) vs 11.88 and 15.68 ms (DESIGN §10 table) vs 12.0 and 15.2 ms (DESIGN §11, probe without window).
- README *Status* table still shows M3 as planned (📋) and 1,537 functions, while DESIGN §9 records M3 as done (23 Oct 2026).
- README Windows steps mention "~2 min" and VS 2022/2026; the earlier build recipe was verified with VS 2026 only.
- English and Spanish DESIGN differ in length (1,104 vs 1,175 lines); equivalence not checked.
