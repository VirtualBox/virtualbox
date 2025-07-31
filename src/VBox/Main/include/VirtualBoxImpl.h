/* $Id: VirtualBoxImpl.h 110242 2025-07-16 08:35:24Z andreas.loeffler@oracle.com $ */
/** @file
 * VirtualBox COM class implementation
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

#ifndef MAIN_INCLUDED_VirtualBoxImpl_h
#define MAIN_INCLUDED_VirtualBoxImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/VBoxCryptoIf.h>

#include "VirtualBoxBase.h"
#include "objectslist.h"
#include "VirtualBoxWrap.h"

#ifdef RT_OS_WINDOWS
# include "win/resource.h"
#endif

//#ifdef DEBUG_bird
//# define VBOXSVC_WITH_CLIENT_WATCHER
//#endif

namespace com
{
    class Event;
    class EventQueue;
}

class SessionMachine;
class GuestOSType;
class Progress;
class Host;
class SystemProperties;
class DHCPServer;
class PerformanceCollector;
class CloudProviderManager;
#ifdef VBOX_WITH_EXTPACK
class ExtPackManager;
#endif
class AutostartDb;
class NATNetwork;
#ifdef VBOX_WITH_CLOUD_NET
class CloudNetwork;
#endif /* VBOX_WITH_CLOUD_NET */

typedef std::list<ComObjPtr<SessionMachine> > SessionMachinesList;

#ifdef RT_OS_WINDOWS
class SVCHlpClient;
#endif

namespace settings
{
    class MainConfigFile;
    struct MediaRegistry;
}

#if defined(VBOX_WITH_SDS) && !defined(VBOX_WITH_XPCOM)
class VirtualBoxClassFactory; /* See ../src-server/win/svcmain.cpp  */
#endif

class ATL_NO_VTABLE VirtualBox :
    public VirtualBoxWrap
#ifdef RT_OS_WINDOWS
     , public ATL::CComCoClass<VirtualBox, &CLSID_VirtualBox>
