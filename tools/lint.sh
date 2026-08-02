#!/bin/bash
# Run clang-tidy linter on C++ source files
#
# Usage:
#   bazel run :lint          # Lint all .cc files
#   bazel run :lint -- --fix # Lint and auto-fix issues
#   bazel run :lint -- FILE...  # Lint specific files

set -euo pipefail
source "${BUILD_WORKSPACE_DIRECTORY:-}/tools/guard.sh"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
NC='\033[0m' # No Color

# Check if clang-tidy is installed
if ! command -v clang-tidy &> /dev/null; then
    echo -e "${RED}Error: clang-tidy not found${NC}"
    echo "Install with: brew install llvm"
    echo "  and add to PATH: export PATH=\"\$(brew --prefix llvm)/bin:\$PATH\""
    exit 1
fi

# Check for compile_commands.json
COMPILE_DB="$REPO_ROOT/compile_commands.json"
if [ ! -f "$COMPILE_DB" ]; then
    echo -e "${YELLOW}Warning: compile_commands.json not found${NC}"
    echo "Generate it with: bazel run @hedron_compile_commands//:refresh_all"
    echo "Proceeding without compilation database..."
    COMPILE_DB=""
fi

# Get Bazel external directory for third-party includes
# Must cd to REPO_ROOT first so bazel info works correctly
cd "$REPO_ROOT"
BAZEL_OUTPUT_BASE=$(bazel info output_base 2>/dev/null || bazelisk info output_base 2>/dev/null || echo "")

# Locate pugixml / Luau headers under Bazel's external tree.
# On a clean CI checkout these repos are not present until something fetches them.
#
# Bazel 9 materializes BCR modules (e.g. pugixml+) as junctions/symlinks into
# the repo contents cache. GNU find does not follow those by default, so we
# check known paths first and only fall back to find -L.
find_third_party_includes() {
    PUGIXML_INCLUDE=""
    LUAU_INCLUDE=""
    if [ -z "$BAZEL_OUTPUT_BASE" ]; then
        return
    fi
    local external="$BAZEL_OUTPUT_BASE/external"
    local pugixml_dir=""
    local candidate

    for candidate in \
        "$external/pugixml+/pugixml.hpp" \
        "$external/pugixml~/pugixml.hpp" \
        "$external/pugixml/pugixml.hpp"; do
        if [ -f "$candidate" ]; then
            pugixml_dir=$(dirname "$candidate")
            break
        fi
    done

    # Follow symlinks/junctions so content-cache repos are searchable.
    if [ -z "$pugixml_dir" ] && [ -d "$external" ]; then
        pugixml_dir=$(find -L "$external" -name "pugixml.hpp" -type f 2>/dev/null \
            | grep -v openxlsx | head -1 | xargs dirname 2>/dev/null || echo "")
    fi
    if [ -n "$pugixml_dir" ]; then
        PUGIXML_INCLUDE="-I$pugixml_dir"
    fi

    # Find Luau headers (VM, Compiler, Ast, Analysis, Config)
    local luau_vm_inc=""
    for candidate in \
        "$external/+git_repository+luau/VM/include" \
        "$external/luau+/VM/include" \
        "$external/luau~/VM/include" \
        "$external/luau/VM/include"; do
        if [ -d "$candidate" ]; then
            luau_vm_inc="$candidate"
            break
        fi
    done
    if [ -z "$luau_vm_inc" ] && [ -d "$external" ]; then
        luau_vm_inc=$(find -L "$external" -path "*luau*/VM/include" -type d 2>/dev/null | head -1 || echo "")
    fi
    if [ -n "$luau_vm_inc" ]; then
        local luau_dir
        luau_dir=$(dirname "$(dirname "$luau_vm_inc")")
        LUAU_INCLUDE="-I$luau_dir/VM/include -I$luau_dir/Compiler/include -I$luau_dir/Ast/include -I$luau_dir/Common/include -I$luau_dir/Analysis/include -I$luau_dir/Config/include"
    fi
}

find_third_party_includes

# If headers are missing, fetch external deps so clang-tidy can resolve includes.
# Prefer Bazel 9 --repo form; fall back to target patterns for older clients.
if [ -z "${PUGIXML_INCLUDE:-}" ] || [ -z "${LUAU_INCLUDE:-}" ]; then
    echo -e "${YELLOW}Third-party headers not found; fetching @pugixml and @luau...${NC}"
    # Fetch may fail offline; continue and report missing includes below.
    if ! bazel fetch --repo=@pugixml --repo=@luau >/dev/null 2>&1; then
        bazel fetch @pugixml//... @luau//... >/dev/null 2>&1 || true
    fi
    # Refresh output_base in case fetch populated a new path layout
    BAZEL_OUTPUT_BASE=$(bazel info output_base 2>/dev/null || bazelisk info output_base 2>/dev/null || echo "")
    find_third_party_includes
fi

if [ -z "${PUGIXML_INCLUDE:-}" ] || [ -z "${LUAU_INCLUDE:-}" ]; then
    echo -e "${YELLOW}Warning: missing third-party includes for clang-tidy:${NC}"
    [ -z "${PUGIXML_INCLUDE:-}" ] && echo "  - pugixml.hpp (build/fetch @pugixml)"
    [ -z "${LUAU_INCLUDE:-}" ] && echo "  - Luau headers (build/fetch @luau)"
