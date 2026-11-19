// Xbox Emulator — Memory Manager
//
// Implementação da Fase 1 do design em docs/MEMORY.md.
// - RAM principal (512 MB por padrão) com semântica big-endian.
// - Dispatch MMIO via handlers registrados em runtime.
// - DMA cru (sem byteswap).
//
// A RAM armazena os bytes EXATAMENTE como o jogo os escreveu (big-endian).
// Cada acesso read_*/write_* faz byteswap quando o host é little-endian.
// Acesso via dma_read/dma_write NÃO faz byteswap (dado cru).

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace xboxemu::memory {

// Detecta endianness do host em tempo de compilação.
constexpr bool host_is_little_endian() {
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
    return false;
#else
    return true;
#endif
}

// -----------------------------------------------------------------------------
// MMIOHandler — interface para qualquer região MMIO.
// -----------------------------------------------------------------------------
class MMIOHandler {
public:
    virtual ~MMIOHandler() = default;

    // Acesso 32-bit (caso mais comum em hardware do 360).
    virtual uint32_t read32(uint32_t offset) = 0;
    virtual void     write32(uint32_t offset, uint32_t value) = 0;

    // Acesso 8/16-bit: default delega para read32/write32 com shift.
    // Handlers podem sobrescrever se o hardware real não permitir.
    virtual uint8_t  read8(uint32_t offset);
    virtual uint16_t read16(uint32_t offset);
    virtual void     write8(uint32_t offset, uint8_t value);
    virtual void     write16(uint32_t offset, uint16_t value);

    // Acesso 64-bit: default é dois acessos 32-bit (high, low).
    virtual uint64_t read64(uint32_t offset);
    virtual void     write64(uint32_t offset, uint64_t value);
};

// -----------------------------------------------------------------------------
// MemoryManager — dono da RAM e dispatcher de MMIO.
// -----------------------------------------------------------------------------
class MemoryManager {
public:
    struct Config {
        uint32_t ram_size = 512u * 1024u * 1024u;
        bool enable_bounds_check = true;   // false em Release futuramente
        bool enable_mmio_trace   = false;
        bool enable_ram_trace    = false;
    };

    MemoryManager();
    ~MemoryManager();

    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;

    // Aloca a RAM e zera. Retorna false se ram_size == 0 ou alloc falhar.
    bool initialize(const Config& config = {});
    void shutdown();

    // ---- Introspecção ------------------------------------------------------
    uint32_t ram_size() const;
    bool     is_ram(uint32_t phys) const;
    uint8_t* ram_ptr(uint32_t phys);
    const uint8_t* ram_ptr(uint32_t phys) const;

    // ---- Acesso físico (RAM + MMIO) ---------------------------------------
    // Se phys cair em RAM, lê/escreve com byteswap.
    // Se phys cair em MMIO registrado, delega ao handler.
    // Caso contrário, retorna 0 (leitura) ou ignora (escrita).
    uint8_t  read_u8 (uint32_t phys);
    uint16_t read_u16(uint32_t phys);
    uint32_t read_u32(uint32_t phys);
    uint64_t read_u64(uint32_t phys);

    void write_u8 (uint32_t phys, uint8_t  value);
    void write_u16(uint32_t phys, uint16_t value);
    void write_u32(uint32_t phys, uint32_t value);
    void write_u64(uint32_t phys, uint64_t value);

    // ---- DMA (bytes crus, sem byteswap) -----------------------------------
    // Usado por GPU, XMA e disco. Retorna false se região não for RAM.
    bool dma_read (void* dst_host, uint32_t src_phys, size_t size) const;
    bool dma_write(uint32_t dst_phys, const void* src_host, size_t size);

    // ---- Registro de MMIO --------------------------------------------------
    // Rejeita (silenciosamente) se houver overlap com região já registrada.
    void register_mmio(uint32_t base, uint32_t size,
                       std::unique_ptr<MMIOHandler> handler);
    void clear_mmio();

private:
    struct MmioRegion {
        uint32_t base;
        uint32_t size;
        std::unique_ptr<MMIOHandler> handler;
    };

    MMIOHandler* find_mmio(uint32_t phys, uint32_t size, uint32_t& offset_out);

    Config config_{};
    std::unique_ptr<uint8_t[]> ram_;
    uint32_t ram_size_ = 0;
    std::vector<MmioRegion> mmio_regions_;
};

} // namespace xboxemu::memory
