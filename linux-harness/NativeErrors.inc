// NativeErrors.inc -- shared by PSystemEngine.cpp and the linux-harness copies;
// included in each engine's run loop just before the native opcode dispatch.
// When PC is at one of the Z80 interpreter's run-time error labels (reached
// by a native case that reproduced the pre-error state, by an undefined
// opcode's table entry, or by Z80 code), do what the Z80 does from there:
//   NOTIMP, HLT: SAVIPC first.  STKOVR: NP := INTEND (prevent a recursive
//   overflow).  XEQERR: XERRCD := error number; BOMBP := SP - 14 (where
//   EXECERROR's MSCW will be); BOMIPC := IPCSAV; IPC := the "CXP 0,2"
//   instruction; BACK -- the native CXP then calls PROCEDURE EXECERROR.
// Only with register compatibility off (host: PM_NATIVE_ERRORS). Host also
// provides PM_CXP02 (address of the CXP 0,2 bytes) and PM_INTEND.
if (PM_NATIVE_ERRORS) {
    const int neCode = NativeErrorCode(PM_CPU.r.PC);
    if (neCode >= 0) {
        if (neCode == 11 || neCode == 14) {                     // NOTIMP, HLT: SAVIPC
            PM_MEM[PM_V(0x0246)] = (uint8_t)PM_CPU.r.BC(); PM_MEM[PM_V(0x0247)] = (uint8_t)(PM_CPU.r.BC() >> 8);
        }
        if (neCode == 4) {                                        // STKOVR: NP := INTEND
            const uint16_t ne = PM_INTEND;
            PM_MEM[PM_V(0x0240)] = (uint8_t)ne; PM_MEM[PM_V(0x0241)] = (uint8_t)(ne >> 8);
        }
        PM_MEM[PM_V(0x02E6)] = (uint8_t)neCode; PM_MEM[PM_V(0x02E7)] = 0;     // XERRCD
        const uint16_t neBombp = (uint16_t)(PM_CPU.r.SP - 14);
        PM_MEM[PM_V(0x02EE)] = (uint8_t)neBombp; PM_MEM[PM_V(0x02EF)] = (uint8_t)(neBombp >> 8); // BOMBP
        PM_MEM[PM_V(0x02FA)] = PM_MEM[PM_V(0x0246)]; PM_MEM[PM_V(0x02FB)] = PM_MEM[PM_V(0x0247)];            // BOMIPC := IPCSAV
        PM_CPU.r.setBC(PM_CXP02);
        PM_CPU.r.PC = 0x03B0;                                     // BACK: dispatches CXP 0,2
    }
}
