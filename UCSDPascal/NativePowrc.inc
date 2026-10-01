// NativePowrc.inc -- shared by PSystemEngine.cpp and the linux-harness copies;
// included at the top of CEQU, CNEQ, CLEQ and CGEQ. Set comparisons (type
// POWRC = 8), register compatibility off. Mirrors the Z80 POWRC code
// (PCSETUP, PCEQSN, PCLEQ, PCGEQ, ZERCHKA/B, PCRSLT):
//   stack: szb, set_b, sza, set_a, rest.  Cut back to just past set_a (NEWSP)
//   and push 0 or 1 (LD B,0; PUSH BC).
//   =  / <> : common words equal, and the longer set's extra words all zero
//   <=      : no bit of set_a outside set_b; set_a's extra words zero
//   >=      : no bit of set_b outside set_a; set_b's extra words zero
// ALEQB (sza <= szb) and NEWSP are written as the real code writes them.
if (!PM_PRESERVE && PM_CODE8(PM_CPU.r.BC()) == 8) {
    const uint8_t pcOp = (uint8_t)nativeOpcodeLookup;
    const uint16_t pcIpc = (uint16_t)(PM_CPU.r.BC() + 1);
    const uint16_t pcSp = PM_CPU.r.SP;
    const uint8_t pcSzb = PM_MEM[pcSp];
    const uint16_t pcSetB = (uint16_t)(pcSp + 2);
    const uint16_t pcSzaAddr = (uint16_t)(pcSp + 2 + 2 * pcSzb);
    const uint8_t pcSza = PM_MEM[pcSzaAddr];
    const uint16_t pcSetA = (uint16_t)(pcSzaAddr + 2);
    const uint16_t pcNewsp = (uint16_t)(pcSetA + 2 * pcSza);
    const bool pcAleqb = pcSzb >= pcSza;
    const uint8_t pcMin = pcAleqb ? pcSza : pcSzb;
    auto pcZero = [&](uint16_t at, int words) {
        for (int k = 0; k < 2 * words; k++) if (PM_MEM[(uint16_t)(at + k)]) return false;
        return true;
    };
    bool pcResult = true;
    for (int k = 0; k < 2 * pcMin && pcResult; k++) {
        uint8_t a = PM_MEM[(uint16_t)(pcSetA + k)], b = PM_MEM[(uint16_t)(pcSetB + k)];
        if (pcOp == OP_CEQU || pcOp == OP_CNEQ) pcResult = (a == b);
        else if (pcOp == OP_CLEQ) pcResult = (a & (uint8_t)~b) == 0;
        else pcResult = (b & (uint8_t)~a) == 0;                            // CGEQ
    }
    if (pcResult) {
        const uint16_t aRest = (uint16_t)(pcSetA + 2 * pcMin), bRest = (uint16_t)(pcSetB + 2 * pcMin);
        if (pcOp == OP_CEQU || pcOp == OP_CNEQ)
            pcResult = pcAleqb ? pcZero(bRest, pcSzb - pcSza) : pcZero(aRest, pcSza - pcSzb);
        else if (pcOp == OP_CLEQ)
            pcResult = pcAleqb ? true : pcZero(aRest, pcSza - pcSzb);
        else
            pcResult = pcAleqb ? pcZero(bRest, pcSzb - pcSza) : true;
    }
    if (pcOp == OP_CNEQ) pcResult = !pcResult;
    PM_MEM[PM_V(0x02A0)] = pcAleqb ? 1 : 0;                                       // ALEQB
    PM_MEM[PM_V(0x024C)] = (uint8_t)pcNewsp; PM_MEM[PM_V(0x024D)] = (uint8_t)(pcNewsp >> 8); // NEWSP
    PM_MEM[PM_V(0x0246)] = (uint8_t)pcIpc; PM_MEM[PM_V(0x0247)] = (uint8_t)(pcIpc >> 8);     // CSETUP's SAVIPC
    PM_CPU.r.SP = pcNewsp;
    PushStackWord(pcResult ? 1 : 0);
    PM_CPU.r.setBC(pcIpc);
    commit = true;
    PM_COUNT(pcOp);
    break;
}
