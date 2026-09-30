// harness.cpp -- Linux-only verification tool. NOT part of the deliverable;
// this exists purely so trace-diffing can be done in this sandbox, since
// the real target (PSystemEngine.cpp/MainFrm.cpp) is Windows/MFC and can't
// be compiled or run here. Mirrors PSystemEngine's Big Disk port protocol
// and RunLoop exactly (same opcode addresses, same fast-forward hack for
// the boot delay loop), minus anything GUI-specific.
// PM_V(a): P-machine variable address (identity here; see PSystemEngine.cpp)
#define PM_V(a) ((uint16_t)(a))
#include "z80.h"
#include "PCodeOpcodes.h"
// Host bindings for the shared native-code include files
// (NativeSegReturn.inc, NativeCxp.inc, NativeCsp.inc).
#define PM_MEM mem
#define PM_CPU cpu
#define PM_PRESERVE preserveZ80RegCompat
#define PM_COUNT(op) { opcodeEmulatedCount[op]++; lastNativeOpcode = op; }
#define PM_ROM(a) mem[(uint16_t)(a)]   // interpreter tables / code bytes (see NativeCsp.inc)
#define PM_IOCFG IoConfig()   // unit-I/O configuration (PCodeOpcodes.h)
// run-time errors (NativeErrors.inc)
#define PM_NATIVE_ERRORS (useNativeOps && !preserveZ80RegCompat)
#define PM_CXP02 ((uint16_t)0x03D7)
#define PM_INTEND ((uint16_t)(mem[0x03EA] | (mem[0x03EB] << 8)))
#include "UcsdReal.h"

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>

static double DecodeUcsdReal(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
    if (b0 == 0) return 0.0;
    int exp = (int)b0 - 128;
    bool sign = (b1 & 0x80) != 0;
    uint32_t fracBits = ((uint32_t)(b1 & 0x7F) << 16) | ((uint32_t)b2 << 8) | b3;
    double mantissa = 0.5 + (double)fracBits / 16777216.0;
    double value = mantissa * std::ldexp(1.0, exp);
    return sign ? -value : value;
}

static bool EncodeUcsdReal(double v, uint8_t& b0, uint8_t& b1, uint8_t& b2, uint8_t& b3) {
    if (v == 0.0) { b0 = b1 = b2 = b3 = 0; return true; }
    bool sign = v < 0.0;
    v = std::fabs(v);
    int exp = 0;
    double m = std::frexp(v, &exp);
    uint32_t fracBits = (uint32_t)std::llround((m - 0.5) * 16777216.0);
    if (fracBits >= (1u << 23)) { fracBits = 0; exp += 1; }
    int expByte = exp + 128;
    if (expByte < 1 || expByte > 255) return false;
    b0 = (uint8_t)expByte;
    b1 = (uint8_t)((sign ? 0x80 : 0) | ((fracBits >> 16) & 0x7F));
    b2 = (uint8_t)((fracBits >> 8) & 0xFF);
    b3 = (uint8_t)(fracBits & 0xFF);
    return true;
}

// Reverse lookup: given the Z80 PC value the dispatch shortcut (or real
// Z80 BACK code continuing to run after a native commit) has arrived at,
// return which p-code opcode's own Z80 handler entry point this is, or
// -1 if PC isn't one of these 68 known addresses. This lets the native
// switch below dispatch on the opcode value itself (case OP_ADI, etc.)
// while still firing in every situation the original switch(PC) design
// did -- including the case where PC reaches a target via real BACK
// code naturally continuing to execute one instruction at a time after
// a previous native commit, not just immediately following the
// shortcut's own direct computation. Checked every iteration,
// unconditionally, exactly like the original address-keyed switch was.
static int NativeOpcodeForTarget(uint16_t pc) {
    switch (pc) {
        case 0x0740: return OP_ADI;
        case 0x077C: return OP_NGI;
        case 0x0788: return OP_SBI;
        case 0x0791: return OP_CHK;
        case 0x0722: return OP_NOT;
        case 0x072D: return OP_ABI;
        case 0x087C: return OP_EQUI;
        case 0x08B8: return OP_NEQI;
        case 0x088F: return OP_GEQI;
        case 0x08A7: return OP_GTRI;
        case 0x08CB: return OP_LEQI;
        case 0x08D0: return OP_LESI;
        case 0x1185: return OP_UJP;
        case 0x04A7: return OP_STL;
        case 0x0557: return OP_IXA;
        case 0x05CC: return OP_LDB;
        case 0x04C6: return OP_LAO;
        case 0x070A: return OP_LAND;
        case 0x0522: return OP_INCR;
        case 0x0498: return OP_LDL;
        case 0x0716: return OP_LOR;
        case 0x05D6: return OP_STB;
        case 0x06A4: return OP_STP;
        case 0x0507: return OP_LDA;
        case 0x045D: return OP_LDCI;
        case 0x11EF: return OP_RBP;
        case 0x052C: return OP_STO;
        case 0x054A: return OP_STIND;
        case 0x15B8: return OP_CSP;
        case 0x048C: return OP_LLA;
        case 0x0534: return OP_SIND0;
        case 0x050E: return OP_LOD;
        case 0x0467: return OP_LDCN;
        case 0x0598: return OP_LDM;
        case 0x05B0: return OP_STM;
        case 0x0C55: return OP_INN;
        case 0x1398: return OP_CXP;
        case 0x11A6: return OP_XJP;
        case 0x0518: return OP_STR;
        case 0x05DF: return OP_LPA;
        case 0x05EB: return OP_LSA;
        case 0x0619: return OP_SAS;
        case 0x066D: return OP_LDP;
        case 0x1337: return OP_CLP;
        case 0x1348: return OP_CBP;
        case 0x1369: return OP_CIP;
        case 0x133D: return OP_CGP;
        case 0x09F1: return OP_DIF;
        case 0x0A12: return OP_UNI;
        case 0x0B66: return OP_SGS;
        case 0x0B69: return OP_SRS;
        case 0x0644: return OP_IXP;
        case 0x0C00: return OP_ADJ;
        case 0x056E: return OP_MOV;
        case 0x0582: return OP_LDC;
        case 0x0747: return OP_DVI;
        case 0x0761: return OP_MPI;
        case 0x0754: return OP_MODI;
        case 0x1201: return OP_RNP;
        case 0x08D5: return OP_CEQU;
        case 0x08E2: return OP_CNEQ;
        case 0x08EF: return OP_CGTR;
        case 0x08FB: return OP_CLEQ;
        case 0x0907: return OP_CLSS;
        case 0x0910: return OP_CGEQ;
        case 0x04D2: return OP_LDO;
        case 0x04E1: return OP_SRO;
        case 0x117D: return OP_FJP;
#include "NativeOpsTargets.inc"
        default: return -1;
    }
}

static bool LoadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    std::streamsize sz = f.tellg();
    f.seekg(0);
    out.resize(sz > 0 ? (size_t)sz : 0);
    if (sz > 0) f.read((char*)out.data(), sz);
    return true;
}

struct BigDiskState {
    int drive = 0;
    uint16_t blk = 0, len = 0, dma = 0;
    uint8_t len_hi = 0, blk_hi = 0, dma_hi = 0, cmd = 1;
};

// Records, at every BACK re-entry (PC==0x03B0), a snapshot of the p-machine
// state relevant to correctness: BC (IPC), DE, HL, SP, and the top two
// words of the evaluation stack. Two runs (intercepts ON vs OFF) must
// produce byte-identical sequences of these records if the native-C
// opcodes are behaving exactly like the Z80 code they replace.
// bc/de/hl/sp/a are the Z80 registers that happen to implement this
// interpreter today -- bc doubles as IPC (the p-code instruction
// pointer) and sp as the p-machine's own operand stack pointer, both
// genuinely meaningful P-machine state, not just Z80 incidentals.
// The fields below are explicitly P-machine registers, read directly
// from their fixed memory locations (per SYSTEM.MICRO's own "Internal
// P-machine registers, widely used temporaries" block at 0x0240, plus
// MP/BASE/JTAB/SEGP just above it at 0x02F0-0x02F7) rather than derived
// from any Z80-specific state. Once P-code opcode simulation is fully
// replaced by native C, bc/de/hl/a stop being meaningful (there's no
// Z80 executing to hold them) -- these fields are what a verification
// log needs to keep comparing against once that happens.
struct BackRecord {
    uint16_t bc, de, hl, sp, top0, top1;
    uint8_t a;
    int lastNativeOp;
    uint8_t opcodeAtFetch; // the p-code opcode byte about to be fetched at this point
    uint16_t np;      // 0x0240 -- top of heap pointer
    uint16_t mpd0;     // 0x0242 -- ^local var with offset zero
    uint16_t based0;   // 0x0244 -- ^global var with offset zero
    uint16_t ipcsav;   // 0x0246 -- saved IPC (complex ops, XEQERR)
    uint16_t mp;       // 0x02F2 -- mark pointer (current frame base)
    uint16_t base;     // 0x02F0 -- base environment pointer
    uint16_t jtab;     // 0x02F4 -- current procedure's jump table
    uint16_t segp;     // 0x02F6 -- current segment pointer
    bool operator==(const BackRecord& o) const {
        return bc==o.bc && de==o.de && hl==o.hl && sp==o.sp && top0==o.top0 && top1==o.top1 && a==o.a
            && np==o.np && mpd0==o.mpd0 && based0==o.based0 && ipcsav==o.ipcsav
            && mp==o.mp && base==o.base && jtab==o.jtab && segp==o.segp;
    }
};

class Engine {
public:
    bool useNativeOps = true; // toggle for A/B trace-diff runs
    bool preserveZ80RegCompat = true; // toggle for the new Z80-register-compat gating test
    uint16_t ipc = 0; // P-machine IPC register (mirrors harness.cpp's own PSystemEngine.cpp copy)
    uint64_t echoDirectCallCount = 0; // fires whenever ECHO is called directly (not via UNITWRITE) and natively handled
    int lastNativeOpcode = -1; // debug: which native case last fired (for tracing a mismatch back to its cause)
    uint64_t opcodeFetchCount[256] = {0};
    uint64_t opcodeEmulatedCount[256] = {0};
    std::vector<BackRecord> backLog;
    long maxInstructions = 0;

    Z80 cpu;
    uint8_t mem[65536];
    std::vector<uint8_t> volDrive0, volDrive1;
    BigDiskState bd;

    Engine() {
        memset(mem, 0xE5, sizeof(mem));
        cpu.reset();
        cpu.rd = [this](uint16_t a) -> uint8_t { return mem[a]; };
        cpu.wr = [this](uint16_t a, uint8_t v) { mem[a] = v; };
        cpu.in_port = [this](uint8_t p) -> uint8_t { return PortIn(p); };
        cpu.out_port = [this](uint8_t p, uint8_t v) { PortOut(p, v); };
    }

    bool LoadFiles(const std::string& pascalBinPath, const std::string& bigDiskPath,
                   const std::string& emptyDiskPath) {
        if (!LoadWholeFile(bigDiskPath, volDrive0)) { fprintf(stderr, "can't open %s\n", bigDiskPath.c_str()); return false; }
        LoadWholeFile(emptyDiskPath, volDrive1); // optional
        std::ifstream pf(pascalBinPath, std::ios::binary);
        if (!pf) { fprintf(stderr, "can't open %s\n", pascalBinPath.c_str()); return false; }
        memset(mem, 0xE5, sizeof(mem));
        pf.read((char*)mem + 0xF000, 0x10000 - 0xF000);
        cpu.reset();
        cpu.r.PC = 0xF000;
        cpu.r.SP = 0xFDFD;
        return true;
    }

    std::vector<uint8_t>* DriveImage(int drive) {
        if (drive == 0) return &volDrive0;
        if (drive == 1) return &volDrive1;
        return nullptr;
    }
    static int UnitValueToDrive(uint8_t v) {
        switch (v) {
            case 4: return 0; case 5: return 1; case 9: return 2; case 10: return 3;
            default: return -1;
        }
    }

    std::string consoleText; // accumulated printable console output, for a crude "did it boot" check

    uint8_t PortIn(uint8_t port) {
        switch (port) {
            case 0: return 0x00; // CONST: no key ever waiting (no scripted input yet)
            case 1: return 0x00; // CONIN: never actually reached if CONST always says "no key"
            case 207: {
                auto* img = DriveImage(bd.drive);
            // As MunkDisk.c: no drive -> status 1; only READ (1) and WRITE (2)
            // transfer data; CLEAR (4) or anything else -> status 0, nothing moved.
            if (!img || img->empty()) return 0x01;   // no image mounted on that unit
            if (bd.cmd != 1 && bd.cmd != 2) return 0x00;
                long fileOff = (long)bd.blk * 512;
                int n = bd.len;
                if (img && fileOff >= 0 && (size_t)(fileOff + n) <= img->size()) {
                    if (bd.cmd == 2) { for (int i = 0; i < n; i++) (*img)[fileOff + i] = mem[(uint16_t)(bd.dma + i)]; }
                    else { for (int i = 0; i < n; i++) mem[(uint16_t)(bd.dma + i)] = (*img)[fileOff + i]; }
                    return 0x00;
                }
                return 0x01;
            }
            default: return 0xFF;
        }
    }

    void PortOut(uint8_t port, uint8_t v) {
        switch (port) {
            case 1: if (v >= 32 && v < 127) consoleText += (char)v; else if (v == 10 || v==13) consoleText += '\n'; break;
            case 200: bd.drive = UnitValueToDrive(v); break; // -1 = no such drive: status 1 (MunkDisk.c), never drive 0
            case 203: bd.len_hi = v; bd.len = (bd.len_hi << 8) | (bd.len & 0xFF); break;
            case 204: bd.len = (bd.len & 0xFF00) | v; break;
            case 205: bd.blk_hi = v; bd.blk = (bd.blk_hi << 8) | (bd.blk & 0xFF); break;
            case 206: bd.blk = (bd.blk & 0xFF00) | v; break;
            case 15: bd.dma = (bd.dma & 0xFF00) | v; break;
            case 16: bd.dma_hi = v; bd.dma = (bd.dma_hi << 8) | (bd.dma & 0xFF); break;
            case 207: bd.cmd = v; break;
            default: break;
        }
    }

    // Little-endian SP-relative stack helpers, matching Z80 PUSH/POP exactly.
    uint16_t PopStackWord() {
        uint8_t lo = mem[cpu.r.SP];
        uint8_t hi = mem[(uint16_t)(cpu.r.SP + 1)];
        cpu.r.SP += 2;
        return (uint16_t)((hi << 8) | lo);
    }
    void PushStackWord(uint16_t v) {
        cpu.r.SP -= 2;
        mem[cpu.r.SP] = v & 0xFF;
        mem[(uint16_t)(cpu.r.SP + 1)] = v >> 8;
    }
    uint16_t PeekStackWord(int off) const {
        uint16_t addr = (uint16_t)(cpu.r.SP + off);
        return (uint16_t)(mem[addr] | (mem[(uint16_t)(addr+1)] << 8));
    }
    uint16_t DecodeGBDE() {
        uint16_t bc = cpu.r.BC();
        uint8_t a = mem[bc]; bc = (uint16_t)(bc + 1);
        if ((a & 0x80) == 0) { cpu.r.setBC(bc); cpu.r.A = a; return a; }
        uint8_t hi = a & 0x7F;
        uint8_t lo = mem[bc]; bc = (uint16_t)(bc + 1);
        cpu.r.setBC(bc);
        cpu.r.A = lo;
        return (uint16_t)((hi << 8) | lo);
    }
    uint16_t GetIA() {
        uint8_t lexLevels = mem[cpu.r.BC()];
        cpu.r.setBC((uint16_t)(cpu.r.BC() + 1));
        uint16_t hl = (uint16_t)(mem[0x02F2] | (mem[0x02F3] << 8));
        for (uint8_t i = 0; i < lexLevels; i++) {
            uint16_t de = (uint16_t)(mem[hl] | (mem[(uint16_t)(hl + 1)] << 8));
            hl = de;
        }
        uint16_t offset = DecodeGBDE();
        hl = (uint16_t)(hl + offset * 2 + 10);
        cpu.r.setDE(0x000A);
        return hl;
    }

