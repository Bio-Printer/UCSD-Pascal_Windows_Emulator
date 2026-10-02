// MainFrm.cpp
#include "stdafx.h"
#include "UCSDPascalApp.h"
#include "MainFrm.h"
#include "Version.h"
#include "OpcodeCountDlg.h"

// WM_MENURHBUTTONUP: right-click on a menu item while a popup menu is open.
// Added in the Windows Vista SDK; not present in some older MinGW headers.
#ifndef WM_MENURHBUTTONUP
#define WM_MENURHBUTTONUP 0x0122
#endif

// Named constant for the deferred "show unmount popup" message.
// ON_MESSAGE requires a simple #define, not an inline arithmetic expression,
// to satisfy the MFC _messageEntries const-array initialiser.
#define WM_SHOW_UNMOUNT_MENU (WM_APP + 2)

namespace {
    UINT indicators[] = { ID_SEPARATOR };

    std::wstring ExeDir() {
        wchar_t path[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
        if (n == 0 || n == MAX_PATH) return L".";
        std::wstring s(path, n);
        size_t pos = s.find_last_of(L"\\/");
        return (pos == std::wstring::npos) ? L"." : s.substr(0, pos);
    }
}

BEGIN_MESSAGE_MAP(CMainFrame, CFrameWnd)
    ON_WM_CREATE()
    ON_WM_DESTROY()
    ON_WM_CLOSE()
    ON_WM_PAINT()
    ON_WM_ERASEBKGND()
    ON_WM_SIZE()
    ON_WM_CHAR()
    ON_WM_KEYDOWN()
    ON_WM_LBUTTONDOWN()
    ON_WM_MOUSEMOVE()
    ON_WM_LBUTTONUP()
    ON_WM_RBUTTONUP()
    ON_WM_TIMER()
    ON_COMMAND(ID_FILE_OPEN_BIGDISK, &CMainFrame::OnFileOpenBigDisk)
    ON_COMMAND(ID_FILE_OPEN_SCRATCH, &CMainFrame::OnFileOpenScratch)
    ON_COMMAND(ID_FILE_OPEN_UNIT9, &CMainFrame::OnFileOpenUnit9)
    ON_COMMAND(ID_FILE_OPEN_UNIT10, &CMainFrame::OnFileOpenUnit10)
    ON_COMMAND(ID_FILE_OPEN_UNIT11, &CMainFrame::OnFileOpenUnit11)
    ON_COMMAND(ID_FILE_OPEN_UNIT12, &CMainFrame::OnFileOpenUnit12)
    ON_COMMAND(ID_FILE_EXIT, &CMainFrame::OnFileExit)
    ON_COMMAND(ID_FILE_UNMOUNT_4,  &CMainFrame::OnFileUnmount4)
    ON_COMMAND(ID_FILE_UNMOUNT_5,  &CMainFrame::OnFileUnmount5)
    ON_COMMAND(ID_FILE_UNMOUNT_9,  &CMainFrame::OnFileUnmount9)
    ON_COMMAND(ID_FILE_UNMOUNT_10, &CMainFrame::OnFileUnmount10)
    ON_COMMAND(ID_FILE_UNMOUNT_11, &CMainFrame::OnFileUnmount11)
    ON_COMMAND(ID_FILE_UNMOUNT_12, &CMainFrame::OnFileUnmount12)
    ON_UPDATE_COMMAND_UI(ID_FILE_UNMOUNT_4,  &CMainFrame::OnUpdateFileUnmount4)
    ON_UPDATE_COMMAND_UI(ID_FILE_UNMOUNT_5,  &CMainFrame::OnUpdateFileUnmount5)
    ON_UPDATE_COMMAND_UI(ID_FILE_UNMOUNT_9,  &CMainFrame::OnUpdateFileUnmount9)
    ON_UPDATE_COMMAND_UI(ID_FILE_UNMOUNT_10, &CMainFrame::OnUpdateFileUnmount10)
    ON_UPDATE_COMMAND_UI(ID_FILE_UNMOUNT_11, &CMainFrame::OnUpdateFileUnmount11)
    ON_UPDATE_COMMAND_UI(ID_FILE_UNMOUNT_12, &CMainFrame::OnUpdateFileUnmount12)
    ON_UPDATE_COMMAND_UI(ID_FILE_OPEN_BIGDISK, &CMainFrame::OnUpdateFileOpenBigDisk)
    ON_UPDATE_COMMAND_UI(ID_FILE_OPEN_SCRATCH, &CMainFrame::OnUpdateFileOpenScratch)
    ON_UPDATE_COMMAND_UI(ID_FILE_OPEN_UNIT9,   &CMainFrame::OnUpdateFileOpenUnit9)
    ON_UPDATE_COMMAND_UI(ID_FILE_OPEN_UNIT10,  &CMainFrame::OnUpdateFileOpenUnit10)
    ON_UPDATE_COMMAND_UI(ID_FILE_OPEN_UNIT11,  &CMainFrame::OnUpdateFileOpenUnit11)
    ON_UPDATE_COMMAND_UI(ID_FILE_OPEN_UNIT12,  &CMainFrame::OnUpdateFileOpenUnit12)
    ON_MESSAGE(WM_MENURHBUTTONUP,     &CMainFrame::OnMenuRightButtonUp)
    ON_MESSAGE(WM_SHOW_UNMOUNT_MENU,  &CMainFrame::OnShowUnmountMenu)
    ON_COMMAND(ID_OPTIONS_TRACE, &CMainFrame::OnOptionsTrace)
    ON_COMMAND(ID_OPTIONS_IMPORT_FILE, &CMainFrame::OnOptionsImportFile)
    ON_COMMAND(ID_OPTIONS_EXPORT_FILE, &CMainFrame::OnOptionsExportFile)
    ON_COMMAND(ID_OPTIONS_EXECMODE_PCODE, &CMainFrame::OnOptionsExecModePCode)
    ON_COMMAND(ID_OPTIONS_EXECMODE_Z80, &CMainFrame::OnOptionsExecModeZ80)
    ON_COMMAND(ID_OPTIONS_PRESERVE_Z80_REGS, &CMainFrame::OnOptionsPreserveZ80Regs)
    ON_COMMAND(ID_OPTIONS_RECLAIM_INTERP, &CMainFrame::OnOptionsReclaimInterp)
    ON_COMMAND(ID_OPTIONS_HOST_CLOCK, &CMainFrame::OnOptionsHostClock)
    ON_COMMAND(ID_OPTIONS_HARVARD, &CMainFrame::OnOptionsHarvard)
    ON_COMMAND(ID_OPTIONS_PAUSE, &CMainFrame::OnOptionsPause)
    ON_COMMAND(ID_OPTIONS_FONT_SMALL, &CMainFrame::OnOptionsFontSmall)
    ON_COMMAND(ID_OPTIONS_FONT_MEDIUM, &CMainFrame::OnOptionsFontMedium)
    ON_COMMAND(ID_OPTIONS_FONT_LARGE, &CMainFrame::OnOptionsFontLarge)
    ON_COMMAND(ID_VIEW_OPCODECOUNT, &CMainFrame::OnViewOpcodeCount)
    ON_COMMAND(ID_HELP_ABOUT, &CMainFrame::OnHelpAbout)
    ON_COMMAND(ID_OPTIONS_VERIFY_RECORD,  &CMainFrame::OnVerifyRecord)
    ON_COMMAND(ID_OPTIONS_VERIFY_RECORD_RECLAIMED, &CMainFrame::OnVerifyRecordReclaimed)
    ON_UPDATE_COMMAND_UI(ID_OPTIONS_VERIFY_RECORD_RECLAIMED, &CMainFrame::OnUpdateVerifyCompare)
    ON_COMMAND(ID_OPTIONS_VERIFY_COMPARE, &CMainFrame::OnVerifyCompare)
    ON_COMMAND(ID_OPTIONS_VERIFY_CANCEL,  &CMainFrame::OnVerifyCancel)
    ON_UPDATE_COMMAND_UI(ID_OPTIONS_VERIFY_RECORD,  &CMainFrame::OnUpdateVerifyRecord)
    ON_UPDATE_COMMAND_UI(ID_OPTIONS_VERIFY_COMPARE, &CMainFrame::OnUpdateVerifyCompare)
    ON_UPDATE_COMMAND_UI(ID_OPTIONS_VERIFY_CANCEL,  &CMainFrame::OnUpdateVerifyCancel)
    ON_UPDATE_COMMAND_UI(ID_OPTIONS_TRACE, &CMainFrame::OnUpdateOptionsTrace)
    ON_UPDATE_COMMAND_UI(ID_OPTIONS_EXECMODE_PCODE, &CMainFrame::OnUpdateOptionsExecModePCode)
    ON_UPDATE_COMMAND_UI(ID_OPTIONS_EXECMODE_Z80, &CMainFrame::OnUpdateOptionsExecModeZ80)
    ON_UPDATE_COMMAND_UI(ID_OPTIONS_PRESERVE_Z80_REGS, &CMainFrame::OnUpdateOptionsPreserveZ80Regs)
    ON_UPDATE_COMMAND_UI(ID_OPTIONS_RECLAIM_INTERP, &CMainFrame::OnUpdateOptionsReclaimInterp)
    ON_UPDATE_COMMAND_UI(ID_OPTIONS_HOST_CLOCK, &CMainFrame::OnUpdateOptionsHostClock)
    ON_UPDATE_COMMAND_UI(ID_OPTIONS_HARVARD, &CMainFrame::OnUpdateOptionsHarvard)
    ON_UPDATE_COMMAND_UI(ID_OPTIONS_PAUSE, &CMainFrame::OnUpdateOptionsPause)
    ON_UPDATE_COMMAND_UI(ID_OPTIONS_FONT_SMALL, &CMainFrame::OnUpdateOptionsFontSmall)
    ON_UPDATE_COMMAND_UI(ID_OPTIONS_FONT_MEDIUM, &CMainFrame::OnUpdateOptionsFontMedium)
    ON_UPDATE_COMMAND_UI(ID_OPTIONS_FONT_LARGE, &CMainFrame::OnUpdateOptionsFontLarge)
END_MESSAGE_MAP()

CMainFrame::CMainFrame() {
    m_engine = std::make_unique<PSystemEngine>();
}

CMainFrame::~CMainFrame() {
}

BOOL CMainFrame::PreCreateWindow(CREATESTRUCT& cs) {
    if (!CFrameWnd::PreCreateWindow(cs)) return FALSE;
    cs.style = WS_OVERLAPPEDWINDOW;
    cs.lpszClass = AfxRegisterWndClass(CS_HREDRAW | CS_VREDRAW,
                                       ::LoadCursor(nullptr, IDC_ARROW),
                                       (HBRUSH)::GetStockObject(BLACK_BRUSH),
                                       ::LoadIcon(AfxGetInstanceHandle(), MAKEINTRESOURCE(IDR_MAINFRAME)));
    return TRUE;
}

int CMainFrame::OnCreate(LPCREATESTRUCT lpCreateStruct) {
    if (CFrameWnd::OnCreate(lpCreateStruct) == -1) return -1;

    if (!m_statusBar.Create(this) ||
        !m_statusBar.SetIndicators(indicators, sizeof(indicators) / sizeof(UINT))) {
        return -1;
    }
    m_statusBar.SetPaneInfo(0, ID_SEPARATOR, SBPS_STRETCH, 0);

    RecreateFont();
    ResizeToFitGrid();
    SetTimer(1, 50, nullptr); // ~20fps repaint of the terminal grid
    SetWindowTextW((std::wstring(APP_NAME_W) + L" - v" + APP_VERSION_W).c_str());

    return 0;
}

void CMainFrame::OnDestroy() {
    StopEngineThread();
    CFrameWnd::OnDestroy();
}

void CMainFrame::OnClose() {
    CFrameWnd::OnClose();
}

void CMainFrame::RecreateFont() {
    if (m_font.GetSafeHandle()) m_font.DeleteObject();
    m_font.CreatePointFont(m_fontPointSize * 10, L"Consolas");

    CClientDC dc(this);
    CFont* pOld = dc.SelectObject(&m_font);
    TEXTMETRICW tm;
    dc.GetTextMetrics(&tm);
    m_cellWidth = tm.tmAveCharWidth;
    m_cellHeight = tm.tmHeight + tm.tmExternalLeading;
    dc.SelectObject(pOld);

    Invalidate();
}

// Sizes the window so the area left for the terminal -- the client area
// minus the docked status bar -- exactly fits the TERM_COLS x TERM_ROWS
// grid at the current font/cell size. Called on startup (after the status
// bar exists) and whenever the font size changes.
//
// This must not rely on CalcWindowRect alone: MFC's CWnd::CalcWindowRect
// calls AdjustWindowRectEx with bMenu = FALSE and knows nothing about
// control bars, so the window came out short by the menu bar and the
// status bar covered the bottom of the grid -- about two text lines. (That
// shortfall is also why TERM_ROWS was once raised to 47 to "fit the
// window", which broke the editor's scrolling; see PSystemEngine.h.)
// So: estimate with the menu and the status bar included, then ask MFC's
// own layout (RepositionBars in query mode) how much room the terminal
// really got and correct by the difference -- which also covers a menu
// that wraps onto two lines, DPI scaling and themed borders.
void CMainFrame::ResizeToFitGrid() {
    const int wantW = TERM_COLS * m_cellWidth;
    const int wantH = TERM_ROWS * m_cellHeight;

    CRect r(0, 0, wantW, wantH);
    if (m_statusBar.GetSafeHwnd()) {
        CRect sb;
        m_statusBar.GetWindowRect(&sb);
        r.bottom += sb.Height();
    }
    ::AdjustWindowRectEx(&r, GetStyle(), GetMenu() != nullptr, GetExStyle());
    SetWindowPos(nullptr, 0, 0, r.Width(), r.Height(), SWP_NOMOVE | SWP_NOZORDER);

    for (int pass = 0; pass < 3; pass++) {
        RecalcLayout();
        CRect avail;   // what is left for the terminal after the docked bars
        RepositionBars(AFX_IDW_CONTROLBAR_FIRST, AFX_IDW_CONTROLBAR_LAST, 0, reposQuery, &avail);
        const int dw = wantW - avail.Width();
        const int dh = wantH - avail.Height();
        if (dw == 0 && dh == 0) break;
        CRect wr;
        GetWindowRect(&wr);
        SetWindowPos(nullptr, 0, 0, wr.Width() + dw, wr.Height() + dh, SWP_NOMOVE | SWP_NOZORDER);
    }
}

void CMainFrame::OnSize(UINT nType, int cx, int cy) {
    CFrameWnd::OnSize(nType, cx, cy);
    // The terminal grid is a fixed logical size (TERM_COLS x TERM_ROWS);
    // resizing the window just changes how much of it is visible (extra
    // background if larger, clipped if smaller) rather than rescaling the
    // font. This keeps behavior simple and predictable.
}

// Prevent Windows from erasing the window to the system background colour
// (typically gray or white) between frames.  Without this, every repaint
// cycle shows a flash: erase-to-gray, then paint-to-black, then text.
// Since OnPaint always fills the entire client area with solid black before
// drawing the grid, the erase step is not just redundant but actively harmful.
BOOL CMainFrame::OnEraseBkgnd(CDC* /*pDC*/) {
    return TRUE; // "yes, we handled it" -- do nothing; OnPaint handles it
}

void CMainFrame::OnPaint() {
    CPaintDC screenDC(this); // validates the update region, must exist

    // ---- Double-buffer setup ----------------------------------------
    // Draw everything into an off-screen bitmap the same size as the
    // client area, then blit the whole frame to the screen in one atomic
    // operation.  This ensures the display transitions from one complete
    // frame to the next without any intermediate partial state being
    // visible, which is the primary cause of the visible flicker.
    CRect clientRect;
    GetClientRect(&clientRect);

    CDC memDC;
    memDC.CreateCompatibleDC(&screenDC);
    CBitmap memBmp;
    memBmp.CreateCompatibleBitmap(&screenDC, clientRect.Width(), clientRect.Height());
    CBitmap* pOldBmp = memDC.SelectObject(&memBmp);

    // ---- Draw into the off-screen buffer ----------------------------
    std::vector<uint8_t> grid;
    int cursorX = 0, cursorY = 0;
    m_engine->SnapshotGrid(grid, cursorX, cursorY);

    CFont* pOldFont = memDC.SelectObject(&m_font);
    memDC.FillSolidRect(&clientRect, RGB(0, 0, 0));

    if (!grid.empty()) {
        // Normalised selection rectangle (character-cell coordinates)
        int selR1 = 0, selC1 = 0, selR2 = -1, selC2 = -1;
        bool hasSel = m_selActive;
        if (hasSel) {
            selR1 = min(m_selAnchor.y,  m_selCurrent.y);
            selR2 = max(m_selAnchor.y,  m_selCurrent.y);
            selC1 = min(m_selAnchor.x,  m_selCurrent.x);
            selC2 = max(m_selAnchor.x,  m_selCurrent.x);
            selR1 = max(selR1, 0); selR2 = min(selR2, TERM_ROWS - 1);
            selC1 = max(selC1, 0); selC2 = min(selC2, TERM_COLS - 1);
        }

        std::wstring rowText;
        rowText.resize(TERM_COLS);
        for (int y = 0; y < TERM_ROWS; y++) {
            for (int x = 0; x < TERM_COLS; x++)
                rowText[x] = (wchar_t)grid[(size_t)y * TERM_COLS + x];

            if (!hasSel || y < selR1 || y > selR2) {
                memDC.SetBkColor(RGB(0, 0, 0));
                memDC.SetTextColor(RGB(0, 255, 0));
                memDC.TextOutW(0, y * m_cellHeight, rowText.c_str(), TERM_COLS);
            } else {
                int x0 = selC1, x1 = selC2 + 1;
                if (x0 > 0) {
                    memDC.SetBkColor(RGB(0, 0, 0));
                    memDC.SetTextColor(RGB(0, 255, 0));
                    memDC.TextOutW(0, y * m_cellHeight, rowText.c_str(), x0);
                }
                if (x1 > x0) {
                    memDC.SetBkColor(RGB(0, 255, 0));
                    memDC.SetTextColor(RGB(0, 0, 0));
                    memDC.TextOutW(x0 * m_cellWidth, y * m_cellHeight,
                                  rowText.c_str() + x0, x1 - x0);
                }
                if (x1 < TERM_COLS) {
                    memDC.SetBkColor(RGB(0, 0, 0));
                    memDC.SetTextColor(RGB(0, 255, 0));
                    memDC.TextOutW(x1 * m_cellWidth, y * m_cellHeight,
                                  rowText.c_str() + x1, TERM_COLS - x1);
                }
            }
        }

        if (!hasSel) {
            int cx0 = cursorX * m_cellWidth;
            int cy0 = cursorY * m_cellHeight + m_cellHeight - 2;
            CPen pen(PS_SOLID, 1, RGB(0, 255, 0));
            CPen* pOldPen = memDC.SelectObject(&pen);
            memDC.MoveTo(cx0, cy0);
            memDC.LineTo(cx0 + m_cellWidth, cy0);
            memDC.SelectObject(pOldPen);
        }
    }

    memDC.SelectObject(pOldFont);

    // ---- Single atomic blit to the screen ---------------------------
    // This is the only moment the display changes: from the previous complete
    // frame to the new complete frame, with nothing in between.
    screenDC.BitBlt(0, 0, clientRect.Width(), clientRect.Height(),
                    &memDC, 0, 0, SRCCOPY);

    // ---- Cleanup ----------------------------------------------------
    memDC.SelectObject(pOldBmp);
    // memBmp and memDC destroyed by their destructors

    if (m_engine->HasHalted()) UpdateStatusBarText();
}

// ---------------------------------------------------------------------------
// Text selection (left-button drag) and file-inject (right-button)
// ---------------------------------------------------------------------------

// Pixel-to-character-cell helper.  Clamps to the grid bounds.
static CPoint PixelToCell(CPoint pt, int cellW, int cellH) {
    int col = pt.x / max(cellW, 1);
    int row = pt.y / max(cellH, 1);
    col = max(0, min(col, TERM_COLS - 1));
    row = max(0, min(row, TERM_ROWS  - 1));
    return CPoint(col, row);
}

void CMainFrame::OnLButtonDown(UINT /*nFlags*/, CPoint point) {
    m_selAnchor  = PixelToCell(point, m_cellWidth, m_cellHeight);
    m_selCurrent = m_selAnchor;
    m_selActive   = true;
    m_selDragging = true;
    SetCapture(); // track mouse even if it leaves the window
    Invalidate(FALSE);
}

void CMainFrame::OnMouseMove(UINT nFlags, CPoint point) {
    if (m_selDragging && (nFlags & MK_LBUTTON)) {
        CPoint cell = PixelToCell(point, m_cellWidth, m_cellHeight);
        if (cell != m_selCurrent) {
            m_selCurrent = cell;
            Invalidate(FALSE);
        }
    }
}

void CMainFrame::OnLButtonUp(UINT /*nFlags*/, CPoint point) {
    if (!m_selDragging) return;
    ReleaseCapture();
    m_selDragging = false;
    m_selCurrent  = PixelToCell(point, m_cellWidth, m_cellHeight);

    // Build the selected text and put it on the clipboard.
    int r1 = min(m_selAnchor.y,  m_selCurrent.y);
    int r2 = max(m_selAnchor.y,  m_selCurrent.y);
    int c1 = min(m_selAnchor.x,  m_selCurrent.x);
    int c2 = max(m_selAnchor.x,  m_selCurrent.x);
    r1 = max(r1, 0); r2 = min(r2, TERM_ROWS - 1);
    c1 = max(c1, 0); c2 = min(c2, TERM_COLS - 1);

    if (r1 <= r2 && c1 <= c2 && m_engine) {
        std::vector<uint8_t> grid;
        int cx = 0, cy = 0;
        m_engine->SnapshotGrid(grid, cx, cy);

        std::wstring text;
        for (int r = r1; r <= r2; r++) {
            int lineEnd = c2;
            // Trim trailing spaces on each line (except the last selected row,
            // where the user may have intentionally selected whitespace).
            if (r < r2) {
                while (lineEnd > c1 &&
                       grid[(size_t)r * TERM_COLS + lineEnd] == ' ')
                    --lineEnd;
            }
            for (int c = c1; c <= lineEnd; c++)
                text += (wchar_t)grid[(size_t)r * TERM_COLS + c];
            if (r < r2) text += L"\r\n";
        }

        if (!text.empty() && OpenClipboard()) {
            EmptyClipboard();
            // Allocate global memory: (length + 1) wide chars
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE,
                                       (text.size() + 1) * sizeof(wchar_t));
            if (hMem) {
                wchar_t* pMem = (wchar_t*)GlobalLock(hMem);
                if (pMem) {
                    memcpy(pMem, text.c_str(),
                           (text.size() + 1) * sizeof(wchar_t));
                    GlobalUnlock(hMem);
                    SetClipboardData(CF_UNICODETEXT, hMem);
                }
            }
            CloseClipboard();
        }
    }

