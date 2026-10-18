#include <gtest/gtest.h>

#include "utils/memory_pool.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

namespace {

using Byte = std::byte;

} // namespace

// ---------------------------------------------------------------------------
// Pool<N>::Config
// ---------------------------------------------------------------------------

TEST(MemoryPoolTest, DefaultConfigIsZeroInitialized) {
    vnic::memory::Pool<64>::Config config;

    EXPECT_EQ(config.blockSize, 0u);
    EXPECT_EQ(config.capacity, 0u);
    EXPECT_EQ(config.socketId, 0u);
}

TEST(MemoryPoolTest, ConfigureReturnsTrueAndAcceptsReconfiguration) {
    vnic::memory::Pool<64> pool;
    vnic::memory::Pool<64>::Config config;
    config.blockSize = 64;
    config.capacity  = 8;
    config.socketId  = 1;

    EXPECT_TRUE(pool.configure(config));
    EXPECT_TRUE(pool.configure(config)); // idempotent / reconfigurable

    config.capacity = 16;
    EXPECT_TRUE(pool.configure(config));
}

TEST(MemoryPoolTest, UsageWithoutConfigureStillAllocates) {
    vnic::memory::Pool<16> pool; // default-constructed, no configure() call

    void* ptr = pool.allocate();
    ASSERT_NE(ptr, nullptr);
    pool.deallocate(ptr);
}

// ---------------------------------------------------------------------------
// allocate
// ---------------------------------------------------------------------------

TEST(MemoryPoolTest, AllocateReturnsNonNull) {
    vnic::memory::Pool<32> pool;

    void* ptr = pool.allocate();

    ASSERT_NE(ptr, nullptr);
    pool.deallocate(ptr);
}

TEST(MemoryPoolTest, AllocateReturnsDistinctBlocks) {
    vnic::memory::Pool<32> pool;

    void* first  = pool.allocate();
    void* second = pool.allocate();

    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first, second);

    pool.deallocate(first);
    pool.deallocate(second);
}

TEST(MemoryPoolTest, AllocateReturnsMaxAlignedBlock) {
    vnic::memory::Pool<64> pool;

    void* ptr = pool.allocate();
    ASSERT_NE(ptr, nullptr);

    const auto address = reinterpret_cast<std::uintptr_t>(ptr);
    EXPECT_EQ(address % alignof(std::max_align_t), 0u);

    pool.deallocate(ptr);
}

TEST(MemoryPoolTest, AllocateProvidesAtLeastNUsableBytes) {
    constexpr std::size_t kBlockSize = 48;
    vnic::memory::Pool<kBlockSize> pool;

    auto* ptr = static_cast<Byte*>(pool.allocate());
    ASSERT_NE(ptr, nullptr);

    // The whole block must be writable and readable.
    std::memset(ptr, 0xAB, kBlockSize);
    for (std::size_t i = 0; i < kBlockSize; ++i) {
        EXPECT_EQ(ptr[i], Byte{0xAB}) << "at offset " << i;
    }

    pool.deallocate(ptr);
}

TEST(MemoryPoolTest, BlocksAreIndependent) {
    vnic::memory::Pool<8> pool;

    auto* first  = static_cast<Byte*>(pool.allocate());
    auto* second = static_cast<Byte*>(pool.allocate());
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);

    std::memset(first, 0x11, 8);
    std::memset(second, 0x22, 8);

    EXPECT_EQ(first[0], Byte{0x11});
    EXPECT_EQ(second[0], Byte{0x22});

    pool.deallocate(first);
    pool.deallocate(second);
}

TEST(MemoryPoolTest, SmallerBlockSizeIsRespected) {
    vnic::memory::Pool<4> pool;

    auto* ptr = static_cast<Byte*>(pool.allocate());
    ASSERT_NE(ptr, nullptr);

    std::memset(ptr, 0x7F, 4);
    EXPECT_EQ(ptr[0], Byte{0x7F});
    EXPECT_EQ(ptr[3], Byte{0x7F});

    pool.deallocate(ptr);
}

// ---------------------------------------------------------------------------
// deallocate
// ---------------------------------------------------------------------------

TEST(MemoryPoolTest, DeallocateOfNullptrIsSafe) {
    vnic::memory::Pool<32> pool;

    EXPECT_NO_FATAL_FAILURE(pool.deallocate(nullptr));
}

TEST(MemoryPoolTest, AllocateDeallocateCyclesAreStable) {
    vnic::memory::Pool<256> pool;

    for (int i = 0; i < 1000; ++i) {
        void* ptr = pool.allocate();
        ASSERT_NE(ptr, nullptr);
        pool.deallocate(ptr);
    }
}

