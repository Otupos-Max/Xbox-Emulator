================================================================================
XBOX EMULATOR
Emulador experimental de Xbox 360 para Android
================================================================================

O Xbox Emulator é um projeto independente de emulação desenvolvido do zero,
com o objetivo de reproduzir a arquitetura de hardware e software do Xbox 360
por meio de software, permitindo futuramente executar jogos compatíveis em
dispositivos Android.

O projeto busca oferecer controle total sobre sua arquitetura interna, seus
componentes e suas implementações.

Emulação própria. Arquitetura própria. Desenvolvimento do zero.


================================================================================
OBJETIVOS
================================================================================

- Emular a CPU Xenon (PowerPC + VMX/AltiVec + SMT).
- Emular a GPU Xenos (shaders unificados + eDRAM).
- Reproduzir o gerenciamento de memória unificada do console.
- Implementar os componentes e barramentos de hardware.
- Desenvolver os serviços fundamentais do sistema Xbox 360 (kernel stub, XAM).
- Implementar carregamento e execução de pacotes XEX.
- Implementar renderização gráfica via Vulkan.
- Reproduzir o sistema de áudio.
- Implementar controles e dispositivos de entrada.
- Desenvolver uma interface própria para Android.
- Permitir o carregamento e a execução de jogos compatíveis.


================================================================================
NÃO-OBJETIVOS (ESCOPO PERMANENTE)
================================================================================

Os itens abaixo NÃO fazem parte do escopo do projeto e não serão implementados:

- Emulação do Kinect.
- Emulação de Xbox Live (serviços online).
- Emulação de periféricos raros (volantes específicos, câmeras).
- Distribuição de jogos, firmware, BIOS ou qualquer arquivo protegido.
- Compatibilidade com Xbox original (primeira geração).
- Compatibilidade com Xbox One / Series X|S.
- Portes para plataformas que não sejam Android/ARM64 no escopo inicial
  (x86-64 desktop pode ser considerado futuramente como plataforma de testes).


================================================================================
METAS DE VIABILIDADE E DESEMPENHO
================================================================================

O projeto reconhece que emular Xbox 360 em ARM64/Android é um dos alvos mais
difíceis possíveis. Para evitar promessas vazias, o desenvolvimento é dividido
em dois estágios com metas mensuráveis.

Estágio 1 — Prova de Conceito (PoC)
------------------------------------
- Meta: executar um XEX homebrew "hello world" e exibir texto na tela.
- Requisitos: boot stub do kernel, XEX loader, PPU interpreter funcional,
  memory mapper, framebuffer básico.
- Desempenho-alvo: > 30 fps em Snapdragon 8 Gen 1 ou superior para o hello
  world (não para jogos comerciais).
- Prazo estimado: não definido; condicionado à conclusão das etapas 1 a 5 do
  roadmap.

Estágio 2 — Produto Experimental
---------------------------------
- Meta: executar jogos comerciais simples (títulos 2D, indies, XBLA) em
  resolução reduzida.
- Requisitos: JIT funcional, Xenos → Vulkan, sistema XAM mínimo, FATX.
- Desempenho-alvo: > 30 fps em resolução 480p-720p em dispositivos topo de
  linha contemporâneos.
- Prazo estimado: não definido; depende de financiamento/colaboração.

Dispositivos de referência
--------------------------
- Snapdragon 8 Gen 1 / 8 Gen 2 (ARM64, Adreno).
- Dimensity 9200+ (ARM64, Mali).
- Android 12+ (NDK r26+).
- Mínimo: 8 GB RAM (o Xbox 360 tem 512 MB unificados; overhead de emulação
  exige folga).


================================================================================
ESTRATÉGIA DE TRADUÇÃO PPC → ARM64
================================================================================

Traduzir PowerPC para ARM64 no Android é o maior desafio técnico do projeto.
A estratégia adotada é híbrida e evolutiva:

Fase 1 — Interpretador (atual)
-------------------------------
- Implementação direta: FETCH → DECODE → EXECUTE → UPDATE STATE.
- Vantagens: portável, depurável, base para validação semântica.
- Desvantagens: lento (~50-200x mais lento que nativo).
- Uso: testes unitários, validação de instruções, boot de homebrew.

