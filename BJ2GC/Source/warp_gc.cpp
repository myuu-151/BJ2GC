#include "warp_gc.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kS = 0.625f;  // the board's 1024x768 units to the screen's 640x480
constexpr float kPi = 3.14159265f;
const GXColor kWhite{255, 255, 255, 255};

uint8_t Byte(float v) { return uint8_t(std::max(0.0f, std::min(255.0f, v))); }

}  // namespace

bool Warp::Load() {
    bool ok = gx2d::Load(hyper_initial_, "hyperspace_initial.tex", true);
    ok = gx2d::Load(hyper_, "hyperspace.tex", true) && ok;
    ok = gx2d::Load(warp_lines_, "warplines.tex", true) && ok;
    ok = gx2d::Load(tunnel_end_, "tunnelend.tex") && ok;
    ok = gx2d::Load(fire_ring_, "firering.tex") && ok;
    ok = gx2d::Load(black_hole_, "blackhole.tex") && ok;
    ok = gx2d::Load(hole_cover_, "holemask.tex") && ok;
    return ok;
}

void Warp::Start() {
    // The backdrop as a mesh, each vertex's angle and distance from the middle.
    for (int row = 0; row < kGrid; row++)
        for (int col = 0; col < kGrid; col++) {
            int i = row * kGrid + col;
            base_x_[i] = float(col * 1024 / (kGrid - 1));
            base_y_[i] = float(row * 768 / (kGrid - 1));
            float dx = base_x_[i] - 512, dy = base_y_[i] - 384;
            ang_[i] = std::atan2(dy, dx) + 2 * kPi;
            rad_[i] = std::sqrt(dx * dx + dy * dy);
            x_[i] = base_x_[i];
            y_[i] = base_y_[i];
        }
    k_ = 1;
    swirl_ = 0;
    hole_ = hole_frame_ = hole_angle_ = hole_spin_ = 0;
    tt_ = z_ = 0;
    time_ = 0;
    countdown_ = 120;
    active_ = false;
    white_ = p_ = 0;
    since_active_ = 0;
    wander_x_ = wander_y_ = a_ = b_ = 0;
    twist_ = a0_ = a1_ = 0;
    std::fill(hist_x_, hist_x_ + kRings + 1, 0.0f);
    std::fill(hist_y_, hist_y_ + kRings + 1, 0.0f);
    shift_ = 0;
    cam_x_ = cam_y_ = 0;
    scroll1_ = scroll2_ = 0;
}

void Warp::Update(int time) {
    time_ = time;

    // The whirlpool: the backdrop grows a little, and from 90 its middle
    // turns and draws in, the more the further from the edge.
    k_ += 0.001f;
    if (time >= 90) swirl_ += 0.01f;
    for (int row = 1; row < kGrid - 1; row++)
        for (int col = 1; col < kGrid - 1; col++) {
            int i = row * kGrid + col;
            if (time >= 90) {
                float d = float(std::min(std::min(row, kGrid - 1 - row), std::min(col, kGrid - 1 - col)));
                rad_[i] = std::max(0.0f, rad_[i] - d * 0.35f * swirl_);
                ang_[i] += d * 0.001f * swirl_;
                while (ang_[i] >= 2 * kPi) ang_[i] -= 2 * kPi;
            }
            x_[i] = 512 + std::cos(ang_[i]) * rad_[i] * k_;
            y_[i] = 384 + std::sin(ang_[i]) * rad_[i] * k_;
        }

    // The black hole fading in, turning ever faster.
    hole_ = std::min(1.0f, hole_ + 0.02f);
    hole_frame_ -= 0.1f;
    if (hole_frame_ < 0) hole_frame_ += 5;
    hole_spin_ += 0.0005f;
    hole_angle_ -= hole_spin_;

    // The board and pods collapsing, from 80.
    if (time >= 80) {
        tt_ = std::min(1.0f, tt_ + 0.05f);
        z_ = std::min(1.0f, z_ + (1 - std::cos(tt_ * kPi / 2)) * 0.01f);
    }

    if (time < kTunnelStart) return;
    if (!active_) {
        if (--countdown_ <= 0) {
            active_ = true;
            white_ = 1;
        }
        return;
    }
    since_active_++;
    white_ = std::max(0.0f, white_ - 0.015f);
    // The tunnel's far end wanders, its turns passing down it to the viewer.
    wander_x_ += 4 * std::sin(a_);
    a_ += 0.0077f;
    wander_y_ += 4 * std::sin(b_);
    b_ += 0.013f;
    twist_ += 0.002f * std::sin(a0_) + 0.001f * std::sin(a1_);
    a0_ += 0.006f;
    a1_ += 0.0093f;
    hist_x_[kRings] = wander_x_;
    hist_y_[kRings] = wander_y_;
    shift_ += 0.25f;
    if (shift_ >= 1) {
        shift_ -= 1;
        for (int r = 0; r < kRings; r++) {
            hist_x_[r] = hist_x_[r + 1];
            hist_y_[r] = hist_y_[r + 1];
        }
    }
    cam_x_ += (RingX(0) - cam_x_) * 0.09f;
    cam_y_ += (RingY(0) - cam_y_) * 0.09f;
    scroll1_ += 0.006f;
    if (scroll1_ >= 1) scroll1_ -= 1;
    scroll2_ += 0.004f;
    if (scroll2_ >= 1) scroll2_ -= 1;
    // After 200, out of the tunnel.
    if (since_active_ > 200 && p_ < 1) {
        float inc = (p_ / 50 + 0.001f) * std::min(1.0f, (1.001f - p_) * 5);
        p_ = std::min(1.0f, p_ + inc);
    }
}