TEST(MemoryPoolTest, AllocateAfterDeallocateStillWorks) {
    vnic::memory::Pool<32> pool;

    void* first = pool.allocate();
    ASSERT_NE(first, nullptr);
    pool.deallocate(first);

    void* second = pool.allocate();
    ASSERT_NE(second, nullptr);
    pool.deallocate(second);
}

// ---------------------------------------------------------------------------
// Independence of pools
// ---------------------------------------------------------------------------

TEST(MemoryPoolTest, DifferentPoolsAreIndependent) {
    vnic::memory::Pool<16> small;
    vnic::memory::Pool<16> large;

    void* fromSmall = small.allocate();
    void* fromLarge = large.allocate();

    ASSERT_NE(fromSmall, nullptr);
    ASSERT_NE(fromLarge, nullptr);
    EXPECT_NE(fromSmall, fromLarge);

    small.deallocate(fromSmall);
    large.deallocate(fromLarge);
}

TEST(MemoryPoolTest, DifferentBlockSizesCoexist) {
    vnic::memory::Pool<8> p8;
    vnic::memory::Pool<4096> p4096;

    void* a = p8.allocate();
    void* b = p4096.allocate();

    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    p8.deallocate(a);
    p4096.deallocate(b);
}

// ---------------------------------------------------------------------------
// Introspection (getConfig / liveBlocks / totalBlocks)
// ---------------------------------------------------------------------------

TEST(MemoryPoolTest, GetConfigReturnsLastAppliedConfiguration) {
    vnic::memory::Pool<64> pool;
    vnic::memory::Pool<64>::Config config;
    config.blockSize = 64;
    config.capacity  = 32;
    config.socketId  = 3;

    ASSERT_TRUE(pool.configure(config));

    EXPECT_EQ(pool.getConfig().blockSize, 64u);
    EXPECT_EQ(pool.getConfig().capacity, 32u);
    EXPECT_EQ(pool.getConfig().socketId, 3u);
}

TEST(MemoryPoolTest, LiveBlocksTracksAllocationsAndReleases) {
    vnic::memory::Pool<32> pool;
    EXPECT_EQ(pool.liveBlocks(), 0u);

    void* first  = pool.allocate();
    void* second = pool.allocate();
    EXPECT_EQ(pool.liveBlocks(), 2u);

    pool.deallocate(first);
    EXPECT_EQ(pool.liveBlocks(), 1u);

    pool.deallocate(second);
    EXPECT_EQ(pool.liveBlocks(), 0u);
}

TEST(MemoryPoolTest, TotalBlocksIsMonotonic) {
    vnic::memory::Pool<32> pool;

    void* first = pool.allocate();
    pool.deallocate(first);
    EXPECT_EQ(pool.totalBlocks(), 1u);

    void* second = pool.allocate();
    pool.deallocate(second);
    EXPECT_EQ(pool.totalBlocks(), 2u);
    EXPECT_EQ(pool.liveBlocks(), 0u);
}

TEST(MemoryPoolTest, DeallocateNullptrDoesNotUnderflowCounters) {
    vnic::memory::Pool<32> pool;
    (void)pool.allocate();
    EXPECT_EQ(pool.liveBlocks(), 1u);

    pool.deallocate(nullptr);

    EXPECT_EQ(pool.liveBlocks(), 1u);
}

TEST(MemoryPoolTest, IsNotCopyableButIsMovable) {
    static_assert(!std::is_copy_constructible_v<vnic::memory::Pool<32>>);
    static_assert(!std::is_copy_assignable_v<vnic::memory::Pool<32>>);
    static_assert(std::is_move_constructible_v<vnic::memory::Pool<32>>);
    static_assert(std::is_move_assignable_v<vnic::memory::Pool<32>>);
    SUCCEED();
}

TEST(MemoryPoolTest, MovePreservesConfigurationAndCounters) {
    vnic::memory::Pool<32> source;
    vnic::memory::Pool<32>::Config config;
    config.capacity = 7;
    ASSERT_TRUE(source.configure(config));
    void* block = source.allocate();
    ASSERT_NE(block, nullptr);

    vnic::memory::Pool<32> moved{std::move(source)};

    EXPECT_EQ(moved.getConfig().capacity, 7u);
    EXPECT_EQ(moved.liveBlocks(), 1u);
    EXPECT_EQ(moved.totalBlocks(), 1u);

    moved.deallocate(block);
    EXPECT_EQ(moved.liveBlocks(), 0u);
}