Fase 2 — Recompilador por Blocos Básicos (AOT por módulo)
---------------------------------------------------------
- Análise estática do XEX no carregamento.
- Tradução de blocos básicos PPC → código intermediário (IR).
- Execução via interpretador da IR.
- Vantagens: mais rápido que interpretador puro, sem W^X.
- Uso: primeiros jogos homebrew, validação de desempenho.

Fase 3 — JIT com Cache de Código
---------------------------------
- Tradução dinâmica de blocos básicos para ARM64 em runtime.
- Memória executável alocada via mmap + mprotect (PROT_EXEC).
- Fallback para interpretador em instruções não suportadas.
- Requisitos: contornar restrições W^X do Android 10+ (uso de JIT explícito
  via android:usesCleartextTraffic e JIT compiler API quando disponível).
- Uso: jogos comerciais.

Fase 4 — Otimizações por Perfil
--------------------------------
- Perfil de execução para identificar hot paths.
- Recompilação otimizada de blocos quentes.
- Vetorização ARM64 NEON para VMX/AltiVec.

Observação importante
---------------------
Sem JIT funcional, a meta do Estágio 2 é matematicamente improvável. O
desenvolvimento do JIT é tratado como item crítico do roadmap, não opcional.


================================================================================
ARQUITETURA
================================================================================

O projeto é dividido em módulos independentes, permitindo desenvolver,
testar e aprimorar cada componente separadamente.

Core
----
Responsável pelo núcleo da emulação.

CPU
---
- Emulação dos três núcleos Xenon.
- Gerenciamento dos seis contextos de hardware (2 por núcleo).
- Emulação de SMT (2 threads por núcleo, pipeline compartilhado).
- Decodificação de instruções PowerPC.
- Emulação de VMX/AltiVec (128 bits, obrigatório para jogos comerciais).
- Gerenciamento de MMIO e registradores especiais (SPR, GPR, FPR, VR).
- Gerenciamento de caches L1 (32 KB I/D por núcleo) e L2 (1 MB compartilhado).
- Modelo de timing por ciclos (aproximado).

GPU
---
- Emulação da arquitetura gráfica Xenos.
- Emulação de shaders unificados (48 ALUs VEC + 16 ALUs escalares).
- Emulação da eDRAM (10 MB) e dos 192 ALUs de blending.
- Emulação de tessellation por hardware.
- Tradução de comandos gráficos para Vulkan.

Memory
------
- Gerenciamento da memória unificada de 512 MB GDDR3 (CPU + GPU).
- Gerenciamento de memória virtual.
- Leitura e escrita de dados.
- Integração com os componentes do console.
- Tratamento de MMIO (mapeamento de registradores em espaço de memória).

Bus
---
- Comunicação entre os componentes virtuais.
- Transferência de dados entre os dispositivos.
- Modelagem dos barramentos internos (FSB, PCIe virtual).

System
------
Responsável pela implementação dos componentes fundamentais do sistema do
Xbox 360:
- Kernel stub (boot mínimo).
- Serviços de sistema (XAM).
- Sistema de arquivos FATX (HDD e NAND).
- XEX loader (carregamento de executáveis).
- Gerenciamento de threads e sincronização.

Graphics
--------
Responsável pela renderização gráfica e pela comunicação entre a GPU emulada
e as APIs gráficas disponíveis no Android (Vulkan).

Audio
-----
Responsável pela reprodução e processamento de áudio (XMA decoder, mixer).

Input
-----
Gerenciamento dos controles, comandos e dispositivos de entrada.

Android
-------
Camada responsável pela integração com o Android:
- Interface do aplicativo (Kotlin).
- Comunicação entre Kotlin e C++ via JNI.
- Ciclo de vida da Activity.
- Permissões e armazenamento.

Compatibility
-------------
Responsável pelos mecanismos de compatibilidade e suporte à execução de
diferentes jogos:
- Perfis por título.
- Workarounds para jogos específicos.
- Base de compatibilidade pública.

