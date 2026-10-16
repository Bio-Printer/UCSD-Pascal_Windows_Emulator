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

## Page Up ... Delete and the mouse wheel as one code each (versions 2.00, 2.01)

Page Up, Page Down, Home, End, Insert and Delete have no setting in
SYSTEM.MISCINFO, so the emulator types the UCSD editor's own commands for
them (Page Down `>P`, Page Up `<P>`, Home `JB`, End `JE`, Insert `I`,
Delete `D` ^U ^C; see EDITOR_KEYS.md). Other programs -- Tiny-C's vi --
take those letters as their own commands. From version 2.00 a program can
ask for one code per key instead: while the word SYSCOM^.EXPANSION[1]
(address 0x030A in the P-machine's variable block; the operating system
does not use it) is 25605, these keys send

| Key | Code |
|---|---|
| Home | 0x84 |
| End | 0x85 |
| Insert | 0x86 |
| Delete | 0x87 |
| Page Up | 0x88 |
| Page Down | 0x89 |

The program stores 25605 when it starts and 0 when it ends, so the editor
and everything else get the keys as before.

From version 2.01 the mouse wheel takes part too: while the word is 25605
each notch away from you sends 0x96 and each notch towards you 0x97
(finer wheels and touchpads add up to a notch first); otherwise the wheel
sends nothing. Tiny-C: `KEY_WHEELUP` and `KEY_WHEELDN` in psys.h; vi
scrolls 3 lines a notch (as ^Y / ^E), in command mode.

From version 2.02 the word may also be 25606: the same keys and wheel, and
the cursor drawn as a block (its cell inverted: green ground, the
character black) instead of the line under it, so it is easy to find.
When the program puts 0 back the cursor is a line again. Tiny-C:
`PX_KEYS_BLOCK` in psys.h; vi asks for it (the shell, with 25605, does
not). Tiny-C: `PX_KEYS` and
`KEY_HOME` ... `KEY_PGDN` in psys.h; vi (TOOLS:VI.CODE) does it, and its
Page Up/Down page, Home/End go to the start/end of the line, Insert
inserts and Delete deletes the character under the cursor.

## Scroll codes (version 2.03)

A program that repaints a screen after it scrolled by moving the cursor and
rewriting every row is slow. From version 2.03 the console scrolls on request.
The program sets the word SYSCOM^.EXPANSION[1] to **25607** (the keys and the
block cursor of 25606, and the scroll codes); the emulator answers by changing
it to **25608**, so the program knows this emulator has them (an older one
leaves 25607 and does not take the keys: the program then stores 25606
instead). While the word is 25607 or 25608 the console takes (ESC is 27):

| Sequence | Does |
|---|---|
| `ESC [ n S` | scroll the region up n lines (1 if n is left out): the rows move up, blank rows come in at the bottom |
| `ESC [ n T` | scroll the region down n lines |
| `ESC [ top ; bottom r` | the scroll region, rows counted from 1 (`ESC [ r`: the whole screen) |

