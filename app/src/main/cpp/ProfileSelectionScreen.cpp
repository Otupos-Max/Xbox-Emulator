#include "ProfileSelectionScreen.hpp"

#include <android/asset_manager.h>
#include <android/keycodes.h>
#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace xenon {
namespace {

constexpr const char* kLogTag = "Xenon";

struct Vertex {
    float position[3];
    float normal[3];
    float texcoord[2];
};

struct TextVertex {
    float position[2];
    float texcoord[2];
};

struct Color {
    float r;
    float g;
    float b;
    float a;
};

constexpr Color kText{0.10f, 0.11f, 0.12f, 1.f};
constexpr Color kWhite{0.98f, 0.98f, 0.98f, 1.f};
constexpr Color kGreen{0.20f, 0.68f, 0.08f, 1.f};
constexpr Color kGreenBright{0.42f, 0.95f, 0.22f, 1.f};
constexpr Color kOnline{0.38f, 0.86f, 0.10f, 1.f};
constexpr Color kOffline{0.42f, 0.43f, 0.45f, 1.f};
constexpr Color kRed{0.80f, 0.10f, 0.12f, 1.f};
constexpr Color kCardTop{0.62f, 0.63f, 0.66f, 0.92f};
constexpr Color kCardBottom{0.90f, 0.91f, 0.92f, 0.94f};
constexpr Color kSilhouette{0.86f, 0.87f, 0.89f, 1.f};

struct CardLayout {
    float startX;
    float top;
    float width;
    float height;
    float gap;
};

CardLayout cardLayout(float width, float height) {
    CardLayout layout{};
    layout.gap = 14.f;
    layout.startX = width * .385f;
    layout.width = (width * .59f - layout.gap * 3.f) / 4.f;
    layout.top = height * .27f;
    layout.height = height * .40f;
    return layout;
}

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) return shader;

    char log[512] = {};
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                        "Profile shader compilation failed: %s", log);
    glDeleteShader(shader);
    return 0;
}

GLuint linkProgram(const char* vertexSource, const char* fragmentSource) {
    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertex == 0 || fragment == 0) {
        if (vertex != 0) glDeleteShader(vertex);
        if (fragment != 0) glDeleteShader(fragment);
        return 0;
    }

    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glBindAttribLocation(program, 0, "position");
    glBindAttribLocation(program, 1, "normal");
    glBindAttribLocation(program, 2, "texcoord");
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);

    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE) return program;

    char log[512] = {};
    glGetProgramInfoLog(program, sizeof(log), nullptr, log);
    __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                        "Profile shader link failed: %s", log);
    glDeleteProgram(program);
    return 0;
}

void drawRect(const ProfileDrawApi& api, float x, float y, float width, float height,
              const Color& color) {
    api.rect(api.context, x, y, width, height,
             color.r, color.g, color.b, color.a);
}

// Filled rounded rectangle built from horizontal strips (the host only draws plain rects).
void drawRoundedRect(const ProfileDrawApi& api, float x, float y, float w, float h,
                     float radius, const Color& color) {
    radius = std::min(radius, std::min(w, h) * .5f);
    if (radius < 1.f) {
        drawRect(api, x, y, w, h, color);
        return;
    }
    drawRect(api, x, y + radius, w, h - radius * 2.f, color);
    const int steps = std::max(3, static_cast<int>(radius));
    const float stepH = radius / steps;
    for (int i = 0; i < steps; ++i) {
        const float dy = radius - (i + .5f) * stepH;
        const float inset = radius - std::sqrt(std::max(0.f, radius * radius - dy * dy));
        drawRect(api, x + inset, y + i * stepH, w - inset * 2.f, stepH + .35f, color);
        drawRect(api, x + inset, y + h - (i + 1) * stepH - .35f, w - inset * 2.f,
                 stepH + .35f, color);
    }
}

// Rounded rect whose top part uses one color and bottom part another.
void drawSplitRoundedRect(const ProfileDrawApi& api, float x, float y, float w, float h,
                          float radius, float split, const Color& top, const Color& bottom) {
    radius = std::min(radius, std::min(w, h) * .5f);
    const int steps = std::max(3, static_cast<int>(radius));
    const float stepH = radius / steps;
    for (int i = 0; i < steps; ++i) {
        const float dy = radius - (i + .5f) * stepH;
        const float inset = radius - std::sqrt(std::max(0.f, radius * radius - dy * dy));
        drawRect(api, x + inset, y + i * stepH, w - inset * 2.f, stepH + .35f, top);
        drawRect(api, x + inset, y + h - (i + 1) * stepH - .35f, w - inset * 2.f,
                 stepH + .35f, bottom);
    }
    drawRect(api, x, y + radius, w, split - radius, top);
    drawRect(api, x, y + split, w, h - split - radius, bottom);
}

void drawRoundedOutline(const ProfileDrawApi& api, float x, float y, float w, float h,
                        float radius, float thickness, const Color& color) {
    const int steps = std::max(4, static_cast<int>(radius));
    const float stepH = radius / steps;
    drawRect(api, x + radius, y, w - radius * 2.f, thickness, color);
    drawRect(api, x + radius, y + h - thickness, w - radius * 2.f, thickness, color);
    drawRect(api, x, y + radius, thickness, h - radius * 2.f, color);
    drawRect(api, x + w - thickness, y + radius, thickness, h - radius * 2.f, color);
    for (int i = 0; i < steps; ++i) {
        const float dy = radius - (i + .5f) * stepH;
        const float inset = radius - std::sqrt(std::max(0.f, radius * radius - dy * dy));
        const float nextDy = radius - (i + 1.5f) * stepH;
        const float nextInset = i + 1 < steps
            ? radius - std::sqrt(std::max(0.f, radius * radius - nextDy * nextDy)) : 0.f;
        const float segW = std::max(thickness, inset - nextInset + thickness);
        for (int side = 0; side < 2; ++side) {
            const float rowY = side == 0 ? y + i * stepH : y + h - (i + 1) * stepH;
            drawRect(api, x + inset, rowY, segW, stepH + .4f, color);
            drawRect(api, x + w - inset - segW, rowY, segW, stepH + .4f, color);
        }
        if (i == 0) {
            drawRect(api, x + inset, y, radius - inset, thickness, color);
            drawRect(api, x + w - radius, y, radius - inset, thickness, color);
            drawRect(api, x + inset, y + h - thickness, radius - inset, thickness, color);
            drawRect(api, x + w - radius, y + h - thickness, radius - inset, thickness, color);
        }
    }
}

