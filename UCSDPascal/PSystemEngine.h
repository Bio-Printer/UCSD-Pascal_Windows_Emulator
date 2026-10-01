// PSystemEngine.h
//
// GUI-agnostic engine layer wrapping the Z80 core and the UCSD Pascal
// II.0 Big Disk boot path. This file has NO MFC or Windows GUI
// dependency -- it only uses plain Win32 threading primitives (CRITICAL_SECTION,
// HANDLE/Event) so it can be unit-tested outside of the MFC app if needed,
// and so the actual GUI layer (MainFrm/TerminalView) stays thin.
//
// Design:
//   - RunLoop() executes Z80 instructions continuously until Stop() is
//     called. It's meant to be run on its own worker thread.
//   - Console OUTPUT (portOut case 1) is decoded using the exact same
//     control-code table as the console/batch-testing builds (CHR(1)
//     begins a gotoxy sequence, CHR(2) clears to end of line, etc.),
//     but instead of emitting VT100/ANSI escapes, it writes directly
//     into an internal character grid (m_grid) and moves an internal
//     cursor (m_cursorX/m_cursorY). This sidesteps needing any ANSI
//     interpreter in the GUI -- the GUI just paints m_grid as-is.
//   - Console INPUT (portIn cases 0/1) pulls from a thread-safe queue
//     (m_keyQueue) fed by the GUI thread's keyboard handlers. CONST is
//     non-blocking (checks queue empivalent to real CP/M semantics);
//     CONIN blocks (waits on m_keyAvailable) until a key exists or the
//     engine is stopping -- it never fabricates a value.
//   - All grid/queue access is guarded by a CRITICAL_SECTION so the
//     worker thread and the GUI thread never race.
//
// The terminal grid is sized to match the real system's documented
// 132x45 screen (see project notes: "132×45 screen with L2 as default
// editor").

#pragma once

#include "z80.h"
#include <vector>
#include <string>
#include <chrono>
#include <cstdint>
#include <atomic>
#include <fstream>
#include <windows.h>
#include "PCodeOpcodes.h"

// Terminal grid dimensions, matching this system's real 132x45 screen.
const int TERM_COLS = 132;
const int TERM_ROWS = 45;  // MUST equal SCREEN HEIGHT in SYSTEM.MISCINFO (45) and the row limit
                           // in this system's FGOTOXY (44 = last row). The editor scrolls by
                           // moving to what it believes is the bottom row and sending a line
                           // feed; with a taller console (47 was used for a while) that line
                           // feed doesn't scroll and the display falls apart when you move
                           // past the bottom of the screen.

struct BigDiskState {
    int drive = 0;
    uint16_t blk = 0, len = 0, dma = 0;
    uint8_t len_hi = 0, blk_hi = 0, dma_hi = 0, cmd = 1;
};

struct FdcState { uint8_t drive = 0, track = 0, sector = 0; };

class PSystemEngine {
public:
    PSystemEngine();
    ~PSystemEngine();

    // Load the three required data files. Returns false (with mMessage
    // set) if pascal.bin or the Big Disk image can't be opened; the
    // Empty Big Disk (unit 5, scratch volume) is optional.
    bool LoadFiles(const std::wstring& pascalBinPath,
                   const std::wstring& bigDiskPath,
                   const std::wstring& emptyDiskPath,
                   std::wstring& errorMessageOut);

    // Optionally mount a floppy-style image as unit #9 or #10 (see
    // unitValueToDrive below). Safe to call before or after LoadFiles.
    bool MountUnit9(const std::wstring& path, std::wstring& errorMessageOut);
    bool MountUnit10(const std::wstring& path, std::wstring& errorMessageOut);

    // Runs Z80 instructions continuously until Stop() is called or the
    // CPU halts. Intended to be the body of a worker thread.
    void RunLoop();
    void Stop();
    bool IsRunning() const { return m_running; }

    // Instruction tracing (same "PC=.... A=.. BC=.... ..." format used
    // throughout this project), off by default. Enabling opens/creates
    // the given file; call before starting RunLoop for a full-session
    // trace, or at any point to start tracing from then on.
    bool EnableTrace(const std::wstring& tracePath, std::wstring& errorMessageOut);
    void DisableTrace();

    // Control whether native-C P-code opcode replacements are active.
    // When disabled (Z80 mode), the P-code interpreter runs entirely as
    // real Z80 code — no native switch cases, no BACK dispatch shortcut,
    // no SLDC/SLDL/SLDO/SIND fast paths. BIOS/device intercepts stay on
    // regardless since those are needed to keep the terminal working.
    // Defaults to true (native mode). Takes effect on the next RunLoop call.
    void SetNativePcodeOps(bool native) { m_nativePcodeOps = native; }
    void SetTraceAlsoZ80(bool also)     { m_traceAlsoZ80   = also;  }

