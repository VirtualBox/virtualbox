/* $Id: serialport-os2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Serial Port API, OS/2 Implementation.
 */

/*
 * Copyright (C) 2017-2024 Oracle and/or its affiliates.
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
#define INCL_BASE
#define INCL_DOSFILEMGR
#define INCL_ERRORS
#define INCL_DOS
#define INCL_DOSDEVIOCTL
#define INCL_DOSDEVICES
#include <os2.h>
#undef RT_MAX

#include <iprt/serialport.h>
#include "internal/iprt.h"

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/cdefs.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include "internal/magics.h"



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/**
 * Returned data structure for ASYNC_EXTGETBAUDRATE.
 */
typedef struct OS2EXTGETBAUDRATEDATA
{
    /** Current bit rate. */
    ULONG               uBitRateCur;
    /** Fraction of the current bit rate. */
    BYTE                bBitRateCurFrac;
    /** Minimum supported bit rate. */
    ULONG               uBitRateMin;
    /** Fraction of the minimum bit rate. */
    BYTE                bBitRateCurMin;
    /** Maximum supported bit rate. */
    ULONG               uBitRateMax;
    /** Fraction of the maximum bit rate. */
    BYTE                bBitRateCurMax;
} OS2EXTGETBAUDRATEDATA;
/** Pointer to the get extended baud rate data packet. */
typedef OS2EXTGETBAUDRATEDATA *POS2EXTGETBAUDRATEDATA;


/**
 * Data packet for the ASYNC_EXTSETBAUDRATE ioctl.
 */
typedef struct OS2EXTSETBAUDRATEDATA
{
    /** Current bit rate. */
    ULONG               uBitRate;
    /** Fraction of the current bit rate. */
    BYTE                bBitRateFrac;
} OS2EXTSETBAUDRATEDATA;
/** Pointer to the set extended baud rate data packet. */
typedef OS2EXTSETBAUDRATEDATA *POS2EXTSETBAUDRATEDATA;


/**
 * Data packet for the ASYNC_GETLINECTRL ioctl.
 */
typedef struct OS2GETLINECTRLDATA
{
    /** Returns the current amount of data bits in a symbol used for the communication. */
    BYTE                bDataBits;
    /** Current parity setting. */
    BYTE                bParity;
    /** Current number of stop bits. */
    BYTE                bStopBits;
    /** Flag whether a break condition is currently transmitted on the line. */
    BYTE                bTxBrk;
} OS2GETLINECTRLDATA;
/** Pointer to the get line control data packet. */
typedef OS2GETLINECTRLDATA *POS2GETLINECTRLDATA;


/**
 * Data packet for the ASYNC_SETLINECTRL ioctl.
 */
typedef struct OS2SETLINECTRLDATA
{
    /** Returns the current amount of data bits in a symbol used for the communication. */
    BYTE                bDataBits;
    /** Current parity setting. */
    BYTE                bParity;
    /** Current number of stop bits. */
    BYTE                bStopBits;
} OS2SETLINECTRLDATA;
/** Pointer to the get line control data packet. */
typedef OS2SETLINECTRLDATA *POS2SETLINECTRLDATA;


/**
 * Internal serial port state.
 */
typedef struct RTSERIALPORTINTERNAL
{
    /** Magic value (RTSERIALPORT_MAGIC). */
    uint32_t            u32Magic;
    /** Flags given while opening the serial port. */
    uint32_t            fOpenFlags;
    /** The file descriptor of the serial port. */
    HFILE               hDev;
    /** Flag whether blocking mode is currently enabled. */
    bool                fBlocking;
    /** Flag whether RTSerialPortEvtPoll() was interrupted by RTSerialPortEvtPollInterrupt(). */
    volatile bool       fInterrupt;
} RTSERIALPORTINTERNAL;
/** Pointer to the internal serial port state. */
typedef RTSERIALPORTINTERNAL *PRTSERIALPORTINTERNAL;



