/* $Id: ClipUtil.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * ClipUtil - Clipboard Utility
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#ifdef RT_OS_OS2
# define INCL_BASE
# define INCL_PM
# define INCL_ERRORS
# include <os2.h>
# undef RT_MAX
#endif

#include <iprt/assert.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/process.h>
#include <iprt/string.h>
#include <iprt/stream.h>
#include <iprt/utf16.h>
#include <iprt/zero.h>

#ifdef RT_OS_DARWIN
/** @todo   */
#elif defined(RT_OS_WINDOWS)
# include <iprt/nt/nt-and-windows.h>
#elif !defined(RT_OS_OS2)
# include <X11/Xlib.h>
# include <X11/Xatom.h>
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2) || defined(RT_OS_DARWIN)
# undef MULTI_TARGET_CLIPBOARD
# undef CU_X11
#else
# define MULTI_TARGET_CLIPBOARD
# define CU_X11
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Clipboard format descriptor.
 */
typedef struct CLIPUTILFORMAT
{
    /** Format name. */
    const char *pszName;

#if defined(RT_OS_WINDOWS)
    /** Windows integer format (CF_XXXX). */
    UINT        fFormat;
    /** Windows string format name. */
    const WCHAR *pwszFormat;

#elif defined(RT_OS_OS2)
    /** OS/2 integer format. */
    ULONG       fFormat;
    /** OS/2 string format name. */
    const char *pszFormat;

#elif defined(RT_OS_DARWIN)
    /** Native format (flavor). */
    CFStringRef *hStrFormat;
#else
    /** The X11 atom for the format. */
    Atom        uAtom;
    /** The X11 atom name if uAtom must be termined dynamically. */
    const char *pszAtomName;
    /** @todo X11 */
#endif

    /** Description. */
    const char *pszDesc;
    /** CLIPUTILFORMAT_F_XXX. */
    uint32_t    fFlags;
} CLIPUTILFORMAT;
/** Pointer to a clipobard format descriptor. */
typedef CLIPUTILFORMAT const *PCCLIPUTILFORMAT;

/** Convert to/from UTF-8.  */
#define CLIPUTILFORMAT_F_CONVERT_UTF8       RT_BIT_32(0)
/** Ad hoc entry.  */
#define CLIPUTILFORMAT_F_AD_HOC             RT_BIT_32(1)


#ifdef MULTI_TARGET_CLIPBOARD
/**
 * Clipboard target descriptor.
 */
typedef struct CLIPUTILTARGET
{
    /** Target name.   */
    const char *pszName;
    /** The X11 atom for the target. */
    Atom        uAtom;
    /** The X11 atom name if uAtom must be termined dynamically. */
    const char *pszAtomName;
    /** Description. */
    const char *pszDesc;
} CLIPUTILTARGET;
/** Pointer to clipboard target descriptor. */
typedef CLIPUTILTARGET const *PCCLIPUTILTARGET;
#endif /* MULTI_TARGET_CLIPBOARD */


#ifdef RT_OS_OS2
/** Header for Odin32 specific clipboard entries.
 * (Used to get the correct size of the data.)
 */
typedef struct _Odin32ClipboardHeader
{
    /** Magic (CLIPHEADER_MAGIC) */
    char        achMagic[8];
    /** Size of the following data.
     * (The interpretation depends on the type.) */
    unsigned    cbData;
    /** Odin32 format number. */
    unsigned    uFormat;
} CLIPHEADER, *PCLIPHEADER;

#define CLIPHEADER_MAGIC "Odin\1\0\1"
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Command line parameters */
static const RTGETOPTDEF g_aCmdOptions[] =
{
    { "--list",                     'l',                            RTGETOPT_REQ_NOTHING },
    { "--get",                      'g',                            RTGETOPT_REQ_STRING  },
    { "--get-file",                 'G',                            RTGETOPT_REQ_STRING  },
    { "--put",                      'p',                            RTGETOPT_REQ_STRING  },
    { "--put-file",                 'P',                            RTGETOPT_REQ_STRING  },
    { "--check",                    'c',                            RTGETOPT_REQ_STRING  },
    { "--check-file",               'C',                            RTGETOPT_REQ_STRING  },
    { "--check-not",                'n',                            RTGETOPT_REQ_STRING  },
    { "--zap",                      'z',                            RTGETOPT_REQ_NOTHING },
#ifdef MULTI_TARGET_CLIPBOARD
    { "--target",                   't',                            RTGETOPT_REQ_STRING  },
#endif
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
    { "--close",                    'k',                            RTGETOPT_REQ_NOTHING },
#endif
    { "--wait",                     'w',                            RTGETOPT_REQ_UINT32 },
    { "--quiet",                    'q',                            RTGETOPT_REQ_NOTHING },
    { "--verbose",                  'v',                            RTGETOPT_REQ_NOTHING },
    { "--version",                  'V',                            RTGETOPT_REQ_NOTHING },
    { "--help",                     'h',                            RTGETOPT_REQ_NOTHING }, /* for Usage() */
};

/** Format descriptors. */
static CLIPUTILFORMAT g_aFormats[] =
{
#if defined(RT_OS_WINDOWS)
    { "text/ansi",                  CF_TEXT, NULL,              "ANSI text", 0 },
    { "text/utf-16",                CF_UNICODETEXT, NULL,       "UTF-16 text", 0 },
    { "text/utf-8",                 CF_UNICODETEXT, NULL,       "UTF-8 text", CLIPUTILFORMAT_F_CONVERT_UTF8 },
    /* https://docs.microsoft.com/en-us/windows/desktop/dataxchg/html-clipboard-format */
    { "text/html",                  0, L"HTML Format",          "HTML text", 0 },
    { "bitmap",                     CF_DIB, NULL,               "Bitmap (DIB)", 0 },
    { "bitmap/v5",                  CF_DIBV5, NULL,             "Bitmap version 5 (DIBv5)", 0 },
#elif defined(RT_OS_OS2)
    { "text/ascii",                 CF_TEXT, NULL,              "ASCII text", 0 },
    { "text/utf-8",                 CF_TEXT, NULL,              "UTF-8 text", CLIPUTILFORMAT_F_CONVERT_UTF8 },
    { "text/utf-16",                0, "Odin32 UnicodeText",    "UTF-16 text", 0},
#elif defined(RT_OS_DARWIN)
    { "text/utf-8",                 kUTTypeUTF8PlainText,       "UTF-8 text", 0 },
    { "text/utf-16",                kUTTypeUTF16PlainText,      "UTF-16 text", 0 },
#else
    /** @todo X11   */
    { "text/utf-8",                 None, "UTF8_STRING",       "UTF-8 text", 0 },
#endif
};

#ifdef MULTI_TARGET_CLIPBOARD
/** Target descriptors. */
static CLIPUTILTARGET g_aTargets[] =
{
    { "clipboard", 0, "CLIPBOARD",     "XA_CLIPBOARD: The clipboard (default)" },
    { "primary",   XA_PRIMARY,   NULL, "XA_PRIMARY:   Primary selected text (middle mouse button)" },
    { "secondary", XA_SECONDARY, NULL, "XA_SECONDARY: Secondary selected text (with ctrl)" },
};

/** The current clipboard target. */
static CLIPUTILTARGET *g_pTarget = &g_aTargets[0];
#endif /* MULTI_TARGET_CLIPBOARD */

/** The -v/-q state. */
static unsigned g_uVerbosity = 1;

#ifdef RT_OS_DARWIN

#elif defined(RT_OS_OS2)
/** Anchorblock handle. */
static HAB      g_hOs2Ab = NULLHANDLE;
/** The message queue handle.   */
static HMQ      g_hOs2MsgQueue = NULLHANDLE;
/** Windows that becomes clipboard owner when setting data. */
static HWND     g_hOs2Wnd = NULLHANDLE;
/** Set if we've opened the clipboard. */
static bool     g_fOs2OpenedClipboard = false;
/** Set if we're the clipboard owner. */
static bool     g_fOs2ClipboardOwner = false;
/** Set when we receive a WM_TIMER message during DoWait(). */
static bool volatile g_fOs2TimerTicked = false;

#elif defined(RT_OS_WINDOWS)
/** Set if we've opened the clipboard. */
static bool     g_fWinOpenedClipboard = false;
/** Set when we receive a WM_TIMER message during DoWait(). */
static bool volatile g_fWinTimerTicked = false;
/** Window that becomes clipboard owner when setting data. */
static HWND     g_hWinWnd = NULL;

