#include "Bj2App.h"

#include <ogc/lwp_watchdog.h>
#include <ogc/pad.h>
#include <ogc/system.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <malloc.h>
#include <unistd.h>

#include "System/System.h"
#include "audio_gc.h"
#include "music_gc.h"

// Octave's log: /octiso.log on the SD card, Dolphin's log window without one.
// (Not SYS_Report: on the console it talks to the IPL's EXI device, which
// Octave found can hang.)
void OctLog(const char* format, ...);

namespace {

constexpr float kUpdateTime = 1.0f / bj2::Game::kUpdatesPerSecond;
constexpr int kRepeatDelay = 30;    // updates before a held direction repeats
constexpr int kRepeatEvery = 12;
constexpr int kHintTime = 290;      // updates the hint shows (gem+0xa8)
constexpr float kCell = 84.0f * Bj2App::kScale;

// The board frame (sm_frame, 469x477) where the original has it at 640x480
// (measured from its screen).
constexpr float kFrameX = 165.0f;
constexpr float kFrameY = 1.0f;

// The level bar full, in the board's units (its glow is 20 more; FUN_005ab4d7).
constexpr int kBarFull = 708;
constexpr float kPi = 3.14159265f;

// The board-wide light sweep: a 1 in this chance each idle update (test
// builds can make it often).
#ifndef BJ2_SWEEP_CHANCE
#define BJ2_SWEEP_CHANCE 6000
#endif

// For the log: batches of lines cleared, and swaps that made none.
int g_matches = 0, g_bad_swaps = 0;

// The next backdrop's texels read ahead each frame: 8 KB, a few ms from the SD card.
constexpr uint32_t kBackdropPiece = 8 * 1024;

// For the watchdog: the main thread's counts, and where it is.
volatile uint32_t g_frames = 0, g_steps = 0;
volatile int g_state = 0;
const char* volatile g_where = "start";

// Above every other thread, as PPGC's: when the game stops (no update and no
// frame for 3 seconds) it logs where the main thread was, then every 15
// seconds. Its lines reach the card whenever the main thread waits.
void* Watchdog(void*) {
    uint32_t frames = g_frames, steps = g_steps;
    int still = 0;  // half seconds with neither
    for (;;) {
        usleep(500 * 1000);
        if (g_frames != frames || g_steps != steps) {
            frames = g_frames;
            steps = g_steps;
            still = 0;
            continue;
        }
        still++;
        if (still == 6 || (still > 6 && (still - 6) % 30 == 0))
            OctLog("bj2: STALLED %d s: frame %u, update %u, state %d, at %s", still / 2, unsigned(frames),
                   unsigned(steps), g_state, g_where);
    }
    return nullptr;
}

// The pads, as PAD_ScanPads would read them: a port with no controller is
// reset, or one plugged in again is never seen; a read that failed on the
// way keeps the reading before (as PPGC's CastleGame).
void ReadPadStatus(PADStatus (&pads)[PAD_CHANMAX]) {
    static PADStatus last[PAD_CHANMAX] = {};
    PAD_Read(pads);
    uint32_t reset = 0;
    for (int i = 0; i < PAD_CHANMAX; i++) {
        if (pads[i].err == PAD_ERR_NO_CONTROLLER) {
            reset |= PAD_CHAN0_BIT >> i;
        } else if (pads[i].err == PAD_ERR_TRANSFER || pads[i].err == PAD_ERR_NOT_READY) {
            pads[i] = last[i];
        }
        last[i] = pads[i];
    }
    if (reset) PAD_Reset(reset);
}

}  // namespace

