# Como contribuir

Obrigado pelo interesse. Este documento é curto de propósito — prefiro
colocar o essencial e evoluir conforme a comunidade cresce.

## Antes de abrir uma issue

- Verifique se já existe issue semelhante.
- Descreva o problema com passos reproduzíveis.
- Para bugs de emulação, informe: jogo/XEX, dispositivo, versão do app,
  log (`adb logcat`).

## Antes de enviar um pull request

1. Abra uma issue descrevendo a mudança (mesmo que seja pequena).
2. Um PR por assunto. Não misture refactor com feature.
3. Todo código novo precisa de teste na pasta `Tests/`.
4. Rode `ctest --test-dir build --output-on-failure` e garanta verde.
5. Atualize a documentação relevante (`README.txt`, `docs/`, `coverage`).
6. Formato de commit: `área: descrição curta` — ex.: `ppc: implementa rlwinm`.

## Estilo de código

- C++17, sem extensões proprietárias.
- Classes: `PascalCase` (`PPCDecoder`).
- Funções e variáveis: `snake_case` (`decode_instruction`).
- Constantes: `kPascalCase` (`kInstructionSize`).
- Membros privados: sufixo `_` (`program_counter_`).
- `#pragma once` em headers.
- Sem `using namespace` em headers.
- Prefira `std::` explícito.
- Comentários em português (consistência com o README).

## Estrutura de pastas (resumo)

- `Core/` — núcleo da emulação (CPU, GPU, memória, barramentos).
- `System/` — kernel stub, XEX loader, FATX, XAM.
- `Graphics/` — backend Vulkan.
- `Audio/` — mixer e decoders.
- `Input/` — controles.
- `Android/` — app (Kotlin + JNI).
- `Compatibility/` — perfis por jogo.
- `Tests/` — testes unitários e de integração.
- `docs/` — documentação técnica (design docs, ADRs).

## Comunicação

- Issues para bugs e propostas.
- Discussions para dúvidas abertas e design.
- Pull requests para código.

## Licença

Ao contribuir, você concorda que sua contribuição é licenciada sob GPLv3.
