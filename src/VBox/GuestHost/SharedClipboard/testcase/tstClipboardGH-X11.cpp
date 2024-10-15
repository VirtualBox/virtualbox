/* $Id: tstClipboardGH-X11.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Clipboard guest/host X11 code test cases.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#include <VBox/GuestHost/SharedClipboard.h>
#include <VBox/GuestHost/SharedClipboard-x11.h>
#include <VBox/GuestHost/clipboard-helper.h>
#include <VBox/HostServices/VBoxClipboardSvc.h>

#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/test.h>
#include <iprt/utf16.h>

#include <poll.h>
#include <X11/Xatom.h>


/*********************************************************************************************************************************
*   Externals                                                                                                                    *
*********************************************************************************************************************************/
extern SHCLX11FMTTABLE g_aFormats[];

extern void clipUpdateX11Targets(PSHCLX11CTX pCtx, SHCLX11FMTIDX *pTargets, size_t cTargets);
extern void clipReportEmpty(PSHCLX11CTX pCtx);
extern void clipConvertDataFromX11Worker(void *pClient, void *pvSrc, unsigned cbSrc);
extern SHCLX11FMTIDX clipGetTextFormatFromTargets(PSHCLX11CTX pCtx, SHCLX11FMTIDX *pTargets, size_t cTargets);
extern SHCLX11FMT clipRealFormatForX11Format(SHCLX11FMTIDX uFmtIdx);
extern Atom clipGetAtom(PSHCLX11CTX pCtx, const char *pcszName);
extern void clipQueryX11Targets(PSHCLX11CTX pCtx);
extern size_t clipReportMaxX11Formats(void);


/*********************************************************************************************************************************
*   Internal prototypes                                                                                                          *
*********************************************************************************************************************************/
static SHCLX11FMTIDX tstClipFindX11FormatByAtomText(const char *pcszAtom);


/*********************************************************************************************************************************
*   Prototypes, used for testcases by clipboard-x11.cpp                                                                          *
*********************************************************************************************************************************/
void tstRequestTargets(SHCLX11CTX* pCtx);
void tstClipRequestData(PSHCLX11CTX pCtx, SHCLX11FMTIDX target, void *closure);
void tstThreadScheduleCall(void (*proc)(void *, void *), void *client_data);


/*********************************************************************************************************************************
*   Own callback implementations                                                                                                 *
*********************************************************************************************************************************/
extern DECLCALLBACK(void) clipQueryX11TargetsCallback(Widget widget, XtPointer pClient,
                                                      Atom * /* selection */, Atom *atomType,
                                                      XtPointer pValue, long unsigned int *pcLen,
                                                      int *piFormat);


/*********************************************************************************************************************************
*   Defines                                                                                                                      *
*********************************************************************************************************************************/
#define TESTCASE_WIDGET_ID (Widget)0xffff


/* For the purpose of the test case, we just execute the procedure to be
 * scheduled, as we are running single threaded. */
void tstThreadScheduleCall(void (*proc)(void *, void *), void *client_data)
{
    proc(client_data, NULL);
}

/* The data in the simulated VBox clipboard. */
static int g_tst_rcDataVBox = VINF_SUCCESS;
static void *g_tst_pvDataVBox = NULL;
static uint32_t g_tst_cbDataVBox = 0;
static SHCLEVENTSOURCE g_EventSource;

/* Set empty data in the simulated VBox clipboard. */
static void tstClipEmptyVBox(PSHCLX11CTX pCtx, int retval)
{
    g_tst_rcDataVBox = retval;
    RTMemFree(g_tst_pvDataVBox);
    g_tst_pvDataVBox = NULL;
    g_tst_cbDataVBox = 0;
    ShClX11ReportFormatsToX11Async(pCtx, 0);
}

/* Set the data in the simulated VBox clipboard. */
static int tstClipSetVBoxUtf16(PSHCLX11CTX pCtx, int retval,
                               const char *pcszData, size_t cb)
{
    PRTUTF16 pwszData = NULL;
    size_t cwData = 0;
    int rc = RTStrToUtf16Ex(pcszData, RTSTR_MAX, &pwszData, 0, &cwData);
    if (RT_FAILURE(rc))
        return rc;
    AssertReturn(cb <= cwData * 2 + 2, VERR_BUFFER_OVERFLOW);
    void *pv = RTMemDup(pwszData, cb);
    RTUtf16Free(pwszData);
    if (pv == NULL)
        return VERR_NO_MEMORY;
    if (g_tst_pvDataVBox)
        RTMemFree(g_tst_pvDataVBox);
    g_tst_rcDataVBox = retval;
    g_tst_pvDataVBox = pv;
    g_tst_cbDataVBox = cb;
    ShClX11ReportFormatsToX11Async(pCtx, VBOX_SHCL_FMT_UNICODETEXT);
    return VINF_SUCCESS;
}

Display *XtDisplay(Widget w) { NOREF(w); return (Display *) 0xffff; }

void XtAppSetExitFlag(XtAppContext app_context) { NOREF(app_context); }

