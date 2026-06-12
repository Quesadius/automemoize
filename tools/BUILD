load("@buildifier_prebuilt//:rules.bzl", "buildifier", "buildifier_test")

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
    srcs = [
        "BUILD",
        "//:BUILD",
    ],
    lint_mode = "warn",
)
