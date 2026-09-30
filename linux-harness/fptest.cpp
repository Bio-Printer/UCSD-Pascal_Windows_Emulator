// fptest.cpp -- differential test of UcsdReal.h against the real Z80
// floating-point routines in SYSTEM.MICRO. Boots the pure-Z80 harness
// engine until the interpreter is resident, then for each case puts the
// operands on a scratch stack, points PC at the p-code wrapper (ADR, SBR,
// ...) and single-steps until BACK1 (success) or FPIERR (FP error).
//   ./fptest <data dir> [cases per op]
#define main harness_main_unused
#include "harness.cpp"
#undef main
#include "UcsdReal.h"
#include <random>

static Engine* E;
static uint16_t scratchSP;

// Run a wrapper. stack[] = bytes from the stack pointer up. Returns false
// if the Z80 code reached FPIERR (FP error); result bytes read from the new SP.
static bool z80call(uint16_t entry, const uint8_t* stack, int n, uint8_t* res, int rn) {
    uint16_t sp = (uint16_t)(scratchSP - n);
    for (int i = 0; i < n; i++) E->mem[(uint16_t)(sp + i)] = stack[i];
    E->cpu.r.SP = sp; E->cpu.r.setBC(0x3000); E->cpu.r.PC = entry;
    for (long k = 0; k < 400000; k++) {
        uint16_t pc = E->cpu.r.PC;
        if (pc == 0x03A4 || pc == 0x03B0) {
            for (int i = 0; i < rn; i++) res[i] = E->mem[(uint16_t)(E->cpu.r.SP + i)];
            return true;
        }
        if (pc == 0x041B) {                       // FPIERR: the error result is on the stack too
            for (int i = 0; i < rn; i++) res[i] = E->mem[(uint16_t)(E->cpu.r.SP + i)];
            return false;
        }
        E->cpu.step();
    }
    fprintf(stderr, "runaway at entry %04X\n", entry); exit(2);
}

static std::mt19937_64 rng(12345);
static uint8_t rb() { return (uint8_t)rng(); }
static void randReal(uint8_t r[4], int mode) {
    r[1] = rb(); r[2] = rb(); r[3] = rb();
    switch (mode % 6) {
        case 0: r[0] = rb(); break;                                  // anything
        case 1: r[0] = (uint8_t)(0x70 + rng() % 0x20); break;        // ordinary magnitudes
        case 2: r[0] = (uint8_t)(rng() % 4 == 0 ? 0 : rng() % 3); break; // zero / tiny
        case 3: r[0] = (uint8_t)(0xFD + rng() % 3); break;           // huge
        case 4: r[0] = (uint8_t)(0x80 + rng() % 16); r[2] = r[3] = 0; break; // short mantissas
        case 5: r[0] = (uint8_t)(0x40 + rng() % 0x80); break;
    }
}

static long fails = 0;
static void report(const char* op, const uint8_t* a, const uint8_t* b, bool zok, const uint8_t* zr, bool cok, const uint8_t* cr, int rn) {
    if (++fails <= 40) {
        printf("  MISMATCH %s a=%02X%02X%02X%02X", op, a[0], a[1], a[2], a[3]);
        if (b) printf(" b=%02X%02X%02X%02X", b[0], b[1], b[2], b[3]);
        printf("  z80:%s", zok ? "" : "ERROR "); for (int i = 0; i < rn; i++) printf("%02X", zr[i]);
        printf("  C:%s", cok ? "" : "ERROR "); for (int i = 0; i < rn; i++) printf("%02X", cr[i]);
        printf("\n");
    }
}

