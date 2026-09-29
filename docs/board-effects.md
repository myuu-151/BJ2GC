# Board effects (from the original exe)

Research notes from a study of the original game's code: coordinates in the board's 1024x768 units, times in updates (100 a second). Not yet checked against the running game.

Every effect the original draws on the board during Classic play. Not yet checked against the running game; the level bar is in `level-transition.md`. Coordinates are in 1024×768 space, times are in updates (100 per second), and "cell" means the gem's top-left corner (X, Y). `MT` is the board's MTRand and `rand` is the CRT `rand()`. `SetDrawMode(1)` = `0x46a340` (additive); `SetColorize` = `0x46a360`.

Gem colours: 0 yellow, 1 white, 2 blue, 3 red, 4 purple, 5 orange, 6 green. Gem sheets (`gem0-6.gif`) are 1680×84: 20 frames of 84 px.

**1. Matched gem death** (`FUN_005aab8a`, frames built in `FUN_005dc96d`, draw at `0x59edb4`)
- `5a7ec1` sets gem+0x9c = 1 and frame +0xa0 = 0.
- Every 3rd tick, frame++. After frame 5 (18 updates) the gem is freed.
- Frame f is the gem's frame 0 scaled into a centred square of side `trunc((1 − (f+1)/7)·84) | 1`: 73, 61, 49, 37, 25, 13 px. It is drawn opaque, normal blend.
- There are no particles, flash or light for a normal match. Your "shrink" is therefore close to the original: match these 6 exact steps.

**2. Board draw order** (`FUN_005a2d15`)
Everything is shifted by the shake offset. Order: backdrop/frame → gems (sorted by y) → gem lighting → selectors → power glow → particles (`598525`) → explosion sprites → score popups → lightning.

**3. Power gem explosion** (`5a63a3` then `5a68b0`)
- Destroys the 3×3 block. Power gems caught in it get +0x78 = 10 and explode 10 updates later (chains). A hypercube caught in it gives 120 rainbow sparkles.
- **Explosion sprites:**
  - `explosion.jpg` (1600×80, 20 frames of 80), additive, drawn at x−40, y−40.
  - 80 slots per centre, thinned by the density table at `0x62a490[min(nCentres,15)]` (79 spawn for one centre).
  - Slot j: radius j px, angle j·0.503 + (MT%100)/800, start delay j/10.
  - The frame advances on even ticks; frame 0 is never drawn; deleted at frame 20.
- **Shards** (type 0):
  - `gemshard.gif` + `_` (1200×30, 40 frames of 30), colorized with the gem colour, normal blend.
  - 15 per destroyed gem, thinned by the table at `0x62a4e8`.
  - Radius trunc(2.6j), angle j·0.906 + (MT%100)/400.
  - Velocity is outward·4 plus jitter ((rand%100)/100 − 0.5, − 1.0 for y)·3.1.
  - Frame MT%40; frame period MT%4+1 ticks.
- **Sparkles** (type 2):
  - `sparkle.gif` (560×40, 14 frames of 40), additive, colorized white, drawn at x−20, y−20.
  - 18 per centre: radius trunc(0.2j)+5, velocity 0.351·r outward.
  - Frame period MT%4+3; the particle dies when the frame reaches 14.
- **Particle update** (`5a7712`), each tick:
  - Sparkles: vx·=0.98 and vy = vy·0.98 + 0.07·g.
  - Other types: vy += 0.15·g.
  - Then x += vx, y += vy. g = 1.
  - Frame = (frame+1)%40 when tick % period == 0.
  - A particle is removed once it falls below the board.
- **Screen shake:**
  - n = gems destroyed. Time s = min(0.2n+1.2, 2); amplitude a = min(1.2n+1.25, 6).
  - Every 3rd tick: s −= 0.1 (floor 0), and each offset axis = trunc(((rand%2000 − 1000)/1000)·a·s).
- **Light flash:** {45, 20, I}, where I rises 0.1 per tick and falls 0.05. Two open points:
  - It seems to be applied after the gem pass (`0x59e6cf`), so it never shows.
  - Its rising flag is never initialised.
  - I'd skip it.
- **Score popups:** one per destroyed gem, value (10·n + 40)·mult.

