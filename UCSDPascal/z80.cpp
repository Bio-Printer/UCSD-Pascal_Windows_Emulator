#include "z80.h"

// ---- 8-bit ALU helpers -----------------------------------------------

uint8_t Z80::inc8(uint8_t v){
    uint8_t res = v+1;
    r.F = (r.F & FC); // preserve carry
    if(res & 0x80) r.F |= FS;
    if(res==0) r.F |= FZ;
    if((v & 0x0F)==0x0F) r.F |= FH;
    if(v==0x7F) r.F |= FPV;
    set53(res);
    return res;
}
uint8_t Z80::dec8(uint8_t v){
    uint8_t res = v-1;
    r.F = (r.F & FC) | FN;
    if(res & 0x80) r.F |= FS;
    if(res==0) r.F |= FZ;
    if((v & 0x0F)==0x00) r.F |= FH;
    if(v==0x80) r.F |= FPV;
    set53(res);
    return res;
}
void Z80::add8(uint8_t v, bool carry){
    int c = carry ? (r.F&FC) : 0;
    int res = r.A + v + c;
    uint8_t res8 = (uint8_t)res;
    r.F = 0;
    if(res8 & 0x80) r.F |= FS;
    if(res8==0) r.F |= FZ;
    if(((r.A & 0xF)+(v & 0xF)+c) > 0xF) r.F |= FH;
    if(res > 0xFF) r.F |= FC;
    if((~(r.A ^ v)) & (r.A ^ res8) & 0x80) r.F |= FPV;
    set53(res8);
    r.A = res8;
}
void Z80::sub8(uint8_t v, bool carry){
    int c = carry ? (r.F&FC) : 0;
    int res = r.A - v - c;
    uint8_t res8 = (uint8_t)res;
    r.F = FN;
    if(res8 & 0x80) r.F |= FS;
    if(res8==0) r.F |= FZ;
    if(((r.A & 0xF) - (v & 0xF) - c) < 0) r.F |= FH;
    if(res < 0) r.F |= FC;
    if((r.A ^ v) & (r.A ^ res8) & 0x80) r.F |= FPV;
    set53(res8);
    r.A = res8;
}
void Z80::and8(uint8_t v){ r.A &= v; r.F = FH; setSZ(r.A); if(parity(r.A)) r.F|=FPV; set53(r.A); }
void Z80::or8(uint8_t v){ r.A |= v; r.F = 0; setSZ(r.A); if(parity(r.A)) r.F|=FPV; set53(r.A); }
void Z80::xor8(uint8_t v){ r.A ^= v; r.F = 0; setSZ(r.A); if(parity(r.A)) r.F|=FPV; set53(r.A); }
void Z80::cp8(uint8_t v){
    uint8_t a = r.A;
    sub8(v,false);
    r.A = a; // CP doesn't store result
    set53(v); // undocumented flags come from operand for CP; close enough here
}
uint16_t Z80::add16(uint16_t a, uint16_t b){
    int res = a+b;
    r.F &= (FS|FZ|F5|F3|FPV); // ADD HL,rr: S,Z,5,3,P/V unaffected; only H,N,C change
    if(((a&0xFFF)+(b&0xFFF)) > 0xFFF) r.F |= FH;
    if(res > 0xFFFF) r.F |= FC;
    return (uint16_t)res;
}
uint16_t Z80::adc16(uint16_t a, uint16_t b){
    int carry = r.F & FC;
    int lcarry = ((a&0xFF)+(b&0xFF) > 255) ? 1 : 0;
    long i = (long)a + (long)b + carry;
    r.F = 0;
    if( ((a>>8)&0xF) + ((b>>8)&0xF) + carry + lcarry > 0xF ) r.F |= FH;
    if( (a<0x8000) && (i>0x7FFFL) ) r.F |= FPV;
    if( i > 0xFFFFL ) r.F |= FC;
    if( i ) {} else r.F |= FZ; // matches reference: checks untruncated i, not the truncated 16-bit result
    uint16_t res = (uint16_t)i;
    if( res & 0x8000 ) r.F |= FS;
    return res;
}
uint16_t Z80::sbc16(uint16_t a, uint16_t b){
    int carry = r.F & FC;
    int lcarry = ((b&0xFF) > (a&0xFF)) ? 1 : 0;
    long i = (long)a - (long)b - carry;
    r.F = FN;
    if( ((b>>8)&0xF) + carry + lcarry > ((a>>8)&0xF) ) r.F |= FH;
    if( (a>0x7FFF) && (i<0x8000L) ) r.F |= FPV;
    if( i < 0L ) r.F |= FC;
    if( i ) {} else r.F |= FZ;
    uint16_t res = (uint16_t)i;
    if( res & 0x8000 ) r.F |= FS;
    return res;
}