    // When true (the default), native-C p-code opcode handling also
    // spends the extra effort to leave the Z80 CPU's own scratch
    // registers (A, and each opcode's own DE/HL) holding exactly what
    // the real Z80 handler code would have left them as. This is pure
    // Z80-compatibility bookkeeping with no P-machine meaning of its
    // own -- it exists for register-level trace-diff verification
    // against real Z80 execution, and for whatever real Z80 fallback
    // code might still depend on it.
    //
    // When false, that bookkeeping is skipped everywhere it's gated
    // (the BACK dispatch shortcut's own A/DE/HL mirroring, and each
    // converted opcode's own result-specific register writes -- see
    // ADI/SBI/NGI/NOT's own cases). This is the direction this effort is
    // actually headed: eliminating the Z80 registers from the execution
    // path entirely, opcode by opcode. What's verified while doing that
    // is P-machine register equality (NP, MPD0, BASED0, MP, BASE, JTAB,
    // SEGP, and the P-code stack itself via SP/top0/top1) -- NOT Z80
    // scratch-register agreement, which is expected to come and go
    // depending on which opcodes have been converted so far, not
    // something to preserve for its own sake. BC (which mirrors the
    // P-machine IPC register, m_ipc) is a P-machine register, not Z80
    // scratch, so it stays synced unconditionally regardless of this
    // flag.
    //
    // This flag is meaningless in Z80 mode (no native dispatch exists
    // there at all) and should be disabled/grayed out whenever Z80 mode
    // is active. Defaults to true. Takes effect on the next RunLoop call.
    void SetPreserveZ80RegisterCompat(bool preserve) { m_preserveZ80RegisterCompat = preserve; }

    bool GetPreserveZ80RegisterCompat() const { return m_preserveZ80RegisterCompat; }

    // Feed a keystroke from the GUI thread. vk/ch follow the same
    // convention as a WM_CHAR message: a plain 8-bit character. Special
    // keys (arrows, function keys) are not modeled -- this system's own
    // command set doesn't need them beyond what already maps to plain
    // ASCII control codes (ESC, CR, BS, etc.).
    void PostKey(uint8_t ch);

    // Verify P-System support (see verify/). IsWaitingForKey(): the system
    // is blocked in CONIN with nothing queued -- after a scripted run this
    // means the script has been fully consumed. Console capture records
    // every console-output byte (while enabled) so a run can be checked
    // against the prompts its script expects.
    bool IsWaitingForKey() const { return m_waitingForKey; }

    // Reclaim the Z80 interpreter's and BIOS memory for the P-System (P-Code
    // mode with Z80 register compatibility off). With the native boot (the
    // normal case) the P-System starts in the reclaimed layout: nothing of
    // SYSTEM.MICRO or pascal.bin in its memory except the opcode table and the
    // P-machine variable area (0x0100-0x03A3); the heap starts at 0x03A4
    // instead of 0x1EBA and MEMTOP is 0xFFF2 instead of 0xFDF2 -- about
    // 7.4 KB more. SYSTEM.MICRO is still read, as a data file, into the
    // engine's private table copy (m_rom: dispatch, standard-procedure, unit,
    // powers-of-ten and reserved-word tables). If the native boot is not
    // possible, the Z80 boot runs and the interpreter's area is reclaimed at
    // the first P-code instruction instead (0x1EBA -> 0x03A4 only). Should Z80
    // code ever be needed afterwards (nothing in the validation suites does),
    // the engine stops and ReclaimFault() says where.
    void SetReclaimInterpreterMemory(bool on) { m_reclaimRequested = on; }
    bool InterpreterMemoryReclaimed() const { return m_reclaimed; }

