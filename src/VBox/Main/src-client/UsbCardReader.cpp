/* $Id: UsbCardReader.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * UsbCardReader - Driver Interface to USB Smart Card Reader emulation.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_USB_CARDREADER
#include "LoggingNew.h"

#include "UsbCardReader.h"
#include "ConsoleImpl.h"
#include "ConsoleVRDPServer.h"

#include <VBox/vmm/pdmdrv.h>
#include <VBox/vmm/pdmcardreaderinfs.h>
#include <VBox/err.h>

#include <iprt/req.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct USBCARDREADER USBCARDREADER;
typedef struct USBCARDREADER *PUSBCARDREADER;

struct USBCARDREADER
{
    UsbCardReader *pUsbCardReader;

    PPDMDRVINS pDrvIns;

    PDMICARDREADERDOWN ICardReaderDown;
    PPDMICARDREADERUP  pICardReaderUp;

    /* Thread handling Cmd to card reader */
    PPDMTHREAD          pThrCardReaderCmd;
    /* Queue handling requests to cardreader */
    RTREQQUEUE          hReqQCardReaderCmd;
};


/*
 * Command queue's callbacks.
 */

static DECLCALLBACK(void) drvCardReaderCmdStatusChange(PUSBCARDREADER pThis,
                                                       void *pvUser,
                                                       uint32_t u32Timeout,
                                                       PDMICARDREADER_READERSTATE *paReaderStats,
                                                       uint32_t cReaderStats)
{
    LogFlowFunc(("ENTER: pvUser:%p, u32Timeout:%d\n", pvUser, u32Timeout));

    UsbCardReader *pUsbCardReader = pThis->pUsbCardReader;
    if (!pUsbCardReader)
        pThis->pICardReaderUp->pfnSetStatusChange(pThis->pICardReaderUp,
                                                  pvUser, VRDE_SCARD_E_NO_SMARTCARD,
                                                  paReaderStats, cReaderStats);
    else
        pUsbCardReader->GetStatusChange(pThis, pvUser, u32Timeout, paReaderStats, cReaderStats);

    LogFlowFuncLeave();
}


static DECLCALLBACK(void) drvCardReaderCmdEstablishContext(PUSBCARDREADER pThis)
{
    LogFlowFunc(("\n"));

    UsbCardReader *pUsbCardReader = pThis->pUsbCardReader;
    if (!pUsbCardReader)
        pThis->pICardReaderUp->pfnEstablishContext(pThis->pICardReaderUp, VRDE_SCARD_E_NO_SMARTCARD);
    else
        pUsbCardReader->EstablishContext(pThis);

    LogFlowFuncLeave();
}

static DECLCALLBACK(void) drvCardReaderCmdReleaseContext(PUSBCARDREADER pThis, void *pvUser)
{
    LogFlowFunc(("ENTER: pvUser:%p\n", pvUser));
    NOREF(pvUser);

    UsbCardReader *pUsbCardReader = pThis->pUsbCardReader;
    if (!pUsbCardReader)
    {
        /* Do nothing. */
    }
    else
        pUsbCardReader->ReleaseContext(pThis);

    LogFlowFuncLeave();
}

static DECLCALLBACK(void) drvCardReaderCmdStatus(PUSBCARDREADER pThis, void *pvUser)
{
    LogFlowFunc(("ENTER: pvUser:%p\n", pvUser));

    UsbCardReader *pUsbCardReader = pThis->pUsbCardReader;
    if (!pUsbCardReader)
    {
        pThis->pICardReaderUp->pfnStatus(pThis->pICardReaderUp,
                                         pvUser,
                                         VRDE_SCARD_E_NO_SMARTCARD,
                                         /* pszReaderName */ NULL,
                                         /* cchReaderName */ 0,
                                         /* u32CardState */ 0,
                                         /* u32Protocol */ 0,
                                         /* pu8Atr */ 0,
                                         /* cbAtr */ 0);
    }
    else
        pUsbCardReader->Status(pThis, pvUser);

    LogFlowFuncLeave();
}

static DECLCALLBACK(void) drvCardReaderCmdConnect(PUSBCARDREADER pThis,
                                                  void *pvUser,
                                                  const char *pcszCardReaderName,
                                                  uint32_t u32ShareMode,
                                                  uint32_t u32PreferredProtocols)
{
    LogFlowFunc(("ENTER: pvUser:%p, pcszCardReaderName:%s, u32ShareMode:%RX32, u32PreferredProtocols:%RX32\n",
                 pvUser, pcszCardReaderName, u32ShareMode, u32PreferredProtocols));

    UsbCardReader *pUsbCardReader = pThis->pUsbCardReader;
    if (!pUsbCardReader)
        pThis->pICardReaderUp->pfnConnect(pThis->pICardReaderUp,
                                          pvUser,
                                          VRDE_SCARD_E_NO_SMARTCARD,
                                          0);
    else
        pUsbCardReader->Connect(pThis, pvUser, pcszCardReaderName, u32ShareMode, u32PreferredProtocols);

    LogFlowFuncLeave();
}

static DECLCALLBACK(void) drvCardReaderCmdDisconnect(PUSBCARDREADER pThis,
                                                     void *pvUser,
                                                     uint32_t u32Disposition)
{
    LogFlowFunc(("ENTER: pvUser:%p, u32Disposition:%RX32\n", pvUser, u32Disposition));

    UsbCardReader *pUsbCardReader = pThis->pUsbCardReader;
    if (!pUsbCardReader)
        pThis->pICardReaderUp->pfnDisconnect(pThis->pICardReaderUp, pvUser, VRDE_SCARD_E_NO_SMARTCARD);
    else
        pUsbCardReader->Disconnect(pThis, pvUser, u32Disposition);

    LogFlowFuncLeave();
}

static DECLCALLBACK(void) drvCardReaderCmdTransmit(PUSBCARDREADER pThis,
                                                   void *pvUser,
                                                   PDMICARDREADER_IO_REQUEST *pIoSendRequest,
                                                   uint8_t *pbSendBuffer,
                                                   uint32_t cbSendBuffer,
                                                   uint32_t cbRecvBuffer)
{
    LogFlowFunc(("ENTER: pvUser:%p, pIoSendRequest:%p, pbSendBuffer:%p, cbSendBuffer:%d, cbRecvBuffer:%d\n",
                 pvUser, pIoSendRequest, pbSendBuffer, cbSendBuffer, cbRecvBuffer));

    UsbCardReader *pUsbCardReader = pThis->pUsbCardReader;
    if (!pUsbCardReader)
        pThis->pICardReaderUp->pfnTransmit(pThis->pICardReaderUp,
                                           pvUser,
                                           VRDE_SCARD_E_NO_SMARTCARD,
                                           /* pioRecvPci */ NULL,
                                           /* pu8RecvBuffer */ NULL,
                                           /* cbRecvBuffer*/ 0);
    else
        pUsbCardReader->Transmit(pThis, pvUser, pIoSendRequest, pbSendBuffer, cbSendBuffer, cbRecvBuffer);

    /* Clean up buffers allocated by driver */
    RTMemFree(pIoSendRequest);
    RTMemFree(pbSendBuffer);

    LogFlowFuncLeave();
}

static DECLCALLBACK(void) drvCardReaderCmdGetAttr(PUSBCARDREADER pThis,
                                                  void *pvUser,
                                                  uint32_t u32AttrId,
                                                  uint32_t cbAttrib)
{
    LogFlowFunc(("ENTER: pvUser:%p, u32AttrId:%RX32, cbAttrib:%d\n",
                pvUser, u32AttrId, cbAttrib));

    UsbCardReader *pUsbCardReader = pThis->pUsbCardReader;
    if (!pUsbCardReader)
        pThis->pICardReaderUp->pfnGetAttrib(pThis->pICardReaderUp,
                                            pvUser,
                                            VRDE_SCARD_E_NO_SMARTCARD,
                                            u32AttrId,
                                            /* pvAttrib */ NULL,
                                            /* cbAttrib */ 0);
    else
        pUsbCardReader->GetAttrib(pThis, pvUser, u32AttrId, cbAttrib);

    LogFlowFuncLeave();
}

static DECLCALLBACK(void) drvCardReaderCmdSetAttr(PUSBCARDREADER pThis,
                                                  void *pvUser,
                                                  uint32_t u32AttrId,
                                                  void *pvAttrib,
                                                  uint32_t cbAttrib)
{
    LogFlowFunc(("ENTER: pvUser:%p, u32AttrId:%RX32, pvAttrib:%p, cbAttrib:%d\n",
                 pvUser, u32AttrId, pvAttrib, cbAttrib));

    UsbCardReader *pUsbCardReader = pThis->pUsbCardReader;
    if (!pUsbCardReader)
        pThis->pICardReaderUp->pfnSetAttrib(pThis->pICardReaderUp, pvUser, VRDE_SCARD_E_NO_SMARTCARD, u32AttrId);
    else
        pUsbCardReader->SetAttrib(pThis, pvUser, u32AttrId, (uint8_t *)pvAttrib, cbAttrib);

    /* Clean up buffers allocated by driver */
    RTMemFree(pvAttrib);

    LogFlowFuncLeave();
}

