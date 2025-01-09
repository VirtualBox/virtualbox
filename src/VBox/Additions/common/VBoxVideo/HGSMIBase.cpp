/* $Id: HGSMIBase.cpp 107674 2025-01-09 18:12:18Z vadim.galitsyn@oracle.com $ */
/** @file
 * VirtualBox Video driver, common code - HGSMI guest-to-host communication.
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

#include <HGSMIBase.h>
#include <VBoxVideoIPRT.h>
#include <VBoxVideoGuest.h>
#include <VBoxVideoVBE.h>
#include <HGSMIChannels.h>
#include <HGSMIChSetup.h>

/** Detect whether HGSMI is supported by the host. */
DECLHIDDEN(bool) VBoxHGSMIIsSupported(void)
{
    uint16_t DispiId;

    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_INDEX, VBE_DISPI_INDEX_ID);
    VBVO_PORT_WRITE_U16(VBE_DISPI_IOPORT_DATA, VBE_DISPI_ID_HGSMI);

    DispiId = VBVO_PORT_READ_U16(VBE_DISPI_IOPORT_DATA);

    return (DispiId == VBE_DISPI_ID_HGSMI);
}


/**
 * Inform the host of the location of the host flags in VRAM via an HGSMI command.
 * @returns  IPRT status value.
 * @returns  VERR_NOT_IMPLEMENTED  if the host does not support the command.
 * @returns  VERR_NO_MEMORY        if a heap allocation fails.
 * @param    pCtx                  the context of the guest heap to use.
 * @param    offLocation           the offset chosen for the flags withing guest VRAM.
 */
DECLHIDDEN(int) VBoxHGSMIReportFlagsLocation(PHGSMIGUESTCOMMANDCONTEXT pCtx, HGSMIOFFSET offLocation)
{

    /* Allocate the IO buffer. */
    HGSMIBUFFERLOCATION RT_UNTRUSTED_VOLATILE_HOST *p =
        (HGSMIBUFFERLOCATION RT_UNTRUSTED_VOLATILE_HOST *)VBoxHGSMIBufferAlloc(pCtx, sizeof(*p), HGSMI_CH_HGSMI,
                                                                               HGSMI_CC_HOST_FLAGS_LOCATION);
    if (!p)
        return VERR_NO_MEMORY;

    /* Prepare data to be sent to the host. */
    p->offLocation = offLocation;
    p->cbLocation  = sizeof(HGSMIHOSTFLAGS);
    /* No need to check that the buffer is valid as we have just allocated it. */
    VBoxHGSMIBufferSubmit(pCtx, p);
    /* Free the IO buffer. */
    VBoxHGSMIBufferFree(pCtx, p);

    return VINF_SUCCESS;
}


/**
 * Notify the host of HGSMI-related guest capabilities via an HGSMI command.
 * @returns  IPRT status value.
 * @returns  VERR_NOT_IMPLEMENTED  if the host does not support the command.
 * @returns  VERR_NO_MEMORY        if a heap allocation fails.
 * @param    pCtx                  the context of the guest heap to use.
 * @param    fCaps                 the capabilities to report, see VBVACAPS.
 */
DECLHIDDEN(int) VBoxHGSMISendCapsInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx, uint32_t fCaps)
{

    /* Allocate the IO buffer. */
    VBVACAPS RT_UNTRUSTED_VOLATILE_HOST *p =
        (VBVACAPS RT_UNTRUSTED_VOLATILE_HOST *)VBoxHGSMIBufferAlloc(pCtx, sizeof(*p), HGSMI_CH_VBVA, VBVA_INFO_CAPS);

    if (!p)
        return VERR_NO_MEMORY;

    /* Prepare data to be sent to the host. */
    p->rc    = VERR_NOT_IMPLEMENTED;
    p->fCaps = fCaps;
    /* No need to check that the buffer is valid as we have just allocated it. */
    VBoxHGSMIBufferSubmit(pCtx, p);

    AssertRC(p->rc);
    /* Free the IO buffer. */
    VBoxHGSMIBufferFree(pCtx, p);
    return p->rc;
}


