// PSystemEngine.cpp
#include "PSystemEngine.h"
#include "PMachineTables.inc"   // P-machine data for P-Code mode without SYSTEM.MICRO

// PM_V(a): the address of the P-machine / SYSCOM variable at a (a is its
// address in the Z80 interpreter's layout, 0x0200-0x03A3). In the relocated
// layout (P-Code mode, reclaimed memory) the whole block moves down to 0x0080.
#define PM_V(a) ((uint16_t)((a) + m_varDelta))
#include "UcsdText.h"
#include "PCodeOpcodes.h"
// Host bindings for the shared native-code include files
// (NativeSegReturn.inc, NativeCxp.inc, NativeCsp.inc).
#define PM_MEM m_mem
#define PM_CPU m_cpu
#define PM_PRESERVE m_preserveZ80RegisterCompat
#define PM_COUNT(op) m_opcodeEmulatedCount[op]++
#define PM_ROM(a) (m_rom.empty() ? m_mem[(uint16_t)(a)] : m_rom[(uint16_t)(a)])   // interpreter tables / code bytes (see NativeCsp.inc)
// Code (instruction-stream) accesses: the P-code instructions, their inline
// operands and constants, the procedure dictionary and attribute tables of a
// code segment, and the segment loader's writes. Every read of code goes
// through PM_CODE8/PM_CODE16 and every write of code through PM_CODEW8, so a
// host can keep code in a memory of its own (the Harvard layout of P-Code
// mode); a host with one memory maps them onto it.
#define PM_CODE8(a) (m_mem[(uint16_t)(a)])
#define PM_CODE16(a) ((uint16_t)(m_mem[(uint16_t)(a)] | (m_mem[(uint16_t)((a) + 1)] << 8)))
#define PM_CODEW8(a, v) (m_mem[(uint16_t)(a)] = (uint8_t)(v))
#define PM_IOCFG IoConfig()   // unit-I/O configuration (PCodeOpcodes.h)
// run-time errors (NativeErrors.inc)
#define PM_NATIVE_ERRORS (m_nativePcodeOps && !m_preserveZ80RegisterCompat)
#define PM_CXP02 m_cxp02
#define PM_INTEND (m_reclaimed ? m_heapStart : (uint16_t)(PM_ROM(0x03EA) | (PM_ROM(0x03EB) << 8)))
#include "UcsdReal.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <climits>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <filesystem>

// ---- UCSD 4-byte REAL format conversion -----------------------------
// Format, per FPL.TEXT's own documented comment: four bytes [exp]
// [sabc v] [w x] [y z], where s,a,b,c are individual bits, v,w,x,y,z are
// hex digits. Exponent is biased by 128 (exp==0 means the value is
// zero). Mantissa is normalized to [0.5,1.0) with an implicit leading
// bit just after the binary point: value = (sign?-1:1) * .1abcvwxyz(bin)
// * 2^(exp-128). Reverse-engineered from that comment and FPFNEG's own
// "flip bit 7 of the second byte" logic, then verified bit-exact
// against the source's own hardcoded FPCPI4 constant (pi/4 = 80 49 0F
// DB) before being used anywhere below.
static double DecodeUcsdReal(uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
    if (b0 == 0) return 0.0;
    int exp = (int)b0 - 128;
    bool sign = (b1 & 0x80) != 0;
    uint32_t fracBits = ((uint32_t)(b1 & 0x7F) << 16) | ((uint32_t)b2 << 8) | b3;
    double mantissa = 0.5 + (double)fracBits / 16777216.0; // 2^24
    double value = mantissa * std::ldexp(1.0, exp);
    return sign ? -value : value;
}

static bool EncodeUcsdReal(double v, uint8_t& b0, uint8_t& b1, uint8_t& b2, uint8_t& b3) {
    if (v == 0.0) { b0 = b1 = b2 = b3 = 0; return true; }
    bool sign = v < 0.0;
    v = std::fabs(v);
    int exp = 0;
    double m = std::frexp(v, &exp); // m in [0.5,1.0), v = m*2^exp -- matches our convention exactly
    uint32_t fracBits = (uint32_t)std::llround((m - 0.5) * 16777216.0);
    if (fracBits >= (1u << 23)) { fracBits = 0; exp += 1; } // rounding overflow into the next power of two
    int expByte = exp + 128;
    if (expByte < 1 || expByte > 255) return false; // out of representable range (0 is reserved for "zero")
    b0 = (uint8_t)expByte;
    b1 = (uint8_t)((sign ? 0x80 : 0) | ((fracBits >> 16) & 0x7F));
    b2 = (uint8_t)((fracBits >> 8) & 0xFF);
    b3 = (uint8_t)(fracBits & 0xFF);
    return true;
}


// Reverse lookup: given the Z80 PC value the dispatch shortcut (or real
// Z80 BACK code continuing to run after a native commit) has arrived at,
// return which p-code opcode's own Z80 handler entry point this is, or
// -1 if PC isn't one of these 68 known addresses. This lets the native
// switch below dispatch on the opcode value itself (case OP_ADI, etc.)
// while still firing in every situation the original switch(PC) design
// did -- including when PC reaches a target via real BACK (or BACK1)
// code naturally continuing to execute one instruction at a time after
// a previous native commit, not just immediately following the
// shortcut's own direct computation. Checked every iteration,
// unconditionally, exactly like the original address-keyed switch was.
static int NativeOpcodeForTarget(uint16_t pc) {
    switch (pc) {
        case 0x0740: return OP_ADI;
        case 0x077C: return OP_NGI;
        case 0x0788: return OP_SBI;
        case 0x0791: return OP_CHK;
        case 0x0722: return OP_NOT;
        case 0x072D: return OP_ABI;
        case 0x087C: return OP_EQUI;
        case 0x08B8: return OP_NEQI;
        case 0x088F: return OP_GEQI;
        case 0x08A7: return OP_GTRI;
        case 0x08CB: return OP_LEQI;
        case 0x08D0: return OP_LESI;
        case 0x1185: return OP_UJP;
        case 0x04A7: return OP_STL;
        case 0x0557: return OP_IXA;
        case 0x05CC: return OP_LDB;
        case 0x04C6: return OP_LAO;
        case 0x070A: return OP_LAND;
        case 0x0522: return OP_INCR;
        case 0x0498: return OP_LDL;
        case 0x0716: return OP_LOR;
        case 0x05D6: return OP_STB;
        case 0x06A4: return OP_STP;
        case 0x0507: return OP_LDA;
        case 0x045D: return OP_LDCI;
        case 0x11EF: return OP_RBP;
        case 0x052C: return OP_STO;
        case 0x054A: return OP_STIND;
        case 0x15B8: return OP_CSP;
        case 0x048C: return OP_LLA;
        case 0x0534: return OP_SIND0;
        case 0x050E: return OP_LOD;
        case 0x0467: return OP_LDCN;
        case 0x0598: return OP_LDM;
        case 0x05B0: return OP_STM;
        case 0x0C55: return OP_INN;
        case 0x1398: return OP_CXP;
        case 0x11A6: return OP_XJP;
        case 0x0518: return OP_STR;
        case 0x05DF: return OP_LPA;
        case 0x05EB: return OP_LSA;
        case 0x0619: return OP_SAS;
        case 0x066D: return OP_LDP;
        case 0x1337: return OP_CLP;
        case 0x1348: return OP_CBP;
        case 0x1369: return OP_CIP;
        case 0x133D: return OP_CGP;
        case 0x09F1: return OP_DIF;
        case 0x0A12: return OP_UNI;
        case 0x0B66: return OP_SGS;
        case 0x0B69: return OP_SRS;
        case 0x0644: return OP_IXP;
        case 0x0C00: return OP_ADJ;
        case 0x056E: return OP_MOV;
        case 0x0582: return OP_LDC;
        case 0x0747: return OP_DVI;
        case 0x0761: return OP_MPI;
        case 0x0754: return OP_MODI;
        case 0x1201: return OP_RNP;
        case 0x08D5: return OP_CEQU;
        case 0x08E2: return OP_CNEQ;
        case 0x08EF: return OP_CGTR;
        case 0x08FB: return OP_CLEQ;
        case 0x0907: return OP_CLSS;
        case 0x0910: return OP_CGEQ;
        case 0x04D2: return OP_LDO;
        case 0x04E1: return OP_SRO;
        case 0x117D: return OP_FJP;
#include "NativeOpsTargets.inc"
        default: return -1;
    }
}

PSystemEngine::PSystemEngine() {
    InitializeCriticalSection(&m_gridLock);
    InitializeCriticalSection(&m_keyLock);
    m_keyAvailable = CreateEventW(nullptr, TRUE, FALSE, nullptr);  // manual-reset
    m_stopEvent    = CreateEventW(nullptr, TRUE, FALSE, nullptr);  // manual-reset
    m_grid.assign((size_t)TERM_COLS * TERM_ROWS, ' ');

    memset(m_mem, 0xE5, sizeof(m_mem));
    m_cpu.reset();
    m_cpu.rd = [this](uint16_t a) -> uint8_t { return m_mem[a]; };
    m_cpu.wr = [this](uint16_t a, uint8_t v) { m_mem[a] = v; };
    m_cpu.in_port = [this](uint8_t p) -> uint8_t { return PortIn(p); };
    m_cpu.out_port = [this](uint8_t p, uint8_t v) { PortOut(p, v); };
}

PSystemEngine::~PSystemEngine() {
    Stop();
    if (m_traceFile) fclose(m_traceFile);
    if (m_keyAvailable) CloseHandle(m_keyAvailable);
    if (m_stopEvent) CloseHandle(m_stopEvent);
    DeleteCriticalSection(&m_gridLock);
    DeleteCriticalSection(&m_keyLock);
    // Flush any in-progress punch session -- if the emulator is closed
    // while a Pascal program was mid-transfer, preserve what arrived.
    if (m_punchFile.is_open()) {
        PunchFlushHexBuffer();
        m_punchFile.close();
    }
}

static bool LoadWholeFile(const std::wstring& path, std::vector<uint8_t>& out) {
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(sz > 0 ? (size_t)sz : 0);
    if (sz > 0) { size_t n = fread(out.data(), 1, (size_t)sz, f); (void)n; }
    fclose(f);
    return true;
}

bool PSystemEngine::LoadFiles(const std::wstring& pascalBinPath,
                              const std::wstring& bigDiskPath,
                              const std::wstring& emptyDiskPath,
                              std::wstring& errorMessageOut) {
    if (!LoadWholeFile(bigDiskPath, m_volDrive0)) {
        errorMessageOut = L"Could not open Big Disk image:\r\n" + bigDiskPath;
        return false;
    }
    m_volDrive0Path = bigDiskPath;
    StampDrive(0);
    // Empty/scratch disk (unit 5) is optional -- not every session needs it.
    if (LoadWholeFile(emptyDiskPath, m_volDrive1)) m_volDrive1Path = emptyDiskPath;
    StampDrive(1);

    FILE* pf = nullptr;
    _wfopen_s(&pf, pascalBinPath.c_str(), L"rb");
    memset(m_mem, 0xE5, sizeof(m_mem));
    // pascal.bin (the Z80 loader and BIOS) is only needed for the Z80 boot.
    // P-Code mode with register compatibility off and reclaimed memory boots
    // natively without it; if the Z80 boot turns out to be needed and it is
    // missing, RunLoop stops with BootFault().
    m_haveLoader = pf != nullptr;
    if (pf) {
        size_t n = fread(m_mem + 0xF000, 1, 0x10000 - 0xF000, pf);
        (void)n;
        fclose(pf);
    }

    m_cpu.reset();
    m_cpu.r.PC = 0xF000;
    m_cpu.r.SP = 0xFDFD;
    return true;
}

bool PSystemEngine::MountUnit9(const std::wstring& path, std::wstring& errorMessageOut) {
    if (!LoadWholeFile(path, m_volDrive2)) {
        errorMessageOut = L"Could not open unit #9 image:\r\n" + path;
        return false;
    }
    m_volDrive2Path = path;
    StampDrive(2);
    return true;
}

bool PSystemEngine::MountUnit10(const std::wstring& path, std::wstring& errorMessageOut) {
    if (!LoadWholeFile(path, m_volDrive3)) {
        errorMessageOut = L"Could not open unit #10 image:\r\n" + path;
        return false;
    }
    m_volDrive3Path = path;
    StampDrive(3);
    return true;
}

bool PSystemEngine::EnableTrace(const std::wstring& tracePath, std::wstring& errorMessageOut) {
    if (m_traceFile) fclose(m_traceFile);
    _wfopen_s(&m_traceFile, tracePath.c_str(), L"w");
    if (!m_traceFile) {
        errorMessageOut = L"Could not create trace file:\r\n" + tracePath;
        m_tracing = false;
        return false;
    }
    m_tracing = true;
    return true;
}

void PSystemEngine::DisableTrace() {
    m_tracing = false;
    if (m_traceFile) { fclose(m_traceFile); m_traceFile = nullptr; }
}

void PSystemEngine::PostKey(uint8_t ch) {
    EnterCriticalSection(&m_keyLock);
    m_keyQueue.push_back(ch);
    LeaveCriticalSection(&m_keyLock);
    SetEvent(m_keyAvailable);
}

// ---- Verify P-System binary P-code log ----
static const int kVerifyRec = 25;
static std::string VerifyDescribe(const uint8_t* r) {
    auto w = [&](int o) { return (unsigned)(r[o] | (r[o + 1] << 8)); };
    char b[200];
    snprintf(b, sizeof b, "IPC=%04X %-7s(%02X) SP=%04X NP=%04X MPD0=%04X BASED0=%04X IPCSAV=%04X MP=%04X BASE=%04X JTAB=%04X SEGP=%04X TOS=%04X %04X",
             w(0), PCodeOpcodeName(r[2]), r[2], w(3), w(5), w(7), w(9), w(11), w(13), w(15), w(17), w(19), w(21), w(23));
    return b;
}

bool PSystemEngine::StartVerifyLog(const std::wstring& path, bool compare, std::wstring& errorMessageOut) {
    StopVerifyLog();
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), compare ? L"rb" : L"wb");
    if (!f) { errorMessageOut = L"Cannot open the verify log: " + path; return false; }
    setvbuf(f, nullptr, _IOFBF, 1 << 20);
    m_verifyFile = f;
    m_verifyCompare = compare;
    m_verifyCount = 0;
    m_verifyMismatch.clear();
    m_verifyRecent.clear();
    m_verifyHaltNow = false;
    m_verifyMinSP = m_cpu.r.SP;
    return true;
}

void PSystemEngine::StopVerifyLog() {
    if (!m_verifyFile) return;
    if (m_verifyCompare && m_verifyMismatch.empty()) {
        uint8_t x;
        if (fread(&x, 1, 1, m_verifyFile) == 1)
            m_verifyMismatch = "The run ended after " + std::to_string(m_verifyCount) +
                               " P-code instructions, but the reference log has more.";
    }
    fclose(m_verifyFile);
    m_verifyFile = nullptr;
}

void PSystemEngine::VerifyLogStep() {
    // Normalize the free stack space. Z80 routines CALL helpers and PUSH
    // temporaries onto the P-machine stack, leaving return addresses and
    // other left-overs below SP; native code leaves different ones (or
    // none). Programs that read a never-initialized variable pick those up,
    // so they would make the Z80 and native runs differ for reasons that
    // have nothing to do with P-code semantics. So, in verify runs only and
    // identically in both modes, the stack space used below SP since the
    // previous P-code instruction is cleared at every instruction boundary.
    // The P-machine never legitimately reads below SP.
    // Safeguards: tracking starts at the first P-code instruction (the boot
    // loader runs its stack elsewhere), and nothing at or below the heap top
    // (NP) is ever cleared -- a dip that far means SP was somewhere else
    // entirely, not a normal use of the free stack space.
    {
        uint16_t sp = m_cpu.r.SP;
        uint16_t np = (uint16_t)(m_mem[PM_V(0x0240)] | (m_mem[PM_V(0x0241)] << 8));
        if (m_verifyCount > 0 && m_verifyMinSP < sp && m_verifyMinSP > np)
            memset(m_mem + m_verifyMinSP, 0, (size_t)(sp - m_verifyMinSP));
        m_verifyMinSP = sp;
    }
    uint8_t r[kVerifyRec];
    auto put = [&](int o, uint16_t v) { r[o] = (uint8_t)v; r[o + 1] = (uint8_t)(v >> 8); };
    auto rd = [&](uint16_t a) { return (uint16_t)(m_mem[a] | (m_mem[(uint16_t)(a + 1)] << 8)); };
    uint16_t bc = m_cpu.r.BC(), sp = m_cpu.r.SP;
    put(0, bc); r[2] = PM_CODE8(bc); put(3, sp);
    put(5, rd(PM_V(0x0240))); put(7, rd(PM_V(0x0242))); put(9, rd(PM_V(0x0244))); put(11, rd(PM_V(0x0246)));
    put(13, rd(PM_V(0x02F2))); put(15, rd(PM_V(0x02F0))); put(17, rd(PM_V(0x02F4))); put(19, rd(PM_V(0x02F6)));
    put(21, rd(sp)); put(23, rd((uint16_t)(sp + 2)));
    if (!m_verifyCompare) {
        fwrite(r, 1, kVerifyRec, m_verifyFile);
    } else if (!m_verifyMismatch.empty()) {
        return;   // keep the FIRST mismatch; the engine is already stopping
    } else {
        uint8_t ref[kVerifyRec];
        if (fread(ref, 1, kVerifyRec, m_verifyFile) != (size_t)kVerifyRec) {
            m_verifyMismatch = "The reference log ended after " + std::to_string(m_verifyCount) +
                               " P-code instructions, but this run continued:\n  now:       " + VerifyDescribe(r);
            m_verifyHaltNow = true;
            SetEvent(m_stopEvent);
        } else if (memcmp(r, ref, kVerifyRec) != 0) {
            std::string m = "P-code instruction #" + std::to_string(m_verifyCount + 1) + " differs from the reference log.\n";
            m += "  reference: " + VerifyDescribe(ref) + "\n  this run:  " + VerifyDescribe(r) + "\n";
            m += "Preceding instructions (identical in both):\n";
            for (size_t o = 0; o + kVerifyRec <= m_verifyRecent.size(); o += kVerifyRec)
                m += "  " + VerifyDescribe(&m_verifyRecent[o]) + "\n";
            m_verifyMismatch = m;
            m_verifyHaltNow = true;
            SetEvent(m_stopEvent);
        }
    }
    m_verifyRecent.insert(m_verifyRecent.end(), r, r + kVerifyRec);
    if (m_verifyRecent.size() > (size_t)kVerifyRec * 8) m_verifyRecent.erase(m_verifyRecent.begin(), m_verifyRecent.begin() + kVerifyRec);
    m_verifyCount++;
    if (m_verifyStopAt && m_verifyCount == m_verifyStopAt) { m_verifyHaltNow = true; SetEvent(m_stopEvent); }
}

size_t PSystemEngine::PendingKeyCount() {
    EnterCriticalSection(&m_keyLock);
    size_t n = m_keyQueue.size();
    LeaveCriticalSection(&m_keyLock);
    return n;
}

void PSystemEngine::SetConsoleCapture(bool on) {
    EnterCriticalSection(&m_gridLock);
    m_captureConsole = on;
    if (!on) m_consoleCapture.clear();
    LeaveCriticalSection(&m_gridLock);
}

std::string PSystemEngine::TakeConsoleCapture() {
    EnterCriticalSection(&m_gridLock);
    std::string s;
    s.swap(m_consoleCapture);
    LeaveCriticalSection(&m_gridLock);
    return s;
}

void PSystemEngine::SnapshotGrid(std::vector<uint8_t>& gridOut, int& cursorXOut, int& cursorYOut) {
    EnterCriticalSection(&m_gridLock);
    gridOut = m_grid;
    cursorXOut = m_cursorX;
    cursorYOut = m_cursorY;
    LeaveCriticalSection(&m_gridLock);
}

void PSystemEngine::ScrollIfNeeded() {
    // Caller already holds m_gridLock.
    if (m_cursorY >= TERM_ROWS) {
        int overflow = m_cursorY - TERM_ROWS + 1;
        size_t rowBytes = (size_t)TERM_COLS;
        memmove(m_grid.data(), m_grid.data() + (size_t)overflow * rowBytes,
                (TERM_ROWS - overflow) * rowBytes);
        memset(m_grid.data() + (size_t)(TERM_ROWS - overflow) * rowBytes, ' ',
               (size_t)overflow * rowBytes);
        m_cursorY = TERM_ROWS - 1;
    }
}

// Mirrors vt100ConOut's control-code table exactly, but instead of
// emitting ANSI escapes, directly manipulates the internal grid/cursor.
void PSystemEngine::PutChar(uint8_t v) {
    EnterCriticalSection(&m_gridLock);
    if (m_captureConsole) m_consoleCapture.push_back((char)v);

    if (m_ctrlCharsLeft == 2) { m_ctrlParamX = v; m_ctrlCharsLeft = 1; LeaveCriticalSection(&m_gridLock); return; }
    if (m_ctrlCharsLeft == 1) {
        int x = m_ctrlParamX - 32, y = v - 32;
        if (x < 0) x = 0;
        if (x >= TERM_COLS) x = TERM_COLS - 1;
        if (y < 0) y = 0;
        if (y >= TERM_ROWS) y = TERM_ROWS - 1;
        m_cursorX = x; m_cursorY = y;
        m_ctrlCharsLeft = 0;
        LeaveCriticalSection(&m_gridLock);
        return;
    }

    if (v < 32 || v == 127) {
        switch (v) {
            case 1: m_ctrlCharsLeft = 2; break;                    // begin gotoxy sequence
            case 2: {                                              // clear to end of line
                size_t off = (size_t)m_cursorY * TERM_COLS + m_cursorX;
                memset(m_grid.data() + off, ' ', TERM_COLS - m_cursorX);
                break;
            }
            case 3: {                                              // clear to end of screen
                size_t off = (size_t)m_cursorY * TERM_COLS + m_cursorX;
                memset(m_grid.data() + off, ' ', m_grid.size() - off);
                break;
            }
            case 4: if (m_cursorY > 0) m_cursorY--; break;          // cursor up
            case 5: if (m_cursorX < TERM_COLS - 1) m_cursorX++; break; // cursor right
            case 6: m_cursorX = 0; m_cursorY = 0; break;            // home
            case 8: if (m_cursorX > 0) m_cursorX--; break;          // backspace
            case 9: m_cursorX = (m_cursorX / 8 + 1) * 8; if (m_cursorX >= TERM_COLS) m_cursorX = TERM_COLS - 1; break; // tab
            case 10: m_cursorY++; ScrollIfNeeded(); break;          // linefeed
            case 12: memset(m_grid.data(), ' ', m_grid.size()); m_cursorX = 0; m_cursorY = 0; break; // clear screen + home
            case 13: m_cursorX = 0; break;                          // CR
            case 14: case 15: break;                                // normal/reverse video: no attribute grid yet, ignore
            default: break;
        }
        LeaveCriticalSection(&m_gridLock);
        return;
    }

    // Printable character.
    if (m_cursorX >= 0 && m_cursorX < TERM_COLS && m_cursorY >= 0 && m_cursorY < TERM_ROWS) {
        m_grid[(size_t)m_cursorY * TERM_COLS + m_cursorX] = v;
    }
    m_cursorX++;
    if (m_cursorX >= TERM_COLS) { m_cursorX = 0; m_cursorY++; ScrollIfNeeded(); }

    LeaveCriticalSection(&m_gridLock);
}

int PSystemEngine::UnitValueToDrive(uint8_t v) {
    switch (v) {
        case 4: return 0; case 5: return 1; case 9: return 2; case 10: return 3;
        case 11: return 4; case 12: return 5; case 13: return 6; case 14: return 7; case 15: return 8;
        default: return -1;
    }
}

std::vector<uint8_t>* PSystemEngine::DriveImage(int drive) {
    if (drive == 0) return &m_volDrive0;
    if (drive == 1) return &m_volDrive1;
    if (drive == 2) return &m_volDrive2;
    if (drive == 3) return &m_volDrive3;
    return nullptr;
}

