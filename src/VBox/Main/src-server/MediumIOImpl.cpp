/* $Id: MediumIOImpl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox COM class implementation: MediumIO
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_MAIN_MEDIUMIO
#include "MediumIOImpl.h"
#include "MediumImpl.h"
#include "MediumLock.h"
#include "DataStreamImpl.h"
#include "Global.h"
#include "ProgressImpl.h"
#include "VirtualBoxImpl.h"

#include "AutoCaller.h"
#include "LoggingNew.h"
#include "ThreadTask.h"

#include <iprt/fsvfs.h>
#include <iprt/dvm.h>
#include <iprt/zero.h>
#include <iprt/cpp/utils.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Private member data.
 */
struct MediumIO::Data
{
    Data(Medium * const a_pMedium, VirtualBox * const a_pVirtualBox, bool a_fWritable, uint32_t a_cbSector = 512)
        : ptrMedium(a_pMedium)
        , ptrVirtualBox(a_pVirtualBox)
        , fWritable(a_fWritable)
        , cbSector(a_cbSector)
        , PasswordStore(false /*fKeyBufNonPageable*/)
        , pHdd(NULL)
        , hVfsFile(NIL_RTVFSFILE)
    {
    }

    /** Reference to the medium we're accessing. */
    ComPtr<Medium>                  ptrMedium;
    /** Reference to the VirtualBox object the medium is part of. */
    ComPtr<VirtualBox>              ptrVirtualBox;
    /** Set if writable, clear if readonly. */
    bool                            fWritable;
    /** The sector size. */
    uint32_t                        cbSector;
    /** Secret key store used to hold the passwords for encrypted medium. */
    SecretKeyStore                  PasswordStore;
    /** Crypto filter settings. */
    MediumCryptoFilterSettings      CryptoSettings;
    /** Medium lock list.  */
    MediumLockList                  LockList;
    /** The HDD instance. */
    PVDISK                          pHdd;
    /** VFS file for the HDD instance. */
    RTVFSFILE                       hVfsFile;

private:
    Data() : PasswordStore(false) { }
};


/**
 * MediumIO::StreamTask class for asynchronous convert to stream operation.
 *
 * @note Instances of this class must be created using new() because the
 *       task thread function will delete them when the task is complete.
 *
 * @note The constructor of this class adds a caller on the managed Medium
 *       object which is automatically released upon destruction.
 */
class MediumIO::StreamTask : public ThreadTask
{
public:
    StreamTask(MediumIO *pMediumIO, DataStream *pDataStream, Progress *pProgress, const char *pszFormat,
               MediumVariant_T fMediumVariant)
        : ThreadTask("StreamTask"),
          mMediumIO(pMediumIO),
          mMediumCaller(pMediumIO->m->ptrMedium),
          m_pDataStream(pDataStream),
          m_fMediumVariant(fMediumVariant),
          m_strFormat(pszFormat),
          mProgress(pProgress),
          mVirtualBoxCaller(NULL)
    {
        AssertReturnVoidStmt(pMediumIO, mHrc = E_FAIL);
        AssertReturnVoidStmt(pDataStream, mHrc = E_FAIL);
        mHrc = mMediumCaller.hrc();
        if (FAILED(mHrc))
            return;

        /* Get strong VirtualBox reference, see below. */
        VirtualBox *pVirtualBox = pMediumIO->m->ptrVirtualBox;
        mVirtualBox = pVirtualBox;
        mVirtualBoxCaller.attach(pVirtualBox);
        mHrc = mVirtualBoxCaller.hrc();
        if (FAILED(mHrc))
            return;
    }

    // Make all destructors virtual. Just in case.
    virtual ~StreamTask()
    {
        /* send the notification of completion.*/
        if (   isAsync()
            && !mProgress.isNull())
            mProgress->i_notifyComplete(mHrc);
    }

    HRESULT hrc() const { return mHrc; }
    bool isOk() const { return SUCCEEDED(hrc()); }

    const ComPtr<Progress>& GetProgressObject() const {return mProgress;}

