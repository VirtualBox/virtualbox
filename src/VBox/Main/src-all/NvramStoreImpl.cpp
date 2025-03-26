/* $Id: NvramStoreImpl.cpp 108960 2025-03-26 13:45:20Z brent.paulson@oracle.com $ */
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

#define LOG_GROUP LOG_GROUP_MAIN_NVRAMSTORE
#include "LoggingNew.h"

#include "NvramStoreImpl.h"
#ifdef VBOX_COM_INPROC
# include "ConsoleImpl.h"
#else
# include "MachineImpl.h"
# include "GuestOSTypeImpl.h"
# include "AutoStateDep.h"
#endif
#include "UefiVariableStoreImpl.h"
#include "VirtualBoxImpl.h"

#include "AutoCaller.h"

#include <VBox/com/array.h>
#include <VBox/vmm/pdmdrv.h>
#include <VBox/err.h>

#include <iprt/cpp/utils.h>
#include <iprt/efi.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/vfs.h>
#include <iprt/zip.h>


// defines
////////////////////////////////////////////////////////////////////////////////

/** Version of the NVRAM saved state unit. */
#define NVRAM_STORE_SAVED_STATE_VERSION 1


// globals
////////////////////////////////////////////////////////////////////////////////

/**
 * NVRAM store driver instance data.
 */
typedef struct DRVMAINNVRAMSTORE
{
    /** Pointer to the keyboard object. */
    NvramStore                  *pNvramStore;
    /** Pointer to the driver instance structure. */
    PPDMDRVINS                  pDrvIns;
    /** Our VFS connector interface. */
    PDMIVFSCONNECTOR            IVfs;
} DRVMAINNVRAMSTORE, *PDRVMAINNVRAMSTORE;

/** The NVRAM store map keyed by namespace/entity. */
typedef std::map<Utf8Str, RTVFSFILE> NvramStoreMap;
/** The NVRAM store map iterator. */
typedef std::map<Utf8Str, RTVFSFILE>::iterator NvramStoreIter;

struct BackupableNvramStoreData
{
    BackupableNvramStoreData()
    { }

    /** The NVRAM file path. */
    com::Utf8Str            strNvramPath;
#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
    /** The key id used for encrypting the NVRAM file */
    com::Utf8Str            strKeyId;
    /** The key store containing the encrypting DEK */
    com::Utf8Str            strKeyStore;
#endif
};

/////////////////////////////////////////////////////////////////////////////
// NvramStore::Data structure
/////////////////////////////////////////////////////////////////////////////

struct NvramStore::Data
{
    Data()
        : pParent(NULL)
#ifdef VBOX_COM_INPROC
          , cRefs(0)
          , fSsmSaved(false)
#endif
#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
          , mpKeyStore(NULL)
#endif
    { }

#ifdef VBOX_COM_INPROC
    /** The Console owning this NVRAM store. */
    Console * const         pParent;
    /** Number of references held to this NVRAM store from the various devices/drivers. */
    volatile uint32_t       cRefs;
    /** Flag whether the NVRAM data was saved during a save state operation
     * preventing it from getting written to the backing file. */
    bool                    fSsmSaved;
#else
    /** The Machine object owning this NVRAM store. */
    Machine * const                    pParent;
    /** The peer NVRAM store object. */
    ComObjPtr<NvramStore>              pPeer;
    /** The UEFI variable store. */
    const ComObjPtr<UefiVariableStore> pUefiVarStore;
#endif

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
    /* Store for secret keys. */
    SecretKeyStore                      *mpKeyStore;
#endif

    Backupable<BackupableNvramStoreData> bd;

    /** The NVRAM store. */
    NvramStoreMap                        mapNvram;
};

// constructor / destructor
////////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(NvramStore)

HRESULT NvramStore::FinalConstruct()
{
    return BaseFinalConstruct();
}

void NvramStore::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}

// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initialization stuff shared across the different methods.
 *
 * @returns COM result indicator
 */
int NvramStore::initImpl()
{
    m = new Data();

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
# ifdef VBOX_COM_INPROC
    bool fNonPageable = true;
# else
    /* Non-pageable memory is not accessible for non-VM process */
    bool fNonPageable = false;
# endif

    m->mpKeyStore = new SecretKeyStore(fNonPageable /* fKeyBufNonPageable */);
    AssertReturn(m->mpKeyStore, VERR_NO_MEMORY);
#endif

    return VINF_SUCCESS;
}


#if !defined(VBOX_COM_INPROC)
/**
 * Initializes the NVRAM store object.
 *
 * @returns COM result indicator
 */
HRESULT NvramStore::init(Machine *aParent)
{
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("aParent: %p\n", aParent));

    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    int vrc = initImpl();
    if (RT_FAILURE(vrc))
        return E_FAIL;

    /* share the parent weakly */
    unconst(m->pParent) = aParent;

    m->bd.allocate();

    autoInitSpan.setSucceeded();

    LogFlowThisFuncLeave();
    return S_OK;
}

/**
 *  Initializes the NVRAM store object given another NVRAM store object
 *  (a kind of copy constructor). This object shares data with
 *  the object passed as an argument.
 *
 *  @note This object must be destroyed before the original object
 *  it shares data with is destroyed.
 */
HRESULT NvramStore::init(Machine *aParent, NvramStore *that)
{
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("aParent: %p, that: %p\n", aParent, that));

    ComAssertRet(aParent && that, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    initImpl();

    unconst(m->pParent) = aParent;
    m->pPeer = that;

    AutoWriteLock thatlock(that COMMA_LOCKVAL_SRC_POS);
    m->bd.share(that->m->bd);

    autoInitSpan.setSucceeded();

    LogFlowThisFuncLeave();
    return S_OK;
}

/**
 *  Initializes the guest object given another guest object
 *  (a kind of copy constructor). This object makes a private copy of data
 *  of the original object passed as an argument.
 */
HRESULT NvramStore::initCopy(Machine *aParent, NvramStore *that)
{
    LogFlowThisFuncEnter();
    LogFlowThisFunc(("aParent: %p, that: %p\n", aParent, that));

    ComAssertRet(aParent && that, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    initImpl();

    unconst(m->pParent) = aParent;
    // mPeer is left null

    AutoWriteLock thatlock(that COMMA_LOCKVAL_SRC_POS);
    m->bd.attachCopy(that->m->bd);

    autoInitSpan.setSucceeded();

    LogFlowThisFuncLeave();
    return S_OK;
}

#else

/**
 * Initializes the NVRAM store object.
 *
 * @returns COM result indicator
 * @param aParent                       Handle of our parent object
 * @param strNonVolatileStorageFile     The NVRAM file path.
 */
HRESULT NvramStore::init(Console *aParent, const com::Utf8Str &strNonVolatileStorageFile)
{
    LogFlowThisFunc(("aParent=%p\n", aParent));

    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    initImpl();

    unconst(m->pParent) = aParent;

    m->bd.allocate();
    m->bd->strNvramPath = strNonVolatileStorageFile;

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}
#endif /* VBOX_COM_INPROC */


/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void NvramStore::uninit()
{
    LogFlowThisFuncEnter();

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    unconst(m->pParent) = NULL;
#ifndef VBOX_COM_INPROC
    unconst(m->pUefiVarStore) = NULL;
#endif

    /* Delete the NVRAM content. */
    NvramStoreIter it = m->mapNvram.begin();
    while (it != m->mapNvram.end())
    {
        RTVfsFileRelease(it->second);
        it++;
    }

    m->mapNvram.clear();
    m->bd.free();

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
    if (m->mpKeyStore != NULL)
        delete m->mpKeyStore;
#endif

    delete m;
    m = NULL;

    LogFlowThisFuncLeave();
}

HRESULT NvramStore::getNonVolatileStorageFile(com::Utf8Str &aNonVolatileStorageFile)
{
#ifndef VBOX_COM_INPROC
    Utf8Str strTmp;
    {
        AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
        strTmp = m->bd->strNvramPath;
    }

    AutoReadLock mlock(m->pParent COMMA_LOCKVAL_SRC_POS);
    if (strTmp.isEmpty())
        strTmp = m->pParent->i_getDefaultNVRAMFilename();
    if (strTmp.isNotEmpty())
        m->pParent->i_calculateFullPath(strTmp, aNonVolatileStorageFile);
#else
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);
    aNonVolatileStorageFile = m->bd->strNvramPath;
#endif

    return S_OK;
}