// ---- register file decode (unprefixed): 0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A
uint8_t Z80::readReg8(int code){
    switch(code){
        case 0: return r.B; case 1: return r.C; case 2: return r.D; case 3: return r.E;
        case 4: return r.H; case 5: return r.L; case 6: return rd(r.HL()); case 7: return r.A;
    }
    return 0;
}
void Z80::writeReg8(int code, uint8_t v){
    switch(code){
        case 0: r.B=v; break; case 1: r.C=v; break; case 2: r.D=v; break; case 3: r.E=v; break;
        case 4: r.H=v; break; case 5: r.L=v; break; case 6: wr(r.HL(),v); break; case 7: r.A=v; break;
    }
}

// ---- CB-prefixed (rotate/shift/bit/res/set) ---------------------------
// Returns the resulting value; also updates flags for rot/shift ops.
int Z80::rotShiftCB(int which, uint8_t v, uint8_t& out){
    uint8_t res=v; uint8_t carryOut=0;
    switch(which){
        case 0: carryOut=(v>>7)&1; res=(v<<1)|carryOut; break;              // RLC
        case 1: carryOut=v&1; res=(v>>1)|(carryOut<<7); break;              // RRC
        case 2: carryOut=(v>>7)&1; res=(v<<1)|(r.F&FC); break;              // RL
        case 3: carryOut=v&1; res=(v>>1)|((r.F&FC)<<7); break;              // RR
        case 4: carryOut=(v>>7)&1; res=(v<<1); break;                       // SLA
        case 5: carryOut=v&1; res=(uint8_t)((v>>1)|(v&0x80)); break;        // SRA
        case 6: carryOut=(v>>7)&1; res=(v<<1)|1; break;                     // SLL (undoc)
        case 7: carryOut=v&1; res=(v>>1); break;                            // SRL
    }
    r.F = 0;
    if(carryOut) r.F |= FC;
    setSZ(res);
    if(parity(res)) r.F |= FPV;
    set53(res);
    out = res;
    return res;
}

int Z80::execCB(uint8_t op, uint16_t* idxReg, int8_t disp, bool haveDisp){
    int x = op>>6, y=(op>>3)&7, z=op&7;
    uint16_t addr = haveDisp ? (uint16_t)(*idxReg + disp) : r.HL();
    uint8_t v = (z==6 || haveDisp) ? rd(addr) : readReg8(z);
    if(x==0){ // rot/shift
        uint8_t res; rotShiftCB(y, v, res);
        if(haveDisp){ wr(addr,res); if(z!=6) writeReg8(z,res); }
        else writeReg8(z, res);
        return 8;
    } else if(x==1){ // BIT y,r
        uint8_t bit = v & (1<<y);
        r.F = (r.F & FC) | FH;
        if(bit==0) r.F |= FZ|FPV;
        if(y==7 && bit) r.F |= FS;
        set53(v);
        return 8;
    } else if(x==2){ // RES y,r
        uint8_t res = v & ~(1<<y);
        if(haveDisp){ wr(addr,res); if(z!=6) writeReg8(z,res); } else writeReg8(z,res);
        return 8;
    } else { // SET y,r
        uint8_t res = v | (1<<y);
        if(haveDisp){ wr(addr,res); if(z!=6) writeReg8(z,res); } else writeReg8(z,res);
        return 8;
    }
}

