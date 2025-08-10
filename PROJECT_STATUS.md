## Repository Cleanup

- Added `.note.GNU-stack` to all assembly sources to avoid linker warnings and
  enforce non-executable stacks.
- Updated `Makefile` to pass `-Wa,--noexecstack` and `-z noexecstack`.
- Linker warnings for RWX segments silenced via --no-warn-rwx-segments.
- Added patterns for build artifacts to `.gitignore`.
- No build directories were present at cleanup time.

- Added `LICENSE` file with BSD 3-Clause license text to document project licensing.
