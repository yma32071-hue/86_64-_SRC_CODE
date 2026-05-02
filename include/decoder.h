#pragma once
#include "cpu.h"
#include "memory.h"
#include "io.h"
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>

class Decoder {
public:
    CPU&    cpu;
    Memory& mem;
    IO&     io;

    uint8_t  cursorRow  = 0;
    uint8_t  cursorCol  = 0;
    std::string consoleOut;
    std::vector<std::string> biosLog;

    Decoder(CPU& c, Memory& m, IO& i) : cpu(c), mem(m), io(i) {}

    bool step();

private:
    // ---- fetch helpers ----
    uint8_t  fetch8()  { return mem.read8 (cpu.eip++); }
    uint16_t fetch16() { uint16_t v=mem.read16(cpu.eip); cpu.eip+=2; return v; }
    uint32_t fetch32() { uint32_t v=mem.read32(cpu.eip); cpu.eip+=4; return v; }

    // ---- ModRM ----
    struct RMAddr { bool isReg; int regIdx; uint32_t addr; };

    RMAddr decodeRM(uint8_t mrm) {
        uint8_t mod=(mrm>>6)&3, rm=mrm&7;
        RMAddr r{};
        if (mod==3) { r.isReg=true; r.regIdx=rm; return r; }
        r.isReg=false;
        uint32_t ea=0;
        if (rm==4) { // SIB
            uint8_t sib=fetch8(), sc=(sib>>6)&3, idx=(sib>>3)&7, base=sib&7;
            ea = (base==5&&mod==0) ? fetch32() : cpu.reg32(base);
            if (idx!=4) ea += cpu.reg32(idx) << sc;
        } else if (rm==5&&mod==0) {
            ea = fetch32();
        } else {
            ea = cpu.reg32(rm);
        }
        if (mod==1) ea += (uint32_t)(int32_t)(int8_t)fetch8();
        else if (mod==2) ea += fetch32();
        r.addr = ea; return r;
    }

    uint32_t readRM32 (const RMAddr& r) { return r.isReg ? cpu.reg32(r.regIdx) : mem.read32(r.addr); }
    void     writeRM32(const RMAddr& r, uint32_t v) { if (r.isReg) cpu.reg32(r.regIdx)=v; else mem.write32(r.addr,v); }
    uint8_t  readRM8  (const RMAddr& r) { return r.isReg ? cpu.reg8(r.regIdx)  : mem.read8 (r.addr); }
    void     writeRM8 (const RMAddr& r, uint8_t  v) { if (r.isReg) cpu.setReg8(r.regIdx,v); else mem.write8(r.addr,v); }
    uint16_t readRM16 (const RMAddr& r) { return r.isReg ? cpu.reg16(r.regIdx) : mem.read16(r.addr); }
    void     writeRM16(const RMAddr& r, uint16_t v) { if (r.isReg) cpu.setReg16(r.regIdx,v); else mem.write16(r.addr,v); }

    // ---- stack ----
    void     push32(uint32_t v) { cpu.regs[REG_ESP]-=4; mem.write32(cpu.regs[REG_ESP],v); }
    uint32_t pop32 ()           { uint32_t v=mem.read32(cpu.regs[REG_ESP]); cpu.regs[REG_ESP]+=4; return v; }

    // ---- flags ----
    static bool parity(uint8_t v) { v^=v>>4; v^=v>>2; v^=v>>1; return !(v&1); }

    void setFlagsAdd32(uint32_t a, uint32_t b, uint64_t r) {
        cpu.flags.CF=(r>>32)&1; cpu.flags.ZF=!(uint32_t)r; cpu.flags.SF=(r>>31)&1;
        cpu.flags.PF=parity(r&0xFF); cpu.flags.AF=((a^b^r)>>4)&1;
        cpu.flags.OF=(!((a^b)&0x80000000))&&((a^r)&0x80000000);
    }
    void setFlagsSub32(uint32_t a, uint32_t b, uint64_t r) {
        cpu.flags.CF=(b>a); cpu.flags.ZF=!(uint32_t)r; cpu.flags.SF=(r>>31)&1;
        cpu.flags.PF=parity(r&0xFF); cpu.flags.AF=((a^b^r)>>4)&1;
        cpu.flags.OF=((a^b)&0x80000000)&&((a^r)&0x80000000);
    }
    void setFlagsLogic32(uint32_t r) {
        cpu.flags.CF=cpu.flags.OF=false;
        cpu.flags.ZF=!r; cpu.flags.SF=(r>>31)&1; cpu.flags.PF=parity(r&0xFF);
    }
    void setFlagsAdd8(uint8_t a, uint8_t b, uint16_t r) {
        cpu.flags.CF=(r>>8)&1; cpu.flags.ZF=!(uint8_t)r; cpu.flags.SF=(r>>7)&1;
        cpu.flags.PF=parity(r&0xFF); cpu.flags.AF=((a^b^r)>>4)&1;
        cpu.flags.OF=(!((a^b)&0x80))&&((a^r)&0x80);
    }
    void setFlagsSub8(uint8_t a, uint8_t b, uint16_t r) {
        cpu.flags.CF=(b>a); cpu.flags.ZF=!(uint8_t)r; cpu.flags.SF=(r>>7)&1;
        cpu.flags.PF=parity(r&0xFF); cpu.flags.AF=((a^b^r)>>4)&1;
        cpu.flags.OF=((a^b)&0x80)&&((a^r)&0x80);
    }
    void setFlagsLogic8(uint8_t r) {
        cpu.flags.CF=cpu.flags.OF=false;
        cpu.flags.ZF=!r; cpu.flags.SF=(r>>7)&1; cpu.flags.PF=parity(r);
    }

