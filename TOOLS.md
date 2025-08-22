# Tooling and Analysis Reference

This document catalogs the installation pathways and runtime footprints of the
instrumentation suite used for **xv6-network**. Each subsection records the
command executed and a brief excerpt of its output.

## Installation Commands

| Tool | Installation Method | Example Command | Notes |
|------|--------------------|-----------------|-------|
| clang / lld / llvm | apt | `sudo apt install clang lld llvm` | core LLVM toolchain |
| ninja | apt | `sudo apt install ninja-build` | primary build driver |
| clang-format | apt | `sudo apt install clang-format` | code formatting |
| clang-tidy | apt | `sudo apt install clang-tidy` | static analysis |
| bear | apt | `sudo apt install bear` | generates compile_commands.json |
| cppcheck | apt | `sudo apt install cppcheck` | C static analyzer |
| qemu-system-x86 | apt | `sudo apt install qemu-system-x86` | emulator |
| gdb | apt | `sudo apt install gdb` | debugger |
| valgrind | apt | `sudo apt install valgrind` | memory debugger |
| cloc | apt | `sudo apt install cloc` | line counts |
| cscope | apt | `sudo apt install cscope` | code navigation |
| sloccount | apt | `sudo apt install sloccount` | historical SLOC estimates |
| flawfinder | apt | `sudo apt install flawfinder` | security scan |
| nodejs / npm | apt | `sudo apt install nodejs npm` | runtime for JS tools |
| lizard | pip | `pip install lizard` | complexity metrics |
| diffoscope | pip | `pip install diffoscope` | binary/source diffing |
| pylint | pip | `pip install pylint` | Python linting |
| flake8 | pip | `pip install flake8` | Python style checks |
| mypy | pip | `pip install mypy` | Python type checks |
| semgrep | pip | `pip install semgrep` | multi-language rules engine |
| eslint | npm | `npm install -g eslint` | JavaScript linting |
| jshint | npm | `npm install -g jshint` | JavaScript linting |
| jscpd | npm | `npm install -g jscpd` | copy-paste detector |
| nyc | npm | `npm install -g nyc` | JS coverage |
| dtrace | source build | `git clone https://github.com/dtrace4linux/linux.git && cd linux && make && sudo make install` | optional tracing; not executed |
| libmagic | apt | `sudo apt install libmagic-dev` | required by diffoscope |

## Execution Snapshots

### cloc
```
$ cloc .
github.com/AlDanial/cloc v 1.98  T=0.48 s (330.2 files/s, 123691.6 lines/s)
-------------------------------------------------------------------------------
Language                     files          blank        comment           code
-------------------------------------------------------------------------------
Assembly                        29           2460            755          45425
C                               48           1018            741           6303
```

### sloccount
```
$ sloccount .
Total Physical Source Lines of Code (SLOC)                = 51,973
Development Effort Estimate, Person-Years (Person-Months) = 12.66 (151.98)
```

### lizard
```
$ lizard .
NLOC    CCN   token  PARAM  length  location
     13      3     88      2      13 checksum@4-16@./net/net.c
```

### cppcheck
```
$ cppcheck --enable=warning,style,performance,portability,unusedFunction --inline-suppr .
bootmain.c:22:10: style: The scope of the variable 'va' can be reduced. [variableScope]
console.c:33:14: style: Suspicious condition (assignment + comparison); Clarify expression with parentheses. [clarifyCondition]
```

### flawfinder
```
$ flawfinder .
Hits = 417
Lines analyzed = 9618
```

### cscope
```
$ cscope -R -b
cscope.out generated (binary database)
```

### diffoscope
```
$ diffoscope README README.textile
--- README
+++ README.textile
@@ -1,7 +1,93 @@
```

### valgrind
```
$ valgrind ls
==12868== Memcheck, a memory error detector
==12868== Command: ls
```

### gdb
```
$ gdb --version
GNU gdb (Ubuntu) 15.0.50.20240403-git
```

### pylint
```
$ pylint $(git ls-files '*.py')
No files to lint: exiting.
```

### flake8
```
$ flake8 $(git ls-files '*.py')
(no output: repository contains no Python files)
```

### mypy
```
$ mypy $(git ls-files '*.py')
mypy: error: Missing target module, package, files, or command.
```

### semgrep
```
$ semgrep --config=auto .
sh.c:140: warning: Avoid 'gets()'. This function does not consider buffer boundaries and can lead to buffer overflows.
```

### eslint
```
$ eslint .
ESLint couldn't find an eslint.config.(js|mjs|cjs) file.
```

### jshint
```
$ jshint .
(no JavaScript sources)
```

### jscpd
```
$ jscpd .
Found 5 clones.
Detection time:: 729.206ms
```

### nyc
```
$ nyc node -e "console.log('hi')"
----------|---------|----------|---------|---------|-------------------
File      | % Stmts | % Branch | % Funcs | % Lines | Uncovered Line #s
----------|---------|----------|---------|---------|-------------------
All files |       0 |        0 |       0 |       0 |
```

### clang-format
```
$ clang-format --version
clang-format version 20.1.8
```

### clang-tidy
```
$ clang-tidy --version
LLVM version 20.1.0
```

### bear
```
$ bear --version
bear 3.1.3
```

### qemu-system-x86_64
```
$ qemu-system-x86_64 --version
QEMU emulator version 8.2.2
```

### dtrace
Not executed: building dtrace requires the out-of-tree [dtrace4linux](https://github.com/dtrace4linux/linux)
source and a configured kernel.

