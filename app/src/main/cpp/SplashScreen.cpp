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
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
varying vec2 vUv;
uniform vec2 uRes;
uniform float uTime;
uniform float uOutro;

const float TAU = 6.28318530718;
const vec3 GREEN = vec3(0.212, 0.871, 0.408);
const vec3 CYAN  = vec3(0.30, 0.92, 1.00);
const vec3 DEEP  = vec3(0.010, 0.030, 0.022);

float hash11(float n) { return fract(sin(n * 127.1) * 43758.5453); }
float hash21(vec2 p) { return fract(sin(dot(p, vec2(41.3, 289.1))) * 45758.5453); }
mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, -s, s, c); }

float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash21(i), hash21(i + vec2(1.0, 0.0)), u.x),
               mix(hash21(i + vec2(0.0, 1.0)), hash21(i + vec2(1.0, 1.0)), u.x), u.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 3; i++) { v += a * noise(p); p = rot(0.6) * p * 2.03; a *= 0.5; }
    return v;
}
float sdHex(vec2 p, float r) {
    const vec3 k = vec3(-0.866025404, 0.5, 0.577350269);
    p = abs(p);
    p -= 2.0 * min(dot(k.xy, p), 0.0) * k.xy;
    p -= vec2(clamp(p.x, -k.z * r, k.z * r), r);
    return length(p) * sign(p.y);
}
float sdBox(vec2 p, vec2 b) {
    vec2 d = abs(p) - b;
    return length(max(d, 0.0)) + min(max(d.x, d.y), 0.0);
}
float ease(float a, float b, float t) { return smoothstep(a, b, t); }

// Emblema original do Xenon: nucleo + duas laminas em X + 3 aneis hexagonais.
vec3 emblem(vec2 p, float t) {
    vec3 col = vec3(0.0);
    float build = ease(1.35, 2.9, t);
    float ang = atan(p.y, p.x);

    for (int i = 0; i < 3; i++) {
        float fi = float(i);
        float reveal = ease(1.4 + fi * 0.22, 2.1 + fi * 0.22, t);
        float dir = mod(fi, 2.0) == 0.0 ? 1.0 : -1.0;
        float spin = dir * (t * (0.25 + fi * 0.12)) + (1.0 - reveal) * dir * 3.0;
        vec2 q = rot(spin) * p;
        float r = 0.20 + fi * 0.075 + (1.0 - reveal) * 0.35;
        float d = abs(sdHex(q, r));
        float qa = atan(q.y, q.x) / TAU + 0.5;
        float segCount = 6.0 + fi * 6.0;
        float seg = step(0.22 + fi * 0.1, fract(qa * segCount));
        float arc = step(qa, reveal);
        float glow = 0.0016 / (d + 0.0018) + smoothstep(0.004, 0.0, d) * 1.5;
        col += mix(GREEN, CYAN, fi * 0.45) * glow * seg * arc * reveal * (0.75 - fi * 0.15);
    }

    float bladeIn = ease(1.9, 2.6, t);
    float slide = (1.0 - bladeIn) * 0.6;
    vec2 a1 = rot(0.785398) * p; a1.x += slide;
    vec2 a2 = rot(-0.785398) * p; a2.x -= slide;
    float taper1 = 0.026 - abs(a1.x) * 0.06;
    float taper2 = 0.026 - abs(a2.x) * 0.06;
    float b1 = sdBox(a1, vec2(0.17, max(taper1, 0.004)));
    float b2 = sdBox(a2, vec2(0.17, max(taper2, 0.004)));
    float blade = min(b1, b2);
    blade = max(blade, -(length(p) - 0.045));
    float fill = smoothstep(0.003, 0.0, blade) * bladeIn;
    float edge = 0.0022 / (abs(blade) + 0.0022) * bladeIn;
    float sweep = exp(-pow((p.x + p.y * 0.6 - (mod(t - 2.4, 2.6) * 1.4 - 0.6)) * 9.0, 2.0));
    col += fill * mix(vec3(0.05, 0.25, 0.12), vec3(0.9, 1.0, 0.95), sweep);
    col += edge * GREEN * 0.9;

    float coreT = ease(1.1, 1.35, t);
    float beat = 0.8 + 0.2 * sin(t * 7.0) + 0.3 * exp(-mod(t - 1.3, 1.1) * 5.0) * step(1.3, t);
    float r0 = length(p);
    col += (0.010 / (r0 + 0.006)) * coreT * beat * mix(GREEN, vec3(1.0), 0.6);
    col += smoothstep(0.03, 0.0, r0) * coreT * vec3(1.0);

    for (int i = 0; i < 16; i++) {
        float fi = float(i);
        float h = hash11(fi + 3.0);
        float rad = 0.17 + 0.28 * hash11(fi * 1.7);
        float a = h * TAU + t * (0.4 + h * 0.9) * (mod(fi, 2.0) == 0.0 ? 1.0 : -1.0);
        vec2 pp = vec2(cos(a), sin(a) * 0.55) * rad * mix(2.5, 1.0, build);
        pp = rot(0.35) * pp;
        vec2 d = p - pp;
        float tw = 0.6 + 0.4 * sin(t * 6.0 + fi);
        col += mix(GREEN, CYAN, h) * (0.00006 / dot(d, d)) * build * tw;
    }
    col *= 1.0 + 0.08 * sin(ang * 6.0 + t * 2.0) * build;
    return col;
}

