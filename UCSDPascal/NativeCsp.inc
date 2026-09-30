// NativeCsp.inc -- shared by PSystemEngine.cpp and the linux-harness copies.
// Included inside CSP just before the CSPTBL jump-table dispatch. Native C
// versions of ten standard procedures, written from STP.TEXT / the SYSTEM.
// MICRO listing:
//   0 IOC   1 NEW   2 MVL   3 MVR   4 EXIT  7 IDS   8 TRS   9 TIM
//   10 FLC  11 SCN  21 GSEG 22 RSEG 23 TNC  24 RND  32 MRK  33 RLS
//   34 IOR  35 UBUSY 36 POT 37 UWAIT 40 MEMA
//   5 UREAD  6 UWRITE  38 UCLEAR  (unit I/O: disk and character drivers)
// 39 HLT and IOC's error go to the run-time error labels (NativeErrors.inc).
// Host must also provide PortIn/PortOut and
//   bool NativeBiosCall(uint8_t fn, uint8_t c, uint8_t* aOut, bool dryRun)
// (dryRun: only report whether BIOS function fn is handled natively).
// Error exits (IOC with IORSLT<>0, EXIT's NOEXIT, TNC/RND overflow, NEW's
// STKOVR, GSEG on a unit not natively readable) are detected up front and
// fall through, untouched, to the real Z80 routine.
// These only compute P-machine state (stack, heap pointer, memory); they do
// NOT reproduce the Z80 routines' leftover A/DE/HL, so they run only when
// Preserve Z80 Register Compatibility is OFF. With it ON, the real Z80
// routines run as before and register-exact trace-diffs stay valid.
//
// Each real routine exits via BACK1 (BC := IPCSAV) or via BACK with BC
// untouched since CSP's own "INC BC; SAVIPC"; either way the next fetch is
// at IPC+1, so every case here commits through plain BACK with BC = IPC+1.
// NEW falls through (nothing written) if it would hit STKOVR.
// Host must #define PM_MEM, PM_CPU, PM_PRESERVE, PM_COUNT(op) and PM_ROM(addr).
if (!PM_PRESERVE) {
    auto csRd = [&](uint16_t a) -> uint16_t { return (uint16_t)(PM_MEM[a] | (PM_MEM[(uint16_t)(a + 1)] << 8)); };
    // Reads of the interpreter's own TABLES and CODE BYTES (CSP table, unit
    // table, driver code, powers of ten, reserved words) go through PM_ROM:
    // the host may reclaim the interpreter's memory for the heap and keep a
    // snapshot of the image instead (PSystemEngine: SetReclaimInterpreterMemory).
    auto romB = [&](uint16_t a) -> uint8_t { return (uint8_t)PM_ROM(a); };
    auto romW = [&](uint16_t a) -> uint16_t { return (uint16_t)(romB(a) | (romB((uint16_t)(a + 1)) << 8)); };
    auto csWr = [&](uint16_t a, uint16_t v) { PM_MEM[a] = (uint8_t)(v & 0xFF); PM_MEM[(uint16_t)(a + 1)] = (uint8_t)(v >> 8); };
    auto csCgdirp = [&]() {                       // CGDIRP: release GDIRP if it isn't NIL (=1)
        uint16_t g = csRd(PM_V(0x02EC));
        if (g != 0x0001) { csWr(PM_V(0x0240), g); csWr(PM_V(0x02EC), 0x0001); }
    };
#include "NativeSegLoad.inc"
    // The interpreter's own unit-I/O tables, found through UWAIT's CALL GETU
    // (UWAIT: LD A,3; LD (UREQ),A; CALL GETU) so the right addresses are
    // used whichever SYSTEM.MICRO build is running. In GETU: CP MAXU+1 at
    // +11 (operand +12), LD DE,UNITBL at +24 (operand +25). Every opcode is
    // checked; if anything differs, the unit-I/O cases leave it to Z80.
    const PmIoConfig& io = PM_IOCFG;          // unit-I/O configuration (PCodeOpcodes.h)
    const bool ioTablesOk = io.ok;
    const uint8_t ioMaxu1 = io.maxu1;
    const uint16_t ioUnitbl = io.unitbl;
    bool cspDone = false;
    uint16_t cspJump = 0;   // a run-time error label to continue at (NativeErrors.inc)
    uint16_t cspNextIpc = (uint16_t)(PM_CPU.r.BC() + 1);   // EXIT overrides
    switch (procNum) {
        case 0:    // IOC: IO check
            if (csRd(PM_V(0x02E4)) != 0) cspJump = 0x040D;        // UIOERR (NativeErrors.inc)
            cspDone = true; break;
        case 39:   // HLT: unconditional halt -> the HLT label (SAVIPC; error 14)
            cspJump = 0x0425;
            cspDone = true; break;
        case 4: {  // EXIT(seg_num, proc_num): rewrite MSIPCs up the dynamic chain
            uint16_t procW = PeekStackWord(0), segW = PeekStackWord(2);
            uint8_t pn = (uint8_t)procW, sn = (uint8_t)segW;
            auto exitOf = [&](uint16_t jt) -> uint16_t { uint16_t q = (uint16_t)(jt - 4); return (uint16_t)(q - csRd(q)); };
            uint16_t jt0 = csRd(PM_V(0x02F4));
            uint16_t ipcsav = exitOf(jt0);
            std::vector<std::pair<uint16_t,uint16_t>> msipcWrites;
            uint16_t sys = 0;
            bool inChain = !(PM_MEM[jt0] == pn && PM_MEM[csRd(PM_V(0x02F6))] == sn);
            bool noExit = false;
            if (inChain) {
                sys = csRd(csRd(PM_V(0x02F0)));                    // ^PASCALSYSTEM's MSCW
                uint16_t c = csRd(PM_V(0x02F2));
                bool ok = false;
                for (int it = 0; it < 4096; it++) {
                    if (c == sys) break;                     // reached PASCALSYSTEM's MSCW: NOEXIT
                    uint16_t pj = csRd((uint16_t)(c + 4));
                    msipcWrites.push_back({ (uint16_t)(c + 8), exitOf(pj) });
                    if (PM_MEM[pj] == pn && PM_MEM[csRd((uint16_t)(c + 6))] == sn) { ok = true; break; }
                    c = csRd((uint16_t)(c + 2));
                }
                if (!ok) noExit = true;   // the Z80 EXIT has done every write below by then
            }
            PopStackWord(); PopStackWord();
            csWr(PM_V(0x0290), procW);                             // PROCNUM
            csWr(PM_V(0x02C6), segW);                              // SEGNUM (word store)
            if (inChain) csWr(PM_V(0x0292), sys);                  // SYSMSCW
            for (auto& w : msipcWrites) csWr(w.first, w.second);
            cspNextIpc = ipcsav;                             // BACK1 with IPCSAV = current proc's exit code
            if (noExit) cspJump = 0x03E4;                    // NOEXIT (IPCSAV = the exit IPC, as the Z80 leaves it)
            cspDone = true; break;
        }
        case 7: {  // IDS idsearch(VAR symcursor; symbuf): reserved-word lookup (RESTBL)
            uint16_t symbufp = PeekStackWord(0), symcur = PeekStackWord(2);
            const uint16_t RESTBL = 0x1766;
            uint8_t tok[8] = { 0, ' ', ' ', ' ', ' ', ' ', ' ', ' ' };
            uint16_t de = (uint16_t)(symbufp + csRd(symcur));
            uint8_t a = (uint8_t)(PM_MEM[de] & 0x7F); if (a >= 0x60) a = (uint8_t)(a - 0x20);
            tok[0] = a; de = (uint16_t)(de + 1);
            uint8_t b = 7; int ti = 0; bool ended = false;
            for (uint32_t guard = 0; guard < 0x10000; guard++) {
                a = (uint8_t)(PM_MEM[de] & 0x7F);
                if (a == 0x5F) { de = (uint16_t)(de + 1); continue; }     // '_' ignored
                if (a >= 0x60) a = (uint8_t)(a - 0x20);
                bool ok = (a >= 'A' && a < 'A' + 26) || (a >= '0' && a <= '9');
                if (!ok) { ended = true; break; }
                b = (uint8_t)(b - 1);
                if (!(b & 0x80)) { ti++; if (ti <= 7) tok[ti] = a; else { ended = false; break; } }
                de = (uint16_t)(de + 1);
            }
            if (!ended) break;                               // pathological >135-char token: stays Z80
            uint16_t newCur = (uint16_t)(de - 1 - symbufp);
            auto calcad = [&](uint8_t ch) -> uint16_t {
                uint8_t i2 = (uint8_t)((uint8_t)(ch - 0x41) * 2);
                uint16_t n = romW((uint16_t)(RESTBL + i2));
                return (uint16_t)(RESTBL + (uint16_t)(n * 12) + 54);
            };
            uint16_t rec = calcad(tok[0]), idend = calcad((uint8_t)(tok[0] + 1));
            bool found = false;
            for (int it = 0; it < 8192 && rec != idend; it++) {
                bool eq = true;
                for (int k = 1; k <= 7; k++) if (romB((uint16_t)(rec + k)) != tok[k]) { eq = false; break; }
                if (eq) { found = true; break; }
                rec = (uint16_t)(rec + 12);
            }
            if (!found && rec != idend) break;               // runaway table scan: stays Z80
            PopStackWord(); PopStackWord();
            csWr(PM_V(0x0294), RESTBL); csWr(PM_V(0x0292), symbufp); csWr(PM_V(0x0290), symcur); // RESWRDP/SYMBUFP/SYMCUR
            for (int k = 0; k < 8; k++) PM_MEM[(uint16_t)(PM_V(0x0298) + k)] = tok[k]; // TOKEN
            csWr(symcur, newCur);
            csWr(PM_V(0x0296), idend);                             // IDEND
            if (found) {
                for (int k = 0; k < 4; k++) PM_MEM[(uint16_t)(symcur + 2 + k)] = romB((uint16_t)(rec + 8 + k));
            } else {
                PM_MEM[(uint16_t)(symcur + 2)] = 0; PM_MEM[(uint16_t)(symcur + 3)] = 0;
                for (int k = 0; k < 8; k++) PM_MEM[(uint16_t)(symcur + 6 + k)] = tok[k];
            }
            cspDone = true; break;
        }
        case 8: {  // TRS treesearch(root, VAR found, target): 0 match, 1 right-NIL, -1 left-NIL
            uint16_t target = PeekStackWord(0), foundp = PeekStackWord(2), node = PeekStackWord(4);
            uint16_t result = 0; bool done = false;
            for (int it = 0; it < 65536; it++) {
                int i = 0;
                for (; i < 8; i++) if (PM_MEM[(uint16_t)(node + i)] != PM_MEM[(uint16_t)(target + i)]) break;
                if (i == 8) { result = 0; done = true; break; }
                bool right = PM_MEM[(uint16_t)(node + i)] < PM_MEM[(uint16_t)(target + i)]; // CP carry
                uint16_t link = csRd((uint16_t)(node + (right ? 8 : 10)));
                if (link == 0x0001) { result = right ? 0x0001 : 0xFFFF; done = true; break; }
                node = link;
            }
            if (!done) break;
            PopStackWord(); PopStackWord(); PopStackWord();
            csWr(PM_V(0x0290), target); csWr(PM_V(0x0292), foundp); csWr(PM_V(0x0294), node); // TARGETP/FOUNDP/ROOTP
            PushStackWord(result);
            csWr(foundp, node);
            cspDone = true; break;
        }
        case 21: { // GSEG getseg(seg): GETSEG, then STKCHK (result ignored by the real code)
            uint8_t seg = (uint8_t)PeekStackWord(0);
            uint16_t hi = (uint16_t)(PM_V(0x0251) + 4 * (uint16_t)seg), lo = slDecL(hi);
            if ((PM_MEM[hi] | PM_MEM[lo]) != 0) {            // resident: refcount++
                PopStackWord();
                PM_MEM[PM_V(0x02C6)] = seg;
                uint16_t n = (uint16_t)(((PM_MEM[hi] << 8) | PM_MEM[lo]) + 1);
                PM_MEM[lo] = (uint8_t)n; PM_MEM[slIncL(lo)] = (uint8_t)(n >> 8);
            } else {
                SegLoadPlan gp;
                const int gs = slLoad(seg, (uint16_t)(PM_CPU.r.SP + 2), gp);
                if (gs == SL_DECLINE) break;                 // stays Z80
                PopStackWord();
                if (gs != SL_OK) {                           // segment load error -> NOPROC / SYIOER
                    cspJump = slError(gp, gs, 0x1514);       // GSEG's CALL GETSEG return point
                    cspDone = true; break;
                }
                slCommit(gp);                                // leaves the segment on the stack
            }
            cspDone = true; break;
        }
        case 22: { // RSEG releaseseg(seg): DECREF; if it hits 0, pop the segment off the stack
            uint8_t seg = (uint8_t)PopStackWord();
            uint16_t hi = (uint16_t)(PM_V(0x0251) + 4 * (uint16_t)seg), lo = slDecL(hi);
            uint16_t n = (uint16_t)(((PM_MEM[hi] << 8) | PM_MEM[lo]) - 1);
            PM_MEM[lo] = (uint8_t)n; PM_MEM[hi] = (uint8_t)(n >> 8);
            if (n == 0) {
                uint16_t a = (uint16_t)(hi + 1);
                uint8_t e = PM_MEM[a]; a = slIncL(a); uint8_t d = PM_MEM[a];
                uint16_t h = slIncL((uint16_t)((d << 8) | e));
                PM_CPU.r.SP = (uint16_t)(h + 1);             // SP := ^seg+2
            }
            cspDone = true; break;
        }
        case 23:   // TNC trunc(x): FPFFIX
        case 24: { // RND round(x): FPFADD(x, +-0.5) then FPFFIX -- bit-exact via UcsdReal.h
            uint8_t x[4];
            for (int k = 0; k < 4; k++) x[k] = PM_MEM[(uint16_t)(PM_CPU.r.SP + k)];
            uint16_t v;
            const bool ok = procNum == 23 ? ucsdreal::Fix(x, v) : ucsdreal::Rnd(x, v);
            PopStackWord(); PopStackWord();
            PushStackWord(v);                                // on overflow FPFFIX pushes 7FFF / 8000...
            PM_MEM[PM_V(0x0248)] = ok ? 0 : 1;                     // ...and sets FPERROR := 1 (FPLCBEG cleared it)
            if (!ok) cspJump = 0x041B;                       // FPLCHK -> FPIERR
            cspDone = true; break;
        }
        case 36: { // POT pwroften(i): 10^i from TENTBL, 0 <= i <= 38
            uint16_t i = PeekStackWord(0);
            if (i >= 39) {                                   // FPFPOT: 0.0 in its place, FPERROR = 1,
                PopStackWord(); PushStackWord(0); PushStackWord(0);   // then FPLCHK -> FPIERR
                PM_MEM[PM_V(0x0248)] = 1;
                cspJump = 0x041B;
                cspDone = true; break;
            }
            uint16_t t = (uint16_t)(0x0FF7 + 4 * i);
            PopStackWord();
            PushStackWord(romW((uint16_t)(t + 2)));
            PushStackWord(romW(t));
            PM_MEM[PM_V(0x0248)] = 0;                              // FPLCBEG
            cspDone = true; break;
        }
        case 5:    // UREAD(unit, VAR buf, count, block, async)
        case 6:    // UWRITE(unit, VAR buf, count, block, async)
        case 38: { // UCLEAR(unit)
            // Mirrors the running interpreter's unit I/O (SYSIO -> GETU ->
            // CALLIO -> driver), disassembled from BIGGY's SYSTEM.MICRO (see
            // verify/INTERP_FULL_LISTING.txt for the related U120 CPMIO.TEXT):
            //   disk driver: unit, block, length and buffer to the Big Disk
            //     ports, request to port CFh, status back into IORSLT (a
            //     clear first calls BIOS HOME);
            //   character driver: per character, BIOS input with the EOF
            //     check (console: zero-fills the rest, echoes) or output
            //     through ECHO (DLE blank expansion, CR -> CR LF);
            //   console clear: CHCLR flushes type-ahead.
            // It calls the engine's own PortIn/PortOut and BIOS functions --
            // the very ones the Z80 driver's OUT/IN and BIOS calls reach --
            // so devices behave identically. Anything it does not recognise
            // (other driver code, a BIOS function not handled natively) is
            // detected before anything is changed and left to the Z80 code.
            if (!ioTablesOk) break;
            const uint8_t req = procNum == 5 ? 0x01 : (procNum == 6 ? 0x02 : 0x04);
            uint16_t asyW = 0, blkW = 0, lenW = 0, buf = 0;
            uint8_t unit;
            if (procNum != 38) {
                asyW = PeekStackWord(0); blkW = PeekStackWord(2); lenW = PeekStackWord(4);
                buf = (uint16_t)(PeekStackWord(8) + PeekStackWord(6));
                unit = (uint8_t)PeekStackWord(10);
            } else {
                unit = (uint8_t)PeekStackWord(0);
                // UCLEAR goes straight to CALLIO: the driver sees the
                // parameters left by the previous request.
                asyW = csRd(PM_V(0x02DA)); blkW = csRd(PM_V(0x02D8)); lenW = csRd(PM_V(0x02D6)); buf = csRd(PM_V(0x02D4));
            }
            const bool badUnit = unit == 0 || unit >= ioMaxu1;
            const uint16_t upt = (uint16_t)(ioUnitbl + (uint8_t)(unit * 4));
            const bool dirOk = !badUnit && (io.flags[unit] & req) != 0;
            const bool callDriver = dirOk && !(lenW == 0 && !(asyW & 0x02));   // CALLIO's "nothing to do"
            enum { NODRV, DISK, CHAR } kind = NODRV;
            uint16_t syeof = 0;
            bool char12 = false;
            uint8_t civ = 0, cov = 0;
            if (callDriver) {
                if (io.kind[unit] == PMIO_DISK) {
                    kind = DISK;
                    if ((req & 0x04) && !NativeBiosCall(0x18, 0, nullptr, true)) break;          // BIOS HOME
                } else if (io.kind[unit] == PMIO_CHAR12 || io.kind[unit] == PMIO_CHAR678) {
                    kind = CHAR;
                    char12 = io.kind[unit] == PMIO_CHAR12;                    // units 1,2 (clear flushes)
                    syeof = PM_V(io.syeof);
                    civ = io.civ[unit];
                    cov = io.cov[unit];
                    if (req == 0x04) {
                        if (char12 && (!NativeBiosCall(0x06, 0, nullptr, true) || !NativeBiosCall(0x09, 0, nullptr, true))) break;
                    } else if (req == 0x02) {
                        if (!NativeBiosCall(cov, 0, nullptr, true)) break;
                    } else {
                        if (!NativeBiosCall(civ, 0, nullptr, true)) break;
                        if (unit == 1 && !NativeBiosCall(cov, 0, nullptr, true)) break;
                    }
                } else break;                                                 // unknown driver: Z80
            }
            // ---- commit ----
            for (int k = 0; k < (procNum == 38 ? 1 : 6); k++) PopStackWord();   // SYSIO pops 5, GETU the unit
            PM_MEM[PM_V(0x02D2)] = req;                                             // UREQ
            csWr(PM_V(0x02DC), 0x03A4);                                             // URTN := BACK1
            if (procNum != 38) { csWr(PM_V(0x02DA), asyW); csWr(PM_V(0x02D8), blkW); csWr(PM_V(0x02D6), lenW); csWr(PM_V(0x02D4), buf); }
            PM_MEM[PM_V(0x02E4)] = 0;                                               // GETU: IORSLT := 0 (low byte)
            if (badUnit) { PM_MEM[PM_V(0x02E4)] = 2; cspDone = true; break; }       // BLUN
            PM_MEM[PM_V(0x02D3)] = unit;                                            // UNIT
            csWr(PM_V(0x02D0), upt);                                                // UPTR
            if (!dirOk) { PM_MEM[PM_V(0x02E4)] = 3; cspDone = true; break; }        // BDIR
            if (kind == DISK) {
                PortOut(0xC8, unit);
                if (req & 0x04) NativeBiosCall(0x18, 0, nullptr, false);      // BIOS HOME
                PortOut(0xCD, (uint8_t)(blkW >> 8)); PortOut(0xCE, (uint8_t)blkW);
                PortOut(0xCB, (uint8_t)(lenW >> 8)); PortOut(0xCC, (uint8_t)lenW);
                PortOut(0x10, (uint8_t)(buf >> 8));  PortOut(0x0F, (uint8_t)buf);
                PortOut(0xCF, req);
                PM_MEM[PM_V(0x02E4)] = PortIn(0xCF);                                // status -> IORSLT (byte)
            } else if (kind == CHAR) {
                if (req == 0x04) {
                    if (char12) {
                        PM_MEM[PM_V(0x02E1)] = 0;                                   // CLAST
                        for (;;) {                                            // CHCLR: flush type-ahead
                            uint8_t a = 0;
                            NativeBiosCall(0x06, 0, &a, false);
                            if (a == 0) break;
                            NativeBiosCall(0x09, 0, nullptr, false);
                        }
                    }
                } else {
                    PM_MEM[PM_V(0x02E2)] = civ; PM_MEM[PM_V(0x02E3)] = cov;              // SETVECT: CIVECT / COVECT
                    auto out = [&](uint8_t ch) { NativeBiosCall(cov, ch, nullptr, false); };
                    auto echo = [&](uint8_t ch) {                             // ECHO
                        const uint8_t asy = PM_MEM[PM_V(0x02DA)];
                        if (!(asy & 0x04) && PM_MEM[PM_V(0x02E1)] == 0x10) {        // blank count after DLE
                            PM_MEM[PM_V(0x02E1)] = (uint8_t)(ch - 0x20);
                            for (;;) {
                                uint8_t a = (uint8_t)(PM_MEM[PM_V(0x02E1)] - 1);
                                if (a & 0x80) return;
                                PM_MEM[PM_V(0x02E1)] = a;
                                out(0x20);
                            }
                        }
                        PM_MEM[PM_V(0x02E1)] = ch;
                        if (ch == 0x10) {
                            if (!(asy & 0x04)) return;                        // DLE: wait for the count
                            PM_MEM[PM_V(0x02E1)] = 0x20;
                        }
                        out(ch);
                        if (asy & 0x08) return;                               // CRLFBIT: no LF after CR
                        if (PM_MEM[PM_V(0x02E1)] != 0x0D) return;
                        PM_MEM[PM_V(0x02E1)] = 0x0A;
                        out(0x0A);
                    };
                    uint16_t hl = buf, de = lenW;
                    while (de != 0) {
                        de--;
                        if (req == 0x02) { echo(PM_MEM[hl]); hl++; continue; }
                        uint8_t c = 0;
                        NativeBiosCall(civ, 0, &c, false);
                        PM_MEM[hl] = c;
                        if (!(PM_MEM[PM_V(0x02DA)] & 0x04) && c == PM_MEM[syeof]) {       // end of file
                            if (unit == 1) { de++; do { PM_MEM[hl] = 0; de--; hl++; } while (de != 0); }
                            break;
                        }
                        if (unit == 1) echo(c);
                        hl++;
                    }
                }
            }
            cspDone = true; break;
        }
        case 35:   // UBUSY(unit): always false on this system
        case 37: { // UWAIT(unit): nothing to wait for
            if (!ioTablesOk) break;
            uint8_t unit = (uint8_t)PeekStackWord(0);        // GETU uses the low byte
            uint16_t upt = (uint16_t)(ioUnitbl + (uint8_t)(unit * 4));
            if (unit == 0 || unit >= ioMaxu1 ||              // 0 < unit <= MAXU (MAXU+1 is GETU's CP operand)
                (io.flags[unit] & 0x03) == 0) break;       // UWAIT asks INBIT|OUTBIT: GETU error, stays Z80
            PM_MEM[PM_V(0x02E4)] = 0;                              // IORSLT := 0 (GETU, byte store)
            PM_MEM[PM_V(0x02D2)] = 0x03;                           // UREQ := INBIT|OUTBIT (UWAIT, also via UBUSY)
            PM_MEM[PM_V(0x02D3)] = unit;                           // UNIT
            csWr(PM_V(0x02D0), upt);                               // UPTR
            PopStackWord();
            if (procNum == 35) PushStackWord(0);             // UBUSY result: false
            cspDone = true; break;
        }
        case 1: {  // NEW(VAR p: ^; size_p: integer)
            uint16_t g = csRd(PM_V(0x02EC));
            uint16_t np0 = (g != 0x0001) ? g : csRd(PM_V(0x0240));
            uint16_t sizeW = PeekStackWord(0);
            uint16_t pAddr = PeekStackWord(2);
            uint16_t newNp = (uint16_t)(np0 + 2 * sizeW);
            uint16_t spAfter = (uint16_t)(PM_CPU.r.SP + 4);
            const bool nwOverflow = (uint16_t)(spAfter - 62) < newNp;  // STKCHK carry
            csCgdirp();
            PopStackWord(); PopStackWord();
            csWr(pAddr, np0);
            csWr(PM_V(0x0240), newNp);
            if (nwOverflow) cspJump = 0x03E9;           // the Z80 NEW does all of the above, then STKOVR
            cspDone = true; break;
        }
        case 2:    // MVL moveleft(src_base, src_idx, dst_base, dst_idx, len) -- LDIR
        case 3: {  // MVR moveright -- LDDR from the far end
            uint16_t len = PopStackWord();
            uint16_t di = PopStackWord(), db = PopStackWord();
            uint16_t si = PopStackWord(), sb = PopStackWord();
            uint16_t dst = (uint16_t)(db + di), src = (uint16_t)(sb + si);
            if ((int16_t)len > 0) {                   // TSTCNT: count <= 0 is a no-op
                if (procNum == 2) for (uint16_t i = 0; i < len; i++) PM_MEM[(uint16_t)(dst + i)] = PM_MEM[(uint16_t)(src + i)];
                else for (uint16_t i = len; i-- > 0; )             PM_MEM[(uint16_t)(dst + i)] = PM_MEM[(uint16_t)(src + i)];
            }
            cspDone = true; break;
        }
        case 9: {  // TIM(VAR hitime, lotime: integer)
            NativeClockUpdate();                             // the host clock (if on): PSystemEngine::SetHostClock
            uint16_t loAddr = PopStackWord(); csWr(loAddr, csRd(PM_V(0x031A)));
            uint16_t hiAddr = PopStackWord(); csWr(hiAddr, csRd(PM_V(0x031C)));
            cspDone = true; break;
        }
        case 10: { // FLC fillchar(buffer, index, count, ch)
            uint8_t  ch    = (uint8_t)(PopStackWord() & 0xFF);
            uint16_t count = PopStackWord();
            uint16_t index = PopStackWord();
            uint16_t base  = PopStackWord();
            uint16_t start = (uint16_t)(base + index);
            if ((int16_t)count > 0)
                for (uint16_t i = 0; i < count; i++) PM_MEM[(uint16_t)(start + i)] = ch;
            cspDone = true; break;
        }
        case 11: { // SCN scan(maxdisp, forpast, ch, base, index, mask) -> displacement
            PopStackWord();                             // mask (junked by the real code too)
            uint16_t index = PopStackWord();
            uint16_t base  = PopStackWord();
            uint8_t  ch    = (uint8_t)(PopStackWord() & 0xFF);
            bool     past  = (PopStackWord() & 1) != 0; // POP AF: carry = bit 0 of the low byte
            int16_t  md    = (int16_t)PopStackWord();
            uint16_t start = (uint16_t)(base + index), p = start;
            uint32_t n = (md >= 0) ? (uint32_t)md : (uint32_t)(-(int32_t)md);
            for (uint32_t k = 0; k < n; k++) {
                bool eq = (PM_MEM[p] == ch);
                if (past ? !eq : eq) break;
                p = (uint16_t)(md >= 0 ? p + 1 : p - 1);
            }
            PushStackWord((uint16_t)(p - start));
            cspDone = true; break;
        }
        case 32: { // MRK mark(VAR i): i := NP
            csCgdirp();
            uint16_t addr = PopStackWord();
            csWr(addr, csRd(PM_V(0x0240)));
            cspDone = true; break;
        }
        case 33: { // RLS release(VAR i): NP := i; GDIRP := NIL
            uint16_t addr = PopStackWord();
            csWr(PM_V(0x0240), csRd(addr));
            csWr(PM_V(0x02EC), 0x0001);
            cspDone = true; break;
        }
        case 34:   // IOR ioresult: push IORSLT
            PushStackWord(csRd(PM_V(0x02E4)));
            cspDone = true; break;
        case 40:   // MEMA memavail: (SP - NP) / 2, unsigned
            PushStackWord((uint16_t)((uint16_t)(PM_CPU.r.SP - csRd(PM_V(0x0240))) >> 1));
            cspDone = true; break;
        default: break;
    }
    if (cspDone && cspJump) {                         // run-time error: IPC / IPCSAV as the CSP
        PM_CPU.r.setBC(cspNextIpc);                   // dispatch leaves them, then the error label
        csWr(PM_V(0x0246), cspNextIpc);
        PM_CPU.r.PC = cspJump;
        PM_COUNT(OP_CSP);
        break;
    }
    if (cspDone) {
        PM_CPU.r.setBC(cspNextIpc);
        csWr(PM_V(0x0246), cspNextIpc);                     // IPCSAV, as the real routine leaves it for BACK1
        commit = true;
        PM_COUNT(OP_CSP);
        break;
    }
}
