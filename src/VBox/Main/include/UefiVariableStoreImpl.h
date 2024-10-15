/* $Id: UefiVariableStoreImpl.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM UEFI variable store class implementation
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

#ifndef MAIN_INCLUDED_UefiVariableStoreImpl_h
#define MAIN_INCLUDED_UefiVariableStoreImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "UefiVariableStoreWrap.h"
#include <iprt/types.h>

#include <iprt/formats/efi-common.h>

class NvramStore;
class Machine;

class ATL_NO_VTABLE UefiVariableStore :
    public UefiVariableStoreWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(UefiVariableStore)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
    HRESULT init(NvramStore *aParent, Machine *pMachine);
    void uninit();

    // public methods for internal purposes only

private:

    // Wrapped NVRAM store properties
    HRESULT getSecureBootEnabled(BOOL *pfEnabled);
    HRESULT setSecureBootEnabled(BOOL fEnabled);

    // Wrapped NVRAM store members
    HRESULT addVariable(const com::Utf8Str &aName, const com::Guid &aOwnerUuid, const std::vector<UefiVariableAttributes_T> &aAttributes,
                        const std::vector<BYTE> &aData);
    HRESULT deleteVariable(const com::Utf8Str &aName, const com::Guid &aOwnerUuid);
    HRESULT changeVariable(const com::Utf8Str &aName, const std::vector<BYTE> &aData);
    HRESULT queryVariableByName(const com::Utf8Str &aName, com::Guid &aOwnerUuid, std::vector<UefiVariableAttributes_T> &aAttributes,
                                std::vector<BYTE> &aData);
    HRESULT queryVariables(std::vector<com::Utf8Str> &aNames, std::vector<com::Guid> &aOwnerUuids);
    HRESULT enrollOraclePlatformKey(void);
    HRESULT enrollPlatformKey(const std::vector<BYTE> &aData, const com::Guid &aOwnerUuid);
    HRESULT addKek(const std::vector<BYTE> &aData, const com::Guid &aOwnerUuid, SignatureType_T enmSignatureType);
    HRESULT addSignatureToDb(const std::vector<BYTE> &aData, const com::Guid &aOwnerUuid, SignatureType_T enmSignatureType);
    HRESULT addSignatureToDbx(const std::vector<BYTE> &aData, const com::Guid &aOwnerUuid, SignatureType_T enmSignatureType);
    HRESULT enrollDefaultMsSignatures(void);
    HRESULT addSignatureToMok(const std::vector<BYTE> &aData, const com::Guid &aOwnerUuid, SignatureType_T enmSignatureType);

    int i_uefiVarStoreSetVarAttr(const char *pszVar, uint32_t fAttr);
    int i_uefiVarStoreQueryVarAttr(const char *pszVar, uint32_t *pfAttr);
    int i_uefiVarStoreQueryVarSz(const char *pszVar, uint64_t *pcbVar);
    int i_uefiVarStoreQueryVarOwnerUuid(const char *pszVar, PRTUUID pUuid);
    uint32_t i_uefiVarAttrToMask(const std::vector<UefiVariableAttributes_T> &aAttributes);
    void i_uefiAttrMaskToVec(uint32_t fAttr, std::vector<UefiVariableAttributes_T> &aAttributes);

    HRESULT i_retainUefiVariableStore(bool fReadonly);
    HRESULT i_releaseUefiVariableStore(void);

    HRESULT i_uefiVarStoreAddVar(PCEFI_GUID pGuid, const char *pszVar, uint32_t fAttr, PRTVFSFILE phVfsFile);
    HRESULT i_uefiVarStoreOpenVar(const char *pszVar, PRTVFSFILE phVfsFile);
    HRESULT i_uefiVarStoreSetVar(PCEFI_GUID pGuid, const char *pszVar, uint32_t fAttr, const void *pvData, size_t cbData);
    HRESULT i_uefiVarStoreQueryVar(const char *pszVar, void *pvData, size_t cbData);
    HRESULT i_uefiSigDbAddSig(RTEFISIGDB hEfiSigDb, const void *pvData, size_t cbData, const com::Guid &aOwnerUuid, SignatureType_T enmSignatureType);
    HRESULT i_uefiVarStoreAddSignatureToDbVec(PCEFI_GUID pGuid, const char *pszDb, const std::vector<BYTE> &aData,
                                              const com::Guid &aOwnerUuid, SignatureType_T enmSignatureType, bool fRuntime = true);
    HRESULT i_uefiVarStoreAddSignatureToDb(PCEFI_GUID pGuid, const char *pszDb, const void *pvData, size_t cbData,
                                           const com::Guid &aOwnerUuid, SignatureType_T enmSignatureType, bool fRuntime = true);

    struct Data;            // opaque data struct, defined in UefiVariableStoreImpl.cpp
    Data *m;
};

#endif /* !MAIN_INCLUDED_UefiVariableStoreImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
