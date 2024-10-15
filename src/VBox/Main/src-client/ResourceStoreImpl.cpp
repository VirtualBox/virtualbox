/* $Id: ResourceStoreImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM resource store class implementation
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#define LOG_GROUP LOG_GROUP_MAIN_RESOURCESTORE
#include "LoggingNew.h"

#include "ResourceStoreImpl.h"
#include "ConsoleImpl.h"
#include "VirtualBoxImpl.h"

#include "AutoCaller.h"

#include <VBox/com/array.h>
#include <VBox/vmm/pdmdrv.h>
#include <VBox/err.h>

#include <iprt/cpp/utils.h>
#include <iprt/file.h>
#include <iprt/vfs.h>


// defines
////////////////////////////////////////////////////////////////////////////////


// globals
////////////////////////////////////////////////////////////////////////////////

/**
 * Resource store driver instance data.
 */
typedef struct DRVMAINRESOURCESTORE
{
    /** Pointer to the keyboard object. */
    ResourceStore               *pResourceStore;
    /** Pointer to the driver instance structure. */
    PPDMDRVINS                  pDrvIns;
    /** Our VFS connector interface. */
    PDMIVFSCONNECTOR            IVfs;
} DRVMAINRESOURCESTORE, *PDRVMAINRESOURCESTORE;

/** The resource store map keyed by namespace/entity. */
typedef std::map<Utf8Str, RTVFSFILE> ResourceStoreMap;
/** The resource store map iterator. */
typedef std::map<Utf8Str, RTVFSFILE>::iterator ResourceStoreIter;

struct BackupableResourceStoreData
{
    BackupableResourceStoreData()
    { }

    /** The resource store. */
    ResourceStoreMap            mapResources;
};

/////////////////////////////////////////////////////////////////////////////
// ResourceStore::Data structure
/////////////////////////////////////////////////////////////////////////////

struct ResourceStore::Data
{
    Data()
        : pParent(NULL)
    { }

    /** The Console owning this resource store. */
    Console * const         pParent;
    /** Number of references held to this resource store from the various devices/drivers. */
    volatile uint32_t       cRefs;

    Backupable<BackupableResourceStoreData> bd;
};

// constructor / destructor
////////////////////////////////////////////////////////////////////////////////

DEFINE_EMPTY_CTOR_DTOR(ResourceStore)

HRESULT ResourceStore::FinalConstruct()
{
    return BaseFinalConstruct();
}

void ResourceStore::FinalRelease()
{
    uninit();
    BaseFinalRelease();
}


// public initializer/uninitializer for internal purposes only
/////////////////////////////////////////////////////////////////////////////

/**
 * Initializes the resource store object.
 *
 * @returns COM result indicator
 * @param aParent                       Handle of our parent object
 */
HRESULT ResourceStore::init(Console *aParent)
{
    LogFlowThisFunc(("aParent=%p\n", aParent));

    ComAssertRet(aParent, E_INVALIDARG);

    /* Enclose the state transition NotReady->InInit->Ready */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    m = new Data();

    unconst(m->pParent) = aParent;

    m->bd.allocate();

    /* Confirm a successful initialization */
    autoInitSpan.setSucceeded();

    return S_OK;
}


/**
 *  Uninitializes the instance and sets the ready flag to FALSE.
 *  Called either from FinalRelease() or by the parent when it gets destroyed.
 */
void ResourceStore::uninit()
{
    LogFlowThisFuncEnter();

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (autoUninitSpan.uninitDone())
        return;

    unconst(m->pParent) = NULL;

    /* Delete the store content. */
    ResourceStoreIter it = m->bd->mapResources.begin();
    while (it != m->bd->mapResources.end())
    {
        RTVfsFileRelease(it->second);
        it++;
    }

    m->bd->mapResources.clear();
    m->bd.free();

    delete m;
    m = NULL;

    LogFlowThisFuncLeave();
}


/**
 * Adds the given item to the store under the namespace and path.
 *
 * @returns VBox status code.
 * @param   pszNamespace        The namespace of the item.
 * @param   pszPath             The path to the item.
 * @param   hVfsFile            The item data as a VFS file handle, a reference is retained on success.
 */
