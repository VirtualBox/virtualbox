/* $Id: HGSMIBuffers.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Video driver, common code - HGSMI buffer management.
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

/**
 * Set up the HGSMI guest-to-host command context.
 * @returns iprt status value
 * @param  pCtx                    the context to set up
 * @param  pvGuestHeapMemory       a pointer to the mapped backing memory for
 *                                 the guest heap
 * @param  cbGuestHeapMemory       the size of the backing memory area
 * @param  offVRAMGuestHeapMemory  the offset of the memory pointed to by
 *                                 @a pvGuestHeapMemory within the video RAM
 * @param  pEnv                    HGSMI environment.
 */
DECLHIDDEN(int) VBoxHGSMISetupGuestContext(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                           void *pvGuestHeapMemory,
                                           uint32_t cbGuestHeapMemory,
                                           uint32_t offVRAMGuestHeapMemory,
                                           const HGSMIENV *pEnv)
{
    /** @todo should we be using a fixed ISA port value here? */
    pCtx->port = (RTIOPORT)VGA_PORT_HGSMI_GUEST;
#ifdef VBOX_WDDM_MINIPORT
    return VBoxSHGSMIInit(&pCtx->heapCtx, pvGuestHeapMemory,
                          cbGuestHeapMemory, offVRAMGuestHeapMemory, pEnv);
#else
    return HGSMIHeapSetup(&pCtx->heapCtx, pvGuestHeapMemory,
                          cbGuestHeapMemory, offVRAMGuestHeapMemory, pEnv);
#endif
}


/**
 * Allocate and initialise a command descriptor in the guest heap for a
 * guest-to-host command.
 *
 * @returns  pointer to the descriptor's command data buffer
 * @param  pCtx     the context containing the heap to be used
 * @param  cbData   the size of the command data to go into the descriptor
 * @param  u8Ch     the HGSMI channel to be used, set to the descriptor
 * @param  u16Op    the HGSMI command to be sent, set to the descriptor
 */
DECLHIDDEN(void RT_UNTRUSTED_VOLATILE_HOST *) VBoxHGSMIBufferAlloc(PHGSMIGUESTCOMMANDCONTEXT pCtx,
                                                                   HGSMISIZE cbData,
                                                                   uint8_t u8Ch,
                                                                   uint16_t u16Op)
{
#ifdef VBOX_WDDM_MINIPORT
    return VBoxSHGSMIHeapAlloc(&pCtx->heapCtx, cbData, u8Ch, u16Op);
#else
    return HGSMIHeapAlloc(&pCtx->heapCtx, cbData, u8Ch, u16Op);
#endif
}


/**
 * Free a descriptor allocated by @a VBoxHGSMIBufferAlloc.
 *
 * @param  pCtx      the context containing the heap used
 * @param  pvBuffer  the pointer returned by @a VBoxHGSMIBufferAlloc
 */
DECLHIDDEN(void) VBoxHGSMIBufferFree(PHGSMIGUESTCOMMANDCONTEXT pCtx, void  RT_UNTRUSTED_VOLATILE_HOST *pvBuffer)
{
#ifdef VBOX_WDDM_MINIPORT
    VBoxSHGSMIHeapFree(&pCtx->heapCtx, pvBuffer);
#else
    HGSMIHeapFree(&pCtx->heapCtx, pvBuffer);
#endif
}

/**
 * Submit a command descriptor allocated by @a VBoxHGSMIBufferAlloc.
 *
 * @param  pCtx      the context containing the heap used
 * @param  pvBuffer  the pointer returned by @a VBoxHGSMIBufferAlloc
 */
DECLHIDDEN(int) VBoxHGSMIBufferSubmit(PHGSMIGUESTCOMMANDCONTEXT pCtx, void RT_UNTRUSTED_VOLATILE_HOST *pvBuffer)
{
    /* Initialize the buffer and get the offset for port IO. */
    HGSMIOFFSET offBuffer = HGSMIHeapBufferOffset(HGSMIGUESTCMDHEAP_GET(&pCtx->heapCtx), pvBuffer);

    Assert(offBuffer != HGSMIOFFSET_VOID);
    if (offBuffer != HGSMIOFFSET_VOID)
    {
        /* Submit the buffer to the host. */
        VBVO_PORT_WRITE_U32(pCtx->port, offBuffer);
        /* Make the compiler aware that the host has changed memory. */
        ASMCompilerBarrier();
        return VINF_SUCCESS;
    }

    return VERR_INVALID_PARAMETER;
}
