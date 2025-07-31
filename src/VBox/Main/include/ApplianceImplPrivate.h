/* $Id: ApplianceImplPrivate.h 110133 2025-07-07 12:49:50Z brent.paulson@oracle.com $ */
/** @file
 * VirtualBox Appliance private data definitions
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

#ifndef MAIN_INCLUDED_ApplianceImplPrivate_h
#define MAIN_INCLUDED_ApplianceImplPrivate_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


class VirtualSystemDescription;

#include "ovfreader.h"
#include "SecretKeyStore.h"
#include "ThreadTask.h"
#include "CertificateImpl.h"
#include <map>
#include <vector>
#include <iprt/manifest.h>
#include <iprt/vfs.h>
#include <iprt/crypto/x509.h>
#include <iprt/crypto/pkcs7.h>

////////////////////////////////////////////////////////////////////////////////
//
// Appliance data definition
//
////////////////////////////////////////////////////////////////////////////////

namespace settings
{
    struct AttachedDevice;
}

typedef std::pair<Utf8Str, Utf8Str> STRPAIR;

typedef std::vector<com::Guid> GUIDVEC;

/* Describe a location for the import/export. The location could be a file on a
 * local hard disk or a remote target based on the supported inet protocols. */
struct LocationInfo
{
    LocationInfo()
      : storageType(VFSType_File) {}
    VFSType_T storageType; /* Which type of storage should be handled */
    Utf8Str strProvider;   /* cloud provider name in case of export/import to Cloud */
    Utf8Str strPath;       /* File path for the import/export */
    Utf8Str strHostname;   /* Hostname on remote storage locations (could be empty) */
    Utf8Str strUsername;   /* Username on remote storage locations (could be empty) */
    Utf8Str strPassword;   /* Password on remote storage locations (could be empty) */
};

/**
 * opaque private instance data of Appliance class
 */
struct Appliance::Data
{
    enum digest_T {SHA1, SHA256};

    /** @todo r=andy Make up your mind if naming attributes with a m_ prefix or not. */
    Data()
      : state(Appliance::ApplianceIdle)
      , fDigestTypes(0)
      , hOurManifest(NIL_RTMANIFEST)
      , fManifest(true)
      , fDeterminedDigestTypes(false)
      , hTheirManifest(NIL_RTMANIFEST)
      , hMemFileTheirManifest(NIL_RTVFSFILE)
      , fSignerCertLoaded(false)
      , fCertificateIsSelfSigned(false)
      , fSignatureValid(false)
      , fCertificateValid(false)
      , fCertificateMissingPath(true)
      , fCertificateValidTime(false)
      , pbSignedDigest(NULL)
      , cbSignedDigest(0)
      , enmSignedDigestType(RTDIGESTTYPE_INVALID)
      , fContentInfoLoaded(false)
      , fContentInfoOkay(false)
      , fContentInfoSameCert(false)
      , fContentInfoValidSignature(false)
      , fContentInfoDoneVerification(false)
      , fContentInfoVerifiedOkay(false)
      , fExportISOImages(false)
      , pReader(NULL)
      , ulWeightForXmlOperation(0)
      , ulWeightForManifestOperation(0)
      , ulTotalDisksMB(0)
      , cDisks(0)
      , m_pSecretKeyStore(NULL)
      , m_cPwProvided(0)
    {
        RT_ZERO(SignerCert);
        RT_ZERO(ContentInfo);
    }

    ~Data()
    {
        if (pReader)
        {
            delete pReader;
            pReader = NULL;
        }
        resetReadData();
    }

