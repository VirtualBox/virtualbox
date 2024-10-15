/* $Id: HGSMIHostCmd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Video driver, common code - HGSMI host-to-guest communication.
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
#include <VBoxVideoIPRT.h>
#include <HGSMIHostCmd.h>

/**
 * Initialise the host context structure.
 *
 * @param  pCtx               the context structure to initialise
 * @param  pvBaseMapping      where the basic HGSMI structures are mapped at
 * @param  offHostFlags       the offset of the host flags into the basic HGSMI
 *                            structures
 * @param  pvHostAreaMapping  where the area for the host heap is mapped at
 * @param  offVRAMHostArea    offset of the host heap area into VRAM
 * @param  cbHostArea         size in bytes of the host heap area
 */
DECLHIDDEN(void) VBoxHGSMISetupHostContext(PHGSMIHOSTCOMMANDCONTEXT pCtx,
                                           void *pvBaseMapping,
                                           uint32_t offHostFlags,
                                           void *pvHostAreaMapping,
                                           uint32_t offVRAMHostArea,
                                           uint32_t cbHostArea)
{
    uint8_t *pu8HostFlags = ((uint8_t *)pvBaseMapping) + offHostFlags;
    pCtx->pfHostFlags = (HGSMIHOSTFLAGS *)pu8HostFlags;
    /** @todo should we really be using a fixed ISA port value here? */
    pCtx->port        = (RTIOPORT)VGA_PORT_HGSMI_HOST;
    HGSMIAreaInitialize(&pCtx->areaCtx, pvHostAreaMapping, cbHostArea,
                         offVRAMHostArea);
}


/** Send completion notification to the host for the command located at offset
 * @a offt into the host command buffer. */
static void HGSMINotifyHostCmdComplete(PHGSMIHOSTCOMMANDCONTEXT pCtx, HGSMIOFFSET offt)
{
    VBVO_PORT_WRITE_U32(pCtx->port, offt);
}


/**
 * Inform the host that a command has been handled.
 *
 * @param  pCtx   the context containing the heap to be used
 * @param  pvMem  pointer into the heap as mapped in @a pCtx to the command to
 *                be completed
 */
DECLHIDDEN(void) VBoxHGSMIHostCmdComplete(PHGSMIHOSTCOMMANDCONTEXT pCtx, void RT_UNTRUSTED_VOLATILE_HOST *pvMem)
{
    HGSMIBUFFERHEADER RT_UNTRUSTED_VOLATILE_GUEST *pHdr = HGSMIBufferHeaderFromData(pvMem);
    HGSMIOFFSET offMem = HGSMIPointerToOffset(&pCtx->areaCtx, pHdr);
    Assert(offMem != HGSMIOFFSET_VOID);
    if (offMem != HGSMIOFFSET_VOID)
        HGSMINotifyHostCmdComplete(pCtx, offMem);
}


/** Submit an incoming host command to the appropriate handler. */
static void hgsmiHostCmdProcess(PHGSMIHOSTCOMMANDCONTEXT pCtx,
                                HGSMIOFFSET offBuffer)
{
    int rc = HGSMIBufferProcess(&pCtx->areaCtx, &pCtx->channels, offBuffer);
    Assert(!RT_FAILURE(rc));
    if(RT_FAILURE(rc))
    {
        /* failure means the command was not submitted to the handler for some reason
         * it's our responsibility to notify its completion in this case */
        HGSMINotifyHostCmdComplete(pCtx, offBuffer);
    }
    /* if the cmd succeeded it's responsibility of the callback to complete it */
}

/** Get the next command from the host. */
static HGSMIOFFSET hgsmiGetHostBuffer(PHGSMIHOSTCOMMANDCONTEXT pCtx)
{
    return VBVO_PORT_READ_U32(pCtx->port);
}


/** Get and handle the next command from the host. */
static void hgsmiHostCommandQueryProcess(PHGSMIHOSTCOMMANDCONTEXT pCtx)
{
    HGSMIOFFSET offset = hgsmiGetHostBuffer(pCtx);
    AssertReturnVoid(offset != HGSMIOFFSET_VOID);
    hgsmiHostCmdProcess(pCtx, offset);
}


/** Drain the host command queue. */
DECLHIDDEN(void) VBoxHGSMIProcessHostQueue(PHGSMIHOSTCOMMANDCONTEXT pCtx)
{
    while (pCtx->pfHostFlags->u32HostFlags & HGSMIHOSTFLAGS_COMMANDS_PENDING)
    {
        if (!ASMAtomicCmpXchgBool(&pCtx->fHostCmdProcessing, true, false))
            return;
        hgsmiHostCommandQueryProcess(pCtx);
        ASMAtomicWriteBool(&pCtx->fHostCmdProcessing, false);
    }
}