#else
/** Number of errors (incremented by error handle callback). */
static uint32_t volatile g_cX11Errors;
/** The X11 display. */
static Display *g_pX11Display = NULL;
/** The X11 dummy window.   */
static Window   g_hX11Window = 0;
/** TARGETS */
static Atom     g_uX11AtomTargets;
/** MULTIPLE */
static Atom     g_uX11AtomMultiple;

#endif


/**
 * Gets a format descriptor, complaining if invalid format.
 *
 * @returns Pointer to the descriptor if found, NULL + msg if not.
 * @param   pszFormat           The format to get.
 */
static PCCLIPUTILFORMAT GetFormatDesc(const char *pszFormat)
{
    for (size_t i = 0; i < RT_ELEMENTS(g_aFormats); i++)
        if (strcmp(pszFormat, g_aFormats[i].pszName) == 0)
        {
#if defined(RT_OS_DARWIN)
            /** @todo   */

#elif defined(RT_OS_OS2)
            if (g_aFormats[i].pszFormat && g_aFormats[i].fFormat == 0)
            {
                g_aFormats[i].fFormat = WinAddAtom(WinQuerySystemAtomTable(), g_aFormats[i].pszFormat);
                if (g_aFormats[i].fFormat == 0)
                    RTMsgError("WinAddAtom(,%s) failed: %#x", g_aFormats[i].pszFormat, WinGetLastError(g_hOs2Ab));
            }

#elif defined(RT_OS_WINDOWS)
            if (g_aFormats[i].pwszFormat && g_aFormats[i].fFormat == 0)
            {
                g_aFormats[i].fFormat = RegisterClipboardFormatW(g_aFormats[i].pwszFormat);
                if (g_aFormats[i].fFormat == 0)
                    RTMsgError("RegisterClipboardFormatW(%ls) failed: %u (%#x)",
                               g_aFormats[i].pwszFormat, GetLastError(), GetLastError());
            }
#elif defined(CU_X11)
            if (g_aFormats[i].pszAtomName && g_aFormats[i].uAtom == 0)
                g_aFormats[i].uAtom = XInternAtom(g_pX11Display, g_aFormats[i].pszAtomName, False);
#endif
            return &g_aFormats[i];
        }

    /*
     * Try register the format.
     */
    static CLIPUTILFORMAT AdHoc;
    AdHoc.pszName     = pszFormat;
    AdHoc.pszDesc     = pszFormat;
    AdHoc.fFlags      = CLIPUTILFORMAT_F_AD_HOC;
#ifdef RT_OS_DARWIN
/** @todo   */

#elif defined(RT_OS_OS2)
    AdHoc.pszFormat   = pszFormat;
    AdHoc.fFormat     = WinAddAtom(WinQuerySystemAtomTable(), pszFormat);
    if (AdHoc.fFormat == 0)
    {
        RTMsgError("Invalid format '%s' (%#x)", pszFormat, WinGetLastError(g_hOs2Ab));
        return NULL;
    }

#elif defined(RT_OS_WINDOWS)
    AdHoc.pwszFormat  = NULL;
    AdHoc.fFormat     = RegisterClipboardFormatA(pszFormat);
    if (AdHoc.fFormat == 0)
    {
        RTMsgError("RegisterClipboardFormatA(%s) failed: %u (%#x)", pszFormat, GetLastError(), GetLastError());
        return NULL;
    }

#else
    AdHoc.pszAtomName = pszFormat;
    AdHoc.uAtom       = XInternAtom(g_pX11Display, pszFormat, False);
    if (AdHoc.uAtom == None)
    {
        RTMsgError("Invalid format '%s' or out of memory for X11 atoms", pszFormat);
        return NULL;
    }

#endif
    return &AdHoc;
}


#ifdef RT_OS_DARWIN

/** @todo   */


#elif defined(RT_OS_OS2)

/**
 * The window procedure for the object window.
 *
 * @returns Message result.
 *
 * @param   hwnd    The window handle.
 * @param   msg     The message.
 * @param   mp1     Message parameter 1.
 * @param   mp2     Message parameter 2.
 */
static MRESULT EXPENTRY CuOs2WinProc(HWND hwnd, ULONG msg, MPARAM mp1, MPARAM mp2)
{
    if (g_uVerbosity > 2)
        RTMsgInfo("CuOs2WinProc: hwnd=%#lx msg=%#lx mp1=%#lx mp2=%#lx\n", hwnd, msg, mp1, mp2);

    switch (msg)
    {
        case WM_CREATE:
            return NULL; /* FALSE(/NULL) == Continue*/
        case WM_DESTROY:
            break;

        /*
         * Clipboard viewer message - the content has been changed.
         * This is sent *after* releasing the clipboard sem
         * and during the WinSetClipbrdViewer call.
         */
        case WM_DRAWCLIPBOARD:
            break;

        /*
         * Clipboard owner message - the content was replaced.
         * This is sent by someone with an open clipboard, so don't try open it now.
         */
        case WM_DESTROYCLIPBOARD:
            break;

        /*
         * Clipboard owner message - somebody is requesting us to render a format.
         * This is called by someone which owns the clipboard, but that's fine.
         */
        case WM_RENDERFMT:
            break;

        /*
         * Clipboard owner message - we're about to quit and should render all formats.
         */
        case WM_RENDERALLFMTS:
            break;

        /*
         * Clipboard owner messages dealing with owner drawn content.
         * We shouldn't be seeing any of these.
         */
        case WM_PAINTCLIPBOARD:
        case WM_SIZECLIPBOARD:
        case WM_HSCROLLCLIPBOARD:
        case WM_VSCROLLCLIPBOARD:
            AssertMsgFailed(("msg=%lx (%ld)\n", msg, msg));
            break;

        /*
         * We shouldn't be seeing any other messages according to the docs.
         * But for whatever reason, PM sends us a WM_ADJUSTWINDOWPOS message
         * during WinCreateWindow. So, ignore that and assert on anything else.
         */
        default:
            AssertMsgFailed(("msg=%lx (%ld)\n", msg, msg));
        case WM_ADJUSTWINDOWPOS:
            break;

        /*
         * We use this window fielding WM_TIMER during DoWait.
         */
        case WM_TIMER:
            if (SHORT1FROMMP(mp1) == 1)
                g_fOs2TimerTicked = true;
            break;
    }
    return NULL;
}


/**
 * Initialize the OS/2 bits.
 */
static RTEXITCODE CuOs2Init(void)
{
    g_hOs2Ab = WinInitialize(0);
    if (g_hOs2Ab == NULLHANDLE)
        return RTMsgErrorExitFailure("WinInitialize failed!");

    g_hOs2MsgQueue = WinCreateMsgQueue(g_hOs2Ab, 10);
    if (g_hOs2MsgQueue == NULLHANDLE)
        return RTMsgErrorExitFailure("WinCreateMsgQueue failed: %#x", WinGetLastError(g_hOs2Ab));

    static char s_szClass[] = "VBox-ClipUtilClipboardClass";
    if (!WinRegisterClass(g_hOs2Wnd, (PCSZ)s_szClass, CuOs2WinProc, 0, 0))
        return RTMsgErrorExitFailure("WinRegisterClass failed: %#x", WinGetLastError(g_hOs2Ab));

    g_hOs2Wnd = WinCreateWindow(HWND_OBJECT,                             /* hwndParent */
                                (PCSZ)s_szClass,                         /* pszClass */
                                (PCSZ)"VirtualBox Clipboard Utility",    /* pszName */
                                0,                                       /* flStyle */
                                0, 0, 0, 0,                              /* x, y, cx, cy */
                                NULLHANDLE,                              /* hwndOwner */
                                HWND_BOTTOM,                             /* hwndInsertBehind */
                                42,                                      /* id */
                                NULL,                                    /* pCtlData */
                                NULL);                                   /* pPresParams */
    if (g_hOs2Wnd == NULLHANDLE)
        return RTMsgErrorExitFailure("WinCreateWindow failed: %#x", WinGetLastError(g_hOs2Ab));

    return RTEXITCODE_SUCCESS;
}


/**
 * Terminates the OS/2 bits.
 */