    /**
     * Implementation code for the "create base" task.
     * Used as function for execution from a standalone thread.
     */
    void handler()
    {
        LogFlowFuncEnter();
        try
        {
            mHrc = executeTask(); /* (destructor picks up mHrc, see above) */
            LogFlowFunc(("hrc=%Rhrc\n", mHrc));
        }
        catch (...)
        {
            LogRel(("Some exception in the function MediumIO::StreamTask:handler()\n"));
        }

        LogFlowFuncLeave();
    }

    const ComObjPtr<MediumIO> mMediumIO;
    AutoCaller mMediumCaller;

protected:
    HRESULT         mHrc;

    ComObjPtr<DataStream> m_pDataStream;
    MediumVariant_T m_fMediumVariant;
    Utf8Str         m_strFormat;

private:
    HRESULT executeTask();

    const ComObjPtr<Progress> mProgress;

    /* Must have a strong VirtualBox reference during a task otherwise the
     * reference count might drop to 0 while a task is still running. This
     * would result in weird behavior, including deadlocks due to uninit and
     * locking order issues. The deadlock often is not detectable because the
     * uninit uses event semaphores which sabotages deadlock detection. */
    ComObjPtr<VirtualBox> mVirtualBox;
    AutoCaller mVirtualBoxCaller;

    static DECLCALLBACK(int) i_vdStreamOpen(void *pvUser, const char *pszLocation, uint32_t fOpen,
                                            PFNVDCOMPLETED pfnCompleted, void **ppStorage);
    static DECLCALLBACK(int) i_vdStreamClose(void *pvUser, void *pStorage);
    static DECLCALLBACK(int) i_vdStreamDelete(void *pvUser, const char *pcszFilename);
    static DECLCALLBACK(int) i_vdStreamMove(void *pvUser, const char *pcszSrc, const char *pcszDst, unsigned fMove);
    static DECLCALLBACK(int) i_vdStreamGetFreeSpace(void *pvUser, const char *pcszFilename, int64_t *pcbFreeSpace);
    static DECLCALLBACK(int) i_vdStreamGetModificationTime(void *pvUser, const char *pcszFilename, PRTTIMESPEC pModificationTime);
    static DECLCALLBACK(int) i_vdStreamGetSize(void *pvUser, void *pStorage, uint64_t *pcbSize);
    static DECLCALLBACK(int) i_vdStreamSetSize(void *pvUser, void *pStorage, uint64_t cbSize);
    static DECLCALLBACK(int) i_vdStreamRead(void *pvUser, void *pStorage, uint64_t uOffset, void *pvBuffer, size_t cbBuffer,
                                            size_t *pcbRead);
    static DECLCALLBACK(int) i_vdStreamWrite(void *pvUser, void *pStorage, uint64_t uOffset,
                                             const void *pvBuffer, size_t cbBuffer, size_t *pcbWritten);
    static DECLCALLBACK(int) i_vdStreamFlush(void *pvUser, void *pStorage);
};


/**
 * State of a streamed file.
 */
typedef struct STREAMFILE
{
    /** The data stream for this file state. */
    DataStream              *pDataStream;
    /** The last seen offset used to stream zeroes for non consecutive writes. */
    uint64_t                uOffsetLast;
    /** Set file size. */
    uint64_t                cbFile;
} STREAMFILE;
/** Pointer to the stream file state. */
typedef STREAMFILE *PSTREAMFILE;



DECLCALLBACK(int) MediumIO::StreamTask::i_vdStreamOpen(void *pvUser, const char *pszLocation, uint32_t fOpen, PFNVDCOMPLETED pfnCompleted,
                                                       void **ppStorage)
{
    RT_NOREF2(pvUser, pszLocation);

    /* Validate input. */
    AssertPtrReturn(ppStorage, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pfnCompleted, VERR_INVALID_PARAMETER);
    AssertReturn((fOpen & RTFILE_O_ACCESS_MASK) == RTFILE_O_WRITE, VERR_INVALID_PARAMETER);

    int vrc = VINF_SUCCESS;
    PSTREAMFILE pStreamFile = (PSTREAMFILE)RTMemAllocZ(sizeof(*pStreamFile));
    if (RT_LIKELY(pStreamFile))
    {
        pStreamFile->pDataStream = (DataStream *)pvUser;
        pStreamFile->uOffsetLast = 0;
        pStreamFile->cbFile      = 0;
        *ppStorage = pStreamFile;
    }
    else
        vrc = VERR_NO_MEMORY;

    return vrc;
}

