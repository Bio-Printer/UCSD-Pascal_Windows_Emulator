// UCSDPascalApp.cpp : defines the class behaviors for the application
#include "stdafx.h"
#include "UCSDPascalApp.h"
#include "MainFrm.h"

BEGIN_MESSAGE_MAP(CUCSDPascalApp, CWinApp)
END_MESSAGE_MAP()

CUCSDPascalApp theApp;

CUCSDPascalApp::CUCSDPascalApp() {
}

BOOL CUCSDPascalApp::InitInstance() {
    // InitCommonControlsEx is needed for the status bar / common controls
    // used by the main frame.
    INITCOMMONCONTROLSEX icex;
    icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
    icex.dwICC = ICC_WIN95_CLASSES;
    InitCommonControlsEx(&icex);

    CWinApp::InitInstance();

    // Registry key for settings persistence (under HKCU\Software\EDN\UCSD Pascal II.0 Emulator).
    SetRegistryKey(L"EDN");

    CMainFrame* pFrame = new CMainFrame();
    if (!pFrame) return FALSE;
    m_pMainWnd = pFrame;

    if (!pFrame->LoadFrame(IDR_MAINFRAME)) {
        delete pFrame;
        return FALSE;
    }

    pFrame->ShowWindow(m_nCmdShow);
    pFrame->UpdateWindow();

    // Mount the volumes remembered in the registry (see
    // CMainFrame::TryAutoLoad).  Nothing is copied, and nothing is taken
    // from the .exe folder except pascal.bin; a missing volume is reported
    // and chosen again via the File menu.
    pFrame->TryAutoLoad();

    return TRUE;
}

int CUCSDPascalApp::ExitInstance() {
    return CWinApp::ExitInstance();
}
