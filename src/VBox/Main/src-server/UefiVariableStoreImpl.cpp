/* $Id: UefiVariableStoreImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM NVRAM store class implementation
 */

/*
 * Copyright (C) 2021-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_UEFIVARIABLESTORE
#include "LoggingNew.h"

#include "UefiVariableStoreImpl.h"
#include "NvramStoreImpl.h"
#include "MachineImpl.h"

#include "AutoStateDep.h"
#include "AutoCaller.h"

#include "TrustAnchorsAndCerts.h"

#include <VBox/com/array.h>

#include <iprt/cpp/utils.h>
#include <iprt/efi.h>
#include <iprt/file.h>
#include <iprt/vfs.h>

#include <iprt/formats/efi-varstore.h>
#include <iprt/formats/efi-signature.h>

// defines
////////////////////////////////////////////////////////////////////////////////

// globals
////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
// UefiVariableStore::Data structure
/////////////////////////////////////////////////////////////////////////////

struct UefiVariableStore::Data
{
    Data()
        : pParent(NULL),
          pMachine(NULL),
          hVfsUefiVarStore(NIL_RTVFS)
    { }

    /** The NVRAM store owning this UEFI variable store intstance. */
    NvramStore * const      pParent;
    /** The machine this UEFI variable store belongs to. */
    Machine    * const      pMachine;
    /** VFS handle to the UEFI variable store. */
    RTVFS                   hVfsUefiVarStore;
};

// constructor / destructor
////////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(UefiVariableStore)

HRESULT UefiVariableStore::FinalConstruct()
{
    return BaseFinalConstruct();
}

void UefiVariableStore::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the UEFI variable store object.
 *
 * @returns COM result indicator.
 * @param   aParent                     The NVRAM store owning the UEFI NVRAM content.
 * @param   pMachine
 */
HRESULT UefiVariableStore::init(NvramStore *aParent, Machine *pMachine)
{
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("aParent: %p\n", aParent));

    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    /* share the parent weakly */
    unconst(m->pParent)  = aParent;
    unconst(m->pMachine) = pMachine;
    m->hVfsUefiVarStore  = NIL_RTVFS;

    autoInitSpan.setSucceeded();

    LogFlowThisFuncLeave();
    return S_OK;
}


/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void UefiVariableStore::uninit()
{
    LogFlowThisFuncEnter();

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    Assert(m->hVfsUefiVarStore == NIL_RTVFS);

    unconst(m->pParent) = NULL;
    unconst(m->pMachine) = NULL;

    delete m;
    m = NULL;

    LogFlowThisFuncLeave();
}


HRESULT UefiVariableStore::getSecureBootEnabled(BOOL *pfEnabled)
{
    HRESULT hrc = i_retainUefiVariableStore(true /*fReadonly*/);
    if (FAILED(hrc)) return hrc;

    AutoReadLock rlock(this COMMA_LOCKVAL_SRC_POS);

    uint64_t cbVar = 0;
    int vrc = i_uefiVarStoreQueryVarSz("PK", &cbVar);
    if (RT_SUCCESS(vrc))
    {
        *pfEnabled = TRUE;

        /* Check the SecureBootEnable variable for the override. */
        vrc = i_uefiVarStoreQueryVarSz("SecureBootEnable", &cbVar);
        if (RT_SUCCESS(vrc))
        {
            if (cbVar == sizeof(uint8_t))
            {
                uint8_t bVar = 0;
                hrc = i_uefiVarStoreQueryVar("SecureBootEnable", &bVar, sizeof(bVar));
                if (SUCCEEDED(hrc))
                    *pfEnabled = bVar == 0x0 ? FALSE : TRUE;
            }
            else
                hrc = setError(E_FAIL, tr("The 'SecureBootEnable' variable size is bogus (expected 1, got %llu)"), cbVar);
        }
        else if (vrc != VERR_FILE_NOT_FOUND)
            hrc = setError(E_FAIL, tr("Failed to query the 'SecureBootEnable' variable size: %Rrc"), vrc);
    }
    else if (vrc == VERR_FILE_NOT_FOUND) /* No platform key means no secure boot. */
        *pfEnabled = FALSE;
    else
        hrc = setError(E_FAIL, tr("Failed to query the platform key variable size: %Rrc"), vrc);

    i_releaseUefiVariableStore();
    return hrc;
}


