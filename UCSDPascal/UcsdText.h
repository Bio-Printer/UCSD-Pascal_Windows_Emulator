// UcsdText.h
//
// Convert an ordinary Windows (or Unix) text file into the on-disk format of
// a UCSD Pascal II.0 .TEXT file, for ImportFileToVolume. Header-only and
// free of Windows/MFC dependencies so it can also be compiled and tested on
// its own.
//
// UCSD .TEXT format, as written by the system editor (checked against the
// files on the U132-A volume):
//   * Blocks 0-1 (1024 bytes): editor header. Written here as all zeros,
//     which the editor accepts (it fills in its own settings on save).
//   * Then 1024-byte pages. Each page holds only WHOLE lines, and the
//     unused rest of the page is NUL-filled; a line never crosses a page
//     boundary. The file is therefore always an even number of blocks and
//     its last block is full (DLASTBYTE = 512).
//   * Each line ends in a single CR (0x0D) -- no LF.
//   * Leading indentation is stored as DLE (0x10) followed by (32 + number
//     of spaces), exactly like the editor writes it (it writes the DLE pair
//     even for zero indentation).
//
// Conversion choices for the Windows side:
//   * CR LF, lone LF and lone CR are all accepted as line ends.
//   * Tabs expand to 8-column stops (UCSD text has no tab character).
//   * Trailing blanks are dropped; a UTF-8 byte-order mark is skipped;
//     a DOS ^Z (0x1A) ends the file.
//   * Other control characters and bytes >= 0x7F become '?'.
//   * A line too long for one page (> 1000 characters) is split.
#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

inline std::vector<uint8_t> ConvertToUcsdText(const std::vector<uint8_t>& in) {
    std::vector<uint8_t> out(1024, 0);                 // editor header
    std::vector<uint8_t> page;
    page.reserve(1024);
    auto flushPage = [&]() {
        page.resize(1024, 0);                           // NUL-fill the rest
        out.insert(out.end(), page.begin(), page.end());
        page.clear();
    };
    auto addRecord = [&](const std::vector<uint8_t>& rec) {
        // keep at least one NUL at the end of every page
        if (page.size() + rec.size() > 1023) flushPage();
        page.insert(page.end(), rec.begin(), rec.end());
    };

    size_t i = 0, n = in.size();
    if (n >= 3 && in[0] == 0xEF && in[1] == 0xBB && in[2] == 0xBF) i = 3;
    bool eof = false;
    while (i < n && !eof) {
        std::vector<uint8_t> line;
        size_t col = 0;
        while (i < n && in[i] != '\r' && in[i] != '\n') {
            uint8_t c = in[i++];
            if (c == 0x1A) { eof = true; break; }           // DOS end-of-file
            if (c == '\t') {
                do { line.push_back(' '); col++; } while (col % 8 != 0);
                continue;
            }
            if (c < 0x20 || c > 0x7E) c = '?';
            line.push_back(c);
            col++;
        }
        bool hadEnd = false;
        if (!eof && i < n && in[i] == '\r') { i++; hadEnd = true; if (i < n && in[i] == '\n') i++; }
        else if (!eof && i < n && in[i] == '\n') { i++; hadEnd = true; }
        if (!hadEnd && line.empty()) break;                 // nothing after the last line end

        while (!line.empty() && line.back() == ' ') line.pop_back();
        if (line.empty()) {                                 // blank line: just CR
            addRecord(std::vector<uint8_t>{ 0x0D });
            continue;
        }
        size_t lead = 0;
        while (lead < line.size() && line[lead] == ' ') lead++;
        size_t indent = (lead > 223) ? 223 : lead;          // DLE count byte must stay < 256
        size_t pos = indent;
        bool first = true;
        while (pos < line.size()) {
            size_t take = line.size() - pos;
            if (take > 1000) take = 1000;
            std::vector<uint8_t> rec;
            rec.push_back(0x10);
            rec.push_back((uint8_t)(32 + (first ? indent : 0)));
            rec.insert(rec.end(), line.begin() + pos, line.begin() + pos + take);
            rec.push_back(0x0D);
            addRecord(rec);
            pos += take;
            first = false;
        }
    }
    if (!page.empty() || out.size() == 1024) flushPage();  // at least one page
    return out;
}
