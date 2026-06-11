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
// Automemoize is a simple memoization library for C++.
// It memoizes the result of a function call based on its arguments.
// The result is cached and returned on subsequent calls with the same
// arguments.
//
// Example:
//
//   auto memoized_add = automemoize([](int a, int b) { return a + b; });
//   EXPECT_EQ(memoized_add(1, 2), 3);
//   EXPECT_EQ(memoized_add(1, 2), 3);  // Cached result
//
//  automemoize is thread-compatible.

namespace internal {
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

// Helper to extract types from std::function
template <typename T>
struct std_function_traits;

template <typename R, typename... Args>
struct std_function_traits<std::function<R(Args...)>> {
  using return_type = R;
  using args_tuple = std::tuple<Args...>;
};

// Primary template is empty so that unsupported callables (e.g. generic
// lambdas or overloaded functors, where no signature can be deduced) fail the
// automemoize constraint cleanly instead of triggering a hard error during
// instantiation.
template <typename T, typename = void>
struct function_traits {};

// Uses std::function CTAD to deduce the signature. This handles lambdas,
// functors, and function pointers, stripping const/noexcept/etc.
template <typename T>
struct function_traits<T,
                       std::void_t<decltype(std::function{std::declval<T>()})>>
    : std_function_traits<decltype(std::function{std::declval<T>()})> {};

// Specialization for const member function pointers (raw)
template <typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType (ClassType::*)(Args...) const, void> {
  using return_type = ReturnType;
  using args_tuple = std::tuple<ClassType, Args...>;
};

// Specialization for non-const member function pointers (raw)
template <typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType (ClassType::*)(Args...), void> {
  using return_type = ReturnType;
  using args_tuple = std::tuple<ClassType, Args...>;
};

// Specialization for noexcept const member function pointers (raw)
template <typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType (ClassType::*)(Args...) const noexcept,
                       void> {
  using return_type = ReturnType;
  using args_tuple = std::tuple<ClassType, Args...>;
};

// Specialization for noexcept non-const member function pointers (raw)
template <typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType (ClassType::*)(Args...) noexcept, void> {
  using return_type = ReturnType;
  using args_tuple = std::tuple<ClassType, Args...>;
};

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
}  // namespace internal

template <typename F>
  requires requires {
    typename internal::function_traits<F>::args_tuple;
    // The cache hashes and compares the *decayed* key tuple, so validate
    // exactly that type.
    requires requires(const internal::cache_key_t<F>& key) {
      {
        absl::Hash<internal::cache_key_t<F>>{}(key)
      } -> std::convertible_to<std::size_t>;
    };
    requires internal::tuple_elements_equality_comparable<
        internal::cache_key_t<F>>::value;
  }
auto automemoize(F f) {
  using Traits = internal::function_traits<F>;
  using ReturnType = typename Traits::return_type;

  // Decay the tuple types.
  // If function takes (const string&), we must store (string) in the map.
  using ArgsTuple = internal::cache_key_t<F>;

  return [f = std::move(f),
          cache = absl::flat_hash_map<ArgsTuple, ReturnType>()](
             auto&&... args) mutable -> ReturnType {
    // Construct the key by forwarding.
    // If args are rvalues, they are MOVED into 'key'.
    // 'args' are now potentially empty/moved-from.
    ArgsTuple key(std::forward<decltype(args)>(args)...);

    auto it = cache.find(key);
    if (it == cache.end()) {
      // Invoke f before touching the map: f may re-enter this memoizer
      // (e.g. recursive memoization through a self-reference), so no
      // iterator can be held across the call.
      ReturnType result = std::apply(f, key);
      it = cache.emplace(std::move(key), std::move(result)).first;
    }
    return it->second;
  };
}

#endif  // AUTOMEMOIZE_H_
