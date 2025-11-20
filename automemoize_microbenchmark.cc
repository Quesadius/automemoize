#include <benchmark/benchmark.h>

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

BENCHMARK_MAIN();