static RTEXITCODE CuOs2Term(void)
{
    if (g_fOs2OpenedClipboard)
    {
        if (!WinCloseClipbrd(g_hOs2Ab))
            return RTMsgErrorExitFailure("WinCloseClipbrd failed: %#x", WinGetLastError(g_hOs2Ab));
        g_fOs2OpenedClipboard = false;
    }

    WinDestroyWindow(g_hOs2Wnd);
    g_hOs2Wnd = NULLHANDLE;

    WinDestroyMsgQueue(g_hOs2MsgQueue);
    g_hOs2MsgQueue = NULLHANDLE;

    WinTerminate(g_hOs2Ab);
    g_hOs2Ab = NULLHANDLE;

    return RTEXITCODE_SUCCESS;
}


/**
 * Opens the OS/2 clipboard.
 */
static RTEXITCODE CuOs2OpenClipboardIfNecessary(void)
{
    if (g_fOs2OpenedClipboard)
        return RTEXITCODE_SUCCESS;
    if (WinOpenClipbrd(g_hOs2Ab))
    {
        if (g_uVerbosity > 0)
            RTMsgInfo("Opened the clipboard\n");
        g_fOs2OpenedClipboard = true;
        return RTEXITCODE_SUCCESS;
    }
    return RTMsgErrorExitFailure("WinOpenClipbrd failed: %#x", WinGetLastError(g_hOs2Ab));
}


#elif defined(RT_OS_WINDOWS)

/**
 * Window procedure for the clipboard owner window on Windows.
 */
static LRESULT CALLBACK CuWinWndProc(HWND hWnd, UINT idMsg, WPARAM wParam, LPARAM lParam) RT_NOTHROW_DEF
{
    if (g_uVerbosity > 2)
        RTMsgInfo("CuWinWndProc: hWnd=%p idMsg=%#05x wParam=%#zx lParam=%#zx\n", hWnd, idMsg, wParam, lParam);

    switch (idMsg)
    {
        case WM_TIMER:
            if (wParam == 1)
                g_fWinTimerTicked = true;
            break;
    }
    return DefWindowProc(hWnd, idMsg, wParam, lParam);
}


/**
 * Initialize the Windows bits.
 */
static RTEXITCODE CuWinInit(void)
{
    /* Register the window class: */
    static wchar_t s_wszClass[] = L"VBox-ClipUtilClipboardClass";
    WNDCLASSW WndCls = {0};
    WndCls.style            = CS_NOCLOSE;
    WndCls.lpfnWndProc      = CuWinWndProc;
    WndCls.cbClsExtra       = 0;
    WndCls.cbWndExtra       = 0;
    WndCls.hInstance        = (HINSTANCE)GetModuleHandle(NULL);
    WndCls.hIcon            = NULL;
    WndCls.hCursor          = NULL;
    WndCls.hbrBackground    = (HBRUSH)(COLOR_BACKGROUND + 1);
    WndCls.lpszMenuName     = NULL;
    WndCls.lpszClassName    = s_wszClass;

    ATOM uAtomWndClass      = RegisterClassW(&WndCls);
    if (!uAtomWndClass)
        return RTMsgErrorExitFailure("RegisterClassW failed: %u (%#x)", GetLastError(), GetLastError());

    /* Create the clipboard owner window: */
    g_hWinWnd = CreateWindowExW(WS_EX_TRANSPARENT,                      /* fExStyle */
                                s_wszClass,                             /* pwszClass */
                                L"VirtualBox Clipboard Utility",        /* pwszName */
                                0,                                      /* fStyle */
                                0, 0, 0, 0,                             /* x, y, cx, cy */
                                HWND_MESSAGE,                           /* hWndParent */
                                NULL,                                   /* hMenu */
                                (HINSTANCE)GetModuleHandle(NULL),       /* hinstance */
                                NULL);                                  /* pParam */
    if (g_hWinWnd == NULL)
        return RTMsgErrorExitFailure("CreateWindowExW failed: %u (%#x)", GetLastError(), GetLastError());

    return RTEXITCODE_SUCCESS;
}

/**
 * Terminates the Windows bits.
 */
static RTEXITCODE CuWinTerm(void)
{
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    if (g_fWinOpenedClipboard)
    {
        if (CloseClipboard())
            g_fWinOpenedClipboard = false;
        else
            rcExit = RTMsgErrorExitFailure("CloseClipboard failed: %u (%#x)", GetLastError(), GetLastError());
    }

    if (g_hWinWnd != NULL)
    {
        if (!DestroyWindow(g_hWinWnd))
            rcExit = RTMsgErrorExitFailure("DestroyWindow failed: %u (%#x)", GetLastError(), GetLastError());
        g_hWinWnd = NULL;
    }

    return rcExit;
}


/**
 * Opens the window clipboard.
 */
static RTEXITCODE WinOpenClipboardIfNecessary(void)
{
    if (g_fWinOpenedClipboard)
        return RTEXITCODE_SUCCESS;
    if (OpenClipboard(g_hWinWnd))
    {
        if (g_uVerbosity > 0)
            RTMsgInfo("Opened the clipboard\n");
        g_fWinOpenedClipboard = true;
        return RTEXITCODE_SUCCESS;
    }
    return RTMsgErrorExitFailure("OpenClipboard failed: %u (%#x)", GetLastError(), GetLastError());
}


#else /* X11: */

/**
 * Error handler callback.
 */
static int CuX11ErrorCallback(Display *pX11Display, XErrorEvent *pErrEvt)
{
    g_cX11Errors++;
    char szErr[2048];
    XGetErrorText(pX11Display, pErrEvt->error_code, szErr, sizeof(szErr));
    RTMsgError("An X Window protocol error occurred: %s\n"
               "  Request code: %u\n"
               "  Minor code:   %u\n"
               "  Serial number of the failed request: %u\n",
               szErr, pErrEvt->request_code, pErrEvt->minor_code, pErrEvt->serial);
    return 0;
}


/**
 * Initialize the X11 bits.
 */
static RTEXITCODE CuX11Init(void)
{
    /*
     * Open the X11 display and create a little dummy window.
     */
    XSetErrorHandler(CuX11ErrorCallback);
    g_pX11Display = XOpenDisplay(NULL);
    if (!g_pX11Display)
        return RTMsgErrorExitFailure("XOpenDisplay failed");

    int const iDefaultScreen = DefaultScreen(g_pX11Display);
    g_hX11Window = XCreateSimpleWindow(g_pX11Display,
                                       RootWindow(g_pX11Display, iDefaultScreen),
                                       0 /*x*/, 0 /*y*/,
                                       1 /*cx*/, 1 /*cy*/,
                                       0 /*cPxlBorder*/,
                                       BlackPixel(g_pX11Display, iDefaultScreen) /*Border*/,
                                       WhitePixel(g_pX11Display, iDefaultScreen) /*Background*/);

    /*
     * Resolve any unknown atom values we might need later.
     */
    for (size_t i = 0; i < RT_ELEMENTS(g_aTargets); i++)
        if (g_aTargets[i].pszAtomName)
        {
            g_aTargets[i].uAtom = XInternAtom(g_pX11Display, g_aTargets[i].pszAtomName, False);
            if (g_uVerbosity > 2)
                RTPrintf("target %s atom=%#x\n", g_aTargets[i].pszName, g_aTargets[i].uAtom);
        }

    g_uX11AtomTargets = XInternAtom(g_pX11Display, "TARGETS", False);
    g_uX11AtomMultiple = XInternAtom(g_pX11Display, "MULTIPLE", False);

    return RTEXITCODE_SUCCESS;
}

#endif /* X11 */


#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
/**
 * Closes the clipboard if open.
 */
static RTEXITCODE CuCloseClipboard(void)
{
# if defined(RT_OS_OS2)
    if (g_fOs2OpenedClipboard)
    {
        if (!WinCloseClipbrd(g_hOs2Ab))
            return RTMsgErrorExitFailure("WinCloseClipbrd failed: %#x", WinGetLastError(g_hOs2Ab));
        g_fOs2OpenedClipboard = false;
        if (g_uVerbosity > 0)
            RTMsgInfo("Closed the clipboard.\n");
    }
# else
    if (g_fWinOpenedClipboard)
    {
        if (!CloseClipboard())
            return RTMsgErrorExitFailure("CloseClipboard failed: %u (%#x)", GetLastError(), GetLastError());
        g_fWinOpenedClipboard = false;
        if (g_uVerbosity > 0)
            RTMsgInfo("Closed the clipboard.\n");
    }
# endif
    else if (g_uVerbosity > 0)
        RTMsgInfo("No need to close clipboard, not opened.\n");

    return RTEXITCODE_SUCCESS;
}
#endif /* RT_OS_OS2 || RT_OS_WINDOWS */


