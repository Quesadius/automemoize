cc_library(
name = "automemoize",
    srcs = ["automemoize.h"],
    deps = ["@absl//absl/container:flat_hash_map"],
    copts = ["-std=c++20"],
)

cc_test(
    name = "automemoize_test",
    srcs = ["automemoize_test.cc"],
    deps = [
        ":automemoize",
        "@googletest//:gtest_main",
    ],
    copts = ["-std=c++20"],
)

cc_binary(
    name = "automemoize_benchmark",
    srcs = ["automemoize_microbenchmark.cc"],
    deps = [
        ":automemoize",
        "@google_benchmark//:benchmark_main",
    ],
    copts = ["-std=c++20"],
)

cc_test(
    name = "automemoize_fuzz_test",
    srcs = ["automemoize_fuzz_test.cc"],
    deps = [
        ":automemoize",
        "@fuzztest//fuzztest",
        "@fuzztest//fuzztest:fuzztest_gtest_main",
    ],
    copts = ["-std=c++20"],
)