int main(int argc, char** argv) {
    std::string d = argc > 1 ? argv[1] : "data";
    long N = argc > 2 ? atol(argv[2]) : 20000;
    static Engine eng; E = &eng;
    eng.useNativeOps = false; eng.maxInstructions = 3000000;
    if (!eng.LoadFiles(d + "/pascal.bin", d + "/Big_Disk.BLK", d + "/Empty_Big_Disk.BLK")) return 1;
    eng.RunLoop();
    if (eng.mem[0x10D1] != 0xCD || eng.mem[0x10D4] != 0xCD) { printf("interpreter not resident\n"); return 1; }
    scratchSP = (uint16_t)(eng.cpu.r.SP - 0x200);
    printf("interpreter resident; scratch stack at %04X; %ld cases per op\n", scratchSP, N);

    struct Bin { const char* name; uint16_t entry; int kind; } bins[] = {
        {"ADR", 0x10D1, 0}, {"SBR", 0x10DA, 1}, {"MPR", 0x10E3, 2}, {"DVR", 0x10F5, 3} };
    for (auto& op : bins) {
        long before = fails, errs = 0;
        for (long i = 0; i < N; i++) {
            uint8_t a[4], b[4], st[8], zr[4], cr[4];
            randReal(a, (int)i); randReal(b, (int)(i / 6 + rng() % 6));
            if (op.kind <= 1 && i % 5 == 0) {       // near-equal operands: cancellation paths
                memcpy(b, a, 4); if (i % 10 == 0) b[3] ^= (uint8_t)(1 << (rng() % 8));
                if (i % 15 == 0) b[0] = (uint8_t)(a[0] + (int)(rng() % 5) - 2);
            }
            if (op.kind <= 1 && i % 7 == 0 && a[0] > 30) b[0] = (uint8_t)(a[0] - 22 - rng() % 6); // shift ~22..27
            if (op.kind >= 2 && i % 4 == 0) {       // exponent sums/differences at the edges
                a[0] = (uint8_t)(rng() % 2 ? 0x40 + rng() % 0x80 : rng());
                int want = op.kind == 2 ? (int)(0x80 + (rng() % 5) - 2 + (rng() % 2 ? 0x80 : 0)) - a[0] + 1
                                        : a[0] - ((rng() % 2 ? 127 : -128) + (int)(rng() % 5) - 2);
                b[0] = (uint8_t)want;
            }
            // stack: TOS = b (arg2), then a (arg1)
            memcpy(st, b, 4); memcpy(st + 4, a, 4);
            bool zok = z80call(op.entry, st, 8, zr, 4);
            bool cok = op.kind == 0 ? ucsdreal::AddSub(a, b, false, cr) : op.kind == 1 ? ucsdreal::AddSub(a, b, true, cr)
                     : op.kind == 2 ? ucsdreal::Mul(a, b, cr) : ucsdreal::Div(a, b, cr);
            if (!zok) errs++;
            if (zok != cok || (memcmp(zr, cr, 4))) report(op.name, a, b, zok, zr, cok, cr, 4);
        }
        printf("%-4s %8ld cases, %6ld Z80 FP errors (overflow/underflow/zero-divide): %s\n", op.name, N, errs,
               fails == before ? "ALL MATCH" : "MISMATCHES");
    }
    // SQR (x*x), NGR, ABR
    { long before = fails, errs = 0;
      for (long i = 0; i < N; i++) { uint8_t a[4], zr[4], cr[4]; randReal(a, (int)i);
        bool zok = z80call(0x10EC, a, 4, zr, 4), cok = ucsdreal::Mul(a, a, cr); if (!zok) errs++;
        if (zok != cok || (memcmp(zr, cr, 4))) report("SQR", a, nullptr, zok, zr, cok, cr, 4); }
      printf("SQR  %8ld cases, %6ld Z80 FP errors: %s\n", N, errs, fails == before ? "ALL MATCH" : "MISMATCHES"); }
    { long before = fails;
      for (long i = 0; i < N; i++) { uint8_t a[4], zr[4], cr[4]; randReal(a, (int)i);
        memcpy(cr, a, 4); cr[1] ^= 0x80;
        bool zok = z80call(0x10FE, a, 4, zr, 4); if (!zok || memcmp(zr, cr, 4)) report("NGR", a, nullptr, zok, zr, true, cr, 4);
        memcpy(cr, a, 4); cr[1] &= 0x7F;
        zok = z80call(0x10C8, a, 4, zr, 4); if (!zok || memcmp(zr, cr, 4)) report("ABR", a, nullptr, zok, zr, true, cr, 4); }
      printf("NGR/ABR %5ld cases each: %s\n", N, fails == before ? "ALL MATCH" : "MISMATCHES"); }
    // FLT: every 16-bit integer; FLO: float the integer under a real
    { long before = fails;
      for (long v = 0; v < 65536; v++) { uint8_t st[2] = { (uint8_t)v, (uint8_t)(v >> 8) }, zr[4], cr[4];
        bool zok = z80call(0x10A6, st, 2, zr, 4); ucsdreal::Float((uint16_t)v, cr);
        if (!zok || memcmp(zr, cr, 4)) report("FLT", st, nullptr, zok, zr, true, cr, 4); }
      for (long i = 0; i < 2000; i++) { uint8_t x[4], st[6], zr[8], cr[8]; randReal(x, (int)i); uint16_t v = (uint16_t)rng();
        memcpy(st, x, 4); st[4] = (uint8_t)v; st[5] = (uint8_t)(v >> 8);
        bool zok = z80call(0x10AF, st, 6, zr, 8); memcpy(cr, x, 4); ucsdreal::Float(v, cr + 4);
        if (!zok || memcmp(zr, cr, 8)) report("FLO", st, nullptr, zok, zr, true, cr, 8); }
      printf("FLT  all 65536 integers, FLO 2000 cases: %s\n", fails == before ? "ALL MATCH" : "MISMATCHES"); }
    // TNC, RND (CSP 23/24) and POT (CSP 36)
    for (int which = 0; which < 2; which++) { long before = fails, errs = 0;
      for (long i = 0; i < N; i++) { uint8_t a[4], zr[2], cr[2]; randReal(a, (int)i);
        if (i % 3 == 0) { a[0] = (uint8_t)(0x80 + rng() % 18); }            // values near the int range edges
        if (i % 11 == 0) { a[0] = (uint8_t)(0x81 + rng() % 15); a[3] = 0; a[2] &= 0xF0; } // exact halves etc.
        uint16_t v; bool zok = z80call(which ? 0x1110 : 0x1107, a, 4, zr, 2);
        bool cok = which ? ucsdreal::Rnd(a, v) : ucsdreal::Fix(a, v); cr[0] = (uint8_t)v; cr[1] = (uint8_t)(v >> 8);
        if (!zok) errs++;
        if (zok != cok || (memcmp(zr, cr, 2))) report(which ? "RND" : "TNC", a, nullptr, zok, zr, cok, cr, 2); }
      printf("%s  %8ld cases, %6ld Z80 FP errors: %s\n", which ? "RND" : "TNC", N, errs, fails == before ? "ALL MATCH" : "MISMATCHES"); }
    { long before = fails;
      for (int i = -3; i < 45; i++) { uint8_t st[2] = { (uint8_t)i, (uint8_t)(i >> 8) }, zr[4], cr[4];
        bool zok = z80call(0x1119, st, 2, zr, 4); bool cok = (uint16_t)i < 39;
        if (cok) for (int k = 0; k < 4; k++) cr[k] = E->mem[(uint16_t)(0x0FF7 + 4 * i + k)];
        else memset(cr, 0, 4);                                   // out of range: FPFPOT leaves 0.0
        if (zok != cok || (memcmp(zr, cr, 4))) report("POT", st, nullptr, zok, zr, cok, cr, 4); }
      printf("POT  -3..44: %s\n", fails == before ? "ALL MATCH" : "MISMATCHES"); }
    // EFJ / NFJ (never emitted by the II.0 compiler, so checked here directly): the native
    // logic in NativeOps.inc -- pop b then a; EFJ jumps if a != b, NFJ if a == b; short jump
    // BC := BC+1+ofs, no jump BC := BC+1 -- against the real routines.
    for (int which = 0; which < 2; which++) { long before = fails;
      for (long i = 0; i < 50000; i++) {
        uint16_t a = (uint16_t)rng(), b = (i % 3 == 0) ? a : (uint16_t)rng();
        if (i % 7 == 0) b = (uint16_t)(a ^ (1u << (rng() % 16)));
        uint8_t ofs = (uint8_t)(rng() % 0x80), st[4] = { (uint8_t)b, (uint8_t)(b >> 8), (uint8_t)a, (uint8_t)(a >> 8) };
        E->mem[0x3000] = ofs;
        uint16_t sp = (uint16_t)(scratchSP - 4);
        for (int k = 0; k < 4; k++) E->mem[(uint16_t)(sp + k)] = st[k];
        E->cpu.r.SP = sp; E->cpu.r.setBC(0x3000); E->cpu.r.PC = which ? 0x116E : 0x115F;
        for (long k = 0; k < 1000 && E->cpu.r.PC != 0x03B0; k++) E->cpu.step();
        bool jump = which ? (a == b) : (a != b);
        uint16_t wantBC = jump ? (uint16_t)(0x3000 + 1 + ofs) : 0x3001;
        if (E->cpu.r.PC != 0x03B0 || E->cpu.r.BC() != wantBC || E->cpu.r.SP != (uint16_t)(sp + 4)) {
            if (++fails <= 12) printf("  MISMATCH %s a=%04X b=%04X ofs=%02X: z80 BC=%04X SP=%04X, native BC=%04X\n",
                which ? "NFJ" : "EFJ", a, b, ofs, E->cpu.r.BC(), E->cpu.r.SP, wantBC);
        }
      }
      printf("%s  50000 cases: %s\n", which ? "NFJ" : "EFJ", fails == before ? "ALL MATCH" : "MISMATCHES"); }
    // REAL comparisons: the six Z80 comparison routines with type REALC
    // (CSETUP reads the type byte at BC = 0x3000) against PmRealCompare.
    { long before = fails; const int N = 60000;
      static const struct { uint8_t op; uint16_t entry; const char* name; } ops[6] = {
          { OP_CEQU, 0x08D5, "CEQU" }, { OP_CNEQ, 0x08E2, "CNEQ" }, { OP_CGTR, 0x08EF, "CGTR" },
          { OP_CLEQ, 0x08FB, "CLEQ" }, { OP_CLSS, 0x0907, "CLSS" }, { OP_CGEQ, 0x0910, "CGEQ" } };
      for (int i = 0; i < N; i++) {
          uint8_t a[4], b[4];
          randReal(a, i); randReal(b, i / 6);
          switch (i % 8) {                                        // make close and equal pairs common
              case 1: memcpy(b, a, 4); break;                                      // equal
              case 2: memcpy(b, a, 4); b[3] ^= (uint8_t)(1 << (rng() % 8)); break; // differ in byte 3
              case 3: memcpy(b, a, 4); b[2] ^= (uint8_t)(1 << (rng() % 8)); break; // differ in byte 2
              case 4: memcpy(b, a, 4); b[1] ^= (uint8_t)(1 << (rng() % 7)); break; // high mantissa byte
              case 5: memcpy(b, a, 4); b[1] ^= 0x80; break;                        // opposite sign
              case 6: a[1] |= 0x80; b[1] |= 0x80; break;                           // both negative
              default: break;
          }
          const auto& o = ops[i % 6];
          uint8_t st[8] = { b[0], b[1], b[2], b[3], a[0], a[1], a[2], a[3] }, zr[2];
          E->mem[0x3000] = 2;                                      // type REALC
          z80call(o.entry, st, 8, zr, 2);
          const uint16_t zv = (uint16_t)(zr[0] | (zr[1] << 8));
          const uint16_t zret = (uint16_t)(E->mem[0x024A] | (E->mem[0x024B] << 8));
          const uint16_t cv = PmRealCompare(o.op, a, b) ? 1 : 0;
          if (zv != cv || zret != PmRealcRetadr(o.op)) {
              if (++fails <= 40) printf("  MISMATCH %s a=%02X%02X%02X%02X b=%02X%02X%02X%02X  z80 %u (RETADR %04X)  C %u\n",
                                        o.name, a[0], a[1], a[2], a[3], b[0], b[1], b[2], b[3], zv, zret, cv);
          }
      }
      printf("REALC %d cases (CEQU CNEQ CGTR CLEQ CLSS CGEQ): %s\n", N, fails == before ? "ALL MATCH" : "MISMATCHES"); }

    printf("\n%s (%ld mismatches)\n", fails ? "FAILURES" : "ALL OPERATIONS MATCH THE Z80 ROUTINES", fails);
    return fails ? 1 : 0;
}