**4. Hypercube** (`5a85bc`, `5a888f`, `5a8c66`)
- **Creation:** a 5-match turns the centroid gem into colour 9 (old colour kept at +0x64). There is no creation flash.
- **Idle:** `hypergem.jpg` 3360×84 (40 frames), frame (tick/6)%40.
- **When used:**
  - The swap counter goes +4 per tick instead of +5, to 180.
  - The cube waits 35 ticks, then t += 0.0175. Alpha = min(1, 2(1−t)) in 3D (1−t in 2D), and it shrinks by 1−0.25t. It is deleted at t > 1.
  - The partner gem is electrified.
- **Power glow:**
  - `powerglow.jpg` 2400×240 (10 frames), additive, at partner x−78, y−78.
  - Starts 15 ticks after the swap; t += 0.012 per tick (84 ticks).
  - Frame int(30t)%10; alpha 255·min(1, 4(1−|2t−1|)).
- **Zaps:**
  - Each electrified gem: +0x30 += 0.015; above 1.0 (67 ticks) it is destroyed by `5a6e5f`.
  - A new bolt fires if none exists, or when MT % (20/(nElectrified+1)+5) == 0.
  - It goes from a random freshly electrified gem to the last unelectrified gem of the target colour in row-major order.
- **Bolt** (`5a7d39`, draw `5989c7`):
  - 8 points from centre to centre (+42). Bow = perpendicular·ln(dx²+dy²)·0.4.
  - t += 0.012 per tick. Every 4th tick the points are re-jittered: centre = lerp + (bow·fade + 24·r)·w, with w = 1−|1−2f|, fade = max(0, 1−3(1−t)) and r = (MT%1000 − 500)/500. Edge half-width jitter is 18 (3D) or 12 (2D).
  - Drawn as an additive strip: `lightning.png` in the colour·k, then `lightning_center.png` in white·k, with k = min(1, 8(1−t)).
  - Bolt colours: (255,255,64), (200,200,200), (64,128,255), (255,100,100), (255,64,255), (255,128,64), (64,255,64).
- **Zapped gem burst** (`5a6e5f`):
  - 10 explosion sprites (radius j, angle j·0.503 + (MT%100)/800, no delay).
  - 18 shards: radius trunc(2.6j), angle j·0.906 + (MT%100)/400, positioned −15; velocity outward·2.5 + ((rand%100)/100 − 0.5 for x, − 1.0 for y)·1.9.
  - Plus a light flash.
- **Electrified gem light:** |sin(15t)|·0.6, scale 15, offset 10.

**5. Power gem idle** (`596747`)
- No flash on creation (a 4-match sets +0x70).
- Two `bigstar.gif` (120×120) at x−20, y−20, additive and colorized, over the gem:
  - Angle A += 0.015 per tick, alpha 255·|2p−1|.
  - Angle B −= 0.004 per tick, alpha 255·(1−|2p−1|).
  - p += 0.006 per tick, wrapping at 1.
- In 3D it also lights its neighbours (scale 20, offset 10, I = |2p−1|). The `gem_add.jpg` glow is the one drawn over it.

**6. Score popups** (`Sexy::Points`, ctor `5cf1eb`, draw `5cf05a`, spawn `5a6236`)
- One per match run, in every cascade step. Value = trunc(runScore·mult). Position = (average gem x + 42, average gem y + 34).
- Font `ContinuumBold60outline` (FONT_DIGIT), "%d", pre-rendered in the colour.
- Colours: (255,255,64), (255,255,255), (64,128,255), (255,153,153), (255,64,255), (255,181,145), (64,255,64).
- s = min(value/mult, 80). Life = min(2s+70, 180).
- Scale is a spring from 0 toward 0.5 + 0.006s: v = (v + (target − scale)·0.018)·min(0.86 + 0.0015s, 0.962); scale += v.
- y −= 1 every 3rd tick. Alpha = min(1, life/18)·255, normal blend.

**7. Praise text** (`5b524c` on the EffectOverlay, update `5b7cda`, draw near `5b5e2e`)
- The move's running score (Board+0x15e94) sets a level:
  - ≥ 60·mult: Good, sound only.
  - ≥ 125·mult: "EXCELLENT".
  - ≥ 275·mult: "INCREDIBLE".
- It fires only when the level goes above the last one announced; cooldowns are 100, 120 and 150.
- Font `QuincyCaps74gold2`, centred at (654, 245).
- 30-tick delay, then size targets 1.2 (60 ticks) → 1.75 (140) → 0, via sv += (target − s)·0.0015 (0.002 once the target is 0).
- Alpha +0.02 per tick for 170 ticks, then −0.03 per tick. There is a separate width/height stretch spring; the formulas are in `fx\praise.txt`.
- The same function shows "NO MOVES!", "GO!", "LEVEL %d" and "TIME UP!".

