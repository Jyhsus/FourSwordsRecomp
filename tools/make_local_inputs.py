#!/usr/bin/env python3
"""Deterministic button scripts for 2-4 linked GBAs (docs/PLAYTEST.md).

Output is the FSR_LOCAL_INPUTS / FourSwordsLinkProbe --inputs format:
"frame buttons-P1 buttons-P2 ...", pressed = 1, GBA bit order
(A=1 B=2 Select=4 Start=8 Right=16 Left=32 Up=64 Down=128 R=256 L=512),
each line held until the next.

Every script starts like tests/m3_two_gba.inputs: nothing until frame 1500,
then A and Start in turn on every GBA until frame 2280 (skip the intro, link
in the lobby, pick the only stage a new file has, start it). The game is in
the tutorial from about frame 2300. Then:

  identity  one player at a time walks in their own direction, everyone else
            waits: P1 right, P2 left, P3 up, P4 down, 90 frames each, from
            frame 2700, 300 frames apart. Afterwards each Link stands apart
            from the group in its own direction, so a screenshot shows which
            input moved which Link and which view follows it.
  soak      every player gets its own pseudo-random walk (seeded by the
            player number): a direction or none for 20-90 frames, now and
            then A, B or R taps, sometimes A held for a spin attack. Start
            and Select are never pressed, so no player pauses the game.

usage: make_local_inputs.py identity|soak PLAYERS [FRAMES] > file.inputs
"""

import random
import sys

A, B, SELECT, START, RIGHT, LEFT, UP, DOWN, R, L = (1 << i for i in range(10))
DIRECTIONS = [0, RIGHT, LEFT, UP, DOWN, RIGHT | UP, RIGHT | DOWN, LEFT | UP, LEFT | DOWN]
START_OF_PLAY = 2300


def boot(players):
    """Skip the intro, link and start the stage: A and Start in turn."""
    rows = [(0, [0] * players)]
    for frame in range(1500, 2280, 30):
        key = A if (frame - 1500) // 30 % 2 == 0 else START
        rows.append((frame, [key] * players))
        rows.append((frame + 6, [0] * players))
    return rows


def identity(players):
    rows = boot(players)
    walk = [RIGHT, LEFT, UP, DOWN]
    for p in range(players):
        start = 2700 + 300 * p
        keys = [0] * players
        keys[p] = walk[p]
        rows.append((start, keys))
        rows.append((start + 90, [0] * players))
    return rows


def soak(players, frames):
    rows = boot(players)
    # One event list per player, merged into rows that list every player.
    events = []
    for p in range(players):
        rng = random.Random(1000 + p)
        frame = START_OF_PLAY + rng.randrange(0, 30)
        while frame < frames:
            direction = rng.choice(DIRECTIONS)
            length = rng.randrange(20, 91)
            events.append((frame, p, direction))
            roll = rng.random()
            if roll < 0.25:          # a tap of A, B or R while walking
                tap = frame + rng.randrange(2, length - 8)
                button = rng.choice([A, A, B, R])
                events.append((tap, p, direction | button))
                events.append((tap + 4, p, direction))
            elif roll < 0.30:        # hold A: charge and release a spin attack
                events.append((frame, p, A))
                events.append((frame + min(length, 80), p, 0))
            frame += length
    events.sort()
    keys = [0] * players
    for frame, p, value in events:
        keys[p] = value
        if rows[-1][0] == frame:
            rows[-1] = (frame, list(keys))
        else:
            rows.append((frame, list(keys)))
    rows.append((frames, [0] * players))
    return rows


def main():
    if len(sys.argv) < 3 or sys.argv[1] not in ("identity", "soak"):
        sys.exit(__doc__)
    kind, players = sys.argv[1], int(sys.argv[2])
    if not 2 <= players <= 4:
        sys.exit("PLAYERS must be 2, 3 or 4")
    frames = int(sys.argv[3]) if len(sys.argv) > 3 else 24000
    rows = identity(players) if kind == "identity" else soak(players, frames)
    print(f"# tools/make_local_inputs.py {kind} {players}" + (f" {frames}" if kind == "soak" else ""))
    for frame, keys in rows:
        print(frame, *keys)


if __name__ == "__main__":
    main()
