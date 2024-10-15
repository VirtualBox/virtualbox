/* $Id: USBProxyBackendUsbIp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox USB Proxy Backend, USB/IP.
 */

/*
 * Copyright (C) 2015-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_MAIN_USBPROXYBACKEND
#include "USBProxyService.h"
#include "USBGetDevices.h"
#include "LoggingNew.h"

#include <VBox/usb.h>
#include <VBox/usblib.h>
#include <VBox/err.h>

#include <iprt/string.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/tcp.h>
#include <iprt/env.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/pipe.h>
#include <iprt/asm.h>
#include <iprt/cdefs.h>
#include <iprt/time.h>

/** The USB/IP default port to connect to. */
#define USBIP_PORT_DEFAULT    3240
/** The USB version number used for the protocol. */
#define USBIP_VERSION         UINT16_C(0x0111)
/** Request indicator in the command code. */
#define USBIP_INDICATOR_REQ   RT_BIT(15)

/** Command/Reply code for OP_REQ/RET_DEVLIST. */
#define USBIP_REQ_RET_DEVLIST UINT16_C(5)

/** @todo Duplicate code in USBProxyDevice-usbip.cpp */
/**
 * Exported device entry in the OP_RET_DEVLIST reply.
 */
#pragma pack(1)
typedef struct UsbIpExportedDevice
{
    /** Path of the device, zero terminated string. */
    char     szPath[256];
    /** Bus ID of the exported device, zero terminated string. */
    char     szBusId[32];
    /** Bus number. */
    uint32_t u32BusNum;
    /** Device number. */
    uint32_t u32DevNum;
    /** Speed indicator of the device. */
    uint32_t u32Speed;
    /** Vendor ID of the device. */
    uint16_t u16VendorId;
    /** Product ID of the device. */
    uint16_t u16ProductId;
    /** Device release number. */
    uint16_t u16BcdDevice;
    /** Device class. */
    uint8_t  bDeviceClass;
    /** Device Subclass. */
    uint8_t  bDeviceSubClass;
    /** Device protocol. */
    uint8_t  bDeviceProtocol;
    /** Configuration value. */
    uint8_t  bConfigurationValue;
    /** Current configuration value of the device. */
    uint8_t  bNumConfigurations;
    /** Number of interfaces for the device. */
    uint8_t  bNumInterfaces;
} UsbIpExportedDevice;
/** Pointer to a exported device entry. */
typedef UsbIpExportedDevice *PUsbIpExportedDevice;
#pragma pack()
AssertCompileSize(UsbIpExportedDevice, 312);

/**
 * Interface descriptor entry for an exported device.
 */
#pragma pack(1)
typedef struct UsbIpDeviceInterface
{
    /** Intefrace class. */
    uint8_t  bInterfaceClass;
    /** Interface sub class. */
    uint8_t  bInterfaceSubClass;
    /** Interface protocol identifier. */
    uint8_t  bInterfaceProtocol;
    /** Padding byte for alignment. */
    uint8_t  bPadding;
} UsbIpDeviceInterface;
/** Pointer to an interface descriptor entry. */
typedef UsbIpDeviceInterface *PUsbIpDeviceInterface;
#pragma pack()

/**
 * USB/IP device list request.
 */
#pragma pack(1)
typedef struct UsbIpReqDevList
{
    /** Protocol version number. */
    uint16_t     u16Version;
    /** Command code. */
    uint16_t     u16Cmd;
    /** Status field, unused. */
    int32_t      i32Status;
} UsbIpReqDevList;
/** Pointer to a device list request. */
typedef UsbIpReqDevList *PUsbIpReqDevList;
#pragma pack()

/**
 * USB/IP Import reply.
 *
 * This is only the header, for successful
 * requests the device details are sent to as
 * defined in UsbIpExportedDevice.
 */
#pragma pack(1)
typedef struct UsbIpRetDevList
{
    /** Protocol version number. */
    uint16_t     u16Version;
    /** Command code. */
    uint16_t     u16Cmd;
    /** Status field, unused. */
    int32_t      i32Status;
    /** Number of exported devices. */
    uint32_t     u32DevicesExported;
} UsbIpRetDevList;
/** Pointer to a import reply. */
typedef UsbIpRetDevList *PUsbIpRetDevList;
#pragma pack()

/** Pollset id of the socket. */
#define USBIP_POLL_ID_SOCKET 0
/** Pollset id of the pipe. */
#define USBIP_POLL_ID_PIPE   1

/** @name USB/IP error codes.
 * @{ */
/** Success indicator. */
#define USBIP_STATUS_SUCCESS                 INT32_C(0)
/** @} */

/** @name USB/IP device speeds.
 * @{ */