DECLCALLBACK(int) MediumIO::StreamTask::i_vdStreamClose(void *pvUser, void *pStorage)
{
    RT_NOREF(pvUser);
    PSTREAMFILE pStreamFile = (PSTREAMFILE)pStorage;
    int vrc = VINF_SUCCESS;

    /* Fill up to the configured file size. */
    if (pStreamFile->uOffsetLast < pStreamFile->cbFile)
    {
        do
        {
            size_t cbThisWrite = sizeof(g_abRTZero64K);
            size_t cbWritten = 0;

            if (pStreamFile->cbFile - pStreamFile->uOffsetLast < sizeof(g_abRTZero64K))
                cbThisWrite = (size_t)(pStreamFile->cbFile - pStreamFile->uOffsetLast);

            vrc = pStreamFile->pDataStream->i_write(&g_abRTZero64K[0], cbThisWrite, &cbWritten);
            if (RT_SUCCESS(vrc))
                pStreamFile->uOffsetLast += cbWritten;

        } while (   RT_SUCCESS(vrc)
                 && pStreamFile->uOffsetLast < pStreamFile->cbFile);
    }

    int vrc2 = pStreamFile->pDataStream->i_close();
    if (RT_SUCCESS(vrc))
        vrc = vrc2;

    RTMemFree(pStreamFile);
    return vrc;
}

