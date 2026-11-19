#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <aaudio/AAudio.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>

#include <algorithm>
#include <atomic>
#include <thread>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/types.h>
#include <vector>
#include <unistd.h>

namespace {

enum class Screen : int { Home, Library, System, Settings, ProfileSelect };

struct Rect { float x, y, w, h; };
struct Color { float r, g, b, a; };

// Light theme: white background to match the splash video.
constexpr Color kBg{1.0f, 1.0f, 1.0f, 1};
constexpr Color kPanel{0.945f, 0.953f, 0.957f, 1};
constexpr Color kPanel2{0.882f, 0.898f, 0.910f, 1};
constexpr Color kText{0.067f, 0.075f, 0.086f, 1};
constexpr Color kMuted{0.420f, 0.451f, 0.490f, 1};
constexpr Color kGreen{0.180f, 0.620f, 0.360f, 1};
constexpr Color kBlue{0.404f, 0.682f, 0.961f, 1};

struct Renderer {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    GLuint program = 0;
    GLuint vertexBuffer = 0;
    GLint resolution = -1;
    GLint rectLocation = -1;
    GLint colorLocation = -1;
    int width = 0;
    int height = 0;
    int surfaceWidth = 0;
    int surfaceHeight = 0;

    bool initialize(ANativeWindow* window) {
        display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display == EGL_NO_DISPLAY || eglInitialize(display, nullptr, nullptr) != EGL_TRUE) {
            shutdown();
            return false;
        }

        EGLConfig config;
        EGLint count = 0;
        const EGLint attrs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 0,
            EGL_NONE
        };
        eglChooseConfig(display, attrs, &config, 1, &count);
        if (count == 0) {
            shutdown();
            return false;
        }

