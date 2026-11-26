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

**Solved in M3 (section 9):** the Thumb code in IWRAM at
`0x03006D18`–`0x03006E96` is the m4a sound driver's `SoundMainRAM`, now a
`[[code_copy]]`; single player is fully static.

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
| Verify Four Swords' serial protocol on `GbaLinkHub` (2 consoles, local) | ✅ (section 9) |
| 3 and 4 consoles, local | ✅ (section 10) |
| Online 2 players, then 3–4 | 📋 |
| Every virtual GBA boots directly into Four Swords (section 2) | ✅ (sections 9–10) |

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
* the file screen's **Multiplayer** button: Local (split screen, section 10) or
  Online (the Netplay tab) ✅; 📋 "Single player (Anniversary style)";
* 🔬 an optional **DSi Anniversary Edition** field, only if section 5 proves feasible.

## 7. Milestones

1. **M1 — Boots:** native build reaches the cartridge's game-select screen.
2. **M2 — Four Swords direct boot** and a stable single-console Four Swords lobby.
3. **M3 — Local 2-console** session through the emulated cable (proves the protocol). ✅ in one process (section 9); netplay path pending. Local split-screen play for 2–4 players: section 10.
4. **M4 — Anniversary-style single player** (follow, swap, whistle).
5. **M5 — Online 2 players**, then **3–4**.
6. **M6 — Research on Anniversary content** (go / no-go).

Between M2 and M3 come the items in section 8: saves in 3 slots, no BIOS for
the player, and the Ship of Harkinian-style interface.

## 8. Work plan after M2 (priority order)

These three items come before M3, in this order.

### 8.1 Saves in 3 slots ✅ (upstream patch pending)

**Problem:** when the player leaves Four Swords, the game goes back to mode 6
("CHOOSE A GAME"), and the direct-boot hook (section 2, now on the mode-6
handler `0x0803074C`) sends it straight back into Four Swords. There is no way
out.

**Decided model:** one `.sav` file. The cartridge's 3 original file slots are
the launcher's 3 saves. No extra save format, and a `.sav` stays usable in any
emulator.

Reverse engineering needed:

1. Layout of the slots in the EEPROM (64 Kbit, 8 KiB): offset and size of each
   slot, the file name, the "slot in use" flag and the checksum (algorithm and
   range covered).
2. The routine that loads a slot in file select (mode 8), so the launcher's
   choice can be applied without showing "CHOOSE A FILE".
3. The mode Four Swords asks for when it exits, so "Quit to menu" can return to
   the launcher instead of looping.
4. The file name: maximum length and allowed characters (the game's own
   character table).

Result:

* The launcher shows the 3 slots with their names, and can create, rename and
  delete them. The launcher name **is** the in-game file name.
* Picking a slot boots straight into Four Swords, with no "CHOOSE A FILE".
* "Quit to menu" goes back to the launcher.

**Findings (23 Oct 2026):**

*EEPROM layout.* The `.sav` stores each 8-byte EEPROM block byte-reversed; the
offsets below are in game order. A descriptor table at `0x0814C654` (8
entries, 12 bytes: size, status header A, header B, data A, data B) drives
every access. Each copy is written at its offset and again at `+0x1000`;
copy B is used only when writing A fails.

| Entry | Size | Header A / B | Data A / B | Content |
|---|---|---|---|---|
| 0–2 | `0x480` | `0x40`, `0x50`, `0x60` | `0x100`, `0x600`, `0xB00` | A Link to the Past part of files 1–3 |
| 3 | `0x20` | — | `0x000` | cartridge signature string |
| 4–6 | `0x40` | `0x70/0x80`, `0x90/0xA0`, `0xB0/0xC0` | `0x580/0x5C0`, `0xA80/0xAC0`, `0xF80/0xFC0` | Four Swords file headers |
| 7 | `0x10` | `0x20/0x30` | `0x0E0/0x0F0` | system block, loaded to `0x03000000` |

A status header is `u16 sum, u16 -sum, u32 signature`: `S4FT` = valid, `INIT`
= empty, `DelF` = erased (both with `0xFFFF 0xFFFF`). The checksum
(`0x080679B0`) adds every `u16` XOR the number of bytes left, over the
signature and then the data. File header: `+0x04` = `0x11`, `+0x08` name (6
bytes, zero-padded), `+0x24`/`+0x25` = 1 on a new file, `+0x3F` = 3. A slot is
in use when it loads and its name is not empty (`0x080111F8`). Creating a file
resets the A Link to the Past part to `INIT`; erasing writes `DelF` to both
parts and leaves the data. The two games share the 3 files, so erasing one
erases its A Link to the Past progress too, as on the cartridge. Code:
`0x08067940`–`0x080681D0` (EEPROM), `0x080111F8`–`0x08011476` (slots; RAM
copy of the headers at `0x0200B250 + i*0x40`).

*Name.* At most 6 characters, Windows-1252: `A–Z a–z 0–9`, space,
`' - : ! ? , .`, `À Â Ä Æ Ç È É Ê Ë Ì Î Ï Ð Ñ Ò Ô Ö Œ Ù Û Ü` and their
lower-case forms (mapped by typing every cell of "ENTER A NAME").

*System block (`0x03000000`, entry 7).* `S4FT`, `[4]` = current file,
`[5]`–`[8]` settings, `[9]` name length and `[10..15]` the name Four Swords
uses. After a soft reset with `GoGameS` at `[9]`, `AgbMain` (`0x08068108`)
skips file select and enters mode 6 directly (`0x0803072C`). Mode 6
sub-state 0 (`0x080308F8`) reloads file `[4]` (`0x08011418`) and copies its
name into the block (`0x080681D0`).

