#pragma once
#include <cstdint>
#include <string>
#include <vector>

class IO {
public:
    uint8_t ports[65536] = {};
    std::vector<std::string> log;

    uint8_t  read8 (uint16_t p)             { return ports[p]; }
    uint16_t read16(uint16_t p)             { return ports[p] | ((uint16_t)ports[p+1]<<8); }

    void write8 (uint16_t p, uint8_t v)     { ports[p] = v;  addLog("OUT", p, v); }
    void write16(uint16_t p, uint16_t v)    { ports[p]=v&0xFF; ports[p+1]=v>>8; addLog("OUT16",p,v); }

    void reset() { memset(ports, 0, sizeof(ports)); log.clear(); }

private:
    void addLog(const char* op, uint16_t p, uint32_t v) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%s 0x%04X = 0x%02X", op, p, v);
        log.push_back(buf);
        if (log.size() > 500) log.erase(log.begin());
    }
};
