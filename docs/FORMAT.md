# formatting guide

## tooling
- clang-format is the single source of truth. use the repo default style (llvm fallback) unless a `.clang-format` is added.
- preferred: `make format` (runs `clang-format -i` across `src`, `include`, `tests`, `tools`).
- one-off: `find src include tests tools -type f \( -name '*.c' -o -name '*.h' \) -print0 | xargs -0 clang-format -i`.

## scope
- format all c sources and headers in `src/`, `include/`, `tests/`, and `tools/`.
- do not run clang-format on docs, scripts, or generated artifacts.

## style notes
- 4-space indentation; no tabs in code.
- keep lines reasonably short (~100 cols); clang-format will wrap as needed.
- brace style: k&r/llvm default (function opening brace on same line).
- pointer qualifiers hug the type (`type* ptr`), consistent with clang-format defaults.
- trailing commas allowed in enums/initializer lists when clang-format emits them.

## pre-commit checklist
- run `make format` after code changes.
- ensure `make test` (or ci) passes to catch format-induced diffs in golden files/tests.