HRESULT UefiVariableStore::setSecureBootEnabled(BOOL fEnabled)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    HRESULT hrc = i_retainUefiVariableStore(false /*fReadonly*/);
    if (FAILED(hrc)) return hrc;

    AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);

    EFI_GUID GuidSecureBootEnable = EFI_SECURE_BOOT_ENABLE_DISABLE_GUID;
    uint64_t cbVar = 0;
    int vrc = i_uefiVarStoreQueryVarSz("PK", &cbVar);
    if (RT_SUCCESS(vrc))
    {
        uint8_t bVar = fEnabled ? 0x1 : 0x0;
        hrc = i_uefiVarStoreSetVar(&GuidSecureBootEnable, "SecureBootEnable",
                                     EFI_VAR_HEADER_ATTR_NON_VOLATILE
                                   | EFI_VAR_HEADER_ATTR_BOOTSERVICE_ACCESS
                                   | EFI_VAR_HEADER_ATTR_RUNTIME_ACCESS,
                                   &bVar, sizeof(bVar));
    }
    else if (vrc == VERR_FILE_NOT_FOUND) /* No platform key means no secure boot support. */
        hrc = setError(VBOX_E_OBJECT_NOT_FOUND, tr("Secure boot is not available because the platform key (PK) is not enrolled"));
    else
        hrc = setError(E_FAIL, tr("Failed to query the platform key variable size: %Rrc"), vrc);

    i_releaseUefiVariableStore();
    return hrc;
}


HRESULT UefiVariableStore::addVariable(const com::Utf8Str &aName, const com::Guid &aOwnerUuid,
                                       const std::vector<UefiVariableAttributes_T> &aAttributes,
                                       const std::vector<BYTE> &aData)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    HRESULT hrc = i_retainUefiVariableStore(false /*fReadonly*/);
    if (FAILED(hrc)) return hrc;

    AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);

    uint32_t fAttr = i_uefiVarAttrToMask(aAttributes);
    EFI_GUID OwnerGuid;
    RTEfiGuidFromUuid(&OwnerGuid, aOwnerUuid.raw());
    hrc = i_uefiVarStoreSetVar(&OwnerGuid, aName.c_str(), fAttr, &aData.front(), aData.size());

    i_releaseUefiVariableStore();
    return hrc;
}


HRESULT UefiVariableStore::deleteVariable(const com::Utf8Str &aName, const com::Guid &aOwnerUuid)
{
    RT_NOREF(aOwnerUuid);

    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    HRESULT hrc = i_retainUefiVariableStore(false /*fReadonly*/);
    if (FAILED(hrc)) return hrc;

    AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);

    char szVarPath[_1K];
    ssize_t cch = RTStrPrintf2(szVarPath, sizeof(szVarPath), "/raw/%s", aName.c_str());
    if (cch > 0)
    {
        RTVFSDIR hVfsDirRoot = NIL_RTVFSDIR;
        int vrc = RTVfsOpenRoot(m->hVfsUefiVarStore, &hVfsDirRoot);
        if (RT_SUCCESS(vrc))
        {
            vrc = RTVfsDirRemoveDir(hVfsDirRoot, szVarPath, 0 /*fFlags*/);
            RTVfsDirRelease(hVfsDirRoot);
            if (RT_FAILURE(vrc))
                hrc = setError(VBOX_E_IPRT_ERROR, tr("Failed to remove variable '%s' (%Rrc)"), aName.c_str(), vrc);
        }
        else
            hrc = setError(VBOX_E_IPRT_ERROR, tr("Failed to open the variable store root (%Rrc)"), vrc);
    }
    else
        hrc = setError(E_FAIL, tr("The variable name is too long"));

    i_releaseUefiVariableStore();
    return hrc;
}


HRESULT UefiVariableStore::changeVariable(const com::Utf8Str &aName, const std::vector<BYTE> &aData)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    HRESULT hrc = i_retainUefiVariableStore(false /*fReadonly*/);
    if (FAILED(hrc)) return hrc;

    AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);

    RTVFSFILE hVfsFile = NIL_RTVFSFILE;
    hrc = i_uefiVarStoreOpenVar(aName.c_str(), &hVfsFile);
    if (SUCCEEDED(hrc))
    {
        int vrc = RTVfsFileSetSize(hVfsFile, aData.size(), RTVFSFILE_SIZE_F_NORMAL);
        if (RT_SUCCESS(vrc))
        {
            vrc = RTVfsFileWriteAt(hVfsFile, 0 /*off*/, &aData.front(), aData.size(), NULL /*pcbWritten*/);
            if (RT_FAILURE(vrc))
                hrc = setError(VBOX_E_IPRT_ERROR, tr("Failed to data for variable '%s' (%Rrc)"), aName.c_str(), vrc);
        }
        else
            hrc = setError(VBOX_E_IPRT_ERROR, tr("Failed to allocate space for the variable '%s' (%Rrc)"), aName.c_str(), vrc);

        RTVfsFileRelease(hVfsFile);
    }

    i_releaseUefiVariableStore();
    return hrc;
}