    // Clear the selection highlight and repaint
    m_selActive = false;
    Invalidate(FALSE);
}

void CMainFrame::OnRButtonUp(UINT /*nFlags*/, CPoint /*point*/) {
    // Pick a text file and inject its contents as keyboard input to the
    // running UCSD Pascal system.  This is intended for repeatable test
    // sequences: record your keystrokes in a plain text file, then inject
    // them back in through here without having to retype them.
    CFileDialog dlg(TRUE, nullptr, nullptr,
                    OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                    L"Text files (*.txt;*.text)|*.txt;*.text|All files (*.*)|*.*||",
                    this);
    dlg.m_ofn.lpstrTitle = L"Inject keyboard input from file";
    if (dlg.DoModal() != IDOK) return;

    std::wstring path = (LPCWSTR)dlg.GetPathName();
    FILE* f = nullptr;
    _wfopen_s(&f, path.c_str(), L"rb");
    if (!f) {
        AfxMessageBox(L"Could not open the selected file.", MB_ICONERROR);
        return;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> bytes((size_t)max(sz, 0L));
    if (sz > 0) fread(bytes.data(), 1, (size_t)sz, f);
    fclose(f);

    // Feed every byte directly through PostKey, the same path the keyboard
    // uses.  0x0A (Unix LF) is translated to 0x0D (CR) so that text files
    // with Unix line endings work correctly (the editor expects CR as Return).
    long injected = 0;
    for (uint8_t b : bytes) {
        if (b == 0x0A) b = 0x0D;
        m_engine->PostKey(b);
        ++injected;
    }

    CString msg;
    msg.Format(L"Injected %ld byte(s) from:\r\n%s", injected, path.c_str());
    AfxMessageBox(msg, MB_ICONINFORMATION);
}

void CMainFrame::OnChar(UINT nChar, UINT nRepCnt, UINT nFlags) {
    if (m_verifyRunner) return;   // the verify script owns the keyboard
    if (nChar >= 1 && nChar <= 255) {
        m_engine->PostKey((uint8_t)nChar);
    }
    CFrameWnd::OnChar(nChar, nRepCnt, nFlags);
}

// Keys that don't generate WM_CHAR (arrows, Page Up/Down, Home, End,
// Insert, Delete). They are sent to the P-System as what it expects,
// matched to SYSTEM.MISCINFO (v1.68 file: see tools/SYSTEM.MISCINFO and
// EDITOR_KEYS.md):
//   Arrows: one control character each -- the "KEY TO MOVE CURSOR ..."
//   settings: up=^T (0x14), down=^R (0x12), left=^Q (0x11),
//   right=^U (0x15). (Right used to be ^S, which is also the STOP key.)
//   Page Up/Down, Home, End, Insert, Delete have no SYSTEM.MISCINFO
//   setting at all -- in the UCSD editor those actions are letter
//   commands -- so these keys type the editor's own command sequence.
//   They are meant for the editor's command level (the ">Edit:" prompt):
//     Page Down  ">P"       page forward
//     Page Up    "<P>"      page back, then restore the forward direction
//     Home       "JB"       jump to the beginning of the file
//     End        "JE"       jump to the end of the file
//     Insert     "I"        start inserting (Ctrl-C accepts, Esc cancels)
//     Delete     "D" ^U ^C  delete the character under the cursor
//   Every sequence was checked against the real SYSTEM.EDITOR in the
//   harness (v1.68).
void CMainFrame::OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags) {
    if (m_verifyRunner) return;   // the verify script owns the keyboard
    auto send = [this](const char* keys) { for (const char* p = keys; *p; p++) m_engine->PostKey((uint8_t)*p); };
    switch (nChar) {
        case VK_UP:     m_engine->PostKey(0x14); return; // ^T
        case VK_DOWN:   m_engine->PostKey(0x12); return; // ^R
        case VK_LEFT:   m_engine->PostKey(0x11); return; // ^Q
        case VK_RIGHT:  m_engine->PostKey(0x15); return; // ^U
        case VK_NEXT:   send(">P");              return; // Page Down
        case VK_PRIOR:  send("<P>");             return; // Page Up
        case VK_HOME:   send("JB");              return;
        case VK_END:    send("JE");              return;
        case VK_INSERT: send("I");               return;
        case VK_DELETE: send("D\x15\x03");       return; // Delete mode, right, accept
        default: break;
    }
    CFrameWnd::OnKeyDown(nChar, nRepCnt, nFlags);
}

