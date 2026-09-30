// NativeSegReturn.inc -- shared by PSystemEngine.cpp and the linux-harness
// copies (harness.cpp, remove_file.cpp); included inside RNP and RBP.
// Host must #define PM_MEM (the 64K memory array). Expects in scope:
// curSegP (SEGP before the return) and newSegCandidate (caller's msseg).
// Produces segRetA: the value the real code leaves in A at BACK.
//
// Real code (RNP $20..$40): compare the popped msseg with SEGP. Same
// segment -> A = SEGP's high byte (from "LD A,(SEGP+1);CP H"). Different
// segment -> $30: DECREF the segment being LEFT (seg # = first byte of its
// proc table, "LD HL,(SEGP);LD A,(HL)"), which leaves A = C|B of the new
// refcount ("LD A,C;OR B"). Nothing else differs: SEGP is then set to the
// caller's segment exactly as in the same-segment case.
uint8_t segRetA = (uint8_t)(curSegP >> 8);
if (newSegCandidate != curSegP) {
    uint8_t  drSeg = PM_MEM[curSegP];
    uint16_t drHi  = (uint16_t)(PM_V(0x0251) + (uint16_t)drSeg * 4);            // LD BC,INTSEGT+1; ADD HL,BC
    uint16_t drLo  = (uint16_t)((drHi & 0xFF00) | ((drHi - 1) & 0x00FF)); // DEC L (8-bit, as the real code)
    uint16_t drCnt = (uint16_t)(((PM_MEM[drHi] << 8) | PM_MEM[drLo]) - 1); // DEC BC
    PM_MEM[drLo] = (uint8_t)(drCnt & 0xFF);
    PM_MEM[drHi] = (uint8_t)(drCnt >> 8);
    segRetA = (uint8_t)((drCnt & 0xFF) | (drCnt >> 8));
}