/** Unknown speed. */
#define USBIP_SPEED_UNKNOWN  0
/** Low (1.0) speed. */
#define USBIP_SPEED_LOW      1
/** Full (1.1) speed. */
#define USBIP_SPEED_FULL     2
/** High (2.0) speed. */
#define USBIP_SPEED_HIGH     3
/** Variable (CWUSB) speed. */
#define USBIP_SPEED_WIRELESS 4
/** Super (3.0) speed. */
#define USBIP_SPEED_SUPER    5
/** @} */

/**
 * Private USB/IP proxy backend data.
 */
struct USBProxyBackendUsbIp::Data
{
    Data()
        : hSocket(NIL_RTSOCKET),
          hWakeupPipeR(NIL_RTPIPE),
          hWakeupPipeW(NIL_RTPIPE),
          hPollSet(NIL_RTPOLLSET),
          uPort(USBIP_PORT_DEFAULT),
          pszHost(NULL),
          hMtxDevices(NIL_RTSEMFASTMUTEX),
          cUsbDevicesCur(0),
          pUsbDevicesCur(NULL),
          enmRecvState(kUsbIpRecvState_Invalid),
          cbResidualRecv(0),
          pbRecvBuf(NULL),
          cDevicesLeft(0),
          pHead(NULL),
          ppNext(&pHead)
    { }

    /** Socket handle to the server. */
    RTSOCKET       hSocket;
    /** Pipe used to interrupt wait(), the read end. */
    RTPIPE         hWakeupPipeR;
    /** Pipe used to interrupt wait(), the write end. */
    RTPIPE         hWakeupPipeW;
    /** Pollset for the socket and wakeup pipe. */
    RTPOLLSET      hPollSet;
    /** Port of the USB/IP host to connect to. */
    uint32_t       uPort;
    /** USB/IP host address. */
    char          *pszHost;
    /** Mutex protecting the device list against concurrent access. */
    RTSEMFASTMUTEX hMtxDevices;
    /** Number of devices in the list. */
    uint32_t       cUsbDevicesCur;
    /** The current list of devices to compare with. */
    PUSBDEVICE     pUsbDevicesCur;
    /** Current receive state. */
    USBIPRECVSTATE enmRecvState;
    /** Scratch space for holding the data until it was completely received.
     * Which one to access is based on the current receive state. */
    union
    {
        UsbIpRetDevList      RetDevList;
        UsbIpExportedDevice  ExportedDevice;
        UsbIpDeviceInterface DeviceInterface;
        /** Byte view. */
        uint8_t              abRecv[1];
    } Scratch;
    /** Residual number of bytes to receive before we can work with the data. */
    size_t                   cbResidualRecv;
    /** Current pointer into the scratch buffer. */
    uint8_t                 *pbRecvBuf;
    /** Number of devices left to receive for the current request. */
    uint32_t                 cDevicesLeft;
    /** Number of interfaces to skip during receive. */
    uint32_t                 cInterfacesLeft;
    /** The current head pointer for the new device list. */
    PUSBDEVICE               pHead;
    /** The next pointer to add a device to. */
    PUSBDEVICE              *ppNext;
    /** Current amount of devices in the list. */
    uint32_t                 cDevicesCur;
    /** Timestamp of the last time we successfully connected. */
    uint64_t                 tsConnectSuccessLast;
};

/**
 * Convert the given exported device structure from host to network byte order.
 *
 * @param   pDevice           The device structure to convert.
 */
DECLINLINE(void) usbProxyBackendUsbIpExportedDeviceN2H(PUsbIpExportedDevice pDevice)
{
    pDevice->u32BusNum    = RT_N2H_U32(pDevice->u32BusNum);
    pDevice->u32DevNum    = RT_N2H_U32(pDevice->u32DevNum);
    pDevice->u32Speed     = RT_N2H_U32(pDevice->u32Speed);
    pDevice->u16VendorId  = RT_N2H_U16(pDevice->u16VendorId);
    pDevice->u16ProductId = RT_N2H_U16(pDevice->u16ProductId);
    pDevice->u16BcdDevice = RT_N2H_U16(pDevice->u16BcdDevice);
}

/**
 * Initialize data members.
 */
USBProxyBackendUsbIp::USBProxyBackendUsbIp()
    : USBProxyBackend()
{
}

USBProxyBackendUsbIp::~USBProxyBackendUsbIp()
{

}

/**
 * Initializes the object (called right after construction).
 *
 * @returns S_OK on success and non-fatal failures, some COM error otherwise.
 */
