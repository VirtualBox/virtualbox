/* $Id: VBoxVNC.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxVNC - VNC VRDE module.
 */

/*
 * Contributed by Ivo Smits <Ivo@UFO-Net.nl>, Howard Su and
 * Christophe Devriese <christophe.devriese@gmail.com>.
 *
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
#define LOG_GROUP LOG_GROUP_VRDE
#include <VBox/log.h>

#include <iprt/asm.h>
#include <iprt/alloca.h>
#include <iprt/ldr.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/mem.h>
#include <iprt/socket.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/cpp/utils.h>

#include <iprt/errcore.h>
#include <VBox/RemoteDesktop/VRDEOrders.h>
#include <VBox/RemoteDesktop/VRDE.h>

#include <rfb/rfb.h>

#ifdef LIBVNCSERVER_IPv6
// enable manually!
// #define VBOX_USE_IPV6
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define VNC_SIZEOFRGBA          4
#define VNC_PASSWORDSIZE        20
#define VNC_ADDRESSSIZE         60
#define VNC_PORTSSIZE           20
#define VNC_ADDRESS_OPTION_MAX  500


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
class VNCServerImpl
{
public:
    VNCServerImpl()
    {
        mVNCServer = NULL;
        mFrameBuffer = NULL;
        mScreenBuffer = NULL;
        mCursor = NULL;
        uClients = 0;
    }

    ~VNCServerImpl()
    {
        if (mFrameBuffer)
            RTMemFree(mFrameBuffer);
        if (mCursor)
            rfbFreeCursor(mCursor);
        RT_ZERO(szVNCPassword);
        if (mVNCServer)
            rfbScreenCleanup(mVNCServer);
    }

    int Init(const VRDEINTERFACEHDR *pCallbacks, void *pvCallback);

    VRDEINTERFACEHDR *GetInterface() { return &Entries.header; }

private:
    // VNC password
    char szVNCPassword[VNC_PASSWORDSIZE + 1];
    // the structure we pass to libvncserver
    char *apszVNCPasswordStruct[2];

    // VNC related variables
    rfbScreenInfoPtr mVNCServer;
    void *mCallback;
    rfbCursorPtr mCursor;
    VRDEFRAMEBUFFERINFO FrameInfo;
    unsigned char *mScreenBuffer;
    unsigned char *mFrameBuffer;
    uint32_t uClients;
    static enum rfbNewClientAction rfbNewClientEvent(rfbClientPtr cl);
    static void vncMouseEvent(int buttonMask, int x, int y, rfbClientPtr cl);
    static void vncKeyboardEvent(rfbBool down, rfbKeySym keySym, rfbClientPtr cl);
    static void clientGoneHook(rfbClientPtr cl);

    static uint32_t RGB2BGR(uint32_t c)
    {
        c = ((c >> 0) & 0xff) << 16 |
            ((c >> 8) & 0xff) << 8 |
            ((c >> 16) & 0xff) << 0;

        return c;
    }

    int     queryVrdeFeature(const char *pszName, char *pszValue, size_t cbValue);

    static VRDEENTRYPOINTS_4 Entries;
    VRDECALLBACKS_4 *mCallbacks;

    static DECLCALLBACK(void) VRDEDestroy(HVRDESERVER hServer);
    static DECLCALLBACK(int)  VRDEEnableConnections(HVRDESERVER hServer, bool fEnable);
    static DECLCALLBACK(void) VRDEDisconnect(HVRDESERVER hServer, uint32_t u32ClientId, bool fReconnect);
    static DECLCALLBACK(void) VRDEResize(HVRDESERVER hServer);
    static DECLCALLBACK(void) VRDEUpdate(HVRDESERVER hServer, unsigned uScreenId, void *pvUpdate,uint32_t cbUpdate);
    static DECLCALLBACK(void) VRDEColorPointer(HVRDESERVER hServer, const VRDECOLORPOINTER *pPointer);
    static DECLCALLBACK(void) VRDEHidePointer(HVRDESERVER hServer);
    static DECLCALLBACK(void) VRDEAudioSamples(HVRDESERVER hServer, const void *pvSamples, uint32_t cSamples, VRDEAUDIOFORMAT format);
    static DECLCALLBACK(void) VRDEAudioVolume(HVRDESERVER hServer, uint16_t u16Left, uint16_t u16Right);
    static DECLCALLBACK(void) VRDEUSBRequest(HVRDESERVER hServer,
        uint32_t u32ClientId,
        void *pvParm,
        uint32_t cbParm);
    static DECLCALLBACK(void) VRDEClipboard(HVRDESERVER hServer,
        uint32_t u32Function,
        uint32_t u32Format,
        void *pvData,
        uint32_t cbData,
        uint32_t *pcbActualRead);
    static DECLCALLBACK(void) VRDEQueryInfo(HVRDESERVER hServer,
        uint32_t index,
        void *pvBuffer,
        uint32_t cbBuffer,
        uint32_t *pcbOut);
    static DECLCALLBACK(void) VRDERedirect(HVRDESERVER hServer,
        uint32_t u32ClientId,
        const char *pszServer,
        const char *pszUser,
        const char *pszDomain,
        const char *pszPassword,
        uint32_t u32SessionId,
        const char *pszCookie);
    static DECLCALLBACK(void) VRDEAudioInOpen(HVRDESERVER hServer,
        void *pvCtx,
        uint32_t u32ClientId,
        VRDEAUDIOFORMAT audioFormat,
        uint32_t u32SamplesPerBlock);
    static DECLCALLBACK(void) VRDEAudioInClose(HVRDESERVER hServer,
        uint32_t u32ClientId);
};

VRDEENTRYPOINTS_4 VNCServerImpl::Entries = {
    { VRDE_INTERFACE_VERSION_3, sizeof(VRDEENTRYPOINTS_3) },
    VNCServerImpl::VRDEDestroy,
    VNCServerImpl::VRDEEnableConnections,
    VNCServerImpl::VRDEDisconnect,
    VNCServerImpl::VRDEResize,
    VNCServerImpl::VRDEUpdate,
    VNCServerImpl::VRDEColorPointer,
    VNCServerImpl::VRDEHidePointer,
    VNCServerImpl::VRDEAudioSamples,
    VNCServerImpl::VRDEAudioVolume,
    VNCServerImpl::VRDEUSBRequest,
    VNCServerImpl::VRDEClipboard,
    VNCServerImpl::VRDEQueryInfo,
    VNCServerImpl::VRDERedirect,
    VNCServerImpl::VRDEAudioInOpen,
    VNCServerImpl::VRDEAudioInClose
};


/** Destroy the server instance.
 *
 * @param hServer The server instance handle.
 *
 * @return IPRT status code.
 */