std::wstring* PSystemEngine::DrivePath(int drive) {
    if (drive == 0) return &m_volDrive0Path;
    if (drive == 1) return &m_volDrive1Path;
    if (drive == 2) return &m_volDrive2Path;
    if (drive == 3) return &m_volDrive3Path;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Protection against writing an old copy over a newer file
// ---------------------------------------------------------------------------
// The emulator reads a whole volume image into memory when it mounts it and
// writes each block the P-System writes straight back into the file. If the
// file is replaced while the emulator runs (a git pull of the volumes, a copy
// from Explorer, another program), those writes would put the OLD copy's
// blocks -- the directory above all -- into the NEW file, leaving a mix of
// the two. So the file's size and last-write time are recorded whenever the
// emulator loads or writes it, and a write to a file that no longer matches
// is refused. The UI warns once (TakeChangedOnDiskUnit); re-opening the unit
// or restarting the emulator loads the new file.

static bool StatImageFile(const std::wstring& path, uint64_t& size, int64_t& mtime) {
    std::error_code ec;
    const std::filesystem::path p(path);
    size = (uint64_t)std::filesystem::file_size(p, ec);
    if (ec) return false;
    mtime = (int64_t)std::filesystem::last_write_time(p, ec).time_since_epoch().count();
    return !ec;
}

void PSystemEngine::StampDrive(int drive) {
    auto* path = DrivePath(drive);
    if (drive < 0 || drive > 3 || !path) return;
    DiskStamp& s = m_diskStamp[drive];
    s.valid = !path->empty() && StatImageFile(*path, s.size, s.mtime);
    m_changedOnDisk[drive] = false;
}

bool PSystemEngine::DriveFileUnchanged(int drive) {
    if (drive < 0 || drive > 3) return true;
    if (m_changedOnDisk[drive]) return false;           // already refused: stay refused
    const DiskStamp& s = m_diskStamp[drive];
    if (!s.valid) return true;                          // nothing recorded: no check
    uint64_t size = 0; int64_t mtime = 0;
    if (StatImageFile(*DrivePath(drive), size, mtime) && size == s.size && mtime == s.mtime)
        return true;
    m_changedOnDisk[drive] = true;
    static const int units[4] = { 4, 5, 9, 10 };
    m_changedOnDiskPending.store(units[drive]);
    return false;
}

int PSystemEngine::TakeChangedOnDiskUnit(std::wstring& pathOut) {
    const int unit = m_changedOnDiskPending.exchange(0);
    if (unit) {
        const int drive = unit == 4 ? 0 : unit == 5 ? 1 : unit == 9 ? 2 : 3;
        pathOut = *DrivePath(drive);
    }
    return unit;
}

bool PSystemEngine::IsChangedOnDisk(int unit) const {
    const int drive = unit == 4 ? 0 : unit == 5 ? 1 : unit == 9 ? 2 : unit == 10 ? 3 : -1;
    return drive >= 0 && m_changedOnDisk[drive];
}

bool PSystemEngine::FlushDriveRegion(int drive, long fileOffset, int length) {
    auto* img = DriveImage(drive);
    auto* path = DrivePath(drive);
    if (!img || !path || path->empty()) { m_diskFlushFailCount++; return false; }
    if (fileOffset < 0 || (size_t)(fileOffset + length) > img->size()) { m_diskFlushFailCount++; return false; }
    if (!DriveFileUnchanged(drive)) { m_diskFlushFailCount++; return false; }   // file replaced since loaded

    // "r+b" requires the file to already exist (it does -- we loaded it
    // from here) and lets us seek+overwrite in place without truncating
    // or rewriting the whole multi-megabyte image for one small change.
    FILE* f = nullptr;
    _wfopen_s(&f, path->c_str(), L"r+b");
    if (!f) { m_diskFlushFailCount++; return false; }
    bool ok = (fseek(f, fileOffset, SEEK_SET) == 0) &&
              (fwrite(img->data() + fileOffset, 1, (size_t)length, f) == (size_t)length);
    fclose(f);
    if (ok) m_diskFlushOkCount++; else m_diskFlushFailCount++;
    StampDrive(drive);                                  // our own write: the new baseline
    return ok;
}

// ---------------------------------------------------------------------------
// UCSD Pascal volume directory helpers
// ---------------------------------------------------------------------------

// Unit-number-to-drive-index mapping (4→0, 5→1, 9→2, 10→3).
static int UnitToDriveIndex(int unit) {
    if (unit == 4)  return 0;
    if (unit == 5)  return 1;
    if (unit == 9)  return 2;
    if (unit == 10) return 3;
    return -1;
}

// UCSD Pascal disk layout (II.0, 512-byte blocks):
//   Blocks 0-1 : boot area (not directory)
//   Blocks 2-5 : directory (4 blocks = 2048 bytes at byte offset 1024)
//   Block 6+   : file data
//
// The directory at byte 1024 begins with a 26-byte volume header, then up
// to 77 26-byte file entries ((2048 - 26) / 26 = 77).
//
// Volume header layout (26 bytes at byte 1024):
//   [0-1]  DFIRSTBLK  (always 0)
//   [2-3]  DLASTBLK   (first data block, typically 6)
//   [4-5]  DFIBKIND   (0 = directory)
//   [6]    length of volume name (max 7)
//   [7-13] volume name characters (7 bytes)
//   [14-15] DEOVBLK   (total blocks on disk)
//   [16-17] DNUMFILES (number of file directory entries)
//   [18-25] dates / padding
//
// File entry layout (26 bytes):
//   [0-1]  DFIRSTBLK
//   [2-3]  DLASTBLK   (= first block PAST the file)
//   [4-5]  DFIBKIND   (lower 4 bits: 0=untyped,2=code,3=text,4=info,
//                       5=data,6=graf,7=foto)
//   [6]    length of filename (max 15)
//   [7-21] filename characters (15 bytes, zero-padded)
//   [22-23] DLASTBYTE (bytes used in the last block; 512 = last block full)
//   [24-25] DACCESS   (date word)

static const int UCSD_DIR_BYTE_OFFSET = 1024; // block 2 × 512
static const int UCSD_ENTRY_SIZE      = 26;
static const int UCSD_MAX_ENTRIES     = 77;   // (2048 - 26) / 26

std::wstring PSystemEngine::GetVolumeNameForUnit(int unit) const {
    int drive = UnitToDriveIndex(unit);
    if (drive < 0) return L"";
    const std::vector<uint8_t>* img = nullptr;
    if (drive == 0) img = &m_volDrive0;
    else if (drive == 1) img = &m_volDrive1;
    else if (drive == 2) img = &m_volDrive2;
    else if (drive == 3) img = &m_volDrive3;
    if (!img || img->size() < (size_t)(UCSD_DIR_BYTE_OFFSET + 26)) return L"";
    int nameLen = (*img)[UCSD_DIR_BYTE_OFFSET + 6];
    if (nameLen < 0 || nameLen > 7) return L"";
    std::wstring name;
    for (int i = 0; i < nameLen; i++)
        name += (wchar_t)(*img)[UCSD_DIR_BYTE_OFFSET + 7 + i];
    return name;
}

void PSystemEngine::UnmountUnit(int unit) {
    int drive = UnitToDriveIndex(unit);
    if (drive < 0) return;
    auto* img  = DriveImage(drive);
    auto* path = DrivePath(drive);
    if (img)  img->clear();
    if (path) path->clear();
    m_diskStamp[drive] = DiskStamp();
    m_changedOnDisk[drive] = false;
}

// ---------------------------------------------------------------------------
// NativeBldmscw -- shared core of CLP and CBP
// ---------------------------------------------------------------------------
// Implements the body of BLDMSCW (0x127B) for Pascal (non-assembly) same-
// segment calls.  On success it advances BC past the proc_num byte, writes
// SAVIPC and the five P-machine registers (MP/MPD0/SEGP/JTAB/NEWSEG/NEWJTB),
// pushes the six MSCW words, and sets BC = entry address, then returns true.
// Returns false when the called procedure is an assembly-language entry
// (first byte of jtab = 0) or when the stack-overflow check fires -- the
// caller then falls straight through to the real Z80 code at 0x1337/0x1348.
//
// Layout of the six-word MSCW on the stack after this call (SP grows down,
// so lowest address = last word pushed):
//   [MP+ 0] msstat  -- static link (= msdyn for CLP; CBP overwrites below)
//   [MP+ 2] msdyn   -- dynamic link  = caller's MP
//   [MP+ 4] msjtab  -- caller's JTAB
//   [MP+ 6] msseg   -- caller's SEGP
//   [MP+ 8] msipc   -- saved IPC (= IPCSAV = address of next caller byte)
//   [MP+10] mssp    -- caller's SP before parameters were pushed
//
// MPD0 (0x0242) = MP + 10 = &mssp.
// Perform a CP/M BIOS call natively -- exactly what the BIOS-linker
// intercept in RunLoop does for the same function -- for the native unit
// I/O in NativeCsp.inc. dryRun: only report whether fn is handled natively
// (anything else, e.g. READER, runs the real Z80 BIOS code instead).
bool PSystemEngine::NativeBiosCall(uint8_t fn, uint8_t c, uint8_t* aOut, bool dryRun) {
    switch (fn) {
        case 0x06: case 0x09: case 0x0C: case 0x18: case 0x0F: case 0x12: break;
        default: return false;
    }
    if (dryRun) return true;
    uint8_t a = 0;
    if (fn == 0x06) a = PortIn(0);            // CONST
    else if (fn == 0x09) a = PortIn(1);       // CONIN
    else if (fn == 0x0C) PortOut(1, c);       // CONOUT
    else if (fn == 0x0F) PrinterWriteChar(c); // LIST
    else if (fn == 0x12) PunchWriteChar(c);   // PUNCH
    // 0x18 HOME: nothing to do
    if (aOut) *aOut = a;
    m_biosLinkerNativeCount++;
    return true;
}

// ===================== Native boot (P-Code mode) =====================
// The interpreter's BOOT code (BIGGY's SYSTEM.MICRO, 0x1EBA-0x2018: IOINIT,
// SYSTLE, DENTP/SYSBLK/SEGCNT, BOOT). NativeBoot implements exactly this code
// and refuses to run if the loaded interpreter's bytes differ.
static const uint8_t kBiggyBootCode[0x2019 - 0x1EBA] = {   // as in the SYSTEM.MICRO file (before BOOT runs)
    0x2A, 0x01, 0x00, 0x11, 0xEF, 0xFF, 0x19, 0x22, 0xF8, 0x02, 0xC9, 0xE1, 0x22, 0x03, 0x02, 0xC3,
    0xA4, 0x03, 0x0D, 0x53, 0x59, 0x53, 0x54, 0x45, 0x4D, 0x2E, 0x50, 0x41, 0x53, 0x43, 0x41, 0x4C,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x31, 0x19, 0x38, 0xCD, 0xBA, 0x1E, 0x2A, 0xE8, 0x02, 0xE5, 0x21,
    0x19, 0x20, 0xE5, 0x21, 0x00, 0x00, 0xE5, 0x21, 0x00, 0x08, 0xE5, 0x21, 0x02, 0x00, 0xE5, 0xCD,
    0xA0, 0x1B, 0x21, 0x33, 0x20, 0x22, 0xDA, 0x1E, 0x0E, 0x00, 0x11, 0x06, 0x00, 0x19, 0x11, 0xCC,
    0x1E, 0x06, 0x0E, 0x1A, 0xBE, 0xC2, 0x19, 0x1F, 0x13, 0x23, 0x10, 0xF7, 0xC3, 0x2A, 0x1F, 0x2A,
    0xDA, 0x1E, 0x11, 0x1A, 0x00, 0x19, 0x22, 0xDA, 0x1E, 0x0D, 0xC2, 0x04, 0x1F, 0xC3, 0x27, 0x1F,
    0x2A, 0xE8, 0x02, 0xE5, 0x21, 0x19, 0x28, 0xE5, 0x21, 0x00, 0x00, 0xE5, 0x21, 0x40, 0x00, 0xE5,
    0x2A, 0xDA, 0x1E, 0x4E, 0x23, 0x46, 0xC5, 0x69, 0x60, 0x22, 0xDC, 0x1E, 0xCD, 0xA0, 0x1B, 0x3E,
    0x10, 0x32, 0xDE, 0x1E, 0x11, 0x44, 0x03, 0x21, 0x19, 0x28, 0x3A, 0xE8, 0x02, 0x12, 0x13, 0xAF,
    0x12, 0x13, 0x4E, 0x23, 0x46, 0x23, 0xE5, 0x2A, 0xDC, 0x1E, 0x09, 0xEB, 0x73, 0x23, 0x72, 0x23,
    0xEB, 0xE1, 0x7E, 0x12, 0x13, 0x23, 0x7E, 0x12, 0x13, 0x23, 0x3A, 0xDE, 0x1E, 0x3D, 0x32, 0xDE,
    0x1E, 0xC2, 0x54, 0x1F, 0x21, 0x48, 0x03, 0x5E, 0x23, 0x56, 0x2A, 0xF8, 0x02, 0x23, 0x23, 0xA7,
    0xED, 0x52, 0xF9, 0x3A, 0xE8, 0x02, 0x4F, 0x06, 0x00, 0xC5, 0xE5, 0x21, 0x00, 0x00, 0xE5, 0xD5,
    0x2A, 0x46, 0x03, 0xE5, 0xCD, 0xA0, 0x1B, 0x21, 0x54, 0x02, 0x01, 0xC4, 0xFF, 0xAF, 0x77, 0x23,
    0x0C, 0xC2, 0xA8, 0x1F, 0x04, 0xC2, 0xA8, 0x1F, 0x21, 0x01, 0x00, 0x22, 0x50, 0x02, 0x2A, 0xF8,
    0x02, 0x22, 0x52, 0x02, 0x2A, 0xF8, 0x02, 0x22, 0xF6, 0x02, 0x2B, 0x46, 0x2B, 0x4E, 0xA7, 0xED,
    0x42, 0x22, 0xF4, 0x02, 0x2B, 0x46, 0x2B, 0x4E, 0xA7, 0xED, 0x42, 0x22, 0x46, 0x02, 0x2A, 0xF4,
    0x02, 0x01, 0xF8, 0xFF, 0x09, 0x4E, 0x23, 0x46, 0xAF, 0x91, 0x6F, 0x3E, 0x00, 0x98, 0x67, 0x39,
    0xF9, 0x11, 0xE4, 0x02, 0xD5, 0xE5, 0x21, 0xFC, 0xFF, 0x39, 0xE5, 0x21, 0xD6, 0x00, 0xE5, 0xE5,
    0x21, 0xFC, 0xFF, 0x39, 0xE5, 0xE5, 0x22, 0xF2, 0x02, 0x22, 0xF0, 0x02, 0x01, 0x0A, 0x00, 0x09,
    0x22, 0x42, 0x02, 0x22, 0x44, 0x02, 0x21, 0xBA, 0x1E, 0x22, 0x40, 0x02, 0xC3, 0xA4, 0x03,
};

// SYSRD(unit, buf, 0, len, block) as the BOOT code calls it: the five
// parameters and the return address are pushed exactly where the Z80 pushes
// them (they stay in free memory below the stack), then the unit I/O runs as
// the interpreter's SYSRD -> SYSIO -> GETU -> CALLIO -> Big Disk driver does:
// the same I/O variables and the same controller port sequence.
void PSystemEngine::NativeBootSysrd(uint16_t unitWord, uint16_t buf, uint16_t len, uint16_t block, uint16_t retAddr, uint16_t& sp) {
    auto wr16 = [&](uint16_t a, uint16_t v) { m_mem[a] = (uint8_t)v; m_mem[(uint16_t)(a + 1)] = (uint8_t)(v >> 8); };
    auto push = [&](uint16_t v) { sp = (uint16_t)(sp - 2); wr16(sp, v); };
    push(unitWord); push(buf); push(0); push(len); push(block);
    push(retAddr);                                   // CALL SYSRD
    const uint8_t unit = (uint8_t)unitWord;
    m_mem[PM_V(0x02D2)] = 0x01;                            // UREQ := INBIT
    wr16(PM_V(0x02DC), retAddr);                           // URTN := return address
    wr16(PM_V(0x02DA), 0);                                 // UASY
    wr16(PM_V(0x02D8), block);                             // UBLK
    wr16(PM_V(0x02D6), len);                               // ULEN
    wr16(PM_V(0x02D4), buf);                               // UBUF := base + index 0
    m_mem[PM_V(0x02E4)] = 0;                               // GETU: IORSLT := 0 (low byte)
    m_mem[PM_V(0x02D3)] = unit;                            // UNIT
    wr16(PM_V(0x02D0), (uint16_t)(0x1AFB + 4 * unit));     // UPTR
    PortOut(0xC8, unit);                             // Big Disk driver
    PortOut(0xCD, (uint8_t)(block >> 8)); PortOut(0xCE, (uint8_t)block);
    PortOut(0xCB, (uint8_t)(len >> 8));   PortOut(0xCC, (uint8_t)len);
    PortOut(0x10, (uint8_t)(buf >> 8));   PortOut(0x0F, (uint8_t)buf);
    PortOut(0xCF, 0x01);
    m_mem[PM_V(0x02E4)] = PortIn(0xCF);                    // status -> IORSLT
    // What the Z80 SYSRD path leaves in those stack slots (they stay in free
    // memory -- the segment-0 read's end up inside the outer block's data):
    // SYSRD: EX (SP),HL puts 0 (the async parameter) where the return address
    // was; SYSIO's CALL GETU pushes its return address 1BCE into the buffer
    // slot, and GETU's EX (SP),HL puts it into the unit slot.
    wr16((uint16_t)(sp + 0), 0);                     // return address slot
    wr16((uint16_t)(sp + 8), 0x1BCE);                // buffer slot
    wr16((uint16_t)(sp + 10), 0x1BCE);               // unit slot
    sp = (uint16_t)(sp + 12);                        // parameters and return address consumed
}

// reclaimLayout (P-Code mode, register compatibility off, Reclaim option):
// the same boot with nothing Z80 at all. SYSTEM.MICRO is not read and
// pascal.bin is not used: the P-machine data the native code needs is
// compiled in (PMachineTables.inc) and placed in the engine's private table
// copy (m_rom) at its usual addresses. The P-System's memory gets only the
// P-machine variable block (SYSCOM, SEGTBL, INTSEGT, the interpreter's own
// variables), moved down from 0x0200-0x03A3 to 0x0080-0x0223 (m_varDelta;
// every native access goes through PM_V), and the 3-byte CXP 0,2 for
// run-time errors at 0x0040. The heap starts at 0x0224 (was 0x1EBA), MEMTOP
// is 0xFFF2 (was 0xFDF2) and segment 0 sits at the very top.
bool PSystemEngine::NativeBoot(std::string& why, bool reclaimLayout) {
    auto rd16 = [&](uint16_t a) { return (uint16_t)(m_mem[a] | (m_mem[(uint16_t)(a + 1)] << 8)); };
    auto wr16 = [&](uint16_t a, uint16_t v) { m_mem[a] = (uint8_t)v; m_mem[(uint16_t)(a + 1)] = (uint8_t)(v >> 8); };
    auto say = [&](const char* t) { for (; *t; t++) PortOut(1, (uint8_t)*t); };
    const std::vector<uint8_t>* disk = DriveImage(UnitValueToDrive(4));
    if (!disk || disk->size() < 6 * 512) { why = "no unit #4 disk image"; return false; }

    // ---- Stage 1: the pascal.bin loader (its code stays in memory, unexecuted) ----
    // Its console lines go through the BIOS: say them as the loader would.
    if (reclaimLayout) {                              // the compiled-in tables, at their usual addresses
        m_rom.assign(65536, 0x00);
        memcpy(&m_rom[0x0100], kPmXfrtbl, sizeof kPmXfrtbl);
        memcpy(&m_rom[0x0200], kPmVarInit, sizeof kPmVarInit);
        memcpy(&m_rom[0x0FF7], kPmTentbl, sizeof kPmTentbl);
        memcpy(&m_rom[0x1766], kPmRestbl, sizeof kPmRestbl);
        memcpy(&m_rom[0x03D7], kPmCxp02, sizeof kPmCxp02);
        memcpy(&m_rom[0x1ECC], kPmSystle, sizeof kPmSystle);
        m_builtInTables = true; m_ioReady = false;
        m_varDelta = 0x0080 - 0x0200;                // the variable block at 0x0080
        memset(m_mem, 0xE5, 65536);                  // nothing of SYSTEM.MICRO or pascal.bin in memory
        memcpy(m_mem + PM_V(0x0200), kPmVarInit, sizeof kPmVarInit);
        memcpy(m_mem + 0x0040, kPmCxp02, sizeof kPmCxp02);
        m_cxp02 = 0x0040;
        m_heapStart = PM_V(0x03A4);                  // 0x0224
        m_reclaimed = true;
        say("\r\nNative boot, P-Code mode -- no Z80 interpreter, BIOS or loader in memory\r\n");
    }
    uint8_t* const ram = reclaimLayout ? m_rom.data() : m_mem;
    auto rdR = [&](uint16_t a) { return (uint16_t)(ram[a] | (ram[(uint16_t)(a + 1)] << 8)); };
    if (!reclaimLayout) {                            // ---- the loader's work, Z80-identical layout ----
    m_mem[0x0000] = 0xC3; m_mem[0x0001] = 0x03; m_mem[0x0002] = 0xFE;   // BIOS warm boot vector
    auto readTo = [&](uint16_t block, uint16_t len, uint16_t buf) -> bool {
        size_t off = (size_t)block * 512;
        if (off + len > disk->size()) return false;
        for (uint16_t i = 0; i < len; i++) ram[(uint16_t)(buf + i)] = (*disk)[off + i];
        return true;
    };
    if (!readTo(2, 4 * 512, 0x0200)) { why = "cannot read the unit #4 directory"; return false; }
    uint16_t dent = (uint16_t)(0x0200 + 0x1A);       // the loader's directory buffer (Z80 layout)
    static const uint8_t microName[13] = { 12, 'S','Y','S','T','E','M','.','M','I','C','R','O' };
    bool microFound = false;
    for (int c = 77; c > 0; c--, dent = (uint16_t)(dent + 0x1A))
        if (memcmp(&ram[(uint16_t)(dent + 6)], microName, 13) == 0) { microFound = true; break; }
    if (!microFound) { why = "SYSTEM.MICRO not found on unit #4"; return false; }
    const uint16_t microFirst = rdR(dent), microLen = (uint16_t)((rdR((uint16_t)(dent + 2)) - microFirst) * 512);
    say("\r\nLoading SYSTEM.MICRO (P-Code Interpreter tables) -- native boot, P-Code mode\r\n");
    if (!readTo(microFirst, microLen, 0x0100)) { why = "cannot read SYSTEM.MICRO"; return false; }
    if (ram[0x0100] != 0x2D) memmove(ram + 0x0100, ram + 0x0400, 0x5000);  // do_move: LDIR 0400 -> 0100
    ram[0x0203] = 0x00; ram[0x0204] = 0xFE;                              // FIRSTSP := BIOS page
    {   // The loader's stack (from 0x0100) leaves the return addresses of its
        // last calls in page zero. Specific to this pascal.bin: written only if
        // its loader code is the known one (FNV-1a of F000-F2FF).
        uint64_t hsh = 0xCBF29CE484222325ull;
        for (int i = 0; i < 0x300; i++) hsh = (hsh ^ m_mem[0xF000 + i]) * 0x100000001B3ull;
        static const uint8_t pageZero[10] = { 0x50, 0xF2, 0x50, 0xF2, 0x77, 0xF1, 0x3E, 0xF2, 0x08, 0xF1 };
        if (hsh == 0xC9D12F8C84E87938ULL) memcpy(m_mem + 0x00F6, pageZero, sizeof pageZero);
    }
    if (memcmp(ram + 0x1EBA, kBiggyBootCode, sizeof kBiggyBootCode) != 0) {
        why = "this SYSTEM.MICRO's BOOT code is not the one native boot implements";
        return false;                                // memory is re-initialised by the Z80 boot below
    }

    }                                                // ---- end of the loader's work ----

    // ---- Stage 2: the interpreter's BOOT (0x1EDF) ----
    uint16_t sp = 0x3819;                            // LD SP,RELSEG+1000H
    sp = (uint16_t)(sp - 2); wr16(sp, 0x1EE5);       // CALL IOINIT (return address stays below SP)
    sp = (uint16_t)(sp + 2);
    const uint16_t memtop = reclaimLayout ? (uint16_t)0xFFF2 : (uint16_t)(rd16(0x0001) + 0xFFEF);
    wr16(PM_V(0x02F8), memtop);                            // IOINIT: MEMTOP
    const uint16_t sysunt = rd16(PM_V(0x02E8));
    NativeBootSysrd(sysunt, 0x2019, 0x0800, 2, 0x1EFC, sp);             // step 2: the directory
    uint16_t dentp = 0x2033;                          // skip entry 0 (the volume name)
    bool found = false;
    for (int c = 256; c > 0; c--) {
        wr16(0x1EDA, dentp);
        if (memcmp(&m_mem[(uint16_t)(dentp + 6)], &ram[0x1ECC], 14) == 0) { found = true; break; }   // SYSTLE
        dentp = (uint16_t)(dentp + 0x1A);
    }
    wr16(0x1EDA, dentp);
    if (!found) { why = "SYSTEM.PASCAL not found on the boot unit"; return false; }
    const uint16_t sysblk = rd16(dentp);
    wr16(0x1EDC, sysblk);                            // SYSBLK
    NativeBootSysrd(sysunt, 0x2819, 0x0040, sysblk, 0x1F49, sp);       // step 3: segment dictionary
    for (int i = 0; i < 16; i++) {                    // SEGTBL[i] := unit, absolute block, length
        const uint16_t e = (uint16_t)(PM_V(0x0344) + 6 * i), r = (uint16_t)(0x2819 + 4 * i);
        m_mem[e] = m_mem[PM_V(0x02E8)]; m_mem[(uint16_t)(e + 1)] = 0;
        wr16((uint16_t)(e + 2), (uint16_t)(rd16(r) + sysblk));
        wr16((uint16_t)(e + 4), rd16((uint16_t)(r + 2)));
        wr16((uint16_t)(sp - 2), (uint16_t)(r + 2)); // the loop's PUSH HL (^RELSEG[i].CODELEN)
    }
    m_mem[0x1EDE] = 0;                               // SEGCNT counted down to 0
    const uint16_t seg0len = rd16(PM_V(0x0348));
    sp = (uint16_t)(memtop + 2 - seg0len);           // step 4: segment 0 at the top
    NativeBootSysrd((uint16_t)m_mem[PM_V(0x02E8)], sp, seg0len, rd16(PM_V(0x0346)), 0x1FA1, sp);
    memset(m_mem + PM_V(0x0254), 0, 60);                   // INTSEGT[1..]: cleared
    wr16(PM_V(0x0250), 1); wr16(PM_V(0x0252), memtop);           // INTSEGT[0]: the operating system
    wr16(PM_V(0x02F6), memtop);                            // step 5: SEGP
    const uint16_t jtab = (uint16_t)(memtop - 2 - PM_CODE16((uint16_t)(memtop - 2)));
    wr16(PM_V(0x02F4), jtab);                              // JTAB (self-relative)
    const uint16_t ipc = (uint16_t)(jtab - 2 - PM_CODE16((uint16_t)(jtab - 2)));
    wr16(PM_V(0x0246), ipc);                               // IPCSAV (self-relative)
    const uint16_t datasz = PM_CODE16((uint16_t)(jtab - 8));
    sp = (uint16_t)(sp - datasz);                    // the outer block's data
    const uint16_t frameSp = sp;
    auto push = [&](uint16_t v) { sp = (uint16_t)(sp - 2); wr16(sp, v); };
    push(PM_V(0x02E4));                                    // ^SYSCOM parameter
    push(frameSp);                                   // MSCW (dummy save state)...
    push((uint16_t)(sp - 4));                        // ...MSIPC: address of an ABORT opcode
    push(0x00D6); push(0x00D6);                      // the ABORT opcode
    const uint16_t mp = (uint16_t)(sp - 4);
    push(mp); push(mp);                              // STAT and DYN: self-referencing
    wr16(PM_V(0x02F2), mp); wr16(PM_V(0x02F0), mp);              // MP, BASE
    wr16(PM_V(0x0242), (uint16_t)(mp + 10)); wr16(PM_V(0x0244), (uint16_t)(mp + 10)); // MPD0, BASED0
    const uint16_t npStart = reclaimLayout ? m_heapStart : (uint16_t)0x1EBA;
    wr16(PM_V(0x0240), npStart);                           // NP := INTEND (0x03A4 when reclaimed)

    // ---- Z80 registers as the BOOT code leaves them at BACK1 -> BACK ----
    Z80Regs& r = m_cpu.r;
    r.SP = sp;
    r.setBC(ipc);                                    // BACK1: GETIPC
    r.setDE(PM_V(0x02E4));
    r.setHL(npStart);
    const uint8_t dsLo = (uint8_t)datasz, dsHi = (uint8_t)(datasz >> 8);
    const int borrow = dsLo != 0 ? 1 : 0;            // XOR A; SUB C
    const int res = 0 - dsHi - borrow;               // LD A,0; SBC A,B
    r.A = (uint8_t)res;
    uint8_t f = 0x02;                                // N
    if (r.A & 0x80) f |= 0x80;
    if (r.A == 0) f |= 0x40;
    if ((0 - (dsHi & 0xF) - borrow) < 0) f |= 0x10;
    if (res < 0) f |= 0x01;
    if ((0 ^ dsHi) & (0 ^ r.A) & 0x80) f |= 0x04;
    f &= (0x80 | 0x40 | 0x20 | 0x08 | 0x04);         // ...then ADD HL,rr (last: ADD HL,BC = MP + 10)
    if (((mp & 0xFFF) + 0x00A) > 0xFFF) f |= 0x10;
    if ((uint32_t)mp + 0x00A > 0xFFFF) f |= 0x01;
    r.F = f;
    r.PC = 0x03B0;                                   // BACK: the first P-code instruction
    return true;
}

// The unit-I/O configuration (PM_IOCFG), worked out once: discovered from
// the Z80 interpreter's image, or -- P-Code mode without SYSTEM.MICRO --
// the built-in one.
const PmIoConfig& PSystemEngine::IoConfig() {
    if (!m_ioReady) {
        m_io = m_builtInTables ? BuiltInIoConfig()
                               : PmIoConfigFromImage([&](uint16_t a) { return (uint8_t)(m_rom.empty() ? m_mem[a] : m_rom[a]); });
        m_ioReady = true;
    }
    return m_io;
}

// ---- The PC's date and time (SetHostClock) ----
void PSystemEngine::HostClockStart() {
    const auto sysNow = std::chrono::system_clock::now();
    const std::time_t tt = std::chrono::system_clock::to_time_t(sysNow);
    std::tm lt{};
#ifdef _WIN32
    localtime_s(&lt, &tt);
#else
    localtime_r(&tt, &lt);
#endif
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(sysNow.time_since_epoch()).count() % 1000;
    m_clockTicks0 = (uint32_t)((lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec) * 60 + ms * 60 / 1000);
    m_clockT0 = std::chrono::steady_clock::now();
    // today's date into the boot volume's DLASTBOOT (directory entry 0, byte 20):
    // PACKED RECORD MONTH: 0..12 (bits 0-3); DAY: 0..31 (4-8); YEAR: 0..100 (9-15)
    std::vector<uint8_t>* disk = DriveImage(UnitValueToDrive(4));
    if (disk && disk->size() >= 3 * 512) {
        uint8_t* d = disk->data() + 1024;
        const uint16_t kind = (uint16_t)(d[4] | (d[5] << 8)), nameLen = d[6];
        if ((kind & 15) == 0 && nameLen >= 1 && nameLen <= 7) {        // a volume header
            const uint16_t date = (uint16_t)(((lt.tm_year % 100) << 9) | (lt.tm_mday << 4) | (lt.tm_mon + 1));
            d[20] = (uint8_t)date; d[21] = (uint8_t)(date >> 8);
        }
    }
}

void PSystemEngine::NativeClockUpdate() {
    if (!m_hostClock) return;
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_clockT0).count();
    const uint32_t t = m_clockTicks0 + (uint32_t)(ms * 60 / 1000);
    m_mem[PM_V(0x031A)] = (uint8_t)t;         m_mem[(uint16_t)(PM_V(0x031A) + 1)] = (uint8_t)(t >> 8);    // LOWTIME
    m_mem[PM_V(0x031C)] = (uint8_t)(t >> 16); m_mem[(uint16_t)(PM_V(0x031C) + 1)] = (uint8_t)(t >> 24);   // HIGHTIME
}

bool PSystemEngine::NativeDiskRead(uint16_t unit, uint16_t block, uint16_t len, std::vector<uint8_t>& out) {
    // Native segment reads (NativeSegLoad.inc), for any Big Disk unit,
    // exactly as the port-207 READ does: unit -> drive (UnitValueToDrive),
    // block * 512 in that drive's image. No image, or a read past its end,
    // returns false so the real Z80 READSEG/SYSRD (and its I/O error) runs.
    const std::vector<uint8_t>* img = DriveImage(UnitValueToDrive((uint8_t)unit));
    if (!img || img->empty()) return false;
    size_t off = (size_t)block * 512;
    if (off + len > img->size()) return false;
    out.assign(img->begin() + off, img->begin() + off + len);
    return true;
}

bool PSystemEngine::NativeBldmscw() {
    // Read proc_num from the code stream; the return IPC is the byte after it
    const uint16_t bc = m_cpu.r.BC();
    return NativeBldmscwProc(PM_CODE8(bc), (uint16_t)(bc + 1));
}

// BLDMSCW with the procedure number and the return IPC supplied, not read
// from the code stream: CXP/CIP/CLP/CGP/CBP pass the byte at BC and BC + 1;
// CSP 138 (CALLI, NativeCalli.inc) the procedure from the function value and
// the address after the CSP.
bool PSystemEngine::NativeBldmscwProc(uint8_t procNum, uint16_t retIpc) {
    // Save NEWSEG = current SEGP
    uint16_t segp = (uint16_t)(m_mem[PM_V(0x02F6)] | (m_mem[PM_V(0x02F7)] << 8));
    m_mem[PM_V(0x02CA)] = segp & 0xFF; m_mem[PM_V(0x02CB)] = segp >> 8; // NEWSEG

    // Advance IPC to the return address
    uint16_t bc = retIpc;
    m_cpu.r.setBC(bc);
    m_mem[PM_V(0x0246)] = bc & 0xFF; m_mem[PM_V(0x0247)] = bc >> 8; // SAVIPC

    // Look up jtab in the segment table.
    // Table entry for proc N is at SEGP - N*2 (negative indexing).
    // The stored value is a negative-self-relative 16-bit word:
    //   jtab_addr = table_entry_addr - stored_val  (uint16 arithmetic)
    uint16_t tAddr = (uint16_t)(segp - (uint16_t)(procNum * 2));
    uint16_t tSval = PM_CODE16(tAddr);
    uint16_t jtab  = (uint16_t)(tAddr - tSval);
    m_mem[PM_V(0x02CC)] = jtab & 0xFF; m_mem[PM_V(0x02CD)] = jtab >> 8; // NEWJTB

    // Assembly-language procedure? (first byte of jtab = 0 → fall through)
    if (PM_CODE8(jtab) == 0) return false;

    // Read datasz and parmsz from the jtab header (at jtab + DATASZ, where
    // DATASZ .EQU 0FFF8H = -8, so datasz is at jtab-8, parmsz at jtab-6).
    uint16_t dOff  = (uint16_t)(jtab - 8);
    uint16_t datasz = PM_CODE16(dOff);
    uint16_t parmsz = PM_CODE16((uint16_t)(dOff + 2));

    // Non-CXP path: extend the stack by datasz only (not datasz+parmsz).
    uint16_t oldSP = m_cpu.r.SP;
    m_cpu.r.SP = (uint16_t)(oldSP - datasz);

    // Copy parmsz bytes of parameters from the old stack top to the new
    // stack area (LDIR: source = oldSP, dest = new SP, count = parmsz).
    for (uint16_t i = 0; i < parmsz; i++)
        m_mem[(uint16_t)(m_cpu.r.SP + i)] = m_mem[(uint16_t)(oldSP + i)];

    // Push the six MSCW fields.  mssp = caller's SP before params were
    // pushed (= oldSP + parmsz since params have already been consumed).
    uint16_t mssp    = (uint16_t)(oldSP + parmsz);
    uint16_t ipcsav  = (uint16_t)(m_mem[PM_V(0x0246)] | (m_mem[PM_V(0x0247)] << 8));
    uint16_t curJtab = (uint16_t)(m_mem[PM_V(0x02F4)] | (m_mem[PM_V(0x02F5)] << 8));
    uint16_t curMP   = (uint16_t)(m_mem[PM_V(0x02F2)] | (m_mem[PM_V(0x02F3)] << 8));
    PushStackWord(mssp);    // [MP+10] mssp   (first push = highest address)
    PushStackWord(ipcsav);  // [MP+ 8] msipc
    PushStackWord(segp);    // [MP+ 6] msseg  (= NEWSEG = caller's segment)
    PushStackWord(curJtab); // [MP+ 4] msjtab
    PushStackWord(curMP);   // [MP+ 2] msdyn
    PushStackWord(curMP);   // [MP+ 0] msstat (= msdyn for CLP; CBP overwrites)

    // STKCHK (0x1260): carry set (= overflow) if (SP - 60) < NP.
    uint16_t np = (uint16_t)(m_mem[PM_V(0x0240)] | (m_mem[PM_V(0x0241)] << 8));
    if ((uint16_t)(m_cpu.r.SP - 60) < np) {
        m_cpu.r.PC = 0x03E9; // STKOVR
        return false;
    }

    // Set up the new P-machine frame environment
    uint16_t newMP = m_cpu.r.SP;
    m_mem[PM_V(0x02F2)] = newMP & 0xFF; m_mem[PM_V(0x02F3)] = newMP >> 8;               // MP
    uint16_t newMPD0 = (uint16_t)(newMP + 10);
    m_mem[PM_V(0x0242)] = newMPD0 & 0xFF; m_mem[PM_V(0x0243)] = newMPD0 >> 8;           // MPD0
    m_mem[PM_V(0x02F6)] = segp & 0xFF;   m_mem[PM_V(0x02F7)] = segp >> 8;               // SEGP = NEWSEG
    m_mem[PM_V(0x02F4)] = jtab & 0xFF;   m_mem[PM_V(0x02F5)] = jtab >> 8;               // JTAB = NEWJTB

    // Compute the procedure's entry address (2 bytes before jtab, same
    // negative-self-relative encoding): entry_addr = (jtab-2) - stored_val
    uint16_t eRef  = (uint16_t)(jtab - 2);
    uint16_t eSval = PM_CODE16(eRef);
    uint16_t entry = (uint16_t)(eRef - eSval);
    m_cpu.r.setBC(entry); // BC = new IPC = entry point of called procedure

    // Register fidelity: A and DE from BLDMSCW's own entry-address
    // calculation (LD A,H; SBC A,D at 0x1331 is the last A-write;
    // DE = eSval because D and E were loaded from jtab-2 at 0x132A/0x132C).
    uint8_t eH = eRef >> 8, eD = eSval >> 8;
    uint8_t eL = eRef & 0xFF, eE = eSval & 0xFF;
    uint8_t carry = (eL < eE) ? 1 : 0;
    m_cpu.r.A = (uint8_t)(eH - eD - carry); // LD A,H; SBC A,D result
    m_cpu.r.setDE(eSval);                    // D=mem[jtab-1], E=mem[jtab-2]
    return true;
}

static const char* UcsdKindName(int kind) {
    switch (kind & 0xF) {
        case 2: return "CODE";
        case 3: return "TEXT";
        case 4: return "INFO";
        case 5: return "DATA";
        case 6: return "GRAF";
        case 7: return "FOTO";
        default: return "    ";
    }
}

std::vector<PSystemEngine::UcsdDirEntry> PSystemEngine::GetVolumeDirectory(int unit) const {
    std::vector<UcsdDirEntry> result;
    int drive = UnitToDriveIndex(unit);
    if (drive < 0) return result;
    const std::vector<uint8_t>* img = nullptr;
    if (drive == 0) img = &m_volDrive0;
    else if (drive == 1) img = &m_volDrive1;
    else if (drive == 2) img = &m_volDrive2;
    else if (drive == 3) img = &m_volDrive3;
    if (!img || img->size() < (size_t)(UCSD_DIR_BYTE_OFFSET + UCSD_ENTRY_SIZE * 2))
        return result;

    auto rd16 = [&](int off) -> uint16_t {
        return (uint16_t)((*img)[off] | ((*img)[off + 1] << 8));
    };

    uint16_t numFiles = rd16(UCSD_DIR_BYTE_OFFSET + 16);
    if (numFiles > UCSD_MAX_ENTRIES) numFiles = UCSD_MAX_ENTRIES;

    for (int i = 0; i < numFiles; i++) {
        int off = UCSD_DIR_BYTE_OFFSET + UCSD_ENTRY_SIZE + i * UCSD_ENTRY_SIZE;
        if (off + UCSD_ENTRY_SIZE > (int)img->size()) break;

        UcsdDirEntry e;
        e.firstBlock = rd16(off + 0);
        e.lastBlock  = rd16(off + 2);
        e.kind       = rd16(off + 4) & 0xF;
        int nlen = (*img)[off + 6];
        if (nlen < 0 || nlen > 15) continue; // corrupt entry
        for (int j = 0; j < nlen; j++)
            e.name += (wchar_t)(*img)[off + 7 + j];
        e.lastByte = rd16(off + 22);
        if (e.lastByte == 0 || e.lastByte > 512) e.lastByte = 512;
        if (!e.name.empty() && e.lastBlock > e.firstBlock)
            result.push_back(e);
    }
    return result;
}

std::wstring PSystemEngine::ExportFileFromVolume(int unit, const std::wstring& ucsdName,
                                                  const std::wstring& windowsDestPath) const {
    // Find the entry
    auto entries = GetVolumeDirectory(unit);
    const UcsdDirEntry* found = nullptr;
    for (auto& e : entries)
        if (e.name == ucsdName) { found = &e; break; }
    if (!found)
        return L"File not found in volume directory: " + ucsdName;

    int drive = UnitToDriveIndex(unit);
    const std::vector<uint8_t>* img = nullptr;
    if (drive == 0) img = &m_volDrive0;
    else if (drive == 1) img = &m_volDrive1;
    else if (drive == 2) img = &m_volDrive2;
    else if (drive == 3) img = &m_volDrive3;
    if (!img) return L"Internal error: drive image unavailable.";

    long byteOffset = (long)found->firstBlock * 512L;
    long byteCount  = found->ByteCount();
    if (byteCount < 0) byteCount = 0;

    if (byteOffset < 0 || byteOffset + byteCount > (long)img->size())
        return L"File data is outside the disk image bounds -- the image may be truncated.";

    // ---- Build the output buffer -----------------------------------
    // Space-run (DLE) decompression, NUL suppression, and bare-CR to
    // CRLF expansion apply only to .TEXT and .LIST files, matched by
    // filename suffix rather than the directory's own kind byte --
    // .LIST files (e.g. those produced by redirecting assembler/
    // compiler listing output to a named file) are stored as kind=5
    // (DATAFILE), the same kind as genuinely binary data files, so kind
    // alone can't distinguish them. Every other file is copied through
    // unmodified: those bytes are meaningful binary content, not UCSD's
    // text-run encoding, and decoding them the same way would corrupt
    // the file.
    auto endsWithCI = [](const std::wstring& s, const wchar_t* suffix) {
        size_t slen = wcslen(suffix);
        if (s.size() < slen) return false;
        return _wcsicmp(s.c_str() + (s.size() - slen), suffix) == 0;
    };
    bool isTextLike = endsWithCI(found->name, L".TEXT") || endsWithCI(found->name, L".LIST");

    std::vector<uint8_t> out;
    if (isTextLike) {
        out.reserve((size_t)byteCount);
        bool pendingDle = false;
        // A TEXT file (kind 3) begins with a 2-block header page -- the
        // editor's stored settings, not text -- and its text starts at byte
        // 1024. Exporting the header too put a line of binary bytes at the
        // top of the Windows file (a compile error if imported back).
        const long textStart = (found->kind == 3 && byteCount >= 1024) ? 1024 : 0;
        for (long i = textStart; i < byteCount; i++) {
            uint8_t c = (*img)[byteOffset + i];
            if (pendingDle) {
                uint8_t spaces = (uint8_t)(c - 0x20);
                out.insert(out.end(), spaces, (uint8_t)0x20);
                pendingDle = false;
                continue;
            }
            if (c == 0x10) { pendingDle = true; continue; } // swallow the marker; next byte is the count
            if (c == 0x00) continue; // suppress NUL, matching PrinterWriteChar's own filtering
            out.push_back(c);
            if (c == 0x0D) out.push_back(0x0A); // UCSD text stores bare CR; Windows needs CRLF
        }
    } else if (byteCount > 0) {
        out.assign(img->begin() + byteOffset, img->begin() + byteOffset + byteCount);
    }

    FILE* f = nullptr;
    _wfopen_s(&f, windowsDestPath.c_str(), L"wb");
    if (!f) return L"Could not create destination file:\r\n" + windowsDestPath;

    bool ok = true;
    if (!out.empty())
        ok = (fwrite(out.data(), 1, out.size(), f) == out.size());
    fclose(f);

    if (!ok) {
        ::DeleteFileW(windowsDestPath.c_str()); // remove partial output
        return L"Write error while exporting to:\r\n" + windowsDestPath;
    }
    return L""; // success
}

