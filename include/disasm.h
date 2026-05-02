#pragma once
#include "memory.h"
#include <string>
#include <vector>
#include <cstdio>
#include <cstdarg>

struct DisasmLine {
    uint32_t addr;
    int      len;
    std::string bytes;
    std::string text;
};

class Disasm {
    const Memory& mem;
    uint32_t pos;

    uint8_t  r8()  { return mem.read8 (pos++); }
    uint16_t r16() { uint16_t v=mem.read16(pos); pos+=2; return v; }
    uint32_t r32() { uint32_t v=mem.read32(pos); pos+=4; return v; }

    static const char* R32[8];
    static const char* R8 [8];

    struct RM { bool isReg; int regIdx; char memStr[64]; };

    RM decodeRM(uint8_t mrm, bool wide) {
        RM r{}; uint8_t mod=(mrm>>6)&3, rm=mrm&7;
        if (mod==3) { r.isReg=true; r.regIdx=rm; return r; }
        r.isReg=false;
        char base[32]=""; char idx_s[32]=""; int32_t disp=0;
        if (rm==4) {
            uint8_t sib=r8(), sc=(sib>>6)&3, idx=(sib>>3)&7, ba=sib&7;
            if (ba==5&&mod==0) { disp=(int32_t)r32(); snprintf(base,sizeof(base),"0x%08X",disp); }
            else snprintf(base,sizeof(base),"%s",R32[ba]);
            if (idx!=4) snprintf(idx_s,sizeof(idx_s),"+%s*%d",R32[idx],1<<sc);
        } else if (rm==5&&mod==0) {
            disp=(int32_t)r32(); snprintf(base,sizeof(base),"0x%08X",disp);
        } else {
            snprintf(base,sizeof(base),"%s",R32[rm]);
        }
        if (mod==1) { disp=(int32_t)(int8_t)r8(); }
        else if (mod==2) { disp=(int32_t)r32(); }
        char disps[32]="";
        if (mod==1||mod==2) { if(disp>=0) snprintf(disps,sizeof(disps),"+0x%X",disp); else snprintf(disps,sizeof(disps),"-0x%X",-disp); }
        snprintf(r.memStr,sizeof(r.memStr),"[%s%s%s]",base,idx_s,disps);
        return r;
    }

    std::string rmStr(RM& r, bool wide) {
        if (r.isReg) return wide ? R32[r.regIdx] : R8[r.regIdx];
        return r.memStr;
    }

    std::string fmt(const char* f, ...) {
        char buf[128]; va_list va; va_start(va,f); vsnprintf(buf,sizeof(buf),f,va); va_end(va); return buf;
    }

public:
    Disasm(const Memory& m) : mem(m), pos(0) {}

