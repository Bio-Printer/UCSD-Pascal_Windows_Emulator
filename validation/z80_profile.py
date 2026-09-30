#!/usr/bin/env python3
"""Where does Z80 code still run in native mode (register compatibility off)?

Builds a copy of the REAL engine (../UCSDPascal/PSystemEngine.cpp, via
../verify/linux-shim) with a Z80 instruction counter added (in /tmp; the
source is not modified), runs the full Verify P-System script natively
(the system rebuilding itself: ~48.5 million P-code instructions), and
reports every Z80 instruction executed, grouped by purpose. Routine names
come from system_micro_symbols.txt (the SYSTEM.MICRO listing).

    python3 z80_profile.py
"""
import os, re, struct, subprocess, bisect, collections, sys
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.join(HERE, '..')
UCSD = os.path.join(ROOT, 'UCSDPascal'); VER = os.path.join(ROOT, 'verify')
syms = [(int(l[:4], 16), l[5:].strip()) for l in open(os.path.join(HERE, 'system_micro_symbols.txt')) if l[:1] != '#']
A = [a for a, _ in syms]
def name(pc):
    if pc >= 0xF000: return "pascal.bin"
    i = bisect.bisect_right(A, pc) - 1
    return syms[i][1] if i >= 0 else "?"
IO = {'CALLIO','SYSIO','GETU','UWAIT','UBUSY','DSK0','DR0DRVR','BDIR','BOMIT','CBIS','CBOS','SYSTLE','CHCLR','CH01','ECHO','BOOT','CBOOT','UREAD','UWRITE','UCLEAR','SYSRD','SYSWR','IOINIT'}
def group(n, pc):
    if pc >= 0xF000: return "boot loader + BIOS (pascal.bin)"
    if n in ('FOUND', 'STEP5', 'FSTENT', 'NRPTOP'): return "interpreter start-up"
    if n in IO or 0x1AE9 <= pc < 0x2C00: return "unit I/O + CP/M I/O drivers (console, disk)"
    if n in ('UNI', 'SETUP'): return "set union 'Unionb' + SETUP"
    if n in ('UJP', 'FJP', 'XJP'): return "jumps"
    if n in ('BLD4','BLDMSCW','CBPXNL','CIPXNL','STKCHK','BLD3','CXP','CIP','CLP','CGP','CBP','RNP','RBP','GETSEG','READSEG','GSGREAD','RLSEG','RLLIST'):
        return "procedure call/return fall-backs"
    return "other p-code sub-paths"

s = open(os.path.join(UCSD, 'PSystemEngine.cpp')).read()
o = "        m_cpu.step();\n        if (m_verifyFile && m_cpu.r.SP < m_verifyMinSP) m_verifyMinSP = m_cpu.r.SP;"
assert s.count(o) == 1, "step site changed -- update z80_profile.py"
s = s.replace(o, "        { extern unsigned long long g_z80hist[65536]; g_z80hist[m_cpu.r.PC]++; }\n" + o)
# count every CSP call and those that fall back to the Z80 routine (a native
# path that silently stopped being used shows up here)
o2 = '#include "NativeCsp.inc"'
assert s.count(o2) == 1
s = s.replace(o2, '{ extern unsigned long long g_cspSeen[64]; g_cspSeen[procNum & 63]++; }\n' + o2)
o3 = 'uint16_t entryAddr = (uint16_t)(0x15E7 + procNum * 2);'
assert s.count(o3) == 1
s = s.replace(o3, '{ extern unsigned long long g_cspZ80[64]; g_cspZ80[procNum & 63]++; }\n' + o3)
open('/tmp/z80prof_engine.cpp', 'w').write('#include <cstdint>\nunsigned long long g_z80hist[65536], g_cspSeen[64], g_cspZ80[64];\n#include "PSystemEngine.h"\n' + s)
r = open(os.path.join(VER, 'run_verify.cpp')).read()
o = "    e.StopVerifyLog();\n    e.Stop(); t.join();"
assert r.count(o) == 1
r = r.replace(o, o + '\n    { extern unsigned long long g_z80hist[65536], g_cspSeen[64], g_cspZ80[64]; FILE* hf = fopen("/tmp/z80prof.bin", "wb"); fwrite(g_z80hist, 8, 65536, hf); fwrite(g_cspSeen, 8, 64, hf); fwrite(g_cspZ80, 8, 64, hf); fclose(hf); }')
open('/tmp/z80prof_run.cpp', 'w').write(r)
subprocess.run(['g++', '-std=c++17', '-O2', '-I', os.path.join(VER, 'linux-shim'), '-I', UCSD, '-o', '/tmp/z80prof',
                '/tmp/z80prof_run.cpp', '/tmp/z80prof_engine.cpp', os.path.join(UCSD, 'z80.cpp'), '-lpthread'], check=True)
out = subprocess.run(['/tmp/z80prof', os.path.join(ROOT, 'data'), os.path.join(VER, 'SOURCE.BLK'), os.path.join(VER, 'COMPASM.BLK'),
                      os.path.join(VER, 'VERIFY.SCRIPT'), 'native', '/tmp/z80prof_work', '', '900'], capture_output=True, text=True).stdout
print(out.strip().split('\n')[-1])
raw = open('/tmp/z80prof.bin', 'rb').read()
H = struct.unpack('<65536Q', raw[:65536 * 8])
CS = struct.unpack('<64Q', raw[65536 * 8:65536 * 8 + 512]); CZ = struct.unpack('<64Q', raw[65536 * 8 + 512:65536 * 8 + 1024])
g = collections.Counter(); rt = collections.Counter()
for pc in range(65536):
    if H[pc]: n = name(pc); g[group(n, pc)] += H[pc]; rt[n] += H[pc]
tot = sum(g.values())
print(f"\nZ80 instructions executed in native mode over the whole Verify run: {tot:,}")
for k, v in g.most_common(): print(f"   {v:12,d}  {100*v/max(tot,1):5.1f}%  {k}")
print("\ntop routines:")
for k, v in rt.most_common(20): print(f"   {v:12,d}  {k}")
CSPN = {0:'IOC',1:'NEW',2:'MVL',3:'MVR',4:'EXIT',5:'UREAD',6:'UWRITE',7:'IDS',8:'TRS',9:'TIM',10:'FLC',11:'SCN',21:'GSEG',22:'RSEG',
        23:'TNC',24:'RND',25:'SIN',26:'COS',27:'LOG',28:'ATAN',29:'LN',30:'EXP',31:'SQT',32:'MRK',33:'RLS',34:'IOR',35:'UBUSY',
        36:'POT',37:'UWAIT',38:'UCLEAR',39:'HLT',40:'MEMA'}
fb = [(k, CS[k], CZ[k]) for k in range(64) if CZ[k]]
print("\nstandard procedures (CSP) that fell back to Z80:" + ("" if fb else " none"))
for k, n, z in fb: print(f"   CSP {k:2d} {CSPN.get(k, '?'):7s} {z:9,d} of {n:9,d} calls")