DECLCALLBACK(void) VNCServerImpl::VRDEDestroy(HVRDESERVER hServer)
{
    VNCServerImpl *instance = (VNCServerImpl *)hServer;
    rfbShutdownServer(instance->mVNCServer, TRUE);

    uint32_t port = UINT32_MAX;
    instance->mCallbacks->VRDECallbackProperty(instance->mCallback,
                                               VRDE_SP_NETWORK_BIND_PORT,
                                               &port, sizeof(port), NULL);
    return;
}


/**
 * Query a feature and store it's value in a user supplied buffer.
 *
 * @returns VBox status code.
 * @param   pszName             The feature name.
 * @param   pszValue            The value buffer.  The buffer is not touched at
 *                              all on failure.
 * @param   cbValue             The size of the output buffer.
 */
int VNCServerImpl::queryVrdeFeature(const char *pszName, char *pszValue, size_t cbValue)
{
    union
    {
        VRDEFEATURE Feat;
        uint8_t     abBuf[VNC_ADDRESS_OPTION_MAX + sizeof(VRDEFEATURE)];
    } u;

    u.Feat.u32ClientId = 0;
    int rc = RTStrCopy(u.Feat.achInfo, VNC_ADDRESS_OPTION_MAX, pszName); AssertRC(rc);
    if (RT_SUCCESS(rc))
    {
        uint32_t cbOut = 0;
        rc = mCallbacks->VRDECallbackProperty(mCallback,
                                              VRDE_QP_FEATURE,
                                              &u.Feat,
                                              VNC_ADDRESS_OPTION_MAX,
                                              &cbOut);
        if (RT_SUCCESS(rc))
        {
            size_t cbRet = strlen(u.Feat.achInfo) + 1;
            if (cbRet <= cbValue)
                memcpy(pszValue, u.Feat.achInfo, cbRet);
            else
                rc = VERR_BUFFER_OVERFLOW;
        }
    }

    return rc;
}


/** The server should start to accept clients connections.
 *
 * @param hServer The server instance handle.
 * @param fEnable Whether to enable or disable client connections.
 *                When is false, all existing clients are disconnected.
 *
 * @return IPRT status code.
 */