bool Bj2App::Initialize() {
    bool ok = gx2d::Load(frame_, "frame.tex") && gx2d::Load(selector_, "selector.tex") &&
              gx2d::Load(hypergem_, "hypergem.tex");
    ok = gx2d::Load(scorepod_, "scorepod.tex") && ok;
    ok = gx2d::Load(bar_[0], "barleft.tex") && gx2d::Load(bar_[1], "barmid.tex") &&
         gx2d::Load(bar_[2], "barright.tex") && ok;
    ok = font_score_.Load("font_score") && ok;
    ok = font_small_.Load("font_small") && ok;
    ok = font_label_.Load("font_label") && ok;
    ok = font_big_.Load("font_big") && ok;
    ok = warp_.Load() && ok;
    ok = font_points_.Load("font_points") && ok;
    ok = fx_.Load() && ok;
    ok = gx2d::Load(litgems_, "litgems.tex") && ok;
    for (int c = 0; c < 7; c++) {
        char name[32];
        std::snprintf(name, sizeof(name), "gem%d.tex", c);
        ok = gx2d::Load(gems_[c], name) && ok;
        std::snprintf(name, sizeof(name), "glow%d.tex", c);
        ok = gx2d::Load(glows_[c], name) && ok;
    }
    bool sounds = audio_gc::Load();
    bool music = music_gc::Start();
    // The global generator from the clock, as the original's from the tick
    // count at start-up.
    game_.global.SRand(uint32_t(gettime()));
    game_.NewGame();
    LoadBackdrop(game_.Level());
    OctLog("bj2: sounds %s, music %s", sounds ? "loaded" : "MISSING", music ? "streaming" : "MISSING");
    OctLog("bj2: data %s; gem0 %dx%d, frame %dx%d, backdrop %dx%d", ok ? "loaded" : "MISSING", gems_[0].width,
           gems_[0].height, frame_.width, frame_.height, backdrop_.width, backdrop_.height);
    static lwp_t watchdog = LWP_THREAD_NULL;
    if (watchdog == LWP_THREAD_NULL && LWP_CreateThread(&watchdog, Watchdog, nullptr, nullptr, 64 * 1024, 100) != 0)
        OctLog("bj2: no watchdog");
    return ok;
}

void Bj2App::LoadBackdrop(int level) {
    if (level == backdrop_level_) return;
    backdrop_level_ = level;
    g_where = "backdrop swap";
    if (next_level_ == level && next_backdrop_.Started()) {
        // Read by now (it had the whole whirlpool); if not, the rest now.
        const bool rest = next_backdrop_.Reading();
        if (next_backdrop_.Read(UINT32_MAX) && next_backdrop_.Take(backdrop_)) {
            OctLog("bj2: backdrop %d in: %d frames, %u ms of reads%s", level, next_read_frames_,
                   unsigned(next_read_us_ / 1000), rest ? " (the rest read at the flash)" : "");
            next_level_ = -1;
            return;
        }
        OctLog("bj2: backdrop %d: the read ahead failed; read now", level);
    }
    next_backdrop_.Cancel();
    next_level_ = -1;
    char name[32];
    std::snprintf(name, sizeof(name), "backdrop%02d.tex", (level - 1) % 10);
    if (!gx2d::Load(backdrop_, name)) OctLog("bj2: backdrop %s: not read", name);
}

void Bj2App::PreloadBackdrop(int level) {
    char name[32];
    std::snprintf(name, sizeof(name), "backdrop%02d.tex", (level - 1) % 10);
    next_level_ = next_backdrop_.Start(name) ? level : -1;
    next_read_us_ = 0;
    next_read_frames_ = 0;
    if (next_level_ < 0) OctLog("bj2: backdrop %s: no read ahead", name);
}

void Bj2App::ReadPad() {
    PADStatus pads[PAD_CHANMAX];
    ReadPadStatus(pads);
    uint16_t now = pads[0].err == PAD_ERR_NONE ? pads[0].button : 0;
    // The stick as the D-pad.
    if (pads[0].err == PAD_ERR_NONE) {
        if (pads[0].stickX < -50) now |= PAD_BUTTON_LEFT;
        if (pads[0].stickX > 50) now |= PAD_BUTTON_RIGHT;
        if (pads[0].stickY > 50) now |= PAD_BUTTON_UP;
        if (pads[0].stickY < -50) now |= PAD_BUTTON_DOWN;
    }
#if BJ2_PADTEST
    // Test builds: the pad driven as a player would, to the hint's move: the
    // cursor to its gem, A, then the direction. A button every other frame.
    static uint32_t frame = 0;
    now = 0;
    bj2::Move m{};
    if (++frame % 2 == 0 && game_.GetState() == bj2::Game::State::Idle && game_.Hint(&m)) {
        if (cursor_col_ != m.col || cursor_row_ != m.row)
            now = cursor_col_ < m.col ? PAD_BUTTON_RIGHT : cursor_col_ > m.col ? PAD_BUTTON_LEFT
                : cursor_row_ < m.row ? PAD_BUTTON_DOWN : PAD_BUTTON_UP;
        else if (!selected_)
            now = PAD_BUTTON_A;
        else
            now = m.target_col > m.col ? PAD_BUTTON_RIGHT : m.target_col < m.col ? PAD_BUTTON_LEFT
                : m.target_row > m.row ? PAD_BUTTON_DOWN : PAD_BUTTON_UP;
    } else if (frame % 2 == 0 && game_.GetState() == bj2::Game::State::GameOver && frame % 600 == 0) {
        now = PAD_BUTTON_START;
    }
#endif
    pressed_ |= now & ~held_;
    held_ = now;
}

