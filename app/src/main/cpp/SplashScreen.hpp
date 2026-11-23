#pragma once

#include <GLES2/gl2.h>

namespace xenon {

// Tela de abertura do Xenon renderizada 100% na GPU (GLSL ES 2.0) e
// orquestrada em C++. Fases:
//   0.0s - 1.3s  Hiperespaco: tunel de luz acelerando para o centro
//   1.3s         Ignicao do nucleo: flash, onda de choque e flare anamorfico
//   1.4s - 3.0s  Montagem do emblema: aneis hexagonais, laminas em X, particulas
//   3.0s - 5.4s  Brilho especular, nebulosa de plasma e logotipo em glitch
//   5.4s - 6.2s  Saida: zoom para dentro do nucleo e fade
class SplashScreen {
public:
    static constexpr float kDuration = 6.2f;
    static constexpr float kOutroLength = 0.8f;
    static constexpr float kMinSkipTime = 0.6f;

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
