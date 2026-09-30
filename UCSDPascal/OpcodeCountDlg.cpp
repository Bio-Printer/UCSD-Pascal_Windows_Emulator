// OpcodeCountDlg.cpp
#include "stdafx.h"
#include "OpcodeCountDlg.h"
#include "PCodeOpcodes.h"
#include <vector>
#include <algorithm>

BEGIN_MESSAGE_MAP(COpcodeCountDlg, CDialog)
    ON_BN_CLICKED(IDC_OPCODE_REFRESH, &COpcodeCountDlg::OnRefresh)
    ON_NOTIFY(NM_CUSTOMDRAW, IDC_OPCODE_LIST, &COpcodeCountDlg::OnCustomDrawList)
    ON_NOTIFY(LVN_COLUMNCLICK, IDC_OPCODE_LIST, &COpcodeCountDlg::OnColumnClick)
END_MESSAGE_MAP()

COpcodeCountDlg::COpcodeCountDlg(PSystemEngine* engine, CWnd* pParent)
    : CDialog(IDD_OPCODECOUNT, pParent), m_engine(engine) {
}

void COpcodeCountDlg::DoDataExchange(CDataExchange* pDX) {
    CDialog::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_OPCODE_LIST, m_list);
}

BOOL COpcodeCountDlg::OnInitDialog() {
    CDialog::OnInitDialog();

    // Dark background so "white text" (native) and "grey text" (Z80)
    // actually read as bright/dim against each other, matching the
    // terminal's own black background elsewhere in this app.
    m_list.SetExtendedStyle(m_list.GetExtendedStyle() | LVS_EX_FULLROWSELECT);
    m_list.SetBkColor(RGB(0, 0, 0));
    m_list.SetTextBkColor(RGB(0, 0, 0));

    m_list.InsertColumn(0, _T("Opcode"),  LVCFMT_LEFT,  55);
    m_list.InsertColumn(1, _T("Name"),    LVCFMT_LEFT,  75);
    m_list.InsertColumn(2, _T("Count"),   LVCFMT_RIGHT, 66);
    m_list.InsertColumn(3, _T("Handler"), LVCFMT_LEFT,  78);

    PopulateList();
    return TRUE;
}

void COpcodeCountDlg::OnRefresh() {
    PopulateList(); // re-queries the engine, then re-applies the current sort
}

void COpcodeCountDlg::PopulateList() {
    if (!m_engine) { m_list.DeleteAllItems(); return; }

    m_rows.clear();
    uint64_t totalC = 0, totalZ80 = 0;

    // m_engine->DebugOpcodeFetchCount(op) is the total number of times
    // this opcode has been the next p-code instruction dispatched this
    // session, whether it ran via native C or the Z80 core. DebugOpcode-
    // EmulatedCount(op) is how many of those were actually handled by
    // native C (some opcodes, like CEQU or CSP, have a native fast path
    // for common cases but still fall through to Z80 for the rest, so
    // fetch count and emulated count can differ even for opcodes that
    // DO have native code). The totals line below sums both, but the
    // per-row "Handler" column is the simpler, purely categorical fact
    // the user actually wants to sort/scan by: does this opcode have
    // ANY native-C code at all, or is it still 100% Z80?
    for (int op = 0; op < 256; op++) {
        uint64_t fetch    = m_engine->DebugOpcodeFetchCount((uint8_t)op);
        uint64_t emulated = m_engine->DebugOpcodeEmulatedCount((uint8_t)op);
        bool native = PCodeOpcodeIsNative((uint8_t)op);
        totalC   += emulated;
        totalZ80 += (fetch > emulated) ? (fetch - emulated) : 0;
        if (op == 0x9E && fetch > 0) {
            // CSP (Call Standard Procedure) is a single p-code opcode that
            // dispatches to dozens of separate standard-library routines
            // via CSPTBL, keyed by a procedure-number byte. Lumping all of
            // that under one "CSP" row hides which routines actually run,
            // so break it down into one row per selector that has fired
            // (CSP-SQT for square root, CSP-IOC, etc.), using the real
            // dispatch itself as still native (matching CSP's own overall
            // Handler status). For most selectors this only reflects the
            // dispatch -- the routine jumped to remains real Z80 code --
            // but for SIN/COS/LOG/ATAN/LN/EXP/SQT (selectors 25-31) the
            // computation itself is native too (see CSP's own case in
            // PSystemEngine.cpp), so "Native" for those rows means the
            // whole thing, not just the jump.
            //
            // Per-selector counting only happens inside CSP's own native-C
            // case, which is gated on m_nativePcodeOps -- so in Z80 mode
            // (or any other situation where fetch count and selector count
            // could disagree), every DebugCspSelectorCount() reads zero
            // even though fetch is clearly nonzero. Falling through to
            // "continue" unconditionally in that case would silently drop
            // CSP from the list entirely, unlike every other opcode -- so
            // track whether any selector row actually got added, and if
            // not, fall back to one plain "CSP" row using the overall
            // fetch count, exactly like the generic path below does for
            // every other opcode.
            bool anySelectorRow = false;
            for (int sel = 0; sel < 256; sel++) {
                uint64_t selCount = m_engine->DebugCspSelectorCount((uint8_t)sel);
                if (selCount == 0) continue;
                const char* selName = CspSelectorName((uint8_t)sel);
                CString name;
                if (selName) name.Format(_T("CSP-%S"), selName);
                else name.Format(_T("CSP-#%d"), sel);
                m_rows.push_back({ op, selCount, native, name });
                anySelectorRow = true;
            }
            if (!anySelectorRow) m_rows.push_back({ op, fetch, native, CString() });
            continue;
        }
        if (fetch > 0) m_rows.push_back({ op, fetch, native, CString() });
    }

    SortRows();
    DisplayRows();

    CString totals;
    totals.Format(_T("Handled by native C code: %llu     Handled by Z80 code: %llu\r\n")
                  _T("Native unit I/O: unit #4 %llu (native) / %llu (fell through)     units #1+#2 %llu\r\n")
                  _T("Native BIOS linker (CONST/CONIN/CONOUT): %llu"),
                  (unsigned long long)totalC, (unsigned long long)totalZ80,
                  (unsigned long long)m_engine->DebugUnit4IoNativeCount(),
                  (unsigned long long)m_engine->DebugUnit4IoFallbackCount(),
                  (unsigned long long)m_engine->DebugCharIoNativeCount(),
                  (unsigned long long)m_engine->DebugBiosLinkerNativeCount());
    SetDlgItemText(IDC_OPCODE_TOTALS, totals);
}

