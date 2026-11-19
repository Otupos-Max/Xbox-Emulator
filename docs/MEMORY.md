================================================================================
XBOX EMULATOR — DESIGN: MEMORY MANAGER
Documento: docs/MEMORY.md (texto puro)
Versão: 0.1
Última atualização: [DATA]
Autor(es): [AUTOR]
Status: Proposta de design (aguardando revisão)
================================================================================

Este documento define a arquitetura do subsistema de memória do emulador:
o Memory Manager, o MMIO, o mapa de endereços do Xenon, o tratamento de
endianness e a integração com CPU e Bus.

O objetivo é ter uma base sólida o suficiente para implementar os marcos
M2 (memory mapper funcional) e servir de alicerce para M3 (XEX loader) e
M4 (framebuffer).

Nada aqui é sagrado. É uma proposta. Toda decisão precisa de justificativa
técnica e, quando relevante, de um ADR em docs/adr/.


================================================================================
1. FATOS SOBRE A MEMÓRIA DO XBOX 360
================================================================================

Fonte: documentação pública (free60 wiki, análises técnicas independentes,
manuais IBM do PPE). Nenhum documento proprietário da Microsoft é usado.

1.1 Memória principal
---------------------
- 512 MB GDDR3 unificada.
- Compartilhada entre CPU e GPU (sem barreira física de "VRAM" separada).
- Largura de banda nominal: ~22.4 GB/s (CPU) e ~22.4 GB/s (GPU externo,
  antes da eDRAM).
- A GPU tem 10 MB de eDRAM na daughter die, com 256 GB/s internos,
  usada como framebuffer e z-buffer.

1.2 Organização física
----------------------
- 512 MB de RAM contíguos, mapeados a partir do endereço físico 0x00000000.
- A GPU reserva parte da RAM para o framebuffer visível (não é a eDRAM;
  a eDRAM é memória da daughter die, separada dos 512 MB).
- O restante é usado por kernel, jogos, texturas, etc.

1.3 Endianness
--------------
- Xenon (PPC64) é BIG-ENDIAN.
- ARM64 é LITTLE-ENDIAN.
- Isso significa que TODO acesso a dados na memória emulada precisa de
  byteswap, exceto quando o dado é tratado como bytes crus (ex.: buffers
  de DMA e texturas).
- VMX tem regras ainda mais complexas de endianness para elementos
  (vector element swapping). Ver seção 9.

1.4 MMU do Xenon
----------------
- Xenon usa um MMU com suporte a:
  - Páginas de 4 KB, 64 KB, 1 MB e 16 MB.
  - Modo "real mode" (sem MMU) e "virtual mode".
  - Um "physical memory window" (janela direta) mapeada em virtual
    address.
- O kernel do 360 configura o MMU logo no boot.
- Para o emulador, o modelo recomendado é:
  - Fase 1 (MVP): apenas modo real + janela direta de RAM.
  - Fase 2: MMU completa com page tables (necessária para jogos comerciais).


================================================================================
2. MAPA DE ENDEREÇOS FÍSICOS (O QUE O EMULADOR PRECISA MODELAR)
================================================================================

Endereços em hexadecimal, formato de 32 bits (o Xenon usa 32 bits físicos;
o PPC64 permite 64 bits virtuais, mas o 360 é configurado com 32 bits
físicos).

Região                              | Faixa                  | Notas
------------------------------------|------------------------|---------------------
RAM principal (512 MB)              | 0x00000000-0x1FFFFFFF  | Leitura/escrita
Reservado / aliases                 | 0x20000000-0x7FBFFFFF  | Não mapeado
GPU registers (Xenos)               | 0x7FC80000-0x7FCFFFFF  | MMIO
GPU unknown (a validar)             | 0x7FD00000-0x7FDFFFFF  | MMIO (TBD)
Audio (XMA decoder)                 | 0x7FE00000-0x7FEFFFFF  | MMIO (TBD)
XAM / kernel MMIO (TBD)             | 0x7FF00000-0x7FFFFFFF  | MMIO (TBD)
PCIe / config space                 | 0xC0000000-0xCFFFFFFF  | MMIO (TBD)
Flash (NAND)                        | 0xE0000000-0xEFFFFFFF  | MMIO (TBD)
SRAM / periféricos (TBD)            | 0xF0000000-0xFFFFFFFF  | MMIO (TBD)