void Bj2App::Update(float delta_time) {
    g_where = "music";
    music_gc::Update();
    g_where = "pad";
    ReadPad();
    owed_ += std::min(delta_time, 0.1f);
    while (owed_ >= kUpdateTime) {
        owed_ -= kUpdateTime;
        g_where = "update";
        Step();
        g_steps = g_steps + 1;
        pressed_ = 0;
    }
    if (next_backdrop_.Reading()) {
        g_where = "backdrop read";
        const uint64_t t0 = SYS_GetTimeMicroseconds();
        next_backdrop_.Read(kBackdropPiece);
        next_read_us_ += SYS_GetTimeMicroseconds() - t0;
        next_read_frames_++;
    }
    // In hyperspace, the next level's backdrop is already at the tunnel's end.
    bool warped = game_.GetState() == bj2::Game::State::LevelUp && warp_.Active();
    LoadBackdrop(game_.Level() + (warped ? 1 : 0));
    g_where = "octave";
}

void Bj2App::Where(const char* where) { g_where = where; }

void Bj2App::Step() {
    updates_++;
    if (hint_updates_ > 0) hint_updates_--;

    // The D-pad: a press, then repeats while held.
    const uint16_t dirs = PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT | PAD_BUTTON_UP | PAD_BUTTON_DOWN;
    uint16_t dir = pressed_ & dirs;
    if (held_ & dirs) {
        repeat_++;
        if (!dir && repeat_ > kRepeatDelay && (repeat_ - kRepeatDelay) % kRepeatEvery == 0) dir = held_ & dirs;
    } else {
        repeat_ = 0;
    }
    int dx = (dir & PAD_BUTTON_RIGHT) ? 1 : (dir & PAD_BUTTON_LEFT) ? -1 : 0;
    int dy = (dir & PAD_BUTTON_DOWN) ? 1 : (dir & PAD_BUTTON_UP) ? -1 : 0;
    if (dx && dy) dy = 0;

#if BJ2_FXTEST
    // Test builds: the hint's gem made a power gem, then a hypercube, by
    // turns, and its move made, every 2 seconds.
    static int fx_wait = 0, fx_turn = 0;
    if (game_.GetState() == bj2::Game::State::Idle && ++fx_wait > 200) {
        bj2::Move m{};
        if (game_.Hint(&m)) {
            game_.CycleGem(m.col, m.row);
            if (fx_turn++ % 2) game_.CycleGem(m.col, m.row);
            game_.TrySwap(m.col, m.row, m.target_col - m.col, m.target_row - m.row);
        }
        fx_wait = 0;
    }
#endif
#if BJ2_WARPTEST
    // Test builds: each level over 3 seconds after its board settles.
    static int still = 0;
    if (game_.GetState() == bj2::Game::State::Idle && ++still > 300) {
        game_.CompleteLevel();
        still = 0;
    }
#endif
#if BJ2_AUTOPLAY
    // Test builds: the hint's move, a moment after the board settles; a new
    // game three seconds after one ends.
    static int wait = 0;
    if (game_.GetState() == bj2::Game::State::Idle && ++wait > 60) {
        bj2::Move m{cursor_col_, cursor_row_, cursor_col_, cursor_row_};
        if (game_.Hint(&m)) game_.TrySwap(m.col, m.row, m.target_col - m.col, m.target_row - m.row);
        cursor_col_ = m.col;
        cursor_row_ = m.row;
        wait = 0;
    } else if (game_.GetState() == bj2::Game::State::GameOver && ++wait > 600) {
        game_.NewGame();
        fx_.Clear();
        collapse_time_ = -1;
        wait = 0;
    }
#endif

    if (game_.GetState() == bj2::Game::State::GameOver) {
        if (pressed_ & (PAD_BUTTON_START | PAD_BUTTON_A)) {
            game_.NewGame();
            selected_ = false;
            fx_.Clear();
            collapse_time_ = -1;
        }
    } else {
        if (pressed_ & (PAD_BUTTON_Y | PAD_BUTTON_X)) hint_updates_ = kHintTime;
#if BJ2_DEBUGKEYS
        // Test builds: Z the level done, L out of moves, R the cursor's gem
        // a power gem, then a hypercube.
        if (pressed_ & PAD_TRIGGER_Z) game_.CompleteLevel();
        if (pressed_ & PAD_TRIGGER_L) game_.EndNoMoves();
        if (pressed_ & PAD_TRIGGER_R) game_.CycleGem(cursor_col_, cursor_row_);
#endif
        if (pressed_ & PAD_BUTTON_B) selected_ = false;
        if (dx || dy) {
            if (selected_) {
                // A gem held: the direction swaps it with that neighbour.
                if (game_.TrySwap(cursor_col_, cursor_row_, dx, dy)) {
                    cursor_col_ += dx;
                    cursor_row_ += dy;
                    selected_ = false;
                    hint_updates_ = 0;
                }
            } else {
                cursor_col_ = std::max(0, std::min(bj2::Board::kSize - 1, cursor_col_ + dx));
                cursor_row_ = std::max(0, std::min(bj2::Board::kSize - 1, cursor_row_ + dy));
            }
        }
        if (pressed_ & PAD_BUTTON_A) {
            selected_ = !selected_;
            if (selected_) audio_gc::PlaySelect();
        }
    }
    game_.Update();

    // The level's end: the warp from its first update, "LEVEL n" once the
    // next board has flown in, "NO MOVES!" at the end.
    const bj2::Game::State state = game_.GetState();
    if (state == bj2::Game::State::LevelUp) {
        if (last_state_ != bj2::Game::State::LevelUp) {
            // Memory: free in malloc's arena, and never yet claimed by it.
            const struct mallinfo mi = mallinfo();
            const unsigned unclaimed =
                unsigned(static_cast<char*>(SYS_GetArena1Hi()) - static_cast<char*>(SYS_GetArena1Lo()));
            OctLog("bj2: update %u: level %d done, the warp; free %uK + %uK unclaimed", unsigned(updates_),
                   game_.Level(), unsigned(mi.fordblks / 1024), unclaimed / 1024);
            warp_.Start();
            PreloadBackdrop(game_.Level() + 1);
        }
        const bool was_active = warp_.Active();
        warp_.Update(game_.StateTime());
        if (warp_.Active() && !was_active) OctLog("bj2: update %u: hyperspace", unsigned(updates_));
    }
    if (state != last_state_ && (state == bj2::Game::State::FlyIn || state == bj2::Game::State::Ready ||
                                 state == bj2::Game::State::GameOver))
        OctLog("bj2: update %u: state %d, level %d", unsigned(updates_), int(state), game_.Level());
    g_state = int(state);
    if (state == bj2::Game::State::Ready && last_state_ != bj2::Game::State::Ready)
        big_text_.Show(game_.Message(), 80, font_big_);
    if (state == bj2::Game::State::GameOver && last_state_ != bj2::Game::State::GameOver) {
        big_text_.Show(game_.Message(), 80, font_big_);
        StartCollapse();
    }
    if (state == bj2::Game::State::GameOver) UpdateCollapse();
    for (const bj2::FxEvent& e : game_.TakeFx()) {
        fx_.Handle(e);
        if (e.fx == bj2::Fx::Praise) big_text_.Show(e.value == 1 ? "EXCELLENT" : "INCREDIBLE", 80, font_big_);
    }
    fx_.Update();
    last_state_ = state;
    big_text_.Update();
    UpdateLighting();

    // The bar, twice an update, a 120th of the way and 1 more to the level's
    // progress; still while the board flies in (state 0xd).
    if (state != bj2::Game::State::FlyIn) {
        const int target = int(kBarFull * game_.LevelProgress());
        for (int i = 0; i < 2; i++) {
            if (bar_width_ < target) bar_width_ = std::min(target, bar_width_ + (target - bar_width_) / 120 + 1);
            if (bar_width_ > target) bar_width_ = std::max(target, bar_width_ - (bar_width_ - target) / 120 - 1);
        }
    }

    for (const bj2::SoundEvent& e : game_.TakeSounds()) {
        audio_gc::Play(e.sound, e.level);
        if (e.sound == bj2::Sound::Bad) g_bad_swaps++;
        if (e.sound == bj2::Sound::Match || e.sound == bj2::Sound::MatchBig) g_matches++;
    }
}

