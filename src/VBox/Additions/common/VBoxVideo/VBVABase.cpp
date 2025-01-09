/* $Id: VBVABase.cpp 107674 2025-01-09 18:12:18Z vadim.galitsyn@oracle.com $ */
/** @file
 * VirtualBox Video driver, common code - VBVA initialisation and helper
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
#include <VBoxVideoIPRT.h>
#include <HGSMIChannels.h>

/*
 * There is a hardware ring buffer in the graphics device video RAM, formerly
 * in the VBox VMMDev PCI memory space.
 * All graphics commands go there serialized by VBoxVBVABufferBeginUpdate.
 * and vboxHwBufferEndUpdate.
 *
 * off32Free is writing position. off32Data is reading position.
 * off32Free == off32Data means buffer is empty.
 * There must be always gap between off32Data and off32Free when data
 * are in the buffer.
 * Guest only changes off32Free, host changes off32Data.
 */

/* Forward declarations of internal functions. */
static void vboxHwBufferFlush(PHGSMIGUESTCOMMANDCONTEXT pCtx);
static void vboxHwBufferPlaceDataAt(PVBVABUFFERCONTEXT pCtx, const void *p,
                                    uint32_t cb, uint32_t offset);
static bool vboxHwBufferWrite(PVBVABUFFERCONTEXT pCtx,
                              PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                              const void *p, uint32_t cb);


static bool vboxVBVAInformHost(PVBVABUFFERCONTEXT pCtx, PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx, int32_t cScreen, bool fEnable)
{
    bool fRc = false;

#if 0  /* All callers check this */
    if (ppdev->bHGSMISupported)
#endif
    {
        VBVAENABLE_EX RT_UNTRUSTED_VOLATILE_HOST *pEnable =
            (VBVAENABLE_EX RT_UNTRUSTED_VOLATILE_HOST *)VBoxHGSMIBufferAlloc(pHGSMICtx, sizeof(VBVAENABLE_EX),
                                                                             HGSMI_CH_VBVA, VBVA_ENABLE);
        if (pEnable != NULL)
        {
            pEnable->Base.u32Flags  = fEnable ? VBVA_F_ENABLE : VBVA_F_DISABLE;
            pEnable->Base.u32Offset = pCtx->offVRAMBuffer;
            pEnable->Base.i32Result = VERR_NOT_SUPPORTED;
            if (cScreen >= 0)
            {
                pEnable->Base.u32Flags |= VBVA_F_EXTENDED | VBVA_F_ABSOFFSET;
                pEnable->u32ScreenId    = cScreen;
            }

            VBoxHGSMIBufferSubmit(pHGSMICtx, pEnable);

            if (fEnable)
                fRc = RT_SUCCESS(pEnable->Base.i32Result);
            else
                fRc = true;

            VBoxHGSMIBufferFree(pHGSMICtx, pEnable);
        }
        else
        {
            // LogFunc(("HGSMIHeapAlloc failed\n"));
        }
    }

    return fRc;
}

/*
 * Public hardware buffer methods.
 */
DECLHIDDEN(bool) VBoxVBVAEnable(PVBVABUFFERCONTEXT pCtx,
                                PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                                VBVABUFFER *pVBVA, int32_t cScreen)
{
    bool fRc = false;

    // LogFlowFunc(("pVBVA %p\n", pVBVA));

#if 0  /* All callers check this */
    if (ppdev->bHGSMISupported)
#endif
    {
        // LogFunc(("pVBVA %p vbva off 0x%x\n", pVBVA, pCtx->offVRAMBuffer));

        pVBVA->hostFlags.u32HostEvents      = 0;
        pVBVA->hostFlags.u32SupportedOrders = 0;
        pVBVA->off32Data          = 0;
        pVBVA->off32Free          = 0;
        memset(pVBVA->aRecords, 0, sizeof (pVBVA->aRecords));
        pVBVA->indexRecordFirst   = 0;
        pVBVA->indexRecordFree    = 0;
        pVBVA->cbPartialWriteThreshold = 256;
        pVBVA->cbData             = pCtx->cbBuffer - sizeof (VBVABUFFER) + sizeof (pVBVA->au8Data);

        pCtx->fHwBufferOverflow = false;
        pCtx->pRecord    = NULL;
        pCtx->pVBVA      = pVBVA;

        fRc = vboxVBVAInformHost(pCtx, pHGSMICtx, cScreen, true);
    }

    if (!fRc)
    {
        VBoxVBVADisable(pCtx, pHGSMICtx, cScreen);
    }

    return fRc;
}