IMPORTANTE: os endereços exatos das regiões MMIO acima precisam ser
validados contra a documentação pública (free60 wiki, Xenia, análises
independentes) ANTES de serem implementados. O documento marca como
"(TBD)" tudo o que não deve ser considerado definitivo.

O Memory Manager deve ser DATA-DRIVEN: a tabela de regiões deve estar em
uma estrutura de configuração, não espalhada em if/else pelo código.


================================================================================
3. MAPA DE ENDEREÇOS VIRTUAIS (VISÃO GERAL)
================================================================================

O Xenon opera em dois "mundos" de endereçamento:

  - Kernel space: endereços virtuais altos (>= 0x80000000).
  - User space: endereços virtuais baixos (< 0x80000000), por processo.

Mapeamentos públicos conhecidos (sujeitos a validação):

Faixa virtual                          | Conteúdo
---------------------------------------|------------------------------------
0x00000000-0x3FFFFFFF                  | User space (jogos)
0x40000000-0x7FFFFFFF                  | User space (jogos)
0x80000000-0x8FFFFFFF                  | Kernel image (256 MB)
0x90000000-0x9FFFFFFF                  | Kernel data (256 MB)
0xA0000000-0xBFFFFFFF                  | Physical memory window #1 (512 MB)
0xC0000000-0xDFFFFFFF                  | Physical memory window #2 (alias)
0xE0000000-0xFFFFFFFF                  | Kernel MMIO e outros

Para o MVP (Fase 1), apenas a janela direta de RAM e MMIO importam:
- O boot stub mapeia a janela direta.
- O XEX loader mapeia o executável dentro do user space.
- MMIO é tratado em modo real (sem page table).

Para jogos comerciais (Fase 2), MMU completa é necessária. Fora do escopo
deste documento; será tratada em docs/MMU.md.


================================================================================
4. OBJETIVOS DE DESIGN
================================================================================

O1. Correção semântica
    Todo acesso a memória deve respeitar endianness, alinhamento e
    comportamento de MMIO.

O2. Performance
    O caminho rápido (leitura/escrita em RAM, sem MMIO, sem fault) deve
    ser virtualmente zero-overhead quando acessado pelo interpretador
    e pelo JIT.

O3. Modularidade
    O Memory Manager não deve conhecer GPU, CPU ou áudio. Ele apenas
    expõe interfaces que esses módulos consomem.

O4. Testabilidade
    Toda a semântica deve ser testável sem hardware real. Testes
    unitários cobrem RAM, MMIO e endianness isoladamente.

O5. Data-driven
    O mapa de endereços e os handlers MMIO são registrados em runtime,
    não hardcoded.

O6. Determinismo
    Para jogos que dependem de timing, o Memory Manager não pode
    introduzir variabilidade (ex.: sem alocação dinâmica no caminho
    quente, sem mutex desnecessário).


================================================================================
5. ARQUITETURA DE COMPONENTES
================================================================================

  +-------------------------------------------------------------+
  |                        CPU (PPC)                            |
  |  Loads/stores via MemoryManager::read_*/write_*             |
  +----------------------------+--------------------------------+
                               |
  +----------------------------v--------------------------------+
  |                     Bus (opcional)                          |
  |  Roteia acessos entre CPU, GPU, DMA                         |
  +----------------------------+--------------------------------+
                               |
  +----------------------------v--------------------------------+
  |                    MemoryManager                            |
  |                                                             |
  |  +-------------+  +-------------+  +------------------+     |
  |  | RAM Region  |  |  MMIO Map   |  | Virtual->Phys    |     |
  |  | (512 MB)    |  | (handlers)  |  | (page tables)    |     |
  |  +-------------+  +-------------+  +------------------+     |
  |                                                             |
  +-------------------------------------------------------------+

