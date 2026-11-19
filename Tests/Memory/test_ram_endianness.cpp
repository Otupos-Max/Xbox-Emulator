#include <gtest/gtest.h>

#include "MemoryManager.hpp"

using namespace xboxemu::memory;

class EndiannessTest : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(mm_.initialize()); }
    MemoryManager mm_;
};

TEST_F(EndiannessTest, BytesStoredInWrittenOrder) {
    mm_.write_u8(0u, 0x12);
    mm_.write_u8(1u, 0x34);
    mm_.write_u8(2u, 0x56);
    mm_.write_u8(3u, 0x78);

    // Buffer físico tem os bytes na ordem escrita.
    EXPECT_EQ(mm_.ram_ptr(0u)[0], 0x12);
    EXPECT_EQ(mm_.ram_ptr(0u)[1], 0x34);
    EXPECT_EQ(mm_.ram_ptr(0u)[2], 0x56);
    EXPECT_EQ(mm_.ram_ptr(0u)[3], 0x78);

    // Leitura como u32 interpreta big-endian.
    EXPECT_EQ(mm_.read_u32(0u), 0x12345678u);
}

TEST_F(EndiannessTest, WriteU32ProducesBigEndianBytes) {
    mm_.write_u32(0u, 0x11223344u);

    EXPECT_EQ(mm_.ram_ptr(0u)[0], 0x11);
    EXPECT_EQ(mm_.ram_ptr(0u)[1], 0x22);
    EXPECT_EQ(mm_.ram_ptr(0u)[2], 0x33);
    EXPECT_EQ(mm_.ram_ptr(0u)[3], 0x44);
}

TEST_F(EndiannessTest, U64ByteOrder) {
    mm_.write_u64(0u, 0x0102030405060708ull);

    EXPECT_EQ(mm_.ram_ptr(0u)[0], 0x01);
    EXPECT_EQ(mm_.ram_ptr(0u)[7], 0x08);
    EXPECT_EQ(mm_.read_u64(0u), 0x0102030405060708ull);
}

TEST_F(EndiannessTest, DmaIsRawNoByteSwap) {
    const uint8_t raw[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    ASSERT_TRUE(mm_.dma_write(0x10u, raw, 4u));

    // DMA preserva os bytes exatamente como vieram.
    EXPECT_EQ(mm_.ram_ptr(0x10u)[0], 0xAA);
    EXPECT_EQ(mm_.ram_ptr(0x10u)[3], 0xDD);

    // Leitura como u32 (RAM) interpreta big-endian.
    EXPECT_EQ(mm_.read_u32(0x10u), 0xAABBCCDDu);
}

TEST_F(EndiannessTest, U16ByteOrder) {
    mm_.write_u16(0x20u, 0xBEEFu);
    EXPECT_EQ(mm_.ram_ptr(0x20u)[0], 0xBE);
    EXPECT_EQ(mm_.ram_ptr(0x20u)[1], 0xEF);
    EXPECT_EQ(mm_.read_u16(0x20u), 0xBEEFu);
}
