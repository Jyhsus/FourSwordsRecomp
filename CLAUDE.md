# CLAUDE.md — FourSwordsRecomp

Static recompilation of *The Legend of Zelda: Four Swords* from the USA cartridge
*A Link to the Past & Four Swords* (`AZLE`, SHA-1 `a272055abbbf6c26b0cd54c87395d01699589161`),
built on GBARecomp (submodule `gbarecomp/`, launcher `recomp-ui/`).
Goals and design: `docs/DESIGN.md`. Milestones, pending and unverified items: `PROGRESS.md`.

## Language and docs
- The maintainer (Jyhsus) writes in Spanish; reply in Spanish.
- Public docs (README, `docs/`, `patches/`, `PROGRESS`) are English-first with Spanish mirrors (`*.es.md`). Keep both in sync.
- CLAUDE.md and `.claude/skills/` are for agents: English, no mirror.
- Keep this file short. Put new findings in their home (table below), dated, saying what was verified and what was not. Never drop warnings, unverified results, pending issues or check dates when moving or editing docs. If documents disagree, keep both figures and flag it; do not assume one wins.

## Hard rules
- Never commit or distribute the ROM, the BIOS, `generated/` output, DSi dumps, extracted assets, box art or `.sav` saves. `.gitignore` enforces most of this; check `git status` before committing.
- Never hand-edit `generated/`. Fix `game.toml`, symbols or `src/` hooks, then regenerate (`tools/regen.ps1` / `tools/regen.sh`).
- Engine (gbarecomp) bugs belong upstream; keep game-specific code in this repo. Local engine changes are patches in `patches/gbarecomp/` (`git -C gbarecomp apply`; CMake checks). Never commit inside `gbarecomp/`.
- `git status` showing `m gbarecomp` is expected (patches; the BIOS step rewrites `src/runtime/generated_bios/`). Never commit it.
- Edit saves only through `src/fs_save.cpp`. Test on a copy via `--save-path`, never on `saves/`.
- Local player N = machine N-1 = cable port N-1 = the game's P<N> = view N = plan entry N-1 (file, controller). Never reorder views or inputs after the plan.
- Reference engine: *The Minish Cap* (same studio). Use `zeldaret/tmc` names and MinishCapRecomp patterns; import names/addresses only, never decomp source.
- License: PolyForm Noncommercial (same as GBARecomp).

## Commits
Conventional commit types are required (`feat`, `fix`, `docs`, `chore`, `test`, `refactor`, `perf`, `build`), as in `git log`: `fix(wide): ...`.

## Windows
The game builds and runs with MSYS2 MinGW64 + SDL2 + Ninja; `C:\msys64\mingw64\bin` must be first on PATH (also at run time; it is not on the Windows PATH, and Git's ucrt64 DLLs are incompatible). Start the game with `tools\play.cmd`. `gba_recompile` is built with MSVC. Python: `C:\msys64\mingw64\bin\python.exe`.

## Where things live
| Need | Go to |
|---|---|
| Build, regenerate, symbols, jump tables, `play.cmd` (manual) | skill `build-project` |
| TCP debug loop, input replay, bridge/call-return problems, missing native entries, local-session replays, tests | skill `debug-project` |
| 16:9 wide view, stage buffers, probe replays | skill `widescreen-debug` (read before touching `src/fs_wide.cpp`) |
| Boot hook and game modes (mode byte `0x03006D10`, `start_four_swords` `0x08030E0C`) | `docs/DESIGN.md` §2 |
| Save layout, slots, name charset, quit path, launcher file screen | `docs/DESIGN.md` §8.1 |
| Two GBAs on one cable (M3), cable timing patch | `docs/DESIGN.md` §9 |
| Local multiplayer: layout and scaler (`src/fs_layout.h`), controllers (`src/fs_pads.*`), saves merge, performance, graphics reports | `docs/DESIGN.md` §10 |
| Widescreen findings | `docs/DESIGN.md` §11 |
| Netplay (`max_players = 4`; the upstream online runner never writes the `.sav`) | `docs/DESIGN.md` §4, §10 |
| Playtest procedure and evidence | `docs/PLAYTEST.md` (+ `.es.md`) |
| Engine patches and upstream status | `patches/README.md` |
| Milestones M1–M6, pending, unverified, discrepancies | `PROGRESS.md` |

Next milestones: M4 Anniversary-style single player, M5 online 2–4, M6 DSi content research (may be impossible).