/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** Indicator whether the CTS input is set/clear. */
#define OS2_GET_MODEM_INPUT_CTS         RT_BIT(4)
/** Indicator whether the DSR input is set/clear. */
#define OS2_GET_MODEM_INPUT_DSR         RT_BIT(5)
/** Indicator whether the RI input is set/clear. */
#define OS2_GET_MODEM_INPUT_RI          RT_BIT(6)
/** Indicator whether the DCD input is set/clear. */
#define OS2_GET_MODEM_INPUT_DCD         RT_BIT(7)

/** There is something to read on the serial port. */
#define OS2_GET_COMM_EVT_RX             RT_BIT(0)
/** A receive timeout interrupt was generated on the serial port during a read request. */
#define OS2_GET_COMM_EVT_RTI            RT_BIT(1)
/** The transmit queue for the serial port is empty. */
#define OS2_GET_COMM_EVT_TX_EMPTY       RT_BIT(2)
/** The CTS signal changes state. */
#define OS2_GET_COMM_EVT_CTS_CHG        RT_BIT(3)
/** The DSR signal changes state. */
#define OS2_GET_COMM_EVT_DSR_CHG        RT_BIT(4)
/** The DCD signal changes state. */
#define OS2_GET_COMM_EVT_DCD_CHG        RT_BIT(5)
/** A break condition was detected on the serial port. */
#define OS2_GET_COMM_EVT_BRK            RT_BIT(6)
/** A parity, framing or receive hardware overrun error occurred. */
#define OS2_GET_COMM_EVT_COMM_ERR       RT_BIT(7)
/** Trailing edge ring indicator was detected. */
#define OS2_GET_COMM_EVT_RI_TRAIL_EDGE  RT_BIT(8)


/*********************************************************************************************************************************
*   Global variables                                                                                                             *
*********************************************************************************************************************************/
/** OS/2 parity value to IPRT parity enum. */
static RTSERIALPORTPARITY s_aParityConvTbl[] =
{
    RTSERIALPORTPARITY_NONE,
    RTSERIALPORTPARITY_ODD,
    RTSERIALPORTPARITY_EVEN,
    RTSERIALPORTPARITY_MARK,
    RTSERIALPORTPARITY_SPACE
};

/** OS/2 data bits value to IPRT data bits enum. */
static RTSERIALPORTDATABITS s_aDataBitsConvTbl[] =
{
    RTSERIALPORTDATABITS_INVALID,
    RTSERIALPORTDATABITS_INVALID,
    RTSERIALPORTDATABITS_INVALID,
    RTSERIALPORTDATABITS_INVALID,
    RTSERIALPORTDATABITS_INVALID,
    RTSERIALPORTDATABITS_5BITS,
    RTSERIALPORTDATABITS_6BITS,
    RTSERIALPORTDATABITS_7BITS,
    RTSERIALPORTDATABITS_8BITS
};

