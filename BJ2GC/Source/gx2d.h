// 2D drawing on GX in 640x480 screen coordinates: textures from the data's
// .tex files (tools/make_data.py) and quads of them, tinted, blended or added.
#pragma once

#include <gccore.h>

#include <cstdint>
#include <string>

namespace gx2d {

struct Texture {
    GXTexObj obj;
    void* texels = nullptr;
    int width = 0, height = 0;             // the texture's (a multiple of 4)
    int frames_x = 1, frames_y = 1;        // an animation's frames in a grid
    int frame_w = 0, frame_h = 0;
    bool Loaded() const { return texels != nullptr; }
};

// A .tex file under the data folder (BJ2GC/Scripts/Data/NAME); `repeat`
// tiles it (its sides a power of two).
bool Load(Texture& t, const std::string& name, bool repeat = false);
void Free(Texture& t);

// Before drawing: the projection, vertex format and blending for 2D over a
// framebuffer of `fb_width` x `fb_height`.
void Begin(float fb_width, float fb_height);

enum class Blend { Alpha, Add };

// Frame `frame` of `t` (0 for a single picture) over the rectangle x, y,
// w, h, its colours and alpha multiplied by `tint` (0-255 each).
void Draw(const Texture& t, int frame, float x, float y, float w, float h, GXColor tint = {255, 255, 255, 255},
          Blend blend = Blend::Alpha);
// The part sx, sy, sw, sh of `t` (in its pixels) over the rectangle x, y, w, h.
void DrawPart(const Texture& t, float sx, float sy, float sw, float sh, float x, float y, float w, float h,
              GXColor tint = {255, 255, 255, 255}, Blend blend = Blend::Alpha);
// Frame `frame` of `t`, w x h, centred on cx, cy and turned `angle` radians
// (clockwise on the screen).
void DrawRotated(const Texture& t, int frame, float cx, float cy, float w, float h, float angle,
                 GXColor tint = {255, 255, 255, 255}, Blend blend = Blend::Alpha);

// What's drawn next goes through x' = a x + b y + tx, y' = c x + d y + ty.
void SetTransform(float a, float b, float c, float d, float tx, float ty);
void ResetTransform();

// Triangles or quads of `t`, each vertex with its own colour.
struct Vertex {
    float x, y, u, v;
    GXColor c;
};
void DrawMesh(const Texture& t, const Vertex* v, int count, Blend blend = Blend::Alpha,
              uint8_t primitive = GX_QUADS);

// A plain rectangle.
void Rect(float x, float y, float w, float h, GXColor color);

// One of the game's own bitmap fonts (NAME.fnt and NAME.tex, from its
// data/*.txt by tools/make_data.py), laid out as the framework's ImageFont.
class Font {
public:
    bool Load(const std::string& name);
    // Pixels across at `scale`.
    float Width(const std::string& text, float scale = 1.0f) const;
    // `text` with its top at y (lines split at '\n'); x is its left, centre
    // or right as `align` is -1, 0 or 1.
    void Draw(const std::string& text, float x, float y, float scale = 1.0f, GXColor tint = {255, 255, 255, 255},
              int align = -1) const;
    float LineHeight(float scale = 1.0f) const { return float(ascent_ + 4) * scale; }
    int Ascent() const { return ascent_; }

private:
    struct Glyph {
        int16_t advance = 0;
        uint16_t x = 0, y = 0, w = 0, h = 0;
        int16_t ox = 0, oy = 0;
        bool present = false;
    };
    Glyph glyphs_[128];
    int ascent_ = 0;
    Texture texture_;
};

}  // namespace gx2d
