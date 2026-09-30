// PCodeOpcodes.h
//
// Static, human-readable metadata for every possible p-code opcode byte
// value (0x00-0xFF), used by the View > Op Code Count dialog.
//
// Names for 0x80-0xFF come directly from SYSTEM.MICRO's XFRTBL jump
// table (transfer table at 0x0100, one entry per opcode, entry N =
// opcode 0x80+N -- see PSystemEngine::RunLoop for how this table is
// walked at runtime). Opcodes 0x00-0x7F never appear in that table --
// they're all SLDC (short load constant): the low 7 bits of the opcode
// byte ARE the constant being pushed, decoded by the SLDCI routine at
// 0x03AB rather than through the table. The SLDL/SLDO/SIND ranges within
// 0x80-0xFF work the same way (a small immediate packed into the low
// bits of several consecutive opcode values), which is why those names
// repeat across a run of opcodes instead of being unique.
//
// PCodeOpcodeIsNative() is the single source of truth for which opcodes
// currently have a native-C replacement in PSystemEngine::RunLoop().
// IMPORTANT: this list is maintained BY HAND and must be kept in sync
// with the `switch (m_cpu.r.PC)` block there -- add an opcode here at
// the same time you add its case in RunLoop(), not before or after.
#pragma once
#include <cstdint>

// P-Code Opcode Constants
#define OP_ABI    0x80
#define OP_ABR    0x81
#define OP_ADI    0x82
#define OP_ADR    0x83
#define OP_LAND   0x84
#define OP_DIF    0x85
#define OP_DVI    0x86
#define OP_DVR    0x87
#define OP_CHK    0x88
#define OP_FLO    0x89
#define OP_FLT    0x8A
#define OP_INN    0x8B
#define OP_INT    0x8C
#define OP_LOR    0x8D
#define OP_MODI   0x8E
#define OP_MPI    0x8F
#define OP_MPR    0x90
#define OP_NGI    0x91
#define OP_NGR    0x92
#define OP_NOT    0x93
#define OP_SRS    0x94
#define OP_SBI    0x95
#define OP_SBR    0x96
#define OP_SGS    0x97
#define OP_SQI    0x98
#define OP_SQR    0x99
#define OP_STO    0x9A
#define OP_IXS    0x9B
#define OP_UNI    0x9C
#define OP_NOTIMP 0x9D
#define OP_CSP    0x9E
#define OP_LDCN   0x9F
#define OP_ADJ    0xA0
#define OP_FJP    0xA1
#define OP_INCR   0xA2
#define OP_STIND  0xA3
#define OP_IXA    0xA4
#define OP_LAO    0xA5
#define OP_LSA    0xA6
#define OP_NOTIMP_A 0xA7
#define OP_MOV    0xA8
#define OP_LDO    0xA9
#define OP_SAS    0xAA
#define OP_SRO    0xAB
#define OP_XJP    0xAC
#define OP_RNP    0xAD
#define OP_CIP    0xAE
#define OP_CEQU   0xAF
#define OP_CGEQ   0xB0
#define OP_CGTR   0xB1
#define OP_LDA    0xB2
#define OP_LDC    0xB3
#define OP_CLEQ   0xB4
#define OP_CLSS   0xB5
#define OP_LOD    0xB6
#define OP_CNEQ   0xB7
#define OP_STR    0xB8
#define OP_UJP    0xB9
#define OP_LDP    0xBA
#define OP_STP    0xBB
#define OP_LDM    0xBC
#define OP_STM    0xBD
#define OP_LDB    0xBE
#define OP_STB    0xBF
#define OP_IXP    0xC0
#define OP_RBP    0xC1
#define OP_CBP    0xC2
#define OP_EQUI   0xC3
#define OP_GEQI   0xC4
#define OP_GTRI   0xC5
#define OP_LLA    0xC6
#define OP_LDCI   0xC7
#define OP_LEQI   0xC8
#define OP_LESI   0xC9
#define OP_LDL    0xCA
#define OP_NEQI   0xCB
#define OP_STL    0xCC
#define OP_CXP    0xCD
#define OP_CLP    0xCE
#define OP_CGP    0xCF
#define OP_LPA    0xD0
#define OP_NOTIMP_C 0xD1
#define OP_NOTIMP_D 0xD2
#define OP_EFJ    0xD3
#define OP_NFJ    0xD4
#define OP_BPT    0xD5
#define OP_ABORT  0xD6
#define OP_BACK   0xD7