float Warp::RingX(int r) const { return hist_x_[r] + (hist_x_[r + 1] - hist_x_[r]) * shift_; }
float Warp::RingY(int r) const { return hist_y_[r] + (hist_y_[r + 1] - hist_y_[r]) * shift_; }

void Warp::DrawWhirlpool(const gx2d::Texture& backdrop) const {
    static gx2d::Vertex v[(kGrid - 1) * (kGrid - 1) * 4];
    int n = 0;
    for (int row = 0; row < kGrid - 1; row++)
        for (int col = 0; col < kGrid - 1; col++) {
            const int corners[4] = {row * kGrid + col, row * kGrid + col + 1, (row + 1) * kGrid + col + 1,
                                    (row + 1) * kGrid + col};
            for (int c : corners)
                v[n++] = {x_[c] * kS, y_[c] * kS, base_x_[c] / 1024, base_y_[c] / 768, kWhite};
        }
    gx2d::DrawMesh(backdrop, v, n);

    // The black hole: its cover darkening the middle, two of its frames
    // crossfaded, added, turning.
    if (hole_ > 0) {
        gx2d::Draw(hole_cover_, 0, 320 - 40, 240 - 40, 80, 80, GXColor{255, 255, 255, Byte(hole_ * 255)});
        int f0 = int(hole_frame_) % 5, f1 = (f0 + 1) % 5;
        float frac = hole_frame_ - std::floor(hole_frame_);
        gx2d::DrawRotated(black_hole_, f0, 320, 240, 160, 160, hole_angle_,
                          GXColor{255, 255, 255, Byte(hole_ * 255 * (1 - frac))}, gx2d::Blend::Add);
        gx2d::DrawRotated(black_hole_, f1, 320, 240, 160, 160, hole_angle_,
                          GXColor{255, 255, 255, Byte(hole_ * 255 * frac)}, gx2d::Blend::Add);
    }
}

void Warp::DrawTunnelOver() const {
    if (time_ < kTunnelStart || active_) return;
    DrawTunnel();
    // Its last 19 updates whiten the screen.
    if (countdown_ < 20) gx2d::Rect(0, 0, 640, 480, GXColor{255, 255, 255, Byte((20 - countdown_) / 20.0f * 255)});
}