vec3 scene(vec2 p, float t) {
    float r = length(p);
    float ang = atan(p.y, p.x);
    vec3 col = DEEP * (1.0 - r * 0.6);

    // Hiperespaco: estrias de luz convergindo, aceleram ate a ignicao.
    float warp = ease(0.0, 1.25, t) * (1.0 - ease(1.3, 2.6, t) * 0.8);
    float cells = 90.0;
    float id = floor((ang / TAU + 0.5) * cells);
    float hid = hash11(id);
    float z = fract(hid * 9.0 - t * (0.35 + hid) * (0.5 + warp * 2.5));
    float rr = z * z * 1.6;
    float fa = abs(fract((ang / TAU + 0.5) * cells) - 0.5);
    float len = 0.02 + warp * 0.25 * z;
    float streak = smoothstep(len, 0.0, abs(r - rr)) * smoothstep(0.25, 0.0, fa) * step(0.45, hid);
    col += mix(CYAN, GREEN, hid) * streak * (0.3 + warp * 1.4) * z;

    // Nebulosa de plasma pos-ignicao.
    float neb = ease(1.3, 3.0, t);
    vec2 np = p * 2.2 + vec2(t * 0.04, -t * 0.03);
    vec2 nebulaWarp = vec2(sin(np.y * 1.7 + t * 0.08), cos(np.x * 1.7 - t * 0.08)) * 0.12;
    float f = fbm(np + nebulaWarp);
    col += mix(vec3(0.0, 0.06, 0.04), vec3(0.05, 0.30, 0.20), f * f) * f * neb * smoothstep(1.4, 0.1, r);

    // Grade hexagonal holografica pulsante.
    vec2 gp = p * 9.0;
    gp.x *= 1.1547;
    gp.y += mod(floor(gp.x), 2.0) * 0.5;
    vec2 gf = abs(fract(gp) - 0.5);
    float grid = smoothstep(0.47, 0.5, max(gf.x * 1.5 + gf.y, gf.y * 2.0));
    float wavefront = exp(-pow((r - (t - 1.3) * 0.9) * 6.0, 2.0)) * step(1.3, t);
    col += GREEN * grid * (0.03 + wavefront * 0.6) * neb * smoothstep(1.2, 0.3, r);

    return col;
}

void main() {
    float aspect = uRes.x / uRes.y;
    vec2 p = (vUv - 0.5) * vec2(aspect, 1.0);
    p.y = -p.y;
    float t = uTime;

    float zoom = 1.0 + uOutro * uOutro * 6.0;
    p /= zoom;
    p += vec2(sin(t * 47.0), cos(t * 53.0)) * 0.006 * exp(-max(t - 1.3, 0.0) * 4.0) * step(1.3, t);

    // Onda de choque distorce o espaco.
    float sw = (t - 1.3) * 1.3;
    float rp = length(p);
    float ring = exp(-pow((rp - sw) * 18.0, 2.0)) * step(1.3, t) * exp(-max(t - 1.3, 0.0) * 1.2);
    vec2 dp = p - normalize(p + 1e-5) * ring * 0.03;

    vec3 col = scene(dp, t);

    // Aberracao cromatica no emblema.
    float ca = 0.004 + ring * 0.02 + uOutro * 0.03 + 0.003 * step(0.97, hash11(floor(t * 12.0)));
    vec2 off = vec2(ca, 0.0);
    vec3 eg = emblem(dp, t);
    vec3 e;
    e.r = emblem(dp + off, t).r;
    e.g = eg.g;
    e.b = emblem(dp - off, t).b;
    e = max(e, eg * vec3(0.0, 1.0, 0.0)) + eg * vec3(0.2, 0.0, 0.2);
    col += e;

    col += mix(CYAN, vec3(1.0), 0.5) * ring * 1.2;

    // Flare anamorfico horizontal.
    float coreT = smoothstep(1.1, 1.4, t);
    float flare = exp(-abs(p.y) * 90.0) * exp(-abs(p.x) * 1.6) * coreT * (0.6 + 0.4 * exp(-max(t - 1.3, 0.0) * 2.0));
    col += mix(CYAN, GREEN, 0.4) * flare * 1.3;
    // Fantasmas de lente ao longo do eixo do nucleo.
    for (int i = 1; i < 4; i++) {
        float fi = float(i);
        vec2 gpos = vec2(-0.18 * fi, 0.05 * fi);
        float gd = length(p - gpos);
        col += mix(GREEN, CYAN, fi / 3.0) * smoothstep(0.05 * fi, 0.0, gd) * 0.08 * coreT;
    }

    // Flash da ignicao e flash de saida.
    col += vec3(0.85, 1.0, 0.92) * exp(-abs(t - 1.32) * 9.0) * step(1.0, t);
    col += vec3(0.8, 1.0, 0.9) * smoothstep(0.55, 0.85, uOutro) * 1.5;

    // Tonemap, scanlines, vinheta, granulacao.
    col = 1.0 - exp(-col * 1.4);
    float scan = 0.94 + 0.06 * sin(vUv.y * uRes.y * 1.6 + t * 30.0);
    col *= scan;
    vec2 v = vUv - 0.5;
    col *= 1.0 - dot(v, v) * 1.1;
    col += (hash21(vUv * uRes + fract(t) * 100.0) - 0.5) * 0.035;
    col *= ease(0.0, 0.35, t);
    col *= 1.0 - smoothstep(0.85, 1.0, uOutro);
    gl_FragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
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
