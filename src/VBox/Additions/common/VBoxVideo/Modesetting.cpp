/* $Id: Modesetting.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Video driver, common code - HGSMI initialisation and helper
 * functions.
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

#include <VBoxVideoGuest.h>
#include <VBoxVideoVBE.h>
#include <HGSMIChannels.h>

#ifndef VBOX_GUESTR3XF86MOD
# include <VBoxVideoIPRT.h>
#endif

/**
 * Gets the count of virtual monitors attached to the guest via an HGSMI
 * command
 *
 * @returns the right count on success or 1 on failure.
 * @param  pCtx  the context containing the heap to use
 */
DECLHIDDEN(uint32_t) VBoxHGSMIGetMonitorCount(PHGSMIGUESTCOMMANDCONTEXT pCtx)
{
    /* Query the configured number of displays. */
    uint32_t cDisplays = 0;
    VBoxQueryConfHGSMI(pCtx, VBOX_VBVA_CONF32_MONITOR_COUNT, &cDisplays);
    // LogFunc(("cDisplays = %d\n", cDisplays));
    if (cDisplays == 0 || cDisplays > VBOX_VIDEO_MAX_SCREENS)
        /* Host reported some bad value. Continue in the 1 screen mode. */
        cDisplays = 1;
    return cDisplays;
}


/**
 * Query whether the virtual hardware supports VBE_DISPI_ID_CFG
 * and set the interface.
 *
 * @returns Whether the interface is supported.
 */
DECLHIDDEN(bool) VBoxVGACfgAvailable(void)
{
    uint16_t DispiId;
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ID);
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_DATA, VBE_DISPI_ID_CFG);
    DispiId = VBVO_PORT_READ_U16(VBE_DISPI_IOPORT_DATA);
    return (DispiId == VBE_DISPI_ID_CFG);
}


/**
 * Query a configuration value from the virtual hardware which supports VBE_DISPI_ID_CFG.
 * I.e. use this function only if VBoxVGACfgAvailable returns true.
 *
 * @returns Whether the value is supported.
 * @param  u16Id       Identifier of the configuration value (VBE_DISPI_CFG_ID_*).
 * @param  pu32Value   Where to store value from the host.
 * @param  u32DefValue What to assign to *pu32Value if the value is not supported.
 */
DECLHIDDEN(bool) VBoxVGACfgQuery(uint16_t u16Id, uint32_t *pu32Value, uint32_t u32DefValue)
{
    uint32_t u32;
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_CFG);
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_DATA, VBE_DISPI_CFG_MASK_SUPPORT | u16Id);
    u32 = VBVO_PORT_READ_U32(VBE_DISPI_IOPORT_DATA);
    if (u32)
    {
        VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_DATA, u16Id);
        *pu32Value = VBVO_PORT_READ_U32(VBE_DISPI_IOPORT_DATA);
        return true;
    }

    *pu32Value = u32DefValue;
    return false;
}


/**
 * Returns the size of the video RAM in bytes.
 *
 * @returns the size
 */
DECLHIDDEN(uint32_t) VBoxVideoGetVRAMSize(void)
{
    /** @note A 32bit read on this port returns the VRAM size if interface is older than VBE_DISPI_ID_CFG. */
    return VBVO_PORT_READ_U32(VBE_DISPI_IOPORT_DATA);
}


/**
 * Check whether this hardware allows the display width to have non-multiple-
 * of-eight values.
 *
 * @returns true if any width is allowed, false otherwise.
 */
DECLHIDDEN(bool) VBoxVideoAnyWidthAllowed(void)
{
    unsigned DispiId;
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ID);
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_DATA, VBE_DISPI_ID_ANYX);
    DispiId = VBVO_PORT_READ_U16(VBE_DISPI_IOPORT_DATA);
    return (DispiId == VBE_DISPI_ID_ANYX);
}


