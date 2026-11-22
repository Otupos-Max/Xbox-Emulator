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
#include "Core/Filesystem/XdvdfsImage.hpp"
#include "ProfileSelectionScreen.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <ctime>
#include <exception>
#include <fstream>
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

// Mesmo mecanismo do PendingProfile acima, mas para o termo de busca digitado
// no dialogo da Xenon Store (nativeOnStoreSearch, chamado pelo Java).
struct PendingSearch {
    std::mutex mutex;
    std::string query;
    bool ready = false;
};

PendingSearch pendingSearch;

struct StoreDownloadProgress {
    std::string gameId;
    uint64_t downloaded = 0;
    uint64_t total = 0;
    int state = 0;
};

struct PendingStoreDownloadProgress {
    std::mutex mutex;
    std::vector<StoreDownloadProgress> updates;
};
PendingStoreDownloadProgress pendingStoreDownloadProgress;

bool takePendingSearch(std::string& query) {
    std::lock_guard<std::mutex> lock(pendingSearch.mutex);
    if (!pendingSearch.ready) return false;
    query = std::move(pendingSearch.query);
    pendingSearch.ready = false;
    return true;
}

void takePendingStoreDownloadUpdates(std::vector<StoreDownloadProgress>& updates) {
    std::lock_guard<std::mutex> lock(pendingStoreDownloadProgress.mutex);
    updates.swap(pendingStoreDownloadProgress.updates);
}

enum class Screen : int { Home, Store, Library, Settings, About, ProfileSelect };

struct Rect { float x, y, w, h; };
struct Color { float r, g, b, a; };

// Dark theme with neon-green accents, to match the Xenon dashboard look.
constexpr Color kBg{0.035f, 0.047f, 0.043f, 1};
constexpr Color kPanel{0.071f, 0.090f, 0.082f, 1};
constexpr Color kPanel2{0.118f, 0.145f, 0.133f, 1};
constexpr Color kText{0.933f, 0.961f, 0.945f, 1};
constexpr Color kMuted{0.541f, 0.616f, 0.588f, 1};
constexpr Color kGreen{0.212f, 0.871f, 0.408f, 1};
constexpr Color kBlue{0.404f, 0.682f, 0.961f, 1};

// Geometria fixa da barra superior e da barra lateral de navegacao.
constexpr float kSidebarW = 220.f;
constexpr float kTopBarH = 64.f;
constexpr float kNavItemH = 60.f;
constexpr float kNavTopPad = 14.f;

// ---------------------------------------------------------------------------
// Xenon Store: cliente minimo da API publica de jogos. Sem biblioteca de JSON
// disponivel no projeto, o parser abaixo e um recursive-descent bem pequeno,
// o suficiente para o formato { count, games: [{ id, title, description,
// link, cover, screenshots, created_at }] } que a API devolve.
// ---------------------------------------------------------------------------

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };
    Type type = Type::Null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<JsonValue> arrayValue;
    std::vector<std::pair<std::string, JsonValue>> objectValue;

    const JsonValue* find(const std::string& key) const {
        for (const auto& entry : objectValue) {
            if (entry.first == key) return &entry.second;
        }
        return nullptr;
    }

    std::string asString(const std::string& fallback = "") const {
        if (type == Type::String) return stringValue;
        if (type == Type::Number) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.0f", numberValue);
            return buffer;
        }
        return fallback;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : s(text) {}

    bool parse(JsonValue& out) {
        skipWs();
        return parseValue(out);
    }

private:
    const std::string& s;
    size_t pos = 0;

    void skipWs() {
        while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
    }
    char peek() const { return pos < s.size() ? s[pos] : '\0'; }
    bool consume(char c) {
        if (peek() == c) { ++pos; return true; }
        return false;
    }

    bool parseValue(JsonValue& out) {
        skipWs();
        switch (peek()) {
            case '{': return parseObject(out);
            case '[': return parseArray(out);
            case '"': return parseString(out);
            case 't':
            case 'f': return parseBool(out);
            case 'n': return parseNull(out);
            default: return parseNumber(out);
        }
    }

    bool parseObject(JsonValue& out) {
        out.type = JsonValue::Type::Object;
        ++pos;
        skipWs();
        if (consume('}')) return true;
        while (true) {
            skipWs();
            JsonValue key;
            if (peek() != '"' || !parseString(key)) return false;
            skipWs();
            if (!consume(':')) return false;
            JsonValue value;
            if (!parseValue(value)) return false;
            out.objectValue.emplace_back(std::move(key.stringValue), std::move(value));
            skipWs();
            if (consume(',')) continue;
            if (consume('}')) break;
            return false;
        }
        return true;
    }

    bool parseArray(JsonValue& out) {
        out.type = JsonValue::Type::Array;
        ++pos;
        skipWs();
        if (consume(']')) return true;
        while (true) {
            JsonValue value;
            if (!parseValue(value)) return false;
            out.arrayValue.push_back(std::move(value));
            skipWs();
            if (consume(',')) continue;
            if (consume(']')) break;
            return false;
        }
        return true;
    }

    bool parseString(JsonValue& out) {
        out.type = JsonValue::Type::String;
        if (!consume('"')) return false;
        std::string& result = out.stringValue;
        while (pos < s.size()) {
            char c = s[pos++];
            if (c == '"') return true;
            if (c != '\\') {
                result += c;
                continue;
            }
            if (pos >= s.size()) return false;
            char esc = s[pos++];
            switch (esc) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'u': {
                    if (pos + 4 > s.size()) return false;
                    unsigned int code = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = s[pos++];
                        code <<= 4;
                        if (h >= '0' && h <= '9') code |= static_cast<unsigned int>(h - '0');
                        else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned int>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned int>(h - 'A' + 10);
                        else return false;
                    }
                    // So BMP (suficiente para titulos/descricoes de jogos).
                    if (code < 0x80) {
                        result += static_cast<char>(code);
                    } else if (code < 0x800) {
                        result += static_cast<char>(0xC0 | (code >> 6));
                        result += static_cast<char>(0x80 | (code & 0x3F));
                    } else {
                        result += static_cast<char>(0xE0 | (code >> 12));
                        result += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                        result += static_cast<char>(0x80 | (code & 0x3F));
                    }
                    break;
                }
                default: return false;
            }
        }
        return false;
    }

    bool parseBool(JsonValue& out) {
        if (s.compare(pos, 4, "true") == 0) {
            out.type = JsonValue::Type::Bool;
            out.boolValue = true;
            pos += 4;
            return true;
        }
        if (s.compare(pos, 5, "false") == 0) {
            out.type = JsonValue::Type::Bool;
            out.boolValue = false;
            pos += 5;
            return true;
        }
        return false;
    }

    bool parseNull(JsonValue& out) {
        if (s.compare(pos, 4, "null") == 0) {
            out.type = JsonValue::Type::Null;
            pos += 4;
            return true;
        }
        return false;
    }

    bool parseNumber(JsonValue& out) {
        size_t start = pos;
        if (peek() == '-') ++pos;
        while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
        if (peek() == '.') {
            ++pos;
            while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos;
            if (peek() == '+' || peek() == '-') ++pos;
            while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) ++pos;
        }
        if (pos == start) return false;
        out.type = JsonValue::Type::Number;
        out.numberValue = std::strtod(s.substr(start, pos - start).c_str(), nullptr);
        return true;
    }
};

// Um jogo do catalogo da Xenon Store, ja convertido dos campos do JSON.
struct GameEntry {
    std::string id;
    std::string title;
    std::string description;
    std::string link;
    std::string sha256;
    std::string cover;
    std::vector<std::string> screenshots;
    std::string createdAt;
};

bool parseGamesResponse(const std::string& text, std::vector<GameEntry>& out) {
    JsonValue root;
    JsonParser parser(text);
    if (!parser.parse(root) || root.type != JsonValue::Type::Object) return false;
    const JsonValue* gamesNode = root.find("games");
    if (!gamesNode || gamesNode->type != JsonValue::Type::Array) return false;

    out.clear();
    out.reserve(gamesNode->arrayValue.size());
    for (const auto& item : gamesNode->arrayValue) {
        if (item.type != JsonValue::Type::Object) continue;
        GameEntry entry;
        if (const auto* v = item.find("id")) entry.id = v->asString();
        if (const auto* v = item.find("title")) entry.title = v->asString();
        if (const auto* v = item.find("description")) entry.description = v->asString();
        if (const auto* v = item.find("link")) entry.link = v->asString();
        if (const auto* v = item.find("sha256")) entry.sha256 = v->asString();
        if (entry.sha256.empty()) {
            if (const auto* v = item.find("sha_256")) entry.sha256 = v->asString();
        }
        if (entry.sha256.empty()) {
            if (const auto* v = item.find("sha256_hash")) entry.sha256 = v->asString();
        }
        if (const auto* v = item.find("cover")) entry.cover = v->asString();
        if (const auto* v = item.find("created_at")) entry.createdAt = v->asString();
        if (const auto* v = item.find("screenshots"); v && v->type == JsonValue::Type::Array) {
            for (const auto& shot : v->arrayValue) {
                if (shot.type == JsonValue::Type::String) entry.screenshots.push_back(shot.stringValue);
            }
        }
        out.push_back(std::move(entry));
    }
    return true;
}

// Percent-encoding simples para o parametro ?q= da busca.
std::string urlEncode(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3);
    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else if (c == ' ') {
            out += '+';
        } else {
            out += '%';
            out += hex[(c >> 4) & 0xF];
            out += hex[c & 0xF];
        }
    }
    return out;
}

struct Renderer {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    GLuint program = 0;
    GLuint texProgram = 0;
    GLuint boxProgram = 0;
    GLuint vertexBuffer = 0;
    size_t vertexBufferCapacity = 8;
    GLint resolution = -1;
    GLint rectLocation = -1;
    GLint colorLocation = -1;
    GLint texResolution = -1;
    GLint texRectLocation = -1;
    GLint texAlphaLocation = -1;
    GLint texSamplerLocation = -1;
    GLint texRadiusLocation = -1;
    GLint boxResolution = -1;
    GLint boxRectLocation = -1;
    GLint boxColorLocation = -1;
    GLint boxCornersLocation = -1;
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

        // Segundo programa, usado só para desenhar o logo (textura PNG) como
        // um quad. Opcional: se falhar, o app continua com o simbolo vetorial.
        const char* texVertex = R"(precision highp float;
attribute vec2 position;
uniform vec2 resolution;
uniform vec4 rect;
varying vec2 vUV;
void main() {
    vUV = position;
    vec2 pixel = rect.xy + position * rect.zw;
    vec2 normalized = pixel / resolution * 2.0 - 1.0;
    gl_Position = vec4(normalized.x, -normalized.y, 0.0, 1.0);
})";
        const char* texFragment = R"(precision mediump float;
varying vec2 vUV;
uniform sampler2D image;
uniform float alpha;
uniform vec4 rect;
uniform float radius;
void main() {
    vec4 sampled = texture2D(image, vUV);
    // Cantos arredondados opcionais (radius = 0 mantem o retangulo reto).
    vec2 halfSize = rect.zw * 0.5;
    vec2 p = (vUV - 0.5) * rect.zw;
    float r = min(radius, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + r;
    float d = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
    float mask = radius > 0.0 ? 1.0 - smoothstep(-1.0, 1.0, d) : 1.0;
    gl_FragColor = vec4(sampled.rgb, sampled.a * alpha * mask);
})";
        GLuint texVs = compile(GL_VERTEX_SHADER, texVertex);
        GLuint texFs = compile(GL_FRAGMENT_SHADER, texFragment);
        if (texVs != 0 && texFs != 0) {
            texProgram = glCreateProgram();
            glAttachShader(texProgram, texVs);
            glAttachShader(texProgram, texFs);
            glBindAttribLocation(texProgram, 0, "position");
            glLinkProgram(texProgram);
            GLint texLinked = GL_FALSE;
            glGetProgramiv(texProgram, GL_LINK_STATUS, &texLinked);
            if (texLinked != GL_TRUE) {
                glDeleteProgram(texProgram);
                texProgram = 0;
            } else {
                texResolution = glGetUniformLocation(texProgram, "resolution");
                texRectLocation = glGetUniformLocation(texProgram, "rect");
                texAlphaLocation = glGetUniformLocation(texProgram, "alpha");
                texSamplerLocation = glGetUniformLocation(texProgram, "image");
                texRadiusLocation = glGetUniformLocation(texProgram, "radius");
            }
        }
        if (texVs != 0) glDeleteShader(texVs);
        if (texFs != 0) glDeleteShader(texFs);

        // Terceiro programa: retangulos com cantos arredondados de verdade
        // (SDF), usado por roundedRect(). Se falhar, roundedRect() cai para
        // um retangulo reto (rect()), entao nada quebra.
        const char* boxVertex = R"(precision highp float;