    /**
     * Resets data used by read.
     */
    void resetReadData(void)
    {
        strOvfManifestEntry.setNull();
        if (hOurManifest != NIL_RTMANIFEST)
        {
            RTManifestRelease(hOurManifest);
            hOurManifest = NIL_RTMANIFEST;
        }
        if (hTheirManifest != NIL_RTMANIFEST)
        {
            RTManifestRelease(hTheirManifest);
            hTheirManifest = NIL_RTMANIFEST;
        }
        if (hMemFileTheirManifest)
        {
            RTVfsFileRelease(hMemFileTheirManifest);
            hMemFileTheirManifest = NIL_RTVFSFILE;
        }
        if (pbSignedDigest)
        {
            RTMemFree(pbSignedDigest);
            pbSignedDigest = NULL;
            cbSignedDigest = 0;
        }
        if (fSignerCertLoaded)
        {
            RTCrX509Certificate_Delete(&SignerCert);
            fSignerCertLoaded = false;
        }
        RT_ZERO(SignerCert);
        enmSignedDigestType      = RTDIGESTTYPE_INVALID;
        fCertificateIsSelfSigned = false;
        fSignatureValid          = false;
        fCertificateValid        = false;
        fCertificateMissingPath  = true;
        fCertificateValidTime    = false;
        fDeterminedDigestTypes   = false;
        fDigestTypes             = RTMANIFEST_ATTR_SHA1 | RTMANIFEST_ATTR_SHA256 | RTMANIFEST_ATTR_SHA512;
        ptrCertificateInfo.setNull();
        strCertError.setNull();
        if (fContentInfoLoaded)
        {
            RTCrPkcs7ContentInfo_Delete(&ContentInfo);
            fContentInfoLoaded = false;
        }
        RT_ZERO(ContentInfo);
    }

    Appliance::ApplianceState      state;

    LocationInfo        locInfo;        // location info for the currently processed OVF
    /** The digests types to calculate (RTMANIFEST_ATTR_XXX) for the manifest.
     * This will be a single value when exporting.  Zero, one or two.  */
    uint32_t            fDigestTypes;
    /** Manifest created while importing or exporting. */
    RTMANIFEST          hOurManifest;

    /** @name Write data
     * @{ */
    bool                fManifest;      // Create a manifest file on export
    /** @} */

    /** @name Read data
     *  @{ */
    /** The manifest entry name of the OVF-file. */
    Utf8Str             strOvfManifestEntry;

    /** Set if we've parsed the manifest and determined the digest types. */
    bool                fDeterminedDigestTypes;

    /** Manifest read in during read() and kept around for later verification. */
    RTMANIFEST          hTheirManifest;
    /** Memorized copy of the manifest file for signature checking purposes. */
    RTVFSFILE           hMemFileTheirManifest;

    /** The signer certificate from the signature file (.cert).
     * This will be used in the future provide information about the signer via
     * the API. */
    RTCRX509CERTIFICATE SignerCert;
    /** Set if the SignerCert member contains usable data. */
    bool                fSignerCertLoaded;
    /** Cached RTCrX509Validity_IsValidAtTimeSpec result set by read(). */
    bool                fCertificateIsSelfSigned;
    /** Set by read() if pbSignedDigest verified correctly against SignerCert. */
    bool                fSignatureValid;
    /** Set by read() when the SignerCert checked out fine. */
    bool                fCertificateValid;
    /** Set by read() when the SignerCert certificate path couldn't be built. */
    bool                fCertificateMissingPath;
    /** Set by read() when the SignerCert (+path) is valid in the temporal sense. */
    bool                fCertificateValidTime;
    /** For keeping certificate error messages we delay from read() to import(). */
    Utf8Str             strCertError;
    /** The signed digest of the manifest. */
    uint8_t            *pbSignedDigest;
    /** The size of the signed digest. */
    size_t              cbSignedDigest;
    /** The digest type used to sign the manifest. */
    RTDIGESTTYPE        enmSignedDigestType;
    /** The certificate info object.  This is NULL if no signature and
     *  successfully loaded certificate. */
    ComObjPtr<Certificate> ptrCertificateInfo;

