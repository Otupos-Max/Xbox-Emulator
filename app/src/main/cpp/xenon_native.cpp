#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/types.h>
#include <vector>
#include <unistd.h>

#include "Core/Memory/MemoryManager.hpp"

namespace {

enum class Screen : int { Home, Library, System, Settings };

struct Rect { float x, y, w, h; };
struct Color { float r, g, b, a; };

constexpr Color kBg{0.031f, 0.039f, 0.051f, 1};
constexpr Color kPanel{0.063f, 0.075f, 0.094f, 1};
constexpr Color kPanel2{0.082f, 0.098f, 0.118f, 1};
constexpr Color kText{0.956f, 0.969f, 0.961f, 1};
constexpr Color kMuted{0.573f, 0.604f, 0.639f, 1};
constexpr Color kGreen{0.459f, 0.839f, 0.608f, 1};
constexpr Color kBlue{0.404f, 0.682f, 0.961f, 1};

struct Renderer {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    GLuint program = 0;
    GLint resolution = -1;
    GLint rectLocation = -1;
    GLint colorLocation = -1;
    int width = 0;
    int height = 0;

    bool initialize(ANativeWindow* window) {
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        eglInitialize(display, nullptr, nullptr);

        EGLConfig config;
        EGLint count = 0;
        const EGLint attrs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 0,
            EGL_NONE
        };
        eglChooseConfig(display, attrs, &config, 1, &count);
        if (count == 0) return false;

        surface = eglCreateWindowSurface(display, config, window, nullptr);
        const EGLint contextAttrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttrs);
        if (eglMakeCurrent(display, surface, surface, context) != EGL_TRUE) return false;
        eglQuerySurface(display, surface, EGL_WIDTH, &width);
        eglQuerySurface(display, surface, EGL_HEIGHT, &height);

        const char* vertex = R"(precision highp float;
attribute vec2 position;
uniform vec2 resolution;
uniform vec4 rect;
void main() {
    vec2 pixel = rect.xy + position * rect.zw;
    vec2 normalized = pixel / resolution * 2.0 - 1.0;
    gl_Position = vec4(normalized.x, -normalized.y, 0.0, 1.0);
})";
        const char* fragment = R"(precision mediump float;
