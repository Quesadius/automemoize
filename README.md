# Automemoize

![C++](https://img.shields.io/badge/C%2B%2B-20%20%2F%2023-blue)
![Build](https://img.shields.io/badge/build-Bazel-43A047)
![Header-only](https://img.shields.io/badge/header--only-yes-success)

A tiny, header-only C++ memoization library. Wrap any callable and get back a
drop-in replacement that caches results by argument value: the first call with
a given set of arguments runs the function, and every later call with equal
arguments returns the stored result without running it again.

```cpp
#include "automemoize.h"

auto slow_square = [](int n) {
  // Imagine this is expensive.
  return n * n;
};

auto fast_square = automemoize(slow_square);

fast_square(9);  // Computes 81.
fast_square(9);  // Returns the cached 81 — slow_square is not called.
```

That is the whole idea. The rest of this document explains exactly what is
cached, what callables are accepted, the guarantees you can rely on, and the
sharp edges worth knowing.

## Table of contents

- [Why memoize](#why-memoize)
- [Features](#features)
- [Installation](#installation)
- [Quick start](#quick-start)
- [The three entry points](#the-three-entry-points)
  - [automemoize](#automemoizef)
  - [automemoize_recursive](#automemoize_recursivesigf)
  - [The AUTOMEMOIZED macro](#the-automemoized-macro)
- [Supported callables](#supported-callables)
- [What gets rejected, and why](#what-gets-rejected-and-why)
- [Semantics in detail](#semantics-in-detail)
- [Performance](#performance)
- [Recipes](#recipes)
- [How it works](#how-it-works)
- [Comparison with Python](#comparison-with-python)
- [FAQ](#faq)
- [Building and testing this repository](#building-and-testing-this-repository)

## Why memoize

Memoization trades memory for time. It is the right tool when a function is:

- **Pure** — its result depends only on its arguments, with no observable side
  effects. Memoizing an impure function freezes whatever the first call
  returned, which is almost never what you want.
- **Expensive relative to a hash lookup** — recomputation costs more than
  hashing the arguments and probing a hash map.
- **Called repeatedly with repeating arguments** — if every call is unique, the
  cache only adds overhead.

Classic fits include recursive algorithms with overlapping subproblems
(Fibonacci, edit distance, many dynamic-programming problems), parsing or
compilation lookups, and pure configuration or geometry calculations queried
many times with the same inputs.

## Features

- **One function call to adopt** — `automemoize(f)` returns a caching wrapper
  with the same call signature as `f`.
- **Works with everything callable** — free functions, function pointers,
  lambdas (including move-only ones), functors, member function pointers, and
  C++23 static call operators.
- **Zero-copy cache hits** — when your argument types match the stored key
  types, a cache hit performs no argument copies or allocations. Passing a
  large `std::string` you have already looked up costs a hash and a compare,
  not a copy.
- **Compile-time safety** — unsupported callables are rejected with a clean
  "constraint not satisfied" at the call site, not a wall of template errors
  from deep inside the library.
- **Recursion-aware** — a dedicated combinator and a decorator-style macro make
  recursive memoization (the canonical use case) correct and easy.
- **Header-only** — one header, one real dependency (Abseil).

## Installation

Automemoize is a single header, [`automemoize.h`](automemoize.h). Its only
runtime dependency is [Abseil](https://abseil.io/) (for `absl::flat_hash_map`
and `absl::Hash`). It requires **C++20 or later**.

### With Bazel (bzlmod)

Automemoize is a Bazel module named `automemoize`. Because it is not (yet)
published to the Bazel Central Registry, point at the source directly from your
own `MODULE.bazel`.

For a project that should track a specific commit of the upstream repository:

```starlark
bazel_dep(name = "automemoize", version = "1.0.0")

git_override(
    module_name = "automemoize",
    remote = "https://github.com/Quesadius/automemoize.git",
    commit = "0000000000000000000000000000000000000000",  # pin a real SHA
)
```

For a project sitting next to a local checkout:

```starlark
bazel_dep(name = "automemoize", version = "1.0.0")

local_path_override(
    module_name = "automemoize",
    path = "../automemoize",
)
```

Then depend on the library target and include the header:

```starlark
cc_binary(
    name = "main",
    srcs = ["main.cc"],
    deps = ["@automemoize"],
)
```

Abseil is pulled in transitively — you do not need to declare it yourself
unless you also use it directly. Make sure your build compiles as C++20, for
example with this line in your `.bazelrc`:

```text
build --cxxopt=-std=c++20
```

### Without Bazel (vendoring)

Because it is a single header, you can copy `automemoize.h` into your project
(or add this repository as a submodule) and provide Abseil yourself. With
CMake and `FetchContent`:

```cmake
include(FetchContent)
set(ABSL_PROPAGATE_CXX_STD ON)
FetchContent_Declare(
  absl
  URL https://github.com/abseil/abseil-cpp/archive/refs/tags/20260107.1.tar.gz
)
FetchContent_MakeAvailable(absl)

add_executable(app main.cc)
target_compile_features(app PRIVATE cxx_std_20)
target_include_directories(app PRIVATE third_party/automemoize)
target_link_libraries(app PRIVATE absl::flat_hash_map absl::hash)
```

## Quick start

```cpp
#include <iostream>
#include <string>

#include "automemoize.h"

int main() {
  // Wrap a lambda.
  auto add = automemoize([](int a, int b) { return a + b; });
  std::cout << add(2, 3) << "\n";  // 5, computed
  std::cout << add(2, 3) << "\n";  // 5, cached

  // Wrap a function that takes strings.
  auto greet = automemoize(
      [](const std::string& name) { return "Hello, " + name + "!"; });
  std::cout << greet("world") << "\n";  // computed
  std::cout << greet("world") << "\n";  // cached, no string copy on the hit

  return 0;
}
```

## The three entry points

Automemoize exposes one function for the common case, one for recursion, and
one macro for defining a memoized function in a single step.

### `automemoize(f)`

Wraps a callable `f` and returns a new callable with the same parameters and
return type. The returned object owns a private cache.

```cpp
auto memoized = automemoize(some_callable);
auto result = memoized(arg1, arg2);
```

The returned wrapper is a value you can store, move, and copy. Copying it
copies the cache. Each separately created wrapper has an independent cache:

```cpp
int calls = 0;
auto f = [&](int x) { ++calls; return x + 1; };

auto a = automemoize(f);
auto b = automemoize(f);  // Distinct cache from a.

a(10);  // calls == 1
a(10);  // calls == 1 (a's cache hit)
b(10);  // calls == 2 (b has never seen 10)
```

### `automemoize_recursive<Sig>(f)`

Wrapping a normal recursive function only caches the outermost call — the
recursive calls inside the function body still call the original, uncached
function. `automemoize_recursive` solves this by passing the memoizer to your
function as its first argument, so recursive calls made through it are cached.

You must spell the signature explicitly as `Return(Args...)`, because a lambda
that takes `auto& self` is a template and its argument types cannot be deduced.

```cpp
#include <cstdint>

auto fib = automemoize_recursive<int64_t(int)>(
    [](auto& self, int n) -> int64_t {
      if (n <= 1) return n;
      return self(n - 1) + self(n - 2);  // Recurse through the cache.
    });

fib(90);  // Linear time, not exponential.
```

The leading `self` parameter is the memoizer itself; the remaining parameters
(`int n` above) follow the same rules as `automemoize`. Note that C++23's
deducing-this recursive lambdas do **not** replace this: there, `self` refers
to the raw closure, so recursion would bypass the cache.

### The AUTOMEMOIZED macro

`AUTOMEMOIZED(ReturnType, name, (params)) { body }` defines a memoized function
in one step, analogous to Python's `@functools.cache` decorator. Calls to
`name` anywhere — including recursive calls in the body — go through one
shared, lazily initialized, unbounded cache.

```cpp
#include <cstdint>

AUTOMEMOIZED(int64_t, fib, (int n)) {
  if (n <= 1) return n;
  return fib(n - 1) + fib(n - 2);  // Recursive calls are cached.
}

// Elsewhere:
fib(90);  // Computed once per distinct n, then cached forever.
```

Unlike `automemoize`, which gives each wrapper instance its own cache, this
defines exactly one function with one process-wide cache. Use it at namespace
scope. The parameter list is spelled twice in the expansion, so it must not use
default arguments, and `ReturnType` must not contain a bare comma (alias such
types first, for example `using Pair = std::pair<int, int>;`).

## Supported callables

Automemoize deduces the signature of almost anything you can call.

**Free functions and function pointers:**

```cpp
int add(int a, int b) { return a + b; }

auto m = automemoize(add);
auto m2 = automemoize(&add);  // Equivalent.
```

**Lambdas:**

```cpp
auto m = automemoize([](double x) { return x * x; });
```

**Functors (objects with `operator()`):**

```cpp
struct Multiplier {
  int factor;
  int operator()(int x) const { return x * factor; }
};

auto triple = automemoize(Multiplier{3});
triple(10);  // 30
```

**Member function pointers** — the object becomes the leading argument, stored
in the cache by value. The class must be hashable and equality comparable:

```cpp
struct Account {
  int id;
  int balance() const { return id * 100; }

  bool operator==(const Account& o) const { return id == o.id; }
  template <typename H>
  friend H AbslHashValue(H h, const Account& a) {
    return H::combine(std::move(h), a.id);
  }
};

auto balance = automemoize(&Account::balance);
balance(Account{7});  // 700, keyed on the Account
```

**Move-only callables**, such as a lambda capturing a `std::unique_ptr`:

```cpp
auto p = std::make_unique<int>(5);
auto m = automemoize([p = std::move(p)](int x) { return x + *p; });
m(1);  // 6
```

**C++23 static call operators** — a functor whose `operator()` is `static`:

```cpp
struct Hash3 {
  static int operator()(int x) { return x * 3; }  // C++23
};

auto m = automemoize(Hash3{});
```

## What gets rejected, and why

Automemoize constrains its input so that mistakes fail at compile time, at the
call site, with a readable message. The following are rejected by design:

| Rejected input                             | Reason                                                          |
| ------------------------------------------ | --------------------------------------------------------------- |
| Generic lambda (`[](auto x){...}`)         | No single signature to deduce.                                  |
| Overloaded `operator()`                    | Ambiguous signature.                                            |
| Non-const lvalue-reference parameter       | Out-parameter; its writes could not be replayed on a cache hit. |
| Rvalue-reference parameter                 | Cannot be re-invoked from a stored key.                         |
| `void` return                              | Nothing to cache.                                               |
| Reference return                           | A reference into the cache would dangle when the map rehashes.  |
| Move-only return                           | The result must be both stored and returned by copy.            |
| Argument type not hashable by `absl::Hash` | The cache cannot key on it.                                     |
| Argument type not equality comparable      | The cache cannot resolve collisions.                            |

Because the rejection is a constraint failure, you can detect supportability in
your own code with a `requires` expression:

```cpp
template <typename F>
concept Memoizable = requires(F f) { automemoize(std::move(f)); };

static_assert(Memoizable<int (*)(int)>);
static_assert(!Memoizable<decltype([](auto x) { return x; })>);
```

If you need to memoize a function with a move-only result, return a
`std::shared_ptr` instead (see [Recipes](#recipes)).

## Semantics in detail

**Arguments are stored by value.** Reference and `const` qualifiers are
stripped from parameters to form the key type: a function taking
`const std::string&` is keyed on `std::string`. This is what lets the cache
outlive the caller's arguments.

**Cache hits do not copy arguments.** When the decayed types of the arguments
you pass exactly match the stored key types, lookup happens through a tuple of
references — no copies, no allocations on a hit. Arguments that need a
conversion (for example a string literal passed where a `std::string` is
expected) are converted once before lookup. Rvalue arguments are moved into the
function only on a miss; on a hit they are left untouched.

**Results are returned by value.** Each call returns a copy of the cached
result. The library never hands out a reference into its map, so a later
insertion that rehashes the map can never invalidate a result you are holding.

**Each wrapper owns its cache.** Two wrappers built from the same function do
not share results. The `AUTOMEMOIZED` macro is the explicit exception: it
creates a single function backed by a single shared cache.

**Recursion is supported.** Your function may legally call back into the same
memoizer (directly via `automemoize_recursive`/`AUTOMEMOIZED`, or indirectly
through a captured reference). The implementation never holds a cache iterator
across a call to your function, so re-entrant insertions are safe.

**Irreflexive keys are never cached.** Keys are matched with `operator==`. A
value that does not compare equal to itself — most notably floating-point
`NaN` — can never be found again, so such calls are passed through uncached:
the function runs every time and the cache does not grow. This avoids both an
unbounded memory leak and a violation of the hash map's key contract.

```cpp
auto f = automemoize([](double x) { return x * 2; });
double nan = std::numeric_limits<double>::quiet_NaN();
f(nan);  // Always recomputes; never stored.
f(2.0);  // Cached normally.
```

**Thread-compatibility, not thread-safety.** A single memoizer may be read and
written concurrently only with external synchronization, because a cache hit
still mutates internal map state on a miss and the underlying `flat_hash_map`
is not thread-safe. Distinct memoizer instances are independent and safe to use
from different threads. For concurrent access to one cache, wrap it:

```cpp
#include <mutex>

auto raw = automemoize(expensive_fn);
std::mutex m;

auto safe_call = [&](auto&&... args) {
  std::lock_guard<std::mutex> lock(m);
  return raw(std::forward<decltype(args)>(args)...);
};
```

## Performance

The point of memoization is to turn recomputation into a hash lookup, and the
point of the zero-copy fast path is to keep that lookup cheap even for
expensive-to-copy keys.

The repository ships a microbenchmark
([`automemoize_microbenchmark.cc`](automemoize_microbenchmark.cc)). The figures
below are illustrative and machine-dependent; run the benchmark yourself for
numbers that mean something on your hardware. The shape, however, is the point:

- A memoized cache hit for a naive recursive Fibonacci is roughly four orders
  of magnitude faster than recomputing it.
- A cache hit on a one-kilobyte `std::string` key is dominated by hashing, not
  copying, because the argument is never copied on a hit.

Run it with optimizations on:

```bash
bazel run -c opt //:automemoize_benchmark
```

## Recipes

**Memoizing a function with an expensive or move-only result.** Return a
`std::shared_ptr`. The cache then stores and copies only a pointer, and callers
share one instance of the result:

```cpp
auto load_config = automemoize([](std::string path) {
  return std::make_shared<const Config>(ParseConfig(path));
});

std::shared_ptr<const Config> c = load_config("app.conf");  // Parsed once.
```

**Memoizing a member function across many objects.** The object is part of the
key, so different objects get different cache entries automatically (the class
must be hashable and equality comparable, as shown earlier):

```cpp
auto area = automemoize(&Polygon::area);
area(triangle);  // computed
area(triangle);  // cached
area(square);    // computed (different key)
```

**Building a recursive memoized function once, reused everywhere.** Use the
macro for a single shared cache, or the combinator for a value you can pass
around and give independent caches:

```cpp
// Shared, define-once:
AUTOMEMOIZED(int64_t, catalan, (int n)) {
  if (n == 0) return 1;
  int64_t sum = 0;
  for (int i = 0; i < n; ++i) sum += catalan(i) * catalan(n - 1 - i);
  return sum;
}
```

## How it works

A short tour for the curious; you do not need any of this to use the library.

1. **Signature deduction.** A small `function_traits` trait inspects the
   callable and extracts its return type and a tuple of parameter types. It has
   specializations for function pointers, member function pointers (in all the
   `const`/`noexcept`/ref-qualified flavors), and functors — for a functor it
   reads `&T::operator()` directly rather than going through `std::function`,
   which is what allows move-only callables and C++23 static call operators.
   Anything it cannot deduce (a generic lambda, an overloaded operator) leaves
   the trait empty, which makes the public constraint fail cleanly.

2. **Key type.** The parameter tuple is decayed
   (`std::tuple<const std::string&>` becomes `std::tuple<std::string>`) to form
   the type stored in an `absl::flat_hash_map`.

3. **Transparent lookup.** The map uses transparent hash and equality functors
   built on `absl::HashOf`, so a tuple of _references_ to your arguments hashes
   identically to the stored tuple of values. That is the mechanism behind
   zero-copy hits: on the fast path the library looks the key up by reference
   and only materializes a stored key on a miss.

4. **One shared core.** All three entry points funnel into a single
   `lookup_or_compute` routine that handles the fast path, the conversion path,
   the irreflexive-key guard, and the re-entrancy-safe insertion order.

The internals live in `namespace automemoize_internal`; only `automemoize`,
`automemoize_recursive`, and `AUTOMEMOIZED` are part of the public surface.

## Comparison with Python

If you reach for `functools.lru_cache` / `functools.cache` in Python, here is
the mapping:

| Python                             | Automemoize                                    |
| ---------------------------------- | ---------------------------------------------- |
| `@functools.cache` on a function   | `AUTOMEMOIZED(R, name, (params)) { ... }`      |
| `@lru_cache(maxsize=None)`         | Same — the cache is unbounded.                 |
| Decorating an existing function    | `auto g = automemoize(f);`                     |
| Recursive memoization "just works" | Use `AUTOMEMOIZED` or `automemoize_recursive`. |
| Bounded LRU (`maxsize=N`)          | Not provided; the cache never evicts.          |

The most important difference is the last row: Automemoize caches are
**unbounded**. There is no eviction, so a memoizer that sees unboundedly many
distinct arguments will grow without limit. Scope your memoizers accordingly,
or clear them by letting the wrapper go out of scope.

## FAQ

**Does a cache hit copy my arguments?** No — not when the argument types match
the key types exactly. The lookup is done through references. Only a miss
constructs a stored key.

**Can I clear the cache?** There is no `clear()` method. A wrapper from
`automemoize` owns its cache, so destroying the wrapper frees it; create a new
one to start fresh. An `AUTOMEMOIZED` function's cache lives for the life of
the process.

**Is it thread-safe?** No; it is thread-compatible. See
[Semantics in detail](#semantics-in-detail) for a synchronized-wrapper
pattern.

**Why was my callable rejected?** See
[What gets rejected, and why](#what-gets-rejected-and-why). The most common
cause is a generic (`auto`) lambda parameter, which has no single signature to
deduce.

**What about a function returning `void`?** It is rejected — there is nothing
to cache. Memoization only makes sense for value-returning, pure functions.

## Building and testing this repository

The project builds with [Bazel](https://bazel.build/) (see
[`.bazelversion`](.bazelversion) for the pinned version).

```bash
# Build everything.
bazel build //...

# Run the unit and property tests.
bazel test //...

# Run the benchmark with optimizations.
bazel run -c opt //:automemoize_benchmark

# Check BUILD-file formatting.
bazel run //tools:buildifier_check
```

The test suite spans three targets: the example-driven unit tests
([`automemoize_test.cc`](automemoize_test.cc)), randomized property tests that
check result-equivalence and cache-hit counting across thousands of generated
call sequences ([`automemoize_property_test.cc`](automemoize_property_test.cc)),
and BUILD-file linting. Continuous integration builds and tests under both
C++20 and C++23.

A fuzz test ([`automemoize_fuzz_test.cc`](automemoize_fuzz_test.cc)) mirrors the
property tests for use with [FuzzTest](https://github.com/google/fuzztest); it
is currently disabled in the build because FuzzTest's dependency graph is not
yet compatible with Bazel 9 (see the note in [`BUILD`](BUILD)).