HRESULT NvramStore::getUefiVariableStore(ComPtr<IUefiVariableStore> &aUefiVarStore)
{
#ifndef VBOX_COM_INPROC
    Utf8Str strPath;
    NvramStore::getNonVolatileStorageFile(strPath);
    if (strPath.isEmpty())
        return setError(E_FAIL, tr("No NVRAM store file found"));

    /* We need a write lock because of the lazy initialization. */
    AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);

    /* Check if we have to create the UEFI variable store object */
    HRESULT hrc = S_OK;
    if (!m->pUefiVarStore)
    {
        /* Load the NVRAM file first if it isn't already. */
        if (!m->mapNvram.size())
        {
            int vrc = i_loadStore(strPath.c_str());
            if (RT_FAILURE(vrc))
                hrc = setError(E_FAIL, tr("Loading the NVRAM store failed (%Rrc)\n"), vrc);
        }

        if (SUCCEEDED(hrc))
        {
            NvramStoreIter it = m->mapNvram.find("efi/nvram");
            if (it != m->mapNvram.end())
            {
                unconst(m->pUefiVarStore).createObject();
                m->pUefiVarStore->init(this, m->pParent);
            }
            else
                hrc = setError(VBOX_E_OBJECT_NOT_FOUND, tr("The UEFI NVRAM file is not existing for this machine"));
        }
    }

    if (SUCCEEDED(hrc))
    {
        m->pUefiVarStore.queryInterfaceTo(aUefiVarStore.asOutParam());
        /* The "modified" state is handled by i_retainUefiVarStore. */
    }

    return hrc;
#else
    NOREF(aUefiVarStore);
    return E_NOTIMPL;
#endif
}


HRESULT NvramStore::getKeyId(com::Utf8Str &aKeyId)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
    aKeyId = m->bd->strKeyId;
#else
    aKeyId = com::Utf8Str::Empty;
#endif

    return S_OK;
}


HRESULT NvramStore::getKeyStore(com::Utf8Str &aKeyStore)
{
    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
    aKeyStore = m->bd->strKeyStore;
#else
    aKeyStore = com::Utf8Str::Empty;
#endif

    return S_OK;
}


