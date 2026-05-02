#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <iostream>
#include <string>

struct Registers {
    uint64_t rax = 0, rbx = 0, rcx = 0, rdx = 0;
    uint64_t rsi = 0, rdi = 0, rbp = 0, rsp = 0;
    uint64_t r8  = 0, r9  = 0, r10 = 0, r11 = 0;
    uint64_t r12 = 0, r13 = 0, r14 = 0, r15 = 0;
    uint64_t rip = 0;
    uint64_t rflags = 0;
};

class CPU {
public:
    Registers regs;
    std::vector<uint8_t> memory;

    CPU(size_t memSize = 1024 * 1024)
        : memory(memSize, 0) {}

    void reset() {
        regs = Registers{};
    }

    void dumpRegisters() const {
        std::cout << "=== Register State ===" << std::endl;
        std::cout << "RAX: 0x" << std::hex << regs.rax
                  << "  RBX: 0x" << regs.rbx << std::endl;
        std::cout << "RCX: 0x" << regs.rcx
                  << "  RDX: 0x" << regs.rdx << std::endl;
        std::cout << "RIP: 0x" << regs.rip << std::dec << std::endl;
    }
};
