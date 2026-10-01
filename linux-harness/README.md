# Linux test harness

This folder is **not part of the Visual Studio build** -- it won't show up
in the `.sln`/`.vcxproj` and Visual Studio has no reason to touch it. It's
a pair of standalone command-line tools that let Claude (or you) boot and
drive the real UCSD Pascal system on Linux, without MFC/Windows at all,
to verify changes to `PSystemEngine.cpp` quickly and repeatably. Every
native-C p-code opcode and BIOS-vector replacement in this project was
verified with these tools before being written into `PSystemEngine.cpp`.

Both tools reuse the exact same `z80.h`/`z80.cpp` Z80 core as the real
project (copied here so this folder builds standalone -- see "Keeping in
sync" below), plus a hand-written port-level engine that mirrors
`PSystemEngine`'s `PortIn`/`PortOut`/native-opcode logic closely enough to
be a faithful stand-in, not the real thing verbatim.

## Building

Needs nothing beyond g++ and a C++17 standard library:

```
g++ -O2 -std=c++17 -o harness harness.cpp z80.cpp
g++ -O2 -std=c++17 -o remove_file remove_file.cpp z80.cpp
```

Both tools expect `pascal.bin`, `Big_Disk.BLK`, and `Empty_Big_Disk.BLK`
in a data directory passed as their first argument -- normally `../data`
relative to this folder, i.e. the same `data/` the real project ships
with. Don't duplicate those files into this folder.

## `harness.cpp` -- boot trace-diff

Boots the same disk image twice -- once with every native-C opcode
replacement disabled (pure, unmodified Z80 execution) and once with them
all enabled -- and compares state at every single pass through `BACK`
(the p-code fetch loop, `0x03B0`). This is the standard the whole
project holds native replacements to: they must be byte-for-byte
identical to what the real Z80 code would have produced, not just
"close enough" or "produces the right answer eventually."

The comparison covers two layers:
- **Z80 registers** (`BC`/`DE`/`HL`/`SP`/`A` and the top two stack
  words). Register `A` is included specifically because it's the
  register most likely to hide a real discrepancy that functional
  testing alone won't catch -- see the gotcha about it below. These
  registers stop being meaningful once p-code opcode simulation is
  fully replaced by native C (there's no Z80 executing to hold them),
  so treat this layer as useful for now, not the long-term reference.
- **P-machine registers** -- `NP`/`MPD0`/`BASED0`/`IPCSAV` (from
  `0x0240`-`0x0247`) and `MP`/`BASE`/`JTAB`/`SEGP` (from
  `0x02F0`-`0x02F7`), read directly from their fixed memory locations.
  This is the layer that outlives the Z80 registers: once SYSTEM.MICRO
  and the BIOS are fully replaced, this is what a verification log
  still needs to check. It already caught a real bug the Z80-register
  layer completely missed -- see the `IPCSAV`/`SAVIPC` gotcha below.

```
./harness <data-dir> <max-instructions> [trusted-log-output-path]
./harness ../data 5000000
./harness ../data 5000000 /tmp/trusted_boot_log.txt
```

Prints `TRACE-DIFF PASSED` (or the exact mismatched registers, with the
BACK-visit number, if something's wrong -- shown as two blocks, Z80
registers and P-machine registers, baseline vs native), an
opcode-frequency histogram from the baseline run (useful for deciding
what to natively implement next), and a fire-count for every opcode
currently implemented natively.

**The optional third argument writes a "trusted boot log"** -- the
baseline (pure, unmodified Z80) run's full P-machine register state at
every single opcode fetch, one line per fetch, to a plain text file.
This is the reference to keep once there's no live Z80 baseline left to
compare against: generate it now, while a real Z80 execution still
exists to produce it from, and it keeps working as the trusted
reference for a fully-native run's own P-machine register log to be
diffed against later. Format is one whitespace-separated line per
entry: `visit opcode ipc(bc) sp de hl a top0 top1 np mpd0 based0
ipcsav mp base jtab segp` (all values hex except the leading decimal
visit number), with a two-line `#`-comment header.

If you add a new native opcode to `PSystemEngine.cpp`, mirror the exact
same logic into the `switch` inside `harness.cpp`'s `RunLoop()`, rerun,
and confirm zero mismatches before considering it verified. This is how
a real bug (`LDO` leaving the wrong value in `DE`) got caught immediately
during development, rather than shipping silently.

## `remove_file.cpp` -- interactive, scripted-keystroke driver

Boots the disk, waits for the first `Command:` prompt, then feeds it a
literal keystroke sequence exactly as if someone had typed it -- Filer
commands, Editor insert-mode text, Compile, Run, all of it. This is what
let Claude drive the real Editor and Filer workflows (`Get`, `Insert`,
`Update`, `Compile`, `Run`) end-to-end without a GUI.

```
./remove_file <data-dir> <output-disk.BLK> <legacy-candidate> <legacy-cr> <poison-mode> <legacy-poison-end> <key-sequence> <native-flags>
```

The first two positional args are the only ones worth using directly;
the rest exist because this tool grew incrementally during debugging
rather than being designed as a clean CLI up front:

- **argv[1] data dir** -- e.g. `../data`
- **argv[2] output path** -- where the resulting disk image gets
  written (the tool never modifies the source data dir)
- **argv[3]/argv[4]** -- legacy, only used when argv[7] (the key
  sequence) is empty; ignore them and always pass a real key sequence
- **argv[5] poison mode** -- which BIOS vector range to overwrite with
  `0x76` (Z80 HALT) immediately after the first `Command:` prompt, to
  verify a native replacement really has made the real BIOS
  unreachable. One of:
  - a raw hex address pair via argv[5]/argv[6] (e.g. `fe06 fe0e` for
    just CONST/CONIN/CONOUT)
  - `alldisk` -- every disk-related vector (HOME through SECTRAN)
  - `all` -- the entire documented 17-entry vector table (`0xFE00`-`0xFE32`)
  - `listpunch` -- just LIST and PUNCH (`0xFE0F`-`0xFE14`)
  - `fullpage` -- the entire `0xFE00`-`0xFEFF` page, not just the
    documented entries -- the strongest test, and the one that should
    be used whenever a new opcode/BIOS function is added
  - anything else, or omitted -- no poisoning (normal regression run)
- **argv[7] key sequence** -- a plain string of the literal keystrokes to
  inject once `Command:` appears, e.g. `"f" "g" "MYGOTOXY" "\r" "q"` to
  Get a file via the Filer. Use `chr(3)` for `<etx>`/accept in Insert
  mode (NOT `chr(27)`/ESC -- that discards the typed text), `chr(0x12)`
  for the down-arrow control code, etc. See "Gotchas" below.
- **argv[8] native flags** -- 4 characters of `0`/`1`, in order
  CONST/CONIN/CONOUT/HOME, to selectively disable individual native BIOS
  replacements for bisection (does corruption/a halt go away if this
  ONE native path is turned off?). Default `"1111"` (all on). LIST/PUNCH
  native handling isn't wired to a flag -- it's always on.

The tool prints the full console output (its best-effort text rendering
-- it does NOT implement the real gotoxy/cursor-control decode
`PSystemEngine::PutChar` does, so screen-positioning bytes can render as
stray characters; don't trust exact column layout, only the text
content) and, on stderr, a detailed trace: every key consumed and the
console state at that moment, every Big Disk protocol read/write, every
BIOS-linker call (native or falling through to real BIOS), and -- if it
halts -- a stack dump flagging genuine `CALL` return addresses (checked
by confirming a real `0xCD` opcode sits 3 bytes before each candidate),
which is how the actual callers of CONST/CONIN/CONOUT/HOME/LIST/PUNCH
were traced back to `ECHO`/`CBOS`/`CHCLR`/`DSK0` in the first place.

### Gotchas (all cost real debugging time -- don't relearn them)

- **`SAVIPC` (the macro behind `IPCSAV`, `0x0246`) has an effect that
  outlives the instruction that calls it, and it's invisible to a
  Z80-register-only comparison.** Several opcodes (`CSP`, `IXA`, `CEQU`)
  call it before repurposing `BC` as scratch space, and it's tempting to
  reason "I never actually clobber `BC` in my native version, so there's
  nothing to restore" -- true for `BC` itself, but `SAVIPC` also
  unconditionally *writes* the current `BC` to a fixed memory location,
  read later by error-handling/`XEQERR` code. That write is real,
  independent, observable state, and skipping it is invisible to any
  comparison that only checks Z80 registers -- it took the P-machine
  register layer (added specifically because Z80 registers won't exist
  forever) to catch this, across multiple opcodes, several rounds after
  they'd already "passed" the register-only trace-diff clean. If an
  opcode's real code calls `SAVIPC`, replicate the memory write itself,
  not just whatever `BC` state it happens to be protecting.

- **A p-code opcode name that matches an assembly label elsewhere in the
  file doesn't mean they're the same thing.** Opcode `0xD7` is named
  "`BACK`" in the Op Code Count dialog, which is also the name of the
  fetch-loop entry point at `0x03B0` -- these are two different things.
  `0xD7`'s own `XFRTBL` entry happens to point directly at the
  fetch-loop's `BACK` label (verified against the real table, not
  assumed), making it a genuine no-op opcode, not the dispatch loop
  itself. Reading a name in a profiler and assuming it must refer to
  something already understood, rather than checking what it actually
  resolves to, cost a full round of misdirected work here.


- **A table symbol defined as `.EQU $-2` (or similar arithmetic on the
  current address) does NOT necessarily point at its own first entry** --
  double-check by computing the actual address, not by assuming the
  label sits where the table's contents begin. `CEQU`'s `CMPTBL` is
  exactly this trap: the symbol is defined two bytes before the first
  real table entry, so naively reading "table position × entry size"
  gives type codes 0,2,4,6,8,10 for the six comparison types, when the
  real codes (verified against a live mismatch) are 2,4,6,8,10,12. This
  produced a genuinely wrong answer, not just a wrong register --
  string-equality operands were being run through boolean-equality
  logic. Caught by the trace-diff flagging a stack-value mismatch (not
  just a register one); confirmed by disabling suspect branches one at
  a time until the actual culprit was isolated, rather than assuming
  the most-recently-added opcode was automatically at fault.

- **A native opcode can be 100% functionally correct and still leave the
  wrong value in register `A`, because nothing downstream ever reads it**
  -- `BACK` always overwrites `A` fresh via its own `LD A,(BC)` before
  using it again, so a wrong leftover `A` never breaks anything visible.
  This makes it the single easiest register to get quietly wrong, and it
  really happened: six comparison opcodes, `LDO`/`SRO`, and `FJP` all
  had a wrong or entirely-untouched `A` for one to several rounds before
  the trace-diff was strengthened to check it and caught every one. The
  fix is never a shortcut formula -- trace the actual Z80 instruction
  sequence byte-by-byte (which register got `SUB`'d against which,
  what survives to the next `LD A,` before the routine returns) rather
  than guessing from the "obvious" result value. When in doubt, check
  register `A` too, not just the ones that affect the visible outcome.

- **Deliver the whole key sequence at once, don't pace it.** The system's
  own idle/"reinit" cycle fires roughly every ~642,000 instructions;
  anything slower than that between keystrokes risks the system
  resetting state in the gap. `remove_file.cpp` already queues the full
  sequence up front for this reason -- don't reintroduce per-key pacing.
- **`Ctrl-C` (`0x03`, `<etx>`) accepts Insert-mode text; `ESC` (`0x1B`)
  discards it.** Getting this backwards silently produces an empty
  saved file with no error.
- **`Filer G(et` loads an existing named file into the workfile and
  keeps that name association** (so `Update` in the Editor saves back to
  the real filename, not just `SYSTEM.WRK.TEXT`). This is the reliable
  way to create/verify persistence, not a blank Editor start.
- **After a restart, `Filer G(et` may ask `Throw away current workfile
  ?`** -- answer `y` before typing the next filename, or that `y` (or
  worse, part of the filename) gets consumed as an unrelated answer and
  cascades into garbage.
- **`Compile: what text?`-style prompts can share Editor-style command
  bindings** -- e.g. typing a filename containing `X` can trigger
  "Xchange" mid-name. Prefer bare `<ret>` (use the current workfile) over
  typing an explicit filename at these prompts where possible.
- **The Filer's `L(dir`/similar prompts want a real volume/unit
  specifier with a trailing colon** -- `#4:` or `BIGGY:`, not a bare `4`.

## Keeping in sync

`NativeCalli.inc` (CSP 138, CALLI, version 1.91) and `NativeDouble.inc` (the
8-byte double CSPs 100..137) are copied here so that every shared file is
identical (`validation/run_all.sh` checks that first), but no harness program
includes them: neither has a pure-Z80 counterpart to trace-diff against (the Z80
interpreter has no such CSPs). CALLI reuses `NativeCxp.inc` and
`NativeBldmscwProc`, which *are* used here, and the trace-diff checks that the
shared code still behaves as before. CALLI itself is tested by Tiny-C's
`tests/funcptr.c` and `tests/funcseg.c`, the doubles by `tests/doubles.c` and
`tools/f12test.py` (https://github.com/Bio-Printer/UCSD-C).

`z80.h`/`z80.cpp` here are byte-for-byte copies of `UCSDPascal/z80.h`/
`z80.cpp`. That file changes rarely; if it ever does change in the real
project, copy the change here too, or this harness will quietly stop
being a faithful stand-in.

The port-level engine and native-opcode logic in both `.cpp` files here
are a **hand-maintained mirror** of `PSystemEngine.cpp`'s `PortIn`/
`PortOut`/`RunLoop`, not a shared/included copy -- when you add a new
native opcode or BIOS function to `PSystemEngine.cpp`, mirror the same
logic into both files here (at minimum `harness.cpp`, to trace-diff it;
`remove_file.cpp` too if you'll want to interactively exercise it) before
trusting it's correct.
