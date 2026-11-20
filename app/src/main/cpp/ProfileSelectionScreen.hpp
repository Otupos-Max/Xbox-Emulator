#pragma once

#include <GLES2/gl2.h>
#include <android/asset_manager.h>

#include <string>
#include <vector>

namespace xenon {

struct ProfileDrawApi {
    void* context = nullptr;
    void (*rect)(void*, float, float, float, float, float, float, float, float) = nullptr;
    void (*text)(void*, const char*, float, float, float, float, float, float, float) = nullptr;
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

    bool loadModel(AAssetManager* assets, const char* assetName, Model& model);
    bool createPrograms();
    void drawBackground(float elapsedSeconds);
    void drawModel(const Model& model, float centerX, float centerY, float scale,
                   float angle, float elapsedSeconds, float alpha, float verticalScale);
    void loadSelection();
    void saveSelection() const;

    GLuint backgroundProgram_ = 0;
    GLuint backgroundBuffer_ = 0;
    GLuint modelProgram_ = 0;
    GLint backgroundPosition_ = -1;
    GLint backgroundTime_ = -1;
    GLint modelPosition_ = -1;
    GLint modelNormal_ = -1;
    GLint modelTexcoord_ = -1;
    GLint modelViewport_ = -1;
    GLint modelCenter_ = -1;
    GLint modelScale_ = -1;
    GLint modelAngle_ = -1;
    GLint modelTime_ = -1;
    GLint modelAlpha_ = -1;
    GLint modelVerticalScale_ = -1;
    GLint modelTexture_ = -1;
    Model male_;
    Model female_;
    std::string storagePath_;
    float selectionBlend_[4] = {1.f, 0.f, 0.f, 0.f};
    int selectedProfile_ = 0;
    int activeProfile_ = 0;
    float previousFrameTime_ = 0.f;
    bool hasPreviousFrame_ = false;
};

} // namespace xenon