    // ---- Group 1 ALU ----
    uint32_t alu32(uint8_t op, uint32_t a, uint32_t b) {
        uint64_t r;
        switch(op&7){
            case 0: r=(uint64_t)a+b;            setFlagsAdd32(a,b,r); return (uint32_t)r;  // ADD
            case 1: r=a|b;                       setFlagsLogic32(r);   return (uint32_t)r;  // OR
            case 2: r=(uint64_t)a+b+cpu.flags.CF; setFlagsAdd32(a,b,r); return (uint32_t)r; // ADC
            case 3: { uint64_t c=cpu.flags.CF; r=(uint64_t)a-b-c; setFlagsSub32(a,b+c,r); return (uint32_t)r; } // SBB
            case 4: r=a&b;                       setFlagsLogic32(r);   return (uint32_t)r;  // AND
            case 5: r=(uint64_t)a-b;             setFlagsSub32(a,b,r); return (uint32_t)r;  // SUB
            case 6: r=a^b;                       setFlagsLogic32(r);   return (uint32_t)r;  // XOR
            case 7: r=(uint64_t)a-b;             setFlagsSub32(a,b,r); return a;             // CMP
        }
        return 0;
    }
    uint8_t alu8(uint8_t op, uint8_t a, uint8_t b) {
        uint16_t r;
        switch(op&7){
            case 0: r=(uint16_t)a+b;              setFlagsAdd8(a,b,r); return (uint8_t)r;
            case 1: r=a|b;                         setFlagsLogic8(r);   return (uint8_t)r;
            case 2: r=(uint16_t)a+b+cpu.flags.CF;  setFlagsAdd8(a,b,r); return (uint8_t)r;
            case 3: { uint16_t c=cpu.flags.CF; r=(uint16_t)a-b-c; setFlagsSub8(a,b+c,r); return (uint8_t)r; }
            case 4: r=a&b;                         setFlagsLogic8(r);   return (uint8_t)r;
            case 5: r=(uint16_t)a-b;               setFlagsSub8(a,b,r); return (uint8_t)r;
            case 6: r=a^b;                         setFlagsLogic8(r);   return (uint8_t)r;
            case 7: r=(uint16_t)a-b;               setFlagsSub8(a,b,r); return a;
        }
        return 0;
    }

    // ---- Group 2 shifts ----
    uint32_t shift32(uint8_t op, uint32_t v, uint8_t cnt) {
        cnt &= 31; if (!cnt) return v;
        switch(op&7){
            case 0: { uint32_t r=(v<<cnt)|(v>>(32-cnt)); cpu.flags.CF=r&1; cpu.flags.OF=(cnt==1)&&(bool)((r>>31)&1)!=cpu.flags.CF; return r; } // ROL
            case 1: { uint32_t r=(v>>cnt)|(v<<(32-cnt)); cpu.flags.CF=(r>>31)&1; return r; } // ROR
            case 4: case 6: cpu.flags.CF=(v>>(32-cnt))&1; { uint32_t r=v<<cnt; cpu.flags.ZF=!r; cpu.flags.SF=(r>>31)&1; cpu.flags.PF=parity(r&0xFF); if(cnt==1)cpu.flags.OF=(r>>31)!=(v>>31); return r; } // SHL
            case 5: cpu.flags.CF=(v>>(cnt-1))&1; { uint32_t r=v>>cnt; cpu.flags.ZF=!r; cpu.flags.SF=0; cpu.flags.PF=parity(r&0xFF); if(cnt==1)cpu.flags.OF=(v>>31)&1; return r; } // SHR
            case 7: cpu.flags.CF=(v>>(cnt-1))&1; { uint32_t r=(uint32_t)((int32_t)v>>cnt); cpu.flags.ZF=!r; cpu.flags.SF=(r>>31)&1; cpu.flags.PF=parity(r&0xFF); return r; } // SAR
            default: return v;
        }
    }
    uint8_t shift8(uint8_t op, uint8_t v, uint8_t cnt) {
        cnt &= 7; if (!cnt) return v;
        switch(op&7){
            case 0: { uint8_t r=(v<<cnt)|(v>>(8-cnt)); cpu.flags.CF=r&1; return r; }
            case 1: { uint8_t r=(v>>cnt)|(v<<(8-cnt)); cpu.flags.CF=(r>>7)&1; return r; }
            case 4: case 6: cpu.flags.CF=(v>>(8-cnt))&1; { uint8_t r=v<<cnt; cpu.flags.ZF=!r; cpu.flags.SF=(r>>7)&1; cpu.flags.PF=parity(r); return r; }
            case 5: cpu.flags.CF=(v>>(cnt-1))&1; { uint8_t r=v>>cnt; cpu.flags.ZF=!r; cpu.flags.SF=0; cpu.flags.PF=parity(r); return r; }
            case 7: cpu.flags.CF=(v>>(cnt-1))&1; { uint8_t r=(uint8_t)((int8_t)v>>cnt); cpu.flags.ZF=!r; cpu.flags.SF=(r>>7)&1; cpu.flags.PF=parity(r); return r; }
            default: return v;
        }
    }

    // ---- BIOS/VGA ----
    void vgaPutChar(uint8_t ch, uint8_t attr) {
        if (ch=='\r') { cursorCol=0; return; }
        if (ch=='\n') { cursorCol=0; cursorRow++; }
        else if (ch=='\b') { if(cursorCol) cursorCol--; }
        else {
            if (cursorRow<25 && cursorCol<80) {
                uint32_t off=VGA_TEXT_BASE+(cursorRow*80+cursorCol)*2;
                mem.write8(off,ch); mem.write8(off+1,attr);
            }
            if (++cursorCol>=80) { cursorCol=0; cursorRow++; }
        }
        if (cursorRow>=25) scrollVGA(1,0x07);
        consoleOut += (char)ch;
    }

    void scrollVGA(int lines, uint8_t attr) {
        for (int row=0; row<25-lines; row++)
            for (int col=0; col<80; col++) {
                uint32_t d=VGA_TEXT_BASE+(row*80+col)*2, s=d+lines*160;
                mem.write8(d, mem.read8(s)); mem.write8(d+1, mem.read8(s+1));
            }
        for (int row=25-lines; row<25; row++)
            for (int col=0; col<80; col++) {
                uint32_t d=VGA_TEXT_BASE+(row*80+col)*2;
                mem.write8(d,0x20); mem.write8(d+1,attr);
            }
        if (lines>0 && cursorRow>0) cursorRow--;
    }