/**
 * Tell the host about how VRAM is divided up between each screen via an HGSMI
 * command.  It is acceptable to specifiy identical data for each screen if
 * they share a single framebuffer.
 *
 * @returns iprt status code, either VERR_NO_MEMORY or the status returned by
 *          @a pfnFill
 * @todo  What was I thinking of with that callback function?  It
 *        would be much simpler to just pass in a structure in normal
 *        memory and copy it.
 * @param  pCtx      the context containing the heap to use
 * @param  u32Count  the number of screens we are activating
 * @param  pfnFill   a callback which initialises the VBVAINFOVIEW structures
 *                   for all screens
 * @param  pvData    context data for @a pfnFill
 */
DECLHIDDEN(int) VBoxHGSMISendViewInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                      uint32_t u32Count,
                                      PFNHGSMIFILLVIEWINFO pfnFill,
                                      void *pvData)
{
    int rc;
    /* Issue the screen info command. */
    VBVAINFOVIEW RT_UNTRUSTED_VOLATILE_HOST *pInfo =
        (VBVAINFOVIEW RT_UNTRUSTED_VOLATILE_HOST *)VBoxHGSMIBufferAlloc(pCtx, sizeof(VBVAINFOVIEW) * u32Count,
                                                                        HGSMI_CH_VBVA, VBVA_INFO_VIEW);
    if (pInfo)
    {
        rc = pfnFill(pvData, (VBVAINFOVIEW *)pInfo /* lazy bird */, u32Count);
        if (RT_SUCCESS(rc))
            VBoxHGSMIBufferSubmit(pCtx, pInfo);
        VBoxHGSMIBufferFree(pCtx, pInfo);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


/**
 * Set a video mode using port registers.  This must be done for the first
 * screen before every HGSMI modeset and also works when HGSM is not enabled.
 * @param  cWidth      the mode width
 * @param  cHeight     the mode height
 * @param  cVirtWidth  the mode pitch
 * @param  cBPP        the colour depth of the mode
 * @param  fFlags      flags for the mode.  These will be or-ed with the
 *                     default _ENABLED flag, so unless you are restoring
 *                     a saved mode or have special requirements you can pass
 *                     zero here.
 * @param  cx          the horizontal panning offset
 * @param  cy          the vertical panning offset
 */
DECLHIDDEN(void) VBoxVideoSetModeRegisters(uint16_t cWidth, uint16_t cHeight,
                                           uint16_t cVirtWidth, uint16_t cBPP,
                                           uint16_t fFlags, uint16_t cx,
                                           uint16_t cy)
{
    /* set the mode characteristics */
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_XRES);
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_DATA, cWidth);
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_YRES);
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_DATA, cHeight);
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_VIRT_WIDTH);
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_DATA, cVirtWidth);
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_BPP);
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_DATA, cBPP);
    /* enable the mode */
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ENABLE);
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_DATA, fFlags | VBE_DISPI_ENABLED);
    /* Panning registers */
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_X_OFFSET);
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_DATA, cx);
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_Y_OFFSET);
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_DATA, cy);
    /** @todo read from the port to see if the mode switch was successful */
}


/**
 * Get the video mode for the first screen using the port registers.  All
 * parameters are optional
 * @returns  true if the VBE mode returned is active, false if we are in VGA
 *           mode
 * @note  If anyone else needs additional register values just extend the
 *        function with additional parameters and fix any existing callers.
 * @param  pcWidth      where to store the mode width
 * @param  pcHeight     where to store the mode height
 * @param  pcVirtWidth  where to store the mode pitch
 * @param  pcBPP        where to store the colour depth of the mode
 * @param  pfFlags      where to store the flags for the mode
 */