HRESULT UefiVariableStore::queryVariableByName(const com::Utf8Str &aName, com::Guid &aOwnerUuid,
                                               std::vector<UefiVariableAttributes_T> &aAttributes,
                                               std::vector<BYTE> &aData)
{
    HRESULT hrc = i_retainUefiVariableStore(true /*fReadonly*/);
    if (FAILED(hrc)) return hrc;

    AutoReadLock rlock(this COMMA_LOCKVAL_SRC_POS);

    uint32_t fAttr;
    int vrc = i_uefiVarStoreQueryVarAttr(aName.c_str(), &fAttr);
    if (RT_SUCCESS(vrc))
    {
        RTUUID OwnerUuid;
        vrc = i_uefiVarStoreQueryVarOwnerUuid(aName.c_str(), &OwnerUuid);
        if (RT_SUCCESS(vrc))
        {
            uint64_t cbVar = 0;
            vrc = i_uefiVarStoreQueryVarSz(aName.c_str(), &cbVar);
            if (RT_SUCCESS(vrc))
            {
                aData.resize(cbVar);
                hrc = i_uefiVarStoreQueryVar(aName.c_str(), &aData.front(), aData.size());
                if (SUCCEEDED(hrc))
                {
                    aOwnerUuid = com::Guid(OwnerUuid);
                    i_uefiAttrMaskToVec(fAttr, aAttributes);
                }
            }
            else
                hrc = setError(VBOX_E_IPRT_ERROR, tr("Failed to query the size of variable '%s': %Rrc"), aName.c_str(), vrc);
        }
        else
            hrc = setError(VBOX_E_IPRT_ERROR, tr("Failed to query the owner UUID of variable '%s': %Rrc"), aName.c_str(), vrc);
    }
    else
        hrc = setError(VBOX_E_IPRT_ERROR, tr("Failed to query the attributes of variable '%s': %Rrc"), aName.c_str(), vrc);

    i_releaseUefiVariableStore();
    return hrc;
}


HRESULT UefiVariableStore::queryVariables(std::vector<com::Utf8Str> &aNames,
                                          std::vector<com::Guid> &aOwnerUuids)
{
    HRESULT hrc = i_retainUefiVariableStore(true /*fReadonly*/);
    if (FAILED(hrc)) return hrc;

    AutoReadLock rlock(this COMMA_LOCKVAL_SRC_POS);

    RTVFSDIR hVfsDir = NIL_RTVFSDIR;
    int vrc = RTVfsDirOpen(m->hVfsUefiVarStore, "by-name", 0 /*fFlags*/, &hVfsDir);
    if (RT_SUCCESS(vrc))
    {
        RTDIRENTRYEX DirEntry;

        vrc = RTVfsDirReadEx(hVfsDir, &DirEntry, NULL, RTFSOBJATTRADD_NOTHING);
        for (;;)
        {
            RTUUID OwnerUuid;
            vrc = i_uefiVarStoreQueryVarOwnerUuid(DirEntry.szName, &OwnerUuid);
            if (RT_FAILURE(vrc))
                break;

            aNames.push_back(Utf8Str(DirEntry.szName));
            aOwnerUuids.push_back(com::Guid(OwnerUuid));

            vrc = RTVfsDirReadEx(hVfsDir, &DirEntry, NULL, RTFSOBJATTRADD_NOTHING);
            if (RT_FAILURE(vrc))
                break;
        }

        if (vrc == VERR_NO_MORE_FILES)
            vrc = VINF_SUCCESS;

        RTVfsDirRelease(hVfsDir);
    }

    i_releaseUefiVariableStore();

    if (RT_FAILURE(vrc))
        return setError(VBOX_E_IPRT_ERROR, tr("Failed to query the variables: %Rrc"), vrc);

    return S_OK;
}


HRESULT UefiVariableStore::enrollOraclePlatformKey(void)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    HRESULT hrc = i_retainUefiVariableStore(false /*fReadonly*/);
    if (FAILED(hrc)) return hrc;

    AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);

    EFI_GUID GuidGlobalVar = EFI_GLOBAL_VARIABLE_GUID;

    /** @todo This conversion from EFI GUID -> IPRT UUID -> Com GUID is nuts... */
    EFI_GUID GuidOwnerVBox = EFI_SIGNATURE_OWNER_GUID_VBOX;
    RTUUID   UuidVBox;
    RTEfiGuidToUuid(&UuidVBox, &GuidOwnerVBox);

    const com::Guid GuidVBox(UuidVBox);

    hrc = i_uefiVarStoreAddSignatureToDb(&GuidGlobalVar, "PK", g_abUefiOracleDefPk, g_cbUefiOracleDefPk,
                                         GuidVBox, SignatureType_X509);

    i_releaseUefiVariableStore();
    return hrc;
}