attribute vec2 position;
uniform vec2 resolution;
uniform vec4 rect;
varying vec2 vLocal;
void main() {
    vLocal = (position - 0.5) * rect.zw;
    vec2 pixel = rect.xy + position * rect.zw;
    vec2 normalized = pixel / resolution * 2.0 - 1.0;
    gl_Position = vec4(normalized.x, -normalized.y, 0.0, 1.0);
})";
        const char* boxFragment = R"(precision mediump float;
varying vec2 vLocal;
uniform vec4 rect;
uniform vec4 color;
uniform vec4 corners;
float sdRoundBox(vec2 p, vec2 b, float r) {
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
void main() {
    vec2 halfSize = rect.zw * 0.5;
    // corners = (superior-esquerdo, superior-direito, inferior-direito, inferior-esquerdo)
    float topRadius = vLocal.x < 0.0 ? corners.x : corners.y;
    float bottomRadius = vLocal.x < 0.0 ? corners.w : corners.z;
    float radius = vLocal.y < 0.0 ? topRadius : bottomRadius;
    radius = min(radius, min(halfSize.x, halfSize.y));
    float d = sdRoundBox(vLocal, halfSize, radius);
    float alpha = 1.0 - smoothstep(-1.0, 1.0, d);
    gl_FragColor = vec4(color.rgb, color.a * alpha);
})";
        GLuint boxVs = compile(GL_VERTEX_SHADER, boxVertex);
        GLuint boxFs = compile(GL_FRAGMENT_SHADER, boxFragment);
        if (boxVs != 0 && boxFs != 0) {
            boxProgram = glCreateProgram();
            glAttachShader(boxProgram, boxVs);
            glAttachShader(boxProgram, boxFs);
            glBindAttribLocation(boxProgram, 0, "position");
            glLinkProgram(boxProgram);
            GLint boxLinked = GL_FALSE;
            glGetProgramiv(boxProgram, GL_LINK_STATUS, &boxLinked);
            if (boxLinked != GL_TRUE) {
                glDeleteProgram(boxProgram);
                boxProgram = 0;
            } else {
                boxResolution = glGetUniformLocation(boxProgram, "resolution");
                boxRectLocation = glGetUniformLocation(boxProgram, "rect");
                boxColorLocation = glGetUniformLocation(boxProgram, "color");
                boxCornersLocation = glGetUniformLocation(boxProgram, "corners");
            }
        }
        if (boxVs != 0) glDeleteShader(boxVs);
        if (boxFs != 0) glDeleteShader(boxFs);

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
                if (texProgram != 0) glDeleteProgram(texProgram);
                if (boxProgram != 0) glDeleteProgram(boxProgram);
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
        texProgram = 0;
        boxProgram = 0;
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

    // Retangulo com os 4 cantos arredondados de verdade (SDF). Cai para um
    // retangulo reto se o terceiro programa de shader nao estiver disponivel.
    void roundedRect(float x, float y, float w, float h, const Color& c, float radius = 14.f) {
        roundedRectCorners(x, y, w, h, c, radius, radius, radius, radius);
    }

    // Mesma coisa, mas com um raio por canto (superior-esq, superior-dir,
    // inferior-dir, inferior-esq) -- usado para "costurar" blocos, como a
    // capa de um card que so precisa ser arredondada em cima.
    void roundedRectCorners(float x, float y, float w, float h, const Color& c,
                            float topLeft, float topRight, float bottomRight, float bottomLeft) {
        if (boxProgram == 0) {
            rect(x, y, w, h, c);
            return;
        }
        glUseProgram(boxProgram);
        glUniform2f(boxResolution, static_cast<float>(width), static_cast<float>(height));
        glUniform4f(boxRectLocation, x, y, w, h);
        glUniform4f(boxColorLocation, c.r, c.g, c.b, c.a);
        glUniform4f(boxCornersLocation, topLeft, topRight, bottomRight, bottomLeft);

        static const float vertices[] = {0,0, 1,0, 0,1, 1,1};
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
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

    // Desenha uma textura (usada apenas para o logo carregado de logo/logo.png).
    void texturedRect(float x, float y, float w, float h, GLuint texture, float alpha = 1.f,
                      float radius = 0.f) {
        if (texProgram == 0 || texture == 0) return;
        glUseProgram(texProgram);
        glUniform2f(texResolution, static_cast<float>(width), static_cast<float>(height));
        glUniform4f(texRectLocation, x, y, w, h);
        glUniform1f(texAlphaLocation, alpha);
        glUniform1f(texRadiusLocation, radius);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glUniform1i(texSamplerLocation, 0);

        static const float vertices[] = {0,0, 1,0, 0,1, 1,1};
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(vertices), vertices);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindTexture(GL_TEXTURE_2D, 0);
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

// Cria uma textura GLES2 RGBA8 a partir de pixels já decodificados.
GLuint createTextureFromRGBA(const uint8_t* pixels, int texWidth, int texHeight) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) return 0;
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texWidth, texHeight, 0, GL_RGBA,
                GL_UNSIGNED_BYTE, pixels);
    glBindTexture(GL_TEXTURE_2D, 0);
    return texture;
}

// ---------------------------------------------------------------------------
// Rede (Xenon Store): chamadas sincronas de volta pro Java (HttpURLConnection
// e BitmapFactory), sempre a partir de uma thread em segundo plano propria
// (nunca a thread de renderizacao nem a UI thread do Android).
// ---------------------------------------------------------------------------

bool attachJniEnv(ANativeActivity* activity, JNIEnv*& env, bool& attached) {
    attached = false;
    if (!activity || !activity->vm) return false;
    const jint status = activity->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return false;
        attached = true;
        return true;
    }
    return status == JNI_OK;
}

// Chama XenonActivity.httpGet(url) e devolve os bytes crus da resposta.
bool httpGetBytes(ANativeActivity* activity, const std::string& url, std::vector<uint8_t>& out) {
    if (!activity || !activity->clazz) return false;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (!attachJniEnv(activity, env, attached)) return false;

    bool ok = false;
    jclass activityClass = env->GetObjectClass(activity->clazz);
    jmethodID method = activityClass
        ? env->GetMethodID(activityClass, "httpGet", "(Ljava/lang/String;)[B") : nullptr;
    if (method) {
        jstring jurl = env->NewStringUTF(url.c_str());
        auto result = static_cast<jbyteArray>(env->CallObjectMethod(activity->clazz, method, jurl));
        if (env->ExceptionCheck()) {
            env->ExceptionDescribe();
            env->ExceptionClear();
            result = nullptr;
        }
        if (result) {
            const jsize length = env->GetArrayLength(result);
            out.resize(static_cast<size_t>(std::max(length, 0)));
            if (length > 0) {
                env->GetByteArrayRegion(result, 0, length, reinterpret_cast<jbyte*>(out.data()));
            }
            ok = true;
            env->DeleteLocalRef(result);
        }
        env->DeleteLocalRef(jurl);
    }
    if (activityClass) env->DeleteLocalRef(activityClass);
    if (attached) activity->vm->DetachCurrentThread();
    return ok;
}

// Chama XenonActivity.decodeBytesRGBA(bytes) e devolve pixels RGBA8 prontos
// para virar uma textura GLES2 (na thread de renderizacao).
bool decodeImageBytes(ANativeActivity* activity, const std::vector<uint8_t>& bytes,
                      std::vector<uint8_t>& rgba, int& outWidth, int& outHeight) {
    if (!activity || !activity->clazz || bytes.empty()) return false;
    JNIEnv* env = nullptr;
    bool attached = false;
    if (!attachJniEnv(activity, env, attached)) return false;

    bool ok = false;
    jclass activityClass = env->GetObjectClass(activity->clazz);
    jmethodID method = activityClass
        ? env->GetMethodID(activityClass, "decodeBytesRGBA", "([B)[I") : nullptr;
    if (method) {
        jbyteArray jbytes = env->NewByteArray(static_cast<jsize>(bytes.size()));
        if (jbytes) {
            env->SetByteArrayRegion(jbytes, 0, static_cast<jsize>(bytes.size()),
                                    reinterpret_cast<const jbyte*>(bytes.data()));
            auto result = static_cast<jintArray>(
                env->CallObjectMethod(activity->clazz, method, jbytes));
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
                result = nullptr;
            }
            if (result) {
                const jsize length = env->GetArrayLength(result);
                if (length >= 2) {
                    jint* data = env->GetIntArrayElements(result, nullptr);
                    outWidth = data[0];
                    outHeight = data[1];
                    if (outWidth > 0 && outHeight > 0 &&
                        static_cast<jsize>(2 + outWidth * outHeight) <= length) {
                        rgba.resize(static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight) * 4);
                        for (int i = 0; i < outWidth * outHeight; ++i) {
                            const uint32_t pixel = static_cast<uint32_t>(data[2 + i]);
                            rgba[i * 4 + 0] = static_cast<uint8_t>((pixel >> 16) & 0xFFu);
                            rgba[i * 4 + 1] = static_cast<uint8_t>((pixel >> 8) & 0xFFu);
                            rgba[i * 4 + 2] = static_cast<uint8_t>(pixel & 0xFFu);
                            rgba[i * 4 + 3] = static_cast<uint8_t>((pixel >> 24) & 0xFFu);
                        }
                        ok = true;
                    }
                    env->ReleaseIntArrayElements(result, data, JNI_ABORT);
                }
                env->DeleteLocalRef(result);
            }
            env->DeleteLocalRef(jbytes);
        }
    }
    if (activityClass) env->DeleteLocalRef(activityClass);
    if (attached) activity->vm->DetachCurrentThread();
    return ok;
}

// Lista de jogos da Xenon Store, buscada e atualizada em segundo plano.
// games/loading/hasError/hasLoadedOnce sao lidos pela thread de desenho a
// cada frame; o mutex protege apenas o vetor "games" em si.
struct CatalogService {
    std::mutex mutex;
    std::vector<GameEntry> games;
    std::atomic<bool> loading{false};
    std::atomic<bool> hasError{false};
    std::atomic<bool> hasLoadedOnce{false};
    std::atomic<uint64_t> requestToken{0};
    std::string currentQuery;

    void fetchGames(ANativeActivity* activity, std::string query) {
        currentQuery = query;
        loading = true;
        hasError = false;
        const uint64_t myToken = ++requestToken;
        std::thread([this, activity, query, myToken]() {
            std::string url = "https://xbox-game-atlas.lovable.app/api/public/games";
            if (!query.empty()) url += "?q=" + urlEncode(query);

            std::vector<uint8_t> bytes;
            const bool downloaded = httpGetBytes(activity, url, bytes);
            std::vector<GameEntry> parsed;
            bool parsedOk = false;
            if (downloaded) {
                std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
                parsedOk = parseGamesResponse(text, parsed);
            }

            std::lock_guard<std::mutex> lock(mutex);
            if (myToken != requestToken.load()) return; // uma busca mais nova ja comecou
            if (downloaded && parsedOk) {
                games = std::move(parsed);
                hasError = false;
            } else {
                games.clear();
                hasError = true;
            }
            hasLoadedOnce = true;
            loading = false;
        }).detach();
    }