HRESULT NvramStore::initUefiVariableStore(ULONG aSize)
{
#ifndef VBOX_COM_INPROC
    if (aSize != 0)
        return setError(E_NOTIMPL, tr("Supporting another NVRAM size apart from the default one is not supported right now"));

    /* the machine needs to be mutable */
    AutoMutableStateDependency adep(m->pParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    Utf8Str strPath;
    NvramStore::getNonVolatileStorageFile(strPath);

    /* We need a write lock because of the lazy initialization. */
    AutoReadLock mlock(m->pParent COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);

    if (m->pParent->i_getFirmwareType() == FirmwareType_BIOS)
        return setError(VBOX_E_NOT_SUPPORTED, tr("The selected firmware type doesn't support a UEFI variable store"));

    /* Load the NVRAM file first if it isn't already. */
    HRESULT hrc = S_OK;
    if (!m->mapNvram.size())
    {
        int vrc = i_loadStore(strPath.c_str());
        if (RT_FAILURE(vrc))
            hrc = setError(E_FAIL, tr("Loading the NVRAM store failed (%Rrc)\n"), vrc);
    }

    if (SUCCEEDED(hrc))
    {
        int vrc = VINF_SUCCESS;
        RTVFSFILE hVfsUefiVarStore = NIL_RTVFSFILE;
        NvramStoreIter it = m->mapNvram.find("efi/nvram");
        if (it != m->mapNvram.end())
            hVfsUefiVarStore = it->second;
        else
        {
            /* Create a new file. */
            vrc = RTVfsMemFileCreate(NIL_RTVFSIOSTREAM, 0 /*cbEstimate*/, &hVfsUefiVarStore);
            if (RT_SUCCESS(vrc))
            {
                /** @todo The size is hardcoded to match what the firmware image uses right now which is a gross hack... */
                vrc = RTVfsFileSetSize(hVfsUefiVarStore, 540672, RTVFSFILE_SIZE_F_NORMAL);
                if (RT_SUCCESS(vrc))
                    m->mapNvram["efi/nvram"] = hVfsUefiVarStore;
                else
                    RTVfsFileRelease(hVfsUefiVarStore);
            }
        }

        if (RT_SUCCESS(vrc))
        {
            vrc = RTEfiVarStoreCreate(hVfsUefiVarStore, 0 /*offStore*/, 0 /*cbStore*/, RTEFIVARSTORE_CREATE_F_DEFAULT, 0 /*cbBlock*/,
                                      NULL /*pErrInfo*/);
            if (RT_FAILURE(vrc))
                return setError(E_FAIL, tr("Failed to initialize the UEFI variable store (%Rrc)"), vrc);
        }
        else
            return setError(E_FAIL, tr("Failed to initialize the UEFI variable store (%Rrc)"), vrc);

        m->pParent->i_setModified(Machine::IsModified_NvramStore);
    }

    return hrc;
#else
    NOREF(aSize);
    return E_NOTIMPL;
#endif
}


/**
 * Returns the path of the non-volatile stroage file.
 *
 * @returns Path to non-volatile stroage file. Empty if not supported / found.
 *
 * @note    Convenience function for machine object or other callers.
 */
Utf8Str NvramStore::i_getNonVolatileStorageFile()
{
    AutoCaller autoCaller(this);
    AssertReturn(autoCaller.isOk(), Utf8Str::Empty);

    Utf8Str strTmp;
    NvramStore::getNonVolatileStorageFile(strTmp);
    return strTmp;
}


/**
 * Loads the NVRAM store from the given TAR filesystem stream.
 *
 * @returns IPRT status code.
 * @param   hVfsFssTar          Handle to the tar filesystem stream.
 */
int NvramStore::i_loadStoreFromTar(RTVFSFSSTREAM hVfsFssTar)
{
    int vrc = VINF_SUCCESS;

    /*
     * Process the stream.
     */
    for (;;)
    {
        /*
         * Retrieve the next object.
         */
        char       *pszName;
        RTVFSOBJ    hVfsObj;
        vrc = RTVfsFsStrmNext(hVfsFssTar, &pszName, NULL, &hVfsObj);
        if (RT_FAILURE(vrc))
        {
            if (vrc == VERR_EOF)
                vrc = VINF_SUCCESS;
            break;
        }

        RTFSOBJINFO UnixInfo;
        vrc = RTVfsObjQueryInfo(hVfsObj, &UnixInfo, RTFSOBJATTRADD_UNIX);
        if (RT_SUCCESS(vrc))
        {
            switch (UnixInfo.Attr.fMode & RTFS_TYPE_MASK)
            {
                case RTFS_TYPE_FILE:
                {
                    LogRel(("NvramStore: Loading '%s' from archive\n", pszName));
                    RTVFSIOSTREAM hVfsIosEntry = RTVfsObjToIoStream(hVfsObj);
                    Assert(hVfsIosEntry != NIL_RTVFSIOSTREAM);

                    RTVFSFILE hVfsFileEntry;
                    vrc = RTVfsMemorizeIoStreamAsFile(hVfsIosEntry, RTFILE_O_READ | RTFILE_O_WRITE, &hVfsFileEntry);
                    if (RT_FAILURE(vrc))
                        break;
                    RTVfsIoStrmRelease(hVfsIosEntry);

                    m->mapNvram[Utf8Str(pszName)] = hVfsFileEntry;
                    break;
                }
                case RTFS_TYPE_DIRECTORY:
                    break;
                default:
                    vrc = VERR_NOT_SUPPORTED;
                    break;
            }
        }

        /*
         * Release the current object and string.
         */
        RTVfsObjRelease(hVfsObj);
        RTStrFree(pszName);

        if (RT_FAILURE(vrc))
            break;
    }

    return vrc;
}

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION

/**
 * Sets up the encryption or decryption machinery.
 *
 * @returns VBox status code.
 * @param   hVfsIosInOut        Handle to the input stream to be decrypted or the destination to the encrypted
 *                              output is written to.
 * @param   fEncrypt            Flag whether to setup encryption or decryption.
 * @param   ppCryptoIf          Where to store the pointer to the cryptographic interface which needs to be released
 *                              when done.
 * @param   ppKey               Where to store the pointer to the secret key buffer which needs to be released when done.
 * @param   phVfsIos            Where to store the handle to the plaintext I/O stream (either input or output) on success.
 */
int NvramStore::i_setupEncryptionOrDecryption(RTVFSIOSTREAM hVfsIosInOut, bool fEncrypt,
                                              PCVBOXCRYPTOIF *ppCryptoIf, SecretKey **ppKey,
                                              PRTVFSIOSTREAM phVfsIos)
{
    int vrc = VINF_SUCCESS;
    PCVBOXCRYPTOIF pCryptoIf = NULL;
    SecretKey *pKey = NULL;
    const char *pszPassword = NULL;

    vrc = i_retainCryptoIf(&pCryptoIf);
    if (RT_SUCCESS(vrc))
    {
        vrc = m->mpKeyStore->retainSecretKey(m->bd->strKeyId, &pKey);
        if (RT_SUCCESS(vrc))
        {
            pszPassword = (const char *)pKey->getKeyBuffer();
            if (fEncrypt)
                vrc = pCryptoIf->pfnCryptoIoStrmFromVfsIoStrmEncrypt(hVfsIosInOut, m->bd->strKeyStore.c_str(), pszPassword,
                                                                     phVfsIos);
            else
                vrc = pCryptoIf->pfnCryptoIoStrmFromVfsIoStrmDecrypt(hVfsIosInOut, m->bd->strKeyStore.c_str(), pszPassword,
                                                                     phVfsIos);
            if (RT_SUCCESS(vrc))
            {
                *ppCryptoIf = pCryptoIf;
                *ppKey      = pKey;
                return VINF_SUCCESS;
            }
            else
                LogRelMax(10, ("Failed to decrypt the NVRAM store using secret key ID '%s' with %Rrc\n",
                               m->bd->strKeyId.c_str(), vrc));

            m->mpKeyStore->releaseSecretKey(m->bd->strKeyId);
        }
        else
            LogRelMax(10, ("Failed to retain the secret key ID '%s' with %Rrc\n",
                           m->bd->strKeyId.c_str(), vrc));

        i_releaseCryptoIf(pCryptoIf);
    }
    else
        LogRelMax(10, ("Failed to retain the cryptographic interface with %Rrc\n", vrc));

    return vrc;
}

/**
 * Releases all resources acquired in NvramStore::i_setupEncryptionOrDecryption().
 *
 * @param   hVfsIos             Handle to the I/O stream previously created.
 * @param   pCryptoIf           Pointer to the cryptographic interface being released.
 * @param   pKey                Pointer to the key buffer being released.
 */
void NvramStore::i_releaseEncryptionOrDecryptionResources(RTVFSIOSTREAM hVfsIos, PCVBOXCRYPTOIF pCryptoIf,
                                                          SecretKey *pKey)
{
    Assert(hVfsIos != NIL_RTVFSIOSTREAM);
    AssertPtr(pCryptoIf);
    AssertPtr(pKey);

    i_releaseCryptoIf(pCryptoIf);
    pKey->release();
    RTVfsIoStrmRelease(hVfsIos);
}

#endif /* VBOX_WITH_FULL_VM_ENCRYPTION */

/**
 * Loads the NVRAM store from the given VFS directory handle.
 *
 * @returns IPRT status code.
 * @param   hVfsDir             Handle to the NVRAM root VFS directory.
 * @param   pszNamespace        The namespace to load the content for.
 */
int NvramStore::i_loadStoreFromDir(RTVFSDIR hVfsDir, const char *pszNamespace)
{
    int vrc = VINF_SUCCESS;

    RTVFSDIR hNamespaceDir = NIL_RTVFSDIR;
    vrc = RTVfsDirOpenDir(hVfsDir, pszNamespace, 0 /*fFlags*/, &hNamespaceDir);
    if (RT_SUCCESS(vrc))
    {
        for (;;)
        {
            RTDIRENTRYEX DirEntry; /* ASSUMES that no entry has a longer name than what RTDIRENTRYEX provides by default. */
            size_t cbDir = sizeof(DirEntry);
            vrc = RTVfsDirReadEx(hNamespaceDir, &DirEntry, &cbDir, RTFSOBJATTRADD_NOTHING);
            if (RT_FAILURE(vrc))
            {
                if (vrc == VERR_NO_MORE_FILES)
                    vrc = VINF_SUCCESS;
                break;
            }

            if (RT_SUCCESS(vrc))
            {
                switch (DirEntry.Info.Attr.fMode & RTFS_TYPE_MASK)
                {
                    case RTFS_TYPE_FILE:
                    {
                        LogRel(("NvramStore: Loading '%s' from directory '%s'\n", DirEntry.szName, pszNamespace));

                        RTVFSIOSTREAM hVfsIosEntry;
                        vrc = RTVfsDirOpenFileAsIoStream(hNamespaceDir, DirEntry.szName, RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_NONE, &hVfsIosEntry);
                        if (RT_SUCCESS(vrc))
                        {
                            RTVFSIOSTREAM hVfsIosDecrypted = NIL_RTVFSIOSTREAM;

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
                            PCVBOXCRYPTOIF pCryptoIf = NULL;
                            SecretKey *pKey = NULL;

                            if (   m->bd->strKeyId.isNotEmpty()
                                && m->bd->strKeyStore.isNotEmpty())
                                vrc = i_setupEncryptionOrDecryption(hVfsIosEntry, false /*fEncrypt*/,
                                                                    &pCryptoIf, &pKey, &hVfsIosDecrypted);
#endif
                            if (RT_SUCCESS(vrc))
                            {
                                RTVFSFILE hVfsFileEntry;
                                vrc = RTVfsMemorizeIoStreamAsFile(hVfsIosDecrypted != NIL_RTVFSIOSTREAM
                                                                  ? hVfsIosDecrypted
                                                                  : hVfsIosEntry,
                                                                  RTFILE_O_READ | RTFILE_O_WRITE, &hVfsFileEntry);
                                if (RT_SUCCESS(vrc))
                                    m->mapNvram[Utf8StrFmt("%s/%s", pszNamespace, DirEntry.szName)] = hVfsFileEntry;
                            }

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
                            if (hVfsIosDecrypted != NIL_RTVFSIOSTREAM)
                                i_releaseEncryptionOrDecryptionResources(hVfsIosDecrypted, pCryptoIf, pKey);
#endif

                            RTVfsIoStrmRelease(hVfsIosEntry);
                        }
                        else
                            LogRel(("Failed to open '%s' in NVRAM store '%s', vrc=%Rrc\n", DirEntry.szName, pszNamespace, vrc));

                        break;
                    }
                    case RTFS_TYPE_DIRECTORY:
                        break;
                    default:
                        vrc = VERR_NOT_SUPPORTED;
                        break;
                }
            }

            if (RT_FAILURE(vrc))
                break;
        }

        RTVfsDirRelease(hNamespaceDir);
    }

    return vrc;
}


/**
 * Loads the NVRAM store.
 *
 * @returns IPRT status code.
 */
int NvramStore::i_loadStore(const char *pszPath)
{
    AssertPtrReturn(pszPath, VERR_INVALID_POINTER);
    AssertReturn(*pszPath, VERR_PATH_ZERO_LENGTH); /* IPRT below doesn't like empty strings. */

    uint64_t cbStore = 0;
    int vrc = RTFileQuerySizeByPath(pszPath, &cbStore);
    if (RT_SUCCESS(vrc))
    {
        if (cbStore <= _1M) /* Arbitrary limit to fend off bogus files because the file will be read into memory completely. */
        {
            /*
             * Old NVRAM files just consist of the EFI variable store whereas starting
             * with VirtualBox 7.0 and the introduction of the TPM the need to handle multiple
             * independent NVRAM files came up. For those scenarios all NVRAM states are collected
             * in a tar archive.
             *
             * Here we detect whether the file is the new tar archive format or whether it is just
             * the plain EFI variable store file.
             */
            RTVFSIOSTREAM hVfsIosNvram;
            vrc = RTVfsIoStrmOpenNormal(pszPath, RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_NONE,
                                        &hVfsIosNvram);
            if (RT_SUCCESS(vrc))
            {
                RTVFSIOSTREAM hVfsIosDecrypted = NIL_RTVFSIOSTREAM;

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
                PCVBOXCRYPTOIF pCryptoIf = NULL;
                SecretKey *pKey = NULL;

                if (   m->bd->strKeyId.isNotEmpty()
                    && m->bd->strKeyStore.isNotEmpty())
                    vrc = i_setupEncryptionOrDecryption(hVfsIosNvram, false /*fEncrypt*/,
                                                        &pCryptoIf, &pKey, &hVfsIosDecrypted);
#endif
                if (RT_SUCCESS(vrc))
                {
                    /* Read the content. */
                    RTVFSFILE hVfsFileNvram;
                    vrc = RTVfsMemorizeIoStreamAsFile(  hVfsIosDecrypted != NIL_RTVFSIOSTREAM
                                                      ? hVfsIosDecrypted
                                                      : hVfsIosNvram,
                                                      RTFILE_O_READ, &hVfsFileNvram);
                    if (RT_SUCCESS(vrc))
                    {
                        if (RT_SUCCESS(vrc))
                        {
                            /* Try to parse it as an EFI variable store. */
                            RTERRINFOSTATIC ErrInfo;
                            RTVFS hVfsEfiVarStore;
                            vrc = RTEfiVarStoreOpenAsVfs(hVfsFileNvram, RTVFSMNT_F_READ_ONLY, 0 /*fVarStoreFlags*/,
                                                         &hVfsEfiVarStore, RTErrInfoInitStatic(&ErrInfo));
                            if (RT_SUCCESS(vrc))
                            {
                                vrc = RTVfsFileSeek(hVfsFileNvram, 0 /*offSeek*/, RTFILE_SEEK_BEGIN, NULL /*poffActual*/);
                                AssertRC(vrc);

                                RTVfsFileRetain(hVfsFileNvram); /* Retain a new reference for the map. */
                                m->mapNvram[Utf8Str("efi/nvram")] = hVfsFileNvram;

                                RTVfsRelease(hVfsEfiVarStore);
                            }
                            else if (vrc == VERR_VFS_UNKNOWN_FORMAT)
                            {
                                /* Check for the new style tar archive. */
                                vrc = RTVfsFileSeek(hVfsFileNvram, 0 /*offSeek*/, RTFILE_SEEK_BEGIN, NULL /*poffActual*/);
                                AssertRC(vrc);

                                RTVFSIOSTREAM hVfsIosTar = RTVfsFileToIoStream(hVfsFileNvram);
                                Assert(hVfsIosTar != NIL_RTVFSIOSTREAM);

                                RTVFSFSSTREAM hVfsFssTar;
                                vrc = RTZipTarFsStreamFromIoStream(hVfsIosTar, 0 /*fFlags*/, &hVfsFssTar);
                                RTVfsIoStrmRelease(hVfsIosTar);
                                if (RT_SUCCESS(vrc))
                                {
                                    vrc = i_loadStoreFromTar(hVfsFssTar);
                                    RTVfsFsStrmRelease(hVfsFssTar);
                                }
                                else
                                    LogRel(("The given NVRAM file is neither a raw UEFI variable store nor a tar archive (opening failed with %Rrc)\n", vrc));
                            }
                            else
                                LogRel(("Opening the UEFI variable store '%s' failed with %Rrc%RTeim\n", pszPath, vrc, &ErrInfo.Core));

                            RTVfsFileRelease(hVfsFileNvram);
                        }
                        else
                            LogRel(("Failed to memorize NVRAM store '%s' with %Rrc\n", pszPath, vrc));
                    }
                }

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
                if (hVfsIosDecrypted != NIL_RTVFSIOSTREAM)
                    i_releaseEncryptionOrDecryptionResources(hVfsIosDecrypted, pCryptoIf, pKey);
#endif

                RTVfsIoStrmRelease(hVfsIosNvram);
            }
            else
                LogRelMax(10, ("NVRAM store '%s' couldn't be opened with %Rrc\n", pszPath, vrc));
        }
        else
        {
            LogRelMax(10, ("NVRAM store '%s' exceeds limit of %u bytes, actual size is %u\n",
                           pszPath, _1M, cbStore));
            vrc = VERR_OUT_OF_RANGE;
        }
    }
    else if (vrc == VERR_IS_A_DIRECTORY) /* Valid if the NVRAM was saved with VBoxInternal2/SaveNvramContentAsDirectory 1. */
    {
        RTVFSDIR hNvramDir = NIL_RTVFSDIR;
        vrc = RTVfsDirOpenNormal(pszPath, 0 /*fFlags*/, &hNvramDir);
        if (RT_SUCCESS(vrc))
        {
            for (;;)
            {
                RTDIRENTRYEX DirEntry; /* ASSUMES that no entry has a longer name than what RTDIRENTRYEX provides by default. */
                size_t cbDir = sizeof(DirEntry);

                vrc = RTVfsDirReadEx(hNvramDir, &DirEntry, &cbDir, RTFSOBJATTRADD_NOTHING);
                if (RT_FAILURE(vrc))
                {
                    if (vrc == VERR_NO_MORE_FILES)
                        vrc = VINF_SUCCESS;
                    break;
                }

                /* This ASSUMES that the structure follows the <namespace>/<file> naming scheme. */
                if (RT_SUCCESS(vrc))
                {
                    switch (DirEntry.Info.Attr.fMode & RTFS_TYPE_MASK)
                    {
                        case RTFS_TYPE_FILE:
                            break;
                        case RTFS_TYPE_DIRECTORY:
                        {
                            if (   (DirEntry.szName[0] == '.' && DirEntry.szName[1] == '\0')
                                || (DirEntry.szName[0] == '.' && DirEntry.szName[1] == '.' && DirEntry.szName[2] == '\0'))
                                break;

                            vrc = i_loadStoreFromDir(hNvramDir, DirEntry.szName);
                            break;
                        }
                        default:
                            vrc = VERR_NOT_SUPPORTED;
                            break;
                    }
                }

                if (RT_FAILURE(vrc))
                    break;
            }

            RTVfsDirRelease(hNvramDir);
        }
        else
            LogRelMax(10, ("NVRAM store '%s' couldn't be opened as a directory, vrc=%Rrc\n", pszPath, vrc));

    }
    else if (vrc == VERR_FILE_NOT_FOUND) /* Valid for the first run where no NVRAM file is there. */
        vrc = VINF_SUCCESS;

    return vrc;
}


/**
 * Saves the NVRAM store as a tar archive.
 */
int NvramStore::i_saveStoreAsTar(const char *pszPath)
{
    uint32_t        offError = 0;
    RTERRINFOSTATIC ErrInfo;
    RTVFSIOSTREAM   hVfsIos;

    int vrc = RTVfsChainOpenIoStream(pszPath, RTFILE_O_WRITE | RTFILE_O_DENY_WRITE | RTFILE_O_CREATE_REPLACE,
                                     &hVfsIos, &offError, RTErrInfoInitStatic(&ErrInfo));
    if (RT_SUCCESS(vrc))
    {
        RTVFSIOSTREAM hVfsIosEncrypted = NIL_RTVFSIOSTREAM;

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
        PCVBOXCRYPTOIF pCryptoIf = NULL;
        SecretKey *pKey = NULL;

        if (   m->bd->strKeyId.isNotEmpty()
            && m->bd->strKeyStore.isNotEmpty())
            vrc = i_setupEncryptionOrDecryption(hVfsIos, true /*fEncrypt*/,
                                                &pCryptoIf, &pKey, &hVfsIosEncrypted);
#endif

        if (RT_SUCCESS(vrc))
        {
            RTVFSFSSTREAM hVfsFss;
            vrc = RTZipTarFsStreamToIoStream(  hVfsIosEncrypted != NIL_RTVFSIOSTREAM
                                             ? hVfsIosEncrypted
                                             : hVfsIos,
                                             RTZIPTARFORMAT_GNU, 0 /*fFlags*/, &hVfsFss);
            if (RT_SUCCESS(vrc))
            {
                NvramStoreIter it = m->mapNvram.begin();

                while (it != m->mapNvram.end())
                {
                    RTVFSFILE hVfsFile = it->second;

                    vrc = RTVfsFileSeek(hVfsFile, 0 /*offSeek*/, RTFILE_SEEK_BEGIN, NULL /*poffActual*/);
                    AssertRC(vrc);

                    RTVFSOBJ hVfsObj = RTVfsObjFromFile(hVfsFile);
                    vrc = RTVfsFsStrmAdd(hVfsFss, it->first.c_str(), hVfsObj, 0 /*fFlags*/);
                    RTVfsObjRelease(hVfsObj);
                    if (RT_FAILURE(vrc))
                        break;

                    it++;
                }

                RTVfsFsStrmRelease(hVfsFss);
            }

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
            if (hVfsIosEncrypted != NIL_RTVFSIOSTREAM)
                i_releaseEncryptionOrDecryptionResources(hVfsIosEncrypted, pCryptoIf, pKey);
#endif
        }

        RTVfsIoStrmRelease(hVfsIos);
    }

    return vrc;
}


/**
 * Saves the NVRAM store as a directory tree.
 */
int NvramStore::i_saveStoreAsDir(const char *pszPath)
{
    int vrc = VINF_SUCCESS;
    if (RTDirExists(pszPath))
        vrc = RTDirRemoveRecursive(pszPath, RTDIRRMREC_F_CONTENT_AND_DIR);
    else if (RTPathExists(pszPath))
        vrc = RTPathUnlink(pszPath, 0 /*fUnlink*/);
    if (RT_FAILURE(vrc))
    {
        LogRel(("Failed to delete existing NVRAM store '%s': %Rrc\n", pszPath, vrc));
        return vrc;
    }

    vrc = RTDirCreate(pszPath, 0700 /*fMode*/, RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_NOT_CRITICAL);
    if (RT_SUCCESS(vrc))
    {
        NvramStoreIter it = m->mapNvram.begin();

        while (it != m->mapNvram.end())
        {
            /** @todo r=aeichner This is pretty in-efficient but not called often (not at all by default)
             *                   and there aren't many entries anyway. */
            char szPathOut[RTPATH_MAX];
            char szPathFile[RTPATH_MAX];

            /* Construct the path excluding the filename. */
            vrc = RTStrCopy(szPathFile, sizeof(szPathFile), it->first.c_str());
            if (RT_FAILURE(vrc))
                break;
            RTPathStripFilename(szPathFile);
            vrc = RTPathJoin(szPathOut, sizeof(szPathOut), pszPath, szPathFile);
            if (RT_FAILURE(vrc))
                break;

            /* Create the directory structure. */
            vrc = RTDirCreateFullPathEx(szPathOut, 0700 /*fMode*/, RTDIRCREATE_FLAGS_NOT_CONTENT_INDEXED_NOT_CRITICAL);
            if (RT_FAILURE(vrc))
                break;

            vrc = RTVfsFileSeek(it->second, 0 /*offSeek*/, RTFILE_SEEK_BEGIN, NULL /*poffActual*/);
            AssertRC(vrc);

            /* Construct path, including the filename now. */
            vrc = RTPathJoin(szPathOut, sizeof(szPathOut), pszPath, it->first.c_str());
            if (RT_FAILURE(vrc))
                break;

            /* Write the file, encrypting it if required. */
            RTVFSFILE hVfsFile;
            vrc = RTVfsFileOpenNormal(szPathOut, RTFILE_O_WRITE | RTFILE_O_DENY_WRITE | RTFILE_O_CREATE_REPLACE,
                                      &hVfsFile);
            if (RT_SUCCESS(vrc))
            {
                RTVFSIOSTREAM hVfsIos = RTVfsFileToIoStream(hVfsFile);
                RTVFSIOSTREAM hVfsIosEncrypted = NIL_RTVFSIOSTREAM;

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
                PCVBOXCRYPTOIF pCryptoIf = NULL;
                SecretKey *pKey = NULL;

                if (   m->bd->strKeyId.isNotEmpty()
                    && m->bd->strKeyStore.isNotEmpty())
                    vrc = i_setupEncryptionOrDecryption(hVfsIos, true /*fEncrypt*/,
                                                        &pCryptoIf, &pKey, &hVfsIosEncrypted);
#endif

                if (RT_SUCCESS(vrc))
                {
                    RTVFSIOSTREAM hVfsIosSrc = RTVfsFileToIoStream(it->second);
                    vrc = RTVfsUtilPumpIoStreams(hVfsIosSrc,
                                                   hVfsIosEncrypted != NIL_RTVFSIOSTREAM
                                                 ? hVfsIosEncrypted
                                                 : hVfsIos, 0 /*cbBufHint*/);
                    RTVfsIoStrmRelease(hVfsIosSrc);
#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
                    if (hVfsIosEncrypted != NIL_RTVFSIOSTREAM)
                        i_releaseEncryptionOrDecryptionResources(hVfsIosEncrypted, pCryptoIf, pKey);
#endif
                }

                RTVfsIoStrmRelease(hVfsIos);
                RTVfsFileRelease(hVfsFile);
            }

            it++;
        }

        /* Cleanup in case of error. */
        if (RT_FAILURE(vrc))
        {
            int vrc2 = RTDirRemoveRecursive(pszPath, RTDIRRMREC_F_CONTENT_AND_DIR);
            if (RT_FAILURE(vrc2))
                LogRel(("Cleaning up NVRAM store '%s' failed with %Rrc (after creation failed with %Rrc)\n", pszPath,   vrc2, vrc));
        }
    }
    else
        LogRel(("NVRAM store '%s' directory creation failed: %Rrc\n", pszPath, vrc));

    return vrc;
}


int NvramStore::i_retainCryptoIf(PCVBOXCRYPTOIF *ppCryptoIf)
{
#ifdef VBOX_COM_INPROC
    return m->pParent->i_retainCryptoIf(ppCryptoIf);
#else
    HRESULT hrc = m->pParent->i_getVirtualBox()->i_retainCryptoIf(ppCryptoIf);
    if (SUCCEEDED(hrc))
        return VINF_SUCCESS;

    return VERR_COM_IPRT_ERROR;
#endif
}


int NvramStore::i_releaseCryptoIf(PCVBOXCRYPTOIF pCryptoIf)
{
#ifdef VBOX_COM_INPROC
    return m->pParent->i_releaseCryptoIf(pCryptoIf);
#else
    HRESULT hrc = m->pParent->i_getVirtualBox()->i_releaseCryptoIf(pCryptoIf);
    if (SUCCEEDED(hrc))
        return VINF_SUCCESS;

    return VERR_COM_IPRT_ERROR;
#endif
}


/**
 * Saves the NVRAM store.
 *
 * @returns IPRT status code.
 */
int NvramStore::i_saveStore(void)
{
    int vrc = VINF_SUCCESS;

    Utf8Str strPath;
    NvramStore::getNonVolatileStorageFile(strPath);

    /*
     * Only store the NVRAM content if the path is not empty, if it is
     * this means the VM was just created and the store was not saved yet,
     * see @bugref{10191}.
     */
    if (strPath.isNotEmpty())
    {
        /*
         * Skip creating the tar archive if only the UEFI NVRAM content is available in order
         * to maintain backwards compatibility. As soon as there is more than one entry or
         * it doesn't belong to the UEFI the tar archive will be created.
         */
        AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
        if (   m->mapNvram.size() == 1
            && m->mapNvram.begin()->first == "efi/nvram")
        {
            RTVFSFILE hVfsFileNvram = m->mapNvram.begin()->second;

            vrc = RTVfsFileSeek(hVfsFileNvram, 0 /*offSeek*/, RTFILE_SEEK_BEGIN, NULL /*poffActual*/);
            AssertLogRelRC(vrc);

            RTVFSIOSTREAM hVfsIosDst;
            vrc = RTVfsIoStrmOpenNormal(strPath.c_str(), RTFILE_O_CREATE_REPLACE | RTFILE_O_WRITE | RTFILE_O_DENY_NONE,
                                        &hVfsIosDst);
            if (RT_SUCCESS(vrc))
            {
                RTVFSIOSTREAM hVfsIosSrc = RTVfsFileToIoStream(hVfsFileNvram);
                Assert(hVfsIosSrc != NIL_RTVFSIOSTREAM);

                RTVFSIOSTREAM hVfsIosEncrypted = NIL_RTVFSIOSTREAM;

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
                PCVBOXCRYPTOIF pCryptoIf = NULL;
                SecretKey *pKey = NULL;

                if (   m->bd->strKeyId.isNotEmpty()
                    && m->bd->strKeyStore.isNotEmpty())
                    vrc = i_setupEncryptionOrDecryption(hVfsIosDst, true /*fEncrypt*/,
                                                        &pCryptoIf, &pKey, &hVfsIosEncrypted);
#endif

                vrc = RTVfsUtilPumpIoStreams(hVfsIosSrc,
                                               hVfsIosEncrypted != NIL_RTVFSIOSTREAM
                                             ? hVfsIosEncrypted
                                             : hVfsIosDst
                                             , 0 /*cbBufHint*/);

#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
                if (hVfsIosEncrypted != NIL_RTVFSIOSTREAM)
                    i_releaseEncryptionOrDecryptionResources(hVfsIosEncrypted, pCryptoIf, pKey);
#endif

                RTVfsIoStrmRelease(hVfsIosSrc);
                RTVfsIoStrmRelease(hVfsIosDst);
            }
        }
        else if (m->mapNvram.size())
        {
            /* Check whether the NVRAM content is supposed to be saved under a directory. */
#ifndef VBOX_COM_INPROC
            Machine * const         pMachine = m->pParent;
#else
            const ComPtr<IMachine> &pMachine = m->pParent->i_machine();
#endif

            Bstr bstrName("VBoxInternal2/SaveNvramContentAsDirectory");
            Bstr bstrValue;
            HRESULT hrc = pMachine->GetExtraData(bstrName.raw(), bstrValue.asOutParam());
            if (FAILED(hrc))
                throw hrc;

            bool fSaveAsDir = bstrValue == "1";

            if (fSaveAsDir)
                vrc = i_saveStoreAsDir(strPath.c_str());
            else
                vrc = i_saveStoreAsTar(strPath.c_str());
        }
        /* else: No NVRAM content to store so we are done here. */
    }

    return vrc;
}


#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
HRESULT NvramStore::i_updateEncryptionSettings(const com::Utf8Str &strKeyId,
                                               const com::Utf8Str &strKeyStore)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd.backup();
    m->bd->strKeyId = strKeyId;
    m->bd->strKeyStore = strKeyStore;

    /* clear all passwords because they are invalid now */
    m->mpKeyStore->deleteAllSecretKeys(false, true);

    alock.release();
    AutoWriteLock mlock(m->pParent COMMA_LOCKVAL_SRC_POS);
#ifndef VBOX_COM_INPROC
    m->pParent->i_setModified(Machine::IsModified_NvramStore);
#endif
    return S_OK;
}


