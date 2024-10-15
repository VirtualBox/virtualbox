/* $Id: Pcap.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Helpers for writing libpcap files.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "Pcap.h"

#include <iprt/file.h>
#include <iprt/stream.h>
#include <iprt/time.h>
#include <iprt/errcore.h>
#include <VBox/vmm/pdmnetinline.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/* "libpcap" magic */
#define PCAP_MAGIC  0xa1b2c3d4

/* "libpcap" file header (minus magic number). */
struct pcap_hdr
{
    uint16_t    version_major;  /* major version number                         = 2 */
    uint16_t    version_minor;  /* minor version number                         = 4 */
    int32_t     thiszone;       /* GMT to local correction                      = 0 */
    uint32_t    sigfigs;        /* accuracy of timestamps                       = 0 */
    uint32_t    snaplen;        /* max length of captured packets, in octets    = 0xffff */
    uint32_t    network;        /* data link type                               = 01 */
};

/* "libpcap" record header. */
struct pcaprec_hdr
{
    uint32_t    ts_sec;         /* timestamp seconds */
    uint32_t    ts_usec;        /* timestamp microseconds */
    uint32_t    incl_len;       /* number of octets of packet saved in file */
    uint32_t    orig_len;       /* actual length of packet */
};

struct pcaprec_hdr_init
{
    uint32_t            u32Magic;
    struct pcap_hdr     pcap;
};


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static pcaprec_hdr_init const s_Hdr =
{
    PCAP_MAGIC,
    { 2, 4, 0, 0, 0xffff, 1 },
};

static const char s_szDummyData[] = { 0, 0, 0, 0 };

/**
 * Internal helper.
 */
static void pcapCalcHeader(struct pcaprec_hdr *pHdr, uint64_t StartNanoTS, size_t cbFrame, size_t cbMax)
{
    uint64_t u64TS = RTTimeNanoTS() - StartNanoTS;
    pHdr->ts_sec   = (uint32_t)(u64TS / 1000000000);
    pHdr->ts_usec  = (uint32_t)((u64TS / 1000) % 1000000);
    pHdr->incl_len = (uint32_t)RT_MIN(cbFrame, cbMax);
    pHdr->orig_len = (uint32_t)cbFrame;
}


/**
 * Internal helper.
 */
static void pcapUpdateHeader(struct pcaprec_hdr *pHdr, size_t cbFrame, size_t cbMax)
{
    pHdr->incl_len = (uint32_t)RT_MIN(cbFrame, cbMax);
    pHdr->orig_len = (uint32_t)cbFrame;
}


/**
 * Writes the stream header.
 *
 * @returns IPRT status code, @see RTStrmWrite.
 *
 * @param   pStream         The stream handle.
 * @param   StartNanoTS     What to subtract from the RTTimeNanoTS output.
 */
int PcapStreamHdr(PRTSTREAM pStream, uint64_t StartNanoTS)
{
    int rc1 = RTStrmWrite(pStream, &s_Hdr, sizeof(s_Hdr));
    int rc2 = PcapStreamFrame(pStream, StartNanoTS, s_szDummyData, 60, sizeof(s_szDummyData));
    return RT_SUCCESS(rc1) ? rc2 : rc1;
}


/**
 * Writes a frame to a stream.
 *
 * @returns IPRT status code, @see RTStrmWrite.
 *
 * @param   pStream         The stream handle.
 * @param   StartNanoTS     What to subtract from the RTTimeNanoTS output.
 * @param   pvFrame         The start of the frame.
 * @param   cbFrame         The size of the frame.
 * @param   cbMax           The max number of bytes to include in the file.
 */
int PcapStreamFrame(PRTSTREAM pStream, uint64_t StartNanoTS, const void *pvFrame, size_t cbFrame, size_t cbMax)
{
    struct pcaprec_hdr Hdr;
    pcapCalcHeader(&Hdr, StartNanoTS, cbFrame, cbMax);
    int rc1 = RTStrmWrite(pStream, &Hdr, sizeof(Hdr));
    int rc2 = RTStrmWrite(pStream, pvFrame, Hdr.incl_len);
    return RT_SUCCESS(rc1) ? rc2 : rc1;
}


/**
 * Writes a GSO frame to a stream.
 *
 * @returns IPRT status code, @see RTStrmWrite.
 *
 * @param   pStream         The stream handle.
 * @param   StartNanoTS     What to subtract from the RTTimeNanoTS output.
 * @param   pGso            Pointer to the GSO context.
 * @param   pvFrame         The start of the GSO frame.
 * @param   cbFrame         The size of the GSO frame.
 * @param   cbSegMax        The max number of bytes to include in the file for
 *                          each segment.
 */