std::wstring PSystemEngine::ImportFileToVolume(int unit, const std::wstring& windowsFilePath) {
    int drive = UnitToDriveIndex(unit);
    if (drive < 0) return L"Invalid unit number (use 4, 5, 9, or 10).";

    std::vector<uint8_t>* img = DriveImage(drive);
    if (!img || img->empty())
        return L"No disk image is loaded for that unit. Use File menu to open one first.";
    if (img->size() < (size_t)(UCSD_DIR_BYTE_OFFSET + UCSD_ENTRY_SIZE))
        return L"Disk image is too small to contain a valid UCSD directory.";

    // ---- Load the Windows source file ----
    FILE* wf = nullptr;
    _wfopen_s(&wf, windowsFilePath.c_str(), L"rb");
    if (!wf) return L"Could not open source file:\r\n" + windowsFilePath;
    fseek(wf, 0, SEEK_END);
    long rawSize = ftell(wf);
    fseek(wf, 0, SEEK_SET);
    if (rawSize < 0 || rawSize > 16L * 1024 * 1024) {
        fclose(wf);
        return L"File is too large (limit: 16 MB).";
    }
    std::vector<uint8_t> fileData((size_t)rawSize);
    if (rawSize > 0) fread(fileData.data(), 1, (size_t)rawSize, wf);
    fclose(wf);

    // ---- Parse the directory ----
    auto rd16 = [&](int off) -> uint16_t {
        return (uint16_t)((*img)[off] | ((*img)[off + 1] << 8));
    };
    auto wr16 = [&](int off, uint16_t v) {
        (*img)[off]     = (uint8_t)(v & 0xFF);
        (*img)[off + 1] = (uint8_t)(v >> 8);
    };

    uint16_t eovblk   = rd16(UCSD_DIR_BYTE_OFFSET + 14);
    uint16_t numFiles = rd16(UCSD_DIR_BYTE_OFFSET + 16);
    uint16_t dirEndBlk= rd16(UCSD_DIR_BYTE_OFFSET + 2); // first data block (typically 6)

    if (eovblk == 0 || eovblk > (uint16_t)(img->size() / 512))
        return L"Directory appears corrupt (invalid end-of-volume block count).";
    if (numFiles >= UCSD_MAX_ENTRIES)
        return L"Directory is full -- the volume already has the maximum of 77 files.";

    // Find the first free block = highest DLASTBLK among all file entries.
    uint16_t firstFreeBlk = dirEndBlk;
    for (int i = 0; i < numFiles; i++) {
        int off = UCSD_DIR_BYTE_OFFSET + UCSD_ENTRY_SIZE + i * UCSD_ENTRY_SIZE;
        uint16_t dlast = rd16(off + 2);
        if (dlast > firstFreeBlk) firstFreeBlk = dlast;
    }

    // How many 512-byte blocks does the file occupy?
    uint16_t blocksNeeded = (uint16_t)((rawSize + 511) / 512);
    if (blocksNeeded == 0) blocksNeeded = 1; // even empty files get one block
    uint16_t newLastBlk = (uint16_t)(firstFreeBlk + blocksNeeded);

    if (newLastBlk > eovblk)
        return L"Not enough free space on that volume.\r\n"
               L"Need " + std::to_wstring(blocksNeeded) +
               L" blocks; only " + std::to_wstring(eovblk - firstFreeBlk) + L" are free.";

    // ---- Build the UCSD filename from the Windows path ----
    // Strip drive/directory; keep base name only, uppercase, max 15 chars.
    // UCSD filenames: [A-Z][A-Z0-9._]* up to 15 characters total.
    std::wstring baseName;
    size_t slashPos = windowsFilePath.find_last_of(L"\\/");
    baseName = (slashPos == std::wstring::npos) ? windowsFilePath
                                                  : windowsFilePath.substr(slashPos + 1);
    // Convert to uppercase
    for (wchar_t& c : baseName)
        c = (wchar_t)towupper(c);
    // Keep only valid UCSD filename characters
    std::wstring ucsdName;
    for (wchar_t c : baseName) {
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') ||
                c == L'.' || c == L'_') {
            ucsdName += c;
            if (ucsdName.size() == 15) break;
        }
    }
    if (ucsdName.empty()) return L"Could not derive a valid UCSD filename from:\r\n" + baseName;

    // Check for duplicate filename
    for (int i = 0; i < numFiles; i++) {
        int off = UCSD_DIR_BYTE_OFFSET + UCSD_ENTRY_SIZE + i * UCSD_ENTRY_SIZE;
        int nlen = (*img)[off + 6];
        if (nlen < 0 || nlen > 15) continue;
        std::wstring existing;
        for (int j = 0; j < nlen; j++)
            existing += (wchar_t)(*img)[off + 7 + j];
        if (existing == ucsdName)
            return L"A file named \"" + ucsdName + L"\" already exists on that volume.";
    }

    // Determine file kind from the extension (the part after the last dot).
    uint16_t fileKind = 5; // default: DATA
    size_t dotPos = ucsdName.rfind(L'.');
    if (dotPos != std::wstring::npos) {
        std::wstring ext = ucsdName.substr(dotPos + 1);
        if      (ext == L"TEXT" || ext == L"TXT"  || ext == L"BACK" ||  ext == L"C" || ext == L"H"  ) fileKind = 3;
        else if (ext == L"CODE"                                      ) fileKind = 2;
        else if (ext == L"INFO"                                      ) fileKind = 4;
        else if (ext == L"GRAF"                                      ) fileKind = 6;
        else if (ext == L"FOTO"                                      ) fileKind = 7;
        else                                                           fileKind = 5; // DATA
    }

    // ---- Text files: convert to UCSD .TEXT format ----
    // A raw byte copy is only right for code/data files. A .TEXT file needs
    // the 1K editor header, CR-only line ends, DLE-coded indentation and
    // whole lines packed into NUL-padded 1K pages (see UcsdText.h); without
    // them the editor misreads the file and can hang.
    if (fileKind == 3) {
        fileData = ConvertToUcsdText(fileData);
        rawSize = (long)fileData.size();
        blocksNeeded = (uint16_t)((rawSize + 511) / 512);
        newLastBlk = (uint16_t)(firstFreeBlk + blocksNeeded);
        if (newLastBlk > eovblk)
            return L"Not enough free space on that volume.\r\n"
                   L"Need " + std::to_wstring(blocksNeeded) +
                   L" blocks; only " + std::to_wstring(eovblk - firstFreeBlk) + L" are free.";
    }

    // ---- Write file data into the volume image ----
    long byteOffset = (long)firstFreeBlk * 512L;
    long neededBytes = (long)blocksNeeded * 512L;
    // Grow the in-memory image if the volume is larger than currently loaded
    if (byteOffset + neededBytes > (long)img->size())
        img->resize((size_t)(byteOffset + neededBytes), 0);
    // Zero-fill the whole allocation (pads the last partial block with zeros)
    std::fill(img->begin() + byteOffset, img->begin() + byteOffset + neededBytes, 0u);
    if (!fileData.empty())
        std::copy(fileData.begin(), fileData.end(), img->begin() + byteOffset);

    // ---- Add the directory entry at the end of the current entries ----
    int entryOff = UCSD_DIR_BYTE_OFFSET + UCSD_ENTRY_SIZE + numFiles * UCSD_ENTRY_SIZE;
    std::fill(img->begin() + entryOff, img->begin() + entryOff + UCSD_ENTRY_SIZE, 0u);
    wr16(entryOff + 0, firstFreeBlk);          // DFIRSTBLK
    wr16(entryOff + 2, newLastBlk);            // DLASTBLK
    wr16(entryOff + 4, fileKind);              // DFIBKIND
    (*img)[entryOff + 6] = (uint8_t)ucsdName.size(); // filename length byte
    for (int i = 0; i < (int)ucsdName.size(); i++)
        (*img)[entryOff + 7 + i] = (uint8_t)ucsdName[i];
    // DLASTBYTE: bytes used in the last block (512 = full)
    uint16_t lastByte = (rawSize == 0) ? (uint16_t)0
                       : (rawSize % 512 == 0) ? (uint16_t)512
                       : (uint16_t)(rawSize % 512);
    wr16(entryOff + 22, lastByte);
    wr16(entryOff + 24, 0); // date = 0 (no clock on this system)

    // ---- Update DNUMFILES in the volume header ----
    wr16(UCSD_DIR_BYTE_OFFSET + 16, (uint16_t)(numFiles + 1));

    // ---- Flush both the directory blocks and the new file data to disk ----
    FlushDriveRegion(drive, UCSD_DIR_BYTE_OFFSET, 2048); // blocks 2-5
    FlushDriveRegion(drive, byteOffset, (int)neededBytes);

    return L""; // empty string = success
}

std::vector<uint8_t>* PSystemEngine::FdcImage() {
    if (m_fdc.drive == 0) return &m_floppy0;
    if (m_fdc.drive == 1) return &m_floppy1;
    return nullptr;
}

uint8_t PSystemEngine::PortIn(uint8_t port) {
    switch (port) {
        case 0: { // CONST: non-blocking "is a key waiting?"
            EnterCriticalSection(&m_keyLock);
            bool has = !m_keyQueue.empty();
            LeaveCriticalSection(&m_keyLock);
            return has ? 0xFF : 0x00;
        }
        case 1: { // CONIN: block for a real keystroke.
            for (;;) {
                EnterCriticalSection(&m_keyLock);
                if (!m_keyQueue.empty()) {
                    uint8_t v = m_keyQueue.front();
                    m_keyQueue.erase(m_keyQueue.begin());
                    bool stillHas = !m_keyQueue.empty();
                    if (!stillHas) ResetEvent(m_keyAvailable);
                    LeaveCriticalSection(&m_keyLock);
                    if (m_tracing && m_traceFile)
                        fprintf(m_traceFile, "[CONSOLE @instr %ld] '%c' (0x%02X)\n",
                                m_traceCounter, (v >= 32 && v < 127) ? v : '.', v);
                    return v;
                }
                LeaveCriticalSection(&m_keyLock);
                // The stop event is listed FIRST: WaitForMultipleObjects reports
                // the lowest-index signaled handle, and Stop() sets both events
                // (both manual-reset). With m_keyAvailable first, a stop while
                // waiting here kept returning "key available", found the queue
                // empty and waited again -- forever.
                HANDLE handles[2] = { m_stopEvent, m_keyAvailable };
                m_waitingForKey = true;
                DWORD r = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                m_waitingForKey = false;
                if (r == WAIT_OBJECT_0) return 0x00; // stopping
                // else loop back and consume the key we were signaled for
            }
        }
        case 14: { // FDCST
            auto* img = FdcImage();
            if (!img || img->empty()) return 0x01;
            if (m_fdc.sector < 1 || m_fdc.sector > 26 || m_fdc.track > 76) return 0x01;
            long off = (long)m_fdc.track * 26 * 128 + (long)(m_fdc.sector - 1) * 128;
            if ((size_t)(off + 128) > img->size()) return 0x01;
            for (int i = 0; i < 128; i++) m_mem[(uint16_t)(m_bd.dma + i)] = (*img)[off + i];
            return 0x00;
        }
        case 207: {
            auto* img = DriveImage(m_bd.drive);
            // As MunkDisk.c: no drive -> status 1; only READ (1) and WRITE (2)
            // transfer data; CLEAR (4) or anything else -> status 0, nothing moved.
            if (!img || img->empty()) return 0x01;   // no image mounted on that unit
            if (m_bd.cmd != 1 && m_bd.cmd != 2) return 0x00;
            long fileOff = (long)m_bd.blk * 512;
            int n = m_bd.len;
            if (img && fileOff >= 0 && (size_t)(fileOff + n) <= img->size()) {
                if (m_bd.cmd == 2) {
                    for (int i = 0; i < n; i++) (*img)[fileOff + i] = m_mem[(uint16_t)(m_bd.dma + i)];
                    FlushDriveRegion(m_bd.drive, fileOff, n);
                } else {
                    for (int i = 0; i < n; i++) m_mem[(uint16_t)(m_bd.dma + i)] = (*img)[fileOff + i];
                }
                return 0x00;
            }
            return 0x01;
        }
        default: return 0xFF;
    }
}

void PSystemEngine::PortOut(uint8_t port, uint8_t v) {
    switch (port) {
        case 1:
            PutChar(v);
            if (m_tracing && m_traceFile)
                fprintf(m_traceFile, "[CONSOLE @instr %ld] '%c' (0x%02X)\n",
                        m_traceCounter, (v >= 32 && v < 127) ? v : '.', v);
            break;
        case 199: break; // program-requested abort: ignored in the GUI build
        case 10: m_fdc.drive = v; break;
        case 11: m_fdc.track = v; break;
        case 12: m_fdc.sector = v; break;
        case 13: break;
        case 200: m_bd.drive = UnitValueToDrive(v); break; // -1 = no such drive: status 1 (MunkDisk.c), never drive 0
        case 203: m_bd.len_hi = v; m_bd.len = (m_bd.len_hi << 8) | (m_bd.len & 0xFF); break;
        case 204: m_bd.len = (m_bd.len & 0xFF00) | v; break;
        case 205: m_bd.blk_hi = v; m_bd.blk = (m_bd.blk_hi << 8) | (m_bd.blk & 0xFF); break;
        case 206: m_bd.blk = (m_bd.blk & 0xFF00) | v; break;
        case 15: m_bd.dma = (m_bd.dma & 0xFF00) | v; break;
        case 16: m_bd.dma_hi = v; m_bd.dma = (m_bd.dma_hi << 8) | (m_bd.dma & 0xFF); break;
        case 207: m_bd.cmd = v; break;
        default: break;
    }
}

uint16_t PSystemEngine::PopStackWord() {
    uint8_t lo = m_mem[m_cpu.r.SP];
    uint8_t hi = m_mem[(uint16_t)(m_cpu.r.SP + 1)];
    m_cpu.r.SP += 2;
    return (uint16_t)((hi << 8) | lo);
}

void PSystemEngine::PushStackWord(uint16_t v) {
    m_cpu.r.SP -= 2;
    m_mem[m_cpu.r.SP] = v & 0xFF;
    m_mem[(uint16_t)(m_cpu.r.SP + 1)] = v >> 8;
    if (m_verifyFile && m_cpu.r.SP < m_verifyMinSP) m_verifyMinSP = m_cpu.r.SP;
}

uint16_t PSystemEngine::PeekStackWord(int byteOffsetFromSP) const {
    uint16_t addr = (uint16_t)(m_cpu.r.SP + byteOffsetFromSP);
    uint8_t lo = m_mem[addr];
    uint8_t hi = m_mem[(uint16_t)(addr + 1)];
    return (uint16_t)((hi << 8) | lo);
}

uint16_t PSystemEngine::DecodeGBDE() {
    uint16_t bc = m_cpu.r.BC();
    uint8_t a = PM_CODE8(bc);
    bc = (uint16_t)(bc + 1);
    if ((a & 0x80) == 0) {
        m_cpu.r.setBC(bc);
        m_cpu.r.A = a; // real GBDE leaves A = the single operand byte (bit7 already 0)
        return a;
    }
    uint8_t hi = a & 0x7F;
    uint8_t lo = PM_CODE8(bc);
    bc = (uint16_t)(bc + 1);
    m_cpu.r.setBC(bc);
    m_cpu.r.A = lo; // real GBDE re-loads A from the SECOND operand byte (the low
                     // byte) right before RET -- "LD D,A" stores the masked high
                     // byte into D first, but A itself gets overwritten again
                     // immediately after, so A does NOT end up holding it
    return (uint16_t)((hi << 8) | lo);
}

uint16_t PSystemEngine::GetIA() {
    uint8_t lexLevels = PM_CODE8(m_cpu.r.BC());
    m_cpu.r.setBC((uint16_t)(m_cpu.r.BC() + 1));
    uint16_t hl = (uint16_t)(m_mem[PM_V(0x02F2)] | (m_mem[PM_V(0x02F3)] << 8)); // HL = MP
    for (uint8_t i = 0; i < lexLevels; i++) {
        uint16_t de = (uint16_t)(m_mem[hl] | (m_mem[(uint16_t)(hl + 1)] << 8)); // chase static link
        hl = de; // EX DE,HL
    }
    uint16_t offset = DecodeGBDE(); // also sets A, per GBDE's own leftover contract
    hl = (uint16_t)(hl + offset * 2 + 10); // +DISP0 (0x000A, a fixed constant, not a memory read)
    m_cpu.r.setDE(0x000A); // real code's last step is "LD DE,DISP0" -- DE is
                            // never touched again before RET, so it ends up
                            // holding this literal constant, not the offset
    return hl;
}

void PSystemEngine::NativeSrs(uint16_t i, uint16_t j) {
    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC

    if (j < i) {
        // $90: empty set (a single size-0 word, no data words)
        if (m_preserveZ80RegisterCompat) m_cpu.r.A = (uint8_t)(((uint16_t)(j - i)) >> 8); // high byte of the 16-bit
                                                           // subtraction that detected j<i
        if (m_preserveZ80RegisterCompat) m_cpu.r.setDE(j); // unchanged since the very first "POP DE" at entry
        PushStackWord(0);
        if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(0);
        m_cpu.r.PC = 0x03A4; // BACK1
        return;
    }

    uint8_t iDiv16 = (uint8_t)(i / 16), iMod16 = (uint8_t)(i % 16);
    uint8_t jDiv16 = (uint8_t)(j / 16), jMod16 = (uint8_t)(j % 16);
    uint16_t bitterJ = (uint16_t)((1u << (jMod16 + 1)) - 1);
    uint16_t unbitrI = (uint16_t)(0xFFFFu << iMod16);

    for (int w = jDiv16; w >= 0; w--) {
        uint16_t wordVal;
        if (w == jDiv16 && w == iDiv16) wordVal = (uint16_t)(bitterJ & unbitrI);
        else if (w == jDiv16) wordVal = bitterJ;
        else if (w == iDiv16) wordVal = unbitrI;
        else if (w > iDiv16 && w < jDiv16) wordVal = 0xFFFF;
        else wordVal = 0x0000;
        PushStackWord(wordVal);
    }
    uint16_t setSize = (uint16_t)(jDiv16 + 1);
    PushStackWord(setSize);

    if (m_preserveZ80RegisterCompat) m_cpu.r.A = (uint8_t)(setSize & 0xFF);
    if (m_preserveZ80RegisterCompat) m_cpu.r.setDE(0x0000);
    if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(setSize);
    m_cpu.r.PC = 0x03A4; // BACK1
}

bool PSystemEngine::CmpSetupOrdering(bool& outCarry, bool& outZero) {
    uint8_t typeCode = PM_CODE8(m_cpu.r.BC()); // peek only until a type is confirmed handled
    if (typeCode == 6) { // BOOLC
        m_cpu.r.setBC((uint16_t)(m_cpu.r.BC() + 1));
        m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC
        uint16_t b = PopStackWord();
        uint16_t a = PopStackWord();
        uint8_t aBit = (uint8_t)(a & 1), bBit = (uint8_t)(b & 1);
        outCarry = (aBit < bBit);
        outZero  = (aBit == bBit);
        m_cpu.r.A = aBit;
        m_cpu.r.setDE((uint16_t)((b & 0xFF00) | bBit));
        return true;
    } else if (typeCode == 4) { // STRGC: lexicographic compare, up to
                                  // min(lenA,lenB) characters; if still
                                  // equal, compare lengths (a shorter
                                  // string that's a prefix of a longer one
                                  // is "less than" it).
        m_cpu.r.setBC((uint16_t)(m_cpu.r.BC() + 1));
        m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC
        uint16_t bPtr = PopStackWord();
        uint16_t aPtr = PopStackWord();
        bool aDisguised = (aPtr >> 8) == 0;
        bool bDisguised = (bPtr >> 8) == 0;
        uint8_t aLen = aDisguised ? 1 : m_mem[aPtr];
        uint8_t bLen = bDisguised ? 1 : m_mem[bPtr];
        uint16_t aData = (uint16_t)(aPtr + 1);
        uint16_t bData = (uint16_t)(bPtr + 1);
        uint16_t aBasePtr = aDisguised ? PM_V(0x024E) : aPtr;
        uint8_t minLen = (aLen < bLen) ? aLen : bLen;
        bool foundMismatch = false;
        uint16_t deFinal = aBasePtr;
        uint8_t aRegFinal = 0;
        int cmpResult = 0;
        for (uint8_t idx = 0; idx < minLen; idx++) {
            uint8_t ac = aDisguised ? (aPtr & 0xFF) : m_mem[(uint16_t)(aData + idx)];
            uint8_t bc = bDisguised ? (bPtr & 0xFF) : m_mem[(uint16_t)(bData + idx)];
            deFinal = (uint16_t)(aBasePtr + idx + 1);
            if (ac != bc) { foundMismatch = true; aRegFinal = ac; cmpResult = (int)ac - (int)bc; break; }
        }
        if (!foundMismatch) {
            cmpResult = (int)aLen - (int)bLen;
            aRegFinal = aLen; // "LD A,(LENA)" reloads lenA fresh
        }
        outCarry = (cmpResult < 0);
        outZero  = (cmpResult == 0);
        m_cpu.r.A = aRegFinal;
        m_cpu.r.setDE(deFinal);
        return true;
    }
    if ((typeCode == 10 || typeCode == 12) && !m_preserveZ80RegisterCompat) {
        // BYTEC / WORDC (e.g. < on PACKED ARRAY OF CHAR), register
        // compatibility off: GBDE size, SAVIPC past it, then SWEQ -- scan
        // while equal (CPI; a count of 0 wraps round to 65536) and take the
        // flags from the last pair compared (DEC HL; CP (HL)).
        m_cpu.r.setBC((uint16_t)(m_cpu.r.BC() + 1));
        uint16_t count = DecodeGBDE();
        m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC
        uint32_t n = (typeCode == 12) ? (uint32_t)(uint16_t)(count * 2) : count;
        if (n == 0) n = 0x10000;
        const uint16_t bPtr = PopStackWord();
        const uint16_t aPtr = PopStackWord();
        uint8_t ac = 0, bc = 0;
        for (uint32_t k = 0; k < n; k++) {
            ac = m_mem[(uint16_t)(aPtr + k)]; bc = m_mem[(uint16_t)(bPtr + k)];
            if (ac != bc) break;
        }
        outCarry = ac < bc;
        outZero  = ac == bc;
        return true;
    }
    return false; // REALC/POWRC (NativeRealc/NativePowrc.inc), or BYTEC/WORDC with compatibility on: Z80
}

void PSystemEngine::ConsoleEcho(uint8_t c, bool suppressDle, bool suppressCrLf) {
    const uint16_t CLAST = PM_V(0x02E1);
    if (!suppressDle && m_mem[CLAST] == 0x10) {
        // Continuing a DLE blank-run: c is the run-length byte (count =
        // c - 0x20). Emit that many spaces, then return -- the run's
        // own bytes are never themselves written to the device.
        uint8_t count = (uint8_t)(c - 0x20);
        m_mem[CLAST] = count;
        for (;;) {
            uint8_t next = (uint8_t)(count - 1);
            if (next & 0x80) break; // "negative" as a signed byte -- run exhausted
            count = next;
            m_mem[CLAST] = count;
            PortOut(1, 0x20);
        }
        return;
    }
    m_mem[CLAST] = c;
    if (c == 0x10) {
        if (!suppressDle) return; // swallow the DLE marker; the next byte starts a run
        m_mem[CLAST] = 0x20;       // bookkeeping only -- c itself is still written below
    }
    PortOut(1, c);
    if (suppressCrLf) return;
    if (m_mem[CLAST] == 0x0D) {
        m_mem[CLAST] = 0x0A;
        PortOut(1, 0x0A);
    }
}

// ---- Punch: device (unit #8) logger --------------------------------
// Every character written to unit 8 (the PUNCH BIOS function, reached
// as REMOUT: from Pascal -- "PUNCH" is the BIOS routine's own internal
// name, not the volume name a Pascal program uses; see
// System_Micro_LST.txt's Figure 1) gets mirrored here instead of being
// silently discarded, into C:\Tmp\Punch.out (opened in append mode, so
// repeated runs accumulate rather than overwrite).
//
// Format: a marker line first, then the file's data as Intel-HEX
// records (32 data bytes per record, address starting at 0x0000 and
// counting up by the record's own length), each followed on the SAME
// line by that record's printable-character rendering in groups of 8
// (space-separated), then a CRLF. ':' never appears in the printable
// rendering -- it's reserved as the hex record's own start marker --
// so it displays as '.', same as any other non-printable byte.
//
// The incoming character stream is buffered a line at a time (up to a
// CR or LF) specifically so the end-of-transfer marker line can be
// recognized as a whole and handled specially (written verbatim, NOT
// hex-encoded) even though the file's own data may itself contain CR/LF
// bytes as ordinary line breaks -- each such line is simply checked
// against the marker, fails to match, and is fed through as ordinary
// data instead.
static const char* const PUNCH_END_MARKER = "<----<<<  <----<<<  <----<<<  <----<<< ";

void PSystemEngine::PunchWriteChar(uint8_t c) {
    if (!m_punchFileOpen) {
        // Create C:\Tmp directory -- this is a no-op if it already exists,
        // and the only way the file open below could silently fail in a
        // standard Windows installation (C:\ is always writable).
        CreateDirectoryW(L"C:\\Tmp", nullptr);
        m_punchFile.open("C:\\Tmp\\Punch.out", std::ios::app | std::ios::binary);
        m_punchFileOpen = true; // don't retry every character even if this failed
        if (m_punchFile.is_open()) {
            m_punchFile << ">>>---->   >>>---->   >>>---->   >>>---->\r\n";
            m_punchFile.flush();
        }
        m_punchHexAddress = 0;
        m_punchHexBuffer.clear();
        m_punchLineBuffer.clear();
        m_punchLastWasCR = false;
    }
    if (!m_punchFile.is_open()) return; // couldn't open -- drop silently

    if (c == '\n' && m_punchLastWasCR) {
        // second half of a CRLF pair -- the CR already triggered the
        // line-end check below; don't double-trigger on this LF or a
        // stray character would spuriously reopen the file right after
        // correctly closing it on the end-of-transfer marker.
        m_punchLastWasCR = false;
        return;
    }
    m_punchLastWasCR = (c == '\r');

    if (c == '\r' || c == '\n') {
        if (m_punchLineBuffer == PUNCH_END_MARKER) {
            PunchFlushHexBuffer(); // flush any partial (< 32 byte) trailing record first
            m_punchFile << m_punchLineBuffer << "\r\n";
            m_punchFile.flush();
            m_punchFile.close();
            m_punchFileOpen = false;
            m_punchLineBuffer.clear();
            m_punchHexBuffer.clear();
            return;
        }
        for (char lc : m_punchLineBuffer) PunchAddDataByte((uint8_t)lc);
        PunchAddDataByte(c); // the line's own terminator is real file data too
        m_punchLineBuffer.clear();
        return;
    }
    m_punchLineBuffer += (char)c;

    // Also check immediately after each character is appended, not only on
    // CR/LF -- a Pascal WRITE (without WRITELN) never sends a CR, so the
    // CR/LF-triggered check above would never fire if that's how the
    // Pascal program sends the end-of-transfer marker.
    if (m_punchLineBuffer == PUNCH_END_MARKER) {
        PunchFlushHexBuffer();
        m_punchFile << m_punchLineBuffer << "\r\n";
        m_punchFile.flush();
        m_punchFile.close();
        m_punchFileOpen = false;
        m_punchLineBuffer.clear();
        m_punchHexBuffer.clear();
        return;
    }

    if (m_punchLineBuffer.size() > 500) { // safety cap: can't possibly still be matching the (short) marker
        for (char lc : m_punchLineBuffer) PunchAddDataByte((uint8_t)lc);
        m_punchLineBuffer.clear();
    }
}

void PSystemEngine::PunchAddDataByte(uint8_t b) {
    m_punchHexBuffer.push_back(b);
    if (m_punchHexBuffer.size() == 32) PunchFlushHexBuffer();
}

void PSystemEngine::PunchFlushHexBuffer() {
    if (m_punchHexBuffer.empty() || !m_punchFile.is_open()) return;
    uint8_t len = (uint8_t)m_punchHexBuffer.size();
    uint16_t addr = m_punchHexAddress;
    const int LINE_BUF = 16 + 64 + 1; // ":NNAAAATT" + up to 32×2 hex digits + checksum + null
    char line[LINE_BUF];
    int pos = sprintf_s(line, LINE_BUF, ":%02X%04X00", len, addr);
    uint8_t checksum = (uint8_t)(len + (addr >> 8) + (addr & 0xFF)); // + record type 0x00, a no-op add
    for (uint8_t b : m_punchHexBuffer) {
        pos += sprintf_s(line + pos, LINE_BUF - pos, "%02X", b);
        checksum = (uint8_t)(checksum + b);
    }
    checksum = (uint8_t)(0 - checksum); // two's complement, wraps correctly in uint8_t arithmetic
    pos += sprintf_s(line + pos, LINE_BUF - pos, "%02X", checksum);
    m_punchFile << line << "  ";
    for (size_t i = 0; i < m_punchHexBuffer.size(); i++) {
        uint8_t b = m_punchHexBuffer[i];
        char disp = (b == ':') ? '.' : ((b >= 0x20 && b <= 0x7E) ? (char)b : '.');
        m_punchFile << disp;
        if ((i + 1) % 8 == 0 && i + 1 < m_punchHexBuffer.size()) m_punchFile << ' ';
    }
    m_punchFile << "\r\n";
    m_punchFile.flush();
    m_punchHexAddress = (uint16_t)(m_punchHexAddress + len);
    m_punchHexBuffer.clear();
}

// ---- Printer: device (unit #6 / "PRINTER:") logger ------------------
// Much simpler than PUNCH above: no hex encoding, no end-of-transfer
// marker, no printable-character column. Every character sent to the
// printer is written to C:\Tmp\PRINTER exactly as it was sent, so the
// file reads like a plain text capture of whatever the Pascal program
// printed -- nothing more. Opened once, in append mode, on first use,
// and left open for the rest of the session (unlike PUNCH, there's no
// natural "end of transfer" signal for ordinary printer output to close
// on).
void PSystemEngine::PrinterWriteChar(uint8_t c) {
    if (c == 0x00) return; // NULs are padding/filler, not real printer output -- drop them
    if (!m_printerFileOpen) {
        CreateDirectoryW(L"C:\\Tmp", nullptr); // no-op if it already exists
        m_printerFile.open("C:\\Tmp\\PRINTER", std::ios::app | std::ios::binary);
        m_printerFileOpen = true; // don't retry every character even if this failed
    }
    if (!m_printerFile.is_open()) return; // couldn't open -- drop silently
    m_printerFile.put((char)c);
    m_printerFile.flush(); // low-volume output; flush every character for reliability
}

