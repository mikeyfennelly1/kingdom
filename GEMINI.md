# C++ Development in Epic

This project is a C++20 application managed with `devbox`.

## Environment & Tools

- **C++ Standard:** C++20 (`-std=c++20`)
- **Compiler:** GCC (`g++`)
- **Build System:** CMake + Ninja
- **Task Runner:** `go-task` (use `task` commands)

## Workflows

### Building the Project

The project uses `go-task` for automation. Common tasks:

- `task build`: Compiles the project using CMake and Ninja (includes `clang-tidy` checks).
- `task run`: Builds and executes the main binary.
- `task format`: Formats code using `clang-format`.
- `task lint`: Runs `clang-tidy` static analysis.
- `task clean`: Removes build artifacts.

### Development Standards

- **Formatting:** Code must be formatted via `task format` (uses `.clang-format`).
- **Linting:** Pay attention to `clang-tidy` warnings during build; they are treated as errors.
- **Code Style:** Prefer modern C++20 features (concepts, ranges, etc.) where appropriate.
- **Naming:** Follow standard C++ conventions (PascalCase for classes, camelCase or snake_case for methods/variables).
- **Includes:** Use `#include <...>` for standard/system headers and `#include "..."` for project headers.

## Project Structure

- `src/`: Source code directory.
  - `main.cpp`: Entry point of the application.
- `CMakeLists.txt`: Build configuration.
- `Taskfile.yml`: Task automation configuration.
