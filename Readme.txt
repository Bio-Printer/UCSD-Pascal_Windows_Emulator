UCSD Pascal II.0 -- MFC Windows GUI (Visual Studio Project)
==============================================================

WHAT THIS IS
------------
A full MFC Windows GUI application (dropdown menus, status bar, a
resizable main window) hosting the same Z80 emulator and UCSD Pascal
II.0 boot path used throughout this project. Targets Visual Studio
2026, Platform Toolset v145, matching your other MFC projects
(TicTacToeAB, CheckersAB, EDN_File_Sync).

FOLDER LAYOUT
-------------
UCSDPascal.sln                 -- open this in Visual Studio
UCSDPascal\                    -- the project itself
    UCSDPascal.vcxproj
    UCSDPascalApp.h/.cpp       -- CWinApp: creates the main frame, tries
                                   auto-loading the data files on startup
    MainFrm.h/.cpp              -- CFrameWnd: menu, status bar, terminal
                                   painting, keyboard input, worker thread
    PSystemEngine.h/.cpp        -- the emulator itself (GUI-agnostic --
                                   no MFC dependency, just the Z80 core
                                   plus a thread-safe character grid and
                                   keyboard queue)
    z80.h/z80.cpp                -- the Z80 core, unchanged from the
                                   console-testing build
    UCSDPascal.rc, resource.h    -- menu, About dialog, icon, version info
data\                          -- pascal.bin, Big_Disk.BLK,
                                   Empty_Big_Disk.BLK; a post-build step
                                   copies these next to the compiled .exe
                                   automatically

HOW TO BUILD
------------
1. Open UCSDPascal.sln in Visual Studio 2026.
2. Select the x64 platform (Debug or Release) -- there's no Win32/x86
   configuration in this first version.
3. Build. The post-build step copies pascal.bin / Big_Disk.BLK /
   Empty_Big_Disk.BLK from data\ into the output folder automatically.
4. Run. It should auto-load those three files and boot straight to the
   "Welcome BIGGY" prompt.

WHAT I COULD AND COULDN'T VERIFY MYSELF
-----------------------------------------
I don't have Visual Studio or the real MFC libraries available in my
environment, so I could NOT compile this project end-to-end myself --
this is the one significant difference from the console build, where I
was able to cross-compile and test under Wine.

(Fixed after the first round of real compiler feedback: PSystemEngine.cpp
and z80.cpp are intentionally MFC-free/portable, so they don't include
stdafx.h -- they're now marked "Not Using" precompiled headers in the
project file instead of inheriting the project-wide "Use" setting. Also
removed a redundant IDC_STATIC definition in resource.h, since MFC's own
afxres.h already defines it.)

What I COULD do:
  - PSystemEngine.h/.cpp (the actual emulator: Z80 core, Big Disk
    protocol, console-output decoding, keyboard queue) has NO MFC
    dependency, and I compiled it cleanly with a Windows cross-compiler
    (MinGW-w64) to catch syntax and logic errors. This is the same
    engine logic as the working console build, restructured into a
    class with proper thread-safety for a GUI, but functionally
    equivalent.
  - I hand-wrote MainFrm.cpp, UCSDPascalApp.cpp, and the .rc/.vcxproj
    files carefully against standard, well-established MFC patterns,
    but none of that MFC-specific code has been compiled or run.

Given that, please treat this as a solid first draft rather than a
guaranteed-clean build. If Visual Studio reports errors, they're most
likely to be small things in the MFC-specific files (MainFrm.cpp,
UCSDPascalApp.cpp, the .rc file, or the project file itself) rather
than in the engine. Let me know exactly what the compiler says and I
can fix it directly.

DESIGN NOTES
------------
- The terminal is a fixed 132x45 character grid (matching this
  system's documented real screen size). Console output bytes are
  decoded using the exact same control-code table the console build
  used (CHR(1) begins a gotoxy sequence, CHR(2) clears to end of line,
  etc.) but instead of emitting ANSI escapes, they directly update an
  internal grid buffer and cursor position, which the GUI paints
  as-is -- no ANSI interpreter needed in the GUI layer.
- The Z80 CPU runs continuously on its own worker thread (via
  AfxBeginThread), so the GUI thread stays responsive. A timer
  repaints the terminal ~20 times/second from a thread-safe snapshot
  of the grid.
- Keyboard input is handled via WM_CHAR on the main frame (there's no
  separate CView -- this app doesn't have "documents" in the MFC
  sense, so a plain CFrameWnd handling input and painting directly
  keeps things simpler).
- Resizing the window is supported directly (standard OS behavior) --
  the terminal grid stays a fixed logical size and font, so a larger
  window just shows more background around it and a smaller one
  clips; font size can be changed via Options > Font Size instead of
  scaling automatically on resize, since that's simpler and lower-risk
  for this first version.
- File > Open Big Disk / Scratch / Unit 9 / Unit 10 all stop the
  running system, reload all currently-known disk paths into a fresh
  engine instance, and restart it -- this avoids race conditions
  between the worker thread and the GUI thread over the disk image
  buffers, at the cost of a brief reboot whenever you change disks.
- Options > Enable Trace Logging turns on the same instruction trace
  format used throughout this project (writes trace.txt next to the
  .exe), for comparing a session against a real-hardware trace.
- The boot ROM contains a shared busy-wait subroutine (at address
  0xF244 in pascal.bin) that simulates real disk seek/settle timing
  from the original 1979 hardware -- harmless and quick on real
  silicon, but since software emulation runs meaningfully slower than
  the real Z80 it's timed for, left alone it turned a sub-second real
  delay into several minutes of wall-clock time before the system
  would appear to respond at all. Since there's no physical disk that
  actually needs to seek here, PSystemEngine detects entry into this
  routine and fast-forwards it (it always finishes with the same
  register state regardless of how long it runs, so this has no
  effect on correctness) -- boot now reaches the Command: prompt in
  well under a minute.

NEXT STEPS
----------
Once this compiles and runs to your satisfaction, this is the
foundation for incrementally replacing individual Z80 P-code
interpreter routines with native C: PSystemEngine already isolates the
CPU/memory/port layer cleanly, so a routine-by-routine port can be
verified by trace-diffing against this same emulator's output, the
same way we've been verifying everything else in this project.