HRESULT NvramStore::i_getEncryptionSettings(com::Utf8Str &strKeyId,
                                            com::Utf8Str &strKeyStore)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock alock(this COMMA_LOCKVAL_SRC_POS);

    strKeyId    = m->bd->strKeyId;
    strKeyStore = m->bd->strKeyStore;

    return S_OK;
}


int NvramStore::i_addPassword(const Utf8Str &strKeyId, const Utf8Str &strPassword)
{
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), VERR_INVALID_STATE);

    /* keep only required password */
    if (strKeyId != m->bd->strKeyId)
        return VINF_SUCCESS;

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    return m->mpKeyStore->addSecretKey(strKeyId, (const uint8_t *)strPassword.c_str(), strPassword.length() + 1);
}


int NvramStore::i_removePassword(const Utf8Str &strKeyId)
{
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), VERR_INVALID_STATE);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    return m->mpKeyStore->deleteSecretKey(strKeyId);
}


int NvramStore::i_removeAllPasswords()
{
    AutoCaller autoCaller(this);
    AssertComRCReturn(autoCaller.hrc(), VERR_INVALID_STATE);

    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    m->mpKeyStore->deleteAllSecretKeys(false, true);
    return VINF_SUCCESS;
}
#endif


#ifndef VBOX_COM_INPROC