int USBProxyBackendUsbIp::init(USBProxyService *pUsbProxyService, const com::Utf8Str &strId,
                               const com::Utf8Str &strAddress, bool fLoadingSettings)
{
    int vrc = VINF_SUCCESS;

    USBProxyBackend::init(pUsbProxyService, strId, strAddress, fLoadingSettings);

    unconst(m_strBackend) = Utf8Str("USBIP");

    m = new Data;

    m->tsConnectSuccessLast = 0;

    /* Split address into hostname and port. */
    RTCList<RTCString> lstAddress = strAddress.split(":");
    if (lstAddress.size() < 1)
        return VERR_INVALID_PARAMETER;
    m->pszHost = RTStrDup(lstAddress[0].c_str());
    if (!m->pszHost)
        return VERR_NO_STR_MEMORY;
    if (lstAddress.size() == 2)
    {
        m->uPort = lstAddress[1].toUInt32();
        if (!m->uPort)
            return VERR_INVALID_PARAMETER;
    }

    /* Setup wakeup pipe and poll set first. */
    vrc = RTSemFastMutexCreate(&m->hMtxDevices);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTPipeCreate(&m->hWakeupPipeR, &m->hWakeupPipeW, 0);
        if (RT_SUCCESS(vrc))
        {
            vrc = RTPollSetCreate(&m->hPollSet);
            if (RT_SUCCESS(vrc))
            {
                vrc = RTPollSetAddPipe(m->hPollSet, m->hWakeupPipeR, RTPOLL_EVT_READ, USBIP_POLL_ID_PIPE);
                if (RT_SUCCESS(vrc))
                {
                    /*
                     * Connect to the USB/IP host. Be more graceful to connection errors
                     * if we are instantiated while the settings are loaded to let
                     * VBoxSVC start.
                     *
                     * The worker thread keeps trying to connect every few seconds until
                     * either the USB source is removed by the user or the USB server is
                     * reachable.
                     */
                    vrc = reconnect();
                    if (RT_SUCCESS(vrc) || fLoadingSettings)
                        vrc = start(); /* Start service thread. */
                }

                if (RT_FAILURE(vrc))
                {
                    RTPollSetRemove(m->hPollSet, USBIP_POLL_ID_PIPE);
                    int vrc2 = RTPollSetDestroy(m->hPollSet);
                    AssertRC(vrc2);
                    m->hPollSet = NIL_RTPOLLSET;
                }
            }

            if (RT_FAILURE(vrc))
            {
                int vrc2 = RTPipeClose(m->hWakeupPipeR);
                AssertRC(vrc2);
                vrc2 = RTPipeClose(m->hWakeupPipeW);
                AssertRC(vrc2);
                m->hWakeupPipeR = m->hWakeupPipeW = NIL_RTPIPE;
            }
        }
        if (RT_FAILURE(vrc))
        {
            RTSemFastMutexDestroy(m->hMtxDevices);
            m->hMtxDevices = NIL_RTSEMFASTMUTEX;
        }
    }

    return vrc;
}

/**
 * Stop all service threads and free the device chain.
 */
void USBProxyBackendUsbIp::uninit()
{
    LogFlowThisFunc(("\n"));

    /*
     * Stop the service.
     */
    if (isActive())
        stop();

    /*
     * Free resources.
     */
    if (m->hPollSet != NIL_RTPOLLSET)
    {
        disconnect();

        int vrc = RTPollSetRemove(m->hPollSet, USBIP_POLL_ID_PIPE);
        AssertRC(vrc);
        vrc = RTPollSetDestroy(m->hPollSet);
        AssertRC(vrc);
        vrc = RTPipeClose(m->hWakeupPipeR);
        AssertRC(vrc);
        vrc = RTPipeClose(m->hWakeupPipeW);
        AssertRC(vrc);

        m->hPollSet = NIL_RTPOLLSET;
        m->hWakeupPipeR = NIL_RTPIPE;
        m->hWakeupPipeW = NIL_RTPIPE;
    }

    if (m->pszHost)
        RTStrFree(m->pszHost);
    if (m->hMtxDevices != NIL_RTSEMFASTMUTEX)
    {
        RTSemFastMutexDestroy(m->hMtxDevices);
        m->hMtxDevices = NIL_RTSEMFASTMUTEX;
    }

    delete m;
    USBProxyBackend::uninit();
}


int USBProxyBackendUsbIp::captureDevice(HostUSBDevice *aDevice)
{
    AssertReturn(aDevice, VERR_GENERAL_FAILURE);
    AssertReturn(!aDevice->isWriteLockOnCurrentThread(), VERR_GENERAL_FAILURE);

    AutoReadLock devLock(aDevice COMMA_LOCKVAL_SRC_POS);
    LogFlowThisFunc(("aDevice=%s\n", aDevice->i_getName().c_str()));

    /*
     * We don't need to do anything when the device is held... fake it.
     */
    Assert(aDevice->i_getUnistate() == kHostUSBDeviceState_Capturing);
    devLock.release();

    return VINF_SUCCESS;
}


