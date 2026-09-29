# Level transition (from the original exe)

Research notes from a study of the original game's code, 3D path. Coordinates are in the board's 1024×768 units; times are in updates (100 a second). The GameCube version is `BJ2GC/Source/warp_gc.cpp` and `fx_gc.cpp` (BigText).

## Level bar (`FUN_005ab4d7` @0x5abb35–0x5abc38)

- **Target:** `T = clamp(((disp − pen − start)·708) / (end − start), 0, 708)`.
  - `disp` is the rolling score `[0x15e40]`.
  - `pen` is 0 in Classic.
  - `start` is `[0x15ee8]` and `end` is `[0x15eec]`.
- **Animation:** it runs twice per update, except in state 0xd:
  - if `bar < T`, `bar += (T−bar)/120 + 1`;
  - if `bar > T`, `bar −= (bar−T)/120 + 1`.
  - Emptying from 708 to 0 takes 146 updates.
- **Drawing** (`FUN_0059df5c`): white and additive at (279, 704), 58 high, `Scale(bar + 20)` wide. It uses IMAGE_BAR_LEFT/MID/RIGHT (`barleft/mid/right.gif`).
- **No pulse** in Classic.
- **Rolling score:** `disp += (int)(((real−disp)/k/25 + 1)·k)`, where `k = [0x15e28]`.

## Timeline

1. **Level reached:** state 1→8 when `disp − start ≥ end − start`. Input stops.
2. **8→9:** plays `Level_Complete.ogg` once the praise-voice cooldown `[0x162c]` is 0. In state 9 it waits for `real ≤ disp`, then calls `FUN_0059d37a`.
3. **State 10, whirlpool** (init `FUN_0059d37a`, update `FUN_005aafae`, draw `FUN_005a1787`). t counts from entry.
   - **Backdrop mesh:** 48×48 vertices.
     - Each vertex: `x = col·1024/47`, `y = row·768/47`, `u = x/1024`, `v = y/768`, `ang = atan2(y−384, x−512)+2π`, `r = |(dx, dy)|`.
     - Each update: `k += 0.001` (starts at 1). Inner vertices get `x = 512+cos(ang)·r·k` and `y = 384+sin(ang)·r·k`.
     - From t=90: `s += 0.01`, `r = max(0, r − d·0.35·s)` and `ang += d·0.001·s`, where `d = min(i, 47−i, j, 47−j)`.
   - **Black hole:** `bh += 0.02`, capped at 1.
     - `holemask.png` (128²) at the centre, normal blend, alpha `bh`.
     - `blackhole_chopped.jpg` (five 256² frames), additive, rotated about (512, 384).
     - Frame `f −= 0.1`, wrapping by +5. Frames `⌊f⌋` and `⌊f⌋+1` are crossfaded.
     - Angle `−= w`, with `w += 0.0005` each update.
   - **Collapse** (from t=80):
     - `tt += 0.05` (capped at 1) and `z += (1−cos(tt·π/2))·0.01` (capped at 1).
     - `sc = 1−z`, `e = sin(zπ/2)`.
     - Each piece is scaled by `sc` about its (W/2, W/2), with its centre moved to `(pos + W/2 − 512)·sc + 512`.

     | Piece | Image (width) | x | y | Rotation |
     |---|---|---|---|---|
     | Board | FRAME.gif (750) | `264+⌊1000e⌋` | 3 | `−5z` |
     | Score pod | SCOREPOD (238) | `⌊22−300e⌋` | `42−⌊400e⌋` | `+6z` |
     | Gadget | GADJET3balls (245) | `⌊27−300e⌋` | `⌊339+600e⌋` | `+4z` |