void XtDestroyWidget(Widget w) { NOREF(w); }

XtAppContext XtCreateApplicationContext(void) { return (XtAppContext)0xffff; }

void XtDestroyApplicationContext(XtAppContext app_context) { NOREF(app_context); }

void XtToolkitInitialize(void) {}

Boolean XtToolkitThreadInitialize(void) { return True; }

Display *XtOpenDisplay(XtAppContext app_context,
                       _Xconst _XtString display_string,
                       _Xconst _XtString application_name,
                       _Xconst _XtString application_class,
                       XrmOptionDescRec *options, Cardinal num_options,
                       int *argc, char **argv)
{
    RT_NOREF8(app_context, display_string, application_name, application_class, options, num_options, argc, argv);
    return (Display *)0xffff;
}

Widget XtVaAppCreateShell(_Xconst _XtString application_name,  _Xconst _XtString application_class,
                          WidgetClass widget_class, Display *display, ...)
{
    RT_NOREF(application_name, application_class, widget_class, display);
    return TESTCASE_WIDGET_ID;
}

void XtSetMappedWhenManaged(Widget widget, _XtBoolean mapped_when_managed) { RT_NOREF(widget, mapped_when_managed); }

void XtRealizeWidget(Widget widget) { NOREF(widget); }

XtInputId XtAppAddInput(XtAppContext app_context, int source, XtPointer condition, XtInputCallbackProc proc, XtPointer closure)
{
    RT_NOREF(app_context, source, condition, proc, closure);
    return 0xffff;
}

/* Atoms we need other than the formats we support. */
static const char *g_tst_apszSupAtoms[] =
{
    "PRIMARY", "CLIPBOARD", "TARGETS", "MULTIPLE", "TIMESTAMP"
};

/* This just looks for the atom names in a couple of tables and returns an
 * index with an offset added. */
Atom XInternAtom(Display *, const char *pcsz, int)
{
    Atom atom = 0;
    size_t const cFormats = clipReportMaxX11Formats();
    size_t i;
    for (i = 0; i < cFormats; ++i)
    {
        if (!strcmp(pcsz, g_aFormats[i].pcszAtom))
            atom = (Atom) (i + 0x1000);
    }
    for (i = 0; i < RT_ELEMENTS(g_tst_apszSupAtoms); ++i)
        if (!strcmp(pcsz, g_tst_apszSupAtoms[i]))
            atom = (Atom) (i + 0x2000);
    Assert(atom);  /* Have we missed any atoms? */
    return atom;
}

/* Take a request for the targets we are currently offering. */
static SHCLX11FMTIDX g_tst_aSelTargetsIdx[10] = { 0 };
static size_t g_tst_cTargets = 0;

void tstRequestTargets(SHCLX11CTX* pCtx)
{
    clipUpdateX11Targets(pCtx, g_tst_aSelTargetsIdx, g_tst_cTargets);
}

/* The current values of the X selection, which will be returned to the
 * XtGetSelectionValue callback. */
static Atom g_tst_atmSelType = 0;
static const void *g_tst_pSelData = NULL;
static unsigned long g_tst_cSelData = 0;
static int g_tst_selFormat = 0;

void tstClipRequestData(PSHCLX11CTX pCtx, SHCLX11FMTIDX target, void *closure)
{
    RT_NOREF(pCtx);
    unsigned long count = 0;
    int format = 0;
    if (target != g_tst_aSelTargetsIdx[0])
    {
        clipConvertDataFromX11Worker(closure, NULL, 0); /* Could not convert to target. */
        return;
    }
    void *pValue = NULL;
    pValue = g_tst_pSelData ? RTMemDup(g_tst_pSelData, g_tst_cSelData) : NULL;
    count = g_tst_pSelData ? g_tst_cSelData : 0;
    format = g_tst_selFormat;
    if (!pValue)
    {
        count = 0;
        format = 0;
    }
    clipConvertDataFromX11Worker(closure, pValue, count * format / 8);
    if (pValue)
        RTMemFree(pValue);
}

/* The formats currently on offer from X11 via the shared clipboard. */
static uint32_t g_tst_uX11Formats = 0;

static uint32_t tstClipQueryFormats(void)
{
    return g_tst_uX11Formats;
}

static void tstClipInvalidateFormats(void)
{
    g_tst_uX11Formats = ~0;
}

static RTMSINTERVAL g_msTimeout = RT_MS_5SEC;
/* Does our clipboard code currently own the selection? */
static bool g_tst_fOwnsSel = false;
/* The procedure that is called when we should convert the selection to a
 * given format. */
static XtConvertSelectionProc g_tst_pfnSelConvert = NULL;
/* The procedure which is called when we lose the selection. */
static XtLoseSelectionProc g_tst_pfnSelLose = NULL;
/* The procedure which is called when the selection transfer has completed. */
static XtSelectionDoneProc g_tst_pfnSelDone = NULL;