**8. Swap and bad swap** (`5a888f`)
- pos = mid + (pos − mid)·cos(π·c/180), c += 5 per tick: 36 ticks, truncated to int.
- A bad swap negates the offsets and replays the same ease back (72 ticks in total). No shake.

**9. Falling** (`5a9ced`, refill `5a71f0`)
- y += vy; snap to the row; vy += 0.24. No bounce, no squash.
- Gems above a hole start with vy = 1.
- New gems start 102 px above the top gem in their column (at most −84), with vy = (vy of the gem below) − 0.55.
- SOUND_GEM_HIT at most once every 10 ticks.

**10. Gem spin, selector and hint**
- **Spin:** every 3rd tick, if frame ≠ 0 or the gem is selected: frame = (frame+1)%20. The hint sets frame = 1, giving one 60-tick spin. There are no idle spins.
- **Selector:** `selector.gif` 84×84, static, normal blend. Alpha 255, or 255·(1 − c/180) on valid-swap gems.
- **Hint trigger:** 1500 idle ticks or the Hint button. It sets gem+0xa8 = 290 (counting down) and +0xac, which increments every 5 ticks.
- **Hint sparkles:** additive, alpha min(1, 0.1·a8)·255:
  - frame ac%14 at (X+13, Y−7) while a8 ≥ 80
  - frame (ac−8)%14 at (X−10, Y+3) while ac > 7 and a8 ≥ 40
  - frame (ac−16)%14 at (X+5, Y+19) while ac > 15
- **Hint arrow**, while a8 ≥ 90 (the first 200 ticks). Let t = a8 − 90 and e = 200 − t:
  - A = min(1, 3.5(1 − |t/100 − 1|)).
  - b = (1 − cos(0.125e))/2, a period of about 50 ticks.
  - `hint_arrow.gif` (100×80) at (X−8, Y−49 + trunc(10b)), normal blend, alpha A.
  - Arrow glow from `help_indicator_arrows_` (source rect 0,0,40,40) at (X+21, arrow y + 20), additive, alpha b·A.

**11. Gem lighting** (3D only, `596931`, drawn at `0x59fe54`)
- Nine levels per gem, reset every frame: 8 facet directions (up, up-left, left, … up-right) plus the centre.
- Light source (sx, sy, scale, offset, I): d = (sx − gx − 42)/scale, e = (sy − gy − 42)/scale, q = max(1, d² + e² − offset). If q < 100, each facet gets += max(0, (dir·(d,e))/q·I).
- Draw `al_litgems.gif` (756×756) cell (i·84, colour·84) for each level > 0.01, additive, grey min(255, 255·L).
- Skipped for spinning, swapping, dying and hypercube gems.
- **Hover:** intensity +0.045 per tick, −0.012 per tick always. Phase +0.0625 per tick, wrapping at 10, walking the facet table [0,4,8,2,6,3,7,8,1,5]. Neighbours get 0.3 of it on the opposite facet.
- **Idle diagonal sweep:** when idle, a MT%6000 == 0 chance per tick. t += 0.02 per tick (50 ticks). v = 1 − 9·|t − (row+col)/16|; if v > 0, facet 3 += 0.8v, facet 7 += 0.6v, centre += 0.6v.

**12. No-moves collapse** (`595ea2`, trigger `5a5030(200)`)
- 200 ticks of shaking: every 6 ticks each gem jumps to its cell ± (MT%9 − 4) px.
- Then SOUND_EXPLODE and the gems fly off:
  - 3D: vy = MT%6 − 4, vx = MT%7 − 3, vz = MT%12, spin = (MT%2000 − 1000)·0.00011.
  - 2D: vy = MT%12 − 8, vx = MT%14 − 6.
- Each tick: x += vx, y += vy, vy += 0.1 (+0.025 more in 2D), z += vz, vz += 0.15.
- Drawn scaled by 1024/(1024 − z) about (512, 384), rotated by −spin.

**Uncertain**
- The hypercube swap may pin a zero-length bolt between the two gems, which would give NaN points (probably invisible).
- The "nearest target" test compares against 0x7fffffff and always passes (a bug in the original), so targets follow scan order.
- Left out: the Action/Puzzle-only effects: rocks, bombs, floating gems (particle types 3 and 5), and the Puzzle help indicator.