void drawCircle(const ProfileDrawApi& api, float cx, float cy, float radius,
                const Color& color) {
    const int steps = std::max(6, static_cast<int>(radius * 1.5f));
    const float stepH = radius * 2.f / steps;
    for (int i = 0; i < steps; ++i) {
        const float dy = -radius + (i + .5f) * stepH;
        const float half = std::sqrt(std::max(0.f, radius * radius - dy * dy));
        drawRect(api, cx - half, cy - radius + i * stepH, half * 2.f, stepH + .3f, color);
    }
}

// Glossy status orb: base disc plus a soft highlight.
void drawOrb(const ProfileDrawApi& api, float cx, float cy, float radius, const Color& color) {
    drawCircle(api, cx, cy, radius + 1.f, {0.f, 0.f, 0.f, .18f});
    drawCircle(api, cx, cy, radius, color);
    drawCircle(api, cx - radius * .25f, cy - radius * .3f, radius * .45f,
               {1.f, 1.f, 1.f, .35f});
}

// Generic person silhouette used for "Criar perfil" and "Convidado".
void drawSilhouette(const ProfileDrawApi& api, float cx, float top, float areaH,
                    const Color& color) {
    const float headR = areaH * .17f;
    const float headCy = top + areaH * .30f;
    drawCircle(api, cx, headCy, headR, color);
    drawRect(api, cx - headR * .45f, headCy + headR * .8f, headR * .9f, headR * .6f, color);
    const float shoulderTop = headCy + headR * 1.25f;
    const float bodyW = headR * 4.f;
    drawRoundedRect(api, cx - bodyW * .5f, shoulderTop, bodyW, top + areaH - shoulderTop + 20.f,
                    headR * 1.5f, color);
}

uint32_t nextCodepoint(const char*& text) {
    const unsigned char first = static_cast<unsigned char>(*text++);
    if (first < 0x80u) return first;
    if ((first & 0xE0u) == 0xC0u && *text) {
        const unsigned char second = static_cast<unsigned char>(*text++);
        return ((first & 0x1Fu) << 6) | (second & 0x3Fu);
    }
    if ((first & 0xF0u) == 0xE0u && text[0] && text[1]) {
        const unsigned char second = static_cast<unsigned char>(*text++);
        const unsigned char third = static_cast<unsigned char>(*text++);
        return ((first & 0x0Fu) << 12) | ((second & 0x3Fu) << 6) | (third & 0x3Fu);
    }
    if ((first & 0xF8u) == 0xF0u && text[0] && text[1] && text[2]) {
        const unsigned char second = static_cast<unsigned char>(*text++);
        const unsigned char third = static_cast<unsigned char>(*text++);
        const unsigned char fourth = static_cast<unsigned char>(*text++);
        return ((first & 0x07u) << 18) | ((second & 0x3Fu) << 12) |
               ((third & 0x3Fu) << 6) | (fourth & 0x3Fu);
    }
    return '?';
}

} // namespace

bool ProfileSelectionScreen::initialize(AAssetManager* assets,
                                        const std::string& storagePath) {
    storagePath_ = storagePath;
    loadSelection();
    if (!createPrograms()) return false;
    if (!loadFont(assets)) return false;
    if (!loadModel(assets, "profile_models/male.xmesh", male_)) return false;
    if (!loadModel(assets, "profile_models/female.xmesh", female_)) return false;
    return true;
}

void ProfileSelectionScreen::shutdown() {
    for (const Model::Batch& batch : male_.batches) {
        if (batch.vertexBuffer != 0) glDeleteBuffers(1, &batch.vertexBuffer);
        if (batch.indexBuffer != 0) glDeleteBuffers(1, &batch.indexBuffer);
    }
    if (male_.texture != 0) glDeleteTextures(1, &male_.texture);
    for (const Model::Batch& batch : female_.batches) {
        if (batch.vertexBuffer != 0) glDeleteBuffers(1, &batch.vertexBuffer);
        if (batch.indexBuffer != 0) glDeleteBuffers(1, &batch.indexBuffer);
    }
    if (female_.texture != 0) glDeleteTextures(1, &female_.texture);
    if (backgroundBuffer_ != 0) glDeleteBuffers(1, &backgroundBuffer_);
    if (backgroundProgram_ != 0) glDeleteProgram(backgroundProgram_);
    if (modelProgram_ != 0) glDeleteProgram(modelProgram_);
    if (textBuffer_ != 0) glDeleteBuffers(1, &textBuffer_);
    if (fontTexture_ != 0) glDeleteTextures(1, &fontTexture_);
    if (textProgram_ != 0) glDeleteProgram(textProgram_);

    male_ = {};
    female_ = {};
    backgroundBuffer_ = 0;
    backgroundProgram_ = 0;
    modelProgram_ = 0;
    textBuffer_ = 0;
    fontTexture_ = 0;
    textProgram_ = 0;
    fontAtlasWidth_ = 0;
    fontAtlasHeight_ = 0;
    fontEmSize_ = 0;
    textBufferCapacity_ = 0;
    glyphs_.clear();
    hasPreviousFrame_ = false;
}

bool ProfileSelectionScreen::createPrograms() {
    static const char* backgroundVertex = R"(precision highp float;
attribute vec2 position;
varying vec2 uv;
void main() {
    uv = position;
    gl_Position = vec4(position * 2.0 - 1.0, 0.0, 1.0);
})";
    static const char* backgroundFragment = R"(precision mediump float;