DECLHIDDEN(bool) VBoxVideoGetModeRegisters(uint16_t *pcWidth, uint16_t *pcHeight,
                                           uint16_t *pcVirtWidth, uint16_t *pcBPP,
                                           uint16_t *pfFlags)
{
    uint16_t fFlags;

    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ENABLE);
    fFlags = VBVO_PORT_READ_U16(VBE_DISPI_IOPORT_DATA);
    if (pcWidth)
    {
        VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_XRES);
        *pcWidth = VBVO_PORT_READ_U16(VBE_DISPI_IOPORT_DATA);
    }
    if (pcHeight)
    {
        VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_YRES);
        *pcHeight = VBVO_PORT_READ_U16(VBE_DISPI_IOPORT_DATA);
    }
    if (pcVirtWidth)
    {
        VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_VIRT_WIDTH);
        *pcVirtWidth = VBVO_PORT_READ_U16(VBE_DISPI_IOPORT_DATA);
    }
    if (pcBPP)
    {
        VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_BPP);
        *pcBPP = VBVO_PORT_READ_U16(VBE_DISPI_IOPORT_DATA);
    }
    if (pfFlags)
        *pfFlags = fFlags;
    return RT_BOOL(fFlags & VBE_DISPI_ENABLED);
}


/**
 * Disable our extended graphics mode and go back to VGA mode.
 */
DECLHIDDEN(void) VBoxVideoDisableVBE(void)
{
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ENABLE);
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_DATA, 0);
}


/**
 * Set a video mode via an HGSMI request.  The views must have been
 * initialised first using @a VBoxHGSMISendViewInfo and if the mode is being
 * set on the first display then it must be set first using registers.
 * @param  pCtx      The context containing the heap to use.
 * @param  cDisplay  the screen number
 * @param  cOriginX  the horizontal displacement relative to the first screen
 * @param  cOriginY  the vertical displacement relative to the first screen
 * @param  offStart  the offset of the visible area of the framebuffer
 *                   relative to the framebuffer start
 * @param  cbPitch   the offset in bytes between the starts of two adjecent
 *                   scan lines in video RAM
 * @param  cWidth    the mode width
 * @param  cHeight   the mode height
 * @param  cBPP      the colour depth of the mode
 * @param  fFlags    flags
 */
DECLHIDDEN(void) VBoxHGSMIProcessDisplayInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                             uint32_t cDisplay,
                                             int32_t  cOriginX,
                                             int32_t  cOriginY,
                                             uint32_t offStart,
                                             uint32_t cbPitch,
                                             uint32_t cWidth,
                                             uint32_t cHeight,
                                             uint16_t cBPP,
                                             uint16_t fFlags)
{
    /* Issue the screen info command. */
    VBVAINFOSCREEN RT_UNTRUSTED_VOLATILE_HOST *pScreen =
        (VBVAINFOSCREEN RT_UNTRUSTED_VOLATILE_HOST *)VBoxHGSMIBufferAlloc(pCtx, sizeof(VBVAINFOSCREEN),
                                                                          HGSMI_CH_VBVA, VBVA_INFO_SCREEN);
    if (pScreen != NULL)
    {
        pScreen->u32ViewIndex    = cDisplay;
        pScreen->i32OriginX      = cOriginX;
        pScreen->i32OriginY      = cOriginY;
        pScreen->u32StartOffset  = offStart;
        pScreen->u32LineSize     = cbPitch;
        pScreen->u32Width        = cWidth;
        pScreen->u32Height       = cHeight;
        pScreen->u16BitsPerPixel = cBPP;
        pScreen->u16Flags        = fFlags;

        VBoxHGSMIBufferSubmit(pCtx, pScreen);

        VBoxHGSMIBufferFree(pCtx, pScreen);
    }
    else
    {
        // LogFunc(("HGSMIHeapAlloc failed\n"));
    }
}


/** Report the rectangle relative to which absolute pointer events should be
 *  expressed.  This information remains valid until the next VBVA resize event
 *  for any screen, at which time it is reset to the bounding rectangle of all
 *  virtual screens.
 * @param  pCtx      The context containing the heap to use.
 * @param  cOriginX  Upper left X co-ordinate relative to the first screen.
 * @param  cOriginY  Upper left Y co-ordinate relative to the first screen.
 * @param  cWidth    Rectangle width.
 * @param  cHeight   Rectangle height.
 * @returns  iprt status code.
 * @returns  VERR_NO_MEMORY      HGSMI heap allocation failed.
 */