uniform vec4 color;
void main() { gl_FragColor = color; })";

        auto compile = [](GLenum type, const char* source) {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);
            return shader;
        };
        GLuint vs = compile(GL_VERTEX_SHADER, vertex);
        GLuint fs = compile(GL_FRAGMENT_SHADER, fragment);
        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glBindAttribLocation(program, 0, "position");
        glLinkProgram(program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        resolution = glGetUniformLocation(program, "resolution");
        rectLocation = glGetUniformLocation(program, "rect");
        colorLocation = glGetUniformLocation(program, "color");

        glViewport(0, 0, width, height);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        return true;
    }

    void shutdown() {
        if (display != EGL_NO_DISPLAY) {
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
            if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
            eglTerminate(display);
        }
        display = EGL_NO_DISPLAY;
        surface = EGL_NO_SURFACE;
        context = EGL_NO_CONTEXT;
    }

    void resize(ANativeWindow* window) {
        eglQuerySurface(display, surface, EGL_WIDTH, &width);
        eglQuerySurface(display, surface, EGL_HEIGHT, &height);
        glViewport(0, 0, width, height);
    }

    void rect(float x, float y, float w, float h, const Color& c) {
        glUseProgram(program);
        glUniform2f(resolution, static_cast<float>(width), static_cast<float>(height));
        glUniform4f(rectLocation, x, y, w, h);
        glUniform4f(colorLocation, c.r, c.g, c.b, c.a);

        static const float vertices[] = {0,0, 1,0, 0,1, 1,1};
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, vertices);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    void roundedRect(float x, float y, float w, float h, const Color& c) {
        rect(x, y, w, h, c);
    }

    void text(const std::string& value, float x, float y, float scale, const Color& c) {
        static const std::string font = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        static const std::vector<std::string> glyphs = {
            "011101000110101010001010001","1000010000100001111010000100001","11110100011000010000111110","1111010001100001000011111","11111100001110010000111111",
            "11111100001110010000110000","01110100011000110111110010","1000010000100001111110000100001","1111100100010010010011111","00111000100010001000101110",
            "10010101011000110000100001","10000100001000010000111111","10001110101100110000100001","10001100011010110010110001","0111010001100011000101110",
            "11110100011011110000110000","01110100011000110110101111","11110100011011110100110001","0111010000101110000101110","1111100100010010010010010",
            "10000100001000010000101110","10000100001010001010001000","10000101010101011010100001","10001100010010001000110001","10001100010101001000100010",
            "11111000100010001000111111","11110110011000010000111111","11110110011011110000111111","0111010001100001000101110","1111100100010010010010010",
            "11110100011000010000100001","0111010001100011000101110","01110010010100101000111110","1111100100010010010010010","0111010000101110000101110",
            "11111001000100100100100100","0111010001100011000101110","10000100001000010000111111","11111000100010001000111111","10000101001000101000100001"
        };
        (void)glyphs;
        const int glyphWidth = 5;
        const int glyphHeight = 5;
        for (char raw : value) {
            char ch = raw;
            if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
            int index = static_cast<int>(font.find(ch));
            if (ch == '.') index = -2;
            if (ch == '/') index = -3;
            if (ch == '-') index = -4;
            if (ch == ':') index = -5;
            for (int row = 0; row < glyphHeight; ++row) {
                for (int col = 0; col < glyphWidth; ++col) {
                    bool on = false;
                    if (index >= 0) {
                        const std::string patterns[] = {
                            "0111010001101010100011111","10001110001000110001011111","1111010001100001000011111","1111010001100001000011111","1111110000111001000011111",
                            "1111110000111001000011000","0111010001101111010010111","1000110001100011111110001","0110010010010010010010110","0001100100010001000100110",
                            "1001011001100011000010001","1000100010001000100011111","1000111001101011000110001","1000110001101011001110001","0111010001100011000101110",
                            "1111010001111011000011000","01110100011000110101101111","1111010001111011010011001","0111010000100100010011110","1111100100010001000100100",
                            "1000110001100011000111110","1000110001101010101010001","1000111010101010101011001","1000110001010101000110001","1000110001010100100010001",
                            "1111100010001000100011111","1111011001100001000011111","1111011001101111000111111","0111010001100001000101110","1111100100010001000100100",
                            "1111010001100001000010001","0111010001100011000101110","0111001001010010100011111","1111100100010001000100100","0111010000101110000101110",
                            "1111100100010001000100100","0111010001100011000101110","1000010000100001000011111","1111100010001000100011111","1000010101010101000110001"
                        };
                        on = col < 5 && row < 5 && patterns[index].length() >= (row * 5 + col) &&
                             patterns[index][row * 5 + col] == '1';
                    } else if (ch == ' ') {
                        on = false;
                    } else if (index == -2) {
                        on = row == 4 && col == 1;
                    } else if (index == -3) {
                        on = (row == col);
                    } else if (index == -4) {
                        on = row == 2 && col >= 1 && col <= 3;
                    } else if (index == -5) {
                        on = (row == 1 || row == 3) && col == 2;
                    }
                    if (on) rect(x + col * scale, y + row * scale, scale * .82f, scale * .82f, c);
                }
            }
            x += (glyphWidth + 1) * scale;
        }
    }

    void begin() {
        glClearColor(kBg.r, kBg.g, kBg.b, kBg.a);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void present() { eglSwapBuffers(display, surface); }
};

struct SplashVideo {
    AMediaExtractor* extractor = nullptr;
    AMediaCodec* codec = nullptr;
    int fileDescriptor = -1;
    bool codecStarted = false;
    bool inputEnded = false;

