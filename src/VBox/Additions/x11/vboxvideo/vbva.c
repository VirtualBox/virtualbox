/* $Id: vbva.c 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox X11 Additions graphics driver 2D acceleration functions
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

#if defined(IN_XF86_MODULE) && !defined(NO_ANSIC)
# include "xf86_ansic.h"
#endif
#include "compiler.h"

#include "vboxvideo.h"

#ifdef XORG_7X
# include <stdlib.h>
# include <string.h>
#endif

/**************************************************************************
* Main functions                                                          *
**************************************************************************/

/**
 * Callback function called by the X server to tell us about dirty
 * rectangles in the video buffer.
 *
 * @param pScrn   pointer to the information structure for the current
 *                screen
 * @param iRects  Number of dirty rectangles to update
 * @param aRects  Array of structures containing the coordinates of the
 *                rectangles
 */
void vbvxHandleDirtyRect(ScrnInfoPtr pScrn, int iRects, BoxPtr aRects)
{
    VBVACMDHDR cmdHdr;
    VBOXPtr pVBox;
    int i;
    unsigned j;

    pVBox = pScrn->driverPrivate;
    if (!pScrn->vtSema)
        return;

    for (j = 0; j < pVBox->cScreens; ++j)
    {
        /* Just continue quietly if VBVA is not currently active. */
        struct VBVABUFFER *pVBVA = pVBox->pScreens[j].aVbvaCtx.pVBVA;
        if (   !pVBVA
            || !(pVBVA->hostFlags.u32HostEvents & VBVA_F_MODE_ENABLED))
            continue;
        for (i = 0; i < iRects; ++i)
        {
            if (   aRects[i].x1 >   pVBox->pScreens[j].aScreenLocation.x
                                  + pVBox->pScreens[j].aScreenLocation.cx
                || aRects[i].y1 >   pVBox->pScreens[j].aScreenLocation.y
                                  + pVBox->pScreens[j].aScreenLocation.cy
                || aRects[i].x2 <   pVBox->pScreens[j].aScreenLocation.x
                || aRects[i].y2 <   pVBox->pScreens[j].aScreenLocation.y)
                continue;
            cmdHdr.x = (int16_t)aRects[i].x1 - pVBox->pScreens[0].aScreenLocation.x;
            cmdHdr.y = (int16_t)aRects[i].y1 - pVBox->pScreens[0].aScreenLocation.y;
            cmdHdr.w = (uint16_t)(aRects[i].x2 - aRects[i].x1);
            cmdHdr.h = (uint16_t)(aRects[i].y2 - aRects[i].y1);

#if 0
            TRACE_LOG("display=%u, x=%d, y=%d, w=%d, h=%d\n",
                      j, cmdHdr.x, cmdHdr.y, cmdHdr.w, cmdHdr.h);
#endif

            if (VBoxVBVABufferBeginUpdate(&pVBox->pScreens[j].aVbvaCtx,
                                          &pVBox->guestCtx))
            {
                VBoxVBVAWrite(&pVBox->pScreens[j].aVbvaCtx, &pVBox->guestCtx, &cmdHdr,
                              sizeof(cmdHdr));
                VBoxVBVABufferEndUpdate(&pVBox->pScreens[j].aVbvaCtx);
            }
        }
    }
}

static DECLCALLBACK(void *) hgsmiEnvAlloc(void *pvEnv, HGSMISIZE cb)
{
    RT_NOREF(pvEnv);
    return calloc(1, cb);
}

static DECLCALLBACK(void) hgsmiEnvFree(void *pvEnv, void *pv)
{
    RT_NOREF(pvEnv);
    free(pv);
}

static HGSMIENV g_hgsmiEnv =
{
    NULL,
    hgsmiEnvAlloc,
    hgsmiEnvFree
};

/**
 * Calculate the location in video RAM of and initialise the heap for guest to
 * host messages.
 */
void vbvxSetUpHGSMIHeapInGuest(VBOXPtr pVBox, uint32_t cbVRAM)
{
    int rc;
    uint32_t offVRAMBaseMapping, offGuestHeapMemory, cbGuestHeapMemory;
    void *pvGuestHeapMemory;

    VBoxHGSMIGetBaseMappingInfo(cbVRAM, &offVRAMBaseMapping, NULL, &offGuestHeapMemory, &cbGuestHeapMemory, NULL);
    pvGuestHeapMemory = ((uint8_t *)pVBox->base) + offVRAMBaseMapping + offGuestHeapMemory;
    rc = VBoxHGSMISetupGuestContext(&pVBox->guestCtx, pvGuestHeapMemory, cbGuestHeapMemory,
                                    offVRAMBaseMapping + offGuestHeapMemory, &g_hgsmiEnv);
    AssertMsg(RT_SUCCESS(rc), ("Failed to set up the guest-to-host message buffer heap, rc=%d\n", rc));
    pVBox->cbView = offVRAMBaseMapping;
}

