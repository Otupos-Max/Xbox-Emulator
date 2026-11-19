#include <gtest/gtest.h>

#include <cstdint>
#include <memory>

#include "MemoryManager.hpp"

using namespace xboxemu::memory;

namespace {

class FakeMMIO : public MMIOHandler {
public:
    uint32_t value = 0xCAFEBABEu;
    uint32_t last_read_offset  = 0xFFFFFFFFu;
    uint32_t last_write_offset = 0xFFFFFFFFu;
    uint32_t last_write_value  = 0xFFFFFFFFu;
    int reads  = 0;
    int writes = 0;

    uint32_t read32(uint32_t offset) override {
        last_read_offset = offset;
        ++reads;
        return value;
    }

    void write32(uint32_t offset, uint32_t v) override {
        last_write_offset = offset;
        last_write_value  = v;
        ++writes;
    }
};

} // namespace

class MmioTest : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(mm_.initialize()); }
    MemoryManager mm_;
};

TEST_F(MmioTest, ReadDelegatesWithCorrectOffset) {
    auto fake = std::make_unique<FakeMMIO>();
    FakeMMIO* raw = fake.get();
    mm_.register_mmio(0x7FC80000u, 0x1000u, std::move(fake));

    EXPECT_EQ(mm_.read_u32(0x7FC80000u), 0xCAFEBABEu);
    EXPECT_EQ(raw->reads, 1);
    EXPECT_EQ(raw->last_read_offset, 0u);

    mm_.read_u32(0x7FC80020u);
    EXPECT_EQ(raw->last_read_offset, 0x20u);
    EXPECT_EQ(raw->reads, 2);
}

TEST_F(MmioTest, WriteDelegatesWithCorrectOffsetAndValue) {
    auto fake = std::make_unique<FakeMMIO>();
    FakeMMIO* raw = fake.get();
    mm_.register_mmio(0x7FC80000u, 0x1000u, std::move(fake));

    mm_.write_u32(0x7FC80010u, 0xDEADBEEFu);

    EXPECT_EQ(raw->writes, 1);
    EXPECT_EQ(raw->last_write_offset, 0x10u);
    EXPECT_EQ(raw->last_write_value, 0xDEADBEEFu);
}

TEST_F(MmioTest, UnmappedRegionReturnsZero) {
    // Sem MMIO registrado, leitura em região não-RAM retorna 0.
    EXPECT_EQ(mm_.read_u32(0x7FC80000u), 0u);
}

TEST_F(MmioTest, AccessBeyondRegionFallsThrough) {
    auto fake = std::make_unique<FakeMMIO>();
    FakeMMIO* raw = fake.get();
    mm_.register_mmio(0x7FC80000u, 0x10u, std::move(fake));

    // 0x10 bytes de range. Acesso em 0x7FC80020 (32 bytes à frente) cai fora.
    mm_.read_u32(0x7FC80020u);
    EXPECT_EQ(raw->reads, 0);
}

TEST_F(MmioTest, OverlapRejected) {
    auto a = std::make_unique<FakeMMIO>();
    FakeMMIO* raw_a = a.get();
    mm_.register_mmio(0x7FC80000u, 0x100u, std::move(a));

    auto b = std::make_unique<FakeMMIO>();
    FakeMMIO* raw_b = b.get();
    // Sobreposto: [0x7FC80080, 0x7FC80100) intersecta a região A.
    mm_.register_mmio(0x7FC80080u, 0x100u, std::move(b));

    // Acesso em 0x7FC80080 deve ir para o handler A (B foi rejeitado).
    mm_.read_u32(0x7FC80080u);
    EXPECT_EQ(raw_a->reads, 1);
    EXPECT_EQ(raw_b->reads, 0);
}

TEST_F(MmioTest, ClearMmioRemovesHandlers) {
    auto fake = std::make_unique<FakeMMIO>();
    FakeMMIO* raw = fake.get();
    mm_.register_mmio(0x7FC80000u, 0x100u, std::move(fake));

    mm_.clear_mmio();
    mm_.read_u32(0x7FC80000u);
    EXPECT_EQ(raw->reads, 0);
}
