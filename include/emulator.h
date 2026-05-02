#pragma once
#include "cpu.h"
#include "memory.h"
#include "io.h"
#include "decoder.h"
#include "disasm.h"
#include <mutex>
#include <string>
#include <vector>

class Emulator {
public:
    CPU     cpu;
    Memory  mem;
    IO      io;
    Decoder dec;
    mutable std::mutex mtx;

    Emulator() : dec(cpu, mem, io) { cpu.reset(); }

    void reset(bool clearMem = false) {
        std::lock_guard<std::mutex> lk(mtx);
        cpu.reset();
        if (clearMem) mem.reset();
        else          mem.clearVGA();
        io.reset();
        dec.cursorRow  = 0;
        dec.cursorCol  = 0;
        dec.consoleOut.clear();
        dec.biosLog.clear();
    }

    void loadProgram(uint32_t addr, const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lk(mtx);
        mem.load(addr, data, len);
        cpu.eip    = addr;
        cpu.halted = false;
        cpu.running= false;
        dec.cursorRow = 0;
        dec.cursorCol = 0;
        dec.consoleOut.clear();
        dec.biosLog.clear();
        mem.clearVGA();
    }

    // Execute exactly one instruction; returns false if halted
    bool step() {
        std::lock_guard<std::mutex> lk(mtx);
        if (cpu.halted) return false;
        return dec.step();
    }

    // Execute up to n instructions; returns actual count
    int runSteps(int n) {
        std::lock_guard<std::mutex> lk(mtx);
        int i = 0;
        while (i < n && !cpu.halted) {
            if (!dec.step()) break;
            i++;
        }
        return i;
    }

    // Disassemble count instructions starting at addr
    std::vector<DisasmLine> disasm(uint32_t addr, int count) const {
        std::lock_guard<std::mutex> lk(mtx);
        Disasm d(mem);
        return d.disassemble(addr, count);
    }

    // Snapshot of console output since last call
    std::string flushConsole() {
        std::lock_guard<std::mutex> lk(mtx);
        std::string s = dec.consoleOut;
        dec.consoleOut.clear();
        return s;
    }

    // Read memory range safely
    std::vector<uint8_t> readMem(uint32_t addr, uint32_t len) const {
        std::lock_guard<std::mutex> lk(mtx);
        std::vector<uint8_t> out;
        out.reserve(len);
        for (uint32_t i = 0; i < len && (addr+i) < MEM_SIZE; i++)
            out.push_back(mem.data[addr+i]);
        return out;
    }
};