    // Harvard layout (P-Code mode; takes effect only with the native boot and
    // the reclaimed-memory layout above, i.e. P-Code mode, register
    // compatibility off, memory reclaimed). Code segments are kept in a 64K
    // instruction space of their own (I-space) instead of on the P-machine
    // stack, so they no longer take memory from the stack and heap (D-space):
    //  * I-space: the "CXP 0,2" error stub at 0x0040 and an ABORT opcode at
    //    0x0044; segment 0 (the operating system) at the top, ending at MEMTOP
    //    as before; below it a code stack, growing down to 0x0100, holding
    //    every other segment that is in memory.
    //  * D-space: the P-machine stack starts at the top of memory; the heap
    //    as before.
    //  * MSCW msipc / msseg / msjtab, IPC, SEGP, JTAB and INTSEGT addresses
    //    are I-space addresses. A segment's code is freed when its INTSEGT
    //    reference count reaches zero (RNP/RBP, RELEASESEG), together with
    //    any segment loaded after it -- what resetting SP over a segment
    //    does in the normal layout.
    //  * Inline string constants (LSA, LPA) are data the program reads through
    //    the address the instruction pushes, so they are copied to D-space:
    //    when a segment is loaded, its procedures are walked instruction by
    //    instruction and every LSA/LPA constant goes into a constant pool on
    //    the stack -- where the normal layout puts the whole segment, so the
    //    pool lives and dies exactly as the segment's stack copy would.
    //    Segment 0's pool is at the top of D-space. LSA/LPA push the copy's
    //    address (m_constAddr, indexed by the instruction's I-space address).
    //  * Assembly-language procedures cannot run (there is no Z80 code to
    //    run them anyway once memory is reclaimed).
    // HarvardActive() says whether it took effect; HarvardCodeSegments() and
    // HarvardCodeFree() are test aids (segments on the code stack, free
    // I-space bytes).
    void SetHarvard(bool on) { m_harvardRequested = on; }
    bool HarvardActive() const { return m_harvard; }
    int HarvardCodeSegments() const { return (int)m_codeSegs.size(); }
    uint16_t HarvardCodeFree() const { return m_harvard ? (uint16_t)(m_codeTop - kCodeFloor) : 0; }
    int HarvardUnwalkedProcs() const { return m_constUnwalked; }

    // The P-System has halted: execution reached the ABORT opcode's routine
    // (a "JP ABORT" loop in the Z80 interpreter). The boot makes an ABORT the
    // return point of the operating system's main program, so this means the
    // system's main program ended (e.g. the unrecoverable state after an
    // "Exit from uncalled proc" error). RunLoop returns instead of spinning.
    bool IsSystemHalted() const { return m_systemHalted; }

    // The PC's date and time for the P-System (set before RunLoop). When on:
    //  * TIME(hi, lo) -- SYSCOM's HIGHTIME:LOWTIME, which the Z80 interpreter
    //    expects a 60 Hz clock interrupt to advance -- counts sixtieths of a
    //    second, starting at the current time of day when the P-System starts
    //    and advancing in real time (never backwards, also across midnight).
    //  * the boot volume's DLASTBOOT is set to today's date before booting, so
    //    the P-System starts with today's date (THEDATE). This changes the
    //    in-memory disk image; the disk file changes only if the P-System writes
    //    that directory block, as after a Filer D(ate).
    // Off (the default, and always for Verify runs): TIME returns 0 and the
    // date is the disk's -- runs stay deterministic.
    void SetHostClock(bool on) { m_hostClock = on; }

    // Native boot (P-Code mode): at the start of RunLoop, instead of running
    // the Z80 loader (pascal.bin) and the interpreter's BOOT code, the engine
    // does their work itself and starts at the first P-code instruction.
    // The result is the SAME memory and P-machine state as the Z80 boot
    // (same addresses throughout), so the Verify reference logs stay valid.
    // Z80 mode always boots with the Z80 code. If the interpreter's boot code
    // is not the one this implements (checked byte for byte), or a file is
    // missing, the Z80 boot runs instead and NativeBootNote() says why.
    bool NativelyBooted() const { return m_nativeBooted; }
    const std::string& BootFault() const { return m_bootFault; }   // non-empty: the P-System could not be started
    const std::string& NativeBootNote() const { return m_nativeBootNote; }
    const Z80Regs& DebugRegs() const { return m_cpu.r; }     // test aid
    const std::string& ReclaimFault() const { return m_reclaimFault; }

    // Binary P-code log for Verify P-System. One 25-byte record per P-code
    // instruction, taken at every BACK dispatch (both modes): IPC, opcode,
    // SP, NP, MPD0, BASED0, IPCSAV, MP, BASE, JTAB, SEGP and the top two
    // stack words (little-endian). Record mode writes the log; compare mode
    // reads a reference log and stops the engine at the first instruction
    // whose record differs (VerifyMismatch() then describes it).
    bool StartVerifyLog(const std::wstring& path, bool compare, std::wstring& errorMessageOut);
    void StopVerifyLog();                 // compare mode: also checks the reference has no extra records
    uint64_t VerifyRecordCount() const { return m_verifyCount; }
    bool VerifyMismatch() const { return !m_verifyMismatch.empty(); }
    const std::string& VerifyMismatchText() const { return m_verifyMismatch; }
    void SetVerifyStopAt(uint64_t recordCount) { m_verifyStopAt = recordCount; } // test aid: halt exactly there
    const uint8_t* DebugMemory() const { return m_mem; }
    size_t PendingKeyCount();
    void SetConsoleCapture(bool on);
    std::string TakeConsoleCapture();