int USBProxyBackendUsbIp::releaseDevice(HostUSBDevice *aDevice)
{
    AssertReturn(aDevice, VERR_GENERAL_FAILURE);
    AssertReturn(!aDevice->isWriteLockOnCurrentThread(), VERR_GENERAL_FAILURE);

    AutoReadLock devLock(aDevice COMMA_LOCKVAL_SRC_POS);
    LogFlowThisFunc(("aDevice=%s\n", aDevice->i_getName().c_str()));

    /*
     * We're not really holding it atm., just fake it.
     */
    Assert(aDevice->i_getUnistate() == kHostUSBDeviceState_ReleasingToHost);
    devLock.release();

    return VINF_SUCCESS;
}


bool USBProxyBackendUsbIp::isFakeUpdateRequired()
{
    return true;
}


int USBProxyBackendUsbIp::wait(RTMSINTERVAL aMillies)
{
    int vrc = VINF_SUCCESS;
    bool fDeviceListChangedOrWokenUp = false;

    /* Don't start any possibly lengthy operation if we are supposed to return immediately again. */
    if (!aMillies)
        return VINF_SUCCESS;

    /* Try to reconnect once when we enter if we lost the connection earlier. */
    if (m->hSocket == NIL_RTSOCKET)
        reconnect();

    /* Query a new device list upon entering. */
    if (   m->hSocket != NIL_RTSOCKET
        && m->enmRecvState == kUsbIpRecvState_None)
    {
        vrc = startListExportedDevicesReq();
        if (RT_FAILURE(vrc))
            disconnect();
    }

    /*
     * Because the USB/IP protocol doesn't specify a way to get notified about
     * new or removed exported devices we have to poll the host periodically for
     * a new device list and compare it with the previous one notifying the proxy
     * service about changes.
     */
    while (   !fDeviceListChangedOrWokenUp
           && aMillies > 0
           && RT_SUCCESS(vrc))
    {
        RTMSINTERVAL msWait = aMillies;
        uint64_t msPollStart = RTTimeMilliTS();
        uint32_t uIdReady = 0;
        uint32_t fEventsRecv = 0;

        /* Limit the waiting time to 3sec so we can either reconnect or get a new device list. */
        if (m->hSocket == NIL_RTSOCKET || m->enmRecvState == kUsbIpRecvState_None)
            msWait = RT_MIN(3000, aMillies);

        vrc = RTPoll(m->hPollSet, msWait, &fEventsRecv, &uIdReady);
        if (RT_SUCCESS(vrc))
        {
            if (uIdReady == USBIP_POLL_ID_PIPE)
            {
                /* Drain the wakeup pipe. */
                char bRead = 0;
                size_t cbRead = 0;

                vrc = RTPipeRead(m->hWakeupPipeR, &bRead, 1, &cbRead);
                Assert(RT_SUCCESS(vrc) && cbRead == 1);
                fDeviceListChangedOrWokenUp = true;
            }
            else if (uIdReady == USBIP_POLL_ID_SOCKET)
            {
                if (fEventsRecv & RTPOLL_EVT_READ)
                    vrc = receiveData();
                if (   RT_SUCCESS(vrc)
                    && (fEventsRecv & RTPOLL_EVT_ERROR))
                    vrc = VERR_NET_SHUTDOWN;

                /*
                 * If we are in the none state again we received the previous request
                 * and have a new device list to compare the old against.
                 */
                if (m->enmRecvState == kUsbIpRecvState_None)
                {
                    if (hasDevListChanged(m->pHead))
                        fDeviceListChangedOrWokenUp = true;

                    /* Update to the new list in any case now that we have it anyway. */
                    RTSemFastMutexRequest(m->hMtxDevices);
                    freeDeviceList(m->pUsbDevicesCur);
                    m->cUsbDevicesCur = m->cDevicesCur;
                    m->pUsbDevicesCur = m->pHead;
                    RTSemFastMutexRelease(m->hMtxDevices);

                    m->pHead = NULL;
                    resetRecvState();
                }

                /* Current USB/IP server closes the connection after each request, don't abort but try again. */
                if (vrc == VERR_NET_SHUTDOWN || vrc == VERR_BROKEN_PIPE || vrc == VERR_NET_CONNECTION_RESET_BY_PEER)
                {
                    Log(("USB/IP: Lost connection to host \"%s\", trying to reconnect...\n", m->pszHost));
                    disconnect();
                    vrc = VINF_SUCCESS;
                }
            }
            else
            {
                AssertMsgFailed(("Invalid poll ID returned\n"));
                vrc = VERR_INVALID_STATE;
            }
            aMillies -= (RTMSINTERVAL)(RTTimeMilliTS() - msPollStart);
        }
        else if (vrc == VERR_TIMEOUT)
        {
            aMillies -= msWait;
            if (aMillies)
            {
                /* Try to reconnect and start a new request if we lost the connection before. */
                if (m->hSocket == NIL_RTSOCKET)
                {
                    vrc = reconnect();
                    if (RT_SUCCESS(vrc))
                        vrc = startListExportedDevicesReq();
                    else if (   vrc == VERR_NET_SHUTDOWN
                             || vrc == VERR_BROKEN_PIPE
                             || vrc == VERR_NET_CONNECTION_RESET_BY_PEER
                             || vrc == VERR_NET_CONNECTION_REFUSED)
                    {
                        if (hasDevListChanged(m->pHead))
                            fDeviceListChangedOrWokenUp = true;
                        vrc = VINF_SUCCESS;
                    }
                }
            }
        }
    }

    LogFlowFunc(("return vrc=%Rrc\n", vrc));
    return vrc;
}