/**
 * Get the information needed to map the basic communication structures in
 * device memory into our address space.  All pointer parameters are optional.
 *
 * @param  cbVRAM               how much video RAM is allocated to the device
 * @param  poffVRAMBaseMapping  where to save the offset from the start of the
 *                              device VRAM of the whole area to map
 * @param  pcbMapping           where to save the mapping size
 * @param  poffGuestHeapMemory  where to save the offset into the mapped area
 *                              of the guest heap backing memory
 * @param  pcbGuestHeapMemory   where to save the size of the guest heap
 *                              backing memory
 * @param  poffHostFlags        where to save the offset into the mapped area
 *                              of the host flags
 */
DECLHIDDEN(void) VBoxHGSMIGetBaseMappingInfo(uint32_t cbVRAM,
                                             uint32_t *poffVRAMBaseMapping,
                                             uint32_t *pcbMapping,
                                             uint32_t *poffGuestHeapMemory,
                                             uint32_t *pcbGuestHeapMemory,
                                             uint32_t *poffHostFlags)
{
    AssertPtrNullReturnVoid(poffVRAMBaseMapping);
    AssertPtrNullReturnVoid(pcbMapping);
    AssertPtrNullReturnVoid(poffGuestHeapMemory);
    AssertPtrNullReturnVoid(pcbGuestHeapMemory);
    AssertPtrNullReturnVoid(poffHostFlags);
    if (poffVRAMBaseMapping)
        *poffVRAMBaseMapping = cbVRAM - VBVA_ADAPTER_INFORMATION_SIZE;
    if (pcbMapping)
        *pcbMapping = VBVA_ADAPTER_INFORMATION_SIZE;
    if (poffGuestHeapMemory)
        *poffGuestHeapMemory = 0;
    if (pcbGuestHeapMemory)
        *pcbGuestHeapMemory =   VBVA_ADAPTER_INFORMATION_SIZE
                              - sizeof(HGSMIHOSTFLAGS);
    if (poffHostFlags)
        *poffHostFlags =   VBVA_ADAPTER_INFORMATION_SIZE
                         - sizeof(HGSMIHOSTFLAGS);
}

/**
 * Query the host for an HGSMI configuration parameter via an HGSMI command.
 * @returns iprt status value
 * @param  pCtx      the context containing the heap used
 * @param  u32Index  the index of the parameter to query,
 *                   @see VBVACONF32::u32Index
 * @param  pulValue  where to store the value of the parameter on success
 */
DECLHIDDEN(int) VBoxQueryConfHGSMI(PHGSMIGUESTCOMMANDCONTEXT pCtx, uint32_t u32Index, uint32_t *pulValue)
{
    VBVACONF32 *p;

    /* Allocate the IO buffer. */
    p = (VBVACONF32 *)VBoxHGSMIBufferAlloc(pCtx, sizeof(*p), HGSMI_CH_VBVA, VBVA_QUERY_CONF32);
    if (!p)
        return VERR_NO_MEMORY;

    /* Prepare data to be sent to the host. */
    p->u32Index = u32Index;
    p->u32Value = UINT32_MAX;
    /* No need to check that the buffer is valid as we have just allocated it. */
    VBoxHGSMIBufferSubmit(pCtx, p);
    *pulValue = p->u32Value;
    /* Free the IO buffer. */
    VBoxHGSMIBufferFree(pCtx, p);
    return VINF_SUCCESS;
}

/**
 * Pass the host a new mouse pointer shape via an HGSMI command.
 *
 * @returns  success or failure
 * @param  pCtx      the context containing the heap to be used
 * @param  fFlags    cursor flags, @see VMMDevReqMousePointer::fFlags
 * @param  cHotX     horizontal position of the hot spot
 * @param  cHotY     vertical position of the hot spot
 * @param  cWidth    width in pixels of the cursor
 * @param  cHeight   height in pixels of the cursor
 * @param  pPixels   pixel data, @see VMMDevReqMousePointer for the format
 * @param  cbLength  size in bytes of the pixel data
 */