    void stop() {
        if (codec) {
            if (codecStarted) AMediaCodec_stop(codec);
            AMediaCodec_delete(codec);
            codec = nullptr;
        }
        codecStarted = false;
        if (extractor) {
            AMediaExtractor_delete(extractor);
            extractor = nullptr;
        }
        if (fileDescriptor >= 0) {
            close(fileDescriptor);
            fileDescriptor = -1;
        }
        inputEnded = false;
    }

    bool start(AAssetManager* assets, ANativeWindow* window) {
        stop();
        if (!assets || !window) {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon", "Splash video requires an asset manager and window");
            return false;
        }

        AAsset* asset = AAssetManager_open(assets, "video splash.mp4", AASSET_MODE_RANDOM);
        if (!asset) {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon", "Splash video asset not found");
            return false;
        }

        off64_t offset = 0;
        off64_t length = 0;
        fileDescriptor = AAsset_openFileDescriptor64(asset, &offset, &length);
        AAsset_close(asset);
        if (fileDescriptor < 0) {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon", "Unable to open splash video asset descriptor");
            return false;
        }

        extractor = AMediaExtractor_new();
        if (!extractor ||
            AMediaExtractor_setDataSourceFd(extractor, fileDescriptor, offset, length) != AMEDIA_OK) {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon", "Unable to read splash video");
            stop();
            return false;
        }

        int videoTrack = -1;
        AMediaFormat* format = nullptr;
        for (size_t i = 0; i < AMediaExtractor_getTrackCount(extractor); ++i) {
            AMediaFormat* candidate = AMediaExtractor_getTrackFormat(extractor, i);
            const char* mime = nullptr;
            if (candidate && AMediaFormat_getString(candidate, AMEDIAFORMAT_KEY_MIME, &mime) &&
                mime && std::strncmp(mime, "video/", 6) == 0) {
                videoTrack = static_cast<int>(i);
                format = candidate;
                break;
            }
            if (candidate) AMediaFormat_delete(candidate);
        }

        if (videoTrack < 0 || !format) {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon", "Splash video has no video track");
            stop();
            return false;
        }

        const char* mime = nullptr;
        AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime);
        codec = mime ? AMediaCodec_createDecoderByType(mime) : nullptr;
        const bool configured = codec &&
            AMediaExtractor_selectTrack(extractor, static_cast<size_t>(videoTrack)) == AMEDIA_OK &&
            AMediaCodec_configure(codec, format, window, nullptr, 0) == AMEDIA_OK;
        AMediaFormat_delete(format);
        if (!configured || AMediaCodec_start(codec) != AMEDIA_OK) {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon", "Unable to initialize splash video decoder");
            stop();
            return false;
        }

        codecStarted = true;
        return true;
    }

    bool renderFrame() {
        if (!codec || !extractor) return false;

        if (!inputEnded) {
            const ssize_t inputIndex = AMediaCodec_dequeueInputBuffer(codec, 0);
            if (inputIndex >= 0) {
                size_t bufferSize = 0;
                uint8_t* buffer = AMediaCodec_getInputBuffer(codec, inputIndex, &bufferSize);
                const ssize_t sampleSize = buffer
                    ? AMediaExtractor_readSampleData(extractor, buffer, bufferSize)
                    : -1;
                if (sampleSize < 0) {
                    AMediaCodec_queueInputBuffer(codec, inputIndex, 0, 0, 0,
                                                 AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                    inputEnded = true;
                } else {
                    const int64_t presentationTime = AMediaExtractor_getSampleTime(extractor);
                    AMediaCodec_queueInputBuffer(codec, inputIndex, 0,
                                                 static_cast<size_t>(sampleSize),
                                                 presentationTime, 0);
                    AMediaExtractor_advance(extractor);
                }
            }
        }

        AMediaCodecBufferInfo info{};
        const ssize_t outputIndex = AMediaCodec_dequeueOutputBuffer(codec, &info, 0);
        if (outputIndex >= 0) {
            const bool ended = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            AMediaCodec_releaseOutputBuffer(codec, outputIndex, true);
            if (ended) {
                stop();
                return false;
            }
        }
        return true;
    }
};