DECLHIDDEN(int)      VBoxHGSMIUpdateInputMapping(PHGSMIGUESTCOMMANDCONTEXT pCtx, int32_t  cOriginX, int32_t  cOriginY,
                                                 uint32_t cWidth, uint32_t cHeight)
{
    int rc;
    VBVAREPORTINPUTMAPPING *p;
    // Log(("%s: cOriginX=%d, cOriginY=%d, cWidth=%u, cHeight=%u\n", __PRETTY_FUNCTION__, (int)cOriginX, (int)cOriginX,
    //      (unsigned)cWidth, (unsigned)cHeight));

    /* Allocate the IO buffer. */
    p = (VBVAREPORTINPUTMAPPING *)VBoxHGSMIBufferAlloc(pCtx, sizeof(VBVAREPORTINPUTMAPPING), HGSMI_CH_VBVA,
                                                       VBVA_REPORT_INPUT_MAPPING);
    if (p)
    {
        /* Prepare data to be sent to the host. */
        p->x  = cOriginX;
        p->y  = cOriginY;
        p->cx = cWidth;
        p->cy = cHeight;
        rc = VBoxHGSMIBufferSubmit(pCtx, p);
        /* Free the IO buffer. */
        VBoxHGSMIBufferFree(pCtx, p);
    }
    else
        rc = VERR_NO_MEMORY;
    // LogFunc(("rc = %d\n", rc));
    return rc;
}


/**
 * Get most recent video mode hints.
 * @param  pCtx      the context containing the heap to use
 * @param  cScreens  the number of screens to query hints for, starting at 0.
 * @param  paHints   array of VBVAMODEHINT structures for receiving the hints.
 * @returns  iprt status code
 * @returns  VERR_NO_MEMORY      HGSMI heap allocation failed.
 * @returns  VERR_NOT_SUPPORTED  Host does not support this command.
 */
DECLHIDDEN(int) VBoxHGSMIGetModeHints(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                      unsigned cScreens, VBVAMODEHINT *paHints)
{
    int rc;
    VBVAQUERYMODEHINTS RT_UNTRUSTED_VOLATILE_HOST *pQuery;

    AssertPtrReturn(paHints, VERR_INVALID_POINTER);
    pQuery = (VBVAQUERYMODEHINTS RT_UNTRUSTED_VOLATILE_HOST *)VBoxHGSMIBufferAlloc(pCtx,
                                                                                      sizeof(VBVAQUERYMODEHINTS)
                                                                                   +  cScreens * sizeof(VBVAMODEHINT),
                                                                                   HGSMI_CH_VBVA, VBVA_QUERY_MODE_HINTS);
    if (pQuery != NULL)
    {
        pQuery->cHintsQueried        = cScreens;
        pQuery->cbHintStructureGuest = sizeof(VBVAMODEHINT);
        pQuery->rc                   = VERR_NOT_SUPPORTED;

        VBoxHGSMIBufferSubmit(pCtx, pQuery);
        rc = pQuery->rc;
        if (RT_SUCCESS(rc))
            memcpy(paHints, (void *)(pQuery + 1), cScreens * sizeof(VBVAMODEHINT));

        VBoxHGSMIBufferFree(pCtx, pQuery);
    }
    else
    {
        // LogFunc(("HGSMIHeapAlloc failed\n"));
        rc = VERR_NO_MEMORY;
    }
    return rc;
}


/**
 * Query the supported flags in VBVAINFOSCREEN::u16Flags.
 *
 * @returns The mask of VBVA_SCREEN_F_* flags or 0 if host does not support the request.
 * @param  pCtx  the context containing the heap to use
 */
DECLHIDDEN(uint16_t) VBoxHGSMIGetScreenFlags(PHGSMIGUESTCOMMANDCONTEXT pCtx)
{
    uint32_t u32Flags = 0;
    int rc = VBoxQueryConfHGSMI(pCtx, VBOX_VBVA_CONF32_SCREEN_FLAGS, &u32Flags);
    // LogFunc(("u32Flags = 0x%x rc %Rrc\n", u32Flags, rc));
    if (RT_FAILURE(rc) || u32Flags > UINT16_MAX)
        u32Flags = 0;
    return (uint16_t)u32Flags;
}