DECLCALLBACK(int) MediumIO::StreamTask::i_vdStreamDelete(void *pvUser, const char *pcszFilename)
{
    NOREF(pvUser);
    NOREF(pcszFilename);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

DECLCALLBACK(int) MediumIO::StreamTask::i_vdStreamMove(void *pvUser, const char *pcszSrc, const char *pcszDst, unsigned fMove)
{
    NOREF(pvUser);
    NOREF(pcszSrc);
    NOREF(pcszDst);
    NOREF(fMove);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

DECLCALLBACK(int) MediumIO::StreamTask::i_vdStreamGetFreeSpace(void *pvUser, const char *pcszFilename, int64_t *pcbFreeSpace)
{
    NOREF(pvUser);
    NOREF(pcszFilename);
    AssertPtrReturn(pcbFreeSpace, VERR_INVALID_POINTER);
    *pcbFreeSpace = INT64_MAX;
    return VINF_SUCCESS;
}

DECLCALLBACK(int) MediumIO::StreamTask::i_vdStreamGetModificationTime(void *pvUser, const char *pcszFilename, PRTTIMESPEC pModificationTime)
{
    NOREF(pvUser);
    NOREF(pcszFilename);
    AssertPtrReturn(pModificationTime, VERR_INVALID_POINTER);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

DECLCALLBACK(int) MediumIO::StreamTask::i_vdStreamGetSize(void *pvUser, void *pStorage, uint64_t *pcbSize)
{
    NOREF(pvUser);
    PSTREAMFILE pStreamFile = (PSTREAMFILE)pStorage;
    AssertPtrReturn(pcbSize, VERR_INVALID_POINTER);

    *pcbSize = pStreamFile->cbFile;
    return VINF_SUCCESS;
}

DECLCALLBACK(int) MediumIO::StreamTask::i_vdStreamSetSize(void *pvUser, void *pStorage, uint64_t cbSize)
{
    RT_NOREF(pvUser);
    PSTREAMFILE pStreamFile = (PSTREAMFILE)pStorage;

    /* Reducing the size is not supported. */
    int vrc = VINF_SUCCESS;
    if (pStreamFile->cbFile < cbSize)
        pStreamFile->cbFile = cbSize;
    else
        vrc = VERR_NOT_SUPPORTED;

    return vrc;
}

DECLCALLBACK(int) MediumIO::StreamTask::i_vdStreamRead(void *pvUser, void *pStorage, uint64_t uOffset, void *pvBuffer, size_t cbBuffer,
                                                       size_t *pcbRead)
{
    NOREF(pvUser);
    NOREF(pStorage);
    NOREF(uOffset);
    NOREF(cbBuffer);
    NOREF(pcbRead);
    AssertPtrReturn(pvBuffer, VERR_INVALID_POINTER);
    AssertFailedReturn(VERR_NOT_SUPPORTED);
}

DECLCALLBACK(int) MediumIO::StreamTask::i_vdStreamWrite(void *pvUser, void *pStorage, uint64_t uOffset, const void *pvBuffer, size_t cbBuffer,
                                                        size_t *pcbWritten)
{
    RT_NOREF(pvUser);
    PSTREAMFILE pStreamFile = (PSTREAMFILE)pStorage;
    int vrc = VINF_SUCCESS;

    /* Fill up to the new offset if there is non consecutive access. */
    if (pStreamFile->uOffsetLast < uOffset)
    {
        do
        {
            size_t cbThisWrite = sizeof(g_abRTZero64K);
            size_t cbWritten = 0;

            if (uOffset - pStreamFile->uOffsetLast < sizeof(g_abRTZero64K))
                cbThisWrite = (size_t)(uOffset - pStreamFile->uOffsetLast);

            vrc = pStreamFile->pDataStream->i_write(&g_abRTZero64K[0], cbThisWrite, &cbWritten);
            if (RT_SUCCESS(vrc))
                pStreamFile->uOffsetLast += cbWritten;

        } while (   RT_SUCCESS(vrc)
                 && pStreamFile->uOffsetLast < uOffset);
    }

    if (RT_SUCCESS(vrc))
    {
        if (pcbWritten)
            vrc = pStreamFile->pDataStream->i_write(pvBuffer, cbBuffer, pcbWritten);
        else
        {
            const uint8_t *pbBuf = (const uint8_t *)pvBuffer;
            size_t cbLeft = cbBuffer;
            size_t cbWritten = 0;
            while (   cbLeft > 0
                   && RT_SUCCESS(vrc))
            {
                vrc = pStreamFile->pDataStream->i_write(pbBuf, cbLeft, &cbWritten);
                if (RT_SUCCESS(vrc))
                {
                    pbBuf  += cbWritten;
                    cbLeft -= cbWritten;
                }
            }
        }

        if (RT_SUCCESS(vrc))
        {
            size_t cbWritten = pcbWritten ? *pcbWritten : cbBuffer;

            /* Adjust file size. */
            if (uOffset + cbWritten > pStreamFile->cbFile)
                pStreamFile->cbFile = uOffset + cbWritten;

            pStreamFile->uOffsetLast = uOffset + cbWritten;
        }
    }

    return vrc;
}

DECLCALLBACK(int) MediumIO::StreamTask::i_vdStreamFlush(void *pvUser, void *pStorage)
{
    NOREF(pvUser);
    NOREF(pStorage);
    return VINF_SUCCESS;
}

/**
 * Implementation code for the "stream" task.
 */
HRESULT MediumIO::StreamTask::executeTask()
{
    HRESULT hrc = S_OK;
    VDINTERFACEIO IfsOutputIO;
    VDINTERFACEPROGRESS IfsProgress;
    PVDINTERFACE pIfsOp = NULL;
    PVDINTERFACE pIfsImg = NULL;
    PVDISK pDstDisk;

    if (mProgress)
    {
        IfsProgress.pfnProgress = mProgress->i_vdProgressCallback;
        VDInterfaceAdd(&IfsProgress.Core,
                       "Medium::StreamTask::vdInterfaceProgress",
                       VDINTERFACETYPE_PROGRESS,
                       mProgress,
                       sizeof(IfsProgress),
                       &pIfsOp);
    }

    IfsOutputIO.pfnOpen                   = i_vdStreamOpen;
    IfsOutputIO.pfnClose                  = i_vdStreamClose;
    IfsOutputIO.pfnDelete                 = i_vdStreamDelete;
    IfsOutputIO.pfnMove                   = i_vdStreamMove;
    IfsOutputIO.pfnGetFreeSpace           = i_vdStreamGetFreeSpace;
    IfsOutputIO.pfnGetModificationTime    = i_vdStreamGetModificationTime;
    IfsOutputIO.pfnGetSize                = i_vdStreamGetSize;
    IfsOutputIO.pfnSetSize                = i_vdStreamSetSize;
    IfsOutputIO.pfnReadSync               = i_vdStreamRead;
    IfsOutputIO.pfnWriteSync              = i_vdStreamWrite;
    IfsOutputIO.pfnFlushSync              = i_vdStreamFlush;
    VDInterfaceAdd(&IfsOutputIO.Core, "stream", VDINTERFACETYPE_IO,
                   m_pDataStream, sizeof(VDINTERFACEIO), &pIfsImg);

    int vrc = VDCreate(NULL, VDTYPE_HDD, &pDstDisk);
    if (RT_SUCCESS(vrc))
    {
        /* Create the output image */
        vrc = VDCopy(mMediumIO->m->pHdd, VD_LAST_IMAGE, pDstDisk, m_strFormat.c_str(),
                     "stream", false, 0, m_fMediumVariant, NULL,
                     VD_OPEN_FLAGS_NORMAL | VD_OPEN_FLAGS_SEQUENTIAL, pIfsOp,
                     pIfsImg, NULL);
        if (RT_FAILURE(vrc))
            hrc = mMediumIO->setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                          tr("Failed to convert and stream disk image"));

        VDDestroy(pDstDisk);
    }
    else
        hrc = mMediumIO->setErrorBoth(VBOX_E_FILE_ERROR, vrc,
                                      tr("Failed to create destination disk container"));

    return hrc;
}


/*********************************************************************************************************************************
*   Boilerplate constructor & destructor                                                                                         *
*********************************************************************************************************************************/

DEFINE_EMPTY_CTOR_DTOR(MediumIO)

HRESULT MediumIO::FinalConstruct()
{
    LogFlowThisFunc(("\n"));
    return BaseFinalConstruct();
}

void MediumIO::FinalRelease()
{
    LogFlowThisFuncEnter();
    uninit();
    BaseFinalRelease();
    LogFlowThisFuncLeave();
}


/*********************************************************************************************************************************
*   Initializer & uninitializer                                                                                                  *
*********************************************************************************************************************************/

/**
 * Initializes the medium I/O object.
 *
 * @param   pMedium         Pointer to the medium to access.
 * @param   pVirtualBox     Pointer to the VirtualBox object the medium is part of.
 * @param   fWritable       Read-write (true) or readonly (false) access.
 * @param   rStrKeyId       The key ID for an encrypted medium.  Empty if not
 *                          encrypted.
 * @param   rStrPassword    The password for an encrypted medium.  Empty if not
 *                          encrypted.
 *
 */
HRESULT MediumIO::initForMedium(Medium *pMedium, VirtualBox *pVirtualBox, bool fWritable,
                                com::Utf8Str const &rStrKeyId, com::Utf8Str const &rStrPassword)
{
    LogFlowThisFunc(("pMedium=%p pVirtualBox=%p fWritable=%RTbool\n", pMedium, pVirtualBox, fWritable));
    CheckComArgExpr(rStrPassword, rStrPassword.isEmpty() == rStrKeyId.isEmpty()); /* checked by caller */

    /*
     * Enclose the state transition NotReady->InInit->Ready
     */
    AutoInitSpan autoInitSpan(this);
    AssertReturn(autoInitSpan.isOk(), E_FAIL);

    /*
     * Allocate data instance.
     */
    HRESULT hrc = S_OK;
    m = new(std::nothrow) Data(pMedium, pVirtualBox, fWritable);
    if (m)
    {
        /*
         * Add the password to the keystore if specified.
         */
        if (rStrKeyId.isNotEmpty())
        {
            int vrc = m->PasswordStore.addSecretKey(rStrKeyId, (const uint8_t *)rStrPassword.c_str(),
                                                    rStrPassword.length() + 1 /*including the Schwarzenegger character*/);
            if (vrc == VERR_NO_MEMORY)
                hrc = setError(E_OUTOFMEMORY, tr("Failed to allocate enough secure memory for the key/password"));
            else if (RT_FAILURE(vrc))
                hrc = setErrorBoth(E_FAIL, vrc, tr("Unknown error happened while adding a password (%Rrc)"), vrc);
        }

        /*
         * Try open the medium and then get a VFS file handle for it.
         */
        if (SUCCEEDED(hrc))
        {
            hrc = pMedium->i_openForIO(fWritable, &m->PasswordStore, &m->pHdd, &m->LockList, &m->CryptoSettings);
            if (SUCCEEDED(hrc))
            {
                int vrc = VDCreateVfsFileFromDisk(m->pHdd, 0 /*fFlags*/, &m->hVfsFile);
                if (RT_FAILURE(vrc))
                {
                    hrc = setErrorBoth(E_FAIL, vrc, tr("VDCreateVfsFileFromDisk failed: %Rrc"), vrc);
                    m->hVfsFile = NIL_RTVFSFILE;
                }
            }
        }
    }
    else
        hrc = E_OUTOFMEMORY;

    /*
     * Done. Just update object readiness state.
     */
    if (SUCCEEDED(hrc))
        autoInitSpan.setSucceeded();
    else
    {
        if (m)
            i_close(); /* Free password and whatever i_openHddForIO() may accidentally leave around on failure. */
        autoInitSpan.setFailed(hrc);
    }

    LogFlowThisFunc(("returns %Rhrc\n", hrc));
    return hrc;
}

/**
 * Uninitializes the instance (called from FinalRelease()).
 */
void MediumIO::uninit()
{
    LogFlowThisFuncEnter();

    /* Enclose the state transition Ready->InUninit->NotReady */
    AutoUninitSpan autoUninitSpan(this);
    if (!autoUninitSpan.uninitDone())
    {
        if (m)
        {
            i_close();

            delete m;
            m = NULL;
        }
    }

    LogFlowThisFuncLeave();
}


/*********************************************************************************************************************************
*   IMediumIO attributes                                                                                                         *
*********************************************************************************************************************************/

HRESULT MediumIO::getMedium(ComPtr<IMedium> &a_rPtrMedium)
{
    a_rPtrMedium = m->ptrMedium;
    return S_OK;
}

HRESULT MediumIO::getWritable(BOOL *a_fWritable)
{
    *a_fWritable = m->fWritable;
    return S_OK;
}

HRESULT MediumIO::getExplorer(ComPtr<IVFSExplorer> &a_rPtrExplorer)
{
    RT_NOREF_PV(a_rPtrExplorer);
    return E_NOTIMPL;
}


/*********************************************************************************************************************************
*   IMediumIO methods                                                                                                            *
*********************************************************************************************************************************/

HRESULT MediumIO::read(LONG64 a_off, ULONG a_cbRead, std::vector<BYTE> &a_rData)
{
    /*
     * Validate input.
     */
    if (a_cbRead > _256K)
        return setError(E_INVALIDARG, tr("Max read size is 256KB, given: %u"), a_cbRead);
    if (a_cbRead == 0)
        return setError(E_INVALIDARG, tr("Zero byte read is not supported."));

    /*
     * Allocate return buffer.
     */
    try
    {
        a_rData.resize(a_cbRead);
    }
    catch (std::bad_alloc &)
    {
        return E_OUTOFMEMORY;
    }

    /*
     * Do the reading.  To play safe we exclusivly lock the object while doing this.
     */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    size_t cbActual = 0;
    int vrc = RTVfsFileReadAt(m->hVfsFile, a_off, &a_rData.front(), a_cbRead, &cbActual);
    alock.release();

    /*
     * Manage the result.
     */
    HRESULT hrc;
    if (RT_SUCCESS(vrc))
    {
        if (cbActual != a_cbRead)
        {
            Assert(cbActual < a_cbRead);
            a_rData.resize(cbActual);
        }
        hrc = S_OK;
    }
    else
    {
        a_rData.resize(0);
        hrc = setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Error reading %u bytes at %RU64: %Rrc", "", a_cbRead),
                           a_cbRead, a_off, vrc);
    }

    return hrc;
}

HRESULT MediumIO::write(LONG64 a_off, const std::vector<BYTE> &a_rData, ULONG *a_pcbWritten)
{
    /*
     * Validate input.
     */
    size_t cbToWrite = a_rData.size();
    if (cbToWrite == 0)
        return setError(E_INVALIDARG, tr("Zero byte write is not supported."));
    if (!m->fWritable)
        return setError(E_ACCESSDENIED, tr("Medium not opened for writing."));
    CheckComArgPointerValid(a_pcbWritten);
    *a_pcbWritten = 0;

    /*
     * Do the writing.  To play safe we exclusivly lock the object while doing this.
     */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    size_t cbActual = 0;
    int vrc = RTVfsFileWriteAt(m->hVfsFile, a_off, &a_rData.front(), cbToWrite, &cbActual);
    alock.release();

    /*
     * Manage the result.
     */
    HRESULT hrc;
    if (RT_SUCCESS(vrc))
    {
        *a_pcbWritten = (ULONG)cbActual;
        hrc = S_OK;
    }
    else
        hrc = setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Error writing %zu bytes at %RU64: %Rrc", "", cbToWrite),
                           cbToWrite, a_off, vrc);

    return hrc;
}

