/* $Id: HGSMI.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Host Guest Shared Memory Interface (HGSMI) - Host/Guest shared part.
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

#ifndef VBOX_INCLUDED_Graphics_HGSMI_h
#define VBOX_INCLUDED_Graphics_HGSMI_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxVideoIPRT.h"

#include "HGSMIDefs.h"
#include "HGSMIChannels.h"
#include "HGSMIMemAlloc.h"

/**
 * Basic mechanism for the HGSMI is to prepare and pass data buffer to the host and the guest.
 * Data inside these buffers are opaque for the HGSMI and are interpreted by higher levels.
 *
 * Every shared memory buffer passed between the guest/host has the following structure:
 *
 * HGSMIBUFFERHEADER header;
 * uint8_t data[header.u32BufferSize];
 * HGSMIBUFFERTAIL tail;
 *
 * Note: Offset of the 'header' in the memory is used for virtual hardware IO.
 *
 * Buffers are verifyed using the offset and the content of the header and the tail,
 * which are constant during a call.
 *
 * Invalid buffers are ignored.
 *
 * Actual 'data' is not verifyed, as it is expected that the data can be changed by the
 * called function.
 *
 * Since only the offset of the buffer is passed in a IO operation, the header and tail
 * must contain:
 *     * size of data in this buffer;
 *     * checksum for buffer verification.
 *
 * For segmented transfers:
 *     * the sequence identifier;
 *     * offset of the current segment in the sequence;
 *     * total bytes in the transfer.
 *
 * Additionally contains:
 *     * the channel ID;
 *     * the channel information.
 */

typedef struct HGSMIHEAP
{
    HGSMIAREA area; /**< Description. */
    HGSMIMADATA ma; /**< Memory allocator */
} HGSMIHEAP;

/* The size of the array of channels. Array indexes are uint8_t. Note: the value must not be changed. */
#define HGSMI_NUMBER_OF_CHANNELS 0x100

/**
 * Channel handler called when the guest submits a buffer.
 *
 * @returns stuff
 * @param   pvHandler       Value specified when registring.
 * @param   u16ChannelInfo  Command code.
 * @param   pvBuffer        HGSMI buffer with command data.  This is shared with
 *                          the guest.  Consider untrusted and volatile!
 * @param   cbBuffer        Size of command data.
 * @thread  EMT on the host side.
 */
typedef DECLCALLBACKTYPE(int, FNHGSMICHANNELHANDLER,(void *pvHandler, uint16_t u16ChannelInfo,
                                                     RT_UNTRUSTED_VOLATILE_HSTGST void *pvBuffer, HGSMISIZE cbBuffer));
/** Pointer to a channel handler callback. */
typedef FNHGSMICHANNELHANDLER *PFNHGSMICHANNELHANDLER;

/** Information about a handler: pfn + context. */
typedef struct _HGSMICHANNELHANDLER
{
    PFNHGSMICHANNELHANDLER pfnHandler;
    void *pvHandler;
} HGSMICHANNELHANDLER;

/** Channel description. */
typedef struct _HGSMICHANNEL
{
    HGSMICHANNELHANDLER handler;       /**< The channel handler. */
    const char *pszName;               /**< NULL for hardcoded channels or RTStrDup'ed name. */
    uint8_t u8Channel;                 /**< The channel id, equal to the channel index in the array. */
    uint8_t u8Flags;                   /**< HGSMI_CH_F_* */
} HGSMICHANNEL;

typedef struct _HGSMICHANNELINFO
{
    /** Channel handlers indexed by the channel id.
     * The array is accessed under the instance lock. */
    HGSMICHANNEL Channels[HGSMI_NUMBER_OF_CHANNELS];
}  HGSMICHANNELINFO;


RT_C_DECLS_BEGIN

DECLINLINE(HGSMIBUFFERHEADER *) HGSMIBufferHeaderFromPtr(void RT_UNTRUSTED_VOLATILE_HSTGST *pvBuffer)
{
    return (HGSMIBUFFERHEADER *)pvBuffer;
}