/** OS/2 stop bits value to IPRT stop bits enum. */
static RTSERIALPORTSTOPBITS s_aStopBitsConvTbl[] =
{
    RTSERIALPORTSTOPBITS_ONE,
    RTSERIALPORTSTOPBITS_ONEPOINTFIVE,
    RTSERIALPORTSTOPBITS_TWO
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * The slow path of rtSerialPortSwitchBlockingMode that does the actual switching.
 *
 * @returns IPRT status code.
 * @param   pThis                   The internal serial port instance data.
 * @param   fBlocking               The desired mode of operation.
 * @remarks Do not call directly.
 *
 * @note Affects only read behavior.
 */
static int rtSerialPortSwitchBlockingModeSlow(PRTSERIALPORTINTERNAL pThis, bool fBlocking)
{
    DCBINFO DcbInfo;
    ULONG cbDcbInfo = sizeof(DcbInfo);
    ULONG rcOs2 = DosDevIOCtl(pThis->hDev, IOCTL_ASYNC, ASYNC_GETDCBINFO, NULL, 0, NULL, &DcbInfo, cbDcbInfo, &cbDcbInfo);
    if (!rcOs2)
    {
        DcbInfo.fbTimeout &= ~0x06;
        DcbInfo.fbTimeout |= fBlocking ? 0x04 : 0x06;
        rcOs2 = DosDevIOCtl(pThis->hDev, IOCTL_ASYNC, ASYNC_SETDCBINFO, &DcbInfo, cbDcbInfo, &cbDcbInfo, NULL, 0, NULL);
        if (rcOs2)
            return RTErrConvertFromOS2(rcOs2);
    }
    else
        return RTErrConvertFromOS2(rcOs2);

    pThis->fBlocking = fBlocking;
    return VINF_SUCCESS;
}


/**
 * Switches the serial port to the desired blocking mode if necessary.
 *
 * @returns IPRT status code.
 * @param   pThis                   The internal serial port instance data.
 * @param   fBlocking               The desired mode of operation.
 *
 * @note Affects only read behavior.
 */
DECLINLINE(int) rtSerialPortSwitchBlockingMode(PRTSERIALPORTINTERNAL pThis, bool fBlocking)
{
    if (pThis->fBlocking != fBlocking)
        return rtSerialPortSwitchBlockingModeSlow(pThis, fBlocking);
    return VINF_SUCCESS;
}


RTDECL(int)  RTSerialPortOpen(PRTSERIALPORT phSerialPort, const char *pszPortAddress, uint32_t fFlags)
{
    AssertPtrReturn(phSerialPort, VERR_INVALID_POINTER);
    AssertPtrReturn(pszPortAddress, VERR_INVALID_POINTER);
    AssertReturn(*pszPortAddress != '\0', VERR_INVALID_PARAMETER);
    AssertReturn(!(fFlags & ~RTSERIALPORT_OPEN_F_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertReturn((fFlags & RTSERIALPORT_OPEN_F_READ) || (fFlags & RTSERIALPORT_OPEN_F_WRITE),
                 VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;
    PRTSERIALPORTINTERNAL pThis = (PRTSERIALPORTINTERNAL)RTMemAllocZ(sizeof(*pThis));
    if (pThis)
    {
        ULONG fOpenMode =   OPEN_SHARE_DENYREADWRITE
                          | OPEN_FLAGS_SEQUENTIAL
                          | OPEN_FLAGS_NOINHERIT
                          | OPEN_FLAGS_FAIL_ON_ERROR;

        if ((fFlags & RTSERIALPORT_OPEN_F_READ) && !(fFlags & RTSERIALPORT_OPEN_F_WRITE))
            fOpenMode |= OPEN_ACCESS_READONLY;
        else if (!(fFlags & RTSERIALPORT_OPEN_F_READ) && (fFlags & RTSERIALPORT_OPEN_F_WRITE))
            fOpenMode |= OPEN_ACCESS_WRITEONLY;
        else
            fOpenMode |= OPEN_ACCESS_READWRITE;

        pThis->u32Magic     = RTSERIALPORT_MAGIC;
        pThis->fOpenFlags   = fFlags;
        pThis->fInterrupt   = false;
        pThis->fBlocking    = true;

        ULONG uAction = 0;
        ULONG rcOs2 = DosOpen((const UCHAR *)pszPortAddress, &pThis->hDev, &uAction, 0, FILE_NORMAL, FILE_OPEN, fOpenMode, NULL);
        if (!rcOs2)
        {
            /* Switch to a known read blocking mode. */
            rc = rtSerialPortSwitchBlockingMode(pThis, false);
            if (RT_SUCCESS(rc))
            {
                *phSerialPort = pThis;
                return VINF_SUCCESS;
            }

            DosClose(pThis->hDev);
        }
        else
            rc = RTErrConvertFromOS2(rcOs2);

        RTMemFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


RTDECL(int)  RTSerialPortClose(RTSERIALPORT hSerialPort)
{
    PRTSERIALPORTINTERNAL pThis = hSerialPort;
    if (pThis == NIL_RTSERIALPORT)
        return VINF_SUCCESS;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSERIALPORT_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Do the cleanup.
     */
    AssertReturn(ASMAtomicCmpXchgU32(&pThis->u32Magic, RTSERIALPORT_MAGIC_DEAD, RTSERIALPORT_MAGIC), VERR_INVALID_HANDLE);

    DosClose(pThis->hDev);
    RTMemFree(pThis);
    return VINF_SUCCESS;
}


RTDECL(RTHCINTPTR) RTSerialPortToNative(RTSERIALPORT hSerialPort)
{
    PRTSERIALPORTINTERNAL pThis = hSerialPort;
    AssertPtrReturn(pThis, -1);
    AssertReturn(pThis->u32Magic == RTSERIALPORT_MAGIC, -1);

    return pThis->hDev;
}


RTDECL(int) RTSerialPortRead(RTSERIALPORT hSerialPort, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    PRTSERIALPORTINTERNAL pThis = hSerialPort;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSERIALPORT_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbToRead > 0, VERR_INVALID_PARAMETER);

    int rc = rtSerialPortSwitchBlockingMode(pThis, true);
    if (RT_SUCCESS(rc))
    {
        /*
         * Attempt read.
         */
        ULONG cbRead = 0;
        ULONG rcOs2 = DosRead(pThis->hDev, pvBuf, cbToRead, &cbRead);
        if (!rcOs2)
        {
            if (pcbRead)
                /* caller can handle partial read. */
                *pcbRead = cbRead;
            else
            {
                /* Caller expects all to be read. */
                while (cbToRead > cbRead)
                {
                    ULONG cbReadPart = 0;
                    rcOs2 = DosRead(pThis->hDev, (uint8_t *)pvBuf + cbRead, cbToRead - cbRead, &cbReadPart);
                    if (rcOs2)
                        return RTErrConvertFromOS2(rcOs2);

                    cbRead += cbReadPart;
                }
            }
        }
        else
            rc = RTErrConvertFromOS2(rcOs2);
    }

    return rc;
}


RTDECL(int) RTSerialPortReadNB(RTSERIALPORT hSerialPort, void *pvBuf, size_t cbToRead, size_t *pcbRead)
{
    PRTSERIALPORTINTERNAL pThis = hSerialPort;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSERIALPORT_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbToRead > 0, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbRead, VERR_INVALID_POINTER);

    *pcbRead = 0;

    int rc = rtSerialPortSwitchBlockingMode(pThis, false);
    if (RT_SUCCESS(rc))
    {
        ULONG cbThisRead = 0;
        ULONG rcOs2 = DosRead(pThis->hDev, pvBuf, cbToRead, &cbThisRead);
        if (!rcOs2)
        {
            *pcbRead = cbThisRead;

            if (cbThisRead == 0)
                rc = VINF_TRY_AGAIN;
        }
        else
            rc = RTErrConvertFromOS2(rcOs2);
    }

    return rc;
}


RTDECL(int) RTSerialPortWrite(RTSERIALPORT hSerialPort, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten)
{
    PRTSERIALPORTINTERNAL pThis = hSerialPort;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSERIALPORT_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbToWrite > 0, VERR_INVALID_PARAMETER);

    /*
     * Attempt write.
     */
    int rc = VINF_SUCCESS;
    ULONG cbThisWritten = 0;
    ULONG rcOs2 = DosWrite(pThis->hDev, pvBuf, cbToWrite, &cbThisWritten);
    if (!rcOs2)
    {
        if (pcbWritten)
            /* caller can handle partial write. */
            *pcbWritten = cbThisWritten;
        else
        {
            /** @todo Wait for TX empty and loop. */
            rc = VERR_NOT_SUPPORTED;
        }
    }
    else
        rc = RTErrConvertFromOS2(rcOs2);

    return rc;
}


RTDECL(int) RTSerialPortWriteNB(RTSERIALPORT hSerialPort, const void *pvBuf, size_t cbToWrite, size_t *pcbWritten)
{
    PRTSERIALPORTINTERNAL pThis = hSerialPort;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSERIALPORT_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbToWrite > 0, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pcbWritten, VERR_INVALID_POINTER);

    *pcbWritten = 0;

    int rc = VINF_SUCCESS;
    ULONG cbThisWritten = 0;
    ULONG rcOs2 = DosWrite(pThis->hDev, pvBuf, cbToWrite, &cbThisWritten);
    if (!rcOs2)
    {
        *pcbWritten = cbThisWritten;
        if (!cbThisWritten)
            rc = VINF_TRY_AGAIN;
    }
    else
        rc = RTErrConvertFromOS2(rcOs2);

    return rc;
}


RTDECL(int) RTSerialPortCfgQueryCurrent(RTSERIALPORT hSerialPort, PRTSERIALPORTCFG pCfg)
{
    PRTSERIALPORTINTERNAL pThis = hSerialPort;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSERIALPORT_MAGIC, VERR_INVALID_HANDLE);

    int rc = VINF_SUCCESS;
    OS2EXTGETBAUDRATEDATA ExtBaudRate;
    ULONG cbExtBaudRate = sizeof(ExtBaudRate);
    ULONG rcOs2 = DosDevIOCtl(pThis->hDev, IOCTL_ASYNC, ASYNC_EXTGETBAUDRATE, NULL, 0, NULL, &ExtBaudRate, cbExtBaudRate, &cbExtBaudRate);
    if (!rcOs2)
    {
        OS2GETLINECTRLDATA LineCtrl;
        ULONG cbLineCtrl = sizeof(LineCtrl);
        rcOs2 = DosDevIOCtl(pThis->hDev, IOCTL_ASYNC, ASYNC_GETLINECTRL, NULL, 0, NULL, &LineCtrl, cbLineCtrl, &cbLineCtrl);
        if (!rcOs2)
        {
            pCfg->uBaudRate = ExtBaudRate.uBitRateCur;
            if (LineCtrl.bParity < RT_ELEMENTS(s_aParityConvTbl))
                pCfg->enmParity = s_aParityConvTbl[LineCtrl.bParity];
            else
                rc = VERR_IPE_UNEXPECTED_STATUS;

            if (   RT_SUCCESS(rc)
                && LineCtrl.bDataBits < RT_ELEMENTS(s_aDataBitsConvTbl)
                && s_aDataBitsConvTbl[LineCtrl.bDataBits] != RTSERIALPORTDATABITS_INVALID)
                pCfg->enmDataBitCount = s_aDataBitsConvTbl[LineCtrl.bDataBits];
            else
                rc = VERR_IPE_UNEXPECTED_STATUS;

            if (   RT_SUCCESS(rc)
                && LineCtrl.bStopBits < RT_ELEMENTS(s_aStopBitsConvTbl))
                pCfg->enmStopBitCount = s_aStopBitsConvTbl[LineCtrl.bStopBits];
            else
                rc = VERR_IPE_UNEXPECTED_STATUS;
        }
        else
            rc = RTErrConvertFromOS2(rcOs2);
    }
    else
        rc = RTErrConvertFromOS2(rcOs2);

    return rc;
}


RTDECL(int) RTSerialPortCfgSet(RTSERIALPORT hSerialPort, PCRTSERIALPORTCFG pCfg, PRTERRINFO pErrInfo)
{
    PRTSERIALPORTINTERNAL pThis = hSerialPort;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSERIALPORT_MAGIC, VERR_INVALID_HANDLE);

    int rc = VINF_SUCCESS;
    OS2EXTSETBAUDRATEDATA ExtBaudRate;
    OS2SETLINECTRLDATA LineCtrl;
    ULONG cbExtBaudRate = sizeof(ExtBaudRate);
    ULONG cbLineCtrl = sizeof(LineCtrl);

    ExtBaudRate.uBitRate     = pCfg->uBaudRate;
    ExtBaudRate.bBitRateFrac = 0;

    BYTE idx = 0;
    while (idx < RT_ELEMENTS(s_aParityConvTbl))
    {
        if (s_aParityConvTbl[idx] == pCfg->enmParity)
        {
            LineCtrl.bParity = idx;
            break;
        }
        idx++;
    }
    AssertReturn(idx < RT_ELEMENTS(s_aParityConvTbl), VERR_INTERNAL_ERROR);

    idx = 0;
    while (idx < RT_ELEMENTS(s_aDataBitsConvTbl))
    {
        if (s_aDataBitsConvTbl[idx] == pCfg->enmDataBitCount)
        {
            LineCtrl.bDataBits = idx;
            break;
        }
        idx++;
    }
    AssertReturn(idx < RT_ELEMENTS(s_aDataBitsConvTbl), VERR_INTERNAL_ERROR);

    idx = 0;
    while (idx < RT_ELEMENTS(s_aStopBitsConvTbl))
    {
        if (s_aStopBitsConvTbl[idx] == pCfg->enmStopBitCount)
        {
            LineCtrl.bStopBits = idx;
            break;
        }
        idx++;
    }
    AssertReturn(idx < RT_ELEMENTS(s_aStopBitsConvTbl), VERR_INTERNAL_ERROR);

    ULONG rcOs2 = DosDevIOCtl(pThis->hDev, IOCTL_ASYNC, ASYNC_EXTSETBAUDRATE, &ExtBaudRate, cbExtBaudRate, &cbExtBaudRate, NULL, 0, NULL);
    if (!rcOs2)
    {
        rcOs2 = DosDevIOCtl(pThis->hDev, IOCTL_ASYNC, ASYNC_SETLINECTRL, &LineCtrl, cbLineCtrl, &cbLineCtrl, NULL, 0, NULL);
        if (rcOs2)
            rc = RTErrConvertFromOS2(rcOs2);
    }
    else
        rc = RTErrConvertFromOS2(rcOs2);

    return rc;
}


