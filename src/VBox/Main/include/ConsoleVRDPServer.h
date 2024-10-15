/* $Id: ConsoleVRDPServer.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Console VRDE Server Helper class and implementation of IVRDEServerInfo
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

#ifndef MAIN_INCLUDED_ConsoleVRDPServer_h
#define MAIN_INCLUDED_ConsoleVRDPServer_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "VRDEServerInfoWrap.h"
#include "RemoteUSBBackend.h"
#include "HGCM.h"

#include "AuthLibrary.h"

#include <VBox/RemoteDesktop/VRDEImage.h>
#include <VBox/RemoteDesktop/VRDEMousePtr.h>
#include <VBox/RemoteDesktop/VRDESCard.h>
#include <VBox/RemoteDesktop/VRDETSMF.h>
#define VRDE_VIDEOIN_WITH_VRDEINTERFACE /* Get the VRDE interface definitions. */
#include <VBox/RemoteDesktop/VRDEVideoIn.h>
#include <VBox/RemoteDesktop/VRDEInput.h>

#include <VBox/HostServices/VBoxClipboardExt.h>
#include <VBox/HostServices/VBoxHostChannel.h>

#include "SchemaDefs.h"

// ConsoleVRDPServer
///////////////////////////////////////////////////////////////////////////////

class EmWebcam;

typedef struct _VRDPInputSynch
{
    int cGuestNumLockAdaptions;
    int cGuestCapsLockAdaptions;

    bool fGuestNumLock;
    bool fGuestCapsLock;
    bool fGuestScrollLock;

    bool fClientNumLock;
    bool fClientCapsLock;
    bool fClientScrollLock;
} VRDPInputSynch;

/* Member of Console. Helper class for VRDP server management. Not a COM class. */
class ConsoleVRDPServer
{
public:
    DECLARE_TRANSLATE_METHODS(ConsoleVRDPServer)

    ConsoleVRDPServer (Console *console);
    ~ConsoleVRDPServer ();

    int Launch (void);

    void NotifyAbsoluteMouse (bool fGuestWantsAbsolute)
    {
        m_fGuestWantsAbsolute = fGuestWantsAbsolute;
    }

    void NotifyKeyboardLedsChange (BOOL fNumLock, BOOL fCapsLock, BOOL fScrollLock)
    {
        bool fGuestNumLock    = (fNumLock != FALSE);
        bool fGuestCapsLock   = (fCapsLock != FALSE);
        bool fGuestScrollLock = (fScrollLock != FALSE);

        /* Might need to resync in case the guest itself changed the LED status. */
        if (m_InputSynch.fClientNumLock != fGuestNumLock)
        {
            m_InputSynch.cGuestNumLockAdaptions = 2;
        }

        if (m_InputSynch.fClientCapsLock != fGuestCapsLock)
        {
            m_InputSynch.cGuestCapsLockAdaptions = 2;
        }

        m_InputSynch.fGuestNumLock    = fGuestNumLock;
        m_InputSynch.fGuestCapsLock   = fGuestCapsLock;
        m_InputSynch.fGuestScrollLock = fGuestScrollLock;
    }

    void EnableConnections (void);
    void DisconnectClient (uint32_t u32ClientId, bool fReconnect);
    int MousePointer(BOOL alpha, ULONG xHot, ULONG yHot, ULONG width, ULONG height, const uint8_t *pu8Shape);
    void MousePointerUpdate (const VRDECOLORPOINTER *pPointer);
    void MousePointerHide (void);

    void Stop (void);

    AuthResult Authenticate (const Guid &uuid, AuthGuestJudgement guestJudgement,
                             const char *pszUser, const char *pszPassword, const char *pszDomain,
                             uint32_t u32ClientId);

    void AuthDisconnect (const Guid &uuid, uint32_t u32ClientId);

    void USBBackendCreate (uint32_t u32ClientId, void **ppvIntercept);
    void USBBackendDelete (uint32_t u32ClientId);

