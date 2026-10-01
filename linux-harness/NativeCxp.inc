// NativeCxp.inc -- shared by PSystemEngine.cpp and the linux-harness copies.
// Included inside CXP after the same-segment fast path has been ruled out.
// Host must #define PM_MEM, PM_CPU, PM_PRESERVE, PM_COUNT(op) and provide
// NativeBldmscwProc(), PushStackWord(). Expects in scope: segNum, segp (current
// SEGP), the switch's `commit` flag, and the call's operands, which the
// includer supplies so that the same code serves CXP and CSP 138 (CALLI,
// NativeCalli.inc), whose segment and procedure come off the stack:
//   cxProcN  the procedure number
//   cxIpc1   IPC after the segment number (what CXP $10 saves in SAVIPC)
//   cxIpc2   the return IPC, after the procedure number (CXP: BC + 2)
//   CXP_POST (macro) the name of this include's one internal label: labels have
//            function scope, so each include in a function needs its own
//   cxWhy    (int, 0) set when the call is declined, i.e. nothing was
//            touched and the real Z80 code would take over: 1 = stack
//            overflow, 2 = assembly-language procedure, 3 = segment not
//            natively readable. CXP ignores it; CALLI, which has no Z80
//            code to fall back on, raises the matching error.
//
// Handles natively:
//   * seg 0 (the op sys): refcount[0]++, NEWSEG := MEMTOP, BLDXNL, CBPXNL
//   * any other segment already resident (GETSEG's refcount>0 branch):
//     SAVIPC, (SEGNUM) := seg, refcount++, NEWSEG := intsegt[seg].addr,
//     BLDXNL, CIPXNL
//   * a segment that is NOT resident (GETSEG -> GSGREAD: READSEG disk read,
//     RLSEG relocation, then BLDMSCW's code-read-in path), via
//     NativeSegLoad.inc, when the host can read the segment's unit natively
// Falls through to real Z80 code, touching NOTHING, when a non-resident
// segment's unit isn't natively readable (or SEGTBL says it doesn't exist),
// when the target
// is an assembly procedure (BLDMSCW's CXP refcount fix-up path), or when the
// stack would overflow (STKOVR). All three conditions are checked before
// the first write, so a fall-through leaves the machine exactly as the Z80
// CXP entry expects it.
//
// BLDXNL is BLDMSCW from BLD3 onward, i.e. identical except that NEWSEG is
// supplied by the caller instead of copied from SEGP. So this reuses the
// verified NativeBldmscw (which takes NEWSEG from SEGP) by setting SEGP to
// the new segment first, then patching the one word that differs: the
// pushed msseg, which the real code takes from the caller's SEGP.
do {
#include "NativeSegLoad.inc"
    auto cxRd = [&](uint16_t a) -> uint16_t { return (uint16_t)(PM_MEM[a] | (PM_MEM[(uint16_t)(a + 1)] << 8)); };
    auto cxWr = [&](uint16_t a, uint16_t v) { PM_MEM[a] = (uint8_t)(v & 0xFF); PM_MEM[(uint16_t)(a + 1)] = (uint8_t)(v >> 8); };

    uint16_t cxRefAddr = (uint16_t)(PM_V(0x0250) + (uint16_t)segNum * 4); // INTSEGT[seg].refcount
    uint16_t cxNewSeg;
    uint16_t cxOldMP = cxRd(PM_V(0x02F2));
    uint16_t cxNewMP = 0;
    if (segNum != 0 && cxRd(cxRefAddr) == 0) {
        // ---- not resident: GETSEG -> GSGREAD (READSEG + RLSEG), then
        // BLDXNL with carry set (BLDMSCW's "$50: code was read in" path),
        // then CIPXNL. Everything is validated against the segment image
        // BEFORE the first write.
        SegLoadPlan cxPlan;
        const int cxSl = slLoad(segNum, PM_CPU.r.SP, cxPlan);
        if (cxSl == SL_DECLINE) { cxWhy = 3; break; }              // stays Z80
        if (cxSl == SL_NOROOM) {                                   // Harvard: I-space full -> STKOVR
            cxWr(PM_V(0x0246), cxIpc1);
            PM_CPU.r.PC = 0x03E9;
            break;
        }
        if (cxSl != SL_OK) {                                       // segment load error (compat off only)
            if (PM_PRESERVE) { cxWhy = 3; break; }
            cxWr(PM_V(0x0246), cxIpc1);                                  // CXP $10: SAVIPC, then CALL GETSEG
            PM_CPU.r.PC = slError(cxPlan, cxSl, 0x13C1);           // NOPROC / SYIOER (NativeErrors.inc)
            break;
        }
        uint8_t  cxProc = cxProcN;
        uint16_t cxPtr  = (uint16_t)(cxPlan.newseg - (uint16_t)(cxProc * 2));
        if (!cxPlan.inImage(cxPtr, 2)) { cxWhy = 3; break; }
        uint16_t cxJt   = (uint16_t)(cxPtr - cxPlan.img16(cxPtr));
        if (!cxPlan.inImage((uint16_t)(cxJt - 8), 9)) { cxWhy = 3; break; }
        if (cxPlan.image[(uint16_t)(cxJt - cxPlan.segbot)] == 0) { cxWhy = 2; break; } // assembly procedure: stays Z80
        uint16_t cxDs = cxPlan.img16((uint16_t)(cxJt - 8));
        uint16_t cxPs = cxPlan.img16((uint16_t)(cxJt - 6));
        uint16_t cxSpF = (uint16_t)(cxPlan.dBot - cxDs - cxPs - 12);       // dBot: SP after the load (see NativeSegLoad.inc)
        if ((uint16_t)(cxSpF - 62) < cxRd(PM_V(0x0240))) { cxWhy = 1; break; } // STKOVR: stays Z80

        // ---- commit ----
        cxWr(PM_V(0x0246), cxIpc1);                                    // CXP $10: SAVIPC
        slCommit(cxPlan);                                         // SP := SEGBOT, segment loaded + relocated
        cxNewSeg = cxPlan.newseg;
        // BLD3: A := proc_num; INC BC; SAVIPC
        uint16_t cxBc2 = cxIpc2;
        PM_CPU.r.setBC(cxBc2);
        cxWr(PM_V(0x0246), cxBc2);
        uint16_t tAddr = (uint16_t)(cxNewSeg - (uint16_t)(cxProc * 2));
        uint16_t jtab  = (uint16_t)(tAddr - PM_CODE16(tAddr));
        cxWr(PM_V(0x02CC), jtab);                                       // NEWJTB
        uint16_t datasz = PM_CODE16((uint16_t)(jtab - 8));
        uint16_t parmsz = PM_CODE16((uint16_t)(jtab - 6));
        uint16_t sp = (uint16_t)(PM_CPU.r.SP - datasz - parmsz); // $50: extend by datasz+parmsz
        uint16_t src = cxPlan.dTop;                               // params sit just above the segment (Harvard: its pool)
        for (uint16_t i = 0; i < parmsz; i++) PM_MEM[(uint16_t)(sp + i)] = PM_MEM[(uint16_t)(src + i)];
        PM_CPU.r.SP = sp;
        PushStackWord((uint16_t)(src + parmsz));                  // mssp
        PushStackWord(cxBc2);                                     // msipc
        PushStackWord(segp);                                      // msseg (caller)
        PushStackWord(cxRd(PM_V(0x02F4)));                              // msjtab
        PushStackWord(cxOldMP);                                   // msdyn
        PushStackWord(cxOldMP);                                   // msstat
        cxNewMP = PM_CPU.r.SP;
        cxWr(PM_V(0x02F2), cxNewMP);                                    // MP
        cxWr(PM_V(0x0242), (uint16_t)(cxNewMP + 10));                   // MPD0
        cxWr(PM_V(0x02F6), cxNewSeg);                                   // SEGP
        cxWr(PM_V(0x02F4), jtab);                                       // JTAB
        uint16_t eRef = (uint16_t)(jtab - 2), eSval = PM_CODE16(eRef);
        PM_CPU.r.setBC((uint16_t)(eRef - eSval));                 // entry point
        if (PM_PRESERVE) {
            uint8_t carry = ((eRef & 0xFF) < (eSval & 0xFF)) ? 1 : 0;
            PM_CPU.r.A = (uint8_t)((eRef >> 8) - (eSval >> 8) - carry);
            PM_CPU.r.setDE(eSval);
        }
        PM_COUNT(OP_CXP);
        goto CXP_POST;
    }
    { // resident / seg-0 path in its own scope so `goto cxPost` above skips it cleanly
    if (segNum == 0) {
        cxNewSeg = cxRd(PM_V(0x02F8));                  // MEMTOP
    } else {
        cxNewSeg = cxRd((uint16_t)(cxRefAddr + 2));
    }

    // Precheck exactly what NativeBldmscw will do with this NEWSEG.
    uint16_t cxBc1   = cxIpc1;                    // IPC past seg_num (CXP: points at proc_num)
    uint8_t  cxProc  = cxProcN;
    uint16_t cxTAddr = (uint16_t)(cxNewSeg - (uint16_t)(cxProc * 2));
    uint16_t cxJtab  = (uint16_t)(cxTAddr - PM_CODE16(cxTAddr));
    if (PM_CODE8(cxJtab) == 0) { cxWhy = 2; break; } // assembly procedure: stays Z80
    uint16_t cxDatasz = PM_CODE16((uint16_t)(cxJtab - 8));
    if ((uint16_t)(PM_CPU.r.SP - cxDatasz - 12 - 60) < cxRd(PM_V(0x0240))) { cxWhy = 1; break; } // STKOVR: stays Z80

    // ---- commit ----
    PM_CPU.r.setBC(cxBc1);
    if (segNum != 0) {
        cxWr(PM_V(0x0246), cxBc1);                      // SAVIPC (CXP $10)
        PM_MEM[PM_V(0x02C6)] = segNum;                  // LD (SEGNUM),A in GETSEG
    }
    cxWr(cxRefAddr, (uint16_t)(cxRd(cxRefAddr) + 1)); // refcount++
    cxWr(PM_V(0x02F6), cxNewSeg);                       // SEGP := NEWSEG so NativeBldmscw builds with it
    NativeBldmscwProc(cxProc, cxIpc2);            // cannot fail: both failure cases prechecked above
    cxNewMP = cxRd(PM_V(0x02F2));
    cxWr((uint16_t)(cxNewMP + 6), segp);          // msseg = caller's segment, as BLDXNL pushes it
    PM_COUNT(OP_CXP);
    }
  CXP_POST:
    {
    if (PM_HARVARD && segNum == 0 && cxProcN == 2) PM_ERR_SHADOW(cxNewMP);   // EXECERROR: location shadow (SetHarvard)

    uint16_t cxEntry = PM_CPU.r.BC();
    uint8_t  cxLex   = PM_CODE8((uint16_t)(cxRd(PM_V(0x02F4)) + 1));
    uint8_t  cxLexM1 = (uint8_t)(cxLex - 1);        // CIPXNL: LD A,(lex); DEC A; JP P,$10
    bool cxBaseStyle = (segNum == 0) || (cxLexM1 & 0x80) != 0; // CBPXNL directly, or CIPXNL's base-level branch
    if (cxBaseStyle) {
        uint16_t oldBASE = cxRd(PM_V(0x02F0));
        uint16_t newMPD0 = cxRd(PM_V(0x0242));
        PushStackWord(oldBASE);
        cxWr(PM_V(0x0244), newMPD0);                    // BASED0
        cxWr(PM_V(0x02F0), cxNewMP);                    // BASE
        cxWr(cxNewMP, cxRd(oldBASE));             // msstat := old base's stat link
        PM_CPU.r.setBC(cxEntry);
        if (PM_PRESERVE) {
            if (segNum != 0) PM_CPU.r.A = cxLexM1;    // CIPXNL's DEC A survives CBPXNL (seg 0 skips CIPXNL)
            PM_CPU.r.setHL((uint16_t)(cxNewMP + 1)); PM_CPU.r.setDE((uint16_t)(oldBASE + 1));
        }
    } else {
        uint8_t  target  = cxLexM1;
        uint16_t examine = cxNewMP, result = 0;
        bool found = false;
        for (int iter = 0; iter < 30000; iter++) {   // the DYNAMIC chain: < 5500 frames fit in 64K
            uint16_t fJtab = cxRd((uint16_t)(examine + 4));
            uint16_t fDyn  = cxRd((uint16_t)(examine + 2));
            result = fDyn;
            if (PM_CODE8((uint16_t)(fJtab + 1)) == target) { found = true; break; }
            examine = fDyn;
        }
        if (!found) {                             // (not reachable: the search covers any possible chain)
            // Continue in CIPXNL's $10 loop with the state it expects: the IPC
            // it pushed (PUSH BC at 136C) and A = the target lex level.
            PushStackWord(cxEntry);
            PM_CPU.r.A = target;
            PM_CPU.r.setBC(examine);
            PM_CPU.r.PC = 0x137F;
            break;
        }
        cxWr(cxNewMP, result);                    // replace msstat
        if (PM_PRESERVE) { PM_CPU.r.A = target; PM_CPU.r.setDE(cxEntry); PM_CPU.r.setHL(cxOldMP); }
        PM_CPU.r.setBC(cxEntry);
    }
    commit = true;
    }
} while (0);