DECLINLINE(uint8_t RT_UNTRUSTED_VOLATILE_HSTGST *) HGSMIBufferDataFromPtr(void RT_UNTRUSTED_VOLATILE_HSTGST *pvBuffer)
{
    return (uint8_t RT_UNTRUSTED_VOLATILE_HSTGST *)pvBuffer + sizeof(HGSMIBUFFERHEADER);
}

DECLINLINE(HGSMIBUFFERTAIL RT_UNTRUSTED_VOLATILE_HSTGST *)
HGSMIBufferTailFromPtr(void RT_UNTRUSTED_VOLATILE_HSTGST *pvBuffer, uint32_t u32DataSize)
{
    return (HGSMIBUFFERTAIL RT_UNTRUSTED_VOLATILE_HSTGST *)(HGSMIBufferDataFromPtr(pvBuffer) + u32DataSize);
}

DECLINLINE(HGSMISIZE) HGSMIBufferMinimumSize(void)
{
    return sizeof(HGSMIBUFFERHEADER) + sizeof(HGSMIBUFFERTAIL);
}

DECLINLINE(HGSMIBUFFERHEADER RT_UNTRUSTED_VOLATILE_HSTGST *) HGSMIBufferHeaderFromData(const void RT_UNTRUSTED_VOLATILE_HSTGST *pvData)
{
    return (HGSMIBUFFERHEADER RT_UNTRUSTED_VOLATILE_HSTGST *)((uint8_t *)pvData - sizeof(HGSMIBUFFERHEADER));
}

DECLINLINE(HGSMISIZE) HGSMIBufferRequiredSize(uint32_t u32DataSize)
{
    return HGSMIBufferMinimumSize() + u32DataSize;
}

DECLINLINE(HGSMIOFFSET) HGSMIPointerToOffset(const HGSMIAREA *pArea, const void RT_UNTRUSTED_VOLATILE_HSTGST *pv)
{
    return pArea->offBase + (HGSMIOFFSET)((uint8_t *)pv - pArea->pu8Base);
}

DECLINLINE(void RT_UNTRUSTED_VOLATILE_HSTGST *) HGSMIOffsetToPointer(const HGSMIAREA *pArea, HGSMIOFFSET offBuffer)
{
    return pArea->pu8Base + (offBuffer - pArea->offBase);
}

DECLINLINE(uint8_t RT_UNTRUSTED_VOLATILE_HSTGST*) HGSMIBufferDataFromOffset(const HGSMIAREA *pArea, HGSMIOFFSET offBuffer)
{
    void RT_UNTRUSTED_VOLATILE_HSTGST *pvBuffer = HGSMIOffsetToPointer(pArea, offBuffer);
    return HGSMIBufferDataFromPtr(pvBuffer);
}

DECLINLINE(HGSMIOFFSET) HGSMIBufferOffsetFromData(const HGSMIAREA *pArea, void RT_UNTRUSTED_VOLATILE_HSTGST *pvData)
{
    HGSMIBUFFERHEADER RT_UNTRUSTED_VOLATILE_HSTGST *pHeader = HGSMIBufferHeaderFromData(pvData);
    return HGSMIPointerToOffset(pArea, pHeader);
}

DECLINLINE(uint8_t RT_UNTRUSTED_VOLATILE_HSTGST *) HGSMIBufferDataAndChInfoFromOffset(const HGSMIAREA *pArea,
                                                                                      HGSMIOFFSET offBuffer,
                                                                                      uint16_t *pu16ChannelInfo)
{
    HGSMIBUFFERHEADER RT_UNTRUSTED_VOLATILE_HSTGST *pHeader =
        (HGSMIBUFFERHEADER RT_UNTRUSTED_VOLATILE_HSTGST *)HGSMIOffsetToPointer(pArea, offBuffer);
    *pu16ChannelInfo = pHeader->u16ChannelInfo;
    return HGSMIBufferDataFromPtr(pHeader);
}

