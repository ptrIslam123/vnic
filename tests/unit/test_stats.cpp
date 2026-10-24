#include <gtest/gtest.h>

#include "stats/stats.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

namespace {

constexpr std::size_t kCounterCount{10};

std::array<std::uint64_t, kCounterCount> Snapshot(const vnic::stats::Stats& s) {
    return {
        s.loadProcessedPackets(), s.loadProcessedBytes(), s.loadDroppedPackets(),     s.loadDroppedBytes(),
        s.loadErrors(),           s.loadOverflows(),      s.loadTransmittedPackets(), s.loadTransmittedBytes(),
        s.loadTxDroppedPackets(), s.loadTxDroppedBytes(),
    };
}

bool AllZero(const std::array<std::uint64_t, kCounterCount>& values) {
    for (const auto value : values) {
        if (value != 0) {
            return false;
        }
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Initial state
// ---------------------------------------------------------------------------

TEST(StatsTest, FreshStatsAreZero) {
    const vnic::stats::Stats stats;

    EXPECT_TRUE(AllZero(Snapshot(stats)));
}

// ---------------------------------------------------------------------------
// fetch*/load* pairs
// ---------------------------------------------------------------------------

TEST(StatsTest, ProcessedPacketsCounter) {
    vnic::stats::Stats stats;

    stats.fetchProcessedPackets();
    EXPECT_EQ(stats.loadProcessedPackets(), 1u);

    stats.fetchProcessedPackets(4);
    EXPECT_EQ(stats.loadProcessedPackets(), 5u);
}

TEST(StatsTest, ProcessedBytesCounter) {
    vnic::stats::Stats stats;

    stats.fetchProcessedBytes();
    EXPECT_EQ(stats.loadProcessedBytes(), 1u);

    stats.fetchProcessedBytes(1500);
    EXPECT_EQ(stats.loadProcessedBytes(), 1501u);
}

TEST(StatsTest, DroppedPacketsCounter) {
    vnic::stats::Stats stats;

    stats.fetchDroppedPackets();
    EXPECT_EQ(stats.loadDroppedPackets(), 1u);

    stats.fetchDroppedPackets(3);
    EXPECT_EQ(stats.loadDroppedPackets(), 4u);
}

TEST(StatsTest, DroppedBytesCounter) {
    vnic::stats::Stats stats;

    stats.fetchDroppedBytes();
    EXPECT_EQ(stats.loadDroppedBytes(), 1u);

    stats.fetchDroppedBytes(64);
    EXPECT_EQ(stats.loadDroppedBytes(), 65u);
}

TEST(StatsTest, ErrorsCounter) {
    vnic::stats::Stats stats;

    stats.fetchErrors();
    EXPECT_EQ(stats.loadErrors(), 1u);

    stats.fetchErrors(9);
    EXPECT_EQ(stats.loadErrors(), 10u);
}

TEST(StatsTest, OverflowsCounter) {
    vnic::stats::Stats stats;

    stats.fetchOverflows();
    EXPECT_EQ(stats.loadOverflows(), 1u);

    stats.fetchOverflows(2);
    EXPECT_EQ(stats.loadOverflows(), 3u);
}

TEST(StatsTest, TransmittedPacketsCounter) {
    vnic::stats::Stats stats;

    stats.fetchTransmittedPackets();
    EXPECT_EQ(stats.loadTransmittedPackets(), 1u);

    stats.fetchTransmittedPackets(7);
    EXPECT_EQ(stats.loadTransmittedPackets(), 8u);
}

TEST(StatsTest, TransmittedBytesCounter) {
    vnic::stats::Stats stats;

    stats.fetchTransmittedBytes();
    EXPECT_EQ(stats.loadTransmittedBytes(), 1u);

    stats.fetchTransmittedBytes(2048);
    EXPECT_EQ(stats.loadTransmittedBytes(), 2049u);
}

TEST(StatsTest, TxDroppedPacketsCounter) {
    vnic::stats::Stats stats;

    stats.fetchTxDroppedPackets();
    EXPECT_EQ(stats.loadTxDroppedPackets(), 1u);

    stats.fetchTxDroppedPackets(6);
    EXPECT_EQ(stats.loadTxDroppedPackets(), 7u);
}

TEST(StatsTest, TxDroppedBytesCounter) {
    vnic::stats::Stats stats;

    stats.fetchTxDroppedBytes();
    EXPECT_EQ(stats.loadTxDroppedBytes(), 1u);

    stats.fetchTxDroppedBytes(100);
    EXPECT_EQ(stats.loadTxDroppedBytes(), 101u);
}

// ---------------------------------------------------------------------------
// Independence
// ---------------------------------------------------------------------------

TEST(StatsTest, CountersAreIndependent) {
    vnic::stats::Stats stats;

    stats.fetchProcessedPackets(3);
    stats.fetchDroppedBytes(7);
    stats.fetchOverflows();

    const auto values = Snapshot(stats);
    EXPECT_EQ(values[0], 3u); // processedPackets
    EXPECT_EQ(values[3], 7u); // droppedBytes
    EXPECT_EQ(values[5], 1u); // overflows

    // Everything else stays zero.
    EXPECT_EQ(values[1], 0u);
    EXPECT_EQ(values[2], 0u);
    EXPECT_EQ(values[4], 0u);
    EXPECT_EQ(values[6], 0u);
    EXPECT_EQ(values[7], 0u);
    EXPECT_EQ(values[8], 0u);
    EXPECT_EQ(values[9], 0u);
}

// ---------------------------------------------------------------------------
// reset
// ---------------------------------------------------------------------------

TEST(StatsTest, ResetClearsEveryCounter) {
    vnic::stats::Stats stats;

    stats.fetchProcessedPackets(11);
    stats.fetchProcessedBytes(12);
    stats.fetchDroppedPackets(13);
    stats.fetchDroppedBytes(14);
    stats.fetchErrors(15);
    stats.fetchOverflows(16);
    stats.fetchTransmittedPackets(17);
    stats.fetchTransmittedBytes(18);
    stats.fetchTxDroppedPackets(19);
    stats.fetchTxDroppedBytes(20);
    ASSERT_FALSE(AllZero(Snapshot(stats)));

    stats.reset();

    EXPECT_TRUE(AllZero(Snapshot(stats)));
}

// ---------------------------------------------------------------------------
// Copyability
// ---------------------------------------------------------------------------

TEST(StatsTest, CopyIsIndependent) {
    vnic::stats::Stats original;
    original.fetchProcessedPackets(5);

    vnic::stats::Stats copy = original;
    original.fetchProcessedPackets(3);

    EXPECT_EQ(copy.loadProcessedPackets(), 5u);
    EXPECT_EQ(original.loadProcessedPackets(), 8u);
}

TEST(StatsTest, AssignmentCopiesCurrentValues) {
    vnic::stats::Stats source;
    source.fetchTransmittedBytes(1024);

    vnic::stats::Stats target;
    target.fetchTransmittedBytes(1);
    target = source;

    EXPECT_EQ(target.loadTransmittedBytes(), 1024u);
}

// ---------------------------------------------------------------------------
// Thread safety
// ---------------------------------------------------------------------------

TEST(StatsTest, ConcurrentIncrementsAreAccumulated) {
    vnic::stats::Stats stats;

    constexpr int kThreads{4};
    constexpr int kIterations{10000};

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&stats] {
            for (int j = 0; j < kIterations; ++j) {
                stats.fetchProcessedPackets();
                stats.fetchProcessedBytes(2);
                stats.fetchOverflows();
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    const auto expected = static_cast<std::uint64_t>(kThreads) * kIterations;
    EXPECT_EQ(stats.loadProcessedPackets(), expected);
    EXPECT_EQ(stats.loadProcessedBytes(), expected * 2);
    EXPECT_EQ(stats.loadOverflows(), expected);
}
