
### Project Status: Repository Cleanup

This document outlines recent changes to the repository's build system and project structure to improve clarity, security, and maintainability.

#### Build System Enhancements
* **Compiler Flags**: Enforced stricter compilation diagnostics by adding `-Wextra` and `-Wpedantic` to **`CFLAGS`** in the `Makefile`. This helps catch subtle issues and ensures compliance with strict ISO standards.
* **Stack Protection**: Ensured all assembly sources emit the `.note.GNU-stack` section to avoid linker warnings and enforce a non-executable stack. This was done by updating the `Makefile` to pass the **`-Wa,--noexecstack`** and **`-z noexecstack`** flags.
* **Linker Warnings**: Silenced linker warnings for RWX segments by adding **`--no-warn-rwx-segments`** to `LDFLAGS`.

#### Repository Housekeeping
* **Git Ignore**: Updated **`.gitignore`** to include patterns for all build artifacts, ensuring that only source code and project files are tracked. A pattern for `*.tar.gz` was also added to prevent the accidental commitment of archived sources.
* **Licensing**: Added a **`LICENSE`** file with the BSD 3-Clause license text to clearly document the project's licensing.
* **File Removal**: Removed the `xv6-rev5.tar.gz` archive, as it is no longer needed in the repository.
* No build directories were present at the time of cleanup.