    void NativeSrs(uint16_t i, uint16_t j) {
        mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8;

        if (j < i) {
            if (preserveZ80RegCompat) cpu.r.A = (uint8_t)(((uint16_t)(j - i)) >> 8);
            if (preserveZ80RegCompat) cpu.r.setDE(j);
            PushStackWord(0);
            if (preserveZ80RegCompat) cpu.r.setHL(0);
            cpu.r.PC = 0x03A4;
            return;
        }

        uint8_t iDiv16 = (uint8_t)(i / 16), iMod16 = (uint8_t)(i % 16);
        uint8_t jDiv16 = (uint8_t)(j / 16), jMod16 = (uint8_t)(j % 16);
        uint16_t bitterJ = (uint16_t)((1u << (jMod16 + 1)) - 1);
        uint16_t unbitrI = (uint16_t)(0xFFFFu << iMod16);

        for (int w = jDiv16; w >= 0; w--) {
            uint16_t wordVal;
            if (w == jDiv16 && w == iDiv16) wordVal = (uint16_t)(bitterJ & unbitrI);
            else if (w == jDiv16) wordVal = bitterJ;
            else if (w == iDiv16) wordVal = unbitrI;
            else if (w > iDiv16 && w < jDiv16) wordVal = 0xFFFF;
            else wordVal = 0x0000;
            PushStackWord(wordVal);
        }
        uint16_t setSize = (uint16_t)(jDiv16 + 1);
        PushStackWord(setSize);

        if (preserveZ80RegCompat) cpu.r.A = (uint8_t)(setSize & 0xFF);
        if (preserveZ80RegCompat) cpu.r.setDE(0x0000);
        if (preserveZ80RegCompat) cpu.r.setHL(setSize);
        cpu.r.PC = 0x03A4;
    }

    bool CmpSetupOrdering(bool& outCarry, bool& outZero) {
        uint8_t typeCode = mem[cpu.r.BC()];
        if (typeCode == 6) { // BOOLC
            cpu.r.setBC((uint16_t)(cpu.r.BC() + 1));
            mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8;
            uint16_t b = PopStackWord();
            uint16_t a = PopStackWord();
            uint8_t aBit = (uint8_t)(a & 1), bBit = (uint8_t)(b & 1);
            outCarry = (aBit < bBit);
            outZero  = (aBit == bBit);
            cpu.r.A = aBit;
            cpu.r.setDE((uint16_t)((b & 0xFF00) | bBit));
            return true;
        } else if (typeCode == 4) { // STRGC
            cpu.r.setBC((uint16_t)(cpu.r.BC() + 1));
            mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8;
            uint16_t bPtr = PopStackWord();
            uint16_t aPtr = PopStackWord();
            bool aDisguised = (aPtr >> 8) == 0;
            bool bDisguised = (bPtr >> 8) == 0;
            uint8_t aLen = aDisguised ? 1 : mem[aPtr];
            uint8_t bLen = bDisguised ? 1 : mem[bPtr];
            uint16_t aData = (uint16_t)(aPtr + 1);
            uint16_t bData = (uint16_t)(bPtr + 1);
            uint16_t aBasePtr = aDisguised ? 0x024E : aPtr;
            uint8_t minLen = (aLen < bLen) ? aLen : bLen;
            bool foundMismatch = false;
            uint16_t deFinal = aBasePtr;
            uint8_t aRegFinal = 0;
            int cmpResult = 0;
            for (uint8_t idx = 0; idx < minLen; idx++) {
                uint8_t ac = aDisguised ? (aPtr & 0xFF) : mem[(uint16_t)(aData + idx)];
                uint8_t bc = bDisguised ? (bPtr & 0xFF) : mem[(uint16_t)(bData + idx)];
                deFinal = (uint16_t)(aBasePtr + idx + 1);
                if (ac != bc) { foundMismatch = true; aRegFinal = ac; cmpResult = (int)ac - (int)bc; break; }
            }
            if (!foundMismatch) {
                cmpResult = (int)aLen - (int)bLen;
                aRegFinal = aLen;
            }
            outCarry = (cmpResult < 0);
            outZero  = (cmpResult == 0);
            cpu.r.A = aRegFinal;
            cpu.r.setDE(deFinal);
            return true;
        }
        if ((typeCode == 10 || typeCode == 12) && !preserveZ80RegCompat) {
        // BYTEC / WORDC (e.g. < on PACKED ARRAY OF CHAR), register
        // compatibility off: GBDE size, SAVIPC past it, then SWEQ -- scan
        // while equal (CPI; a count of 0 wraps round to 65536) and take the
        // flags from the last pair compared (DEC HL; CP (HL)).
        cpu.r.setBC((uint16_t)(cpu.r.BC() + 1));
        uint16_t count = DecodeGBDE();
        mem[PM_V(0x0246)] = cpu.r.BC() & 0xFF; mem[PM_V(0x0247)] = cpu.r.BC() >> 8; // SAVIPC
        uint32_t n = (typeCode == 12) ? (uint32_t)(uint16_t)(count * 2) : count;
        if (n == 0) n = 0x10000;
        const uint16_t bPtr = PopStackWord();
        const uint16_t aPtr = PopStackWord();
        uint8_t ac = 0, bc = 0;
        for (uint32_t k = 0; k < n; k++) {
            ac = mem[(uint16_t)(aPtr + k)]; bc = mem[(uint16_t)(bPtr + k)];
            if (ac != bc) break;
        }
        outCarry = ac < bc;
        outZero  = ac == bc;
        return true;
    }
    return false;
    }

    // This trace-diff engine has no native BIOS-linker intercept, so it reports
    // every BIOS function as not handled: the native unit I/O in NativeCsp.inc
    // then leaves all unit I/O to the real Z80 code here.
    // Unit-I/O configuration for the shared native unit I/O, discovered once
    // from the Z80 interpreter in memory (PCodeOpcodes.h).
    PmIoConfig io; bool ioReady = false;
    const PmIoConfig& IoConfig() {
        if (!ioReady) { io = PmIoConfigFromImage([&](uint16_t a) { return mem[a]; }); ioReady = true; }
        return io;
    }
    bool NativeBiosCall(uint8_t, uint8_t, uint8_t*, bool) { return false; }

    bool NativeDiskRead(uint16_t unit, uint16_t block, uint16_t len, std::vector<uint8_t>& out) {
        // Any Big Disk unit, exactly as the port-207 READ does (unit -> drive,
        // block * 512); no image or past its end -> false (Z80 READSEG runs).
        auto* img = DriveImage(UnitValueToDrive((uint8_t)unit));
        if (!img || img->empty()) return false;
        size_t off = (size_t)block * 512;
        if (off + len > img->size()) return false;
        out.assign(img->begin() + off, img->begin() + off + len);
        return true;
    }

    bool NativeBldmscw() {
        uint16_t segp = (uint16_t)(mem[0x02F6] | (mem[0x02F7] << 8));
        mem[0x02CA] = segp & 0xFF; mem[0x02CB] = segp >> 8;
        uint16_t bc = cpu.r.BC();
        uint8_t procNum = mem[bc]; bc = (uint16_t)(bc + 1);
        cpu.r.setBC(bc);
        mem[0x0246] = bc & 0xFF; mem[0x0247] = bc >> 8;
        uint16_t tAddr = (uint16_t)(segp - (uint16_t)(procNum * 2));
        uint16_t tSval = (uint16_t)(mem[tAddr] | (mem[(uint16_t)(tAddr+1)] << 8));
        uint16_t jtab  = (uint16_t)(tAddr - tSval);
        mem[0x02CC] = jtab & 0xFF; mem[0x02CD] = jtab >> 8;
        if (mem[jtab] == 0) return false;
        uint16_t dOff  = (uint16_t)(jtab - 8);
        uint16_t datasz = (uint16_t)(mem[dOff]             | (mem[(uint16_t)(dOff+1)] << 8));
        uint16_t parmsz = (uint16_t)(mem[(uint16_t)(dOff+2)] | (mem[(uint16_t)(dOff+3)] << 8));
        uint16_t oldSP = cpu.r.SP;
        cpu.r.SP = (uint16_t)(oldSP - datasz);
        for (uint16_t i = 0; i < parmsz; i++)
            mem[(uint16_t)(cpu.r.SP + i)] = mem[(uint16_t)(oldSP + i)];
        uint16_t mssp    = (uint16_t)(oldSP + parmsz);
        uint16_t ipcsav  = (uint16_t)(mem[0x0246] | (mem[0x0247] << 8));
        uint16_t curJtab = (uint16_t)(mem[0x02F4] | (mem[0x02F5] << 8));
        uint16_t curMP   = (uint16_t)(mem[0x02F2] | (mem[0x02F3] << 8));
        PushStackWord(mssp); PushStackWord(ipcsav); PushStackWord(segp);
        PushStackWord(curJtab); PushStackWord(curMP); PushStackWord(curMP);
        uint16_t np = (uint16_t)(mem[0x0240] | (mem[0x0241] << 8));
        if ((uint16_t)(cpu.r.SP - 60) < np) { cpu.r.PC = 0x03E9; return false; }
        uint16_t newMP = cpu.r.SP;
        mem[0x02F2] = newMP & 0xFF; mem[0x02F3] = newMP >> 8;
        mem[0x0242] = (uint16_t)(newMP+10) & 0xFF; mem[0x0243] = (uint16_t)(newMP+10) >> 8;
        mem[0x02F6] = segp & 0xFF; mem[0x02F7] = segp >> 8;
        mem[0x02F4] = jtab & 0xFF; mem[0x02F5] = jtab >> 8;
        uint16_t eRef  = (uint16_t)(jtab - 2);
        uint16_t eSval = (uint16_t)(mem[eRef] | (mem[(uint16_t)(eRef+1)] << 8));
        uint16_t entry = (uint16_t)(eRef - eSval);
        cpu.r.setBC(entry);
        uint8_t eH = eRef >> 8, eD = eSval >> 8;
        uint8_t eL = eRef & 0xFF, eE = eSval & 0xFF;
        uint8_t carry = (eL < eE) ? 1 : 0;
        cpu.r.A = (uint8_t)(eH - eD - carry);
        cpu.r.setDE(eSval);
        return true;
    }