DECLHIDDEN(void) VBoxVBVADisable(PVBVABUFFERCONTEXT pCtx,
                                 PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                                 int32_t cScreen)
{
    // LogFlowFunc(("\n"));

    pCtx->fHwBufferOverflow = false;
    pCtx->pRecord           = NULL;
    pCtx->pVBVA             = NULL;

    vboxVBVAInformHost(pCtx, pHGSMICtx, cScreen, false);
}

DECLHIDDEN(bool) VBoxVBVABufferBeginUpdate(PVBVABUFFERCONTEXT pCtx,
                                           PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx)
{
    bool fRc = false;

    // LogFunc(("flags = 0x%08X\n", pCtx->pVBVA? pCtx->pVBVA->u32HostEvents: -1));

    if (   pCtx->pVBVA
        && (pCtx->pVBVA->hostFlags.u32HostEvents & VBVA_F_MODE_ENABLED))
    {
        uint32_t indexRecordNext;

        Assert(!pCtx->fHwBufferOverflow);
        Assert(pCtx->pRecord == NULL);

        indexRecordNext = (pCtx->pVBVA->indexRecordFree + 1) % VBVA_MAX_RECORDS;

        if (indexRecordNext == pCtx->pVBVA->indexRecordFirst)
        {
            /* All slots in the records queue are used. */
            vboxHwBufferFlush (pHGSMICtx);
        }

        if (indexRecordNext == pCtx->pVBVA->indexRecordFirst)
        {
            /* Even after flush there is no place. Fail the request. */
            // LogFunc(("no space in the queue of records!!! first %d, last %d\n",
            //          pCtx->pVBVA->indexRecordFirst, pCtx->pVBVA->indexRecordFree));
        }
        else
        {
            /* Initialize the record. */
            VBVARECORD *pRecord = &pCtx->pVBVA->aRecords[pCtx->pVBVA->indexRecordFree];

            pRecord->cbRecord = VBVA_F_RECORD_PARTIAL;

            pCtx->pVBVA->indexRecordFree = indexRecordNext;

            // LogFunc(("indexRecordNext = %d\n", indexRecordNext));

            /* Remember which record we are using. */
            pCtx->pRecord = pRecord;

            fRc = true;
        }
    }

    return fRc;
}

DECLHIDDEN(void) VBoxVBVABufferEndUpdate(PVBVABUFFERCONTEXT pCtx)
{
    VBVARECORD *pRecord;

    // LogFunc(("\n"));

    Assert(pCtx->pVBVA);

    pRecord = pCtx->pRecord;
    Assert(pRecord && (pRecord->cbRecord & VBVA_F_RECORD_PARTIAL));

    /* Mark the record completed. */
    pRecord->cbRecord &= ~VBVA_F_RECORD_PARTIAL;

    pCtx->fHwBufferOverflow = false;
    pCtx->pRecord = NULL;
}

/*
 * Private operations.
 */
static uint32_t vboxHwBufferAvail (const VBVABUFFER *pVBVA)
{
    int32_t i32Diff = pVBVA->off32Data - pVBVA->off32Free;

    return i32Diff > 0? i32Diff: pVBVA->cbData + i32Diff;
}

static void vboxHwBufferFlush(PHGSMIGUESTCOMMANDCONTEXT pCtx)
{
    /* Issue the flush command. */
    VBVAFLUSH RT_UNTRUSTED_VOLATILE_HOST *pFlush =
        (VBVAFLUSH RT_UNTRUSTED_VOLATILE_HOST * )VBoxHGSMIBufferAlloc(pCtx, sizeof(VBVAFLUSH), HGSMI_CH_VBVA, VBVA_FLUSH);
    if (pFlush != NULL)
    {
        pFlush->u32Reserved = 0;

        VBoxHGSMIBufferSubmit(pCtx, pFlush);

        VBoxHGSMIBufferFree(pCtx, pFlush);
    }
    else
    {
        // LogFunc(("HGSMIHeapAlloc failed\n"));
    }
}