/**
 * Lists the clipboard format.
 */
static RTEXITCODE ListClipboardContent(void)
{
#if defined(RT_OS_OS2)
    RTEXITCODE rcExit = CuOs2OpenClipboardIfNecessary();
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        HATOMTBL const hAtomTbl  = WinQuerySystemAtomTable();
        uint32_t       idx       = 0;
        ULONG          fFormat   = 0;
        while ((fFormat = WinEnumClipbrdFmts(g_hOs2Ab)) != 0)
        {
            char szName[256] = {0};
            ULONG cchRet = WinQueryAtomName(hAtomTbl, fFormat, szName, sizeof(szName));
            if (cchRet != 0)
                RTPrintf("#%02u: %#06x - %s\n", idx, fFormat, szName);
            else
            {
                const char *pszName = NULL;
                switch (fFormat)
                {
                    case CF_TEXT: pszName = "CF_TEXT"; break;
                    case CF_BITMAP: pszName = "CF_BITMAP"; break;
                    case CF_DSPTEXT: pszName = "CF_DSPTEXT"; break;
                    case CF_DSPBITMAP: pszName = "CF_DSPBITMAP"; break;
                    case CF_METAFILE: pszName = "CF_METAFILE"; break;
                    case CF_DSPMETAFILE: pszName = "CF_DSPMETAFILE"; break;
                    case CF_PALETTE: pszName = "CF_PALETTE"; break;
                    default:
                        break;
                }
                if (pszName)
                    RTPrintf("#%02u: %#06x - %s\n", idx, fFormat, pszName);
                else
                    RTPrintf("#%02u: %#06x\n", idx, fFormat);
            }

            idx++;
        }
    }

    return rcExit;

#elif defined(RT_OS_WINDOWS)
    RTEXITCODE rcExit = WinOpenClipboardIfNecessary();
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        SetLastError(0);
        uint32_t idx     = 0;
        UINT     fFormat = 0;
        while ((fFormat = EnumClipboardFormats(fFormat)) != 0)
        {
            WCHAR wszName[256];
            int   cchName = GetClipboardFormatNameW(fFormat, wszName, RT_ELEMENTS(wszName));
            if (cchName > 0)
                RTPrintf("#%02u: %#06x - %ls\n", idx, fFormat, wszName);
            else
            {
                const char *pszName = NULL;
                switch (fFormat)
                {
                    case CF_TEXT: pszName = "CF_TEXT"; break;
                    case CF_BITMAP: pszName = "CF_BITMAP"; break;
                    case CF_METAFILEPICT: pszName = "CF_METAFILEPICT"; break;
                    case CF_SYLK: pszName = "CF_SYLK"; break;
                    case CF_DIF: pszName = "CF_DIF"; break;
                    case CF_TIFF: pszName = "CF_TIFF"; break;
                    case CF_OEMTEXT: pszName = "CF_OEMTEXT"; break;
                    case CF_DIB: pszName = "CF_DIB"; break;
                    case CF_PALETTE: pszName = "CF_PALETTE"; break;
                    case CF_PENDATA: pszName = "CF_PENDATA"; break;
                    case CF_RIFF: pszName = "CF_RIFF"; break;
                    case CF_WAVE: pszName = "CF_WAVE"; break;
                    case CF_UNICODETEXT: pszName = "CF_UNICODETEXT"; break;
                    case CF_ENHMETAFILE: pszName = "CF_ENHMETAFILE"; break;
                    case CF_HDROP: pszName = "CF_HDROP"; break;
                    case CF_LOCALE: pszName = "CF_LOCALE"; break;
                    case CF_DIBV5: pszName = "CF_DIBV5"; break;
                    default:
                        break;
                }
                if (pszName)
                    RTPrintf("#%02u: %#06x - %s\n", idx, fFormat, pszName);
                else
                    RTPrintf("#%02u: %#06x\n", idx, fFormat);
            }

            idx++;
        }
        if (idx == 0)
            RTPrintf("Empty\n");
    }
    return rcExit;

#elif defined(CU_X11)
    /* Request the TARGETS property: */
    Atom uAtomDst = g_uX11AtomTargets;
    int rc = XConvertSelection(g_pX11Display, g_pTarget->uAtom, g_uX11AtomTargets, uAtomDst, g_hX11Window, CurrentTime);
    if (g_uVerbosity > 1)
        RTPrintf("XConvertSelection -> %d\n", rc);

    /* Wait for the reply: */
    for (;;)
    {
        XEvent Evt = {0};
        rc = XNextEvent(g_pX11Display, &Evt);
        if (Evt.type == SelectionNotify)
        {
            if (g_uVerbosity > 1)
                RTPrintf("XNextEvent -> %d; type=SelectionNotify\n", rc);
            if (Evt.xselection.selection == g_pTarget->uAtom)
            {
                if (Evt.xselection.property == None)
                    return RTMsgErrorExitFailure("XConvertSelection(,%s,TARGETS,) failed", g_pTarget->pszName);

                /* Get the TARGETS property data: */
                Atom            uAtomRetType = 0;
                int             iActualFmt   = 0;
                unsigned long   cbLeftToRead = 0;
                unsigned long   cItems       = 0;
                unsigned char  *pbData       = NULL;
                rc = XGetWindowProperty(g_pX11Display, g_hX11Window, uAtomDst,
                                        0 /*offset*/, sizeof(Atom) * 4096 /* should be enough */, True /*fDelete*/, XA_ATOM,
                                        &uAtomRetType, &iActualFmt, &cItems, &cbLeftToRead, &pbData);
                if (g_uVerbosity > 1)
                    RTPrintf("XConvertSelection -> %d; uAtomRetType=%u iActualFmt=%d cItems=%lu cbLeftToRead=%lu pbData=%p\n",
                             rc, uAtomRetType, iActualFmt, cItems, cbLeftToRead, pbData);
                if (pbData && cItems > 0)
                {
                    /* Display the TARGETS: */
                    Atom const *paTargets = (Atom const *)pbData;
                    for (unsigned long i = 0; i < cItems; i++)
                    {
                        const char *pszName = XGetAtomName(g_pX11Display, paTargets[i]);
                        if (pszName)
                            RTPrintf("#%02u: %#06x - %s\n", i, paTargets[i], pszName);
                        else
                            RTPrintf("#%02u: %#06x\n", i, paTargets[i]);
                    }
                }
                else
                    RTMsgInfo("Empty");
                if (pbData)
                    XFree(pbData);
                return RTEXITCODE_SUCCESS;
            }
        }
        else if (g_uVerbosity > 1)
            RTPrintf("XNextEvent -> %d; type=%d\n", rc, Evt.type);
    }

#else
    return RTMsgErrorExitFailure("ListClipboardContent is not implemented");
#endif
}


/**
 * Reads the given clipboard format and stores it in on the heap.
 *
 * @returns Success indicator.
 * @param   pFmtDesc    The format to get.
 * @param   ppvData     Where to return the pointer to the data. Free using
 *                      RTMemFree when done.
 * @param   pcbData     Where to return the amount of data returned.
 */
