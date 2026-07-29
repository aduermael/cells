#!/usr/bin/env sh
# Install the cells agent skill into Codex / Claude / Grok skill directories.
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/aduermael/cells/main/install-skill.sh | sh
#   sh install-skill.sh --codex --claude --grok
#   sh install-skill.sh --install-cli
set -eu

repo="${CELLS_REPO:-aduermael/cells}"
ref="${CELLS_REF:-main}"
skill_name="${CELLS_SKILL_NAME:-cells}"
targets="${CELLS_SKILL_TARGETS:-}"
mode="install"
install_cli="${CELLS_INSTALL_CLI:-0}"

# Relative paths under skill/ that make up the installed package (single source of truth).
# Keep in sync when adding skill assets.
SKILL_PACKAGE_FILES="
SKILL.md
install.sh
SCRIPTING.md
samples/set-values.luau
samples/read-print.luau
samples/transform.luau
"

if [ -n "${CELLS_SKILL_BASE_URL:-}" ]; then
  base_url="$CELLS_SKILL_BASE_URL"
else
  base_url="https://raw.githubusercontent.com/$repo/$ref/skill"
fi

err() {
  printf '%s\n' "$*" >&2
}

have() {
  command -v "$1" >/dev/null 2>&1
}

usage() {
  cat <<'USAGE'
Usage: install-skill.sh [--codex] [--claude] [--grok] [--all]
       install-skill.sh --auto-update-existing

Options:
  --codex                 Install .agents/skills/cells.
  --claude                Install .claude/skills/cells.
  --grok                  Install .grok/skills/cells.
  --all                   Install all default agent targets.
  --target <path|name>    Install one named target or custom skill directory.
  --name <name>           Use a skill folder name other than cells.
  --auto-update-existing  Update only existing cells skill folders; create nothing.
  --install-cli           Run the bundled skill/install.sh after installing the skill.
USAGE
}

add_target() {
  targets="${targets:+$targets }$1"
}

target_path() {
  case "$1" in
    codex) printf '.agents/skills/%s\n' "$skill_name" ;;
    claude) printf '.claude/skills/%s\n' "$skill_name" ;;
    grok) printf '.grok/skills/%s\n' "$skill_name" ;;
    *) printf '%s\n' "$1" ;;
  esac
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --codex) add_target codex ;;
    --claude) add_target claude ;;
    --grok) add_target grok ;;
    --all) targets="codex claude grok" ;;
    --target)
      shift
      if [ "$#" -eq 0 ]; then
        err "missing value after --target"
        exit 1
      fi
      add_target "$1"
      ;;
    --target=*) add_target "${1#*=}" ;;
    --name)
      shift
      if [ "$#" -eq 0 ]; then
        err "missing value after --name"
        exit 1
      fi
      skill_name="$1"
      ;;
    --name=*) skill_name="${1#*=}" ;;
    --auto-update-existing) mode="update-existing" ;;
    --install-cli) install_cli="1" ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      err "unknown option $1"
      usage >&2
      exit 1
      ;;
  esac
  shift
done

if [ -z "$targets" ]; then
  targets="codex claude grok"
fi

tmpdir="$(mktemp -d "${TMPDIR:-/tmp}/cells-skill-install.XXXXXX")"
cleanup() {
  rm -rf "$tmpdir"
}
trap cleanup EXIT INT HUP TERM

package_dir="$tmpdir/package"
mkdir -p "$package_dir"

script_dir=""
case "$0" in
  */*) script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd) ;;
esac

stage_local_package() {
  # Copy the whole skill/ tree so new assets ship without per-file curl lists
  # when installing from a checkout. Still iterate SKILL_PACKAGE_FILES for
  # remote installs and change detection.
  cp -a "$script_dir/skill/." "$package_dir/"
}

stage_remote_package() {
  if ! have curl; then
    err "Missing required command: curl"
    exit 1
  fi
  for rel in $SKILL_PACKAGE_FILES; do
    dest_file="$package_dir/$rel"
    mkdir -p "$(dirname "$dest_file")"
    curl -fsSL "$base_url/$rel" -o "$dest_file"
  done
}

if [ -n "$script_dir" ] && [ -d "$script_dir/skill" ] && [ -f "$script_dir/skill/SKILL.md" ]; then
  stage_local_package
else
  stage_remote_package
fi

if [ ! -f "$package_dir/install.sh" ]; then
  err "Skill package missing install.sh"
  exit 1
fi
chmod 0755 "$package_dir/install.sh"

installed_paths=""
updated_paths=""
unchanged_paths=""
first_path=""

install_file() {
  # $1 = relative path within package
  rel="$1"
  src="$package_dir/$rel"
  dst="$dest/$rel"
  if [ ! -f "$src" ]; then
    return 0
  fi
  mkdir -p "$(dirname "$dst")"
  if ! cmp -s "$src" "$dst" 2>/dev/null; then
    cp "$src" "$dst"
    changed="1"
  fi
  if [ "$rel" = "install.sh" ] && [ ! -x "$dst" ]; then
    chmod 0755 "$dst"
    changed="1"
  fi
}

for target in $targets; do
  dest=$(target_path "$target")
  if [ "$mode" = "update-existing" ] && [ ! -f "$dest/SKILL.md" ]; then
    continue
  fi

  parent=$(dirname "$dest")
  mkdir -p "$parent"

  if [ -L "$dest" ] && [ ! -d "$dest" ]; then
    rm "$dest"
  fi
  if [ -e "$dest" ] && [ ! -d "$dest" ]; then
    err "Cannot install skill at $dest because a non-directory file already exists there."
    exit 1
  fi

  had_skill="0"
  if [ -f "$dest/SKILL.md" ]; then
    had_skill="1"
  fi

  mkdir -p "$dest"
  changed="0"

  for rel in $SKILL_PACKAGE_FILES; do
    install_file "$rel"
  done

  # Ensure install.sh is executable even if already byte-identical
  if [ -f "$dest/install.sh" ] && [ ! -x "$dest/install.sh" ]; then
    chmod 0755 "$dest/install.sh"
    changed="1"
  fi

  [ -n "$first_path" ] || first_path="$dest"

  if [ "$changed" = "0" ]; then
    unchanged_paths="${unchanged_paths} $dest"
  elif [ "$had_skill" = "1" ]; then
    updated_paths="${updated_paths} $dest"
  else
    installed_paths="${installed_paths} $dest"
  fi
done

if [ -n "$installed_paths" ]; then
  printf 'Installed cells skill:%s\n' "$installed_paths"
fi
if [ -n "$updated_paths" ]; then
  printf 'Updated cells skill:%s\n' "$updated_paths"
fi
if [ -n "$unchanged_paths" ]; then
  printf 'cells skill already up to date:%s\n' "$unchanged_paths"
fi
if [ "$mode" = "update-existing" ] && [ -z "$first_path" ]; then
  printf 'No existing cells skill folders found; nothing installed.\n'
fi

if [ "$install_cli" = "1" ] && [ -n "$first_path" ]; then
  sh "$first_path/install.sh"
elif [ -n "$first_path" ]; then
  printf 'The skill will use %s/install.sh if the cells command is not available.\n' "$first_path"
fi