void Bj2App::Render(float fb_width, float fb_height) {
    static int frames = 0;
    g_frames = g_frames + 1;
    g_where = "render";
    if (frames++ < 3 || frames % 600 == 0)
        OctLog("bj2: frame %d at %.0fx%.0f: update %u, state %d, score %d, level %d; %d matches, %d swapped back",
               frames, fb_width, fb_height, unsigned(updates_), int(game_.GetState()), game_.Score(),
               game_.Level(), g_matches, g_bad_swaps);
    gx2d::Begin(fb_width, fb_height);
    const bj2::Game::State state = game_.GetState();
    const bool warping = state == bj2::Game::State::LevelUp;

    // Hyperspace takes the whole screen.
    if (warping && warp_.Active()) {
        g_where = "render hyperspace";
        warp_.DrawHyperspace(backdrop_);
        return;
    }

    if (warping) {
        g_where = "render whirlpool";
        warp_.DrawWhirlpool(backdrop_);
    } else {
        gx2d::Draw(backdrop_, 0, 0, 0, 640, 480);
    }

    if (warping) {
        // Collapsing into the black hole (FUN_005a1787): shrunk towards the
        // middle, turned, and flung apart as they go.
        const float z = warp_.Collapse(), e = std::sin(z * kPi / 2), sc = 1 - z;
        PieceTransform(264, 3, 750, 264 + std::floor(1000 * e), 3, -5 * z, sc, true);
        DrawBoard(fb_width, fb_height, true, false, false);
        PieceTransform(22, 42, 238, std::floor(22 - 300 * e), 42 - std::floor(400 * e), 6 * z, sc, true);
        DrawPod();
        PieceTransform(27, 339, 245, std::floor(27 - 300 * e), std::floor(339 + 600 * e), 4 * z, sc, true);
        DrawHelp();
        gx2d::ResetTransform();
        warp_.DrawTunnelOver();
        return;
    }
    if (state == bj2::Game::State::FlyIn) {
        // The next level's board and pods flying back in (FUN_005a238c).
        const float u = 1 - game_.StateTime() / float(bj2::Game::kFlyInTime);
        PieceTransform(264, 3, 750, 264 + 1024 * u * u, 3, -0.4f * u, 1, false);
        DrawBoard(fb_width, fb_height, false, false, false);
        PieceTransform(22, 42, 238, 22 - 600 * u, 42, 0.5f * u, 1, false);
        DrawPod();
        PieceTransform(27, 339, 245, 27 - 400 * u, 339, 0.6f * u, 1, false);
        DrawHelp();
        gx2d::ResetTransform();
        return;
    }

    const bool playing = state != bj2::Game::State::GameOver && state != bj2::Game::State::Ready;
    // The board, shaken by explosions; its effects over it (0x5a2d15).
    gx2d::SetTransform(1, 0, 0, 1, fx_.ShakeX() * kScale, fx_.ShakeY() * kScale);
    DrawBoard(fb_width, fb_height, true, true, playing);
    fx_.Draw(font_points_);
    gx2d::ResetTransform();
    DrawPod();
    DrawHelp();

    // "LEVEL n", "NO MOVES!" over the board.
    big_text_.Draw(font_big_);
    if (state == bj2::Game::State::GameOver)
        font_small_.Draw("Press Start to play again", 307.0f * kScale + 4 * kCell, 300, 1.0f,
                         GXColor{255, 255, 255, 255}, 0);
}

