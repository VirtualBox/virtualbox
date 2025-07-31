/* $Id: ApplianceImpl.h 110133 2025-07-07 12:49:50Z brent.paulson@oracle.com $ */
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

#ifndef MAIN_INCLUDED_ApplianceImpl_h
#define MAIN_INCLUDED_ApplianceImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

/* VBox includes */
#include "VirtualSystemDescriptionWrap.h"
#include "ApplianceWrap.h"
#include "MediumFormatImpl.h"

/** @todo This file needs massive cleanup. Split IAppliance in a public and
 * private classes. */
#include "ovfreader.h"
#include <set>

/* VBox forward declarations */
class Certificate;
class Progress;
class VirtualSystemDescription;
struct VirtualSystemDescriptionEntry;
struct LocationInfo;
typedef struct VDINTERFACE   *PVDINTERFACE;
typedef struct VDINTERFACEIO *PVDINTERFACEIO;
typedef struct SHASTORAGE    *PSHASTORAGE;

namespace ovf
{
    struct HardDiskController;
    struct VirtualSystem;
    class  OVFReader;
    struct DiskImage;
    struct EnvelopeData;
}

namespace xml
{
    class Document;
    class ElementNode;
}

namespace settings
{
    class MachineConfigFile;
}

class ATL_NO_VTABLE Appliance :
    public ApplianceWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(Appliance)

    HRESULT FinalConstruct();
    void FinalRelease();


    HRESULT init(VirtualBox *aVirtualBox);
    void uninit();

    /* public methods only for internal purposes */

    static HRESULT i_setErrorStatic(HRESULT aResultCode, const char *aText, ...)
    {
        va_list va;
        va_start(va, aText);
        HRESULT hrc = setErrorInternalV(aResultCode, getStaticClassIID(), getStaticComponentName(), aText, va, false, true);
        va_end(va);
        return hrc;
    }

    /* private instance data */
private:
    // wrapped IAppliance properties
    HRESULT getPath(com::Utf8Str &aPath);
    HRESULT getDisks(std::vector<com::Utf8Str> &aDisks);
    HRESULT getCertificate(ComPtr<ICertificate> &aCertificateInfo);
    HRESULT getVirtualSystemDescriptions(std::vector<ComPtr<IVirtualSystemDescription> > &aVirtualSystemDescriptions);
    HRESULT getMachines(std::vector<com::Utf8Str> &aMachines);

    // wrapped IAppliance methods
    HRESULT read(const com::Utf8Str &aFile,
                 ComPtr<IProgress> &aProgress);
    HRESULT interpret();
    HRESULT importMachines(const std::vector<ImportOptions_T> &aOptions,
                           ComPtr<IProgress> &aProgress);
    HRESULT createVFSExplorer(const com::Utf8Str &aURI,
                              ComPtr<IVFSExplorer> &aExplorer);
    HRESULT write(const com::Utf8Str &aFormat,
                  const std::vector<ExportOptions_T> &aOptions,
                  const com::Utf8Str &aPath,
                  ComPtr<IProgress> &aProgress);
    HRESULT getWarnings(std::vector<com::Utf8Str> &aWarnings);
    HRESULT getPasswordIds(std::vector<com::Utf8Str> &aIdentifiers);
    HRESULT getMediumIdsForPasswordId(const com::Utf8Str &aPasswordId, std::vector<com::Guid> &aIdentifiers);
    HRESULT addPasswords(const std::vector<com::Utf8Str> &aIdentifiers,
                         const std::vector<com::Utf8Str> &aPasswords);
    HRESULT createVirtualSystemDescriptions(ULONG aRequested, ULONG *aCreated);
    /** weak VirtualBox parent */
    VirtualBox* const mVirtualBox;

    struct ImportStack;
    class TaskOVF;
    class TaskOPC;
    class TaskCloud;

    struct Data;            // opaque, defined in ApplianceImplPrivate.h
    Data *m;

    enum SetUpProgressMode { ImportFile, ImportS3, WriteFile, WriteS3, ExportCloud, ImportCloud };

    enum ApplianceState { ApplianceIdle, ApplianceImporting, ApplianceExporting };
    void i_setApplianceState(const ApplianceState &state);
    /** @name General stuff
     * @{
     */
    bool i_isApplianceIdle();
    HRESULT i_searchUniqueVMName(Utf8Str &aName) const;
    HRESULT i_ensureUniqueImageFilePath(const Utf8Str &aMachineFolder,
                                        DeviceType_T aDeviceType,
                                        Utf8Str &aName) const;
    HRESULT i_setUpProgress(ComObjPtr<Progress> &pProgress,
                            const Utf8Str &strDescription,
                            SetUpProgressMode mode);
    void i_addWarning(const char* aWarning, ...);
    void i_disksWeight();
    void i_parseBucket(Utf8Str &aPath, Utf8Str &aBucket);

    static void i_importOrExportThreadTask(TaskOVF *pTask);
    static void i_exportOPCThreadTask(TaskOPC *pTask);
    static void i_importOrExportCloudThreadTask(TaskCloud *pTask);

    HRESULT i_initBackendNames();

    Utf8Str i_typeOfVirtualDiskFormatFromURI(Utf8Str type) const;