varying vec2 uv;
uniform float time;
float ribbon(float y, float center, float thickness, float softness) {
    return smoothstep(thickness + softness, thickness, abs(y - center));
}
void main() {
    float y = 1.0 - uv.y;
    // Studio grey: darker ceiling, bright horizon, soft reflective floor.
    vec3 top = vec3(0.26, 0.27, 0.29);
    vec3 mid = vec3(0.80, 0.81, 0.83);
    vec3 floorColor = vec3(0.72, 0.73, 0.76);
    vec3 base = mix(top, mid, smoothstep(0.0, 0.62, y));
    base = mix(base, floorColor, smoothstep(0.62, 1.0, y) * 0.6);
    float spot = exp(-length((uv - vec2(0.25, 0.55)) * vec2(1.1, 1.4)) * 2.4);
    base += vec3(0.10) * spot;

    // Xbox-style green swooshes sweeping from the left edge to the lower right.
    float x = uv.x;
    float drift = sin(time * 0.12) * 0.012;
    float c1 = 0.40 + 0.30 * x - 0.20 * x * x + drift + 0.012 * sin(x * 5.0 + time * 0.2);
    float c2 = 0.52 + 0.22 * x - 0.08 * x * x - drift;
    float c3 = 0.63 + 0.12 * x + 0.04 * sin(x * 3.0 - time * 0.15);
    float w1 = 0.07 * (1.0 - x * 0.75);
    float w2 = 0.035 * (1.0 - x * 0.6);
    float w3 = 0.02 + 0.015 * x;
    vec3 green = vec3(0.30, 0.78, 0.10);
    vec3 lime = vec3(0.62, 0.95, 0.38);
    float r1 = ribbon(y, c1, w1, 0.015);
    float r2 = ribbon(y, c2, w2, 0.012);
    float r3 = ribbon(y, c3, w3, 0.010);
    float fade1 = 1.0 - smoothstep(0.55, 0.95, x) * 0.9;
    base = mix(base, mix(green, lime, smoothstep(c1 - w1, c1 + w1, y)), r1 * fade1);
    base = mix(base, mix(vec3(0.90, 0.95, 0.88), lime, 0.4), r2 * 0.85 * fade1);
    base = mix(base, mix(green, lime, 0.5), r3 * (0.35 + 0.65 * smoothstep(0.3, 1.0, x)));
    float glow = exp(-abs(y - c1) * 18.0) * fade1;
    base += vec3(0.05, 0.12, 0.02) * glow * 0.35;

    float vignette = smoothstep(0.35, 1.0, length((uv - vec2(0.5, 0.55)) * vec2(1.0, 1.2)));
    base *= 1.0 - vignette * 0.18;
    gl_FragColor = vec4(base, 1.0);
})";
    static const char* modelVertex = R"(precision highp float;
attribute vec3 position;
attribute vec3 normal;
attribute vec2 texcoord;
uniform vec2 viewport;
uniform vec2 center;
uniform float scale;
uniform float angle;
uniform float verticalScale;
varying vec3 surfaceNormal;
varying vec2 uv;
void main() {
    float facingAngle = angle + 3.14159265;
    float c = cos(facingAngle);
    float s = sin(facingAngle);
    vec3 p = vec3(c * position.x + s * position.z,
                  position.y * verticalScale,
                  -s * position.x + c * position.z);
    vec2 pixel = center + vec2(p.x, -p.y) * scale;
    vec2 clip = pixel / viewport * 2.0 - 1.0;
    gl_Position = vec4(clip.x, -clip.y, clamp(p.z * scale * 0.004, -0.8, 0.8), 1.0);
    surfaceNormal = normalize(vec3(c * normal.x + s * normal.z, normal.y, -s * normal.x + c * normal.z));
    uv = texcoord;
})";
    static const char* modelFragment = R"(precision mediump float;
uniform sampler2D modelTexture;
uniform float alpha;
varying vec3 surfaceNormal;
varying vec2 uv;
void main() {
    vec4 texel = texture2D(modelTexture, uv);
    vec3 n = normalize(surfaceNormal);
    vec3 light = normalize(vec3(-0.45, 0.72, -0.55));
    float diffuse = max(dot(n, light), 0.0);
    float rim = pow(1.0 - max(dot(n, vec3(0.0, 0.0, 1.0)), 0.0), 2.2);
    vec3 color = texel.rgb * (0.50 + diffuse * 0.72);
    color += vec3(0.06, 0.19, 0.10) * rim * 0.28;
    gl_FragColor = vec4(color, texel.a * alpha);
})";
    static const char* textVertex = R"(precision highp float;
attribute vec2 position;
attribute vec2 texcoord;
uniform vec2 viewport;
varying vec2 uv;
void main() {
    vec2 normalized = position / viewport * 2.0 - 1.0;
    gl_Position = vec4(normalized.x, -normalized.y, 0.0, 1.0);
    uv = texcoord;
})";
    static const char* textFragment = R"(precision mediump float;