HRESULT MediumIO::formatFAT(BOOL a_fQuick)
{
    /*
     * Validate input.
     */
    if (!m->fWritable)
        return setError(E_ACCESSDENIED, tr("Medium not opened for writing."));

    /*
     * Format the medium as FAT and let the format API figure the parameters.
     * We exclusivly lock the object while doing this as concurrent medium access makes no sense.
     */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    RTERRINFOSTATIC ErrInfo;
    int vrc = RTFsFatVolFormat(m->hVfsFile, 0, 0, a_fQuick ? RTFSFATVOL_FMT_F_QUICK : RTFSFATVOL_FMT_F_FULL,
                               (uint16_t)m->cbSector, 0, RTFSFATTYPE_INVALID, 0, 0, 0, 0, 0, RTErrInfoInitStatic(&ErrInfo));
    alock.release();

    /*
     * Manage the result.
     */
    HRESULT hrc;
    if (RT_SUCCESS(vrc))
        hrc = S_OK;
    else if (RTErrInfoIsSet(&ErrInfo.Core))
        hrc = setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Error formatting (%Rrc): %s"), vrc, ErrInfo.Core.pszMsg);
    else
        hrc = setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("Error formatting: %Rrc"), vrc);

    return hrc;
}

HRESULT MediumIO::initializePartitionTable(PartitionTableType_T a_enmFormat, BOOL a_fWholeDiskInOneEntry)
{
    /*
     * Validate input.
     */
    const char *pszFormat;
    if (a_enmFormat == PartitionTableType_MBR)
        pszFormat = "MBR"; /* RTDVMFORMATTYPE_MBR */
    else if (a_enmFormat == PartitionTableType_GPT)
        pszFormat = "GPT"; /* RTDVMFORMATTYPE_GPT */
    else
        return setError(E_INVALIDARG, tr("Invalid partition format type: %d"), a_enmFormat);
    if (!m->fWritable)
        return setError(E_ACCESSDENIED, tr("Medium not opened for writing."));
    if (a_fWholeDiskInOneEntry)
        return setError(E_NOTIMPL, tr("whole-disk-in-one-entry is not implemented yet, sorry."));

    /*
     * Do the partitioning.
     * We exclusivly lock the object while doing this as concurrent medium access makes little sense.
     */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);

    RTDVM hVolMgr;
    int vrc = RTDvmCreate(&hVolMgr, m->hVfsFile, m->cbSector, 0 /*fFlags*/);
    HRESULT hrc;
    if (RT_SUCCESS(vrc))
    {
        vrc = RTDvmMapInitialize(hVolMgr, pszFormat); /** @todo Why doesn't RTDvmMapInitialize take RTDVMFORMATTYPE? */
        if (RT_SUCCESS(vrc))
        {
            /*
             * Create a partition for the whole disk?
             */
            hrc = S_OK; /** @todo a_fWholeDiskInOneEntry requies RTDvm to get a function for creating partitions. */
        }
        else
            hrc = setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("RTDvmMapInitialize failed: %Rrc"), vrc);
        RTDvmRelease(hVolMgr);
    }
    else
        hrc = setErrorBoth(VBOX_E_FILE_ERROR, vrc, tr("RTDvmCreate failed: %Rrc"), vrc);

    return hrc;
}