#if 0 /* unused */
    std::set<Utf8Str> i_URIFromTypeOfVirtualDiskFormat(Utf8Str type);
#endif

    HRESULT i_findMediumFormatFromDiskImage(const ovf::DiskImage &di, ComObjPtr<MediumFormat>& mf);

    RTVFSIOSTREAM i_manifestSetupDigestCalculationForGivenIoStream(RTVFSIOSTREAM hVfsIos, const char *pszManifestEntry,
                                                                   bool fRead = true);
    /** @}  */

    /** @name Read stuff
     * @{
     */
    HRESULT i_readImpl(const LocationInfo &aLocInfo, ComObjPtr<Progress> &aProgress);

    HRESULT i_readFS(TaskOVF *pTask);
    HRESULT i_readFSOVF(TaskOVF *pTask);
    HRESULT i_readFSOVA(TaskOVF *pTask);
    HRESULT i_readOVFFile(TaskOVF *pTask, RTVFSIOSTREAM hIosOvf, const char *pszManifestEntry);
    HRESULT i_readManifestFile(TaskOVF *pTask, RTVFSIOSTREAM hIosMf, const char *pszSubFileNm);
    HRESULT i_readSignatureFile(TaskOVF *pTask, RTVFSIOSTREAM hIosCert, const char *pszSubFileNm);
    HRESULT i_readTailProcessing(TaskOVF *pTask);
    HRESULT i_readTailProcessingGetManifestData(void **ppvData, size_t *pcbData);
    HRESULT i_readTailProcessingSignedData(PRTERRINFOSTATIC pErrInfo);
    HRESULT i_readTailProcessingVerifySelfSignedOvfCert(TaskOVF *pTask, RTCRSTORE hTrustedCerts, PRTERRINFOSTATIC pErrInfo);
    HRESULT i_readTailProcessingVerifyIssuedOvfCert(TaskOVF *pTask, RTCRSTORE hTrustedStore, PRTERRINFOSTATIC pErrInfo);
    HRESULT i_readTailProcessingVerifyContentInfoCerts(void const *pvData, size_t cbData,
                                                       RTCRSTORE hTrustedStore, PRTERRINFOSTATIC pErrInfo);
    HRESULT i_readTailProcessingVerifyAnalyzeSignerInfo(void const *pvData, size_t cbData, RTCRSTORE hTrustedStore,
                                                        uint32_t iSigner, PRTTIMESPEC pNow, int vrc,
                                                        PRTERRINFOSTATIC pErrInfo, PRTCRSTORE phTrustedStore2);
    HRESULT i_readTailProcessingVerifyContentInfoFailOne(const char *pszSignature, int vrc, PRTERRINFOSTATIC pErrInfo);

    HRESULT i_gettingCloudData(TaskCloud *pTask);
    /** @}  */

    /** @name Import stuff
     * @{
     */
    HRESULT i_importImpl(const LocationInfo &aLocInfo, ComObjPtr<Progress> &aProgress);

    HRESULT i_importFS(TaskOVF *pTask);
    HRESULT i_importFSOVF(TaskOVF *pTask, AutoWriteLockBase &rWriteLock);
    HRESULT i_importFSOVA(TaskOVF *pTask, AutoWriteLockBase &rWriteLock);
    HRESULT i_importDoIt(TaskOVF *pTask, AutoWriteLockBase &rWriteLock, RTVFSFSSTREAM hVfsFssOva = NIL_RTVFSFSSTREAM);

    HRESULT i_verifyManifestFile(ImportStack &stack);

    void i_convertDiskAttachmentValues(const ovf::HardDiskController &hdc,
                                       uint32_t ulAddressOnParent,
                                       Utf8Str &controllerName,
                                       int32_t &lControllerPort,
                                       int32_t &lDevice);

    void i_importOneDiskImage(const ovf::DiskImage &di,
                              const Utf8Str &strDstPath,
                              ComObjPtr<Medium> &pTargetMedium,
                              ImportStack &stack);

    void i_importMachineGeneric(const ovf::VirtualSystem &vsysThis,
                                ComObjPtr<VirtualSystemDescription> &vsdescThis,
                                ComPtr<IMachine> &pNewMachineRet,
                                ImportStack &stack);
    void i_importVBoxMachine(ComObjPtr<VirtualSystemDescription> &vsdescThis,
                             ComPtr<IMachine> &pNewMachine,
                             ImportStack &stack);
    void i_importMachines(ImportStack &stack);
    HRESULT i_verifyStorageControllerPortValid(const StorageControllerType_T aStorageControllerType,
                                               const uint32_t aControllerPort,
                                               ULONG *ulMaxPorts);

    HRESULT i_preCheckImageAvailability(ImportStack &stack);
    bool    i_importEnsureOvaLookAhead(ImportStack &stack);
    RTVFSIOSTREAM i_importOpenSourceFile(ImportStack &stack, Utf8Str const &rstrSrcPath, const char *pszManifestEntry);
    HRESULT i_importCreateAndWriteDestinationFile(Utf8Str const &rstrDstPath,
                                                  RTVFSIOSTREAM hVfsIosSrc, Utf8Str const &rstrSrcLogNm);

    void    i_importCopyFile(ImportStack &stack, Utf8Str const &rstrSrcPath, Utf8Str const &rstrDstPath,
                             const char *pszManifestEntry);
    void    i_importDecompressFile(ImportStack &stack, Utf8Str const &rstrSrcPath, Utf8Str const &rstrDstPath,
                                   const char *pszManifestEntry);
    HRESULT i_importCloudImpl(TaskCloud *pTask);
    /** @} */

    /** @name Write stuff
     * @{
     */
    HRESULT i_writeImpl(ovf::OVFVersion_T aFormat, const LocationInfo &aLocInfo, ComObjPtr<Progress> &aProgress);
    HRESULT i_writeOPCImpl(ovf::OVFVersion_T aFormat, const LocationInfo &aLocInfo, ComObjPtr<Progress> &aProgress);
    HRESULT i_writeCloudImpl(const LocationInfo &aLocInfo, ComObjPtr<Progress> &aProgress);

    HRESULT i_writeFS(TaskOVF *pTask);
    HRESULT i_writeFSOVF(TaskOVF *pTask, AutoWriteLockBase& writeLock);
    HRESULT i_writeFSOVA(TaskOVF *pTask, AutoWriteLockBase& writeLock);
    HRESULT i_writeFSOPC(TaskOPC *pTask);
    HRESULT i_exportCloudImpl(TaskCloud *pTask);
    HRESULT i_writeFSImpl(TaskOVF *pTask, AutoWriteLockBase &writeLock, RTVFSFSSTREAM hVfsFssDst);
    HRESULT i_writeBufferToFile(RTVFSFSSTREAM hVfsFssDst, const char *pszFilename, const void *pvContent, size_t cbContent);
    HRESULT i_writeNvramFileToIoStream(Utf8Str strNvramSrcPath, Utf8Str strNvramDstFile, RTVFSFSSTREAM hVfsFssDst);

    struct XMLStack;

    void i_buildXML(AutoWriteLockBase& writeLock,
                    xml::Document &doc,
                    XMLStack &stack,
                    ovf::OVFVersion_T enFormat);
    void i_buildXMLForOneVirtualSystem(AutoWriteLockBase& writeLock,
                                       xml::ElementNode &elmToAddVirtualSystemsTo,
                                       std::list<xml::ElementNode*> *pllElementsWithUuidAttributes,
                                       uint32_t &ulFile,
                                       ComObjPtr<VirtualSystemDescription> &vsdescThis,
                                       ovf::OVFVersion_T enFormat,
                                       XMLStack &stack);
    /** @} */

    friend class Machine;
    friend class Certificate;
};

