#include <set>
#include <string>
#include <utility>
#include <vector>

#include "automemoize.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

// NOTE: This target is disabled in BUILD until fuzztest publishes
// Bazel-9-compatible releases (see the TODO there). The properties below
// mirror the deterministic versions in automemoize_property_test.cc, which
// do run in CI; fuzzing adds coverage-guided input discovery on top.

int Add(int a, int b) { return a + b; }

void MemoizedMatchesOriginal(int a, int b) {
  auto memoized_add = automemoize(Add);
  EXPECT_EQ(memoized_add(a, b), Add(a, b));
}
FUZZ_TEST(AutomemoizeFuzzTest, MemoizedMatchesOriginal);

// One memoizer across a whole call sequence: every call returns what the
// original function would, and the original runs exactly once per distinct
// argument tuple. The small domain forces the sequence to revisit keys.
void IntSequenceMatchesOriginal(const std::vector<std::pair<int, int>>& seq) {
  int calls = 0;
  auto memo = automemoize([&calls](int a, int b) {
    ++calls;
    return a * 1000003 + b;
  });

  std::set<std::pair<int, int>> distinct;
  for (const auto& [a, b] : seq) {
    ASSERT_EQ(memo(a, b), a * 1000003 + b);
    distinct.insert({a, b});
    ASSERT_EQ(calls, static_cast<int>(distinct.size()));
  }
}
FUZZ_TEST(AutomemoizeFuzzTest, IntSequenceMatchesOriginal)
    .WithDomains(fuzztest::VectorOf(
        fuzztest::PairOf(fuzztest::InRange(-8, 8), fuzztest::InRange(-8, 8))));

// The same sequence property with arbitrary strings: embedded NULs and
// varying lengths exercise hashing and equality of the stored keys. The
// "#" + s oracle yields a distinct result per key, so a lookup returning
// the wrong entry's value would be caught.
void StringSequenceMatchesOriginal(const std::vector<std::string>& seq) {
  int calls = 0;
  auto memo = automemoize([&calls](const std::string& s) {
    ++calls;
    return "#" + s;
  });

  std::set<std::string> distinct;
  for (const std::string& s : seq) {
    ASSERT_EQ(memo(s), "#" + s);
    distinct.insert(s);
    ASSERT_EQ(calls, static_cast<int>(distinct.size()));
  }
}
FUZZ_TEST(AutomemoizeFuzzTest, StringSequenceMatchesOriginal);

// Exact-type calls (zero-copy fast path) and calls whose arguments need
// conversion (const char*, conversion path) must resolve to the SAME cache
// entry. Printable domain so c_str() round-trips (no embedded NULs).
void ConvertedCallsShareCacheEntries(const std::vector<std::string>& seq) {
  int calls = 0;
  auto memo = automemoize([&calls](const std::string& s) {
    ++calls;
    return "#" + s;
  });

  std::set<std::string> distinct;
  bool use_cstr = false;
  for (const std::string& s : seq) {
    if (use_cstr) {
      ASSERT_EQ(memo(s.c_str()), "#" + s);  // Conversion path.
    } else {
      ASSERT_EQ(memo(s), "#" + s);  // Fast path.
    }
    use_cstr = !use_cstr;
    distinct.insert(s);
    ASSERT_EQ(calls, static_cast<int>(distinct.size()));
  }
}
FUZZ_TEST(AutomemoizeFuzzTest, ConvertedCallsShareCacheEntries)
    .WithDomains(fuzztest::VectorOf(fuzztest::PrintableAsciiString()));