    /** The PKCS\#7/CMS signed data signing manifest, optional VBox extension.
     * This contains at least one signature using the same certificate as above
     * (SignerCert), but should preferrably use a different digest.  The PKCS\#7/CMS
     * format is a lot more versatile, allow multiple signatures using different
     * digests and certificates, optionally with counter signed timestamps.
     * Additional intermediate certificates can also be shipped, helping to bridge
     * the gap to a trusted root certificate installed on the recieving system.  */
    RTCRPKCS7CONTENTINFO ContentInfo;
    /** Set if the ContentInfo member contains usable data. */
    bool                fContentInfoLoaded;
    /** Set by read() if the ContentInfo member checked out okay (says nothing about
     *  the signature or certificates within it). */
    bool                fContentInfoOkay;
    /** Set by read() if the ContentInfo member is using the SignerCert too. */
    bool                fContentInfoSameCert;
    /** Set by read() if the ContentInfo member contains valid signatures (not
     * saying anything about valid signing certificates). */
    bool                fContentInfoValidSignature;
    /** Set by read() if we've already verified the signed data signature(s). */
    bool                fContentInfoDoneVerification;

    bool                fContentInfoVerifiedOkay;
    /** @} */

    bool                fExportISOImages;// when 1 the ISO images are exported

    RTCList<ImportOptions_T> optListImport;
    RTCList<ExportOptions_T> optListExport;

    ovf::OVFReader      *pReader;

    std::list< ComObjPtr<VirtualSystemDescription> >
                        virtualSystemDescriptions;

    std::list<Utf8Str>  llWarnings;

    ULONG               ulWeightForXmlOperation;
    ULONG               ulWeightForManifestOperation;
    ULONG               ulTotalDisksMB;
    ULONG               cDisks;

    std::list<Guid>     llGuidsMachinesCreated;

    /** Sequence of password identifiers to encrypt disk images during export. */
    std::vector<com::Utf8Str> m_vecPasswordIdentifiers;
    /** Map to get all medium identifiers assoicated with a given password identifier. */
    std::map<com::Utf8Str, GUIDVEC> m_mapPwIdToMediumIds;
    /** Secret key store used to hold the passwords during export. */
    SecretKeyStore            *m_pSecretKeyStore;
    /** Number of passwords provided. */
    uint32_t                  m_cPwProvided;
};

struct Appliance::XMLStack
{
    std::map<Utf8Str, const VirtualSystemDescriptionEntry*> mapDisks;
    std::list<Utf8Str> mapFilesSequence; // list of all files (e.g. NVRAM files) and disks to include in exported VM(s)
    std::list<Utf8Str> mapDiskSequenceForOneVM;//temporarily keeps all disks attached to one exported VM
    std::map<Utf8Str, bool> mapNetworks;
    std::map<Utf8Str, std::pair<Utf8Str, Utf8Str> > mapNvramFiles; // details of all NVRAM files in the exported VM(s)
    std::map<Utf8Str, std::pair<Utf8Str, Utf8Str> > mapNvramFileForOneVM; // temporary details of NVRAM file in an exported VM
};

class Appliance::TaskOVF : public ThreadTask
{
public:
    enum TaskType
    {
        Read,
        Import,
        Write
    };

    TaskOVF(Appliance *aThat,
            TaskType aType,
            LocationInfo aLocInfo,
            ComObjPtr<Progress> &aProgress)
      : ThreadTask("TaskOVF")
      , pAppliance(aThat)
      , taskType(aType)
      , locInfo(aLocInfo)
      , pProgress(aProgress)
      , enFormat(ovf::OVFVersion_unknown)
      , hrc(S_OK)
    {
        switch (taskType)
        {
            case TaskOVF::Read:     m_strTaskName = "ApplRead"; break;
            case TaskOVF::Import:   m_strTaskName = "ApplImp"; break;
            case TaskOVF::Write:    m_strTaskName = "ApplWrit"; break;
            default:                m_strTaskName = "ApplTask"; break;
        }
    }

    static DECLCALLBACK(int) updateProgress(unsigned uPercent, void *pvUser);

    Appliance *pAppliance;
    TaskType taskType;
    const LocationInfo locInfo;
    ComObjPtr<Progress> pProgress;

    ovf::OVFVersion_T enFormat;

    HRESULT hrc;

    void handler()
    {
        Appliance::i_importOrExportThreadTask(this);
    }
};

class Appliance::TaskOPC : public ThreadTask
{
public:
    enum TaskType
    {
        Export
    };

