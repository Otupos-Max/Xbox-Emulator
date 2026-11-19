#include "MemoryManager.hpp"

#include <cstring>

namespace xboxemu::memory {

namespace detail {

template <typename T>
inline T bswap(T v);

template <> inline uint16_t bswap<uint16_t>(uint16_t v) { return __builtin_bswap16(v); }
template <> inline uint32_t bswap<uint32_t>(uint32_t v) { return __builtin_bswap32(v); }
template <> inline uint64_t bswap<uint64_t>(uint64_t v) { return __builtin_bswap64(v); }

// Lê sizeof(T) bytes como valor big-endian (independente do host).
template <typename T>
inline T read_be(const uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    if constexpr (host_is_little_endian()) {
        v = bswap<T>(v);
    }
    return v;
}

// Escreve value em sizeof(T) bytes como big-endian.
template <typename T>
inline void write_be(uint8_t* p, T value) {
    if constexpr (host_is_little_endian()) {
        value = bswap<T>(value);
    }
    std::memcpy(p, &value, sizeof(T));
}

inline bool in_ram_range(uint32_t phys, size_t size, uint32_t ram_size) {
    return static_cast<uint64_t>(phys) + size <= ram_size;
}

} // namespace detail

// =============================================================================
// MMIOHandler — defaults
// =============================================================================
uint8_t MMIOHandler::read8(uint32_t offset) {
    const uint32_t aligned = offset & ~3u;
    const unsigned shift   = (offset & 3u) * 8u;
    return static_cast<uint8_t>(read32(aligned) >> shift);
}

uint16_t MMIOHandler::read16(uint32_t offset) {
    const uint32_t aligned = offset & ~3u;
    const unsigned shift   = (offset & 3u) * 8u;
    return static_cast<uint16_t>(read32(aligned) >> shift);
}

void MMIOHandler::write8(uint32_t offset, uint8_t value) {
    const uint32_t aligned = offset & ~3u;
    const unsigned shift   = (offset & 3u) * 8u;
    uint32_t v = read32(aligned);
    v = (v & ~(0xFFu << shift)) | (static_cast<uint32_t>(value) << shift);
    write32(aligned, v);
}

void MMIOHandler::write16(uint32_t offset, uint16_t value) {
    const uint32_t aligned = offset & ~3u;
    const unsigned shift   = (offset & 3u) * 8u;
    uint32_t v = read32(aligned);
    v = (v & ~(0xFFFFu << shift)) | (static_cast<uint32_t>(value) << shift);
    write32(aligned, v);
}

uint64_t MMIOHandler::read64(uint32_t offset) {
    const uint64_t hi = read32(offset);
    const uint64_t lo = read32(offset + 4);
    return (hi << 32) | lo;
}

void MMIOHandler::write64(uint32_t offset, uint64_t value) {
    write32(offset,     static_cast<uint32_t>(value >> 32));
    write32(offset + 4, static_cast<uint32_t>(value & 0xFFFFFFFFu));
}

// =============================================================================
// MemoryManager
// =============================================================================
MemoryManager::MemoryManager() = default;
MemoryManager::~MemoryManager() = default;

bool MemoryManager::initialize() {
    return initialize(Config());
}

bool MemoryManager::initialize(const Config& config) {
    config_   = config;
    ram_size_ = config.ram_size;
    if (ram_size_ == 0) return false;

    ram_.reset(new uint8_t[ram_size_]);
    if (!ram_) return false;

    std::memset(ram_.get(), 0, ram_size_);
    return true;
}

void MemoryManager::shutdown() {
    ram_.reset();
    ram_size_ = 0;
    mmio_regions_.clear();
}

uint32_t MemoryManager::ram_size() const { return ram_size_; }

bool MemoryManager::is_ram(uint32_t phys) const { return phys < ram_size_; }

uint8_t* MemoryManager::ram_ptr(uint32_t phys) {
    return is_ram(phys) ? ram_.get() + phys : nullptr;
}

const uint8_t* MemoryManager::ram_ptr(uint32_t phys) const {
    return is_ram(phys) ? ram_.get() + phys : nullptr;
}

// ---- Leitura ----------------------------------------------------------------
uint8_t MemoryManager::read_u8(uint32_t phys) {
    if (detail::in_ram_range(phys, 1, ram_size_)) return ram_[phys];
    uint32_t off = 0;
    if (auto* h = find_mmio(phys, 1, off)) return h->read8(off);
    return 0;
}

uint16_t MemoryManager::read_u16(uint32_t phys) {
    if (detail::in_ram_range(phys, 2, ram_size_)) {
        return detail::read_be<uint16_t>(ram_.get() + phys);
    }
    uint32_t off = 0;
    if (auto* h = find_mmio(phys, 2, off)) return h->read16(off);
    return 0;
}

uint32_t MemoryManager::read_u32(uint32_t phys) {
    if (detail::in_ram_range(phys, 4, ram_size_)) {
        return detail::read_be<uint32_t>(ram_.get() + phys);
    }
    uint32_t off = 0;
    if (auto* h = find_mmio(phys, 4, off)) return h->read32(off);
    return 0;
}

uint64_t MemoryManager::read_u64(uint32_t phys) {
    if (detail::in_ram_range(phys, 8, ram_size_)) {
        return detail::read_be<uint64_t>(ram_.get() + phys);
    }
    uint32_t off = 0;
    if (auto* h = find_mmio(phys, 8, off)) return h->read64(off);
    // Boundary RAM/MMIO — split em dois u32.
    const uint64_t hi = read_u32(phys);
    const uint64_t lo = read_u32(phys + 4);
    return (hi << 32) | lo;
}

// ---- Escrita ----------------------------------------------------------------
void MemoryManager::write_u8(uint32_t phys, uint8_t value) {
    if (detail::in_ram_range(phys, 1, ram_size_)) { ram_[phys] = value; return; }
    uint32_t off = 0;
    if (auto* h = find_mmio(phys, 1, off)) h->write8(off, value);
}

void MemoryManager::write_u16(uint32_t phys, uint16_t value) {
    if (detail::in_ram_range(phys, 2, ram_size_)) {
        detail::write_be<uint16_t>(ram_.get() + phys, value);
        return;
    }
    uint32_t off = 0;
    if (auto* h = find_mmio(phys, 2, off)) h->write16(off, value);
}

void MemoryManager::write_u32(uint32_t phys, uint32_t value) {
    if (detail::in_ram_range(phys, 4, ram_size_)) {
        detail::write_be<uint32_t>(ram_.get() + phys, value);
        return;
    }
    uint32_t off = 0;
    if (auto* h = find_mmio(phys, 4, off)) h->write32(off, value);
}

void MemoryManager::write_u64(uint32_t phys, uint64_t value) {
    if (detail::in_ram_range(phys, 8, ram_size_)) {
        detail::write_be<uint64_t>(ram_.get() + phys, value);
        return;
    }
    uint32_t off = 0;
    if (auto* h = find_mmio(phys, 8, off)) { h->write64(off, value); return; }
    write_u32(phys,     static_cast<uint32_t>(value >> 32));
    write_u32(phys + 4, static_cast<uint32_t>(value & 0xFFFFFFFFu));
}

// ---- DMA --------------------------------------------------------------------
bool MemoryManager::dma_read(void* dst_host, uint32_t src_phys, size_t size) const {
    if (!detail::in_ram_range(src_phys, size, ram_size_)) return false;
    std::memcpy(dst_host, ram_.get() + src_phys, size);
    return true;
}

bool MemoryManager::dma_write(uint32_t dst_phys, const void* src_host, size_t size) {
    if (!detail::in_ram_range(dst_phys, size, ram_size_)) return false;
    std::memcpy(ram_.get() + dst_phys, src_host, size);
    return true;
}

// ---- MMIO -------------------------------------------------------------------
void MemoryManager::register_mmio(uint32_t base, uint32_t size,
                                  std::unique_ptr<MMIOHandler> handler) {
    if (!handler || size == 0) return;

    // Overlap check
    for (const auto& r : mmio_regions_) {
        if (base < r.base + r.size && r.base < base + size) return;
    }

    mmio_regions_.push_back(MmioRegion{base, size, std::move(handler)});
}

void MemoryManager::clear_mmio() {
    mmio_regions_.clear();
}

MMIOHandler* MemoryManager::find_mmio(uint32_t phys, uint32_t size,
                                      uint32_t& offset_out) {
    for (auto& r : mmio_regions_) {
        if (phys >= r.base &&
            static_cast<uint64_t>(phys) + size <= static_cast<uint64_t>(r.base) + r.size) {
            offset_out = phys - r.base;
            return r.handler.get();
        }
    }
    return nullptr;
}

} // namespace xboxemu::memory