// SIND0 (short static index and load word, index=0) is a single, distinct
// opcode -- not part of the SIND_BASE range below, which covers indices
// 1-7 (0xF9-0xFF) only.
#define OP_SIND0  0xF8

// Parameterized ranges
#define OP_SLDL_BASE 0xD8
#define OP_SLDO_BASE 0xE8
#define OP_SIND_BASE 0xF9

inline const char* PCodeOpcodeName(uint8_t opcode) {

    static const char* const names[256] = {
        // 0x00-0x7F: SLDC (short load constant; value is opcode & 0x7F)
        "SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC",
        "SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC",
        "SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC",
        "SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC",
        "SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC",
        "SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC",
        "SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC",
        "SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC",
        "SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC",
        "SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC",
        "SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC",
        "SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC",
        "SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC",
        "SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC",
        "SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC",
        "SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC","SLDC",
        // 0x80-0x9F
        "ABI","ABR","ADI","ADR","LAND","DIF","DVI","DVR",
        "CHK","FLO","FLT","INN","INT","LOR","MODI","MPI",
        "MPR","NGI","NGR","NOT","SRS","SBI","SBR","SGS",
        "SQI","SQR","STO","IXS","UNI","NOTIMP","CSP","LDCN",
        // 0xA0-0xBF
        "ADJ","FJP","INCR","STIND","IXA","LAO","LSA","NOTIMP",
        "MOV","LDO","SAS","SRO","XJP","RNP","CIP","CEQU",
        "CGEQ","CGTR","LDA","LDC","CLEQ","CLSS","LOD","CNEQ",
        "STR","UJP","LDP","STP","LDM","STM","LDB","STB",
        // 0xC0-0xDF
        "IXP","RBP","CBP","EQUI","GEQI","GTRI","LLA","LDCI",
        "LEQI","LESI","LDL","NEQI","STL","CXP","CLP","CGP",
        "LPA","NOTIMP","NOTIMP","EFJ","NFJ","BPT","ABORT","BACK",
        "SLDL","SLDL","SLDL","SLDL","SLDL","SLDL","SLDL","SLDL",
        // 0xE0-0xFF
        "SLDL","SLDL","SLDL","SLDL","SLDL","SLDL","SLDL","SLDL",
        "SLDO","SLDO","SLDO","SLDO","SLDO","SLDO","SLDO","SLDO",
        "SLDO","SLDO","SLDO","SLDO","SLDO","SLDO","SLDO","SLDO",
        "SIND0","SIND","SIND","SIND","SIND","SIND","SIND","SIND",
    };
    return names[opcode];
}

// Names CSPTBL's own procedure-number entries (0x15E7, one word-pointer
// per selector), matching that table's exact order. Used by the View >
// Op Code Count dialog to break the single CSP opcode down into
// individual routines (CSP-SQT for square root, CSP-IOC for the I/O
// completion routine, etc.) instead of lumping every standard-library
// call under one count. Indices 12-20 are unused (zero entries in the
// real table -- calls with those selectors trap into the OS via
// CSPTRAP instead of jumping to a table entry) and return nullptr;
// indices beyond the table's own end (41+) also return nullptr, and the
// caller should fall back to showing the bare selector number for those.
inline const char* CspSelectorName(uint8_t sel) {
    static const char* const names[41] = {
        "IOC","NEW","MVL","MVR","EXIT","UREAD","UWRITE","IDS","TRS","TIM","FLC","SCN", // 0-11
        nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,       // 12-20
        "GSEG","RSEG","TNC","RND","SIN","COS","LOG","ATAN","LN","EXP","SQT",           // 21-31
        "MRK","RLS","IOR","UBUSY","POT","UWAIT","UCLEAR","HLT","MEMA",                 // 32-40
    };
    if (sel >= 41) return nullptr;
    return names[sel];
}

