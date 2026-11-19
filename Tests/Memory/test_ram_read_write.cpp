#include <gtest/gtest.h>

#include <cstring>

#include "MemoryManager.hpp"

using namespace xboxemu::memory;

class RamTest : public ::testing::Test {
protected:
    void SetUp() override { ASSERT_TRUE(mm_.initialize()); }
    MemoryManager mm_;
};

TEST_F(RamTest, RamSizeIs512MB) {
    EXPECT_EQ(mm_.ram_size(), 512u * 1024u * 1024u);
}

TEST_F(RamTest, ZeroedAfterInit) {
    EXPECT_EQ(mm_.read_u32(0u),          0u);
    EXPECT_EQ(mm_.read_u32(0x1000u),     0u);
    EXPECT_EQ(mm_.read_u64(0x1FFFF000u), 0ull);
}

TEST_F(RamTest, WriteReadU8) {
    mm_.write_u8(0x100u, 0xAB);
    EXPECT_EQ(mm_.read_u8(0x100u), 0xAB);
}

TEST_F(RamTest, WriteReadU16) {
    mm_.write_u16(0x200u, 0xBEEFu);
    EXPECT_EQ(mm_.read_u16(0x200u), 0xBEEFu);
}

TEST_F(RamTest, WriteReadU32) {
    mm_.write_u32(0x300u, 0xDEADBEEFu);
    EXPECT_EQ(mm_.read_u32(0x300u), 0xDEADBEEFu);
}

TEST_F(RamTest, WriteReadU64) {
    mm_.write_u64(0x400u, 0xDEADBEEFCAFEBABEull);
    EXPECT_EQ(mm_.read_u64(0x400u), 0xDEADBEEFCAFEBABEull);
}

TEST_F(RamTest, BoundaryLastValidByte) {
    mm_.write_u8(0x1FFFFFFFu, 0x42);
    EXPECT_EQ(mm_.read_u8(0x1FFFFFFFu), 0x42);
}

TEST_F(RamTest, OutOfRamWithoutMmioReturnsZero) {
    EXPECT_EQ(mm_.read_u8(0x20000000u),  0u);
    EXPECT_EQ(mm_.read_u32(0x30000000u), 0u);
    EXPECT_EQ(mm_.read_u64(0x40000000u), 0ull);
}

TEST_F(RamTest, RamPtrValidAndNull) {
    EXPECT_NE(mm_.ram_ptr(0u),          nullptr);
    EXPECT_NE(mm_.ram_ptr(0x1FFFFFFFu), nullptr);
    EXPECT_EQ(mm_.ram_ptr(0x20000000u), nullptr);
}

TEST_F(RamTest, DmaWriteReadRoundtrip) {
    const uint8_t buf[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    ASSERT_TRUE(mm_.dma_write(0x1000u, buf, sizeof(buf)));

    uint8_t out[16] = {};
    ASSERT_TRUE(mm_.dma_read(out, 0x1000u, sizeof(out)));
    EXPECT_EQ(std::memcmp(buf, out, sizeof(buf)), 0);
}

TEST_F(RamTest, DmaOutOfRangeFails) {
    uint8_t buf[8] = {};
    EXPECT_FALSE(mm_.dma_read(buf, 0x1FFFFFFCu, 8u));  // passa do fim
}