    void RunLoop() {
        bool tracingStarted = false;
        auto captureIfBack = [this]() {
            if (cpu.r.PC == 0x03B0) {
                opcodeFetchCount[mem[cpu.r.BC()]]++;
                auto rd16 = [this](uint16_t addr) -> uint16_t {
                    return (uint16_t)(mem[addr] | (mem[(uint16_t)(addr + 1)] << 8));
                };
                BackRecord rec{ cpu.r.BC(), cpu.r.DE(), cpu.r.HL(), cpu.r.SP,
                                (uint16_t)((mem[(uint16_t)(cpu.r.SP+1)]<<8)|mem[cpu.r.SP]),
                                (uint16_t)((mem[(uint16_t)(cpu.r.SP+3)]<<8)|mem[(uint16_t)(cpu.r.SP+2)]),
                                cpu.r.A, lastNativeOpcode, mem[cpu.r.BC()],
                                rd16(0x0240), rd16(0x0242), rd16(0x0244), rd16(0x0246),
                                rd16(0x02F2), rd16(0x02F0), rd16(0x02F4), rd16(0x02F6) };
                backLog.push_back(rec);
            }
        };
        for (long i = 0; !cpu.r.halted; i++) {
            if (maxInstructions && i >= maxInstructions) break;

            // Boot-delay fast-forward (unchanged from PSystemEngine::RunLoop).
            if (cpu.r.PC == 0xF244) { cpu.r.setHL(1); cpu.r.B = 1; }

            // Capture #1: the state as THIS iteration begins, if we're about
            // to fetch a new opcode.
            if (useNativeOps && cpu.r.PC == 0x03A4) {          // native BACK1: GETIPC; JP BACK
                cpu.r.setBC((uint16_t)(mem[0x0246] | (mem[0x0247] << 8)));
                cpu.r.PC = 0x03B0;
            }
            captureIfBack();

            // Native BACK-dispatch shortcut.
            if (useNativeOps && cpu.r.PC == 0x03B0) {
                ipc = cpu.r.BC();
                uint8_t opcode = mem[ipc];
                ipc = (uint16_t)(ipc + 1);
                cpu.r.setBC(ipc);
                if (preserveZ80RegCompat) cpu.r.A = (uint8_t)(opcode << 1);
                if ((opcode & 0x80) == 0) {
                    cpu.r.PC = 0x03AB;
                } else {
                    uint16_t xfrtblAddr = (uint16_t)(0x0100 + (opcode - 0x80) * 2);
                    uint16_t target = (uint16_t)(mem[xfrtblAddr] | (mem[(uint16_t)(xfrtblAddr + 1)] << 8));
                    if (preserveZ80RegCompat) {
                        cpu.r.setDE((uint16_t)(xfrtblAddr + 1));
                        cpu.r.setHL(target);
                    }
                    cpu.r.PC = target;
                }
            }

            // Native ECHO (direct calls, not via UNITWRITE). Only proceeds
            // when COVECT is CONOUT (0x0C); anything else falls through.
            if (useNativeOps && cpu.r.PC == 0x1DD2) {
                uint8_t covect = mem[0x02E3];
                if (covect == 0x0C) {
                    uint8_t uasy = mem[0x02DA];
                    bool dleEnabled = (uasy & 0x04) == 0;
                    bool crlfEnabled = (uasy & 0x08) == 0;
                    uint8_t c = cpu.r.C;
                    const uint16_t CLAST = 0x02E1;
                    uint8_t aFinal;

                    if (dleEnabled && mem[CLAST] == 0x10) {
                        uint8_t clast = (uint8_t)(c - 0x20);
                        while (true) {
                            uint8_t next = (uint8_t)(clast - 1);
                            if (next & 0x80) { aFinal = next; break; }
                            clast = next;
                            PortOut(1, 0x20);
                        }
                        mem[CLAST] = clast;
                    } else {
                        mem[CLAST] = c;
                        bool outputIt = true;
                        if (c == 0x10) {
                            if (dleEnabled) {
                                outputIt = false;
                                aFinal = 0;
                            } else {
                                mem[CLAST] = 0x20;
                            }
                        }
                        if (outputIt) {
                            PortOut(1, c);
                            if (!crlfEnabled) {
                                aFinal = (uint8_t)(uasy & 0x08);
                            } else {
                                aFinal = mem[CLAST];
                                if (mem[CLAST] == 0x0D) {
                                    mem[CLAST] = 0x0A;
                                    PortOut(1, 0x0A);
                                    aFinal = covect;
                                }
                            }
                        }
                    }
                    cpu.r.A = aFinal;
                    cpu.r.PC = PopStackWord();
                    echoDirectCallCount++;
                }
            }

#include "NativeErrors.inc"
            int nativeOpcodeLookup = useNativeOps ? NativeOpcodeForTarget(cpu.r.PC) : -1;
            const uint16_t nativeEntryPc = cpu.r.PC;   // to recognise a native hand-over to another native entry
            bool commit = false;
            if (nativeOpcodeLookup >= 0) {
                switch ((uint8_t)nativeOpcodeLookup) {
                    case OP_ADI: { // ADI
                        uint16_t de = PopStackWord(), hl = PopStackWord();
                        uint16_t result = (uint16_t)(hl + de);
                        PushStackWord(result);
                        if (preserveZ80RegCompat) { cpu.r.setDE(de); cpu.r.setHL(result); }
                        commit = true;
                        opcodeEmulatedCount[OP_ADI]++; lastNativeOpcode=OP_ADI;
                        break;
                    }
                    case OP_NGI: { // NGI
                        uint16_t hl = PopStackWord();
                        uint16_t result = (uint16_t)(0 - hl);
                        PushStackWord(result);
                        if (preserveZ80RegCompat) { cpu.r.setHL(result); cpu.r.A = (uint8_t)(result >> 8); }
                        commit = true;
                        opcodeEmulatedCount[OP_NGI]++; lastNativeOpcode=OP_NGI;
                        break;
                    }
                    case OP_SBI: { // SBI
                        uint16_t de = PopStackWord(), hl = PopStackWord();
                        uint16_t result = (uint16_t)(hl - de);
                        PushStackWord(result);
                        if (preserveZ80RegCompat) { cpu.r.setDE(de); cpu.r.setHL(result); }
                        commit = true;
                        opcodeEmulatedCount[OP_SBI]++; lastNativeOpcode=OP_SBI;
                        break;
                    }
                    case OP_CHK: { // CHK
                        uint16_t maxRaw = PopStackWord();
                        uint16_t minRaw = PopStackWord();
                        uint16_t numRaw = PeekStackWord(0);
                        int16_t num  = (int16_t)numRaw;
                        int16_t minV = (int16_t)minRaw;
                        int16_t maxV = (int16_t)maxRaw;
                        uint8_t minHi = minRaw >> 8, numHi = numRaw >> 8, maxHi = maxRaw >> 8;

                        bool minOk = (num >= minV);
                        uint8_t aAfterMin = ((minHi ^ numHi) & 0x80) == 0
                            ? (uint8_t)((uint16_t)(numRaw - minRaw) >> 8)
                            : (uint8_t)((minHi ^ numHi) & minHi);

                        if (minOk) {
                            bool maxOk = (num <= maxV);
                            uint8_t aAfterMax = ((maxHi ^ numHi) & 0x80) == 0
                                ? (uint8_t)((uint16_t)(maxRaw - numRaw) >> 8)
                                : (uint8_t)((maxHi ^ numHi) & numHi);
                            if (maxOk) {
                                if (preserveZ80RegCompat) { cpu.r.A = aAfterMax; cpu.r.setHL(numRaw); cpu.r.setDE(maxRaw); }
                                commit = true;
                            } else {
                                cpu.r.A = aAfterMax;
                                cpu.r.setHL(numRaw);
                                cpu.r.setDE(maxRaw);
                                mem[0x0246] = cpu.r.BC() & 0xFF;
                                mem[0x0247] = cpu.r.BC() >> 8;
                                cpu.r.PC = 0x03DA;
                            }
                        } else {
                            cpu.r.A = aAfterMin;
                            cpu.r.setHL(maxRaw);
                            cpu.r.setDE(minRaw);
                            mem[0x0246] = cpu.r.BC() & 0xFF;
                            mem[0x0247] = cpu.r.BC() >> 8;
                            cpu.r.PC = 0x03DA;
                        }
                        opcodeEmulatedCount[0x88]++; lastNativeOpcode=0x88;
                        break;
                    }
                    case OP_NOT: { // NOT
                        uint16_t value = PopStackWord();
                        uint16_t result = (uint16_t)(~value);
                        PushStackWord(result);
                        if (preserveZ80RegCompat) { cpu.r.setHL(result); cpu.r.A = (uint8_t)(result >> 8); }
                        commit = true;
                        opcodeEmulatedCount[OP_NOT]++; lastNativeOpcode=OP_NOT;
                        break;
                    }
                    case OP_ABI: { // ABI
                        uint16_t value = PopStackWord();
                        uint16_t result;
                        if ((int16_t)value >= 0) result = value;
                        else { result = (uint16_t)(0 - value); result &= 0x7FFF; }
                        PushStackWord(result);
                        if (preserveZ80RegCompat) { cpu.r.setHL(result); cpu.r.A = (uint8_t)(result >> 8); }
                        commit = true;
                        opcodeEmulatedCount[0x80]++; lastNativeOpcode=0x80;
                        break;
                    }
                    case OP_EQUI: { // EQUI
                        uint16_t bRaw = PopStackWord(), aRaw = PopStackWord();
                        uint16_t result = ((int16_t)aRaw == (int16_t)bRaw) ? 1 : 0;
                        PushStackWord(result);
                        uint8_t aLo=aRaw&0xFF, aHi=aRaw>>8, bLo=bRaw&0xFF, bHi=bRaw>>8;
                        uint8_t lowSub = (uint8_t)(aLo - bLo);
                        uint8_t aFinal = (lowSub != 0) ? lowSub : (uint8_t)(aHi - bHi);
                        if (preserveZ80RegCompat) { cpu.r.A = aFinal; cpu.r.setDE(bRaw); cpu.r.setHL(result); }
                        commit = true;
                        opcodeEmulatedCount[0xC3]++; lastNativeOpcode=0xC3;
                        break;
                    }
                    case OP_NEQI: { // NEQI
                        uint16_t bRaw = PopStackWord(), aRaw = PopStackWord();
                        uint16_t result = ((int16_t)aRaw != (int16_t)bRaw) ? 1 : 0;
                        PushStackWord(result);
                        uint8_t aLo=aRaw&0xFF, aHi=aRaw>>8, bLo=bRaw&0xFF, bHi=bRaw>>8;
                        uint8_t lowSub = (uint8_t)(aLo - bLo);
                        uint8_t aFinal = (lowSub != 0) ? lowSub : (uint8_t)(aHi - bHi);
                        if (preserveZ80RegCompat) { cpu.r.A = aFinal; cpu.r.setDE(bRaw); cpu.r.setHL(result); }
                        commit = true;
                        opcodeEmulatedCount[0xCB]++; lastNativeOpcode=0xCB;
                        break;
                    }
                    case OP_GEQI: { // GEQI
                        uint16_t bRaw = PopStackWord(), aRaw = PopStackWord();
                        uint16_t result = ((int16_t)aRaw >= (int16_t)bRaw) ? 1 : 0;
                        PushStackWord(result);
                        uint8_t aLo=aRaw&0xFF, aHi=aRaw>>8, bLo=bRaw&0xFF, bHi=bRaw>>8;
                        uint8_t xorResult = (uint8_t)(bHi ^ aHi);
                        uint8_t aFinal;
                        if (xorResult & 0x80) aFinal = (uint8_t)(xorResult & aHi);
                        else { bool borrow = aLo < bLo; aFinal = (uint8_t)(aHi - bHi - (borrow?1:0)); }
                        if (preserveZ80RegCompat) { cpu.r.A = aFinal; cpu.r.setDE(bRaw); cpu.r.setHL(result); }
                        commit = true;
                        opcodeEmulatedCount[0xC4]++; lastNativeOpcode=0xC4;
                        break;
                    }
                    case OP_GTRI: { // GTRI
                        uint16_t bRaw = PopStackWord(), aRaw = PopStackWord();
                        uint16_t result = ((int16_t)aRaw > (int16_t)bRaw) ? 1 : 0;
                        PushStackWord(result);
                        uint8_t aLo=aRaw&0xFF, aHi=aRaw>>8, bLo=bRaw&0xFF, bHi=bRaw>>8;
                        uint8_t xorResult = (uint8_t)(bHi ^ aHi);
                        uint8_t aFinal;
                        if (xorResult & 0x80) aFinal = (uint8_t)(xorResult & aHi);
                        else { bool borrow = bLo < aLo; aFinal = (uint8_t)(bHi - aHi - (borrow?1:0)); }
                        if (preserveZ80RegCompat) { cpu.r.A = aFinal; cpu.r.setDE(bRaw); cpu.r.setHL(result); }
                        commit = true;
                        opcodeEmulatedCount[0xC5]++; lastNativeOpcode=0xC5;
                        break;
                    }
                    case OP_LEQI: { // LEQI
                        uint16_t bRaw = PopStackWord(), aRaw = PopStackWord();
                        uint16_t result = ((int16_t)aRaw <= (int16_t)bRaw) ? 1 : 0;
                        PushStackWord(result);
                        uint8_t aLo=aRaw&0xFF, aHi=aRaw>>8, bLo=bRaw&0xFF, bHi=bRaw>>8;
                        uint8_t xorResult = (uint8_t)(aHi ^ bHi);
                        uint8_t aFinal;
                        if (xorResult & 0x80) aFinal = (uint8_t)(xorResult & bHi);
                        else { bool borrow = bLo < aLo; aFinal = (uint8_t)(bHi - aHi - (borrow?1:0)); }
                        if (preserveZ80RegCompat) { cpu.r.A = aFinal; cpu.r.setDE(aRaw); cpu.r.setHL(result); }
                        commit = true;
                        opcodeEmulatedCount[0xC8]++; lastNativeOpcode=0xC8;
                        break;
                    }
                    case OP_LESI: { // LESI
                        uint16_t bRaw = PopStackWord(), aRaw = PopStackWord();
                        uint16_t result = ((int16_t)aRaw < (int16_t)bRaw) ? 1 : 0;
                        PushStackWord(result);
                        uint8_t aLo=aRaw&0xFF, aHi=aRaw>>8, bLo=bRaw&0xFF, bHi=bRaw>>8;
                        uint8_t xorResult = (uint8_t)(aHi ^ bHi);
                        uint8_t aFinal;
                        if (xorResult & 0x80) aFinal = (uint8_t)(xorResult & bHi);
                        else { bool borrow = aLo < bLo; aFinal = (uint8_t)(aHi - bHi - (borrow?1:0)); }
                        if (preserveZ80RegCompat) { cpu.r.A = aFinal; cpu.r.setDE(aRaw); cpu.r.setHL(result); }
                        commit = true;
                        opcodeEmulatedCount[0xC9]++; lastNativeOpcode=0xC9;
                        break;
                    }
                    case OP_UJP: { // UJP
                        uint8_t ofs = mem[cpu.r.BC()];
                        if ((ofs & 0x80) == 0) {
                            uint16_t newBC = (uint16_t)(cpu.r.BC() + 1 + ofs);
                            cpu.r.setBC(newBC);
                            if (preserveZ80RegCompat) cpu.r.A = (uint8_t)(newBC >> 8);
                            commit = true;
                            opcodeEmulatedCount[0xB9]++; lastNativeOpcode=0xB9;
                        } else {
                            // Long jump through the procedure's jump table (offset >= 0x80): the entry
                            // at JTAB + (offset - 256) holds a self-relative pointer to the target
                            // (Z80 UJP $10: LD HL,(JTAB); BC := FFxx; ADD HL,BC; SELREL).
                            uint16_t jtab = (uint16_t)(mem[0x02F4] | (mem[0x02F5] << 8));
                            uint16_t entry = (uint16_t)(jtab + ofs - 256);
                            uint16_t rel = (uint16_t)(mem[entry] | (mem[(uint16_t)(entry + 1)] << 8));
                            uint16_t target = (uint16_t)(entry - rel);
                            cpu.r.setBC(target);
                            if (preserveZ80RegCompat) { cpu.r.A = ofs; cpu.r.setDE(rel); cpu.r.setHL(target); }
                            commit = true;
                            opcodeEmulatedCount[0xB9]++; lastNativeOpcode=0xB9;
                        }
                        break;
                    }
                    case OP_STL: { // STL
                        uint16_t offset = DecodeGBDE();
                        uint16_t mpd0 = (uint16_t)(mem[0x0242] | (mem[0x0243] << 8));
                        uint16_t addr = (uint16_t)(mpd0 + offset * 2);
                        uint16_t value = PopStackWord();
                        mem[addr] = value & 0xFF;
                        mem[(uint16_t)(addr + 1)] = value >> 8;
                        if (preserveZ80RegCompat) { cpu.r.setDE(value); cpu.r.setHL((uint16_t)(addr + 1)); }
                        commit = true;
                        opcodeEmulatedCount[0xCC]++; lastNativeOpcode=0xCC;
                        break;
                    }
                    case OP_IXA: { // IXA
                        uint16_t elementSize = DecodeGBDE();
                        mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8; // SAVIPC
                        uint16_t index = PopStackWord();
                        uint16_t offset = (uint16_t)(index * elementSize * 2);
                        uint16_t base = PopStackWord();
                        uint16_t result = (uint16_t)(offset + base);
                        PushStackWord(result);
                        if (preserveZ80RegCompat) { cpu.r.A = (uint8_t)((elementSize & 0xFF) - 1); cpu.r.setDE(elementSize == 1 ? elementSize : (uint16_t)0); cpu.r.setHL(result); }
                        commit = true;
                        opcodeEmulatedCount[0xA4]++; lastNativeOpcode=0xA4;
                        break;
                    }
                    case OP_LDB: { // LDB
                        uint16_t index = PopStackWord();
                        uint16_t base = PopStackWord();
                        uint16_t addr = (uint16_t)(base + index);
                        uint16_t value = mem[addr];
                        PushStackWord(value);
                        if (preserveZ80RegCompat) { cpu.r.setDE(value); cpu.r.setHL(addr); }
                        commit = true;
                        opcodeEmulatedCount[0xBE]++; lastNativeOpcode=0xBE;
                        break;
                    }
                    case OP_LAO: { // LAO
                        uint16_t offset = DecodeGBDE();
                        uint16_t based0 = (uint16_t)(mem[0x0244] | (mem[0x0245] << 8));
                        uint16_t addr = (uint16_t)(based0 + offset * 2);
                        PushStackWord(addr);
                        if (preserveZ80RegCompat) { cpu.r.setDE(offset); cpu.r.setHL(addr); }
                        commit = true;
                        opcodeEmulatedCount[0xA5]++; lastNativeOpcode=0xA5;
                        break;
                    }
                    case OP_LAND: { // LAND
                        uint16_t deRaw = PopStackWord();
                        uint16_t hlRaw = PopStackWord();
                        uint16_t result = (uint16_t)(deRaw & hlRaw);
                        PushStackWord(result);
                        if (preserveZ80RegCompat) { cpu.r.A = (uint8_t)(result >> 8); cpu.r.setDE(deRaw); cpu.r.setHL(result); }
                        commit = true;
                        opcodeEmulatedCount[0x84]++; lastNativeOpcode=0x84;
                        break;
                    }
                    case OP_INCR: { // INCR
                        uint16_t literal = DecodeGBDE();
                        uint16_t value = PopStackWord();
                        uint16_t result = (uint16_t)(value + literal * 2);
                        PushStackWord(result);
                        if (preserveZ80RegCompat) { cpu.r.setDE(literal); cpu.r.setHL(result); }
                        commit = true;
                        opcodeEmulatedCount[0xA2]++; lastNativeOpcode=0xA2;
                        break;
                    }
                    case OP_LDL: { // LDL
                        uint16_t offset = DecodeGBDE();
                        uint16_t mpd0 = (uint16_t)(mem[0x0242] | (mem[0x0243] << 8));
                        uint16_t addr = (uint16_t)(mpd0 + offset * 2);
                        uint16_t value = (uint16_t)(mem[addr] | (mem[(uint16_t)(addr + 1)] << 8));
                        PushStackWord(value);
                        if (preserveZ80RegCompat) { cpu.r.setDE(value); cpu.r.setHL((uint16_t)(addr + 1)); }
                        commit = true;
                        opcodeEmulatedCount[0xCA]++; lastNativeOpcode=0xCA;
                        break;
                    }
                    case OP_LOR: { // LOR
                        uint16_t x = PopStackWord(), y = PopStackWord();
                        uint16_t result = (uint16_t)(x | y);
                        PushStackWord(result);
                        if (preserveZ80RegCompat) { cpu.r.A = (uint8_t)(result >> 8); cpu.r.setDE(y); cpu.r.setHL(result); }
                        commit = true;
                        opcodeEmulatedCount[0x8D]++; lastNativeOpcode=0x8D;
                        break;
                    }
                    case OP_STB: { // STB
                        uint16_t charWord = PopStackWord();
                        uint16_t index = PopStackWord();
                        uint16_t base = PopStackWord();
                        uint16_t addr = (uint16_t)(base + index);
                        uint8_t ch = (uint8_t)(charWord & 0xFF);
                        mem[addr] = ch;
                        if (preserveZ80RegCompat) { cpu.r.A = ch; cpu.r.setDE(index); cpu.r.setHL(addr); }
                        commit = true;
                        opcodeEmulatedCount[0xBF]++; lastNativeOpcode=0xBF;
                        break;
                    }
                    case OP_STP: { // STP
                        uint16_t bcIpc = cpu.r.BC();
                        mem[0x0246] = bcIpc & 0xFF; mem[0x0247] = bcIpc >> 8;

                        uint16_t data = PopStackWord();
                        uint16_t rightBitNumberWord = PopStackWord();
                        uint8_t rightBitNumber = (uint8_t)(rightBitNumberWord & 0xFF);
                        uint16_t bitsPerElementWord = PopStackWord();
                        uint8_t bitsPerElement = (uint8_t)(bitsPerElementWord & 0xFF);
                        uint16_t targetAddr = PopStackWord();

                        uint16_t mask = (uint16_t)((1u << bitsPerElement) - 1);

                        uint32_t shiftedMask32 = ((uint32_t)mask) << rightBitNumber;
                        uint32_t shiftedData32 = ((uint32_t)data) << rightBitNumber;
                        uint16_t shiftedMask = (uint16_t)shiftedMask32;
                        uint16_t shiftedData = (uint16_t)shiftedData32;

                        uint16_t targetWord = (uint16_t)(mem[targetAddr] | (mem[(uint16_t)(targetAddr + 1)] << 8));
                        uint16_t newWord = (uint16_t)((targetWord & (uint16_t)~shiftedMask) | shiftedData);

                        mem[targetAddr] = newWord & 0xFF;
                        mem[(uint16_t)(targetAddr + 1)] = newWord >> 8;

                        if (preserveZ80RegCompat) { cpu.r.A = (uint8_t)(newWord >> 8); cpu.r.setDE(shiftedData); cpu.r.setHL((uint16_t)(targetAddr + 1)); }
                        commit = true;
                        opcodeEmulatedCount[0xBB]++; lastNativeOpcode=0xBB;
                        break;
                    }
                    case OP_LDA: { // LDA
                        uint16_t addr = GetIA();
                        PushStackWord(addr);
                        if (preserveZ80RegCompat) cpu.r.setHL(addr);
                        commit = true;
                        opcodeEmulatedCount[0xB2]++; lastNativeOpcode=0xB2;
                        break;
                    }
                    case OP_LDCI: { // LDCI
                        uint16_t bc = cpu.r.BC();
                        uint8_t lo = mem[bc];
                        uint8_t hi = mem[(uint16_t)(bc + 1)];
                        cpu.r.setBC((uint16_t)(bc + 2));
                        uint16_t value = (uint16_t)((hi << 8) | lo);
                        PushStackWord(value);
                        if (preserveZ80RegCompat) { cpu.r.A = hi; cpu.r.setHL(value); }
                        commit = true;
                        opcodeEmulatedCount[0xC7]++; lastNativeOpcode=0xC7;
                        break;
                    }
                    case OP_RBP: { // RBP
                        uint16_t mpVal = (uint16_t)(mem[0x02F2] | (mem[0x02F3] << 8));
                        uint16_t newBase = (uint16_t)(mem[(uint16_t)(mpVal - 2)] | (mem[(uint16_t)(mpVal - 1)] << 8));
                        uint16_t newBased0FromRBP = (uint16_t)(newBase + 10);

                        uint16_t mpd0Val = (uint16_t)(mem[0x0242] | (mem[0x0243] << 8));
                        uint16_t oldSP = (uint16_t)(mem[mpd0Val] | (mem[(uint16_t)(mpd0Val + 1)] << 8));
                        uint8_t numWords = mem[cpu.r.BC()];
                        uint16_t bytesToReturn = (uint16_t)(numWords * 2);
                        uint16_t srcStart = (uint16_t)(mpd0Val + 2);
                        uint16_t destStart = (uint16_t)(oldSP - bytesToReturn);
                        uint16_t newSP = (bytesToReturn == 0) ? oldSP : destStart;

                        uint16_t framePtr = mpVal;
                        framePtr = (uint16_t)(framePtr + 2);
                        uint16_t newMP = (uint16_t)(mem[framePtr] | (mem[(uint16_t)(framePtr + 1)] << 8));
                        framePtr = (uint16_t)(framePtr + 2);
                        uint16_t newMPD0 = (uint16_t)(newMP + 10);
                        uint16_t newJTAB = (uint16_t)(mem[framePtr] | (mem[(uint16_t)(framePtr + 1)] << 8));
                        framePtr = (uint16_t)(framePtr + 2);
                        uint16_t newSegCandidate = (uint16_t)(mem[framePtr] | (mem[(uint16_t)(framePtr + 1)] << 8));
                        framePtr = (uint16_t)(framePtr + 2);

                        uint16_t curSegP = (uint16_t)(mem[0x02F6] | (mem[0x02F7] << 8));
                        { // same OR different segment -- both native now
#include "NativeSegReturn.inc"
                            mem[0x02F0] = newBase & 0xFF; mem[0x02F1] = newBase >> 8;
                            mem[0x0244] = newBased0FromRBP & 0xFF; mem[0x0245] = newBased0FromRBP >> 8;

                            if (bytesToReturn > 0) {
                                if (destStart <= srcStart) {
                                    for (uint16_t i = 0; i < bytesToReturn; i++)
                                        mem[(uint16_t)(destStart + i)] = mem[(uint16_t)(srcStart + i)];
                                } else {
                                    for (uint16_t i = bytesToReturn; i-- > 0; )
                                        mem[(uint16_t)(destStart + i)] = mem[(uint16_t)(srcStart + i)];
                                }
                            }
                            uint16_t newBC = (uint16_t)(mem[framePtr] | (mem[(uint16_t)(framePtr + 1)] << 8));

                            mem[0x02F2] = newMP & 0xFF; mem[0x02F3] = newMP >> 8;
                            mem[0x0242] = newMPD0 & 0xFF; mem[0x0243] = newMPD0 >> 8;
                            mem[0x02F4] = newJTAB & 0xFF; mem[0x02F5] = newJTAB >> 8;
                            mem[0x02F6] = newSegCandidate & 0xFF; mem[0x02F7] = newSegCandidate >> 8;

                            if (preserveZ80RegCompat) cpu.r.A = segRetA;
                            if (preserveZ80RegCompat) cpu.r.setDE(newSegCandidate);
                            if (preserveZ80RegCompat) cpu.r.setHL(newSP);
                            cpu.r.SP = newSP;
                            cpu.r.setBC(newBC);
                            commit = true;
                            opcodeEmulatedCount[0xC1]++; lastNativeOpcode=0xC1;
                        }
                        break;
                    }
                    case OP_STO: { // STO
                        uint16_t value = PopStackWord();
                        uint16_t addr = PopStackWord();
                        mem[addr] = value & 0xFF;
                        mem[(uint16_t)(addr + 1)] = value >> 8;
                        if (preserveZ80RegCompat) { cpu.r.setDE(value); cpu.r.setHL((uint16_t)(addr + 1)); }
                        commit = true;
                        opcodeEmulatedCount[0x9A]++; lastNativeOpcode=0x9A;
                        break;
                    }
                    case OP_STIND: { // STIND (loads, despite the name)
                        uint16_t base = PopStackWord();
                        uint16_t index = DecodeGBDE();
                        uint16_t addr = (uint16_t)(base + index * 2);
                        uint16_t value = (uint16_t)(mem[addr] | (mem[(uint16_t)(addr + 1)] << 8));
                        PushStackWord(value);
                        if (preserveZ80RegCompat) { cpu.r.setDE(value); cpu.r.setHL((uint16_t)(addr + 1)); }
                        commit = true;
                        opcodeEmulatedCount[0xA3]++; lastNativeOpcode=0xA3;
                        break;
                    }
                    case OP_CSP: { // CSP (dispatch shortcut + native transcendentals 25-31)
                        uint8_t procNum = mem[cpu.r.BC()];

                        if (procNum >= 25 && procNum <= 31) {
                            uint16_t lowWord  = PeekStackWord(0);
                            uint16_t highWord = PeekStackWord(2);
                            uint8_t b0 = lowWord & 0xFF, b1 = lowWord >> 8;
                            uint8_t b2 = highWord & 0xFF, b3 = highWord >> 8;
                            double x = DecodeUcsdReal(b0, b1, b2, b3);

                            bool domainOk = true;
                            if ((procNum == 27 || procNum == 29) && x <= 0.0) domainOk = false;
                            if (procNum == 31 && x < 0.0) domainOk = false;

                            if (domainOk) {
                                double result;
                                switch (procNum) {
                                    case 25: result = std::sin(x);   break;
                                    case 26: result = std::cos(x);   break;
                                    case 27: result = std::log10(x); break;
                                    case 28: result = std::atan(x);  break;
                                    case 29: result = std::log(x);   break;
                                    case 30: result = std::exp(x);   break;
                                    case 31: result = std::sqrt(x);  break;
                                    default: result = 0.0; break;
                                }
                                if (std::isfinite(result)) {
                                    uint8_t r0, r1, r2, r3;
                                    if (EncodeUcsdReal(result, r0, r1, r2, r3)) {
                                        PopStackWord();
                                        PopStackWord();
                                        PushStackWord((uint16_t)((r3 << 8) | r2));
                                        PushStackWord((uint16_t)((r1 << 8) | r0));
                                        cpu.r.setBC((uint16_t)(cpu.r.BC() + 1));
                                        mem[0x0246] = cpu.r.BC() & 0xFF;
                                        mem[0x0247] = cpu.r.BC() >> 8;
                                        cpu.r.PC = 0x03A4;
                                        opcodeEmulatedCount[OP_CSP]++; lastNativeOpcode=OP_CSP;
                                        break;
                                    }
                                }
                            }
                        }

#include "NativeCsp.inc"
                        uint16_t entryAddr = (uint16_t)(0x15E7 + procNum * 2);
                        uint16_t target = (uint16_t)(mem[entryAddr] | (mem[(uint16_t)(entryAddr + 1)] << 8));
                        if (target != 0) {
                            cpu.r.setBC((uint16_t)(cpu.r.BC() + 1));
                            mem[0x0246] = cpu.r.BC() & 0xFF;
                            mem[0x0247] = cpu.r.BC() >> 8;
                            cpu.r.A = (uint8_t)(target >> 8);
                            cpu.r.setDE((uint16_t)(entryAddr + 1));
                            cpu.r.setHL(target);
                            cpu.r.PC = target;
                            opcodeEmulatedCount[OP_CSP]++; lastNativeOpcode=OP_CSP;
                        }
                        break;
                    }
                    case OP_LLA: { // LLA
                        uint16_t offset = DecodeGBDE();
                        uint16_t mpd0 = (uint16_t)(mem[0x0242] | (mem[0x0243] << 8));
                        uint16_t addr = (uint16_t)(mpd0 + offset * 2);
                        PushStackWord(addr);
                        if (preserveZ80RegCompat) { cpu.r.setDE(offset); cpu.r.setHL(addr); }
                        commit = true;
                        opcodeEmulatedCount[0xC6]++; lastNativeOpcode=0xC6;
                        break;
                    }
                    case OP_SIND0: { // SIND0
                        uint16_t addr = PopStackWord();
                        uint16_t value = (uint16_t)(mem[addr] | (mem[(uint16_t)(addr + 1)] << 8));
                        PushStackWord(value);
                        if (preserveZ80RegCompat) { cpu.r.setDE(value); cpu.r.setHL((uint16_t)(addr + 1)); }
                        commit = true;
                        opcodeEmulatedCount[OP_SIND0]++; lastNativeOpcode=OP_SIND0;
                        break;
                    }
                    case OP_LOD: { // LOD
                        uint16_t addr = GetIA();
                        uint16_t value = (uint16_t)(mem[addr] | (mem[(uint16_t)(addr + 1)] << 8));
                        PushStackWord(value);
                        if (preserveZ80RegCompat) { cpu.r.setDE(value); cpu.r.setHL((uint16_t)(addr + 1)); }
                        commit = true;
                        opcodeEmulatedCount[0xB6]++; lastNativeOpcode=0xB6;
                        break;
                    }
                    case OP_LDCN: { // LDCN
                        PushStackWord(0x0001);
                        if (preserveZ80RegCompat) cpu.r.setHL(0x0001);
                        commit = true;
                        opcodeEmulatedCount[0x9F]++; lastNativeOpcode=0x9F;
                        break;
                    }
                    case OP_LDM: { // LDM
                        uint16_t bc = cpu.r.BC();
                        uint8_t count = mem[bc];
                        if (count == 0) break;
                        cpu.r.setBC((uint16_t)(bc + 1));
                        uint16_t srcAddr = PopStackWord();
                        for (int i = count - 1; i >= 0; i--) {
                            uint16_t word = (uint16_t)(mem[(uint16_t)(srcAddr + i * 2)] |
                                                        (mem[(uint16_t)(srcAddr + i * 2 + 1)] << 8));
                            PushStackWord(word);
                        }
                        uint16_t firstWord = (uint16_t)(mem[srcAddr] | (mem[(uint16_t)(srcAddr + 1)] << 8));
                        if (preserveZ80RegCompat) { cpu.r.A = 0; cpu.r.setDE(firstWord); cpu.r.setHL(srcAddr); }
                        commit = true;
                        opcodeEmulatedCount[0xBC]++; lastNativeOpcode=0xBC;
                        break;
                    }
                    case OP_STM: { // STM
                        uint16_t bc = cpu.r.BC();
                        uint8_t numWords = mem[bc];
                        bc = (uint16_t)(bc + 1);
                        cpu.r.setBC(bc);

                        if (numWords == 0) {
                            PopStackWord();
                            if (preserveZ80RegCompat) cpu.r.A = 0;
                            commit = true;
                            opcodeEmulatedCount[0xBD]++; lastNativeOpcode=0xBD;
                            break;
                        }

                        uint16_t destAddr = PeekStackWord(2 * numWords);

                        uint16_t lastPopped = 0;
                        for (int i = 0; i < numWords; i++) {
                            lastPopped = PopStackWord();
                            mem[(uint16_t)(destAddr + 2 * i)] = lastPopped & 0xFF;
                            mem[(uint16_t)(destAddr + 2 * i + 1)] = lastPopped >> 8;
                        }
                        PopStackWord(); // shared "$20: POP HL" tail

                        if (preserveZ80RegCompat) { cpu.r.A = 0; cpu.r.setDE(lastPopped); cpu.r.setHL(destAddr); }
                        commit = true;
                        opcodeEmulatedCount[0xBD]++; lastNativeOpcode=0xBD;
                        break;
                    }
                    case OP_INN: { // INN
                        uint16_t bcIpc = cpu.r.BC();
                        mem[0x0246] = bcIpc & 0xFF; mem[0x0247] = bcIpc >> 8;

                        uint16_t sp0 = cpu.r.SP;
                        uint16_t szaWord = (uint16_t)(mem[sp0] | (mem[(uint16_t)(sp0 + 1)] << 8));
                        uint8_t sza = (uint8_t)(szaWord & 0xFF);
                        uint16_t spAfterPop = (uint16_t)(sp0 + 2);

                        uint16_t iAddr = (uint16_t)(spAfterPop + 2 * sza);
                        uint16_t iVal = (uint16_t)(mem[iAddr] | (mem[(uint16_t)(iAddr + 1)] << 8));
                        uint16_t restOfStackAddr = (uint16_t)(iAddr + 2);

                        uint32_t sum = (uint32_t)0xF010 + (uint32_t)iVal;
                        if (sum > 0xFFFF) {   // INN $99: i < 0 or i >= 4080
                    if (preserveZ80RegCompat) break;
                    cpu.r.SP = restOfStackAddr;
                    PushStackWord(0);
                    cpu.r.PC = 0x03DA;
                    break;
                }

                        uint8_t wordIdx   = (uint8_t)(iVal / 16);
                        uint8_t bitInWord = (uint8_t)(iVal % 16);

                        mem[0x02A0] = wordIdx;   // IDIV := i div 16 (the Z80 RRD writes it before the size check)
                        if (wordIdx >= sza) {
                            // Element beyond the set's stored size: not in the set (Z80 INN $20:
                            // SP := ^rest of stack; push 0; BACK1). With register compatibility
                            // on it stays Z80 -- the real code leaves D holding a value from the
                            // previous instruction there.
                            if (preserveZ80RegCompat) break;
                            cpu.r.SP = restOfStackAddr;
                            PushStackWord(0);
                            commit = true;
                            opcodeEmulatedCount[0x8B]++;
                            break;
                        }

                        uint8_t bitInByte = (uint8_t)(bitInWord & 0x07);
                        uint8_t mask = (uint8_t)(1 << bitInByte);
                        uint16_t byteOff = (uint16_t)((wordIdx + 2) * 2);
                        if (bitInWord & 0x08) byteOff = (uint16_t)(byteOff + 1);
                        uint16_t targetAddr = (uint16_t)(byteOff + (sp0 - 2));
                        uint8_t testResult = (uint8_t)(mask & mem[targetAddr]);

                        cpu.r.SP = restOfStackAddr;
                        PushStackWord(testResult != 0 ? 1 : 0);
                        if (preserveZ80RegCompat) { cpu.r.A = testResult; cpu.r.setDE(bitInByte); cpu.r.setHL(testResult != 0 ? 1 : 0); }
                        commit = true;
                        opcodeEmulatedCount[0x8B]++; lastNativeOpcode=0x8B;
                        break;
                    }
                    case OP_CXP: { // CXP (same-segment fast path only)
                        uint16_t bc = cpu.r.BC();
                        uint8_t segNum = mem[bc];
                        uint16_t segp = (uint16_t)(mem[0x02F6] | (mem[0x02F7] << 8));
                        uint8_t curSegByte = mem[segp];

                        if (segNum != curSegByte) { // different segment: seg 0 / resident natively, disk read stays Z80
#include "NativeCxp.inc"
                            break;
                        }

                        cpu.r.setBC((uint16_t)(bc + 1));
                        if (preserveZ80RegCompat) cpu.r.A = segNum;
                        if (preserveZ80RegCompat) cpu.r.setHL(segp);
                        cpu.r.PC = 0x1369; // CIP -- remains real Z80 code
                        opcodeEmulatedCount[0xCD]++; lastNativeOpcode=0xCD;
                        break;
                    }
                    case OP_XJP: { // XJP (in-range case only)
                        uint16_t bc = cpu.r.BC();
                        bc = (uint16_t)(bc + 1);
                        uint16_t tableAddr = (uint16_t)(bc & 0xFFFE);
                        uint16_t minVal = (uint16_t)(mem[tableAddr] | (mem[(uint16_t)(tableAddr + 1)] << 8));
                        uint16_t maxVal = (uint16_t)(mem[(uint16_t)(tableAddr + 2)] | (mem[(uint16_t)(tableAddr + 3)] << 8));
                        uint16_t elseWordAddr = (uint16_t)(tableAddr + 4);

                        uint16_t indexVal = PeekStackWord(0);

                        int16_t indexS = (int16_t)indexVal, minS = (int16_t)minVal, maxS = (int16_t)maxVal;
                        if (indexS < minS || indexS > maxS) {
                            // Out of range: continue at the else-jump slot (Z80 XJP: IPCSAV :=
                            // its address; pop the index; BACK1). With register compatibility on it
                            // stays Z80 -- A/DE/HL there depend on which comparison branch exits.
                            if (preserveZ80RegCompat) break;
                            PopStackWord();
                            mem[0x0246] = elseWordAddr & 0xFF; mem[0x0247] = elseWordAddr >> 8; // IPCSAV
                            cpu.r.setBC(elseWordAddr);
                            commit = true;
                            opcodeEmulatedCount[0xAC]++;
                            break;
                        }

                        PopStackWord();
                        uint16_t entryAddr = (uint16_t)(elseWordAddr + 2 + 2 * (uint16_t)(indexS - minS));
                        uint16_t storedVal = (uint16_t)(mem[entryAddr] | (mem[(uint16_t)(entryAddr + 1)] << 8));
                        uint16_t target = (uint16_t)(entryAddr - storedVal);

                        mem[0x0246] = elseWordAddr & 0xFF; mem[0x0247] = elseWordAddr >> 8;

                        uint8_t minLow = (uint8_t)(minVal & 0xFF);
                        uint8_t indexHigh = (uint8_t)(indexVal >> 8);
                        uint8_t xorVal = (uint8_t)(minLow ^ indexHigh);
                        uint8_t aFinal = ((xorVal & 0x80) == 0) ? xorVal : (uint8_t)(xorVal & indexHigh);

                        if (preserveZ80RegCompat) { cpu.r.A = aFinal; cpu.r.setDE((uint16_t)(2 * (uint16_t)(indexS - minS + 1))); cpu.r.setHL(target); }
                        cpu.r.setBC(target);
                        commit = true;
                        opcodeEmulatedCount[0xAC]++; lastNativeOpcode=0xAC;
                        break;
                    }
                    case OP_STR: { // STR
                        uint16_t addr = GetIA();
                        uint16_t value = PopStackWord();
                        mem[addr] = value & 0xFF;
                        mem[(uint16_t)(addr + 1)] = value >> 8;
                        if (preserveZ80RegCompat) { cpu.r.setDE(value); cpu.r.setHL((uint16_t)(addr + 1)); }
                        commit = true;
                        opcodeEmulatedCount[0xB8]++; lastNativeOpcode=0xB8;
                        break;
                    }
                    case OP_LPA: { // LPA
                        uint16_t bc = cpu.r.BC();
                        uint8_t length = mem[bc];
                        bc = (uint16_t)(bc + 1);
                        uint16_t dataAddr = bc;
                        PushStackWord(dataAddr);
                        bc = (uint16_t)(bc + length);
                        cpu.r.setBC(bc);
                        if (preserveZ80RegCompat) cpu.r.A = (uint8_t)(bc >> 8);
                        commit = true;
                        opcodeEmulatedCount[0xD0]++; lastNativeOpcode=0xD0;
                        break;
                    }
                    case OP_LSA: { // LSA
                        uint16_t ipc = cpu.r.BC();
                        uint8_t strLen = mem[ipc];
                        uint8_t cAfterInc = (uint8_t)((ipc + 1) & 0xFF);
                        uint8_t bAfterInc = (uint8_t)((ipc + 1) >> 8);
                        uint16_t addR = (uint16_t)strLen + cAfterInc;
                        bool carry = (addR >= 256);
                        if (preserveZ80RegCompat) cpu.r.A = (uint8_t)(bAfterInc + (carry ? 1 : 0));
                        PushStackWord(ipc);
                        cpu.r.setBC((uint16_t)(ipc + 1 + strLen));
                        commit = true;
                        opcodeEmulatedCount[0xA6]++; lastNativeOpcode=0xA6;
                        break;
                    }
                    case OP_SAS: { // SAS
                        uint16_t bc = cpu.r.BC();
                        uint8_t maxlen = mem[bc];
                        mem[0x02A0] = maxlen;
                        bc = (uint16_t)(bc + 1); cpu.r.setBC(bc);
                        mem[0x0246] = bc & 0xFF; mem[0x0247] = bc >> 8;
                        uint16_t src = PopStackWord();
                        uint16_t srcPtr;
                        if ((src >> 8) == 0) {
                            mem[0x024E] = 1; mem[0x024F] = (uint8_t)(src & 0xFF);
                            srcPtr = 0x024E;
                        } else { srcPtr = src; }
                        uint8_t srcLen = mem[srcPtr];
                        if (srcLen > maxlen) { PopStackWord(); cpu.r.PC = 0x0420; break; }
                        uint16_t dst = PopStackWord();
                        uint16_t count = (uint16_t)(srcLen + 1);
                        for (uint16_t i = 0; i < count; i++)
                            mem[(uint16_t)(dst+i)] = mem[(uint16_t)(srcPtr+i)];
                        uint16_t ipcsav = (uint16_t)(mem[0x0246] | (mem[0x0247] << 8));
                        cpu.r.setBC(ipcsav);
                        if (preserveZ80RegCompat) cpu.r.setHL((uint16_t)(srcPtr + count));
                        if (preserveZ80RegCompat) cpu.r.setDE((uint16_t)(dst    + count));
                        if (preserveZ80RegCompat) cpu.r.A = maxlen;
                        commit = true;
                        opcodeEmulatedCount[0xAA]++; lastNativeOpcode=0xAA;
                        break;
                    }
                    case OP_LDP: { // LDP
                        mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8;
                        uint8_t rightBits = (uint8_t)(PopStackWord() & 0xFF);
                        uint8_t bitsPerE  = (uint8_t)(PopStackWord() & 0xFF);
                        uint16_t addr     = PopStackWord();
                        uint8_t e = mem[addr], d = mem[(uint16_t)(addr+1)];
                        uint8_t shift = rightBits;
                        if (shift >= 8) { uint8_t t=d; d=e; e=t; shift=(uint8_t)(shift-8); }
                        for (uint8_t s = 0; s < shift; s++) {
                            uint8_t bit = d & 1; d >>= 1;
                            e = (uint8_t)((e >> 1) | (bit << 7));
                        }
                        uint16_t mask = (bitsPerE >= 16) ? 0xFFFFu : (uint16_t)((1u<<bitsPerE)-1u);
                        e = (uint8_t)(e & (mask & 0xFF)); d = (uint8_t)(d & (mask >> 8));
                        uint16_t result = (uint16_t)((d << 8) | e);
                        PushStackWord(result);
                        uint16_t ipcsav = (uint16_t)(mem[0x0246] | (mem[0x0247] << 8));
                        cpu.r.setBC(ipcsav);
                        if (preserveZ80RegCompat) { cpu.r.setDE(result); cpu.r.setHL((uint16_t)(0x06E8 + (uint16_t)bitsPerE * 2 + 1)); cpu.r.A = d; }
                        commit = true;
                        opcodeEmulatedCount[0xBA]++; lastNativeOpcode=0xBA;
                        break;
                    }
                    case OP_CLP: { // CLP
                        if (!NativeBldmscw()) break;
                        if (preserveZ80RegCompat) cpu.r.setHL(0x133A);
                        commit = true;
                        opcodeEmulatedCount[0xCE]++; lastNativeOpcode=0xCE;
                        break;
                    }
                    case OP_CBP: { // CBP
                        uint16_t oldBASE = (uint16_t)(mem[0x02F0] | (mem[0x02F1] << 8));
                        if (!NativeBldmscw()) break;
                        uint16_t newMP   = (uint16_t)(mem[0x02F2] | (mem[0x02F3] << 8));
                        uint16_t newMPD0 = (uint16_t)(mem[0x0242] | (mem[0x0243] << 8));
                        uint16_t entryBC = cpu.r.BC();
                        PushStackWord(oldBASE);
                        mem[0x0244] = newMPD0 & 0xFF; mem[0x0245] = newMPD0 >> 8;
                        mem[0x02F0] = newMP   & 0xFF; mem[0x02F1] = newMP   >> 8;
                        uint16_t oldSL = (uint16_t)(mem[oldBASE] | (mem[(uint16_t)(oldBASE+1)] << 8));
                        mem[newMP] = oldSL & 0xFF; mem[(uint16_t)(newMP+1)] = oldSL >> 8;
                        cpu.r.setBC(entryBC);
                        if (preserveZ80RegCompat) cpu.r.setHL((uint16_t)(newMP + 1));
                        if (preserveZ80RegCompat) cpu.r.setDE((uint16_t)(oldBASE + 1));
                        commit = true;
                        opcodeEmulatedCount[0xC2]++; lastNativeOpcode=0xC2;
                        break;
                    }
                    case OP_CIP: { // CIP
                        uint16_t oldMP = (uint16_t)(mem[0x02F2] | (mem[0x02F3] << 8));
                        if (!NativeBldmscw()) break;

                        uint16_t newProcEntryAddr = cpu.r.BC();
                        uint16_t newMP   = (uint16_t)(mem[0x02F2] | (mem[0x02F3] << 8));
                        uint16_t newJtab = (uint16_t)(mem[0x02F4] | (mem[0x02F5] << 8));
                        uint8_t calledLexLevel = mem[(uint16_t)(newJtab + 1)];

                        if (((uint8_t)(calledLexLevel - 1) & 0x80) != 0) { // CIPXNL: DEC A; JP P -- base-level proc
                            uint16_t oldBASE = (uint16_t)(mem[0x02F0] | (mem[0x02F1] << 8));
                            uint16_t newMPD0 = (uint16_t)(mem[0x0242] | (mem[0x0243] << 8));
                            PushStackWord(oldBASE);
                            mem[0x0244] = newMPD0 & 0xFF; mem[0x0245] = newMPD0 >> 8;
                            mem[0x02F0] = newMP   & 0xFF; mem[0x02F1] = newMP   >> 8;
                            uint16_t oldStatLink = (uint16_t)(mem[oldBASE] | (mem[(uint16_t)(oldBASE + 1)] << 8));
                            mem[newMP] = oldStatLink & 0xFF; mem[(uint16_t)(newMP + 1)] = oldStatLink >> 8;
                            cpu.r.setBC(newProcEntryAddr);
                            if (preserveZ80RegCompat) cpu.r.setHL((uint16_t)(newMP + 1));
                            if (preserveZ80RegCompat) cpu.r.setDE((uint16_t)(oldBASE + 1));
                            if (preserveZ80RegCompat) cpu.r.A = (uint8_t)(calledLexLevel - 1); // CIPXNL left A = lex-1 (0xFF for lex 0) before JP CBPXNL
                        } else {
                            uint8_t targetLexLevel = (uint8_t)(calledLexLevel - 1);
                            uint16_t examineAddr = newMP;
                            uint16_t result = 0;
                            bool found = false;
                            for (int iter = 0; iter < 30000; iter++) {
                                uint16_t frameJtab = (uint16_t)(mem[(uint16_t)(examineAddr + 4)] | (mem[(uint16_t)(examineAddr + 5)] << 8));
                                uint16_t frameDyn  = (uint16_t)(mem[(uint16_t)(examineAddr + 2)] | (mem[(uint16_t)(examineAddr + 3)] << 8));
                                result = frameDyn;
                                uint8_t creatorLexLevel = mem[(uint16_t)(frameJtab + 1)];
                                if (creatorLexLevel == targetLexLevel) { found = true; break; }
                                examineAddr = frameDyn;
                            }
                            if (!found) {
                                // CIPXNL's $10 loop expects the IPC it pushed and A = the target lex level.
                                PushStackWord(newProcEntryAddr);
                                cpu.r.A = targetLexLevel;
                                cpu.r.setBC(examineAddr);
                                cpu.r.PC = 0x137F;
                                break;
                            }
                            mem[newMP] = result & 0xFF; mem[(uint16_t)(newMP + 1)] = result >> 8;
                            if (preserveZ80RegCompat) cpu.r.A = targetLexLevel;
                            if (preserveZ80RegCompat) cpu.r.setDE(newProcEntryAddr);
                            if (preserveZ80RegCompat) cpu.r.setHL(oldMP);
                            cpu.r.setBC(newProcEntryAddr);
                        }
                        commit = true;
                        opcodeEmulatedCount[0xAE]++; lastNativeOpcode=0xAE;
                        break;
                    }
                    case OP_CGP: { // CGP
                        if (!NativeBldmscw()) break;

                        uint16_t newMP = (uint16_t)(mem[0x02F2] | (mem[0x02F3] << 8));
                        uint16_t curBase = (uint16_t)(mem[0x02F0] | (mem[0x02F1] << 8));

                        mem[newMP] = curBase & 0xFF; mem[(uint16_t)(newMP + 1)] = curBase >> 8;

                        if (preserveZ80RegCompat) cpu.r.setHL(curBase);
                        commit = true;
                        opcodeEmulatedCount[0xCF]++; lastNativeOpcode=0xCF;
                        break;
                    }
                    case OP_DIF: { // DIF
                        uint16_t sp = cpu.r.SP;
                        uint16_t szbWord = PeekStackWord(0);
                        uint8_t szb = (uint8_t)(szbWord & 0xFF);
                        uint16_t szaAddr = (uint16_t)(sp + 2 * (1 + szb));
                        uint16_t szaWord = (uint16_t)(mem[szaAddr] | (mem[(uint16_t)(szaAddr + 1)] << 8));
                        uint8_t sza = (uint8_t)(szaWord & 0xFF);

                        mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8;

                        uint8_t minCount = (sza < szb) ? sza : szb;
                        uint16_t curSp = (uint16_t)(sp + 2);
                        uint16_t setAWordAddr = (uint16_t)(szaAddr + 2);
                        uint8_t aFinal = 0;
                        for (uint8_t i = 0; i < minCount; i++) {
                            uint16_t setBWord = (uint16_t)(mem[curSp] | (mem[(uint16_t)(curSp + 1)] << 8));
                            curSp = (uint16_t)(curSp + 2);
                            uint8_t lowResult  = (uint8_t)((~setBWord & 0xFF) & mem[setAWordAddr]);
                            mem[setAWordAddr] = lowResult;
                            uint8_t highResult = (uint8_t)((~(setBWord >> 8) & 0xFF) & mem[(uint16_t)(setAWordAddr + 1)]);
                            mem[(uint16_t)(setAWordAddr + 1)] = highResult;
                            setAWordAddr = (uint16_t)(setAWordAddr + 2);
                            aFinal = highResult;
                            if (i == (uint8_t)(minCount - 1) && preserveZ80RegCompat) cpu.r.setDE(setBWord);
                        }

                        cpu.r.SP = szaAddr;
                        if (preserveZ80RegCompat) { cpu.r.A = aFinal; cpu.r.setHL(szaAddr); }
                        commit = true;
                        opcodeEmulatedCount[0x85]++; lastNativeOpcode=0x85;
                        break;
                    }
                    case OP_UNI: { // UNI (Uniona sub-case only)
                        uint16_t sp = cpu.r.SP;
                        uint16_t szbWord = PeekStackWord(0);
                        uint8_t szb = (uint8_t)(szbWord & 0xFF);
                        uint16_t szaAddr = (uint16_t)(sp + 2 * (1 + szb));
                        uint16_t szaWord = (uint16_t)(mem[szaAddr] | (mem[(uint16_t)(szaAddr + 1)] << 8));
                        uint8_t sza = (uint8_t)(szaWord & 0xFF);

                        if (sza < szb) {   // Unionb: native with register compatibility off
                            if (PM_PRESERVE) break;
#include "NativeUnionb.inc"
                            break;
                        }

                        mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8;

                        uint16_t curSp = (uint16_t)(sp + 2);
                        uint16_t setAWordAddr = (uint16_t)(szaAddr + 2);
                        uint8_t aFinal = 0;
                        for (uint8_t i = 0; i < szb; i++) {
                            uint16_t setBWord = (uint16_t)(mem[curSp] | (mem[(uint16_t)(curSp + 1)] << 8));
                            curSp = (uint16_t)(curSp + 2);
                            uint8_t lowResult  = (uint8_t)((setBWord & 0xFF) | mem[setAWordAddr]);
                            mem[setAWordAddr] = lowResult;
                            uint8_t highResult = (uint8_t)((setBWord >> 8) | mem[(uint16_t)(setAWordAddr + 1)]);
                            mem[(uint16_t)(setAWordAddr + 1)] = highResult;
                            setAWordAddr = (uint16_t)(setAWordAddr + 2);
                            aFinal = highResult;
                            if (i == (uint8_t)(szb - 1) && preserveZ80RegCompat) cpu.r.setDE(setBWord);
                        }

                        cpu.r.SP = szaAddr;
                        if (preserveZ80RegCompat) { cpu.r.A = aFinal; cpu.r.setHL(szaAddr); }
                        commit = true;
                        opcodeEmulatedCount[0x9C]++; lastNativeOpcode=0x9C;
                        break;
                    }
                    case OP_SGS: { // SGS
                        uint16_t i = PeekStackWord(0);
                        if (((int16_t)i) < 0) {   // SRS $99
                    if (preserveZ80RegCompat) break;
                    mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8; // SAVIPC
                    mem[cpu.r.SP] = 0; mem[(uint16_t)(cpu.r.SP + 1)] = 0;
                    cpu.r.PC = 0x03DA;
                    break;
                }
                        uint32_t boundCheck = (uint32_t)0xF010 + (uint32_t)i;
                        if (boundCheck > 0xFFFF) {   // SRS $99
                    if (preserveZ80RegCompat) break;
                    mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8; // SAVIPC
                    mem[cpu.r.SP] = 0; mem[(uint16_t)(cpu.r.SP + 1)] = 0;
                    cpu.r.PC = 0x03DA;
                    break;
                }
                        PopStackWord();
                        NativeSrs(i, i);
                        opcodeEmulatedCount[0x97]++; lastNativeOpcode=0x97;
                        break;
                    }
                    case OP_SRS: { // SRS
                        uint16_t j = PeekStackWord(0);
                        uint16_t i = PeekStackWord(2);
                        if (((int16_t)i) < 0) {   // SRS $99
                    if (preserveZ80RegCompat) break;
                    mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8; // SAVIPC
                    PopStackWord();
                    mem[cpu.r.SP] = 0; mem[(uint16_t)(cpu.r.SP + 1)] = 0;
                    cpu.r.PC = 0x03DA;
                    break;
                }
                        uint32_t boundCheck = (uint32_t)0xF010 + (uint32_t)j;
                        if (boundCheck > 0xFFFF) {   // SRS $99
                    if (preserveZ80RegCompat) break;
                    mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8; // SAVIPC
                    PopStackWord();
                    mem[cpu.r.SP] = 0; mem[(uint16_t)(cpu.r.SP + 1)] = 0;
                    cpu.r.PC = 0x03DA;
                    break;
                }
                        PopStackWord();
                        PopStackWord();
                        NativeSrs(i, j);
                        opcodeEmulatedCount[0x94]++; lastNativeOpcode=0x94;
                        break;
                    }
                    case OP_IXP: { // IXP
                        uint16_t bc = cpu.r.BC();
                        uint8_t elementsPerWord = mem[bc];
                        bc = (uint16_t)(bc + 1);
                        uint8_t bitsPerElement = mem[bc];
                        bc = (uint16_t)(bc + 1);
                        if (preserveZ80RegCompat) cpu.r.setBC(bc); // cosmetic only -- SAVIPC below uses local bc directly
                        mem[0x0246] = bc & 0xFF; mem[0x0247] = bc >> 8;

                        uint16_t index = PopStackWord();
                        uint16_t quotient  = (elementsPerWord != 0) ? (uint16_t)(index / elementsPerWord) : 0;
                        uint16_t remainder = (elementsPerWord != 0) ? (uint16_t)(index % elementsPerWord) : index;

                        uint16_t baseAddr = PopStackWord();
                        uint16_t indexedWordAddr = (uint16_t)(baseAddr + 2 * quotient);
                        PushStackWord(indexedWordAddr);
                        PushStackWord(bitsPerElement);

                        uint32_t reps = (bitsPerElement == 0) ? 256u : (uint32_t)bitsPerElement;
                        uint8_t rightBitNumber = 0;
                        for (uint32_t k = 0; k < reps; k++)
                            rightBitNumber = (uint8_t)(rightBitNumber + (remainder & 0xFF));
                        PushStackWord(rightBitNumber);

                        if (preserveZ80RegCompat) { cpu.r.A = rightBitNumber; cpu.r.setDE(remainder); cpu.r.setHL(rightBitNumber); }
                        cpu.r.PC = 0x03A4;
                        opcodeEmulatedCount[0xC0]++; lastNativeOpcode=0xC0;
                        break;
                    }
                    case OP_ADJ: { // ADJ
                        uint16_t bc = cpu.r.BC();
                        uint8_t szFinalW = mem[bc];
                        bc = (uint16_t)(bc + 1);
                        cpu.r.setBC(bc);
                        uint16_t szOrigW = PopStackWord();

                        uint16_t szFinalB = (uint16_t)(szFinalW * 2);
                        uint16_t szOrigB  = (uint16_t)(szOrigW * 2);

                        if (szOrigB == szFinalB) {
                            if (preserveZ80RegCompat) { cpu.r.A = (uint8_t)(szFinalW & 0xFF); cpu.r.setDE(szFinalB); cpu.r.setHL(0); }
                            commit = true;
                            opcodeEmulatedCount[0xA0]++; lastNativeOpcode=0xA0;
                            break;
                        }

                        mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8;

                        uint16_t spOld = cpu.r.SP;

                        if (szOrigB > szFinalB) {
                            uint16_t dst = (uint16_t)(spOld + szOrigB - 1);
                            uint16_t src = (uint16_t)(spOld + szFinalB - 1);
                            for (uint16_t i = 0; i < szFinalB; i++)
                                mem[(uint16_t)(dst - i)] = mem[(uint16_t)(src - i)];
                            uint16_t newSp = (uint16_t)(spOld + szOrigB - szFinalB);
                            cpu.r.SP = newSp;
                            if (preserveZ80RegCompat) { cpu.r.A = (uint8_t)(szFinalW & 0xFF); cpu.r.setDE((uint16_t)(spOld - 1)); cpu.r.setHL(newSp); }
                        } else {
                            uint16_t newSp = (uint16_t)(spOld - (szFinalB - szOrigB));
                            cpu.r.SP = newSp;
                            for (uint16_t i = 0; i < szOrigB; i++)
                                mem[(uint16_t)(newSp + i)] = mem[(uint16_t)(spOld + i)];
                            for (uint16_t i = szOrigB; i < szFinalB; i++)
                                mem[(uint16_t)(newSp + i)] = 0;
                            if (preserveZ80RegCompat) { cpu.r.A = 0x00; cpu.r.setDE((uint16_t)(spOld + szOrigB)); cpu.r.setHL((uint16_t)(spOld + szOrigB - 1)); }
                        }

                        commit = true;
                        opcodeEmulatedCount[0xA0]++; lastNativeOpcode=0xA0;
                        break;
                    }
                    case OP_MOV: { // MOV
                        uint16_t numWords = DecodeGBDE();

                        mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8;

                        uint16_t byteCount = (uint16_t)(numWords * 2);
                        uint16_t source = PopStackWord();
                        uint16_t dest   = PopStackWord();

                        uint32_t iterations = (byteCount == 0) ? 65536u : (uint32_t)byteCount;
                        for (uint32_t i = 0; i < iterations; i++)
                            mem[(uint16_t)(dest + i)] = mem[(uint16_t)(source + i)];

                        if (preserveZ80RegCompat) { cpu.r.A = (uint8_t)(byteCount >> 8); cpu.r.setDE((uint16_t)(dest + byteCount)); cpu.r.setHL((uint16_t)(source + byteCount)); }
                        commit = true;
                        opcodeEmulatedCount[0xA8]++; lastNativeOpcode=0xA8;
                        break;
                    }
                    case OP_LDC: { // LDC
                        uint16_t bc = cpu.r.BC();
                        uint8_t numWords = mem[bc];
                        uint16_t hl = (uint16_t)(bc + 2);
                        hl = (uint16_t)(hl & 0xFFFE);
                        uint8_t aFinal = (uint8_t)(hl & 0xFF);
                        uint16_t de = 0;
                        uint8_t b = numWords;
                        do {
                            de = (uint16_t)(mem[hl] | (mem[(uint16_t)(hl + 1)] << 8));
                            hl = (uint16_t)(hl + 2);
                            PushStackWord(de);
                            b = (uint8_t)(b - 1);
                        } while (b != 0);

                        cpu.r.setBC(hl);
                        if (preserveZ80RegCompat) { cpu.r.A = aFinal; cpu.r.setDE(de); cpu.r.setHL(hl); }
                        commit = true;
                        opcodeEmulatedCount[0xB3]++; lastNativeOpcode=0xB3;
                        break;
                    }
                    case OP_DVI: { // DVI
                        uint16_t divisorPeek = PeekStackWord(0);
                        if (divisorPeek == 0 || divisorPeek == 0x8000) {   // DIVZER
                    if (preserveZ80RegCompat) break;
                    mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8; // SAVIPC
                    PopStackWord();                                   // divisor; the dividend stays (DIVD $99)
                    cpu.r.PC = 0x03F9;                                // DIVZER (NativeErrors.inc)
                    break;
                }

                        uint16_t bcIpc = cpu.r.BC();
                        mem[0x0246] = bcIpc & 0xFF; mem[0x0247] = bcIpc >> 8;

                        uint16_t divisor  = PopStackWord();
                        uint16_t dividend = PopStackWord();
                        bool divisorNeg  = ((int16_t)divisor)  < 0;
                        bool dividendNeg = ((int16_t)dividend) < 0;

                        auto sbc8 = [](uint8_t a, uint8_t b, bool borrowIn, bool& borrowOut) -> uint8_t {
                            int diff = (int)a - (int)b - (borrowIn ? 1 : 0);
                            borrowOut = (diff < 0);
                            return (uint8_t)diff;
                        };

                        uint16_t deQuot, hlRem;
                        uint8_t aFinal;

                        if (!divisorNeg && !dividendNeg) {
                            deQuot = (uint16_t)(dividend / divisor);
                            hlRem  = (uint16_t)(dividend % divisor);
                            aFinal = 0;
                        } else if (!divisorNeg && dividendNeg) {
                            uint16_t hlIn = (uint16_t)(~dividend);
                            uint16_t q = (uint16_t)(hlIn / divisor);
                            uint16_t r = (uint16_t)(hlIn % divisor);
                            deQuot = (uint16_t)(~q);
                            bool borrow;
                            uint8_t lo = sbc8((uint8_t)(divisor & 0xFF), (uint8_t)(r & 0xFF), true, borrow);
                            uint8_t hi = sbc8((uint8_t)(divisor >> 8), (uint8_t)(r >> 8), borrow, borrow);
                            hlRem = (uint16_t)((hi << 8) | lo);
                            aFinal = hi;
                        } else {
                            uint16_t bcDiv = (uint16_t)(0 - divisor);
                            if (dividendNeg || dividend == 0) {
                                uint16_t hlIn = (uint16_t)(0 - dividend);
                                uint16_t q = (uint16_t)(hlIn / bcDiv);
                                uint16_t r = (uint16_t)(hlIn % bcDiv);
                                deQuot = q;
                                bool borrow;
                                uint8_t lo = sbc8(0, (uint8_t)(r & 0xFF), false, borrow);
                                uint8_t hi = sbc8(0, (uint8_t)(r >> 8), borrow, borrow);
                                hlRem = (uint16_t)((hi << 8) | lo);
                                aFinal = hi;
                            } else {
                                uint16_t hlIn = (uint16_t)(dividend - 1);
                                uint16_t q = (uint16_t)(hlIn / bcDiv);
                                uint16_t r = (uint16_t)(hlIn % bcDiv);
                                deQuot = (uint16_t)(~q);
                                int32_t sub = (int32_t)r - (int32_t)bcDiv;
                                hlRem = (uint16_t)(sub + 1);
                                aFinal = (uint8_t)(deQuot >> 8);
                            }
                        }

                        PushStackWord(deQuot);
                        if (preserveZ80RegCompat) { cpu.r.A = aFinal; cpu.r.setDE(deQuot); cpu.r.setHL(hlRem); }
                        commit = true;
                        opcodeEmulatedCount[0x86]++; lastNativeOpcode=0x86;
                        break;
                    }
                    case OP_MPI: { // MPI
                        uint16_t bcIpc = cpu.r.BC();
                        mem[0x0246] = bcIpc & 0xFF; mem[0x0247] = bcIpc >> 8;

                        uint16_t multiplier   = PopStackWord();
                        uint16_t multiplicand = PopStackWord();

                        uint16_t product = (uint16_t)(multiplicand * multiplier);

                        int highestBit = -1;
                        for (int i = 15; i >= 0; i--) {
                            if (multiplier & (1u << i)) { highestBit = i; break; }
                        }
                        uint16_t bcFinal = (highestBit < 0) ? multiplicand : (uint16_t)(multiplicand << highestBit);

                        PushStackWord(product);
                        if (preserveZ80RegCompat) { cpu.r.setDE(0x0000); cpu.r.setHL(product); cpu.r.setBC(bcFinal); }
                        cpu.r.PC = 0x03A4; // BACK1 -- always restores BC from IPCSAV regardless
                        opcodeEmulatedCount[0x8F]++; lastNativeOpcode=0x8F;
                        break;
                    }
                    case OP_MODI: { // MODI
                        uint16_t divisorPeek = PeekStackWord(0);
                        if (divisorPeek == 0 || divisorPeek == 0x8000) {   // DIVZER
                    if (preserveZ80RegCompat) break;
                    mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8; // SAVIPC
                    PopStackWord();                                   // divisor; the dividend stays (DIVD $99)
                    cpu.r.PC = 0x03F9;                                // DIVZER (NativeErrors.inc)
                    break;
                }

                        uint16_t bcIpc = cpu.r.BC();
                        mem[0x0246] = bcIpc & 0xFF; mem[0x0247] = bcIpc >> 8;

                        uint16_t divisor  = PopStackWord();
                        uint16_t dividend = PopStackWord();
                        bool divisorNeg  = ((int16_t)divisor)  < 0;
                        bool dividendNeg = ((int16_t)dividend) < 0;

                        auto sbc8 = [](uint8_t a, uint8_t b, bool borrowIn, bool& borrowOut) -> uint8_t {
                            int diff = (int)a - (int)b - (borrowIn ? 1 : 0);
                            borrowOut = (diff < 0);
                            return (uint8_t)diff;
                        };

                        uint16_t deQuot, hlRem;
                        uint8_t aFinal;

                        if (!divisorNeg && !dividendNeg) {
                            deQuot = (uint16_t)(dividend / divisor);
                            hlRem  = (uint16_t)(dividend % divisor);
                            aFinal = 0;
                        } else if (!divisorNeg && dividendNeg) {
                            uint16_t hlIn = (uint16_t)(~dividend);
                            uint16_t q = (uint16_t)(hlIn / divisor);
                            uint16_t r = (uint16_t)(hlIn % divisor);
                            deQuot = (uint16_t)(~q);
                            bool borrow;
                            uint8_t lo = sbc8((uint8_t)(divisor & 0xFF), (uint8_t)(r & 0xFF), true, borrow);
                            uint8_t hi = sbc8((uint8_t)(divisor >> 8), (uint8_t)(r >> 8), borrow, borrow);
                            hlRem = (uint16_t)((hi << 8) | lo);
                            aFinal = hi;
                        } else {
                            uint16_t bcDiv = (uint16_t)(0 - divisor);
                            if (dividendNeg || dividend == 0) {
                                uint16_t hlIn = (uint16_t)(0 - dividend);
                                uint16_t q = (uint16_t)(hlIn / bcDiv);
                                uint16_t r = (uint16_t)(hlIn % bcDiv);
                                deQuot = q;
                                bool borrow;
                                uint8_t lo = sbc8(0, (uint8_t)(r & 0xFF), false, borrow);
                                uint8_t hi = sbc8(0, (uint8_t)(r >> 8), borrow, borrow);
                                hlRem = (uint16_t)((hi << 8) | lo);
                                aFinal = hi;
                            } else {
                                uint16_t hlIn = (uint16_t)(dividend - 1);
                                uint16_t q = (uint16_t)(hlIn / bcDiv);
                                uint16_t r = (uint16_t)(hlIn % bcDiv);
                                deQuot = (uint16_t)(~q);
                                int32_t sub = (int32_t)r - (int32_t)bcDiv;
                                hlRem = (uint16_t)(sub + 1);
                                aFinal = (uint8_t)(deQuot >> 8);
                            }
                        }

                        PushStackWord(hlRem);
                        if (preserveZ80RegCompat) { cpu.r.A = aFinal; cpu.r.setDE(deQuot); cpu.r.setHL(hlRem); }
                        commit = true;
                        opcodeEmulatedCount[0x8E]++; lastNativeOpcode=0x8E;
                        break;
                    }
                    case OP_RNP: { // RNP
                        uint16_t mpd0Val = (uint16_t)(mem[0x0242] | (mem[0x0243] << 8));
                        uint16_t oldSP = (uint16_t)(mem[mpd0Val] | (mem[(uint16_t)(mpd0Val + 1)] << 8));
                        uint8_t numWords = mem[cpu.r.BC()];
                        uint16_t bytesToReturn = (uint16_t)(numWords * 2);
                        uint16_t srcStart = (uint16_t)(mpd0Val + 2);
                        uint16_t destStart = (uint16_t)(oldSP - bytesToReturn);
                        uint16_t newSP = (bytesToReturn == 0) ? oldSP : destStart;

                        uint16_t framePtr = (uint16_t)(mem[0x02F2] | (mem[0x02F3] << 8));
                        framePtr = (uint16_t)(framePtr + 2);
                        uint16_t newMP = (uint16_t)(mem[framePtr] | (mem[(uint16_t)(framePtr + 1)] << 8));
                        framePtr = (uint16_t)(framePtr + 2);
                        uint16_t newMPD0 = (uint16_t)(newMP + 10);
                        uint16_t newJTAB = (uint16_t)(mem[framePtr] | (mem[(uint16_t)(framePtr + 1)] << 8));
                        framePtr = (uint16_t)(framePtr + 2);
                        uint16_t newSegCandidate = (uint16_t)(mem[framePtr] | (mem[(uint16_t)(framePtr + 1)] << 8));
                        framePtr = (uint16_t)(framePtr + 2);

                        uint16_t curSegP = (uint16_t)(mem[0x02F6] | (mem[0x02F7] << 8));
                        { // same OR different segment -- both native now
#include "NativeSegReturn.inc"
                            if (bytesToReturn > 0) {
                                if (destStart <= srcStart) {
                                    for (uint16_t i = 0; i < bytesToReturn; i++)
                                        mem[(uint16_t)(destStart + i)] = mem[(uint16_t)(srcStart + i)];
                                } else {
                                    for (uint16_t i = bytesToReturn; i-- > 0; )
                                        mem[(uint16_t)(destStart + i)] = mem[(uint16_t)(srcStart + i)];
                                }
                            }
                            uint16_t newBC = (uint16_t)(mem[framePtr] | (mem[(uint16_t)(framePtr + 1)] << 8));

                            mem[0x02F2] = newMP & 0xFF; mem[0x02F3] = newMP >> 8;
                            mem[0x0242] = newMPD0 & 0xFF; mem[0x0243] = newMPD0 >> 8;
                            mem[0x02F4] = newJTAB & 0xFF; mem[0x02F5] = newJTAB >> 8;
                            mem[0x02F6] = newSegCandidate & 0xFF; mem[0x02F7] = newSegCandidate >> 8;

                            if (preserveZ80RegCompat) cpu.r.A = segRetA;
                            if (preserveZ80RegCompat) cpu.r.setDE(newSegCandidate);
                            if (preserveZ80RegCompat) cpu.r.setHL(newSP);
                            cpu.r.SP = newSP;
                            cpu.r.setBC(newBC);
                            commit = true;
                            opcodeEmulatedCount[0xAD]++; lastNativeOpcode=0xAD;
                        }
                        break;
                    }
                    case OP_CEQU: { // CEQU
#include "NativeRealc.inc"
#include "NativePowrc.inc"
                        uint8_t typeCode = mem[cpu.r.BC()];
                        if (typeCode == 6) { // BOOLC
                            cpu.r.setBC((uint16_t)(cpu.r.BC() + 1));
                            mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8; // SAVIPC
                            uint16_t b = PopStackWord();
                            uint16_t a = PopStackWord();
                            bool equal = ((a & 1) == (b & 1));
                            PushStackWord(equal ? 1 : 0);
                            if (preserveZ80RegCompat) cpu.r.A = (uint8_t)(a & 1);
                            if (preserveZ80RegCompat) cpu.r.setDE((uint16_t)((b & 0xFF00) | (b & 1)));
                            if (preserveZ80RegCompat) cpu.r.setHL(equal ? 1 : 0);
                            commit = true;
                            opcodeEmulatedCount[0xAF]++; lastNativeOpcode=0xAF;
                        } else if (typeCode == 10 || typeCode == 12) { // BYTEC/WORDC
                            cpu.r.setBC((uint16_t)(cpu.r.BC() + 1));
                            mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8; // SAVIPC
                            uint16_t count = DecodeGBDE();
                            mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8; // SAVIPC again past the size operand, as the Z80 BYTEC/WORDC compare does (found by Verify P-System)
                            uint16_t byteCount = (typeCode == 12) ? (uint16_t)(count * 2) : count;
                            uint16_t bPtr = PopStackWord();
                            uint16_t aPtr = PopStackWord();
                            bool equal = true;
                            uint16_t deFinal = aPtr;
                            uint8_t aRegFinal = 0;
                            for (uint16_t idx = 0; idx < byteCount; idx++) {
                                uint8_t ac = mem[(uint16_t)(aPtr + idx)];
                                uint8_t bc = mem[(uint16_t)(bPtr + idx)];
                                deFinal = (uint16_t)(aPtr + idx);
                                aRegFinal = ac;
                                if (ac != bc) { equal = false; break; }
                            }
                            PushStackWord(equal ? 1 : 0);
                            if (preserveZ80RegCompat) cpu.r.A = aRegFinal;
                            if (preserveZ80RegCompat) cpu.r.setDE(deFinal);
                            if (preserveZ80RegCompat) cpu.r.setHL(equal ? 1 : 0);
                            commit = true;
                            opcodeEmulatedCount[0xAF]++; lastNativeOpcode=0xAF;
                        } else if (typeCode == 4) { // STRGC
                            cpu.r.setBC((uint16_t)(cpu.r.BC() + 1));
                            mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8; // SAVIPC
                            uint16_t bPtr = PopStackWord();
                            uint16_t aPtr = PopStackWord();
                            bool aDisguised = (aPtr >> 8) == 0;
                            bool bDisguised = (bPtr >> 8) == 0;
                            uint8_t aLen = aDisguised ? 1 : mem[aPtr];
                            uint8_t bLen = bDisguised ? 1 : mem[bPtr];
                            uint16_t aData = (uint16_t)(aPtr + 1);
                            uint16_t bData = (uint16_t)(bPtr + 1);
                            uint16_t aBasePtr = aDisguised ? 0x024E : aPtr;
                            uint8_t minLen = (aLen < bLen) ? aLen : bLen;
                            bool foundMismatch = false;
                            uint16_t deFinal = aBasePtr;
                            uint8_t aRegFinal = 0;
                            for (uint8_t idx = 0; idx < minLen; idx++) {
                                uint8_t ac = aDisguised ? (aPtr & 0xFF) : mem[(uint16_t)(aData + idx)];
                                uint8_t bc = bDisguised ? (bPtr & 0xFF) : mem[(uint16_t)(bData + idx)];
                                deFinal = (uint16_t)(aBasePtr + idx + 1);
                                if (ac != bc) { foundMismatch = true; aRegFinal = ac; break; }
                            }
                            bool equal;
                            if (foundMismatch) {
                                equal = false;
                                if (preserveZ80RegCompat) cpu.r.A = aRegFinal;
                            } else {
                                equal = (aLen == bLen);
                                if (preserveZ80RegCompat) cpu.r.A = aLen;
                            }
                            PushStackWord(equal ? 1 : 0);
                            if (preserveZ80RegCompat) cpu.r.setDE(deFinal);
                            if (preserveZ80RegCompat) cpu.r.setHL(equal ? 1 : 0);
                            commit = true;
                            opcodeEmulatedCount[0xAF]++; lastNativeOpcode=0xAF;
                        }
                        break;
                    }
                    case OP_CNEQ: { // CNEQ
#include "NativeRealc.inc"
#include "NativePowrc.inc"
                        uint8_t typeCode = mem[cpu.r.BC()];
                        if (typeCode == 6) { // BOOLC
                            cpu.r.setBC((uint16_t)(cpu.r.BC() + 1));
                            mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8;
                            uint16_t b = PopStackWord();
                            uint16_t a = PopStackWord();
                            bool equal = ((a & 1) == (b & 1));
                            PushStackWord(equal ? 0 : 1);
                            if (preserveZ80RegCompat) cpu.r.A = (uint8_t)(a & 1);
                            if (preserveZ80RegCompat) cpu.r.setDE((uint16_t)((b & 0xFF00) | (b & 1)));
                            if (preserveZ80RegCompat) cpu.r.setHL(equal ? 0 : 1);
                            commit = true;
                            opcodeEmulatedCount[0xB7]++; lastNativeOpcode=0xB7;
                        } else if (typeCode == 10 || typeCode == 12) { // BYTEC/WORDC
                            cpu.r.setBC((uint16_t)(cpu.r.BC() + 1));
                            mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8;
                            uint16_t count = DecodeGBDE();
                            mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8; // SAVIPC again past the size operand, as the Z80 BYTEC/WORDC compare does (found by Verify P-System)
                            uint16_t byteCount = (typeCode == 12) ? (uint16_t)(count * 2) : count;
                            uint16_t bPtr = PopStackWord();
                            uint16_t aPtr = PopStackWord();
                            bool equal = true;
                            uint16_t deFinal = aPtr;
                            uint8_t aRegFinal = 0;
                            for (uint16_t idx = 0; idx < byteCount; idx++) {
                                uint8_t ac = mem[(uint16_t)(aPtr + idx)];
                                uint8_t bc = mem[(uint16_t)(bPtr + idx)];
                                deFinal = (uint16_t)(aPtr + idx);
                                aRegFinal = ac;
                                if (ac != bc) { equal = false; break; }
                            }
                            PushStackWord(equal ? 0 : 1);
                            if (preserveZ80RegCompat) cpu.r.A = aRegFinal;
                            if (preserveZ80RegCompat) cpu.r.setDE(deFinal);
                            if (preserveZ80RegCompat) cpu.r.setHL(equal ? 0 : 1);
                            commit = true;
                            opcodeEmulatedCount[0xB7]++; lastNativeOpcode=0xB7;
                        } else if (typeCode == 4) { // STRGC
                            cpu.r.setBC((uint16_t)(cpu.r.BC() + 1));
                            mem[0x0246] = cpu.r.BC() & 0xFF; mem[0x0247] = cpu.r.BC() >> 8;
                            uint16_t bPtr = PopStackWord();
                            uint16_t aPtr = PopStackWord();
                            bool aDisguised = (aPtr >> 8) == 0;
                            bool bDisguised = (bPtr >> 8) == 0;
                            uint8_t aLen = aDisguised ? 1 : mem[aPtr];
                            uint8_t bLen = bDisguised ? 1 : mem[bPtr];
                            uint16_t aData = (uint16_t)(aPtr + 1);
                            uint16_t bData = (uint16_t)(bPtr + 1);
                            uint16_t aBasePtr = aDisguised ? 0x024E : aPtr;
                            uint8_t minLen = (aLen < bLen) ? aLen : bLen;
                            bool foundMismatch = false;
                            uint16_t deFinal = aBasePtr;
                            uint8_t aRegFinal = 0;
                            for (uint8_t idx = 0; idx < minLen; idx++) {
                                uint8_t ac = aDisguised ? (aPtr & 0xFF) : mem[(uint16_t)(aData + idx)];
                                uint8_t bc = bDisguised ? (bPtr & 0xFF) : mem[(uint16_t)(bData + idx)];
                                deFinal = (uint16_t)(aBasePtr + idx + 1);
                                if (ac != bc) { foundMismatch = true; aRegFinal = ac; break; }
                            }
                            bool equal;
                            if (foundMismatch) {
                                equal = false;
                                if (preserveZ80RegCompat) cpu.r.A = aRegFinal;
                            } else {
                                equal = (aLen == bLen);
                                if (preserveZ80RegCompat) cpu.r.A = aLen;
                            }
                            PushStackWord(equal ? 0 : 1);
                            if (preserveZ80RegCompat) cpu.r.setDE(deFinal);
                            if (preserveZ80RegCompat) cpu.r.setHL(equal ? 0 : 1);
                            commit = true;
                            opcodeEmulatedCount[0xB7]++; lastNativeOpcode=0xB7;
                        }
                        break;
                    }
                    case OP_CGTR: { // CGTR
#include "NativeRealc.inc"
                        bool carry, zero;
                        if (!CmpSetupOrdering(carry, zero)) break;
                        bool result = !carry && !zero;
                        PushStackWord(result ? 1 : 0);
                        if (preserveZ80RegCompat) cpu.r.setHL(result ? 1 : 0);
                        cpu.r.PC = 0x03A4;
                        opcodeEmulatedCount[0xB1]++; lastNativeOpcode=0xB1;
                        break;
                    }
                    case OP_CLEQ: { // CLEQ
#include "NativeRealc.inc"
#include "NativePowrc.inc"
                        bool carry, zero;
                        if (!CmpSetupOrdering(carry, zero)) break;
                        bool result = carry || zero;
                        PushStackWord(result ? 1 : 0);
                        if (preserveZ80RegCompat) cpu.r.setHL(result ? 1 : 0);
                        cpu.r.PC = 0x03A4;
                        opcodeEmulatedCount[0xB4]++; lastNativeOpcode=0xB4;
                        break;
                    }
                    case OP_CLSS: { // CLSS
#include "NativeRealc.inc"
                        bool carry, zero;
                        if (!CmpSetupOrdering(carry, zero)) break;
                        bool result = carry;
                        PushStackWord(result ? 1 : 0);
                        if (preserveZ80RegCompat) cpu.r.setHL(result ? 1 : 0);
                        cpu.r.PC = 0x03A4;
                        opcodeEmulatedCount[0xB5]++; lastNativeOpcode=0xB5;
                        break;
                    }
                    case OP_CGEQ: { // CGEQ
#include "NativeRealc.inc"
#include "NativePowrc.inc"
                        bool carry, zero;
                        if (!CmpSetupOrdering(carry, zero)) break;
                        bool result = !carry;
                        PushStackWord(result ? 1 : 0);
                        if (preserveZ80RegCompat) cpu.r.setHL(result ? 1 : 0);
                        cpu.r.PC = 0x03A4;
                        opcodeEmulatedCount[0xB0]++; lastNativeOpcode=0xB0;
                        break;
                    }
                    case OP_LDO: { // LDO
                        uint16_t offset = DecodeGBDE();
                        uint16_t based0 = (uint16_t)(mem[0x0244] | (mem[0x0245] << 8));
                        uint16_t addr = (uint16_t)(based0 + offset * 2);
                        uint16_t value = (uint16_t)(mem[addr] | (mem[(uint16_t)(addr + 1)] << 8));
                        PushStackWord(value);
                        if (preserveZ80RegCompat) { cpu.r.setDE(value); cpu.r.setHL((uint16_t)(addr + 1)); }
                        commit = true;
                        opcodeEmulatedCount[0xA9]++; lastNativeOpcode=0xA9;
                        break;
                    }
                    case OP_SRO: { // SRO
                        uint16_t offset = DecodeGBDE();
                        uint16_t based0 = (uint16_t)(mem[0x0244] | (mem[0x0245] << 8));
                        uint16_t addr = (uint16_t)(based0 + offset * 2);
                        uint16_t value = PopStackWord();
                        mem[addr] = value & 0xFF;
                        mem[(uint16_t)(addr + 1)] = value >> 8;
                        if (preserveZ80RegCompat) { cpu.r.setDE(value); cpu.r.setHL((uint16_t)(addr + 1)); }
                        commit = true;
                        opcodeEmulatedCount[0xAB]++; lastNativeOpcode=0xAB;
                        break;
                    }
                    case OP_FJP: { // FJP
                        uint16_t boolValue = PeekStackWord(0);
                        uint8_t ofs = mem[cpu.r.BC()];
                        if ((boolValue & 1) != 0) {
                            PopStackWord();
                            cpu.r.setBC((uint16_t)(cpu.r.BC() + 1));
                            if (preserveZ80RegCompat) cpu.r.A = (uint8_t)(boolValue >> 8);
                            commit = true;
                            opcodeEmulatedCount[0xA1]++; lastNativeOpcode=0xA1;
                        } else if ((ofs & 0x80) == 0) {
                            PopStackWord();
                            uint16_t newBC = (uint16_t)(cpu.r.BC() + 1 + ofs);
                            cpu.r.setBC(newBC);
                            if (preserveZ80RegCompat) cpu.r.A = (uint8_t)(newBC >> 8);
                            commit = true;
                            opcodeEmulatedCount[0xA1]++; lastNativeOpcode=0xA1;
                        } else {
                            // Long jump through the procedure's jump table (offset >= 0x80): the entry
                            // at JTAB + (offset - 256) holds a self-relative pointer to the target
                            // (Z80 UJP $10: LD HL,(JTAB); BC := FFxx; ADD HL,BC; SELREL).
                            PopStackWord();                                     // the false boolean
                            uint16_t jtab = (uint16_t)(mem[0x02F4] | (mem[0x02F5] << 8));
                            uint16_t entry = (uint16_t)(jtab + ofs - 256);
                            uint16_t rel = (uint16_t)(mem[entry] | (mem[(uint16_t)(entry + 1)] << 8));
                            uint16_t target = (uint16_t)(entry - rel);
                            cpu.r.setBC(target);
                            if (preserveZ80RegCompat) { cpu.r.A = ofs; cpu.r.setDE(rel); cpu.r.setHL(target); }
                            commit = true;
                            opcodeEmulatedCount[0xA1]++; lastNativeOpcode=0xA1;
                        }
                        break;
                    }
#include "NativeOps.inc"
                    default: break;
                }
            }
            if (commit) cpu.r.PC = 0x03B0;
            if (useNativeOps && cpu.r.PC == 0x03AB) { // SLDC, all 128 values
                uint8_t doubled = (uint8_t)(mem[(uint16_t)(cpu.r.BC() - 1)] << 1); // recomputed fresh -- BC always points past the opcode byte, regardless of whether the shortcut fired for THIS dispatch
                uint8_t original = (uint8_t)(doubled >> 1);
                PushStackWord(original);
                if (preserveZ80RegCompat) { cpu.r.A = original; cpu.r.setHL(original); }
                cpu.r.PC = 0x03B0;
                opcodeEmulatedCount[original]++; lastNativeOpcode=original;
            }
            if (useNativeOps && cpu.r.PC == 0x047C) { // SLDL, all 16 values
                uint8_t doubled = (uint8_t)(mem[(uint16_t)(cpu.r.BC() - 1)] << 1); // recomputed fresh -- BC always points past the opcode byte, regardless of whether the shortcut fired for THIS dispatch
                uint8_t displacement = (uint8_t)(doubled + 0x52);
                uint16_t mpd0 = (uint16_t)(mem[0x0242] | (mem[0x0243] << 8));
                uint16_t addr = (uint16_t)(mpd0 + displacement);
                uint16_t value = (uint16_t)(mem[addr] | (mem[(uint16_t)(addr + 1)] << 8));
                PushStackWord(value);
                if (preserveZ80RegCompat) { cpu.r.A = displacement; cpu.r.setDE(value); cpu.r.setHL((uint16_t)(addr + 1)); }
                cpu.r.PC = 0x03B0;
                uint8_t opcodeByte = (uint8_t)((doubled >> 1) | 0x80);
                opcodeEmulatedCount[opcodeByte]++; lastNativeOpcode=opcodeByte;
            }
            if (useNativeOps && cpu.r.PC == 0x04B6) { // SLDO, all 16 values
                uint8_t doubled = (uint8_t)(mem[(uint16_t)(cpu.r.BC() - 1)] << 1); // recomputed fresh -- BC always points past the opcode byte, regardless of whether the shortcut fired for THIS dispatch
                uint8_t displacement = (uint8_t)(doubled + 0x32);
                uint16_t based0 = (uint16_t)(mem[0x0244] | (mem[0x0245] << 8));
                uint16_t addr = (uint16_t)(based0 + displacement);
                uint16_t value = (uint16_t)(mem[addr] | (mem[(uint16_t)(addr + 1)] << 8));
                PushStackWord(value);
                if (preserveZ80RegCompat) { cpu.r.A = displacement; cpu.r.setDE(value); cpu.r.setHL((uint16_t)(addr + 1)); }
                cpu.r.PC = 0x03B0;
                uint8_t opcodeByte = (uint8_t)((doubled >> 1) | 0x80);
                opcodeEmulatedCount[opcodeByte]++; lastNativeOpcode=opcodeByte;
            }
            if (useNativeOps && cpu.r.PC == 0x053C) { // SIND, 7 values (opcodes 0xF9-0xFF)
                uint8_t doubled = (uint8_t)(mem[(uint16_t)(cpu.r.BC() - 1)] << 1); // recomputed fresh -- BC always points past the opcode byte, regardless of whether the shortcut fired for THIS dispatch
                uint8_t displacement = (uint8_t)(doubled + 0x10);
                uint16_t base = PopStackWord();
                uint16_t addr = (uint16_t)(base + displacement);
                uint16_t value = (uint16_t)(mem[addr] | (mem[(uint16_t)(addr + 1)] << 8));
                PushStackWord(value);
                if (preserveZ80RegCompat) { cpu.r.A = displacement; cpu.r.setDE(value); cpu.r.setHL((uint16_t)(addr + 1)); }
                cpu.r.PC = 0x03B0;
                uint8_t opcodeByte = (uint8_t)((doubled >> 1) | 0x80);
                opcodeEmulatedCount[opcodeByte]++; lastNativeOpcode=opcodeByte;
            }


            // Capture #2: only meaningful for the native run -- if the
            // dispatch shortcut, ECHO, or some native opcode above landed
            // back on 0x03B0 within THIS SAME iteration, that's a
            // legitimate second "about to fetch" event. Gated behind
            // useNativeOps: for the baseline run nothing above ever
            // touches PC, so this would just re-capture capture #1's
            // exact same state, double-counting every baseline visit.
            // native case just committed: back to the top (capture + native BACK dispatch)
            if (useNativeOps && (cpu.r.PC == 0x03B0 || cpu.r.PC == 0x03A4 ||
                (cpu.r.PC != nativeEntryPc && (NativeOpcodeForTarget(cpu.r.PC) >= 0 || NativeErrorCode(cpu.r.PC) >= 0)))) continue; // + native hand-over

            cpu.step();
        }
    }
};

