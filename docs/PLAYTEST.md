# Playtest: local split-screen multiplayer

*Versión en español: [PLAYTEST.es.md](PLAYTEST.es.md)*

A guide for testing FourSwordsRecomp with 2 to 4 people on one PC. What has
already been checked without people, and what has not, is in
[DESIGN.md](DESIGN.md), sections 10 and 11.

## 1. Before you start

### Which executable

- `build\FourSwordsRecomp.exe`, built from the tag `playtest-rc1` on the
  `widescreen` branch.
- Always start it with **`tools\play.cmd`** (double click): the launcher
  opens. The executable needs the MSYS2 MinGW64 DLLs (SDL2 and the C++
  library), which are not on the Windows PATH. Double-clicking the `.exe`
  itself fails, and other copies of those DLLs (Git's, for example) are not
  compatible.
- To check the version, in a `cmd` console in the project folder:
  `tools\play.cmd --version`. It must show the tag's commit and must
  **not** say `(with local changes)`. Write down what it shows.

### Protect your saves

1. Close the game.
2. Copy `saves\four_swords_usa.sav` somewhere else, for example
   `saves\four_swords_usa.before-playtest.sav`.
3. If you like, create a new file on the file screen just for the test ("New
   file"). There are 3 files; each file is played by one person at most and
   everyone else joins as a guest (Guest), which is never saved.

If something goes wrong with saving, put the copy back with the game closed.

### Settings

- Launcher: **Screen = raw** (the console's exact colours; the other choices
  simulate GBA screens and change the colours on purpose).
- Fullscreen: either option works; local multiplayer always uses the desktop
  resolution. You can also play in a window and resize it.
- Play → file screen → **Multiplayer → Local**:
  - **Players**: 2, 3 or 4.
  - For each player, a file or **Guest**, a **controller** and, for one of
    them only, **Keyboard**.
  - **Whole-number scale**: off (the picture fills more of the screen) or on
    (every pixel the same size, with larger borders). Pick one and keep it
    for the whole test.
  - **16:9 view (experimental)**: off for the first part.

## 2. Assign and identify each controller

1. Connect every controller **before** opening the Local page.
2. On the Local page, have each person **hold a button** on their
   controller: **pressed** appears next to that controller's row. That shows
   which entry of the list is the controller in each person's hands.
3. Give each player their controller. Picking another player's controller
   swaps the two, so one controller never ends up with two players.
4. A connected controller given to nobody does nothing during the game, not
   even when another player's controller is unplugged.
5. Write down which person is P1, P2, P3 and P4, with which controller and
   which file.

What must hold for the whole game:

| Player | Lobby badge | Link's colour | View with 2 | View with 3 | View with 4 |
|---|---|---|---|---|---|
| P1 | P1 | green | left | top left | top left |
| P2 | P2 | red | right | top right | top right |
| P3 | P3 | blue | — | bottom, centred | bottom left |
| P4 | P4 | purple | — | — | bottom right |

On the link screen ("PLEASE WAIT", with the P1–P4 badges), each view shows
**YOU** over its own player's badge. Inside a stage, each view follows its
own Link.

## 3. Test sequence

Everything in **3:2** first, then the same comparison in **16:9**. Write
down the time each test starts.

### A. Identity and individual movement (3:2)
1. In the lobby, check that each view shows **YOU** over the right badge
   (table above).
2. In the tutorial, move one player at a time while everyone else leaves
   their controller alone: only the Link of that player's colour moves, and
   that player's view follows it.
3. Repeat with the keyboard if someone uses it.

### B. Tutorial
Play the tutorial ("Chambers of Insight") up to its second floor. Moving to
the second floor is where an exit happened that is now fixed: if it happens
again, stop (section 5).

### C. Transitions
Go from one area to another several times and let one player fall and come
back. The screen fades to black and back; no view may stay frozen.

### D. Unplug and plug back
1. Unplug one player's controller. The game pauses and the title bar says
   "Player N: reconnect the controller".
2. Plug it back. The game goes on and the same controller moves the same
   player again.
3. With two controllers of the same model, unplug both and plug them back in
   the opposite order. Check who controls whom and write it down.
4. The pause key (**Shift+P** by default, `config.ini` [KeyMap]) resumes
   without waiting for the controller.

### E. Finish a stage
Finish a whole stage, boss included. Write down which stage and how long it
took.

### F. Quit and open again (saving)
1. To go back to the launcher: on the link screen (P1–P4), any player
   presses **B twice**. Closing the window also ends the session and saves.
2. Open the game again. On the file screen, check that each file that was
   played kept its progress and that the other files are unchanged.

This is the most important save test: in the automated tests the game never
got as far as writing its save, so this has never been seen.

### G. The same in 16:9
Do this with **2 or 3 players**: with 4, 16:9 does not keep full speed on the
development PC (the game runs at about 93 % and stutters); with 4, stay in
3:2. Turn on **16:9 view (experimental)** and repeat A, B and C (and E if
there is time) with the same number of players. Compare with 3:2:
- each view shows more of the room at the sides, without stretching;
- the HUD (hearts, rupees and item) sits at the edges, and moves a little
  inward while a text box is open (expected);
- while loading, and on screens that are not a stage (lobby, menus, stage
  select, the fairy fountain), the picture is 3:2 with black bars (also
  expected).

If something breaks in 16:9, go back to 3:2 and carry on there.

## 4. What to write down when something goes wrong

- Time, number of players and which person was which player.
- Devices: each controller's model and who had the keyboard.
- Picture mode: 3:2 or 16:9, scale, window or fullscreen.
- Scene: stage, floor, area and what was happening on screen.
- What you did just before: buttons, unplugging, pausing.
- Evidence: a photo or screenshot and the session's folder (section 6).

## 5. When to stop a test

Stop, without opening the game again yet, if:
- a player loses control or moves another Link;
- the game freezes, closes or shows an error;
- a file disappears, loses progress, or another file changes;
- a view stays black or frozen for more than a few seconds.

Collect the evidence first (section 6). If there is a save problem, play no
more with that file and keep the copy from the start.

## 6. Collecting the evidence

Every local session keeps a folder of its own that is never overwritten:

```
build\sessions\YYYY-MM-DD_HH-MM-SS\
    session.log      version, commit, players, controllers, scene every 10 s,
                     performance, saves and, if there is one, the error
    inputs.inputs    every player's buttons at every moment
    start.sav        the save as it was when the session started
    config.ini       the launcher's settings
    keybinds.ini     the keys
```

`build\fsr_local_last.log`, `.inputs` and `.sav` are also copies of the
latest session (the next one overwrites them).

After an unexpected exit:
1. Do not start another session yet.
2. Copy the newest folder in `build\sessions\` (or zip it).
3. If an error box appeared, take a screenshot.
4. Add your notes (section 4).

With `inputs.inputs` and a copy of `start.sav` the session can be replayed
exactly without people (developer instructions in DESIGN.md §10).

## 7. Known limitations

- Only P1's GBA is heard.
- Link's shadow blinking every other frame is how the game draws it (on the
  GBA's slow screen it looked see-through); it is not a fault. If it bothers
  you on a 144 Hz monitor, note it; a 60 or 120 Hz desktop mode may look
  steadier (untested).
- The first time each part of the game runs, its code is read from disk;
  the game now reads it all in the background at startup, which takes a few
  seconds on a hard disk. Wait on the launcher or the intro for a moment.
- Speed measured on the development PC: 2 and 3 players at full speed in 3:2
  and in 16:9; 4 players at full speed in 3:2, with a frame shown late now
  and then (a small stutter may be noticeable); 4 players in 16:9, no. Each
  session's log records the time per frame every 10 s.
- The washed-out colours and flickering sprites noticed before have not been
  reproduced or compared with a reference; if you see them, write down the
  scene and the Screen setting.
- An exit some testers had with four players has not been reproduced and is
  not fixed. If it happens, the evidence in section 6 is what is missing to
  find it.
- The 16:9 view has only been checked with automated inputs; no boss fight
  has been seen in 16:9.