void Bj2App::UpdateLighting() {
    // The cursor's gem brightens, the rest fade; each light walks round its facets.
    for (int i = 0; i < 64; i++) {
        hover_[i] = std::max(0.0f, hover_[i] - 0.012f);
        hover_phase_[i] += 0.0625f;
        if (hover_phase_[i] >= 10) hover_phase_[i] -= 10;
    }
    if (game_.GetState() == bj2::Game::State::Idle) {
        float& h = hover_[cursor_row_ * bj2::Board::kSize + cursor_col_];
        h = std::min(1.0f, h + 0.045f);
    }
    // Now and then, a light sweeps the board corner to corner.
    if (sweep_ >= 0) {
        sweep_ += 0.02f;
        if (sweep_ > 1) sweep_ = -1;
    } else if (game_.GetState() == bj2::Game::State::Idle && light_rand_.Next() % BJ2_SWEEP_CHANCE == 0) {
        sweep_ = 0;
    }
}

void Bj2App::Lighting(float (&light)[64][9]) const {
    // Facets 0-7: up, up-left, left, down-left, down, down-right, right, up-right.
    static const float kDir[8][2] = {{0, -1}, {-0.7071f, -0.7071f}, {-1, 0}, {-0.7071f, 0.7071f},
                                     {0, 1},  {0.7071f, 0.7071f},   {1, 0},  {0.7071f, -0.7071f}};
    static const int kWalk[10] = {0, 4, 8, 2, 6, 3, 7, 8, 1, 5};
    for (auto& l : light)
        for (float& v : l) v = 0;
    const bj2::Board& board = game_.GetBoard();
    // A light at sx, sy on every gem near it: the facets facing it.
    auto source = [&](float sx, float sy, float scale, float offset, float intensity) {
        for (int i = 0; i < 64; i++) {
            const bj2::Gem* g = board.At(i % 8, i / 8);
            if (!g) continue;
            float d = (sx - g->x - 42) / scale, e = (sy - g->y - 42) / scale;
            float q = std::max(1.0f, d * d + e * e - offset);
            if (q >= 100) continue;
            for (int f = 0; f < 8; f++)
                light[i][f] += std::max(0.0f, (kDir[f][0] * d + kDir[f][1] * e) / q * intensity);
        }
    };
    const float p = std::fmod(updates_ * 0.006f, 1.0f);
    for (int i = 0; i < 64; i++) {
        const bj2::Gem* g = board.At(i % 8, i / 8);
        if (!g) continue;
        if (g->power && game_.ClearStep(g) < 0) source(g->x + 42, g->y + 42, 20, 10, std::fabs(2 * p - 1));
        float charge = game_.Charge(g);
        if (charge > 0) source(g->x + 42, g->y + 42, 15, 10, std::fabs(std::sin(15 * charge)) * 0.6f);
        // The cursor's light on its gem, gliding from facet to facet.
        if (hover_[i] > 0) {
            const int step = int(hover_phase_[i]);
            const float frac = hover_phase_[i] - float(step);
            light[i][kWalk[step]] += hover_[i] * (1 - frac);
            light[i][kWalk[(step + 1) % 10]] += hover_[i] * frac;
        }
        if (sweep_ >= 0) {
            float v = 1 - 9 * std::fabs(sweep_ - (i / 8 + i % 8) / 16.0f);
            if (v > 0) {
                light[i][3] += 0.8f * v;
                light[i][7] += 0.6f * v;
                light[i][8] += 0.6f * v;
            }
        }
    }
}

