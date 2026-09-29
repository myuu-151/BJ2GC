#include "fx_gc.h"

#include <algorithm>
#include <cmath>

namespace {

constexpr float kS = 0.625f;  // the board's units to the screen's

}  // namespace

void BigText::Show(const std::string& text, int duration, const gx2d::Font& font) {
    text_ = text;
    // Drawn into a buffer 16 wider than the words (at most 800) and 110 high.
    box_w_ = std::min(font.Width(text) + 16, 800.0f);
    duration_ = duration;
    age_ = 0;
    on_ = true;
    rising_ = true;
    alpha_ = 0;
    scale_ = 1;
    scale_v_ = 0.025f;
    wobble_ = 1;
    wobble_v_ = 0;
    firm_ = 0;
}

void BigText::Update() {
    if (!on_) return;
    age_++;
    // In for duration + 90 updates, then out.
    if (rising_) {
        alpha_ = std::min(1.0f, alpha_ + 0.02f);
        if (age_ >= duration_ + 90) rising_ = false;
    } else {
        alpha_ -= 0.03f;
        if (alpha_ <= 0) on_ = false;
    }
    // Its size: still for 30, then springs (undamped) to 1.2 for 60, to
    // 1.75 for duration + 60, then to nothing.
    if (age_ > 30) {
        float target = 1.2f, stiffness = 0.0015f;
        if (age_ > 90 + duration_ + 60) {
            target = 0;
            stiffness = 0.002f;
        } else if (age_ > 90) {
            target = 1.75f;
        }
        scale_v_ += (target - scale_) * stiffness;
        scale_ += scale_v_;
    }
    // Its shape: from a wide line, wobbling to its own proportions.
    firm_ = std::min(1.0f, firm_ + 0.04f);
    wobble_v_ = (0.975f - 0.12f * firm_) * (wobble_v_ - wobble_ * (0.01f * firm_ + 0.002f));
    wobble_ += wobble_v_;
}

void BigText::Draw(const gx2d::Font& font) const {
    if (!on_ || alpha_ <= 0 || scale_ <= 0) return;
    float w = (box_w_ + 4 * box_w_ * wobble_) * 0.73f * scale_;
    float h = std::max(6.0f, (110 - 198 * wobble_) * 0.73f * scale_);
    if (w <= 0) return;
    // The buffer (the words at 8, baseline 94) stretched over w x h.
    float sx = w / box_w_, sy = h / 110;
    gx2d::SetTransform(sx * kS, 0, 0, sy * kS, (654 - w / 2) * kS, (245 - h / 2) * kS);
    font.Draw(text_, 8, float(94 - font.Ascent()), 1.0f,
              GXColor{255, 255, 255, uint8_t(std::min(1.0f, alpha_) * 255)});
    gx2d::ResetTransform();
}