    void *USBBackendRequestPointer (uint32_t u32ClientId, const Guid *pGuid);
    void USBBackendReleasePointer (const Guid *pGuid);

    /* Private interface for the RemoteUSBBackend destructor. */
    void usbBackendRemoveFromList (RemoteUSBBackend *pRemoteUSBBackend);

    /* Private methods for the Remote USB thread. */
    RemoteUSBBackend *usbBackendGetNext (RemoteUSBBackend *pRemoteUSBBackend);

    void notifyRemoteUSBThreadRunning (RTTHREAD thread);
    bool isRemoteUSBThreadRunning (void);
    void waitRemoteUSBThreadEvent (RTMSINTERVAL cMillies);

    void ClipboardCreate (uint32_t u32ClientId);
    void ClipboardDelete (uint32_t u32ClientId);

    /*
     * Forwarders to VRDP server library.
     */
    void SendUpdate (unsigned uScreenId, void *pvUpdate, uint32_t cbUpdate) const;
    void SendResize (void);
    void SendUpdateBitmap (unsigned uScreenId, uint32_t x, uint32_t y, uint32_t w, uint32_t h) const;

    void SendAudioSamples (void const *pvSamples, uint32_t cSamples, VRDEAUDIOFORMAT format) const;
    void SendAudioVolume (uint16_t left, uint16_t right) const;
    void SendUSBRequest (uint32_t u32ClientId, void *pvParms, uint32_t cbParms) const;

    void QueryInfo (uint32_t index, void *pvBuffer, uint32_t cbBuffer, uint32_t *pcbOut) const;

    int SendAudioInputBegin(void **ppvUserCtx,
                            void *pvContext,
                            uint32_t cSamples,
                            uint32_t iSampleHz,
                            uint32_t cChannels,
                            uint32_t cBits);

    void SendAudioInputEnd(void *pvUserCtx);

    int SCardRequest(void *pvUser, uint32_t u32Function, const void *pvData, uint32_t cbData);

    int VideoInDeviceAttach(const VRDEVIDEOINDEVICEHANDLE *pDeviceHandle, void *pvDeviceCtx);
    int VideoInDeviceDetach(const VRDEVIDEOINDEVICEHANDLE *pDeviceHandle);
    int VideoInGetDeviceDesc(void *pvUser, const VRDEVIDEOINDEVICEHANDLE *pDeviceHandle);
    int VideoInControl(void *pvUser, const VRDEVIDEOINDEVICEHANDLE *pDeviceHandle,
                       const VRDEVIDEOINCTRLHDR *pReq, uint32_t cbReq);

    Console *getConsole(void) { return mConsole; }

    void onMousePointerShapeChange(BOOL visible, BOOL alpha, ULONG xHot, ULONG yHot,
                                   ULONG width, ULONG height, ComSafeArrayIn(BYTE,shape));

private:
    /* Note: This is not a ComObjPtr here, because the ConsoleVRDPServer object
     * is actually just a part of the Console.
     */
    Console *mConsole;

    HVRDESERVER mhServer;
    int mServerInterfaceVersion;

    int32_t volatile mcInResize; /* Do not Stop the server if this is not 0. */

    static int loadVRDPLibrary (const char *pszLibraryName);

    /** Static because will never load this more than once! */
    static RTLDRMOD mVRDPLibrary;

    static PFNVRDECREATESERVER mpfnVRDECreateServer;

    static VRDEENTRYPOINTS_4 mEntryPoints;
    static VRDEENTRYPOINTS_4 *mpEntryPoints;
    static VRDECALLBACKS_4 mCallbacks;

