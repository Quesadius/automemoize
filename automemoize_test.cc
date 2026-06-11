#include "automemoize.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <functional>
#include <string>
#include <utility>

TEST(AutomemoizeTest, BasicMemoization) {
  int call_count = 0;
  auto add_counted = [&](int a, int b) {
    call_count++;
    return a + b;
  };

  auto memoized_add = automemoize(add_counted);

  EXPECT_EQ(memoized_add(1, 2), 3);
  EXPECT_EQ(call_count, 1);

  EXPECT_EQ(memoized_add(1, 2), 3);
  EXPECT_EQ(call_count, 1);  // Should be cached

  EXPECT_EQ(memoized_add(2, 3), 5);
  EXPECT_EQ(call_count, 2);
}

struct Multiplier {
  int factor;
  int operator()(int x) const { return x * factor; }
};

TEST(AutomemoizeTest, FunctorSupport) {
  Multiplier m{10};
  auto memoized_m = automemoize(m);

  EXPECT_EQ(memoized_m(5), 50);
  EXPECT_EQ(memoized_m(5), 50);
}

// Global counter for side-effect testing
static int global_call_count = 0;
int side_effect_add(int a, int b) {
  global_call_count++;
  return a + b;
}

TEST(AutomemoizeTest, CachingBehavior) {
  global_call_count = 0;
  auto memoized_add = automemoize(side_effect_add);

  EXPECT_EQ(memoized_add(2, 3), 5);
  EXPECT_EQ(global_call_count, 1);

  EXPECT_EQ(memoized_add(2, 3), 5);
  EXPECT_EQ(global_call_count, 1);  // Should still be 1

  EXPECT_EQ(memoized_add(3, 3), 6);
  EXPECT_EQ(global_call_count, 2);
}

std::string concat(std::string a, std::string b) { return a + b; }

TEST(AutomemoizeTest, StringArguments) {
  auto memoized_concat = automemoize(concat);

  EXPECT_EQ(memoized_concat("hello", "world"), "helloworld");
  EXPECT_EQ(memoized_concat("hello", "world"), "helloworld");
}

TEST(AutomemoizeTest, CacheIndependence) {
  global_call_count = 0;
  auto memo1 = automemoize(side_effect_add);
  auto memo2 = automemoize(side_effect_add);

  EXPECT_EQ(memo1(1, 1), 2);
  EXPECT_EQ(global_call_count, 1);

  EXPECT_EQ(memo1(1, 1), 2);
  EXPECT_EQ(global_call_count, 1);

  EXPECT_EQ(memo2(1, 1), 2);
  EXPECT_EQ(global_call_count,
            2);  // Should call again because memo2 has its own cache
}

struct TestClass {
  int id = 0;
  int member_function(int a) { return a + id; }

  bool operator==(const TestClass& other) const { return id == other.id; }

  template <typename H>
  friend H AbslHashValue(H h, const TestClass& f) {
    return H::combine(std::move(h), f.id);
  }
};

TEST(AutomemoizeTest, MemberFunctionSupport) {
  TestClass obj{10};
  auto memoized_member = automemoize(&TestClass::member_function);

  EXPECT_EQ(memoized_member(obj, 5), 15);
  EXPECT_EQ(memoized_member(obj, 5), 15);

  TestClass obj2{20};
  EXPECT_EQ(memoized_member(obj2, 5), 25);
}

int noexcept_func(int a) noexcept { return a + 1; }

TEST(AutomemoizeTest, NoexceptSupport) {
  auto m_noexcept = automemoize(noexcept_func);
  EXPECT_EQ(m_noexcept(1), 2);
}

struct RefQualified {
  int operator()(int a) & { return a + 1; }
};

TEST(AutomemoizeTest, RefQualifiedSupport) {
  RefQualified rq;
  auto m_rq = automemoize(rq);
  EXPECT_EQ(m_rq(1), 2);
}