static RTEXITCODE ReadClipboardData(PCCLIPUTILFORMAT pFmtDesc, void **ppvData, size_t *pcbData)
{
    *ppvData = NULL;
    *pcbData = 0;

#if defined(RT_OS_OS2)
    RTEXITCODE rcExit = CuOs2OpenClipboardIfNecessary();
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        ULONG fFmtInfo = 0;
        if (WinQueryClipbrdFmtInfo(g_hOs2Ab, pFmtDesc->fFormat, &fFmtInfo))
        {
            ULONG uData = WinQueryClipbrdData(g_hOs2Ab, pFmtDesc->fFormat);
            if (fFmtInfo & CFI_POINTER)
            {
                PCLIPHEADER pOdinHdr = (PCLIPHEADER)uData;
                if (pFmtDesc->fFormat == CF_TEXT)
                {
                    if (pFmtDesc->fFlags & CLIPUTILFORMAT_F_CONVERT_UTF8)
                    {
                        char *pszUtf8 = NULL;
                        int rc = RTStrCurrentCPToUtf8(&pszUtf8, (const char *)uData);
                        if (RT_SUCCESS(rc))
                        {
                            *pcbData = strlen(pszUtf8) + 1;
                            *ppvData = RTMemDup(pszUtf8, *pcbData);
                            RTStrFree(pszUtf8);
                        }
                        else
                            return RTMsgErrorExitFailure("RTStrCurrentCPToUtf8 failed: %Rrc", rc);
                    }
                    else
                    {
                        *pcbData = strlen((const char *)uData) + 1;
                        *ppvData = RTMemDup((const char *)uData, *pcbData);
                    }
                }
                else if (   strcmp(pFmtDesc->pszFormat, "Odin32 UnicodeText") == 0
                         && memcmp(pOdinHdr->achMagic, CLIPHEADER_MAGIC, sizeof(pOdinHdr->achMagic)) == 0)
                {
                    *pcbData = pOdinHdr->cbData;
                    *ppvData = RTMemDup(pOdinHdr + 1, pOdinHdr->cbData);
                }
                else
                {
                    /* We could use DosQueryMem here to figure out the size of the allocation... */
                    *pcbData = PAGE_SIZE - (uData & PAGE_OFFSET_MASK);
                    *ppvData = RTMemDup((void const *)uData, *pcbData);
                }
            }
            else
            {
                *pcbData = sizeof(uData);
                *ppvData = RTMemDup(&uData, sizeof(uData));
            }
            if (!*ppvData)
                rcExit = RTMsgErrorExitFailure("Out of memory allocating %#zx bytes.", *pcbData);
        }
        else
            rcExit = RTMsgErrorExitFailure("WinQueryClipbrdFmtInfo(,%s,) failed: %#x\n",
                                           pFmtDesc->pszName, WinGetLastError(g_hOs2Ab));
    }
    return rcExit;

#elif defined(RT_OS_WINDOWS)
    RTEXITCODE rcExit = WinOpenClipboardIfNecessary();
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        HANDLE hData = GetClipboardData(pFmtDesc->fFormat);
        if (hData != NULL)
        {
            SIZE_T const cbData = GlobalSize(hData);
            PVOID  const pvData = GlobalLock(hData);
            if (pvData != NULL)
            {
                *pcbData = cbData;
                if (cbData != 0)
                {
                    if (pFmtDesc->fFlags & CLIPUTILFORMAT_F_CONVERT_UTF8)
                    {
                        char  *pszUtf8 = NULL;
                        size_t cchUtf8 = 0;
                        int rc = RTUtf16ToUtf8Ex((PCRTUTF16)pvData, cbData / sizeof(RTUTF16), &pszUtf8, 0, &cchUtf8);
                        if (RT_SUCCESS(rc))
                        {
                            *pcbData = cchUtf8 + 1;
                            *ppvData = RTMemDup(pszUtf8, cchUtf8 + 1);
                            RTStrFree(pszUtf8);
                            if (!*ppvData)
                                rcExit = RTMsgErrorExitFailure("Out of memory allocating %#zx bytes.", cbData);
                        }
                        else
                            rcExit = RTMsgErrorExitFailure("RTUtf16ToUtf8Ex failed: %Rrc", rc);
                    }
                    else
                    {
                        *ppvData = RTMemDup(pvData, cbData);
                        if (!*ppvData)
                            rcExit = RTMsgErrorExitFailure("Out of memory allocating %#zx bytes.", cbData);
                    }
                }
                GlobalUnlock(hData);
            }
            else
                rcExit = RTMsgErrorExitFailure("GetClipboardData(%s) failed: %u (%#x)\n",
                                               pFmtDesc->pszName, GetLastError(), GetLastError());
        }
        else
            rcExit = RTMsgErrorExitFailure("GetClipboardData(%s) failed: %u (%#x)\n",
                                           pFmtDesc->pszName, GetLastError(), GetLastError());
    }
    return rcExit;

#elif defined(CU_X11)

    /* Request the data: */
    Atom const uAtomDst = pFmtDesc->uAtom;
    int rc = XConvertSelection(g_pX11Display, g_pTarget->uAtom, pFmtDesc->uAtom, uAtomDst, g_hX11Window, CurrentTime);
    if (g_uVerbosity > 1)
        RTPrintf("XConvertSelection -> %d\n", rc);

    /* Wait for the reply: */
    for (;;)
    {
        XEvent Evt = {0};
        rc = XNextEvent(g_pX11Display, &Evt);
        if (Evt.type == SelectionNotify)
        {
            if (g_uVerbosity > 1)
                RTPrintf("XNextEvent -> %d; type=SelectionNotify\n", rc);
            if (Evt.xselection.selection == g_pTarget->uAtom)
            {
                if (Evt.xselection.property == None)
                    return RTMsgErrorExitFailure("XConvertSelection(,%s,%s,) failed", g_pTarget->pszName, pFmtDesc->pszName);

                /*
                 * Retrieve the data.
                 */
                Atom            uAtomRetType   = 0;
                int             cBitsActualFmt = 0;
                unsigned long   cbLeftToRead   = 0;
                unsigned long   cItems         = 0;
                unsigned char  *pbData         = NULL;
                rc = XGetWindowProperty(g_pX11Display, g_hX11Window, uAtomDst,
                                        0 /*offset*/, _64M, False/*fDelete*/, AnyPropertyType,
                                        &uAtomRetType, &cBitsActualFmt, &cItems, &cbLeftToRead, &pbData);
                if (g_uVerbosity > 1)
                    RTPrintf("XConvertSelection -> %d; uAtomRetType=%u cBitsActualFmt=%d cItems=%lu cbLeftToRead=%lu pbData=%p\n",
                             rc, uAtomRetType, cBitsActualFmt, cItems, cbLeftToRead, pbData);
                RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
                if (pbData && cItems > 0)
                {
                    *pcbData = cItems * (cBitsActualFmt / 8);
                    *ppvData = RTMemDup(pbData, *pcbData);
                    if (!*ppvData)
                        rcExit = RTMsgErrorExitFailure("Out of memory allocating %#zx bytes.", *pcbData);
                }
                if (pbData)
                    XFree(pbData);
                XDeleteProperty(g_pX11Display, g_hX11Window, uAtomDst);
                return rcExit;
            }
        }
        else if (g_uVerbosity > 1)
            RTPrintf("XNextEvent -> %d; type=%d\n", rc, Evt.type);
    }

#else
    RT_NOREF(pFmtDesc);
    return RTMsgErrorExitFailure("ReadClipboardData is not implemented\n");
#endif
}


/**
 * Puts the given data and format on the clipboard.
 *
 * @returns Success indicator.
 * @param   pFmtDesc     The format.
 * @param   pvData       The data.
 * @param   cbData       The amount of data in bytes.
 */
static RTEXITCODE WriteClipboardData(PCCLIPUTILFORMAT pFmtDesc, void const *pvData, size_t cbData)
{
#if defined(RT_OS_OS2)
    RTEXITCODE rcExit = CuOs2OpenClipboardIfNecessary();
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /** @todo do we need to become owner? */

        /* Convert to local code page if needed: */
        char *pszLocale = NULL;
        if (pFmtDesc->fFlags & CLIPUTILFORMAT_F_CONVERT_UTF8)
        {
            int rc = RTStrUtf8ToCurrentCPEx(&pszLocale, (char *)pvData, cbData);
            if (RT_SUCCESS(rc))
            {
                pvData = pszLocale;
                cbData = strlen(pszLocale) + 1;
            }
            else
                return RTMsgErrorExitFailure("RTStrUtf8ToCurrentCPEx failed: %Rrc\n", rc);
        }

        /* Allocate a bunch of shared memory for the object. */
        PVOID  pvShared = NULL;
        APIRET orc = DosAllocSharedMem(&pvShared, NULL, cbData,
                                       OBJ_GIVEABLE | OBJ_GETTABLE | OBJ_TILE | PAG_READ | PAG_WRITE | PAG_COMMIT);
        if (orc == NO_ERROR)
        {
            memcpy(pvShared, pvData, cbData);

            if (WinSetClipbrdData(g_hOs2Ab, (uintptr_t)pvShared, pFmtDesc->fFormat, CFI_POINTER))
            {
                if (g_uVerbosity > 0)
                    RTMsgInfo("Put '%s' on the clipboard: %p LB %zu\n", pFmtDesc->pszName, pvShared, cbData);
                rcExit = RTEXITCODE_SUCCESS;
            }
            else
            {
                rcExit = RTMsgErrorExitFailure("WinSetClipbrdData(,%p LB %#x,%s,) failed: %#x\n",
                                               pvShared, cbData, pFmtDesc->pszName, WinGetLastError(g_hOs2Ab));
                DosFreeMem(pvShared);
            }
        }
        else
            rcExit = RTMsgErrorExitFailure("DosAllocSharedMem(,, %#x,) -> %u", cbData, orc);
        RTStrFree(pszLocale);
    }
    return rcExit;


