# Building and Instrumenting `xv6-network`

This guide replaces the former `build.sh` and documents the modern LLVM/Ninja
workflow for the educational xv6-network kernel.  It enumerates required tools,
explains their roles, and shows how to compile the kernel using Ninja as the
single entry point.

## 1. Toolchain and Analysis Utilities

Install the complete suite of compilers, linkers, emulators and analysis tools:

```bash
sudo apt update
sudo apt install -y clang lld llvm ninja-build \
                    clang-format clang-tidy bear cppcheck \
                    qemu-system-x86 gdb
```

* **clang / clang-tidy / clang-format** – C and assembly compilation plus static
  analysis and formatting.
* **lld** – Linker producing ELF i386 images for the kernel and boot code.
* **llvm / bear / cppcheck** – Instrumentation helpers generating compile
  commands (`bear`) and performing additional static analysis (`cppcheck`).
* **ninja-build** – High‑performance build driver used as the project's main
  interface.
* **qemu-system-x86** – Emulator for running the resulting kernel image.
* **gdb** – Interactive debugger for kernel or user programs.
The Ninja entry point ultimately drives the legacy Makefile, which still relies on GCC and binutils; the LLVM toolchain above is available for analysis and future migration.

## 2. Build Profiles

Three tuned profiles mirror the historical `build.sh` script:

| Profile      | Flags                                                                   |
|--------------|-------------------------------------------------------------------------|
| `developer`  | `-O0 -g3 -ggdb -fno-omit-frame-pointer -DDEBUG`                          |
| `performance`| `-O3 -g -fno-omit-frame-pointer -DNDEBUG`                                |
| `release`    | `-O3 -s -fomit-frame-pointer -DNDEBUG`                                   |

Baseline flags shared across profiles:
```
-fno-pic -static -fno-builtin -fno-strict-aliasing -Wall -MD -m32 -Werror \
-fno-stack-protector -Wa,--noexecstack
```

## 3. Building

Invoke Ninja with the desired profile (the default target is `developer`):

```bash
ninja            # developer build
ninja performance
ninja release
```

Artifacts such as `kernel`, `xv6.img`, and assorted `.asm` and `.sym` files are
produced in the repository root.  `ninja clean` resets the tree.

## 4. Emulation

Execute the kernel image under QEMU:

```bash
qemu-system-i386 -drive file=xv6.img,format=raw,index=0,if=floppy, -serial mon:stdio
```

## 5. Further Instrumentation

* Run `bear -- ninja` to generate `compile_commands.json` for IDE integration.
* Use `cppcheck` and `clang-tidy` for deeper static diagnostics.
* Profile or trace execution with GDB and QEMU's built‑in tracing facilities.


## 6. Tool Installation Reference

The project relies on a broad set of analysis and instrumentation utilities.  The
table below summarises the canonical installation channels endorsed by each
project's authoritative source (Debian archives, the Python Package Index, the
Node package registry, or upstream source repositories).

```
+-------------------+--------------------+--------------------------------------------------+
| Tool              | Installation Method| Example Command                                  |
+-------------------+--------------------+--------------------------------------------------+
| lizard            | pip                | pip install lizard                               |
| cloc              | apt                | sudo apt install cloc                            |
| cscope            | apt                | sudo apt install cscope                          |
| diffoscope        | pip                | pip install diffoscope                           |
| dtrace            | Source/Build       | git clone https://github.com/dtrace4linux/linux.git; \
|                   |                    | cd linux; make; sudo make install                |
| valgrind          | apt                | sudo apt install valgrind                        |
| cppcheck          | apt                | sudo apt install cppcheck                        |
| sloccount         | apt                | sudo apt install sloccount                       |
| flawfinder        | apt                | sudo apt install flawfinder                      |
| gdb               | apt                | sudo apt install gdb                             |
| pylint            | pip                | pip install pylint                               |
| flake8            | pip                | pip install flake8                               |
| mypy              | pip                | pip install mypy                                 |
| semgrep           | pip                | pip install semgrep                              |
| eslint            | npm                | npm install -g eslint                            |
| jshint            | npm                | npm install -g jshint                            |
| jscpd             | npm                | npm install -g jscpd                             |
| nyc               | npm                | npm install -g nyc                               |
+-------------------+--------------------+--------------------------------------------------+
```

*DTrace has no Debian or PyPI package; the Linux port must be compiled from the
source repository shown above.*
