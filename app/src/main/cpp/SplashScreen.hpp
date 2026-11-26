#pragma once

#include <GLES2/gl2.h>

namespace xenon {

// Tela de abertura leve do Xenon (GLSL ES 2.0): emblema hexagonal com X
// que aparece com fade/zoom suave, ~2.4s no total, pulavel.
class SplashScreen {
public:
    static constexpr float kDuration = 2.4f;
    static constexpr float kOutroLength = 0.4f;
    static constexpr float kMinSkipTime = 0.3f;

    bool initialize();
    void shutdown();

    // outro: 0..1 (progresso da animacao de saida).
    void draw(float logicalWidth, float logicalHeight, float elapsedSeconds, float outro);
    bool ready() const { return program_ != 0; }

private:
    GLuint program_ = 0;
    GLuint buffer_ = 0;
    GLint position_ = -1;
    GLint resolution_ = -1;
    GLint time_ = -1;
    GLint outro_ = -1;
};

} // namespace xenon
