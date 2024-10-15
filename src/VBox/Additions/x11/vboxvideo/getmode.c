/* $Id: getmode.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox X11 Additions graphics driver dynamic video mode functions.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "vboxvideo.h"

#define NEED_XF86_TYPES
#include "xf86.h"

#ifdef XORG_7X
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
#endif

#ifdef VBOXVIDEO_13
# ifdef RT_OS_LINUX
#  include <linux/input.h>
#  ifndef EVIOCGRAB
#   define EVIOCGRAB _IOW('E', 0x90, int)
#  endif
#  ifndef KEY_SWITCHVIDEOMODE
#   define KEY_SWITCHVIDEOMODE 227
#  endif
#  include <dirent.h>
#  include <errno.h>
#  include <fcntl.h>
#  include <unistd.h>
# endif /* RT_OS_LINUX */
#endif /* VBOXVIDEO_13 */

/**************************************************************************
* Main functions                                                          *
**************************************************************************/

/**
 * Fills a display mode M with a built-in mode of name pszName and dimensions
 * cx and cy.
 */
static void vboxFillDisplayMode(ScrnInfoPtr pScrn, DisplayModePtr m,
                                const char *pszName, unsigned cx, unsigned cy)
{
    VBOXPtr pVBox = pScrn->driverPrivate;
    char szName[256];
    DisplayModePtr pPrev = m->prev;
    DisplayModePtr pNext = m->next;

    if (!pszName)
    {
        sprintf(szName, "%ux%u", cx, cy);
        pszName = szName;
    }
    TRACE_LOG("pszName=%s, cx=%u, cy=%u\n", pszName, cx, cy);
    if (m->name)
        free((void*)m->name);
    memset(m, '\0', sizeof(*m));
    m->prev          = pPrev;
    m->next          = pNext;
    m->status        = MODE_OK;
    m->type          = M_T_BUILTIN;
    /* Older versions of VBox only support screen widths which are a multiple
     * of 8 */
    if (pVBox->fAnyX)
        m->HDisplay  = cx;
    else
        m->HDisplay  = cx & ~7;
    m->HSyncStart    = m->HDisplay + 2;
    m->HSyncEnd      = m->HDisplay + 4;
    m->HTotal        = m->HDisplay + 6;
    m->VDisplay      = cy;
    m->VSyncStart    = m->VDisplay + 2;
    m->VSyncEnd      = m->VDisplay + 4;
    m->VTotal        = m->VDisplay + 6;
    m->Clock         = m->HTotal * m->VTotal * 60 / 1000; /* kHz */
    m->name      = xnfstrdup(pszName);
}

/**
 * Allocates an empty display mode and links it into the doubly linked list of
 * modes pointed to by pScrn->modes.  Returns a pointer to the newly allocated
 * memory.
 */
static DisplayModePtr vboxAddEmptyScreenMode(ScrnInfoPtr pScrn)
{
    DisplayModePtr pMode = xnfcalloc(sizeof(DisplayModeRec), 1);

    TRACE_ENTRY();
    if (!pScrn->modes)
    {
        pScrn->modes = pMode;
        pMode->next = pMode;
        pMode->prev = pMode;
    }
    else
    {
        pMode->next = pScrn->modes;
        pMode->prev = pScrn->modes->prev;
        pMode->next->prev = pMode;
        pMode->prev->next = pMode;
    }
    return pMode;
}

/**
 * Create display mode entries in the screen information structure for each
 * of the graphics modes that we wish to support, that is:
 *  - A dynamic mode in first place which will be updated by the RandR code.
 *  - Any modes that the user requested in xorg.conf/XFree86Config.
 */
void vboxAddModes(ScrnInfoPtr pScrn)
{
    unsigned cx = 0;
    unsigned cy = 0;
    unsigned i;
    DisplayModePtr pMode;

    /* Add two dynamic mode entries.  When we receive a new size hint we will
     * update whichever of these is not current. */
    pMode = vboxAddEmptyScreenMode(pScrn);
    vboxFillDisplayMode(pScrn, pMode, NULL, 800, 600);
    pMode = vboxAddEmptyScreenMode(pScrn);
    vboxFillDisplayMode(pScrn, pMode, NULL, 800, 600);
    /* Add any modes specified by the user.  We assume here that the mode names
     * reflect the mode sizes. */
    for (i = 0; pScrn->display->modes && pScrn->display->modes[i]; i++)
    {
        if (sscanf(pScrn->display->modes[i], "%ux%u", &cx, &cy) == 2)
        {
            pMode = vboxAddEmptyScreenMode(pScrn);
            vboxFillDisplayMode(pScrn, pMode, pScrn->display->modes[i], cx, cy);
        }
    }
}