#endif
{

public:

    typedef std::list<ComPtr<IInternalSessionControl> > InternalControlList;
    typedef ObjectsList<Machine> MachinesOList;

    class AsyncEvent;
    friend class AsyncEvent;

#ifndef VBOX_WITH_XPCOM
# ifdef VBOX_WITH_SDS
    DECLARE_CLASSFACTORY_EX(VirtualBoxClassFactory)
# else
    DECLARE_CLASSFACTORY_SINGLETON(VirtualBox)
# endif
#endif

    // Do not use any ATL registry support.
    //DECLARE_REGISTRY_RESOURCEID(IDR_VIRTUALBOX)

    // Kind of redundant (VirtualBoxWrap declares itself not aggregatable and
    // CComCoClass<VirtualBox, &CLSID_VirtualBox> as aggregatable, the former
    // is the first inheritance), but the C++ multiple inheritance rules and
    // the class factory in svcmain.cpp needs this to disambiguate.
    DECLARE_NOT_AGGREGATABLE(VirtualBox)

    // to postpone generation of the default ctor/dtor
    DECLARE_COMMON_CLASS_METHODS(VirtualBox)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init();
    HRESULT initMachines();
    HRESULT initMedia(const Guid &uuidMachineRegistry,
                      const settings::MediaRegistry &mediaRegistry,
                      const Utf8Str &strMachineFolder);
    void uninit();

    // public methods only for internal purposes

    /**
     * Override of the default locking class to be used for validating lock
     * order with the standard member lock handle.
     */
    virtual VBoxLockingClass getLockingClass() const
    {
        return LOCKCLASS_VIRTUALBOXOBJECT;
    }

#ifdef DEBUG
    void i_dumpAllBackRefs();
#endif

    HRESULT i_postEvent(Event *event);

    HRESULT i_addProgress(IProgress *aProgress);
    HRESULT i_removeProgress(IN_GUID aId);

#ifdef RT_OS_WINDOWS
    typedef HRESULT (*PFN_SVC_HELPER_CLIENT_T)(SVCHlpClient *aClient, Progress *aProgress, void *aUser, int *aVrc);
    HRESULT i_startSVCHelperClient(bool aPrivileged,
                                   PFN_SVC_HELPER_CLIENT_T aFunc,
                                   void *aUser, Progress *aProgress);
#endif

    void i_addProcessToReap(RTPROCESS pid);
    void i_updateClientWatcher();

    int i_loadVDPlugin(const char *pszPluginLibrary);
    int i_unloadVDPlugin(const char *pszPluginLibrary);

    void i_onMediumRegistered(const Guid &aMediumId, const DeviceType_T aDevType, BOOL aRegistered);
    void i_onMediumConfigChanged(IMedium *aMedium);
    void i_onMediumChanged(IMediumAttachment* aMediumAttachment);
    void i_onSharedFolderChanged();
    void i_onStorageControllerChanged(const Guid &aMachineId, const com::Utf8Str &aControllerName);
    void i_onStorageDeviceChanged(IMediumAttachment* aStorageDevice, BOOL fRemoved, BOOL fSilent);
    void i_onMachineStateChanged(const Guid &aId, MachineState_T aState);
    void i_onMachineDataChanged(const Guid &aId, BOOL aTemporary = FALSE);
    void i_onMachineGroupsChanged(const Guid &aId);
    BOOL i_onExtraDataCanChange(const Guid &aId, const Utf8Str &aKey, const Utf8Str &aValue, Bstr &aError);
    void i_onExtraDataChanged(const Guid &aId, const Utf8Str &aKey, const Utf8Str &aValue);
    void i_onMachineRegistered(const Guid &aId, BOOL aRegistered);
    void i_onSessionStateChanged(const Guid &aId, SessionState_T aState);

    void i_onSnapshotTaken(const Guid &aMachineId, const Guid &aSnapshotId);
    void i_onSnapshotDeleted(const Guid &aMachineId, const Guid &aSnapshotId);
    void i_onSnapshotRestored(const Guid &aMachineId, const Guid &aSnapshotId);
    void i_onSnapshotChanged(const Guid &aMachineId, const Guid &aSnapshotId);

    void i_onGuestPropertyChanged(const Guid &aMachineId, const Utf8Str &aName, const Utf8Str &aValue, const Utf8Str &aFlags,
                                  const BOOL fWasDeleted);
    void i_onNatRedirectChanged(const Guid &aMachineId, ULONG ulSlot, bool fRemove, const Utf8Str &aName,
                                NATProtocol_T aProto, const Utf8Str &aHostIp, uint16_t aHostPort,
                                const Utf8Str &aGuestIp, uint16_t aGuestPort);
    void i_onNATNetworkChanged(const Utf8Str &aNetworkName);
    void i_onNATNetworkStartStop(const Utf8Str &aNetworkName, BOOL aStart);
    void i_onNATNetworkSetting(const Utf8Str &aNetworkName, BOOL aEnabled, const Utf8Str &aNetwork,
                               const Utf8Str &aGateway, BOOL aAdvertiseDefaultIpv6RouteEnabled,
                               BOOL fNeedDhcpServer);
    void i_onNATNetworkPortForward(const Utf8Str &aNetworkName, BOOL create, BOOL fIpv6,
                                   const Utf8Str &aRuleName, NATProtocol_T proto,
                                   const Utf8Str &aHostIp, LONG aHostPort,
                                   const Utf8Str &aGuestIp, LONG aGuestPort);
    void i_onHostNameResolutionConfigurationChange();

    int i_natNetworkRefInc(const Utf8Str &aNetworkName);
    int i_natNetworkRefDec(const Utf8Str &aNetworkName);

    RWLockHandle *i_getNatNetLock() const;
    bool i_isNatNetStarted(const Utf8Str &aNetworkName) const;

    void i_onCloudProviderListChanged(BOOL aRegistered);
    void i_onCloudProviderRegistered(const Utf8Str &aProviderId, BOOL aRegistered);
    void i_onCloudProviderUninstall(const Utf8Str &aProviderId);

    void i_onProgressCreated(const Guid &aId, BOOL aCreated);

    void i_onLanguageChanged(const Utf8Str &aLanguageId);

#ifdef VBOX_WITH_UPDATE_AGENT
    void i_onUpdateAgentAvailable(IUpdateAgent *aAgent,
                                  const Utf8Str &aVer, UpdateChannel_T aChannel, UpdateSeverity_T aSev,
                                  const Utf8Str &aDownloadURL, const Utf8Str &aWebURL, const Utf8Str &aReleaseNotes);
    void i_onUpdateAgentError(IUpdateAgent *aAgent, const Utf8Str &aErrMsg, LONG aRc);
    void i_onUpdateAgentStateChanged(IUpdateAgent *aAgent, UpdateState_T aState);
    void i_onUpdateAgentSettingsChanged(IUpdateAgent *aAgent, const Utf8Str &aAttributeHint);
#endif /* VBOX_WITH_UPDATE_AGENT */

#ifdef VBOX_WITH_EXTPACK
    void i_onExtPackInstalled(const Utf8Str &aExtPackName);
    void i_onExtPackUninstalled(const Utf8Str &aExtPackName);
#endif

#ifdef VBOX_WITH_CLOUD_NET
    HRESULT i_findCloudNetworkByName(const com::Utf8Str &aNetworkName,
                                     ComObjPtr<CloudNetwork> *aNetwork = NULL);
    HRESULT i_getEventSource(ComPtr<IEventSource>& aSource);
#endif /* VBOX_WITH_CLOUD_NET */

    ComObjPtr<GuestOSType> i_getUnknownOSType();

    void i_getOpenedMachines(SessionMachinesList &aMachines,
                           InternalControlList *aControls = NULL);
    MachinesOList &i_getMachinesList();

    HRESULT i_findMachine(const Guid &aId,
                          bool fPermitInaccessible,
                          bool aSetError,
                          ComObjPtr<Machine> *aMachine = NULL);

    HRESULT i_findMachineByName(const Utf8Str &aName,
                                bool aSetError,
                                ComObjPtr<Machine> *aMachine = NULL);

    HRESULT i_validateMachineGroup(const com::Utf8Str &aGroup, bool fPrimary);
    HRESULT i_convertMachineGroups(const std::vector<com::Utf8Str> aMachineGroups, StringsList *pllMachineGroups);

    HRESULT i_findHardDiskById(const Guid &id,
                               bool aSetError,
                               ComObjPtr<Medium> *aHardDisk = NULL);
    HRESULT i_findHardDiskByLocation(const Utf8Str &strLocation,
                                     bool aSetError,
                                     ComObjPtr<Medium> *aHardDisk = NULL);
    HRESULT i_findDVDOrFloppyImage(DeviceType_T mediumType,
                                   const Guid *aId,
                                   const Utf8Str &aLocation,
                                   bool aSetError,
                                   ComObjPtr<Medium> *aImage = NULL);
    HRESULT i_findRemoveableMedium(DeviceType_T mediumType,
                                   const Guid &uuid,
                                   bool fRefresh,
                                   bool aSetError,
                                   ComObjPtr<Medium> &pMedium);
    HRESULT i_findSharedFolder(const Utf8Str &aName,
                               ComPtr<ISharedFolder> &aSharedFolder);

    HRESULT i_findGuestOSType(const Utf8Str &strOSType,
                              ComObjPtr<GuestOSType> &guestOSType);

    const Guid &i_getGlobalRegistryId() const;

    const ComObjPtr<Host> &i_host() const;
    SystemProperties *i_getSystemProperties() const;
    CloudProviderManager *i_getCloudProviderManager() const;
#ifdef VBOX_WITH_EXTPACK
    ExtPackManager *i_getExtPackManager() const;
#endif
#ifdef VBOX_WITH_RESOURCE_USAGE_API
    const ComObjPtr<PerformanceCollector> &i_performanceCollector() const;
#endif /* VBOX_WITH_RESOURCE_USAGE_API */

    void i_getDefaultMachineFolder(Utf8Str &str) const;
    void i_getDefaultHardDiskFormat(Utf8Str &str) const;

    /** Returns the VirtualBox home directory */
    const Utf8Str &i_homeDir() const;
    int i_calculateFullPath(const Utf8Str &strPath, Utf8Str &aResult);
    void i_copyPathRelativeToConfig(const Utf8Str &strSource, Utf8Str &strTarget);
    HRESULT i_registerMedium(const ComObjPtr<Medium> &pMedium, ComObjPtr<Medium> *ppMedium,
                             AutoWriteLock &mediaTreeLock, bool fCalledFromMediumInit = false);
    HRESULT i_unregisterMedium(Medium *pMedium);
    HRESULT i_unregisterMachineMedia(const Guid &id);
    HRESULT i_unregisterMachine(Machine *pMachine, CleanupMode_T aCleanupMode, const Guid &id);
    void i_rememberMachineNameChangeForMedia(const Utf8Str &strOldConfigDir,
                                             const Utf8Str &strNewConfigDir);
    void i_saveMediaRegistry(settings::MediaRegistry &mediaRegistry,
                             const Guid &uuidRegistry,
                             const Utf8Str &strMachineFolder);
    HRESULT i_saveSettings();
    void i_markRegistryModified(const Guid &uuid);
    void i_unmarkRegistryModified(const Guid &uuid);
    void i_saveModifiedRegistries();
    static const com::Utf8Str &i_getVersionNormalized();
    static HRESULT i_ensureFilePathExists(const Utf8Str &strFileName, bool fCreate);
    const Utf8Str& i_settingsFilePath();
    AutostartDb* i_getAutostartDb() const;
    RWLockHandle& i_getMachinesListLockHandle();
    RWLockHandle& i_getMediaTreeLockHandle();
    int  i_encryptSetting(const Utf8Str &aPlaintext, Utf8Str *aCiphertext);
    int  i_decryptSetting(Utf8Str *aPlaintext, const Utf8Str &aCiphertext);
    void i_storeSettingsKey(const Utf8Str &aKey);
    bool i_isMediaUuidInUse(const Guid &aId, DeviceType_T deviceType);
    HRESULT i_retainCryptoIf(PCVBOXCRYPTOIF *ppCryptoIf);
    HRESULT i_releaseCryptoIf(PCVBOXCRYPTOIF pCryptoIf);
    HRESULT i_unloadCryptoIfModule(void);
    HRESULT i_getSupportedGuestOSTypes(std::vector<PlatformArchitecture_T> aArchitectures,
                                       std::vector<ComPtr<IGuestOSType> > &aGuestOSTypes);

private:
    class ClientWatcher;

    // wrapped IVirtualBox properties
    HRESULT getVersion(com::Utf8Str &aVersion);
    HRESULT getVersionNormalized(com::Utf8Str &aVersionNormalized);
    HRESULT getRevision(ULONG *aRevision);
    HRESULT getPackageType(com::Utf8Str &aPackageType);
    HRESULT getAPIVersion(com::Utf8Str &aAPIVersion);
    HRESULT getAPIRevision(LONG64 *aAPIRevision);
    HRESULT getHomeFolder(com::Utf8Str &aHomeFolder);
    HRESULT getSettingsFilePath(com::Utf8Str &aSettingsFilePath);
    HRESULT getHost(ComPtr<IHost> &aHost);
    HRESULT getPlatformProperties(PlatformArchitecture_T platformArchitecture, ComPtr<IPlatformProperties> &aPlatformProperties);
    HRESULT getSystemProperties(ComPtr<ISystemProperties> &aSystemProperties);
    HRESULT getMachines(std::vector<ComPtr<IMachine> > &aMachines);
    HRESULT getMachineGroups(std::vector<com::Utf8Str> &aMachineGroups);
    HRESULT getHardDisks(std::vector<ComPtr<IMedium> > &aHardDisks);
    HRESULT getDVDImages(std::vector<ComPtr<IMedium> > &aDVDImages);
    HRESULT getFloppyImages(std::vector<ComPtr<IMedium> > &aFloppyImages);
    HRESULT getProgressOperations(std::vector<ComPtr<IProgress> > &aProgressOperations);
    HRESULT getGuestOSFamilies(std::vector<com::Utf8Str> &aOSFamilies);
    HRESULT getGuestOSTypes(std::vector<ComPtr<IGuestOSType> > &aGuestOSTypes);
    HRESULT getSharedFolders(std::vector<ComPtr<ISharedFolder> > &aSharedFolders);
    HRESULT getPerformanceCollector(ComPtr<IPerformanceCollector> &aPerformanceCollector);
    HRESULT getDHCPServers(std::vector<ComPtr<IDHCPServer> > &aDHCPServers);
    HRESULT getNATNetworks(std::vector<ComPtr<INATNetwork> > &aNATNetworks);
    HRESULT getEventSource(ComPtr<IEventSource> &aEventSource);
    HRESULT getExtensionPackManager(ComPtr<IExtPackManager> &aExtensionPackManager);
    HRESULT getHostOnlyNetworks(std::vector<ComPtr<IHostOnlyNetwork> > &aHostOnlyNetworks);
    HRESULT getInternalNetworks(std::vector<com::Utf8Str> &aInternalNetworks);
    HRESULT getGenericNetworkDrivers(std::vector<com::Utf8Str> &aGenericNetworkDrivers);
    HRESULT getCloudNetworks(std::vector<ComPtr<ICloudNetwork> > &aCloudNetworks);
    HRESULT getCloudProviderManager(ComPtr<ICloudProviderManager> &aCloudProviderManager);

   // wrapped IVirtualBox methods
    HRESULT composeMachineFilename(const com::Utf8Str &aName,
                                   const com::Utf8Str &aGroup,
                                   const com::Utf8Str &aCreateFlags,
                                   const com::Utf8Str &aBaseFolder,
                                   com::Utf8Str &aFile);
    HRESULT createMachine(const com::Utf8Str &aSettingsFile,
                          const com::Utf8Str &aName,
                          PlatformArchitecture_T aArchitecture,
                          const std::vector<com::Utf8Str> &aGroups,
                          const com::Utf8Str &aOsTypeId,
                          const com::Utf8Str &aFlags,
                          const com::Utf8Str &aCipher,
                          const com::Utf8Str &aPasswordId,
                          const com::Utf8Str &aPassword,
                          ComPtr<IMachine> &aMachine);
    HRESULT openMachine(const com::Utf8Str &aSettingsFile,
                        const com::Utf8Str &aPassword,
                        ComPtr<IMachine> &aMachine);
    HRESULT registerMachine(const ComPtr<IMachine> &aMachine);
    HRESULT findMachine(const com::Utf8Str &aNameOrId,
                        ComPtr<IMachine> &aMachine);
    HRESULT getMachinesByGroups(const std::vector<com::Utf8Str> &aGroups,
                                std::vector<ComPtr<IMachine> > &aMachines);
    HRESULT getMachineStates(const std::vector<ComPtr<IMachine> > &aMachines,
                             std::vector<MachineState_T> &aStates);
    HRESULT createAppliance(ComPtr<IAppliance> &aAppliance);
    HRESULT createUnattendedInstaller(ComPtr<IUnattended> &aUnattended);
    HRESULT createMedium(const com::Utf8Str &aFormat,
                         const com::Utf8Str &aLocation,
                         AccessMode_T aAccessMode,
                         DeviceType_T aDeviceType,
                         ComPtr<IMedium> &aMedium);
    HRESULT openMedium(const com::Utf8Str &aLocation,
                       DeviceType_T aDeviceType,
                       AccessMode_T aAccessMode,
                       BOOL aForceNewUuid,
                       ComPtr<IMedium> &aMedium);
    HRESULT getGuestOSType(const com::Utf8Str &aId,
                           ComPtr<IGuestOSType> &aType);
    HRESULT getGuestOSSubtypesByFamilyId(const Utf8Str &strOSFamily,
                                         std::vector<com::Utf8Str> &aOSSubtypes);
    HRESULT getGuestOSDescsBySubtype(const Utf8Str &strOSSubtype,
                                     std::vector<com::Utf8Str> &aGuestOSDescs);
    HRESULT createSharedFolder(const com::Utf8Str &aName,
                               const com::Utf8Str &aHostPath,
                               BOOL aWritable,
                               BOOL aAutomount,
                               const com::Utf8Str &aAutoMountPoint);
    HRESULT removeSharedFolder(const com::Utf8Str &aName);
    HRESULT getExtraDataKeys(std::vector<com::Utf8Str> &aKeys);
    HRESULT getExtraData(const com::Utf8Str &aKey,
                         com::Utf8Str &aValue);
    HRESULT setExtraData(const com::Utf8Str &aKey,
                         const com::Utf8Str &aValue);
    HRESULT setSettingsSecret(const com::Utf8Str &aPassword);
    HRESULT createDHCPServer(const com::Utf8Str &aName,
                             ComPtr<IDHCPServer> &aServer);
    HRESULT findDHCPServerByNetworkName(const com::Utf8Str &aName,
                                        ComPtr<IDHCPServer> &aServer);
    HRESULT removeDHCPServer(const ComPtr<IDHCPServer> &aServer);
    HRESULT createNATNetwork(const com::Utf8Str &aNetworkName,
                             ComPtr<INATNetwork> &aNetwork);
    HRESULT findNATNetworkByName(const com::Utf8Str &aNetworkName,
                                 ComPtr<INATNetwork> &aNetwork);
    HRESULT removeNATNetwork(const ComPtr<INATNetwork> &aNetwork);
    HRESULT createHostOnlyNetwork(const com::Utf8Str &aNetworkName,
                                  ComPtr<IHostOnlyNetwork> &aNetwork);
    HRESULT findHostOnlyNetworkByName(const com::Utf8Str &aNetworkName,
                                     ComPtr<IHostOnlyNetwork> &aNetwork);
    HRESULT findHostOnlyNetworkById(const com::Guid &aId,
                                   ComPtr<IHostOnlyNetwork> &aNetwork);
    HRESULT removeHostOnlyNetwork(const ComPtr<IHostOnlyNetwork> &aNetwork);
    HRESULT createCloudNetwork(const com::Utf8Str &aNetworkName,
                               ComPtr<ICloudNetwork> &aNetwork);
    HRESULT findCloudNetworkByName(const com::Utf8Str &aNetworkName,
                                   ComPtr<ICloudNetwork> &aNetwork);
    HRESULT removeCloudNetwork(const ComPtr<ICloudNetwork> &aNetwork);
    HRESULT checkFirmwarePresent(PlatformArchitecture_T aPlatformArchitecture,
                                 FirmwareType_T aFirmwareType,
                                 const com::Utf8Str &aVersion,
                                 com::Utf8Str &aUrl,
                                 com::Utf8Str &aFile,
                                 BOOL *aResult);
    HRESULT findProgressById(const com::Guid &aId,
                             ComPtr<IProgress> &aProgressObject);
    HRESULT getTrackedObject(const com::Utf8Str &aTrObjId,
                             ComPtr<IUnknown> &aPIface,
                             TrackedObjectState_T *aState,
                             LONG64 *aCreationTime,
                             LONG64 *aDeletionTime);
    HRESULT getTrackedObjectIds (const com::Utf8Str& aName,
                                 std::vector<com::Utf8Str> &aObjIdsList);

    static HRESULT i_setErrorStaticBoth(HRESULT aResultCode, int vrc, const char *aText, ...)
    {
        va_list va;
        va_start (va, aText);
        HRESULT hrc = setErrorInternalV(aResultCode, getStaticClassIID(), getStaticComponentName(), aText, va, false, true, vrc);
        va_end(va);
        return hrc;
    }

    HRESULT i_registerMachine(Machine *aMachine);
    HRESULT i_registerDHCPServer(DHCPServer *aDHCPServer,
                                 bool aSaveRegistry = true);
    HRESULT i_unregisterDHCPServer(DHCPServer *aDHCPServer);
    HRESULT i_registerNATNetwork(NATNetwork *aNATNetwork,
                                 bool aSaveRegistry = true);
    HRESULT i_unregisterNATNetwork(NATNetwork *aNATNetwork,
                                   bool aSaveRegistry = true);
    HRESULT i_checkMediaForConflicts(const Guid &aId,
                                     const Utf8Str &aLocation,
                                     Utf8Str &aConflictType,
                                     ComObjPtr<Medium> *pDupMedium);
    int  i_decryptSettings();
    int  i_decryptMediumSettings(Medium *pMedium);
    int  i_decryptSettingBytes(uint8_t *aPlaintext,
                               const uint8_t *aCiphertext,
                               size_t aCiphertextSize) const;
    int  i_encryptSettingBytes(const uint8_t *aPlaintext,
                               uint8_t *aCiphertext,
                               size_t aPlaintextSize,
                               size_t aCiphertextSize) const;
    void i_reportDriverVersions(void);

    struct Data;            // opaque data structure, defined in VirtualBoxImpl.cpp

    Data *m;

    /* static variables (defined in VirtualBoxImpl.cpp) */
    static com::Utf8Str sVersion;
    static com::Utf8Str sVersionNormalized;
    static ULONG sRevision;
    static com::Utf8Str sPackageType;
    static com::Utf8Str sAPIVersion;
    static std::map<com::Utf8Str, int> sNatNetworkNameToRefCount;
    static RWLockHandle* spMtxNatNetworkNameToRefCountLock;

    static DECLCALLBACK(int) AsyncEventHandler(RTTHREAD thread, void *pvUser);

#ifdef RT_OS_WINDOWS
    friend class StartSVCHelperClientData;
    static void i_SVCHelperClientThreadTask(StartSVCHelperClientData *pTask);
#endif

#if defined(RT_OS_WINDOWS) && defined(VBOXSVC_WITH_CLIENT_WATCHER)
protected:
    void i_callHook(const char *a_pszFunction) RT_OVERRIDE;
    bool i_watchClientProcess(RTPROCESS a_pidClient, const char *a_pszFunction);
public:
    static void i_logCaller(const char *a_pszFormat, ...);
private:

#endif
};

////////////////////////////////////////////////////////////////////////////////

#endif /* !MAIN_INCLUDED_VirtualBoxImpl_h */
