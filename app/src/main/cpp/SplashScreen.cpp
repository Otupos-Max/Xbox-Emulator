#include "SplashScreen.hpp"

#include <android/log.h>

namespace xenon {
namespace {

const char* kVertexShader = R"(
attribute vec2 aPos;
varying vec2 vUv;
void main() {
    vUv = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
})";

const char* kFragmentShader = R"(
precision mediump float;
varying vec2 vUv;
uniform vec2 uRes;
uniform float uTime;
uniform float uOutro;

const vec3 GREEN = vec3(0.212, 0.871, 0.408);
const vec3 DEEP  = vec3(0.012, 0.028, 0.020);

float sdHex(vec2 p, float r) {
    const vec3 k = vec3(-0.866025404, 0.5, 0.577350269);
    p = abs(p);
    p -= 2.0 * min(dot(k.xy, p), 0.0) * k.xy;
    p -= vec2(clamp(p.x, -k.z * r, k.z * r), r);
    return length(p) * sign(p.y);
}
float sdSeg(vec2 p, vec2 a, vec2 b) {
    vec2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

void main() {
    float aspect = uRes.x / uRes.y;
    vec2 p = (vUv - 0.5) * vec2(aspect, 1.0);
    p.y = -p.y;
    p.y += 0.06;
    float t = uTime;

    // Entrada suave: emblema cresce de 0.85 a 1.0; saida: leve zoom.
    float intro = smoothstep(0.0, 0.9, t);
    float scale = mix(0.85, 1.0, intro) * (1.0 + uOutro * 0.15);
    vec2 q = p / scale;
    float r = length(p);

    // Fundo: gradiente radial verde escuro.
    vec3 col = DEEP + GREEN * 0.10 * exp(-r * 3.0) * intro;

    // Anel hexagonal fino.
    float hex = abs(sdHex(q, 0.20));
    float ring = smoothstep(0.006, 0.0, hex) + 0.0025 / (hex + 0.01);
    col += GREEN * ring * 0.7 * smoothstep(0.1, 0.8, t);

    // X central com brilho.
    float d = min(sdSeg(q, vec2(-0.10, -0.10), vec2(0.10, 0.10)),
                  sdSeg(q, vec2(-0.10, 0.10), vec2(0.10, -0.10)));
    float xa = smoothstep(0.3, 1.0, t);
    col += mix(GREEN, vec3(1.0), 0.55) * smoothstep(0.022, 0.016, d) * xa;
    col += GREEN * (0.004 / (d + 0.02)) * xa;

    // Vinheta e fades.
    vec2 v = vUv - 0.5;
    col *= 1.0 - dot(v, v) * 0.9;
    col *= smoothstep(0.0, 0.35, t) * (1.0 - uOutro);
    gl_FragColor = vec4(col, 1.0);
})";

GLuint compile(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok != GL_TRUE) {
        char log[1024] = {};
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        __android_log_print(ANDROID_LOG_ERROR, "Xenon", "Splash shader error: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

bool SplashScreen::initialize() {
    shutdown();
    GLuint vs = compile(GL_VERTEX_SHADER, kVertexShader);
    GLuint fs = compile(GL_FRAGMENT_SHADER, kFragmentShader);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glBindAttribLocation(program_, 0, "aPos");
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        __android_log_print(ANDROID_LOG_ERROR, "Xenon", "Splash program link failed");
        glDeleteProgram(program_);
        program_ = 0;
        return false;
    }
    position_ = 0;
    resolution_ = glGetUniformLocation(program_, "uRes");
    time_ = glGetUniformLocation(program_, "uTime");
    outro_ = glGetUniformLocation(program_, "uOutro");

    static const float quad[] = {-1.f, -1.f, 1.f, -1.f, -1.f, 1.f, 1.f, 1.f};
    glGenBuffers(1, &buffer_);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    return true;
}

void SplashScreen::shutdown() {
    if (buffer_) glDeleteBuffers(1, &buffer_);
    if (program_) glDeleteProgram(program_);
    buffer_ = 0;
    program_ = 0;
}

void SplashScreen::draw(float logicalWidth, float logicalHeight, float elapsedSeconds,
                        float outro) {
    if (!program_) return;
    glUseProgram(program_);
    glUniform2f(resolution_, logicalWidth, logicalHeight);
    glUniform1f(time_, elapsedSeconds);
    glUniform1f(outro_, outro);
    glBindBuffer(GL_ARRAY_BUFFER, buffer_);
    glVertexAttribPointer(static_cast<GLuint>(position_), 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    glEnableVertexAttribArray(static_cast<GLuint>(position_));
    glDisable(GL_BLEND);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glEnable(GL_BLEND);
}

} // namespace xenon