/** Set the initial values for the guest screen size hints to standard values
 * in case nothing else is available. */
void VBoxInitialiseSizeHints(ScrnInfoPtr pScrn)
{
    VBOXPtr pVBox = VBOXGetRec(pScrn);
    unsigned i;

    for (i = 0; i < pVBox->cScreens; ++i)
    {
        pVBox->pScreens[i].aPreferredSize.cx = 800;
        pVBox->pScreens[i].aPreferredSize.cy = 600;
        pVBox->pScreens[i].afConnected       = true;
    }
    /* Set up the first mode correctly to match the requested initial mode. */
    pScrn->modes->HDisplay = pVBox->pScreens[0].aPreferredSize.cx;
    pScrn->modes->VDisplay = pVBox->pScreens[0].aPreferredSize.cy;
}

static Bool useHardwareCursor(uint32_t fCursorCapabilities)
{
    if (fCursorCapabilities & VBOX_VBVA_CURSOR_CAPABILITY_HARDWARE)
        return true;
    return false;
}

static void compareAndMaybeSetUseHardwareCursor(VBOXPtr pVBox, uint32_t fCursorCapabilities, Bool *pfChanged, Bool fSet)
{
    if (pVBox->fUseHardwareCursor != useHardwareCursor(fCursorCapabilities))
        *pfChanged = true;
    if (fSet)
        pVBox->fUseHardwareCursor = useHardwareCursor(fCursorCapabilities);
}

#define COMPARE_AND_MAYBE_SET(pDest, src, pfChanged, fSet) \
do { \
    if (*(pDest) != (src)) \
    { \
        if (fSet) \
            *(pDest) = (src); \
        *(pfChanged) = true; \
    } \
} while(0)

/** Read in information about the most recent size hints and cursor
 * capabilities requested for the guest screens from HGSMI. */
void vbvxReadSizesAndCursorIntegrationFromHGSMI(ScrnInfoPtr pScrn, Bool *pfNeedUpdate)
{
    VBOXPtr pVBox = VBOXGetRec(pScrn);
    int rc;
    unsigned i;
    Bool fChanged = false;
    uint32_t fCursorCapabilities;

    if (!pVBox->fHaveHGSMIModeHints)
        return;
    rc = VBoxHGSMIGetModeHints(&pVBox->guestCtx, pVBox->cScreens, pVBox->paVBVAModeHints);
    AssertMsg(rc == VINF_SUCCESS, ("VBoxHGSMIGetModeHints failed, rc=%d.\n", rc));
    for (i = 0; i < pVBox->cScreens; ++i)
        if (pVBox->paVBVAModeHints[i].magic == VBVAMODEHINT_MAGIC)
        {
            COMPARE_AND_MAYBE_SET(&pVBox->pScreens[i].aPreferredSize.cx, pVBox->paVBVAModeHints[i].cx & 0x8fff, &fChanged, true);
            COMPARE_AND_MAYBE_SET(&pVBox->pScreens[i].aPreferredSize.cy, pVBox->paVBVAModeHints[i].cy & 0x8fff, &fChanged, true);
            COMPARE_AND_MAYBE_SET(&pVBox->pScreens[i].afConnected, RT_BOOL(pVBox->paVBVAModeHints[i].fEnabled), &fChanged, true);
            COMPARE_AND_MAYBE_SET(&pVBox->pScreens[i].aPreferredLocation.x, (int32_t)pVBox->paVBVAModeHints[i].dx & 0x8fff, &fChanged,
                                  true);
            COMPARE_AND_MAYBE_SET(&pVBox->pScreens[i].aPreferredLocation.y, (int32_t)pVBox->paVBVAModeHints[i].dy & 0x8fff, &fChanged,
                                  true);
            if (pVBox->paVBVAModeHints[i].dx != ~(uint32_t)0 && pVBox->paVBVAModeHints[i].dy != ~(uint32_t)0)
                COMPARE_AND_MAYBE_SET(&pVBox->pScreens[i].afHaveLocation, true, &fChanged, true);
            else
                COMPARE_AND_MAYBE_SET(&pVBox->pScreens[i].afHaveLocation, false, &fChanged, true);
        }
    rc = VBoxQueryConfHGSMI(&pVBox->guestCtx, VBOX_VBVA_CONF32_CURSOR_CAPABILITIES, &fCursorCapabilities);
    AssertMsg(rc == VINF_SUCCESS, ("Getting VBOX_VBVA_CONF32_CURSOR_CAPABILITIES failed, rc=%d.\n", rc));
    compareAndMaybeSetUseHardwareCursor(pVBox, fCursorCapabilities, &fChanged, true);
    if (pfNeedUpdate != NULL && fChanged)
        *pfNeedUpdate = true;
}

