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

constexpr Color kText{0.055f, 0.078f, 0.075f, 1.f};
constexpr Color kMuted{0.31f, 0.38f, 0.36f, 1.f};
constexpr Color kGreen{0.12f, 0.61f, 0.32f, 1.f};
constexpr Color kMint{0.49f, 0.82f, 0.60f, 1.f};
constexpr Color kBlue{0.21f, 0.54f, 0.81f, 1.f};
constexpr Color kCard{0.93f, 0.96f, 0.94f, 0.82f};

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
void main() {
    float y = 1.0 - uv.y;
    vec3 base = mix(vec3(0.96, 0.97, 0.96), vec3(0.84, 0.88, 0.86),
                    smoothstep(0.0, 1.0, y) * 0.64);
    float wave = 0.825 + 0.045 * sin(uv.x * 3.15 - time * 0.10)
                       + 0.012 * sin(uv.x * 6.3 + time * 0.07);
    float upperWave = 0.775 + 0.022 * sin(uv.x * 2.85 + time * 0.08);
    float lowerFill = smoothstep(wave - 0.012, wave + 0.024, y);
    float waveBand = exp(-abs(y - wave) * 145.0);
    float upperBand = exp(-abs(y - upperWave) * 105.0);
    float softShadow = exp(-abs(y - wave - 0.018) * 58.0);
    float softGlow = exp(-abs(y - wave + 0.008) * 72.0);
    base = mix(base, vec3(0.39, 0.64, 0.48), lowerFill * 0.60);
    base = mix(base, vec3(0.18, 0.48, 0.30), lowerFill *
               smoothstep(wave + 0.035, 0.99, y) * 0.32);
    base += vec3(0.045, 0.13, 0.067) * waveBand * 0.30;
    base += vec3(0.16, 0.30, 0.20) * upperBand * 0.13;
    base *= 1.0 - softShadow * 0.07;
    base += vec3(0.04, 0.13, 0.065) * softGlow * 0.10;
    float reflection = exp(-abs(y - wave - 0.075) * 25.0);
    base += vec3(0.11, 0.20, 0.14) * reflection * 0.028;
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
    float c = cos(angle);
    float s = sin(angle);
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
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    const Model& heroModel = selectedProfile_ == 1 || selectedProfile_ == 3 ? female_ : male_;
    const float modelScale = std::min(height * .30f, 216.f);
    const float floorLine = height - 68.f;
    const float modelCenterY = floorLine - modelScale * .995f;
    const float modelCenterX = width * .235f;
    const float idleYaw = std::sin(elapsedSeconds * .22f) * .012f;
    drawModel(heroModel, modelCenterX, modelCenterY, modelScale, idleYaw, 1.f, 1.f);

    const float startX = width * .485f;
    const float gap = 11.f;
    const float cardWidth = (width * .49f - gap * 3.f) / 4.f;
    const float cardTop = height * .365f;
    const float cardHeight = height * .405f;
    const float titleX = startX;
    drawText("Escolha um perfil", titleX, height * .105f, 6.f,
             kText.r, kText.g, kText.b, kText.a);
    drawText("Faça login para continuar", titleX, height * .178f, 3.45f,
             kMuted.r, kMuted.g, kMuted.b, kMuted.a);

    static const char* names[] = {"Jogador um", "Jogador dois", "Criar perfil", "Convidado"};
    static const char* statuses[] = {"Online", "Online", "Novo perfil", "Offline"};
    static const Color accents[] = {kGreen, kBlue, kMint, kMuted};

    for (int i = 0; i < 4; ++i) {
        const float x = startX + i * (cardWidth + gap);
        const float centerX = x + cardWidth * .5f;
        const float blend = std::clamp(selectionBlend_[i], 0.f, 1.f);
        const float pulse = .5f + .5f * std::sin(elapsedSeconds * 2.7f);
        drawRect(api, x, cardTop, cardWidth, cardHeight,
                 {kCard.r, kCard.g, kCard.b, .87f + blend * .08f});
        if (blend > .01f) {
            const float glowAlpha = .09f + .07f * pulse;
            drawRect(api, x - 10.f, cardTop - 10.f, cardWidth + 20.f, 3.f,
                     {kGreen.r, kGreen.g, kGreen.b, glowAlpha});
            drawRect(api, x - 10.f, cardTop + cardHeight + 7.f, cardWidth + 20.f, 3.f,
                     {kGreen.r, kGreen.g, kGreen.b, glowAlpha});
            drawRect(api, x - 10.f, cardTop - 7.f, 3.f, cardHeight + 14.f,
                     {kGreen.r, kGreen.g, kGreen.b, glowAlpha});
            drawRect(api, x + cardWidth + 7.f, cardTop - 7.f, 3.f, cardHeight + 14.f,
                     {kGreen.r, kGreen.g, kGreen.b, glowAlpha});
            const float edgeAlpha = .80f + .18f * pulse;
            drawRect(api, x - 2.f, cardTop - 2.f, cardWidth + 4.f, 2.f,
                     {kGreen.r, kGreen.g, kGreen.b, edgeAlpha});
            drawRect(api, x - 2.f, cardTop + cardHeight, cardWidth + 4.f, 2.f,
                     {kGreen.r, kGreen.g, kGreen.b, edgeAlpha});
            drawRect(api, x - 2.f, cardTop, 2.f, cardHeight,
                     {kGreen.r, kGreen.g, kGreen.b, edgeAlpha});
            drawRect(api, x + cardWidth, cardTop, 2.f, cardHeight,
                     {kGreen.r, kGreen.g, kGreen.b, edgeAlpha});
            drawRect(api, x + 7.f, cardTop + 7.f, cardWidth - 14.f, 2.f,
                     {kMint.r, kMint.g, kMint.b, .4f + blend * .38f});
        }

        if (i == 2) {
            const float plusY = cardTop + 69.f;
            drawRect(api, centerX - 29.f, plusY + 13.f, 58.f, 8.f,
                     {kGreen.r, kGreen.g, kGreen.b, .82f});
            drawRect(api, centerX - 4.f, plusY - 12.f, 8.f, 58.f,
                     {kGreen.r, kGreen.g, kGreen.b, .82f});
        } else {
            const Model& model = i == 1 || i == 3 ? female_ : male_;
            const float size = 76.f + blend * 5.f;
            const float yaw = std::sin(elapsedSeconds * .18f) * .009f;
            drawModel(model, centerX, cardTop + 96.f, size, yaw, 1.f, 1.f);
        }

        const float labelScale = i == 2 ? 2.65f : 2.9f;
        const Color labelColor = blend > .35f ? kGreen : kText;
        drawText(names[i], centerX - textWidth(names[i], labelScale) * .5f,
                 cardTop + cardHeight - 80.f, labelScale,
                 labelColor.r, labelColor.g, labelColor.b, labelColor.a);
        const Color statusColor = i == 2 ? kMuted : accents[i];
        const float statusScale = 2.25f;
        drawRect(api, centerX - 32.f, cardTop + cardHeight - 48.f, 7.f, 7.f,
                 {statusColor.r, statusColor.g, statusColor.b, .9f});
        drawText(statuses[i], centerX + 8.f - textWidth(statuses[i], statusScale) * .5f,
                 cardTop + cardHeight - 49.f, statusScale,
                 statusColor.r, statusColor.g, statusColor.b, statusColor.a);
    }

    glDisable(GL_DEPTH_TEST);
    drawRect(api, 0.f, height - 60.f, width, 60.f, {0.91f, 0.94f, 0.92f, .90f});
    drawRect(api, 0.f, height - 60.f, width, 1.f, {0.20f, 0.38f, 0.28f, .13f});
    drawText("A  Selecionar", 40.f, height - 38.f, 3.2f,
             kText.r, kText.g, kText.b, kText.a);
    const char* backText = "B  Voltar";
    drawText(backText, width - 40.f - textWidth(backText, 3.2f),
             height - 38.f, 3.2f, kMuted.r, kMuted.g, kMuted.b, kMuted.a);
}

ProfileInputAction ProfileSelectionScreen::handleTouch(float x, float y, float width,
                                                       float height) {
    const float startX = width * .485f;
    const float gap = 11.f;
    const float cardWidth = (width * .49f - gap * 3.f) / 4.f;
    const float cardTop = height * .365f;
    const float cardHeight = height * .405f;
    for (int i = 0; i < 4; ++i) {
        const float left = startX + i * (cardWidth + gap);
        if (x >= left && x <= left + cardWidth &&
            y >= cardTop && y <= cardTop + cardHeight) {
            if (selectedProfile_ == i) return ProfileInputAction::Confirm;
            selectedProfile_ = i;
            return ProfileInputAction::None;
        }
    }

    if (y >= height - 60.f) {
        return x < width * .5f ? ProfileInputAction::Confirm : ProfileInputAction::Back;
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
            return ProfileInputAction::Confirm;
        case AKEYCODE_BACK:
        case AKEYCODE_ESCAPE:
        case AKEYCODE_BUTTON_B:
            return ProfileInputAction::Back;
        default:
            return ProfileInputAction::None;
    }
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
