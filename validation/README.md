# Validation suite for the native-C P-code conversion effort

This directory holds everything needed to verify a change to
`UCSDPascal/PSystemEngine.cpp` (or its mirrors in `linux-harness/`)
without re-deriving the test methodology from scratch each session.

If you are Claude, picking this project back up in a new conversation:
read this file first, then `run_all.sh` to see the exact commands. All
scripts assume they are run from *this* directory
(`UCSD-Pascal---P-Machine_work/validation/`) and that `linux-harness/`
and `data/` are its siblings (i.e. this project's own top-level layout,
unchanged).

## What's here

- `run_all.sh` — builds `harness` and `remove_file` from
  `../linux-harness/`, then runs every check below in order and reports
  pass/fail for each. Run this after ANY change to PSystemEngine.cpp's
  opcode-switch logic, before considering the change done.
- `pmachine_focused_diff.patch.py` — a Python script that takes
  `../linux-harness/harness.cpp`, patches in a P-machine-focused
  comparison (see "Why a separate comparison" below), builds it with
  `preserveZ80RegCompat = false` on the native engine, and runs it.
  Prints PASS/FAIL. This is the test that actually exercises
  `m_preserveZ80RegisterCompat = false` — the default trace-diff never
  does, since both engines run with the flag at its default (true).
- `functional_tests.py` — a set of small Pascal programs fed through
  `remove_file` (the interactive-keystroke test harness), each with its
  expected output. Run standalone or via `run_all.sh`.
- `pascal_programs/` — the source of each Pascal test program used by
  `functional_tests.py`, kept as standalone `.pas`-style text files too,
  in case you want to feed one through by hand or extend it.

## Why two different trace-diffs

`harness.cpp` runs two full boot sequences of the same disk image side
by side: one with `useNativeOps = false` (pure Z80, the ground truth)
and one with `useNativeOps = true` (native C opcode replacements). At
every `BACK` dispatch point it snapshots both engines' registers and
compares. This is the primary regression test for the whole native-C
conversion project — a single opcode's native implementation producing
a wrong result anywhere in the boot sequence shows up as a mismatch
here, usually within the first few hundred thousand instructions.

That default comparison includes the Z80 scratch registers (`DE`,
`HL`, `A`) alongside the P-machine-relevant ones (`BC`/IPC, `SP`, the
stack-top words, and the named P-machine registers `NP`/`MPD0`/
`BASED0`/`IPCSAV`/`MP`/`BASE`/`JTAB`/`SEGP`). That's correct and
sufficient AS LONG AS `m_preserveZ80RegisterCompat` stays at its
default (`true`) in both engines, which `run_all.sh`'s first check
verifies.

But the whole point of that flag is that turning it off deliberately
lets Z80 scratch registers diverge from what real Z80 execution would
leave — that's not a bug, it's the intended, ongoing direction of this
work (see `/areas/ucsd-pascal-emulator.md` in memory, or just ask the
user, for the "why"). So a second, P-machine-focused comparison
(excluding `DE`/`HL`/`A` from the equality check) is what actually
verifies the toggle is safe: run it with the native engine's
`preserveZ80RegCompat` set to `false`, and confirm P-machine state
still matches the Z80 baseline throughout. `pmachine_focused_diff.patch.py`
automates building and running exactly that.

Do not skip the P-machine-focused check when you've just converted a
new opcode's register-compat gating — the plain trace-diff with the
flag at its default won't tell you anything new about that specific
change, since nothing gated is exercised.

## Check 4: functional tests with the toggle off

`run_all.sh` also rebuilds `remove_file` with `preserveZ80RegCompat`
defaulted to `false` (via `sed` into /tmp, never touching the source)
and re-runs every functional test against it. Check 2 only covers the
boot sequence with the toggle off; this covers real compiled programs
(procedure calls, strings, sets, packed arrays) with it off, which is
the configuration this project is heading toward.

## Check 5: text import conversion

Options/Import converts .TEXT files with ConvertToUcsdText (UCSDPascal/
UcsdText.h): 1K zero header, CR-only lines, DLE-coded indentation, whole
lines in NUL-padded 1K pages. ucsdtext_check.py builds that header on Linux
and checks format invariants + exact round trip for every test program and
tools/FIXUP.TEXT, with CRLF, LF and tab-indented input. (v1.66: a converted
FIXUP.TEXT was also loaded, saved, compiled and executed in the real editor
and compiler; the old raw byte copy hung the editor.)

