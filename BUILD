load("@buildifier_prebuilt//:rules.bzl", "buildifier", "buildifier_test")
load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

buildifier(
    name = "buildifier_check",
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
    name = "buildifier_test",
    srcs = ["BUILD"],
    lint_mode = "warn",
)

cc_library(
    name = "automemoize",
    hdrs = ["automemoize.h"],
    deps = [
        "@absl//absl/container:flat_hash_map",
        "@absl//absl/hash",
    ],
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

# TODO: Re-enable when fuzztest is compatible with Bazel 9. fuzztest pulls in a
# massive number of transitive dependencies (e.g. riegeli, snappy, highwayhash,
# lz4, brotli, zstd, boringssl, rules_go, etc.). Many of the versions pinned in
# the Bazel Central Registry for those transitive libraries still rely on
# Bazel's native cc_library rule, which was completely removed in Bazel 9 in
# favor of @rules_cc//cc:defs.bzl.
# cc_test(
#     name = "automemoize_fuzz_test",
#     srcs = ["automemoize_fuzz_test.cc"],
#     deps = [
#         ":automemoize",
#         "@fuzztest//fuzztest",
#         "@fuzztest//fuzztest:fuzztest_gtest_main",
#     ],
# )
