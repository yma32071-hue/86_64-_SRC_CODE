#define CPPHTTPLIB_NO_COMPRESS 1
#include "httplib.h"
#include "json.hpp"
#include "include/emulator.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>

using json = nlohmann::json;

// ---- Global emulator instance ----
static Emulator emu;

// ---- Helper: read static file ----
static std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

// ---- JSON helpers ----
static json cpuState() {
    auto& c = emu.cpu;
    json j;
    j["eax"] = c.regs[0]; j["ecx"] = c.regs[1]; j["edx"] = c.regs[2]; j["ebx"] = c.regs[3];
    j["esp"] = c.regs[4]; j["ebp"] = c.regs[5]; j["esi"] = c.regs[6]; j["edi"] = c.regs[7];
    j["eip"] = c.eip;
    j["eflags"] = c.flags.toEFLAGS();
    j["cf"]  = c.flags.CF; j["pf"] = c.flags.PF; j["af"]  = c.flags.AF;
    j["zf"]  = c.flags.ZF; j["sf"] = c.flags.SF; j["tf"]  = c.flags.TF;
    j["if_"] = c.flags.IF; j["df"] = c.flags.DF; j["of"]  = c.flags.OF;
    j["halted"]  = c.halted;
    j["running"] = c.running;
    j["cycles"]  = c.cycleCount;
    j["cursorRow"] = (int)emu.dec.cursorRow;
    j["cursorCol"] = (int)emu.dec.cursorCol;
    return j;
}

static json vgaScreen() {
    const uint8_t* buf = emu.mem.vgaText();
    json rows = json::array();
    for (int row = 0; row < 25; row++) {
        json cols = json::array();
        for (int col = 0; col < 80; col++) {
            int off = (row*80+col)*2;
            json cell;
            cell["ch"]   = buf[off];
            cell["attr"] = buf[off+1];
            cols.push_back(cell);
        }
        rows.push_back(cols);
    }
    return rows;
}

// ---- Demo programs (flat 32-bit x86 machine code) ----

// Hello World: prints "Hello, World! :-)" via INT 10h AH=0E, then HLT
static const uint8_t prog_hello[] = {
    // MOV ESI, 0x1013  (address of string after code)
    0xBE, 0x13, 0x10, 0x00, 0x00,
    // .loop: MOV AL, [ESI]
    0x8A, 0x06,
    // TEST AL, AL
    0x84, 0xC0,
    // JZ .done   (+8 bytes to HLT)
    0x74, 0x08,
    // MOV AH, 0x0E
    0xB4, 0x0E,
    // INT 10h
    0xCD, 0x10,
    // INC ESI
    0x46,
    // JMP .loop  (back 13 bytes)
    0xEB, 0xF3,
    // HLT
    0xF4,
    // String data at 0x1013:
    'H','e','l','l','o',',',' ','W','o','r','l','d','!',' ',':','-',')','\n',0
};

// Fibonacci: fills 0x2000..0x205C with first 24 Fibonacci numbers, then HLT
static const uint8_t prog_fib[] = {
    // XOR EAX, EAX
    0x31, 0xC0,
    // MOV EBX, 1
    0xBB, 0x01, 0x00, 0x00, 0x00,
    // MOV ECX, 24
    0xB9, 0x18, 0x00, 0x00, 0x00,
    // MOV EDI, 0x2000
    0xBF, 0x00, 0x20, 0x00, 0x00,
    // .loop: MOV [EDI], EAX
    0x89, 0x07,
    // ADD EDI, 4
    0x83, 0xC7, 0x04,
    // MOV EDX, EAX
    0x89, 0xC2,
    // ADD EDX, EBX
    0x01, 0xDA,
    // MOV EAX, EBX
    0x89, 0xD8,
    // MOV EBX, EDX
    0x89, 0xD3,
    // LOOP .loop  (back 15 bytes)
    0xE2, 0xF1,
    // Print results: MOV ESI, 0x2000
    0xBE, 0x00, 0x20, 0x00, 0x00,
    // MOV ECX, 24
    0xB9, 0x18, 0x00, 0x00, 0x00,
    // .print: MOV EAX, [ESI]
    0x8B, 0x06,
    // Rough decimal print subroutine would be very long; just HLT here
    // The user can view the memory at 0x2000 in the hex viewer
    0x83, 0xC6, 0x04,
    // LOOP .print
    0xE2, 0xFA,
    // HLT
    0xF4
};