int ResourceStore::i_addItem(const char *pszNamespace, const char *pszPath, RTVFSFILE hVfsFile)
{
    Utf8Str strKey;
    int vrc = strKey.printfNoThrow("%s/%s", pszNamespace, pszPath);
    AssertRCReturn(vrc, vrc);

    AutoWriteLock wlock(this COMMA_LOCKVAL_SRC_POS);
    try
    {
        RTVfsFileRetain(hVfsFile);
        m->bd->mapResources[strKey] = hVfsFile;
    }
    catch (...)
    {
        AssertLogRelFailed();
        vrc = VERR_UNEXPECTED_EXCEPTION;
    }

    return vrc;
}


//
// private methods
//
/*static*/
DECLCALLBACK(int) ResourceStore::i_resourceStoreQuerySize(PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath,
                                                          uint64_t *pcb)
{
    PDRVMAINRESOURCESTORE pThis = RT_FROM_MEMBER(pInterface, DRVMAINRESOURCESTORE, IVfs);

    Utf8Str strKey;
    int vrc = strKey.printfNoThrow("%s/%s", pszNamespace, pszPath);
    AssertRCReturn(vrc, vrc);

    AutoReadLock rlock(pThis->pResourceStore COMMA_LOCKVAL_SRC_POS);
    ResourceStoreIter it = pThis->pResourceStore->m->bd->mapResources.find(strKey);
    if (it != pThis->pResourceStore->m->bd->mapResources.end())
    {
        RTVFSFILE hVfsFile = it->second;
        return RTVfsFileQuerySize(hVfsFile, pcb);
    }

    return VERR_NOT_FOUND;
}


/*static*/
DECLCALLBACK(int) ResourceStore::i_resourceStoreReadAll(PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath,
                                                        void *pvBuf, size_t cbRead)
{
    PDRVMAINRESOURCESTORE pThis = RT_FROM_MEMBER(pInterface, DRVMAINRESOURCESTORE, IVfs);

    Utf8Str strKey;
    int vrc = strKey.printfNoThrow("%s/%s", pszNamespace, pszPath);
    AssertRCReturn(vrc, vrc);

    AutoReadLock rlock(pThis->pResourceStore COMMA_LOCKVAL_SRC_POS);
    ResourceStoreIter it = pThis->pResourceStore->m->bd->mapResources.find(strKey);
    if (it != pThis->pResourceStore->m->bd->mapResources.end())
    {
        RTVFSFILE hVfsFile = it->second;

        vrc = RTVfsFileSeek(hVfsFile, 0 /*offSeek*/, RTFILE_SEEK_BEGIN, NULL /*poffActual*/);
        AssertLogRelRC(vrc);

        return RTVfsFileRead(hVfsFile, pvBuf, cbRead, NULL /*pcbRead*/);
    }

    return VERR_NOT_FOUND;
}


/*static*/
DECLCALLBACK(int) ResourceStore::i_resourceStoreWriteAll(PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath,
                                                         const void *pvBuf, size_t cbWrite)
{
    PDRVMAINRESOURCESTORE pThis = RT_FROM_MEMBER(pInterface, DRVMAINRESOURCESTORE, IVfs);

    Utf8Str strKey;
    int vrc = strKey.printfNoThrow("%s/%s", pszNamespace, pszPath);
    AssertRCReturn(vrc, vrc);

    AutoWriteLock wlock(pThis->pResourceStore COMMA_LOCKVAL_SRC_POS);

    ResourceStoreIter it = pThis->pResourceStore->m->bd->mapResources.find(strKey);
    if (it != pThis->pResourceStore->m->bd->mapResources.end())
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
                pThis->pResourceStore->m->bd->mapResources[strKey] = hVfsFile;
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
DECLCALLBACK(int) ResourceStore::i_resourceStoreDelete(PPDMIVFSCONNECTOR pInterface, const char *pszNamespace, const char *pszPath)
{
    PDRVMAINRESOURCESTORE pThis = RT_FROM_MEMBER(pInterface, DRVMAINRESOURCESTORE, IVfs);

    Utf8Str strKey;
    int vrc = strKey.printfNoThrow("%s/%s", pszNamespace, pszPath);
    AssertRCReturn(vrc, vrc);

    AutoWriteLock wlock(pThis->pResourceStore COMMA_LOCKVAL_SRC_POS);
    ResourceStoreIter it = pThis->pResourceStore->m->bd->mapResources.find(strKey);
    if (it != pThis->pResourceStore->m->bd->mapResources.end())
    {
        RTVFSFILE hVfsFile = it->second;
        pThis->pResourceStore->m->bd->mapResources.erase(it);
        RTVfsFileRelease(hVfsFile);
        return VINF_SUCCESS;
    }

    return VERR_NOT_FOUND;
}


/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
DECLCALLBACK(void *) ResourceStore::i_drvQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PPDMDRVINS              pDrvIns = PDMIBASE_2_PDMDRV(pInterface);
    PDRVMAINRESOURCESTORE   pDrv    = PDMINS_2_DATA(pDrvIns, PDRVMAINRESOURCESTORE);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pDrvIns->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIVFSCONNECTOR, &pDrv->IVfs);
    return NULL;
}


