/* $Id: VBoxVideoWinDbg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * ???
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
 *
 * This file is part of VirtualBox base platform packages, as
 * available from https://www.virtualbox.org.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation, in version 3 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses>.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 */

#define IPRT_NO_CRT_FOR_3RD_PARTY /* lazy */
#include <iprt/win/windows.h>
#define KDEXT_64BIT
#include <iprt/win/wdbgexts.h>

#define VBOXVWD_VERSION_MAJOR 1
#define VBOXVWD_VERSION_MINOR 1

static EXT_API_VERSION g_VBoxVWDVersion = {
        VBOXVWD_VERSION_MAJOR,
        VBOXVWD_VERSION_MINOR,
        EXT_API_VERSION_NUMBER64,
        0
};

/**
 * DLL entry point.
 */
BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID lpReserved)
{
    RT_NOREF(hInstance, dwReason, lpReserved);
    return TRUE;
}

/* note: need to name it this way to make dprintf & other macros defined in wdbgexts.h work */
WINDBG_EXTENSION_APIS64 ExtensionApis = {0};
USHORT SavedMajorVersion;
USHORT SavedMinorVersion;

#ifdef __cplusplus
extern "C"
{
#endif
LPEXT_API_VERSION WDBGAPI ExtensionApiVersion();
VOID WDBGAPI CheckVersion();
VOID WDBGAPI WinDbgExtensionDllInit(PWINDBG_EXTENSION_APIS64 lpExtensionApis, USHORT MajorVersion, USHORT MinorVersion);
#ifdef __cplusplus
}
#endif

LPEXT_API_VERSION WDBGAPI ExtensionApiVersion()
{
    return &g_VBoxVWDVersion;
}

VOID WDBGAPI CheckVersion()
{
    return;
}

VOID WDBGAPI WinDbgExtensionDllInit(PWINDBG_EXTENSION_APIS64 lpExtensionApis, USHORT MajorVersion, USHORT MinorVersion)
{
    ExtensionApis = *lpExtensionApis;
    SavedMajorVersion = MajorVersion;
    SavedMinorVersion = MinorVersion;
}

DECLARE_API(help)
{
    RT_NOREF(args, dwProcessor, dwCurrentPc, hCurrentThread, hCurrentProcess);
    dprintf("**** VirtualBox Video Driver debugging extension ****\n"
            " The following commands are supported: \n"
            " !ms - save memory (video data) to clipboard \n"
            "  usage: !ms <virtual memory address> <width> <height> [bitsPerPixel (default is 32)] [pitch (default is ((width * bpp + 7) >> 3) + 3) & ~3)]\n");
}