void CMainFrame::OnTimer(UINT_PTR nIDEvent) {
    if (nIDEvent == 2 && m_verifyRunner) {
        // Verify P-System: feed the script (one key per poll, only while the
        // system is blocked waiting for a key -- see PSystemVerify.h).
        if (m_engine->VerifyMismatch()) {
            StopEngineThread();
            m_engine->StopVerifyLog();
            FinishVerify(L"MISMATCH -- the P-code executed differs from the reference log.", false);
            return;
        }
        VerifyRunner::State st = m_verifyRunner->Poll();
        if (st == VerifyRunner::FAILED) {
            m_engine->StopVerifyLog();
            StopEngineThread();
            FinishVerify(L"The script stopped: the system did not show what the script expected.", false);
            return;
        }
        if (st == VerifyRunner::DONE) {
            // End the log at a defined point: the system is blocked waiting
            // for a key, so every run's log ends at the same instruction.
            if (!(m_engine->IsWaitingForKey() && m_engine->PendingKeyCount() == 0) && !m_engine->IsSystemHalted()) return;
            m_engine->StopVerifyLog();            // compare mode: also checks for extra reference records
            StopEngineThread();
            if (m_engine->VerifyMismatch())
                FinishVerify(L"MISMATCH -- the P-code executed differs from the reference log.", false);
            else
                FinishVerify(m_verifyRecord ? L"Reference log recorded." : L"PASSED -- every P-code instruction matched the reference log.", true);
            return;
        }
        return;
    }
    if (nIDEvent == 1) {
        if (m_engine && !m_bootFaultShown && !m_engine->BootFault().empty()) {
            m_bootFaultShown = true;
            AfxMessageBox(CString(L"The P-System could not be started.\n\n") + CString(m_engine->BootFault().c_str()), MB_ICONWARNING);
        }
        if (m_engine && !m_verifyRunner && m_engine->IsSystemHalted()) {
            // The P-System's main program ended (it reached ABORT) -- on the
            // original machine a dead stop until PASCAL was run again.
            AfxMessageBox(L"The P-System has halted: its main program ended (it executed ABORT).\n\n"
                          L"This is how UCSD Pascal II.0 stops -- for example after an \"Exit from uncalled "
                          L"proc\" error, whose recovery ends the operating system itself.\n\n"
                          L"The P-System will now restart.", MB_ICONINFORMATION);
            RestartEngineWithCurrentPaths();
            return;
        }
        if (m_engine) {
            // A mounted volume's file was replaced while the emulator ran
            // (e.g. a git pull): the engine refuses to write into it, so its
            // older copy cannot overwrite the new file. Say so once.
            std::wstring changedPath;
            const int unit = m_engine->TakeChangedOnDiskUnit(changedPath);
            if (unit) {
                CString msg;
                msg.Format(L"The volume file on unit #%d was changed outside the emulator after it was mounted:\n\n%s\n\n"
                           L"To protect the new file, the emulator will not write to it: anything the P-System "
                           L"writes to unit #%d from now on is NOT saved.\n\n"
                           L"Re-open the unit (File > Open Unit #%d) or restart the emulator to load the new file.",
                           unit, changedPath.c_str(), unit, unit);
                AfxMessageBox(msg, MB_ICONWARNING);
            }
        }
        if (m_engine && !m_reclaimFaultShown && !m_engine->ReclaimFault().empty()) {
            m_reclaimFaultShown = true;
            CString msg(L"The P-System stopped: ");
            msg += CString(m_engine->ReclaimFault().c_str());
            msg += m_engine->HarvardActive()
                ? L"\n\nTurn off Options > Harvard Mode (or Reclaim Z80 Interpreter Memory) to run this program."
                : L"\n\nTurn off Options > Reclaim Z80 Interpreter Memory to run this program.";
            AfxMessageBox(msg, MB_ICONWARNING);
        }
        Invalidate(FALSE);
        UpdateStatusBarText();
    }
    CFrameWnd::OnTimer(nIDEvent);
}

void CMainFrame::UpdateStatusBarText() {
    CString text;
    if (m_verifyRunner) {
        double secs = (GetTickCount64() - m_verifyStartTicks) / 1000.0;
        text.Format(L"Verify P-System (%s): script step %u of %u, %llu P-code instructions, %.0f s -- keyboard disabled",
                    m_verifyRecord ? (m_verifyReclaimed ? L"recording reclaimed-memory reference" : L"recording reference, Z80 mode")
                                   : (m_verifyReclaimed ? L"comparing against the reclaimed-memory reference" : L"comparing against reference"),
                    (unsigned)(m_verifyRunner->StepIndex() + 1), (unsigned)m_verifyRunner->StepCount(),
                    (unsigned long long)m_engine->VerifyRecordCount(), secs);
        m_statusBar.SetPaneText(0, text);
        return;
    }
    if (m_engine->HasHalted()) {
        text = L"System halted.";
    } else if (m_engine->IsRunning()) {
        text = m_paused ? L"Paused -- Options > Resume to continue" : (m_traceEnabled ? L"Running (tracing to trace.txt)" : L"Running");
        // The configuration actually running: execution mode, how it booted,
        // the memory layout the engine is in (not just what the menu says),
        // register compatibility and the clock. Settings that take effect at
        // the next restart are named as such.
        text += m_traceZ80Mode ? L" -- Z80 mode" : L" -- P-Code mode";
        if (m_engine->NativelyBooted())
            text += L", native boot";
        else if (!m_engine->NativeBootNote().empty())
            text += CString(L", Z80 boot (native boot not possible: ") + CString(m_engine->NativeBootNote().c_str()) + L")";
        else
            text += L", Z80 boot";
        const bool harvard = m_engine->HarvardActive();
        text += harvard ? L", Harvard mode (separate I & D space)"
                        : (m_engine->InterpreterMemoryReclaimed() ? L", memory reclaimed" : L", normal memory layout");
        if (!m_traceZ80Mode)
            text += m_preserveZ80RegisterCompat ? L", Z80 register compatibility on" : L", register compatibility off";
        text += m_hostClock ? L", PC date and time" : L", boot disk date";
        const bool harvardCanApply = !m_traceZ80Mode && !m_preserveZ80RegisterCompat && m_reclaimInterpMemory;
        if (m_harvardMode && !harvard)
            text += harvardCanApply ? L"  [Harvard Mode: takes effect when the P-System restarts]"
                                    : L"  [Harvard Mode is checked but needs P-Code mode, register compatibility off and Reclaim on]";
        else if (!m_harvardMode && harvard)
            text += L"  [Harvard Mode off: takes effect when the P-System restarts]";
    } else {
        text = L"No system loaded -- use File > Open Big Disk...";
    }
    m_statusBar.SetPaneText(0, text);
}

UINT CMainFrame::EngineThreadProc(LPVOID pParam) {
    PSystemEngine* engine = (PSystemEngine*)pParam;
    engine->RunLoop();
    return 0;
}