void Bj2App::StartCollapse() {
    collapse_time_ = 0;
    for (int i = 0; i < 64; i++) {
        drop_x_[i] = drop_y_[i] = drop_z_[i] = 0;
        drop_vx_[i] = drop_vy_[i] = drop_vz_[i] = drop_spin_[i] = drop_angle_[i] = 0;
    }
}

void Bj2App::UpdateCollapse() {
    if (collapse_time_ < 0) return;
    collapse_time_++;
    if (collapse_time_ < 200) {
        // Each gem jumps about its cell, every 6 updates.
        if (collapse_time_ % 6 == 0)
            for (int i = 0; i < 64; i++) {
                drop_x_[i] = float(int(collapse_rand_.Next() % 9) - 4);
                drop_y_[i] = float(int(collapse_rand_.Next() % 9) - 4);
            }
        return;
    }
    if (collapse_time_ == 200) {
        // Then they fly apart, towards the viewer.
        audio_gc::Play(bj2::Sound::Explode);
        for (int i = 0; i < 64; i++) {
            drop_vy_[i] = float(int(collapse_rand_.Next() % 6) - 4);
            drop_vx_[i] = float(int(collapse_rand_.Next() % 7) - 3);
            drop_vz_[i] = float(collapse_rand_.Next() % 12);
            drop_spin_[i] = float(int(collapse_rand_.Next() % 2000) - 1000) * 0.00011f;
        }
    }
    for (int i = 0; i < 64; i++) {
        drop_x_[i] += drop_vx_[i];
        drop_y_[i] += drop_vy_[i];
        drop_vy_[i] += 0.1f;
        drop_z_[i] += drop_vz_[i];
        drop_vz_[i] += 0.15f;
        drop_angle_[i] -= drop_spin_[i];
    }
}

void Bj2App::PieceTransform(float px0, float py0, float w, float px, float py, float angle, float scale,
                            bool collapse) {
    // Its centre where it is drawn, and where it goes.
    const float cx0 = px0 + w / 2, cy0 = py0 + w / 2;
    float cx = px + w / 2, cy = py + w / 2;
    if (collapse) {
        cx = (cx - 512) * scale + 512;
        cy = (cy - 384) * scale + 384;
    }
    const float c = std::cos(angle) * scale, s = std::sin(angle) * scale;
    // x' = M x + (C - M C0), in the screen's units.
    gx2d::SetTransform(c, -s, s, c, (cx - (c * cx0 - s * cy0)) * kScale, (cy - (s * cx0 + c * cy0)) * kScale);
}