    // Thread-safe snapshot of the terminal grid + cursor position, for
    // the GUI to paint. Copies out of the shared buffer under the lock.
    void SnapshotGrid(std::vector<uint8_t>& gridOut, int& cursorXOut, int& cursorYOut);

    // True once the CPU has halted (distinct from Stop() being called
    // externally) -- lets the GUI show a "system halted" state.
    bool HasHalted() const { return m_halted; }

    // Debug-only accessor used by the standalone test harness.
    size_t DebugDrive0Size() const { return m_volDrive0.size(); }
    uint16_t DebugPC() const { return m_cpu.r.PC; }
    long DebugInstrCount() const { return m_traceCounter; }
    uint8_t DebugBdDrive() const { return (uint8_t)m_bd.drive; }
    uint16_t DebugBdBlk() const { return m_bd.blk; }
    uint16_t DebugBdLen() const { return m_bd.len; }
    uint8_t DebugBdCmd() const { return m_bd.cmd; }

    // How many disk-image write-throughs (see FlushDriveRegion) have
    // succeeded vs. failed this session -- e.g. the underlying file
    // became read-only or was deleted out from under the app. A
    // nonzero fail count means changes are NOT reaching disk and
    // will be lost on restart; surface this to the user if it ever
    // matters enough for a status-bar indicator.
    uint64_t DebugDiskFlushOkCount() const { return m_diskFlushOkCount; }
    uint64_t DebugDiskFlushFailCount() const { return m_diskFlushFailCount; }
    // A mounted volume's image FILE was changed by something else (a git
    // pull, another program) after the emulator loaded it: writes to it are
    // refused (see FlushDriveRegion) so the emulator's older copy does not
    // overwrite the new file. Returns the unit number (4, 5, 9, 10) once,
    // then 0; the UI shows a warning. Re-opening the unit (or restarting)
    // reloads the file and clears the refusal.
    int TakeChangedOnDiskUnit(std::wstring& pathOut);
    bool IsChangedOnDisk(int unit) const;

    // Instruction-frequency instrumentation, purely for deciding which
    // p-code opcode to port to native C next. m_opcodeFetchCount[op]
    // increments every time opcode `op` is dispatched at the BACK
    // re-entry point (0x03B0), whether or not it's natively emulated;
    // m_opcodeEmulatedCount[op] increments only when a native-C
    // replacement actually fires in place of the Z80 routine. Both are
    // indexed by the raw p-code opcode byte.
    uint64_t DebugOpcodeFetchCount(uint8_t opcode) const { return m_opcodeFetchCount[opcode]; }
    uint64_t DebugOpcodeEmulatedCount(uint8_t opcode) const { return m_opcodeEmulatedCount[opcode]; }
    // Per-selector fire count for CSP (Call Standard Procedure), indexed
    // by the procedure number byte (CSPTBL's own index at 0x15E7). Lets
    // the View > Op Code Count dialog break the single "CSP" line down
    // into individual entries (CSP-IOC, CSP-SQT for square root, etc.)
    // instead of lumping every standard-library call together.
    uint64_t DebugCspSelectorCount(uint8_t sel) const { return m_cspSelectorCount[sel]; }

    // How many times the native UNITREAD/UNITWRITE fast path (unit #4 /
    // BIGGY only) actually fired, vs. fell through to the real Z80/BIOS
    // disk driver (different unit, or an out-of-range request).
    uint64_t DebugUnit4IoNativeCount() const { return m_unit4IoNativeCount; }
    uint64_t DebugUnit4IoFallbackCount() const { return m_unit4IoFallbackCount; }

    // Same idea for the character-device fast path (units #1 CONSOLE
    // and #2 SYSTERM -- see ConsoleEcho()).
    uint64_t DebugCharIoNativeCount() const { return m_charIoNativeCount; }

    // How many times CONST/CONIN/CONOUT were served natively via the
    // BIOS-linker intercept (see the comment above that code in
    // RunLoop()) instead of executing the real BIOS at 0xFE00.
    uint64_t DebugBiosLinkerNativeCount() const { return m_biosLinkerNativeCount; }

    // Import a Windows file into a mounted UCSD Pascal disk volume image.
    // unit: UCSD unit number (4, 5, 9, or 10).
    // windowsFilePath: full Win32 path to the source file.
    // Returns empty string on success; a human-readable error message otherwise.
    std::wstring ImportFileToVolume(int unit, const std::wstring& windowsFilePath);