    void handleINT(uint8_t n) {
        uint8_t ah=cpu.reg8(4), al=cpu.reg8(0);
        char tmp[128];
        switch(n) {
        case 0x10: // Video
            switch(ah) {
            case 0x00: mem.clearVGA(); cursorRow=cursorCol=0; break;
            case 0x01: break;
            case 0x02: cursorRow=(cpu.regs[REG_EDX]>>8)&0xFF; cursorCol=cpu.regs[REG_EDX]&0xFF; break;
            case 0x03: cpu.regs[REG_EDX]=((uint32_t)cursorRow<<8)|cursorCol; cpu.regs[REG_ECX]=0x0607; break;
            case 0x06: { int ln=al?al:25; scrollVGA(ln,(cpu.regs[REG_EBX]>>8)&0xFF); break; }
            case 0x08: { uint32_t off=VGA_TEXT_BASE+(cursorRow*80+cursorCol)*2; cpu.setReg8(0,mem.read8(off)); cpu.setReg8(4,mem.read8(off+1)); break; }
            case 0x09: { uint8_t at=cpu.regs[REG_EBX]&0xFF; uint16_t cnt=cpu.regs[REG_ECX]&0xFFFF;
                for(int i=0;i<cnt;i++){uint32_t off=VGA_TEXT_BASE+(cursorRow*80+cursorCol)*2; mem.write8(off,al); mem.write8(off+1,at); if(++cursorCol>=80){cursorCol=0;cursorRow++;}} break; }
            case 0x0A: { uint16_t cnt=cpu.regs[REG_ECX]&0xFFFF;
                for(int i=0;i<cnt;i++){uint32_t off=VGA_TEXT_BASE+(cursorRow*80+cursorCol)*2; mem.write8(off,al); if(++cursorCol>=80){cursorCol=0;cursorRow++;}} break; }
            case 0x0E: vgaPutChar(al,0x07); break;
            case 0x0F: cpu.setReg8(0,0x03); cpu.setReg8(4,80); break;
            }
            break;
        case 0x16:
            if(ah==0x00) { cpu.regs[REG_EAX]=0; }
            else if(ah==0x01) { cpu.flags.ZF=true; }
            break;
        case 0x21:
            switch(ah) {
            case 0x02: vgaPutChar(cpu.regs[REG_EDX]&0xFF,0x07); break;
            case 0x09: { uint32_t ptr=cpu.regs[REG_EDX]; uint8_t c; int lim=2000;
                while((c=mem.read8(ptr++))!='$'&&lim-->0) vgaPutChar(c,0x07);
                cpu.setReg8(0,'$'); break; }
            case 0x40: { // write to handle
                uint32_t ptr=cpu.regs[REG_EDX]; uint16_t len=cpu.regs[REG_ECX]&0xFFFF;
                for(int i=0;i<len;i++) vgaPutChar(mem.read8(ptr+i),0x07);
                cpu.regs[REG_EAX]=len; cpu.flags.CF=false; break; }
            case 0x4C: cpu.halted=true; cpu.running=false; break;
            }
            break;
        case 0x03: cpu.halted=true; cpu.running=false; break;
        default:
            snprintf(tmp,sizeof(tmp),"INT 0x%02X (AX=0x%04X)",n,cpu.regs[REG_EAX]&0xFFFF);
            biosLog.push_back(tmp);
            break;
        }
    }
};

