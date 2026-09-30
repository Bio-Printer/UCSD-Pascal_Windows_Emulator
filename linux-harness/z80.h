// Minimal-but-real Z80 CPU core for the UCSD p-System interpreter bring-up.
// Covers: full unprefixed + CB-prefixed opcode set, the commonly-used ED
// opcodes (block instructions, IN/OUT, NEG, IM, 16-bit LD, ADC/SBC HL,rr,
// RETN/RETI), and the common DD/FD (IX/IY) forms used by 1978-era Z80
// assembly (LD reg,nn / LD (I?+d),n / INC/DEC (I?+d) / arithmetic with
// (I?+d) / JP (I?)). Undocumented/exotic opcodes are not implemented;
// they log and act as a NOP so exploration can continue instead of
// crashing outright.
#pragma once
#include <cstdint>
#include <cstdio>
#include <functional>

struct Z80Regs {
    uint8_t A=0,F=0,B=0,C=0,D=0,E=0,H=0,L=0;
    uint8_t A_=0,F_=0,B_=0,C_=0,D_=0,E_=0,H_=0,L_=0;
    uint16_t IX=0, IY=0, SP=0, PC=0;
    uint8_t I=0, R=0;
    bool IFF1=false, IFF2=false;
    uint8_t IM=0;
    bool halted=false;

    uint16_t BC() const { return (B<<8)|C; }
    uint16_t DE() const { return (D<<8)|E; }
    uint16_t HL() const { return (H<<8)|L; }
    void setBC(uint16_t v){ B=v>>8; C=v&0xFF; }
    void setDE(uint16_t v){ D=v>>8; E=v&0xFF; }
    void setHL(uint16_t v){ H=v>>8; L=v&0xFF; }
};

// Flag bits
enum { FS=0x80, FZ=0x40, F5=0x20, FH=0x10, F3=0x08, FPV=0x04, FN=0x02, FC=0x01 };

class Z80 {
public:
    Z80Regs r;
    // Memory access hooks (default: flat 64K array owned externally)
    std::function<uint8_t(uint16_t)> rd;
    std::function<void(uint16_t,uint8_t)> wr;
    std::function<uint8_t(uint8_t)> in_port;
    std::function<void(uint8_t,uint8_t)> out_port;
    long long tstates = 0;
    long long instructions_executed = 0;
    bool trace = false;

    void reset() { r = Z80Regs{}; r.SP = 0xFFFF; }

    // Execute exactly one instruction; returns t-states consumed.
    int step();

private:
    uint8_t fetch8() { return rd(r.PC++); }
    int8_t  fetch8s(){ return (int8_t)fetch8(); }
    uint16_t fetch16(){ uint8_t lo=fetch8(); uint8_t hi=fetch8(); return (hi<<8)|lo; }

    void push16(uint16_t v){ r.SP-=2; wr(r.SP, v&0xFF); wr((uint16_t)(r.SP+1), v>>8); }
    uint16_t pop16(){ uint8_t lo=rd(r.SP); uint8_t hi=rd((uint16_t)(r.SP+1)); r.SP+=2; return (hi<<8)|lo; }

    // flag helpers
    void setSZ(uint8_t v){ if(v&0x80) r.F|=FS; else r.F&=~FS; if(v==0) r.F|=FZ; else r.F&=~FZ; }
    // This target simulator (z80pack-derived) never implements the
    // undocumented flags 5/3 — no instruction in its source ever sets or
    // clears them. Matching that exactly (rather than modeling real Z80
    // silicon) means treating bits 5/3 as pure passthrough: never touched.
    void set53(uint8_t){ /* no-op: match reference simulator behavior */ }
    static bool parity(uint8_t v){ v^=v>>4; v^=v>>2; v^=v>>1; return !(v&1); }

    uint8_t inc8(uint8_t v);
    uint8_t dec8(uint8_t v);
    void add8(uint8_t v, bool carry=false);
    void sub8(uint8_t v, bool carry=false);
    void and8(uint8_t v);
    void or8(uint8_t v);
    void xor8(uint8_t v);
    void cp8(uint8_t v);
    uint16_t add16(uint16_t a, uint16_t b);
    uint16_t adc16(uint16_t a, uint16_t b);
    uint16_t sbc16(uint16_t a, uint16_t b);

    uint8_t& reg8(int code, uint16_t* idxReg=nullptr, int16_t disp=0); // 0..7 = B,C,D,E,H,L,(HL),A
    uint8_t readReg8(int code);
    void writeReg8(int code, uint8_t v);

    int execMain(uint8_t op);
    int execCB(uint8_t op, uint16_t* idxReg=nullptr, int8_t disp=0, bool haveDisp=false);
    int execED(uint8_t op);
    int execIndexed(uint16_t& idxReg); // DD/FD prefix handling
    int rotShiftCB(int which, uint8_t v, uint8_t& out);
};