/** Tell the host about the location of the area of VRAM set aside for the host
 * heap. */
static int vboxHGSMIReportHostArea(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                   uint32_t u32AreaOffset, uint32_t u32AreaSize)
{
    VBVAINFOHEAP *p;
    int rc = VINF_SUCCESS;

    /* Allocate the IO buffer. */
    p = (VBVAINFOHEAP *)VBoxHGSMIBufferAlloc(pCtx,
                                       sizeof (VBVAINFOHEAP), HGSMI_CH_VBVA,
                                       VBVA_INFO_HEAP);
    if (p)
    {
        /* Prepare data to be sent to the host. */
        p->u32HeapOffset = u32AreaOffset;
        p->u32HeapSize   = u32AreaSize;
        rc = VBoxHGSMIBufferSubmit(pCtx, p);
        /* Free the IO buffer. */
        VBoxHGSMIBufferFree(pCtx, p);
    }
    else
        rc = VERR_NO_MEMORY;
    return rc;
}


/**
 * Get the information needed to map the area used by the host to send back
 * requests.
 *
 * @param  pCtx                the context containing the heap to use
 * @param  cbVRAM              how much video RAM is allocated to the device
 * @param  offVRAMBaseMapping  the offset of the basic communication structures
 *                             into the guest's VRAM
 * @param  poffVRAMHostArea    where to store the offset into VRAM of the host
 *                             heap area
 * @param  pcbHostArea         where to store the size of the host heap area
 */
DECLHIDDEN(void) VBoxHGSMIGetHostAreaMapping(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                             uint32_t cbVRAM,
                                             uint32_t offVRAMBaseMapping,
                                             uint32_t *poffVRAMHostArea,
                                             uint32_t *pcbHostArea)
{
    uint32_t offVRAMHostArea = offVRAMBaseMapping, cbHostArea = 0;

    AssertPtrReturnVoid(poffVRAMHostArea);
    AssertPtrReturnVoid(pcbHostArea);
    VBoxQueryConfHGSMI(pCtx, VBOX_VBVA_CONF32_HOST_HEAP_SIZE, &cbHostArea);
    if (cbHostArea != 0)
    {
        uint32_t cbHostAreaMaxSize = cbVRAM / 4;
        /** @todo what is the idea of this? */
        if (cbHostAreaMaxSize >= VBVA_ADAPTER_INFORMATION_SIZE)
        {
            cbHostAreaMaxSize -= VBVA_ADAPTER_INFORMATION_SIZE;
        }
        if (cbHostArea > cbHostAreaMaxSize)
        {
            cbHostArea = cbHostAreaMaxSize;
        }
        /* Round up to 4096 bytes. */
        cbHostArea = (cbHostArea + 0xFFF) & ~0xFFF;
        offVRAMHostArea = offVRAMBaseMapping - cbHostArea;
    }

    *pcbHostArea = cbHostArea;
    *poffVRAMHostArea = offVRAMHostArea;
    // LogFunc(("offVRAMHostArea = 0x%08X, cbHostArea = 0x%08X\n",
    //          offVRAMHostArea, cbHostArea));
}


/**
 * Tell the host about the ways it can use to communicate back to us via an
 * HGSMI command
 *
 * @returns  iprt status value
 * @param  pCtx                  the context containing the heap to use
 * @param  offVRAMFlagsLocation  where we wish the host to place its flags
 *                               relative to the start of the VRAM
 * @param  fCaps                 additions HGSMI capabilities the guest
 *                               supports
 * @param  offVRAMHostArea       offset into VRAM of the host heap area
 * @param  cbHostArea            size in bytes of the host heap area
 */
DECLHIDDEN(int) VBoxHGSMISendHostCtxInfo(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                         HGSMIOFFSET offVRAMFlagsLocation,
                                         uint32_t fCaps,
                                         uint32_t offVRAMHostArea,
                                         uint32_t cbHostArea)
{
    // Log(("VBoxVideo::vboxSetupAdapterInfo\n"));

    /* setup the flags first to ensure they are initialized by the time the
     * host heap is ready */
    int rc = VBoxHGSMIReportFlagsLocation(pCtx, offVRAMFlagsLocation);
    AssertRC(rc);
    if (RT_SUCCESS(rc) && fCaps)
    {
        /* Inform about caps */
        rc = VBoxHGSMISendCapsInfo(pCtx, fCaps);
        AssertRC(rc);
    }
    if (RT_SUCCESS (rc))
    {
        /* Report the host heap location. */
        rc = vboxHGSMIReportHostArea(pCtx, offVRAMHostArea, cbHostArea);
        AssertRC(rc);
    }
    // Log(("VBoxVideo::vboxSetupAdapterInfo finished rc = %d\n", rc));
    return rc;
}
