#ifndef AUTOMEMOIZE_H_
#define AUTOMEMOIZE_H_

#include <concepts>
#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/container/flat_hash_map.h"
#include "absl/hash/hash.h"

// Automemoize
//
// automemoize(f) wraps a callable in a caching layer: the first call with a
// given set of arguments invokes f and stores the result; subsequent calls
// with equal arguments return the cached result without invoking f.
//
// Example:
//
//   auto memoized_add = automemoize([](int a, int b) { return a + b; });
//   EXPECT_EQ(memoized_add(1, 2), 3);
//   EXPECT_EQ(memoized_add(1, 2), 3);  // Cached result
//
// Supported callables: function pointers, member function pointers (the
// object becomes the leading argument), lambdas, and functors with a single
// non-template operator() — including move-only ones.
//
// Requirements, enforced at compile time:
//  - The decayed argument types must be hashable with absl::Hash and
//    equality comparable.
//  - The return type must be a copy-constructible object type (not void,
//    not a reference).
//  - Parameters must be by-value or const lvalue references. Non-const
//    lvalue references are out-parameters whose writes would not be
//    replayed on cache hits, and rvalue-reference parameters cannot be
//    re-invoked from a stored key.
//
// Semantics and performance:
//  - Arguments are stored in the cache by value (references are decayed).
//  - A call whose decayed argument types exactly match the stored key types
//    performs no argument copies on a cache hit, and rvalue arguments are
//    consumed only on a miss. Arguments needing conversion (e.g. a string
//    literal for a std::string parameter) are converted before lookup.
//  - Results are returned by value (a copy of the cached result).
//  - Keys are matched with operator==, so an argument value that does not
//    compare equal to itself (e.g. floating-point NaN) is never cached:
//    such calls invoke f every time and do not grow the cache.
//  - Each memoizer owns its own cache; copying the memoizer copies the
//    cache. f may re-enter its own memoizer (recursive memoization).
//  - automemoize is thread-compatible: concurrent calls on the same
//    instance require external synchronization.
//
// For memoizing recursive computations, see automemoize_recursive below.
// For defining a memoized function in one step (akin to Python's
// @functools.cache decorator), see the AUTOMEMOIZED macro below.

