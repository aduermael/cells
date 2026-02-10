load("@rules_perl//perl:toolchain.bzl", "perl_toolchain")

perl_toolchain(
    name = "system_perl_impl",
    runtime = ["@system_perl//:runtime"],
)

toolchain(
    name = "system_perl_toolchain",
    exec_compatible_with = ["@platforms//os:linux"],
    toolchain = ":system_perl_impl",
    toolchain_type = "@rules_perl//perl:toolchain_type",
)