void Bj2App::DrawBoard(float fb_width, float fb_height, bool gems, bool clip, bool cursor) {
    gx2d::Draw(frame_, 0, kFrameX, kFrameY, float(frame_.width), float(frame_.height));
    const bj2::Board& board = game_.GetBoard();
    bj2::Move hint{};
    const bool show_hint = cursor && hint_updates_ > 0 && game_.Hint(&hint);
    const int hint_age = kHintTime - hint_updates_;
    const float pulse = 0.5f + 0.5f * std::sin(float(updates_) * 0.08f);
    const bool collapsing = collapse_time_ >= 0 && game_.GetState() == bj2::Game::State::GameOver;
    const bj2::Gem* cube = game_.ZapCube();
    static float light[64][9];
    Lighting(light);

    // The board's cells are its 1024x768 positions, scaled; gems outside
    // the board (falling in) are cut off at its top edge.
    if (clip && !(collapsing && collapse_time_ >= 200))
        GX_SetScissor(uint32_t(307.0f * kScale * fb_width / 640.0f), uint32_t(32.0f * kScale * fb_height / 480.0f),
                      uint32_t(8 * kCell * fb_width / 640.0f) + 1, uint32_t(8 * kCell * fb_height / 480.0f) + 1);
    for (int row = 0; gems && row < bj2::Board::kSize; row++)
        for (int col = 0; col < bj2::Board::kSize; col++) {
            const bj2::Gem* g = board.At(col, row);
            if (!g || game_.IsBlownUp(g)) continue;
            float x = g->x, y = g->y, size = 84;  // board units
            // Dying (0x5aab8a): six steps of shrinking, odd sizes, centred.
            int step = game_.ClearStep(g);
            if (step >= 0) {
                float side = float(int((1 - (step + 1) / 7.0f) * 84) | 1);
                x += (84 - side) / 2;
                y += (84 - side) / 2;
                size = side;
            }
            uint8_t alpha = 255;
            if (g == cube) {
                // The used hypercube fading and shrinking.
                float t = game_.ZapCubeTime(), s = 1 - 0.25f * t;
                x += 84 * (1 - s) / 2;
                y += 84 * (1 - s) / 2;
                size *= s;
                alpha = uint8_t(255 * std::min(1.0f, 2 * (1 - t)));
            }
            // A gem turns (20 frames, one every 3 updates) while held, and once
            // round when it's the hint.
            const bool held = selected_ && col == cursor_col_ && row == cursor_row_;
            int frame = held ? int(updates_ / 3) % 20 : 0;
            if (show_hint && col == hint.col && row == hint.row && hint_age < 60) frame = hint_age / 3;
            float angle = 0;
            if (collapsing) {
                const int i = row * bj2::Board::kSize + col;
                x += drop_x_[i];
                y += drop_y_[i];
                if (collapse_time_ >= 200) {
                    // Flying at the viewer: bigger as it comes, about the middle.
                    if (drop_z_[i] >= 1000) continue;
                    float p = 1024 / (1024 - drop_z_[i]);
                    x = (x + 42 - 512) * p + 512 - 42 * p;
                    y = (y + 42 - 384) * p + 384 - 42 * p;
                    size = 84 * p;
                    angle = drop_angle_[i];
                }
            }
            const float sx = x * kScale, sy = y * kScale, ss = size * kScale;
            const GXColor tint{255, 255, 255, alpha};
            if (angle != 0) {
                const gx2d::Texture& t = g->color == 9 ? hypergem_ : gems_[g->color < 7 ? g->color : 0];
                gx2d::DrawRotated(t, 0, sx + ss / 2, sy + ss / 2, ss, ss, angle);
                continue;
            }
            if (g->color == 9) {
                gx2d::Draw(hypergem_, int(updates_ / 6), sx, sy, ss, ss, tint);
            } else if (g->color < 7) {
                gx2d::Draw(gems_[g->color], frame, sx, sy, ss, ss, tint);
                // Its lighting, when it's still in its cell and whole.
                if (step < 0 && frame == 0 && !collapsing && g->x == float(board.ColumnX(col)) &&
                    g->y == float(board.RowY(row)))
                    for (int f = 0; f < 9; f++) {
                        float l = light[row * bj2::Board::kSize + col][f];
                        if (l <= 0.01f) continue;
                        uint8_t v = uint8_t(std::min(255.0f, 255 * l));
                        gx2d::Draw(litgems_, g->color * 9 + f, sx, sy, ss, ss, GXColor{v, v, v, 255},
                                   gx2d::Blend::Add);
                    }
                if (g->power && step < 0) {
                    gx2d::Draw(glows_[g->color], int(updates_ / 4), sx, sy, ss, ss,
                               GXColor{255, 255, 255, uint8_t(160 + 95 * pulse)}, gx2d::Blend::Add);
                    fx_.DrawStars(x, y, g->color, int(updates_));
                }
                // Electrified: flickering white.
                float charge = game_.Charge(g);
                if (charge > 0)
                    gx2d::Draw(gems_[g->color], frame, sx, sy, ss, ss,
                               GXColor{255, 255, 255, uint8_t(255 * 0.6f * std::fabs(std::sin(15 * charge * 6.28f)))},
                               gx2d::Blend::Add);
            }
        }
    GX_SetScissor(0, 0, uint32_t(fb_width), uint32_t(fb_height));

    // The cursor (the original's selector, which shows a held gem), and the hint's arrow.
    if (cursor) {
        float cx = (307.0f + 84.0f * cursor_col_) * kScale, cy = (32.0f + 84.0f * cursor_row_) * kScale;
        uint8_t a = selected_ ? 255 : uint8_t(120 + 100 * pulse);
        gx2d::Draw(selector_, 0, cx, cy, kCell, kCell, GXColor{255, 255, 255, a});
        if (show_hint) fx_.DrawHint(307.0f + 84.0f * hint.col, 32.0f + 84.0f * hint.row, hint_age);
    }

    // The bar to the next level, in the slot at the foot of the frame: the
    // original's glow (FUN_0059df5c), white and added, its ends and a middle
    // stretched between them; short, the halves of its two ends.
    {
        // Our frame sits a little higher than the original's: 2 lower to centre it in the slot.
        const float x = 279.0f * kScale, y = 704.0f * kScale + 2.0f, h = 58.0f * kScale;
        const float w = (bar_width_ + 20.0f) * kScale;
        const float cap = float(bar_[0].frame_w), cap_h = float(bar_[0].frame_h);
        const GXColor white{255, 255, 255, 255};
        if (w < 2 * cap) {
            float half = w / 2;
            gx2d::DrawPart(bar_[0], 0, 0, half, cap_h, x, y, half, h, white, gx2d::Blend::Add);
            gx2d::DrawPart(bar_[2], cap - half, 0, half, cap_h, x + half, y, half, h, white, gx2d::Blend::Add);
        } else {
            gx2d::DrawPart(bar_[0], 0, 0, cap, cap_h, x, y, cap, h, white, gx2d::Blend::Add);
            gx2d::DrawPart(bar_[1], 1, 0, float(bar_[1].frame_w) - 2, cap_h, x + cap, y, w - 2 * cap, h, white,
                           gx2d::Blend::Add);
            gx2d::DrawPart(bar_[2], 0, 0, cap, cap_h, x + w - cap, y, cap, h, white, gx2d::Blend::Add);
        }
    }
}

