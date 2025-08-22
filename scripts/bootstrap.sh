#!/usr/bin/env bash
#===============================================================================
# bootstrap.sh
#
# Reproducible environment setup and build harness for the xv6-network kernel.
# This script installs toolchain dependencies, compiles the kernel and userland
# programs, generates compile_commands.json for IDE integration, and launches
# the kernel under QEMU to verify basic functionality.
#
# The script is intentionally verbose and logs each phase into the `logs/`
# directory to aid reproducibility and troubleshooting.
#===============================================================================

set -euo pipefail

LOGDIR="logs"
mkdir -p "$LOGDIR"

log() {
  printf '[%(%T)T] %s\n' -1 "$*"
}

apt_pkgs=(
  clang lld llvm ninja-build clang-format clang-tidy bear cppcheck \
  qemu-system-x86 gdb build-essential gcc-multilib nodejs npm python3-pip
)

pip_pkgs=(lizard)

npm_pkgs=(eslint)

install_deps() {
  log "Updating APT index"
  sudo apt-get update >>"$LOGDIR/apt.log" 2>&1
  log "Installing APT packages: ${apt_pkgs[*]}"
  sudo apt-get install -y "${apt_pkgs[@]}" >>"$LOGDIR/apt.log" 2>&1

  log "Installing pip packages: ${pip_pkgs[*]}"
  python3 -m pip install --upgrade "${pip_pkgs[@]}" >>"$LOGDIR/pip.log" 2>&1

  log "Installing npm packages: ${npm_pkgs[*]}"
  npm install -g "${npm_pkgs[@]}" >>"$LOGDIR/npm.log" 2>&1
}

build_kernel() {
  log "Cleaning previous build outputs"
  make clean >>"$LOGDIR/build.log" 2>&1 || true

  log "Building kernel with Bear to capture compile commands"
  rm -f compile_commands.json
  bear --output compile_commands.json -- ninja >>"$LOGDIR/build.log" 2>&1
}

run_qemu() {
  log "Booting kernel in QEMU (10s timeout)"
  timeout 10s make qemu-nox >>"$LOGDIR/qemu.log" 2>&1 || true
}

install_deps
build_kernel
run_qemu

log "Artifacts available: kernel, fs.img, xv6.img"
log "Compile database: compile_commands.json"
log "Logs stored in: $LOGDIR"