inline bool PCodeOpcodeIsNative(uint8_t opcode) {
    switch (opcode) {
        case 0x80: // ABI
        case 0x81: // ABR (native with Z80 register compatibility off)
        case 0x83: // ADR (native with Z80 register compatibility off, FP errors fall through)
        case 0x87: // DVR (native with Z80 register compatibility off, FP errors fall through)
        case 0x89: // FLO (native with Z80 register compatibility off)
        case 0x8A: // FLT (native with Z80 register compatibility off)
        case 0x8C: // INT (native with Z80 register compatibility off)
        case 0x90: // MPR (native with Z80 register compatibility off, FP errors fall through)
        case 0x92: // NGR (native with Z80 register compatibility off)
        case 0x96: // SBR (native with Z80 register compatibility off, FP errors fall through)
        case 0x98: // SQI (native with Z80 register compatibility off)
        case 0x99: // SQR (native with Z80 register compatibility off, FP errors fall through)
        case 0x9B: // IXS (native with Z80 register compatibility off, bad index falls through)
        case 0xD3: // EFJ (native with Z80 register compatibility off, long jumps fall through)
        case 0xD4: // NFJ (native with Z80 register compatibility off, long jumps fall through)
        case 0xD5: // BPT (native with Z80 register compatibility off, halts fall through)
        case 0x82: // ADI
        case 0x84: // LAND
        case 0x85: // DIF
        case 0x86: // DVI (DIVZER divide-by-zero/overflow case falls through)
        case 0x88: // CHK
        case 0x8B: // INN (in-declared-range cases only; absolute-out-of-range and set-size-exceeded fall through)
        case 0x8D: // LOR
        case 0x8E: // MODI (DIVZER divide-by-zero/overflow case falls through)
        case 0x8F: // MPI
        case 0x91: // NGI
        case 0x93: // NOT
        case 0x94: // SRS (i<0 or j>=4080 out-of-range case falls through)
        case 0x95: // SBI
        case 0x97: // SGS (i<0 or i>=4080 out-of-range case falls through)
        case 0x9A: // STO
        case 0x9C: // UNI (Uniona sub-case only; Unionb falls through)
        case 0x9E: // CSP
        case 0x9F: // LDCN
        case 0xA0: // ADJ
        case 0xA1: // FJP
        case 0xA2: // INCR
        case 0xA3: // STIND
        case 0xA4: // IXA
        case 0xA5: // LAO
        case 0xA6: // LSA
        case 0xA8: // MOV
        case 0xA9: // LDO
        case 0xAA: // SAS
        case 0xAB: // SRO
        case 0xAC: // XJP (in-range case only; else-jump case falls through)
        case 0xAD: // RNP (same-segment case only; segment-change case falls through)
        case 0xAE: // CIP (base-level and intermediate cases; defensive fallback for the never-expected "not found" case)
        case 0xAF: // CEQU (BOOLC/WORDC/BYTEC/STRGC only; REALC/POWRC fall through)
        case 0xB0: // CGEQ (BOOLC/STRGC only; other types fall through)
        case 0xB1: // CGTR (BOOLC/STRGC only; other types fall through)
        case 0xB2: // LDA
        case 0xB3: // LDC
        case 0xB4: // CLEQ (BOOLC/STRGC only; other types fall through)
        case 0xB5: // CLSS (BOOLC/STRGC only; other types fall through)
        case 0xB6: // LOD
        case 0xB7: // CNEQ (BOOLC/WORDC/BYTEC/STRGC only; REALC/POWRC fall through)
        case 0xB8: // STR
        case 0xB9: // UJP
        case 0xBA: // LDP
        case 0xBB: // STP
        case 0xBC: // LDM (count==0 edge case falls through)
        case 0xBD: // STM
        case 0xBE: // LDB
        case 0xBF: // STB
        case 0xC0: // IXP
        case 0xC1: // RBP (same-segment case only, matching RNP)
        case 0xC2: // CBP (same-segment; assembly-proc path falls through)
        case 0xCD: // CXP (same-segment fast path only; different-segment cases fall through)
        case 0xCE: // CLP (assembly-proc path falls through)
        case 0xCF: // CGP (assembly-proc path falls through)
        case 0xD0: // LPA
        case 0xD7: // BACK -- a genuine, distinct p-code opcode (not the
                    // fetch-loop label at 0x03B0, despite sharing its
                    // name): its XFRTBL entry points directly at BACK's own
                    // fetch-loop entry, so it's a true no-op -- dispatching
                    // to it just re-enters the fetch loop immediately, with
                    // no body of its own to port. Already at the minimum
                    // possible cost with zero code changes, so there's no
                    // corresponding case in the switch below.
        case 0xC3: // EQUI
        case 0xC4: // GEQI
        case 0xC5: // GTRI
        case 0xC6: // LLA
        case 0xC7: // LDCI
        case 0xC8: // LEQI
        case 0xC9: // LESI
        case 0xCA: // LDL
        case 0xCB: // NEQI
        case 0xCC: // STL
        case OP_SIND0: // SIND0
            return true;
        default:
            // SLDC: all of 0x00-0x7F.
            if (opcode <= 0x7F) return true;
            // SLDL: all of 0xD8-0xE7. SLDO: all of 0xE8-0xF7.
            if (opcode >= 0xD8 && opcode <= 0xF7) return true;
            // SIND: all of 0xF9-0xFF.
            if (opcode >= 0xF9 && opcode <= 0xFF) return true;
            return false;
    }
}