Boolean XtOwnSelection(Widget widget, Atom selection, Time time,
                       XtConvertSelectionProc convert,
                       XtLoseSelectionProc lose,
                       XtSelectionDoneProc done)
{
    RT_NOREF(widget, time);
    if (selection != XInternAtom(NULL, "CLIPBOARD", 0))
        return True;  /* We don't really care about this. */
    g_tst_fOwnsSel = true;  /* Always succeed. */
    g_tst_pfnSelConvert = convert;
    g_tst_pfnSelLose = lose;
    g_tst_pfnSelDone = done;
    return True;
}

void XtDisownSelection(Widget widget, Atom selection, Time time)
{
    RT_NOREF(widget, time, selection);
    g_tst_fOwnsSel = false;
    g_tst_pfnSelConvert = NULL;
    g_tst_pfnSelLose = NULL;
    g_tst_pfnSelDone = NULL;
}

/* Request the shared clipboard to convert its data to a given format. */
static bool tstClipConvertSelection(const char *pcszTarget, Atom *type,
                                    XtPointer *value, unsigned long *length,
                                    int *format)
{
    Atom target = XInternAtom(NULL, pcszTarget, 0);
    if (target == 0)
        return false;
    /* Initialise all return values in case we make a quick exit. */
    *type = XA_STRING;
    *value = NULL;
    *length = 0;
    *format = 0;
    if (!g_tst_fOwnsSel)
        return false;
    if (!g_tst_pfnSelConvert)
        return false;
    Atom clipAtom = XInternAtom(NULL, "CLIPBOARD", 0);
    if (!g_tst_pfnSelConvert(TESTCASE_WIDGET_ID, &clipAtom, &target, type,
                             value, length, format))
        return false;
    if (g_tst_pfnSelDone)
        g_tst_pfnSelDone(TESTCASE_WIDGET_ID, &clipAtom, &target);
    return true;
}

/* Set the current X selection data */
static void tstClipSetSelectionValues(const char *pcszTarget, Atom type,
                                      const void *data,
                                      unsigned long count, int format)
{
    Atom clipAtom = XInternAtom(NULL, "CLIPBOARD", 0);
    g_tst_aSelTargetsIdx[0] = tstClipFindX11FormatByAtomText(pcszTarget);
    g_tst_cTargets = 1;
    g_tst_atmSelType = type;
    g_tst_pSelData = data;
    g_tst_cSelData = count;
    g_tst_selFormat = format;
    if (g_tst_pfnSelLose)
        g_tst_pfnSelLose(TESTCASE_WIDGET_ID, &clipAtom);
    g_tst_fOwnsSel = false;
}

static void tstClipSendTargetUpdate(PSHCLX11CTX pCtx)
{
    clipQueryX11Targets(pCtx);
}

/* Configure if and how the X11 TARGETS clipboard target will fail. */
static void tstClipSetTargetsFailure(void)
{
    g_tst_cTargets = 0;
}

char *XtMalloc(Cardinal size)
{
    return (char *) RTMemAlloc(size);
}

void XtFree(char *ptr)
{
    RTMemFree((void *)ptr);
}

char *XGetAtomName(Display *display, Atom atom)
{
    RT_NOREF(display);
    if (!atom)
        return NULL;
    const char *pcszName = NULL;
    if (0x1000 <= atom && atom < 0x2000)
    {
        unsigned index = atom - 0x1000;
        AssertReturn(index < clipReportMaxX11Formats(), NULL);
        pcszName = g_aFormats[index].pcszAtom;
    }
    else
    {
        unsigned index = atom - 0x2000;
        AssertReturn(index < RT_ELEMENTS(g_tst_apszSupAtoms), NULL);
        pcszName = g_tst_apszSupAtoms[index];
    }
    return (char *)RTMemDup(pcszName, sizeof(pcszName) + 1);
}

int XFree(void *data)
{
    RTMemFree(data);
    return 0;
}

void XFreeStringList(char **list)
{
    if (list)
        RTMemFree(*list);
    RTMemFree(list);
}

#define TESTCASE_MAX_BUF_SIZE 256

static int g_tst_rcCompleted = VINF_SUCCESS;
static int g_tst_cbCompleted = 0;
static char g_tst_abCompletedBuf[TESTCASE_MAX_BUF_SIZE];

static DECLCALLBACK(int) tstShClReportFormatsCallback(PSHCLCONTEXT pCtx, uint32_t fFormats, void *pvUser)
{
    RT_NOREF(pCtx, pvUser);
    g_tst_uX11Formats = fFormats;
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) tstShClOnRequestDataFromSourceCallback(PSHCLCONTEXT pCtx, SHCLFORMAT uFmt, void **ppv, uint32_t *pcb, void *pvUser)
{
    RT_NOREF(pCtx, uFmt, pvUser);
    *pcb = g_tst_cbDataVBox;
    if (g_tst_pvDataVBox != NULL)
    {
        void *pv = RTMemDup(g_tst_pvDataVBox, g_tst_cbDataVBox);
        *ppv = pv;
        return pv != NULL ? g_tst_rcDataVBox : VERR_NO_MEMORY;
    }
    *ppv = NULL;
    return g_tst_rcDataVBox;
}