// Counter: counts to 1000000 in EAX, stores result at 0x3000, then HLT
static const uint8_t prog_counter[] = {
    // XOR EAX, EAX
    0x31, 0xC0,
    // MOV ECX, 0xF4240   (1000000)
    0xB9, 0x40, 0x42, 0x0F, 0x00,
    // .loop: INC EAX
    0x40,
    // LOOP .loop  (-2 from next instr = back to INC)
    0xE2, 0xFD,
    // MOV [0x3000], EAX
    0xA3, 0x00, 0x30, 0x00, 0x00,
    // HLT
    0xF4
};

// Sieve of Eratosthenes: mark primes in memory 0x4000..0x43E7 (1000 bytes), HLT
static const uint8_t prog_sieve[] = {
    // Fill 0x4000..0x43E7 with 1 (prime) using STOSB
    // MOV EDI, 0x4000
    0xBF, 0x00, 0x40, 0x00, 0x00,
    // MOV ECX, 1000
    0xB9, 0xE8, 0x03, 0x00, 0x00,
    // MOV AL, 1
    0xB0, 0x01,
    // REP STOSB
    0xF3, 0xAA,
    // Mark 0 as not prime: MOV byte [0x4000], 0
    0xC6, 0x05, 0x00, 0x40, 0x00, 0x00, 0x00,
    // Mark 1 as not prime: MOV byte [0x4001], 0
    0xC6, 0x05, 0x01, 0x40, 0x00, 0x00, 0x00,
    // EBX = 2 (outer loop)
    0xBB, 0x02, 0x00, 0x00, 0x00,
    // outer: CMP EBX, 32  (sqrt(1000))
    0x83, 0xFB, 0x20,
    // JGE done
    0x0F, 0x8D, 0x2A, 0x00, 0x00, 0x00,
    // CMP byte [0x4000+EBX], 0  ; check if EBX is prime
    0x80, 0xBB, 0x00, 0x40, 0x00, 0x00, 0x00,
    // JZ skip (not prime)
    0x74, 0x16,
    // inner: EDX = EBX*EBX
    0x89, 0xD8,  // MOV EAX, EBX
    0x0F, 0xAF, 0xC3,  // IMUL EAX, EBX
    0x89, 0xC2,  // MOV EDX, EAX
    // inner loop: MOV byte [0x4000+EDX], 0
    0xC6, 0x82, 0x00, 0x40, 0x00, 0x00, 0x00,
    // ADD EDX, EBX
    0x01, 0xDA,
    // CMP EDX, 1000
    0x81, 0xFA, 0xE8, 0x03, 0x00, 0x00,
    // JL inner  (back 16)
    0x7C, 0xED,
    // skip: INC EBX
    0x43,
    // JMP outer  (back 37)
    0xEB, 0xD5,
    // done: HLT
    0xF4
};

// Colors: fills VGA screen with colored characters
static const uint8_t prog_colors[] = {
    // MOV EDI, 0xB8000  (VGA text buffer)
    0xBF, 0x00, 0x80, 0x0B, 0x00,
    // MOV ECX, 80*25 = 2000
    0xB9, 0xD0, 0x07, 0x00, 0x00,
    // MOV BL, 0x01  (color attribute start)
    0xB3, 0x01,
    // MOV AL, 0x58  ('X')
    0xB0, 0x58,
    // .loop: MOV [EDI], AL
    0x88, 0x07,
    // MOV [EDI+1], BL
    0x88, 0x5F, 0x01,
    // ADD EDI, 2
    0x83, 0xC7, 0x02,
    // INC BL
    0xFE, 0xC3,
    // CMP BL, 0x80
    0x80, 0xFB, 0x80,
    // JNZ skip
    0x75, 0x03,
    // MOV BL, 0x01
    0xB3, 0x01,
    // LOOP .loop
    0xE2, 0xE9,
    // HLT
    0xF4
};