DECLCALLBACK(int) VNCServerImpl::VRDEEnableConnections(HVRDESERVER hServer, bool fEnable)
{
    RT_NOREF(fEnable);
    VNCServerImpl *instance = (VNCServerImpl *)hServer;

#ifdef LOG_ENABLED
    // enable logging
    rfbLogEnable(true);
#endif
    LogFlowFunc(("enter\n"));

    // At this point, VRDECallbackFramebufferQuery will not succeed.
    // Initialize VNC with 640x480 and wait for VRDEResize to get actual size.
    int dummyWidth = 640, dummyHeight = 480;

    rfbScreenInfoPtr vncServer = rfbGetScreen(0, NULL, dummyWidth, dummyHeight, 8, 3, VNC_SIZEOFRGBA);
    instance->mVNCServer = vncServer;

    VRDEFRAMEBUFFERINFO info;
    RT_ZERO(info);
    info.cWidth = dummyWidth, info.cHeight = dummyHeight;
    info.cBitsPerPixel = 24;
    info.pu8Bits = NULL;
    unsigned char *FrameBuffer = (unsigned char *)RTMemAlloc(info.cWidth * info.cHeight * VNC_SIZEOFRGBA); // RGBA
    rfbNewFramebuffer(instance->mVNCServer, (char *)FrameBuffer, info.cWidth, info.cHeight, 8, 3, VNC_SIZEOFRGBA);
    instance->mFrameBuffer = FrameBuffer;
    instance->mScreenBuffer = (unsigned char *)info.pu8Bits;
    instance->FrameInfo = info;

    vncServer->serverFormat.redShift = 16;
    vncServer->serverFormat.greenShift = 8;
    vncServer->serverFormat.blueShift = 0;
    vncServer->screenData = (void *)instance;
    vncServer->desktopName = "VBoxVNC";

#ifndef VBOX_USE_IPV6

    // get listen address
    char szAddress[VNC_ADDRESSSIZE + 1] = {0};
    uint32_t cbOut = 0;
    int rc = instance->mCallbacks->VRDECallbackProperty(instance->mCallback,
                                                    VRDE_QP_NETWORK_ADDRESS,
                                                    &szAddress, sizeof(szAddress), &cbOut);
    Assert(cbOut <= sizeof(szAddress));
    if (RT_SUCCESS(rc) && szAddress[0])
    {
        if (!rfbStringToAddr(szAddress, &vncServer->listenInterface))
            LogRel(("VNC: could not parse VNC server listen address '%s'\n", szAddress));
    }

    // get listen port
    uint32_t port = 0;
    rc = instance->mCallbacks->VRDECallbackProperty(instance->mCallback,
                                                    VRDE_QP_NETWORK_PORT,
                                                    &port, sizeof(port), &cbOut);
    Assert(cbOut <= sizeof(port));
    if (RT_SUCCESS(rc) && port != 0)
        vncServer->port = port;
    else
    {
        const char szFeatName[] = "Property/TCP/Ports";
        const uint32_t featLen = sizeof(VRDEFEATURE) + RT_MAX(VNC_PORTSSIZE, sizeof(szFeatName)) - 1;
        VRDEFEATURE *feature = (VRDEFEATURE *)RTMemTmpAlloc(featLen);
        feature->u32ClientId = 0;
        RTStrCopy(feature->achInfo, featLen - sizeof(VRDEFEATURE) + 1, szFeatName);

        cbOut = featLen;
        rc = instance->mCallbacks->VRDECallbackProperty(instance->mCallback, VRDE_QP_FEATURE, feature, featLen, &cbOut);
        Assert(cbOut <= featLen);

        if (RT_SUCCESS(rc) && feature->achInfo[0])
        {
            rc = RTStrToUInt32Ex(feature->achInfo, NULL, 0, &port);
            if (RT_FAILURE(rc) || port >= 65535)
                vncServer->autoPort = 1;
            else
                vncServer->port = port;
        }
        else
            vncServer->autoPort = 1;

        RTMemTmpFree(feature);
    }

    rfbInitServer(vncServer);

    vncServer->newClientHook = rfbNewClientEvent;
    vncServer->kbdAddEvent = vncKeyboardEvent;
    vncServer->ptrAddEvent = vncMouseEvent;

    // notify about the actually used port
    port = vncServer->port;
    instance->mCallbacks->VRDECallbackProperty(instance->mCallback,
                                               VRDE_SP_NETWORK_BIND_PORT,
                                               &port, sizeof(port), NULL);
    LogRel(("VNC: port = %u\n", port));
#else
    // with IPv6 from here
    /*

       This is the deal:

       Four new options are available:
       - VNCAddress4 -> IPv4 address to use
       - VNCPort4 -> IPv4 Port to use
       - VNCAddress6 -> IPv6 address to use
       - VNCPort6 -> IPv6 port to use

       By default we prefer IPv6 over IPv4.

       The address length can be unlimited as the interface identifier is not
       limited by any specs - if this is wrong, and someone knows the line
       and the RFC number, i'd appreciate a message :)

       THE MAXIMUM LENGTH OF THE RETURNED VALUES MUST NOT BE GREATER THAN:

                        --> VBOX_ADDRESS_OPTION_MAX <--

       which is defined at the top of this file.

       The way to determine which address to use is as follows:

        1st - get address information from VRDEProperties
            "TCP/Address"
            "TCP/Ports"

        2nd - if the address information is IPv4 get VNCAddress6 and VNCPort6
        2nd - if the address information is IPv6 get VNCAddress4 and VNCPort4
        2nd - if the address information is EMPTY and TCP/Ports returns 3389,
                check both, VNCAddress4 and VNCAddress6 as well as the ports.
                3389 is not a valid VNC port, therefore we assume it's not
                been set

                If one of the addresses is empty we assume to listen on any
                interface/address for that protocol. In other words:
                IPv4: 0.0.0.0
                IPv6: ::

        2nd - if everything is empty -> listen on all interfaces

        3rd - check if the addresses are valid hand them to libvncserver
                to open the initial sockets.

        4th - after the sockets have been opened, the used port of the
                address/protocol in TCP/Address is returned.
                if TCP/Address is empty, prefer IPv6

     */

    /* ok, now first get the address from VRDE/TCP/Address.

    */
    // this should be put somewhere else
    char szIPv6ListenAll[] = "::";
    char szIPv4ListenAll[] = "0.0.0.0";

    uint32_t uServerPort4 = 0;
    uint32_t uServerPort6 = 0;
    uint32_t cbOut = 0;
    size_t resSize = 0;
    RTNETADDRTYPE enmAddrType;
    char *pszVNCAddress6 = NULL;
    char *pszVNCPort6 = NULL;
    char *pszServerAddress4 = NULL;
    char *pszServerAddress6 = NULL;
    char *pszGetAddrInfo4 = NULL; // used to store the result of RTSocketQueryAddressStr()
    char *pszGetAddrInfo6 = NULL; // used to store the result of RTSocketQueryAddressStr()

    // get address
    char *pszTCPAddress = (char *)RTMemTmpAllocZ(VNC_ADDRESS_OPTION_MAX);
    rc = instance->mCallbacks->VRDECallbackProperty(instance->mCallback,
                                                    VRDE_QP_NETWORK_ADDRESS,
                                                    pszTCPAddress,
                                                    VNC_ADDRESS_OPTION_MAX,
                                                    &cbOut);

    // get port (range)
    char *pszTCPPort = (char *)RTMemTmpAllocZ(VNC_ADDRESS_OPTION_MAX);
    rc = instance->mCallbacks->VRDECallbackProperty(instance->mCallback,
                                                    VRDE_QP_NETWORK_PORT_RANGE,
                                                    pszTCPPort,
                                                    VNC_ADDRESS_OPTION_MAX,
                                                    &cbOut);
    Assert(cbOut < VNC_ADDRESS_OPTION_MAX);

    // get tcp ports option from vrde.
    /** @todo r=bird: Is this intentionally overriding VRDE_QP_NETWORK_PORT_RANGE? */
    instance->queryVrdeFeature("Property/TCP/Ports", pszTCPPort, VNC_ADDRESS_OPTION_MAX);

    // get VNCAddress4
    char *pszVNCAddress4 = (char *)RTMemTmpAllocZ(24);
    instance->queryVrdeFeature("Property/VNCAddress4", pszVNCAddress4, 24);

    // VNCPort4
    char *pszVNCPort4 = (char *)RTMemTmpAlloc(6);
    instance->queryVrdeFeature("Property/VNCPort4", pszVNCPort4, 6);

    // VNCAddress6
    pszVNCAddress6 = (char *) RTMemTmpAllocZ(VNC_ADDRESS_OPTION_MAX);
    instance->queryVrdeFeature("Property/VNCAddress6", pszVNCAddress6, VNC_ADDRESS_OPTION_MAX);

    // VNCPort6
    pszVNCPort6 = (char *)RTMemTmpAllocZ(6);
    instance->queryVrdeFeature("Property/VNCPort6", pszVNCPort6, 6);


    if (RTNetIsIPv4AddrStr(pszTCPAddress))
    {
        pszServerAddress4 = pszTCPAddress;

        if (strlen(pszTCPPort) > 0)
        {
            rc = RTStrToUInt32Ex(pszTCPPort, NULL, 10, &uServerPort4);
            if (!RT_SUCCESS(rc) || uServerPort4 > 65535)
                uServerPort4 = 0;
        }

        if (RTNetIsIPv6AddrStr(pszVNCAddress6))
            pszServerAddress6 = pszVNCAddress6;
        else
            pszServerAddress6 = szIPv6ListenAll;

        if (strlen(pszVNCPort6) > 0)
        {
            rc = RTStrToUInt32Ex(pszVNCPort6, NULL, 10, &uServerPort6);
            if (!RT_SUCCESS(rc) || uServerPort6 > 65535)
                uServerPort6 = 0;

        }

    }

    if (RTNetIsIPv6AddrStr(pszTCPAddress))
    {
        pszServerAddress6 = pszTCPAddress;

        if (strlen(pszTCPPort) > 0)
        {
            rc = RTStrToUInt32Ex(pszTCPPort, NULL, 10, &uServerPort6);
            if (!RT_SUCCESS(rc) || uServerPort6 > 65535)
                uServerPort6 = 0;
        }

        if (RTNetIsIPv4AddrStr(pszVNCAddress4))
            pszServerAddress4 = pszVNCAddress4;
        else
            pszServerAddress4 = szIPv4ListenAll;

        if (strlen(pszVNCPort4) > 0)
        {
            rc = RTStrToUInt32Ex(pszVNCPort4, NULL, 10, &uServerPort4);
            if (!RT_SUCCESS(rc) || uServerPort4 > 65535)
                uServerPort4 = 0;

        }
    }

    if ((pszServerAddress4 != pszTCPAddress) && (pszServerAddress6 != pszTCPAddress) && (strlen(pszTCPAddress) > 0))
    {
        // here we go, we prefer IPv6 over IPv4;
        resSize = 42;
        pszGetAddrInfo6 = (char *) RTMemTmpAllocZ(resSize);
        enmAddrType = RTNETADDRTYPE_IPV6;

        rc = RTSocketQueryAddressStr(pszTCPAddress, pszGetAddrInfo6, &resSize, &enmAddrType);
        if (RT_SUCCESS(rc))
            pszServerAddress6 = pszGetAddrInfo6;
        else
        {
            RTMemTmpFree(pszGetAddrInfo6);
            pszGetAddrInfo6 = NULL;
        }

        if (!pszServerAddress6)
        {
            resSize = 16;
            pszGetAddrInfo4 = (char *) RTMemTmpAllocZ(resSize);
            enmAddrType = RTNETADDRTYPE_IPV4;

            rc = RTSocketQueryAddressStr(pszTCPAddress, pszGetAddrInfo4, &resSize, &enmAddrType);

            if (RT_SUCCESS(rc))
                pszServerAddress4 = pszGetAddrInfo4;
            else
            {
                RTMemTmpFree(pszGetAddrInfo4);
                pszGetAddrInfo4 = NULL;
            }
        }
    }

    if (!pszServerAddress4 && strlen(pszVNCAddress4) > 0)
    {
        resSize = 16;
        pszGetAddrInfo4 = (char *) RTMemTmpAllocZ(resSize);
        enmAddrType = RTNETADDRTYPE_IPV4;

        rc = RTSocketQueryAddressStr(pszVNCAddress4, pszGetAddrInfo4, &resSize, &enmAddrType);

        if (RT_SUCCESS(rc))
            pszServerAddress4 = pszGetAddrInfo4;

    }

    if (!pszServerAddress6 && strlen(pszVNCAddress6) > 0)
    {
        resSize = 42;
        pszGetAddrInfo6 = (char *) RTMemTmpAllocZ(resSize);
        enmAddrType = RTNETADDRTYPE_IPV6;

        rc = RTSocketQueryAddressStr(pszVNCAddress6, pszGetAddrInfo6, &resSize, &enmAddrType);

        if (RT_SUCCESS(rc))
            pszServerAddress6 = pszGetAddrInfo6;

    }
    if (!pszServerAddress4)
    {
        if (RTNetIsIPv4AddrStr(pszVNCAddress4))
            pszServerAddress4 = pszVNCAddress4;
        else
            pszServerAddress4 = szIPv4ListenAll;
    }
    if (!pszServerAddress6)
    {
        if (RTNetIsIPv6AddrStr(pszVNCAddress6))
            pszServerAddress6 = pszVNCAddress6;
        else
            pszServerAddress6 = szIPv6ListenAll;
    }

    if (pszVNCPort4 && uServerPort4 == 0)
    {
        rc = RTStrToUInt32Ex(pszVNCPort4, NULL, 10, &uServerPort4);
        if (!RT_SUCCESS(rc) || uServerPort4 > 65535)
            uServerPort4 = 0;
    }

    if (pszVNCPort6 && uServerPort6 == 0)
    {
        rc = RTStrToUInt32Ex(pszVNCPort6, NULL, 10, &uServerPort6);
        if (!RT_SUCCESS(rc) || uServerPort6 > 65535)
            uServerPort6 = 0;
    }

    if (uServerPort4 == 0 || uServerPort6 == 0)
        vncServer->autoPort = 1;
    else
    {
        vncServer->port = uServerPort4;
        vncServer->ipv6port = uServerPort6;
    }

    if (!rfbStringToAddr(pszServerAddress4,&vncServer->listenInterface))
        LogRel(("VNC: could not parse VNC server listen address IPv4 '%s'\n", pszServerAddress4));

    vncServer->listen6Interface = pszServerAddress6;

    rfbInitServer(vncServer);

    vncServer->newClientHook = rfbNewClientEvent;
    vncServer->kbdAddEvent = vncKeyboardEvent;
    vncServer->ptrAddEvent = vncMouseEvent;

    // notify about the actually used port
    int port = 0;
    port = vncServer->ipv6port;

    if (vncServer->listen6Sock < 0)
    {
        LogRel(("VNC: not able to bind to IPv6 socket with address '%s'\n",pszServerAddress6));
        port = 0;

    }

    instance->mCallbacks->VRDECallbackProperty(instance->mCallback,
                                               VRDE_SP_NETWORK_BIND_PORT,
                                               &port, sizeof(port), NULL);
    LogRel(("VNC: port6 = %u\n", port));


    if (pszTCPAddress)
    {
        if (pszTCPAddress == pszServerAddress4)
            pszServerAddress4 = NULL;

        if (pszTCPAddress == pszServerAddress6)
            pszServerAddress6 = NULL;

        RTMemTmpFree(pszTCPAddress);
    }

    RTMemTmpFree(pszTCPPort);
    RTMemTmpFree(pszVNCAddress4);
    RTMemTmpFree(pszVNCPort4);
    RTMemTmpFree(pszGetAddrInfo4);
    RTMemTmpFree(pszVNCAddress6);
    RTMemTmpFree(pszGetAddrInfo6);

    // with ipv6 to here
#endif
    // let's get the password
    instance->szVNCPassword[0] = '\0';
    const char szFeatName[] = "Property/VNCPassword";
    const uint32_t featLen = sizeof(VRDEFEATURE) + RT_MAX(sizeof(instance->szVNCPassword), sizeof(szFeatName)) - 1;
    VRDEFEATURE *feature = (VRDEFEATURE *)RTMemTmpAlloc(featLen);
    feature->u32ClientId = 0;
    RTStrCopy(feature->achInfo, featLen - sizeof(VRDEFEATURE) + 1, szFeatName);

    cbOut = featLen;
    rc = instance->mCallbacks->VRDECallbackProperty(instance->mCallback, VRDE_QP_FEATURE, feature, featLen, &cbOut);
    Assert(cbOut <= featLen);

    if (RT_SUCCESS(rc))
    {
        RTStrCopy(instance->szVNCPassword, sizeof(instance->szVNCPassword), feature->achInfo);
        memset(feature->achInfo, '\0', featLen - sizeof(VRDEFEATURE) + 1);
        LogRel(("VNC: Configuring password\n"));

        instance->apszVNCPasswordStruct[0] = instance->szVNCPassword;
        instance->apszVNCPasswordStruct[1] = NULL;

        vncServer->authPasswdData = (void *)instance->apszVNCPasswordStruct;
        vncServer->passwordCheck = rfbCheckPasswordByList;
    }
    else
        LogRel(("VNC: No password result = %Rrc\n", rc));

    RTMemTmpFree(feature);

    rfbRunEventLoop(vncServer, -1, TRUE);

    return VINF_SUCCESS;
}

