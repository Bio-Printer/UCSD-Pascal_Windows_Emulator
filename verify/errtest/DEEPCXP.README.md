# CXP more than 64 frames deep (P-Code mode only)

`deepcxp.pas` / `DEEPCXP.CODE` (Pascal, 3 blocks) and `deepc.c` /
`DEEPC.CODE` (Tiny-C) do the same thing: a recursive function goes N deep
and then calls a function in another segment (CXP).

| mode   | DOWN(60) | DOWN(70) |
|--------|----------|----------|
| Z80    | 1        | 1        |
| P-Code | 1        | hangs (Z80 PC=137F..1384) |

## Cause (linux-harness, NativeCxp.inc; same pattern in the native CIP in harness.cpp)

For a callee at lex level >= 1, CIPXNL searches the dynamic chain for the
first frame whose lex level is one less. The native code gives up after 64
frames:

```cpp
for (int iter = 0; iter < 64; iter++) { ... }
if (!found) {                             // same bail-out as native CIP
    PM_CPU.r.setBC(examine);
    PM_CPU.r.PC = 0x137F;
    break;
}
```

It then continues at CIPXNL's `$10` loop in Z80 code. That loop expects two
things the native path never sets up:

* **A = the target lex level** (`CP (HL)`). In the trace A was D5.
* **The IPC pushed on the Z80 stack.** `CIPXNL: PUSH BC` saves it, and the
  loop's exit does `POP DE ; get IPC` / `POP HL ; junk old stat link`.

With memory reclaimed, the Z80 code at 137F isn't there at all (it reads as
00 00 00).

Every Tiny-C function is lex level 1 and the target is the lex-0 program
frame, so any cross-segment call made more than about 64 calls deep hits
this. A recursive-descent compiler does that all the time.

Suggested fix: make the search unbounded, as the Z80 loop is, or at least
much larger (the chain always ends at the lex-0 frame). If a bail-out is
kept, it has to push the IPC and set A = target before jumping to 137F.

## The fix (committed)

The fix is applied in the repository: inside `UCSD-Pascal---P-Machine_work.zip` (UCSDPascal/ and linux-harness/) and `linux-harness.zip`. `deepcxp-engine.patch` shows it. It changes the cap from 64 to 30000 in:

* `UCSDPascal/NativeCxp.inc` (CXP)
* `UCSDPascal/PSystemEngine.cpp` (native CIP)

A 64K stack can't hold more than about 5,500 frames, so the search now
always finishes in native code and the broken bail-out is never reached.
The comment above the CIP loop ("a handful of iterations") is wrong: the
loop walks the *dynamic* chain, so its length is the recursion depth, not
the lexical nesting depth.

The linux-harness copies (`harness.cpp`,
`remove_file.cpp`, `NativeCxp.inc`) are patched the same way. The bail-out code itself is still
wrong (see above) if anyone ever relies on it.
