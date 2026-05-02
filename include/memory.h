#pragma once
#include <cstdint>
#include <vector>
#include <cstring>

static const uint32_t MEM_SIZE      = 8u * 1024 * 1024;  // 8 MB
static const uint32_t VGA_TEXT_BASE = 0xB8000u;
static const uint32_t VGA_TEXT_SIZE = 80 * 25 * 2;       // 4 000 bytes

class Memory {
public:
    std::vector<uint8_t> data;

    Memory() : data(MEM_SIZE, 0) { clearVGA(); }

    void reset() {
        std::fill(data.begin(), data.end(), 0);
        clearVGA();
    }

    void clearVGA() {
        for (uint32_t i = VGA_TEXT_BASE; i < VGA_TEXT_BASE + VGA_TEXT_SIZE; i += 2) {
            data[i]   = 0x20;  // space
            data[i+1] = 0x07;  // light-gray on black
        }
    }

    // ---- byte / word / dword access ----
    uint8_t  read8 (uint32_t a) const { return (a < MEM_SIZE) ? data[a] : 0xFF; }
    uint16_t read16(uint32_t a) const {
        if (a+1 >= MEM_SIZE) return 0xFFFF;
        return (uint16_t)data[a] | ((uint16_t)data[a+1]<<8);
    }
    uint32_t read32(uint32_t a) const {
        if (a+3 >= MEM_SIZE) return 0xFFFFFFFF;
        return (uint32_t)data[a]        | ((uint32_t)data[a+1]<<8)
             | ((uint32_t)data[a+2]<<16)| ((uint32_t)data[a+3]<<24);
    }

    void write8 (uint32_t a, uint8_t  v) { if (a < MEM_SIZE) data[a] = v; }
    void write16(uint32_t a, uint16_t v) {
        if (a+1 >= MEM_SIZE) return;
        data[a] = v & 0xFF; data[a+1] = v >> 8;
    }
    void write32(uint32_t a, uint32_t v) {
        if (a+3 >= MEM_SIZE) return;
        data[a]=v&0xFF; data[a+1]=(v>>8)&0xFF;
        data[a+2]=(v>>16)&0xFF; data[a+3]=(v>>24)&0xFF;
    }

    void load(uint32_t addr, const uint8_t* src, size_t len) {
        for (size_t i = 0; i < len && (addr+i) < MEM_SIZE; i++)
            data[addr+i] = src[i];
    }

    const uint8_t* vgaText() const { return data.data() + VGA_TEXT_BASE; }
};