#elif defined(RT_OS_WINDOWS)
    RTEXITCODE rcExit = WinOpenClipboardIfNecessary();
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /*
         * Do input data conversion.
         */
        PRTUTF16 pwszFree = NULL;
        if (pFmtDesc->fFlags & CLIPUTILFORMAT_F_CONVERT_UTF8)
        {
            size_t cwcConv = 0;
            int rc = RTStrToUtf16Ex((char const *)pvData, cbData, &pwszFree, 0, &cwcConv);
            if (RT_SUCCESS(rc))
            {
                pvData = pwszFree;
                cbData = cwcConv * sizeof(RTUTF16);
            }
            else
                return RTMsgErrorExitFailure("RTStrToTUtf16Ex failed: %Rrc\n", rc);
        }

        /*
         * Text formats generally include the zero terminator.
         */
        uint32_t cbZeroPadding = 0;
        if (pFmtDesc->fFormat == CF_UNICODETEXT)
            cbZeroPadding = sizeof(WCHAR);
        else if (pFmtDesc->fFormat == CF_TEXT)
            cbZeroPadding = sizeof(char);

        HANDLE hDstData = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, cbData + cbZeroPadding);
        if (hDstData)
        {
            if (cbData)
            {
                PVOID pvDstData = GlobalLock(hDstData);
                if (pvDstData)
                    memcpy(pvDstData, pvData, cbData);
                else
                    rcExit = RTMsgErrorExitFailure("GlobalLock failed: %u (%#x)\n", GetLastError(), GetLastError());
            }
            if (rcExit == RTEXITCODE_SUCCESS)
            {
                if (SetClipboardData(pFmtDesc->fFormat, hDstData))
                {
                    if (g_uVerbosity > 0)
                        RTMsgInfo("Put '%s' on the clipboard: %p LB %zu\n", pFmtDesc->pszName, hDstData, cbData + cbZeroPadding);
                }
                else
                {
                    rcExit = RTMsgErrorExitFailure("SetClipboardData(%s) failed: %u (%#x)\n",
                                                   pFmtDesc->pszName, GetLastError(), GetLastError());
                    GlobalFree(hDstData);
                }
            }
            else
                GlobalFree(hDstData);
        }
        else
            rcExit = RTMsgErrorExitFailure("GlobalAlloc(,%#zx) failed: %u (%#x)\n",
                                           cbData + cbZeroPadding, GetLastError(), GetLastError());
    }
    return rcExit;

#else
    RT_NOREF(pFmtDesc, pvData, cbData);
    return RTMsgErrorExitFailure("WriteClipboardData is not implemented\n");
#endif
}


/**
 * Check if the given data + format matches what's actually on the clipboard.
 *
 * @returns Success indicator.
 * @param   pFmtDesc    The format to compare.
 * @param   pvExpect    The expected clipboard data.
 * @param   cbExpect    The size of the expected clipboard data.
 */
static RTEXITCODE CompareDataWithClipboard(PCCLIPUTILFORMAT pFmtDesc, void const *pvExpect, size_t cbExpect)
{
    void      *pvData = NULL;
    size_t     cbData = 0;
    RTEXITCODE rcExit = ReadClipboardData(pFmtDesc, &pvData, &cbData);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        if (   cbData == cbExpect
            && memcmp(pvData, pvExpect, cbData) == 0)
            rcExit = RTEXITCODE_SUCCESS;
        else
            rcExit = RTMsgErrorExitFailure("Mismatch for '%s' (cbData=%#zx cbExpect=%#zx)\n",
                                           pFmtDesc->pszName, cbData, cbExpect);
        RTMemFree(pvData);
    }
    return rcExit;
}


/**
 * Gets the given clipboard format.
 *
 * @returns Success indicator.
 * @param   pFmtDesc    The format to get.
 * @param   pStrmOut    Where to output the data.
 * @param   fIsStdOut   Set if @a pStrmOut is standard output, clear if not.
 */
static RTEXITCODE ClipboardContentToStdOut(PCCLIPUTILFORMAT pFmtDesc)
{
    void      *pvData = NULL;
    size_t     cbData = 0;
    RTEXITCODE rcExit = ReadClipboardData(pFmtDesc, &pvData, &cbData);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        int rc = RTStrmWrite(g_pStdOut, pvData, cbData);
        RTMemFree(pvData);
        if (RT_FAILURE(rc))
            rcExit = RTMsgErrorExitFailure("Error writing %#zx bytes to standard output: %Rrc", cbData, rc);
    }
    return rcExit;
}


/**
 * Gets the given clipboard format.
 *
 * @returns Success indicator.
 * @param   pFmtDesc    The format to get.
 * @param   pszFilename The output filename.
 */
static RTEXITCODE ClipboardContentToFile(PCCLIPUTILFORMAT pFmtDesc, const char *pszFilename)
{
    void      *pvData = NULL;
    size_t     cbData = 0;
    RTEXITCODE rcExit = ReadClipboardData(pFmtDesc, &pvData, &cbData);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        RTFILE hFile = NIL_RTFILE;
        int rc = RTFileOpen(&hFile, pszFilename,
                            RTFILE_O_WRITE | RTFILE_O_DENY_WRITE | RTFILE_O_CREATE_REPLACE
                            | (0770 << RTFILE_O_CREATE_MODE_SHIFT));
        if (RT_SUCCESS(rc))
        {
            rc = RTFileWrite(hFile, pvData, cbData, NULL);
            int const rc2 = RTFileClose(hFile);
            if (RT_FAILURE(rc) || RT_FAILURE(rc2))
            {
                if (RT_FAILURE_NP(rc))
                    RTMsgError("Writing %#z bytes to '%s' failed: %Rrc", cbData, pszFilename, rc);
                else
                    RTMsgError("Closing '%s' failed: %Rrc", pszFilename, rc2);
                RTMsgInfo("Deleting '%s'.", pszFilename);
                RTFileDelete(pszFilename);
                rcExit = RTEXITCODE_FAILURE;
            }
        }
        else
            rcExit = RTMsgErrorExitFailure("Failed to open '%s' for writing: %Rrc", pszFilename, rc);
        RTMemFree(pvData);
    }
    return rcExit;
}


/**
 * Puts the given format + data onto the clipboard.
 *
 * @returns Success indicator.
 * @param   pFmtDesc    The format to put.
 * @param   pszData     The string data.
 */
static RTEXITCODE PutStringOnClipboard(PCCLIPUTILFORMAT pFmtDesc, const char *pszData)
{
    return WriteClipboardData(pFmtDesc, pszData, strlen(pszData));
}


/**
 * Puts a format + file content onto the clipboard.
 *
 * @returns Success indicator.
 * @param   pFmtDesc    The format to put.
 * @param   pszFilename The filename.
 */
static RTEXITCODE PutFileOnClipboard(PCCLIPUTILFORMAT pFmtDesc, const char *pszFilename)
{
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    void      *pvData = NULL;
    size_t     cbData = 0;
    int rc = RTFileReadAll(pszFilename, &pvData, &cbData);
    if (RT_SUCCESS(rc))
    {
        rcExit = WriteClipboardData(pFmtDesc, pvData, cbData);
        RTFileReadAllFree(pvData, cbData);
    }
    else
        rcExit = RTMsgErrorExitFailure("Failed to open and read '%s' into memory: %Rrc", pszFilename, rc);
    return rcExit;
}


/**
 * Checks if the given format + data matches what's on the clipboard.
 *
 * @returns Success indicator.
 * @param   pFmtDesc    The format to check.
 * @param   pszData     The string data.
 */
static RTEXITCODE CheckStringAgainstClipboard(PCCLIPUTILFORMAT pFmtDesc, const char *pszData)
{
    return CompareDataWithClipboard(pFmtDesc, pszData, strlen(pszData));
}