HRESULT UefiVariableStore::enrollPlatformKey(const std::vector<BYTE> &aData, const com::Guid &aOwnerUuid)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    HRESULT hrc = i_retainUefiVariableStore(false /*fReadonly*/);
    if (FAILED(hrc)) return hrc;

    AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);

    EFI_GUID GuidGlobalVar = EFI_GLOBAL_VARIABLE_GUID;
    hrc = i_uefiVarStoreAddSignatureToDbVec(&GuidGlobalVar, "PK", aData, aOwnerUuid, SignatureType_X509);

    i_releaseUefiVariableStore();
    return hrc;
}


HRESULT UefiVariableStore::addKek(const std::vector<BYTE> &aData, const com::Guid &aOwnerUuid, SignatureType_T enmSignatureType)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    HRESULT hrc = i_retainUefiVariableStore(false /*fReadonly*/);
    if (FAILED(hrc)) return hrc;

    AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);

    EFI_GUID GuidGlobalVar = EFI_GLOBAL_VARIABLE_GUID;
    hrc = i_uefiVarStoreAddSignatureToDbVec(&GuidGlobalVar, "KEK", aData, aOwnerUuid, enmSignatureType);

    i_releaseUefiVariableStore();
    return hrc;
}


HRESULT UefiVariableStore::addSignatureToDb(const std::vector<BYTE> &aData, const com::Guid &aOwnerUuid, SignatureType_T enmSignatureType)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    HRESULT hrc = i_retainUefiVariableStore(false /*fReadonly*/);
    if (FAILED(hrc)) return hrc;

    AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);

    EFI_GUID GuidSecurityDb = EFI_GLOBAL_VARIABLE_GUID;
    hrc = i_uefiVarStoreAddSignatureToDbVec(&GuidSecurityDb, "db", aData, aOwnerUuid, enmSignatureType);

    i_releaseUefiVariableStore();
    return hrc;
}


HRESULT UefiVariableStore::addSignatureToDbx(const std::vector<BYTE> &aData, const com::Guid &aOwnerUuid, SignatureType_T enmSignatureType)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    HRESULT hrc = i_retainUefiVariableStore(false /*fReadonly*/);
    if (FAILED(hrc)) return hrc;

    AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);

    EFI_GUID GuidSecurityDb = EFI_IMAGE_SECURITY_DATABASE_GUID;
    hrc = i_uefiVarStoreAddSignatureToDbVec(&GuidSecurityDb, "dbx", aData, aOwnerUuid, enmSignatureType);

    i_releaseUefiVariableStore();
    return hrc;
}


HRESULT UefiVariableStore::enrollDefaultMsSignatures(void)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    HRESULT hrc = i_retainUefiVariableStore(false /*fReadonly*/);
    if (FAILED(hrc)) return hrc;

    AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);

    EFI_GUID EfiGuidSecurityDb = EFI_IMAGE_SECURITY_DATABASE_GUID;
    EFI_GUID EfiGuidGlobalVar  = EFI_GLOBAL_VARIABLE_GUID;

    /** @todo This conversion from EFI GUID -> IPRT UUID -> Com GUID is nuts... */
    EFI_GUID EfiGuidMs      = EFI_SIGNATURE_OWNER_GUID_MICROSOFT;
    RTUUID   UuidMs;
    RTEfiGuidToUuid(&UuidMs, &EfiGuidMs);

    const com::Guid GuidMs(UuidMs);

    hrc = i_uefiVarStoreAddSignatureToDb(&EfiGuidGlobalVar, "KEK", g_abUefiMicrosoftKek, g_cbUefiMicrosoftKek,
                                         GuidMs, SignatureType_X509);
    if (SUCCEEDED(hrc))
    {
        hrc = i_uefiVarStoreAddSignatureToDb(&EfiGuidGlobalVar, "KEK", g_abUefiMicrosoftKek2023, g_cbUefiMicrosoftKek2023,
                                             GuidMs, SignatureType_X509);
        if (SUCCEEDED(hrc))
        {
            hrc = i_uefiVarStoreAddSignatureToDb(&EfiGuidSecurityDb, "db", g_abUefiMicrosoft3rdCa, g_cbUefiMicrosoft3rdCa,
                                                 GuidMs, SignatureType_X509);
            if (SUCCEEDED(hrc))
            {
                hrc = i_uefiVarStoreAddSignatureToDb(&EfiGuidSecurityDb, "db", g_abUefiMicrosoft3rdCa2023, g_cbUefiMicrosoft3rdCa2023,
                                                     GuidMs, SignatureType_X509);
                if (SUCCEEDED(hrc))
                {
                    hrc = i_uefiVarStoreAddSignatureToDb(&EfiGuidSecurityDb, "db", g_abUefiMicrosoftWinCa, g_cbUefiMicrosoftWinCa,
                                                         GuidMs, SignatureType_X509);
                    if (SUCCEEDED(hrc))
                        hrc = i_uefiVarStoreAddSignatureToDb(&EfiGuidSecurityDb, "db", g_abUefiMicrosoftWinCa2023, g_cbUefiMicrosoftWinCa2023,
                                                             GuidMs, SignatureType_X509);
                }
            }
        }
    }

    i_releaseUefiVariableStore();
    return hrc;
}


