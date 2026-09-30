// UcsdReal.h
//
// Bit-exact C++ port of the UCSD Pascal II.0 Z80 floating-point package
// (FPL.TEXT / FPI.TEXT in SYSTEM.MICRO: FPLSUM, FPFMUL, FPFDIV, FPFFLOAT,
// FPLNRM/FPLNRMX, FPLRND, FPLSIGN, FPLSTOR). Header-only, no host
// dependencies, so the same code is used by the emulator and by the
// differential tester that checks it against the real Z80 routines.
//
// Real format, as the 4 bytes appear on the P-code stack (lowest address
// first): [0] exponent (bias 128, 0 means the value is zero)
//         [1] sign bit | top 7 mantissa bits (hidden leading 1 not stored)
//         [2] middle mantissa byte   [3] low mantissa byte
// Value = 0.1mmm...m (binary, 24 bits) * 2^(exp-128).
//
// Every operation returns true with the result in out[], or false when the
// Z80 package would set FPERROR (overflow, underflow -- which this package
// treats as an error, not as zero -- or division by zero). Even then out[]
// holds exactly the result the Z80 package leaves on the stack (FPLABN):
// underflow 0.0; overflow FF 7F FF FF (see Ovr). The emulator pushes it
// and raises the floating point error (FPIERR).
#pragma once
#include <cstdint>