    // Return the UCSD volume name from the disk image for the given unit number,
    // or an empty string if no image is loaded for that unit.
    std::wstring GetVolumeNameForUnit(int unit) const;

    // Disconnect a mounted volume: clears the in-memory image and the
    // on-disk path so no further reads or write-throughs touch that file.
    // The running p-system will get I/O errors if it tries to access the
    // unit afterward; unit #4 in particular is the system disk, so
    // unmounting it while the system is active is intentionally warned
    // about in the UI before this is called.
    void UnmountUnit(int unit);

    // Directory entry returned by GetVolumeDirectory().
    struct UcsdDirEntry {
        std::wstring name;
        int          kind;       // 0=untyped,2=code,3=text,4=info,5=data,6=graf,7=foto
        uint16_t     firstBlock;
        uint16_t     lastBlock;  // exclusive (first block past end of file)
        uint16_t     lastByte;   // bytes used in last block (1-512; 512 = last block full)

        // Total byte count of the file's content.
        long ByteCount() const {
            if (lastBlock <= firstBlock) return 0L;
            return (long)(lastBlock - firstBlock - 1) * 512L + lastByte;
        }
    };

    // Return the directory entries for the named unit's mounted disk image.
    // Returns an empty vector if the unit is not mounted or unreadable.
    std::vector<UcsdDirEntry> GetVolumeDirectory(int unit) const;

    // Copy a single file from a mounted UCSD volume image out to a Windows path.
    // ucsdName must match a directory entry name exactly (case-sensitive).
    // Returns empty string on success; a human-readable error message otherwise.
    std::wstring ExportFileFromVolume(int unit, const std::wstring& ucsdName,
                                      const std::wstring& windowsDestPath) const;

private:
    Z80 m_cpu;
    uint8_t m_mem[65536];

    std::vector<uint8_t> m_volDrive0, m_volDrive1, m_volDrive2, m_volDrive3;
    std::vector<uint8_t> m_floppy0, m_floppy1;
    // Where each drive's image actually lives on disk, so a Big Disk
    // WRITE can be committed back to the real file immediately (see
    // FlushDriveRegion) -- matches real disk hardware, which writes
    // to physical media as part of the write itself, not on some
    // later "save" step. Empty if that drive was never mounted.
    std::wstring m_volDrive0Path, m_volDrive1Path, m_volDrive2Path, m_volDrive3Path;
    uint64_t m_diskFlushOkCount = 0, m_diskFlushFailCount = 0;
    // Size and last-write time of each drive's image file as the emulator
    // last saw it (loaded it, or wrote it itself): FlushDriveRegion refuses
    // to write when the file no longer matches.
    struct DiskStamp { bool valid = false; uint64_t size = 0; int64_t mtime = 0; };
    DiskStamp m_diskStamp[4];
    bool m_changedOnDisk[4] = { false, false, false, false };
    std::atomic<int> m_changedOnDiskPending{ 0 };   // unit number for the UI, 0 = none
    void StampDrive(int drive);
    bool DriveFileUnchanged(int drive);
    BigDiskState m_bd;
    FdcState m_fdc;

    // Terminal grid + cursor, protected by m_gridLock.
    std::vector<uint8_t> m_grid;      // TERM_COLS * TERM_ROWS, space-filled
    int m_cursorX = 0, m_cursorY = 0;
    CRITICAL_SECTION m_gridLock;

    // VT100-style control-code decode state (mirrors vt100ConOut's own
    // little state machine for the CHR(1)+X+Y gotoxy sequence).
    int m_ctrlCharsLeft = 0;
    int m_ctrlParamX = 0;

    // Keyboard queue, protected by m_keyLock; m_keyAvailable is signaled
    // whenever the queue becomes non-empty, so CONIN can block on it.
    std::vector<uint8_t> m_keyQueue;
    CRITICAL_SECTION m_keyLock;
    HANDLE m_keyAvailable = nullptr;
    HANDLE m_stopEvent = nullptr;

    volatile bool m_running = false;
    volatile bool m_waitingForKey = false;
    bool m_captureConsole = false;
    std::string m_consoleCapture;
    volatile bool m_halted = false;

