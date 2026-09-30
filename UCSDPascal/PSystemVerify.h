// PSystemVerify.h -- "Verify P-System": run a keyboard script against the
// P-System deterministically. Shared by the GUI (MainFrm) and the Linux
// test runner (verify/run_verify.cpp). Header-only.
//
// SCRIPT FORMAT (verify/VERIFY.SCRIPT), one step per line:
//   # comment                 (blank lines are ignored too)
//   WAIT "text"               wait until this text appears in the console
//                             output (searching after the previous WAIT)
//   TYPE "keys"               type these keys
//   HALTED                    the P-System must halt here (it reaches ABORT:
//                             its main program ended) -- the end of a script
//   Escapes inside "...":  \r  carriage return    \e  ESC    \\  \"
//                          \xNN  any byte (hex)
//
// DETERMINISM. Keys are handed to the system ONLY while it is blocked in
// CONIN waiting for a key with nothing queued -- one key at a time. So the
// system always sees exactly the same input at exactly the same point of
// its instruction stream, however fast or slow the host is, and nothing it
// does can discard type-ahead (the system flushes type-ahead while
// starting, for example). Nothing in the emulator reads the clock, so with
// identical starting disks every run executes the identical instruction
// stream -- which is what makes its P-code log comparable.
//
// RESILIENCE. Every TYPE is preceded by the WAIT for the prompt it answers.
// If the system stops for input WITHOUT having shown the expected text
// (a compile error, an unexpected question), the run stops at once with a
// message showing what was expected and what the screen actually said,
// instead of typing into the wrong prompt.
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include "PSystemEngine.h"

// Fresh copies of the three disks for a verification run, so every run
// starts from the same state and the originals are never touched. The boot
// disk copy has any work file (SYSTEM.WRK.TEXT / SYSTEM.WRK.CODE) removed
// from its directory, so no program ever asks "Throw away current
// workfile?" and every prompt the script meets is known in advance.
inline bool PrepareVerifyDisks(const std::filesystem::path& bootDisk, const std::filesystem::path& sourceVol,
                               const std::filesystem::path& compasmVol, const std::filesystem::path& workDir,
                               std::string& err) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(workDir, ec);
    const fs::path outs[3] = { workDir / "VERIFY_BOOT.BLK", workDir / "VERIFY_SOURCE.BLK", workDir / "VERIFY_COMPASM.BLK" };
    const fs::path ins[3] = { bootDisk, sourceVol, compasmVol };
    for (int i = 0; i < 3; i++) {
        fs::copy_file(ins[i], outs[i], fs::copy_options::overwrite_existing, ec);
        if (ec) { err = "cannot copy " + ins[i].string() + " -> " + outs[i].string() + ": " + ec.message(); return false; }
    }
    std::fstream f(outs[0], std::ios::in | std::ios::out | std::ios::binary);
    if (!f) { err = "cannot open " + outs[0].string(); return false; }
    uint8_t dir[2048];
    f.seekg(1024); f.read((char*)dir, sizeof dir);
    if (!f) { err = "cannot read the boot disk directory"; return false; }
    int n = dir[16] | (dir[17] << 8);
    if (n < 0 || n > 77) { err = "boot disk directory looks invalid"; return false; }
    for (int i = 1; i <= n; ) {
        uint8_t* e = dir + 26 * i;
        std::string name((const char*)e + 7, (size_t)(e[6] <= 15 ? e[6] : 0));
        if (name == "SYSTEM.WRK.TEXT" || name == "SYSTEM.WRK.CODE") {
            memmove(e, e + 26, (size_t)(26 * (n - i)));
            memset(dir + 26 * n, 0, 26);
            n--;
        } else i++;
    }
    dir[16] = (uint8_t)n; dir[17] = (uint8_t)(n >> 8);
    f.seekp(1024); f.write((const char*)dir, sizeof dir);
    if (!f) { err = "cannot write the boot disk directory"; return false; }
    return true;
}

struct VerifyStep {
    enum Kind { WAIT, TYPE, HALTED } kind;
    std::string text;
    int line;
};

