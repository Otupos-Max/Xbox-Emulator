#include <android_native_app_glue.h>
#include <android/asset_manager.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <aaudio/AAudio.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <jni.h>
#include "ProfileSelectionScreen.hpp"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <utility>
#include <sys/types.h>
#include <vector>
#include <unistd.h>

namespace {

struct PendingProfile {
    std::mutex mutex;
    std::string name;
    bool female = false;
    bool ready = false;
};

PendingProfile pendingProfile;

bool takePendingProfile(std::string& name, bool& female) {
    std::lock_guard<std::mutex> lock(pendingProfile.mutex);
    if (!pendingProfile.ready) return false;
    name = std::move(pendingProfile.name);
    female = pendingProfile.female;
    pendingProfile.ready = false;
    return true;
}

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
    size_t vertexBufferCapacity = 8;
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
            EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 16,
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
        vertexBufferCapacity = 8;

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
        vertexBufferCapacity = 8;
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
        std::vector<float> textVertices;
        textVertices.reserve(value.size() * 180);
        for (size_t byte = 0; byte < value.size();) {
            const unsigned char first = static_cast<unsigned char>(value[byte++]);
            uint32_t codepoint = first;
            if ((first & 0xE0u) == 0xC0u && byte < value.size()) {
                codepoint = ((first & 0x1Fu) << 6) |
                    (static_cast<unsigned char>(value[byte++]) & 0x3Fu);
            } else if ((first & 0xF0u) == 0xE0u && byte + 1 < value.size()) {
                const unsigned char second = static_cast<unsigned char>(value[byte++]);
                const unsigned char third = static_cast<unsigned char>(value[byte++]);
                codepoint = ((first & 0x0Fu) << 12) | ((second & 0x3Fu) << 6) |
                    (third & 0x3Fu);
            }
            char ch = codepoint <= 0x7Fu ? static_cast<char>(codepoint) : '?';
            switch (codepoint) {
                case 0x00C7: case 0x00E7: ch = 'C'; break;
                case 0x00C3: case 0x00E3: case 0x00C1: case 0x00E1:
                case 0x00C0: case 0x00E0: case 0x00C2: case 0x00E2: ch = 'A'; break;
                case 0x00C9: case 0x00E9: case 0x00CA: case 0x00EA: ch = 'E'; break;
                case 0x00CD: case 0x00ED: ch = 'I'; break;
                case 0x00D3: case 0x00F3: case 0x00D4: case 0x00F4: ch = 'O'; break;
                case 0x00DA: case 0x00FA: ch = 'U'; break;
                default: break;
            }
            if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
            const size_t glyphIndex = font.find(ch);
            int index = glyphIndex == std::string::npos
                ? -1 : static_cast<int>(glyphIndex);
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
                        static const std::string patterns[] = {
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
                    if (on) {
                        const float left = (x + col * scale) / static_cast<float>(width);
                        const float top = (y + row * scale) / static_cast<float>(height);
                        const float right = (x + (col + .82f) * scale) / static_cast<float>(width);
                        const float bottom = (y + (row + .82f) * scale) / static_cast<float>(height);
                        textVertices.insert(textVertices.end(), {
                            left, top, right, top, left, bottom,
                            left, bottom, right, top, right, bottom
                        });
                    }
                }
            }
            x += (glyphWidth + 1) * scale;
        }
        if (textVertices.empty()) return;

        glUseProgram(program);
        glUniform2f(resolution, static_cast<float>(width), static_cast<float>(height));
        glUniform4f(rectLocation, 0.f, 0.f, static_cast<float>(width),
                    static_cast<float>(height));
        glUniform4f(colorLocation, c.r, c.g, c.b, c.a);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        if (textVertices.size() > vertexBufferCapacity) {
            vertexBufferCapacity = std::max(textVertices.size(), vertexBufferCapacity * 2);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(vertexBufferCapacity * sizeof(float)),
                         nullptr, GL_STREAM_DRAW);
        }
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(textVertices.size() * sizeof(float)),
                        textVertices.data());
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(textVertices.size() / 2));
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
    xenon::ProfileSelectionScreen profileSelection;
    Screen screen = Screen::ProfileSelect;
    bool initialized = false;
    bool rendererInitialized = false;
    bool showingSplash = true;
    bool playingSplashVideo = false;
    ANativeWindow* window = nullptr;
    ANativeActivity* activity = nullptr;
    AAssetManager* assetManager = nullptr;
    int focusedNavigation = static_cast<int>(Screen::Home);
    int controllerDirection = 0;
    std::chrono::steady_clock::time_point controllerRepeatAt;
    std::string profileStoragePath;
    std::chrono::steady_clock::time_point splashStarted;
    std::chrono::steady_clock::time_point profileSelectionStarted;

    void initializeRenderer() {
        rendererInitialized = renderer.initialize(window);
        if (!rendererInitialized) {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon", "Unable to initialize the app renderer");
            return;
        }
        if (!profileSelection.initialize(assetManager, profileStoragePath)) {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon",
                                "Profile selection initialization failed; check model assets and GLES support");
        }
        profileSelectionStarted = std::chrono::steady_clock::now();
    }

    void initializeWindow(ANativeWindow* newWindow, ANativeActivity* nativeActivity,
                          AAssetManager* assets,
                          const char* storagePath) {
        window = newWindow;
        activity = nativeActivity;
        assetManager = assets;
        profileStoragePath = storagePath ? storagePath : "";
        initialized = true;
        splashStarted = std::chrono::steady_clock::now();

        profileSelection.shutdown();
        renderer.shutdown();
        rendererInitialized = false;
        if (showingSplash && splashVideo.start(assetManager, window)) {
            playingSplashVideo = true;
            return;
        }

        playingSplashVideo = false;
        initializeRenderer();
    }

    bool hit(float px, float py, const Rect& r) const {
        return px >= r.x && px <= r.x + r.w && py >= r.y && py <= r.y + r.h;
    }

    void smoothText(const char* text, float x, float y, float scale, const Color& c) {
        profileSelection.drawText(text, x, y, scale, c.r, c.g, c.b, c.a);
    }

    float smoothTextWidth(const char* text, float scale) const {
        return profileSelection.textWidth(text, scale);
    }

    void drawNavigation() {
        float w = static_cast<float>(renderer.width);
        float h = static_cast<float>(renderer.height);
        float barHeight = std::clamp(h * .09f, 58.f, 82.f);
        renderer.rect(0, h - barHeight, w, barHeight, kBg);
        renderer.rect(0, h - barHeight, w, 1, kPanel2);

        const char* labels[] = {"INICIO", "BIBLIOTECA", "SISTEMA", "AJUSTES"};
        for (int i = 0; i < 4; ++i) {
            float itemW = w / 4.f;
            float x = itemW * i;
            bool active = static_cast<int>(screen) == i;
            bool focused = focusedNavigation == i;
            if (active || focused) {
                renderer.roundedRect(x + itemW * .18f, h - barHeight + 10,
                                     itemW * .64f, barHeight - 20, kPanel);
                renderer.roundedRect(x + itemW * .36f, h - barHeight + 16,
                                     itemW * .28f, 4, focused ? kGreen : kPanel2);
            }
            const float labelScale = 3.1f;
            const float labelWidth = smoothTextWidth(labels[i], labelScale);
            smoothText(labels[i], x + itemW * .5f - labelWidth * .5f,
                       h - barHeight + 40.f, labelScale,
                       active || focused ? kGreen : kMuted);
        }
    }

    void drawHome() {
        float w = static_cast<float>(renderer.width);
        float h = static_cast<float>(renderer.height);
        const float margin = std::clamp(w * .045f, 28.f, 48.f);

        renderer.roundedRect(margin, 24.f, 46.f, 46.f, kGreen);
        renderer.rotatedRect(margin + 23.f, 47.f, 29.f, 5.f, .785398f, kBg);
        renderer.rotatedRect(margin + 23.f, 47.f, 29.f, 5.f, -.785398f, kBg);
        smoothText("XENON", margin + 58.f, 25.f, 6.5f, kText);
        smoothText("PAINEL PRINCIPAL", margin + 58.f, 59.f, 2.8f, kGreen);

        const float profileW = 294.f;
        const float profileX = w - margin - profileW;
        renderer.roundedRect(profileX, 22.f, profileW, 58.f, kPanel);
        renderer.roundedRect(profileX + 12.f, 35.f, 32.f, 32.f, kGreen);
        smoothText("X", profileX + 21.f, 42.f, 3.f, kBg);
        smoothText("PERFIL ATIVO", profileX + 56.f, 30.f, 2.1f, kMuted);
        const char* activeName = profileSelection.activeProfileName();
        const float nameScale = std::min(
            3.f, 220.f / std::max(smoothTextWidth(activeName, 1.f), 1.f));
        smoothText(activeName, profileX + 56.f, 51.f, nameScale, kText);

        const float heroY = 100.f;
        const float heroH = std::min(h * .36f, 258.f);
        const float heroW = w - margin * 2.f;
        renderer.roundedRect(margin, heroY, heroW, heroH, kPanel);
        renderer.roundedRect(margin, heroY, 8.f, heroH, kGreen);
        smoothText("EMULADOR XBOX 360", margin + 30.f, heroY + 30.f, 2.8f, kGreen);
        smoothText("SEU XBOX, NO CELULAR", margin + 30.f, heroY + 76.f, 6.8f, kText);
        smoothText("Biblioteca e emulador, tudo em um so lugar.",
                   margin + 30.f, heroY + 112.f, 3.2f, kMuted);
        const float openW = 244.f;
        const float openY = heroY + heroH - 58.f;
        renderer.roundedRect(margin + 30.f, openY, openW, 40.f, kGreen);
        const char* openText = "VER BIBLIOTECA";
        smoothText(openText, margin + 30.f + (openW - smoothTextWidth(openText, 2.6f)) * .5f,
                   openY + 12.f, 2.6f, kBg);

        const float markSize = std::min(174.f, heroH - 38.f);
        const float markX = w - margin - markSize - 42.f;
        const float markY = heroY + (heroH - markSize) * .5f;
        renderer.roundedRect(markX, markY, markSize, markSize,
                             Color{.89f, .94f, .91f, 1.f});
        renderer.rotatedRect(markX + markSize * .5f, markY + markSize * .5f,
                             markSize * .62f, 11.f, .785398f, kGreen);
        renderer.rotatedRect(markX + markSize * .5f, markY + markSize * .5f,
                             markSize * .62f, 11.f, -.785398f, kGreen);

        const float cardsY = heroY + heroH + 16.f;
        const float cardH = 100.f;
        const float cardW = (w - margin * 2.f - 16.f) / 2.f;
        renderer.roundedRect(margin, cardsY, cardW, cardH, kBg);
        renderer.rect(margin, cardsY, cardW, 1.f, kPanel2);
        renderer.roundedRect(margin + 18.f, cardsY + 22.f, 38.f, 38.f, kPanel);
        smoothText("01", margin + 26.f, cardsY + 33.f, 3.f, kGreen);
        smoothText("BIBLIOTECA", margin + 70.f, cardsY + 30.f, 3.8f, kText);
        smoothText("Nenhum titulo carregado", margin + 70.f, cardsY + 58.f, 2.5f, kMuted);

        const float secondX = margin + cardW + 16.f;
        renderer.roundedRect(secondX, cardsY, cardW, cardH, kBg);
        renderer.rect(secondX, cardsY, cardW, 1.f, kPanel2);
        renderer.roundedRect(secondX + 18.f, cardsY + 22.f, 38.f, 38.f, kGreen);
        smoothText("ON", secondX + 25.f, cardsY + 34.f, 2.6f, kBg);
        smoothText("SISTEMA", secondX + 70.f, cardsY + 30.f, 3.8f, kText);
        smoothText("Emulador experimental", secondX + 70.f, cardsY + 58.f, 2.5f, kMuted);

        const float statusY = cardsY + cardH + 12.f;
        renderer.roundedRect(margin, statusY, w - margin * 2.f, 44.f, kPanel);
        renderer.roundedRect(margin + 14.f, statusY + 13.f, 18.f, 18.f, kGreen);
        smoothText("SISTEMA PRONTO", margin + 44.f, statusY + 17.f, 2.5f, kText);
        smoothText("TOQUE NOS CARTOES  |  CONTROLE: SETAS NAVEGAM  A ABRE  X PERFIS",
                   margin + 250.f, statusY + 17.f, 2.1f, kMuted);
    }

    void drawLibrary() {
        float w = static_cast<float>(renderer.width);
        float h = static_cast<float>(renderer.height);
        float margin = std::clamp(w * .06f, 18.f, 42.f);
        smoothText("BIBLIOTECA", margin, 42, 7.f, kText);
        smoothText("Sua colecao de jogos", margin, 72, 3.2f, kMuted);
        float cardY = 118.f;
        renderer.roundedRect(margin, cardY, w - margin * 2, h - cardY - 112.f, kPanel);
        renderer.roundedRect(margin + 32.f, cardY + 46.f, 82.f, 82.f, kBg);
        renderer.roundedRect(margin + 55.f, cardY + 69.f, 36.f, 36.f, kGreen);
        smoothText("X", margin + 67.f, cardY + 80.f, 4.0f, kBg);
        smoothText("Nenhum jogo encontrado", margin + 138.f, cardY + 58.f, 5.0f, kText);
        smoothText("Adicione um arquivo XEX para comecar.", margin + 138.f,
                   cardY + 92.f, 3.2f, kMuted);
        renderer.roundedRect(margin + 138.f, cardY + 128.f, 190.f, 44.f, kGreen);
        const char* addText = "ADICIONAR JOGO";
        smoothText(addText, margin + 138.f + (190.f - smoothTextWidth(addText, 3.1f)) * .5f,
                   cardY + 141.f, 3.1f, kBg);
    }

    void drawSystem() {
        float w = static_cast<float>(renderer.width);
        float h = static_cast<float>(renderer.height);
        float margin = std::clamp(w * .06f, 18.f, 42.f);
        smoothText("SISTEMA", margin, 42, 7.f, kText);
        smoothText("Resumo do emulador", margin, 72, 3.2f, kMuted);

        float cardY = 118.f;
        float cardW = (w - margin * 2 - 24.f) / 2.f;
        renderer.roundedRect(margin, cardY, cardW, 210.f, kPanel);
        smoothText("MEMORIA", margin + 24.f, cardY + 24.f, 3.1f, kMuted);
        smoothText("512 MB", margin + 24.f, cardY + 68.f, 8.f, kText);
        smoothText("RAM UNIFICADA", margin + 24.f, cardY + 112.f, 3.2f, kGreen);
        renderer.roundedRect(margin + 24.f, cardY + 158.f, cardW - 48.f, 10.f, kPanel2);
        renderer.roundedRect(margin + 24.f, cardY + 158.f, (cardW - 48.f) * .18f, 10.f, kGreen);

        renderer.roundedRect(margin + cardW + 24.f, cardY, cardW, 210.f, kBg);
        renderer.rect(margin + cardW + 24.f, cardY, cardW, 1.f, kPanel2);
        smoothText("PROCESSADOR", margin + cardW + 48.f, cardY + 24.f, 3.1f, kMuted);
        smoothText("XENON", margin + cardW + 48.f, cardY + 68.f, 7.f, kText);
        smoothText("3 nucleos  /  6 threads", margin + cardW + 48.f,
                   cardY + 112.f, 3.3f, kGreen);
        smoothText("Arquitetura PowerPC", margin + cardW + 48.f,
                   cardY + 158.f, 3.0f, kMuted);

        float gpuY = cardY + 234.f;
        renderer.roundedRect(margin, gpuY, w - margin * 2, 92.f, kPanel);
        renderer.roundedRect(margin + 18.f, gpuY + 18.f, 56.f, 56.f, kBg);
        smoothText("GPU", margin + 29.f, gpuY + 38.f, 3.1f, kGreen);
        smoothText("XENOS", margin + 92.f, gpuY + 22.f, 4.5f, kText);
        smoothText("Nao iniciada", margin + 92.f, gpuY + 52.f, 3.2f, kMuted);
    }

    void drawSettings() {
        float w = static_cast<float>(renderer.width);
        float h = static_cast<float>(renderer.height);
        float margin = std::clamp(w * .06f, 18.f, 42.f);
        smoothText("AJUSTES", margin, 42, 7.f, kText);
        smoothText("Configuracoes do aplicativo", margin, 72, 3.2f, kMuted);
        for (int i = 0; i < 3; ++i) {
            float y = 112.f + i * 92.f;
            renderer.roundedRect(margin, y, w - margin * 2, 76, kPanel);
            const char* labels[] = {"VERIFICAR MEMORIA", "RASTREIO MMIO", "RELATORIO DE COMPATIBILIDADE"};
            const char* descriptions[] = {"Executar diagnostico rapido",
                                          "Monitorar chamadas do sistema",
                                          "Gerar dados para depuracao"};
            smoothText(labels[i], margin + 24.f, y + 20.f, 3.5f, kText);
            smoothText(descriptions[i], margin + 24.f, y + 43.f, 2.7f, kMuted);
            renderer.roundedRect(w - margin - 82.f, y + 24.f, 58.f, 28.f,
                                 i == 0 ? kGreen : kPanel2);
            renderer.roundedRect(w - margin - (i == 0 ? 59.f : 75.f),
                                 y + 29.f, 18.f, 18.f, i == 0 ? kBg : kMuted);
        }
        (void)h;
    }

    void drawSplash() {
        auto elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - splashStarted).count();
        if (elapsed >= 2.7f) {
            showingSplash = false;
            profileSelectionStarted = std::chrono::steady_clock::now();
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

    void drawProfileSelect() {
        static const auto drawRect = [](void* context, float x, float y, float width,
                                        float height, float r, float g, float b, float a) {
            auto* target = static_cast<Renderer*>(context);
            target->rect(x, y, width, height, Color{r, g, b, a});
        };
        const xenon::ProfileDrawApi api{&renderer, drawRect};
        const float elapsed = std::chrono::duration<float>(
            std::chrono::steady_clock::now() - profileSelectionStarted).count();
        profileSelection.draw(static_cast<float>(renderer.width),
                              static_cast<float>(renderer.height), elapsed, api);
    }

    void requestCreateProfileDialog() {
        if (!activity || !activity->vm || !activity->clazz) {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon",
                                "Cannot open profile dialog: Android activity is unavailable");
            return;
        }

        JNIEnv* env = nullptr;
        bool attached = false;
        const jint status = activity->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
            if (activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
                __android_log_print(ANDROID_LOG_ERROR, "Xenon",
                                    "Cannot attach to Android VM to open profile dialog");
                return;
            }
            attached = true;
        } else if (status != JNI_OK) {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon",
                                "Cannot access Android VM to open profile dialog");
            return;
        }

        jclass activityClass = env->GetObjectClass(activity->clazz);
        jmethodID showDialog = activityClass
            ? env->GetMethodID(activityClass, "showCreateProfileDialog", "()V") : nullptr;
        if (showDialog) {
            env->CallVoidMethod(activity->clazz, showDialog);
        }
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            __android_log_print(ANDROID_LOG_ERROR, "Xenon",
                                "Android profile dialog raised an exception");
        } else if (!showDialog) {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon",
                                "Android profile dialog method is missing");
        }
        if (activityClass) env->DeleteLocalRef(activityClass);
        if (attached) activity->vm->DetachCurrentThread();
    }

    void applyProfileAction(xenon::ProfileInputAction action) {
        if (action == xenon::ProfileInputAction::Confirm) {
            profileSelection.confirmSelection();
            if (profileSelection.hasActiveProfile()) screen = Screen::Home;
        } else if (action == xenon::ProfileInputAction::CreateProfile) {
            requestCreateProfileDialog();
        } else if (action == xenon::ProfileInputAction::Back) {
            profileSelection.cancelSelection();
            screen = Screen::Home;
        }
    }

    void draw() {
        if (playingSplashVideo) {
            if (!splashVideo.renderFrame()) {
                playingSplashVideo = false;
                showingSplash = false;
                initializeRenderer();
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
            applyProfileAction(profileSelection.handleTouch(
                x, y, static_cast<float>(renderer.width), static_cast<float>(renderer.height)));
            return;
        }
        float w = static_cast<float>(renderer.width);
        float h = static_cast<float>(renderer.height);
        const float margin = std::clamp(w * .045f, 28.f, 48.f);
        const float profileW = 294.f;
        const float profileX = w - margin - profileW;
        if (screen == Screen::Home && y >= 22.f && y < 80.f &&
            x >= profileX && x < profileX + profileW) {
            profileSelection.setSelectedProfile(profileSelection.activeProfile());
            screen = Screen::ProfileSelect;
            return;
        }
        const float heroY = 100.f;
        const float heroH = std::min(h * .36f, 258.f);
        if (screen == Screen::Home && y >= heroY && y < heroY + heroH &&
            x >= margin && x < w - margin) {
            focusedNavigation = static_cast<int>(Screen::Library);
            screen = Screen::Library;
            return;
        }
        const float cardY = heroY + heroH + 16.f;
        const float cardW = (w - margin * 2.f - 16.f) / 2.f;
        if (screen == Screen::Home && y >= cardY && y < cardY + 100.f) {
            if (x >= margin && x < margin + cardW) {
                focusedNavigation = static_cast<int>(Screen::Library);
            } else if (x >= margin + cardW + 16.f && x < w - margin) {
                focusedNavigation = static_cast<int>(Screen::System);
            } else {
                return;
            }
            screen = static_cast<Screen>(focusedNavigation);
            return;
        }
        float barHeight = std::clamp(h * .09f, 58.f, 82.f);
        if (y >= h - barHeight) {
            int index = std::clamp(static_cast<int>(x / (w / 4.f)), 0, 3);
            focusedNavigation = index;
            screen = static_cast<Screen>(index);
            return;
        }
    }

    void handleKey(int keyCode) {
        if (screen == Screen::ProfileSelect) {
            applyProfileAction(profileSelection.handleKey(keyCode));
            return;
        }
        switch (keyCode) {
            case AKEYCODE_DPAD_LEFT:
                focusedNavigation = (focusedNavigation + 3) % 4;
                break;
            case AKEYCODE_DPAD_RIGHT:
                focusedNavigation = (focusedNavigation + 1) % 4;
                break;
            case AKEYCODE_DPAD_CENTER:
            case AKEYCODE_ENTER:
            case AKEYCODE_BUTTON_A:
            case AKEYCODE_BUTTON_START:
                screen = static_cast<Screen>(focusedNavigation);
                break;
            case AKEYCODE_BUTTON_X:
                profileSelection.setSelectedProfile(profileSelection.activeProfile());
                screen = Screen::ProfileSelect;
                break;
            case AKEYCODE_BACK:
            case AKEYCODE_ESCAPE:
            case AKEYCODE_BUTTON_B:
            case AKEYCODE_BUTTON_SELECT:
                if (screen == Screen::Home) {
                    profileSelection.setSelectedProfile(profileSelection.activeProfile());
                    screen = Screen::ProfileSelect;
                } else {
                    screen = Screen::Home;
                    focusedNavigation = static_cast<int>(Screen::Home);
                }
                break;
            default:
                break;
        }
    }

    bool handleCreatedProfile(const std::string& name, bool female) {
        if (profileSelection.createProfile(name, female)) {
            screen = Screen::Home;
            return true;
        } else {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon",
                                "Profile creation failed; check the profile name and storage");
            return false;
        }
    }

    void notifyProfileCreationResult(bool success, const char* message) {
        if (!activity || !activity->vm || !activity->clazz) {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon",
                                "Cannot report profile result: Android activity is unavailable");
            return;
        }
        JNIEnv* env = nullptr;
        bool attached = false;
        jint status = activity->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
            if (activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
                __android_log_print(ANDROID_LOG_ERROR, "Xenon",
                                    "Cannot attach to Android VM to report profile result");
                return;
            }
            attached = true;
        } else if (status != JNI_OK) {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon",
                                "Cannot access Android VM to report profile result");
            return;
        }

        jclass activityClass = env->GetObjectClass(activity->clazz);
        jmethodID resultMethod = activityClass
            ? env->GetMethodID(activityClass, "onProfileCreationResult",
                               "(ZLjava/lang/String;)V") : nullptr;
        jstring javaMessage = env->NewStringUTF(message);
        if (resultMethod && javaMessage) {
            env->CallVoidMethod(activity->clazz, resultMethod,
                                success ? JNI_TRUE : JNI_FALSE, javaMessage);
        }
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            __android_log_print(ANDROID_LOG_ERROR, "Xenon",
                                "Android profile result callback raised an exception");
        } else if (!resultMethod || !javaMessage) {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon",
                                "Android profile result callback is unavailable");
        }
        if (javaMessage) env->DeleteLocalRef(javaMessage);
        if (activityClass) env->DeleteLocalRef(activityClass);
        if (attached) activity->vm->DetachCurrentThread();
    }

    void handleControllerMotion(const AInputEvent* event) {
        const float leftX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_X, 0);
        float rightX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RX, 0);
        const float axisZ = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Z, 0);
        const float axisRz = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RZ, 0);
        if (std::abs(axisZ) > std::abs(rightX)) rightX = axisZ;
        if (std::abs(axisRz) > std::abs(rightX)) rightX = axisRz;
        const float hatX = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_X, 0);
        if (screen == Screen::ProfileSelect) {
            applyProfileAction(profileSelection.handleJoystick(leftX, rightX, hatX));
            return;
        }

        float horizontal = std::abs(leftX) >= std::abs(rightX) ? leftX : rightX;
        if (std::abs(hatX) > .5f) horizontal = hatX;
        const int direction = horizontal < -.55f ? -1 : horizontal > .55f ? 1 : 0;
        if (direction == 0) {
            controllerDirection = 0;
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (direction == controllerDirection && now < controllerRepeatAt) return;
        controllerDirection = direction;
        controllerRepeatAt = now + std::chrono::milliseconds(300);
        focusedNavigation = (focusedNavigation + 4 + direction) % 4;
    }
};

