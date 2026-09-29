// The warp between levels, as the original draws it
// (docs/level-transition.md): the backdrop swirled into a black hole while the board and
// its pods collapse, then the hyperspace tunnel, which opens onto the next
// level's backdrop. Driven by the game's clock in its LevelUp state: Update
// once a game update, the drawing as often as the screen wants.
#pragma once

#include "gx2d.h"

class Warp {
public:
    static constexpr int kTunnelStart = 100;  // the Hyperspace widget made (FUN_0059d20d)
    static constexpr int kActive = 220;       // its countdown over: the flash, the next backdrop

    bool Load();
    void Start();          // the level reached: state 10
    void Update(int time);  // `time` updates since Start, after this one

    bool Active() const { return active_; }
    // How far the board and pods have collapsed, 0 to 1.
    float Collapse() const { return z_; }

    // Before Active: the old backdrop swirling, the black hole over it (under
    // the collapsing board), and the tunnel coming, over everything.
    void DrawWhirlpool(const gx2d::Texture& backdrop) const;
    void DrawTunnelOver() const;
    // Active: the whole screen, the next backdrop at the tunnel's end.
    void DrawHyperspace(const gx2d::Texture& backdrop) const;

private:
    static constexpr int kGrid = 48;  // the whirlpool's vertices a side
    static constexpr int kRings = 24, kRingVerts = 24;

    void DrawTunnel() const;
    // A ring's centre, and the camera, in the board's units about the middle.
    float RingX(int r) const;
    float RingY(int r) const;

    gx2d::Texture hyper_initial_, hyper_, warp_lines_, tunnel_end_, fire_ring_, black_hole_, hole_cover_;

    // The whirlpool (FUN_0059d37a, FUN_005aafae).
    float base_x_[kGrid * kGrid], base_y_[kGrid * kGrid];
    float ang_[kGrid * kGrid], rad_[kGrid * kGrid];
    float x_[kGrid * kGrid], y_[kGrid * kGrid];
    float k_ = 1, swirl_ = 0;
    float hole_ = 0, hole_frame_ = 0, hole_angle_ = 0, hole_spin_ = 0;
    float tt_ = 0, z_ = 0;
    int time_ = 0;

    // Hyperspace (FUN_005c72d1, FUN_005c7957, FUN_005c620e).
    int countdown_ = 120;
    bool active_ = false;
    float white_ = 0, p_ = 0;
    int since_active_ = 0;
    float wander_x_ = 0, wander_y_ = 0, a_ = 0, b_ = 0;
    float twist_ = 0, a0_ = 0, a1_ = 0;
    float hist_x_[kRings + 1], hist_y_[kRings + 1], shift_ = 0;
    float cam_x_ = 0, cam_y_ = 0;
    float scroll1_ = 0, scroll2_ = 0;
};