        surface = eglCreateWindowSurface(display, config, window, nullptr);
        const EGLint contextAttrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
        context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttrs);
        if (surface == EGL_NO_SURFACE || context == EGL_NO_CONTEXT ||
            eglMakeCurrent(display, surface, surface, context) != EGL_TRUE) {
            shutdown();
            return false;
        }
        eglQuerySurface(display, surface, EGL_WIDTH, &surfaceWidth);
        eglQuerySurface(display, surface, EGL_HEIGHT, &surfaceHeight);
        width = 1280;
        height = 720;

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
            GLint compiled = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
            if (compiled != GL_TRUE) {
                char log[512] = {};
                glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
                __android_log_print(ANDROID_LOG_ERROR, "Xenon", "Shader compilation failed: %s", log);
                glDeleteShader(shader);
                return static_cast<GLuint>(0);
            }
            return shader;
        };
        GLuint vs = compile(GL_VERTEX_SHADER, vertex);
        GLuint fs = compile(GL_FRAGMENT_SHADER, fragment);
        if (vs == 0 || fs == 0) {
            if (vs != 0) glDeleteShader(vs);
            if (fs != 0) glDeleteShader(fs);
            shutdown();
            return false;
        }
        program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        glBindAttribLocation(program, 0, "position");
        glLinkProgram(program);
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (linked != GL_TRUE) {
            char log[512] = {};
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            __android_log_print(ANDROID_LOG_ERROR, "Xenon", "Program linking failed: %s", log);
            glDeleteProgram(program);
            program = 0;
            shutdown();
            return false;
        }
        resolution = glGetUniformLocation(program, "resolution");
        rectLocation = glGetUniformLocation(program, "rect");
        colorLocation = glGetUniformLocation(program, "color");
        glGenBuffers(1, &vertexBuffer);
        if (vertexBuffer == 0) {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon", "Unable to create the renderer vertex buffer");
            shutdown();
            return false;
        }
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 8, nullptr, GL_STREAM_DRAW);

        glViewport(0, 0, surfaceWidth, surfaceHeight);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        return true;
    }

    void shutdown() {
        if (display != EGL_NO_DISPLAY) {
            if (context != EGL_NO_CONTEXT &&
                eglMakeCurrent(display, surface, surface, context) == EGL_TRUE) {
                if (vertexBuffer != 0) glDeleteBuffers(1, &vertexBuffer);
                if (program != 0) glDeleteProgram(program);
            }
            eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context != EGL_NO_CONTEXT) eglDestroyContext(display, context);
            if (surface != EGL_NO_SURFACE) eglDestroySurface(display, surface);
            eglTerminate(display);
        }
        display = EGL_NO_DISPLAY;
        surface = EGL_NO_SURFACE;
        context = EGL_NO_CONTEXT;
        program = 0;
        vertexBuffer = 0;
    }

    void resize(ANativeWindow* window) {
        eglQuerySurface(display, surface, EGL_WIDTH, &surfaceWidth);
        eglQuerySurface(display, surface, EGL_HEIGHT, &surfaceHeight);
        glViewport(0, 0, surfaceWidth, surfaceHeight);
    }

    void rect(float x, float y, float w, float h, const Color& c) {
        glUseProgram(program);
        glUniform2f(resolution, static_cast<float>(width), static_cast<float>(height));
        glUniform4f(rectLocation, x, y, w, h);
        glUniform4f(colorLocation, c.r, c.g, c.b, c.a);

        static const float vertices[] = {0,0, 1,0, 0,1, 1,1};
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    void roundedRect(float x, float y, float w, float h, const Color& c) {
        rect(x, y, w, h, c);
    }

    void rotatedRect(float centerX, float centerY, float w, float h, float angle, const Color& c) {
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const float halfW = w * 0.5f;
        const float halfH = h * 0.5f;
        const float corners[] = {
            -halfW, -halfH, halfW, -halfH,
            -halfW, halfH, halfW, halfH
        };
        float vertices[8];
        float minX = 0.f;
        float minY = 0.f;
        float maxX = 0.f;
        float maxY = 0.f;
        for (int i = 0; i < 4; ++i) {
            const float x = corners[i * 2] * cosine - corners[i * 2 + 1] * sine;
            const float y = corners[i * 2] * sine + corners[i * 2 + 1] * cosine;
            vertices[i * 2] = x;
            vertices[i * 2 + 1] = y;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }

        const float boundsW = maxX - minX;
        const float boundsH = maxY - minY;
        for (int i = 0; i < 4; ++i) {
            vertices[i * 2] = (vertices[i * 2] - minX) / boundsW;
            vertices[i * 2 + 1] = (vertices[i * 2 + 1] - minY) / boundsH;
        }

        glUseProgram(program);
        glUniform2f(resolution, static_cast<float>(width), static_cast<float>(height));
        glUniform4f(rectLocation, centerX + minX, centerY + minY, boundsW, boundsH);
        glUniform4f(colorLocation, c.r, c.g, c.b, c.a);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
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
                        // Fonte de bloco 5x5 pra A-Z e 0-9 (linha por linha,
                        // 25 bits por caractere). A tabela antiga tinha
                        // entradas erradas/desalinhadas que deixavam várias
                        // letras com cara de símbolo estranho.
                        const std::string patterns[] = {
                            "0111010001111111000110001","1111010001111101000111110","0111110000100001000001111","1111010001100011000111110","1111110000111101000011111",
                            "1111110000111101000010000","0111110000100111000101111","1000110001111111000110001","0111000100001000010001110","0011100010000101001001100",
                            "1000110010111001001010001","1000010000100001000011111","1000111011101011000110001","1000111001101011001110001","0111010001100011000101110",
                            "1111010001111101000010000","0111010001101011001001101","1111010001111101001010001","0111110000011100000111110","1111100100001000010000100",
                            "1000110001100011000101110","1000110001100010101000100","1000110001101011010101010","1000101010001000101010001","1000101010001000010000100",
                            "1111100010001000100011111","0111010011101011100101110","0010001100001000010001110","0111010001000100010011111","1111000001001100000111110",
                            "0001000110010101111100010","1111110000111100000111110","0011001000111101000101110","1111100001000100010000100","0111010001011101000101110",
                            "0111010001011110000101110"
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

    float logicalX(float x) const {
        return surfaceWidth > 0 ? x * width / static_cast<float>(surfaceWidth) : x;
    }

    float logicalY(float y) const {
        return surfaceHeight > 0 ? y * height / static_cast<float>(surfaceHeight) : y;
    }
};

// Plays the splash video's audio track on a background thread via AAudio.
struct SplashAudio {
    std::thread worker;
    std::atomic<bool> running{false};

    void start(int fd, off64_t offset, off64_t length) {
        stop();
        const int ownFd = dup(fd);
        if (ownFd < 0) return;
        running = true;
        worker = std::thread([this, ownFd, offset, length] {
            play(ownFd, offset, length);
            close(ownFd);
        });
    }

    void stop() {
        running = false;
        if (worker.joinable()) worker.join();
    }

    void play(int fd, off64_t offset, off64_t length) {
        AMediaExtractor* extractor = AMediaExtractor_new();
        AMediaCodec* codec = nullptr;
        AAudioStream* stream = nullptr;
        AMediaFormat* format = nullptr;
        int32_t sampleRate = 44100;
        int32_t channels = 2;

        if (!extractor || AMediaExtractor_setDataSourceFd(extractor, fd, offset, length) != AMEDIA_OK) {
            if (extractor) AMediaExtractor_delete(extractor);
            return;
        }
        for (size_t i = 0; i < AMediaExtractor_getTrackCount(extractor); ++i) {
            AMediaFormat* candidate = AMediaExtractor_getTrackFormat(extractor, i);
            const char* mime = nullptr;
            if (candidate && AMediaFormat_getString(candidate, AMEDIAFORMAT_KEY_MIME, &mime) &&
                mime && std::strncmp(mime, "audio/", 6) == 0) {
                AMediaExtractor_selectTrack(extractor, i);
                format = candidate;
                break;
            }
            if (candidate) AMediaFormat_delete(candidate);
        }
        if (!format) {
            AMediaExtractor_delete(extractor);
            return;
        }
        AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sampleRate);
        AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
        const char* mime = nullptr;
        AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime);
        codec = mime ? AMediaCodec_createDecoderByType(mime) : nullptr;
        bool ok = codec && AMediaCodec_configure(codec, format, nullptr, nullptr, 0) == AMEDIA_OK &&
                  AMediaCodec_start(codec) == AMEDIA_OK;
        AMediaFormat_delete(format);

        AAudioStreamBuilder* builder = nullptr;
        if (ok && AAudio_createStreamBuilder(&builder) == AAUDIO_OK) {
            AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_I16);
            AAudioStreamBuilder_setSampleRate(builder, sampleRate);
            AAudioStreamBuilder_setChannelCount(builder, channels);
            if (AAudioStreamBuilder_openStream(builder, &stream) != AAUDIO_OK) stream = nullptr;
            AAudioStreamBuilder_delete(builder);
        }
        if (stream) AAudioStream_requestStart(stream);
        ok = ok && stream;

        bool inputEnded = false;
        while (ok && running) {
            if (!inputEnded) {
                const ssize_t in = AMediaCodec_dequeueInputBuffer(codec, 5000);
                if (in >= 0) {
                    size_t size = 0;
                    uint8_t* buf = AMediaCodec_getInputBuffer(codec, in, &size);
                    const ssize_t n = buf ? AMediaExtractor_readSampleData(extractor, buf, size) : -1;
                    if (n < 0) {
                        AMediaCodec_queueInputBuffer(codec, in, 0, 0, 0, AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
                        inputEnded = true;
                    } else {
                        AMediaCodec_queueInputBuffer(codec, in, 0, static_cast<size_t>(n),
                                                     AMediaExtractor_getSampleTime(extractor), 0);
                        AMediaExtractor_advance(extractor);
                    }
                }
            }
            AMediaCodecBufferInfo info{};
            const ssize_t out = AMediaCodec_dequeueOutputBuffer(codec, &info, 10000);
            if (out == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
                AMediaFormat* f = AMediaCodec_getOutputFormat(codec);
                int32_t ch = channels;
                if (f) { AMediaFormat_getInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &ch); AMediaFormat_delete(f); }
                if (ch > 0) channels = ch;
            } else if (out >= 0) {
                size_t size = 0;
                uint8_t* buf = AMediaCodec_getOutputBuffer(codec, out, &size);
                if (buf && info.size > 0) {
                    const int32_t frames = info.size / (2 * channels);
                    AAudioStream_write(stream, buf + info.offset, frames, 1000000000LL);
                }
                AMediaCodec_releaseOutputBuffer(codec, out, false);
                if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) break;
            }
        }

        if (stream) {
            AAudioStream_requestStop(stream);
            AAudioStream_close(stream);
        }
        if (codec) {
            AMediaCodec_stop(codec);
            AMediaCodec_delete(codec);
        }
        AMediaExtractor_delete(extractor);
    }
};

