# UCSD-Pascal_Windows_Emulator

A Windows (MFC) emulator of the UCSD Pascal II.0 P-System: a Z80 machine
with native P-Code execution (`UCSDPascal/PSystemEngine.cpp` and the
`Native*.inc` files), including the 8-byte `double` CSPs 100..137
(`NativeDouble.inc`) and CSP 138, CALLI -- call through a function pointer
(`NativeCalli.inc`) -- used by Tiny-C (https://github.com/Bio-Printer/UCSD-C).

* **Build:** open `UCSDPascal.sln` in Visual Studio (x64). `data/pascal.bin`
  (the Z80 loader) is copied next to the .exe by the build.
* **Volumes:** File > Open Unit #4 / #5 / #9 / #10. The disk images live in
  https://github.com/Bio-Printer/UCSD-Pascal-Volumes (`BLK_format/`).

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
memory), `VERIFY_UNIT10=path` (mount unit #10 in place),
`VERIFY_IMPORT=unit:path` (Options > Import File; with
`VERIFY_IMPORT_STEP=n` at script step n, while the system runs),
`VERIFY_TOUCH=n:path` (at step n, change the file's last-write time from
outside, as a pull would -- tests the protection above).


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
* Tiny-C (https://github.com/Bio-Printer/UCSD-C): 18 tests incl. new `funcptr`, `funcseg`
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
