# Verify P-System

Records every P-code instruction the system executes while it rebuilds
itself from source -- compiler, Z80 assembler, L2, operating system, Filer
and screen editor compiled, and the Z80 P-code interpreter assembled -- and
lets later runs be compared against that record instruction by instruction.
The reference is recorded with the original Z80 interpreter (Z80 mode), so
it is the yardstick for every native-C conversion.

## Using it (GUI)

Copy this `verify` folder next to the program (the folder that holds
`pascal.bin`). It needs `SOURCE.BLK`, `COMPASM.BLK` and `VERIFY.SCRIPT`.

* **Options > Verify P-System > Record Reference Log (Z80 mode)**
  Copies the current unit #4 disk to `verify\reference_boot.BLK`, restarts
  the system on fresh copies of the disks, runs `VERIFY.SCRIPT` in Z80 mode
  and writes `verify\reference.pcl` (about 1.2 GB, 48.5 million
  instructions).
* **Options > Verify P-System > Verify Against Reference Log**
  Same fresh start (using the saved reference boot disk), in the current
  execution mode, comparing every instruction with the reference and
  stopping at the first difference.
* **Cancel Verification** stops a run.

While a run is going the keyboard is ignored and the status bar shows the
script step and instruction count. At the end a message gives the result
(saved with the console transcript in `verify\last_verify_report.txt`) and
your normal session restarts. Your own disk images are never changed.

## Files

| File | |
|---|---|
| `SOURCE.BLK` | unit #5, volume `SOURCE`: OS, Filer, editor, interpreter sources (43 files); receives all output |
| `COMPASM.BLK` | unit #9, volume `COMPASM`: compiler, Z80 assembler and L2 sources (24 files) |
| `VERIFY.SCRIPT` | the keyboard script (`WAIT "text"` / `TYPE "keys"`) |
| `compare_outputs.py` | compares two runs' disks by content (used by Check 7) |
| `INTERP_FULL_LISTING.txt` | full assembly listing of the U120 interpreter sources (all listing switches on) |
| `build_verify_volumes.py` | builds the two volumes from the distribution archive's `BLK_format` images |
| `run_verify.cpp`, `linux-shim/` | runs the real `PSystemEngine` on Linux (used by validation Check 7) |

The sources are split over two volumes because all 67 files will not fit
under 60 on one; the volume must be named `SOURCE` and be on unit 5 because
the editor includes `SOURCE:HEAD` etc. and the OS includes `#5:GLOBALS`
etc. `YALOE.TEXT` is not included: it is a segment for inclusion in a
larger program and cannot be compiled on its own.

## Why the runs are deterministic

* Every run starts from fresh copies of the same disks; the boot copy has
  any work file removed, so no program ever asks "Throw away current
  workfile?" and the Filer's Get is never used.
* Keys are handed over only while the system is blocked waiting for a key
  with nothing queued, one at a time: the system sees the same input at
  the same point every time, however fast the host is, and never loses
  type-ahead (it discards type-ahead while starting up).
* Nothing in the emulator reads the clock.
* Each `TYPE` is preceded by a `WAIT` for its prompt; if the system stops
  for input without showing the expected text (a compile error, an
  unexpected question), the run stops with a message instead of typing
  into the wrong prompt.

Two Z80-mode runs produce byte-identical logs.

## The log

One 25-byte record per P-code instruction (at every BACK dispatch): IPC,
opcode, SP, NP, MPD0, BASED0, IPCSAV, MP, BASE, JTAB, SEGP and the top two
stack words.

During verify runs only, the free stack space used below SP by the previous
instruction is cleared at each instruction boundary, identically in both
modes. Z80 routines leave return addresses and temporaries there and native
code does not; programs that read never-initialized variables would
otherwise see mode-dependent left-overs. (The P-machine never legitimately
reads below SP; nothing at or below the heap top is ever cleared.)

## Results (v1.72)

* Z80 mode: 48,535,548 P-code instructions, about 33 s on the Linux test
  machine. Two runs: identical logs.
* Native (P-Code) mode, with and without Z80 register compatibility:
  **all 48,535,548 instructions match the Z80 reference**, about 5 s.
* It found one real native bug on its first run: CEQU/CNEQ on byte/word
  arrays did not save the IPC after the size operand (fixed in v1.72).
