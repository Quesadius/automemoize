load("@buildifier_prebuilt//:rules.bzl", "buildifier", "buildifier_test")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

buildifier(
    name = "buildifier.check",
    exclude_patterns = [
        "./.git/*",
    ],
    lint_mode = "warn",
    lint_warnings = [
        "-cc-native",
    ],
    mode = "diff",
)

buildifier_test(
    name = "buildifier.test",
    srcs = ["BUILD"],
    lint_mode = "warn",
)

cc_library(
    name = "automemoize",
    srcs = ["automemoize.h"],
    copts = ["-std=c++20"],
    deps = ["@absl//absl/container:flat_hash_map"],
)

cc_test(
    name = "automemoize_test",
    srcs = ["automemoize_test.cc"],
    deps = [
        ":automemoize",
        "@googletest//:gtest_main",
    ],
)

cc_binary(
    name = "automemoize_benchmark",
    srcs = ["automemoize_microbenchmark.cc"],
    deps = [
        ":automemoize",
        "@google_benchmark//:benchmark_main",
    ],
)

cc_test(
    name = "automemoize_fuzz_test",
    srcs = ["automemoize_fuzz_test.cc"],
    deps = [
        ":automemoize",
        "@fuzztest//fuzztest",
        "@fuzztest//fuzztest:fuzztest_gtest_main",
    ],
)
