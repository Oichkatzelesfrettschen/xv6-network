#!/usr/bin/env bash
#
# build.sh - profile-driven wrapper around the xv6 build system.
#
# This script orchestrates compilation of the xv6 operating system using
# pre-defined optimisation profiles.  Each profile augments the default
# Makefile settings with additional CFLAGS tuned for specific scenarios.
#
# Usage:
#   ./build.sh [--help] PROFILE [make-args]
#
# Profiles:
#   developer    Debug friendly build with no optimisation and full symbols.
#   performance  Optimised build suitable for iterative performance testing.
#   release      Maximum optimisation and stripped binaries for distribution.
#
# Any additional arguments following the profile are passed directly to
# 'make', permitting fine-grained control (e.g., specifying alternative
# targets).  Build artefacts are collected under build/PROFILE/.
#
# The script is intentionally academic and heavily annotated to elucidate
# each step of the build pipeline, aligning with best practices in
# reproducible systems research.

set -euo pipefail

show_help() {
  cat <<'USAGE'
Usage: ./build.sh [--help] PROFILE [make-args]

Compile the xv6 kernel and associated utilities according to PROFILE.

Profiles:
  developer    Enable debug information, disable optimisations.
  performance  Optimise for typical development and profiling cycles.
  release      Produce fully optimised, stripped binaries for deployment.

Options:
  -h, --help   Display this help and exit.

Additional make arguments may be supplied after PROFILE and are forwarded
verbatim to the underlying make invocation.
USAGE
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
if [[ $# -eq 0 ]]; then
  echo "Error: missing build profile" >&2
  show_help
  exit 1
fi

case "$1" in
  -h|--help)
    show_help
    exit 0
    ;;
  developer|performance|release)
    profile="$1"
    shift
    ;;
  *)
    echo "Error: unknown profile '$1'" >&2
    show_help
    exit 1
    ;;
esac

# Baseline compilation flags mirrored from the project's Makefile.  When the
# CFLAGS variable is overridden on the command line these provide a stable
# foundation across profiles.
base_cflags=(
  -fno-pic -static -fno-builtin -fno-strict-aliasing
  -Wall -MD -m32 -Werror -fno-stack-protector -Wa,--noexecstack
)

# Profile-specific compiler flags.
case "$profile" in
  developer)
    profile_cflags=(-O0 -g -fno-omit-frame-pointer -DDEBUG)
    ;;
  performance)
    profile_cflags=(-O2 -g -fomit-frame-pointer -march=native -mtune=native -pipe)
    ;;
  release)
    profile_cflags=(-O3 -DNDEBUG -s -fomit-frame-pointer -march=native -mtune=native)
    ;;
  *)
    echo "Unhandled profile: $profile" >&2
    exit 1
    ;;
esac

# Consolidate base and profile-specific flags.
cflags=("${base_cflags[@]}" "${profile_cflags[@]}")

# Directory to stash resultant artefacts for the chosen profile.
out_dir="build/${profile}"
mkdir -p "${out_dir}"

# ---------------------------------------------------------------------------
# Build sequence
# ---------------------------------------------------------------------------
# Ensure a pristine starting point, invoke make with appended CFLAGS, then
# relocate key build products into the profile-specific directory.  A final
# clean removes transient object files, leaving the repository tree tidy.

echo "[xv6] Building profile '${profile}'..." >&2
make clean >/dev/null
make CFLAGS="${cflags[*]}" "$@"

# Collect representative artefacts.  The list may be extended as the project
# evolves; absent files are skipped gracefully.
artefacts=(
  bootblock bootother kernel kernelmemfs xv6.img xv6memfs.img fs.img \
  kernel.asm kernel.sym kernelmemfs.asm kernelmemfs.sym mkfs
)
for a in "${artefacts[@]}"; do
  if [[ -e "$a" ]]; then
    mv "$a" "${out_dir}/"
  fi
done

# Remove intermediary compilation products to maintain a clean work tree.
make clean >/dev/null

echo "[xv6] Build artefacts stored in '${out_dir}'." >&2

# End of script
