// MainFrm.h : main frame window -- hosts the menu, status bar, and does
// the terminal painting + keyboard input directly (no separate CView,
// since there's no document to speak of -- this is a live system, not
// file-based data).
#pragma once

#include "PSystemEngine.h"
#include "PSystemVerify.h"
#include <memory>
#include <string>

class CMainFrame : public CFrameWnd {
public:
    CMainFrame();
    virtual ~CMainFrame();

    // Called once from CUCSDPascalApp::InitInstance after the window is
    // shown.  Mounts the volumes whose paths are remembered in the registry
    // (nothing is copied, and no volume is taken from the .exe folder; only
    // pascal.bin lives there).  Anything missing is reported by name, and
    // the user chooses again via File > Open.
    void TryAutoLoad();

protected:
    virtual BOOL PreCreateWindow(CREATESTRUCT& cs);

    afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
    afx_msg void OnDestroy();
    afx_msg void OnClose();
    afx_msg void OnPaint();
    afx_msg BOOL OnEraseBkgnd(CDC* pDC); // suppress erase-to-gray before paint
    afx_msg void OnSize(UINT nType, int cx, int cy);
    afx_msg void OnChar(UINT nChar, UINT nRepCnt, UINT nFlags);
    afx_msg void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnFileOpenBigDisk();
    afx_msg void OnFileOpenScratch();
    afx_msg void OnFileOpenUnit9();
    afx_msg void OnFileOpenUnit10();
    afx_msg void OnFileOpenUnit11();
    afx_msg void OnFileOpenUnit12();
    afx_msg void OnFileOpenUnit13();
    afx_msg void OnFileOpenUnit14();
    afx_msg void OnFileExit();
    // UPDATE_COMMAND_UI: rewrite the File > Open items at menu-open time to
    // include the actual UCSD volume name (or "No Volume" if unmounted).
    afx_msg void OnUpdateFileOpenBigDisk(CCmdUI* pCmdUI);
    afx_msg void OnUpdateFileOpenScratch(CCmdUI* pCmdUI);
    afx_msg void OnUpdateFileOpenUnit9(CCmdUI* pCmdUI);
    afx_msg void OnUpdateFileOpenUnit10(CCmdUI* pCmdUI);
    afx_msg void OnUpdateFileOpenUnit11(CCmdUI* pCmdUI);
    afx_msg void OnUpdateFileOpenUnit12(CCmdUI* pCmdUI);
    afx_msg void OnUpdateFileOpenUnit13(CCmdUI* pCmdUI);
    afx_msg void OnUpdateFileOpenUnit14(CCmdUI* pCmdUI);
    // Right-click on a File > Open item (via WM_MENURHBUTTONUP) stores the
    // target unit and posts a deferred message to show the Unmount popup
    // after the main menu has closed.
    afx_msg LRESULT OnMenuRightButtonUp(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnShowUnmountMenu(WPARAM wParam, LPARAM lParam);
    // Unmount command handlers (IDs 32792–32795, dispatched from the
    // right-click Unmount popup created by OnShowUnmountMenu).
    afx_msg void OnFileUnmount4();
    afx_msg void OnFileUnmount5();
    afx_msg void OnFileUnmount9();
    afx_msg void OnFileUnmount10();
    afx_msg void OnFileUnmount11();
    afx_msg void OnFileUnmount12();
    afx_msg void OnFileUnmount13();
    afx_msg void OnFileUnmount14();
    afx_msg void OnUpdateFileUnmount4(CCmdUI* pCmdUI);
    afx_msg void OnUpdateFileUnmount5(CCmdUI* pCmdUI);
    afx_msg void OnUpdateFileUnmount9(CCmdUI* pCmdUI);
    afx_msg void OnUpdateFileUnmount10(CCmdUI* pCmdUI);
    afx_msg void OnUpdateFileUnmount11(CCmdUI* pCmdUI);
    afx_msg void OnUpdateFileUnmount12(CCmdUI* pCmdUI);
    afx_msg void OnUpdateFileUnmount13(CCmdUI* pCmdUI);
    afx_msg void OnUpdateFileUnmount14(CCmdUI* pCmdUI);
    afx_msg void OnOptionsTrace();
    afx_msg void OnOptionsImportFile();
    afx_msg void OnOptionsExportFile();
    afx_msg void OnOptionsExecModePCode();
    afx_msg void OnOptionsExecModeZ80();
    afx_msg void OnOptionsPreserveZ80Regs();
    afx_msg void OnOptionsReclaimInterp();
    afx_msg void OnOptionsHostClock();
    afx_msg void OnOptionsHarvard();
    afx_msg void OnOptionsPause();
    afx_msg void OnOptionsLowWater();
    afx_msg void OnOptionsLowWaterReset();
    afx_msg void OnOptionsFontSmall();
    afx_msg void OnOptionsFontMedium();
    afx_msg void OnOptionsFontLarge();
    afx_msg void OnViewOpcodeCount();
    afx_msg void OnHelpAbout();
    afx_msg void OnVerifyRecord();
    afx_msg void OnVerifyRecordReclaimed();
    afx_msg void OnVerifyCompare();
    afx_msg void OnVerifyCancel();
    afx_msg void OnUpdateVerifyRecord(CCmdUI* pCmdUI);
    afx_msg void OnUpdateVerifyCompare(CCmdUI* pCmdUI);
    afx_msg void OnUpdateVerifyCancel(CCmdUI* pCmdUI);
    afx_msg void OnUpdateOptionsTrace(CCmdUI* pCmdUI);
    afx_msg void OnUpdateOptionsExecModePCode(CCmdUI* pCmdUI);
    afx_msg void OnUpdateOptionsExecModeZ80(CCmdUI* pCmdUI);
    afx_msg void OnUpdateOptionsPreserveZ80Regs(CCmdUI* pCmdUI);
    afx_msg void OnUpdateOptionsReclaimInterp(CCmdUI* pCmdUI);
    afx_msg void OnUpdateOptionsHostClock(CCmdUI* pCmdUI);
    afx_msg void OnUpdateOptionsHarvard(CCmdUI* pCmdUI);
    afx_msg void OnUpdateOptionsPause(CCmdUI* pCmdUI);
    afx_msg void OnUpdateOptionsLowWater(CCmdUI* pCmdUI);
    afx_msg void OnUpdateOptionsLowWaterReset(CCmdUI* pCmdUI);
    afx_msg void OnUpdateOptionsFontSmall(CCmdUI* pCmdUI);
    afx_msg void OnUpdateOptionsFontMedium(CCmdUI* pCmdUI);
    afx_msg void OnUpdateOptionsFontLarge(CCmdUI* pCmdUI);
    // Mouse handlers for text selection (left button) and file-inject (right button)
    afx_msg void OnLButtonDown(UINT nFlags, CPoint point);
    afx_msg void OnMouseMove(UINT nFlags, CPoint point);
    afx_msg void OnLButtonUp(UINT nFlags, CPoint point);
    afx_msg void OnRButtonUp(UINT nFlags, CPoint point);

    DECLARE_MESSAGE_MAP()

private:
    std::unique_ptr<PSystemEngine> m_engine;

    // Verify P-System (see PSystemVerify.h and verify\README.md)
    std::unique_ptr<VerifyRunner> m_verifyRunner;   // non-null while a verification runs
    bool m_verifyRecord = false;                    // true: recording the reference log
    bool m_verifyReclaimed = false;                 // true: the reclaimed-memory layout and its own reference
    bool m_verifyScriptDone = false;
    ULONGLONG m_verifyStartTicks = 0;
    std::wstring VerifyDir();
    void StartVerify(bool record, bool reclaimedRecord = false);
    void FinishVerify(const std::wstring& outcome, bool ok);
    CWinThread* m_workerThread = nullptr;

    std::wstring m_pascalBinPath, m_bigDiskPath, m_scratchDiskPath;
    std::wstring m_unit9Path, m_unit10Path, m_unit11Path, m_unit12Path, m_unit13Path, m_unit14Path;
    std::wstring UnitImagePath(int unit) const;   // the image file mounted on a unit (File menu labels)
    std::wstring MenuPath(int unit) const;        // UnitImagePath padded so the menu's paths line up on the left

    CFont m_font;
    int m_fontPointSize = 11; // 8=small, 11=medium, 14=large
    int m_cellWidth = 8, m_cellHeight = 16;

    CStatusBar m_statusBar;

    bool m_traceEnabled = false;
    bool m_traceZ80Mode  = false; // false = P-Code (native ops), true = Z80 (pure Z80)
    bool m_traceAlsoZ80  = false; // also log every Z80 cpu.step() (for startup analysis)
    bool m_preserveZ80RegisterCompat = true; // see PSystemEngine's own SetPreserveZ80RegisterCompat comment
    bool m_reclaimInterpMemory = false;      // see PSystemEngine::SetReclaimInterpreterMemory (experimental)
    bool m_hostClock = true;                 // the PC's date and time for the P-System (PSystemEngine::SetHostClock)
    bool m_harvardMode = true;               // code in its own I-space (PSystemEngine::SetHarvard); needs the reclaimed layout
    bool m_paused = false;                   // Options > Pause / Resume (PSystemEngine::SetPaused)
    bool m_lowWater = false;                 // Options > Track Least Free Memory (PSystemEngine::SetLowWater)
    bool m_reclaimFaultShown = false;
    bool m_bootFaultShown = false;

    // Right-click-on-menu-item state
    int   m_rightClickUnit = 0;
    POINT m_rightClickPt   = {};

    // Text-selection state (left-button drag on the terminal grid).
    // Coordinates are in character cells (col, row), not pixels.
    // m_selActive is true while the user is holding the mouse button down
    // OR immediately after release (until the next paint clears it).
    bool  m_selActive   = false;      // currently dragging or freshly completed
    bool  m_selDragging = false;      // mouse button is still held
    CPoint m_selAnchor  = {0, 0};    // where the drag started (char cell)
    CPoint m_selCurrent = {0, 0};    // current drag position (char cell)

    void RecreateFont();
    void ResizeToFitGrid();
    void StartEngineThread();
    void StopEngineThread();
    // Stops the current engine (if any), creates a fresh PSystemEngine,
    // reloads pascal.bin/BigDisk/Scratch/Unit9/Unit10 from the
    // remembered paths, and restarts the worker thread. Used whenever
    // any of those paths changes via File > Open.
    void RestartEngineWithCurrentPaths();
    void SaveSettings(); // write volume paths + font size to the registry
    void UpdateStatusBarText();
    void DoUnmount(int unit); // confirm + unmount + clear path + save settings

    static UINT EngineThreadProc(LPVOID pParam);
};