DECLHIDDEN(int)  VBoxHGSMIUpdatePointerShape(PHGSMIGUESTCOMMANDCONTEXT pCtx, uint32_t fFlags,
                                             uint32_t cHotX, uint32_t cHotY, uint32_t cWidth, uint32_t cHeight,
                                             uint8_t *pPixels, uint32_t cbLength)
{
    VBVAMOUSEPOINTERSHAPE *p;
    uint32_t cbPixels = 0;
    int rc;

    if (fFlags & VBOX_MOUSE_POINTER_SHAPE)
    {
        /*
         * Size of the pointer data:
         * sizeof (AND mask) + sizeof (XOR_MASK)
         */
        cbPixels = ((((cWidth + 7) / 8) * cHeight + 3) & ~3)
                 + cWidth * 4 * cHeight;
        if (cbPixels > cbLength)
            return VERR_INVALID_PARAMETER;
        /*
         * If shape is supplied, then always create the pointer visible.
         * See comments in 'vboxUpdatePointerShape'
         */
        fFlags |= VBOX_MOUSE_POINTER_VISIBLE;
    }
    /* Allocate the IO buffer. */
    p = (VBVAMOUSEPOINTERSHAPE *)VBoxHGSMIBufferAlloc(pCtx, sizeof(*p) + cbPixels, HGSMI_CH_VBVA,
                                                      VBVA_MOUSE_POINTER_SHAPE);
    if (!p)
        return VERR_NO_MEMORY;
    /* Prepare data to be sent to the host. */
    /* Will be updated by the host. */
    p->i32Result = VINF_SUCCESS;
    /* We have our custom flags in the field */
    p->fu32Flags = fFlags;
    p->u32HotX   = cHotX;
    p->u32HotY   = cHotY;
    p->u32Width  = cWidth;
    p->u32Height = cHeight;
    if (cbPixels)
        /* Copy the actual pointer data. */
        RT_BCOPY_UNFORTIFIED(p->au8Data, pPixels, cbPixels);
    /* No need to check that the buffer is valid as we have just allocated it. */
    VBoxHGSMIBufferSubmit(pCtx, p);
    rc = p->i32Result;
    /* Free the IO buffer. */
    VBoxHGSMIBufferFree(pCtx, p);
    return rc;
}


/**
 * Report the guest cursor position.  The host may wish to use this information
 * to re-position its own cursor (though this is currently unlikely).  The
 * current host cursor position is returned.
 * @param  pCtx             The context containing the heap used.
 * @param  fReportPosition  Are we reporting a position?
 * @param  x                Guest cursor X position.
 * @param  y                Guest cursor Y position.
 * @param  pxHost           Host cursor X position is stored here.  Optional.
 * @param  pyHost           Host cursor Y position is stored here.  Optional.
 * @returns  iprt status code.
 * @returns  VERR_NO_MEMORY      HGSMI heap allocation failed.
 */
DECLHIDDEN(int) VBoxHGSMIReportCursorPosition(PHGSMIGUESTCOMMANDCONTEXT pCtx, bool fReportPosition,
                                              uint32_t x, uint32_t y, uint32_t *pxHost, uint32_t *pyHost)
{
    VBVACURSORPOSITION *p;

    /* Allocate the IO buffer. */
    p = (VBVACURSORPOSITION *)VBoxHGSMIBufferAlloc(pCtx, sizeof(*p), HGSMI_CH_VBVA,
                                                   VBVA_CURSOR_POSITION);
    if (!p)
        return VERR_NO_MEMORY;
    /* Prepare data to be sent to the host. */
    p->fReportPosition = fReportPosition;
    p->x = x;
    p->y = y;
    /* No need to check that the buffer is valid as we have just allocated it. */
    VBoxHGSMIBufferSubmit(pCtx, p);
    if (pxHost)
        *pxHost = p->x;
    if (pyHost)
        *pyHost = p->y;
    /* Free the IO buffer. */
    VBoxHGSMIBufferFree(pCtx, p);
    return VINF_SUCCESS;
}


/**
 * @todo Mouse pointer position to be read from VMMDev memory, address of the
 * memory region can be queried from VMMDev via an IOCTL. This VMMDev memory
 * region will contain host information which is needed by the guest.
 *
 * Reading will not cause a switch to the host.
 *
 * Have to take into account:
 *  * synchronization: host must write to the memory only from EMT,
 *    large structures must be read under flag, which tells the host
 *    that the guest is currently reading the memory (OWNER flag?).
 *  * guest writes: may be allocate a page for the host info and make
 *    the page readonly for the guest.
 *  * the information should be available only for additions drivers.
 *  * VMMDev additions driver will inform the host which version of the info
 *    it expects, host must support all versions.
 */