/** The server should disconnect the client.
 *
 * @param hServer     The server instance handle.
 * @param u32ClientId The client identifier.
 * @param fReconnect  Whether to send a "REDIRECT to the same server" packet to the
 *                    client before disconnecting.
 *
 * @return IPRT status code.
 */
DECLCALLBACK(void) VNCServerImpl::VRDEDisconnect(HVRDESERVER hServer, uint32_t u32ClientId, bool fReconnect)
{
    RT_NOREF(hServer, u32ClientId, fReconnect);
}

static inline void convert15To32bpp(uint8_t msb, uint8_t lsb, uint8_t &r, uint8_t &g, uint8_t &b)
{
    uint16_t px = lsb << 8 | msb;
    // RGB 555 (1 bit unused)
    r = (px >> 7) & 0xf8;
    g = (px >> 2) & 0xf8;
    b = (px << 3) & 0xf8;
}

static inline void convert16To32bpp(uint8_t msb, uint8_t lsb, uint8_t &r, uint8_t &g, uint8_t &b)
{
    uint16_t px = lsb << 8 | msb;
    // RGB 565 (all bits used, 1 extra bit for green)
    r = (px >> 8) & 0xf8;
    g = (px >> 3) & 0xfc;
    b = (px << 3) & 0xf8;
}