DECLARE_API(ms)
{
    ULONG64 u64Mem;
    ULONG64 u64Width;
    ULONG64 u64Height;
    ULONG64 u64Bpp = 32;
    ULONG64 u64NumColors = 3;
    ULONG64 u64Pitch;
    ULONG64 u64DefaultPitch;
    PCSTR pExpr = args;
    RT_NOREF(dwProcessor, dwCurrentPc, hCurrentThread, hCurrentProcess);

    /* address */
    if (!pExpr) { dprintf("address not specified\n"); return; }
    if (!GetExpressionEx(pExpr, &u64Mem, &pExpr)) { dprintf("error evaluating address\n"); return; }
    if (!u64Mem) { dprintf("address value can not be NULL\n"); return; }

    /* width */
    if (!pExpr) { dprintf("width not specified\n"); return; }
    if (!GetExpressionEx(pExpr, &u64Width, &pExpr)) { dprintf("error evaluating width\n"); return; }
    if (!u64Width) { dprintf("width value can not be NULL\n"); return; }

    /* height */
    if (!pExpr) { dprintf("height not specified\n"); return; }
    if (!GetExpressionEx(pExpr, &u64Height, &pExpr)) { dprintf("error evaluating height\n"); return; }
    if (!u64Height) { dprintf("height value can not be NULL\n"); return; }

#if 0
    if (pExpr && GetExpressionEx(pExpr, &u64NumColors, &pExpr))
    {
        if (!u64NumColors) { dprintf("Num Colors value can not be NULL\n"); return; }
    }
#endif

    /* bpp */
    if (pExpr && GetExpressionEx(pExpr, &u64Bpp, &pExpr))
    {
        if (!u64Bpp) { dprintf("bpp value can not be NULL\n"); return; }
    }

    /* pitch */
    u64DefaultPitch = (((((u64Width * u64Bpp) + 7) >> 3) + 3) & ~3ULL);
    if (pExpr && GetExpressionEx(pExpr, &u64Pitch, &pExpr))
    {
        if (u64Pitch < u64DefaultPitch) { dprintf("pitch value can not be less than (%d)\n", (UINT)u64DefaultPitch); return; }
    }
    else
    {
        u64Pitch = u64DefaultPitch;
    }

    dprintf("processing data for address(0x%p), width(%d), height(%d), bpp(%d), pitch(%d)...\n",
                u64Mem, (UINT)u64Width, (UINT)u64Height, (UINT)u64Bpp, (UINT)u64Pitch);

    ULONG64 cbSize = u64DefaultPitch * u64Height;
    PVOID pvBuf = malloc(cbSize);
    if (!pvBuf)
    {
        dprintf("failed to allocate memory buffer of size(%d)\n", (UINT)cbSize);
        return;
    }
    ULONG uRc = 0;
    if(u64DefaultPitch == u64Pitch)
    {
        ULONG cbRead = 0;
        dprintf("reading the entire memory buffer...\n");
        uRc = ReadMemory(u64Mem, pvBuf, cbSize, &cbRead);
        if (!uRc)
        {
            dprintf("Failed to read the memory buffer of size(%d)\n", (UINT)cbSize);
        }
        else if (cbRead != cbSize)
        {
            dprintf("the actual number of bytes read(%d) no equal the requested size(%d)\n", (UINT)cbRead, (UINT)cbSize);
            uRc = 0;
        }

    }
    else
    {
        ULONG64 u64Offset = u64Mem;
        char* pvcBuf = (char*)pvBuf;
        ULONG64 i;
        dprintf("reading memory by chunks since custom pitch is specified...\n");
        for (i = 0; i < u64Height; ++i, u64Offset+=u64Pitch, pvcBuf+=u64DefaultPitch)
        {
            ULONG cbRead = 0;
            uRc = ReadMemory(u64Offset, pvcBuf, u64DefaultPitch, &cbRead);
            if (!uRc)
            {
                dprintf("WARNING!!! Failed to read the memory buffer of size(%d), chunk(%d)\n", (UINT)u64DefaultPitch, (UINT)i);
                dprintf("ignoring this one and the all the rest, using height(%d)\n", (UINT)i);
                u64Height = i;
                uRc = 1;
                break;
            }
            if (cbRead != u64DefaultPitch)
            {
                dprintf("WARNING!!! the actual number of bytes read(%d) not equal the requested size(%d), chunk(%d)\n", (UINT)cbRead, (UINT)u64DefaultPitch, (UINT)i);
                dprintf("ignoring this one and the all the rest, using height(%d)\n", (UINT)i);
                u64Height = i;
                break;
            }
        }
    }

    if (!uRc)
    {
        dprintf("read memory failed\n");
        free(pvBuf);
        return;
    }

    if (!u64Height)
    {
        dprintf("no data to be processed since height it 0\n");
        free(pvBuf);
        return;
    }

    switch (u64Bpp)
    {
        case 32:
        case 24:
        case 16:
#if 0
            if (u64NumColors != 3)
            {
                dprintf("WARNING: unsupported number colors: (%d)\n", (UINT)u64NumColors);
            }
#else
            u64NumColors = 3;
#endif
            break;
        case 8:
            {
#if 1
                u64NumColors = 1;
#endif

                if (u64NumColors == 1)
                {
                    ULONG64 cbSize32 = u64DefaultPitch * 4 * u64Height;
                    PVOID pvBuf32 = malloc(cbSize32);
                    if (!pvBuf32)
                    {
                        dprintf("ERROR: failed to allocate memory buffer of size(%d)", cbSize32);
                        free(pvBuf);
                        return;
                    }
                    byte* pByteBuf32 = (byte*)pvBuf32;
                    byte* pByteBuf = (byte*)pvBuf;
                    memset(pvBuf32, 0, cbSize32);
                    for (UINT i = 0; i < u64Height; ++i)
                    {
                        for (UINT j = 0; j < u64Width; ++j)
                        {
                            pByteBuf32[0] = pByteBuf[0];
                            pByteBuf32[1] = pByteBuf[0];
                            pByteBuf32[2] = pByteBuf[0];
                            pByteBuf32 += 4;
                            pByteBuf += 1;
                        }
                    }
                    free(pvBuf);
                    pvBuf = pvBuf32;
                    u64DefaultPitch *= 4;
                    u64Bpp *= 4;
                }
                else
                {
                    dprintf("WARNING: unsupported number colors: (%d)\n", (UINT)u64NumColors);
                }
            }
            break;
    }
    BITMAP Bmp = {0};
    HBITMAP hBmp;
    dprintf("read memory succeeded..\n");
    Bmp.bmType = 0;
    Bmp.bmWidth = (LONG)u64Width;
    Bmp.bmHeight = (LONG)u64Height;
    Bmp.bmWidthBytes = (LONG)u64DefaultPitch;
    Bmp.bmPlanes = 1;
    Bmp.bmBitsPixel = (WORD)u64Bpp;
    Bmp.bmBits = (LPVOID)pvBuf;
    hBmp = CreateBitmapIndirect(&Bmp);
    if (hBmp)
    {
        if (OpenClipboard(GetDesktopWindow()))
        {
            if (EmptyClipboard())
            {
                if (SetClipboardData(CF_BITMAP, hBmp))
                {
                    dprintf("succeeded!! You can now do <ctrl>+v in your favourite image editor\n");
                }
                else
                {
                    DWORD winEr = GetLastError();
                    dprintf("SetClipboardData failed, err(%d)\n", winEr);
                }
            }
            else
            {
                DWORD winEr = GetLastError();
                dprintf("EmptyClipboard failed, err(%d)\n", winEr);
            }

            CloseClipboard();
        }
        else
        {
            DWORD winEr = GetLastError();
            dprintf("OpenClipboard failed, err(%d)\n", winEr);
        }

        DeleteObject(hBmp);
    }
    else
    {
        DWORD winEr = GetLastError();
        dprintf("CreateBitmapIndirect failed, err(%d)\n", winEr);
    }
}