HRESULT MediumIO::convertToStream(const com::Utf8Str &aFormat,
                                  const std::vector<MediumVariant_T> &aVariant,
                                  ULONG aBufferSize,
                                  ComPtr<IDataStream> &aStream,
                                  ComPtr<IProgress> &aProgress)
{
    HRESULT hrc = S_OK;
    ComObjPtr<Progress> pProgress;
    ComObjPtr<DataStream> pDataStream;
    MediumIO::StreamTask *pTask = NULL;

    try
    {
        pDataStream.createObject();
        hrc = pDataStream->init(aBufferSize);
        if (FAILED(hrc))
            throw hrc;

        pProgress.createObject();
        hrc = pProgress->init(m->ptrVirtualBox,
                              static_cast<IMediumIO*>(this),
                              BstrFmt(tr("Converting medium '%s' to data stream"), m->ptrMedium->i_getLocationFull().c_str()),
                              TRUE /* aCancelable */);
        if (FAILED(hrc))
            throw hrc;

        ULONG mediumVariantFlags = 0;

        if (aVariant.size())
        {
            for (size_t i = 0; i < aVariant.size(); i++)
                mediumVariantFlags |= (ULONG)aVariant[i];
        }

        /* setup task object to carry out the operation asynchronously */
        pTask = new MediumIO::StreamTask(this, pDataStream, pProgress,
                                         aFormat.c_str(), (MediumVariant_T)mediumVariantFlags);
        hrc = pTask->hrc();
        AssertComRC(hrc);
        if (FAILED(hrc))
            throw hrc;
    }
    catch (HRESULT hrcXcpt) { hrc = hrcXcpt; }

    if (SUCCEEDED(hrc))
    {
        hrc = pTask->createThread();
        pTask = NULL;
        if (SUCCEEDED(hrc))
        {
            pDataStream.queryInterfaceTo(aStream.asOutParam());
            pProgress.queryInterfaceTo(aProgress.asOutParam());
        }
    }
    else if (pTask != NULL)
        delete pTask;

    return hrc;
}

HRESULT MediumIO::close()
{
    /*
     * We need a write lock here to exclude all other access.
     */
    AutoWriteLock alock(this COMMA_LOCKVAL_SRC_POS);
    i_close();
    return S_OK;
}



/*********************************************************************************************************************************
*   IMediumIO internal methods                                                                                                   *
*********************************************************************************************************************************/

/**
 * This is used by both uninit and close().
 *
 * Expects exclusive access (write lock or autouninit) to the object.
 */
void MediumIO::i_close()
{
    if (m->hVfsFile != NIL_RTVFSFILE)
    {
        uint32_t cRefs = RTVfsFileRelease(m->hVfsFile);
        Assert(cRefs == 0);
        NOREF(cRefs);

        m->hVfsFile = NIL_RTVFSFILE;
    }

    if (m->pHdd)
    {
        VDDestroy(m->pHdd);
        m->pHdd = NULL;
    }

    m->LockList.Clear();
    m->ptrMedium.setNull();
    m->PasswordStore.deleteAllSecretKeys(false /* fSuspend */, true /* fForce */);
}

