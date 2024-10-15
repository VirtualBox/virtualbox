/* $Id: TestExecServiceSerial.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * TestExecServ - Basic Remote Execution Service, Serial port Transport Layer.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP RTLOGGROUP_DEFAULT
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/err.h>
#include <iprt/log.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/string.h>
#include <iprt/serialport.h>
#include <iprt/thread.h>
#include <iprt/time.h>

#include "TestExecServiceInternal.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The default baud rate port. */
#define TXS_SERIAL_DEF_BAUDRATE                 115200
/** The default serial device to use. */
#if defined(RT_OS_LINUX)
# define TXS_SERIAL_DEF_DEVICE                  "/dev/ttyS0"
#elif defined(RT_OS_WINDOWS)
# define TXS_SERIAL_DEF_DEVICE                  "COM1"
#elif defined(RT_OS_SOLARIS)
# define TXS_SERIAL_DEF_DEVICE                  "<todo>"
#elif defined(RT_OS_FREEBSD)
# define TXS_SERIAL_DEF_DEVICE                  "<todo>"
#elif defined(RT_OS_DARWIN)
# define TXS_SERIAL_DEF_DEVICE                  "<todo>"
#else
# error "Port me"
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** @name Serial Parameters
 * @{ */
/** The addresses to bind to.  Empty string means any.  */
static uint32_t             g_uSerialBaudRate        = TXS_SERIAL_DEF_BAUDRATE;
/** The serial port device to use. */
static char                 g_szSerialDevice[256]    = TXS_SERIAL_DEF_DEVICE;
/** @} */

/** The serial port handle. */
static RTSERIALPORT         g_hSerialPort            = NIL_RTSERIALPORT;
/** The size of the stashed data. */
static size_t               g_cbSerialStashed        = 0;
/** The size of the stashed data allocation. */
static size_t               g_cbSerialStashedAlloced = 0;
/** The stashed data. */
static uint8_t             *g_pbSerialStashed        = NULL;



/**
 * @interface_method_impl{TXSTRANSPORT,pfnNotifyReboot}
 */
static DECLCALLBACK(void) txsSerialNotifyReboot(void)
{
    /* nothing to do here */
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnNotifyBye}
 */
static DECLCALLBACK(void) txsSerialNotifyBye(void)
{
    /* nothing to do here */
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnNotifyHowdy}
 */
static DECLCALLBACK(void) txsSerialNotifyHowdy(void)
{
    /* nothing to do here */
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnBabble}
 */
static DECLCALLBACK(void) txsSerialBabble(PCTXSPKTHDR pPktHdr, RTMSINTERVAL cMsSendTimeout)
{
    Assert(g_hSerialPort != NIL_RTSERIALPORT);

    /*
     * Try send the babble reply.
     */
    NOREF(cMsSendTimeout); /** @todo implement the timeout here; non-blocking write + select-on-write. */
    int     rc;
    size_t  cbToSend = RT_ALIGN_Z(pPktHdr->cb, TXSPKT_ALIGNMENT);
    do  rc = RTSerialPortWrite(g_hSerialPort, pPktHdr, cbToSend, NULL);
    while (rc == VERR_INTERRUPTED);

    /*
     * Disconnect the client.
     */
    Log(("txsSerialBabble: RTSerialPortWrite rc=%Rrc\n", rc));
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnSendPkt}
 */
static DECLCALLBACK(int) txsSerialSendPkt(PCTXSPKTHDR pPktHdr)
{
    Assert(g_hSerialPort != NIL_RTSERIALPORT);
    Assert(pPktHdr->cb >= sizeof(TXSPKTHDR));

    /*
     * Write it.
     */
    size_t cbToSend = RT_ALIGN_Z(pPktHdr->cb, TXSPKT_ALIGNMENT);
    int rc = RTSerialPortWrite(g_hSerialPort, pPktHdr, cbToSend, NULL);
    if (    RT_FAILURE(rc)
        &&  rc != VERR_INTERRUPTED)
    {
        /* assume fatal connection error. */
        Log(("RTSerialPortWrite -> %Rrc\n", rc));
    }

    return rc;
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnRecvPkt}
 */
static DECLCALLBACK(int) txsSerialRecvPkt(PPTXSPKTHDR ppPktHdr)
{
    Assert(g_hSerialPort != NIL_RTSERIALPORT);

    int rc = VINF_SUCCESS;
    *ppPktHdr = NULL;

    /*
     * Read state.
     */
    size_t      offData       = 0;
    size_t      cbData        = 0;
    size_t      cbDataAlloced;
    uint8_t    *pbData        = NULL;

    /*
     * Any stashed data?
     */
    if (g_cbSerialStashedAlloced)
    {
        offData               = g_cbSerialStashed;
        cbDataAlloced         = g_cbSerialStashedAlloced;
        pbData                = g_pbSerialStashed;

        g_cbSerialStashed        = 0;
        g_cbSerialStashedAlloced = 0;
        g_pbSerialStashed        = NULL;
    }
    else
    {
        cbDataAlloced = RT_ALIGN_Z(64,  TXSPKT_ALIGNMENT);
        pbData = (uint8_t *)RTMemAlloc(cbDataAlloced);
        if (!pbData)
            return VERR_NO_MEMORY;
    }

    /*
     * Read and valid the length.
     */
    while (offData < sizeof(uint32_t))
    {
        size_t cbRead = sizeof(uint32_t) - offData;
        rc = RTSerialPortRead(g_hSerialPort, pbData + offData, cbRead, NULL);
        if (RT_FAILURE(rc))
            break;
        offData += cbRead;
    }
    if (RT_SUCCESS(rc))
    {
        ASMCompilerBarrier(); /* paranoia^3 */
        cbData = *(uint32_t volatile *)pbData;
        if (cbData >= sizeof(TXSPKTHDR) && cbData <= TXSPKT_MAX_SIZE)
        {
            /*
             * Align the length and reallocate the return packet it necessary.
             */
            cbData = RT_ALIGN_Z(cbData, TXSPKT_ALIGNMENT);
            if (cbData > cbDataAlloced)
            {
                void *pvNew = RTMemRealloc(pbData, cbData);
                if (pvNew)
                {
                    pbData = (uint8_t *)pvNew;
                    cbDataAlloced = cbData;
                }
                else
                    rc = VERR_NO_MEMORY;
            }
            if (RT_SUCCESS(rc))
            {
                /*
                 * Read the remainder of the data.
                 */
                while (offData < cbData)
                {
                    size_t cbRead = cbData - offData;
                    rc = RTSerialPortRead(g_hSerialPort, pbData + offData, cbRead, NULL);
                    if (RT_FAILURE(rc))
                        break;
                    offData += cbRead;
                }
            }
        }
        else
            rc = VERR_NET_PROTOCOL_ERROR;
    }
    if (RT_SUCCESS(rc))
        *ppPktHdr = (PTXSPKTHDR)pbData;
    else
    {
        /*
         * Deal with errors.
         */
        if (rc == VERR_INTERRUPTED)
        {
            /* stash it away for the next call. */
            g_cbSerialStashed        = cbData;
            g_cbSerialStashedAlloced = cbDataAlloced;
            g_pbSerialStashed        = pbData;
        }
        else
        {
            RTMemFree(pbData);

            /* assume fatal connection error. */
            Log(("txsSerialRecvPkt: RTSerialPortRead -> %Rrc\n", rc));
        }
    }

    return rc;
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnPollIn}
 */
static DECLCALLBACK(bool) txsSerialPollIn(void)
{
    Assert(g_hSerialPort != NIL_RTSERIALPORT);

    uint32_t fEvtsRecv = 0;
    int rc = RTSerialPortEvtPoll(g_hSerialPort, RTSERIALPORT_EVT_F_DATA_RX,
                                 &fEvtsRecv, 0/*cMillies*/);
    return RT_SUCCESS(rc);
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnTerm}
 */
static DECLCALLBACK(void) txsSerialTerm(void)
{
    if (g_hSerialPort != NIL_RTSERIALPORT)
        RTSerialPortClose(g_hSerialPort);

    /* Clean up stashing. */
    if (g_pbSerialStashed)
        RTMemFree(g_pbSerialStashed);
    g_pbSerialStashed          = NULL;
    g_cbSerialStashed          = 0;
    g_cbSerialStashedAlloced   = 0;

    Log(("txsSerialTerm: done\n"));
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnInit}
 */
static DECLCALLBACK(int) txsSerialInit(void)
{
    uint32_t fOpenFlags = RTSERIALPORT_OPEN_F_READ | RTSERIALPORT_OPEN_F_WRITE;
    int rc = RTSerialPortOpen(&g_hSerialPort, &g_szSerialDevice[0], fOpenFlags);
    if (RT_SUCCESS(rc))
    {
        RTSERIALPORTCFG SerPortCfg;

        SerPortCfg.uBaudRate       = g_uSerialBaudRate;
        SerPortCfg.enmParity       = RTSERIALPORTPARITY_NONE;
        SerPortCfg.enmDataBitCount = RTSERIALPORTDATABITS_8BITS;
        SerPortCfg.enmStopBitCount = RTSERIALPORTSTOPBITS_ONE;
        rc = RTSerialPortCfgSet(g_hSerialPort, &SerPortCfg, NULL);
        if (RT_FAILURE(rc))
        {
            RTMsgError("RTSerialPortCfgSet() failed: %Rrc\n", rc);
            RTSerialPortClose(g_hSerialPort);
            g_hSerialPort = NIL_RTSERIALPORT;
        }
    }
    else
        RTMsgError("RTSerialPortOpen(, %s, %#x) failed: %Rrc\n",
                   g_szSerialDevice, fOpenFlags, rc);

    return rc;
}

/** Options  */
enum TXSSERIALOPT
{
    TXSSERIALOPT_BAUDRATE = 1000,
    TXSSERIALOPT_DEVICE
};

/**
 * @interface_method_impl{TXSTRANSPORT,pfnOption}
 */
static DECLCALLBACK(int) txsSerialOption(int ch, PCRTGETOPTUNION pVal)
{
    int rc;

    switch (ch)
    {
        case TXSSERIALOPT_DEVICE:
            rc = RTStrCopy(g_szSerialDevice, sizeof(g_szSerialDevice), pVal->psz);
            if (RT_FAILURE(rc))
                return RTMsgErrorRc(VERR_INVALID_PARAMETER, "Serial port device path is too long (%Rrc)", rc);
            if (!g_szSerialDevice[0])
                strcpy(g_szSerialDevice, TXS_SERIAL_DEF_DEVICE);
            return VINF_SUCCESS;
        case TXSSERIALOPT_BAUDRATE:
            g_uSerialBaudRate = pVal->u32 == 0 ? TXS_SERIAL_DEF_BAUDRATE : pVal->u32;
            return VINF_SUCCESS;
    }
    return VERR_TRY_AGAIN;
}

/**
 * @interface_method_impl{TXSTRANSPORT,pfnUsage}
 */
static DECLCALLBACK(void) txsSerialUsage(PRTSTREAM pStream)
{
    RTStrmPrintf(pStream,
                 "  --serial-device <device>\n"
                 "       Selects the serial port to use.\n"
                 "       Default: %s\n"
                 "  --serial-baudrate <baudrate>\n"
                 "       Selects the baudrate to set the serial port to.\n"
                 "       Default: %u\n"
                 , TXS_SERIAL_DEF_DEVICE, TXS_SERIAL_DEF_BAUDRATE);
}

/** Command line options for the serial transport layer. */
static const RTGETOPTDEF  g_SerialOpts[] =
{
    { "--serial-device",        TXSSERIALOPT_DEVICE,           RTGETOPT_REQ_STRING },
    { "--serial-baudrate",      TXSSERIALOPT_BAUDRATE,         RTGETOPT_REQ_UINT32 }
};

/** Serial port transport layer. */
const TXSTRANSPORT g_SerialTransport =
{
    /* .szName          = */ "serial",
    /* .pszDesc         = */ "Serial",
    /* .cOpts           = */ &g_SerialOpts[0],
    /* .paOpts          = */ RT_ELEMENTS(g_SerialOpts),
    /* .pfnUsage        = */ txsSerialUsage,
    /* .pfnOption       = */ txsSerialOption,
    /* .pfnInit         = */ txsSerialInit,
    /* .pfnTerm         = */ txsSerialTerm,
    /* .pfnPollIn       = */ txsSerialPollIn,
    /* .pfnPollSetAdd   = */ NULL,
    /* .pfnRecvPkt      = */ txsSerialRecvPkt,
    /* .pfnSendPkt      = */ txsSerialSendPkt,
    /* .pfnBabble       = */ txsSerialBabble,
    /* .pfnNotifyHowdy  = */ txsSerialNotifyHowdy,
    /* .pfnNotifyBye    = */ txsSerialNotifyBye,
    /* .pfnNotifyReboot = */ txsSerialNotifyReboot,
    /* .u32EndMarker    = */ UINT32_C(0x12345678)
};

