# Build and Run Instructions

This document enumerates the steps used to build the **xv6-network** repository and to boot the resulting kernel within a QEMU virtual machine.

## Prerequisites

The build was executed on a Debian-based system with the following toolchain components:

- Clang/LLVM utilities (`clang`, `lld`, `llvm`, `clang-format`, `clang-tidy`)
- GNU build tools (`gcc-multilib`, `ninja-build`, `make`, `gdb`)
- QEMU emulator (`qemu-system-x86`)
- Static-analysis helpers (`cppcheck`, `lizard`, `eslint`)

Install the packages and Python/Node helpers with:

```bash
sudo apt-get update
sudo apt-get install -y clang lld llvm ninja-build clang-format clang-tidy bear cppcheck qemu-system-x86 gdb build-essential gcc-multilib nodejs npm python3-pip
python3 -m pip install lizard
npm install -g eslint
```

For a one-shot reproducible setup that logs each phase and emits a
`compile_commands.json` database for IDE tooling, run the helper script:

```bash
./scripts/bootstrap.sh
```

The script performs dependency installation, rebuilds the kernel under
`bear` so that `compile_commands.json` reflects all compilation units, and
finally boots the resulting `xv6.img` inside QEMU with a short timeout.

## Build

Compile the kernel together with its user-space programs via the project’s Ninja build script:

```bash
ninja
```

This step emits the `kernel` executable, populates the `fs.img` file system image with user binaries, and assembles the bootable `xv6.img` disk.

The generated artifacts (`kernel`, `fs.img`, `xv6.img`, and the auxiliary `mkfs` tool) are excluded from version control and recreated on demand.

## Run in QEMU

Boot the freshly built image inside QEMU (headless mode) to verify a successful start-up sequence:

```bash
make qemu-nox
```

The emulator outputs the BIOS banners, loads `xv6.img`, and ultimately presents the `xv6` shell prompt after initialising devices and the network stack.
