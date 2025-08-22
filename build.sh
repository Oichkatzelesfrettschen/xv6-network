#!/bin/bash
#
# build.sh - Configurable build script for xv6-network
#
# This script acts as a thin wrapper around the repository's Makefile.
# It exposes a user-friendly interface for selecting build profiles that
# tune compilation flags for different development scenarios.
#
# Usage:
#   ./build.sh --profile=<developer|performance|release>
#
# Profiles:
#   developer   - Unoptimized build with maximal debug information and
#                 debug-friendly defines. Suitable for day-to-day kernel
#                 hacking and introspection under debuggers.
#   performance - Aggressively optimized build aimed at benchmarking on
#                 the local machine. Prioritizes speed over debuggability.
#   release     - Production-style build with optimizations and assertions
#                 disabled. Generates smaller binaries intended for
#                 distribution or deployment.
#
# When invoked with no arguments or an unsupported profile, the script
# prints this help text.

set -euo pipefail

# Display usage information and exit.
usage() {
	cat <<USAGE
Usage: $0 --profile=developer|performance|release
USAGE
	exit 1
}

# Ensure a profile was supplied.
[[ $# -eq 0 ]] && usage

case "$1" in
--profile=developer)
	# Debugging build: disable optimizations, keep symbols, add DEBUG define
	CFLAGS_PROFILE="-O0 -g3 -fno-omit-frame-pointer -DDEBUG"
	;;
--profile=performance)
	# High-performance build: enable LTO and target local architecture
	CFLAGS_PROFILE="-O3 -march=native -flto -fomit-frame-pointer"
	;;
--profile=release)
	# Release build: moderate optimization, strip symbols, disable asserts
	CFLAGS_PROFILE="-O2 -DNDEBUG -s"
	;;
*)
	usage
	;;
esac

# Always start from a clean tree to avoid stale objects with wrong flags.
make clean >/dev/null

# Invoke make with the chosen profile flags. We append to existing CFLAGS
# from the Makefile to preserve essential options like -m32 and -Wall.
make -j"$(nproc)" "CFLAGS+=${CFLAGS_PROFILE}"