struct AppState {
    Renderer renderer;
    SplashVideo splashVideo;
    Screen screen = Screen::Home;
    bool initialized = false;
    bool rendererInitialized = false;
    bool showingSplash = true;
    bool playingSplashVideo = false;
    bool memoryInitialized = false;
    ANativeWindow* window = nullptr;
    AAssetManager* assetManager = nullptr;
    std::chrono::steady_clock::time_point splashStarted;
    xboxemu::memory::MemoryManager memory;

    void initialize() {
        if (memoryInitialized) return;
        xboxemu::memory::MemoryManager::Config config;
        config.ram_size = 512u * 1024u * 1024u;
        memory.initialize(config);
        memoryInitialized = true;
    }

    void initializeWindow(ANativeWindow* newWindow, AAssetManager* assets) {
        window = newWindow;
        assetManager = assets;
        initialized = true;
        initialize();
        splashStarted = std::chrono::steady_clock::now();

        renderer.shutdown();
        rendererInitialized = false;
        if (showingSplash && splashVideo.start(assetManager, window)) {
            playingSplashVideo = true;
            return;
        }

        playingSplashVideo = false;
        rendererInitialized = renderer.initialize(window);
        if (!rendererInitialized) {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon", "Unable to initialize the splash screen renderer");
        }
    }

    bool hit(float px, float py, const Rect& r) const {
        return px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h;
    }

    void drawNavigation() {
        float w = static_cast<float>(renderer.width);
        float h = static_cast<float>(renderer.height);
        float barHeight = std::clamp(h * .09f, 58.f, 82.f);
        renderer.rect(0, h - barHeight, w, barHeight, kPanel);

        const char* labels[] = {"INICIO", "BIBLIOTECA", "SISTEMA", "AJUSTES"};
        for (int i = 0; i < 4; ++i) {
            float itemW = w / 4.f;
            float x = itemW * i;
            bool active = static_cast<int>(screen) == i;
            if (active) renderer.rect(x + itemW * .18f, h - barHeight + 5, itemW * .64f, 3, kGreen);
            renderer.text(labels[i], x + itemW * .5f - 18.f, h - barHeight + 22.f, 3.1f, active ? kGreen : kMuted);
        }
    }

    void drawHome() {
        float w = static_cast<float>(renderer.width);
        float h = static_cast<float>(renderer.height);
        float margin = std::clamp(w * .06f, 18.f, 42.f);

        renderer.text("XENON", margin, 28, 5, kText);
        renderer.text("NUCLEO EXPERIMENTAL", margin, 64, 3, kGreen);

        float heroY = 96;
        float heroH = std::min(h * .34f, 260.f);
        renderer.roundedRect(margin, heroY, w - margin * 2, heroH, kPanel2);
        renderer.text("PROXIMO CAPITULO", margin + 20, heroY + 28, 5, kText);
        renderer.text("COMECA AQUI", margin + 20, heroY + 62, 5, kText);
        renderer.text("Emulacao Xbox 360 nativa", margin + 20, heroY + 96, 3, kMuted);
        renderer.roundedRect(margin + 20, heroY + heroH - 58, 142, 38, kGreen);
        renderer.text("ABRIR JOGO", margin + 43, heroY + heroH - 47, 3, kBg);

        float cardsY = heroY + heroH + 24;
        float cardW = (w - margin * 2 - 24) / 2.f;
        renderer.roundedRect(margin, cardsY, cardW, 104, kPanel);
        renderer.text("BIBLIOTECA", margin + 16, cardsY + 20, 4, kText);
        renderer.text("SEUS TITULOS", margin + 16, cardsY + 54, 3, kMuted);
        renderer.roundedRect(margin + cardW + 24, cardsY, cardW, 104, kPanel);
        renderer.text("SISTEMA", margin + cardW + 40, cardsY + 20, 4, kText);
        renderer.text("512 MB RAM", margin + cardW + 40, cardsY + 54, 3, kMuted);

        renderer.text("ESTADO DO PROJETO", margin, cardsY + 132, 3.5f, kGreen);
        renderer.roundedRect(margin, cardsY + 154, w - margin * 2, 76, kPanel);
        renderer.text("MEMORIA     PPC 25 TESTES", margin + 16, cardsY + 178, 3.2f, kText);
        renderer.roundedRect(margin + 16, cardsY + 207, w - margin * 2 - 32, 7, kPanel2);
        renderer.roundedRect(margin + 16, cardsY + 207, (w - margin * 2 - 32) * .72f, 7, kGreen);
    }