int main(int argc, char** argv) {
    std::string dataDir = (argc > 1) ? argv[1] : "data";
    long limit = (argc > 2) ? atol(argv[2]) : 3000000;
    std::string trustedLogPath = (argc > 3) ? argv[3] : "";

    Engine a; a.useNativeOps = false; a.maxInstructions = limit;
    Engine b; b.useNativeOps = true;  b.maxInstructions = limit;

    if (!a.LoadFiles(dataDir+"/pascal.bin", dataDir+"/Big_Disk.BLK", dataDir+"/Empty_Big_Disk.BLK")) return 1;
    if (!b.LoadFiles(dataDir+"/pascal.bin", dataDir+"/Big_Disk.BLK", dataDir+"/Empty_Big_Disk.BLK")) return 1;

    printf("Running WITHOUT native ops (baseline, pure Z80)...\n");
    a.RunLoop();
    printf("  halted=%d  BACK visits=%zu\n", a.cpu.r.halted, a.backLog.size());

    printf("Running WITH native ADI/NGI/SBI...\n");
    b.RunLoop();
    printf("  halted=%d  BACK visits=%zu\n", b.cpu.r.halted, b.backLog.size());

    printf("\n--- console output (baseline run) ---\n%s\n--- end ---\n", a.consoleText.c_str());

    // Write the "trusted boot log" -- the baseline (pure, unmodified Z80)
    // run's own P-machine register state at every single instruction
    // fetch. This is the reference that matters going forward: once
    // p-code opcode simulation is fully replaced by native C, there will
    // be no Z80 registers left to compare against live, and BC/DE/HL/A
    // stop meaning anything -- but this same log, generated now while a
    // real Z80 baseline still exists to generate it from, keeps working
    // as the trusted reference for a fully-native run's own P-machine
    // register log to be diffed against afterward.
    if (!trustedLogPath.empty()) {
        FILE* f = fopen(trustedLogPath.c_str(), "w");
        if (!f) {
            printf("ERROR: could not open '%s' for writing trusted log\n", trustedLogPath.c_str());
        } else {
            fprintf(f, "# Trusted boot log -- baseline (pure Z80) P-machine register state at every opcode fetch.\n");
            fprintf(f, "# visit opcode ipc(bc) sp de hl a top0 top1 np mpd0 based0 ipcsav mp base jtab segp\n");
            for (size_t i = 0; i < a.backLog.size(); i++) {
                const BackRecord& r = a.backLog[i];
                fprintf(f, "%zu %02X %04X %04X %04X %04X %02X %04X %04X %04X %04X %04X %04X %04X %04X %04X %04X\n",
                        i, r.opcodeAtFetch, r.bc, r.sp, r.de, r.hl, r.a, r.top0, r.top1,
                        r.np, r.mpd0, r.based0, r.ipcsav, r.mp, r.base, r.jtab, r.segp);
            }
            fclose(f);
            printf("\nWrote trusted boot log: %zu entries to %s\n", a.backLog.size(), trustedLogPath.c_str());
        }
    }

    bool mismatch = false;
    size_t n = std::min(a.backLog.size(), b.backLog.size());
    for (size_t i = 0; i < n; i++) {
        if (!(a.backLog[i] == b.backLog[i])) {
            const BackRecord& x = a.backLog[i];
            const BackRecord& y = b.backLog[i];
            printf("MISMATCH at BACK visit #%zu: (last native op that fired: 0x%02X) opcodeAtFetch baseline=0x%02X native=0x%02X\n", i, y.lastNativeOp, x.opcodeAtFetch, y.opcodeAtFetch);
            printf("  Z80 regs   baseline BC=%04X DE=%04X HL=%04X SP=%04X top0=%04X top1=%04X A=%02X\n",
                   x.bc, x.de, x.hl, x.sp, x.top0, x.top1, x.a);
            printf("  Z80 regs   native   BC=%04X DE=%04X HL=%04X SP=%04X top0=%04X top1=%04X A=%02X\n",
                   y.bc, y.de, y.hl, y.sp, y.top0, y.top1, y.a);
            printf("  P-machine  baseline NP=%04X MPD0=%04X BASED0=%04X IPCSAV=%04X MP=%04X BASE=%04X JTAB=%04X SEGP=%04X\n",
                   x.np, x.mpd0, x.based0, x.ipcsav, x.mp, x.base, x.jtab, x.segp);
            printf("  P-machine  native   NP=%04X MPD0=%04X BASED0=%04X IPCSAV=%04X MP=%04X BASE=%04X JTAB=%04X SEGP=%04X\n",
                   y.np, y.mpd0, y.based0, y.ipcsav, y.mp, y.base, y.jtab, y.segp);
            mismatch = true;
            break;
        }
    }
    if (!mismatch && a.backLog.size() != b.backLog.size())
        printf("Sequences matched up to the shorter length (%zu), but lengths differ: baseline=%zu native=%zu\n",
               n, a.backLog.size(), b.backLog.size());
    if (!mismatch && a.backLog.size() == b.backLog.size())
        printf("\nTRACE-DIFF PASSED: all %zu BACK-loop snapshots identical between baseline and native-op runs.\n", n);

    printf("\nOpcode fetch histogram (top 15 by frequency, baseline run):\n");
    std::vector<std::pair<int,uint64_t>> hist;
    for (int op = 0; op < 256; op++) if (a.opcodeFetchCount[op]) hist.push_back({op, a.opcodeFetchCount[op]});
    std::sort(hist.begin(), hist.end(), [](auto& x, auto& y){ return x.second > y.second; });
    for (size_t i = 0; i < hist.size() && i < 15; i++)
        printf("  opcode 0x%02X: %llu times\n", hist[i].first, (unsigned long long)hist[i].second);

    printf("\nNative-emulated counts (run WITH native ops):\n");
    printf("  ADI (0x82): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x82]);
    printf("  NGI (0x91): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x91]);
    printf("  SBI (0x95): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x95]);
    printf("  ABI (0x80): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x80]);
    printf("  NOT (0x93): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x93]);
    printf("  EQUI(0xC3): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xC3]);
    printf("  NEQI(0xCB): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xCB]);
    printf("  GEQI(0xC4): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xC4]);
    printf("  GTRI(0xC5): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xC5]);
    printf("  LEQI(0xC8): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xC8]);
    printf("  LESI(0xC9): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xC9]);
    printf("  FJP (0xA1): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xA1]);
    printf("  LDO (0xA9): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xA9]);
    printf("  SRO (0xAB): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xAB]);
    printf("  SLDC0(0x00): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x00]);
    printf("  SLDC1(0x01): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x01]);
    { unsigned long long sldcTotal=0, sldlTotal=0, sldoTotal=0;
      for (int i=0;i<=0x7F;i++) sldcTotal += b.opcodeEmulatedCount[i];
      for (int i=0xD8;i<=0xE7;i++) sldlTotal += b.opcodeEmulatedCount[i];
      for (int i=0xE8;i<=0xF7;i++) sldoTotal += b.opcodeEmulatedCount[i];
      printf("  SLDC (full 0x00-0x7F range) total: %llu\n", sldcTotal);
      printf("  SLDL (full 0xD8-0xE7 range) total: %llu\n", sldlTotal);
      printf("  SLDO (full 0xE8-0xF7 range) total: %llu\n", sldoTotal);
    }
    printf("  UJP (0xB9): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xB9]);
    printf("  STL (0xCC): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xCC]);
    printf("  IXA (0xA4): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xA4]);
    printf("  LDB (0xBE): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xBE]);
    printf("  LAO (0xA5): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xA5]);
    printf("  LAND(0x84): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x84]);
    printf("  INCR(0xA2): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xA2]);
    printf("  LDL (0xCA): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xCA]);
    printf("  STO (0x9A): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x9A]);
    printf("  STIND(0xA3): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xA3]);
    printf("  CSP (0x9E): %llu\n", (unsigned long long)b.opcodeEmulatedCount[OP_CSP]);
    printf("  LLA (0xC6): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xC6]);
    printf("  SIND0(0xF8): %llu\n", (unsigned long long)b.opcodeEmulatedCount[OP_SIND0]);
    printf("  LOD (0xB6): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xB6]);
    printf("  LDCN(0x9F): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x9F]);
    printf("  RNP (0xAD): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xAD]);
    printf("  CEQU(0xAF): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xAF]);
    printf("  LOR (0x8D): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x8D]);
    printf("  LDA (0xB2): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xB2]);
    printf("  RBP (0xC1): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xC1]);
    printf("  STB (0xBF): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xBF]);
    printf("  LDCI(0xC7): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xC7]);
    { unsigned long long sindTotal=0;
      for (int i=0xF9;i<=0xFF;i++) sindTotal += b.opcodeEmulatedCount[i];
      printf("  SIND (full 0xF9-0xFF range) total: %llu\n", sindTotal);
    }
    printf("  ECHO direct-call intercept fires: %llu\n", (unsigned long long)b.echoDirectCallCount);
    printf("  LSA (0xA6): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xA6]);
    printf("  SAS (0xAA): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xAA]);
    printf("  LDP (0xBA): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xBA]);
    printf("  CLP (0xCE): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xCE]);
    printf("  CBP (0xC2): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xC2]);
    printf("  LDM (0xBC): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xBC]);
    printf("  INN (0x8B): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x8B]);
    printf("  CXP (0xCD): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xCD]);
    printf("  XJP (0xAC): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xAC]);
    printf("  CIP (0xAE): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xAE]);
    printf("  UNI (0x9C): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x9C]);
    printf("  DIF (0x85): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x85]);
    printf("  SRS (0x94): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x94]);
    printf("  IXP (0xC0): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xC0]);
    printf("  SGS (0x97): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x97]);
    printf("  CNEQ(0xB7): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xB7]);
    printf("  LPA (0xD0): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xD0]);
    printf("  ADJ (0xA0): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xA0]);
    printf("  LDC (0xB3): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xB3]);
    printf("  MODI(0x8E): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x8E]);
    printf("  DVI (0x86): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x86]);
    printf("  MPI (0x8F): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0x8F]);
    printf("  STM (0xBD): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xBD]);
    printf("  MOV (0xA8): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xA8]);
    printf("  STR (0xB8): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xB8]);
    printf("  STP (0xBB): %llu\n", (unsigned long long)b.opcodeEmulatedCount[0xBB]);

    return mismatch ? 1 : 0;
}
