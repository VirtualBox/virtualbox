/** @file
 * Shared Clipboard: Common X11 code.
 */

/*
 * Copyright (C) 2006-2026 Oracle and/or its affiliates.
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

/* Note: to automatically run regression tests on the Shared Clipboard,
 * execute the tstClipboardGH-X11 testcase.  If you often make changes to the
 * clipboard code, adding the line
 *
 *   OTHERS += $(PATH_tstClipboardGH-X11)/tstClipboardGH-X11.run
 *
 * to LocalConfig.kmk will cause the tests to be run every time the code is
 * changed.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_SHARED_CLIPBOARD

#include <errno.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#ifdef RT_OS_SOLARIS
#include <tsol/label.h>
#endif

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Intrinsic.h>
#include <X11/Shell.h>
#include <X11/Xproto.h>
#include <X11/StringDefs.h>

#include <iprt/assert.h>
#include <iprt/types.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/thread.h>
#include <iprt/utf16.h>
#include <iprt/uri.h>

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
# include <iprt/cpp/list.h>
# include <iprt/cpp/ministring.h>
# include <VBox/GuestHost/SharedClipboard-transfers.h>
#endif
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
# include <VBox/GuestHost/clipboard-transfers-http.h>
#endif

#include <VBox/log.h>
#include <VBox/version.h>

#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/GuestHost/SharedClipboard-x11.h>
#include <VBox/GuestHost/clipboard-helper.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>

/** Own macro for declaring function visibility / linkage based on whether this
 *  code runs as part of test cases or not. */
#ifdef TESTCASE
# define SHCL_X11_DECL(x) x
#else
# define SHCL_X11_DECL(x) static x
#endif


/*********************************************************************************************************************************
*   Externals                                                                                                                    *
*********************************************************************************************************************************/
#ifdef TESTCASE
extern void tstThreadScheduleCall(void (*proc)(void *, void *), void *client_data);
extern void tstClipRequestData(SHCLX11CTX* pCtx, SHCLX11FMTIDX target, void *closure);
extern void tstRequestTargets(SHCLX11CTX* pCtx);
#endif


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
class formats;
SHCL_X11_DECL(Atom) clipGetAtom(PSHCLX11CTX pCtx, const char *pszName);
SHCL_X11_DECL(void) clipQueryX11Targets(PSHCLX11CTX pCtx);

static int          clipInitInternal(PSHCLX11CTX pCtx);
static void         clipUninitInternal(PSHCLX11CTX pCtx);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/

/**
 * The table maps X11 names to data formats
 * and to the corresponding VBox clipboard formats.
 */
SHCL_X11_DECL(SHCLX11FMTTABLE) g_aFormats[] =
{
    { "INVALID",                            SHCLX11FMT_INVALID,     VBOX_SHCL_FMT_NONE },

    { "UTF8_STRING",                        SHCLX11FMT_UTF8,        VBOX_SHCL_FMT_UNICODETEXT },
    { "text/plain;charset=UTF-8",           SHCLX11FMT_UTF8,        VBOX_SHCL_FMT_UNICODETEXT },
    { "text/plain;charset=utf-8",           SHCLX11FMT_UTF8,        VBOX_SHCL_FMT_UNICODETEXT },
    { "STRING",                             SHCLX11FMT_TEXT,        VBOX_SHCL_FMT_UNICODETEXT },
    { "TEXT",                               SHCLX11FMT_TEXT,        VBOX_SHCL_FMT_UNICODETEXT },
    { "text/plain",                         SHCLX11FMT_TEXT,        VBOX_SHCL_FMT_UNICODETEXT },

    { "text/html",                          SHCLX11FMT_HTML,        VBOX_SHCL_FMT_HTML },
    { "text/html;charset=utf-8",            SHCLX11FMT_HTML,        VBOX_SHCL_FMT_HTML },
    { "application/x-moz-nativehtml",       SHCLX11FMT_HTML,        VBOX_SHCL_FMT_HTML },

    { "image/bmp",                          SHCLX11FMT_BMP,         VBOX_SHCL_FMT_BITMAP },
    { "image/x-bmp",                        SHCLX11FMT_BMP,         VBOX_SHCL_FMT_BITMAP },
    { "image/x-MS-bmp",                     SHCLX11FMT_BMP,         VBOX_SHCL_FMT_BITMAP },
    /** @todo Inkscape exports image/png but not bmp... */

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    { "text/uri-list",                      SHCLX11FMT_URI_LIST,                    VBOX_SHCL_FMT_URI_LIST },
    { "x-special/gnome-copied-files",       SHCLX11FMT_URI_LIST_GNOME_COPIED_FILES, VBOX_SHCL_FMT_URI_LIST },
    { "x-special/mate-copied-files",        SHCLX11FMT_URI_LIST_MATE_COPIED_FILES,  VBOX_SHCL_FMT_URI_LIST },
    { "x-special/nautilus-clipboard",       SHCLX11FMT_URI_LIST_NAUTILUS_CLIPBOARD, VBOX_SHCL_FMT_URI_LIST },
    /* Associate KDE cut-selection with the VBox URI format so that we advertise
     * it when exporting files.  It is metadata only, so s_aTransferTargets
     * explicitly prevents selecting or parsing it in the other direction. */
    { "application/x-kde-cutselection",     SHCLX11FMT_URI_LIST_KDE_CUTSELECTION,   VBOX_SHCL_FMT_URI_LIST },
    /** @todo Anything else we need to add here? */
    /** @todo Add Wayland / Weston support. */
#endif
};


#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/** X11 transfer target can be read from the X11 clipboard. */
# define SHCLX11TRANSFERDIR_F_FROM_X11          RT_BIT_32(0)
/** X11 transfer target can be offered to the X11 clipboard. */
# define SHCLX11TRANSFERDIR_F_TO_X11            RT_BIT_32(1)
/** X11 transfer target supports both directions. */
# define SHCLX11TRANSFERDIR_F_BIDIRECTIONAL      (SHCLX11TRANSFERDIR_F_FROM_X11 | SHCLX11TRANSFERDIR_F_TO_X11)

/**
 * Direction and selection policy for an X11 file-transfer target.
 *
 * The X11 format enum identifies the representation; it does not define
 * preference or whether the representation actually contains file names.
 */
typedef struct SHCLX11TRANSFERTARGET
{
    /** The X11 transfer format. */
    SHCLX11FMT enmFmt;
    /** SHCLX11TRANSFERDIR_F_XXX direction flags. */
    uint32_t   fDirections;
    /** Incoming selection priority; higher values have higher priority, while
     *  zero means the target cannot be read as a file list. */
    uint8_t    uPriorityFromX11;
} SHCLX11TRANSFERTARGET;

/**
 * Transfer target capabilities and incoming selection order.
 *
 * Keeping this policy separate from SHCLX11FMT makes adding or reordering enum
 * values harmless.  Prefer the standard URI-list target when several file-list
 * representations are offered, followed by the file-manager-specific formats.
 * KDE cut-selection carries only cut/copy state, not file names, and therefore
 * is offered when exporting but never accepted as an incoming file list.
 */
static const SHCLX11TRANSFERTARGET s_aTransferTargets[] =
{
    { SHCLX11FMT_URI_LIST,                     SHCLX11TRANSFERDIR_F_BIDIRECTIONAL, 100 },
    { SHCLX11FMT_URI_LIST_GNOME_COPIED_FILES,  SHCLX11TRANSFERDIR_F_BIDIRECTIONAL,  90 },
    { SHCLX11FMT_URI_LIST_MATE_COPIED_FILES,   SHCLX11TRANSFERDIR_F_BIDIRECTIONAL,  80 },
    { SHCLX11FMT_URI_LIST_NAUTILUS_CLIPBOARD,  SHCLX11TRANSFERDIR_F_BIDIRECTIONAL,  70 },
    { SHCLX11FMT_URI_LIST_KDE_CUTSELECTION,    SHCLX11TRANSFERDIR_F_TO_X11,          0 }
};
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */


#ifdef TESTCASE
# ifdef RT_OS_SOLARIS_10
char XtStrings [] = "";
WidgetClassRec* applicationShellWidgetClass;
char XtShellStrings [] = "";
int XmbTextPropertyToTextList(
    Display*            /* display */,
    XTextProperty*      /* text_prop */,
    char***             /* list_return */,
    int*                /* count_return */
)
{
  return 0;
}
# else /* !RT_OS_SOLARIS_10 */
const char XtStrings [] = "";
_WidgetClassRec* applicationShellWidgetClass;
const char XtShellStrings [] = "";
# endif /* RT_OS_SOLARIS_10 */
#else /* !TESTCASE */
# ifdef VBOX_WITH_SHARED_CLIPBOARD_X11_LAZY_LOAD
/* Defines needed for lazy loading global data from the shared objects (.so).
 * See r157060. */
DECLASM(WidgetClass * ) LazyGetPtr_applicationShellWidgetClass(void);
#define applicationShellWidgetClass (*LazyGetPtr_applicationShellWidgetClass())
DECLASM(const char *) LazyGetPtr_XtStrings(void);
#define XtStrings (LazyGetPtr_XtStrings())
# endif
#endif /* TESTCASE */


/*********************************************************************************************************************************
*   Defines                                                                                                                      *
*********************************************************************************************************************************/

#define SHCL_MAX_X11_FORMATS        RT_ELEMENTS(g_aFormats)


/*********************************************************************************************************************************
*   Internal structures                                                                                                          *
*********************************************************************************************************************************/

#ifdef TESTCASE
/**
 * Return the max. number of elements in the X11 format table.
 * Used by the testing code in tstClipboardGH-X11.cpp
 * which cannot use RT_ELEMENTS(g_aFormats) directly.
 *
 * @return size_t The number of elements in the g_aFormats array.
 */
SHCL_X11_DECL(size_t) clipReportMaxX11Formats(void)
{
    return (RT_ELEMENTS(g_aFormats));
}
#endif

/**
 * Returns the atom corresponding to a supported X11 format.
 *
 * @returns Found atom to the corresponding X11 format.
 * @param   pCtx                The X11 clipboard context to use.
 * @param   uFmtIdx             Format index to look up atom for.
 */
static Atom clipAtomForX11Format(PSHCLX11CTX pCtx, SHCLX11FMTIDX uFmtIdx)
{
    AssertReturn(uFmtIdx < RT_ELEMENTS(g_aFormats), 0);
    return clipGetAtom(pCtx, g_aFormats[uFmtIdx].pcszAtom);
}

/**
 * Returns the SHCLX11FMT corresponding to a supported X11 format.
 *
 * @return  SHCLX11FMT for a specific format index.
 * @param   uFmtIdx             Format index to look up SHCLX11CLIPFMT for.
 */
SHCL_X11_DECL(SHCLX11FMT) clipRealFormatForX11Format(SHCLX11FMTIDX uFmtIdx)
{
    AssertReturn(uFmtIdx < RT_ELEMENTS(g_aFormats), SHCLX11FMT_INVALID);
    return g_aFormats[uFmtIdx].enmFmtX11;
}

/**
 * Returns the VBox format corresponding to a supported X11 format.
 *
 * @return  SHCLFORMAT for a specific format index.
 * @param   uFmtIdx             Format index to look up VBox format for.
 */