void PSystemEngine::RunLoop() {
    m_running = true;
    m_halted = false;
    ResetEvent(m_stopEvent);
    bool tracingActive = false;

    if (m_hostClock && m_cpu.r.PC == 0xF000) HostClockStart();   // before the boot reads the date

    // Native boot in P-Code mode (see NativeBoot); Z80 mode keeps the Z80 boot.
    if (m_nativePcodeOps && !m_bootAttempted && m_cpu.r.PC == 0xF000) {
        m_bootAttempted = true;
        const uint16_t sp0 = m_cpu.r.SP;
        std::vector<uint8_t> before(m_mem, m_mem + 65536);
        const bool reclaimLayout = m_reclaimRequested && !m_preserveZ80RegisterCompat;
        m_nativeBooted = NativeBoot(m_nativeBootNote, reclaimLayout);
        if (!m_nativeBooted) {                       // restore the power-on state: the Z80 boot runs
            memcpy(m_mem, before.data(), 65536);
            m_cpu.r.PC = 0xF000; m_cpu.r.SP = sp0;
            m_rom.clear(); m_reclaimed = false; m_cxp02 = 0x03D7;
            m_varDelta = 0; m_builtInTables = false; m_ioReady = false; m_heapStart = 0x03A4;
        }
    }
    if (m_cpu.r.PC == 0xF000 && !m_nativeBooted && !m_haveLoader) {
        // The Z80 boot is needed (Z80 mode, register compatibility on, the
        // reclaim option off, or the native boot was not possible) -- and
        // pascal.bin is not there. Stop instead of executing empty memory.
        m_bootFault = "pascal.bin (the Z80 loader) was not found. It is needed to start the P-System in Z80 mode, "
                      "or in P-Code mode unless Z80 register compatibility is off and the Z80 interpreter and BIOS "
                      "memory is reclaimed.";
        if (!m_nativeBootNote.empty()) m_bootFault += " (Native boot was not possible: " + m_nativeBootNote + ".)";
        m_running = false;
        return;
    }

    for (long i = 0; !m_cpu.r.halted; i++) {
        // Checked every 4096 iterations, not every instruction: on Windows each
        // check is a kernel call, which was a measurable per-instruction cost.
        if ((i & 0xFFF) == 0 && WaitForSingleObject(m_stopEvent, 0) == WAIT_OBJECT_0) break;

        // This boot ROM contains a shared busy-wait subroutine at 0xF244
        // (DEC HL / OR L / JR NZ / DJNZ) used to simulate real disk
        // seek/settle timing on the original 1979 hardware. It has no
        // side effects beyond consuming time -- it always finishes with
        // HL=0, A=0, B=0, and the zero flag set, regardless of how it
        // was entered. Software emulation runs meaningfully slower than
        // the real Z80 it's timed for, so left alone this turns a
        // sub-second real delay into minutes of wall-clock time. Since
        // there's no physical disk that actually needs to seek here,
        // it's safe to fast-forward: force it to finish on its next
        // pass rather than counting all the way down.
        if (m_cpu.r.PC == 0xF244) {
            m_cpu.r.setHL(1);
            m_cpu.r.B = 1;
        }

        // ---- Native-C CONST/CONIN/CONOUT via the shared BIOS linker ---
        // SYSTEM.MICRO's own console-idle-poll (CHCLR) and character-echo
        // routines (CBIS/CBOS, called from ECHO) call the BIOS through
        // one shared "linker" subroutine -- LD A,(0002H); LD H,A;
        // JP (HL) -- regardless of which of them is calling. Verified
        // this directly by deliberately HALTing the real BIOS vectors
        // and walking the return-address chain on the stack: ECHO calls
        // CBOS, which falls into a shared helper that calls this exact
        // linker, which was about to jump into CONOUT (0xFE0C) when it
        // hit the HALT.        //
        // Intercepting HERE rather than at ECHO/CHCLR/CBOS's own
        // addresses is deliberate: those addresses are NOT stable across
        // SYSTEM.MICRO builds (this exact build's linker sits at 0x1DA2,
        // not the 0x1D4A the reference assembly listing shows), and
        // ECHO's own DLE-blank-decompression logic still runs normally
        // as real Z80 code either way -- by the time it reaches the
        // linker, ECHO has already decided exactly which raw byte needs
        // to go out, so the native replacement only has to be as smart
        // as CONST/CONIN/CONOUT themselves (trivial), not as smart as
        // ECHO.
        //
        // Register conventions (standard CP/M BIOS): CONST/CONIN return
        // their result in A; CONOUT takes its character in C and none of
        // the routines between ECHO and here ever touch C, so it's still
        // exactly what ECHO meant to send. H is set to mem[0x0002] to
        // match the real linker's own "LD H,A" side effect, purely for
        // fidelity -- nothing downstream appears to depend on it.
        //
        // CONST/CONIN/CONOUT (L=0x06/0x09/0x0C) are handled here, along
        // with HOME/LIST/PUNCH (L=0x18/0x0F/0x12) below; anything else
        // (disk-related BIOS calls, READER) falls through to the real
        // BIOS unchanged, so this stays strictly additive.
        if (!m_biosLinkerSearchDone && m_cpu.r.PC == 0x03B0) {
            m_biosLinkerSearchDone = true;
            for (uint32_t a = 0; a + 5 <= 0x10000; a++) {
                if (m_mem[a] == 0x3A && m_mem[a+1] == 0x02 && m_mem[a+2] == 0x00 &&
                    m_mem[a+3] == 0x67 && m_mem[a+4] == 0xE9) {
                    m_biosLinkerAddr = (uint16_t)a;
                    break;
                }
            }
        }
        if (m_biosLinkerAddr != 0 && m_cpu.r.PC == m_biosLinkerAddr) {
            uint8_t l = m_cpu.r.L;
            if (l == 0x06 || l == 0x09 || l == 0x0C) {
                if (l == 0x06) m_cpu.r.A = PortIn(0);        // CONST
                else if (l == 0x09) m_cpu.r.A = PortIn(1);   // CONIN
                else PortOut(1, m_cpu.r.C);                   // CONOUT
                m_cpu.r.H = m_mem[0x0002];
                m_cpu.r.PC = PopStackWord(); // simulate the eventual RET
                m_biosLinkerNativeCount++;
            }
            else if (l == 0x18) {
                // HOME: move the head to track 0. Meaningless for a
                // Big-Disk-style direct-block-access unit -- there's no
                // physical head position to reset -- so this is always
                // a safe, trivial success regardless of which unit or
                // caller triggered it. Confirmed via the call-stack
                // trace this is reached from DSK0's own UCLEAR handling
                // (CSP 37) even for unit #4/BIGGY itself, which
                // otherwise already bypasses BIOS entirely via the
                // native UNITREAD/UNITWRITE fast path.
                m_cpu.r.H = m_mem[0x0002];
                m_cpu.r.PC = PopStackWord();
                m_biosLinkerNativeCount++;
            }
            else if (l == 0x0F) {
                // LIST (printer output, unit #6 / "PRINTER:" from Pascal).
                // Mirrored to C:\Tmp\PRINTER verbatim, character for
                // character -- much like PUNCH (unit #8) below, but
                // without any hex encoding, end-marker detection, or
                // column formatting, since ordinary printer output has
                // no such transfer protocol to honor. See
                // PrinterWriteChar's own comment for details.
                PrinterWriteChar(m_cpu.r.C);
                m_cpu.r.H = m_mem[0x0002];
                m_cpu.r.PC = PopStackWord();
                m_biosLinkerNativeCount++;
            }
            else if (l == 0x12) {
                // PUNCH (paper-tape punch output, reached from Pascal as
                // REMOUT: -- "PUNCH" is the BIOS routine's own internal
                // name, not the volume name a program writes to). Unlike
                // LIST, this is no longer just discarded: every character
                // gets mirrored to C:\Tmp\Punch.out as an Intel-HEX dump.
                // See PunchWriteChar's own comment for the exact format.
                PunchWriteChar(m_cpu.r.C);
                m_cpu.r.H = m_mem[0x0002];
                m_cpu.r.PC = PopStackWord();
                m_biosLinkerNativeCount++;
            }
        }

        // ---- Native-C ECHO (direct calls, not via UNITWRITE) -----------
        // ConsoleEcho() above already replicates ECHO's DLE-decompression
        // and CR->LF logic for the UNITREAD/UNITWRITE fast path, but ECHO
        // (0x1DD2) is also called directly from elsewhere in SYSTEM.MICRO
        // -- those callers still ran real Z80 code even after ConsoleEcho
        // existed, since nothing intercepted ECHO's own entry point.
        // Only proceeds when COVECT is CONOUT (0x0C), matching
        // ConsoleEcho's own hardcoded PortOut(1,...) assumption; any other
        // target (LIST/PUNCH/etc, reached via a real ECHO call rather than
        // UNITWRITE) falls through untouched.
        // NOTE (v1.74): INACTIVE with the SYSTEM.MICRO this system runs (BIGGY's).
        // These hooks are at the addresses of the U120 distribution build;
        // in BIGGY's interpreter the unit-I/O routines are 30 bytes higher
        // (UREAD 1BAE, UWRITE 1BA9, ECHO 1E2A) and these addresses fall in
        // the middle of other instructions, so they are never reached. Unit
        // I/O is native at the CSP level instead: see NativeCsp.inc.
        if (m_cpu.r.PC == 0x1DD2) {
            uint8_t covect = m_mem[PM_V(0x02E3)]; // COVECT
            if (covect == 0x0C) {
                uint8_t uasy = m_mem[PM_V(0x02DA)]; // UASY
                bool dleEnabled = (uasy & 0x04) == 0;   // DLEBIT clear = decompression enabled
                bool crlfEnabled = (uasy & 0x08) == 0;  // CRLFBIT clear = auto-LF enabled
                uint8_t c = m_cpu.r.C;
                const uint16_t CLAST = PM_V(0x02E1);
                uint8_t aFinal;

                if (dleEnabled && m_mem[CLAST] == 0x10) {
                    // $10/$20: continuing a DLE blank-run -- c is the run-length
                    // byte (count = c - 0x20). Emit that many spaces; the loop's
                    // own final "DEC A" (which triggered the exit) is the last
                    // thing that touches A before RET.
                    uint8_t clast = (uint8_t)(c - 0x20);
                    while (true) {
                        uint8_t next = (uint8_t)(clast - 1);
                        if (next & 0x80) { aFinal = next; break; } // "negative" as signed -- run exhausted
                        clast = next;
                        PortOut(1, 0x20);
                    }
                    m_mem[CLAST] = clast;
                } else {
                    // $30: normal path
                    m_mem[CLAST] = c;
                    bool outputIt = true;
                    if (c == 0x10) {
                        if (dleEnabled) {
                            outputIt = false; // swallow the DLE marker
                            aFinal = 0;        // "AND DLEBIT" with DLEBIT clear -> A=0
                        } else {
                            m_mem[CLAST] = 0x20; // "lie about last char" -- DLE expansion
                                                  // can span UWRITE boundaries
                        }
                    }
                    if (outputIt) {
                        PortOut(1, c);
                        if (!crlfEnabled) {
                            aFinal = (uint8_t)(uasy & 0x08); // "AND CRLFBIT" leftover (CRLFBIT was set, so nonzero)
                        } else {
                            // crlfEnabled (CRLFBIT clear) -- real code always loads
                            // CLAST next to compare against CR, regardless of the
                            // outcome, so A picks that up even when it's a no-match.
                            aFinal = m_mem[CLAST];
                            if (m_mem[CLAST] == 0x0D) {
                                m_mem[CLAST] = 0x0A;
                                PortOut(1, 0x0A);
                                aFinal = covect; // the auto-LF CBOS call overwrites A with its own leftover
                            }
                        }
                    }
                }
                m_cpu.r.A = aFinal;
                m_cpu.r.PC = PopStackWord(); // matches ECHO's own "RET"
                m_biosLinkerNativeCount++;
            }
            // else: COVECT targets something ConsoleEcho doesn't model -- fall through untouched.
        }

        // ---- P-code trace helper (two call sites in this loop) -----------
        // Logs one line per P-code instruction whenever PC == 0x03B0 (the
        // BACK fetch point) and BC holds the IPC.
        //
        // SITE 1 (just below) fires when BACK is reached naturally --
        // the non-native handler did JP BACK, or the previous iteration's
        // cpu.step() ran Z80 BACK and left PC == 0x03B0.  This is the
        // only site needed in Z80 mode (m_nativePcodeOps == false) because
        // Z80 BACK dispatches to a handler and PC is never 0x03B0 again
        // until the handler finishes and does its own JP BACK.
        //
        // SITE 2 (after the native switch + packed-immediate blocks, at the
        // bottom of the loop) fires when a NATIVE case finishes and sets
        // PC back to 0x03B0 -- which means BOTH the opcode count and the
        // log for the NEXT opcode need to happen before cpu.step() runs
        // Z80 BACK for real. Without Site 2, every other opcode in native
        // mode would be silently skipped in the log (and double-consumed).
        //
        // Both sites share the same lambda so the log format is identical.
        // m_opcodeFetchCount is incremented inside the lambda -- never at
        // the native BACK dispatch block below, where it was previously.
        auto rd16 = [&](uint16_t a) -> uint16_t {
            return (uint16_t)(m_mem[a] | (m_mem[(uint16_t)(a+1)] << 8));
        };
        auto logPcodeAtBack = [&]() {
            if (m_cpu.r.PC != 0x03B0) return;
            tracingActive = true; // arm on first BACK (proof interpreter is running)
            uint16_t bc     = m_cpu.r.BC(); // bc IS the IPC at this point
            uint8_t  opcode = PM_CODE8(bc);
            m_opcodeFetchCount[opcode]++;   // count here -- not in the dispatch block
            if (opcode == OP_CSP) {
                // CSP's procedure-number byte is the one right after its own
                // opcode byte (mem[bc+1]). Counted here, unconditionally,
                // rather than solely inside CSP's own native-C case (which
                // is gated on m_nativePcodeOps and so never runs in Z80
                // mode) -- this keeps the Op Code Count dialog's per-
                // selector breakdown accurate in both modes, matching how
                // m_opcodeFetchCount itself is already counted
                // unconditionally right above. CSP's native case no longer
                // increments this itself (see its own comment) to avoid
                // double-counting in native mode.
                m_cspSelectorCount[PM_CODE8((uint16_t)(bc + 1))]++;
            }
            if (m_tracing && m_traceFile) {
                fprintf(m_traceFile,
                    "IPC=%04X %-8s(%02X)  SP=%04X  "
                    "NP=%04X MPD0=%04X BASED0=%04X IPCSAV=%04X  "
                    "MP=%04X BASE=%04X JTAB=%04X SEGP=%04X\n",
                    bc, PCodeOpcodeName(opcode), opcode, m_cpu.r.SP,
                    rd16(PM_V(0x0240)), rd16(PM_V(0x0242)), rd16(PM_V(0x0244)), rd16(PM_V(0x0246)),
                    rd16(PM_V(0x02F0)), rd16(PM_V(0x02F2)), rd16(PM_V(0x02F4)), rd16(PM_V(0x02F6)));
                if (i % 1024 == 0) fflush(m_traceFile);
            }
        };

        // Native BACK1 (0x03A4: "GETIPC; JP BACK"): reload the IPC from
        // IPCSAV and dispatch. BC is the only register it changes, so this is
        // exact with or without Z80 register compatibility.
        if (m_nativePcodeOps && m_cpu.r.PC == 0x03A4) {
            m_cpu.r.setBC(rd16(PM_V(0x0246)));
            m_cpu.r.PC = 0x03B0;
        }
        // Reclaim the interpreter's memory at the first P-code instruction
        // (see SetReclaimInterpreterMemory). Only in native mode with
        // register compatibility off, and only if the heap is exactly where
        // the boot left it.
        // The P-System has halted (see IsSystemHalted): ABORT's routine is a
        // jump to itself. Its address comes from XFRTBL (BACK: H = 01,
        // L = 2 * opcode) at the first P-code instruction.
        if (m_timAddr == 0 && m_cpu.r.PC == 0x03B0)           // the Z80 TIM routine: CSP table entry 9
            m_timAddr = (uint16_t)(PM_ROM(0x15E7 + 18) | (PM_ROM(0x15E7 + 19) << 8));
        if (m_abortAddr == 0 && m_cpu.r.PC == 0x03B0)
            m_abortAddr = (uint16_t)(PM_ROM(0x01AC) | (PM_ROM(0x01AD) << 8));   // XFRTBL (the table copy when not in memory)
        if (m_abortAddr != 0 && m_cpu.r.PC == m_abortAddr) { m_systemHalted = true; break; }
        if (m_reclaimRequested && !m_reclaimed && m_cpu.r.PC == 0x03B0 && m_nativePcodeOps &&
            !m_preserveZ80RegisterCompat && (m_mem[PM_V(0x0240)] | (m_mem[PM_V(0x0241)] << 8)) == 0x1EBA) {
            m_rom.assign(m_mem, m_mem + 65536);
            m_mem[PM_V(0x0240)] = 0xA4; m_mem[PM_V(0x0241)] = 0x03;               // NP := 0x03A4
            memset(m_mem + 0x03A4, 0x76, 0x1EBA - 0x03A4);           // HALT fill: any Z80 use shows up
            // Run-time errors: XEQERR points the IPC at the 3-byte P-code
            // instruction CXP 0,2 (call EXECERROR) at 0x03D7, which is now heap.
            // Keep a copy in page zero (the CP/M buffer, unused without CP/M).
            memcpy(m_mem + 0x0080, &m_rom[0x03D7], 3);
            m_cxp02 = 0x0080;
            m_reclaimed = true;
        }
        if (m_verifyFile && m_cpu.r.PC == 0x03B0) {               // Verify P-System log
            VerifyLogStep();
            if (m_verifyHaltNow) break;   // stop exactly at a mismatch (not up to 4096 iterations later)
        }
        logPcodeAtBack(); // Site 1 -- always, both modes

        // ---- Native-C BACK (the p-code fetch/dispatch loop itself) -----
        // BACK (0x03B0) reads the next opcode byte, and either routes to
        // SLDCI (top bit clear -- SLDC, packed-immediate) or looks the
        // opcode up in XFRTBL (0x0100, one word-pointer per opcode
        // 0x80-0xFF) and jumps to whatever it finds there. Doing this
        // dispatch step natively costs nothing and is always safe: the
        // target itself is unchanged, whether it's one of the native
        // cases in the switch below or a real, unreplaced Z80 handler.
        //
        // This also makes p-code opcode 0xD7 ("BACK", a genuinely
        // distinct opcode whose own XFRTBL entry happens to point right
        // back at this fetch loop's own label -- a true no-op) fully
        // native automatically, with no dedicated case needed: computing
        // its target yields 0x03B0 itself, so PC is already exactly
        // where the next fetch needs it.
        // The shortcut below always keeps the Z80 BC register pair (and
        // m_ipc, the P-machine IPC register it mirrors) in sync,
        // unconditionally: BC/m_ipc is itself a P-machine register (the
        // interpreter's own program counter), not a Z80 scratch register
        // being preserved for compatibility's sake, and every opcode
        // (converted or not) still depends on it directly.
        //
        // The doubled-opcode mirror into A, and DE/HL in the XFRTBL
        // branch below, ARE gated on m_preserveZ80RegisterCompat: these
        // are pure Z80 scratch-register bookkeeping with no P-machine
        // meaning of their own. Whether an unconverted opcode's own real
        // Z80 fallback code would still depend on one of them is not a
        // concern this flag is trying to protect against -- that's
        // exactly the kind of Z80-register convergence this effort is
        // moving away from, not preserving. What's actually verified
        // during this work is P-machine register equality (NP, MPD0,
        // BASED0, MP, BASE, JTAB, SEGP, and the P-code stack itself, via
        // SP/top0/top1) -- Z80 scratch-register agreement is expected to
        // come and go opcode by opcode as more get converted, not
        // something to chase down opcode by opcode as a compatibility
        // requirement.
        //
        // SLDC/SLDL/SLDO/SIND below don't depend on A at all anymore --
        // they recompute the doubled-opcode value fresh from memory
        // instead, since this shortcut doesn't always run immediately
        // before them (real Z80 BACK code can reach their entry point on
        // its own, same as it can for the main opcode switch below,
        // which is why that one uses NativeOpcodeForTarget's own reverse
        // lookup rather than a value stored here).
        if (m_nativePcodeOps && m_cpu.r.PC == 0x03B0) {
            m_ipc = m_cpu.r.BC(); // P-machine IPC register; BC is still the ground truth here
            uint8_t opcode = PM_CODE8(m_ipc);
            // NOTE: opcode already counted by logPcodeAtBack() above.
            m_ipc = (uint16_t)(m_ipc + 1);
            m_cpu.r.setBC(m_ipc);
            if (m_preserveZ80RegisterCompat) m_cpu.r.A = (uint8_t)(opcode << 1); // "ADD A,A" -- doubled, truncated to 8 bits
            if ((opcode & 0x80) == 0) {
                m_cpu.r.PC = 0x03AB; // SLDCI -- handled separately below, not by the opcode switch
            } else {
                uint16_t xfrtblAddr = (uint16_t)(0x0100 + (opcode - 0x80) * 2);
                uint16_t target = (uint16_t)(PM_ROM(xfrtblAddr) | (PM_ROM((uint16_t)(xfrtblAddr + 1)) << 8)); // XFRTBL
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setDE((uint16_t)(xfrtblAddr + 1)); // "EX DE,HL" leaves DE = the table entry address (+1)
                    m_cpu.r.setHL(target);
                }
                m_cpu.r.PC = target;
            }
        }

        // ---- Native-C p-code opcode replacements -----------------------
        // Gated on m_nativePcodeOps -- when false (Z80 mode), all of these
        // are skipped and the real Z80 handlers run instead.
        //
        // Dispatches on the p-code opcode byte itself (case OP_ADI, etc.)
        // rather than on the Z80 target address it resolves to, via
        // NativeOpcodeForTarget's own reverse lookup (see that function's
        // comment). This is checked every iteration, unconditionally --
        // not only right after the shortcut above -- because PC can also
        // arrive at one of these 68 known addresses via real Z80 BACK (or
        // BACK1) code continuing to execute one instruction at a time
        // after a previous native commit (cpu.step() always runs once per
        // outer iteration, even immediately after a commit sets PC back
        // to 0x03B0). A case's own "break;" without touching PC (its
        // rare-case Z80 fallback path) still works correctly: PC is
        // already sitting at the real Z80 handler's entry point by the
        // time the switch runs (set either by the shortcut above or by
        // real Z80 code's own preceding jump), so leaving it alone is
        // precisely "let the real Z80 code run from here."
#include "NativeErrors.inc"
        int nativeOpcodeLookup = m_nativePcodeOps ? NativeOpcodeForTarget(m_cpu.r.PC) : -1;
        const uint16_t nativeEntryPc = m_cpu.r.PC;   // to recognise a native hand-over to another native entry
        // "commit" collapses the many identical "m_cpu.r.PC = 0x03B0;"
        // lines that used to end most cases below into this one, shared
        // spot: a case that wants to hand off to BACK (the ordinary,
        // successful-completion case) just sets commit = true instead of
        // touching PC itself. Defaults to false, so cases that need
        // something else entirely -- falling through untouched (PC stays
        // at the real Z80 target the shortcut above already set), or
        // jumping to a specific different address (INVNDX, BACK1, a
        // still-real-Z80 routine, etc.) -- are completely unaffected:
        // they already set PC explicitly themselves and leave commit
        // alone, so the line below never overwrites what they did. This
        // is a pure mechanical simplification, not a P-machine-register
        // change -- PC is still a Z80 register being used as the engine's
        // own dispatch mechanism, this just stops repeating the same
        // assignment ~65 times.
        bool commit = false;
        if (nativeOpcodeLookup >= 0) switch ((uint8_t)nativeOpcodeLookup) {
            case OP_ADI: { // ADI: POP DE; POP HL; ADD HL,DE; PUSH HL; JP BACK
                uint16_t de = PopStackWord();
                uint16_t hl = PopStackWord();
                uint16_t result = (uint16_t)(hl + de);
                PushStackWord(result);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setDE(de);      // real Z80 leaves DE = the popped addend
                    m_cpu.r.setHL(result);  // real Z80 leaves HL = the sum
                }
                commit = true;
                m_opcodeEmulatedCount[OP_ADI]++;
                break;
            }
            case OP_NGI: { // NGI: POP HL; negate; PUSH HL; JP BACK (DE untouched)
                uint16_t hl = PopStackWord();
                uint16_t result = (uint16_t)(0 - hl);
                PushStackWord(result);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setHL(result);  // real Z80 leaves HL = the negated value
                    m_cpu.r.A = (uint8_t)(result >> 8); // real code's last step is "LD H,A", so A ends up = H
                }
                commit = true;
                m_opcodeEmulatedCount[OP_NGI]++;
                break;
            }
            case OP_SBI: { // SBI: POP DE; POP HL; SBC HL,DE (net HL-DE); PUSH HL; JP BACK
                uint16_t de = PopStackWord();
                uint16_t hl = PopStackWord();
                uint16_t result = (uint16_t)(hl - de);
                PushStackWord(result);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setDE(de);      // real Z80 leaves DE = the popped subtrahend
                    m_cpu.r.setHL(result);  // real Z80 leaves HL = the difference
                }
                commit = true;    // (real code never touches A here -- "AND A" only affects flags)
                m_opcodeEmulatedCount[OP_SBI]++;
                break;
            }
            case OP_CHK: { // CHK: Check number against limits (range-checking).
                            // Stack on entry (top to bottom): max, min, num --
                            // the compiler pushes num first (deepest), then
                            // min, then max (topmost). Verifies signed
                            // min<=num<=max. On success, num is left on the
                            // stack (net effect: 3 pops, 1 push) and execution
                            // continues at BACK. On failure, num is likewise
                            // left on the stack (matching the real code's own
                            // "leave num on stack to help person debug"
                            // comment), IPCSAV is updated via SAVIPC, and the
                            // program bombs via INVNDX.
                            //
                            // Register fidelity: the real code checks min
                            // first (DE=min, HL=num), and only if that passes
                            // reloads DE=max and checks again (HL=num
                            // throughout). Each check takes one of two Z80
                            // code paths depending on whether the two
                            // operands share a sign (a straight 16-bit
                            // subtract, whose high byte ends up in A) or
                            // differ in sign (an XOR/AND trick on just the
                            // high bytes, also ending up in A) -- replicated
                            // exactly below so A comes out bit-for-bit
                            // identical to the real hardware on any of the
                            // four failure paths or two success paths.
                uint16_t maxRaw = PopStackWord();
                uint16_t minRaw = PopStackWord();
                uint16_t numRaw = PeekStackWord(0); // already sitting where it needs to end up
                int16_t num  = (int16_t)numRaw;
                int16_t minV = (int16_t)minRaw;
                int16_t maxV = (int16_t)maxRaw;
                uint8_t minHi = minRaw >> 8, numHi = numRaw >> 8, maxHi = maxRaw >> 8;

                bool minOk = (num >= minV);
                uint8_t aAfterMin = ((minHi ^ numHi) & 0x80) == 0
                    ? (uint8_t)((uint16_t)(numRaw - minRaw) >> 8)
                    : (uint8_t)((minHi ^ numHi) & minHi);

                if (minOk) {
                    bool maxOk = (num <= maxV);
                    uint8_t aAfterMax = ((maxHi ^ numHi) & 0x80) == 0
                        ? (uint8_t)((uint16_t)(maxRaw - numRaw) >> 8)
                        : (uint8_t)((maxHi ^ numHi) & numHi);
                    if (maxOk) {
                        if (m_preserveZ80RegisterCompat) {
                            m_cpu.r.A = aAfterMax;
                            m_cpu.r.setHL(numRaw);
                            m_cpu.r.setDE(maxRaw);
                        }
                        commit = true; // BACK
                    } else {
                        // Failure path (INVNDX) left fully unconditional: real Z80
                        // error-handling code runs from here, and unlike a plain
                        // BACK commit, nothing downstream re-reads these registers
                        // fresh -- so unlike the success path above, this one isn't
                        // provably safe to gate without checking what INVNDX itself
                        // depends on. Left as-is out of caution.
                        m_cpu.r.A = aAfterMax;
                        m_cpu.r.setHL(numRaw);
                        m_cpu.r.setDE(maxRaw);
                        m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; // SAVIPC
                        m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8;
                        m_cpu.r.PC = 0x03DA; // INVNDX
                    }
                } else {
                    m_cpu.r.A = aAfterMin;
                    m_cpu.r.setHL(maxRaw); // EX (SP),HL at $98 swaps HL(num) with stack-top(max)
                    m_cpu.r.setDE(minRaw);
                    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; // SAVIPC
                    m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8;
                    m_cpu.r.PC = 0x03DA; // INVNDX
                }
                m_opcodeEmulatedCount[0x88]++;
                break;
            }
            case OP_NOT: { // NOT: POP HL; complement both bytes (full 16-bit one's
                            // complement, NOT a 0/1 flip -- TRUE is represented as
                            // 0xFFFF here); PUSH HL; JP BACK
                uint16_t value = PopStackWord();
                uint16_t result = (uint16_t)(~value);
                PushStackWord(result);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setHL(result);
                    m_cpu.r.A = (uint8_t)(result >> 8); // real code's last step is "LD H,A"
                }
                commit = true;
                m_opcodeEmulatedCount[OP_NOT]++;
                break;
            }
            case OP_ABI: { // ABI: POP HL; if negative, two's-complement negate and
                            // mask the high byte to 0-127 (handles ABI(-32768), which
                            // would otherwise overflow back to -32768); PUSH HL; JP BACK
                uint16_t value = PopStackWord();
                uint16_t result;
                if ((int16_t)value >= 0) result = value;
                else { result = (uint16_t)(0 - value); result &= 0x7FFF; }
                PushStackWord(result);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setHL(result);
                    m_cpu.r.A = (uint8_t)(result >> 8); // both paths end with "LD H,A" (or equivalent)
                }
                commit = true;
                m_opcodeEmulatedCount[0x80]++;
                break;
            }
            // ---- Integer comparisons: "pop b; pop a; push (a <relop> b)" ----
            // The BOOLEAN result is a straightforward signed int16_t compare --
            // no need to replicate the real code's overflow-avoiding sign
            // gymnastics for that part, a C++ signed comparison gets the
            // identical correct answer directly. But the real code's leftover
            // A register is a genuine intermediate byte-subtraction artifact
            // (not simply 0/1), found via the stricter register-exact trace-
            // diff added when implementing SLDC/SLDL/SLDO: A ends up holding
            // whatever the last SUB/SBC left behind on the path taken, so it
            // depends on the actual byte values, not just the boolean
            // outcome. Derived directly from the source, byte-by-byte, for
            // each of the six (they don't all take the same path -- GTRI
            // subtracts in the opposite order from GEQI, and LEQI/LESI pop
            // into the opposite physical registers before reusing GEQI/
            // GTRI's own code, which changes which byte the shared "AND H"
            // step in the differing-signs case ends up masking against).
            case OP_EQUI: { // EQUI
                uint16_t bRaw = PopStackWord();
                uint16_t aRaw = PopStackWord();
                uint16_t result = ((int16_t)aRaw == (int16_t)bRaw) ? 1 : 0;
                PushStackWord(result);
                uint8_t aLo = aRaw & 0xFF, aHi = aRaw >> 8, bLo = bRaw & 0xFF, bHi = bRaw >> 8;
                uint8_t lowSub = (uint8_t)(aLo - bLo); // LD A,L; SUB E
                uint8_t aFinal = (lowSub != 0) ? lowSub : (uint8_t)(aHi - bHi); // else LD A,H; SBC A,D (borrow always 0 here)
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = aFinal;
                    m_cpu.r.setDE(bRaw);
                    m_cpu.r.setHL(result);
                }
                commit = true;
                m_opcodeEmulatedCount[0xC3]++;
                break;
            }
            case OP_NEQI: { // NEQI (identical A-path to EQUI, just the opposite final branch)
                uint16_t bRaw = PopStackWord();
                uint16_t aRaw = PopStackWord();
                uint16_t result = ((int16_t)aRaw != (int16_t)bRaw) ? 1 : 0;
                PushStackWord(result);
                uint8_t aLo = aRaw & 0xFF, aHi = aRaw >> 8, bLo = bRaw & 0xFF, bHi = bRaw >> 8;
                uint8_t lowSub = (uint8_t)(aLo - bLo);
                uint8_t aFinal = (lowSub != 0) ? lowSub : (uint8_t)(aHi - bHi);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = aFinal;
                    m_cpu.r.setDE(bRaw);
                    m_cpu.r.setHL(result);
                }
                commit = true;
                m_opcodeEmulatedCount[0xCB]++;
                break;
            }
            case OP_GEQI: { // GEQI
                uint16_t bRaw = PopStackWord();
                uint16_t aRaw = PopStackWord();
                uint16_t result = ((int16_t)aRaw >= (int16_t)bRaw) ? 1 : 0;
                PushStackWord(result);
                uint8_t aLo = aRaw & 0xFF, aHi = aRaw >> 8, bLo = bRaw & 0xFF, bHi = bRaw >> 8;
                uint8_t xorResult = (uint8_t)(bHi ^ aHi); // LD A,D; XOR H
                uint8_t aFinal;
                if (xorResult & 0x80) { // JP M,GEQ1 (signs differ)
                    aFinal = (uint8_t)(xorResult & aHi); // GEQ1: AND H
                } else {
                    bool borrow = aLo < bLo; // LD A,L; SUB E
                    aFinal = (uint8_t)(aHi - bHi - (borrow ? 1 : 0)); // LD A,H; SBC A,D
                }
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = aFinal;
                    m_cpu.r.setDE(bRaw);
                    m_cpu.r.setHL(result);
                }
                commit = true;
                m_opcodeEmulatedCount[0xC4]++;
                break;
            }
            case OP_GTRI: { // GTRI (shares GEQI's GEQ1 target for differing signs; same-sign
                            // path subtracts in the opposite order: b-a instead of a-b)
                uint16_t bRaw = PopStackWord();
                uint16_t aRaw = PopStackWord();
                uint16_t result = ((int16_t)aRaw > (int16_t)bRaw) ? 1 : 0;
                PushStackWord(result);
                uint8_t aLo = aRaw & 0xFF, aHi = aRaw >> 8, bLo = bRaw & 0xFF, bHi = bRaw >> 8;
                uint8_t xorResult = (uint8_t)(bHi ^ aHi);
                uint8_t aFinal;
                if (xorResult & 0x80) {
                    aFinal = (uint8_t)(xorResult & aHi); // same GEQ1 code as GEQI
                } else {
                    bool borrow = bLo < aLo; // LD A,E; SUB L (b_lo - a_lo)
                    aFinal = (uint8_t)(bHi - aHi - (borrow ? 1 : 0)); // LD A,D; SBC A,H
                }
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = aFinal;
                    m_cpu.r.setDE(bRaw);
                    m_cpu.r.setHL(result);
                }
                commit = true;
                m_opcodeEmulatedCount[0xC5]++;
                break;
            }
            case OP_LEQI: { // LEQI (pops HL=b, then DE=a -- opposite physical order from
                            // the above four -- then jumps into GEQI's own GEQ0 code, so H
                            // holds b's high byte here instead of a's, changing what the
                            // shared differing-signs "AND H" step masks against)
                uint16_t bRaw = PopStackWord();
                uint16_t aRaw = PopStackWord();
                uint16_t result = ((int16_t)aRaw <= (int16_t)bRaw) ? 1 : 0;
                PushStackWord(result);
                uint8_t aLo = aRaw & 0xFF, aHi = aRaw >> 8, bLo = bRaw & 0xFF, bHi = bRaw >> 8;
                uint8_t xorResult = (uint8_t)(aHi ^ bHi); // same value regardless of operand order
                uint8_t aFinal;
                if (xorResult & 0x80) {
                    aFinal = (uint8_t)(xorResult & bHi); // H=b_hi in this register mapping
                } else {
                    bool borrow = bLo < aLo;
                    aFinal = (uint8_t)(bHi - aHi - (borrow ? 1 : 0));
                }
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = aFinal;
                    m_cpu.r.setDE(aRaw);
                    m_cpu.r.setHL(result);
                }
                commit = true;
                m_opcodeEmulatedCount[0xC8]++;
                break;
            }
            case OP_LESI: { // LESI (pops HL=b, then DE=a, jumps into GTRI's GTR0 code)
                uint16_t bRaw = PopStackWord();
                uint16_t aRaw = PopStackWord();
                uint16_t result = ((int16_t)aRaw < (int16_t)bRaw) ? 1 : 0;
                PushStackWord(result);
                uint8_t aLo = aRaw & 0xFF, aHi = aRaw >> 8, bLo = bRaw & 0xFF, bHi = bRaw >> 8;
                uint8_t xorResult = (uint8_t)(aHi ^ bHi);
                uint8_t aFinal;
                if (xorResult & 0x80) {
                    aFinal = (uint8_t)(xorResult & bHi); // same GEQ1 code, H=b_hi in this mapping
                } else {
                    bool borrow = aLo < bLo; // GTR0's own path: LD A,E(a_lo);SUB L(b_lo) in this mapping
                    aFinal = (uint8_t)(aHi - bHi - (borrow ? 1 : 0));
                }
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = aFinal;
                    m_cpu.r.setDE(aRaw);
                    m_cpu.r.setHL(result);
                }
                commit = true;
                m_opcodeEmulatedCount[0xC9]++;
                break;
            }
            case OP_UJP: { // UJP: Unconditional jump. Only the short-relative
                            // encoding is handled natively (the common case,
                            // same as FJP); the rarer jump-table-indexed long
                            // jump falls through untouched, exactly like FJP.
                uint8_t ofs = PM_CODE8(m_cpu.r.BC());
                if ((ofs & 0x80) == 0) {
                    uint16_t newBC = (uint16_t)(m_cpu.r.BC() + 1 + ofs);
                    m_cpu.r.setBC(newBC); // IPC jump target -- functionally required, not a compat mirror
                    if (m_preserveZ80RegisterCompat) m_cpu.r.A = (uint8_t)(newBC >> 8); // same ADD/ADC-derived leftover as FJP's short-jump path
                    commit = true;
                    m_opcodeEmulatedCount[0xB9]++;
                } else {
                    // Long jump through the procedure's jump table (offset >= 0x80): the entry
                    // at JTAB + (offset - 256) holds a self-relative pointer to the target
                    // (Z80 UJP $10: LD HL,(JTAB); BC := FFxx; ADD HL,BC; SELREL).
                    uint16_t jtab = (uint16_t)(m_mem[PM_V(0x02F4)] | (m_mem[PM_V(0x02F5)] << 8));
                    uint16_t entry = (uint16_t)(jtab + ofs - 256);
                    uint16_t rel = PM_CODE16(entry);
                    uint16_t target = (uint16_t)(entry - rel);
                    m_cpu.r.setBC(target);
                    if (m_preserveZ80RegisterCompat) { m_cpu.r.A = ofs; m_cpu.r.setDE(rel); m_cpu.r.setHL(target); }
                    commit = true;
                    m_opcodeEmulatedCount[0xB9]++;
                }
                break;
            }
            case OP_STL: { // STL: Store local word. Same structure as SRO, but
                            // against the local/frame area (MPD0) instead of
                            // globals (BASED0).
                uint16_t offset = DecodeGBDE();
                uint16_t mpd0 = (uint16_t)(m_mem[PM_V(0x0242)] | (m_mem[PM_V(0x0243)] << 8));
                uint16_t addr = (uint16_t)(mpd0 + offset * 2);
                uint16_t value = PopStackWord();
                m_mem[addr] = value & 0xFF;
                m_mem[(uint16_t)(addr + 1)] = value >> 8;
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setDE(value); // overwrites the offset, same as SRO
                    m_cpu.r.setHL((uint16_t)(addr + 1));
                }
                commit = true;
                m_opcodeEmulatedCount[0xCC]++;
                break;
            }
            case OP_IXA: { // IXA: Index array. Given element_size (word units,
                            // GBDE-encoded), an index and array base on the
                            // stack (index on top), compute base + index *
                            // element_size * 2 and push it. The real code
                            // special-cases element_size==1 (skip a call to
                            // the MULT subroutine, just double the index
                            // directly) purely as a speed optimization -- both
                            // paths converge on the exact same final formula,
                            // confirmed by reading MULT itself (a standard
                            // shift-and-add 16-bit multiply, truncating
                            // exactly like plain C++ multiplication would).
                            // Uses BACK1 rather than BACK in the real code
                            // (BACK1 restores BC from IPCSAV, since BC gets
                            // repurposed as scratch for the two POPs along
                            // the way) -- moot for BC itself here since this
                            // code never repurposes m_cpu.r's BC in the
                            // first place, so there's nothing to restore
                            // there. BUT SAVIPC's write to IPCSAV (0x0246)
                            // is itself observable state, read later by
                            // error-handling/XEQERR -- that still needs
                            // replicating even though nothing here reads it
                            // back. Missed this originally; caught via a
                            // P-machine-register trace-diff mismatch, not a
                            // Z80-register one, since it doesn't affect any
                            // Z80 register at all.
                uint16_t elementSize = DecodeGBDE();
                m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF;   // SAVIPC
                m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8;
                uint16_t index = PopStackWord();
                uint16_t offset = (uint16_t)(index * elementSize * 2);
                uint16_t base = PopStackWord();
                uint16_t result = (uint16_t)(offset + base);
                PushStackWord(result);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = (uint8_t)((elementSize & 0xFF) - 1); // LD A,E; DEC A -- last A-write in IXA's own code
                    m_cpu.r.setDE(elementSize == 1 ? elementSize : (uint16_t)0); // MULT always leaves DE=0 when actually called
                    m_cpu.r.setHL(result);
                }
                commit = true;
                m_opcodeEmulatedCount[0xA4]++;
                break;
            }
            case OP_LDB: { // LDB: Load byte. Pop index, pop base, add, load
                            // one byte zero-extended to a word, push.
                uint16_t index = PopStackWord();
                uint16_t base = PopStackWord();
                uint16_t addr = (uint16_t)(base + index);
                uint16_t value = m_mem[addr]; // zero-extended (D is explicitly cleared in the real code)
                PushStackWord(value);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setDE(value); // real code's "LD E,(HL); LD D,00H" overwrites DE with (0, byte)
                    m_cpu.r.setHL(addr); // real code never touches A here
                }
                commit = true;
                m_opcodeEmulatedCount[0xBE]++;
                break;
            }
            case OP_LAO: { // LAO: Load global address (not the value -- just
                            // computes and pushes the address itself).
                uint16_t offset = DecodeGBDE();
                uint16_t based0 = (uint16_t)(m_mem[PM_V(0x0244)] | (m_mem[PM_V(0x0245)] << 8));
                uint16_t addr = (uint16_t)(based0 + offset * 2);
                PushStackWord(addr);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setDE(offset); // NOT overwritten here -- unlike LDO/SRO,
                                            // nothing pops into DE after the address
                                            // computation, so GBDE's leftover (via
                                            // DecodeGBDE, already applied to A) plus
                                            // the offset itself in DE both survive
                    m_cpu.r.setHL(addr);
                }
                commit = true;
                m_opcodeEmulatedCount[0xA5]++;
                break;
            }
            case OP_LAND: { // LAND: Logical AND (also used for Pascal set
                            // intersection at this single-word size). Plain
                            // bitwise AND of the two popped 16-bit values.
                uint16_t deRaw = PopStackWord();
                uint16_t hlRaw = PopStackWord();
                uint16_t result = (uint16_t)(deRaw & hlRaw);
                PushStackWord(result);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = (uint8_t)(result >> 8); // "LD A,D; AND H; LD H,A" is the last A-write
                    m_cpu.r.setDE(deRaw); // DE is only ever READ (via LD A,E / LD A,D), never written
                    m_cpu.r.setHL(result);
                }
                commit = true;
                m_opcodeEmulatedCount[0x84]++;
                break;
            }
            case OP_INCR: { // INCR: Increment (SP) by literal. Pops a value
                            // (typically an address), adds literal*2 (literal
                            // is in WORD units), pushes the result back.
                uint16_t literal = DecodeGBDE();
                uint16_t value = PopStackWord();
                uint16_t result = (uint16_t)(value + literal * 2);
                PushStackWord(result);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setDE(literal); // NOT overwritten -- nothing pops into
                                             // DE after the address arithmetic here
                    m_cpu.r.setHL(result);
                }
                commit = true;
                m_opcodeEmulatedCount[0xA2]++;
                break;
            }
            case OP_LDL: { // LDL: Load local word. Same structure as LDO, but
                            // against the local/frame area (MPD0).
                uint16_t offset = DecodeGBDE();
                uint16_t mpd0 = (uint16_t)(m_mem[PM_V(0x0242)] | (m_mem[PM_V(0x0243)] << 8));
                uint16_t addr = (uint16_t)(mpd0 + offset * 2);
                uint16_t value = (uint16_t)(m_mem[addr] | (m_mem[(uint16_t)(addr + 1)] << 8));
                PushStackWord(value);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setDE(value); // overwritten by the load, same as LDO
                    m_cpu.r.setHL((uint16_t)(addr + 1));
                }
                commit = true;
                m_opcodeEmulatedCount[0xCA]++;
                break;
            }
            case OP_LOR: { // LOR: Logical OR. Plain bitwise OR of the two
                            // popped 16-bit values (also used for set union
                            // at this single-word size). Note the pop order
                            // is HL-then-DE here, opposite of LAND's DE-then-
                            // HL -- doesn't matter for the boolean result
                            // (OR is commutative either way), but matters
                            // for which popped value survives in DE.
                uint16_t x = PopStackWord(); // first pop -> HL in the real code
                uint16_t y = PopStackWord(); // second pop -> DE in the real code
                uint16_t result = (uint16_t)(x | y);
                PushStackWord(result);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = (uint8_t)(result >> 8); // "LD A,H;OR D;LD H,A" is the last A-write
                    m_cpu.r.setDE(y); // DE is only ever read (via OR D), never written
                    m_cpu.r.setHL(result);
                }
                commit = true;
                m_opcodeEmulatedCount[0x8D]++;
                break;
            }
            case OP_STB: { // STB: Store byte.
                uint16_t charWord = PopStackWord(); // only the low byte matters
                uint16_t index = PopStackWord();
                uint16_t base = PopStackWord();
                uint16_t addr = (uint16_t)(base + index);
                uint8_t ch = (uint8_t)(charWord & 0xFF);
                m_mem[addr] = ch;
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = ch;
                    m_cpu.r.setDE(index);
                    m_cpu.r.setHL(addr);
                }
                commit = true;
                m_opcodeEmulatedCount[0xBF]++;
                break;
            }
            case OP_STP: { // STP: Store into a packed field. Given (from
                            // the P-code stack, top to bottom): data,
                            // right_bit_number, bits_per_element, ^target
                            // -- writes 'data' into a bits_per_element-wide
                            // field starting at bit position
                            // right_bit_number within the 16-bit word at
                            // ^target, leaving other bits of that word
                            // unchanged. Matches the real code's own lack
                            // of masking on 'data' itself before merging it
                            // in -- if data has bits set outside its own
                            // field width, those bits DO leak into the
                            // target word, exactly as the real hardware
                            // does (not "fixed" here). All four operands
                            // come from the P-code stack, not the code
                            // stream, so BC (IPC) is never disturbed here
                            // -- matches precedent, plain BACK not BACK1.
                uint16_t bcIpc = m_cpu.r.BC();
                m_mem[PM_V(0x0246)] = bcIpc & 0xFF; m_mem[PM_V(0x0247)] = bcIpc >> 8; // SAVIPC

                uint16_t data = PopStackWord();
                uint16_t rightBitNumberWord = PopStackWord();
                uint8_t rightBitNumber = (uint8_t)(rightBitNumberWord & 0xFF);
                uint16_t bitsPerElementWord = PopStackWord();
                uint8_t bitsPerElement = (uint8_t)(bitsPerElementWord & 0xFF);
                uint16_t targetAddr = PopStackWord();

                uint16_t mask = (uint16_t)((1u << bitsPerElement) - 1); // CLRMSK[bitsPerElement]

                uint32_t shiftedMask32 = ((uint32_t)mask) << rightBitNumber;
                uint32_t shiftedData32 = ((uint32_t)data) << rightBitNumber;
                uint16_t shiftedMask = (uint16_t)shiftedMask32; // bits past 16 are lost, matching hardware
                uint16_t shiftedData = (uint16_t)shiftedData32;

                uint16_t targetWord = (uint16_t)(m_mem[targetAddr] | (m_mem[(uint16_t)(targetAddr + 1)] << 8));
                uint16_t newWord = (uint16_t)((targetWord & (uint16_t)~shiftedMask) | shiftedData);

                m_mem[targetAddr] = newWord & 0xFF;
                m_mem[(uint16_t)(targetAddr + 1)] = newWord >> 8;

                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = (uint8_t)(newWord >> 8); // matches the final "OR D" write to the high byte
                    m_cpu.r.setDE(shiftedData);
                    m_cpu.r.setHL((uint16_t)(targetAddr + 1));
                }
                commit = true;
                m_opcodeEmulatedCount[0xBB]++;
                break;
            }
            case OP_LDA: { // LDA: Load intermediate address (lex-level >= 2)
                uint16_t addr = GetIA();
                PushStackWord(addr);
                if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(addr); // real code never touches A/DE beyond what GetIA's own GBDE call already sets
                commit = true;
                m_opcodeEmulatedCount[0xB2]++;
                break;
            }
            case OP_LDCI: { // LDCI: Load constant word -- a plain 2-byte
                            // little-endian literal read directly from the
                            // code stream, NOT GBDE's variable-length
                            // encoding.
                uint16_t bc = m_cpu.r.BC();
                uint8_t lo = PM_CODE8(bc);
                uint8_t hi = PM_CODE8((uint16_t)(bc + 1));
                m_cpu.r.setBC((uint16_t)(bc + 2)); // IPC advance past the 2-byte literal -- not a compat mirror, stays unconditional
                uint16_t value = (uint16_t)((hi << 8) | lo);
                PushStackWord(value);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = hi; // "LD A,(BC)" reading the high byte is the last A-write
                    m_cpu.r.setHL(value);
                }
                commit = true;
                m_opcodeEmulatedCount[0xC7]++;
                break;
            }
            case OP_RBP: { // RBP: Return from base procedure. Same teardown
                            // as RNP, but first restores BASE (0x02F0) and
                            // recomputes BASED0 from a word stored 2 bytes
                            // below the current MP (the previous "base
                            // environment" pointer, saved as part of the
                            // base-level MSCW), then falls straight into
                            // RNP's own logic below. Same safety pattern as
                            // RNP: the segment-change case is handled
                            // natively too, via NativeSegReturn.inc (see
                            // RNP's own comment below).
                uint16_t mpVal = (uint16_t)(m_mem[PM_V(0x02F2)] | (m_mem[PM_V(0x02F3)] << 8));
                uint16_t newBase = (uint16_t)(m_mem[(uint16_t)(mpVal - 2)] | (m_mem[(uint16_t)(mpVal - 1)] << 8));
                uint16_t newBased0FromRBP = (uint16_t)(newBase + 10); // +DISP0

                uint16_t mpd0Val = (uint16_t)(m_mem[PM_V(0x0242)] | (m_mem[PM_V(0x0243)] << 8));
                uint16_t oldSP = (uint16_t)(m_mem[mpd0Val] | (m_mem[(uint16_t)(mpd0Val + 1)] << 8));
                uint8_t numWords = PM_CODE8(m_cpu.r.BC());
                uint16_t bytesToReturn = (uint16_t)(numWords * 2);
                uint16_t srcStart = (uint16_t)(mpd0Val + 2);
                uint16_t destStart = (uint16_t)(oldSP - bytesToReturn);
                uint16_t newSP = (bytesToReturn == 0) ? oldSP : destStart;

                uint16_t framePtr = mpVal; // (MP)
                framePtr = (uint16_t)(framePtr + 2);
                uint16_t newMP = (uint16_t)(m_mem[framePtr] | (m_mem[(uint16_t)(framePtr + 1)] << 8));
                framePtr = (uint16_t)(framePtr + 2);
                uint16_t newMPD0 = (uint16_t)(newMP + 10);
                uint16_t newJTAB = (uint16_t)(m_mem[framePtr] | (m_mem[(uint16_t)(framePtr + 1)] << 8));
                framePtr = (uint16_t)(framePtr + 2);
                uint16_t newSegCandidate = (uint16_t)(m_mem[framePtr] | (m_mem[(uint16_t)(framePtr + 1)] << 8));
                framePtr = (uint16_t)(framePtr + 2);

                uint16_t curSegP = (uint16_t)(m_mem[PM_V(0x02F6)] | (m_mem[PM_V(0x02F7)] << 8));
                { // same OR different segment -- both native now
#include "NativeSegReturn.inc"
                    m_mem[PM_V(0x02F0)] = newBase & 0xFF; m_mem[PM_V(0x02F1)] = newBase >> 8; // BASE
                    m_mem[PM_V(0x0244)] = newBased0FromRBP & 0xFF; m_mem[PM_V(0x0245)] = newBased0FromRBP >> 8; // BASED0

                    if (bytesToReturn > 0) {
                        if (destStart <= srcStart) {
                            for (uint16_t i = 0; i < bytesToReturn; i++)
                                m_mem[(uint16_t)(destStart + i)] = m_mem[(uint16_t)(srcStart + i)];
                        } else {
                            for (uint16_t i = bytesToReturn; i-- > 0; )
                                m_mem[(uint16_t)(destStart + i)] = m_mem[(uint16_t)(srcStart + i)];
                        }
                    }
                    uint16_t newBC = (uint16_t)(m_mem[framePtr] | (m_mem[(uint16_t)(framePtr + 1)] << 8));

                    m_mem[PM_V(0x02F2)] = newMP & 0xFF; m_mem[PM_V(0x02F3)] = newMP >> 8;
                    m_mem[PM_V(0x0242)] = newMPD0 & 0xFF; m_mem[PM_V(0x0243)] = newMPD0 >> 8;
                    m_mem[PM_V(0x02F4)] = newJTAB & 0xFF; m_mem[PM_V(0x02F5)] = newJTAB >> 8;
                    m_mem[PM_V(0x02F6)] = newSegCandidate & 0xFF; m_mem[PM_V(0x02F7)] = newSegCandidate >> 8;

                    if (m_preserveZ80RegisterCompat) m_cpu.r.A = segRetA;
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setDE(newSegCandidate);
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(newSP);
                    m_cpu.r.SP = newSP;
                    m_cpu.r.setBC(newBC);
                    commit = true;
                    m_opcodeEmulatedCount[0xC1]++;
                }
                break;
            }
            case OP_STO: { // STO: Store indirect. Pop value, pop address, store.
                uint16_t value = PopStackWord();
                uint16_t addr = PopStackWord();
                m_mem[addr] = value & 0xFF;
                m_mem[(uint16_t)(addr + 1)] = value >> 8;
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setDE(value); // real code never modifies DE after the pop, just reads E/D
                    m_cpu.r.setHL((uint16_t)(addr + 1)); // real code never touches A here
                }
                commit = true;
                m_opcodeEmulatedCount[0x9A]++;
                break;
            }
            case OP_STIND: { // STIND: despite the name, this LOADS a word using a
                            // static (GBDE-encoded) index -- "Static index and
                            // load word", not a store.
                uint16_t base = PopStackWord();
                uint16_t index = DecodeGBDE();
                uint16_t addr = (uint16_t)(base + index * 2);
                uint16_t value = (uint16_t)(m_mem[addr] | (m_mem[(uint16_t)(addr + 1)] << 8));
                PushStackWord(value);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setDE(value); // overwritten by the load
                    m_cpu.r.setHL((uint16_t)(addr + 1));
                }
                commit = true;
                m_opcodeEmulatedCount[0xA3]++;
                break;
            }
            case OP_CSP: { // CSP: Call standard procedure. This is a pure jump
                            // table (CSPTBL at 0x15E7, one word-pointer per
                            // procedure number) -- there's no single "CSP
                            // result" to compute since it just routes to one
                            // of dozens of separate standard-library routines
                            // (IOC, NEW, MVL, MVR, EXIT, and many more), each
                            // of which remains real, unreplaced Z80 code. All
                            // that's natively shortcut here is the dispatch
                            // itself (reading the procedure number, indexing
                            // CSPTBL, jumping to the target) -- a modest,
                            // always-safe optimization, not a behavior change.
                            // Peeks the procedure number first and only
                            // commits (advances BC) if the table entry is
                            // non-zero, so the rare CSPTRAP ("unimplemented,
                            // trap into the OS") case falls through completely
                            // untouched, exactly like FJP/UJP's rare-case
                            // handling.
                uint8_t procNum = PM_CODE8(m_cpu.r.BC());
                // m_cspSelectorCount[procNum] is now counted unconditionally
                // in logPcodeAtBack() (Site 1 above), so it's tracked in Z80
                // mode too -- not incremented again here, which would
                // double-count it in native mode.

                // Native floating-point transcendental/misc functions
                // (SIN, COS, LOG, ATAN, LN, EXP, SQT -- CSPTBL selectors
                // 25-31) are computed directly here using standard C++
                // math, rather than replicating the Z80's own
                // polynomial/CORDIC-style algorithms bit-for-bit. The
                // UCSD 4-byte REAL format (see DecodeUcsdReal's own
                // comment) was reverse-engineered from FPL.TEXT's own
                // documented comment and verified bit-exact against the
                // source's own hardcoded FPCPI4 (pi/4) constant. Small
                // differences from the real Z80 code's own approximation
                // error are expected and acceptable here -- this is NOT
                // meant to be bit-exact with the original algorithm,
                // just numerically correct, matching the explicit intent
                // for this feature.
                //
                // Each of these standard procedures pops exactly one
                // 4-byte real (as two words, low word on top -- matching
                // FPMPUSH's own convention) and pushes exactly one
                // 4-byte real result, confirmed via SIN/COS/ATAN/LOG/LN/
                // EXP/SQT's own uniform "FPLCBEG; CALL FPF<x>; JP FPLCHK"
                // structure in the source (FPLCBEG only clears the error
                // flag; FPLCHK only checks it and returns via BACK1 --
                // neither touches the stack, so the pop/push convention
                // lives entirely in each FPF<x> routine, which all share
                // the same "FPFa: takes argument(s) on tos, leaves result
                // on tos" contract per the source's own naming-convention
                // comment).
                //
                // Domain errors (log/ln of <=0, sqrt of <0) and inputs
                // that would overflow the format's own exponent range
                // fall through to real Z80 code untouched (peek-only, no
                // commit) rather than trying to replicate FPIERR's own
                // bomb-the-program behavior -- these are rare, and the
                // real Z80 CSPTRAP path (for a selector this build's own
                // CSPTBL leaves at zero) is a reasonable fallback.
                // CSP 138: call through a function pointer (NativeCalli.inc)
#include "NativeCalli.inc"
                // 8-byte floating point (double): CSP 100..137 (NativeDouble.inc)
#include "NativeDouble.inc"
                if (procNum >= 25 && procNum <= 31) {
                    uint16_t lowWord  = PeekStackWord(0);
                    uint16_t highWord = PeekStackWord(2);
                    uint8_t b0 = lowWord & 0xFF, b1 = lowWord >> 8;
                    uint8_t b2 = highWord & 0xFF, b3 = highWord >> 8;
                    double x = DecodeUcsdReal(b0, b1, b2, b3);

                    bool domainOk = true;
                    if ((procNum == 27 || procNum == 29) && x <= 0.0) domainOk = false; // LOG/LN
                    if (procNum == 31 && x < 0.0) domainOk = false; // SQT

                    if (domainOk) {
                        double result;
                        switch (procNum) {
                            case 25: result = std::sin(x);   break;
                            case 26: result = std::cos(x);   break;
                            case 27: result = std::log10(x); break;
                            case 28: result = std::atan(x);  break;
                            case 29: result = std::log(x);   break;
                            case 30: result = std::exp(x);   break;
                            case 31: result = std::sqrt(x);  break;
                            default: result = 0.0; break; // unreachable (procNum already range-checked)
                        }
                        if (std::isfinite(result)) {
                            uint8_t r0, r1, r2, r3;
                            if (EncodeUcsdReal(result, r0, r1, r2, r3)) {
                                PopStackWord(); // commit: consume lowWord
                                PopStackWord(); // commit: consume highWord
                                PushStackWord((uint16_t)((r3 << 8) | r2)); // high word first, matches FPMPUSH's own order
                                PushStackWord((uint16_t)((r1 << 8) | r0)); // low word ends up on top
                                m_cpu.r.setBC((uint16_t)(m_cpu.r.BC() + 1));
                                m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF;
                                m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8;
                                m_cpu.r.PC = 0x03A4; // BACK1 -- matches FPLCHK's own success exit
                                m_opcodeEmulatedCount[OP_CSP]++;
                                break;
                            }
                        }
                    }
                    // domain error or overflow -- fall through untouched (nothing committed above)
                }

#include "NativeCsp.inc"
                uint16_t entryAddr = (uint16_t)(0x15E7 + procNum * 2);
                uint16_t target = (uint16_t)(PM_ROM(entryAddr) | (PM_ROM((uint16_t)(entryAddr + 1)) << 8)); // snapshot once reclaimed
                if (target != 0) {
                    m_cpu.r.setBC((uint16_t)(m_cpu.r.BC() + 1));
                    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF;   // SAVIPC: the target routine (or
                    m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8;     // something it calls) may read
                                                            // IPCSAV directly from memory,
                                                            // not just rely on BC itself
                    // A/DE/HL below are left unconditional, NOT gated behind
                    // m_preserveZ80RegisterCompat: unlike a plain-BACK or
                    // BACK1 commit, this jumps directly into one of dozens
                    // of still-real, unverified Z80 standard-procedure
                    // routines (IOC, NEW, MVL, MVR, EXIT, etc.) -- whether
                    // any of those depend on A/DE/HL at their own entry
                    // point hasn't been checked, so diverging here carries
                    // real risk, same reasoning as CXP's own jump into CIP.
                    m_cpu.r.A = (uint8_t)(target >> 8); // "LD A,D" (D = target's high byte) is the last A-write before the jump
                    m_cpu.r.setDE((uint16_t)(entryAddr + 1)); // "EX DE,HL" leaves DE = the old HL = entryAddr+1
                    m_cpu.r.setHL(target);
                    m_cpu.r.PC = target; // jump directly into the (still real-Z80) target routine
                    m_opcodeEmulatedCount[OP_CSP]++;
                }
                break;
            }
            case OP_LLA: { // LLA: Load local address (not the value).
                uint16_t offset = DecodeGBDE();
                uint16_t mpd0 = (uint16_t)(m_mem[PM_V(0x0242)] | (m_mem[PM_V(0x0243)] << 8));
                uint16_t addr = (uint16_t)(mpd0 + offset * 2);
                PushStackWord(addr);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setDE(offset); // NOT overwritten -- nothing pops into DE afterward, same as LAO
                    m_cpu.r.setHL(addr);
                }
                commit = true;
                m_opcodeEmulatedCount[0xC6]++;
                break;
            }
            case OP_SIND0: { // SIND0: Short index and load word, index=0 (i.e.
                            // plain load indirect -- adding a zero index is a
                            // no-op, so this is just LDL/LDO's inner load step
                            // with the address already on the stack).
                uint16_t addr = PopStackWord();
                uint16_t value = (uint16_t)(m_mem[addr] | (m_mem[(uint16_t)(addr + 1)] << 8));
                PushStackWord(value);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setDE(value); // real code never touches A here
                    m_cpu.r.setHL((uint16_t)(addr + 1));
                }
                commit = true;
                m_opcodeEmulatedCount[OP_SIND0]++;
                break;
            }
            case OP_LOD: { // LOD: Load intermediate word (lex-level >= 2 access)
                uint16_t addr = GetIA();
                uint16_t value = (uint16_t)(m_mem[addr] | (m_mem[(uint16_t)(addr + 1)] << 8));
                PushStackWord(value);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setDE(value); // overwritten by the load
                    m_cpu.r.setHL((uint16_t)(addr + 1));
                }
                commit = true;
                m_opcodeEmulatedCount[0xB6]++;
                break;
            }
            case OP_LDCN: { // LDCN: Load constant NIL pointer (0x0001)
                PushStackWord(0x0001);
                if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(0x0001); // real code never touches A or DE here
                commit = true;
                m_opcodeEmulatedCount[0x9F]++;
                break;
            }
            case OP_LDM: { // LDM: Load multiple words (no more than 255).
                            // Pops a source address, reads a count byte from
                            // the code stream, then copies 'count' words from
                            // [source, source+2, ..., source+(count-1)*2]
                            // onto the P-code stack in order (source[0] ends
                            // up on top). The real code's loop actually walks
                            // BACKWARD from the end of the source block,
                            // pushing source[count-1] first and source[0]
                            // last, which is what makes source[0] end up on
                            // top of the stack -- our loop just runs the
                            // iteration order in reverse to get the same
                            // final stack layout without needing to mimic
                            // the backward walk itself.
                            //
                            // count==0 is a real, if pathological, case the
                            // compiler is unlikely to ever emit; the real
                            // code takes an early exit ("JP Z,BACK") that
                            // never touches DE or HL at all, leaving them
                            // as whatever they were before LDM started. That
                            // can't be safely reconstructed here, so this
                            // case peeks the count first and falls through
                            // completely untouched rather than guessing.
                uint16_t bc = m_cpu.r.BC();
                uint8_t count = PM_CODE8(bc); // peek only until we've decided to commit
                if (count == 0) break; // fall through untouched
                m_cpu.r.setBC((uint16_t)(bc + 1));
                uint16_t srcAddr = PopStackWord();
                for (int i = count - 1; i >= 0; i--) {
                    uint16_t word = (uint16_t)(m_mem[(uint16_t)(srcAddr + i * 2)] |
                                                (m_mem[(uint16_t)(srcAddr + i * 2 + 1)] << 8));
                    PushStackWord(word);
                }
                uint16_t firstWord = (uint16_t)(m_mem[srcAddr] | (m_mem[(uint16_t)(srcAddr + 1)] << 8));
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = 0;              // the loop's own "DEC A" reaching zero
                    m_cpu.r.setDE(firstWord);   // last word read by the real loop = source[0]
                    m_cpu.r.setHL(srcAddr);     // real loop's HL winds back down to exactly srcAddr
                }
                commit = true;
                m_opcodeEmulatedCount[0xBC]++;
                break;
            }
            case OP_STM: { // STM: Store multiple words. Counterpart to
                            // LDM: numWords words sit on top of the
                            // P-code stack, with the destination pointer
                            // buried just below them. Pops the words
                            // (top-of-stack first) and writes them to
                            // dest[0], dest[1], ... in that same order --
                            // matching LDM's own convention where the
                            // stack top corresponds to logical index 0.
                            // The destination pointer itself is read via a
                            // direct SP-relative memory access first (NOT
                            // popped at that point -- so the transfer loop
                            // doesn't clobber it while still buried under
                            // the words above it), but a SHARED tail label
                            // ("$20: POP HL"), which both this normal path
                            // and the numWords==0 early-exit path converge
                            // into, explicitly pops and discards it
                            // afterward regardless -- and HL ends up
                            // holding that popped value (destAddr itself,
                            // unchanged), not some advanced pointer.
                            //
                            // numWords==0 is a real, if pathological,
                            // degenerate case: no words to transfer, so
                            // ^dest sits directly on top of the stack
                            // instead of being buried, and reaches that
                            // same shared "$20: POP HL" tail directly.
                uint16_t bc = m_cpu.r.BC();
                uint8_t numWords = PM_CODE8(bc);
                bc = (uint16_t)(bc + 1);
                m_cpu.r.setBC(bc);

                if (numWords == 0) {
                    PopStackWord(); // junk ^dest, explicitly discarded on this path
                    if (m_preserveZ80RegisterCompat) m_cpu.r.A = 0;
                    // DE deliberately left untouched -- real code never writes it here
                    commit = true;
                    m_opcodeEmulatedCount[0xBD]++;
                    break;
                }

                uint16_t destAddr = PeekStackWord(2 * numWords); // buried under the words-to-transfer

                uint16_t lastPopped = 0;
                for (int i = 0; i < numWords; i++) {
                    lastPopped = PopStackWord();
                    m_mem[(uint16_t)(destAddr + 2 * i)] = lastPopped & 0xFF;
                    m_mem[(uint16_t)(destAddr + 2 * i + 1)] = lastPopped >> 8;
                }
                PopStackWord(); // "$20: POP HL" is a SHARED tail both paths converge
                                 // into -- the normal loop also explicitly pops and
                                 // discards ^dest afterward, it's not just left there

                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = 0;
                    m_cpu.r.setDE(lastPopped);
                    m_cpu.r.setHL(destAddr); // HL ends up holding the POPPED ^dest value
                                              // itself (unchanged), not destAddr+2*numWords
                }
                commit = true;
                m_opcodeEmulatedCount[0xBD]++;
                break;
            }
            case OP_INN: { // INN: Set membership test ("i IN set_a"). Stack
                            // (top to bottom, before INN runs): sza (word
                            // count of set_a), set_a[0..sza-1] (the set's
                            // own bit-vector words), i (the value being
                            // tested), rest of stack. Pops the whole
                            // {sza,set_a,i} block and pushes a single
                            // boolean (0 or 1).
                            //
                            // Two sub-cases are deliberately left to fall
                            // through to real Z80 code, both because
                            // they're comparatively rare AND because the
                            // real code leaves DE undefined (stale, whatever
                            // it held before INN started) at those specific
                            // exits, which cannot be safely reconstructed:
                            //   - i is wildly out of range (negative, or
                            //     >= 4080): bombs the program via INVNDX.
                            //   - i is within that absolute range but >=
                            //     this particular set's own declared size
                            //     (sza words): returns false, but via a
                            //     path that never touches DE.
                uint16_t bcIpc = m_cpu.r.BC();
                m_mem[PM_V(0x0246)] = bcIpc & 0xFF; m_mem[PM_V(0x0247)] = bcIpc >> 8; // SAVIPC

                uint16_t sp0 = m_cpu.r.SP; // original SP, pointing at 'sza'
                uint16_t szaWord = (uint16_t)(m_mem[sp0] | (m_mem[(uint16_t)(sp0 + 1)] << 8));
                uint8_t sza = (uint8_t)(szaWord & 0xFF);
                uint16_t spAfterPop = (uint16_t)(sp0 + 2); // points at set_a[0]

                uint16_t iAddr = (uint16_t)(spAfterPop + 2 * sza); // address of 'i'
                uint16_t iVal = (uint16_t)(m_mem[iAddr] | (m_mem[(uint16_t)(iAddr + 1)] << 8));
                uint16_t restOfStackAddr = (uint16_t)(iAddr + 2); // address just past 'i'

                uint32_t sum = (uint32_t)0xF010 + (uint32_t)iVal;
                if (sum > 0xFFFF) {   // INN $99: i < 0 or i >= 4080
                    if (m_preserveZ80RegisterCompat) break;
                    m_cpu.r.SP = restOfStackAddr;                        // POP HL; LD SP,HL
                    PushStackWord(0);                                 // LD HL,0; PUSH HL
                    m_cpu.r.PC = 0x03DA;                                // INVNDX (NativeErrors.inc)
                    break;
                }

                uint8_t wordIdx   = (uint8_t)(iVal / 16);
                uint8_t bitInWord = (uint8_t)(iVal % 16);

                m_mem[PM_V(0x02A0)] = wordIdx;   // IDIV := i div 16 (the Z80 RRD writes it before the size check)
                if (wordIdx >= sza) {
                    // Element beyond the set's stored size: not in the set (Z80 INN $20:
                    // SP := ^rest of stack; push 0; BACK1). With register compatibility
                    // on it stays Z80 -- the real code leaves D holding a value from the
                    // previous instruction there.
                    if (m_preserveZ80RegisterCompat) break;
                    m_cpu.r.SP = restOfStackAddr;
                    PushStackWord(0);
                    commit = true;
                    m_opcodeEmulatedCount[0x8B]++;
                    break;
                }

                uint8_t bitInByte = (uint8_t)(bitInWord & 0x07);
                uint8_t mask = (uint8_t)(1 << bitInByte);
                uint16_t byteOff = (uint16_t)((wordIdx + 2) * 2); // "+2 words" accounts for the
                                                                    // real code's own TWO scratch
                                                                    // pushes (restOfStackAddr, then
                                                                    // the mask via PUSH AF)
                if (bitInWord & 0x08) byteOff = (uint16_t)(byteOff + 1);
                uint16_t targetAddr = (uint16_t)(byteOff + (sp0 - 2)); // real SP at "ADD HL,SP" is
                                                                        // sp0-2 (both scratch pushes
                                                                        // are still on the stack at
                                                                        // that point -- the mask isn't
                                                                        // popped until afterward)
                uint8_t testResult = (uint8_t)(mask & m_mem[targetAddr]);

                m_cpu.r.SP = restOfStackAddr;
                PushStackWord(testResult != 0 ? 1 : 0);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = testResult; // last "AND (HL)" result, real code never overwrites A after this
                    m_cpu.r.setDE(bitInByte);
                    m_cpu.r.setHL(testResult != 0 ? 1 : 0);
                }
                commit = true; // BC never disturbed, so BACK1's restore-from-IPCSAV is moot
                m_opcodeEmulatedCount[0x8B]++;
                break;
            }
            case OP_CXP: { // CXP: Call external (different segment) procedure.
                            // Only the "already in the target segment" fast
                            // path is implemented natively (a same-segment
                            // CXP -- e.g. the OS calling back into itself) --
                            // it just jumps straight to CIP (0x1369), which
                            // is itself natively dispatched (see the PC
                            // assignment below). The different-segment
                            // cases are in NativeCxp.inc: seg 0 (op sys) and
                            // already-resident segments run natively; only a
                            // non-resident segment (GETSEG -> READSEG disk
                            // read + RLSEG relocation), an assembly-language
                            // target, or an imminent STKOVR still falls
                            // through to real Z80 code, untouched.
                uint16_t bc = m_cpu.r.BC();
                uint8_t segNum = PM_CODE8(bc); // peek
                uint16_t segp = (uint16_t)(m_mem[PM_V(0x02F6)] | (m_mem[PM_V(0x02F7)] << 8));
                uint8_t curSegByte = m_mem[segp]; // SEGP dereferenced as a pointer, one byte

                if (segNum != curSegByte) { // different segment: seg 0 / resident natively, disk read stays Z80
                    const uint8_t  cxProcN = PM_CODE8((uint16_t)(bc + 1));   // operands for NativeCxp.inc (shared with CSP 138)
                    const uint16_t cxIpc1  = (uint16_t)(bc + 1);
                    const uint16_t cxIpc2  = (uint16_t)(bc + 2);
                    int cxWhy = 0;                                        // (set when declined; unused here: the Z80 code takes over)
#define CXP_POST cxPost
#include "NativeCxp.inc"
#undef CXP_POST
                    break;
                }

                m_cpu.r.setBC((uint16_t)(bc + 1));
                if (m_preserveZ80RegisterCompat) m_cpu.r.A = segNum;
                if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(segp);
                // DE deliberately left untouched here -- the real code's own
                // fast path never writes DE either, so leaving our own DE
                // register alone reproduces that "stale" value exactly,
                // since it's the same persistent register either way.
                m_cpu.r.PC = 0x1369; // CIP entry -- NativeOpcodeForTarget maps this address to the
                                     // native CIP case on the next loop iteration, so CXP's
                                     // own A/HL writes above are safe to gate (only CIP's rare
                                     // NativeBldmscw-fails path still runs real Z80 code)
                m_opcodeEmulatedCount[0xCD]++;
                break;
            }
            case OP_XJP: { // XJP: Case jump. Reads min/max (2 words) plus an
                            // else-jump slot from the code stream (rounded
                            // up to the next word boundary), pops the case
                            // index off the P-code stack, and either
                            // dispatches to the case-table entry for that
                            // index (if min<=index<=max) or falls through to
                            // the else-jump slot -- a P-code UJP instruction
                            // embedded directly in the code stream, which
                            // the normal dispatch loop executes next once BC
                            // points at it.
                            //
                            // Only the "index in range" (successful case-
                            // jump) path is handled natively. The "out of
                            // range" (else-jump) path falls through
                            // completely untouched: the real code reaches it
                            // via one of four different sign-check
                            // sub-branches (used to compare signed 16-bit
                            // values without overflow -- a problem a plain
                            // C++ signed comparison doesn't have), each
                            // leaving A with a different value, and none of
                            // that is committed here until the in-range
                            // decision below is made via a plain signed
                            // comparison, so falling through is safe.
                uint16_t bc = m_cpu.r.BC();
                bc = (uint16_t)(bc + 1);
                uint16_t tableAddr = (uint16_t)(bc & 0xFFFE); // round up to word boundary
                uint16_t minVal = PM_CODE16(tableAddr);
                uint16_t maxVal = PM_CODE16((uint16_t)(tableAddr + 2));
                uint16_t elseWordAddr = (uint16_t)(tableAddr + 4); // address of the else-jump slot itself

                uint16_t indexVal = PeekStackWord(0); // don't commit the pop until we've decided

                int16_t indexS = (int16_t)indexVal, minS = (int16_t)minVal, maxS = (int16_t)maxVal;
                if (indexS < minS || indexS > maxS) {
                    // Out of range: continue at the else-jump slot (Z80 XJP: IPCSAV :=
                    // its address; pop the index; BACK1). With register compatibility on it
                    // stays Z80 -- A/DE/HL there depend on which comparison branch exits.
                    if (m_preserveZ80RegisterCompat) break;
                    PopStackWord();
                    m_mem[PM_V(0x0246)] = elseWordAddr & 0xFF; m_mem[PM_V(0x0247)] = elseWordAddr >> 8; // IPCSAV
                    m_cpu.r.setBC(elseWordAddr);
                    commit = true;
                    m_opcodeEmulatedCount[0xAC]++;
                    break;
                }

                PopStackWord(); // commit
                uint16_t entryAddr = (uint16_t)(elseWordAddr + 2 + 2 * (uint16_t)(indexS - minS));
                uint16_t storedVal = PM_CODE16(entryAddr);
                uint16_t target = (uint16_t)(entryAddr - storedVal); // negative-self-relative decode

                m_mem[PM_V(0x0246)] = elseWordAddr & 0xFF; m_mem[PM_V(0x0247)] = elseWordAddr >> 8; // IPCSAV
                                                                                          // (real code writes this
                                                                                          // unconditionally, before
                                                                                          // any range check)

                uint8_t minLow = (uint8_t)(minVal & 0xFF);
                uint8_t indexHigh = (uint8_t)(indexVal >> 8);
                uint8_t xorVal = (uint8_t)(minLow ^ indexHigh);
                uint8_t aFinal = ((xorVal & 0x80) == 0) ? xorVal : (uint8_t)(xorVal & indexHigh);

                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = aFinal;
                    m_cpu.r.setDE((uint16_t)(2 * (uint16_t)(indexS - minS + 1)));
                    m_cpu.r.setHL(target);
                }
                m_cpu.r.setBC(target); // new IPC -- functionally required, not a compat mirror
                commit = true;
                m_opcodeEmulatedCount[0xAC]++;
                break;
            }
            case OP_STR: { // STR: Store intermediate word. Calls GetIA
                            // (the shared static-link-chasing address
                            // helper, already used elsewhere) to compute
                            // the target address, pops the value to store
                            // off the P-code stack, and writes it there
                            // (low byte, then high byte).
                uint16_t addr = GetIA(); // also advances BC, sets A and DE=0x000A as side effects
                uint16_t value = PopStackWord();
                m_mem[addr] = value & 0xFF;
                m_mem[(uint16_t)(addr + 1)] = value >> 8;
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setDE(value); // overwrites GetIA's own DE=0x000A leftover
                    m_cpu.r.setHL((uint16_t)(addr + 1)); // real code's own "INC HL" advances past the low byte
                }
                commit = true;
                m_opcodeEmulatedCount[0xB8]++;
                break;
            }
            case OP_LPA: { // LPA: Load constant packed array of characters
                            // address. The array's length (in bytes) is a
                            // byte parameter in the code stream,
                            // immediately followed by the inline character
                            // data itself (word-alignment, if any, is the
                            // COMPILER's own responsibility via padding
                            // bytes -- the interpreter itself just
                            // advances by exactly 'length' bytes). Pushes
                            // the address of the inline data (right after
                            // the length byte) and advances the IPC past
                            // the entire array.
                uint16_t bc = m_cpu.r.BC();
                uint8_t length = PM_CODE8(bc);
                bc = (uint16_t)(bc + 1);
                uint16_t dataAddr = bc; // address of the inline character data itself
                PushStackWord(dataAddr);
                bc = (uint16_t)(bc + length); // skip over the entire inline array
                m_cpu.r.setBC(bc); // IPC advance -- functionally required, not a compat mirror
                if (m_preserveZ80RegisterCompat) m_cpu.r.A = (uint8_t)(bc >> 8); // matches "LD A,0;ADC A,B;LD B,A" -- ends
                                                  // up equal to the new BC's own high byte
                // DE and HL deliberately left untouched -- real code never writes them
                commit = true;
                m_opcodeEmulatedCount[0xD0]++;
                break;
            }
            case OP_LSA: { // LSA: Load constant string address.  The string
                            // is stored inline in the code stream immediately
                            // after the opcode.  Push BC (the current IPC =
                            // address of the string), then advance BC past
                            // the string (length byte + 'length' chars).
                uint16_t ipc = m_cpu.r.BC();
                uint8_t strLen = PM_CODE8(ipc);
                // After INC BC the low byte of IPC becomes (ipc+1)&0xFF.
                // ADD A,C uses that new C; carry propagates into B via ADC.
                uint8_t cAfterInc = (uint8_t)((ipc + 1) & 0xFF);
                uint8_t bAfterInc = (uint8_t)((ipc + 1) >> 8);
                uint16_t addR  = (uint16_t)strLen + cAfterInc;
                bool    carry  = (addR >= 256);
                if (m_preserveZ80RegisterCompat) m_cpu.r.A = (uint8_t)(bAfterInc + (carry ? 1 : 0)); // ADC A,B
                PushStackWord(ipc);
                m_cpu.r.setBC((uint16_t)(ipc + 1 + strLen)); // skip len + chars -- IPC advance, not a compat mirror, stays unconditional
                // HL is not touched by LSA's own code; leave it unchanged.
                commit = true;
                m_opcodeEmulatedCount[0xA6]++;
                break;
            }
            case OP_SAS: { // SAS: String assignment.  Pops src and dst; if src
                            // is a "disguised char" (high byte zero) it first
                            // builds a 1-char string at LTSTRNG (0x024E).
                            // Reads MAXLEN from the code stream.  Falls through
                            // to Z80 (at 0x0640 = $99) only on the error path
                            // where the source is longer than MAXLEN.
                uint16_t bc = m_cpu.r.BC();
                uint8_t maxlen = PM_CODE8(bc);
                m_mem[PM_V(0x02A0)] = maxlen;                         // MAXLEN = BYTE1
                bc = (uint16_t)(bc + 1);
                m_cpu.r.setBC(bc);
                m_mem[PM_V(0x0246)] = bc & 0xFF; m_mem[PM_V(0x0247)] = bc >> 8; // SAVIPC

                uint16_t src = PopStackWord();  // ^src_string or disguised char
                uint16_t srcPtr;
                if ((src >> 8) == 0) {
                    // Disguised character -- build a 1-char string at LTSTRNG
                    m_mem[PM_V(0x024E)] = 1;                   // LTSTRNG length
                    m_mem[PM_V(0x024F)] = (uint8_t)(src & 0xFF);
                    srcPtr = PM_V(0x024E);
                } else {
                    srcPtr = src;
                }
                uint8_t srcLen = m_mem[srcPtr];   // C = source length

                if (srcLen > maxlen) {
                    // String too long -- $99 path: POP HL (junk dst) then
                    // JP S2LONG.  We've already popped src; pop dst too.
                    PopStackWord();
                    m_cpu.r.PC = 0x0420; // S2LONG (runtime error)
                    break;
                }
                uint16_t dst = PopStackWord();    // DE = ^dst_string

                // LDIR: copy srcLen+1 bytes (length byte + chars)
                uint16_t count = (uint16_t)(srcLen + 1);
                for (uint16_t i = 0; i < count; i++)
                    m_mem[(uint16_t)(dst + i)] = m_mem[(uint16_t)(srcPtr + i)];

                // After LDIR: HL = srcPtr+count, DE = dst+count, BC = 0.
                // BACK1 restores BC from IPCSAV; A = MAXLEN (from LD A,(MAXLEN)
                // at 0x0630 just before CP C -- LDIR never touches A).
                uint16_t ipcsav = (uint16_t)(m_mem[PM_V(0x0246)] | (m_mem[PM_V(0x0247)] << 8));
                m_cpu.r.setBC(ipcsav);
                if (m_preserveZ80RegisterCompat) m_cpu.r.setHL((uint16_t)(srcPtr + count));
                if (m_preserveZ80RegisterCompat) m_cpu.r.setDE((uint16_t)(dst    + count));
                if (m_preserveZ80RegisterCompat) m_cpu.r.A = maxlen;
                commit = true;
                m_opcodeEmulatedCount[0xAA]++;
                break;
            }
            case OP_LDP: { // LDP: Load a packed field.  Three stack words
                            // (pushed by the preceding IXP opcode):
                            //   top:   right_bit_number
                            //   next:  bits_per_element
                            //   next:  ^word containing the packed field
                            // Extracts the field and pushes the result.
                m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; // SAVIPC
                m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8;

                uint8_t rightBits = (uint8_t)(PopStackWord() & 0xFF); // B=E
                uint8_t bitsPerE  = (uint8_t)(PopStackWord() & 0xFF); // C=E
                uint16_t addr     = PopStackWord();

                // Load the 16-bit word at addr (little-endian): E=low, D=high
                uint8_t e = m_mem[addr];
                uint8_t d = m_mem[(uint16_t)(addr + 1)];

                // Shift DE right by rightBits positions.
                // Z80 code: if shift >= 8 swap bytes (LH trick) then SRL/RR loop.
                uint8_t shift = rightBits;
                if (shift >= 8) {
                    uint8_t tmp = d; d = e; e = tmp; // swap bytes
                    shift = (uint8_t)(shift - 8);
                }
                for (uint8_t s = 0; s < shift; s++) {
                    uint8_t bit = d & 1;             // SRL D: bit0 is the carry
                    d >>= 1;
                    e = (uint8_t)((e >> 1) | (bit << 7)); // RR E
                }

                // Apply CLRMSK[bitsPerElement] to clear the high junk bits.
                // CLRMSK[n] = (1<<n)-1; table at 0x06E8 in ROM (verified).
                // We compute it directly instead of reading from ROM.
                uint16_t mask = (bitsPerE >= 16) ? 0xFFFFu
                                                  : (uint16_t)((1u << bitsPerE) - 1u);
                e = (uint8_t)(e & (mask & 0xFF));
                d = (uint8_t)(d & (mask >> 8));
                uint16_t result = (uint16_t)((d << 8) | e);

                PushStackWord(result);

                // Register fidelity.  At BACK (via BACK1):
                //   BC = IPCSAV (restored)
                //   DE = result (d in D, e in E)
                //   HL = CLRMSK + bitsPerE*2 + 1  (final HL from mask lookup)
                //   A  = d  (from "AND D; LD D,A" at 0x069E)
                uint16_t ipcsav = (uint16_t)(m_mem[PM_V(0x0246)] | (m_mem[PM_V(0x0247)] << 8));
                m_cpu.r.setBC(ipcsav); // restore -- redundant here since this native path never
                                        // touches BC itself, but kept unconditional for clarity/safety
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setDE(result);
                    m_cpu.r.setHL((uint16_t)(0x06E8 + (uint16_t)bitsPerE * 2 + 1));
                    m_cpu.r.A = d;
                }
                commit = true;
                m_opcodeEmulatedCount[0xBA]++;
                break;
            }
            case OP_CLP: { // CLP: Call local procedure.  BLDMSCW sets up the
                            // new frame; CLP then jumps directly to BACK.
                if (!NativeBldmscw()) break; // fall through for assembly procs
                // At JP BACK: HL = RETADR = 0x133A (address of CLP's own JP
                // BACK instruction, stored in RETADR by BLDMSCW's POP HL).
                if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(0x133A);
                commit = true;
                m_opcodeEmulatedCount[0xCE]++;
                break;
            }
            case OP_CBP: { // CBP: Call base procedure.  Same BLDMSCW setup as
                            // CLP, then the CBPXNL epilogue updates BASE/BASED0
                            // and overwrites the new frame's static link with
                            // the inherited static link of the old base proc.
                uint16_t oldBASE = (uint16_t)(m_mem[PM_V(0x02F0)] | (m_mem[PM_V(0x02F1)] << 8));
                if (!NativeBldmscw()) break;

                // CBPXNL (0x134B): read AFTER NativeBldmscw so MP/MPD0 are fresh
                uint16_t newMP   = (uint16_t)(m_mem[PM_V(0x02F2)] | (m_mem[PM_V(0x02F3)] << 8));
                uint16_t newMPD0 = (uint16_t)(m_mem[PM_V(0x0242)] | (m_mem[PM_V(0x0243)] << 8));
                uint16_t entryBC = m_cpu.r.BC(); // save entry address

                // PUSH old BASE (persists at SP = newMP-2 for RBP to recover)
                PushStackWord(oldBASE);

                // Update BASED0 and BASE
                m_mem[PM_V(0x0244)] = newMPD0 & 0xFF; m_mem[PM_V(0x0245)] = newMPD0 >> 8; // BASED0
                m_mem[PM_V(0x02F0)] = newMP   & 0xFF; m_mem[PM_V(0x02F1)] = newMP   >> 8; // BASE

                // Overwrite msstat[MP] with old BASE's own static link
                // (the inherited static chain for base-procedure calls).
                uint16_t oldStatLink = (uint16_t)(m_mem[oldBASE] | (m_mem[(uint16_t)(oldBASE+1)] << 8));
                m_mem[newMP]                    = oldStatLink & 0xFF;
                m_mem[(uint16_t)(newMP + 1)]    = oldStatLink >> 8;

                // At JP BACK: HL = newMP+1 (from CBPXNL's INC HL at 0x1363),
                // DE = oldBASE+1 (EX DE,HL at 0x1361 left HL=oldBASE+1 in DE)
                m_cpu.r.setBC(entryBC);
                if (m_preserveZ80RegisterCompat) m_cpu.r.setHL((uint16_t)(newMP + 1));
                if (m_preserveZ80RegisterCompat) m_cpu.r.setDE((uint16_t)(oldBASE + 1));
                // A unchanged from NativeBldmscw (high byte of entry addr calc)
                commit = true;
                m_opcodeEmulatedCount[0xC2]++;
                break;
            }
            case OP_CIP: { // CIP: Call intermediate procedure. Same BLDMSCW
                            // setup as CLP/CBP, but then fixes up the new
                            // frame's static link (msstat) to correctly point
                            // at the enclosing lexical scope, which may be
                            // several DYNAMIC frames up from the immediate
                            // caller (unlike CLP, which assumes the called
                            // procedure is a direct lexical child of its
                            // caller).
                            //
                            // If the called procedure is itself base-level
                            // (lex level 0), this falls into the SAME
                            // "CBPXNL" logic CBP uses. Otherwise, walks the
                            // dynamic call chain (via each frame's own
                            // msdyn) looking for a frame whose OWN CALLER
                            // was at lex level (calledLexLevel-1) -- that
                            // frame's own msdyn (read in the SAME step as
                            // the comparison) is the correct static parent.
                uint16_t oldMP = (uint16_t)(m_mem[PM_V(0x02F2)] | (m_mem[PM_V(0x02F3)] << 8)); // capture before BLDMSCW overwrites it
                if (!NativeBldmscw()) break;

                uint16_t newProcEntryAddr = m_cpu.r.BC();
                uint16_t newMP   = (uint16_t)(m_mem[PM_V(0x02F2)] | (m_mem[PM_V(0x02F3)] << 8));
                uint16_t newJtab = (uint16_t)(m_mem[PM_V(0x02F4)] | (m_mem[PM_V(0x02F5)] << 8));
                uint8_t calledLexLevel = PM_CODE8((uint16_t)(newJtab + 1));

                if (((uint8_t)(calledLexLevel - 1) & 0x80) != 0) { // CIPXNL: DEC A; JP P -- base-level proc
                    // Base-level called procedure: identical CBPXNL logic to CBP.
                    uint16_t oldBASE = (uint16_t)(m_mem[PM_V(0x02F0)] | (m_mem[PM_V(0x02F1)] << 8));
                    uint16_t newMPD0 = (uint16_t)(m_mem[PM_V(0x0242)] | (m_mem[PM_V(0x0243)] << 8));

                    PushStackWord(oldBASE);
                    m_mem[PM_V(0x0244)] = newMPD0 & 0xFF; m_mem[PM_V(0x0245)] = newMPD0 >> 8; // BASED0
                    m_mem[PM_V(0x02F0)] = newMP   & 0xFF; m_mem[PM_V(0x02F1)] = newMP   >> 8; // BASE

                    uint16_t oldStatLink = (uint16_t)(m_mem[oldBASE] | (m_mem[(uint16_t)(oldBASE + 1)] << 8));
                    m_mem[newMP] = oldStatLink & 0xFF; m_mem[(uint16_t)(newMP + 1)] = oldStatLink >> 8;

                    m_cpu.r.setBC(newProcEntryAddr);
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setHL((uint16_t)(newMP + 1));
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setDE((uint16_t)(oldBASE + 1));
                    if (m_preserveZ80RegisterCompat) m_cpu.r.A = (uint8_t)(calledLexLevel - 1); // CIPXNL left A = lex-1 (0xFF for lex 0) before JP CBPXNL
                    // (A: see the CIPXNL line just above)
                } else {
                    uint8_t targetLexLevel = (uint8_t)(calledLexLevel - 1);
                    uint16_t examineAddr = newMP; // BC := ^new MSCW to start
                    uint16_t result = 0;
                    bool found = false;
                    for (int iter = 0; iter < 30000; iter++) { // defensive cap; real code has none.
                                                              // This walks the DYNAMIC chain, so it
                                                              // runs once per active frame: recursion
                                                              // depth, not lexical nesting. A 64K
                                                              // stack holds < 5500 frames, so 30000
                                                              // is never reached (64 was: repro/DEEPCXP)
                        uint16_t frameJtab = (uint16_t)(m_mem[(uint16_t)(examineAddr + 4)] | (m_mem[(uint16_t)(examineAddr + 5)] << 8));
                        uint16_t frameDyn  = (uint16_t)(m_mem[(uint16_t)(examineAddr + 2)] | (m_mem[(uint16_t)(examineAddr + 3)] << 8));
                        result = frameDyn; // BC gets updated to msdyn WITHIN this same iteration
                        uint8_t creatorLexLevel = PM_CODE8((uint16_t)(frameJtab + 1));
                        if (creatorLexLevel == targetLexLevel) { found = true; break; }
                        examineAddr = frameDyn;
                    }
                    if (!found) {
                        // Should never happen for valid compiler-generated code.
                        // BLDMSCW's own side effects are already committed at
                        // this point, so resume the search in real Z80 code
                        // exactly where our own loop left off.
                        // CIPXNL's $10 loop expects the IPC it pushed (PUSH BC at
                        // 136C) on the stack and A = the target lex level (CP (HL)).
                        PushStackWord(newProcEntryAddr);
                        m_cpu.r.A = targetLexLevel;
                        m_cpu.r.setBC(examineAddr);
                        m_cpu.r.PC = 0x137F;
                        break;
                    }
                    m_mem[newMP] = result & 0xFF; m_mem[(uint16_t)(newMP + 1)] = result >> 8; // replace msstat
                    if (m_preserveZ80RegisterCompat) m_cpu.r.A = targetLexLevel; // unchanged since the "DEC A" near the start
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setDE(newProcEntryAddr);
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(oldMP); // old (pre-CIP) msstat value, just popped/discarded
                    m_cpu.r.setBC(newProcEntryAddr);
                }
                commit = true;
                m_opcodeEmulatedCount[0xAE]++;
                break;
            }
            case OP_CGP: { // CGP: Call global procedure. Same BLDMSCW
                            // setup as CLP/CBP/CIP, but simply replaces the
                            // new frame's static link (msstat) with the
                            // CURRENT value of BASE -- global procedures
                            // (lex level 1, declared directly at the
                            // outermost/global scope) always have the main
                            // program's own base frame as their static
                            // parent, regardless of who calls them, so no
                            // dynamic-chain walk (like CIP) or BASE/BASED0
                            // update (like CBP) is needed here.
                            //
                            // The real code does "POP HL;...;PUSH HL" on
                            // the msstat slot, but since that's a net-zero
                            // change to SP (pop then push the same size),
                            // it's equivalent to directly overwriting that
                            // memory location in place -- same reasoning
                            // already used for CBP/CIP's own msstat fixups.
                if (!NativeBldmscw()) break; // fall through for assembly-proc/stack-overflow

                uint16_t newMP = (uint16_t)(m_mem[PM_V(0x02F2)] | (m_mem[PM_V(0x02F3)] << 8));
                uint16_t curBase = (uint16_t)(m_mem[PM_V(0x02F0)] | (m_mem[PM_V(0x02F1)] << 8));

                m_mem[newMP] = curBase & 0xFF; m_mem[(uint16_t)(newMP + 1)] = curBase >> 8; // replace msstat

                if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(curBase);
                // A and DE deliberately left as whatever NativeBldmscw() set
                // them to -- the real code never touches them after BLDMSCW
                // returns (matches CBP/CIP's own "A unchanged" fidelity).
                commit = true;
                m_opcodeEmulatedCount[0xCF]++;
                break;
            }
            case OP_DIF: { // DIF: Set difference. AND (NOT set_b) into
                            // set_a in place. Unlike UNI, this operation is
                            // fully symmetric and safe regardless of which
                            // set is larger -- it only ever needs to touch
                            // the first min(sza,szb) words of set_a, since
                            // beyond that point either set_b has no more
                            // bits to subtract (ANDing with all-ones
                            // leaves set_a unchanged there) or set_a has no
                            // more words to modify at all. So the FULL
                            // operation is implemented natively, with no
                            // deferred sub-case.
                            //
                            // Stack layout before DIF runs: szb, set_b (szb
                            // words), sza, set_a (sza words), rest of
                            // stack. After DIF: sza (unchanged size
                            // prefix), set_a (now differenced), rest of
                            // stack -- set_b has been fully consumed.
                uint16_t sp = m_cpu.r.SP;
                uint16_t szbWord = PeekStackWord(0);
                uint8_t szb = (uint8_t)(szbWord & 0xFF);
                uint16_t szaAddr = (uint16_t)(sp + 2 * (1 + szb));
                uint16_t szaWord = (uint16_t)(m_mem[szaAddr] | (m_mem[(uint16_t)(szaAddr + 1)] << 8));
                uint8_t sza = (uint8_t)(szaWord & 0xFF);

                m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC (SETUP's own)

                uint8_t minCount = (sza < szb) ? sza : szb;
                uint16_t curSp = (uint16_t)(sp + 2); // skip past szb's own word
                uint16_t setAWordAddr = (uint16_t)(szaAddr + 2); // start of set_a's own data
                uint8_t aFinal = 0; // matches "LD A,B" when minCount==0 (A stays 0)
                for (uint8_t i = 0; i < minCount; i++) {
                    uint16_t setBWord = (uint16_t)(m_mem[curSp] | (m_mem[(uint16_t)(curSp + 1)] << 8));
                    curSp = (uint16_t)(curSp + 2);
                    uint8_t lowResult  = (uint8_t)((~setBWord & 0xFF) & m_mem[setAWordAddr]);
                    m_mem[setAWordAddr] = lowResult;
                    uint8_t highResult = (uint8_t)((~(setBWord >> 8) & 0xFF) & m_mem[(uint16_t)(setAWordAddr + 1)]);
                    m_mem[(uint16_t)(setAWordAddr + 1)] = highResult;
                    setAWordAddr = (uint16_t)(setAWordAddr + 2);
                    aFinal = highResult; // last write to A in the real loop is the high-byte AND result
                    if (i == (uint8_t)(minCount - 1) && m_preserveZ80RegisterCompat) m_cpu.r.setDE(setBWord); // last word popped from set_b
                }

                m_cpu.r.SP = szaAddr; // matches "LD HL,(NEWSP);LD SP,HL" -- functionally required stack collapse
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = aFinal;
                    m_cpu.r.setHL(szaAddr);
                }
                // DE deliberately left untouched when minCount==0 -- the real code
                // never writes DE on that path either.
                commit = true; // BC never disturbed beyond the SAVIPC write, matches precedent
                m_opcodeEmulatedCount[0x85]++;
                break;
            }
            case OP_UNI: { // UNI: Set union. Only the "Uniona" sub-case
                            // (sza >= szb) is implemented natively -- it
                            // directly OR's set_b's words into set_a's own
                            // memory in place, leaving set_a (with its own
                            // size prefix, unchanged) as the result. The
                            // "Unionb" sub-case (szb > sza) is substantially
                            // more involved -- it unions set_a INTO set_b,
                            // then has to shift the (larger) result up to a
                            // newly-created top of stack via a second block-
                            // move loop -- and is deliberately left as real
                            // Z80 code given that added complexity.
                            //
                            // Stack layout before UNI runs: szb, set_b (szb
                            // words), sza, set_a (sza words), rest of stack.
                            // After UNI (Uniona path): sza (unchanged size
                            // prefix), set_a (now unioned), rest of stack --
                            // set_b has been fully consumed.
                uint16_t sp = m_cpu.r.SP;
                uint16_t szbWord = PeekStackWord(0);
                uint8_t szb = (uint8_t)(szbWord & 0xFF);
                uint16_t szaAddr = (uint16_t)(sp + 2 * (1 + szb));
                uint16_t szaWord = (uint16_t)(m_mem[szaAddr] | (m_mem[(uint16_t)(szaAddr + 1)] << 8));
                uint8_t sza = (uint8_t)(szaWord & 0xFF);

                if (sza < szb) {   // Unionb: native with register compatibility off
                    if (PM_PRESERVE) break;
#include "NativeUnionb.inc"
                    break;
                }

                m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC (SETUP's own)

                uint16_t curSp = (uint16_t)(sp + 2); // skip past szb's own word
                uint16_t setAWordAddr = (uint16_t)(szaAddr + 2); // start of set_a's own data
                uint8_t aFinal = 0; // matches "LD A,B" when szb==0 (A stays 0)
                for (uint8_t i = 0; i < szb; i++) {
                    uint16_t setBWord = (uint16_t)(m_mem[curSp] | (m_mem[(uint16_t)(curSp + 1)] << 8));
                    curSp = (uint16_t)(curSp + 2);
                    uint8_t lowResult  = (uint8_t)((setBWord & 0xFF) | m_mem[setAWordAddr]);
                    m_mem[setAWordAddr] = lowResult;
                    uint8_t highResult = (uint8_t)((setBWord >> 8) | m_mem[(uint16_t)(setAWordAddr + 1)]);
                    m_mem[(uint16_t)(setAWordAddr + 1)] = highResult;
                    setAWordAddr = (uint16_t)(setAWordAddr + 2);
                    aFinal = highResult; // last write to A in the real loop is the high-byte OR result
                    if (i == (uint8_t)(szb - 1) && m_preserveZ80RegisterCompat) m_cpu.r.setDE(setBWord); // last word popped from set_b
                }

                m_cpu.r.SP = szaAddr; // matches "LD HL,(NEWSP);LD SP,HL" -- functionally required stack collapse
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = aFinal;
                    m_cpu.r.setHL(szaAddr);
                }
                // DE deliberately left untouched when szb==0 -- the real code
                // never writes DE on that path either.
                commit = true; // BC never disturbed beyond the SAVIPC write, matches precedent
                m_opcodeEmulatedCount[0x9C]++;
                break;
            }
            case OP_SGS: { // SGS: Build a singleton set, the set [i]. The
                            // real code literally pops i, pushes it twice,
                            // and falls straight through into SRS's own
                            // code with no jump at all -- SGS([i]) IS
                            // SRS([i,i]). Reuses the shared NativeSrs
                            // helper directly with j=i, guaranteeing
                            // identical behavior to that fall-through
                            // rather than a hand-simplified (and
                            // potentially divergent) special case.
                            //
                            // Bounds check via peek (no side effects) so
                            // an out-of-range i falls through completely
                            // untouched -- NativeSrs itself assumes an
                            // already-valid range and is only called once
                            // the pop has committed.
                uint16_t i = PeekStackWord(0);
                if (((int16_t)i) < 0) {   // SRS $99
                    if (m_preserveZ80RegisterCompat) break;
                    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC
                    m_mem[m_cpu.r.SP] = 0; m_mem[(uint16_t)(m_cpu.r.SP + 1)] = 0;   // i and j gone; the null set's size 0 pushed
                    m_cpu.r.PC = 0x03DA;                                // INVNDX (NativeErrors.inc)
                    break;
                }
                uint32_t boundCheck = (uint32_t)0xF010 + (uint32_t)i; // j==i here
                if (boundCheck > 0xFFFF) {   // SRS $99
                    if (m_preserveZ80RegisterCompat) break;
                    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC
                    m_mem[m_cpu.r.SP] = 0; m_mem[(uint16_t)(m_cpu.r.SP + 1)] = 0;   // i and j gone; the null set's size 0 pushed
                    m_cpu.r.PC = 0x03DA;                                // INVNDX (NativeErrors.inc)
                    break;
                }
                PopStackWord(); // commit
                NativeSrs(i, i);
                m_opcodeEmulatedCount[0x97]++;
                break;
            }
            case OP_SRS: { // SRS: Build a subrange set, the set [i..j].
                            // Delegates to the shared NativeSrs helper
                            // (also used by SGS above) once the range is
                            // confirmed valid via peek. See NativeSrs's
                            // own comment in the header for the full
                            // derivation: each word of the resulting set
                            // is computed directly rather than simulating
                            // the real code's own RRD-based div-16 tricks
                            // and byte-pushing loops, verified against a
                            // direct trace of the real algorithm's own
                            // pseudocode across 500+ random (i,j) pairs.
                uint16_t j = PeekStackWord(0);
                uint16_t i = PeekStackWord(2);
                if (((int16_t)i) < 0) {   // SRS $99
                    if (m_preserveZ80RegisterCompat) break;
                    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC
                    PopStackWord();                                   // j
                    m_mem[m_cpu.r.SP] = 0; m_mem[(uint16_t)(m_cpu.r.SP + 1)] = 0;   // i and j gone; the null set's size 0 pushed
                    m_cpu.r.PC = 0x03DA;                                // INVNDX (NativeErrors.inc)
                    break;
                }
                uint32_t boundCheck = (uint32_t)0xF010 + (uint32_t)j;
                if (boundCheck > 0xFFFF) {   // SRS $99
                    if (m_preserveZ80RegisterCompat) break;
                    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC
                    PopStackWord();                                   // j
                    m_mem[m_cpu.r.SP] = 0; m_mem[(uint16_t)(m_cpu.r.SP + 1)] = 0;   // i and j gone; the null set's size 0 pushed
                    m_cpu.r.PC = 0x03DA;                                // INVNDX (NativeErrors.inc)
                    break;
                }
                PopStackWord(); // j
                PopStackWord(); // i
                NativeSrs(i, j);
                m_opcodeEmulatedCount[0x94]++;
                break;
            }
            case OP_IXP: { // IXP: Index a packed array. Given
                            // elements_per_word and bits_per_element (from
                            // the code stream), plus index and the array's
                            // base address (from the P-code stack),
                            // computes and pushes right_bit_number,
                            // bits_per_element, ^indexed_word -- the same
                            // stack layout LDP/STP expect to consume next.
                            //
                            // BC ends up holding the array's own base
                            // address (repurposed as scratch, then
                            // overwritten by POP BC partway through), NOT
                            // the original IPC -- must commit via BACK1,
                            // which restores BC from IPCSAV (written below,
                            // using a local variable, unconditionally)
                            // rather than from whatever this native path
                            // leaves in the register itself -- so, as with
                            // MPI, this path's own BC/A/DE/HL writes don't
                            // affect correctness and are gated behind
                            // m_preserveZ80RegisterCompat.
                uint16_t bc = m_cpu.r.BC();
                uint8_t elementsPerWord = PM_CODE8(bc);
                bc = (uint16_t)(bc + 1);
                uint8_t bitsPerElement = PM_CODE8(bc);
                bc = (uint16_t)(bc + 1);
                if (m_preserveZ80RegisterCompat) m_cpu.r.setBC(bc); // purely cosmetic: SAVIPC below
                                                  // uses the local bc directly, and BACK1 restores
                                                  // BC from IPCSAV regardless of the register itself
                m_mem[PM_V(0x0246)] = bc & 0xFF; m_mem[PM_V(0x0247)] = bc >> 8; // SAVIPC

                uint16_t index = PopStackWord();
                uint16_t quotient  = (elementsPerWord != 0) ? (uint16_t)(index / elementsPerWord) : 0;
                uint16_t remainder = (elementsPerWord != 0) ? (uint16_t)(index % elementsPerWord) : index;

                uint16_t baseAddr = PopStackWord();
                uint16_t indexedWordAddr = (uint16_t)(baseAddr + 2 * quotient);
                PushStackWord(indexedWordAddr);
                PushStackWord(bitsPerElement);

                // right_bit_number := remainder * bitsPerElement, matching
                // the real code's own 8-bit wraparound (repeated "ADD A,E"
                // DJNZ loop) exactly, including its DJNZ-with-zero quirk
                // (256 iterations, not 0, if bitsPerElement==0 -- though
                // this should never occur for a valid packed array type).
                uint32_t reps = (bitsPerElement == 0) ? 256u : (uint32_t)bitsPerElement;
                uint8_t rightBitNumber = 0;
                for (uint32_t k = 0; k < reps; k++)
                    rightBitNumber = (uint8_t)(rightBitNumber + (remainder & 0xFF));
                PushStackWord(rightBitNumber);

                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = rightBitNumber;
                    m_cpu.r.setDE(remainder);
                    m_cpu.r.setHL(rightBitNumber);
                }
                m_cpu.r.PC = 0x03A4; // BACK1 -- restores BC from IPCSAV (written above, unconditionally)
                m_opcodeEmulatedCount[0xC0]++;
                break;
            }
            case OP_ADJ: { // ADJ: Adjust the size of a SET value on top of
                            // the P-code stack (grow with zero-fill, or
                            // shrink/truncate), used when assigning a set
                            // of one declared size to a variable of a
                            // different declared size.
                uint16_t bc = m_cpu.r.BC();
                uint8_t szFinalW = PM_CODE8(bc); // szfinal, in words
                bc = (uint16_t)(bc + 1);
                m_cpu.r.setBC(bc);
                uint16_t szOrigW = PopStackWord(); // szorig, in words (popped)

                uint16_t szFinalB = (uint16_t)(szFinalW * 2);
                uint16_t szOrigB  = (uint16_t)(szOrigW * 2);

                if (szOrigB == szFinalB) {
                    // No-op, but NOT actually untouched: DE gets set to
                    // szFinalB via an earlier "EX DE,HL" (before the pop),
                    // and HL ends up holding the subtraction result
                    // (szOrigB-szFinalB), which is exactly zero here --
                    // neither register is left stale from before ADJ.
                    if (m_preserveZ80RegisterCompat) {
                        m_cpu.r.A = (uint8_t)(szFinalW & 0xFF);
                        m_cpu.r.setDE(szFinalB);
                        m_cpu.r.setHL(0);
                    }
                    commit = true;
                    m_opcodeEmulatedCount[0xA0]++;
                    break;
                }

                m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC

                uint16_t spOld = m_cpu.r.SP;

                if (szOrigB > szFinalB) {
                    // Crunch: keep only the first szFinalB bytes (at spOld),
                    // shift them up to end at spOld+szOrigB-1, discard the
                    // rest, shrink SP.
                    uint16_t dst = (uint16_t)(spOld + szOrigB - 1);
                    uint16_t src = (uint16_t)(spOld + szFinalB - 1);
                    for (uint16_t i = 0; i < szFinalB; i++) // backward copy, matches LDDR's own direction
                        m_mem[(uint16_t)(dst - i)] = m_mem[(uint16_t)(src - i)];
                    uint16_t newSp = (uint16_t)(spOld + szOrigB - szFinalB);
                    m_cpu.r.SP = newSp; // functionally required (real stack shrink)
                    if (m_preserveZ80RegisterCompat) {
                        m_cpu.r.A = (uint8_t)(szFinalW & 0xFF);
                        m_cpu.r.setDE((uint16_t)(spOld - 1));
                        m_cpu.r.setHL(newSp);
                    }
                } else {
                    // Expand: grow SP downward by (szFinalB-szOrigB) bytes,
                    // copy the original szOrigB bytes to the new SP,
                    // zero-fill the rest.
                    uint16_t newSp = (uint16_t)(spOld - (szFinalB - szOrigB));
                    m_cpu.r.SP = newSp; // functionally required (real stack grow)
                    for (uint16_t i = 0; i < szOrigB; i++)
                        m_mem[(uint16_t)(newSp + i)] = m_mem[(uint16_t)(spOld + i)];
                    for (uint16_t i = szOrigB; i < szFinalB; i++)
                        m_mem[(uint16_t)(newSp + i)] = 0;
                    if (m_preserveZ80RegisterCompat) {
                        m_cpu.r.A = 0x00;
                        m_cpu.r.setDE((uint16_t)(spOld + szOrigB));
                        m_cpu.r.setHL((uint16_t)(spOld + szOrigB - 1));
                    }
                }

                commit = true; // BC never disturbed beyond the SAVIPC write, matches precedent
                m_opcodeEmulatedCount[0xA0]++;
                break;
            }
            case OP_MOV: { // MOV: Move words. Copies numWords words (via
                            // GBDE's variable-length encoding) from a
                            // source address to a destination address,
                            // both popped from the P-code stack (source on
                            // top, dest below it). Matches LDIR's own
                            // forward-copy direction exactly (source and
                            // dest both advance toward higher addresses),
                            // which matters for overlapping regions -- and
                            // also its numWords==0 wraparound quirk: LDIR
                            // with BC=0 at entry doesn't copy zero bytes,
                            // it wraps and copies 65536 (DEC BC underflows
                            // to 0xFFFF, which is nonzero, so the P/V-flag
                            // based loop keeps going).
                uint16_t numWords = DecodeGBDE(); // also advances BC (IPC) and
                                                    // sets A as a side effect

                m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC

                uint16_t byteCount = (uint16_t)(numWords * 2); // matches the real
                                                                 // code's own "double DE"
                uint16_t source = PopStackWord();
                uint16_t dest   = PopStackWord();

                uint32_t iterations = (byteCount == 0) ? 65536u : (uint32_t)byteCount;
                for (uint32_t i = 0; i < iterations; i++)
                    m_mem[(uint16_t)(dest + i)] = m_mem[(uint16_t)(source + i)];

                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = (uint8_t)(byteCount >> 8); // matches "LD A,D;ADC A,D" --
                                                             // A ends up as B's own value,
                                                             // the byte count's high byte
                                                             // (LDIR itself never touches A)
                    m_cpu.r.setDE((uint16_t)(dest + byteCount));
                    m_cpu.r.setHL((uint16_t)(source + byteCount));
                }
                commit = true; // BC never disturbed beyond the SAVIPC write, matches precedent
                m_opcodeEmulatedCount[0xA8]++;
                break;
            }
            case OP_LDC: { // LDC: Load multiple-word constant. The
                            // constant's words are stored inline in the
                            // code stream (starting at the next word
                            // boundary after the count byte), and get
                            // pushed onto the P-code stack in FORWARD order
                            // (word[0] first, word[last] ends up on top) --
                            // the compiler stores them "backwards" in the
                            // code stream itself so this forward push
                            // produces the expected on-stack order for
                            // whatever consumes it. Used for multi-word
                            // values like sets and reals.
                uint16_t bc = m_cpu.r.BC();
                uint8_t numWords = PM_CODE8(bc); // count byte -- note: the real code
                                                // does not explicitly advance BC past
                                                // this byte; the word-boundary
                                                // rounding below accounts for it.
                uint16_t hl = (uint16_t)(bc + 2);
                hl = (uint16_t)(hl & 0xFFFE); // net effect: rounds BC+1 UP to the
                                                // next word boundary, where the
                                                // inline data starts
                uint8_t aFinal = (uint8_t)(hl & 0xFF); // "LD A,L;AND 0FEH;LD L,A" --
                                                         // A ends up holding this same
                                                         // rounded low byte, and is never
                                                         // touched again (NOT numWords,
                                                         // despite the earlier "LD A,(BC)")
                uint16_t de = 0;
                uint8_t b = numWords;
                do { // matches DJNZ's own "always run once, then check" semantics
                    de = PM_CODE16(hl);
                    hl = (uint16_t)(hl + 2);
                    PushStackWord(de);
                    b = (uint8_t)(b - 1);
                } while (b != 0);

                m_cpu.r.setBC(hl); // fix up IPC to skip past the entire inline constant -- functionally required
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = aFinal;
                    m_cpu.r.setDE(de);
                    m_cpu.r.setHL(hl); // same final value as BC
                }
                commit = true;
                m_opcodeEmulatedCount[0xB3]++;
                break;
            }
            case OP_DVI: { // DVI: Integer divide ("i DIV j"). Identical to
                            // MODI in every respect (same DIVD algorithm,
                            // same four sign-combination paths, same
                            // DIVZER error case) except it pushes the
                            // quotient (DE) instead of the remainder (HL)
                            // -- see MODI's own comment below for the full
                            // derivation and worked-example verification.
                uint16_t divisorPeek = PeekStackWord(0);
                if (divisorPeek == 0 || divisorPeek == 0x8000) {   // DIVZER
                    if (m_preserveZ80RegisterCompat) break;                                   // compat: the Z80 routine
                    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC
                    PopStackWord();                                   // divisor; the dividend stays (DIVD $99)
                    m_cpu.r.PC = 0x03F9;                                // DIVZER (NativeErrors.inc)
                    break;
                }

                uint16_t bcIpc = m_cpu.r.BC();
                m_mem[PM_V(0x0246)] = bcIpc & 0xFF; m_mem[PM_V(0x0247)] = bcIpc >> 8; // SAVIPC

                uint16_t divisor  = PopStackWord();
                uint16_t dividend = PopStackWord();
                bool divisorNeg  = ((int16_t)divisor)  < 0;
                bool dividendNeg = ((int16_t)dividend) < 0;

                auto sbc8 = [](uint8_t a, uint8_t b, bool borrowIn, bool& borrowOut) -> uint8_t {
                    int diff = (int)a - (int)b - (borrowIn ? 1 : 0);
                    borrowOut = (diff < 0);
                    return (uint8_t)diff;
                };

                uint16_t deQuot, hlRem;
                uint8_t aFinal;

                if (!divisorNeg && !dividendNeg) {
                    deQuot = (uint16_t)(dividend / divisor);
                    hlRem  = (uint16_t)(dividend % divisor);
                    aFinal = 0;
                } else if (!divisorNeg && dividendNeg) {
                    uint16_t hlIn = (uint16_t)(~dividend);
                    uint16_t q = (uint16_t)(hlIn / divisor);
                    uint16_t r = (uint16_t)(hlIn % divisor);
                    deQuot = (uint16_t)(~q);
                    bool borrow;
                    uint8_t lo = sbc8((uint8_t)(divisor & 0xFF), (uint8_t)(r & 0xFF), true, borrow);
                    uint8_t hi = sbc8((uint8_t)(divisor >> 8), (uint8_t)(r >> 8), borrow, borrow);
                    hlRem = (uint16_t)((hi << 8) | lo);
                    aFinal = hi;
                } else {
                    uint16_t bcDiv = (uint16_t)(0 - divisor);
                    if (dividendNeg || dividend == 0) {
                        uint16_t hlIn = (uint16_t)(0 - dividend);
                        uint16_t q = (uint16_t)(hlIn / bcDiv);
                        uint16_t r = (uint16_t)(hlIn % bcDiv);
                        deQuot = q;
                        bool borrow;
                        uint8_t lo = sbc8(0, (uint8_t)(r & 0xFF), false, borrow);
                        uint8_t hi = sbc8(0, (uint8_t)(r >> 8), borrow, borrow);
                        hlRem = (uint16_t)((hi << 8) | lo);
                        aFinal = hi;
                    } else {
                        uint16_t hlIn = (uint16_t)(dividend - 1);
                        uint16_t q = (uint16_t)(hlIn / bcDiv);
                        uint16_t r = (uint16_t)(hlIn % bcDiv);
                        deQuot = (uint16_t)(~q);
                        int32_t sub = (int32_t)r - (int32_t)bcDiv;
                        hlRem = (uint16_t)(sub + 1);
                        aFinal = (uint8_t)(deQuot >> 8);
                    }
                }

                PushStackWord(deQuot); // DVI pushes the QUOTIENT (MODI pushes hlRem instead)
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = aFinal;
                    m_cpu.r.setDE(deQuot);
                    m_cpu.r.setHL(hlRem);
                }
                commit = true;
                m_opcodeEmulatedCount[0x86]++;
                break;
            }
            case OP_MPI: { // MPI: Integer multiply ("i * j"). Uses the
                            // same two's-complement-safe binary
                            // multiplication MULT performs -- a standard
                            // shift-and-add algorithm that works correctly
                            // for signed values because (a*b) mod 2^16 is
                            // identical whether a,b are interpreted as
                            // signed or unsigned. The real loop shifts the
                            // multiplier (DE) right bit by bit, doubling
                            // the multiplicand (BC) each step and
                            // conditionally adding it to the accumulator
                            // (HL), terminating once DE fully reaches
                            // zero. Verified against a direct trace of the
                            // real algorithm across 2000 random input
                            // pairs before being written here.
                            //
                            // BC ends up holding the multiplicand doubled
                            // by the position of the multiplier's highest
                            // set bit -- NOT the original IPC -- so unlike
                            // most other opcodes here, this one MUST commit
                            // via BACK1 rather than plain BACK: BACK1 is
                            // real Z80 code that unconditionally reloads BC
                            // from IPCSAV (written below) before resuming
                            // dispatch, which is what actually restores the
                            // correct IPC here -- plain BACK, by contrast,
                            // just re-reads whatever's currently in BC, so
                            // it would pick up that doubled multiplicand
                            // value instead. This also means this native
                            // path's own BC write below (m_cpu.r.setBC)
                            // doesn't actually affect what IPC BACK1 ends
                            // up restoring -- IPCSAV, not the register, is
                            // what determines that -- so it's gated behind
                            // m_preserveZ80RegisterCompat like the rest.
                            //
                            // A is never touched anywhere in MPI or MULT,
                            // so it's left completely alone here too.
                uint16_t bcIpc = m_cpu.r.BC();
                m_mem[PM_V(0x0246)] = bcIpc & 0xFF; m_mem[PM_V(0x0247)] = bcIpc >> 8; // SAVIPC

                uint16_t multiplier   = PopStackWord(); // DE, per MULT's own entry convention
                uint16_t multiplicand = PopStackWord(); // BC

                uint16_t product = (uint16_t)(multiplicand * multiplier); // natural wraparound
                                                                            // matches the loop's
                                                                            // own 16-bit ADD HL,BC

                int highestBit = -1;
                for (int i = 15; i >= 0; i--) {
                    if (multiplier & (1u << i)) { highestBit = i; break; }
                }
                uint16_t bcFinal = (highestBit < 0) ? multiplicand : (uint16_t)(multiplicand << highestBit);

                PushStackWord(product);
                if (m_preserveZ80RegisterCompat) {
                    // A deliberately left untouched -- matches real MPI/MULT exactly
                    m_cpu.r.setDE(0x0000); // MULT's own loop always terminates with DE fully zeroed
                    m_cpu.r.setHL(product);
                    m_cpu.r.setBC(bcFinal); // purely cosmetic here: BACK1 (below) always restores BC
                                             // from IPCSAV regardless of this register's current value,
                                             // so this write changes nothing functional either way
                }
                m_cpu.r.PC = 0x03A4; // BACK1 -- restores BC from IPCSAV (written above, unconditionally)
                m_opcodeEmulatedCount[0x8F]++;
                break;
            }
            case OP_MODI: { // MODI: Remainder of integer division ("i MOD j"),
                            // computed via the same two's-complement DIVD
                            // algorithm DVI (integer divide) uses, returning
                            // the remainder instead of the quotient. DIVD
                            // has four distinct sign-combination
                            // post-processing paths feeding a shared
                            // DIVPOS core (unsigned restoring division).
                            // DIVPOS's own result is replaced here with a
                            // direct C++ division/modulo (mathematically
                            // identical, no need to simulate its bit-by-bit
                            // loop) -- but DIVPOS is verified to always
                            // leave A=0 at its own exit (its loop counter
                            // decrements to exactly zero to terminate),
                            // which each path's post-processing below
                            // builds on. All four paths, and their exact
                            // "make positive" adjustment (some use bitwise
                            // CPL, i.e. -x-1; others use exact arithmetic
                            // negation, i.e. -x -- these differ and are NOT
                            // interchangeable), were verified against all
                            // six worked examples in the source comments
                            // before being written here.
                            //
                            // Divisor==0 or divisor==-32768 bombs the
                            // program via DIVZER -- falls through to real
                            // Z80 code for that rare/error case.
                uint16_t divisorPeek = PeekStackWord(0);
                if (divisorPeek == 0 || divisorPeek == 0x8000) {   // DIVZER
                    if (m_preserveZ80RegisterCompat) break;                                   // compat: the Z80 routine
                    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC
                    PopStackWord();                                   // divisor; the dividend stays (DIVD $99)
                    m_cpu.r.PC = 0x03F9;                                // DIVZER (NativeErrors.inc)
                    break;
                }

                uint16_t bcIpc = m_cpu.r.BC();
                m_mem[PM_V(0x0246)] = bcIpc & 0xFF; m_mem[PM_V(0x0247)] = bcIpc >> 8; // SAVIPC

                uint16_t divisor  = PopStackWord();
                uint16_t dividend = PopStackWord();
                bool divisorNeg  = ((int16_t)divisor)  < 0;
                bool dividendNeg = ((int16_t)dividend) < 0;

                auto sbc8 = [](uint8_t a, uint8_t b, bool borrowIn, bool& borrowOut) -> uint8_t {
                    int diff = (int)a - (int)b - (borrowIn ? 1 : 0);
                    borrowOut = (diff < 0);
                    return (uint8_t)diff;
                };

                uint16_t deQuot, hlRem;
                uint8_t aFinal;

                if (!divisorNeg && !dividendNeg) {
                    // $30: positive dividend, positive divisor -- DIVPOS's
                    // own result, completely unadjusted.
                    deQuot = (uint16_t)(dividend / divisor);
                    hlRem  = (uint16_t)(dividend % divisor);
                    aFinal = 0;
                } else if (!divisorNeg && dividendNeg) {
                    // $40: negative dividend, positive divisor.
                    // dividend' := ~dividend (CPL, i.e. |dividend|-1, NOT exact)
                    uint16_t hlIn = (uint16_t)(~dividend);
                    uint16_t q = (uint16_t)(hlIn / divisor);
                    uint16_t r = (uint16_t)(hlIn % divisor);
                    deQuot = (uint16_t)(~q); // realquotient := -quotient-1
                    bool borrow;
                    // realremainder := divisor - remainder - 1 (SCF sets initial borrow=1)
                    uint8_t lo = sbc8((uint8_t)(divisor & 0xFF), (uint8_t)(r & 0xFF), true, borrow);
                    uint8_t hi = sbc8((uint8_t)(divisor >> 8), (uint8_t)(r >> 8), borrow, borrow);
                    hlRem = (uint16_t)((hi << 8) | lo);
                    aFinal = hi;
                } else {
                    // $50 onward: negative divisor. Divisor is made positive
                    // via EXACT negation (unsigned wraparound subtract from
                    // zero -- matches the real "XOR A;SUB C;...;SBC A,B"
                    // sequence, and is well-defined even for the edge case
                    // divisor==-32768, though that specific value is already
                    // excluded by the DIVZER check above).
                    uint16_t bcDiv = (uint16_t)(0 - divisor);
                    if (dividendNeg || dividend == 0) {
                        // $80: negative-or-zero dividend, negative divisor.
                        // dividend' := exact negation (SUB/SBC, not CPL) --
                        // well-defined via unsigned wraparound even for the
                        // dividend==-32768 edge case (self-negating in 16-bit
                        // two's complement).
                        uint16_t hlIn = (uint16_t)(0 - dividend);
                        uint16_t q = (uint16_t)(hlIn / bcDiv);
                        uint16_t r = (uint16_t)(hlIn % bcDiv);
                        deQuot = q; // no DE post-processing on this path
                        bool borrow;
                        // realremainder := -remainder (exact negation, no initial borrow)
                        uint8_t lo = sbc8(0, (uint8_t)(r & 0xFF), false, borrow);
                        uint8_t hi = sbc8(0, (uint8_t)(r >> 8), borrow, borrow);
                        hlRem = (uint16_t)((hi << 8) | lo);
                        aFinal = hi;
                    } else {
                        // $60->$70: positive dividend, negative divisor.
                        // dividend' := dividend - 1 (DEC HL, exact -1, NOT CPL).
                        uint16_t hlIn = (uint16_t)(dividend - 1);
                        uint16_t q = (uint16_t)(hlIn / bcDiv);
                        uint16_t r = (uint16_t)(hlIn % bcDiv);
                        deQuot = (uint16_t)(~q); // realquotient := -quotient-1
                        // realremainder := (remainder - divisor) + 1 (AND A
                        // clears carry -- no initial borrow here, unlike $40's SCF)
                        int32_t sub = (int32_t)r - (int32_t)bcDiv;
                        hlRem = (uint16_t)(sub + 1);
                        aFinal = (uint8_t)(deQuot >> 8); // "LD A,D;CPL" -- D holds
                                                           // deQuot's own high byte
                                                           // here, matching it exactly
                    }
                }

                PushStackWord(hlRem);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.A = aFinal;
                    m_cpu.r.setDE(deQuot);
                    m_cpu.r.setHL(hlRem);
                }
                commit = true; // BC never disturbed by this native path, matches
                                      // precedent elsewhere -- BACK1's own restore from
                                      // IPCSAV would be redundant (and slower, requiring
                                      // two real Z80 instructions to reach plain BACK anyway)
                m_opcodeEmulatedCount[0x8E]++;
                break;
            }
            case OP_RNP: { // RNP: Return from normal procedure. Tears down the
                            // current stack frame, optionally moving a 0-2
                            // word return value down to where the caller
                            // expects it, and restores MP/MPD0/JTAB/SEGP from
                            // the saved MSCW. If the restored segment differs
                            // from the current one, the real code also
                            // decrements that segment's reference count --
                            // (DECREF: refcount-- in INTSEGT for the segment
                            // being left; the real code does nothing else on
                            // that path). Both cases are native: the DECREF
                            // step lives in NativeSegReturn.inc, shared with
                            // RBP and the linux-harness copies.
                uint16_t mpd0Val = (uint16_t)(m_mem[PM_V(0x0242)] | (m_mem[PM_V(0x0243)] << 8));
                uint16_t oldSP = (uint16_t)(m_mem[mpd0Val] | (m_mem[(uint16_t)(mpd0Val + 1)] << 8));
                uint8_t numWords = PM_CODE8(m_cpu.r.BC()); // peek only -- BC is about
                                                          // to be entirely replaced
                                                          // below, so the real code
                                                          // never advances past this
                                                          // byte either
                uint16_t bytesToReturn = (uint16_t)(numWords * 2);
                uint16_t srcStart = (uint16_t)(mpd0Val + 2);
                uint16_t destStart = (uint16_t)(oldSP - bytesToReturn);
                uint16_t newSP = (bytesToReturn == 0) ? oldSP : destStart;

                uint16_t framePtr = (uint16_t)(m_mem[PM_V(0x02F2)] | (m_mem[PM_V(0x02F3)] << 8)); // MP
                framePtr = (uint16_t)(framePtr + 2); // skip junked static link
                uint16_t newMP = (uint16_t)(m_mem[framePtr] | (m_mem[(uint16_t)(framePtr + 1)] << 8)); // dynamic link
                framePtr = (uint16_t)(framePtr + 2);
                uint16_t newMPD0 = (uint16_t)(newMP + 10); // +DISP0
                uint16_t newJTAB = (uint16_t)(m_mem[framePtr] | (m_mem[(uint16_t)(framePtr + 1)] << 8));
                framePtr = (uint16_t)(framePtr + 2);
                uint16_t newSegCandidate = (uint16_t)(m_mem[framePtr] | (m_mem[(uint16_t)(framePtr + 1)] << 8));
                framePtr = (uint16_t)(framePtr + 2);

                uint16_t curSegP = (uint16_t)(m_mem[PM_V(0x02F6)] | (m_mem[PM_V(0x02F7)] << 8));
                { // same OR different segment -- both native now
#include "NativeSegReturn.inc"
                    // Same segment: safe to commit natively, no DECREF needed.
                    if (bytesToReturn > 0) {
                        if (destStart <= srcStart) {
                            for (uint16_t i = 0; i < bytesToReturn; i++)
                                m_mem[(uint16_t)(destStart + i)] = m_mem[(uint16_t)(srcStart + i)];
                        } else {
                            for (uint16_t i = bytesToReturn; i-- > 0; )
                                m_mem[(uint16_t)(destStart + i)] = m_mem[(uint16_t)(srcStart + i)];
                        }
                    }
                    uint16_t newBC = (uint16_t)(m_mem[framePtr] | (m_mem[(uint16_t)(framePtr + 1)] << 8));

                    m_mem[PM_V(0x02F2)] = newMP & 0xFF; m_mem[PM_V(0x02F3)] = newMP >> 8;       // MP
                    m_mem[PM_V(0x0242)] = newMPD0 & 0xFF; m_mem[PM_V(0x0243)] = newMPD0 >> 8;   // MPD0
                    m_mem[PM_V(0x02F4)] = newJTAB & 0xFF; m_mem[PM_V(0x02F5)] = newJTAB >> 8;   // JTAB
                    m_mem[PM_V(0x02F6)] = newSegCandidate & 0xFF; m_mem[PM_V(0x02F7)] = newSegCandidate >> 8; // SEGP

                    if (m_preserveZ80RegisterCompat) m_cpu.r.A = segRetA; // "LD A,(SEGP+1);CP H" leaves A = old SEGP's high byte
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setDE(newSegCandidate);      // "EX DE,HL" at the end swaps these
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(newSP);
                    m_cpu.r.SP = newSP;                  // actually move the real stack pointer
                    m_cpu.r.setBC(newBC);                // restore the caller's continuation address
                    commit = true;
                    m_opcodeEmulatedCount[0xAD]++;
                }
                break;
            }
            case OP_CEQU: { // CEQU: Equality comparison, dispatched by a type
#include "NativeRealc.inc"
#include "NativePowrc.inc"
                            // byte to one of 6 completely different algorithms
                            // (see CSETUP/CMPTBL). Natively handled here:
                            // BOOLC (compare bit 0 only), WORDC/BYTEC (array/
                            // record equality -- a plain memcmp, since for
                            // EQUALITY specifically the intricate byte-by-byte
                            // CPI-loop mechanics the real ordering comparisons
                            // need are irrelevant; two blocks are equal iff
                            // every byte matches, full stop), and STRGC
                            // (string equality: same length AND same content,
                            // handling the "disguised single character"
                            // representation where a pointer with a zero high
                            // byte IS the character itself rather than a real
                            // pointer to a length-prefixed string). REALC and
                            // POWRC ("very gross" per the source's own
                            // comment) are NOT natively handled -- both are
                            // substantially more involved (multi-word signed
                            // float comparison; multi-word set bit twiddling)
                            // and only the type byte is peeked before falling
                            // through, so nothing is disturbed for those two.
                // CMPTBL type codes, corrected: the table symbol itself
                // (CMPTBL .EQU $-2) points TWO BYTES BEFORE its own first
                // entry, so codes are 2,4,6,8,10,12 for REALC/STRGC/BOOLC/
                // POWRC/BYTEC/WORDC respectively -- NOT 0,2,4,6,8,10 as a
                // naive reading of the table position would suggest. Caught
                // via the trace-diff: with the naive (wrong) mapping, type=4
                // was being treated as BOOLC (a simple bit-0 compare) when
                // the operands were actually STRGC string pointers.
                uint8_t typeCode = PM_CODE8(m_cpu.r.BC()); // peek only until a type is confirmed handled
                if (typeCode == 6) { // BOOLC: "LD A,E;AND 1;LD E,A;LD A,L;AND 1;CP E" --
                                      // A ends up = a's masked bit0; D is never
                                      // touched (retains b's original high byte),
                                      // only E gets overwritten with b's masked bit0.
                    m_cpu.r.setBC((uint16_t)(m_cpu.r.BC() + 1));
                    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC
                    uint16_t b = PopStackWord();
                    uint16_t a = PopStackWord();
                    bool equal = ((a & 1) == (b & 1));
                    PushStackWord(equal ? 1 : 0);
                    if (m_preserveZ80RegisterCompat) m_cpu.r.A = (uint8_t)(a & 1);
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setDE((uint16_t)((b & 0xFF00) | (b & 1)));
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(equal ? 1 : 0); // PSHTRU1/PSHFLS1: "LD HL,0001H"/"LD HL,0000H"
                    commit = true;
                    m_opcodeEmulatedCount[0xAF]++;
                } else if (typeCode == 10 || typeCode == 12) { // BYTEC or WORDC: scan
                                      // byte-by-byte (SWEQ) rather than a plain
                                      // memcmp, so A/DE land exactly where the real
                                      // CPI-loop would leave them -- A = a's byte at
                                      // the last position examined (the mismatch, or
                                      // the final byte of a full match), DE = a's
                                      // pointer advanced to that same position.
                    m_cpu.r.setBC((uint16_t)(m_cpu.r.BC() + 1));
                    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC
                    uint16_t count = DecodeGBDE();
                    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC again past the size operand, as the Z80 BYTEC/WORDC compare does (found by Verify P-System)
                    uint16_t byteCount = (typeCode == 12) ? (uint16_t)(count * 2) : count;
                    uint16_t bPtr = PopStackWord();
                    uint16_t aPtr = PopStackWord();
                    bool equal = true;
                    uint16_t deFinal = aPtr;
                    uint8_t aRegFinal = 0;
                    for (uint16_t idx = 0; idx < byteCount; idx++) {
                        uint8_t ac = m_mem[(uint16_t)(aPtr + idx)];
                        uint8_t bc = m_mem[(uint16_t)(bPtr + idx)];
                        deFinal = (uint16_t)(aPtr + idx);
                        aRegFinal = ac;
                        if (ac != bc) { equal = false; break; }
                    }
                    PushStackWord(equal ? 1 : 0);
                    if (m_preserveZ80RegisterCompat) m_cpu.r.A = aRegFinal;
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setDE(deFinal);
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(equal ? 1 : 0); // PSHTRU1/PSHFLS1: "LD HL,0001H"/"LD HL,0000H"
                    commit = true;
                    m_opcodeEmulatedCount[0xAF]++;
                } else if (typeCode == 4) { // STRGC: same scanning idea as BYTEC/
                                      // WORDC above, but over min(lenA,lenB)
                                      // characters, then a final length check. A
                                      // "disguised" pointer (high byte zero) means
                                      // that pointer IS a single character rather
                                      // than a real length-prefixed string pointer
                                      // -- the real code redirects it through a
                                      // fixed one-char scratch buffer (LTSTRNG,
                                      // 0x024E) for this same reason, which is why
                                      // DE's base is LTSTRNG rather than the raw
                                      // (meaningless-as-an-address) disguised value
                                      // when 'a' is disguised.
                    m_cpu.r.setBC((uint16_t)(m_cpu.r.BC() + 1));
                    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC
                    uint16_t bPtr = PopStackWord();
                    uint16_t aPtr = PopStackWord();
                    bool aDisguised = (aPtr >> 8) == 0;
                    bool bDisguised = (bPtr >> 8) == 0;
                    uint8_t aLen = aDisguised ? 1 : m_mem[aPtr];
                    uint8_t bLen = bDisguised ? 1 : m_mem[bPtr];
                    uint16_t aData = (uint16_t)(aPtr + 1);
                    uint16_t bData = (uint16_t)(bPtr + 1);
                    uint16_t aBasePtr = aDisguised ? PM_V(0x024E) : aPtr;
                    uint8_t minLen = (aLen < bLen) ? aLen : bLen;
                    bool foundMismatch = false;
                    uint16_t deFinal = aBasePtr;
                    uint8_t aRegFinal = 0;
                    for (uint8_t idx = 0; idx < minLen; idx++) {
                        uint8_t ac = aDisguised ? (aPtr & 0xFF) : m_mem[(uint16_t)(aData + idx)];
                        uint8_t bc = bDisguised ? (bPtr & 0xFF) : m_mem[(uint16_t)(bData + idx)];
                        deFinal = (uint16_t)(aBasePtr + idx + 1);
                        if (ac != bc) { foundMismatch = true; aRegFinal = ac; break; }
                    }
                    bool equal;
                    if (foundMismatch) {
                        equal = false;
                        if (m_preserveZ80RegisterCompat) m_cpu.r.A = aRegFinal;
                    } else {
                        equal = (aLen == bLen);
                        if (m_preserveZ80RegisterCompat) m_cpu.r.A = aLen; // "LD A,(LENA)" reloads lenA fresh
                    }
                    PushStackWord(equal ? 1 : 0);
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setDE(deFinal);
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(equal ? 1 : 0); // PSHTRU1/PSHFLS1: "LD HL,0001H"/"LD HL,0000H"
                    commit = true;
                    m_opcodeEmulatedCount[0xAF]++;
                }
                // (REALC(2) and POWRC(8) are handled by NativeRealc.inc / NativePowrc.inc above;
                //  with register compatibility on they fall through untouched to the Z80 code.)
                break;
            }
            case OP_CNEQ: { // CNEQ: Inequality comparison. Calls the exact
#include "NativeRealc.inc"
#include "NativePowrc.inc"
                            // same CSETUP dispatch as CEQU (same type
                            // byte, same 6-way branch, same per-type
                            // comparison mechanics) -- the ONLY difference
                            // between CEQU and CNEQ is which boolean gets
                            // pushed once the comparison itself is done
                            // (CEQU pushes true on equal; CNEQ pushes true
                            // on NOT equal). So this mirrors CEQU's own
                            // BOOLC/WORDC/BYTEC/STRGC handling exactly,
                            // with every "equal ? 1 : 0" inverted to
                            // "equal ? 0 : 1". REALC and POWRC are, same
                            // as CEQU, not natively handled.
                uint8_t typeCode = PM_CODE8(m_cpu.r.BC()); // peek only until a type is confirmed handled
                if (typeCode == 6) { // BOOLC
                    m_cpu.r.setBC((uint16_t)(m_cpu.r.BC() + 1));
                    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC
                    uint16_t b = PopStackWord();
                    uint16_t a = PopStackWord();
                    bool equal = ((a & 1) == (b & 1));
                    PushStackWord(equal ? 0 : 1);
                    if (m_preserveZ80RegisterCompat) m_cpu.r.A = (uint8_t)(a & 1);
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setDE((uint16_t)((b & 0xFF00) | (b & 1)));
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(equal ? 0 : 1);
                    commit = true;
                    m_opcodeEmulatedCount[0xB7]++;
                } else if (typeCode == 10 || typeCode == 12) { // BYTEC or WORDC
                    m_cpu.r.setBC((uint16_t)(m_cpu.r.BC() + 1));
                    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC
                    uint16_t count = DecodeGBDE();
                    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC again past the size operand, as the Z80 BYTEC/WORDC compare does (found by Verify P-System)
                    uint16_t byteCount = (typeCode == 12) ? (uint16_t)(count * 2) : count;
                    uint16_t bPtr = PopStackWord();
                    uint16_t aPtr = PopStackWord();
                    bool equal = true;
                    uint16_t deFinal = aPtr;
                    uint8_t aRegFinal = 0;
                    for (uint16_t idx = 0; idx < byteCount; idx++) {
                        uint8_t ac = m_mem[(uint16_t)(aPtr + idx)];
                        uint8_t bc = m_mem[(uint16_t)(bPtr + idx)];
                        deFinal = (uint16_t)(aPtr + idx);
                        aRegFinal = ac;
                        if (ac != bc) { equal = false; break; }
                    }
                    PushStackWord(equal ? 0 : 1);
                    if (m_preserveZ80RegisterCompat) m_cpu.r.A = aRegFinal;
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setDE(deFinal);
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(equal ? 0 : 1);
                    commit = true;
                    m_opcodeEmulatedCount[0xB7]++;
                } else if (typeCode == 4) { // STRGC
                    m_cpu.r.setBC((uint16_t)(m_cpu.r.BC() + 1));
                    m_mem[PM_V(0x0246)] = m_cpu.r.BC() & 0xFF; m_mem[PM_V(0x0247)] = m_cpu.r.BC() >> 8; // SAVIPC
                    uint16_t bPtr = PopStackWord();
                    uint16_t aPtr = PopStackWord();
                    bool aDisguised = (aPtr >> 8) == 0;
                    bool bDisguised = (bPtr >> 8) == 0;
                    uint8_t aLen = aDisguised ? 1 : m_mem[aPtr];
                    uint8_t bLen = bDisguised ? 1 : m_mem[bPtr];
                    uint16_t aData = (uint16_t)(aPtr + 1);
                    uint16_t bData = (uint16_t)(bPtr + 1);
                    uint16_t aBasePtr = aDisguised ? PM_V(0x024E) : aPtr;
                    uint8_t minLen = (aLen < bLen) ? aLen : bLen;
                    bool foundMismatch = false;
                    uint16_t deFinal = aBasePtr;
                    uint8_t aRegFinal = 0;
                    for (uint8_t idx = 0; idx < minLen; idx++) {
                        uint8_t ac = aDisguised ? (aPtr & 0xFF) : m_mem[(uint16_t)(aData + idx)];
                        uint8_t bc = bDisguised ? (bPtr & 0xFF) : m_mem[(uint16_t)(bData + idx)];
                        deFinal = (uint16_t)(aBasePtr + idx + 1);
                        if (ac != bc) { foundMismatch = true; aRegFinal = ac; break; }
                    }
                    bool equal;
                    if (foundMismatch) {
                        equal = false;
                        if (m_preserveZ80RegisterCompat) m_cpu.r.A = aRegFinal;
                    } else {
                        equal = (aLen == bLen);
                        if (m_preserveZ80RegisterCompat) m_cpu.r.A = aLen; // "LD A,(LENA)" reloads lenA fresh
                    }
                    PushStackWord(equal ? 0 : 1);
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setDE(deFinal);
                    if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(equal ? 0 : 1);
                    commit = true;
                    m_opcodeEmulatedCount[0xB7]++;
                }
                // (REALC(2) and POWRC(8) are handled by NativeRealc.inc / NativePowrc.inc above;
                //  with register compatibility on they fall through untouched to the Z80 code.)
                break;
            }
            case OP_CGTR: { // CGTR: Greater-than comparison ("a > b").
#include "NativeRealc.inc"
                            // Shares CSETUP's "flags are result of a-b"
                            // contract via CmpSetupOrdering (BOOLC/STRGC
                            // only -- see its own comment for why ordering
                            // comparisons on Sets/Arrays/Records don't
                            // apply here). Result is true iff NOT carry
                            // AND NOT zero (a-b is positive, non-zero).
                            //
                            // CmpSetupOrdering's own A/DE/BC writes are
                            // left as-is (shared helper, also used by
                            // CEQU/CNEQ/CLEQ/CLSS/CGEQ -- not touched in
                            // this pass, same as GetIA/DecodeGBDE
                            // elsewhere). Only this opcode's own HL write
                            // below is gated: it's a pure compat mirror
                            // (the actual result is already on the stack),
                            // and BACK1 restores BC from IPCSAV regardless
                            // of what CmpSetupOrdering itself left there.
                bool carry, zero;
                if (!CmpSetupOrdering(carry, zero)) break; // fall through untouched
                bool result = !carry && !zero;
                PushStackWord(result ? 1 : 0);
                if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(result ? 1 : 0); // PSHTRU1/PSHFLS1: "LD HL,0001H"/"LD HL,0000H"
                m_cpu.r.PC = 0x03A4; // BACK1
                m_opcodeEmulatedCount[0xB1]++;
                break;
            }
            case OP_CLEQ: { // CLEQ: Less-than-or-equal comparison ("a <= b").
#include "NativeRealc.inc"
#include "NativePowrc.inc"
                            // Result is true iff carry OR zero (a-b is
                            // negative or zero). See CGTR's own comment
                            // above for why only HL is gated here.
                bool carry, zero;
                if (!CmpSetupOrdering(carry, zero)) break; // fall through untouched
                bool result = carry || zero;
                PushStackWord(result ? 1 : 0);
                if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(result ? 1 : 0);
                m_cpu.r.PC = 0x03A4; // BACK1
                m_opcodeEmulatedCount[0xB4]++;
                break;
            }
            case OP_CLSS: { // CLSS: Less-than comparison ("a < b"). Result
#include "NativeRealc.inc"
                            // is true iff carry (a-b is negative). See
                            // CGTR's own comment above for why only HL is
                            // gated here.
                bool carry, zero;
                if (!CmpSetupOrdering(carry, zero)) break; // fall through untouched
                bool result = carry;
                PushStackWord(result ? 1 : 0);
                if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(result ? 1 : 0);
                m_cpu.r.PC = 0x03A4; // BACK1
                m_opcodeEmulatedCount[0xB5]++;
                break;
            }
            case OP_CGEQ: { // CGEQ: Greater-than-or-equal comparison
#include "NativeRealc.inc"
#include "NativePowrc.inc"
                            // ("a >= b"). Result is true iff NOT carry
                            // (a-b is non-negative). See CGTR's own
                            // comment above for why only HL is gated here.
                bool carry, zero;
                if (!CmpSetupOrdering(carry, zero)) break; // fall through untouched
                bool result = !carry;
                PushStackWord(result ? 1 : 0);
                if (m_preserveZ80RegisterCompat) m_cpu.r.setHL(result ? 1 : 0);
                m_cpu.r.PC = 0x03A4; // BACK1
                m_opcodeEmulatedCount[0xB0]++;
                break;
            }
            case OP_LDO: { // LDO: Load global word. Operand (word offset,
                            // GBDE-encoded) selects which global; value
                            // lives at BASED0 + offset*2 (globals are
                            // word-sized, hence the doubling).
                uint16_t offset = DecodeGBDE();
                uint16_t based0 = (uint16_t)(m_mem[PM_V(0x0244)] | (m_mem[PM_V(0x0245)] << 8));
                uint16_t addr = (uint16_t)(based0 + offset * 2);
                uint16_t value = (uint16_t)(m_mem[addr] | (m_mem[(uint16_t)(addr + 1)] << 8));
                PushStackWord(value);
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setDE(value);                // real code's "LD E,(HL);LD D,(HL)" overwrites DE
                                                           // with the loaded value right before the push --
                                                           // it does NOT keep holding the decoded offset
                    m_cpu.r.setHL((uint16_t)(addr + 1)); // real code leaves HL = addr+1 (post the second LD D,(HL))
                }
                commit = true;
                m_opcodeEmulatedCount[0xA9]++;
                break;
            }
            case OP_SRO: { // SRO: Store global word (inverse of LDO)
                uint16_t offset = DecodeGBDE();
                uint16_t based0 = (uint16_t)(m_mem[PM_V(0x0244)] | (m_mem[PM_V(0x0245)] << 8));
                uint16_t addr = (uint16_t)(based0 + offset * 2);
                uint16_t value = PopStackWord();
                m_mem[addr] = value & 0xFF;
                m_mem[(uint16_t)(addr + 1)] = value >> 8;
                if (m_preserveZ80RegisterCompat) {
                    m_cpu.r.setDE(value);                // real code's DE ends up holding the stored value (overwrote the offset)
                    m_cpu.r.setHL((uint16_t)(addr + 1));
                }
                commit = true;
                m_opcodeEmulatedCount[0xAB]++;
                break;
            }
            case OP_FJP: { // FJP: False jump. Pop a boolean; if false, jump
                            // by the 1-byte operand that follows (short
                            // relative -- the common case); if true, just
                            // skip that operand byte and continue. The
                            // operand's OWN top bit selects between this
                            // short relative jump and a longer jump-table-
                            // indexed one for larger procedures; only the
                            // short-relative case is handled natively here
                            // -- peeking (not popping/advancing) until we
                            // know which case applies means the rare
                            // jump-table case can fall through to the real
                            // Z80 code completely untouched.
                uint16_t boolValue = PeekStackWord(0);
                uint8_t ofs = PM_CODE8(m_cpu.r.BC());
                if ((boolValue & 1) != 0) {
                    // TRUE: don't jump, just skip the 1-byte operand.
                    // Real code (NOJ) never touches DE/HL, but "POP AF"
                    // itself puts the popped word's HIGH byte into A (Z80's
                    // AF pair convention: A=high, F=low) -- for a uniform
                    // 0x0000/0xFFFF boolean that's 0x00 or 0xFF.
                    PopStackWord();
                    m_cpu.r.setBC((uint16_t)(m_cpu.r.BC() + 1)); // IPC advance past the operand byte -- functionally required
                    if (m_preserveZ80RegisterCompat) m_cpu.r.A = (uint8_t)(boolValue >> 8);
                    commit = true;
                    m_opcodeEmulatedCount[0xA1]++;
                } else if ((ofs & 0x80) == 0) {
                    // FALSE, short relative jump. UJP's own code re-loads A
                    // from the operand byte, then the "ADD A,C; LD C,A;
                    // LD A,0; ADC A,B; LD B,A" 16-bit-add-via-8-bit sequence
                    // leaves A holding the same value as the new B -- i.e.
                    // the high byte of the resulting BC.
                    PopStackWord();
                    uint16_t newBC = (uint16_t)(m_cpu.r.BC() + 1 + ofs);
                    m_cpu.r.setBC(newBC); // IPC jump target -- functionally required
                    if (m_preserveZ80RegisterCompat) m_cpu.r.A = (uint8_t)(newBC >> 8);
                    commit = true;
                    m_opcodeEmulatedCount[0xA1]++;
                } else {
                    // Long jump through the procedure's jump table (offset >= 0x80): the entry
                    // at JTAB + (offset - 256) holds a self-relative pointer to the target
                    // (Z80 UJP $10: LD HL,(JTAB); BC := FFxx; ADD HL,BC; SELREL).
                    PopStackWord();                                     // the false boolean
                    uint16_t jtab = (uint16_t)(m_mem[PM_V(0x02F4)] | (m_mem[PM_V(0x02F5)] << 8));
                    uint16_t entry = (uint16_t)(jtab + ofs - 256);
                    uint16_t rel = PM_CODE16(entry);
                    uint16_t target = (uint16_t)(entry - rel);
                    m_cpu.r.setBC(target);
                    if (m_preserveZ80RegisterCompat) { m_cpu.r.A = ofs; m_cpu.r.setDE(rel); m_cpu.r.setHL(target); }
                    commit = true;
                    m_opcodeEmulatedCount[0xA1]++;
                }
                // else: FALSE + long/jump-table jump -- nothing peeked has
                // been consumed, so it's safe to fall through untouched.
                break;
            }