static void vboxHwBufferPlaceDataAt(PVBVABUFFERCONTEXT pCtx, const void *p,
                                    uint32_t cb, uint32_t offset)
{
    VBVABUFFER *pVBVA = pCtx->pVBVA;
    uint32_t u32BytesTillBoundary = pVBVA->cbData - offset;
    uint8_t  *dst                 = &pVBVA->au8Data[0] + offset;
    int32_t i32Diff               = cb - u32BytesTillBoundary;

    if (i32Diff <= 0)
    {
        /* Chunk will not cross buffer boundary. */
        RT_BCOPY_UNFORTIFIED(dst, p, cb);
    }
    else
    {
        /* Chunk crosses buffer boundary. */
        RT_BCOPY_UNFORTIFIED(dst, p, u32BytesTillBoundary);
        RT_BCOPY_UNFORTIFIED(&pVBVA->au8Data[0], (uint8_t *)p + u32BytesTillBoundary, i32Diff);
    }
}

static bool vboxHwBufferWrite(PVBVABUFFERCONTEXT pCtx,
                              PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                              const void *p, uint32_t cb)
{
    VBVARECORD *pRecord;
    uint32_t cbHwBufferAvail;

    uint32_t cbWritten = 0;

    VBVABUFFER *pVBVA = pCtx->pVBVA;
    Assert(pVBVA);

    if (!pVBVA || pCtx->fHwBufferOverflow)
    {
        return false;
    }

    Assert(pVBVA->indexRecordFirst != pVBVA->indexRecordFree);

    pRecord = pCtx->pRecord;
    Assert(pRecord && (pRecord->cbRecord & VBVA_F_RECORD_PARTIAL));

    // LogFunc(("%d\n", cb));

    cbHwBufferAvail = vboxHwBufferAvail (pVBVA);

    while (cb > 0)
    {
        uint32_t cbChunk = cb;

        // LogFunc(("pVBVA->off32Free %d, pRecord->cbRecord 0x%08X, cbHwBufferAvail %d, cb %d, cbWritten %d\n",
        //             pVBVA->off32Free, pRecord->cbRecord, cbHwBufferAvail, cb, cbWritten));

        if (cbChunk >= cbHwBufferAvail)
        {
            // LogFunc(("1) avail %d, chunk %d\n", cbHwBufferAvail, cbChunk));

            vboxHwBufferFlush (pHGSMICtx);

            cbHwBufferAvail = vboxHwBufferAvail (pVBVA);

            if (cbChunk >= cbHwBufferAvail)
            {
                // LogFunc(("no place for %d bytes. Only %d bytes available after flush. Going to partial writes.\n",
                //             cb, cbHwBufferAvail));

                if (cbHwBufferAvail <= pVBVA->cbPartialWriteThreshold)
                {
                    // LogFunc(("Buffer overflow!!!\n"));
                    pCtx->fHwBufferOverflow = true;
                    Assert(false);
                    return false;
                }

                cbChunk = cbHwBufferAvail - pVBVA->cbPartialWriteThreshold;
            }
        }

        Assert(cbChunk <= cb);
        Assert(cbChunk <= vboxHwBufferAvail (pVBVA));

        vboxHwBufferPlaceDataAt (pCtx, (uint8_t *)p + cbWritten, cbChunk, pVBVA->off32Free);

        pVBVA->off32Free   = (pVBVA->off32Free + cbChunk) % pVBVA->cbData;
        pRecord->cbRecord += cbChunk;
        cbHwBufferAvail -= cbChunk;

        cb        -= cbChunk;
        cbWritten += cbChunk;
    }

    return true;
}

/*
 * Public writer to the hardware buffer.
 */
DECLHIDDEN(bool) VBoxVBVAWrite(PVBVABUFFERCONTEXT pCtx,
                               PHGSMIGUESTCOMMANDCONTEXT pHGSMICtx,
                               const void *pv, uint32_t cb)
{
    return vboxHwBufferWrite (pCtx, pHGSMICtx, pv, cb);
}

DECLHIDDEN(bool) VBoxVBVAOrderSupported(PVBVABUFFERCONTEXT pCtx, unsigned code)
{
    VBVABUFFER *pVBVA = pCtx->pVBVA;

    if (!pVBVA)
    {
        return false;
    }

    if (pVBVA->hostFlags.u32SupportedOrders & (1 << code))
    {
        return true;
    }

    return false;
}

DECLHIDDEN(void) VBoxVBVASetupBufferContext(PVBVABUFFERCONTEXT pCtx,
                                            uint32_t offVRAMBuffer,
                                            uint32_t cbBuffer)
{
    pCtx->offVRAMBuffer = offVRAMBuffer;
    pCtx->cbBuffer      = cbBuffer;
}
