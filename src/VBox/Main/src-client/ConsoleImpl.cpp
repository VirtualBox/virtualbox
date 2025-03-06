/* $Id: ConsoleImpl.cpp 108651 2025-03-06 11:15:45Z alexander.eichner@oracle.com $ */
/** @file
 * VBox Console COM Class implementation
 */

/*
 * Copyright (C) 2005-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_CONSOLE
#include "LoggingNew.h"

/** @todo Move the TAP mess back into the driver! */
#if defined(RT_OS_WINDOWS)
#elif defined(RT_OS_LINUX)
# include <errno.h>
# include <sys/ioctl.h>
# include <sys/poll.h>
# include <sys/fcntl.h>
# include <sys/types.h>
# include <sys/wait.h>
# include <net/if.h>
# include <linux/if_tun.h>
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
#elif defined(RT_OS_FREEBSD)
# include <errno.h>
# include <sys/ioctl.h>
# include <sys/poll.h>
# include <sys/fcntl.h>
# include <sys/types.h>
# include <sys/wait.h>
# include <stdio.h>
# include <stdlib.h>
# include <string.h>
#elif defined(RT_OS_SOLARIS)
# include <iprt/coredumper.h>
#endif

#include "ConsoleImpl.h"

#include "Global.h"
#include "VirtualBoxErrorInfoImpl.h"
#include "GuestImpl.h"
#include "KeyboardImpl.h"
#include "MouseImpl.h"
#include "DisplayImpl.h"
#include "MachineDebuggerImpl.h"
#include "USBDeviceImpl.h"
#include "RemoteUSBDeviceImpl.h"
#include "ConsoleSharedFolderImpl.h"
#ifdef VBOX_WITH_AUDIO_VRDE
# include "DrvAudioVRDE.h"
#endif
#ifdef VBOX_WITH_AUDIO_RECORDING
# include "DrvAudioRec.h"
#endif
#ifdef VBOX_WITH_USB_CARDREADER
# include "UsbCardReader.h"
#endif
#include "PlatformPropertiesImpl.h"
#include "ProgressImpl.h"
#include "ConsoleVRDPServer.h"
#include "VMMDev.h"
#ifdef VBOX_WITH_EXTPACK
# include "ExtPackManagerImpl.h"
#endif
#include "BusAssignmentManager.h"
#include "PCIDeviceAttachmentImpl.h"
#include "EmulatedUSBImpl.h"
#include "NvramStoreImpl.h"
#ifdef VBOX_WITH_VIRT_ARMV8
# include "ResourceStoreImpl.h"
#endif
#ifdef VBOX_WITH_SHARED_CLIPBOARD
# include "GuestShClPrivate.h"
#endif
#include "StringifyEnums.h"

#include "VBoxEvents.h"
#include "AutoCaller.h"
#include "ThreadTask.h"

#ifdef VBOX_WITH_RECORDING
# include "Recording.h"
#endif

#include "CryptoUtils.h"

#include <VBox/com/array.h>
#include "VBox/com/ErrorInfo.h"
#include <VBox/com/listeners.h>

#include <iprt/asm.h>
#include <iprt/buildconfig.h>
#include <iprt/cpp/utils.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/ldr.h>
#include <iprt/path.h>
#include <iprt/process.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/base64.h>
#include <iprt/memsafer.h>

#include <VBox/vmm/vmmr3vtable.h>
#include <VBox/vmm/vmapi.h>
#include <VBox/vmm/vmm.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/pdmaudioifs.h>
#include <VBox/vmm/pdmasynccompletion.h>
#include <VBox/vmm/pdmnetifs.h>
#include <VBox/vmm/pdmstorageifs.h>
#ifdef VBOX_WITH_USB
# include <VBox/vmm/pdmusb.h>
#endif
#ifdef VBOX_WITH_NETSHAPER
# include <VBox/vmm/pdmnetshaper.h>
#endif /* VBOX_WITH_NETSHAPER */
#include <VBox/vmm/mm.h>
#include <VBox/vmm/ssm.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/vusb.h>

#include <VBox/VMMDev.h>

#ifdef VBOX_WITH_SHARED_CLIPBOARD
# include <VBox/HostServices/VBoxClipboardSvc.h>
#endif
#include <VBox/HostServices/DragAndDropSvc.h>
#ifdef VBOX_WITH_GUEST_PROPS
# include <VBox/HostServices/GuestPropertySvc.h>
# include <VBox/com/array.h>
#endif

#ifdef VBOX_OPENSSL_FIPS
# include <openssl/crypto.h>
#endif

#include <set>
#include <algorithm>
#include <memory> // for auto_ptr
#include <vector>
#include <exception>// std::exception

// VMTask and friends
////////////////////////////////////////////////////////////////////////////////

/**
 * Task structure for asynchronous VM operations.
 *
 * Once created, the task structure adds itself as a Console caller. This means:
 *
 * 1. The user must check for #hrc() before using the created structure
 *    (e.g. passing it as a thread function argument). If #hrc() returns a
 *    failure, the Console object may not be used by the task.
 * 2. On successful initialization, the structure keeps the Console caller
 *    until destruction (to ensure Console remains in the Ready state and won't
 *    be accidentally uninitialized). Forgetting to delete the created task
 *    will lead to Console::uninit() stuck waiting for releasing all added
 *    callers.
 *
 * If \a aUsesVMPtr parameter is true, the task structure will also add itself
 * as a Console::mpUVM caller with the same meaning as above. See
 * Console::addVMCaller() for more info.
 */
class VMTask: public ThreadTask
{
public:
    VMTask(Console *aConsole,
           Progress *aProgress,
           const ComPtr<IProgress> &aServerProgress,
           bool aUsesVMPtr)
        : ThreadTask("GenericVMTask"),
          mConsole(aConsole),
          mConsoleCaller(aConsole),
          mProgress(aProgress),
          mServerProgress(aServerProgress),
          mRC(E_FAIL),
          mpSafeVMPtr(NULL)
    {
        AssertReturnVoid(aConsole);
        mRC = mConsoleCaller.hrc();
        if (FAILED(mRC))
            return;
        if (aUsesVMPtr)
        {
            mpSafeVMPtr = new Console::SafeVMPtr(aConsole);
            if (!mpSafeVMPtr->isOk())
                mRC = mpSafeVMPtr->hrc();
        }
    }

    virtual ~VMTask()
    {
        releaseVMCaller();
    }

    HRESULT hrc() const { return mRC; }
    bool isOk() const { return SUCCEEDED(hrc()); }

    /** Releases the VM caller before destruction. Not normally necessary. */
    void releaseVMCaller()
    {
        if (mpSafeVMPtr)
        {
            delete mpSafeVMPtr;
            mpSafeVMPtr = NULL;
        }
    }

    const ComObjPtr<Console>    mConsole;
    AutoCaller                  mConsoleCaller;
    const ComObjPtr<Progress>   mProgress;
    Utf8Str                     mErrorMsg;
    const ComPtr<IProgress>     mServerProgress;

private:
    HRESULT                     mRC;
    Console::SafeVMPtr         *mpSafeVMPtr;
};


class VMPowerUpTask : public VMTask
{
public:
    VMPowerUpTask(Console *aConsole,
                  Progress *aProgress)
        : VMTask(aConsole, aProgress, NULL /* aServerProgress */, false /* aUsesVMPtr */)
        , mpfnConfigConstructor(NULL)
        , mStartPaused(false)
        , mTeleporterEnabled(FALSE)
        , m_pKeyStore(NULL)
    {
        m_strTaskName = "VMPwrUp";
    }

    PFNCFGMCONSTRUCTOR mpfnConfigConstructor;
    Utf8Str mSavedStateFile;
    Utf8Str mKeyStore;
    Utf8Str mKeyId;
    Console::SharedFolderDataMap mSharedFolders;
    bool mStartPaused;
    BOOL mTeleporterEnabled;
    SecretKeyStore *m_pKeyStore;

    /* array of progress objects for hard disk reset operations */
    typedef std::list<ComPtr<IProgress> > ProgressList;
    ProgressList hardDiskProgresses;

    void handler()
    {
        Console::i_powerUpThreadTask(this);
    }

};

class VMPowerDownTask : public VMTask
{
public:
    VMPowerDownTask(Console *aConsole,
                    const ComPtr<IProgress> &aServerProgress)
        : VMTask(aConsole, NULL /* aProgress */, aServerProgress,
                 true /* aUsesVMPtr */)
    {
        m_strTaskName = "VMPwrDwn";
    }

    void handler()
    {
        Console::i_powerDownThreadTask(this);
    }
};

// Handler for global events
////////////////////////////////////////////////////////////////////////////////
inline static const char *networkAdapterTypeToName(NetworkAdapterType_T adapterType);

class VmEventListener
{
public:
    VmEventListener()
    {}


    HRESULT init(Console *aConsole)
    {
        mConsole = aConsole;
        return S_OK;
    }

    void uninit()
    {
    }

    virtual ~VmEventListener()
    {
    }

    STDMETHOD(HandleEvent)(VBoxEventType_T aType, IEvent *aEvent)
    {
        switch(aType)
        {
            case VBoxEventType_OnNATRedirect:
            {
                ComPtr<IMachine> pMachine = mConsole->i_machine();
                ComPtr<INATRedirectEvent> pNREv = aEvent;
                Assert(pNREv);

                Bstr id;
                HRESULT hrc = pNREv->COMGETTER(MachineId)(id.asOutParam());
                AssertComRC(hrc);
                if (id != mConsole->i_getId())
                    break;

                /* now we can operate with redirects */
                NATProtocol_T proto = (NATProtocol_T)0;
                pNREv->COMGETTER(Proto)(&proto);
                BOOL fRemove;
                pNREv->COMGETTER(Remove)(&fRemove);
                Bstr hostIp;
                pNREv->COMGETTER(HostIP)(hostIp.asOutParam());
                LONG hostPort = 0;
                pNREv->COMGETTER(HostPort)(&hostPort);
                Bstr guestIp;
                pNREv->COMGETTER(GuestIP)(guestIp.asOutParam());
                LONG guestPort = 0;
                pNREv->COMGETTER(GuestPort)(&guestPort);
                ULONG ulSlot;
                hrc = pNREv->COMGETTER(Slot)(&ulSlot);
                AssertComRCBreak(hrc, RT_NOTHING);
                mConsole->i_onNATRedirectRuleChanged(ulSlot, fRemove, proto, hostIp.raw(), hostPort, guestIp.raw(), guestPort);
                break;
            }

            case VBoxEventType_OnHostNameResolutionConfigurationChange:
            {
                mConsole->i_onNATDnsChanged();
                break;
            }

            case VBoxEventType_OnHostPCIDevicePlug:
            {
                // handle if needed
                break;
            }

            case VBoxEventType_OnExtraDataChanged:
            {
                ComPtr<IExtraDataChangedEvent> pEDCEv = aEvent;
                Bstr strMachineId;
                HRESULT hrc = pEDCEv->COMGETTER(MachineId)(strMachineId.asOutParam());
                if (FAILED(hrc)) break;

                Bstr strKey;
                hrc = pEDCEv->COMGETTER(Key)(strKey.asOutParam());
                if (FAILED(hrc)) break;

                Bstr strVal;
                hrc = pEDCEv->COMGETTER(Value)(strVal.asOutParam());
                if (FAILED(hrc)) break;

                mConsole->i_onExtraDataChange(strMachineId.raw(), strKey.raw(), strVal.raw());
                break;
            }

            default:
              AssertFailed();
        }

        return S_OK;
    }
private:
    ComObjPtr<Console>    mConsole;
};

typedef ListenerImpl<VmEventListener, Console*> VmEventListenerImpl;


VBOX_LISTENER_DECLARE(VmEventListenerImpl)


// constructor / destructor
/////////////////////////////////////////////////////////////////////////////

Console::Console()
    : mSavedStateDataLoaded(false)
    , mConsoleVRDPServer(NULL)
    , mfVRDEChangeInProcess(false)
    , mfVRDEChangePending(false)
    , mhModVMM(NIL_RTLDRMOD)
    , mpVMM(NULL)
    , mpUVM(NULL)
    , mVMCallers(0)
    , mVMZeroCallersSem(NIL_RTSEMEVENT)
    , mVMDestroying(false)
    , mVMPoweredOff(false)
    , mVMIsAlreadyPoweringOff(false)
    , mfSnapshotFolderSizeWarningShown(false)
    , mfSnapshotFolderExt4WarningShown(false)
    , mfSnapshotFolderDiskTypeShown(false)
    , mfVMHasUsbController(false)
    , mfTurnResetIntoPowerOff(false)
    , mfPowerOffCausedByReset(false)
    , mpVmm2UserMethods(NULL)
    , m_pVMMDev(NULL)
    , mAudioVRDE(NULL)
#ifdef VBOX_WITH_USB_CARDREADER
    , mUsbCardReader(NULL)
#endif
    , mBusMgr(NULL)
    , mLedLock(LOCKCLASS_LISTOFOTHEROBJECTS /* must be higher than LOCKCLASS_OTHEROBJECT */, "LedLock")
    , muLedGen(0)
    , muLedTypeGen(0)
    , mcLedSets(0)
    , m_pKeyStore(NULL)
    , mpIfSecKey(NULL)
    , mpIfSecKeyHlp(NULL)
    , mVMStateChangeCallbackDisabled(false)
    , mfUseHostClipboard(true)
    , mMachineState(MachineState_PoweredOff)
    , mhLdrModCrypto(NIL_RTLDRMOD)
    , mcRefsCrypto(0)
    , mpCryptoIf(NULL)
{
    RT_ZERO(maLedSets);
    RT_ZERO(maLedTypes);
}

Console::~Console()
{}

HRESULT Console::FinalConstruct()
{
    LogFlowThisFunc(("\n"));

    MYVMM2USERMETHODS *pVmm2UserMethods = (MYVMM2USERMETHODS *)RTMemAllocZ(sizeof(*mpVmm2UserMethods) + sizeof(Console *));
    if (!pVmm2UserMethods)
        return E_OUTOFMEMORY;
    pVmm2UserMethods->u32Magic          = VMM2USERMETHODS_MAGIC;
    pVmm2UserMethods->u32Version        = VMM2USERMETHODS_VERSION;
    pVmm2UserMethods->pfnSaveState      = Console::i_vmm2User_SaveState;
    pVmm2UserMethods->pfnNotifyEmtInit  = Console::i_vmm2User_NotifyEmtInit;
    pVmm2UserMethods->pfnNotifyEmtTerm  = Console::i_vmm2User_NotifyEmtTerm;
    pVmm2UserMethods->pfnNotifyPdmtInit = Console::i_vmm2User_NotifyPdmtInit;
    pVmm2UserMethods->pfnNotifyPdmtTerm = Console::i_vmm2User_NotifyPdmtTerm;
    pVmm2UserMethods->pfnNotifyResetTurnedIntoPowerOff = Console::i_vmm2User_NotifyResetTurnedIntoPowerOff;
    pVmm2UserMethods->pfnQueryGenericObject = Console::i_vmm2User_QueryGenericObject;
    pVmm2UserMethods->u32EndMagic       = VMM2USERMETHODS_MAGIC;
    pVmm2UserMethods->pConsole          = this;
    mpVmm2UserMethods = pVmm2UserMethods;

    MYPDMISECKEY *pIfSecKey = (MYPDMISECKEY *)RTMemAllocZ(sizeof(*mpIfSecKey) + sizeof(Console *));
    if (!pIfSecKey)
        return E_OUTOFMEMORY;
    pIfSecKey->pfnKeyRetain             = Console::i_pdmIfSecKey_KeyRetain;
    pIfSecKey->pfnKeyRelease            = Console::i_pdmIfSecKey_KeyRelease;
    pIfSecKey->pfnPasswordRetain        = Console::i_pdmIfSecKey_PasswordRetain;
    pIfSecKey->pfnPasswordRelease       = Console::i_pdmIfSecKey_PasswordRelease;
    pIfSecKey->pConsole                 = this;
    mpIfSecKey = pIfSecKey;

    MYPDMISECKEYHLP *pIfSecKeyHlp = (MYPDMISECKEYHLP *)RTMemAllocZ(sizeof(*mpIfSecKeyHlp) + sizeof(Console *));
    if (!pIfSecKeyHlp)
        return E_OUTOFMEMORY;
    pIfSecKeyHlp->pfnKeyMissingNotify   = Console::i_pdmIfSecKeyHlp_KeyMissingNotify;
    pIfSecKeyHlp->pConsole              = this;
    mpIfSecKeyHlp = pIfSecKeyHlp;

#ifdef VBOX_WITH_USB
    mRemoteUsbIf.pvUser                   = this;
    mRemoteUsbIf.pfnQueryRemoteUsbBackend = Console::i_usbQueryRemoteUsbBackend;
#endif

    return BaseFinalConstruct();
}

void Console::FinalRelease()
{
    LogFlowThisFunc(("\n"));

    uninit();

    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/** @todo r=bird: aLockType is always LockType_VM.   */
HRESULT Console::initWithMachine(IMachine *aMachine, IInternalMachineControl *aControl, LockType_T aLockType)
{
    AssertReturn(aMachine && aControl, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    LogFlowThisFuncEnter();
    LogFlowThisFunc(("aMachine=%p, aControl=%p\n", aMachine, aControl));

    unconst(mMachine) = aMachine;
    unconst(mControl) = aControl;

    /* Cache essential properties and objects, and create child objects */

    HRESULT hrc = mMachine->COMGETTER(State)(&mMachineState);
    AssertComRCReturnRC(hrc);

    hrc = mMachine->COMGETTER(Id)(mstrUuid.asOutParam());
    AssertComRCReturnRC(hrc);

#ifdef VBOX_WITH_EXTPACK
    unconst(mptrExtPackManager).createObject();
    hrc = mptrExtPackManager->initExtPackManager(NULL, VBOXEXTPACKCTX_VM_PROCESS);
    AssertComRCReturnRC(hrc);
#endif

    // Event source may be needed by other children
    unconst(mEventSource).createObject();
    hrc = mEventSource->init();
    AssertComRCReturnRC(hrc);

    mcAudioRefs = 0;
    mcVRDPClients = 0;
    mu32SingleRDPClientId = 0;
    mcGuestCredentialsProvided = false;

    ComPtr<IPlatform> pPlatform;
    hrc = mMachine->COMGETTER(Platform)(pPlatform.asOutParam());
    AssertComRCReturnRC(hrc);

    PlatformArchitecture_T platformArch;
    hrc = pPlatform->COMGETTER(Architecture)(&platformArch);
    AssertComRCReturnRC(hrc);

    /* Now the VM specific parts */
    /** @todo r=bird: aLockType is always LockType_VM.   */
    if (aLockType == LockType_VM)
    {
        const char *pszVMM = NULL; /* Shut up MSVC. */

        switch (platformArch)
        {
            case PlatformArchitecture_x86:
#ifdef VBOX_WITH_VIRT_ARMV8
                {
                    ComPtr<IVirtualBox> pVirtualBox;
                    hrc = mMachine->COMGETTER(Parent)(pVirtualBox.asOutParam());
                    if (SUCCEEDED(hrc))
                    {
                        Bstr bstrEnableX86OnArm;
                        hrc = pVirtualBox->GetExtraData(Bstr("VBoxInternal2/EnableX86OnArm").raw(), bstrEnableX86OnArm.asOutParam());
                        if (FAILED(hrc) || !bstrEnableX86OnArm.equals("1"))
                        {
                            hrc = setError(VBOX_E_PLATFORM_ARCH_NOT_SUPPORTED,
                                           tr("Cannot run the machine because its platform architecture %s is not supported on %s"),
                                           Global::stringifyPlatformArchitecture(platformArch),
                                           Global::stringifyPlatformArchitecture(PlatformArchitecture_ARM));
                            break;
                        }
                    }
                }
#endif
                pszVMM = "VBoxVMM";
                break;
#ifdef VBOX_WITH_VIRT_ARMV8
            case PlatformArchitecture_ARM:
                pszVMM = "VBoxVMMArm";
                break;
#endif
            default:
                hrc = VBOX_E_PLATFORM_ARCH_NOT_SUPPORTED;
                break;
        }

        if (FAILED(hrc))
            return hrc;

        /* Load the VMM. We won't continue without it being successfully loaded here. */
        hrc = i_loadVMM(pszVMM);
        AssertComRCReturnRC(hrc);

#ifdef VBOX_WITH_VIRT_ARMV8
        unconst(mptrResourceStore).createObject();
        hrc = mptrResourceStore->init(this);
        AssertComRCReturnRC(hrc);
#endif
        hrc = mMachine->COMGETTER(VRDEServer)(unconst(mVRDEServer).asOutParam());
        AssertComRCReturnRC(hrc);

        unconst(mGuest).createObject();
        hrc = mGuest->init(this);
        AssertComRCReturnRC(hrc);

        ULONG cCpus = 1;
        hrc = mMachine->COMGETTER(CPUCount)(&cCpus);
        mGuest->i_setCpuCount(cCpus);

        unconst(mKeyboard).createObject();
        hrc = mKeyboard->init(this);
        AssertComRCReturnRC(hrc);

        unconst(mMouse).createObject();
        hrc = mMouse->init(this);
        AssertComRCReturnRC(hrc);

        unconst(mDisplay).createObject();
        hrc = mDisplay->init(this);
        AssertComRCReturnRC(hrc);

        unconst(mVRDEServerInfo).createObject();
        hrc = mVRDEServerInfo->init(this);
        AssertComRCReturnRC(hrc);

        unconst(mEmulatedUSB).createObject();
        hrc = mEmulatedUSB->init(this);
        AssertComRCReturnRC(hrc);

        /* Init the NVRAM store. */
        ComPtr<INvramStore> pNvramStore;
        hrc = aMachine->COMGETTER(NonVolatileStore)(pNvramStore.asOutParam());
        AssertComRCReturnRC(hrc);

        Bstr strNonVolatilePath;
        pNvramStore->COMGETTER(NonVolatileStorageFile)(strNonVolatilePath.asOutParam());

        unconst(mptrNvramStore).createObject();
        hrc = mptrNvramStore->init(this, strNonVolatilePath);
        AssertComRCReturnRC(hrc);

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
        Bstr bstrNvramKeyId;
        Bstr bstrNvramKeyStore;
        hrc = pNvramStore->COMGETTER(KeyId)(bstrNvramKeyId.asOutParam());
        AssertComRCReturnRC(hrc);
        hrc = pNvramStore->COMGETTER(KeyStore)(bstrNvramKeyStore.asOutParam());
        AssertComRCReturnRC(hrc);
        const Utf8Str strNvramKeyId(bstrNvramKeyId);
        const Utf8Str strNvramKeyStore(bstrNvramKeyStore);
        mptrNvramStore->i_updateEncryptionSettings(strNvramKeyId, strNvramKeyStore);
#endif

        /* Grab global and machine shared folder lists */

        hrc = i_fetchSharedFolders(true /* aGlobal */);
        AssertComRCReturnRC(hrc);
        hrc = i_fetchSharedFolders(false /* aGlobal */);
        AssertComRCReturnRC(hrc);

        /* Create other child objects */

        unconst(mConsoleVRDPServer) = new ConsoleVRDPServer(this);
        AssertReturn(mConsoleVRDPServer, E_FAIL);

        /* Figure out size of meAttachmentType vector */
        ComPtr<IVirtualBox> pVirtualBox;
        hrc = aMachine->COMGETTER(Parent)(pVirtualBox.asOutParam());
        AssertComRC(hrc);

        ComPtr<ISystemProperties> pSystemProperties;
        ComPtr<IPlatformProperties> pPlatformProperties;
        if (pVirtualBox)
        {
            hrc = pVirtualBox->COMGETTER(SystemProperties)(pSystemProperties.asOutParam());
            AssertComRC(hrc);
            hrc = pVirtualBox->GetPlatformProperties(platformArch, pPlatformProperties.asOutParam());
            AssertComRC(hrc);
        }

        ChipsetType_T chipsetType = ChipsetType_PIIX3;
        pPlatform->COMGETTER(ChipsetType)(&chipsetType);
        ULONG maxNetworkAdapters = 0;
        if (pPlatformProperties)
            pPlatformProperties->GetMaxNetworkAdapters(chipsetType, &maxNetworkAdapters);
        meAttachmentType.resize(maxNetworkAdapters);
        for (ULONG slot = 0; slot < maxNetworkAdapters; ++slot)
            meAttachmentType[slot] = NetworkAttachmentType_Null;

#ifdef VBOX_WITH_AUDIO_VRDE
        unconst(mAudioVRDE) = new AudioVRDE(this);
        AssertReturn(mAudioVRDE, E_FAIL);
#endif
#ifdef VBOX_WITH_AUDIO_RECORDING
        unconst(mRecording.mAudioRec) = new AudioVideoRec(this);
        AssertReturn(mRecording.mAudioRec, E_FAIL);
#endif

#ifdef VBOX_WITH_USB_CARDREADER
        unconst(mUsbCardReader) = new UsbCardReader(this);
        AssertReturn(mUsbCardReader, E_FAIL);
#endif

        m_cDisksPwProvided = 0;
        m_cDisksEncrypted = 0;

        unconst(m_pKeyStore) = new SecretKeyStore(true /* fKeyBufNonPageable */);
        AssertReturn(m_pKeyStore, E_FAIL);

        /* VirtualBox events registration. */
        {
            ComPtr<IEventSource> pES;
            hrc = pVirtualBox->COMGETTER(EventSource)(pES.asOutParam());
            AssertComRC(hrc);
            ComObjPtr<VmEventListenerImpl> aVmListener;
            aVmListener.createObject();
            aVmListener->init(new VmEventListener(), this);
            mVmListener = aVmListener;
            com::SafeArray<VBoxEventType_T> eventTypes;
            eventTypes.push_back(VBoxEventType_OnNATRedirect);
            eventTypes.push_back(VBoxEventType_OnHostNameResolutionConfigurationChange);
            eventTypes.push_back(VBoxEventType_OnHostPCIDevicePlug);
            eventTypes.push_back(VBoxEventType_OnExtraDataChanged);
            hrc = pES->RegisterListener(aVmListener, ComSafeArrayAsInParam(eventTypes), true);
            AssertComRC(hrc);
        }
    }

    /* Confirm a successful initialization when it's the case */
    autoInitSpan.setSucceeded();

#ifdef VBOX_WITH_EXTPACK
    /* Let the extension packs have a go at things (hold no locks). */
    if (SUCCEEDED(hrc))
        mptrExtPackManager->i_callAllConsoleReadyHooks(this);
#endif

    LogFlowThisFuncLeave();

    return S_OK;
}

/**
 * Uninitializes the Console object.
 */
void Console::uninit()
{
    LogFlowThisFuncEnter();

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
    {
        LogFlowThisFunc(("Already uninitialized.\n"));
        LogFlowThisFuncLeave();
        return;
    }

    LogFlowThisFunc(("initFailed()=%d\n", autoUninitSpan.initFailed()));
    if (mVmListener)
    {
        ComPtr<IEventSource> pES;
        ComPtr<IVirtualBox> pVirtualBox;
        HRESULT hrc = mMachine->COMGETTER(Parent)(pVirtualBox.asOutParam());
        AssertComRC(hrc);
        if (SUCCEEDED(hrc) && !pVirtualBox.isNull())
        {
            hrc = pVirtualBox->COMGETTER(EventSource)(pES.asOutParam());
            AssertComRC(hrc);
            if (!pES.isNull())
            {
                hrc = pES->UnregisterListener(mVmListener);
                AssertComRC(hrc);
            }
        }
        mVmListener.setNull();
    }

    /* power down the VM if necessary */
    if (mpUVM)
    {
        i_powerDown();
        Assert(mpUVM == NULL);
    }

    if (mVMZeroCallersSem != NIL_RTSEMEVENT)
    {
        RTSemEventDestroy(mVMZeroCallersSem);
        mVMZeroCallersSem = NIL_RTSEMEVENT;
    }

    if (mpVmm2UserMethods)
    {
        RTMemFree((void *)mpVmm2UserMethods);
        mpVmm2UserMethods = NULL;
    }

    if (mpIfSecKey)
    {
        RTMemFree((void *)mpIfSecKey);
        mpIfSecKey = NULL;
    }

    if (mpIfSecKeyHlp)
    {
        RTMemFree((void *)mpIfSecKeyHlp);
        mpIfSecKeyHlp = NULL;
    }

#ifdef VBOX_WITH_USB_CARDREADER
    if (mUsbCardReader)
    {
        delete mUsbCardReader;
        unconst(mUsbCardReader) = NULL;
    }
#endif

#ifdef VBOX_WITH_AUDIO_VRDE
    if (mAudioVRDE)
    {
        delete mAudioVRDE;
        unconst(mAudioVRDE) = NULL;
    }
#endif

#ifdef VBOX_WITH_RECORDING
    i_recordingDestroy();
# ifdef VBOX_WITH_AUDIO_RECORDING
    if (mRecording.mAudioRec)
    {
        delete mRecording.mAudioRec;
        unconst(mRecording.mAudioRec) = NULL;
    }
# endif
#endif /* VBOX_WITH_RECORDING */

    // if the VM had a VMMDev with an HGCM thread, then remove that here
    if (m_pVMMDev)
    {
        delete m_pVMMDev;
        unconst(m_pVMMDev) = NULL;
    }

    if (mBusMgr)
    {
        mBusMgr->Release();
        mBusMgr = NULL;
    }

    if (m_pKeyStore)
    {
        delete m_pKeyStore;
        unconst(m_pKeyStore) = NULL;
    }

    m_mapGlobalSharedFolders.clear();
    m_mapMachineSharedFolders.clear();
    m_mapSharedFolders.clear();             // console instances

    mRemoteUSBDevices.clear();
    mUSBDevices.clear();

    if (mVRDEServerInfo)
    {
        mVRDEServerInfo->uninit();
        unconst(mVRDEServerInfo).setNull();
    }

    if (mEmulatedUSB)
    {
        mEmulatedUSB->uninit();
        unconst(mEmulatedUSB).setNull();
    }

    if (mDebugger)
    {
        mDebugger->uninit();
        unconst(mDebugger).setNull();
    }

    if (mDisplay)
    {
        mDisplay->uninit();
        unconst(mDisplay).setNull();
    }

    if (mMouse)
    {
        mMouse->uninit();
        unconst(mMouse).setNull();
    }

    if (mKeyboard)
    {
        mKeyboard->uninit();
        unconst(mKeyboard).setNull();
    }

    if (mGuest)
    {
        mGuest->uninit();
        unconst(mGuest).setNull();
    }

    if (mConsoleVRDPServer)
    {
        delete mConsoleVRDPServer;
        unconst(mConsoleVRDPServer) = NULL;
    }

    if (mptrNvramStore)
    {
        mptrNvramStore->uninit();
        unconst(mptrNvramStore).setNull();
    }

    unconst(mVRDEServer).setNull();

#ifdef VBOX_WITH_VIRT_ARMV8
    if (mptrResourceStore)
    {
        mptrResourceStore->uninit();
        unconst(mptrResourceStore).setNull();
    }
#endif

    unconst(mControl).setNull();
    unconst(mMachine).setNull();

    // we don't perform uninit() as it's possible that some pending event refers to this source
    unconst(mEventSource).setNull();

#ifdef VBOX_WITH_EXTPACK
    unconst(mptrExtPackManager).setNull();
#endif

    /* Unload the VMM. */
    mpVMM = NULL;
    if (mhModVMM != NIL_RTLDRMOD)
    {
        RTLdrClose(mhModVMM);
        mhModVMM = NIL_RTLDRMOD;
    }

    /* Release memory held by the LED sets (no need to take lock). */
    for (size_t idxType = 0; idxType < RT_ELEMENTS(maLedTypes); idxType++)
    {
        maLedTypes[idxType].cLeds = 0;
        maLedTypes[idxType].cAllocated = 0;
        RTMemFree(maLedTypes[idxType].pappLeds);
        maLedTypes[idxType].pappLeds = NULL;
    }
    for (size_t idxSet = 0; idxSet < mcLedSets; idxSet++)
    {
        maLedSets[idxSet].cLeds = 0;
        RTMemFree((void *)maLedSets[idxSet].papLeds);
        maLedSets[idxSet].papLeds = NULL;
        maLedSets[idxSet].paSubTypes = NULL;
    }
    mcLedSets = 0;

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
    /* Close the release log before unloading the cryptographic module. */
    if (m_fEncryptedLog)
    {
        PRTLOGGER pLogEnc = RTLogRelSetDefaultInstance(NULL);
        int vrc = RTLogDestroy(pLogEnc);
        AssertRC(vrc);
    }
#endif

    HRESULT hrc = i_unloadCryptoIfModule();
    AssertComRC(hrc);

    LogFlowThisFuncLeave();
}

#ifdef VBOX_WITH_GUEST_PROPS

/**
 * Wrapper for VMMDev::i_guestPropertiesHandleVMReset
 */
HRESULT Console::i_pullGuestProperties(ComSafeArrayOut(BSTR, names), ComSafeArrayOut(BSTR, values),
                                       ComSafeArrayOut(LONG64, timestamps), ComSafeArrayOut(BSTR, flags))
{
    AssertReturn(mControl.isNotNull(), E_POINTER);
    return mControl->PullGuestProperties(ComSafeArrayOutArg(names), ComSafeArrayOutArg(values),
                                         ComSafeArrayOutArg(timestamps), ComSafeArrayOutArg(flags));
}

/**
 * Handles guest properties on a VM reset.
 *
 * We must delete properties that are flagged TRANSRESET.
 *
 * @todo r=bird: Would be more efficient if we added a request to the HGCM
 *       service to do this instead of detouring thru VBoxSVC.
 *       (IMachine::SetGuestProperty ends up in VBoxSVC, which in turns calls
 *       back into the VM process and the HGCM service.)
 */
void Console::i_guestPropertiesHandleVMReset(void)
{
    std::vector<Utf8Str> names;
    std::vector<Utf8Str> values;
    std::vector<LONG64>  timestamps;
    std::vector<Utf8Str> flags;
    HRESULT hrc = i_enumerateGuestProperties("*", names, values, timestamps, flags);
    if (SUCCEEDED(hrc))
    {
        for (size_t i = 0; i < flags.size(); i++)
        {
            /* Delete all properties which have the flag "TRANSRESET". */
            if (flags[i].contains("TRANSRESET", Utf8Str::CaseInsensitive))
            {
                hrc = mMachine->DeleteGuestProperty(Bstr(names[i]).raw());
                if (FAILED(hrc))
                    LogRel(("RESET: Could not delete transient property \"%s\", hrc=%Rhrc\n",
                            names[i].c_str(), hrc));
            }
        }
    }
    else
        LogRel(("RESET: Unable to enumerate guest properties, hrc=%Rhrc\n", hrc));
}

bool Console::i_guestPropertiesVRDPEnabled(void)
{
    Bstr value;
    HRESULT hrc = mMachine->GetExtraData(Bstr("VBoxInternal2/EnableGuestPropertiesVRDP").raw(),
                                         value.asOutParam());
    if (   hrc   == S_OK
        && value == "1")
        return true;
    return false;
}

void Console::i_guestPropertiesVRDPUpdateLogon(uint32_t u32ClientId, const char *pszUser, const char *pszDomain)
{
    if (!i_guestPropertiesVRDPEnabled())
        return;

    LogFlowFunc(("\n"));

    char szPropNm[256];
    Bstr bstrReadOnlyGuest(L"RDONLYGUEST");

    RTStrPrintf(szPropNm, sizeof(szPropNm), "/VirtualBox/HostInfo/VRDP/Client/%u/Name", u32ClientId);
    Bstr clientName;
    mVRDEServerInfo->COMGETTER(ClientName)(clientName.asOutParam());

    mMachine->SetGuestProperty(Bstr(szPropNm).raw(),
                               clientName.raw(),
                               bstrReadOnlyGuest.raw());

    RTStrPrintf(szPropNm, sizeof(szPropNm), "/VirtualBox/HostInfo/VRDP/Client/%u/User", u32ClientId);
    mMachine->SetGuestProperty(Bstr(szPropNm).raw(),
                               Bstr(pszUser).raw(),
                               bstrReadOnlyGuest.raw());

    RTStrPrintf(szPropNm, sizeof(szPropNm), "/VirtualBox/HostInfo/VRDP/Client/%u/Domain", u32ClientId);
    mMachine->SetGuestProperty(Bstr(szPropNm).raw(),
                               Bstr(pszDomain).raw(),
                               bstrReadOnlyGuest.raw());

    char szClientId[64];
    RTStrPrintf(szClientId, sizeof(szClientId), "%u", u32ClientId);
    mMachine->SetGuestProperty(Bstr("/VirtualBox/HostInfo/VRDP/LastConnectedClient").raw(),
                               Bstr(szClientId).raw(),
                               bstrReadOnlyGuest.raw());

    return;
}

void Console::i_guestPropertiesVRDPUpdateActiveClient(uint32_t u32ClientId)
{
    if (!i_guestPropertiesVRDPEnabled())
        return;

    LogFlowFunc(("%d\n", u32ClientId));

    Bstr bstrFlags(L"RDONLYGUEST,TRANSIENT");

    char szClientId[64];
    RTStrPrintf(szClientId, sizeof(szClientId), "%u", u32ClientId);

    mMachine->SetGuestProperty(Bstr("/VirtualBox/HostInfo/VRDP/ActiveClient").raw(),
                               Bstr(szClientId).raw(),
                               bstrFlags.raw());

    return;
}

void Console::i_guestPropertiesVRDPUpdateNameChange(uint32_t u32ClientId, const char *pszName)
{
    if (!i_guestPropertiesVRDPEnabled())
        return;

    LogFlowFunc(("\n"));

    char szPropNm[256];
    Bstr bstrReadOnlyGuest(L"RDONLYGUEST");

    RTStrPrintf(szPropNm, sizeof(szPropNm), "/VirtualBox/HostInfo/VRDP/Client/%u/Name", u32ClientId);
    Bstr clientName(pszName);

    mMachine->SetGuestProperty(Bstr(szPropNm).raw(),
                               clientName.raw(),
                               bstrReadOnlyGuest.raw());

}

void Console::i_guestPropertiesVRDPUpdateIPAddrChange(uint32_t u32ClientId, const char *pszIPAddr)
{
    if (!i_guestPropertiesVRDPEnabled())
        return;

    LogFlowFunc(("\n"));

    char szPropNm[256];
    Bstr bstrReadOnlyGuest(L"RDONLYGUEST");

    RTStrPrintf(szPropNm, sizeof(szPropNm), "/VirtualBox/HostInfo/VRDP/Client/%u/IPAddr", u32ClientId);
    Bstr clientIPAddr(pszIPAddr);

    mMachine->SetGuestProperty(Bstr(szPropNm).raw(),
                               clientIPAddr.raw(),
                               bstrReadOnlyGuest.raw());

}

void Console::i_guestPropertiesVRDPUpdateLocationChange(uint32_t u32ClientId, const char *pszLocation)
{
    if (!i_guestPropertiesVRDPEnabled())
        return;

    LogFlowFunc(("\n"));

    char szPropNm[256];
    Bstr bstrReadOnlyGuest(L"RDONLYGUEST");

    RTStrPrintf(szPropNm, sizeof(szPropNm), "/VirtualBox/HostInfo/VRDP/Client/%u/Location", u32ClientId);
    Bstr clientLocation(pszLocation);

    mMachine->SetGuestProperty(Bstr(szPropNm).raw(),
                               clientLocation.raw(),
                               bstrReadOnlyGuest.raw());

}

void Console::i_guestPropertiesVRDPUpdateOtherInfoChange(uint32_t u32ClientId, const char *pszOtherInfo)
{
    if (!i_guestPropertiesVRDPEnabled())
        return;

    LogFlowFunc(("\n"));

    char szPropNm[256];
    Bstr bstrReadOnlyGuest(L"RDONLYGUEST");

    RTStrPrintf(szPropNm, sizeof(szPropNm), "/VirtualBox/HostInfo/VRDP/Client/%u/OtherInfo", u32ClientId);
    Bstr clientOtherInfo(pszOtherInfo);

    mMachine->SetGuestProperty(Bstr(szPropNm).raw(),
                               clientOtherInfo.raw(),
                               bstrReadOnlyGuest.raw());

}

void Console::i_guestPropertiesVRDPUpdateClientAttach(uint32_t u32ClientId, bool fAttached)
{
    if (!i_guestPropertiesVRDPEnabled())
        return;

    LogFlowFunc(("\n"));

    Bstr bstrReadOnlyGuest(L"RDONLYGUEST");

    char szPropNm[256];
    RTStrPrintf(szPropNm, sizeof(szPropNm), "/VirtualBox/HostInfo/VRDP/Client/%u/Attach", u32ClientId);

    Bstr bstrValue = fAttached? "1": "0";

    mMachine->SetGuestProperty(Bstr(szPropNm).raw(),
                               bstrValue.raw(),
                               bstrReadOnlyGuest.raw());
}

void Console::i_guestPropertiesVRDPUpdateDisconnect(uint32_t u32ClientId)
{
    if (!i_guestPropertiesVRDPEnabled())
        return;

    LogFlowFunc(("\n"));

    Bstr bstrReadOnlyGuest(L"RDONLYGUEST");

    char szPropNm[256];
    RTStrPrintf(szPropNm, sizeof(szPropNm), "/VirtualBox/HostInfo/VRDP/Client/%u/Name", u32ClientId);
    mMachine->SetGuestProperty(Bstr(szPropNm).raw(), NULL,
                               bstrReadOnlyGuest.raw());

    RTStrPrintf(szPropNm, sizeof(szPropNm), "/VirtualBox/HostInfo/VRDP/Client/%u/User", u32ClientId);
    mMachine->SetGuestProperty(Bstr(szPropNm).raw(), NULL,
                               bstrReadOnlyGuest.raw());

    RTStrPrintf(szPropNm, sizeof(szPropNm), "/VirtualBox/HostInfo/VRDP/Client/%u/Domain", u32ClientId);
    mMachine->SetGuestProperty(Bstr(szPropNm).raw(), NULL,
                               bstrReadOnlyGuest.raw());

    RTStrPrintf(szPropNm, sizeof(szPropNm), "/VirtualBox/HostInfo/VRDP/Client/%u/Attach", u32ClientId);
    mMachine->SetGuestProperty(Bstr(szPropNm).raw(), NULL,
                               bstrReadOnlyGuest.raw());

    char szClientId[64];
    RTStrPrintf(szClientId, sizeof(szClientId), "%d", u32ClientId);
    mMachine->SetGuestProperty(Bstr("/VirtualBox/HostInfo/VRDP/LastDisconnectedClient").raw(),
                               Bstr(szClientId).raw(),
                               bstrReadOnlyGuest.raw());

    return;
}

#endif /* VBOX_WITH_GUEST_PROPS */

#ifdef VBOX_WITH_EXTPACK
/**
 * Used by VRDEServer and others to talke to the extension pack manager.
 *
 * @returns The extension pack manager.
 */
ExtPackManager *Console::i_getExtPackManager()
{
    return mptrExtPackManager;
}
#endif


int Console::i_VRDPClientLogon(uint32_t u32ClientId, const char *pszUser, const char *pszPassword, const char *pszDomain)
{
    LogFlowFuncEnter();
    LogFlowFunc(("%d, %s, %s, %s\n", u32ClientId, pszUser, pszPassword, pszDomain));

    AutoCaller autoCaller(this);
    if (!autoCaller.isOk())
    {
        /* Console has been already uninitialized, deny request */
        LogRel(("AUTH: Access denied (Console uninitialized).\n"));
        LogFlowFuncLeave();
        return VERR_ACCESS_DENIED;
    }

    Guid uuid = Guid(i_getId());

    AuthType_T authType = AuthType_Null;
    HRESULT hrc = mVRDEServer->COMGETTER(AuthType)(&authType);
    AssertComRCReturn(hrc, VERR_ACCESS_DENIED);

    ULONG authTimeout = 0;
    hrc = mVRDEServer->COMGETTER(AuthTimeout)(&authTimeout);
    AssertComRCReturn(hrc, VERR_ACCESS_DENIED);

    AuthResult result = AuthResultAccessDenied;
    AuthGuestJudgement guestJudgement = AuthGuestNotAsked;

    LogFlowFunc(("Auth type %d\n", authType));

    LogRel(("AUTH: User: [%s]. Domain: [%s]. Authentication type: [%s]\n",
                pszUser, pszDomain,
                authType == AuthType_Null?
                    "Null":
                    (authType == AuthType_External?
                        "External":
                        (authType == AuthType_Guest?
                            "Guest":
                            "INVALID"
                        )
                    )
            ));

    switch (authType)
    {
        case AuthType_Null:
        {
            result = AuthResultAccessGranted;
            break;
        }

        case AuthType_External:
        {
            /* Call the external library. */
            result = mConsoleVRDPServer->Authenticate(uuid, guestJudgement, pszUser, pszPassword, pszDomain, u32ClientId);

            if (result != AuthResultDelegateToGuest)
            {
                break;
            }

            LogRel(("AUTH: Delegated to guest.\n"));

            LogFlowFunc(("External auth asked for guest judgement\n"));
        }
        RT_FALL_THRU();

        case AuthType_Guest:
        {
            guestJudgement = AuthGuestNotReacted;

            /** @todo r=dj locking required here for m_pVMMDev? */
            PPDMIVMMDEVPORT pDevPort;
            if (    m_pVMMDev
                 && ((pDevPort = m_pVMMDev->getVMMDevPort()))
               )
            {
                /* Issue the request to guest. Assume that the call does not require EMT. It should not. */

                /* Ask the guest to judge these credentials. */
                uint32_t u32GuestFlags = VMMDEV_SETCREDENTIALS_JUDGE;

                int vrc = pDevPort->pfnSetCredentials(pDevPort, pszUser, pszPassword, pszDomain, u32GuestFlags);
                if (RT_SUCCESS(vrc))
                {
                    /* Wait for guest. */
                    vrc = m_pVMMDev->WaitCredentialsJudgement(authTimeout, &u32GuestFlags);
                    if (RT_SUCCESS(vrc))
                    {
                        switch (u32GuestFlags & (  VMMDEV_CREDENTIALS_JUDGE_OK
                                                 | VMMDEV_CREDENTIALS_JUDGE_DENY
                                                 | VMMDEV_CREDENTIALS_JUDGE_NOJUDGEMENT))
                        {
                            case VMMDEV_CREDENTIALS_JUDGE_DENY:        guestJudgement = AuthGuestAccessDenied;  break;
                            case VMMDEV_CREDENTIALS_JUDGE_NOJUDGEMENT: guestJudgement = AuthGuestNoJudgement;   break;
                            case VMMDEV_CREDENTIALS_JUDGE_OK:          guestJudgement = AuthGuestAccessGranted; break;
                            default:
                                LogFlowFunc(("Invalid guest flags %#08x!!!\n", u32GuestFlags));
                                break;
                        }
                    }
                    else
                        LogFlowFunc(("Wait for credentials judgement vrc = %Rrc!!!\n", vrc));
                    LogFlowFunc(("Guest judgement %d\n", guestJudgement));
                }
                else
                    LogFlowFunc(("Could not set credentials vrc = %Rrc!!!\n", vrc));
            }

            if (authType == AuthType_External)
            {
                LogRel(("AUTH: Guest judgement %d.\n", guestJudgement));
                LogFlowFunc(("External auth called again with guest judgement = %d\n", guestJudgement));
                result = mConsoleVRDPServer->Authenticate(uuid, guestJudgement, pszUser, pszPassword, pszDomain, u32ClientId);
            }
            else
            {
                switch (guestJudgement)
                {
                    case AuthGuestAccessGranted:
                        result = AuthResultAccessGranted;
                        break;
                    default:
                        result = AuthResultAccessDenied;
                        break;
                }
            }
            break;
        }

        default:
            AssertFailed();
    }

    LogFlowFunc(("Result = %d\n", result));
    LogFlowFuncLeave();

    if (result != AuthResultAccessGranted)
    {
        /* Reject. */
        LogRel(("AUTH: Access denied.\n"));
        return VERR_ACCESS_DENIED;
    }

    LogRel(("AUTH: Access granted.\n"));

    /* Multiconnection check must be made after authentication, so bad clients would not interfere with a good one. */
    BOOL allowMultiConnection = FALSE;
    hrc = mVRDEServer->COMGETTER(AllowMultiConnection)(&allowMultiConnection);
    AssertComRCReturn(hrc, VERR_ACCESS_DENIED);

    BOOL reuseSingleConnection = FALSE;
    hrc = mVRDEServer->COMGETTER(ReuseSingleConnection)(&reuseSingleConnection);
    AssertComRCReturn(hrc, VERR_ACCESS_DENIED);

    LogFlowFunc(("allowMultiConnection %d, reuseSingleConnection = %d, mcVRDPClients = %d, mu32SingleRDPClientId = %d\n",
                 allowMultiConnection, reuseSingleConnection, mcVRDPClients, mu32SingleRDPClientId));

    if (allowMultiConnection == FALSE)
    {
        /* Note: the 'mcVRDPClients' variable is incremented in ClientConnect callback, which is called when the client
         * is successfully connected, that is after the ClientLogon callback. Therefore the mcVRDPClients
         * value is 0 for first client.
         */
        if (mcVRDPClients != 0)
        {
            Assert(mcVRDPClients == 1);
            /* There is a client already.
             * If required drop the existing client connection and let the connecting one in.
             */
            if (reuseSingleConnection)
            {
                LogRel(("AUTH: Multiple connections are not enabled. Disconnecting existing client.\n"));
                mConsoleVRDPServer->DisconnectClient(mu32SingleRDPClientId, false);
            }
            else
            {
                /* Reject. */
                LogRel(("AUTH: Multiple connections are not enabled. Access denied.\n"));
                return VERR_ACCESS_DENIED;
            }
        }

        /* Save the connected client id. From now on it will be necessary to disconnect this one. */
        mu32SingleRDPClientId = u32ClientId;
    }

#ifdef VBOX_WITH_GUEST_PROPS
    i_guestPropertiesVRDPUpdateLogon(u32ClientId, pszUser, pszDomain);
#endif /* VBOX_WITH_GUEST_PROPS */

    /* Check if the successfully verified credentials are to be sent to the guest. */
    BOOL fProvideGuestCredentials = FALSE;

    Bstr value;
    hrc = mMachine->GetExtraData(Bstr("VRDP/ProvideGuestCredentials").raw(),
                                 value.asOutParam());
    if (SUCCEEDED(hrc) && value == "1")
    {
        /* Provide credentials only if there are no logged in users. */
        Utf8Str noLoggedInUsersValue;
        LONG64 ul64Timestamp = 0;
        Utf8Str flags;

        hrc = i_getGuestProperty("/VirtualBox/GuestInfo/OS/NoLoggedInUsers",
                                 &noLoggedInUsersValue, &ul64Timestamp, &flags);

        if (SUCCEEDED(hrc) && noLoggedInUsersValue != "false")
        {
            /* And only if there are no connected clients. */
            if (ASMAtomicCmpXchgBool(&mcGuestCredentialsProvided, true, false))
            {
                fProvideGuestCredentials = TRUE;
            }
        }
    }

    /** @todo r=dj locking required here for m_pVMMDev? */
    if (   fProvideGuestCredentials
        && m_pVMMDev)
    {
        uint32_t u32GuestFlags = VMMDEV_SETCREDENTIALS_GUESTLOGON;

        PPDMIVMMDEVPORT pDevPort = m_pVMMDev->getVMMDevPort();
        if (pDevPort)
        {
            int vrc = pDevPort->pfnSetCredentials(m_pVMMDev->getVMMDevPort(), pszUser, pszPassword, pszDomain, u32GuestFlags);
            AssertRC(vrc);
        }
    }

    return VINF_SUCCESS;
}

void Console::i_VRDPClientStatusChange(uint32_t u32ClientId, const char *pszStatus)
{
    LogFlowFuncEnter();

    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    LogFlowFunc(("%s\n", pszStatus));

#ifdef VBOX_WITH_GUEST_PROPS
    /* Parse the status string. */
    if (RTStrICmp(pszStatus, "ATTACH") == 0)
    {
        i_guestPropertiesVRDPUpdateClientAttach(u32ClientId, true);
    }
    else if (RTStrICmp(pszStatus, "DETACH") == 0)
    {
        i_guestPropertiesVRDPUpdateClientAttach(u32ClientId, false);
    }
    else if (RTStrNICmp(pszStatus, "NAME=", strlen("NAME=")) == 0)
    {
        i_guestPropertiesVRDPUpdateNameChange(u32ClientId, pszStatus + strlen("NAME="));
    }
    else if (RTStrNICmp(pszStatus, "CIPA=", strlen("CIPA=")) == 0)
    {
        i_guestPropertiesVRDPUpdateIPAddrChange(u32ClientId, pszStatus + strlen("CIPA="));
    }
    else if (RTStrNICmp(pszStatus, "CLOCATION=", strlen("CLOCATION=")) == 0)
    {
        i_guestPropertiesVRDPUpdateLocationChange(u32ClientId, pszStatus + strlen("CLOCATION="));
    }
    else if (RTStrNICmp(pszStatus, "COINFO=", strlen("COINFO=")) == 0)
    {
        i_guestPropertiesVRDPUpdateOtherInfoChange(u32ClientId, pszStatus + strlen("COINFO="));
    }
#endif

    LogFlowFuncLeave();
}

void Console::i_VRDPClientConnect(uint32_t u32ClientId)
{
    LogFlowFuncEnter();

    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    uint32_t u32Clients = ASMAtomicIncU32(&mcVRDPClients);
    VMMDev *pDev;
    PPDMIVMMDEVPORT pPort;
    if (    (u32Clients == 1)
         && ((pDev = i_getVMMDev()))
         && ((pPort = pDev->getVMMDevPort()))
       )
    {
        pPort->pfnVRDPChange(pPort,
                             true,
                             VRDP_EXPERIENCE_LEVEL_FULL); /** @todo configurable */
    }

    NOREF(u32ClientId);
    mDisplay->i_VRDPConnectionEvent(true);

#ifdef VBOX_WITH_GUEST_PROPS
    i_guestPropertiesVRDPUpdateActiveClient(u32ClientId);
#endif /* VBOX_WITH_GUEST_PROPS */

    LogFlowFuncLeave();
    return;
}

void Console::i_VRDPClientDisconnect(uint32_t u32ClientId,
                                     uint32_t fu32Intercepted)
{
    LogFlowFuncEnter();

    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AssertReturnVoid(mConsoleVRDPServer);

    uint32_t u32Clients = ASMAtomicDecU32(&mcVRDPClients);
    VMMDev *pDev;
    PPDMIVMMDEVPORT pPort;

    if (    (u32Clients == 0)
         && ((pDev = i_getVMMDev()))
         && ((pPort = pDev->getVMMDevPort()))
       )
    {
        pPort->pfnVRDPChange(pPort,
                             false,
                             0);
    }

    mDisplay->i_VRDPConnectionEvent(false);

    if (fu32Intercepted & VRDE_CLIENT_INTERCEPT_USB)
    {
        mConsoleVRDPServer->USBBackendDelete(u32ClientId);
    }

    if (fu32Intercepted & VRDE_CLIENT_INTERCEPT_CLIPBOARD)
    {
        mConsoleVRDPServer->ClipboardDelete(u32ClientId);
    }

#ifdef VBOX_WITH_AUDIO_VRDE
    if (fu32Intercepted & VRDE_CLIENT_INTERCEPT_AUDIO)
    {
        if (mAudioVRDE)
            mAudioVRDE->onVRDEControl(false /* fEnable */, 0 /* uFlags */);
    }
#endif

    AuthType_T authType = AuthType_Null;
    HRESULT hrc = mVRDEServer->COMGETTER(AuthType)(&authType);
    AssertComRC(hrc);

    if (authType == AuthType_External)
        mConsoleVRDPServer->AuthDisconnect(i_getId(), u32ClientId);

#ifdef VBOX_WITH_GUEST_PROPS
    i_guestPropertiesVRDPUpdateDisconnect(u32ClientId);
    if (u32Clients == 0)
        i_guestPropertiesVRDPUpdateActiveClient(0);
#endif /* VBOX_WITH_GUEST_PROPS */

    if (u32Clients == 0)
        mcGuestCredentialsProvided = false;

    LogFlowFuncLeave();
    return;
}

void Console::i_VRDPInterceptAudio(uint32_t u32ClientId)
{
    RT_NOREF(u32ClientId);
    LogFlowFuncEnter();

    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    LogFlowFunc(("u32ClientId=%RU32\n", u32ClientId));

#ifdef VBOX_WITH_AUDIO_VRDE
    if (mAudioVRDE)
        mAudioVRDE->onVRDEControl(true /* fEnable */, 0 /* uFlags */);
#endif

    LogFlowFuncLeave();
    return;
}

void Console::i_VRDPInterceptUSB(uint32_t u32ClientId, void **ppvIntercept)
{
    LogFlowFuncEnter();

    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AssertReturnVoid(mConsoleVRDPServer);

    mConsoleVRDPServer->USBBackendCreate(u32ClientId, ppvIntercept);

    LogFlowFuncLeave();
    return;
}

void Console::i_VRDPInterceptClipboard(uint32_t u32ClientId)
{
    LogFlowFuncEnter();

    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AssertReturnVoid(mConsoleVRDPServer);

    mConsoleVRDPServer->ClipboardCreate(u32ClientId);

    LogFlowFuncLeave();
    return;
}


//static
const char *Console::sSSMConsoleUnit = "ConsoleData";
/** The saved state version.  */
#define CONSOLE_SAVED_STATE_VERSION                         UINT32_C(0x00010002)
/** The saved state version, pre shared folder autoMountPoint.  */
#define CONSOLE_SAVED_STATE_VERSION_PRE_AUTO_MOUNT_POINT    UINT32_C(0x00010001)

inline static const char *networkAdapterTypeToName(NetworkAdapterType_T adapterType)
{
    switch (adapterType)
    {
        case NetworkAdapterType_Am79C970A:
        case NetworkAdapterType_Am79C973:
        case NetworkAdapterType_Am79C960:
            return "pcnet";
#ifdef VBOX_WITH_E1000
        case NetworkAdapterType_I82540EM:
        case NetworkAdapterType_I82543GC:
        case NetworkAdapterType_I82545EM:
            return "e1000";
#endif
#ifdef VBOX_WITH_VIRTIO
        case NetworkAdapterType_Virtio:
            return "virtio-net";
#endif
        case NetworkAdapterType_NE1000:
        case NetworkAdapterType_NE2000:
        case NetworkAdapterType_WD8003:
        case NetworkAdapterType_WD8013:
        case NetworkAdapterType_ELNK2:
            return "dp8390";
        case NetworkAdapterType_ELNK1:
            return "3c501";
        default:
            AssertFailed();
            return "unknown";
    }
    /* not reached */
}

/**
 * Loads various console data stored in the saved state file.
 *
 * This method does validation of the state file and returns an error info
 * when appropriate.
 *
 * The method does nothing if the machine is not in the Saved file or if
 * console data from it has already been loaded.
 *
 * @note The caller must lock this object for writing.
 */
HRESULT Console::i_loadDataFromSavedState()
{
    if (   (   mMachineState != MachineState_Saved
            && mMachineState != MachineState_AbortedSaved)
        || mSavedStateDataLoaded)
        return S_OK;

    Bstr bstrSavedStateFile;
    HRESULT hrc = mMachine->COMGETTER(StateFilePath)(bstrSavedStateFile.asOutParam());
    if (SUCCEEDED(hrc))
    {
        Bstr bstrStateKeyId;
        hrc = mMachine->COMGETTER(StateKeyId)(bstrStateKeyId.asOutParam());
        if (SUCCEEDED(hrc))
        {
            Bstr bstrStateKeyStore;
            hrc = mMachine->COMGETTER(StateKeyStore)(bstrStateKeyStore.asOutParam());
            if (SUCCEEDED(hrc))
            {
                Utf8Str const strSavedStateFile(bstrSavedStateFile);

                PCVMMR3VTABLE pVMM = mpVMM;
                AssertPtrReturn(pVMM, E_UNEXPECTED);

                PSSMHANDLE pSSM;
                SsmStream ssmStream(this, pVMM, m_pKeyStore, bstrStateKeyId, bstrStateKeyStore);

                int vrc = ssmStream.open(strSavedStateFile.c_str(), false /*fWrite*/, &pSSM);
                if (RT_SUCCESS(vrc))
                {
                    uint32_t uVersion = 0;
                    vrc = pVMM->pfnSSMR3Seek(pSSM, sSSMConsoleUnit, 0 /* iInstance */, &uVersion);
                    /** @todo r=bird: This version check is premature, so the logic here is
                     * buggered as we won't ignore VERR_SSM_UNIT_NOT_FOUND as seems to be
                     * intended. Sigh. */
                    if (SSM_VERSION_MAJOR(uVersion) == SSM_VERSION_MAJOR(CONSOLE_SAVED_STATE_VERSION))
                    {
                        if (RT_SUCCESS(vrc))
                            try
                            {
                                vrc = i_loadStateFileExecInternal(pSSM, pVMM, uVersion);
                            }
                            catch (std::bad_alloc &)
                            {
                                vrc = VERR_NO_MEMORY;
                            }
                        else if (vrc == VERR_SSM_UNIT_NOT_FOUND)
                            vrc = VINF_SUCCESS;
                    }
                    else
                        vrc = VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

                    ssmStream.close();
                }

                if (RT_FAILURE(vrc))
                    hrc = setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                       tr("The saved state file '%s' is invalid (%Rrc). Delete the saved state and try again"),
                                       strSavedStateFile.c_str(), vrc);

                mSavedStateDataLoaded = true;
            }
        }
    }

    return hrc;
}

/**
 * Callback handler to save various console data to the state file,
 * called when the user saves the VM state.
 *
 * @returns VBox status code.
 * @param   pSSM      SSM handle.
 * @param   pVMM      The VMM ring-3 vtable.
 * @param   pvUser    Pointer to Console
 *
 * @note    Locks the Console object for reading.
 */
/*static*/ DECLCALLBACK(int)
Console::i_saveStateFileExec(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, void *pvUser)
{
    LogFlowFunc(("\n"));

    Console *pThat = static_cast<Console *>(pvUser);
    AssertReturn(pThat, VERR_INVALID_POINTER);

    AutoCaller autoCaller(pThat);
    AssertComRCReturn(autoCaller.hrc(), VERR_INVALID_STATE);

    AutoReadLock alock(pThat COMMA_LOCKVAL_SRC_POS);

    pVMM->pfnSSMR3PutU32(pSSM, (uint32_t)pThat->m_mapSharedFolders.size());

    for (SharedFolderMap::const_iterator it = pThat->m_mapSharedFolders.begin();
         it != pThat->m_mapSharedFolders.end();
         ++it)
    {
        ConsoleSharedFolder *pSF = (*it).second;
        AutoCaller sfCaller(pSF);
        AutoReadLock sfLock(pSF COMMA_LOCKVAL_SRC_POS);

        const Utf8Str &name = pSF->i_getName();
        pVMM->pfnSSMR3PutU32(pSSM, (uint32_t)name.length() + 1 /* term. 0 */);
        pVMM->pfnSSMR3PutStrZ(pSSM, name.c_str());

        const Utf8Str &hostPath = pSF->i_getHostPath();
        pVMM->pfnSSMR3PutU32(pSSM, (uint32_t)hostPath.length() + 1 /* term. 0 */);
        pVMM->pfnSSMR3PutStrZ(pSSM, hostPath.c_str());

        pVMM->pfnSSMR3PutBool(pSSM, !!pSF->i_isWritable());
        pVMM->pfnSSMR3PutBool(pSSM, !!pSF->i_isAutoMounted());

        const Utf8Str &rStrAutoMountPoint = pSF->i_getAutoMountPoint();
        pVMM->pfnSSMR3PutU32(pSSM, (uint32_t)rStrAutoMountPoint.length() + 1 /* term. 0 */);
        pVMM->pfnSSMR3PutStrZ(pSSM, rStrAutoMountPoint.c_str());
    }

    return VINF_SUCCESS;
}

/**
 * Callback handler to load various console data from the state file.
 *
 * Called when the VM is being restored from the saved state.
 *
 * @returns VBox status code.
 * @param   pSSM         SSM handle.
 * @param   pVMM         The VMM ring-3 vtable.
 * @param   pvUser       pointer to Console
 * @param   uVersion     Console unit version. Should match sSSMConsoleVer.
 * @param   uPass        The data pass.
 */
//static
DECLCALLBACK(int)
Console::i_loadStateFileExec(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, void *pvUser, uint32_t uVersion, uint32_t uPass)
{
    LogFlowFunc(("uVersion=%#x uPass=%#x\n", uVersion, uPass));
    Assert(uPass == SSM_PASS_FINAL); RT_NOREF_PV(uPass);

    if (SSM_VERSION_MAJOR_CHANGED(uVersion, CONSOLE_SAVED_STATE_VERSION))
        return VERR_VERSION_MISMATCH;

    Console *pThat = static_cast<Console *>(pvUser);
    AssertReturn(pThat, VERR_INVALID_PARAMETER);

    /* Currently, nothing to do when we've been called from VMR3Load*. */
    return pVMM->pfnSSMR3SkipToEndOfUnit(pSSM);
}

/**
 * Method to load various console data from the state file.
 *
 * Called from #i_loadDataFromSavedState.
 *
 * @param pSSM         SSM handle.
 * @param pVMM         The VMM vtable.
 * @param u32Version   Console unit version.
 *                     Should match sSSMConsoleVer.
 *
 * @note Locks the Console object for writing.
 */
int Console::i_loadStateFileExecInternal(PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, uint32_t u32Version)
{
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), VERR_ACCESS_DENIED);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    AssertReturn(m_mapSharedFolders.empty(), VERR_INTERNAL_ERROR);

    uint32_t size = 0;
    int vrc = pVMM->pfnSSMR3GetU32(pSSM, &size);
    AssertRCReturn(vrc, vrc);

    for (uint32_t i = 0; i < size; ++i)
    {
        Utf8Str strName;
        Utf8Str strHostPath;
        bool writable = true;
        bool autoMount = false;

        uint32_t cbStr = 0;
        char *buf = NULL;

        vrc = pVMM->pfnSSMR3GetU32(pSSM, &cbStr);
        AssertRCReturn(vrc, vrc);
        buf = new char[cbStr];
        vrc = pVMM->pfnSSMR3GetStrZ(pSSM, buf, cbStr);
        AssertRC(vrc);
        strName = buf;
        delete[] buf;

        vrc = pVMM->pfnSSMR3GetU32(pSSM, &cbStr);
        AssertRCReturn(vrc, vrc);
        buf = new char[cbStr];
        vrc = pVMM->pfnSSMR3GetStrZ(pSSM, buf, cbStr);
        AssertRC(vrc);
        strHostPath = buf;
        delete[] buf;

        if (u32Version >= CONSOLE_SAVED_STATE_VERSION_PRE_AUTO_MOUNT_POINT)
            pVMM->pfnSSMR3GetBool(pSSM, &writable);

        if (   u32Version >= CONSOLE_SAVED_STATE_VERSION_PRE_AUTO_MOUNT_POINT
#ifndef VBOX_OSE /* This broke saved state when introduced in r63916 (4.0). */
            && pVMM->pfnSSMR3HandleRevision(pSSM) >= 63916
#endif
           )
            pVMM->pfnSSMR3GetBool(pSSM, &autoMount);

        Utf8Str strAutoMountPoint;
        if (u32Version >= CONSOLE_SAVED_STATE_VERSION)
        {
            vrc = pVMM->pfnSSMR3GetU32(pSSM, &cbStr);
            AssertRCReturn(vrc, vrc);
            vrc = strAutoMountPoint.reserveNoThrow(cbStr);
            AssertRCReturn(vrc, vrc);
            vrc = pVMM->pfnSSMR3GetStrZ(pSSM, strAutoMountPoint.mutableRaw(), cbStr);
            AssertRCReturn(vrc, vrc);
            strAutoMountPoint.jolt();
        }

        ComObjPtr<ConsoleSharedFolder> pSharedFolder;
        pSharedFolder.createObject();
        HRESULT hrc = pSharedFolder->init(this,
                                          strName,
                                          strHostPath,
                                          writable,
                                          autoMount,
                                          strAutoMountPoint,
                                          false /* fFailOnError */);
        AssertComRCReturn(hrc, VERR_INTERNAL_ERROR);

        m_mapSharedFolders.insert(std::make_pair(strName, pSharedFolder));
    }

    return VINF_SUCCESS;
}

#ifdef VBOX_WITH_GUEST_PROPS

// static
DECLCALLBACK(int) Console::i_doGuestPropNotification(void *pvExtension,
                                                     uint32_t u32Function,
                                                     void *pvParms,
                                                     uint32_t cbParms)
{
    Assert(u32Function == 0); NOREF(u32Function);

    /*
     * No locking, as this is purely a notification which does not make any
     * changes to the object state.
     */
    PGUESTPROPHOSTCALLBACKDATA pCBData = reinterpret_cast<PGUESTPROPHOSTCALLBACKDATA>(pvParms);
    AssertReturn(sizeof(GUESTPROPHOSTCALLBACKDATA) == cbParms, VERR_INVALID_PARAMETER);
    AssertReturn(pCBData->u32Magic == GUESTPROPHOSTCALLBACKDATA_MAGIC, VERR_INVALID_PARAMETER);
    LogFlow(("Console::doGuestPropNotification: pCBData={.pcszName=%s, .pcszValue=%s, .pcszFlags=%s}\n",
             pCBData->pcszName, pCBData->pcszValue, pCBData->pcszFlags));

    Bstr name(pCBData->pcszName);
    Bstr value(pCBData->pcszValue);
    Bstr flags(pCBData->pcszFlags);
    BOOL fWasDeleted = !pCBData->pcszValue;
    ComObjPtr<Console> pConsole = reinterpret_cast<Console *>(pvExtension);
    HRESULT hrc = pConsole->mControl->PushGuestProperty(name.raw(),
                                                        value.raw(),
                                                        pCBData->u64Timestamp,
                                                        flags.raw(),
                                                        fWasDeleted);
    if (SUCCEEDED(hrc))
    {
        ::FireGuestPropertyChangedEvent(pConsole->mEventSource, pConsole->i_getId().raw(), name.raw(), value.raw(), flags.raw(),
                                        fWasDeleted);
        return VINF_SUCCESS;
    }
    LogFlow(("Console::doGuestPropNotification: hrc=%Rhrc pCBData={.pcszName=%s, .pcszValue=%s, .pcszFlags=%s}\n",
             hrc, pCBData->pcszName, pCBData->pcszValue, pCBData->pcszFlags));
    return Global::vboxStatusCodeFromCOM(hrc);
}

HRESULT Console::i_doEnumerateGuestProperties(const Utf8Str &aPatterns,
                                              std::vector<Utf8Str> &aNames,
                                              std::vector<Utf8Str> &aValues,
                                              std::vector<LONG64>  &aTimestamps,
                                              std::vector<Utf8Str> &aFlags)
{
    AssertReturn(m_pVMMDev, E_FAIL);

    VBOXHGCMSVCPARM parm[3];
    parm[0].type = VBOX_HGCM_SVC_PARM_PTR;
    parm[0].u.pointer.addr = (void*)aPatterns.c_str();
    parm[0].u.pointer.size = (uint32_t)aPatterns.length() + 1;

    /*
     * Now things get slightly complicated. Due to a race with the guest adding
     * properties, there is no good way to know how much to enlarge a buffer for
     * the service to enumerate into. We choose a decent starting size and loop a
     * few times, each time retrying with the size suggested by the service plus
     * one Kb.
     */
    size_t cchBuf = 4096;
    Utf8Str Utf8Buf;
    int vrc = VERR_BUFFER_OVERFLOW;
    for (unsigned i = 0; i < 10 && (VERR_BUFFER_OVERFLOW == vrc); ++i)
    {
        try
        {
            Utf8Buf.reserve(cchBuf + 1024);
        }
        catch(...)
        {
            return E_OUTOFMEMORY;
        }

        parm[1].type = VBOX_HGCM_SVC_PARM_PTR;
        parm[1].u.pointer.addr = Utf8Buf.mutableRaw();
        parm[1].u.pointer.size = (uint32_t)cchBuf + 1024;

        parm[2].type = VBOX_HGCM_SVC_PARM_32BIT;
        parm[2].u.uint32 = 0;

        vrc = m_pVMMDev->hgcmHostCall("VBoxGuestPropSvc", GUEST_PROP_FN_HOST_ENUM_PROPS, 3, &parm[0]);
        Utf8Buf.jolt();
        if (parm[2].type != VBOX_HGCM_SVC_PARM_32BIT)
            return setErrorBoth(E_FAIL, vrc, tr("Internal application error"));
        cchBuf = parm[2].u.uint32;
    }
    if (vrc == VERR_BUFFER_OVERFLOW)
        return setError(E_UNEXPECTED, tr("Temporary failure due to guest activity, please retry"));

    /*
     * Finally we have to unpack the data returned by the service into the safe
     * arrays supplied by the caller. We start by counting the number of entries.
     */
    const char *pszBuf
        = reinterpret_cast<const char *>(parm[1].u.pointer.addr);
    unsigned cEntries = 0;
    /* The list is terminated by a zero-length string at the end of a set
     * of four strings. */
    for (size_t i = 0; strlen(pszBuf + i) != 0; )
    {
       /* We are counting sets of four strings. */
       for (unsigned j = 0; j < 4; ++j)
           i += strlen(pszBuf + i) + 1;
       ++cEntries;
    }

    aNames.resize(cEntries);
    aValues.resize(cEntries);
    aTimestamps.resize(cEntries);
    aFlags.resize(cEntries);

    size_t iBuf = 0;
    /* Rely on the service to have formated the data correctly. */
    for (unsigned i = 0; i < cEntries; ++i)
    {
        size_t cchName = strlen(pszBuf + iBuf);
        aNames[i] = &pszBuf[iBuf];
        iBuf += cchName + 1;

        size_t cchValue = strlen(pszBuf + iBuf);
        aValues[i] = &pszBuf[iBuf];
        iBuf += cchValue + 1;

        size_t cchTimestamp = strlen(pszBuf + iBuf);
        aTimestamps[i] = RTStrToUInt64(&pszBuf[iBuf]);
        iBuf += cchTimestamp + 1;

        size_t cchFlags = strlen(pszBuf + iBuf);
        aFlags[i] = &pszBuf[iBuf];
        iBuf += cchFlags + 1;
    }

    return S_OK;
}

#endif /* VBOX_WITH_GUEST_PROPS */


// IConsole properties
/////////////////////////////////////////////////////////////////////////////
HRESULT Console::getMachine(ComPtr<IMachine> &aMachine)
{
    /* mMachine is constant during life time, no need to lock */
    mMachine.queryInterfaceTo(aMachine.asOutParam());

    /* callers expect to get a valid reference, better fail than crash them */
    if (mMachine.isNull())
        return E_FAIL;

    return S_OK;
}

HRESULT Console::getState(MachineState_T *aState)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* we return our local state (since it's always the same as on the server) */
    *aState = mMachineState;

    return S_OK;
}

HRESULT Console::getGuest(ComPtr<IGuest> &aGuest)
{
    /* mGuest is constant during life time, no need to lock */
    mGuest.queryInterfaceTo(aGuest.asOutParam());

    return S_OK;
}

HRESULT Console::getKeyboard(ComPtr<IKeyboard> &aKeyboard)
{
    /* mKeyboard is constant during life time, no need to lock */
    mKeyboard.queryInterfaceTo(aKeyboard.asOutParam());

    return S_OK;
}

HRESULT Console::getMouse(ComPtr<IMouse> &aMouse)
{
    /* mMouse is constant during life time, no need to lock */
    mMouse.queryInterfaceTo(aMouse.asOutParam());

    return S_OK;
}

HRESULT Console::getDisplay(ComPtr<IDisplay> &aDisplay)
{
    /* mDisplay is constant during life time, no need to lock */
    mDisplay.queryInterfaceTo(aDisplay.asOutParam());

    return S_OK;
}

HRESULT Console::getDebugger(ComPtr<IMachineDebugger> &aDebugger)
{
    /* we need a write lock because of the lazy mDebugger initialization*/
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* check if we have to create the debugger object */
    if (!mDebugger)
    {
        unconst(mDebugger).createObject();
        mDebugger->init(this);
    }

    mDebugger.queryInterfaceTo(aDebugger.asOutParam());

    return S_OK;
}

HRESULT Console::getUSBDevices(std::vector<ComPtr<IUSBDevice> > &aUSBDevices)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    size_t i = 0;
    aUSBDevices.resize(mUSBDevices.size());
    for (USBDeviceList::const_iterator it = mUSBDevices.begin(); it != mUSBDevices.end(); ++i, ++it)
        (*it).queryInterfaceTo(aUSBDevices[i].asOutParam());

    return S_OK;
}


HRESULT Console::getRemoteUSBDevices(std::vector<ComPtr<IHostUSBDevice> > &aRemoteUSBDevices)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    size_t i = 0;
    aRemoteUSBDevices.resize(mRemoteUSBDevices.size());
    for (RemoteUSBDeviceList::const_iterator it = mRemoteUSBDevices.begin(); it != mRemoteUSBDevices.end(); ++i, ++it)
        (*it).queryInterfaceTo(aRemoteUSBDevices[i].asOutParam());

    return S_OK;
}

HRESULT Console::getVRDEServerInfo(ComPtr<IVRDEServerInfo> &aVRDEServerInfo)
{
    /* mVRDEServerInfo is constant during life time, no need to lock */
    mVRDEServerInfo.queryInterfaceTo(aVRDEServerInfo.asOutParam());

    return S_OK;
}

HRESULT Console::getEmulatedUSB(ComPtr<IEmulatedUSB> &aEmulatedUSB)
{
    /* mEmulatedUSB is constant during life time, no need to lock */
    mEmulatedUSB.queryInterfaceTo(aEmulatedUSB.asOutParam());

    return S_OK;
}

HRESULT Console::getSharedFolders(std::vector<ComPtr<ISharedFolder> > &aSharedFolders)
{
    /* loadDataFromSavedState() needs a write lock */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Read console data stored in the saved state file (if not yet done) */
    HRESULT hrc = i_loadDataFromSavedState();
    if (FAILED(hrc))
        return hrc;

    size_t i = 0;
    aSharedFolders.resize(m_mapSharedFolders.size());
    for (SharedFolderMap::const_iterator it = m_mapSharedFolders.begin(); it != m_mapSharedFolders.end(); ++i, ++it)
        (it)->second.queryInterfaceTo(aSharedFolders[i].asOutParam());

    return S_OK;
}

HRESULT Console::getEventSource(ComPtr<IEventSource> &aEventSource)
{
    // no need to lock - lifetime constant
    mEventSource.queryInterfaceTo(aEventSource.asOutParam());

    return S_OK;
}

HRESULT Console::getAttachedPCIDevices(std::vector<ComPtr<IPCIDeviceAttachment> > &aAttachedPCIDevices)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mBusMgr)
    {
        std::vector<BusAssignmentManager::PCIDeviceInfo> devInfos;
        mBusMgr->listAttachedPCIDevices(devInfos);
        ComObjPtr<PCIDeviceAttachment> dev;
        aAttachedPCIDevices.resize(devInfos.size());
        for (size_t i = 0; i < devInfos.size(); i++)
        {
            const BusAssignmentManager::PCIDeviceInfo &devInfo = devInfos[i];
            dev.createObject();
            dev->init(NULL, devInfo.strDeviceName,
                      devInfo.hostAddress.valid() ? devInfo.hostAddress.asLong() : -1,
                      devInfo.guestAddress.asLong(),
                      devInfo.hostAddress.valid());
            dev.queryInterfaceTo(aAttachedPCIDevices[i].asOutParam());
        }
    }
    else
        aAttachedPCIDevices.resize(0);

    return S_OK;
}

HRESULT Console::getUseHostClipboard(BOOL *aUseHostClipboard)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    *aUseHostClipboard = mfUseHostClipboard;

    return S_OK;
}

HRESULT Console::setUseHostClipboard(BOOL aUseHostClipboard)
{
    if (mfUseHostClipboard != RT_BOOL(aUseHostClipboard))
    {
        mfUseHostClipboard = RT_BOOL(aUseHostClipboard);
        LogRel(("Shared Clipboard: %s using host clipboard\n", mfUseHostClipboard ? "Enabled" : "Disabled"));
    }

    return S_OK;
}

// IConsole methods
/////////////////////////////////////////////////////////////////////////////

HRESULT Console::powerUp(ComPtr<IProgress> &aProgress)
{
    return i_powerUp(aProgress.asOutParam(), false /* aPaused */);
}

HRESULT Console::powerUpPaused(ComPtr<IProgress> &aProgress)
{
    return i_powerUp(aProgress.asOutParam(), true /* aPaused */);
}

HRESULT Console::powerDown(ComPtr<IProgress> &aProgress)
{
    LogFlowThisFuncEnter();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogFlowThisFunc(("mMachineState=%d\n", mMachineState));
    switch (mMachineState)
    {
        case MachineState_Running:
        case MachineState_Paused:
        case MachineState_Stuck:
            break;

        /* Try cancel the save state. */
        case MachineState_Saving:
            if (!mptrCancelableProgress.isNull())
            {
                HRESULT hrc = mptrCancelableProgress->Cancel();
                if (SUCCEEDED(hrc))
                    break;
            }
            return setError(VBOX_E_INVALID_VM_STATE, tr("Cannot power down at this point during a save state"));

        /* Try cancel the teleportation. */
        case MachineState_Teleporting:
        case MachineState_TeleportingPausedVM:
            if (!mptrCancelableProgress.isNull())
            {
                HRESULT hrc = mptrCancelableProgress->Cancel();
                if (SUCCEEDED(hrc))
                    break;
            }
            return setError(VBOX_E_INVALID_VM_STATE, tr("Cannot power down at this point in a teleportation"));

        /* Try cancel the online snapshot. */
        case MachineState_OnlineSnapshotting:
            if (!mptrCancelableProgress.isNull())
            {
                HRESULT hrc = mptrCancelableProgress->Cancel();
                if (SUCCEEDED(hrc))
                    break;
            }
            return setError(VBOX_E_INVALID_VM_STATE, tr("Cannot power down at this point in an online snapshot"));

        /* Try cancel the live snapshot. */
        case MachineState_LiveSnapshotting:
            if (!mptrCancelableProgress.isNull())
            {
                HRESULT hrc = mptrCancelableProgress->Cancel();
                if (SUCCEEDED(hrc))
                    break;
            }
            return setError(VBOX_E_INVALID_VM_STATE, tr("Cannot power down at this point in a live snapshot"));

        /* extra nice error message for a common case */
        case MachineState_Saved:
        case MachineState_AbortedSaved:
            return setError(VBOX_E_INVALID_VM_STATE, tr("Cannot power down a saved virtual machine"));
        case MachineState_Stopping:
            return setError(VBOX_E_INVALID_VM_STATE, tr("The virtual machine is being powered down"));
        default:
            return setError(VBOX_E_INVALID_VM_STATE,
                            tr("Invalid machine state: %s (must be Running, Paused or Stuck)"),
                            Global::stringifyMachineState(mMachineState));
    }
    LogFlowThisFunc(("Initiating SHUTDOWN request...\n"));

    /* memorize the current machine state */
    MachineState_T lastMachineState = mMachineState;

#ifdef VBOX_WITH_GUEST_PROPS
    if (mfTurnResetIntoPowerOff)
    {
        alock.release(); /** @todo r=bird: This code introduces a race condition wrt to the state.  This must be done elsewhere! */
        mMachine->DeleteGuestProperty(Bstr("/VirtualBox/HostInfo/VMPowerOffReason").raw());
        mMachine->SetGuestProperty(Bstr("/VirtualBox/HostInfo/VMPowerOffReason").raw(),
                                   Bstr("PowerOff").raw(), Bstr("RDONLYGUEST").raw());
        mMachine->SaveSettings();
        alock.acquire();
    }
#endif

    /*
     * Request a progress object from the server (this will set the machine state
     * to Stopping on the server to block others from accessing this machine).
     */
    ComPtr<IProgress> ptrProgress;
    HRESULT hrc = mControl->BeginPoweringDown(ptrProgress.asOutParam());
    if (SUCCEEDED(hrc))
    {
        /* Sync the state with the server: */
        i_setMachineStateLocally(MachineState_Stopping);

        /* Create the power down task: */
        VMPowerDownTask *pTask = NULL;
        try
        {
            pTask = new VMPowerDownTask(this, ptrProgress);
            if (!pTask->isOk())
            {
                hrc = setError(FAILED(pTask->hrc()) ? pTask->hrc() : E_FAIL, tr("Could not create VMPowerDownTask object\n"));
                delete(pTask);
                pTask = NULL;
            }
        }
        catch (std::bad_alloc &)
        {
            hrc = E_OUTOFMEMORY;
        }
        if (SUCCEEDED(hrc))
        {
            hrc = pTask->createThread();
            if (SUCCEEDED(hrc))
            {
                ptrProgress.queryInterfaceTo(aProgress.asOutParam());
                LogFlowThisFunc(("LEAVE: hrc=%Rhrc\n", hrc));
                return hrc;
            }
        }

        /*
         * Cancel the requested power down procedure.
         * This will reset the machine state to the state it had right
         * before calling mControl->BeginPoweringDown().
         */
        ErrorInfoKeeper eik;
        mControl->EndPoweringDown(eik.getResultCode(), eik.getText().raw());
        i_setMachineStateLocally(lastMachineState);
    }
    LogFlowThisFunc(("LEAVE: hrc=%Rhrc\n", hrc));
    return hrc;
}

HRESULT Console::reset()
{
    LogFlowThisFuncEnter();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogFlowThisFunc(("mMachineState=%d\n", mMachineState));
    if (   mMachineState != MachineState_Running
        && mMachineState != MachineState_Teleporting
        && mMachineState != MachineState_LiveSnapshotting
        /** @todo r=bird: This should be allowed on paused VMs as well. Later.  */
       )
        return i_setInvalidMachineStateError();

    /* protect mpUVM */
    SafeVMPtr ptrVM(this);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        /* release the lock before a VMR3* call (EMT might wait for it, @bugref{7648})! */
        alock.release();

        int vrc = ptrVM.vtable()->pfnVMR3Reset(ptrVM.rawUVM());

        hrc = RT_SUCCESS(vrc) ? S_OK : setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("Could not reset the machine (%Rrc)"), vrc);
    }

    LogFlowThisFunc(("mMachineState=%d, hrc=%Rhrc\n", mMachineState, hrc));
    LogFlowThisFuncLeave();
    return hrc;
}

/*static*/ DECLCALLBACK(int) Console::i_unplugCpu(Console *pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, VMCPUID idCpu)
{
    LogFlowFunc(("pThis=%p pVM=%p idCpu=%u\n", pThis, pUVM, idCpu));

    AssertReturn(pThis, VERR_INVALID_PARAMETER);

    int vrc = pVMM->pfnPDMR3DeviceDetach(pUVM, "acpi", 0, idCpu, 0);
    Log(("UnplugCpu: vrc=%Rrc\n", vrc));

    return vrc;
}

HRESULT Console::i_doCPURemove(ULONG aCpu, PUVM pUVM, PCVMMR3VTABLE pVMM)
{
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (FAILED(hrc))
        return hrc;

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogFlowThisFunc(("mMachineState=%d\n", mMachineState));
    AssertReturn(m_pVMMDev, E_FAIL);
    PPDMIVMMDEVPORT pVmmDevPort = m_pVMMDev->getVMMDevPort();
    AssertReturn(pVmmDevPort, E_FAIL);

    if (   mMachineState != MachineState_Running
        && mMachineState != MachineState_Teleporting
        && mMachineState != MachineState_LiveSnapshotting
       )
        return i_setInvalidMachineStateError();

    /* Check if the CPU is present */
    BOOL fCpuAttached;
    hrc = mMachine->GetCPUStatus(aCpu, &fCpuAttached);
    if (FAILED(hrc))
        return hrc;
    if (!fCpuAttached)
        return setError(E_FAIL, tr("CPU %d is not attached"), aCpu);

    /* Leave the lock before any EMT/VMMDev call. */
    alock.release();
    bool fLocked = true;

    /* Check if the CPU is unlocked */
    PPDMIBASE pBase;
    int vrc = pVMM->pfnPDMR3QueryDeviceLun(pUVM, "acpi", 0, aCpu, &pBase);
    if (RT_SUCCESS(vrc))
    {
        Assert(pBase);
        PPDMIACPIPORT pApicPort = PDMIBASE_QUERY_INTERFACE(pBase, PDMIACPIPORT);

        /* Notify the guest if possible. */
        uint32_t idCpuCore, idCpuPackage;
        vrc = pVMM->pfnVMR3GetCpuCoreAndPackageIdFromCpuId(pUVM, aCpu, &idCpuCore, &idCpuPackage); AssertRC(vrc);
        if (RT_SUCCESS(vrc))
            vrc = pVmmDevPort->pfnCpuHotUnplug(pVmmDevPort, idCpuCore, idCpuPackage);
        if (RT_SUCCESS(vrc))
        {
            unsigned cTries = 100;
            do
            {
                /* It will take some time until the event is processed in the guest. Wait...  */
                vrc = pApicPort ? pApicPort->pfnGetCpuStatus(pApicPort, aCpu, &fLocked) : VERR_INVALID_POINTER;
                if (RT_SUCCESS(vrc) && !fLocked)
                    break;

                /* Sleep a bit */
                RTThreadSleep(100);
            } while (cTries-- > 0);
        }
        else if (vrc == VERR_VMMDEV_CPU_HOTPLUG_NOT_MONITORED_BY_GUEST)
        {
            /* Query one time. It is possible that the user ejected the CPU. */
            vrc = pApicPort ? pApicPort->pfnGetCpuStatus(pApicPort, aCpu, &fLocked) : VERR_INVALID_POINTER;
        }
    }

    /* If the CPU was unlocked we can detach it now. */
    if (RT_SUCCESS(vrc) && !fLocked)
    {
        /*
         * Call worker on EMT #0, that's faster and safer than doing everything
         * using VMR3ReqCall.
         */
        PVMREQ pReq;
        vrc = pVMM->pfnVMR3ReqCallU(pUVM, 0, &pReq, 0 /* no wait! */, VMREQFLAGS_VBOX_STATUS, (PFNRT)i_unplugCpu, 4,
                                    this, pUVM, pVMM, (VMCPUID)aCpu);

        if (vrc == VERR_TIMEOUT)
            vrc = pVMM->pfnVMR3ReqWait(pReq, RT_INDEFINITE_WAIT);
        AssertRC(vrc);
        if (RT_SUCCESS(vrc))
            vrc = pReq->iStatus;
        pVMM->pfnVMR3ReqFree(pReq);

        if (RT_SUCCESS(vrc))
        {
            /* Detach it from the VM  */
            vrc = pVMM->pfnVMR3HotUnplugCpu(pUVM, aCpu);
            AssertRC(vrc);
        }
        else
           hrc = setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("Hot-Remove failed (vrc=%Rrc)"), vrc);
    }
    else
        hrc = setErrorBoth(VBOX_E_VM_ERROR, VERR_RESOURCE_BUSY,
                           tr("Hot-Remove was aborted because the CPU may still be used by the guest"), VERR_RESOURCE_BUSY);

    LogFlowThisFunc(("mMachineState=%d, hrc=%Rhrc\n", mMachineState, hrc));
    LogFlowThisFuncLeave();
    return hrc;
}

/*static*/ DECLCALLBACK(int) Console::i_plugCpu(Console *pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, VMCPUID idCpu)
{
    LogFlowFunc(("pThis=%p uCpu=%u\n", pThis, idCpu));
    RT_NOREF(pThis);

    int vrc = pVMM->pfnVMR3HotPlugCpu(pUVM, idCpu);
    AssertRC(vrc);

    /** @todo r=bird: Error handling here just sucks. */

    PCFGMNODE pInst = pVMM->pfnCFGMR3GetChild(pVMM->pfnCFGMR3GetRootU(pUVM), "Devices/acpi/0/");
    AssertRelease(pInst);
    /* nuke anything which might have been left behind. */
    pVMM->pfnCFGMR3RemoveNode(pVMM->pfnCFGMR3GetChildF(pInst, "LUN#%u", idCpu));

#define RC_CHECK() do { AssertReleaseRC(vrc); } while (0)

    PCFGMNODE pLunL0;
    PCFGMNODE pCfg;
    vrc = pVMM->pfnCFGMR3InsertNodeF(pInst, &pLunL0, "LUN#%u", idCpu);    RC_CHECK();
    vrc = pVMM->pfnCFGMR3InsertString(pLunL0, "Driver",       "ACPICpu"); RC_CHECK();
    vrc = pVMM->pfnCFGMR3InsertNode(pLunL0,   "Config",       &pCfg);     RC_CHECK();

    /*
     * Attach the driver.
     */
    PPDMIBASE pBase;
    vrc = pVMM->pfnPDMR3DeviceAttach(pUVM, "acpi", 0, idCpu, 0, &pBase); RC_CHECK();

    Log(("PlugCpu: vrc=%Rrc\n", vrc));

    pVMM->pfnCFGMR3Dump(pInst);

#undef RC_CHECK

    return VINF_SUCCESS;
}

HRESULT Console::i_doCPUAdd(ULONG aCpu, PUVM pUVM, PCVMMR3VTABLE pVMM)
{
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (FAILED(hrc))
        return hrc;

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogFlowThisFunc(("mMachineState=%d\n", mMachineState));
    if (   mMachineState != MachineState_Running
        && mMachineState != MachineState_Teleporting
        && mMachineState != MachineState_LiveSnapshotting
        /** @todo r=bird: This should be allowed on paused VMs as well. Later.  */
       )
        return i_setInvalidMachineStateError();

    AssertReturn(m_pVMMDev, E_FAIL);
    PPDMIVMMDEVPORT pDevPort = m_pVMMDev->getVMMDevPort();
    AssertReturn(pDevPort, E_FAIL);

    /* Check if the CPU is present */
    BOOL fCpuAttached;
    hrc = mMachine->GetCPUStatus(aCpu, &fCpuAttached);
    if (FAILED(hrc))
        return hrc;

    if (fCpuAttached)
        return setError(E_FAIL, tr("CPU %d is already attached"), aCpu);

    /*
     * Call worker on EMT #0, that's faster and safer than doing everything
     * using VMR3ReqCall. Note that we separate VMR3ReqCall from VMR3ReqWait
     * here to make requests from under the lock in order to serialize them.
     */
    PVMREQ pReq;
    int vrc = pVMM->pfnVMR3ReqCallU(pUVM, 0, &pReq, 0 /* no wait! */, VMREQFLAGS_VBOX_STATUS, (PFNRT)i_plugCpu, 4,
                                    this, pUVM, pVMM, aCpu);

    /* release the lock before a VMR3* call (EMT might wait for it, @bugref{7648})! */
    alock.release();

    if (vrc == VERR_TIMEOUT)
        vrc = pVMM->pfnVMR3ReqWait(pReq, RT_INDEFINITE_WAIT);
    AssertRC(vrc);
    if (RT_SUCCESS(vrc))
        vrc = pReq->iStatus;
    pVMM->pfnVMR3ReqFree(pReq);

    if (RT_SUCCESS(vrc))
    {
        /* Notify the guest if possible. */
        uint32_t idCpuCore, idCpuPackage;
        vrc = pVMM->pfnVMR3GetCpuCoreAndPackageIdFromCpuId(pUVM, aCpu, &idCpuCore, &idCpuPackage); AssertRC(vrc);
        if (RT_SUCCESS(vrc))
            vrc = pDevPort->pfnCpuHotPlug(pDevPort, idCpuCore, idCpuPackage);
        /** @todo warning if the guest doesn't support it */
    }
    else
        hrc = setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("Could not add CPU to the machine (%Rrc)"), vrc);

    LogFlowThisFunc(("mMachineState=%d, hrc=%Rhrc\n", mMachineState, hrc));
    LogFlowThisFuncLeave();
    return hrc;
}

HRESULT Console::pause()
{
    LogFlowThisFuncEnter();

    HRESULT hrc = i_pause(Reason_Unspecified);

    LogFlowThisFunc(("hrc=%Rhrc\n", hrc));
    LogFlowThisFuncLeave();
    return hrc;
}

HRESULT Console::resume()
{
    LogFlowThisFuncEnter();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mMachineState != MachineState_Paused)
        return setError(VBOX_E_INVALID_VM_STATE,
                        tr("Cannot resume the machine as it is not paused (machine state: %s)"),
                        Global::stringifyMachineState(mMachineState));

    HRESULT hrc = i_resume(Reason_Unspecified, alock);

    LogFlowThisFunc(("hrc=%Rhrc\n", hrc));
    LogFlowThisFuncLeave();
    return hrc;
}

HRESULT Console::powerButton()
{
    LogFlowThisFuncEnter();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (   mMachineState != MachineState_Running
        && mMachineState != MachineState_Teleporting
        && mMachineState != MachineState_LiveSnapshotting
       )
        return i_setInvalidMachineStateError();

    /* get the VM handle. */
    SafeVMPtr ptrVM(this);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        // no need to release lock, as there are no cross-thread callbacks

        /* get the acpi device interface and press the button. */
        PPDMIBASE pBase = NULL;
        int vrc = ptrVM.vtable()->pfnPDMR3QueryDeviceLun(ptrVM.rawUVM(), "acpi", 0, 0, &pBase);
        /** @todo r=aeichner Think about a prettier way to do this without relying on hardocded device/driver names. */
        if (vrc == VERR_PDM_DEVICE_INSTANCE_NOT_FOUND) /* Try GPIO device for ARM VMs */
            vrc = ptrVM.vtable()->pfnPDMR3QueryDriverOnLun(ptrVM.rawUVM(), "arm-pl061-gpio", 0, 0, "GpioButton", &pBase);
        if (RT_SUCCESS(vrc))
        {
            Assert(pBase);
            PPDMIEVENTBUTTONPORT pPort = PDMIBASE_QUERY_INTERFACE(pBase, PDMIEVENTBUTTONPORT);
            if (pPort)
                vrc = pPort->pfnPowerButtonPress(pPort);
            else
                vrc = VERR_PDM_MISSING_INTERFACE;
        }

        hrc = RT_SUCCESS(vrc) ? S_OK : setErrorBoth(VBOX_E_PDM_ERROR, vrc, tr("Controlled power off failed (%Rrc)"), vrc);
    }

    LogFlowThisFunc(("hrc=%Rhrc\n", hrc));
    LogFlowThisFuncLeave();
    return hrc;
}

HRESULT Console::getPowerButtonHandled(BOOL *aHandled)
{
    LogFlowThisFuncEnter();

    *aHandled = FALSE;

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (   mMachineState != MachineState_Running
        && mMachineState != MachineState_Teleporting
        && mMachineState != MachineState_LiveSnapshotting
       )
        return i_setInvalidMachineStateError();

    /* get the VM handle. */
    SafeVMPtr ptrVM(this);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        // no need to release lock, as there are no cross-thread callbacks

        /* get the acpi device interface and check if the button press was handled. */
        PPDMIBASE pBase;
        int vrc = ptrVM.vtable()->pfnPDMR3QueryDeviceLun(ptrVM.rawUVM(), "acpi", 0, 0, &pBase);
        if (RT_SUCCESS(vrc))
        {
            Assert(pBase);
            PPDMIEVENTBUTTONPORT pPort = PDMIBASE_QUERY_INTERFACE(pBase, PDMIEVENTBUTTONPORT);
            if (pPort)
            {
                bool fHandled = false;
                vrc = pPort->pfnQueryPowerButtonHandled(pPort, &fHandled);
                if (RT_SUCCESS(vrc))
                    *aHandled = fHandled;
            }
            else
                vrc = VERR_PDM_MISSING_INTERFACE;
        }

        hrc = RT_SUCCESS(vrc) ? S_OK
            : setErrorBoth(VBOX_E_PDM_ERROR, vrc,
                           tr("Checking if the ACPI Power Button event was handled by the guest OS failed (%Rrc)"), vrc);

    }
    LogFlowThisFunc(("hrc=%Rhrc\n", hrc));
    LogFlowThisFuncLeave();
    return hrc;
}

HRESULT Console::getGuestEnteredACPIMode(BOOL *aEntered)
{
    LogFlowThisFuncEnter();

    *aEntered = FALSE;

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (   mMachineState != MachineState_Running
        && mMachineState != MachineState_Teleporting
        && mMachineState != MachineState_LiveSnapshotting
       )
        return setError(VBOX_E_INVALID_VM_STATE,
                        tr("Invalid machine state %s when checking if the guest entered the ACPI mode"),
                        Global::stringifyMachineState(mMachineState));

    /* get the VM handle. */
    SafeVMPtr ptrVM(this);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        // no need to release lock, as there are no cross-thread callbacks

        /* get the acpi device interface and query the information. */
        PPDMIBASE pBase;
        int vrc = ptrVM.vtable()->pfnPDMR3QueryDeviceLun(ptrVM.rawUVM(), "acpi", 0, 0, &pBase);
        if (RT_SUCCESS(vrc))
        {
            Assert(pBase);
            PPDMIACPIPORT pPort = PDMIBASE_QUERY_INTERFACE(pBase, PDMIACPIPORT);
            if (pPort)
            {
                bool fEntered = false;
                vrc = pPort->pfnGetGuestEnteredACPIMode(pPort, &fEntered);
                if (RT_SUCCESS(vrc))
                    *aEntered = fEntered;
            }
            else
                vrc = VERR_PDM_MISSING_INTERFACE;
        }

        if (vrc == VERR_PDM_DEVICE_INSTANCE_NOT_FOUND)
        {
            /* Might be an ARM VM. */
            /** @todo r=aeichner Think about a prettier way to do this without relying on hardocded device/driver names,
             *                   and this shouldn't be here as it is not about ACPI but needs a dedicated interface. */
            vrc = ptrVM.vtable()->pfnPDMR3QueryDriverOnLun(ptrVM.rawUVM(), "arm-pl061-gpio", 0, 0, "GpioButton", &pBase);
            if (RT_SUCCESS(vrc))
            {
                Assert(pBase);
                PPDMIEVENTBUTTONPORT pPort = PDMIBASE_QUERY_INTERFACE(pBase, PDMIEVENTBUTTONPORT);
                if (pPort)
                {
                    bool fEntered = false;
                    vrc = pPort->pfnQueryGuestCanHandleButtonEvents(pPort, &fEntered);
                    if (RT_SUCCESS(vrc))
                        *aEntered = fEntered;
                }
                else
                    vrc = VERR_PDM_MISSING_INTERFACE;
            }
        }
    }

    LogFlowThisFuncLeave();
    return hrc;
}

HRESULT Console::sleepButton()
{
    LogFlowThisFuncEnter();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (   mMachineState != MachineState_Running
        && mMachineState != MachineState_Teleporting
        && mMachineState != MachineState_LiveSnapshotting)
        return i_setInvalidMachineStateError();

    /* get the VM handle. */
    SafeVMPtr ptrVM(this);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        // no need to release lock, as there are no cross-thread callbacks

        /* get the acpi device interface and press the sleep button. */
        PPDMIBASE pBase = NULL;
        int vrc = ptrVM.vtable()->pfnPDMR3QueryDeviceLun(ptrVM.rawUVM(), "acpi", 0, 0, &pBase);
        /** @todo r=aeichner Think about a prettier way to do this without relying on hardocded device/driver names. */
        if (vrc == VERR_PDM_DEVICE_INSTANCE_NOT_FOUND) /* Try GPIO device for ARM VMs */
            vrc = ptrVM.vtable()->pfnPDMR3QueryDriverOnLun(ptrVM.rawUVM(), "arm-pl061-gpio", 0, 0, "GpioButton", &pBase);
        if (RT_SUCCESS(vrc))
        {
            Assert(pBase);
            PPDMIEVENTBUTTONPORT pPort = PDMIBASE_QUERY_INTERFACE(pBase, PDMIEVENTBUTTONPORT);
            if (pPort)
                vrc = pPort->pfnSleepButtonPress(pPort);
            else
                vrc = VERR_PDM_MISSING_INTERFACE;
        }

        hrc = RT_SUCCESS(vrc) ? S_OK : setErrorBoth(VBOX_E_PDM_ERROR, vrc, tr("Sending sleep button event failed (%Rrc)"), vrc);
    }

    LogFlowThisFunc(("hrc=%Rhrc\n", hrc));
    LogFlowThisFuncLeave();
    return hrc;
}

/**
 * Refreshes the maLedTypes and muLedTypeGen members.
 */
HRESULT Console::i_refreshLedTypeArrays(AutoReadLock *pReadLock)
{
    pReadLock->release();
    AutoWriteLock alock(mLedLock COMMA_LOCKVAL_SRC_POS);

    /*
     * Check that the refresh was already done by someone else while we
     * acquired the write lock.
     */
    if (muLedTypeGen != muLedGen)
    {
        /*
         * Reset the data.
         */
        for (size_t idxType = 0; idxType < RT_ELEMENTS(maLedTypes); idxType++)
            maLedTypes[idxType].cLeds = 0;

        /*
         * Rebuild the data.
         */
        for (uint32_t idxSet = 0; idxSet < mcLedSets; idxSet++)
        {
            PLEDSET const            pLS        = &maLedSets[idxSet];
            uint32_t const           cLeds      = pLS->cLeds;
            PPDMLED volatile * const papSrcLeds = pLS->papLeds;
            DeviceType_T * const     paSubTypes = pLS->paSubTypes;
            for (uint32_t idxLed = 0; idxLed < cLeds; idxLed++)
            {
                /** @todo If we make Console::i_drvStatus_UnitChanged() modify the generation
                 * too, we could skip NULL entries here and make it a bit more compact.
                 * OTOH, most unused LED entires have a paSubTypes of DeviceType_Null. */
                DeviceType_T enmType = paSubTypes ? paSubTypes[idxLed] : (DeviceType_T)(ASMBitFirstSetU32(pLS->fTypes) - 1);
                if (enmType > DeviceType_Null && enmType < DeviceType_End)
                {
                    uint32_t const idxLedType = maLedTypes[enmType].cLeds;
                    if (idxLedType >= maLedTypes[enmType].cAllocated)
                    {
                        void *pvNew = RTMemRealloc(maLedTypes[enmType].pappLeds,
                                                   sizeof(maLedTypes[0].pappLeds[0]) * (idxLedType + 16));
                        if (!pvNew)
                            return E_OUTOFMEMORY;
                        maLedTypes[enmType].pappLeds   = (PPDMLED volatile  **)pvNew;
                        maLedTypes[enmType].cAllocated = idxLedType + 16;
                    }
                    maLedTypes[enmType].pappLeds[idxLedType] = &papSrcLeds[idxLed];
                    maLedTypes[enmType].cLeds                = idxLedType + 1;
                }
            }
        }
        muLedTypeGen = muLedGen;
    }

    /*
     * We have to release the write lock before re-acquiring the read-lock.
     *
     * This means there is a theoretical race here, however we ASSUME that
     * LED sets are never removed and therefore we will be just fine
     * accessing slightly dated per-type data.
     */
    alock.release();
    pReadLock->acquire();
    return S_OK;
}

/** read the value of a LED. */
DECLINLINE(uint32_t) readAndClearLed(PPDMLED pLed)
{
    if (!pLed)
        return 0;
    uint32_t u32 = pLed->Actual.u32 | pLed->Asserted.u32;
    pLed->Asserted.u32 = 0;
    return u32;
}

HRESULT Console::getDeviceActivity(const std::vector<DeviceType_T> &aType, std::vector<DeviceActivity_T> &aActivity)
{
    /*
     * Make a roadmap of which DeviceType_T LED types are wanted.
     *
     * Note! This approach means we'll return the same values in aActivity for
     *       duplicate aType entries.
     */
    uint32_t fRequestedTypes = 0;
    AssertCompile(DeviceType_End <= 32);

    for (size_t iType = 0; iType < aType.size(); ++iType)
    {
        DeviceType_T const enmType = aType[iType];
        AssertCompile((unsigned)DeviceType_Null == 0 /* first */);
        AssertReturn(enmType > DeviceType_Null && enmType < DeviceType_End,
                     setError(E_INVALIDARG, tr("Invalid DeviceType for getDeviceActivity in entry #%u: %d"), iType, enmType));
        fRequestedTypes |= RT_BIT_32((unsigned)enmType);
    }

    /*
     * Resize the result vector before making changes (may throw, paranoia).
     */
    aActivity.resize(aType.size());

    /*
     * Accumulate the per-type data for all the requested types.
     * We will lazily refresh the per-type data collection here when needed.
     */
    PDMLEDCORE aLEDs[DeviceType_End] = { {0} };
    Assert(aLEDs[1].u32 == 0 && aLEDs[DeviceType_End / 2].u32 == 0 && aLEDs[DeviceType_End - 1].u32 == 0); /* paranoia */
    {
        AutoReadLock alock(mLedLock COMMA_LOCKVAL_SRC_POS);
        if (RT_LIKELY(muLedGen == muLedTypeGen))
        { /* likely */ }
        else
        {
            HRESULT hrc = i_refreshLedTypeArrays(&alock);
            if (FAILED(hrc))
                return hrc;
        }

        AssertCompile((unsigned)DeviceType_Null == 0 /* we skip this one */);
        for (uint32_t idxType = 1; idxType < RT_ELEMENTS(maLedTypes); idxType++)
            if (fRequestedTypes & RT_BIT_32(idxType))
            {
                uint32_t const            cLeds       = maLedTypes[idxType].cLeds;
                PPDMLED volatile ** const pappSrcLeds = maLedTypes[idxType].pappLeds;
                for (size_t iLed = 0; iLed < cLeds; iLed++)
                    aLEDs[idxType].u32 |= readAndClearLed(*pappSrcLeds[iLed]);
            }
    }

    /*
     * Compose the result vector:
     */
    for (size_t iType = 0; iType < aActivity.size(); ++iType)
    {
        switch (aLEDs[aType[iType]].u32 & (PDMLED_READING | PDMLED_WRITING))
        {
            case 0:
                aActivity[iType] = DeviceActivity_Idle;
                break;
            case PDMLED_READING:
                aActivity[iType] = DeviceActivity_Reading;
                break;
            case PDMLED_WRITING:
            case PDMLED_READING | PDMLED_WRITING:
                aActivity[iType] = DeviceActivity_Writing;
                break;
        }
    }

    return S_OK;
}

HRESULT Console::attachUSBDevice(const com::Guid &aId, const com::Utf8Str &aCaptureFilename)
{
#ifdef VBOX_WITH_USB
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (   mMachineState != MachineState_Running
        && mMachineState != MachineState_Paused)
        return setError(VBOX_E_INVALID_VM_STATE,
                        tr("Cannot attach a USB device to the machine which is not running or paused (machine state: %s)"),
                        Global::stringifyMachineState(mMachineState));

    /* Get the VM handle. */
    SafeVMPtr ptrVM(this);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        /* Don't proceed unless we have a USB controller. */
        if (mfVMHasUsbController)
        {
            /* release the lock because the USB Proxy service may call us back
             * (via onUSBDeviceAttach()) */
            alock.release();

            /* Request the device capture */
            hrc = mControl->CaptureUSBDevice(Bstr(aId.toString()).raw(), Bstr(aCaptureFilename).raw());
        }
        else
            hrc = setError(VBOX_E_PDM_ERROR, tr("The virtual machine does not have a USB controller"));
    }
    return hrc;

#else   /* !VBOX_WITH_USB */
    RT_NOREF(aId, aCaptureFilename);
    return setError(VBOX_E_PDM_ERROR, tr("The virtual machine does not have a USB controller"));
#endif  /* !VBOX_WITH_USB */
}

HRESULT Console::detachUSBDevice(const com::Guid &aId, ComPtr<IUSBDevice> &aDevice)
{
    RT_NOREF(aDevice);
#ifdef VBOX_WITH_USB
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Find it. */
    for (USBDeviceList::iterator it = mUSBDevices.begin(); it != mUSBDevices.end(); ++it)
        if ((*it)->i_id() == aId)
        {
            /* Found it! */
            ComObjPtr<OUSBDevice> pUSBDevice(*it);

            /* Remove the device from the collection, it is re-added below for failures */
            mUSBDevices.erase(it);

            /*
             * Inform the USB device and USB proxy about what's cooking.
             */
            alock.release();
            HRESULT hrc = mControl->DetachUSBDevice(Bstr(aId.toString()).raw(), false /* aDone */);
            if (SUCCEEDED(hrc))
            {
                /* Request the PDM to detach the USB device. */
                hrc = i_detachUSBDevice(pUSBDevice);
                if (SUCCEEDED(hrc))
                {
                    //return the detached USB device
                    pUSBDevice.queryInterfaceTo(aDevice.asOutParam());
                    /* Request the device release. Even if it fails, the device will
                     * remain as held by proxy, which is OK for us (the VM process). */
                    return mControl->DetachUSBDevice(Bstr(aId.toString()).raw(), true /* aDone */);
                }
            }

            /* Re-add the device to the collection */
            alock.acquire();
            mUSBDevices.push_back(pUSBDevice);
            return hrc;
        }

    return setError(E_INVALIDARG, tr("USB device with UUID {%RTuuid} is not attached to this machine"), aId.raw());

#else   /* !VBOX_WITH_USB */
    RT_NOREF(aId, aDevice);
    return setError(VBOX_E_PDM_ERROR, tr("The virtual machine does not have a USB controller"));
#endif  /* !VBOX_WITH_USB */
}


HRESULT Console::findUSBDeviceByAddress(const com::Utf8Str &aName, ComPtr<IUSBDevice> &aDevice)
{
#ifdef VBOX_WITH_USB
    aDevice = NULL;

    SafeIfaceArray<IUSBDevice> devsvec;
    HRESULT hrc = COMGETTER(USBDevices)(ComSafeArrayAsOutParam(devsvec));
    if (FAILED(hrc))
        return hrc;

    for (size_t i = 0; i < devsvec.size(); ++i)
    {
        Bstr bstrAddress;
        hrc = devsvec[i]->COMGETTER(Address)(bstrAddress.asOutParam());
        if (FAILED(hrc))
            return hrc;
        if (bstrAddress == aName)
        {
            ComObjPtr<OUSBDevice> pUSBDevice;
            pUSBDevice.createObject();
            pUSBDevice->init(devsvec[i]);
            return pUSBDevice.queryInterfaceTo(aDevice.asOutParam());
        }
    }

    return setErrorNoLog(VBOX_E_OBJECT_NOT_FOUND, tr("Could not find a USB device with address '%s'"), aName.c_str());

#else   /* !VBOX_WITH_USB */
    RT_NOREF(aName, aDevice);
    return E_NOTIMPL;
#endif  /* !VBOX_WITH_USB */
}

HRESULT Console::findUSBDeviceById(const com::Guid &aId, ComPtr<IUSBDevice> &aDevice)
{
#ifdef VBOX_WITH_USB
    aDevice = NULL;

    SafeIfaceArray<IUSBDevice> devsvec;
    HRESULT hrc = COMGETTER(USBDevices)(ComSafeArrayAsOutParam(devsvec));
    if (FAILED(hrc))
        return hrc;

    Utf8Str const strId = aId.toString();
    for (size_t i = 0; i < devsvec.size(); ++i)
    {
        Bstr id;
        hrc = devsvec[i]->COMGETTER(Id)(id.asOutParam());
        if (FAILED(hrc))
            return hrc;
        if (id == strId)
        {
            ComObjPtr<OUSBDevice> pUSBDevice;
            pUSBDevice.createObject();
            pUSBDevice->init(devsvec[i]);
            ComObjPtr<IUSBDevice> iUSBDevice = static_cast <ComObjPtr<IUSBDevice> > (pUSBDevice);
            return iUSBDevice.queryInterfaceTo(aDevice.asOutParam());
        }
    }

    return setErrorNoLog(VBOX_E_OBJECT_NOT_FOUND, tr("Could not find a USB device with uuid {%RTuuid}"), aId.raw());

#else   /* !VBOX_WITH_USB */
    RT_NOREF(aId, aDevice);
    return E_NOTIMPL;
#endif  /* !VBOX_WITH_USB */
}

HRESULT Console::createSharedFolder(const com::Utf8Str &aName, const com::Utf8Str &aHostPath, BOOL aWritable,
                                    BOOL aAutomount, const com::Utf8Str &aAutoMountPoint)
{
    LogFlowThisFunc(("Entering for '%s' -> '%s'\n", aName.c_str(), aHostPath.c_str()));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /// @todo see @todo in AttachUSBDevice() about the Paused state
    if (mMachineState == MachineState_Saved || mMachineState == MachineState_AbortedSaved)
        return setError(VBOX_E_INVALID_VM_STATE,
                        tr("Cannot create a transient shared folder on a machine in a saved state (machine state: %s)"),
                        Global::stringifyMachineState(mMachineState));
    if (   mMachineState != MachineState_PoweredOff
        && mMachineState != MachineState_Teleported
        && mMachineState != MachineState_Aborted
        && mMachineState != MachineState_Running
        && mMachineState != MachineState_Paused
       )
        return setError(VBOX_E_INVALID_VM_STATE,
                        tr("Cannot create a transient shared folder on the machine while it is changing the state (machine state: %s)"),
                        Global::stringifyMachineState(mMachineState));

    ComObjPtr<ConsoleSharedFolder> pSharedFolder;
    HRESULT hrc = i_findSharedFolder(aName, pSharedFolder, false /* aSetError */);
    if (SUCCEEDED(hrc))
        return setError(VBOX_E_FILE_ERROR, tr("Shared folder named '%s' already exists"), aName.c_str());

    pSharedFolder.createObject();
    hrc = pSharedFolder->init(this,
                              aName,
                              aHostPath,
                              !!aWritable,
                              !!aAutomount,
                              aAutoMountPoint,
                              true /* fFailOnError */);
    if (FAILED(hrc))
        return hrc;

    /* If the VM is online and supports shared folders, share this folder
     * under the specified name. (Ignore any failure to obtain the VM handle.) */
    SafeVMPtrQuiet ptrVM(this);
    if (    ptrVM.isOk()
         && m_pVMMDev
         && m_pVMMDev->isShFlActive()
       )
    {
        /* first, remove the machine or the global folder if there is any */
        SharedFolderDataMap::const_iterator it;
        if (i_findOtherSharedFolder(aName, it))
        {
            hrc = i_removeSharedFolder(aName);
            if (FAILED(hrc))
                return hrc;
        }

        /* second, create the given folder */
        hrc = i_createSharedFolder(aName, SharedFolderData(aHostPath, !!aWritable, !!aAutomount, aAutoMountPoint));
        if (FAILED(hrc))
            return hrc;
    }

    m_mapSharedFolders.insert(std::make_pair(aName, pSharedFolder));

    /* Notify console callbacks after the folder is added to the list. */
    alock.release();
    ::FireSharedFolderChangedEvent(mEventSource, Scope_Session);

    LogFlowThisFunc(("Leaving for '%s' -> '%s'\n", aName.c_str(), aHostPath.c_str()));

    return hrc;
}

HRESULT Console::removeSharedFolder(const com::Utf8Str &aName)
{
    LogFlowThisFunc(("Entering for '%s'\n", aName.c_str()));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /// @todo see @todo in AttachUSBDevice() about the Paused state
    if (mMachineState == MachineState_Saved || mMachineState == MachineState_AbortedSaved)
        return setError(VBOX_E_INVALID_VM_STATE,
                        tr("Cannot remove a transient shared folder from a machine in a saved state (machine state: %s)"),
                        Global::stringifyMachineState(mMachineState));;
    if (   mMachineState != MachineState_PoweredOff
        && mMachineState != MachineState_Teleported
        && mMachineState != MachineState_Aborted
        && mMachineState != MachineState_Running
        && mMachineState != MachineState_Paused
       )
        return setError(VBOX_E_INVALID_VM_STATE,
            tr("Cannot remove a transient shared folder from the machine while it is changing the state (machine state: %s)"),
            Global::stringifyMachineState(mMachineState));

    ComObjPtr<ConsoleSharedFolder> pSharedFolder;
    HRESULT hrc = i_findSharedFolder(aName, pSharedFolder, true /* aSetError */);
    if (FAILED(hrc))
        return hrc;

    /* protect the VM handle (if not NULL) */
    SafeVMPtrQuiet ptrVM(this);
    if (    ptrVM.isOk()
         && m_pVMMDev
         && m_pVMMDev->isShFlActive()
       )
    {
        /* if the VM is online and supports shared folders, UNshare this folder. */

        /* first, remove the given folder */
        hrc = i_removeSharedFolder(aName);
        if (FAILED(hrc))
            return hrc;

        /* first, remove the machine or the global folder if there is any */
        SharedFolderDataMap::const_iterator it;
        if (i_findOtherSharedFolder(aName, it))
        {
            hrc = i_createSharedFolder(aName, it->second);
            /* don't check hrc here because we need to remove the console
             * folder from the collection even on failure */
        }
    }

    m_mapSharedFolders.erase(aName);

    /* Notify console callbacks after the folder is removed from the list. */
    alock.release();
    ::FireSharedFolderChangedEvent(mEventSource, Scope_Session);

    LogFlowThisFunc(("Leaving for '%s'\n", aName.c_str()));

    return hrc;
}

HRESULT Console::addEncryptionPassword(const com::Utf8Str &aId, const com::Utf8Str &aPassword,
                                       BOOL aClearOnSuspend)
{
    if (   aId.isEmpty()
        || aPassword.isEmpty())
        return setError(E_FAIL, tr("The ID and password must be both valid"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;
    size_t cbKey = aPassword.length() + 1; /* Include terminator */
    const uint8_t *pbKey = (const uint8_t *)aPassword.c_str();

    int vrc = m_pKeyStore->addSecretKey(aId, pbKey, cbKey);
    if (   RT_SUCCESS(vrc)
#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
        || vrc == VERR_ALREADY_EXISTS /* Allow setting an existing key for encrypted VMs. */
#endif
        )
    {
        unsigned cDisksConfigured = 0;

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
        if (mptrNvramStore.isNotNull())
            mptrNvramStore->i_addPassword(aId, aPassword);

        SecretKey *pKey = NULL;
        vrc = m_pKeyStore->retainSecretKey(aId, &pKey);
        AssertRCReturn(vrc, E_FAIL);
        pKey->setRemoveOnSuspend(!!aClearOnSuspend);
        pKey->release();
#endif

        hrc = i_configureEncryptionForDisk(aId, &cDisksConfigured);
        if (SUCCEEDED(hrc))
        {
#ifndef VBOX_WITH_FULL_VM_ENCRYPTION
            SecretKey *pKey = NULL;
#endif
            vrc = m_pKeyStore->retainSecretKey(aId, &pKey);
            AssertRCReturn(vrc, E_FAIL);

            pKey->setUsers(cDisksConfigured);
#ifndef VBOX_WITH_FULL_VM_ENCRYPTION
            pKey->setRemoveOnSuspend(!!aClearOnSuspend);
            m_pKeyStore->releaseSecretKey(aId);
#endif
            m_cDisksPwProvided += cDisksConfigured;

            if (   m_cDisksPwProvided == m_cDisksEncrypted
                && mMachineState == MachineState_Paused)
            {
                /* get the VM handle. */
                SafeVMPtr ptrVM(this);
                if (!ptrVM.isOk())
                    return ptrVM.hrc();

                alock.release();
                vrc = ptrVM.vtable()->pfnVMR3Resume(ptrVM.rawUVM(), VMRESUMEREASON_RECONFIG);

                hrc = RT_SUCCESS(vrc) ? S_OK
                    : setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("Could not resume the machine execution (%Rrc)"), vrc);
            }
        }
    }
#ifndef VBOX_WITH_FULL_VM_ENCRYPTION
    else if (vrc == VERR_ALREADY_EXISTS)
        hrc = setErrorBoth(VBOX_E_OBJECT_IN_USE, vrc, tr("A password with the given ID already exists"));
#endif
    else if (vrc == VERR_NO_MEMORY)
        hrc = setErrorBoth(E_FAIL, vrc, tr("Failed to allocate enough secure memory for the key"));
    else
        hrc = setErrorBoth(E_FAIL, vrc, tr("Unknown error happened while adding a password (%Rrc)"), vrc);

    return hrc;
}

HRESULT Console::addEncryptionPasswords(const std::vector<com::Utf8Str> &aIds, const std::vector<com::Utf8Str> &aPasswords,
                                        BOOL aClearOnSuspend)
{
    HRESULT hrc = S_OK;

    if (   aIds.empty()
        || aPasswords.empty())
        return setError(E_FAIL, tr("IDs and passwords must not be empty"));

    if (aIds.size() != aPasswords.size())
        return setError(E_FAIL, tr("The number of entries in the id and password arguments must match"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

#ifndef VBOX_WITH_FULL_VM_ENCRYPTION
    /* Check that the IDs do not exist already before changing anything. */
    for (unsigned i = 0; i < aIds.size(); i++)
    {
        SecretKey *pKey = NULL;
        int vrc = m_pKeyStore->retainSecretKey(aIds[i], &pKey);
        if (vrc != VERR_NOT_FOUND)
        {
            AssertPtr(pKey);
            if (pKey)
                pKey->release();
            return setError(VBOX_E_OBJECT_IN_USE, tr("A password with the given ID already exists"));
        }
    }
#else
    /*
     * Passwords for the same ID can be added in different ways because
     * of encrypted VMs now. Just add them instead of generating an error.
     */
    /** @todo Check that passwords with the same ID match. */
#endif

    for (unsigned i = 0; i < aIds.size(); i++)
    {
        hrc = addEncryptionPassword(aIds[i], aPasswords[i], aClearOnSuspend);
        if (FAILED(hrc))
        {
            /*
             * Try to remove already successfully added passwords from the map to not
             * change the state of the Console object.
             */
            ErrorInfoKeeper eik; /* Keep current error info or it gets deestroyed in the IPC methods below. */
            for (unsigned ii = 0; ii < i; ii++)
            {
                i_clearDiskEncryptionKeysOnAllAttachmentsWithKeyId(aIds[ii]);
                removeEncryptionPassword(aIds[ii]);
            }

            break;
        }
    }

    return hrc;
}

HRESULT Console::removeEncryptionPassword(const com::Utf8Str &aId)
{
    if (aId.isEmpty())
        return setError(E_FAIL, tr("The ID must be valid"));

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    SecretKey *pKey = NULL;
    int vrc = m_pKeyStore->retainSecretKey(aId, &pKey);
    if (RT_SUCCESS(vrc))
    {
        m_cDisksPwProvided -= pKey->getUsers();
        m_pKeyStore->releaseSecretKey(aId);
        vrc = m_pKeyStore->deleteSecretKey(aId);
        AssertRCReturn(vrc, E_FAIL);

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
        if (mptrNvramStore.isNotNull())
            mptrNvramStore->i_removePassword(aId);
#endif
    }
    else if (vrc == VERR_NOT_FOUND)
        return setErrorBoth(VBOX_E_OBJECT_NOT_FOUND, vrc, tr("A password with the ID \"%s\" does not exist"), aId.c_str());
    else
        return setErrorBoth(E_FAIL, vrc, tr("Failed to remove password with ID \"%s\" (%Rrc)"), aId.c_str(), vrc);

    return S_OK;
}

HRESULT Console::clearAllEncryptionPasswords()
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
    if (mptrNvramStore.isNotNull())
        mptrNvramStore->i_removeAllPasswords();
#endif

    int vrc = m_pKeyStore->deleteAllSecretKeys(false /* fSuspend */, false /* fForce */);
    if (vrc == VERR_RESOURCE_IN_USE)
        return setErrorBoth(VBOX_E_OBJECT_IN_USE, vrc, tr("A password is still in use by the VM"));
    else if (RT_FAILURE(vrc))
        return setErrorBoth(E_FAIL, vrc, tr("Deleting all passwords failed (%Rrc)"));

    m_cDisksPwProvided = 0;
    return S_OK;
}

// Non-interface public methods
/////////////////////////////////////////////////////////////////////////////

/*static*/
HRESULT Console::i_setErrorStatic(HRESULT aResultCode, const char *pcsz, ...)
{
    va_list args;
    va_start(args, pcsz);
    HRESULT hrc = setErrorInternalV(aResultCode,
                                    getStaticClassIID(),
                                    getStaticComponentName(),
                                    pcsz, args,
                                    false /* aWarning */,
                                    true /* aLogIt */);
    va_end(args);
    return hrc;
}

/*static*/
HRESULT Console::i_setErrorStaticBoth(HRESULT aResultCode, int vrc, const char *pcsz, ...)
{
    va_list args;
    va_start(args, pcsz);
    HRESULT hrc = setErrorInternalV(aResultCode,
                                    getStaticClassIID(),
                                    getStaticComponentName(),
                                    pcsz, args,
                                    false /* aWarning */,
                                    true /* aLogIt */,
                                    vrc);
    va_end(args);
    return hrc;
}

HRESULT Console::i_setInvalidMachineStateError()
{
    return setError(VBOX_E_INVALID_VM_STATE,
                    tr("Invalid machine state: %s"),
                    Global::stringifyMachineState(mMachineState));
}


/**
 * Converts to PDM device names.
 */
/* static */ const char *Console::i_storageControllerTypeToStr(StorageControllerType_T enmCtrlType)
{
    switch (enmCtrlType)
    {
        case StorageControllerType_LsiLogic:
            return "lsilogicscsi";
        case StorageControllerType_BusLogic:
            return "buslogic";
        case StorageControllerType_LsiLogicSas:
            return "lsilogicsas";
        case StorageControllerType_IntelAhci:
            return "ahci";
        case StorageControllerType_PIIX3:
        case StorageControllerType_PIIX4:
        case StorageControllerType_ICH6:
            return "piix3ide";
        case StorageControllerType_I82078:
            return "i82078";
        case StorageControllerType_USB:
            return "Msd";
        case StorageControllerType_NVMe:
            return "nvme";
        case StorageControllerType_VirtioSCSI:
            return "virtio-scsi";
        default:
            return NULL;
    }
}

HRESULT Console::i_storageBusPortDeviceToLun(StorageBus_T enmBus, LONG port, LONG device, unsigned &uLun)
{
    switch (enmBus)
    {
        case StorageBus_IDE:
        case StorageBus_Floppy:
        {
            AssertMsgReturn(port < 2 && port >= 0, ("%d\n", port), E_INVALIDARG);
            AssertMsgReturn(device < 2 && device >= 0, ("%d\n", device), E_INVALIDARG);
            uLun = 2 * port + device;
            return S_OK;
        }
        case StorageBus_SATA:
        case StorageBus_SCSI:
        case StorageBus_SAS:
        case StorageBus_PCIe:
        case StorageBus_VirtioSCSI:
        {
            uLun = port;
            return S_OK;
        }
        case StorageBus_USB:
        {
             /*
              * It is always the first lun, the port denotes the device instance
              * for the Msd device.
              */
            uLun = 0;
            return S_OK;
        }
        default:
            uLun = 0;
            AssertMsgFailedReturn(("%d\n", enmBus), E_INVALIDARG);
    }
}

// private methods
/////////////////////////////////////////////////////////////////////////////

/**
 * Suspend the VM before we do any medium or network attachment change.
 *
 * @param pUVM              Safe VM handle.
 * @param pVMM              Safe VMM vtable.
 * @param pAlock            The automatic lock instance. This is for when we have
 *                          to leave it in order to avoid deadlocks.
 * @param pfResume          where to store the information if we need to resume
 *                          afterwards.
 */
HRESULT Console::i_suspendBeforeConfigChange(PUVM pUVM, PCVMMR3VTABLE pVMM, AutoWriteLock *pAlock, bool *pfResume)
{
    *pfResume = false;

    VMSTATE enmVMState = pVMM->pfnVMR3GetStateU(pUVM);
    switch (enmVMState)
    {
        case VMSTATE_RUNNING:
        case VMSTATE_RESETTING:
        case VMSTATE_SOFT_RESETTING:
        {
            LogFlowFunc(("Suspending the VM...\n"));
            /* disable the callback to prevent Console-level state change */
            mVMStateChangeCallbackDisabled = true;
            if (pAlock)
                pAlock->release();
            int vrc = pVMM->pfnVMR3Suspend(pUVM, VMSUSPENDREASON_RECONFIG);
            if (pAlock)
                pAlock->acquire();
            mVMStateChangeCallbackDisabled = false;
            if (RT_FAILURE(vrc))
                return setErrorInternalF(VBOX_E_INVALID_VM_STATE,
                                         COM_IIDOF(IConsole),
                                         getStaticComponentName(),
                                         false /*aWarning*/,
                                         true /*aLogIt*/,
                                         vrc,
                                         tr("Could suspend VM for medium change (%Rrc)"), vrc);
            *pfResume = true;
            break;
        }
        case VMSTATE_SUSPENDED:
            break;
        default:
            return setErrorInternalF(VBOX_E_INVALID_VM_STATE,
                                     COM_IIDOF(IConsole),
                                     getStaticComponentName(),
                                     false /*aWarning*/,
                                     true /*aLogIt*/,
                                     0 /* aResultDetail */,
                                     tr("Invalid state '%s' for changing medium"),
                                     pVMM->pfnVMR3GetStateName(enmVMState));
    }

    return S_OK;
}

/**
 * Resume the VM after we did any medium or network attachment change.
 * This is the counterpart to Console::suspendBeforeConfigChange().
 *
 * @param pUVM              Safe VM handle.
 * @param pVMM              Safe VMM vtable.
 */
void Console::i_resumeAfterConfigChange(PUVM pUVM, PCVMMR3VTABLE pVMM)
{
    LogFlowFunc(("Resuming the VM...\n"));

    /* disable the callback to prevent Console-level state change */
    mVMStateChangeCallbackDisabled = true;
    int vrc = pVMM->pfnVMR3Resume(pUVM, VMRESUMEREASON_RECONFIG);
    mVMStateChangeCallbackDisabled = false;
    AssertRC(vrc);
    if (RT_FAILURE(vrc))
    {
        VMSTATE enmVMState = pVMM->pfnVMR3GetStateU(pUVM);
        if (enmVMState == VMSTATE_SUSPENDED)
        {
            /* too bad, we failed. try to sync the console state with the VMM state */
            i_vmstateChangeCallback(pUVM, pVMM, VMSTATE_SUSPENDED, enmVMState, this);
        }
    }
}

/**
 * Process a medium change.
 *
 * @param aMediumAttachment The medium attachment with the new medium state.
 * @param fForce            Force medium chance, if it is locked or not.
 * @param pUVM              Safe VM handle.
 * @param pVMM              Safe VMM vtable.
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_doMediumChange(IMediumAttachment *aMediumAttachment, bool fForce, PUVM pUVM, PCVMMR3VTABLE pVMM)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /* We will need to release the write lock before calling EMT */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    const char *pszDevice = NULL;

    SafeIfaceArray<IStorageController> ctrls;
    HRESULT hrc = mMachine->COMGETTER(StorageControllers)(ComSafeArrayAsOutParam(ctrls));
    AssertComRC(hrc);

    IMedium *pMedium = NULL;
    hrc = aMediumAttachment->COMGETTER(Medium)(&pMedium);
    AssertComRC(hrc);

    Bstr mediumLocation;
    if (pMedium)
    {
        hrc = pMedium->COMGETTER(Location)(mediumLocation.asOutParam());
        AssertComRC(hrc);
    }

    Bstr attCtrlName;
    hrc = aMediumAttachment->COMGETTER(Controller)(attCtrlName.asOutParam());
    AssertComRC(hrc);
    ComPtr<IStorageController> pStorageController;
    for (size_t i = 0; i < ctrls.size(); ++i)
    {
        Bstr ctrlName;
        hrc = ctrls[i]->COMGETTER(Name)(ctrlName.asOutParam());
        AssertComRC(hrc);
        if (attCtrlName == ctrlName)
        {
            pStorageController = ctrls[i];
            break;
        }
    }
    if (pStorageController.isNull())
        return setError(E_FAIL, tr("Could not find storage controller '%ls'"), attCtrlName.raw());

    StorageControllerType_T enmCtrlType;
    hrc = pStorageController->COMGETTER(ControllerType)(&enmCtrlType);
    AssertComRC(hrc);
    pszDevice = i_storageControllerTypeToStr(enmCtrlType);

    StorageBus_T enmBus;
    hrc = pStorageController->COMGETTER(Bus)(&enmBus);
    AssertComRC(hrc);

    ULONG uInstance;
    hrc = pStorageController->COMGETTER(Instance)(&uInstance);
    AssertComRC(hrc);

    BOOL fUseHostIOCache;
    hrc = pStorageController->COMGETTER(UseHostIOCache)(&fUseHostIOCache);
    AssertComRC(hrc);

    /*
     * Suspend the VM first. The VM must not be running since it might have
     * pending I/O to the drive which is being changed.
     */
    bool fResume = false;
    hrc = i_suspendBeforeConfigChange(pUVM, pVMM, &alock, &fResume);
    if (FAILED(hrc))
        return hrc;

    /*
     * Call worker on EMT #0, that's faster and safer than doing everything
     * using VMR3ReqCall. Note that we separate VMR3ReqCall from VMR3ReqWait
     * here to make requests from under the lock in order to serialize them.
     */
    PVMREQ pReq;
    int vrc = pVMM->pfnVMR3ReqCallU(pUVM, 0, &pReq, 0 /* no wait! */, VMREQFLAGS_VBOX_STATUS, (PFNRT)i_changeRemovableMedium, 9,
                                    this, pUVM, pVMM, pszDevice, uInstance, enmBus, fUseHostIOCache, aMediumAttachment, fForce);

    /* release the lock before waiting for a result (EMT might wait for it, @bugref{7648})! */
    alock.release();

    if (vrc == VERR_TIMEOUT)
        vrc = pVMM->pfnVMR3ReqWait(pReq, RT_INDEFINITE_WAIT);
    AssertRC(vrc);
    if (RT_SUCCESS(vrc))
        vrc = pReq->iStatus;
    pVMM->pfnVMR3ReqFree(pReq);

    if (fResume)
        i_resumeAfterConfigChange(pUVM, pVMM);

    if (RT_SUCCESS(vrc))
    {
        LogFlowThisFunc(("Returns S_OK\n"));
        return S_OK;
    }

    if (pMedium)
        return setErrorBoth(E_FAIL, vrc, tr("Could not mount the media/drive '%ls' (%Rrc)"), mediumLocation.raw(), vrc);
    return setErrorBoth(E_FAIL, vrc, tr("Could not unmount the currently mounted media/drive (%Rrc)"), vrc);
}

/**
 * Performs the medium change in EMT.
 *
 * @returns VBox status code.
 *
 * @param   pThis           Pointer to the Console object.
 * @param   pUVM            The VM handle.
 * @param   pVMM            The VMM vtable.
 * @param   pcszDevice      The PDM device name.
 * @param   uInstance       The PDM device instance.
 * @param   enmBus          The storage bus type of the controller.
 * @param   fUseHostIOCache Whether to use the host I/O cache (disable async I/O).
 * @param   aMediumAtt      The medium attachment.
 * @param   fForce          Force unmounting.
 *
 * @thread  EMT
 * @note The VM must not be running since it might have pending I/O to the drive which is being changed.
 */
DECLCALLBACK(int) Console::i_changeRemovableMedium(Console *pThis,
                                                   PUVM pUVM,
                                                   PCVMMR3VTABLE pVMM,
                                                   const char *pcszDevice,
                                                   unsigned uInstance,
                                                   StorageBus_T enmBus,
                                                   bool fUseHostIOCache,
                                                   IMediumAttachment *aMediumAtt,
                                                   bool fForce)
{
    LogFlowFunc(("pThis=%p uInstance=%u pszDevice=%p:{%s} enmBus=%u, aMediumAtt=%p, fForce=%d\n",
                 pThis, uInstance, pcszDevice, pcszDevice, enmBus, aMediumAtt, fForce));

    AssertReturn(pThis, VERR_INVALID_PARAMETER);

    AutoCaller autoCaller(pThis);
    AssertComRCReturn(autoCaller.hrc(), VERR_ACCESS_DENIED);

    /*
     * Check the VM for correct state.
     */
    VMSTATE enmVMState = pVMM->pfnVMR3GetStateU(pUVM);
    AssertReturn(enmVMState == VMSTATE_SUSPENDED, VERR_INVALID_STATE);

    int vrc = pThis->i_configMediumAttachment(pcszDevice,
                                              uInstance,
                                              enmBus,
                                              fUseHostIOCache,
                                              false /* fSetupMerge */,
                                              false /* fBuiltinIOCache */,
                                              false /* fInsertDiskIntegrityDrv. */,
                                              0 /* uMergeSource */,
                                              0 /* uMergeTarget */,
                                              aMediumAtt,
                                              pThis->mMachineState,
                                              NULL /* phrc */,
                                              true /* fAttachDetach */,
                                              fForce /* fForceUnmount */,
                                              false  /* fHotplug */,
                                              pUVM,
                                              pVMM,
                                              NULL /* paLedDevType */,
                                              NULL /* ppLunL0 */);
    LogFlowFunc(("Returning %Rrc\n", vrc));
    return vrc;
}


/**
 * Attach a new storage device to the VM.
 *
 * @param aMediumAttachment The medium attachment which is added.
 * @param pUVM              Safe VM handle.
 * @param pVMM              Safe VMM vtable.
 * @param fSilent           Flag whether to notify the guest about the attached device.
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_doStorageDeviceAttach(IMediumAttachment *aMediumAttachment, PUVM pUVM, PCVMMR3VTABLE pVMM, bool fSilent)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /* We will need to release the write lock before calling EMT */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    const char *pszDevice = NULL;

    SafeIfaceArray<IStorageController> ctrls;
    HRESULT hrc = mMachine->COMGETTER(StorageControllers)(ComSafeArrayAsOutParam(ctrls));
    AssertComRC(hrc);

    IMedium *pMedium = NULL;
    hrc = aMediumAttachment->COMGETTER(Medium)(&pMedium);
    AssertComRC(hrc);

    Bstr mediumLocation;
    if (pMedium)
    {
        hrc = pMedium->COMGETTER(Location)(mediumLocation.asOutParam());
        AssertComRC(hrc);
    }

    Bstr attCtrlName;
    hrc = aMediumAttachment->COMGETTER(Controller)(attCtrlName.asOutParam());
    AssertComRC(hrc);
    ComPtr<IStorageController> pStorageController;
    for (size_t i = 0; i < ctrls.size(); ++i)
    {
        Bstr ctrlName;
        hrc = ctrls[i]->COMGETTER(Name)(ctrlName.asOutParam());
        AssertComRC(hrc);
        if (attCtrlName == ctrlName)
        {
            pStorageController = ctrls[i];
            break;
        }
    }
    if (pStorageController.isNull())
        return setError(E_FAIL, tr("Could not find storage controller '%ls'"), attCtrlName.raw());

    StorageControllerType_T enmCtrlType;
    hrc = pStorageController->COMGETTER(ControllerType)(&enmCtrlType);
    AssertComRC(hrc);
    pszDevice = i_storageControllerTypeToStr(enmCtrlType);

    StorageBus_T enmBus;
    hrc = pStorageController->COMGETTER(Bus)(&enmBus);
    AssertComRC(hrc);

    ULONG uInstance;
    hrc = pStorageController->COMGETTER(Instance)(&uInstance);
    AssertComRC(hrc);

    BOOL fUseHostIOCache;
    hrc = pStorageController->COMGETTER(UseHostIOCache)(&fUseHostIOCache);
    AssertComRC(hrc);

    /*
     * Suspend the VM first. The VM must not be running since it might have
     * pending I/O to the drive which is being changed.
     */
    bool fResume = false;
    hrc = i_suspendBeforeConfigChange(pUVM, pVMM, &alock, &fResume);
    if (FAILED(hrc))
        return hrc;

    /*
     * Call worker on EMT #0, that's faster and safer than doing everything
     * using VMR3ReqCall. Note that we separate VMR3ReqCall from VMR3ReqWait
     * here to make requests from under the lock in order to serialize them.
     */
    PVMREQ pReq;
    int vrc = pVMM->pfnVMR3ReqCallU(pUVM, 0, &pReq, 0 /* no wait! */, VMREQFLAGS_VBOX_STATUS, (PFNRT)i_attachStorageDevice, 9,
                                    this, pUVM, pVMM, pszDevice, uInstance, enmBus, fUseHostIOCache, aMediumAttachment, fSilent);

    /* release the lock before waiting for a result (EMT might wait for it, @bugref{7648})! */
    alock.release();

    if (vrc == VERR_TIMEOUT)
        vrc = pVMM->pfnVMR3ReqWait(pReq, RT_INDEFINITE_WAIT);
    AssertRC(vrc);
    if (RT_SUCCESS(vrc))
        vrc = pReq->iStatus;
    pVMM->pfnVMR3ReqFree(pReq);

    if (fResume)
        i_resumeAfterConfigChange(pUVM, pVMM);

    if (RT_SUCCESS(vrc))
    {
        LogFlowThisFunc(("Returns S_OK\n"));
        return S_OK;
    }

    if (!pMedium)
        return setErrorBoth(E_FAIL, vrc, tr("Could not mount the media/drive '%ls' (%Rrc)"), mediumLocation.raw(), vrc);
    return setErrorBoth(E_FAIL, vrc, tr("Could not unmount the currently mounted media/drive (%Rrc)"), vrc);
}


/**
 * Performs the storage attach operation in EMT.
 *
 * @returns VBox status code.
 *
 * @param   pThis           Pointer to the Console object.
 * @param   pUVM            The VM handle.
 * @param   pVMM            The VMM vtable.
 * @param   pcszDevice      The PDM device name.
 * @param   uInstance       The PDM device instance.
 * @param   enmBus          The storage bus type of the controller.
 * @param   fUseHostIOCache Whether to use the host I/O cache (disable async I/O).
 * @param   aMediumAtt      The medium attachment.
 * @param   fSilent         Flag whether to inform the guest about the attached device.
 *
 * @thread  EMT
 * @note The VM must not be running since it might have pending I/O to the drive which is being changed.
 */
DECLCALLBACK(int) Console::i_attachStorageDevice(Console *pThis,
                                                 PUVM pUVM,
                                                 PCVMMR3VTABLE pVMM,
                                                 const char *pcszDevice,
                                                 unsigned uInstance,
                                                 StorageBus_T enmBus,
                                                 bool fUseHostIOCache,
                                                 IMediumAttachment *aMediumAtt,
                                                 bool fSilent)
{
    LogFlowFunc(("pThis=%p uInstance=%u pszDevice=%p:{%s} enmBus=%u, aMediumAtt=%p\n",
                 pThis, uInstance, pcszDevice, pcszDevice, enmBus, aMediumAtt));

    AssertReturn(pThis, VERR_INVALID_PARAMETER);

    AutoCaller autoCaller(pThis);
    AssertComRCReturn(autoCaller.hrc(), VERR_ACCESS_DENIED);

    /*
     * Check the VM for correct state.
     */
    VMSTATE enmVMState = pVMM->pfnVMR3GetStateU(pUVM);
    AssertReturn(enmVMState == VMSTATE_SUSPENDED, VERR_INVALID_STATE);

    int vrc = pThis->i_configMediumAttachment(pcszDevice,
                                              uInstance,
                                              enmBus,
                                              fUseHostIOCache,
                                              false /* fSetupMerge */,
                                              false /* fBuiltinIOCache */,
                                              false /* fInsertDiskIntegrityDrv. */,
                                              0 /* uMergeSource */,
                                              0 /* uMergeTarget */,
                                              aMediumAtt,
                                              pThis->mMachineState,
                                              NULL /* phrc */,
                                              true /* fAttachDetach */,
                                              false /* fForceUnmount */,
                                              !fSilent /* fHotplug */,
                                              pUVM,
                                              pVMM,
                                              NULL /* paLedDevType */,
                                              NULL);
    LogFlowFunc(("Returning %Rrc\n", vrc));
    return vrc;
}

/**
 * Attach a new storage device to the VM.
 *
 * @param aMediumAttachment The medium attachment which is added.
 * @param pUVM              Safe VM handle.
 * @param pVMM              Safe VMM vtable.
 * @param fSilent           Flag whether to notify the guest about the detached device.
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_doStorageDeviceDetach(IMediumAttachment *aMediumAttachment, PUVM pUVM, PCVMMR3VTABLE pVMM, bool fSilent)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /* We will need to release the write lock before calling EMT */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    const char *pszDevice = NULL;

    SafeIfaceArray<IStorageController> ctrls;
    HRESULT hrc = mMachine->COMGETTER(StorageControllers)(ComSafeArrayAsOutParam(ctrls));
    AssertComRC(hrc);

    IMedium *pMedium = NULL;
    hrc = aMediumAttachment->COMGETTER(Medium)(&pMedium);
    AssertComRC(hrc);

    Bstr mediumLocation;
    if (pMedium)
    {
        hrc = pMedium->COMGETTER(Location)(mediumLocation.asOutParam());
        AssertComRC(hrc);
    }

    Bstr attCtrlName;
    hrc = aMediumAttachment->COMGETTER(Controller)(attCtrlName.asOutParam());
    AssertComRC(hrc);
    ComPtr<IStorageController> pStorageController;
    for (size_t i = 0; i < ctrls.size(); ++i)
    {
        Bstr ctrlName;
        hrc = ctrls[i]->COMGETTER(Name)(ctrlName.asOutParam());
        AssertComRC(hrc);
        if (attCtrlName == ctrlName)
        {
            pStorageController = ctrls[i];
            break;
        }
    }
    if (pStorageController.isNull())
        return setError(E_FAIL, tr("Could not find storage controller '%ls'"), attCtrlName.raw());

    StorageControllerType_T enmCtrlType;
    hrc = pStorageController->COMGETTER(ControllerType)(&enmCtrlType);
    AssertComRC(hrc);
    pszDevice = i_storageControllerTypeToStr(enmCtrlType);

    StorageBus_T enmBus = (StorageBus_T)0;
    hrc = pStorageController->COMGETTER(Bus)(&enmBus);
    AssertComRC(hrc);

    ULONG uInstance = 0;
    hrc = pStorageController->COMGETTER(Instance)(&uInstance);
    AssertComRC(hrc);

    /*
     * Suspend the VM first. The VM must not be running since it might have
     * pending I/O to the drive which is being changed.
     */
    bool fResume = false;
    hrc = i_suspendBeforeConfigChange(pUVM, pVMM, &alock, &fResume);
    if (FAILED(hrc))
        return hrc;

    /*
     * Call worker on EMT #0, that's faster and safer than doing everything
     * using VMR3ReqCall. Note that we separate VMR3ReqCall from VMR3ReqWait
     * here to make requests from under the lock in order to serialize them.
     */
    PVMREQ pReq;
    int vrc = pVMM->pfnVMR3ReqCallU(pUVM, 0, &pReq, 0 /* no wait! */, VMREQFLAGS_VBOX_STATUS, (PFNRT)i_detachStorageDevice, 8,
                                    this, pUVM, pVMM, pszDevice, uInstance, enmBus, aMediumAttachment, fSilent);

    /* release the lock before waiting for a result (EMT might wait for it, @bugref{7648})! */
    alock.release();

    if (vrc == VERR_TIMEOUT)
        vrc = pVMM->pfnVMR3ReqWait(pReq, RT_INDEFINITE_WAIT);
    AssertRC(vrc);
    if (RT_SUCCESS(vrc))
        vrc = pReq->iStatus;
    pVMM->pfnVMR3ReqFree(pReq);

    if (fResume)
        i_resumeAfterConfigChange(pUVM, pVMM);

    if (RT_SUCCESS(vrc))
    {
        LogFlowThisFunc(("Returns S_OK\n"));
        return S_OK;
    }

    if (!pMedium)
        return setErrorBoth(E_FAIL, vrc, tr("Could not mount the media/drive '%ls' (%Rrc)"), mediumLocation.raw(), vrc);
    return setErrorBoth(E_FAIL, vrc, tr("Could not unmount the currently mounted media/drive (%Rrc)"), vrc);
}

/**
 * Performs the storage detach operation in EMT.
 *
 * @returns VBox status code.
 *
 * @param   pThis           Pointer to the Console object.
 * @param   pUVM            The VM handle.
 * @param   pVMM            The VMM vtable.
 * @param   pcszDevice      The PDM device name.
 * @param   uInstance       The PDM device instance.
 * @param   enmBus          The storage bus type of the controller.
 * @param   pMediumAtt      Pointer to the medium attachment.
 * @param   fSilent         Flag whether to notify the guest about the detached device.
 *
 * @thread  EMT
 * @note The VM must not be running since it might have pending I/O to the drive which is being changed.
 */
DECLCALLBACK(int) Console::i_detachStorageDevice(Console *pThis,
                                                 PUVM pUVM,
                                                 PCVMMR3VTABLE pVMM,
                                                 const char *pcszDevice,
                                                 unsigned uInstance,
                                                 StorageBus_T enmBus,
                                                 IMediumAttachment *pMediumAtt,
                                                 bool fSilent)
{
    LogRelFlowFunc(("pThis=%p uInstance=%u pszDevice=%p:{%s} enmBus=%u, pMediumAtt=%p\n",
                 pThis, uInstance, pcszDevice, pcszDevice, enmBus, pMediumAtt));

    AssertReturn(pThis, VERR_INVALID_PARAMETER);

    AutoCaller autoCaller(pThis);
    AssertComRCReturn(autoCaller.hrc(), VERR_ACCESS_DENIED);

    /*
     * Check the VM for correct state.
     */
    VMSTATE enmVMState = pVMM->pfnVMR3GetStateU(pUVM);
    AssertReturn(enmVMState == VMSTATE_SUSPENDED, VERR_INVALID_STATE);

    /* Determine the base path for the device instance. */
    PCFGMNODE pCtlInst = pVMM->pfnCFGMR3GetChildF(pVMM->pfnCFGMR3GetRootU(pUVM), "Devices/%s/%u/", pcszDevice, uInstance);
    AssertReturn(pCtlInst || enmBus == StorageBus_USB, VERR_INTERNAL_ERROR);

#define H()         AssertMsgReturn(!FAILED(hrc), ("hrc=%Rhrc\n", hrc), VERR_GENERAL_FAILURE)

    HRESULT hrc;
    int vrc = VINF_SUCCESS;
    LONG lDev;
    hrc = pMediumAtt->COMGETTER(Device)(&lDev);                             H();
    LONG lPort;
    hrc = pMediumAtt->COMGETTER(Port)(&lPort);                              H();
    DeviceType_T lType;
    hrc = pMediumAtt->COMGETTER(Type)(&lType);                              H();
    unsigned uLUN;
    hrc = Console::i_storageBusPortDeviceToLun(enmBus, lPort, lDev, uLUN);  H();

#undef H

    PCFGMNODE pLunL0 = NULL;
    if (enmBus != StorageBus_USB)
    {
        /* First check if the LUN really exists. */
        pLunL0 = pVMM->pfnCFGMR3GetChildF(pCtlInst, "LUN#%u", uLUN);
        if (pLunL0)
        {
            uint32_t fFlags = 0;
            if (fSilent)
                fFlags |= PDM_TACH_FLAGS_NOT_HOT_PLUG;

            vrc = pVMM->pfnPDMR3DeviceDetach(pUVM, pcszDevice, uInstance, uLUN, fFlags);
            if (vrc == VERR_PDM_NO_DRIVER_ATTACHED_TO_LUN)
                vrc = VINF_SUCCESS;
            AssertLogRelRCReturn(vrc, vrc);
            pVMM->pfnCFGMR3RemoveNode(pLunL0);

            Utf8StrFmt devicePath("%s/%u/LUN#%u", pcszDevice, uInstance, uLUN);
            pThis->mapMediumAttachments.erase(devicePath);
        }
        else
            AssertLogRelFailedReturn(VERR_INTERNAL_ERROR);

        pVMM->pfnCFGMR3Dump(pCtlInst);
    }
#ifdef VBOX_WITH_USB
    else
    {
        /* Find the correct USB device in the list. */
        USBStorageDeviceList::iterator it;
        for (it = pThis->mUSBStorageDevices.begin(); it != pThis->mUSBStorageDevices.end(); ++it)
            if (it->iPort == lPort)
                break;
        AssertLogRelReturn(it != pThis->mUSBStorageDevices.end(), VERR_INTERNAL_ERROR);

        vrc = pVMM->pfnPDMR3UsbDetachDevice(pUVM, &it->mUuid);
        AssertLogRelRCReturn(vrc, vrc);
        pThis->mUSBStorageDevices.erase(it);
    }
#endif

    LogFlowFunc(("Returning VINF_SUCCESS\n"));
    return VINF_SUCCESS;
}

/**
 * Called by IInternalSessionControl::OnNetworkAdapterChange().
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_onNetworkAdapterChange(INetworkAdapter *aNetworkAdapter, BOOL changeAdapter)
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    /* don't trigger network changes if the VM isn't running */
    SafeVMPtrQuiet ptrVM(this);
    if (ptrVM.isOk())
    {
        /* Get the properties we need from the adapter */
        BOOL fCableConnected, fTraceEnabled;
        hrc = aNetworkAdapter->COMGETTER(CableConnected)(&fCableConnected);
        AssertComRC(hrc);
        if (SUCCEEDED(hrc))
        {
            hrc = aNetworkAdapter->COMGETTER(TraceEnabled)(&fTraceEnabled);
            AssertComRC(hrc);
            if (SUCCEEDED(hrc))
            {
                ULONG ulInstance;
                hrc = aNetworkAdapter->COMGETTER(Slot)(&ulInstance);
                AssertComRC(hrc);
                if (SUCCEEDED(hrc))
                {
                    /*
                     * Find the adapter instance, get the config interface and update
                     * the link state.
                     */
                    NetworkAdapterType_T adapterType;
                    hrc = aNetworkAdapter->COMGETTER(AdapterType)(&adapterType);
                    AssertComRC(hrc);
                    const char *pszAdapterName = networkAdapterTypeToName(adapterType);

                    // prevent cross-thread deadlocks, don't need the lock any more
                    alock.release();

                    PPDMIBASE pBase = NULL;
                    int vrc = ptrVM.vtable()->pfnPDMR3QueryDeviceLun(ptrVM.rawUVM(), pszAdapterName, ulInstance, 0, &pBase);
                    if (RT_SUCCESS(vrc))
                    {
                        Assert(pBase);
                        PPDMINETWORKCONFIG pINetCfg;
                        pINetCfg = PDMIBASE_QUERY_INTERFACE(pBase, PDMINETWORKCONFIG);
                        if (pINetCfg)
                        {
                            Log(("Console::onNetworkAdapterChange: setting link state to %d\n",
                                  fCableConnected));
                            vrc = pINetCfg->pfnSetLinkState(pINetCfg,
                                                            fCableConnected ? PDMNETWORKLINKSTATE_UP
                                                                            : PDMNETWORKLINKSTATE_DOWN);
                            ComAssertRC(vrc);
                        }
                        if (RT_SUCCESS(vrc) && changeAdapter)
                        {
                            VMSTATE enmVMState = mpVMM->pfnVMR3GetStateU(ptrVM.rawUVM());
                            if (    enmVMState == VMSTATE_RUNNING    /** @todo LiveMigration: Forbid or deal
                                                                         correctly with the _LS variants */
                                ||  enmVMState == VMSTATE_SUSPENDED)
                            {
                                if (fTraceEnabled && fCableConnected && pINetCfg)
                                {
                                    vrc = pINetCfg->pfnSetLinkState(pINetCfg, PDMNETWORKLINKSTATE_DOWN);
                                    ComAssertRC(vrc);
                                }

                                hrc = i_doNetworkAdapterChange(ptrVM.rawUVM(), ptrVM.vtable(), pszAdapterName,
                                                               ulInstance, 0, aNetworkAdapter);

                                if (fTraceEnabled && fCableConnected && pINetCfg)
                                {
                                    vrc = pINetCfg->pfnSetLinkState(pINetCfg, PDMNETWORKLINKSTATE_UP);
                                    ComAssertRC(vrc);
                                }
                            }
                        }
                    }
                    else if (vrc == VERR_PDM_DEVICE_INSTANCE_NOT_FOUND)
                        return setErrorBoth(E_FAIL, vrc, tr("The network adapter #%u is not enabled"), ulInstance);
                    else
                        ComAssertRC(vrc);

                    if (RT_FAILURE(vrc))
                        hrc = E_FAIL;

                    alock.acquire();
                }
            }
        }
        ptrVM.release();
    }

    // definitely don't need the lock any more
    alock.release();

    /* notify console callbacks on success */
    if (SUCCEEDED(hrc))
        ::FireNetworkAdapterChangedEvent(mEventSource, aNetworkAdapter);

    LogFlowThisFunc(("Leaving hrc=%#x\n", hrc));
    return hrc;
}

/**
 * Called by IInternalSessionControl::OnNATEngineChange().
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_onNATRedirectRuleChanged(ULONG ulInstance, BOOL aNatRuleRemove, NATProtocol_T aProto, IN_BSTR aHostIP,
                                            LONG aHostPort, IN_BSTR aGuestIP, LONG aGuestPort)
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    /* don't trigger NAT engine changes if the VM isn't running */
    SafeVMPtrQuiet ptrVM(this);
    if (ptrVM.isOk())
    {
        do
        {
            ComPtr<INetworkAdapter> pNetworkAdapter;
            hrc = i_machine()->GetNetworkAdapter(ulInstance, pNetworkAdapter.asOutParam());
            if (   FAILED(hrc)
                || pNetworkAdapter.isNull())
                break;

            /*
             * Find the adapter instance, get the config interface and update
             * the link state.
             */
            NetworkAdapterType_T adapterType;
            hrc = pNetworkAdapter->COMGETTER(AdapterType)(&adapterType);
            if (FAILED(hrc))
            {
                AssertComRC(hrc);
                hrc = E_FAIL;
                break;
            }

            const char *pszAdapterName = networkAdapterTypeToName(adapterType);
            PPDMIBASE pBase;
            int vrc = ptrVM.vtable()->pfnPDMR3QueryLun(ptrVM.rawUVM(), pszAdapterName, ulInstance, 0, &pBase);
            if (RT_FAILURE(vrc))
            {
                /* This may happen if the NAT network adapter is currently not attached.
                 * This is a valid condition. */
                if (vrc == VERR_PDM_NO_DRIVER_ATTACHED_TO_LUN)
                    break;
                ComAssertRC(vrc);
                hrc = E_FAIL;
                break;
            }

            NetworkAttachmentType_T attachmentType;
            hrc = pNetworkAdapter->COMGETTER(AttachmentType)(&attachmentType);
            if (   FAILED(hrc)
                || attachmentType != NetworkAttachmentType_NAT)
            {
                hrc = E_FAIL;
                break;
            }

            /* look down for PDMINETWORKNATCONFIG interface */
            PPDMINETWORKNATCONFIG pNetNatCfg = NULL;
            while (pBase)
            {
                pNetNatCfg = (PPDMINETWORKNATCONFIG)pBase->pfnQueryInterface(pBase, PDMINETWORKNATCONFIG_IID);
                if (pNetNatCfg)
                    break;
                /** @todo r=bird: This stinks! */
                PPDMDRVINS pDrvIns = PDMIBASE_2_PDMDRV(pBase);
                pBase = pDrvIns->pDownBase;
            }
            if (!pNetNatCfg)
                break;

            bool fUdp = aProto == NATProtocol_UDP;
            vrc = pNetNatCfg->pfnRedirectRuleCommand(pNetNatCfg, !!aNatRuleRemove, fUdp,
                                                     Utf8Str(aHostIP).c_str(), (uint16_t)aHostPort, Utf8Str(aGuestIP).c_str(),
                                                     (uint16_t)aGuestPort);
            if (RT_FAILURE(vrc))
                hrc = E_FAIL;
        } while (0); /* break loop */
        ptrVM.release();
    }

    LogFlowThisFunc(("Leaving hrc=%#x\n", hrc));
    return hrc;
}


/** Helper that converts a BSTR safe array into a C-string array. */
static char **bstrSafeArrayToC(SafeArray<BSTR> const &a_rStrings, size_t *pcEntries) RT_NOEXCEPT
{
    if (pcEntries)
        *pcEntries = 0;

    /*
     * The array is NULL terminated.
     */
    const size_t cStrings = a_rStrings.size();
    char **papszRet = (char **)RTMemAllocZ((cStrings + 1) * sizeof(papszRet[0]));
    AssertReturn(papszRet, NULL);

    /*
     * The individual strings.
     */
    for (size_t i = 0; i < cStrings; i++)
    {
        int vrc = RTUtf16ToUtf8Ex((PCRTUTF16)a_rStrings[i], RTSTR_MAX, &papszRet[i], 0, NULL);
        AssertRC(vrc);
        if (RT_FAILURE(vrc))
        {
            while (i-- > 0)
            {
                RTStrFree(papszRet[i]);
                papszRet[i] = NULL;
            }
            return NULL;
        }

    }
        if (pcEntries)
            *pcEntries = cStrings;
    return papszRet;
}


/**
 * IHostNameResolutionConfigurationChangeEvent
 *
 * Currently this event doesn't carry actual resolver configuration,
 * so we have to go back to VBoxSVC and ask...  This is not ideal.
 */
HRESULT Console::i_onNATDnsChanged()
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /* We wrap PDMINETWORKNATDNSCONFIG to simplify freeing memory allocations
       in it (exceptions, AssertReturn, regular returns). */
    struct DnsConfigCleanupWrapper
    {
        PDMINETWORKNATDNSCONFIG Core;

        DnsConfigCleanupWrapper()
        {
            Core.szDomainName[0]    = '\0';
            Core.cNameServers       = 0;
            Core.papszNameServers   = NULL;
            Core.cSearchDomains     = 0;
            Core.papszSearchDomains = NULL;
        }

        void freeStrArray(char **papsz)
        {
            if (papsz)
            {
                for (size_t i = 0; papsz[i] != NULL; i++)
                    RTStrFree(papsz[i]);
                RTMemFree(papsz);
            }
        }

        ~DnsConfigCleanupWrapper()
        {
            freeStrArray((char **)Core.papszNameServers);
            Core.papszNameServers   = NULL;
            freeStrArray((char **)Core.papszSearchDomains);
            Core.papszSearchDomains = NULL;
        }
    } DnsConfig;


    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS); /** @todo r=bird: Why a write lock? */

    ComPtr<IVirtualBox> ptrVirtualBox;
    HRESULT hrc = mMachine->COMGETTER(Parent)(ptrVirtualBox.asOutParam());
    if (FAILED(hrc))
        return S_OK;

    ComPtr<IHost> ptrHost;
    hrc = ptrVirtualBox->COMGETTER(Host)(ptrHost.asOutParam());
    if (FAILED(hrc))
        return S_OK;

    /* Domain name: */
    {
        com::Bstr bstrDomain;
        ptrHost->COMGETTER(DomainName)(bstrDomain.asOutParam());
        com::Utf8Str const strDomainName(bstrDomain);
        int vrc = RTStrCopy(DnsConfig.Core.szDomainName, sizeof(DnsConfig.Core.szDomainName), strDomainName.c_str());
        AssertRC(vrc);
    }
    Log(("domain name = \"%s\"\n", DnsConfig.Core.szDomainName));

    /* Name servers: */
    {
        SafeArray<BSTR> nameServers;
        hrc = ptrHost->COMGETTER(NameServers)(ComSafeArrayAsOutParam(nameServers));
        if (FAILED(hrc))
            return S_OK;
        DnsConfig.Core.papszNameServers = bstrSafeArrayToC(nameServers, &DnsConfig.Core.cNameServers);
        if (!DnsConfig.Core.papszNameServers)
            return E_OUTOFMEMORY;
    }
    Log(("DNS change - %zu nameservers\n", DnsConfig.Core.cNameServers));
    for (size_t i = 0; i < DnsConfig.Core.cNameServers; i++)
        Log(("- papszNameServers[%zu] = \"%s\"\n", i, DnsConfig.Core.papszNameServers[i]));

    /* Search domains: */
    {
        SafeArray<BSTR> searchDomains;
        hrc = ptrHost->COMGETTER(SearchStrings)(ComSafeArrayAsOutParam(searchDomains));
        if (FAILED(hrc))
            return S_OK;
        DnsConfig.Core.papszSearchDomains = bstrSafeArrayToC(searchDomains, &DnsConfig.Core.cSearchDomains);
        if (!DnsConfig.Core.papszSearchDomains)
            return E_OUTOFMEMORY;
    }
    Log(("Search Domain change - %u domains\n", DnsConfig.Core.cSearchDomains));
    for (size_t i = 0; i < DnsConfig.Core.cSearchDomains; i++)
        Log(("- papszSearchDomain[%zu] = \"%s\"\n", i, DnsConfig.Core.papszSearchDomains[i]));

    /*
     * Notify all the NAT drivers.
     */
    SafeVMPtrQuiet ptrVM(this);
    if (ptrVM.isOk())
        ptrVM.vtable()->pfnPDMR3DriverEnumInstances(ptrVM.rawUVM(), "NAT", Console::notifyNatDnsChangeCallback, &DnsConfig.Core);

    return S_OK;
}

/**
 * @callback_method_impl{FNPDMENUMDRVINS,Helper for Console::i_onNATDnsChanged.}
 */
/*static*/ DECLCALLBACK(int)
Console::notifyNatDnsChangeCallback(PPDMIBASE pIBase, uint32_t uDrvInstance, bool fUsbDev, const char *pszDevice,
                                    uint32_t uDevInstance, unsigned uLun, void *pvUser)
{
    PPDMINETWORKNATCONFIG const pINetNatCfg = (PPDMINETWORKNATCONFIG)pIBase->pfnQueryInterface(pIBase, PDMINETWORKNATCONFIG_IID);
    if (pINetNatCfg && pINetNatCfg->pfnNotifyDnsChanged)
    {
        LogFunc(("Notifying instance #%u attached to %s%s#%u on lun #%u...\n",
                 uDrvInstance, fUsbDev ? "usb device " : "", pszDevice, uDevInstance, uLun));
        pINetNatCfg->pfnNotifyDnsChanged(pINetNatCfg, (PCPDMINETWORKNATDNSCONFIG)pvUser);
    }
    else
        LogFunc(("Not notifying instance #%u attached to %s%s#%u on lun #%u: pINetNatCfg=%p pfnNotifyDnsChanged=%p\n",
                 uDrvInstance, fUsbDev ? "usb device " : "",  pszDevice, uDevInstance, uLun,
                 pINetNatCfg, pINetNatCfg ? pINetNatCfg->pfnNotifyDnsChanged : NULL));

    RT_NOREF(uDrvInstance, fUsbDev, pszDevice, uDevInstance, uLun);
    return VINF_SUCCESS;
}

VMMDevMouseInterface *Console::i_getVMMDevMouseInterface()
{
    return m_pVMMDev;
}

DisplayMouseInterface *Console::i_getDisplayMouseInterface()
{
    return mDisplay;
}

/**
 * Parses one key value pair.
 *
 * @returns VBox status code.
 * @param   psz     Configuration string.
 * @param   ppszEnd Where to store the pointer to the string following the key value pair.
 * @param   ppszKey Where to store the key on success.
 * @param   ppszVal Where to store the value on success.
 */
int Console::i_consoleParseKeyValue(const char *psz, const char **ppszEnd, char **ppszKey, char **ppszVal)
{
    const char *pszKeyStart = psz;
    while (   *psz != '='
           && *psz)
        psz++;

    /* End of string at this point is invalid. */
    if (*psz == '\0')
        return VERR_INVALID_PARAMETER;

    size_t const cchKey = psz - pszKeyStart;

    psz++; /* Skip '=' character */
    const char *pszValStart = psz;

    while (   *psz != ','
           && *psz != '\n'
           && *psz != '\r'
           && *psz)
        psz++;
    size_t const cchVal = psz - pszValStart;

    int vrc = VINF_SUCCESS;
    if (cchKey && cchVal)
    {
        *ppszKey = RTStrDupN(pszKeyStart, cchKey);
        if (*ppszKey)
        {
            *ppszVal = RTStrDupN(pszValStart, cchVal);
            if (*ppszVal)
                *ppszEnd = psz;
            else
            {
                RTStrFree(*ppszKey);
                vrc = VERR_NO_STR_MEMORY;
            }
        }
        else
            vrc = VERR_NO_STR_MEMORY;
    }
    else
        vrc = VERR_INVALID_PARAMETER;

    return vrc;
}

/**
 * Initializes the secret key interface on all configured attachments.
 *
 * @returns COM status code.
 */
HRESULT Console::i_initSecretKeyIfOnAllAttachments(void)
{
    HRESULT hrc = S_OK;
    SafeIfaceArray<IMediumAttachment> sfaAttachments;

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /* Get the VM - must be done before the read-locking. */
    SafeVMPtr ptrVM(this);
    if (!ptrVM.isOk())
        return ptrVM.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    hrc = mMachine->COMGETTER(MediumAttachments)(ComSafeArrayAsOutParam(sfaAttachments));
    AssertComRCReturnRC(hrc);

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
    m_cDisksPwProvided = 0;
#endif

    /* Find the correct attachment. */
    for (unsigned i = 0; i < sfaAttachments.size(); i++)
    {
        const ComPtr<IMediumAttachment> &pAtt = sfaAttachments[i];

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
        ComPtr<IMedium> pMedium;
        ComPtr<IMedium> pBase;

        hrc = pAtt->COMGETTER(Medium)(pMedium.asOutParam());
        AssertComRC(hrc);

        bool fKeepSecIf = false;
        /* Skip non hard disk attachments. */
        if (pMedium.isNotNull())
        {
            /* Get the UUID of the base medium and compare. */
            hrc = pMedium->COMGETTER(Base)(pBase.asOutParam());
            AssertComRC(hrc);

            Bstr bstrKeyId;
            hrc = pBase->GetProperty(Bstr("CRYPT/KeyId").raw(), bstrKeyId.asOutParam());
            if (SUCCEEDED(hrc))
            {
                Utf8Str strKeyId(bstrKeyId);
                SecretKey *pKey = NULL;
                int vrc = m_pKeyStore->retainSecretKey(strKeyId, &pKey);
                if (RT_SUCCESS(vrc))
                {
                    fKeepSecIf = true;
                    m_pKeyStore->releaseSecretKey(strKeyId);
                }
            }
        }
#endif

        /*
         * Query storage controller, port and device
         * to identify the correct driver.
         */
        ComPtr<IStorageController> pStorageCtrl;
        Bstr storageCtrlName;
        LONG lPort, lDev;
        ULONG ulStorageCtrlInst;

        hrc = pAtt->COMGETTER(Controller)(storageCtrlName.asOutParam());
        AssertComRC(hrc);

        hrc = pAtt->COMGETTER(Port)(&lPort);
        AssertComRC(hrc);

        hrc = pAtt->COMGETTER(Device)(&lDev);
        AssertComRC(hrc);

        hrc = mMachine->GetStorageControllerByName(storageCtrlName.raw(), pStorageCtrl.asOutParam());
        AssertComRC(hrc);

        hrc = pStorageCtrl->COMGETTER(Instance)(&ulStorageCtrlInst);
        AssertComRC(hrc);

        StorageControllerType_T enmCtrlType;
        hrc = pStorageCtrl->COMGETTER(ControllerType)(&enmCtrlType);
        AssertComRC(hrc);
        const char *pcszDevice = i_storageControllerTypeToStr(enmCtrlType);

        StorageBus_T enmBus;
        hrc = pStorageCtrl->COMGETTER(Bus)(&enmBus);
        AssertComRC(hrc);

        unsigned uLUN;
        hrc = Console::i_storageBusPortDeviceToLun(enmBus, lPort, lDev, uLUN);
        AssertComRC(hrc);

        PPDMIBASE pIBase = NULL;
        PPDMIMEDIA pIMedium = NULL;
        int vrc = ptrVM.vtable()->pfnPDMR3QueryDriverOnLun(ptrVM.rawUVM(), pcszDevice, ulStorageCtrlInst, uLUN, "VD", &pIBase);
        if (RT_SUCCESS(vrc))
        {
            if (pIBase)
            {
                pIMedium = (PPDMIMEDIA)pIBase->pfnQueryInterface(pIBase, PDMIMEDIA_IID);
                if (pIMedium)
                {
#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
                    vrc = pIMedium->pfnSetSecKeyIf(pIMedium, fKeepSecIf ? mpIfSecKey : NULL, mpIfSecKeyHlp);
                    Assert(RT_SUCCESS(vrc) || vrc == VERR_NOT_SUPPORTED);
                    if (fKeepSecIf)
                        m_cDisksPwProvided++;
#else
                    vrc = pIMedium->pfnSetSecKeyIf(pIMedium, NULL, mpIfSecKeyHlp);
                    Assert(RT_SUCCESS(vrc) || vrc == VERR_NOT_SUPPORTED);
#endif
                }
            }
        }
    }

    return hrc;
}

/**
 * Removes the key interfaces from all disk attachments with the given key ID.
 * Useful when changing the key store or dropping it.
 *
 * @returns COM status code.
 * @param   strId    The ID to look for.
 */
HRESULT Console::i_clearDiskEncryptionKeysOnAllAttachmentsWithKeyId(const Utf8Str &strId)
{
    HRESULT hrc = S_OK;
    SafeIfaceArray<IMediumAttachment> sfaAttachments;

    /* Get the VM - must be done before the read-locking. */
    SafeVMPtr ptrVM(this);
    if (!ptrVM.isOk())
        return ptrVM.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    hrc = mMachine->COMGETTER(MediumAttachments)(ComSafeArrayAsOutParam(sfaAttachments));
    AssertComRCReturnRC(hrc);

    /* Find the correct attachment. */
    for (unsigned i = 0; i < sfaAttachments.size(); i++)
    {
        const ComPtr<IMediumAttachment> &pAtt = sfaAttachments[i];
        ComPtr<IMedium> pMedium;
        ComPtr<IMedium> pBase;
        Bstr bstrKeyId;

        hrc = pAtt->COMGETTER(Medium)(pMedium.asOutParam());
        if (FAILED(hrc))
            break;

        /* Skip non hard disk attachments. */
        if (pMedium.isNull())
            continue;

        /* Get the UUID of the base medium and compare. */
        hrc = pMedium->COMGETTER(Base)(pBase.asOutParam());
        if (FAILED(hrc))
            break;

        hrc = pBase->GetProperty(Bstr("CRYPT/KeyId").raw(), bstrKeyId.asOutParam());
        if (hrc == VBOX_E_OBJECT_NOT_FOUND)
        {
            hrc = S_OK;
            continue;
        }
        else if (FAILED(hrc))
            break;

        if (strId.equals(Utf8Str(bstrKeyId)))
        {

            /*
             * Query storage controller, port and device
             * to identify the correct driver.
             */
            ComPtr<IStorageController> pStorageCtrl;
            Bstr storageCtrlName;
            LONG lPort, lDev;
            ULONG ulStorageCtrlInst;

            hrc = pAtt->COMGETTER(Controller)(storageCtrlName.asOutParam());
            AssertComRC(hrc);

            hrc = pAtt->COMGETTER(Port)(&lPort);
            AssertComRC(hrc);

            hrc = pAtt->COMGETTER(Device)(&lDev);
            AssertComRC(hrc);

            hrc = mMachine->GetStorageControllerByName(storageCtrlName.raw(), pStorageCtrl.asOutParam());
            AssertComRC(hrc);

            hrc = pStorageCtrl->COMGETTER(Instance)(&ulStorageCtrlInst);
            AssertComRC(hrc);

            StorageControllerType_T enmCtrlType;
            hrc = pStorageCtrl->COMGETTER(ControllerType)(&enmCtrlType);
            AssertComRC(hrc);
            const char *pcszDevice = i_storageControllerTypeToStr(enmCtrlType);

            StorageBus_T enmBus;
            hrc = pStorageCtrl->COMGETTER(Bus)(&enmBus);
            AssertComRC(hrc);

            unsigned uLUN;
            hrc = Console::i_storageBusPortDeviceToLun(enmBus, lPort, lDev, uLUN);
            AssertComRC(hrc);

            PPDMIBASE pIBase = NULL;
            PPDMIMEDIA pIMedium = NULL;
            int vrc = ptrVM.vtable()->pfnPDMR3QueryDriverOnLun(ptrVM.rawUVM(), pcszDevice, ulStorageCtrlInst, uLUN, "VD", &pIBase);
            if (RT_SUCCESS(vrc))
            {
                if (pIBase)
                {
                    pIMedium = (PPDMIMEDIA)pIBase->pfnQueryInterface(pIBase, PDMIMEDIA_IID);
                    if (pIMedium)
                    {
                        vrc = pIMedium->pfnSetSecKeyIf(pIMedium, NULL, mpIfSecKeyHlp);
                        Assert(RT_SUCCESS(vrc) || vrc == VERR_NOT_SUPPORTED);
                    }
                }
            }
        }
    }

    return hrc;
}

/**
 * Configures the encryption support for the disk which have encryption conigured
 * with the configured key.
 *
 * @returns COM status code.
 * @param   strId                The ID of the password.
 * @param   pcDisksConfigured    Where to store the number of disks configured for the given ID.
 */
HRESULT Console::i_configureEncryptionForDisk(const com::Utf8Str &strId, unsigned *pcDisksConfigured)
{
    unsigned cDisksConfigured = 0;
    HRESULT hrc = S_OK;
    SafeIfaceArray<IMediumAttachment> sfaAttachments;

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /* Get the VM - must be done before the read-locking. */
    SafeVMPtr ptrVM(this);
    if (!ptrVM.isOk())
        return ptrVM.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    hrc = mMachine->COMGETTER(MediumAttachments)(ComSafeArrayAsOutParam(sfaAttachments));
    if (FAILED(hrc))
        return hrc;

    /* Find the correct attachment. */
    for (unsigned i = 0; i < sfaAttachments.size(); i++)
    {
        const ComPtr<IMediumAttachment> &pAtt = sfaAttachments[i];
        ComPtr<IMedium> pMedium;
        ComPtr<IMedium> pBase;
        Bstr bstrKeyId;

        hrc = pAtt->COMGETTER(Medium)(pMedium.asOutParam());
        if (FAILED(hrc))
            break;

        /* Skip non hard disk attachments. */
        if (pMedium.isNull())
            continue;

        /* Get the UUID of the base medium and compare. */
        hrc = pMedium->COMGETTER(Base)(pBase.asOutParam());
        if (FAILED(hrc))
            break;

        hrc = pBase->GetProperty(Bstr("CRYPT/KeyId").raw(), bstrKeyId.asOutParam());
        if (hrc == VBOX_E_OBJECT_NOT_FOUND)
        {
            hrc = S_OK;
            continue;
        }
        else if (FAILED(hrc))
            break;

        if (strId.equals(Utf8Str(bstrKeyId)))
        {
            /*
             * Found the matching medium, query storage controller, port and device
             * to identify the correct driver.
             */
            ComPtr<IStorageController> pStorageCtrl;
            Bstr storageCtrlName;
            LONG lPort, lDev;
            ULONG ulStorageCtrlInst;

            hrc = pAtt->COMGETTER(Controller)(storageCtrlName.asOutParam());
            if (FAILED(hrc))
                break;

            hrc = pAtt->COMGETTER(Port)(&lPort);
            if (FAILED(hrc))
                break;

            hrc = pAtt->COMGETTER(Device)(&lDev);
            if (FAILED(hrc))
                break;

            hrc = mMachine->GetStorageControllerByName(storageCtrlName.raw(), pStorageCtrl.asOutParam());
            if (FAILED(hrc))
                break;

            hrc = pStorageCtrl->COMGETTER(Instance)(&ulStorageCtrlInst);
            if (FAILED(hrc))
                break;

            StorageControllerType_T enmCtrlType;
            hrc = pStorageCtrl->COMGETTER(ControllerType)(&enmCtrlType);
            AssertComRC(hrc);
            const char *pcszDevice = i_storageControllerTypeToStr(enmCtrlType);

            StorageBus_T enmBus;
            hrc = pStorageCtrl->COMGETTER(Bus)(&enmBus);
            AssertComRC(hrc);

            unsigned uLUN;
            hrc = Console::i_storageBusPortDeviceToLun(enmBus, lPort, lDev, uLUN);
            AssertComRCReturnRC(hrc);

            PPDMIBASE pIBase = NULL;
            PPDMIMEDIA pIMedium = NULL;
            int vrc = ptrVM.vtable()->pfnPDMR3QueryDriverOnLun(ptrVM.rawUVM(), pcszDevice, ulStorageCtrlInst, uLUN, "VD", &pIBase);
            if (RT_SUCCESS(vrc))
            {
                if (pIBase)
                {
                    pIMedium = (PPDMIMEDIA)pIBase->pfnQueryInterface(pIBase, PDMIMEDIA_IID);
                    if (!pIMedium)
                        return setError(E_FAIL, tr("could not query medium interface of controller"));
                    vrc = pIMedium->pfnSetSecKeyIf(pIMedium, mpIfSecKey, mpIfSecKeyHlp);
                    if (vrc == VERR_VD_PASSWORD_INCORRECT)
                    {
                        hrc = setError(VBOX_E_PASSWORD_INCORRECT,
                                       tr("The provided password for ID \"%s\" is not correct for at least one disk using this ID"),
                                       strId.c_str());
                        break;
                    }
                    else if (RT_FAILURE(vrc))
                    {
                        hrc = setErrorBoth(E_FAIL, vrc, tr("Failed to set the encryption key (%Rrc)"), vrc);
                        break;
                    }

                    if (RT_SUCCESS(vrc))
                       cDisksConfigured++;
                }
                else
                    return setError(E_FAIL, tr("could not query base interface of controller"));
            }
        }
    }

    if (   SUCCEEDED(hrc)
        && pcDisksConfigured)
        *pcDisksConfigured = cDisksConfigured;
    else if (FAILED(hrc))
    {
        /* Clear disk encryption setup on successfully configured attachments. */
        ErrorInfoKeeper eik; /* Keep current error info or it gets deestroyed in the IPC methods below. */
        i_clearDiskEncryptionKeysOnAllAttachmentsWithKeyId(strId);
    }

    return hrc;
}

/**
 * Parses the encryption configuration for one disk.
 *
 * @returns COM status code.
 * @param   psz     Pointer to the configuration for the encryption of one disk.
 * @param   ppszEnd Pointer to the string following encrpytion configuration.
 */
HRESULT Console::i_consoleParseDiskEncryption(const char *psz, const char **ppszEnd)
{
    char *pszUuid = NULL;
    char *pszKeyEnc = NULL;
    int vrc = VINF_SUCCESS;
    HRESULT hrc = S_OK;

    while (   *psz
           && RT_SUCCESS(vrc))
    {
        char *pszKey = NULL;
        char *pszVal = NULL;
        const char *pszEnd = NULL;

        vrc = i_consoleParseKeyValue(psz, &pszEnd, &pszKey, &pszVal);
        if (RT_SUCCESS(vrc))
        {
            if (!RTStrCmp(pszKey, "uuid"))
                pszUuid = pszVal;
            else if (!RTStrCmp(pszKey, "dek"))
                pszKeyEnc = pszVal;
            else
                vrc = VERR_INVALID_PARAMETER;

            RTStrFree(pszKey);

            if (*pszEnd == ',')
                psz = pszEnd + 1;
            else
            {
                /*
                 * End of the configuration for the current disk, skip linefeed and
                 * carriage returns.
                 */
                while (   *pszEnd == '\n'
                       || *pszEnd == '\r')
                    pszEnd++;

                psz = pszEnd;
                break; /* Stop parsing */
            }

        }
    }

    if (   RT_SUCCESS(vrc)
        && pszUuid
        && pszKeyEnc)
    {
        ssize_t cbKey = 0;

        /* Decode the key. */
        cbKey = RTBase64DecodedSize(pszKeyEnc, NULL);
        if (cbKey != -1)
        {
            uint8_t *pbKey;
            vrc = RTMemSaferAllocZEx((void **)&pbKey, cbKey, RTMEMSAFER_F_REQUIRE_NOT_PAGABLE);
            if (RT_SUCCESS(vrc))
            {
                vrc = RTBase64Decode(pszKeyEnc, pbKey, cbKey, NULL, NULL);
                if (RT_SUCCESS(vrc))
                {
                    vrc = m_pKeyStore->addSecretKey(Utf8Str(pszUuid), pbKey, cbKey);
                    if (RT_SUCCESS(vrc))
                    {
                        hrc = i_configureEncryptionForDisk(Utf8Str(pszUuid), NULL);
                        if (FAILED(hrc))
                        {
                            /* Delete the key from the map. */
                            vrc = m_pKeyStore->deleteSecretKey(Utf8Str(pszUuid));
                            AssertRC(vrc);
                        }
                    }
                }
                else
                    hrc = setErrorBoth(E_FAIL, vrc, tr("Failed to decode the key (%Rrc)"), vrc);

                RTMemSaferFree(pbKey, cbKey);
            }
            else
                hrc = setErrorBoth(E_FAIL, vrc, tr("Failed to allocate secure memory for the key (%Rrc)"), vrc);
        }
        else
            hrc = setError(E_FAIL, tr("The base64 encoding of the passed key is incorrect"));
    }
    else if (RT_SUCCESS(vrc))
        hrc = setError(E_FAIL, tr("The encryption configuration is incomplete"));

    if (pszUuid)
        RTStrFree(pszUuid);
    if (pszKeyEnc)
    {
        RTMemWipeThoroughly(pszKeyEnc, strlen(pszKeyEnc), 10 /* cMinPasses */);
        RTStrFree(pszKeyEnc);
    }

    if (ppszEnd)
        *ppszEnd = psz;

    return hrc;
}

HRESULT Console::i_setDiskEncryptionKeys(const Utf8Str &strCfg)
{
    HRESULT hrc = S_OK;
    const char *pszCfg = strCfg.c_str();

    while (   *pszCfg
           && SUCCEEDED(hrc))
    {
        const char *pszNext = NULL;
        hrc = i_consoleParseDiskEncryption(pszCfg, &pszNext);
        pszCfg = pszNext;
    }

    return hrc;
}

void Console::i_removeSecretKeysOnSuspend()
{
    /* Remove keys which are supposed to be removed on a suspend. */
    int vrc = m_pKeyStore->deleteAllSecretKeys(true /* fSuspend */, true /* fForce */);
    AssertRC(vrc);
}

/**
 * Process a network adaptor change.
 *
 * @returns COM status code.
 *
 * @param   pUVM                The VM handle (caller hold this safely).
 * @param   pVMM                The VMM vtable.
 * @param   pszDevice           The PDM device name.
 * @param   uInstance           The PDM device instance.
 * @param   uLun                The PDM LUN number of the drive.
 * @param   aNetworkAdapter     The network adapter whose attachment needs to be changed
 */
HRESULT Console::i_doNetworkAdapterChange(PUVM pUVM, PCVMMR3VTABLE pVMM, const char *pszDevice,
                                          unsigned uInstance, unsigned uLun, INetworkAdapter *aNetworkAdapter)
{
    LogFlowThisFunc(("pszDevice=%p:{%s} uInstance=%u uLun=%u aNetworkAdapter=%p\n",
                      pszDevice, pszDevice, uInstance, uLun, aNetworkAdapter));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /*
     * Suspend the VM first.
     */
    bool fResume = false;
    HRESULT hr = i_suspendBeforeConfigChange(pUVM, pVMM, NULL, &fResume);
    if (FAILED(hr))
        return hr;

    /*
     * Call worker in EMT, that's faster and safer than doing everything
     * using VM3ReqCall. Note that we separate VMR3ReqCall from VMR3ReqWait
     * here to make requests from under the lock in order to serialize them.
     */
    int vrc = pVMM->pfnVMR3ReqCallWaitU(pUVM, 0 /*idDstCpu*/, (PFNRT)i_changeNetworkAttachment, 7,
                                        this, pUVM, pVMM, pszDevice, uInstance, uLun, aNetworkAdapter);

    if (fResume)
        i_resumeAfterConfigChange(pUVM, pVMM);

    if (RT_SUCCESS(vrc))
        return S_OK;

    return setErrorBoth(E_FAIL, vrc,  tr("Could not change the network adaptor attachement type (%Rrc)"), vrc);
}


/**
 * Performs the Network Adaptor change in EMT.
 *
 * @returns VBox status code.
 *
 * @param   pThis               Pointer to the Console object.
 * @param   pUVM                The VM handle.
 * @param   pVMM                The VMM vtable.
 * @param   pszDevice           The PDM device name.
 * @param   uInstance           The PDM device instance.
 * @param   uLun                The PDM LUN number of the drive.
 * @param   aNetworkAdapter     The network adapter whose attachment needs to be changed
 *
 * @thread  EMT
 * @note Locks the Console object for writing.
 * @note The VM must not be running.
 */
/*static*/ DECLCALLBACK(int) Console::i_changeNetworkAttachment(Console *pThis,
                                                                PUVM pUVM,
                                                                PCVMMR3VTABLE pVMM,
                                                                const char *pszDevice,
                                                                unsigned uInstance,
                                                                unsigned uLun,
                                                                INetworkAdapter *aNetworkAdapter)
{
    LogFlowFunc(("pThis=%p pszDevice=%p:{%s} uInstance=%u uLun=%u aNetworkAdapter=%p\n",
                 pThis, pszDevice, pszDevice, uInstance, uLun, aNetworkAdapter));

    AssertReturn(pThis, VERR_INVALID_PARAMETER);

    AutoCaller autoCaller(pThis);
    AssertComRCReturn(autoCaller.hrc(), VERR_ACCESS_DENIED);

#ifdef VBOX_STRICT
    ComPtr<IPlatform> pPlatform;
    HRESULT hrc = pThis->mMachine->COMGETTER(Platform)(pPlatform.asOutParam());
    AssertComRC(hrc);

    PlatformArchitecture_T platformArch;
    hrc = pPlatform->COMGETTER(Architecture)(&platformArch);
    AssertComRC(hrc);

    ComPtr<IVirtualBox> pVirtualBox;
    pThis->mMachine->COMGETTER(Parent)(pVirtualBox.asOutParam());

    ComPtr<IPlatformProperties> pPlatformProperties;
    hrc = pVirtualBox->GetPlatformProperties(platformArch, pPlatformProperties.asOutParam());
    AssertComRC(hrc);

    ChipsetType_T chipsetType = ChipsetType_PIIX3;
    pPlatform->COMGETTER(ChipsetType)(&chipsetType);
    AssertComRC(hrc);

    ULONG maxNetworkAdapters = 0;
    hrc = pPlatformProperties->GetMaxNetworkAdapters(chipsetType, &maxNetworkAdapters);
    AssertComRC(hrc);
    AssertMsg(uLun == 0 && uInstance < maxNetworkAdapters,
              ("pszDevice=%s uLun=%d uInstance=%d\n", pszDevice, uLun, uInstance));
#endif
    Log(("pszDevice=%s uLun=%d uInstance=%d\n", pszDevice, uLun, uInstance));

    /*
     * Check the VM for correct state.
     */
    PCFGMNODE pCfg   = NULL;        /* /Devices/Dev/.../Config/ */
    PCFGMNODE pLunL0 = NULL;        /* /Devices/Dev/0/LUN#0/ */
    PCFGMNODE pInst  = pVMM->pfnCFGMR3GetChildF(pVMM->pfnCFGMR3GetRootU(pUVM), "Devices/%s/%d/", pszDevice, uInstance);
    AssertLogRelMsgReturn(pInst, ("pszDevices=%s uInstance=%u\n", pszDevice, uInstance), VERR_CFGM_CHILD_NOT_FOUND);

    int vrc = pThis->i_configNetwork(pszDevice, uInstance, uLun, aNetworkAdapter, pCfg, pLunL0, pInst,
                                     true /*fAttachDetach*/, false /*fIgnoreConnectFailure*/, pUVM, pVMM);

    LogFlowFunc(("Returning %Rrc\n", vrc));
    return vrc;
}

/**
 * Returns the device name of a given audio adapter.
 *
 * @returns Device name, or an empty string if no device is configured.
 * @param   aAudioAdapter       Audio adapter to return device name for.
 */
Utf8Str Console::i_getAudioAdapterDeviceName(IAudioAdapter *aAudioAdapter)
{
    Utf8Str strDevice;

    AudioControllerType_T audioController;
    HRESULT hrc = aAudioAdapter->COMGETTER(AudioController)(&audioController);
    AssertComRC(hrc);
    if (SUCCEEDED(hrc))
    {
        switch (audioController)
        {
            case AudioControllerType_HDA:         strDevice = "hda";          break;
            case AudioControllerType_AC97:        strDevice = "ichac97";      break;
            case AudioControllerType_SB16:        strDevice = "sb16";         break;
            case AudioControllerType_VirtioSound: strDevice = "virtio-sound"; break;
            default:                                                          break; /* None. */
        }
    }

    return strDevice;
}

/**
 * Called by IInternalSessionControl::OnAudioAdapterChange().
 */
HRESULT Console::i_onAudioAdapterChange(IAudioAdapter *aAudioAdapter)
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    /* don't trigger audio changes if the VM isn't running */
    SafeVMPtrQuiet ptrVM(this);
    if (ptrVM.isOk())
    {
        BOOL fEnabledIn, fEnabledOut;
        hrc = aAudioAdapter->COMGETTER(EnabledIn)(&fEnabledIn);
        AssertComRC(hrc);
        if (SUCCEEDED(hrc))
        {
            hrc = aAudioAdapter->COMGETTER(EnabledOut)(&fEnabledOut);
            AssertComRC(hrc);
            if (SUCCEEDED(hrc))
            {
                int vrc = VINF_SUCCESS;

                for (ULONG ulLUN = 0; ulLUN < 16 /** @todo Use a define */; ulLUN++)
                {
                    PPDMIBASE pBase;
                    int vrc2 = ptrVM.vtable()->pfnPDMR3QueryDriverOnLun(ptrVM.rawUVM(),
                                                                        i_getAudioAdapterDeviceName(aAudioAdapter).c_str(),
                                                                        0 /* iInstance */, ulLUN, "AUDIO", &pBase);
                    if (RT_FAILURE(vrc2))
                        continue;

                    if (pBase)
                    {
                        PPDMIAUDIOCONNECTOR pAudioCon = (PPDMIAUDIOCONNECTOR)pBase->pfnQueryInterface(pBase,
                                                                                                      PDMIAUDIOCONNECTOR_IID);
                        if (   pAudioCon
                            && pAudioCon->pfnEnable)
                        {
                            int vrcIn = pAudioCon->pfnEnable(pAudioCon, PDMAUDIODIR_IN, RT_BOOL(fEnabledIn));
                            if (RT_FAILURE(vrcIn))
                                LogRel(("Audio: Failed to %s input of LUN#%RU32, vrcIn=%Rrc\n",
                                        fEnabledIn ? "enable" : "disable", ulLUN, vrcIn));

                            if (RT_SUCCESS(vrc))
                                vrc = vrcIn;

                            int vrcOut = pAudioCon->pfnEnable(pAudioCon, PDMAUDIODIR_OUT, RT_BOOL(fEnabledOut));
                            if (RT_FAILURE(vrcOut))
                                LogRel(("Audio: Failed to %s output of LUN#%RU32, vrcOut=%Rrc\n",
                                        fEnabledIn ? "enable" : "disable", ulLUN, vrcOut));

                            if (RT_SUCCESS(vrc))
                                vrc = vrcOut;
                        }
                    }
                }

                if (RT_SUCCESS(vrc))
                    LogRel(("Audio: Status has changed (input is %s, output is %s)\n",
                            fEnabledIn  ? "enabled" : "disabled", fEnabledOut ? "enabled" : "disabled"));
            }
        }

        ptrVM.release();
    }

    alock.release();

    /* notify console callbacks on success */
    if (SUCCEEDED(hrc))
        ::FireAudioAdapterChangedEvent(mEventSource, aAudioAdapter);

    LogFlowThisFunc(("Leaving S_OKn"));
    return S_OK;
}

/**
 * Called by IInternalSessionControl::OnHostAudioDeviceChange().
 */
HRESULT Console::i_onHostAudioDeviceChange(IHostAudioDevice *aDevice, BOOL aNew, AudioDeviceState_T aState,
                                           IVirtualBoxErrorInfo *aErrInfo)
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    /** @todo Implement logic here. */

    alock.release();

    /* notify console callbacks on success */
    if (SUCCEEDED(hrc))
        ::FireHostAudioDeviceChangedEvent(mEventSource, aDevice, aNew, aState, aErrInfo);

    LogFlowThisFunc(("Leaving S_OK\n"));
    return S_OK;
}

/**
 * Performs the Serial Port attachment change in EMT.
 *
 * @returns VBox status code.
 *
 * @param   pThis               Pointer to the Console object.
 * @param   pUVM                The VM handle.
 * @param   pVMM                The VMM vtable.
 * @param   pSerialPort         The serial port whose attachment needs to be changed
 *
 * @thread  EMT
 * @note Locks the Console object for writing.
 * @note The VM must not be running.
 */
DECLCALLBACK(int) Console::i_changeSerialPortAttachment(Console *pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, ISerialPort *pSerialPort)
{
    LogFlowFunc(("pThis=%p pUVM=%p pSerialPort=%p\n", pThis, pUVM, pSerialPort));

    AssertReturn(pThis, VERR_INVALID_PARAMETER);

    AutoCaller autoCaller(pThis);
    AssertComRCReturn(autoCaller.hrc(), VERR_ACCESS_DENIED);

    AutoWriteLock alock(pThis COMMA_LOCKVAL_SRC_POS);

    /*
     * Check the VM for correct state.
     */
    VMSTATE enmVMState = pVMM->pfnVMR3GetStateU(pUVM);
    AssertReturn(enmVMState == VMSTATE_SUSPENDED, VERR_INVALID_STATE);

    HRESULT hrc = S_OK;
    int vrc = VINF_SUCCESS;
    ULONG ulSlot;
    hrc = pSerialPort->COMGETTER(Slot)(&ulSlot);
    if (SUCCEEDED(hrc))
    {
        /* Check whether the port mode changed and act accordingly. */
        Assert(ulSlot < 4);

        PortMode_T eHostMode;
        hrc = pSerialPort->COMGETTER(HostMode)(&eHostMode);
        if (SUCCEEDED(hrc))
        {
            PCFGMNODE pInst = pVMM->pfnCFGMR3GetChildF(pVMM->pfnCFGMR3GetRootU(pUVM), "Devices/serial/%d/", ulSlot);
            AssertRelease(pInst);

            /* Remove old driver. */
            if (pThis->m_aeSerialPortMode[ulSlot] != PortMode_Disconnected)
            {
                vrc = pVMM->pfnPDMR3DeviceDetach(pUVM, "serial", ulSlot, 0, 0);
                PCFGMNODE pLunL0 = pVMM->pfnCFGMR3GetChildF(pInst, "LUN#0");
                pVMM->pfnCFGMR3RemoveNode(pLunL0);
            }

            if (RT_SUCCESS(vrc))
            {
                BOOL fServer;
                Bstr bstrPath;
                hrc = pSerialPort->COMGETTER(Server)(&fServer);
                if (SUCCEEDED(hrc))
                    hrc = pSerialPort->COMGETTER(Path)(bstrPath.asOutParam());

                /* Configure new driver. */
                if (   SUCCEEDED(hrc)
                    && eHostMode != PortMode_Disconnected)
                {
                    vrc = pThis->i_configSerialPort(pInst, eHostMode, Utf8Str(bstrPath).c_str(), RT_BOOL(fServer));
                    if (RT_SUCCESS(vrc))
                    {
                        /*
                         * Attach the driver.
                         */
                        PPDMIBASE pBase;
                        vrc = pVMM->pfnPDMR3DeviceAttach(pUVM, "serial", ulSlot, 0, 0, &pBase);

                        pVMM->pfnCFGMR3Dump(pInst);
                    }
                }
            }
        }
    }

    if (RT_SUCCESS(vrc) && FAILED(hrc))
        vrc = VERR_INTERNAL_ERROR;

    LogFlowFunc(("Returning %Rrc\n", vrc));
    return vrc;
}


/**
 * Called by IInternalSessionControl::OnSerialPortChange().
 */
HRESULT Console::i_onSerialPortChange(ISerialPort *aSerialPort)
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    HRESULT hrc = S_OK;

    /* don't trigger audio changes if the VM isn't running */
    SafeVMPtrQuiet ptrVM(this);
    if (ptrVM.isOk())
    {
        ULONG ulSlot;
        BOOL fEnabled = FALSE;
        hrc = aSerialPort->COMGETTER(Slot)(&ulSlot);
        if (SUCCEEDED(hrc))
            hrc = aSerialPort->COMGETTER(Enabled)(&fEnabled);
        if (SUCCEEDED(hrc) && fEnabled)
        {
            /* Check whether the port mode changed and act accordingly. */
            Assert(ulSlot < 4);

            PortMode_T eHostMode;
            hrc = aSerialPort->COMGETTER(HostMode)(&eHostMode);
            if (SUCCEEDED(hrc) && m_aeSerialPortMode[ulSlot] != eHostMode)
            {
                /*
                 * Suspend the VM first.
                 */
                bool fResume = false;
                hrc = i_suspendBeforeConfigChange(ptrVM.rawUVM(), ptrVM.vtable(), NULL, &fResume);
                if (FAILED(hrc))
                    return hrc;

                /*
                 * Call worker in EMT, that's faster and safer than doing everything
                 * using VM3ReqCallWait.
                 */
                int vrc = ptrVM.vtable()->pfnVMR3ReqCallWaitU(ptrVM.rawUVM(), 0 /*idDstCpu*/,
                                                              (PFNRT)i_changeSerialPortAttachment, 4,
                                                              this, ptrVM.rawUVM(), ptrVM.vtable(), aSerialPort);

                if (fResume)
                    i_resumeAfterConfigChange(ptrVM.rawUVM(), ptrVM.vtable());
                if (RT_SUCCESS(vrc))
                    m_aeSerialPortMode[ulSlot] = eHostMode;
                else
                    hrc = setErrorBoth(E_FAIL, vrc, tr("Failed to change the serial port attachment (%Rrc)"), vrc);
            }
        }
    }

    if (SUCCEEDED(hrc))
        ::FireSerialPortChangedEvent(mEventSource, aSerialPort);

    LogFlowThisFunc(("Leaving hrc=%#x\n", hrc));
    return hrc;
}

/**
 * Called by IInternalSessionControl::OnParallelPortChange().
 */
HRESULT Console::i_onParallelPortChange(IParallelPort *aParallelPort)
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    ::FireParallelPortChangedEvent(mEventSource, aParallelPort);

    LogFlowThisFunc(("Leaving S_OK\n"));
    return S_OK;
}

/**
 * Called by IInternalSessionControl::OnStorageControllerChange().
 */
HRESULT Console::i_onStorageControllerChange(const Guid &aMachineId, const Utf8Str &aControllerName)
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    ::FireStorageControllerChangedEvent(mEventSource, aMachineId.toString(), aControllerName);

    LogFlowThisFunc(("Leaving S_OK\n"));
    return S_OK;
}

/**
 * Called by IInternalSessionControl::OnMediumChange().
 */
HRESULT Console::i_onMediumChange(IMediumAttachment *aMediumAttachment, BOOL aForce)
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    HRESULT hrc = S_OK;

    /* don't trigger medium changes if the VM isn't running */
    SafeVMPtrQuiet ptrVM(this);
    if (ptrVM.isOk())
    {
        hrc = i_doMediumChange(aMediumAttachment, !!aForce, ptrVM.rawUVM(), ptrVM.vtable());
        ptrVM.release();
    }

    /* notify console callbacks on success */
    if (SUCCEEDED(hrc))
        ::FireMediumChangedEvent(mEventSource, aMediumAttachment);

    LogFlowThisFunc(("Leaving hrc=%#x\n", hrc));
    return hrc;
}

/**
 * Called by IInternalSessionControl::OnCPUChange().
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_onCPUChange(ULONG aCPU, BOOL aRemove)
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    HRESULT hrc = S_OK;

    /* don't trigger CPU changes if the VM isn't running */
    SafeVMPtrQuiet ptrVM(this);
    if (ptrVM.isOk())
    {
        if (aRemove)
            hrc = i_doCPURemove(aCPU, ptrVM.rawUVM(), ptrVM.vtable());
        else
            hrc = i_doCPUAdd(aCPU, ptrVM.rawUVM(), ptrVM.vtable());
        ptrVM.release();
    }

    /* notify console callbacks on success */
    if (SUCCEEDED(hrc))
        ::FireCPUChangedEvent(mEventSource, aCPU, aRemove);

    LogFlowThisFunc(("Leaving hrc=%#x\n", hrc));
    return hrc;
}

/**
 * Called by IInternalSessionControl::OnCpuExecutionCapChange().
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_onCPUExecutionCapChange(ULONG aExecutionCap)
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    /* don't trigger the CPU priority change if the VM isn't running */
    SafeVMPtrQuiet ptrVM(this);
    if (ptrVM.isOk())
    {
        if (   mMachineState == MachineState_Running
            || mMachineState == MachineState_Teleporting
            || mMachineState == MachineState_LiveSnapshotting
            )
        {
            /* No need to call in the EMT thread. */
            int vrc = ptrVM.vtable()->pfnVMR3SetCpuExecutionCap(ptrVM.rawUVM(), aExecutionCap);
            if (RT_FAILURE(vrc))
                hrc = setErrorBoth(E_FAIL, vrc, tr("Failed to change the CPU execution limit (%Rrc)"), vrc);
        }
        else
            hrc = i_setInvalidMachineStateError();
        ptrVM.release();
    }

    /* notify console callbacks on success */
    if (SUCCEEDED(hrc))
    {
        alock.release();
        ::FireCPUExecutionCapChangedEvent(mEventSource, aExecutionCap);
    }

    LogFlowThisFunc(("Leaving hrc=%#x\n", hrc));
    return hrc;
}

/**
 * Called by IInternalSessionControl::OnClipboardError().
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_onClipboardError(const Utf8Str &aId, const Utf8Str &aErrMsg, LONG aRc)
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    /* don't trigger the drag and drop mode change if the VM isn't running */
    SafeVMPtrQuiet ptrVM(this);
    if (ptrVM.isOk())
    {
        if (   mMachineState == MachineState_Running
            || mMachineState == MachineState_Teleporting
            || mMachineState == MachineState_LiveSnapshotting)
        {
        }
        else
            hrc = i_setInvalidMachineStateError();
        ptrVM.release();
    }

    /* notify console callbacks on success */
    if (SUCCEEDED(hrc))
    {
        alock.release();
        ::FireClipboardErrorEvent(mEventSource, aId, aErrMsg, aRc);
    }

    LogFlowThisFunc(("Leaving hrc=%#x\n", hrc));
    return hrc;
}

/**
 * Called by IInternalSessionControl::OnClipboardModeChange().
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_onClipboardModeChange(ClipboardMode_T aClipboardMode)
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    /* don't trigger the clipboard mode change if the VM isn't running */
    SafeVMPtrQuiet ptrVM(this);
    if (ptrVM.isOk())
    {
        if (   mMachineState == MachineState_Running
            || mMachineState == MachineState_Teleporting
            || mMachineState == MachineState_LiveSnapshotting)
        {
            int vrc = i_changeClipboardMode(aClipboardMode);
            if (RT_FAILURE(vrc))
                hrc = E_FAIL; /** @todo r=andy Set error info here! */
        }
        else
            hrc = i_setInvalidMachineStateError();
        ptrVM.release();
    }

    /* notify console callbacks on success */
    if (SUCCEEDED(hrc))
    {
        alock.release();
        ::FireClipboardModeChangedEvent(mEventSource, aClipboardMode);
    }

    LogFlowThisFunc(("Leaving hrc=%#x\n", hrc));
    return hrc;
}

/**
 * Called by IInternalSessionControl::OnClipboardFileTransferModeChange().
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_onClipboardFileTransferModeChange(bool aEnabled)
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    /* don't trigger the change if the VM isn't running */
    SafeVMPtrQuiet ptrVM(this);
    if (ptrVM.isOk())
    {
        if (   mMachineState == MachineState_Running
            || mMachineState == MachineState_Teleporting
            || mMachineState == MachineState_LiveSnapshotting)
        {
            int vrc = i_changeClipboardFileTransferMode(aEnabled);
            if (RT_FAILURE(vrc))
                hrc = E_FAIL; /** @todo r=andy Set error info here! */
        }
        else
            hrc = i_setInvalidMachineStateError();
        ptrVM.release();
    }

    /* notify console callbacks on success */
    if (SUCCEEDED(hrc))
    {
        alock.release();
        ::FireClipboardFileTransferModeChangedEvent(mEventSource, aEnabled ? TRUE : FALSE);
    }

    LogFlowThisFunc(("Leaving hrc=%#x\n", hrc));
    return hrc;
}

/**
 * Called by IInternalSessionControl::OnDnDModeChange().
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_onDnDModeChange(DnDMode_T aDnDMode)
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    /* don't trigger the drag and drop mode change if the VM isn't running */
    SafeVMPtrQuiet ptrVM(this);
    if (ptrVM.isOk())
    {
        if (   mMachineState == MachineState_Running
            || mMachineState == MachineState_Teleporting
            || mMachineState == MachineState_LiveSnapshotting)
            i_changeDnDMode(aDnDMode);
        else
            hrc = i_setInvalidMachineStateError();
        ptrVM.release();
    }

    /* notify console callbacks on success */
    if (SUCCEEDED(hrc))
    {
        alock.release();
        ::FireDnDModeChangedEvent(mEventSource, aDnDMode);
    }

    LogFlowThisFunc(("Leaving hrc=%#x\n", hrc));
    return hrc;
}

/**
 * Check the return code of mConsoleVRDPServer->Launch. LogRel() the error reason and
 * return an error message appropriate for setError().
 */
Utf8Str Console::VRDPServerErrorToMsg(int vrc)
{
    Utf8Str errMsg;
    if (vrc == VERR_NET_ADDRESS_IN_USE)
    {
        /* Not fatal if we start the VM, fatal if the VM is already running. */
        Bstr bstr;
        mVRDEServer->GetVRDEProperty(Bstr("TCP/Ports").raw(), bstr.asOutParam());
        errMsg = Utf8StrFmt(tr("VirtualBox Remote Desktop Extension server can't bind to the port(s): %s"),
                                Utf8Str(bstr).c_str());
        LogRel(("VRDE: Warning: failed to launch VRDE server (%Rrc): %s\n", vrc, errMsg.c_str()));
    }
    else if (vrc == VINF_NOT_SUPPORTED)
    {
        /* This means that the VRDE is not installed.
         * Not fatal if we start the VM, fatal if the VM is already running. */
        LogRel(("VRDE: VirtualBox Remote Desktop Extension is not available.\n"));
        errMsg = Utf8Str(tr("VirtualBox Remote Desktop Extension is not available"));
    }
    else if (RT_FAILURE(vrc))
    {
        /* Fail if the server is installed but can't start. Always fatal. */
        switch (vrc)
        {
            case VERR_FILE_NOT_FOUND:
                errMsg = Utf8StrFmt(tr("Could not find the VirtualBox Remote Desktop Extension library"));
                break;
            default:
                errMsg = Utf8StrFmt(tr("Failed to launch the Remote Desktop Extension server (%Rrc)"), vrc);
                break;
        }
        LogRel(("VRDE: Failed: (%Rrc): %s\n", vrc, errMsg.c_str()));
    }

    return errMsg;
}

/**
 * Called by IInternalSessionControl::OnVRDEServerChange().
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_onVRDEServerChange(BOOL aRestart)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    /* don't trigger VRDE server changes if the VM isn't running */
    SafeVMPtrQuiet ptrVM(this);
    if (ptrVM.isOk())
    {
        /* Serialize. */
        if (mfVRDEChangeInProcess)
            mfVRDEChangePending = true;
        else
        {
            do {
                mfVRDEChangeInProcess = true;
                mfVRDEChangePending = false;

                if (    mVRDEServer
                    &&  (   mMachineState == MachineState_Running
                         || mMachineState == MachineState_Teleporting
                         || mMachineState == MachineState_LiveSnapshotting
                         || mMachineState == MachineState_Paused
                         )
                   )
                {
                    BOOL vrdpEnabled = FALSE;

                    hrc = mVRDEServer->COMGETTER(Enabled)(&vrdpEnabled);
                    ComAssertComRCRetRC(hrc);

                    if (aRestart)
                    {
                        /* VRDP server may call this Console object back from other threads (VRDP INPUT or OUTPUT). */
                        alock.release();

                        if (vrdpEnabled)
                        {
                            // If there was no VRDP server started the 'stop' will do nothing.
                            // However if a server was started and this notification was called,
                            // we have to restart the server.
                            mConsoleVRDPServer->Stop();

                            int vrc = mConsoleVRDPServer->Launch();
                            if (vrc != VINF_SUCCESS)
                            {
                                Utf8Str errMsg = VRDPServerErrorToMsg(vrc);
                                hrc = setErrorBoth(E_FAIL, vrc, "%s", errMsg.c_str());
                            }
                            else
                            {
#ifdef VBOX_WITH_AUDIO_VRDE
                                mAudioVRDE->doAttachDriverViaEmt(ptrVM.rawUVM(), ptrVM.vtable(), NULL /*alock is not held*/);
#endif
                                mConsoleVRDPServer->EnableConnections();
                            }
                        }
                        else
                        {
                            mConsoleVRDPServer->Stop();
#ifdef VBOX_WITH_AUDIO_VRDE
                            mAudioVRDE->doDetachDriverViaEmt(ptrVM.rawUVM(), ptrVM.vtable(), NULL /*alock is not held*/);
#endif
                        }

                        alock.acquire();
                    }
                }
                else
                    hrc = i_setInvalidMachineStateError();

                mfVRDEChangeInProcess = false;
            } while (mfVRDEChangePending && SUCCEEDED(hrc));
        }

        ptrVM.release();
    }

    /* notify console callbacks on success */
    if (SUCCEEDED(hrc))
    {
        alock.release();
        ::FireVRDEServerChangedEvent(mEventSource);
    }

    return hrc;
}

void Console::i_onVRDEServerInfoChange()
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    ::FireVRDEServerInfoChangedEvent(mEventSource);
}

HRESULT Console::i_sendACPIMonitorHotPlugEvent()
{
    LogFlowThisFuncEnter();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (   mMachineState != MachineState_Running
        && mMachineState != MachineState_Teleporting
        && mMachineState != MachineState_LiveSnapshotting)
        return i_setInvalidMachineStateError();

    /* get the VM handle. */
    SafeVMPtr ptrVM(this);
    if (!ptrVM.isOk())
        return ptrVM.hrc();

    // no need to release lock, as there are no cross-thread callbacks

    /* get the acpi device interface and press the sleep button. */
    PPDMIBASE pBase;
    int vrc = ptrVM.vtable()->pfnPDMR3QueryDeviceLun(ptrVM.rawUVM(), "acpi", 0, 0, &pBase);
    if (RT_SUCCESS(vrc))
    {
        Assert(pBase);
        PPDMIACPIPORT pPort = PDMIBASE_QUERY_INTERFACE(pBase, PDMIACPIPORT);
        if (pPort)
            vrc = pPort->pfnMonitorHotPlugEvent(pPort);
        else
            vrc = VERR_PDM_MISSING_INTERFACE;
    }

    HRESULT hrc = RT_SUCCESS(vrc) ? S_OK
                : setErrorBoth(VBOX_E_PDM_ERROR, vrc, tr("Sending monitor hot-plug event failed (%Rrc)"), vrc);

    LogFlowThisFunc(("hrc=%Rhrc\n", hrc));
    LogFlowThisFuncLeave();
    return hrc;
}

#ifdef VBOX_WITH_RECORDING
/**
 * Enables or disables recording of a VM.
 *
 * @returns VBox status code.
 * @retval  VINF_NO_CHANGE if the recording state has not been changed.
 * @param   fEnable             Whether to enable or disable the recording.
 * @param   pAutoLock           Pointer to auto write lock to use for attaching/detaching required driver(s) at runtime.
 * @param   pProgress           Progress object to use. Optional and can be NULL.
 */
int Console::i_recordingEnable(BOOL fEnable, util::AutoWriteLock *pAutoLock, ComPtr<IProgress> &pProgress)
{
    AssertPtrReturn(pAutoLock, VERR_INVALID_POINTER);

    bool const fIsEnabled = mRecording.mCtx.IsStarted();

    if (RT_BOOL(fEnable) == fIsEnabled) /* No change? Bail out. */
        return VINF_NO_CHANGE;

    int vrc = VINF_SUCCESS;

    Display *pDisplay = i_getDisplay();
    AssertPtrReturn(pDisplay, VERR_INVALID_POINTER);

    LogRel(("Recording: %s\n", fEnable ? "Enabling" : "Disabling"));

    SafeVMPtrQuiet ptrVM(this);
    if (ptrVM.isOk())
    {
        if (fEnable)
        {
            vrc = i_recordingCreate(pProgress);
            if (RT_SUCCESS(vrc))
            {
# ifdef VBOX_WITH_AUDIO_RECORDING
                /* Attach the video recording audio driver if required. */
                if (   mRecording.mCtx.IsFeatureEnabled(RecordingFeature_Audio)
                    && mRecording.mAudioRec)
                {
                    vrc = mRecording.mAudioRec->applyConfiguration(mRecording.mCtx.GetConfig());
                    if (RT_SUCCESS(vrc))
                        vrc = mRecording.mAudioRec->doAttachDriverViaEmt(ptrVM.rawUVM(), ptrVM.vtable(), pAutoLock);

                    if (RT_FAILURE(vrc))
                        mRecording.mCtx.SetError(vrc, Utf8StrFmt(tr("Attaching audio recording driver failed (%Rrc)"), vrc));
                }
# endif
                if (   RT_SUCCESS(vrc)
                    && mRecording.mCtx.IsReady()) /* Any video recording (audio and/or video) feature enabled? */
                {
                    vrc = i_recordingStart(pAutoLock);
                }
            }

            if (RT_FAILURE(vrc))
                LogRel(("Recording: Failed to enable with %Rrc\n", vrc));
        }
        else /* Disable */
        {
            vrc = i_recordingStop(pAutoLock);
            if (RT_SUCCESS(vrc))
            {
# ifdef VBOX_WITH_AUDIO_RECORDING
                if (mRecording.mAudioRec)
                {
                    vrc = mRecording.mAudioRec->doDetachDriverViaEmt(ptrVM.rawUVM(), ptrVM.vtable(), pAutoLock);
                    if (RT_FAILURE(vrc))
                        mRecording.mCtx.SetError(vrc, Utf8StrFmt(tr("Detaching audio recording driver failed (%Rrc) -- please consult log file for details"), vrc));
                }
# endif
                i_recordingDestroy();
            }
        }
    }
    else
        vrc = VERR_VM_INVALID_VM_STATE;

    if (RT_FAILURE(vrc))
        LogRel(("Recording: %s failed with %Rrc\n", fEnable ? "Enabling" : "Disabling", vrc));

    return vrc;
}
#endif /* VBOX_WITH_RECORDING */

/**
 * Called by IInternalSessionControl::OnRecordingStateChange().
 */
HRESULT Console::i_onRecordingStateChange(BOOL aEnable, ComPtr<IProgress> &aProgress)
{
#ifdef VBOX_WITH_RECORDING
    HRESULT hrc = S_OK;

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogRel2(("Recording: State changed (%s)\n", aEnable ? "enabled" : "disabled"));

    /* Don't trigger recording changes if the VM isn't running. */
    SafeVMPtrQuiet ptrVM(this);
    if (ptrVM.isOk())
    {
        ComPtr<IVirtualBoxErrorInfo> pErrorInfo;

        int vrc = i_recordingEnable(aEnable, &alock, aProgress);
        if (RT_FAILURE(vrc))
        {
            /* If available, get the error information from the progress object and fire it via the event below. */
            if (aProgress.isNotNull())
            {
                hrc = aProgress->COMGETTER(ErrorInfo(pErrorInfo.asOutParam()));
                AssertComRCReturn(hrc, hrc);
            }
        }

        alock.release(); /* Release lock before firing event. */

        if (vrc != VINF_NO_CHANGE)
            ::FireRecordingStateChangedEvent(mEventSource, aEnable, pErrorInfo);

        if (RT_FAILURE(vrc))
            hrc = VBOX_E_RECORDING_ERROR;

        ptrVM.release();
    }

    return hrc;
#else
    RT_NOREF(aEnable, aProgress);
    ReturnComNotImplemented();
#endif /* VBOX_WITH_RECORDING */
}

/**
 * Called by IInternalSessionControl::OnRecordingScreenStateChange().
 */
HRESULT Console::i_onRecordingScreenStateChange(BOOL aEnable, ULONG aScreen)
{
    RT_NOREF(aEnable, aScreen);
    ReturnComNotImplemented();
}

/**
 * Called by IInternalSessionControl::OnUSBControllerChange().
 */
HRESULT Console::i_onUSBControllerChange()
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    ::FireUSBControllerChangedEvent(mEventSource);

    return S_OK;
}

/**
 * Called by IInternalSessionControl::OnSharedFolderChange().
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_onSharedFolderChange(BOOL aGlobal)
{
    LogFlowThisFunc(("aGlobal=%RTbool\n", aGlobal));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = i_fetchSharedFolders(aGlobal);

    /* notify console callbacks on success */
    if (SUCCEEDED(hrc))
    {
        alock.release();
        ::FireSharedFolderChangedEvent(mEventSource, aGlobal ? Scope_Global : Scope_Machine);
    }

    return hrc;
}

/**
 * Called by IInternalSessionControl::OnGuestDebugControlChange().
 */
HRESULT Console::i_onGuestDebugControlChange(IGuestDebugControl *aGuestDebugControl)
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    HRESULT hrc = S_OK;

    /* don't trigger changes if the VM isn't running */
    SafeVMPtrQuiet ptrVM(this);
    if (ptrVM.isOk())
    {
        /// @todo
    }

    if (SUCCEEDED(hrc))
        ::FireGuestDebugControlChangedEvent(mEventSource, aGuestDebugControl);

    LogFlowThisFunc(("Leaving hrc=%#x\n", hrc));
    return hrc;
}


/**
 * Called by IInternalSessionControl::OnUSBDeviceAttach() or locally by
 * processRemoteUSBDevices() after IInternalMachineControl::RunUSBDeviceFilters()
 * returns TRUE for a given remote USB device.
 *
 * @return S_OK if the device was attached to the VM.
 * @return failure if not attached.
 *
 * @param aDevice       The device in question.
 * @param aError        Error information.
 * @param aMaskedIfs    The interfaces to hide from the guest.
 * @param aCaptureFilename File name where to store the USB traffic.
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_onUSBDeviceAttach(IUSBDevice *aDevice, IVirtualBoxErrorInfo *aError, ULONG aMaskedIfs,
                                     const Utf8Str &aCaptureFilename)
{
#ifdef VBOX_WITH_USB
    LogFlowThisFunc(("aDevice=%p aError=%p\n", aDevice, aError));

    AutoCaller autoCaller(this);
    ComAssertComRCRetRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Get the VM pointer (we don't need error info, since it's a callback). */
    SafeVMPtrQuiet ptrVM(this);
    if (!ptrVM.isOk())
    {
        /* The VM may be no more operational when this message arrives
         * (e.g. it may be Saving or Stopping or just PoweredOff) --
         * autoVMCaller.hrc() will return a failure in this case. */
        LogFlowThisFunc(("Attach request ignored (mMachineState=%d).\n", mMachineState));
        return ptrVM.hrc();
    }

    if (aError != NULL)
    {
        /* notify callbacks about the error */
        alock.release();
        i_onUSBDeviceStateChange(aDevice, true /* aAttached */, aError);
        return S_OK;
    }

    /* Don't proceed unless there's at least one USB hub. */
    if (!ptrVM.vtable()->pfnPDMR3UsbHasHub(ptrVM.rawUVM()))
    {
        LogFlowThisFunc(("Attach request ignored (no USB controller).\n"));
        return E_FAIL;
    }

    alock.release();
    HRESULT hrc = i_attachUSBDevice(aDevice, aMaskedIfs, aCaptureFilename);
    if (FAILED(hrc))
    {
        /* take the current error info */
        com::ErrorInfoKeeper eik;
        /* the error must be a VirtualBoxErrorInfo instance */
        ComPtr<IVirtualBoxErrorInfo> pError = eik.takeError();
        Assert(!pError.isNull());
        if (!pError.isNull())
        {
            /* notify callbacks about the error */
            i_onUSBDeviceStateChange(aDevice, true /* aAttached */, pError);
        }
    }

    return hrc;

#else   /* !VBOX_WITH_USB */
    RT_NOREF(aDevice, aError, aMaskedIfs, aCaptureFilename);
    return E_FAIL;
#endif  /* !VBOX_WITH_USB */
}

/**
 * Called by IInternalSessionControl::OnUSBDeviceDetach() and locally by
 * processRemoteUSBDevices().
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_onUSBDeviceDetach(IN_BSTR aId,
                                     IVirtualBoxErrorInfo *aError)
{
#ifdef VBOX_WITH_USB
    Guid Uuid(aId);
    LogFlowThisFunc(("aId={%RTuuid} aError=%p\n", Uuid.raw(), aError));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Find the device. */
    ComObjPtr<OUSBDevice> pUSBDevice;
    USBDeviceList::iterator it = mUSBDevices.begin();
    while (it != mUSBDevices.end())
    {
        LogFlowThisFunc(("it={%RTuuid}\n", (*it)->i_id().raw()));
        if ((*it)->i_id() == Uuid)
        {
            pUSBDevice = *it;
            break;
        }
        ++it;
    }


    if (pUSBDevice.isNull())
    {
        LogFlowThisFunc(("USB device not found.\n"));

        /* The VM may be no more operational when this message arrives
         * (e.g. it may be Saving or Stopping or just PoweredOff). Use
         * AutoVMCaller to detect it -- AutoVMCaller::hrc() will return a
         * failure in this case. */

        AutoVMCallerQuiet autoVMCaller(this);
        if (FAILED(autoVMCaller.hrc()))
        {
            LogFlowThisFunc(("Detach request ignored (mMachineState=%d).\n", mMachineState));
            return autoVMCaller.hrc();
        }

        /* the device must be in the list otherwise */
        AssertFailedReturn(E_FAIL);
    }

    if (aError != NULL)
    {
        /* notify callback about an error */
        alock.release();
        i_onUSBDeviceStateChange(pUSBDevice, false /* aAttached */, aError);
        return S_OK;
    }

    /* Remove the device from the collection, it is re-added below for failures */
    mUSBDevices.erase(it);

    alock.release();
    HRESULT hrc = i_detachUSBDevice(pUSBDevice);
    if (FAILED(hrc))
    {
        /* Re-add the device to the collection */
        alock.acquire();
        mUSBDevices.push_back(pUSBDevice);
        alock.release();
        /* take the current error info */
        com::ErrorInfoKeeper eik;
        /* the error must be a VirtualBoxErrorInfo instance */
        ComPtr<IVirtualBoxErrorInfo> pError = eik.takeError();
        Assert(!pError.isNull());
        if (!pError.isNull())
        {
            /* notify callbacks about the error */
            i_onUSBDeviceStateChange(pUSBDevice, false /* aAttached */, pError);
        }
    }

    return hrc;

#else   /* !VBOX_WITH_USB */
    RT_NOREF(aId, aError);
    return E_FAIL;
#endif  /* !VBOX_WITH_USB */
}

/**
 * Called by IInternalSessionControl::OnBandwidthGroupChange().
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_onBandwidthGroupChange(IBandwidthGroup *aBandwidthGroup)
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    /* don't trigger bandwidth group changes if the VM isn't running */
    SafeVMPtrQuiet ptrVM(this);
    if (ptrVM.isOk())
    {
        if (   mMachineState == MachineState_Running
            || mMachineState == MachineState_Teleporting
            || mMachineState == MachineState_LiveSnapshotting
            )
        {
            /* No need to call in the EMT thread. */
            Bstr bstrName;
            hrc = aBandwidthGroup->COMGETTER(Name)(bstrName.asOutParam());
            if (SUCCEEDED(hrc))
            {
                Utf8Str const strName(bstrName);
                LONG64 cMax;
                hrc = aBandwidthGroup->COMGETTER(MaxBytesPerSec)(&cMax);
                if (SUCCEEDED(hrc))
                {
                    BandwidthGroupType_T enmType;
                    hrc = aBandwidthGroup->COMGETTER(Type)(&enmType);
                    if (SUCCEEDED(hrc))
                    {
                        int vrc = VINF_SUCCESS;
                        if (enmType == BandwidthGroupType_Disk)
                            vrc = ptrVM.vtable()->pfnPDMR3AsyncCompletionBwMgrSetMaxForFile(ptrVM.rawUVM(), strName.c_str(),
                                                                                            (uint64_t)cMax);
#ifdef VBOX_WITH_NETSHAPER
                        else if (enmType == BandwidthGroupType_Network)
                            vrc = ptrVM.vtable()->pfnPDMR3NsBwGroupSetLimit(ptrVM.rawUVM(), strName.c_str(), cMax);
                        else
                            hrc = E_NOTIMPL;
#endif
                        AssertRC(vrc);
                    }
                }
            }
        }
        else
            hrc = i_setInvalidMachineStateError();
        ptrVM.release();
    }

    /* notify console callbacks on success */
    if (SUCCEEDED(hrc))
    {
        alock.release();
        ::FireBandwidthGroupChangedEvent(mEventSource, aBandwidthGroup);
    }

    LogFlowThisFunc(("Leaving hrc=%#x\n", hrc));
    return hrc;
}

/**
 * Called by IInternalSessionControl::OnStorageDeviceChange().
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_onStorageDeviceChange(IMediumAttachment *aMediumAttachment, BOOL aRemove, BOOL aSilent)
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    HRESULT hrc = S_OK;

    /* don't trigger medium changes if the VM isn't running */
    SafeVMPtrQuiet ptrVM(this);
    if (ptrVM.isOk())
    {
        if (aRemove)
            hrc = i_doStorageDeviceDetach(aMediumAttachment, ptrVM.rawUVM(), ptrVM.vtable(), RT_BOOL(aSilent));
        else
            hrc = i_doStorageDeviceAttach(aMediumAttachment, ptrVM.rawUVM(), ptrVM.vtable(), RT_BOOL(aSilent));
        ptrVM.release();
    }

    /* notify console callbacks on success */
    if (SUCCEEDED(hrc))
        ::FireStorageDeviceChangedEvent(mEventSource, aMediumAttachment, aRemove, aSilent);

    LogFlowThisFunc(("Leaving hrc=%#x\n", hrc));
    return hrc;
}

HRESULT Console::i_onExtraDataChange(const Bstr &aMachineId, const Bstr &aKey, const Bstr &aVal)
{
    LogFlowThisFunc(("\n"));

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc()))
        return autoCaller.hrc();

    if (aMachineId != i_getId())
        return S_OK;

    /* don't do anything if the VM isn't running */
    if (aKey == "VBoxInternal2/TurnResetIntoPowerOff")
    {
        SafeVMPtrQuiet ptrVM(this);
        if (ptrVM.isOk())
        {
            mfTurnResetIntoPowerOff = aVal == "1";
            int vrc = ptrVM.vtable()->pfnVMR3SetPowerOffInsteadOfReset(ptrVM.rawUVM(), mfTurnResetIntoPowerOff);
            AssertRC(vrc);

            ptrVM.release();
        }
    }

    /* notify console callbacks on success */
    ::FireExtraDataChangedEvent(mEventSource, aMachineId.raw(), aKey.raw(), aVal.raw());

    LogFlowThisFunc(("Leaving S_OK\n"));
    return S_OK;
}

/**
 * @note Temporarily locks this object for writing.
 */
HRESULT Console::i_getGuestProperty(const Utf8Str &aName, Utf8Str *aValue, LONG64 *aTimestamp, Utf8Str *aFlags)
{
#ifndef VBOX_WITH_GUEST_PROPS
    ReturnComNotImplemented();
#else  /* VBOX_WITH_GUEST_PROPS */
    if (!RT_VALID_PTR(aValue))
         return E_POINTER;
#ifndef VBOX_WITH_PARFAIT /** @todo Fails due to RT_VALID_PTR() being only a != NULL check with parfait. */
    if (aTimestamp != NULL && !RT_VALID_PTR(aTimestamp))
        return E_POINTER;
    if (aFlags != NULL && !RT_VALID_PTR(aFlags))
        return E_POINTER;
#endif

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /* protect mpUVM (if not NULL) */
    SafeVMPtrQuiet ptrVM(this);
    if (FAILED(ptrVM.hrc()))
        return ptrVM.hrc();

    /* Note: validity of mVMMDev which is bound to uninit() is guaranteed by
     * ptrVM, so there is no need to hold a lock of this */

    HRESULT hrc = E_UNEXPECTED;
    try
    {
        VBOXHGCMSVCPARM parm[4];
        char szBuffer[GUEST_PROP_MAX_VALUE_LEN + GUEST_PROP_MAX_FLAGS_LEN];

        parm[0].type = VBOX_HGCM_SVC_PARM_PTR;
        parm[0].u.pointer.addr = (void *)aName.c_str();
        parm[0].u.pointer.size = (uint32_t)aName.length() + 1; /* The + 1 is the null terminator */

        parm[1].type = VBOX_HGCM_SVC_PARM_PTR;
        parm[1].u.pointer.addr = szBuffer;
        parm[1].u.pointer.size = sizeof(szBuffer);

        parm[2].type = VBOX_HGCM_SVC_PARM_64BIT;
        parm[2].u.uint64 = 0;

        parm[3].type = VBOX_HGCM_SVC_PARM_32BIT;
        parm[3].u.uint32 = 0;

        int vrc = m_pVMMDev->hgcmHostCall("VBoxGuestPropSvc", GUEST_PROP_FN_HOST_GET_PROP,
                                          4, &parm[0]);
        /* The returned string should never be able to be greater than our buffer */
        AssertLogRel(vrc != VERR_BUFFER_OVERFLOW);
        AssertLogRel(RT_FAILURE(vrc) || parm[2].type == VBOX_HGCM_SVC_PARM_64BIT);
        if (RT_SUCCESS(vrc))
        {
            *aValue = szBuffer;

            if (aTimestamp)
                *aTimestamp = parm[2].u.uint64;

            if (aFlags)
                *aFlags = &szBuffer[strlen(szBuffer) + 1];

            hrc = S_OK;
        }
        else if (vrc == VERR_NOT_FOUND)
        {
            *aValue = "";
            hrc = S_OK;
        }
        else
            hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("The VBoxGuestPropSvc service call failed with the error %Rrc"), vrc);
    }
    catch(std::bad_alloc & /*e*/)
    {
        hrc = E_OUTOFMEMORY;
    }

    return hrc;
#endif /* VBOX_WITH_GUEST_PROPS */
}

/**
 * @note Temporarily locks this object for writing.
 */
HRESULT Console::i_setGuestProperty(const Utf8Str &aName, const Utf8Str &aValue, const Utf8Str &aFlags)
{
#ifndef VBOX_WITH_GUEST_PROPS
    ReturnComNotImplemented();
#else /* VBOX_WITH_GUEST_PROPS */

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /* protect mpUVM (if not NULL) */
    SafeVMPtrQuiet ptrVM(this);
    if (FAILED(ptrVM.hrc()))
        return ptrVM.hrc();

    /* Note: validity of mVMMDev which is bound to uninit() is guaranteed by
     * ptrVM, so there is no need to hold a lock of this */

    VBOXHGCMSVCPARM parm[3];

    parm[0].type = VBOX_HGCM_SVC_PARM_PTR;
    parm[0].u.pointer.addr = (void*)aName.c_str();
    parm[0].u.pointer.size = (uint32_t)aName.length() + 1; /* The + 1 is the null terminator */

    parm[1].type = VBOX_HGCM_SVC_PARM_PTR;
    parm[1].u.pointer.addr = (void *)aValue.c_str();
    parm[1].u.pointer.size = (uint32_t)aValue.length() + 1; /* The + 1 is the null terminator */

    int vrc;
    if (aFlags.isEmpty())
    {
        vrc = m_pVMMDev->hgcmHostCall("VBoxGuestPropSvc", GUEST_PROP_FN_HOST_SET_PROP_VALUE, 2, &parm[0]);
    }
    else
    {
        parm[2].type = VBOX_HGCM_SVC_PARM_PTR;
        parm[2].u.pointer.addr = (void*)aFlags.c_str();
        parm[2].u.pointer.size = (uint32_t)aFlags.length() + 1; /* The + 1 is the null terminator */

        vrc = m_pVMMDev->hgcmHostCall("VBoxGuestPropSvc", GUEST_PROP_FN_HOST_SET_PROP, 3, &parm[0]);
    }

    HRESULT hrc = S_OK;
    if (RT_FAILURE(vrc))
        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("The VBoxGuestPropSvc service call failed with the error %Rrc"), vrc);
    return hrc;
#endif /* VBOX_WITH_GUEST_PROPS */
}

HRESULT Console::i_deleteGuestProperty(const Utf8Str &aName)
{
#ifndef VBOX_WITH_GUEST_PROPS
    ReturnComNotImplemented();
#else /* VBOX_WITH_GUEST_PROPS */

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /* protect mpUVM (if not NULL) */
    SafeVMPtrQuiet ptrVM(this);
    if (FAILED(ptrVM.hrc()))
        return ptrVM.hrc();

    /* Note: validity of mVMMDev which is bound to uninit() is guaranteed by
     * ptrVM, so there is no need to hold a lock of this */

    VBOXHGCMSVCPARM parm[1];
    parm[0].type = VBOX_HGCM_SVC_PARM_PTR;
    parm[0].u.pointer.addr = (void*)aName.c_str();
    parm[0].u.pointer.size = (uint32_t)aName.length() + 1; /* The + 1 is the null terminator */

    int vrc = m_pVMMDev->hgcmHostCall("VBoxGuestPropSvc", GUEST_PROP_FN_HOST_DEL_PROP, 1, &parm[0]);

    HRESULT hrc = S_OK;
    if (RT_FAILURE(vrc))
        hrc = setErrorBoth(VBOX_E_IPRT_ERROR, vrc, tr("The VBoxGuestPropSvc service call failed with the error %Rrc"), vrc);
    return hrc;
#endif /* VBOX_WITH_GUEST_PROPS */
}

/**
 * @note Temporarily locks this object for writing.
 */
HRESULT Console::i_enumerateGuestProperties(const Utf8Str &aPatterns,
                                            std::vector<Utf8Str> &aNames,
                                            std::vector<Utf8Str> &aValues,
                                            std::vector<LONG64>  &aTimestamps,
                                            std::vector<Utf8Str> &aFlags)
{
#ifndef VBOX_WITH_GUEST_PROPS
    ReturnComNotImplemented();
#else /* VBOX_WITH_GUEST_PROPS */

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /* protect mpUVM (if not NULL) */
    AutoVMCallerWeak autoVMCaller(this);
    if (FAILED(autoVMCaller.hrc()))
        return autoVMCaller.hrc();

    /* Note: validity of mVMMDev which is bound to uninit() is guaranteed by
     * autoVMCaller, so there is no need to hold a lock of this */

    return i_doEnumerateGuestProperties(aPatterns, aNames, aValues, aTimestamps, aFlags);
#endif /* VBOX_WITH_GUEST_PROPS */
}


/*
 * Internal: helper function for connecting progress reporting
 */
static DECLCALLBACK(int) onlineMergeMediumProgress(void *pvUser, unsigned uPercentage)
{
    HRESULT hrc = S_OK;
    IProgress *pProgress = static_cast<IProgress *>(pvUser);
    if (pProgress)
    {
        ComPtr<IInternalProgressControl> pProgressControl(pProgress);
        AssertReturn(!!pProgressControl, VERR_INVALID_PARAMETER);
        hrc = pProgressControl->SetCurrentOperationProgress(uPercentage);
    }
    return SUCCEEDED(hrc) ? VINF_SUCCESS : VERR_GENERAL_FAILURE;
}

/**
 * @note Temporarily locks this object for writing. bird: And/or reading?
 */
HRESULT Console::i_onlineMergeMedium(IMediumAttachment *aMediumAttachment,
                                     ULONG aSourceIdx, ULONG aTargetIdx,
                                     IProgress *aProgress)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    HRESULT hrc = S_OK;
    int vrc = VINF_SUCCESS;

    /* Get the VM - must be done before the read-locking. */
    SafeVMPtr ptrVM(this);
    if (!ptrVM.isOk())
        return ptrVM.hrc();

    /* We will need to release the lock before doing the actual merge */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* paranoia - we don't want merges to happen while teleporting etc. */
    switch (mMachineState)
    {
        case MachineState_DeletingSnapshotOnline:
        case MachineState_DeletingSnapshotPaused:
            break;

        default:
            return i_setInvalidMachineStateError();
    }

    /** @todo AssertComRC -> AssertComRCReturn! Could potentially end up
     *        using uninitialized variables here. */
    BOOL fBuiltinIOCache = FALSE;
    hrc = mMachine->COMGETTER(IOCacheEnabled)(&fBuiltinIOCache);
    AssertComRC(hrc);
    SafeIfaceArray<IStorageController> ctrls;
    hrc = mMachine->COMGETTER(StorageControllers)(ComSafeArrayAsOutParam(ctrls));
    AssertComRC(hrc);
    LONG lDev = -1;
    hrc = aMediumAttachment->COMGETTER(Device)(&lDev);
    AssertComRC(hrc);
    LONG lPort = -1;
    hrc = aMediumAttachment->COMGETTER(Port)(&lPort);
    AssertComRC(hrc);
    IMedium *pMedium = NULL;
    hrc = aMediumAttachment->COMGETTER(Medium)(&pMedium);
    AssertComRC(hrc);
    Bstr mediumLocation;
    if (pMedium)
    {
        hrc = pMedium->COMGETTER(Location)(mediumLocation.asOutParam());
        AssertComRC(hrc);
    }

    Bstr attCtrlName;
    hrc = aMediumAttachment->COMGETTER(Controller)(attCtrlName.asOutParam());
    AssertComRC(hrc);
    ComPtr<IStorageController> pStorageController;
    for (size_t i = 0; i < ctrls.size(); ++i)
    {
        Bstr ctrlName;
        hrc = ctrls[i]->COMGETTER(Name)(ctrlName.asOutParam());
        AssertComRC(hrc);
        if (attCtrlName == ctrlName)
        {
            pStorageController = ctrls[i];
            break;
        }
    }
    if (pStorageController.isNull())
        return setError(E_FAIL,
                        tr("Could not find storage controller '%ls'"),
                        attCtrlName.raw());

    StorageControllerType_T enmCtrlType;
    hrc = pStorageController->COMGETTER(ControllerType)(&enmCtrlType);
    AssertComRC(hrc);
    const char *pcszDevice = i_storageControllerTypeToStr(enmCtrlType);

    StorageBus_T enmBus;
    hrc = pStorageController->COMGETTER(Bus)(&enmBus);
    AssertComRC(hrc);

    ULONG uInstance = 0;
    hrc = pStorageController->COMGETTER(Instance)(&uInstance);
    AssertComRC(hrc);

    BOOL fUseHostIOCache = TRUE;
    hrc = pStorageController->COMGETTER(UseHostIOCache)(&fUseHostIOCache);
    AssertComRC(hrc);

    unsigned uLUN;
    hrc = Console::i_storageBusPortDeviceToLun(enmBus, lPort, lDev, uLUN);
    AssertComRCReturnRC(hrc);

    Assert(mMachineState == MachineState_DeletingSnapshotOnline);

    /* Pause the VM, as it might have pending IO on this drive */
    bool fResume = false;
    hrc = i_suspendBeforeConfigChange(ptrVM.rawUVM(), ptrVM.vtable(), &alock, &fResume);
    if (FAILED(hrc))
        return hrc;

    bool fInsertDiskIntegrityDrv = false;
    Bstr strDiskIntegrityFlag;
    hrc = mMachine->GetExtraData(Bstr("VBoxInternal2/EnableDiskIntegrityDriver").raw(), strDiskIntegrityFlag.asOutParam());
    if (   hrc == S_OK
        && strDiskIntegrityFlag == "1")
        fInsertDiskIntegrityDrv = true;

    alock.release();

    Console::ReconfigureMediumAttachmentArgs Args1;
    Args1.pcszDevice                = pcszDevice;
    Args1.uInstance                 = uInstance;
    Args1.enmBus                    = enmBus;
    Args1.fUseHostIOCache           = fUseHostIOCache;
    Args1.fBuiltinIOCache           = fBuiltinIOCache;
    Args1.fInsertDiskIntegrityDrv   = fInsertDiskIntegrityDrv;
    Args1.fSetupMerge               = true;
    Args1.uMergeSource              = aSourceIdx;
    Args1.uMergeTarget              = aTargetIdx;
    Args1.aMediumAtt                = aMediumAttachment;
    Args1.aMachineState             = mMachineState;
    vrc = ptrVM.vtable()->pfnVMR3ReqCallWaitU(ptrVM.rawUVM(), VMCPUID_ANY, (PFNRT)i_reconfigureMediumAttachment, 5,
                                              this, ptrVM.rawUVM(), ptrVM.vtable(), &Args1, &hrc);
    /* error handling is after resuming the VM */

    if (fResume)
        i_resumeAfterConfigChange(ptrVM.rawUVM(), ptrVM.vtable());

    if (RT_FAILURE(vrc))
        return setErrorBoth(E_FAIL, vrc, "%Rrc", vrc);
    if (FAILED(hrc))
        return hrc;

    PPDMIBASE pIBase = NULL;
    PPDMIMEDIA pIMedium = NULL;
    vrc = ptrVM.vtable()->pfnPDMR3QueryDriverOnLun(ptrVM.rawUVM(), pcszDevice, uInstance, uLUN, "VD", &pIBase);
    if (RT_SUCCESS(vrc))
    {
        if (pIBase)
        {
            pIMedium = (PPDMIMEDIA)pIBase->pfnQueryInterface(pIBase, PDMIMEDIA_IID);
            if (!pIMedium)
                return setError(E_FAIL, tr("could not query medium interface of controller"));
        }
        else
            return setError(E_FAIL, tr("could not query base interface of controller"));
    }

    /* Finally trigger the merge. */
    vrc = pIMedium->pfnMerge(pIMedium, onlineMergeMediumProgress, aProgress);
    if (RT_FAILURE(vrc))
        return setErrorBoth(E_FAIL, vrc, tr("Failed to perform an online medium merge (%Rrc)"), vrc);

    alock.acquire();
    /* Pause the VM, as it might have pending IO on this drive */
    hrc = i_suspendBeforeConfigChange(ptrVM.rawUVM(), ptrVM.vtable(), &alock, &fResume);
    if (FAILED(hrc))
        return hrc;
    alock.release();

    /* Update medium chain and state now, so that the VM can continue. */
    hrc = mControl->FinishOnlineMergeMedium();

    Console::ReconfigureMediumAttachmentArgs Args2;
    Args2.pcszDevice                = pcszDevice;
    Args2.uInstance                 = uInstance;
    Args2.enmBus                    = enmBus;
    Args2.fUseHostIOCache           = fUseHostIOCache;
    Args2.fBuiltinIOCache           = fBuiltinIOCache;
    Args2.fInsertDiskIntegrityDrv   = fInsertDiskIntegrityDrv;
    Args2.fSetupMerge               = false;
    Args2.uMergeSource              = 0;
    Args2.uMergeTarget              = 0;
    Args2.aMediumAtt                = aMediumAttachment;
    Args2.aMachineState             = mMachineState;
    vrc = ptrVM.vtable()->pfnVMR3ReqCallWaitU(ptrVM.rawUVM(), VMCPUID_ANY, (PFNRT)i_reconfigureMediumAttachment, 5,
                                              this, ptrVM.rawUVM(), ptrVM.vtable(), &Args2, &hrc);
    /* error handling is after resuming the VM */

    if (fResume)
        i_resumeAfterConfigChange(ptrVM.rawUVM(), ptrVM.vtable());

    if (RT_FAILURE(vrc))
        return setErrorBoth(E_FAIL, vrc, "%Rrc", vrc);
    if (FAILED(hrc))
        return hrc;

    return hrc;
}

HRESULT Console::i_reconfigureMediumAttachments(const std::vector<ComPtr<IMediumAttachment> > &aAttachments)
{

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc()))
        return autoCaller.hrc();

    /* get the VM handle. */
    SafeVMPtr ptrVM(this);
    if (!ptrVM.isOk())
        return ptrVM.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    for (size_t i = 0; i < aAttachments.size(); ++i)
    {
        /*
         * We could pass the objects, but then EMT would have to do lots of
         * IPC (to VBoxSVC) which takes a significant amount of time.
         * Better query needed values here and pass them.
         */
        Bstr controllerName;
        HRESULT hrc = aAttachments[i]->COMGETTER(Controller)(controllerName.asOutParam());
        if (FAILED(hrc))
            return hrc;

        ComPtr<IStorageController> pStorageController;
        hrc = mMachine->GetStorageControllerByName(controllerName.raw(), pStorageController.asOutParam());
        if (FAILED(hrc))
            return hrc;

        StorageControllerType_T enmController;
        hrc = pStorageController->COMGETTER(ControllerType)(&enmController);
        if (FAILED(hrc))
            return hrc;
        const char * const pcszDevice = i_storageControllerTypeToStr(enmController);

        ULONG lInstance;
        hrc = pStorageController->COMGETTER(Instance)(&lInstance);
        if (FAILED(hrc))
            return hrc;

        StorageBus_T enmBus;
        hrc = pStorageController->COMGETTER(Bus)(&enmBus);
        if (FAILED(hrc))
            return hrc;

        BOOL fUseHostIOCache;
        hrc = pStorageController->COMGETTER(UseHostIOCache)(&fUseHostIOCache);
        if (FAILED(hrc))
            return hrc;

        BOOL fBuiltinIOCache;
        hrc = mMachine->COMGETTER(IOCacheEnabled)(&fBuiltinIOCache);
        if (FAILED(hrc))
            return hrc;

        bool fInsertDiskIntegrityDrv = false;
        Bstr strDiskIntegrityFlag;
        hrc = mMachine->GetExtraData(Bstr("VBoxInternal2/EnableDiskIntegrityDriver").raw(),
                                     strDiskIntegrityFlag.asOutParam());
        if (   hrc   == S_OK
            && strDiskIntegrityFlag == "1")
            fInsertDiskIntegrityDrv = true;

        alock.release();

        hrc = S_OK;
        Console::ReconfigureMediumAttachmentArgs Args;
        Args.pcszDevice                 = pcszDevice;
        Args.uInstance                  = lInstance;
        Args.enmBus                     = enmBus;
        Args.fUseHostIOCache            = fUseHostIOCache;
        Args.fBuiltinIOCache            = fBuiltinIOCache;
        Args.fInsertDiskIntegrityDrv    = fInsertDiskIntegrityDrv;
        Args.fSetupMerge                = false;
        Args.uMergeSource               = 0;
        Args.uMergeTarget               = 0;
        Args.aMediumAtt                 = aAttachments[i];
        Args.aMachineState              = mMachineState;
        int vrc = ptrVM.vtable()->pfnVMR3ReqCallWaitU(ptrVM.rawUVM(), VMCPUID_ANY, (PFNRT)i_reconfigureMediumAttachment, 5,
                                                      this, ptrVM.rawUVM(), ptrVM.vtable(), &Args, &hrc);
        if (RT_FAILURE(vrc))
            throw setErrorBoth(E_FAIL, vrc, "%Rrc", vrc);
        if (FAILED(hrc))
            throw hrc;

        alock.acquire();
    }

    return S_OK;
}

HRESULT Console::i_onVMProcessPriorityChange(VMProcPriority_T priority)
{
    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (FAILED(hrc))
        return hrc;

    RTPROCPRIORITY enmProcPriority = RTPROCPRIORITY_DEFAULT;
    switch (priority)
    {
        case VMProcPriority_Default:
            enmProcPriority = RTPROCPRIORITY_DEFAULT;
            break;
        case VMProcPriority_Flat:
            enmProcPriority = RTPROCPRIORITY_FLAT;
            break;
        case VMProcPriority_Low:
            enmProcPriority = RTPROCPRIORITY_LOW;
            break;
        case VMProcPriority_Normal:
            enmProcPriority = RTPROCPRIORITY_NORMAL;
            break;
        case VMProcPriority_High:
            enmProcPriority = RTPROCPRIORITY_HIGH;
            break;
        default:
            return setError(E_INVALIDARG, tr("Unsupported priority type (%d)"), priority);
    }
    int vrc = RTProcSetPriority(enmProcPriority);
    if (RT_FAILURE(vrc))
        hrc = setErrorBoth(VBOX_E_VM_ERROR, vrc,
                           tr("Could not set the priority of the process (%Rrc). Try to set it when VM is not started."), vrc);

    return hrc;
}

/**
 * Load an HGCM service.
 *
 * Main purpose of this method is to allow extension packs to load HGCM
 * service modules, which they can't, because the HGCM functionality lives
 * in module VBoxC (and ConsoleImpl.cpp is part of it and thus can call it).
 * Extension modules must not link directly against VBoxC, (XP)COM is
 * handling this.
 */
int Console::i_hgcmLoadService(const char *pszServiceLibrary, const char *pszServiceName)
{
    /* Everyone seems to delegate all HGCM calls to VMMDev, so stick to this
     * convention. Adds one level of indirection for no obvious reason. */
    AssertPtrReturn(m_pVMMDev, VERR_INVALID_STATE);
    return m_pVMMDev->hgcmLoadService(pszServiceLibrary, pszServiceName);
}

/**
 * Merely passes the call to Guest::enableVMMStatistics().
 */
void Console::i_enableVMMStatistics(BOOL aEnable)
{
    if (mGuest)
        mGuest->i_enableVMMStatistics(aEnable);
}

/**
 * Worker for Console::Pause and internal entry point for pausing a VM for
 * a specific reason.
 */
HRESULT Console::i_pause(Reason_T aReason)
{
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    switch (mMachineState)
    {
        case MachineState_Running:
        case MachineState_Teleporting:
        case MachineState_LiveSnapshotting:
            break;

        case MachineState_Paused:
        case MachineState_TeleportingPausedVM:
        case MachineState_OnlineSnapshotting:
            /* Remove any keys which are supposed to be removed on a suspend. */
            if (   aReason == Reason_HostSuspend
                || aReason == Reason_HostBatteryLow)
            {
                i_removeSecretKeysOnSuspend();
                return S_OK;
            }
            return setError(VBOX_E_INVALID_VM_STATE, tr("Already paused"));

        default:
            return i_setInvalidMachineStateError();
    }

    /* get the VM handle. */
    SafeVMPtr ptrVM(this);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        /* release the lock before a VMR3* call (EMT might wait for it, @bugref{7648})! */
        alock.release();

        LogFlowThisFunc(("Sending PAUSE request...\n"));
        if (aReason != Reason_Unspecified)
            LogRel(("Pausing VM execution, reason '%s'\n", ::stringifyReason(aReason)));

        /** @todo r=klaus make use of aReason */
        VMSUSPENDREASON enmReason = VMSUSPENDREASON_USER;
        if (aReason == Reason_HostSuspend)
            enmReason = VMSUSPENDREASON_HOST_SUSPEND;
        else if (aReason == Reason_HostBatteryLow)
            enmReason = VMSUSPENDREASON_HOST_BATTERY_LOW;

        int vrc = ptrVM.vtable()->pfnVMR3Suspend(ptrVM.rawUVM(), enmReason);

        if (RT_FAILURE(vrc))
            hrc = setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("Could not suspend the machine execution (%Rrc)"), vrc);
        else if (   aReason == Reason_HostSuspend
                 || aReason == Reason_HostBatteryLow)
        {
            alock.acquire();
            i_removeSecretKeysOnSuspend();
        }
    }

    LogFlowThisFunc(("hrc=%Rhrc\n", hrc));
    LogFlowThisFuncLeave();
    return hrc;
}

/**
 * Worker for Console::Resume and internal entry point for resuming a VM for
 * a specific reason.
 */
HRESULT Console::i_resume(Reason_T aReason, AutoWriteLock &alock)
{
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    /* get the VM handle. */
    SafeVMPtr ptrVM(this);
    if (!ptrVM.isOk())
        return ptrVM.hrc();

    /* release the lock before a VMR3* call (EMT might wait for it, @bugref{7648})! */
    alock.release();

    LogFlowThisFunc(("Sending RESUME request...\n"));
    if (aReason != Reason_Unspecified)
        LogRel(("Resuming VM execution, reason '%s'\n", ::stringifyReason(aReason)));

    int vrc;
    VMSTATE const enmVMState = mpVMM->pfnVMR3GetStateU(ptrVM.rawUVM());
    if (enmVMState == VMSTATE_CREATED)
    {
#ifdef VBOX_WITH_EXTPACK
        vrc = mptrExtPackManager->i_callAllVmPowerOnHooks(this, ptrVM.vtable()->pfnVMR3GetVM(ptrVM.rawUVM()), ptrVM.vtable());
#else
        vrc = VINF_SUCCESS;
#endif
        if (RT_SUCCESS(vrc))
            vrc = ptrVM.vtable()->pfnVMR3PowerOn(ptrVM.rawUVM()); /* (PowerUpPaused) */
    }
    else
    {
        VMRESUMEREASON enmReason;
        if (aReason == Reason_HostResume)
        {
            /*
             * Host resume may be called multiple times successively. We don't want to VMR3Resume->vmR3Resume->vmR3TrySetState()
             * to assert on us, hence check for the VM state here and bail if it's not in the 'suspended' state.
             * See @bugref{3495}.
             *
             * Also, don't resume the VM through a host-resume unless it was suspended due to a host-suspend.
             */
            if (enmVMState != VMSTATE_SUSPENDED)
            {
                LogRel(("Ignoring VM resume request, VM is currently not suspended (%d)\n", enmVMState));
                return S_OK;
            }
            VMSUSPENDREASON const enmSuspendReason = ptrVM.vtable()->pfnVMR3GetSuspendReason(ptrVM.rawUVM());
            if (enmSuspendReason != VMSUSPENDREASON_HOST_SUSPEND)
            {
                LogRel(("Ignoring VM resume request, VM was not suspended due to host-suspend (%d)\n", enmSuspendReason));
                return S_OK;
            }

            enmReason = VMRESUMEREASON_HOST_RESUME;
        }
        else
        {
            /*
             * Any other reason to resume the VM throws an error when the VM was suspended due to a host suspend.
             * See @bugref{7836}.
             */
            if (   enmVMState == VMSTATE_SUSPENDED
                && ptrVM.vtable()->pfnVMR3GetSuspendReason(ptrVM.rawUVM()) == VMSUSPENDREASON_HOST_SUSPEND)
                return setError(VBOX_E_INVALID_VM_STATE, tr("VM is paused due to host power management"));

            enmReason = aReason == Reason_Snapshot ? VMRESUMEREASON_STATE_SAVED : VMRESUMEREASON_USER;
        }

        // for snapshots: no state change callback, VBoxSVC does everything
        if (aReason == Reason_Snapshot)
            mVMStateChangeCallbackDisabled = true;

        vrc = ptrVM.vtable()->pfnVMR3Resume(ptrVM.rawUVM(), enmReason);

        if (aReason == Reason_Snapshot)
            mVMStateChangeCallbackDisabled = false;
    }

    HRESULT hrc = RT_SUCCESS(vrc) ? S_OK
                : setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("Could not resume the machine execution (%Rrc)"), vrc);

    LogFlowThisFunc(("hrc=%Rhrc\n", hrc));
    LogFlowThisFuncLeave();
    return hrc;
}

/**
 * Internal entry point for saving state of a VM for a specific reason. This
 * method is completely synchronous.
 *
 * The machine state is already set appropriately. It is only changed when
 * saving state actually paused the VM (happens with live snapshots and
 * teleportation), and in this case reflects the now paused variant.
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_saveState(Reason_T aReason, const ComPtr<IProgress> &aProgress, const ComPtr<ISnapshot> &aSnapshot,
                             const Utf8Str &aStateFilePath, bool aPauseVM, bool &aLeftPaused)
{
    LogFlowThisFuncEnter();
    aLeftPaused = false;

    AssertReturn(!aProgress.isNull(), E_INVALIDARG);
    AssertReturn(!aStateFilePath.isEmpty(), E_INVALIDARG);
    Assert(aSnapshot.isNull() || aReason == Reason_Snapshot);

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    LogFlowThisFunc(("mMachineState=%d\n", mMachineState));
    if (   mMachineState != MachineState_Saving
        && mMachineState != MachineState_LiveSnapshotting
        && mMachineState != MachineState_OnlineSnapshotting
        && mMachineState != MachineState_Teleporting
        && mMachineState != MachineState_TeleportingPausedVM)
        return setError(VBOX_E_INVALID_VM_STATE,
                        tr("Cannot save the execution state as the machine is not running or paused (machine state: %s)"),
                        Global::stringifyMachineState(mMachineState));
    bool fContinueAfterwards = mMachineState != MachineState_Saving;

    Bstr strDisableSaveState;
    mMachine->GetExtraData(Bstr("VBoxInternal2/DisableSaveState").raw(), strDisableSaveState.asOutParam());
    if (strDisableSaveState == "1")
        return setError(VBOX_E_VM_ERROR,
                        tr("Saving the execution state is disabled for this VM"));

    if (aReason != Reason_Unspecified)
        LogRel(("Saving state of VM, reason '%s'\n", ::stringifyReason(aReason)));

    /* ensure the directory for the saved state file exists */
    {
        Utf8Str dir = aStateFilePath;
        dir.stripFilename();
        if (!RTDirExists(dir.c_str()))
        {
            int vrc = RTDirCreateFullPath(dir.c_str(), 0700);
            if (RT_FAILURE(vrc))
                return setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Could not create a directory '%s' to save the state to (%Rrc)"),
                                    dir.c_str(), vrc);
        }
    }

    /* Get the VM handle early, we need it in several places. */
    SafeVMPtr ptrVM(this);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
    {
        bool fPaused = false;
        if (aPauseVM)
        {
            /* release the lock before a VMR3* call (EMT might wait for it, @bugref{7648})! */
            alock.release();
            VMSUSPENDREASON enmReason = VMSUSPENDREASON_USER;
            if (aReason == Reason_HostSuspend)
                enmReason = VMSUSPENDREASON_HOST_SUSPEND;
            else if (aReason == Reason_HostBatteryLow)
                enmReason = VMSUSPENDREASON_HOST_BATTERY_LOW;
            int vrc = ptrVM.vtable()->pfnVMR3Suspend(ptrVM.rawUVM(), enmReason);
            alock.acquire();

            if (RT_SUCCESS(vrc))
                fPaused = true;
            else
                hrc = setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("Could not suspend the machine execution (%Rrc)"), vrc);
        }

        Bstr bstrStateKeyId;
        Bstr bstrStateKeyStore;
#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
        if (SUCCEEDED(hrc))
        {
            hrc = mMachine->COMGETTER(StateKeyId)(bstrStateKeyId.asOutParam());
            if (SUCCEEDED(hrc))
            {
                hrc = mMachine->COMGETTER(StateKeyStore)(bstrStateKeyStore.asOutParam());
                if (FAILED(hrc))
                    hrc = setError(hrc, tr("Could not get key store for state file(%Rhrc (0x%08X))"), hrc, hrc);
            }
            else
                hrc = setError(hrc, tr("Could not get key id for state file(%Rhrc (0x%08X))"), hrc, hrc);
        }
#endif

        if (SUCCEEDED(hrc))
        {
            LogFlowFunc(("Saving the state to '%s'...\n", aStateFilePath.c_str()));

            mpVmm2UserMethods->pISnapshot = aSnapshot;
            mptrCancelableProgress = aProgress;

            SsmStream ssmStream(this, ptrVM.vtable(), m_pKeyStore, bstrStateKeyId, bstrStateKeyStore);
            int vrc = ssmStream.create(aStateFilePath.c_str());
            if (RT_SUCCESS(vrc))
            {
                PCSSMSTRMOPS pStreamOps = NULL;
                void *pvStreamOpsUser = NULL;
                vrc = ssmStream.querySsmStrmOps(&pStreamOps, &pvStreamOpsUser);
                if (RT_SUCCESS(vrc))
                {
                    alock.release();

                    vrc = ptrVM.vtable()->pfnVMR3Save(ptrVM.rawUVM(),
                                                      NULL /*pszFilename*/,
                                                      pStreamOps,
                                                      pvStreamOpsUser,
                                                      fContinueAfterwards,
                                                      Console::i_stateProgressCallback,
                                                      static_cast<IProgress *>(aProgress),
                                                      &aLeftPaused);

                    alock.acquire();
                }

                ssmStream.close();
                if (RT_FAILURE(vrc))
                {
                    int vrc2 = RTFileDelete(aStateFilePath.c_str());
                    AssertRC(vrc2);
                }
            }

            mpVmm2UserMethods->pISnapshot = NULL;
            mptrCancelableProgress.setNull();
            if (RT_SUCCESS(vrc))
            {
                Assert(fContinueAfterwards || !aLeftPaused);

                if (!fContinueAfterwards)
                {
                    /*
                     * The machine has been successfully saved, so power it down
                     * (vmstateChangeCallback() will set state to Saved on success).
                     * Note: we release the VM caller, otherwise it will deadlock.
                     */
                    ptrVM.release();
                    alock.release();
                    autoCaller.release();

                    HRESULT hrc2 = i_powerDown();
                    AssertComRC(hrc2);

                    autoCaller.add();
                    alock.acquire();
                }
                else if (fPaused)
                    aLeftPaused = true;
            }
            else
            {
                if (fPaused)
                {
                    alock.release();
                    ptrVM.vtable()->pfnVMR3Resume(ptrVM.rawUVM(), VMRESUMEREASON_STATE_RESTORED);
                    alock.acquire();
                }
                hrc = setErrorBoth(E_FAIL, vrc, tr("Failed to save the machine state to '%s' (%Rrc)"),
                                   aStateFilePath.c_str(), vrc);
            }
        }
    }

    LogFlowFuncLeave();
    return S_OK;
}

/**
 * Internal entry point for cancelling a VM save state.
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_cancelSaveState()
{
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(this);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Get the VM handle. */
    SafeVMPtr ptrVM(this);
    HRESULT hrc = ptrVM.hrc();
    if (SUCCEEDED(hrc))
        ptrVM.vtable()->pfnSSMR3Cancel(ptrVM.rawUVM());

    LogFlowFuncLeave();
    return hrc;
}

#ifdef VBOX_WITH_AUDIO_RECORDING
/**
 * Sends audio (frame) data to the recording routines.
 *
 * @returns HRESULT
 * @param   pvData              Audio data to send.
 * @param   cbData              Size (in bytes) of audio data to send.
 * @param   uTimestampMs        Timestamp (in ms) of audio data.
 */
HRESULT Console::i_recordingSendAudio(const void *pvData, size_t cbData, uint64_t uTimestampMs)
{
    if (   mRecording.mCtx.IsStarted()
        && mRecording.mCtx.IsFeatureEnabled(RecordingFeature_Audio))
    {
        int vrc = mRecording.mCtx.SendAudioFrame(pvData, cbData, uTimestampMs);
        if (RT_FAILURE(vrc))
            return E_FAIL;
    }

    return S_OK;
}
#endif /* VBOX_WITH_AUDIO_RECORDING */

#ifdef VBOX_WITH_RECORDING

int Console::i_recordingGetSettings(settings::Recording &Settings)
{
    Assert(mMachine.isNotNull());

    Settings.applyDefaults();

    ComPtr<IRecordingSettings> pRecordSettings;
    HRESULT hrc = mMachine->COMGETTER(RecordingSettings)(pRecordSettings.asOutParam());
    AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);

    BOOL fTemp;
    hrc = pRecordSettings->COMGETTER(Enabled)(&fTemp);
    AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
    Settings.common.fEnabled = RT_BOOL(fTemp);

    SafeIfaceArray<IRecordingScreenSettings> paRecScreens;
    hrc = pRecordSettings->COMGETTER(Screens)(ComSafeArrayAsOutParam(paRecScreens));
    AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);

    for (unsigned long i = 0; i < (unsigned long)paRecScreens.size(); ++i)
    {
        settings::RecordingScreen recScreenSettings;
        ComPtr<IRecordingScreenSettings> pRecScreenSettings = paRecScreens[i];

        hrc = pRecScreenSettings->COMGETTER(Enabled)(&fTemp);
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        recScreenSettings.fEnabled = RT_BOOL(fTemp);
        com::SafeArray<RecordingFeature_T> vecFeatures;
        hrc = pRecScreenSettings->COMGETTER(Features)(ComSafeArrayAsOutParam(vecFeatures));
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        /* Make sure to clear map first, as we want to (re-)set enabled features. */
        recScreenSettings.featureMap.clear();
        for (size_t f = 0; f < vecFeatures.size(); ++f)
        {
            if (vecFeatures[f] == RecordingFeature_Audio)
                recScreenSettings.featureMap[RecordingFeature_Audio] = true;
            else if (vecFeatures[f] == RecordingFeature_Video)
                recScreenSettings.featureMap[RecordingFeature_Video] = true;
        }
        hrc = pRecScreenSettings->COMGETTER(MaxTime)((ULONG *)&recScreenSettings.ulMaxTimeS);
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        hrc = pRecScreenSettings->COMGETTER(MaxFileSize)((ULONG *)&recScreenSettings.File.ulMaxSizeMB);
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        Bstr bstrTemp;
        hrc = pRecScreenSettings->COMGETTER(Filename)(bstrTemp.asOutParam());
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        recScreenSettings.File.strName = bstrTemp;
        hrc = pRecScreenSettings->COMGETTER(Options)(bstrTemp.asOutParam());
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        recScreenSettings.strOptions = bstrTemp;
        hrc = pRecScreenSettings->COMGETTER(AudioCodec)(&recScreenSettings.Audio.enmCodec);
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        hrc = pRecScreenSettings->COMGETTER(AudioDeadline)(&recScreenSettings.Audio.enmDeadline);
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        hrc = pRecScreenSettings->COMGETTER(AudioRateControlMode)(&recScreenSettings.Audio.enmRateCtlMode);
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        hrc = pRecScreenSettings->COMGETTER(AudioHz)((ULONG *)&recScreenSettings.Audio.uHz);
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        hrc = pRecScreenSettings->COMGETTER(AudioBits)((ULONG *)&recScreenSettings.Audio.cBits);
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        hrc = pRecScreenSettings->COMGETTER(AudioChannels)((ULONG *)&recScreenSettings.Audio.cChannels);
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        hrc = pRecScreenSettings->COMGETTER(VideoCodec)(&recScreenSettings.Video.enmCodec);
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        hrc = pRecScreenSettings->COMGETTER(VideoWidth)((ULONG *)&recScreenSettings.Video.ulWidth);
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        hrc = pRecScreenSettings->COMGETTER(VideoHeight)((ULONG *)&recScreenSettings.Video.ulHeight);
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        hrc = pRecScreenSettings->COMGETTER(VideoDeadline)(&recScreenSettings.Video.enmDeadline);
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        hrc = pRecScreenSettings->COMGETTER(VideoRateControlMode)(&recScreenSettings.Video.enmRateCtlMode);
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        hrc = pRecScreenSettings->COMGETTER(VideoScalingMode)(&recScreenSettings.Video.enmScalingMode);
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        hrc = pRecScreenSettings->COMGETTER(VideoRate)((ULONG *)&recScreenSettings.Video.ulRate);
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);
        hrc = pRecScreenSettings->COMGETTER(VideoFPS)((ULONG *)&recScreenSettings.Video.ulFPS);
        AssertComRCReturn(hrc, VERR_INVALID_PARAMETER);

        Settings.mapScreens[i] = recScreenSettings;
    }

    Assert(Settings.mapScreens.size() == paRecScreens.size());

    return VINF_SUCCESS;
}

/**
 * Creates the recording context.
 *
 * @returns VBox status code.
 */
int Console::i_recordingCreate(ComPtr<IProgress> &pProgress)
{
    settings::Recording Settings;
    int vrc = i_recordingGetSettings(Settings);
    if (RT_SUCCESS(vrc))
        vrc = mRecording.mCtx.Create(this, Settings, pProgress);

    if (RT_FAILURE(vrc))
        setErrorBoth(VBOX_E_RECORDING_ERROR, vrc, tr("Recording initialization failed (%Rrc) -- please consult log file for details"), vrc);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Destroys the recording context.
 */
void Console::i_recordingDestroy(void)
{
    mRecording.mCtx.Destroy();
}

/**
 * Starts recording. Does nothing if recording is already started.
 *
 * @returns VBox status code.
 */
int Console::i_recordingStart(util::AutoWriteLock *pAutoLock /* = NULL */)
{
    RT_NOREF(pAutoLock);

    if (mRecording.mCtx.IsStarted())
        return VINF_SUCCESS;

    int vrc = mRecording.mCtx.Start();
    if (RT_SUCCESS(vrc))
        vrc = mDisplay->i_recordingStart();

    if (RT_FAILURE(vrc))
        mRecording.mCtx.SetError(vrc, Utf8StrFmt(tr("Recording start failed (%Rrc)"), vrc).c_str());

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Stops recording. Does nothing if recording is not in started state.
 *
 * Note: This does *not* disable recording for a VM, in other words,
 *       it does not change the VM's recording (enabled) setting.
 */
int Console::i_recordingStop(util::AutoWriteLock *)
{
    if (!mRecording.mCtx.IsStarted())
        return VINF_SUCCESS;

    int vrc = mRecording.mCtx.Stop();
    if (RT_SUCCESS(vrc))
        vrc = mDisplay->i_recordingStop();

    if (RT_FAILURE(vrc))
        setErrorBoth(VBOX_E_RECORDING_ERROR, vrc, tr("Recording stop failed (%Rrc)"), vrc);

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Sends a cursor shape change to the recording context.
 *
 * @returns VBox status code.
 * @param   fVisible            Whether the mouse cursor actually is visible or not.
 * @param   fAlpha              Whether the pixel data contains alpha channel information or not.
 * @param   xHot                X hot position (in pixel) of the new cursor.
 * @param   yHot                Y hot position (in pixel) of the new cursor.
 * @param   uWidth              Width (in pixel) of the new cursor.
 * @param   uHeight             Height (in pixel) of the new cursor.
 * @param   pu8Shape            Pixel data of the new cursor.
 * @param   cbShape             Size of \a pu8Shape (in bytes).
 */
int Console::i_recordingCursorShapeChange(bool fVisible, bool fAlpha, uint32_t xHot, uint32_t yHot, uint32_t uWidth, uint32_t uHeight, const uint8_t *pu8Shape, uint32_t cbShape)
{
    if (!mRecording.mCtx.IsStarted())
        return VINF_SUCCESS;

    return mRecording.mCtx.SendCursorShapeChange(fVisible, fAlpha, xHot, yHot, uWidth, uHeight, pu8Shape, cbShape,
                                                 mRecording.mCtx.GetCurrentPTS());
}
#endif /* VBOX_WITH_RECORDING */

/**
 * Gets called by Session::UpdateMachineState()
 * (IInternalSessionControl::updateMachineState()).
 *
 * Must be called only in certain cases (see the implementation).
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_updateMachineState(MachineState_T aMachineState)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    AssertReturn(   mMachineState == MachineState_Saving
                 || mMachineState == MachineState_OnlineSnapshotting
                 || mMachineState == MachineState_LiveSnapshotting
                 || mMachineState == MachineState_DeletingSnapshotOnline
                 || mMachineState == MachineState_DeletingSnapshotPaused
                 || aMachineState == MachineState_Saving
                 || aMachineState == MachineState_OnlineSnapshotting
                 || aMachineState == MachineState_LiveSnapshotting
                 || aMachineState == MachineState_DeletingSnapshotOnline
                 || aMachineState == MachineState_DeletingSnapshotPaused
                 , E_FAIL);

    return i_setMachineStateLocally(aMachineState);
}

/**
 * Gets called by Session::COMGETTER(NominalState)()
 * (IInternalSessionControl::getNominalState()).
 *
 * @note Locks this object for reading.
 */
HRESULT Console::i_getNominalState(MachineState_T &aNominalState)
{
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    /* Get the VM handle. */
    SafeVMPtr ptrVM(this);
    if (!ptrVM.isOk())
        return ptrVM.hrc();

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    MachineState_T enmMachineState = MachineState_Null;
    VMSTATE enmVMState = ptrVM.vtable()->pfnVMR3GetStateU(ptrVM.rawUVM());
    switch (enmVMState)
    {
        case VMSTATE_CREATING:
        case VMSTATE_CREATED:
        case VMSTATE_POWERING_ON:
            enmMachineState = MachineState_Starting;
            break;
        case VMSTATE_LOADING:
            enmMachineState = MachineState_Restoring;
            break;
        case VMSTATE_RESUMING:
        case VMSTATE_SUSPENDING:
        case VMSTATE_SUSPENDING_LS:
        case VMSTATE_SUSPENDING_EXT_LS:
        case VMSTATE_SUSPENDED:
        case VMSTATE_SUSPENDED_LS:
        case VMSTATE_SUSPENDED_EXT_LS:
            enmMachineState = MachineState_Paused;
            break;
        case VMSTATE_RUNNING:
        case VMSTATE_RUNNING_LS:
        case VMSTATE_RESETTING:
        case VMSTATE_RESETTING_LS:
        case VMSTATE_SOFT_RESETTING:
        case VMSTATE_SOFT_RESETTING_LS:
        case VMSTATE_DEBUGGING:
        case VMSTATE_DEBUGGING_LS:
            enmMachineState = MachineState_Running;
            break;
        case VMSTATE_SAVING:
            enmMachineState = MachineState_Saving;
            break;
        case VMSTATE_POWERING_OFF:
        case VMSTATE_POWERING_OFF_LS:
        case VMSTATE_DESTROYING:
            enmMachineState = MachineState_Stopping;
            break;
        case VMSTATE_OFF:
        case VMSTATE_OFF_LS:
        case VMSTATE_FATAL_ERROR:
        case VMSTATE_FATAL_ERROR_LS:
        case VMSTATE_LOAD_FAILURE:
        case VMSTATE_TERMINATED:
            enmMachineState = MachineState_PoweredOff;
            break;
        case VMSTATE_GURU_MEDITATION:
        case VMSTATE_GURU_MEDITATION_LS:
            enmMachineState = MachineState_Stuck;
            break;
        default:
            AssertMsgFailed(("%s\n", ptrVM.vtable()->pfnVMR3GetStateName(enmVMState)));
            enmMachineState = MachineState_PoweredOff;
    }
    aNominalState = enmMachineState;

    LogFlowFuncLeave();
    return S_OK;
}

void Console::i_onMousePointerShapeChange(bool fVisible, bool fAlpha,
                                          uint32_t xHot, uint32_t yHot,
                                          uint32_t width, uint32_t height,
                                          const uint8_t *pu8Shape,
                                          uint32_t cbShape)
{
#if 0
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("fVisible=%d, fAlpha=%d, xHot = %d, yHot = %d, width=%d, height=%d, shape=%p\n",
                      fVisible, fAlpha, xHot, yHot, width, height, pShape));
#endif

    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    if (!mMouse.isNull())
       mMouse->i_updatePointerShape(fVisible, fAlpha, xHot, yHot, width, height, pu8Shape, cbShape);

    com::SafeArray<BYTE> shape(cbShape);
    if (pu8Shape)
        memcpy(shape.raw(), pu8Shape, cbShape);
    ::FireMousePointerShapeChangedEvent(mEventSource, fVisible, fAlpha, xHot, yHot, width, height, ComSafeArrayAsInParam(shape));

#ifdef VBOX_WITH_RECORDING
    i_recordingCursorShapeChange(fVisible, fAlpha, xHot, yHot, width, height, pu8Shape, cbShape);
#endif

#if 0
    LogFlowThisFuncLeave();
#endif
}

void Console::i_onMouseCapabilityChange(BOOL supportsAbsolute, BOOL supportsRelative,
                                        BOOL supportsTouchScreen, BOOL supportsTouchPad, BOOL needsHostCursor)
{
    LogFlowThisFunc(("supportsAbsolute=%d supportsRelative=%d supportsTouchScreen=%d supportsTouchPad=%d needsHostCursor=%d\n",
                     supportsAbsolute, supportsRelative, supportsTouchScreen, supportsTouchPad, needsHostCursor));

    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    ::FireMouseCapabilityChangedEvent(mEventSource, supportsAbsolute, supportsRelative, supportsTouchScreen, supportsTouchPad, needsHostCursor);
}

void Console::i_onStateChange(MachineState_T machineState)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());
    ::FireStateChangedEvent(mEventSource, machineState);
}

void Console::i_onAdditionsStateChange()
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    ::FireAdditionsStateChangedEvent(mEventSource);
}

/**
 * @remarks This notification only is for reporting an incompatible
 *          Guest Additions interface, *not* the Guest Additions version!
 *
 *          The user will be notified inside the guest if new Guest
 *          Additions are available (via VBoxTray/VBoxClient).
 */
void Console::i_onAdditionsOutdated()
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    /** @todo implement this */
}

void Console::i_onKeyboardLedsChange(bool fNumLock, bool fCapsLock, bool fScrollLock)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    ::FireKeyboardLedsChangedEvent(mEventSource, fNumLock, fCapsLock, fScrollLock);
}

void Console::i_onUSBDeviceStateChange(IUSBDevice *aDevice, bool aAttached,
                                       IVirtualBoxErrorInfo *aError)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    ::FireUSBDeviceStateChangedEvent(mEventSource, aDevice, aAttached, aError);
}

void Console::i_onRuntimeError(BOOL aFatal, IN_BSTR aErrorID, IN_BSTR aMessage)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    ::FireRuntimeErrorEvent(mEventSource, aFatal, aErrorID, aMessage);
}

HRESULT Console::i_onShowWindow(BOOL aCheck, BOOL *aCanShow, LONG64 *aWinId)
{
    AssertReturn(aCanShow, E_POINTER);
    AssertReturn(aWinId, E_POINTER);

    *aCanShow = FALSE;
    *aWinId = 0;

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    ComPtr<IEvent> ptrEvent;
    if (aCheck)
    {
        *aCanShow = TRUE;
        HRESULT hrc = ::CreateCanShowWindowEvent(ptrEvent.asOutParam(), mEventSource);
        if (SUCCEEDED(hrc))
        {
            VBoxEventDesc EvtDesc(ptrEvent, mEventSource);
            BOOL fDelivered = EvtDesc.fire(5000); /* Wait up to 5 secs for delivery */
            //Assert(fDelivered);
            if (fDelivered)
            {
                // bit clumsy
                ComPtr<ICanShowWindowEvent> ptrCanShowEvent = ptrEvent;
                if (ptrCanShowEvent)
                {
                    BOOL fVetoed   = FALSE;
                    BOOL fApproved = FALSE;
                    ptrCanShowEvent->IsVetoed(&fVetoed);
                    ptrCanShowEvent->IsApproved(&fApproved);
                    *aCanShow = fApproved || !fVetoed;
                }
                else
                    AssertFailed();
            }
        }
    }
    else
    {
        HRESULT hrc = ::CreateShowWindowEvent(ptrEvent.asOutParam(), mEventSource, 0);
        if (SUCCEEDED(hrc))
        {
            VBoxEventDesc EvtDesc(ptrEvent, mEventSource);
            BOOL fDelivered = EvtDesc.fire(5000); /* Wait up to 5 secs for delivery */
            //Assert(fDelivered);
            if (fDelivered)
            {
                ComPtr<IShowWindowEvent> ptrShowEvent = ptrEvent;
                if (ptrShowEvent)
                {
                    LONG64 idWindow = 0;
                    ptrShowEvent->COMGETTER(WinId)(&idWindow);
                    if (idWindow != 0 && *aWinId == 0)
                        *aWinId = idWindow;
                }
                else
                    AssertFailed();
            }
        }
    }

    return S_OK;
}

// private methods
////////////////////////////////////////////////////////////////////////////////

/**
 * Loads the VMM if needed.
 *
 * @returns COM status.
 * @param   pszVMMMod       The VMM module to load.
 * @remarks Caller must write lock the console object.
 */
HRESULT Console::i_loadVMM(const char *pszVMMMod) RT_NOEXCEPT
{
    if (   mhModVMM == NIL_RTLDRMOD
        || mpVMM == NULL)
    {
        Assert(!mpVMM);

        HRESULT         hrc;
        RTERRINFOSTATIC ErrInfo;
        RTLDRMOD        hModVMM = NIL_RTLDRMOD;
        int vrc = SUPR3HardenedLdrLoadAppPriv(pszVMMMod, &hModVMM, RTLDRLOAD_FLAGS_LOCAL, RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(vrc))
        {
            PFNVMMGETVTABLE pfnGetVTable = NULL;
            vrc = RTLdrGetSymbol(hModVMM, VMMR3VTABLE_GETTER_NAME, (void **)&pfnGetVTable);
            if (pfnGetVTable)
            {
                PCVMMR3VTABLE pVMM = pfnGetVTable();
                if (pVMM)
                {
                    if (VMMR3VTABLE_IS_COMPATIBLE(pVMM->uMagicVersion))
                    {
                        if (pVMM->uMagicVersion == pVMM->uMagicVersionEnd)
                        {
                            mhModVMM = hModVMM;
                            mpVMM    = pVMM;
                            LogFunc(("mhLdrVMM=%p phVMM=%p uMagicVersion=%#RX64\n", hModVMM, pVMM, pVMM->uMagicVersion));
                            return S_OK;
                        }

                        hrc = setErrorVrc(vrc, "Bogus VMM vtable: uMagicVersion=%#RX64 uMagicVersionEnd=%#RX64",
                                          pVMM->uMagicVersion, pVMM->uMagicVersionEnd);
                    }
                    else
                        hrc = setErrorVrc(vrc, "Incompatible of bogus VMM version magic: %#RX64", pVMM->uMagicVersion);
                }
                else
                    hrc = setErrorVrc(vrc, "pfnGetVTable return NULL!");
            }
            else
                hrc = setErrorVrc(vrc, "Failed to locate symbol '%s' in VBoxVMM: %Rrc", VMMR3VTABLE_GETTER_NAME, vrc);
            RTLdrClose(hModVMM);
        }
        else
            hrc = setErrorVrc(vrc, "Failed to load VBoxVMM: %#RTeic", &ErrInfo.Core);
        return hrc;
    }

    return S_OK;
}

/**
 * Increases the usage counter of the mpUVM pointer.
 *
 * Guarantees that VMR3Destroy() will not be called on it at least until
 * releaseVMCaller() is called.
 *
 * If this method returns a failure, the caller is not allowed to use mpUVM and
 * may return the failed result code to the upper level. This method sets the
 * extended error info on failure if \a aQuiet is false.
 *
 * Setting \a aQuiet to true is useful for methods that don't want to return
 * the failed result code to the caller when this method fails (e.g. need to
 * silently check for the mpUVM availability).
 *
 * When mpUVM is NULL but \a aAllowNullVM is true, a corresponding error will be
 * returned instead of asserting. Having it false is intended as a sanity check
 * for methods that have checked mMachineState and expect mpUVM *NOT* to be
 * NULL.
 *
 * @param aQuiet       true to suppress setting error info
 * @param aAllowNullVM true to accept mpUVM being NULL and return a failure
 *                     (otherwise this method will assert if mpUVM is NULL)
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_addVMCaller(bool aQuiet /* = false */,
                               bool aAllowNullVM /* = false */)
{
    RT_NOREF(aAllowNullVM);
    AutoCaller autoCaller(this);
    /** @todo Fix race during console/VM reference destruction, refer @bugref{6318}
     *        comment 25. */
    if (FAILED(autoCaller.hrc()))
        return autoCaller.hrc();

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    if (mVMDestroying)
    {
        /* powerDown() is waiting for all callers to finish */
        return aQuiet ? E_ACCESSDENIED : setError(E_ACCESSDENIED, tr("The virtual machine is being powered down"));
    }

    if (mpUVM == NULL)
    {
        Assert(aAllowNullVM == true);

        /* The machine is not powered up */
        return aQuiet ? E_ACCESSDENIED : setError(E_ACCESSDENIED, tr("The virtual machine is not powered up"));
    }

    ++mVMCallers;

    return S_OK;
}

/**
 * Decreases the usage counter of the mpUVM pointer.
 *
 * Must always complete the addVMCaller() call after the mpUVM pointer is no
 * more necessary.
 *
 * @note Locks this object for writing.
 */
void Console::i_releaseVMCaller()
{
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    AssertReturnVoid(mpUVM != NULL);

    Assert(mVMCallers > 0);
    --mVMCallers;

    if (mVMCallers == 0 && mVMDestroying)
    {
        /* inform powerDown() there are no more callers */
        RTSemEventSignal(mVMZeroCallersSem);
    }
}


/**
 * Helper for SafeVMPtrBase.
 */
HRESULT Console::i_safeVMPtrRetainer(PUVM *a_ppUVM, PCVMMR3VTABLE *a_ppVMM, bool a_Quiet) RT_NOEXCEPT
{
    *a_ppUVM = NULL;
    *a_ppVMM = NULL;

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /*
     * Repeat the checks done by addVMCaller.
     */
    if (mVMDestroying) /* powerDown() is waiting for all callers to finish */
        return a_Quiet
             ? E_ACCESSDENIED
             : setError(E_ACCESSDENIED, tr("The virtual machine is being powered down"));
    PUVM const pUVM = mpUVM;
    if (!pUVM)
        return a_Quiet
             ? E_ACCESSDENIED
             : setError(E_ACCESSDENIED, tr("The virtual machine is powered off"));
    PCVMMR3VTABLE const pVMM = mpVMM;
    if (!pVMM)
        return a_Quiet
             ? E_ACCESSDENIED
             : setError(E_ACCESSDENIED, tr("No VMM loaded!"));

    /*
     * Retain a reference to the user mode VM handle and get the global handle.
     */
    uint32_t cRefs = pVMM->pfnVMR3RetainUVM(pUVM);
    if (cRefs == UINT32_MAX)
        return a_Quiet
            ? E_ACCESSDENIED
            : setError(E_ACCESSDENIED, tr("The virtual machine is powered off"));

    /* done */
    *a_ppUVM = pUVM;
    *a_ppVMM = pVMM;
    return S_OK;
}

void Console::i_safeVMPtrReleaser(PUVM *a_ppUVM)
{
    PUVM const pUVM = *a_ppUVM;
    *a_ppUVM = NULL;
    if (pUVM)
    {
        PCVMMR3VTABLE const pVMM = mpVMM;
        if (pVMM)
            pVMM->pfnVMR3ReleaseUVM(pUVM);
    }
}

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION

/*static*/ DECLCALLBACK(int)
Console::i_logEncryptedDirCtxOpen(PCRTLOGOUTPUTIF pIf, void *pvUser, const char *pszFilename, void **ppvDirCtx)
{
    RT_NOREF(pIf, pvUser, pszFilename, ppvDirCtx);
    *ppvDirCtx = (void *)(intptr_t)22;
    return VINF_SUCCESS;
}

/*static*/ DECLCALLBACK(int)
Console::i_logEncryptedDirCtxClose(PCRTLOGOUTPUTIF pIf, void *pvUser, void *pvDirCtx)
{
    RT_NOREF(pIf, pvUser, pvDirCtx);
    Assert((intptr_t)pvDirCtx == 22);
    return VINF_SUCCESS;
}

/*static*/ DECLCALLBACK(int)
Console::i_logEncryptedDelete(PCRTLOGOUTPUTIF pIf, void *pvUser, void *pvDirCtx, const char *pszFilename)
{
    RT_NOREF(pIf, pvDirCtx, pvUser);
    return RTFileDelete(pszFilename);
}


/*static*/ DECLCALLBACK(int)
Console::i_logEncryptedRename(PCRTLOGOUTPUTIF pIf, void *pvUser, void *pvDirCtx,
                              const char *pszFilenameOld, const char *pszFilenameNew, uint32_t fFlags)
{
    RT_NOREF(pIf, pvDirCtx, pvUser);
    return RTFileRename(pszFilenameOld, pszFilenameNew, fFlags);
}

/*static*/ DECLCALLBACK(int)
Console::i_logEncryptedOpen(PCRTLOGOUTPUTIF pIf, void *pvUser, void *pvDirCtx, const char *pszFilename, uint32_t fFlags)
{
    RT_NOREF(pIf, pvDirCtx);
    Console *pConsole = static_cast<Console *>(pvUser);
    RTVFSFILE hVfsFile = NIL_RTVFSFILE;

    int vrc = RTVfsFileOpenNormal(pszFilename, fFlags, &hVfsFile);
    if (RT_SUCCESS(vrc))
    {
        PCVBOXCRYPTOIF pCryptoIf = NULL;
        vrc = pConsole->i_retainCryptoIf(&pCryptoIf);
        if (RT_SUCCESS(vrc))
        {
            SecretKey *pKey = NULL;

            vrc = pConsole->m_pKeyStore->retainSecretKey(pConsole->m_strLogKeyId, &pKey);
            if (RT_SUCCESS(vrc))
            {
                const char *pszPassword = (const char *)pKey->getKeyBuffer();

                vrc = pCryptoIf->pfnCryptoFileFromVfsFile(hVfsFile, pConsole->m_strLogKeyStore.c_str(), pszPassword,
                                                          &pConsole->m_hVfsFileLog);
                pKey->release();
            }

            /* On success we keep the reference to keep the cryptographic module loaded. */
            if (RT_FAILURE(vrc))
                pConsole->i_releaseCryptoIf(pCryptoIf);
        }

        /* Always do this because the encrypted log has retained a reference to the underlying file. */
        RTVfsFileRelease(hVfsFile);
        if (RT_FAILURE(vrc))
            RTFileDelete(pszFilename);
    }

    return vrc;
}


/*static*/ DECLCALLBACK(int)
Console::i_logEncryptedClose(PCRTLOGOUTPUTIF pIf, void *pvUser)
{
    RT_NOREF(pIf);
    Console *pConsole = static_cast<Console *>(pvUser);

    RTVfsFileRelease(pConsole->m_hVfsFileLog);
    pConsole->m_hVfsFileLog = NIL_RTVFSFILE;
    return VINF_SUCCESS;
}


/*static*/ DECLCALLBACK(int)
Console::i_logEncryptedQuerySize(PCRTLOGOUTPUTIF pIf, void *pvUser, uint64_t *pcbSize)
{
    RT_NOREF(pIf);
    Console *pConsole = static_cast<Console *>(pvUser);

    return RTVfsFileQuerySize(pConsole->m_hVfsFileLog, pcbSize);
}


/*static*/ DECLCALLBACK(int)
Console::i_logEncryptedWrite(PCRTLOGOUTPUTIF pIf, void *pvUser, const void *pvBuf, size_t cbWrite, size_t *pcbWritten)
{
    RT_NOREF(pIf);
    Console *pConsole = static_cast<Console *>(pvUser);

    return RTVfsFileWrite(pConsole->m_hVfsFileLog, pvBuf, cbWrite, pcbWritten);
}


/*static*/ DECLCALLBACK(int)
Console::i_logEncryptedFlush(PCRTLOGOUTPUTIF pIf, void *pvUser)
{
    RT_NOREF(pIf);
    Console *pConsole = static_cast<Console *>(pvUser);

    return RTVfsFileFlush(pConsole->m_hVfsFileLog);
}


/*static*/ RTLOGOUTPUTIF const Console::s_ConsoleEncryptedLogOutputIf =
{
    Console::i_logEncryptedDirCtxOpen,
    Console::i_logEncryptedDirCtxClose,
    Console::i_logEncryptedDelete,
    Console::i_logEncryptedRename,
    Console::i_logEncryptedOpen,
    Console::i_logEncryptedClose,
    Console::i_logEncryptedQuerySize,
    Console::i_logEncryptedWrite,
    Console::i_logEncryptedFlush
};

#endif /* VBOX_WITH_FULL_VM_ENCRYPTION */

/**
 * Initialize the release logging facility. In case something
 * goes wrong, there will be no release logging. Maybe in the future
 * we can add some logic to use different file names in this case.
 * Note that the logic must be in sync with Machine::DeleteSettings().
 */
HRESULT Console::i_consoleInitReleaseLog(const ComPtr<IMachine> aMachine)
{
    Bstr bstrLogFolder;
    HRESULT hrc = aMachine->COMGETTER(LogFolder)(bstrLogFolder.asOutParam());
    if (FAILED(hrc))
        return hrc;
    Utf8Str strLogDir = bstrLogFolder;

    /* make sure the Logs folder exists */
    Assert(strLogDir.length());
    if (!RTDirExists(strLogDir.c_str()))
        RTDirCreateFullPath(strLogDir.c_str(), 0700);

    Utf8StrFmt logFile("%s%cVBox.log", strLogDir.c_str(), RTPATH_DELIMITER);
    Utf8StrFmt pngFile("%s%cVBox.png", strLogDir.c_str(), RTPATH_DELIMITER);

    /*
     * Age the old log files.
     * Rename .(n-1) to .(n), .(n-2) to .(n-1), ..., and the last log file to .1
     * Overwrite target files in case they exist.
     */
    ComPtr<IVirtualBox> pVirtualBox;
    aMachine->COMGETTER(Parent)(pVirtualBox.asOutParam());
    ComPtr<ISystemProperties> pSystemProperties;
    pVirtualBox->COMGETTER(SystemProperties)(pSystemProperties.asOutParam());
    ULONG cHistoryFiles = 3;
    pSystemProperties->COMGETTER(LogHistoryCount)(&cHistoryFiles);
    if (cHistoryFiles)
    {
        for (int i = cHistoryFiles - 1; i >= 0; i--)
        {
            Utf8Str *files[] = { &logFile, &pngFile };
            Utf8Str oldName, newName;

            for (unsigned int j = 0; j < RT_ELEMENTS(files); ++j)
            {
                if (i > 0)
                    oldName.printf("%s.%d", files[j]->c_str(), i);
                else
                    oldName = *files[j];
                newName.printf("%s.%d", files[j]->c_str(), i + 1);

                /* If the old file doesn't exist, delete the new file (if it
                 * exists) to provide correct rotation even if the sequence is
                 * broken */
                if (RTFileRename(oldName.c_str(), newName.c_str(), RTFILEMOVE_FLAGS_REPLACE) == VERR_FILE_NOT_FOUND)
                    RTFileDelete(newName.c_str());
            }
        }
    }

    Bstr bstrLogKeyId;
    Bstr bstrLogKeyStore;
    PCRTLOGOUTPUTIF pLogOutputIf = NULL;
    void *pvLogOutputUser = NULL;
    int vrc = VINF_SUCCESS;
#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
    hrc = aMachine->COMGETTER(LogKeyId)(bstrLogKeyId.asOutParam());
    if (SUCCEEDED(hrc))
    {
        hrc = aMachine->COMGETTER(LogKeyStore)(bstrLogKeyStore.asOutParam());
        if (   SUCCEEDED(hrc)
            && bstrLogKeyId.isNotEmpty()
            && bstrLogKeyStore.isNotEmpty())
        {
            m_strLogKeyId    = Utf8Str(bstrLogKeyId);
            m_strLogKeyStore = Utf8Str(bstrLogKeyStore);

            pLogOutputIf    = &s_ConsoleEncryptedLogOutputIf;
            pvLogOutputUser = this;
            m_fEncryptedLog = true;
        }
    }

    if (RT_FAILURE(vrc))
        hrc = setErrorBoth(E_FAIL, vrc, tr("Failed to set encryption for release log (%Rrc)"), vrc);
    else
#endif
    {
        RTERRINFOSTATIC ErrInfo;
         vrc = com::VBoxLogRelCreateEx("VM", logFile.c_str(),
                                       RTLOGFLAGS_PREFIX_TIME_PROG | RTLOGFLAGS_RESTRICT_GROUPS,
                                       "all all.restrict -default.restrict",
                                       "VBOX_RELEASE_LOG", RTLOGDEST_FILE,
                                       32768 /* cMaxEntriesPerGroup */,
                                       0 /* cHistory */, 0 /* uHistoryFileTime */,
                                       0 /* uHistoryFileSize */,
                                       pLogOutputIf, pvLogOutputUser,
                                       RTErrInfoInitStatic(&ErrInfo));
        if (RT_FAILURE(vrc))
            hrc = setErrorBoth(E_FAIL, vrc, tr("Failed to open release log (%s, %Rrc)"), ErrInfo.Core.pszMsg, vrc);
    }

    /* If we've made any directory changes, flush the directory to increase
       the likelihood that the log file will be usable after a system panic.

       Tip: Try 'export VBOX_RELEASE_LOG_FLAGS=flush' if the last bits of the log
            is missing. Just don't have too high hopes for this to help. */
    if (SUCCEEDED(hrc) || cHistoryFiles)
        RTDirFlush(strLogDir.c_str());

    return hrc;
}

/**
 * Common worker for PowerUp and PowerUpPaused.
 *
 * @returns COM status code.
 *
 * @param   aProgress       Where to return the progress object.
 * @param   aPaused         true if PowerUpPaused called.
 */
HRESULT Console::i_powerUp(IProgress **aProgress, bool aPaused)
{
    LogFlowThisFuncEnter();

    CheckComArgOutPointerValid(aProgress);

    AutoCaller autoCaller(this);
    HRESULT hrc = autoCaller.hrc();
    if (FAILED(hrc))
        return hrc;

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    LogFlowThisFunc(("mMachineState=%d\n", mMachineState));

    if (Global::IsOnlineOrTransient(mMachineState))
        return setError(VBOX_E_INVALID_VM_STATE, tr("The virtual machine is already running or busy (machine state: %s)"),
                        Global::stringifyMachineState(mMachineState));


    /* Set up release logging as early as possible after the check if
     * there is already a running VM which we shouldn't disturb. */
    hrc = i_consoleInitReleaseLog(mMachine);
    if (FAILED(hrc))
        return hrc;

#ifdef VBOX_OPENSSL_FIPS
    LogRel(("crypto: FIPS mode %s\n", FIPS_mode() ? "enabled" : "FAILED"));
#endif

    /* test and clear the TeleporterEnabled property  */
    BOOL fTeleporterEnabled;
    hrc = mMachine->COMGETTER(TeleporterEnabled)(&fTeleporterEnabled);
    if (FAILED(hrc))
        return hrc;

#if 0 /** @todo we should save it afterwards, but that isn't necessarily a good idea. Find a better place for this (VBoxSVC).  */
    if (fTeleporterEnabled)
    {
        hrc = mMachine->COMSETTER(TeleporterEnabled)(FALSE);
        if (FAILED(hrc))
            return hrc;
    }
#endif

    PCVMMR3VTABLE const pVMM = mpVMM;
    AssertPtrReturn(pVMM, E_UNEXPECTED);

    ComObjPtr<Progress> pPowerupProgress;
    bool fBeganPoweringUp = false;

    LONG cOperations = 1;
    LONG ulTotalOperationsWeight = 1;
    VMPowerUpTask *task = NULL;

    try
    {
        /* Create a progress object to track progress of this operation. Must
         * be done as early as possible (together with BeginPowerUp()) as this
         * is vital for communicating as much as possible early powerup
         * failure information to the API caller */
        pPowerupProgress.createObject();
        Bstr progressDesc;
        if (mMachineState == MachineState_Saved || mMachineState == MachineState_AbortedSaved)
            progressDesc = tr("Restoring virtual machine");
        else if (fTeleporterEnabled)
            progressDesc = tr("Teleporting virtual machine");
        else
            progressDesc = tr("Starting virtual machine");

        /*
         * Saved VMs will have to prove that their saved states seem kosher.
         */
        Utf8Str strSavedStateFile;
        Bstr bstrStateKeyId;
        Bstr bstrStateKeyStore;

        if (mMachineState == MachineState_Saved || mMachineState == MachineState_AbortedSaved)
        {
            Bstr bstrSavedStateFile;
            hrc = mMachine->COMGETTER(StateFilePath)(bstrSavedStateFile.asOutParam());
            if (FAILED(hrc))
                throw hrc;
            strSavedStateFile = bstrSavedStateFile;

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
            hrc = mMachine->COMGETTER(StateKeyId)(bstrStateKeyId.asOutParam());
            if (FAILED(hrc))
                throw hrc;
            hrc = mMachine->COMGETTER(StateKeyStore)(bstrStateKeyStore.asOutParam());
            if (FAILED(hrc))
                throw hrc;
#endif

            ComAssertRet(bstrSavedStateFile.isNotEmpty(), E_FAIL);
            SsmStream ssmStream(this, pVMM, m_pKeyStore, bstrStateKeyId, bstrStateKeyStore);
            int vrc = ssmStream.open(strSavedStateFile.c_str());
            if (RT_SUCCESS(vrc))
            {
                PCSSMSTRMOPS pStreamOps;
                void *pvStreamOpsUser;

                vrc = ssmStream.querySsmStrmOps(&pStreamOps, &pvStreamOpsUser);
                if (RT_SUCCESS(vrc))
                    vrc = pVMM->pfnSSMR3ValidateFile(NULL /*pszFilename*/, pStreamOps, pvStreamOpsUser,
                                                     false /* fChecksumIt */);
            }

            if (RT_FAILURE(vrc))
            {
                Utf8Str errMsg;
                switch (vrc)
                {
                    case VERR_FILE_NOT_FOUND:
                        errMsg.printf(tr("VM failed to start because the saved state file '%s' does not exist."),
                                      strSavedStateFile.c_str());
                        break;
                    default:
                        errMsg.printf(tr("VM failed to start because the saved state file '%s' is invalid (%Rrc). "
                                         "Delete the saved state prior to starting the VM."), strSavedStateFile.c_str(), vrc);
                        break;
                }
                throw setErrorBoth(VBOX_E_FILE_ERROR, vrc, errMsg.c_str());
            }

        }

        /* Read console data, including console shared folders, stored in the
         * saved state file (if not yet done).
         */
        hrc = i_loadDataFromSavedState();
        if (FAILED(hrc))
            throw hrc;

        /* Check all types of shared folders and compose a single list */
        SharedFolderDataMap sharedFolders;
        {
            /* first, insert global folders */
            for (SharedFolderDataMap::const_iterator it = m_mapGlobalSharedFolders.begin();
                 it != m_mapGlobalSharedFolders.end();
                 ++it)
            {
                const SharedFolderData &d = it->second;
                sharedFolders[it->first] = d;
            }

            /* second, insert machine folders */
            for (SharedFolderDataMap::const_iterator it = m_mapMachineSharedFolders.begin();
                 it != m_mapMachineSharedFolders.end();
                 ++it)
            {
                const SharedFolderData &d = it->second;
                sharedFolders[it->first] = d;
            }

            /* third, insert console folders */
            for (SharedFolderMap::const_iterator it = m_mapSharedFolders.begin();
                 it != m_mapSharedFolders.end();
                 ++it)
            {
                ConsoleSharedFolder *pSF = it->second;
                AutoCaller sfCaller(pSF);
                AutoReadLock sfLock(pSF COMMA_LOCKVAL_SRC_POS);
                sharedFolders[it->first] = SharedFolderData(pSF->i_getHostPath(),
                                                            pSF->i_isWritable(),
                                                            pSF->i_isAutoMounted(),
                                                            pSF->i_getAutoMountPoint());
            }
        }


        /* Setup task object and thread to carry out the operation
         * asynchronously */
        try { task = new VMPowerUpTask(this, pPowerupProgress); }
        catch (std::bad_alloc &) { throw hrc = E_OUTOFMEMORY; }
        if (!task->isOk())
            throw task->hrc();

        task->mpfnConfigConstructor = i_configConstructor;
        task->mSharedFolders = sharedFolders;
        task->mStartPaused = aPaused;
        if (mMachineState == MachineState_Saved || mMachineState == MachineState_AbortedSaved)
            try { task->mSavedStateFile = strSavedStateFile; }
            catch (std::bad_alloc &) { throw hrc = E_OUTOFMEMORY; }
        task->mTeleporterEnabled = fTeleporterEnabled;

        /* Reset differencing hard disks for which autoReset is true,
         * but only if the machine has no snapshots OR the current snapshot
         * is an OFFLINE snapshot; otherwise we would reset the current
         * differencing image of an ONLINE snapshot which contains the disk
         * state of the machine while it was previously running, but without
         * the corresponding machine state, which is equivalent to powering
         * off a running machine and not good idea
         */
        ComPtr<ISnapshot> pCurrentSnapshot;
        hrc = mMachine->COMGETTER(CurrentSnapshot)(pCurrentSnapshot.asOutParam());
        if (FAILED(hrc))
            throw hrc;

        BOOL fCurrentSnapshotIsOnline = false;
        if (pCurrentSnapshot)
        {
            hrc = pCurrentSnapshot->COMGETTER(Online)(&fCurrentSnapshotIsOnline);
            if (FAILED(hrc))
                throw hrc;
        }

        if (strSavedStateFile.isEmpty() && !fCurrentSnapshotIsOnline)
        {
            LogFlowThisFunc(("Looking for immutable images to reset\n"));

            com::SafeIfaceArray<IMediumAttachment> atts;
            hrc = mMachine->COMGETTER(MediumAttachments)(ComSafeArrayAsOutParam(atts));
            if (FAILED(hrc))
                throw hrc;

            for (size_t i = 0;
                 i < atts.size();
                 ++i)
            {
                DeviceType_T devType;
                hrc = atts[i]->COMGETTER(Type)(&devType);
                /** @todo later applies to floppies as well */
                if (devType == DeviceType_HardDisk)
                {
                    ComPtr<IMedium> pMedium;
                    hrc = atts[i]->COMGETTER(Medium)(pMedium.asOutParam());
                    if (FAILED(hrc))
                        throw hrc;

                    /* needs autoreset? */
                    BOOL autoReset = FALSE;
                    hrc = pMedium->COMGETTER(AutoReset)(&autoReset);
                    if (FAILED(hrc))
                        throw hrc;

                    if (autoReset)
                    {
                        ComPtr<IProgress> pResetProgress;
                        hrc = pMedium->Reset(pResetProgress.asOutParam());
                        if (FAILED(hrc))
                            throw hrc;

                        /* save for later use on the powerup thread */
                        task->hardDiskProgresses.push_back(pResetProgress);
                    }
                }
            }
        }
        else
            LogFlowThisFunc(("Machine has a current snapshot which is online, skipping immutable images reset\n"));

        /* setup task object and thread to carry out the operation
         * asynchronously */

#ifdef VBOX_WITH_EXTPACK
        mptrExtPackManager->i_dumpAllToReleaseLog();
#endif

#ifdef RT_OS_SOLARIS
        /* setup host core dumper for the VM */
        Bstr value;
        hrc = mMachine->GetExtraData(Bstr("VBoxInternal2/CoreDumpEnabled").raw(), value.asOutParam());
        if (SUCCEEDED(hrc) && value == "1")
        {
            Bstr coreDumpDir, coreDumpReplaceSys, coreDumpLive;
            mMachine->GetExtraData(Bstr("VBoxInternal2/CoreDumpDir").raw(), coreDumpDir.asOutParam());
            mMachine->GetExtraData(Bstr("VBoxInternal2/CoreDumpReplaceSystemDump").raw(), coreDumpReplaceSys.asOutParam());
            mMachine->GetExtraData(Bstr("VBoxInternal2/CoreDumpLive").raw(), coreDumpLive.asOutParam());

            uint32_t fCoreFlags = 0;
            if (   coreDumpReplaceSys.isEmpty() == false
                && Utf8Str(coreDumpReplaceSys).toUInt32() == 1)
                fCoreFlags |= RTCOREDUMPER_FLAGS_REPLACE_SYSTEM_DUMP;

            if (   coreDumpLive.isEmpty() == false
                && Utf8Str(coreDumpLive).toUInt32() == 1)
                fCoreFlags |= RTCOREDUMPER_FLAGS_LIVE_CORE;

            Utf8Str strDumpDir(coreDumpDir);
            const char *pszDumpDir = strDumpDir.c_str();
            if (   pszDumpDir
                && *pszDumpDir == '\0')
                pszDumpDir = NULL;

            int vrc;
            if (   pszDumpDir
                && !RTDirExists(pszDumpDir))
            {
                /*
                 * Try create the directory.
                 */
                vrc = RTDirCreateFullPath(pszDumpDir, 0700);
                if (RT_FAILURE(vrc))
                    throw setErrorBoth(E_FAIL, vrc, tr("Failed to setup CoreDumper. Couldn't create dump directory '%s' (%Rrc)"),
                                       pszDumpDir, vrc);
            }

            vrc = RTCoreDumperSetup(pszDumpDir, fCoreFlags);
            if (RT_FAILURE(vrc))
                throw setErrorBoth(E_FAIL, vrc, tr("Failed to setup CoreDumper (%Rrc)"), vrc);
            LogRel(("CoreDumper setup successful. pszDumpDir=%s fFlags=%#x\n", pszDumpDir ? pszDumpDir : ".", fCoreFlags));
        }
#endif


        // If there is immutable drive the process that.
        VMPowerUpTask::ProgressList progresses(task->hardDiskProgresses);
        if (aProgress && !progresses.empty())
        {
            for (VMPowerUpTask::ProgressList::const_iterator it = progresses.begin(); it !=  progresses.end(); ++it)
            {
                ++cOperations;
                ulTotalOperationsWeight += 1;
            }
            hrc = pPowerupProgress->init(static_cast<IConsole *>(this),
                                         progressDesc.raw(),
                                         TRUE, // Cancelable
                                         cOperations,
                                         ulTotalOperationsWeight,
                                         tr("Starting Hard Disk operations"),
                                         1);
            AssertComRCReturnRC(hrc);
        }
        else if (   mMachineState == MachineState_Saved
                 || mMachineState == MachineState_AbortedSaved
                 || !fTeleporterEnabled)
            hrc = pPowerupProgress->init(static_cast<IConsole *>(this),
                                         progressDesc.raw(),
                                         FALSE /* aCancelable */);
        else if (fTeleporterEnabled)
            hrc = pPowerupProgress->init(static_cast<IConsole *>(this),
                                         progressDesc.raw(),
                                         TRUE /* aCancelable */,
                                         3    /* cOperations */,
                                         10   /* ulTotalOperationsWeight */,
                                         tr("Teleporting virtual machine"),
                                         1    /* ulFirstOperationWeight */);

        if (FAILED(hrc))
            throw hrc;

        /* Tell VBoxSVC and Machine about the progress object so they can
           combine/proxy it to any openRemoteSession caller. */
        LogFlowThisFunc(("Calling BeginPowerUp...\n"));
        hrc = mControl->BeginPowerUp(pPowerupProgress);
        if (FAILED(hrc))
        {
            LogFlowThisFunc(("BeginPowerUp failed\n"));
            throw hrc;
        }
        fBeganPoweringUp = true;

        LogFlowThisFunc(("Checking if canceled...\n"));
        BOOL fCanceled;
        hrc = pPowerupProgress->COMGETTER(Canceled)(&fCanceled);
        if (FAILED(hrc))
            throw hrc;

        if (fCanceled)
        {
            LogFlowThisFunc(("Canceled in BeginPowerUp\n"));
            throw setError(E_FAIL, tr("Powerup was canceled"));
        }
        LogFlowThisFunc(("Not canceled yet.\n"));

        /** @todo this code prevents starting a VM with unavailable bridged
         * networking interface. The only benefit is a slightly better error
         * message, which should be moved to the driver code. This is the
         * only reason why I left the code in for now. The driver allows
         * unavailable bridged networking interfaces in certain circumstances,
         * and this is sabotaged by this check. The VM will initially have no
         * network connectivity, but the user can fix this at runtime. */
#if 0
        /* the network cards will undergo a quick consistency check */
        for (ULONG slot = 0;
             slot < maxNetworkAdapters;
             ++slot)
        {
            ComPtr<INetworkAdapter> pNetworkAdapter;
            mMachine->GetNetworkAdapter(slot, pNetworkAdapter.asOutParam());
            BOOL enabled = FALSE;
            pNetworkAdapter->COMGETTER(Enabled)(&enabled);
            if (!enabled)
                continue;

            NetworkAttachmentType_T netattach;
            pNetworkAdapter->COMGETTER(AttachmentType)(&netattach);
            switch (netattach)
            {
                case NetworkAttachmentType_Bridged:
                {
                    /* a valid host interface must have been set */
                    Bstr hostif;
                    pNetworkAdapter->COMGETTER(HostInterface)(hostif.asOutParam());
                    if (hostif.isEmpty())
                    {
                        throw setError(VBOX_E_HOST_ERROR,
                                       tr("VM cannot start because host interface networking requires a host interface name to be set"));
                    }
                    ComPtr<IVirtualBox> pVirtualBox;
                    mMachine->COMGETTER(Parent)(pVirtualBox.asOutParam());
                    ComPtr<IHost> pHost;
                    pVirtualBox->COMGETTER(Host)(pHost.asOutParam());
                    ComPtr<IHostNetworkInterface> pHostInterface;
                    if (!SUCCEEDED(pHost->FindHostNetworkInterfaceByName(hostif.raw(), pHostInterface.asOutParam())))
                        throw setError(VBOX_E_HOST_ERROR,
                                       tr("VM cannot start because the host interface '%ls' does not exist"), hostif.raw());
                    break;
                }
                default:
                    break;
            }
        }
#endif // 0


        /* setup task object and thread to carry out the operation
         * asynchronously */
        if (aProgress)
        {
            hrc = pPowerupProgress.queryInterfaceTo(aProgress);
            AssertComRCReturnRC(hrc);
        }

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
        task->mKeyStore   = Utf8Str(bstrStateKeyStore);
        task->mKeyId      = Utf8Str(bstrStateKeyId);
        task->m_pKeyStore = m_pKeyStore;
#endif

        hrc = task->createThread();
        task = NULL;
        if (FAILED(hrc))
            throw hrc;

        /* finally, set the state: no right to fail in this method afterwards
         * since we've already started the thread and it is now responsible for
         * any error reporting and appropriate state change! */
        if (mMachineState == MachineState_Saved || mMachineState == MachineState_AbortedSaved)
            i_setMachineState(MachineState_Restoring);
        else if (fTeleporterEnabled)
            i_setMachineState(MachineState_TeleportingIn);
        else
            i_setMachineState(MachineState_Starting);
    }
    catch (HRESULT aRC)
    {
        hrc = aRC;
    }

    if (FAILED(hrc) && fBeganPoweringUp)
    {

        /* The progress object will fetch the current error info */
        if (!pPowerupProgress.isNull())
            pPowerupProgress->i_notifyComplete(hrc);

        /* Save the error info across the IPC below. Can't be done before the
         * progress notification above, as saving the error info deletes it
         * from the current context, and thus the progress object wouldn't be
         * updated correctly. */
        ErrorInfoKeeper eik;

        /* signal end of operation */
        mControl->EndPowerUp(hrc);
    }

    if (task)
    {
        ErrorInfoKeeper eik;
        delete task;
    }

    LogFlowThisFunc(("mMachineState=%d, hrc=%Rhrc\n", mMachineState, hrc));
    LogFlowThisFuncLeave();
    return hrc;
}

/**
 * Internal power off worker routine.
 *
 * This method may be called only at certain places with the following meaning
 * as shown below:
 *
 * - if the machine state is either Running or Paused, a normal
 *   Console-initiated powerdown takes place (e.g. PowerDown());
 * - if the machine state is Saving, saveStateThread() has successfully done its
 *   job;
 * - if the machine state is Starting or Restoring, powerUpThread() has failed
 *   to start/load the VM;
 * - if the machine state is Stopping, the VM has powered itself off (i.e. not
 *   as a result of the powerDown() call).
 *
 * Calling it in situations other than the above will cause unexpected behavior.
 *
 * Note that this method should be the only one that destroys mpUVM and sets it
 * to NULL.
 *
 * @param aProgress Progress object to run (may be NULL).
 *
 * @note Locks this object for writing.
 *
 * @note Never call this method from a thread that called addVMCaller() or
 *       instantiated an AutoVMCaller object; first call releaseVMCaller() or
 *       release(). Otherwise it will deadlock.
 */
HRESULT Console::i_powerDown(IProgress *aProgress /*= NULL*/)
{
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    ComPtr<IInternalProgressControl> pProgressControl(aProgress);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* Total # of steps for the progress object. Must correspond to the
     * number of "advance percent count" comments in this method! */
    enum { StepCount = 7 };
    /* current step */
    ULONG step = 0;

    HRESULT hrc = S_OK;
    int vrc = VINF_SUCCESS;

    /* sanity */
    Assert(mVMDestroying == false);

    PCVMMR3VTABLE const pVMM = mpVMM;
    AssertPtrReturn(pVMM, E_UNEXPECTED);
    PUVM pUVM = mpUVM;
    AssertPtrReturn(pUVM, E_UNEXPECTED);

    uint32_t cRefs = pVMM->pfnVMR3RetainUVM(pUVM);
    Assert(cRefs != UINT32_MAX);  NOREF(cRefs);

    AssertMsg(   mMachineState == MachineState_Running
              || mMachineState == MachineState_Paused
              || mMachineState == MachineState_Stuck
              || mMachineState == MachineState_Starting
              || mMachineState == MachineState_Stopping
              || mMachineState == MachineState_Saving
              || mMachineState == MachineState_Restoring
              || mMachineState == MachineState_TeleportingPausedVM
              || mMachineState == MachineState_TeleportingIn
              , ("Invalid machine state: %s\n", ::stringifyMachineState(mMachineState)));

    LogRel(("Console::powerDown(): A request to power off the VM has been issued (mMachineState=%s, InUninit=%d)\n",
            ::stringifyMachineState(mMachineState), getObjectState().getState() == ObjectState::InUninit));

    /* Check if we need to power off the VM. In case of mVMPoweredOff=true, the
     * VM has already powered itself off in vmstateChangeCallback() and is just
     * notifying Console about that. In case of Starting or Restoring,
     * powerUpThread() is calling us on failure, so the VM is already off at
     * that point. */
    if (   !mVMPoweredOff
        && (   mMachineState == MachineState_Starting
            || mMachineState == MachineState_Restoring
            || mMachineState == MachineState_TeleportingIn)
       )
        mVMPoweredOff = true;

    /*
     * Go to Stopping state if not already there.
     *
     * Note that we don't go from Saving/Restoring to Stopping because
     * vmstateChangeCallback() needs it to set the state to Saved on
     * VMSTATE_TERMINATED. In terms of protecting from inappropriate operations
     * while leaving the lock below, Saving or Restoring should be fine too.
     * Ditto for TeleportingPausedVM -> Teleported.
     */
    if (   mMachineState != MachineState_Saving
        && mMachineState != MachineState_Restoring
        && mMachineState != MachineState_Stopping
        && mMachineState != MachineState_TeleportingIn
        && mMachineState != MachineState_TeleportingPausedVM
       )
        i_setMachineState(MachineState_Stopping);

    /* ----------------------------------------------------------------------
     * DONE with necessary state changes, perform the power down actions (it's
     * safe to release the object lock now if needed)
     * ---------------------------------------------------------------------- */

    if (mDisplay)
    {
        alock.release();

        mDisplay->i_notifyPowerDown();

        alock.acquire();
    }

    /* Stop the VRDP server to prevent new clients connection while VM is being
     * powered off. */
    if (mConsoleVRDPServer)
    {
        LogFlowThisFunc(("Stopping VRDP server...\n"));

        /* Leave the lock since EMT could call us back as addVMCaller() */
        alock.release();

        mConsoleVRDPServer->Stop();

        alock.acquire();
    }

    /* advance percent count */
    if (pProgressControl)
        pProgressControl->SetCurrentOperationProgress(99 * (++step) / StepCount);


    /* ----------------------------------------------------------------------
     * Now, wait for all mpUVM callers to finish their work if there are still
     * some on other threads. NO methods that need mpUVM (or initiate other calls
     * that need it) may be called after this point
     * ---------------------------------------------------------------------- */

    /* go to the destroying state to prevent from adding new callers */
    mVMDestroying = true;

    if (mVMCallers > 0)
    {
        /* lazy creation */
        if (mVMZeroCallersSem == NIL_RTSEMEVENT)
            RTSemEventCreate(&mVMZeroCallersSem);

        LogFlowThisFunc(("Waiting for mpUVM callers (%d) to drop to zero...\n", mVMCallers));

        alock.release();

        RTSemEventWait(mVMZeroCallersSem, RT_INDEFINITE_WAIT);

        alock.acquire();
    }

    /* advance percent count */
    if (pProgressControl)
        pProgressControl->SetCurrentOperationProgress(99 * (++step) / StepCount);

    vrc = VINF_SUCCESS;

    /*
     * Power off the VM if not already done that.
     * Leave the lock since EMT will call vmstateChangeCallback.
     *
     * Note that VMR3PowerOff() may fail here (invalid VMSTATE) if the
     * VM-(guest-)initiated power off happened in parallel a ms before this
     * call. So far, we let this error pop up on the user's side.
     */
    if (!mVMPoweredOff)
    {
        LogFlowThisFunc(("Powering off the VM...\n"));
        alock.release();
        vrc = pVMM->pfnVMR3PowerOff(pUVM);
#ifdef VBOX_WITH_EXTPACK
        mptrExtPackManager->i_callAllVmPowerOffHooks(this, pVMM->pfnVMR3GetVM(pUVM), pVMM);
#endif
        alock.acquire();
    }

    /* advance percent count */
    if (pProgressControl)
        pProgressControl->SetCurrentOperationProgress(99 * (++step) / StepCount);

#ifdef VBOX_WITH_HGCM
    /* Shutdown HGCM services before destroying the VM. */
    if (m_pVMMDev)
    {
        LogFlowThisFunc(("Shutdown HGCM...\n"));

        /* Leave the lock since EMT might wait for it and will call us back as addVMCaller() */
        alock.release();

# ifdef VBOX_WITH_SHARED_CLIPBOARD
        if (m_hHgcmSvcExtShCl)
        {
            HGCMHostUnregisterServiceExtension(m_hHgcmSvcExtShCl);
            m_hHgcmSvcExtShCl = NULL;
        }
        GuestShCl::destroyInstance();
#endif

# ifdef VBOX_WITH_DRAG_AND_DROP
        if (m_hHgcmSvcExtDragAndDrop)
        {
            HGCMHostUnregisterServiceExtension(m_hHgcmSvcExtDragAndDrop);
            m_hHgcmSvcExtDragAndDrop = NULL;
        }
# endif

        m_pVMMDev->hgcmShutdown();

        alock.acquire();
    }

    /* advance percent count */
    if (pProgressControl)
        pProgressControl->SetCurrentOperationProgress(99 * (++step) / StepCount);

#endif /* VBOX_WITH_HGCM */

    LogFlowThisFunc(("Ready for VM destruction.\n"));

    /* If we are called from Console::uninit(), then try to destroy the VM even
     * on failure (this will most likely fail too, but what to do?..) */
    if (RT_SUCCESS(vrc) || getObjectState().getState() == ObjectState::InUninit)
    {
        /* If the machine has a USB controller, release all USB devices
         * (symmetric to the code in captureUSBDevices()) */
        if (mfVMHasUsbController)
        {
            alock.release();
            i_detachAllUSBDevices(false /* aDone */);
            alock.acquire();
        }

        /* Now we've got to destroy the VM as well. (mpUVM is not valid beyond
         * this point). We release the lock before calling VMR3Destroy() because
         * it will result into calling destructors of drivers associated with
         * Console children which may in turn try to lock Console (e.g. by
         * instantiating SafeVMPtr to access mpUVM). It's safe here because
         * mVMDestroying is set which should prevent any activity. */

        /* Set mpUVM to NULL early just in case if some old code is not using
         * addVMCaller()/releaseVMCaller(). (We have our own ref on pUVM.) */
        pVMM->pfnVMR3ReleaseUVM(mpUVM);
        mpUVM = NULL;

        LogFlowThisFunc(("Destroying the VM...\n"));

        alock.release();

        vrc = pVMM->pfnVMR3Destroy(pUVM);

        /* take the lock again */
        alock.acquire();

        /* advance percent count */
        if (pProgressControl)
            pProgressControl->SetCurrentOperationProgress(99 * (++step) / StepCount);

        if (RT_SUCCESS(vrc))
        {
            LogFlowThisFunc(("Machine has been destroyed (mMachineState=%d)\n",
                             mMachineState));
            /* Note: the Console-level machine state change happens on the
             * VMSTATE_TERMINATE state change in vmstateChangeCallback(). If
             * powerDown() is called from EMT (i.e. from vmstateChangeCallback()
             * on receiving VM-initiated VMSTATE_OFF), VMSTATE_TERMINATE hasn't
             * occurred yet. This is okay, because mMachineState is already
             * Stopping in this case, so any other attempt to call PowerDown()
             * will be rejected. */
        }
        else
        {
            /* bad bad bad, but what to do? (Give Console our UVM ref.) */
            mpUVM = pUVM;
            pUVM = NULL;
            hrc = setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("Could not destroy the machine. (Error: %Rrc)"), vrc);
        }

        /* Complete the detaching of the USB devices. */
        if (mfVMHasUsbController)
        {
            alock.release();
            i_detachAllUSBDevices(true /* aDone */);
            alock.acquire();
        }

        /* advance percent count */
        if (pProgressControl)
            pProgressControl->SetCurrentOperationProgress(99 * (++step) / StepCount);
    }
    else
        hrc = setErrorBoth(VBOX_E_VM_ERROR, vrc, tr("Could not power off the machine. (Error: %Rrc)"), vrc);

    /*
     * Finished with the destruction.
     *
     * Note that if something impossible happened and we've failed to destroy
     * the VM, mVMDestroying will remain true and mMachineState will be
     * something like Stopping, so most Console methods will return an error
     * to the caller.
     */
    if (pUVM != NULL)
        pVMM->pfnVMR3ReleaseUVM(pUVM);
    else
        mVMDestroying = false;

    LogFlowThisFuncLeave();
    return hrc;
}

/**
 * @note Locks this object for writing.
 */
HRESULT Console::i_setMachineState(MachineState_T aMachineState, bool aUpdateServer /* = true */)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;

    if (mMachineState != aMachineState)
    {
        LogThisFunc(("machineState=%s -> %s aUpdateServer=%RTbool\n",
                     ::stringifyMachineState(mMachineState), ::stringifyMachineState(aMachineState), aUpdateServer));
        LogRel(("Console: Machine state changed to '%s'\n", ::stringifyMachineState(aMachineState)));
        mMachineState = aMachineState;

        /// @todo (dmik)
        //      possibly, we need to redo onStateChange() using the dedicated
        //      Event thread, like it is done in VirtualBox. This will make it
        //      much safer (no deadlocks possible if someone tries to use the
        //      console from the callback), however, listeners will lose the
        //      ability to synchronously react to state changes (is it really
        //      necessary??)
        LogFlowThisFunc(("Doing onStateChange()...\n"));
        i_onStateChange(aMachineState);
        LogFlowThisFunc(("Done onStateChange()\n"));

        if (aUpdateServer)
        {
            /* Server notification MUST be done from under the lock; otherwise
             * the machine state here and on the server might go out of sync
             * which can lead to various unexpected results (like the machine
             * state being >= MachineState_Running on the server, while the
             * session state is already SessionState_Unlocked at the same time
             * there).
             *
             * Cross-lock conditions should be carefully watched out: calling
             * UpdateState we will require Machine and SessionMachine locks
             * (remember that here we're holding the Console lock here, and also
             * all locks that have been acquire by the thread before calling
             * this method).
             */
            LogFlowThisFunc(("Doing mControl->UpdateState()...\n"));
            hrc = mControl->UpdateState(aMachineState);
            LogFlowThisFunc(("mControl->UpdateState()=%Rhrc\n", hrc));
        }
    }

    return hrc;
}

/**
 * Searches for a shared folder with the given logical name
 * in the collection of shared folders.
 *
 * @param strName          logical name of the shared folder
 * @param aSharedFolder    where to return the found object
 * @param aSetError        whether to set the error info if the folder is
 *                         not found
 * @return
 *     S_OK when found or E_INVALIDARG when not found
 *
 * @note The caller must lock this object for writing.
 */
HRESULT Console::i_findSharedFolder(const Utf8Str &strName, ComObjPtr<ConsoleSharedFolder> &aSharedFolder, bool aSetError /* = false */)
{
    /* sanity check */
    AssertReturn(isWriteLockOnCurrentThread(), E_FAIL);

    SharedFolderMap::const_iterator it = m_mapSharedFolders.find(strName);
    if (it != m_mapSharedFolders.end())
    {
        aSharedFolder = it->second;
        return S_OK;
    }

    if (aSetError)
        setError(VBOX_E_FILE_ERROR, tr("Could not find a shared folder named '%s'."), strName.c_str());
    return VBOX_E_FILE_ERROR;
}

/**
 * Fetches the list of global or machine shared folders from the server.
 *
 * @param aGlobal true to fetch global folders.
 *
 * @note The caller must lock this object for writing.
 */
HRESULT Console::i_fetchSharedFolders(BOOL aGlobal)
{
    /* sanity check */
    AssertReturn(   getObjectState().getState() == ObjectState::InInit
                 || isWriteLockOnCurrentThread(), E_FAIL);

    LogFlowThisFunc(("Entering\n"));

    /* Check if we're online and keep it that way. */
    SafeVMPtrQuiet ptrVM(this);
    AutoVMCallerQuietWeak autoVMCaller(this);
    bool const online = ptrVM.isOk()
                     && m_pVMMDev
                     && m_pVMMDev->isShFlActive();

    HRESULT hrc = S_OK;

    try
    {
        if (aGlobal)
        {
            /// @todo grab & process global folders when they are done
        }
        else
        {
            SharedFolderDataMap oldFolders;
            if (online)
                oldFolders = m_mapMachineSharedFolders;

            m_mapMachineSharedFolders.clear();

            SafeIfaceArray<ISharedFolder> folders;
            hrc = mMachine->COMGETTER(SharedFolders)(ComSafeArrayAsOutParam(folders));
            if (FAILED(hrc)) throw hrc;

            for (size_t i = 0; i < folders.size(); ++i)
            {
                ComPtr<ISharedFolder> pSharedFolder = folders[i];

                Bstr bstr;
                hrc = pSharedFolder->COMGETTER(Name)(bstr.asOutParam());
                if (FAILED(hrc)) throw hrc;
                Utf8Str strName(bstr);

                hrc = pSharedFolder->COMGETTER(HostPath)(bstr.asOutParam());
                if (FAILED(hrc)) throw hrc;
                Utf8Str strHostPath(bstr);

                BOOL writable;
                hrc = pSharedFolder->COMGETTER(Writable)(&writable);
                if (FAILED(hrc)) throw hrc;

                BOOL autoMount;
                hrc = pSharedFolder->COMGETTER(AutoMount)(&autoMount);
                if (FAILED(hrc)) throw hrc;

                hrc = pSharedFolder->COMGETTER(AutoMountPoint)(bstr.asOutParam());
                if (FAILED(hrc)) throw hrc;
                Utf8Str strAutoMountPoint(bstr);

                m_mapMachineSharedFolders.insert(std::make_pair(strName,
                                                                SharedFolderData(strHostPath, !!writable,
                                                                                 !!autoMount, strAutoMountPoint)));

                /* send changes to HGCM if the VM is running */
                if (online)
                {
                    SharedFolderDataMap::iterator it = oldFolders.find(strName);
                    if (    it == oldFolders.end()
                         || it->second.m_strHostPath != strHostPath)
                    {
                        /* a new machine folder is added or
                         * the existing machine folder is changed */
                        if (m_mapSharedFolders.find(strName) != m_mapSharedFolders.end())
                            ; /* the console folder exists, nothing to do */
                        else
                        {
                            /* remove the old machine folder (when changed)
                             * or the global folder if any (when new) */
                            if (    it != oldFolders.end()
                                 || m_mapGlobalSharedFolders.find(strName) != m_mapGlobalSharedFolders.end()
                               )
                            {
                                hrc = i_removeSharedFolder(strName);
                                if (FAILED(hrc)) throw hrc;
                            }

                            /* create the new machine folder */
                            hrc = i_createSharedFolder(strName,
                                                       SharedFolderData(strHostPath, !!writable, !!autoMount, strAutoMountPoint));
                            if (FAILED(hrc)) throw hrc;
                        }
                    }
                    /* forget the processed (or identical) folder */
                    if (it != oldFolders.end())
                        oldFolders.erase(it);
                }
            }

            /* process outdated (removed) folders */
            if (online)
            {
                for (SharedFolderDataMap::const_iterator it = oldFolders.begin();
                     it != oldFolders.end(); ++it)
                {
                    if (m_mapSharedFolders.find(it->first) != m_mapSharedFolders.end())
                        ; /* the console folder exists, nothing to do */
                    else
                    {
                        /* remove the outdated machine folder */
                        hrc = i_removeSharedFolder(it->first);
                        if (FAILED(hrc)) throw hrc;

                        /* create the global folder if there is any */
                        SharedFolderDataMap::const_iterator git =
                            m_mapGlobalSharedFolders.find(it->first);
                        if (git != m_mapGlobalSharedFolders.end())
                        {
                            hrc = i_createSharedFolder(git->first, git->second);
                            if (FAILED(hrc)) throw hrc;
                        }
                    }
                }
            }
        }
    }
    catch (HRESULT hrc2)
    {
        hrc = hrc2;
        if (online)
            i_atVMRuntimeErrorCallbackF(0, "BrokenSharedFolder", N_("Broken shared folder!"));
    }

    LogFlowThisFunc(("Leaving\n"));

    return hrc;
}

/**
 * Searches for a shared folder with the given name in the list of machine
 * shared folders and then in the list of the global shared folders.
 *
 * @param strName  Name of the folder to search for.
 * @param aIt      Where to store the pointer to the found folder.
 * @return         @c true if the folder was found and @c false otherwise.
 *
 * @note The caller must lock this object for reading.
 */
bool Console::i_findOtherSharedFolder(const Utf8Str &strName,
                                    SharedFolderDataMap::const_iterator &aIt)
{
    /* sanity check */
    AssertReturn(isWriteLockOnCurrentThread(), false);

    /* first, search machine folders */
    aIt = m_mapMachineSharedFolders.find(strName);
    if (aIt != m_mapMachineSharedFolders.end())
        return true;

    /* second, search machine folders */
    aIt = m_mapGlobalSharedFolders.find(strName);
    if (aIt != m_mapGlobalSharedFolders.end())
        return true;

    return false;
}

/**
 * Calls the HGCM service to add a shared folder definition.
 *
 * @param strName      Shared folder name.
 * @param aData        Shared folder data.
 *
 * @note Must be called from under AutoVMCaller and when mpUVM != NULL!
 * @note Doesn't lock anything.
 */
HRESULT Console::i_createSharedFolder(const Utf8Str &strName, const SharedFolderData &aData)
{
    Log(("Adding shared folder '%s' -> '%s'\n", strName.c_str(), aData.m_strHostPath.c_str()));

    /*
     * Sanity checks
     */
    ComAssertRet(strName.isNotEmpty(), E_FAIL);
    ComAssertRet(aData.m_strHostPath.isNotEmpty(), E_FAIL);

    AssertReturn(mpUVM, E_FAIL);
    AssertReturn(m_pVMMDev && m_pVMMDev->isShFlActive(), E_FAIL);

    /*
     * Find out whether we should allow symbolic link creation.
     */
    Bstr bstrValue;
    HRESULT hrc = mMachine->GetExtraData(BstrFmt("VBoxInternal2/SharedFoldersEnableSymlinksCreate/%s", strName.c_str()).raw(),
                                         bstrValue.asOutParam());
    bool fSymlinksCreate = hrc == S_OK && bstrValue == "1";

    /*
     * Check whether the path is valid and exists.
     */
    char szAbsHostPath[RTPATH_MAX];
    int vrc = RTPathAbs(aData.m_strHostPath.c_str(), szAbsHostPath, sizeof(szAbsHostPath));
    if (RT_FAILURE(vrc))
        return setErrorBoth(E_INVALIDARG, vrc, tr("Invalid shared folder path: '%s' (%Rrc)"), aData.m_strHostPath.c_str(), vrc);

    /* Check whether the path is full (absolute).  ASSUMING a RTPATH_MAX of ~4K
       this also checks that the length is within bounds of a SHFLSTRING.  */
    if (RTPathCompare(aData.m_strHostPath.c_str(), szAbsHostPath) != 0)
        return setError(E_INVALIDARG, tr("Shared folder path '%s' is not absolute"), aData.m_strHostPath.c_str());

    bool const fMissing = !RTPathExists(szAbsHostPath);

    /*
     * Check the other two string lengths before converting them all to SHFLSTRINGS.
     */
    if (strName.length() >= _2K)
        return setError(E_INVALIDARG, tr("Shared folder name is too long: %zu bytes", "", strName.length()), strName.length());
    if (aData.m_strAutoMountPoint.length() >= RTPATH_MAX)
        return setError(E_INVALIDARG, tr("Shared folder mount point too long: %zu bytes", "",
                                         (int)aData.m_strAutoMountPoint.length()),
                        aData.m_strAutoMountPoint.length());

    PSHFLSTRING pHostPath       = ShflStringDupUtf8AsUtf16(aData.m_strHostPath.c_str());
    PSHFLSTRING pName           = ShflStringDupUtf8AsUtf16(strName.c_str());
    PSHFLSTRING pAutoMountPoint = ShflStringDupUtf8AsUtf16(aData.m_strAutoMountPoint.c_str());
    if (pHostPath && pName && pAutoMountPoint)
    {
        /*
         * Make a SHFL_FN_ADD_MAPPING call to tell the service about folder.
         */
        VBOXHGCMSVCPARM aParams[SHFL_CPARMS_ADD_MAPPING];
        SHFLSTRING_TO_HGMC_PARAM(&aParams[0], pHostPath);
        SHFLSTRING_TO_HGMC_PARAM(&aParams[1], pName);
        HGCMSvcSetU32(&aParams[2],
                        (aData.m_fWritable  ? SHFL_ADD_MAPPING_F_WRITABLE : 0)
                      | (aData.m_fAutoMount ? SHFL_ADD_MAPPING_F_AUTOMOUNT : 0)
                      | (fSymlinksCreate    ? SHFL_ADD_MAPPING_F_CREATE_SYMLINKS : 0)
                      | (fMissing           ? SHFL_ADD_MAPPING_F_MISSING : 0));
        SHFLSTRING_TO_HGMC_PARAM(&aParams[3], pAutoMountPoint);
        HGCMSvcSetU32(&aParams[4], SymlinkPolicy_None);
        AssertCompile(SHFL_CPARMS_ADD_MAPPING == 5);

        vrc = m_pVMMDev->hgcmHostCall("VBoxSharedFolders", SHFL_FN_ADD_MAPPING, SHFL_CPARMS_ADD_MAPPING, aParams);
        if (RT_FAILURE(vrc))
            hrc = setErrorBoth(E_FAIL, vrc, tr("Could not create a shared folder '%s' mapped to '%s' (%Rrc)"),
                               strName.c_str(), aData.m_strHostPath.c_str(), vrc);

        else if (fMissing)
            hrc = setError(E_INVALIDARG, tr("Shared folder path '%s' does not exist on the host"), aData.m_strHostPath.c_str());
        else
            hrc = S_OK;
    }
    else
        hrc = E_OUTOFMEMORY;
    RTMemFree(pAutoMountPoint);
    RTMemFree(pName);
    RTMemFree(pHostPath);
    return hrc;
}

/**
 * Calls the HGCM service to remove the shared folder definition.
 *
 * @param strName       Shared folder name.
 *
 * @note Must be called from under AutoVMCaller and when mpUVM != NULL!
 * @note Doesn't lock anything.
 */
HRESULT Console::i_removeSharedFolder(const Utf8Str &strName)
{
    ComAssertRet(strName.isNotEmpty(), E_FAIL);

    /* sanity checks */
    AssertReturn(mpUVM, E_FAIL);
    AssertReturn(m_pVMMDev && m_pVMMDev->isShFlActive(), E_FAIL);

    VBOXHGCMSVCPARM parms;
    SHFLSTRING *pMapName;
    size_t cbString;

    Log(("Removing shared folder '%s'\n", strName.c_str()));

    Bstr bstrName(strName);
    cbString = (bstrName.length() + 1) * sizeof(RTUTF16);
    if (cbString >= UINT16_MAX)
        return setError(E_INVALIDARG, tr("The name is too long"));
    pMapName = (SHFLSTRING *) RTMemAllocZ(SHFLSTRING_HEADER_SIZE + cbString);
    Assert(pMapName);
    memcpy(pMapName->String.ucs2, bstrName.raw(), cbString);

    pMapName->u16Size   = (uint16_t)cbString;
    pMapName->u16Length = (uint16_t)(cbString - sizeof(RTUTF16));

    parms.type = VBOX_HGCM_SVC_PARM_PTR;
    parms.u.pointer.addr = pMapName;
    parms.u.pointer.size = ShflStringSizeOfBuffer(pMapName);

    int vrc = m_pVMMDev->hgcmHostCall("VBoxSharedFolders", SHFL_FN_REMOVE_MAPPING, 1, &parms);
    RTMemFree(pMapName);
    if (RT_FAILURE(vrc))
        return setErrorBoth(E_FAIL, vrc, tr("Could not remove the shared folder '%s' (%Rrc)"), strName.c_str(), vrc);

    return S_OK;
}

/**
 * Retains a reference to the default cryptographic interface.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_SUPPORTED if the VM is not configured for encryption.
 * @param   ppCryptoIf          Where to store the pointer to the cryptographic interface on success.
 *
 * @note Locks this object for writing.
 */
int Console::i_retainCryptoIf(PCVBOXCRYPTOIF *ppCryptoIf)
{
    AssertReturn(ppCryptoIf != NULL, VERR_INVALID_PARAMETER);

    int vrc = VINF_SUCCESS;
    if (mhLdrModCrypto == NIL_RTLDRMOD)
    {
#ifdef VBOX_WITH_EXTPACK
        /*
         * Check that a crypto extension pack name is set and resolve it into a
         * library path.
         */
        HRESULT hrc = S_OK;
        Bstr bstrExtPack;

        ComPtr<IVirtualBox> pVirtualBox;
        hrc = mMachine->COMGETTER(Parent)(pVirtualBox.asOutParam());
        ComPtr<ISystemProperties> pSystemProperties;
        if (SUCCEEDED(hrc))
            hrc = pVirtualBox->COMGETTER(SystemProperties)(pSystemProperties.asOutParam());
        if (SUCCEEDED(hrc))
            hrc = pSystemProperties->COMGETTER(DefaultCryptoExtPack)(bstrExtPack.asOutParam());
        if (FAILED(hrc))
        {
            setErrorBoth(hrc, VERR_INVALID_PARAMETER,
                         tr("Failed to query default extension pack name for the cryptographic module"));
            return VERR_INVALID_PARAMETER;
        }

        Utf8Str strExtPack(bstrExtPack);
        if (strExtPack.isEmpty())
        {
            setError(VBOX_E_OBJECT_NOT_FOUND,
                     tr("Ńo extension pack providing a cryptographic support module could be found"));
            return VERR_NOT_FOUND;
        }

        Utf8Str strCryptoLibrary;
        vrc = mptrExtPackManager->i_getCryptoLibraryPathForExtPack(&strExtPack, &strCryptoLibrary);
        if (RT_SUCCESS(vrc))
        {
            RTERRINFOSTATIC ErrInfo;
            vrc = SUPR3HardenedLdrLoadPlugIn(strCryptoLibrary.c_str(), &mhLdrModCrypto, RTErrInfoInitStatic(&ErrInfo));
            if (RT_SUCCESS(vrc))
            {
                /* Resolve the entry point and query the pointer to the cryptographic interface. */
                PFNVBOXCRYPTOENTRY pfnCryptoEntry = NULL;
                vrc = RTLdrGetSymbol(mhLdrModCrypto, VBOX_CRYPTO_MOD_ENTRY_POINT, (void **)&pfnCryptoEntry);
                if (RT_SUCCESS(vrc))
                {
                    vrc = pfnCryptoEntry(&mpCryptoIf);
                    if (RT_FAILURE(vrc))
                        setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                     tr("Failed to query the interface callback table from the cryptographic support module '%s' from extension pack '%s'"),
                                     strCryptoLibrary.c_str(), strExtPack.c_str());
                }
                else
                    setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                                 tr("Failed to resolve the entry point for the cryptographic support module '%s' from extension pack '%s'"),
                                 strCryptoLibrary.c_str(), strExtPack.c_str());

                if (RT_FAILURE(vrc))
                {
                    RTLdrClose(mhLdrModCrypto);
                    mhLdrModCrypto = NIL_RTLDRMOD;
                }
            }
            else
                setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                             tr("Couldn't load the cryptographic support module '%s' from extension pack '%s' (error: '%s')"),
                             strCryptoLibrary.c_str(), strExtPack.c_str(), ErrInfo.Core.pszMsg);
        }
        else
            setErrorBoth(VBOX_E_IPRT_ERROR, vrc,
                         tr("Couldn't resolve the library path of the crpytographic support module for extension pack '%s'"),
                         strExtPack.c_str());
#else
        setError(VBOX_E_NOT_SUPPORTED,
                 tr("The cryptographic support module is not supported in this build because extension packs are not supported"));
        vrc = VERR_NOT_SUPPORTED;
#endif
    }

    if (RT_SUCCESS(vrc))
    {
        ASMAtomicIncU32(&mcRefsCrypto);
        *ppCryptoIf = mpCryptoIf;
    }

    return vrc;
}

/**
 * Releases the reference of the given cryptographic interface.
 *
 * @returns VBox status code.
 * @param   pCryptoIf           Pointer to the cryptographic interface to release.
 *
 * @note Locks this object for writing.
 */
int Console::i_releaseCryptoIf(PCVBOXCRYPTOIF pCryptoIf)
{
    AssertReturn(pCryptoIf == mpCryptoIf, VERR_INVALID_PARAMETER);

    ASMAtomicDecU32(&mcRefsCrypto);
    return VINF_SUCCESS;
}

/**
 * Tries to unload any loaded cryptographic support module if it is not in use currently.
 *
 * @returns COM status code.
 *
 * @note Locks this object for writing.
 */
HRESULT Console::i_unloadCryptoIfModule(void)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);

    if (mcRefsCrypto)
        return setError(E_ACCESSDENIED,
                        tr("The cryptographic support module is in use and can't be unloaded"));

    if (mhLdrModCrypto != NIL_RTLDRMOD)
    {
        int vrc = RTLdrClose(mhLdrModCrypto);
        AssertRC(vrc);
        mhLdrModCrypto = NIL_RTLDRMOD;
    }

    return S_OK;
}

/** @callback_method_impl{FNVMATSTATE}
 *
 * @note    Locks the Console object for writing.
 * @remarks The @a pUVM parameter can be NULL in one case where powerUpThread()
 *          calls after the VM was destroyed.
 */
/*static*/ DECLCALLBACK(void)
Console::i_vmstateChangeCallback(PUVM pUVM, PCVMMR3VTABLE pVMM, VMSTATE enmState, VMSTATE enmOldState, void *pvUser)
{
    LogFlowFunc(("Changing state from %s to %s (pUVM=%p)\n",
                 pVMM->pfnVMR3GetStateName(enmOldState), pVMM->pfnVMR3GetStateName(enmState), pUVM));
    RT_NOREF(pVMM);

    Console *that = static_cast<Console *>(pvUser);
    AssertReturnVoid(that);

    AutoCaller autoCaller(that);

    /* Note that we must let this method proceed even if Console::uninit() has
     * been already called. In such case this VMSTATE change is a result of:
     * 1) powerDown() called from uninit() itself, or
     * 2) VM-(guest-)initiated power off. */
    AssertReturnVoid(   autoCaller.isOk()
                     || that->getObjectState().getState() == ObjectState::InUninit);

    switch (enmState)
    {
        /*
         * The VM has terminated
         */
        case VMSTATE_OFF:
        {
#ifdef VBOX_WITH_GUEST_PROPS
            if (that->mfTurnResetIntoPowerOff)
            {
                Bstr strPowerOffReason;

                if (that->mfPowerOffCausedByReset)
                    strPowerOffReason = Bstr("Reset");
                else
                    strPowerOffReason = Bstr("PowerOff");

                that->mMachine->DeleteGuestProperty(Bstr("/VirtualBox/HostInfo/VMPowerOffReason").raw());
                that->mMachine->SetGuestProperty(Bstr("/VirtualBox/HostInfo/VMPowerOffReason").raw(),
                                                 strPowerOffReason.raw(), Bstr("RDONLYGUEST").raw());
                that->mMachine->SaveSettings();
            }
#endif

            AutoWriteLock alock(that COMMA_LOCKVAL_SRC_POS);

            if (that->mVMStateChangeCallbackDisabled)
                return;

            /* Do we still think that it is running? It may happen if this is a
             * VM-(guest-)initiated shutdown/poweroff.
             */
            if (   that->mMachineState != MachineState_Stopping
                && that->mMachineState != MachineState_Saving
                && that->mMachineState != MachineState_Restoring
                && that->mMachineState != MachineState_TeleportingIn
                && that->mMachineState != MachineState_TeleportingPausedVM
                && !that->mVMIsAlreadyPoweringOff
               )
            {
                LogFlowFunc(("VM has powered itself off but Console still thinks it is running. Notifying.\n"));

                /*
                 * Prevent powerDown() from calling VMR3PowerOff() again if this was called from
                 * the power off state change.
                 * When called from the Reset state make sure to call VMR3PowerOff() first.
                 */
                Assert(that->mVMPoweredOff == false);
                that->mVMPoweredOff = true;

                /*
                 * request a progress object from the server
                 * (this will set the machine state to Stopping on the server
                 * to block others from accessing this machine)
                 */
                ComPtr<IProgress> pProgress;
                HRESULT hrc = that->mControl->BeginPoweringDown(pProgress.asOutParam());
                AssertComRC(hrc);

                /* sync the state with the server */
                that->i_setMachineStateLocally(MachineState_Stopping);

                /*
                 * Setup task object and thread to carry out the operation
                 * asynchronously (if we call powerDown() right here but there
                 * is one or more mpUVM callers (added with addVMCaller()) we'll
                 * deadlock).
                 */
                VMPowerDownTask *pTask = NULL;
                try
                {
                    pTask = new VMPowerDownTask(that, pProgress);
                }
                catch (std::bad_alloc &)
                {
                    LogRelFunc(("E_OUTOFMEMORY creating VMPowerDownTask"));
                    hrc = E_OUTOFMEMORY;
                    break;
                }

                /*
                 * If creating a task failed, this can currently mean one of
                 * two: either Console::uninit() has been called just a ms
                 * before (so a powerDown() call is already on the way), or
                 * powerDown() itself is being already executed. Just do
                 * nothing.
                 */
                if (pTask->isOk())
                {
                    hrc = pTask->createThread();
                    pTask = NULL;
                    if (FAILED(hrc))
                        LogRelFunc(("Problem with creating thread for VMPowerDownTask.\n"));
                }
                else
                {
                    LogFlowFunc(("Console is already being uninitialized. (%Rhrc)\n", pTask->hrc()));
                    delete pTask;
                    pTask = NULL;
                    hrc = E_FAIL;
                }
            }
            break;
        }

        /* The VM has been completely destroyed.
         *
         * Note: This state change can happen at two points:
         *       1) At the end of VMR3Destroy() if it was not called from EMT.
         *       2) At the end of vmR3EmulationThread if VMR3Destroy() was
         *          called by EMT.
         */
        case VMSTATE_TERMINATED:
        {
            AutoWriteLock alock(that COMMA_LOCKVAL_SRC_POS);

            if (that->mVMStateChangeCallbackDisabled)
                break;

#ifdef VBOX_WITH_CLOUD_NET
            /*
             * We stop cloud gateway here because we may have failed to connect to it,
             * configure it, or establish a tunnel. We definitely do not want an orphaned
             * instance running in the cloud.
             */
            if (!that->mGateway.mGatewayInstanceId.isEmpty())
            {
                ComPtr<IVirtualBox> pVirtualBox;
                HRESULT hrc = that->mMachine->COMGETTER(Parent)(pVirtualBox.asOutParam());
                AssertComRC(hrc);
                if (SUCCEEDED(hrc) && !pVirtualBox.isNull())
                    stopCloudGateway(pVirtualBox, that->mGateway);
            }
#endif /* VBOX_WITH_CLOUD_NET */

            /* Terminate host interface networking. If pUVM is NULL, we've been
             * manually called from powerUpThread() either before calling
             * VMR3Create() or after VMR3Create() failed, so no need to touch
             * networking.
             */
            if (pUVM)
                that->i_powerDownHostInterfaces();

            /* From now on the machine is officially powered down or remains in
             * the Saved state.
             */
            switch (that->mMachineState)
            {
                default:
                    AssertFailed();
                    RT_FALL_THRU();
                case MachineState_Stopping:
                    /* successfully powered down */
                    that->i_setMachineState(MachineState_PoweredOff);
                    break;
                case MachineState_Saving:
                    /* successfully saved */
                    that->i_setMachineState(MachineState_Saved);
                    break;
                case MachineState_Starting:
                    /* failed to start, but be patient: set back to PoweredOff
                     * (for similarity with the below) */
                    that->i_setMachineState(MachineState_PoweredOff);
                    break;
                case MachineState_Restoring:
                    /* failed to load the saved state file, but be patient: set
                     * to AbortedSaved (to preserve the saved state file) */
                    that->i_setMachineState(MachineState_AbortedSaved);
                    break;
                case MachineState_TeleportingIn:
                    /* Teleportation failed or was canceled.  Back to powered off. */
                    that->i_setMachineState(MachineState_PoweredOff);
                    break;
                case MachineState_TeleportingPausedVM:
                    /* Successfully teleported the VM. */
                    that->i_setMachineState(MachineState_Teleported);
                    break;
            }
            break;
        }

        case VMSTATE_RESETTING:
        /** @todo shouldn't VMSTATE_RESETTING_LS be here?   */
        {
#ifdef VBOX_WITH_GUEST_PROPS
            /* Do not take any read/write locks here! */
            that->i_guestPropertiesHandleVMReset();
#endif
            break;
        }

        case VMSTATE_SOFT_RESETTING:
        case VMSTATE_SOFT_RESETTING_LS:
            /* Shouldn't do anything here! */
            break;

        case VMSTATE_SUSPENDED:
        {
            AutoWriteLock alock(that COMMA_LOCKVAL_SRC_POS);

            if (that->mVMStateChangeCallbackDisabled)
                break;

            switch (that->mMachineState)
            {
                case MachineState_Teleporting:
                    that->i_setMachineState(MachineState_TeleportingPausedVM);
                    break;

                case MachineState_LiveSnapshotting:
                    that->i_setMachineState(MachineState_OnlineSnapshotting);
                    break;

                case MachineState_TeleportingPausedVM:
                case MachineState_Saving:
                case MachineState_Restoring:
                case MachineState_Stopping:
                case MachineState_TeleportingIn:
                case MachineState_OnlineSnapshotting:
                    /* The worker thread handles the transition. */
                    break;

                case MachineState_Running:
                    that->i_setMachineState(MachineState_Paused);
                    break;

                case MachineState_Paused:
                    /* Nothing to do. */
                    break;

                default:
                    AssertMsgFailed(("%s\n", ::stringifyMachineState(that->mMachineState)));
            }
            break;
        }

        case VMSTATE_SUSPENDED_LS:
        case VMSTATE_SUSPENDED_EXT_LS:
        {
            AutoWriteLock alock(that COMMA_LOCKVAL_SRC_POS);
            if (that->mVMStateChangeCallbackDisabled)
                break;
            switch (that->mMachineState)
            {
                case MachineState_Teleporting:
                    that->i_setMachineState(MachineState_TeleportingPausedVM);
                    break;

                case MachineState_LiveSnapshotting:
                    that->i_setMachineState(MachineState_OnlineSnapshotting);
                    break;

                case MachineState_TeleportingPausedVM:
                case MachineState_Saving:
                    /* ignore */
                    break;

                default:
                    AssertMsgFailed(("%s/%s -> %s\n", ::stringifyMachineState(that->mMachineState),
                                    pVMM->pfnVMR3GetStateName(enmOldState), pVMM->pfnVMR3GetStateName(enmState) ));
                    that->i_setMachineState(MachineState_Paused);
                    break;
            }
            break;
        }

        case VMSTATE_RUNNING:
        {
            if (   enmOldState == VMSTATE_POWERING_ON
                || enmOldState == VMSTATE_RESUMING)
            {
                AutoWriteLock alock(that COMMA_LOCKVAL_SRC_POS);

                if (that->mVMStateChangeCallbackDisabled)
                    break;

                Assert(   (   (   that->mMachineState == MachineState_Starting
                               || that->mMachineState == MachineState_Paused)
                           && enmOldState == VMSTATE_POWERING_ON)
                       || (   (   that->mMachineState == MachineState_Restoring
                               || that->mMachineState == MachineState_TeleportingIn
                               || that->mMachineState == MachineState_Paused
                               || that->mMachineState == MachineState_Saving
                              )
                           && enmOldState == VMSTATE_RESUMING));

                that->i_setMachineState(MachineState_Running);
            }

            break;
        }

        case VMSTATE_RUNNING_LS:
            AssertMsg(   that->mMachineState == MachineState_LiveSnapshotting
                      || that->mMachineState == MachineState_Teleporting,
                      ("%s/%s -> %s\n", ::stringifyMachineState(that->mMachineState),
                      pVMM->pfnVMR3GetStateName(enmOldState), pVMM->pfnVMR3GetStateName(enmState) ));
            break;

        case VMSTATE_FATAL_ERROR:
        {
            AutoWriteLock alock(that COMMA_LOCKVAL_SRC_POS);

            if (that->mVMStateChangeCallbackDisabled)
                break;

            /* Fatal errors are only for running VMs. */
            Assert(Global::IsOnline(that->mMachineState));

            /* Note! 'Pause' is used here in want of something better.  There
             *       are currently only two places where fatal errors might be
             *       raised, so it is not worth adding a new externally
             *       visible state for this yet.  */
            that->i_setMachineState(MachineState_Paused);
            break;
        }

        case VMSTATE_GURU_MEDITATION:
        {
            AutoWriteLock alock(that COMMA_LOCKVAL_SRC_POS);

            if (that->mVMStateChangeCallbackDisabled)
                break;

            /* Guru are only for running VMs */
            Assert(Global::IsOnline(that->mMachineState));

            that->i_setMachineState(MachineState_Stuck);
            break;
        }

        case VMSTATE_CREATED:
        {
            /*
             * We have to set the secret key helper interface for the VD drivers to
             * get notified about missing keys.
             */
            that->i_initSecretKeyIfOnAllAttachments();
            break;
        }

        default: /* shut up gcc */
            break;
    }
}

/**
 * Changes the clipboard mode.
 *
 * @returns VBox status code.
 * @param   aClipboardMode  new clipboard mode.
 */
int Console::i_changeClipboardMode(ClipboardMode_T aClipboardMode)
{
#ifdef VBOX_WITH_SHARED_CLIPBOARD
    VMMDev *pVMMDev = m_pVMMDev;
    AssertPtrReturn(pVMMDev, VERR_INVALID_POINTER);

    VBOXHGCMSVCPARM parm;
    parm.type = VBOX_HGCM_SVC_PARM_32BIT;

    switch (aClipboardMode)
    {
        default:
        case ClipboardMode_Disabled:
            LogRel(("Shared Clipboard: Mode: Off\n"));
            parm.u.uint32 = VBOX_SHCL_MODE_OFF;
            break;
        case ClipboardMode_GuestToHost:
            LogRel(("Shared Clipboard: Mode: Guest to Host\n"));
            parm.u.uint32 = VBOX_SHCL_MODE_GUEST_TO_HOST;
            break;
        case ClipboardMode_HostToGuest:
            LogRel(("Shared Clipboard: Mode: Host to Guest\n"));
            parm.u.uint32 = VBOX_SHCL_MODE_HOST_TO_GUEST;
            break;
        case ClipboardMode_Bidirectional:
            LogRel(("Shared Clipboard: Mode: Bidirectional\n"));
            parm.u.uint32 = VBOX_SHCL_MODE_BIDIRECTIONAL;
            break;
    }

    int vrc = pVMMDev->hgcmHostCall("VBoxSharedClipboard", VBOX_SHCL_HOST_FN_SET_MODE, 1, &parm);
    if (RT_FAILURE(vrc))
        LogRel(("Shared Clipboard: Error changing mode: %Rrc\n", vrc));

    return vrc;
#else
    RT_NOREF(aClipboardMode);
    return VERR_NOT_IMPLEMENTED;
#endif
}

/**
 * Changes the clipboard file transfer mode.
 *
 * @returns VBox status code.
 * @param   aEnabled    Whether clipboard file transfers are enabled or not.
 */
int Console::i_changeClipboardFileTransferMode(bool aEnabled)
{
#ifdef VBOX_WITH_SHARED_CLIPBOARD_TRANSFERS
    VMMDev *pVMMDev = m_pVMMDev;
    AssertPtrReturn(pVMMDev, VERR_INVALID_POINTER);

    VBOXHGCMSVCPARM parm;
    RT_ZERO(parm);

    parm.type     = VBOX_HGCM_SVC_PARM_32BIT;
    parm.u.uint32 = aEnabled ? VBOX_SHCL_TRANSFER_MODE_F_ENABLED : VBOX_SHCL_TRANSFER_MODE_F_NONE;

    int vrc = pVMMDev->hgcmHostCall("VBoxSharedClipboard", VBOX_SHCL_HOST_FN_SET_TRANSFER_MODE, 1 /* cParms */, &parm);
    if (RT_FAILURE(vrc))
        LogRel(("Shared Clipboard: Error changing file transfer mode: %Rrc\n", vrc));

    return vrc;
#else
    RT_NOREF(aEnabled);
    return VERR_NOT_IMPLEMENTED;
#endif
}

/**
 * Changes the drag and drop mode.
 *
 * @param aDnDMode  new drag and drop mode.
 */
int Console::i_changeDnDMode(DnDMode_T aDnDMode)
{
    VMMDev *pVMMDev = m_pVMMDev;
    AssertPtrReturn(pVMMDev, VERR_INVALID_POINTER);

    VBOXHGCMSVCPARM parm;
    RT_ZERO(parm);
    parm.type = VBOX_HGCM_SVC_PARM_32BIT;

    switch (aDnDMode)
    {
        default:
        case DnDMode_Disabled:
            LogRel(("Drag and drop mode: Off\n"));
            parm.u.uint32 = VBOX_DRAG_AND_DROP_MODE_OFF;
            break;
        case DnDMode_GuestToHost:
            LogRel(("Drag and drop mode: Guest to Host\n"));
            parm.u.uint32 = VBOX_DRAG_AND_DROP_MODE_GUEST_TO_HOST;
            break;
        case DnDMode_HostToGuest:
            LogRel(("Drag and drop mode: Host to Guest\n"));
            parm.u.uint32 = VBOX_DRAG_AND_DROP_MODE_HOST_TO_GUEST;
            break;
        case DnDMode_Bidirectional:
            LogRel(("Drag and drop mode: Bidirectional\n"));
            parm.u.uint32 = VBOX_DRAG_AND_DROP_MODE_BIDIRECTIONAL;
            break;
    }

    int vrc = pVMMDev->hgcmHostCall("VBoxDragAndDropSvc", DragAndDropSvc::HOST_DND_FN_SET_MODE, 1 /* cParms */, &parm);
    if (RT_FAILURE(vrc))
        LogRel(("Error changing drag and drop mode: %Rrc\n", vrc));

    return vrc;
}

#ifdef VBOX_WITH_USB
/**
 * @interface_method_impl{REMOTEUSBIF,pfnQueryRemoteUsbBackend}
 */
/*static*/ DECLCALLBACK(PREMOTEUSBCALLBACK)
Console::i_usbQueryRemoteUsbBackend(void *pvUser, PCRTUUID pUuid, uint32_t idClient)
{
    Console *pConsole = (Console *)pvUser;

    AutoReadLock thatLock(pConsole COMMA_LOCKVAL_SRC_POS);

    Guid const uuid(*pUuid);
    return (PREMOTEUSBCALLBACK)pConsole->i_consoleVRDPServer()->USBBackendRequestPointer(idClient, &uuid);
}


/**
 * Sends a request to VMM to attach the given host device.
 * After this method succeeds, the attached device will appear in the
 * mUSBDevices collection.
 *
 * @param aHostDevice  device to attach
 *
 * @note Synchronously calls EMT.
 */
HRESULT Console::i_attachUSBDevice(IUSBDevice *aHostDevice, ULONG aMaskedIfs, const Utf8Str &aCaptureFilename)
{
    AssertReturn(aHostDevice, E_FAIL);
    AssertReturn(!isWriteLockOnCurrentThread(), E_FAIL);

    HRESULT hrc;

    /*
     * Get the address and the Uuid, and call the pfnCreateProxyDevice roothub
     * method in EMT (using usbAttachCallback()).
     */
    Bstr bstrAddress;
    hrc = aHostDevice->COMGETTER(Address)(bstrAddress.asOutParam());
    ComAssertComRCRetRC(hrc);
    Utf8Str const Address(bstrAddress);

    Bstr id;
    hrc = aHostDevice->COMGETTER(Id)(id.asOutParam());
    ComAssertComRCRetRC(hrc);
    Guid const uuid(id);

    BOOL fRemote = FALSE;
    hrc = aHostDevice->COMGETTER(Remote)(&fRemote);
    ComAssertComRCRetRC(hrc);

    Bstr bstrBackend;
    hrc = aHostDevice->COMGETTER(Backend)(bstrBackend.asOutParam());
    ComAssertComRCRetRC(hrc);
    Utf8Str const strBackend(bstrBackend);

    /* Get the VM handle. */
    SafeVMPtr ptrVM(this);
    if (!ptrVM.isOk())
        return ptrVM.hrc();

    LogFlowThisFunc(("Proxying USB device '%s' {%RTuuid}...\n", Address.c_str(), uuid.raw()));

    PCFGMNODE pRemoteCfg = NULL;
    if (fRemote)
    {
        RemoteUSBDevice *pRemoteUSBDevice = static_cast<RemoteUSBDevice *>(aHostDevice);

        pRemoteCfg = mpVMM->pfnCFGMR3CreateTree(ptrVM.rawUVM());
        if (pRemoteCfg)
        {
            int vrc = mpVMM->pfnCFGMR3InsertInteger(pRemoteCfg, "ClientId", pRemoteUSBDevice->clientId());
            if (RT_FAILURE(vrc))
            {
                mpVMM->pfnCFGMR3DestroyTree(pRemoteCfg);
                return setErrorBoth(E_FAIL, vrc, tr("Failed to create configuration for USB device."));
            }
        }
        else
            return setErrorBoth(E_OUTOFMEMORY, VERR_NO_MEMORY, tr("Failed to allocate config tree for USB device."));
    }

    USBConnectionSpeed_T enmSpeed;
    hrc = aHostDevice->COMGETTER(Speed)(&enmSpeed);
    AssertComRCReturnRC(hrc);

    int vrc = ptrVM.vtable()->pfnVMR3ReqCallWaitU(ptrVM.rawUVM(), 0 /* idDstCpu (saved state, see #6232) */,
                                                  (PFNRT)i_usbAttachCallback, 11 | VMREQ_F_EXTRA_ARGS_ALL_PTRS,
                                                  this, ptrVM.rawUVM(), ptrVM.vtable(), aHostDevice, uuid.raw(),
                                                  strBackend.c_str(), Address.c_str(), pRemoteCfg, /* extra arg (ptrs only): */
                                                  &enmSpeed, &aMaskedIfs, aCaptureFilename.isEmpty()
                                                                        ? (const char *)NULL : aCaptureFilename.c_str());
    if (RT_SUCCESS(vrc))
    {
        /* Create a OUSBDevice and add it to the device list */
        ComObjPtr<OUSBDevice> pUSBDevice;
        pUSBDevice.createObject();
        hrc = pUSBDevice->init(aHostDevice);
        AssertComRC(hrc);

        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        mUSBDevices.push_back(pUSBDevice);
        LogFlowFunc(("Attached device {%RTuuid}\n", pUSBDevice->i_id().raw()));

        /* notify callbacks */
        alock.release();
        i_onUSBDeviceStateChange(pUSBDevice, true /* aAttached */, NULL);
    }
    else
    {
        Log1WarningThisFunc(("Failed to create proxy device for '%s' {%RTuuid} (%Rrc)\n", Address.c_str(), uuid.raw(), vrc));
        switch (vrc)
        {
            case VERR_VUSB_NO_PORTS:
                hrc = setErrorBoth(E_FAIL, vrc, tr("Failed to attach the USB device. (No available ports on the USB controller)."));
                break;
            case VERR_VUSB_USBFS_PERMISSION:
                hrc = setErrorBoth(E_FAIL, vrc, tr("Not permitted to open the USB device, check usbfs options"));
                break;
            default:
                hrc = setErrorBoth(E_FAIL, vrc, tr("Failed to create a proxy device for the USB device. (Error: %Rrc)"), vrc);
                break;
        }
    }

    return hrc;
}

/**
 * USB device attach callback used by AttachUSBDevice().
 * Note that AttachUSBDevice() doesn't return until this callback is executed,
 * so we don't use AutoCaller and don't care about reference counters of
 * interface pointers passed in.
 *
 * @thread EMT
 * @note Locks the console object for writing.
 */
//static
DECLCALLBACK(int)
Console::i_usbAttachCallback(Console *that, PUVM pUVM, PCVMMR3VTABLE pVMM, IUSBDevice *aHostDevice, PCRTUUID aUuid,
                             const char *pszBackend, const char *aAddress, PCFGMNODE pRemoteCfg,
                             USBConnectionSpeed_T *penmSpeed, ULONG *pfMaskedIfs, const char *pszCaptureFilename)
{
    RT_NOREF(aHostDevice);
    LogFlowFuncEnter();
    LogFlowFunc(("that={%p} aUuid={%RTuuid}\n", that, aUuid));

    AssertReturn(that && aUuid, VERR_INVALID_PARAMETER);
    AssertReturn(!that->isWriteLockOnCurrentThread(), VERR_GENERAL_FAILURE);

    VUSBSPEED enmSpeed = VUSB_SPEED_UNKNOWN;
    switch (*penmSpeed)
    {
        case USBConnectionSpeed_Low:        enmSpeed = VUSB_SPEED_LOW;          break;
        case USBConnectionSpeed_Full:       enmSpeed = VUSB_SPEED_FULL;         break;
        case USBConnectionSpeed_High:       enmSpeed = VUSB_SPEED_HIGH;         break;
        case USBConnectionSpeed_Super:      enmSpeed = VUSB_SPEED_SUPER;        break;
        case USBConnectionSpeed_SuperPlus:  enmSpeed = VUSB_SPEED_SUPERPLUS;    break;
        default:                            AssertFailed();                     break;
    }

    int vrc = pVMM->pfnPDMR3UsbCreateProxyDevice(pUVM, aUuid, pszBackend, aAddress, pRemoteCfg,
                                                 enmSpeed, *pfMaskedIfs, pszCaptureFilename);
    LogFlowFunc(("vrc=%Rrc\n", vrc));
    LogFlowFuncLeave();
    return vrc;
}

/**
 * Sends a request to VMM to detach the given host device.  After this method
 * succeeds, the detached device will disappear from the mUSBDevices
 * collection.
 *
 * @param aHostDevice  device to attach
 *
 * @note Synchronously calls EMT.
 */
HRESULT Console::i_detachUSBDevice(const ComObjPtr<OUSBDevice> &aHostDevice)
{
    AssertReturn(!isWriteLockOnCurrentThread(), E_FAIL);

    /* Get the VM handle. */
    SafeVMPtr ptrVM(this);
    if (!ptrVM.isOk())
        return ptrVM.hrc();

    /* if the device is attached, then there must at least one USB hub. */
    AssertReturn(ptrVM.vtable()->pfnPDMR3UsbHasHub(ptrVM.rawUVM()), E_FAIL);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    LogFlowThisFunc(("Detaching USB proxy device {%RTuuid}...\n", aHostDevice->i_id().raw()));

    /*
     * If this was a remote device, release the backend pointer.
     * The pointer was requested in usbAttachCallback.
     */
    BOOL fRemote = FALSE;

    HRESULT hrc2 = aHostDevice->COMGETTER(Remote)(&fRemote);
    if (FAILED(hrc2))
        i_setErrorStatic(hrc2, "GetRemote() failed");

    PCRTUUID pUuid = aHostDevice->i_id().raw();
    if (fRemote)
    {
        Guid guid(*pUuid);
        i_consoleVRDPServer()->USBBackendReleasePointer(&guid);
    }

    alock.release();
    int vrc = ptrVM.vtable()->pfnVMR3ReqCallWaitU(ptrVM.rawUVM(), 0 /* idDstCpu (saved state, see #6232) */,
                                                  (PFNRT)i_usbDetachCallback, 4, this, ptrVM.rawUVM(), ptrVM.vtable(), pUuid);
    if (RT_SUCCESS(vrc))
    {
        LogFlowFunc(("Detached device {%RTuuid}\n", pUuid));

        /* notify callbacks */
        i_onUSBDeviceStateChange(aHostDevice, false /* aAttached */, NULL);
    }

    ComAssertRCRet(vrc, E_FAIL);

    return S_OK;
}

/**
 * USB device detach callback used by DetachUSBDevice().
 *
 * Note that DetachUSBDevice() doesn't return until this callback is executed,
 * so we don't use AutoCaller and don't care about reference counters of
 * interface pointers passed in.
 *
 * @thread EMT
 */
//static
DECLCALLBACK(int)
Console::i_usbDetachCallback(Console *that, PUVM pUVM, PCVMMR3VTABLE pVMM, PCRTUUID aUuid)
{
    LogFlowFuncEnter();
    LogFlowFunc(("that={%p} aUuid={%RTuuid}\n", that, aUuid));

    AssertReturn(that && aUuid, VERR_INVALID_PARAMETER);
    AssertReturn(!that->isWriteLockOnCurrentThread(), VERR_GENERAL_FAILURE);

    int vrc = pVMM->pfnPDMR3UsbDetachDevice(pUVM, aUuid);

    LogFlowFunc(("vrc=%Rrc\n", vrc));
    LogFlowFuncLeave();
    return vrc;
}
#endif /* VBOX_WITH_USB */

/* Note: FreeBSD needs this whether netflt is used or not. */
#if ((defined(RT_OS_LINUX) && !defined(VBOX_WITH_NETFLT)) || defined(RT_OS_FREEBSD))

/**
 * Helper function to handle host interface device creation and attachment.
 *
 * @param   networkAdapter the network adapter which attachment should be reset
 * @return  COM status code
 *
 * @note The caller must lock this object for writing.
 *
 * @todo Move this back into the driver!
 */
HRESULT Console::i_attachToTapInterface(INetworkAdapter *networkAdapter)
{
    LogFlowThisFunc(("\n"));
    /* sanity check */
    AssertReturn(isWriteLockOnCurrentThread(), E_FAIL);

# ifdef VBOX_STRICT
    /* paranoia */
    NetworkAttachmentType_T attachment;
    networkAdapter->COMGETTER(AttachmentType)(&attachment);
    Assert(attachment == NetworkAttachmentType_Bridged);
# endif /* VBOX_STRICT */

    ULONG slot = 0;
    HRESULT hrc = networkAdapter->COMGETTER(Slot)(&slot);
    AssertComRCReturnRC(hrc);

# ifdef RT_OS_LINUX
    /*
     * Allocate a host interface device
     */
    int vrc = RTFileOpen(&maTapFD[slot], "/dev/net/tun",
                         RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_NONE | RTFILE_O_INHERIT);
    if (RT_SUCCESS(vrc))
    {
        /*
         * Set/obtain the tap interface.
         */
        struct ifreq IfReq;
        RT_ZERO(IfReq);
        /* The name of the TAP interface we are using */
        Bstr tapDeviceName;
        hrc = networkAdapter->COMGETTER(BridgedInterface)(tapDeviceName.asOutParam());
        if (FAILED(hrc))
            tapDeviceName.setNull(); /* Is this necessary? */
        if (tapDeviceName.isEmpty())
        {
            LogRel(("No TAP device name was supplied.\n"));
            hrc = setError(E_FAIL, tr("No TAP device name was supplied for the host networking interface"));
        }

        if (SUCCEEDED(hrc))
        {
            /* If we are using a static TAP device then try to open it. */
            Utf8Str str(tapDeviceName);
            RTStrCopy(IfReq.ifr_name, sizeof(IfReq.ifr_name), str.c_str()); /** @todo bitch about names which are too long... */
            IfReq.ifr_flags = IFF_TAP | IFF_NO_PI;
            vrc = ioctl(RTFileToNative(maTapFD[slot]), TUNSETIFF, &IfReq);
            if (vrc != 0)
            {
                LogRel(("Failed to open the host network interface %ls\n", tapDeviceName.raw()));
                hrc = setErrorBoth(E_FAIL, vrc, tr("Failed to open the host network interface %ls"), tapDeviceName.raw());
            }
        }
        if (SUCCEEDED(hrc))
        {
            /*
             * Make it pollable.
             */
            if (fcntl(RTFileToNative(maTapFD[slot]), F_SETFL, O_NONBLOCK) != -1)
            {
                Log(("i_attachToTapInterface: %RTfile %ls\n", maTapFD[slot], tapDeviceName.raw()));
                /*
                 * Here is the right place to communicate the TAP file descriptor and
                 * the host interface name to the server if/when it becomes really
                 * necessary.
                 */
                maTAPDeviceName[slot] = tapDeviceName;
                vrc = VINF_SUCCESS;
            }
            else
            {
                int iErr = errno;

                LogRel(("Configuration error: Failed to configure /dev/net/tun non blocking. Error: %s\n", strerror(iErr)));
                vrc = VERR_HOSTIF_BLOCKING;
                hrc = setErrorBoth(E_FAIL, vrc, tr("could not set up the host networking device for non blocking access: %s"),
                                   strerror(errno));
            }
        }
    }
    else
    {
        LogRel(("Configuration error: Failed to open /dev/net/tun vrc=%Rrc\n", vrc));
        switch (vrc)
        {
            case VERR_ACCESS_DENIED:
                /* will be handled by our caller */
                hrc = E_ACCESSDENIED;
                break;
            default:
                hrc = setErrorBoth(E_FAIL, vrc, tr("Could not set up the host networking device: %Rrc"), vrc);
                break;
        }
    }

# elif defined(RT_OS_FREEBSD)
    /*
     * Set/obtain the tap interface.
     */
    /* The name of the TAP interface we are using */
    Bstr tapDeviceName;
    hrc = networkAdapter->COMGETTER(BridgedInterface)(tapDeviceName.asOutParam());
    if (FAILED(hrc))
        tapDeviceName.setNull(); /* Is this necessary? */
    if (tapDeviceName.isEmpty())
    {
        LogRel(("No TAP device name was supplied.\n"));
        hrc = setError(E_FAIL, tr("No TAP device name was supplied for the host networking interface"));
    }
    char szTapdev[1024] = "/dev/";
    /* If we are using a static TAP device then try to open it. */
    Utf8Str str(tapDeviceName);
    if (str.length() + strlen(szTapdev) <= sizeof(szTapdev))
        strcat(szTapdev, str.c_str());
    else
        memcpy(szTapdev + strlen(szTapdev), str.c_str(),
               sizeof(szTapdev) - strlen(szTapdev) - 1); /** @todo bitch about names which are too long... */
    int vrc = RTFileOpen(&maTapFD[slot], szTapdev,
                         RTFILE_O_READWRITE | RTFILE_O_OPEN | RTFILE_O_DENY_NONE | RTFILE_O_INHERIT | RTFILE_O_NON_BLOCK);

    if (RT_SUCCESS(vrc))
        maTAPDeviceName[slot] = tapDeviceName;
    else
    {
        switch (vrc)
        {
            case VERR_ACCESS_DENIED:
                /* will be handled by our caller */
                hrc = E_ACCESSDENIED;
                break;
            default:
                hrc = setErrorBoth(E_FAIL, vrc, tr("Failed to open the host network interface %ls"), tapDeviceName.raw());
                break;
        }
    }
# else
#  error "huh?"
# endif
    /* in case of failure, cleanup. */
    if (RT_FAILURE(vrc) && SUCCEEDED(hrc))
    {
        LogRel(("General failure attaching to host interface\n"));
        hrc = setErrorBoth(E_FAIL, vrc, tr("General failure attaching to host interface"));
    }
    LogFlowThisFunc(("hrc=%Rhrc\n", hrc));
    return hrc;
}


/**
 * Helper function to handle detachment from a host interface
 *
 * @param   networkAdapter the network adapter which attachment should be reset
 * @return  COM status code
 *
 * @note The caller must lock this object for writing.
 *
 * @todo Move this back into the driver!
 */
HRESULT Console::i_detachFromTapInterface(INetworkAdapter *networkAdapter)
{
    /* sanity check */
    LogFlowThisFunc(("\n"));
    AssertReturn(isWriteLockOnCurrentThread(), E_FAIL);

# ifdef VBOX_STRICT
    /* paranoia */
    NetworkAttachmentType_T attachment;
    networkAdapter->COMGETTER(AttachmentType)(&attachment);
    Assert(attachment == NetworkAttachmentType_Bridged);
# endif /* VBOX_STRICT */

    ULONG slot = 0;
    HRESULT hrc = networkAdapter->COMGETTER(Slot)(&slot);
    AssertComRCReturnRC(hrc);

    /* is there an open TAP device? */
    if (maTapFD[slot] != NIL_RTFILE)
    {
        /*
         * Close the file handle.
         */
        Bstr tapDeviceName, tapTerminateApplication;
        bool isStatic = true;
        hrc = networkAdapter->COMGETTER(BridgedInterface)(tapDeviceName.asOutParam());
        if (FAILED(hrc) || tapDeviceName.isEmpty())
        {
            /* If the name is empty, this is a dynamic TAP device, so close it now,
               so that the termination script can remove the interface. Otherwise we still
               need the FD to pass to the termination script. */
            isStatic = false;
            int vrc = RTFileClose(maTapFD[slot]);
            AssertRC(vrc);
            maTapFD[slot] = NIL_RTFILE;
        }
        if (isStatic)
        {
            /* If we are using a static TAP device, we close it now, after having called the
               termination script. */
            int vrc = RTFileClose(maTapFD[slot]);
            AssertRC(vrc);
        }
        /* the TAP device name and handle are no longer valid */
        maTapFD[slot] = NIL_RTFILE;
        maTAPDeviceName[slot] = "";
    }
    LogFlowThisFunc(("returning %Rhrc\n", hrc));
    return hrc;
}

#endif /* (RT_OS_LINUX || RT_OS_FREEBSD) && !VBOX_WITH_NETFLT */

/**
 * Called at power down to terminate host interface networking.
 *
 * @note The caller must lock this object for writing.
 */
HRESULT Console::i_powerDownHostInterfaces()
{
    LogFlowThisFunc(("\n"));

    /* sanity check */
    AssertReturn(isWriteLockOnCurrentThread(), E_FAIL);

#if (defined(RT_OS_LINUX) || defined(RT_OS_FREEBSD)) && !defined(VBOX_WITH_NETFLT)
    /*
     * Host TAP interface termination handling.
     */
    ComPtr<IVirtualBox> pVirtualBox;
    mMachine->COMGETTER(Parent)(pVirtualBox.asOutParam());

    ComPtr<IPlatform> pPlatform;
    HRESULT hrc = mMachine->COMGETTER(Platform)(pPlatform.asOutParam());
    AssertComRC(hrc);

    PlatformArchitecture_T platformArch;
    hrc = pPlatform->COMGETTER(Architecture)(&platformArch);
    AssertComRC(hrc);

    ComPtr<IPlatformProperties> pPlatformProperties;
    hrc = pVirtualBox->GetPlatformProperties(platformArch, pPlatformProperties.asOutParam());
    AssertComRC(hrc);

    ChipsetType_T chipsetType = ChipsetType_PIIX3;
    pPlatform->COMGETTER(ChipsetType)(&chipsetType);
    AssertComRC(hrc);

    ULONG maxNetworkAdapters = 0;
    hrc = pPlatformProperties->GetMaxNetworkAdapters(chipsetType, &maxNetworkAdapters);
    AssertComRC(hrc);

    for (ULONG slot = 0; slot < maxNetworkAdapters; slot++)
    {
        ComPtr<INetworkAdapter> pNetworkAdapter;
        hrc = mMachine->GetNetworkAdapter(slot, pNetworkAdapter.asOutParam());
        if (FAILED(hrc)) break;

        BOOL enabled = FALSE;
        pNetworkAdapter->COMGETTER(Enabled)(&enabled);
        if (!enabled)
            continue;

        NetworkAttachmentType_T attachment;
        pNetworkAdapter->COMGETTER(AttachmentType)(&attachment);
        if (attachment == NetworkAttachmentType_Bridged)
        {
            HRESULT hrc2 = i_detachFromTapInterface(pNetworkAdapter);
            if (FAILED(hrc2) && SUCCEEDED(hrc))
                hrc = hrc2;
        }
    }

    return hrc;

#else
    /* Nothing to do here. */
    return S_OK;
#endif
}


/**
 * Process callback handler for VMR3LoadFromFile, VMR3LoadFromStream, VMR3Save
 * and VMR3Teleport.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   uPercent    Completion percentage (0-100).
 * @param   pvUser      Pointer to an IProgress instance.
 * @return  VINF_SUCCESS.
 */
/*static*/
DECLCALLBACK(int) Console::i_stateProgressCallback(PUVM pUVM, unsigned uPercent, void *pvUser)
{
    IProgress *pProgress = static_cast<IProgress *>(pvUser);

    /* update the progress object */
    if (pProgress)
    {
        ComPtr<IInternalProgressControl> pProgressControl(pProgress);
        AssertReturn(!!pProgressControl, VERR_INVALID_PARAMETER);
        pProgressControl->SetCurrentOperationProgress(uPercent);
    }

    NOREF(pUVM);
    return VINF_SUCCESS;
}

/**
 * @copydoc FNVMATERROR
 *
 * @remarks Might be some tiny serialization concerns with access to the string
 *          object here...
 */
/*static*/ DECLCALLBACK(void)
Console::i_genericVMSetErrorCallback(PUVM pUVM, void *pvUser, int vrc, RT_SRC_POS_DECL, const char *pszFormat, va_list args)
{
    RT_SRC_POS_NOREF();
    Utf8Str *pErrorText = (Utf8Str *)pvUser;
    AssertPtr(pErrorText);

    /* We ignore RT_SRC_POS_DECL arguments to avoid confusion of end-users. */
    va_list va2;
    va_copy(va2, args);

    /* Append to any the existing error message. */
    try
    {
        if (pErrorText->length())
            pErrorText->appendPrintf(".\n%N (%Rrc)", pszFormat, &va2, vrc, vrc);
        else
            pErrorText->printf("%N (%Rrc)", pszFormat, &va2, vrc, vrc);
    }
    catch (std::bad_alloc &)
    {
    }

    va_end(va2);

    NOREF(pUVM);
}

/**
 * VM runtime error callback function (FNVMATRUNTIMEERROR).
 *
 * See VMSetRuntimeError for the detailed description of parameters.
 *
 * @param   pUVM            The user mode VM handle.  Ignored, so passing NULL
 *                          is fine.
 * @param   pvUser          The user argument, pointer to the Console instance.
 * @param   fFlags          The action flags. See VMSETRTERR_FLAGS_*.
 * @param   pszErrorId      Error ID string.
 * @param   pszFormat       Error message format string.
 * @param   va              Error message arguments.
 * @thread EMT.
 */
/* static */ DECLCALLBACK(void)
Console::i_atVMRuntimeErrorCallback(PUVM pUVM, void *pvUser, uint32_t fFlags,
                                    const char *pszErrorId, const char *pszFormat, va_list va)
{
    bool const fFatal = !!(fFlags & VMSETRTERR_FLAGS_FATAL);
    LogFlowFuncEnter();

    Console *that = static_cast<Console *>(pvUser);
    AssertReturnVoid(that);

    Utf8Str message(pszFormat, va);

    LogRel(("Console: VM runtime error: fatal=%RTbool, errorID=%s message=\"%s\"\n", fFatal, pszErrorId, message.c_str()));
    try
    {
        that->i_onRuntimeError(BOOL(fFatal), Bstr(pszErrorId).raw(), Bstr(message).raw());
    }
    catch (std::bad_alloc &)
    {
    }
    LogFlowFuncLeave(); NOREF(pUVM);
}

/**
 * Captures USB devices that match filters of the VM.
 * Called at VM startup.
 *
 * @param   pUVM    The VM handle.
 */
HRESULT Console::i_captureUSBDevices(PUVM pUVM)
{
    RT_NOREF(pUVM);
    LogFlowThisFunc(("\n"));

    /* sanity check */
    AssertReturn(!isWriteLockOnCurrentThread(), E_FAIL);
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /* If the machine has a USB controller, ask the USB proxy service to
     * capture devices */
    if (mfVMHasUsbController)
    {
        /* release the lock before calling Host in VBoxSVC since Host may call
         * us back from under its lock (e.g. onUSBDeviceAttach()) which would
         * produce an inter-process dead-lock otherwise. */
        alock.release();

        HRESULT hrc = mControl->AutoCaptureUSBDevices();
        ComAssertComRCRetRC(hrc);
    }

    return S_OK;
}


/**
 * Detach all USB device which are attached to the VM for the
 * purpose of clean up and such like.
 */
void Console::i_detachAllUSBDevices(bool aDone)
{
    LogFlowThisFunc(("aDone=%RTbool\n", aDone));

    /* sanity check */
    AssertReturnVoid(!isWriteLockOnCurrentThread());
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    mUSBDevices.clear();

    /* release the lock before calling Host in VBoxSVC since Host may call
     * us back from under its lock (e.g. onUSBDeviceAttach()) which would
     * produce an inter-process dead-lock otherwise. */
    alock.release();

    mControl->DetachAllUSBDevices(aDone);
}

/* Make sure that the string is null-terminated and its size is less than cchMax bytes.
 * Replace invalid UTF8 bytes with '?'.
 */
static int validateUtf8String(char *psz, size_t cchMax)
{
    for (;;)
    {
        RTUNICP Cp;
        int vrc = RTStrGetCpNEx((const char **)&psz, &cchMax, &Cp);
        if (RT_SUCCESS(vrc))
        {
            if (!Cp)
                break;
        }
        else
        {
            if (!cchMax)
                return VERR_END_OF_STRING;

            psz[-1] = '?';
        }
    }
    return VINF_SUCCESS;
}

static int validateRemoteUSBDeviceDesc(VRDEUSBDEVICEDESC const *e, uint32_t cbRemaining, bool fDescExt)
{
    uint32_t const cbDesc = fDescExt ? sizeof(VRDEUSBDEVICEDESCEXT) : sizeof(VRDEUSBDEVICEDESC);
    if (cbDesc > cbRemaining)
        return VERR_INVALID_PARAMETER;

    if (   e->oNext         >  cbRemaining /* It is OK for oNext to point to the end of buffer. */
        || e->oManufacturer >= cbRemaining
        || e->oProduct      >= cbRemaining
        || e->oSerialNumber >= cbRemaining)
        return VERR_INVALID_PARAMETER;

    int vrc;
    if (e->oManufacturer)
    {
        vrc = validateUtf8String((char *)e + e->oManufacturer, cbRemaining - e->oManufacturer);
        if (RT_FAILURE(vrc))
            return VERR_INVALID_PARAMETER;
    }
    if (e->oProduct)
    {
        vrc = validateUtf8String((char *)e + e->oProduct, cbRemaining - e->oProduct);
        if (RT_FAILURE(vrc))
            return VERR_INVALID_PARAMETER;
    }
    if (e->oSerialNumber)
    {
        vrc = validateUtf8String((char *)e + e->oSerialNumber, cbRemaining - e->oSerialNumber);
        if (RT_FAILURE(vrc))
            return VERR_INVALID_PARAMETER;
    }

    return VINF_SUCCESS;
}

/**
 * @note Locks this object for writing.
 */
void Console::i_processRemoteUSBDevices(uint32_t u32ClientId, VRDEUSBDEVICEDESC *pDevList, uint32_t cbDevList, bool fDescExt)
{
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("u32ClientId = %d, pDevList=%p, cbDevList = %d, fDescExt = %d\n",
                     u32ClientId, pDevList, cbDevList, fDescExt));

    AutoCaller autoCaller(this);
    if (!autoCaller.isOk())
    {
        /* Console has been already uninitialized, deny request */
        AssertMsgFailed(("Console is already uninitialized\n"));
        LogFlowThisFunc(("Console is already uninitialized\n"));
        LogFlowThisFuncLeave();
        return;
    }

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    /*
     * Mark all existing remote USB devices as dirty.
     */
    for (RemoteUSBDeviceList::iterator it = mRemoteUSBDevices.begin();
         it != mRemoteUSBDevices.end();
         ++it)
    {
        (*it)->dirty(true);
    }

    /*
     * Process the pDevList and add devices those are not already in the mRemoteUSBDevices list.
     */
    VRDEUSBDEVICEDESC *e = pDevList;
    uint32_t cbRemaining = cbDevList;

    /* The cbRemaining condition must be checked first, because the function can
     * receive pDevList = NULL and cbDevList = 0 on client disconnect.
     */
    while (cbRemaining >= sizeof(e->oNext) && e->oNext)
    {
        int const vrc = validateRemoteUSBDeviceDesc(e, cbRemaining, fDescExt);
        if (RT_FAILURE(vrc))
            break; /* Consider the rest of the list invalid too. */

        LogFlowThisFunc(("vendor %04x, product %04x, name = %s\n",
                         e->idVendor, e->idProduct, e->oProduct ? (char *)e + e->oProduct : ""));

        bool fNewDevice = true;

        for (RemoteUSBDeviceList::iterator it = mRemoteUSBDevices.begin();
             it != mRemoteUSBDevices.end();
             ++it)
        {
            if (   (*it)->devId() == e->id
                && (*it)->clientId() == u32ClientId)
            {
               /* The device is already in the list. */
               (*it)->dirty(false);
               fNewDevice = false;
               break;
            }
        }

        if (fNewDevice)
        {
            LogRel(("Remote USB: ++++ Vendor %04X. Product %04X. Name = [%s].\n",
                    e->idVendor, e->idProduct, e->oProduct? (char *)e + e->oProduct: ""));

            /* Create the device object and add the new device to list. */
            ComObjPtr<RemoteUSBDevice> pUSBDevice;
            pUSBDevice.createObject();
            pUSBDevice->init(u32ClientId, e, fDescExt);

            mRemoteUSBDevices.push_back(pUSBDevice);

            /* Check if the device is ok for current USB filters. */
            BOOL  fMatched   = FALSE;
            ULONG fMaskedIfs = 0;
            HRESULT hrc = mControl->RunUSBDeviceFilters(pUSBDevice, &fMatched, &fMaskedIfs);

            AssertComRC(hrc);

            LogFlowThisFunc(("USB filters return %d %#x\n", fMatched, fMaskedIfs));

            if (fMatched)
            {
                alock.release();
                hrc = i_onUSBDeviceAttach(pUSBDevice, NULL, fMaskedIfs, Utf8Str());
                alock.acquire();

                /// @todo (r=dmik) warning reporting subsystem

                if (hrc == S_OK)
                {
                    LogFlowThisFunc(("Device attached\n"));
                    pUSBDevice->captured(true);
                }
            }
        }

        AssertBreak(cbRemaining >= e->oNext); /* validateRemoteUSBDeviceDesc ensures this. */
        cbRemaining -= e->oNext;

        e = (VRDEUSBDEVICEDESC *)((uint8_t *)e + e->oNext);
    }

    /*
     * Remove dirty devices, that is those which are not reported by the server anymore.
     */
    for (;;)
    {
        ComObjPtr<RemoteUSBDevice> pUSBDevice;

        RemoteUSBDeviceList::iterator it = mRemoteUSBDevices.begin();
        while (it != mRemoteUSBDevices.end())
        {
            if ((*it)->dirty())
            {
                pUSBDevice = *it;
                break;
            }

            ++it;
        }

        if (!pUSBDevice)
        {
            break;
        }

        USHORT vendorId = 0;
        pUSBDevice->COMGETTER(VendorId)(&vendorId);

        USHORT productId = 0;
        pUSBDevice->COMGETTER(ProductId)(&productId);

        Bstr product;
        pUSBDevice->COMGETTER(Product)(product.asOutParam());

        LogRel(("Remote USB: ---- Vendor %04x. Product %04x. Name = [%ls].\n", vendorId, productId, product.raw()));

        /* Detach the device from VM. */
        if (pUSBDevice->captured())
        {
            Bstr uuid;
            pUSBDevice->COMGETTER(Id)(uuid.asOutParam());
            alock.release();
            i_onUSBDeviceDetach(uuid.raw(), NULL);
            alock.acquire();
        }

        /* And remove it from the list. */
        mRemoteUSBDevices.erase(it);
    }

    LogFlowThisFuncLeave();
}


/**
 * Worker called by VMPowerUpTask::handler to start the VM (also from saved
 * state) and track progress.
 *
 * @param   pTask       The power up task.
 *
 * @note Locks the Console object for writing.
 */
/*static*/
void Console::i_powerUpThreadTask(VMPowerUpTask *pTask)
{
    LogFlowFuncEnter();

    AssertReturnVoid(pTask);
    AssertReturnVoid(!pTask->mConsole.isNull());
    AssertReturnVoid(!pTask->mProgress.isNull());

    VirtualBoxBase::initializeComForThread();

    HRESULT hrc = S_OK;
    int vrc = VINF_SUCCESS;

    /* Set up a build identifier so that it can be seen from core dumps what
     * exact build was used to produce the core. */
    static char s_szBuildID[48];
    RTStrPrintf(s_szBuildID, sizeof(s_szBuildID), "%s%s%s%s VirtualBox %s r%u %s%s%s%s",
                "BU", "IL", "DI", "D", RTBldCfgVersion(), RTBldCfgRevision(), "BU", "IL", "DI", "D");

    ComObjPtr<Console> pConsole = pTask->mConsole;

    /* Note: no need to use AutoCaller because VMPowerUpTask does that */

    /* The lock is also used as a signal from the task initiator (which
     * releases it only after RTThreadCreate()) that we can start the job */
    AutoWriteLock alock(pConsole COMMA_LOCKVAL_SRC_POS);

    /* sanity */
    Assert(pConsole->mpUVM == NULL);

    try
    {
        // Create the VMM device object, which starts the HGCM thread; do this only
        // once for the console, for the pathological case that the same console
        // object is used to power up a VM twice.
        if (!pConsole->m_pVMMDev)
        {
            pConsole->m_pVMMDev = new VMMDev(pConsole);
            AssertReturnVoid(pConsole->m_pVMMDev);
        }

        /* wait for auto reset ops to complete so that we can successfully lock
         * the attached hard disks by calling LockMedia() below */
        for (VMPowerUpTask::ProgressList::const_iterator
             it = pTask->hardDiskProgresses.begin();
             it != pTask->hardDiskProgresses.end(); ++it)
        {
            HRESULT hrc2 = (*it)->WaitForCompletion(-1);
            AssertComRC(hrc2);

            hrc = pTask->mProgress->SetNextOperation(BstrFmt(tr("Disk Image Reset Operation - Immutable Image")).raw(), 1);
            AssertComRCReturnVoid(hrc);
        }

        /*
         * Lock attached media. This method will also check their accessibility.
         * If we're a teleporter, we'll have to postpone this action so we can
         * migrate between local processes.
         *
         * Note! The media will be unlocked automatically by
         *       SessionMachine::i_setMachineState() when the VM is powered down.
         */
        if (!pTask->mTeleporterEnabled)
        {
            hrc = pConsole->mControl->LockMedia();
            if (FAILED(hrc)) throw hrc;
        }

        /* Create the VRDP server. In case of headless operation, this will
         * also create the framebuffer, required at VM creation.
         */
        ConsoleVRDPServer *server = pConsole->i_consoleVRDPServer();
        Assert(server);

        /* Does VRDP server call Console from the other thread?
         * Not sure (and can change), so release the lock just in case.
         */
        alock.release();
        vrc = server->Launch();
        alock.acquire();

        if (vrc != VINF_SUCCESS)
        {
            Utf8Str errMsg = pConsole->VRDPServerErrorToMsg(vrc);
            if (   RT_FAILURE(vrc)
                && vrc != VERR_NET_ADDRESS_IN_USE) /* not fatal */
                throw i_setErrorStaticBoth(E_FAIL, vrc, errMsg.c_str());
        }

        ComPtr<IMachine> pMachine = pConsole->i_machine();
        ULONG cCpus = 1;
        pMachine->COMGETTER(CPUCount)(&cCpus);

        VMProcPriority_T enmVMPriority = VMProcPriority_Default;
        pMachine->COMGETTER(VMProcessPriority)(&enmVMPriority);

        VMExecutionEngine_T enmExecEngine = VMExecutionEngine_NotSet;
        pMachine->COMGETTER(VMExecutionEngine)(&enmExecEngine);

        /*
         * Create the VM
         *
         * Note! Release the lock since EMT will call Console. It's safe because
         *       mMachineState is either Starting or Restoring state here.
         */
        alock.release();

        if (enmVMPriority != VMProcPriority_Default)
            pConsole->i_onVMProcessPriorityChange(enmVMPriority);

        PCVMMR3VTABLE pVMM = pConsole->mpVMM;
        PVM           pVM  = NULL;
        vrc = pVMM->pfnVMR3Create(cCpus,
                                  pConsole->mpVmm2UserMethods,
                                     enmExecEngine == VMExecutionEngine_Interpreter
                                  || enmExecEngine == VMExecutionEngine_Recompiler /** @todo NativeApi too? */
                                  ? VMCREATE_F_DRIVERLESS : 0,
                                  Console::i_genericVMSetErrorCallback,
                                  &pTask->mErrorMsg,
                                  pTask->mpfnConfigConstructor,
                                  static_cast<Console *>(pConsole),
                                  &pVM, NULL);
        alock.acquire();
        if (RT_SUCCESS(vrc))
        {
            do /* break "loop" */
            {
                /*
                 * Register our load/save state file handlers
                 */
                vrc = pVMM->pfnSSMR3RegisterExternal(pConsole->mpUVM, sSSMConsoleUnit, 0 /*iInstance*/,
                                                     CONSOLE_SAVED_STATE_VERSION, 0 /* cbGuess */,
                                                     NULL, NULL, NULL,
                                                     NULL, i_saveStateFileExec, NULL,
                                                     NULL, i_loadStateFileExec, NULL,
                                                     static_cast<Console *>(pConsole));
                AssertRCBreak(vrc);

                vrc = static_cast<Console *>(pConsole)->i_getDisplay()->i_registerSSM(pConsole->mpUVM);
                AssertRC(vrc);
                if (RT_FAILURE(vrc))
                    break;

                /*
                 * Synchronize debugger settings
                 */
                MachineDebugger *machineDebugger = pConsole->i_getMachineDebugger();
                if (machineDebugger)
                    machineDebugger->i_flushQueuedSettings();

                /*
                 * Set domain name and DNS search list for DrvNAT
                 */
                pConsole->i_onNATDnsChanged();

                /*
                 * Shared Folders
                 */
                if (pConsole->m_pVMMDev->isShFlActive())
                {
                    /* Does the code below call Console from the other thread?
                     * Not sure, so release the lock just in case. */
                    alock.release();

                    for (SharedFolderDataMap::const_iterator it = pTask->mSharedFolders.begin();
                         it != pTask->mSharedFolders.end();
                         ++it)
                    {
                        const SharedFolderData &d = it->second;
                        hrc = pConsole->i_createSharedFolder(it->first, d);
                        if (FAILED(hrc))
                        {
                            ErrorInfoKeeper eik;
                            pConsole->i_atVMRuntimeErrorCallbackF(0, "BrokenSharedFolder",
                                   N_("The shared folder '%s' could not be set up: %ls.\n"
                                      "The shared folder setup will not be complete. It is recommended to power down the virtual "
                                      "machine and fix the shared folder settings while the machine is not running"),
                                    it->first.c_str(), eik.getText().raw());
                        }
                    }
                    if (FAILED(hrc))
                        hrc = S_OK;          // do not fail with broken shared folders

                    /* acquire the lock again */
                    alock.acquire();
                }

#ifdef VBOX_WITH_AUDIO_VRDE
                /*
                 * Attach the VRDE audio driver.
                 */
                if (pConsole->i_getVRDEServer())
                {
                    BOOL fVRDEEnabled = FALSE;
                    hrc = pConsole->i_getVRDEServer()->COMGETTER(Enabled)(&fVRDEEnabled);
                    AssertComRCBreak(hrc, RT_NOTHING);

                    if (   fVRDEEnabled
                        && pConsole->mAudioVRDE)
                        pConsole->mAudioVRDE->doAttachDriverViaEmt(pConsole->mpUVM, pVMM, &alock);
                }
#endif

                /*
                 * Enable client connections to the VRDP server.
                 */
                pConsole->i_consoleVRDPServer()->EnableConnections();

                /* Release the lock before a lengthy operation. */
                alock.release();

#ifdef VBOX_WITH_RECORDING
                /*
                 * Start recording if configured.
                 */
                ComPtr<IRecordingSettings> pRecordingSettings;
                hrc = pConsole->mMachine->COMGETTER(RecordingSettings)(pRecordingSettings.asOutParam());
                AssertComRCBreak(hrc, RT_NOTHING);

                BOOL fRecordingEnabled = FALSE;
                hrc = pRecordingSettings->COMGETTER(Enabled)(&fRecordingEnabled);
                AssertComRCBreak(hrc, RT_NOTHING);

                if (fRecordingEnabled)
                {
                    ComPtr<IProgress> pProgress;
                    hrc = pRecordingSettings->Start(pProgress.asOutParam());
                    if (FAILED(hrc))
                    {
                        com::ErrorInfoKeeper eik;
                        ComPtr<IVirtualBoxErrorInfo> pErrorInfo = eik.takeError();
                        Bstr strMsg;
                        hrc = pErrorInfo->COMGETTER(Text)(strMsg.asOutParam());
                        AssertComRCBreak(hrc, RT_NOTHING);
                        LONG lRc;
                        hrc = pErrorInfo->COMGETTER(ResultCode)(&lRc);
                        AssertComRCBreak(hrc, RT_NOTHING);

                        LogRel(("Recording: Failed to start on VM power up: %ls (%Rrc)\n",
                                strMsg.raw(), lRc));
                    }
                }
#endif
                /*
                 * Capture USB devices.
                 */
                hrc = pConsole->i_captureUSBDevices(pConsole->mpUVM);
                if (FAILED(hrc))
                {
                    alock.acquire();
                    break;
                }

                /*
                 * Load saved state?
                 */
                if (pTask->mSavedStateFile.length())
                {
                    LogFlowFunc(("Restoring saved state from '%s'...\n", pTask->mSavedStateFile.c_str()));

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
                    SsmStream ssmStream(pConsole, pVMM, pTask->m_pKeyStore, pTask->mKeyId, pTask->mKeyStore);

                    vrc = ssmStream.open(pTask->mSavedStateFile.c_str());
                    if (RT_SUCCESS(vrc))
                    {
                        PCSSMSTRMOPS pStreamOps;
                        void *pvStreamOpsUser;

                        vrc = ssmStream.querySsmStrmOps(&pStreamOps, &pvStreamOpsUser);
                        if (RT_SUCCESS(vrc))
                            vrc = pVMM->pfnVMR3LoadFromStream(pConsole->mpUVM,
                                                              pStreamOps, pvStreamOpsUser,
                                                              Console::i_stateProgressCallback,
                                                              static_cast<IProgress *>(pTask->mProgress),
                                                              false /*fTeleporting*/);
                    }
#else
                    vrc = pVMM->pfnVMR3LoadFromFile(pConsole->mpUVM,
                                                    pTask->mSavedStateFile.c_str(),
                                                    Console::i_stateProgressCallback,
                                                    static_cast<IProgress *>(pTask->mProgress));
#endif
                    if (RT_SUCCESS(vrc))
                    {
                        if (pTask->mStartPaused)
                            /* done */
                            pConsole->i_setMachineState(MachineState_Paused);
                        else
                        {
                            /* Start/Resume the VM execution */
#ifdef VBOX_WITH_EXTPACK
                            vrc = pConsole->mptrExtPackManager->i_callAllVmPowerOnHooks(pConsole, pVM, pVMM);
#endif
                            if (RT_SUCCESS(vrc))
                                vrc = pVMM->pfnVMR3Resume(pConsole->mpUVM, VMRESUMEREASON_STATE_RESTORED);
                            AssertLogRelRC(vrc);
                        }
                    }

                    /* Power off in case we failed loading or resuming the VM */
                    if (RT_FAILURE(vrc))
                    {
                        int vrc2 = pVMM->pfnVMR3PowerOff(pConsole->mpUVM); AssertLogRelRC(vrc2);
#ifdef VBOX_WITH_EXTPACK
                        pConsole->mptrExtPackManager->i_callAllVmPowerOffHooks(pConsole, pVM, pVMM);
#endif
                    }
                }
                else if (pTask->mTeleporterEnabled)
                {
                    /* -> ConsoleImplTeleporter.cpp */
                    bool fPowerOffOnFailure;
                    hrc = pConsole->i_teleporterTrg(pConsole->mpUVM, pConsole->mpVMM, pMachine, &pTask->mErrorMsg,
                                                    pTask->mStartPaused, pTask->mProgress, &fPowerOffOnFailure);
                    if (FAILED(hrc) && fPowerOffOnFailure)
                    {
                        ErrorInfoKeeper eik;
                        int vrc2 = pVMM->pfnVMR3PowerOff(pConsole->mpUVM); AssertLogRelRC(vrc2);
#ifdef VBOX_WITH_EXTPACK
                        pConsole->mptrExtPackManager->i_callAllVmPowerOffHooks(pConsole, pVM, pVMM);
#endif
                    }
                }
                else if (pTask->mStartPaused)
                    /* done */
                    pConsole->i_setMachineState(MachineState_Paused);
                else
                {
                    /* Power on the VM (i.e. start executing) */
#ifdef VBOX_WITH_EXTPACK
                    vrc = pConsole->mptrExtPackManager->i_callAllVmPowerOnHooks(pConsole, pVM, pVMM);
#endif
                    if (RT_SUCCESS(vrc))
                        vrc = pVMM->pfnVMR3PowerOn(pConsole->mpUVM);
                    AssertLogRelRC(vrc);
                }

                /* acquire the lock again */
                alock.acquire();
            }
            while (0);

            /* On failure, destroy the VM */
            if (FAILED(hrc) || RT_FAILURE(vrc))
            {
                /* preserve existing error info */
                ErrorInfoKeeper eik;

                /* powerDown() will call VMR3Destroy() and do all necessary
                 * cleanup (VRDP, USB devices) */
                alock.release();
                HRESULT hrc2 = pConsole->i_powerDown();
                alock.acquire();
                AssertComRC(hrc2);
            }
            else
            {
                /*
                 * Deregister the VMSetError callback. This is necessary as the
                 * pfnVMAtError() function passed to VMR3Create() is supposed to
                 * be sticky but our error callback isn't.
                 */
                alock.release();
                pVMM->pfnVMR3AtErrorDeregister(pConsole->mpUVM, Console::i_genericVMSetErrorCallback, &pTask->mErrorMsg);
                /** @todo register another VMSetError callback? */
                alock.acquire();
            }
        }
        else
        {
            /*
             * If VMR3Create() failed it has released the VM memory.
             */
            if (pConsole->m_pVMMDev)
            {
                alock.release(); /* just to be on the safe side... */
                pConsole->m_pVMMDev->hgcmShutdown(true /*fUvmIsInvalid*/);
                alock.acquire();
            }
            pVMM->pfnVMR3ReleaseUVM(pConsole->mpUVM);
            pConsole->mpUVM = NULL;
        }

        if (SUCCEEDED(hrc) && RT_FAILURE(vrc))
        {
            /* If VMR3Create() or one of the other calls in this function fail,
             * an appropriate error message has been set in pTask->mErrorMsg.
             * However since that happens via a callback, the hrc status code in
             * this function is not updated.
             */
            if (!pTask->mErrorMsg.length())
            {
                /* If the error message is not set but we've got a failure,
                 * convert the VBox status code into a meaningful error message.
                 * This becomes unused once all the sources of errors set the
                 * appropriate error message themselves.
                 */
                AssertMsgFailed(("Missing error message during powerup for status code %Rrc\n", vrc));
                pTask->mErrorMsg = Utf8StrFmt(tr("Failed to start VM execution (%Rrc)"), vrc);
            }

            /* Set the error message as the COM error.
             * Progress::notifyComplete() will pick it up later. */
            throw i_setErrorStaticBoth(E_FAIL, vrc, pTask->mErrorMsg.c_str());
        }
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    if (   pConsole->mMachineState == MachineState_Starting
        || pConsole->mMachineState == MachineState_Restoring
        || pConsole->mMachineState == MachineState_TeleportingIn
       )
    {
        /* We are still in the Starting/Restoring state. This means one of:
         *
         * 1) we failed before VMR3Create() was called;
         * 2) VMR3Create() failed.
         *
         * In both cases, there is no need to call powerDown(), but we still
         * need to go back to the PoweredOff/Saved state. Reuse
         * vmstateChangeCallback() for that purpose.
         */

        /* preserve existing error info */
        ErrorInfoKeeper eik;

        Assert(pConsole->mpUVM == NULL);
        i_vmstateChangeCallback(NULL, pConsole->mpVMM, VMSTATE_TERMINATED, VMSTATE_CREATING, pConsole);
    }

    /*
     * Evaluate the final result. Note that the appropriate mMachineState value
     * is already set by vmstateChangeCallback() in all cases.
     */

    /* release the lock, don't need it any more */
    alock.release();

    if (SUCCEEDED(hrc))
    {
        /* Notify the progress object of the success */
        pTask->mProgress->i_notifyComplete(S_OK);
    }
    else
    {
        /* The progress object will fetch the current error info */
        pTask->mProgress->i_notifyComplete(hrc);
        LogRel(("Power up failed (vrc=%Rrc, hrc=%Rhrc (%#08X))\n", vrc, hrc, hrc));
    }

    /* Notify VBoxSVC and any waiting openRemoteSession progress object. */
    pConsole->mControl->EndPowerUp(hrc);

#if defined(RT_OS_WINDOWS)
    /* uninitialize COM */
    CoUninitialize();
#endif

    LogFlowFuncLeave();
}


/**
 * Reconfigures a medium attachment (part of taking or deleting an online snapshot).
 *
 * @param   pThis                   Reference to the console object.
 * @param   pUVM                    The VM handle.
 * @param   pVMM                    The VMM vtable.
 * @param   pArgs                   Argument package.
 * @param   phrc                    Where to store com error - only valid if we return VERR_GENERAL_FAILURE.
 * @return  VBox status code.
 */
/* static */
DECLCALLBACK(int) Console::i_reconfigureMediumAttachment(Console *pThis,
                                                         PUVM pUVM,
                                                         PCVMMR3VTABLE pVMM,
                                                         Console::ReconfigureMediumAttachmentArgs const *pArgs,
                                                         HRESULT *phrc)
{
    LogFlowFunc(("pUVM=%p aMediumAtt=%p phrc=%p\n", pUVM, pArgs->aMediumAtt, phrc));

    HRESULT         hrc;
    Bstr            bstr;
    *phrc = S_OK;
#define H() do { if (FAILED(hrc)) { AssertMsgFailed(("hrc=%Rhrc (%#x)\n", hrc, hrc)); *phrc = hrc; return VERR_GENERAL_FAILURE; } } while (0)

    /* Ignore attachments other than hard disks, since at the moment they are
     * not subject to snapshotting in general. */
    DeviceType_T lType;
    hrc = pArgs->aMediumAtt->COMGETTER(Type)(&lType); H();
    if (lType != DeviceType_HardDisk)
        return VINF_SUCCESS;

    /* Update the device instance configuration. */
    int vrc = pThis->i_configMediumAttachment(pArgs->pcszDevice,
                                              pArgs->uInstance,
                                              pArgs->enmBus,
                                              pArgs->fUseHostIOCache,
                                              pArgs->fBuiltinIOCache,
                                              pArgs->fInsertDiskIntegrityDrv,
                                              pArgs->fSetupMerge,
                                              pArgs->uMergeSource,
                                              pArgs->uMergeTarget,
                                              pArgs->aMediumAtt,
                                              pArgs->aMachineState,
                                              phrc,
                                              true /* fAttachDetach */,
                                              false /* fForceUnmount */,
                                              false /* fHotplug */,
                                              pUVM,
                                              pVMM,
                                              NULL /* paLedDevType */,
                                              NULL /* ppLunL0)*/);
    if (RT_FAILURE(vrc))
    {
        AssertMsgFailed(("vrc=%Rrc\n", vrc));
        return vrc;
    }

#undef H

    LogFlowFunc(("Returns success\n"));
    return VINF_SUCCESS;
}

/**
 * Thread for powering down the Console.
 *
 * @param   pTask       The power down task.
 *
 * @note Locks the Console object for writing.
 */
/*static*/
void Console::i_powerDownThreadTask(VMPowerDownTask *pTask)
{
    int vrc = VINF_SUCCESS; /* only used in assertion */
    LogFlowFuncEnter();
    try
    {
        if (pTask->isOk() == false)
            vrc = VERR_GENERAL_FAILURE;

        const ComObjPtr<Console> &that = pTask->mConsole;

        /* Note: no need to use AutoCaller to protect Console because VMTask does
         * that */

        /* wait until the method tat started us returns */
        AutoWriteLock thatLock(that COMMA_LOCKVAL_SRC_POS);

        /* release VM caller to avoid the powerDown() deadlock */
        pTask->releaseVMCaller();

        thatLock.release();

        that->i_powerDown(pTask->mServerProgress);

        /* complete the operation */
        that->mControl->EndPoweringDown(S_OK, Bstr().raw());

    }
    catch (const std::exception &e)
    {
        AssertMsgFailed(("Exception %s was caught, vrc=%Rrc\n", e.what(), vrc));
        NOREF(e); NOREF(vrc);
    }

    LogFlowFuncLeave();
}

/**
 * @interface_method_impl{VMM2USERMETHODS,pfnSaveState}
 */
/*static*/ DECLCALLBACK(int)
Console::i_vmm2User_SaveState(PCVMM2USERMETHODS pThis, PUVM pUVM)
{
    Console *pConsole = ((MYVMM2USERMETHODS *)pThis)->pConsole;
    NOREF(pUVM);

    /*
     * For now, just call SaveState.  We should probably try notify the GUI so
     * it can pop up a progress object and stuff. The progress object created
     * by the call isn't returned to anyone and thus gets updated without
     * anyone noticing it.
     */
    ComPtr<IProgress> pProgress;
    HRESULT hrc = pConsole->mMachine->SaveState(pProgress.asOutParam());
    return SUCCEEDED(hrc) ? VINF_SUCCESS : Global::vboxStatusCodeFromCOM(hrc);
}

/**
 * @interface_method_impl{VMM2USERMETHODS,pfnNotifyEmtInit}
 */
/*static*/ DECLCALLBACK(void)
Console::i_vmm2User_NotifyEmtInit(PCVMM2USERMETHODS pThis, PUVM pUVM, PUVMCPU pUVCpu)
{
    NOREF(pThis); NOREF(pUVM); NOREF(pUVCpu);
    VirtualBoxBase::initializeComForThread();
}

/**
 * @interface_method_impl{VMM2USERMETHODS,pfnNotifyEmtTerm}
 */
/*static*/ DECLCALLBACK(void)
Console::i_vmm2User_NotifyEmtTerm(PCVMM2USERMETHODS pThis, PUVM pUVM, PUVMCPU pUVCpu)
{
    NOREF(pThis); NOREF(pUVM); NOREF(pUVCpu);
    VirtualBoxBase::uninitializeComForThread();
}

/**
 * @interface_method_impl{VMM2USERMETHODS,pfnNotifyPdmtInit}
 */
/*static*/ DECLCALLBACK(void)
Console::i_vmm2User_NotifyPdmtInit(PCVMM2USERMETHODS pThis, PUVM pUVM)
{
    NOREF(pThis); NOREF(pUVM);
    VirtualBoxBase::initializeComForThread();
}

/**
 * @interface_method_impl{VMM2USERMETHODS,pfnNotifyPdmtTerm}
 */
/*static*/ DECLCALLBACK(void)
Console::i_vmm2User_NotifyPdmtTerm(PCVMM2USERMETHODS pThis, PUVM pUVM)
{
    NOREF(pThis); NOREF(pUVM);
    VirtualBoxBase::uninitializeComForThread();
}

/**
 * @interface_method_impl{VMM2USERMETHODS,pfnNotifyResetTurnedIntoPowerOff}
 */
/*static*/ DECLCALLBACK(void)
Console::i_vmm2User_NotifyResetTurnedIntoPowerOff(PCVMM2USERMETHODS pThis, PUVM pUVM)
{
    Console *pConsole = ((MYVMM2USERMETHODS *)pThis)->pConsole;
    NOREF(pUVM);

    pConsole->mfPowerOffCausedByReset = true;
}

/**
 * Internal function to get LED set off of Console instance
 *
 * @returns pointer to PDMLED object
 *
 * @param   iLedSet      Index of LED set to fetch
 */
PPDMLED volatile *
Console::i_getLedSet(uint32_t iLedSet)
{
    AssertReturn(iLedSet < RT_ELEMENTS(maLedSets), NULL);
    return maLedSets[iLedSet].papLeds;
}

/**
 * @interface_method_impl{VMM2USERMETHODS,pfnQueryGenericObject}
 */
/*static*/ DECLCALLBACK(void *)
Console::i_vmm2User_QueryGenericObject(PCVMM2USERMETHODS pThis, PUVM pUVM, PCRTUUID pUuid)
{
    Console *pConsole = ((MYVMM2USERMETHODS *)pThis)->pConsole;
    NOREF(pUVM);

    /* To simplify comparison we copy the UUID into a com::Guid object. */
    com::Guid const UuidCopy(*pUuid);

    if (UuidCopy == COM_IIDOF(IConsole))
    {
        IConsole *pIConsole = static_cast<IConsole *>(pConsole);
        return pIConsole;
    }

    if (UuidCopy == COM_IIDOF(IMachine))
    {
        IMachine *pIMachine = pConsole->mMachine;
        return pIMachine;
    }

    if (UuidCopy == COM_IIDOF(IKeyboard))
    {
        IKeyboard *pIKeyboard = pConsole->mKeyboard;
        return pIKeyboard;
    }

    if (UuidCopy == COM_IIDOF(IMouse))
    {
        IMouse *pIMouse = pConsole->mMouse;
        return pIMouse;
    }

    if (UuidCopy == COM_IIDOF(IDisplay))
    {
        IDisplay *pIDisplay = pConsole->mDisplay;
        return pIDisplay;
    }

    if (UuidCopy == COM_IIDOF(INvramStore))
    {
        NvramStore *pNvramStore = static_cast<NvramStore *>(pConsole->mptrNvramStore);
        return pNvramStore;
    }

#ifdef VBOX_WITH_VIRT_ARMV8
    if (UuidCopy == COM_IIDOF(IResourceStore))
    {
        ResourceStore *pResourceStore = static_cast<ResourceStore *>(pConsole->mptrResourceStore);
        return pResourceStore;
    }
#endif

    if (UuidCopy == VMMDEV_OID)
        return pConsole->m_pVMMDev;

    if (UuidCopy == USBCARDREADER_OID)
        return pConsole->mUsbCardReader;

    if (UuidCopy == COM_IIDOF(ISnapshot))
        return ((MYVMM2USERMETHODS *)pThis)->pISnapshot;

#ifdef VBOX_WITH_USB
    if (UuidCopy == REMOTEUSBIF_OID)
        return &pConsole->mRemoteUsbIf;
#endif

    if (UuidCopy == EMULATEDUSBIF_OID)
        return pConsole->mEmulatedUSB->i_getEmulatedUsbIf();

    return NULL;
}


/**
 * @interface_method_impl{PDMISECKEY,pfnKeyRetain}
 */
/*static*/ DECLCALLBACK(int)
Console::i_pdmIfSecKey_KeyRetain(PPDMISECKEY pInterface, const char *pszId, const uint8_t **ppbKey, size_t *pcbKey)
{
    Console *pConsole = ((MYPDMISECKEY *)pInterface)->pConsole;

    AutoReadLock thatLock(pConsole COMMA_LOCKVAL_SRC_POS);
    SecretKey *pKey = NULL;

    int vrc = pConsole->m_pKeyStore->retainSecretKey(Utf8Str(pszId), &pKey);
    if (RT_SUCCESS(vrc))
    {
        *ppbKey = (const uint8_t *)pKey->getKeyBuffer();
        *pcbKey = pKey->getKeySize();
    }

    return vrc;
}

/**
 * @interface_method_impl{PDMISECKEY,pfnKeyRelease}
 */
/*static*/ DECLCALLBACK(int)
Console::i_pdmIfSecKey_KeyRelease(PPDMISECKEY pInterface, const char *pszId)
{
    Console *pConsole = ((MYPDMISECKEY *)pInterface)->pConsole;

    AutoReadLock thatLock(pConsole COMMA_LOCKVAL_SRC_POS);
    return pConsole->m_pKeyStore->releaseSecretKey(Utf8Str(pszId));
}

/**
 * @interface_method_impl{PDMISECKEY,pfnPasswordRetain}
 */
/*static*/ DECLCALLBACK(int)
Console::i_pdmIfSecKey_PasswordRetain(PPDMISECKEY pInterface, const char *pszId, const char **ppszPassword)
{
    Console *pConsole = ((MYPDMISECKEY *)pInterface)->pConsole;

    AutoReadLock thatLock(pConsole COMMA_LOCKVAL_SRC_POS);
    SecretKey *pKey = NULL;

    int vrc = pConsole->m_pKeyStore->retainSecretKey(Utf8Str(pszId), &pKey);
    if (RT_SUCCESS(vrc))
        *ppszPassword = (const char *)pKey->getKeyBuffer();

    return vrc;
}

/**
 * @interface_method_impl{PDMISECKEY,pfnPasswordRelease}
 */
/*static*/ DECLCALLBACK(int)
Console::i_pdmIfSecKey_PasswordRelease(PPDMISECKEY pInterface, const char *pszId)
{
    Console *pConsole = ((MYPDMISECKEY *)pInterface)->pConsole;

    AutoReadLock thatLock(pConsole COMMA_LOCKVAL_SRC_POS);
    return pConsole->m_pKeyStore->releaseSecretKey(Utf8Str(pszId));
}

/**
 * @interface_method_impl{PDMISECKEYHLP,pfnKeyMissingNotify}
 */
/*static*/ DECLCALLBACK(int)
Console::i_pdmIfSecKeyHlp_KeyMissingNotify(PPDMISECKEYHLP pInterface)
{
    Console *pConsole = ((MYPDMISECKEYHLP *)pInterface)->pConsole;

    /* Set guest property only, the VM is paused in the media driver calling us. */
    pConsole->mMachine->DeleteGuestProperty(Bstr("/VirtualBox/HostInfo/DekMissing").raw());
    pConsole->mMachine->SetGuestProperty(Bstr("/VirtualBox/HostInfo/DekMissing").raw(),
                                         Bstr("1").raw(), Bstr("RDONLYGUEST").raw());
    pConsole->mMachine->SaveSettings();

    return VINF_SUCCESS;
}



/**
 * The Main status driver instance data.
 */
typedef struct DRVMAINSTATUS
{
    /** The LED connectors. */
    PDMILEDCONNECTORS   ILedConnectors;
    /** Pointer to the LED ports interface above us. */
    PPDMILEDPORTS       pLedPorts;
    /** Pointer to the array of LED pointers. */
    PPDMLED volatile   *papLeds;
    /** The unit number corresponding to the first entry in the LED array. */
    uint32_t            iFirstLUN;
    /** The unit number corresponding to the last entry in the LED array.
     * (The size of the LED array is iLastLUN - iFirstLUN + 1.) */
    uint32_t            iLastLUN;
    /** Pointer to the driver instance. */
    PPDMDRVINS          pDrvIns;
    /** The Media Notify interface. */
    PDMIMEDIANOTIFY     IMediaNotify;
    /** Set if there potentially are medium attachments. */
    bool                fHasMediumAttachments;
    /** Device name+instance for mapping */
    char                *pszDeviceInstance;
    /** Pointer to the Console object, for driver triggered activities. */
    Console             *pConsole;
} DRVMAINSTATUS;
/** Pointer the instance data for a Main status driver. */
typedef DRVMAINSTATUS *PDRVMAINSTATUS;


/**
 * Notification about a unit which have been changed.
 *
 * The driver must discard any pointers to data owned by
 * the unit and requery it.
 *
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   iLUN            The unit number.
 */
DECLCALLBACK(void) Console::i_drvStatus_UnitChanged(PPDMILEDCONNECTORS pInterface, unsigned iLUN)
{
    PDRVMAINSTATUS pThis = RT_FROM_MEMBER(pInterface, DRVMAINSTATUS, ILedConnectors);
    if (iLUN >= pThis->iFirstLUN && iLUN <= pThis->iLastLUN)
    {
        /*
         * Query the pointer to the PDMLED field inside the target device
         * structure (owned by the virtual hardware device).
         */
        PPDMLED pLed;
        int vrc = pThis->pLedPorts->pfnQueryStatusLed(pThis->pLedPorts, iLUN, &pLed);
        if (RT_FAILURE(vrc))
            pLed = NULL;

        /*
         * Update the corresponding papLeds[] entry.
         *
         * papLeds[] points to the struct PDMLED of each of this driver's
         * units.  The entries are initialized here, called out of a loop
         * in Console::i_drvStatus_Construct(), which previously called
         * Console::i_attachStatusDriver() to allocate the array itself.
         *
         * The arrays (and thus individual LEDs) are eventually read out
         * by Console::getDeviceActivity(), which is itself called from
         * src/VBox/Frontends/VirtualBox/src/runtime/UIIndicatorsPool.cpp
         */
        /** @todo acquire Console::mLedLock here in exclusive mode? */
        ASMAtomicWritePtr(&pThis->papLeds[iLUN - pThis->iFirstLUN], pLed);
        Log(("drvStatus_UnitChanged: iLUN=%d pLed=%p\n", iLUN, pLed));
    }
}


/**
 * Notification about a medium eject.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   uLUN            The unit number.
 */
DECLCALLBACK(int) Console::i_drvStatus_MediumEjected(PPDMIMEDIANOTIFY pInterface, unsigned uLUN)
{
    PDRVMAINSTATUS pThis = RT_FROM_MEMBER(pInterface, DRVMAINSTATUS, IMediaNotify);
    LogFunc(("uLUN=%d\n", uLUN));
    if (pThis->fHasMediumAttachments)
    {
        Console * const pConsole = pThis->pConsole;
        AutoWriteLock alock(pConsole COMMA_LOCKVAL_SRC_POS);

        ComPtr<IMediumAttachment> pMediumAtt;
        Utf8Str devicePath = Utf8StrFmt("%s/LUN#%u", pThis->pszDeviceInstance, uLUN);
        Console::MediumAttachmentMap::const_iterator end = pConsole->mapMediumAttachments.end();
        Console::MediumAttachmentMap::const_iterator it  = pConsole->mapMediumAttachments.find(devicePath);
        if (it != end)
            pMediumAtt = it->second;
        Assert(!pMediumAtt.isNull());
        if (!pMediumAtt.isNull())
        {
            IMedium *pMedium = NULL;
            HRESULT hrc = pMediumAtt->COMGETTER(Medium)(&pMedium);
            AssertComRC(hrc);
            if (SUCCEEDED(hrc) && pMedium)
            {
                BOOL fHostDrive = FALSE;
                hrc = pMedium->COMGETTER(HostDrive)(&fHostDrive);
                AssertComRC(hrc);
                if (!fHostDrive)
                {
                    alock.release();

                    ComPtr<IMediumAttachment> pNewMediumAtt;
                    hrc = pThis->pConsole->mControl->EjectMedium(pMediumAtt, pNewMediumAtt.asOutParam());
                    if (SUCCEEDED(hrc))
                    {
                        pThis->pConsole->mMachine->SaveSettings();
                        ::FireMediumChangedEvent(pThis->pConsole->mEventSource, pNewMediumAtt);
                    }

                    alock.acquire();
                    if (pNewMediumAtt != pMediumAtt)
                    {
                        pConsole->mapMediumAttachments.erase(devicePath);
                        pConsole->mapMediumAttachments.insert(std::make_pair(devicePath, pNewMediumAtt));
                    }
                }
            }
        }
    }
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
DECLCALLBACK(void *)  Console::i_drvStatus_QueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVMAINSTATUS pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINSTATUS);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMILEDCONNECTORS, &pThis->ILedConnectors);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIANOTIFY, &pThis->IMediaNotify);
    return NULL;
}


/**
 * Destruct a status driver instance.
 *
 * @param   pDrvIns     The driver instance data.
 */
DECLCALLBACK(void) Console::i_drvStatus_Destruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVMAINSTATUS pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINSTATUS);
    LogFlowFunc(("iInstance=%d\n", pDrvIns->iInstance));

    if (pThis->papLeds)
    {
        unsigned iLed = pThis->iLastLUN - pThis->iFirstLUN + 1;
        while (iLed-- > 0)
            ASMAtomicWriteNullPtr(&pThis->papLeds[iLed]);
    }
}


/**
 * Construct a status driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
DECLCALLBACK(int) Console::i_drvStatus_Construct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVMAINSTATUS pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINSTATUS);
    LogFlowFunc(("iInstance=%d\n", pDrvIns->iInstance));

    /*
     * Initialize data.
     */
    com::Guid ConsoleUuid(COM_IIDOF(IConsole));
    IConsole *pIConsole = (IConsole *)PDMDrvHlpQueryGenericUserObject(pDrvIns, ConsoleUuid.raw());
    AssertLogRelReturn(pIConsole, VERR_INTERNAL_ERROR_3);
    Console *pConsole = static_cast<Console *>(pIConsole);
    AssertLogRelReturn(pConsole, VERR_INTERNAL_ERROR_3);

    pDrvIns->IBase.pfnQueryInterface        = Console::i_drvStatus_QueryInterface;
    pThis->ILedConnectors.pfnUnitChanged    = Console::i_drvStatus_UnitChanged;
    pThis->IMediaNotify.pfnEjected          = Console::i_drvStatus_MediumEjected;
    pThis->pDrvIns                          = pDrvIns;
    pThis->pConsole                         = pConsole;
    pThis->fHasMediumAttachments            = false;
    pThis->papLeds                          = NULL;
    pThis->pszDeviceInstance                = NULL;

    /*
     * Validate configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns,
                                  "DeviceInstance|"
                                  "iLedSet|"
                                  "HasMediumAttachments|"
                                  "First|"
                                  "Last",
                                  "");
    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /*
     * Read config.
     */
    PCPDMDRVHLPR3 const pHlp = pDrvIns->pHlpR3;

    uint32_t iLedSet;
    int vrc = pHlp->pfnCFGMQueryU32(pCfg, "iLedSet", &iLedSet);
    AssertLogRelMsgRCReturn(vrc, ("Configuration error: Failed to query the \"iLedSet\" value! vrc=%Rrc\n", vrc), vrc);
    pThis->papLeds = pConsole->i_getLedSet(iLedSet);

    vrc = pHlp->pfnCFGMQueryBoolDef(pCfg, "HasMediumAttachments", &pThis->fHasMediumAttachments, false);
    AssertLogRelMsgRCReturn(vrc, ("Configuration error: Failed to query the \"HasMediumAttachments\" value! vrc=%Rrc\n", vrc), vrc);

    if (pThis->fHasMediumAttachments)
    {
        vrc = pHlp->pfnCFGMQueryStringAlloc(pCfg, "DeviceInstance", &pThis->pszDeviceInstance);
        AssertLogRelMsgRCReturn(vrc, ("Configuration error: Failed to query the \"DeviceInstance\" value! vrc=%Rrc\n", vrc), vrc);
    }

    vrc = pHlp->pfnCFGMQueryU32Def(pCfg, "First", &pThis->iFirstLUN, 0);
    AssertLogRelMsgRCReturn(vrc, ("Configuration error: Failed to query the \"First\" value! vrc=%Rrc\n", vrc), vrc);

    vrc = pHlp->pfnCFGMQueryU32Def(pCfg, "Last", &pThis->iLastLUN, 0);
    AssertLogRelMsgRCReturn(vrc, ("Configuration error: Failed to query the \"Last\" value! vrc=%Rrc\n", vrc), vrc);

    AssertLogRelMsgReturn(pThis->iFirstLUN <= pThis->iLastLUN,
                          ("Configuration error: Invalid unit range %u-%u\n", pThis->iFirstLUN, pThis->iLastLUN),
                          VERR_INVALID_PARAMETER);

    /*
     * Get the ILedPorts interface of the above driver/device and
     * query the LEDs we want.
     */
    pThis->pLedPorts = PDMIBASE_QUERY_INTERFACE(pDrvIns->pUpBase, PDMILEDPORTS);
    AssertMsgReturn(pThis->pLedPorts, ("Configuration error: No led ports interface above!\n"),
                    VERR_PDM_MISSING_INTERFACE_ABOVE);

    for (unsigned i = pThis->iFirstLUN; i <= pThis->iLastLUN; ++i)
        Console::i_drvStatus_UnitChanged(&pThis->ILedConnectors, i);

    return VINF_SUCCESS;
}


/**
 * Console status driver (LED) registration record.
 */
const PDMDRVREG Console::DrvStatusReg =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "MainStatus",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Main status driver (Main as in the API).",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_STATUS,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVMAINSTATUS),
    /* pfnConstruct */
    Console::i_drvStatus_Construct,
    /* pfnDestruct */
    Console::i_drvStatus_Destruct,
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
    /* u32EndVersion */
    PDM_DRVREG_VERSION
};



/* vi: set tabstop=4 shiftwidth=4 expandtab: */
