// NativeDouble.inc -- 8-byte floating point ("double") as standard
// procedures CSP 100..137. Native P-Code mode only: the Z80 interpreter has
// no such routines (its CSPTBL has 41 entries).
// Included in PSystemEngine.cpp's CSP case, before the 25..31 block.
//
// FORMAT: IEEE-754 binary64, 8 bytes = 4 words, little-endian, lowest
// address first, exactly as LDM 4 / STM 4 move it and as it lies on the
// stack from SP up (byte 0 = low mantissa byte, byte 7 = sign | high
// exponent).
// The arithmetic and the functions are the host's IEEE hardware and C
// runtime (<cmath>): about 16 significant digits, exponent 1e-308..1e308.
// No traps: division by zero gives an infinity, a domain error a NaN, as in C.
//
// ARGUMENTS are pushed in order (the first deepest); "D" is an 8-byte
// value, "F" a 4-byte UCSD real, "I" a word, "L" a long (2 words, low word
// on top, as Tiny-C stores it), "A" an address. Results replace them.
//   100 DADD  D a, D b -> D a+b        101 DSUB  D a, D b -> D a-b
//   102 DMUL  D a, D b -> D a*b        103 DDIV  D a, D b -> D a/b
//   104 DNEG  D a -> D -a
//   105 DCMP  D a, D b, I rel -> I 1 if the relation holds, else 0: rel
//             0 ==  1 !=  2 <  3 <=  4 >  5 >=  (C semantics: with a NaN
//             only != holds); rel + 8: the negation (for branches)
//   106 FTOD  F x -> D                 107 DTOF  D x -> F (saturates;
//                                           too small -> 0.0)
//   108 ITOD  I i (signed) -> D        109 DTOI  D x -> I (truncated,
//                                           saturates at -32768/32767)
//   110 LTOD  L l (signed) -> D        111 DTOL  D x -> L (truncated, saturates)
//   112 DSQRT 113 DSIN 114 DCOS 115 DTAN 116 DASIN 117 DACOS 118 DATAN
//   120 DEXP  121 DLN   122 DLOG10     124 DFLOOR 125 DCEIL 126 DFABS
//   128 DSINH 129 DCOSH 130 DTANH       (all: D x -> D f(x))
//   119 DATAN2 D y, D x -> D atan2(y,x)
//   123 DPOW   D x, D y -> D pow(x,y)   127 DFMOD D x, D y -> D fmod(x,y)
//   131 DLDEXP D x, I n -> D x*2^n
//   132 DFREXP D x, A ^int -> D m (0.5 <= |m| < 1; *A := the exponent)
//   133 DTOA   D x, I fmt, I prec, A buf -> (nothing): buf := x as text,
//              NUL-terminated, at most 40 chars; fmt is 'e', 'f' or 'g'
//              (C's %.*e / %.*f / %.*g; prec 0..17; %f of |x| >= 1e30
//              gives %e instead so the text always fits)
//   134 ATOD   A buf -> D x, I n: x := the number at the start of the
//              NUL-terminated text buf (C's strtod), n := characters used
//              (0: no number, x = 0)
//   135 ATODM  A buf, A dst -> I n: like ATOD, but the value is stored at
//              dst (8 bytes) -- for code that cannot take 4 words off the
//              stack (the compiler's constants, the library's strtold)
//   136 ULTOD  L l (unsigned) -> D     137 DTOUL D x -> L (unsigned;
//              truncated, saturates at 0 and 4294967295)
{
    const int D = 8;                                    // bytes in a double
    auto dblGet = [&](uint16_t a) -> double {           // 8 bytes at a -> double
        uint64_t u = 0;
        for (int k = 7; k >= 0; k--) u = (u << 8) | m_mem[(uint16_t)(a + k)];
        double d;
        std::memcpy(&d, &u, 8);
        return d;
    };
    auto dblPut = [&](uint16_t a, double d) {           // double -> 8 bytes at a
        uint64_t u;
        std::memcpy(&u, &d, 8);
        for (int k = 0; k < 8; k++) { m_mem[(uint16_t)(a + k)] = (uint8_t)(u & 0xFF); u >>= 8; }
    };
    auto dblWord = [&](uint16_t a) -> uint16_t { return (uint16_t)(m_mem[a] | (m_mem[(uint16_t)(a + 1)] << 8)); };
    bool dblDone = false;
    if (procNum >= 100 && procNum <= 137) {
        const uint16_t sp = m_cpu.r.SP;
        double r = 0.0;
        dblDone = true;
        switch (procNum) {
        case 105: {                                   // DCMP: D a, D b, I rel -> I 0/1
            const int rel = dblWord(sp);
            const double b = dblGet((uint16_t)(sp + 2)), a = dblGet((uint16_t)(sp + 2 + D));
            bool v;
            switch (rel & 7) {
            case 0: v = a == b; break;
            case 1: v = a != b; break;
            case 2: v = a < b; break;
            case 3: v = a <= b; break;
            case 4: v = a > b; break;
            default: v = a >= b; break;
            }
            if (rel & 8) v = !v;
            m_cpu.r.SP = (uint16_t)(sp + 2 + 2 * D);
            PushStackWord(v ? 1 : 0);
            break;
        }
        case 100: case 101: case 102: case 103: case 119: case 123: case 127: {
            const double b = dblGet(sp), a = dblGet((uint16_t)(sp + D));
            switch (procNum) {
            case 100: r = a + b; break;
            case 101: r = a - b; break;
            case 102: r = a * b; break;
            case 103: r = a / b; break;
            case 119: r = std::atan2(a, b); break;
            case 123: r = std::pow(a, b); break;
            default:  r = std::fmod(a, b); break;
            }
            m_cpu.r.SP = (uint16_t)(sp + D);
            dblPut(m_cpu.r.SP, r);
            break;
        }
        case 104: case 112: case 113: case 114: case 115: case 116: case 117: case 118:
        case 120: case 121: case 122: case 124: case 125: case 126: case 128: case 129: case 130: {
            const double x = dblGet(sp);
            switch (procNum) {
            case 104: r = -x; break;
            case 112: r = std::sqrt(x); break;
            case 113: r = std::sin(x); break;
            case 114: r = std::cos(x); break;
            case 115: r = std::tan(x); break;
            case 116: r = std::asin(x); break;
            case 117: r = std::acos(x); break;
            case 118: r = std::atan(x); break;
            case 120: r = std::exp(x); break;
            case 121: r = std::log(x); break;
            case 122: r = std::log10(x); break;
            case 124: r = std::floor(x); break;
            case 125: r = std::ceil(x); break;
            case 126: r = std::fabs(x); break;
            case 128: r = std::sinh(x); break;
            case 129: r = std::cosh(x); break;
            default:  r = std::tanh(x); break;
            }
            dblPut(sp, r);
            break;
        }
        case 106: {                                   // FTOD: 4-byte real -> D
            const double x = DecodeUcsdReal(m_mem[sp], m_mem[(uint16_t)(sp + 1)], m_mem[(uint16_t)(sp + 2)], m_mem[(uint16_t)(sp + 3)]);
            m_cpu.r.SP = (uint16_t)(sp + 4 - D);
            dblPut(m_cpu.r.SP, x);
            break;
        }
        case 107: {                                   // DTOF: D -> 4-byte real
            double x = dblGet(sp);
            uint8_t b0, b1, b2, b3;
            if (std::isnan(x)) x = 0.0;
            if (!EncodeUcsdReal(x, b0, b1, b2, b3)) {
                if (std::fabs(x) < 1.0) { b0 = b1 = b2 = b3 = 0; }        // underflow: 0.0
                else { b0 = 0xFF; b1 = (uint8_t)(x < 0 ? 0xFF : 0x7F); b2 = b3 = 0xFF; }  // overflow: the largest
            }
            m_cpu.r.SP = (uint16_t)(sp + D - 4);
            m_mem[m_cpu.r.SP] = b0; m_mem[(uint16_t)(m_cpu.r.SP + 1)] = b1;
            m_mem[(uint16_t)(m_cpu.r.SP + 2)] = b2; m_mem[(uint16_t)(m_cpu.r.SP + 3)] = b3;
            break;
        }
        case 108: {                                   // ITOD
            const double x = (double)(int16_t)dblWord(sp);
            m_cpu.r.SP = (uint16_t)(sp + 2 - D);
            dblPut(m_cpu.r.SP, x);
            break;
        }
        case 109: {                                   // DTOI
            double x = std::trunc(dblGet(sp));
            const int16_t v = std::isnan(x) ? 0 : (x >= 32767.0 ? 32767 : (x <= -32768.0 ? -32768 : (int16_t)x));
            m_cpu.r.SP = (uint16_t)(sp + D);
            PushStackWord((uint16_t)v);
            break;
        }
        case 110: {                                   // LTOD: low word on top
            const int32_t l = (int32_t)((uint32_t)dblWord(sp) | ((uint32_t)dblWord((uint16_t)(sp + 2)) << 16));
            m_cpu.r.SP = (uint16_t)(sp + 4 - D);
            dblPut(m_cpu.r.SP, (double)l);
            break;
        }
        case 111: {                                   // DTOL
            double x = std::trunc(dblGet(sp));
            const int32_t l = std::isnan(x) ? 0 : (x >= 2147483647.0 ? 2147483647 : (x <= -2147483648.0 ? INT32_MIN : (int32_t)x));
            m_cpu.r.SP = (uint16_t)(sp + D);
            PushStackWord((uint16_t)((uint32_t)l >> 16));
            PushStackWord((uint16_t)((uint32_t)l & 0xFFFF));
            break;
        }
        case 136: {                                   // ULTOD: low word on top
            const uint32_t l = (uint32_t)dblWord(sp) | ((uint32_t)dblWord((uint16_t)(sp + 2)) << 16);
            m_cpu.r.SP = (uint16_t)(sp + 4 - D);
            dblPut(m_cpu.r.SP, (double)l);
            break;
        }
        case 137: {                                   // DTOUL
            double x = std::trunc(dblGet(sp));
            const uint32_t l = std::isnan(x) || x <= 0.0 ? 0u : (x >= 4294967295.0 ? 0xFFFFFFFFu : (uint32_t)x);
            m_cpu.r.SP = (uint16_t)(sp + D);
            PushStackWord((uint16_t)(l >> 16));
            PushStackWord((uint16_t)(l & 0xFFFF));
            break;
        }
        case 131: {                                   // DLDEXP: D x, I n
            const int n = (int16_t)dblWord(sp);
            const double x = dblGet((uint16_t)(sp + 2));
            m_cpu.r.SP = (uint16_t)(sp + 2);
            dblPut(m_cpu.r.SP, std::ldexp(x, n));
            break;
        }
        case 132: {                                   // DFREXP: D x, A ^int
            const uint16_t ea = dblWord(sp);
            int e = 0;
            const double m = std::frexp(dblGet((uint16_t)(sp + 2)), &e);
            m_mem[ea] = (uint8_t)(e & 0xFF); m_mem[(uint16_t)(ea + 1)] = (uint8_t)((e >> 8) & 0xFF);
            m_cpu.r.SP = (uint16_t)(sp + 2);
            dblPut(m_cpu.r.SP, m);
            break;
        }
        case 133: {                                   // DTOA: D x, I fmt, I prec, A buf
            const uint16_t buf = dblWord(sp);
            int prec = (int16_t)dblWord((uint16_t)(sp + 2));
            int fmt = dblWord((uint16_t)(sp + 4)) & 0xFF;
            const double x = dblGet((uint16_t)(sp + 6));
            if (prec < 0) prec = 0;
            if (prec > 17) prec = 17;
            if (fmt != 'e' && fmt != 'f' && fmt != 'g') fmt = 'g';
            if (fmt == 'f' && std::fabs(x) >= 1e30) fmt = 'e';
            char f[8] = { '%', '.', '*', (char)fmt, 0 };
            char text[64];
            std::snprintf(text, sizeof text, f, prec, x);
            text[40] = 0;
            for (int k = 0; ; k++) { m_mem[(uint16_t)(buf + k)] = (uint8_t)text[k]; if (!text[k]) break; }
            m_cpu.r.SP = (uint16_t)(sp + 6 + D);
            break;
        }
        case 134: {                                   // ATOD: A buf -> D x, I n
            const uint16_t buf = dblWord(sp);
            char text[80];
            int k = 0;
            for (; k < 79 && m_mem[(uint16_t)(buf + k)]; k++) text[k] = (char)m_mem[(uint16_t)(buf + k)];
            text[k] = 0;
            char* end = text;
            const double x = std::strtod(text, &end);
            m_cpu.r.SP = (uint16_t)(sp + 2 - D);
            dblPut(m_cpu.r.SP, end == text ? 0.0 : x);
            PushStackWord((uint16_t)(end - text));
            break;
        }
        case 135: {                                   // ATODM: A buf, A dst -> I n
            const uint16_t dst = dblWord(sp), buf = dblWord((uint16_t)(sp + 2));
            char text[80];
            int k = 0;
            for (; k < 79 && m_mem[(uint16_t)(buf + k)]; k++) text[k] = (char)m_mem[(uint16_t)(buf + k)];
            text[k] = 0;
            char* end = text;
            const double x = std::strtod(text, &end);
            dblPut(dst, end == text ? 0.0 : x);
            m_cpu.r.SP = (uint16_t)(sp + 4);
            PushStackWord((uint16_t)(end - text));
            break;
        }
        default:
            dblDone = false;                          // not assigned: falls through
            break;
        }
    }
    if (dblDone) {
        m_cpu.r.setBC((uint16_t)(m_cpu.r.BC() + 1));
        m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF;
        m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8;
        m_cpu.r.PC = 0x03A4;                          // BACK1, as the 25..31 block exits
        m_opcodeEmulatedCount[OP_CSP]++;
        break;
    }
}