namespace automemoize_internal {

// Extracted callable signature: return type plus parameter tuple.
template <typename R, typename... Args>
struct signature {
  using return_type = R;
  using args_tuple = std::tuple<Args...>;
};

// Traits for an explicitly spelled function type R(Args...). The primary
// template is empty so that non-function types fail constraints cleanly.
template <typename Sig>
struct signature_traits {};

template <typename R, typename... Args>
struct signature_traits<R(Args...)> : signature<R, Args...> {};

template <typename R, typename... Args>
struct signature_traits<R(Args...) noexcept> : signature<R, Args...> {};

// Signature of a member function pointer type, EXCLUDING the object
// parameter. Used to deduce the signature of a functor's operator().
// The primary template is empty so that unsupported callables fail the
// automemoize constraint cleanly. (&&-qualified call operators are omitted:
// they cannot be invoked on the stored callable.)
template <typename T>
struct memfn_signature {};

template <typename C, typename R, typename... Args>
struct memfn_signature<R (C::*)(Args...)> : signature<R, Args...> {};

template <typename C, typename R, typename... Args>
struct memfn_signature<R (C::*)(Args...) const> : signature<R, Args...> {};

template <typename C, typename R, typename... Args>
struct memfn_signature<R (C::*)(Args...) noexcept> : signature<R, Args...> {};

template <typename C, typename R, typename... Args>
struct memfn_signature<R (C::*)(Args...) const noexcept>
    : signature<R, Args...> {};

template <typename C, typename R, typename... Args>
struct memfn_signature<R (C::*)(Args...) &> : signature<R, Args...> {};

template <typename C, typename R, typename... Args>
struct memfn_signature<R (C::*)(Args...) const&> : signature<R, Args...> {};

template <typename C, typename R, typename... Args>
struct memfn_signature<R (C::*)(Args...) & noexcept> : signature<R, Args...> {};

template <typename C, typename R, typename... Args>
struct memfn_signature<R (C::*)(Args...) const & noexcept>
    : signature<R, Args...> {};

// function_traits deduces {return_type, args_tuple} for a callable. The
// primary template is empty so that unsupported callables (e.g. generic
// lambdas or overloaded functors, where no signature can be deduced) fail
// the automemoize constraint cleanly instead of triggering a hard error.
template <typename T, typename = void>
struct function_traits {};

// Lambdas and functors with a single non-template operator(). Deducing from
// &T::operator() directly (rather than std::function CTAD) supports
// move-only callables and avoids instantiating std::function.
template <typename T>
struct function_traits<T, std::void_t<decltype(&T::operator())>>
    : memfn_signature<decltype(&T::operator())> {};

// Function pointers. (A function passed by value decays to a pointer.)
template <typename R, typename... Args>
struct function_traits<R (*)(Args...), void> : signature<R, Args...> {};

template <typename R, typename... Args>
struct function_traits<R (*)(Args...) noexcept, void> : signature<R, Args...> {
};

// Member function pointers: the object becomes the leading argument,
// stored by value in the key.
template <typename C, typename R, typename... Args>
struct function_traits<R (C::*)(Args...), void> : signature<R, C, Args...> {};

template <typename C, typename R, typename... Args>
struct function_traits<R (C::*)(Args...) const, void>
    : signature<R, C, Args...> {};

template <typename C, typename R, typename... Args>
struct function_traits<R (C::*)(Args...) noexcept, void>
    : signature<R, C, Args...> {};

template <typename C, typename R, typename... Args>
struct function_traits<R (C::*)(Args...) const noexcept, void>
    : signature<R, C, Args...> {};

// Decay tuple types
// Converts tuple<const int&, string&> -> tuple<int, string>
// This is crucial so the cache stores VALUES, not references.
template <typename T>
struct decay_tuple;

template <typename... Args>
struct decay_tuple<std::tuple<Args...>> {
  using type = std::tuple<std::decay_t<Args>...>;
};

template <typename T>
using decay_tuple_t = typename decay_tuple<T>::type;

// The cache key type actually stored in the map: the function's argument
// types with references and cv-qualifiers stripped.
template <typename F>
using cache_key_t = decay_tuple_t<typename function_traits<F>::args_tuple>;

// std::tuple's operator== is not necessarily constrained at declaration
// level, so a tuple-level `key == key` check can falsely succeed for
// non-comparable elements. Check element-wise instead.
template <typename T>
struct tuple_elements_equality_comparable;

template <typename... Ts>
struct tuple_elements_equality_comparable<std::tuple<Ts...>>
    : std::bool_constant<(std::equality_comparable<Ts> && ...)> {};

// Parameters must be by-value or const lvalue references; see the file
// comment.
template <typename T>
struct params_memoizable;

template <typename... Args>
struct params_memoizable<std::tuple<Args...>>
    : std::bool_constant<((!std::is_rvalue_reference_v<Args> &&
                           (!std::is_lvalue_reference_v<Args> ||
                            std::is_const_v<std::remove_reference_t<Args>>)) &&
                          ...)> {};

// True when the decayed caller argument types exactly match the key's
// element types, enabling the no-copy lookup fast path.
template <typename Tuple, typename... Query>
struct query_matches_key : std::false_type {};

template <typename... Elems, typename... Query>
  requires(sizeof...(Elems) == sizeof...(Query))
struct query_matches_key<std::tuple<Elems...>, Query...>
    : std::bool_constant<(std::is_same_v<Elems, std::decay_t<Query>> && ...)> {
};

// Transparent hash/equality over key tuples. Hashing goes through
// absl::HashOf on the elements, which makes a tuple of references hash
// identically to a tuple of the referenced values — so lookups can use a
// reference tuple and avoid copying the arguments.
struct TupleHash {
  using is_transparent = void;

  template <typename Tuple>
  std::size_t operator()(const Tuple& t) const {
    return std::apply(
        [](const auto&... elems) { return absl::HashOf(elems...); }, t);
  }
};

struct TupleEq {
  using is_transparent = void;