Principais classes:

  class MemoryManager
    - Dono do buffer de RAM (512 MB, alocado uma vez no boot).
    - Expõe read_u8/u16/u32/u64, write_u8/u16/u32/u64 em endereço FÍSICO.
    - Expõe read/write em endereço VIRTUAL (traduz para físico).
    - Mantém o mapa MMIO (endereço -> handler).
    - Expõe DMA read/write para GPU e periféricos.

  class MMIOHandler (interface)
    - read32(offset), write32(offset, value).
    - read64/write64 com default implementado sobre 32-bit.
    - Sem estado global; handlers são registrados por base.

  class AddressSpace (opcional, Fase 2)
    - Page table do processo atual.
    - Tradução virtual -> físico.
    - TLB (cache de tradução).

  class Bus (opcional)
    - Ponto único de acesso para CPU, GPU, DMA.
    - Permite injeção de trace, contabilização de ciclos, hooks.


================================================================================
6. API DO MEMORY MANAGER
================================================================================

A API é dividida em três camadas, do mais quente ao mais frio:

6.1 Camada física (quente — usada pelo JIT)
-------------------------------------------

  class MemoryManager {
  public:
    // ---- RAM fast path ------------------------------------------------
    // Retorna ponteiro direto para a RAM, ou nullptr se o endereço
    // físico não for RAM (ex.: MMIO). O chamador é responsável por
    // checar nullptr.
    uint8_t* ram_ptr(uint32_t phys_addr);

    // Verdadeiro se phys_addr está dentro da região de RAM principal.
    bool is_ram(uint32_t phys_addr) const;

    // ---- Acesso físico com bounds check + MMIO dispatch --------------
    // Endereços válidos: 0x00000000 - 0xFFFFFFFF.
    // Se o endereço cair em MMIO, o handler correspondente é invocado.
    // Se o endereço for inválido, comportamento é undefined (retorna 0
    // e loga em builds Debug).
    uint8_t  read_u8 (uint32_t phys_addr);
    uint16_t read_u16(uint32_t phys_addr);
    uint32_t read_u32(uint32_t phys_addr);
    uint64_t read_u64(uint32_t phys_addr);

    void write_u8 (uint32_t phys_addr, uint8_t  value);
    void write_u16(uint32_t phys_addr, uint16_t value);
    void write_u32(uint32_t phys_addr, uint32_t value);
    void write_u64(uint32_t phys_addr, uint64_t value);

    // ---- DMA (usado por GPU, XMA, disco) -----------------------------
    // Copia blocos sem byteswap, tratando o dado como bytes crus.
    // Retorna false se alguma região não for RAM.
    bool dma_read (void* dst_host, uint32_t src_phys, size_t size);
    bool dma_write(uint32_t dst_phys, const void* src_host, size_t size);

    // ---- Registro de MMIO --------------------------------------------
    void register_mmio(uint32_t base, uint32_t size,
                       std::unique_ptr<MMIOHandler> handler);

    // ---- Configuração -------------------------------------------------
    struct Config {
      uint32_t ram_size = 512 * 1024 * 1024;
      bool     enable_bounds_check = true;   // false em Release
      bool     enable_mmio_trace   = false;  // true só em debug
      bool     enable_ram_trace    = false;  // muito verboso
    };
    bool initialize(const Config& config);
  };


6.2 Camada virtual (morna — usada pelo interpretador e pelo kernel)
-------------------------------------------------------------------

  class AddressSpace {
  public:
    // Tradução virtual -> físico. Retorna UINT32_MAX se fault.
    uint32_t translate(uint32_t vaddr) const;

    // Acesso virtual (traduz + delega para MemoryManager).
    // Usado pelo interpretador quando em modo virtual.
    uint32_t read32(uint32_t vaddr);
    void     write32(uint32_t vaddr, uint32_t value);
    // ... (u8/u16/u64 análogos)

    // Instalação/remoção de mapeamentos (usado pelo kernel emulado).
    void map_page(uint32_t vaddr, uint32_t paddr, PageSize size,
                  bool writable, bool executable, bool cached);
    void unmap_page(uint32_t vaddr);
  };


