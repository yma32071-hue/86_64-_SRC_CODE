#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

enum RegIdx { REG_EAX=0, REG_ECX, REG_EDX, REG_EBX, REG_ESP, REG_EBP, REG_ESI, REG_EDI };

struct Flags {
    bool CF=false, PF=false, AF=false, ZF=false;
    bool SF=false, TF=false, IF=true,  DF=false, OF=false;

    uint32_t toEFLAGS() const {
        uint32_t f = 0x2;
        if (CF) f |= (1<<0); if (PF) f |= (1<<2); if (AF) f |= (1<<4);
        if (ZF) f |= (1<<6); if (SF) f |= (1<<7); if (TF) f |= (1<<8);
        if (IF) f |= (1<<9); if (DF) f |= (1<<10); if (OF) f |= (1<<11);
        return f;
    }
    void fromEFLAGS(uint32_t f) {
        CF=(f>>0)&1; PF=(f>>2)&1; AF=(f>>4)&1; ZF=(f>>6)&1;
        SF=(f>>7)&1; TF=(f>>8)&1; IF=(f>>9)&1; DF=(f>>10)&1; OF=(f>>11)&1;
    }
};

struct CPU {
    uint32_t regs[8] = {};  // EAX ECX EDX EBX ESP EBP ESI EDI
    uint32_t eip = 0x1000;
    Flags    flags;
    bool     halted  = false;
    bool     running = false;
    uint32_t cycleCount = 0;

    uint32_t& reg32(int i)       { return regs[i&7]; }
    uint32_t  reg32(int i) const { return regs[i&7]; }

    uint8_t reg8(int i) const {
        return (i < 4) ? (regs[i] & 0xFF) : ((regs[i-4] >> 8) & 0xFF);
    }
    void setReg8(int i, uint8_t v) {
        if (i < 4) regs[i] = (regs[i] & 0xFFFFFF00) | v;
        else       regs[i-4] = (regs[i-4] & 0xFFFF00FF) | ((uint32_t)v << 8);
    }
    uint16_t reg16(int i) const { return regs[i&7] & 0xFFFF; }
    void setReg16(int i, uint16_t v) { regs[i&7] = (regs[i&7] & 0xFFFF0000) | v; }

    void reset() {
        for (auto& r : regs) r = 0;
        regs[REG_ESP] = 0x7FF000;
        eip = 0x1000;
        flags = Flags{};
        halted = false;
        running = false;
        cycleCount = 0;
    }

    static const char* name32(int i) {
        static const char* n[]={"EAX","ECX","EDX","EBX","ESP","EBP","ESI","EDI"};
        return n[i&7];
    }
    static const char* name16(int i) {
        static const char* n[]={"AX","CX","DX","BX","SP","BP","SI","DI"};
        return n[i&7];
    }
    static const char* name8(int i) {
        static const char* n[]={"AL","CL","DL","BL","AH","CH","DH","BH"};
        return n[i&7];
    }
};
