// BoardFx (fx_gc.h): the board's effects as the original's code has them
// (docs/board-effects.md).
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "fx_gc.h"

namespace {

constexpr float kS = 0.625f;  // the board's units to the screen's
constexpr float kPi = 3.14159265f;

// The points' and shards' colours by gem colour (0x5cf1eb); the bolts' (0x5989c7).
const GXColor kGemColours[7] = {{255, 255, 64, 255},  {255, 255, 255, 255}, {64, 128, 255, 255}, {255, 153, 153, 255},
                                {255, 64, 255, 255},  {255, 181, 145, 255}, {64, 255, 64, 255}};
const GXColor kBoltColours[7] = {{255, 255, 64, 255}, {200, 200, 200, 255}, {64, 128, 255, 255}, {255, 100, 100, 255},
                                 {255, 64, 255, 255}, {255, 128, 64, 255},  {64, 255, 64, 255}};

GXColor Colour(const GXColor (&table)[7], int c, float k = 1) {
    GXColor out = table[c >= 0 && c < 7 ? c : 1];
    out.r = uint8_t(out.r * k);
    out.g = uint8_t(out.g * k);
    out.b = uint8_t(out.b * k);
    return out;
}

uint8_t Alpha(float a) { return uint8_t(std::max(0.0f, std::min(1.0f, a)) * 255); }

}  // namespace

bool BoardFx::Load() {
    bool ok = gx2d::Load(explosion_, "explosion.tex");
    ok = gx2d::Load(shard_, "gemshard.tex") && ok;
    ok = gx2d::Load(sparkle_, "sparkle.tex") && ok;
    ok = gx2d::Load(star_, "bigstar.tex") && ok;
    ok = gx2d::Load(glow_, "powerglow.tex") && ok;
    ok = gx2d::Load(lightning_, "lightning.tex") && ok;
    ok = gx2d::Load(lightning_centre_, "lightning_center.tex") && ok;
    ok = gx2d::Load(arrow_, "hint_arrow.tex") && ok;
    ok = gx2d::Load(arrow_glow_, "hint_glow.tex") && ok;
    return ok;
}

void BoardFx::Clear() {
    particles_.clear();
    blasts_.clear();
    points_.clear();
    bolts_.clear();
    glows_.clear();
    shake_time_ = shake_x_ = shake_y_ = 0;
}

void BoardFx::Blasts(float cx, float cy, int count, int step, bool delayed) {
    // Slot j: j out, at j x 0.503 radians and a little, after j/10 updates (0x5a68b0).
    for (int j = 0; j < count; j += step) {
        float a = j * 0.503f + float(rand_.Next() % 100) / 800;
        blasts_.push_back({cx + std::cos(a) * j, cy + std::sin(a) * j, delayed ? j / 10 : 0, 0, 0});
    }
}

void BoardFx::Shatter(float cx, float cy, int color, int count, float speed, float jitter) {
    // Shards thrown out and up, each turning through its 40 frames.
    for (int j = 0; j < count; j++) {
        float a = j * 0.906f + float(rand_.Next() % 100) / 400;
        float r = float(int(2.6f * j));
        Particle p{};
        p.kind = 0;
        p.x = cx + std::cos(a) * r;
        p.y = cy + std::sin(a) * r;
        p.vx = std::cos(a) * speed + (float(rand_.Next() % 100) / 100 - 0.5f) * jitter;
        p.vy = std::sin(a) * speed + (float(rand_.Next() % 100) / 100 - 1.0f) * jitter;
        p.frame = int(rand_.Next() % 40);
        p.period = int(rand_.Next() % 4) + 1;
        p.color = color;
        particles_.push_back(p);
    }
}

void BoardFx::Handle(const bj2::FxEvent& e) {
    using bj2::Fx;
    const float cx = e.x + 42, cy = e.y + 42;  // a gem's middle
    switch (e.fx) {
    case Fx::Points: {
        float size = float(std::min(e.count, 80));
        points_.push_back({e.x, e.y, e.value, e.color, std::min(int(2 * size) + 70, 180), 0, size, 0, 0});
        break;
    }
    case Fx::Explosion: {
        // Fewer blasts each when several go off at once.
        Blasts(cx, cy, 80, std::max(1, std::min(e.count, 15)), true);
        for (int j = 0; j < 18; j++) {
            float a = j * 2 * kPi / 18 + Random01() * 0.3f;
            float r = float(int(0.2f * j)) + 5;
            Particle p{};
            p.kind = 2;
            p.x = cx + std::cos(a) * r;
            p.y = cy + std::sin(a) * r;
            p.vx = std::cos(a) * r * 0.351f;
            p.vy = std::sin(a) * r * 0.351f;
            p.period = int(rand_.Next() % 4) + 3;
            particles_.push_back(p);
        }
        break;
    }
    case Fx::Shatter:
        Shatter(cx, cy, e.color, particles_.size() > 150 ? 8 : 15, 4, 3.1f);
        break;
    case Fx::Shake:
        shake_time_ = std::min(0.2f * e.count + 1.2f, 2.0f);
        shake_size_ = std::min(1.2f * e.count + 1.25f, 6.0f);
        break;
    case Fx::Bolt: {
        Bolt b{};
        b.x0 = cx;
        b.y0 = cy;
        b.x1 = e.x2 + 42;
        b.y1 = e.y2 + 42;
        // Bowed to one side by the log of its length squared.
        float dx = b.x1 - b.x0, dy = b.y1 - b.y0, len = std::sqrt(dx * dx + dy * dy);
        float bow = len > 1 ? std::log(dx * dx + dy * dy) * 0.4f : 0;
        b.bow_x = len > 1 ? -dy / len * bow : 0;
        b.bow_y = len > 1 ? dx / len * bow : 0;
        b.color = e.color;
        Jitter(b);
        bolts_.push_back(b);
        break;
    }
    case Fx::Zapped:
        Blasts(cx, cy, 10, 1, false);
        Shatter(cx - 15, cy - 15, e.color, 18, 2.5f, 1.9f);
        break;
    case Fx::PowerGlow:
        glows_.push_back({cx, cy, 15, 0});
        break;
    case Fx::Praise:
        break;  // the front end's BigText
    }
}