uint32_t HGSMIChecksum(HGSMIOFFSET offBuffer, const HGSMIBUFFERHEADER RT_UNTRUSTED_VOLATILE_HSTGST *pHeader,
                       const HGSMIBUFFERTAIL RT_UNTRUSTED_VOLATILE_HSTGST *pTail);

int  HGSMIAreaInitialize(HGSMIAREA *pArea, void *pvBase, HGSMISIZE cbArea, HGSMIOFFSET offBase);
void HGSMIAreaClear(HGSMIAREA *pArea);

DECLINLINE(bool) HGSMIAreaContainsOffset(const HGSMIAREA *pArea, HGSMIOFFSET off)
{
    return off >= pArea->offBase && off - pArea->offBase < pArea->cbArea;
}

DECLINLINE(bool) HGSMIAreaContainsPointer(const HGSMIAREA *pArea, const void RT_UNTRUSTED_VOLATILE_HSTGST *pv)
{
    return (uintptr_t)pv - (uintptr_t)pArea->pu8Base < pArea->cbArea;
}

HGSMIOFFSET HGSMIBufferInitializeSingle(const HGSMIAREA *pArea, HGSMIBUFFERHEADER *pHeader, HGSMISIZE cbBuffer,
                                        uint8_t u8Channel, uint16_t u16ChannelInfo);

int  HGSMIHeapSetup(HGSMIHEAP *pHeap, void *pvBase, HGSMISIZE cbArea, HGSMIOFFSET offBase, const HGSMIENV *pEnv);
void HGSMIHeapDestroy(HGSMIHEAP *pHeap);
void RT_UNTRUSTED_VOLATILE_HSTGST *HGSMIHeapBufferAlloc(HGSMIHEAP *pHeap, HGSMISIZE cbBuffer);
void HGSMIHeapBufferFree(HGSMIHEAP *pHeap, void RT_UNTRUSTED_VOLATILE_HSTGST *pvBuf);

void RT_UNTRUSTED_VOLATILE_HOST *HGSMIHeapAlloc(HGSMIHEAP *pHeap,
                                                HGSMISIZE cbData,
                                                uint8_t u8Channel,
                                                uint16_t u16ChannelInfo);

void HGSMIHeapFree(HGSMIHEAP *pHeap, void RT_UNTRUSTED_VOLATILE_HSTGST *pvData);

DECLINLINE(const HGSMIAREA *) HGSMIHeapArea(HGSMIHEAP *pHeap)
{
    return &pHeap->area;
}

DECLINLINE(HGSMIOFFSET) HGSMIHeapOffset(HGSMIHEAP *pHeap)
{
    return HGSMIHeapArea(pHeap)->offBase;
}

DECLINLINE(HGSMISIZE) HGSMIHeapSize(HGSMIHEAP *pHeap)
{
    return HGSMIHeapArea(pHeap)->cbArea;
}

DECLINLINE(HGSMIOFFSET) HGSMIHeapBufferOffset(HGSMIHEAP *pHeap, void RT_UNTRUSTED_VOLATILE_HOST *pvData)
{
    return HGSMIBufferOffsetFromData(HGSMIHeapArea(pHeap), pvData);
}

HGSMICHANNEL *HGSMIChannelFindById(HGSMICHANNELINFO *pChannelInfo, uint8_t u8Channel);

int HGSMIChannelRegister(HGSMICHANNELINFO *pChannelInfo, uint8_t u8Channel, const char *pszName,
                         PFNHGSMICHANNELHANDLER pfnChannelHandler, void *pvChannelHandler);
int HGSMIBufferProcess(const HGSMIAREA *pArea, HGSMICHANNELINFO *pChannelInfo, HGSMIOFFSET offBuffer);
RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_Graphics_HGSMI_h */