    DisasmLine decode(uint32_t addr) {
        pos = addr;
        DisasmLine line; line.addr=addr;

        // skip prefixes
        uint8_t op;
        while (true) {
            op=r8();
            if (op==0x66||op==0x67||op==0xF0||op==0xF2||op==0xF3) continue;
            if (op==0x26||op==0x2E||op==0x36||op==0x3E||op==0x64||op==0x65) continue;
            break;
        }

        auto mkRM=[&](bool wide)->std::pair<std::string,RM> {
            uint8_t m=r8(); RM rm=decodeRM(m,wide);
            return {rmStr(rm,wide),rm};
        };

        static const char* grp1[]={"ADD","OR","ADC","SBB","AND","SUB","XOR","CMP"};
        static const char* grp2[]={"ROL","ROR","RCL","RCR","SHL","SHR","SHL","SAR"};
        static const char* grp3[]={"TEST","TEST","NOT","NEG","MUL","IMUL","DIV","IDIV"};
        static const char* jcc[] ={"JO","JNO","JB","JNB","JZ","JNZ","JBE","JA","JS","JNS","JP","JNP","JL","JGE","JLE","JG"};

        std::string t;
        if (op>=0x00&&op<=0x3D) {
            // ADD/OR/ADC/SBB/AND/SUB/XOR/CMP groups
            int g=op/8, sub=op%8;
            static const char* gnames[]={"ADD","OR","ADC","SBB","AND","SUB","XOR","CMP"};
            if (sub<6) {
                bool wide=(sub&1); bool rev=(sub>=2&&sub<4)||(sub>=2);
                auto [rms,rm]=mkRM(wide);
                int reg=((mem.read8(addr+(pos-addr-1-1))>>3)&7); // FIXME: simpler
                // just use group name
                t=fmt("%-6s ...", gnames[g]);
            }
        }

        // Simplified but correct disassembly for key opcodes:
        switch(op) {
        case 0x00:{ auto[s,r]=mkRM(false); t=fmt("ADD     %s, %s",s.c_str(),R8[(mem.read8(pos-1)>>3)&7]); break; }
        case 0x01:{ auto[s,r]=mkRM(true);  t=fmt("ADD     %s, %s",s.c_str(),R32[(mem.read8(pos-1)>>3)&7]); break; }
        case 0x02:{ auto[s,r]=mkRM(false); t=fmt("ADD     %s, %s",R8[(mem.read8(pos-1)>>3)&7],s.c_str()); break; }
        case 0x03:{ auto[s,r]=mkRM(true);  t=fmt("ADD     %s, %s",R32[(mem.read8(pos-1)>>3)&7],s.c_str()); break; }
        case 0x04: t=fmt("ADD     AL, 0x%02X",r8()); break;
        case 0x05: t=fmt("ADD     EAX, 0x%08X",r32()); break;
        case 0x08:{ auto[s,r]=mkRM(false); t=fmt("OR      %s, %s",s.c_str(),R8[(mem.read8(pos-1)>>3)&7]); break; }
        case 0x09:{ auto[s,r]=mkRM(true);  t=fmt("OR      %s, %s",s.c_str(),R32[(mem.read8(pos-1)>>3)&7]); break; }
        case 0x0A:{ auto[s,r]=mkRM(false); t=fmt("OR      %s, %s",R8[(mem.read8(pos-1)>>3)&7],s.c_str()); break; }
        case 0x0B:{ auto[s,r]=mkRM(true);  t=fmt("OR      %s, %s",R32[(mem.read8(pos-1)>>3)&7],s.c_str()); break; }
        case 0x0C: t=fmt("OR      AL, 0x%02X",r8()); break;
        case 0x0D: t=fmt("OR      EAX, 0x%08X",r32()); break;
        case 0x20:{ auto[s,r]=mkRM(false); t=fmt("AND     %s, %s",s.c_str(),R8[(mem.read8(pos-1)>>3)&7]); break; }
        case 0x21:{ auto[s,r]=mkRM(true);  t=fmt("AND     %s, %s",s.c_str(),R32[(mem.read8(pos-1)>>3)&7]); break; }
        case 0x22:{ auto[s,r]=mkRM(false); t=fmt("AND     %s, %s",R8[(mem.read8(pos-1)>>3)&7],s.c_str()); break; }
        case 0x23:{ auto[s,r]=mkRM(true);  t=fmt("AND     %s, %s",R32[(mem.read8(pos-1)>>3)&7],s.c_str()); break; }
        case 0x24: t=fmt("AND     AL, 0x%02X",r8()); break;
        case 0x25: t=fmt("AND     EAX, 0x%08X",r32()); break;
        case 0x28:{ auto[s,r]=mkRM(false); t=fmt("SUB     %s, %s",s.c_str(),R8[(mem.read8(pos-1)>>3)&7]); break; }
        case 0x29:{ auto[s,r]=mkRM(true);  t=fmt("SUB     %s, %s",s.c_str(),R32[(mem.read8(pos-1)>>3)&7]); break; }
        case 0x2A:{ auto[s,r]=mkRM(false); t=fmt("SUB     %s, %s",R8[(mem.read8(pos-1)>>3)&7],s.c_str()); break; }
        case 0x2B:{ auto[s,r]=mkRM(true);  t=fmt("SUB     %s, %s",R32[(mem.read8(pos-1)>>3)&7],s.c_str()); break; }
        case 0x2C: t=fmt("SUB     AL, 0x%02X",r8()); break;
        case 0x2D: t=fmt("SUB     EAX, 0x%08X",r32()); break;
        case 0x30:{ auto[s,r]=mkRM(false); t=fmt("XOR     %s, %s",s.c_str(),R8[(mem.read8(pos-1)>>3)&7]); break; }
        case 0x31:{ auto[s,r]=mkRM(true);  t=fmt("XOR     %s, %s",s.c_str(),R32[(mem.read8(pos-1)>>3)&7]); break; }
        case 0x32:{ auto[s,r]=mkRM(false); t=fmt("XOR     %s, %s",R8[(mem.read8(pos-1)>>3)&7],s.c_str()); break; }
        case 0x33:{ auto[s,r]=mkRM(true);  t=fmt("XOR     %s, %s",R32[(mem.read8(pos-1)>>3)&7],s.c_str()); break; }
        case 0x34: t=fmt("XOR     AL, 0x%02X",r8()); break;
        case 0x35: t=fmt("XOR     EAX, 0x%08X",r32()); break;
        case 0x38:{ auto[s,r]=mkRM(false); t=fmt("CMP     %s, %s",s.c_str(),R8[(mem.read8(pos-1)>>3)&7]); break; }
        case 0x39:{ auto[s,r]=mkRM(true);  t=fmt("CMP     %s, %s",s.c_str(),R32[(mem.read8(pos-1)>>3)&7]); break; }
        case 0x3A:{ auto[s,r]=mkRM(false); t=fmt("CMP     %s, %s",R8[(mem.read8(pos-1)>>3)&7],s.c_str()); break; }
        case 0x3B:{ auto[s,r]=mkRM(true);  t=fmt("CMP     %s, %s",R32[(mem.read8(pos-1)>>3)&7],s.c_str()); break; }
        case 0x3C: t=fmt("CMP     AL, 0x%02X",r8()); break;
        case 0x3D: t=fmt("CMP     EAX, 0x%08X",r32()); break;
        case 0x40:case 0x41:case 0x42:case 0x43:case 0x44:case 0x45:case 0x46:case 0x47: t=fmt("INC     %s",R32[op-0x40]); break;
        case 0x48:case 0x49:case 0x4A:case 0x4B:case 0x4C:case 0x4D:case 0x4E:case 0x4F: t=fmt("DEC     %s",R32[op-0x48]); break;
        case 0x50:case 0x51:case 0x52:case 0x53:case 0x54:case 0x55:case 0x56:case 0x57: t=fmt("PUSH    %s",R32[op-0x50]); break;
        case 0x58:case 0x59:case 0x5A:case 0x5B:case 0x5C:case 0x5D:case 0x5E:case 0x5F: t=fmt("POP     %s",R32[op-0x58]); break;
        case 0x60: t="PUSHA"; break;
        case 0x61: t="POPA"; break;
        case 0x68: t=fmt("PUSH    0x%08X",r32()); break;
        case 0x6A: t=fmt("PUSH    0x%02X",(uint8_t)r8()); break;
        case 0x70:case 0x71:case 0x72:case 0x73:case 0x74:case 0x75:case 0x76:case 0x77:
        case 0x78:case 0x79:case 0x7A:case 0x7B:case 0x7C:case 0x7D:case 0x7E:case 0x7F:{
            int8_t d=r8(); t=fmt("%-6s  0x%08X",jcc[op-0x70],(uint32_t)(pos+d)); break; }
        case 0x80:case 0x82:{ uint8_t m=r8(),op2=(m>>3)&7; auto rm=decodeRM(m,false); t=fmt("%-6s  %s, 0x%02X",grp1[op2],rmStr(rm,false).c_str(),r8()); break; }
        case 0x81:{ uint8_t m=r8(),op2=(m>>3)&7; auto rm=decodeRM(m,true);  t=fmt("%-6s  %s, 0x%08X",grp1[op2],rmStr(rm,true).c_str(),r32()); break; }
        case 0x83:{ uint8_t m=r8(),op2=(m>>3)&7; auto rm=decodeRM(m,true);  t=fmt("%-6s  %s, 0x%02X",grp1[op2],rmStr(rm,true).c_str(),(uint8_t)r8()); break; }
        case 0x84:{ auto[s,r]=mkRM(false); t=fmt("TEST    %s, %s",s.c_str(),R8[(mem.read8(pos-1)>>3)&7]); break; }
        case 0x85:{ auto[s,r]=mkRM(true);  t=fmt("TEST    %s, %s",s.c_str(),R32[(mem.read8(pos-1)>>3)&7]); break; }
        case 0x86:{ auto[s,r]=mkRM(false); t=fmt("XCHG    %s, %s",s.c_str(),R8[(mem.read8(pos-1)>>3)&7]); break; }
        case 0x87:{ auto[s,r]=mkRM(true);  t=fmt("XCHG    %s, %s",s.c_str(),R32[(mem.read8(pos-1)>>3)&7]); break; }
        case 0x88:{ auto[s,r]=mkRM(false); t=fmt("MOV     %s, %s",s.c_str(),R8[(mem.read8(pos-1)>>3)&7]); break; }
        case 0x89:{ auto[s,r]=mkRM(true);  t=fmt("MOV     %s, %s",s.c_str(),R32[(mem.read8(pos-1)>>3)&7]); break; }
        case 0x8A:{ auto[s,r]=mkRM(false); t=fmt("MOV     %s, %s",R8[(mem.read8(pos-1)>>3)&7],s.c_str()); break; }
        case 0x8B:{ auto[s,r]=mkRM(true);  t=fmt("MOV     %s, %s",R32[(mem.read8(pos-1)>>3)&7],s.c_str()); break; }
        case 0x8D:{ auto[s,r]=mkRM(true);  t=fmt("LEA     %s, %s",R32[(mem.read8(pos-1)>>3)&7],s.c_str()); break; }
        case 0x90: t="NOP"; break;
        case 0x91:case 0x92:case 0x93:case 0x94:case 0x95:case 0x96:case 0x97: t=fmt("XCHG    EAX, %s",R32[op-0x90]); break;
        case 0x98: t="CWDE"; break;
        case 0x99: t="CDQ"; break;
        case 0x9C: t="PUSHF"; break;
        case 0x9D: t="POPF"; break;
        case 0xA0: t=fmt("MOV     AL, [0x%08X]",r32()); break;
        case 0xA1: t=fmt("MOV     EAX, [0x%08X]",r32()); break;
        case 0xA2: t=fmt("MOV     [0x%08X], AL",r32()); break;
        case 0xA3: t=fmt("MOV     [0x%08X], EAX",r32()); break;
        case 0xA4: t="MOVSB"; break;
        case 0xA5: t="MOVSD"; break;
        case 0xA8: t=fmt("TEST    AL, 0x%02X",r8()); break;
        case 0xA9: t=fmt("TEST    EAX, 0x%08X",r32()); break;
        case 0xAA: t="STOSB"; break;
        case 0xAB: t="STOSD"; break;
        case 0xAC: t="LODSB"; break;
        case 0xAD: t="LODSD"; break;
        case 0xAE: t="SCASB"; break;
        case 0xAF: t="SCASD"; break;
        case 0xB0:case 0xB1:case 0xB2:case 0xB3:case 0xB4:case 0xB5:case 0xB6:case 0xB7: t=fmt("MOV     %s, 0x%02X",R8[op-0xB0],r8()); break;
        case 0xB8:case 0xB9:case 0xBA:case 0xBB:case 0xBC:case 0xBD:case 0xBE:case 0xBF: t=fmt("MOV     %s, 0x%08X",R32[op-0xB8],r32()); break;
        case 0xC0:{ uint8_t m=r8(),op2=(m>>3)&7; auto rm=decodeRM(m,false); t=fmt("%-6s  %s, %d",grp2[op2],rmStr(rm,false).c_str(),r8()); break; }
        case 0xC1:{ uint8_t m=r8(),op2=(m>>3)&7; auto rm=decodeRM(m,true);  t=fmt("%-6s  %s, %d",grp2[op2],rmStr(rm,true).c_str(),r8()); break; }
        case 0xC2: t=fmt("RET     %d",r16()); break;
        case 0xC3: t="RET"; break;
        case 0xC6:{ auto[s,r]=mkRM(false); t=fmt("MOV     %s, 0x%02X",s.c_str(),r8()); break; }
        case 0xC7:{ auto[s,r]=mkRM(true);  t=fmt("MOV     %s, 0x%08X",s.c_str(),r32()); break; }
        case 0xC9: t="LEAVE"; break;
        case 0xCC: t="INT     3"; break;
        case 0xCD: t=fmt("INT     0x%02X",r8()); break;
        case 0xCF: t="IRET"; break;
        case 0xD0:{ uint8_t m=r8(),op2=(m>>3)&7; auto rm=decodeRM(m,false); t=fmt("%-6s  %s, 1",grp2[op2],rmStr(rm,false).c_str()); break; }
        case 0xD1:{ uint8_t m=r8(),op2=(m>>3)&7; auto rm=decodeRM(m,true);  t=fmt("%-6s  %s, 1",grp2[op2],rmStr(rm,true).c_str()); break; }
        case 0xD2:{ uint8_t m=r8(),op2=(m>>3)&7; auto rm=decodeRM(m,false); t=fmt("%-6s  %s, CL",grp2[op2],rmStr(rm,false).c_str()); break; }
        case 0xD3:{ uint8_t m=r8(),op2=(m>>3)&7; auto rm=decodeRM(m,true);  t=fmt("%-6s  %s, CL",grp2[op2],rmStr(rm,true).c_str()); break; }
        case 0xE0:{ int8_t d=r8(); t=fmt("LOOPNZ  0x%08X",(uint32_t)(pos+d)); break; }
        case 0xE1:{ int8_t d=r8(); t=fmt("LOOPZ   0x%08X",(uint32_t)(pos+d)); break; }
        case 0xE2:{ int8_t d=r8(); t=fmt("LOOP    0x%08X",(uint32_t)(pos+d)); break; }
        case 0xE3:{ int8_t d=r8(); t=fmt("JCXZ    0x%08X",(uint32_t)(pos+d)); break; }
        case 0xE4: t=fmt("IN      AL, 0x%02X",r8()); break;
        case 0xE6: t=fmt("OUT     0x%02X, AL",r8()); break;
        case 0xE8:{ int32_t d=r32(); t=fmt("CALL    0x%08X",(uint32_t)(pos+d)); break; }
        case 0xE9:{ int32_t d=r32(); t=fmt("JMP     0x%08X",(uint32_t)(pos+d)); break; }
        case 0xEB:{ int8_t d=r8(); t=fmt("JMP     0x%08X",(uint32_t)(pos+d)); break; }
        case 0xEC: t="IN      AL, DX"; break;
        case 0xEE: t="OUT     DX, AL"; break;
        case 0xF4: t="HLT"; break;
        case 0xF5: t="CMC"; break;
        case 0xF6:{ uint8_t m=r8(),op2=(m>>3)&7; auto rm=decodeRM(m,false);
            if(op2<2){ t=fmt("TEST    %s, 0x%02X",rmStr(rm,false).c_str(),r8()); }
            else { t=fmt("%-6s  %s",grp3[op2],rmStr(rm,false).c_str()); } break; }
        case 0xF7:{ uint8_t m=r8(),op2=(m>>3)&7; auto rm=decodeRM(m,true);
            if(op2<2){ t=fmt("TEST    %s, 0x%08X",rmStr(rm,true).c_str(),r32()); }
            else { t=fmt("%-6s  %s",grp3[op2],rmStr(rm,true).c_str()); } break; }
        case 0xF8: t="CLC"; break;
        case 0xF9: t="STC"; break;
        case 0xFA: t="CLI"; break;
        case 0xFB: t="STI"; break;
        case 0xFC: t="CLD"; break;
        case 0xFD: t="STD"; break;
        case 0xFE:{ uint8_t m=r8(),op2=(m>>3)&7; auto rm=decodeRM(m,false); t=fmt("%-6s  %s",op2?"DEC":"INC",rmStr(rm,false).c_str()); break; }
        case 0xFF:{ uint8_t m=r8(),op2=(m>>3)&7; auto rm=decodeRM(m,true);
            static const char* g5[]={"INC","DEC","CALL","CALL FAR","JMP","JMP FAR","PUSH","???"};
            t=fmt("%-6s  %s",g5[op2],rmStr(rm,true).c_str()); break; }
        case 0x0F:{
            uint8_t op2=r8();
            if (op2>=0x80&&op2<=0x8F) { int32_t d=r32(); t=fmt("%-6s  0x%08X",jcc[op2-0x80],(uint32_t)(pos+d)); }
            else if(op2==0xAF){ auto[s,rr]=mkRM(true); t=fmt("IMUL    %s, %s",R32[(mem.read8(pos-1)>>3)&7],s.c_str()); }
            else if(op2==0xB6){ auto[s,rr]=mkRM(false); t=fmt("MOVZX   %s, %s",R32[(mem.read8(pos-1)>>3)&7],s.c_str()); }
            else if(op2==0xB7){ auto[s,rr]=mkRM(true);  t=fmt("MOVZX   %s, %s",R32[(mem.read8(pos-1)>>3)&7],s.c_str()); }
            else if(op2==0xBE){ auto[s,rr]=mkRM(false); t=fmt("MOVSX   %s, %s",R32[(mem.read8(pos-1)>>3)&7],s.c_str()); }
            else if(op2==0xBF){ auto[s,rr]=mkRM(true);  t=fmt("MOVSX   %s, %s",R32[(mem.read8(pos-1)>>3)&7],s.c_str()); }
            else t=fmt("0F %02X ???",op2);
            break; }
        default: t=fmt("DB      0x%02X",op); break;
        }

        line.len = (int)(pos - addr);

        // build hex bytes string
        char hexbuf[64]=""; int hl=0;
        for (int i=0; i<line.len && i<8; i++)
            hl+=snprintf(hexbuf+hl,sizeof(hexbuf)-hl,"%02X ",mem.read8(addr+i));
        line.bytes = hexbuf;
        line.text  = t;
        return line;
    }

    std::vector<DisasmLine> disassemble(uint32_t addr, int count) {
        std::vector<DisasmLine> result;
        for (int i=0; i<count && addr<MEM_SIZE-16; i++) {
            DisasmLine l = decode(addr);
            result.push_back(l);
            addr += l.len;
        }
        return result;
    }
};

inline const char* Disasm::R32[8] = {"EAX","ECX","EDX","EBX","ESP","EBP","ESI","EDI"};
inline const char* Disasm::R8 [8] = {"AL","CL","DL","BL","AH","CH","DH","BH"};
