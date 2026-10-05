// run_verify.cpp -- Linux test runner for "Verify P-System": boots the REAL
// PSystemEngine (via verify/linux-shim) on fresh copies of the disks and runs
// a keyboard script with VerifyRunner, exactly as the GUI option does.
//   run_verify <data dir> <SOURCE.BLK> <COMPASM.BLK> <script> <z80|native> <work dir> [record:<log>|compare:<log>] [max seconds]
#include "PSystemVerify.h"
#include <thread>
#include <filesystem>
#include <chrono>
#include <iostream>
#include <sstream>
static std::wstring W(const std::string& s) { return std::wstring(s.begin(), s.end()); }
int main(int argc, char** argv) {
    if (argc < 7) { fprintf(stderr, "usage: run_verify <data dir> <SOURCE.BLK> <COMPASM.BLK> <script> <z80|native> <work dir> [trace] [max s]\n"); return 2; }
    std::string data = argv[1], work = argv[6], trace = argc > 7 ? argv[7] : "";
    int maxSec = argc > 8 ? atoi(argv[8]) : 7200;
    std::ifstream sf(argv[4]); std::stringstream ss; ss << sf.rdbuf();
    std::vector<VerifyStep> steps; std::string err;
    if (!ParseVerifyScript(ss.str(), steps, err)) { fprintf(stderr, "script: %s\n", err.c_str()); return 2; }
    if (!PrepareVerifyDisks(data + "/Big_Disk.BLK", argv[2], argv[3], work, err)) { fprintf(stderr, "%s\n", err.c_str()); return 2; }
    PSystemEngine e; std::wstring werr;
    if (!e.LoadFiles(W(data + "/pascal.bin"), W(work + "/VERIFY_BOOT.BLK"), W(work + "/VERIFY_SOURCE.BLK"), werr) ||
        !e.MountUnit9(W(work + "/VERIFY_COMPASM.BLK"), werr)) { fprintf(stderr, "load failed\n"); return 2; }
    if (const char* u10 = getenv("VERIFY_UNIT10")) {          // a volume on unit #10 (used in place)
        std::wstring uerr;
        if (!e.MountUnit10(W(u10), uerr)) { fprintf(stderr, "cannot mount unit #10\n"); return 2; }
    }
    for (int u = 11; u <= 14; u++) {                           // VERIFY_UNIT11..14: volumes on units 11-14 (used in place)
        const std::string var = "VERIFY_UNIT" + std::to_string(u);
        if (const char* p = getenv(var.c_str())) {
            std::wstring uerr;
            if (!e.MountUnit(u, W(p), uerr)) { fprintf(stderr, "cannot mount unit #%d\n", u); return 2; }
        }
    }
    // VERIFY_IMPORT="unit:path": Options > Import File, before booting -- or,
    // with VERIFY_IMPORT_STEP=n, while the system runs, when the script
    // reaches step n (counting from 0)
    auto doImport = [&]() {
        const char* imp = getenv("VERIFY_IMPORT");
        std::string spec = imp; size_t c = spec.find(':');
        std::wstring ierr = e.ImportFileToVolume(atoi(spec.substr(0, c).c_str()), W(spec.substr(c + 1)));
        fprintf(stderr, "import %s: %s\n", imp, ierr.empty() ? "OK" : std::string(ierr.begin(), ierr.end()).c_str());
    };
    const long importStep = getenv("VERIFY_IMPORT") && getenv("VERIFY_IMPORT_STEP") ? atol(getenv("VERIFY_IMPORT_STEP")) : -1;
    if (getenv("VERIFY_IMPORT") && importStep < 0) doImport();
    bool imported = false;
    // VERIFY_TOUCH="step:path": at script step n, change the file's last-write
    // time from outside the engine, as a git pull or a copy would
    const long touchStep = getenv("VERIFY_TOUCH") ? atol(getenv("VERIFY_TOUCH")) : -1;
    bool touched = false;
    // VERIFY_PAUSE="step:ms": at script step n, pause the engine (Options >
    // Pause) for ms milliseconds and report whether any P-code ran meanwhile
    const long pauseStep = getenv("VERIFY_PAUSE") ? atol(getenv("VERIFY_PAUSE")) : -1;
    bool pauseDone = false;
    auto pcodeCount = [&]() { uint64_t n = 0; for (int op = 0; op < 256; op++) n += e.DebugOpcodeEmulatedCount((uint8_t)op); return n; };
    bool native = std::string(argv[5]) == "native";
    e.SetNativePcodeOps(native);
    e.SetPreserveZ80RegisterCompat(getenv("VERIFY_COMPAT") != nullptr);
    if (getenv("VERIFY_NOCOPROC")) e.SetZ80Coprocessor(false);   // Z80 mode without the coprocessor (the original interpreter alone)
    bool compare = trace.rfind("compare:", 0) == 0, record = trace.rfind("record:", 0) == 0;
    if (trace.rfind("text:", 0) == 0) { e.SetTraceAlsoZ80(true); if (!e.EnableTrace(W(trace.substr(5)), werr)) return 2; }
    if ((compare || record) && !e.StartVerifyLog(W(trace.substr(compare ? 8 : 7)), compare, werr)) { fprintf(stderr, "cannot open the verify log\n"); return 2; }
    if (getenv("VERIFY_STOP_AT")) e.SetVerifyStopAt(strtoull(getenv("VERIFY_STOP_AT"), nullptr, 10));
    if (getenv("VERIFY_RECLAIM")) e.SetReclaimInterpreterMemory(true);
    if (getenv("VERIFY_HOSTCLOCK")) e.SetHostClock(true);    // the PC's date and time, as the GUI (not deterministic: off by default)
    const char* hv = getenv("VERIFY_HARVARD");          // with VERIFY_RECLAIM, native mode: code in its own I-space
    const bool harvard = hv && *hv && strcmp(hv, "0") != 0;   // (unset, empty or 0: off)
    if (harvard) e.SetHarvard(true);
    e.SetConsoleCapture(true);
    // VERIFY_LOWWATER=1: the least free memory (SP - NP) of the run, and the step it was reached in;
    // =2: also each step's own least (tracking restarts at each step: the
    // instructions between a step's end and the restart, at most a poll's
    // worth, are not seen)
    const char* lwv = getenv("VERIFY_LOWWATER");
    const bool lowWater = lwv != nullptr, lowEachStep = lwv && strcmp(lwv, "2") == 0;
    if (lowWater) e.SetLowWater(true);
    int lowWords = -1; std::string lowStep;
    VerifyRunner r(e, steps);
    std::thread t([&] { e.RunLoop(); });
    auto t0 = std::chrono::steady_clock::now();
    VerifyRunner::State st;
    size_t lastStep = (size_t)-1;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    while ((st = r.Poll()) == VerifyRunner::RUNNING) {
        if (e.VerifyMismatch()) break;                        // engine stopped at a verify mismatch
        if (!e.IsRunning()) { st = r.Poll(); break; }         // engine stopped (e.g. HALTED): let the script see it
        if (importStep >= 0 && !imported && r.StepIndex() >= (size_t)importStep) { imported = true; doImport(); }
        if (touchStep >= 0 && !touched && r.StepIndex() >= (size_t)touchStep) {   // "git pull" behind the emulator's back
            touched = true;
            std::string spec = getenv("VERIFY_TOUCH"); std::string p = spec.substr(spec.find(':') + 1);
            std::error_code ec;
            std::filesystem::last_write_time(p, std::filesystem::last_write_time(p, ec) + std::chrono::hours(1), ec);
            fprintf(stderr, "touched %s: %s\n", p.c_str(), ec ? "FAILED" : "OK");
        }
        if (pauseStep >= 0 && !pauseDone && r.StepIndex() >= (size_t)pauseStep) {
            pauseDone = true;
            std::string spec = getenv("VERIFY_PAUSE"); const long ms = atol(spec.substr(spec.find(':') + 1).c_str());
            const uint64_t before = pcodeCount();
            e.SetPaused(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));       // reaches the next check (4096 steps) or a key wait
            const uint64_t a = pcodeCount();
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            const uint64_t b = pcodeCount();
            const bool waiting = e.IsPaused();
            e.SetPaused(false);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            fprintf(stderr, "pause at step %ld: %llu P-code instructions before the pause took hold, %llu during %ld ms paused "
                    "(engine waiting: %s), %llu in 200 ms after resume\n", pauseStep, (unsigned long long)(a - before),
                    (unsigned long long)(b - a), ms, waiting ? "yes" : "no (blocked for a key)", (unsigned long long)(pcodeCount() - b));
        }
        if (lowWater) {
            const int w = e.LowWaterWords();
            if (w >= 0 && (lowWords < 0 || w < lowWords)) {
                lowWords = w;
                lowStep = "step " + std::to_string(r.StepIndex() + 1) + ": " + r.CurrentStepText();
            }
            if (lowEachStep && r.StepIndex() != lastStep) {
                if (lastStep != (size_t)-1) printf("low water: step %zu: %d words\n", lastStep + 1, w);
                e.ResetLowWater();
            }
        }
        if (r.StepIndex() != lastStep) {
            lastStep = r.StepIndex();
            double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            fprintf(stderr, "[%7.1fs] step %zu/%zu  %s\n", el, r.StepIndex() + 1, r.StepCount(), r.CurrentStepText().c_str());
        }
        if (std::chrono::steady_clock::now() - t0 > std::chrono::seconds(maxSec)) { fprintf(stderr, "TIMEOUT\n"); break; }
        std::this_thread::sleep_for(std::chrono::microseconds(300));
    }
    // End the log at a precisely defined point: the script is done and the
    // system is blocked waiting for a key (no instruction is executing), so
    // every run's log ends at the same instruction. Only then stop -- which
    // releases the key wait and lets a few more, unlogged, instructions run.
    if (st == VerifyRunner::DONE) {
        for (int k = 0; k < 20000 && !(e.IsWaitingForKey() && e.PendingKeyCount() == 0) && !e.IsSystemHalted(); k++)
            std::this_thread::sleep_for(std::chrono::microseconds(250));
    }
    e.StopVerifyLog();
    e.Stop(); t.join();
    if (getenv("VERIFY_DUMP_MEM")) {
        FILE* mf = fopen(getenv("VERIFY_DUMP_MEM"), "wb"); fwrite(e.DebugMemory(), 1, 65536, mf); fclose(mf);
        const Z80Regs& g = e.DebugRegs();
        std::string rp = std::string(getenv("VERIFY_DUMP_MEM")) + ".regs";
        FILE* rf = fopen(rp.c_str(), "w");
        fprintf(rf, "A=%02X F=%02X BC=%04X DE=%04X HL=%04X IX=%04X IY=%04X SP=%04X PC=%04X I=%02X R=%02X IM=%d A'=%02X F'=%02X BC'=%02X%02X DE'=%02X%02X HL'=%02X%02X\n",
                g.A, g.F, g.BC(), g.DE(), g.HL(), g.IX, g.IY, g.SP, g.PC, g.I, g.R, g.IM, g.A_, g.F_, g.B_, g.C_, g.D_, g.E_, g.H_, g.L_);
        fclose(rf);
    }
    if (!e.BootFault().empty()) printf("BOOT FAULT: %s\n", e.BootFault().c_str());
    if (e.NativelyBooted()) printf("native boot: yes\n");
    else if (!e.NativeBootNote().empty()) printf("native boot: NO -- %s\n", e.NativeBootNote().c_str());
    double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::string tr = r.Transcript();
    FILE* tf = fopen((work + "/transcript.txt").c_str(), "wb");
    for (unsigned char c : tr) { if (c == '\r') c = '\n'; if ((c >= 32 && c < 127) || c == '\n') fputc(c, tf); }
    fclose(tf);
    printf("P-code instructions %s: %llu\n", compare ? "compared" : (record ? "recorded" : "run"), (unsigned long long)e.VerifyRecordCount());
    if (lowWater) {
        const int w = e.LowWaterWords();
        if (w >= 0 && (lowWords < 0 || w < lowWords)) { lowWords = w; lowStep = "the end"; }
        if (lowEachStep && lastStep != (size_t)-1) printf("low water: step %zu: %d words\n", lastStep + 1, w);
        printf("least free memory: %d words (SP - NP; reached during %s)\n", lowWords, lowStep.c_str());
    }
    if (!native) printf("z80 coprocessor: %llu CSPs (doubles, CALLI, 4-byte real functions)\n", (unsigned long long)e.Z80CoprocessorCalls());
    if (harvard) printf("harvard layout: %s, %d segments on the code stack, %u bytes of I-space free\n",
                                         e.HarvardActive() ? "yes" : "NO", e.HarvardCodeSegments(), (unsigned)e.HarvardCodeFree());
    if (getenv("VERIFY_RECLAIM")) printf("interpreter memory reclaimed: %s%s%s\n", e.InterpreterMemoryReclaimed() ? "yes" : "NO",
                                          e.ReclaimFault().empty() ? "" : " -- STOPPED: ", e.ReclaimFault().c_str());
    if (e.VerifyMismatch()) { printf("VERIFY MISMATCH after %.1f s:\n%s\n", el, e.VerifyMismatchText().c_str()); return 3; }
    if (st == VerifyRunner::DONE) { printf("VERIFY SCRIPT COMPLETED: %zu steps, %zu keys, %.1f s (%s mode)\n", r.StepCount(), r.KeysTyped(), el, native ? "native" : "Z80"); return 0; }
    printf("VERIFY SCRIPT FAILED after %.1f s: %s\n", el, st == VerifyRunner::FAILED ? r.Error().c_str() : "timeout");
    if (st != VerifyRunner::FAILED) {
        const uint8_t* m = e.DebugMemory();
        printf("  engine at timeout: Z80 PC=%04X halted=%d waitingForKey=%d  IPC(BC)=? SP=? MP=%04X SEGP=%04X\n",
               e.DebugPC(), (int)e.HasHalted(), (int)e.IsWaitingForKey(), m[0x02F2] | (m[0x02F3] << 8), m[0x02F6] | (m[0x02F7] << 8));
    }
    return 1;
}
