# UCSD-Pascal_Windows_Emulator

A Windows (MFC) emulator of the UCSD Pascal II.0 P-System: a Z80 machine
with native P-Code execution (`UCSDPascal/PSystemEngine.cpp` and the
`Native*.inc` files), including the 8-byte `double` CSPs 100..137
(`NativeDouble.inc`) used by Tiny-C (https://github.com/Bio-Printer/UCSD-C).

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