    FILE* m_traceFile = nullptr;
    FILE* m_verifyFile = nullptr;
    std::vector<uint8_t> m_rom;        // interpreter image snapshot once memory is reclaimed
    bool m_reclaimRequested = false, m_reclaimed = false;
    // Harvard layout (see SetHarvard). m_codeMem is what PM_CODE8/16 and
    // PM_CODEW8 use: m_mem normally, the I-space m_code when Harvard is on.
    bool m_harvardRequested = false, m_harvard = false;
    std::vector<uint8_t> m_code;
    uint8_t* m_codeMem = m_mem;
    struct CodeSeg { uint8_t seg; uint16_t bot, top, dTop; };   // [bot, top) in I-space; dTop: its pool's top in D-space
    std::vector<CodeSeg> m_codeSegs;                      // the code stack, oldest first
    uint16_t m_codeTop = 0;                               // first byte above the free I-space
    static constexpr uint16_t kCodeFloor = 0x0100, kCodeCxp02 = 0x0040, kCodeAbort = 0x0044;
    bool CodePlace(uint16_t len, uint16_t& newseg, uint16_t& segbot) const;  // where a segment of len bytes goes
    void CodeCommit(uint8_t seg, uint16_t segbot, uint16_t top, uint16_t dTop);
    uint16_t CodeFree(uint8_t seg);                       // returns the freed segment's pool top (0: not found)
    // The constant pool: m_constAddr[a] is the D-space copy for the LSA/LPA
    // whose length byte is at I-space address a (0: none).
    std::vector<uint16_t> m_constAddr;
    int m_constUnwalked = 0;                              // procedures the constant scan could not walk (test aid)
    void HarvardConstFault(uint16_t lenAt);
    uint16_t HarvardSelfPatchTarget(uint16_t a) const;    // Tiny-C /Z call sequence: the CXP operand its STO writes
    uint16_t m_codeStoreAt = 0;                           // that address while the sequence runs (0: none)
    static bool ScanSegConsts(const uint8_t* seg, uint32_t len, std::vector<uint16_t>& key,
                              std::vector<uint16_t>& src, std::vector<uint16_t>& cnt, int& unwalked);
    void ConstPoolInstall(uint16_t segbot, const std::vector<uint16_t>& key, const std::vector<uint16_t>& src,
                          const std::vector<uint16_t>& cnt, uint16_t dBot);
    volatile bool m_systemHalted = false;
    bool m_hostClock = false;
    uint32_t m_clockTicks0 = 0;                    // HIGHTIME:LOWTIME when the P-System started
    std::chrono::steady_clock::time_point m_clockT0;
    uint16_t m_timAddr = 0;                        // the Z80 TIM routine (CSP table entry 9)
    void NativeClockUpdate();                      // HIGHTIME/LOWTIME := the host clock (if on)
    void HostClockStart();                         // the start value and today's date on the boot volume
    bool m_bootAttempted = false, m_nativeBooted = false;
    bool m_haveLoader = false;         // pascal.bin was loaded (needed only for the Z80 boot)
    std::string m_bootFault;
    std::string m_nativeBootNote;
    bool NativeBoot(std::string& why, bool reclaimLayout);
    void NativeBootSysrd(uint16_t unitWord, uint16_t buf, uint16_t len, uint16_t block, uint16_t retAddr, uint16_t& sp);
    uint16_t m_abortAddr = 0;          // XFRTBL entry of opcode 0xD6 (ABORT), found at the first P-code instruction
    uint16_t m_cxp02 = 0x03D7;
    PmIoConfig m_io; bool m_ioReady = false;   // PM_IOCFG (see IoConfig)
    bool m_builtInTables = false;      // P-Code mode without SYSTEM.MICRO: tables compiled into the engine
    const PmIoConfig& IoConfig();
    int m_varDelta = 0;
    uint16_t m_heapStart = 0x03A4;     // where the heap starts once memory is reclaimed (NP / INTEND)                // PM_V: the P-machine variable block (normally 0x0200-0x03A3) relocated by this much         // the "CXP 0,2" bytes XEQERR points the IPC at (page zero once reclaimed)
    std::string m_reclaimFault;
    bool m_verifyCompare = false;
    bool m_verifyHaltNow = false;
    uint16_t m_verifyMinSP = 0xFFFF;   // lowest SP since the last P-code instruction boundary
    uint64_t m_verifyStopAt = 0;
    uint64_t m_verifyCount = 0;
    std::string m_verifyMismatch;
    std::vector<uint8_t> m_verifyRecent;  // last records, for the mismatch report
    void VerifyLogStep();
    long m_traceCounter = 0;
    bool m_tracing = false;
    bool m_nativePcodeOps = true; // false = Z80 mode (no native switch, no BACK dispatch)
    bool m_traceAlsoZ80   = false; // also log every Z80 cpu.step() (for startup analysis)
    bool m_preserveZ80RegisterCompat = true; // see SetPreserveZ80RegisterCompat's own comment