uniform sampler2D atlas;
uniform vec4 color;
varying vec2 uv;
void main() {
    float coverage = texture2D(atlas, uv).r;
    gl_FragColor = vec4(color.rgb, color.a * coverage);
})";

    backgroundProgram_ = linkProgram(backgroundVertex, backgroundFragment);
    modelProgram_ = linkProgram(modelVertex, modelFragment);
    const GLuint textVs = compileShader(GL_VERTEX_SHADER, textVertex);
    const GLuint textFs = compileShader(GL_FRAGMENT_SHADER, textFragment);
    if (textVs != 0 && textFs != 0) {
        textProgram_ = glCreateProgram();
        glAttachShader(textProgram_, textVs);
        glAttachShader(textProgram_, textFs);
        glBindAttribLocation(textProgram_, 0, "position");
        glBindAttribLocation(textProgram_, 1, "texcoord");
        glLinkProgram(textProgram_);
        GLint linked = GL_FALSE;
        glGetProgramiv(textProgram_, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            char log[512] = {};
            glGetProgramInfoLog(textProgram_, sizeof(log), nullptr, log);
            __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                                "Profile font shader link failed: %s", log);
            glDeleteProgram(textProgram_);
            textProgram_ = 0;
        }
    }
    if (textVs != 0) glDeleteShader(textVs);
    if (textFs != 0) glDeleteShader(textFs);
    if (backgroundProgram_ == 0 || modelProgram_ == 0 || textProgram_ == 0) return false;

    backgroundPosition_ = glGetAttribLocation(backgroundProgram_, "position");
    backgroundTime_ = glGetUniformLocation(backgroundProgram_, "time");
    modelPosition_ = glGetAttribLocation(modelProgram_, "position");
    modelNormal_ = glGetAttribLocation(modelProgram_, "normal");
    modelTexcoord_ = glGetAttribLocation(modelProgram_, "texcoord");
    modelViewport_ = glGetUniformLocation(modelProgram_, "viewport");
    modelCenter_ = glGetUniformLocation(modelProgram_, "center");
    modelScale_ = glGetUniformLocation(modelProgram_, "scale");
    modelAngle_ = glGetUniformLocation(modelProgram_, "angle");
    modelAlpha_ = glGetUniformLocation(modelProgram_, "alpha");
    modelVerticalScale_ = glGetUniformLocation(modelProgram_, "verticalScale");
    modelTexture_ = glGetUniformLocation(modelProgram_, "modelTexture");
    textPosition_ = glGetAttribLocation(textProgram_, "position");
    textTexcoord_ = glGetAttribLocation(textProgram_, "texcoord");
    textViewport_ = glGetUniformLocation(textProgram_, "viewport");
    textColor_ = glGetUniformLocation(textProgram_, "color");
    textTexture_ = glGetUniformLocation(textProgram_, "atlas");

    static const float quad[] = {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 1.f, 1.f};
    glGenBuffers(1, &backgroundBuffer_);
    glGenBuffers(1, &textBuffer_);
    if (backgroundBuffer_ == 0 || textBuffer_ == 0) return false;
    glBindBuffer(GL_ARRAY_BUFFER, backgroundBuffer_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    if (glGetError() != GL_NO_ERROR) return false;
    return true;
}

bool ProfileSelectionScreen::loadFont(AAssetManager* assets) {
    AAsset* asset = assets
        ? AAssetManager_open(assets, "profile_models/profile_font.xfont", AASSET_MODE_BUFFER)
        : nullptr;
    if (!asset) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Profile font atlas asset is missing");
        return false;
    }

    const off64_t assetLength = AAsset_getLength64(asset);
    if (assetLength < 20) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Profile font atlas is truncated");
        AAsset_close(asset);
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(assetLength));
    size_t bytesRead = 0;
    while (bytesRead < bytes.size()) {
        const int result = AAsset_read(asset, bytes.data() + bytesRead, bytes.size() - bytesRead);
        if (result <= 0) break;
        bytesRead += static_cast<size_t>(result);
    }
    AAsset_close(asset);
    if (bytesRead != bytes.size() || std::memcmp(bytes.data(), "XFNT", 4) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Invalid profile font atlas");
        return false;
    }

    uint32_t glyphCount = 0;
    std::memcpy(&fontAtlasWidth_, bytes.data() + 4, sizeof(fontAtlasWidth_));
    std::memcpy(&fontAtlasHeight_, bytes.data() + 8, sizeof(fontAtlasHeight_));
    std::memcpy(&fontEmSize_, bytes.data() + 12, sizeof(fontEmSize_));
    std::memcpy(&glyphCount, bytes.data() + 16, sizeof(glyphCount));
    const uint64_t expectedLength = 20ull + static_cast<uint64_t>(glyphCount) * 20ull +
                                    static_cast<uint64_t>(fontAtlasWidth_) * fontAtlasHeight_;
    if (fontAtlasWidth_ == 0 || fontAtlasHeight_ == 0 || fontAtlasWidth_ > 2048 ||
        fontAtlasHeight_ > 2048 || fontEmSize_ == 0 || glyphCount == 0 ||
        glyphCount > 256 || expectedLength != bytes.size()) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                            "Unsupported or truncated profile font atlas");
        return false;
    }

    glyphs_.resize(glyphCount);
    size_t offset = 20;
    for (Glyph& glyph : glyphs_) {
        std::memcpy(&glyph.codepoint, bytes.data() + offset, sizeof(glyph.codepoint));
        std::memcpy(&glyph.x, bytes.data() + offset + 4, sizeof(glyph.x));
        std::memcpy(&glyph.y, bytes.data() + offset + 6, sizeof(glyph.y));
        std::memcpy(&glyph.width, bytes.data() + offset + 8, sizeof(glyph.width));
        std::memcpy(&glyph.height, bytes.data() + offset + 10, sizeof(glyph.height));
        std::memcpy(&glyph.bearingX, bytes.data() + offset + 12, sizeof(glyph.bearingX));
        std::memcpy(&glyph.bearingY, bytes.data() + offset + 14, sizeof(glyph.bearingY));
        std::memcpy(&glyph.advance, bytes.data() + offset + 16, sizeof(glyph.advance));
        if (glyph.x + glyph.width > fontAtlasWidth_ ||
            glyph.y + glyph.height > fontAtlasHeight_) {
            __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                                "Profile font glyph is outside the atlas");
            return false;
        }
        offset += 20;
    }

    glGenTextures(1, &fontTexture_);
    glBindTexture(GL_TEXTURE_2D, fontTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE,
                 static_cast<GLsizei>(fontAtlasWidth_), static_cast<GLsizei>(fontAtlasHeight_),
                 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                 bytes.data() + 20 + static_cast<size_t>(glyphCount) * 20);
    if (fontTexture_ == 0 || glGetError() != GL_NO_ERROR) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                            "Unable to upload the profile font atlas");
        return false;
    }
    return true;
}