void BoardFx::Jitter(Bolt& b) {
    // Eight points from end to end, bowed as it fades, each shaken sideways.
    float dx = b.x1 - b.x0, dy = b.y1 - b.y0, len = std::sqrt(dx * dx + dy * dy);
    float nx = len > 0 ? -dy / len : 0, ny = len > 0 ? dx / len : 0;
    float fade = std::max(0.0f, 1 - 3 * (1 - b.t));
    for (int i = 0; i < 8; i++) {
        float f = i / 7.0f, w = 1 - std::fabs(1 - 2 * f);
        float r = float(int(rand_.Next() % 1000) - 500) / 500;
        b.px[i] = b.x0 + dx * f + (b.bow_x * fade + 24 * r * nx) * w;
        b.py[i] = b.y0 + dy * f + (b.bow_y * fade + 24 * r * ny) * w;
        b.half[i] = 6 + 18 * float(rand_.Next() % 100) / 100;
    }
}

void BoardFx::Update() {
    tick_++;
    for (Particle& p : particles_) {
        if (p.kind == 2) {
            p.vx *= 0.98f;
            p.vy = p.vy * 0.98f + 0.07f;
        } else {
            p.vy += 0.15f;
        }
        p.x += p.vx;
        p.y += p.vy;
        if (++p.tick % p.period == 0) p.frame = p.kind == 2 ? p.frame + 1 : (p.frame + 1) % 40;
    }
    particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
                                    [](const Particle& p) { return p.y > 800 || (p.kind == 2 && p.frame >= 14); }),
                     particles_.end());
    for (Blast& b : blasts_) {
        if (b.delay > 0) {
            b.delay--;
            continue;
        }
        if (++b.tick % 2 == 0) b.frame++;
    }
    blasts_.erase(std::remove_if(blasts_.begin(), blasts_.end(), [](const Blast& b) { return b.frame >= 20; }),
                  blasts_.end());
    for (Points& p : points_) {
        // Springing up to its size, rising a pixel every 3, fading at the end.
        float target = 0.5f + 0.006f * p.size;
        p.velocity = (p.velocity + (target - p.scale) * 0.018f) * std::min(0.86f + 0.0015f * p.size, 0.962f);
        p.scale += p.velocity;
        if (++p.age % 3 == 0) p.y -= 1;
        p.life--;
    }
    points_.erase(std::remove_if(points_.begin(), points_.end(), [](const Points& p) { return p.life <= 0; }),
                  points_.end());
    for (Bolt& b : bolts_) {
        b.t += 0.012f;
        if (++b.tick % 4 == 0) Jitter(b);
    }
    bolts_.erase(std::remove_if(bolts_.begin(), bolts_.end(), [](const Bolt& b) { return b.t >= 1; }), bolts_.end());
    for (Glow& g : glows_) {
        if (g.wait > 0)
            g.wait--;
        else
            g.t += 0.012f;
    }
    glows_.erase(std::remove_if(glows_.begin(), glows_.end(), [](const Glow& g) { return g.t >= 1; }), glows_.end());
    // The shake: every 3 updates a new offset, smaller as it dies.
    if (tick_ % 3 == 0) {
        if (shake_time_ > 0) {
            shake_time_ = std::max(0.0f, shake_time_ - 0.1f);
            auto offset = [&] {
                return float(int((float(int(rand_.Next() % 2000) - 1000) / 1000) * shake_size_ * shake_time_));
            };
            shake_x_ = offset();
            shake_y_ = offset();
        } else {
            shake_x_ = shake_y_ = 0;
        }
    }
}

void BoardFx::DrawStars(float x, float y, int color, int tick) const {
    // Two stars over the gem, turning opposite ways, one fading in as the
    // other fades out (0x596747).
    float p = std::fmod(tick * 0.006f, 1.0f), fade = std::fabs(2 * p - 1);
    float cx = (x + 40) * kS, cy = (y + 40) * kS, size = 120 * kS;
    GXColor c = Colour(kGemColours, color);
    c.a = Alpha(fade);
    gx2d::DrawRotated(star_, 0, cx, cy, size, size, tick * 0.015f, c, gx2d::Blend::Add);
    c.a = Alpha(1 - fade);
    gx2d::DrawRotated(star_, 0, cx, cy, size, size, -tick * 0.004f, c, gx2d::Blend::Add);
}