    void drawLibrary() {
        float w = static_cast<float>(renderer.width);
        float h = static_cast<float>(renderer.height);
        float margin = std::clamp(w * .06f, 18.f, 42.f);
        renderer.text("BIBLIOTECA", margin, 38, 6, kText);
        renderer.roundedRect(margin, 120, w - margin * 2, h - 250, kPanel);
        renderer.text("SEM TITULOS", margin + 20, 155, 4, kMuted);
        renderer.text("Prepare o carregador XEX", margin + 20, 190, 3, kMuted);
        renderer.roundedRect(margin + 20, 225, 164, 42, kGreen);
        renderer.text("ADICIONAR JOGO", margin + 42, 238, 3, kBg);
    }

    void drawSystem() {
        float w = static_cast<float>(renderer.width);
        float h = static_cast<float>(renderer.height);
        float margin = std::clamp(w * .06f, 18.f, 42.f);
        renderer.text("SISTEMA", margin, 38, 6, kText);

        float cardY = 110;
        renderer.roundedRect(margin, cardY, w - margin * 2, 150, kPanel);
        renderer.text("MEMORIA UNIFICADA", margin + 18, cardY + 22, 3.5f, kGreen);
        renderer.text("512 MB", margin + 18, cardY + 62, 7, kText);
        renderer.roundedRect(margin + 18, cardY + 112, w - margin * 2 - 36, 10, kPanel2);
        renderer.roundedRect(margin + 18, cardY + 112, (w - margin * 2 - 36) * .18f, 10, kGreen);

        renderer.roundedRect(margin, cardY + 170, w - margin * 2, 82, kPanel);
        renderer.text("CPU XENON  3 CORES / 6 THREADS", margin + 18, cardY + 198, 3.3f, kText);
        renderer.roundedRect(margin, cardY + 272, w - margin * 2, 82, kPanel);
        renderer.text("XENOS GPU  NAO INICIADA", margin + 18, cardY + 300, 3.3f, kText);
    }

    void drawSettings() {
        float w = static_cast<float>(renderer.width);
        float margin = std::clamp(w * .06f, 18.f, 42.f);
        renderer.text("AJUSTES", margin, 38, 6, kText);
        for (int i = 0; i < 3; ++i) {
            float y = 112.f + i * 92.f;
            renderer.roundedRect(margin, y, w - margin * 2, 76, kPanel);
            const char* labels[] = {"VERIFICAR MEMORIA", "RASTREIO MMIO", "ESTENDER RELATORIO"};
            renderer.text(labels[i], margin + 18, y + 28, 3.3f, kText);
            renderer.roundedRect(w - margin - 68, y + 24, 50, 26, i == 0 ? kGreen : kPanel2);
        }
    }

    void drawSplash() {
        auto elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - splashStarted).count();
        if (elapsed >= 2.7f) {
            showingSplash = false;
            return;
        }

        float w = static_cast<float>(renderer.width);
        float h = static_cast<float>(renderer.height);
        float cx = w * .5f;

        for (int i = 0; i < 3; ++i) {
            float inset = 54.f - i * 16.f;
            renderer.roundedRect(cx - 110.f + inset * .28f, h * .34f - 38.f + i * 14.f,
                                 220.f - inset * .56f, 5.f,
                                 i == 1 ? kGreen : kPanel2);
        }