/**
 * Destruct a resource store driver instance.
 *
 * @param   pDrvIns     The driver instance data.
 */
DECLCALLBACK(void) ResourceStore::i_drvDestruct(PPDMDRVINS pDrvIns)
{
    PDMDRV_CHECK_VERSIONS_RETURN_VOID(pDrvIns);
    PDRVMAINRESOURCESTORE pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINRESOURCESTORE);
    LogFlow(("ResourceStore::drvDestruct: iInstance=%d\n", pDrvIns->iInstance));

    if (pThis->pResourceStore)
    {
        ASMAtomicDecU32(&pThis->pResourceStore->m->cRefs);
        pThis->pResourceStore = NULL;
    }
}


/**
 * Construct a resource store driver instance.
 *
 * @copydoc FNPDMDRVCONSTRUCT
 */
DECLCALLBACK(int) ResourceStore::i_drvConstruct(PPDMDRVINS pDrvIns, PCFGMNODE pCfg, uint32_t fFlags)
{
    PDMDRV_CHECK_VERSIONS_RETURN(pDrvIns);
    RT_NOREF(fFlags, pCfg);
    PDRVMAINRESOURCESTORE pThis = PDMINS_2_DATA(pDrvIns, PDRVMAINRESOURCESTORE);
    LogFlow(("ResourceStore::drvConstruct: iInstance=%d\n", pDrvIns->iInstance));

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
    pDrvIns->IBase.pfnQueryInterface    = ResourceStore::i_drvQueryInterface;

    pThis->IVfs.pfnQuerySize            = ResourceStore::i_resourceStoreQuerySize;
    pThis->IVfs.pfnReadAll              = ResourceStore::i_resourceStoreReadAll;
    pThis->IVfs.pfnWriteAll             = ResourceStore::i_resourceStoreWriteAll;
    pThis->IVfs.pfnDelete               = ResourceStore::i_resourceStoreDelete;

    /*
     * Get the resource store object pointer.
     */
    com::Guid uuid(COM_IIDOF(IResourceStore));
    pThis->pResourceStore = (ResourceStore *)PDMDrvHlpQueryGenericUserObject(pDrvIns, uuid.raw());
    if (!pThis->pResourceStore)
    {
        AssertMsgFailed(("Configuration error: No/bad resource store object!\n"));
        return VERR_NOT_FOUND;
    }

    ASMAtomicIncU32(&pThis->pResourceStore->m->cRefs);
    return VINF_SUCCESS;
}


/**
 * Resource store driver registration record.
 */
const PDMDRVREG ResourceStore::DrvReg =
{
    /* u32Version */
    PDM_DRVREG_VERSION,
    /* szName */
    "ResourceStore",
    /* szRCMod */
    "",
    /* szR0Mod */
    "",
    /* pszDescription */
    "Main resource store driver (Main as in the API).",
    /* fFlags */
    PDM_DRVREG_FLAGS_HOST_BITS_DEFAULT,
    /* fClass. */
    PDM_DRVREG_CLASS_STATUS,
    /* cMaxInstances */
    ~0U,
    /* cbInstance */
    sizeof(DRVMAINRESOURCESTORE),
    /* pfnConstruct */
    ResourceStore::i_drvConstruct,
    /* pfnDestruct */
    ResourceStore::i_drvDestruct,
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