    // P-machine IPC register: the address of the next p-code opcode byte
    // to fetch, i.e. the p-code program counter. Synced from the Z80 CPU's
    // own BC register pair at the top of every BACK dispatch (see RunLoop)
    // -- BC is still the ground truth for this value, since most opcode
    // bodies (not yet converted) read it directly for their own operand
    // bytes, and any fallback to real Z80 code depends on it too. Native
    // opcode code that HAS been converted should read/reference m_ipc by
    // name rather than m_cpu.r.BC() directly, so the P-machine-level
    // meaning is explicit at the call site; this is the first step toward
    // eventually decoupling IPC handling from the Z80 register entirely.
    uint16_t m_ipc = 0;

    // See the Debug* accessors above.
    uint64_t m_opcodeFetchCount[256] = {0};
    uint64_t m_opcodeEmulatedCount[256] = {0};
    uint64_t m_cspSelectorCount[256] = {0}; // indexed by CSP's own procedure-number byte
    uint64_t m_unit4IoNativeCount = 0;
    uint64_t m_unit4IoFallbackCount = 0;
    uint64_t m_charIoNativeCount = 0;

    // See the comment above the BIOS-linker interception code in
    // RunLoop(). m_biosLinkerAddr is found once, lazily, the first time
    // execution reaches BACK (0x03B0) -- SYSTEM.MICRO loads from disk
    // at runtime rather than being part of pascal.bin, and this exact
    // build's copy of the shared "LD A,(0002H); LD H,A; JP (HL)" BIOS
    // linker does NOT sit at the same address the reference assembly
    // listing shows (verified directly: it's 0x1DA2 in this build, not
    // the listing's 0x1D4A) -- so a hand-picked constant would be
    // fragile against any future SYSTEM.MICRO rebuild. Searching once
    // for the exact byte pattern is what actually survives that.
    uint16_t m_biosLinkerAddr = 0;
    bool m_biosLinkerSearchDone = false;
    uint64_t m_biosLinkerNativeCount = 0;

    // ---- Punch: device (unit #8) -> C:\Tmp\Punch.out logger ------------
    // Every character a p-code program writes to the PUNCH BIOS function
    // (L=0x12) is captured here instead of being discarded, and mirrored
    // to a Windows file as an Intel-HEX dump (32 data bytes per record,
    // address starting at 0x0000 and counting up), so a Pascal program
    // can "punch" a file's contents out to a real host-visible file
    // without needing any actual serial hardware. See PSystemEngine.cpp
    // for the exact record/marker format.
    std::ofstream m_punchFile;
    bool m_punchFileOpen = false;
    std::string m_punchLineBuffer;       // accumulates chars until CR/LF, to
                                          // test the completed line against
                                          // the end-of-transfer marker
    bool m_punchLastWasCR = false;       // so a CRLF pair triggers the line-end check only once
    std::vector<uint8_t> m_punchHexBuffer; // up to 32 bytes awaiting the next hex record
    uint16_t m_punchHexAddress = 0;
    void PunchWriteChar(uint8_t c);
    void PunchAddDataByte(uint8_t b);
    void PunchFlushHexBuffer();

    // LIST (BIOS L=0x0F, unit #6 / "PRINTER:" from Pascal): much simpler
    // than PUNCH above -- no hex encoding, no end-of-transfer marker, no
    // column formatting. Every character sent to the printer is written
    // to C:\Tmp\PRINTER verbatim, exactly as it was sent, so the file
    // reads like an ordinary text capture of whatever was printed --
    // except NUL (0x00) bytes, which are padding/filler rather than real
    // output and are silently dropped rather than written.
    std::ofstream m_printerFile;
    bool m_printerFileOpen = false;
    void PrinterWriteChar(uint8_t c);

    void PutChar(uint8_t v);   // decode one console-output byte into the grid
    void ScrollIfNeeded();
    uint8_t PortIn(uint8_t port);
    void PortOut(uint8_t port, uint8_t v);
    std::vector<uint8_t>* DriveImage(int drive);
    std::wstring* DrivePath(int drive); // matches DriveImage's drive numbering
    // Writes exactly the bytes at img[fileOffset..fileOffset+length) back
    // to that drive's underlying file, at the same offset -- called right
    // after every successful Big Disk WRITE (both the general path in
    // PortIn's case 207, and the native UNITREAD/UNITWRITE fast path) so
    // a change is on physical disk before the call that made it returns,
    // exactly like real disk hardware. Returns false (and leaves the file
    // untouched) if the drive has no known path or the file can't be
    // opened for writing; the in-memory image is authoritative for the
    // rest of this session either way.
    bool FlushDriveRegion(int drive, long fileOffset, int length);
    // Shared BLDMSCW ("Build Mark Stack Context Word") helper for CLP and
    // CBP. Reads proc_num from the code stream, looks up the procedure's
    // jtab via the segment table, extends the stack and copies parameters,
    // pushes the six MSCW words, performs the stack-overflow check, and
    // updates MP/MPD0/SEGP/JTAB. On success leaves BC = procedure entry
    // address and returns true; returns false (and sets PC to an error
    // handler or leaves state unmodified) for assembly-language procedures
    // or stack overflow.
    bool NativeBldmscw();
    bool NativeBldmscwProc(uint8_t procNum, uint16_t retIpc);   // NativeBldmscw with the procedure number and return IPC given (CXP, CSP 138)
    bool NativeDiskRead(uint16_t unit, uint16_t block, uint16_t len, std::vector<uint8_t>& out);
    bool NativeBiosCall(uint8_t fn, uint8_t c, uint8_t* aOut, bool dryRun);