    TaskOPC(Appliance *aThat,
            TaskType aType,
            LocationInfo aLocInfo,
            ComObjPtr<Progress> &aProgress)
      : ThreadTask("TaskOPC")
      , pAppliance(aThat)
      , taskType(aType)
      , locInfo(aLocInfo)
      , pProgress(aProgress)
      , hrc(S_OK)
    {
        m_strTaskName = "OPCExpt";
    }

    ~TaskOPC()
    {
    }

    static DECLCALLBACK(int) updateProgress(unsigned uPercent, void *pvUser);

    Appliance *pAppliance;
    TaskType taskType;
    const LocationInfo locInfo;
    ComObjPtr<Progress> pProgress;

    HRESULT hrc;

    void handler()
    {
        Appliance::i_exportOPCThreadTask(this);
    }
};


class Appliance::TaskCloud : public ThreadTask
{
public:
    enum TaskType
    {
        Export,
        Import,
        ReadData
    };

    TaskCloud(Appliance *aThat,
              TaskType aType,
              LocationInfo aLocInfo,
              ComObjPtr<Progress> &aProgress)
      : ThreadTask("TaskCloud")
      , pAppliance(aThat)
      , taskType(aType)
      , locInfo(aLocInfo)
      , pProgress(aProgress)
      , hrc(S_OK)
    {
        switch (taskType)
        {
            case TaskCloud::Export:    m_strTaskName = "CloudExpt"; break;
            case TaskCloud::Import:    m_strTaskName = "CloudImpt"; break;
            case TaskCloud::ReadData:  m_strTaskName = "CloudRead"; break;
            default:                   m_strTaskName = "CloudTask"; break;
        }
    }

    ~TaskCloud()
    {
    }

    static DECLCALLBACK(int) updateProgress(unsigned uPercent, void *pvUser);

    Appliance *pAppliance;
    TaskType taskType;
    const LocationInfo locInfo;
    ComObjPtr<Progress> pProgress;

    HRESULT hrc;

    void handler()
    {
        Appliance::i_importOrExportCloudThreadTask(this);
    }
};

struct MyHardDiskAttachment
{
    ComPtr<IMachine>    pMachine;
    Utf8Str             controllerName;
    int32_t             lControllerPort;        // 0-29 for SATA
    int32_t             lDevice;                // IDE: 0 or 1, otherwise 0 always
};

/**
 * Used by Appliance::importMachineGeneric() to store
 * input parameters and rollback information.
 */
struct Appliance::ImportStack
{
    // input pointers
    const LocationInfo              &locInfo;           // ptr to location info from Appliance::importFS()
    Utf8Str                         strSourceDir;       // directory where source files reside
    const ovf::DiskImagesMap        &mapDisks;          // ptr to disks map in OVF
    ComObjPtr<Progress>             &pProgress;         // progress object passed into Appliance::importFS()

    // input parameters from VirtualSystemDescriptions
    Utf8Str                         strNameVBox;        // VM name
    Utf8Str                         strSettingsFilename; // Absolute path to VM config file
    Utf8Str                         strMachineFolder;   // Absolute path to VM folder (derived from strSettingsFilename)
    Utf8Str                         strOsTypeVBox;      // VirtualBox guest OS type as string
    Utf8Str                         strPrimaryGroup;    // VM primary group as string
    Utf8Str                         strDescription;
    uint32_t                        cCPUs;              // CPU count
    bool                            fForceHWVirt;       // if true, we force enabling hardware virtualization
    bool                            fForceIOAPIC;       // if true, we force enabling the IOAPIC
    uint32_t                        ulMemorySizeMB;     // virtual machine RAM in megabytes
    Utf8Str                         strFirmwareType;    //Firmware - BIOS or EFI
#ifdef VBOX_WITH_USB
    bool                            fUSBEnabled;
#endif
    Utf8Str                         strAudioAdapter;    // if not empty, then the guest has audio enabled, and this is the decimal
                                                        // representation of the audio adapter (should always be "0" for AC97 presently)
    Utf8Str                         strNvramPath;       // if not empty, then the guest is using EFI and this is the path
                                                        // to the VM's NVRAM file