/**
 * Check if the given format + file content matches what's on the clipboard.
 *
 * @returns Success indicator.
 * @param   pFmtDesc    The format to check.
 * @param   pszFilename The filename.
 */
static RTEXITCODE CheckFileAgainstClipboard(PCCLIPUTILFORMAT pFmtDesc, const char *pszFilename)
{
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    void      *pvData = NULL;
    size_t     cbData = 0;
    int rc = RTFileReadAll(pszFilename, &pvData, &cbData);
    if (RT_SUCCESS(rc))
    {
        rcExit = CompareDataWithClipboard(pFmtDesc, pvData, cbData);
        RTFileReadAllFree(pvData, cbData);
    }
    else
        rcExit = RTMsgErrorExitFailure("Failed to open and read '%s' into memory: %Rrc", pszFilename, rc);
    return rcExit;
}


/**
 * Check that the given format is not on the clipboard.
 *
 * @returns Success indicator.
 * @param   pFmtDesc    The format to check.
 */
static RTEXITCODE CheckFormatNotOnClipboard(PCCLIPUTILFORMAT pFmtDesc)
{
#if defined(RT_OS_OS2)
    RTEXITCODE rcExit = CuOs2OpenClipboardIfNecessary();
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        ULONG fFmtInfo = 0;
        if (WinQueryClipbrdFmtInfo(g_hOs2Ab, pFmtDesc->fFormat, &fFmtInfo))
            rcExit = RTMsgErrorExitFailure("Format '%s' is present");
    }
    return rcExit;

#elif defined(RT_OS_WINDOWS)
    RTEXITCODE rcExit = WinOpenClipboardIfNecessary();
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        if (IsClipboardFormatAvailable(pFmtDesc->fFormat))
            rcExit = RTMsgErrorExitFailure("Format '%s' is present");
    }
    return rcExit;

#else
    RT_NOREF(pFmtDesc);
    return RTMsgErrorExitFailure("CheckFormatNotOnClipboard is not implemented");
#endif
}


/**
 * Empties the clipboard.
 *
 * @returns Success indicator.
 */
static RTEXITCODE ZapAllClipboardData(void)
{
#if defined(RT_OS_OS2)
    RTEXITCODE rcExit = CuOs2OpenClipboardIfNecessary();
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        ULONG fFmtInfo = 0;
        if (WinEmptyClipbrd(g_hOs2Ab))
        {
            WinSetClipbrdOwner(g_hOs2Ab, g_hOs2Wnd); /* Probably unnecessary? */
            WinSetClipbrdOwner(g_hOs2Ab, NULLHANDLE);
            g_fOs2ClipboardOwner = false;
        }
        else
            rcExit = RTMsgErrorExitFailure("WinEmptyClipbrd() failed: %#x\n", WinGetLastError(g_hOs2Ab));
    }
    return rcExit;

#elif defined(RT_OS_WINDOWS)
    RTEXITCODE rcExit = WinOpenClipboardIfNecessary();
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        if (!EmptyClipboard())
            rcExit = RTMsgErrorExitFailure("EmptyClipboard() failed: %u (%#x)\n", GetLastError(), GetLastError());
    }
    return rcExit;

#else
    return RTMsgErrorExitFailure("ZapAllClipboardData is not implemented");
#endif
}


/**
 * Waits/delays at least @a cMsWait milliseconds.
 *
 * @returns Success indicator.
 * @param   cMsWait     Minimum wait/delay time in milliseconds.
 */
static RTEXITCODE DoWait(uint32_t cMsWait)
{
    uint64_t const msStart = RTTimeMilliTS();
    if (g_uVerbosity > 1)
        RTMsgInfo("Waiting %u ms...\n", cMsWait);

#if defined(RT_OS_OS2)
    /*
     * Arm a timer which will timeout after the desired period and
     * quit when we've dispatched it.
     */
    g_fOs2TimerTicked = false;
    if (WinStartTimer(g_hOs2Ab, g_hOs2Wnd, 1 /*idEvent*/, cMsWait + 1) != 0)
    {
        QMSG Msg;
        while (WinGetMsg(g_hOs2Ab, &Msg, NULL, 0, 0))
        {
            WinDispatchMsg(g_hOs2Ab, &Msg);
            if (g_fOs2TimerTicked || RTTimeMilliTS() - msStart >= cMsWait)
                break;
        }

        if (!WinStopTimer(g_hOs2Ab, g_hOs2Wnd, 1 /*idEvent*/))
            RTMsgWarning("WinStopTimer failed: %#x", WinGetLastError(g_hOs2Ab));
    }
    else
        return RTMsgErrorExitFailure("WinStartTimer(,,,%u ms) failed: %#x", cMsWait + 1, WinGetLastError(g_hOs2Ab));

#elif defined(RT_OS_WINDOWS)
    /*
     * Arm a timer which will timeout after the desired period and
     * quit when we've dispatched it.
     */
    g_fWinTimerTicked = false;
    if (SetTimer(g_hWinWnd, 1 /*idEvent*/, cMsWait + 1, NULL /*pfnTimerProc*/) != 0)
    {
        MSG Msg;
        while (GetMessageW(&Msg, NULL, 0, 0))
        {
            TranslateMessage(&Msg);
            DispatchMessageW(&Msg);
            if (g_fWinTimerTicked || RTTimeMilliTS() - msStart >= cMsWait)
                break;
        }

        if (!KillTimer(g_hWinWnd, 1 /*idEvent*/))
            RTMsgWarning("KillTimer failed: %u (%#x)", GetLastError(), GetLastError());
    }
    else
        return RTMsgErrorExitFailure("SetTimer(,,%u ms,) failed: %u (%#x)", cMsWait + 1, GetLastError(), GetLastError());

#else
/** @todo X11 needs to run it's message queue too, because if we're offering
 *        things on the "clipboard" we must reply to requests for them.  */
    /*
     * Just a plain simple RTThreadSleep option (will probably not be used in the end):
     */
    for (;;)
    {
        uint64_t cMsElapsed = RTTimeMilliTS() - msStart;
        if (cMsElapsed >= cMsWait)
            break;
        RTThreadSleep(cMsWait - cMsElapsed);
    }
#endif

    if (g_uVerbosity > 2)
        RTMsgInfo("Done waiting after %u ms.\n", RTTimeMilliTS() - msStart);
    return RTEXITCODE_SUCCESS;
}


/**
 * Display the usage to @a pStrm.
 */
static void Usage(PRTSTREAM pStrm)
{
    RTStrmPrintf(pStrm,
                 "usage: %s [--get <fmt> [--get ...]] [--get-file <fmt> <file> [--get-file ...]]\n"
                 "       %s [--zap] [--put <fmt> <content> [--put ...]] [--put-file <fmt> <file> [--put-file ...]] [--wait <ms>]\n"
                 "       %s [--check <fmt> <expected> [--check ...]] [--check-file <fmt> <file> [--check-file ...]]\n"
                 "           [--check-no <fmt> [--check-no ...]]\n"
                 , RTProcShortName(), RTProcShortName(), RTProcShortName());
    RTStrmPrintf(pStrm, "\n");
    RTStrmPrintf(pStrm, "Actions/Options:\n");

    for (unsigned i = 0; i < RT_ELEMENTS(g_aCmdOptions); i++)
    {
        const char *pszHelp;
        switch (g_aCmdOptions[i].iShort)
        {
            case 'l':   pszHelp = "List the clipboard content."; break;
            case 'g':   pszHelp = "Get given clipboard format and writes it to standard output."; break;
            case 'G':   pszHelp = "Get given clipboard format and writes it to the specified file."; break;
            case 'p':   pszHelp = "Puts given format and content on the clipboard."; break;
            case 'P':   pszHelp = "Puts given format and file content on the clipboard."; break;
            case 'c':   pszHelp = "Checks that the given format and content matches the clipboard."; break;
            case 'C':   pszHelp = "Checks that the given format and file content matches the clipboard."; break;
            case 'n':   pszHelp = "Checks that the given format is not on the clipboard."; break;
            case 'z':   pszHelp = "Zaps the clipboard content."; break;
#ifdef MULTI_TARGET_CLIPBOARD
            case 't':   pszHelp = "Selects the target clipboard."; break;
#endif
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
            case 'k':   pszHelp = "Closes the clipboard if open (win,os2)."; break;
#endif
            case 'w':   pszHelp = "Waits a given number of milliseconds before continuing."; break;
            case 'v':   pszHelp = "More verbose execution."; break;
            case 'q':   pszHelp = "Quiet execution."; break;
            case 'h':   pszHelp = "Displays this help and exit"; break;
            case 'V':   pszHelp = "Displays the program revision"; break;

            default:
                pszHelp = "Option undocumented";
                break;
        }
        if ((unsigned)g_aCmdOptions[i].iShort < 127U)
        {
            char szOpt[64];
            RTStrPrintf(szOpt, sizeof(szOpt), "%s, -%c", g_aCmdOptions[i].pszLong, g_aCmdOptions[i].iShort);
            RTStrmPrintf(pStrm, "  %-19s %s\n", szOpt, pszHelp);
        }
        else
            RTStrmPrintf(pStrm, "  %-19s %s\n", g_aCmdOptions[i].pszLong, pszHelp);
    }
    RTStrmPrintf(pStrm,
                 "\n"
                 "Note! Options are processed in the order they are given.\n");

    RTStrmPrintf(pStrm, "\nFormats:\n");
    for (size_t i = 0; i < RT_ELEMENTS(g_aFormats); i++)
        RTStrmPrintf(pStrm, "    %-12s: %s\n", g_aFormats[i].pszName, g_aFormats[i].pszDesc);

#ifdef MULTI_TARGET_CLIPBOARD
    RTStrmPrintf(pStrm, "\nTarget:\n");
    for (size_t i = 0; i < RT_ELEMENTS(g_aTargets); i++)
        RTStrmPrintf(pStrm, "    %-12s: %s\n", g_aTargets[i].pszName, g_aTargets[i].pszDesc);
#endif
}