RTDECL(int) RTSerialPortEvtPoll(RTSERIALPORT hSerialPort, uint32_t fEvtMask, uint32_t *pfEvtsRecv,
                                RTMSINTERVAL msTimeout)
{
    PRTSERIALPORTINTERNAL pThis = hSerialPort;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSERIALPORT_MAGIC, VERR_INVALID_HANDLE);
    AssertReturn(!(fEvtMask & ~RTSERIALPORT_EVT_F_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertPtrReturn(pfEvtsRecv, VERR_INVALID_POINTER);

    *pfEvtsRecv = 0;

    /*
     * We need to kind of busy wait here as there is, to my knowledge, no API
     * to wait for a COM event.
     *
     * @todo Adaptive waiting
     * @todo Handle rollover after 48days eventually
     */
    int rc = VINF_SUCCESS;
    uint64_t tsStart = RTTimeSystemMilliTS();
    do
    {
        if (ASMAtomicXchgBool(&pThis->fInterrupt, false))
        {
            rc = VERR_INTERRUPTED;
            break;
        }

        USHORT fCommEvt = 0;
        ULONG cbCommEvt = sizeof(fCommEvt);
        ULONG rcOs2 = DosDevIOCtl(pThis->hDev, IOCTL_ASYNC, ASYNC_GETCOMMEVENT, NULL, 0, NULL,
                                  &fCommEvt, cbCommEvt, &cbCommEvt);
        if (!rcOs2)
        {
            AssertReturn(cbCommEvt = sizeof(fCommEvt), VERR_IPE_UNEXPECTED_STATUS);

            if (   (fEvtMask & RTSERIALPORT_EVT_F_DATA_RX)
                && (fCommEvt & OS2_GET_COMM_EVT_RX))
                *pfEvtsRecv |= RTSERIALPORT_EVT_F_DATA_RX;

            /** @todo Is there something better to indicate that there is room in the queue instead of queue is empty? */
            if (   (fEvtMask & RTSERIALPORT_EVT_F_DATA_TX)
                && (fCommEvt & OS2_GET_COMM_EVT_TX_EMPTY))
                *pfEvtsRecv |= RTSERIALPORT_EVT_F_DATA_TX;

            if (   (fEvtMask & RTSERIALPORT_EVT_F_STATUS_LINE_CHANGED)
                && (fCommEvt & (OS2_GET_COMM_EVT_CTS_CHG | OS2_GET_COMM_EVT_DSR_CHG | OS2_GET_COMM_EVT_DCD_CHG)))
                *pfEvtsRecv |= RTSERIALPORT_EVT_F_STATUS_LINE_CHANGED;

            if (   (fEvtMask & RTSERIALPORT_EVT_F_BREAK_DETECTED)
                && (fCommEvt & OS2_GET_COMM_EVT_BRK))
                *pfEvtsRecv |= RTSERIALPORT_EVT_F_BREAK_DETECTED;

            if (*pfEvtsRecv != 0)
                break;
        }
        else
        {
            rc = RTErrConvertFromOS2(rcOs2);
            break;
        }

        uint64_t tsNow = RTTimeSystemMilliTS();
        if (   msTimeout == RT_INDEFINITE_WAIT
            || tsNow - tsStart < msTimeout)
            DosSleep(1);
        else
            rc = VERR_TIMEOUT;
    } while (RT_SUCCESS(rc));

    return rc;
}


RTDECL(int) RTSerialPortEvtPollInterrupt(RTSERIALPORT hSerialPort)
{
    PRTSERIALPORTINTERNAL pThis = hSerialPort;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSERIALPORT_MAGIC, VERR_INVALID_HANDLE);

    ASMAtomicXchgBool(&pThis->fInterrupt, true);
    return VINF_SUCCESS;
}