6.3 Camada de depuração
-----------------------

  - Trace opcional de todos os acessos (para debug de jogos).
  - Log de MMIO não mapeado (com endereço e PC, quando disponível).
  - Contadores de acesso por região (para análise de performance).


================================================================================
7. MMIO — DESIGN
================================================================================

7.1 Modelo
----------

Cada região MMIO é registrada com:
  - Base (endereço físico de início).
  - Tamanho (em bytes; potência de 2 recomendada).
  - Handler (objeto que implementa MMIOHandler).

O MemoryManager mantém um mapa ordenado de regiões para resolução
O(log n). Para o caminho quente (RAM), o check `is_ram()` é O(1)
(apenas uma comparação de faixa).

7.2 Interface do handler

  class MMIOHandler {
  public:
    virtual ~MMIOHandler() = default;

    // Acesso 32-bit (é o caso mais comum em hardware do 360).
    virtual uint32_t read32(uint32_t offset) = 0;
    virtual void     write32(uint32_t offset, uint32_t value) = 0;

    // Acesso 8/16-bit: default delega para read32/write32 com shift.
    // Handlers podem sobrescrever se o hardware real não permitir
    // acessos menores.
    virtual uint8_t  read8(uint32_t offset);
    virtual uint16_t read16(uint32_t offset);
    virtual void     write8(uint32_t offset, uint8_t value);
    virtual void     write16(uint32_t offset, uint16_t value);

    // Acesso 64-bit: default é dois acessos 32-bit.
    virtual uint64_t read64(uint32_t offset);
    virtual void     write64(uint32_t offset, uint64_t value);
  };

7.3 Regras de comportamento

  - Acesso desalinhado (misaligned) em MMIO é permitido por padrão
    (o handler decide se rejeita ou não).
  - Leitura de MMIO pode ter side effects (ex.: FIFO). Por isso o
    interpretador NUNCA deve fazer read especulativo de MMIO.
  - Escrita em MMIO é sempre visível imediatamente (não há cache).
  - Um handler pode gerar interrupção na CPU (chamando uma callback
    fornecida pelo System). Isso será definido em docs/INTERRUPTS.md.

7.4 Handlers a implementar (ordem de prioridade)

  Prioridade | Handler            | Marco alvo
  -----------|--------------------|------------
  Alta       | GPU (Xenos)        | M4 / M5
  Alta       | Reset/clock stub   | M4
  Média      | XMA (áudio)        | M6
  Média      | PCIe config space  | M5
  Baixa      | NAND / flash       | Pós-M6


================================================================================
8. INTEGRAÇÃO COM A CPU (PPC)
================================================================================

8.1 Modo real (Fase 1)

  - Toda leitura/escrita do PPC é física.
  - `PPCInterpreter::execute_load(...)` chama `MemoryManager::read_*`.
  - `PPCInterpreter::execute_store(...)` chama `MemoryManager::write_*`.

8.2 Modo virtual (Fase 2)

  - Toda leitura/escrita do PPC é virtual.
  - `PPCInterpreter` chama `AddressSpace::read_*` que traduz e delega.
  - TLB: cache de tradução por (vaddr >> 12) -> paddr.
  - Fault de página deve gerar exceção PPC (DSI/ISI) tratada pelo
    interpretador.

8.3 Caminho quente (JIT)

  - O JIT, ao gerar código para uma load/store, deve:
    1. Checar `is_ram(addr)` inline (comparação com 0x1FFFFFFF).
    2. Se sim, `memcpy` byteswapped direto do `ram_ptr(addr)`.
    3. Se não, chamar `MemoryManager::read_*/write_*` (função C,
       não virtual, marcada `noinline` para evitar inlining do
       dispatcher MMIO dentro do bloco JIT).

  - Em ARM64, o byteswap tem instruções nativas:
    - `rev` (32-bit), `rev16`, `rev32`, `rev64`.
    - `LDREX`/`STREX` para atômicos.
    - Vamos usar isso intensivamente. Ver seção 9.