*Implemented.* `src/fs_save.{h,cpp}` reads and edits the `.sav` exactly like
the game (tested by `tests/fs_save_test.cpp` against saves the game wrote;
`Image::blank()` matches a first-boot save byte for byte, taking the fixed
strings from the player's ROM). `--slot 1-3` boots a file directly: a hook on
the screen reset `0x0800BED8`, filtered to AgbMain's call, sets `[4]` and the
same mode bytes as `0x0803072C`. The mode-6 hook now lets sub-state 0 run
before starting Four Swords; until now Four Swords started with the default
name "LINK" when file select was skipped. Checked: `--slot 2` reaches the Four
Swords intro at frame 300 (file select took ~900) with the right name in the
system block, and the lobby at frame 20000 with call-stack depth 0, on both
the `--slot` and the file-select paths.

*Exit.* Four Swords leaves through sub-state 7 (`0x08030018`) of the table at
`0x084278C0` (used at `0x0802FB20`; sub-state 6 returns to mode 9): it waits
for the fade, resets the screen and calls `set_game_mode(6)` (`0x0803003E`).
Mode 9 sub-state 2 (`0x0802FAEC`, table `0x08427830`) dispatches those lobby
steps by `0x03000BFC`. With one GBA no key leaves the lobby, but writing 7 to
`0x03000BFC` there reproduces the quit.

A hook on `0x08030018` acts once the fade flag `0x03000520` is clear. With
`--slot` it calls `gbarecomp::request_quit()`: the run loop stops at the frame
boundary and `run_game()` exits normally, flushing the `.sav` (it is written
when writes settle and on shutdown, so leaving the process from a hook could
lose a save). Without `--slot` it disables the direct-boot hook, so the stock
"CHOOSE A GAME" appears instead of a loop. `request_quit()` is a local
GBARecomp patch (`patches/gbarecomp/0001-runtime-request-quit.patch`, written
as the upstream PR; see `patches/README.md`). Checked from a lobby savestate
with step 7 forced: `--slot` ends with exit code 0 in that frame; without it
the game shows "CHOOSE A GAME".

*Launcher screen (23 Oct 2026).* After the recomp-ui launcher's **Play**,
`src/fs_slots_ui.cpp` opens a file screen: the 3 files with their names, and
**New file**, **Rename**, **Erase** (with a warning that A Link to the Past
progress in that file goes too) and **Play**; **Back** or Esc returns to the
launcher. It is the game's own small SDL2 + OpenGL + Dear ImGui window,
compiled into `game_launcher_ui` against recomp-ui's ImGui and painted with the
launcher's GBA theme and fonts, so recomp-ui needs no change. Names are typed
or picked from a grid of the 112 characters of "ENTER A NAME"
(`fsr::save::name_characters()`); characters off the grid never reach the
field and the field stops at 6. Every edit goes through `fs_save` and is
stored at once (atomic rename). With no `.sav` yet, the first file starts
from `Image::blank()` built from the ROM the launcher verified. The `.sav` is
the one `run_game()` will use: `--save-path`, else game.toml's
`saves/four_swords_usa.sav` next to the `.toml`, else the ROM name with
`.sav`.

`main()` loops: launcher → file screen → `run_game()` with that file (same
path as `--slot`). When the session ends because the player quit Four Swords
(`fsr::quit_to_menu()`), the loop opens the launcher again (forced with
`--launcher`, so it also appears with "Skip launcher on boot" set). Closing a
window still quits the program. `run_game()` supports being called again in
the same process (it resets its per-run hooks on entry). The file screen and
the loop are skipped in the same cases as the launcher (headless and scripted
runs, explicit `--rom`, `--no-launcher`, `GBARECOMP_NO_LAUNCHER=1`) and for
netplay sessions; an explicit `--slot` keeps ending the program on quit.

Checked on Windows with the windowed build and a fresh scratch `.sav`: create
(typed letters, a grid "É", an off-grid key ignored, the 7th character
refused), rename, erase, play file 2 (Four Swords intro with no "CHOOSE A
FILE"; `0x03000004` = 1 and the name in the system block), quit from the lobby
(savestate with step 7 forced) back to the launcher, a second full round in the
same process, and closing the launcher ends the process. `fs_save_test`
passes; headless `--slot` with the quit still exits with code 0.

**Still to do:** sending the `request_quit()` patch upstream; checking the
real quit with two GBAs (M3).

### 8.2 No BIOS needed by the player 📋

Today the player must supply the GBA BIOS. GBARecomp already has opt-in HLE for
the math, copy and decompression SWIs (`src/runtime/bios_hle.cpp`) and a boot
skip (`bios_hle_boot_skip`), but `IntrWait` (`0x04`), `VBlankIntrWait`
(`0x05`) and the IRQ vector (`0x18`) still run in the recompiled BIOS, so it
must still be linked.

Plan:

1. HLE of `IntrWait` (`0x04`) and `VBlankIntrWait` (`0x05`): halt until the
   requested interrupt flags appear in `0x03007FF8`, then clear them, as the
   real BIOS does.
2. HLE of the interrupt vector: save registers, call the handler pointer at
   `0x03007FFC`, return, without BIOS code.
3. HLE boot (the existing boot skip) and starting with **no BIOS file at all**.
4. Two builds, selected at build time (the GBARecomp HLE policy): a
   **development** build with the real BIOS, used as the reference, and a
   **public** build with HLE only.
5. Validate frame by frame against the real BIOS (savestate and framebuffer
   comparisons over the boot and the Four Swords lobby).
6. Package it as a contribution to GBARecomp: these are engine services, not
   game-specific code.

### 8.3 Ship of Harkinian-style interface 📋

* An own start screen: **Play / Multiplayer / Options / Quit**.
* An in-game menu bar opened with **F1**.
* The room list stays; Discord sign-in stays optional.
* Original art only (no Nintendo assets, see section 0).

## 9. M3 — Two GBAs on one cable ✅ (local, one process)

**Result (23 Oct 2026):** two virtual GBAs on one `GbaLinkHub` cable, in one
process and with no network, boot from cold, link in the Four Swords lobby
and enter the same Four Swords game together (mode 2). Each GBA's buttons move
its own Link, and both screens show the same scene. The run is deterministic
(same final state hash on every run) and fully static: linked sessions have
no interpreter fallback, so every entry it reaches is native.

**Tool.** `tools/link_probe.cpp` builds `FourSwordsLinkProbe` (not built by
default: `ninja -C build FourSwordsLinkProbe`). It runs 2–4 machines on one
cable with the game's generated code and hooks, one `.sav` per machine
(read only), scripted buttons per machine, BMP captures, a trace of the mode
bytes and SIO registers, and a final whole-session state to resume from:

```sh
FourSwordsLinkProbe --rom roms/zelda_lttp_four_swords_usa.gba --bios gbarecomp/bios/gba_bios.bin \
  --save0 a.sav --save1 b.sav --slot 2 --frames 2800 --inputs tests/m3_two_gba.inputs \
  --trace 100 --shots 1700,2450,2600,2760 --out m3
```

With `tests/m3_two_gba.inputs` (A and Start in turn from frame 1500, then
GBA 1 walks right and GBA 2 walks left): Four Swords intro at frame 300,
linked lobby at ~1,700 (each GBA shows the P1 and P2 badges, "YOU" on its
own), mode 5 at ~2,000, both in the game from ~2,100–2,300; the moves at
2,500 and 2,650 appear on both screens. 2,800 frames of two GBAs take ~13 s.

**What it took:**

1. *The IWRAM code at `0x03006D18–0x03006E96` is not link code.* It is the
   m4a sound driver's `SoundMainRAM`, copied by the sound init (CpuSet at
   `0x08134692`, literals at `0x081346D8`: source `0x08133A58`, `0x400`
   bytes). Declared as a `[[code_copy]]` with its Thumb entry, its ARM body
   (`0x03006D24`) and its Thumb tail (`0x03006DA6`); single player is now
   `FULLY_STATIC` (no dispatch misses, no interpreted instructions).
2. *Other RAM code, needed once linked.* EWRAM: `0x08000318 → 0x02030950`
   (`0x40`, ARM, the cable-side IRQ code) and `0x0800C7E8 → 0x02030590`
   (`0x200`, Thumb, the VBlank handler), copied from the pool at
   `0x0800C4D0`. IWRAM: `0x08008FA4 → 0x03000530` (`0x300`),
   `0x080007C4 → 0x03000E40` (`0x100`) and `0x0800023C → 0x03006740`
   (`0xE0`), all ARM, from AgbMain's pool at `0x0800B158`. Each was checked
   byte for byte against RAM.
3. *Functions reached only through pointers in ROM data* (object and state
   descriptors such as `0x08427C28 → 0x0803DBD5`). `tools/scan_code_pointers.ps1`
   seeds every aligned data word (from `0x08138000`) that has the Thumb bit
   and points into the code to a `PUSH {…, LR}` not yet compiled: 1,354
   entries in `symbols/AZLE_data_pointers.toml`, which `tools/regen.*` load.
   Three more over-sized auto jump tables they reach were trimmed with
   `tools/audit_jump_tables.py` (now in `symbols/AZLE_symbols.toml`). The
   corpus grew from ~33,900 to ~47,150 functions.
4. *The cable handshake timing.* Both GBAs stopped at "Linking with other
   systems…" (lobby step 4). Traced on the cable: when the lobby moves from
   polled to interrupt-driven transfers, the parent starts a transfer, and
   ~5,450 cycles later its link init (`0x0800C41C`) resets SIO and
   `0x0800C576` turns on the Timer3 IRQ that drives the next transfers only
   if SIOCNT is no longer busy. The hub's two-GBA time at 115,200 bps is
   5,755 cycles (mGBA's table: 36 bits plus a ~512-cycle timeout), so the
   transfer was still busy, the timer was never enabled and nothing was sent
   again. Measured: up to 5,450 cycles works, 5,500 or more stalls. The game
   now sets 5,243 cycles (the 36 bits alone) through
   `gba::GbaLinkHub::set_multiplayer_transfer_cycles()`, a local GBARecomp
   patch (`patches/gbarecomp/0002-link-hub-transfer-time-override.patch`,
   written as the upstream PR). Emulators share the problem (a libretro mGBA
   fork has a "Four Swords link assist" option).

**Still open:**

* Root cause of the timing gap: real hardware may be faster (GBATEK does not
  give the timeout length), or the recompiled CPU may count too few cycles on
  that path. Until it is known, the override is a workaround for this game.
* The same check with the real netplay path (two processes over UDP
  loopback, then the launcher's rooms) and quitting a linked game (section
  8.1). 3–4 GBAs link with the same value: section 10.

## 10. Local multiplayer, split screen ✅ (2–4 players)

**Result (23 Oct 2026):** the file screen (section 8.1) has a **Multiplayer**
button with two choices:

* **Local:** 2, 3 or 4 players on this computer. Each player picks a file in
  use or **Guest**, and a controller by name; one player can also have the
  keyboard. The game runs that many virtual GBAs in this process on one
  `GbaLinkHub` cable, as `FourSwordsLinkProbe` does (section 9), and shows
  them in one resizable window (layout below). The audio is player 1's; the
  other GBAs' sound is dropped. Quitting Four Swords comes back to the
  launcher; closing the window quits, as in single player.
* **Online:** goes back to the launcher, whose Netplay tab has the rooms
  (section 4).

The same thing without the launcher, for testing:
`FourSwordsRecomp --local 2,guest [--save-path x.sav] game.toml` (2–4
comma-separated players, each `1`–`3` or `guest`), plus
`--local-scale fit|integer` and `--local-keyboard 1-4|none`. `src/fs_local.cpp`
is the runner. It is handed to `run_game()` as a `RunOptions::netplay` launch
with no network, so the runtime still checks the ROM, the BIOS and the window
settings first. Debug aids: `FSR_LOCAL_INPUTS` (a probe input script, added to
the live controls), `GBARECOMP_FORCE_DRAWABLE=WxH` (lay the window out as if it
had that size) and, with `--frames N`, `FSR_LOCAL_DUMP` (a BMP of the last
window frame).

**Who is player N (16 Nov 2026).** One number means one player everywhere:
player N is machine N−1, cable port N−1, view N in the window and entry N−1
of the plan's files and controllers; player 1's GBA is the one heard. The game
takes each GBA's player number from its cable port (SIOCNT bits 4–5; port 0 is
the parent). Checked with `FourSwordsLinkProbe` on four GBAs: the lobby puts
"YOU" over P1 on machine 0, P2 on machine 1 and P3 on machine 2, and in the
game the buttons of machines 0–3 move the green, red, blue and purple Link.

Testers reported the order reversed (two players: P1↔P2; three: P1↔P3; four:
P1↔P4 and P2↔P3). The session is not reversed (above). The first runner gave
out controllers in SDL's device order: `HostWindow` took device 0 for player 1
and device N−1 went to player N. A device order that is the reverse of what
the players expect gives exactly those three reports, and SDL's order depends
on the driver, not on the pads' player lights. That is the likely cause; it is
**not confirmed** on the testers' hardware (the development machine had no
controller connected). The fix does not depend on it:

* `fsr::local_pads()` lists controllers by player light (SDL's player index:
  the XInput slot on Xbox pads), then by connection order. SDL's device order
  is not used.
* The Local page shows each player's controller by name. Holding a button
  marks that controller's row "pressed". Picking a controller another player
  has swaps the two.
* `HostWindow`'s own controller is turned off (`set_gamepad_enabled(false)`,
  local patch 0003), so no device can drive two players.
* A player keeps their device for the session. If it is unplugged the game
  pauses and the title bar names the player. A controller plugged in later
  takes a waiting place: first the one whose controller had the same player
  light, then one whose controller was the same model, then the first waiting
  place. A controller that was connected when the session started but given
  to nobody is never handed to anyone (someone else may be holding it). The
  pause key overrides the wait. `src/fs_pads.cpp`, checked with SDL virtual
  controllers by `tests/fs_pads_test.cpp` (19 Nov 2026: two of these rules
  were added after review; without them the test fails).
* The keyboard belongs to one player (player 1 by default, or nobody), on top
  of that player's controller. The left stick also works as the Control Pad.
* Files and guests are indexed by player; choosing controllers never touches
  them.

**Choices kept between runs (25 Nov 2026).** Pressing **Start** on the Local
page writes the player count, each player's controller, who has the keyboard,
the whole-number scale and the 16:9 view to `fsr_local_settings.ini`, next to
the executable. Changes left without pressing Start are not written, and the
files each player plays are not kept. `src/fs_prefs.cpp`:

* Order: a command-line flag (`--local-scale`, `--local-keyboard`,
  `--local-view`), then the file, then the default. Start writes what the page
  shows, including a value a flag put there.
* `--local` never reads or writes the file, so its scripted runs and replays
  do not depend on it. Runs with `--slot` or a scripted flag (`--frames`,
  `--no-window`, `--rom` and the like) do not either. A netplay session writes
  nothing, but one started without those flags still reads the file at startup,
  with no effect on the session.
