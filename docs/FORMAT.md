# Formatting Guide

## Tooling
- **clang-format** is the single source of truth. Use the repo's default style (LLVM fallback) unless a `.clang-format` is introduced.
- Preferred invocation: `make format` (runs `clang-format -i` on all `.c`/`.h` files under `src`, `include`, `tests`, `tools`).
- One-off: `find src include tests tools -type f \( -name '*.c' -o -name '*.h' \) -print0 | xargs -0 clang-format -i`.

## Scope
- Format all C sources and headers in `src/`, `include/`, `tests/`, and `tools/`.
- Do **not** run clang-format on docs, scripts, or generated artifacts.

## Style Notes
- 4-space indentation; no tabs in code.
- Keep lines reasonably short (~100 cols); clang-format will wrap as needed.
- Brace style: K&R/LLVM default (function opening brace on same line).
- Pointer qualifiers hug the type (`Type* ptr`), consistent with clang-format defaults.
- Trailing commas allowed in enums/initializer lists when clang-format emits them.

## Pre-commit Checklist
- Run `make format` after code changes.
- Ensure `make test` (or CI) passes to catch format-induced diffs in golden files/tests.