// ---- ED-prefixed (common subset) --------------------------------------
int Z80::execED(uint8_t op){
    switch(op){
        case 0x47: r.I=r.A; return 9;                  // LD I,A
        case 0x4F: r.R=r.A; return 9;                  // LD R,A
        case 0x57: r.A=r.I; r.F=(r.F&FC); setSZ(r.A); if(r.IFF2) r.F|=FPV; set53(r.A); return 9; // LD A,I
        case 0x5F: r.A=r.R; r.F=(r.F&FC); setSZ(r.A); if(r.IFF2) r.F|=FPV; set53(r.A); return 9; // LD A,R
        case 0x44: case 0x4C: case 0x54: case 0x5C: case 0x64: case 0x6C: case 0x74: case 0x7C: { // NEG
            uint8_t a=r.A; r.A=0; sub8(a,false); return 8;
        }
        case 0x46: case 0x4E: case 0x66: case 0x6E: r.IM=0; return 8;
        case 0x56: case 0x76: r.IM=1; return 8;
        case 0x5E: case 0x7E: r.IM=2; return 8;
        case 0x45: case 0x4D: case 0x55: case 0x5D: case 0x65: case 0x6D: case 0x75: case 0x7D: // RETN/RETI
            r.IFF1 = r.IFF2; r.PC = pop16(); return 14;
        case 0xA0: { // LDI
            uint8_t v=rd(r.HL()); wr(r.DE(),v);
            r.setHL(r.HL()+1); r.setDE(r.DE()+1); r.setBC(r.BC()-1);
            r.F &= ~(FN|FH); if(r.BC()) r.F|=FPV; else r.F&=~FPV;
            return 16;
        }
        case 0xB0: { // LDIR: reference simulator runs the whole repeat in one instruction
            uint32_t cnt = r.BC();
            uint16_t d = r.DE(), s = r.HL();
            if(cnt==0) cnt = 0x10000; // BC=0 means 65536 iterations on real hardware
            for(uint32_t k=0;k<cnt;k++){ wr(d, rd(s)); d++; s++; }
            r.setDE(d); r.setHL(s); r.setBC(0);
            r.F &= ~(FN|FPV|FH);
            return 21*cnt - 5; // matches reference t-state accounting closely enough
        }
        case 0xA8: { // LDD
            uint8_t v=rd(r.HL()); wr(r.DE(),v);
            r.setHL(r.HL()-1); r.setDE(r.DE()-1); r.setBC(r.BC()-1);
            r.F &= ~(FN|FH); if(r.BC()) r.F|=FPV; else r.F&=~FPV;
            return 16;
        }
        case 0xB8: { // LDDR
            uint32_t cnt = r.BC();
            uint16_t d = r.DE(), s = r.HL();
            if(cnt==0) cnt = 0x10000;
            for(uint32_t k=0;k<cnt;k++){ wr(d, rd(s)); d--; s--; }
            r.setDE(d); r.setHL(s); r.setBC(0);
            r.F &= ~(FN|FPV|FH);
            return 21*cnt - 5;
        }
        case 0xA1: { // CPI
            uint8_t operand=rd(r.HL());
            r.F = (r.F & FC);
            if((operand&0xF) > (r.A&0xF)) r.F |= FH;
            uint8_t res = r.A - operand;
            r.setHL(r.HL()+1); r.setBC(r.BC()-1);
            r.F |= FN;
            if(r.BC()) r.F|=FPV;
            if(res==0) r.F|=FZ;
            if(res&0x80) r.F|=FS;
            return 16;
        }
        case 0xB1: { // CPIR (H flag intentionally not computed, per reference)
            uint32_t cnt = r.BC();
            uint16_t s = r.HL();
            uint8_t res=0;
            if(cnt==0) cnt = 0x10000;
            uint32_t k=0;
            do { res = r.A - rd(s); s++; k++; } while(k<cnt && res!=0);
            r.setHL(s); r.setBC((uint16_t)(r.BC()-k));
            r.F |= FN;
            if(r.BC()) r.F|=FPV; else r.F&=~FPV;
            if(res==0) r.F|=FZ; else r.F&=~FZ;
            if(res&0x80) r.F|=FS; else r.F&=~FS;
            return 16*k;
        }
        case 0xA9: { // CPD
            uint8_t operand=rd(r.HL());
            r.F = (r.F & FC);
            if((operand&0xF) > (r.A&0xF)) r.F |= FH;
            uint8_t res = r.A - operand;
            r.setHL(r.HL()-1); r.setBC(r.BC()-1);
            r.F |= FN;
            if(r.BC()) r.F|=FPV;
            if(res==0) r.F|=FZ;
            if(res&0x80) r.F|=FS;
            return 16;
        }
        case 0xB9: { // CPDR (H flag intentionally not computed, per reference)
            uint32_t cnt = r.BC();
            uint16_t s = r.HL();
            uint8_t res=0;
            if(cnt==0) cnt = 0x10000;
            uint32_t k=0;
            do { res = r.A - rd(s); s--; k++; } while(k<cnt && res!=0);
            r.setHL(s); r.setBC((uint16_t)(r.BC()-k));
            r.F |= FN;
            if(r.BC()) r.F|=FPV; else r.F&=~FPV;
            if(res==0) r.F|=FZ; else r.F&=~FZ;
            if(res&0x80) r.F|=FS; else r.F&=~FS;
            return 16*k;
        }
        case 0x6F: { // RLD
            uint8_t m = rd(r.HL());
            uint8_t j = r.A & 0x0F;
            r.A = (r.A & 0xF0) | (m >> 4);
            m = (uint8_t)(m << 4) | j;
            wr(r.HL(), m);
            r.F &= ~(FH|FN);
            if(r.A) r.F &= ~FZ; else r.F |= FZ;
            if(r.A & 0x80) r.F |= FS; else r.F &= ~FS;
            if(parity(r.A)) r.F |= FPV; else r.F &= ~FPV;
            return 18;
        }
        case 0x67: { // RRD
            uint8_t m = rd(r.HL());
            uint8_t j = r.A & 0x0F;
            r.A = (r.A & 0xF0) | (m & 0x0F);
            m = (uint8_t)(m >> 4) | (uint8_t)(j << 4);
            wr(r.HL(), m);
            r.F &= ~(FH|FN);
            if(r.A) r.F &= ~FZ; else r.F |= FZ;
            if(r.A & 0x80) r.F |= FS; else r.F &= ~FS;
            if(parity(r.A)) r.F |= FPV; else r.F &= ~FPV;
            return 18;
        }
        default: break;
    }
    // 16-bit LD (nn),rr / LD rr,(nn) and ADC/SBC HL,rr and IN/OUT r,(C) — regular pattern
    int x=op>>6, y=(op>>3)&7, z=op&7;
    if(x==1){
        auto rp = [&](int p)->uint16_t{ switch(p){case 0:return r.BC();case 1:return r.DE();case 2:return r.HL();case 3:return r.SP;} return 0; };
        auto setrp = [&](int p, uint16_t v){ switch(p){case 0:r.setBC(v);break;case 1:r.setDE(v);break;case 2:r.setHL(v);break;case 3:r.SP=v;break;} };
        int p = y>>1; bool q = y&1;
        switch(z){
            case 0: { // IN r,(C) / IN (C)
                uint8_t v = in_port ? in_port(r.C) : 0xFF;
                if(y!=6) writeReg8(y,v);
                r.F=(r.F&FC); setSZ(v); if(parity(v)) r.F|=FPV; set53(v);
                return 12;
            }
            case 1: { uint8_t v = (y==6)?0:readReg8(y); if(out_port) out_port(r.C, v); return 12; } // OUT (C),r
            case 2: if(!q) return 15,(r.setHL(sbc16(r.HL(), rp(p))),15); else { r.setHL(adc16(r.HL(), rp(p))); return 15; }
            case 3: { uint16_t addr=fetch16(); if(!q){ wr(addr, rp(p)&0xFF); wr((uint16_t)(addr+1), rp(p)>>8);} else { uint8_t lo=rd(addr),hi=rd((uint16_t)(addr+1)); setrp(p, (hi<<8)|lo);} return 20; }
            default: break;
        }
    }
    // Unimplemented ED opcode: treat as 2-byte NOP so we can keep going.
    if(trace) fprintf(stderr, "[z80] unimplemented ED %02X at PC=%04X\n", op, r.PC-2);
    return 8;
}