#include "NativeOps.inc"
            default: break;
        }
        if (commit) m_cpu.r.PC = 0x03B0;
        // ---- SLDC, all 128 values (short load constant) ----
        // SLDC isn't table-dispatched like the opcodes above -- it's the
        // fallthrough path at BACK itself for any opcode byte with its
        // top bit clear, where the byte's own value (0-127) IS the
        // constant to push. Intercepting at SLDCI's entry (0x03AB),
        // before its own "RRA" un-doubles A, means A is still 2x the
        // real opcode value -- RRA with a carry-in of 0 (always true
        // here, since these opcodes' top bit is clear, so ADD A,A never
        // sets carry) is just A>>1, so that's all "undoing" it takes.
        if (m_nativePcodeOps && m_cpu.r.PC == 0x03AB) {
            uint8_t doubled = (uint8_t)(PM_CODE8((uint16_t)(m_cpu.r.BC() - 1)) << 1); // recomputed fresh -- BC always points past the opcode byte, regardless of whether the shortcut fired for THIS dispatch
            uint8_t original = (uint8_t)(doubled >> 1); // matches real RRA exactly (carry-in always 0 here)
            PushStackWord(original);
            if (m_preserveZ80RegisterCompat) {
                m_cpu.r.A = original;      // real leftover: RRA's own result
                m_cpu.r.setHL(original);   // real leftover: LD L,A; LD H,00H
            }
            m_cpu.r.PC = 0x03B0;
            m_opcodeEmulatedCount[original]++;
        }
        // ---- SLDL, all 16 values (short load local word) ----
        // Like SLDC, this is ONE shared routine (0x047C) reused by all 16
        // opcodes 0xD8-0xE7 -- their XFRTBL slots all point here, and the
        // opcode byte itself (still 2x, per the dispatch mechanism, and
        // this time with the top bit that got shifted into carry known
        // to be 1, since these opcodes' top bit is set) tells the routine
        // which local to load via "ADD A,52H" directly on the doubled
        // value, producing a byte displacement into the local/frame area
        // (based at MPD0, 0x0242) without needing to un-double A at all.
        if (m_nativePcodeOps && m_cpu.r.PC == 0x047C) {
            uint8_t doubled = (uint8_t)(PM_CODE8((uint16_t)(m_cpu.r.BC() - 1)) << 1); // recomputed fresh -- BC always points past the opcode byte, regardless of whether the shortcut fired for THIS dispatch
            uint8_t displacement = (uint8_t)(doubled + 0x52);
            uint16_t mpd0 = (uint16_t)(m_mem[PM_V(0x0242)] | (m_mem[PM_V(0x0243)] << 8));
            uint16_t addr = (uint16_t)(mpd0 + displacement);
            uint16_t value = (uint16_t)(m_mem[addr] | (m_mem[(uint16_t)(addr + 1)] << 8));
            PushStackWord(value);
            if (m_preserveZ80RegisterCompat) {
                m_cpu.r.A = displacement;   // real leftover: ADD A,52H's own result
                m_cpu.r.setDE(value);       // DE gets overwritten by LD E,(HL); LD D,(HL)
                m_cpu.r.setHL((uint16_t)(addr + 1)); // HL = addr+1 after INC HL
            }
            m_cpu.r.PC = 0x03B0;
            uint8_t opcodeByte = (uint8_t)((doubled >> 1) | 0x80); // restore the bit shifted into carry
            m_opcodeEmulatedCount[opcodeByte]++;
        }
        // ---- SLDO, all 16 values (short load global word) ----
        // Identical structure to SLDL, just against the global area
        // (based at BASED0, 0x0244) instead of the local/frame area, and
        // a different additive constant (0x32).
        if (m_nativePcodeOps && m_cpu.r.PC == 0x04B6) {
            uint8_t doubled = (uint8_t)(PM_CODE8((uint16_t)(m_cpu.r.BC() - 1)) << 1); // recomputed fresh -- BC always points past the opcode byte, regardless of whether the shortcut fired for THIS dispatch
            uint8_t displacement = (uint8_t)(doubled + 0x32);
            uint16_t based0 = (uint16_t)(m_mem[PM_V(0x0244)] | (m_mem[PM_V(0x0245)] << 8));
            uint16_t addr = (uint16_t)(based0 + displacement);
            uint16_t value = (uint16_t)(m_mem[addr] | (m_mem[(uint16_t)(addr + 1)] << 8));
            PushStackWord(value);
            if (m_preserveZ80RegisterCompat) {
                m_cpu.r.A = displacement;
                m_cpu.r.setDE(value);
                m_cpu.r.setHL((uint16_t)(addr + 1));
            }
            m_cpu.r.PC = 0x03B0;
            uint8_t opcodeByte = (uint8_t)((doubled >> 1) | 0x80);
            m_opcodeEmulatedCount[opcodeByte]++;
        }
        // ---- SIND, 7 values (short static index and load word) ----
        // Shared entry (0x053C) for opcodes 0xF9-0xFF, the same packed-
        // immediate pattern as SLDL/SLDO/SIND0. Given a base address on
        // the stack, adds a displacement (from the doubled opcode value
        // via "ADD A,10H") and loads the word at that address. SIND0
        // (0xF8, index=0) is the trivial special case, already handled
        // separately above as a plain load-indirect with no offset.
        if (m_nativePcodeOps && m_cpu.r.PC == 0x053C) {
            uint8_t doubled = (uint8_t)(PM_CODE8((uint16_t)(m_cpu.r.BC() - 1)) << 1); // recomputed fresh -- BC always points past the opcode byte, regardless of whether the shortcut fired for THIS dispatch
            uint8_t displacement = (uint8_t)(doubled + 0x10);
            uint16_t base = PopStackWord();
            uint16_t addr = (uint16_t)(base + displacement);
            uint16_t value = (uint16_t)(m_mem[addr] | (m_mem[(uint16_t)(addr + 1)] << 8));
            PushStackWord(value);
            if (m_preserveZ80RegisterCompat) {
                m_cpu.r.A = displacement; // matches "ADD A,10H" leftover
                m_cpu.r.setDE(value);     // overwritten by the load
                m_cpu.r.setHL((uint16_t)(addr + 1));
            }
            m_cpu.r.PC = 0x03B0;
            uint8_t opcodeByte = (uint8_t)((doubled >> 1) | 0x80);
            m_opcodeEmulatedCount[opcodeByte]++;
        }

        // ---- Native-C UNITREAD/UNITWRITE (CSP 5/6) fast path, unit #4 only ---
        // UREAD/UWRITE are not top-level p-code opcodes -- they're CSP
        // (0x9E, "call standard procedure") sub-selectors, reached via
        // SYSTEM.MICRO's own CSPTBL dispatch at these fixed Z80
        // addresses. By the time execution reaches either address, CSP
        // has already resolved which selector was requested, so we only
        // need to hook these two addresses directly.
        //
        // Stack layout at entry (matches SYSIO's own pop sequence in
        // CPMIO.TEXT exactly -- top of stack first): async, blk, len,
        // buf_DE, buf_HL, unit -- i.e. UNITREAD/UNITWRITE's 5 Pascal
        // parameters (unit, var buf, count, blocknum, async), pushed
        // left-to-right so async (the last parameter) ends up on top.
        //
        // Only unit #4 (BIGGY) is handled here, and only when the
        // request is fully in range -- everything else (a different
        // unit, or an out-of-range block/length) falls through
        // untouched to the real Z80 CPMIO driver + BIOS calls, so this
        // is strictly additive: nothing that used to work can break.
        // NOTE (v1.74): INACTIVE with the SYSTEM.MICRO this system runs (BIGGY's).
        // These hooks are at the addresses of the U120 distribution build;
        // in BIGGY's interpreter the unit-I/O routines are 30 bytes higher
        // (UREAD 1BAE, UWRITE 1BA9, ECHO 1E2A) and these addresses fall in
        // the middle of other instructions, so they are never reached. Unit
        // I/O is native at the CSP level instead: see NativeCsp.inc.
        if (m_cpu.r.PC == 0x1B90 || m_cpu.r.PC == 0x1B8B) {
            bool isWrite = (m_cpu.r.PC == 0x1B8B);
            uint16_t async = PeekStackWord(0);
            uint16_t len   = PeekStackWord(4);
            uint16_t bufDE = PeekStackWord(6);
            uint16_t bufHL = PeekStackWord(8);
            uint16_t unit  = PeekStackWord(10);
            if (unit == 4) {
                uint16_t blk = PeekStackWord(2);
                uint16_t buf = (uint16_t)(bufHL + bufDE);
                long fileOff = (long)blk * 512;
                if (fileOff >= 0 && (size_t)(fileOff + len) <= m_volDrive0.size()) {
                    if (isWrite) {
                        for (int i = 0; i < len; i++) m_volDrive0[fileOff + i] = m_mem[(uint16_t)(buf + i)];
                        FlushDriveRegion(0, fileOff, len);
                    }
                    else         { for (int i = 0; i < len; i++) m_mem[(uint16_t)(buf + i)] = m_volDrive0[fileOff + i]; }
                    m_cpu.r.SP += 12;      // pop all 6 words (async,blk,len,bufDE,bufHL,unit)
                    m_mem[PM_V(0x02E4)] = 0;     // IORSLT low byte  = 0 (success)
                    m_mem[PM_V(0x02E5)] = 0;     // IORSLT high byte = 0
                    m_cpu.r.PC = 0x03A4;   // BACK1 -- exactly where URTN points in the real routine
                    m_unit4IoNativeCount++;
                } else {
                    m_unit4IoFallbackCount++; // out of range -- let the real driver handle/report it
                }
            }
            // ---- Native-C character-device fast path: units #1 CONSOLE
            // and #2 SYSTERM ----
            // Both share SYSTEM.MICRO's own CHDRVR driver and the same
            // physical CIVECT/COVECT BIOS vectors (CONIN/CONOUT, per
            // CTABLE) -- SYSTERM is only different in that it never
            // echoes a typed character back to the screen and never
            // does the EOF-triggered zero-fill, matching its "non-
            // echoing keyboard" comment in UNITBL.
            //
            // Units #3 (GRAPHICS), #6 (PRINTER), and #7 (REMIN) are
            // deliberately NOT handled here: unit #3 has no valid
            // operations at all (its UNITBL flags are 0, so GETU's own
            // check already rejects it correctly); #6/#7 have no
            // modeled physical device in this emulator (no printer, no
            // remote reader), and pascal.bin's actual BIOS behavior for
            // their vectors (LIST/READER) isn't something I have
            // visibility into to replicate correctly -- so they keep
            // running through the real Z80/BIOS path unchanged, exactly
            // as before this change.
            else if (unit == 1 || unit == 2) {
                bool isConsole = (unit == 1);
                uint16_t buf = (uint16_t)(bufHL + bufDE);
                // EOFBIT and DLEBIT are the same bit (0x04) in UASY --
                // "disable EOF detection" while reading, "disable DLE
                // blank-run decompression" while writing.
                bool suppressDle = (async & 0x04) != 0;
                bool suppressCrLf = (async & 0x08) != 0;

                if (isWrite) {
                    // Real CHDRVR calls ECHO unconditionally on write,
                    // for every unit that reaches it -- not gated by
                    // UNIT==1 the way read-echo and EOF-zero-fill are.
                    for (int i = 0; i < (int)len; i++) {
                        ConsoleEcho(m_mem[(uint16_t)(buf + i)], suppressDle, suppressCrLf);
                    }
                } else {
                    bool eofActive = !suppressDle; // same bit, read-side meaning
                    for (int i = 0; i < (int)len; i++) {
                        uint8_t c = PortIn(1); // CONIN -- blocks for a real keystroke
                        m_mem[(uint16_t)(buf + i)] = c;
                        if (eofActive && c == 0x03 /* SYEOF, ^C */) {
                            if (isConsole) {
                                for (int j = i; j < (int)len; j++) m_mem[(uint16_t)(buf + j)] = 0;
                            }
                            break; // stop early either way -- only CONSOLE zero-fills
                        }
                        if (isConsole) ConsoleEcho(c, suppressDle, suppressCrLf);
                    }
                }
                m_cpu.r.SP += 12;
                m_mem[PM_V(0x02E4)] = 0;
                m_mem[PM_V(0x02E5)] = 0;
                m_cpu.r.PC = 0x03A4;
                m_charIoNativeCount++;
            }
        }

        // ---- A native case just committed (PC == BACK) ----
        // Loop straight back to the top: Site 1 logs and counts the next
        // opcode there and the native BACK shortcut dispatches it. (Until
        // v1.71 this spot logged the opcode as "Site 2" and then let
        // cpu.step() run the real Z80 BACK routine -- about ten Z80
        // instructions for every p-code instruction, 85% of all Z80
        // instructions executed in native mode.) Only in native mode: in
        // Z80 mode the Z80 BACK code is the dispatcher.
        if (m_nativePcodeOps && (m_cpu.r.PC == 0x03B0 || m_cpu.r.PC == 0x03A4 ||   // BACK or BACK1, or a
            (m_cpu.r.PC != nativeEntryPc && (NativeOpcodeForTarget(m_cpu.r.PC) >= 0 || NativeErrorCode(m_cpu.r.PC) >= 0)))) // hand-over (CXP -> CIP)
            continue;

        // ---- Z80 supplemental trace (optional) ----
        // When enabled, logs every Z80 cpu.step() call -- including startup
        // code that runs before the first BACK, and the Z80 BACK-handler
        // instructions that cpu.step() executes between native cases.
        // Prefix "Z80 " distinguishes these lines from P-code lines.
        // No tracingActive gate: we specifically want to see startup code.
        if (m_traceAlsoZ80 && m_tracing && m_traceFile) {
            uint16_t pc = m_cpu.r.PC;
            uint8_t b0 = m_mem[pc], b1 = m_mem[(uint16_t)(pc+1)], b2 = m_mem[(uint16_t)(pc+2)];
            fprintf(m_traceFile, "Z80  PC=%04X %02X %02X %02X  A=%02X BC=%04X DE=%04X HL=%04X SP=%04X F=%02X\n",
                    pc, b0, b1, b2,
                    m_cpu.r.A, m_cpu.r.BC(), m_cpu.r.DE(), m_cpu.r.HL(), m_cpu.r.SP, m_cpu.r.F);
            if (i % 4096 == 0) fflush(m_traceFile);
        }

        m_traceCounter = i;
        if (m_abortAddr != 0 && m_cpu.r.PC == m_abortAddr) { m_systemHalted = true; break; } // ABORT
        if (m_hostClock && m_timAddr != 0 && m_cpu.r.PC == m_timAddr) NativeClockUpdate();  // Z80 TIME reads the clock words
        if (m_reclaimed) {   // no Z80 code exists any more: stop instead of executing garbage
            char b[200];
            snprintf(b, sizeof b, "Z80 code needed at PC=%04X after the interpreter memory was reclaimed "
                     "(P-code IPC=%04X, opcode %02X). Everything the validation suites exercise is native; this path is not.",
                     m_cpu.r.PC, m_cpu.r.BC(), m_mem[m_cpu.r.BC()]);
            m_reclaimFault = b;
            break;
        }
        m_cpu.step();
        if (m_verifyFile && m_cpu.r.SP < m_verifyMinSP) m_verifyMinSP = m_cpu.r.SP;
    }

    m_halted = m_cpu.r.halted;
    m_running = false;
}

void PSystemEngine::Stop() {
    SetEvent(m_stopEvent);
    SetEvent(m_keyAvailable); // wake up a blocked CONIN so RunLoop can exit
}