void Bj2App::DrawPod() {
    // The score in its pod, top left, as the original; the level under it.
    const float cx = 88.0f;
    gx2d::Draw(scorepod_, 0, 13, 28, float(scorepod_.frame_w), float(scorepod_.frame_h));
    char text[32];
    std::snprintf(text, sizeof(text), "%d", game_.Score());
    font_score_.Draw(text, cx, 47, 0.62f, GXColor{255, 255, 255, 255}, 0);
    std::snprintf(text, sizeof(text), "LEVEL %d", game_.Level());
    font_label_.Draw(text, cx, 120, 1.0f, GXColor{255, 255, 255, 255}, 0);
}

void Bj2App::DrawHelp() {
    // The controls, bottom left: the buttons in a column, what they do beside them.
    static const char* const kHelp[][2] = {
        {"A", "pick up a gem"}, {"D-pad", "swap it"}, {"B", "put it down"}, {"Y", "hint"}};
    for (int i = 0; i < 4; i++) {
        float ly = 364.0f + 22.0f * float(i);
        font_small_.Draw(kHelp[i][0], 18, ly, 1.0f, GXColor{255, 215, 90, 255});
        font_small_.Draw(kHelp[i][1], 72, ly, 1.0f, GXColor{255, 255, 255, 235});
    }
}
