# FourSwordsRecomp

**The Legend of Zelda: Four Swords (GBA), statically recompiled to native PC — on its own, without A Link to the Past.**

*🇪🇸 Leer en español: [README.es.md](README.es.md)*

> **Built on [GBARecomp](https://github.com/mstan/gbarecomp) by Matthew Stan.**
> This project would not exist without it: GBARecomp provides the ARM7TDMI
> recompiler, the modeled GBA hardware, the launcher, link-cable netplay and the
> self-healing runtime. If you like this project, please ⭐ star and support
> [GBARecomp](https://github.com/mstan/gbarecomp) and its other game ports, and
> report framework bugs upstream.

> ⚠️ **Early development.** This is not a playable release yet. See
> [Status](#status) and the [design & roadmap](docs/DESIGN.md).

---

## What is this?

In 2002, *Four Swords* shipped on the same Game Boy Advance cartridge as
*A Link to the Past*. It needed 2–4 players, each with their own GBA, cartridge
and a link cable. In 2011 Nintendo released the *Anniversary Edition* for
DSi/3DS, which added single-player mode and new stages, but it was only
available for free for a limited time.

FourSwordsRecomp aims to make the original GBA game playable natively on PC:

| Goal | How | Status |
|---|---|---|
| **Four Swords only** | Boot straight into Four Swords; *A Link to the Past* is never started | 📋 planned |
| **Single player, Anniversary style** | Two virtual GBAs run locally; you control one Link, the other follows; swap with L/R | 📋 planned |
| **Online multiplayer, 2–4 players** | Every PC runs all virtual GBAs; only controller inputs travel over the network (delay or rollback netcode) | 🔧 framework ready, game untested |
| **Anniversary Edition content** | Research only: the DSi version is different code; possibly convert stage *data* from the player's own dump | 🔬 may be impossible |
| **Launcher** | GBARecomp's launcher: asks for the USA ROM + BIOS, verifies them, netplay lobby | 🔧 wired, untested |

The detailed reasoning for each goal is in **[docs/DESIGN.md](docs/DESIGN.md)**.

### What "static recompilation" means

The game's ARM machine code is translated **ahead of time** into C++ and
compiled into a normal executable. The game logic runs as native code instead
of being emulated instruction by instruction. The hardware around it (graphics,
sound, timers, save chip, link port) is modeled by GBARecomp's runtime. That
opens the door to things an emulator can't easily do, such as hooking the
game's own functions to add a single-player mode.

## What you need

* Your **own dump** of *The Legend of Zelda: A Link to the Past & Four Swords*
  (**USA**, `AZLE`). SHA-1 and details in [baserom.md](baserom.md).
* Your **own GBA BIOS** dump (SHA-1 `300c20df6731a33952ded8c436f7f186d25d3492`).

**No ROM, BIOS, generated code or game assets are included or will ever be
distributed.** The generated C++ is derived from the ROM, so it is produced on
your machine and is git-ignored.

## Building (Windows)

Requirements: Git, Python 3.11+, Visual Studio 2022 with *Desktop development
with C++* and CMake tools, and SDL2 for the windowed build.

```powershell
git clone --recurse-submodules https://github.com/<your-user>/FourSwordsRecomp.git
cd FourSwordsRecomp

# 1. Build the recompiler
cmake -S gbarecomp -B gbarecomp\build -A x64
cmake --build gbarecomp\build --config Release --target gba_recompile

# 2. Put your files in place (never committed)
mkdir roms
copy "D:\path\to\your\AZLE.gba" roms\zelda_lttp_four_swords_usa.gba
copy "D:\path\to\your\gba_bios.bin" gbarecomp\bios\gba_bios.bin

# 3. Generate C++ from your ROM (takes several minutes: 8 MiB ROM)
powershell -ExecutionPolicy Bypass -File tools\regen.ps1

# 4. Build the game
cmake -S . -B build -A x64
cmake --build build --config Release
```

On Linux/macOS use `tools/regen.sh` and a normal CMake build. The GBARecomp
[getting-started guide](https://github.com/mstan/gbarecomp/blob/main/docs/GETTING_STARTED.md)
covers toolchain problems.

## Status

| Milestone | Status |
|---|---|
| ROM, BIOS and save type identified; `game.toml` written | ✅ |
| Whole ROM generated to C++ (1,537 functions; misses are interpreted and cached) | ✅ |
| M1 — native build boots through the BIOS and the Nintendo/Capcom logos to the file-select screen | ✅ (headless Linux, 22 Oct 2026) |
| M2 — direct boot into Four Swords | 📋 |
| M3 — two consoles linked locally through the emulated cable | 📋 |
| M4 — Anniversary-style single player | 📋 |
| M5 — online 2 players, then 3–4 | 📋 |
| M6 — Anniversary content research (go / no-go) | 🔬 |

## Repository layout

| Path | Contents |
|---|---|
| `gbarecomp/` | GBARecomp framework (git submodule) |
| `recomp-ui/` | Shared launcher UI (git submodule) |
| `game.toml` | Recompiler and runtime configuration for `AZLE` |
| `src/` | Game-owned code: entry point, Four Swords hooks |
| `tools/` | `regen.ps1` / `regen.sh` — regenerate C++ from your ROM |
| `symbols/` | Function/data names discovered during reverse engineering |
| `generated/` | Local generated C++ (git-ignored) |
| `docs/` | Design, roadmap, research notes (English + Spanish) |

## Contributing

Help is very welcome, especially with reverse engineering (finding the game-select
routine, player object structures in RAM, the link protocol) and testing.
Engine problems belong in [GBARecomp's issue tracker](https://github.com/mstan/gbarecomp/issues);
Four Swords–specific problems belong here.

## Credits

* **[GBARecomp](https://github.com/mstan/gbarecomp)** — Matthew Stan and contributors: recompiler, runtime, launcher ([recomp-ui](https://github.com/mstan/recomp-ui)) and netplay.
* **[zeldaret/tmc](https://github.com/zeldaret/tmc)** — *The Minish Cap* decompilation, a reference for the shared Capcom/Flagship engine.
* **[recomp-net](https://github.com/RetroPortingToolKit/recomp-net)** and **[rbengine](https://github.com/RetroPortingToolKit/rbengine)** — networking and rollback used by GBARecomp.

## License

[PolyForm Noncommercial 1.0.0](LICENSE), the same license as GBARecomp, which
this project builds on. Non-commercial use only.

*The Legend of Zelda*, *Four Swords* and *A Link to the Past* are trademarks of
Nintendo. This project is not affiliated with or endorsed by Nintendo or by
the GBARecomp authors.