The cursor does not move; the screen starts with the whole screen as the
region. (These are the ANSI / VT100 codes for scrolling, not the VT52's, which
has none; the one-byte codes of the system's own console are unchanged.) Any
other byte after ESC is an ordinary byte, as before. Tiny-C: `PX_KEYS_SCROLL`
in psys.h; vi uses them for Page Down, Page Up and a cursor that scrolls the
screen.

## Keys waiting (version 2.04)

A program cannot see the keys typed ahead (UNITBUSY is always false here), so it cannot
know it is behind the typist.  From version 2.04 the program sets
SYSCOM^.EXPANSION[1] to **25609** (everything 25607 asks for, and the count of keys
waiting); the emulator answers **25610** and keeps the number of keys not yet read by
the program (0..255) in SYSCOM^.EXPANSION[2], updated as keys arrive from Windows and as the
program reads them.  An older emulator leaves 25609: the program then stores 25607.
Tiny-C: `PX_KEYS_AHEAD` / `PX_AHEAD_OK` in psys.h; vi paints the screen only when no key
is waiting (typing, `j`, the mouse wheel: one repaint after a burst).  The verify runner
types a whole TYPE at once with `VERIFY_BURST=1`.

## The break key (version 2.05)

The system's break key (`CRTINFO.BREAK` in SYSTEM.MISCINFO, set with SETUP) did nothing: the
interpreter has the run-time error 8, "User break" (`UBREAK`), but nothing called it.  From
version 2.05 a key typed while a program is running and has not read a key for 1.5 seconds,
if it is the break character and `MISCINFO.NOBREAK` is not set, is not a key: at the next
P-code instruction the system takes the error, "Program interrupted by user", and returns to
the command level (a long library call, such as a big block move, finishes first).  A key
typed while the program waits for one, or soon after it read one (a busy editor), is an
ordinary key.  BIGGY 1.24 has the backquote (`` ` ``) as its break key.  The verify runner
has `TYPENOW "keys"` (type at once, while the program runs) and `VERIFY_BREAKQUIET=ms`.

## Ctrl-S and Ctrl-Q stop and start the console output (version 2.06)

A long printout can be held: the stop key (`CRTINFO.STOP` in SYSTEM.MISCINFO, ^S on BIGGY) typed while a
program is running and not waiting for a key stops its console output -- the program waits in its next
write -- and ^Q starts it again (the arrow keys' Left sends ^Q too).  They are not keys for the program
then; typed while it waits for a key, both are ordinary keys, as before.  The status bar says
"Output stopped (^S) -- ^Q goes on".  The verify runner has `QUIET "ms"`: the console output must not
grow for that long.

## Import and Export reach into sub-volumes (version 2.07)

A sub-volume is a data file whose own blocks 2..5 hold a UCSD directory -- a volume inside a file,
its block numbers counted from the file's first block (Tiny-C's shell makes one with `MKDIR NAME
[BLOCKS]`; its files are `VOL:/A/B/NAME`).  Options > **Import File** has an "Into" list below the
units: the chosen unit's volume and every sub-volume on it (`BIGGY:`, `BIGGY:/A`, `BIGGY:/A/B`),
and the file goes into the one picked.  Options > **Export File** lists a sub-volume as `A/` (kind
`SUBV`): a double-click opens it, `..` goes back up, and the line above the list says where you
are (`Files on BIGGY:/A/B`); Export on a sub-volume itself copies the whole file, and a
double-click on a file exports it.  The engine: `GetSubVolumes(unit)`, and `GetVolumeDirectory`,
`ImportFileToVolume`, `ExportFileFromVolume` take the sub-volume's first block (0: the volume).
The verify runner: `VERIFY_IMPORT=unit/A/B:path`, `VERIFY_EXPORT=unit[/A/B]:NAME:path` (when the
script has ended) and `VERIFY_SUBVOLS=1` (lists every sub-volume).

## The boot chooses the operating system (version 2.08)

BIGGY holds two operating systems: SYSTEM.PASCAL (the Pascal one) and SYSTEM.C (the same
system written in C, [UCSD-SYSTEM-C](https://github.com/Bio-Printer/UCSD-SYSTEM-C)).  Its
SYSTEM.MICRO has a boot that asks "Type any key within 2 seconds to boot SYSTEM.PASCAL
instead of SYSTEM.C:" and boots SYSTEM.C unless a key is typed (the key is not passed on); when
the one chosen is not on the disk, it boots the other.  The boot is the interpreter's BOOT with
two instructions changed and a routine in the unused end of the file (UCSD-SYSTEM-C
`tools/boot.py`, SYS-C:BOOTC.TEXT).  For it the emulator has a key wait: `OUT (2),n` returns
when a key is typed, or after n tenths of a second (port 2; the key stays queued for CONST and
CONIN).  The native boot of P-Code mode does the same as that code when it finds it (the
Reclaim and Harvard layouts, which do not load SYSTEM.MICRO, look at the disk's), so memory
and registers at the first P-code instruction are those of the Z80 boot.  Any other
SYSTEM.MICRO boots SYSTEM.PASCAL as before.  A Verify run does not wait (it reads no clock):
the verify runner waits with `VERIFY_BOOTWAIT=1`, and a script's first `TYPENOW " "` then
chooses SYSTEM.PASCAL.

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
`VERIFY_IMPORT=unit[/A/B]:path` (Options > Import File, into sub-volume A/B; with
`VERIFY_IMPORT_STEP=n` at script step n, while the system runs),
`VERIFY_EXPORT=unit[/A/B]:NAME:path` (Options > Export File, after the script),
`VERIFY_SUBVOLS=1` (list the sub-volumes of every unit),
`VERIFY_TOUCH=n:path` (at step n, change the file's last-write time from
outside, as a pull would -- tests the protection above),
`VERIFY_NOCOPROC=1` (Z80 mode without the coprocessor; Z80 runs print a
"z80 coprocessor:" line with the number of CSPs it did),
`VERIFY_HARVARD=1` (with `VERIFY_RECLAIM=1`: Harvard mode; prints a
"harvard layout:" line), `VERIFY_PAUSE=n:ms` (at step n, Pause for ms
milliseconds, report how many P-code instructions ran meanwhile, Resume),
`VERIFY_BOOTWAIT=1` (the boot's key wait, `OUT (2)`, waits as in the GUI: 2.08),
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