HRESULT UefiVariableStore::addSignatureToMok(const std::vector<BYTE> &aData, const com::Guid &aOwnerUuid, SignatureType_T enmSignatureType)
{
    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pMachine);
    if (FAILED(adep.hrc())) return adep.hrc();

    HRESULT hrc = i_retainUefiVariableStore(false /*fReadonly*/);
    if (FAILED(hrc)) return hrc;

    AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);

    EFI_GUID GuidMokList = EFI_IMAGE_MOK_DATABASE_GUID;
    hrc = i_uefiVarStoreAddSignatureToDbVec(&GuidMokList, "MokList", aData, aOwnerUuid, enmSignatureType, false /*fRuntime*/);

    i_releaseUefiVariableStore();
    return hrc;
}




/**
 * Sets the given attributes for the given EFI variable store variable.
 *
 * @returns IPRT status code.
 * @param   pszVar              The variable to set the attributes for.
 * @param   fAttr               The attributes to set, see EFI_VAR_HEADER_ATTR_XXX.
 */
int UefiVariableStore::i_uefiVarStoreSetVarAttr(const char *pszVar, uint32_t fAttr)
{
    char szVarPath[_1K];
    ssize_t cch = RTStrPrintf2(szVarPath, sizeof(szVarPath), "/raw/%s/attr", pszVar);
    Assert(cch > 0); RT_NOREF(cch);

    RTVFSFILE hVfsFileAttr = NIL_RTVFSFILE;
    int vrc = RTVfsFileOpen(m->hVfsUefiVarStore, szVarPath,
                           RTFILE_O_READWRITE | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                           &hVfsFileAttr);
    if (RT_SUCCESS(vrc))
    {
        uint32_t fAttrLe = RT_H2LE_U32(fAttr);
        vrc = RTVfsFileWrite(hVfsFileAttr, &fAttrLe, sizeof(fAttrLe), NULL /*pcbWritten*/);
        RTVfsFileRelease(hVfsFileAttr);
    }

    return vrc;
}


/**
 * Queries the attributes for the given EFI variable store variable.
 *
 * @returns IPRT status code.
 * @param   pszVar              The variable to query the attributes for.
 * @param   pfAttr              Where to store the attributes on success.
 */
int UefiVariableStore::i_uefiVarStoreQueryVarAttr(const char *pszVar, uint32_t *pfAttr)
{
    char szVarPath[_1K];
    ssize_t cch = RTStrPrintf2(szVarPath, sizeof(szVarPath), "/raw/%s/attr", pszVar);
    Assert(cch > 0); RT_NOREF(cch);

    RTVFSFILE hVfsFileAttr = NIL_RTVFSFILE;
    int vrc = RTVfsFileOpen(m->hVfsUefiVarStore, szVarPath,
                           RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                           &hVfsFileAttr);
    if (RT_SUCCESS(vrc))
    {
        uint32_t fAttrLe = 0;
        vrc = RTVfsFileRead(hVfsFileAttr, &fAttrLe, sizeof(fAttrLe), NULL /*pcbRead*/);
        RTVfsFileRelease(hVfsFileAttr);
        if (RT_SUCCESS(vrc))
            *pfAttr = RT_LE2H_U32(fAttrLe);
    }

    return vrc;
}


/**
 * Queries the data size for the given variable.
 *
 * @returns IPRT status code.
 * @param   pszVar              The variable to query the size for.
 * @param   pcbVar              Where to store the size of the variable data on success.
 */
int UefiVariableStore::i_uefiVarStoreQueryVarSz(const char *pszVar, uint64_t *pcbVar)
{
    char szVarPath[_1K];
    ssize_t cch = RTStrPrintf2(szVarPath, sizeof(szVarPath), "/by-name/%s", pszVar);
    Assert(cch > 0); RT_NOREF(cch);

    RTVFSFILE hVfsFile = NIL_RTVFSFILE;
    int vrc = RTVfsFileOpen(m->hVfsUefiVarStore, szVarPath,
                            RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                            &hVfsFile);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTVfsFileQuerySize(hVfsFile, pcbVar);
        RTVfsFileRelease(hVfsFile);
    }
    else if (vrc == VERR_PATH_NOT_FOUND)
        vrc = VERR_FILE_NOT_FOUND;

    return vrc;
}


/**
 * Returns the owner UUID of the given variable.
 *
 * @returns IPRT status code.
 * @param   pszVar              The variable to query the owner UUID for.
 * @param   pUuid               Where to store the owner UUID on success.
 */