inline bool ParseVerifyScript(const std::string& src, std::vector<VerifyStep>& out, std::string& err) {
    out.clear();
    int lineNo = 0;
    size_t pos = 0;
    while (pos <= src.size()) {
        size_t eol = src.find('\n', pos);
        std::string line = src.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = (eol == std::string::npos) ? src.size() + 1 : eol + 1;
        lineNo++;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t i = 0;
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
        if (i >= line.size() || line[i] == '#') continue;
        size_t kw = i;
        while (i < line.size() && line[i] != ' ' && line[i] != '\t') i++;
        std::string word = line.substr(kw, i - kw);
        VerifyStep st;
        st.line = lineNo;
        if (word == "HALTED") {
            st.kind = VerifyStep::HALTED; st.text = "HALTED";
            out.push_back(st);
            continue;
        }
        if (word == "WAIT") st.kind = VerifyStep::WAIT;
        else if (word == "TYPE") st.kind = VerifyStep::TYPE;
        else { err = "line " + std::to_string(lineNo) + ": expected WAIT, TYPE or HALTED, found '" + word + "'"; return false; }
        while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
        if (i >= line.size() || line[i] != '"') { err = "line " + std::to_string(lineNo) + ": missing opening quote"; return false; }
        i++;
        std::string text;
        bool closed = false;
        while (i < line.size()) {
            char c = line[i++];
            if (c == '"') { closed = true; break; }
            if (c != '\\') { text += c; continue; }
            if (i >= line.size()) break;
            char e = line[i++];
            if (e == 'r') text += '\r';
            else if (e == 'e') text += '\x1B';
            else if (e == '\\') text += '\\';
            else if (e == '"') text += '"';
            else if (e == 'x' && i + 1 < line.size() + 1) {
                unsigned v = 0;
                for (int k = 0; k < 2 && i < line.size(); k++) {
                    char h = line[i++];
                    v = v * 16 + (unsigned)((h >= '0' && h <= '9') ? h - '0' : (h | 0x20) - 'a' + 10);
                }
                text += (char)v;
            } else { err = "line " + std::to_string(lineNo) + ": unknown escape \\" + e; return false; }
        }
        if (!closed) { err = "line " + std::to_string(lineNo) + ": missing closing quote"; return false; }
        if (text.empty()) { err = "line " + std::to_string(lineNo) + ": empty text"; return false; }
        st.text = text;
        out.push_back(st);
    }
    if (out.empty()) { err = "script has no steps"; return false; }
    return true;
}

class VerifyRunner {
public:
    enum State { RUNNING, DONE, FAILED };

    VerifyRunner(PSystemEngine& e, const std::vector<VerifyStep>& steps) : m_e(e), m_steps(steps) {}

    // Call periodically (GUI timer / test loop). Types at most one key per call.
    State Poll() {
        if (m_state != RUNNING) return m_state;
        while (m_step < m_steps.size()) {
            const VerifyStep& s = m_steps[m_step];
            bool idle = m_e.IsWaitingForKey() && m_e.PendingKeyCount() == 0;
            m_out += m_e.TakeConsoleCapture();      // after reading idle: nothing can be missed
            if (s.kind == VerifyStep::HALTED) {
                if (m_e.IsSystemHalted()) { m_step++; continue; }
                if (idle) return Fail("expected the P-System to halt, but it is waiting for input");
                return RUNNING;
            }
            if (m_e.IsSystemHalted()) return Fail("the P-System halted (ABORT) unexpectedly");
            if (s.kind == VerifyStep::WAIT) {
                size_t p = m_out.find(s.text, m_from);
                if (p != std::string::npos) { m_from = p + s.text.size(); m_step++; continue; }
                if (idle) return Fail("expected \"" + Printable(s.text) + "\" but the system is waiting for input");
                return RUNNING;
            }
            if (!idle) return RUNNING;
            m_e.PostKey((uint8_t)s.text[m_typed]);
            m_keysTyped++;
            if (++m_typed == s.text.size()) { m_typed = 0; m_step++; }
            return RUNNING;
        }
        return m_state = DONE;
    }

    size_t StepIndex() const { return m_step; }
    size_t StepCount() const { return m_steps.size(); }
    size_t KeysTyped() const { return m_keysTyped; }
    const std::string& Error() const { return m_error; }
    const std::string& Transcript() const { return m_out; }
    std::string CurrentStepText() const {
        if (m_step >= m_steps.size()) return "done";
        const VerifyStep& s = m_steps[m_step];
        if (s.kind == VerifyStep::HALTED) return "HALTED (script line " + std::to_string(s.line) + ")";
        return std::string(s.kind == VerifyStep::WAIT ? "WAIT \"" : "TYPE \"") + Printable(s.text) + "\" (script line " + std::to_string(s.line) + ")";
    }

    static std::string Printable(const std::string& t) {
        std::string r;
        for (unsigned char c : t) {
            if (c == '\r') r += "\\r"; else if (c == 0x1B) r += "\\e";
            else if (c < 32 || c > 126) { char b[8]; snprintf(b, sizeof b, "\\x%02X", c); r += b; }
            else r += (char)c;
        }
        return r;
    }

private:
    State Fail(const std::string& why) {
        std::string tail = m_out.size() > 400 ? m_out.substr(m_out.size() - 400) : m_out;
        std::string clean;
        for (unsigned char c : tail) clean += (c == '\r' || c == '\n') ? '\n' : ((c >= 32 && c < 127) ? (char)c : ' ');
        m_error = "Script line " + std::to_string(m_steps[m_step].line) + ": " + why + ".\nLast console output:\n" + clean;
        return m_state = FAILED;
    }
    PSystemEngine& m_e;
    std::vector<VerifyStep> m_steps;
    size_t m_step = 0, m_typed = 0, m_from = 0, m_keysTyped = 0;
    std::string m_out, m_error;
    State m_state = RUNNING;
};