int USBProxyBackendUsbIp::interruptWait(void)
{
    AssertReturn(!isWriteLockOnCurrentThread(), VERR_GENERAL_FAILURE);

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    int vrc = RTPipeWriteBlocking(m->hWakeupPipeW, "", 1, NULL);
    if (RT_SUCCESS(vrc))
        RTPipeFlush(m->hWakeupPipeW);
    LogFlowFunc(("returning %Rrc\n", vrc));
    return vrc;
}


PUSBDEVICE USBProxyBackendUsbIp::getDevices(void)
{
    PUSBDEVICE pFirst = NULL;
    PUSBDEVICE *ppNext = &pFirst;

    LogFlowThisFunc(("\n"));

    /* Create a deep copy of the device list. */
    RTSemFastMutexRequest(m->hMtxDevices);
    PUSBDEVICE pCur = m->pUsbDevicesCur;
    while (pCur)
    {
        PUSBDEVICE pNew = (PUSBDEVICE)RTMemAllocZ(sizeof(USBDEVICE));
        if (pNew)
        {
            pNew->pszManufacturer    = RTStrDup(pCur->pszManufacturer);
            pNew->pszProduct         = RTStrDup(pCur->pszProduct);
            if (pCur->pszSerialNumber)
                pNew->pszSerialNumber = RTStrDup(pCur->pszSerialNumber);
            pNew->pszBackend         = RTStrDup(pCur->pszBackend);
            pNew->pszAddress         = RTStrDup(pCur->pszAddress);

            pNew->idVendor           = pCur->idVendor;
            pNew->idProduct          = pCur->idProduct;
            pNew->bcdDevice          = pCur->bcdDevice;
            pNew->bcdUSB             = pCur->bcdUSB;
            pNew->bDeviceClass       = pCur->bDeviceClass;
            pNew->bDeviceSubClass    = pCur->bDeviceSubClass;
            pNew->bDeviceProtocol    = pCur->bDeviceProtocol;
            pNew->bNumConfigurations = pCur->bNumConfigurations;
            pNew->enmState           = pCur->enmState;
            pNew->u64SerialHash      = pCur->u64SerialHash;
            pNew->bBus               = pCur->bBus;
            pNew->bPort              = pCur->bPort;
            pNew->enmSpeed           = pCur->enmSpeed;

            /* link it */
            pNew->pNext = NULL;
            pNew->pPrev = *ppNext;
            *ppNext = pNew;
            ppNext = &pNew->pNext;
        }

        pCur = pCur->pNext;
    }
    RTSemFastMutexRelease(m->hMtxDevices);

    LogFlowThisFunc(("returning %#p\n", pFirst));
    return pFirst;
}

/**
 * Frees a given device list.
 *
 * @param   pHead    The head of the device list to free.
 */
void USBProxyBackendUsbIp::freeDeviceList(PUSBDEVICE pHead)
{
    PUSBDEVICE pNext = pHead;
    while (pNext)
    {
        PUSBDEVICE pFree = pNext;
        pNext = pNext->pNext;
        freeDevice(pFree);
    }
}

/**
 * Resets the receive state to the idle state.
 */
void USBProxyBackendUsbIp::resetRecvState()
{
    LogFlowFunc(("\n"));
    freeDeviceList(m->pHead);
    m->pHead          = NULL;
    m->ppNext         = &m->pHead;
    m->cDevicesCur    = 0;
    m->enmRecvState   = kUsbIpRecvState_None;
    m->cbResidualRecv = 0;
    m->pbRecvBuf      = &m->Scratch.abRecv[0];
    m->cDevicesLeft   = 0;
    LogFlowFunc(("\n"));
}

/**
 * Disconnects from the host and resets the receive state.
 */