## Check 6: floating point and other directly-tested operators (fptest)

UCSDPascal/UcsdReal.h is a bit-exact C++ port of the Z80 floating-point
package in SYSTEM.MICRO (FPLSUM, FPFMUL, FPFDIV, FPFFLOAT, FPFFIX, FPFRND,
normalize/round/store). linux-harness/fptest.cpp boots the pure-Z80 engine,
then for each case calls the REAL Z80 routine (ADR, SBR, MPR, DVR, SQR,
NGR, ABR, FLT, FLO, TNC, RND, POT, EFJ, NFJ) and the C version on the same
operands and compares results and error outcome byte for byte. Operands
mix random bit patterns with targeted edge cases (zeros, extreme and
boundary exponents, cancellation, 22-27 bit alignment shifts). FLT is
checked for all 65,536 integers. EFJ/NFJ are tested here because the II.0
compiler never emits them (it only generates FJP). v1.67 ran 500,000 cases
per operation with no mismatch; run_all.sh uses 200,000 (20,000 quick).

## Check 8: reclaimed interpreter memory (full mode only)

Runs the whole Verify build natively with the Z80 interpreter's and BIOS
memory reclaimed (PSystemEngine::SetReclaimInterpreterMemory; since v1.80
the native boot starts in that layout: heap from 0x03A4 instead of 0x1EBA,
MEMTOP 0xFFF2 instead of 0xFDF2, nothing of SYSTEM.MICRO or pascal.bin in
memory but the opcode table and P-machine variables; the engine stops if
any Z80 instruction is ever needed). The P-code log necessarily differs (heap addresses), so the
check is: the script completes, nothing needed Z80 code, and every output
file is identical in content to the Z80 reference run's -- apart from at
most three alignment-padding bytes per code file (compare_outputs.py
--allow-code-bytes 3), which hold whatever was in the compiler's buffer.
v1.76: passes; the compiler reports ~3,467 more free words in every compile.
v1.80 (reclaimed at boot): the compiler compiling itself has 7,590 free
words instead of 3,867 (+7,446 bytes). v1.81 (relocated layout, no Z80
files): 7,782 words (+7,830 bytes).

## Check 9: run-time errors (full mode only)

verify/ERRORS.SCRIPT runs regression programs (REAL and PACKED ARRAY OF CHAR
comparisons) and programs that each trigger one run-time error
(see verify/README.md). Recorded in Z80 mode, then a native run must match
it instruction for instruction, and a run with the interpreter's memory
reclaimed must complete with all 14 error reports (HALT prints none).
v1.92: 24 programs and 18 error reports; also STRCONST (string and
packed-array constants, 13 checks) and HCODE (an error in a segment
procedure resumed with ESC, and unit 64); every `S#, P#, I#` line of the
reclaimed run must be identical to the normal native run's.

## Harvard mode (VERIFY_HARVARD=1)

`VERIFY_HARVARD=1 bash run_all.sh` runs every check with Harvard mode on
wherever the reclaimed layout is used (Checks 8, 9, 11, 12, 13; the other
checks do not use that layout). v1.92: all 13 pass; Check 8 and 11 report
21,477 free words (7,782 without it); Check 9 expects the HCODE lines for
unit 64 as I-space instead of a bad unit.

## Check 10: native boot (full mode only)

In P-Code mode the engine boots natively (PSystemEngine::NativeBoot): it
does what the pascal.bin loader and the interpreter's BOOT code do -- reads
SYSTEM.MICRO, the directory, SYSTEM.PASCAL's segment dictionary and segment
0 -- and starts at the first P-code instruction. Check 10 boots both ways and
requires all 65,536 bytes of memory and every Z80 register to be identical
at the first P-code instruction. (Checks 7-9 then run from the native boot.)

## Check 11: P-Code mode with nothing Z80 (full mode only)

The data folder has no pascal.bin and the boot disk no SYSTEM.MICRO. With
register compatibility off and memory reclaimed, the native boot uses the
tables compiled into the engine (UCSDPascal/PMachineTables.inc, generated by
tools/make_pmachine_tables.py) and the relocated layout: the P-machine
variable block (SYSCOM, SEGTBL, ...) at 0x0080-0x0223 instead of
0x0200-0x03A3, the heap from 0x0224, MEMTOP 0xFFF2. The whole Verify build
must complete and write the same files as the Z80 reference run. v1.81: the
compiler compiling itself has 7,782 free words (normal boot: 3,867).

