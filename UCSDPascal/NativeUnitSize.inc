// NativeUnitSize.inc -- CSP 139, UNITSIZE: the size of a unit's disk image
// (version 2.09). The Z80 interpreter has no such routine; in Z80 mode the
// engine's coprocessor runs this same code for it (SetZ80Coprocessor).
// Included in PSystemEngine.cpp's CSP case, after NativeCalli.inc.
//
//   139 UNITSIZE  I unit, I blocks -> I size
//       blocks 1..32767: the image of unit (4, 5, 9..15) becomes that many
//       blocks -- longer (zeros at its end) or shorter (the blocks past it
//       gone) -- and the file on the PC with it (PSystemEngine::UnitImageSize).
//       blocks 0: no change. size: the image's blocks after it; -1 with no
//       image on that unit, a bad size, or a file that cannot be changed.
//       Nothing else is touched: the volume's own size (its directory's
//       DEOVBLK) is the program's to write. UCSD-C's GROW and SHRINK of a
//       volume use it (UCSD-C ucsdc/lib/subvol.c).
if (procNum == 139) {
    const int usBlocks = (int16_t)PopStackWord();
    const uint8_t usUnit = (uint8_t)PopStackWord();
    PushStackWord((uint16_t)UnitImageSize(usUnit, usBlocks));
    m_cpu.r.setBC((uint16_t)(m_cpu.r.BC() + 1));
    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF;
    m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8;
    m_cpu.r.PC = 0x03A4;                                // BACK1, as the doubles exit
    m_opcodeEmulatedCount[OP_CSP]++;
    break;
}