void COpcodeCountDlg::SortRows() {
    int col = m_sortColumn;
    bool asc = m_sortAscending;

    // Returns <0, 0, or >0 -- a genuine total order per column, so the
    // ascending/descending flip below is a simple sign check rather than
    // an ad-hoc "!less" that would violate strict weak ordering on ties.
    auto compare = [col](const Row& a, const Row& b) -> int {
        switch (col) {
            case 0: // Opcode
                return (a.opcode > b.opcode) - (a.opcode < b.opcode);
            case 1: { // Name (alphabetical, case-insensitive)
                CString na(a.displayName.IsEmpty() ? CString(PCodeOpcodeName((uint8_t)a.opcode)) : a.displayName);
                CString nb(b.displayName.IsEmpty() ? CString(PCodeOpcodeName((uint8_t)b.opcode)) : b.displayName);
                return na.CompareNoCase(nb);
            }
            case 3: // Handler -- native (1) sorts after Z80 (0) when ascending,
                    // so ascending groups "Native" rows together and descending
                    // groups "Z80" rows together; tie-break by opcode so each
                    // group is internally in a stable, predictable order.
                if (a.native != b.native) return a.native ? 1 : -1;
                return (a.opcode > b.opcode) - (a.opcode < b.opcode);
            case 2: // Count
            default:
                return (a.count > b.count) - (a.count < b.count);
        }
    };
    std::sort(m_rows.begin(), m_rows.end(), [&](const Row& a, const Row& b) {
        int c = compare(a, b);
        return asc ? (c < 0) : (c > 0);
    });
}

void COpcodeCountDlg::DisplayRows() {
    m_list.DeleteAllItems();
    int idx = 0;
    for (const auto& r : m_rows) {
        CString opcodeText;
        opcodeText.Format(_T("0x%02X"), r.opcode);
        int item = m_list.InsertItem(idx++, opcodeText);
        m_list.SetItemText(item, 1, r.displayName.IsEmpty() ? CString(PCodeOpcodeName((uint8_t)r.opcode)) : r.displayName);
        CString countText;
        countText.Format(_T("%llu"), (unsigned long long)r.count);
        m_list.SetItemText(item, 2, countText);
        m_list.SetItemText(item, 3, r.native ? _T("Native") : _T("Z80"));
        // Stashed for OnCustomDrawList to pick the text color by.
        m_list.SetItemData(item, r.native ? 1 : 0);
    }
    UpdateHeaderSortArrows();
}

void COpcodeCountDlg::UpdateHeaderSortArrows() {
    CHeaderCtrl* header = m_list.GetHeaderCtrl();
    if (!header) return;
    int colCount = header->GetItemCount();
    for (int c = 0; c < colCount; c++) {
        HDITEM hdi = {};
        hdi.mask = HDI_FORMAT;
        header->GetItem(c, &hdi);
        hdi.fmt &= ~(HDF_SORTUP | HDF_SORTDOWN);
        if (c == m_sortColumn)
            hdi.fmt |= (m_sortAscending ? HDF_SORTUP : HDF_SORTDOWN);
        header->SetItem(c, &hdi);
    }
}

void COpcodeCountDlg::OnColumnClick(NMHDR* pNMHDR, LRESULT* pResult) {
    LPNMLISTVIEW pNMLV = reinterpret_cast<LPNMLISTVIEW>(pNMHDR);
    int col = pNMLV->iSubItem;
    if (col == m_sortColumn) {
        m_sortAscending = !m_sortAscending; // same column clicked again -- reverse
    } else {
        m_sortColumn = col;
        // Count: default to descending (largest first) -- the most useful
        // initial view for a frequency table. Opcode/Name/Handler: default
        // to ascending (numeric order, alphabetical order, and for Handler
        // specifically, ascending groups "Z80" opcodes first -- the ones
        // still needing native-C conversion, which is usually what you
        // want to scan first).
        m_sortAscending = (col == 0 || col == 1 || col == 3);
    }
    SortRows();
    DisplayRows();
    *pResult = 0;
}

void COpcodeCountDlg::OnCustomDrawList(NMHDR* pNMHDR, LRESULT* pResult) {
    LPNMLVCUSTOMDRAW pLVCD = reinterpret_cast<LPNMLVCUSTOMDRAW>(pNMHDR);
    switch (pLVCD->nmcd.dwDrawStage) {
        case CDDS_PREPAINT:
            *pResult = CDRF_NOTIFYITEMDRAW;
            return;
        case CDDS_ITEMPREPAINT: {
            bool native = pLVCD->nmcd.lItemlParam != 0;
            pLVCD->clrText = native ? RGB(255, 255, 255) : RGB(160, 160, 160);
            pLVCD->clrTextBk = RGB(0, 0, 0);
            *pResult = CDRF_DODEFAULT;
            return;
        }
        default:
            *pResult = CDRF_DODEFAULT;
            return;
    }
}
