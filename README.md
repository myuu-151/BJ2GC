![Bejeweled 2 on the GameCube](docs/images/banner.jpg)

# BJ2GC

Bejeweled 2 Deluxe's Classic mode on the GameCube, built with
[Octave-libogc](https://github.com/myuu-151/Octave-libogc).

The game is written anew to play as the original does. The board, its random
numbers, the scoring, the timings, the effects and the level transition all
follow the original, and the fill and random numbers match it exactly. None of
the game's art, fonts, sounds or music is in this repository. You make them
yourself from your own Steam copy of the game, and they go onto the disc you
build.

## What's in it

- **Classic mode:** swaps, cascades, power gems and their explosions,
  hypercubes and their lightning, levels, and "no more moves".
- **The original's look at 640x480:** the backdrop for each level, the gems,
  the frame, the score pod, the glowing level bar, and the game's own bitmap
  fonts.
- **The original's effects:**
  - shards, sparkles and screen shake;
  - springy score popups in each gem's colour;
  - two turning stars on each power gem;
  - the hint arrow;
  - EXCELLENT and INCREDIBLE;
  - the board collapsing when there are no more moves.
- **The level transition:** the backdrop swirls into a black hole and the board
  collapses into it, then the hyperspace tunnel, then the next level flies in
  under "LEVEL n".
- **The original's sound effects and voices**, played where the original plays
  them.
- **The Classic music:** *Beyond the Network*, streamed from the disc and looped.

## What you need

- **Bejeweled 2 Deluxe, installed through Steam.** The data is made only from a
  Steam install.
- **[devkitPro](https://devkitpro.org/)** with devkitPPC and libogc, and the
  `DEVKITPRO` and `DEVKITPPC` environment variables set.
- **[Octave-libogc](https://github.com/myuu-151/Octave-libogc)**, cloned next to
  this repository, with its GameCube engine library built
  (`Engine/Build/GCN/libEngine.a`) and `Octave.exe`:

  ```
  Documents/
    BJ2GC/            this repository
    octave-libogc/    Octave-libogc
  ```

  Both the build and the data tool look for it there. Give the build another
  path with `OCTAVE=<path>`.
- **Python 3** with [Pillow](https://python-pillow.org/).

## Building

**1. Make the data** from your copy of the game:

```
python tools/make_data.py
```

- **Finding the game:** it looks the game up through Steam's own records (Steam
  app 3300) and checks that some of its files are the Steam copy's. If either
  fails, it stops and says why.
- **What it writes:** everything goes into `BJ2GC/Scripts/Data/`, which git
  ignores:
  - the textures, already in GameCube formats;
  - five of the game's bitmap fonts;
  - 25 sounds as 16-bit PCM;
  - the Classic music as Ogg Vorbis (about 19 MB).
- **Conversion:** it converts sound and music with the ffmpeg that comes with
  Octave-libogc (`octave-libogc/External/ffmpeg`), which can decode the game's
  tracker music.

Run it again whenever the tool changes.

**2. Build the disc** from the `octave-libogc` folder:

```
Octave.exe -headless -project <path to this repo>/BJ2GC/BJ2GC.octp -build GameCube
```

This compiles the game with `BJ2GC/Makefile_GCN` and packs it with the data.
The result is `BJ2GC/Packaged/GameCube/BJ2GC.iso`, about 38 MB. It plays in
Dolphin, and on a GameCube through Swiss.

**Disc banner:** the banner is `BJ2GC/opening.bnr`. To change it, edit
`art/banner.png` (96 × 32) and run `python tools/make_banner.py`.

## Controls

| Button | Action |
|---|---|
| D-pad or stick | Move the cursor |
| A | Pick up a gem, then press a direction to swap it |
| B | Put the gem down |
| Y or X | Show a hint |
| A or Start | New game, after "No more moves" |

## Test builds

Set these in the environment when you build the disc. Each test build logs its
progress (score, level, matches, swaps undone) to the console.

| Setting | What it does |
|---|---|
| `EXTRA=-DBJ2_DEBUGKEYS=1` | Adds Z (complete the level), L (out of moves) and R (turn the gem under the cursor into a power gem, then a hypercube, then back) |
| `AUTOPLAY=1` | Plays by itself, making the hint's move, and starts a new game when one ends |
| `EXTRA=-DBJ2_PADTEST=1` | Drives the pad like a player: the cursor to the hint's gem, A, then the direction |
| `EXTRA=-DBJ2_WARPTEST=1` | Completes each level 3 seconds after the board settles, to show the level transition |
| `EXTRA=-DBJ2_FXTEST=1` | Every 2 seconds, turns the hint's gem into a power gem or a hypercube (by turns) and makes the move |

## The PC checker

`bj2check` runs the same game code on a PC, without graphics. Build it with
CMake from this folder.

| Command | What it does |
|---|---|
| `bj2check board SEED` | Prints the board a seed starts with |
| `bj2check fill-check SNAPSHOT.txt` | Checks the fill against a board saved from the original |
| `bj2check play SEED` | Plays a whole game by the hint |

## Layout

| Path | What it is |
|---|---|
| `src/game/` | The game: board, fill, matches, scoring, swaps, cascades, hypercubes, levels. Platform-free. |
| `BJ2GC/Source/` | The GameCube side: drawing (GX), sound, music, effects, the level transition |
| `BJ2GC/` | The Octave project: `BJ2GC.octp`, `Makefile_GCN`, the disc banner |
| `tools/make_data.py` | Makes the data from your Steam copy |
| `tools/check/` | `bj2check` |
| `docs/` | How the original's effects and level transition work |