static DECLCALLBACK(void) drvCardReaderCmdControl(PUSBCARDREADER pThis,
                                                  void *pvUser,
                                                  uint32_t u32ControlCode,
                                                  void *pvInBuffer,
                                                  uint32_t cbInBuffer,
                                                  uint32_t cbOutBuffer)
{
    LogFlowFunc(("ENTER: pvUser:%p, u32ControlCode:%RX32, pvInBuffer:%p, cbInBuffer:%d, cbOutBuffer:%d\n",
                 pvUser, u32ControlCode, pvInBuffer, cbInBuffer, cbOutBuffer));

    UsbCardReader *pUsbCardReader = pThis->pUsbCardReader;
    if (!pUsbCardReader)
        pThis->pICardReaderUp->pfnControl(pThis->pICardReaderUp,
                                          pvUser,
                                          VRDE_SCARD_E_NO_SMARTCARD,
                                          u32ControlCode,
                                          /* pvOutBuffer */ NULL,
                                          /* cbOutBuffer */ 0);
    else
        pUsbCardReader->Control(pThis, pvUser, u32ControlCode, (uint8_t *)pvInBuffer, cbInBuffer, cbOutBuffer);

    /* Clean up buffers allocated by driver */
    RTMemFree(pvInBuffer);

    LogFlowFuncLeave();
}


/*
 * PDMICARDREADERDOWN - interface
 */

/** @interface_method_impl{PDMICARDREADERDOWN,pfnConnect} */
static DECLCALLBACK(int) drvCardReaderDownConnect(PPDMICARDREADERDOWN pInterface,
                                                  void *pvUser,
                                                  const char *pcszCardReaderName,
                                                  uint32_t u32ShareMode,
                                                  uint32_t u32PreferredProtocols)
{
    AssertPtrReturn(pInterface, VERR_INVALID_PARAMETER);
    LogFlowFunc(("ENTER: pcszCardReaderName:%s, pvUser:%p, u32ShareMode:%RX32, u32PreferredProtocols:%RX32\n",
                 pcszCardReaderName, pvUser, u32ShareMode, u32PreferredProtocols));
    PUSBCARDREADER pThis = RT_FROM_MEMBER(pInterface, USBCARDREADER, ICardReaderDown);
    int vrc = RTReqQueueCallEx(pThis->hReqQCardReaderCmd, NULL, 0, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                               (PFNRT)drvCardReaderCmdConnect, 5,
                               pThis, pvUser, pcszCardReaderName, u32ShareMode, u32PreferredProtocols);
    AssertRC(vrc);
    LogFlowFunc(("LEAVE: %Rrc\n", vrc));
    return vrc;
}

/** @interface_method_impl{PDMICARDREADERDOWN,pfnDisconnect} */
static DECLCALLBACK(int) drvCardReaderDownDisconnect(PPDMICARDREADERDOWN pInterface,
                                                     void *pvUser,
                                                     uint32_t u32Disposition)
{
    AssertPtrReturn(pInterface, VERR_INVALID_PARAMETER);
    LogFlowFunc(("ENTER: pvUser:%p, u32Disposition:%RX32\n",
                 pvUser, u32Disposition));
    PUSBCARDREADER pThis = RT_FROM_MEMBER(pInterface, USBCARDREADER, ICardReaderDown);
    int vrc = RTReqQueueCallEx(pThis->hReqQCardReaderCmd, NULL, 0, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                               (PFNRT)drvCardReaderCmdDisconnect, 3, pThis, pvUser, u32Disposition);
    AssertRC(vrc);
    LogFlowFunc(("LEAVE: %Rrc\n", vrc));
    return vrc;
}

/** @interface_method_impl{PDMICARDREADERDOWN,pfnEstablishContext} */
static DECLCALLBACK(int) drvCardReaderDownEstablishContext(PPDMICARDREADERDOWN pInterface)
{
    AssertPtrReturn(pInterface, VERR_INVALID_PARAMETER);
    LogFlowFunc(("ENTER:\n"));
    PUSBCARDREADER pThis = RT_FROM_MEMBER(pInterface, USBCARDREADER, ICardReaderDown);
    int vrc = RTReqQueueCallEx(pThis->hReqQCardReaderCmd, NULL, 0, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                               (PFNRT)drvCardReaderCmdEstablishContext, 1, pThis);
    AssertRC(vrc);
    LogFlowFunc(("LEAVE: %Rrc\n", vrc));
    return vrc;
}

/** @interface_method_impl{PDMICARDREADERDOWN,pfnReleaseContext} */
static DECLCALLBACK(int) drvCardReaderDownReleaseContext(PPDMICARDREADERDOWN pInterface,
                                                         void *pvUser)
{
    AssertPtrReturn(pInterface, VERR_INVALID_PARAMETER);
    LogFlowFunc(("ENTER: pvUser:%p\n", pvUser));
    PUSBCARDREADER pThis = RT_FROM_MEMBER(pInterface, USBCARDREADER, ICardReaderDown);

    /** @todo Device calls this when the driver already destroyed. */
    if (pThis->hReqQCardReaderCmd == NIL_RTREQQUEUE)
    {
        LogFlowFunc(("LEAVE: device already deleted.\n"));
        return VINF_SUCCESS;
    }

    int vrc = RTReqQueueCallEx(pThis->hReqQCardReaderCmd, NULL, 0, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                               (PFNRT)drvCardReaderCmdReleaseContext, 2, pThis, pvUser);
    AssertRC(vrc);
    LogFlowFunc(("LEAVE: %Rrc\n", vrc));
    return vrc;
}

/** @interface_method_impl{PDMICARDREADERDOWN,pfnStatus} */
static DECLCALLBACK(int) drvCardReaderDownStatus(PPDMICARDREADERDOWN pInterface,
                                                 void *pvUser,
                                                 uint32_t cchReaderName,
                                                 uint32_t cbAtrLen)
{
    AssertPtrReturn(pInterface, VERR_INVALID_PARAMETER);
    LogFlowFunc(("ENTER: pvUser:%p, cchReaderName:%d, cbAtrLen:%d\n",
                 pvUser, cchReaderName, cbAtrLen));
    NOREF(cchReaderName);
    NOREF(cbAtrLen);
    PUSBCARDREADER pThis = RT_FROM_MEMBER(pInterface, USBCARDREADER, ICardReaderDown);
    int vrc = RTReqQueueCallEx(pThis->hReqQCardReaderCmd, NULL, 0, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                              (PFNRT)drvCardReaderCmdStatus, 2, pThis, pvUser);
    AssertRC(vrc);
    LogFlowFunc(("LEAVE: %Rrc\n", vrc));
    return vrc;
}

/** @interface_method_impl{PDMICARDREADERDOWN,pfnGetStatusChange} */
static DECLCALLBACK(int) drvCardReaderDownGetStatusChange(PPDMICARDREADERDOWN pInterface,
                                                          void *pvUser,
                                                          uint32_t u32Timeout,
                                                          PDMICARDREADER_READERSTATE *paReaderStats,
                                                          uint32_t cReaderStats)
{
    AssertPtrReturn(pInterface, VERR_INVALID_PARAMETER);
    LogFlowFunc(("ENTER: pvUser:%p, u32Timeout:%d, cReaderStats:%d\n",
                 pvUser, u32Timeout, cReaderStats));
    PUSBCARDREADER pThis = RT_FROM_MEMBER(pInterface, USBCARDREADER, ICardReaderDown);
    int vrc = RTReqQueueCallEx(pThis->hReqQCardReaderCmd, NULL, 0, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                               (PFNRT)drvCardReaderCmdStatusChange, 5, pThis, pvUser, u32Timeout, paReaderStats, cReaderStats);
    AssertRC(vrc);
    LogFlowFunc(("LEAVE: %Rrc\n", vrc));
    return vrc;
}

/** @interface_method_impl{PDMICARDREADERDOWN,pfnBeginTransaction} */
static DECLCALLBACK(int) drvCardReaderDownBeginTransaction(PPDMICARDREADERDOWN pInterface,
                                                           void *pvUser)
{
    RT_NOREF(pvUser);
    AssertPtrReturn(pInterface, VERR_INVALID_PARAMETER);
    LogFlowFunc(("ENTER: pvUser:%p\n",
                 pvUser));
    PUSBCARDREADER pThis = RT_FROM_MEMBER(pInterface, USBCARDREADER, ICardReaderDown); NOREF(pThis);
    int vrc = VERR_NOT_SUPPORTED;
    AssertRC(vrc);
    LogFlowFunc(("LEAVE: %Rrc\n", vrc));
    return vrc;
}

/** @interface_method_impl{PDMICARDREADERDOWN,pfnEndTransaction} */
static DECLCALLBACK(int) drvCardReaderDownEndTransaction(PPDMICARDREADERDOWN pInterface,
                                                         void *pvUser,
                                                         uint32_t u32Disposition)
{
    RT_NOREF(pvUser, u32Disposition);
    AssertPtrReturn(pInterface, VERR_INVALID_PARAMETER);
    LogFlowFunc(("ENTER: pvUser:%p, u32Disposition:%RX32\n",
                 pvUser, u32Disposition));
    PUSBCARDREADER pThis = RT_FROM_MEMBER(pInterface, USBCARDREADER, ICardReaderDown); NOREF(pThis);
    int vrc = VERR_NOT_SUPPORTED;
    AssertRC(vrc);
    LogFlowFunc(("LEAVE: %Rrc\n", vrc));
    return vrc;
}