  template <typename T, typename U>
  bool operator()(const T& a, const U& b) const {
    return a == b;
  }
};

// Whether a {return_type, args_tuple} signature can be memoized; see the
// "Requirements" section of the file comment.
template <typename Traits>
concept memoizable_signature =
    requires {
      typename Traits::return_type;
      typename Traits::args_tuple;
    } && !std::is_void_v<typename Traits::return_type> &&
    !std::is_reference_v<typename Traits::return_type> &&
    std::copy_constructible<typename Traits::return_type> &&
    params_memoizable<typename Traits::args_tuple>::value &&
    requires(const decay_tuple_t<typename Traits::args_tuple>& key) {
      {
        absl::Hash<decay_tuple_t<typename Traits::args_tuple>>{}(key)
      } -> std::convertible_to<std::size_t>;
    } &&
    tuple_elements_equality_comparable<
        decay_tuple_t<typename Traits::args_tuple>>::value;

template <typename F>
concept memoizable = memoizable_signature<function_traits<F>>;

// Shared memoization core: look up the key built from `args` in `cache`,
// invoking `invoke` to compute the result on a miss. `invoke` must be
// callable both with the original arguments (fast path) and with lvalue
// references to the stored key elements (conversion path).
template <typename KeyTuple, typename ReturnType, typename Cache,
          typename Invoke, typename... CallArgs>
ReturnType lookup_or_compute(Cache& cache, Invoke&& invoke,
                             CallArgs&&... args) {
  if constexpr (query_matches_key<KeyTuple, CallArgs...>::value) {
    // Fast path: argument types match the key exactly, so look up through
    // a tuple of references — no copies on a cache hit.
    const auto query = std::tie(std::as_const(args)...);
    if (auto it = cache.find(query); it != cache.end()) {
      return it->second;
    }
    // Copy the arguments into the stored key BEFORE invoking, so the key
    // cannot be affected by the call; then forward the originals with
    // their original value categories (rvalues are moved into the call,
    // not into the key).
    KeyTuple key(args...);
    // Invoke before touching the map: the computation may re-enter this
    // memoizer (recursive memoization), so no iterator can be held across
    // the call.
    ReturnType result = std::invoke(std::forward<Invoke>(invoke),
                                    std::forward<CallArgs>(args)...);
    // A key that does not compare equal to itself (e.g. contains NaN) can
    // never be found again and violates the hash map's key contract, so
    // such calls are passed through uncached.
    if (!(key == key)) {
      return result;
    }
    return cache.emplace(std::move(key), std::move(result)).first->second;
  } else {
    // Conversion path: some argument requires conversion to its stored
    // type (e.g. a string literal for a std::string parameter). Convert
    // once into the key, then look up and invoke from the key.
    KeyTuple key(std::forward<CallArgs>(args)...);
    if (auto it = cache.find(key); it != cache.end()) {
      return it->second;
    }
    ReturnType result = std::apply(std::forward<Invoke>(invoke), key);
    // See above: never insert a key that fails self-equality.
    if (!(key == key)) {
      return result;
    }
    return cache.emplace(std::move(key), std::move(result)).first->second;
  }
}

// The memoizing wrapper returned by automemoize_recursive: invokes f with
// a reference to itself as the first argument, so recursive calls through
// that reference are cached.
template <typename Sig, typename F>
class RecursiveMemoizer;

template <typename R, typename... Args, typename F>
class RecursiveMemoizer<R(Args...), F> {
 public:
  explicit RecursiveMemoizer(F f) : f_(std::move(f)) {}

  template <typename... CallArgs>
  R operator()(CallArgs&&... args) {
    return lookup_or_compute<KeyTuple, R>(
        cache_,
        [this](auto&&... inner) -> R {
          return std::invoke(f_, *this,
                             std::forward<decltype(inner)>(inner)...);
        },
        std::forward<CallArgs>(args)...);
  }

 private:
  using KeyTuple = std::tuple<std::decay_t<Args>...>;

