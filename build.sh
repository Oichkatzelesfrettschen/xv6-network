#!/usr/bin/env bash
#
# build.sh - A profile-aware build orchestrator for the xv6-network kernel.
#
# Usage: ./build.sh [--help] <profile>
#
# Profiles:
#   developer    Produce a debuggable build with minimal optimizations.
#   performance  Build with aggressive optimizations while retaining symbols.
#   release      Emit a stripped, highly optimized build for distribution.
#
# The script cleans previous build artifacts before invoking GNU Make with
# profile-specific compiler flags.  The flags replicate the baseline options
# from the project's Makefile and adjust optimization, debugging, and
# instrumentation according to the selected profile.
#
# This script exemplifies modern shell practices: it is robust to errors,
# documents its intent academically, and favors explicitness for clarity.

set -euo pipefail

show_usage() {
    cat <<'USAGE'
Usage: ./build.sh [--help] <profile>

Profiles:
  developer    Build with debug information and no optimization.
  performance  Optimize for runtime performance yet retain symbols.
  release      Optimize and strip symbols for distribution.

The build is performed in the repository root and mirrors the Makefile's
baseline compilation flags while tailoring them per profile.
USAGE
}

if [[ $# -ne 1 ]]; then
    show_usage
    exit 1
fi

case "$1" in
    --help|-h)
        show_usage
        exit 0
        ;;
    developer)
        PROFILE_FLAGS="-O0 -g3 -ggdb -fno-omit-frame-pointer -DDEBUG"
        ;;
    performance)
        PROFILE_FLAGS="-O3 -g -fno-omit-frame-pointer -DNDEBUG"
        ;;
    release)
        PROFILE_FLAGS="-O3 -s -fomit-frame-pointer -DNDEBUG"
        ;;
    *)
        echo "[build.sh] Unknown profile: $1" >&2
        show_usage
        exit 1
        ;;
 esac

# Baseline flags derived from the project's Makefile.
BASE_FLAGS="-fno-pic -static -fno-builtin -fno-strict-aliasing -Wall -MD -m32 -Werror -fno-stack-protector -Wa,--noexecstack"

CFLAGS_COMBINED="$BASE_FLAGS $PROFILE_FLAGS"

echo "[build.sh] Cleaning previous build artifacts..."
make clean

echo "[build.sh] Invoking make with profile '$1'"
make CFLAGS="$CFLAGS_COMBINED"