// ---------- step() implementation ----------
inline bool Decoder::step() {
    if (cpu.halted) return false;

    uint8_t opcode;
    while (true) {  // prefix loop
        opcode = fetch8();
        if (opcode==0x66||opcode==0x67||opcode==0xF0) continue;
        if (opcode==0x26||opcode==0x2E||opcode==0x36||opcode==0x3E) continue;
        if (opcode==0x64||opcode==0x65) continue;
        if (opcode==0xF2||opcode==0xF3) continue;
        break;
    }
    cpu.cycleCount++;

    // helper macros to shorten repetitive opcode blocks
    #define MODRM(M)  uint8_t _m=fetch8(); auto rm=decodeRM(_m); int _reg=(_m>>3)&7; (void)_reg; (void)rm; (void)M;
    #define REG _reg
    #define RM  rm

    switch(opcode) {

    // --- ADD ---
    case 0x00:{ MODRM(0); uint8_t a=readRM8(RM),b=cpu.reg8(REG); uint16_t r=a+b; setFlagsAdd8(a,b,r); writeRM8(RM,(uint8_t)r); return true; }
    case 0x01:{ MODRM(0); uint32_t a=readRM32(RM),b=cpu.reg32(REG); uint64_t r=(uint64_t)a+b; setFlagsAdd32(a,b,r); writeRM32(RM,(uint32_t)r); return true; }
    case 0x02:{ MODRM(0); uint8_t a=cpu.reg8(REG),b=readRM8(RM); uint16_t r=a+b; setFlagsAdd8(a,b,r); cpu.setReg8(REG,(uint8_t)r); return true; }
    case 0x03:{ MODRM(0); uint32_t a=cpu.reg32(REG),b=readRM32(RM); uint64_t r=(uint64_t)a+b; setFlagsAdd32(a,b,r); cpu.reg32(REG)=(uint32_t)r; return true; }
    case 0x04:{ uint8_t a=cpu.reg8(0),b=fetch8(); uint16_t r=a+b; setFlagsAdd8(a,b,r); cpu.setReg8(0,(uint8_t)r); return true; }
    case 0x05:{ uint32_t a=cpu.regs[0],b=fetch32(); uint64_t r=(uint64_t)a+b; setFlagsAdd32(a,b,r); cpu.regs[0]=(uint32_t)r; return true; }

    // --- OR ---
    case 0x08:{ MODRM(0); uint8_t r=readRM8(RM)|cpu.reg8(REG); setFlagsLogic8(r); writeRM8(RM,r); return true; }
    case 0x09:{ MODRM(0); uint32_t r=readRM32(RM)|cpu.reg32(REG); setFlagsLogic32(r); writeRM32(RM,r); return true; }
    case 0x0A:{ MODRM(0); uint8_t r=cpu.reg8(REG)|readRM8(RM); setFlagsLogic8(r); cpu.setReg8(REG,r); return true; }
    case 0x0B:{ MODRM(0); uint32_t r=cpu.reg32(REG)|readRM32(RM); setFlagsLogic32(r); cpu.reg32(REG)=r; return true; }
    case 0x0C:{ uint8_t r=cpu.reg8(0)|fetch8(); setFlagsLogic8(r); cpu.setReg8(0,r); return true; }
    case 0x0D:{ uint32_t r=cpu.regs[0]|fetch32(); setFlagsLogic32(r); cpu.regs[0]=r; return true; }

    // --- ADC ---
    case 0x10:{ MODRM(0); uint8_t a=readRM8(RM),b=cpu.reg8(REG); uint16_t r=a+b+cpu.flags.CF; setFlagsAdd8(a,b,r); writeRM8(RM,(uint8_t)r); return true; }
    case 0x11:{ MODRM(0); uint32_t a=readRM32(RM),b=cpu.reg32(REG); uint64_t r=(uint64_t)a+b+cpu.flags.CF; setFlagsAdd32(a,b,r); writeRM32(RM,(uint32_t)r); return true; }
    case 0x12:{ MODRM(0); uint8_t a=cpu.reg8(REG),b=readRM8(RM); uint16_t r=a+b+cpu.flags.CF; setFlagsAdd8(a,b,r); cpu.setReg8(REG,(uint8_t)r); return true; }
    case 0x13:{ MODRM(0); uint32_t a=cpu.reg32(REG),b=readRM32(RM); uint64_t r=(uint64_t)a+b+cpu.flags.CF; setFlagsAdd32(a,b,r); cpu.reg32(REG)=(uint32_t)r; return true; }
    case 0x14:{ uint8_t a=cpu.reg8(0),b=fetch8(); uint16_t r=a+b+cpu.flags.CF; setFlagsAdd8(a,b,r); cpu.setReg8(0,(uint8_t)r); return true; }
    case 0x15:{ uint32_t a=cpu.regs[0],b=fetch32(); uint64_t r=(uint64_t)a+b+cpu.flags.CF; setFlagsAdd32(a,b,r); cpu.regs[0]=(uint32_t)r; return true; }

    // --- SBB ---
    case 0x18:{ MODRM(0); uint8_t c=cpu.flags.CF,a=readRM8(RM),b=cpu.reg8(REG)+c; uint16_t r=a-b; setFlagsSub8(a,b,r); writeRM8(RM,(uint8_t)r); return true; }
    case 0x19:{ MODRM(0); uint32_t c=cpu.flags.CF,a=readRM32(RM),b=cpu.reg32(REG)+c; uint64_t r=(uint64_t)a-b; setFlagsSub32(a,b,r); writeRM32(RM,(uint32_t)r); return true; }
    case 0x1A:{ MODRM(0); uint8_t c=cpu.flags.CF,a=cpu.reg8(REG),b=readRM8(RM)+c; uint16_t r=a-b; setFlagsSub8(a,b,r); cpu.setReg8(REG,(uint8_t)r); return true; }
    case 0x1B:{ MODRM(0); uint32_t c=cpu.flags.CF,a=cpu.reg32(REG),b=readRM32(RM)+c; uint64_t r=(uint64_t)a-b; setFlagsSub32(a,b,r); cpu.reg32(REG)=(uint32_t)r; return true; }
    case 0x1C:{ uint8_t c=cpu.flags.CF,a=cpu.reg8(0),b=fetch8()+c; uint16_t r=a-b; setFlagsSub8(a,b,r); cpu.setReg8(0,(uint8_t)r); return true; }
    case 0x1D:{ uint32_t c=cpu.flags.CF,a=cpu.regs[0],b=fetch32()+c; uint64_t r=(uint64_t)a-b; setFlagsSub32(a,b,r); cpu.regs[0]=(uint32_t)r; return true; }

    // --- AND ---
    case 0x20:{ MODRM(0); uint8_t r=readRM8(RM)&cpu.reg8(REG); setFlagsLogic8(r); writeRM8(RM,r); return true; }
    case 0x21:{ MODRM(0); uint32_t r=readRM32(RM)&cpu.reg32(REG); setFlagsLogic32(r); writeRM32(RM,r); return true; }
    case 0x22:{ MODRM(0); uint8_t r=cpu.reg8(REG)&readRM8(RM); setFlagsLogic8(r); cpu.setReg8(REG,r); return true; }
    case 0x23:{ MODRM(0); uint32_t r=cpu.reg32(REG)&readRM32(RM); setFlagsLogic32(r); cpu.reg32(REG)=r; return true; }
    case 0x24:{ uint8_t r=cpu.reg8(0)&fetch8(); setFlagsLogic8(r); cpu.setReg8(0,r); return true; }
    case 0x25:{ uint32_t r=cpu.regs[0]&fetch32(); setFlagsLogic32(r); cpu.regs[0]=r; return true; }

    // --- SUB ---
    case 0x28:{ MODRM(0); uint8_t a=readRM8(RM),b=cpu.reg8(REG); uint16_t r=a-b; setFlagsSub8(a,b,r); writeRM8(RM,(uint8_t)r); return true; }
    case 0x29:{ MODRM(0); uint32_t a=readRM32(RM),b=cpu.reg32(REG); uint64_t r=(uint64_t)a-b; setFlagsSub32(a,b,r); writeRM32(RM,(uint32_t)r); return true; }
    case 0x2A:{ MODRM(0); uint8_t a=cpu.reg8(REG),b=readRM8(RM); uint16_t r=a-b; setFlagsSub8(a,b,r); cpu.setReg8(REG,(uint8_t)r); return true; }
    case 0x2B:{ MODRM(0); uint32_t a=cpu.reg32(REG),b=readRM32(RM); uint64_t r=(uint64_t)a-b; setFlagsSub32(a,b,r); cpu.reg32(REG)=(uint32_t)r; return true; }
    case 0x2C:{ uint8_t a=cpu.reg8(0),b=fetch8(); uint16_t r=a-b; setFlagsSub8(a,b,r); cpu.setReg8(0,(uint8_t)r); return true; }
    case 0x2D:{ uint32_t a=cpu.regs[0],b=fetch32(); uint64_t r=(uint64_t)a-b; setFlagsSub32(a,b,r); cpu.regs[0]=(uint32_t)r; return true; }

    // --- XOR ---
    case 0x30:{ MODRM(0); uint8_t r=readRM8(RM)^cpu.reg8(REG); setFlagsLogic8(r); writeRM8(RM,r); return true; }
    case 0x31:{ MODRM(0); uint32_t r=readRM32(RM)^cpu.reg32(REG); setFlagsLogic32(r); writeRM32(RM,r); return true; }
    case 0x32:{ MODRM(0); uint8_t r=cpu.reg8(REG)^readRM8(RM); setFlagsLogic8(r); cpu.setReg8(REG,r); return true; }
    case 0x33:{ MODRM(0); uint32_t r=cpu.reg32(REG)^readRM32(RM); setFlagsLogic32(r); cpu.reg32(REG)=r; return true; }
    case 0x34:{ uint8_t r=cpu.reg8(0)^fetch8(); setFlagsLogic8(r); cpu.setReg8(0,r); return true; }
    case 0x35:{ uint32_t r=cpu.regs[0]^fetch32(); setFlagsLogic32(r); cpu.regs[0]=r; return true; }

    // --- CMP ---
    case 0x38:{ MODRM(0); uint8_t a=readRM8(RM),b=cpu.reg8(REG); setFlagsSub8(a,b,(uint16_t)a-b); return true; }
    case 0x39:{ MODRM(0); uint32_t a=readRM32(RM),b=cpu.reg32(REG); setFlagsSub32(a,b,(uint64_t)a-b); return true; }
    case 0x3A:{ MODRM(0); uint8_t a=cpu.reg8(REG),b=readRM8(RM); setFlagsSub8(a,b,(uint16_t)a-b); return true; }
    case 0x3B:{ MODRM(0); uint32_t a=cpu.reg32(REG),b=readRM32(RM); setFlagsSub32(a,b,(uint64_t)a-b); return true; }
    case 0x3C:{ uint8_t a=cpu.reg8(0),b=fetch8(); setFlagsSub8(a,b,(uint16_t)a-b); return true; }
    case 0x3D:{ uint32_t a=cpu.regs[0],b=fetch32(); setFlagsSub32(a,b,(uint64_t)a-b); return true; }

    // --- INC r32 (40-47) ---
    case 0x40:case 0x41:case 0x42:case 0x43:case 0x44:case 0x45:case 0x46:case 0x47:{
        int i=opcode-0x40; uint32_t a=cpu.regs[i]; bool cf=cpu.flags.CF;
        uint64_t r=(uint64_t)a+1; setFlagsAdd32(a,1,r); cpu.flags.CF=cf; cpu.regs[i]=(uint32_t)r; return true; }

    // --- DEC r32 (48-4F) ---
    case 0x48:case 0x49:case 0x4A:case 0x4B:case 0x4C:case 0x4D:case 0x4E:case 0x4F:{
        int i=opcode-0x48; uint32_t a=cpu.regs[i]; bool cf=cpu.flags.CF;
        uint64_t r=(uint64_t)a-1; setFlagsSub32(a,1,r); cpu.flags.CF=cf; cpu.regs[i]=(uint32_t)r; return true; }

    // --- PUSH r32 / POP r32 ---
    case 0x50:case 0x51:case 0x52:case 0x53:case 0x54:case 0x55:case 0x56:case 0x57:
        push32(cpu.regs[opcode-0x50]); return true;
    case 0x58:case 0x59:case 0x5A:case 0x5B:case 0x5C:case 0x5D:case 0x5E:case 0x5F:
        cpu.regs[opcode-0x58]=pop32(); return true;

    // --- PUSHA / POPA ---
    case 0x60:{ uint32_t sp=cpu.regs[REG_ESP]; push32(cpu.regs[0]); push32(cpu.regs[1]); push32(cpu.regs[2]); push32(cpu.regs[3]); push32(sp); push32(cpu.regs[5]); push32(cpu.regs[6]); push32(cpu.regs[7]); return true; }
    case 0x61:{ cpu.regs[7]=pop32(); cpu.regs[6]=pop32(); cpu.regs[5]=pop32(); pop32(); cpu.regs[3]=pop32(); cpu.regs[2]=pop32(); cpu.regs[1]=pop32(); cpu.regs[0]=pop32(); return true; }

    // --- PUSH imm ---
    case 0x68: push32(fetch32()); return true;
    case 0x6A: push32((uint32_t)(int32_t)(int8_t)fetch8()); return true;
    case 0x6B:{ MODRM(0); int8_t imm=fetch8(); int64_t r=(int64_t)(int32_t)readRM32(RM)*imm; cpu.reg32(REG)=(uint32_t)r; cpu.flags.CF=cpu.flags.OF=(r!=(int64_t)(int32_t)(uint32_t)r); return true; }

    // --- Jcc rel8 (70-7F) ---
    case 0x70:{ int8_t d=fetch8(); if(cpu.flags.OF) cpu.eip+=d; return true; }
    case 0x71:{ int8_t d=fetch8(); if(!cpu.flags.OF) cpu.eip+=d; return true; }
    case 0x72:{ int8_t d=fetch8(); if(cpu.flags.CF) cpu.eip+=d; return true; }
    case 0x73:{ int8_t d=fetch8(); if(!cpu.flags.CF) cpu.eip+=d; return true; }
    case 0x74:{ int8_t d=fetch8(); if(cpu.flags.ZF) cpu.eip+=d; return true; }
    case 0x75:{ int8_t d=fetch8(); if(!cpu.flags.ZF) cpu.eip+=d; return true; }
    case 0x76:{ int8_t d=fetch8(); if(cpu.flags.CF||cpu.flags.ZF) cpu.eip+=d; return true; }
    case 0x77:{ int8_t d=fetch8(); if(!cpu.flags.CF&&!cpu.flags.ZF) cpu.eip+=d; return true; }
    case 0x78:{ int8_t d=fetch8(); if(cpu.flags.SF) cpu.eip+=d; return true; }
    case 0x79:{ int8_t d=fetch8(); if(!cpu.flags.SF) cpu.eip+=d; return true; }
    case 0x7A:{ int8_t d=fetch8(); if(cpu.flags.PF) cpu.eip+=d; return true; }
    case 0x7B:{ int8_t d=fetch8(); if(!cpu.flags.PF) cpu.eip+=d; return true; }
    case 0x7C:{ int8_t d=fetch8(); if(cpu.flags.SF!=cpu.flags.OF) cpu.eip+=d; return true; }
    case 0x7D:{ int8_t d=fetch8(); if(cpu.flags.SF==cpu.flags.OF) cpu.eip+=d; return true; }
    case 0x7E:{ int8_t d=fetch8(); if(cpu.flags.ZF||(cpu.flags.SF!=cpu.flags.OF)) cpu.eip+=d; return true; }
    case 0x7F:{ int8_t d=fetch8(); if(!cpu.flags.ZF&&(cpu.flags.SF==cpu.flags.OF)) cpu.eip+=d; return true; }

    // --- Group 1 (80-83) ---
    case 0x80:{ MODRM(0); uint8_t op=_m>>3&7,b=fetch8(),a=readRM8(RM),r=alu8(op,a,b); if(op!=7) writeRM8(RM,r); return true; }
    case 0x81:{ MODRM(0); uint8_t op=_m>>3&7; uint32_t b=fetch32(),a=readRM32(RM),r=alu32(op,a,b); if(op!=7) writeRM32(RM,r); return true; }
    case 0x82:{ MODRM(0); uint8_t op=_m>>3&7,b=fetch8(),a=readRM8(RM),r=alu8(op,a,b); if(op!=7) writeRM8(RM,r); return true; }
    case 0x83:{ MODRM(0); uint8_t op=_m>>3&7; uint32_t b=(uint32_t)(int32_t)(int8_t)fetch8(),a=readRM32(RM),r=alu32(op,a,b); if(op!=7) writeRM32(RM,r); return true; }

    // --- TEST (84-85) ---
    case 0x84:{ MODRM(0); setFlagsLogic8(readRM8(RM)&cpu.reg8(REG)); return true; }
    case 0x85:{ MODRM(0); setFlagsLogic32(readRM32(RM)&cpu.reg32(REG)); return true; }

    // --- XCHG (86-87) ---
    case 0x86:{ MODRM(0); uint8_t a=readRM8(RM),b=cpu.reg8(REG); writeRM8(RM,b); cpu.setReg8(REG,a); return true; }
    case 0x87:{ MODRM(0); uint32_t a=readRM32(RM),b=cpu.reg32(REG); writeRM32(RM,b); cpu.reg32(REG)=a; return true; }

    // --- MOV (88-8B) ---
    case 0x88:{ MODRM(0); writeRM8(RM,cpu.reg8(REG)); return true; }
    case 0x89:{ MODRM(0); writeRM32(RM,cpu.reg32(REG)); return true; }
    case 0x8A:{ MODRM(0); cpu.setReg8(REG,readRM8(RM)); return true; }
    case 0x8B:{ MODRM(0); cpu.reg32(REG)=readRM32(RM); return true; }
    case 0x8C: fetch8(); return true;  // MOV sreg (stub)
    case 0x8D:{ MODRM(0); cpu.reg32(REG)=rm.isReg?0:rm.addr; return true; } // LEA
    case 0x8E: fetch8(); return true;  // MOV sreg (stub)
    case 0x8F:{ MODRM(0); writeRM32(RM,pop32()); return true; }

    // --- NOP / XCHG EAX,rx (90-97) ---
    case 0x90: return true;
    case 0x91:case 0x92:case 0x93:case 0x94:case 0x95:case 0x96:case 0x97:{
        int r=opcode-0x90; uint32_t t=cpu.regs[0]; cpu.regs[0]=cpu.regs[r]; cpu.regs[r]=t; return true; }

    // --- CWDE / CDQ ---
    case 0x98: cpu.regs[0]=(uint32_t)(int32_t)(int16_t)(cpu.regs[0]&0xFFFF); return true;
    case 0x99: cpu.regs[2]=(cpu.regs[0]>>31)?0xFFFFFFFF:0; return true;

    // --- PUSHF/POPF ---
    case 0x9C: push32(cpu.flags.toEFLAGS()); return true;
    case 0x9D: cpu.flags.fromEFLAGS(pop32()); return true;

    // --- SAHF / LAHF ---
    case 0x9E:{ uint8_t ah=cpu.reg8(4); cpu.flags.CF=ah&1; cpu.flags.PF=(ah>>2)&1; cpu.flags.AF=(ah>>4)&1; cpu.flags.ZF=(ah>>6)&1; cpu.flags.SF=(ah>>7)&1; return true; }
    case 0x9F:{ uint8_t f=0x02|(cpu.flags.CF)|(cpu.flags.PF<<2)|(cpu.flags.AF<<4)|(cpu.flags.ZF<<6)|(cpu.flags.SF<<7); cpu.setReg8(4,f); return true; }

    // --- MOV moffs ---
    case 0xA0: cpu.setReg8(0,mem.read8(fetch32())); return true;
    case 0xA1: cpu.regs[0]=mem.read32(fetch32()); return true;
    case 0xA2: mem.write8(fetch32(),cpu.reg8(0)); return true;
    case 0xA3: mem.write32(fetch32(),cpu.regs[0]); return true;

    // --- TEST AL/EAX ---
    case 0xA8:{ uint8_t r=cpu.reg8(0)&fetch8(); setFlagsLogic8(r); return true; }
    case 0xA9:{ uint32_t r=cpu.regs[0]&fetch32(); setFlagsLogic32(r); return true; }

    // --- MOVS/STOS/LODS/SCAS ---
    case 0xA4: mem.write8(cpu.regs[7],mem.read8(cpu.regs[6])); cpu.regs[6]+=cpu.flags.DF?-1u:1; cpu.regs[7]+=cpu.flags.DF?-1u:1; return true;
    case 0xA5: mem.write32(cpu.regs[7],mem.read32(cpu.regs[6])); cpu.regs[6]+=cpu.flags.DF?-4u:4; cpu.regs[7]+=cpu.flags.DF?-4u:4; return true;
    case 0xAA: mem.write8(cpu.regs[7],cpu.reg8(0)); cpu.regs[7]+=cpu.flags.DF?-1u:1; return true;
    case 0xAB: mem.write32(cpu.regs[7],cpu.regs[0]); cpu.regs[7]+=cpu.flags.DF?-4u:4; return true;
    case 0xAC: cpu.setReg8(0,mem.read8(cpu.regs[6])); cpu.regs[6]+=cpu.flags.DF?-1u:1; return true;
    case 0xAD: cpu.regs[0]=mem.read32(cpu.regs[6]); cpu.regs[6]+=cpu.flags.DF?-4u:4; return true;
    case 0xAE:{ uint8_t a=cpu.reg8(0),b=mem.read8(cpu.regs[7]); setFlagsSub8(a,b,(uint16_t)a-b); cpu.regs[7]+=cpu.flags.DF?-1u:1; return true; }
    case 0xAF:{ uint32_t a=cpu.regs[0],b=mem.read32(cpu.regs[7]); setFlagsSub32(a,b,(uint64_t)a-b); cpu.regs[7]+=cpu.flags.DF?-4u:4; return true; }

    // --- MOV r8,imm8 / MOV r32,imm32 ---
    case 0xB0:case 0xB1:case 0xB2:case 0xB3:case 0xB4:case 0xB5:case 0xB6:case 0xB7:
        cpu.setReg8(opcode-0xB0,fetch8()); return true;
    case 0xB8:case 0xB9:case 0xBA:case 0xBB:case 0xBC:case 0xBD:case 0xBE:case 0xBF:
        cpu.regs[opcode-0xB8]=fetch32(); return true;

    // --- Group 2 shifts (C0-C1) ---
    case 0xC0:{ MODRM(0); uint8_t cnt=fetch8(); writeRM8(RM,shift8((_m>>3)&7,readRM8(RM),cnt)); return true; }
    case 0xC1:{ MODRM(0); uint8_t cnt=fetch8(); writeRM32(RM,shift32((_m>>3)&7,readRM32(RM),cnt)); return true; }

    // --- RET ---
    case 0xC2:{ uint16_t n=fetch16(); cpu.eip=pop32(); cpu.regs[REG_ESP]+=n; return true; }
    case 0xC3: cpu.eip=pop32(); return true;

    // --- MOV r/m,imm ---
    case 0xC6:{ MODRM(0); writeRM8(RM,fetch8()); return true; }
    case 0xC7:{ MODRM(0); writeRM32(RM,fetch32()); return true; }

    // --- ENTER / LEAVE ---
    case 0xC8:{ uint16_t sz=fetch16(); fetch8(); push32(cpu.regs[REG_EBP]); cpu.regs[REG_EBP]=cpu.regs[REG_ESP]; cpu.regs[REG_ESP]-=sz; return true; }
    case 0xC9: cpu.regs[REG_ESP]=cpu.regs[REG_EBP]; cpu.regs[REG_EBP]=pop32(); return true;

    // --- INT ---
    case 0xCC: handleINT(3); return !cpu.halted;
    case 0xCD:{ uint8_t n=fetch8(); handleINT(n); return !cpu.halted; }
    case 0xCE: if(cpu.flags.OF) handleINT(4); return true;
    case 0xCF:{ cpu.eip=pop32(); pop32(); cpu.flags.fromEFLAGS(pop32()); return true; }

    // --- Shifts D0-D3 ---
    case 0xD0:{ MODRM(0); writeRM8(RM,shift8((_m>>3)&7,readRM8(RM),1)); return true; }
    case 0xD1:{ MODRM(0); writeRM32(RM,shift32((_m>>3)&7,readRM32(RM),1)); return true; }
    case 0xD2:{ MODRM(0); writeRM8(RM,shift8((_m>>3)&7,readRM8(RM),cpu.regs[1]&0x1F)); return true; }
    case 0xD3:{ MODRM(0); writeRM32(RM,shift32((_m>>3)&7,readRM32(RM),cpu.regs[1]&0x1F)); return true; }

    // --- LOOP (E0-E2) / JCXZ (E3) ---
    case 0xE0:{ int8_t d=fetch8(); cpu.regs[1]--; if(cpu.regs[1]&&!cpu.flags.ZF) cpu.eip+=d; return true; }
    case 0xE1:{ int8_t d=fetch8(); cpu.regs[1]--; if(cpu.regs[1]&&cpu.flags.ZF) cpu.eip+=d; return true; }
    case 0xE2:{ int8_t d=fetch8(); cpu.regs[1]--; if(cpu.regs[1]) cpu.eip+=d; return true; }
    case 0xE3:{ int8_t d=fetch8(); if(!cpu.regs[1]) cpu.eip+=d; return true; }

    // --- IN/OUT imm8 ---
    case 0xE4: cpu.setReg8(0,io.read8(fetch8())); return true;
    case 0xE5: cpu.regs[0]=io.read8(fetch8()); return true;
    case 0xE6: io.write8(fetch8(),cpu.reg8(0)); return true;
    case 0xE7: io.write8(fetch8(),cpu.regs[0]&0xFF); return true;

    // --- CALL rel32 / JMP rel32 / JMP rel8 ---
    case 0xE8:{ int32_t d=fetch32(); push32(cpu.eip); cpu.eip+=d; return true; }
    case 0xE9:{ int32_t d=fetch32(); cpu.eip+=d; return true; }
    case 0xEA:{ uint32_t o=fetch32(); fetch16(); cpu.eip=o; return true; }
    case 0xEB:{ int8_t d=fetch8(); cpu.eip+=d; return true; }

    // --- IN/OUT DX ---
    case 0xEC: cpu.setReg8(0,io.read8(cpu.reg16(REG_EDX))); return true;
    case 0xED: cpu.regs[0]=io.read8(cpu.reg16(REG_EDX)); return true;
    case 0xEE: io.write8(cpu.reg16(REG_EDX),cpu.reg8(0)); return true;
    case 0xEF: io.write8(cpu.reg16(REG_EDX),cpu.regs[0]&0xFF); return true;

    // --- HLT ---
    case 0xF4: cpu.halted=true; cpu.running=false; return false;

    // --- CMC ---
    case 0xF5: cpu.flags.CF=!cpu.flags.CF; return true;

    // --- Group 3 (F6-F7) ---
    case 0xF6:{ MODRM(0); uint8_t op=(_m>>3)&7, a=readRM8(RM);
        switch(op){
        case 0:case 1: setFlagsLogic8(a&fetch8()); break;
        case 2: writeRM8(RM,~a); break;
        case 3:{ uint16_t r=0-a; setFlagsSub8(0,a,r); writeRM8(RM,(uint8_t)r); break; }
        case 4:{ uint16_t r=(uint16_t)cpu.reg8(0)*a; cpu.setReg8(0,r&0xFF); cpu.setReg8(4,r>>8); cpu.flags.CF=cpu.flags.OF=(r>0xFF); break; }
        case 5:{ int16_t r=(int16_t)(int8_t)cpu.reg8(0)*(int8_t)a; cpu.setReg8(0,(uint8_t)r); cpu.setReg8(4,(r>>8)&0xFF); cpu.flags.CF=cpu.flags.OF=(r<-128||r>127); break; }
        case 6: if(a&&cpu.reg8(0)){ cpu.setReg8(4,cpu.reg8(0)%a); cpu.setReg8(0,cpu.reg8(0)/a); } break;
        case 7: if(a){ cpu.setReg8(4,(int8_t)cpu.reg8(0)%(int8_t)a); cpu.setReg8(0,(int8_t)cpu.reg8(0)/(int8_t)a); } break;
        } return true; }

    case 0xF7:{ MODRM(0); uint8_t op=(_m>>3)&7; uint32_t a=readRM32(RM);
        switch(op){
        case 0:case 1: setFlagsLogic32(a&fetch32()); break;
        case 2: writeRM32(RM,~a); break;
        case 3:{ uint64_t r=0ull-a; setFlagsSub32(0,a,r); writeRM32(RM,(uint32_t)r); break; }
        case 4:{ uint64_t r=(uint64_t)cpu.regs[0]*a; cpu.regs[0]=(uint32_t)r; cpu.regs[2]=r>>32; cpu.flags.CF=cpu.flags.OF=(r>0xFFFFFFFF); break; }
        case 5:{ int64_t r=(int64_t)(int32_t)cpu.regs[0]*(int32_t)a; cpu.regs[0]=(uint32_t)r; cpu.regs[2]=(uint32_t)(r>>32); cpu.flags.CF=cpu.flags.OF=(r<INT32_MIN||r>INT32_MAX); break; }
        case 6:{ if(a){ uint64_t n=((uint64_t)cpu.regs[2]<<32)|cpu.regs[0]; cpu.regs[0]=(uint32_t)(n/a); cpu.regs[2]=(uint32_t)(n%a); } break; }
        case 7:{ if(a){ int64_t n=((int64_t)(int32_t)cpu.regs[2]<<32)|(uint32_t)cpu.regs[0]; cpu.regs[0]=(uint32_t)(n/(int32_t)a); cpu.regs[2]=(uint32_t)(n%(int32_t)a); } break; }
        } return true; }

    // --- Flag instructions (F8-FD) ---
    case 0xF8: cpu.flags.CF=false; return true;
    case 0xF9: cpu.flags.CF=true;  return true;
    case 0xFA: cpu.flags.IF=false; return true;
    case 0xFB: cpu.flags.IF=true;  return true;
    case 0xFC: cpu.flags.DF=false; return true;
    case 0xFD: cpu.flags.DF=true;  return true;

    // --- Group 4 (FE) / Group 5 (FF) ---
    case 0xFE:{ MODRM(0); uint8_t op=(_m>>3)&7,a=readRM8(RM); bool cf=cpu.flags.CF;
        if(op==0){ uint16_t r=a+1; setFlagsAdd8(a,1,r); cpu.flags.CF=cf; writeRM8(RM,(uint8_t)r); }
        else if(op==1){ uint16_t r=a-1; setFlagsSub8(a,1,r); cpu.flags.CF=cf; writeRM8(RM,(uint8_t)r); }
        return true; }

    case 0xFF:{ MODRM(0); uint8_t op=(_m>>3)&7;
        switch(op){
        case 0:{ bool cf=cpu.flags.CF; uint32_t a=readRM32(RM); uint64_t r=(uint64_t)a+1; setFlagsAdd32(a,1,r); cpu.flags.CF=cf; writeRM32(RM,(uint32_t)r); break; }
        case 1:{ bool cf=cpu.flags.CF; uint32_t a=readRM32(RM); uint64_t r=(uint64_t)a-1; setFlagsSub32(a,1,r); cpu.flags.CF=cf; writeRM32(RM,(uint32_t)r); break; }
        case 2:{ uint32_t tgt=readRM32(RM); push32(cpu.eip); cpu.eip=tgt; break; }
        case 4: cpu.eip=readRM32(RM); break;
        case 6: push32(readRM32(RM)); break;
        } return true; }

    // ============ 0F two-byte opcodes ============
    case 0x0F:{
        uint8_t op2=fetch8();
        switch(op2){
        // Jcc rel32
        case 0x80:{ int32_t d=fetch32(); if(cpu.flags.OF) cpu.eip+=d; return true; }
        case 0x81:{ int32_t d=fetch32(); if(!cpu.flags.OF) cpu.eip+=d; return true; }
        case 0x82:{ int32_t d=fetch32(); if(cpu.flags.CF) cpu.eip+=d; return true; }
        case 0x83:{ int32_t d=fetch32(); if(!cpu.flags.CF) cpu.eip+=d; return true; }
        case 0x84:{ int32_t d=fetch32(); if(cpu.flags.ZF) cpu.eip+=d; return true; }
        case 0x85:{ int32_t d=fetch32(); if(!cpu.flags.ZF) cpu.eip+=d; return true; }
        case 0x86:{ int32_t d=fetch32(); if(cpu.flags.CF||cpu.flags.ZF) cpu.eip+=d; return true; }
        case 0x87:{ int32_t d=fetch32(); if(!cpu.flags.CF&&!cpu.flags.ZF) cpu.eip+=d; return true; }
        case 0x88:{ int32_t d=fetch32(); if(cpu.flags.SF) cpu.eip+=d; return true; }
        case 0x89:{ int32_t d=fetch32(); if(!cpu.flags.SF) cpu.eip+=d; return true; }
        case 0x8C:{ int32_t d=fetch32(); if(cpu.flags.SF!=cpu.flags.OF) cpu.eip+=d; return true; }
        case 0x8D:{ int32_t d=fetch32(); if(cpu.flags.SF==cpu.flags.OF) cpu.eip+=d; return true; }
        case 0x8E:{ int32_t d=fetch32(); if(cpu.flags.ZF||(cpu.flags.SF!=cpu.flags.OF)) cpu.eip+=d; return true; }
        case 0x8F:{ int32_t d=fetch32(); if(!cpu.flags.ZF&&(cpu.flags.SF==cpu.flags.OF)) cpu.eip+=d; return true; }
        // SETcc
        case 0x94:{ MODRM(0); writeRM8(RM,cpu.flags.ZF?1:0); return true; }
        case 0x95:{ MODRM(0); writeRM8(RM,!cpu.flags.ZF?1:0); return true; }
        case 0x9C:{ MODRM(0); writeRM8(RM,(cpu.flags.SF!=cpu.flags.OF)?1:0); return true; }
        case 0x9D:{ MODRM(0); writeRM8(RM,(cpu.flags.SF==cpu.flags.OF)?1:0); return true; }
        // IMUL r32, r/m32
        case 0xAF:{ MODRM(0); int64_t r=(int64_t)(int32_t)cpu.reg32(REG)*(int32_t)readRM32(RM); cpu.reg32(REG)=(uint32_t)r; cpu.flags.CF=cpu.flags.OF=(r!=(int64_t)(int32_t)(uint32_t)r); return true; }
        // MOVZX / MOVSX
        case 0xB6:{ MODRM(0); cpu.reg32(REG)=(uint32_t)readRM8(RM); return true; }
        case 0xB7:{ MODRM(0); cpu.reg32(REG)=(uint32_t)readRM16(RM); return true; }
        case 0xBE:{ MODRM(0); cpu.reg32(REG)=(uint32_t)(int32_t)(int8_t)readRM8(RM); return true; }
        case 0xBF:{ MODRM(0); cpu.reg32(REG)=(uint32_t)(int32_t)(int16_t)readRM16(RM); return true; }
        // XADD
        case 0xC0:{ MODRM(0); uint8_t a=readRM8(RM),b=cpu.reg8(REG); uint16_t r=a+b; setFlagsAdd8(a,b,r); cpu.setReg8(REG,a); writeRM8(RM,(uint8_t)r); return true; }
        case 0xC1:{ MODRM(0); uint32_t a=readRM32(RM),b=cpu.reg32(REG); uint64_t r=(uint64_t)a+b; setFlagsAdd32(a,b,r); cpu.reg32(REG)=a; writeRM32(RM,(uint32_t)r); return true; }
        default: return true;
        }
    }

    default: return true; // unknown - skip
    }

    #undef MODRM
    #undef REG
    #undef RM
}
