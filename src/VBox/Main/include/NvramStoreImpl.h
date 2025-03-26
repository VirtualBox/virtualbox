/* $Id: NvramStoreImpl.h 108960 2025-03-26 13:45:20Z brent.paulson@oracle.com $ */
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

#ifndef MAIN_INCLUDED_NvramStoreImpl_h
#define MAIN_INCLUDED_NvramStoreImpl_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include "NvramStoreWrap.h"
#include "SecretKeyStore.h"
#include <VBox/vmm/pdmdrv.h>
#include <VBox/VBoxCryptoIf.h>


#ifdef VBOX_COM_INPROC
class Console;
struct DRVMAINNVRAMSTORE;
#else
class GuestOSType;

namespace settings
{
    struct NvramSettings;
}
#endif

class ATL_NO_VTABLE NvramStore :
    public NvramStoreWrap
{
public:

    DECLARE_COMMON_CLASS_METHODS(NvramStore)

    HRESULT FinalConstruct();
    void FinalRelease();

    // public initializer/uninitializer for internal purposes only
#ifdef VBOX_COM_INPROC
    HRESULT init(Console *aParent, const com::Utf8Str &strNonVolatileStorageFile);
#else
    HRESULT init(Machine *parent);
    HRESULT init(Machine *parent, NvramStore *that);
    HRESULT initCopy(Machine *parent, NvramStore *that);
#endif
    void uninit();

    // public methods for internal purposes only
#ifndef VBOX_COM_INPROC
    HRESULT i_loadSettings(const settings::NvramSettings &data);
    HRESULT i_saveSettings(settings::NvramSettings &data);
#endif

#ifdef VBOX_COM_INPROC
    static const PDMDRVREG  DrvReg;
#else
    void i_rollback();
    void i_commit();
    void i_copyFrom(NvramStore *aThat);
    HRESULT i_applyDefaults(GuestOSType *aOSType);
#endif

    com::Utf8Str i_getNonVolatileStorageFile();
    void i_updateNonVolatileStorageFile(const com::Utf8Str &aNonVolatileStorageFile);

    int i_loadStore(const char *pszPath);
    int i_saveStore(void);

#ifndef VBOX_COM_INPROC
    HRESULT i_retainUefiVarStore(PRTVFS phVfs, bool fReadonly);
    HRESULT i_releaseUefiVarStore(RTVFS hVfs);
#endif

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
    HRESULT i_updateEncryptionSettings(const com::Utf8Str &strKeyId,
                                       const com::Utf8Str &strKeyStore);
    HRESULT i_getEncryptionSettings(com::Utf8Str &strKeyId,
                                    com::Utf8Str &strKeyStore);

    int i_addPassword(const Utf8Str &strKeyId, const Utf8Str &strPassword);
    int i_removePassword(const Utf8Str &strKeyId);
    int i_removeAllPasswords();
#endif

private:

    int initImpl(void);

    // Wrapped NVRAM store properties
    HRESULT getNonVolatileStorageFile(com::Utf8Str &aNonVolatileStorageFile);
    HRESULT getUefiVariableStore(ComPtr<IUefiVariableStore> &aUefiVarStore);
    HRESULT getKeyId(com::Utf8Str &aKeyId);
    HRESULT getKeyStore(com::Utf8Str &aKeyStore);

    // Wrapped NVRAM store members
    HRESULT initUefiVariableStore(ULONG aSize);

    int i_loadStoreFromTar(RTVFSFSSTREAM hVfsFssTar);
    int i_loadStoreFromDir(RTVFSDIR hVfsDir, const char *pszNamespace);
    int i_saveStoreAsTar(const char *pszPath);
    int i_saveStoreAsDir(const char *pszPath);

    int i_retainCryptoIf(PCVBOXCRYPTOIF *ppCryptoIf);
    int i_releaseCryptoIf(PCVBOXCRYPTOIF pCryptoIf);

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
    int i_setupEncryptionOrDecryption(RTVFSIOSTREAM hVfsIosInOut, bool fEncrypt,
                                      PCVBOXCRYPTOIF *ppCryptoIf, SecretKey **ppKey,
                                      PRTVFSIOSTREAM phVfsIos);
    void i_releaseEncryptionOrDecryptionResources(RTVFSIOSTREAM hVfsIos, PCVBOXCRYPTOIF pCryptoIf,
                                                  SecretKey *pKey);
#endif

#ifdef VBOX_COM_INPROC
    static DECLCALLBACK(int)    i_SsmSaveExec(PPDMDRVINS pDrvIns, PSSMHANDLE pSSM);
    static int                  i_SsmSaveExecInner(struct DRVMAINNVRAMSTORE *pThis, PCPDMDRVHLPR3 pHlp, PSSMHANDLE pSSM,
                                                   void **ppvData, size_t *pcbDataMax) RT_NOEXCEPT;
    static DECLCALLBACK(int)    i_SsmLoadExec(PPDMDRVINS pDrvIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass);
    static int                  i_SsmLoadExecInner(struct DRVMAINNVRAMSTORE *pThis, PCPDMDRVHLPR3 pHlp, PSSMHANDLE pSSM,
                                                   uint32_t cEntries, void **ppvData, size_t *pcbDataMax) RT_NOEXCEPT;

    static DECLCALLBACK(int)    i_nvramStoreQuerySize(PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath,
                                                      uint64_t *pcb);
    static DECLCALLBACK(int)    i_nvramStoreReadAll(PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath,
                                                    void *pvBuf, size_t cbRead);
    static DECLCALLBACK(int)    i_nvramStoreWriteAll(PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath,
                                                     const void *pvBuf, size_t cbWrite);
    static DECLCALLBACK(int)    i_nvramStoreDelete(PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath);
    static DECLCALLBACK(void *) i_drvQueryInterface(PPDMIBASE pInterface, const char *pszIID);
    static DECLCALLBACK(int)    i_drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags);
    static DECLCALLBACK(void)   i_drvDestruct(PPDMDRVINS pDrvIns);
#endif

    struct Data;            // opaque data struct, defined in NvramStoreImpl.cpp
    Data *m;
};

#endif /* !MAIN_INCLUDED_NvramStoreImpl_h */
/* vi: set tabstop=4 shiftwidth=4 expandtab: */