Tests
-----
Testes individuais dos componentes e validação das implementações:
- Testes unitários do PPCDecoder e PPCInterpreter.
- Testes de integração CPU + Memory.
- Testes de regressão de compatibilidade.


================================================================================
ESTRUTURA DO PROJETO
================================================================================

Xbox Emulator/
├── Core/
│   ├── CPU/
│   │   ├── PowerPC/
│   │   │   ├── PPCDecoder.hpp
│   │   │   ├── PPCDecoder.cpp
│   │   │   ├── PPCInterpreter.hpp
│   │   │   ├── PPCInterpreter.cpp
│   │   │   ├── PPCVMX.hpp
│   │   │   └── PPCVMX.cpp
│   │   ├── XenonCPU.hpp
│   │   └── XenonCPU.cpp
│   ├── GPU/
│   │   ├── XenosGPU.hpp
│   │   ├── XenosGPU.cpp
│   │   ├── XenosShader.hpp
│   │   └── XenosEDRAM.hpp
│   ├── Memory/
│   │   ├── MemoryManager.hpp
│   │   └── MemoryManager.cpp
│   └── Bus/
│       ├── Bus.hpp
│       └── Bus.cpp
├── System/
│   ├── Kernel/
│   ├── XEXLoader/
│   ├── FATX/
│   └── XAM/
├── Graphics/
│   └── VulkanBackend/
├── Audio/
├── Input/
├── Android/
│   ├── app/
│   └── jni/
├── app/
│   └── src/main/
│       ├── AndroidManifest.xml
│       ├── cpp/
│       ├── res/drawable/
│       └── assets/
├── Compatibility/
├── Tests/
│   ├── PPC/
│   ├── CPU/
│   └── Integration/
├── CMakeLists.txt
├── LICENSE
└── main.cpp


================================================================================
TECNOLOGIAS
================================================================================

Tecnologia        | Aplicação
------------------|----------------------------------------------------------
C++17             | Núcleo da emulação
Kotlin            | Integração Android
Android NDK       | Execução de código nativo
CMake             | Sistema de compilação
Vulkan            | Renderização gráfica planejada
ARM64             | Plataforma de execução
JNI               | Ponte Kotlin ↔ C++
GoogleTest        | Framework de testes


================================================================================
CPU XENON
================================================================================

A CPU Xenon é o processador utilizado pelo Xbox 360.

Arquitetura:
- 3 núcleos físicos (baseados no IBM Cell PPE).
- 2 contextos de hardware (threads) por núcleo, via SMT (Simultaneous
  Multithreading).
- 6 contextos de execução no total.
- Pipeline in-order compartilhado entre as duas threads de cada núcleo.
- VMX/AltiVec: extensão SIMD de 128 bits, presente em todos os núcleos.
- Caches: L1 de 32 KB (instruções) + 32 KB (dados) por núcleo; L2 unificado
  de 1 MB compartilhado entre os três núcleos.
- Registradores: 32 GPR (64 bits), 32 FPR (64 bits), 128 VR (128 bits),
  SPRs diversos (LR, CTR, XER, CR, MSR, etc.).

O projeto possui uma implementação inicial da estrutura da CPU, com três
núcleos virtuais e dois contextos de interpretação por núcleo.


================================================================================
GPU XENOS
================================================================================

A GPU Xenos é o processador gráfico do Xbox 360.

Arquitetura:
- Arquitetura unificada de shaders (vertex + pixel no mesmo pool).
- 48 ALUs vetoriais (VEC) + 16 ALUs escalares.
- eDRAM de 10 MB na daughter die, com 192 ALUs de blending dedicadas.
- Largura de banda interna muito alta (eDRAM a 256 GB/s).
- Tessellation por hardware (anterior ao DirectX 11).
- Memória unificada com a CPU (512 MB GDDR3).
- Suporte a formatos de textura e compressão específicos.

Desafios para emulação:
- Nenhuma GPU móvel possui eDRAM equivalente; a solução é simular via
  resolução escalonada e tile-based rendering em Vulkan.
- Shaders unificados exigem tradução para o modelo Vulkan (SPIR-V).
- Tessellation por hardware precisa ser mapeada para tessellation por
  shader no Vulkan (feature opcional).


