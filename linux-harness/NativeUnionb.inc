// NativeUnionb.inc -- shared by PSystemEngine.cpp and the linux-harness copies;
// included in UNI for the "Unionb" case (szb > sza), register compatibility
// off. Mirrors the Z80 UNI $30 path: OR set_a into set_b, move set_b up so
// it ends just past set_a, and push the size word szb (the real code sets
// C := szb and pushes BC after the move loop has counted B down to 0).
// Expects in scope: sp
// (SP at entry, the szb word), szb, sza, szaAddr (address of the sza word).
{
    uint16_t ubIpc = PM_CPU.r.BC();
    PM_MEM[PM_V(0x0246)] = (uint8_t)ubIpc; PM_MEM[PM_V(0x0247)] = (uint8_t)(ubIpc >> 8);   // SETUP's SAVIPC
    PM_MEM[PM_V(0x024C)] = (uint8_t)szaAddr; PM_MEM[PM_V(0x024D)] = (uint8_t)(szaAddr >> 8); // NEWSP
    uint16_t ubB = (uint16_t)(sp + 2), ubA = (uint16_t)(szaAddr + 2);
    for (uint16_t k = 0; k < (uint16_t)(2 * sza); k++)                       // union set_a into set_b
        PM_MEM[(uint16_t)(ubB + k)] |= PM_MEM[(uint16_t)(ubA + k)];
    uint16_t ubSrc = szaAddr, ubDst = (uint16_t)(ubA + 2 * sza);            // just past set_b / set_a
    for (uint16_t k = 0; k < (uint16_t)(2 * szb); k++) {                    // move down-to-up, byte by byte
        ubSrc = (uint16_t)(ubSrc - 1); ubDst = (uint16_t)(ubDst - 1);
        PM_MEM[ubDst] = PM_MEM[ubSrc];
    }
    PM_CPU.r.SP = ubDst;
    PushStackWord((uint16_t)szb);
    commit = true;
    PM_COUNT(OP_UNI);
}