/** @interface_method_impl{PDMICARDREADERDOWN,pfnTransmit} */
static DECLCALLBACK(int) drvCardReaderDownTransmit(PPDMICARDREADERDOWN pInterface,
                                                   void *pvUser,
                                                   const PDMICARDREADER_IO_REQUEST *pIoSendRequest,
                                                   const uint8_t *pbSendBuffer,
                                                   uint32_t cbSendBuffer,
                                                   uint32_t cbRecvBuffer)
{
    AssertPtrReturn(pInterface, VERR_INVALID_PARAMETER);
    LogFlowFunc(("ENTER: pvUser:%p, pIoSendRequest:%p, pbSendBuffer:%p, cbSendBuffer:%d, cbRecvBuffer:%d\n",
                 pvUser, pIoSendRequest, pbSendBuffer, cbSendBuffer, cbRecvBuffer));
    PUSBCARDREADER pThis = RT_FROM_MEMBER(pInterface, USBCARDREADER, ICardReaderDown);
    uint8_t *pbSendBufferCopy = NULL;
    if (   pbSendBuffer
        && cbSendBuffer)
    {
        pbSendBufferCopy = (uint8_t *)RTMemDup(pbSendBuffer, cbSendBuffer);
        if (!pbSendBufferCopy)
            return VERR_NO_MEMORY;
    }
    PDMICARDREADER_IO_REQUEST *pIoSendRequestCopy = NULL;
    if (pIoSendRequest)
    {
        pIoSendRequestCopy = (PDMICARDREADER_IO_REQUEST *)RTMemDup(pIoSendRequest, pIoSendRequest->cbPciLength);
        if (!pIoSendRequestCopy)
        {
            RTMemFree(pbSendBufferCopy);
            return VERR_NO_MEMORY;
        }
    }
    int vrc = RTReqQueueCallEx(pThis->hReqQCardReaderCmd, NULL, 0,RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                               (PFNRT)drvCardReaderCmdTransmit, 6,
                               pThis, pvUser, pIoSendRequestCopy, pbSendBufferCopy, cbSendBuffer, cbRecvBuffer);
    AssertRC(vrc);
    LogFlowFunc(("LEAVE: %Rrc\n", vrc));
    return vrc;
}

/** @interface_method_impl{PDMICARDREADERDOWN,pfnGetAttr} */
static DECLCALLBACK(int) drvCardReaderDownGetAttr(PPDMICARDREADERDOWN pInterface,
                                                  void *pvUser,
                                                  uint32_t u32AttribId,
                                                  uint32_t cbAttrib)
{
    AssertPtrReturn(pInterface, VERR_INVALID_PARAMETER);
    LogFlowFunc(("ENTER: pvUser:%p, u32AttribId:%RX32, cbAttrib:%d\n",
                 pvUser, u32AttribId, cbAttrib));
    PUSBCARDREADER pThis = RT_FROM_MEMBER(pInterface, USBCARDREADER, ICardReaderDown);
    int vrc = RTReqQueueCallEx(pThis->hReqQCardReaderCmd, NULL, 0, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                               (PFNRT)drvCardReaderCmdGetAttr, 4, pThis, pvUser, u32AttribId, cbAttrib);
    AssertRC(vrc);
    LogFlowFunc(("LEAVE: %Rrc\n", vrc));
    return vrc;
}

/** @interface_method_impl{PDMICARDREADERDOWN,pfnSetAttr} */
static DECLCALLBACK(int) drvCardReaderDownSetAttr(PPDMICARDREADERDOWN pInterface,
                                                  void *pvUser,
                                                  uint32_t u32AttribId,
                                                  const void *pvAttrib,
                                                  uint32_t cbAttrib)
{
    AssertPtrReturn(pInterface, VERR_INVALID_PARAMETER);
    LogFlowFunc(("ENTER: pvUser:%p, u32AttribId:%RX32, pvAttrib:%p, cbAttrib:%d\n",
                 pvUser, u32AttribId, pvAttrib, cbAttrib));
    PUSBCARDREADER pThis = RT_FROM_MEMBER(pInterface, USBCARDREADER, ICardReaderDown);
    void *pvAttribCopy = NULL;
    if (   pvAttrib
        && cbAttrib)
    {
        pvAttribCopy = RTMemDup(pvAttrib, cbAttrib);
        AssertPtrReturn(pvAttribCopy, VERR_NO_MEMORY);
    }
    int vrc = RTReqQueueCallEx(pThis->hReqQCardReaderCmd, NULL, 0, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                               (PFNRT)drvCardReaderCmdSetAttr, 5, pThis, pvUser, u32AttribId, pvAttribCopy, cbAttrib);
    AssertRC(vrc);
    LogFlowFunc(("LEAVE: %Rrc\n", vrc));
    return vrc;
}

/** @interface_method_impl{PDMICARDREADERDOWN,pfnControl} */
static DECLCALLBACK(int) drvCardReaderDownControl(PPDMICARDREADERDOWN pInterface,
                                                  void *pvUser,
                                                  uint32_t u32ControlCode,
                                                  const void *pvInBuffer,
                                                  uint32_t cbInBuffer,
                                                  uint32_t cbOutBuffer)
{
    AssertPtrReturn(pInterface, VERR_INVALID_PARAMETER);
    LogFlowFunc(("ENTER: pvUser:%p, u32ControlCode:%RX32 pvInBuffer:%p, cbInBuffer:%d, cbOutBuffer:%d\n",
                 pvUser, u32ControlCode, pvInBuffer, cbInBuffer, cbOutBuffer));
    PUSBCARDREADER pThis = RT_FROM_MEMBER(pInterface, USBCARDREADER, ICardReaderDown);
    void *pvInBufferCopy = NULL;
    if (   pvInBuffer
        && cbInBuffer)
    {
        pvInBufferCopy = RTMemDup(pvInBuffer, cbInBuffer);
        AssertReturn(pvInBufferCopy, VERR_NO_MEMORY);
    }
    int vrc = RTReqQueueCallEx(pThis->hReqQCardReaderCmd, NULL, 0, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                               (PFNRT)drvCardReaderCmdControl, 6,
                               pThis, pvUser, u32ControlCode, pvInBufferCopy, cbInBuffer, cbOutBuffer);
    AssertRC(vrc);
    LogFlowFunc(("LEAVE: %Rrc\n", vrc));
    return vrc;
}


/*
 * Cardreader driver thread routines
 */
static DECLCALLBACK(int) drvCardReaderThreadCmd(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    PUSBCARDREADER pThis = PDMINS_2_DATA(pDrvIns, PUSBCARDREADER);

    LogFlowFunc(("ENTER: pDrvIns:%d, state %d\n", pDrvIns->iInstance, pThread->enmState));

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
    {
        LogFlowFunc(("LEAVE: INITIALIZING: VINF_SUCCESS\n"));
        return VINF_SUCCESS;
    }

    int vrc = VINF_SUCCESS;
    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        vrc = RTReqQueueProcess(pThis->hReqQCardReaderCmd, RT_INDEFINITE_WAIT);

        AssertMsg(vrc == VWRN_STATE_CHANGED, ("Left RTReqProcess and error code is not VWRN_STATE_CHANGED vrc=%Rrc\n", vrc));
    }

    LogFlowFunc(("LEAVE: %Rrc\n", vrc));
    return vrc;
}

static DECLCALLBACK(int) drvCardReaderWakeupFunc(PUSBCARDREADER pThis)
{
    NOREF(pThis);
    /* Returning a VINF_* will cause RTReqQueueProcess return. */
    return VWRN_STATE_CHANGED;
}

static DECLCALLBACK(int) drvCardReaderThreadCmdWakeup(PPDMDRVINS pDrvIns, PPDMTHREAD pThread)
{
    RT_NOREF(pThread);
    LogFlowFunc(("ENTER: pDrvIns:%i\n", pDrvIns->iInstance));
    PUSBCARDREADER pThis = PDMINS_2_DATA(pDrvIns, PUSBCARDREADER);

    AssertReturn(pThis->hReqQCardReaderCmd != NIL_RTREQQUEUE, VERR_INVALID_STATE);

    PRTREQ pReq;
    int vrc = RTReqQueueCall(pThis->hReqQCardReaderCmd, &pReq, 10000, (PFNRT)drvCardReaderWakeupFunc, 1, pThis);
    AssertMsgRC(vrc, ("Inserting request into queue failed vrc=%Rrc\n", vrc));

    if (RT_SUCCESS(vrc))
        RTReqRelease(pReq);
    /** @todo handle VERR_TIMEOUT */

    return vrc;
}


/*
 * USB Card reader driver implementation.
 */

UsbCardReader::UsbCardReader(Console *console)
    :
    mpDrv(NULL),
    mParent(console),
    m_pRemote(NULL)
{
    LogFlowFunc(("\n"));
}

UsbCardReader::~UsbCardReader()
{
    LogFlowFunc(("mpDrv %p\n", mpDrv));
    if (mpDrv)
    {
        mpDrv->pUsbCardReader = NULL;
        mpDrv = NULL;
    }
}

typedef struct UCRREMOTEREADER
{
    bool fAvailable;
    char szReaderName[1024];

    bool fHandle;
    VRDESCARDHANDLE hCard;
} UCRREMOTEREADER;

struct UCRREMOTE
{
    UsbCardReader *pUsbCardReader;

    /* The remote identifiers. */
    uint32_t u32ClientId;
    uint32_t u32DeviceId;

    bool fContext;
    VRDESCARDCONTEXT context;

    /* Possible a few readers. Currently only one. */
    UCRREMOTEREADER reader;
};