void USBProxyBackendUsbIp::disconnect()
{
    LogFlowFunc(("\n"));

    if (m->hSocket != NIL_RTSOCKET)
    {
        int vrc = RTPollSetRemove(m->hPollSet, USBIP_POLL_ID_SOCKET);
        NOREF(vrc);
        Assert(RT_SUCCESS(vrc) || vrc == VERR_POLL_HANDLE_ID_NOT_FOUND);

        RTTcpClientCloseEx(m->hSocket, false /*fGracefulShutdown*/);
        m->hSocket = NIL_RTSOCKET;
    }

    resetRecvState();
    LogFlowFunc(("returns\n"));
}

/**
 * Tries to reconnect to the USB/IP host.
 *
 * @returns VBox status code.
 */
int USBProxyBackendUsbIp::reconnect()
{
    LogFlowFunc(("\n"));

    /* Make sure we are disconnected. */
    disconnect();

    /* Connect to the USB/IP host. */
    int vrc = RTTcpClientConnect(m->pszHost, m->uPort, &m->hSocket);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTTcpSetSendCoalescing(m->hSocket, false);
        if (RT_FAILURE(vrc))
            LogRelMax(5, ("USB/IP: Disabling send coalescing failed (vrc=%Rrc), continuing nevertheless but expect increased latency\n", vrc));

        vrc = RTPollSetAddSocket(m->hPollSet, m->hSocket, RTPOLL_EVT_READ | RTPOLL_EVT_ERROR, USBIP_POLL_ID_SOCKET);
        if (RT_FAILURE(vrc))
        {
            RTTcpClientCloseEx(m->hSocket, false /*fGracefulShutdown*/);
            m->hSocket = NIL_RTSOCKET;
        }
        else
        {
            LogFlowFunc(("Connected to host \"%s\"\n", m->pszHost));
            m->tsConnectSuccessLast = RTTimeMilliTS();
        }
    }
    else if (m->tsConnectSuccessLast + 10 * RT_MS_1SEC < RTTimeMilliTS())
    {
        /* Make sure the device list is clear if we failed to reconnect for some time. */
        RTSemFastMutexRequest(m->hMtxDevices);
        if (m->pUsbDevicesCur)
        {
            freeDeviceList(m->pUsbDevicesCur);
            m->cUsbDevicesCur = 0;
            m->pUsbDevicesCur = NULL;
        }
        RTSemFastMutexRelease(m->hMtxDevices);
    }

    LogFlowFunc(("returns vrc=%Rrc\n", vrc));
    return vrc;
}

/**
 * Initiates a new List Exported Devices request.
 *
 * @returns VBox status code.
 */
int USBProxyBackendUsbIp::startListExportedDevicesReq()
{
    int vrc = VINF_SUCCESS;

    LogFlowFunc(("\n"));

    /*
     * Reset the current state and reconnect in case we were called in the middle
     * of another transfer (which should not happen).
     */
    Assert(m->enmRecvState == kUsbIpRecvState_None);
    if (m->enmRecvState != kUsbIpRecvState_None)
        vrc = reconnect();

    if (RT_SUCCESS(vrc))
    {
        /* Send of the request. */
        UsbIpReqDevList ReqDevList;
        ReqDevList.u16Version = RT_H2N_U16(USBIP_VERSION);
        ReqDevList.u16Cmd     = RT_H2N_U16(USBIP_INDICATOR_REQ | USBIP_REQ_RET_DEVLIST);
        ReqDevList.i32Status  = RT_H2N_S32(0);

        vrc = RTTcpWrite(m->hSocket, &ReqDevList, sizeof(ReqDevList));
        if (RT_SUCCESS(vrc))
            advanceState(kUsbIpRecvState_Hdr);
    }

    LogFlowFunc(("returns vrc=%Rrc\n", vrc));
    return vrc;
}

/**
 * Advances the state machine to the given state.
 *
 * @param   enmRecvState    The new receive state.
 */
void USBProxyBackendUsbIp::advanceState(USBIPRECVSTATE enmRecvState)
{
    LogFlowFunc(("enmRecvState=%u\n", enmRecvState));

    switch (enmRecvState)
    {
        case kUsbIpRecvState_None:
            break;
        case kUsbIpRecvState_Hdr:
        {
            m->cbResidualRecv = sizeof(UsbIpRetDevList);
            m->pbRecvBuf      = (uint8_t *)&m->Scratch.RetDevList;
            break;
        }
        case kUsbIpRecvState_ExportedDevice:
        {
            m->cbResidualRecv = sizeof(UsbIpExportedDevice);
            m->pbRecvBuf      = (uint8_t *)&m->Scratch.ExportedDevice;
            break;
        }
        case kUsbIpRecvState_DeviceInterface:
        {
            m->cbResidualRecv = sizeof(UsbIpDeviceInterface);
            m->pbRecvBuf      = (uint8_t *)&m->Scratch.DeviceInterface;
            break;
        }
        default:
            AssertMsgFailed(("Invalid USB/IP receive state %d\n", enmRecvState));
            return;
    }

    m->enmRecvState = enmRecvState;
    LogFlowFunc(("returns\n"));
}