int UefiVariableStore::i_uefiVarStoreQueryVarOwnerUuid(const char *pszVar, PRTUUID pUuid)
{
    char szVarPath[_1K];
    ssize_t cch = RTStrPrintf2(szVarPath, sizeof(szVarPath), "/raw/%s/uuid", pszVar);
    Assert(cch > 0); RT_NOREF(cch);

    RTVFSFILE hVfsFileAttr = NIL_RTVFSFILE;
    int vrc = RTVfsFileOpen(m->hVfsUefiVarStore, szVarPath,
                           RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                           &hVfsFileAttr);
    if (RT_SUCCESS(vrc))
    {
        EFI_GUID OwnerGuid;
        vrc = RTVfsFileRead(hVfsFileAttr, &OwnerGuid, sizeof(OwnerGuid), NULL /*pcbRead*/);
        RTVfsFileRelease(hVfsFileAttr);
        if (RT_SUCCESS(vrc))
            RTEfiGuidToUuid(pUuid, &OwnerGuid);
    }

    return vrc;
}


/**
 * Converts the given vector of variables attributes to a bitmask used internally.
 *
 * @returns Mask of UEFI variable attributes.
 * @param   vecAttributes       Vector of variable atttributes.
 */
uint32_t UefiVariableStore::i_uefiVarAttrToMask(const std::vector<UefiVariableAttributes_T> &vecAttributes)
{
    uint32_t fAttr = 0;

    for (size_t i = 0; i < vecAttributes.size(); i++)
        fAttr |= (ULONG)vecAttributes[i];

    return fAttr;
}


/**
 * Converts the given aatribute mask to the attribute vector used externally.
 *
 * @param   fAttr               The attribute mask.
 * @param   aAttributes         The vector to store the attibutes in.
 */
void UefiVariableStore::i_uefiAttrMaskToVec(uint32_t fAttr, std::vector<UefiVariableAttributes_T> &aAttributes)
{
    if (fAttr & EFI_VAR_HEADER_ATTR_NON_VOLATILE)
        aAttributes.push_back(UefiVariableAttributes_NonVolatile);
    if (fAttr & EFI_VAR_HEADER_ATTR_BOOTSERVICE_ACCESS)
        aAttributes.push_back(UefiVariableAttributes_BootServiceAccess);
    if (fAttr & EFI_VAR_HEADER_ATTR_RUNTIME_ACCESS)
        aAttributes.push_back(UefiVariableAttributes_RuntimeAccess);
    if (fAttr & EFI_VAR_HEADER_ATTR_HW_ERROR_RECORD)
        aAttributes.push_back(UefiVariableAttributes_HwErrorRecord);
    if (fAttr & EFI_AUTH_VAR_HEADER_ATTR_AUTH_WRITE_ACCESS)
        aAttributes.push_back(UefiVariableAttributes_AuthWriteAccess);
    if (fAttr & EFI_AUTH_VAR_HEADER_ATTR_TIME_BASED_AUTH_WRITE_ACCESS)
        aAttributes.push_back(UefiVariableAttributes_AuthTimeBasedWriteAccess);
    if (fAttr & EFI_AUTH_VAR_HEADER_ATTR_APPEND_WRITE)
        aAttributes.push_back(UefiVariableAttributes_AuthAppendWrite);
}


/**
 * Retains the reference of the variable store from the parent.
 *
 * @returns COM status code.
 * @param   fReadonly           Flag whether the access is readonly.
 */
HRESULT UefiVariableStore::i_retainUefiVariableStore(bool fReadonly)
{
    Assert(m->hVfsUefiVarStore = NIL_RTVFS);
    return m->pParent->i_retainUefiVarStore(&m->hVfsUefiVarStore, fReadonly);
}


/**
 * Releases the reference of the variable store from the parent.
 *
 * @returns COM status code.
 */
HRESULT UefiVariableStore::i_releaseUefiVariableStore(void)
{
    RTVFS hVfs = m->hVfsUefiVarStore;

    m->hVfsUefiVarStore = NIL_RTVFS;
    return m->pParent->i_releaseUefiVarStore(hVfs);
}


/**
 * Adds the given variable to the variable store.
 *
 * @returns IPRT status code.
 * @param   pGuid               The EFI GUID of the variable.
 * @param   pszVar              The variable name.
 * @param   fAttr               Attributes for the variable.
 * @param   phVfsFile           Where to return the VFS file handle to the created variable on success.
 */