HRESULT NvramStore::i_retainUefiVarStore(PRTVFS phVfs, bool fReadonly)
{
    /* the machine needs to be mutable unless fReadonly is set */
    AutoMutableStateDependency adep(fReadonly ? NULL : m->pParent);
    if (FAILED(adep.hrc())) return adep.hrc();

    AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);

    HRESULT hrc = S_OK;
    NvramStoreIter it = m->mapNvram.find("efi/nvram");
    if (it != m->mapNvram.end())
    {
        RTVFSFILE hVfsFileNvram = it->second;
        RTVFS hVfsEfiVarStore;
        uint32_t fMntFlags = fReadonly ? RTVFSMNT_F_READ_ONLY : 0;

        int vrc = RTEfiVarStoreOpenAsVfs(hVfsFileNvram, fMntFlags, 0 /*fVarStoreFlags*/, &hVfsEfiVarStore,
                                         NULL /*pErrInfo*/);
        if (RT_SUCCESS(vrc))
        {
            *phVfs = hVfsEfiVarStore;
            if (!fReadonly)
                m->pParent->i_setModified(Machine::IsModified_NvramStore);
        }
        else
            hrc = setError(E_FAIL, tr("Opening the UEFI variable store failed (%Rrc)"), vrc);
    }
    else
        hrc = setError(VBOX_E_OBJECT_NOT_FOUND, tr("The UEFI NVRAM file is not existing for this machine"));

    return hrc;
}


