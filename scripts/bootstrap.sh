#!/usr/bin/env bash
#
# bootstrap.sh - Bootstrap script for xv6-network development environment
#
# This script installs required toolchains, builds the project with Bear
# to generate compile_commands.json for IDE integration, and can optionally
# run QEMU for testing.
#
# Usage:
#   ./scripts/bootstrap.sh [--install] [--build] [--run-qemu] [--help]
#
# Options:
#   --install     Install required toolchains and dependencies
#   --build       Build project with Bear to generate compile_commands.json
#   --run-qemu    Run QEMU emulator after building
#   --help        Show this help message
#
# If no options are provided, all steps (install, build, run-qemu) are executed.

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

show_help() {
    cat <<'USAGE'
Usage: ./scripts/bootstrap.sh [--install] [--build] [--run-qemu] [--help]

Bootstrap script for xv6-network development environment.

Options:
  --install     Install required toolchains and dependencies
  --build       Build project with Bear to generate compile_commands.json
  --run-qemu    Run QEMU emulator after building
  --help        Show this help message

If no options are provided, all steps (install, build, run-qemu) are executed.

The script will:
1. Install development tools including clang, ninja, bear, qemu-system-x86
2. Build the project using Bear to generate compile_commands.json for IDE tooling
3. Optionally run QEMU to test the kernel

Generated files:
- compile_commands.json: For IDE integration (clangd, VS Code, etc.)
- kernel: Kernel binary
- xv6.img: Bootable disk image
- fs.img: File system image
- mkfs: File system creation utility
USAGE
}

# Install required packages
install_toolchains() {
    print_info "Installing required toolchains and dependencies..."
    
    # Update package lists
    print_info "Updating package lists..."
    sudo apt-get update >/tmp/apt-update.log 2>&1 || {
        print_error "Failed to update package lists"
        tail -n 20 /tmp/apt-update.log
        exit 1
    }
    tail -n 20 /tmp/apt-update.log
    
    # Install QEMU
    print_info "Installing QEMU system emulator..."
    sudo apt-get install -y qemu-system-x86 >/tmp/apt-qemu.log 2>&1 || {
        print_error "Failed to install QEMU"
        tail -n 20 /tmp/apt-qemu.log
        exit 1
    }
    tail -n 20 /tmp/apt-qemu.log
    
    # Install development tools
    print_info "Installing development tools..."
    sudo apt-get install -y \
        clang lld llvm ninja-build \
        clang-format clang-tidy bear cppcheck \
        gdb build-essential \
        >/tmp/apt-dev.log 2>&1 || {
        print_error "Failed to install development tools"
        tail -n 20 /tmp/apt-dev.log
        exit 1
    }
    
    print_info "All toolchains installed successfully!"
}

# Build project with Bear
build_project() {
    print_info "Building project with Bear to generate compile_commands.json..."
    
    # Clean first
    print_info "Cleaning previous build artifacts..."
    make clean >/dev/null 2>&1 || true
    
    # Build with Bear to generate compile_commands.json
    print_info "Building with Bear..."
    if command -v bear >/dev/null 2>&1; then
        bear -- make >/tmp/build.log 2>&1 || {
            print_error "Build failed"
            tail -n 20 /tmp/build.log
            exit 1
        }
        print_info "compile_commands.json generated successfully!"
    else
        print_warning "Bear not available, building without compile database..."
        make >/tmp/build.log 2>&1 || {
            print_error "Build failed"
            tail -n 20 /tmp/build.log
            exit 1
        }
    fi
    
    tail -n 20 /tmp/build.log
    print_info "Build completed successfully!"
}

# Run QEMU emulator
run_qemu() {
    print_info "Starting QEMU emulator (will timeout after 10 seconds)..."
    print_info "Press Ctrl+A then X to exit QEMU manually"
    
    # Run QEMU with timeout
    timeout 10s make qemu-nox >/tmp/qemu.log 2>&1 || {
        print_info "QEMU session completed (timeout or manual exit)"
    }
    
    # Show QEMU output
    if [ -f /tmp/qemu.log ]; then
        print_info "QEMU output:"
        tail -n 20 /tmp/qemu.log | strings
    fi
}

# Main function
main() {
    local do_install=false
    local do_build=false
    local do_run_qemu=false
    local show_help_flag=false
    
    # Parse command line arguments
    if [ $# -eq 0 ]; then
        # No arguments - do everything
        do_install=true
        do_build=true
        do_run_qemu=true
    else
        while [ $# -gt 0 ]; do
            case "$1" in
                --install)
                    do_install=true
                    ;;
                --build)
                    do_build=true
                    ;;
                --run-qemu)
                    do_run_qemu=true
                    ;;
                --help)
                    show_help_flag=true
                    ;;
                *)
                    print_error "Unknown option: $1"
                    echo "Use --help for usage information"
                    exit 1
                    ;;
            esac
            shift
        done
    fi
    
    if [ "$show_help_flag" = true ]; then
        show_help
        exit 0
    fi
    
    print_info "Starting xv6-network bootstrap..."
    
    if [ "$do_install" = true ]; then
        install_toolchains
    fi
    
    if [ "$do_build" = true ]; then
        build_project
    fi
    
    if [ "$do_run_qemu" = true ]; then
        run_qemu
    fi
    
    print_info "Bootstrap completed successfully!"
    
    if [ -f compile_commands.json ]; then
        print_info "IDE integration ready - compile_commands.json is available"
    fi
}

# Run main function with all arguments
main "$@"