int PcapStreamGsoFrame(PRTSTREAM pStream, uint64_t StartNanoTS, PCPDMNETWORKGSO pGso,
                       const void *pvFrame, size_t cbFrame, size_t cbSegMax)
{
    struct pcaprec_hdr Hdr;
    pcapCalcHeader(&Hdr, StartNanoTS, 0, 0);

    uint8_t const  *pbFrame = (uint8_t const *)pvFrame;
    uint8_t         abHdrs[256];
    uint32_t const  cSegs   = PDMNetGsoCalcSegmentCount(pGso, cbFrame);
    for (uint32_t iSeg = 0; iSeg < cSegs; iSeg++)
    {
        uint32_t cbSegPayload, cbHdrs;
        uint32_t offSegPayload = PDMNetGsoCarveSegment(pGso, pbFrame, cbFrame, iSeg, cSegs, abHdrs, &cbHdrs, &cbSegPayload);

        pcapUpdateHeader(&Hdr, cbHdrs + cbSegPayload, cbSegMax);
        int rc = RTStrmWrite(pStream, &Hdr, sizeof(Hdr));
        if (RT_FAILURE(rc))
            return rc;

        rc = RTStrmWrite(pStream, abHdrs, RT_MIN(Hdr.incl_len, cbHdrs));
        if (RT_SUCCESS(rc) && Hdr.incl_len > cbHdrs)
            rc = RTStrmWrite(pStream, pbFrame + offSegPayload, Hdr.incl_len - cbHdrs);
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}


/**
 * Writes the file header.
 *
 * @returns IPRT status code, @see RTFileWrite.
 *
 * @param   File            The file handle.
 * @param   StartNanoTS     What to subtract from the RTTimeNanoTS output.
 */
int PcapFileHdr(RTFILE File, uint64_t StartNanoTS)
{
    int rc1 = RTFileWrite(File, &s_Hdr, sizeof(s_Hdr), NULL);
    int rc2 = PcapFileFrame(File, StartNanoTS, s_szDummyData, 60, sizeof(s_szDummyData));
    return RT_SUCCESS(rc1) ? rc2 : rc1;
}


/**
 * Writes a frame to a file.
 *
 * @returns IPRT status code, @see RTFileWrite.
 *
 * @param   File            The file handle.
 * @param   StartNanoTS     What to subtract from the RTTimeNanoTS output.
 * @param   pvFrame         The start of the frame.
 * @param   cbFrame         The size of the frame.
 * @param   cbMax           The max number of bytes to include in the file.
 */
int PcapFileFrame(RTFILE File, uint64_t StartNanoTS, const void *pvFrame, size_t cbFrame, size_t cbMax)
{
    struct pcaprec_hdr  Hdr;
    pcapCalcHeader(&Hdr, StartNanoTS, cbFrame, cbMax);
    int rc1 = RTFileWrite(File, &Hdr, sizeof(Hdr), NULL);
    int rc2 = RTFileWrite(File, pvFrame, Hdr.incl_len, NULL);
    return RT_SUCCESS(rc1) ? rc2 : rc1;
}


/**
 * Writes a GSO frame to a file.
 *
 * @returns IPRT status code, @see RTFileWrite.
 *
 * @param   File            The file handle.
 * @param   StartNanoTS     What to subtract from the RTTimeNanoTS output.
 * @param   pGso            Pointer to the GSO context.
 * @param   pvFrame         The start of the GSO frame.
 * @param   cbFrame         The size of the GSO frame.
 * @param   cbSegMax        The max number of bytes to include in the file for
 *                          each segment.
 */
int PcapFileGsoFrame(RTFILE File, uint64_t StartNanoTS, PCPDMNETWORKGSO pGso,
                     const void *pvFrame, size_t cbFrame, size_t cbSegMax)
{
    struct pcaprec_hdr Hdr;
    pcapCalcHeader(&Hdr, StartNanoTS, 0, 0);

    uint8_t const  *pbFrame = (uint8_t const *)pvFrame;
    uint8_t         abHdrs[256];
    uint32_t const  cSegs   = PDMNetGsoCalcSegmentCount(pGso, cbFrame);
    for (uint32_t iSeg = 0; iSeg < cSegs; iSeg++)
    {
        uint32_t cbSegPayload, cbHdrs;
        uint32_t offSegPayload = PDMNetGsoCarveSegment(pGso, pbFrame, cbFrame, iSeg, cSegs, abHdrs, &cbHdrs, &cbSegPayload);

        pcapUpdateHeader(&Hdr, cbHdrs + cbSegPayload, cbSegMax);
        int rc = RTFileWrite(File, &Hdr, sizeof(Hdr), NULL);
        if (RT_FAILURE(rc))
            return rc;

        rc = RTFileWrite(File, abHdrs, RT_MIN(Hdr.incl_len, cbHdrs), NULL);
        if (RT_SUCCESS(rc) && Hdr.incl_len > cbHdrs)
            rc = RTFileWrite(File, pbFrame + offSegPayload, Hdr.incl_len - cbHdrs, NULL);
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}

