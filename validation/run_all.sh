#!/bin/bash
# Full validation suite for the native-C P-code conversion effort.
# Run from this directory (validation/). See README.md for what each
# check actually verifies and why.
#
# Usage: ./run_all.sh [quick]
#   ./run_all.sh          -- full suite (50,000,000-instruction P-machine check)
#   ./run_all.sh quick    -- fast smoke check (5,000,000 instructions throughout)

set -uo pipefail
cd "$(dirname "$0")"

QUICK=0
if [ "${1:-}" = "quick" ]; then
    QUICK=1
fi

FAIL=0

echo "=== Shared include files identical in UCSDPascal/ and linux-harness/ ==="
for f in ../UCSDPascal/*.inc ../UCSDPascal/UcsdReal.h ../UCSDPascal/PCodeOpcodes.h; do
    if ! cmp -s "$f" "../linux-harness/$(basename "$f")"; then
        echo "FAIL: $(basename "$f") differs between UCSDPascal/ and linux-harness/ -- copy the UCSDPascal one over"
        exit 1
    fi
done
echo "OK"
echo

echo "=== Building harness and remove_file from ../linux-harness ==="
g++ -O2 -std=c++17 -o /tmp/validation_harness ../linux-harness/harness.cpp ../linux-harness/z80.cpp
if [ $? -ne 0 ]; then
    echo "BUILD FAILED: harness.cpp"
    exit 1
fi
g++ -O2 -std=c++17 -o ../linux-harness/remove_file ../linux-harness/remove_file.cpp ../linux-harness/z80.cpp
if [ $? -ne 0 ]; then
    echo "BUILD FAILED: remove_file.cpp"
    exit 1
fi
echo "builds OK"
echo

echo "=== Check 1: boot trace-diff, default toggle (preserveZ80RegisterCompat=true) ==="
if [ $QUICK -eq 1 ]; then
    BUDGET=5000000
else
    BUDGET=5000000
fi
OUT=$(/tmp/validation_harness ../data "$BUDGET" 2>&1)
echo "$OUT" | grep -i "halted\|Sequences"
if echo "$OUT" | grep -qi "MISMATCH"; then
    echo "FAIL: mismatch found in default-toggle trace-diff"
    echo "$OUT" | grep -A 6 -i "MISMATCH"
    FAIL=1
else
    echo "PASS: no mismatch (matches this project's known-good baseline -- see README.md)"
fi
echo

echo "=== Check 2: P-machine-focused trace-diff, toggle OFF (preserveZ80RegisterCompat=false) ==="
if [ $QUICK -eq 1 ]; then
    PBUDGET=5000000
else
    PBUDGET=50000000
fi
if python3 pmachine_focused_diff.patch.py "$PBUDGET" 2>&1 | tail -5; then
    : # script prints PASSED/FAILED itself; exit code below is what matters
fi
python3 pmachine_focused_diff.patch.py "$PBUDGET" > /tmp/pmachine_check_output.txt 2>&1
if [ $? -ne 0 ]; then
    echo "FAIL: P-machine-focused check failed -- see /tmp/pmachine_check_output.txt"
    FAIL=1
else
    echo "PASS"
fi
echo

echo "=== Check 3: functional tests (real compiled Pascal programs) ==="
if python3 functional_tests.py; then
    echo "PASS"
else
    echo "FAIL: one or more functional tests failed (see above)"
    FAIL=1
fi
echo

echo "=== Check 4: functional tests again, with preserveZ80RegisterCompat=false ==="
sed 's/bool preserveZ80RegCompat = true;/bool preserveZ80RegCompat = false;/' ../linux-harness/remove_file.cpp > /tmp/remove_file_nocompat.cpp
if ! grep -q "bool preserveZ80RegCompat = false;" /tmp/remove_file_nocompat.cpp; then
    echo "FAIL: could not flip the default in remove_file.cpp (declaration changed?)"
    FAIL=1
elif ! g++ -O2 -std=c++17 -I ../linux-harness -o /tmp/remove_file_nocompat /tmp/remove_file_nocompat.cpp ../linux-harness/z80.cpp; then
    echo "FAIL: build of toggle-off remove_file failed"
    FAIL=1
elif REMOVE_FILE_BIN=/tmp/remove_file_nocompat python3 functional_tests.py; then
    echo "PASS"
else
    echo "FAIL: functional tests differ with the toggle off"
    FAIL=1
fi
echo

echo "=== Check 5: Windows -> UCSD .TEXT import conversion (UcsdText.h) ==="
if python3 ucsdtext_check.py | tail -4; then echo "PASS"; else echo "FAIL: text conversion"; FAIL=1; fi
echo

echo "=== Check 6: native real arithmetic / TNC / RND / POT / EFJ / NFJ vs the real Z80 routines (fptest) ==="
if [ $QUICK -eq 1 ]; then FPN=20000; else FPN=200000; fi
if g++ -O2 -std=c++17 -I ../linux-harness -o /tmp/validation_fptest ../linux-harness/fptest.cpp ../linux-harness/z80.cpp \
   && /tmp/validation_fptest ../data $FPN 2>/dev/null | grep -v "^\[" | grep -v "^$" | tail -14; then
    if /tmp/validation_fptest ../data 2000 2>/dev/null | grep -q "ALL OPERATIONS MATCH"; then echo "PASS"; else echo "FAIL: fptest mismatches"; FAIL=1; fi
else
    echo "FAIL: fptest build/run"; FAIL=1
fi
echo

if [ $QUICK -eq 0 ]; then
echo "=== Check 7: Verify P-System -- record the Z80 reference, then verify a native run against it (~1.5 min) ==="
if g++ -std=c++17 -O2 -I ../verify/linux-shim -I ../UCSDPascal -o /tmp/validation_run_verify ../verify/run_verify.cpp ../UCSDPascal/PSystemEngine.cpp ../UCSDPascal/z80.cpp -lpthread; then
    rm -rf /tmp/vv_ref /tmp/vv_nat /tmp/vv_ref.pcl
    /tmp/validation_run_verify ../data ../verify/SOURCE.BLK ../verify/COMPASM.BLK ../verify/VERIFY.SCRIPT z80 /tmp/vv_ref record:/tmp/vv_ref.pcl 900 2>/dev/null | tail -2
    if /tmp/validation_run_verify ../data ../verify/SOURCE.BLK ../verify/COMPASM.BLK ../verify/VERIFY.SCRIPT native /tmp/vv_nat compare:/tmp/vv_ref.pcl 900 2>/dev/null | tee /tmp/vv_nat.out | tail -2 && grep -q "VERIFY SCRIPT COMPLETED" /tmp/vv_nat.out && ! grep -q MISMATCH /tmp/vv_nat.out; then
        echo "   output disks (native vs Z80 run), by content:"
        okd=1
        for f in VERIFY_SOURCE.BLK VERIFY_COMPASM.BLK VERIFY_BOOT.BLK; do
            printf "     %-20s " $f; python3 ../verify/compare_outputs.py /tmp/vv_ref/$f /tmp/vv_nat/$f | head -3 || okd=0
        done
        if [ $okd -eq 1 ]; then echo "PASS"; else echo "FAIL: output files differ"; FAIL=1; fi
    else
        echo "FAIL: native run differs from the Z80 reference"; head -20 /tmp/vv_nat.out; FAIL=1
    fi
    rm -f /tmp/vv_ref.pcl
else
    echo "FAIL: could not build run_verify"; FAIL=1
fi
echo
fi

if [ $QUICK -eq 0 ] && [ -x /tmp/validation_run_verify ]; then
echo "=== Check 8: the whole Verify build with the Z80 interpreter's memory reclaimed (native mode) ==="
rm -rf /tmp/vv_rec
VERIFY_RECLAIM=1 /tmp/validation_run_verify ../data ../verify/SOURCE.BLK ../verify/COMPASM.BLK ../verify/VERIFY.SCRIPT native /tmp/vv_rec "" 900 2>/dev/null | tee /tmp/vv_rec.out | tail -2
okr=1
grep -q "VERIFY SCRIPT COMPLETED" /tmp/vv_rec.out && grep -q "reclaimed: yes$" /tmp/vv_rec.out || okr=0
for f in VERIFY_SOURCE.BLK VERIFY_COMPASM.BLK VERIFY_BOOT.BLK; do
    printf "     %-20s " $f; python3 ../verify/compare_outputs.py /tmp/vv_ref/$f /tmp/vv_rec/$f --allow-code-bytes 3 | head -3 || okr=0
done
echo "     free memory reported (normal vs reclaimed): $(grep -o 'Smallest available space = [0-9]*' /tmp/vv_nat/transcript.txt | head -1 | grep -o '[0-9]*$') vs $(grep -o 'Smallest available space = [0-9]*' /tmp/vv_rec/transcript.txt | head -1 | grep -o '[0-9]*$') words"
if [ $okr -eq 1 ]; then echo "PASS"; else echo "FAIL: reclaimed-memory build"; FAIL=1; fi
echo
fi

if [ $QUICK -eq 0 ] && [ -x /tmp/validation_run_verify ]; then
echo "=== Check 9: run-time errors and regression programs -- 24 programs (verify/ERRORS.SCRIPT) ==="
rm -rf /tmp/vv_eref /tmp/vv_enat /tmp/vv_erec /tmp/vv_eref.pcl
/tmp/validation_run_verify ../data ../verify/SOURCE.BLK ../verify/ERRTEST.BLK ../verify/ERRORS.SCRIPT z80 /tmp/vv_eref record:/tmp/vv_eref.pcl 600 2>/dev/null | tail -1
/tmp/validation_run_verify ../data ../verify/SOURCE.BLK ../verify/ERRTEST.BLK ../verify/ERRORS.SCRIPT native /tmp/vv_enat compare:/tmp/vv_eref.pcl 600 2>/dev/null | tee /tmp/vv_enat.out | tail -1
VERIFY_RECLAIM=1 /tmp/validation_run_verify ../data ../verify/SOURCE.BLK ../verify/ERRTEST.BLK ../verify/ERRORS.SCRIPT native /tmp/vv_erec "" 600 2>/dev/null | tee /tmp/vv_erec.out | tail -1
nrep=$(grep -c "Value range error\|String overflow\|Divide by zero\|Floating point error\|IO error\|STK OFLOW\|Exit from uncalled\|No proc in seg-table" /tmp/vv_erec/transcript.txt)
echo "     error reports with the interpreter's memory reclaimed: $nrep (expected 18; HALT prints none; E11 then halts the P-System)"
ncmp=$(grep -c "REALCMP: 7 of 7 comparisons true\|BYTECMP: 7 of 7 comparisons true" /tmp/vv_erec/transcript.txt)
echo "     REAL / PACKED ARRAY OF CHAR comparison programs with memory reclaimed: $ncmp of 2 report 7 of 7"
ndeep=$(grep -c "DEPTH 70: 1\|depth 70: ok\|DEEPCIP: 101" /tmp/vv_erec/transcript.txt)
echo "     deep CXP / CIP programs (DEEPCXP, DEEPC, DEEPCIP) with memory reclaimed: $ndeep of 3 correct"
nstr=$(grep -c "STRCONST: 13 OF 13 CHECKS TRUE" /tmp/vv_erec/transcript.txt)
echo "     string / packed-array constants (STRCONST) with memory reclaimed: $nstr of 1 report 13 of 13"
# HCODE: the error's location (S#, P#, I#) must be the same in every layout
# (the Harvard layout shows EXECERROR a copy, see PSystemEngine::SetHarvard);
# unit 64 is I-space with VERIFY_HARVARD, a bad unit otherwise.
grep -a "S# " /tmp/vv_enat/transcript.txt > /tmp/vv_enat.locs
grep -a "S# " /tmp/vv_erec/transcript.txt > /tmp/vv_erec.locs
nloc=$(wc -l < /tmp/vv_enat.locs)
if cmp -s /tmp/vv_enat.locs /tmp/vv_erec.locs; then sameloc=1; else sameloc=0; fi
echo "     error locations (S#, P#, I#) with memory reclaimed the same as in the normal layout: $sameloc ($nloc reports)"
if [ -n "${VERIFY_HARVARD:-}" ] && [ "${VERIFY_HARVARD:-}" != "0" ]; then
    nhc=$(grep -c "HCODE RESUMED IN SEGMENT\|HCODE: I-SPACE 64..66 = 205 0 2\|HCODE: WRITE 0, READ BACK 8 OF 8\|HCODE: CLEAR 0\|HCODE DONE" /tmp/vv_erec/transcript.txt)
else
    nhc=$(grep -c "HCODE RESUMED IN SEGMENT\|HCODE: NO I-SPACE, IORESULT 2\|HCODE DONE" /tmp/vv_erec/transcript.txt); nhc=$((nhc + 2))
fi
echo "     error resumed in a segment, code unit 64 (HCODE) with memory reclaimed: $nhc of 5 lines as expected"
if grep -q "VERIFY SCRIPT COMPLETED" /tmp/vv_enat.out && ! grep -q MISMATCH /tmp/vv_enat.out && grep -q "VERIFY SCRIPT COMPLETED" /tmp/vv_erec.out && [ "$nrep" -eq 18 ] && [ "$ncmp" -eq 2 ] && [ "$ndeep" -eq 3 ] && [ "$nstr" -eq 1 ] && [ "$sameloc" -eq 1 ] && [ "$nloc" -eq 17 ] && [ "$nhc" -eq 5 ]; then
    echo "PASS"
else
    echo "FAIL: run-time error handling"; head -12 /tmp/vv_enat.out; FAIL=1
fi
rm -f /tmp/vv_eref.pcl
echo
fi

if [ $QUICK -eq 0 ] && [ -x /tmp/validation_run_verify ]; then
echo "=== Check 10: native boot (P-Code mode) -- memory and registers at the first P-code instruction vs the Z80 boot ==="
for m in z80 native; do
    rm -rf /tmp/vv_boot_$m
    VERIFY_STOP_AT=1 VERIFY_DUMP_MEM=/tmp/vv_boot_$m.bin /tmp/validation_run_verify ../data ../verify/SOURCE.BLK ../verify/COMPASM.BLK ../verify/VERIFY.SCRIPT $m /tmp/vv_boot_$m record:/tmp/vv_boot_$m.pcl 60 2>/dev/null | grep "native boot"
done
if cmp -s /tmp/vv_boot_z80.bin /tmp/vv_boot_native.bin && diff -q /tmp/vv_boot_z80.bin.regs /tmp/vv_boot_native.bin.regs >/dev/null; then
    echo "     all 65536 bytes and every Z80 register identical"; echo "PASS"
else
    echo "FAIL: the native boot differs from the Z80 boot"; cmp /tmp/vv_boot_z80.bin /tmp/vv_boot_native.bin | head -3; diff /tmp/vv_boot_z80.bin.regs /tmp/vv_boot_native.bin.regs; FAIL=1
fi
rm -f /tmp/vv_boot_*.pcl
echo
fi

if [ $QUICK -eq 0 ] && [ -x /tmp/validation_run_verify ]; then
echo "=== Check 11: P-Code mode with nothing Z80 -- no pascal.bin, no SYSTEM.MICRO on the boot disk ==="
rm -rf /tmp/vv_nz /tmp/vv_nzd && mkdir -p /tmp/vv_nzd
python3 - << 'PYEOF'
D = 1024
img = bytearray(open('../data/Big_Disk.BLK', 'rb').read())
w = lambda o: img[o] | (img[o + 1] << 8)
n = w(D + 16)
names = [bytes(img[D + 26 * i + 7:D + 26 * i + 7 + img[D + 26 * i + 6]]).decode('latin1') for i in range(1, n + 1)]
i = names.index('SYSTEM.MICRO') + 1
img[D + 26 * i:D + 26 * n] = img[D + 26 * (i + 1):D + 26 * (n + 1)]; img[D + 26 * n:D + 26 * (n + 1)] = bytes(26)
img[D + 16] = (n - 1) & 0xFF; img[D + 17] = (n - 1) >> 8
open('/tmp/vv_nzd/Big_Disk.BLK', 'wb').write(img)
PYEOF
VERIFY_RECLAIM=1 /tmp/validation_run_verify /tmp/vv_nzd ../verify/SOURCE.BLK ../verify/COMPASM.BLK ../verify/VERIFY.SCRIPT native /tmp/vv_nz "" 600 2>/dev/null | tee /tmp/vv_nz.out | tail -1
oknz=1
grep -q "VERIFY SCRIPT COMPLETED" /tmp/vv_nz.out && grep -q "native boot: yes" /tmp/vv_nz.out || oknz=0
# the disks the build writes to (the boot disk here lacks SYSTEM.MICRO on purpose)
for f in VERIFY_SOURCE.BLK VERIFY_COMPASM.BLK; do
    printf "     %-20s " $f; python3 ../verify/compare_outputs.py /tmp/vv_ref/$f /tmp/vv_nz/$f --allow-code-bytes 3 > /tmp/vv_nz.cmp || oknz=0; head -1 /tmp/vv_nz.cmp
done
echo "     free memory reported: $(grep -o 'Smallest available space = [0-9]*' /tmp/vv_nz/transcript.txt | head -1 | grep -o '[0-9]*$') words (normal boot 3867)"
if [ $oknz -eq 1 ]; then echo "PASS"; else echo "FAIL: P-Code mode without pascal.bin / SYSTEM.MICRO"; FAIL=1; fi
echo
fi

if [ $QUICK -eq 0 ] && [ -x /tmp/validation_run_verify ]; then
echo "=== Check 12: reclaimed-memory reference logs -- recorded, then a second run must reproduce every record ==="
ok12=1
for sc in VERIFY:COMPASM ERRORS:ERRTEST; do
    scr=${sc%%:*}; vol=${sc##*:}
    rm -rf /tmp/vv_rrA /tmp/vv_rrC
    VERIFY_RECLAIM=1 /tmp/validation_run_verify ../data ../verify/SOURCE.BLK ../verify/$vol.BLK ../verify/$scr.SCRIPT native /tmp/vv_rrA record:/tmp/vv_rrA.pcl 600 2>/dev/null | grep -q "SCRIPT COMPLETED" || ok12=0
    # the second run compared against the first: determinism AND verification in one pass
    VERIFY_RECLAIM=1 /tmp/validation_run_verify ../data ../verify/SOURCE.BLK ../verify/$vol.BLK ../verify/$scr.SCRIPT native /tmp/vv_rrC compare:/tmp/vv_rrA.pcl 600 2>/dev/null > /tmp/vv_rrC.out
    if grep -q "SCRIPT COMPLETED" /tmp/vv_rrC.out && ! grep -q MISMATCH /tmp/vv_rrC.out; then
        echo "     $scr.SCRIPT: recorded $(($(stat -c%s /tmp/vv_rrA.pcl) / 25)) instructions; a second run reproduced every record"
    else
        echo "     $scr.SCRIPT: the second run DIFFERS from the recording"; head -8 /tmp/vv_rrC.out; ok12=0
    fi
    rm -f /tmp/vv_rrA.pcl
done
if [ $ok12 -eq 1 ]; then echo "PASS"; else echo "FAIL: reclaimed-memory reference logs"; FAIL=1; fi
echo
fi

if [ $QUICK -eq 0 ] && [ -x /tmp/validation_run_verify ]; then
echo "=== Check 13: the Tiny-C compiler compiling FV.TEXT (deep calls between segments: CXP) ==="
rm -rf /tmp/vv_tz /tmp/vv_tn /tmp/vv_tr /tmp/vv_tz.pcl
/tmp/validation_run_verify ../data ../verify/SOURCE.BLK ../verify/CXPBUG.BLK ../verify/TINYC_FV.SCRIPT z80 /tmp/vv_tz record:/tmp/vv_tz.pcl 600 2>/dev/null | tail -1
/tmp/validation_run_verify ../data ../verify/SOURCE.BLK ../verify/CXPBUG.BLK ../verify/TINYC_FV.SCRIPT native /tmp/vv_tn compare:/tmp/vv_tz.pcl 600 2>/dev/null | tee /tmp/vv_tn.out | tail -1
VERIFY_RECLAIM=1 /tmp/validation_run_verify ../data ../verify/SOURCE.BLK ../verify/CXPBUG.BLK ../verify/TINYC_FV_FULL.SCRIPT native /tmp/vv_tr "" 600 2>/dev/null | tee /tmp/vv_tr.out | tail -1
ok13=1
grep -q "VERIFY SCRIPT COMPLETED" /tmp/vv_tn.out && ! grep -q MISMATCH /tmp/vv_tn.out || ok13=0
grep -q "VERIFY SCRIPT COMPLETED" /tmp/vv_tr.out && grep -q "Done\." /tmp/vv_tr/transcript.txt || ok13=0
echo "     compile pass: native matches the Z80 reference instruction for instruction; with memory reclaimed: $(grep -c 'Done\.' /tmp/vv_tr/transcript.txt) \"Done.\""
if [ $ok13 -eq 1 ]; then echo "PASS"; else echo "FAIL: Tiny-C FV compile"; head -8 /tmp/vv_tn.out; FAIL=1; fi
rm -f /tmp/vv_tz.pcl
echo
fi

if [ $FAIL -eq 0 ]; then
    echo "=== ALL CHECKS PASSED ==="
    exit 0
else
    echo "=== SOME CHECKS FAILED -- see above, do not package this change ==="
    exit 1
fi