RTDECL(int) RTSerialPortChgBreakCondition(RTSERIALPORT hSerialPort, bool fSet)
{
    PRTSERIALPORTINTERNAL pThis = hSerialPort;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSERIALPORT_MAGIC, VERR_INVALID_HANDLE);

    ULONG rcOs2 = DosDevIOCtl(pThis->hDev, IOCTL_ASYNC, fSet ? ASYNC_SETBREAKON : ASYNC_SETBREAKOFF,
                              NULL, 0, NULL, NULL, 0, NULL);

    return RTErrConvertFromOS2(rcOs2);
}


RTDECL(int) RTSerialPortChgStatusLines(RTSERIALPORT hSerialPort, uint32_t fClear, uint32_t fSet)
{
    PRTSERIALPORTINTERNAL pThis = hSerialPort;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSERIALPORT_MAGIC, VERR_INVALID_HANDLE);

    MODEMSTATUS MdmSts;
    ULONG cbMdmSts = sizeof(MdmSts);

    MdmSts.fbModemOn =    (fSet & RTSERIALPORT_CHG_STS_LINES_F_RTS ? 0x02 : 0x00)
                        | (fSet & RTSERIALPORT_CHG_STS_LINES_F_DTR ? 0x01 : 0x00);
    MdmSts.fbModemOff = 0xff;
    MdmSts.fbModemOff &= ~(  (fClear & RTSERIALPORT_CHG_STS_LINES_F_RTS ? 0x02 : 0x00)
                           | (fClear & RTSERIALPORT_CHG_STS_LINES_F_DTR ? 0x01 : 0x00));

    ULONG rcOs2 = DosDevIOCtl(pThis->hDev, IOCTL_ASYNC, ASYNC_SETMODEMCTRL, &MdmSts, cbMdmSts, &cbMdmSts, NULL, 0, NULL);

    return RTErrConvertFromOS2(rcOs2);
}


