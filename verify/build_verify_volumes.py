#!/usr/bin/env python3
"""
Build the two source volumes for "Verify P-System" from the UCSD II.0
distribution disk images (BLK_format/ of the distribution archive, v1.03+).

  python3 build_verify_volumes.py <BLK_format dir> <output dir>

  SOURCE.BLK  -> mounted as unit #5, volume name SOURCE
      OS (SYSTEM.TEXT + includes), Filer (II.0.FILER.TEXT + includes),
      Screen editor (EDITOR.TEXT + includes), Z80 P-code interpreter
      (CPMINC.TEXT + includes). The volume must be named SOURCE and be on
      unit 5: the editor includes SOURCE:HEAD etc. and the OS includes
      #5:GLOBALS etc. All compiled/assembled output files go here too.
  COMPASM.BLK -> mounted as unit #9, volume name COMPASM
      Pascal compiler (COMPILER.TEXT + includes), Z80 assembler
      (ASMZ80.TEXT + includes) and L2. (YALOE.TEXT is a segment meant to be
      included in a larger program; it cannot be compiled on its own.)
All 67 source files do not fit under the 60-file limit on one volume
(UCSD allows 77), so they are split in two. Files are copied with their
exact on-disk bytes (a .TEXT file keeps its UCSD format).
"""
import os, sys
D = 1024
def w(b, o): return b[o] | (b[o + 1] << 8)
def put(b, o, v): b[o] = v & 0xFF; b[o + 1] = (v >> 8) & 0xFF
def entries(img):
    for i in range(1, w(img, D + 16) + 1):
        o = D + 26 * i
        yield dict(first=w(img, o), last=w(img, o + 2), kindw=w(img, o + 4), name=bytes(img[o + 7:o + 7 + img[o + 6]]).decode('latin1'),
                   lastbyte=w(img, o + 22), date=w(img, o + 24))
def extract(img, name):
    for e in entries(img):
        if e['name'] == name: return bytes(img[e['first'] * 512:e['last'] * 512]), e
    raise SystemExit(f"missing {name}")
def new_volume(template, volname):
    img = bytearray(open(template, 'rb').read())
    n = w(img, D + 16); img[D + 26:D + 26 * (n + 1)] = bytes(26 * n); put(img, D + 16, 0)   # empty directory
    img[D + 6] = len(volname); img[D + 7:D + 14] = volname.ljust(7).encode()
    return img
def add(img, name, data, e):
    n = w(img, D + 16); assert n < 77
    first = w(img, D + 26 * n + 2) if n else w(img, D + 2)        # after the last file (directory kept in block order)
    nb = len(data) // 512
    assert first + nb <= w(img, D + 14), "volume full"
    o = D + 26 * (n + 1); img[o:o + 26] = bytes(26)
    put(img, o, first); put(img, o + 2, first + nb); put(img, o + 4, e['kindw'])
    img[o + 6] = len(name); img[o + 7:o + 7 + len(name)] = name.encode(); put(img, o + 22, e['lastbyte']); put(img, o + 24, e['date'])
    put(img, D + 16, n + 1)
    img[first * 512:(first + nb) * 512] = data

SETS = {
 'SOURCE': [('U134.4_OS_SOURCE', ['SYSTEM.TEXT', 'GLOBALS.TEXT', 'SYSSEGS.A.TEXT', 'SYSSEGS.B.TEXT', 'SYSTEM.A.TEXT', 'SYSTEM.B.TEXT', 'SYSTEM.C.TEXT',
                                  'II.0.FILER.TEXT', 'FILER.VARS.TEXT', 'FILER.A.TEXT', 'FILER.B.TEXT', 'FILER.C.TEXT', 'FILER.D.TEXT', 'FILER.E.TEXT']),
            ('U126_EDITOR_SOURCE', ['EDITOR.TEXT', 'HEAD.TEXT', 'INIT.TEXT', 'OUT.TEXT', 'COPYFILE.TEXT', 'ENVIRON.TEXT', 'PUTSYNTAX.TEXT',
                                    'COMMAND.TEXT', 'INSERTIT.TEXT', 'MOVEIT.TEXT', 'FIND.TEXT', 'USER.TEXT', 'MISC.TEXT', 'UTIL.TEXT']),
            ('U120_Z80_P-CODE_SOURCE', ['CPMINC.TEXT', 'INTERP.TEXT', 'VARS.TEXT', 'ARITH.TEXT', 'SET1.TEXT', 'SET2.TEXT', 'FPL.TEXT', 'FPI.TEXT',
                                        'FPT.TEXT', 'NOFPT.TEXT', 'PROC1.TEXT', 'PROC2.TEXT', 'STP.TEXT', 'CPMIO.TEXT', 'BOOT.TEXT'])],
 'COMPASM': [('U132.A_PASCAL_COMPILER_SOURCE', ['COMPILER.TEXT', 'COMPGLBLS.TEXT', 'COMPINIT.TEXT', 'DECPART.A.TEXT', 'DECPART.B.TEXT', 'DECPART.C.TEXT',
                                                'BODYPART.A.TEXT', 'BODYPART.B.TEXT', 'BODYPART.C.TEXT', 'BODYPART.D.TEXT', 'BODYPART.E.TEXT',
                                                'UNITPART.TEXT', 'PROCS.A.TEXT', 'PROCS.B.TEXT', 'BLOCK.TEXT']),
             ('U123.1_Z80_ASSEM_SOURCE', ['ASMZ80.TEXT', 'ASM1.TEXT', 'ASM2.TEXT', 'ASM3.TEXT', 'EXTRA.Z80.TEXT', 'ASM4.TEXT', 'ASM5.TEXT', 'ASM6.TEXT']),
             ('U128_L2_YALOE_SOURCE', ['L2.TEXT'])],
}
if __name__ == '__main__':
    src, out = sys.argv[1], sys.argv[2]
    tmpl = os.path.join(src, 'Empty_Big_Disk.BLK')
    for vol, groups in SETS.items():
        img = new_volume(tmpl, vol)
        for disk, names in groups:
            dimg = bytearray(open(os.path.join(src, disk + '.BLK'), 'rb').read())
            for nm in names:
                data, e = extract(dimg, nm); add(img, nm, data, e)
        path = os.path.join(out, vol + '.BLK'); open(path, 'wb').write(img)
        n = w(img, D + 16)
        print(f"{path}: volume {vol}, {n} files, {sum(e['last'] - e['first'] for e in entries(img))} blocks")