// ---- DD/FD (index register) prefix ------------------------------------
// Handles the common subset: LD IX,nn ; LD (IX+d),n/r ; LD r,(IX+d) ;
// INC/DEC (IX+d) ; ADD/ADC/SUB/SBC/AND/XOR/OR/CP (IX+d) ; INC/DEC IX ;
// ADD IX,rr ; PUSH/POP IX ; JP (IX) ; EX (SP),IX ; plus CB-with-displacement.
int Z80::execIndexed(uint16_t& idx){
    uint8_t op = fetch8();
    if(op==0xCB){
        int8_t disp = fetch8s();
        uint8_t sub = fetch8();
        return execCB(sub, &idx, disp, true) + 8;
    }
    // Substituting H/L codes 4/5 with high/low byte of the index register is
    // the classic DD/FD trick when there's no displacement operand in play
    // (e.g. LD IXh,n). We special-case the operand fetch instead of that
    // generality, for the specific patterns below.
    switch(op){
        case 0x21: idx = fetch16(); return 14;                          // LD IX,nn
        case 0x22: { uint16_t a=fetch16(); wr(a, idx&0xFF); wr((uint16_t)(a+1), idx>>8); return 20; } // LD (nn),IX
        case 0x2A: { uint16_t a=fetch16(); uint8_t lo=rd(a),hi=rd((uint16_t)(a+1)); idx=(hi<<8)|lo; return 20; } // LD IX,(nn)
        case 0x23: idx++; return 10;                                    // INC IX
        case 0x2B: idx--; return 10;                                    // DEC IX
        case 0x09: idx = add16(idx, r.BC()); return 15;
        case 0x19: idx = add16(idx, r.DE()); return 15;
        case 0x29: idx = add16(idx, idx); return 15;
        case 0x39: idx = add16(idx, r.SP); return 15;
        case 0xE5: push16(idx); return 15;                              // PUSH IX
        case 0xE1: idx = pop16(); return 14;                            // POP IX
        case 0xE9: r.PC = idx; return 8;                                // JP (IX)
        case 0xF9: r.SP = idx; return 10;                               // LD SP,IX
        case 0x34: { int8_t d=fetch8s(); uint16_t a=idx+d; wr(a, inc8(rd(a))); return 23; } // INC (IX+d)
        case 0x35: { int8_t d=fetch8s(); uint16_t a=idx+d; wr(a, dec8(rd(a))); return 23; } // DEC (IX+d)
        case 0x36: { int8_t d=fetch8s(); uint8_t n=fetch8(); wr((uint16_t)(idx+d), n); return 19; } // LD (IX+d),n
        case 0xE3: { uint8_t lo=rd(r.SP),hi=rd((uint16_t)(r.SP+1)); uint16_t old=idx; idx=(hi<<8)|lo; wr(r.SP,old&0xFF); wr((uint16_t)(r.SP+1),old>>8); return 23; } // EX (SP),IX
        default: break;
    }
    // LD r,(IX+d) / LD (IX+d),r : 0x40-0x7E pattern excluding 0x76(HALT handled elsewhere)
    if((op & 0xC0)==0x40 && op!=0x76){
        int destc=(op>>3)&7, srcc=op&7;
        if(destc==6 || srcc==6){
            int8_t d=fetch8s(); uint16_t a=idx+d;
            if(srcc==6){ uint8_t v=rd(a); writeReg8(destc,v); }
            else { uint8_t v=readReg8(srcc); wr(a,v); }
            return 19;
        }
        // plain register-to-register under DD/FD prefix behaves as unprefixed
        writeReg8(destc, readReg8(srcc));
        return 8;
    }
    if((op & 0xC0)==0x80){ // ALU A,(IX+d)
        int z=op&7; int alu=(op>>3)&7;
        uint8_t v;
        if(z==6){ int8_t d=fetch8s(); v=rd((uint16_t)(idx+d)); } else v=readReg8(z);
        switch(alu){
            case 0: add8(v,false); break; case 1: add8(v,true); break;
            case 2: sub8(v,false); break; case 3: sub8(v,true); break;
            case 4: and8(v); break; case 5: xor8(v); break;
            case 6: or8(v); break; case 7: cp8(v); break;
        }
        return (z==6)?19:8;
    }
    // Fallback: treat prefix as a no-op and re-dispatch the trailing byte as
    // an unprefixed instruction (matches real Z80 behaviour for the many
    // DD/FD-prefixed opcodes that don't actually reference IX/IY).
    r.PC--; // un-fetch; execMain will fetch it again
    if(trace) fprintf(stderr, "[z80] DD/FD op %02X treated as unprefixed at PC=%04X\n", op, r.PC);
    return execMain(fetch8());
}