void CMainFrame::StartEngineThread() {
    m_workerThread = AfxBeginThread(&CMainFrame::EngineThreadProc, m_engine.get());
}

void CMainFrame::StopEngineThread() {
    if (m_engine) m_engine->Stop();
    if (m_workerThread) {
        ::WaitForSingleObject(m_workerThread->m_hThread, 2000);
        m_workerThread = nullptr; // owned/cleaned up by MFC's thread termination
    }
}

void CMainFrame::SaveSettings() {
    // Persist volume paths and UI preferences to the registry so that the
    // next run of the program opens the same volumes automatically.
    auto rsave = [](const wchar_t* key, const std::wstring& val) {
        AfxGetApp()->WriteProfileStringW(L"Volumes", key, val.c_str());
    };
    rsave(L"Unit4Path",  m_bigDiskPath);
    rsave(L"Unit5Path",  m_scratchDiskPath);
    rsave(L"Unit9Path",  m_unit9Path);
    rsave(L"Unit10Path", m_unit10Path);
    rsave(L"Unit11Path", m_unit11Path);
    rsave(L"Unit12Path", m_unit12Path);
    AfxGetApp()->WriteProfileInt(L"Options", L"FontPointSize", m_fontPointSize);
    AfxGetApp()->WriteProfileInt(L"Options", L"TraceEnabled",  m_traceEnabled ? 1 : 0);
    AfxGetApp()->WriteProfileInt(L"Options", L"TraceZ80Mode",  m_traceZ80Mode ? 1 : 0);
    AfxGetApp()->WriteProfileInt(L"Options", L"TraceAlsoZ80",  m_traceAlsoZ80 ? 1 : 0);
    AfxGetApp()->WriteProfileInt(L"Options", L"PreserveZ80RegisterCompat", m_preserveZ80RegisterCompat ? 1 : 0);
    AfxGetApp()->WriteProfileInt(L"Options", L"ReclaimInterpMemory", m_reclaimInterpMemory ? 1 : 0);
    AfxGetApp()->WriteProfileInt(L"Options", L"HostClock", m_hostClock ? 1 : 0);
    AfxGetApp()->WriteProfileInt(L"Options", L"HarvardMode", m_harvardMode ? 1 : 0);
}

void CMainFrame::RestartEngineWithCurrentPaths() {
    StopEngineThread();
    m_engine = std::make_unique<PSystemEngine>();

    if (m_pascalBinPath.empty() || m_bigDiskPath.empty()) {
        UpdateStatusBarText();
        return; // not enough to boot yet
    }

    std::wstring err;
    if (!m_engine->LoadFiles(m_pascalBinPath, m_bigDiskPath, m_scratchDiskPath, err)) {
        AfxMessageBox(err.c_str(), MB_ICONERROR);
        UpdateStatusBarText();
        return;
    }
    if (!m_unit9Path.empty()) m_engine->MountUnit9(m_unit9Path, err);
    if (!m_unit10Path.empty()) m_engine->MountUnit10(m_unit10Path, err);
    if (!m_unit11Path.empty()) m_engine->MountUnit(11, m_unit11Path, err);
    if (!m_unit12Path.empty()) m_engine->MountUnit(12, m_unit12Path, err);

    // Apply execution mode (always, not just when tracing)
    m_engine->SetNativePcodeOps(!m_traceZ80Mode);
    m_engine->SetTraceAlsoZ80(m_traceAlsoZ80);
    m_engine->SetPreserveZ80RegisterCompat(m_preserveZ80RegisterCompat);
    // Reclaim takes effect at the first P-code instruction, and only in P-Code
    // mode with register compatibility off (the engine checks too).
    m_engine->SetReclaimInterpreterMemory(m_reclaimInterpMemory && !m_traceZ80Mode && !m_preserveZ80RegisterCompat);
    m_engine->SetHostClock(m_hostClock);   // today's date and TIME from the PC (not in Verify runs: deterministic)
    // Harvard mode needs the reclaimed layout (the engine ignores it otherwise).
    m_engine->SetHarvard(m_harvardMode && !m_traceZ80Mode && !m_preserveZ80RegisterCompat && m_reclaimInterpMemory);
    m_paused = false;                      // a new engine runs
    m_reclaimFaultShown = false;
    m_bootFaultShown = false;

    if (m_traceEnabled) {
        std::wstring tracePath = ExeDir() + L"\\trace.txt";
        m_engine->EnableTrace(tracePath, err);
    }

    StartEngineThread();
    UpdateStatusBarText();
}

void CMainFrame::TryAutoLoad() {
    std::wstring dir = ExeDir();
    m_pascalBinPath = dir + L"\\pascal.bin";

    // Volume paths come ONLY from the registry (SaveSettings writes them
    // whenever a volume is opened or unmounted).  There is deliberately no
    // fall-back to volume files next to the .exe: a stale copy there could
    // be mounted by mistake.  pascal.bin is not a volume and stays next to
    // the .exe.
    auto rload = [](const wchar_t* key) -> std::wstring {
        CString v = AfxGetApp()->GetProfileStringW(L"Volumes", key, L"");
        return (LPCWSTR)v;
    };
    m_bigDiskPath    = rload(L"Unit4Path");
    m_scratchDiskPath= rload(L"Unit5Path");
    m_unit9Path      = rload(L"Unit9Path");
    m_unit10Path     = rload(L"Unit10Path");
    m_unit11Path     = rload(L"Unit11Path");
    m_unit12Path     = rload(L"Unit12Path");

    // Load font preference (default: 11 pt / medium)
    m_fontPointSize = AfxGetApp()->GetProfileInt(L"Options", L"FontPointSize", 11);
    if (m_fontPointSize != 8 && m_fontPointSize != 11 && m_fontPointSize != 14)
        m_fontPointSize = 11;
    RecreateFont();

    // Restore trace-enabled state (default off)
    m_traceEnabled = (AfxGetApp()->GetProfileInt(L"Options", L"TraceEnabled", 0) != 0);
    m_traceZ80Mode = (AfxGetApp()->GetProfileInt(L"Options", L"TraceZ80Mode", 0) != 0);
    m_traceAlsoZ80 = (AfxGetApp()->GetProfileInt(L"Options", L"TraceAlsoZ80", 0) != 0);
    m_preserveZ80RegisterCompat = (AfxGetApp()->GetProfileInt(L"Options", L"PreserveZ80RegisterCompat", 1) != 0);
    m_reclaimInterpMemory = (AfxGetApp()->GetProfileInt(L"Options", L"ReclaimInterpMemory", 0) != 0);
    m_hostClock = (AfxGetApp()->GetProfileInt(L"Options", L"HostClock", 1) != 0);
    m_harvardMode = (AfxGetApp()->GetProfileInt(L"Options", L"HarvardMode", 1) != 0);   // default on (engine 1.92)

    // pascal.bin (the Z80 loader) is needed unless the P-System boots natively
    // with nothing Z80: P-Code mode, register compatibility off, memory reclaimed.
    const bool needPascalBin = m_traceZ80Mode || m_preserveZ80RegisterCompat || !m_reclaimInterpMemory;
    const bool haveBoot = !m_bigDiskPath.empty() &&
        ::GetFileAttributesW(m_bigDiskPath.c_str()) != INVALID_FILE_ATTRIBUTES;
    const bool havePascalBin =
        ::GetFileAttributesW(m_pascalBinPath.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (!haveBoot || (needPascalBin && !havePascalBin)) {
        // Say exactly what is wrong.  The remembered paths are left alone (a
        // volume on a drive that is offline today should not be forgotten).
        CString msg;
        if (m_bigDiskPath.empty())
            msg = L"No boot volume (unit #4) is remembered.\r\n\r\n"
                  L"Use File > Open Big Disk... to choose one.";
        else if (!haveBoot)
            msg.Format(L"The boot volume (unit #4) was not found:\r\n%s\r\n\r\n"
                       L"Use File > Open Big Disk... to choose it.", m_bigDiskPath.c_str());
        else
            msg.Format(L"pascal.bin was not found:\r\n%s\r\n\r\n"
                       L"It must be in the same folder as the executable.", m_pascalBinPath.c_str());
        AfxMessageBox(msg, MB_ICONINFORMATION);
        UpdateStatusBarText();
        return;
    }

    // A remembered volume that has disappeared is reported, not silently skipped.
    CString missing;
    auto chk = [&missing](const wchar_t* label, const std::wstring& p) {
        if (!p.empty() && ::GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) {
            missing += L"\r\n  ";
            missing += label;
            missing += p.c_str();
        }
    };
    chk(L"unit #5:  ",  m_scratchDiskPath);
    chk(L"unit #9:  ",  m_unit9Path);
    chk(L"unit #10: ",  m_unit10Path);
    chk(L"unit #11: ",  m_unit11Path);
    chk(L"unit #12: ",  m_unit12Path);
    if (!missing.IsEmpty()) {
        CString msg = L"These remembered volumes were not found and are not mounted:";
        msg += missing;
        msg += L"\r\n\r\nUse the File menu to mount them again.";
        AfxMessageBox(msg, MB_ICONINFORMATION);
    }

    RestartEngineWithCurrentPaths();
}

void CMainFrame::OnFileOpenBigDisk() {
    CFileDialog dlg(TRUE, L"BLK", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                     L"Big Disk images (*.BLK)|*.BLK|All Files (*.*)|*.*||");
    if (dlg.DoModal() != IDOK) return;
    m_bigDiskPath = dlg.GetPathName();
    if (m_pascalBinPath.empty()) m_pascalBinPath = ExeDir() + L"\\pascal.bin";
    SaveSettings();
    RestartEngineWithCurrentPaths();
}

void CMainFrame::OnFileOpenScratch() {
    CFileDialog dlg(TRUE, L"BLK", nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                     L"Big Disk images (*.BLK)|*.BLK|All Files (*.*)|*.*||");
    if (dlg.DoModal() != IDOK) return;
    m_scratchDiskPath = dlg.GetPathName();
    SaveSettings();
    RestartEngineWithCurrentPaths();
}

void CMainFrame::OnFileOpenUnit9() {
    CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                     L"Disk images (*.BLK;*.raw)|*.BLK;*.raw|All Files (*.*)|*.*||");
    if (dlg.DoModal() != IDOK) return;
    m_unit9Path = dlg.GetPathName();
    SaveSettings();
    RestartEngineWithCurrentPaths();
}

void CMainFrame::OnFileOpenUnit10() {
    CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                     L"Disk images (*.BLK;*.raw)|*.BLK;*.raw|All Files (*.*)|*.*||");
    if (dlg.DoModal() != IDOK) return;
    m_unit10Path = dlg.GetPathName();
    SaveSettings();
    RestartEngineWithCurrentPaths();
}

void CMainFrame::OnFileOpenUnit11() {
    CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                     L"Disk images (*.BLK;*.raw)|*.BLK;*.raw|All Files (*.*)|*.*||");
    if (dlg.DoModal() != IDOK) return;
    m_unit11Path = dlg.GetPathName();
    SaveSettings();
    RestartEngineWithCurrentPaths();
}

void CMainFrame::OnFileOpenUnit12() {
    CFileDialog dlg(TRUE, nullptr, nullptr, OFN_FILEMUSTEXIST | OFN_HIDEREADONLY,
                     L"Disk images (*.BLK;*.raw)|*.BLK;*.raw|All Files (*.*)|*.*||");
    if (dlg.DoModal() != IDOK) return;
    m_unit12Path = dlg.GetPathName();
    SaveSettings();
    RestartEngineWithCurrentPaths();
}