4. **Hyperspace** (created at t=100: `FUN_0059d20d`/`FUN_005c72d1`; update `FUN_005c7957`; draw `FUN_005c620e`).
   - **Countdown:** 120 updates. For the last 19, a white screen fades in with alpha `(20−n)/20`.
   - **At the end (t≈220):** the white is 1 and then falls by 0.015 per update. The warp becomes active and the backdrop switches to the next one.
   - **Tunnel:** 24 rings of 24 vertices, drawn far to near and additive. With `q = 1−p`:
     - `Z = 800 − 5000·r·q/24`, `S = 1024/(1024−Z)`, `R = (int)(200 − 150·r·q/24)`;
     - `x = ((cx − camX)·q + cos φ·R)·S + 512`, `φ = 2πj/24 + 4A`;
     - colour `min(255, 384−15r)`;
     - `u = j/24`, `v = 0.02r − 0.48`.
   - **Tunnel textures:** before activation, `nr_hyperspace_initial`. After it, `nr_hyperspace` (v +0.006 per update) and `nr_warplines` (v +0.004 per update).
   - **Wander** (only while active):
     - The far ring's centre does `cx += 4 sin a` (`a += 0.0077`) and `cy += 4 sin b` (`b += 0.013`).
     - `A += 0.002 sin a0 + 0.001 sin a1`, with `a0 += 0.006` and `a1 += 0.0093`.
     - The ring history moves one ring toward the viewer every 4 updates.
     - The camera follows ring 0 at a rate of 0.09.
   - **Behind the tunnel** (while active):
     - The next backdrop, coloured `min(255, 300p)`, sized `(0.95p³+0.2)·(1024, 768)`, centred at `ring23·(1−p) + centre·p`.
     - `tunnelend.png` `2.5·R23·S` wide, with black around it.
     - `firering.jpg` additive, brightness `min(255, 400p)`, size `5·R23·S`, frame `(n/2)%10`.
   - **Progress:** 200 updates after activation, `p += (p/50 + 0.001)·min(1, (1.001−p)·5)`. It reaches 1 after about 193 updates. Ten updates later comes `FUN_005aac20`, at t≈623.
5. **New level** (`FUN_005aac20`): the level goes up by 1 with new thresholds, and the board is refilled stacked above. Plays `Get_ready.ogg`, then state 0xd.
6. **Fly-in** (state 0xd, 100 updates, draw `FUN_005a238c`): `t += 0.01`.

   | Piece | x | Rotation |
   |---|---|---|
   | Board | `264 + 1024(1−t)²` | `−0.4(1−t)` |
   | Pod | `22 − 600(1−t)` | `+0.5(1−t)` |
   | Gadget | `27 − 400(1−t)` | `+0.6(1−t)` |

   The bar stays frozen.
7. **"LEVEL n"** (`FUN_005b524c`, duration 80), in QuincyCaps74gold2.
   - **Buffer:** `min(textW+16, 800)` × 110, with the text at (8, 94).
   - **Drawn** centred at (654, 245): width `(W + 4W·wob)·0.73·s`, height `max(6, (110−198·wob)·0.73·s)`.
   - **Alpha:** +0.02 per update for 170 updates, then −0.03 per update.
   - **Scale** `s`: starts at 1 with velocity 0.025. It holds for 30 updates, then springs toward 1.2 for 60 updates, then toward 1.75 until its counter runs out (k 0.0015), then toward 0 (k 0.002). There is no damping.
   - **Wobble:** `kk += 0.04` (capped at 1), `wv = (0.975 − 0.12kk)·(wv − wob·(0.01kk + 0.002))`, `wob += wv`. `wob` starts at 1.
   - After that: state 0, a 25-update delay, then the fall (gemongem2 at most every 10 updates). The bar empties over 146 updates.

## Assets

- **Images:**
  - HYPERSPACE → `nr_hyperspace.jpg`
  - HYPERSPACE_INITIAL → `nr_hyperspace_initial.jpg`
  - WARP_LINES → `nr_warplines.jpg`
  - TUNNEL_END → `tunnelend.png` + `tunnelend_.png`
  - FIRE_RING → `firering.jpg`
  - BLACK_HOLE → `blackhole_chopped.jpg`
  - BLACK_HOLE_COVER → `holemask.png`
- **Font:** FONT_HUGE → QuincyCaps74gold2
- **Sounds:** `Level_Complete.ogg`, `Get_ready.ogg`, `gemongem2.ogg`
- **Music:** unchanged. MUSICOFFSET_LEVEL_CLEAR and GET_READY are never used.

## Uncertain

- The sign conventions of RotateRad and DrawImageRotated.
- Whether new gems are clipped during the fly-in.
- The gem-shine intensity at `[0x15fb0]`.
- ±1 update of ordering.