/**
 * Inform the server that the display was resized.
 * The server will query information about display
 * from the application via callbacks.
 *
 * @param hServer Handle of VRDE server instance.
 */
DECLCALLBACK(void) VNCServerImpl::VRDEResize(HVRDESERVER hServer)
{
    VNCServerImpl *instance = (VNCServerImpl *)hServer;
    VRDEFRAMEBUFFERINFO info;
    bool fAvail = instance->mCallbacks->VRDECallbackFramebufferQuery(instance->mCallback, 0, &info);
    if (!fAvail)
        return;

    LogRel(("VNCServerImpl::VRDEResize to %dx%dx%dbpp\n", info.cWidth, info.cHeight, info.cBitsPerPixel));

    // we always alloc an RGBA buffer
    unsigned char *FrameBuffer = (unsigned char *)RTMemAlloc(info.cWidth * info.cHeight * VNC_SIZEOFRGBA); // RGBA
    if (info.cBitsPerPixel == 32 || info.cBitsPerPixel == 24)
    {
        // Convert RGB (windows/vbox) to BGR(vnc)
        uint32_t i, j;
        for (i = 0, j = 0; i < info.cWidth * info.cHeight * VNC_SIZEOFRGBA; i += VNC_SIZEOFRGBA, j += info.cBitsPerPixel / 8)
        {
            unsigned char r = info.pu8Bits[j];
            unsigned char g = info.pu8Bits[j + 1];
            unsigned char b = info.pu8Bits[j + 2];
            FrameBuffer[i]     = b;
            FrameBuffer[i + 1] = g;
            FrameBuffer[i + 2] = r;
        }
    }
    else if (info.cBitsPerPixel == 16)
    {
        uint32_t i, j;
        for (i = 0, j = 0;
             i < info.cWidth * info.cHeight * VNC_SIZEOFRGBA;
             i += VNC_SIZEOFRGBA, j += info.cBitsPerPixel / 8)
        {
            convert16To32bpp(info.pu8Bits[j],
                             info.pu8Bits[j + 1],
                             FrameBuffer[i],
                             FrameBuffer[i + 1],
                             FrameBuffer[i + 2]);
        }
    }
    rfbNewFramebuffer(instance->mVNCServer, (char *)FrameBuffer, info.cWidth, info.cHeight, 8, 3, VNC_SIZEOFRGBA);

    void *temp = instance->mFrameBuffer;
    instance->mFrameBuffer = FrameBuffer;
    instance->mScreenBuffer = (unsigned char *)info.pu8Bits;
    instance->FrameInfo = info;
    if (temp)
        RTMemFree(temp);
}

/**
 * Send a update.
 *
 * @param hServer   Handle of VRDE server instance.
 * @param uScreenId The screen index.
 * @param pvUpdate  Pointer to VBoxGuest.h::VRDEORDERHDR structure with extra data.
 * @param cbUpdate  Size of the update data.
 */
DECLCALLBACK(void) VNCServerImpl::VRDEUpdate(HVRDESERVER hServer, unsigned uScreenId, void *pvUpdate,uint32_t cbUpdate)
{
    RT_NOREF(uScreenId);
    char *ptr = (char *)pvUpdate;
    VNCServerImpl *instance = (VNCServerImpl *)hServer;
    VRDEORDERHDR *order = (VRDEORDERHDR *)ptr;
    ptr += sizeof(VRDEORDERHDR);
    if (order == NULL)
    {
        /* Inform the VRDE server that the current display update sequence is
         * completed. At this moment the framebuffer memory contains a definite
         * image, that is synchronized with the orders already sent to VRDE client.
         * The server can now process redraw requests from clients or initial
         * fullscreen updates for new clients.
         */

    }
    else
    {
        if (sizeof(VRDEORDERHDR) != cbUpdate)
        {
            VRDEORDERCODE *code = (VRDEORDERCODE *)ptr;
            ptr += sizeof(VRDEORDERCODE);

            switch(code->u32Code)
            {
            case VRDE_ORDER_SOLIDRECT:
                {
                    VRDEORDERSOLIDRECT *solidrect = (VRDEORDERSOLIDRECT *)ptr;
                    rfbFillRect(instance->mVNCServer, solidrect->x, solidrect->y,
                        solidrect->x + solidrect->w, solidrect->y + solidrect->h, RGB2BGR(solidrect->rgb));
                    return;
                }
            /// @todo more orders
            }
        }

        if (!instance->mScreenBuffer)
        {
            VRDEResize(hServer);
            if (!instance->mScreenBuffer)
            {
                LogRel(("VNCServerImpl::VRDEUpdate: Cannot get frame buffer"));
                return;
            }
        }

        uint32_t width = instance->FrameInfo.cWidth;
        uint32_t bpp = instance->FrameInfo.cBitsPerPixel / 8;
        uint32_t joff = order->y * width + order->x;
        uint32_t srcx, srcy, destx, desty;
        if (instance->FrameInfo.cBitsPerPixel == 32 || instance->FrameInfo.cBitsPerPixel == 24)
        {
            for (srcy = joff * bpp, desty = joff * VNC_SIZEOFRGBA;
                 desty < (joff + order->h * width) * VNC_SIZEOFRGBA;
                 srcy += width * bpp, desty += width * VNC_SIZEOFRGBA)
            {
                // RGB to BGR
                for (srcx = srcy, destx = desty;
                     destx < desty + order->w * VNC_SIZEOFRGBA;
                     srcx += bpp, destx += VNC_SIZEOFRGBA)
                {
                    instance->mFrameBuffer[destx]     = instance->mScreenBuffer[srcx + 2];
                    instance->mFrameBuffer[destx + 1] = instance->mScreenBuffer[srcx + 1];
                    instance->mFrameBuffer[destx + 2] = instance->mScreenBuffer[srcx];
                }
            }
        }
        else if (instance->FrameInfo.cBitsPerPixel == 16)
        {
            for (srcy = joff * bpp, desty = joff * VNC_SIZEOFRGBA;
                 desty < (joff + order->h * width) * VNC_SIZEOFRGBA;
                 srcy += width * bpp, desty += width * VNC_SIZEOFRGBA)
            {
                for (srcx = srcy, destx = desty;
                     destx < desty + order->w * VNC_SIZEOFRGBA;
                     srcx += bpp, destx += VNC_SIZEOFRGBA)
                {
                    convert16To32bpp(instance->mScreenBuffer[srcx],
                                     instance->mScreenBuffer[srcx + 1],
                                     instance->mFrameBuffer[destx],
                                     instance->mFrameBuffer[destx + 1],
                                     instance->mFrameBuffer[destx + 2]);
                }
            }
        }
        rfbMarkRectAsModified(instance->mVNCServer, order->x, order->y, order->x+order->w, order->y+order->h);
    }
}