================================================================================
POWERPC INTERPRETER
================================================================================

O interpretador é responsável por executar as instruções da arquitetura
PowerPC.

Ciclo de execução:

  FETCH
    ↓
  DECODE
    ↓
  EXECUTE
    ↓
  UPDATE STATE

Componentes atuais:
- PPCDecoder: decodificação inicial das instruções.
- PPCInterpreter: interpretação e execução experimental.
- XenonCPU: gerenciamento dos núcleos e contextos.

Cobertura atual de instruções (atualizada em [DATA]):
-----------------------------------------------------
Categoria          | Status
-------------------|--------------------------------------------------------
Aritmética inteira | Parcial (add, sub, addi, addis)
Lógica             | Parcial (and, or, xor, andi., ori, xori)
Carregamento (load)| Parcial (lwz, lbz, lhz)
Armazenamento (st) | Parcial (stw, stb, sth)
Controle de fluxo  | Parcial (b, bl, bc, beq, bne)
Comparação         | Parcial (cmp, cmpi, cmpl)
Rotação/Shift      | Não implementado
Ponto flutuante    | Não implementado
VMX/AltiVec        | Não implementado
SPR                | Não implementado
Exceções/Interrupções | Não implementado
System call        | Não implementado

A implementação atual é um protótipo. A cobertura de instruções, a semântica
do PowerPC e a integração com a memória ainda precisam ser ampliadas e
validadas.

Roadmap de cobertura:
1. Completar categorias básicas (aritmética, lógica, load/store, controle).
2. Implementar ponto flutuante (FPR, FPSCR).
3. Implementar rotação/shift.
4. Implementar VMX/AltiVec (prioridade alta para jogos).
5. Implementar SPRs (LR, CTR, XER, CR, MSR).
6. Implementar exceções e interrupções.
7. Implementar system calls (sc).
8. Validação cruzada com testes de referência (IBM PPU manual).


================================================================================
DESENVOLVIMENTO
================================================================================

O desenvolvimento está organizado em etapas progressivas, reordenadas para
priorizar infraestrutura mínima de execução antes do subsistema gráfico.

Fase A — Fundação
------------------
1. Estrutura inicial da CPU Xenon (concluído — protótipo).
2. Interpretador PowerPC com cobertura básica (em andamento).
3. Sistema de memória (mapeamento, MMIO, memória unificada).
4. Gerenciamento dos seis contextos de hardware (SMT).
5. Barramentos e comunicação entre componentes.

Fase B — Sistema mínimo executável
-----------------------------------
6. Boot stub do kernel.
7. XEX loader (carregamento de executáveis).
8. FATX (sistema de arquivos).
9. XAM mínimo (serviços que jogos consultam na inicialização).
10. Framebuffer básico (hello world na tela).

Fase C — Subsistemas de mídia
------------------------------
11. Emulação da GPU Xenos (shaders unificados, eDRAM, tessellation).
12. Tradução Xenos → Vulkan.
13. Sistema de áudio (XMA decoder, mixer).
14. Controles e dispositivos de entrada.

Fase D — Android e produto
---------------------------
15. Integração com Android (JNI, UI).
16. JIT PPC → ARM64.
17. Carregamento e execução de jogos comerciais simples.
18. Testes de compatibilidade e desempenho.

Fase E — Otimização
--------------------
19. Otimizações NEON para VMX.
20. Perfis por jogo e workarounds.
21. Base pública de compatibilidade.

Observação: a GPU foi movida para a Fase C. Sem kernel, XEX loader e
framebuffer, não há como validar a GPU com nada real. A ordem original
(GPU na etapa 6) foi considerada arriscada por desenvolver no vácuo.


================================================================================
FILOSOFIA DO PROJETO
================================================================================

O Xbox Emulator é desenvolvido com foco em:

- Controle: implementação própria dos componentes, sem depender de outro
  emulador como núcleo.
- Modularidade: arquitetura organizada e expansível.
- Desempenho: otimização para dispositivos ARM64.
- Compatibilidade: reprodução progressiva das funcionalidades do Xbox 360.
- Independência: desenvolvimento da implementação do zero.
- Honestidade técnica: metas mensuráveis, escopo declarado, limitações
  documentadas publicamente.


