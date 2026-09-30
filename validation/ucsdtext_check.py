#!/usr/bin/env python3
"""Check 5: the Windows->UCSD .TEXT converter used by Options/Import
(UCSDPascal/UcsdText.h). Builds it on Linux, converts every Pascal program in
pascal_programs/ plus ../tools/FIXUP.TEXT (in CRLF, LF and tab-indented
variants), and verifies the UCSD format invariants and that decoding the
result gives back the original lines (tabs expanded, trailing blanks dropped).
Loading/compiling a converted file in the real editor was verified by hand
in v1.66 (see the README)."""
import os, subprocess, sys, glob
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = r'''#include "UcsdText.h"
#include <cstdio>
int main(int c, char** v) { FILE* f = fopen(v[1], "rb"); std::vector<uint8_t> in; int ch;
  while ((ch = fgetc(f)) != EOF) in.push_back((uint8_t)ch); fclose(f);
  auto out = ConvertToUcsdText(in); FILE* o = fopen(v[2], "wb"); fwrite(out.data(), 1, out.size(), o); fclose(o); }'''
open('/tmp/ucsdtext_conv.cpp', 'w').write(SRC)
b = subprocess.run(['g++', '-O2', '-std=c++17', '-I', os.path.join(HERE, '..', 'UCSDPascal'), '-o', '/tmp/ucsdtext_conv', '/tmp/ucsdtext_conv.cpp'], capture_output=True, text=True)
if b.returncode: print("BUILD FAILED\n" + b.stderr); sys.exit(1)

def decode(t):
    body, lines, cur, k = t[1024:], [], bytearray(), 0
    while k < len(body):
        c = body[k]
        if c == 0x10: cur += b' ' * (body[k+1] - 32); k += 2; continue
        if c == 0: k += 1; continue
        if c == 0x0D: lines.append(cur.decode('latin1')); cur = bytearray()
        else: cur.append(c)
        k += 1
    return lines

ok = True
files = sorted(glob.glob(os.path.join(HERE, 'pascal_programs', '*.pas'))) + [os.path.join(HERE, '..', 'tools', 'FIXUP.TEXT')]
for path in files:
    text = open(path, 'rb').read().replace(b'\r\n', b'\n').decode('latin1').rstrip('\n')
    for tag, data in [('CRLF', text), ('LF', text), ('TABS', text.replace('    ', '\t'))]:
        raw = data.replace('\n', '\r\n') if tag == 'CRLF' else data
        open('/tmp/ucsdtext_in', 'wb').write(raw.encode('latin1'))
        subprocess.run(['/tmp/ucsdtext_conv', '/tmp/ucsdtext_in', '/tmp/ucsdtext_out'], check=True)
        t = open('/tmp/ucsdtext_out', 'rb').read()
        body = t[1024:]
        pages_ok = len(t) % 1024 == 0 and all(
            all(x == 0 for x in body[p:p+1024][body[p:p+1024].rfind(b'\r')+1:]) for p in range(0, len(body), 1024))
        want = [l.expandtabs(8).rstrip() for l in data.split('\n')]   # tabs -> 8-column stops
        good = pages_ok and t[:1024] == bytes(1024) and b'\n' not in body and decode(t) == want
        print(f"{'PASS' if good else 'FAIL'}  {os.path.basename(path):32s} {tag:4s} {len(t)//512:3d} blocks")
        ok &= good
print("\nALL UCSD TEXT CONVERSIONS PASSED" if ok else "\nUCSD TEXT CONVERSION FAILURES ABOVE")
sys.exit(0 if ok else 1)