HRESULT UefiVariableStore::i_uefiVarStoreAddVar(PCEFI_GUID pGuid, const char *pszVar, uint32_t fAttr, PRTVFSFILE phVfsFile)
{
    RTUUID UuidVar;
    RTEfiGuidToUuid(&UuidVar, pGuid);

    char szVarPath[_1K];
    ssize_t cch = RTStrPrintf2(szVarPath, sizeof(szVarPath), "/by-uuid/%RTuuid/%s", &UuidVar, pszVar);
    Assert(cch > 0); RT_NOREF(cch);

    HRESULT hrc = S_OK;
    int vrc = RTVfsFileOpen(m->hVfsUefiVarStore, szVarPath,
                            RTFILE_O_READWRITE | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                            phVfsFile);
    if (   vrc == VERR_PATH_NOT_FOUND
        || vrc == VERR_FILE_NOT_FOUND)
    {
        /*
         * Try to create the owner GUID of the variable by creating the appropriate directory,
         * ignore error if it exists already.
         */
        RTVFSDIR hVfsDirRoot = NIL_RTVFSDIR;
        vrc = RTVfsOpenRoot(m->hVfsUefiVarStore, &hVfsDirRoot);
        if (RT_SUCCESS(vrc))
        {
            char szGuidPath[_1K];
            cch = RTStrPrintf2(szGuidPath, sizeof(szGuidPath), "by-uuid/%RTuuid", &UuidVar);
            Assert(cch > 0);

            RTVFSDIR hVfsDirGuid = NIL_RTVFSDIR;
            vrc = RTVfsDirCreateDir(hVfsDirRoot, szGuidPath, 0755, 0 /*fFlags*/, &hVfsDirGuid);
            if (RT_SUCCESS(vrc))
                RTVfsDirRelease(hVfsDirGuid);
            else if (vrc == VERR_ALREADY_EXISTS)
                vrc = VINF_SUCCESS;

            RTVfsDirRelease(hVfsDirRoot);
        }
        else
            hrc = setError(E_FAIL, tr("Opening variable storage root directory failed: %Rrc"), vrc);

        if (RT_SUCCESS(vrc))
        {
            vrc = RTVfsFileOpen(m->hVfsUefiVarStore, szVarPath,
                               RTFILE_O_READWRITE | RTFILE_O_DENY_NONE | RTFILE_O_CREATE,
                               phVfsFile);
            if (RT_SUCCESS(vrc))
                vrc = i_uefiVarStoreSetVarAttr(pszVar, fAttr);
        }

        if (RT_FAILURE(vrc))
            hrc = setError(E_FAIL, tr("Creating the variable '%s' failed: %Rrc"), pszVar, vrc);
    }

    return hrc;
}


/**
 * Tries to open the given variable from the variable store and returns a file handle.
 *
 * @returns IPRT status code.
 * @param   pszVar              The variable name.
 * @param   phVfsFile           Where to return the VFS file handle to the created variable on success.
 */
HRESULT UefiVariableStore::i_uefiVarStoreOpenVar(const char *pszVar, PRTVFSFILE phVfsFile)
{
    char szVarPath[_1K];
    ssize_t cch = RTStrPrintf2(szVarPath, sizeof(szVarPath), "/by-name/%s", pszVar);
    Assert(cch > 0); RT_NOREF(cch);

    HRESULT hrc = S_OK;
    int vrc = RTVfsFileOpen(m->hVfsUefiVarStore, szVarPath,
                            RTFILE_O_READWRITE | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                            phVfsFile);
    if (   vrc == VERR_PATH_NOT_FOUND
        || vrc == VERR_FILE_NOT_FOUND)
        hrc = setError(VBOX_E_OBJECT_NOT_FOUND, tr("The variable '%s' could not be found"), pszVar);
    else if (RT_FAILURE(vrc))
        hrc = setError(VBOX_E_IPRT_ERROR, tr("Couldn't open variable '%s' (%Rrc)"), pszVar, vrc);

    return hrc;
}


HRESULT UefiVariableStore::i_uefiVarStoreSetVar(PCEFI_GUID pGuid, const char *pszVar, uint32_t fAttr, const void *pvData, size_t cbData)
{
    RTVFSFILE hVfsFileVar = NIL_RTVFSFILE;

    HRESULT hrc = i_uefiVarStoreAddVar(pGuid, pszVar, fAttr, &hVfsFileVar);
    if (SUCCEEDED(hrc))
    {
        int vrc = RTVfsFileWrite(hVfsFileVar, pvData, cbData, NULL /*pcbWritten*/);
        if (RT_FAILURE(vrc))
            hrc = setError(E_FAIL, tr("Setting the variable '%s' failed: %Rrc"), pszVar, vrc);

        RTVfsFileRelease(hVfsFileVar);
    }

    return hrc;
}