## Check 12: reclaimed-memory reference logs (full mode only)

The reclaimed-memory layout has its own reference log (GUI: Options > Verify
P-System > Record Reference Log (P-Code mode, reclaimed memory);
verify/reference_reclaimed.pcl). It is recorded by the native engine itself,
since the Z80 interpreter cannot run in that layout, so it is a regression
baseline: later runs in that layout are checked against it instruction by
instruction -- IPC, opcode, SP, NP, MPD0, BASED0, IPCSAV, MP, BASE, JTAB,
SEGP and the top two stack words at every P-code instruction. Check 12
records it for the Verify build and for the error suite, then runs each a
second time compared against the recording: every record must be
reproduced (determinism and verification in one pass; ~1.3 GB of free disk
space needed for the build's log). (v1.82: 48,529,153 and
1,396,121 instructions.)

## Check 13: the Tiny-C compiler (full mode only)

verify/CXPBUG.BLK holds the Tiny-C compiler (TINYC.CODE) and FV.TEXT, whose
compile makes calls into other segments far more than 64 frames deep. The
compile pass is recorded in Z80 mode and a native run must match it
instruction for instruction (TINYC_FV.SCRIPT); with memory reclaimed the
whole compile must reach "Done." (TINYC_FV_FULL.SCRIPT). On v1.83 the native
run stopped after 270,000 instructions (the CXP static-link search bug).

## Z80 profile (not a pass/fail check)

z80_profile.py builds a copy of the REAL engine with a Z80 instruction
counter, runs the full Verify P-System script natively (the system
rebuilding itself, ~48.5 million P-code instructions; register
compatibility off) and reports every Z80 instruction still executed,
grouped by purpose (routine names from system_micro_symbols.txt, taken from
the SYSTEM.MICRO listing). Use it to choose and confirm each conversion.

  v1.72  48,818,812 Z80 instructions: 50% long jump-table jumps, 39% unit
         I/O + CP/M I/O drivers
  v1.73  24,333,614: 79% unit I/O + CP/M I/O drivers (console output,
         disk), 20% other sub-paths, 1% call/return fall-backs
  v1.74  479,970: unit I/O (UREAD/UWRITE/UCLEAR) native; six CSP
         procedures lost from NativeCsp.inc in v1.67 (NEW MVL MVR TIM FLC
         SCN) restored. Left: INN out of range 309k, call/return
         fall-backs 140k, XJP 16.5k, set compares, boot.
  v1.75  154,423: INN beyond the set's size and XJP's else-jump native
         (register compatibility off). Left: procedure call fall-backs
         (BLDMSCW/BLD4, CBPXNL, CIPXNL, STKCHK) 142k, set compares, boot.
  v1.76  3,853 -- all of it before the first P-code instruction (the
         pascal.bin loader and BIOS 2,168, the interpreter's start-up
         1,455, its disk reads 230). From the first P-code instruction on,
         ZERO Z80 instructions. Fixes: the loop now also stays native when
         a native case hands over to another native entry (CXP -> CIP,
         142k); UNI "Unionb" and set comparisons (POWRC) native.
  v1.79  0 -- the boot is native in P-Code mode too: from power-on through
         the whole Verify build, no Z80 instruction executes.
The profile also lists every standard procedure (CSP) that fell back to
Z80 -- a native path that silently stops being used shows up there.

## Known-good baseline numbers

At the standard 5,000,000-instruction budget against `../data`
(`pascal.bin` + `Big_Disk.BLK` + `Empty_Big_Disk.BLK`), a fully correct
build reaches:

    baseline (pure Z80):    BACK visits = 210529
    native (native C ops):  BACK visits = 3256036

(The native figure rose from 452838 to 465593 in v1.64 -- RNP/RBP segment
changes and resident/seg-0 CXP went native -- and to 467119 in v1.65, when
non-resident CXP (native segment load from disk + relocation) went native.
Both deliberate, verified changes: fewer Z80 instructions per P-code op.
In v1.71 it jumped to 2273354: the run loop no longer executes the Z80
BACK/BACK1 dispatch code after every native opcode -- which had been 85%
of all Z80 instructions executed in native mode -- so about 4.9x more
P-code runs in the same budget. v1.73: 2642195 -- long jump-table jumps
(UJP/FJP/EFJ/NFJ) native. v1.75: baseline 210529 / native 2570769 --
the Big Disk emulation now reports units with no image (11-14 in this
harness) as unavailable, as the original MunkDisk.c does, instead of
redirecting them to unit 4; the system probes those units while starting,
so the boot takes a different, correct path. v1.76: native 3256036 -- a native
case handing over to another native entry (CXP -> CIP) no longer runs the
Z80 routine in between.)