/**
 * Receives data from the USB/IP host and processes it when everything for the current
 * state was received.
 *
 * @returns VBox status code.
 */
int USBProxyBackendUsbIp::receiveData()
{
    int vrc = VINF_SUCCESS;
    size_t cbRecvd = 0;

    LogFlowFunc(("\n"));

    do
    {
        vrc = RTTcpReadNB(m->hSocket, m->pbRecvBuf, m->cbResidualRecv, &cbRecvd);

        LogFlowFunc(("RTTcpReadNB(%#p, %#p, %zu, %zu) -> %Rrc\n", m->hSocket, m->pbRecvBuf, m->cbResidualRecv, cbRecvd, vrc));

        if (   vrc == VINF_SUCCESS
            && cbRecvd > 0)
        {
            m->cbResidualRecv -= cbRecvd;
            m->pbRecvBuf      += cbRecvd;
            /* In case we received everything for the current state process the data. */
            if (!m->cbResidualRecv)
            {
                vrc = processData();
                if (   RT_SUCCESS(vrc)
                    && m->enmRecvState == kUsbIpRecvState_None)
                    break;
            }
        }
        else if (vrc == VINF_TRY_AGAIN)
            Assert(!cbRecvd);

    } while (vrc == VINF_SUCCESS && cbRecvd > 0);

    if (vrc == VINF_TRY_AGAIN)
        vrc = VINF_SUCCESS;

    LogFlowFunc(("returns vrc=%Rrc\n", vrc));
    return vrc;
}

/**
 * Processes the data in the scratch buffer based on the current state.
 *
 * @returns VBox status code.
 */
int USBProxyBackendUsbIp::processData()
{
    int vrc = VINF_SUCCESS;

    switch (m->enmRecvState)
    {
        case kUsbIpRecvState_Hdr:
        {
            /* Check that the reply matches our expectations. */
            if (   RT_N2H_U16(m->Scratch.RetDevList.u16Version) == USBIP_VERSION
                && RT_N2H_U16(m->Scratch.RetDevList.u16Cmd) == USBIP_REQ_RET_DEVLIST
                && RT_N2H_S32(m->Scratch.RetDevList.i32Status) == USBIP_STATUS_SUCCESS)

            {
                /* Populate the number of exported devices in the list and go to the next state. */
                m->cDevicesLeft = RT_N2H_U32(m->Scratch.RetDevList.u32DevicesExported);
                if (m->cDevicesLeft)
                    advanceState(kUsbIpRecvState_ExportedDevice);
                else
                    advanceState(kUsbIpRecvState_None);
            }
            else
            {
                LogRelMax(10, ("USB/IP: Host sent an invalid reply to the list exported device request (Version: %#x Cmd: %#x Status: %#x)\n",
                               RT_N2H_U16(m->Scratch.RetDevList.u16Version), RT_N2H_U16(m->Scratch.RetDevList.u16Cmd),
                               RT_N2H_S32(m->Scratch.RetDevList.i32Status)));
                /* Disconnect and start over. */
                advanceState(kUsbIpRecvState_None);
                disconnect();
                vrc = VERR_NET_SHUTDOWN;
            }
            break;
        }
        case kUsbIpRecvState_ExportedDevice:
        {
            /* Create a new device and add it to the list. */
            usbProxyBackendUsbIpExportedDeviceN2H(&m->Scratch.ExportedDevice);
            vrc = addDeviceToList(&m->Scratch.ExportedDevice);
            if (RT_SUCCESS(vrc))
            {
                m->cInterfacesLeft = m->Scratch.ExportedDevice.bNumInterfaces;
                if (m->cInterfacesLeft)
                    advanceState(kUsbIpRecvState_DeviceInterface);
                else
                {
                    m->cDevicesLeft--;
                    if (m->cDevicesLeft)
                        advanceState(kUsbIpRecvState_ExportedDevice);
                    else
                        advanceState(kUsbIpRecvState_None);
                }
            }
            break;
        }
        case kUsbIpRecvState_DeviceInterface:
        {
            /*
             * If all interfaces for the current device were received receive the next device
             * if there is another one left, if not we are done with the current request.
             */
            m->cInterfacesLeft--;
            if (m->cInterfacesLeft)
                advanceState(kUsbIpRecvState_DeviceInterface);
            else
            {
                m->cDevicesLeft--;
                if (m->cDevicesLeft)
                    advanceState(kUsbIpRecvState_ExportedDevice);
                else
                    advanceState(kUsbIpRecvState_None);
            }
            break;
        }
        case kUsbIpRecvState_None:
        default:
            AssertMsgFailed(("Invalid USB/IP receive state %d\n", m->enmRecvState));
            return VERR_INVALID_STATE;
    }

    return vrc;
}