    static DECLCALLBACK(int)  VRDPCallbackQueryProperty     (void *pvCallback, uint32_t index, void *pvBuffer, uint32_t cbBuffer, uint32_t *pcbOut);
    static DECLCALLBACK(int)  VRDPCallbackClientLogon       (void *pvCallback, uint32_t u32ClientId, const char *pszUser, const char *pszPassword, const char *pszDomain);
    static DECLCALLBACK(void) VRDPCallbackClientConnect     (void *pvCallback, uint32_t u32ClientId);
    static DECLCALLBACK(void) VRDPCallbackClientDisconnect  (void *pvCallback, uint32_t u32ClientId, uint32_t fu32Intercepted);
    static DECLCALLBACK(int)  VRDPCallbackIntercept         (void *pvCallback, uint32_t u32ClientId, uint32_t fu32Intercept, void **ppvIntercept);
    static DECLCALLBACK(int)  VRDPCallbackUSB               (void *pvCallback, void *pvIntercept, uint32_t u32ClientId, uint8_t u8Code, const void *pvRet, uint32_t cbRet);
    static DECLCALLBACK(int)  VRDPCallbackClipboard         (void *pvCallback, void *pvIntercept, uint32_t u32ClientId, uint32_t u32Function, uint32_t u32Format, const void *pvData, uint32_t cbData);
    static DECLCALLBACK(bool) VRDPCallbackFramebufferQuery  (void *pvCallback, unsigned uScreenId, VRDEFRAMEBUFFERINFO *pInfo);
    static DECLCALLBACK(void) VRDPCallbackFramebufferLock   (void *pvCallback, unsigned uScreenId);
    static DECLCALLBACK(void) VRDPCallbackFramebufferUnlock (void *pvCallback, unsigned uScreenId);
    static DECLCALLBACK(void) VRDPCallbackInput             (void *pvCallback, int type, const void *pvInput, unsigned cbInput);
    static DECLCALLBACK(void) VRDPCallbackVideoModeHint     (void *pvCallback, unsigned cWidth, unsigned cHeight,  unsigned cBitsPerPixel, unsigned uScreenId);
    static DECLCALLBACK(void) VRDECallbackAudioIn           (void *pvCallback, void *pvCtx, uint32_t u32ClientId, uint32_t u32Event, const void *pvData, uint32_t cbData);

    void fetchCurrentState(void);

    bool m_fGuestWantsAbsolute;
    int m_mousex;
    int m_mousey;

    ComPtr<IDisplaySourceBitmap> maSourceBitmaps[SchemaDefs::MaxGuestMonitors];

    ComPtr<IEventListener> mConsoleListener;

    VRDPInputSynch m_InputSynch;

    int32_t mVRDPBindPort;

    RTCRITSECT mCritSect;

    int lockConsoleVRDPServer (void);
    void unlockConsoleVRDPServer (void);

    int mcClipboardRefs;
    PFNSHCLEXTCALLBACK mpfnClipboardCallback;

    static DECLCALLBACK(int) ClipboardCallback (void *pvCallback, uint32_t u32ClientId, uint32_t u32Function, uint32_t u32Format, const void *pvData, uint32_t cbData);
    static DECLCALLBACK(int) ClipboardServiceExtension(void *pvExtension, uint32_t u32Function, void *pvParms, uint32_t cbParms);

#ifdef VBOX_WITH_USB
    RemoteUSBBackend *usbBackendFindByUUID (const Guid *pGuid);
    RemoteUSBBackend *usbBackendFind (uint32_t u32ClientId);

    typedef struct _USBBackends
    {
        RemoteUSBBackend *pHead;
        RemoteUSBBackend *pTail;

        RTTHREAD thread;

        bool fThreadRunning;

        RTSEMEVENT event;
    } USBBackends;

    USBBackends mUSBBackends;

    void remoteUSBThreadStart (void);
    void remoteUSBThreadStop (void);
#endif /* VBOX_WITH_USB */

#ifndef VBOX_WITH_VRDEAUTH_IN_VBOXSVC
    /* External authentication library context. The library is loaded in the
     * Authenticate method and unloaded at the object destructor.
     */
    AUTHLIBRARYCONTEXT mAuthLibCtx;
#endif

