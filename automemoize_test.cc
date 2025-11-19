#include "automemoize.h"
#include <gtest/gtest.h>
#include <string>

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
    EXPECT_EQ(call_count, 1); // Should be cached
    
    EXPECT_EQ(memoized_add(2, 3), 5);
    EXPECT_EQ(call_count, 2);
}

struct Multiplier {
    int factor;
    int operator()(int x) const {
        return x * factor;
    }
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
    EXPECT_EQ(global_call_count, 1); // Should still be 1
    
    EXPECT_EQ(memoized_add(3, 3), 6);
    EXPECT_EQ(global_call_count, 2);
}

std::string concat(std::string a, std::string b) {
    return a + b;
}

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
    EXPECT_EQ(global_call_count, 2); // Should call again because memo2 has its own cache
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

int noexcept_func(int a) noexcept {
    return a + 1;
}

struct RefQualified {
    int operator()(int a) & { return a + 1; }
};

struct NoExceptFunctor {
    int operator()(int a) const noexcept { return a + 1; }
};

TEST(AutomemoizeTest, EdgeCaseSupport) {
    // 1. noexcept free function
    auto m_noexcept = automemoize(noexcept_func);
    EXPECT_EQ(m_noexcept(1), 2);

    // 2. Ref-qualified (lvalue)
    RefQualified rq;
    auto m_rq = automemoize(rq);
    EXPECT_EQ(m_rq(1), 2);

    // 3. Noexcept functor
    NoExceptFunctor nef;
    auto m_nef = automemoize(nef);
    EXPECT_EQ(m_nef(1), 2);
    
    // 4. noexcept lambda
    auto noexcept_lambda = [](int a) noexcept { return a + 1; };
    auto m_noexcept_lambda = automemoize(noexcept_lambda);
    EXPECT_EQ(m_noexcept_lambda(1), 2);
}

// Helper for rvalue test
std::string return_same(std::string s) {
    return s;
}

// Helper for const member test
struct ConstMember {
    int val = 10;
    int get() const { return val; }
    bool operator==(const ConstMember& other) const { return val == other.val; }
    template <typename H> friend H AbslHashValue(H h, const ConstMember& c) { return H::combine(std::move(h), c.val); }
};

// Helper for noexcept member test
struct NoexceptMember {
    int val = 20;
    int get() const noexcept { return val; }
    bool operator==(const NoexceptMember& other) const { return val == other.val; }
    template <typename H> friend H AbslHashValue(H h, const NoexceptMember& c) { return H::combine(std::move(h), c.val); }
};

TEST(AutomemoizeTest, CoverageGaps) {
    // 1. Rvalue bug check
    auto m_str = automemoize(return_same);
    std::string input = "hello";
    // Pass as rvalue. If moved into key, function receives empty string.
    std::string result = m_str(std::move(input)); 
    EXPECT_EQ(result, "hello");

    // 2. Const member function
    ConstMember cm;
    auto m_const = automemoize(&ConstMember::get);
    EXPECT_EQ(m_const(cm), 10);

    // 3. Noexcept member function
    NoexceptMember nm;
    auto m_noexcept = automemoize(&NoexceptMember::get);
    EXPECT_EQ(m_noexcept(nm), 20);
}