/**
 * Set the mouse pointer shape.
 *
 * @param hServer  Handle of VRDE server instance.
 * @param pPointer The pointer shape information.
 */
DECLCALLBACK(void) VNCServerImpl::VRDEColorPointer(HVRDESERVER hServer,
                                                   const VRDECOLORPOINTER *pPointer)
{
    VNCServerImpl *instance = (VNCServerImpl *)hServer;
    rfbCursorPtr cursor = (rfbCursorPtr)calloc(sizeof(rfbCursor), 1);

    cursor->width = pPointer->u16Width;
    cursor->height = pPointer->u16Height;

    unsigned char *mem = (unsigned char *)malloc(pPointer->u16Width * pPointer->u16Height * VNC_SIZEOFRGBA);
    cursor->richSource = mem;

    unsigned char *maskmem = (unsigned char *)malloc(pPointer->u16Width * pPointer->u16Height);
    cursor->mask = maskmem;

    unsigned char *mask = (unsigned char *)pPointer + sizeof(VRDECOLORPOINTER);

    for(int i = pPointer->u16Height - 1; i >= 0 ; i--)
    {
        for(uint16_t j = 0; j < pPointer->u16Width/8; j ++)
        {
            *maskmem = ~(*(mask + i * (pPointer->u16Width / 8) + j));
            *maskmem++;
        }
    }
    unsigned char *color = (unsigned char *)pPointer + sizeof(VRDECOLORPOINTER) + pPointer->u16MaskLen;
    for(int i = pPointer->u16Height - 1; i >= 0 ; i--)
    {
        for(uint16_t j = 0; j < pPointer->u16Width; j ++)
        {
            // put the color value;
            *(mem++) = *(color + (i * pPointer->u16Width *3 + j * 3 + 2));
            *(mem++) = *(color + (i * pPointer->u16Width *3 + j * 3 + 1));
            *(mem++) = *(color + (i * pPointer->u16Width *3 + j * 3));
            *(mem++) = 0xff;
        }
    }

    cursor->xhot = pPointer->u16HotX;
    cursor->yhot = pPointer->u16HotY;

    rfbSetCursor(instance->mVNCServer, cursor);

    if (instance->mCursor)
        rfbFreeCursor(instance->mCursor);

    instance->mCursor = cursor;
}

/**
 * Hide the mouse pointer.
 *
 * @param hServer Handle of VRDE server instance.
 */
DECLCALLBACK(void) VNCServerImpl::VRDEHidePointer(HVRDESERVER hServer)
{
    VNCServerImpl *pInstance = (VNCServerImpl *)hServer;
    RT_NOREF(pInstance);

    /// @todo what's behavior for this. hide doesn't seems right
    //rfbSetCursor(pInstance->mVNCServer, NULL);
}

/**
 * Queues the samples to be sent to clients.
 *
 * @param hServer    Handle of VRDE server instance.
 * @param pvSamples  Address of samples to be sent.
 * @param cSamples   Number of samples.
 * @param format     Encoded audio format for these samples.
 *
 * @note Initialized to NULL when the application audio callbacks are NULL.
 */
DECLCALLBACK(void) VNCServerImpl::VRDEAudioSamples(HVRDESERVER hServer,
                                                   const void *pvSamples,
                                                   uint32_t cSamples,
                                                   VRDEAUDIOFORMAT format)
{
    RT_NOREF(hServer, pvSamples, cSamples, format);
}

/**
 * Sets the sound volume on clients.
 *
 * @param hServer    Handle of VRDE server instance.
 * @param left       0..0xFFFF volume level for left channel.
 * @param right      0..0xFFFF volume level for right channel.
 *
 * @note Initialized to NULL when the application audio callbacks are NULL.
 */
DECLCALLBACK(void) VNCServerImpl::VRDEAudioVolume(HVRDESERVER hServer,
                                                  uint16_t u16Left,
                                                  uint16_t u16Right)
{
    RT_NOREF(hServer, u16Left, u16Right);
}

/**
 * Sends a USB request.
 *
 * @param hServer      Handle of VRDE server instance.
 * @param u32ClientId  An identifier that allows the server to find the corresponding client.
 *                     The identifier is always passed by the server as a parameter
 *                     of the FNVRDEUSBCALLBACK. Note that the value is the same as
 *                     in the VRDESERVERCALLBACK functions.
 * @param pvParm       Function specific parameters buffer.
 * @param cbParm       Size of the buffer.
 *
 * @note Initialized to NULL when the application USB callbacks are NULL.
 */
DECLCALLBACK(void) VNCServerImpl::VRDEUSBRequest(HVRDESERVER hServer,
                                                 uint32_t u32ClientId,
                                                 void *pvParm,
                                                 uint32_t cbParm)
{
    RT_NOREF(hServer, u32ClientId, pvParm, cbParm);
}

/**
 * Called by the application when (VRDE_CLIPBOARD_FUNCTION_*):
 *   - (0) guest announces available clipboard formats;
 *   - (1) guest requests clipboard data;
 *   - (2) guest responds to the client's request for clipboard data.
 *
 * @param hServer     The VRDE server handle.
 * @param u32Function The cause of the call.
 * @param u32Format   Bitmask of announced formats or the format of data.
 * @param pvData      Points to: (1) buffer to be filled with clients data;
 *                               (2) data from the host.
 * @param cbData      Size of 'pvData' buffer in bytes.
 * @param pcbActualRead Size of the copied data in bytes.
 *
 * @note Initialized to NULL when the application clipboard callbacks are NULL.
 */
DECLCALLBACK(void) VNCServerImpl::VRDEClipboard(HVRDESERVER hServer,
                                                uint32_t u32Function,
                                                uint32_t u32Format,
                                                void *pvData,
                                                uint32_t cbData,
                                                uint32_t *pcbActualRead)
{
    RT_NOREF(hServer, u32Function, u32Format, pvData, cbData, pcbActualRead);
}