typedef struct UCRREQCTX
{
    UCRREMOTE *pRemote;
    uint32_t u32Function;
    void *pvUser;
    union
    {
        struct
        {
            PDMICARDREADER_READERSTATE *paReaderStats;
            uint32_t cReaderStats;
        } GetStatusChange;
        struct
        {
            uint32_t u32AttrId;
        } GetAttrib;
        struct
        {
            uint32_t u32AttrId;
        } SetAttrib;
        struct
        {
            uint32_t u32ControlCode;
        } Control;
    } u;
} UCRREQCTX;

int UsbCardReader::vrdeSCardRequest(void *pvUser, uint32_t u32Function, const void *pvData, uint32_t cbData)
{
    int vrc = mParent->i_consoleVRDPServer()->SCardRequest(pvUser, u32Function, pvData, cbData);
    LogFlowFunc(("%d %Rrc\n", u32Function, vrc));
    return vrc;
}

int UsbCardReader::VRDENotify(uint32_t u32Id, void *pvData, uint32_t cbData)
{
    RT_NOREF(cbData);
    int vrc = VINF_SUCCESS;

    switch (u32Id)
    {
        case VRDE_SCARD_NOTIFY_ATTACH:
        {
            VRDESCARDNOTIFYATTACH *p = (VRDESCARDNOTIFYATTACH *)pvData;
            Assert(cbData == sizeof(VRDESCARDNOTIFYATTACH));

            LogFlowFunc(("[%d,%d]\n", p->u32ClientId, p->u32DeviceId));

            /* Add this remote instance, which allow access to card readers attached to the client, to the list. */
            /** @todo currently only one device is allowed. */
            if (m_pRemote)
            {
                AssertFailed();
                vrc = VERR_NOT_SUPPORTED;
                break;
            }
            UCRREMOTE *pRemote = (UCRREMOTE *)RTMemAllocZ(sizeof(UCRREMOTE));
            if (pRemote == NULL)
            {
                vrc = VERR_NO_MEMORY;
                break;
            }

            pRemote->pUsbCardReader = this;
            pRemote->u32ClientId = p->u32ClientId;
            pRemote->u32DeviceId = p->u32DeviceId;

            m_pRemote = pRemote;

            /* Try to establish a context. */
            VRDESCARDESTABLISHCONTEXTREQ req;
            req.u32ClientId = m_pRemote->u32ClientId;
            req.u32DeviceId = m_pRemote->u32DeviceId;

            vrc = vrdeSCardRequest(m_pRemote, VRDE_SCARD_FN_ESTABLISHCONTEXT, &req, sizeof(req));

            LogFlowFunc(("sent ESTABLISHCONTEXT\n"));
        } break;

        case VRDE_SCARD_NOTIFY_DETACH:
        {
            VRDESCARDNOTIFYDETACH *p = (VRDESCARDNOTIFYDETACH *)pvData; NOREF(p);
            Assert(cbData == sizeof(VRDESCARDNOTIFYDETACH));

            /** @todo Just free. There should be no pending requests, because VRDP cancels them. */
            RTMemFree(m_pRemote);
            m_pRemote = NULL;
        } break;

        default:
            vrc = VERR_INVALID_PARAMETER;
            AssertFailed();
            break;
    }

    return vrc;
}