8.4 Endianness (detalhe)

  - Regra do projeto: a RAM emulada armazena os bytes exatamente como
    o jogo os escreveu (big-endian).
  - Ou seja, NÃO convertemos a RAM para little-endian na escrita.
  - Cada read/write faz byteswap no momento do acesso.
  - Para `dma_read`/`dma_write`, byteswap é omitido (dado cru).
  - Isso é mais lento que converter a RAM inteira, mas é mais correto
    para MMIO e para acesso de GPU.

  - Alternativa considerada (rejeitada): manter a RAM em big-endian
    e usar `__builtin_bswap` apenas quando o valor precisa ser
    interpretado (registradores). Isso evita byteswap em dobro,
    mas complica o código e quebra a regra "RAM = bytes do jogo".
    Mantemos a primeira regra.

8.5 Alinhamento

  - PPC64 permite acessos desalinhados em alguns modos; o 360 não
    garante alinhamento em todos os casos.
  - O interpretador deve reproduzir o comportamento real (exceção de
    alinhamento em `lwz` desalinhado, ou split em dois acessos,
    dependendo do modo).
  - Para o MVP, permitimos desalinhamento e implementamos split.
    Ajustamos se jogos reais exigirem exceção.


================================================================================
9. ENDIANNESS — CASOS ESPECIAIS
================================================================================

9.1 Escalar (GPR, FPR)
-----------------------
  read_u32(phys) -> byteswap16x2 no retorno (ARM64: `rev`).
  write_u32(phys, val) -> byteswap antes de escrever.
  Inline no caminho quente.

9.2 VMX / AltiVec (128-bit)
----------------------------
  VMX tem endianness de ELEMENTO, não só de palavra. O layout dos
  16 bytes é big-endian para cada elemento (byte, halfword, word,
  doubleword), mas o vetor inteiro pode ser reinterpretado.

  Regra: armazenamos os 16 bytes como vieram. Cada instrução VMX
  interpreta os bytes de acordo com a operação:
    - vaddubm: soma bytes individualmente (sem swap).
    - vadduwm: soma words (swap 4 bytes por word).
    - vaddfp: soma floats (swap 4 bytes por float).

  Consequência: VMX no emulador é INTENSIVO em byteswap, e a
  implementação precisa ser cuidadosa para não criar bugs silenciosos.
  Detalhes em docs/PPC_COVERAGE.md (seção VMX).

9.3 Texturas (GPU)
-------------------
  Texturas têm seus próprios formatos (DXT, endianness específicos).
  GPU lê textura como bytes crus + interpreta pelo formato. NÃO
  byteswap no Memory Manager. A camada Graphics cuida disso.

9.4 DMA
--------
  DMA é sempre bytes crus. O Memory Manager não altera bytes em
  dma_read/dma_write.


================================================================================
10. INTEGRAÇÃO COM O BUS
================================================================================

O Bus é opcional no MVP, mas recomendado para:

  - Ponto único de acesso quando CPU e GPU competem por RAM.
  - Injeção de trace e contadores de performance.
  - Modelagem de ciclos (CPU espera GPU terminar escrita em MMIO).
  - Coerência de cache futura.

Para o MVP, a CPU fala direto com o Memory Manager. Quando o Bus
existir (Fase B, M5), a API do Memory Manager não muda — o Bus
apenas chama as mesmas funções do Memory Manager, adicionando
instrumentação.


================================================================================
11. PERFORMANCE — NÚMEROS-ALVO
================================================================================

Cenário                              | Alvo (Release, ARM64)
-------------------------------------|-------------------------
read_u32 em RAM (com byteswap)       | < 5 ns (~15 ciclos)
write_u32 em RAM (com byteswap)      | < 5 ns
read_u32 em MMIO                     | < 50 ns (~150 ciclos)
dma_write 4 KB                       | < 2 us
ram_ptr()                            | < 1 ns (inline)

