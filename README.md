# UCSD-Pascal_Windows_Emulator

A Windows (MFC) emulator of the UCSD Pascal II.0 P-System: a Z80 machine
with native P-Code execution (`UCSDPascal/PSystemEngine.cpp` and the
`Native*.inc` files), including the 8-byte `double` CSPs 100..137
(`NativeDouble.inc`) and CSP 138, CALLI -- call through a function pointer
(`NativeCalli.inc`) -- used by Tiny-C (https://github.com/Bio-Printer/UCSD-TinyC).

* **Build:** open `UCSDPascal.sln` in Visual Studio (x64). `data/pascal.bin`
  (the Z80 loader) is copied next to the .exe by the build.
* **Volumes:** File > Open Unit #4 / #5 / #9 / #10 / #11 / #12 / #13 / #14. The disk images live in
  https://github.com/Bio-Printer/UCSD-Pascal-Volumes (`BLK_format/`).

## Harvard mode: separate I & D space (version 1.92)

Options > **Harvard Mode  Separate I & D space** (on by default, remembered
in the registry as `Options\HarvardMode`). The P-System's code segments are
kept in a 64K instruction space of their own (I-space) instead of on the
P-machine stack, so the stack and heap (D-space) get the memory the code
took. It takes effect when the P-System restarts, and only in the reclaimed
layout: P-Code mode, Preserve Z80 Register Compatibility off, Reclaim Z80
Interpreter and BIOS Memory on (the item is grayed out otherwise, and the
P-System runs in the normal layout). Z80 mode is unchanged.

Free memory (Linux runs of the same engine):

| | normal reclaimed layout | Harvard mode |
|---|---|---|
| Pascal compiler compiling itself (Verify build) | 7,782 words | 21,477 words |
| Tiny-C compiling PI.C, Preprocessing | 14,175 words | 21,834 words |
| Tiny-C voltest, least memory (linking) | 8,111 words | 15,770 words |

What it means for programs (details: `PSystemEngine::SetHarvard` in
`UCSDPascal/PSystemEngine.h`):

* String and packed-array constants in the code (LSA, LPA) are copied to a
  constant pool in D-space when their segment is loaded; the pool lives and
  dies where the segment would have been on the stack.
* Tiny-C's `/Z` call sequence, which stores into its own CXP operands, is
  recognised and that one store writes I-space.
* Run-time errors: EXECERROR's PRINTLOCS reads the failing location through
  pointers into the code; the engine gives it a copy, so `S#, P#, I#` are
  the same as in the normal layout. Returning from the error (ESC) works.
* **Unit 64** reads and writes I-space as data: `UNITREAD(64, buf, n, addr)`
  / `UNITWRITE(64, buf, n, addr)` copy n bytes from / to I-space at addr
  (the block argument); `UNITCLEAR(64)` does nothing; IORESULT 0. In every
  other layout, and in Z80 mode, unit 64 is a bad unit (IORESULT 2).
* Assembly-language procedures cannot run (as with memory reclaimed).
* Options > Verify P-System runs in the normal layouts as before (its
  reference logs are unchanged); the Harvard layout is verified by
  `validation/run_all.sh` with `VERIFY_HARVARD=1`.

## Z80 mode's coprocessor (version 1.93)

The Z80 interpreter (SYSTEM.MICRO) has no 8-byte doubles (CSP 100..137), no
CALLI (CSP 138), and this build has no SIN, COS, LOG, ATAN, LN, EXP, SQT for
4-byte reals (CSP 25..31 go to `JP NOTIMP`). In Z80 mode the engine now does
exactly those CSPs with the same native code as P-Code mode, as a math
coprocessor board would: when the interpreter's CSP routine (0x15B8) is about
to dispatch one of them, the engine runs it and returns to BACK1. Every other
P-code, every other CSP included, is still the Z80 interpreter, and nothing on
the disks changes. So Tiny-C programs with doubles, `float` functions or
function pointers run in Z80 mode with bit-identical results, and Tiny-C's
`-z` / `/Z` is no longer needed for Z80 mode (it still works). Programs that
use none of these CSPs run exactly as before (the Verify reference logs are
unchanged). `PSystemEngine::SetZ80Coprocessor(false)` (run_verify:
`VERIFY_NOCOPROC=1`) turns it off. There is no menu item; it is always on.

## Pause / Resume

Options > **Pause** (the first item) stops the P-System where it is; the item
then reads **Resume**. Keys typed while paused are kept and delivered after
Resume. The status bar says "Paused". Not available during Verify P-System.

## Disk units 11 to 14 (versions 1.94 and 1.95)

File > Open Unit #11, #12, #13 and #14 work like #9 and #10: the volume shows
in the Filer's V(olumes, `#11:` .. `#14:` and the volume name work
everywhere, and Options > Import / Export offer them. Units 13 and 14 need
BIGGY revision 1.07 (UCSD-Pascal-Volumes): its operating system is built
with `MAXUNIT = 14` and disk units `[4,5,9..14]` (U134.4_OS_SOURCE_v1.07);
an older SYSTEM.PASCAL has a unit table only up to 12 and reports units 13
and 14 as "No such vol on-line".

Give every mounted volume its own name. II.0 finds a volume by name when it
closes a new file, so with two volumes of the same name on line (two copies
of an empty disk, say) a code file written to the first of them fails with
"IO error: vol went off-line". The 1.06 operating system does the same; it
is how II.0 works, not a 1.95 change.

## File menu paths (version 1.96)

The File menu shows the image file of each mounted unit after its Open and
Unmount items. Windows right-aligns that column, so each path is padded on
the right (spaces and hair spaces, measured in the menu font) to the width of
the longest one: the paths start in the same place and the blank space is on
their right.

## Least free memory (versions 1.97 to 1.99)

Options > **Track Least Free Memory** (remembered; no restart needed) makes
the engine watch, at every P-code instruction and in both modes, the room
between the stack and the heap -- SP - NP, the memory a program has left
before *STK OFLOW* -- and keep the least it has seen. The item below it
shows that worst case, **Least Free Memory: N words**; clicking it starts
again from the room at that moment (so: click, run the program, open the
menu). While tracking, the status bar shows it too, with the room now:
"least free N words (now M)".

The figure is on the same scale as the P-System's own MEMAVAIL ("words
free"): a program that reads MEMAVAIL at its deepest point shows N exactly
that number less what its own printf pushes. Each instruction's pushes
inside itself (a few words) are not seen, nor is the 2 KB directory buffer
the operating system reads above the heap when it opens a file (that buffer
is not part of the heap, and the stack may use the memory again).
run_verify: `VERIFY_LOWWATER=1` prints "least free memory: N words" and the
script step it was reached in; `=2` also the least of every step.

Version 1.97 gave the two menu items the command IDs of File > Open Unit
#11 and #12 (32828, 32829): they showed as "Open Unit #11/#12" and opened
those units.  1.98 gives them IDs of their own (32836, 32837).

Version 1.99 lets a program measure itself the same way, through the
word SYSCOM^.EXPANSION[8] (address 0x0318 in the P-machine's variable
block; the operating system does not use it).  The program stores
0xFFFF there; from its next P-code instruction on the engine keeps in
that word the least free memory, in words (SP - NP, at least 1), since
then; storing 0 stops it.  Until it sees 0xFFFF the engine leaves the
word alone, and without the engine (another machine) the word keeps the
0xFFFF, so the program can tell.  It is always on, independent of the
menu option.  Tiny-C: memleast_start(), memleast(), memleast_stop() in
psys.h; the Tiny-C compiler's "(N words free)" after each pass is that
pass's least, so the least of them is what the status bar shows.

## Volume files changed while the emulator runs

The emulator reads each volume image into memory when it mounts it and
writes every block the P-System writes straight back into the file. If the
file is replaced while the emulator runs -- a `git pull` of the volumes, a
copy from Explorer, another program -- those writes would put the old
copy's blocks (the directory above all) into the new file.

So the emulator records each image file's size and last-write time when it
loads or writes it, and refuses to write to a file that has changed since
(`FlushDriveRegion`). A warning names the unit and the file; from then on
nothing the P-System writes to that unit is saved. Re-open the unit (File >
Open Unit #n) or restart the emulator to load the new file.

Best practice: close the emulator before pulling new volumes.

## Linux test runner (`verify/run_verify.cpp`)

Boots the real engine through `verify/linux-shim` and types a keyboard
script (`WAIT "text"` / `TYPE "keys"`):

    g++ -std=c++17 -O2 -w -I verify/linux-shim -I UCSDPascal -o run_verify \
        verify/run_verify.cpp UCSDPascal/PSystemEngine.cpp UCSDPascal/z80.cpp -lpthread
    run_verify <data dir with Big_Disk.BLK, pascal.bin> <unit5.BLK> <unit9.BLK> <script> native|z80 <work dir> [trace] [max s]

Environment options: `VERIFY_RECLAIM=1` (reclaim the Z80 interpreter's
memory), `VERIFY_UNIT10=path` .. `VERIFY_UNIT14=path` (mount units #10..#14 in place),
`VERIFY_IMPORT=unit:path` (Options > Import File; with
`VERIFY_IMPORT_STEP=n` at script step n, while the system runs),
`VERIFY_TOUCH=n:path` (at step n, change the file's last-write time from
outside, as a pull would -- tests the protection above),
`VERIFY_NOCOPROC=1` (Z80 mode without the coprocessor; Z80 runs print a
"z80 coprocessor:" line with the number of CSPs it did),
`VERIFY_HARVARD=1` (with `VERIFY_RECLAIM=1`: Harvard mode; prints a
"harvard layout:" line), `VERIFY_PAUSE=n:ms` (at step n, Pause for ms
milliseconds, report how many P-code instructions ran meanwhile, Resume),
`VERIFY_HOSTCLOCK=1` (the PC's date and time, as the GUI's "PC date and
time" option; off by default so runs are repeatable -- with it the
compiler prints its time and lines/min),
`VERIFY_LOWWATER=1` (the least free memory, SP - NP, of the run and the
step it was reached in; `=2` also each step's).


# Engine PR: CSP 138 (CALLI), version 1.91

**Repository:** UCSD-Pascal_Windows_Emulator  **Base:** 7ac58ed (version 1.90)  **Branch:** `calli-csp138`

## What
Adds CSP 138, CALLI: call through a function pointer. Tiny-C used to compile such a call to a
sequence that stored the function value into the operand bytes of the following `CXP` at run
time (self-modifying code). It now compiles to `<arguments>; <function value>; CSP 138`.
CALLI pops the function value (`seg | proc << 8`) and does what `CXP seg,proc` would; the
return IPC is the instruction after the CSP.

## How
* `NativeCalli.inc` (new, included in the CSP case before `NativeDouble.inc`): another segment
  goes through `NativeCxp.inc` (segment 0, resident, or read from disk first); the same segment
  through CIP's BLDMSCW plus the static-link fix-up.
* Cases the Z80 CXP would hand to real Z80 code (assembly-language procedure, stack overflow,
  a segment the native loader declines) have no Z80 CSP to fall back on: they raise STKOVR/NOPROC.
* Refactor of shared code, behaviour unchanged for CXP: `NativeBldmscw()` becomes a wrapper around
  `NativeBldmscwProc(procNum, retIpc)`; `NativeCxp.inc` takes its operands from the includer
  (`cxProcN`, `cxIpc1`, `cxIpc2`, `cxWhy`) and its one label is a macro (`CXP_POST`), because it is
  now included twice (labels have function scope). The linux-harness copies mirror this.
* Native P-Code mode only, like the double CSPs: the Z80 interpreter's CSPTBL has 41 entries.

## Testing (Linux, g++)
* Tiny-C (https://github.com/Bio-Printer/UCSD-TinyC): 18 tests incl. new `funcptr`, `funcseg`
  (CALLI into unloaded segments, recursion through a pointer); crosscheck, selfcompile, voltest,
  tcverify native and z80 (z80 with `-z`).
* Boot trace-diff (`linux-harness/harness`): output identical before and after.
* `validation/pmachine_focused_diff.patch.py` and `functional_tests.py`: pass, as on 1.90.
* Stack overflow through CALLI reports `*STK OFLOW*`, like a direct call.

## Not tested / notes
* Not built with MSVC / Visual Studio (g++ only). `NativeCalli.inc` is not listed in the .vcxproj,
  like `NativeDouble.inc`.
* `validation/run_all.sh` already fails its first check on 1.90 (`NativeCsp.inc` differs between
  `UCSDPascal/` and `linux-harness/`), so I ran its other checks by hand. It will also flag
  `NativeCalli.inc`, which is deliberately not mirrored (no Z80 baseline): see linux-harness/README.md.
* The error paths for an assembly-language target and a declined segment load are untested.

* 
