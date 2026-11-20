#pragma once

#include <GLES2/gl2.h>
#include <android/asset_manager.h>

#include <chrono>
#include <string>
#include <vector>

namespace xenon {

struct ProfileDrawApi {
    void* context = nullptr;
    void (*rect)(void*, float, float, float, float, float, float, float, float) = nullptr;
};

enum class ProfileInputAction {
    None,
    Confirm,
    Back
};

class ProfileSelectionScreen {
public:
    bool initialize(AAssetManager* assets, const std::string& storagePath);
    void shutdown();

    void draw(float width, float height, float elapsedSeconds, const ProfileDrawApi& api);
    ProfileInputAction handleTouch(float x, float y, float width, float height);
    ProfileInputAction handleKey(int keyCode);
    ProfileInputAction handleJoystick(float leftX, float rightX, float hatX);

    void setSelectedProfile(int index);
    void confirmSelection();
    void cancelSelection();
    int activeProfile() const { return activeProfile_; }
    int selectedProfile() const { return selectedProfile_; }
    const char* activeProfileName() const;

private:
    struct Model {
        struct Batch {
            GLuint vertexBuffer = 0;
            GLuint indexBuffer = 0;
            GLsizei indexCount = 0;
        };

        std::vector<Batch> batches;
        GLuint texture = 0;
    };

    struct Glyph {
        uint32_t codepoint = 0;
        uint16_t x = 0;
        uint16_t y = 0;
        uint16_t width = 0;
        uint16_t height = 0;
        int16_t bearingX = 0;
        int16_t bearingY = 0;
        uint16_t advance = 0;
    };

    bool loadModel(AAssetManager* assets, const char* assetName, Model& model);
    bool loadFont(AAssetManager* assets);
    bool createPrograms();
    void drawBackground(float elapsedSeconds);
    void drawText(const char* text, float x, float y, float scale, float r, float g,
                  float b, float a);
    float textWidth(const char* text, float scale) const;
    void drawModel(const Model& model, float centerX, float centerY, float scale,
                   float angle, float alpha, float verticalScale);
    void drawPortrait(const Model& model, float x, float y, float w, float h,
                      float elapsedSeconds);
    void loadSelection();
    void saveSelection() const;

    GLuint backgroundProgram_ = 0;
    GLuint backgroundBuffer_ = 0;
    GLuint modelProgram_ = 0;
    GLuint textProgram_ = 0;
    GLuint textBuffer_ = 0;
    GLuint fontTexture_ = 0;
    GLint backgroundPosition_ = -1;
    GLint backgroundTime_ = -1;
    GLint modelPosition_ = -1;
    GLint modelNormal_ = -1;
    GLint modelTexcoord_ = -1;
    GLint modelViewport_ = -1;
    GLint modelCenter_ = -1;
    GLint modelScale_ = -1;
    GLint modelAngle_ = -1;
    GLint modelAlpha_ = -1;
    GLint modelVerticalScale_ = -1;
    GLint modelTexture_ = -1;
    GLint textPosition_ = -1;
    GLint textTexcoord_ = -1;
    GLint textViewport_ = -1;
    GLint textColor_ = -1;
    GLint textTexture_ = -1;
    uint32_t fontAtlasWidth_ = 0;
    uint32_t fontAtlasHeight_ = 0;
    uint32_t fontEmSize_ = 0;
    size_t textBufferCapacity_ = 0;
    std::vector<Glyph> glyphs_;
    Model male_;
    Model female_;
    std::string storagePath_;
    float selectionBlend_[4] = {1.f, 0.f, 0.f, 0.f};
    int selectedProfile_ = 0;
    int activeProfile_ = 0;
    int joystickDirection_ = 0;
    std::chrono::steady_clock::time_point joystickRepeatAt_;
    float previousFrameTime_ = 0.f;
    bool hasPreviousFrame_ = false;
};

} // namespace xenon