RTDECL(int) RTSerialPortQueryStatusLines(RTSERIALPORT hSerialPort, uint32_t *pfStsLines)
{
    PRTSERIALPORTINTERNAL pThis = hSerialPort;
    AssertPtrReturn(pThis, VERR_INVALID_PARAMETER);
    AssertReturn(pThis->u32Magic == RTSERIALPORT_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrReturn(pfStsLines, VERR_INVALID_POINTER);

    *pfStsLines = 0;

    int rc = VINF_SUCCESS;
    BYTE fStsLines = 0;
    ULONG cbStsLines = sizeof(fStsLines);
    ULONG rcOs2 = DosDevIOCtl(pThis->hDev, IOCTL_ASYNC, ASYNC_GETMODEMINPUT, NULL, 0, NULL, &fStsLines, cbStsLines, &cbStsLines);
    if (!rcOs2)
    {
        AssertReturn(cbStsLines == sizeof(BYTE), VERR_IPE_UNEXPECTED_STATUS);

        *pfStsLines |= (fStsLines & OS2_GET_MODEM_INPUT_DCD) ? RTSERIALPORT_STS_LINE_DCD : 0;
        *pfStsLines |= (fStsLines & OS2_GET_MODEM_INPUT_RI)  ? RTSERIALPORT_STS_LINE_RI  : 0;
        *pfStsLines |= (fStsLines & OS2_GET_MODEM_INPUT_DSR) ? RTSERIALPORT_STS_LINE_DSR : 0;
        *pfStsLines |= (fStsLines & OS2_GET_MODEM_INPUT_CTS) ? RTSERIALPORT_STS_LINE_CTS : 0;
    }
    else
        rc = RTErrConvertFromOS2(rcOs2);

    return rc;
}

