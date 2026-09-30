#!/usr/bin/env python3
"""
Builds and runs the P-machine-focused trace-diff: same boot sequence as
harness.cpp's default comparison, but with the native engine's
m_preserveZ80RegisterCompat set to false, and the equality check
excluding the Z80 scratch registers (DE, HL, A) that flag is allowed to
leave diverged. See ../README.md ("Why two different trace-diffs") for
the reasoning.

Usage (from this directory):
    python3 pmachine_focused_diff.patch.py [instruction_budget]

Default instruction_budget is 50,000,000 (the number the project's own
known-good baseline in README.md is quoted at). Pass a smaller number
for a quick smoke check, e.g. 5000000.

Exits 0 and prints "PASSED" on success; exits 1 and prints the mismatch
detail on failure. Leaves a built binary at /tmp/pmachine_focused_diff
(and its patched source at /tmp/pmachine_focused_diff.cpp) for reuse or
inspection -- re-run this script to rebuild after any source change.
"""
import subprocess
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
HARNESS_DIR = os.path.join(HERE, "..", "linux-harness")
DATA_DIR = os.path.join(HERE, "..", "data")
PATCHED_CPP = "/tmp/pmachine_focused_diff.cpp"
BINARY = "/tmp/pmachine_focused_diff"

budget = sys.argv[1] if len(sys.argv) > 1 else "50000000"

with open(os.path.join(HARNESS_DIR, "harness.cpp")) as f:
    src = f.read()

old_struct = '''    bool operator==(const BackRecord& o) const {
        return bc==o.bc && de==o.de && hl==o.hl && sp==o.sp && top0==o.top0 && top1==o.top1 && a==o.a
            && np==o.np && mpd0==o.mpd0 && based0==o.based0 && ipcsav==o.ipcsav
            && mp==o.mp && base==o.base && jtab==o.jtab && segp==o.segp;
    }
};'''
new_struct = '''    bool operator==(const BackRecord& o) const {
        return bc==o.bc && de==o.de && hl==o.hl && sp==o.sp && top0==o.top0 && top1==o.top1 && a==o.a
            && np==o.np && mpd0==o.mpd0 && based0==o.based0 && ipcsav==o.ipcsav
            && mp==o.mp && base==o.base && jtab==o.jtab && segp==o.segp;
    }
    // P-machine-focused equality: excludes de/hl/a (pure Z80 scratch
    // registers, expected to diverge once m_preserveZ80RegisterCompat is
    // off) but keeps bc (=IPC, a P-machine register), sp/top0/top1 (the
    // P-code stack itself), and every explicit P-machine register.
    bool pmachineEqual(const BackRecord& o) const {
        return bc==o.bc && sp==o.sp && top0==o.top0 && top1==o.top1
            && np==o.np && mpd0==o.mpd0 && based0==o.based0 && ipcsav==o.ipcsav
            && mp==o.mp && base==o.base && jtab==o.jtab && segp==o.segp;
    }
};'''
if old_struct not in src:
    print("FAILED: BackRecord::operator== not found in the expected shape -- "
          "harness.cpp's struct definition has changed; update this script's "
          "old_struct/new_struct strings to match.")
    sys.exit(1)
src = src.replace(old_struct, new_struct)

old_report = '''    if (!mismatch && a.backLog.size() == b.backLog.size())
        printf("\\nTRACE-DIFF PASSED: all %zu BACK-loop snapshots identical between baseline and native-op runs.\\n", n);'''
new_report = old_report + '''

    bool pmismatch = false;
    for (size_t i = 0; i < n; i++) {
        if (!a.backLog[i].pmachineEqual(b.backLog[i])) {
            const BackRecord& x = a.backLog[i];
            const BackRecord& y = b.backLog[i];
            printf("P-MACHINE MISMATCH at BACK visit #%zu: (last native op that fired: 0x%02X)\\n", i, y.lastNativeOp);
            printf("  baseline BC=%04X SP=%04X top0=%04X top1=%04X NP=%04X MPD0=%04X BASED0=%04X IPCSAV=%04X MP=%04X BASE=%04X JTAB=%04X SEGP=%04X\\n",
                   x.bc, x.sp, x.top0, x.top1, x.np, x.mpd0, x.based0, x.ipcsav, x.mp, x.base, x.jtab, x.segp);
            printf("  native   BC=%04X SP=%04X top0=%04X top1=%04X NP=%04X MPD0=%04X BASED0=%04X IPCSAV=%04X MP=%04X BASE=%04X JTAB=%04X SEGP=%04X\\n",
                   y.bc, y.sp, y.top0, y.top1, y.np, y.mpd0, y.based0, y.ipcsav, y.mp, y.base, y.jtab, y.segp);
            pmismatch = true;
            break;
        }
    }
    if (!pmismatch)
        printf("\\nP-MACHINE-FOCUSED CHECK PASSED: all %zu BACK-loop snapshots' P-machine state identical (Z80 scratch registers DE/HL/A excluded).\\n", n);
    else
        exit(1);'''
if old_report not in src:
    print("FAILED: the TRACE-DIFF PASSED report line not found in the expected "
          "shape -- harness.cpp's main() has changed; update this script's "
          "old_report/new_report strings to match.")
    sys.exit(1)
src = src.replace(old_report, new_report)

old_engine_b = "Engine b; b.useNativeOps = true;  b.maxInstructions = limit;"
new_engine_b = "Engine b; b.useNativeOps = true; b.preserveZ80RegCompat = false; b.maxInstructions = limit;"
if old_engine_b not in src:
    print("FAILED: Engine b's setup line not found in the expected shape -- "
          "update this script's old_engine_b/new_engine_b strings to match.")
    sys.exit(1)
src = src.replace(old_engine_b, new_engine_b)

with open(PATCHED_CPP, "w") as f:
    f.write(src)

build = subprocess.run(
    ["g++", "-O2", "-std=c++17", "-I", HARNESS_DIR, "-o", BINARY,
     PATCHED_CPP, os.path.join(HARNESS_DIR, "z80.cpp")],
    capture_output=True, text=True)
if build.returncode != 0:
    print("BUILD FAILED:")
    print(build.stdout)
    print(build.stderr)
    sys.exit(1)

run = subprocess.run([BINARY, DATA_DIR, budget], capture_output=True, text=True, timeout=300)
print(run.stdout[-4000:])
if "P-MACHINE-FOCUSED CHECK PASSED" in run.stdout:
    print("PASSED")
    sys.exit(0)
else:
    print("FAILED")
    sys.exit(1)