int main() {
    std::cout << "x86_64 Emulator GUI starting on port 5000..." << std::endl;

    // Pre-load Hello World demo
    emu.mem.load(0x1000, prog_hello, sizeof(prog_hello));
    emu.cpu.eip = 0x1000;

    httplib::Server svr;

    // ---- Serve frontend ----
    svr.Get("/", [](const httplib::Request&, httplib::Response& res) {
        std::string html = readFile("static/index.html");
        if (html.empty()) html = "<h1>index.html not found</h1>";
        res.set_content(html, "text/html");
    });

    // ---- API: GET /api/state ----
    svr.Get("/api/state", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin","*");
        res.set_content(cpuState().dump(), "application/json");
    });

    // ---- API: GET /api/screen ----
    svr.Get("/api/screen", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin","*");
        res.set_content(vgaScreen().dump(), "application/json");
    });

    // ---- API: GET /api/memory?addr=&len= ----
    svr.Get("/api/memory", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin","*");
        uint32_t addr = 0, len = 256;
        if (req.has_param("addr")) addr = (uint32_t)std::stoul(req.get_param_value("addr"), nullptr, 0);
        if (req.has_param("len"))  len  = (uint32_t)std::stoul(req.get_param_value("len"),  nullptr, 0);
        if (len > 4096) len = 4096;
        auto bytes = emu.readMem(addr, len);
        json j; j["addr"] = addr; j["data"] = bytes;
        res.set_content(j.dump(), "application/json");
    });

    // ---- API: GET /api/disasm?addr=&count= ----
    svr.Get("/api/disasm", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin","*");
        uint32_t addr = emu.cpu.eip;
        int count = 30;
        if (req.has_param("addr"))  addr  = (uint32_t)std::stoul(req.get_param_value("addr"),  nullptr, 0);
        if (req.has_param("count")) count = std::stoi(req.get_param_value("count"));
        if (count > 100) count = 100;
        auto lines = emu.disasm(addr, count);
        json arr = json::array();
        for (auto& l : lines) {
            json e;
            e["addr"]  = l.addr;
            e["len"]   = l.len;
            e["bytes"] = l.bytes;
            e["text"]  = l.text;
            arr.push_back(e);
        }
        res.set_content(arr.dump(), "application/json");
    });

    // ---- API: GET /api/log ----
    svr.Get("/api/log", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin","*");
        json j; j["io"] = emu.io.log; j["bios"] = emu.dec.biosLog;
        res.set_content(j.dump(), "application/json");
    });

    // ---- API: POST /api/step ----
    svr.Post("/api/step", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin","*");
        emu.step();
        res.set_content(cpuState().dump(), "application/json");
    });

    // ---- API: POST /api/run ----
    svr.Post("/api/run", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin","*");
        int steps = 100000;
        if (req.has_param("steps")) steps = std::stoi(req.get_param_value("steps"));
        int done = emu.runSteps(steps);
        json j = cpuState(); j["stepsRan"] = done;
        res.set_content(j.dump(), "application/json");
    });

    // ---- API: POST /api/reset ----
    svr.Post("/api/reset", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin","*");
        emu.reset(true);
        res.set_content(cpuState().dump(), "application/json");
    });

    // ---- API: POST /api/load ----
    // Body JSON: { "addr": 4096, "data": [0xBE, 0x13, ...], "program": "hello" }
    svr.Post("/api/load", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin","*");
        try {
            json body = json::parse(req.body);
            // Built-in programs
            if (body.contains("program")) {
                std::string prog = body["program"];
                if (prog == "hello") {
                    emu.reset(true); emu.mem.load(0x1000, prog_hello, sizeof(prog_hello)); emu.cpu.eip=0x1000;
                } else if (prog == "fibonacci") {
                    emu.reset(true); emu.mem.load(0x1000, prog_fib, sizeof(prog_fib)); emu.cpu.eip=0x1000;
                } else if (prog == "counter") {
                    emu.reset(true); emu.mem.load(0x1000, prog_counter, sizeof(prog_counter)); emu.cpu.eip=0x1000;
                } else if (prog == "sieve") {
                    emu.reset(true); emu.mem.load(0x1000, prog_sieve, sizeof(prog_sieve)); emu.cpu.eip=0x1000;
                } else if (prog == "colors") {
                    emu.reset(true); emu.mem.load(0x1000, prog_colors, sizeof(prog_colors)); emu.cpu.eip=0x1000;
                }
                res.set_content(cpuState().dump(), "application/json");
                return;
            }
            // Custom binary
            uint32_t addr = body.value("addr", 0x1000);
            std::vector<uint8_t> data;
            if (body.contains("data") && body["data"].is_array()) {
                for (auto& b : body["data"]) data.push_back((uint8_t)b.get<int>());
            } else if (body.contains("hex") && body["hex"].is_string()) {
                std::string hex = body["hex"];
                for (size_t i=0; i+1<hex.size(); i+=2) {
                    if (hex[i]==' ') { i--; continue; }
                    data.push_back((uint8_t)std::stoul(hex.substr(i,2),nullptr,16));
                }
            }
            if (!data.empty()) {
                emu.reset(true);
                emu.mem.load(addr, data.data(), data.size());
                emu.cpu.eip = addr;
                emu.cpu.halted = false;
            }
            res.set_content(cpuState().dump(), "application/json");
        } catch (...) {
            res.status = 400;
            res.set_content("{\"error\":\"bad request\"}", "application/json");
        }
    });

    svr.listen("0.0.0.0", 5000);
    return 0;
}
