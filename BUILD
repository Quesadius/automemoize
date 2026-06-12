load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library", "cc_test")

# The buildifier tooling lives in //tools so that this package only loads
# from non-dev dependencies: consumers of @automemoize load this file, and
# dev_dependency repos (like buildifier_prebuilt) do not exist for them.

exports_files(
    ["BUILD"],
    visibility = ["//tools:__pkg__"],
)

cc_library(
    name = "automemoize",
    hdrs = ["automemoize.h"],
    visibility = ["//visibility:public"],
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