HRESULT NvramStore::i_releaseUefiVarStore(RTVFS hVfs)
{
    RTVfsRelease(hVfs);
    return S_OK;
}


/**
 *  Loads settings from the given machine node.
 *  May be called once right after this object creation.
 *
 *  @param data Configuration settings.
 *
 *  @note Locks this object for writing.
 */
HRESULT NvramStore::i_loadSettings(const settings::NvramSettings &data)
{
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoReadLock mlock(m->pParent COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    m->bd->strNvramPath = data.strNvramPath;
#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
    m->bd->strKeyId     = data.strKeyId;
    m->bd->strKeyStore  = data.strKeyStore;
#endif

    Utf8Str strTmp(m->bd->strNvramPath);
    if (strTmp.isNotEmpty())
        m->pParent->i_copyPathRelativeToMachine(strTmp, m->bd->strNvramPath);
    if (   m->pParent->i_getFirmwareType() == FirmwareType_BIOS
        || m->bd->strNvramPath == m->pParent->i_getDefaultNVRAMFilename())
        m->bd->strNvramPath.setNull();

    LogFlowThisFuncLeave();
    return S_OK;
}

/**
 *  Saves settings to the given machine node.
 *
 *  @param data Configuration settings.
 *
 *  @note Locks this object for writing.
 */
HRESULT NvramStore::i_saveSettings(settings::NvramSettings &data)
{
    AutoCaller autoCaller(this);
    AssertComRCReturnRC(autoCaller.hrc());

    AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);

    data.strNvramPath = m->bd->strNvramPath;
#ifdef VBOX_WITH_FULL_VM_ENCRYPTION
    data.strKeyId     = m->bd->strKeyId;
    data.strKeyStore  = m->bd->strKeyStore;
#endif

    int vrc = i_saveStore();
    if (RT_FAILURE(vrc))
        return setError(E_FAIL, tr("Failed to save the NVRAM content to disk (%Rrc)"), vrc);

    return S_OK;
}

void NvramStore::i_rollback()
{
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    m->bd.rollback();
}

void NvramStore::i_commit()
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertReturnVoid(autoCaller.isOk());

    /* sanity too */
    AutoCaller peerCaller(m->pPeer);
    AssertReturnVoid(peerCaller.isOk());

    /* lock both for writing since we modify both (mPeer is "master" so locked
     * first) */
    AutoMultiWriteLock2 alock(m->pPeer, this COMMA_LOCKVAL_SRC_POS);

    if (m->bd.isBackedUp())
    {
        m->bd.commit();
        if (m->pPeer)
        {
            /* attach new data to the peer and reshare it */
            AutoWriteLock peerlock(m->pPeer COMMA_LOCKVAL_SRC_POS);
            m->pPeer->m->bd.attach(m->bd);
        }
    }
}

