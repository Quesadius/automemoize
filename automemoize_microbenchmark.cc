#include <benchmark/benchmark.h>

#include <cstddef>
#include <string>

#include "automemoize.h"

// Naive recursive Fibonacci
int fibonacci(int n) {
  if (n <= 1) return n;
  return fibonacci(n - 1) + fibonacci(n - 2);
}

static void BM_Fibonacci_Naive(benchmark::State& state) {
  for (auto _ : state) {
    benchmark::DoNotOptimize(fibonacci(state.range(0)));
  }
}
BENCHMARK(BM_Fibonacci_Naive)->Arg(20);

// Memoized Fibonacci
static void BM_Fibonacci_Memoized(benchmark::State& state) {
  auto memoized_fib = automemoize(fibonacci);
  for (auto _ : state) {
    benchmark::DoNotOptimize(memoized_fib(state.range(0)));
  }
}
BENCHMARK(BM_Fibonacci_Memoized)->Arg(20);

// Cache-hit overhead with cheap int keys.
static void BM_CacheHit_IntKeys(benchmark::State& state) {
  auto memoized_add = automemoize([](int a, int b) { return a + b; });
  benchmark::DoNotOptimize(memoized_add(1, 2));  // Warm the cache.
  for (auto _ : state) {
    benchmark::DoNotOptimize(memoized_add(1, 2));
  }
}
BENCHMARK(BM_CacheHit_IntKeys);

// Cache-hit overhead with expensive-to-copy string keys.
static void BM_CacheHit_StringKeys(benchmark::State& state) {
  auto memoized = automemoize([](const std::string& a, const std::string& b) {
    return a.size() + b.size();
  });
  std::string a(state.range(0), 'a');
  std::string b(state.range(0), 'b');
  benchmark::DoNotOptimize(memoized(a, b));  // Warm the cache.
  for (auto _ : state) {
    benchmark::DoNotOptimize(memoized(a, b));
  }
}
BENCHMARK(BM_CacheHit_StringKeys)->Arg(8)->Arg(64)->Arg(1024);

BENCHMARK_MAIN();
