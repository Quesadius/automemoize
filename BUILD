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

cc_test(
    name = "automemoize_property_test",
    srcs = ["automemoize_property_test.cc"],
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

# TODO: Re-enable when fuzztest is compatible with Bazel 9. Verified still
# broken as of fuzztest 20260219.0 on Bazel 9.0.1 (2026-06): the chain
# fuzztest -> flatbuffers -> aspect_rules_esbuild -> aspect_bazel_lib fails
# because even the newest aspect_bazel_lib in the Bazel Central Registry
# (2.9.4) uses incompatible_use_toolchain_transition, which Bazel 9 removed
# (rules_swift and rules_go pins also need single_version_overrides to get
# that far). Until upstream publishes Bazel-9-compatible releases, the fuzz
# target cannot build.
# cc_test(
#     name = "automemoize_fuzz_test",
#     srcs = ["automemoize_fuzz_test.cc"],
#     deps = [
#         ":automemoize",
#         "@fuzztest//fuzztest",
#         "@fuzztest//fuzztest:fuzztest_gtest_main",
#     ],
# )