void onAppCmd(android_app* app, int32_t cmd) {
    auto* state = reinterpret_cast<AppState*>(app->userData);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window && state) {
                state->initializeWindow(app->window, app->activity,
                                        app->activity->assetManager,
                                        app->activity->internalDataPath);
            }
            break;
        case APP_CMD_TERM_WINDOW:
            if (state) {
                state->splashVideo.stop();
                state->playingSplashVideo = false;
                state->profileSelection.shutdown();
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
    if (!state) return 0;
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_KEY) {
        if (AKeyEvent_getAction(event) == AKEY_EVENT_ACTION_DOWN && !state->showingSplash) {
            state->handleKey(AKeyEvent_getKeyCode(event));
            return 1;
        }
        return 0;
    }
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return 0;
    const int32_t source = AInputEvent_getSource(event);
    const bool gamepadSource =
        (source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK ||
        (source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD ||
        (source & AINPUT_SOURCE_DPAD) == AINPUT_SOURCE_DPAD;
    if (gamepadSource) {
        if (state->showingSplash) return 1;
        if ((AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK) ==
            AMOTION_EVENT_ACTION_MOVE) {
            state->handleControllerMotion(event);
        }
        return 1;
    }
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

extern "C" JNIEXPORT jboolean JNICALL
Java_com_xboxemulator_XenonActivity_nativeOnProfileCreated(
    JNIEnv* env, jobject, jstring profileName, jboolean female) {
    if (!profileName) return JNI_FALSE;
    const char* utfName = env->GetStringUTFChars(profileName, nullptr);
    if (!utfName) return JNI_FALSE;
    try {
        std::lock_guard<std::mutex> lock(pendingProfile.mutex);
        pendingProfile.name = utfName;
        pendingProfile.female = female == JNI_TRUE;
        pendingProfile.ready = true;
    } catch (const std::exception& error) {
        env->ReleaseStringUTFChars(profileName, utfName);
        __android_log_print(ANDROID_LOG_ERROR, "Xenon",
                            "Unable to queue the new profile: %s", error.what());
        return JNI_FALSE;
    }
    env->ReleaseStringUTFChars(profileName, utfName);
    return JNI_TRUE;
}

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
        std::string createdName;
        bool createdFemale = false;
        if (takePendingProfile(createdName, createdFemale)) {
            bool created = false;
            const char* message =
                "Não foi possível salvar o perfil. Verifique o espaço disponível e tente novamente.";
            try {
                created = state.handleCreatedProfile(createdName, createdFemale);
            } catch (const std::exception& error) {
                __android_log_print(ANDROID_LOG_ERROR, "Xenon",
                                    "Profile creation failed: %s", error.what());
                message = "Ocorreu um erro ao criar o perfil. Tente novamente.";
            }
            state.notifyProfileCreationResult(created, created ? "" : message);
        }
        if (state.initialized) state.draw();
    }
    state.profileSelection.shutdown();
    state.renderer.shutdown();
}
