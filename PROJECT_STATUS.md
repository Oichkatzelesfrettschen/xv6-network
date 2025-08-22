## Repository Cleanup

- Added `.note.GNU-stack` to all assembly sources to avoid linker warnings and
  enforce non-executable stacks.
- Updated `Makefile` to pass `-Wa,--noexecstack` and `-z noexecstack`.
- Linker warnings for RWX segments silenced via --no-warn-rwx-segments.
- Added patterns for build artifacts to `.gitignore`.
- No build directories were present at cleanup time.
- Removed `xv6-rev5.tar.gz` and added `*.tar.gz` to `.gitignore` to avoid
  committing archived sources.
- Added `LICENSE` file with BSD 3-Clause license text to document project licensing.
