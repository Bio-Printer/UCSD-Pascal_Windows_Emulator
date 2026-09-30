// Version.h
//
// Single source of truth for this app's name/version. Used to build the
// main window's title bar (see CMainFrame::OnCreate in MainFrm.cpp).
//
// When bumping the version, also update the matching FileVersion /
// ProductVersion strings in UCSDPascal.rc by hand -- the .rc VERSIONINFO
// block can't include this header, so the two are kept in sync manually.
#pragma once

#define APP_NAME_W    L"UCSD Pascal II.0 Emulator"
#define APP_VERSION_W L"1.90"
