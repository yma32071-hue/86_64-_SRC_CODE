#include "srcemulator.h"

int main() {
    std::cout << "x86_64 Emulator - Starting..." << std::endl;

    CPU cpu;
    cpu.reset();

    std::cout << "CPU initialized with " << cpu.memory.size() / 1024
              << " KB of memory." << std::endl;

    cpu.regs.rax = 0xDEADBEEF;
    cpu.regs.rip = 0x1000;
    cpu.dumpRegisters();

    std::cout << "Emulator ready." << std::endl;
    return 0;
}