// Run-time error labels of the Z80 interpreter (RUN-TIME ERROR SUPPORT,
// 0x03BE-0x0430): the error number each one passes to XEQERR, or -1.
// Used by NativeErrors.inc, which performs XEQERR natively.
inline int NativeErrorCode(uint16_t pc) {
    switch (pc) {
        case 0x03DA: return 1;  // INVNDX  value range
        case 0x03DF: return 2;  // NOPROC  no proc in seg-table
        case 0x03E4: return 3;  // NOEXIT  exit from uncalled proc
        case 0x03E9: return 4;  // STKOVR  stack overflow
        case 0x03F4: return 5;  // INTOVR  integer overflow
        case 0x03F9: return 6;  // DIVZER  divide by zero
        case 0x03FE: return 7;  // BADMEM
        case 0x0403: return 8;  // UBREAK  user break
        case 0x0408: return 9;  // SYIOER  system I/O error
        case 0x040D: return 10; // UIOERR  user I/O error
        case 0x0412: return 11; // NOTIMP  instruction not implemented
        case 0x041B: return 12; // FPIERR  floating point error
        case 0x0420: return 13; // S2LONG  string overflow
        case 0x0425: return 14; // HLT     halt
        case 0x042E: return 15; // BPTHLT  breakpoint / conditional halt
        default:     return -1;
    }
}

// The unit-I/O configuration the native unit I/O needs (NativeCsp.inc,
// NativeSegLoad.inc): which units exist (GETU's MAXU+1), each unit's
// capability flags and driver kind, the BIOS functions of each character
// unit, and the address of SYSCOM's end-of-file key (in the Z80
// interpreter's variable layout; use it through PM_V). Hosts provide it as
// PM_IOCFG: discovered from a Z80 interpreter image (PmIoConfigFromImage),
// or built in (PSystemEngine, when P-Code mode runs without SYSTEM.MICRO).
enum { PMIO_NONE = 0, PMIO_DISK = 1, PMIO_CHAR12 = 2, PMIO_CHAR678 = 3 };
struct PmIoConfig {
    bool ok = false;
    uint8_t maxu1 = 0;          // valid units: 1 .. maxu1-1
    uint16_t unitbl = 0;        // UNITBL's address in the Z80 image (the value UPTR holds)
    uint8_t flags[16] = {};     // UNITBL[u] capability bits (INBIT 1, OUTBIT 2, CLRBIT 4)
    uint8_t kind[16] = {};      // PMIO_*
    uint8_t civ[16] = {}, cov[16] = {};   // character units: BIOS input / output function
    uint16_t syeof = 0;         // SYSCOM's EOF key (Z80 layout)
};
// Discovery from the Z80 interpreter's image -- exactly the checks the
// native unit I/O used to make itself: UWAIT's CALL GETU; GETU's CP MAXU+1
// and LD DE,UNITBL; each unit's driver (the Big Disk driver, or a character
// driver whose shared part calls SETVECT and checks SYSCOM's EOF key).
template <class RB> inline PmIoConfig PmIoConfigFromImage(RB rb) {
    PmIoConfig c;
    auto rw = [&](uint16_t a) { return (uint16_t)(rb(a) | (rb((uint16_t)(a + 1)) << 8)); };
    const uint16_t uw = rw((uint16_t)(0x15E7 + 2 * 37));
    if (rb(uw) != 0x3E || rb((uint16_t)(uw + 5)) != 0xCD) return c;
    const uint16_t getu = rw((uint16_t)(uw + 6));
    if (rb(getu) != 0xAF || rb((uint16_t)(getu + 11)) != 0xFE || rb((uint16_t)(getu + 24)) != 0x11) return c;
    c.maxu1 = rb((uint16_t)(getu + 12));
    c.unitbl = rw((uint16_t)(getu + 25));
    if (c.maxu1 > 16) return c;
    for (int u = 1; u < c.maxu1; u++) {
        const uint16_t upt = (uint16_t)(c.unitbl + (uint8_t)(u * 4));
        c.flags[u] = rb(upt);
        const uint16_t d = rw((uint16_t)(upt + 2));
        auto at = [&](int k) { return rb((uint16_t)(d + k)); };
        if (at(0) == 0x3A && at(1) == 0xD3 && at(2) == 0x02 && at(3) == 0xD3 && at(4) == 0xC8) { c.kind[u] = PMIO_DISK; continue; }
        if (at(0) == 0x3A && at(1) == 0xD2 && at(2) == 0x02 && at(3) == 0xE6 && at(4) == 0x04 && (at(5) == 0xCA || at(5) == 0xC2)) {
            const bool c12 = at(5) == 0xCA;
            const uint16_t common = c12 ? rw((uint16_t)(d + 6)) : (uint16_t)(d + 8);
            if (rb(common) != 0xCD || rb((uint16_t)(common + 0x2C)) != 0x3A) continue;
            const uint16_t setvect = rw((uint16_t)(common + 1));
            if (rb((uint16_t)(setvect + 6)) != 0x11) continue;
            const uint16_t ctable = rw((uint16_t)(setvect + 7));
            c.kind[u] = c12 ? PMIO_CHAR12 : PMIO_CHAR678;
            c.civ[u] = rb((uint16_t)(ctable + 2 * u));
            c.cov[u] = rb((uint16_t)(ctable + 2 * u + 1));
            c.syeof = rw((uint16_t)(common + 0x2D));
        }
    }
    c.ok = true;
    return c;
}