static DECLCALLBACK(int) tstShClOnSendDataToDestCallback(PSHCLCONTEXT pCtx, void *pv, uint32_t cb, void *pvUser)
{
    RT_NOREF(pCtx);

    PSHCLX11RESPONSE pData = (PSHCLX11RESPONSE)pvUser;

    if (cb <= TESTCASE_MAX_BUF_SIZE)
    {
        g_tst_rcCompleted = pData->rc;
        if (cb != 0)
            memcpy(g_tst_abCompletedBuf, pv, cb);
    }
    else
        g_tst_rcCompleted = VERR_BUFFER_OVERFLOW;
    g_tst_cbCompleted = cb;

    if (pData->enmType == SHCLX11EVENTTYPE_READ)
        RTMemFree(pData->Read.pvData);
    RTMemFree(pData);

    return VINF_SUCCESS;
}

/**
 * Looks up the X11 format matching a given X11 atom text.
 *
 * @returns the format on success, NIL_CLIPX11FORMAT on failure
 * @param   pcszAtom                Atom text to look up format for.
 */
static SHCLX11FMTIDX tstClipFindX11FormatByAtomText(const char *pcszAtom)
{
    const size_t j = clipReportMaxX11Formats();

    for (unsigned i = 0; i < j; ++i)
    {
        if (!strcmp(g_aFormats[i].pcszAtom, pcszAtom))
            return i;
    }
    return NIL_CLIPX11FORMAT;
}

static bool tstClipTextFormatConversion(PSHCLX11CTX pCtx)
{
    bool fSuccess = true;
    SHCLX11FMTIDX targets[2];
    SHCLX11FMTIDX x11Format;
    targets[0] = tstClipFindX11FormatByAtomText("text/plain");
    targets[1] = tstClipFindX11FormatByAtomText("image/bmp");
    x11Format = clipGetTextFormatFromTargets(pCtx, targets, 2);
    if (clipRealFormatForX11Format(x11Format) != SHCLX11FMT_TEXT)
        fSuccess = false;
    targets[0] = tstClipFindX11FormatByAtomText("UTF8_STRING");
    targets[1] = tstClipFindX11FormatByAtomText("text/plain");
    x11Format = clipGetTextFormatFromTargets(pCtx, targets, 2);
    if (clipRealFormatForX11Format(x11Format) != SHCLX11FMT_UTF8)
        fSuccess = false;
    return fSuccess;
}

static void tstStringFromX11(RTTEST hTest, PSHCLX11CTX pCtx,
                             const char *pcszExp, int rcExp)
{
    bool fRc = true;
    tstClipSendTargetUpdate(pCtx);
    if (tstClipQueryFormats() != VBOX_SHCL_FMT_UNICODETEXT)
    {
        RTTestFailed(hTest, "Wrong targets reported: %02X\n", tstClipQueryFormats());
    }
    else
    {
        uint32_t cbActual = 0;
        uint8_t  abBuf[TESTCASE_MAX_BUF_SIZE];
        int rc = ShClX11ReadDataFromX11(pCtx, &g_EventSource, g_msTimeout, VBOX_SHCL_FMT_UNICODETEXT, abBuf, sizeof(abBuf), &cbActual);
        if (rc != rcExp)
            RTTestFailed(hTest, "Wrong return code, expected %Rrc, got %Rrc\n", rcExp, rc);
        else if (RT_FAILURE(rcExp))
            fRc = true;
        else
        {
            RTUTF16 wcExp[TESTCASE_MAX_BUF_SIZE / 2];
            RTUTF16 *pwcExp = wcExp;
            size_t cwc = 0;
            rc = RTStrToUtf16Ex(pcszExp, RTSTR_MAX, &pwcExp, RT_ELEMENTS(wcExp), &cwc);
            AssertRC(rc);
            size_t cbExp = cwc * 2 + 2;
            if (RT_SUCCESS(rc))
            {
                if (cbActual != cbExp)
                {
                    RTTestFailed(hTest, "Returned string is the wrong size: got size %u, expected %u\n", cbActual, cbExp);
                }
                else
                {
                    if (memcmp(abBuf, wcExp, cbExp) == 0)
                        fRc = true;
                    else
                        RTTestFailed(hTest, "Returned string \"%.*ls\" does not match expected string \"%s\"\n",
                                     TESTCASE_MAX_BUF_SIZE, abBuf, pcszExp);
                }
            }
        }
    }
    if (!fRc)
        RTTestFailureDetails(hTest, "Expected: string \"%s\", rc=%Rrc\n", pcszExp, rcExp);
}

