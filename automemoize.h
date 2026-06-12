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
//  - Each memoizer owns its own cache; copying the memoizer copies the
//    cache. f may re-enter its own memoizer (recursive memoization).
//  - automemoize is thread-compatible: concurrent calls on the same
//    instance require external synchronization.

namespace automemoize_internal {

// Extracted callable signature: return type plus parameter tuple.
template <typename R, typename... Args>
struct signature {
  using return_type = R;
  using args_tuple = std::tuple<Args...>;
};

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

template <typename F>
concept memoizable =
    requires {
      typename function_traits<F>::return_type;
      typename function_traits<F>::args_tuple;
    } && !std::is_void_v<typename function_traits<F>::return_type> &&
    !std::is_reference_v<typename function_traits<F>::return_type> &&
    std::copy_constructible<typename function_traits<F>::return_type> &&
    params_memoizable<typename function_traits<F>::args_tuple>::value &&
    requires(const cache_key_t<F>& key) {
      { absl::Hash<cache_key_t<F>>{}(key) } -> std::convertible_to<std::size_t>;
    } && tuple_elements_equality_comparable<cache_key_t<F>>::value;

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
    if constexpr (automemoize_internal::query_matches_key<
                      KeyTuple, decltype(args)...>::value) {
      // Fast path: argument types match the key exactly, so look up through
      // a tuple of references — no copies on a cache hit.
      const auto query = std::tie(std::as_const(args)...);
      if (auto it = cache.find(query); it != cache.end()) {
        return it->second;
      }
      // Copy the arguments into the stored key BEFORE invoking f, so the
      // key cannot be affected by the call; then forward the originals to f
      // with their original value categories (rvalues are moved into f, not
      // into the key).
      KeyTuple key(args...);
      // Invoke f before touching the map: f may re-enter this memoizer
      // (e.g. recursive memoization through a self-reference), so no
      // iterator can be held across the call.
      ReturnType result = std::invoke(f, std::forward<decltype(args)>(args)...);
      return cache.emplace(std::move(key), std::move(result)).first->second;
    } else {
      // Conversion path: some argument requires conversion to its stored
      // type (e.g. a string literal for a std::string parameter). Convert
      // once into the key, then look up and invoke from the key.
      KeyTuple key(std::forward<decltype(args)>(args)...);
      if (auto it = cache.find(key); it != cache.end()) {
        return it->second;
      }
      ReturnType result = std::apply(f, key);
      return cache.emplace(std::move(key), std::move(result)).first->second;
    }
  };
}

#endif  // AUTOMEMOIZE_H_