void NvramStore::i_copyFrom(NvramStore *aThat)
{
    AssertReturnVoid(aThat != NULL);

    /* sanity */
    AutoCaller autoCaller(this);
    AssertReturnVoid(autoCaller.isOk());

    /* sanity too */
    AutoCaller thatCaller(aThat);
    AssertReturnVoid(thatCaller.isOk());

    /* peer is not modified, lock it for reading (aThat is "master" so locked
     * first) */
    AutoReadLock rl(aThat COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock wl(this COMMA_LOCKVAL_SRC_POS);

    /* this will back up current data */
    m->bd.assignCopy(aThat->m->bd);

    // Intentionally "forget" the NVRAM file since it must be unique and set
    // to the correct value before the copy of the settings makes sense.
    m->bd->strNvramPath.setNull();
}

HRESULT NvramStore::i_applyDefaults(GuestOSType *aOSType)
{
    HRESULT hrc = S_OK;

    if (aOSType->i_recommendedEFISecureBoot())
    {
        /* Initialize the UEFI variable store and enroll default keys. */
        hrc = initUefiVariableStore(0 /*aSize*/);
        if (SUCCEEDED(hrc))
        {
            ComPtr<IUefiVariableStore> pVarStore;

            hrc = getUefiVariableStore(pVarStore);
            if (SUCCEEDED(hrc))
            {
                hrc = pVarStore->EnrollOraclePlatformKey();
                if (SUCCEEDED(hrc))
                    hrc = pVarStore->EnrollDefaultMsSignatures();
            }
        }
    }

    return hrc;
}

void NvramStore::i_updateNonVolatileStorageFile(const Utf8Str &aNonVolatileStorageFile)
{
    /* sanity */
    AutoCaller autoCaller(this);
    AssertComRCReturnVoid(autoCaller.hrc());

    AutoReadLock mlock(m->pParent COMMA_LOCKVAL_SRC_POS);
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    Utf8Str strTmp(aNonVolatileStorageFile);
    if (strTmp == m->pParent->i_getDefaultNVRAMFilename())
        strTmp.setNull();

    if (strTmp == m->bd->strNvramPath)
        return;

    m->bd.backup();
    m->bd->strNvramPath = strTmp;
}

#else /* VBOX_COM_INPROC */

//
// private methods
//
/*static*/
DECLCALLBACK(int) NvramStore::i_nvramStoreQuerySize(PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath,
                                                    uint64_t *pcb)
{
    PDRVMAINNVRAMSTORE pThis = RT_FROM_MEMBER(pInterface, DRVMAINNVRAMSTORE, IVfs);

    Utf8Str strKey;
    int vrc = strKey.printfNoThrow("%s/%s", pszNamespace, pszPath);
    AssertRCReturn(vrc, vrc);

    AutoReadLock rlock(pThis->pNvramStore COMMA_LOCKVAL_SRC_POS);
    NvramStoreIter it = pThis->pNvramStore->m->mapNvram.find(strKey);
    if (it != pThis->pNvramStore->m->mapNvram.end())
    {
        RTVFSFILE hVfsFile = it->second;
        return RTVfsFileQuerySize(hVfsFile, pcb);
    }

    return VERR_NOT_FOUND;
}


/*static*/
DECLCALLBACK(int) NvramStore::i_nvramStoreReadAll(PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath,
                                                  void *pvBuf, size_t cbRead)
{
    PDRVMAINNVRAMSTORE pThis = RT_FROM_MEMBER(pInterface, DRVMAINNVRAMSTORE, IVfs);

    Utf8Str strKey;
    int vrc = strKey.printfNoThrow("%s/%s", pszNamespace, pszPath);
    AssertRCReturn(vrc, vrc);

    AutoReadLock rlock(pThis->pNvramStore COMMA_LOCKVAL_SRC_POS);
    NvramStoreIter it = pThis->pNvramStore->m->mapNvram.find(strKey);
    if (it != pThis->pNvramStore->m->mapNvram.end())
    {
        RTVFSFILE hVfsFile = it->second;

        vrc = RTVfsFileSeek(hVfsFile, 0 /*offSeek*/, RTFILE_SEEK_BEGIN, NULL /*poffActual*/);
        AssertLogRelRC(vrc);

        return RTVfsFileRead(hVfsFile, pvBuf, cbRead, NULL /*pcbRead*/);
    }

    return VERR_NOT_FOUND;
}


/*static*/
DECLCALLBACK(int) NvramStore::i_nvramStoreWriteAll(PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath,
                                                   const void *pvBuf, size_t cbWrite)
{
    PDRVMAINNVRAMSTORE pThis = RT_FROM_MEMBER(pInterface, DRVMAINNVRAMSTORE, IVfs);

    Utf8Str strKey;
    int vrc = strKey.printfNoThrow("%s/%s", pszNamespace, pszPath);
    AssertRCReturn(vrc, vrc);

    AutoWriteLock wlock(pThis->pNvramStore COMMA_LOCKVAL_SRC_POS);

    NvramStoreIter it = pThis->pNvramStore->m->mapNvram.find(strKey);
    if (it != pThis->pNvramStore->m->mapNvram.end())
    {
        RTVFSFILE hVfsFile = it->second;

        vrc = RTVfsFileSeek(hVfsFile, 0 /*offSeek*/, RTFILE_SEEK_BEGIN, NULL /*poffActual*/);
        AssertLogRelRC(vrc);
        vrc = RTVfsFileSetSize(hVfsFile, cbWrite, RTVFSFILE_SIZE_F_NORMAL);
        if (RT_SUCCESS(vrc))
            vrc = RTVfsFileWrite(hVfsFile, pvBuf, cbWrite, NULL /*pcbWritten*/);
    }
    else
    {
        /* Create a new entry. */
        RTVFSFILE hVfsFile = NIL_RTVFSFILE;
        vrc = RTVfsFileFromBuffer(RTFILE_O_READ | RTFILE_O_WRITE, pvBuf, cbWrite, &hVfsFile);
        if (RT_SUCCESS(vrc))
        {
            try
            {
                pThis->pNvramStore->m->mapNvram[strKey] = hVfsFile;
            }
            catch (...)
            {
                AssertLogRelFailed();
                RTVfsFileRelease(hVfsFile);
                vrc = VERR_UNEXPECTED_EXCEPTION;
            }
        }
    }

    return vrc;
}


/*static*/
DECLCALLBACK(int) NvramStore::i_nvramStoreDelete(PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath)
{
    PDRVMAINNVRAMSTORE pThis = RT_FROM_MEMBER(pInterface, DRVMAINNVRAMSTORE, IVfs);

    Utf8Str strKey;
    int vrc = strKey.printfNoThrow("%s/%s", pszNamespace, pszPath);
    AssertRCReturn(vrc, vrc);

    AutoWriteLock wlock(pThis->pNvramStore COMMA_LOCKVAL_SRC_POS);
    NvramStoreIter it = pThis->pNvramStore->m->mapNvram.find(strKey);
    if (it != pThis->pNvramStore->m->mapNvram.end())
    {
        RTVFSFILE hVfsFile = it->second;
        pThis->pNvramStore->m->mapNvram.erase(it);
        RTVfsFileRelease(hVfsFile);
        return VINF_SUCCESS;
    }

    return VERR_NOT_FOUND;
}


/*static*/
DECLCALLBACK(int) NvramStore::i_SsmSaveExec(PPDMDRVINS pDrvIns, PSSMHANDLE pSSM)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVMAINNVRAMSTORE pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINNVRAMSTORE);
    PCPDMDRVHLPR3      pHlp  = pDrvIns->pHlpR3;

    AutoWriteLock wlock(pThis->pNvramStore COMMA_LOCKVAL_SRC_POS);

    size_t cEntries = pThis->pNvramStore->m->mapNvram.size();
    AssertReturn(cEntries < 32, VERR_OUT_OF_RANGE); /* Some sanity checking. */
    pHlp->pfnSSMPutU32(pSSM, (uint32_t)cEntries);

    void *pvData = NULL;
    size_t cbDataMax = 0;
    int vrc = i_SsmSaveExecInner(pThis, pHlp, pSSM, &pvData, &cbDataMax);
    if (pvData)
        RTMemFree(pvData);
    AssertRCReturn(vrc, vrc);

    pThis->pNvramStore->m->fSsmSaved = true;
    return pHlp->pfnSSMPutU32(pSSM, UINT32_MAX); /* sanity/terminator */
}


/*static*/
int NvramStore::i_SsmSaveExecInner(PDRVMAINNVRAMSTORE pThis, PCPDMDRVHLPR3 pHlp, PSSMHANDLE pSSM,
                                   void **ppvData, size_t *pcbDataMax) RT_NOEXCEPT
{
    for (NvramStoreIter it = pThis->pNvramStore->m->mapNvram.begin(); it != pThis->pNvramStore->m->mapNvram.end(); ++it)
    {
        RTVFSFILE hVfsFile = it->second;

        uint64_t cbFile;
        int vrc = RTVfsFileQuerySize(hVfsFile, &cbFile);
        AssertRCReturn(vrc, vrc);
        AssertReturn(cbFile < _1M, VERR_OUT_OF_RANGE);

        if (*pcbDataMax < cbFile)
        {
            void *pvNew = RTMemRealloc(*ppvData, cbFile);
            AssertPtrReturn(pvNew, VERR_NO_MEMORY);
            *ppvData    = pvNew;
            *pcbDataMax = cbFile;
        }

        vrc = RTVfsFileReadAt(hVfsFile, 0 /*off*/, *ppvData, cbFile, NULL /*pcbRead*/);
        AssertRCReturn(vrc, vrc);

        pHlp->pfnSSMPutStrZ(pSSM, it->first.c_str());
        pHlp->pfnSSMPutU64(pSSM, cbFile);
        pHlp->pfnSSMPutMem(pSSM, *ppvData, cbFile);
    }
    return VINF_SUCCESS;
}


