# x86_64 Emulator

A C++ x86_64 CPU emulator, designed as an alternative to Bochs.

## Project Structure

- `srcemulator.cpp` — Main source file with entry point
- `srcemulator.h` — CPU and register definitions
- `CMakeLists.txt` — CMake build configuration
- `build/` — Compiled output directory (git-ignored)

## Build System

- **Language:** C++17
- **Build Tool:** CMake + GCC 14
- **Output:** `./build/x86emu`

### Build Commands

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/x86emu
```

## Architecture

### `Registers` struct
Holds all 16 general-purpose x86_64 registers (RAX–R15), RIP, and RFLAGS as `uint64_t` values.

### `CPU` class
- Owns a `Registers` instance and a flat byte `memory` vector (default 1 MB)
- `reset()` — zeroes all registers
- `dumpRegisters()` — prints register state to stdout

## Workflow

The "Start application" workflow builds and runs the emulator as a console application.

## Dependencies

- `cmake` (system, installed via Nix)
- `ninja` (system, installed via Nix)
- GCC C++ compiler (provided by Replit runtime)
