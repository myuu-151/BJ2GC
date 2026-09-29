// Bejeweled 2 on the GameCube: the game (../src/game) run at its 100
// updates a second, fed pad 1, and drawn with GX (gx2d) at 640x480, the size
// the original draws its small art at.
#pragma once

#include <ogc/lwp.h>

#include <cstdint>
#include <string>

#include "fx_gc.h"
#include "game/game.h"
#include "gx2d.h"
#include "warp_gc.h"

class Bj2App {
public:
    bool Initialize();
    void Update(float delta_time);
    // While Octave renders its UI (StageWidget): the board and its art.
    void Render(float fb_width, float fb_height);
    // For the watchdog's log: what the main thread is doing now.
    static void Where(const char* where);

    const bj2::Game& GetGame() const { return game_; }
    int CursorCol() const { return cursor_col_; }
    int CursorRow() const { return cursor_row_; }

    // The board's scale on the screen: its 1024x768 coordinates to 640x480.
    static constexpr float kScale = 0.625f;

private:
    void ReadPad();
    void Step();  // one of the game's updates, with the input it gets
    void LoadBackdrop(int level);
    // The board (its frame, gems, cursor and bar), the score pod and the
    // controls: each drawn through a transform while the level changes.
    void DrawBoard(float fb_width, float fb_height, bool gems, bool clip, bool cursor);
    void DrawPod();
    void DrawHelp();
    // A piece normally at px0, py0 (board units, w across, turning about its
    // w/2, w/2) moved to px, py, turned `angle`; `collapse` also shrinks it by
    // `scale` towards the screen's middle.
    static void PieceTransform(float px0, float py0, float w, float px, float py, float angle, float scale,
                               bool collapse);

    bj2::Game game_;
    float owed_ = 0;  // time owed to the next update
    uint32_t updates_ = 0;

    int cursor_col_ = 3, cursor_row_ = 3;
    bool selected_ = false;
    int hint_updates_ = 0;  // the hint shows while > 0
    uint16_t held_ = 0, pressed_ = 0;
    int repeat_ = 0;         // D-pad auto-repeat, updates held
    int backdrop_level_ = -1;
    // The next level's backdrop, read from the disc a piece a frame while
    // the level ends (as Octave's Texture::ReloadPart), so the warp doesn't
    // stop for it.
    void PreloadBackdrop(int level);
    gx2d::PartLoad next_backdrop_;
    int next_level_ = -1;
    uint64_t next_read_us_ = 0;  // time spent reading it, for the log
    int next_read_frames_ = 0;

    gx2d::Texture backdrop_, frame_, selector_, hypergem_, scorepod_;
    gx2d::Texture bar_[3];  // the level bar's glow: left, middle, right
    int bar_width_ = 0;     // its width in the board's units, easing to the level's progress (at most 708)
    Warp warp_;
    BigText big_text_;
    bj2::Game::State last_state_ = bj2::Game::State::Idle;
    gx2d::Texture gems_[7], glows_[7];
    gx2d::Font font_score_, font_small_, font_label_, font_big_, font_points_;
    BoardFx fx_;

    // The gems' lighting (0x596931): nine highlights a gem, the eight
    // facets and the middle, lit by power gems, electrified gems, the
    // cursor (a light walking round its facets) and a sweep now and then.
    void UpdateLighting();
    void Lighting(float (&light)[64][9]) const;
    gx2d::Texture litgems_;
    float hover_[64] = {}, hover_phase_[64] = {};
    float sweep_ = -1;
    bj2::MTRand light_rand_{77};

    // Out of moves (0x595ea2): 200 updates of shaking, then the gems fly
    // at the viewer, each its own way.
    void StartCollapse();
    void UpdateCollapse();
    int collapse_time_ = -1;  // -1: none
    float drop_x_[64], drop_y_[64], drop_z_[64], drop_vx_[64], drop_vy_[64], drop_vz_[64];
    float drop_spin_[64], drop_angle_[64];
    bj2::MTRand collapse_rand_{99};
};