static void tstLatin1FromX11(RTTEST hTest, PSHCLX11CTX pCtx,
                             const char *pcszExp, int rcExp)
{
    bool retval = false;
    tstClipSendTargetUpdate(pCtx);
    if (tstClipQueryFormats() != VBOX_SHCL_FMT_UNICODETEXT)
        RTTestFailed(hTest, "Wrong targets reported: %02X\n",
                     tstClipQueryFormats());
    else
    {
        uint32_t cbActual = 0;
        uint8_t  abBuf[TESTCASE_MAX_BUF_SIZE];
        int rc = ShClX11ReadDataFromX11(pCtx, &g_EventSource, g_msTimeout, VBOX_SHCL_FMT_UNICODETEXT, abBuf, sizeof(abBuf), &cbActual);
        if (rc != rcExp)
            RTTestFailed(hTest, "Wrong return code, expected %Rrc, got %Rrc\n", rcExp, rc);
        else if (RT_FAILURE(rcExp))
            retval = true;
        else
        {
            RTUTF16 wcExp[TESTCASE_MAX_BUF_SIZE / 2];
            //RTUTF16 *pwcExp = wcExp; - unused
            size_t cwc;
            for (cwc = 0; cwc == 0 || pcszExp[cwc - 1] != '\0'; ++cwc)
                wcExp[cwc] = pcszExp[cwc];
            size_t cbExp = cwc * 2;
            if (cbActual != cbExp)
            {
                RTTestFailed(hTest, "Returned string is the wrong size, string \"%.*ls\", size %u, expected \"%s\", size %u\n",
                             RT_MIN(TESTCASE_MAX_BUF_SIZE, cbActual), abBuf, cbActual,
                             pcszExp, cbExp);
            }
            else
            {
                if (memcmp(abBuf, wcExp, cbExp) == 0)
                    retval = true;
                else
                    RTTestFailed(hTest, "Returned string \"%.*ls\" does not match expected string \"%s\"\n",
                                 TESTCASE_MAX_BUF_SIZE, abBuf, pcszExp);
            }
        }
    }
    if (!retval)
        RTTestFailureDetails(hTest, "Expected: string \"%s\", rc %Rrc\n",
                             pcszExp, rcExp);
}

static void tstStringFromVBox(RTTEST hTest, PSHCLX11CTX pCtx, const char *pcszTarget, Atom typeExp,  const char *valueExp)
{
    RT_NOREF(pCtx);
    bool retval = false;
    Atom type;
    XtPointer value = NULL;
    unsigned long length;
    int format;
    size_t lenExp = strlen(valueExp);
    if (tstClipConvertSelection(pcszTarget, &type, &value, &length, &format))
    {
        if (   type != typeExp
            || length != lenExp
            || format != 8
            || memcmp((const void *) value, (const void *)valueExp,
                      lenExp))
        {
            RTTestFailed(hTest, "Bad data: type %d, (expected %d), length %u, (expected %u), format %d (expected %d), value \"%.*s\" (expected \"%.*s\")\n",
                     type, typeExp, length, lenExp, format, 8,
                     RT_MIN(length, 20), value, RT_MIN(lenExp, 20), valueExp);
        }
        else
            retval = true;
    }
    else
        RTTestFailed(hTest, "Conversion failed\n");
    XtFree((char *)value);
    if (!retval)
        RTTestFailureDetails(hTest, "Conversion to %s, expected \"%s\"\n",
                             pcszTarget, valueExp);
}

static void tstStringFromVBoxFailed(RTTEST hTest, PSHCLX11CTX pCtx, const char *pcszTarget)
{
    RT_NOREF(pCtx);
    Atom type;
    XtPointer value = NULL;
    unsigned long length;
    int format;
    RTTEST_CHECK_MSG(hTest, !tstClipConvertSelection(pcszTarget, &type, &value,
                                                     &length, &format),
                     (hTest, "Conversion to target %s, should have failed but didn't, returned type %d, length %u, format %d, value \"%.*s\"\n",
                      pcszTarget, type, length, format, RT_MIN(length, 20),
                      value));
    XtFree((char *)value);
}

static void tstNoSelectionOwnership(PSHCLX11CTX pCtx, const char *pcszTestCtx)
{
    RT_NOREF(pCtx);
    RTTESTI_CHECK_MSG(!g_tst_fOwnsSel, ("context: %s\n", pcszTestCtx));
}

static void tstBadFormatRequestFromHost(RTTEST hTest, PSHCLX11CTX pCtx)
{
    tstClipSetSelectionValues("UTF8_STRING", XA_STRING, "hello world",
                           sizeof("hello world"), 8);
    tstClipSendTargetUpdate(pCtx);
    if (tstClipQueryFormats() != VBOX_SHCL_FMT_UNICODETEXT)
        RTTestFailed(hTest, "Wrong targets reported: %02X\n",
                     tstClipQueryFormats());
    else
    {
        uint32_t cbActual = 0;
        uint8_t  abBuf[TESTCASE_MAX_BUF_SIZE];
        int rc = ShClX11ReadDataFromX11(pCtx, &g_EventSource, g_msTimeout, 0xF000 /* vboxFormat */, abBuf, sizeof(abBuf), &cbActual);
        if (rc != VERR_NOT_IMPLEMENTED)
            RTTestFailed(hTest, "Wrong return code, expected VERR_NOT_IMPLEMENTED, got %Rrc\n",
                         rc);
        tstClipSetSelectionValues("", XA_STRING, "", sizeof(""), 8);
        tstClipSendTargetUpdate(pCtx);
        if (tstClipQueryFormats() == VBOX_SHCL_FMT_UNICODETEXT)
            RTTestFailed(hTest, "Failed to report targets after bad host request.\n");
    }
}

