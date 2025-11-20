#include "automemoize.h"
#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

int Add(int a, int b) { return a + b; }

void MemoizedMatchesOriginal(int a, int b) {
  auto memoized_add = automemoize(Add);
  EXPECT_EQ(memoized_add(a, b), Add(a, b));
}
FUZZ_TEST(AutomemoizeFuzzTest, MemoizedMatchesOriginal);