        renderer.roundedRect(cx - 64.f, h * .38f - 64.f, 128.f, 128.f, kPanel);
        renderer.roundedRect(cx - 50.f, h * .38f - 50.f, 100.f, 100.f, kGreen);
        renderer.roundedRect(cx - 36.f, h * .38f - 36.f, 72.f, 72.f, kBg);

        renderer.rect(cx - 42.f, h * .38f - 14.f, 84.f, 12.f, kText);
        renderer.rect(cx - 42.f, h * .38f - 14.f, 12.f, 54.f, kText);
        renderer.rect(cx + 30.f, h * .38f - 14.f, 12.f, 54.f, kText);

        renderer.text("XENON", cx - 58.f, h * .58f, 8, kText);
        renderer.text("EXPERIMENTAL EMULATOR", cx - 108.f, h * .58f + 58.f, 3, kMuted);

        float barW = w * .55f;
        float progress = std::clamp(elapsed / 2.7f, 0.f, 1.f);
        renderer.roundedRect(cx - barW * .5f, h * .78f, barW, 6.f, kPanel2);
        renderer.roundedRect(cx - barW * .5f, h * .78f, barW * progress, 6.f, kGreen);
    }

    void draw() {
        if (playingSplashVideo) {
            if (!splashVideo.renderFrame()) {
                playingSplashVideo = false;
                showingSplash = false;
                rendererInitialized = renderer.initialize(window);
                if (!rendererInitialized) {
                    __android_log_print(ANDROID_LOG_ERROR, "Xenon", "Unable to initialize the app renderer");
                }
            }
            return;
        }
        if (!rendererInitialized) return;

        renderer.begin();
        if (showingSplash) {
            drawSplash();
            renderer.present();
            return;
        }
        switch (screen) {
            case Screen::Home: drawHome(); break;
            case Screen::Library: drawLibrary(); break;
            case Screen::System: drawSystem(); break;
            case Screen::Settings: drawSettings(); break;
        }
        drawNavigation();
        renderer.present();
    }

    void handleTouch(float x, float y) {
        float w = static_cast<float>(renderer.width);
        float h = static_cast<float>(renderer.height);
        float barHeight = std::clamp(h * .09f, 58.f, 82.f);
        if (y >= h - barHeight) {
            int index = std::clamp(static_cast<int>(x / (w / 4.f)), 0, 3);
            screen = static_cast<Screen>(index);
            return;
        }
    }
};

void onAppCmd(android_app* app, int32_t cmd) {
    auto* state = reinterpret_cast<AppState*>(app->userData);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window && state) {
                state->initializeWindow(app->window, app->activity->assetManager);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            if (state) {
                state->splashVideo.stop();
                state->playingSplashVideo = false;
                state->renderer.shutdown();
                state->rendererInitialized = false;
                state->initialized = false;
                state->window = nullptr;
            }
            break;
        case APP_CMD_WINDOW_RESIZED:
            if (state && state->initialized && state->rendererInitialized) {
                state->renderer.resize(app->window);
            }
            break;
        default:
            break;
    }
}

int32_t onInputEvent(android_app* app, AInputEvent* event) {
    auto* state = reinterpret_cast<AppState*>(app->userData);
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION || !state) return 0;
    if (AMotionEvent_getAction(event) == AMOTION_EVENT_ACTION_DOWN) {
        if (state->showingSplash) return 1;
        state->handleTouch(AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0));
        return 1;
    }
    return 0;
}

} // namespace

void android_main(android_app* app) {
    AppState state;
    app->userData = &state;
    app->onAppCmd = onAppCmd;
    app->onInputEvent = onInputEvent;

    bool running = true;
    while (running) {
        android_poll_source* source = nullptr;
        int events = 0;
        int result = ALooper_pollOnce(state.initialized ? 16 : -1, nullptr, &events,
                                      reinterpret_cast<void**>(&source));
        if (result == ALOOPER_POLL_ERROR) break;
        if (source) source->process(app, source);
        if (app->destroyRequested) running = false;
        if (state.initialized) state.draw();
    }
    state.renderer.shutdown();
}
