"""Repository rule that wraps system Perl for use as a Bazel toolchain.

This allows builds on platforms where the relocatable-perl binaries from
rules_perl don't work (e.g., Alpine Linux/musl where glibc binaries fail).

The repo is always fetched (even on macOS) but the toolchain is only selected
on Linux due to exec_compatible_with constraints. If perl is not found,
the repo creates an empty filegroup so it doesn't break non-Linux builds.
"""

def _find_xsubpp(repository_ctx):
    """Find xsubpp, which may not be in PATH on some distros (e.g., Alpine)."""
    xsubpp = repository_ctx.which("xsubpp")
    if xsubpp:
        return xsubpp

    # Ask perl where its library files are and look for xsubpp there
    perl = repository_ctx.which("perl")
    if perl:
        result = repository_ctx.execute([perl, "-MConfig", "-e", "print $Config{privlibexp}"])
        if result.return_code == 0:
            privlib = result.stdout.strip()
            candidate = repository_ctx.path(privlib + "/ExtUtils/xsubpp")
            if candidate.exists:
                return candidate

    return None

def _system_perl_impl(repository_ctx):
    perl = repository_ctx.which("perl")

    if perl:
        xsubpp = _find_xsubpp(repository_ctx)
        repository_ctx.symlink(perl, "bin/perl")
        if xsubpp:
            repository_ctx.symlink(xsubpp, "bin/xsubpp")
        else:
            # Create a stub xsubpp (only the interpreter is needed for OpenSSL asm generation)
            repository_ctx.file("bin/xsubpp", "#!/usr/bin/env perl\ndie 'xsubpp stub: not available';\n", executable = True)
    else:
        repository_ctx.file("bin/.empty", "")

    repository_ctx.file("BUILD.bazel", """\
filegroup(
    name = "runtime",
    srcs = glob(["bin/*"]),
    visibility = ["//visibility:public"],
)
""")

system_perl = repository_rule(
    implementation = _system_perl_impl,
    local = True,
    doc = "Wraps system Perl installation for use as a Bazel toolchain.",
)
