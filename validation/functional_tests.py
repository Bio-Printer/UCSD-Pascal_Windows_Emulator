#!/usr/bin/env python3
"""
Feeds each Pascal program in pascal_programs/ through the built
`remove_file` interactive-keystroke test harness (edit -> compile ->
run -> check output), and checks the console output against the exact
expected string recorded below.

Usage (from this directory):
    python3 functional_tests.py

Requires ../linux-harness/remove_file to already be built (run_all.sh
builds it; or build it yourself: g++ -O2 -std=c++17 -o remove_file
remove_file.cpp z80.cpp, from ../linux-harness/).

Exits 0 if every program's output contains its expected substring;
exits 1 and prints a diff-style report for any that don't.

Each entry below is (pascal filename in pascal_programs/, a substring
that must appear in the program's console output once run). The
substring check (not exact match) is deliberate: exact console output
includes prompts, blank lines from WRITELN's own formatting, and the
"Command:" prompt repeating after re-initialization, none of which is
the point of the check -- what matters is that the program's own
WRITELN output is correct.
"""
import subprocess
import sys
import os

HERE = os.path.dirname(os.path.abspath(__file__))
HARNESS_DIR = os.path.join(HERE, "..", "linux-harness")
DATA_DIR = os.path.join(HERE, "..", "data")
PASCAL_DIR = os.path.join(HERE, "pascal_programs")
# REMOVE_FILE_BIN env var overrides the binary -- run_all.sh uses this to
# re-run the same programs against a build with preserveZ80RegCompat=false.
REMOVE_FILE_BIN = os.environ.get("REMOVE_FILE_BIN", os.path.join(HARNESS_DIR, "remove_file"))

# (pascal filename, required substring in the console output)
CASES = [
    ("chk_transcendental_test.pas",
     "X=5\n\n 4.79426E-1\n\n 1.41421\n\n\n\nValue range error"),
    ("abs_compare_boolean_test.pas",
     "ABS(-7)=7\n\n5=3 FALSE\n\n5<>3 TRUE\n\n5>=3 TRUE\n\n5>3 TRUE\n\n"
     "5<=3 FALSE\n\n5<3 FALSE\n\nTANDF FALSE\n\nTORF TRUE"),
    ("array_record_string_test.pas",
     "100 200 300 400 500 \n\nR.A=42 R.B=99 R.C=Z\n\nS=HELLO\n\nS=HELLO WORLD"),
    ("sets_divmod_case_test.pas",
     "GREEN IN S1\n\nYELLOW NOT IN S1\n\n17 DIV 5 = 3\n\n17 MOD 5 = 2\n\n"
     "-17 DIV 5 = -4\n\n-17 MOD 5 = 3\n\ncase: RED\n\ncase: GREEN\n\n"
     "case: BLUE\n\ncase: YELLOW"),
    ("mpi_packedarray_test.pas",
     "123 * 456 = -9448\n\n-17 * 23 = -391\n\n1 2 3 0 1 2 3 0 \n\n1 2 2 0 1 2 1 0"),
    ("proc_string_set_test.pas",
     "FACT(7)=5040\n\nINNER B=115\n\nINNER B=132\n\nS=T\n\nS<>T NOW\n\n"
     "ALPHA<ALPHB\n\n5 IN [3..9]\n\n10 NOT IN [3..9]\n\n3 IN [3]"),
    ("real_set_misc_test.pas",
     "X+Y=-7.50000E-1\n\nX-Y= 3.75000\n\nX*Y=-3.37500\n\nX/Y=-6.66667E-1\n\nSQR(Y)= 5.06250\n\n"
     "ABS(Y)= 2.25000\n\n-X=-1.50000\n\nFLOAT 7= 7.00000\n\n7+X= 8.50000\n\nX+7= 8.50000\n\n"
     "ROUND(-2.5)=-3 TRUNC(9.99)=9\n\n3^10= 5.90490E4\n\n1/3= 3.33333E-1\n\n1E35= 1.00000E35\n\n"
     "SQR(12)=144\n\n5 IN A*B\n\n9 NOT IN A*B\n\nS[2]=E\n\nSEVEN"),
    ("round_trunc_segment_test.pas",
     "3 -3 2 0 1235\n\n0 3 -4 32767\n\n2 -2 32767 0\n\nIN SEGMENT 7\n\nIN SEGMENT 8"),
]


def run_program(pascal_path):
    with open(pascal_path) as f:
        program = f.read().replace("\n", "\r")
    seq = "e" + "\r" + "i" + program + chr(3) + "q" + "u" + "c" + "\r" + "r" + "\r"
    seq_bytes = seq.encode("latin1")
    result = subprocess.run(
        [REMOVE_FILE_BIN, DATA_DIR, "/tmp/functional_test_scratch.BLK", "",
         "0", "fullpage", "0", seq_bytes.decode("latin1"), "1111"],
        capture_output=True, timeout=60)
    out = result.stdout.decode("latin1", errors="replace")
    idx = out.find("Running")
    return out[idx:] if idx >= 0 else out


def main():
    if not os.path.exists(REMOVE_FILE_BIN):
        print(f"FAILED: {REMOVE_FILE_BIN} not built yet -- build it first "
              f"(see this script's own docstring, or run run_all.sh).")
        return 1

    all_ok = True
    for filename, expected in CASES:
        path = os.path.join(PASCAL_DIR, filename)
        output = run_program(path)
        if expected in output:
            print(f"PASS  {filename}")
        else:
            all_ok = False
            print(f"FAIL  {filename}")
            print(f"  expected substring: {expected!r}")
            print(f"  actual output:       {output[:600]!r}")

    if all_ok:
        print("\nALL FUNCTIONAL TESTS PASSED")
        return 0
    else:
        print("\nFUNCTIONAL TEST FAILURES ABOVE")
        return 1


if __name__ == "__main__":
    sys.exit(main())