* v1.73: long jump-table jumps made native; still all 48,535,548 match.
* v1.74: unit I/O (UREAD/UWRITE/UCLEAR) native; still all match, and the
  files the native run writes are identical in content to the Z80 run's
  (`compare_outputs.py`: code segments and directory entries; only unused
  padding bytes may differ, since they hold whatever was in memory).
* v1.75: INN / XJP native. The Big Disk emulation now matches the original
  MunkDisk.c: CLEAR transfers nothing, and a unit with no image mounted
  reports status 1 instead of being redirected to the unit 4 disk. The
  Z80 reference is unchanged (identical log), so earlier recorded
  reference logs stay valid.
* v1.76: from the first P-code instruction on, NO Z80 code executes during
  the whole build (validation/z80_profile.py: 3,853 Z80 instructions, all
  in the boot). Still all 48,535,548 instructions match.

## Reclaiming the Z80 interpreter's memory (experimental, v1.76)

**Options > Reclaim Z80 Interpreter Memory** (P-Code mode, register
compatibility off; applies at the next restart). At the first P-code
instruction the heap start moves from 0x1EBA to 0x03A4, giving the P-System
about 6.8 KB more: the compiler compiling itself has 7,334 free words
instead of 3,867. The whole Verify build runs this way (validation Check 8)
and produces the same files as the Z80 reference.

Run-time errors are handled natively since v1.77 (see below), so they are
reported by the P-System as usual with memory reclaimed. Still to do before
this can be the default: the boot still uses the Z80 loader and start-up code (the BIOS page, 0xFE00-0xFFFF, can only be reclaimed once
the boot is native, since the system's first segment is loaded just below
it during start-up).

## Run-time errors (v1.77)

`ERRORS.SCRIPT` with the `ERRTEST.BLK` volume (on unit 9; sources in
`errtest/`) runs 18 programs that each trigger one run-time error -- value
range (CHK, string index, set membership, set building), string overflow,
DIV and MOD by zero, floating point overflow, division by zero and TRUNC
overflow, HALT, an I/O error, stack overflow by recursion and by NEW, three
damaged code files (a segment of length 0, a segment beyond the end of the
disk, a segment block holding another segment: "No proc in seg-table" and
"System IO error", see `errtest/SEGERRS.README`), and EXIT from a procedure
never called -- and answers the system's report.

E11 (EXIT from a procedure never called) is last: after it the P-System
halts. The failed EXIT has already rewritten every frame's return point up
to the system's own, so the recovery EXIT(COMMAND) ends the operating
system's main program -- which returns to the ABORT opcode that the boot
placed there as its return point. ABORT is "JP ABORT" in the Z80
interpreter (a dead stop on the original machine); the emulator now
recognises it and reports "The P-System has halted" (the GUI then restarts
the P-System), and the script's HALTED step checks for it.

Native handling (register compatibility off): each native instruction
reproduces what its Z80 routine does to the stack before an error and
continues at the interpreter's error label; `NativeErrors.inc` then does
XEQERR (XERRCD, BOMBP = SP-14, BOMIPC, IPC := the CXP 0,2 instruction that
calls EXECERROR). Floating point errors push exactly the Z80 package's error
result (`UcsdReal.h`; checked by fptest, which now compares error results
too): underflow 0.0, overflow FF 7F FF FF -- always positive, because
FPLABN's POP HL replaces the sign register before FPLSIGN uses it.

The whole suite matches the Z80 reference instruction for instruction, and
runs with the interpreter's memory reclaimed (validation Check 9). Errors
while loading a code segment are native too (v1.78): NativeSegLoad.inc
reproduces READSEG's state at each error exit (GETSEG's RETADR2/SEGNUM/REFP,
SEGTP, the extended stack with READSEG's return address, SYSRD's variables
and -- for a read error -- the Big Disk status through the engine's own
ports), then continues at NOPROC or SYIOER.

## Native boot (v1.79)

In **P-Code mode** the engine boots natively (`PSystemEngine::NativeBoot`):
it does the work of the pascal.bin loader (page zero, SYSTEM.MICRO read from
unit 4 and moved to 0x0100, FIRSTSP) and of the interpreter's BOOT code
(MEMTOP; the directory read to 0x2019; SYSTEM.PASCAL found; its segment
dictionary read to 0x2819; SEGTBL; segment 0 read below MEMTOP; INTSEGT; the
first frame with ^SYSCOM and the ABORT return point; the P-machine
registers) and starts at the first P-code instruction. Memory is laid out
exactly as the Z80 boot leaves it -- same addresses, the interpreter image
and pascal.bin's BIOS page still in place, even the stack left-overs of the
boot's disk reads -- so every reference log stays valid: at the first P-code
instruction all 65,536 bytes and every Z80 register are identical to a Z80
boot (validation Check 10). NativeBoot checks the interpreter's BOOT code
byte for byte and runs the Z80 boot instead if it differs (the status bar
says why). **Z80 mode** always boots with the Z80 code.