================================================================================
ESTADO ATUAL
================================================================================

Em desenvolvimento.

Atualmente o projeto possui:
- Estrutura inicial da CPU Xenon (3 núcleos, 2 contextos cada).
- Decoder PowerPC experimental.
- Interpretador PowerPC inicial (cobertura parcial — ver seção POWERPC
  INTERPRETER).
- Integração entre os núcleos e seus contextos.

Ainda NÃO possui:
- Capacidade de executar jogos de Xbox 360.
- VMX/AltiVec.
- Emulação de GPU funcional.
- Boot stub de kernel.
- XEX loader.
- JIT.

Última atualização do estado: [DATA]


================================================================================
LICENÇA
================================================================================

Este projeto é distribuído sob a licença GNU General Public License v3.0
(GPLv3).

Isso significa que:
- O código pode ser usado, modificado e redistribuído.
- Trabalhos derivados devem permanecer sob a mesma licença.
- O código-fonte deve ser disponibilizado a quem receber o binário.

A escolha da GPLv3 reflete o compromisso do projeto com a abertura e com a
devolução de melhorias à comunidade.

Para detalhes, consulte o arquivo LICENSE no repositório.


================================================================================
ANDROID NATIVO
================================================================================

O aplicativo Android é implementado nativamente em C++ com NativeActivity,
OpenGL ES 2.0 e o Android Native App Glue. Não existe interface web no projeto.

Estrutura:

- app/src/main/AndroidManifest.xml: configuração da atividade nativa
- app/src/main/cpp/xenon_native.cpp: interface, navegação e ciclo de vida
- app/src/main/cpp/CMakeLists.txt: ligação do núcleo C++ com EGL/GLES
- app/src/main/res/drawable/: recursos gráficos do APK

Compilar em uma máquina Android configurada:

  export ANDROID_HOME=/caminho/para/android-sdk
  export ANDROID_NDK_HOME=$ANDROID_HOME/ndk/26.3.11579264
  ./gradlew assembleDebug

O APK será gerado em app/build/outputs/apk/debug/app-debug.apk.


================================================================================
COMO CONTRIBUIR
================================================================================

Contribuições são bem-vindas, especialmente nas áreas de:
- Cobertura de instruções PowerPC e VMX.
- Testes unitários do PPCDecoder e PPCInterpreter.
- Documentação técnica.
- Tradução de comandos Xenos → Vulkan.
- Análise de compatibilidade de jogos específicos.

Antes de enviar pull requests:
1. Abra uma issue descrevendo a mudança.
2. Escreva testes para o código novo (pasta Tests/).
3. Siga o estilo de código do projeto (C++17, snake_case em arquivos,
   PascalCase em classes).
4. Atualize a documentação quando relevante.

Para colaboradores recorrentes, o projeto pode ser expandido com um
CONTRIBUTING.md detalhado. Até lá, use as issues do repositório.


================================================================================
AVISO LEGAL
================================================================================

Xbox e Xbox 360 são marcas registradas da Microsoft Corporation.

Este é um projeto independente, sem associação ou afiliação oficial com a
Microsoft.

O projeto não fornece jogos, firmware, BIOS ou arquivos protegidos por
direitos autorais. A utilização desses arquivos deve respeitar as leis
aplicáveis e os direitos de seus respectivos proprietários.

O usuário é o único responsável pelo uso de arquivos de jogos e firmware,
que devem ser obtidos legalmente a partir de mídias que possua.

Este emulador é fornecido "como está", sem garantias de qualquer tipo,
expressas ou implícitas, incluindo, mas não se limitando a, garantias de
comercialização, adequação a um propósito específico e não violação.

Em nenhuma circunstância os autores ou detentores de direitos autorais serão
responsáveis por qualquer reclamação, danos ou outra responsabilidade,
seja em ação contratual, ato ilícito ou de outra forma, decorrente de, ou em
conexão com, o software ou o uso ou outros negócios no software.


================================================================================
XBOX EMULATOR
Emulação própria. Arquitetura própria. Desenvolvimento do zero.
================================================================================