    std::vector<uint8_t>* FdcImage();
    static int UnitValueToDrive(uint8_t v);

    // Little-endian, SP-relative stack helpers used by the native-C
    // p-code opcode replacements below -- match the real Z80 PUSH/POP
    // exactly, since the p-machine's evaluation stack IS the Z80
    // hardware stack (SP) in this interpreter.
    uint16_t PopStackWord();
    void PushStackWord(uint16_t v);
    uint16_t PeekStackWord(int byteOffsetFromSP) const; // non-destructive read

    // Replicates GBDE ("get a big, possibly two-byte, constant from code
    // into DE"): reads one operand byte from the code stream at BC,
    // advancing BC by 1. If its top bit is clear, that byte (0-127) is
    // the whole value. If set, it's the high byte of a two-byte value
    // (with the top bit itself masked off) and a second operand byte
    // follows, advancing BC by 1 more. Used by LDO/SRO (and LDL/STL/LAO/
    // LLA, not yet natively handled) to decode a variable's word offset.
    uint16_t DecodeGBDE();

    // Replicates GETIA ("get intermediate address"): chases the static-link
    // chain from MP (0x02F2) a fixed number of lex-levels (a single operand
    // byte, always >1 per the source comment -- lex-level 0/1 accesses are
    // handled by the simpler LDL/STL/LLA family instead), then adds a
    // GBDE-decoded word displacement plus the fixed DISP0 constant (10).
    // Shared by LDA, LOD, and STR in the real code (only LOD is natively
    // implemented so far).
    uint16_t GetIA();

    // Replicates SRS ("build a subrange set, the set [i..j]"): computes
    // and pushes the set's data words (word[0] on top of stack) plus its
    // size prefix, given i and j (already popped/committed by the
    // caller, and already verified in-range: i>=0 and j<4080, which the
    // caller must check itself via PeekStackWord BEFORE popping, so it
    // can cleanly fall through untouched on an out-of-range value rather
    // than needing to undo a commit). Also handles j<i here (pushes the
    // empty set), a valid, non-error outcome. Shared by SRS itself and
    // SGS ("build a singleton set, the set [i]"), which the real code
    // implements as a literal fall-through into SRS's own code with
    // j==i.
    void NativeSrs(uint16_t i, uint16_t j);

    // Shared CSETUP-equivalent for the ordering comparison family (CGTR,
    // CLEQ, CLSS, CGEQ -- CEQU/CNEQ have their own separate, independently
    // verified inline logic and are not routed through this). Handles
    // BOOLC and STRGC types only, matching the documented "all relations"
    // support for those two types specifically -- Set only supports
    // =,<>,<=,>= (not strict ordering), and Arrays/Records only support
    // =,<>, so CGTR/CLSS in particular would never legitimately be called
    // with those types anyway. Commits SAVIPC and the operand pops, and
    // sets A/DE to match "after CSETUP flags are result of a-b" (the
    // source's own contract), but does NOT push a result, set HL, or set
    // PC -- HL always gets overwritten by the caller's own PSHTRU1/
    // PSHFLS1-equivalent regardless of any intermediate value here, so
    // it's not tracked. Returns true (with outCarry/outZero set to match
    // "a-b", i.e. outCarry means a<b) if handled; returns false (nothing
    // committed, caller should fall through) for any other type.
    bool CmpSetupOrdering(bool& outCarry, bool& outZero);


    // Native replacement for CPMIO.TEXT's ECHO routine, used by both the
    // write side (always) and the read side (unit #1 CONSOLE only) of
    // the character-device fast path. Reproduces its DLE-run-length
    // blank decompression and CR-triggers-auto-LF behavior exactly, and
    // reads/writes CLAST (0x02E1) as real memory -- not a shadow
    // variable -- so it stays in lockstep with the real Z80 driver on
    // any unit/path this fast path doesn't handle.
    void ConsoleEcho(uint8_t c, bool suppressDle, bool suppressCrLf);
};
