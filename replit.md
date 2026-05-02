# x86++ Emulator

A complete GUI x86 (32-bit) CPU emulator written in C++, served as a web application with a full dark-themed control panel.

## Features

- **Real x86 instruction engine** — 150+ opcodes implemented including all ALU groups, control flow, stack ops, string ops, shifts/rotates, MUL/DIV, and two-byte 0F prefix instructions
- **BIOS simulation** — INT 10h (VGA video), INT 16h (keyboard), INT 21h (DOS-like services)
- **VGA text mode** — 80×25 character display with full CGA 16-color palette
- **Live disassembler** — real-time disassembly of memory, follows EIP with breakpoint support
- **Hex memory viewer** — browse any memory region with ASCII panel
- **Custom program loader** — paste raw hex bytes to load and execute your own programs
- **5 built-in demos** — Hello World, Fibonacci, Counter, Sieve of Eratosthenes, Color Screen
- **Execution controls** — Step (F8), Run (F5), Pause, Reset (F2) with speed slider
- **Full register display** — EAX–EDI, ESP, EBP, EIP, EFLAGS + individual flags

## Project Structure

```
srcemulator.cpp       Main entry point + HTTP server (cpp-httplib)
include/
  cpu.h               CPU registers and EFLAGS
  memory.h            Flat 8 MB memory model with VGA text buffer at 0xB8000
  io.h                I/O port simulation
  decoder.h           x86 instruction decoder/executor + BIOS interrupt handler
  disasm.h            x86 disassembler (builds string representation)
  emulator.h          Top-level emulator class (thread-safe)
  httplib.h           cpp-httplib (header-only HTTP server)
  json.hpp            nlohmann/json (header-only JSON)
static/
  index.html          Single-page frontend GUI (vanilla JS)
CMakeLists.txt        CMake build configuration
```

## Architecture

### Memory Layout
- `0x000000–0x09FFFF`: Conventional RAM (640 KB)
- `0x0B8000–0x0BFFFF`: VGA text mode buffer (80×25 × 2 bytes)
- `0x100000–0x7FFFFF`: Extended RAM
- Total: **8 MB flat address space**

### CPU (32-bit Protected Mode)
- Registers: EAX, EBX, ECX, EDX, ESI, EDI, EBP, ESP, EIP
- Flags: CF, PF, AF, ZF, SF, TF, IF, DF, OF (via EFLAGS)
- Programs are loaded at `0x1000`; initial ESP at `0x7FF000`

### HTTP API (port 5000)
| Method | Route | Description |
|--------|-------|-------------|
| GET | `/` | Serve frontend GUI |
| GET | `/api/state` | Full CPU register/flag state |
| GET | `/api/screen` | VGA screen (80×25 JSON array) |
| GET | `/api/memory?addr=&len=` | Memory hex dump |
| GET | `/api/disasm?addr=&count=` | Disassembly listing |
| GET | `/api/log` | I/O and BIOS log |
| POST | `/api/step` | Execute one instruction |
| POST | `/api/run?steps=N` | Execute up to N instructions |
| POST | `/api/reset` | Full reset |
| POST | `/api/load` | Load binary or select built-in demo |

## Build System

- **Language**: C++17
- **Build tool**: CMake + GCC 14
- **Dependencies**: cmake, ninja (Nix), pthreads

### Build command
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
./build/x86emu
```

## Keyboard Shortcuts (in GUI)

| Key | Action |
|-----|--------|
| F5 | Run / Pause |
| F8 | Step one instruction |
| F2 | Reset emulator |

## Demo Programs

All demos load at address `0x1000`:

| Name | Description |
|------|-------------|
| Hello World | Prints via INT 10h AH=0Eh (teletype), then HLT |
| Fibonacci | Computes 24 Fibonacci numbers into memory at 0x2000 |
| Counter | Counts 1,000,000 iterations in ECX, stores result at 0x3000 |
| Sieve of Eratosthenes | Marks 1,000 primes at 0x4000 |
| Color Screen | Fills VGA buffer with colored characters using direct memory write |