namespace ucsdreal {
// FPLUND: A = 0 propagated through the mantissa and exponent -> 0.0.
inline bool Und(uint8_t out[4]) { out[0] = out[1] = out[2] = out[3] = 0; return false; }
// FPLOVR: A = FF propagated through the mantissa and exponent. The sign:
// FPLABN starts with POP HL (to count the error), which replaces H with the
// high byte of the word the operation saved first (PUSH AF: its stack
// cut-back, always even) -- so FPLSIGN's bit 0 of H+1 is always 1 and the
// sign bit comes out clear. The overflow result is therefore always
// FF 7F FF FF (largest positive), whatever the operands' signs; the H
// argument (the sign the result would have had) is deliberately unused.
inline bool Ovr(uint8_t /*H*/, uint8_t out[4]) {
    out[0] = 0xFF; out[1] = 0x7F; out[2] = 0xFF; out[3] = 0xFF;
    return false;
}

// FPLRND -> FPLSIGN -> FPLSTOR. B,C,D = mantissa (B has the leading 1 in
// bit 7), E = round byte, L = exponent, bit 0 of H = result sign.
inline bool Round(uint8_t B, uint8_t C, uint8_t D, uint8_t E, uint8_t L, uint8_t H, uint8_t out[4]) {
    if (E & 0x80) {                                   // round up (half up on the magnitude)
        if (++D == 0 && ++C == 0 && ++B == 0) { B = 0x80; L++; }
    }
    if (L == 0) return Ovr(H, out);                   // FPLSIGN: exponent wrapped -> FPLOVR
    uint8_t s = (uint8_t)(H + 1);                     // A := H+1; RRCA; AND 80H; XOR B
    uint8_t A = (uint8_t)(((s & 1) << 7) ^ B);
    out[0] = L; out[1] = A; out[2] = C; out[3] = D;   // FPLSTOR: result LACD
    return true;
}

// FPLNRMX: entered with the sign flag of B. Shift left one bit at a time
// (decrementing the exponent) until the leading bit is set.
inline bool NormalizeX(uint8_t B, uint8_t C, uint8_t D, uint8_t E, uint8_t L, uint8_t H, uint8_t out[4]) {
    while (!(B & 0x80)) {
        L--;
        if (L == 0) return Und(out);                  // FPLUND (an error in this package)
        B = (uint8_t)((B << 1) | (C >> 7));
        C = (uint8_t)((C << 1) | (D >> 7));
        D = (uint8_t)((D << 1) | (E >> 7));
        E = (uint8_t)(E << 1);
    }
    return Round(B, C, D, E, L, H, out);
}

// FPLNRM: entered with the zero flag of B. Byte shifts first, then FPLNRMX.
inline bool Normalize(uint8_t B, uint8_t C, uint8_t D, uint8_t E, uint8_t L, uint8_t H, uint8_t out[4]) {
    if (B != 0) return NormalizeX(B, C, D, E, L, H, out);
    if ((E | D | C) == 0) { out[0] = out[1] = out[2] = out[3] = 0; return true; } // FPLZERO
    uint8_t A = L;
    for (;;) {
        if (A < 9) return Und(out);                   // SUB 9 borrows -> FPLUND
        A = (uint8_t)(A - 9 + 1);
        B = C; C = D; D = E; E = 0;
        if (B != 0) return NormalizeX(B, C, D, E, A, H, out);
    }
}

inline void Zero(uint8_t out[4]) { out[0] = out[1] = out[2] = out[3] = 0; }

// FPFADD / FPFSUB: a1 is the deeper operand, a2 the top of stack; the
// result is a1 + a2 or a1 - a2.
inline bool AddSub(const uint8_t a1[4], const uint8_t a2[4], bool subtract, uint8_t out[4]) {
    uint8_t B = subtract ? 0x7F : 0x02;
    const uint8_t* greater = a1;
    const uint8_t* lesser = a2;          // (not "small": windows.h has "#define small char")
    uint8_t A = (uint8_t)(a1[0] - a2[0]);
    if (a1[0] < a2[0]) { greater = a2; lesser = a1; B++; A = (uint8_t)(0 - A); }
    uint8_t C = A;
    if (lesser[0] == 0) {
        if (C == 0) { Zero(out); return true; }      // both zero
        C = 25;                                       // past the precision
    }
    uint8_t E = greater[1];
    uint8_t x = (uint8_t)(E ^ B);
    uint8_t D = (uint8_t)((x << 1) | (x >> 7));       // RLCA: result sign into bit 0
    B++;
    x = (uint8_t)(E ^ B ^ lesser[1] ^ D);
    E = (uint8_t)((x & 0x80) ^ D);                    // bit 7: signs differ, bit 0: result sign
    uint8_t H = E;
    uint8_t mb = (uint8_t)(lesser[1] | 0x80), mc = lesser[2], md = lesser[3], me = 0;
    A = C;
    if (A >= 26) A = 25;
    while (A >= 8) { me = md; md = mc; mc = mb; mb = 0; A = (uint8_t)(A - 8); }
    for (uint8_t n = A; n; n--) {                     // SRL B / RR C / RR D / RR E
        me = (uint8_t)((me >> 1) | (md << 7));
        md = (uint8_t)((md >> 1) | (mc << 7));
        mc = (uint8_t)((mc >> 1) | (mb << 7));
        mb = (uint8_t)(mb >> 1);
    }
    uint8_t L = greater[0];
    uint8_t bb = (uint8_t)(greater[1] | 0x80);
    if (!(H & 0x80)) {                                // same signs: add magnitudes
        unsigned s = (unsigned)md + greater[3];
        md = (uint8_t)s;
        s = (unsigned)mc + greater[2] + (s >> 8);
        mc = (uint8_t)s;
        s = (unsigned)bb + mb + (s >> 8);
        mb = (uint8_t)s;
        if (s >> 8) {                                 // carry: shift right, carry in = 1
            me = (uint8_t)((me >> 1) | (md << 7));
            md = (uint8_t)((md >> 1) | (mc << 7));
            mc = (uint8_t)((mc >> 1) | (mb << 7));
            mb = (uint8_t)((mb >> 1) | 0x80);
            L++;
        }
        return Round(mb, mc, md, me, L, H, out);
    }
    // different signs: greater - lesser
    int t = 0 - me;                 uint8_t br = t < 0; me = (uint8_t)t;
    t = greater[3] - md - br;           br = t < 0; md = (uint8_t)t;
    t = greater[2] - mc - br;           br = t < 0; mc = (uint8_t)t;
    t = bb - mb - br;               br = t < 0; mb = (uint8_t)t;
    if (br) {                                         // subtracted larger from smaller
        H = (uint8_t)(H + 1);
        t = 0 - me;                 br = t < 0; me = (uint8_t)t;
        t = 0 - md - br;            br = t < 0; md = (uint8_t)t;
        t = 0 - mc - br;            br = t < 0; mc = (uint8_t)t;
        t = 0 - mb - br;            mb = (uint8_t)t;
    }
    return Normalize(mb, mc, md, me, L, H, out);
}

inline uint32_t Mant(const uint8_t a[4]) {
    return ((uint32_t)(a[1] | 0x80) << 16) | ((uint32_t)a[2] << 8) | a[3];
}

// FPFMUL: a1 * a2.
inline bool Mul(const uint8_t a1[4], const uint8_t a2[4], uint8_t out[4]) {
    if (a1[0] == 0 || a2[0] == 0) { Zero(out); return true; }
    uint8_t x = (uint8_t)(a1[1] ^ a2[1]);
    uint8_t H = (uint8_t)((x << 1) | (x >> 7));
    unsigned sum = (unsigned)(uint8_t)(a1[0] - 1) + a2[0];
    uint8_t A = (uint8_t)sum;
    bool carry = sum > 0xFF;
    if (A & 0x80) { if (carry) return Ovr(H, out); }  // FPLOVRX (B = the sign byte: H := B)
    else          { if (!carry) return Und(out); }    // FPLUND
    uint8_t L = (uint8_t)(A + 0x81);
    uint64_t P = (uint64_t)Mant(a1) * Mant(a2);       // exact 48-bit product
    return NormalizeX((uint8_t)(P >> 40), (uint8_t)(P >> 32), (uint8_t)(P >> 24),
                      (uint8_t)(P >> 16), L, H, out);
}

// FPFDIV: a1 / a2.
inline bool Div(const uint8_t a1[4], const uint8_t a2[4], uint8_t out[4]) {
    if (a2[0] == 0) return Ovr((uint8_t)((a1[1] << 1) | (a1[1] >> 7)), out); // divide by zero -> FPLOVRX, sign of the dividend
    if (a1[0] == 0) { Zero(out); return true; }
    uint8_t x = (uint8_t)(a1[1] ^ a2[1]);
    uint8_t H = (uint8_t)((x << 1) | (x >> 7));
    uint8_t A = (uint8_t)(a1[0] - a2[0]);
    bool borrow = a1[0] < a2[0];
    if (A & 0x80) { if (!borrow) return Ovr(H, out); } // FPLOVRX
    else          { if (borrow) return Und(out); }    // FPLUND
    uint8_t L = (uint8_t)(A + 0x81);
    uint64_t Q = ((uint64_t)Mant(a1) << 25) / Mant(a2); // 26 quotient bits (restoring division)
    return NormalizeX((uint8_t)(Q >> 18), (uint8_t)(Q >> 10), (uint8_t)(Q >> 2),
                      (uint8_t)((Q & 3) << 6), L, H, out);
}

// FPFFLOAT: 16-bit integer -> real. Never fails.
inline void Float(uint16_t v, uint8_t out[4]) {
    uint8_t B = (uint8_t)(v >> 8), C = (uint8_t)v, D = 0, E = 0, H = 0;
    if (B & 0x80) {                                   // negative: flip sign, negate BCDE
        H = 1;
        uint32_t m = ((uint32_t)B << 24) | ((uint32_t)C << 16);
        m = 0u - m;
        B = (uint8_t)(m >> 24); C = (uint8_t)(m >> 16); D = (uint8_t)(m >> 8); E = (uint8_t)m;
    }
    Normalize(B, C, D, E, 0x90, H, out);
}

// FPFFIX: real -> 16-bit integer, truncating toward zero. Uses the top 16
// mantissa bits; false when exp >= 0x90 (|x| >= 32768 -> FPERROR), with out
// = 7FFF, or 8000 for a negative value, as the Z80 routine pushes.
inline bool Fix(const uint8_t a[4], uint16_t& out) {
    uint8_t ex = a[0];
    if (ex >= 0x90) { out = (a[1] & 0x80) ? 0x8000 : 0x7FFF; return false; }
    uint16_t m = (uint16_t)(((a[1] | 0x80) << 8) | a[2]);
    uint8_t A = (uint8_t)(ex - 0x90);
    int sh = (A >= 0xF0) ? (int)(uint8_t)(0 - A) : 16;
    uint16_t mag = (sh >= 16) ? 0 : (uint16_t)(m >> sh);
    out = (a[1] & 0x80) ? (uint16_t)(0 - mag) : mag;
    return true;
}

// FPFRND: FPFADD(x, +-0.5 with the sign of x) then FPFFIX.
inline bool Rnd(const uint8_t a[4], uint16_t& out) {
    uint8_t half[4] = { 0x80, (uint8_t)(a[1] & 0x80), 0, 0 }, s[4];
    bool ok = AddSub(a, half, false, s);              // on overflow s is the error result (exp FF)...
    return Fix(s, out) && ok;                         // ...which FPFFIX then turns into 7FFF / 8000
}

} // namespace ucsdreal
