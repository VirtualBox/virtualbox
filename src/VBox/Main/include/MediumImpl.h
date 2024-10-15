/* $Id: MediumImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef MAIN_INCLUDED_MediumImpl_h
#define MAIN_INCLUDED_MediumImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/vd.h>
#include "MediumWrap.h"
#include "VirtualBoxBase.h"
#include "AutoCaller.h"
#include "SecretKeyStore.h"
class Progress;
class MediumFormat;
class MediumLockList;
struct MediumCryptoFilterSettings;

namespace settings
{
    struct Medium;
}

////////////////////////////////////////////////////////////////////////////////

/**
 * Medium component class for all media types.
 */
class ATL_NO_VTABLE Medium :
    public MediumWrap
{
public:
    DECLARE_COMMON_CLASS_METHODS(Medium)

    HRESULT FinalConstruct();
    void FinalRelease();

    enum HDDOpenMode  { OpenReadWrite, OpenReadOnly };
                // have to use a special enum for the overloaded init() below;
                // can't use AccessMode_T from XIDL because that's mapped to an int
                // and would be ambiguous

    // public initializer/uninitializer for internal purposes only

    // initializer to create empty medium (VirtualBox::CreateMedium())
    HRESULT init(VirtualBox *aVirtualBox,
                 const Utf8Str &aFormat,
                 const Utf8Str &aLocation,
                 const Guid &uuidMachineRegistry,
                 const DeviceType_T aDeviceType);

    // initializer for opening existing media
    // (VirtualBox::OpenMedium(); Machine::AttachDevice())
    HRESULT init(VirtualBox *aVirtualBox,
                 const Utf8Str &aLocation,
                 HDDOpenMode enOpenMode,
                 bool fForceNewUuid,
                 DeviceType_T aDeviceType);

    // initializer used when loading settings
    HRESULT initOne(Medium *aParent,
                    DeviceType_T aDeviceType,
                    const Guid &uuidMachineRegistry,
                    const Utf8Str &strMachineFolder,
                    const settings::Medium &data);
    static HRESULT initFromSettings(VirtualBox *aVirtualBox,
                                    DeviceType_T aDeviceType,
                                    const Guid &uuidMachineRegistry,
                                    const Utf8Str &strMachineFolder,
                                    const settings::Medium &data,
                                    AutoWriteLock &mediaTreeLock,
                                    std::list<std::pair<Guid, DeviceType_T> > &uIdsForNotify);

    // initializer for host floppy/DVD
    HRESULT init(VirtualBox *aVirtualBox,
                 DeviceType_T aDeviceType,
                 const Utf8Str &aLocation,
                 const Utf8Str &aDescription = Utf8Str::Empty);

    void uninit();

    void i_deparent();
    void i_setParent(const ComObjPtr<Medium> &pParent);

    // unsafe methods for internal purposes only (ensure there is
    // a caller and a read lock before calling them!)
    const ComObjPtr<Medium>& i_getParent() const;
    const MediaList& i_getChildren() const;

    const Guid& i_getId() const;
    MediumState_T i_getState() const;
    MediumVariant_T i_getVariant() const;
    bool i_isHostDrive() const;
    bool i_isClosing() const;
    const Utf8Str& i_getLocationFull() const;
    const Utf8Str& i_getFormat() const;
    const ComObjPtr<MediumFormat> & i_getMediumFormat() const;
    bool i_isMediumFormatFile() const;
    uint64_t i_getSize() const;
    uint64_t i_getLogicalSize() const;
    DeviceType_T i_getDeviceType() const;
    MediumType_T i_getType() const;
    Utf8Str i_getName();

    /* handles caller/locking itself */
    bool i_addRegistry(const Guid &id);
    bool i_addRegistryNoCallerCheck(const Guid &id);
    /* handles caller/locking itself, caller is responsible for tree lock */
    bool i_addRegistryAll(const Guid &id);
    /* handles caller/locking itself */
    bool i_removeRegistry(const Guid& id);
    /* handles caller/locking itself, caller is responsible for tree lock */
    bool i_removeRegistryAll(const Guid& id);
    bool i_isInRegistry(const Guid& id);
    bool i_getFirstRegistryMachineId(Guid &uuid) const;
    void i_markRegistriesModified();

    HRESULT i_setPropertyDirect(const Utf8Str &aName, const Utf8Str &aValue);

    HRESULT i_addBackReference(const Guid &aMachineId,
                               const Guid &aSnapshotId = Guid::Empty);
    HRESULT i_removeBackReference(const Guid &aMachineId,
                                  const Guid &aSnapshotId = Guid::Empty);


    const Guid* i_getFirstMachineBackrefId() const;
    const Guid* i_getAnyMachineBackref(const Guid &aId) const;
    const Guid* i_getFirstMachineBackrefSnapshotId() const;
    size_t i_getMachineBackRefCount() const;

#ifdef DEBUG
    void i_dumpBackRefs();
#endif

    HRESULT i_updatePath(const Utf8Str &strOldPath, const Utf8Str &strNewPath);

    /* handles caller/locking itself */
    ComObjPtr<Medium> i_getBase(uint32_t *aLevel = NULL);
    /* handles caller/locking itself */
    uint32_t i_getDepth();

    bool i_isReadOnly();
    void i_updateId(const Guid &id);

    void i_saveSettingsOne(settings::Medium &data,
                           const Utf8Str &strHardDiskFolder);
    HRESULT i_saveSettings(settings::Medium &data,
                           const Utf8Str &strHardDiskFolder);

    HRESULT i_createMediumLockList(bool fFailIfInaccessible,
                                   Medium *pToLock,
                                   bool fMediumLockWriteAll,
                                   Medium *pToBeParent,
                                   MediumLockList &mediumLockList);

    HRESULT i_createDiffStorage(ComObjPtr<Medium> &aTarget,
                                MediumVariant_T aVariant,
                                MediumLockList *pMediumLockList,
                                ComObjPtr<Progress> *aProgress,
                                bool aWait,
                                bool aNotify);
    Utf8Str i_getPreferredDiffFormat();
    MediumVariant_T i_getPreferredDiffVariant();

    HRESULT i_close(AutoCaller &autoCaller);
    HRESULT i_unlockRead(MediumState_T *aState);
    HRESULT i_unlockWrite(MediumState_T *aState);
    HRESULT i_deleteStorage(ComObjPtr<Progress> *aProgress, bool aWait, bool aNotify);
    HRESULT i_markForDeletion();
    HRESULT i_unmarkForDeletion();
    HRESULT i_markLockedForDeletion();
    HRESULT i_unmarkLockedForDeletion();

    HRESULT i_queryPreferredMergeDirection(const ComObjPtr<Medium> &pOther,
                                           bool &fMergeForward);

    HRESULT i_prepareMergeTo(const ComObjPtr<Medium> &pTarget,
                             const Guid *aMachineId,
                             const Guid *aSnapshotId,
                             bool fLockMedia,
                             bool &fMergeForward,
                             ComObjPtr<Medium> &pParentForTarget,
                             MediumLockList * &aChildrenToReparent,
                             MediumLockList * &aMediumLockList);
    HRESULT i_mergeTo(const ComObjPtr<Medium> &pTarget,
                      bool fMergeForward,
                      const ComObjPtr<Medium> &pParentForTarget,
                      MediumLockList *aChildrenToReparent,
                      MediumLockList *aMediumLockList,
                      ComObjPtr<Progress> *aProgress,
                      bool aWait,
                      bool aNotify);
    void i_cancelMergeTo(MediumLockList *aChildrenToReparent,
                       MediumLockList *aMediumLockList);

    HRESULT i_resize(uint64_t aLogicalSize,
                     MediumLockList *aMediumLockList,
                     ComObjPtr<Progress> *aProgress,
                     bool aWait,
                     bool aNotify);

    HRESULT i_fixParentUuidOfChildren(MediumLockList *pChildrenToReparent);

    HRESULT i_addRawToFss(const char *aFilename, SecretKeyStore *pKeyStore, RTVFSFSSTREAM hVfsFssDst,
                          const ComObjPtr<Progress> &aProgress, bool fSparse);

    HRESULT i_exportFile(const char *aFilename,
                         const ComObjPtr<MediumFormat> &aFormat,
                         MediumVariant_T aVariant,
                         SecretKeyStore *pKeyStore,
                         RTVFSIOSTREAM hVfsIosDst,
                         const ComObjPtr<Progress> &aProgress);
    HRESULT i_importFile(const char *aFilename,
                         const ComObjPtr<MediumFormat> &aFormat,
                         MediumVariant_T aVariant,
                         RTVFSIOSTREAM hVfsIosSrc,
                         const ComObjPtr<Medium> &aParent,
                         const ComObjPtr<Progress> &aProgress,
                         bool aNotify);

    HRESULT i_cloneToEx(const ComObjPtr<Medium> &aTarget, MediumVariant_T aVariant,
                        const ComObjPtr<Medium> &aParent, IProgress **aProgress,
                        uint32_t idxSrcImageSame, uint32_t idxDstImageSame, bool aNotify);

    const Utf8Str& i_getKeyId();

    HRESULT i_openForIO(bool fWritable, SecretKeyStore *pKeyStore, PVDISK *ppHdd, MediumLockList *pMediumLockList,
                        struct MediumCryptoFilterSettings *pCryptoSettings);

private:

    // wrapped IMedium properties
    HRESULT getId(com::Guid &aId);
    HRESULT getDescription(AutoCaller &autoCaller, com::Utf8Str &aDescription);
    HRESULT setDescription(AutoCaller &autoCaller, const com::Utf8Str &aDescription);
    HRESULT getState(MediumState_T *aState);
    HRESULT getVariant(std::vector<MediumVariant_T> &aVariant);
    HRESULT getLocation(com::Utf8Str &aLocation);
    HRESULT setLocation(const com::Utf8Str &aLocation);
    HRESULT getName(com::Utf8Str &aName);
    HRESULT getDeviceType(DeviceType_T *aDeviceType);
    HRESULT getHostDrive(BOOL *aHostDrive);
    HRESULT getSize(LONG64 *aSize);
    HRESULT getFormat(com::Utf8Str &aFormat);
    HRESULT getMediumFormat(ComPtr<IMediumFormat> &aMediumFormat);
    HRESULT getType(AutoCaller &autoCaller, MediumType_T *aType);
    HRESULT setType(AutoCaller &autoCaller, MediumType_T aType);
    HRESULT getAllowedTypes(std::vector<MediumType_T> &aAllowedTypes);
    HRESULT getParent(AutoCaller &autoCaller, ComPtr<IMedium> &aParent);
    HRESULT getChildren(AutoCaller &autoCaller, std::vector<ComPtr<IMedium> > &aChildren);
    HRESULT getBase(AutoCaller &autoCaller, ComPtr<IMedium> &aBase);
    HRESULT getReadOnly(AutoCaller &autoCaller, BOOL *aReadOnly);
    HRESULT getLogicalSize(LONG64 *aLogicalSize);
    HRESULT getAutoReset(BOOL *aAutoReset);
    HRESULT setAutoReset(BOOL aAutoReset);
    HRESULT getLastAccessError(com::Utf8Str &aLastAccessError);
    HRESULT getMachineIds(std::vector<com::Guid> &aMachineIds);

    // wrapped IMedium methods
    HRESULT setIds(AutoCaller &aAutoCaller,
                   BOOL aSetImageId,
                   const com::Guid &aImageId,
                   BOOL aSetParentId,
                   const com::Guid &aParentId);
    HRESULT refreshState(AutoCaller &aAutoCaller,
                         MediumState_T *aState);
    HRESULT getSnapshotIds(const com::Guid &aMachineId,
                           std::vector<com::Guid> &aSnapshotIds);
    HRESULT lockRead(ComPtr<IToken> &aToken);
    HRESULT lockWrite(ComPtr<IToken> &aToken);
    HRESULT close(AutoCaller &aAutoCaller);
    HRESULT getProperty(const com::Utf8Str &aName,
                        com::Utf8Str &aValue);
    HRESULT setProperty(const com::Utf8Str &aName,
                        const com::Utf8Str &aValue);
    HRESULT getProperties(const com::Utf8Str &aNames,
                          std::vector<com::Utf8Str> &aReturnNames,
                          std::vector<com::Utf8Str> &aReturnValues);
    HRESULT setProperties(const std::vector<com::Utf8Str> &aNames,
                          const std::vector<com::Utf8Str> &aValues);
    HRESULT createBaseStorage(LONG64 aLogicalSize,
                              const std::vector<MediumVariant_T> &aVariant,
                              ComPtr<IProgress> &aProgress);
    HRESULT deleteStorage(ComPtr<IProgress> &aProgress);
    HRESULT createDiffStorage(AutoCaller &autoCaller,
                              const ComPtr<IMedium> &aTarget,
                              const std::vector<MediumVariant_T> &aVariant,
                              ComPtr<IProgress> &aProgress);
    HRESULT mergeTo(const ComPtr<IMedium> &aTarget,
                    ComPtr<IProgress> &aProgress);
    HRESULT cloneTo(const ComPtr<IMedium> &aTarget,
                    const std::vector<MediumVariant_T> &aVariant,
                    const ComPtr<IMedium> &aParent,
                    ComPtr<IProgress> &aProgress);
    HRESULT resizeAndCloneTo(const ComPtr<IMedium> &aTarget,
                             LONG64 aLogicalSize,
                             const std::vector<MediumVariant_T> &aVariant,
                             const ComPtr<IMedium> &aParent,
                             ComPtr<IProgress> &aProgress);
    HRESULT cloneToBase(const ComPtr<IMedium> &aTarget,
                        const std::vector<MediumVariant_T> &aVariant,
                        ComPtr<IProgress> &aProgress);
    HRESULT moveTo(AutoCaller &autoCaller,
                   const com::Utf8Str &aLocation,
                   ComPtr<IProgress> &aProgress);
    HRESULT compact(ComPtr<IProgress> &aProgress);
    HRESULT resize(LONG64 aLogicalSize,
                   ComPtr<IProgress> &aProgress);
    HRESULT reset(AutoCaller &autoCaller, ComPtr<IProgress> &aProgress);
    HRESULT changeEncryption(const com::Utf8Str &aCurrentPassword, const com::Utf8Str &aCipher,
                             const com::Utf8Str &aNewPassword, const com::Utf8Str &aNewPasswordId,
                             ComPtr<IProgress> &aProgress);
    HRESULT getEncryptionSettings(AutoCaller &autoCaller, com::Utf8Str &aCipher, com::Utf8Str &aPasswordId);
    HRESULT checkEncryptionPassword(const com::Utf8Str &aPassword);
    HRESULT openForIO(BOOL aWritable, com::Utf8Str const &aPassword, ComPtr<IMediumIO> &aMediumIO);

    // Private internal nmethods
    HRESULT i_queryInfo(bool fSetImageId, bool fSetParentId, AutoCaller &autoCaller);
    HRESULT i_canClose();
    HRESULT i_unregisterWithVirtualBox();
    HRESULT i_setStateError();
    HRESULT i_setLocation(const Utf8Str &aLocation, const Utf8Str &aFormat = Utf8Str::Empty);
    HRESULT i_setFormat(const Utf8Str &aFormat);
    VDTYPE i_convertDeviceType();
    DeviceType_T i_convertToDeviceType(VDTYPE enmType);
    Utf8Str i_vdError(int aVRC);

    bool    i_isPropertyForFilter(const com::Utf8Str &aName);

    HRESULT i_getFilterProperties(std::vector<com::Utf8Str> &aReturnNames,
                                  std::vector<com::Utf8Str> &aReturnValues);

    HRESULT i_preparationForMoving(const Utf8Str &aLocation);
    bool    i_isMoveOperation(const ComObjPtr<Medium> &pTarget) const;
    bool    i_resetMoveOperationData();
    Utf8Str i_getNewLocationForMoving() const;

    static DECLCALLBACK(void) i_vdErrorCall(void *pvUser, int vrc, RT_SRC_POS_DECL,
                                            const char *pszFormat, va_list va);
    static DECLCALLBACK(bool) i_vdConfigAreKeysValid(void *pvUser,
                                                     const char *pszzValid);
    static DECLCALLBACK(int) i_vdConfigQuerySize(void *pvUser, const char *pszName,
                                                 size_t *pcbValue);
    static DECLCALLBACK(int) i_vdConfigUpdate(void *pvUser, bool fCreate,
                                            const char *pszName, const char *pszValue);

    static DECLCALLBACK(int) i_vdConfigQuery(void *pvUser, const char *pszName,
                                             char *pszValue, size_t cchValue);

    static DECLCALLBACK(bool) i_vdCryptoConfigAreKeysValid(void *pvUser,
                                                           const char *pszzValid);
    static DECLCALLBACK(int) i_vdCryptoConfigQuerySize(void *pvUser, const char *pszName,
                                                       size_t *pcbValue);
    static DECLCALLBACK(int) i_vdCryptoConfigQuery(void *pvUser, const char *pszName,
                                                   char *pszValue, size_t cchValue);

    static DECLCALLBACK(int) i_vdCryptoKeyRetain(void *pvUser, const char *pszId,
                                                 const uint8_t **ppbKey, size_t *pcbKey);
    static DECLCALLBACK(int) i_vdCryptoKeyRelease(void *pvUser, const char *pszId);
    static DECLCALLBACK(int) i_vdCryptoKeyStorePasswordRetain(void *pvUser, const char *pszId, const char **ppszPassword);
    static DECLCALLBACK(int) i_vdCryptoKeyStorePasswordRelease(void *pvUser, const char *pszId);
    static DECLCALLBACK(int) i_vdCryptoKeyStoreSave(void *pvUser, const void *pvKeyStore, size_t cbKeyStore);
    static DECLCALLBACK(int) i_vdCryptoKeyStoreReturnParameters(void *pvUser, const char *pszCipher,
                                                                const uint8_t *pbDek, size_t cbDek);

    class Task;
    class CreateBaseTask;
    class CreateDiffTask;
    class CloneTask;
    class MoveTask;
    class CompactTask;
    class ResizeTask;
    class ResetTask;
    class DeleteTask;
    class MergeTask;
    class ImportTask;
    class EncryptTask;
    friend class Task;
    friend class CreateBaseTask;
    friend class CreateDiffTask;
    friend class CloneTask;
    friend class MoveTask;
    friend class CompactTask;
    friend class ResizeTask;
    friend class ResetTask;
    friend class DeleteTask;
    friend class MergeTask;
    friend class ImportTask;
    friend class EncryptTask;

    HRESULT i_taskCreateBaseHandler(Medium::CreateBaseTask &task);
    HRESULT i_taskCreateDiffHandler(Medium::CreateDiffTask &task);
    HRESULT i_taskMergeHandler(Medium::MergeTask &task);
    HRESULT i_taskCloneHandler(Medium::CloneTask &task);
    HRESULT i_taskMoveHandler(Medium::MoveTask &task);
    HRESULT i_taskDeleteHandler(Medium::DeleteTask &task);
    HRESULT i_taskResetHandler(Medium::ResetTask &task);
    HRESULT i_taskCompactHandler(Medium::CompactTask &task);
    HRESULT i_taskResizeHandler(Medium::ResizeTask &task);
    HRESULT i_taskImportHandler(Medium::ImportTask &task);
    HRESULT i_taskEncryptHandler(Medium::EncryptTask &task);

    void i_taskEncryptSettingsSetup(struct MediumCryptoFilterSettings *pSettings, const char *pszCipher,
                                    const char *pszKeyStore,  const char *pszPassword,
                                    bool fCreateKeyStore);

    struct Data;            // opaque data struct, defined in MediumImpl.cpp
    Data *m;
};


/**
 * Settings for a crypto filter instance.
 */
struct MediumCryptoFilterSettings
{
    MediumCryptoFilterSettings()
        : fCreateKeyStore(false),
          pszPassword(NULL),
          pszKeyStore(NULL),
          pszKeyStoreLoad(NULL),
          pbDek(NULL),
          cbDek(0),
          pszCipher(NULL),
          pszCipherReturned(NULL)
    { }

    bool              fCreateKeyStore;
    const char        *pszPassword;
    char              *pszKeyStore;
    const char        *pszKeyStoreLoad;

    const uint8_t     *pbDek;
    size_t            cbDek;
    const char        *pszCipher;

    /** The cipher returned by the crypto filter. */
    char              *pszCipherReturned;

    PVDINTERFACE      vdFilterIfaces;

    VDINTERFACECONFIG vdIfCfg;
    VDINTERFACECRYPTO vdIfCrypto;
};



#endif /* !MAIN_INCLUDED_MediumImpl_h */

