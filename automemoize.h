
#include <concepts>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "absl/container/flat_hash_map.h"

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

// Primary template uses std::function CTAD to deduce signature
// This handles lambdas, functors, and function pointers, stripping
// const/noexcept/etc.
template <typename T>
struct function_traits {
  using std_func_type = decltype(std::function{std::declval<T>()});
  using traits = std_function_traits<std_func_type>;
  using return_type = typename traits::return_type;
  using args_tuple = typename traits::args_tuple;
};

// Specialization for const member function pointers (raw)
template <typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType (ClassType::*)(Args...) const> {
  using return_type = ReturnType;
  using args_tuple = std::tuple<ClassType, Args...>;
};

// Specialization for non-const member function pointers (raw)
template <typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType (ClassType::*)(Args...)> {
  using return_type = ReturnType;
  using args_tuple = std::tuple<ClassType, Args...>;
};

// Specialization for noexcept const member function pointers (raw)
template <typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType (ClassType::*)(Args...) const noexcept> {
  using return_type = ReturnType;
  using args_tuple = std::tuple<ClassType, Args...>;
};

// Specialization for noexcept non-const member function pointers (raw)
template <typename ClassType, typename ReturnType, typename... Args>
struct function_traits<ReturnType (ClassType::*)(Args...) noexcept> {
  using return_type = ReturnType;
  using args_tuple = std::tuple<ClassType, Args...>;
};
}  // namespace internal

template <typename F>
  requires requires {
    typename internal::function_traits<F>::args_tuple;
    requires requires(typename internal::function_traits<F>::args_tuple t) {
      {
        absl::Hash<typename internal::function_traits<F>::args_tuple>{}(t)
      } -> std::convertible_to<size_t>;
    };
  }
auto automemoize(F f) {
  using Traits = internal::function_traits<F>;
  using ReturnType = typename Traits::return_type;

  // Decay the tuple types.
  // If function takes (const string&), we must store (string) in the map.
  using ArgsTuple = internal::decay_tuple_t<typename Traits::args_tuple>;

  return [f, cache = absl::flat_hash_map<ArgsTuple, ReturnType>()](
             auto&&... args) mutable -> ReturnType {
    // Construct the key by forwarding.
    // If args are rvalues, they are MOVED into 'key'.
    // 'args' are now potentially empty/moved-from.
    ArgsTuple key(std::forward<decltype(args)>(args)...);

    auto it = cache.find(key);
    if (it != cache.end()) {
      return it->second;
    } else {
      ReturnType result = std::apply(f, key);
      cache.emplace(std::move(key), result);
      return result;
    }
  };
}