* A missing file, a missing key or a bad value gives the default. Two players
  on one controller put all four back; a keyboard player beyond the player
  count goes back to player 1. A write that fails is only reported on stderr
  and the game starts. Deleting the file restores the defaults.
* **Limitation:** a controller is kept as its position in the list
  ("Controller N", 1–16), not as a device. If player lights or connection order
  differ on the next run (more likely without a player light, or with two pads
  of one model), a player gets another controller: check the names and the
  "pressed" marker before Start. A position with nothing connected shows "not
  connected", and that player's place waits.
* Further limits, from reading the code in review (25 Nov 2026, not run):
  * A saved position with nothing there can leave a connected controller
    unused: with player 1 on "Controller 2" and one pad connected, that pad is
    ignored for the session if no playing player has position 1, and player 1
    waits for another one. A first start with the defaults gives it to player
    1. Reassign on the page before Start.
  * Lowering the player count below the keyboard's player, or a
    `--local-keyboard` beyond it, makes the page set the keyboard to nobody
    (as it did before), and Start now keeps that. Reading the file differs: it
    goes back to player 1.
  * Started by bare name through `PATH`, the program takes the current
    directory for the file's, as the launcher does for `config.ini`.
* Checked (25 Nov 2026): `tests/fs_prefs_test.cpp`; `tests/fs_pads_test.cpp`
  (a plan read from the file, SDL virtual controllers); the executable with
  `--local guest,guest --frames 120`: a file with other values was not read
  (the session log showed the defaults) and stayed unchanged, the flags
  applied, and no file appeared when there was none; two 60-frame
  `--no-window` runs without `--local` left the file unchanged and created
  none.
* Manual checks by the maintainer (25 Nov 2026): the windowed launcher flow,
  two guests, at most one controller (Xbox 360).
  * Passed: choices kept after closing the program; changes left without Start
    not kept; the defaults with no saved choice (2 players, keyboard on player
    1, whole-number scale off, 3:2); the controller choice kept; late
    connection; detection and disconnection.
  * `--local-scale fit` over a saved `integer`: the first try was reported as
    failed; the repeat, on the same executable, passed (box unchecked, `fit`
    written after Start). The cause of the first result is unknown.
  * Connection order was reported correct, but with one controller, which
    cannot check the order among several.
* **Not checked:** several controllers (another order on the next run, two of
  one model, a saved one absent), the "pressed" marker (not reported on its
  own), a computer restart, and `--local-keyboard` and `--local-view` in the
  launcher flow. No test covers `src/main.cpp`'s use of the module (file before
  flags, saving on Start), nor that an older file survives a failed write.

**Window layout and scaling (16 Nov 2026).** `src/fs_layout.h`, checked by
`tests/fs_layout_test.cpp`. All views have the same size and the source's
aspect (3:2), and sit together in the middle of the window:

| Views | Places |
|---|---|
| 1 | centred (layout only; there is no single-player mode here) |
| 2 | P1 left, P2 right, centred vertically |
| 3 | P1 top left, P2 top right, P3 bottom centre |
| 4 | P1 and P2 on top, P3 and P4 below |

The canvas is the window's own pixels and each GBA frame is scaled straight
into its place, so nothing rescales the picture afterwards. Nothing is
stretched or cropped; the rest of the window is black. Two scales:

* **Fit** (default): the largest size that fits. Each source pixel becomes a
  block of whole pixels (the scale rounded down) and only the leftover
  fraction is blended at block edges; inside a block no pixels are mixed. A
  whole-number scale is exact pixel repetition, and a fit within two pixels
  of one snaps to it.
* **Whole-number** (`--local-scale integer`, or the checkbox): every pixel the
  same size, with larger bars.

On a 1920×1080 screen: two views are 960×640 each, exactly 4×, in both modes
(bars above and below are unavoidable with two 3:2 views side by side); three
or four views are 809×539 (3.37×) with Fit, or 720×480 (3×) with 240-pixel
side bars in whole-number mode. A 2-pixel gap separates the views unless it
would cost a whole-number scale.

Fullscreen is always the desktop's own mode. Exclusive fullscreen (launcher
`fullscreen = 2`) switched a 1920×1080 monitor to 1024×768 with four views
(drawn at 2.13×) and to 1440×900 with two: the mode nearest the window, which
the monitor then rescales and, at 4:3, distorts (measured with the first
runner, 16 Nov 2026).

**Diagnosis of the reported exit with four players (16 Nov 2026).** Not
reproduced, so **not fixed**:

| Run | Result |
|---|---|
| `FourSwordsLinkProbe`, 2 / 3 / 4 GBAs, 3,100 frames, cold boot to the tutorial, each GBA moving its own Link | no error; hashes `39cc9470`, `e31f15aa`, `010a73b5` |
| First runner, windowed, `--local 1,guest,guest,guest`, 3,100 frames, three times | exit code 0 |
| First runner, exclusive fullscreen (the testers' launcher setting), 4 and 2 players, 600 frames | exit code 0 (mode switch above) |
| New runner, 2 / 3 / 4 players, 3,100 frames | ended normally |

What was found: any error inside the local runner (a missing native entry,
since linked sessions have no interpreter fallback; a cable error; the window
failing to open) ended the program with one line on stderr, which nobody sees
when the game is started from the launcher. Such an error is the most likely
kind of "unexpected exit" in a part of the game no test has reached, but
which one happened is unknown. Now the runner writes, next to the executable
and overwritten by each session:

* `fsr_local_last.log`: version, players, files, controllers, window size,
  controller and save events, and on an error the frame, each GBA's mode
  bytes and the message (also shown in a dialog);
* `fsr_local_last.inputs`: every player's buttons, in `FSR_LOCAL_INPUTS` format;
* `fsr_local_last.sav`: the shared save as the session found it.

To replay a session: the same `--local` list, `--save-path` on a copy of that
`.sav`, and `FSR_LOCAL_INPUTS` on that file.

**First exit caught this way (16 Nov 2026).** A two-player game stopped in
frame 11,684 with `multiplayer native dispatch entry is missing: 08009a90
(Thumb)`. `0x08009A08` is a table of 8 Thumb pointers *inside the code area*
(`0x08009A29`…`0x08009CA1`, reached by the `bx r4` at `0x080099F8`). Its
targets are switch cases, not functions, and `tools/scan_code_pointers.ps1`
only read the data area, from `0x08138000`. The script now also looks inside
the code area: single pointers under the same rule as before (the target is a
`PUSH {…, LR}`), and tables of four or more Thumb pointers in a row with at
least three different targets, none inside the table and none on blank ROM,
which it writes as sized `[[jump_table]]`s. On this ROM that finds 22 tables,
of which this one was the only one not yet compiled, and no new single
pointer; the overlay keeps its 1,354 functions and gains that table (47,033 →
47,077 Thumb entries after regenerating). Replaying the session's inputs on a
copy of its save now runs past that frame, to frame 12,500 (where the
replay was told to stop). This is one missing entry found, not proof that
there are no others; whether it was also the four-player exit is unknown.

**Graphics reports (16 Nov 2026).** One tester saw washed-out colours and
flickering sprites compared with the original. Neither has been reproduced as
reported; what is known:

* *Colours, fact:* `game.toml` set `[video] screen = "frontlit"`, the
  engine's model of the GBA SP's front-lit panel, which lowers saturation and
  lifts black on purpose. It applies when nothing overrides it; the launcher
  passes its own Screen setting (`raw` in the test machine's `config.ini`).
  `raw` is the PPU's colours with the exact 5-to-8-bit expansion. The same
  frame through each model is visibly different (frontlit is the palest).
  The default is now `raw`, so a start without the launcher shows the same
  colours as one with it; this is not shown to be what the tester saw.
* *Colours and shimmer, fact:* exclusive fullscreen ran the picture through
  the monitor's scaler at a non-native mode and drew it at 2.13× with nearest
  sampling, which makes columns of pixels uneven, so anything that moves
  shimmers. Fixed by the layout above.
* *Flicker, fact:* the engine does not wait for the monitor's refresh
  (`GBARECOMP_VSYNC=1` opts in); the test monitor runs at 144 Hz and the game
  at 59.73 Hz. Consecutive raw PPU frames of the tutorial room show only the
  sprites' own animation, no every-other-frame flicker.
* *Not done yet:* a side-by-side with a reference (the same USA ROM in
  another emulator with colour correction and filters off, or hardware) on
  the same scene; finding out which setting the tester had, which sprites
  flickered and where. Until then the cause of that report is open. No
  saturation boost, interpolation or frame blending was added.

**Saves (decided 23 Oct 2026): merge by file.** Otherwise every GBA would own
a full copy of the cartridge's EEPROM, and whichever wrote the `.sav` last
would erase the others' progress. Instead:

1. There is still one `.sav` (section 8.1). Each GBA powers on with a copy of
   it, with the system block's current file (byte 4) set to that player's
   file. `--slot`'s boot hook then leaves byte 4 alone
   (`fsr::kBootSavedFile`), so each GBA boots its own file.
2. A file can be played by one GBA at most; the screen and `--local` refuse
   the same file twice. There are 3 files and up to 4 players, so the rest
   play as **Guest**: a first-boot save (`Image::blank()` from the player's
   ROM) with one file named `Guest2`…`Guest4`, never written to disk.
3. When a GBA's EEPROM has been written and then left alone for 60 frames
   (the game writes a file as many 8-byte blocks over several frames), and
   again on the way out, only that GBA's file is copied into the `.sav`:
   descriptors *i* (its A Link to the Past part) and 4+*i* (its Four Swords
   header), status headers and data, both mirrors
   (`fsr::save::Image::copy_file()`). The signature, the system block and the
   other files stay as they are on disk.

`tests/fs_save_test.cpp` covers `copy_file()` (one file merged, the others
untouched) and `set_current_file()` (byte 4 in both mirrors, checksum valid
through a reload).

**Checked:**

* `--local 2,guest` with `tests/m3_two_gba.inputs`: both GBAs link and
  enter the game together, side by side, at full speed (2,800 frames in
  47.6 s, about 59 fps).
* Four players (`--local 2,guest,guest,guest`) and three
  (`--local guest,2,guest`) with the same inputs on every GBA: everyone enters
  the same game, each screen following its own Link.
* `FourSwordsLinkProbe` gives the same final hash as in section 9.

**Finding: 3–4 GBAs needed the same handshake time.** With the reference
times (8,376 cycles for three GBAs, 10,486 for four) every GBA waits forever on
"Linking with other systems…", exactly as two did in section 9. With 5,243
cycles they link and play. On a real cable four GBAs take longer than two, so
this points at our timing on the parent's path (the recompiled CPU reaching
`0x0800C576` too early) rather than at the cable. `src/fs_boot.cpp` now sets
5,243 for 2, 3 and 4 GBAs. The root cause is still open (section 9).

**Checks for the first playtest build (19 Nov 2026,** `playtest-rc1`**,**
docs/PLAYTEST.md**).** Without people:

* Unit tests: `fs_save`, `fs_layout` and `fs_pads` (controller assignment
  with SDL virtual controllers) pass.
* `FourSwordsLinkProbe` with `tests/inputs/` (from
  `tools/make_local_inputs.py`): identity scripts for 2, 3 and 4 GBAs, and
  24,000-frame soak runs with a different random walk per player. The soak
  runs stay on the tutorial's first floor and cover it from its bottom to
  its top edge; each GBA's camera follows its own Link. Four-GBA soak runs
  of 24,000 frames in each of the four other stages, with floor exits forced
  through `0x03005050`, cover their three floors, a game over and the return
  to stage select. No missing native entry. One stop
  (`02016d90 (ARM)`) came from the test device: the forced exit landed on
  the stage-select screen; without it the same run completes.
* The user's recorded two-player session that stopped in frame 11,684
  (`0x08009A08`, above) runs past that frame on this build.
* Quitting: in the linked lobby, B pressed twice by any player reaches lobby
  step 7 and the quit hook; the session returns to the launcher.
* Saving: no automated run made the game write its EEPROM, not even past a
  boss and the fairy fountain. The merge of real game writes into the `.sav`
  is still unseen; the playtest checks it (PLAYTEST §3 F).
* Performance of the real executable on the development machine:
  6,000 frames of the soak scripts at 1920×1080 (budget 16.74 ms a frame):

  | Players | View | Work per frame | Emulation | Frames over budget | Stalls |
  |---|---|---|---|---|---|
  | 2 | 3:2 | 6.79 ms | 5.08 ms | 11 | 0 |
  | 2 | 16:9 | 8.61 ms | 6.88 ms | 11 | 0 |
  | 3 | 3:2 | 10.06 ms | 8.19 ms | 17 | 0 |
  | 3 | 16:9 | 12.78 ms | 10.85 ms | 25 | 0 |
  | 4 | 3:2 | 13.91 ms | 11.88 ms | 683 | 0 |
  | 4 | 16:9 | 17.89 ms | 15.68 ms | 3,859 | 201 |

  Drawing takes under 1 ms and presenting about 1.3 ms; emulation is the
  rest, one GBA after another on one thread. Four players in 16:9 do not
  keep full speed on this machine (about 93 %); four in 3:2 do, with
  frames now and then shown late. Four players should use 3:2 (the Local
  page warns).

* Each local session now also keeps `sessions/YYYY-MM-DD_HH-MM-SS/` next to
  the executable (`session.log`, `inputs.inputs`, `start.sav`, `config.ini`,
  `keybinds.ini`), never overwritten. The log starts with the build's commit
  and engine patches, writes every GBA's mode, stage state, stage, floor and
  camera every 600 frames, and the work time per frame.

**Smoothness after the first manual test (24 Nov 2026).** The session
(two players, 16:9, 53,125 frames, 33 stalls, peaks up to 368 ms) was
replayed exactly from its `inputs.inputs` and `start.sav`. Each run's log
now lists every frame over 33.5 ms with each part and the page faults it
caused, and the time between presents. Two causes, both shared by 3:2 and
16:9:

* *Frame pacing.* The local runner waited with `sleep_until`, which on
  Windows wakes on the ~15.6 ms system tick: 16.74 ± 4.5 ms between presents,
  only 61 % of frames between 15 and 18.5 ms, the same with
  `GBARECOMP_VSYNC=1`. It now uses the engine's `FramePacer`, as single
  player does: 16.78 ± 0.75 ms, 99.4 % between 15 and 18.5 ms, in 3:2,
  16:9 and with vsync. The game runs at 59.6 fps instead of 59.73 (a late
  frame is never paid back with a short one).
* *Code read from disk on first use.* The project is on a 5,400 rpm hard
  disk and the executable is about 111 MB of code. Run from a copy made with
  unbuffered I/O (so nothing of it is cached), 84 frames took over 33.5 ms,
  up to 694 ms, all in "emulation", each with 160–300 page faults: the
  intro, area changes, first encounters. `main()` now asks Windows to read
  the whole image in the background at startup
  (`PrefetchVirtualMemory`): the same cold run has 1 slow frame (the first),
  worst 42 ms.

**The session log (24 Nov 2026, second manual test).** A three-player
16:9 session over Parsec stalled once every 600 frames: the scene line the
log writes then took 20-140 ms in "logging" (the line was flushed with
`std::endl` on the frame loop's thread, to two files on the 5,400 rpm disk,
while OBS was recording to the same disk). The log and the input record are
now queued and written by a thread of their own. Replaying that session's
first 12,000 frames (3 players, 16:9, same inputs, no Parsec), alternating
old and new executables: "logging" at most 20.6 and 22.7 ms before, 0.0
after; stalls (present gaps of 35 ms or more) 19 and 28 before, 3 and 1
after. The inputs file it writes is byte for byte the session's own.

The same whole session replayed without Parsec or OBS (old executable):
13.66 ms of work per frame on average (emulation 11.63), 596 of 66,616
frames over 16.74 ms and 105 stalls, about one per 600 frames (the log
line). The session
itself, with Parsec and OBS on the same PC: 15.46 ms (emulation 12.73),
17,128 over and 112 stalls. So three GBAs in 16:9 fit in a frame on this PC,
with about 2-3 ms to spare in a stage; the two programs together cost about
1.8 ms per frame and pushed a quarter of the frames late. Which of them
costs what was not measured apart.

Not explained: one 150 ms frame in one warm run, with no part of it
measured as slow; it did not happen again. Link's shadow, reported as
flickering, is the game's own: its sprite (16×8, tile 769) is in OAM only
on every other frame, a usual GBA way to make a shadow look see-through on
a slow LCD. It is kept. On a 144 Hz monitor each GBA frame stays on screen
for 2 or 3 refreshes, unevenly, which may make that 30 Hz blink more
visible than on the GBA; that is presentation, untested on other refresh
rates.

**Still open:**

* No 2,800-frame run writes the EEPROM, so only the unit tests check the
  merge. It still needs a real check: finish a stage in a local game and
  confirm that the file's medals and progress reach the `.sav`.
* The windowed flow (launcher → files → Multiplayer → Local) is built and its
  parts were run, but nobody has clicked through it yet, and the new
  controller choices have not been tried with real controllers (order,
  "pressed" marker, unplugging and plugging back). Update (25 Nov 2026): the
  maintainer clicked through it with two guests and one controller; what passed
  and what is still untried is listed above.
* A real game with four people, and anything past the tutorial with two or
  three.
* Online play (`multiplayer_runner.cpp` upstream) never writes a player's
  `.sav`, only paired checkpoints, so progress made online is lost. The fix is
  the same as here, in the engine (an upstream PR).
* The Local page's choices are kept between runs (above), controllers by
  position only; with real controllers it has been tried with one at most.
* Controller buttons are fixed (SDL's standard layout); only the keyboard
  follows `keybinds.ini`.

## 11. Real widescreen 🔬 (findings, 16 Nov 2026)

Goal: a 16:9 view that shows more of the world, with the 3:2 picture kept as
an option. Not a stretched, cropped or letterboxed picture. **Experimental**:
local multiplayer has it as an option (`--local-view 16:9`, or the checkbox on
the Local page), checked in the tutorial and on the three floors of the four
other stages, boss rooms included; 3:2 stays the default.

**What the engine gives.** The PPU can render extra columns
(`set_view_margins`), games supply what goes in them through providers
(`g_ws_tilemap_provider`, BG and OBJ placement hooks, `g_ws_pillarbox`), and
`GbaNetplayPresentation` is a host-only mirror PPU per machine: the real PPU
stays at 240×160 and the mirror never enters a snapshot or a state hash.
`game.toml`'s `widescreen = 0` turns none of this on by itself.

**Prototype.** `FourSwordsLinkProbe --wide 284` gives every machine a mirror
284 columns wide with no provider and saves both pictures. Results on the
four-GBA tutorial run:

* The session hash is the same with and without the mirrors (`010a73b5`): a
  wider presentation does not reach game logic, collisions or the link.
* In the game BG0–BG3 are all 256×256 text maps (`BGxCNT` = `1F0C`, `1D05`,
  `1C02`, `1E07`). The hardware holds 16 pixels beyond the 240 on screen; 284
  needs 44. The margins show the far edge of the map wrapped around, not the
  room.
* The hearts, rupee counter and item icon keep their 240-wide coordinates, so
  they sit inside the picture instead of at its edges.
* The camera stops at the room's edge as computed for 240 pixels.

**What a real adaptation needs,** in order:

1. The room's tile map in RAM (the hardware maps are only a scrolling window
   onto it) and a tilemap provider that reads it for the margin columns.
   *The Minish Cap* names (`zeldaret/tmc`) are the reference; addresses and
   names only.
2. The HUD moved to the wide picture's edges through the OBJ/BG placement
   hooks, on the mirror only.
3. A rule for rooms narrower than the view and for the camera's edge stops:
   show what exists, bars for the rest.
4. Sprites near the edges. The game decides which objects exist from its own
   240-wide view; objects may pop in inside the margins. Showing them earlier
   means changing game logic, which must not happen: every GBA of a session
   has to compute the same thing.
5. Title, lobby, menus, stage intro and cut-scenes stay 3:2 with bars
   (`g_ws_pillarbox`), and are listed here when known.

**First working version (16 Nov 2026).** `src/fs_wide.cpp` is the game's
view policy; `src/fs_local.cpp` gives each GBA a mirror 284 columns wide when
16:9 is chosen, and `FourSwordsLinkProbe --wide 284 --room 1` does the same
without a window. What it reads, all found by matching the hardware tile maps
against RAM (`--dump 1` writes the memory of every GBA at each `--shots`
frame):

| Address | What |
|---|---|
| `0x03000470`, `0x03000472` (u16) | Camera X and Y: the world pixel at the screen's top-left. BG1 and BG2 scroll by their low 9 bits |
| `0x03000478`, `0x0300047A` (u16) | Stage width and height in pixels (1008×1008 in the tutorial) |
| `0x020031D0` | BG1 (upper layer) tile entries for the whole stage: 128×128 hardware entries, `0x100` bytes per row |
| `0x02016D90` | The same for BG2 (lower layer) |

In the tutorial, with four GBAs, the visible 30×20 tiles of both layers match
those buffers entry for entry (600 of 600) in every capture, and the whole
stage can be drawn from them.

* Step 1 ✅: margin columns of BG1 and BG2 come from those buffers, inside the
  stage's size. BG0 (the HUD and text) and BG3 draw nothing in the margins.
* Step 3 ✅ (horizontal): where the camera stops at the stage's edge, the 240
  native columns slide toward that side of the picture instead of leaving an
  empty margin. The slide follows the BG2 scroll register, which is what the
  frame is drawn with; the camera variable runs one frame ahead of it while
  the camera moves. Checked at a stage's left edge: the native picture sits at
  column 0, at column 1 with the camera at X = 1, and at column 22 in the
  open, identical pixel for pixel to the 240-wide frame.
* Vertical edges: the view is no taller than the original, so the camera's
  own stop at the top and bottom already keeps every row inside the stage,
  and the margin columns use the same rows. Nothing was changed for them.
  This follows from the code; the recorded session never reaches a top or
  bottom edge (camera Y stays between 84 and 748 of 0–848).
* Step 5 ✅ as far as the recorded session goes. Outside game mode 2 (title,
  lobby, file and menu screens) the margins are black. Inside mode 2 the byte
  at `0x03005050` is the stage's own state: 8 entering an area, 9 play, 10
  leaving an area, 1–7 loading the next one, 11 a fallen player
  (`FourSwordsLinkProbe --map-log F` writes it per frame with the camera and
  how many on-screen tiles differ from the buffers):
  * Play: the screen always shows what the buffers hold. At most 44 of a
    layer's ~600 tiles differ, for one frame, when the game changes part of
    the map.
  * Leaving an area: the fade is a blend, and the margins fade with it.
  * Loading (91 frames): the screen is black and matches nothing. The margins
    are drawn only while no more than 150 tiles of either layer differ, so
    here, and on any screen that uses BG1 and BG2 for something else, the
    picture is 3:2 with black bars.
  * Entering an area: the game opens the picture with a window that grows.
    A margin row appears once the window covers that whole row of the native
    picture, so the margins open a little after the centre does. They never
    show the room ahead of the effect.
  * A fallen player, text boxes: as in play; the HUD keeps its 240-wide
    placement while a text box is open (step 2).
* Step 2 ✅: the HUD is BG0, unscrolled: the counter at the top left, the
  hearts at the top centre, the item frame at the top right and the rupees at
  the bottom right, plus one priority-0 sprite, the item icon. The left group
  is drawn at the picture's left edge, the right group at its right edge and
  the hearts stay in the middle; a priority-0 sprite whose centre is on a HUD
  tile moves with that tile. This happens only while BG0 holds the HUD and
  nothing else (no window enabled, BG0 unscrolled, tiles only in the HUD's
  places, no group across a cut). Otherwise, for example while a text box is
  open, the HUD is drawn where the 240-wide screen puts it, so the box is
  never cut; the HUD therefore steps 22 pixels inward while a box is open.
  Seen on both floors of the tutorial, with and without text boxes.
* Step 4 ✅ for the 284-column view: the game does not cut sprites off at the
  screen's edge. Over a recorded two-player session (12,500 frames, 620,000
  world sprites; `FourSwordsLinkProbe --oam-log F` writes the camera and every
  sprite per frame) it draws every object whose left edge is between about
  −80 and 303; no sprite has a 9-bit X between 304 and 415. The view shows
  columns −22 to 261, so everything it can show is already in OAM. One thing
  was wrong: OAM X has 9 bits, so an object at 256–303 is read by hardware as
  −256…−209 and was missing from the last 6 columns of the right margin (up
  to 28 when the picture slides at a stage's edge) until it came below 256.
  The view now reads X values 256–359 as positive. Checked by drawing the
  same recorded frames with and without the rule: the only pixels that change
  are those sprites, in the last columns, and the session hash is the same.
  Left open: a sprite 64 pixels wide can still appear late on the left when
  the picture has slid fully (it would need X below −80), and whether the
  game creates or removes objects by distance inside this range is not known.
* The session hash of the four-GBA probe run is the same in 3:2 and in 16:9
  (`010a73b5`).

**The other stages (16 Nov 2026).** The recorded session only covers the
tutorial, "Chambers of Insight", and its second floor (an earlier note here
called that floor "the stage after the tutorial"; it is not). A new file
cannot pick anything else, so the other stages were reached by writing to RAM
from the probe. How a stage is chosen:

| Address | What |
|---|---|
| mode 5 | "CHOOSE A STAGE", between the lobby and the stage. With a new file the cursor only has Chambers of Insight |
| `0x03006C81` (u8) | The stage chosen on that screen, 0–3 |
| `0x03006C82` (u8) | Non-zero: the tutorial is played whatever was chosen. `0x08012C5E` stores 4 in the stage byte when this is set, else the chosen stage |
| `0x03006C84` (u8) | Floor, from 0 |
| `0x03006C87` (u8) | The stage being played: 0 a cave with water, 1 lava, 2 a palace in the sky, 3 forest, 4 Chambers of Insight (named here by what they look like) |
| `0x03000420` block | Rebuilt for each floor by `0x0802C89C`: `+9` the floor's tile set, `+0xB` the floor number shown on the HUD |

`FourSwordsLinkProbe --poke F:ADDR:BYTE,...` writes a byte to every GBA's RAM
before frame F. Holding `0x03006C82` at 0 and `0x03006C81` at 0–3 from frame
1,945 to 2,110 (the map screen and the confirmation) loads each of the four
stages on two GBAs. This is a test device only: it skips the game's own rule
that the tutorial comes first, and nothing in the game does it.

In each of the four, with both players walked around for 2,650 frames:

* the tiles on screen match the buffers on every frame, on both GBAs (no
  frame with a single differing tile), so every stage keeps its map in the
  same two buffers; all four are 1008×1008;
* the native picture inside the wide one is identical to the 240-wide frame;
* the stage edges that the recording never reached are on screen: the bottom
  edge (camera Y = 848, cave), the left edge (camera X = 0, palace: native
  picture at column 0) and the right edge (camera X = 768, forest: native
  picture at column 44, no empty column);
* the HUD is at the picture's edges.

**Later floors and bosses (16 Nov 2026).** Writing 10 (leaving an area) to the
stage's state byte `0x03005050` during play makes the game leave the floor
and load the next one by its own path, as it does when the players reach the
exit: the floor byte goes up and the next floor is generated and loaded. Done
at frames 2,600, 3,500 and 4,400 in each of the four stages (again a test
device: the floor is not played). Each stage has three floors, the third with
its boss; `FourSwordsLinkProbe --map-log` now also writes the stage and the
floor.

* On floors 2 and 3 of every stage, in play, the on-screen tiles match the
  buffers on every frame, and the native picture inside the wide one is
  identical to the 240-wide frame at every capture.
* Boss rooms: the forest boss and the palace's boss room, with its text box
  and with the HUD hidden during the scene, draw their real room in the
  margins. No boss was fought: the captures are of the first seconds in the
  room.
* A top edge is on screen on the second floor of the cave and of the forest
  (camera Y = 24, X = 0: native picture at column 0).
* Loading a floor: the screen is black and the margins with it, as between
  the tutorial's floors.
* After the forest's boss floor the game enters mode 4, a fairy fountain. Its
  room is exactly one screen wide (camera X = 32, nothing but a plain fill
  beyond), so there is nothing real to show at the sides: it stays 3:2 with
  black bars, like every mode other than 2. Trying it wide gave two flat
  green bands with the trees cut at the old edge, so it was left as it was.
* Forcing the exit a third time, on the boss floor, only moved on in the
  forest; in the other three the game stays in the leaving state.

Not checked: a boss fight itself and whatever a boss does to the picture
(shakes, flashes, large or rotating sprites), a stage finished by playing it,
the start of a stage as the game shows it without the forced choice, a HUD
with more hearts or a longer rupee count than a new file has, play with
people, and speed on a slower machine.

The window layout takes the view size: four 284×160 views fill a 1920×1080
screen (957×539 each).

**Speed (19 Nov 2026).** In 16:9 each GBA was drawn twice: by its own PPU at
240 columns and by the mirror at 284. The GBA's own PPU now keeps its timing
and registers but stops drawing (`set_rasterization_enabled(false)`, as in
online play); the memory of four GBAs is byte-identical with and without it.
What remains is the engine's wide compositor: four GBAs take 12.0 ms a frame
with a 240-column mirror and 15.2 ms with a 284-column one (probe, no window;
the game's providers add 0.6 ms). Four players in 16:9 therefore run at
about 93 % speed on the development machine, and the Local page warns;
two and three players keep full speed (table in section 10).

**After the first manual test (24 Nov 2026).** A 53,125-frame two-player
session in 16:9, replayed exactly from its `inputs.inputs` (all 88 scene
checkpoints match), showed two defects of the wide view, both fixed:

* *The HUD jumped.* The rule above was all or nothing: any BG0 tile outside
  the HUD's places put the whole HUD back at its 240-wide place. That
  happened in 38 % of play frames: every "+20" rupee popup (two seconds,
  just above the counter), every message ("You lose 50 Rupees!", "A Rupee
  Wraith appeared!") and the whole boss floor, whose rupee counter
  ("x2 2132") starts one column further left. Now every BG0 cell is placed
  on its own, every frame: in the top four rows a group starting at the left
  goes to the left edge, the hearts stay in the middle, the group at the
  right goes to the right edge; in the bottom five rows the group reaching
  the right side (counter, popups) goes to the right edge; everything else
  (messages, menus) stays with the 240 native columns, lined up with the
  world and its sprites. A group is a run of used cells with at most one
  empty cell inside.
* *Black blocks beside the menu.* The pause menu (held L) is drawn inside
  WIN1 (x 80–160, y 40–120), WINOUT showing the world. The engine draws a
  margin row black when the window is not uniform along it. While windows
  are on and WINOUT shows BG1 and BG2, the margins are now treated as
  outside every window (`g_ws_authored_margin_layers`), so WINOUT decides
  there, as beside the menu on the GBA. The area-entry effect has WINOUT = 0
  and keeps black margins.

Checked from saved states of the session: the menu opened at the stage's
right edge, at its left edge and while walking (the menu does not pause the
game), messages, the boss floor and the area-entry effect. The four-GBA
session hash is unchanged (`2cc03dd9`). Not seen: the "P1 is down!" message
(not in the recording).

The same session also showed stutter in both views. That was not the wide
view: frame pacing and code read from disk on first use (section 10).

**Bands (24 Nov 2026).** Screens shown 3:2 inside the 16:9 view (title,
lobby, stage select, fairy fountain, loads) had black bands, which read as
part of the picture and made the change of shape abrupt. The window now
fills those bands with a fixed dark grey (RGB 34, 34, 38; `kWideBandColour`
in `src/fs_wide.h`), so each view keeps its 16:9 shape. Only the bands are
filled: `fsr::wide_bands()` is true when the wide view itself decided the
frame is not a stage; black that comes from the game (the area-entry
effect's WINOUT = 0) stays black. Checked in the executable at the lobby,
stage select, area entry and play.

**The rupee counter beside a hint (24 Nov 2026, second manual test).** The
three-player session of 24 Nov (66,617 frames) was replayed in
`FourSwordsLinkProbe` and every frame's HUD placement logged (`--map-log`
with `--wide` now ends with `fsr::wide_hud_groups()`: each run of BG0 cells
and where it went, taken from the map the frame was drawn with). Only one
group ever changed place during play: the rupee counter. The one-line hints
of the forest stage ("Pull them both to reveal the ...", a three-row box at
columns 0-21) end one cell before the counter (columns 23-28), the one-cell
gap joined them, and the counter went to the native columns with the box,
22 columns inside the picture, for about 5 s at each of 12 hints (3,789
frames per player). A run that is not a HUD group as a whole is now split at
a gap when the piece at its end is one on its own; the box stays with the
picture and the counter at the edge. After the fix the whole session has no
HUD group changing place while it stays on screen, except two frames on
player 2 where a large box opening and closing touches the "+20" popup
without a gap (13,700 and 58,614). Evidence image:
`build/diag/playtest-fixes/hud-hint-before-after.png` (3:2, 16:9 before,
16:9 after, frame 52,410).

**The area-entry iris in 16:9 (24 Nov 2026).** The triangle iris (WIN0
rewritten every scanline, WINOUT = 0, frames 2,040-2,123 of that session) is
drawn exactly as in 3:2 inside the native 240 columns. In the margins, the
engine continues a scanline only when all of its 240 pixels have the same
window control (`gba_ppu.cpp`, the rule for providers without authored
margins), so while the triangle is wider than the screen the margins open
row by row in rectangles instead of following its edges, for about the last
30 frames. Alternatives, not chosen yet: keep the margins black for the whole
iris; extrapolate each scanline's window edges into the margins (an engine
change); or keep the current rows.