    // session (not initially created)
    ComPtr<ISession>                pSession;           // session opened in Appliance::importFS() for machine manipulation
    bool                            fSessionOpen;       // true if the pSession is currently open and needs closing

    /** @name File access related stuff (TAR stream)
     *  @{  */
    /** OVA file system stream handle. NIL if not OVA.  */
    RTVFSFSSTREAM                   hVfsFssOva;
    /** OVA lookahead I/O stream object. */
    RTVFSIOSTREAM                   hVfsIosOvaLookAhead;
    /** OVA lookahead I/O stream object name. */
    char                           *pszOvaLookAheadName;
    /** @} */

    // a list of images that we created/imported; this is initially empty
    // and will be cleaned up on errors
    std::list<MyHardDiskAttachment> llHardDiskAttachments;      // disks that were attached
    std::map<Utf8Str, Utf8Str>      mapNewUUIDsToOriginalUUIDs;

    ImportStack(const LocationInfo &aLocInfo,
                const ovf::DiskImagesMap &aMapDisks,
                ComObjPtr<Progress> &aProgress,
                RTVFSFSSTREAM aVfsFssOva)
        : locInfo(aLocInfo),
          mapDisks(aMapDisks),
          pProgress(aProgress),
          cCPUs(1),
          fForceHWVirt(false),
          fForceIOAPIC(false),
          ulMemorySizeMB(0),
#ifdef VBOX_WITH_USB
          fUSBEnabled(false),
#endif
          fSessionOpen(false),
          hVfsFssOva(aVfsFssOva),
          hVfsIosOvaLookAhead(NIL_RTVFSIOSTREAM),
          pszOvaLookAheadName(NULL)
    {
        if (hVfsFssOva != NIL_RTVFSFSSTREAM)
            RTVfsFsStrmRetain(hVfsFssOva);

        // disk images have to be on the same place as the OVF file. So
        // strip the filename out of the full file path
        strSourceDir = aLocInfo.strPath;
        strSourceDir.stripFilename();
    }

    ~ImportStack()
    {
        if (hVfsFssOva != NIL_RTVFSFSSTREAM)
        {
            RTVfsFsStrmRelease(hVfsFssOva);
            hVfsFssOva = NIL_RTVFSFSSTREAM;
        }
        if (hVfsIosOvaLookAhead != NIL_RTVFSIOSTREAM)
        {
            RTVfsIoStrmRelease(hVfsIosOvaLookAhead);
            hVfsIosOvaLookAhead = NIL_RTVFSIOSTREAM;
        }
        if (pszOvaLookAheadName)
        {
            RTStrFree(pszOvaLookAheadName);
            pszOvaLookAheadName = NULL;
        }
    }

    HRESULT restoreOriginalUUIDOfAttachedDevice(settings::MachineConfigFile *config);
    void saveOriginalUUIDOfAttachedDevice(settings::AttachedDevice &device, const Utf8Str &newlyUuid);
    RTVFSIOSTREAM claimOvaLookAHead(void);
};

////////////////////////////////////////////////////////////////////////////////
//
// VirtualSystemDescription data definition
//
////////////////////////////////////////////////////////////////////////////////

struct VirtualSystemDescription::Data
{
    /** item descriptions */
    std::vector<VirtualSystemDescriptionEntry> maDescriptions;

    /** VirtualBox machine this description was exported from (export only) */
    ComPtr<Machine> pMachine;

    /** machine config created from <vbox:Machine> element if found (import only) */
    settings::MachineConfigFile *pConfig;
};

////////////////////////////////////////////////////////////////////////////////
//
// Internal helpers
//
////////////////////////////////////////////////////////////////////////////////

void convertCIMOSType2VBoxOSType(Utf8Str &strType, ovf::CIMOSType_T c, const Utf8Str &cStr);

ovf::CIMOSType_T convertVBoxOSType2CIMOSType(const char *pcszVBox, BOOL fLongMode);

Utf8Str convertNetworkAttachmentTypeToString(NetworkAttachmentType_T type);


#endif /* !MAIN_INCLUDED_ApplianceImplPrivate_h */