#undef COMPARE_AND_MAYBE_SET

#ifdef VBOXVIDEO_13
# ifdef RT_OS_LINUX
/** We have this for two purposes: one is to ensure that the X server is woken
 * up when we get a video ACPI event.  Two is to grab ACPI video events to
 * prevent gnome-settings-daemon from seeing them, as older versions ignored
 * the time stamp and handled them at the wrong time. */
static void acpiEventHandler(int fd, void *pvData)
{
    struct input_event event;
    ssize_t rc;
    RT_NOREF(pvData);

    do
        rc = read(fd, &event, sizeof(event));
    while (rc > 0 || (rc == -1 && errno == EINTR));
    /* Why do they return EAGAIN instead of zero bytes read like everyone else does? */
    AssertMsg(rc != -1 || errno == EAGAIN, ("Reading ACPI input event failed.\n"));
}

void vbvxSetUpLinuxACPI(ScreenPtr pScreen)
{
    static const char s_szDevInput[] = "/dev/input/";
    struct dirent *pDirent;
    char szFile[sizeof(s_szDevInput) + sizeof(pDirent->d_name) + 16];
    VBOXPtr pVBox = VBOXGetRec(xf86Screens[pScreen->myNum]);
    DIR *pDir;
    int fd = -1;

    if (pVBox->fdACPIDevices != -1 || pVBox->hACPIEventHandler != NULL)
        FatalError("ACPI input file descriptor not initialised correctly.\n");
    pDir = opendir("/dev/input");
    if (pDir == NULL)
        return;
    memcpy(szFile, s_szDevInput, sizeof(s_szDevInput));
    for (pDirent = readdir(pDir); pDirent != NULL; pDirent = readdir(pDir))
    {
        if (strncmp(pDirent->d_name, "event", sizeof("event") - 1) == 0)
        {
#define BITS_PER_BLOCK (sizeof(unsigned long) * 8)
            char szDevice[64] = "";
            unsigned long afKeys[KEY_MAX / BITS_PER_BLOCK];
            size_t const cchName = strlen(pDirent->d_name);
            if (cchName + sizeof(s_szDevInput) > sizeof(szFile))
                continue;
            memcpy(&szFile[sizeof(s_szDevInput) - 1], pDirent->d_name, cchName + 1);
            if (fd != -1)
                close(fd);
            fd = open(szFile, O_RDONLY | O_NONBLOCK);
            if (   fd == -1
                || ioctl(fd, EVIOCGNAME(sizeof(szDevice)), szDevice) == -1
                || strcmp(szDevice, "Video Bus") != 0)
                continue;
            if (   ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(afKeys)), afKeys) == -1
                || ((   afKeys[KEY_SWITCHVIDEOMODE / BITS_PER_BLOCK]
                     >> KEY_SWITCHVIDEOMODE % BITS_PER_BLOCK) & 1) == 0)
                break;
            if (ioctl(fd, EVIOCGRAB, (void *)1) != 0)
                break;
            pVBox->hACPIEventHandler
                = xf86AddGeneralHandler(fd, acpiEventHandler, pScreen);
            if (pVBox->hACPIEventHandler == NULL)
                break;
            pVBox->fdACPIDevices = fd;
            fd = -1;
            break;
#undef BITS_PER_BLOCK
        }
    }
    if (fd != -1)
        close(fd);
    closedir(pDir);
}

void vbvxCleanUpLinuxACPI(ScreenPtr pScreen)
{
    VBOXPtr pVBox = VBOXGetRec(xf86Screens[pScreen->myNum]);
    if (pVBox->fdACPIDevices != -1)
        close(pVBox->fdACPIDevices);
    pVBox->fdACPIDevices = -1;
    xf86RemoveGeneralHandler(pVBox->hACPIEventHandler);
    pVBox->hACPIEventHandler = NULL;
}
# endif /* RT_OS_LINUX */
#endif /* VBOXVIDEO_13 */
