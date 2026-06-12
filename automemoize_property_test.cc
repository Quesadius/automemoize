#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <limits>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "automemoize.h"

// Deterministic, seeded pseudo-random property tests. These check the same
// properties as automemoize_fuzz_test.cc, but run in CI without the
// fuzztest dependency (currently incompatible with Bazel 9; see BUILD).
//
// The central property of a memoizer, checked across randomized call
// sequences against a single instance:
//   1. Every call returns exactly what the original function would return.
//   2. The original function runs exactly once per distinct argument tuple.

namespace {

// A pool of random strings with bytes in [min_byte, max_byte] and lengths
// in [0, 40] (straddling typical small-string-optimization boundaries).
// Sampling from a small pool guarantees the call sequence contains repeats.
std::vector<std::string> RandomStringPool(std::mt19937& rng, int size,
                                          int min_byte, int max_byte) {
  std::uniform_int_distribution<int> length(0, 40);
  std::uniform_int_distribution<int> byte(min_byte, max_byte);
  std::vector<std::string> pool;
  pool.reserve(size);
  for (int i = 0; i < size; ++i) {
    std::string s(length(rng), '\0');
    for (char& c : s) c = static_cast<char>(byte(rng));
    pool.push_back(std::move(s));
  }
  return pool;
}

TEST(AutomemoizePropertyTest, IntSequenceMatchesOriginalAndCountsMisses) {
  std::mt19937 rng(20260612);
  // A small domain so the sequence revisits keys often.
  std::uniform_int_distribution<int> value(-8, 8);

  for (int round = 0; round < 10; ++round) {
    int calls = 0;
    auto memo = automemoize([&calls](int a, int b) {
      ++calls;
      return a * 1000003 + b;
    });

    std::set<std::pair<int, int>> distinct;
    for (int i = 0; i < 2000; ++i) {
      const int a = value(rng);
      const int b = value(rng);
      ASSERT_EQ(memo(a, b), a * 1000003 + b);
      distinct.insert({a, b});
      ASSERT_EQ(calls, static_cast<int>(distinct.size()));
    }
  }
}

TEST(AutomemoizePropertyTest, StringSequenceMatchesOriginalAndCountsMisses) {
  std::mt19937 rng(42);
  // Full byte range, including embedded NULs.
  const std::vector<std::string> pool = RandomStringPool(rng, 32, 0, 255);
  std::uniform_int_distribution<size_t> pick(0, pool.size() - 1);

  int calls = 0;
  // The "#" + s oracle yields a distinct result per distinct key, so a
  // lookup that returned the wrong entry's value would be caught.
  auto memo = automemoize([&calls](const std::string& s) {
    ++calls;
    return "#" + s;
  });

  std::set<std::string> distinct;
  for (int i = 0; i < 3000; ++i) {
    const std::string& s = pool[pick(rng)];
    if (i % 2 == 0) {
      ASSERT_EQ(memo(s), "#" + s);  // Lvalue: zero-copy fast path.
    } else {
      ASSERT_EQ(memo(std::string(s)), "#" + s);  // Rvalue: fast path.
    }
    distinct.insert(s);
    ASSERT_EQ(calls, static_cast<int>(distinct.size()));
  }
}

// Exact-type calls (fast path) and calls whose arguments need conversion
// (const char*, conversion path) must resolve to the SAME cache entry: the
// transparent hash/equality the fast path relies on must agree with the
// stored keys. NUL-free pool so c_str() round-trips.
TEST(AutomemoizePropertyTest, ConvertedArgumentsShareCacheEntries) {
  std::mt19937 rng(7);
  const std::vector<std::string> pool = RandomStringPool(rng, 32, 'a', 'z');
  std::uniform_int_distribution<size_t> pick(0, pool.size() - 1);

  int calls = 0;
  auto memo = automemoize([&calls](const std::string& s) {
    ++calls;
    return "#" + s;
  });

  std::set<std::string> distinct;
  for (int i = 0; i < 3000; ++i) {
    const std::string& s = pool[pick(rng)];
    switch (i % 3) {
      case 0:
        ASSERT_EQ(memo(s), "#" + s);  // Fast path.
        break;
      case 1:
        ASSERT_EQ(memo(s.c_str()), "#" + s);  // Conversion path.
        break;
      default:
        ASSERT_EQ(memo(std::string(s)), "#" + s);  // Rvalue fast path.
        break;
    }
    distinct.insert(s);
    ASSERT_EQ(calls, static_cast<int>(distinct.size()));
  }
}

// Documented behavior: keys are matched with operator==, so an argument
// that does not compare equal to itself (NaN) is never cached — every such
// call invokes f again. It must also never be INSERTED: keys failing
// self-equality violate the hash map's key contract (absl asserts on it in
// debug builds).
TEST(AutomemoizePropertyTest, NanKeysNeverHitTheCache) {
  int calls = 0;
  auto memo = automemoize([&calls](double d) {
    ++calls;
    return std::isnan(d) ? -1.0 : d * 2;
  });

  const double nan = std::numeric_limits<double>::quiet_NaN();
  EXPECT_EQ(memo(nan), -1.0);
  EXPECT_EQ(memo(nan), -1.0);
  EXPECT_EQ(calls, 2);  // NaN != NaN: both calls were misses.

  EXPECT_EQ(memo(1.5), 3.0);
  EXPECT_EQ(memo(1.5), 3.0);
  EXPECT_EQ(calls, 3);  // Ordinary doubles hit as usual.
}

}  // namespace