// REAL comparisons (CEQU/CNEQ/CLEQ/CGEQ/CLSS/CGTR with type REALC = 2),
// exactly as the Z80 REALC routine (1122) sets the flags and the six
// comparison routines turn them into a result -- bytewise, not arithmetic:
//   different signs: CP of the two sign bits (b's against a's);
//   both negative:   the operands are swapped (larger magnitude = smaller);
//   then, until a byte differs: the exponents (byte 0), the sign/high
//   mantissa bytes (byte 1), then byte 2, then byte 3 -- the order REALC
//   pops and compares them. C = "first < second", Z = "equal".
// a = the first operand (deeper on the stack), b = the second (on top);
// r[k] at SP + k, as all the native real arithmetic reads them.
inline bool PmRealCompare(uint8_t op, const uint8_t a[4], const uint8_t b[4]) {
    bool c = false, z = false;
    const uint8_t sa = (uint8_t)(a[1] & 0x80), sb = (uint8_t)(b[1] & 0x80);
    if (sa != sb) { c = sb < sa; z = false; }               // LD A,B / AND 80H / CP D
    else {
        const uint8_t* x = a; const uint8_t* y = b;
        if (sa) { x = b; y = a; }                            // both negative: switch
        int k = 0;
        while (k < 3 && x[k] == y[k]) k++;                   // exp, high byte, byte 2 ...
        c = x[k] < y[k]; z = x[k] == y[k];                   // ... and finally byte 3
    }
    switch (op) {
        case OP_CEQU: return z;
        case OP_CNEQ: return !z;
        case OP_CLSS: return c;
        case OP_CGEQ: return !c;
        case OP_CGTR: return !c && !z;
        default:      return c || z;                         // OP_CLEQ
    }
}
// RETADR as the Z80 leaves it: REALC saves the return address of the
// comparison routine's CALL CSETUP.
inline uint16_t PmRealcRetadr(uint8_t op) {
    switch (op) {
        case OP_CEQU: return 0x08D8;
        case OP_CNEQ: return 0x08E5;
        case OP_CGTR: return 0x08F2;
        case OP_CLEQ: return 0x08FE;
        case OP_CLSS: return 0x090A;
        default:      return 0x0913;                         // OP_CGEQ
    }
}