/** Callback to fill in the view structures */
static DECLCALLBACK(int) vboxFillViewInfo(void *pvVBox, struct VBVAINFOVIEW *pViews, uint32_t cViews)
{
    VBOXPtr pVBox = (VBOXPtr)pvVBox;
    unsigned i;
    for (i = 0; i < cViews; ++i)
    {
        pViews[i].u32ViewIndex = i;
        pViews[i].u32ViewOffset = 0;
        pViews[i].u32ViewSize = pVBox->cbView;
        pViews[i].u32MaxScreenSize = pVBox->cbFBMax;
    }
    return VINF_SUCCESS;
}

/**
 * Initialise VirtualBox's accelerated video extensions.
 *
 * @returns TRUE on success, FALSE on failure
 */
static Bool vboxSetupVRAMVbva(VBOXPtr pVBox)
{
    int rc = VINF_SUCCESS;
    unsigned i;

    pVBox->cbFBMax = pVBox->cbView;
    for (i = 0; i < pVBox->cScreens; ++i)
    {
        pVBox->cbFBMax -= VBVA_MIN_BUFFER_SIZE;
        pVBox->pScreens[i].aoffVBVABuffer = pVBox->cbFBMax;
        TRACE_LOG("VBVA buffer offset for screen %u: 0x%lx\n", i,
                  (unsigned long) pVBox->cbFBMax);
        VBoxVBVASetupBufferContext(&pVBox->pScreens[i].aVbvaCtx,
                                   pVBox->pScreens[i].aoffVBVABuffer,
                                   VBVA_MIN_BUFFER_SIZE);
    }
    TRACE_LOG("Maximum framebuffer size: %lu (0x%lx)\n",
              (unsigned long) pVBox->cbFBMax,
              (unsigned long) pVBox->cbFBMax);
    rc = VBoxHGSMISendViewInfo(&pVBox->guestCtx, pVBox->cScreens,
                               vboxFillViewInfo, (void *)pVBox);
    AssertMsg(RT_SUCCESS(rc), ("Failed to send the view information to the host, rc=%d\n", rc));
    return TRUE;
}

static Bool haveHGSMIModeHintAndCursorReportingInterface(VBOXPtr pVBox)
{
    uint32_t fModeHintReporting, fCursorReporting;

    return    RT_SUCCESS(VBoxQueryConfHGSMI(&pVBox->guestCtx, VBOX_VBVA_CONF32_MODE_HINT_REPORTING, &fModeHintReporting))
           && RT_SUCCESS(VBoxQueryConfHGSMI(&pVBox->guestCtx, VBOX_VBVA_CONF32_GUEST_CURSOR_REPORTING, &fCursorReporting))
           && fModeHintReporting == VINF_SUCCESS
           && fCursorReporting == VINF_SUCCESS;
}

static Bool hostHasScreenBlankingFlag(VBOXPtr pVBox)
{
    uint32_t fScreenFlags;

    return    RT_SUCCESS(VBoxQueryConfHGSMI(&pVBox->guestCtx, VBOX_VBVA_CONF32_SCREEN_FLAGS, &fScreenFlags))
           && fScreenFlags & VBVA_SCREEN_F_BLANK;
}

/**
 * Inform VBox that we will supply it with dirty rectangle information
 * and install the dirty rectangle handler.
 *
 * @returns TRUE for success, FALSE for failure
 * @param   pScrn   Pointer to a structure describing the X screen in use
 */
Bool
vboxEnableVbva(ScrnInfoPtr pScrn)
{
    Bool rc = TRUE;
    unsigned i;
    VBOXPtr pVBox = pScrn->driverPrivate;

    TRACE_ENTRY();
    if (!vboxSetupVRAMVbva(pVBox))
        return FALSE;
    for (i = 0; i < pVBox->cScreens; ++i)
    {
        struct VBVABUFFER *pVBVA;

        pVBVA = (struct VBVABUFFER *) (  ((uint8_t *)pVBox->base)
                                       + pVBox->pScreens[i].aoffVBVABuffer);
        if (!VBoxVBVAEnable(&pVBox->pScreens[i].aVbvaCtx, &pVBox->guestCtx,
                            pVBVA, i))
            rc = FALSE;
    }
    AssertMsg(rc, ("Failed to enable screen update reporting for at least one virtual monitor.\n"));
    pVBox->fHaveHGSMIModeHints = haveHGSMIModeHintAndCursorReportingInterface(pVBox);
    pVBox->fHostHasScreenBlankingFlag = hostHasScreenBlankingFlag(pVBox);
    return rc;
}

/**
 * Inform VBox that we will stop supplying it with dirty rectangle
 * information. This function is intended to be called when an X
 * virtual terminal is disabled, or the X server is terminated.
 *
 * @param   pScrn   Pointer to a structure describing the X screen in use
 */
void
vboxDisableVbva(ScrnInfoPtr pScrn)
{
    unsigned i;
    VBOXPtr pVBox = pScrn->driverPrivate;

    TRACE_ENTRY();
    for (i = 0; i < pVBox->cScreens; ++i)
        VBoxVBVADisable(&pVBox->pScreens[i].aVbvaCtx, &pVBox->guestCtx, i);
}