bool ProfileSelectionScreen::loadModel(AAssetManager* assets, const char* assetName,
                                       Model& model) {
    AAsset* asset = assets ? AAssetManager_open(assets, assetName, AASSET_MODE_BUFFER) : nullptr;
    if (!asset) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                            "Unable to open profile model asset: %s", assetName);
        return false;
    }

    const off64_t assetLength = AAsset_getLength64(asset);
    if (assetLength < 24 || static_cast<uint64_t>(assetLength) >
                              static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                            "Invalid profile model size: %s", assetName);
        AAsset_close(asset);
        return false;
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(assetLength));
    size_t bytesRead = 0;
    while (bytesRead < bytes.size()) {
        const int result = AAsset_read(asset, bytes.data() + bytesRead, bytes.size() - bytesRead);
        if (result <= 0) break;
        bytesRead += static_cast<size_t>(result);
    }
    AAsset_close(asset);
    if (bytesRead != bytes.size() || std::memcmp(bytes.data(), "XMSH", 4) != 0) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                            "Invalid profile model header: %s", assetName);
        return false;
    }

    uint32_t version = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint32_t textureWidth = 0;
    uint32_t textureHeight = 0;
    std::memcpy(&version, bytes.data() + 4, sizeof(version));
    std::memcpy(&vertexCount, bytes.data() + 8, sizeof(vertexCount));
    std::memcpy(&indexCount, bytes.data() + 12, sizeof(indexCount));
    std::memcpy(&textureWidth, bytes.data() + 16, sizeof(textureWidth));
    std::memcpy(&textureHeight, bytes.data() + 20, sizeof(textureHeight));
    const uint64_t expectedLength = 24ull + static_cast<uint64_t>(vertexCount) * sizeof(Vertex)
                                  + static_cast<uint64_t>(indexCount) * sizeof(uint32_t)
                                  + static_cast<uint64_t>(textureWidth) * textureHeight * 4ull;
    if (version != 1 || vertexCount == 0 || indexCount == 0 || indexCount % 3 != 0 ||
        vertexCount > 1000000 || indexCount > 3000000 || textureWidth == 0 ||
        textureHeight == 0 || textureWidth > 4096 || textureHeight > 4096 ||
        expectedLength != bytes.size()) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                            "Unsupported or truncated profile model: %s", assetName);
        return false;
    }

    const size_t vertexBytes = static_cast<size_t>(vertexCount) * sizeof(Vertex);
    const size_t indexOffset = 24 + vertexBytes;
    const size_t textureOffset = indexOffset + static_cast<size_t>(indexCount) * sizeof(uint32_t);
    std::vector<Vertex> vertices(vertexCount);
    const uint8_t* positions = bytes.data() + 24;
    const size_t positionBytes = static_cast<size_t>(vertexCount) * 3 * sizeof(float);
    for (uint32_t i = 0; i < vertexCount; ++i) {
        std::memcpy(vertices[i].position,
                    positions + static_cast<size_t>(i) * 3 * sizeof(float),
                    3 * sizeof(float));
    }
    const uint8_t* normals = positions + positionBytes;
    for (uint32_t i = 0; i < vertexCount; ++i) {
        std::memcpy(vertices[i].normal, normals + static_cast<size_t>(i) * 3 * sizeof(float),
                    3 * sizeof(float));
    }
    const uint8_t* texcoords = normals + positionBytes;
    for (uint32_t i = 0; i < vertexCount; ++i) {
        std::memcpy(vertices[i].texcoord, texcoords + static_cast<size_t>(i) * 2 * sizeof(float),
                    2 * sizeof(float));
    }

    const size_t maxBatchVertices = std::numeric_limits<uint16_t>::max();
    std::vector<Vertex> batchVertices;
    std::vector<uint16_t> batchIndices;
    batchVertices.reserve(maxBatchVertices);
    batchIndices.reserve(maxBatchVertices);
    auto uploadBatch = [&] {
        if (batchIndices.empty()) return;
        Model::Batch batch;
        batch.indexCount = static_cast<GLsizei>(batchIndices.size());
        glGenBuffers(1, &batch.vertexBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, batch.vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(batchVertices.size() * sizeof(Vertex)),
                     batchVertices.data(), GL_STATIC_DRAW);
        glGenBuffers(1, &batch.indexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, batch.indexBuffer);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(batchIndices.size() * sizeof(uint16_t)),
                     batchIndices.data(), GL_STATIC_DRAW);
        model.batches.push_back(batch);
        batchVertices.clear();
        batchIndices.clear();
    };

    for (uint32_t i = 0; i < indexCount; i += 3) {
        if (batchVertices.size() + 3 > maxBatchVertices) uploadBatch();
        for (uint32_t corner = 0; corner < 3; ++corner) {
            uint32_t sourceIndex = 0;
            const size_t offset = indexOffset +
                static_cast<size_t>(i + corner) * sizeof(sourceIndex);
            std::memcpy(&sourceIndex, bytes.data() + offset, sizeof(sourceIndex));
            if (sourceIndex >= vertexCount) {
                __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                                    "Invalid profile model index: %s", assetName);
                return false;
            }
            batchVertices.push_back(vertices[sourceIndex]);
            batchIndices.push_back(static_cast<uint16_t>(batchVertices.size() - 1));
        }
    }
    uploadBatch();

    glGenTextures(1, &model.texture);
    glBindTexture(GL_TEXTURE_2D, model.texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(textureWidth),
                 static_cast<GLsizei>(textureHeight), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 bytes.data() + textureOffset);
    glGenerateMipmap(GL_TEXTURE_2D);
    if (glGetError() != GL_NO_ERROR) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                            "OpenGL could not upload profile model: %s", assetName);
        return false;
    }
    return true;
}