int main(int argc, char *argv[])
{
    /*
     * Init IPRT.
     */
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Host specific init.
     */
#ifdef RT_OS_DARWIN
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
#elif defined(RT_OS_OS2)
    RTEXITCODE rcExit = CuOs2Init();
#elif defined(RT_OS_WINDOWS)
    RTEXITCODE rcExit = CuWinInit();
#else
    RTEXITCODE rcExit = CuX11Init();
#endif
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    /*
     * Process options (in order).
     */
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, g_aCmdOptions, RT_ELEMENTS(g_aCmdOptions), 1, 0 /* fFlags */);
    while ((rc = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        RTEXITCODE rcExit2 = RTEXITCODE_SUCCESS;
        switch (rc)
        {
#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
            case 'k':
                rcExit2 = CuCloseClipboard();
                break;
#endif

            case 'l':
                rcExit2 = ListClipboardContent();
                break;

            case 'g':
            {
                PCCLIPUTILFORMAT pFmtDesc = GetFormatDesc(ValueUnion.psz);
                if (pFmtDesc)
                    rcExit2 = ClipboardContentToStdOut(pFmtDesc);
                else
                    rcExit2 = RTEXITCODE_FAILURE;
                break;
            }

            case 'G':
            {
                PCCLIPUTILFORMAT pFmtDesc = GetFormatDesc(ValueUnion.psz);
                if (pFmtDesc)
                {
                    rc = RTGetOptFetchValue(&GetState, &ValueUnion, RTGETOPT_REQ_STRING);
                    if (RT_SUCCESS(rc))
                        rcExit2 = ClipboardContentToFile(pFmtDesc, ValueUnion.psz);
                    else
                        return RTMsgErrorExitFailure("No filename given with --get-file");
                }
                else
                    rcExit2 = RTEXITCODE_FAILURE;
                break;
            }

            case 'p':
            {
                PCCLIPUTILFORMAT pFmtDesc = GetFormatDesc(ValueUnion.psz);
                if (pFmtDesc)
                {
                    rc = RTGetOptFetchValue(&GetState, &ValueUnion, RTGETOPT_REQ_STRING);
                    if (RT_SUCCESS(rc))
                        rcExit2 = PutStringOnClipboard(pFmtDesc, ValueUnion.psz);
                    else
                        return RTMsgErrorExitFailure("No data string given with --put");
                }
                else
                    rcExit2 = RTEXITCODE_FAILURE;
                break;
            }

            case 'P':
            {
                PCCLIPUTILFORMAT pFmtDesc = GetFormatDesc(ValueUnion.psz);
                if (pFmtDesc)
                {
                    rc = RTGetOptFetchValue(&GetState, &ValueUnion, RTGETOPT_REQ_STRING);
                    if (RT_SUCCESS(rc))
                        rcExit2 = PutFileOnClipboard(pFmtDesc, ValueUnion.psz);
                    else
                        return RTMsgErrorExitFailure("No filename given with --put-file");
                }
                else
                    rcExit2 = RTEXITCODE_FAILURE;
                break;
            }

            case 'c':
            {
                PCCLIPUTILFORMAT pFmtDesc = GetFormatDesc(ValueUnion.psz);
                if (pFmtDesc)
                {
                    rc = RTGetOptFetchValue(&GetState, &ValueUnion, RTGETOPT_REQ_STRING);
                    if (RT_SUCCESS(rc))
                        rcExit2 = CheckStringAgainstClipboard(pFmtDesc, ValueUnion.psz);
                    else
                        return RTMsgErrorExitFailure("No data string given with --check");
                }
                else
                    rcExit2 = RTEXITCODE_FAILURE;
                break;
            }

            case 'C':
            {
                PCCLIPUTILFORMAT pFmtDesc = GetFormatDesc(ValueUnion.psz);
                if (pFmtDesc)
                {
                    rc = RTGetOptFetchValue(&GetState, &ValueUnion, RTGETOPT_REQ_STRING);
                    if (RT_SUCCESS(rc))
                        rcExit2 = CheckFileAgainstClipboard(pFmtDesc, ValueUnion.psz);
                    else
                        return RTMsgErrorExitFailure("No filename given with --check-file");
                }
                else
                    rcExit2 = RTEXITCODE_FAILURE;
                break;
            }

            case 'n':
            {
                PCCLIPUTILFORMAT pFmtDesc = GetFormatDesc(ValueUnion.psz);
                if (pFmtDesc)
                    rcExit2 = CheckFormatNotOnClipboard(pFmtDesc);
                else
                    rcExit2 = RTEXITCODE_FAILURE;
                break;
            }


            case 'z':
                rcExit2 = ZapAllClipboardData();
                break;

#ifdef MULTI_TARGET_CLIPBOARD
            case 't':
            {
                CLIPUTILTARGET *pNewTarget = NULL;
                for (size_t i = 0; i < RT_ELEMENTS(g_aTargets); i++)
                    if (strcmp(ValueUnion.psz, g_aTargets[i].pszName) == 0)
                    {
                        pNewTarget = &g_aTargets[i];
                        break;
                    }
                if (!pNewTarget)
                    return RTMsgErrorExitFailure("Unknown target '%s'", ValueUnion.psz);
                if (pNewTarget != g_pTarget && g_uVerbosity > 0)
                    RTMsgInfo("Switching from '%s' to '%s'\n", g_pTarget->pszName, pNewTarget->pszName);
                g_pTarget = pNewTarget;
                break;
            }
#endif

            case 'w':
                rcExit2 = DoWait(ValueUnion.u32);
                break;

            case 'q':
                g_uVerbosity = 0;
                break;

            case 'v':
                g_uVerbosity++;
                break;

            case 'h':
                Usage(g_pStdOut);
                return RTEXITCODE_SUCCESS;

            case 'V':
            {
                char szRev[] = "$Revision: 106320 $";
                szRev[RT_ELEMENTS(szRev) - 2] = '\0';
                RTPrintf(RTStrStrip(strchr(szRev, ':') + 1));
                return RTEXITCODE_SUCCESS;
            }

            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }

        if (rcExit2 != RTEXITCODE_SUCCESS && rcExit == RTEXITCODE_SUCCESS)
            rcExit = rcExit2;
    }

    /*
     * Host specific cleanup.
     */
#if defined(RT_OS_OS2)
    RTEXITCODE rcExit2 = CuOs2Term();
#elif defined(RT_OS_WINDOWS)
    RTEXITCODE rcExit2 = CuWinTerm();
#else
    RTEXITCODE rcExit2 = RTEXITCODE_SUCCESS;
#endif
    if (rcExit2 != RTEXITCODE_SUCCESS && rcExit != RTEXITCODE_SUCCESS)
        rcExit = rcExit2;

    return rcExit;
}