  F f_;
  absl::flat_hash_map<KeyTuple, R, TupleHash, TupleEq> cache_;
};

// Whether F can serve as the callable of RecursiveMemoizer<Sig, F>: it must
// accept the memoizer itself as the leading argument, followed by the
// signature's arguments, and return the signature's return type.
template <typename R, typename F, typename Self, typename ArgsTuple>
struct is_recursively_invocable : std::false_type {};

template <typename R, typename F, typename Self, typename... Args>
struct is_recursively_invocable<R, F, Self, std::tuple<Args...>>
    : std::bool_constant<
          std::is_invocable_r_v<R, F&, Self&, const std::decay_t<Args>&...>> {};

template <typename Sig, typename F>
concept recursively_memoizable =
    memoizable_signature<signature_traits<Sig>> &&
    is_recursively_invocable<typename signature_traits<Sig>::return_type, F,
                             RecursiveMemoizer<Sig, F>,
                             typename signature_traits<Sig>::args_tuple>::value;

}  // namespace automemoize_internal

template <typename F>
  requires automemoize_internal::memoizable<F>
auto automemoize(F f) {
  using ReturnType =
      typename automemoize_internal::function_traits<F>::return_type;
  using KeyTuple = automemoize_internal::cache_key_t<F>;
  using Cache =
      absl::flat_hash_map<KeyTuple, ReturnType, automemoize_internal::TupleHash,
                          automemoize_internal::TupleEq>;

  return [f = std::move(f),
          cache = Cache()](auto&&... args) mutable -> ReturnType {
    return automemoize_internal::lookup_or_compute<KeyTuple, ReturnType>(
        cache, f, std::forward<decltype(args)>(args)...);
  };
}

// automemoize_recursive<R(Args...)>(f)
//
// Memoizes a recursive computation. f receives the memoizer itself as its
// first argument, so recursive calls made through it are cached:
//
//   auto fib = automemoize_recursive<int64_t(int)>(
//       [](auto& self, int n) -> int64_t {
//         if (n <= 1) return n;
//         return self(n - 1) + self(n - 2);
//       });
//   fib(90);  // Computed in linear time; hits the cache thereafter.
//
// The signature must be spelled explicitly: a callable taking `auto& self`
// is a template, so its argument types cannot be deduced. The signature's
// arguments follow the same rules as automemoize.
template <typename Sig, typename F>
  requires automemoize_internal::recursively_memoizable<Sig, F>
auto automemoize_recursive(F f) {
  return automemoize_internal::RecursiveMemoizer<Sig, F>(std::move(f));
}

// AUTOMEMOIZED(ReturnType, name, (params)) { body }
//
// Defines a memoized function in one step, analogous to Python's
// @functools.cache decorator. Calls to `name` anywhere — including
// recursive calls inside the body itself — go through the cache:
//
//   AUTOMEMOIZED(int64_t, fib, (int n)) {
//     if (n <= 1) return n;
//     return fib(n - 1) + fib(n - 2);  // Recursive calls are cached.
//   }
//
// Unlike automemoize (which gives each wrapper instance its own cache),
// this defines ONE function with ONE shared, lazily initialized, unbounded
// cache, like @functools.cache. The cache is thread-compatible, NOT
// thread-safe: synchronize externally if `name` is called concurrently.
//
// Use at namespace scope only. The parameter list must not use default
// arguments (it is spelled twice in the expansion), and ReturnType must
// not contain a bare comma (alias such types first).
#define AUTOMEMOIZED(ReturnType, name, params)                  \
  inline ReturnType name##_automemoized_impl params;            \
  struct name##_automemoized_t {                                \
    static auto& memoizer() {                                   \
      static auto memo = automemoize(name##_automemoized_impl); \
      return memo;                                              \
    }                                                           \
    template <typename... Args>                                 \
    ReturnType operator()(Args&&... args) const {               \
      return memoizer()(std::forward<Args>(args)...);           \
    }                                                           \
  };                                                            \
  inline constexpr name##_automemoized_t name{};                \
  inline ReturnType name##_automemoized_impl params

#endif  // AUTOMEMOIZE_H_