int UsbCardReader::VRDEResponse(int rcRequest, void *pvUser, uint32_t u32Function, void *pvData, uint32_t cbData)
{
    RT_NOREF(cbData);
    int vrc = VINF_SUCCESS;

    LogFlowFunc(("%Rrc %p %u %p %u\n", rcRequest, pvUser, u32Function, pvData, cbData));

    switch (u32Function)
    {
        case VRDE_SCARD_FN_ESTABLISHCONTEXT:
        {
            Assert(cbData == sizeof(VRDESCARDESTABLISHCONTEXTRSP) || RT_FAILURE(rcRequest));
            VRDESCARDESTABLISHCONTEXTRSP *pRsp = (VRDESCARDESTABLISHCONTEXTRSP *)pvData;
            UCRREMOTE *pRemote = (UCRREMOTE *)pvUser;

            /* Check if the context was created. */
            Assert(!pRemote->fContext);
            if (   RT_SUCCESS(rcRequest)
                && pRsp->u32ReturnCode == VRDE_SCARD_S_SUCCESS)
            {
                pRemote->fContext = true;
                pRemote->context = pRsp->Context;

                LogFlowFunc(("ESTABLISHCONTEXT success\n"));

                /* Now list readers attached to the remote client. */
                VRDESCARDLISTREADERSREQ req;
                req.Context = pRemote->context;

                vrc = vrdeSCardRequest(pRemote, VRDE_SCARD_FN_LISTREADERS, &req, sizeof(req));
            }
        } break;

        case VRDE_SCARD_FN_LISTREADERS:
        {
            Assert(cbData == sizeof(VRDESCARDLISTREADERSRSP) || RT_FAILURE(rcRequest));
            VRDESCARDLISTREADERSRSP *pRsp = (VRDESCARDLISTREADERSRSP *)pvData;
            UCRREMOTE *pRemote = (UCRREMOTE *)pvUser;

            Assert(pRemote->fContext);
            if (   RT_SUCCESS(rcRequest)
                && pRsp->u32ReturnCode == VRDE_SCARD_S_SUCCESS
                && pRemote->fContext)
            {
                LogFlowFunc(("LISTREADERS: cReaders %d\n",
                             pRsp->cReaders));

                uint32_t i;
                for (i = 0; i < pRsp->cReaders; i++)
                {
                    LogFlowFunc(("LISTREADERS: [%d] [%s]\n",
                                 i, pRsp->apszNames[i]));

                    /** @todo only the first reader is supported. */
                    if (i != 0)
                    {
                        continue;
                    }

                    if (pRsp->apszNames[i])
                        RTStrCopy(pRemote->reader.szReaderName, sizeof(pRemote->reader.szReaderName), pRsp->apszNames[i]);
                    else
                        RT_ZERO(pRemote->reader.szReaderName);
                    pRemote->reader.fHandle = false;
                    pRemote->reader.fAvailable = true;
                }
            }
        } break;

        case VRDE_SCARD_FN_RELEASECONTEXT:
        {
            Assert(cbData == sizeof(VRDESCARDRELEASECONTEXTRSP) || RT_FAILURE(rcRequest));
            VRDESCARDRELEASECONTEXTRSP *pRsp = (VRDESCARDRELEASECONTEXTRSP *)pvData; NOREF(pRsp);
            UCRREQCTX *pCtx = (UCRREQCTX *)pvUser; NOREF(pCtx);

            Assert(pCtx->u32Function == u32Function);

            LogFlowFunc(("RELEASECONTEXT completed\n"));

            /* No notification is expected here by the caller. */
            Assert(!m_pRemote->fContext);
        } break;

        case VRDE_SCARD_FN_GETSTATUSCHANGE:
        {
            Assert(cbData == sizeof(VRDESCARDGETSTATUSCHANGERSP) || RT_FAILURE(rcRequest));
            VRDESCARDGETSTATUSCHANGERSP *pRsp = (VRDESCARDGETSTATUSCHANGERSP *)pvData;
            UCRREQCTX *pCtx = (UCRREQCTX *)pvUser;

            Assert(pCtx->u32Function == u32Function);

            LogFlowFunc(("GETSTATUSCHANGE\n"));

            uint32_t rcCard;
            if (RT_FAILURE(rcRequest))
            {
                rcCard = VRDE_SCARD_E_NO_SMARTCARD;
            }
            else
            {
                rcCard = pRsp->u32ReturnCode;

                if (pRsp->u32ReturnCode == VRDE_SCARD_S_SUCCESS)
                {
                    uint32_t i;
                    for (i = 0; i < pRsp->cReaders; i++)
                    {
                        LogFlowFunc(("GETSTATUSCHANGE: [%d] %RX32\n",
                                     i, pRsp->aReaderStates[i].u32EventState));

                        /** @todo only the first reader is supported. */
                        if (i != 0)
                        {
                            continue;
                        }

                        if (i >= pCtx->u.GetStatusChange.cReaderStats)
                        {
                            continue;
                        }

                        pCtx->u.GetStatusChange.paReaderStats[i].u32EventState = pRsp->aReaderStates[i].u32EventState;
                        pCtx->u.GetStatusChange.paReaderStats[i].cbAtr = pRsp->aReaderStates[i].u32AtrLength > 36?
                                                                             36:
                                                                             pRsp->aReaderStates[i].u32AtrLength;
                        memcpy(pCtx->u.GetStatusChange.paReaderStats[i].au8Atr,
                               pRsp->aReaderStates[i].au8Atr,
                               pCtx->u.GetStatusChange.paReaderStats[i].cbAtr);
                    }
                }
            }

            mpDrv->pICardReaderUp->pfnSetStatusChange(mpDrv->pICardReaderUp,
                                                      pCtx->pvUser,
                                                      rcCard,
                                                      pCtx->u.GetStatusChange.paReaderStats,
                                                      pCtx->u.GetStatusChange.cReaderStats);

            RTMemFree(pCtx);
        } break;

        case VRDE_SCARD_FN_CANCEL:
        {
            Assert(cbData == sizeof(VRDESCARDCANCELRSP) || RT_FAILURE(rcRequest));
            VRDESCARDCANCELRSP *pRsp = (VRDESCARDCANCELRSP *)pvData; NOREF(pRsp);
            UCRREQCTX *pCtx = (UCRREQCTX *)pvUser; NOREF(pCtx);

            Assert(pCtx->u32Function == u32Function);

            LogFlowFunc(("CANCEL\n"));
        } break;

        case VRDE_SCARD_FN_CONNECT:
        {
            Assert(cbData == sizeof(VRDESCARDCONNECTRSP) || RT_FAILURE(rcRequest));
            VRDESCARDCONNECTRSP *pRsp = (VRDESCARDCONNECTRSP *)pvData;
            UCRREQCTX *pCtx = (UCRREQCTX *)pvUser;

            Assert(pCtx->u32Function == u32Function);

            LogFlowFunc(("CONNECT\n"));

            uint32_t u32ActiveProtocol = 0;
            uint32_t rcCard;

            if (RT_FAILURE(rcRequest))
            {
                rcCard = VRDE_SCARD_E_NO_SMARTCARD;
            }
            else
            {
                rcCard = pRsp->u32ReturnCode;

                if (pRsp->u32ReturnCode == VRDE_SCARD_S_SUCCESS)
                {
                    u32ActiveProtocol = pRsp->u32ActiveProtocol;

                    Assert(!m_pRemote->reader.fHandle);
                    m_pRemote->reader.hCard = pRsp->hCard;
                    m_pRemote->reader.fHandle = true;
                }
            }

            mpDrv->pICardReaderUp->pfnConnect(mpDrv->pICardReaderUp,
                                              pCtx->pvUser,
                                              rcCard,
                                              u32ActiveProtocol);

            RTMemFree(pCtx);
        } break;

        case VRDE_SCARD_FN_RECONNECT:
        {
            Assert(cbData == sizeof(VRDESCARDRECONNECTRSP) || RT_FAILURE(rcRequest));
            VRDESCARDRECONNECTRSP *pRsp = (VRDESCARDRECONNECTRSP *)pvData; NOREF(pRsp);
            UCRREQCTX *pCtx = (UCRREQCTX *)pvUser; NOREF(pCtx);

            Assert(pCtx->u32Function == u32Function);

            LogFlowFunc(("RECONNECT\n"));
        } break;

        case VRDE_SCARD_FN_DISCONNECT:
        {
            Assert(cbData == sizeof(VRDESCARDDISCONNECTRSP) || RT_FAILURE(rcRequest));
            VRDESCARDDISCONNECTRSP *pRsp = (VRDESCARDDISCONNECTRSP *)pvData;
            UCRREQCTX *pCtx = (UCRREQCTX *)pvUser;

            Assert(pCtx->u32Function == u32Function);

            LogFlowFunc(("DISCONNECT\n"));

            Assert(!pCtx->pRemote->reader.fHandle);

            uint32_t rcCard;

            if (RT_FAILURE(rcRequest))
            {
                rcCard = VRDE_SCARD_E_NO_SMARTCARD;
            }
            else
            {
                rcCard = pRsp->u32ReturnCode;
            }

            mpDrv->pICardReaderUp->pfnDisconnect(mpDrv->pICardReaderUp,
                                                 pCtx->pvUser,
                                                 rcCard);

            RTMemFree(pCtx);
        } break;

        case VRDE_SCARD_FN_BEGINTRANSACTION:
        {
            Assert(cbData == sizeof(VRDESCARDBEGINTRANSACTIONRSP) || RT_FAILURE(rcRequest));
            VRDESCARDBEGINTRANSACTIONRSP *pRsp = (VRDESCARDBEGINTRANSACTIONRSP *)pvData; NOREF(pRsp);
            UCRREQCTX *pCtx = (UCRREQCTX *)pvUser; NOREF(pCtx);

            Assert(pCtx->u32Function == u32Function);

            LogFlowFunc(("BEGINTRANSACTION\n"));
        } break;

        case VRDE_SCARD_FN_ENDTRANSACTION:
        {
            Assert(cbData == sizeof(VRDESCARDENDTRANSACTIONRSP) || RT_FAILURE(rcRequest));
            VRDESCARDENDTRANSACTIONRSP *pRsp = (VRDESCARDENDTRANSACTIONRSP *)pvData; NOREF(pRsp);
            UCRREQCTX *pCtx = (UCRREQCTX *)pvUser; NOREF(pCtx);

            Assert(pCtx->u32Function == u32Function);

            LogFlowFunc(("ENDTRANSACTION\n"));
        } break;

        case VRDE_SCARD_FN_STATE:
        {
            Assert(cbData == sizeof(VRDESCARDSTATERSP) || RT_FAILURE(rcRequest));
            VRDESCARDSTATERSP *pRsp = (VRDESCARDSTATERSP *)pvData; NOREF(pRsp);
            UCRREQCTX *pCtx = (UCRREQCTX *)pvUser; NOREF(pCtx);

            Assert(pCtx->u32Function == u32Function);

            LogFlowFunc(("STATE\n"));
        } break;

        case VRDE_SCARD_FN_STATUS:
        {
            Assert(cbData == sizeof(VRDESCARDSTATUSRSP) || RT_FAILURE(rcRequest));
            VRDESCARDSTATUSRSP *pRsp = (VRDESCARDSTATUSRSP *)pvData;
            UCRREQCTX *pCtx = (UCRREQCTX *)pvUser;

            Assert(pCtx->u32Function == u32Function);

            LogFlowFunc(("STATUS\n"));

            char *pszReaderName = NULL;
            uint32_t cchReaderName = 0;
            uint32_t u32CardState = 0;
            uint32_t u32Protocol = 0;
            uint32_t u32AtrLength = 0;
            uint8_t *pbAtr = NULL;

            uint32_t rcCard;

            if (RT_FAILURE(rcRequest))
            {
                rcCard = VRDE_SCARD_E_NO_SMARTCARD;
            }
            else
            {
                rcCard = pRsp->u32ReturnCode;

                if (pRsp->u32ReturnCode == VRDE_SCARD_S_SUCCESS)
                {
                    pszReaderName = pRsp->szReader;
                    cchReaderName = (uint32_t)strlen(pRsp->szReader) + 1;
                    u32CardState = pRsp->u32State;
                    u32Protocol = pRsp->u32Protocol;
                    u32AtrLength = pRsp->u32AtrLength;
                    pbAtr = &pRsp->au8Atr[0];
                }
            }

            mpDrv->pICardReaderUp->pfnStatus(mpDrv->pICardReaderUp,
                                             pCtx->pvUser,
                                             rcCard,
                                             pszReaderName,
                                             cchReaderName,
                                             u32CardState,
                                             u32Protocol,
                                             pbAtr,
                                             u32AtrLength);

            RTMemFree(pCtx);
        } break;

        case VRDE_SCARD_FN_TRANSMIT:
        {
            Assert(cbData == sizeof(VRDESCARDTRANSMITRSP) || RT_FAILURE(rcRequest));
            VRDESCARDTRANSMITRSP *pRsp = (VRDESCARDTRANSMITRSP *)pvData;
            UCRREQCTX *pCtx = (UCRREQCTX *)pvUser;

            Assert(pCtx->u32Function == u32Function);

            LogFlowFunc(("TRANSMIT\n"));

            PDMICARDREADER_IO_REQUEST *pioRecvPci = NULL;
            uint8_t *pu8RecvBuffer = NULL;
            uint32_t cbRecvBuffer = 0;

            uint32_t rcCard;

            if (RT_FAILURE(rcRequest))
            {
                rcCard = VRDE_SCARD_E_NO_SMARTCARD;
            }
            else
            {
                rcCard = pRsp->u32ReturnCode;

                if (pRsp->u32ReturnCode == VRDE_SCARD_S_SUCCESS)
                {
                    pu8RecvBuffer = pRsp->pu8RecvBuffer;
                    cbRecvBuffer = pRsp->u32RecvLength;
                    /** @todo pioRecvPci */
                }
            }

            mpDrv->pICardReaderUp->pfnTransmit(mpDrv->pICardReaderUp,
                                               pCtx->pvUser,
                                               rcCard,
                                               pioRecvPci,
                                               pu8RecvBuffer,
                                               cbRecvBuffer);

            RTMemFree(pioRecvPci);

            RTMemFree(pCtx);
        } break;

        case VRDE_SCARD_FN_CONTROL:
        {
            Assert(cbData == sizeof(VRDESCARDCONTROLRSP) || RT_FAILURE(rcRequest));
            VRDESCARDCONTROLRSP *pRsp = (VRDESCARDCONTROLRSP *)pvData;
            UCRREQCTX *pCtx = (UCRREQCTX *)pvUser;

            Assert(pCtx->u32Function == u32Function);

            LogFlowFunc(("CONTROL\n"));

            uint8_t *pu8OutBuffer = NULL;
            uint32_t cbOutBuffer = 0;

            uint32_t rcCard;

            if (RT_FAILURE(rcRequest))
            {
                rcCard = VRDE_SCARD_E_NO_SMARTCARD;
            }
            else
            {
                rcCard = pRsp->u32ReturnCode;

                if (pRsp->u32ReturnCode == VRDE_SCARD_S_SUCCESS)
                {
                    pu8OutBuffer = pRsp->pu8OutBuffer;
                    cbOutBuffer = pRsp->u32OutBufferSize;
                }
            }

            mpDrv->pICardReaderUp->pfnControl(mpDrv->pICardReaderUp,
                                              pCtx->pvUser,
                                              rcCard,
                                              pCtx->u.Control.u32ControlCode,
                                              pu8OutBuffer,
                                              cbOutBuffer);

            RTMemFree(pCtx);
        } break;

        case VRDE_SCARD_FN_GETATTRIB:
        {
            Assert(cbData == sizeof(VRDESCARDGETATTRIBRSP) || RT_FAILURE(rcRequest));
            VRDESCARDGETATTRIBRSP *pRsp = (VRDESCARDGETATTRIBRSP *)pvData;
            UCRREQCTX *pCtx = (UCRREQCTX *)pvUser;

            Assert(pCtx->u32Function == u32Function);

            LogFlowFunc(("GETATTRIB\n"));

            uint8_t *pu8Attrib = NULL;
            uint32_t cbAttrib = 0;

            uint32_t rcCard;

            if (RT_FAILURE(rcRequest))
            {
                rcCard = VRDE_SCARD_E_NO_SMARTCARD;
            }
            else
            {
                rcCard = pRsp->u32ReturnCode;

                if (pRsp->u32ReturnCode == VRDE_SCARD_S_SUCCESS)
                {
                    pu8Attrib = pRsp->pu8Attr;
                    cbAttrib = pRsp->u32AttrLength;
                }
            }

            mpDrv->pICardReaderUp->pfnGetAttrib(mpDrv->pICardReaderUp,
                                                pCtx->pvUser,
                                                rcCard,
                                                pCtx->u.GetAttrib.u32AttrId,
                                                pu8Attrib,
                                                cbAttrib);

            RTMemFree(pCtx);
        } break;

        case VRDE_SCARD_FN_SETATTRIB:
        {
            Assert(cbData == sizeof(VRDESCARDSETATTRIBRSP) || RT_FAILURE(rcRequest));
            VRDESCARDSETATTRIBRSP *pRsp = (VRDESCARDSETATTRIBRSP *)pvData;
            UCRREQCTX *pCtx = (UCRREQCTX *)pvUser;

            Assert(pCtx->u32Function == u32Function);

            LogFlowFunc(("SETATTRIB\n"));

            uint32_t rcCard;

            if (RT_FAILURE(rcRequest))
            {
                rcCard = VRDE_SCARD_E_NO_SMARTCARD;
            }
            else
            {
                rcCard = pRsp->u32ReturnCode;
            }

            mpDrv->pICardReaderUp->pfnSetAttrib(mpDrv->pICardReaderUp,
                                                pCtx->pvUser,
                                                rcCard,
                                                pCtx->u.SetAttrib.u32AttrId);

            RTMemFree(pCtx);
        } break;

        default:
            AssertFailed();
            vrc = VERR_INVALID_PARAMETER;
            break;
    }

    return vrc;
}

