// NativeMount.inc -- CSP 140 MOUNT and CSP 141 IMAGENAME: a unit's disk image changed by a
// (and CSP 142 VOLADDR, 143 VOLINFO: the volumes there are to MOUNT, version 2.18)
// program (version 2.14), so that a makefile can bring each source volume on line in turn.
// The Z80 interpreter has no such routines; in Z80 mode the engine's coprocessor runs this
// same code for them (SetZ80Coprocessor).  Included in PSystemEngine.cpp's CSP case, after
// NativeUnitSize.inc.
//
//   140 MOUNT      I unit, A name -> I result
//       name: a NUL-terminated string at that byte address -- an image's name, NAME or
//       NAME.BLK (letters, digits, . _ - only: no folder), looked for in the folder of the
//       image on that unit now (else the boot disk's): unit (5, 9..14; not 4, the boot
//       disk) gets that image, as File > Open Unit would do it; the old image's file keeps
//       what was written to it (every write goes through at once).  "" takes the image
//       off (as File > Close Unit).  result: the new image's blocks (0 for ""), or -1 no
//       such image, -2 a unit that cannot be changed, -3 a bad name
//       (PSystemEngine::MountByName).  The P-System's own idea of the unit's volume is the
//       program's to refresh: UCSD-C's MOUNT does it.
//   141 IMAGENAME  I unit, A buf, I size -> I length
//       the name of the image file on unit (no folder: U132.A_PASCAL_COMPILER_SOURCE.BLK)
//       into buf, NUL-terminated, at most size - 1 characters; its length, -1 with no image.
//   142 VOLADDR    I index -> L address  (version 2.18)
//       the index-th volume a program can MOUNT (0, 1, ...; PSystemEngine::VolumeAt): on the
//       RP2040 port the volumes of its flash catalog, and this is the volume's flash address
//       (0x10000000 + its offset); in the emulator the .BLK files in the boot disk's folder by
//       name, and this is its number + 1 (a PC has no flash).  0: no such volume.  As the
//       doubles' longs: two words, the low one on top.
//   143 VOLINFO    I index, A rec -> I found  (version 2.18)
//       rec (32 bytes): the volume's name for MOUNT, NUL-terminated, in bytes 0..23; its
//       blocks (word 12); the unit it is on now, 0 none (word 13); flags, 1 read-only and
//       2 persistent (word 14); word 15 0.  found: 1, or 0 no such volume (rec unchanged).
if (procNum == 142 || procNum == 143) {
    VolumeEntry vi;
    if (procNum == 142) {
        const int viIndex = (int16_t)PopStackWord();
        const uint32_t viAddr = VolumeAt(viIndex, vi) ? vi.addr : 0;
        PushStackWord((uint16_t)(viAddr >> 16));
        PushStackWord((uint16_t)viAddr);
    } else {
        const uint16_t viRec = PopStackWord();
        const int viIndex = (int16_t)PopStackWord();
        const bool viFound = VolumeAt(viIndex, vi);
        if (viFound) {
            for (int i = 0; i < 32; i++) m_mem[(uint16_t)(viRec + i)] = 0;
            for (size_t i = 0; i < vi.name.size() && i < 23; i++) m_mem[(uint16_t)(viRec + i)] = (uint8_t)vi.name[i];
            const uint16_t blocks = vi.blocks > 65535 ? 65535 : (uint16_t)vi.blocks;
            m_mem[(uint16_t)(viRec + 24)] = (uint8_t)blocks; m_mem[(uint16_t)(viRec + 25)] = (uint8_t)(blocks >> 8);
            m_mem[(uint16_t)(viRec + 26)] = vi.unit;
            m_mem[(uint16_t)(viRec + 28)] = vi.flags;
        }
        PushStackWord(viFound ? 1 : 0);
    }
    m_cpu.r.setBC((uint16_t)(m_cpu.r.BC() + 1));
    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF;
    m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8;
    m_cpu.r.PC = 0x03A4;                                // BACK1, as the doubles exit
    m_opcodeEmulatedCount[OP_CSP]++;
    break;
}
if (procNum == 140 || procNum == 141) {
    int mountResult;
    if (procNum == 140) {
        const uint16_t mnAddr = PopStackWord();
        const uint8_t mnUnit = (uint8_t)PopStackWord();
        std::string mnName;
        for (uint16_t a = mnAddr; m_mem[a] && mnName.size() < 80; a++)
            mnName += (char)m_mem[a];
        mountResult = MountByName(mnUnit, mnName);
    } else {
        const int inSize = (int16_t)PopStackWord();
        const uint16_t inAddr = PopStackWord();
        const uint8_t inUnit = (uint8_t)PopStackWord();
        const std::string inName = ImageFileName(inUnit);
        mountResult = -1;
        if (!inName.empty() && inSize > 0) {
            mountResult = (int)inName.size() < inSize - 1 ? (int)inName.size() : inSize - 1;   // (not std::min: windows.h's min macro)
            for (int i = 0; i < mountResult; i++)
                m_mem[(uint16_t)(inAddr + i)] = (uint8_t)inName[i];
            m_mem[(uint16_t)(inAddr + mountResult)] = 0;
        }
    }
    PushStackWord((uint16_t)mountResult);
    m_cpu.r.setBC((uint16_t)(m_cpu.r.BC() + 1));
    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF;
    m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8;
    m_cpu.r.PC = 0x03A4;                                // BACK1, as the doubles exit
    m_opcodeEmulatedCount[OP_CSP]++;
    break;
}
