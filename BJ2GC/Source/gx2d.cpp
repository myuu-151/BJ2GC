#include "gx2d.h"

#include <malloc.h>

#include <cmath>
#include <cstring>

#include "System/System.h"

namespace gx2d {

namespace {

constexpr uint8_t kFormat = GX_VTXFMT7;  // direct position (x, y) and uv
const char* const kRoot = "BJ2GC/Scripts/Data/";

uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

constexpr uint8_t kColouredFormat = GX_VTXFMT6;  // position, colour and uv (meshes)

Blend g_blend = Blend::Alpha;

void SetBlend(Blend b) {
    if (b == g_blend) return;
    g_blend = b;
    if (b == Blend::Add)
        GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_ONE, GX_LO_CLEAR);
    else
        GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
}

// One TEV stage: the texel (or white) times the colour register C0, or, for
// meshes, the texel times each vertex's colour.
enum class Mode { None, Plain, Textured, Coloured };
Mode g_mode = Mode::None;

void SetMode(Mode mode) {
    if (mode == g_mode) return;
    g_mode = mode;
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    if (mode == Mode::Coloured) GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    if (mode != Mode::Plain) {
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
        GX_SetNumTexGens(1);
        GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    } else {
        GX_SetNumTexGens(0);
    }
    if (mode == Mode::Coloured) {
        GX_SetNumChans(1);
        GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
        GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_RASC, GX_CC_ZERO);
        GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
    } else if (mode == Mode::Textured) {
        GX_SetNumChans(0);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
        GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_TEXC, GX_CC_C0, GX_CC_ZERO);
        GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_A0, GX_CA_ZERO);
    } else {
        GX_SetNumChans(0);
        GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLORNULL);
        GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
        GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
    }
    GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
}

void SetTextured(bool textured) { SetMode(textured ? Mode::Textured : Mode::Plain); }

}  // namespace

bool Load(Texture& t, const std::string& name, bool repeat) {
    Free(t);
    char* data = nullptr;
    uint32_t size = 0;
    SYS_AcquireFileData((std::string(kRoot) + name).c_str(), true, 0, data, size);
    if (!data) return false;
    const uint8_t* d = reinterpret_cast<const uint8_t*>(data);
    if (size < 32 || std::memcmp(d, "BJTX", 4) != 0) {
        SYS_ReleaseFileData(data);
        return false;
    }
    t.width = be16(d + 4);
    t.height = be16(d + 6);
    uint8_t format = uint8_t(be16(d + 8));
    t.frames_x = be16(d + 10);
    t.frames_y = be16(d + 12);
    t.frame_w = be16(d + 14);
    t.frame_h = be16(d + 16);
    uint32_t bytes = size - 32;
    t.texels = memalign(32, bytes);
    if (!t.texels) {
        SYS_ReleaseFileData(data);
        return false;
    }
    std::memcpy(t.texels, d + 32, bytes);
    SYS_ReleaseFileData(data);
    DCFlushRange(t.texels, bytes);
    const uint8_t wrap = repeat ? GX_REPEAT : GX_CLAMP;
    GX_InitTexObj(&t.obj, t.texels, uint16_t(t.width), uint16_t(t.height), format, wrap, wrap, GX_FALSE);
    GX_InitTexObjFilterMode(&t.obj, GX_LINEAR, GX_LINEAR);
    return true;
}

void Free(Texture& t) {
    if (t.texels) free(t.texels);
    t = Texture{};
}

