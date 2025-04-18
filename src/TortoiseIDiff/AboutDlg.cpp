﻿// TortoiseIDiff - an image diff viewer in TortoiseGit

// Copyright (C) 2023, 2025 - TortoiseGit
// Copyright (C) 2012-2013, 2020 - TortoiseSVN

// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//

#include "stdafx.h"
#include "resource.h"
#include "AboutDlg.h"
#include "Theme.h"
#include "../version.h"

CAboutDlg::CAboutDlg(HWND hParent)
    : m_hParent(hParent)
{
}

CAboutDlg::~CAboutDlg()
{
}

LRESULT CAboutDlg::DlgFunc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (uMsg)
    {
    case WM_INITDIALOG:
        {
            InitDialog(hwndDlg, IDI_TORTOISEIDIFF);
            // initialize the controls
            wchar_t verbuf[1024] = { 0 };
            wchar_t maskbuf[1024] = { 0 };
            ::LoadString (hResource, IDS_VERSION, maskbuf, _countof(maskbuf));
            swprintf_s(verbuf, maskbuf, TGIT_VERMAJOR, TGIT_VERMINOR, TGIT_VERMICRO, TGIT_VERBUILD);
            SetDlgItemText(hwndDlg, IDC_ABOUTVERSION, verbuf);
            CTheme::Instance().SetThemeForDialog(*this, CTheme::Instance().IsDarkTheme());
        }
        return TRUE;
    case WM_COMMAND:
        return DoCommand(LOWORD(wParam));
    default:
        return FALSE;
    }
}

LRESULT CAboutDlg::DoCommand(int id)
{
    switch (id)
    {
    case IDOK:
        [[fallthrough]];
    case IDCANCEL:
        CTheme::Instance().SetThemeForDialog(*this, false);
        EndDialog(*this, id);
        break;
    }
    return 1;
}

