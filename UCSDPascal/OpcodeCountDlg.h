// OpcodeCountDlg.h
//
// View > Op Code Count dialog. Shows every p-code opcode that has fired
// this session. A "Handler" column shows "Native" or "Z80" for each
// opcode -- whether it currently has ANY native-C replacement
// (PCodeOpcodeIsNative) or is still running entirely through the Z80
// interpreter -- and is sortable by clicking its header, so clicking
// once groups all Native opcodes together and clicking again groups
// all Z80 opcodes together. Native rows are also drawn in white, Z80
// rows in grey, as a second, always-visible cue independent of sort
// order. All four columns (Opcode, Name, Count, Handler) are sortable
// by clicking their header; clicking the same header again reverses
// the sort direction. A totals line at the bottom sums instruction
// counts actually executed by native C vs by the Z80 core (an opcode
// with a native fast path that still falls through for some cases,
// like CEQU or CSP, contributes to both). Reads live counters off
// PSystemEngine -- press Refresh to re-sample while the emulator keeps
// running (the current sort column/direction is preserved across a
// refresh).
#pragma once

#include "PSystemEngine.h"
#include "resource.h"

class COpcodeCountDlg : public CDialog {
public:
    // engine must outlive the dialog; the dialog only reads from it.
    explicit COpcodeCountDlg(PSystemEngine* engine, CWnd* pParent = nullptr);

    enum { IDD = IDD_OPCODECOUNT };

protected:
    virtual BOOL OnInitDialog();
    virtual void DoDataExchange(CDataExchange* pDX);
    afx_msg void OnRefresh();
    afx_msg void OnCustomDrawList(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnColumnClick(NMHDR* pNMHDR, LRESULT* pResult);
    DECLARE_MESSAGE_MAP()

private:
    struct Row {
        int      opcode;
        uint64_t count;   // total dispatch count (native + Z80 fallback)
        bool     native;  // has ANY native-C replacement -- drives both the
                          // "Handler" column text and the row's text color
        CString  displayName; // if non-empty, overrides PCodeOpcodeName(opcode)
                               // in the Name column -- used for CSP's own
                               // per-selector breakdown (CSP-SQT, CSP-IOC, etc.)
    };

    PSystemEngine* m_engine;
    CListCtrl m_list;
    std::vector<Row> m_rows;   // cached data, independent of on-screen sort order
    int  m_sortColumn = 2;      // default: Count column
    bool m_sortAscending = false; // default: descending (highest first, as before)

    void PopulateList();   // re-query the engine, rebuild m_rows, then sort+display
    void SortRows();       // sort m_rows per m_sortColumn/m_sortAscending
    void DisplayRows();    // rebuild the on-screen list items from m_rows
    void UpdateHeaderSortArrows();
};
