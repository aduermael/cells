#!/usr/bin/env sh
# Install the cells CLI for agent use: Homebrew first, then direct release install.
# Used by the cells skill when `cells` is not on PATH.
set -eu

repo="${CELLS_REPO:-aduermael/cells}"
brew_formula="${CELLS_BREW_FORMULA:-aduermael/tap/cells}"

if [ -n "${CELLS_INSTALL_URL:-}" ]; then
  install_url="$CELLS_INSTALL_URL"
else
  install_url="https://raw.githubusercontent.com/$repo/main/install.sh"
fi

have() {
  command -v "$1" >/dev/null 2>&1
}

try_brew() {
  if ! have brew; then
    return 1
  fi
  echo "Installing cells with Homebrew..."
  brew install "$brew_formula"
}

try_direct() {
  if ! have curl; then
    echo "Missing curl; cannot run direct installer." >&2
    return 1
  fi
  echo "Installing cells with the direct installer..."
  curl -fsSL "$install_url" | sh
}

if have cells && [ "${CELLS_FORCE_INSTALL:-0}" != "1" ]; then
  echo "cells is already installed: $(command -v cells)"
  exit 0
fi

if try_brew; then
  exit 0
fi

try_direct