int UsbCardReader::EstablishContext(struct USBCARDREADER *pDrv)
{
    AssertReturn(pDrv == mpDrv, VERR_NOT_SUPPORTED);

    /* The context here is a not a real device context.
     * The device can be detached at the moment, for example the VRDP client did not connect yet.
     */

    return mpDrv->pICardReaderUp->pfnEstablishContext(mpDrv->pICardReaderUp,
                                                      VRDE_SCARD_S_SUCCESS);
}

int UsbCardReader::ReleaseContext(struct USBCARDREADER *pDrv)
{
    AssertReturn(pDrv == mpDrv, VERR_NOT_SUPPORTED);

    int vrc = VINF_SUCCESS;

    if (   !m_pRemote
        || !m_pRemote->fContext)
    {
        /* Do nothing. */
    }
    else
    {
        UCRREQCTX *pCtx = (UCRREQCTX *)RTMemAlloc(sizeof(UCRREQCTX));
        if (!pCtx)
        {
            /* Do nothing. */
        }
        else
        {
            pCtx->pRemote = m_pRemote;
            pCtx->u32Function = VRDE_SCARD_FN_RELEASECONTEXT;
            pCtx->pvUser = NULL;

            VRDESCARDRELEASECONTEXTREQ req;
            req.Context = m_pRemote->context;

            vrc = vrdeSCardRequest(pCtx, VRDE_SCARD_FN_RELEASECONTEXT, &req, sizeof(req));
            if (RT_FAILURE(vrc))
                RTMemFree(pCtx);
            else
                m_pRemote->fContext = false;
        }
    }

    return vrc;
}

int UsbCardReader::GetStatusChange(struct USBCARDREADER *pDrv,
                                   void *pvUser,
                                   uint32_t u32Timeout,
                                   PDMICARDREADER_READERSTATE *paReaderStats,
                                   uint32_t cReaderStats)
{
    AssertReturn(pDrv == mpDrv, VERR_NOT_SUPPORTED);

    int vrc;
    if (   !m_pRemote
        || !m_pRemote->fContext
        || !m_pRemote->reader.fAvailable)
        vrc = mpDrv->pICardReaderUp->pfnSetStatusChange(mpDrv->pICardReaderUp,
                                                        pvUser,
                                                        VRDE_SCARD_E_NO_SMARTCARD,
                                                        paReaderStats,
                                                        cReaderStats);
    else
    {
        UCRREQCTX *pCtx = (UCRREQCTX *)RTMemAlloc(sizeof(UCRREQCTX));
        if (!pCtx)
            vrc = mpDrv->pICardReaderUp->pfnSetStatusChange(mpDrv->pICardReaderUp,
                                                            pvUser,
                                                            VRDE_SCARD_E_NO_MEMORY,
                                                            paReaderStats,
                                                            cReaderStats);
        else
        {
            pCtx->pRemote = m_pRemote;
            pCtx->u32Function = VRDE_SCARD_FN_GETSTATUSCHANGE;
            pCtx->pvUser = pvUser;
            pCtx->u.GetStatusChange.paReaderStats = paReaderStats;
            pCtx->u.GetStatusChange.cReaderStats = cReaderStats;

            VRDESCARDGETSTATUSCHANGEREQ req;
            req.Context = m_pRemote->context;
            req.u32Timeout = u32Timeout;
            req.cReaders = 1;
            req.aReaderStates[0].pszReader = &m_pRemote->reader.szReaderName[0];
            req.aReaderStates[0].u32CurrentState = paReaderStats[0].u32CurrentState;

            vrc = vrdeSCardRequest(pCtx, VRDE_SCARD_FN_GETSTATUSCHANGE, &req, sizeof(req));
            if (RT_FAILURE(vrc))
                RTMemFree(pCtx);
        }
    }

    return vrc;
}

int UsbCardReader::Connect(struct USBCARDREADER *pDrv,
                           void *pvUser,
                           const char *pszReaderName,
                           uint32_t u32ShareMode,
                           uint32_t u32PreferredProtocols)
{
    RT_NOREF(pszReaderName);
    AssertReturn(pDrv == mpDrv, VERR_NOT_SUPPORTED);

    int vrc;
    if (   !m_pRemote
        || !m_pRemote->fContext
        || !m_pRemote->reader.fAvailable)
        vrc = mpDrv->pICardReaderUp->pfnConnect(mpDrv->pICardReaderUp,
                                                pvUser,
                                                VRDE_SCARD_E_NO_SMARTCARD,
                                                VRDE_SCARD_PROTOCOL_T0);
    else
    {
        UCRREQCTX *pCtx = (UCRREQCTX *)RTMemAlloc(sizeof(UCRREQCTX));
        if (!pCtx)
            vrc = mpDrv->pICardReaderUp->pfnConnect(mpDrv->pICardReaderUp,
                                                    pvUser,
                                                    VRDE_SCARD_E_NO_MEMORY,
                                                    VRDE_SCARD_PROTOCOL_T0);
        else
        {
            pCtx->pRemote = m_pRemote;
            pCtx->u32Function = VRDE_SCARD_FN_CONNECT;
            pCtx->pvUser = pvUser;

            VRDESCARDCONNECTREQ req;
            req.Context = m_pRemote->context;
            req.pszReader = &m_pRemote->reader.szReaderName[0];
            req.u32ShareMode = u32ShareMode;
            req.u32PreferredProtocols = u32PreferredProtocols;

            vrc = vrdeSCardRequest(pCtx, VRDE_SCARD_FN_CONNECT, &req, sizeof(req));
            if (RT_FAILURE(vrc))
                RTMemFree(pCtx);
        }
    }

    return vrc;
}

int UsbCardReader::Disconnect(struct USBCARDREADER *pDrv,
                              void *pvUser,
                              uint32_t u32Mode)
{
    AssertReturn(pDrv == mpDrv, VERR_NOT_SUPPORTED);

    int vrc;
    if (   !m_pRemote
        || !m_pRemote->fContext
        || !m_pRemote->reader.fAvailable
        || !m_pRemote->reader.fHandle)
        vrc = mpDrv->pICardReaderUp->pfnDisconnect(mpDrv->pICardReaderUp,
                                                   pvUser,
                                                   VRDE_SCARD_E_NO_SMARTCARD);
    else
    {
        UCRREQCTX *pCtx = (UCRREQCTX *)RTMemAlloc(sizeof(UCRREQCTX));
        if (!pCtx)
            vrc = mpDrv->pICardReaderUp->pfnDisconnect(mpDrv->pICardReaderUp,
                                                       pvUser,
                                                       VRDE_SCARD_E_NO_MEMORY);
        else
        {
            pCtx->pRemote = m_pRemote;
            pCtx->u32Function = VRDE_SCARD_FN_DISCONNECT;
            pCtx->pvUser = pvUser;

            VRDESCARDDISCONNECTREQ req;
            req.hCard = m_pRemote->reader.hCard;
            req.u32Disposition = u32Mode;

            vrc = vrdeSCardRequest(pCtx, VRDE_SCARD_FN_DISCONNECT, &req, sizeof(req));
            if (RT_FAILURE(vrc))
                RTMemFree(pCtx);
            else
                m_pRemote->reader.fHandle = false;
        }
    }

    return vrc;
}