void Warp::DrawTunnel() const {
    // 24 rings of 24, far to near, each ring's colour darker with distance;
    // a ring's depth and size shrink the tunnel to nothing as p reaches 1.
    const float q = 1 - p_;
    float px[kRings][kRingVerts + 1], py[kRings][kRingVerts + 1];
    uint8_t shade[kRings];
    for (int r = 0; r < kRings; r++) {
        float z = 800 - 5000.0f * r * q / kRings;
        float s = 1024 / (1024 - z);
        float radius = float(int(200 - 150.0f * r * q / kRings));
        float cx = (RingX(r) - cam_x_) * q, cy = (RingY(r) - cam_y_) * q;
        for (int j = 0; j <= kRingVerts; j++) {
            float phi = 2 * kPi * j / kRingVerts + 4 * twist_;
            px[r][j] = ((cx + std::cos(phi) * radius) * s + 512) * kS;
            py[r][j] = ((cy + std::sin(phi) * radius) * s + 384) * kS;
        }
        shade[r] = uint8_t(std::min(255, 384 - 15 * r));
    }
    static gx2d::Vertex v[(kRings - 1) * kRingVerts * 4];
    auto layer = [&](const gx2d::Texture& t, float scroll) {
        int n = 0;
        for (int r = kRings - 2; r >= 0; r--) {
            float v_far = 0.02f * (r + 1) - 0.48f + scroll, v_near = 0.02f * r - 0.48f + scroll;
            GXColor far{shade[r + 1], shade[r + 1], shade[r + 1], 255}, near{shade[r], shade[r], shade[r], 255};
            for (int j = 0; j < kRingVerts; j++) {
                float u0 = float(j) / kRingVerts, u1 = float(j + 1) / kRingVerts;
                v[n++] = {px[r + 1][j], py[r + 1][j], u0, v_far, far};
                v[n++] = {px[r + 1][j + 1], py[r + 1][j + 1], u1, v_far, far};
                v[n++] = {px[r][j + 1], py[r][j + 1], u1, v_near, near};
                v[n++] = {px[r][j], py[r][j], u0, v_near, near};
            }
        }
        gx2d::DrawMesh(t, v, n, gx2d::Blend::Add);
    };
    if (!active_) {
        layer(hyper_initial_, 0);
    } else {
        layer(hyper_, scroll1_);
        layer(warp_lines_, scroll2_);
    }
}

void Warp::DrawHyperspace(const gx2d::Texture& backdrop) const {
    gx2d::Rect(0, 0, 640, 480, GXColor{0, 0, 0, 255});
    const float q = 1 - p_;
    const int far = kRings - 1;
    const float z = 800 - 5000.0f * far * q / kRings, s = 1024 / (1024 - z);
    const float radius = float(int(200 - 150.0f * far * q / kRings)) * s;
    const float ex = (RingX(far) - cam_x_) * q * s + 512, ey = (RingY(far) - cam_y_) * q * s + 384;

    // The next level's backdrop at the tunnel's end, growing and brightening.
    float grow = std::min(1.0f, 0.95f * p_ * p_ * p_ + 0.2f);
    float bw = 1024 * grow, bh = 768 * grow;
    float bx = ex * (1 - p_) + 512 * p_, by = ey * (1 - p_) + 384 * p_;
    uint8_t c = Byte(300 * p_);
    gx2d::Draw(backdrop, 0, (bx - bw / 2) * kS, (by - bh / 2) * kS, bw * kS, bh * kS, GXColor{c, c, c, 255});

    // The end's round window onto it, black around; the fire ring about it.
    float te = 2.5f * radius;
    float tx = (ex - te / 2) * kS, ty = (ey - te / 2) * kS, tw = te * kS;
    gx2d::Draw(tunnel_end_, 0, tx, ty, tw, tw);
    const GXColor black{0, 0, 0, 255};
    gx2d::Rect(0, 0, 640, std::max(0.0f, ty), black);
    gx2d::Rect(0, ty + tw, 640, std::max(0.0f, 480 - ty - tw), black);
    gx2d::Rect(0, ty, std::max(0.0f, tx), tw, black);
    gx2d::Rect(tx + tw, ty, std::max(0.0f, 640 - tx - tw), tw, black);
    float fr = 5 * radius;
    uint8_t f = Byte(400 * p_);
    gx2d::Draw(fire_ring_, (since_active_ / 2) % 10, (ex - fr / 2) * kS, (ey - fr / 2) * kS, fr * kS, fr * kS,
               GXColor{f, f, f, 255}, gx2d::Blend::Add);

    DrawTunnel();
    if (white_ > 0) gx2d::Rect(0, 0, 640, 480, GXColor{255, 255, 255, Byte(white_ * 255)});
}