void Begin(float fb_width, float fb_height) {
    GX_SetViewport(0.0f, 0.0f, fb_width, fb_height, 0, 1);
    GX_SetScissor(0, 0, uint32_t(fb_width), uint32_t(fb_height));
    Mtx44 projection;
    guOrtho(projection, 0.0f, 480.0f, 0.0f, 640.0f, -1.0f, 1.0f);
    GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
    Mtx identity;
    guMtxIdentity(identity);
    GX_LoadPosMtxImm(identity, GX_PNMTX0);
    ResetTransform();
    GX_SetCurrentMtx(GX_PNMTX0);
    GX_SetVtxAttrFmt(kFormat, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(kFormat, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetVtxAttrFmt(kColouredFormat, GX_VA_POS, GX_POS_XY, GX_F32, 0);
    GX_SetVtxAttrFmt(kColouredFormat, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxAttrFmt(kColouredFormat, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GX_SetCullMode(GX_CULL_NONE);
    GX_SetFog(GX_FOG_NONE, 0.0f, 1.0f, 0.1f, 1.0f, GXColor{0, 0, 0, 0});
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaUpdate(GX_FALSE);
    GX_SetNumChans(0);
    GX_SetNumTevStages(1);
    GX_SetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    // Force both to be set.
    g_mode = Mode::None;
    SetTextured(true);
    g_blend = Blend::Add;
    SetBlend(Blend::Alpha);
}

void DrawPart(const Texture& t, float sx, float sy, float sw, float sh, float x, float y, float w, float h,
              GXColor tint, Blend blend) {
    if (!t.Loaded()) return;
    SetTextured(true);
    SetBlend(blend);
    GX_SetTevColor(GX_TEVREG0, tint);
    GX_LoadTexObj(const_cast<GXTexObj*>(&t.obj), GX_TEXMAP0);
    float u0 = sx / float(t.width), v0 = sy / float(t.height);
    float u1 = (sx + sw) / float(t.width), v1 = (sy + sh) / float(t.height);
    GX_Begin(GX_QUADS, kFormat, 4);
    GX_Position2f32(x, y);
    GX_TexCoord2f32(u0, v0);
    GX_Position2f32(x + w, y);
    GX_TexCoord2f32(u1, v0);
    GX_Position2f32(x + w, y + h);
    GX_TexCoord2f32(u1, v1);
    GX_Position2f32(x, y + h);
    GX_TexCoord2f32(u0, v1);
    GX_End();
}

void Draw(const Texture& t, int frame, float x, float y, float w, float h, GXColor tint, Blend blend) {
    int fw = t.frame_w ? t.frame_w : t.width, fh = t.frame_h ? t.frame_h : t.height;
    int count = t.frames_x * t.frames_y;
    frame = count > 0 ? ((frame % count) + count) % count : 0;
    DrawPart(t, float((frame % t.frames_x) * fw), float((frame / t.frames_x) * fh), float(fw), float(fh), x, y, w, h,
             tint, blend);
}

bool Font::Load(const std::string& name) {
    char* data = nullptr;
    uint32_t size = 0;
    SYS_AcquireFileData((std::string(kRoot) + name + ".fnt").c_str(), true, 0, data, size);
    if (!data) return false;
    const uint8_t* d = reinterpret_cast<const uint8_t*>(data);
    bool ok = size >= 8 && std::memcmp(d, "BJFN", 4) == 0;
    if (ok) {
        ascent_ = be16(d + 4);
        int count = be16(d + 6);
        for (int i = 0; i < count && 8 + 16 * (i + 1) <= int(size); i++) {
            const uint8_t* g = d + 8 + 16 * i;
            uint16_t c = be16(g);
            if (c >= 128) continue;
            Glyph& gl = glyphs_[c];
            gl.advance = int16_t(be16(g + 2));
            gl.x = be16(g + 4);
            gl.y = be16(g + 6);
            gl.w = be16(g + 8);
            gl.h = be16(g + 10);
            gl.ox = int16_t(be16(g + 12));
            gl.oy = int16_t(be16(g + 14));
            gl.present = true;
        }
    }
    SYS_ReleaseFileData(data);
    return ok && gx2d::Load(texture_, name + ".tex");
}

float Font::Width(const std::string& text, float scale) const {
    float w = 0, line = 0;
    for (char ch : text) {
        if (ch == '\n') {
            w = line > w ? line : w;
            line = 0;
            continue;
        }
        unsigned char c = (unsigned char)ch;
        if (c < 128 && glyphs_[c].present) line += glyphs_[c].advance * scale;
    }
    return line > w ? line : w;
}

void Font::Draw(const std::string& text, float x, float y, float scale, GXColor tint, int align) const {
    if (!texture_.Loaded()) return;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        float pen = x;
        if (align == 0) pen -= Width(line, scale) / 2;
        if (align > 0) pen -= Width(line, scale);
        for (char ch : line) {
            unsigned char c = (unsigned char)ch;
            if (c >= 128 || !glyphs_[c].present) continue;
            const Glyph& g = glyphs_[c];
            if (g.w && g.h) {
                // The glyph's part of the texture.
                float u0 = float(g.x) / texture_.width, v0 = float(g.y) / texture_.height;
                float u1 = float(g.x + g.w) / texture_.width, v1 = float(g.y + g.h) / texture_.height;
                float gx = pen + g.ox * scale, gy = y + g.oy * scale, gw = g.w * scale, gh = g.h * scale;
                if (scale == 1.0f) {
                    // At its own size, texel to pixel: sharp, not smeared between two.
                    gx = std::floor(gx + 0.5f);
                    gy = std::floor(gy + 0.5f);
                }
                SetTextured(true);
                SetBlend(Blend::Alpha);
                GX_SetTevColor(GX_TEVREG0, tint);
                GX_LoadTexObj(const_cast<GXTexObj*>(&texture_.obj), GX_TEXMAP0);
                GX_Begin(GX_QUADS, kFormat, 4);
                GX_Position2f32(gx, gy);
                GX_TexCoord2f32(u0, v0);
                GX_Position2f32(gx + gw, gy);
                GX_TexCoord2f32(u1, v0);
                GX_Position2f32(gx + gw, gy + gh);
                GX_TexCoord2f32(u1, v1);
                GX_Position2f32(gx, gy + gh);
                GX_TexCoord2f32(u0, v1);
                GX_End();
            }
            pen += g.advance * scale;
        }
        y += LineHeight(scale);
        start = end + 1;
    }
}

namespace {
float g_m[6] = {1, 0, 0, 1, 0, 0};  // the transform now: a, b, c, d, tx, ty
}  // namespace

void SetTransform(float a, float b, float c, float d, float tx, float ty) {
    g_m[0] = a, g_m[1] = b, g_m[2] = c, g_m[3] = d, g_m[4] = tx, g_m[5] = ty;
    Mtx m = {{a, b, 0, tx}, {c, d, 0, ty}, {0, 0, 1, 0}};
    GX_LoadPosMtxImm(m, GX_PNMTX0);
}

void ResetTransform() { SetTransform(1, 0, 0, 1, 0, 0); }

void DrawRotated(const Texture& t, int frame, float cx, float cy, float w, float h, float angle, GXColor tint,
                 Blend blend) {
    // About its centre: turned, then moved, then through the transform now.
    const float c = std::cos(angle), s = std::sin(angle);
    const float m[6] = {g_m[0], g_m[1], g_m[2], g_m[3], g_m[4], g_m[5]};
    SetTransform(m[0] * c + m[1] * s, -m[0] * s + m[1] * c, m[2] * c + m[3] * s, -m[2] * s + m[3] * c,
                 m[0] * cx + m[1] * cy + m[4], m[2] * cx + m[3] * cy + m[5]);
    Draw(t, frame, -w / 2, -h / 2, w, h, tint, blend);
    SetTransform(m[0], m[1], m[2], m[3], m[4], m[5]);
}

void DrawMesh(const Texture& t, const Vertex* v, int count, Blend blend, uint8_t primitive) {
    if (!t.Loaded() || count <= 0) return;
    SetMode(Mode::Coloured);
    SetBlend(blend);
    GX_LoadTexObj(const_cast<GXTexObj*>(&t.obj), GX_TEXMAP0);
    GX_Begin(primitive, kColouredFormat, uint16_t(count));
    for (int i = 0; i < count; i++) {
        GX_Position2f32(v[i].x, v[i].y);
        GX_Color4u8(v[i].c.r, v[i].c.g, v[i].c.b, v[i].c.a);
        GX_TexCoord2f32(v[i].u, v[i].v);
    }
    GX_End();
}

void Rect(float x, float y, float w, float h, GXColor color) {
    SetTextured(false);
    SetBlend(Blend::Alpha);
    GX_SetTevColor(GX_TEVREG0, color);
    GX_Begin(GX_QUADS, kFormat, 4);
    GX_Position2f32(x, y);
    GX_Position2f32(x + w, y);
    GX_Position2f32(x + w, y + h);
    GX_Position2f32(x, y + h);
    GX_End();
}

}  // namespace gx2d