From power-on through the whole Verify build, P-Code mode now executes no
Z80 instruction at all (validation/z80_profile.py: 0).

## Reclaimed memory from the boot (v1.80)

With **Options > Reclaim Z80 Interpreter and BIOS Memory** (P-Code mode,
register compatibility off) the native boot starts the P-System in the
reclaimed layout. SYSTEM.MICRO is not loaded into the P-System's memory: it
is read as a data file into the engine's private table copy, and only the
opcode table and the P-machine variable area (0x0100-0x03A3: SYSCOM,
SEGTBL, INTSEGT, the I/O variables -- the operating system uses these
addresses) go into memory. pascal.bin is not needed at all. The heap starts
at 0x03A4 (was 0x1EBA) and MEMTOP is 0xFFF2 (was 0xFDF2, below the BIOS
page), so segment 0 sits at the very top: 7,446 bytes more for the P-System
(the compiler compiling itself: 7,590 free words instead of 3,867).

Why SYSTEM.MICRO is still read: P-Code mode no longer runs any of its Z80
code, but it still holds P-machine DATA the native code uses -- the opcode
dispatch table, the initial P-machine variables and SYSCOM, the
standard-procedure and unit tables, the powers of ten, the reserved-word
table the compiler's IDSEARCH uses, and the CXP 0,2 instruction for
run-time errors. These could be compiled into the engine (a few hundred
bytes of tables), after which P-Code mode would not need SYSTEM.MICRO at
all. Z80 mode, and P-Code mode with register compatibility on (whose
fall-back paths run Z80 code), keep loading and using it as before.

## P-Code mode without SYSTEM.MICRO or pascal.bin (v1.81)

With the reclaim option (P-Code mode, register compatibility off) nothing
Z80 is used any more:

* The P-machine data the native code needs -- the opcode dispatch table,
  the initial P-machine variables and SYSCOM, the powers of ten, the
  reserved-word table, the CXP 0,2 instruction for run-time errors, the
  operating system's file name and the unit configuration -- is compiled
  into the engine (`UCSDPascal/PMachineTables.inc`, generated from the boot
  disk's SYSTEM.MICRO by `tools/make_pmachine_tables.py`). No Z80 code is
  included. The unit configuration used to be discovered by reading the Z80
  drivers' instruction bytes; that discovery is now a separate step
  (`PmIoConfigFromImage`) used only when an interpreter image is present.
* The P-machine variable block is relocated from 0x0200-0x03A3 to
  0x0080-0x0223: every native access goes through `PM_V(address)` (425
  addresses), which adds 0 in the normal layout and -0x180 here. The
  operating system reaches SYSCOM only through the pointer the boot pushes
  (now 0x0164). The opcode table is no longer in the P-System's memory, the
  CXP 0,2 bytes sit at 0x0040, the heap starts at 0x0224 and MEMTOP is
  0xFFF2: 7,830 bytes more than the Z80 layout.
* SYSTEM.MICRO need not be on the boot disk and pascal.bin need not exist
  (validation Check 11). If the Z80 boot is needed and pascal.bin is
  missing, the engine stops with a message (BootFault).

Z80 mode, and P-Code mode without the reclaim option, keep the Z80 layout
and use SYSTEM.MICRO and pascal.bin exactly as before -- the reference logs
are unaffected (Checks 7 and 10).

## Two reference logs (v1.82)

A record holds addresses (IPC, SP, MP, NP, SEGP, ...), so each memory layout
needs its own reference:

| Reference | Layout | Recorded by | Verifies |
|---|---|---|---|
| `reference.pcl` (+ `reference_boot.BLK`) | Z80 layout | the Z80 interpreter (Z80 mode) | Z80 mode, and P-Code mode without the reclaim option |
| `reference_reclaimed.pcl` (+ `reference_reclaimed_boot.BLK`) | reclaimed / relocated layout | the native engine (P-Code mode, compatibility off, reclaim) | P-Code mode with the reclaim option |

**Verify Against Reference Log** uses the one that matches the current
settings. The Z80 reference checks the native engine against the original
interpreter; the reclaimed-memory reference is a regression baseline -- it
proves later versions behave exactly as the version it was recorded with.
Both check every P-machine register and the top two stack words at every
P-code instruction. Recordings are deterministic (validation Check 12).

## Comparisons of REALs and packed arrays of char (v1.83)

The native comparisons handed REAL comparisons (type REALC) and the ordering
comparisons of PACKED ARRAY OF CHAR (<, <=, >, >= with type BYTEC) back to
the Z80 routines; with memory reclaimed that stopped the engine. Both are
native now (register compatibility off):

* REALC (`NativeRealc.inc`, `PmRealCompare` in PCodeOpcodes.h) mirrors the
  Z80 REALC routine exactly -- a bytewise comparison (signs; both negative:
  swapped; then exponent, high mantissa byte, byte 2, byte 3), not an
  arithmetic one -- and the six comparison routines' use of its flags.
  fptest checks it against the real Z80 routines for all six opcodes on
  60,000 pairs of reals (equal, one byte apart, opposite signs, negative).
* BYTEC/WORDC ordering (CmpSetupOrdering) mirrors SWEQ: scan while equal
  (a count of 0 wraps round to 65536), flags from the last pair compared.

ERRORS.SCRIPT now starts with two regression programs: REALCMP (REALCMP.CODE,
source errtest/REALCMP.PAS.txt) and BYTECMP (compiled by the script); both
must print "7 of 7 comparisons true" in every mode (validation Check 9).

What can still need Z80 code with memory reclaimed (none of it exercised by
the suites; the engine stops with a message if it happens):
* error cases of the standard functions SIN, COS, LOG, ATAN, LN, EXP, SQRT
  (e.g. SQRT(-1), LN(0), EXP overflow);
* a stack overflow on a call into another segment (CXP);
* assembly-language procedures (Z80 code by nature);
* reading unit 7 (REMIN:) -- the BIOS reader function is not native;
* pathological cases: LDM of 0 words, a 136-character identifier,
  UNITWAIT on a unit that does not exist.

## Deep calls: the CXP / CIP static-link search (v1.84)

Found by the Tiny-C compiler (UCSD-C project, repro/DEEPCXP). When a
procedure at lex level >= 1 is called, CIPXNL searches the DYNAMIC chain for
the first frame one lex level lower -- as many frames as the recursion is
deep. The native CXP and CIP gave up after 64 frames and then jumped into the
Z80 search loop without the state it needs (the IPC it pushes, A = the
target level), so the Z80 code looped forever -- and with memory reclaimed
it is not there at all. A recursive-descent compiler calls into other
segments much deeper than 64 frames.

Fix (patch from the UCSD-C project, reviewed and applied): the search limit
is 30000 in NativeCxp.inc (CXP) and the native CIP (PSystemEngine.cpp and
the linux-harness copies); a 64K stack holds fewer than 5,500 frames, so the
search always completes natively, as the Z80 loop does. In addition the
hand-over itself now provides the Z80 loop's state (IPC pushed, A = target):
with the limit forced down to 4 in a test build, the Tiny-C FV compile still
matches the Z80 reference instruction for instruction.

Regression tests: ERRORS.SCRIPT runs DEEPCXP (CXP 60 and 70 frames deep),
DEEPC (the same in Tiny-C) and DEEPCIP (CIP from 150 frames deep) in every
mode (Check 9); Check 13 runs the Tiny-C compile itself. The old v1.83
engine fails both.

## Harvard layout regression programs (engine 1.92)

ERRORS.SCRIPT also compiles and runs two programs for the Harvard layout
(code in its own I-space, `VERIFY_HARVARD=1` with `VERIFY_RECLAIM=1`; see
`PSystemEngine::SetHarvard`). Both run in every layout, so Check 9 covers the
normal layouts too:

- STRCONST (errtest/STRCONST.TEXT): string and packed-array constants (LSA,
  LPA), which the Harvard layout copies out of the code -- 13 checks.
- HCODE (errtest/HCODE.TEXT): a divide by zero in a segment procedure,
  answered with ESC so EXECERROR returns and the program continues; then
  UNITREAD / UNITWRITE / UNITCLEAR on unit 64, which is I-space as data in
  the Harvard layout and a bad unit (IORESULT 2) in every other one.
  Check 9 also requires every error report's `S#, P#, I#` line with memory
  reclaimed (Harvard or not) to be identical to the normal layout's.