    // Copia da lista atual pra desenhar sem segurar o lock durante o frame.
    std::vector<GameEntry> snapshot() {
        std::lock_guard<std::mutex> lock(mutex);
        return games;
    }

    bool findById(const std::string& id, GameEntry& out) {
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& game : games) {
            if (game.id == id) {
                out = game;
                return true;
            }
        }
        return false;
    }
};

// Uma imagem (capa ou captura de tela) ja baixada e decodificada, esperando
// para ser enviada pra GPU pela thread de renderizacao.
struct DecodedTexture {
    std::string url;
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba; // vazio == falhou ao baixar/decodificar
};

// Fila thread-safe entre as threads de download e a thread de renderizacao.
struct TextureLoader {
    std::mutex mutex;
    std::vector<DecodedTexture> queue;

    void push(DecodedTexture&& item) {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push_back(std::move(item));
    }

    std::vector<DecodedTexture> drain() {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<DecodedTexture> out;
        out.swap(queue);
        return out;
    }
};

// Geometria calculada da tela da Xenon Store (barra de busca + grade de
// cards), usada tanto para desenhar quanto para testar toques, pra nao haver
// risco das duas contas saírem diferentes.
struct StoreLayout {
    float x0 = 0.f, y0 = 0.f, contentW = 0.f;
    float searchBarY = 0.f, searchBarH = 0.f;
    float gridY = 0.f;
    float cardW = 0.f, cardH = 0.f, gap = 0.f;
    int columns = 4, rows = 2, maxVisible = 8;
};

struct LibraryLayout {
    float x0 = 0.f, y0 = 0.f, contentW = 0.f;
    float gridY = 0.f, cardW = 0.f, cardH = 0.f, gap = 16.f;
    int columns = 1, rows = 1, maxVisible = 1;
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
    GLuint logoTexture = 0;
    bool logoLoadAttempted = false;
    float logoAspect = 1.f;

    // Xenon Store: catalogo remoto, cache de texturas (capas/capturas) e o
    // estado da tela de detalhes do jogo selecionado.
    CatalogService catalog;
    TextureLoader textureLoader;
    std::unordered_map<std::string, GLuint> coverTextures;
    std::unordered_map<std::string, bool> coverRequested;
    std::string storeSearchQuery;
    bool showGameDetail = false;
    // Foco do controle na Xenon Store: -1 = menu lateral, 0 = barra de busca,
    // 1..N = card do jogo (indice + 1).
    int storeFocus = -1;
    std::string selectedGameId;
    int detailScreenshotIndex = 0;
    std::chrono::steady_clock::time_point detailOpenedAt;
    bool detailAnimationStarted = false;
    std::unordered_map<std::string, StoreDownloadProgress> storeDownloads;
    std::vector<std::string> libraryGames;
    size_t librarySelection = 0;
    std::chrono::steady_clock::time_point libraryScannedAt;
    std::string libraryStatus;

    void initializeRenderer() {
        logoTexture = 0;
        logoLoadAttempted = false;
        coverTextures.clear();
        coverRequested.clear();
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

    // Decodifica app/src/main/assets/logo/logo.png (via BitmapFactory, pelo
    // lado Java) e sobe a textura. So tenta uma vez por contexto GL; se o
    // arquivo nao existir, o app segue usando o simbolo vetorial no lugar.
    void ensureLogoTexture() {
        if (logoTexture != 0 || logoLoadAttempted) return;
        logoLoadAttempted = true;
        if (!activity || !activity->vm || !activity->clazz) return;

        JNIEnv* env = nullptr;
        bool attached = false;
        const jint status = activity->vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
            if (activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
            attached = true;
        } else if (status != JNI_OK) {
            return;
        }

        jclass activityClass = env->GetObjectClass(activity->clazz);
        jmethodID decodeMethod = activityClass
            ? env->GetMethodID(activityClass, "decodeAssetBitmapRGBA", "(Ljava/lang/String;)[I")
            : nullptr;
        if (decodeMethod) {
            jstring path = env->NewStringUTF("logo/logo.png");
            auto result = static_cast<jintArray>(
                env->CallObjectMethod(activity->clazz, decodeMethod, path));
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
                result = nullptr;
            }
            if (result) {
                const jsize length = env->GetArrayLength(result);
                if (length >= 2) {
                    jint* data = env->GetIntArrayElements(result, nullptr);
                    const int texWidth = data[0];
                    const int texHeight = data[1];
                    if (texWidth > 0 && texHeight > 0 &&
                        static_cast<jsize>(2 + texWidth * texHeight) <= length) {
                        std::vector<uint8_t> rgba(
                            static_cast<size_t>(texWidth) * static_cast<size_t>(texHeight) * 4);
                        for (int i = 0; i < texWidth * texHeight; ++i) {
                            const uint32_t pixel = static_cast<uint32_t>(data[2 + i]);
                            rgba[i * 4 + 0] = static_cast<uint8_t>((pixel >> 16) & 0xFFu);
                            rgba[i * 4 + 1] = static_cast<uint8_t>((pixel >> 8) & 0xFFu);
                            rgba[i * 4 + 2] = static_cast<uint8_t>(pixel & 0xFFu);
                            rgba[i * 4 + 3] = static_cast<uint8_t>((pixel >> 24) & 0xFFu);
                        }
                        logoTexture = createTextureFromRGBA(rgba.data(), texWidth, texHeight);
                        logoAspect = texHeight > 0
                            ? static_cast<float>(texWidth) / static_cast<float>(texHeight) : 1.f;
                    }
                    env->ReleaseIntArrayElements(result, data, JNI_ABORT);
                }
                env->DeleteLocalRef(result);
            }
            env->DeleteLocalRef(path);
        }
        if (activityClass) env->DeleteLocalRef(activityClass);
        if (attached) activity->vm->DetachCurrentThread();
    }

    // Dispara (uma vez por URL) o download+decodificacao de uma capa ou
    // captura de tela em segundo plano; o resultado chega via textureLoader.
    void requestCoverTexture(const std::string& url) {
        if (url.empty()) return;
        if (coverTextures.find(url) != coverTextures.end()) return;
        auto it = coverRequested.find(url);
        if (it != coverRequested.end() && it->second) return;
        coverRequested[url] = true;

        ANativeActivity* act = activity;
        TextureLoader* loader = &textureLoader;
        std::thread([act, loader, url]() {
            DecodedTexture result;
            result.url = url;
            std::vector<uint8_t> bytes;
            bool loaded = false;
            if (url.rfind("file://", 0) == 0) {
                std::ifstream input(url.substr(7), std::ios::binary);
                if (input) {
                    input.seekg(0, std::ios::end);
                    const std::streamoff length = input.tellg();
                    if (length > 0 && length <= 8 * 1024 * 1024) {
                        bytes.resize(static_cast<size_t>(length));
                        input.seekg(0, std::ios::beg);
                        input.read(reinterpret_cast<char*>(bytes.data()), length);
                        loaded = input.gcount() == length;
                    }
                }
            } else {
                loaded = httpGetBytes(act, url, bytes);
            }
            if (loaded) {
                int w = 0, h = 0;
                std::vector<uint8_t> rgba;
                if (decodeImageBytes(act, bytes, rgba, w, h)) {
                    result.width = w;
                    result.height = h;
                    result.rgba = std::move(rgba);
                }
            }
            loader->push(std::move(result));
        }).detach();
    }

    // Chamado uma vez por frame: sobe pra GPU as imagens que ja terminaram de
    // baixar/decodificar em segundo plano (so pode rodar na thread de GL).
    void pumpTextureUploads() {
        for (auto& item : textureLoader.drain()) {
            if (!item.rgba.empty() && item.width > 0 && item.height > 0) {
                coverTextures[item.url] = createTextureFromRGBA(item.rgba.data(), item.width, item.height);
            } else {
                coverTextures[item.url] = 0; // tentou e falhou; evita baixar de novo toda hora
            }
        }
    }

    // Textura pronta para uma URL (capa/captura), ou 0 se ainda nao chegou;
    // dispara o download na primeira vez que a URL e pedida.
    GLuint coverTexture(const std::string& url) {
        if (url.empty()) return 0;
        auto it = coverTextures.find(url);
        if (it != coverTextures.end()) return it->second;
        requestCoverTexture(url);
        return 0;
    }