void CMainFrame::OnFileExit() {
    PostMessageW(WM_CLOSE);
}

// ---------------------------------------------------------------------------
// File menu dynamic labels -- show the actual UCSD volume name
// ---------------------------------------------------------------------------

// The image file's full path goes after a tab: Windows shows it in the menu's
// right-hand column (where "Alt+F4" is), so the paths line up and the menu is
// as wide as the longest one. '&' in a path is doubled (not a mnemonic).
static CString MenuPathSuffix(const std::wstring& path) {
    if (path.empty()) return CString();
    CString p(path.c_str());
    p.Replace(L"&", L"&&");
    return CString(L"\t") + p;
}

static void SetFileMenuLabel(CCmdUI* pCmdUI, int unit, const wchar_t* accel,
                              PSystemEngine* engine, const std::wstring& path) {
    std::wstring vol = engine->GetVolumeNameForUnit(unit);
    CString text;
    if (vol.empty())
        text.Format(L"Open Unit #%s - No Volume...", accel);
    else
        text.Format(L"Open Unit #%s - %s...", accel, vol.c_str());
    if (!vol.empty()) text += MenuPathSuffix(path);
    pCmdUI->SetText(text);
}

std::wstring CMainFrame::UnitImagePath(int unit) const {
    switch (unit) {
        case 4:  return m_bigDiskPath;
        case 5:  return m_scratchDiskPath;
        case 9:  return m_unit9Path;
        case 10: return m_unit10Path;
        case 11: return m_unit11Path;
        case 12: return m_unit12Path;
        default: return std::wstring();
    }
}

void CMainFrame::OnUpdateFileOpenBigDisk(CCmdUI* p) {
    SetFileMenuLabel(p, 4, L"&4", m_engine.get(), UnitImagePath(4));
}
void CMainFrame::OnUpdateFileOpenScratch(CCmdUI* p) {
    SetFileMenuLabel(p, 5, L"&5", m_engine.get(), UnitImagePath(5));
}
void CMainFrame::OnUpdateFileOpenUnit9(CCmdUI* p) {
    SetFileMenuLabel(p, 9, L"&9", m_engine.get(), UnitImagePath(9));
}
void CMainFrame::OnUpdateFileOpenUnit10(CCmdUI* p) {
    SetFileMenuLabel(p, 10, L"1&0", m_engine.get(), UnitImagePath(10));
}
void CMainFrame::OnUpdateFileOpenUnit11(CCmdUI* p) {
    SetFileMenuLabel(p, 11, L"1&1", m_engine.get(), UnitImagePath(11));
}
void CMainFrame::OnUpdateFileOpenUnit12(CCmdUI* p) {
    SetFileMenuLabel(p, 12, L"1&2", m_engine.get(), UnitImagePath(12));
}

// ---------------------------------------------------------------------------
// Right-click on a File > Open item → Unmount context menu
// WM_MENURHBUTTONUP fires while the menu is still open. We store the target
// unit, send WM_CANCELMODE to close the menu, then process WM_APP+2 (which
// arrives after the menu is gone) to show the small Unmount popup.
// ---------------------------------------------------------------------------

LRESULT CMainFrame::OnMenuRightButtonUp(WPARAM wParam, LPARAM lParam) {
    HMENU hMenu = (HMENU)lParam;
    int idx     = (int)wParam;
    UINT cmdId  = ::GetMenuItemID(hMenu, idx);

    int unit = 0;
    if      (cmdId == ID_FILE_OPEN_BIGDISK) unit = 4;
    else if (cmdId == ID_FILE_OPEN_SCRATCH) unit = 5;
    else if (cmdId == ID_FILE_OPEN_UNIT9)   unit = 9;
    else if (cmdId == ID_FILE_OPEN_UNIT10)  unit = 10;
    else if (cmdId == ID_FILE_OPEN_UNIT11)  unit = 11;
    else if (cmdId == ID_FILE_OPEN_UNIT12)  unit = 12;

    if (unit == 0) return 0;
    if (m_engine->GetVolumeNameForUnit(unit).empty()) return 0; // nothing to unmount

    m_rightClickUnit = unit;
    ::GetCursorPos(&m_rightClickPt);
    PostMessageW(WM_SHOW_UNMOUNT_MENU); // deferred -- fires after menu closes
    SendMessageW(WM_CANCELMODE);  // closes the menu synchronously
    return 0;
}

LRESULT CMainFrame::OnShowUnmountMenu(WPARAM, LPARAM) {
    int unit = m_rightClickUnit;
    m_rightClickUnit = 0;
    if (unit == 0) return 0;

    std::wstring vol = m_engine->GetVolumeNameForUnit(unit);
    if (vol.empty()) return 0; // was unmounted between the two messages somehow

    UINT unmountId = (unit == 4) ? ID_FILE_UNMOUNT_4  :
                     (unit == 5) ? ID_FILE_UNMOUNT_5  :
                     (unit == 9) ? ID_FILE_UNMOUNT_9  :
                     (unit == 10) ? ID_FILE_UNMOUNT_10 :
                     (unit == 11) ? ID_FILE_UNMOUNT_11 : ID_FILE_UNMOUNT_12;

    CMenu ctx;
    ctx.CreatePopupMenu();
    CString label;
    label.Format(L"Unmount Unit #%d - %s", unit, vol.c_str());
    ctx.AppendMenuW(MF_STRING, unmountId, label);
    ctx.TrackPopupMenu(TPM_LEFTBUTTON | TPM_RIGHTBUTTON,
                       m_rightClickPt.x, m_rightClickPt.y, this);
    return 0;
}

// ---------------------------------------------------------------------------
// Unmount handlers -- called when the user confirms via the right-click popup
// ---------------------------------------------------------------------------

void CMainFrame::DoUnmount(int unit) {
    std::wstring vol = m_engine->GetVolumeNameForUnit(unit);
    if (vol.empty()) return;

    CString warn;
    if (unit == 4) {
        warn.Format(
            L"Unit #4 (%s) is the system disk.\r\n"
            L"Unmounting it while UCSD Pascal is running will crash the system.\r\n\r\n"
            L"Unmount anyway?", vol.c_str());
    } else {
        warn.Format(L"Unmount unit #%d (%s)?\r\n\r\nAny unsaved changes to that volume will be lost.",
                    unit, vol.c_str());
    }
    if (AfxMessageBox(warn, MB_YESNO | MB_ICONWARNING) != IDYES) return;

    m_engine->UnmountUnit(unit);

    // Clear the remembered path so SaveSettings() and TryAutoLoad() both
    // see the unit as unloaded on the next launch.
    if      (unit == 4) m_bigDiskPath.clear();
    else if (unit == 5) m_scratchDiskPath.clear();
    else if (unit == 9) m_unit9Path.clear();
    else if (unit == 10) m_unit10Path.clear();
    else if (unit == 11) m_unit11Path.clear();
    else                m_unit12Path.clear();

    SaveSettings();
}

void CMainFrame::OnFileUnmount4()  { DoUnmount(4);  }
void CMainFrame::OnFileUnmount5()  { DoUnmount(5);  }
void CMainFrame::OnFileUnmount9()  { DoUnmount(9);  }
void CMainFrame::OnFileUnmount10() { DoUnmount(10); }
void CMainFrame::OnFileUnmount11() { DoUnmount(11); }
void CMainFrame::OnFileUnmount12() { DoUnmount(12); }

// UPDATE_COMMAND_UI for Unmount items: enabled only when something is
// mounted on that unit; label shows the volume name so the user can
// confirm exactly what they are about to unmount.
static void SetUnmountLabel(CCmdUI* pCmdUI, int unit, PSystemEngine* engine, const std::wstring& path) {
    std::wstring vol = engine->GetVolumeNameForUnit(unit);
    bool loaded = !vol.empty();
    pCmdUI->Enable(loaded ? TRUE : FALSE);
    CString text;
    if (loaded)
        text.Format(L"Unmount Unit #%d - %s", unit, vol.c_str());
    else
        text.Format(L"Unmount Unit #%d", unit);
    if (loaded) text += MenuPathSuffix(path);
    pCmdUI->SetText(text);
}
void CMainFrame::OnUpdateFileUnmount4(CCmdUI*  p) { SetUnmountLabel(p, 4,  m_engine.get(), UnitImagePath(4)); }
void CMainFrame::OnUpdateFileUnmount5(CCmdUI*  p) { SetUnmountLabel(p, 5,  m_engine.get(), UnitImagePath(5)); }
void CMainFrame::OnUpdateFileUnmount9(CCmdUI*  p) { SetUnmountLabel(p, 9,  m_engine.get(), UnitImagePath(9)); }
void CMainFrame::OnUpdateFileUnmount10(CCmdUI* p) { SetUnmountLabel(p, 10, m_engine.get(), UnitImagePath(10)); }
void CMainFrame::OnUpdateFileUnmount11(CCmdUI* p) { SetUnmountLabel(p, 11, m_engine.get(), UnitImagePath(11)); }
void CMainFrame::OnUpdateFileUnmount12(CCmdUI* p) { SetUnmountLabel(p, 12, m_engine.get(), UnitImagePath(12)); }

// ---------------------------------------------------------------------------
// Trace logging options dialog
// ---------------------------------------------------------------------------
// Inline dialog class: no separate .h file needed since it's only used here.
// OnCommand handles IDC_TRACE_ENABLE state changes to enable/disable
// the browse button live. Execution mode (P-Code/Z80) and Preserve Z80
// Register Compatibility are no longer part of this dialog -- they're
// their own items directly under the Options menu now, independent of
// whether trace logging is enabled.
class CTraceOptionsDlg : public CDialog {
public:
    bool         m_enabled   = false;
    bool         m_alsoZ80   = false;
    std::wstring m_path;

    CTraceOptionsDlg(bool enabled, bool alsoZ80, const std::wstring& path, CWnd* parent)
        : CDialog(IDD_TRACE_OPTIONS, parent)
        , m_enabled(enabled), m_alsoZ80(alsoZ80), m_path(path) {}

    BOOL OnInitDialog() override {
        CDialog::OnInitDialog();
        ((CButton*)GetDlgItem(IDC_TRACE_ENABLE))->SetCheck(m_enabled ? BST_CHECKED : BST_UNCHECKED);
        SetDlgItemTextW(IDC_TRACE_PATH, m_path.c_str());
        ((CButton*)GetDlgItem(IDC_TRACE_ALSO_Z80))->SetCheck(m_alsoZ80 ? BST_CHECKED : BST_UNCHECKED);
        UpdateDependentState();
        return TRUE;
    }

    void UpdateDependentState() {
        bool on = (((CButton*)GetDlgItem(IDC_TRACE_ENABLE))->GetCheck() == BST_CHECKED);
        GetDlgItem(IDC_TRACE_PATH)->EnableWindow(on);
        GetDlgItem(IDC_TRACE_BROWSE)->EnableWindow(on);
        GetDlgItem(IDC_TRACE_ALSO_Z80)->EnableWindow(on);
    }

    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override {
        if (LOWORD(wParam) == IDC_TRACE_ENABLE &&
            (HIWORD(wParam) == BN_CLICKED || HIWORD(wParam) == 0)) {
            UpdateDependentState();
            return TRUE;
        }
        if (LOWORD(wParam) == IDC_TRACE_BROWSE) {
            CFileDialog dlg(FALSE, L"txt", m_path.empty() ? nullptr : m_path.c_str(),
                            OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT,
                            L"Text files (*.txt)|*.txt|All files (*.*)|*.*||", this);
            dlg.m_ofn.lpstrTitle = L"Trace log file";
            if (dlg.DoModal() == IDOK)
                SetDlgItemTextW(IDC_TRACE_PATH, dlg.GetPathName());
            return TRUE;
        }
        return CDialog::OnCommand(wParam, lParam);
    }