// ---- main opcode dispatch ----------------------------------------------
int Z80::execMain(uint8_t op){
    if(op==0xCB){ return 8 + execCB(fetch8()); }
    if(op==0xED){ return execED(fetch8()); }
    if(op==0xDD){ return 4 + execIndexed(r.IX); }
    if(op==0xFD){ return 4 + execIndexed(r.IY); }

    int x=op>>6, y=(op>>3)&7, z=op&7;

    if(op==0x00) return 4; // NOP
    if(op==0x76){ r.halted=true; return 4; } // HALT
    if(op==0xF3){ r.IFF1=r.IFF2=false; return 4; } // DI
    if(op==0xFB){ r.IFF1=r.IFF2=true; return 4; }  // EI

    if(x==1){ // LD r,r' (includes HALT already handled)
        writeReg8(y, readReg8(z));
        return (y==6||z==6)?7:4;
    }
    if(x==2){ // ALU A,r
        uint8_t v = readReg8(z);
        switch(y){
            case 0: add8(v,false); break; case 1: add8(v,true); break;
            case 2: sub8(v,false); break; case 3: sub8(v,true); break;
            case 4: and8(v); break; case 5: xor8(v); break;
            case 6: or8(v); break; case 7: cp8(v); break;
        }
        return (z==6)?7:4;
    }
    if(x==0){
        switch(z){
            case 0: // relative jumps / misc
                if(y==0) return 4; // NOP already handled but keep for safety
                if(y==1){ // EX AF,AF'
                    std::swap(r.A,r.A_); std::swap(r.F,r.F_); return 4;
                }
                if(y==2){ int8_t d=fetch8s(); if(--r.B!=0){ r.PC+=d; return 13;} return 8; } // DJNZ
                if(y==3){ int8_t d=fetch8s(); r.PC+=d; return 12; } // JR d
                { int8_t d=fetch8s(); bool cond=false;
                  switch(y){case 4:cond=!(r.F&FZ);break;case 5:cond=(r.F&FZ);break;case 6:cond=!(r.F&FC);break;case 7:cond=(r.F&FC);break;}
                  if(cond){ r.PC+=d; return 12;} return 7; }
            case 1: {
                int p=y>>1; bool q=y&1;
                auto rp=[&]()->uint16_t{switch(p){case 0:return r.BC();case 1:return r.DE();case 2:return r.HL();case 3:return r.SP;}return 0;};
                auto setrp=[&](uint16_t v){switch(p){case 0:r.setBC(v);break;case 1:r.setDE(v);break;case 2:r.setHL(v);break;case 3:r.SP=v;break;}};
                if(!q){ setrp(fetch16()); return 10; }
                else { r.setHL(add16(r.HL(), rp())); return 11; }
            }
            case 2: { // indirect loads
                switch(y){
                    case 0: wr(r.BC(), r.A); return 7;
                    case 1: r.A = rd(r.BC()); return 7;
                    case 2: wr(r.DE(), r.A); return 7;
                    case 3: r.A = rd(r.DE()); return 7;
                    case 4: { uint16_t a=fetch16(); wr(a,r.L); wr((uint16_t)(a+1),r.H); return 16; }
                    case 5: { uint16_t a=fetch16(); r.L=rd(a); r.H=rd((uint16_t)(a+1)); return 16; }
                    case 6: { uint16_t a=fetch16(); wr(a,r.A); return 13; }
                    case 7: { uint16_t a=fetch16(); r.A=rd(a); return 13; }
                }
                break;
            }
            case 3: { // INC/DEC rr
                int p=y>>1; bool q=y&1;
                auto rp=[&]()->uint16_t{switch(p){case 0:return r.BC();case 1:return r.DE();case 2:return r.HL();case 3:return r.SP;}return 0;};
                auto setrp=[&](uint16_t v){switch(p){case 0:r.setBC(v);break;case 1:r.setDE(v);break;case 2:r.setHL(v);break;case 3:r.SP=v;break;}};
                setrp(rp() + (q? -1 : 1));
                return 6;
            }
            case 4: writeReg8(y, inc8(readReg8(y))); return (y==6)?11:4;      // INC r
            case 5: writeReg8(y, dec8(readReg8(y))); return (y==6)?11:4;      // DEC r
            case 6: writeReg8(y, fetch8()); return (y==6)?10:7;               // LD r,n
            case 7: { // rotates on A / misc
                switch(y){
                    case 0: { uint8_t c=(r.A>>7)&1; r.A=(r.A<<1)|c; r.F=(r.F&(FS|FZ|FPV))|c; set53(r.A); return 4; } // RLCA
                    case 1: { uint8_t c=r.A&1; r.A=(r.A>>1)|(c<<7); r.F=(r.F&(FS|FZ|FPV))|c; set53(r.A); return 4; } // RRCA
                    case 2: { uint8_t c=(r.A>>7)&1; r.A=(r.A<<1)|(r.F&FC); r.F=(r.F&(FS|FZ|FPV))|c; set53(r.A); return 4; } // RLA
                    case 3: { uint8_t c=r.A&1; r.A=(r.A>>1)|((r.F&FC)<<7); r.F=(r.F&(FS|FZ|FPV))|c; set53(r.A); return 4; } // RRA
                    case 4: { // DAA
                        uint8_t a=r.A; uint8_t adj=0; bool carry=(r.F&FC); bool halfIn=(r.F&FH);
                        bool origLow=(a&0xF)>9, origHigh=(a>0x99);
                        if(halfIn || origLow) adj|=0x06;
                        if(carry || origHigh) { adj|=0x60; carry=true; }
                        uint8_t res = (r.F&FN) ? (uint8_t)(a-adj) : (uint8_t)(a+adj);
                        uint8_t newH;
                        if(r.F&FN) newH = (halfIn && (a&0xF)<6) ? 1:0;
                        else newH = ((a&0xF)+ (adj&0xF) ) > 0xF ? 1:0;
                        r.F = (r.F & FN);
                        if(carry) r.F |= FC;
                        if(newH) r.F |= FH;
                        setSZ(res); if(parity(res)) r.F|=FPV; set53(res);
                        r.A=res; return 4;
                    }
                    case 5: r.A=~r.A; r.F|=(FN|FH); set53(r.A); return 4; // CPL
                    case 6: r.F=(r.F&(FS|FZ|F5|F3|FPV))|FC; return 4; // SCF
                    case 7: { uint8_t oldC = r.F & FC;
                              r.F = (r.F & (FS|FZ|F5|F3|FPV)) | (oldC ? FH : 0);
                              if(!oldC) r.F |= FC;
                              return 4; } // CCF: C:=~C, H:=old C, N:=0
                }
            }
        }
    }
    if(x==3){
        switch(z){
            case 0: { bool cond; switch(y){case 0:cond=!(r.F&FZ);break;case 1:cond=(r.F&FZ);break;case 2:cond=!(r.F&FC);break;case 3:cond=(r.F&FC);break;case 4:cond=!(r.F&FPV);break;case 5:cond=(r.F&FPV);break;case 6:cond=!(r.F&FS);break;default:cond=(r.F&FS);}
                      if(cond){ r.PC=pop16(); return 11;} return 5; }
            case 1: {
                int p=y>>1; bool q=y&1;
                if(!q){
                    uint16_t v=pop16();
                    switch(p){case 0:r.setBC(v);break;case 1:r.setDE(v);break;case 2:r.setHL(v);break;case 3:r.A=v>>8;r.F=v&0xFF;break;}
                    return 10;
                } else {
                    switch(p){
                        case 0: r.PC=pop16(); return 10; // RET
                        case 1: std::swap(r.B,r.B_); std::swap(r.C,r.C_); std::swap(r.D,r.D_); std::swap(r.E,r.E_); std::swap(r.H,r.H_); std::swap(r.L,r.L_); return 4; // EXX
                        case 2: r.PC=r.HL(); return 4; // JP (HL)
                        case 3: r.SP=r.HL(); return 6; // LD SP,HL
                    }
                }
                break;
            }
            case 2: { uint16_t a=fetch16(); bool cond; switch(y){case 0:cond=!(r.F&FZ);break;case 1:cond=(r.F&FZ);break;case 2:cond=!(r.F&FC);break;case 3:cond=(r.F&FC);break;case 4:cond=!(r.F&FPV);break;case 5:cond=(r.F&FPV);break;case 6:cond=!(r.F&FS);break;default:cond=(r.F&FS);}
                      if(cond) r.PC=a; return 10; }
            case 3: {
                switch(y){
                    case 0: r.PC=fetch16(); return 10; // JP nn
                    case 2: { uint8_t n=fetch8(); if(out_port) out_port(n, r.A); return 11; } // OUT (n),A
                    case 3: { uint8_t n=fetch8(); r.A = in_port ? in_port(n) : 0xFF; return 11; } // IN A,(n)
                    case 4: { uint8_t lo=rd(r.SP),hi=rd((uint16_t)(r.SP+1)); uint16_t old=r.HL(); wr(r.SP,old&0xFF); wr((uint16_t)(r.SP+1),old>>8); r.setHL((hi<<8)|lo); return 19; } // EX (SP),HL
                    case 5: std::swap(r.D,r.H); std::swap(r.E,r.L); return 4; // EX DE,HL
                    case 6: r.IFF1=r.IFF2=false; return 4; // DI
                    case 7: r.IFF1=r.IFF2=true; return 4;  // EI
                }
                break;
            }
            case 4: { uint16_t a=fetch16(); bool cond; switch(y){case 0:cond=!(r.F&FZ);break;case 1:cond=(r.F&FZ);break;case 2:cond=!(r.F&FC);break;case 3:cond=(r.F&FC);break;case 4:cond=!(r.F&FPV);break;case 5:cond=(r.F&FPV);break;case 6:cond=!(r.F&FS);break;default:cond=(r.F&FS);}
                      if(cond){ push16(r.PC); r.PC=a; return 17;} return 10; }
            case 5: {
                int p=y>>1; bool q=y&1;
                if(!q){
                    uint16_t v; switch(p){case 0:v=r.BC();break;case 1:v=r.DE();break;case 2:v=r.HL();break;default:v=(r.A<<8)|r.F;break;}
                    push16(v); return 11;
                } else if(y==1){ uint16_t a=fetch16(); push16(r.PC); r.PC=a; return 17; } // CALL nn
                break;
            }
            case 6: { uint8_t n=fetch8();
                switch(y){
                    case 0: add8(n,false); break; case 1: add8(n,true); break;
                    case 2: sub8(n,false); break; case 3: sub8(n,true); break;
                    case 4: and8(n); break; case 5: xor8(n); break;
                    case 6: or8(n); break; case 7: cp8(n); break;
                }
                return 7;
            }
            case 7: push16(r.PC); r.PC = y*8; return 11; // RST
        }
    }
    if(trace) fprintf(stderr, "[z80] unimplemented opcode %02X at PC=%04X\n", op, r.PC-1);
    return 4;
}

int Z80::step(){
    if(r.halted) return 4;
    uint8_t oldF53 = r.F & (F5|F3); // this reference simulator never touches bits 5/3 of F...
    uint8_t op = fetch8();
    bool rawF = (op == 0xF1 || op == 0x08); // POP AF and EX AF,AF' raw-transfer F verbatim (incl. bits 5/3); every other instruction leaves 5/3 untouched in this reference simulator
    int t = execMain(op);
    if(!rawF) r.F = (r.F & ~(F5|F3)) | oldF53;
    tstates += t;
    instructions_executed++;
    return t;
}
