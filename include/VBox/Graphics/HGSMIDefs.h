/* $Id: HGSMIDefs.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Host Guest Shared Memory Interface (HGSMI) - shared part - types and defines.
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

#ifndef VBOX_INCLUDED_Graphics_HGSMIDefs_h
#define VBOX_INCLUDED_Graphics_HGSMIDefs_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VBoxVideoIPRT.h"

/* HGSMI uses 32 bit offsets and sizes. */
typedef uint32_t HGSMISIZE;
typedef uint32_t HGSMIOFFSET;

#define HGSMIOFFSET_VOID ((HGSMIOFFSET)~0)

/**
 * Describes a shared memory area buffer.
 *
 * Used for calculations with offsets and for buffers verification.
 */
typedef struct HGSMIAREA
{
    uint8_t     *pu8Base; /**< The starting address of the area. Corresponds to offset 'offBase'. */
    HGSMIOFFSET  offBase; /**< The starting offset of the area. */
    HGSMIOFFSET  offLast; /**< The last valid offset:  offBase + cbArea - 1 - (sizeof(header) + sizeof(tail)). */
    HGSMISIZE    cbArea;  /**< Size of the area. */
} HGSMIAREA;


/* The buffer description flags. */
#define HGSMI_BUFFER_HEADER_F_SEQ_MASK     0x03 /* Buffer sequence type mask. */
#define HGSMI_BUFFER_HEADER_F_SEQ_SINGLE   0x00 /* Single buffer, not a part of a sequence. */
#define HGSMI_BUFFER_HEADER_F_SEQ_START    0x01 /* The first buffer in a sequence. */
#define HGSMI_BUFFER_HEADER_F_SEQ_CONTINUE 0x02 /* A middle buffer in a sequence. */
#define HGSMI_BUFFER_HEADER_F_SEQ_END      0x03 /* The last buffer in a sequence. */


#pragma pack(1) /** @todo not necessary. use AssertCompileSize instead. */
/* 16 bytes buffer header. */
typedef struct HGSMIBUFFERHEADER
{
    uint32_t    u32DataSize;            /* Size of data that follows the header. */

    uint8_t     u8Flags;                /* The buffer description: HGSMI_BUFFER_HEADER_F_* */

    uint8_t     u8Channel;              /* The channel the data must be routed to. */
    uint16_t    u16ChannelInfo;         /* Opaque to the HGSMI, used by the channel. */

    union {
        uint8_t au8Union[8];            /* Opaque placeholder to make the union 8 bytes. */

        struct
        {                               /* HGSMI_BUFFER_HEADER_F_SEQ_SINGLE */
            uint32_t u32Reserved1;      /* A reserved field, initialize to 0. */
            uint32_t u32Reserved2;      /* A reserved field, initialize to 0. */
        } Buffer;

        struct
        {                               /* HGSMI_BUFFER_HEADER_F_SEQ_START */
            uint32_t u32SequenceNumber; /* The sequence number, the same for all buffers in the sequence. */
            uint32_t u32SequenceSize;   /* The total size of the sequence. */
        } SequenceStart;

        struct
        {                               /* HGSMI_BUFFER_HEADER_F_SEQ_CONTINUE and HGSMI_BUFFER_HEADER_F_SEQ_END */
            uint32_t u32SequenceNumber; /* The sequence number, the same for all buffers in the sequence. */
            uint32_t u32SequenceOffset; /* Data offset in the entire sequence. */
        } SequenceContinue;
    } u;
} HGSMIBUFFERHEADER;

/* 8 bytes buffer tail. */
typedef struct HGSMIBUFFERTAIL
{
    uint32_t    u32Reserved;        /* Reserved, must be initialized to 0. */
    uint32_t    u32Checksum;        /* Verifyer for the buffer header and offset and for first 4 bytes of the tail. */
} HGSMIBUFFERTAIL;
#pragma pack()

AssertCompileSize(HGSMIBUFFERHEADER, 16);
AssertCompileSize(HGSMIBUFFERTAIL, 8);

/* The size of the array of channels. Array indexes are uint8_t. Note: the value must not be changed. */
#define HGSMI_NUMBER_OF_CHANNELS 0x100

typedef struct HGSMIENV
{
    /* Environment context pointer. */
    void *pvEnv;

    /* Allocate system memory. */
    DECLCALLBACKMEMBER(void *, pfnAlloc,(void *pvEnv, HGSMISIZE cb));

    /* Free system memory. */
    DECLCALLBACKMEMBER(void, pfnFree,(void *pvEnv, void *pv));
} HGSMIENV;

#endif /* !VBOX_INCLUDED_Graphics_HGSMIDefs_h */