int main()
{
    /*
     * Init the runtime, test and say hello.
     */
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstClipboardGH-X11", &hTest);
    if (RT_FAILURE(rc))
        return RTEXITCODE_FAILURE;
    RTTestBanner(hTest);

    /*
     * Run the tests.
     */
    SHCLCALLBACKS Callbacks;
    RT_ZERO(Callbacks);
    Callbacks.pfnReportFormats           = tstShClReportFormatsCallback;
    Callbacks.pfnOnRequestDataFromSource = tstShClOnRequestDataFromSourceCallback;
    Callbacks.pfnOnSendDataToDest        = tstShClOnSendDataToDestCallback;

    SHCLX11CTX X11Ctx;
    rc = ShClX11Init(&X11Ctx, &Callbacks, NULL /* pParent */, false /* fHeadless */);
    AssertRCReturn(rc, RTEXITCODE_FAILURE);

    uint32_t cbActual = 0;
    uint8_t  abBuf[TESTCASE_MAX_BUF_SIZE];

    RTTESTI_CHECK_RC_OK(ShClEventSourceCreate(&g_EventSource, 0 /* ID */));

    /* UTF-8 from X11 */
    RTTestSub(hTest, "reading UTF-8 from X11");
    /* Simple test */
    tstClipSetSelectionValues("UTF8_STRING", XA_STRING, "hello world", sizeof("hello world"), 8);
    tstStringFromX11(hTest, &X11Ctx, "hello world", VINF_SUCCESS);

    /* With an embedded carriage return */
    tstClipSetSelectionValues("text/plain;charset=UTF-8", XA_STRING,
                              "hello\nworld", sizeof("hello\nworld"), 8);
    tstStringFromX11(hTest, &X11Ctx, "hello\r\nworld", VINF_SUCCESS);
    /* With an embedded CRLF */
    tstClipSetSelectionValues("text/plain;charset=UTF-8", XA_STRING,
                              "hello\r\nworld", sizeof("hello\r\nworld"), 8);
    tstStringFromX11(hTest, &X11Ctx, "hello\r\nworld", VINF_SUCCESS);
    /* With an embedded LFCR */
    tstClipSetSelectionValues("text/plain;charset=UTF-8", XA_STRING,
                              "hello\n\rworld", sizeof("hello\n\rworld"), 8);
    tstStringFromX11(hTest, &X11Ctx, "hello\r\n\rworld", VINF_SUCCESS);
    /* An empty string */
    tstClipSetSelectionValues("text/plain;charset=utf-8", XA_STRING, "",
                              sizeof(""), 8);
    tstStringFromX11(hTest, &X11Ctx, "", VINF_SUCCESS);
    /* With an embedded UTF-8 character. */
    tstClipSetSelectionValues("STRING", XA_STRING,
                              "100\xE2\x82\xAC" /* 100 Euro */,
                              sizeof("100\xE2\x82\xAC"), 8);
    tstStringFromX11(hTest, &X11Ctx, "100\xE2\x82\xAC", VINF_SUCCESS);
    /* A non-zero-terminated string */
    tstClipSetSelectionValues("TEXT", XA_STRING,
                              "hello world", sizeof("hello world") - 1, 8);
    tstStringFromX11(hTest, &X11Ctx, "hello world", VINF_SUCCESS);

    /* Latin1 from X11 */
    RTTestSub(hTest, "reading Latin1 from X11");
    /* Simple test */
    tstClipSetSelectionValues("STRING", XA_STRING, "Georges Dupr\xEA",
                              sizeof("Georges Dupr\xEA"), 8);
    tstLatin1FromX11(hTest, &X11Ctx, "Georges Dupr\xEA", VINF_SUCCESS);
    /* With an embedded carriage return */
    tstClipSetSelectionValues("TEXT", XA_STRING, "Georges\nDupr\xEA",
                              sizeof("Georges\nDupr\xEA"), 8);
    tstLatin1FromX11(hTest, &X11Ctx, "Georges\r\nDupr\xEA", VINF_SUCCESS);
    /* With an embedded CRLF */
    tstClipSetSelectionValues("TEXT", XA_STRING, "Georges\r\nDupr\xEA",
                              sizeof("Georges\r\nDupr\xEA"), 8);
    tstLatin1FromX11(hTest, &X11Ctx, "Georges\r\r\nDupr\xEA", VINF_SUCCESS);
    /* With an embedded LFCR */
    tstClipSetSelectionValues("TEXT", XA_STRING, "Georges\n\rDupr\xEA",
                              sizeof("Georges\n\rDupr\xEA"), 8);
    tstLatin1FromX11(hTest, &X11Ctx, "Georges\r\n\rDupr\xEA", VINF_SUCCESS);
    /* A non-zero-terminated string */
    tstClipSetSelectionValues("text/plain", XA_STRING,
                              "Georges Dupr\xEA!",
                              sizeof("Georges Dupr\xEA!") - 1, 8);
    tstLatin1FromX11(hTest, &X11Ctx, "Georges Dupr\xEA!", VINF_SUCCESS);

    /*
     * Unknown X11 format
     */
    RTTestSub(hTest, "handling of an unknown X11 format");
    tstClipInvalidateFormats();
    tstClipSetSelectionValues("CLIPBOARD", XA_STRING, "Test",
                              sizeof("Test"), 8);
    tstClipSendTargetUpdate(&X11Ctx);
    RTTEST_CHECK_MSG(hTest, tstClipQueryFormats() == 0,
                     (hTest, "Failed to send a format update notification\n"));

    /*
     * Timeout from X11
     */
    RTTestSub(hTest, "X11 conversion failure (timeout)");
    tstClipSetSelectionValues("UTF8_STRING", XT_CONVERT_FAIL, NULL,0, 8);
    tstStringFromX11(hTest, &X11Ctx, "", VERR_SHCLPB_NO_DATA);

    /*
     * No data in X11 clipboard
     */
    RTTestSub(hTest, "a data request from an empty X11 clipboard");
    tstClipSetSelectionValues("UTF8_STRING", XA_STRING, NULL, 0, 8);
    rc = ShClX11ReadDataFromX11(&X11Ctx, &g_EventSource, g_msTimeout, VBOX_SHCL_FMT_UNICODETEXT, abBuf, sizeof(abBuf), &cbActual);
    RTTEST_CHECK_MSG(hTest, rc == VERR_SHCLPB_NO_DATA,
                     (hTest, "Returned %Rrc instead of VERR_SHCLPB_NO_DATA\n",
                      rc));

    /*
     * Ensure that VBox is notified when we return the CB to X11
     */
    RTTestSub(hTest, "notification of switch to X11 clipboard");
    tstClipInvalidateFormats();
    clipReportEmpty(&X11Ctx);
    RTTEST_CHECK_MSG(hTest, tstClipQueryFormats() == 0,
                     (hTest, "Failed to send a format update (release) notification\n"));

    /*
     * Request for an invalid VBox format from X11
     */
    RTTestSub(hTest, "a request for an invalid VBox format from X11");
    /* Testing for 0xffff will go into handling VBOX_SHCL_FMT_UNICODETEXT, where we don't have
     * have any data at the moment so far, so this will return VERR_SHCLPB_NO_DATA. */
    rc = ShClX11ReadDataFromX11(&X11Ctx, &g_EventSource, g_msTimeout, 0xffff /* vboxFormat */, abBuf, sizeof(abBuf), &cbActual);
    RTTEST_CHECK_MSG(hTest, rc == VERR_SHCLPB_NO_DATA,
                     (hTest, "Returned %Rrc instead of VERR_SHCLPB_NO_DATA\n",
                      rc));
    /*
     * Targets failure from X11
     */
    RTTestSub(hTest, "X11 targets conversion failure");
    tstClipSetSelectionValues("UTF8_STRING", XA_STRING, "hello world",
                              sizeof("hello world"), 8);
    tstClipSetTargetsFailure();
    Atom atom = XA_STRING;
    long unsigned int cLen = 0;
    int format = 8;
    clipQueryX11TargetsCallback(NULL, (XtPointer) &X11Ctx, NULL, &atom, NULL, &cLen,
                                &format);
    RTTEST_CHECK_MSG(hTest, tstClipQueryFormats() == 0,
                     (hTest, "Wrong targets reported: %02X\n",
                      tstClipQueryFormats()));

    /*
     * X11 text format conversion
     */
    RTTestSub(hTest, "handling of X11 selection targets");
    RTTEST_CHECK_MSG(hTest, tstClipTextFormatConversion(&X11Ctx),
                     (hTest, "failed to select the right X11 text formats\n"));
    /*
     * UTF-8 from VBox
     */
    RTTestSub(hTest, "reading UTF-8 from VBox");
    /* Simple test */
    tstClipSetVBoxUtf16(&X11Ctx, VINF_SUCCESS, "hello world",
                        sizeof("hello world") * 2);
    tstStringFromVBox(hTest, &X11Ctx, "UTF8_STRING",
                      clipGetAtom(&X11Ctx, "UTF8_STRING"), "hello world");
    /* With an embedded carriage return */
    tstClipSetVBoxUtf16(&X11Ctx, VINF_SUCCESS, "hello\r\nworld",
                        sizeof("hello\r\nworld") * 2);
    tstStringFromVBox(hTest, &X11Ctx, "text/plain;charset=UTF-8",
                      clipGetAtom(&X11Ctx, "text/plain;charset=UTF-8"),
                      "hello\nworld");
    /* With an embedded CRCRLF */
    tstClipSetVBoxUtf16(&X11Ctx, VINF_SUCCESS, "hello\r\r\nworld",
                        sizeof("hello\r\r\nworld") * 2);
    tstStringFromVBox(hTest, &X11Ctx, "text/plain;charset=UTF-8",
                      clipGetAtom(&X11Ctx, "text/plain;charset=UTF-8"),
                      "hello\r\nworld");
    /* With an embedded CRLFCR */
    tstClipSetVBoxUtf16(&X11Ctx, VINF_SUCCESS, "hello\r\n\rworld",
                        sizeof("hello\r\n\rworld") * 2);
    tstStringFromVBox(hTest, &X11Ctx, "text/plain;charset=UTF-8",
                      clipGetAtom(&X11Ctx, "text/plain;charset=UTF-8"),
                      "hello\n\rworld");
    /* An empty string */
    tstClipSetVBoxUtf16(&X11Ctx, VINF_SUCCESS, "", 2);
    tstStringFromVBox(hTest, &X11Ctx, "text/plain;charset=utf-8",
                      clipGetAtom(&X11Ctx, "text/plain;charset=utf-8"), "");
    /* With an embedded UTF-8 character. */
    tstClipSetVBoxUtf16(&X11Ctx, VINF_SUCCESS, "100\xE2\x82\xAC" /* 100 Euro */,
                        10);
    tstStringFromVBox(hTest, &X11Ctx, "STRING",
                      clipGetAtom(&X11Ctx, "STRING"), "100\xE2\x82\xAC");
    /* A non-zero-terminated string */
    tstClipSetVBoxUtf16(&X11Ctx, VINF_SUCCESS, "hello world",
                        sizeof("hello world") * 2 - 2);
    tstStringFromVBox(hTest, &X11Ctx, "TEXT", clipGetAtom(&X11Ctx, "TEXT"),
                     "hello world");

    /*
     * Timeout from VBox
     */
    RTTestSub(hTest, "reading from VBox with timeout");
    tstClipEmptyVBox(&X11Ctx, VERR_TIMEOUT);
    tstStringFromVBoxFailed(hTest, &X11Ctx, "UTF8_STRING");

    /*
     * No data in VBox clipboard
     */
    RTTestSub(hTest, "an empty VBox clipboard");
    tstClipSetSelectionValues("TEXT", XA_STRING, "", sizeof(""), 8);
    tstClipEmptyVBox(&X11Ctx, VINF_SUCCESS);
    RTTEST_CHECK_MSG(hTest, g_tst_fOwnsSel,
                     (hTest, "VBox grabbed the clipboard with no data and we ignored it\n"));
    tstStringFromVBoxFailed(hTest, &X11Ctx, "UTF8_STRING");

    /*
     * An unknown VBox format
     */
    RTTestSub(hTest, "reading an unknown VBox format");
    tstClipSetSelectionValues("TEXT", XA_STRING, "", sizeof(""), 8);
    tstClipSetVBoxUtf16(&X11Ctx, VINF_SUCCESS, "", 2);
    ShClX11ReportFormatsToX11Async(&X11Ctx, 0xa0000);
    RTTEST_CHECK_MSG(hTest, g_tst_fOwnsSel,
                     (hTest, "VBox grabbed the clipboard with unknown data and we ignored it\n"));
    tstStringFromVBoxFailed(hTest, &X11Ctx, "UTF8_STRING");

    /*
     * VBox requests a bad format
     */
    RTTestSub(hTest, "recovery from a bad format request");
    tstBadFormatRequestFromHost(hTest, &X11Ctx);

    ShClX11Destroy(&X11Ctx);

    /*
     * Headless clipboard tests
     */
    RTTEST_CHECK_RC_OK(hTest, ShClX11Init(&X11Ctx, &Callbacks, NULL /* pParent */, true /* fHeadless */));

    /* Read from X11 */
    RTTestSub(hTest, "reading from X11, headless clipboard");

    /* Simple test */
    tstClipSetVBoxUtf16(&X11Ctx, VINF_SUCCESS, "", sizeof("") * 2);
    tstClipSetSelectionValues("UTF8_STRING", XA_STRING, "hello world", sizeof("hello world"), 8);
    rc = ShClX11ReadDataFromX11(&X11Ctx, &g_EventSource, g_msTimeout, VBOX_SHCL_FMT_UNICODETEXT, abBuf, sizeof(abBuf), &cbActual);
    RTTEST_CHECK_MSG(hTest, cbActual == 0, (hTest, "expected 0 but got %RU32\n", cbActual));
    RTTEST_CHECK_MSG(hTest, rc == VINF_SUCCESS, (hTest, "expected VINF_SUCCESS but got %Rrc\n", rc));

    /* Read from VBox */
    RTTestSub(hTest, "reading from VBox, headless clipboard");

    /* Simple test */
    tstClipEmptyVBox(&X11Ctx, VERR_WRONG_ORDER);
    tstClipSetSelectionValues("TEXT", XA_STRING, "", sizeof(""), 8);
    tstClipSetVBoxUtf16(&X11Ctx, VINF_SUCCESS, "hello world",
                        sizeof("hello world") * 2);
    tstNoSelectionOwnership(&X11Ctx, "reading from VBox, headless clipboard");

    RTTEST_CHECK_RC_OK(hTest, ShClX11Destroy(&X11Ctx));
    ShClEventSourceDestroy(&g_EventSource);

    return RTTestSummaryAndDestroy(hTest);
}