struct NoExceptFunctor {
  int operator()(int a) const noexcept { return a + 1; }
};

TEST(AutomemoizeTest, NoexceptFunctorSupport) {
  NoExceptFunctor nef;
  auto m_nef = automemoize(nef);
  EXPECT_EQ(m_nef(1), 2);
}

TEST(AutomemoizeTest, LambdaSupport) {
  auto lambda = [](int a) { return a + 1; };
  auto m_lambda = automemoize(lambda);
  EXPECT_EQ(m_lambda(1), 2);
}

// Helper for rvalue test
std::string return_same(std::string s) { return s; }

TEST(AutomemoizeTest, RvalueSupport) {
  std::string input = "hello";
  auto m_str = automemoize(return_same);
  // Pass as rvalue. If moved into key, function receives empty string.
  std::string result = m_str(std::move(input));
  EXPECT_EQ(result, "hello");
}

// Helper for const member test
struct ConstMember {
  int val = 10;
  int get() const { return val; }
  bool operator==(const ConstMember& other) const { return val == other.val; }
  template <typename H>
  friend H AbslHashValue(H h, const ConstMember& c) {
    return H::combine(std::move(h), c.val);
  }
};

TEST(AutomemoizeTest, ConstMemberSupport) {
  ConstMember cm;
  auto m_const = automemoize(&ConstMember::get);
  EXPECT_EQ(m_const(cm), 10);
}

// Helper for noexcept member test
struct NoexceptMember {
  int val = 20;
  int get() const noexcept { return val; }
  bool operator==(const NoexceptMember& other) const {
    return val == other.val;
  }
  template <typename H>
  friend H AbslHashValue(H h, const NoexceptMember& c) {
    return H::combine(std::move(h), c.val);
  }
};

TEST(AutomemoizeTest, NoexceptMemberSupport) {
  NoexceptMember nm;
  auto m_noexcept = automemoize(&NoexceptMember::get);
  EXPECT_EQ(m_noexcept(nm), 20);
}

// A function may re-enter its own memoizer (recursive memoization). The
// implementation must not hold cache iterators across the invocation of the
// wrapped function, or re-entrant inserts would invalidate them.
TEST(AutomemoizeTest, ReentrantRecursion) {
  int call_count = 0;
  std::function<int64_t(int)> memo_fib;
  memo_fib = automemoize(std::function<int64_t(int)>([&](int n) -> int64_t {
    call_count++;
    if (n <= 1) return n;
    return memo_fib(n - 1) + memo_fib(n - 2);
  }));

  EXPECT_EQ(memo_fib(40), 102334155);
  // Linear, not exponential: each n computed exactly once.
  EXPECT_EQ(call_count, 41);

  EXPECT_EQ(memo_fib(40), 102334155);
  EXPECT_EQ(call_count, 41);  // Fully cached
}

// Whether automemoize accepts a callable. Must be checked through a template
// (substitution context) so that constraint failures evaluate to false
// instead of being hard errors.
template <typename F>
concept CanAutomemoize = requires(F f) { automemoize(f); };

// Sanity check: supported callables satisfy the concept.
static_assert(CanAutomemoize<int (*)(int)>);

// Callables whose signature cannot be deduced (e.g. generic lambdas) must
// fail the constraint cleanly, not hard-error inside function_traits.
TEST(AutomemoizeTest, RejectsGenericLambdaCleanly) {
  auto generic = [](auto x) { return x; };
  static_assert(!CanAutomemoize<decltype(generic)>);
}

// Functions whose decayed arguments cannot be compared must also fail the
// constraint cleanly.
struct NotComparable {
  int v = 0;
  template <typename H>
  friend H AbslHashValue(H h, const NotComparable& n) {
    return H::combine(std::move(h), n.v);
  }
};

TEST(AutomemoizeTest, RejectsNonComparableArgsCleanly) {
  auto f = [](NotComparable n) { return n.v; };
  static_assert(!CanAutomemoize<decltype(f)>);
}