void i_parseURI(Utf8Str strUri, LocationInfo &locInfo);

struct VirtualSystemDescriptionEntry
{
    uint32_t ulIndex;                       ///< zero-based index of this entry within array
    VirtualSystemDescriptionType_T type;    ///< type of this entry
    Utf8Str strRef;                         ///< reference number (hard disk controllers only)
    Utf8Str strOvf;                         ///< original OVF value (type-dependent)
    Utf8Str strVBoxSuggested;               ///< configuration value (type-dependent); original value suggested by interpret()
    Utf8Str strVBoxCurrent;                 ///< configuration value (type-dependent); current value, either from interpret() or setFinalValue()
    Utf8Str strExtraConfigSuggested;        ///< extra configuration key=value strings (type-dependent); original value suggested by interpret()
    Utf8Str strExtraConfigCurrent;          ///< extra configuration key=value strings (type-dependent); current value, either from interpret() or setFinalValue()

    uint32_t ulSizeMB;                      ///< hard disk images only: a copy of ovf::DiskImage::ulSuggestedSizeMB
    bool skipIt;                            ///< used during export to skip some parts if it's needed
};

class ATL_NO_VTABLE VirtualSystemDescription :
    public VirtualSystemDescriptionWrap
{
    friend class Appliance;

public:

    DECLARE_COMMON_CLASS_METHODS(VirtualSystemDescription)

    HRESULT FinalConstruct();
    void FinalRelease();

    HRESULT init();
    void uninit();

    /* public methods only for internal purposes */
    void i_addEntry(VirtualSystemDescriptionType_T aType,
                    const Utf8Str &strRef,
                    const Utf8Str &aOvfValue,
                    const Utf8Str &aVBoxValue,
                    uint32_t ulSizeMB = 0,
                    const Utf8Str &strExtraConfig = "");

    std::list<VirtualSystemDescriptionEntry*> i_findByType(VirtualSystemDescriptionType_T aType);
    const VirtualSystemDescriptionEntry* i_findControllerFromID(const Utf8Str &id);
    const VirtualSystemDescriptionEntry* i_findByIndex(const uint32_t aIndex);

    void i_importVBoxMachineXML(const xml::ElementNode &elmMachine);
    const settings::MachineConfigFile* i_getMachineConfig() const;

    /* private instance data */
private:

    // wrapped IVirtualSystemDescription properties
    HRESULT getCount(ULONG *aCount);

    // wrapped IVirtualSystemDescription methods
    HRESULT getDescription(std::vector<VirtualSystemDescriptionType_T> &aTypes,
                           std::vector<com::Utf8Str> &aRefs,
                           std::vector<com::Utf8Str> &aOVFValues,
                           std::vector<com::Utf8Str> &aVBoxValues,
                           std::vector<com::Utf8Str> &aExtraConfigValues);
    HRESULT getDescriptionByType(VirtualSystemDescriptionType_T aType,
                                 std::vector<VirtualSystemDescriptionType_T> &aTypes,
                                 std::vector<com::Utf8Str> &aRefs,
                                 std::vector<com::Utf8Str> &aOVFValues,
                                 std::vector<com::Utf8Str> &aVBoxValues,
                                 std::vector<com::Utf8Str> &aExtraConfigValues);
    HRESULT getValuesByType(VirtualSystemDescriptionType_T aType,
                            VirtualSystemDescriptionValueType_T aWhich,
                            std::vector<com::Utf8Str> &aValues);
    HRESULT setFinalValues(const std::vector<BOOL> &aEnabled,
                           const std::vector<com::Utf8Str> &aVBoxValues,
                           const std::vector<com::Utf8Str> &aExtraConfigValues);
    HRESULT addDescription(VirtualSystemDescriptionType_T aType,
                           const com::Utf8Str &aVBoxValue,
                           const com::Utf8Str &aExtraConfigValue);
    HRESULT removeDescriptionByType(VirtualSystemDescriptionType_T aType);
    void i_removeByType(VirtualSystemDescriptionType_T aType);

    struct Data;
    Data *m;

    friend class Machine;
};

#endif /* !MAIN_INCLUDED_ApplianceImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