    void OnOK() override {
        m_enabled = (((CButton*)GetDlgItem(IDC_TRACE_ENABLE))->GetCheck() == BST_CHECKED);
        m_alsoZ80 = (((CButton*)GetDlgItem(IDC_TRACE_ALSO_Z80))->GetCheck() == BST_CHECKED);
        CString p; GetDlgItemTextW(IDC_TRACE_PATH, p); m_path = (LPCWSTR)p;
        CDialog::OnOK();
    }
};

void CMainFrame::OnOptionsTrace() {
    std::wstring tracePath = ExeDir() + L"\\trace.txt";

    CTraceOptionsDlg dlg(m_traceEnabled, m_traceAlsoZ80, tracePath, this);
    if (dlg.DoModal() != IDOK) return;

    m_traceAlsoZ80 = dlg.m_alsoZ80;
    m_engine->SetTraceAlsoZ80(m_traceAlsoZ80);

    // P-code logging is always included when tracing is on -- execution
    // mode (its own menu items now) controls HOW it runs, not what gets
    // logged.
    bool wantEnabled = dlg.m_enabled;
    if (wantEnabled && !dlg.m_path.empty()) {
        std::wstring err;
        if (m_engine->EnableTrace(dlg.m_path, err)) {
            m_traceEnabled = true;
        } else {
            AfxMessageBox(err.c_str(), MB_ICONERROR);
            wantEnabled = false;
        }
    }
    if (!wantEnabled && m_traceEnabled) {
        m_engine->DisableTrace();
        m_traceEnabled = false;
    }

    SaveSettings();
    UpdateStatusBarText();
}

void CMainFrame::OnOptionsExecModePCode() {
    m_traceZ80Mode = false;
    m_engine->SetNativePcodeOps(true);
    SaveSettings();
    UpdateStatusBarText();
}

void CMainFrame::OnOptionsExecModeZ80() {
    m_traceZ80Mode = true;
    m_engine->SetNativePcodeOps(false);
    SaveSettings();
    UpdateStatusBarText();
}

void CMainFrame::OnOptionsPreserveZ80Regs() {
    // Meaningless (and disabled/grayed via OnUpdateOptionsPreserveZ80Regs)
    // in Z80 mode, since there's no native dispatch there at all -- but
    // guard here too in case the command still reaches us some other way.
    if (m_traceZ80Mode) return;
    m_preserveZ80RegisterCompat = !m_preserveZ80RegisterCompat;
    m_engine->SetPreserveZ80RegisterCompat(m_preserveZ80RegisterCompat);
    SaveSettings();
    if (m_preserveZ80RegisterCompat && m_engine->InterpreterMemoryReclaimed()) {
        // Compatibility mode runs Z80 code paths, and that code has been
        // overwritten by the heap: restart without reclaiming.
        AfxMessageBox(L"Z80 register compatibility needs the Z80 interpreter's code, whose memory has been "
                      L"reclaimed. The P-System will now restart without reclaiming it.", MB_ICONINFORMATION);
        RestartEngineWithCurrentPaths();
    }
}

void CMainFrame::OnUpdateOptionsExecModePCode(CCmdUI* pCmdUI) { pCmdUI->SetRadio(!m_traceZ80Mode); }
void CMainFrame::OnUpdateOptionsExecModeZ80(CCmdUI* pCmdUI)   { pCmdUI->SetRadio(m_traceZ80Mode); }
void CMainFrame::OnOptionsReclaimInterp() {
    if (m_traceZ80Mode || m_preserveZ80RegisterCompat) return;   // grayed out then
    m_reclaimInterpMemory = !m_reclaimInterpMemory;
    SaveSettings();
    if (AfxMessageBox(m_reclaimInterpMemory
            ? L"Reclaim the Z80 interpreter's and BIOS memory (about 7.4 KB more for the P-System)?\n\n"
              L"This takes effect when the P-System restarts. Run-time errors are reported by the "
              L"P-System as usual. Should anything ever still need Z80 code, the emulator stops "
              L"with a message.\n\nRestart the P-System now?"
            : L"The Z80 interpreter's memory will no longer be reclaimed.\n\nRestart the P-System now?",
            MB_YESNO | MB_ICONQUESTION) == IDYES)
        RestartEngineWithCurrentPaths();
}

void CMainFrame::OnOptionsHostClock() {
    m_hostClock = !m_hostClock;
    SaveSettings();
    if (AfxMessageBox(m_hostClock
            ? L"The P-System will start with the PC's date, and TIME will return the PC's time of day "
              L"(in sixtieths of a second).\n\nThis takes effect when the P-System restarts. Restart now?"
            : L"The P-System will start with the date stored on the boot disk, and TIME will return 0.\n\n"
              L"This takes effect when the P-System restarts. Restart now?",
            MB_YESNO | MB_ICONQUESTION) == IDYES)
        RestartEngineWithCurrentPaths();
}

void CMainFrame::OnUpdateOptionsHostClock(CCmdUI* pCmdUI) {
    pCmdUI->SetCheck(m_hostClock ? 1 : 0);
}

// Harvard mode (PSystemEngine::SetHarvard): code segments in a 64K
// instruction space of their own, so the P-System's stack and heap get the
// memory they took. Only in the reclaimed layout (P-Code mode, register
// compatibility off, reclaim on) -- grayed out otherwise, keeping its check.
void CMainFrame::OnOptionsHarvard() {
    if (m_traceZ80Mode || m_preserveZ80RegisterCompat || !m_reclaimInterpMemory) return;   // grayed out then
    m_harvardMode = !m_harvardMode;
    SaveSettings();
    if (AfxMessageBox(m_harvardMode
            ? L"Harvard mode: code segments are kept in a separate 64K instruction space (I-space), so the "
              L"stack and heap (D-space) get the memory the code took -- several thousand words more for "
              L"large programs such as the compilers.\n\nAssembly-language procedures cannot run in this "
              L"mode.\n\nThis takes effect when the P-System restarts. Restart now?"
            : L"Code segments will be loaded into the P-System's own memory again (no separate I-space).\n\n"
              L"This takes effect when the P-System restarts. Restart now?",
            MB_YESNO | MB_ICONQUESTION) == IDYES)
        RestartEngineWithCurrentPaths();
}

void CMainFrame::OnUpdateOptionsHarvard(CCmdUI* pCmdUI) {
    pCmdUI->Enable(!m_traceZ80Mode && !m_preserveZ80RegisterCompat && m_reclaimInterpMemory);
    pCmdUI->SetCheck(m_harvardMode ? 1 : 0);
}

// Pause / Resume: the engine thread stops executing (PSystemEngine::SetPaused);
// keys typed meanwhile are kept until Resume. Not during Verify P-System.
void CMainFrame::OnOptionsPause() {
    if (m_verifyRunner || !m_engine || !m_engine->IsRunning()) return;
    m_paused = !m_paused;
    m_engine->SetPaused(m_paused);
    UpdateStatusBarText();
}

void CMainFrame::OnUpdateOptionsPause(CCmdUI* pCmdUI) {
    pCmdUI->Enable(!m_verifyRunner && m_engine && m_engine->IsRunning() && !m_engine->HasHalted());
    pCmdUI->SetText(m_paused ? L"&Resume" : L"&Pause");
}

void CMainFrame::OnUpdateOptionsReclaimInterp(CCmdUI* pCmdUI) {
    pCmdUI->Enable(!m_traceZ80Mode && !m_preserveZ80RegisterCompat);
    pCmdUI->SetCheck(m_reclaimInterpMemory ? 1 : 0);
}

void CMainFrame::OnUpdateOptionsPreserveZ80Regs(CCmdUI* pCmdUI) {
    pCmdUI->Enable(!m_traceZ80Mode);
    pCmdUI->SetCheck(m_preserveZ80RegisterCompat ? 1 : 0);
}

void CMainFrame::OnOptionsFontSmall()  { m_fontPointSize = 8;  RecreateFont(); ResizeToFitGrid(); SaveSettings(); }
void CMainFrame::OnOptionsFontMedium() { m_fontPointSize = 11; RecreateFont(); ResizeToFitGrid(); SaveSettings(); }
void CMainFrame::OnOptionsFontLarge()  { m_fontPointSize = 14; RecreateFont(); ResizeToFitGrid(); SaveSettings(); }

// ---------------------------------------------------------------------------
// Import Windows File to UCSD Pascal Volume
// ---------------------------------------------------------------------------

// Modal dialog for picking which unit to import into. Populated at runtime
// with the actual volume names from each loaded disk image. Units whose
// image files are not loaded are shown disabled so the user can't pick them.
class CImportUnitDlg : public CDialog {
public:
    int m_selectedUnit = 4; // in: default unit; out: chosen unit

    struct UnitStatus {
        int    unit;
        bool   loaded;
        CString label; // e.g. "Unit #4 -- BIGGY  (Big Disk)"
    };
    std::vector<UnitStatus> m_units;

    CImportUnitDlg(std::vector<UnitStatus> units, CWnd* parent)
        : CDialog(IDD_IMPORT_UNIT, parent), m_units(std::move(units)) {}

    BOOL OnInitDialog() override {
        CDialog::OnInitDialog();
        static const int kIDs[] = { IDC_IMPORT_RADIO4, IDC_IMPORT_RADIO5, IDC_IMPORT_RADIO9,
                                    IDC_IMPORT_RADIO10, IDC_IMPORT_RADIO11, IDC_IMPORT_RADIO12 };
        int firstLoaded = -1;
        for (int i = 0; i < (int)m_units.size() && i < 6; i++) {
            CButton* btn = (CButton*)GetDlgItem(kIDs[i]);
            if (!btn) continue;
            btn->SetWindowTextW(m_units[i].label);
            btn->EnableWindow(m_units[i].loaded ? TRUE : FALSE);
            if (m_units[i].loaded) {
                if (firstLoaded < 0) firstLoaded = i;
                if (m_units[i].unit == m_selectedUnit) btn->SetCheck(BST_CHECKED);
            }
        }
        // If the default unit isn't loaded, fall back to the first that is
        bool defaultFound = false;
        for (auto& u : m_units) if (u.unit == m_selectedUnit && u.loaded) { defaultFound = true; break; }
        if (!defaultFound && firstLoaded >= 0) {
            m_selectedUnit = m_units[firstLoaded].unit;
            ((CButton*)GetDlgItem(kIDs[firstLoaded]))->SetCheck(BST_CHECKED);
        }
        return TRUE;
    }

    void OnOK() override {
        static const int kIDs[] = { IDC_IMPORT_RADIO4, IDC_IMPORT_RADIO5, IDC_IMPORT_RADIO9,
                                    IDC_IMPORT_RADIO10, IDC_IMPORT_RADIO11, IDC_IMPORT_RADIO12 };
        for (int i = 0; i < (int)m_units.size() && i < 6; i++) {
            CButton* btn = (CButton*)GetDlgItem(kIDs[i]);
            if (btn && btn->GetCheck() == BST_CHECKED) {
                m_selectedUnit = m_units[i].unit;
                break;
            }
        }
        CDialog::OnOK();
    }
};