struct SplashVideo {
    SplashAudio audio;
    std::chrono::steady_clock::time_point playbackStart;
    bool clockStarted = false;
    ssize_t pendingOutput = -1;
    AMediaCodecBufferInfo pendingInfo{};
    AMediaExtractor* extractor = nullptr;
    AMediaCodec* codec = nullptr;
    int fileDescriptor = -1;
    bool codecStarted = false;
    bool inputEnded = false;

    void stop() {
        audio.stop();
        pendingOutput = -1;
        clockStarted = false;
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

        audio.start(fileDescriptor, offset, length);

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

        if (pendingOutput < 0) {
            pendingOutput = AMediaCodec_dequeueOutputBuffer(codec, &pendingInfo, 0);
            if (pendingOutput < 0) return true;
        }
        // Show each frame at its timestamp so the picture stays in sync with the audio.
        const auto now = std::chrono::steady_clock::now();
        if (!clockStarted) {
            playbackStart = now - std::chrono::microseconds(pendingInfo.presentationTimeUs);
            clockStarted = true;
        }
        const auto due = playbackStart + std::chrono::microseconds(pendingInfo.presentationTimeUs);
        if (now < due) return true;
        {
            const ssize_t outputIndex = pendingOutput;
            const AMediaCodecBufferInfo info = pendingInfo;
            pendingOutput = -1;
            const bool ended = (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) != 0;
            AMediaCodec_releaseOutputBuffer(codec, outputIndex, info.size > 0);
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
    Screen screen = Screen::ProfileSelect;
    bool initialized = false;
    bool rendererInitialized = false;
    bool showingSplash = true;
    bool playingSplashVideo = false;
    ANativeWindow* window = nullptr;
    AAssetManager* assetManager = nullptr;
    std::chrono::steady_clock::time_point splashStarted;

    void initializeWindow(ANativeWindow* newWindow, AAssetManager* assets) {
        window = newWindow;
        assetManager = assets;
        initialized = true;
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

        const float logoY = h * .38f;
        renderer.roundedRect(cx - 64.f, logoY - 64.f, 128.f, 128.f, kPanel);
        renderer.roundedRect(cx - 48.f, logoY - 48.f, 96.f, 96.f, kPanel2);
        renderer.rotatedRect(cx, logoY, 76.f, 13.f, 0.785398f, kGreen);
        renderer.rotatedRect(cx, logoY, 76.f, 13.f, -0.785398f, kGreen);
        renderer.rotatedRect(cx, logoY, 14.f, 14.f, 0.785398f, kText);

        renderer.text("XENON", cx - 58.f, h * .58f, 8, kText);
        renderer.text("EXPERIMENTAL EMULATOR", cx - 108.f, h * .58f + 58.f, 3, kMuted);

        float barW = w * .55f;
        float progress = std::clamp(elapsed / 2.7f, 0.f, 1.f);
        renderer.roundedRect(cx - barW * .5f, h * .78f, barW, 6.f, kPanel2);
        renderer.roundedRect(cx - barW * .5f, h * .78f, barW * progress, 6.f, kGreen);
    }

    // Avatar simples e genérico (boneco em blocos), sem relação com nenhum
    // personagem real: só pra dar um corpo visual a cada perfil no seletor.
    void drawAvatarFigure(float cx, float topY, const Color& accent) {
        float headSize = 40.f;
        float torsoW = 92.f;
        float torsoH = 78.f;
        float legW = 32.f;
        float legH = 48.f;
        float legGap = 10.f;

        renderer.roundedRect(cx - headSize * .5f, topY, headSize, headSize, kPanel2);
        renderer.roundedRect(cx - torsoW * .5f, topY + headSize + 6.f, torsoW, torsoH, kPanel2);
        renderer.rect(cx - torsoW * .5f + 10.f, topY + headSize + 20.f, torsoW - 20.f, 14.f, accent);
        renderer.roundedRect(cx - legGap * .5f - legW, topY + headSize + 6.f + torsoH, legW, legH, kMuted);
        renderer.roundedRect(cx + legGap * .5f, topY + headSize + 6.f + torsoH, legW, legH, kMuted);
    }

    struct ProfileSlot { const char* name; const char* sub; bool create; Color accent; };

    static const ProfileSlot* profileSlots() {
        static const ProfileSlot slots[4] = {
            {"JOGADOR UM", "ONLINE", false, kGreen},
            {"JOGADOR DOIS", "ONLINE", false, kBlue},
            {"CRIAR PERFIL", "NOVO PERFIL", true, kPanel2},
            {"CONVIDADO", "OFFLINE", false, kMuted},
        };
        return slots;
    }

    // Caixa de toque (e posição central) de cada um dos 4 cartões, usada
    // tanto pro desenho quanto pra detectar o toque.
    void profileCardLayout(Rect out[4], float cx[4]) const {
        float w = static_cast<float>(renderer.width);
        float cardW = std::min(w * .2f, 230.f);
        float gap = (w - cardW * 4.f) / 5.f;
        float top = static_cast<float>(renderer.height) * .30f;
        for (int i = 0; i < 4; ++i) {
            cx[i] = gap + cardW * .5f + i * (cardW + gap);
            out[i] = Rect{gap + i * (cardW + gap), top - 20.f, cardW, 260.f};
        }
    }

    void drawProfileSelect() {
        float w = static_cast<float>(renderer.width);
        float h = static_cast<float>(renderer.height);
        float margin = std::clamp(w * .06f, 18.f, 42.f);

        renderer.text("ENTRAR OU SAIR", w - margin - 236.f, 30.f, 3.3f, kText);
        renderer.text("ESCOLHA O SEU PERFIL", w - margin - 236.f, 64.f, 2.5f, kMuted);

        const ProfileSlot* slots = profileSlots();
        Rect cards[4];
        float centers[4];
        profileCardLayout(cards, centers);

        for (int i = 0; i < 4; ++i) {
            const ProfileSlot& slot = slots[i];
            float cx = centers[i];
            float top = cards[i].y + 20.f;

            if (slot.create) {
                renderer.roundedRect(cx - 46.f, top, 92.f, 92.f, kPanel);
                renderer.rect(cx - 24.f, top + 42.f, 48.f, 8.f, kMuted);
                renderer.rect(cx - 4.f, top + 22.f, 8.f, 48.f, kMuted);
            } else {
                drawAvatarFigure(cx, top, slot.accent);
                renderer.roundedRect(cx + 28.f, top, 16.f, 16.f, slot.accent);
            }

            float nameWidth = static_cast<float>(std::strlen(slot.name)) * 6.f * 2.6f;
            renderer.text(slot.name, cx - nameWidth * .5f, top + 200.f, 2.6f, kText);
            float subWidth = static_cast<float>(std::strlen(slot.sub)) * 6.f * 2.1f;
            renderer.text(slot.sub, cx - subWidth * .5f, top + 230.f, 2.1f, kMuted);
        }

        float barHeight = std::clamp(h * .09f, 58.f, 82.f);
        renderer.rect(0, h - barHeight, w, barHeight, kPanel);
        renderer.text("A SELECIONAR", margin, h - barHeight + 26.f, 2.6f, kText);
        const char* backLabel = "B VOLTAR";
        float backWidth = static_cast<float>(std::strlen(backLabel)) * 6.f * 2.6f;
        renderer.text(backLabel, w - margin - backWidth, h - barHeight + 26.f, 2.6f, kMuted);
    }

    void handleProfileSelectTouch(float x, float y) {
        Rect cards[4];
        float centers[4];
        profileCardLayout(cards, centers);
        for (int i = 0; i < 4; ++i) {
            const Rect& r = cards[i];
            if (x >= r.x && x <= r.x + r.w && y >= r.y && y <= r.y + r.h) {
                // Ainda não há criação/gestão real de perfil: qualquer cartão
                // (inclusive "Criar Perfil") só abre o dashboard por enquanto.
                screen = Screen::Home;
                return;
            }
        }
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
            case Screen::ProfileSelect: drawProfileSelect(); break;
        }
        if (screen != Screen::ProfileSelect) drawNavigation();
        renderer.present();
    }

    void handleTouch(float x, float y) {
        if (screen == Screen::ProfileSelect) {
            handleProfileSelectTouch(x, y);
            return;
        }
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
        state->handleTouch(
            state->renderer.logicalX(AMotionEvent_getX(event, 0)),
            state->renderer.logicalY(AMotionEvent_getY(event, 0)));
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