/**
 * Creates a new USB device and adds it to the list.
 *
 * @returns VBox status code.
 * @param   pDev    Pointer to the USB/IP exported device structure to take
 *                  the information for the new device from.
 */
int USBProxyBackendUsbIp::addDeviceToList(PUsbIpExportedDevice pDev)
{
    int vrc = VINF_SUCCESS;
    PUSBDEVICE pNew = (PUSBDEVICE)RTMemAllocZ(sizeof(USBDEVICE));
    if (!pNew)
        return VERR_NO_MEMORY;

    pNew->pszManufacturer = RTStrDup("");
    pNew->pszProduct      = RTStrDup("");
    pNew->pszSerialNumber = NULL;
    pNew->pszBackend      = RTStrDup("usbip");

    /* Make sure the Bus id is 0 terminated. */
    pDev->szBusId[31] = '\0';
    pNew->pszAddress = RTStrAPrintf2("usbip://%s:%u:%s", m->pszHost, m->uPort, &pDev->szBusId[0]);
    if (RT_LIKELY(pNew->pszAddress))
    {
        pNew->idVendor           = pDev->u16VendorId;
        pNew->idProduct          = pDev->u16ProductId;
        pNew->bcdDevice          = pDev->u16BcdDevice;
        pNew->bDeviceClass       = pDev->bDeviceClass;
        pNew->bDeviceSubClass    = pDev->bDeviceSubClass;
        pNew->bDeviceProtocol    = pDev->bDeviceProtocol;
        pNew->bNumConfigurations = pDev->bNumConfigurations;
        pNew->enmState           = USBDEVICESTATE_USED_BY_HOST_CAPTURABLE;
        pNew->u64SerialHash      = 0;
        /** @todo The following is not correct but is required to to get USB testing working
         * because only the port can be part of a filter (adding the required attributes for the bus
         * breaks API and ABI compatibility).
         * Filtering by port number is required for USB testing to connect to the correct device
         * in case there are multiple ones.
         */
        pNew->bBus               = (uint8_t)pDev->u32DevNum;
        pNew->bPort              = (uint8_t)pDev->u32BusNum;

        switch (pDev->u32Speed)
        {
            case USBIP_SPEED_LOW:
                pNew->enmSpeed = USBDEVICESPEED_LOW;
                pNew->bcdUSB = 1 << 8;
                break;
            case USBIP_SPEED_FULL:
                pNew->enmSpeed = USBDEVICESPEED_FULL;
                pNew->bcdUSB = 1 << 8;
                break;
            case USBIP_SPEED_HIGH:
                pNew->enmSpeed = USBDEVICESPEED_HIGH;
                pNew->bcdUSB = 2 << 8;
                break;
            case USBIP_SPEED_WIRELESS:
                pNew->enmSpeed = USBDEVICESPEED_VARIABLE;
                pNew->bcdUSB = 1 << 8;
                break;
            case USBIP_SPEED_SUPER:
                pNew->enmSpeed = USBDEVICESPEED_SUPER;
                pNew->bcdUSB = 3 << 8;
                break;
            case USBIP_SPEED_UNKNOWN:
            default:
                pNew->bcdUSB = 1 << 8;
                pNew->enmSpeed = USBDEVICESPEED_UNKNOWN;
        }

        /* link it */
        pNew->pNext = NULL;
        pNew->pPrev = *m->ppNext;
        *m->ppNext = pNew;
        m->ppNext = &pNew->pNext;
        m->cDevicesCur++;
    }
    else
        vrc = VERR_NO_STR_MEMORY;

    if (RT_FAILURE(vrc))
    {
        if (pNew->pszManufacturer)
            RTStrFree((char *)pNew->pszManufacturer);
        if (pNew->pszProduct)
            RTStrFree((char *)pNew->pszProduct);
        if (pNew->pszBackend)
            RTStrFree((char *)pNew->pszBackend);
        if (pNew->pszAddress)
            RTStrFree((char *)pNew->pszAddress);
        RTMemFree(pNew);
    }

    return vrc;
}

/**
 * Compares the given device list with the current one and returns whether it has
 * changed.
 *
 * @returns flag whether the device list has changed compared to the current one.
 * @param   pDevices    The device list to compare the current one against.
 */
bool USBProxyBackendUsbIp::hasDevListChanged(PUSBDEVICE pDevices)
{
    /** @todo */
    NOREF(pDevices);
    return true;
}

