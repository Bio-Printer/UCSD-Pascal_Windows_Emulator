// UCSDPascalApp.h : main application class
#pragma once

#ifndef __AFXWIN_H__
#error "include 'stdafx.h' before including this file for PCH"
#endif

#include "resource.h"

class CUCSDPascalApp : public CWinApp {
public:
    CUCSDPascalApp();

    virtual BOOL InitInstance();
    virtual int ExitInstance();

    DECLARE_MESSAGE_MAP()
};

extern CUCSDPascalApp theApp;