    void requestSearchDialog() {
        if (!activity || !activity->vm || !activity->clazz) {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon",
                                "Cannot open search dialog: Android activity is unavailable");
            return;
        }
        JNIEnv* env = nullptr;
        bool attached = false;
        if (!attachJniEnv(activity, env, attached)) {
            __android_log_print(ANDROID_LOG_ERROR, "Xenon",
                                "Cannot attach to Android VM to open search dialog");
            return;
        }
        jclass activityClass = env->GetObjectClass(activity->clazz);
        jmethodID showDialog = activityClass
            ? env->GetMethodID(activityClass, "showSearchDialog", "(Ljava/lang/String;)V") : nullptr;
        if (showDialog) {
            jstring query = env->NewStringUTF(storeSearchQuery.c_str());
            env->CallVoidMethod(activity->clazz, showDialog, query);
            env->DeleteLocalRef(query);
        }
        if (activityClass) env->DeleteLocalRef(activityClass);
        if (attached) activity->vm->DetachCurrentThread();
    }

    void handleSearchSubmitted(const std::string& query) {
        storeSearchQuery = query;
        catalog.fetchGames(activity, query);
    }

    void openGameDetail(const std::string& id) {
        selectedGameId = id;
        showGameDetail = true;
        detailScreenshotIndex = 0;
        detailOpenedAt = std::chrono::steady_clock::now();
        detailAnimationStarted = true;
    }

    void closeGameDetail() {
        showGameDetail = false;
        selectedGameId.clear();
        detailAnimationStarted = false;
    }

    void downloadStoreGame(const std::string& url, const std::string& title,
                           const std::string& sha256, const std::string& gameId,
                           const std::string& coverUrl) {
        if (url.empty() || !activity || !activity->vm || !activity->clazz) return;
        JNIEnv* env = nullptr;
        bool attached = false;
        if (!attachJniEnv(activity, env, attached)) return;
        jclass activityClass = env->GetObjectClass(activity->clazz);
        jmethodID downloadMethod = activityClass
            ? env->GetMethodID(activityClass, "downloadStoreGame", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V") : nullptr;
        if (downloadMethod) {
            jstring jurl = env->NewStringUTF(url.c_str());
            jstring jtitle = env->NewStringUTF(title.c_str());
            jstring jhash = env->NewStringUTF(sha256.c_str());
            jstring jgameId = env->NewStringUTF(gameId.c_str());
            jstring jcoverUrl = env->NewStringUTF(coverUrl.c_str());
            env->CallVoidMethod(activity->clazz, downloadMethod, jurl, jtitle, jhash, jgameId, jcoverUrl);
            env->DeleteLocalRef(jurl);
            env->DeleteLocalRef(jtitle);
            env->DeleteLocalRef(jhash);
            env->DeleteLocalRef(jgameId);
            env->DeleteLocalRef(jcoverUrl);
        }
        if (activityClass) env->DeleteLocalRef(activityClass);
        if (attached) activity->vm->DetachCurrentThread();
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

    // Quebra um texto em linhas que cabem em maxWidth, pra desenhar paragrafos
    // (por exemplo a descricao de um jogo na tela de detalhes).
    std::vector<std::string> wrapText(const std::string& text, float maxWidth, float scale) {
        std::vector<std::string> lines;
        std::string current;
        std::string word;
        auto flushWord = [&]() {
            if (word.empty()) return;
            std::string candidate = current.empty() ? word : current + " " + word;
            if (!current.empty() && smoothTextWidth(candidate.c_str(), scale) > maxWidth) {
                lines.push_back(current);
                current = word;
            } else {
                current = candidate;
            }
            word.clear();
        };
        for (char c : text) {
            if (c == ' ' || c == '\n') {
                flushWord();
                if (c == '\n') {
                    lines.push_back(current);
                    current.clear();
                }
            } else {
                word += c;
            }
        }
        flushWord();
        if (!current.empty()) lines.push_back(current);
        return lines;
    }

    // Barra superior (logo, status) e barra lateral de navegacao, desenhadas
    // em toda tela exceto na selecao de perfil.
    void drawChrome() {
        ensureLogoTexture();
        float w = static_cast<float>(renderer.width);
        float h = static_cast<float>(renderer.height);

        renderer.rect(0, 0, w, kTopBarH, kPanel);
        renderer.rect(0, kTopBarH - 1.f, w, 1.f, kPanel2);

        const float logoBoxH = 34.f;
        if (logoTexture != 0) {
            const float logoW = std::clamp(logoBoxH * logoAspect, 20.f, 160.f);
            renderer.texturedRect(24.f, (kTopBarH - logoBoxH) * .5f, logoW, logoBoxH, logoTexture);
            smoothText("XENON", 24.f + logoW + 14.f, (kTopBarH - 18.f) * .5f, 4.6f, kText);
        } else {
            renderer.roundedRect(24.f, (kTopBarH - 36.f) * .5f, 36.f, 36.f, kGreen);
            renderer.rotatedRect(24.f + 18.f, kTopBarH * .5f, 22.f, 4.f, .785398f, kBg);
            renderer.rotatedRect(24.f + 18.f, kTopBarH * .5f, 22.f, 4.f, -.785398f, kBg);
            smoothText("XENON", 24.f + 50.f, (kTopBarH - 18.f) * .5f, 4.6f, kText);
        }

        // Relogio, barras de sinal e wi-fi no canto superior direito.
        const float statusY = kTopBarH * .5f;
        float cursorX = w - 26.f;
        const time_t now = std::time(nullptr);
        char clockBuffer[8] = {};
        struct tm localTime {};
        if (localtime_r(&now, &localTime)) {
            std::snprintf(clockBuffer, sizeof(clockBuffer), "%02d:%02d",
                         localTime.tm_hour, localTime.tm_min);
        } else {
            std::snprintf(clockBuffer, sizeof(clockBuffer), "--:--");
        }
        const float clockWidth = smoothTextWidth(clockBuffer, 3.6f);
        cursorX -= clockWidth;
        smoothText(clockBuffer, cursorX, statusY - 9.f, 3.6f, kText);

        cursorX -= 34.f;
        for (int bar = 0; bar < 4; ++bar) {
            const float barH = 6.f + bar * 4.f;
            renderer.rect(cursorX + bar * 7.f, statusY + 9.f - barH, 5.f, barH, kGreen);
        }

        cursorX -= 40.f;
        renderer.roundedRect(cursorX, statusY - 8.f, 18.f, 14.f, kGreen);
        renderer.roundedRect(cursorX + 4.f, statusY - 4.f, 10.f, 7.f, kBg);

        // Barra lateral.
        renderer.rect(0, kTopBarH, kSidebarW, h - kTopBarH, kPanel);
        renderer.rect(kSidebarW - 1.f, kTopBarH, 1.f, h - kTopBarH, kPanel2);

        static const char* labels[] = {"INICIO", "XENON STORE", "MEUS JOGOS", "CONFIGURACOES", "SOBRE"};
        for (int i = 0; i < 5; ++i) {
            const float itemY = kTopBarH + kNavTopPad + i * kNavItemH;
            const bool active = static_cast<int>(screen) == i;
            const bool focused = focusedNavigation == i;
            if (active || focused) {
                renderer.rect(0, itemY, 4.f, kNavItemH - 10.f, kGreen);
                renderer.roundedRectCorners(4.f, itemY, kSidebarW - 4.f, kNavItemH - 10.f,
                                            active ? kPanel2 : Color{kPanel2.r, kPanel2.g, kPanel2.b, .6f},
                                            0.f, 10.f, 10.f, 0.f);
            }
            smoothText(labels[i], 26.f, itemY + (kNavItemH - 10.f) * .5f - 9.f, 2.8f,
                       active ? kGreen : (focused ? kText : kMuted));
        }
    }

    void drawHome() {
        float w = static_cast<float>(renderer.width);
        float h = static_cast<float>(renderer.height);
        const float x0 = kSidebarW;
        const float y0 = kTopBarH;
        const float margin = 28.f;
        const float contentW = w - x0;

        // Painel do perfil ativo, no canto superior direito.
        const float profileW = 240.f;
        const float profileH = 86.f;
        const float profileX = w - margin - profileW;
        const float profileY = y0 + margin;
        renderer.roundedRect(profileX, profileY, profileW, profileH, kPanel);
        renderer.rect(profileX + 14.f, profileY + profileH - 3.f, profileW - 28.f, 2.f,
                  Color{kGreen.r, kGreen.g, kGreen.b, .58f});
        renderer.roundedRect(profileX + 14.f, profileY + 14.f, 42.f, 42.f, kGreen);
        smoothText("X", profileX + 28.f, profileY + 25.f, 3.4f, kBg);
        const char* activeName = profileSelection.activeProfileName();
        smoothText(activeName, profileX + 68.f, profileY + 16.f, 3.f, kText);
        smoothText("JOGOS INSTALADOS: 0", profileX + 68.f, profileY + 40.f, 1.9f, kMuted);
        smoothText("ESPACO LIVRE: 223 GB", profileX + 68.f, profileY + 60.f, 1.9f, kMuted);

        // Banner de boas-vindas.
        const float heroY = y0 + margin;
        const float heroH = 258.f;
        const float heroW = contentW - margin * 2.f - profileW - 18.f;
        const float heroX = x0 + margin;
        renderer.roundedRect(heroX, heroY, heroW, heroH, kPanel);
        renderer.roundedRectCorners(heroX + 6.f, heroY + 1.f, heroW - 7.f, heroH - 2.f,
                        Color{.085f, .125f, .103f, 1.f}, 15.f, 15.f, 15.f, 15.f);
        renderer.rect(x0 + margin, heroY, 6.f, heroH, kGreen);

        const float motifX = heroX + heroW - 76.f;
        renderer.rotatedRect(motifX, heroY + heroH * .5f, 150.f, 8.f, .785398f,
                     Color{kGreen.r, kGreen.g, kGreen.b, .10f});
        renderer.rotatedRect(motifX, heroY + heroH * .5f, 150.f, 8.f, -.785398f,
                     Color{kGreen.r, kGreen.g, kGreen.b, .10f});
        renderer.roundedRect(motifX - 40.f, heroY + heroH * .5f - 40.f, 80.f, 80.f,
                     Color{.11f, .18f, .14f, .62f}, 40.f);
        renderer.roundedRect(motifX - 29.f, heroY + heroH * .5f - 29.f, 58.f, 58.f,
                     Color{.13f, .22f, .16f, .8f}, 29.f);

        const float markSize = 110.f;
        const float markX = x0 + margin + 30.f;
        const float markY = heroY + (heroH - markSize) * .5f;
        if (logoTexture != 0) {
            float logoW = markSize;
            float logoH = markSize / std::max(logoAspect, 0.01f);
            if (logoH > markSize) {
                logoH = markSize;
                logoW = markSize * logoAspect;
            }
            renderer.texturedRect(markX + (markSize - logoW) * .5f,
                                  markY + (markSize - logoH) * .5f, logoW, logoH, logoTexture);
        } else {
            renderer.roundedRect(markX, markY, markSize, markSize, Color{.09f, .14f, .12f, 1.f});
            renderer.rotatedRect(markX + markSize * .5f, markY + markSize * .5f,
                                 markSize * .62f, 11.f, .785398f, kGreen);
            renderer.rotatedRect(markX + markSize * .5f, markY + markSize * .5f,
                                 markSize * .62f, 11.f, -.785398f, kGreen);
        }

        const float textX = markX + markSize + 34.f;
        smoothText("SISTEMA DE ENTRETENIMENTO", textX, heroY + 34.f, 2.0f, kMuted);
        smoothText("BEM-VINDO AO", textX, heroY + 58.f, 3.0f, kGreen);
        smoothText("XENON", textX, heroY + 78.f, 8.2f, kText);
        smoothText("Sua nova experiencia em jogos comeca aqui.",
               textX, heroY + 142.f, 2.7f, kMuted);
        const float exploreW = 188.f;
        const float exploreY = heroY + heroH - 56.f;
        renderer.roundedRect(textX, exploreY, exploreW, 40.f, kGreen, 20.f);
        const char* exploreText = "EXPLORAR";
        smoothText(exploreText, textX + (exploreW - smoothTextWidth(exploreText, 2.8f)) * .5f,
                   exploreY + 13.f, 2.8f, kBg);
        renderer.rect(textX + exploreW + 22.f, exploreY + 9.f, 1.f, 22.f, kPanel2);
        smoothText("DESCUBRA A BIBLIOTECA", textX + exploreW + 38.f, exploreY + 13.f,
                   1.8f, kMuted);

        // Linha "Destaques": vitrine dos jogos em destaque.
        const float destaquesY = heroY + heroH + 24.f;
        smoothText("DESTAQUES", x0 + margin, destaquesY, 3.6f, kText);
        const char* verTodos = "VER TODOS";
        smoothText(verTodos, w - margin - smoothTextWidth(verTodos, 2.2f), destaquesY + 4.f,
                   2.2f, kGreen);

        const int cardCount = 5;
        const float cardGap = 14.f;
        const float cardsY = destaquesY + 28.f;
        const float cardsAreaW = contentW - margin * 2.f;
        const float cardW = (cardsAreaW - cardGap * (cardCount - 1)) / cardCount;
        const float cardH = std::min(188.f, h - cardsY - 24.f);
        static const char* titles[cardCount] = {"FORZA HORIZON", "SONIC GEN.", "MINECRAFT",
                                                "PORTAL 2", "PLANTS VS ZUMBIS"};
        static const char* initials[cardCount] = {"FH", "SG", "MC", "P2", "PZ"};
        // Cada card ganha uma cor de destaque diferente, so pra nao ficar tudo
        // igual (a capa em si e so um espaco reservado ate termos capas reais).
        const Color accents[5] = {
            kGreen,
            Color{0.360f, 0.620f, 0.980f, 1.f},
            Color{0.980f, 0.690f, 0.260f, 1.f},
            Color{0.760f, 0.420f, 0.980f, 1.f},
            Color{0.260f, 0.870f, 0.780f, 1.f},
        };
        const float coverRatio = .62f;
        for (int i = 0; i < cardCount; ++i) {
            const float cardX = x0 + margin + i * (cardW + cardGap);
            const Color& accent = accents[i % 5];
            const float coverH = cardH * coverRatio;
            const Color cover{accent.r * .18f, accent.g * .18f, accent.b * .18f, 1.f};

            renderer.roundedRect(cardX, cardsY, cardW, cardH, kPanel, 16.f);
            // Cantos arredondados so em cima: embaixo a capa vira o rodape do
            // card, entao os cantos de baixo dela ficam retos.
            renderer.roundedRectCorners(cardX, cardsY, cardW, coverH, cover, 16.f, 16.f, 0.f, 0.f);
            renderer.rect(cardX, cardsY, cardW, 3.f, accent);

            const float initialScale = 5.4f;
            const float initialWidth = smoothTextWidth(initials[i], initialScale);
            smoothText(initials[i], cardX + (cardW - initialWidth) * .5f,
                       cardsY + coverH * .5f - 24.f, initialScale, accent);

            smoothText(titles[i], cardX + 14.f, cardsY + coverH + 16.f, 2.1f, kText);
            renderer.roundedRect(cardX + 14.f, cardsY + coverH + 40.f, 64.f, 20.f, accent, 6.f);
            smoothText("INSTALAR", cardX + 14.f + 9.f, cardsY + coverH + 45.f, 1.5f, kBg);
            renderer.rect(cardX + cardW - 24.f, cardsY + coverH + 46.f, 10.f, 2.f,
                          Color{accent.r, accent.g, accent.b, .7f});
        }
    }

    StoreLayout computeStoreLayout() {
        StoreLayout L;
        const float w = static_cast<float>(renderer.width);
        const float h = static_cast<float>(renderer.height);
        L.x0 = kSidebarW + 36.f;
        L.y0 = kTopBarH + 32.f;
        L.contentW = w - kSidebarW - 72.f;
        // Titulo (~56px) + subtitulo, depois a barra de busca: espaco suficiente
        // pra nao sobrepor o texto.
        L.searchBarY = L.y0 + 104.f;
        L.searchBarH = 56.f;
        L.gridY = L.searchBarY + L.searchBarH + 32.f;
        L.gap = 24.f;
        // Cards em formato retrato (proporcao de capa de caixa do Xbox 360),
        // com a capa ocupando quase todo o card.
        const float coverAspect = 1.38f;   // altura / largura da capa
        const float infoH = 64.f;          // area do titulo abaixo da capa
        const float targetW = 250.f;
        const float available = std::max(160.f, h - L.gridY - 28.f);
        L.columns = std::max(2, static_cast<int>((L.contentW + L.gap) / (targetW + L.gap)));
        L.cardW = (L.contentW - L.gap * (L.columns - 1)) / static_cast<float>(L.columns);
        L.cardH = L.cardW * coverAspect + infoH;
        if (L.cardH > available) {
            // Tela baixa: encolhe o card pra caber pelo menos uma fileira inteira.
            L.cardH = available;
            L.cardW = (available - infoH) / coverAspect;
            L.columns = std::max(1, static_cast<int>((L.contentW + L.gap) / (L.cardW + L.gap)));
        }
        L.rows = std::max(1, static_cast<int>((available + L.gap) / (L.cardH + L.gap)));
        L.maxVisible = L.columns * L.rows;
        return L;
    }

    void drawStore() {
        const float h = static_cast<float>(renderer.height);
        const StoreLayout L = computeStoreLayout();
        const bool loading = catalog.loading.load();
        const bool hasError = catalog.hasError.load();
        const bool loadedOnce = catalog.hasLoadedOnce.load();

        // Dispara a primeira busca assim que a loja e aberta pela primeira vez.
        if (!loadedOnce && !loading) {
            catalog.fetchGames(activity, storeSearchQuery);
        }

        std::vector<GameEntry> games = catalog.snapshot();
        smoothText("XENON STORE", L.x0, L.y0, 7.f, kText);
        char subtitle[128];
        if (loading) {
            std::snprintf(subtitle, sizeof(subtitle), "Carregando catalogo...");
        } else if (hasError) {
            std::snprintf(subtitle, sizeof(subtitle), "Nao foi possivel carregar os jogos agora");
        } else if (!storeSearchQuery.empty()) {
            std::snprintf(subtitle, sizeof(subtitle), "%d resultado(s) para \"%s\"",
                         static_cast<int>(games.size()), storeSearchQuery.c_str());
        } else {
            std::snprintf(subtitle, sizeof(subtitle), "%d jogo(s) no catalogo",
                         static_cast<int>(games.size()));
        }
        smoothText(subtitle, L.x0, L.y0 + 66.f, 2.6f, kMuted);

        // Barra de busca (toque abre o dialogo nativo de busca do Android).
        if (storeFocus == 0) {
            renderer.roundedRect(L.x0 - 3.f, L.searchBarY - 3.f, L.contentW + 6.f, L.searchBarH + 6.f, kGreen, 31.f);
        }
        renderer.roundedRect(L.x0, L.searchBarY, L.contentW, L.searchBarH, kPanel, 28.f);
        renderer.rect(L.x0 + 28.f, L.searchBarY + L.searchBarH - 2.f, L.contentW - 56.f, 2.f, Color{kGreen.r, kGreen.g, kGreen.b, .35f});
        renderer.roundedRect(L.x0 + 22.f, L.searchBarY + L.searchBarH * .5f - 9.f, 18.f, 18.f, kGreen, 9.f);
        const std::string placeholder = storeSearchQuery.empty() ? "Buscar jogos..." : storeSearchQuery;
        smoothText(placeholder.c_str(), L.x0 + 54.f, L.searchBarY + L.searchBarH * .5f - 9.f, 2.6f,
                   storeSearchQuery.empty() ? kMuted : kText);
        const char* searchHint = "TOQUE PARA BUSCAR";
        smoothText(searchHint, L.x0 + L.contentW - smoothTextWidth(searchHint, 1.8f) - 28.f,
                   L.searchBarY + L.searchBarH * .5f - 7.f, 1.8f, kGreen);

        if (loading) {
            // Skeleton de carregamento: blocos cinza "pulsando" no lugar dos cards.
            const auto now = std::chrono::steady_clock::now();
            const float t = std::chrono::duration<float>(now.time_since_epoch()).count();
            const float pulse = 0.5f + 0.5f * std::sin(t * 3.f);
            for (int i = 0; i < L.maxVisible; ++i) {
                const int col = i % L.columns;
                const int row = i / L.columns;
                const float cardX = L.x0 + col * (L.cardW + L.gap);
                const float cardY = L.gridY + row * (L.cardH + L.gap);
                const Color shade{kPanel2.r, kPanel2.g, kPanel2.b, 0.5f + 0.3f * pulse};
                renderer.roundedRect(cardX, cardY, L.cardW, L.cardH, shade, 18.f);
            }
            return;
        }

        if (hasError) {
            renderer.roundedRect(L.x0, L.gridY, L.contentW, h - L.gridY - 24.f, kPanel, 16.f);
            smoothText("Nao foi possivel carregar os jogos agora.",
                       L.x0 + 28.f, L.gridY + 36.f, 3.2f, kText);
            smoothText("Verifique sua conexao com a internet e tente novamente.",
                       L.x0 + 28.f, L.gridY + 68.f, 2.4f, kMuted);
            renderer.roundedRect(L.x0 + 28.f, L.gridY + 100.f, 220.f, 44.f, kGreen, 22.f);
            const char* retryText = "TENTAR NOVAMENTE";
            smoothText(retryText, L.x0 + 28.f + (220.f - smoothTextWidth(retryText, 2.4f)) * .5f,
                       L.gridY + 113.f, 2.4f, kBg);
            return;
        }

        if (games.empty()) {
            renderer.roundedRect(L.x0, L.gridY, L.contentW, h - L.gridY - 24.f, kPanel, 16.f);
            const char* emptyTitle = storeSearchQuery.empty() ? "Nenhum jogo no catalogo ainda"
                                                              : "Nenhum jogo encontrado";
            smoothText(emptyTitle, L.x0 + 28.f, L.gridY + 36.f, 3.6f, kText);
            const char* emptySubtitle = storeSearchQuery.empty()
                ? "Volte mais tarde para conferir novidades."
                : "Tente buscar por outro termo.";
            smoothText(emptySubtitle, L.x0 + 28.f, L.gridY + 72.f, 2.6f, kMuted);
            return;
        }

        const Color accents[5] = {
            kGreen,
            Color{0.360f, 0.620f, 0.980f, 1.f},
            Color{0.980f, 0.690f, 0.260f, 1.f},
            Color{0.760f, 0.420f, 0.980f, 1.f},
            Color{0.260f, 0.870f, 0.780f, 1.f},
        };
        const int visibleCount = std::min(static_cast<int>(games.size()), L.maxVisible);
        for (int i = 0; i < visibleCount; ++i) {
            const GameEntry& game = games[i];
            const int col = i % L.columns;
            const int row = i / L.columns;
            const float cardX = L.x0 + col * (L.cardW + L.gap);
            const float cardY = L.gridY + row * (L.cardH + L.gap);
            const Color& accent = accents[i % 5];
            const float coverH = L.cardH - 64.f;
            const float pad = 8.f;

            // Card focado pelo controle ganha um contorno verde.
            if (storeFocus == i + 1) {
                renderer.roundedRect(cardX - 4.f, cardY - 4.f, L.cardW + 8.f, L.cardH + 8.f, kGreen, 22.f);
            }
            // Sombra suave + card com borda fina destacando a capa.
            renderer.roundedRect(cardX + 3.f, cardY + 6.f, L.cardW, L.cardH, Color{0.f, 0.f, 0.f, .35f}, 18.f);
            renderer.roundedRect(cardX, cardY, L.cardW, L.cardH, kPanel2, 18.f);
            renderer.roundedRect(cardX + 1.f, cardY + 1.f, L.cardW - 2.f, L.cardH - 2.f, kPanel, 17.f);
            const float imgX = cardX + pad;
            const float imgY = cardY + pad;
            const float imgW = L.cardW - pad * 2.f;
            const float imgH = coverH - pad;
            const GLuint cover = coverTexture(game.cover);
            if (cover != 0) {
                renderer.texturedRect(imgX, imgY, imgW, imgH, cover, 1.f, 12.f);
            } else {
                const Color coverColor{accent.r * .18f, accent.g * .18f, accent.b * .18f, 1.f};
                renderer.roundedRect(imgX, imgY, imgW, imgH, coverColor, 12.f);
                const std::string initial = game.title.empty() ? "?" : game.title.substr(0, 1);
                const float scale = 7.f;
                const float iw = smoothTextWidth(initial.c_str(), scale);
                smoothText(initial.c_str(), imgX + (imgW - iw) * .5f, imgY + imgH * .5f - 30.f,
                           scale, accent);
            }
            renderer.rect(imgX, imgY + imgH + 6.f, imgW * .28f, 3.f, accent);

            std::string title = game.title.empty() ? "Sem titulo" : game.title;
            while (title.size() > 4 && smoothTextWidth((title + "...").c_str(), 2.2f) > L.cardW - 24.f) {
                title.pop_back();
            }
            if (title != game.title) title += "...";
            smoothText(title.c_str(), cardX + 12.f, cardY + coverH + 20.f, 2.2f, kText);
        }

        if (storeFocus > visibleCount) storeFocus = visibleCount;
        const char* padHint = "CONTROLE:  A ABRIR   B VOLTAR   Y BUSCAR";
        smoothText(padHint, L.x0 + L.contentW - smoothTextWidth(padHint, 1.8f), L.y0 + 70.f, 1.8f, kMuted);

        if (static_cast<int>(games.size()) > visibleCount) {
            char more[64];
            std::snprintf(more, sizeof(more), "+%d jogo(s) - refine sua busca para ver mais",
                         static_cast<int>(games.size()) - visibleCount);
            smoothText(more, L.x0,
                       L.gridY + L.rows * L.cardH + (L.rows - 1) * L.gap + 28.f, 2.2f, kMuted);
        }
    }

    // Modal de detalhes do jogo selecionado na Xenon Store: capa, titulo,
    // descricao, botao de download e o carrossel de capturas.
    void drawGameDetailOverlay() {
        const float w = static_cast<float>(renderer.width);
        const float h = static_cast<float>(renderer.height);
        GameEntry game;
        if (!catalog.findById(selectedGameId, game)) {
            closeGameDetail();
            return;
        }

        float openProgress = 1.f;
        if (detailAnimationStarted) {
            const float elapsed = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - detailOpenedAt).count();
            openProgress = std::min(1.f, elapsed / .24f);
            openProgress = 1.f - std::pow(1.f - openProgress, 3.f);
        }
        const float overlayAlpha = .68f * openProgress;
        renderer.rect(0, 0, w, h, Color{0.f, 0.f, 0.f, overlayAlpha});

        const float basePanelW = std::min(960.f, w - 80.f);
        const float basePanelH = std::min(600.f, h - 64.f);
        const float panelScale = .88f + .12f * openProgress;
        const float panelW = basePanelW * panelScale;
        const float panelH = basePanelH * panelScale;
        const float panelX = (w - panelW) * .5f;
        const float panelY = (h - panelH) * .5f;
        renderer.roundedRect(panelX, panelY, panelW, panelH, kPanel, 20.f);

        const float closeSize = 36.f;
        const float closeX = panelX + panelW - closeSize - 18.f;
        const float closeY = panelY + 18.f;
        renderer.roundedRect(closeX, closeY, closeSize, closeSize, kPanel2, 10.f);
        smoothText("X", closeX + 12.f, closeY + 8.f, 3.2f, kText);

        const float coverW = 220.f;
        const float coverH = 300.f;
        const float coverX = panelX + 28.f;
        const float coverY = panelY + 28.f;
        const GLuint cover = coverTexture(game.cover);
        if (cover != 0) {
            renderer.texturedRect(coverX, coverY, coverW, coverH, cover, 1.f, 14.f);
        } else {
            renderer.roundedRect(coverX, coverY, coverW, coverH, kPanel2, 14.f);
            const std::string initial = game.title.empty() ? "?" : game.title.substr(0, 1);
            const float scale = 7.f;
            const float iw = smoothTextWidth(initial.c_str(), scale);
            smoothText(initial.c_str(), coverX + (coverW - iw) * .5f, coverY + coverH * .5f - 30.f,
                       scale, kGreen);
        }

        const float textX = coverX + coverW + 28.f;
        const float textW = panelX + panelW - 28.f - textX;
        const std::string title = game.title.empty() ? "Jogo sem titulo" : game.title;
        smoothText(title.c_str(), textX, coverY, 5.2f, kText);

        const std::string description = game.description.empty()
            ? "Sem descricao disponivel para este jogo."
            : game.description;
        const std::vector<std::string> lines = wrapText(description, textW, 2.4f);
        const int maxLines = 6;
        float lineY = coverY + 50.f;
        for (int i = 0; i < static_cast<int>(lines.size()) && i < maxLines; ++i) {
            smoothText(lines[i].c_str(), textX, lineY, 2.4f, kMuted);
            lineY += 26.f;
        }
        if (static_cast<int>(lines.size()) > maxLines) {
            smoothText("...", textX, lineY, 2.4f, kMuted);
        }

        const float openW = 200.f;
        const float openY = coverY + coverH - 44.f;
        const auto download = storeDownloads.find(game.id);
        const bool hasDownload = download != storeDownloads.end();
        const StoreDownloadProgress* progress = hasDownload ? &download->second : nullptr;
        const char* openText = !progress ? "BAIXAR JOGO"
            : (progress->state == 1 ? "CONTINUAR" : (progress->state == 2 ? "TENTAR NOVAMENTE" : "BAIXANDO"));
        renderer.roundedRect(textX, openY, openW, 44.f, hasDownload ? kPanel2 : kGreen, 22.f);
        if (progress && progress->total > 0 && progress->state == 0) {
            const float fraction = std::min(1.f, static_cast<float>(progress->downloaded)
                    / static_cast<float>(progress->total));
            renderer.roundedRect(textX, openY, openW * fraction, 44.f, kGreen, 22.f);
        }
        const float openScale = progress && progress->state == 2 ? 1.5f : 1.9f;
        smoothText(openText, textX + (openW - smoothTextWidth(openText, openScale)) * .5f,
               openY + 15.f, openScale, hasDownload ? kText : kBg);
        if (progress) {
            char progressText[128];
            if (progress->total > 0) {
                const double percent = std::min(100.0, progress->downloaded * 100.0 / progress->total);
                std::snprintf(progressText, sizeof(progressText), "%.2f%%  %.2f / %.2f MB",
                              percent,
                              progress->downloaded / (1024.0 * 1024.0),
                              progress->total / (1024.0 * 1024.0));
            } else {
                std::snprintf(progressText, sizeof(progressText), "%.2f MB baixados",
                              progress->downloaded / (1024.0 * 1024.0));
            }
            smoothText(progressText, textX, openY - 22.f, 2.0f,
                       progress->state == 2 ? kMuted : kText);
        }

        // Carrossel de capturas de tela.
        const float shotsY = coverY + coverH + 28.f;
        const float shotsH = panelY + panelH - shotsY - 24.f;
        if (!game.screenshots.empty() && shotsH > 60.f) {
            const int count = static_cast<int>(game.screenshots.size());
            detailScreenshotIndex = ((detailScreenshotIndex % count) + count) % count;
            const std::string& shotUrl = game.screenshots[static_cast<size_t>(detailScreenshotIndex)];
            const float shotW = panelW - 56.f - 96.f;
            const float shotX = panelX + 28.f + 48.f;
            renderer.roundedRect(shotX, shotsY, shotW, shotsH, kBg, 12.f);
            const GLuint shotTex = coverTexture(shotUrl);
            if (shotTex != 0) {
                renderer.texturedRect(shotX, shotsY, shotW, shotsH, shotTex);
            } else {
                smoothText("Carregando...", shotX + 16.f, shotsY + shotsH * .5f - 9.f, 2.2f, kMuted);
            }

            renderer.roundedRect(panelX + 28.f, shotsY + shotsH * .5f - 18.f, 36.f, 36.f, kPanel2, 10.f);
            smoothText("<", panelX + 28.f + 12.f, shotsY + shotsH * .5f - 9.f, 2.6f, kText);
            renderer.roundedRect(panelX + panelW - 28.f - 36.f, shotsY + shotsH * .5f - 18.f, 36.f, 36.f,
                                 kPanel2, 10.f);
            smoothText(">", panelX + panelW - 28.f - 36.f + 13.f, shotsY + shotsH * .5f - 9.f, 2.6f, kText);

            const float dotsY = shotsY + shotsH + 10.f;
            if (dotsY + 8.f < panelY + panelH) {
                const int visibleDots = std::min(count, 10);
                const float totalDots = visibleDots * 16.f;
                const float dotsX = shotX + (shotW - totalDots) * .5f;
                for (int i = 0; i < visibleDots; ++i) {
                    renderer.roundedRect(dotsX + i * 16.f, dotsY, 8.f, 8.f,
                                         i == detailScreenshotIndex ? kGreen : kPanel2, 4.f);
                }
            }
        } else {
            smoothText("Sem capturas de tela disponiveis.", panelX + 28.f, shotsY, 2.4f, kMuted);
        }
    }

    LibraryLayout computeLibraryLayout() const {
        LibraryLayout layout;
        const float width = static_cast<float>(renderer.width);
        const float height = static_cast<float>(renderer.height);
        layout.x0 = kSidebarW + 24.f;
        layout.y0 = kTopBarH + 20.f;
        layout.contentW = std::max(160.f, width - kSidebarW - 48.f);
        layout.gridY = layout.y0 + 72.f;
        const float availableHeight = std::max(120.f, height - layout.gridY - 34.f);
        layout.columns = std::max(1, std::min(5,
                static_cast<int>((layout.contentW + layout.gap) / 176.f)));
        layout.cardW = (layout.contentW - layout.gap * (layout.columns - 1))
                / static_cast<float>(layout.columns);
        layout.cardH = std::min(220.f, availableHeight);
        layout.rows = std::max(1, static_cast<int>((availableHeight + layout.gap)
                / (layout.cardH + layout.gap)));
        layout.maxVisible = layout.columns * layout.rows;
        return layout;
    }

    void refreshLibraryGames() {
        const auto now = std::chrono::steady_clock::now();
        if (now - libraryScannedAt < std::chrono::seconds(1)) return;
        libraryScannedAt = now;
        std::vector<std::string> found;
        const std::string gamesPath = profileStoragePath + "/games";
        if (DIR* directory = opendir(gamesPath.c_str())) {
            while (dirent* entry = readdir(directory)) {
                std::string name = entry->d_name;
                if (name.size() < 4) continue;
                std::string extension = name.substr(name.size() - 4);
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (extension == ".iso") found.push_back(std::move(name));
            }
            closedir(directory);
        }
        std::sort(found.begin(), found.end());
        libraryGames = std::move(found);
        if (libraryGames.empty()) librarySelection = 0;
        else librarySelection = std::min(librarySelection, libraryGames.size() - 1);
    }

    void attemptLaunchSelectedIso() {
        if (libraryGames.empty() || librarySelection >= libraryGames.size()) return;
        const std::string path = profileStoragePath + "/games/" + libraryGames[librarySelection];
        xboxemu::filesystem::XdvdfsImage image;
        std::string error;
        if (!image.open(path, &error)) {
            libraryStatus = "ISO invalido ou nao suportado: " + error;
            return;
        }
        xboxemu::filesystem::XdvdfsFile executable;
        if (!image.findFile("default.xex", executable)) {
            libraryStatus = "XDVDFS aberto, mas default.xex nao foi encontrado.";
            return;
        }
        std::vector<uint8_t> header;
        if (!image.readFilePrefix("default.xex", 4, header, &error) ||
            header.size() != 4 || std::memcmp(header.data(), "XEX2", 4) != 0) {
            libraryStatus = "default.xex encontrado, mas a assinatura XEX2 e invalida.";
            return;
        }
        libraryStatus = "XEX2 encontrado (" + std::to_string(executable.size)
                + " bytes), mas o core ainda nao descomprime/carrega XEX.";
    }

    void drawLibrary() {
        refreshLibraryGames();
        const float height = static_cast<float>(renderer.height);
        const LibraryLayout layout = computeLibraryLayout();
        smoothText("MEUS JOGOS", layout.x0, layout.y0, 6.f, kText);
        char subtitle[96];
        std::snprintf(subtitle, sizeof(subtitle), "%zu ISO(s) no armazenamento", libraryGames.size());
        smoothText(subtitle, layout.x0, layout.y0 + 30.f, 2.6f, kMuted);
        const float panelY = layout.y0 + 52.f;
        renderer.roundedRect(layout.x0, panelY, layout.contentW,
                             std::max(80.f, height - panelY - 20.f), kPanel);

        if (libraryGames.empty()) {
            smoothText("Nenhum ISO encontrado", layout.x0 + 24.f, panelY + 42.f, 3.4f, kText);
            smoothText("Os downloads concluidos aparecerao aqui.", layout.x0 + 24.f,
                       panelY + 76.f, 2.4f, kMuted);
            return;
        }

        const size_t pageStart = (librarySelection / static_cast<size_t>(layout.maxVisible))
                * static_cast<size_t>(layout.maxVisible);
        const size_t pageEnd = std::min(libraryGames.size(), pageStart + layout.maxVisible);
        for (size_t index = pageStart; index < pageEnd; ++index) {
            const size_t localIndex = index - pageStart;
            const int column = static_cast<int>(localIndex % layout.columns);
            const int row = static_cast<int>(localIndex / layout.columns);
            const float x = layout.x0 + column * (layout.cardW + layout.gap);
            const float y = layout.gridY + row * (layout.cardH + layout.gap);
            const bool selected = index == librarySelection;
            if (selected) {
                renderer.roundedRect(x - 3.f, y - 3.f, layout.cardW + 6.f,
                                     layout.cardH + 6.f, kGreen, 12.f);
            }
            const std::string& name = libraryGames[index];
            const std::string coverPath = profileStoragePath + "/games/" + name + ".cover";
            const GLuint cover = access(coverPath.c_str(), R_OK) == 0
                    ? coverTexture("file://" + coverPath) : 0;
            renderer.roundedRect(x, y, layout.cardW, layout.cardH, kPanel2, 10.f);
            if (cover != 0) {
                renderer.texturedRect(x + 4.f, y + 4.f, layout.cardW - 8.f,
                                      layout.cardH - 8.f, cover, 1.f, 8.f);
            } else {
                const std::string initial = name.empty() ? "?" : name.substr(0, 1);
                smoothText(initial.c_str(), x + layout.cardW * .42f, y + layout.cardH * .25f,
                           std::min(7.f, layout.cardW * .045f), kGreen);
            }
            const float labelH = 42.f;
            renderer.roundedRect(x + 4.f, y + layout.cardH - labelH - 4.f,
                                 layout.cardW - 8.f, labelH, Color{0.f, 0.f, 0.f, .78f}, 7.f);
            std::string title = name;
            while (title.size() > 5 && smoothTextWidth((title + "...").c_str(), 2.f) > layout.cardW - 20.f) {
                title.pop_back();
            }
            if (title != name) title += "...";
            smoothText(title.c_str(), x + 10.f, y + layout.cardH - 29.f, 2.f, kText);
        }

        if (pageStart > 0 || pageEnd < libraryGames.size()) {
            char page[64];
            std::snprintf(page, sizeof(page), "%zu / %zu", pageStart / layout.maxVisible + 1,
                          (libraryGames.size() + layout.maxVisible - 1) / layout.maxVisible);
            smoothText(page, layout.x0 + layout.contentW - smoothTextWidth(page, 2.f) - 16.f,
                       layout.y0 + 30.f, 2.f, kMuted);
        }
        if (!libraryStatus.empty()) {
            const float statusY = std::min(height - 26.f, layout.gridY + layout.rows * (layout.cardH + layout.gap) + 8.f);
            smoothText(libraryStatus.c_str(), layout.x0, statusY, 2.f, kMuted);
        }
    }

    void drawAbout() {
        float w = static_cast<float>(renderer.width);
        const float x0 = kSidebarW + 28.f;
        const float y0 = kTopBarH + 28.f;
        const float contentW = w - kSidebarW - 56.f;
        smoothText("SOBRE", x0, y0, 7.f, kText);
        smoothText("Resumo do emulador", x0, y0 + 30.f, 3.2f, kMuted);

        float cardY = y0 + 46.f;
        float cardW = (contentW - 24.f) / 2.f;
        renderer.roundedRect(x0, cardY, cardW, 210.f, kPanel);
        smoothText("MEMORIA", x0 + 24.f, cardY + 24.f, 3.1f, kMuted);
        smoothText("512 MB", x0 + 24.f, cardY + 68.f, 8.f, kText);
        smoothText("RAM UNIFICADA", x0 + 24.f, cardY + 112.f, 3.2f, kGreen);
        renderer.roundedRect(x0 + 24.f, cardY + 158.f, cardW - 48.f, 10.f, kPanel2);
        renderer.roundedRect(x0 + 24.f, cardY + 158.f, (cardW - 48.f) * .18f, 10.f, kGreen);

        renderer.roundedRect(x0 + cardW + 24.f, cardY, cardW, 210.f, kBg);
        renderer.rect(x0 + cardW + 24.f, cardY, cardW, 1.f, kPanel2);
        smoothText("PROCESSADOR", x0 + cardW + 48.f, cardY + 24.f, 3.1f, kMuted);
        smoothText("XENON", x0 + cardW + 48.f, cardY + 68.f, 7.f, kText);
        smoothText("3 nucleos  /  6 threads", x0 + cardW + 48.f,
                   cardY + 112.f, 3.3f, kGreen);
        smoothText("Arquitetura PowerPC", x0 + cardW + 48.f,
                   cardY + 158.f, 3.0f, kMuted);

        float gpuY = cardY + 234.f;
        renderer.roundedRect(x0, gpuY, contentW, 92.f, kPanel);
        renderer.roundedRect(x0 + 18.f, gpuY + 18.f, 56.f, 56.f, kBg);
        smoothText("GPU", x0 + 29.f, gpuY + 38.f, 3.1f, kGreen);
        smoothText("XENOS", x0 + 92.f, gpuY + 22.f, 4.5f, kText);
        smoothText("Nao iniciada", x0 + 92.f, gpuY + 52.f, 3.2f, kMuted);
    }

    void drawSettings() {
        float w = static_cast<float>(renderer.width);
        const float x0 = kSidebarW + 28.f;
        const float y0 = kTopBarH + 28.f;
        const float contentW = w - kSidebarW - 56.f;
        smoothText("CONFIGURACOES", x0, y0, 7.f, kText);
        smoothText("Configuracoes do aplicativo", x0, y0 + 30.f, 3.2f, kMuted);
        for (int i = 0; i < 3; ++i) {
            float y = y0 + 70.f + i * 92.f;
            renderer.roundedRect(x0, y, contentW, 76, kPanel);
            const char* labels[] = {"VERIFICAR MEMORIA", "RASTREIO MMIO", "RELATORIO DE COMPATIBILIDADE"};
            const char* descriptions[] = {"Executar diagnostico rapido",
                                          "Monitorar chamadas do sistema",
                                          "Gerar dados para depuracao"};
            smoothText(labels[i], x0 + 24.f, y + 20.f, 3.5f, kText);
            smoothText(descriptions[i], x0 + 24.f, y + 43.f, 2.7f, kMuted);
            renderer.roundedRect(x0 + contentW - 82.f, y + 24.f, 58.f, 28.f,
                                 i == 0 ? kGreen : kPanel2);
            renderer.roundedRect(x0 + contentW - (i == 0 ? 59.f : 75.f),
                                 y + 29.f, 18.f, 18.f, i == 0 ? kBg : kMuted);
        }
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
        pumpTextureUploads();
        switch (screen) {
            case Screen::Home: drawHome(); break;
            case Screen::Store: drawStore(); break;
            case Screen::Library: drawLibrary(); break;
            case Screen::Settings: drawSettings(); break;
            case Screen::About: drawAbout(); break;
            case Screen::ProfileSelect: drawProfileSelect(); break;
        }
        if (screen != Screen::ProfileSelect) drawChrome();
        if (showGameDetail) drawGameDetailOverlay();
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

        // Modal de detalhes do jogo: captura todo toque enquanto estiver
        // aberto (inclusive fora dele, pra fechar).
        if (showGameDetail) {
            const float panelW = std::min(960.f, w - 80.f);
            const float panelH = std::min(600.f, h - 64.f);
            const float panelX = (w - panelW) * .5f;
            const float panelY = (h - panelH) * .5f;

            const float closeSize = 36.f;
            const float closeX = panelX + panelW - closeSize - 18.f;
            const float closeY = panelY + 18.f;
            if (x >= closeX && x < closeX + closeSize && y >= closeY && y < closeY + closeSize) {
                closeGameDetail();
                return;
            }
            if (x < panelX || x >= panelX + panelW || y < panelY || y >= panelY + panelH) {
                closeGameDetail();
                return;
            }

            const float coverW = 220.f, coverH = 300.f;
            const float coverX = panelX + 28.f, coverY = panelY + 28.f;
            const float textX = coverX + coverW + 28.f;
            const float openW = 200.f;
            const float openY = coverY + coverH - 44.f;
            if (x >= textX && x < textX + openW && y >= openY && y < openY + 44.f) {
                GameEntry game;
                if (catalog.findById(selectedGameId, game)) {
                    downloadStoreGame(game.link, game.title, game.sha256, game.id, game.cover);
                }
                return;
            }

            GameEntry game;
            const float shotsY = coverY + coverH + 28.f;
            const float shotsH = panelY + panelH - shotsY - 24.f;
            if (catalog.findById(selectedGameId, game) && !game.screenshots.empty() && shotsH > 60.f) {
                const int count = static_cast<int>(game.screenshots.size());
                if (x >= panelX + 28.f && x < panelX + 28.f + 36.f &&
                    y >= shotsY + shotsH * .5f - 18.f && y < shotsY + shotsH * .5f + 18.f) {
                    detailScreenshotIndex = (detailScreenshotIndex - 1 + count) % count;
                    return;
                }
                if (x >= panelX + panelW - 28.f - 36.f && x < panelX + panelW - 28.f &&
                    y >= shotsY + shotsH * .5f - 18.f && y < shotsY + shotsH * .5f + 18.f) {
                    detailScreenshotIndex = (detailScreenshotIndex + 1) % count;
                    return;
                }
            }
            return; // qualquer outro toque dentro do modal nao faz nada
        }

        // Barra lateral: cada item ocupa uma faixa vertical fixa.
        if (x < kSidebarW && y >= kTopBarH) {
            int index = static_cast<int>((y - kTopBarH - kNavTopPad) / kNavItemH);
            if (index >= 0 && index < 5) {
                focusedNavigation = index;
                screen = static_cast<Screen>(index);
            }
            return;
        }
        if (y < kTopBarH) return;

        // Tela da Xenon Store: barra de busca, botao de nova tentativa e os
        // cards do catalogo.
        if (screen == Screen::Store) {
            const StoreLayout L = computeStoreLayout();
            if (x >= L.x0 && x < L.x0 + L.contentW && y >= L.searchBarY && y < L.searchBarY + L.searchBarH) {
                requestSearchDialog();
                return;
            }
            const bool loading = catalog.loading.load();
            const bool hasError = catalog.hasError.load();
            if (hasError && !loading) {
                if (x >= L.x0 + 28.f && x < L.x0 + 28.f + 220.f &&
                    y >= L.gridY + 100.f && y < L.gridY + 144.f) {
                    catalog.fetchGames(activity, storeSearchQuery);
                }
                return;
            }
            if (!loading) {
                std::vector<GameEntry> games = catalog.snapshot();
                const int visibleCount = std::min(static_cast<int>(games.size()), L.maxVisible);
                for (int i = 0; i < visibleCount; ++i) {
                    const int col = i % L.columns;
                    const int row = i / L.columns;
                    const float cardX = L.x0 + col * (L.cardW + L.gap);
                    const float cardY = L.gridY + row * (L.cardH + L.gap);
                    if (x >= cardX && x < cardX + L.cardW && y >= cardY && y < cardY + L.cardH) {
                        openGameDetail(games[i].id);
                        return;
                    }
                }
            }
            return;
        }

        if (screen == Screen::Library) {
            refreshLibraryGames();
            const LibraryLayout layout = computeLibraryLayout();
            const size_t pageStart = (librarySelection / static_cast<size_t>(layout.maxVisible))
                    * static_cast<size_t>(layout.maxVisible);
            const size_t pageEnd = std::min(libraryGames.size(), pageStart + layout.maxVisible);
            for (size_t index = pageStart; index < pageEnd; ++index) {
                const size_t localIndex = index - pageStart;
                const int column = static_cast<int>(localIndex % layout.columns);
                const int row = static_cast<int>(localIndex / layout.columns);
                const float cardX = layout.x0 + column * (layout.cardW + layout.gap);
                const float cardY = layout.gridY + row * (layout.cardH + layout.gap);
                if (x >= cardX && x < cardX + layout.cardW &&
                    y >= cardY && y < cardY + layout.cardH) {
                    librarySelection = index;
                    attemptLaunchSelectedIso();
                    return;
                }
            }
            return;
        }

        if (screen == Screen::Home) {
            const float margin = 28.f;
            const float profileW = 240.f;
            const float profileX = w - margin - profileW;
            const float profileY = kTopBarH + margin;
            if (x >= profileX && x < profileX + profileW &&
                y >= profileY && y < profileY + 66.f) {
                profileSelection.setSelectedProfile(profileSelection.activeProfile());
                screen = Screen::ProfileSelect;
                return;
            }
            const float heroY = kTopBarH + margin;
            const float heroH = 258.f;
            const float heroW = w - kSidebarW - margin * 2.f - profileW - 18.f;
            if (x >= kSidebarW + margin && x < kSidebarW + margin + heroW &&
                y >= heroY && y < heroY + heroH) {
                focusedNavigation = static_cast<int>(Screen::Library);
                screen = Screen::Library;
                return;
            }
        }
    }

    // Controles genericos (USB, Bluetooth ou receptor sem fio "pendrive")
    // as vezes mandam BUTTON_1..16 em vez de A/B/X/Y: traduz pros nomes padrao.
    static int normalizeControllerKey(int keyCode) {
        switch (keyCode) {
            case AKEYCODE_BUTTON_1: return AKEYCODE_BUTTON_A;
            case AKEYCODE_BUTTON_2: return AKEYCODE_BUTTON_B;
            case AKEYCODE_BUTTON_3: return AKEYCODE_BUTTON_X;
            case AKEYCODE_BUTTON_4: return AKEYCODE_BUTTON_Y;
            case AKEYCODE_BUTTON_9: return AKEYCODE_BUTTON_SELECT;
            case AKEYCODE_BUTTON_10: return AKEYCODE_BUTTON_START;
            case AKEYCODE_BUTTON_C: return AKEYCODE_BUTTON_Y;
            case AKEYCODE_BUTTON_Z: return AKEYCODE_BUTTON_X;
            case AKEYCODE_NUMPAD_ENTER:
            case AKEYCODE_SPACE: return AKEYCODE_BUTTON_A;
            default: return keyCode;
        }
    }

    // Navegacao da Xenon Store pelo controle (grade de cards + busca).
    void handleStoreKey(int keyCode) {
        const StoreLayout L = computeStoreLayout();
        const bool loading = catalog.loading.load();
        const bool hasError = catalog.hasError.load();
        const int count = loading ? 0 : std::min(static_cast<int>(catalog.snapshot().size()), L.maxVisible);
        if (storeFocus > count) storeFocus = count;
        const int idx = storeFocus - 1;
        switch (keyCode) {
            case AKEYCODE_DPAD_LEFT:
                if (storeFocus <= 0 || idx % L.columns == 0) storeFocus = -1;
                else --storeFocus;
                break;
            case AKEYCODE_DPAD_RIGHT:
                if (storeFocus >= 1 && idx + 1 < count) ++storeFocus;
                break;
            case AKEYCODE_DPAD_UP:
                if (storeFocus >= 1) storeFocus = idx < L.columns ? 0 : storeFocus - L.columns;
                break;
            case AKEYCODE_DPAD_DOWN:
                if (storeFocus == 0) { if (count > 0) storeFocus = 1; }
                else if (idx + L.columns < count) storeFocus += L.columns;
                break;
            case AKEYCODE_DPAD_CENTER:
            case AKEYCODE_ENTER:
            case AKEYCODE_BUTTON_A:
            case AKEYCODE_BUTTON_START:
                if (hasError && !loading) { catalog.fetchGames(activity, storeSearchQuery); break; }
                if (storeFocus == 0) requestSearchDialog();
                else if (storeFocus >= 1) {
                    std::vector<GameEntry> games = catalog.snapshot();
                    if (idx < static_cast<int>(games.size())) openGameDetail(games[idx].id);
                }
                break;
            case AKEYCODE_BUTTON_Y:
                requestSearchDialog();
                break;
            case AKEYCODE_BACK:
            case AKEYCODE_ESCAPE:
            case AKEYCODE_BUTTON_B:
            case AKEYCODE_BUTTON_SELECT:
                storeFocus = -1;
                break;
            default:
                break;
        }
    }

    void handleLibraryKey(int keyCode) {
        refreshLibraryGames();
        if (libraryGames.empty()) {
            if (keyCode == AKEYCODE_BACK || keyCode == AKEYCODE_ESCAPE ||
                keyCode == AKEYCODE_BUTTON_B || keyCode == AKEYCODE_BUTTON_SELECT) {
                screen = Screen::Home;
                focusedNavigation = static_cast<int>(Screen::Home);
            } else if (keyCode == AKEYCODE_BUTTON_Y) {
                libraryScannedAt = {};
                refreshLibraryGames();
                libraryStatus = "Biblioteca atualizada.";
            }
            return;
        }
        const LibraryLayout layout = computeLibraryLayout();
        switch (keyCode) {
            case AKEYCODE_DPAD_LEFT:
                if (librarySelection > 0) --librarySelection;
                break;
            case AKEYCODE_DPAD_RIGHT:
                if (librarySelection + 1 < libraryGames.size()) ++librarySelection;
                break;
            case AKEYCODE_DPAD_UP:
                if (librarySelection >= static_cast<size_t>(layout.columns)) {
                    librarySelection -= static_cast<size_t>(layout.columns);
                }
                break;
            case AKEYCODE_DPAD_DOWN:
                if (librarySelection + static_cast<size_t>(layout.columns) < libraryGames.size()) {
                    librarySelection += static_cast<size_t>(layout.columns);
                }
                break;
            case AKEYCODE_DPAD_CENTER:
            case AKEYCODE_ENTER:
            case AKEYCODE_BUTTON_A:
            case AKEYCODE_BUTTON_START:
                attemptLaunchSelectedIso();
                break;
            case AKEYCODE_BUTTON_Y:
                libraryScannedAt = {};
                refreshLibraryGames();
                libraryStatus = "Biblioteca atualizada.";
                break;
            case AKEYCODE_BACK:
            case AKEYCODE_ESCAPE:
            case AKEYCODE_BUTTON_B:
            case AKEYCODE_BUTTON_SELECT:
                screen = Screen::Home;
                focusedNavigation = static_cast<int>(Screen::Home);
                break;
            default:
                break;
        }
    }

    void handleKey(int keyCode) {
        keyCode = normalizeControllerKey(keyCode);
        if (showGameDetail) {
            switch (keyCode) {
                case AKEYCODE_BACK:
                case AKEYCODE_ESCAPE:
                case AKEYCODE_BUTTON_B:
                case AKEYCODE_BUTTON_SELECT:
                    closeGameDetail();
                    break;
                case AKEYCODE_DPAD_LEFT:
                    --detailScreenshotIndex;
                    break;
                case AKEYCODE_DPAD_RIGHT:
                    ++detailScreenshotIndex;
                    break;
                case AKEYCODE_DPAD_CENTER:
                case AKEYCODE_ENTER:
                case AKEYCODE_BUTTON_A:
                case AKEYCODE_BUTTON_START: {
                    GameEntry game;
                    if (catalog.findById(selectedGameId, game)) {
                        downloadStoreGame(game.link, game.title, game.sha256, game.id, game.cover);
                    }
                    break;
                }
                default:
                    break;
            }
            return;
        }
        if (screen == Screen::ProfileSelect) {
            applyProfileAction(profileSelection.handleKey(keyCode));
            return;
        }
        if (screen == Screen::Store && storeFocus >= 0) {
            handleStoreKey(keyCode);
            return;
        }
        if (screen == Screen::Library) {
            handleLibraryKey(keyCode);
            return;
        }
        switch (keyCode) {
            case AKEYCODE_DPAD_RIGHT:
                // Do menu lateral entra na loja (vai para o primeiro jogo).
                if (screen == Screen::Store) storeFocus = catalog.snapshot().empty() ? 0 : 1;
                break;
            case AKEYCODE_DPAD_UP:
                focusedNavigation = (focusedNavigation + 4) % 5;
                break;
            case AKEYCODE_DPAD_DOWN:
                focusedNavigation = (focusedNavigation + 1) % 5;
                break;
            case AKEYCODE_DPAD_CENTER:
            case AKEYCODE_ENTER:
            case AKEYCODE_BUTTON_A:
            case AKEYCODE_BUTTON_START:
                screen = static_cast<Screen>(focusedNavigation);
                if (screen == Screen::Store) storeFocus = catalog.snapshot().empty() ? 0 : 1;
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

        // Na loja (e nos detalhes do jogo) o analogico/direcional anda nas
        // duas direcoes, convertido em teclas do direcional.
        if (showGameDetail || screen == Screen::Store || screen == Screen::Library) {
            const float lY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
            const float hY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);
            const float horiz = std::abs(hatX) > .5f ? hatX : leftX;
            const float vert = std::abs(hY) > .5f ? hY : lY;
            int key = 0;
            int dir = 0;
            if (std::abs(horiz) >= std::abs(vert)) {
                if (horiz < -.55f) { key = AKEYCODE_DPAD_LEFT; dir = 3; }
                else if (horiz > .55f) { key = AKEYCODE_DPAD_RIGHT; dir = 4; }
            } else {
                if (vert < -.55f) { key = AKEYCODE_DPAD_UP; dir = 1; }
                else if (vert > .55f) { key = AKEYCODE_DPAD_DOWN; dir = 2; }
            }
            if (dir == 0) { controllerDirection = 0; return; }
            const auto now = std::chrono::steady_clock::now();
            if (dir == controllerDirection && now < controllerRepeatAt) return;
            controllerDirection = dir;
            controllerRepeatAt = now + std::chrono::milliseconds(260);
            handleKey(key);
            return;
        }

        // A barra de navegacao agora e vertical (lateral), entao o controle
        // usa o eixo vertical do analogico/direcional em vez do horizontal.
        const float leftY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_Y, 0);
        const float rightY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_RY, 0);
        const float hatY = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HAT_Y, 0);
        float vertical = std::abs(leftY) >= std::abs(rightY) ? leftY : rightY;
        if (std::abs(hatY) > .5f) vertical = hatY;
        const int direction = vertical < -.55f ? -1 : vertical > .55f ? 1 : 0;
        if (direction == 0) {
            controllerDirection = 0;
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (direction == controllerDirection && now < controllerRepeatAt) return;
        controllerDirection = direction;
        controllerRepeatAt = now + std::chrono::milliseconds(300);
        focusedNavigation = (focusedNavigation + 5 + direction) % 5;
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

extern "C" JNIEXPORT void JNICALL
Java_com_xboxemulator_XenonActivity_nativeOnStoreSearch(
    JNIEnv* env, jobject, jstring query) {
    std::string text;
    if (query) {
        const char* utfQuery = env->GetStringUTFChars(query, nullptr);
        if (utfQuery) {
            text = utfQuery;
            env->ReleaseStringUTFChars(query, utfQuery);
        }
    }
    std::lock_guard<std::mutex> lock(pendingSearch.mutex);
    pendingSearch.query = std::move(text);
    pendingSearch.ready = true;
}

extern "C" JNIEXPORT void JNICALL
Java_com_xboxemulator_XenonActivity_nativeOnStoreDownloadProgress(
    JNIEnv* env, jclass, jstring gameId, jlong downloaded, jlong total, jint state) {
    if (!gameId) return;
    const char* utfGameId = env->GetStringUTFChars(gameId, nullptr);
    if (!utfGameId) return;
    StoreDownloadProgress update;
    update.gameId = utfGameId;
    update.downloaded = downloaded > 0 ? static_cast<uint64_t>(downloaded) : 0;
    update.total = total > 0 ? static_cast<uint64_t>(total) : 0;
    update.state = state;
    env->ReleaseStringUTFChars(gameId, utfGameId);
    std::lock_guard<std::mutex> lock(pendingStoreDownloadProgress.mutex);
    pendingStoreDownloadProgress.updates.push_back(std::move(update));
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
        std::string submittedQuery;
        if (takePendingSearch(submittedQuery)) {
            state.handleSearchSubmitted(submittedQuery);
        }
        std::vector<StoreDownloadProgress> downloadUpdates;
        takePendingStoreDownloadUpdates(downloadUpdates);
        for (auto& update : downloadUpdates) {
            if (update.state == 3) state.storeDownloads.erase(update.gameId);
            else state.storeDownloads[update.gameId] = std::move(update);
        }
        if (state.initialized) state.draw();
    }
    state.profileSelection.shutdown();
    state.renderer.shutdown();
}
