#include <gtest/gtest.h>

#include "utils/rcu.h"

#include <atomic>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// A value whose two fields must always stay equal. Used to check that an update
// is observed as a whole (never partially updated).
struct Coupled {
    int a{0};
    int b{0};
};

} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(RcuTest, DefaultConstructedValueIsValueInitialized) {
    vnic::utils::Rcu<int> rcu;

    EXPECT_EQ(rcu.snapshot(), 0);
}

TEST(RcuTest, ConstructWithValue) {
    vnic::utils::Rcu<int> rcu{42};

    EXPECT_EQ(rcu.snapshot(), 42);
}

TEST(RcuTest, ConstructWithNonTrivialType) {
    vnic::utils::Rcu<std::string> rcu{std::string{"hello"}};

    EXPECT_EQ(rcu.snapshot(), "hello");
}

TEST(RcuTest, IsNotCopyable) {
    static_assert(!std::is_copy_constructible_v<vnic::utils::Rcu<int>>);
    static_assert(!std::is_copy_assignable_v<vnic::utils::Rcu<int>>);
    SUCCEED();
}

// ---------------------------------------------------------------------------
// read
// ---------------------------------------------------------------------------

TEST(RcuTest, ReadInvokesCallbackWithCurrentValue) {
    vnic::utils::Rcu<int> rcu{21};

    const int doubled = rcu.read([](const int& value) { return value * 2; });

    EXPECT_EQ(doubled, 42);
}

TEST(RcuTest, ReadReturnsCallbackResultByValue) {
    vnic::utils::Rcu<int> rcu{7};

    auto value = rcu.read([](const int& v) { return v; });

    static_assert(std::is_same_v<decltype(value), int>, "read must return by value");
    EXPECT_EQ(value, 7);
}

TEST(RcuTest, ReadSupportsArbitraryReturnTypes) {
    vnic::utils::Rcu<int> rcu{5};

    const bool isPositive  = rcu.read([](const int& v) { return v > 0; });
    const std::string text = rcu.read([](const int&) { return std::string{"done"}; });

    EXPECT_TRUE(isPositive);
    EXPECT_EQ(text, "done");
}

TEST(RcuTest, ReadDoesNotModifyValue) {
    vnic::utils::Rcu<int> rcu{3};

    (void)rcu.read([](const int& value) { return value * 10; });

    EXPECT_EQ(rcu.snapshot(), 3);
}

// ---------------------------------------------------------------------------
// snapshot
// ---------------------------------------------------------------------------

TEST(RcuTest, SnapshotReturnsCopyOfValue) {
    vnic::utils::Rcu<std::string> rcu{std::string{"original"}};

    std::string copy = rcu.snapshot();
    copy             = "modified";

    EXPECT_EQ(copy, "modified");
    EXPECT_EQ(rcu.snapshot(), "original"); // internal value is untouched
}

// ---------------------------------------------------------------------------
// write
// ---------------------------------------------------------------------------

TEST(RcuTest, WriteReplacesValue) {
    vnic::utils::Rcu<int> rcu{1};

    rcu.write(99);

    EXPECT_EQ(rcu.snapshot(), 99);
}

TEST(RcuTest, WriteIsVisibleThroughRead) {
    vnic::utils::Rcu<int> rcu{1};

    rcu.write(10);
    EXPECT_EQ(rcu.read([](const int& v) { return v; }), 10);

    rcu.write(20);
    EXPECT_EQ(rcu.read([](const int& v) { return v; }), 20);
}

TEST(RcuTest, WriteWithNonTrivialType) {
    vnic::utils::Rcu<std::string> rcu{std::string{"first"}};

    rcu.write(std::string{"second"});

    EXPECT_EQ(rcu.snapshot(), "second");
}

// ---------------------------------------------------------------------------
// Concurrency
// ---------------------------------------------------------------------------

TEST(RcuTest, ConcurrentWritesAndReadsObserveConsistentValues) {
    vnic::utils::Rcu<Coupled> rcu{Coupled{0, 0}};

    std::atomic<bool> stop{false};
    std::atomic<int> mismatches{0};

    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back([&rcu, &stop, &mismatches] {
            while (!stop.load(std::memory_order_relaxed)) {
                Coupled value = rcu.snapshot();
                if (value.a != value.b) {
                    mismatches.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    std::thread writer{[&rcu, &stop] {
        for (int i = 1; i <= 5000; ++i) {
            rcu.write(Coupled{i, i});
        }
        stop.store(true, std::memory_order_relaxed);
    }};

    writer.join();
    for (auto& reader : readers) {
        reader.join();
    }

    EXPECT_EQ(mismatches.load(), 0);
    EXPECT_EQ(rcu.snapshot().a, 5000);
}

TEST(RcuTest, ReadUnderConcurrentWriteNeverSeesBrokenInvariant) {
    vnic::utils::Rcu<Coupled> rcu{Coupled{0, 0}};

    std::atomic<bool> stop{false};
    std::atomic<int> mismatches{0};

    std::thread writer{[&rcu, &stop] {
        for (int i = 1; i <= 5000; ++i) {
            rcu.write(Coupled{i, i});
        }
        stop.store(true, std::memory_order_relaxed);
    }};

    while (!stop.load(std::memory_order_relaxed)) {
        const int broken = rcu.read([](const Coupled& value) { return value.a != value.b ? 1 : 0; });
        if (broken != 0) {
            mismatches.fetch_add(1, std::memory_order_relaxed);
        }
    }

    writer.join();
    EXPECT_EQ(mismatches.load(), 0);
}
