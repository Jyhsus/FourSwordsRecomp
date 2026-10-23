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
  use or **Guest**. The game runs that many virtual GBAs in this process on
  one `GbaLinkHub` cable, as `FourSwordsLinkProbe` does (section 9), and shows
  them in one window: two side by side (480×160), three or four in a 2×2 grid
  (480×320; with three, the fourth cell stays black). Player 1 plays with the
  keyboard and the first controller, player N with controller N (SDL order,
  same button mapping as single player). The audio is player 1's; the other
  GBAs' sound is dropped. Quitting Four Swords comes back to the launcher;
  closing the window quits, as in single player.
* **Online:** goes back to the launcher, whose Netplay tab has the rooms
  (section 4).

The same thing without the launcher, for testing:
`FourSwordsRecomp --local 2,guest [--save-path x.sav] game.toml` (2–4
comma-separated players, each `1`–`3` or `guest`). `src/fs_local.cpp` is the
runner. It is handed to `run_game()` as a `RunOptions::netplay` launch with no
network, so the runtime still checks the ROM, the BIOS and the window
settings first. Debug aids: `FSR_LOCAL_INPUTS` (a probe input script, added to
the live controls) and, with `--frames N`, `FSR_LOCAL_DUMP` (a BMP of the last
split-screen frame).

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

**Still open:**

* No 2,800-frame run writes the EEPROM, so only the unit tests check the
  merge. It still needs a real check: finish a stage in a local game and
  confirm that the file's medals and progress reach the `.sav`.
* The windowed flow (launcher → files → Multiplayer → Local) is built and its
  parts were run, but nobody has clicked through it yet.
* Online play (`multiplayer_runner.cpp` upstream) never writes a player's
  `.sav`, only paired checkpoints, so progress made online is lost. The fix is
  the same as here, in the engine (an upstream PR).
* Controllers are assigned in SDL order; there is no way to reorder them yet.