void CMainFrame::OnOptionsImportFile() {
    // ---- Step 1: pick the Windows source file ----
    CFileDialog dlgFile(TRUE, nullptr, nullptr,
        OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_PATHMUSTEXIST,
        L"All Files (*.*)|*.*||", this);
    dlgFile.m_ofn.lpstrTitle = L"Select File to Import into UCSD Pascal Volume";
    if (dlgFile.DoModal() != IDOK) return;
    std::wstring srcPath = (LPCWSTR)dlgFile.GetPathName();

    // ---- Step 2: pick the target unit ----
    // Build labels showing unit number, volume name, and load state
    static const struct { int unit; const wchar_t* desc; } kUnits[] = {
        { 4, L"Big Disk"     },
        { 5, L"Scratch Disk" },
        { 9, L"Floppy / Unit #9"  },
        {10, L"Floppy / Unit #10" },
        {11, L"Unit #11" },
        {12, L"Unit #12" },
    };
    std::vector<CImportUnitDlg::UnitStatus> statuses;
    for (auto& ku : kUnits) {
        CImportUnitDlg::UnitStatus s;
        s.unit = ku.unit;
        std::wstring volName = m_engine->GetVolumeNameForUnit(ku.unit);
        s.loaded = !volName.empty();
        // E.g. "Unit #4 - BIGGY  (Big Disk)" or "Unit #9  (not loaded)"
        CString lbl;
        if (s.loaded)
            lbl.Format(L"Unit #%d  -  %s  (%s)", ku.unit, volName.c_str(), ku.desc);
        else
            lbl.Format(L"Unit #%d  (%s  -  not loaded)", ku.unit, ku.desc);
        s.label = lbl;
        statuses.push_back(s);
    }

    CImportUnitDlg dlgUnit(statuses, this);
    // Default to unit 4 if loaded, otherwise the first loaded unit
    dlgUnit.m_selectedUnit = 4;
    if (dlgUnit.DoModal() != IDOK) return;
    int targetUnit = dlgUnit.m_selectedUnit;

    // ---- Step 3: perform the import ----
    std::wstring err = m_engine->ImportFileToVolume(targetUnit, srcPath);
    if (!err.empty()) {
        AfxMessageBox((L"Import failed:\r\n\r\n" + err).c_str(), MB_OK | MB_ICONERROR);
        return;
    }

    // ---- Step 4: report success ----
    // Derive the UCSD filename the same way ImportFileToVolume does
    std::wstring baseName;
    size_t slashPos = srcPath.find_last_of(L"\\/");
    baseName = (slashPos == std::wstring::npos) ? srcPath : srcPath.substr(slashPos + 1);
    for (wchar_t& c : baseName) c = (wchar_t)towupper(c);
    std::wstring ucsdName;
    for (wchar_t c : baseName) {
        if ((c >= L'A' && c <= L'Z') || (c >= L'0' && c <= L'9') || c == L'.' || c == L'_') {
            ucsdName += c;
            if (ucsdName.size() == 15) break;
        }
    }
    std::wstring volName = m_engine->GetVolumeNameForUnit(targetUnit);
    CString msg;
    msg.Format(L"Successfully imported:\r\n\r\n"
               L"  Windows file: %s\r\n"
               L"  UCSD name:    %s\r\n"
               L"  Volume:       %s (unit #%d)\r\n\r\n"
               L"The file is now visible in the UCSD Filer's directory listing.",
               srcPath.c_str(), ucsdName.c_str(), volName.c_str(), targetUnit);
    AfxMessageBox(msg, MB_OK | MB_ICONINFORMATION);
}

// ---------------------------------------------------------------------------
// Export UCSD P-System File to Windows
// ---------------------------------------------------------------------------
//
// CExportFileDlg: modal dialog that lets the user pick a mounted volume and
// one of its directory entries, then confirms the export. Uses OnCommand
// (not a per-method message map) so this can be a self-contained local class.
//
class CExportFileDlg : public CDialog {
public:
    int          m_selectedUnit = 4;  // in/out: unit number chosen
    std::wstring m_selectedFile;      // out: the UCSD filename chosen
    PSystemEngine* m_engine = nullptr;

    CExportFileDlg(PSystemEngine* eng, CWnd* parent)
        : CDialog(IDD_EXPORT_FILE, parent), m_engine(eng) {}

    BOOL OnInitDialog() override {
        CDialog::OnInitDialog();
        // Populate the combo with every unit that has a volume loaded
        CComboBox* cb = (CComboBox*)GetDlgItem(IDC_EXPORT_UNIT_COMBO);
        static const int kUnits[] = { 4, 5, 9, 10, 11, 12 };
        int firstIdx = -1;
        for (int u : kUnits) {
            std::wstring vol = m_engine->GetVolumeNameForUnit(u);
            if (vol.empty()) continue;
            CString label;
            label.Format(L"Unit #%d - %s", u, vol.c_str());
            int idx = cb->AddString(label);
            cb->SetItemData(idx, (DWORD_PTR)u);
            if (firstIdx < 0) firstIdx = 0;
        }
        if (cb->GetCount() == 0) {
            AfxMessageBox(L"No volumes are currently mounted.", MB_OK | MB_ICONINFORMATION);
            EndDialog(IDCANCEL);
            return TRUE;
        }
        cb->SetCurSel(0);
        m_selectedUnit = (int)cb->GetItemData(0);
        PopulateFileList();
        GetDlgItem(IDOK)->EnableWindow(FALSE);
        return TRUE;
    }

    void PopulateFileList() {
        CListBox* lb = (CListBox*)GetDlgItem(IDC_EXPORT_FILE_LIST);
        lb->ResetContent();
        m_names.clear();
        auto entries = m_engine->GetVolumeDirectory(m_selectedUnit);
        static const char* kinds[] = { "     ","     ","CODE ","TEXT ","INFO ","DATA ","GRAF ","FOTO " };
        for (int origIdx = 0; origIdx < (int)entries.size(); origIdx++) {
            auto& e = entries[origIdx];
            // Format: "FILENAME.EXT     TYPE   N blks   M bytes"
            CString line;
            const char* kindStr = (e.kind >= 0 && e.kind <= 7) ? kinds[e.kind] : "     ";
            long bytes = e.ByteCount();
            line.Format(L"%-16s  %S  %4d blks  %7ld bytes",
                        e.name.c_str(), kindStr,
                        (int)(e.lastBlock - e.firstBlock), bytes);
            int listPos = lb->AddString(line);
            // Store the ORIGINAL directory index in item data.
            // Previously this stored 'listPos' (the post-AddString position)
            // which was harmless when the list was unsorted but broke silently
            // with LBS_SORT -- the sorted position and the directory position
            // diverge, so GetCurSel() returned a sorted index but m_names[]
            // was indexed by directory order, causing the wrong file to be
            // exported.  Storing origIdx here and using GetItemData() in OnOK
            // makes the lookup correct regardless of sort order.
            lb->SetItemData(listPos, (DWORD_PTR)origIdx);
            m_names.push_back(e.name); // indexed by directory order (origIdx)
        }
        GetDlgItem(IDC_EXPORT_INFO)->SetWindowTextW(
            entries.empty() ? L"(volume has no files)" : L"");
        GetDlgItem(IDOK)->EnableWindow(FALSE);
    }

    BOOL OnCommand(WPARAM wParam, LPARAM lParam) override {
        UINT id   = LOWORD(wParam);
        UINT code = HIWORD(wParam);
        if (id == IDC_EXPORT_UNIT_COMBO && code == CBN_SELCHANGE) {
            CComboBox* cb = (CComboBox*)GetDlgItem(IDC_EXPORT_UNIT_COMBO);
            int sel = cb->GetCurSel();
            if (sel >= 0) m_selectedUnit = (int)cb->GetItemData(sel);
            PopulateFileList();
            return TRUE;
        }
        if (id == IDC_EXPORT_FILE_LIST && code == LBN_SELCHANGE) {
            CListBox* lb = (CListBox*)GetDlgItem(IDC_EXPORT_FILE_LIST);
            int listSel  = lb->GetCurSel();
            bool haveSelection = (listSel >= 0);
            GetDlgItem(IDOK)->EnableWindow(haveSelection ? TRUE : FALSE);
            if (haveSelection) {
                // Use GetItemData to get the original directory index, not
                // the (potentially sorted) list position.
                int origIdx = (int)lb->GetItemData(listSel);
                auto entries = m_engine->GetVolumeDirectory(m_selectedUnit);
                if (origIdx >= 0 && origIdx < (int)entries.size()) {
                    auto& e = entries[origIdx];
                    CString info;
                    info.Format(L"Blocks %u-%u  |  %ld bytes",
                                (unsigned)e.firstBlock, (unsigned)(e.lastBlock - 1),
                                e.ByteCount());
                    GetDlgItem(IDC_EXPORT_INFO)->SetWindowTextW(info);
                }
            }
            return TRUE;
        }
        return CDialog::OnCommand(wParam, lParam);
    }

    void OnOK() override {
        CListBox* lb = (CListBox*)GetDlgItem(IDC_EXPORT_FILE_LIST);
        int listSel = lb->GetCurSel();
        if (listSel >= 0) {
            // GetItemData holds the original directory index (not the list
            // position), so this correctly retrieves the right name even when
            // the list is sorted differently from the directory order.
            int origIdx = (int)lb->GetItemData(listSel);
            if (origIdx >= 0 && origIdx < (int)m_names.size())
                m_selectedFile = m_names[origIdx];
        }
        CDialog::OnOK();
    }

private:
    std::vector<std::wstring> m_names; // indexed by original directory order
};

void CMainFrame::OnOptionsExportFile() {
    // ---- Step 1: pick source volume and file ----
    CExportFileDlg dlgExport(m_engine.get(), this);
    if (dlgExport.DoModal() != IDOK || dlgExport.m_selectedFile.empty()) return;

    int          srcUnit = dlgExport.m_selectedUnit;
    std::wstring ucsdName = dlgExport.m_selectedFile;

    // ---- Step 2: pick Windows destination ----
    // Default the filename to the UCSD name (already uppercase)
    CFileDialog dlgSave(FALSE, nullptr, ucsdName.c_str(),
        OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST,
        L"All Files (*.*)|*.*||", this);
    dlgSave.m_ofn.lpstrTitle = L"Export UCSD File to Windows";
    if (dlgSave.DoModal() != IDOK) return;
    std::wstring destPath = (LPCWSTR)dlgSave.GetPathName();

    // ---- Step 3: export ----
    std::wstring err = m_engine->ExportFileFromVolume(srcUnit, ucsdName, destPath);
    if (!err.empty()) {
        AfxMessageBox((L"Export failed:\r\n\r\n" + err).c_str(), MB_OK | MB_ICONERROR);
        return;
    }

    // ---- Step 4: report success ----
    std::wstring volName = m_engine->GetVolumeNameForUnit(srcUnit);
    CString msg;
    msg.Format(L"Successfully exported:\r\n\r\n"
               L"  UCSD file: %s  (on %s, unit #%d)\r\n"
               L"  Windows:   %s",
               ucsdName.c_str(), volName.c_str(), srcUnit, destPath.c_str());
    AfxMessageBox(msg, MB_OK | MB_ICONINFORMATION);
}

