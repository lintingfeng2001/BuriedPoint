# Repository Guidelines

## Verified Local Development Environment (Source of Truth)

This checkout came from another developer. For work on this machine, use the
verified environment below instead of inherited assumptions in upstream
documentation. These values were last verified on 2026-08-24. Re-check them
after changing the IDE or toolchain, and never record credentials or other
secrets here.

### Host and IDE

- Host: Windows 11 Home China 25H2, build `26200.9168`, x64; locale `zh-CN`
  and time zone `China Standard Time` (UTC+08:00).
- Hardware: AMD Ryzen 5 5600H with Radeon Graphics, 12 logical processors,
  and 13.9 GiB usable physical memory.
- IDE: JetBrains CLion `2025.1.3`, build `251.26927.39`.
- CLion uses its Reworked Terminal with no project-specific shell override.
  Agent-side commands run in PowerShell Core `7.6.4` with UTF-8 output.

### Active C/C++ Toolchain

- The active CLion profile is `Debug`; its build directory is
  `cmake-build-debug/`. It enables `BUILD_BURIED_TEST=ON` and
  `BUILD_BURIED_EXAMPLES=ON`.
- CLion configures with its bundled CMake `3.31.6` and generates Ninja files.
- The active compiler is MSYS2 MinGW64 GCC/G++ `16.1.0`, target
  `x86_64-w64-mingw32`:
  `D:\software\msys2\mingw64\bin\gcc.exe` and
  `D:\software\msys2\mingw64\bin\g++.exe`.
- The active build tool is Ninja `1.13.2` at
  `D:\software\msys2\mingw64\bin\ninja.exe`; GDB `17.2` is available in the
  same MSYS2 toolchain.
- The project requires CMake `3.20+`, C11, and C++20.
- `CMakePresets.json` provides the reproducible CLI profile `mingw-debug`. It
  uses the same GCC/G++ and Ninja paths, MSYS2 CMake `4.4.0`, enables tests and
  examples, and writes to `build/mingw-debug/`.
- Do not rely on ambient GCC resolution. `PATH` also contains an unrelated GCC
  `14.1.0` at `E:\Fortran\mingw64\bin`, while Ninja is not on `PATH`. Use the
  CMake preset or the explicit paths above.

### Other Installed Development Tools

- Git for Windows `2.52.0.windows.1` is available at
  `D:\software\Git\cmd\git.exe`; this checkout is an initialized Git
  repository with an `origin` remote.
- Rust `1.97.1` and Cargo `1.97.1` are installed for
  `x86_64-pc-windows-msvc`. The mock server uses Rust edition 2021.
- Visual Studio Community 2022 `17.14.38` is installed as a secondary
  toolchain. MSVC tools are `14.44.35207`; Windows SDKs `10.0.26100.0` and
  `10.0.22621.0` are under `E:\Windows Kits\10`. The active CLion profile does
  not use MSVC, and MSBuild is not on `PATH`.
- System CMake `4.4.0` is on `PATH`. CLion's bundled Ninja is `1.12.1`, but the
  active project cache uses MSYS2 Ninja `1.13.2` as described above.
- The Python launcher exists, but no Python interpreter is installed. Do not
  invoke `py scripts/build.py` unless Python is installed and verified first.
- Clang/clang-cl, cppcheck, and GitHub CLI are not currently available on
  `PATH`.

### Tool Selection Rules

- Treat CLion + MSYS2 MinGW64 as the local default. Do not switch to MSVC just
  because older project documentation mentions Visual Studio.
- For command-line builds, prefer the checked-in `mingw-debug` preset because
  it pins the compiler and Ninja paths and avoids the unrelated GCC on `PATH`.
- Treat `cmake-build-debug/`, `build/`, `.idea/`, `.codegraph/`, `.cursor/`,
  and `bin/` as local/generated state according to `.gitignore`; do not add
  them to commits.

## Project Structure & Module Organization

The public C API lives in `include/buried.h`; implementation code is under `src/`. Keep features within the existing modules: `common/`, `context/`, `crypt/`, `database/`, and `report/`. `examples/` contains small client programs, while `tests/` contains the project-owned GoogleTest suite. `server/` is a Rust mock HTTP receiver for local reporting tests. Dependencies in `src/third_party/` and the vendored `googletest/` tree should only change during intentional dependency upgrades. Treat `bin/` as prebuilt artifacts, not source.

## Build, Test, and Development Commands

- In CLion, use the enabled `Debug` CMake profile. It builds into
  `cmake-build-debug/` with MSYS2 MinGW64.
- `cmake --preset mingw-debug` configures the reproducible command-line Debug
  build with tests and examples enabled.
- `cmake --build --preset mingw-debug` builds that preset with eight jobs.
- `cmake --build cmake-build-debug --parallel 8` rebuilds the existing CLion
  cache from the command line.
- `build\mingw-debug\tests\buried_test.exe` runs project tests directly; CTest
  is not registered. Use `--gtest_list_tests` for a quick runtime/toolchain
  smoke check.
- `cargo run --manifest-path server/Cargo.toml` starts the local receiver on port `5678`.
- `scripts/build.py` describes an older MSVC-oriented flow and currently cannot
  run because Python is not installed. It is not the local default build path.

## Coding Style & Naming Conventions

Match nearby code: two-space indentation for C/C++, four spaces for Rust, opening braces on the declaration line, and ordered system-before-project includes. C++ targets C++20 and C targets C11. Use `PascalCase` for types and public methods, `snake_case` for variables and fields, and a trailing underscore for private fields/helpers (`logger_`, `InitLogger_`). Public C functions use the `Buried_Action` pattern. No repository formatter is configured; keep diffs focused and run `cargo fmt --manifest-path server/Cargo.toml` for Rust changes.

## Testing Guidelines

Add focused GoogleTest cases in `tests/test_<area>.cc`, using descriptive suites and cases such as `CommonServiceTest.BasicTest`. Register new test files in `tests/CMakeLists.txt`. Exercise success and failure paths, especially persistence, encryption, and HTTP behavior. There is no stated coverage threshold; regression coverage is expected for fixes.

## Commit & Pull Request Guidelines

Use concise, imperative subjects such as `Fix retry handling in HTTP reporter`.
Keep commits scoped and exclude generated build output. Pull requests should
explain behavior changes, reference an issue when available, list verification
commands, and include logs or request/response samples for reporting changes.
Call out public API or configuration changes explicitly; screenshots are only
useful when observable output changes.

## Security & Configuration

Never commit credentials, real user identifiers, or captured production payloads. Use synthetic values in tests and examples. Do not hand-edit generated build artifacts or files produced from `src/buried_config.h.in`.