These numbers are deterministic (same disk image, same instruction
budget) and have been stable across every correct revision of this
project. If a change makes these numbers drop, or produces ANY
mismatch line before reaching them, something broke — don't package
the change until you've found out what.

At 50,000,000 instructions (used for the P-machine-focused check, for
higher confidence than the default budget gives):

    baseline: BACK visits = 2129284
    native:   BACK visits = 4528329

## Known-good functional output

`functional_tests.py` checks each program's output against the exact
strings recorded there. If you add a newly-converted opcode and want a
functional (not just trace-diff) check that it behaves correctly
end-to-end through real compiled Pascal, add a small program here
rather than constructing one from scratch each time — CONCAT/records/
arrays/sets/CASE/DIV/MOD are already covered; extend the existing
programs' bodies rather than writing overlapping new ones.

## If a check fails

1. Don't package the change. Find the regression first.
2. For a trace-diff mismatch: the printed BACK visit number tells you
   how many opcodes ran correctly before the first divergence. Narrow
   down which opcode is at fault by checking `lastNativeOpcode` in the
   mismatch output, or by adding a temporary bounded dump of BackRecord
   fields around that visit number (see this project's own chat history
   for the exact pattern — grep past conversations for "backLog\[k\]"
   if you need the boilerplate again).
3. For a functional-test mismatch: the failing program's source is
   right there in `pascal_programs/` — check it against a UCSD Pascal
   II.0 manual/reference if the EXPECTED output itself might be wrong,
   but assume the code is the bug until proven otherwise.
4. Once fixed, re-run `run_all.sh` from scratch — don't assume fixing
   one thing didn't disturb another.

## Shared native-code include files

Since v1.64, new native code is written ONCE, in include files that live
in both UCSDPascal/ and linux-harness/ (identical copies, like
PCodeOpcodes.h): NativeSegReturn.inc (RNP/RBP DECREF), NativeCxp.inc
(CXP: seg 0, resident and non-resident segments), NativeSegLoad.inc (the
GETSEG disk-read path -- READSEG + RLSEG/RLLIST -- shared by CXP and GSEG;
needs a host NativeDiskRead(), unit 4 only), NativeCsp.inc (21 CSP standard
procedures), NativeOps.inc + NativeOpsTargets.inc (the 15 operators added
in v1.67; UcsdReal.h at file scope). UcsdReal.h and PCodeOpcodes.h are
shared too and covered by the same identical-copy check. Each host binds them with four macros defined right after its
own #include "PCodeOpcodes.h": PM_MEM, PM_CPU, PM_PRESERVE, PM_COUNT(op).
Since v1.92 also the code-memory accessors PM_CODE8 / PM_CODE16 / PM_CODEW8
(every read or write of code goes through them) and the Harvard hooks
PM_HARVARD, PM_CODE_PLACE, PM_CODE_COMMIT, PM_CODE_FREE, PM_CONST_SCAN,
PM_CONST_INSTALL, PM_ERR_ARM, PM_ERR_SHADOW, PM_CODE_UNIT (no-ops in the
linux-harness programs, which have no Harvard layout).
When editing one, edit the UCSDPascal/ copy and copy it over the
linux-harness/ one; `diff UCSDPascal/X.inc linux-harness/X.inc` must be
empty before packaging.

NativeCsp.inc's procedures run only with preserveZ80RegCompat OFF, so only
Checks 2 and 4 exercise them. NativeCxp.inc/NativeSegLoad.inc run in both
modes and are register-exact (Check 1 covers the boot's segment loads).

## Keeping this suite current

When you convert more opcodes or make another structural change (like
the `commit` flag collapse), update this directory in the SAME session,
before packaging:

- If the new opcode has a rare/interesting behavior not already
  exercised (a new fall-through pattern, a new register-restoration
  quirk), consider adding a functional test for it.
- Bump the "known-good baseline numbers" here ONLY if a deliberate,
  verified-correct change actually changes total instruction throughput
  (e.g. a real optimization) — never bump them to make a check pass
  after an unexplained regression.