void ProfileSelectionScreen::drawBackground(float elapsedSeconds) {
    if (backgroundProgram_ == 0 || backgroundBuffer_ == 0) return;
    glDisable(GL_DEPTH_TEST);
    glUseProgram(backgroundProgram_);
    glUniform1f(backgroundTime_, elapsedSeconds);
    glBindBuffer(GL_ARRAY_BUFFER, backgroundBuffer_);
    glVertexAttribPointer(static_cast<GLuint>(backgroundPosition_), 2, GL_FLOAT, GL_FALSE,
                          0, nullptr);
    glEnableVertexAttribArray(static_cast<GLuint>(backgroundPosition_));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void ProfileSelectionScreen::drawModel(const Model& model, float centerX, float centerY,
                                       float scale, float angle, float alpha,
                                       float verticalScale) {
    if (modelProgram_ == 0 || model.batches.empty() || model.texture == 0) return;
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glUseProgram(modelProgram_);
    glUniform2f(modelViewport_, 1280.f, 720.f);
    glUniform2f(modelCenter_, centerX, centerY);
    glUniform1f(modelScale_, scale);
    glUniform1f(modelAngle_, angle);
    glUniform1f(modelAlpha_, alpha);
    glUniform1f(modelVerticalScale_, verticalScale);
    glUniform1i(modelTexture_, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, model.texture);
    glEnableVertexAttribArray(static_cast<GLuint>(modelPosition_));
    glEnableVertexAttribArray(static_cast<GLuint>(modelNormal_));
    glEnableVertexAttribArray(static_cast<GLuint>(modelTexcoord_));
    for (const Model::Batch& batch : model.batches) {
        glBindBuffer(GL_ARRAY_BUFFER, batch.vertexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, batch.indexBuffer);
        glVertexAttribPointer(static_cast<GLuint>(modelPosition_), 3, GL_FLOAT, GL_FALSE,
                              sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, position)));
        glVertexAttribPointer(static_cast<GLuint>(modelNormal_), 3, GL_FLOAT, GL_FALSE,
                              sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, normal)));
        glVertexAttribPointer(static_cast<GLuint>(modelTexcoord_), 2, GL_FLOAT, GL_FALSE,
                              sizeof(Vertex), reinterpret_cast<const void*>(offsetof(Vertex, texcoord)));
        glDrawElements(GL_TRIANGLES, batch.indexCount, GL_UNSIGNED_SHORT, nullptr);
    }
}

void ProfileSelectionScreen::drawText(const char* text, float x, float y, float scale,
                                      float r, float g, float b, float a) {
    if (!text || textProgram_ == 0 || fontTexture_ == 0 || fontEmSize_ == 0) return;
    const float pixelScale = scale * 5.f / static_cast<float>(fontEmSize_);
    const float baseline = y + pixelScale * 47.f;
    float penX = x;
    std::vector<TextVertex> vertices;
    vertices.reserve(std::strlen(text) * 6);

    while (*text) {
        const uint32_t codepoint = nextCodepoint(text);
        auto glyph = std::find_if(glyphs_.begin(), glyphs_.end(),
                                  [codepoint](const Glyph& candidate) {
                                      return candidate.codepoint == codepoint;
                                  });
        if (glyph == glyphs_.end()) {
            glyph = std::find_if(glyphs_.begin(), glyphs_.end(),
                                 [](const Glyph& candidate) {
                                     return candidate.codepoint == '?';
                                 });
        }
        if (glyph == glyphs_.end()) continue;

        if (glyph->width > 0 && glyph->height > 0) {
            const float left = penX + glyph->bearingX * pixelScale;
            const float top = baseline + glyph->bearingY * pixelScale;
            const float right = left + glyph->width * pixelScale;
            const float bottom = top + glyph->height * pixelScale;
            const float u0 = static_cast<float>(glyph->x) / fontAtlasWidth_;
            const float v0 = static_cast<float>(glyph->y) / fontAtlasHeight_;
            const float u1 = static_cast<float>(glyph->x + glyph->width) / fontAtlasWidth_;
            const float v1 = static_cast<float>(glyph->y + glyph->height) / fontAtlasHeight_;
            vertices.insert(vertices.end(), {
                {{left, top}, {u0, v0}}, {{right, top}, {u1, v0}},
                {{left, bottom}, {u0, v1}},
                {{left, bottom}, {u0, v1}}, {{right, top}, {u1, v0}},
                {{right, bottom}, {u1, v1}}
            });
        }
        penX += glyph->advance * pixelScale;
    }

    if (vertices.empty()) return;
    glDisable(GL_DEPTH_TEST);
    glUseProgram(textProgram_);
    glUniform2f(textViewport_, 1280.f, 720.f);
    glUniform4f(textColor_, r, g, b, a);
    glUniform1i(textTexture_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, fontTexture_);
    glBindBuffer(GL_ARRAY_BUFFER, textBuffer_);
    const size_t byteSize = vertices.size() * sizeof(TextVertex);
    if (byteSize > textBufferCapacity_) {
        textBufferCapacity_ = byteSize;
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(textBufferCapacity_),
                     nullptr, GL_STREAM_DRAW);
    }
    glBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(byteSize), vertices.data());
    glVertexAttribPointer(static_cast<GLuint>(textPosition_), 2, GL_FLOAT, GL_FALSE,
                          sizeof(TextVertex), nullptr);
    glVertexAttribPointer(static_cast<GLuint>(textTexcoord_), 2, GL_FLOAT, GL_FALSE,
                          sizeof(TextVertex), reinterpret_cast<const void*>(2 * sizeof(float)));
    glEnableVertexAttribArray(static_cast<GLuint>(textPosition_));
    glEnableVertexAttribArray(static_cast<GLuint>(textTexcoord_));
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
}

float ProfileSelectionScreen::textWidth(const char* text, float scale) const {
    if (!text || fontEmSize_ == 0) return 0.f;
    const float pixelScale = scale * 5.f / static_cast<float>(fontEmSize_);
    float width = 0.f;
    while (*text) {
        const uint32_t codepoint = nextCodepoint(text);
        auto glyph = std::find_if(glyphs_.begin(), glyphs_.end(),
                                  [codepoint](const Glyph& candidate) {
                                      return candidate.codepoint == codepoint;
                                  });
        if (glyph != glyphs_.end()) width += glyph->advance * pixelScale;
    }
    return width;
}

void ProfileSelectionScreen::drawPortrait(const Model& model, float x, float y, float w,
                                          float h, float elapsedSeconds) {
    // Clip the model to the portrait area so only head and shoulders are visible.
    GLint viewport[4] = {0, 0, 1280, 720};
    glGetIntegerv(GL_VIEWPORT, viewport);
    const float sx = viewport[2] / 1280.f;
    const float sy = viewport[3] / 720.f;
    glEnable(GL_SCISSOR_TEST);
    glScissor(viewport[0] + static_cast<GLint>(x * sx),
              viewport[1] + static_cast<GLint>((720.f - (y + h)) * sy),
              static_cast<GLsizei>(w * sx), static_cast<GLsizei>(h * sy));
    glClear(GL_DEPTH_BUFFER_BIT);
    const float scale = h * 1.45f;
    const float yaw = std::sin(elapsedSeconds * .18f) * .02f;
    drawModel(model, x + w * .5f, y + scale * .93f, scale, yaw, 1.f, 1.f);
    glDisable(GL_SCISSOR_TEST);
}