// Shows live per-opcode dispatch counts (see PSystemEngine's
// m_opcodeFetchCount/m_opcodeEmulatedCount), so we can see which p-code
// opcode to port to native C next. m_engine stays valid for as long as
// CMainFrame exists, so passing the raw pointer here is safe even though
// the dialog doesn't own it.
void CMainFrame::OnViewOpcodeCount() {
    COpcodeCountDlg dlg(m_engine.get(), this);
    dlg.DoModal();
}

void CMainFrame::OnUpdateOptionsTrace(CCmdUI* pCmdUI) {
    pCmdUI->SetCheck(m_traceEnabled ? 1 : 0);
}
void CMainFrame::OnUpdateOptionsFontSmall(CCmdUI* pCmdUI) { pCmdUI->SetRadio(m_fontPointSize == 8); }
void CMainFrame::OnUpdateOptionsFontMedium(CCmdUI* pCmdUI) { pCmdUI->SetRadio(m_fontPointSize == 11); }
void CMainFrame::OnUpdateOptionsFontLarge(CCmdUI* pCmdUI) { pCmdUI->SetRadio(m_fontPointSize == 14); }

// Minimal About dialog handler using the plain Windows API rather than
// a dedicated CDialog subclass, to keep this first version simple.
void CMainFrame::OnHelpAbout() {
    CDialog dlg(IDD_ABOUTBOX, this);
    dlg.DoModal();
}


// ===================== Verify P-System =====================
// Files (next to the program, in a "verify" folder):
//   verify\SOURCE.BLK, verify\COMPASM.BLK   source volumes (units 5 and 9)
//   verify\VERIFY.SCRIPT                    keyboard script
//   verify\reference_boot.BLK               copy of unit 4 taken when recording
//   verify\reference.pcl                    reference P-code log (~1.2 GB)
//   verify\work\                            fresh disk copies for each run
//   verify\last_verify_report.txt           result of the last run
std::wstring CMainFrame::VerifyDir() { return ExeDir() + L"\\verify"; }

void CMainFrame::OnVerifyRecord()  { StartVerify(true); }
void CMainFrame::OnVerifyRecordReclaimed() { StartVerify(true, true); }
void CMainFrame::OnVerifyCompare() { StartVerify(false); }
void CMainFrame::OnVerifyCancel() {
    if (!m_verifyRunner) return;
    StopEngineThread();
    m_engine->StopVerifyLog();
    FinishVerify(L"Cancelled.", false);
}
void CMainFrame::OnUpdateVerifyRecord(CCmdUI* pCmdUI)  { pCmdUI->Enable(!m_verifyRunner && !m_pascalBinPath.empty() && !m_bigDiskPath.empty()); }
void CMainFrame::OnUpdateVerifyCompare(CCmdUI* pCmdUI) { pCmdUI->Enable(!m_verifyRunner && !m_pascalBinPath.empty()); }
void CMainFrame::OnUpdateVerifyCancel(CCmdUI* pCmdUI)  { pCmdUI->Enable(m_verifyRunner != nullptr); }

// Two reference logs, one per memory layout (they cannot be compared with
// each other -- almost every value in a record is an address):
//   reference.pcl           the Z80 layout: recorded in Z80 mode (the original
//                           interpreter); verifies Z80 mode and P-Code mode
//                           without the reclaim option
//   reference_reclaimed.pcl the reclaimed layout (P-Code mode, register
//                           compatibility off, reclaim option): recorded by the
//                           native engine itself -- a regression baseline
// Verify uses the one that matches the current settings.
void CMainFrame::StartVerify(bool record, bool reclaimedRecord) {
    const bool reclaimed = record ? reclaimedRecord
                                  : (!m_traceZ80Mode && !m_preserveZ80RegisterCompat && m_reclaimInterpMemory);
    const std::wstring dir = VerifyDir();
    const std::wstring source = dir + L"\\SOURCE.BLK", compasm = dir + L"\\COMPASM.BLK", script = dir + L"\\VERIFY.SCRIPT";
    const std::wstring refBoot = dir + (reclaimed ? L"\\reference_reclaimed_boot.BLK" : L"\\reference_boot.BLK");
    const std::wstring refLog = dir + (reclaimed ? L"\\reference_reclaimed.pcl" : L"\\reference.pcl"), work = dir + L"\\work";
    auto exists = [](const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; };
    for (const std::wstring& f : { source, compasm, script })
        if (!exists(f)) { AfxMessageBox((L"Verify P-System needs this file:\n\n" + f).c_str(), MB_ICONERROR); return; }
    if (!record && (!exists(refLog) || !exists(refBoot))) {
        AfxMessageBox(reclaimed
            ? L"There is no reclaimed-memory reference log yet (your settings select the reclaimed-memory layout: "
              L"P-Code mode, register compatibility off, reclaim option on).\n\n"
              L"Use Options > Verify P-System > Record Reference Log (P-Code mode, reclaimed memory) first."
            : L"There is no reference log yet.\n\nUse Options > Verify P-System > Record Reference Log first.", MB_ICONINFORMATION);
        return;
    }
    // read and check the script before touching anything
    std::string text;
    { FILE* f = nullptr; _wfopen_s(&f, script.c_str(), L"rb");
      if (f) { char buf[4096]; size_t n; while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n); fclose(f); } }
    std::vector<VerifyStep> steps; std::string perr;
    if (!ParseVerifyScript(text, steps, perr)) { AfxMessageBox(CString(L"VERIFY.SCRIPT: ") + CString(perr.c_str()), MB_ICONERROR); return; }

    CString prompt;
    if (record && reclaimed)
        prompt.Format(L"Record the reclaimed-memory reference log?\n\n"
                      L"The P-System restarts on fresh copies of the disks in P-Code mode with Z80 register "
                      L"compatibility off and the Z80 interpreter and BIOS memory reclaimed, and runs the "
                      L"Verify script (%u steps), logging every P-code instruction to reference_reclaimed.pcl "
                      L"in %s (~1.2 GB).\n\nThis reference is recorded by the native engine itself: later "
                      L"verifications against it prove that behaviour in this layout has not changed. "
                      L"(The Z80 reference proves the native engine against the original interpreter.)\n\n"
                      L"The current unit #4 disk is copied as this reference's boot disk. Your own disks are "
                      L"not changed. The keyboard is disabled until it finishes.",
                      (unsigned)steps.size(), dir.c_str());
    else if (record)
        prompt.Format(L"Record the Verify P-System reference log?\n\n"
                      L"The P-System restarts on fresh copies of the disks and, in Z80 mode, compiles the "
                      L"compiler, assembler, L2, operating system, Filer and editor and assembles the Z80 "
                      L"interpreter (%u script steps). Every P-code instruction is logged (about 49 million; "
                      L"a ~1.2 GB file in %s).\n\nThe current unit #4 disk is copied as the reference boot disk. "
                      L"Your own disks are not changed. The keyboard is disabled until it finishes.",
                      (unsigned)steps.size(), dir.c_str());
    else
        prompt.Format(L"Verify the P-System against the %s?\n\n"
                      L"The P-System restarts on fresh copies of the reference disks and runs the same script "
                      L"in the current execution mode (%s), comparing every P-code instruction with the "
                      L"reference. It stops at the first difference.\n\nYour own disks are not changed. "
                      L"The keyboard is disabled until it finishes.",
                      reclaimed ? L"reclaimed-memory reference log" : L"reference log",
                      m_traceZ80Mode ? L"Z80" : (reclaimed ? L"P-Code / native, reclaimed memory" : L"P-Code / native"));
    if (AfxMessageBox(prompt, MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;

    StopEngineThread();
    if (record && !CopyFileW(m_bigDiskPath.c_str(), refBoot.c_str(), FALSE)) {
        AfxMessageBox((L"Could not copy the unit #4 disk to\n" + refBoot).c_str(), MB_ICONERROR);
        RestartEngineWithCurrentPaths();
        return;
    }
    std::string err;
    if (!PrepareVerifyDisks(refBoot, source, compasm, work, err)) {
        AfxMessageBox(CString(L"Could not prepare the verify disks:\n\n") + CString(err.c_str()), MB_ICONERROR);
        RestartEngineWithCurrentPaths();
        return;
    }
    m_engine = std::make_unique<PSystemEngine>();
    m_paused = false;
    std::wstring werr;
    if (!m_engine->LoadFiles(m_pascalBinPath, work + L"\\VERIFY_BOOT.BLK", work + L"\\VERIFY_SOURCE.BLK", werr) ||
        !m_engine->MountUnit9(work + L"\\VERIFY_COMPASM.BLK", werr) ||
        !m_engine->StartVerifyLog(refLog, !record, werr)) {
        AfxMessageBox(werr.c_str(), MB_ICONERROR);
        RestartEngineWithCurrentPaths();
        return;
    }
    if (reclaimed) {                                  // the reclaimed-memory layout
        m_engine->SetNativePcodeOps(true);
        m_engine->SetPreserveZ80RegisterCompat(false);
        m_engine->SetReclaimInterpreterMemory(true);
    } else {
        m_engine->SetNativePcodeOps(record ? false : !m_traceZ80Mode);    // the Z80 reference: Z80 mode
        m_engine->SetPreserveZ80RegisterCompat(m_preserveZ80RegisterCompat);
    }
    m_engine->SetTraceAlsoZ80(false);
    m_engine->SetConsoleCapture(true);
    m_verifyRunner = std::make_unique<VerifyRunner>(*m_engine, steps);
    m_verifyRecord = record;
    m_verifyReclaimed = reclaimed;
    m_verifyStartTicks = GetTickCount64();
    StartEngineThread();
    SetTimer(2, 5, nullptr);
    UpdateStatusBarText();
}

void CMainFrame::FinishVerify(const std::wstring& outcome, bool ok) {
    KillTimer(2);
    double secs = (GetTickCount64() - m_verifyStartTicks) / 1000.0;
    CString msg;
    msg.Format(L"Verify P-System -- %s\n\n%s\n\nScript step %u of %u, %llu P-code instructions, %.0f seconds.",
               m_verifyRecord ? (m_verifyReclaimed ? L"record reclaimed-memory reference (P-Code mode)" : L"record reference (Z80 mode)")
                              : (m_verifyReclaimed ? L"compare, P-Code mode, reclaimed memory"
                                                   : (m_traceZ80Mode ? L"compare, Z80 mode" : L"compare, P-Code / native mode")),
               outcome.c_str(), (unsigned)(m_verifyRunner->StepIndex() + (ok ? 0 : 1)), (unsigned)m_verifyRunner->StepCount(),
               (unsigned long long)m_engine->VerifyRecordCount(), secs);
    if (m_engine->VerifyMismatch()) msg += CString(L"\n\n") + CString(m_engine->VerifyMismatchText().c_str());
    if (!m_verifyRunner->Error().empty()) msg += CString(L"\n\n") + CString(m_verifyRunner->Error().c_str());
    // keep a report (and the console transcript) next to the log
    FILE* f = nullptr;
    _wfopen_s(&f, (VerifyDir() + L"\\last_verify_report.txt").c_str(), L"wb");
    if (f) {
        CStringA a(msg);
        fwrite((const char*)a, 1, a.GetLength(), f);
        const std::string& t = m_verifyRunner->Transcript();
        fputs("\r\n\r\n---- console transcript ----\r\n", f);
        for (unsigned char c : t) { if (c == '\r') fputs("\r\n", f); else if (c >= 32 && c < 127) fputc(c, f); }
        fclose(f);
    }
    m_verifyRunner.reset();
    AfxMessageBox(msg, ok ? MB_ICONINFORMATION : MB_ICONWARNING);
    RestartEngineWithCurrentPaths();   // back to the user's own disks and settings
}