/**
 * Query various information from the VRDE server.
 *
 * @param hServer   The VRDE server handle.
 * @param index     VRDE_QI_* identifier of information to be returned.
 * @param pvBuffer  Address of memory buffer to which the information must be written.
 * @param cbBuffer  Size of the memory buffer in bytes.
 * @param pcbOut    Size in bytes of returned information value.
 *
 * @remark The caller must check the *pcbOut. 0 there means no information was returned.
 *         A value greater than cbBuffer means that information is too big to fit in the
 *         buffer, in that case no information was placed to the buffer.
 */
DECLCALLBACK(void) VNCServerImpl::VRDEQueryInfo(HVRDESERVER hServer,
                                                uint32_t index,
                                                void *pvBuffer,
                                                uint32_t cbBuffer,
                                                uint32_t *pcbOut)
{
    VNCServerImpl *instance = (VNCServerImpl *)hServer;
    *pcbOut = 0;

    switch (index)
    {
        case VRDE_QI_ACTIVE:    /* # of active clients */
        case VRDE_QI_NUMBER_OF_CLIENTS: /* # of connected clients */
        {
            uint32_t cbOut = sizeof(uint32_t);
            if (cbBuffer >= cbOut)
            {
                *pcbOut = cbOut;
                *(uint32_t *)pvBuffer = instance->uClients;
            }
            break;
        }
        /// @todo lots more queries to implement
        default:
            break;
    }
}


/**
 * The server should redirect the client to the specified server.
 *
 * @param hServer       The server instance handle.
 * @param u32ClientId   The client identifier.
 * @param pszServer     The server to redirect the client to.
 * @param pszUser       The username to use for the redirection.
 *                      Can be NULL.
 * @param pszDomain     The domain. Can be NULL.
 * @param pszPassword   The password. Can be NULL.
 * @param u32SessionId  The ID of the session to redirect to.
 * @param pszCookie     The routing token used by a load balancer to
 *                      route the redirection. Can be NULL.
 */
DECLCALLBACK(void) VNCServerImpl::VRDERedirect(HVRDESERVER hServer,
                                               uint32_t u32ClientId,
                                               const char *pszServer,
                                               const char *pszUser,
                                               const char *pszDomain,
                                               const char *pszPassword,
                                               uint32_t u32SessionId,
                                               const char *pszCookie)
{
    RT_NOREF(hServer, u32ClientId, pszServer, pszUser, pszDomain, pszPassword, u32SessionId, pszCookie);
}

/**
 * Audio input open request.
 *
 * @param hServer      Handle of VRDE server instance.
 * @param pvCtx        To be used in VRDECallbackAudioIn.
 * @param u32ClientId  An identifier that allows the server to find the corresponding client.
 * @param audioFormat  Preferred format of audio data.
 * @param u32SamplesPerBlock Preferred number of samples in one block of audio input data.
 *
 * @note Initialized to NULL when the VRDECallbackAudioIn callback is NULL.
 */
DECLCALLBACK(void) VNCServerImpl::VRDEAudioInOpen(HVRDESERVER hServer,
                                                  void *pvCtx,
                                                  uint32_t u32ClientId,
                                                  VRDEAUDIOFORMAT audioFormat,
                                                  uint32_t u32SamplesPerBlock)
{
    RT_NOREF(hServer, pvCtx, u32ClientId, audioFormat, u32SamplesPerBlock);
}

/**
 * Audio input close request.
 *
 * @param hServer      Handle of VRDE server instance.
 * @param u32ClientId  An identifier that allows the server to find the corresponding client.
 *
 * @note Initialized to NULL when the VRDECallbackAudioIn callback is NULL.
 */
DECLCALLBACK(void) VNCServerImpl::VRDEAudioInClose(HVRDESERVER hServer, uint32_t u32ClientId)
{
    RT_NOREF(hServer, u32ClientId);
}



int VNCServerImpl::Init(const VRDEINTERFACEHDR *pCallbacks,
                        void *pvCallback)
{
    if (pCallbacks->u64Version == VRDE_INTERFACE_VERSION_3)
    {
        mCallbacks = (VRDECALLBACKS_4 *)pCallbacks;
        mCallback = pvCallback;
    }
    else if (pCallbacks->u64Version == VRDE_INTERFACE_VERSION_1)
    {
        /// @todo this is incorrect and it will cause crash if client call unsupport func.
        mCallbacks = (VRDECALLBACKS_4 *)pCallbacks;
        mCallback = pvCallback;


        // since they are same in order, let's just change header
        Entries.header.u64Version = VRDE_INTERFACE_VERSION_1;
        Entries.header.u64Size = sizeof(VRDEENTRYPOINTS_1);
    }
    else
        return VERR_VERSION_MISMATCH;

    return VINF_SUCCESS;
}