void ProfileSelectionScreen::draw(float width, float height, float elapsedSeconds,
                                  const ProfileDrawApi& api) {
    drawBackground(elapsedSeconds);

    float delta = hasPreviousFrame_ ? elapsedSeconds - previousFrameTime_ : 1.f / 60.f;
    delta = std::clamp(delta, 0.f, 0.08f);
    previousFrameTime_ = elapsedSeconds;
    hasPreviousFrame_ = true;
    const float blendAmount = 1.f - std::exp(-delta * 9.f);
    for (int i = 0; i < 4; ++i) {
        const float target = i == selectedProfile_ ? 1.f : 0.f;
        selectionBlend_[i] += (target - selectionBlend_[i]) * blendAmount;
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);

    // Large hero avatar on the left, standing on the floor.
    glDisable(GL_DEPTH_TEST);
    const float floorLine = height - 42.f;
    const float heroCenterX = width * .235f;
    drawRect(api, heroCenterX - 150.f, floorLine - 8.f, 300.f, 14.f, {0.f, 0.f, 0.f, .10f});
    drawRect(api, heroCenterX - 100.f, floorLine - 5.f, 200.f, 8.f, {0.f, 0.f, 0.f, .12f});
    glEnable(GL_DEPTH_TEST);
    glClear(GL_DEPTH_BUFFER_BIT);
    const Model& heroModel = selectedProfile_ == 1 ? female_ : male_;
    const float modelScale = std::min(height * .45f, 330.f);
    const float modelCenterY = floorLine - modelScale * .995f;
    const float idleYaw = std::sin(elapsedSeconds * .22f) * .012f;
    drawModel(heroModel, heroCenterX, modelCenterY, modelScale, idleYaw, 1.f, 1.f);
    glDisable(GL_DEPTH_TEST);

    // Right-aligned title block.
    const float rightEdge = width - 48.f;
    const char* title = "Escolha um perfil";
    const char* subtitle = "Faça login para continuar";
    drawText(title, rightEdge - textWidth(title, 6.6f), height * .075f, 6.6f,
             kWhite.r, kWhite.g, kWhite.b, kWhite.a);
    drawText(subtitle, rightEdge - textWidth(subtitle, 3.5f), height * .165f, 3.5f,
             kWhite.r, kWhite.g, kWhite.b, .92f);

    static const char* names[] = {"Jogador um", "Jogador dois", "Criar perfil", "Convidado"};
    static const char* statuses[] = {"Online", "Online", "Novo perfil", "Offline"};

    const CardLayout layout = cardLayout(width, height);
    const float radius = 12.f;
    const float pulse = .5f + .5f * std::sin(elapsedSeconds * 2.7f);

    for (int i = 0; i < 4; ++i) {
        const float blend = std::clamp(selectionBlend_[i], 0.f, 1.f);
        const float grow = blend * 10.f;
        const float x = layout.startX + i * (layout.width + layout.gap) - grow * .5f;
        const float y = layout.top - grow * 1.4f;
        const float w = layout.width + grow;
        const float h = layout.height + grow * 1.4f;
        const float portraitH = h * .65f;
        const float centerX = x + w * .5f;

        // Drop shadow.
        drawRoundedRect(api, x + 2.f, y + 6.f, w, h, radius, {0.f, 0.f, 0.f, .14f});

        // Outer neon glow on the selected card.
        if (blend > .01f) {
            const float glow = (.18f + .10f * pulse) * blend;
            for (int ring = 4; ring >= 1; --ring) {
                const float spread = ring * 4.f;
                drawRoundedRect(api, x - spread, y - spread, w + spread * 2.f, h + spread * 2.f,
                                radius + spread,
                                {kGreenBright.r, kGreenBright.g, kGreenBright.b,
                                 glow / static_cast<float>(ring)});
            }
        }

        const Color bottom{
            kCardBottom.r + (kGreen.r - kCardBottom.r) * blend,
            kCardBottom.g + (kGreen.g - kCardBottom.g) * blend,
            kCardBottom.b + (kGreen.b - kCardBottom.b) * blend,
            kCardBottom.a};
        drawSplitRoundedRect(api, x, y, w, h, radius, portraitH, kCardTop, bottom);
        // Soft highlight across the portrait area.
        drawRect(api, x + radius, y + 2.f, w - radius * 2.f, portraitH * .35f,
                 {1.f, 1.f, 1.f, .10f});

        if (i == 2 || i == 3) {
            drawSilhouette(api, centerX, y + 8.f, portraitH - 8.f, kSilhouette);
        } else {
            glEnable(GL_DEPTH_TEST);
            drawPortrait(i == 1 ? female_ : male_, x + 3.f, y + 3.f, w - 6.f,
                         portraitH - 3.f, elapsedSeconds);
            glDisable(GL_DEPTH_TEST);
        }

        if (blend > .01f) {
            drawRoundedOutline(api, x - 1.f, y - 1.f, w + 2.f, h + 2.f, radius + 1.f, 3.5f,
                               {kGreenBright.r, kGreenBright.g, kGreenBright.b,
                                (.85f + .15f * pulse) * blend});
        }

        const Color labelColor{
            kText.r + (kWhite.r - kText.r) * blend,
            kText.g + (kWhite.g - kText.g) * blend,
            kText.b + (kWhite.b - kText.b) * blend, 1.f};
        const float textX = x + 18.f;
        drawText(names[i], textX, y + portraitH + 14.f, 3.3f,
                 labelColor.r, labelColor.g, labelColor.b, 1.f);

        const float orbY = y + portraitH + (h - portraitH) * .70f;
        const float orbR = 9.f;
        if (i == 2) {
            drawCircle(api, textX + orbR, orbY, orbR, {0.28f, 0.29f, 0.31f, 1.f});
            drawRect(api, textX + orbR - 5.f, orbY - 1.25f, 10.f, 2.5f, kWhite);
            drawRect(api, textX + orbR - 1.25f, orbY - 5.f, 2.5f, 10.f, kWhite);
        } else {
            drawOrb(api, textX + orbR, orbY, orbR, i == 3 ? kOffline : kOnline);
        }
        drawText(statuses[i], textX + orbR * 2.f + 10.f, orbY - 12.f, 2.6f,
                 labelColor.r, labelColor.g, labelColor.b, .95f);
    }

    // Controller hints bottom-left: green A and red B buttons.
    const float hintY = height - 34.f;
    drawOrb(api, 52.f, hintY, 14.f, kGreen);
    drawText("A", 52.f - textWidth("A", 2.6f) * .5f, hintY - 13.f, 2.6f,
             kWhite.r, kWhite.g, kWhite.b, 1.f);
    drawText("Selecionar", 74.f, hintY - 14.f, 3.f, kWhite.r, kWhite.g, kWhite.b, 1.f);
    const float bX = 74.f + textWidth("Selecionar", 3.f) + 34.f;
    drawOrb(api, bX, hintY, 14.f, kRed);
    drawText("B", bX - textWidth("B", 2.6f) * .5f, hintY - 13.f, 2.6f,
             kWhite.r, kWhite.g, kWhite.b, 1.f);
    drawText("Voltar", bX + 22.f, hintY - 14.f, 3.f, kWhite.r, kWhite.g, kWhite.b, 1.f);
}

