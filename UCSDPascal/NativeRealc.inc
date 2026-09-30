// NativeRealc.inc -- shared by PSystemEngine.cpp and the linux-harness copies;
// included at the top of CEQU, CNEQ, CLEQ, CGEQ, CLSS and CGTR. REAL
// comparisons (type REALC = 2), register compatibility off: pops both reals
// and pushes 1 or 0 exactly as the Z80 REALC routine and the comparison
// routines do (PmRealCompare, PCodeOpcodes.h -- checked against the real Z80
// routines by linux-harness/fptest.cpp).
if (!PM_PRESERVE && PM_MEM[PM_CPU.r.BC()] == 2) {
    const uint8_t rcOp = (uint8_t)nativeOpcodeLookup;
    const uint16_t rcIpc = (uint16_t)(PM_CPU.r.BC() + 1);      // past the type byte (CSETUP's SAVIPC)
    const uint16_t rcSp = PM_CPU.r.SP;
    uint8_t rcA[4], rcB[4];
    for (int k = 0; k < 4; k++) { rcB[k] = PM_MEM[(uint16_t)(rcSp + k)]; rcA[k] = PM_MEM[(uint16_t)(rcSp + 4 + k)]; }
    const bool rcResult = PmRealCompare(rcOp, rcA, rcB);
    const uint16_t rcRet = PmRealcRetadr(rcOp);
    PM_MEM[PM_V(0x024A)] = (uint8_t)rcRet; PM_MEM[(uint16_t)(PM_V(0x024A) + 1)] = (uint8_t)(rcRet >> 8); // RETADR
    PM_MEM[PM_V(0x0246)] = (uint8_t)rcIpc; PM_MEM[(uint16_t)(PM_V(0x0246) + 1)] = (uint8_t)(rcIpc >> 8); // IPCSAV
    PM_CPU.r.SP = (uint16_t)(rcSp + 8);                         // both reals popped...
    PushStackWord(rcResult ? 1 : 0);                            // ...PSHTRU1 / PSHFLS1
    PM_CPU.r.setBC(rcIpc);
    commit = true;
    PM_COUNT(rcOp);
    break;
}