void BoardFx::DrawHint(float x, float y, int age) const {
    // a8 counts 290 down; ac up every 5 updates.
    const int a8 = 290 - age, ac = age / 5;
    const GXColor white{255, 255, 255, Alpha(0.1f * a8)};
    auto spark = [&](int frame, float sx, float sy) {
        gx2d::Draw(sparkle_, frame % 14, sx * kS, sy * kS, 40 * kS, 40 * kS, white, gx2d::Blend::Add);
    };
    if (a8 >= 80) spark(ac, x + 13, y - 7);
    if (ac > 7 && a8 >= 40) spark(ac - 8, x - 10, y + 3);
    if (ac > 15) spark(ac - 16, x + 5, y + 19);
    if (a8 < 90) return;
    // The arrow over it, bobbing.
    const float t = float(a8 - 90), e = 200 - t;
    const float a = std::min(1.0f, 3.5f * (1 - std::fabs(t / 100 - 1)));
    const float b = (1 - std::cos(0.125f * e)) / 2;
    const float ay = y - 49 + float(int(10 * b));
    gx2d::Draw(arrow_, 0, (x - 8) * kS, ay * kS, 100 * kS, 80 * kS, GXColor{255, 255, 255, Alpha(a)});
    gx2d::Draw(arrow_glow_, 0, (x + 21) * kS, (ay + 20) * kS, 40 * kS, 40 * kS, GXColor{255, 255, 255, Alpha(b * a)},
               gx2d::Blend::Add);
}

void BoardFx::Draw(const gx2d::Font& points_font) const {
    for (const Particle& p : particles_) {
        if (p.kind == 2)
            gx2d::Draw(sparkle_, p.frame, (p.x - 20) * kS, (p.y - 20) * kS, 40 * kS, 40 * kS,
                       GXColor{255, 255, 255, 255}, gx2d::Blend::Add);
        else
            gx2d::Draw(shard_, p.frame, (p.x - 15) * kS, (p.y - 15) * kS, 30 * kS, 30 * kS,
                       Colour(kGemColours, p.color));
    }
    for (const Blast& b : blasts_)
        if (b.delay <= 0 && b.frame > 0)
            gx2d::Draw(explosion_, b.frame, (b.x - 40) * kS, (b.y - 40) * kS, 80 * kS, 80 * kS,
                       GXColor{255, 255, 255, 255}, gx2d::Blend::Add);
    for (const Points& p : points_) {
        if (p.scale <= 0) continue;
        char text[16];
        std::snprintf(text, sizeof(text), "%d", p.value);
        GXColor c = Colour(kGemColours, p.color);
        c.a = Alpha(p.life / 18.0f);
        points_font.Draw(text, p.x * kS, p.y * kS - points_font.LineHeight(p.scale) / 2, p.scale, c, 0);
    }
    for (const Bolt& b : bolts_) {
        // A strip through its points: its colour, then a white core.
        float k = std::min(1.0f, 8 * (1 - b.t));
        gx2d::Vertex v[7 * 4];
        auto strip = [&](const gx2d::Texture& t, GXColor c, float width) {
            int n = 0;
            for (int i = 0; i < 7; i++) {
                float dx = b.px[i + 1] - b.px[i], dy = b.py[i + 1] - b.py[i], len = std::sqrt(dx * dx + dy * dy);
                float nx = len > 0 ? -dy / len : 0, ny = len > 0 ? dx / len : 0;
                float h0 = b.half[i] * width, h1 = b.half[i + 1] * width;
                v[n++] = {(b.px[i] - nx * h0) * kS, (b.py[i] - ny * h0) * kS, 0, 0, c};
                v[n++] = {(b.px[i] + nx * h0) * kS, (b.py[i] + ny * h0) * kS, 1, 0, c};
                v[n++] = {(b.px[i + 1] + nx * h1) * kS, (b.py[i + 1] + ny * h1) * kS, 1, 1, c};
                v[n++] = {(b.px[i + 1] - nx * h1) * kS, (b.py[i + 1] - ny * h1) * kS, 0, 1, c};
            }
            gx2d::DrawMesh(t, v, n, gx2d::Blend::Add);
        };
        strip(lightning_, Colour(kBoltColours, b.color, k), 1.0f);
        uint8_t w = uint8_t(255 * k);
        strip(lightning_centre_, GXColor{w, w, w, 255}, 0.6f);
    }
    for (const Glow& g : glows_) {
        if (g.wait > 0) continue;
        gx2d::Draw(glow_, int(30 * g.t) % 10, (g.x - 120) * kS, (g.y - 120) * kS, 240 * kS, 240 * kS,
                   GXColor{255, 255, 255, Alpha(4 * (1 - std::fabs(2 * g.t - 1)))}, gx2d::Blend::Add);
    }
}