ProfileInputAction ProfileSelectionScreen::handleTouch(float x, float y, float width,
                                                       float height) {
    const CardLayout layout = cardLayout(width, height);
    for (int i = 0; i < 4; ++i) {
        const float left = layout.startX + i * (layout.width + layout.gap);
        if (x >= left && x <= left + layout.width &&
            y >= layout.top - 14.f && y <= layout.top + layout.height) {
            if (selectedProfile_ == i) return ProfileInputAction::Confirm;
            selectedProfile_ = i;
            return ProfileInputAction::None;
        }
    }

    // Bottom-left controller hints: "A Selecionar" then "B Voltar".
    if (y >= height - 64.f && x < 360.f) {
        return x < 200.f ? ProfileInputAction::Confirm : ProfileInputAction::Back;
    }
    return ProfileInputAction::None;
}

ProfileInputAction ProfileSelectionScreen::handleKey(int keyCode) {
    switch (keyCode) {
        case AKEYCODE_DPAD_LEFT:
            selectedProfile_ = (selectedProfile_ + 3) % 4;
            return ProfileInputAction::None;
        case AKEYCODE_DPAD_RIGHT:
            selectedProfile_ = (selectedProfile_ + 1) % 4;
            return ProfileInputAction::None;
        case AKEYCODE_DPAD_CENTER:
        case AKEYCODE_ENTER:
        case AKEYCODE_BUTTON_A:
        case AKEYCODE_BUTTON_START:
            return ProfileInputAction::Confirm;
        case AKEYCODE_BACK:
        case AKEYCODE_ESCAPE:
        case AKEYCODE_BUTTON_B:
        case AKEYCODE_BUTTON_SELECT:
            return ProfileInputAction::Back;
        default:
            return ProfileInputAction::None;
    }
}

ProfileInputAction ProfileSelectionScreen::handleJoystick(float leftX, float rightX,
                                                          float hatX) {
    float horizontal = std::abs(leftX) >= std::abs(rightX) ? leftX : rightX;
    if (std::abs(hatX) > 0.5f) horizontal = hatX;
    const int direction = horizontal < -0.55f ? -1 : horizontal > 0.55f ? 1 : 0;
    if (direction == 0) {
        joystickDirection_ = 0;
        return ProfileInputAction::None;
    }
    const auto now = std::chrono::steady_clock::now();
    if (direction == joystickDirection_ && now < joystickRepeatAt_) {
        return ProfileInputAction::None;
    }

    joystickDirection_ = direction;
    joystickRepeatAt_ = now + std::chrono::milliseconds(300);
    selectedProfile_ = (selectedProfile_ + 4 + direction) % 4;
    return ProfileInputAction::None;
}

void ProfileSelectionScreen::setSelectedProfile(int index) {
    if (index >= 0 && index < 4) selectedProfile_ = index;
}

void ProfileSelectionScreen::confirmSelection() {
    activeProfile_ = selectedProfile_;
    saveSelection();
}

void ProfileSelectionScreen::cancelSelection() {
    selectedProfile_ = activeProfile_;
}

const char* ProfileSelectionScreen::activeProfileName() const {
    static const char* names[] = {"Jogador um", "Jogador dois", "Perfil novo", "Convidado"};
    return names[std::clamp(activeProfile_, 0, 3)];
}

void ProfileSelectionScreen::loadSelection() {
    if (storagePath_.empty()) return;
    const std::string path = storagePath_ + "/xenon-profile.dat";
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return;
    int storedProfile = 0;
    const bool valid = std::fscanf(file, "%d", &storedProfile) == 1 &&
                       storedProfile >= 0 && storedProfile < 4;
    std::fclose(file);
    if (valid) {
        activeProfile_ = storedProfile;
        selectedProfile_ = storedProfile;
    }
}

void ProfileSelectionScreen::saveSelection() const {
    if (storagePath_.empty()) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                            "Cannot persist profile selection: app storage path is empty");
        return;
    }
    const std::string path = storagePath_ + "/xenon-profile.dat";
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                            "Cannot open profile storage for writing: %s", path.c_str());
        return;
    }
    const int written = std::fprintf(file, "%d\n", activeProfile_);
    const int closeResult = std::fclose(file);
    if (written < 0 || closeResult != 0) {
        __android_log_print(ANDROID_LOG_ERROR, kLogTag,
                            "Cannot persist the selected profile: %s", path.c_str());
    }
}

} // namespace xenon