fi

# Parse arguments
FIX_MODE=false
FILES=()

for arg in "$@"; do
    case $arg in
        --fix)
            FIX_MODE=true
            ;;
        *)
            FILES+=("$arg")
            ;;
    esac
done

# Find files to lint (only .cc files, not headers or test files)
# Test files (_test.cc) are excluded because they depend on external test
# framework headers (gtest) that aren't available to clang-tidy
# Native files (core/net/native/) are excluded because they depend on
# libdatachannel is pure Bazel; headers live in the external repo checkout
if [ ${#FILES[@]} -eq 0 ]; then
    while IFS= read -r -d '' file; do
        FILES+=("$file")
    done < <(find "$REPO_ROOT/core" -type f -name "*.cc" \
        ! -name "*_test.cc" \
        ! -path "*/net/native/*" \
        -print0 2>/dev/null)
fi

if [ ${#FILES[@]} -eq 0 ]; then
    echo -e "${YELLOW}No C++ source files found${NC}"
    exit 0
fi

# Build clang-tidy command
TIDY_CMD=(clang-tidy)

if [ -n "$COMPILE_DB" ]; then
    TIDY_CMD+=(-p "$REPO_ROOT")
fi

if $FIX_MODE; then
    TIDY_CMD+=(--fix)
fi

# Auto-detect parallelism
get_num_cores() {
    if command -v nproc &> /dev/null; then
        nproc
    elif command -v sysctl &> /dev/null; then
        sysctl -n hw.ncpu
    else
        echo 4
    fi
}

NPROCS=${JOBS:-$(get_num_cores)}

# Run clang-tidy
echo -e "${GREEN}Running clang-tidy on ${#FILES[@]} files with $NPROCS parallel jobs...${NC}"
echo ""

# Create temp directory for output files
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

# Build the extra args for clang-tidy
# Write args to a temp file to preserve word boundaries across subprocess calls
EXTRA_ARGS_FILE="$TMPDIR/extra_args.txt"
{
    echo "-std=c++17"
    echo "-I$REPO_ROOT"
    echo "-I$REPO_ROOT/third_party/miniz"
    if [ -n "${PUGIXML_INCLUDE:-}" ]; then
        echo "$PUGIXML_INCLUDE"
    fi
    if [ -n "${LUAU_INCLUDE:-}" ]; then
        # Split LUAU_INCLUDE by space and write each -I separately
        for arg in $LUAU_INCLUDE; do
            echo "$arg"
        done
    fi
} > "$EXTRA_ARGS_FILE"

# Function to lint a single file (exported for xargs)
lint_one_file() {
    local file="$1"
    local tmpdir="$2"
    local project_root="$3"
    local fix_mode="$4"
    local extra_args_file="$5"

    if [ ! -f "$file" ]; then
        return 0
    fi

    # Create output file named after the source file
    local outfile="$tmpdir/$(echo "$file" | tr '/' '_').out"
    local exitfile="$tmpdir/$(echo "$file" | tr '/' '_').exit"

    # Build command
    local cmd=(clang-tidy)
    if [ -f "$project_root/compile_commands.json" ]; then
        cmd+=(-p "$project_root")
    fi
    if [ "$fix_mode" = "true" ]; then
        cmd+=(--fix)
    fi

    # Read extra args from file (preserves word boundaries)
    local extra_args=()
    while IFS= read -r arg; do
        extra_args+=("$arg")
    done < "$extra_args_file"

    # Run clang-tidy
    local exit_code=0
    "${cmd[@]}" \
        --config-file="$project_root/.clang-tidy" \
        "$file" \
        -- \
        "${extra_args[@]}" \
        > "$outfile" 2>&1 || exit_code=$?

    echo "$exit_code" > "$exitfile"
}
export -f lint_one_file

# Run linting in parallel
printf '%s\n' "${FILES[@]}" | xargs -P "$NPROCS" -I {} bash -c \
    'lint_one_file "$1" "$2" "$3" "$4" "$5"' _ {} "$TMPDIR" "$REPO_ROOT" "$FIX_MODE" "$EXTRA_ARGS_FILE"

# Collect results
FAILED=0
for file in "${FILES[@]}"; do
    if [ ! -f "$file" ]; then
        continue
    fi

    local_outfile="$TMPDIR/$(echo "$file" | tr '/' '_').out"
    local_exitfile="$TMPDIR/$(echo "$file" | tr '/' '_').exit"

    if [ -f "$local_exitfile" ]; then
        exit_code=$(cat "$local_exitfile")
        if [ "$exit_code" -ne 0 ]; then
            FAILED=1
        fi
    fi

    # Only print output if there's something meaningful
    if [ -f "$local_outfile" ]; then
        filtered=$(grep -v -E "^$|warnings generated|Suppressed .* warnings|Use -header-filter|^Error while processing|^Found compiler error" "$local_outfile" 2>/dev/null || true)
        if [ -n "$filtered" ]; then
            echo -e "${YELLOW}Checking:${NC} $file"
            echo "$filtered"
        fi
    fi
done

echo ""
if [ $FAILED -ne 0 ]; then
    echo -e "${RED}Linting completed with warnings/errors${NC}"
    exit 1
else
    echo -e "${GREEN}Linting passed${NC}"
fi