/*static*/
DECLCALLBACK(int) NvramStore::i_SsmLoadExec(PPDMDRVINS pDrvIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    PDRVMAINNVRAMSTORE pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINNVRAMSTORE);
    PCPDMDRVHLPR3      pHlp  = pDrvIns->pHlpR3;

    AssertMsgReturn(uVersion >= NVRAM_STORE_SAVED_STATE_VERSION, ("%d\n", uVersion),
                    VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION);

    if (uPass == SSM_PASS_FINAL)
    {
        AutoWriteLock wlock(pThis->pNvramStore COMMA_LOCKVAL_SRC_POS);

        /* Clear any content first. */
        NvramStoreIter it = pThis->pNvramStore->m->mapNvram.begin();
        while (it != pThis->pNvramStore->m->mapNvram.end())
        {
            RTVfsFileRelease(it->second);
            it++;
        }

        pThis->pNvramStore->m->mapNvram.clear();

        uint32_t cEntries = 0;
        int vrc = pHlp->pfnSSMGetU32(pSSM, &cEntries);
        AssertRCReturn(vrc, vrc);
        AssertReturn(cEntries < 32, VERR_OUT_OF_RANGE);

        void *pvData = NULL;
        size_t cbDataMax = 0;
        vrc = i_SsmLoadExecInner(pThis, pHlp, pSSM, cEntries, &pvData, &cbDataMax);
        if (pvData)
            RTMemFree(pvData);
        AssertRCReturn(vrc, vrc);

        /* The marker. */
        uint32_t u32;
        vrc = pHlp->pfnSSMGetU32(pSSM, &u32);
        AssertRCReturn(vrc, vrc);
        AssertMsgReturn(u32 == UINT32_MAX, ("%#x\n", u32), VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
    }

    return VINF_SUCCESS;
}


/*static*/
int NvramStore::i_SsmLoadExecInner(PDRVMAINNVRAMSTORE pThis, PCPDMDRVHLPR3 pHlp, PSSMHANDLE pSSM,
                                   uint32_t cEntries, void **ppvData, size_t *pcbDataMax) RT_NOEXCEPT
{
    while (cEntries-- > 0)
    {
        char szId[_1K]; /* Lazy developer */
        int vrc = pHlp->pfnSSMGetStrZ(pSSM, &szId[0], sizeof(szId));
        AssertRCReturn(vrc, vrc);

        uint64_t cbFile = 0;
        vrc = pHlp->pfnSSMGetU64(pSSM, &cbFile);
        AssertRCReturn(vrc, vrc);
        AssertReturn(cbFile < _1M, VERR_OUT_OF_RANGE);

        if (*pcbDataMax < cbFile)
        {
            void *pvNew = RTMemRealloc(*ppvData, cbFile);
            AssertPtrReturn(pvNew, VERR_NO_MEMORY);
            *ppvData    = pvNew;
            *pcbDataMax = cbFile;
        }

        vrc = pHlp->pfnSSMGetMem(pSSM, *ppvData, cbFile);
        AssertRCReturn(vrc, vrc);

        RTVFSFILE hVfsFile;
        vrc = RTVfsFileFromBuffer(RTFILE_O_READWRITE, *ppvData, cbFile, &hVfsFile);
        AssertRCReturn(vrc, vrc);

        try
        {
            pThis->pNvramStore->m->mapNvram[Utf8Str(szId)] = hVfsFile;
        }
        catch (...)
        {
            AssertLogRelFailed();
            RTVfsFileRelease(hVfsFile);
            return VERR_UNEXPECTED_EXCEPTION;
        }
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
DECLCALLBACK(void *) NvramStore::i_drvQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS          pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVMAINNVRAMSTORE  pDrv    = PDMINS_2_DATA(pDrvIns, PDRVMAINNVRAMSTORE);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIVFSCONNECTOR, &pDrv->IVfs);
    return NULL;
}


/**
 * Destruct a NVRAM store driver instance.
 *
 * @returns VBox status code.
 * @param   pDrvIns     The driver instance data.
 */
DECLCALLBACK(void) NvramStore::i_drvDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVMAINNVRAMSTORE pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINNVRAMSTORE);
    LogFlow(("NvramStore::drvDestruct: iInstance=%d\n", pDrvIns->iInstance));

    if (pThis->pNvramStore)
    {
        uint32_t cRefs = ASMAtomicDecU32(&pThis->pNvramStore->m->cRefs);
        if (   !cRefs
            && !pThis->pNvramStore->m->fSsmSaved)
        {
            try
            {
                int vrc = pThis->pNvramStore->i_saveStore();
                AssertLogRelRC(vrc); /** @todo Disk full error? */
            }
            catch (...)
            {
                AssertLogRelFailed();
            }
        }
    }
}


/**
 * Construct a NVRAM store driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
DECLCALLBACK(int) NvramStore::i_drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    RT_NOREF(fFlags, pCfg);
    PDRVMAINNVRAMSTORE pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINNVRAMSTORE);
    LogFlow(("NvramStore::drvConstruct: iInstance=%d\n", pDrvIns->iInstance));

    /*
     * Validate configuration.
     */
    PDMDRV_VALIDATE_CONFIG_RETURN(pDrvIns, "", "");
    AssertMsgReturn(PDMDrvHlpNoAttach(pDrvIns) == VERR_PDM_NO_ATTACHED_DRIVER,
                    ("Configuration error: Not possible to attach anything to this driver!\n"),
                    VERR_PDM_DRVINS_NO_ATTACH);

    /*
     * IBase.
     */
    pDrvIns->IBase.pfnQueryInterface    = NvramStore::i_drvQueryInterface;

    pThis->IVfs.pfnQuerySize            = NvramStore::i_nvramStoreQuerySize;
    pThis->IVfs.pfnReadAll              = NvramStore::i_nvramStoreReadAll;
    pThis->IVfs.pfnWriteAll             = NvramStore::i_nvramStoreWriteAll;
    pThis->IVfs.pfnDelete               = NvramStore::i_nvramStoreDelete;

    /*
     * Get the NVRAM store object pointer.
     */
    com::Guid uuid(COM_IIDOF(INvramStore));
    pThis->pNvramStore = (NvramStore *)PDMDrvHlpQueryGenericUserObject(pDrvIns, uuid.raw());
    if (!pThis->pNvramStore)
    {
        AssertMsgFailed(("Configuration error: No/bad NVRAM store object!\n"));
        return VERR_NOT_FOUND;
    }

    /*
     * Only the first instance will register the SSM handlers and will do the work on behalf
     * of all other NVRAM store driver instances when it comes to SSM.
     */
    if (pDrvIns->iInstance == 0)
    {
        int vrc = PDMDrvHlpSSMRegister(pDrvIns, NVRAM_STORE_SAVED_STATE_VERSION, 0 /*cbGuess*/,
                                       NvramStore::i_SsmSaveExec, NvramStore::i_SsmLoadExec);
        if (RT_FAILURE(vrc))
            return PDMDrvHlpVMSetError(pDrvIns, vrc, RT_SRC_POS,
                                       N_("Failed to register the saved state unit for the NVRAM store"));
    }

    uint32_t cRefs = ASMAtomicIncU32(&pThis->pNvramStore->m->cRefs);
    if (cRefs == 1)
    {
        int vrc;
        try
        {
            vrc = pThis->pNvramStore->i_loadStore(pThis->pNvramStore->m->bd->strNvramPath.c_str());
        }
        catch (...)
        {
            vrc = VERR_UNEXPECTED_EXCEPTION;
        }
        if (RT_FAILURE(vrc))
        {
            ASMAtomicDecU32(&pThis->pNvramStore->m->cRefs);
            return PDMDrvHlpVMSetError(pDrvIns, vrc, RT_SRC_POS,
                                       N_("Failed to load the NVRAM store from the file"));
        }
    }

    return VINF_SUCCESS;
}


/**
 * NVRAM store driver registration record.
 */
const PDMDRVREG NvramStore::DrvReg =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "NvramStore",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Main NVRAM store driver (Main as in the API).",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_STATUS,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVMAINNVRAMSTORE),
    /* pfnConstruct */
    NvramStore::i_drvConstruct,
    /* pfnDestruct */
    NvramStore::i_drvDestruct,
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

#endif /* VBOX_COM_INPROC */

/* vi: set tabstop=4 shiftwidth=4 expandtab: */