    uint32_t volatile mu32AudioInputClientId;

    int32_t volatile mcClients;

#if 0 /** @todo Chromium got removed (see @bugref{9529}) and this is not available for VMSVGA yet. */
    static DECLCALLBACK(void) H3DORBegin(const void *pvContext, void **ppvInstance,
                                         const char *pszFormat);
    static DECLCALLBACK(void) H3DORGeometry(void *pvInstance,
                                            int32_t x, int32_t y, uint32_t w, uint32_t h);
    static DECLCALLBACK(void) H3DORVisibleRegion(void *pvInstance,
                                                 uint32_t cRects, const RTRECT *paRects);
    static DECLCALLBACK(void) H3DORFrame(void *pvInstance,
                                         void *pvData, uint32_t cbData);
    static DECLCALLBACK(void) H3DOREnd(void *pvInstance);
    static DECLCALLBACK(int)  H3DORContextProperty(const void *pvContext, uint32_t index,
                                                   void *pvBuffer, uint32_t cbBuffer, uint32_t *pcbOut);
#endif

    void remote3DRedirect(bool fEnable);

    /*
     * VRDE server optional interfaces.
     */

    /* Image update interface. */
    bool m_fInterfaceImage;
    VRDEIMAGECALLBACKS m_interfaceCallbacksImage;
    VRDEIMAGEINTERFACE m_interfaceImage;
    static DECLCALLBACK(int) VRDEImageCbNotify (void *pvContext,
                                                void *pvUser,
                                                HVRDEIMAGE hVideo,
                                                uint32_t u32Id,
                                                void *pvData,
                                                uint32_t cbData);
    /* Mouse pointer interface. */
    VRDEMOUSEPTRINTERFACE m_interfaceMousePtr;

    /* Smartcard interface. */
    VRDESCARDINTERFACE m_interfaceSCard;
    VRDESCARDCALLBACKS m_interfaceCallbacksSCard;
    static DECLCALLBACK(int) VRDESCardCbNotify(void *pvContext,
                                               uint32_t u32Id,
                                               void *pvData,
                                               uint32_t cbData);
    static DECLCALLBACK(int) VRDESCardCbResponse(void *pvContext,
                                                 int vrcRequest,
                                                 void *pvUser,
                                                 uint32_t u32Function,
                                                 void *pvData,
                                                 uint32_t cbData);

    /* TSMF interface. */
    VRDETSMFINTERFACE m_interfaceTSMF;
    VRDETSMFCALLBACKS m_interfaceCallbacksTSMF;
    static DECLCALLBACK(void) VRDETSMFCbNotify(void *pvContext,
                                               uint32_t u32Notification,
                                               void *pvChannel,
                                               const void *pvParm,
                                               uint32_t cbParm);
    void setupTSMF(void);

    static DECLCALLBACK(int) tsmfHostChannelAttach(void *pvProvider, void **ppvInstance, uint32_t u32Flags,
                                                   VBOXHOSTCHANNELCALLBACKS *pCallbacks, void *pvCallbacks);
    static DECLCALLBACK(void) tsmfHostChannelDetach(void *pvInstance);
    static DECLCALLBACK(int) tsmfHostChannelSend(void *pvInstance, const void *pvData, uint32_t cbData);
    static DECLCALLBACK(int) tsmfHostChannelRecv(void *pvInstance, void *pvData, uint32_t cbData,
                                                 uint32_t *pcbReturned, uint32_t *pcbRemaining);
    static DECLCALLBACK(int) tsmfHostChannelControl(void *pvInstance, uint32_t u32Code,
                                                    const void *pvParm, uint32_t cbParm,
                                                    const void *pvData, uint32_t cbData, uint32_t *pcbDataReturned);
    int tsmfLock(void);
    void tsmfUnlock(void);
    RTCRITSECT mTSMFLock;

