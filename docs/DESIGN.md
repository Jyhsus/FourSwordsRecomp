# FourSwordsRecomp — Design and Roadmap

*Versión en español: [DESIGN.es.md](DESIGN.es.md)*

This document explains **how** each goal of the project is meant to work, what
is already provided by [GBARecomp](https://github.com/mstan/gbarecomp), and
what still has to be built. Status markers: ✅ done · 🔧 in progress · 📋 planned · 🔬 research (may turn out impossible).

---

## 0. Ground rules

* **No Nintendo data in this repository.** No ROM, no BIOS, no generated code
  (it is derived from the ROM), no DSi dump, no extracted assets, no box art.
  The player supplies their own dumps; the launcher verifies them by hash.
* **The cartridge image is never modified.** "Removing" A Link to the Past
  means *never booting it*, not cutting it out of the ROM (the two games share
  one binary and one engine; cutting would only break things).
* **Generated code is never hand-edited.** Fixes go into `game.toml`, the
  symbol files, or game-owned hooks in `src/`, then the code is regenerated.

## 1. The base: static recompilation with GBARecomp ✅/🔧

| Step | Status |
|---|---|
| ROM identified: USA *A Link to the Past & Four Swords*, game code `AZLE`, SHA-1 `a272055a…9161` | ✅ |
| BIOS verified (SHA-1 `300c20df…3492`, the one GBARecomp expects) | ✅ |
| Save hardware identified: 64 Kbit EEPROM (`EEPROM_V122` at `0x08427298`) | ✅ |
| `game.toml` written. Code lives in `0x08000000–0x0813FFFF`; the rest is data. The ahead-of-time pointer-table scan is off for now because over this ROM it needs more than 6 GB of RAM | ✅ |
| Whole ROM generated and compiled into a native host | ✅ |
| Cold boot → BIOS → Nintendo/Capcom logos → file-select screen (frame ~900) | ✅ |
| Static coverage of the Four Swords code paths (no interpreter fallbacks) | 📋 |

GBARecomp translates the ARM7TDMI code to C++ and links it against a modeled
GBA (PPU, audio, DMA, timers, EEPROM, serial port). Code the analyzer misses is
interpreted on first use and cached natively, so the game is playable while
coverage is completed.

Useful reference: *The Minish Cap* was made by the same studio (Capcom /
Flagship) right after Four Swords and shares a lot of engine design. The
[zeldaret/tmc](https://github.com/zeldaret/tmc) decompilation and
[MinishCapRecomp](https://github.com/mstan/MinishCapRecomp) are the closest
references for naming functions and structures. Only *names and addresses*
may be imported, never decompiled source.

## 2. Boot straight into Four Swords ✅ (launcher toggle pending)

On the cartridge, the player first picks a save file ("CHOOSE A FILE"), then
chooses between *A Link to the Past* and *Four Swords*.

**How the cartridge does it** (found with the TCP debugger, 22 Oct 2026):

| Address | What |
|---|---|
| `0x03006D10` (u8) | Current game mode; the main loop at `0x0800B1B2` calls `game_mode_handlers[mode]` (table `0x0842731C`) every frame |
| `0x03005050` (u8) | Sub-state of the current mode |
| `0x0800B5EC` | `set_game_mode(r0)`: writes the mode and clears the sub-states |
| mode 8 / 6 / 9 / 13 | File select / "CHOOSE A GAME" (`0x0803074C`) / Four Swords / A Link to the Past |
| `0x02016D62` (u8) | Cursor on "CHOOSE A GAME": 0 = LttP, 1 = Four Swords |
| `0x08030E0C` | `start_four_swords`: initialises Four Swords and calls `set_game_mode(9)` |

After a file is picked, file select calls `set_game_mode(6)`. On "CHOOSE A
GAME", picking Four Swords calls `start_four_swords` when more than one GBA is
linked; with one GBA it first shows a "This is a Multi-Pak multiplayer game"
notice, and pressing A through it still starts the Four Swords intro.

**Implementation:** `src/fs_boot.cpp` hooks the mode-6 handler `0x0803074C`
(declared as a `[[mod_function_hook]]` in `game.toml`). On its first frame the
handler becomes a tail call to `start_four_swords`, so picking a save file goes
straight to the Four Swords intro on every virtual GBA. `FSR_STOCK_MENU=1`
disables the hook to show the stock screen; a launcher option is still to do.

The first version hooked `set_game_mode` instead. A review before merging found
that it missed one way into mode 6: `AgbMain` (`0x0800B084`) calls
`0x08068108` at boot. If IWRAM holds a handoff marker (`0x54463453` at
`0x03000000` plus a 7-byte string), left there by a soft reset, it writes
mode 6 inline (`0x0803072C`) without calling `set_game_mode`. A cold boot never
has the marker. Hooking the handler covers every path; the inline entry was
checked by writing mode 6 / sub-state 0 over TCP on the file-select screen,
which started the Four Swords intro.

**Static coverage needed for this:** the mode tables are only reached through
function pointers, so they are declared as `[[jump_table]]`s in `game.toml`.
Seeding them exposed a recompiler problem: unbounded ("BX walked")
auto-detected jump tables were sized past their end, seeding ARM "functions"
in data that ran into the ROM's `0xFF` padding (~490k functions, ~20 GB of
RAM). `tools/audit_jump_tables.py` trims them; its output is the overlay
`symbols/AZLE_symbols.toml`. Result: about 33,000 functions (33,465 with all
the M2 seeds), generated in ~1 min.
This should also be fixed upstream.

**Is the tail call safe?** `start_four_swords` does not read `r0`–`r3`: its
first instruction after `push {lr}` is `bl 0x0800BED8`, which writes `r0` and
`r1` before reading them. So it does not matter that the hook enters it with
whatever the main loop left in `r0` instead of the link count the stock path
leaves there. These paths enter mode 6; the hook changes all of them:

| Entry | Context | Effect of the hook |
|---|---|---|
| `0x0800F87A` `set_game_mode(6)` | File select, after a file is picked | Intended: straight to Four Swords |
| `0x0803003E` `set_game_mode(6)` | Mode-5 sub-state (table `0x084278DC`), re-initialises and returns to "CHOOSE A GAME" | Goes back into Four Swords instead (fine for a Four Swords-only build) |
| `0x080310A6` `set_game_mode(6)` | Multi-Pak notice sub-state (table `0x08427980`), right after its own `start_four_swords` call | Unreachable while the hook is on: the mode-6 sub-states never run |
| `0x0803072C` inline write | `AgbMain` after a soft reset with the IWRAM handoff marker | Straight to Four Swords |

**Lobby reached and stable** (22 Oct 2026, Windows, headless). Input was
replayed with `GBARECOMP_INPUT_REPLAY` (create a file, pick it, press A every
40 frames until frame 20000), then the game idled to frame 40000. Through the
whole intro (Zelda, Vaati, the four Links) it reaches the Four Swords title with
"Linking with other systems… Please wait a moment." at mode 9, sub-state 2,
and stays there: no aborts, call-return stack depth 0 at frames 3000, 8000,
20000 and 40000, and A/B/START at the lobby change nothing.

**The "evicted 511 host-less call-return entries" warning** was a real leak
that ended in `generated call-return stack overflow` (abort) during the intro.
The runtime pushes a return address on every `BL` and pops it when the callee
returns. Savestates showed the stack filling with `0x08134A66` and
`0x08134A7A`, the return addresses of two m4a veneers that call
`*(0x02031AB8)` / `*(0x02031ABC)`. Those are entries 34/35 (0-based) of the
m4a sound driver's jump table, whose RAM copy starts at `0x02031A30`, so the
finder never saw the targets. The whole driver ran in the self-heal interpreter bridge, mixed
with native veneers, and those returns were never popped. The PC in the
warning (`0x08009476`, later `0x08034FE8`) is just whichever call was being
pushed when the stack filled. The same mix also made the bridge abort on the
IWRAM ARM routine `0x03005AE0` (copied from `0x080008A8`) as soon as sound
effects played. The leak came before the hook: savestates from the original
build already had 80 entries.

Fix in `game.toml`: the m4a jump table (`0x083C2654`, 36 entries), the IRQ
handler table (`0x08427360`, 12 entries, DMA-copied to `0x03000B70` by
`0x0800B6D0`; the VBlank entry before it points to EWRAM code and is left out),
`m4aSoundMain` (`0x081346FC`), `SoundMain` (`0x081339D4`), `MPlayMain`
(`0x08134074`), `0x0800B7FC`, a `[[code_copy]]` plus `[[extra_func]]` for
`0x03005AE0`, and the callback group at `0x0842757C` (3 entries). After that,
no ROM address is bridged on the Four Swords path (checked with a clean miss
log over the 40000-frame run). The stock A Link to the Past path still bridges
many ROM functions; that game is out of scope.

**Testing note:** stepping frame by frame over TCP (`run_frames`) exposed
these bridge problems sooner than free running, because each step ends at an
arbitrary PC. For long runs, prefer `--frames N` with `GBARECOMP_INPUT_REPLAY`
(lines `frame,0xKEYINPUT`) and `--dump-png` / `--dump-state`.

**Left for M3:** Thumb code in IWRAM at `0x03006D18`–`0x03006E96` is still
bridged (a jump-table candidate, likely the link/serial code; source copy not
found yet). Not needed with one GBA.

## 3. Single player like the Anniversary Edition 📋

**What the DSi Anniversary Edition does:** two Links instead of four on the
two-player maps. The second Link follows the leader; L/R switches which Link
you control (the other one sits down and can be picked up); a whistle (X)
recalls the other Link and gives control back to the green one. Block puzzles
that need several Links are assisted automatically.

**Why the GBA game cannot do this on its own:** every player in Four Swords is
a separate GBA with its own copy of the game, kept in step through the link
cable. The game refuses to start an adventure with one console.

**Plan — a local "virtual party":**

1. Start a *local* session with **two virtual GBAs connected by the emulated
   cable**, in the same process. GBARecomp already runs several `GbaInstance`s
   on one shared timeline with a cable hub (`GbaLinkHub`) for netplay; single
   player reuses this machinery without any network.
2. Show only the screen of the Link you are controlling.
3. An **input router** decides what each virtual GBA receives each frame:
   * the controlled Link gets your controller;
   * the other Link gets a *follow controller*: inputs computed from both
     Links' positions read from RAM (walk toward the leader, stop when close).
4. **Swap (L/R):** exchange who gets the real controller and switch the visible
   screen. **Whistle:** teleport/recall behavior, implemented by driving the
   follower or, if needed, a narrow game-owned hook.
5. Puzzle assistance (two-Link block pushes, Nokkun/Hikkun): detect the
   situation from game state and have the follower mirror the push.

Steps 1–4 only need the RAM addresses of the player objects, which is ordinary
reverse-engineering work. Step 5 is the hardest and comes last. Medals and
save behavior stay as the GBA game defines them.

## 4. Online multiplayer, 2 to 4 players 🔧

GBARecomp's netplay already supports **2–4 seats**, with delay-sync or
rollback (delay-sync is the default for 3–4 players), LAN or Internet via a
UDP relay, and session checkpoints. `src/main.cpp` sets `max_players = 4`.

**Why this is not "a link cable over the Internet":** sending link-cable
transfers through the network would make every serial exchange wait for the
ping, and the game would stutter or desync. Instead, **every player's PC runs
all the virtual GBAs** of the match, connected by an emulated cable *locally*.
Only controller inputs cross the network. Every PC computes the exact same
result, so the game's own multiplayer code works unmodified and latency is
handled by input delay or rollback.

| Step | Status |
|---|---|
| Netplay compiled in, 4 seats allowed | ✅ (code) |
| Verify Four Swords' serial protocol on `GbaLinkHub` (2 consoles, local) | 📋 |
| 3 and 4 consoles, local | 📋 |
| Online 2 players, then 3–4 | 📋 |
| Every virtual GBA boots directly into Four Swords (section 2) | 📋 |

Each player needs their own verified ROM and BIOS, just as each player
needed their own cartridge in 2002.

## 5. Anniversary Edition content 🔬

The Anniversary Edition added the **Realm of Memories** (areas based on *The
Legend of Zelda*, *A Link to the Past* and *Link's Awakening*) and the
**Hero's Trial** (harder versions of the stages).

**Honest status: this may not be possible.** The Anniversary Edition is a
DSiWare program for the Nintendo DS's ARM9/ARM7 CPUs, not GBA code, so
GBARecomp cannot recompile it and none of its code can be reused. What *might*
be possible:

1. Research: study, from the player's own DSi dump, whether the new stages are
   stored as data (room layouts, tilesets, object placement) in a format close
   to the GBA game's.
2. If they are: a converter that runs **on the player's machine at launch**,
   reads their DSi dump and builds the extra stages as a mod (`.gbamod`) for the
   GBA engine. Nothing from the DSi version would ever be distributed.
3. Hero's Trial is mostly harder versions of existing stages; parts of it may be
   rebuildable as data tweaks even without the DSi dump.

If the research shows that the formats are unrelated, this goal is dropped and
documented as such.

## 6. Launcher 🔧

The launcher is GBARecomp's shared **recomp-ui** launcher (Dear ImGui), already
wired in `CMakeLists.txt`:

* asks for the **USA ROM** and the **GBA BIOS**, checks SHA-1 and remembers them;
* settings (video, audio, controls) and the **Netplay** tab (host/join, 2–4 players);
* 📋 a "Single player (Anniversary style)" / "Multiplayer" mode choice;
* 🔬 an optional **DSi Anniversary Edition** field, only if section 5 proves feasible.

## 7. Milestones

1. **M1 — Boots:** native build reaches the cartridge's game-select screen.
2. **M2 — Four Swords direct boot** and a stable single-console Four Swords lobby.
3. **M3 — Local 2-console** session through the emulated cable (proves the protocol).
4. **M4 — Anniversary-style single player** (follow, swap, whistle).
5. **M5 — Online 2 players**, then **3–4**.
6. **M6 — Research on Anniversary content** (go / no-go).