int UsbCardReader::Status(struct USBCARDREADER *pDrv,
                          void *pvUser)
{
    AssertReturn(pDrv == mpDrv, VERR_NOT_SUPPORTED);

    int vrc;
    if (   !m_pRemote
        || !m_pRemote->fContext
        || !m_pRemote->reader.fAvailable
        || !m_pRemote->reader.fHandle)
        vrc = mpDrv->pICardReaderUp->pfnStatus(mpDrv->pICardReaderUp,
                                               pvUser,
                                               VRDE_SCARD_E_NO_SMARTCARD,
                                               /* pszReaderName */ NULL,
                                               /* cchReaderName */ 0,
                                               /* u32CardState */ 0,
                                               /* u32Protocol */ 0,
                                               /* pu8Atr */ 0,
                                               /* cbAtr */ 0);
    else
    {
        UCRREQCTX *pCtx = (UCRREQCTX *)RTMemAlloc(sizeof(UCRREQCTX));
        if (!pCtx)
            vrc = mpDrv->pICardReaderUp->pfnStatus(mpDrv->pICardReaderUp,
                                                   pvUser,
                                                   VRDE_SCARD_E_NO_MEMORY,
                                                   /* pszReaderName */ NULL,
                                                   /* cchReaderName */ 0,
                                                   /* u32CardState */ 0,
                                                   /* u32Protocol */ 0,
                                                   /* pu8Atr */ 0,
                                                   /* cbAtr */ 0);
        else
        {
            pCtx->pRemote = m_pRemote;
            pCtx->u32Function = VRDE_SCARD_FN_STATUS;
            pCtx->pvUser = pvUser;

            VRDESCARDSTATUSREQ req;
            req.hCard = m_pRemote->reader.hCard;

            vrc = vrdeSCardRequest(pCtx, VRDE_SCARD_FN_STATUS, &req, sizeof(req));
            if (RT_FAILURE(vrc))
                RTMemFree(pCtx);
        }
    }

    return vrc;
}

int UsbCardReader::Transmit(struct USBCARDREADER *pDrv,
                            void *pvUser,
                            PDMICARDREADER_IO_REQUEST *pIoSendRequest,
                            uint8_t *pbSendBuffer,
                            uint32_t cbSendBuffer,
                            uint32_t cbRecvBuffer)
{
    AssertReturn(pDrv == mpDrv, VERR_NOT_SUPPORTED);

    int vrc = VINF_SUCCESS;

    UCRREQCTX *pCtx = NULL;
    uint32_t rcSCard = VRDE_SCARD_S_SUCCESS;

    if (   !m_pRemote
        || !m_pRemote->fContext
        || !m_pRemote->reader.fAvailable
        || !m_pRemote->reader.fHandle)
    {
        rcSCard = VRDE_SCARD_E_NO_SMARTCARD;
    }

    if (rcSCard == VRDE_SCARD_S_SUCCESS)
    {
        if (   !pIoSendRequest
            || (   pIoSendRequest->cbPciLength < 2 *  sizeof(uint32_t)
                || pIoSendRequest->cbPciLength > 2 *  sizeof(uint32_t) + VRDE_SCARD_MAX_PCI_DATA)
           )
        {
            AssertFailed();
            rcSCard = VRDE_SCARD_E_INVALID_PARAMETER;
        }
    }

    if (rcSCard == VRDE_SCARD_S_SUCCESS)
    {
        pCtx = (UCRREQCTX *)RTMemAlloc(sizeof(UCRREQCTX));
        if (!pCtx)
        {
            rcSCard = VRDE_SCARD_E_NO_MEMORY;
        }
    }

    if (rcSCard != VRDE_SCARD_S_SUCCESS)
    {
        Assert(pCtx == NULL);

        vrc = pDrv->pICardReaderUp->pfnTransmit(pDrv->pICardReaderUp,
                                                pvUser,
                                                rcSCard,
                                                /* pioRecvPci */ NULL,
                                                /* pu8RecvBuffer */ NULL,
                                                /* cbRecvBuffer*/ 0);
    }
    else
    {
        pCtx->pRemote = m_pRemote;
        pCtx->u32Function = VRDE_SCARD_FN_TRANSMIT;
        pCtx->pvUser = pvUser;

        VRDESCARDTRANSMITREQ req;
        req.hCard = m_pRemote->reader.hCard;

        req.ioSendPci.u32Protocol = pIoSendRequest->u32Protocol;
        req.ioSendPci.u32PciLength = pIoSendRequest->cbPciLength < 2 * sizeof(uint32_t)?
                                         (uint32_t)(2 * sizeof(uint32_t)):
                                         pIoSendRequest->cbPciLength;
        Assert(pIoSendRequest->cbPciLength <= VRDE_SCARD_MAX_PCI_DATA + 2 * sizeof(uint32_t));
        memcpy(req.ioSendPci.au8PciData,
               (uint8_t *)pIoSendRequest + 2 * sizeof(uint32_t),
               req.ioSendPci.u32PciLength - 2 * sizeof(uint32_t));

        req.u32SendLength = cbSendBuffer;
        req.pu8SendBuffer = pbSendBuffer;
        req.u32RecvLength = cbRecvBuffer;

        vrc = vrdeSCardRequest(pCtx, VRDE_SCARD_FN_TRANSMIT, &req, sizeof(req));
        if (RT_FAILURE(vrc))
            RTMemFree(pCtx);
    }

    return vrc;
}

int UsbCardReader::Control(struct USBCARDREADER *pDrv,
                           void *pvUser,
                           uint32_t u32ControlCode,
                           uint8_t *pu8InBuffer,
                           uint32_t cbInBuffer,
                           uint32_t cbOutBuffer)
{
    AssertReturn(pDrv == mpDrv, VERR_NOT_SUPPORTED);

    int vrc = VINF_SUCCESS;

    UCRREQCTX *pCtx = NULL;
    uint32_t rcSCard = VRDE_SCARD_S_SUCCESS;

    if (   !m_pRemote
        || !m_pRemote->fContext
        || !m_pRemote->reader.fAvailable
        || !m_pRemote->reader.fHandle)
    {
        rcSCard = VRDE_SCARD_E_NO_SMARTCARD;
    }

    if (rcSCard == VRDE_SCARD_S_SUCCESS)
    {
        if (   cbInBuffer > _128K
            || cbOutBuffer > _128K)
        {
            AssertFailed();
            rcSCard = VRDE_SCARD_E_INVALID_PARAMETER;
        }
    }

    if (rcSCard == VRDE_SCARD_S_SUCCESS)
    {
        pCtx = (UCRREQCTX *)RTMemAlloc(sizeof(UCRREQCTX));
        if (!pCtx)
        {
            rcSCard = VRDE_SCARD_E_NO_MEMORY;
        }
    }

    if (rcSCard != VRDE_SCARD_S_SUCCESS)
    {
        Assert(pCtx == NULL);

        vrc = pDrv->pICardReaderUp->pfnControl(pDrv->pICardReaderUp,
                                               pvUser,
                                               rcSCard,
                                               u32ControlCode,
                                               /* pvOutBuffer */ NULL,
                                               /* cbOutBuffer*/ 0);
    }
    else
    {
        pCtx->pRemote = m_pRemote;
        pCtx->u32Function = VRDE_SCARD_FN_CONTROL;
        pCtx->pvUser = pvUser;
        pCtx->u.Control.u32ControlCode = u32ControlCode;

        VRDESCARDCONTROLREQ req;
        req.hCard = m_pRemote->reader.hCard;
        req.u32ControlCode = u32ControlCode;
        req.u32InBufferSize = cbInBuffer;
        req.pu8InBuffer = pu8InBuffer;
        req.u32OutBufferSize = cbOutBuffer;

        vrc = vrdeSCardRequest(pCtx, VRDE_SCARD_FN_CONTROL, &req, sizeof(req));
        if (RT_FAILURE(vrc))
            RTMemFree(pCtx);
    }

    return vrc;
}