Notas:
- "Ciclos" referem-se a ciclos ARM64 no device de referência
  (Snapdragon 8 Gen 2, prime core ~3.2 GHz).
- Em modo Debug com bounds check e trace, os números são
  10-100x piores. Isso é aceitável.

O caminho quente deve ser medido com benchmarks na pasta Tests/Bench/
e os resultados publicados em docs/BENCHMARKS.md.


================================================================================
12. TESTES
================================================================================

12.1 Testes unitários (obrigatórios)
------------------------------------

Tests/Memory/:
  - test_ram_read_write.cpp
      Escrita e leitura em cada largura (u8/u16/u32/u64).
      Boundary: endereço 0, 0x1FFFFFFF, 0x20000000 (fora da RAM).
  - test_ram_endianness.cpp
      Escreve bytes conhecidos, lê como u32, verifica valor.
      Ex.: write_u8(0, 0x12), write_u8(1, 0x34), write_u8(2, 0x56),
           write_u8(3, 0x78), read_u32(0) == 0x12345678.
  - test_mmio_dispatch.cpp
      Registra handler fake, lê/escreve, verifica que handler foi
      chamado com offset correto.
  - test_mmio_overlap.cpp
      Registro de duas regiões sobrepostas deve falhar (ou ter
      comportamento definido).
  - test_dma.cpp
      dma_write de buffer conhecido para RAM, verifica bytes.
  - test_bounds_check.cpp
      Acesso fora da RAM + fora de MMIO retorna 0 e loga (em Debug).

12.2 Testes de integração
-------------------------

Tests/Integration/:
  - test_cpu_load_store.cpp
      Interpretador executa lwz/stw em RAM, resultado correto.
  - test_cpu_mmio_stub.cpp
      Interpretador executa lwz em MMIO com handler fake.

12.3 Benchmarks
---------------

Tests/Bench/:
  - bench_ram_read.cpp
  - bench_ram_write.cpp
  - bench_mmio_read.cpp
  - bench_dma.cpp

Publicar resultados em docs/BENCHMARKS.md a cada release.


================================================================================
13. ARMADILHAS CONHECIDAS
================================================================================

A1. Endianness em dobro
    Se a RAM for armazenada em big-endian (bytes crus) e o código
    fizer byteswap em read E em write, o valor sai correto. Se em
    algum lugar o código esquecer o byteswap, o bug é silencioso e
    difícil de rastrear. Testar endianness SEMPRE.

A2. Misaligned MMIO
    Alguns handlers reais não permitem acesso desalinhado (ex.:
    FIFO de 32 bits). O default do projeto PERMITE desalinhamento;
    o handler rejeita explicitamente se for o caso. Não confundir.

A3. MMIO com side effect em read
    Nunca fazer leitura especulativa. O interpretador NUNCA deve
    "espiar" MMIO para otimizar (ex.: branch prediction com read).
    Isso pode alterar estado do hardware emulado.

A4. Cache de RAM em MMIO
    Não cachear resultado de read em MMIO. Sempre chamar o handler.

A5. Concorrência
    No MVP, single-thread é assumido. Quando o JIT e a GPU emulada
    rodarem em threads separadas, será necessário modelar coerência
    (ou serializar via Bus). Fora do escopo deste documento.

A6. Páginas de 4 KB vs 64 KB
    Xenon suporta ambos. ARM64 usa 4 KB, 16 KB ou 64 KB. Se a MMU
    for implementada em Fase 2, escolher o tamanho de página ARM64
    que minimize TLB misses para o padrão de acesso do jogo. Não é
    problema agora.

A7. RAM "reservada" pela GPU
    Parte dos 512 MB é usada como framebuffer visível. O jogo
    escreve nela via GPU, não via CPU. O Memory Manager NÃO deve
    bloquear acesso — apenas refletir o que está lá.


================================================================================
14. ROADMAP DE IMPLEMENTAÇÃO
================================================================================

