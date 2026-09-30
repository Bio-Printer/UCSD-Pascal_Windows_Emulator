#!/usr/bin/env python3
"""Compare the files two Verify runs wrote (e.g. a Z80 reference run and a
native run) by their meaningful content:
  * directory: every entry's name, first/last block, kind and bytes used in
    the last block (not the unused bytes of the name field)
  * .CODE files: the segment dictionary's addresses and lengths, and each
    segment's code bytes (not the unused tail of its last block)
  * every other file: all bytes
Unused bytes (block tails, name padding) hold whatever was in memory and may
legitimately differ between a Z80 and a native run.
    compare_outputs.py <diskA.BLK> <diskB.BLK> [--allow-code-bytes N]
    exit status 0 = same. --allow-code-bytes N accepts up to N differing
    bytes per code file (and lists them): alignment-padding bytes inside a
    segment hold whatever was in memory, e.g. the three in COMPINIT when
    the Z80 interpreter's memory has been reclaimed.
"""
import sys
D = 1024
def w(b, o): return b[o] | (b[o + 1] << 8)
def entries(img):
    for i in range(1, w(img, D + 16) + 1):
        o = D + 26 * i; n = img[o + 6]
        yield dict(name=bytes(img[o + 7:o + 7 + n]).decode('latin1'), first=w(img, o), last=w(img, o + 2),
                   kind=w(img, o + 4) & 15, lastbyte=w(img, o + 22))
def content(img, e):
    data = bytes(img[e['first'] * 512:e['last'] * 512])
    if not e['name'].endswith('.CODE'): return data[:max(0, len(data) - 512 + e['lastbyte'])]
    segs = []
    for s in range(16):
        a, l = w(data, 4 * s), w(data, 4 * s + 2)
        segs.append((a, l, data[a * 512:a * 512 + l] if l else b''))
    return segs
allow = int(sys.argv[sys.argv.index('--allow-code-bytes') + 1]) if '--allow-code-bytes' in sys.argv else 0
a, b = bytearray(open(sys.argv[1], 'rb').read()), bytearray(open(sys.argv[2], 'rb').read())
notes = []
ea, eb = {e['name']: e for e in entries(a)}, {e['name']: e for e in entries(b)}
problems = []
for n in sorted(set(ea) | set(eb)):
    if n not in ea or n not in eb: problems.append(f"{n}: only on one disk"); continue
    x, y = ea[n], eb[n]
    if (x['first'], x['last'], x['kind'], x['lastbyte']) != (y['first'], y['last'], y['kind'], y['lastbyte']):
        problems.append(f"{n}: directory entry differs"); continue
    ca, cb = content(a, x), content(b, y)
    if ca == cb: continue
    if allow and n.endswith('.CODE') and [(s[0], s[1]) for s in ca] == [(s[0], s[1]) for s in cb]:
        diffs = [(k, off) for k, (sa, sb) in enumerate(zip(ca, cb)) for off in range(sa[1]) if sa[2][off] != sb[2][off]]
        if len(diffs) <= allow:
            notes.append(f"{n}: {len(diffs)} code byte(s) differ (allowed): " + ", ".join(f"segment {k} offset {o}" for k, o in diffs))
            continue
    problems.append(f"{n}: content differs")
print(f"{len(ea)} files compared; " + ("all identical in content" if not problems else f"{len(problems)} differ:"))
for p in problems: print("  " + p)
for m in notes: print("  note: " + m)
sys.exit(1 if problems else 0)