HRESULT UefiVariableStore::i_uefiVarStoreQueryVar(const char *pszVar, void *pvData, size_t cbData)
{
    HRESULT hrc = S_OK;

    char szVarPath[_1K];
    ssize_t cch = RTStrPrintf2(szVarPath, sizeof(szVarPath), "/by-name/%s", pszVar);
    Assert(cch > 0); RT_NOREF(cch);

    RTVFSFILE hVfsFile = NIL_RTVFSFILE;
    int vrc = RTVfsFileOpen(m->hVfsUefiVarStore, szVarPath,
                            RTFILE_O_READ | RTFILE_O_DENY_NONE | RTFILE_O_OPEN,
                            &hVfsFile);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTVfsFileRead(hVfsFile, pvData, cbData, NULL /*pcbRead*/);
        if (RT_FAILURE(vrc))
            hrc = setError(E_FAIL, tr("Failed to read data of variable '%s': %Rrc"), pszVar, vrc);

        RTVfsFileRelease(hVfsFile);
    }
    else
        hrc = setError(E_FAIL, tr("Failed to open variable '%s' for reading: %Rrc"), pszVar, vrc);

    return hrc;
}

HRESULT UefiVariableStore::i_uefiSigDbAddSig(RTEFISIGDB hEfiSigDb, const void *pvData, size_t cbData,
                                             const com::Guid &aOwnerUuid, SignatureType_T enmSignatureType)
{
    RTEFISIGTYPE enmSigType = RTEFISIGTYPE_INVALID;

    switch (enmSignatureType)
    {
        case SignatureType_X509:
            enmSigType = RTEFISIGTYPE_X509;
            break;
        case SignatureType_Sha256:
            enmSigType = RTEFISIGTYPE_SHA256;
            break;
        default:
            return setError(E_FAIL, tr("The given signature type is not supported"));
    }

    int vrc = RTEfiSigDbAddSignatureFromBuf(hEfiSigDb, enmSigType, aOwnerUuid.raw(), pvData, cbData);
    if (RT_SUCCESS(vrc))
        return S_OK;

    return setError(E_FAIL, tr("Failed to add signature to the database (%Rrc)"), vrc);
}


HRESULT UefiVariableStore::i_uefiVarStoreAddSignatureToDb(PCEFI_GUID pGuid, const char *pszDb, const void *pvData, size_t cbData,
                                                          const com::Guid &aOwnerUuid, SignatureType_T enmSignatureType, bool fRuntime)
{
    RTVFSFILE hVfsFileSigDb = NIL_RTVFSFILE;

    HRESULT hrc = i_uefiVarStoreAddVar(pGuid, pszDb,
                                         EFI_VAR_HEADER_ATTR_NON_VOLATILE
                                       | EFI_VAR_HEADER_ATTR_BOOTSERVICE_ACCESS
                                       | (fRuntime ? EFI_VAR_HEADER_ATTR_RUNTIME_ACCESS : 0)
                                       | EFI_AUTH_VAR_HEADER_ATTR_TIME_BASED_AUTH_WRITE_ACCESS,
                                       &hVfsFileSigDb);
    if (SUCCEEDED(hrc))
    {
        RTEFISIGDB hEfiSigDb;

        int vrc = RTEfiSigDbCreate(&hEfiSigDb);
        if (RT_SUCCESS(vrc))
        {
            vrc = RTEfiSigDbAddFromExistingDb(hEfiSigDb, hVfsFileSigDb);
            if (RT_SUCCESS(vrc))
            {
                hrc = i_uefiSigDbAddSig(hEfiSigDb, pvData, cbData, aOwnerUuid, enmSignatureType);
                if (SUCCEEDED(hrc))
                {
                    vrc = RTVfsFileSeek(hVfsFileSigDb, 0 /*offSeek*/, RTFILE_SEEK_BEGIN, NULL /*poffActual*/);
                    AssertRC(vrc);

                    vrc = RTEfiSigDbWriteToFile(hEfiSigDb, hVfsFileSigDb);
                    if (RT_FAILURE(vrc))
                        hrc = setError(E_FAIL, tr("Writing updated signature database failed: %Rrc"), vrc);
                }
            }
            else
                hrc = setError(E_FAIL, tr("Loading signature database failed: %Rrc"), vrc);

            RTEfiSigDbDestroy(hEfiSigDb);
        }
        else
            hrc = setError(E_FAIL, tr("Creating signature database failed: %Rrc"), vrc);

        RTVfsFileRelease(hVfsFileSigDb);
    }

    return hrc;
}


HRESULT UefiVariableStore::i_uefiVarStoreAddSignatureToDbVec(PCEFI_GUID pGuid, const char *pszDb, const std::vector<BYTE> &aData,
                                                             const com::Guid &aOwnerUuid, SignatureType_T enmSignatureType, bool fRuntime)
{
    return i_uefiVarStoreAddSignatureToDb(pGuid, pszDb, &aData.front(), aData.size(), aOwnerUuid, enmSignatureType, fRuntime);
}

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