Fase 1 (MVP — destrava M2, M4)
-------------------------------
  [ ] MemoryManager::initialize() com buffer de 512 MB.
  [ ] read/write em RAM com byteswap.
  [ ] is_ram() e ram_ptr().
  [ ] Registro de MMIO com handler stub (read 0 / write nop).
  [ ] Testes unitários de RAM e endianness.
  [ ] Integração com PPCInterpreter (loads/stores básicos).

Fase 2 (destrava M5)
---------------------
  [ ] Handlers MMIO reais para GPU (registradores que jogos consultam).
  [ ] dma_read/dma_write.
  [ ] Handlers de reset/clock stub.
  [ ] Benchmarks publicados.

Fase 3 (destrava M6 e MMU)
---------------------------
  [ ] AddressSpace com page tables.
  [ ] Tradução virtual -> físico com TLB.
  [ ] Faults (DSI/ISI).
  [ ] Integração com kernel stub.

Fase 4 (futuro)
----------------
  [ ] Coerência de cache com GPU em thread separada.
  [ ] Otimizações de byteswap com NEON.
  [ ] Perfis de acesso para jogos específicos.


================================================================================
15. QUESTÕES ABERTAS (PARA DISCUSSÃO)
================================================================================

Q1. O buffer de 512 MB deve ser alocado com `mmap` (para permitir
    mprotect em page tables futuras) ou `malloc`?
    - Pró: `mmap` prepara para MMU.
    - Contra: `malloc` é portável e mais simples.

Q2. Como o kernel emulado acessa MMIO? Via MemoryManager (mesma API)
    ou via atalho (API separada)?
    - Pró atalho: mais rápido, kernel "confia" no hardware.
    - Contra atalho: quebra a regra "tudo passa pelo MemoryManager".

Q3. Endianness de MMIO: alguns registradores são little-endian (PCIe
    config space usa little-endian). Como modelar isso sem confundir
    com o big-endian do PPC?
    - Opção A: handlers declaram sua endianness.
    - Opção B: MemoryManager sempre big-endian; handlers convertem.
    - Recomendação: Opção B (mais simples, mais explícita).

Q4. AddressSpace: 1 objeto global (um processo) ou por-processo?
    O 360 só roda um jogo por vez, mas o kernel tem seu próprio
    espaço. Modelar separadamente ou unificado?
    - Recomendação: unificado no MVP, separar em Fase 3 se necessário.

Q5. TLB: implementar ou começar com tradução direta por page table?
    - Recomendação: tradução direta no MVP, TLB só quando
      profiling mostrar gargalo.

Q6. Misaligned RAM access: exceção ou split?
    - Recomendação: split no MVP. Revisitar se jogos exigirem
      exceção (improvável).


================================================================================
16. REFERÊNCIAS
================================================================================

- free60 wiki (arquitetura do Xbox 360, mapa de memória).
- IBM Xenon PPE manual (MMU, endianness, VMX).
- IBM PowerPC 2.02 ISA (semântica de load/store).
- Documentação pública do Xenos (patentes, whitepapers).
- Projetos de referência (apenas leitura, sem código): Xenia (mapa de
  memória, comportamento de MMIO), RPCS3 (PS3 usa PPE também, útil
  como referência de design de Memory Manager), Dolphin (GameCube,
  reference de design para endianness).

Nenhum código de outro emulador é usado ou copiado. A referência é
apenas arquitetural e pública.


================================================================================
17. PRÓXIMOS DOCUMENTOS
================================================================================

Após aprovação deste:

  - docs/XEX_LOADER.md — formato XEX, LZX, imports, mapeamento.
  - docs/MMU.md — page tables, TLB, faults.
  - docs/INTERRUPTS.md — como MMIO gera interrupção na CPU.
  - docs/adr/0001-ram-allocation.md — decisão de alocação.
  - docs/adr/0002-endianness-policy.md — decisão de byteswap.

================================================================================
FIM DO DOCUMENTO
================================================================================