    /* Video input interface. */
    VRDEVIDEOININTERFACE m_interfaceVideoIn;
    VRDEVIDEOINCALLBACKS m_interfaceCallbacksVideoIn;
    static DECLCALLBACK(void) VRDECallbackVideoInNotify(void *pvCallback,
                                                        uint32_t u32Id,
                                                        const void *pvData,
                                                        uint32_t cbData);
    static DECLCALLBACK(void) VRDECallbackVideoInDeviceDesc(void *pvCallback,
                                                            int vrcRequest,
                                                            void *pDeviceCtx,
                                                            void *pvUser,
                                                            const VRDEVIDEOINDEVICEDESC *pDeviceDesc,
                                                            uint32_t cbDevice);
    static DECLCALLBACK(void) VRDECallbackVideoInControl(void *pvCallback,
                                                         int vrcRequest,
                                                         void *pDeviceCtx,
                                                         void *pvUser,
                                                         const VRDEVIDEOINCTRLHDR *pControl,
                                                         uint32_t cbControl);
    static DECLCALLBACK(void) VRDECallbackVideoInFrame(void *pvCallback,
                                                       int vrcRequest,
                                                       void *pDeviceCtx,
                                                       const VRDEVIDEOINPAYLOADHDR *pFrame,
                                                       uint32_t cbFrame);
    EmWebcam *mEmWebcam;

    /* Input interface. */
    VRDEINPUTINTERFACE m_interfaceInput;
    VRDEINPUTCALLBACKS m_interfaceCallbacksInput;
    static DECLCALLBACK(void) VRDECallbackInputSetup(void *pvCallback,
                                                     int vrcRequest,
                                                     uint32_t u32Method,
                                                     const void *pvResult,
                                                     uint32_t cbResult);
    static DECLCALLBACK(void) VRDECallbackInputEvent(void *pvCallback,
                                                     uint32_t u32Method,
                                                     const void *pvEvent,
                                                     uint32_t cbEvent);
    uint64_t mu64TouchInputTimestampMCS;
};


class Console;

class ATL_NO_VTABLE VRDEServerInfo :
    public VRDEServerInfoWrap
{
public:
    DECLARE_NOT_AGGREGATABLE(VRDEServerInfo)

    DECLARE_COMMON_CLASS_METHODS(VRDEServerInfo)

    HRESULT FinalConstruct();
    void FinalRelease();

    /* Public initializer/uninitializer for internal purposes only. */
    HRESULT init(Console *aParent);
    void uninit();

private:
    // wrapped IVRDEServerInfo properties
#define DECL_GETTER(_aType, _aName) virtual HRESULT get##_aName(_aType *a##_aName)
#define DECL_GETTER_REF(_aType, _aName) virtual HRESULT get##_aName(_aType &a##_aName)
    DECL_GETTER(BOOL, Active);
    DECL_GETTER(LONG, Port);
    DECL_GETTER(ULONG, NumberOfClients);
    DECL_GETTER(LONG64, BeginTime);
    DECL_GETTER(LONG64, EndTime);
    DECL_GETTER(LONG64, BytesSent);
    DECL_GETTER(LONG64, BytesSentTotal);
    DECL_GETTER(LONG64, BytesReceived);
    DECL_GETTER(LONG64, BytesReceivedTotal);
    DECL_GETTER_REF(com::Utf8Str, User);
    DECL_GETTER_REF(com::Utf8Str, Domain);
    DECL_GETTER_REF(com::Utf8Str, ClientName);
    DECL_GETTER_REF(com::Utf8Str, ClientIP);
    DECL_GETTER(ULONG, ClientVersion);
    DECL_GETTER(ULONG, EncryptionStyle);
#undef DECL_GETTER_REF
#undef DECL_GETTER

    Console * const         mParent;
};

#endif /* !MAIN_INCLUDED_ConsoleVRDPServer_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