int UsbCardReader::GetAttrib(struct USBCARDREADER *pDrv,
                             void *pvUser,
                             uint32_t u32AttrId,
                             uint32_t cbAttrib)
{
    AssertReturn(pDrv == mpDrv, VERR_NOT_SUPPORTED);

    int vrc = VINF_SUCCESS;

    UCRREQCTX *pCtx = NULL;
    uint32_t rcSCard = VRDE_SCARD_S_SUCCESS;

    if (   !m_pRemote
        || !m_pRemote->fContext
        || !m_pRemote->reader.fAvailable
        || !m_pRemote->reader.fHandle)
    {
        rcSCard = VRDE_SCARD_E_NO_SMARTCARD;
    }

    if (rcSCard == VRDE_SCARD_S_SUCCESS)
    {
        if (cbAttrib > _128K)
        {
            AssertFailed();
            rcSCard = VRDE_SCARD_E_INVALID_PARAMETER;
        }
    }

    if (rcSCard == VRDE_SCARD_S_SUCCESS)
    {
        pCtx = (UCRREQCTX *)RTMemAlloc(sizeof(UCRREQCTX));
        if (!pCtx)
        {
            rcSCard = VRDE_SCARD_E_NO_MEMORY;
        }
    }

    if (rcSCard != VRDE_SCARD_S_SUCCESS)
    {
        Assert(pCtx == NULL);

        pDrv->pICardReaderUp->pfnGetAttrib(pDrv->pICardReaderUp,
                                           pvUser,
                                           rcSCard,
                                           u32AttrId,
                                           /* pvAttrib */ NULL,
                                           /* cbAttrib */ 0);
    }
    else
    {
        pCtx->pRemote = m_pRemote;
        pCtx->u32Function = VRDE_SCARD_FN_GETATTRIB;
        pCtx->pvUser = pvUser;
        pCtx->u.GetAttrib.u32AttrId = u32AttrId;

        VRDESCARDGETATTRIBREQ req;
        req.hCard = m_pRemote->reader.hCard;
        req.u32AttrId = u32AttrId;
        req.u32AttrLen = cbAttrib;

        vrc = vrdeSCardRequest(pCtx, VRDE_SCARD_FN_GETATTRIB, &req, sizeof(req));
        if (RT_FAILURE(vrc))
            RTMemFree(pCtx);
    }

    return vrc;
}

int UsbCardReader::SetAttrib(struct USBCARDREADER *pDrv,
                             void *pvUser,
                             uint32_t u32AttrId,
                             uint8_t *pu8Attrib,
                             uint32_t cbAttrib)
{
    AssertReturn(pDrv == mpDrv, VERR_NOT_SUPPORTED);

    int vrc = VINF_SUCCESS;

    UCRREQCTX *pCtx = NULL;
    uint32_t rcSCard = VRDE_SCARD_S_SUCCESS;

    if (   !m_pRemote
        || !m_pRemote->fContext
        || !m_pRemote->reader.fAvailable
        || !m_pRemote->reader.fHandle)
    {
        rcSCard = VRDE_SCARD_E_NO_SMARTCARD;
    }

    if (rcSCard == VRDE_SCARD_S_SUCCESS)
    {
        if (cbAttrib > _128K)
        {
            AssertFailed();
            rcSCard = VRDE_SCARD_E_INVALID_PARAMETER;
        }
    }

    if (rcSCard == VRDE_SCARD_S_SUCCESS)
    {
        pCtx = (UCRREQCTX *)RTMemAlloc(sizeof(UCRREQCTX));
        if (!pCtx)
        {
            rcSCard = VRDE_SCARD_E_NO_MEMORY;
        }
    }

    if (rcSCard != VRDE_SCARD_S_SUCCESS)
    {
        Assert(pCtx == NULL);

        pDrv->pICardReaderUp->pfnSetAttrib(pDrv->pICardReaderUp,
                                           pvUser,
                                           rcSCard,
                                           u32AttrId);
    }
    else
    {
        pCtx->pRemote = m_pRemote;
        pCtx->u32Function = VRDE_SCARD_FN_SETATTRIB;
        pCtx->pvUser = pvUser;
        pCtx->u.SetAttrib.u32AttrId = u32AttrId;

        VRDESCARDSETATTRIBREQ req;
        req.hCard = m_pRemote->reader.hCard;
        req.u32AttrId = u32AttrId;
        req.u32AttrLen = cbAttrib;
        req.pu8Attr = pu8Attrib;

        vrc = vrdeSCardRequest(pCtx, VRDE_SCARD_FN_SETATTRIB, &req, sizeof(req));
        if (RT_FAILURE(vrc))
            RTMemFree(pCtx);
    }

    return vrc;
}


/*
 * PDMDRVINS
 */

/* static */ DECLCALLBACK(void *) UsbCardReader::drvQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    LogFlowFunc(("pInterface:%p, pszIID:%s\n", pInterface, pszIID));
    PPDMDRVINS pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PUSBCARDREADER pThis = PDMINS_2_DATA(pDrvIns, PUSBCARDREADER);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMICARDREADERDOWN, &pThis->ICardReaderDown);
    return NULL;
}

/* static */ DECLCALLBACK(void) UsbCardReader::drvDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    LogFlowFunc(("iInstance/%d\n",pDrvIns->iInstance));
    PUSBCARDREADER pThis = PDMINS_2_DATA(pDrvIns, PUSBCARDREADER);

    /** @todo The driver is destroyed before the device.
     * So device calls ReleaseContext when there is no more driver.
     * Notify the device here so it can do cleanup or
     * do a cleanup now in the driver.
     */
    if (pThis->hReqQCardReaderCmd != NIL_RTREQQUEUE)
    {
        int vrc = RTReqQueueDestroy(pThis->hReqQCardReaderCmd);
        AssertRC(vrc);
        pThis->hReqQCardReaderCmd = NIL_RTREQQUEUE;
    }

    pThis->pUsbCardReader->mpDrv = NULL;
    pThis->pUsbCardReader = NULL;
    LogFlowFuncLeave();
}

/* static */ DECLCALLBACK(int) UsbCardReader::drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(fFlags, pCfg);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    LogFlowFunc(("iInstance/%d, pCfg:%p, fFlags:%x\n", pDrvIns->iInstance, pCfg, fFlags));
    PUSBCARDREADER pThis = PDMINS_2_DATA(pDrvIns, PUSBCARDREADER);

    pThis->hReqQCardReaderCmd = NIL_RTREQQUEUE;

    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "", "");
    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    com::Guid uuid(USBCARDREADER_OID);
    pThis->pUsbCardReader = (UsbCardReader *)PDMDrvHlpQueryGenericUserObject(pDrvIns, uuid.raw());
    AssertMsgReturn(RT_VALID_PTR(pThis->pUsbCardReader), ("Configuration error: No/bad USB card reader object value!\n"), VERR_NOT_FOUND);

    pThis->pUsbCardReader->mpDrv = pThis;
    pThis->pDrvIns = pDrvIns;

    pDrvIns->IBase.pfnQueryInterface = UsbCardReader::drvQueryInterface;

    pThis->ICardReaderDown.pfnEstablishContext  = drvCardReaderDownEstablishContext;
    pThis->ICardReaderDown.pfnReleaseContext    = drvCardReaderDownReleaseContext;
    pThis->ICardReaderDown.pfnConnect           = drvCardReaderDownConnect;
    pThis->ICardReaderDown.pfnDisconnect        = drvCardReaderDownDisconnect;
    pThis->ICardReaderDown.pfnStatus            = drvCardReaderDownStatus;
    pThis->ICardReaderDown.pfnGetStatusChange   = drvCardReaderDownGetStatusChange;
    pThis->ICardReaderDown.pfnBeginTransaction  = drvCardReaderDownBeginTransaction;
    pThis->ICardReaderDown.pfnEndTransaction    = drvCardReaderDownEndTransaction;
    pThis->ICardReaderDown.pfnTransmit          = drvCardReaderDownTransmit;
    pThis->ICardReaderDown.pfnGetAttr           = drvCardReaderDownGetAttr;
    pThis->ICardReaderDown.pfnSetAttr           = drvCardReaderDownSetAttr;
    pThis->ICardReaderDown.pfnControl           = drvCardReaderDownControl;

    pThis->pICardReaderUp = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMICARDREADERUP);
    AssertReturn(pThis->pICardReaderUp, VERR_PDM_MISSING_INTERFACE);

    /* Command Thread Synchronization primitives */
    int vrc = RTReqQueueCreate(&pThis->hReqQCardReaderCmd);
    AssertLogRelRCReturn(vrc, vrc);

    vrc = PDMDrvHlpThreadCreate(pDrvIns,
                                &pThis->pThrCardReaderCmd,
                                pThis,
                                drvCardReaderThreadCmd /* worker routine */,
                                drvCardReaderThreadCmdWakeup /* wakeup routine */,
                                128 * _1K, RTTHREADTYPE_IO, "UCRCMD");
    if (RT_FAILURE(vrc))
    {
        RTReqQueueDestroy(pThis->hReqQCardReaderCmd);
        pThis->hReqQCardReaderCmd = NIL_RTREQQUEUE;
    }

    LogFlowFunc(("LEAVE: %Rrc\n", vrc));
    return vrc;
}

/* static */ const PDMDRVREG UsbCardReader::DrvReg =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName[32] */
    "UsbCardReader",
    /* szRCMod[32] */
    "",
    /* szR0Mod[32] */
    "",
    /* pszDescription */
    "Main Driver communicating with VRDE",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass */
    PDM_DRVREG_CLASS_USB,
    /* cMaxInstances */
    1,
    /* cbInstance */
    sizeof(USBCARDREADER),
    /* pfnConstruct */
    UsbCardReader::drvConstruct,
    /* pfnDestruct */
    UsbCardReader::drvDestruct,
    /* pfnRelocate */
    NULL,
    /* pfnIOCtl */
    NULL,
    /* pfnPowerOn */
    NULL,
    /* pfnReset */
    NULL,
    /* pfnSuspend */
    NULL,
    /* pfnResume */
    NULL,
    /* pfnAttach */
    NULL,
    /* pfnDetach */
    NULL,
    /* pfnPowerOff */
    NULL,
    /* pfnSoftReset */
    NULL,
    /* u32VersionEnd */
    PDM_DRVREG_VERSION
};
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