static SHCLFORMAT clipVBoxFormatForX11Format(SHCLX11FMTIDX uFmtIdx)
{
    AssertReturn(uFmtIdx < RT_ELEMENTS(g_aFormats), VBOX_SHCL_FMT_NONE);
    return g_aFormats[uFmtIdx].uFmtVBox;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Looks up the direction and priority policy of an X11 transfer target.
 *
 * @returns Transfer target capabilities, or NULL if the format is not a transfer target.
 * @param   enmFmt              X11 format to look up.
 */
static const SHCLX11TRANSFERTARGET *shClX11TransferTargetLookup(SHCLX11FMT enmFmt)
{
    for (size_t i = 0; i < RT_ELEMENTS(s_aTransferTargets); i++)
        if (s_aTransferTargets[i].enmFmt == enmFmt)
            return &s_aTransferTargets[i];
    return NULL;
}


/**
 * Checks whether an X11 transfer target supports a direction.
 *
 * Unknown transfer targets are deliberately rejected so they cannot silently
 * become importable or exportable merely by being added to g_aFormats.
 *
 * @returns true if supported, false if not.
 * @param   enmFmt              X11 format to check.
 * @param   fDirection          SHCLX11TRANSFERDIR_F_XXX direction to check.
 */
static bool shClX11TransferTargetSupportsDirection(SHCLX11FMT enmFmt, uint32_t fDirection)
{
    const SHCLX11TRANSFERTARGET *pTarget = shClX11TransferTargetLookup(enmFmt);
    return pTarget && (pTarget->fDirections & fDirection) != 0;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

/**
 * Looks up the X11 format matching a given X11 atom.
 *
 * @returns The format on success, NIL_CLIPX11FORMAT on failure.
 * @param   pCtx                The X11 clipboard context to use.
 * @param   atomFormat          Atom to look up X11 format for.
 */
static SHCLX11FMTIDX clipFindX11FormatByAtom(PSHCLX11CTX pCtx, Atom atomFormat)
{
    for (unsigned i = 0; i < RT_ELEMENTS(g_aFormats); ++i)
        if (clipAtomForX11Format(pCtx, i) == atomFormat)
        {
            LogFlowFunc(("Returning index %u for atom '%s'\n", i, g_aFormats[i].pcszAtom));
            return i;
        }
    return NIL_CLIPX11FORMAT;
}

/**
 * Enumerates supported X11 clipboard formats corresponding to given VBox formats.
 *
 * @returns The next matching X11 format index in the list, or NIL_CLIPX11FORMAT if there are no more.
 * @param   uFormatsVBox            VBox formats to enumerate supported X11 clipboard formats for.
 * @param   lastFmtIdx              The value returned from the last call of this function.
 *                                  Use NIL_CLIPX11FORMAT to start the enumeration.
 */
static SHCLX11FMTIDX clipEnumX11Formats(SHCLFORMATS uFormatsVBox,
                                        SHCLX11FMTIDX lastFmtIdx)
{
    for (unsigned i = lastFmtIdx + 1; i < RT_ELEMENTS(g_aFormats); ++i)
    {
        SHCLFORMAT const uFmtVBox = clipVBoxFormatForX11Format(i);
        if (uFormatsVBox & uFmtVBox)
        {
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
            /* URI targets are filtered by the explicit export policy.  This
             * includes KDE metadata, while excluding any future import-only
             * representations. */
            if (   uFmtVBox != VBOX_SHCL_FMT_URI_LIST
                || shClX11TransferTargetSupportsDirection(clipRealFormatForX11Format(i),
                                                           SHCLX11TRANSFERDIR_F_TO_X11))
#endif
                return i;
        }
    }

    return NIL_CLIPX11FORMAT;
}

/**
 * Array of structures for mapping Xt widgets to context pointers.  We
 * need this because the widget clipboard callbacks do not pass user data.
 */
static struct
{
    /** Pointer to widget we want to associate the context with. */
    Widget      pWidget;
    /** Pointer to X11 context associated with the widget. */
    PSHCLX11CTX pCtx;
} g_aContexts[VBOX_SHARED_CLIPBOARD_X11_CONNECTIONS_MAX];

/**
 * Registers a new X11 clipboard context.
 *
 * @returns VBox status code.
 * @param   pCtx                The X11 clipboard context to use.
 */
static int clipRegisterContext(PSHCLX11CTX pCtx)
{
    AssertPtrReturn(pCtx, VERR_INVALID_PARAMETER);

    bool fFound = false;

    Widget pWidget = pCtx->pWidget;
    AssertReturn(pWidget != NULL, VERR_INVALID_PARAMETER);

    for (unsigned i = 0; i < RT_ELEMENTS(g_aContexts); ++i)
    {
        AssertReturn(   (g_aContexts[i].pWidget != pWidget)
                     && (g_aContexts[i].pCtx != pCtx), VERR_WRONG_ORDER);
        if (g_aContexts[i].pWidget == NULL && !fFound)
        {
            AssertReturn(g_aContexts[i].pCtx == NULL, VERR_INTERNAL_ERROR);
            g_aContexts[i].pWidget = pWidget;
            g_aContexts[i].pCtx = pCtx;
            fFound = true;
        }
    }

    return fFound ? VINF_SUCCESS : VERR_OUT_OF_RESOURCES;
}

/**
 * Unregister an X11 clipboard context.
 *
 * @param   pCtx                The X11 clipboard context to use.
 */
static void clipUnregisterContext(PSHCLX11CTX pCtx)
{
    AssertPtrReturnVoid(pCtx);

    Widget pWidget = pCtx->pWidget;
    if (!pWidget)
        return;

    bool fFound = false;
    for (unsigned i = 0; i < RT_ELEMENTS(g_aContexts); ++i)
    {
        Assert(!fFound || g_aContexts[i].pWidget != pWidget);
        if (g_aContexts[i].pWidget == pWidget)
        {
            Assert(g_aContexts[i].pCtx != NULL);
            g_aContexts[i].pWidget = NULL;
            g_aContexts[i].pCtx = NULL;
            fFound = true;
        }
    }
}

/**
 * Finds a X11 clipboard context for a specific X11 widget.
 *
 * @returns Pointer to associated X11 clipboard context if found, or NULL if not found.
 * @param   pWidget                 X11 widget to return X11 clipboard context for.
 */
static PSHCLX11CTX clipLookupContext(Widget pWidget)
{
    AssertPtrReturn(pWidget, NULL);

    for (unsigned i = 0; i < RT_ELEMENTS(g_aContexts); ++i)
    {
        if (g_aContexts[i].pWidget == pWidget)
        {
            Assert(g_aContexts[i].pCtx != NULL);
            return g_aContexts[i].pCtx;
        }
    }

    return NULL;
}

/**
 * Converts an atom name string to an X11 atom, looking it up in a cache before asking the server.
 *
 * @returns Found X11 atom.
 * @param   pCtx                The X11 clipboard context to use.
 * @param   pcszName            Name of atom to return atom for.
 */
SHCL_X11_DECL(Atom) clipGetAtom(PSHCLX11CTX pCtx, const char *pcszName)
{
    AssertPtrReturn(pcszName, None);
    return XInternAtom(XtDisplay(pCtx->pWidget), pcszName, False);
}

/** String written to the wakeup pipe. */
#define WAKE_UP_STRING      "WakeUp!"
/** Length of the string written. */
#define WAKE_UP_STRING_LEN  ( sizeof(WAKE_UP_STRING) - 1 )

/**
 * Schedules a function call to run on the Xt event thread by passing it to
 * the application context as a 0ms timeout and waking up the event loop by
 * writing to the wakeup pipe which it monitors.
 */
static int clipThreadScheduleCall(PSHCLX11CTX pCtx,
                                  void (*proc)(void *, void *),
                                  void *client_data)
{
    LogFlowFunc(("proc=%p, client_data=%p\n", proc, client_data));

#ifndef TESTCASE
    AssertReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->pAppContext, VERR_INVALID_POINTER);

    XtAppAddTimeOut(pCtx->pAppContext, 0, (XtTimerCallbackProc)proc,
                    (XtPointer)client_data);

    ssize_t cbWritten;
    do
        cbWritten = write(pCtx->wakeupPipeWrite, WAKE_UP_STRING, WAKE_UP_STRING_LEN);
    while (cbWritten < 0 && errno == EINTR);
    if (cbWritten < 0)
    {
        /* A full non-blocking pipe is already readable and will wake the worker. */
# if EAGAIN == EWOULDBLOCK
        if (errno != EAGAIN)
# else
        if (errno != EAGAIN && errno != EWOULDBLOCK)
# endif
        {
            int const rc = RTErrConvertFromErrno(errno);
            /* The Xt callback is already queued and cannot be cancelled without racing the worker. */
            AssertFatalMsgRC(rc, ("Waking the X11 event thread failed with %Rrc\n", rc));
        }
    }
    else if (cbWritten != WAKE_UP_STRING_LEN)
        AssertFatalMsgFailed(("Waking the X11 event thread wrote only %zd of %zu bytes\n",
                              cbWritten, (size_t)WAKE_UP_STRING_LEN));
#else
    RT_NOREF(pCtx);
    tstThreadScheduleCall(proc, client_data);
#endif

    LogFlowFuncLeaveRC(VINF_SUCCESS);
    return VINF_SUCCESS;
}

/**
 * Reports the formats currently supported by the X11 clipboard to VBox.
 *
 * @note    Runs in Xt event thread.
 *
 * @param   pCtx                The X11 clipboard context to use.
 */
static void clipReportFormatsToVBox(PSHCLX11CTX pCtx)
{
    SHCLFORMATS vboxFmt = clipVBoxFormatForX11Format(pCtx->idxFmtText);
    vboxFmt            |= clipVBoxFormatForX11Format(pCtx->idxFmtBmp);
    vboxFmt            |= clipVBoxFormatForX11Format(pCtx->idxFmtHTML);
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    vboxFmt            |= clipVBoxFormatForX11Format(pCtx->idxFmtURI);
    if (vboxFmt & VBOX_SHCL_FMT_URI_LIST)
        LogRelMax2(16, ("Shared Clipboard: X11 reports host file transfer via target '%s' (VBox formats %#x)\n",
                       g_aFormats[pCtx->idxFmtURI].pcszAtom, vboxFmt));
#endif

    LogFlowFunc(("idxFmtText=%u ('%s'), idxFmtBmp=%u ('%s'), idxFmtHTML=%u ('%s')",
                 pCtx->idxFmtText, g_aFormats[pCtx->idxFmtText].pcszAtom,
                 pCtx->idxFmtBmp,  g_aFormats[pCtx->idxFmtBmp].pcszAtom,
                 pCtx->idxFmtHTML, g_aFormats[pCtx->idxFmtHTML].pcszAtom));
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    Log((", idxFmtURI=%u ('%s')", pCtx->idxFmtURI, g_aFormats[pCtx->idxFmtURI].pcszAtom));
#endif
    Log((" -> vboxFmt=%#x\n", vboxFmt));

    if (LogRelIs2Enabled())
    {
        char *pszFmts = ShClFormatsToStrA(vboxFmt);
        LogRel2(("Shared Clipboard: X11 reported available VBox formats %#x / '%s'\n", vboxFmt, pszFmts ? pszFmts : "<alloc failed>"));
        RTStrFree(pszFmts);
    }

    if (pCtx->Callbacks.pfnReportFormats)
        pCtx->Callbacks.pfnReportFormats(pCtx->pFrontend, vboxFmt, NULL /* pvUser */);
}

/**
 * Forgets which formats were previously in the X11 clipboard.  Called when we
 * grab the clipboard.
 *
 * @param   pCtx                The X11 clipboard context to use.
 */
static void clipResetX11Formats(PSHCLX11CTX pCtx)
{
    LogFlowFuncEnter();

    pCtx->idxFmtText = 0;
    pCtx->idxFmtBmp  = 0;
    pCtx->idxFmtHTML = 0;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    pCtx->idxFmtURI  = 0;
#endif
}

/**
 * Tells VBox that X11 currently has nothing in its clipboard.
 *
 * @param  pCtx                 The X11 clipboard context to use.
 */
SHCL_X11_DECL(void) clipReportEmpty(PSHCLX11CTX pCtx)
{
    clipResetX11Formats(pCtx);
    clipReportFormatsToVBox(pCtx);
}

/**
 * Go through an array of X11 clipboard targets to see if they contain a text
 * format we can support, and if so choose the ones we prefer (e.g. we like
 * UTF-8 better than plain text).
 *
 * @return Index to supported X clipboard format.
 * @param  pCtx                 The X11 clipboard context to use.
 * @param  paIdxFmtTargets      The list of targets.
 * @param  cTargets             The size of the list in @a pTargets.
 */
SHCL_X11_DECL(SHCLX11FMTIDX) clipGetTextFormatFromTargets(PSHCLX11CTX pCtx,
                                                          SHCLX11FMTIDX *paIdxFmtTargets,
                                                          size_t cTargets)
{
    AssertPtrReturn(pCtx, NIL_CLIPX11FORMAT);
    AssertReturn(RT_VALID_PTR(paIdxFmtTargets) || cTargets == 0, NIL_CLIPX11FORMAT);

    SHCLX11FMTIDX idxFmtText = NIL_CLIPX11FORMAT;
    SHCLX11FMT    fmtTextX11 = SHCLX11FMT_INVALID;
    for (unsigned i = 0; i < cTargets; ++i)
    {
        SHCLX11FMTIDX idxFmt = paIdxFmtTargets[i];
        if (idxFmt != NIL_CLIPX11FORMAT)
        {
            if (   (clipVBoxFormatForX11Format(idxFmt) == VBOX_SHCL_FMT_UNICODETEXT)
                && fmtTextX11 < clipRealFormatForX11Format(idxFmt))
            {
                fmtTextX11 = clipRealFormatForX11Format(idxFmt);
                idxFmtText = idxFmt;
            }
        }
    }
    return idxFmtText;
}

/**
 * Goes through an array of X11 clipboard targets to see if they contain a bitmap
 * format we can support, and if so choose the ones we prefer (e.g. we like
 * BMP better than PNG because we don't have to convert).
 *
 * @return Supported X clipboard format.
 * @param  pCtx                 The X11 clipboard context to use.
 * @param  paIdxFmtTargets      The list of targets.
 * @param  cTargets             The size of the list in @a pTargets.
 */
static SHCLX11FMTIDX clipGetBitmapFormatFromTargets(PSHCLX11CTX pCtx,
                                                    SHCLX11FMTIDX *paIdxFmtTargets,
                                                    size_t cTargets)
{
    AssertPtrReturn(pCtx, NIL_CLIPX11FORMAT);
    AssertReturn(RT_VALID_PTR(paIdxFmtTargets) || cTargets == 0, NIL_CLIPX11FORMAT);

    SHCLX11FMTIDX idxFmtBmp = NIL_CLIPX11FORMAT;
    SHCLX11FMT    fmtBmpX11 = SHCLX11FMT_INVALID;
    for (unsigned i = 0; i < cTargets; ++i)
    {
        SHCLX11FMTIDX idxFmt = paIdxFmtTargets[i];
        if (idxFmt != NIL_CLIPX11FORMAT)
        {
            if (   (clipVBoxFormatForX11Format(idxFmt) == VBOX_SHCL_FMT_BITMAP)
                && fmtBmpX11 < clipRealFormatForX11Format(idxFmt))
            {
                fmtBmpX11 = clipRealFormatForX11Format(idxFmt);
                idxFmtBmp = idxFmt;
            }
        }
    }
    return idxFmtBmp;
}

/**
 * Goes through an array of X11 clipboard targets to see if they contain a HTML
 * format we can support, and if so choose the ones we prefer.
 *
 * @return Supported X clipboard format.
 * @param  pCtx                 The X11 clipboard context to use.
 * @param  paIdxFmtTargets      The list of targets.
 * @param  cTargets             The size of the list in @a pTargets.
 */
static SHCLX11FMTIDX clipGetHtmlFormatFromTargets(PSHCLX11CTX pCtx,
                                                  SHCLX11FMTIDX *paIdxFmtTargets,
                                                  size_t cTargets)
{
    AssertPtrReturn(pCtx, NIL_CLIPX11FORMAT);
    AssertReturn(RT_VALID_PTR(paIdxFmtTargets) || cTargets == 0, NIL_CLIPX11FORMAT);

    SHCLX11FMTIDX idxFmtHTML = NIL_CLIPX11FORMAT;
    SHCLX11FMT    fmxHTMLX11 = SHCLX11FMT_INVALID;
    for (unsigned i = 0; i < cTargets; ++i)
    {
        SHCLX11FMTIDX idxFmt = paIdxFmtTargets[i];
        if (idxFmt != NIL_CLIPX11FORMAT)
        {
            if (   (clipVBoxFormatForX11Format(idxFmt) == VBOX_SHCL_FMT_HTML)
                && fmxHTMLX11 < clipRealFormatForX11Format(idxFmt))
            {
                fmxHTMLX11 = clipRealFormatForX11Format(idxFmt);
                idxFmtHTML = idxFmt;
            }
        }
    }
    return idxFmtHTML;
}

# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Goes through an array of X11 clipboard targets to find the preferred file-list
 * representation that is safe to import.
 *
 * Selection uses s_aTransferTargets rather than SHCLX11FMT enum values or the
 * order in which the clipboard owner returned its targets.  Targets which only
 * carry transfer metadata are ignored even when they map to VBOX_SHCL_FMT_URI_LIST
 * for export purposes.
 *
 * @return Supported X clipboard format.
 * @param  pCtx                 The X11 clipboard context to use.
 * @param  paIdxFmtTargets      The list of targets.
 * @param  cTargets             The size of the list in @a pTargets.
 */
SHCL_X11_DECL(SHCLX11FMTIDX) clipGetURIListFormatFromTargets(PSHCLX11CTX pCtx,
                                                             SHCLX11FMTIDX *paIdxFmtTargets,
                                                             size_t cTargets)
{
    AssertPtrReturn(pCtx, NIL_CLIPX11FORMAT);
    AssertReturn(RT_VALID_PTR(paIdxFmtTargets) || cTargets == 0, NIL_CLIPX11FORMAT);

    SHCLX11FMTIDX idxFmtURI = NIL_CLIPX11FORMAT;
    uint8_t       uPriority = 0;
    bool          fSawUnsupportedTransferMetadata = false;
    for (unsigned i = 0; i < cTargets; ++i)
    {
        SHCLX11FMTIDX idxFmt = paIdxFmtTargets[i];
        if (idxFmt != NIL_CLIPX11FORMAT)
        {
            SHCLX11FMT const enmFmtX11 = clipRealFormatForX11Format(idxFmt);
            const SHCLX11TRANSFERTARGET *pTarget = shClX11TransferTargetLookup(enmFmtX11);
            if (   pTarget
                && !(pTarget->fDirections & SHCLX11TRANSFERDIR_F_FROM_X11))
            {
                fSawUnsupportedTransferMetadata = true;
                LogRelMax2(16, ("Shared Clipboard: Ignoring X11 clipboard target '%s'; it only describes cut/copy "
                                "state, not file names\n",
                               g_aFormats[idxFmt].pcszAtom));
            }
            else if (   pTarget
                     && clipVBoxFormatForX11Format(idxFmt) == VBOX_SHCL_FMT_URI_LIST
                     && uPriority < pTarget->uPriorityFromX11)
            {
                uPriority  = pTarget->uPriorityFromX11;
                idxFmtURI  = idxFmt;
            }
        }
    }

    if (idxFmtURI != NIL_CLIPX11FORMAT)
        LogRelMax2(16, ("Shared Clipboard: Selected X11 URI-list target '%s' for host file transfer\n",
                       g_aFormats[idxFmtURI].pcszAtom));
    else if (fSawUnsupportedTransferMetadata)
        LogRelMax(16, ("Shared Clipboard: X11 clipboard had file-transfer metadata but no supported file list target "
                        "(for example text/uri-list); host file transfer will not be announced\n"));

    return idxFmtURI;
}
# endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

/**
 * Goes through an array of X11 clipboard targets to see if we can support any
 * of them and if relevant to choose the ones we prefer (e.g. we like Utf8
 * better than plain text).
 *
 * @param  pCtx                 The X11 clipboard context to use.
 * @param  paIdxFmtTargets      The list of targets.
 * @param  cTargets             The size of the list in @a pTargets.
 */
static void clipGetFormatsFromTargets(PSHCLX11CTX pCtx,
                                      SHCLX11FMTIDX *paIdxFmtTargets, size_t cTargets)
{
    AssertPtrReturnVoid(pCtx);
    AssertPtrReturnVoid(paIdxFmtTargets);

    SHCLX11FMTIDX idxFmtText = clipGetTextFormatFromTargets(pCtx, paIdxFmtTargets, cTargets);
    if (pCtx->idxFmtText != idxFmtText)
        pCtx->idxFmtText = idxFmtText;

    pCtx->idxFmtBmp = SHCLX11FMT_INVALID; /* not yet supported */ /** @todo r=andy Check this. */
    SHCLX11FMTIDX idxFmtBmp = clipGetBitmapFormatFromTargets(pCtx, paIdxFmtTargets, cTargets);
    if (pCtx->idxFmtBmp != idxFmtBmp)
        pCtx->idxFmtBmp = idxFmtBmp;

    SHCLX11FMTIDX idxFmtHTML = clipGetHtmlFormatFromTargets(pCtx, paIdxFmtTargets, cTargets);
    if (pCtx->idxFmtHTML != idxFmtHTML)
        pCtx->idxFmtHTML = idxFmtHTML;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    SHCLX11FMTIDX idxFmtURI = clipGetURIListFormatFromTargets(pCtx, paIdxFmtTargets, cTargets);
    if (pCtx->idxFmtURI != idxFmtURI)
        pCtx->idxFmtURI = idxFmtURI;
#endif
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_XT_BUSY
DECLINLINE(bool) clipGetXtBusy(PSHCLX11CTX pCtx)
{
    LogFlowFunc(("fXtBusy=%RTbool, fXtNeedsUpdate=%RTbool\n", pCtx->fXtBusy, pCtx->fXtNeedsUpdate));
    return pCtx->fXtBusy;
}

DECLINLINE(bool) clipGetXtNeedsUpdate(PSHCLX11CTX pCtx)
{
    LogFlowFunc(("fXtBusy=%RTbool, fXtNeedsUpdate=%RTbool\n", pCtx->fXtBusy, pCtx->fXtNeedsUpdate));
    return pCtx->fXtNeedsUpdate;
}

DECLINLINE(bool) clipSetXtBusy(PSHCLX11CTX pCtx, bool fBusy)
{
    pCtx->fXtBusy = fBusy;
    LogFlowFunc(("fXtBusy=%RTbool, fXtNeedsUpdate=%RTbool\n", pCtx->fXtBusy, pCtx->fXtNeedsUpdate));
    return pCtx->fXtBusy;
}

DECLINLINE(bool) clipSetXtNeedsUpdate(PSHCLX11CTX pCtx, bool fNeedsUpdate)
{
    pCtx->fXtNeedsUpdate = fNeedsUpdate;
    LogFlowFunc(("fXtBusy=%RTbool, fXtNeedsUpdate=%RTbool\n", pCtx->fXtBusy, pCtx->fXtNeedsUpdate));
    return pCtx->fXtNeedsUpdate;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_XT_BUSY */

/**
 * Updates the context's information about targets currently supported by X11,
 * based on an array of X11 atoms.
 *
 * @param  pCtx                 The X11 clipboard context to use.
 * @param  pTargets             The array of atoms describing the targets supported.
 * @param  cTargets             The size of the array @a pTargets.
 */
SHCL_X11_DECL(void) clipUpdateX11Targets(PSHCLX11CTX pCtx, SHCLX11FMTIDX *paIdxFmtTargets, size_t cTargets)
{
    LogFlowFuncEnter();

#ifdef VBOX_WITH_SHARED_CLIPBOARD_XT_BUSY
    clipSetXtBusy(pCtx, false);
    if (clipGetXtNeedsUpdate(pCtx))
    {
        /* We may already be out of date. */
        clipSetXtNeedsUpdate(pCtx, false);
        clipQueryX11Targets(pCtx);
        return;
    }
#endif

    if (paIdxFmtTargets == NULL)
    {
        /* No data available */
        clipReportEmpty(pCtx);
        return;
    }

    clipGetFormatsFromTargets(pCtx, paIdxFmtTargets, cTargets);
    clipReportFormatsToVBox(pCtx);
}

/**
 * Notifies the VBox clipboard about available data formats ("targets" on X11),
 * based on the information obtained from the X11 clipboard.
 *
 * @note  Callback installed by clipQueryX11Targets() for XtGetSelectionValue().
 * @note  This function is treated as API glue, and as such is not part of any
 *        unit test.  So keep it simple, be paranoid and log everything.
 */
SHCL_X11_DECL(void) clipQueryX11TargetsCallback(Widget widget, XtPointer pClient,
                                                Atom * /* selection */, Atom *atomType,
                                                XtPointer pValue, long unsigned int *pcLen,
                                                int *piFormat)
{
    RT_NOREF(piFormat);

    PSHCLX11CTX pCtx = reinterpret_cast<SHCLX11CTX *>(pClient);

    LogFlowFunc(("pValue=%p, *pcLen=%u, *atomType=%d%s\n",
                 pValue, *pcLen, *atomType, *atomType == XT_CONVERT_FAIL ? " (XT_CONVERT_FAIL)" : ""));

    Atom *pAtoms = (Atom *)pValue;

    unsigned cFormats = *pcLen;

    LogRel2(("Shared Clipboard: Querying X11 formats ...\n"));
    LogRel2(("Shared Clipboard: %u X11 formats were found\n", cFormats));

    SHCLX11FMTIDX *paIdxFmt = NULL;
    if (   cFormats
        && pValue
        && (*atomType != XT_CONVERT_FAIL /* time out */))
    {
        /* Allocated array to hold the format indices. */
        paIdxFmt = (SHCLX11FMTIDX *)RTMemAllocZ(cFormats * sizeof(SHCLX11FMTIDX));
    }

#if !defined(TESTCASE)
    if (pValue)
    {
        for (unsigned i = 0; i < cFormats; ++i)
        {
            if (pAtoms[i])
            {
                char *pszName = XGetAtomName(XtDisplay(widget), pAtoms[i]);
                LogRel2(("Shared Clipboard: Found X11 format '%s'\n", pszName));
                XFree(pszName);
            }
            else
                LogFunc(("Found empty target\n"));
        }
    }
#endif

    if (paIdxFmt)
    {
        for (unsigned i = 0; i < cFormats; ++i)
        {
            for (unsigned j = 0; j < RT_ELEMENTS(g_aFormats); ++j)
            {
                Atom target = XInternAtom(XtDisplay(widget),
                                          g_aFormats[j].pcszAtom, False);
                if (*(pAtoms + i) == target)
                    paIdxFmt[i] = j;
            }
#if !defined(TESTCASE)
            if (paIdxFmt[i] != SHCLX11FMT_INVALID)
                LogRel2(("Shared Clipboard: Reporting X11 format '%s'\n", g_aFormats[paIdxFmt[i]].pcszAtom));
#endif
        }
    }
    else
        LogFunc(("Reporting empty targets (none reported or allocation failure)\n"));

    clipUpdateX11Targets(pCtx, paIdxFmt, cFormats);
    RTMemFree(paIdxFmt);

    XtFree(reinterpret_cast<char *>(pValue));
}

/**
 * Queries the current formats ("targets") of the X11 clipboard ("CLIPBOARD").
 *
 * @param   pCtx                The X11 clipboard context to use.
 */
SHCL_X11_DECL(void) clipQueryX11Targets(PSHCLX11CTX pCtx)
{
#ifndef TESTCASE

# ifdef VBOX_WITH_SHARED_CLIPBOARD_XT_BUSY
    if (clipGetXtBusy(pCtx))
    {
        clipSetXtNeedsUpdate(pCtx, true);
        return;
    }
    clipSetXtBusy(pCtx, true);
# endif

    XtGetSelectionValue(pCtx->pWidget,
                        clipGetAtom(pCtx, "CLIPBOARD"),
                        clipGetAtom(pCtx, "TARGETS"),
                        clipQueryX11TargetsCallback, pCtx,
                        CurrentTime);
#else
    tstRequestTargets(pCtx);
#endif
}

typedef struct
{
    int type;                   /* event base */
    unsigned long serial;
    Bool send_event;
    Display *display;
    Window window;
    int subtype;
    Window owner;
    Atom selection;
    Time timestamp;
    Time selection_timestamp;
} XFixesSelectionNotifyEvent;

#ifndef TESTCASE
/**
 * Waits until an event arrives and handle it if it is an XFIXES selection
 * event, which Xt doesn't know about.
 *
 * @param   pCtx                The X11 clipboard context to use.
 */
static void clipPeekEventAndDoXFixesHandling(PSHCLX11CTX pCtx)
{
    union
    {
        XEvent event;
        XFixesSelectionNotifyEvent fixes;
    } event = { { 0 } };

    if (XtAppPeekEvent(pCtx->pAppContext, &event.event))
    {
        if (   (event.event.type == pCtx->fixesEventBase)
            && (event.fixes.owner != XtWindow(pCtx->pWidget)))
        {
            if (   (event.fixes.subtype == 0  /* XFixesSetSelectionOwnerNotify */)
                && (event.fixes.owner != 0))
                clipQueryX11Targets(pCtx);
            else
                clipReportEmpty(pCtx);
        }
    }
}

/**
 * The main loop of our X11 event thread.
 *
 * @returns VBox status code.
 * @param   hThreadSelf             Associated thread handle.
 * @param   pvUser                  Pointer to the X11 clipboard context to use.
 */
static DECLCALLBACK(int) clipThreadMain(RTTHREAD hThreadSelf, void *pvUser)
{
    PSHCLX11CTX pCtx = (PSHCLX11CTX)pvUser;
    AssertPtr(pCtx);

    LogFlowFunc(("pCtx=%p\n", pCtx));

    bool fSignalled = false; /* Whether we have signalled the parent already or not. */

    int rc = clipInitInternal(pCtx);
    if (RT_SUCCESS(rc))
    {
        rc = clipRegisterContext(pCtx);
        if (RT_SUCCESS(rc))
        {
            if (pCtx->fGrabClipboardOnStart)
                clipQueryX11Targets(pCtx);

            pCtx->fThreadStarted = true;

            /* We're now ready to run, tell parent. */
            int rc2 = RTThreadUserSignal(hThreadSelf);
            AssertRC(rc2);

            fSignalled = true;

            while (XtAppGetExitFlag(pCtx->pAppContext) == FALSE)
            {
                clipPeekEventAndDoXFixesHandling(pCtx);
                XtAppProcessEvent(pCtx->pAppContext, XtIMAll);
            }

            LogRel(("Shared Clipboard: X11 event thread exiting\n"));

            clipUnregisterContext(pCtx);
        }
        else
        {
            LogRel(("Shared Clipboard: unable to register clip context: %Rrc\n", rc));
        }

        clipUninitInternal(pCtx);
    }

    if (!fSignalled) /* Signal parent if we didn't do so yet. */
    {
        int rc2 = RTThreadUserSignal(hThreadSelf);
        AssertRC(rc2);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Worker function for stopping the clipboard which runs on the event
 * thread.
 *
 * @param   pvUserData          Pointer to the X11 clipboard context to use.
 */
static void clipThreadSignalStop(void *pvUserData, void *)
{
    PSHCLX11CTX pCtx = (PSHCLX11CTX)pvUserData;

    /* This might mean that we are getting stopped twice. */
    Assert(pCtx->pWidget != NULL);

    /* Set the termination flag to tell the Xt event loop to exit.  We
     * reiterate that any outstanding requests from the X11 event loop to
     * the VBox part *must* have returned before we do this. */
    XtAppSetExitFlag(pCtx->pAppContext);
}

/**
 * Sets up the XFixes library and load the XFixesSelectSelectionInput symbol.
 */
static int clipLoadXFixes(Display *pDisplay, PSHCLX11CTX pCtx)
{
    int rc;

    void *hFixesLib = dlopen("libXfixes.so.1", RTLD_LAZY);
    if (!hFixesLib)
        hFixesLib = dlopen("libXfixes.so.2", RTLD_LAZY);
    if (!hFixesLib)
        hFixesLib = dlopen("libXfixes.so.3", RTLD_LAZY);
    if (!hFixesLib)
        hFixesLib = dlopen("libXfixes.so.4", RTLD_LAZY);
    if (hFixesLib)
    {
        /* For us, a NULL function pointer is a failure */
        pCtx->fixesSelectInput = (void (*)(Display *, Window, Atom, long unsigned int))
                                 (uintptr_t)dlsym(hFixesLib, "XFixesSelectSelectionInput");
        if (pCtx->fixesSelectInput)
        {
            int dummy1 = 0;
            int dummy2 = 0;
            if (XQueryExtension(pDisplay, "XFIXES", &dummy1, &pCtx->fixesEventBase, &dummy2) != 0)
            {
                if (pCtx->fixesEventBase >= 0)
                {
                    rc = VINF_SUCCESS;
                }
                else
                {
                    LogRel(("Shared Clipboard: fixesEventBase is less than zero: %d\n", pCtx->fixesEventBase));
                    rc = VERR_NOT_SUPPORTED;
                }
            }
            else
            {
                LogRel(("Shared Clipboard: XQueryExtension failed\n"));
                rc = VERR_NOT_SUPPORTED;
            }
        }
        else
        {
            LogRel(("Shared Clipboard: Symbol XFixesSelectSelectionInput not found!\n"));
            rc = VERR_NOT_SUPPORTED;
        }
    }
    else
    {
        LogRel(("Shared Clipboard: libxFixes.so.* not found!\n"));
        rc = VERR_NOT_SUPPORTED;
    }
    return rc;
}

/**
 * This is the callback which is scheduled when data is available on the
 * wakeup pipe.  It simply reads all data from the pipe.
 *
 * @param   pvUserData          Pointer to the X11 clipboard context to use.
 */
static void clipThreadDrainWakeupPipe(XtPointer pvUserData, int *, XtInputId *)
{
    LogFlowFuncEnter();

    PSHCLX11CTX pCtx = (PSHCLX11CTX)pvUserData;
    char acBuf[WAKE_UP_STRING_LEN];

    while (read(pCtx->wakeupPipeRead, acBuf, sizeof(acBuf)) > 0) {}
}
#endif /* !TESTCASE */

/**
 * X11-specific initialisation for the Shared Clipboard.
 *
 * Note: Must be called from the thread serving the Xt stuff.
 *
 * @returns VBox status code.
 * @param   pCtx                The X11 clipboard context to init.
 */
static int clipInitInternal(PSHCLX11CTX pCtx)
{
    LogFlowFunc(("pCtx=%p\n", pCtx));

    /* Make sure we are thread safe. */
    XtToolkitThreadInitialize();

    /*
     * Set up the Clipboard application context and main window.  We call all
     * these functions directly instead of calling XtOpenApplication() so
     * that we can fail gracefully if we can't get an X11 display.
     */
    XtToolkitInitialize();

    int rc = VINF_SUCCESS;

    Assert(pCtx->pAppContext == NULL); /* No nested initialization. */
    pCtx->pAppContext = XtCreateApplicationContext();
    if (pCtx->pAppContext == NULL)
    {
        LogRel(("Shared Clipboard: Failed to create Xt application context\n"));
        return VERR_NOT_SUPPORTED; /** @todo Fudge! */
    }

    /* Create a window and make it a clipboard viewer. */
    int      cArgc  = 0;
    char    *pcArgv = 0;
    Display *pDisplay = XtOpenDisplay(pCtx->pAppContext, 0, 0, "VBoxShCl", 0, 0, &cArgc, &pcArgv);
    if (pDisplay == NULL)
    {
        LogRel(("Shared Clipboard: Failed to connect to the X11 clipboard - the window system may not be running\n"));
        rc = VERR_NOT_SUPPORTED;
    }

#ifndef TESTCASE
    if (RT_SUCCESS(rc))
    {
        rc = clipLoadXFixes(pDisplay, pCtx);
        if (RT_FAILURE(rc))
           LogRel(("Shared Clipboard: Failed to load the XFIXES extension\n"));
    }
#endif

    if (RT_SUCCESS(rc))
    {
        pCtx->pWidget = XtVaAppCreateShell(0, "VBoxShCl",
                                           applicationShellWidgetClass,
                                           pDisplay,
                                           XtNwidth, 1, XtNheight, 1,
                                           NULL);
        if (pCtx->pWidget == NULL)
        {
            LogRel(("Shared Clipboard: Failed to create Xt app shell\n"));
            rc = VERR_NO_MEMORY; /** @todo r=andy Improve this. */
        }
        else
        {
#ifndef TESTCASE
            if (!XtAppAddInput(pCtx->pAppContext, pCtx->wakeupPipeRead,
                               (XtPointer) XtInputReadMask,
                               clipThreadDrainWakeupPipe, (XtPointer) pCtx))
            {
                LogRel(("Shared Clipboard: Failed to add input to Xt app context\n"));
                rc = VERR_ACCESS_DENIED; /** @todo r=andy Improve this. */
            }
#endif
        }
    }

    if (RT_SUCCESS(rc))
    {
        XtSetMappedWhenManaged(pCtx->pWidget, false);
        XtRealizeWidget(pCtx->pWidget);

#ifndef TESTCASE
        /* Enable clipboard update notification. */
        pCtx->fixesSelectInput(pDisplay, XtWindow(pCtx->pWidget),
                               clipGetAtom(pCtx, "CLIPBOARD"),
                               7 /* All XFixes*Selection*NotifyMask flags */);
#endif
    }

    if (RT_FAILURE(rc))
    {
        LogRel(("Shared Clipboard: Initialisation failed: %Rrc\n", rc));
        clipUninitInternal(pCtx);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * X11-specific uninitialisation for the Shared Clipboard.
 *
 * Note: Must be called from the thread serving the Xt stuff.
 *
 * @param   pCtx                The X11 clipboard context to uninit.
 */
static void clipUninitInternal(PSHCLX11CTX pCtx)
{
    AssertPtrReturnVoid(pCtx);

    LogFlowFunc(("pCtx=%p\n", pCtx));

    if (pCtx->pWidget)
    {
        /* Valid widget + invalid appcontext = bug.  But don't return yet. */
        AssertPtr(pCtx->pAppContext);

        XtDestroyWidget(pCtx->pWidget);
        pCtx->pWidget = NULL;
    }

    if (pCtx->pAppContext)
    {
        XtDestroyApplicationContext(pCtx->pAppContext);
        pCtx->pAppContext = NULL;
    }

    LogFlowFuncLeaveRC(VINF_SUCCESS);
}

/**
 * Sets the callback table, internal version.
 *
 * @param   pCtx                The clipboard context.
 * @param   pCallbacks          Callback table to set. If NULL, the current callback table will be cleared.
 */
static void shClX11SetCallbacksInternal(PSHCLX11CTX pCtx, PSHCLCALLBACKS pCallbacks)
{
    if (pCallbacks)
    {
        memcpy(&pCtx->Callbacks, pCallbacks, sizeof(SHCLCALLBACKS));
    }
    else
        RT_ZERO(pCtx->Callbacks);
}

/**
 * Initializes a X11 context of the Shared Clipboard.
 *
 * @returns VBox status code.
 * @param   pCtx        The clipboard context to initialize.
 * @param   pCallbacks  Callbacks to use (copied, not used directly). Optional.
 * @param   pParent     Parent context to use.
 */
int ShClX11Init(PSHCLX11CTX pCtx, PSHCLCALLBACKS pCallbacks, PSHCLCONTEXT pParent)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    LogFlowFunc(("pCtx=%p\n", pCtx));

    RT_BZERO(pCtx, sizeof(SHCLX11CTX));
    pCtx->wakeupPipeRead  = -1;
    pCtx->wakeupPipeWrite = -1;

    /* Init clipboard cache. */
    ShClCacheInit(&pCtx->Cache);

    /* Install given callbacks. */
    shClX11SetCallbacksInternal(pCtx, pCallbacks);

    pCtx->pFrontend = pParent;

#ifdef VBOX_WITH_SHARED_CLIPBOARD_XT_BUSY
    pCtx->fXtBusy        = false;
    pCtx->fXtNeedsUpdate = false;
#endif

    int rc = VINF_SUCCESS;

    LogRel(("Shared Clipboard: Initializing X11 clipboard\n"));

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
    rc = ShClTransferHttpServerInit(&pCtx->HttpCtx.HttpServer);
#endif

#ifdef TESTCASE
    if (RT_SUCCESS(rc))
    {
        /** @todo The testcases currently do not utilize the threading code. So init stuff here. */
        rc = clipInitInternal(pCtx);
        if (RT_SUCCESS(rc))
            rc = clipRegisterContext(pCtx);
    }
#endif

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Initializing X11 clipboard failed with %Rrc\n", rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Terminates (uninitializes) a Shared Clipboard X11 context.
 *
 * @returns VBox status code.
 * @param   pCtx                The X11 clipboard context to destroy.
 */
int ShClX11Term(PSHCLX11CTX pCtx)
{
    if (!pCtx)
        return VINF_SUCCESS;

    LogFlowFunc(("pCtx=%p\n", pCtx));

    /* Delete the clipboard cache. */
    ShClCacheTerm(&pCtx->Cache);

    int rc = VINF_SUCCESS;
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS_HTTP
    rc = ShClTransferHttpServerDestroy(&pCtx->HttpCtx.HttpServer);
#endif

#ifdef TESTCASE
    /** @todo The testcases currently do not utilize the threading code. So uninit stuff here. */
    clipUnregisterContext(pCtx);
    clipUninitInternal(pCtx);
#endif

    /* We set this to NULL when the event thread exits.  It really should
     * have exited at this point, when we are about to unload the code from
     * memory. */
    AssertStmt(pCtx->pWidget == NULL, rc = VERR_WRONG_ORDER);

    return rc;
}

#ifndef TESTCASE
/** Closes the wakeup pipe created for the X11 event thread. */
static void clipThreadCloseWakeupPipe(PSHCLX11CTX pCtx)
{
    if (pCtx->wakeupPipeRead >= 0)
    {
        close(pCtx->wakeupPipeRead);
        pCtx->wakeupPipeRead = -1;
    }

    if (pCtx->wakeupPipeWrite >= 0)
    {
        close(pCtx->wakeupPipeWrite);
        pCtx->wakeupPipeWrite = -1;
    }
}

/**
 * Starts our own Xt even thread for handling Shared Clipboard messages, extended version.
 *
 * @returns VBox status code.
 * @param   pCtx                The X11 clipboard context to use.
 * @param   pszName             Thread name to use.
 * @param   fGrab               Whether we should try to grab the shared clipboard at once.
 */
int ShClX11ThreadStartEx(PSHCLX11CTX pCtx, const char *pszName, bool fGrab)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);

    pCtx->Thread = NIL_RTTHREAD;
    pCtx->fThreadStarted = false;
    pCtx->fGrabClipboardOnStart = fGrab;

    clipResetX11Formats(pCtx);

    int rc;

    /*
     * Create the pipes.
     ** @todo r=andy Replace this with RTPipe API.
     */
    int pipes[2];
    if (!pipe(pipes))
    {
        pCtx->wakeupPipeRead  = pipes[0];
        pCtx->wakeupPipeWrite = pipes[1];

        if (   !fcntl(pCtx->wakeupPipeRead,  F_SETFL, O_NONBLOCK)
            && !fcntl(pCtx->wakeupPipeWrite, F_SETFL, O_NONBLOCK))
        {
            rc = VINF_SUCCESS;
        }
        else
        {
            rc = RTErrConvertFromErrno(errno);
            clipThreadCloseWakeupPipe(pCtx);
        }
    }
    else
        rc = RTErrConvertFromErrno(errno);

    if (RT_SUCCESS(rc))
    {
        LogRel2(("Shared Clipboard: Starting X11 event thread ...\n"));

        rc = RTThreadCreate(&pCtx->Thread, clipThreadMain, pCtx, 0,
                            RTTHREADTYPE_IO, RTTHREADFLAGS_WAITABLE, pszName);
        if (RT_SUCCESS(rc))
        {
            /* The backend must not release pCtx while the worker initializes it. */
            rc = RTThreadUserWait(pCtx->Thread, RT_INDEFINITE_WAIT);
            AssertFatalMsgRC(rc, ("Waiting for X11 event thread startup failed with %Rrc\n", rc));
        }
        else
            clipThreadCloseWakeupPipe(pCtx);

        if (RT_FAILURE(rc))
        {
            LogRel(("Shared Clipboard: Failed to start the X11 event thread with %Rrc\n", rc));
            clipUninitInternal(pCtx);
        }
        else
        {
            if (!pCtx->fThreadStarted)
            {
                /* The worker signalled its terminal startup failure; reap it before returning. */
                int rcThread = VERR_IPE_UNINITIALIZED_STATUS;
                int rc2 = RTThreadWait(pCtx->Thread, RT_INDEFINITE_WAIT, &rcThread);
                AssertFatalMsgRC(rc2, ("Reaping X11 event thread after startup failure failed with %Rrc\n", rc2));
                pCtx->Thread = NIL_RTTHREAD;
                rc = RT_FAILURE(rcThread) ? rcThread : VERR_GENERAL_FAILURE;

                clipThreadCloseWakeupPipe(pCtx);
                LogRel(("Shared Clipboard: X11 event thread reported an error while starting: %Rrc\n", rc));
            }
            else
                LogRel2(("Shared Clipboard: X11 event thread started\n"));
        }
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Starts our own Xt even thread for handling Shared Clipboard messages.
 *
 * @returns VBox status code.
 * @param   pCtx                The X11 clipboard context to use.
 * @param   fGrab               Whether we should try to grab the shared clipboard at once.
 */
int ShClX11ThreadStart(PSHCLX11CTX pCtx, bool fGrab)
{
    return ShClX11ThreadStartEx(pCtx, "SHCLX11", fGrab);
}

/**
 * Stops the Shared Clipboard Xt even thread.
 *
 * @note  Any requests from this object to get clipboard data from VBox
 *        *must* have completed or aborted before we are called, as
 *        otherwise the X11 event loop will still be waiting for the request
 *        to return and will not be able to terminate.
 *
 * @returns VBox status code.
 * @param   pCtx                The X11 clipboard context to use.
 */
int ShClX11ThreadStop(PSHCLX11CTX pCtx)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertReturn(pCtx->Thread != NIL_RTTHREAD, VERR_INVALID_STATE);

    int rcThread = VERR_IPE_UNINITIALIZED_STATUS;
    int rcWait = RTThreadWait(pCtx->Thread, 0 /* cMillies */, &rcThread);
    if (RT_SUCCESS(rcWait))
    {
        pCtx->Thread = NIL_RTTHREAD;
        pCtx->fThreadStarted = false;
        clipThreadCloseWakeupPipe(pCtx);
        return rcThread;
    }
    AssertFatalMsg(rcWait == VERR_TIMEOUT, ("Probing X11 event thread failed with %Rrc\n", rcWait));

    LogRel2(("Shared Clipboard: Signalling the X11 event thread to stop\n"));

    /* Write to the "stop" pipe. */
    int const rcSignal = clipThreadScheduleCall(pCtx, clipThreadSignalStop, (XtPointer)pCtx);
    AssertFatalMsgRC(rcSignal, ("Cannot notify X11 event thread on shutdown with %Rrc\n", rcSignal));

    LogRel2(("Shared Clipboard: Waiting for X11 event thread to stop ...\n"));

    rcWait = RTThreadWait(pCtx->Thread, RT_MS_30SEC /* msTimeout */, &rcThread);
    if (RT_FAILURE(rcWait))
    {
        LogRel(("Shared Clipboard: X11 event thread did not stop promptly (%Rrc); waiting indefinitely\n", rcWait));
        rcWait = RTThreadWait(pCtx->Thread, RT_INDEFINITE_WAIT, &rcThread);
    }
    AssertFatalMsgRC(rcWait, ("Reaping X11 event thread failed with %Rrc\n", rcWait));

    pCtx->Thread = NIL_RTTHREAD;
    pCtx->fThreadStarted = false;
    clipThreadCloseWakeupPipe(pCtx);

    int const rc = rcThread;
    if (RT_SUCCESS(rc))
        LogRel2(("Shared Clipboard: X11 event thread stopped successfully\n"));
    else
        LogRel(("Shared Clipboard: Stopping X11 event thread failed with %Rrc\n", rc));

    LogFlowFuncLeaveRC(rc);
    return rc;
}
#endif /* !TESTCASE */

/**
 * Returns the targets supported by VBox.
 *
 * This will return a list of atoms which tells the caller
 * what kind of clipboard formats we support.
 *
 * @returns VBox status code.
 * @param  pCtx                 The X11 clipboard context to use.
 * @param  atomTypeReturn       The type of the data we are returning.
 * @param  pValReturn           A pointer to the data we are returning. This
 *                              should be set to memory allocated by XtMalloc,
 *                              which will be freed later by the Xt toolkit.
 * @param  pcLenReturn          The length of the data we are returning.
 * @param  piFormatReturn       The format (8bit, 16bit, 32bit) of the data we are
 *                              returning.
 * @note  X11 backend code, called by the XtOwnSelection callback.
 */
static int clipCreateX11Targets(PSHCLX11CTX pCtx, Atom *atomTypeReturn,
                                XtPointer *pValReturn,
                                unsigned long *pcLenReturn,
                                int *piFormatReturn)
{
    const unsigned cFixedTargets = 3; /* See below. */

    Atom *pAtomTargets = (Atom *)XtMalloc((SHCL_MAX_X11_FORMATS + cFixedTargets) * sizeof(Atom));
    if (!pAtomTargets)
        return VERR_NO_MEMORY;

    unsigned cTargets = 0;
    SHCLX11FMTIDX idxFmt = NIL_CLIPX11FORMAT;
    do
    {
        idxFmt = clipEnumX11Formats(pCtx->vboxFormats, idxFmt);
        if (idxFmt != NIL_CLIPX11FORMAT)
        {
            pAtomTargets[cTargets] = clipAtomForX11Format(pCtx, idxFmt);
            ++cTargets;
        }
    } while (idxFmt != NIL_CLIPX11FORMAT);

    /* We always offer these fixed targets. */
    pAtomTargets[cTargets]     = clipGetAtom(pCtx, "TARGETS");
    pAtomTargets[cTargets + 1] = clipGetAtom(pCtx, "MULTIPLE");
    pAtomTargets[cTargets + 2] = clipGetAtom(pCtx, "TIMESTAMP");

    *atomTypeReturn = XA_ATOM;
    *pValReturn = (XtPointer)pAtomTargets;
    *pcLenReturn = cTargets + cFixedTargets;
    *piFormatReturn = 32;

    LogFlowFunc(("cTargets=%u\n", cTargets + cFixedTargets));

    return VINF_SUCCESS;
}

/**
 * Helper for clipConvertToX11Data() that will cache the data returned.
 *
 * @returns VBox status code. VERR_SHCLPB_NO_DATA if no data available.
 * @param   pCtx                The X11 clipboard context to use.
 * @param   uFmt                Clipboard format to read data in.
 * @param   ppv                 Returns an allocated buffer with data read on success.
 *                              Needs to be free'd with RTMemFree() by the caller.
 * @param   pcb                 Returns the amount of data read (in bytes) on success.
 *
 * @thread  X11 event thread.
 */
static int shClX11RequestDataForX11CallbackHelper(PSHCLX11CTX pCtx, SHCLFORMAT uFmt,
                                                  void **ppv, uint32_t *pcb)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(ppv,  VERR_INVALID_POINTER);
    AssertPtrReturn(pcb,  VERR_INVALID_POINTER);

    if (LogRelIs2Enabled())
    {
        char *pszFmts = ShClFormatsToStrA(uFmt);
        LogRel2(("Shared Clipboard: Requesting data for X11 from source as %#x/'%s'\n",
                 uFmt, pszFmts ? pszFmts : "<alloc failed>"));
        RTStrFree(pszFmts);
    }

    int      rc;
    void    *pv = NULL;
    uint32_t cb = 0;

    PSHCLCACHEENTRY pCacheEntry = ShClCacheGet(&pCtx->Cache, uFmt);
    if (!pCacheEntry) /* Cache miss */
    {
        if (pCtx->fCacheOnlyFormats & uFmt)
            rc = VERR_SHCLPB_NO_DATA;
        else
        {
            AssertPtrReturn(pCtx->Callbacks.pfnOnRequestDataFromSource, VERR_INVALID_POINTER);
            rc = pCtx->Callbacks.pfnOnRequestDataFromSource(pCtx->pFrontend, uFmt, &pv, &cb,
                                                            NULL /* pvUser */);
            if (RT_SUCCESS(rc))
            {
                rc = ShClCacheSet(&pCtx->Cache, uFmt, pv, cb);
                /** @todo r=bird: Leaks pv/cb on ShClCacheSet error? */
            }
        }
    }
    else /* Cache hit */
    {
        Assert(pCacheEntry->pvData && pCacheEntry->cbData /* (ShClCacheGet already checks this) */);
        cb = pCacheEntry->cbData;
        pv = RTMemDup(pCacheEntry->pvData, cb);
        if (pv)
            rc = VINF_SUCCESS;
        else
        {
            cb = 0;
            rc = VERR_NO_MEMORY;
        }
    }

    LogFlowFunc(("pCtx=%p, uFmt=%#x -> Cache %s\n", pCtx, uFmt, pCacheEntry ? "HIT" : "MISS"));

    if (RT_SUCCESS(rc))
    {
       /* Safey net in case the stuff above misbehaves (must return VERR_SHCLPB_NO_DATA if no data available). */
        if (pv != NULL && cb > 0)
        {
            *ppv = pv;
            *pcb = cb;
        }
        else
            rc = VERR_SHCLPB_NO_DATA;
    }
    else
        LogRel(("Shared Clipboard: Requesting data for X11 from source failed with %Rrc\n", rc));

    LogFlowFunc(("Returning pv=%p, cb=%RU32, rc=%Rrc\n", pv, cb, rc));
    return rc;
}

/**
 * Free's an allocated SHCLX11RESPONSE struct.
 *
 * @param   pResp               Pointer to response to free.
 *                              The pointer will be invalid after return.
 */
static void shClX11ResponseFree(PSHCLX11RESPONSE pResp)
{
    if (!pResp)
        return;

    switch (pResp->enmType)
    {
        case SHCLX11EVENTTYPE_READ:
        {
            if (pResp->Read.pvData)
            {
                Assert(pResp->Read.cbData);
                RTMemFree(pResp->Read.pvData);
            }
            break;
        }

        case SHCLX11EVENTTYPE_REPORT_FORMATS:
            RT_FALL_THROUGH();
        case SHCLX11EVENTTYPE_WRITE:
            RT_FALL_THROUGH();
        default:
            break;
    }

    RTMemFree(pResp);
}

/**
 * Satisfies a request from X11 to convert the clipboard text to UTF-8 LF.
 *
 * @returns VBox status code. VERR_SHCLPB_NO_DATA if no data was converted.
 * @param  pDisplay             An X11 display structure, needed for conversions
 *                              performed by Xlib.
 * @param  pv                   The text to be converted (UCS-2 with Windows EOLs).
 * @param  cb                   The length of the text in @cb in bytes.
 * @param  atomTypeReturn       Where to store the atom for the type of the data
 *                              we are returning.
 * @param  pValReturn           Where to store the pointer to the data we are
 *                              returning.  This should be to memory allocated by
 *                              XtMalloc, which will be freed by the Xt toolkit
 *                              later.
 * @param  pcLenReturn          Where to store the length of the data we are
 *                              returning.
 * @param  piFormatReturn       Where to store the bit width (8, 16, 32) of the
 *                              data we are returning.
 */
static int clipConvertUtf16ToX11Data(Display *pDisplay, PRTUTF16 pwszSrc,
                                     size_t cbSrc, Atom *atomTarget,
                                     Atom *atomTypeReturn,
                                     XtPointer *pValReturn,
                                     unsigned long *pcLenReturn,
                                     int *piFormatReturn)
{
    RT_NOREF(pDisplay);
    AssertReturn(cbSrc % sizeof(RTUTF16) == 0, VERR_INVALID_PARAMETER);

    const size_t cwcSrc = cbSrc / sizeof(RTUTF16);
    if (!cwcSrc)
        return VERR_SHCLPB_NO_DATA;

    /* This may slightly overestimate the space needed. */
    size_t cbDst = 0;
    int rc = ShClHlpUtf16LenUtf8(pwszSrc, cwcSrc, &cbDst);
    if (RT_SUCCESS(rc))
    {
        cbDst++; /* Add space for terminator. */

        char *pszDst = (char *)XtMalloc(cbDst);
        if (pszDst)
        {
            size_t cbActual = 0;
            rc = ShClHlpConvUtf16CRLFToUtf8LF(pwszSrc, cwcSrc, pszDst, cbDst, &cbActual);
            if (RT_SUCCESS(rc))
            {
                *atomTypeReturn = *atomTarget;
                *pValReturn     = (XtPointer)pszDst;
                *pcLenReturn    = cbActual + 1 /* Include terminator */;
                *piFormatReturn = 8;
            }
            else
                XtFree(pszDst);
        }
        else
            rc = VERR_NO_MEMORY;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Satisfies a request from X11 to convert the clipboard HTML fragment to UTF-8.  We
 * return null-terminated text, but can cope with non-null-terminated input.
 *
 * @returns VBox status code.
 * @param  pDisplay             An X11 display structure, needed for conversions
 *                              performed by Xlib.
 * @param  pv                   The text to be converted (UTF8 with Windows EOLs).
 * @param  cb                   The length of the text in @cb in bytes.
 * @param  atomTypeReturn       Where to store the atom for the type of the data
 *                              we are returning.
 * @param  pValReturn           Where to store the pointer to the data we are
 *                              returning.  This should be to memory allocated by
 *                              XtMalloc, which will be freed by the Xt toolkit later.
 * @param  pcLenReturn          Where to store the length of the data we are returning.
 * @param  piFormatReturn       Where to store the bit width (8, 16, 32) of the
 *                              data we are returning.
 */
static int clipConvertHtmlToX11Data(Display *pDisplay, const char *pszSrc,
                                    size_t cbSrc, Atom *atomTarget,
                                    Atom *atomTypeReturn,
                                    XtPointer *pValReturn,
                                    unsigned long *pcLenReturn,
                                    int *piFormatReturn)
{
    RT_NOREF(pDisplay, pValReturn);

    /* This may slightly overestimate the space needed. */
    LogFlowFunc(("Source: %s", pszSrc));

    char *pszDest = (char *)XtMalloc(cbSrc);
    if (pszDest == NULL)
        return VERR_NO_MEMORY;

    memcpy(pszDest, pszSrc, cbSrc);

    *atomTypeReturn = *atomTarget;
    *pValReturn = (XtPointer)pszDest;
    *pcLenReturn = cbSrc;
    *piFormatReturn = 8;

    return VINF_SUCCESS;
}


/**
 * Does this atom correspond to one of the two selection types we support?
 *
 * @param  pCtx                 The X11 clipboard context to use.
 * @param  selType              The atom in question.
 */
static bool clipIsSupportedSelectionType(PSHCLX11CTX pCtx, Atom selType)
{
    return(   (selType == clipGetAtom(pCtx, "CLIPBOARD"))
           || (selType == clipGetAtom(pCtx, "PRIMARY")));
}

/**
 * Removes a trailing nul character from a string by adjusting the string
 * length.  Some X11 applications don't like zero-terminated text...
 *
 * @param  pText                The text in question.
 * @param  pcText               The length of the text, adjusted on return.
 * @param  format               The format of the text.
 */
static void clipTrimTrailingNul(XtPointer pText, unsigned long *pcText,
                                SHCLX11FMT format)
{
    AssertPtrReturnVoid(pText);
    AssertPtrReturnVoid(pcText);
    AssertReturnVoid((format == SHCLX11FMT_UTF8) || (format == SHCLX11FMT_TEXT) || (format == SHCLX11FMT_HTML));

    if (((char *)pText)[*pcText - 1] == '\0')
       --(*pcText);
}

static int clipConvertToX11Data(PSHCLX11CTX pCtx, Atom *atomTarget,
                                Atom *atomTypeReturn,
                                XtPointer *pValReturn,
                                unsigned long *pcLenReturn,
                                int *piFormatReturn)
{
    int rc = VERR_NOT_SUPPORTED; /* Play safe by default. */

    SHCLX11FMTIDX idxFmtX11 = clipFindX11FormatByAtom(pCtx, *atomTarget);
    SHCLX11FMT    fmtX11    = clipRealFormatForX11Format(idxFmtX11);

    LogFlowFunc(("vboxFormats=0x%x, idxFmtX11=%u ('%s'), fmtX11=%u\n",
                 pCtx->vboxFormats, idxFmtX11, g_aFormats[idxFmtX11].pcszAtom, fmtX11));

    if (LogRelIs2Enabled())
    {
        char *pszFmts = ShClFormatsToStrA(pCtx->vboxFormats);
        LogRel2(("Shared Clipboard: Converting VBox formats %#x/'%s' to '%s' for X11\n", pCtx->vboxFormats,
                 pszFmts ? pszFmts : "<alloc failed>", fmtX11 == SHCLX11FMT_INVALID ? "<invalid>" : g_aFormats[idxFmtX11].pcszAtom));
        RTStrFree(pszFmts);
    }

    void    *pv = NULL;
    uint32_t cb = 0;

    if (   (   (fmtX11 == SHCLX11FMT_UTF8)
            || (fmtX11 == SHCLX11FMT_TEXT)
           )
        && (pCtx->vboxFormats & VBOX_SHCL_FMT_UNICODETEXT))
    {
        rc = shClX11RequestDataForX11CallbackHelper(pCtx, VBOX_SHCL_FMT_UNICODETEXT, &pv, &cb);
        if (   RT_SUCCESS(rc)
            && (   (fmtX11 == SHCLX11FMT_UTF8)
                || (fmtX11 == SHCLX11FMT_TEXT)))
        {
            rc = clipConvertUtf16ToX11Data(XtDisplay(pCtx->pWidget),
                                           (PRTUTF16)pv, cb, atomTarget,
                                           atomTypeReturn, pValReturn,
                                           pcLenReturn, piFormatReturn);
        }

        if (RT_SUCCESS(rc))
            clipTrimTrailingNul(*(XtPointer *)pValReturn, pcLenReturn, fmtX11);

        RTMemFree(pv);
    }
    else if (   (fmtX11 == SHCLX11FMT_BMP)
             && (pCtx->vboxFormats & VBOX_SHCL_FMT_BITMAP))
    {
        rc = shClX11RequestDataForX11CallbackHelper(pCtx, VBOX_SHCL_FMT_BITMAP, &pv, &cb);
        if (RT_SUCCESS(rc))
        {
            /* Create a full BMP from it. */
            rc = ShClHlpDibToBmp(pv, cb, (void **)pValReturn,
                                  (size_t *)pcLenReturn);
        }

        if (RT_SUCCESS(rc))
        {
            *atomTypeReturn = *atomTarget;
            *piFormatReturn = 8;
        }

        RTMemFree(pv);
    }
    else if (  (fmtX11 == SHCLX11FMT_HTML)
            && (pCtx->vboxFormats & VBOX_SHCL_FMT_HTML))
    {
        rc = shClX11RequestDataForX11CallbackHelper(pCtx, VBOX_SHCL_FMT_HTML, &pv, &cb);
        if (RT_SUCCESS(rc))
        {
            /**
             * The common VBox HTML encoding will be UTF-8.
             * Before sending it to the X11 clipboard we have to convert it to UTF-8 first.
             *
             * Strange that we get UTF-16 from the X11 clipboard, but
             * in same time we send UTF-8 to X11 clipboard and it works.
             ** @todo r=andy Verify this.
             */
            rc = clipConvertHtmlToX11Data(XtDisplay(pCtx->pWidget),
                                          (const char*)pv, cb, atomTarget,
                                          atomTypeReturn, pValReturn,
                                          pcLenReturn, piFormatReturn);
            if (RT_SUCCESS(rc))
                clipTrimTrailingNul(*(XtPointer *)pValReturn, pcLenReturn, fmtX11);

            RTMemFree(pv);
        }
    }
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    else if (shClX11TransferTargetSupportsDirection(fmtX11, SHCLX11TRANSFERDIR_F_TO_X11))
    {
        if (pCtx->vboxFormats & VBOX_SHCL_FMT_URI_LIST)
        {
            rc = shClX11RequestDataForX11CallbackHelper(pCtx, VBOX_SHCL_FMT_URI_LIST, &pv, &cb);
            if (RT_SUCCESS(rc))
            {
                void  *pvX11;
                size_t cbX11;
                rc = ShClX11TransferConvertToX11((const char *)pv, cb, fmtX11, &pvX11, &cbX11);
                if (RT_SUCCESS(rc))
                {
                    *atomTypeReturn = *atomTarget;
                    *pValReturn     = (XtPointer)pvX11;
                    *pcLenReturn    = cbX11;
                    *piFormatReturn = 8;
                }
            }

            RTMemFree(pv);
            pv = NULL;
        }
        /* else not supported yet. */
    }
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
    else
    {
        *atomTypeReturn = XT_CONVERT_FAIL;
        *pValReturn     = (XtPointer)NULL;
        *pcLenReturn    = 0;
        *piFormatReturn = 0;
    }

    if (   RT_FAILURE(rc)
        && rc != VERR_SHCLPB_NO_DATA
        && LogRelIsEnabled())
    {
        char *pszFmts2 = ShClFormatsToStrA(pCtx->vboxFormats);
        char *pszAtomName = XGetAtomName(XtDisplay(pCtx->pWidget), *atomTarget);
        LogRel(("Shared Clipboard: Converting VBox formats '%s' to '%s' for X11 (idxFmtX11=%u, fmtX11=%u, atomTarget='%s') failed, rc=%Rrc\n",
                pszFmts2 ? pszFmts2 : "<alloc error>", g_aFormats[idxFmtX11].pcszAtom, idxFmtX11, fmtX11, pszAtomName ? pszAtomName : "unknown", rc));
        RTStrFree(pszFmts2);
        if (pszAtomName)
            XFree(pszAtomName);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Returns VBox's clipboard data for an X11 client.
 *
 * @note Callback for XtOwnSelection.
 */
static Boolean clipXtConvertSelectionProc(Widget widget, Atom *atomSelection,
                                          Atom *atomTarget,
                                          Atom *atomTypeReturn,
                                          XtPointer *pValReturn,
                                          unsigned long *pcLenReturn,
                                          int *piFormatReturn)
{
    LogFlowFuncEnter();

    PSHCLX11CTX pCtx = clipLookupContext(widget);
    if (!pCtx)
        return False;

    /* Is this the rigt selection (clipboard) we were asked for? */
    if (!clipIsSupportedSelectionType(pCtx, *atomSelection))
        return False;

    int rc;
    if (*atomTarget == clipGetAtom(pCtx, "TARGETS"))
        rc = clipCreateX11Targets(pCtx, atomTypeReturn, pValReturn,
                                  pcLenReturn, piFormatReturn);
    else
        rc = clipConvertToX11Data(pCtx, atomTarget, atomTypeReturn,
                                  pValReturn, pcLenReturn, piFormatReturn);

#if 0 /** @todo Disabled -- crashes when running with tstClipboardGH-X11. */
    XSelectionRequestEvent* pReq =
        XtGetSelectionRequest(widget, *atomSelection, (XtRequestId)NULL);
    LogFlowFunc(("returning pVBoxWnd=%#x, ownerWnd=%#x, reqWnd=%#x, %RTbool, rc=%Rrc\n",
                 XtWindow(pCtx->pWidget), pReq->owner, pReq->requestor, RT_SUCCESS(rc), rc));
#endif
    return RT_SUCCESS(rc) ? True : False;
}

static void clipXtConvertSelectionProcLose(Widget widget, Atom *atomSelection)
{
    RT_NOREF(widget, atomSelection);
    LogFlowFuncEnter();
}

static void clipXtConvertSelectionProcDone(Widget widget, Atom *atomSelection, Atom *atomTarget)
{
    RT_NOREF(widget, atomSelection, atomTarget);
    LogFlowFuncEnter();
}

/**
 * Invalidates the local clipboard cache.
 *
 * @param   pCtx                The X11 clipboard context to use.
 */
static void clipInvalidateClipboardCache(PSHCLX11CTX pCtx)
{
    LogFlowFuncEnter();

    ShClCacheInvalidate(&pCtx->Cache);
}

/**
 * Takes possession of the X11 clipboard (and middle-button selection).
 *
 * @param   pCtx                The X11 clipboard context to use.
 * @param   uFormats            Clipboard formats to set.
 */
static void clipGrabX11Clipboard(PSHCLX11CTX pCtx, SHCLFORMATS uFormats)
{
    LogFlowFuncEnter();

    /** @todo r=andy The docs say: "the value CurrentTime is not acceptable" here!? */
    if (XtOwnSelection(pCtx->pWidget, clipGetAtom(pCtx, "CLIPBOARD"),
                       CurrentTime,
                       clipXtConvertSelectionProc, clipXtConvertSelectionProcLose, clipXtConvertSelectionProcDone))
    {
        pCtx->vboxFormats = uFormats;

        /* Grab the middle-button paste selection too. */
        XtOwnSelection(pCtx->pWidget, clipGetAtom(pCtx, "PRIMARY"),
                       CurrentTime, clipXtConvertSelectionProc, NULL, 0);
#ifndef TESTCASE
        /* Xt suppresses these if we already own the clipboard, so send them
         * ourselves. */
        XSetSelectionOwner(XtDisplay(pCtx->pWidget),
                           clipGetAtom(pCtx, "CLIPBOARD"),
                           XtWindow(pCtx->pWidget), CurrentTime);
        XSetSelectionOwner(XtDisplay(pCtx->pWidget),
                           clipGetAtom(pCtx, "PRIMARY"),
                           XtWindow(pCtx->pWidget), CurrentTime);
#endif
    }
}

/**
 * Worker function for ShClX11ReportFormatsToX11Async.
 *
 * @param  pvUserData           Pointer to a PSHCLX11REQUEST structure containing
 *                              information about the VBox formats available and the
 *                              clipboard context data.  Must be free'd by the worker.
 *
 * @thread X11 event thread.
 */
static void shClX11ReportFormatsToX11Worker(void *pvUserData, void * /* interval */)
{
    AssertPtrReturnVoid(pvUserData);

    PSHCLX11REQUEST pReq = (PSHCLX11REQUEST)pvUserData;
    AssertReturnVoid(pReq->enmType == SHCLX11EVENTTYPE_REPORT_FORMATS);

    PSHCLX11CTX pCtx      = pReq->pCtx;
    SHCLFORMATS fFormats  = pReq->Formats.fFormats;
    SHCLFORMAT  uFmtCache = pReq->Formats.uFmtCache;
    void       *pvCache   = pReq->Formats.pvCache;
    uint32_t    cbCache   = pReq->Formats.cbCache;

    if (LogRelIs2Enabled())
    {
        char *pszFmts = ShClFormatsToStrA(fFormats);
        LogRel2(("Shared Clipboard: Reported available VBox formats %#x/'%s' to X11\n",
                 fFormats, pszFmts ? pszFmts : "<alloc failed>"));
        RTStrFree(pszFmts);
    }

    clipInvalidateClipboardCache(pCtx);
    pCtx->fCacheOnlyFormats = uFmtCache;

    if (uFmtCache != VBOX_SHCL_FMT_NONE)
    {
        int rc = ShClCacheSet(&pCtx->Cache, uFmtCache, pvCache, cbCache);
        if (RT_FAILURE(rc))
        {
            LogRel(("Shared Clipboard: Caching format %#x before advertising it to X11 failed with %Rrc; "
                    "suppressing the format\n", uFmtCache, rc));
            fFormats &= ~uFmtCache;
        }
    }

    RTMemFree(pvCache);
    pReq->Formats.pvCache = NULL;
    RTMemFree(pReq);

    clipGrabX11Clipboard(pCtx, fFormats);
    clipResetX11Formats(pCtx);

    LogFlowFuncLeave();
}

/**
 * Announces new clipboard formats to the X11 clipboard.
 *
 * @returns VBox status code.
 * @param   pCtx                Context data for the clipboard backend.
 * @param   fFormats            Clipboard formats offered.
 *
 * @note    When calling this function, data for the clipboard already has to be available,
 *          as we grab the clipboard, which in turn then calls the X11 data conversion callback.
 */
int ShClX11ReportFormatsToX11Async(PSHCLX11CTX pCtx, SHCLFORMATS fFormats)
{
    return ShClX11ReportFormatsToX11AsyncEx(pCtx, fFormats, VBOX_SHCL_FMT_NONE, NULL, 0);
}

/**
 * Announces new clipboard formats and atomically seeds one cache entry on the
 * X11 event thread before taking ownership of the selections.
 *
 * This is used for data which must be prepared asynchronously.  Once the
 * formats become visible to X11 clients, conversion of @a uFmtCache is
 * guaranteed to be a cache hit and cannot block on the data source.
 *
 * @returns VBox status code.
 * @param   pCtx                Context data for the clipboard backend.
 * @param   fFormats            Clipboard formats offered.
 * @param   uFmtCache           Format of @a pvCache, or VBOX_SHCL_FMT_NONE.
 * @param   pvCache             Data to cache before advertising @a fFormats.
 * @param   cbCache             Size of @a pvCache in bytes.
 *
 * @thread  Any thread.  Cache installation and selection ownership happen on
 *          the X11 event thread.
 */
int ShClX11ReportFormatsToX11AsyncEx(PSHCLX11CTX pCtx, SHCLFORMATS fFormats, SHCLFORMAT uFmtCache,
                                     const void *pvCache, uint32_t cbCache)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    if (uFmtCache != VBOX_SHCL_FMT_NONE)
    {
        AssertReturn(ShClFormatIsValid(uFmtCache), VERR_INVALID_PARAMETER);
        AssertReturn((fFormats & uFmtCache) == uFmtCache, VERR_INVALID_PARAMETER);
        AssertPtrReturn(pvCache, VERR_INVALID_POINTER);
        AssertReturn(cbCache, VERR_INVALID_PARAMETER);
    }
    else
        AssertReturn(!pvCache && !cbCache, VERR_INVALID_PARAMETER);

    int rc;

    PSHCLX11REQUEST pReq = (PSHCLX11REQUEST)RTMemAllocZ(sizeof(SHCLX11REQUEST));
    if (pReq)
    {
        pReq->enmType           = SHCLX11EVENTTYPE_REPORT_FORMATS;
        pReq->pCtx              = pCtx;
        pReq->Formats.fFormats  = fFormats;
        pReq->Formats.uFmtCache = uFmtCache;

        if (uFmtCache != VBOX_SHCL_FMT_NONE)
        {
            pReq->Formats.pvCache = RTMemDup(pvCache, cbCache);
            if (!pReq->Formats.pvCache)
            {
                RTMemFree(pReq);
                return VERR_NO_MEMORY;
            }
            pReq->Formats.cbCache = cbCache;
        }

        rc = clipThreadScheduleCall(pCtx, shClX11ReportFormatsToX11Worker, (XtPointer)pReq);
        if (RT_FAILURE(rc))
        {
            RTMemFree(pReq->Formats.pvCache);
            RTMemFree(pReq);
        }
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
/**
 * Converts transfer data to a format returned back to X11.
 *
 * @returns VBox status code.
 * @param   pszSrc              Transfer data to convert.
 * @param   cbSrc               Size of transfer data (in bytes) to convert.
 * @param   enmFmtX11           X11 format to convert data to.
 * @param   ppvDst              Where to return converted data on success. Must be free'd with XtFree().
 * @param   pcbDst              Where to return the bytes of the converted data on success. Optional.
 */
int ShClX11TransferConvertToX11(const char *pszSrc, size_t cbSrc,  SHCLX11FMT enmFmtX11, void **ppvDst, size_t *pcbDst)
{
    AssertPtrReturn(pszSrc, VERR_INVALID_POINTER);
    AssertReturn(cbSrc, VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppvDst, VERR_INVALID_POINTER);
    /* pcbDst is optional. */

    int rc = VINF_SUCCESS;

    char *pszDst = NULL;

# ifdef DEBUG_andy
    LogFlowFunc(("Src:\n%.*RhXd\n", cbSrc, pszSrc));
# endif

    switch (enmFmtX11)
    {
        case SHCLX11FMT_URI_LIST_KDE_CUTSELECTION:
        {
            /* KDE stores only cut/copy state in this target: "0" means copy
             * and "1" means cut.  Shared Clipboard exports copies, while the
             * file names themselves are provided through text/uri-list. */
            pszDst = RTStrDup("0"); /* Copy. */
            if (!pszDst)
                rc = VERR_NO_MEMORY;
            break;
        }

        case SHCLX11FMT_URI_LIST_GNOME_COPIED_FILES:
            RT_FALL_THROUGH();
        case SHCLX11FMT_URI_LIST_MATE_COPIED_FILES:
            RT_FALL_THROUGH();
        case SHCLX11FMT_URI_LIST_NAUTILUS_CLIPBOARD:
        {
            const char chSep = '\n'; /* Currently (?) all entries need to be separated by '\n'. */

            /* Note: There must be *no* final new line ('\n') at the end, otherwise Nautilus will crash! */
            pszDst = RTStrAPrintf2("copy%c%s", chSep, pszSrc);
            if (!pszDst)
                rc = VERR_NO_MEMORY;
            break;
        }

        case SHCLX11FMT_URI_LIST:
        {
            pszDst = RTStrDup(pszSrc);
            AssertPtrBreakStmt(pszDst, rc = VERR_NO_MEMORY);
            break;
        }

        default:
            AssertFailed(); /* Most likely a bug in the code; let me know. */
            break;
    }

    if (RT_SUCCESS(rc))
    {
        size_t const cbDst = RTStrNLen(pszDst, RTSTR_MAX);
        void        *pvDst = (void *)XtMalloc(cbDst);
        if (pvDst)
        {
            memcpy(pvDst, pszDst, cbDst);
# ifdef DEBUG_andy
            LogFlowFunc(("Dst:\n%.*RhXd\n", cbDst, pvDst));
# endif
        }
        else
            rc = VERR_NO_MEMORY;

        if (pcbDst)
            *pcbDst = cbDst;
        *ppvDst = pvDst;

        RTStrFree(pszDst);
        pszDst = NULL;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Checks whether a line is a file-manager action marker, rather than an URI.
 *
 * @returns true if the line is an action marker, false if not.
 * @param   pchLine             Line to check.
 * @param   cchLine             Length (in bytes) of \a pchLine.
 */
static bool shClX11TransferIsActionLine(const char *pchLine, size_t cchLine)
{
    AssertPtrReturn(pchLine, false);

    static const char *s_apszActions[] =
    {
        "copy",
        "cut",
        "move"
    };

    for (size_t i = 0; i < RT_ELEMENTS(s_apszActions); i++)
    {
        size_t const cchAction = strlen(s_apszActions[i]);
        if (   cchLine == cchAction
            && !memcmp(pchLine, s_apszActions[i], cchAction))
            return true;
    }

    return false;
}


/**
 * Appends a single transfer list entry to a CRLF-separated string list.
 *
 * @returns VBox status code.
 * @param   ppszList            String list to append to.
 * @param   pcbList             Size (in bytes) of the string list without the terminator.
 * @param   pchEntry            Entry to append.
 * @param   cchEntry            Length (in bytes) of \a pchEntry.
 */
static int shClX11TransferAppendListEntry(char **ppszList, size_t *pcbList, const char *pchEntry, size_t cchEntry)
{
    AssertPtrReturn(ppszList, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbList,  VERR_INVALID_POINTER);
    AssertPtrReturn(pchEntry, VERR_INVALID_POINTER);
    AssertReturn(cchEntry, VERR_INVALID_PARAMETER);

    size_t const cchSep = sizeof(SHCL_TRANSFER_URI_LIST_SEP_STR) - 1;
    int rc = RTStrAAppendExN(ppszList, 2 /* cPairs */, pchEntry, cchEntry, SHCL_TRANSFER_URI_LIST_SEP_STR, cchSep);
    if (RT_SUCCESS(rc))
        *pcbList += cchEntry + cchSep;

    return rc;
}


/**
 * Converts X11 data to a string list usable for transfers.
 *
 * @returns VBox status code.
 * @param   pvData              Data to convert to a string list.
 * @param   cbData              Size (in bytes) of \a pvData.
 * @param   ppszList            Where to return the allocated string list on success.
 *                              Must be free'd with RTStrFree().
 * @param   pcbList             Size (in bytes) of the returned string list on success.
 *                              Includes terminator.
 */
int ShClX11TransferConvertFromX11(const char *pvData, size_t cbData, char **ppszList, size_t *pcbList)
{
    AssertPtrReturn(pvData, VERR_INVALID_POINTER);
    AssertReturn(cbData, VERR_INVALID_PARAMETER);
    AssertPtrReturn(ppszList, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbList, VERR_INVALID_POINTER);

    /* For URI lists we only accept valid UTF-8 encodings. */
    int rc = RTStrValidateEncodingEx((char *)pvData, cbData, 0 /* fFlags */);
    if (RT_FAILURE(rc))
    {
        LogRelMax(16, ("Shared Clipboard: X11 URI-list clipboard data is not valid UTF-8 (%zu bytes), rc=%Rrc\n",
                       cbData, rc));
        return rc;
    }

    *ppszList = NULL;
    *pcbList  = 0;

# ifdef DEBUG_andy
    LogFlowFunc(("Data:\n%.*RhXd\n", cbData, pvData));
# endif

    const char *pchCur = pvData;
    const char *pchEnd = pvData + cbData;
    while (   pchEnd > pchCur
           && pchEnd[-1] == '\0')
        pchEnd--;

    uint32_t cEntries = 0;
    bool     fFirstContentLine = true;
    while (pchCur < pchEnd)
    {
        const char *pchLine = pchCur;
        while (   pchCur < pchEnd
               && *pchCur != '\r'
               && *pchCur != '\n'
               && *pchCur != '\0')
            pchCur++;

        if (   pchCur < pchEnd
            && *pchCur == '\0')
        {
            LogRelMax(16, ("Shared Clipboard: X11 URI-list clipboard data contains an embedded NUL byte; refusing file transfer\n"));
            rc = VERR_INVALID_PARAMETER;
            break;
        }

        size_t const cchLine = (size_t)(pchCur - pchLine);

        if (pchCur < pchEnd)
        {
            if (*pchCur == '\r')
            {
                pchCur++;
                if (   pchCur < pchEnd
                    && *pchCur == '\n')
                    pchCur++;
            }
            else if (*pchCur == '\n')
                pchCur++;
        }

        if (cchLine == 0)
            continue;

        /* text/uri-list comments are not transfer roots. */
        if (*pchLine == '#')
            continue;

        if (   fFirstContentLine
            && shClX11TransferIsActionLine(pchLine, cchLine))
        {
            LogRelMax2(16, ("Shared Clipboard: Skipping X11 file-copy action marker '%.*s'\n",
                           (int)RT_MIN(cchLine, (size_t)32), pchLine));
            fFirstContentLine = false;
            continue;
        }
        fFirstContentLine = false;

        LogRelMax2(16, ("Shared Clipboard: X11 URI-list entry #%RU32: '%.*s'\n",
                       cEntries, (int)RT_MIN(cchLine, (size_t)4096), pchLine));

        rc = shClX11TransferAppendListEntry(ppszList, pcbList, pchLine, cchLine);
        if (RT_FAILURE(rc))
            break;

        cEntries++;
    }

    if (RT_FAILURE(rc))
    {
        RTStrFree(*ppszList);
        *ppszList = NULL;
        *pcbList  = 0;
        return rc;
    }

    if (!cEntries)
    {
        LogRelMax(16, ("Shared Clipboard: X11 URI-list clipboard data contained no file entries; refusing file transfer\n"));
        return VERR_SHCLPB_NO_DATA;
    }

    LogRelMax2(16, ("Shared Clipboard: Converted %RU32 X11 URI-list entries for host file transfer\n", cEntries));

    *pcbList += 1; /* Include terminator. */
    return VINF_SUCCESS;
}
#endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */

/**
 * Worker function for clipConvertDataFromX11.
 *
 * Converts the data read from the X11 clipboard to the required format.
 * Signals the wait event.
 *
 * Converts the text obtained UTF-16LE with Windows EOLs.
 * Converts full BMP data to DIB format.
 *
 * @thread X11 event thread.
 */
SHCL_X11_DECL(void) clipConvertDataFromX11Worker(void *pClient, void *pvSrc, unsigned cbSrc)
{
    PSHCLX11REQUEST pReq = (PSHCLX11REQUEST)pClient;
    AssertPtrReturnVoid(pReq);

    LogFlowFunc(("uFmtVBox=%#x, idxFmtX11=%u, pvSrc=%p, cbSrc=%u\n", pReq->Read.uFmtVBox, pReq->Read.idxFmtX11, pvSrc, cbSrc));

    /* Sanity. */
    AssertReturnVoid(pReq->enmType == SHCLX11EVENTTYPE_READ);
    AssertReturnVoid(pReq->Read.uFmtVBox != VBOX_SHCL_FMT_NONE);
    AssertReturnVoid(pReq->Read.idxFmtX11 < SHCL_MAX_X11_FORMATS);

    AssertPtrReturnVoid(pReq->pCtx);

    LogRel2(("Shared Clipboard: Converting X11 format index %#x to VBox format %#x (%RU32 bytes max)\n",
             pReq->Read.idxFmtX11, pReq->Read.uFmtVBox, pReq->Read.cbMax));

    int rc = VINF_SUCCESS;

    void  *pvDst = NULL;
    size_t cbDst = 0;

    PSHCLX11CTX pCtx = pReq->pCtx;
    AssertPtr(pReq->pCtx);

#ifdef VBOX_WITH_SHARED_CLIPBOARD_XT_BUSY
    clipSetXtBusy(pCtx, false);
    if (clipGetXtNeedsUpdate(pCtx))
        clipQueryX11Targets(pCtx);
#endif

    /* If X11 clipboard buffer has no data, libXt can pass to XtGetSelectionValue()
     * callback an empty string, in this case cbSrc is 0. */
    if (pvSrc == NULL || cbSrc == 0)
    {
        /* The clipboard selection may have changed before we could get it. */
        rc = VERR_SHCLPB_NO_DATA;
    }
    else if (pReq->Read.uFmtVBox == VBOX_SHCL_FMT_UNICODETEXT)
    {
        /* In which format is the clipboard data? */
        switch (clipRealFormatForX11Format(pReq->Read.idxFmtX11))
        {
            case SHCLX11FMT_UTF8:
                RT_FALL_THROUGH();
            case SHCLX11FMT_TEXT:
            {
                size_t cwDst;
                /* If we are given broken UTF-8, we treat it as Latin1. */ /** @todo BUGBUG Is this acceptable? */
                if (RT_SUCCESS(RTStrValidateEncodingEx((char *)pvSrc, cbSrc, 0)))
                    rc = ShClHlpConvUtf8LFToUtf16CRLF((const char *)pvSrc, cbSrc,
                                                       (PRTUTF16 *)&pvDst, &cwDst);
                else
                    rc = ShClHlpConvLatin1LFToUtf16CRLF((char *)pvSrc, cbSrc,
                                                         (PRTUTF16 *)&pvDst, &cwDst);
                if (RT_SUCCESS(rc))
                {
                    cwDst += 1                        /* Include terminator */;
                    cbDst  = cwDst * sizeof(RTUTF16); /* Convert RTUTF16 units to bytes. */

                    LogFlowFunc(("UTF-16 text (%zu bytes):\n%ls\n", cbDst, pvDst));
                }
                break;
            }

            default:
            {
                rc = VERR_INVALID_PARAMETER;
                break;
            }
        }
    }
    else if (pReq->Read.uFmtVBox == VBOX_SHCL_FMT_BITMAP)
    {
        /* In which format is the clipboard data? */
        switch (clipRealFormatForX11Format(pReq->Read.idxFmtX11))
        {
            case SHCLX11FMT_BMP:
            {
                const void *pDib;
                size_t cbDibSize;
                rc = ShClHlpBmpGetDib((const void *)pvSrc, cbSrc,
                                       &pDib, &cbDibSize);
                if (RT_SUCCESS(rc))
                {
                    pvDst = RTMemAlloc(cbDibSize);
                    if (!pvDst)
                        rc = VERR_NO_MEMORY;
                    else
                    {
                        memcpy(pvDst, pDib, cbDibSize);
                        cbDst = cbDibSize;
                    }
                }
                break;
            }

            default:
            {
                rc = VERR_INVALID_PARAMETER;
                break;
            }
        }
    }
    else if (pReq->Read.uFmtVBox == VBOX_SHCL_FMT_HTML)
    {
        /* In which format is the clipboard data? */
        switch (clipRealFormatForX11Format(pReq->Read.idxFmtX11))
        {
            case SHCLX11FMT_HTML:
            {
                /*
                 * The common VBox HTML encoding will be - UTF-8
                 * because it more general for HTML formats then UTF-16
                 * X11 clipboard returns UTF-16, so before sending it we should
                 * convert it to UTF-8.
                 */
                pvDst = NULL;
                cbDst = 0;

                /*
                 * Some applications sends data in UTF-16, some in UTF-8,
                 * without indication it in MIME.
                 *
                 * In case of UTF-16, at least [Open|Libre] Office adds an byte order mark (0xfeff)
                 * at the start of the clipboard data.
                 */
                if (   cbSrc >= sizeof(RTUTF16)
                    && *(PRTUTF16)pvSrc == VBOX_SHCL_UTF16_BOM)
                {
                    rc = ShClHlpConvUtf16ToUtf8HTML((PRTUTF16)pvSrc, cbSrc / sizeof(RTUTF16), (char**)&pvDst, &cbDst);
                    if (RT_SUCCESS(rc))
                    {
                        LogFlowFunc(("UTF-16 Unicode source (%u bytes):\n%ls\n\n", cbSrc, pvSrc));
                        LogFlowFunc(("Byte Order Mark = %hx", ((PRTUTF16)pvSrc)[0]));
                        LogFlowFunc(("UTF-8 Unicode dest (%u bytes):\n%s\n\n", cbDst, pvDst));
                    }
                    else
                        LogRel(("Shared Clipboard: Converting UTF-16 Unicode failed with %Rrc\n", rc));
                }
                else /* Raw data. */
                {
                    pvDst = RTMemAllocZ(cbSrc + 1 /* '\0' */);
                    if(pvDst)
                    {
                         memcpy(pvDst, pvSrc, cbSrc);
                         cbDst = cbSrc + 1 /* '\0' */;
                    }
                    else
                    {
                         rc = VERR_NO_MEMORY;
                         break;
                    }
                }

                rc = VINF_SUCCESS;
                break;
            }

            default:
            {
                rc = VERR_INVALID_PARAMETER;
                break;
            }
        }
    }
# ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    else if (pReq->Read.uFmtVBox == VBOX_SHCL_FMT_URI_LIST)
    {
        SHCLX11FMT const enmFmtX11 = clipRealFormatForX11Format(pReq->Read.idxFmtX11);
        /* Recheck the direction at the conversion boundary.  Normally target
         * selection already guarantees this, but the check also protects
         * forced, stale or otherwise inconsistent format indices from feeding
         * metadata such as KDE's "0"/"1" value to the URI-list parser. */
        if (shClX11TransferTargetSupportsDirection(enmFmtX11, SHCLX11TRANSFERDIR_F_FROM_X11))
            rc = ShClX11TransferConvertFromX11((const char *)pvSrc, cbSrc, (char **)&pvDst, &cbDst);
        else
        {
            const char *pszTarget = pReq->Read.idxFmtX11 < RT_ELEMENTS(g_aFormats)
                                  ? g_aFormats[pReq->Read.idxFmtX11].pcszAtom : "<invalid>";
            LogRelMax(16, ("Shared Clipboard: Refusing to parse X11 clipboard target '%s' as a file list\n",
                           pszTarget));
            rc = VERR_NOT_SUPPORTED;
        }
    }
# endif /* VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS */
    else
        rc = VERR_NOT_SUPPORTED;

    LogFlowFunc(("pvDst=%p, cbDst=%RU32\n", pvDst, cbDst));

    if (RT_FAILURE(rc))
        LogRel(("Shared Clipboard: Converting X11 format index %#x to VBox format %#x failed, rc=%Rrc\n",
                pReq->Read.idxFmtX11, pReq->Read.uFmtVBox, rc));

    PSHCLEVENTPAYLOAD pPayload = NULL;

    if (   pvDst
        && cbDst)
    {
        size_t           cbResp   = sizeof(SHCLX11RESPONSE);
        PSHCLX11RESPONSE pResp    = (PSHCLX11RESPONSE)RTMemAllocZ(cbResp);
        if (pResp)
        {
            pResp->enmType     = SHCLX11EVENTTYPE_READ;
            pResp->Read.pvData = pvDst;
            pResp->Read.cbData = cbDst;

            pvDst = NULL; /* The response owns the data now. */

            rc = ShClPayloadCreate(0 /* ID, unused */, pResp, cbResp, &pPayload);
        }
        else
            rc = VERR_NO_MEMORY;
    }

    /* Let the caller know in any case. */
    int rc2 = ShClEventSignal(pReq->pEvent, pPayload);
    if (RT_SUCCESS(rc2))
        pPayload = NULL; /* The event owns the payload now. */

    if (RT_SUCCESS(rc))
        rc = rc2;

    if (pPayload) /* Free payload on error. */
    {
        ShClPayloadDestroy(pPayload);
        pPayload = NULL;
    }

    if (RT_FAILURE(rc))
        LogRelMax(16, ("Shared Clipboard: Converting X11 clipboard data failed with %Rrc\n", rc));
    else
        LogRel2(("Shared Clipboard: Converting X11 clipboard data completed with %Rrc\n", rc));

    RTMemFree(pReq);
    RTMemFree(pvDst);

    LogFlowFuncLeaveRC(rc);
}

/**
 * Converts the data read from the X11 clipboard to the required format.
 *
 * @thread X11 event thread.
 */
SHCL_X11_DECL(void) clipConvertDataFromX11(Widget widget, XtPointer pClient,
                                           Atom * /* selection */, Atom *atomType,
                                           XtPointer pvSrc, long unsigned int *pcLen,
                                           int *piFormat)
{
    RT_NOREF(widget);

    int rc = VINF_SUCCESS;

    if (*atomType == XT_CONVERT_FAIL) /* Xt timeout */
    {
        LogRel(("Shared Clipboard: Reading clipboard data from X11 timed out\n"));
        rc = VERR_TIMEOUT;
    }
    else
    {
        PSHCLX11REQUEST pReq = (PSHCLX11REQUEST)pClient;
        if (pReq) /* Give some more clues, if available. */
        {
            AssertReturnVoid(pReq->enmType == SHCLX11EVENTTYPE_READ);
            AssertReturnVoid(pReq->Read.idxFmtX11 < SHCL_MAX_X11_FORMATS); /* Paranoia, should be checked already by the caller. */
            if (LogRelIs2Enabled())
            {
                char *pszFmts = ShClFormatsToStrA(pReq->Read.uFmtVBox);
                LogRel2(("Shared Clipboard: Converting X11 format '%s' -> VBox format %#x/'%s'\n",
                         g_aFormats[pReq->Read.idxFmtX11].pcszAtom, pReq->Read.uFmtVBox, pszFmts ? pszFmts : "<alloc failed>"));
                RTStrFree(pszFmts);
            }

            if (pReq->pCtx->Callbacks.pfnOnClipboardRead) /* Usually only used for testcases. */
            {
                void  *pvData = NULL;
                size_t cbData = 0;
                rc = pReq->pCtx->Callbacks.pfnOnClipboardRead(pReq->pCtx->pFrontend, pReq->Read.uFmtVBox, &pvData, &cbData, NULL);
                if (RT_SUCCESS(rc))
                {
                    /* Feed to conversion worker. */
                    clipConvertDataFromX11Worker(pClient, pvData, cbData);
                    RTMemFree(pvData);
                }
            }
            else /* Call conversion worker with current data provided by X (default). */
                clipConvertDataFromX11Worker(pClient, pvSrc, (*pcLen) * (*piFormat) / 8);
        }
        else
            rc = VERR_INVALID_POINTER;
    }

    if (RT_FAILURE(rc))
    {
        LogRel(("Shared Clipboard: Reading clipboard data from X11 failed with %Rrc\n", rc));

        /* Make sure to complete the request in any case by calling the conversion worker. */
        clipConvertDataFromX11Worker(pClient, NULL, 0);
    }

    XtFree((char *)pvSrc);
}

/**
 * Requests the current clipboard data from a specific selection.
 *
 * @returns VBox status code.
 * @param   pCtx                The X11 clipboard context to use.
 * @param   pszWhere            Clipboard selection to request the data from.
 * @param   idxFmt              The X11 format to request the data in.
 * @param   pReq                Where to store the requested data on success.
 */
static int clipGetSelectionValueEx(PSHCLX11CTX pCtx, const char *pszWhere, SHCLX11FMTIDX idxFmt,
                                   PSHCLX11REQUEST pReq)
{
    AssertPtrReturn(pszWhere, VERR_INVALID_POINTER);
    AssertReturn(idxFmt < SHCL_MAX_X11_FORMATS, VERR_INVALID_PARAMETER);
    AssertReturn(clipIsSupportedSelectionType(pCtx, clipGetAtom(pCtx, pszWhere)), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pReq, VERR_INVALID_POINTER);

    LogRel2(("Shared Clipboard: Requesting X11 selection value in %s for format '%s'\n", pszWhere, g_aFormats[idxFmt].pcszAtom));

#ifndef TESTCASE
    XtGetSelectionValue(pCtx->pWidget, clipGetAtom(pCtx, pszWhere),
                        clipAtomForX11Format(pCtx, idxFmt),
                        clipConvertDataFromX11,
                        reinterpret_cast<XtPointer>(pReq),
                        CurrentTime);
#else
    tstClipRequestData(pCtx, idxFmt, (void *)pReq);
#endif

    return VINF_SUCCESS; /** @todo Return real rc. */
}

/**
 * Requests the current clipboard data from the CLIPBOARD selection.
 *
 * @returns VBox status code.
 * @param   pCtx                The X11 clipboard context to use.
 * @param   idxFmt              The X11 format to request the data in.
 * @param   pReq                Where to store the requested data on success.
 *
 * @sa clipGetSelectionValueEx() for requesting data for a specific selection.
 */
static int clipGetSelectionValue(PSHCLX11CTX pCtx, SHCLX11FMTIDX idxFmt, PSHCLX11REQUEST pReq)
{
    return clipGetSelectionValueEx(pCtx, "CLIPBOARD", idxFmt, pReq);
}

/**
 * Worker function for ShClX11ReadDataFromX11Async.
 *
 * @param  pvUserData           Pointer to a PSHCLX11REQUEST structure containing
 *                              information about the clipboard read request.
 *                              Must be free'd by the worker.
 * @thread X11 event thread.
 */
static void ShClX11ReadDataFromX11Worker(void *pvUserData, void * /* interval */)
{
    AssertPtrReturnVoid(pvUserData);

    PSHCLX11REQUEST   pReq = (PSHCLX11REQUEST)pvUserData;
    AssertReturnVoid(pReq->enmType == SHCLX11EVENTTYPE_READ);
    SHCLX11CTX       *pCtx = pReq->pCtx;
    AssertPtrReturnVoid(pCtx);

    LogFlowFunc(("pReq->uFmtVBox=%#x, idxFmtX11=%#x\n", pReq->Read.uFmtVBox, pReq->Read.idxFmtX11));

    int rc = VERR_SHCLPB_NO_DATA; /* VBox thinks we have data and we don't. */

#ifdef VBOX_WITH_SHARED_CLIPBOARD_XT_BUSY
    const bool fXtBusy = clipGetXtBusy(pCtx);
    clipSetXtBusy(pCtx, true);
    if (fXtBusy)
    {
        /* If the clipboard is busy just fend off the request. */
        rc = VERR_TRY_AGAIN;
    }
    else
#endif
    if (pReq->Read.uFmtVBox & VBOX_SHCL_FMT_UNICODETEXT)
    {
        pReq->Read.idxFmtX11 = pCtx->idxFmtText;
        if (pReq->Read.idxFmtX11 != SHCLX11FMT_INVALID)
        {
            /* Send out a request for the data to the current clipboard owner. */
            rc = clipGetSelectionValue(pCtx, pCtx->idxFmtText, pReq);
        }
    }
    else if (pReq->Read.uFmtVBox & VBOX_SHCL_FMT_BITMAP)
    {
        pReq->Read.idxFmtX11 = pCtx->idxFmtBmp;
        if (pReq->Read.idxFmtX11 != SHCLX11FMT_INVALID)
        {
            /* Send out a request for the data to the current clipboard owner. */
            rc = clipGetSelectionValue(pCtx, pCtx->idxFmtBmp, pReq);
        }
    }
    else if (pReq->Read.uFmtVBox & VBOX_SHCL_FMT_HTML)
    {
        pReq->Read.idxFmtX11 = pCtx->idxFmtHTML;
        if (pReq->Read.idxFmtX11 != SHCLX11FMT_INVALID)
        {
            /* Send out a request for the data to the current clipboard owner. */
            rc = clipGetSelectionValue(pCtx, pCtx->idxFmtHTML, pReq);
        }
    }
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    else if (pReq->Read.uFmtVBox & VBOX_SHCL_FMT_URI_LIST)
    {
        pReq->Read.idxFmtX11 = pCtx->idxFmtURI;
        if (pReq->Read.idxFmtX11 != SHCLX11FMT_INVALID)
        {
            /* Send out a request for the data to the current clipboard owner. */
            rc = clipGetSelectionValue(pCtx, pCtx->idxFmtURI, pReq);
        }
    }
#endif
    else
    {
#ifdef VBOX_WITH_SHARED_CLIPBOARD_XT_BUSY
        clipSetXtBusy(pCtx, false);
#endif
        rc = VERR_NOT_IMPLEMENTED;
    }

    /* If the above stuff fails, make sure to let the waiters know.
     *
     * Getting the actual selection value via clipGetSelectionValue[Ex]() above will happen in the X event thread,
     * which has its own signalling then. So this check only handles errors which happens before we put anything
     * onto the X event thread.
     */
    if (RT_FAILURE(rc))
    {
        int rc2 = ShClEventSignalEx(pReq->pEvent, rc, NULL /* Payload */);
        AssertRC(rc2);

        RTMemFree(pReq);
    }

    if (RT_FAILURE(rc))
        LogRelMax(16, ("Shared Clipboard: Reading X11 clipboard data failed with %Rrc\n", rc));
    else
        LogRel2(("Shared Clipboard: Reading X11 clipboard data completed with %Rrc\n", rc));

    LogFlowFuncLeaveRC(rc);
}

/**
 * Reads from the X11 clipboard (asynchronously).
 *
 * @returns VBox status code.
 * @param   pCtx                Context data for the clipboard backend.
 * @param   uFmt                The format that the VBox would like to receive the data in.
 * @param   cbMax               Maximum data to read (in bytes).
 *                              Specify UINT32_MAX to read as much as available.
 * @param   pEvent              Event to use for waiting for data to arrive.
 *                              The event's payload will contain the data read. Needs to be free'd with ShClEventRelease().
 */
int ShClX11ReadDataFromX11Async(PSHCLX11CTX pCtx, SHCLFORMAT uFmt, uint32_t cbMax, PSHCLEVENT pEvent)
{
    AssertPtrReturn(pEvent, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    PSHCLX11REQUEST pReq = (PSHCLX11REQUEST)RTMemAllocZ(sizeof(SHCLX11REQUEST));
    if (pReq)
    {
        pReq->enmType       = SHCLX11EVENTTYPE_READ;
        pReq->pCtx          = pCtx;
        pReq->Read.uFmtVBox = uFmt;
        pReq->Read.cbMax    = cbMax;
        pReq->pEvent        = pEvent;

        /* We use this to schedule a worker function on the event thread. */
        rc = clipThreadScheduleCall(pCtx, ShClX11ReadDataFromX11Worker, (XtPointer)pReq);
        if (RT_FAILURE(rc))
            RTMemFree(pReq);
    }
    else
        rc = VERR_NO_MEMORY;

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Reads from the X11 clipboard, internal version.
 *
 * @returns VBox status code.
 * @retval  VERR_SHCLPB_NO_DATA if format is supported but no data is available currently.
 * @retval  VERR_NOT_IMPLEMENTED if the format is not implemented.
 * @param   pCtx                Context data for the clipboard backend.
 * @param   pEventSource        Event source to use.
 * @param   msTimeout           Timeout (in ms) for waiting.
 * @param   uFmt                The format that the VBox would like to receive the data in.
 * @param   cbMax               Maximum size (in bytes) to read.
 * @param   pResp               Where to return the allocated SHCLX11RESPONSE on success.
 *                              Must be free'd via shClX11ResponseFree() by the caller.
 */
static int shClX11ReadDataFromX11Internal(PSHCLX11CTX pCtx, PSHCLEVENTSOURCE pEventSource, RTMSINTERVAL msTimeout,
                                          SHCLFORMAT uFmt, uint32_t cbMax, PSHCLX11RESPONSE *ppResp)
{
    PSHCLEVENT pEvent;
    int rc = ShClEventSourceGenerateAndRegisterEvent(pEventSource, &pEvent);
    if (RT_SUCCESS(rc))
    {
        rc = ShClX11ReadDataFromX11Async(pCtx, uFmt, cbMax, pEvent);
        if (RT_SUCCESS(rc))
        {
            PSHCLEVENTPAYLOAD pPayload;
            int               rcEvent;
            rc = ShClEventWaitEx(pEvent, msTimeout, &rcEvent, &pPayload);
            if (RT_SUCCESS(rc))
            {
                if (pPayload)
                {
                    if (   pPayload->cbData == sizeof(SHCLX11RESPONSE)
                        && RT_VALID_PTR(pPayload->pvData))
                    {
                        PSHCLX11RESPONSE pResp = (PSHCLX11RESPONSE)pPayload->pvData;
                        if (pResp->enmType == SHCLX11EVENTTYPE_READ)
                        {
                            pPayload->pvData = NULL; /* pvData (pResp) is owned by ppResp now. */
                            pPayload->cbData = 0;

                            ShClPayloadDestroy(pPayload);

                            *ppResp = pResp;
                        }
                        else
                            rc = VERR_INVALID_PARAMETER;
                    }
                    else
                        rc = VERR_INVALID_PARAMETER;
                }
                else /* No payload given; could happen on invalid / not-expected formats. */
                    rc = VERR_SHCLPB_NO_DATA;
            }
            else if (rc == VERR_SHCLPB_EVENT_FAILED)
                rc = rcEvent;
        }

        ShClEventRelease(pEvent);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Reads from the X11 clipboard, extended version.
 *
 * @returns VBox status code.
 * @retval  VERR_SHCLPB_NO_DATA if format is supported but no data is available currently.
 * @retval  VERR_NOT_IMPLEMENTED if the format is not implemented.
 * @param   pCtx                Context data for the clipboard backend.
 * @param   pEventSource        Event source to use.
 * @param   msTimeout           Timeout (in ms) for waiting.
 * @param   uFmt                The format that the VBox would like to receive the data in.
 * @param   ppvBuf              Where to return the allocated received data on success.
 *                              Must be free'd by the caller.
 * @param   pcbBuf              Where to return the size (in bytes) of \a ppvBuf.
 */
int ShClX11ReadDataFromX11Ex(PSHCLX11CTX pCtx, PSHCLEVENTSOURCE pEventSource, RTMSINTERVAL msTimeout,
                             SHCLFORMAT uFmt, void **ppvBuf, uint32_t *pcbBuf)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pEventSource, VERR_INVALID_POINTER);
    AssertPtrReturn(ppvBuf, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbBuf, VERR_INVALID_POINTER);

    PSHCLX11RESPONSE pResp;
    int rc = shClX11ReadDataFromX11Internal(pCtx, pEventSource, msTimeout, uFmt, UINT32_MAX, &pResp);
    if (RT_SUCCESS(rc))
    {
        *ppvBuf = pResp->Read.pvData;
        *pcbBuf = pResp->Read.cbData;

        pResp->Read.pvData = NULL; /* Is owned by ppvBuf now. */
        pResp->Read.cbData = 0;

        shClX11ResponseFree(pResp);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Reads from the X11 clipboard.
 *
 * @returns VBox status code.
 * @retval  VERR_SHCLPB_NO_DATA if format is supported but no data is available currently.
 * @retval  VERR_NOT_IMPLEMENTED if the format is not implemented.
 * @param   pCtx                Context data for the clipboard backend.
 * @param   pEventSource        Event source to use.
 * @param   msTimeout           Timeout (in ms) for waiting.
 * @param   uFmt                The format that the VBox would like to receive the data in.
 * @param   pvBuf               Where to store the received data on success.
 * @param   cbBuf               Size (in bytes) of \a pvBuf. Also marks maximum data to read (in bytes).
 * @param   pcbRead             Where to return the read bytes on success. Optional.
 */
int ShClX11ReadDataFromX11(PSHCLX11CTX pCtx, PSHCLEVENTSOURCE pEventSource, RTMSINTERVAL msTimeout,
                           SHCLFORMAT uFmt, void *pvBuf, uint32_t cbBuf, uint32_t *pcbRead)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pEventSource, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf, VERR_INVALID_PARAMETER);
    /* pcbRead is optional. */

    PSHCLX11RESPONSE pResp;
    int rc = shClX11ReadDataFromX11Internal(pCtx, pEventSource, msTimeout, uFmt, cbBuf, &pResp);
    if (RT_SUCCESS(rc))
    {
        memcpy(pvBuf, pResp->Read.pvData, RT_MIN(cbBuf, pResp->Read.cbData));
        if (pcbRead)
            *pcbRead = pResp->Read.cbData;

        shClX11ResponseFree(pResp);
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Writes to the X11 clipboard (asynchronously).
 *
 * @returns VBox status code.
 * @param   pCtx                Context data for the clipboard backend.
 * @param   uFmts               The format(s) to write.
 *                              Conversions might be performed, if available.
 * @param   pvBuf               Pointer to data to write.
 * @param   cbBuf               Size (in bytes) of data to write.
 * @param   pEvent              Event to use for waiting for data to get written.
 *                              The event's payload will contain the amount of data written.
 *                              Needs to be free'd with ShClEventRelease().
 */
int ShClX11WriteDataToX11Async(PSHCLX11CTX pCtx, SHCLFORMATS uFmts, const void *pvBuf, uint32_t cbBuf, PSHCLEVENT pEvent)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf,  VERR_INVALID_POINTER);
    AssertReturn(cbBuf,  VERR_INVALID_PARAMETER);
    /* pEvent not used yet. */ RT_NOREF(pEvent);

    int rc = ShClCacheSetMultiple(&pCtx->Cache, uFmts, pvBuf, cbBuf);
    if (RT_SUCCESS(rc))
    {
        clipResetX11Formats(pCtx);
        clipGrabX11Clipboard(pCtx, uFmts);
    }

    return VINF_SUCCESS;
}

/**
 * Writes to the X11 clipboard.
 *
 * This function currently only is implemented as asynchronous version.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_AVAILABLE the the X11 clipboard is not available.
 * @retval  VERR_TRY_AGAIN if format is supported but data could not be written.
 * @retval  VERR_NOT_IMPLEMENTED if the format is not implemented.
 * @param   pCtx                Context data for the clipboard backend.
 * @param   uFmt                The format to write.
 * @param   pvBuf               Pointer to data to write. Must match format to write.
 * @param   cbBuf               Size (in bytes) of data to write.
 * @param   pcbWritten          Where to return the written bytes on success. Optional.
 *                              Currently always returns the value of \a cbBuf on success.
 *
 * @note    Text data must be in UTF-8, always.
 */
int ShClX11WriteDataToX11(PSHCLX11CTX pCtx, PSHCLEVENTSOURCE pEventSource, RTMSINTERVAL msTimeout,
                          SHCLFORMAT uFmt, const void *pvBuf, uint32_t cbBuf, uint32_t *pcbWritten)
{
    AssertPtrReturn(pCtx, VERR_INVALID_POINTER);
    AssertPtrReturn(pEventSource, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf, VERR_INVALID_PARAMETER);
    /* pcbWritetn is optional. */

    RT_NOREF(msTimeout); /* Not used yet. */

    int rc = ShClX11WriteDataToX11Async(pCtx, uFmt, pvBuf, cbBuf, NULL /* pEvent */);
    if (RT_SUCCESS(rc))
    {
        if (pcbWritten)
            *pcbWritten = cbBuf;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}
