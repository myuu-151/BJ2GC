// The original's effects over the board, each as its code has it (docs/board-effects.md).
#pragma once

#include <string>
#include <vector>

#include "game/game.h"
#include "game/mtrand.h"
#include "gx2d.h"

// The big words over the board: "LEVEL n", "EXCELLENT", "NO MOVES!"
// (FUN_005b524c on the EffectOverlay): they swell from a line, wobble, grow
// and go, in QuincyCaps74gold2, centred on 654, 245 of the board's units.
class BigText {
public:
    void Show(const std::string& text, int duration, const gx2d::Font& font);
    void Update();  // once a game update
    void Draw(const gx2d::Font& font) const;
    bool Visible() const { return on_; }

private:
    std::string text_;
    float box_w_ = 0;
    int duration_ = 0, age_ = 0;
    bool on_ = false, rising_ = true;
    float alpha_ = 0, scale_ = 1, scale_v_ = 0, wobble_ = 1, wobble_v_ = 0, firm_ = 0;
};

// The board's particles and flashes, from the game's FxEvents: explosions,
// shards, sparkles, the shake, the points, the hypercube's lightning and
// glow, the power gems' stars, the hint's arrow. Positions in the board's
// 1024x768 units; drawn through the board's own offset (the shake).
class BoardFx {
public:
    bool Load();
    void Handle(const bj2::FxEvent& e);
    void Update();  // once a game update
    void Clear();

    float ShakeX() const { return shake_x_; }
    float ShakeY() const { return shake_y_; }

    // Over a power gem whose top left is x, y: its two turning stars.
    void DrawStars(float x, float y, int color, int tick) const;
    // The hint on the gem at x, y, `age` updates after asked (0 to 290).
    void DrawHint(float x, float y, int age) const;
    // Over the gems, in the original's order: particles, explosions,
    // points, lightning, the hypercube's glow.
    void Draw(const gx2d::Font& points_font) const;

private:
    struct Particle {
        int kind;  // 0 a shard, 2 a sparkle
        float x, y, vx, vy;
        int frame, period, tick, color;
    };
    struct Blast {
        float x, y;
        int delay, frame, tick;
    };
    struct Points {
        float x, y;
        int value, color, life, age;
        float size, scale, velocity;
    };
    struct Bolt {
        float x0, y0, x1, y1;  // centres
        float bow_x, bow_y;
        float t;
        int color, tick;
        float px[8], py[8], half[8];
    };
    struct Glow {
        float x, y;
        int wait;
        float t;
    };
    void Shatter(float cx, float cy, int color, int count, float speed, float jitter);
    void Blasts(float cx, float cy, int count, int step, bool delayed);
    void Jitter(Bolt& b);
    float Random01() { return float(rand_.Next() % 1000) / 1000.0f; }

    gx2d::Texture explosion_, shard_, sparkle_, star_, glow_, lightning_, lightning_centre_, arrow_, arrow_glow_;
    std::vector<Particle> particles_;
    std::vector<Blast> blasts_;
    std::vector<Points> points_;
    std::vector<Bolt> bolts_;
    std::vector<Glow> glows_;
    float shake_time_ = 0, shake_size_ = 0, shake_x_ = 0, shake_y_ = 0;
    int tick_ = 0;
    bj2::MTRand rand_{1234};
};
