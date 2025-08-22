#!/usr/bin/env bash
#
# build.sh - Profile-driven build system for xv6.
#
# This script is a wrapper around the xv6 Makefile that provides a
# user-friendly way to build the kernel with different optimization
# and debugging settings. It supports distinct profiles (developer,
# performance, release) by injecting specific CFLAGS into the build
# process. Build artifacts are neatly organized into profile-specific
# directories.
#
# Usage:
#   ./build.sh [--help] <PROFILE> [make-args]
#
# The script is designed to be self-documenting, with clear explanations
# of each step to promote reproducibility and understanding.

set -euo pipefail

# ---
# Help and Usage
# ---

show_help() {
  cat <<'USAGE'
Usage: ./build.sh [--help] PROFILE [make-args]

Compile the xv6 kernel and associated utilities according to the specified PROFILE.

Profiles:
  developer    Unoptimized build with debug information. Ideal for debugging.
  performance  Optimized build for profiling and performance testing.
  release      Fully optimized, stripped binaries for distribution.

Options:
  -h, --help   Display this help message and exit.

Additional make arguments can be provided after the profile name and will be
forwarded directly to the underlying `make` command.
USAGE
}

# ---
# Argument Parsing
# ---

if [[ $# -eq 0 ]]; then
  echo "Error: missing build profile." >&2
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
    echo "Error: unknown profile '$1'." >&2
    show_help
    exit 1
    ;;
esac

# ---
# Configuration
# ---

# Baseline flags for all builds, ensuring essential compiler options are
# always present. These are consistent with the project's core Makefile.
base_cflags=(
  -fno-pic -static -fno-builtin -fno-strict-aliasing
  -Wall -Wextra -Wpedantic -MD -m32 -Werror -fno-stack-protector -Wa,--noexecstack
)

# Profile-specific compiler flags.
case "$profile" in
  developer)
    profile_cflags=(-O0 -g -fno-omit-frame-pointer -DDEBUG)
    ;;
  performance)
    profile_cflags=(-O3 -g -fomit-frame-pointer -march=native -mtune=native -pipe)
    ;;
  release)
    profile_cflags=(-O3 -DNDEBUG -s -fomit-frame-pointer -march=native -mtune=native)
    ;;
  *)
    echo "Internal error: Unhandled profile '$profile'." >&2
    exit 1
    ;;
esac

# Consolidate base and profile-specific flags into a single string.
# This string will be passed to `make` to override the default CFLAGS.
cflags=("${base_cflags[@]}" "${profile_cflags[@]}")

# Determine the output directory for this build profile.
out_dir="build/${profile}"
mkdir -p "${out_dir}"

# ---
# Build Execution
# ---

echo "[xv6] Building with '${profile}' profile..." >&2

# Start with a clean slate to prevent stale object files from interfering.
# The redirection to /dev/null silences the output.
make clean >/dev/null

# Invoke the main Makefile, passing the combined CFLAGS and any
# additional user-provided arguments.
make CFLAGS="${cflags[*]}" "$@"

# ---
# Post-Build Artifact Handling
# ---

echo "[xv6] Moving build artifacts to '${out_dir}'..." >&2

# A list of key build products to be collected.
artefacts=(
  bootblock bootother kernel kernelmemfs xv6.img xv6memfs.img fs.img
  kernel.asm kernel.sym kernelmemfs.asm kernelmemfs.sym mkfs
)

# Move each artifact to the designated output directory.
# The -e check ensures that we only try to move existing files.
for a in "${artefacts[@]}"; do
  if [[ -e "$a" ]]; then
    mv "$a" "${out_dir}/"
  fi
done

# Perform a final clean-up to remove intermediate object files,
# keeping the main source directory clean.
make clean >/dev/null

echo "[xv6] Build completed. Artifacts are located in '${out_dir}'." >&2