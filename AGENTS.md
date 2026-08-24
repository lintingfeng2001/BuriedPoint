# Repository Guidelines

## Project Structure & Module Organization

The public C API lives in `include/buried.h`; implementation code is under `src/`. Keep features within the existing modules: `common/`, `context/`, `crypt/`, `database/`, and `report/`. `examples/` contains small client programs, while `tests/` contains the project-owned GoogleTest suite. `server/` is a Rust mock HTTP receiver for local reporting tests. Dependencies in `src/third_party/` and the vendored `googletest/` tree should only change during intentional dependency upgrades. Treat `bin/` as prebuilt artifacts, not source.

## Build, Test, and Development Commands

Development targets Windows, Visual Studio 2022 (v143), CMake 3.20+, and x64.

- `py scripts/build.py` clears `build/`, configures Debug, and builds both library variants.
- `py scripts/build.py --test --example` also builds `buried_test` and all examples.
- `cmake --build build/x64-Debug --config Debug --parallel 8` rebuilds an already configured tree.
- `build\x64-Debug\tests\Debug\buried_test.exe` runs project tests directly; CTest is not registered.
- `cargo run --manifest-path server/Cargo.toml` starts the local receiver on port `5678`.

## Coding Style & Naming Conventions

Match nearby code: two-space indentation for C/C++, four spaces for Rust, opening braces on the declaration line, and ordered system-before-project includes. C++ targets C++20 and C targets C11. Use `PascalCase` for types and public methods, `snake_case` for variables and fields, and a trailing underscore for private fields/helpers (`logger_`, `InitLogger_`). Public C functions use the `Buried_Action` pattern. No repository formatter is configured; keep diffs focused and run `cargo fmt --manifest-path server/Cargo.toml` for Rust changes.

## Testing Guidelines

Add focused GoogleTest cases in `tests/test_<area>.cc`, using descriptive suites and cases such as `CommonServiceTest.BasicTest`. Register new test files in `tests/CMakeLists.txt`. Exercise success and failure paths, especially persistence, encryption, and HTTP behavior. There is no stated coverage threshold; regression coverage is expected for fixes.

## Commit & Pull Request Guidelines

This checkout has no accessible Git history, so use concise, imperative subjects such as `Fix retry handling in HTTP reporter`. Keep commits scoped and exclude generated build output. Pull requests should explain behavior changes, reference an issue when available, list verification commands, and include logs or request/response samples for reporting changes. Call out public API or configuration changes explicitly; screenshots are only useful when observable output changes.

## Security & Configuration

Never commit credentials, real user identifiers, or captured production payloads. Use synthetic values in tests and examples. Do not hand-edit generated build artifacts or files produced from `src/buried_config.h.in`.