void VNCServerImpl::vncKeyboardEvent(rfbBool down, rfbKeySym keycode, rfbClientPtr cl)
{
    VNCServerImpl *instance = static_cast<VNCServerImpl *>(cl->screen->screenData);
    VRDEINPUTSCANCODE point;

    /* Conversion table for key code range 32-127 (which happen to equal the ASCII codes).
     * Values 0xe0?? indicate that a 0xe0 scancode will be sent first (extended keys), then code ?? is sent */
    static unsigned codes_low[] =
    {
        // Conversion table for VNC key code range 32-127
        0x39, 0x02, 0x28, 0x04, 0x05, 0x06, 0x08, 0x28, 0x0a, 0x0b, 0x09, 0x0d, 0x33, 0x0c, 0x34, 0x35, //space, !"#$%&'()*+`-./
        0x0b, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x27, 0x27, 0x33, 0x0d, 0x34, 0x35, 0x03, //0123456789:;<=>?@
        0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, //A-M
        0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c, //N-Z
        0x1a, 0x2b, 0x1b, 0x07, 0x0c, 0x29, //[\]^_`
        0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, //a-m
        0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c, //n-z
        0x1a, 0x2b, 0x1b, 0x29 //{|}~
    };

    int code = -1;
    if (keycode < 32)
    {
        //ASCII control codes.. unused..
    }
    else if (keycode < 127)
    {
        //DEL is in high area
        code = codes_low[keycode - 32];
    }
    else if ((keycode & 0xFE00) != 0xFE00)
    {
    }
    else
    {
        switch(keycode)
        {
        case 65027: code = 0xe038; break; //AltGr = RAlt
        case 65288: code =   0x0e; break; //Backspace
        case 65289: code =   0x0f; break; //Tab

        case 65293: code =   0x1c; break; //Return
            //case 65299: break; Pause/break
        case 65300: code =   0x46; break; //ScrollLock
            //case 65301: break; SysRq
        case 65307: code =   0x01; break; //Escape

        case 65360: code = 0xe047; break; //Home
        case 65361: code = 0xe04b; break; //Left
        case 65362: code = 0xe048; break; //Up
        case 65363: code = 0xe04d; break; //Right
        case 65364: code = 0xe050; break; //Down
        case 65365: code = 0xe049; break; //Page up
        case 65366: code = 0xe051; break; //Page down
        case 65367: code = 0xe04f; break; //End

            //case 65377: break; //Print screen
        case 65379: code = 0xe052; break; //Insert

        case 65383: code = 0xe05d; break; //Menu
        case 65407: code =   0x45; break; //NumLock

        case 65421: code = 0xe01c; break; //Numpad return
        case 65429: code =   0x47; break; //Numpad home
        case 65430: code =   0x4b; break; //Numpad left
        case 65431: code =   0x48; break; //Numpad up
        case 65432: code =   0x4d; break; //Numpad right
        case 65433: code =   0x50; break; //Numpad down
        case 65434: code =   0x49; break; //Numpad page up
        case 65435: code =   0x51; break; //Numpad page down
        case 65436: code =   0x4f; break; //Numpad end
        case 65437: code =   0x4c; break; //Numpad begin
        case 65438: code =   0x52; break; //Numpad ins
        case 65439: code =   0x53; break; //Numpad del
        case 65450: code =   0x37; break; //Numpad *
        case 65451: code =   0x4e; break; //Numpad +
        case 65452: code =   0x53; break; //Numpad separator
        case 65453: code =   0x4a; break; //Numpad -
        case 65454: code =   0x53; break; //Numpad decimal
        case 65455: code = 0xe035; break; //Numpad /
        case 65456: code =   0x52; break; //Numpad 0
        case 65457: code =   0x4f; break; //Numpad 1
        case 65458: code =   0x50; break; //Numpad 2
        case 65459: code =   0x51; break; //Numpad 3
        case 65460: code =   0x4b; break; //Numpad 4
        case 65461: code =   0x4c; break; //Numpad 5
        case 65462: code =   0x4d; break; //Numpad 6
        case 65463: code =   0x47; break; //Numpad 7
        case 65464: code =   0x48; break; //Numpad 8
        case 65465: code =   0x49; break; //Numpad 9

        case 65470: code =   0x3b; break; //F1
        case 65471: code =   0x3c; break; //F2
        case 65472: code =   0x3d; break; //F3
        case 65473: code =   0x3e; break; //F4
        case 65474: code =   0x3f; break; //F5
        case 65475: code =   0x40; break; //F6
        case 65476: code =   0x41; break; //F7
        case 65477: code =   0x42; break; //F8
        case 65478: code =   0x43; break; //F9
        case 65479: code =   0x44; break; //F10
        case 65480: code =   0x57; break; //F11
        case 65481: code =   0x58; break; //F12

        case 65505: code =   0x2a; break; //Left shift
        case 65506: code =   0x36; break; //Right shift
        case 65507: code =   0x1d; break; //Left ctrl
        case 65508: code = 0xe01d; break; //Right ctrl
        case 65509: code =   0x3a; break; //Caps Lock
        case 65510: code =   0x3a; break; //Shift Lock
        case 65513: code =   0x38; break; //Left Alt
        case 65514: code = 0xe038; break; //Right Alt
        case 65515: code = 0xe05b; break; //Left windows key
        case 65516: code = 0xe05c; break; //Right windows key
        case 65535: code = 0xe053; break; //Delete
        }
    }

    if (code == -1)
    {
        LogRel(("VNC: unhandled keyboard code: down=%d code=%d\n", down, keycode));
        return;
    }
    if (code > 0xff)
    {
        point.uScancode = (code >> 8) & 0xff;
        instance->mCallbacks->VRDECallbackInput(instance->mCallback, VRDE_INPUT_SCANCODE, &point, sizeof(point));
    }

    point.uScancode = (code & 0xff) | (down ? 0 : 0x80);
    instance->mCallbacks->VRDECallbackInput(instance->mCallback, VRDE_INPUT_SCANCODE, &point, sizeof(point));
}

void VNCServerImpl::vncMouseEvent(int buttonMask, int x, int y, rfbClientPtr cl)
{
    VNCServerImpl *instance = static_cast<VNCServerImpl *>(cl->screen->screenData);

    VRDEINPUTPOINT point;
    unsigned button = 0;
    if (buttonMask & 1) button |= VRDE_INPUT_POINT_BUTTON1;
    if (buttonMask & 2) button |= VRDE_INPUT_POINT_BUTTON3;
    if (buttonMask & 4) button |= VRDE_INPUT_POINT_BUTTON2;
    if (buttonMask & 8) button |= VRDE_INPUT_POINT_WHEEL_UP;
    if (buttonMask & 16) button |= VRDE_INPUT_POINT_WHEEL_DOWN;
    point.uButtons = button;
    point.x = x;
    point.y = y;
    instance->mCallbacks->VRDECallbackInput(instance->mCallback, VRDE_INPUT_POINT, &point, sizeof(point));
    rfbDefaultPtrAddEvent(buttonMask, x, y, cl);
}

enum rfbNewClientAction VNCServerImpl::rfbNewClientEvent(rfbClientPtr cl)
{
    VNCServerImpl *instance = static_cast<VNCServerImpl *>(cl->screen->screenData);

    /// @todo we need auth user here

    instance->mCallbacks->VRDECallbackClientConnect(instance->mCallback, (int)cl->sock);
    instance->uClients++;

    cl->clientGoneHook = clientGoneHook;

    return RFB_CLIENT_ACCEPT;
}

void VNCServerImpl::clientGoneHook(rfbClientPtr cl)
{
    VNCServerImpl *instance = static_cast<VNCServerImpl *>(cl->screen->screenData);

    instance->uClients--;
    instance->mCallbacks->VRDECallbackClientDisconnect(instance->mCallback, (int)cl->sock, 0);
}

VNCServerImpl *g_VNCServer = 0;

DECLEXPORT(int) VRDECreateServer(const VRDEINTERFACEHDR *pCallbacks,
                                 void *pvCallback,
                                 VRDEINTERFACEHDR **ppEntryPoints,
                                 HVRDESERVER *phServer)
{
    if (!g_VNCServer)
    {
        g_VNCServer = new VNCServerImpl();
    }

    int rc = g_VNCServer->Init(pCallbacks, pvCallback);

    if (RT_SUCCESS(rc))
    {
        *ppEntryPoints = g_VNCServer->GetInterface();
        *phServer = (HVRDESERVER)g_VNCServer;
    }

    return rc;
}

static const char * const supportedProperties[] =
{
    "TCP/Ports",
    "TCP/Address",
    NULL
};

DECLEXPORT(const char * const *) VRDESupportedProperties(void)
{
    LogFlowFunc(("enter\n"));
    return supportedProperties;
}
