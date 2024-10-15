/* $Id: GuestSessionImplTasks.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VirtualBox Main - Guest session tasks.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_MAIN_GUESTSESSION
#include "LoggingNew.h"

#include "GuestImpl.h"
#ifndef VBOX_WITH_GUEST_CONTROL
# error "VBOX_WITH_GUEST_CONTROL must defined in this file"
#endif
#include "GuestSessionImpl.h"
#include "GuestSessionImplTasks.h"
#include "GuestCtrlImplPrivate.h"

#include "Global.h"
#include "AutoCaller.h"
#include "ConsoleImpl.h"
#include "ProgressImpl.h"

#include <memory> /* For auto_ptr. */

#include <iprt/env.h>
#include <iprt/file.h> /* For CopyTo/From. */
#include <iprt/dir.h>
#include <iprt/path.h>
#include <iprt/fsvfs.h>


/*********************************************************************************************************************************
*   Defines                                                                                                                      *
*********************************************************************************************************************************/

/**
 * (Guest Additions) ISO file flags.
 * Needed for handling Guest Additions updates.
 */
#define ISOFILE_FLAG_NONE                0
/** Copy over the file from host to the
 *  guest. */
#define ISOFILE_FLAG_COPY_FROM_ISO       RT_BIT(0)
/** Execute file on the guest after it has
 *  been successfully transferred. */
#define ISOFILE_FLAG_EXECUTE             RT_BIT(7)
/** File is optional, does not have to be
 *  existent on the .ISO. */
#define ISOFILE_FLAG_OPTIONAL            RT_BIT(8)


// session task classes
/////////////////////////////////////////////////////////////////////////////

GuestSessionTask::GuestSessionTask(GuestSession *pSession)
    : ThreadTask("GenericGuestSessionTask")
{
    mSession = pSession;

    switch (mSession->i_getGuestPathStyle())
    {
        case PathStyle_DOS:
            mstrGuestPathStyle = "\\";
            break;

        default:
            mstrGuestPathStyle = "/";
            break;
    }
}

GuestSessionTask::~GuestSessionTask(void)
{
}

/**
 * Creates (and initializes / sets) the progress objects of a guest session task.
 *
 * @returns VBox status code.
 * @param   cOperations         Number of operation the task wants to perform.
 */
int GuestSessionTask::createAndSetProgressObject(ULONG cOperations /* = 1 */)
{
    LogFlowThisFunc(("cOperations=%ld\n", cOperations));

    /* Create the progress object. */
    ComObjPtr<Progress> pProgress;
    HRESULT hrc = pProgress.createObject();
    if (FAILED(hrc))
        return VERR_COM_UNEXPECTED;

    hrc = pProgress->init(static_cast<IGuestSession*>(mSession),
                          Bstr(mDesc).raw(),
                          TRUE /* aCancelable */, cOperations, Bstr(mDesc).raw());
    if (FAILED(hrc))
        return VERR_COM_UNEXPECTED;

    mProgress = pProgress;

    LogFlowFuncLeave();
    return VINF_SUCCESS;
}

#if 0 /* unused */
/** @note The task object is owned by the thread after this returns, regardless of the result.  */
int GuestSessionTask::RunAsync(const Utf8Str &strDesc, ComObjPtr<Progress> &pProgress)
{
    LogFlowThisFunc(("strDesc=%s\n", strDesc.c_str()));

    mDesc = strDesc;
    mProgress = pProgress;
    HRESULT hrc = createThreadWithType(RTTHREADTYPE_MAIN_HEAVY_WORKER);

    LogFlowThisFunc(("Returning hrc=%Rhrc\n", hrc));
    return Global::vboxStatusCodeToCOM(hrc);
}
#endif

/**
 * Gets a guest property from the VM.
 *
 * @returns VBox status code.
 * @param   pGuest              Guest object of VM to get guest property from.
 * @param   strPath             Guest property to path to get.
 * @param   strValue            Where to store the guest property value on success.
 */
int GuestSessionTask::getGuestProperty(const ComObjPtr<Guest> &pGuest,
                                       const Utf8Str &strPath, Utf8Str &strValue)
{
    ComObjPtr<Console> pConsole = pGuest->i_getConsole();
    const ComPtr<IMachine> pMachine = pConsole->i_machine();

    Assert(!pMachine.isNull());
    Bstr strTemp, strFlags;
    LONG64 i64Timestamp;
    HRESULT hrc = pMachine->GetGuestProperty(Bstr(strPath).raw(), strTemp.asOutParam(), &i64Timestamp, strFlags.asOutParam());
    if (SUCCEEDED(hrc))
    {
        strValue = strTemp;
        return VINF_SUCCESS;
    }
    return VERR_NOT_FOUND;
}

/**
 * Sets the percentage of a guest session task progress.
 *
 * @returns VBox status code.
 * @param   uPercent            Percentage (0-100) to set.
 */
int GuestSessionTask::setProgress(ULONG uPercent)
{
    if (mProgress.isNull()) /* Progress is optional. */
        return VINF_SUCCESS;

    BOOL fCanceled;
    if (   SUCCEEDED(mProgress->COMGETTER(Canceled(&fCanceled)))
        && fCanceled)
        return VERR_CANCELLED;
    BOOL fCompleted;
    if (   SUCCEEDED(mProgress->COMGETTER(Completed(&fCompleted)))
        && fCompleted)
    {
        AssertMsgFailed(("Setting value of an already completed progress\n"));
        return VINF_SUCCESS;
    }
    HRESULT hrc = mProgress->SetCurrentOperationProgress(uPercent);
    if (FAILED(hrc))
        return VERR_COM_UNEXPECTED;

    return VINF_SUCCESS;
}

/**
 * Sets the task's progress object to succeeded.
 *
 * @returns VBox status code.
 */
int GuestSessionTask::setProgressSuccess(void)
{
    if (mProgress.isNull()) /* Progress is optional. */
        return VINF_SUCCESS;

    BOOL fCompleted;
    if (   SUCCEEDED(mProgress->COMGETTER(Completed(&fCompleted)))
        && !fCompleted)
    {
#ifdef VBOX_STRICT
        ULONG uCurOp; mProgress->COMGETTER(Operation(&uCurOp));
        ULONG cOps;   mProgress->COMGETTER(OperationCount(&cOps));
        AssertMsg(uCurOp + 1 /* Zero-based */ == cOps, ("Not all operations done yet (%u/%u)\n", uCurOp + 1, cOps));
#endif
        HRESULT hrc = mProgress->i_notifyComplete(S_OK);
        if (FAILED(hrc))
            return VERR_COM_UNEXPECTED; /** @todo Find a better vrc. */
    }

    return VINF_SUCCESS;
}

/**
 * Sets the task's progress object to an error using a string message.
 *
 * @returns Returns \a hrc for convenience.
 * @param   hrc                 Progress operation result to set.
 * @param   strMsg              Message to set.
 */
HRESULT GuestSessionTask::setProgressErrorMsg(HRESULT hrc, const Utf8Str &strMsg)
{
    LogFlowFunc(("hrc=%Rhrc, strMsg=%s\n", hrc, strMsg.c_str()));

    if (mProgress.isNull()) /* Progress is optional. */
        return hrc; /* Return original status. */

    BOOL fCanceled;
    BOOL fCompleted;
    if (   SUCCEEDED(mProgress->COMGETTER(Canceled(&fCanceled)))
        && !fCanceled
        && SUCCEEDED(mProgress->COMGETTER(Completed(&fCompleted)))
        && !fCompleted)
    {
        HRESULT hrc2 = mProgress->i_notifyComplete(hrc,
                                                   COM_IIDOF(IGuestSession),
                                                   GuestSession::getStaticComponentName(),
                                                   /* Make sure to hand-in the message via format string to avoid problems
                                                    * with (file) paths which e.g. contain "%s" and friends. Can happen with
                                                    * randomly generated Validation Kit stuff. */
                                                   "%s", strMsg.c_str());
        if (FAILED(hrc2))
            return hrc2;
    }
    return hrc; /* Return original status. */
}

/**
 * Sets the task's progress object to an error using a string message and a guest error info object.
 *
 * @returns Returns \a hrc for convenience.
 * @param   hrc                 Progress operation result to set.
 * @param   strMsg              Message to set.
 * @param   guestErrorInfo      Guest error info to use.
 */
HRESULT GuestSessionTask::setProgressErrorMsg(HRESULT hrc, const Utf8Str &strMsg, const GuestErrorInfo &guestErrorInfo)
{
    return setProgressErrorMsg(hrc, strMsg + Utf8Str(": ") + GuestBase::getErrorAsString(guestErrorInfo));
}

/**
 * Creates a directory on the guest.
 *
 * @return VBox status code.
 *         VINF_ALREADY_EXISTS if directory on the guest already exists (\a fCanExist is \c true).
 *         VWRN_ALREADY_EXISTS if directory on the guest already exists but must not exist (\a fCanExist is \c false).
 * @param  strPath                  Absolute path to directory on the guest (guest style path) to create.
 * @param  fMode                    Directory mode to use for creation.
 * @param  enmDirectoryCreateFlags  Directory creation flags.
 * @param  fFollowSymlinks          Whether to follow symlinks on the guest or not.
 * @param  fCanExist                Whether the directory to create is allowed to exist already.
 */
int GuestSessionTask::directoryCreateOnGuest(const com::Utf8Str &strPath,
                                             uint32_t fMode, DirectoryCreateFlag_T enmDirectoryCreateFlags,
                                             bool fFollowSymlinks, bool fCanExist)
{
    LogFlowFunc(("strPath=%s, enmDirectoryCreateFlags=0x%x, fMode=%RU32, fFollowSymlinks=%RTbool, fCanExist=%RTbool\n",
                 strPath.c_str(), enmDirectoryCreateFlags, fMode, fFollowSymlinks, fCanExist));

    GuestFsObjData objData;
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = mSession->i_directoryQueryInfo(strPath, fFollowSymlinks, objData, &vrcGuest);
    if (RT_SUCCESS(vrc))
    {
        if (!fCanExist)
        {
            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                Utf8StrFmt(tr("Guest directory \"%s\" already exists"), strPath.c_str()));
            vrc = VERR_ALREADY_EXISTS;
        }
        else
            vrc = VWRN_ALREADY_EXISTS;
    }
    else
    {
        switch (vrc)
        {
            case VERR_GSTCTL_GUEST_ERROR:
            {
                switch (vrcGuest)
                {
                    case VERR_FILE_NOT_FOUND:
                        RT_FALL_THROUGH();
                    case VERR_PATH_NOT_FOUND:
                        vrc = mSession->i_directoryCreate(strPath.c_str(), fMode, enmDirectoryCreateFlags, &vrcGuest);
                        break;
                    default:
                        break;
                }

                if (RT_FAILURE(vrc))
                    setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                        Utf8StrFmt(tr("Guest error creating directory \"%s\" on the guest: %Rrc"),
                                                   strPath.c_str(), vrcGuest));
                break;
            }

            default:
                setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                    Utf8StrFmt(tr("Host error creating directory \"%s\" on the guest: %Rrc"),
                                               strPath.c_str(), vrc));
                break;
        }
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Creates a directory on the host.
 *
 * @return VBox status code. VERR_ALREADY_EXISTS if directory on the guest already exists.
 * @param  strPath                  Absolute path to directory on the host (host style path) to create.
 * @param  fMode                    Directory mode to use for creation.
 * @param  fCreate                  Directory creation flags.
 * @param  fCanExist                Whether the directory to create is allowed to exist already.
 */
int GuestSessionTask::directoryCreateOnHost(const com::Utf8Str &strPath, uint32_t fMode, uint32_t fCreate, bool fCanExist)
{
    LogFlowFunc(("strPath=%s, fMode=%RU32, fCreate=0x%x, fCanExist=%RTbool\n", strPath.c_str(), fMode, fCreate, fCanExist));

    LogRel2(("Guest Control: Creating host directory \"%s\" ...\n", strPath.c_str()));

    int vrc = RTDirCreate(strPath.c_str(), fMode, fCreate);
    if (RT_FAILURE(vrc))
    {
        if (vrc == VERR_ALREADY_EXISTS)
        {
            if (!fCanExist)
            {
                setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                    Utf8StrFmt(tr("Host directory \"%s\" already exists"), strPath.c_str()));
            }
            else
                vrc = VINF_SUCCESS;
        }
        else
            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                Utf8StrFmt(tr("Could not create host directory \"%s\": %Rrc"),
                                           strPath.c_str(), vrc));
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Main function for copying a file from guest to the host.
 *
 * @return VBox status code.
 * @param  strSrcFile         Full path of source file on the host to copy.
 * @param  srcFile            Guest file (source) to copy to the host. Must be in opened and ready state already.
 * @param  strDstFile         Full destination path and file name (guest style) to copy file to.
 * @param  phDstFile          Pointer to host file handle (destination) to copy to. Must be in opened and ready state already.
 * @param  fFileCopyFlags     File copy flags.
 * @param  offCopy            Offset (in bytes) where to start copying the source file.
 * @param  cbSize             Size (in bytes) to copy from the source file.
 */
int GuestSessionTask::fileCopyFromGuestInner(const Utf8Str &strSrcFile, ComObjPtr<GuestFile> &srcFile,
                                             const Utf8Str &strDstFile, PRTFILE phDstFile,
                                             FileCopyFlag_T fFileCopyFlags, uint64_t offCopy, uint64_t cbSize)
{
    RT_NOREF(fFileCopyFlags);

    if (!cbSize) /* Nothing to copy, i.e. empty file? Bail out. */
        return VINF_SUCCESS;

    BOOL     fCanceled      = FALSE;
    uint64_t cbWrittenTotal = 0;
    uint64_t cbToRead       = cbSize;

    int vrc = VINF_SUCCESS;

    if (offCopy)
    {
        uint64_t offActual;
        vrc = srcFile->i_seekAt(offCopy, GUEST_FILE_SEEKTYPE_BEGIN, GSTCTL_DEFAULT_TIMEOUT_MS, &offActual);
        if (RT_FAILURE(vrc))
        {
            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                Utf8StrFmt(tr("Seeking to offset %RU64 of guest file \"%s\" failed: %Rrc"),
                                           offCopy, strSrcFile.c_str(), vrc));
            return vrc;
        }
    }

    BYTE byBuf[_64K]; /** @todo Can we do better here? */
    while (cbToRead)
    {
        uint32_t cbRead;
        const uint32_t cbChunk = RT_MIN(cbToRead, sizeof(byBuf));
        vrc = srcFile->i_readData(cbChunk, GSTCTL_DEFAULT_TIMEOUT_MS, byBuf, sizeof(byBuf), &cbRead);
        if (RT_FAILURE(vrc))
        {
            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                Utf8StrFmt(tr("Reading %RU32 bytes @ %RU64 from guest \"%s\" failed: %Rrc", "", cbChunk),
                                           cbChunk, cbWrittenTotal, strSrcFile.c_str(), vrc));
            break;
        }

        vrc = RTFileWrite(*phDstFile, byBuf, cbRead, NULL /* No partial writes */);
        if (RT_FAILURE(vrc))
        {
            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                Utf8StrFmt(tr("Writing %RU32 bytes to host file \"%s\" failed: %Rrc", "", cbRead),
                                           cbRead, strDstFile.c_str(), vrc));
            break;
        }

        AssertBreak(cbToRead >= cbRead);
        cbToRead -= cbRead;

        /* Update total bytes written to the guest. */
        cbWrittenTotal += cbRead;
        AssertBreak(cbWrittenTotal <= cbSize);

        /* Did the user cancel the operation above? */
        if (   SUCCEEDED(mProgress->COMGETTER(Canceled(&fCanceled)))
            && fCanceled)
            break;

        AssertBreakStmt(cbSize, vrc = VERR_INTERNAL_ERROR);
        vrc = setProgress(((double)cbWrittenTotal / (double)cbSize) * 100);
        if (RT_FAILURE(vrc))
            break;
    }

    if (   SUCCEEDED(mProgress->COMGETTER(Canceled(&fCanceled)))
        && fCanceled)
        return VINF_SUCCESS;

    if (RT_FAILURE(vrc))
        return vrc;

    /*
     * Even if we succeeded until here make sure to check whether we really transferred
     * everything.
     */
    if (cbWrittenTotal == 0)
    {
        /* If nothing was transferred but the file size was > 0 then "vbox_cat" wasn't able to write
         * to the destination -> access denied. */
        setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                            Utf8StrFmt(tr("Writing guest file \"%s\" to host file \"%s\" failed: Access denied"),
                                       strSrcFile.c_str(), strDstFile.c_str()));
        vrc = VERR_ACCESS_DENIED;
    }
    else if (cbWrittenTotal < cbSize)
    {
        /* If we did not copy all let the user know. */
        setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                            Utf8StrFmt(tr("Copying guest file \"%s\" to host file \"%s\" failed (%RU64/%RU64 bytes transferred)"),
                                       strSrcFile.c_str(), strDstFile.c_str(), cbWrittenTotal, cbSize));
        vrc = VERR_INTERRUPTED;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Closes a formerly opened guest file.
 *
 * @returns VBox status code.
 * @param   file                Guest file to close.
 *
 * @note    Set a progress error message on error.
 */
int GuestSessionTask::fileClose(const ComObjPtr<GuestFile> &file)
{
    int vrcGuest;
    int vrc = file->i_close(&vrcGuest);
    if (RT_FAILURE(vrc))
    {
        Utf8Str strFilename;
        HRESULT const hrc = file->getFilename(strFilename);
        AssertComRCReturn(hrc, VERR_OBJECT_DESTROYED);
        setProgressErrorMsg(VBOX_E_IPRT_ERROR, Utf8StrFmt(tr("Error closing guest file \"%s\": %Rrc"),
                                                          strFilename.c_str(), vrc == VERR_GSTCTL_GUEST_ERROR ? vrcGuest : vrc));
        if (RT_SUCCESS(vrc))
            vrc = vrc == VERR_GSTCTL_GUEST_ERROR ? vrcGuest : vrc;
    }

    return vrc;
}

/**
 * Copies a file from the guest to the host.
 *
 * @return VBox status code.
 * @retval VWRN_ALREADY_EXISTS  if the file already exists and FileCopyFlag_NoReplace is specified,
 *                              *or * the file at the destination has the same (or newer) modification time
 *                              and FileCopyFlag_Update is specified.
 * @param  strSrc               Full path of source file on the guest to copy.
 * @param  strDst               Full destination path and file name (host style) to copy file to.
 * @param  fFileCopyFlags       File copy flags.
 */
int GuestSessionTask::fileCopyFromGuest(const Utf8Str &strSrc, const Utf8Str &strDst, FileCopyFlag_T fFileCopyFlags)
{
    LogFlowThisFunc(("strSource=%s, strDest=%s, enmFileCopyFlags=%#x\n", strSrc.c_str(), strDst.c_str(), fFileCopyFlags));

    GuestFileOpenInfo srcOpenInfo;
    srcOpenInfo.mFilename     = strSrc;
    srcOpenInfo.mOpenAction   = FileOpenAction_OpenExisting;
    srcOpenInfo.mAccessMode   = FileAccessMode_ReadOnly;
    srcOpenInfo.mSharingMode  = FileSharingMode_All; /** @todo Use _Read when implemented. */

    ComObjPtr<GuestFile> srcFile;

    GuestFsObjData srcObjData;
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = mSession->i_fsObjQueryInfo(strSrc, TRUE /* fFollowSymlinks */, srcObjData, &vrcGuest);
    if (RT_FAILURE(vrc))
    {
        if (vrc == VERR_GSTCTL_GUEST_ERROR)
            setProgressErrorMsg(VBOX_E_IPRT_ERROR, tr("Guest file lookup failed"),
                                GuestErrorInfo(GuestErrorInfo::Type_Fs, vrcGuest, strSrc.c_str()));
        else
            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                Utf8StrFmt(tr("Guest file lookup for \"%s\" failed: %Rrc"), strSrc.c_str(), vrc));
    }
    else
    {
        switch (srcObjData.mType)
        {
            case FsObjType_File:
                break;

            case FsObjType_Symlink:
                if (!(fFileCopyFlags & FileCopyFlag_FollowLinks))
                {
                    setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                        Utf8StrFmt(tr("Guest file \"%s\" is a symbolic link"),
                                                   strSrc.c_str()));
                    vrc = VERR_IS_A_SYMLINK;
                }
                break;

            default:
                setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                    Utf8StrFmt(tr("Guest object \"%s\" is not a file (is type %#x)"),
                                               strSrc.c_str(), srcObjData.mType));
                vrc = VERR_NOT_A_FILE;
                break;
        }
    }

    if (RT_FAILURE(vrc))
        return vrc;

    vrc = mSession->i_fileOpen(srcOpenInfo, srcFile, &vrcGuest);
    if (RT_FAILURE(vrc))
    {
        if (vrc == VERR_GSTCTL_GUEST_ERROR)
            setProgressErrorMsg(VBOX_E_IPRT_ERROR, tr("Guest file could not be opened"),
                                GuestErrorInfo(GuestErrorInfo::Type_File, vrcGuest, strSrc.c_str()));
        else
            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                Utf8StrFmt(tr("Guest file \"%s\" could not be opened: %Rrc"), strSrc.c_str(), vrc));
    }

    if (RT_FAILURE(vrc))
        return vrc;

    RTFSOBJINFO dstObjInfo;
    RT_ZERO(dstObjInfo);

    bool fSkip = false; /* Whether to skip handling the file. */

    if (RT_SUCCESS(vrc))
    {
        vrc = RTPathQueryInfo(strDst.c_str(), &dstObjInfo, RTFSOBJATTRADD_NOTHING);
        if (RT_SUCCESS(vrc))
        {
            if (fFileCopyFlags & FileCopyFlag_NoReplace)
            {
                LogRel2(("Guest Control: Host file \"%s\" already exists, skipping\n", strDst.c_str()));
                vrc = VWRN_ALREADY_EXISTS;
                fSkip = true;
            }

            if (   !fSkip
                && fFileCopyFlags & FileCopyFlag_Update)
            {
                RTTIMESPEC srcModificationTimeTS;
                RTTimeSpecSetSeconds(&srcModificationTimeTS, srcObjData.mModificationTime);
                if (RTTimeSpecCompare(&srcModificationTimeTS, &dstObjInfo.ModificationTime) <= 0)
                {
                    LogRel2(("Guest Control: Host file \"%s\" has same or newer modification date, skipping\n", strDst.c_str()));
                    vrc = VWRN_ALREADY_EXISTS;
                    fSkip = true;
                }
            }
        }
        else
        {
            if (vrc == VERR_PATH_NOT_FOUND)       /* Destination file does not exist (yet)? */
                vrc = VERR_FILE_NOT_FOUND;        /* Needed in next block further down. */
            else if (vrc != VERR_FILE_NOT_FOUND)  /* Ditto. */
                setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                    Utf8StrFmt(tr("Host file lookup for \"%s\" failed: %Rrc"), strDst.c_str(), vrc));
        }
    }

    if (fSkip)
    {
        int vrc2 = fileClose(srcFile);
        if (RT_SUCCESS(vrc))
            vrc = vrc2;

        return vrc;
    }

    if (RT_SUCCESS(vrc))
    {
        if (RTFS_IS_FILE(dstObjInfo.Attr.fMode))
        {
            if (fFileCopyFlags & FileCopyFlag_NoReplace)
            {
                setProgressErrorMsg(VBOX_E_IPRT_ERROR, Utf8StrFmt(tr("Host file \"%s\" already exists"), strDst.c_str()));
                vrc = VERR_ALREADY_EXISTS;
            }
        }
        else if (RTFS_IS_DIRECTORY(dstObjInfo.Attr.fMode))
        {
            setProgressErrorMsg(VBOX_E_IPRT_ERROR, Utf8StrFmt(tr("Host destination \"%s\" is a directory"), strDst.c_str()));
            vrc = VERR_IS_A_DIRECTORY;
        }
        else if (RTFS_IS_SYMLINK(dstObjInfo.Attr.fMode))
        {
            if (!(fFileCopyFlags & FileCopyFlag_FollowLinks))
            {
                setProgressErrorMsg(VBOX_E_IPRT_ERROR, Utf8StrFmt(tr("Host destination \"%s\" is a symbolic link"), strDst.c_str()));
                vrc = VERR_IS_A_SYMLINK;
            }
        }
        else
        {
            LogFlowThisFunc(("Host file system type %#x not supported\n", dstObjInfo.Attr.fMode & RTFS_TYPE_MASK));
            vrc = VERR_NOT_SUPPORTED;
        }
    }

    LogFlowFunc(("vrc=%Rrc, dstFsType=%#x, pszDstFile=%s\n", vrc, dstObjInfo.Attr.fMode & RTFS_TYPE_MASK, strDst.c_str()));

    if (   RT_SUCCESS(vrc)
        || vrc == VERR_FILE_NOT_FOUND)
    {
        LogRel2(("Guest Control: Copying file \"%s\" from guest to \"%s\" on host ...\n", strSrc.c_str(), strDst.c_str()));

        RTFILE hDstFile;
        vrc = RTFileOpen(&hDstFile, strDst.c_str(),
                         RTFILE_O_WRITE | RTFILE_O_OPEN_CREATE | RTFILE_O_DENY_WRITE); /** @todo Use the correct open modes! */
        if (RT_SUCCESS(vrc))
        {
            LogFlowThisFunc(("Copying \"%s\" to \"%s\" (%RI64 bytes) ...\n",
                             strSrc.c_str(), strDst.c_str(), srcObjData.mObjectSize));

            vrc = fileCopyFromGuestInner(strSrc, srcFile, strDst, &hDstFile, fFileCopyFlags,
                                         0 /* Offset, unused */, (uint64_t)srcObjData.mObjectSize);

            int vrc2 = RTFileClose(hDstFile);
            AssertRC(vrc2);
        }
        else
            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                Utf8StrFmt(tr("Opening/creating host file \"%s\" failed: %Rrc"), strDst.c_str(), vrc));
    }

    int vrc2 = fileClose(srcFile);
    if (RT_SUCCESS(vrc))
        vrc = vrc2;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Main function for copying a file from host to the guest.
 *
 * @return VBox status code.
 * @param  strSrcFile         Full path of source file on the host to copy.
 * @param  hVfsFile           The VFS file handle to read from.
 * @param  strDstFile         Full destination path and file name (guest style) to copy file to.
 * @param  fileDst            Guest file (destination) to copy to the guest. Must be in opened and ready state already.
 * @param  fFileCopyFlags     File copy flags.
 * @param  offCopy            Offset (in bytes) where to start copying the source file.
 * @param  cbSize             Size (in bytes) to copy from the source file.
 */
int GuestSessionTask::fileCopyToGuestInner(const Utf8Str &strSrcFile, RTVFSFILE hVfsFile,
                                           const Utf8Str &strDstFile, ComObjPtr<GuestFile> &fileDst,
                                           FileCopyFlag_T fFileCopyFlags, uint64_t offCopy, uint64_t cbSize)
{
    RT_NOREF(fFileCopyFlags);

    if (!cbSize) /* Nothing to copy, i.e. empty file? Bail out. */
        return VINF_SUCCESS;

    BOOL     fCanceled      = FALSE;
    uint64_t cbWrittenTotal = 0;
    uint64_t cbToRead       = cbSize;

    int vrc = VINF_SUCCESS;

    if (offCopy)
    {
        uint64_t offActual;
        vrc = RTVfsFileSeek(hVfsFile, offCopy, RTFILE_SEEK_END, &offActual);
        if (RT_FAILURE(vrc))
        {
            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                Utf8StrFmt(tr("Seeking to offset %RU64 of host file \"%s\" failed: %Rrc"),
                                           offCopy, strSrcFile.c_str(), vrc));
            return vrc;
        }
    }

    BYTE byBuf[_64K];
    while (cbToRead)
    {
        size_t cbRead;
        const uint32_t cbChunk = RT_MIN(cbToRead, sizeof(byBuf));
        vrc = RTVfsFileRead(hVfsFile, byBuf, cbChunk, &cbRead);
        if (RT_FAILURE(vrc))
        {
            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                Utf8StrFmt(tr("Reading %RU32 bytes @ %RU64 from host file \"%s\" failed: %Rrc"),
                                           cbChunk, cbWrittenTotal, strSrcFile.c_str(), vrc));
            break;
        }

        vrc = fileDst->i_writeData(GSTCTL_DEFAULT_TIMEOUT_MS, byBuf, (uint32_t)cbRead, NULL /* No partial writes */);
        if (RT_FAILURE(vrc))
        {
            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                Utf8StrFmt(tr("Writing %zu bytes to guest file \"%s\" failed: %Rrc"),
                                           cbRead, strDstFile.c_str(), vrc));
            break;
        }

        Assert(cbToRead >= cbRead);
        cbToRead -= cbRead;

        /* Update total bytes written to the guest. */
        cbWrittenTotal += cbRead;
        Assert(cbWrittenTotal <= cbSize);

        /* Did the user cancel the operation above? */
        if (   SUCCEEDED(mProgress->COMGETTER(Canceled(&fCanceled)))
            && fCanceled)
            break;

        AssertBreakStmt(cbSize, vrc = VERR_INTERNAL_ERROR);
        vrc = setProgress(((double)cbWrittenTotal / (double)cbSize) * 100);
        if (RT_FAILURE(vrc))
            break;
    }

    if (RT_FAILURE(vrc))
        return vrc;

    /*
     * Even if we succeeded until here make sure to check whether we really transferred
     * everything.
     */
    if (cbWrittenTotal == 0)
    {
        /* If nothing was transferred but the file size was > 0 then "vbox_cat" wasn't able to write
         * to the destination -> access denied. */
        setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                            Utf8StrFmt(tr("Writing to guest file \"%s\" failed: Access denied"),
                                       strDstFile.c_str()));
        vrc = VERR_ACCESS_DENIED;
    }
    else if (cbWrittenTotal < cbSize)
    {
        /* If we did not copy all let the user know. */
        setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                            Utf8StrFmt(tr("Copying to guest file \"%s\" failed (%RU64/%RU64 bytes transferred)"),
                                       strDstFile.c_str(), cbWrittenTotal, cbSize));
        vrc = VERR_INTERRUPTED;
    }

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Copies a file from the host to the guest.
 *
 * @return VBox status code.
 * @retval VWRN_ALREADY_EXISTS  if the file already exists and FileCopyFlag_NoReplace is specified,
 *                              *or * the file at the destination has the same (or newer) modification time
 *                              and FileCopyFlag_Update is specified.
 * @param  strSrc               Full path of source file on the host.
 * @param  strDst               Full destination path and file name (guest style) to copy file to. Guest-path style.
 * @param  fFileCopyFlags       File copy flags.
 */
int GuestSessionTask::fileCopyToGuest(const Utf8Str &strSrc, const Utf8Str &strDst, FileCopyFlag_T fFileCopyFlags)
{
    LogFlowThisFunc(("strSource=%s, strDst=%s, fFileCopyFlags=%#x\n", strSrc.c_str(), strDst.c_str(), fFileCopyFlags));

    GuestFileOpenInfo dstOpenInfo;
    dstOpenInfo.mFilename        = strDst;
    if (fFileCopyFlags & FileCopyFlag_NoReplace)
        dstOpenInfo.mOpenAction  = FileOpenAction_CreateNew;
    else
        dstOpenInfo.mOpenAction  = FileOpenAction_CreateOrReplace;
    dstOpenInfo.mAccessMode      = FileAccessMode_WriteOnly;
    dstOpenInfo.mSharingMode     = FileSharingMode_All; /** @todo Use _Read when implemented. */

    ComObjPtr<GuestFile> dstFile;
    int vrcGuest;
    int vrc = mSession->i_fileOpen(dstOpenInfo, dstFile, &vrcGuest);
    if (RT_FAILURE(vrc))
    {
        if (vrc == VERR_GSTCTL_GUEST_ERROR)
            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                Utf8StrFmt(tr("Guest file \"%s\" could not be created or replaced"), strDst.c_str()),
                                GuestErrorInfo(GuestErrorInfo::Type_File, vrcGuest, strDst.c_str()));
        else
            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                Utf8StrFmt(tr("Guest file \"%s\" could not be created or replaced: %Rrc"), strDst.c_str(), vrc));
        return vrc;
    }

    char szSrcReal[RTPATH_MAX];

    RTFSOBJINFO srcObjInfo;
    RT_ZERO(srcObjInfo);

    bool fSkip = false; /* Whether to skip handling the file. */

    if (RT_SUCCESS(vrc))
    {
        vrc = RTPathReal(strSrc.c_str(), szSrcReal, sizeof(szSrcReal));
        if (RT_FAILURE(vrc))
        {
            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                Utf8StrFmt(tr("Host path lookup for file \"%s\" failed: %Rrc"),
                                           strSrc.c_str(), vrc));
        }
        else
        {
            vrc = RTPathQueryInfo(szSrcReal, &srcObjInfo, RTFSOBJATTRADD_NOTHING);
            if (RT_SUCCESS(vrc))
            {
                /* Only perform a remote file query when needed.  */
                if (   (fFileCopyFlags & FileCopyFlag_Update)
                    || (fFileCopyFlags & FileCopyFlag_NoReplace))
                {
                    GuestFsObjData dstObjData;
                    vrc = mSession->i_fileQueryInfo(strDst, RT_BOOL(fFileCopyFlags & FileCopyFlag_FollowLinks), dstObjData,
                                                    &vrcGuest);
                    if (RT_SUCCESS(vrc))
                    {
                        if (fFileCopyFlags & FileCopyFlag_NoReplace)
                        {
                            LogRel2(("Guest Control: Guest file \"%s\" already exists, skipping\n", strDst.c_str()));
                            vrc = VWRN_ALREADY_EXISTS;
                            fSkip = true;
                        }

                        if (   !fSkip
                            && fFileCopyFlags & FileCopyFlag_Update)
                        {
                            RTTIMESPEC dstModificationTimeTS;
                            RTTimeSpecSetSeconds(&dstModificationTimeTS, dstObjData.mModificationTime);
                            if (RTTimeSpecCompare(&dstModificationTimeTS, &srcObjInfo.ModificationTime) <= 0)
                            {
                                LogRel2(("Guest Control: Guest file \"%s\" has same or newer modification date, skipping\n",
                                         strDst.c_str()));
                                vrc = VWRN_ALREADY_EXISTS;
                                fSkip = true;
                            }
                        }
                    }
                    else
                    {
                        if (vrc == VERR_GSTCTL_GUEST_ERROR)
                        {
                            switch (vrcGuest)
                            {
                                case VERR_FILE_NOT_FOUND:
                                    vrc = VINF_SUCCESS;
                                    break;

                                default:
                                    setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                                        Utf8StrFmt(tr("Guest error while determining object data for guest file \"%s\": %Rrc"),
                                                                   strDst.c_str(), vrcGuest));
                                    break;
                            }
                        }
                        else
                            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                                Utf8StrFmt(tr("Host error while determining object data for guest file \"%s\": %Rrc"),
                                                           strDst.c_str(), vrc));
                    }
                }
            }
            else
            {
                setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                    Utf8StrFmt(tr("Host source file lookup for \"%s\" failed: %Rrc"),
                                               szSrcReal, vrc));
            }
        }
    }

    if (fSkip)
    {
        int vrc2 = fileClose(dstFile);
        if (RT_SUCCESS(vrc))
            vrc = vrc2;

        return vrc;
    }

    if (RT_SUCCESS(vrc))
    {
        LogRel2(("Guest Control: Copying file \"%s\" from host to \"%s\" on guest ...\n", strSrc.c_str(), strDst.c_str()));

        RTVFSFILE hSrcFile;
        vrc = RTVfsFileOpenNormal(szSrcReal, RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_WRITE, &hSrcFile);
        if (RT_SUCCESS(vrc))
        {
            LogFlowThisFunc(("Copying \"%s\" to \"%s\" (%RI64 bytes) ...\n",
                             szSrcReal, strDst.c_str(), srcObjInfo.cbObject));

            vrc = fileCopyToGuestInner(szSrcReal, hSrcFile, strDst, dstFile,
                                       fFileCopyFlags, 0 /* Offset, unused */, srcObjInfo.cbObject);

            int vrc2 = RTVfsFileRelease(hSrcFile);
            AssertRC(vrc2);
        }
        else
            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                Utf8StrFmt(tr("Opening host file \"%s\" failed: %Rrc"),
                                           szSrcReal, vrc));
    }

    int vrc2 = fileClose(dstFile);
    if (RT_SUCCESS(vrc))
        vrc = vrc2;

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

/**
 * Adds a guest file system entry to a given list.
 *
 * @return VBox status code.
 * @param  strFile              Path to file system entry to add.
 * @param  fsObjData            Guest file system information of entry to add.
 */
int FsList::AddEntryFromGuest(const Utf8Str &strFile, const GuestFsObjData &fsObjData)
{
    LogFlowFunc(("Adding \"%s\"\n", strFile.c_str()));

    FsEntry *pEntry = NULL;
    try
    {
        pEntry = new FsEntry();
        pEntry->fMode = fsObjData.GetFileMode();
        pEntry->strPath = strFile;

        mVecEntries.push_back(pEntry);
    }
    catch (std::bad_alloc &)
    {
        if (pEntry)
            delete pEntry;
        return VERR_NO_MEMORY;
    }

    return VINF_SUCCESS;
}

/**
 * Adds a host file system entry to a given list.
 *
 * @return VBox status code.
 * @param  strFile              Path to file system entry to add.
 * @param  pcObjInfo            File system information of entry to add.
 */
int FsList::AddEntryFromHost(const Utf8Str &strFile, PCRTFSOBJINFO pcObjInfo)
{
    LogFlowFunc(("Adding \"%s\"\n", strFile.c_str()));

    FsEntry *pEntry = NULL;
    try
    {
        pEntry = new FsEntry();
        pEntry->fMode = pcObjInfo->Attr.fMode;
        pEntry->strPath = strFile;

        mVecEntries.push_back(pEntry);
    }
    catch (std::bad_alloc &)
    {
        if (pEntry)
            delete pEntry;
        return VERR_NO_MEMORY;
    }

    return VINF_SUCCESS;
}

FsList::FsList(const GuestSessionTask &Task)
    : mTask(Task)
{
}

FsList::~FsList()
{
    Destroy();
}

/**
 * Initializes a file list.
 *
 * @return VBox status code.
 * @param  strSrcRootAbs        Source root path (absolute) for this file list.
 * @param  strDstRootAbs        Destination root path (absolute) for this file list.
 * @param  SourceSpec           Source specification to use.
 */
int FsList::Init(const Utf8Str &strSrcRootAbs, const Utf8Str &strDstRootAbs,
                 const GuestSessionFsSourceSpec &SourceSpec)
{
    mSrcRootAbs = strSrcRootAbs;
    mDstRootAbs = strDstRootAbs;
    mSourceSpec = SourceSpec;

    /* Note: Leave the source and dest roots unmodified -- how paths will be treated
     *       will be done directly when working on those. See @bugref{10139}. */

    LogFlowFunc(("mSrcRootAbs=%s, mDstRootAbs=%s, fDirCopyFlags=%#x, fFileCopyFlags=%#x\n",
                 mSrcRootAbs.c_str(), mDstRootAbs.c_str(), mSourceSpec.fDirCopyFlags, mSourceSpec.fFileCopyFlags));

    return VINF_SUCCESS;
}

/**
 * Destroys a file list.
 */
void FsList::Destroy(void)
{
    LogFlowFuncEnter();

    FsEntries::iterator itEntry = mVecEntries.begin();
    while (itEntry != mVecEntries.end())
    {
        FsEntry *pEntry = *itEntry;
        delete pEntry;
        mVecEntries.erase(itEntry);
        itEntry = mVecEntries.begin();
    }

    Assert(mVecEntries.empty());

    LogFlowFuncLeave();
}

#ifdef DEBUG
/**
 * Dumps a FsList to the debug log.
 */
void FsList::DumpToLog(void)
{
    LogFlowFunc(("strSrcRootAbs=%s, strDstRootAbs=%s\n", mSrcRootAbs.c_str(), mDstRootAbs.c_str()));

    FsEntries::iterator itEntry = mVecEntries.begin();
    while (itEntry != mVecEntries.end())
    {
        FsEntry *pEntry = *itEntry;
        LogFlowFunc(("\tstrPath=%s (fMode %#x)\n", pEntry->strPath.c_str(), pEntry->fMode));
        ++itEntry;
    }

    LogFlowFuncLeave();
}
#endif /* DEBUG */

/**
 * Builds a guest file list from a given path (and optional filter).
 *
 * @return VBox status code.
 * @param  strPath              Directory on the guest to build list from.
 * @param  strSubDir            Current sub directory path; needed for recursion.
 *                              Set to an empty path.
 */
int FsList::AddDirFromGuest(const Utf8Str &strPath, const Utf8Str &strSubDir /* = "" */)
{
    Utf8Str strPathAbs = strPath;
    if (!strPathAbs.endsWith(PATH_STYLE_SEP_STR(mSourceSpec.enmPathStyle)))
        strPathAbs += PATH_STYLE_SEP_STR(mSourceSpec.enmPathStyle);

    Utf8Str strPathSub = strSubDir;
    if (   strPathSub.isNotEmpty()
        && !strPathSub.endsWith(PATH_STYLE_SEP_STR(mSourceSpec.enmPathStyle)))
        strPathSub += PATH_STYLE_SEP_STR(mSourceSpec.enmPathStyle);

    strPathAbs += strPathSub;

    LogFlowFunc(("Entering \"%s\" (sub \"%s\")\n", strPathAbs.c_str(), strPathSub.c_str()));

    LogRel2(("Guest Control: Handling directory \"%s\" on guest ...\n", strPathAbs.c_str()));

    GuestDirectoryOpenInfo dirOpenInfo;
    dirOpenInfo.mFilter = "";
    dirOpenInfo.mPath   = strPathAbs;
    dirOpenInfo.mFlags  = 0; /** @todo Handle flags? */

    const ComObjPtr<GuestSession> &pSession = mTask.GetSession();

    ComObjPtr <GuestDirectory> pDir;
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = pSession->i_directoryOpen(dirOpenInfo, pDir, &vrcGuest);
    if (RT_FAILURE(vrc))
    {
        switch (vrc)
        {
            case VERR_INVALID_PARAMETER:
               break;

            case VERR_GSTCTL_GUEST_ERROR:
                break;

            default:
                break;
        }

        return vrc;
    }

    if (strPathSub.isNotEmpty())
    {
        GuestFsObjData fsObjData;
        fsObjData.mType = FsObjType_Directory;

        vrc = AddEntryFromGuest(strPathSub, fsObjData);
    }

    if (RT_SUCCESS(vrc))
    {
        ComObjPtr<GuestFsObjInfo> fsObjInfo;
        while (RT_SUCCESS(vrc = pDir->i_read(fsObjInfo, &vrcGuest)))
        {
            FsObjType_T enmObjType = FsObjType_Unknown; /* Shut up MSC. */
            HRESULT hrc2 = fsObjInfo->COMGETTER(Type)(&enmObjType);
            AssertComRC(hrc2);

            com::Bstr bstrName;
            hrc2 = fsObjInfo->COMGETTER(Name)(bstrName.asOutParam());
            AssertComRC(hrc2);

            Utf8Str strEntry = strPathSub + Utf8Str(bstrName);

            LogFlowFunc(("Entry \"%s\"\n", strEntry.c_str()));

            switch (enmObjType)
            {
                case FsObjType_Directory:
                {
                    if (   bstrName.equals(".")
                        || bstrName.equals(".."))
                    {
                        break;
                    }

                    LogRel2(("Guest Control: Directory \"%s\"\n", strEntry.c_str()));

                    if (!(mSourceSpec.fDirCopyFlags & DirectoryCopyFlag_Recursive))
                        break;

                    vrc = AddDirFromGuest(strPath, strEntry);
                    break;
                }

                case FsObjType_Symlink:
                {
                    if (   mSourceSpec.fDirCopyFlags  & DirectoryCopyFlag_FollowLinks
                        || mSourceSpec.fFileCopyFlags & FileCopyFlag_FollowLinks)
                    {
                        /** @todo Symlink handling from guest is not implemented yet.
                         *        See IGuestSession::symlinkRead(). */
                        LogRel2(("Guest Control: Warning: Symlink support on guest side not available, skipping \"%s\"\n",
                                 strEntry.c_str()));
                    }
                    break;
                }

                case FsObjType_File:
                {
                    LogRel2(("Guest Control: File \"%s\"\n", strEntry.c_str()));

                    vrc = AddEntryFromGuest(strEntry, fsObjInfo->i_getData());
                    break;
                }

                default:
                    break;
            }
        }

        if (   vrc      == VERR_GSTCTL_GUEST_ERROR
            && vrcGuest == VERR_NO_MORE_FILES) /* End of listing reached? */
            vrc = VINF_SUCCESS;
    }

    int vrc2 = pDir->i_close(&vrcGuest);
    if (RT_SUCCESS(vrc))
        vrc = vrc2;

    return vrc;
}

/**
 * Builds a host file list from a given path.
 *
 * @return VBox status code.
 * @param  strPath              Directory on the host to build list from.
 * @param  strSubDir            Current sub directory path; needed for recursion.
 *                              Set to an empty path.
 * @param  pszPathReal          Scratch buffer for holding the resolved real path.
 *                              Needed for recursion.
 * @param  cbPathReal           Size (in bytes) of \a pszPathReal.
 * @param  pDirEntry            Where to store looked up directory information for handled paths.
 *                              Needed for recursion.
 */
int FsList::AddDirFromHost(const Utf8Str &strPath, const Utf8Str &strSubDir,
                           char *pszPathReal, size_t cbPathReal, PRTDIRENTRYEX pDirEntry)
{
    Utf8Str strPathAbs = strPath;
    if (!strPathAbs.endsWith(RTPATH_SLASH_STR))
        strPathAbs += RTPATH_SLASH_STR;

    Utf8Str strPathSub = strSubDir;
    if (   strPathSub.isNotEmpty()
        && !strPathSub.endsWith(RTPATH_SLASH_STR))
        strPathSub += RTPATH_SLASH_STR;

    strPathAbs += strPathSub;

    LogFlowFunc(("Entering \"%s\" (sub \"%s\")\n", strPathAbs.c_str(), strPathSub.c_str()));

    LogRel2(("Guest Control: Handling directory \"%s\" on host ...\n", strPathAbs.c_str()));

    RTFSOBJINFO objInfo;
    int vrc = RTPathQueryInfo(strPathAbs.c_str(), &objInfo, RTFSOBJATTRADD_NOTHING);
    if (RT_SUCCESS(vrc))
    {
        if (RTFS_IS_DIRECTORY(objInfo.Attr.fMode))
        {
            if (strPathSub.isNotEmpty())
                vrc = AddEntryFromHost(strPathSub, &objInfo);

            if (RT_SUCCESS(vrc))
            {
                RTDIR hDir;
                vrc = RTDirOpen(&hDir, strPathAbs.c_str());
                if (RT_SUCCESS(vrc))
                {
                    do
                    {
                        /* Retrieve the next directory entry. */
                        vrc = RTDirReadEx(hDir, pDirEntry, NULL, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
                        if (RT_FAILURE(vrc))
                        {
                            if (vrc == VERR_NO_MORE_FILES)
                                vrc = VINF_SUCCESS;
                            break;
                        }

                        Utf8Str strEntry = strPathSub + Utf8Str(pDirEntry->szName);

                        LogFlowFunc(("Entry \"%s\"\n", strEntry.c_str()));

                        switch (pDirEntry->Info.Attr.fMode & RTFS_TYPE_MASK)
                        {
                            case RTFS_TYPE_DIRECTORY:
                            {
                                /* Skip "." and ".." entries. */
                                if (RTDirEntryExIsStdDotLink(pDirEntry))
                                    break;

                                LogRel2(("Guest Control: Directory \"%s\"\n", strEntry.c_str()));

                                if (!(mSourceSpec.fDirCopyFlags & DirectoryCopyFlag_Recursive))
                                    break;

                                vrc = AddDirFromHost(strPath, strEntry, pszPathReal, cbPathReal, pDirEntry);
                                break;
                            }

                            case RTFS_TYPE_FILE:
                            {
                                LogRel2(("Guest Control: File \"%s\"\n", strEntry.c_str()));

                                vrc = AddEntryFromHost(strEntry, &pDirEntry->Info);
                                break;
                            }

                            case RTFS_TYPE_SYMLINK:
                            {
                                Utf8Str strEntryAbs = strPathAbs + (const char *)pDirEntry->szName;

                                vrc = RTPathReal(strEntryAbs.c_str(), pszPathReal, cbPathReal);
                                if (RT_SUCCESS(vrc))
                                {
                                    vrc = RTPathQueryInfo(pszPathReal, &objInfo, RTFSOBJATTRADD_NOTHING);
                                    if (RT_SUCCESS(vrc))
                                    {
                                        if (RTFS_IS_DIRECTORY(objInfo.Attr.fMode))
                                        {
                                            LogRel2(("Guest Control: Symbolic link \"%s\" -> \"%s\" (directory)\n",
                                                     strEntryAbs.c_str(), pszPathReal));
                                            if (mSourceSpec.fDirCopyFlags  & DirectoryCopyFlag_FollowLinks)
                                                vrc = AddDirFromHost(strPath, strEntry, pszPathReal, cbPathReal, pDirEntry);
                                        }
                                        else if (RTFS_IS_FILE(objInfo.Attr.fMode))
                                        {
                                            LogRel2(("Guest Control: Symbolic link \"%s\" -> \"%s\" (file)\n",
                                                     strEntryAbs.c_str(), pszPathReal));
                                            if (mSourceSpec.fFileCopyFlags & FileCopyFlag_FollowLinks)
                                                vrc = AddEntryFromHost(strEntry, &objInfo);
                                        }
                                        else
                                            vrc = VERR_NOT_SUPPORTED;
                                    }

                                    if (RT_FAILURE(vrc))
                                        LogRel2(("Guest Control: Unable to query symbolic link info for \"%s\", vrc=%Rrc\n",
                                                 pszPathReal, vrc));
                                }
                                else
                                {
                                    LogRel2(("Guest Control: Unable to resolve symlink for \"%s\", vrc=%Rrc\n",
                                             strPathAbs.c_str(), vrc));
                                    if (vrc == VERR_FILE_NOT_FOUND) /* Broken symlink, skip. */
                                        vrc = VINF_SUCCESS;
                                }
                                break;
                            }

                            default:
                                break;
                        }

                    } while (RT_SUCCESS(vrc));

                    RTDirClose(hDir);
                }
            }
        }
        else if (RTFS_IS_FILE(objInfo.Attr.fMode))
            vrc = VERR_IS_A_FILE;
        else if (RTFS_IS_SYMLINK(objInfo.Attr.fMode))
            vrc = VERR_IS_A_SYMLINK;
        else
            vrc = VERR_NOT_SUPPORTED;
    }
    else
        LogFlowFunc(("Unable to query \"%s\", vrc=%Rrc\n", strPathAbs.c_str(), vrc));

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

GuestSessionTaskOpen::GuestSessionTaskOpen(GuestSession *pSession, uint32_t uFlags, uint32_t uTimeoutMS)
                                           : GuestSessionTask(pSession)
                                           , mFlags(uFlags)
                                           , mTimeoutMS(uTimeoutMS)
{
    m_strTaskName = "gctlSesOpen";
}

GuestSessionTaskOpen::~GuestSessionTaskOpen(void)
{

}

/** @copydoc GuestSessionTask::Run */
int GuestSessionTaskOpen::Run(void)
{
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(mSession);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    int vrc = mSession->i_startSession(NULL /*pvrcGuest*/);
    /* Nothing to do here anymore. */

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

GuestSessionCopyTask::GuestSessionCopyTask(GuestSession *pSession)
                                           : GuestSessionTask(pSession)
{
}

GuestSessionCopyTask::~GuestSessionCopyTask()
{
    FsLists::iterator itList = mVecLists.begin();
    while (itList != mVecLists.end())
    {
        FsList *pFsList = (*itList);
        pFsList->Destroy();
        delete pFsList;
        mVecLists.erase(itList);
        itList = mVecLists.begin();
    }

    Assert(mVecLists.empty());
}

GuestSessionTaskCopyFrom::GuestSessionTaskCopyFrom(GuestSession *pSession, GuestSessionFsSourceSet const &vecSrc,
                                                   const Utf8Str &strDest)
    : GuestSessionCopyTask(pSession)
{
    m_strTaskName = "gctlCpyFrm";

    mSources = vecSrc;
    mDest    = strDest;
}

GuestSessionTaskCopyFrom::~GuestSessionTaskCopyFrom(void)
{
}

/**
 * Initializes a copy-from-guest task.
 *
 * @returns HRESULT
 * @param   strTaskDesc         Friendly task description.
 */
HRESULT GuestSessionTaskCopyFrom::Init(const Utf8Str &strTaskDesc)
{
    setTaskDesc(strTaskDesc);

    /* Create the progress object. */
    ComObjPtr<Progress> pProgress;
    HRESULT hrc = pProgress.createObject();
    if (FAILED(hrc))
        return hrc;

    mProgress = pProgress;

    int vrc = VINF_SUCCESS;

    ULONG cOperations = 0;
    Utf8Str strErrorInfo;

    /**
     * Note: We need to build up the file/directory here instead of GuestSessionTaskCopyFrom::Run
     *       because the caller expects a ready-for-operation progress object on return.
     *       The progress object will have a variable operation count, based on the elements to
     *       be processed.
     */

    if (mSources.empty())
    {
        strErrorInfo.printf(tr("No guest sources specified"));
        vrc = VERR_INVALID_PARAMETER;
    }
    else if (mDest.isEmpty())
    {
        strErrorInfo.printf(tr("Host destination must not be empty"));
        vrc = VERR_INVALID_PARAMETER;
    }
    else
    {
        GuestSessionFsSourceSet::iterator itSrc = mSources.begin();
        while (itSrc != mSources.end())
        {
            Utf8Str strSrc = itSrc->strSource;
            Utf8Str strDst = mDest;

            bool    fFollowSymlinks;

            if (strSrc.isEmpty())
            {
                strErrorInfo.printf(tr("Guest source entry must not be empty"));
                vrc = VERR_INVALID_PARAMETER;
                break;
            }

            if (itSrc->enmType == FsObjType_Directory)
            {
                fFollowSymlinks = itSrc->fDirCopyFlags & DirectoryCopyFlag_FollowLinks;
            }
            else
            {
                fFollowSymlinks = RT_BOOL(itSrc->fFileCopyFlags & FileCopyFlag_FollowLinks);
            }

            LogFlowFunc(("strSrc=%s (path style is %s), strDst=%s, fFollowSymlinks=%RTbool\n",
                         strSrc.c_str(), GuestBase::pathStyleToStr(itSrc->enmPathStyle), strDst.c_str(), fFollowSymlinks));

            GuestFsObjData srcObjData;
            int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
            vrc = mSession->i_fsObjQueryInfo(strSrc, fFollowSymlinks, srcObjData, &vrcGuest);
            if (RT_FAILURE(vrc))
            {
                if (vrc == VERR_GSTCTL_GUEST_ERROR)
                    strErrorInfo = GuestBase::getErrorAsString(tr("Guest source lookup failed"),
                                                               GuestErrorInfo(GuestErrorInfo::Type_Fs, vrcGuest, strSrc.c_str()));
                else
                    strErrorInfo.printf(tr("Guest source lookup for \"%s\" failed: %Rrc"),
                                        strSrc.c_str(), vrc);
                break;
            }

            if (srcObjData.mType == FsObjType_Directory)
            {
                if (itSrc->enmType != FsObjType_Directory)
                {
                    strErrorInfo.printf(tr("Guest source is not a file: %s"), strSrc.c_str());
                    vrc = VERR_NOT_A_FILE;
                    break;
                }
            }
            else
            {
                if (itSrc->enmType != FsObjType_File)
                {
                    strErrorInfo.printf(tr("Guest source is not a directory: %s"), strSrc.c_str());
                    vrc = VERR_NOT_A_DIRECTORY;
                    break;
                }
            }

            FsList *pFsList = NULL;
            try
            {
                pFsList = new FsList(*this);
                vrc = pFsList->Init(strSrc, strDst, *itSrc);
                if (RT_SUCCESS(vrc))
                {
                    switch (itSrc->enmType)
                    {
                        case FsObjType_Directory:
                        {
                            vrc = pFsList->AddDirFromGuest(strSrc);
                            break;
                        }

                        case FsObjType_File:
                            /* The file name is already part of the actual list's source root (strSrc). */
                            break;

                        default:
                            LogRel2(("Guest Control: Warning: Unknown guest file system type %#x for source \"%s\", skipping\n",
                                     itSrc->enmType, strSrc.c_str()));
                            break;
                    }
                }

                if (RT_FAILURE(vrc))
                {
                    delete pFsList;
                    strErrorInfo.printf(tr("Error adding guest source \"%s\" to list: %Rrc"),
                                        strSrc.c_str(), vrc);
                    break;
                }
#ifdef DEBUG
                pFsList->DumpToLog();
#endif
                mVecLists.push_back(pFsList);
            }
            catch (std::bad_alloc &)
            {
                vrc = VERR_NO_MEMORY;
                break;
            }

            AssertPtr(pFsList);
            cOperations += (ULONG)pFsList->mVecEntries.size();

            itSrc++;
        }
    }

    if (RT_SUCCESS(vrc))
    {
        /* When there are no entries in the first source list, this means the source only contains a single file
         * (see \a mSrcRootAbs of FsList). So use \a mSrcRootAbs directly. */
        Utf8Str const &strFirstOp = mVecLists[0]->mVecEntries.size() > 0
                                  ? mVecLists[0]->mVecEntries[0]->strPath : mVecLists[0]->mSrcRootAbs;

        /* Now that we know how many objects we're handling, tweak the progress description so that it
         * reflects more accurately what the progress is actually doing. */
        if (cOperations > 1)
        {
            mDesc.printf(tr("Copying \"%s\" [and %zu %s] from guest to \"%s\" on the host ..."),
                         strFirstOp.c_str(), cOperations - 1, cOperations > 2 ? tr("others") : tr("other"), mDest.c_str());
        }
        else
            mDesc.printf(tr("Copying \"%s\" from guest to \"%s\" on the host ..."), strFirstOp.c_str(), mDest.c_str());

        hrc = pProgress->init(static_cast<IGuestSession*>(mSession), Bstr(mDesc).raw(),
                              TRUE /* aCancelable */, cOperations + 1 /* Number of operations */, Bstr(strFirstOp).raw());
    }
    else /* On error we go with an "empty" progress object when will be used for error handling. */
        hrc = pProgress->init(static_cast<IGuestSession*>(mSession), Bstr(mDesc).raw(),
                              TRUE /* aCancelable */, 1 /* cOperations */, Bstr(mDesc).raw());

    if (FAILED(hrc)) /* Progress object creation failed -- we're doomed. */
        return hrc;

    if (RT_FAILURE(vrc))
    {
        if (strErrorInfo.isEmpty())
            strErrorInfo.printf(tr("Failed with %Rrc"), vrc);
        setProgressErrorMsg(VBOX_E_IPRT_ERROR, strErrorInfo);
    }

    LogFlowFunc(("Returning %Rhrc (%Rrc)\n", hrc, vrc));
    return hrc;
}

/** @copydoc GuestSessionTask::Run */
int GuestSessionTaskCopyFrom::Run(void)
{
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(mSession);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    int vrc = VINF_SUCCESS;

    FsLists::const_iterator itList = mVecLists.begin();
    while (itList != mVecLists.end())
    {
        FsList *pList = *itList;
        AssertPtr(pList);

        LogFlowFunc(("List: srcRootAbs=%s, dstRootAbs=%s\n", pList->mSrcRootAbs.c_str(), pList->mDstRootAbs.c_str()));

        Utf8Str strSrcRootAbs = pList->mSrcRootAbs;
        Utf8Str strDstRootAbs = pList->mDstRootAbs;

        vrc = GuestPath::BuildDestinationPath(strSrcRootAbs, mSession->i_getGuestPathStyle() /* Source */,
                                              strDstRootAbs, PATH_STYLE_NATIVE  /* Dest */);
        if (RT_FAILURE(vrc))
        {
            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                Utf8StrFmt(tr("Building host destination root path \"%s\" failed: %Rrc"),
                                           strDstRootAbs.c_str(), vrc));
            break;
        }

        bool fCopyIntoExisting;
        bool fFollowSymlinks;

        if (pList->mSourceSpec.enmType == FsObjType_Directory)
        {
            fCopyIntoExisting = RT_BOOL(pList->mSourceSpec.fDirCopyFlags & DirectoryCopyFlag_CopyIntoExisting);
            fFollowSymlinks   = RT_BOOL(pList->mSourceSpec.fDirCopyFlags & DirectoryCopyFlag_FollowLinks);
        }
        else if (pList->mSourceSpec.enmType == FsObjType_File)
        {
            fCopyIntoExisting = !RT_BOOL(pList->mSourceSpec.fFileCopyFlags & FileCopyFlag_NoReplace);
            fFollowSymlinks   = RT_BOOL(pList->mSourceSpec.fFileCopyFlags & FileCopyFlag_FollowLinks);
        }
        else
            AssertFailedBreakStmt(vrc = VERR_NOT_IMPLEMENTED);

        uint32_t const  fDirMode          = 0700; /** @todo Play safe by default; implement ACLs. */
        uint32_t        fDirCreate        = 0;

        bool            fDstExists        = true;

        RTFSOBJINFO dstFsObjInfo;
        RT_ZERO(dstFsObjInfo);
        vrc = RTPathQueryInfoEx(strDstRootAbs.c_str(), &dstFsObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK /* fFlags */);
        if (RT_SUCCESS(vrc))
        {
            char szPathReal[RTPATH_MAX];
            vrc = RTPathReal(strDstRootAbs.c_str(), szPathReal, sizeof(szPathReal));
            if (RT_SUCCESS(vrc))
            {
                vrc = RTPathQueryInfoEx(szPathReal, &dstFsObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK /* fFlags */);
                if (RT_SUCCESS(vrc))
                {
                    LogRel2(("Guest Control: Host destination is a symbolic link \"%s\" -> \"%s\" (%s)\n",
                             strDstRootAbs.c_str(), szPathReal,
                             GuestBase::fsObjTypeToStr(GuestBase::fileModeToFsObjType(dstFsObjInfo.Attr.fMode))));
                }

                strDstRootAbs = szPathReal;
            }
        }
        else
        {
            if (   vrc == VERR_FILE_NOT_FOUND
                || vrc == VERR_PATH_NOT_FOUND)
            {
                fDstExists = false;
                vrc        = VINF_SUCCESS;
            }
            else
            {
                setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                    Utf8StrFmt(tr("Host path lookup for \"%s\" failed: %Rrc"), strDstRootAbs.c_str(), vrc));
                break;
            }
        }

        /* Create the root directory. */
        if (pList->mSourceSpec.enmType == FsObjType_Directory)
        {
            LogFlowFunc(("Directory: fDirCopyFlags=%#x, fCopyIntoExisting=%RTbool, fFollowSymlinks=%RTbool -> fDstExist=%RTbool (%s)\n",
                         pList->mSourceSpec.fDirCopyFlags, fCopyIntoExisting, fFollowSymlinks,
                         fDstExists, GuestBase::fsObjTypeToStr(GuestBase::fileModeToFsObjType(dstFsObjInfo.Attr.fMode))));

            if (fDstExists)
            {
                switch (dstFsObjInfo.Attr.fMode & RTFS_TYPE_MASK)
                {
                    case RTFS_TYPE_DIRECTORY:
                    {
                        if (!fCopyIntoExisting)
                        {
                            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                                Utf8StrFmt(tr("Host root directory \"%s\" already exists"), strDstRootAbs.c_str()));
                            vrc = VERR_ALREADY_EXISTS;
                            break;
                        }
                        break;
                    }

                    case RTFS_TYPE_FILE:
                    {
                        setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                            Utf8StrFmt(tr("Destination \"%s\" on the host already exists and is a file"), strDstRootAbs.c_str()));
                        vrc = VERR_IS_A_FILE;
                        break;
                    }

                    default:
                    {
                        setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                            Utf8StrFmt(tr("Unknown object type (%#x) on host for \"%s\""),
                                                       dstFsObjInfo.Attr.fMode & RTFS_TYPE_MASK, strDstRootAbs.c_str()));
                        vrc = VERR_NOT_SUPPORTED;
                        break;
                    }
                }
            }

            if (RT_FAILURE(vrc))
                break;

            /* Make sure the destination root directory exists. */
            if (pList->mSourceSpec.fDryRun == false)
            {
                vrc = directoryCreateOnHost(strDstRootAbs, fDirMode, 0 /* fCreate */, true /* fCanExist */);
                if (RT_FAILURE(vrc))
                    break;
            }

            AssertBreakStmt(pList->mSourceSpec.enmType == FsObjType_Directory, vrc = VERR_NOT_SUPPORTED);

            /* Walk the entries. */
            FsEntries::const_iterator itEntry = pList->mVecEntries.begin();
            while (itEntry != pList->mVecEntries.end())
            {
                FsEntry *pEntry = *itEntry;
                AssertPtr(pEntry);

                Utf8Str strSrcAbs = strSrcRootAbs;
                Utf8Str strDstAbs = strDstRootAbs;

                strSrcAbs += PATH_STYLE_SEP_STR(pList->mSourceSpec.enmPathStyle);
                strSrcAbs += pEntry->strPath;

                strDstAbs += PATH_STYLE_SEP_STR(PATH_STYLE_NATIVE);
                strDstAbs += pEntry->strPath;

                /* Clean up the final guest source path. */
                vrc = GuestPath::Translate(strSrcAbs, pList->mSourceSpec.enmPathStyle /* Source */,
                                           pList->mSourceSpec.enmPathStyle /* Dest */);
                if (RT_FAILURE(vrc))
                {
                    setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                        Utf8StrFmt(tr("Translating guest source path \"%s\" failed: %Rrc"),
                                                   strSrcAbs.c_str(), vrc));
                    break;
                }

                /* Translate the final host desitnation path. */
                vrc = GuestPath::Translate(strDstAbs, mSession->i_getGuestPathStyle() /* Source */, PATH_STYLE_NATIVE /* Dest */);
                if (RT_FAILURE(vrc))
                {
                    setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                        Utf8StrFmt(tr("Translating host destination path \"%s\" failed: %Rrc"),
                                                   strDstAbs.c_str(), vrc));
                    break;
                }

                mProgress->SetNextOperation(Bstr(strSrcAbs).raw(), 1);

                switch (pEntry->fMode & RTFS_TYPE_MASK)
                {
                    case RTFS_TYPE_DIRECTORY:
                        if (!pList->mSourceSpec.fDryRun)
                            vrc = directoryCreateOnHost(strDstAbs, fDirMode, fDirCreate, fCopyIntoExisting);
                        break;

                    case RTFS_TYPE_FILE:
                        RT_FALL_THROUGH();
                    case RTFS_TYPE_SYMLINK:
                        if (!pList->mSourceSpec.fDryRun)
                            vrc = fileCopyFromGuest(strSrcAbs, strDstAbs, pList->mSourceSpec.fFileCopyFlags);
                        break;

                    default:
                        AssertFailed(); /* Should never happen (we already have a filtered list). */
                        break;
                }

                if (RT_FAILURE(vrc))
                    break;

                ++itEntry;
            }
        }
        else if (pList->mSourceSpec.enmType == FsObjType_File)
        {
            LogFlowFunc(("File: fFileCopyFlags=%#x, fCopyIntoExisting=%RTbool, fFollowSymlinks=%RTbool -> fDstExist=%RTbool (%s)\n",
                         pList->mSourceSpec.fFileCopyFlags, fCopyIntoExisting, fFollowSymlinks,
                         fDstExists, GuestBase::fsObjTypeToStr(GuestBase::fileModeToFsObjType(dstFsObjInfo.Attr.fMode))));

            if (fDstExists)
            {
                switch (dstFsObjInfo.Attr.fMode & RTFS_TYPE_MASK)
                {
                    case RTFS_TYPE_DIRECTORY:
                    {
                        setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                            Utf8StrFmt(tr("Destination \"%s\" on the host already exists and is a directory"),
                                                       strDstRootAbs.c_str()));
                        vrc = VERR_IS_A_DIRECTORY;
                        break;
                    }

                    case RTFS_TYPE_FILE:
                    {
                        if (!fCopyIntoExisting)
                        {
                            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                                Utf8StrFmt(tr("Host file \"%s\" already exists"), strDstRootAbs.c_str()));
                            vrc = VERR_ALREADY_EXISTS;
                        }
                        break;
                    }

                    default:
                    {
                        /** @todo Resolve symlinks? */
                        setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                            Utf8StrFmt(tr("Unknown object type (%#x) on host for \"%s\""),
                                                       dstFsObjInfo.Attr.fMode & RTFS_TYPE_MASK, strDstRootAbs.c_str()));
                        vrc = VERR_NOT_SUPPORTED;
                        break;
                    }
                }
            }

            if (RT_SUCCESS(vrc))
            {
                /* Translate the final host destination file path. */
                vrc = GuestPath::Translate(strDstRootAbs,
                                           mSession->i_getGuestPathStyle() /* Dest */, PATH_STYLE_NATIVE /* Source */);
                if (RT_FAILURE(vrc))
                {
                    setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                        Utf8StrFmt(tr("Translating host destination path \"%s\" failed: %Rrc"),
                                                   strDstRootAbs.c_str(), vrc));
                    break;
                }

                if (!pList->mSourceSpec.fDryRun)
                    vrc = fileCopyFromGuest(strSrcRootAbs, strDstRootAbs, pList->mSourceSpec.fFileCopyFlags);
            }
        }
        else
            AssertFailedStmt(vrc = VERR_NOT_SUPPORTED);

        if (RT_FAILURE(vrc))
            break;

        ++itList;
    }

    if (RT_SUCCESS(vrc))
        vrc = setProgressSuccess();

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

GuestSessionTaskCopyTo::GuestSessionTaskCopyTo(GuestSession *pSession, GuestSessionFsSourceSet const &vecSrc,
                                               const Utf8Str &strDest)
    : GuestSessionCopyTask(pSession)
{
    m_strTaskName = "gctlCpyTo";

    mSources = vecSrc;
    mDest    = strDest;
}

GuestSessionTaskCopyTo::~GuestSessionTaskCopyTo(void)
{
}

/**
 * Initializes a copy-to-guest task.
 *
 * @returns HRESULT
 * @param   strTaskDesc         Friendly task description.
 */
HRESULT GuestSessionTaskCopyTo::Init(const Utf8Str &strTaskDesc)
{
    LogFlowFuncEnter();

    setTaskDesc(strTaskDesc);

    /* Create the progress object. */
    ComObjPtr<Progress> pProgress;
    HRESULT hrc = pProgress.createObject();
    if (FAILED(hrc))
        return hrc;

    mProgress = pProgress;

    int vrc = VINF_SUCCESS;

    ULONG cOperations = 0;
    Utf8Str strErrorInfo;

    /*
     * Note: We need to build up the file/directory here instead of GuestSessionTaskCopyTo::Run
     *       because the caller expects a ready-for-operation progress object on return.
     *       The progress object will have a variable operation count, based on the elements to
     *       be processed.
     */

    if (mSources.empty())
    {
        strErrorInfo.printf(tr("No host sources specified"));
        vrc = VERR_INVALID_PARAMETER;
    }
    else if (mDest.isEmpty())
    {
        strErrorInfo.printf(tr("Guest destination must not be empty"));
        vrc = VERR_INVALID_PARAMETER;
    }
    else
    {
        GuestSessionFsSourceSet::iterator itSrc = mSources.begin();
        while (itSrc != mSources.end())
        {
            Utf8Str strSrc = itSrc->strSource;
            Utf8Str strDst = mDest;

            bool    fFollowSymlinks;

            if (strSrc.isEmpty())
            {
                strErrorInfo.printf(tr("Host source entry must not be empty"));
                vrc = VERR_INVALID_PARAMETER;
                break;
            }

            if (itSrc->enmType == FsObjType_Directory)
            {
                fFollowSymlinks = itSrc->fDirCopyFlags & DirectoryCopyFlag_FollowLinks;
            }
            else
            {
                fFollowSymlinks = RT_BOOL(itSrc->fFileCopyFlags & FileCopyFlag_FollowLinks);
            }

            LogFlowFunc(("strSrc=%s (path style is %s), strDst=%s\n",
                         strSrc.c_str(), GuestBase::pathStyleToStr(itSrc->enmPathStyle), strDst.c_str()));

            RTFSOBJINFO srcFsObjInfo;
            vrc = RTPathQueryInfoEx(strSrc.c_str(), &srcFsObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK /* fFlags */);
            if (RT_FAILURE(vrc))
            {
                strErrorInfo.printf(tr("No such host file/directory: %s"), strSrc.c_str());
                break;
            }

            switch (srcFsObjInfo.Attr.fMode & RTFS_TYPE_MASK)
            {
                case RTFS_TYPE_DIRECTORY:
                {
                    if (itSrc->enmType != FsObjType_Directory)
                    {
                        strErrorInfo.printf(tr("Host source \"%s\" is not a file (is a directory)"), strSrc.c_str());
                        vrc = VERR_NOT_A_FILE;
                    }
                    break;
                }

                case RTFS_TYPE_FILE:
                {
                    if (itSrc->enmType == FsObjType_Directory)
                    {
                        strErrorInfo.printf(tr("Host source \"%s\" is not a directory (is a file)"), strSrc.c_str());
                        vrc = VERR_NOT_A_DIRECTORY;
                    }
                    break;
                }

                case RTFS_TYPE_SYMLINK:
                {
                    if (!fFollowSymlinks)
                    {
                        strErrorInfo.printf(tr("Host source \"%s\" is a symbolic link"), strSrc.c_str());
                        vrc = VERR_IS_A_SYMLINK;
                        break;
                    }

                    char szPathReal[RTPATH_MAX];
                    vrc = RTPathReal(strSrc.c_str(), szPathReal, sizeof(szPathReal));
                    if (RT_SUCCESS(vrc))
                    {
                        vrc = RTPathQueryInfoEx(szPathReal, &srcFsObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
                        if (RT_SUCCESS(vrc))
                        {
                            LogRel2(("Guest Control: Host source is a symbolic link \"%s\" -> \"%s\" (%s)\n",
                                     strSrc.c_str(), szPathReal,
                                     GuestBase::fsObjTypeToStr(GuestBase::fileModeToFsObjType(srcFsObjInfo.Attr.fMode))));

                            /* We want to keep the symbolic link name of the source instead of the target pointing to,
                             * so don't touch the source's name here. */
                            itSrc->enmType = GuestBase::fileModeToFsObjType(srcFsObjInfo.Attr.fMode);
                        }
                        else
                        {
                            strErrorInfo.printf(tr("Querying symbolic link info for host source \"%s\" failed"), strSrc.c_str());
                            break;
                        }
                    }
                    else
                    {
                        strErrorInfo.printf(tr("Resolving symbolic link for host source \"%s\" failed"), strSrc.c_str());
                        break;
                    }
                    break;
                }

                default:
                    LogRel2(("Guest Control: Warning: Unknown host file system type %#x for source \"%s\", skipping\n",
                             srcFsObjInfo.Attr.fMode & RTFS_TYPE_MASK, strSrc.c_str()));
                    break;
            }

            if (RT_FAILURE(vrc))
                break;

            FsList *pFsList = NULL;
            try
            {
                pFsList = new FsList(*this);
                vrc = pFsList->Init(strSrc, strDst, *itSrc);
                if (RT_SUCCESS(vrc))
                {
                    switch (itSrc->enmType)
                    {
                        case FsObjType_Directory:
                        {
                            char szPathReal[RTPATH_MAX];
                            RTDIRENTRYEX DirEntry;
                            vrc = pFsList->AddDirFromHost(strSrc /* strPath */, "" /* strSubDir */,
                                                          szPathReal, sizeof(szPathReal), &DirEntry);
                            break;
                        }

                        case FsObjType_File:
                            /* The file name is already part of the actual list's source root (strSrc). */
                            break;

                        case FsObjType_Symlink:
                            AssertFailed(); /* Should never get here, as we do the resolving above. */
                            break;

                        default:
                            LogRel2(("Guest Control: Warning: Unknown source type %#x for host source \"%s\", skipping\n",
                                     itSrc->enmType, strSrc.c_str()));
                            break;
                    }
                }

                if (RT_FAILURE(vrc))
                {
                    delete pFsList;
                    strErrorInfo.printf(tr("Error adding host source \"%s\" to list: %Rrc"),
                                        strSrc.c_str(), vrc);
                    break;
                }
#ifdef DEBUG
                pFsList->DumpToLog();
#endif
                mVecLists.push_back(pFsList);
            }
            catch (std::bad_alloc &)
            {
                vrc = VERR_NO_MEMORY;
                break;
            }

            AssertPtr(pFsList);
            cOperations += (ULONG)pFsList->mVecEntries.size();

            itSrc++;
        }
    }

    if (RT_SUCCESS(vrc))
    {
        /* When there are no entries in the first source list, this means the source only contains a single file
         * (see \a mSrcRootAbs of FsList). So use \a mSrcRootAbs directly. */
        Utf8Str const &strFirstOp = mVecLists[0]->mVecEntries.size() > 0
                                  ? mVecLists[0]->mVecEntries[0]->strPath : mVecLists[0]->mSrcRootAbs;

        /* Now that we know how many objects we're handling, tweak the progress description so that it
         * reflects more accurately what the progress is actually doing. */
        if (cOperations > 1)
        {
            mDesc.printf(tr("Copying \"%s\" [and %zu %s] from host to \"%s\" on the guest ..."),
                         strFirstOp.c_str(), cOperations - 1, cOperations > 2 ? tr("others") : tr("other"), mDest.c_str());
        }
        else
            mDesc.printf(tr("Copying \"%s\" from host to \"%s\" on the guest ..."), strFirstOp.c_str(), mDest.c_str());

        hrc = pProgress->init(static_cast<IGuestSession*>(mSession), Bstr(mDesc).raw(),
                              TRUE /* aCancelable */, cOperations + 1/* Number of operations */,
                              Bstr(strFirstOp).raw());
    }
    else /* On error we go with an "empty" progress object when will be used for error handling. */
        hrc = pProgress->init(static_cast<IGuestSession*>(mSession), Bstr(mDesc).raw(),
                              TRUE /* aCancelable */, 1 /* cOperations */, Bstr(mDesc).raw());

    if (FAILED(hrc)) /* Progress object creation failed -- we're doomed. */
        return hrc;

    if (RT_FAILURE(vrc))
    {
        if (strErrorInfo.isEmpty())
            strErrorInfo.printf(tr("Failed with %Rrc"), vrc);
        setProgressErrorMsg(VBOX_E_IPRT_ERROR, strErrorInfo);
    }

    LogFlowFunc(("Returning %Rhrc (%Rrc)\n", hrc, vrc));
    return hrc;
}

/** @copydoc GuestSessionTask::Run */
int GuestSessionTaskCopyTo::Run(void)
{
    LogFlowThisFuncEnter();

    AutoCaller autoCaller(mSession);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    int vrc = VINF_SUCCESS;

    FsLists::const_iterator itList = mVecLists.begin();
    while (itList != mVecLists.end())
    {
        FsList *pList = *itList;
        AssertPtr(pList);

        LogFlowFunc(("List: srcRootAbs=%s, dstRootAbs=%s\n", pList->mSrcRootAbs.c_str(), pList->mDstRootAbs.c_str()));

        Utf8Str strSrcRootAbs = pList->mSrcRootAbs;
        Utf8Str strDstRootAbs = pList->mDstRootAbs;

        vrc = GuestPath::BuildDestinationPath(strSrcRootAbs, PATH_STYLE_NATIVE /* Source */,
                                              strDstRootAbs, mSession->i_getGuestPathStyle()  /* Dest */);
        if (RT_FAILURE(vrc))
        {
            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                Utf8StrFmt(tr("Building guest destination root path \"%s\" failed: %Rrc"),
                                           strDstRootAbs.c_str(), vrc));
            break;
        }

        bool fCopyIntoExisting;
        bool fFollowSymlinks;

        if (pList->mSourceSpec.enmType == FsObjType_Directory)
        {
            fCopyIntoExisting = RT_BOOL(pList->mSourceSpec.fDirCopyFlags & DirectoryCopyFlag_CopyIntoExisting);
            fFollowSymlinks   = RT_BOOL(pList->mSourceSpec.fDirCopyFlags & DirectoryCopyFlag_FollowLinks);
        }
        else if (pList->mSourceSpec.enmType == FsObjType_File)
        {
            fCopyIntoExisting = !RT_BOOL(pList->mSourceSpec.fFileCopyFlags & FileCopyFlag_NoReplace);
            fFollowSymlinks   = RT_BOOL(pList->mSourceSpec.fFileCopyFlags & FileCopyFlag_FollowLinks);
        }
        else
            AssertFailedBreakStmt(vrc = VERR_NOT_IMPLEMENTED);

        uint32_t const fDirMode          = 0700; /** @todo Play safe by default; implement ACLs. */

        bool           fDstExists        = true;

        GuestFsObjData dstObjData;
        int vrcGuest;
        vrc = mSession->i_fsObjQueryInfo(strDstRootAbs, fFollowSymlinks, dstObjData, &vrcGuest);
        if (RT_FAILURE(vrc))
        {
            if (vrc == VERR_GSTCTL_GUEST_ERROR)
            {
                switch (vrcGuest)
                {
                    case VERR_PATH_NOT_FOUND:
                        RT_FALL_THROUGH();
                    case VERR_FILE_NOT_FOUND:
                    {
                        fDstExists = false;
                        vrc        = VINF_SUCCESS;
                        break;
                    }
                    default:
                        setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                            Utf8StrFmt(tr("Querying information on guest for \"%s\" failed: %Rrc"),
                                            strDstRootAbs.c_str(), vrcGuest));
                        break;
                }
            }
            else
            {
                setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                    Utf8StrFmt(tr("Querying information on guest for \"%s\" failed: %Rrc"),
                                               strDstRootAbs.c_str(), vrc));
                break;
            }
        }

        if (pList->mSourceSpec.enmType == FsObjType_Directory)
        {
            LogFlowFunc(("Directory: fDirCopyFlags=%#x, fCopyIntoExisting=%RTbool, fFollowSymlinks=%RTbool -> fDstExist=%RTbool (%s)\n",
                         pList->mSourceSpec.fDirCopyFlags, fCopyIntoExisting, fFollowSymlinks,
                         fDstExists, GuestBase::fsObjTypeToStr(dstObjData.mType)));

            if (fDstExists)
            {
                switch (dstObjData.mType)
                {
                    case FsObjType_Directory:
                    {
                        if (!fCopyIntoExisting)
                        {
                            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                                Utf8StrFmt(tr("Guest root directory \"%s\" already exists"),
                                                           strDstRootAbs.c_str()));
                            vrc = VERR_ALREADY_EXISTS;
                        }
                        break;
                    }

                    case FsObjType_File:
                    {
                        setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                            Utf8StrFmt(tr("Destination \"%s\" on guest already exists and is a file"),
                                                       strDstRootAbs.c_str()));
                        vrc = VERR_IS_A_FILE;
                        break;
                    }

                    case FsObjType_Symlink:
                        /** @todo Resolve symlinks? */
                        break;

                    default:
                        setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                            Utf8StrFmt(tr("Unknown object type (%#x) on guest for \"%s\""),
                                                       dstObjData.mType, strDstRootAbs.c_str()));
                        vrc = VERR_NOT_SUPPORTED;
                        break;
                }
            }

            if (RT_FAILURE(vrc))
                break;

            /* Make sure the destination root directory exists. */
            if (pList->mSourceSpec.fDryRun == false)
            {
                vrc = directoryCreateOnGuest(strDstRootAbs, fDirMode, DirectoryCreateFlag_None,
                                             fFollowSymlinks, fCopyIntoExisting);
                if (RT_FAILURE(vrc))
                    break;
            }

            /* Walk the entries. */
            FsEntries::const_iterator itEntry = pList->mVecEntries.begin();
            while (   RT_SUCCESS(vrc)
                   && itEntry != pList->mVecEntries.end())
            {
                FsEntry *pEntry = *itEntry;
                AssertPtr(pEntry);

                Utf8Str strSrcAbs = strSrcRootAbs;
                Utf8Str strDstAbs = strDstRootAbs;

                strSrcAbs += PATH_STYLE_SEP_STR(PATH_STYLE_NATIVE);
                strSrcAbs += pEntry->strPath;

                strDstAbs += PATH_STYLE_SEP_STR(mSession->i_getGuestPathStyle());
                strDstAbs += pEntry->strPath;

                /* Clean up the final host source path. */
                vrc = GuestPath::Translate(strSrcAbs, pList->mSourceSpec.enmPathStyle /* Source */,
                                           pList->mSourceSpec.enmPathStyle /* Dest */);
                if (RT_FAILURE(vrc))
                {
                    setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                        Utf8StrFmt(tr("Translating host source path\"%s\" failed: %Rrc"),
                                                   strSrcAbs.c_str(), vrc));
                    break;
                }

                /* Translate final guest destination path. */
                vrc = GuestPath::Translate(strDstAbs,
                                           PATH_STYLE_NATIVE /* Source */,  mSession->i_getGuestPathStyle() /* Dest */);
                if (RT_FAILURE(vrc))
                {
                    setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                        Utf8StrFmt(tr("Translating guest destination path \"%s\" failed: %Rrc"),
                                                   strDstAbs.c_str(), vrc));
                    break;
                }

                mProgress->SetNextOperation(Bstr(strSrcAbs).raw(), 1);

                switch (pEntry->fMode & RTFS_TYPE_MASK)
                {
                    case RTFS_TYPE_DIRECTORY:
                    {
                        LogRel2(("Guest Control: Copying directory \"%s\" from host to \"%s\" on guest ...\n",
                                 strSrcAbs.c_str(), strDstAbs.c_str()));
                        if (!pList->mSourceSpec.fDryRun)
                            vrc = directoryCreateOnGuest(strDstAbs, fDirMode, DirectoryCreateFlag_None,
                                                         fFollowSymlinks, fCopyIntoExisting);
                        break;
                    }

                    case RTFS_TYPE_FILE:
                    {
                        if (!pList->mSourceSpec.fDryRun)
                            vrc = fileCopyToGuest(strSrcAbs, strDstAbs, pList->mSourceSpec.fFileCopyFlags);
                        break;
                    }

                    default:
                        LogRel2(("Guest Control: Warning: Host file system type 0x%x for \"%s\" is not supported, skipping\n",
                                 pEntry->fMode & RTFS_TYPE_MASK, strSrcAbs.c_str()));
                        break;
                }

                if (RT_FAILURE(vrc))
                    break;

                ++itEntry;
            }
        }
        else if (pList->mSourceSpec.enmType == FsObjType_File)
        {
            LogFlowFunc(("File: fFileCopyFlags=%#x, fCopyIntoExisting=%RTbool, fFollowSymlinks=%RTbool -> fDstExist=%RTbool (%s)\n",
                         pList->mSourceSpec.fFileCopyFlags, fCopyIntoExisting, fFollowSymlinks,
                         fDstExists, GuestBase::fsObjTypeToStr(dstObjData.mType)));

            if (fDstExists)
            {
                switch (dstObjData.mType)
                {
                    case FsObjType_Directory:
                    {
                        setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                            Utf8StrFmt(tr("Destination \"%s\" on the guest already exists and is a directory"),
                                                       strDstRootAbs.c_str()));
                        vrc = VERR_IS_A_DIRECTORY;
                        break;
                    }

                    case FsObjType_File:
                    {
                        if (!fCopyIntoExisting)
                        {
                            setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                                Utf8StrFmt(tr("Guest file \"%s\" already exists"), strDstRootAbs.c_str()));
                            vrc = VERR_ALREADY_EXISTS;
                        }
                        break;
                    }

                    default:
                    {
                        setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                            Utf8StrFmt(tr("Unsupported guest file system type (%#x) for \"%s\""),
                                                       dstObjData.mType, strDstRootAbs.c_str()));
                        vrc = VERR_NOT_SUPPORTED;
                        break;
                    }
                }
            }

            if (RT_SUCCESS(vrc))
            {
                /* Translate the final guest destination file path. */
                vrc = GuestPath::Translate(strDstRootAbs,
                                           PATH_STYLE_NATIVE /* Source */,  mSession->i_getGuestPathStyle() /* Dest */);
                if (RT_FAILURE(vrc))
                {
                    setProgressErrorMsg(VBOX_E_IPRT_ERROR,
                                        Utf8StrFmt(tr("Translating guest destination path \"%s\" failed: %Rrc"),
                                                   strDstRootAbs.c_str(), vrc));
                    break;
                }

                if (!pList->mSourceSpec.fDryRun)
                    vrc = fileCopyToGuest(strSrcRootAbs, strDstRootAbs, pList->mSourceSpec.fFileCopyFlags);
            }
        }
        else
            AssertFailedStmt(vrc = VERR_NOT_SUPPORTED);

        if (RT_FAILURE(vrc))
            break;

        ++itList;
    }

    if (RT_SUCCESS(vrc))
        vrc = setProgressSuccess();

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

UpdateAdditionsProcess::~UpdateAdditionsProcess()
{
    /* Log any remainders if not done yet. */
    if (mLineStdOut.isNotEmpty())
        LogRel(("Guest Additions Update: %s\n", mLineStdOut.c_str()));
    if (mLineStdErr.isNotEmpty())
        LogRel(("Guest Additions Update: %s\n", mLineStdErr.c_str()));
}

/**
 * Callback implementation to output guest update process stdout / stderr output to the release log.
 * Only complete lines will be logged for cosmetic reasons.
 *
 * @returns VBox status code.
 * @param   uHandle             Process output handle.
 * @param   pbData              Pointer to data.
 * @param   cbData              Size (in bytes) of \a pbData.
 *
 * @note    Only stdout (handle ID 1) and stderr (handle ID 2) are implemented.
 */
int UpdateAdditionsProcess::onOutputCallback(uint32_t uHandle, const BYTE *pbData, size_t cbData)
{
    AssertReturn(RTStrValidateEncodingEx((const char *)pbData, cbData,   RTSTR_VALIDATE_ENCODING_EXACT_LENGTH
                                                                       | RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED),
                 VERR_INVALID_PARAMETER);

    Utf8Str *pstrLine = NULL;

    switch (uHandle)
    {
        case GUEST_PROC_OUT_H_STDOUT:
            pstrLine = &mLineStdOut;
            break;

        case GUEST_PROC_OUT_H_STDERR:
            pstrLine = &mLineStdErr;
            break;

        default:
            /* Ignore. */
            break;
    }

    int vrc = VINF_SUCCESS;

    if (pstrLine)
    {
        const char *cch = (const char *)pbData;
        while (cbData)
        {
            if (*cch == '\n')
                break;
            pstrLine->append(*cch);
            cch++;
            cbData--;
        }

        if (*cch == '\n')
        {
            LogRel(("Guest Additions Update: %s\n", pstrLine->c_str()));
            pstrLine->setNull();
            cch++;
        }
    }

    return vrc;
}

GuestSessionTaskUpdateAdditions::GuestSessionTaskUpdateAdditions(GuestSession *pSession,
                                                                 const Utf8Str &strSource,
                                                                 const ProcessArguments &aArguments,
                                                                 uint32_t fFlags)
                                                                 : GuestSessionTask(pSession)
{
    m_strTaskName = "gctlUpGA";

    mSource    = strSource;
    mArguments = aArguments;
    mFlags     = fFlags;
}

GuestSessionTaskUpdateAdditions::~GuestSessionTaskUpdateAdditions(void)
{

}

/**
 * Adds arguments to existing process arguments.
 * Identical / already existing arguments will be filtered out.
 *
 * @returns VBox status code.
 * @param   aArgumentsDest      Destination to add arguments to.
 * @param   aArgumentsSource    Arguments to add.
 */
int GuestSessionTaskUpdateAdditions::addProcessArguments(ProcessArguments &aArgumentsDest, const ProcessArguments &aArgumentsSource)
{
    try
    {
        /* Filter out arguments which already are in the destination to
         * not end up having them specified twice. Not the fastest method on the
         * planet but does the job. */
        ProcessArguments::const_iterator itSource = aArgumentsSource.begin();
        while (itSource != aArgumentsSource.end())
        {
            bool fFound = false;
            ProcessArguments::iterator itDest = aArgumentsDest.begin();
            while (itDest != aArgumentsDest.end())
            {
                if ((*itDest).equalsIgnoreCase((*itSource)))
                {
                    fFound = true;
                    break;
                }
                ++itDest;
            }

            if (!fFound)
                aArgumentsDest.push_back((*itSource));

            ++itSource;
        }
    }
    catch(std::bad_alloc &)
    {
        return VERR_NO_MEMORY;
    }

    return VINF_SUCCESS;
}

/**
 * Helper function to copy a file from a VISO to the guest.
 *
 * @returns VBox status code.
 * @param   pSession            Guest session to use.
 * @param   hVfsIso             VISO handle to use.
 * @param   strFileSrc          Source file path on VISO to copy.
 * @param   strFileDst          Destination file path on guest.
 * @param   fOptional           When set to \c true, the file is optional, i.e. can be skipped
 *                              when not found, \c false if not.
 */
int GuestSessionTaskUpdateAdditions::copyFileToGuest(GuestSession *pSession, RTVFS hVfsIso,
                                                     Utf8Str const &strFileSrc, const Utf8Str &strFileDst, bool fOptional)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);
    AssertReturn(hVfsIso != NIL_RTVFS, VERR_INVALID_POINTER);

    RTVFSFILE hVfsFile = NIL_RTVFSFILE;
    int vrc = RTVfsFileOpen(hVfsIso, strFileSrc.c_str(), RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_WRITE, &hVfsFile);
    if (RT_SUCCESS(vrc))
    {
        uint64_t cbSrcSize = 0;
        vrc = RTVfsFileQuerySize(hVfsFile, &cbSrcSize);
        if (RT_SUCCESS(vrc))
        {
            LogRel(("Guest Additions Update: Copying installer file \"%s\" to \"%s\" on guest ...\n",
                    strFileSrc.c_str(), strFileDst.c_str()));

            GuestFileOpenInfo dstOpenInfo;
            dstOpenInfo.mFilename    = strFileDst;
            dstOpenInfo.mOpenAction  = FileOpenAction_CreateOrReplace;
            dstOpenInfo.mAccessMode  = FileAccessMode_WriteOnly;
            dstOpenInfo.mSharingMode = FileSharingMode_All; /** @todo Use _Read when implemented. */

            ComObjPtr<GuestFile> dstFile;
            int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
            vrc = mSession->i_fileOpen(dstOpenInfo, dstFile, &vrcGuest);
            if (RT_FAILURE(vrc))
            {
                switch (vrc)
                {
                    case VERR_GSTCTL_GUEST_ERROR:
                        setUpdateErrorMsg(VBOX_E_GSTCTL_GUEST_ERROR,
                                          GuestFile::i_guestErrorToString(vrcGuest, strFileDst.c_str()));
                        break;

                    default:
                        setUpdateErrorMsg(VBOX_E_GSTCTL_GUEST_ERROR,
                                          Utf8StrFmt(tr("Guest file \"%s\" could not be opened: %Rrc"),
                                                     strFileDst.c_str(), vrc));
                        break;
                }
            }
            else
            {
                vrc = fileCopyToGuestInner(strFileSrc, hVfsFile, strFileDst, dstFile, FileCopyFlag_None, 0 /*offCopy*/, cbSrcSize);

                int vrc2 = fileClose(dstFile);
                if (RT_SUCCESS(vrc))
                    vrc = vrc2;
            }
        }

        RTVfsFileRelease(hVfsFile);
    }
    else if (fOptional)
        vrc = VINF_SUCCESS;

    return vrc;
}

/**
 * Sets an update error message to the current progress object + logs to release log.
 *
 * @returns Returns \a hrc for convenience.
 * @param   hrc                 Progress operation result to set.
 * @param   strMsg              Message to set.
 */
HRESULT GuestSessionTaskUpdateAdditions::setUpdateErrorMsg(HRESULT hrc, const Utf8Str &strMsg)
{
    Utf8Str const strLog = "Guest Additions Update failed: " + strMsg;
    LogRel(("%s\n", strLog.c_str()));
    return GuestSessionTask::setProgressErrorMsg(hrc, strLog);
}

/**
 * Sets an update error message to the current progress object + logs to release log.
 *
 * @returns Returns \a hrc for convenience.
 * @param   hrc                 Progress operation result to set.
 * @param   strMsg              Message to set.
 * @param   guestErrorInfo      Guest error info to use.
 */
HRESULT GuestSessionTaskUpdateAdditions::setUpdateErrorMsg(HRESULT hrc, const Utf8Str &strMsg, const GuestErrorInfo &guestErrorInfo)
{
    Utf8Str const strLog = strMsg + Utf8Str(": ") + GuestBase::getErrorAsString(guestErrorInfo);
    return GuestSessionTaskUpdateAdditions::setProgressErrorMsg(hrc, strLog);
}

/**
 * Helper function to run (start) a file on the guest.
 *
 * @returns VBox status code.
 * @param   pSession            Guest session to use.
 * @param   procInfo            Guest process startup info to use.
 * @param   fSilent             Whether to set progress into failure state in case of error.
 */
int GuestSessionTaskUpdateAdditions::runFileOnGuest(GuestSession *pSession, GuestProcessStartupInfo &procInfo, bool fSilent)
{
    AssertPtrReturn(pSession, VERR_INVALID_POINTER);

#ifndef VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT
    RT_NOREF(procInfo, fSilent);
    return VERR_NOT_SUPPORTED;
#else
    LogRel(("Guest Additions Update: Running \"%s\" ...\n", procInfo.mName.c_str()));

    UpdateAdditionsProcess guestProc;
    int vrcGuest = VERR_IPE_UNINITIALIZED_STATUS;
    int vrc = guestProc.init(pSession, procInfo, false /* Async */, &vrcGuest);
    if (RT_SUCCESS(vrc))
    {
        if (RT_SUCCESS(vrcGuest))
        {
            vrc = guestProc.wait(&vrcGuest);
            if (RT_SUCCESS(vrc))
                vrc = guestProc.getTerminationStatus();
            else if (vrc == VERR_NOT_FOUND)
                /** @todo Linux Guest Additions terminate VBoxService when updating (via uninstall.sh),
                 *        which in turn terminates the Guest Control session this updater task was relying on.
                 *        This leads into a VERR_NOT_FOUND error, as the Guest Session is not around anymore.
                 *        Fend this off for now, but needs a clean(er) solution long-term. See @bugref{10776}. */
                vrc = VINF_SUCCESS;
        }
    }

    if (   RT_FAILURE(vrc)
        && !fSilent)
    {
        Utf8Str cmdLine;
        for (size_t iArg = 0; iArg < procInfo.mArguments.size(); iArg++)
        {
            cmdLine.append(procInfo.mArguments.at(iArg));
            if (iArg < procInfo.mArguments.size() - 1)
                cmdLine.append(" ");
        }

        switch (vrc)
        {
            case VERR_GSTCTL_PROCESS_EXIT_CODE:
            {
                int32_t iExitCode;
                vrc = guestProc.getTerminationStatus(&iExitCode);
                Assert(vrc == VERR_GSTCTL_PROCESS_EXIT_CODE);
                setUpdateErrorMsg(VBOX_E_GSTCTL_GUEST_ERROR,
                                  Utf8StrFmt(tr("Running update file \"%s\" on guest failed with exit code %d"),
                                             cmdLine.c_str(), iExitCode));
                break;

            }
            case VERR_GSTCTL_GUEST_ERROR:
                setUpdateErrorMsg(VBOX_E_GSTCTL_GUEST_ERROR, tr("Running update file on guest failed"),
                                  GuestErrorInfo(GuestErrorInfo::Type_Process, vrcGuest, cmdLine.c_str()));
                break;

            case VERR_INVALID_STATE: /** @todo Special guest control vrc needed! */
                setUpdateErrorMsg(VBOX_E_GSTCTL_GUEST_ERROR,
                                  Utf8StrFmt(tr("Update file \"%s\" reported invalid running state"),
                                             procInfo.mExecutable.c_str()));
                break;

            default:
                setUpdateErrorMsg(VBOX_E_GSTCTL_GUEST_ERROR,
                                  Utf8StrFmt(tr("Error while running update command \"%s\" on guest: %Rrc"),
                                             cmdLine.c_str(), vrc));
                break;
        }
    }

    return vrc;
#endif /* VBOX_WITH_GSTCTL_TOOLBOX_SUPPORT */
}

/**
 * Helper function which checks Guest Additions installation status.
 *
 * @returns IPRT status code.
 * @param   pSession    Guest session to use.
 * @param   osType      Guest type.
 */
int GuestSessionTaskUpdateAdditions::checkGuestAdditionsStatus(GuestSession *pSession, eOSType osType)
{
    int vrc = VINF_SUCCESS;
    HRESULT hrc;

    if (osType == eOSType_Linux)
    {
        const Utf8Str ksStatusScript = Utf8Str("/sbin/rcvboxadd");

        /* Check if Guest Additions kernel modules were loaded. */
        GuestProcessStartupInfo procInfo;
        procInfo.mName = "Kernel modules status check";
        procInfo.mFlags = ProcessCreateFlag_None;
        procInfo.mExecutable = Utf8Str("/bin/sh");;
        procInfo.mArguments.push_back(procInfo.mExecutable); /* Set argv0. */
        procInfo.mArguments.push_back(ksStatusScript);
        procInfo.mArguments.push_back("status-kernel");

        vrc = runFileOnGuest(pSession, procInfo, true /* fSilent */);
        if (RT_SUCCESS(vrc))
        {
            /* Replace the last argument with corresponding value and check
             * if Guest Additions user services were started. */
            procInfo.mName = "User services status check";
            procInfo.mArguments.pop_back();
            procInfo.mArguments.push_back("status-user");

            vrc = runFileOnGuest(pSession, procInfo, true /* fSilent */);
            if (RT_FAILURE(vrc))
                hrc = setUpdateErrorMsg(VBOX_E_GSTCTL_GUEST_ERROR,
                                        Utf8StrFmt(tr("Files were installed, but user services were not reloaded automatically. "
                                                      "Please consider rebooting the guest")));
        }
        else
            hrc = setUpdateErrorMsg(VBOX_E_GSTCTL_GUEST_ERROR,
                                    Utf8StrFmt(tr("Files were installed, but kernel modules were not reloaded automatically. "
                                                  "Please consider rebooting the guest")));
    }

    return vrc;
}

/**
 * Helper function which waits until Guest Additions services started.
 *
 * Newly created guest session needs to be closed by caller.
 *
 * @returns 0 on success or VERR_TIMEOUT if guest services were not
 *          started on time.
 * @param   pGuest          Guest interface to use.
 * @param   osType          Guest type.
 * @param   pNewSession     Output parameter for newly established guest type.
 */
int GuestSessionTaskUpdateAdditions::waitForGuestSession(ComObjPtr<Guest> pGuest, eOSType osType,
                                                         ComObjPtr<GuestSession> &pNewSession)
{
    int vrc                         = VERR_GSTCTL_GUEST_ERROR;
    int vrcRet                      = VERR_TIMEOUT;

    uint64_t tsStart                = RTTimeSystemMilliTS();
    const uint64_t cMsTimeout       = 10 * RT_MS_1MIN;

    AssertReturn(!pGuest.isNull(), VERR_TIMEOUT);

    do
    {
        GuestCredentials        guestCreds;
        GuestSessionStartupInfo startupInfo;

        startupInfo.mName           = "Guest Additions connection check";
        startupInfo.mOpenTimeoutMS  = 100;

        vrc = pGuest->i_sessionCreate(startupInfo, guestCreds, pNewSession);
        if (RT_SUCCESS(vrc))
        {
            Assert(!pNewSession.isNull());

            int vrcGuest = VERR_GSTCTL_GUEST_ERROR; /* unused. */
            vrc = pNewSession->i_startSession(&vrcGuest);
            if (RT_SUCCESS(vrc))
            {
                /* Wait for VBoxService to start. */
                GuestSessionWaitResult_T enmWaitResult = GuestSessionWaitResult_None;
                int vrcGuest2 = VINF_SUCCESS; /* unused. */
                vrc = pNewSession->i_waitFor(GuestSessionWaitForFlag_Start, 100 /* timeout, ms */, enmWaitResult, &vrcGuest2);
                if (RT_SUCCESS(vrc))
                {
                    /* Make sure Guest Additions were reloaded on the guest side. */
                    vrc = checkGuestAdditionsStatus(pNewSession, osType);
                    if (RT_SUCCESS(vrc))
                        LogRel(("Guest Additions Update: Guest Additions were successfully reloaded after installation\n"));
                    else
                        LogRel(("Guest Additions Update: Guest Additions were failed to reload after installation, please consider rebooting the guest\n"));

                    vrcRet = VINF_SUCCESS;
                    break;
                }
            }

            vrc = pNewSession->Close();
        }

        RTThreadSleep(100);

    } while ((RTTimeSystemMilliTS() - tsStart) < cMsTimeout);

    return vrcRet;
}

/**
 * Helper function which retrieves guest platform architecture information.
 *
 * @returns Platform architecture type or PlatformArchitecture_None if
 *          architecture information cannot be retrieved.
 */
PlatformArchitecture_T GuestSessionTaskUpdateAdditions::getPlatformArch(void)
{
    HRESULT hrc;
    PlatformArchitecture_T enmArch = PlatformArchitecture_None;

    ComObjPtr<GuestSession> pSession = mSession;
    Assert(!pSession.isNull());

    ComObjPtr<Guest> pGuest(pSession->i_getParent());
    Assert(!pGuest.isNull());

    ComObjPtr<Console> pConsole = pGuest->i_getConsole();
    Assert(!pConsole.isNull());

    const ComPtr<IMachine> pMachine = pConsole->i_machine();
    Assert(!pMachine.isNull());

    ComPtr<IPlatform> pPlatform;

    hrc = pMachine->COMGETTER(Platform)(pPlatform.asOutParam());
    AssertComRCReturn(hrc, PlatformArchitecture_None);

    hrc = pPlatform->COMGETTER(Architecture)(&enmArch);
    AssertComRCReturn(hrc, PlatformArchitecture_None);

    return enmArch;
}

/** @copydoc GuestSessionTask::Run */
int GuestSessionTaskUpdateAdditions::Run(void)
{
    LogFlowThisFuncEnter();

    ComObjPtr<GuestSession> pSession = mSession;
    Assert(!pSession.isNull());

    AutoCaller autoCaller(pSession);
    if (FAILED(autoCaller.hrc())) return autoCaller.hrc();

    int vrc = setProgress(10);
    if (RT_FAILURE(vrc))
        return vrc;

    HRESULT hrc = S_OK;

    LogRel(("Guest Additions Update: Automatic update started, using \"%s\"\n", mSource.c_str()));

    ComObjPtr<Guest> pGuest(mSession->i_getParent());
#if 0
    /*
     * Wait for the guest being ready within 30 seconds.
     */
    AdditionsRunLevelType_T addsRunLevel;
    uint64_t tsStart = RTTimeSystemMilliTS();
    while (   SUCCEEDED(hrc = pGuest->COMGETTER(AdditionsRunLevel)(&addsRunLevel))
           && (    addsRunLevel != AdditionsRunLevelType_Userland
                && addsRunLevel != AdditionsRunLevelType_Desktop))
    {
        if ((RTTimeSystemMilliTS() - tsStart) > GSTCTL_DEFAULT_TIMEOUT_MS)
        {
            vrc = VERR_TIMEOUT;
            break;
        }

        RTThreadSleep(100); /* Wait a bit. */
    }

    if (FAILED(hrc)) vrc = VERR_TIMEOUT;
    if (vrc == VERR_TIMEOUT)
        hrc = setUpdateErrorMsg(VBOX_E_NOT_SUPPORTED,
                                Utf8StrFmt(tr("Guest Additions were not ready within time, giving up")));
#else
    /*
     * For use with the GUI we don't want to wait, just return so that the manual .ISO mounting
     * can continue.
     */
    AdditionsRunLevelType_T addsRunLevel;
    if (   FAILED(hrc = pGuest->COMGETTER(AdditionsRunLevel)(&addsRunLevel))
        || (   addsRunLevel != AdditionsRunLevelType_Userland
            && addsRunLevel != AdditionsRunLevelType_Desktop))
    {
        if (addsRunLevel == AdditionsRunLevelType_System)
            hrc = setUpdateErrorMsg(VBOX_E_NOT_SUPPORTED,
                                    Utf8StrFmt(tr("Guest Additions are installed but not fully loaded yet, aborting automatic update")));
        else
            hrc = setUpdateErrorMsg(VBOX_E_NOT_SUPPORTED,
                                    Utf8StrFmt(tr("Guest Additions not installed or ready, aborting automatic update")));
        vrc = VERR_NOT_SUPPORTED;
    }
#endif

    if (RT_SUCCESS(vrc))
    {
        /*
         * Determine if we are able to update automatically. This only works
         * if there are recent Guest Additions installed already.
         */
        Utf8Str strAddsVer;
        vrc = getGuestProperty(pGuest, "/VirtualBox/GuestAdd/Version", strAddsVer);
        if (   RT_SUCCESS(vrc)
            && RTStrVersionCompare(strAddsVer.c_str(), "4.1") < 0)
        {
            hrc = setUpdateErrorMsg(VBOX_E_NOT_SUPPORTED,
                                    Utf8StrFmt(tr("Guest has too old Guest Additions (%s) installed for automatic updating, please update manually"),
                                               strAddsVer.c_str()));
            vrc = VERR_NOT_SUPPORTED;
        }
    }

    Utf8Str strOSVer;
    eOSType osType = eOSType_Unknown;
    if (RT_SUCCESS(vrc))
    {
        /*
         * Determine guest OS type and the required installer image.
         */
/** @todo r=bird: Why are we using the guest properties for this instead of the
 * reported guest VBOXOSTYPE/ID?  Since we've got guest properties, we must
 * have GAs, so the guest additions must've reported the guest OS type. That
 * would allow proper OS categorization by family ID instead of this ridiculous
 * naive code assuming anything that isn't windows or solaris must be linux.  */
        Utf8Str strOSType;
        vrc = getGuestProperty(pGuest, "/VirtualBox/GuestInfo/OS/Product", strOSType);
        if (RT_SUCCESS(vrc))
        {
            if (   strOSType.contains(GUEST_OS_ID_STR_PARTIAL("Microsoft"), Utf8Str::CaseInsensitive)
                || strOSType.contains(GUEST_OS_ID_STR_PARTIAL("Windows"), Utf8Str::CaseInsensitive))
            {
                osType = eOSType_Windows;

                /*
                 * Determine guest OS version.
                 */
                vrc = getGuestProperty(pGuest, "/VirtualBox/GuestInfo/OS/Release", strOSVer);
                if (RT_FAILURE(vrc))
                {
                    hrc = setUpdateErrorMsg(VBOX_E_NOT_SUPPORTED,
                                            Utf8StrFmt(tr("Unable to detected guest OS version, please update manually")));
                    vrc = VERR_NOT_SUPPORTED;
                }

                /* Because Windows 2000 + XP and is bitching with WHQL popups even if we have signed drivers we
                 * can't do automated updates here. */
                /* Windows XP 64-bit (5.2) is a Windows 2003 Server actually, so skip this here. */
                if (   RT_SUCCESS(vrc)
                    && RTStrVersionCompare(strOSVer.c_str(), "5.0") >= 0)
                {
                    if (   strOSVer.startsWith("5.0") /* Exclude the build number. */
                        || strOSVer.startsWith("5.1") /* Exclude the build number. */)
                    {
                        /* If we don't have AdditionsUpdateFlag_WaitForUpdateStartOnly set we can't continue
                         * because the Windows Guest Additions installer will fail because of WHQL popups. If the
                         * flag is set this update routine ends successfully as soon as the installer was started
                         * (and the user has to deal with it in the guest). */
                        if (!(mFlags & AdditionsUpdateFlag_WaitForUpdateStartOnly))
                        {
                            hrc = setUpdateErrorMsg(VBOX_E_NOT_SUPPORTED,
                                                    Utf8StrFmt(tr("Windows 2000 and XP are not supported for automatic updating due to WHQL interaction, please update manually")));
                            vrc = VERR_NOT_SUPPORTED;
                        }
                    }
                }
                else
                {
                    hrc = setUpdateErrorMsg(VBOX_E_NOT_SUPPORTED,
                                            Utf8StrFmt(tr("%s (%s) not supported for automatic updating, please update manually"),
                                                       strOSType.c_str(), strOSVer.c_str()));
                    vrc = VERR_NOT_SUPPORTED;
                }
            }
            else if (strOSType.contains(GUEST_OS_ID_STR_PARTIAL("Solaris"), Utf8Str::CaseInsensitive))
            {
                osType = eOSType_Solaris;
            }
            else /* Everything else hopefully means Linux :-). */
                osType = eOSType_Linux;

            if (   RT_SUCCESS(vrc)
                && (   osType != eOSType_Windows
                    && osType != eOSType_Linux))
                /** @todo Support Solaris. */
            {
                hrc = setUpdateErrorMsg(VBOX_E_NOT_SUPPORTED,
                                        Utf8StrFmt(tr("Detected guest OS (%s) does not support automatic Guest Additions updating, please update manually"),
                                                   strOSType.c_str()));
                vrc = VERR_NOT_SUPPORTED;
            }
        }
    }

    if (RT_SUCCESS(vrc))
    {
        /*
         * Try to open the .ISO file to extract all needed files.
         */
        RTVFSFILE hVfsFileIso;
        vrc = RTVfsFileOpenNormal(mSource.c_str(), RTFILE_O_OPEN | RTFILE_O_READ | RTFILE_O_DENY_WRITE, &hVfsFileIso);
        if (RT_FAILURE(vrc))
        {
            hrc = setUpdateErrorMsg(VBOX_E_IPRT_ERROR,
                                    Utf8StrFmt(tr("Unable to open Guest Additions .ISO file \"%s\": %Rrc"),
                                               mSource.c_str(), vrc));
        }
        else
        {
            RTVFS hVfsIso;
            vrc = RTFsIso9660VolOpen(hVfsFileIso, 0 /*fFlags*/, &hVfsIso, NULL);
            if (RT_FAILURE(vrc))
            {
                hrc = setUpdateErrorMsg(VBOX_E_IPRT_ERROR,
                                        Utf8StrFmt(tr("Unable to open file as ISO 9660 file system volume: %Rrc"), vrc));
            }
            else
            {
                if (RT_SUCCESS(vrc))
                    vrc = setProgress(5);

                Utf8Str strUpdateDir;

                /*
                 * Prepare the update directory.
                 */
                if (RT_SUCCESS(vrc))
                {
                    /* Note: No fallback to unsafe guest locations! See @bugref{10625}. */
                    int vrcGuest;
                    vrc = pSession->i_fsCreateTemp("VBoxAutoUpdate-XXXXXXXXXXXX", "" /* Use default temp dir */,
                                                   true /* fDirectory */, strUpdateDir, 755 /* Mode */, false /* fSecure */, &vrcGuest);
                    if (RT_SUCCESS(vrc))
                    {
                        if (osType == eOSType_Windows)
                            strUpdateDir.append("\\");
                        else
                            strUpdateDir.append("/");

                        LogRel(("Guest Additions Update: Update directory is '%s'\n", strUpdateDir.c_str()));
                    }
                    else
                    {
                        switch (vrc)
                        {
                            case VERR_GSTCTL_GUEST_ERROR:
                                hrc = setUpdateErrorMsg(VBOX_E_IPRT_ERROR, tr("Creating update directory on guest failed"),
                                                        GuestErrorInfo(GuestErrorInfo::Type_Directory, vrcGuest, strUpdateDir.c_str()));
                                break;

                            default:
                                hrc = setUpdateErrorMsg(VBOX_E_IPRT_ERROR,
                                                        Utf8StrFmt(tr("Creating update directory \"%s\" on guest failed: %Rrc"),
                                                                   strUpdateDir.c_str(), vrc));
                                break;
                        }
                    }
                }

                if (RT_SUCCESS(vrc))
                    vrc = setProgress(10);

                if (RT_SUCCESS(vrc))
                {
                    /* Prepare the file(s) we want to copy over to the guest and
                     * (maybe) want to run. */
                    switch (osType)
                    {
                        case eOSType_Windows:
                        {
                            /* Do we need to install our certificates? We do this for W2K and up. */
                            bool fInstallCert = false;

                            /* Only Windows 2000 and up need certificates to be installed. */
                            if (RTStrVersionCompare(strOSVer.c_str(), "5.0") >= 0)
                            {
                                fInstallCert = true;
                                LogRel(("Guest Additions Update: Certificates for auto updating WHQL drivers will be installed\n"));
                            }
                            else
                                LogRel(("Guest Additions Update: Skipping installation of certificates for WHQL drivers\n"));

                            if (fInstallCert)
                            {
                                static struct { const char *pszDst, *pszIso; } const s_aCertFiles[] =
                                {
                                    { "vbox.cer",           "/CERT/VBOX.CER" },
                                    { "vbox-sha1.cer",      "/CERT/VBOX-SHA1.CER" },
                                    { "vbox-sha256.cer",    "/CERT/VBOX-SHA256.CER" },
                                    { "vbox-sha256-r3.cer", "/CERT/VBOX-SHA256-R3.CER" },
                                    { "oracle-vbox.cer",    "/CERT/ORACLE-VBOX.CER" },
                                };
                                uint32_t fCopyCertUtil = ISOFILE_FLAG_COPY_FROM_ISO;
                                for (uint32_t i = 0; i < RT_ELEMENTS(s_aCertFiles); i++)
                                {
                                    /* Skip if not present on the ISO. */
                                    RTFSOBJINFO ObjInfo;
                                    vrc = RTVfsQueryPathInfo(hVfsIso, s_aCertFiles[i].pszIso, &ObjInfo, RTFSOBJATTRADD_NOTHING,
                                                             RTPATH_F_ON_LINK);
                                    if (RT_FAILURE(vrc))
                                        continue;

                                    /* Copy the certificate certificate. */
                                    Utf8Str const strDstCert(strUpdateDir + s_aCertFiles[i].pszDst);
                                    mFiles.push_back(ISOFile(s_aCertFiles[i].pszIso,
                                                             strDstCert,
                                                             ISOFILE_FLAG_COPY_FROM_ISO | ISOFILE_FLAG_OPTIONAL));

                                    /* Out certificate installation utility. */
                                    /* First pass: Copy over the file (first time only) + execute it to remove any
                                     *             existing VBox certificates. */
                                    UpdateAdditionsStartupInfo siCertUtilRem;
                                    siCertUtilRem.mName = "VirtualBox Certificate Utility, removing old VirtualBox certificates";
                                    /* The argv[0] should contain full path to the executable module */
                                    siCertUtilRem.mArguments.push_back(strUpdateDir + "VBoxCertUtil.exe");
                                    siCertUtilRem.mArguments.push_back(Utf8Str("remove-trusted-publisher"));
                                    siCertUtilRem.mArguments.push_back(Utf8Str("--root")); /* Add root certificate as well. */
                                    siCertUtilRem.mArguments.push_back(strDstCert);
                                    siCertUtilRem.mArguments.push_back(strDstCert);
                                    mFiles.push_back(ISOFile("CERT/VBOXCERTUTIL.EXE",
                                                             strUpdateDir + "VBoxCertUtil.exe",
                                                             fCopyCertUtil | ISOFILE_FLAG_EXECUTE | ISOFILE_FLAG_OPTIONAL,
                                                             siCertUtilRem));
                                    fCopyCertUtil = 0;
                                    /* Second pass: Only execute (but don't copy) again, this time installng the
                                     *              recent certificates just copied over. */
                                    UpdateAdditionsStartupInfo siCertUtilAdd;
                                    siCertUtilAdd.mName = "VirtualBox Certificate Utility, installing VirtualBox certificates";
                                    /* The argv[0] should contain full path to the executable module */
                                    siCertUtilAdd.mArguments.push_back(strUpdateDir + "VBoxCertUtil.exe");
                                    siCertUtilAdd.mArguments.push_back(Utf8Str("add-trusted-publisher"));
                                    siCertUtilAdd.mArguments.push_back(Utf8Str("--root")); /* Add root certificate as well. */
                                    siCertUtilAdd.mArguments.push_back(strDstCert);
                                    siCertUtilAdd.mArguments.push_back(strDstCert);
                                    mFiles.push_back(ISOFile("CERT/VBOXCERTUTIL.EXE",
                                                             strUpdateDir + "VBoxCertUtil.exe",
                                                             ISOFILE_FLAG_EXECUTE | ISOFILE_FLAG_OPTIONAL,
                                                             siCertUtilAdd));
                                }
                            }
                            /* The installers in different flavors, as we don't know (and can't assume)
                             * the guest's bitness. */
                            mFiles.push_back(ISOFile("VBOXWINDOWSADDITIONS-X86.EXE",
                                                     strUpdateDir + "VBoxWindowsAdditions-x86.exe",
                                                     ISOFILE_FLAG_COPY_FROM_ISO));
                            mFiles.push_back(ISOFile("VBOXWINDOWSADDITIONS-AMD64.EXE",
                                                     strUpdateDir + "VBoxWindowsAdditions-amd64.exe",
                                                     ISOFILE_FLAG_COPY_FROM_ISO));
                            /* The stub loader which decides which flavor to run. */
                            UpdateAdditionsStartupInfo siInstaller;
                            siInstaller.mName = "VirtualBox Windows Guest Additions Installer";
                            /* Set a running timeout of 5 minutes -- the Windows Guest Additions
                             * setup can take quite a while, so be on the safe side. */
                            siInstaller.mTimeoutMS = 5 * 60 * 1000;

                            /* The argv[0] should contain full path to the executable module */
                            siInstaller.mArguments.push_back(strUpdateDir + "VBoxWindowsAdditions.exe");
                            siInstaller.mArguments.push_back(Utf8Str("/S")); /* We want to install in silent mode. */
                            siInstaller.mArguments.push_back(Utf8Str("/l")); /* ... and logging enabled. */
                            /* Don't quit VBoxService during upgrade because it still is used for this
                             * piece of code we're in right now (that is, here!) ... */
                            siInstaller.mArguments.push_back(Utf8Str("/no_vboxservice_exit"));
                            /* Tell the installer to report its current installation status
                             * using a running VBoxTray instance via balloon messages in the
                             * Windows taskbar. */
                            siInstaller.mArguments.push_back(Utf8Str("/post_installstatus"));
                            /* Add optional installer command line arguments from the API to the
                             * installer's startup info. */
                            vrc = addProcessArguments(siInstaller.mArguments, mArguments);
                            AssertRC(vrc);
                            /* If the caller does not want to wait for out guest update process to end,
                             * complete the progress object now so that the caller can do other work. */
                            if (mFlags & AdditionsUpdateFlag_WaitForUpdateStartOnly)
                                siInstaller.mFlags |= ProcessCreateFlag_WaitForProcessStartOnly;
                            mFiles.push_back(ISOFile("VBOXWINDOWSADDITIONS.EXE",
                                                     strUpdateDir + "VBoxWindowsAdditions.exe",
                                                     ISOFILE_FLAG_COPY_FROM_ISO | ISOFILE_FLAG_EXECUTE, siInstaller));
                            break;
                        }
                        case eOSType_Linux:
                        {
                            bool fIsArm = getPlatformArch() == PlatformArchitecture_ARM;

                            const Utf8Str strInstallerBinUC("VBOXLINUXADDITIONS" + Utf8Str(fIsArm ? "-ARM64" : "") + ".RUN");
                            const Utf8Str strInstallerBin  ("VBoxLinuxAdditions" + Utf8Str(fIsArm ? "-arm64" : "") + ".run");

                            /* Copy over the installer to the guest but don't execute it.
                             * Execution will be done by the shell instead. */
                            mFiles.push_back(ISOFile(strInstallerBinUC, strUpdateDir + strInstallerBin, ISOFILE_FLAG_COPY_FROM_ISO));

                            UpdateAdditionsStartupInfo siInstaller;
                            siInstaller.mName = "VirtualBox Linux Guest Additions Installer";
                            /* Set a running timeout of 5 minutes -- compiling modules and stuff for the Linux Guest Additions
                             * setup can take quite a while, so be on the safe side. */
                            siInstaller.mTimeoutMS = 5 * 60 * 1000;
                            /* The argv[0] should contain full path to the shell we're using to execute the installer. */
                            siInstaller.mArguments.push_back("/bin/sh");
                            /* Now add the stuff we need in order to execute the installer.  */
                            siInstaller.mArguments.push_back(strUpdateDir + strInstallerBin);
                            /* Make sure to add "--nox11" to the makeself wrapper in order to not getting any blocking xterm
                             * window spawned when doing any unattended Linux GA installations. */
                            siInstaller.mArguments.push_back("--nox11");
                            siInstaller.mArguments.push_back("--");
                            /* Force the upgrade. Needed in order to skip the confirmation dialog about warning to upgrade. */
                            siInstaller.mArguments.push_back("--force"); /** @todo We might want a dedicated "--silent" switch here. */
                            /* If the caller does not want to wait for out guest update process to end,
                             * complete the progress object now so that the caller can do other work. */
                            if (mFlags & AdditionsUpdateFlag_WaitForUpdateStartOnly)
                                siInstaller.mFlags |= ProcessCreateFlag_WaitForProcessStartOnly;
                            mFiles.push_back(ISOFile("/bin/sh" /* Source */, "/bin/sh" /* Dest */,
                                                     ISOFILE_FLAG_EXECUTE, siInstaller));
                            break;
                        }
                        case eOSType_Solaris:
                            /** @todo Add Solaris support. */
                            break;
                        default:
                            AssertReleaseMsgFailed(("Unsupported guest type: %d\n", osType));
                            break;
                    }
                }

                if (RT_SUCCESS(vrc))
                {
                    /* We want to spend 40% total for all copying operations. So roughly
                     * calculate the specific percentage step of each copied file. */
                    uint8_t uOffset = 20; /* Start at 20%. */
                    uint8_t uStep = 40 / (uint8_t)mFiles.size(); Assert(mFiles.size() <= 10);

                    LogRel(("Guest Additions Update: Copying over update files to the guest ...\n"));

                    std::vector<ISOFile>::const_iterator itFiles = mFiles.begin();
                    while (itFiles != mFiles.end())
                    {
                        if (itFiles->fFlags & ISOFILE_FLAG_COPY_FROM_ISO)
                        {
                            bool fOptional = false;
                            if (itFiles->fFlags & ISOFILE_FLAG_OPTIONAL)
                                fOptional = true;
                            vrc = copyFileToGuest(pSession, hVfsIso, itFiles->strSource, itFiles->strDest, fOptional);
                            if (RT_FAILURE(vrc))
                            {
                                hrc = setUpdateErrorMsg(VBOX_E_IPRT_ERROR,
                                                        Utf8StrFmt(tr("Error while copying file \"%s\" to \"%s\" on the guest: %Rrc"),
                                                                   itFiles->strSource.c_str(), itFiles->strDest.c_str(), vrc));
                                break;
                            }
                        }

                        vrc = setProgress(uOffset);
                        if (RT_FAILURE(vrc))
                            break;
                        uOffset += uStep;

                        ++itFiles;
                    }
                }

                /* Done copying, close .ISO file. */
                RTVfsRelease(hVfsIso);

                if (RT_SUCCESS(vrc))
                {
                    /* We want to spend 35% total for all copying operations. So roughly
                     * calculate the specific percentage step of each copied file. */
                    uint8_t uOffset = 60; /* Start at 60%. */
                    uint8_t uStep = 35 / (uint8_t)mFiles.size(); Assert(mFiles.size() <= 10);

                    LogRel(("Guest Additions Update: Executing update files ...\n"));

                    std::vector<ISOFile>::iterator itFiles = mFiles.begin();
                    while (itFiles != mFiles.end())
                    {
                        if (itFiles->fFlags & ISOFILE_FLAG_EXECUTE)
                        {
                            vrc = runFileOnGuest(pSession, itFiles->mProcInfo);
                            if (RT_FAILURE(vrc))
                                break;
                        }

                        vrc = setProgress(uOffset);
                        if (RT_FAILURE(vrc))
                            break;
                        uOffset += uStep;

                        ++itFiles;
                    }
                }

                if (RT_SUCCESS(vrc))
                {
                    /* Linux Guest Additions will restart VBoxService during installation process.
                     * In this case, connection to the guest will be temporary lost until new
                     * kernel modules will be rebuilt, loaded and new VBoxService restarted.
                     * Handle this case here: check if old connection was terminated and
                     * new one has started. */
                    if (osType == eOSType_Linux)
                    {
                        if (pSession->i_isTerminated())
                        {
                            LogRel(("Guest Additions Update: Old guest session has terminated, waiting updated guest services to start\n"));

                            /* Wait for VBoxService to restart and re-establish guest session. */
                            vrc = waitForGuestSession(pSession->i_getParent(), osType, pSession);
                            if (RT_FAILURE(vrc))
                                hrc = setUpdateErrorMsg(VBOX_E_IPRT_ERROR,
                                                        Utf8StrFmt(tr("Guest services were not restarted, please reinstall Guest Additions manually")));
                        }
                        else
                        {
                            vrc = VERR_TRY_AGAIN;
                            hrc = setUpdateErrorMsg(VBOX_E_IPRT_ERROR,
                                                    Utf8StrFmt(tr("Old guest session is still active, guest services were not restarted "
                                                                  "after installation, please reinstall Guest Additions manually")));
                        }
                    }

                    /* Remove temporary update files on the guest side before reporting completion.
                     * Only enabled for Linux guest for now. Windows has issues w/ deletting temporary
                      * installation directory. */
                    if ((osType == eOSType_Linux) && !pSession->i_isTerminated())
                    {
                        hrc = pSession->i_directoryRemove(strUpdateDir, DIRREMOVEREC_FLAG_RECURSIVE | DIRREMOVEREC_FLAG_CONTENT_AND_DIR, &vrc);
                        LogRel(("Cleanup Guest Additions update directory '%s', hrc=%Rrc, vrc=%Rrc\n",
                                strUpdateDir.c_str(), hrc, vrc));
                    }

                    if (RT_SUCCESS(vrc))
                    {
                        LogRel(("Guest Additions Update: Automatic update succeeded\n"));
                        hrc = setProgressSuccess();
                    }
                }
            }

            RTVfsFileRelease(hVfsFileIso);
        }
    }

    if (RT_FAILURE(vrc))
    {
        if (vrc == VERR_CANCELLED)
        {
            LogRel(("Guest Additions Update: Automatic update was canceled\n"));

            hrc = setUpdateErrorMsg(E_ABORT,
                                    Utf8StrFmt(tr("Operation was canceled")));
        }
        else if (vrc == VERR_TIMEOUT)
        {
            LogRel(("Guest Additions Update: Automatic update has timed out\n"));

            hrc = setUpdateErrorMsg(E_FAIL,
                                    Utf8StrFmt(tr("Operation has timed out")));
        }
        else
        {
            Utf8Str strError = Utf8StrFmt("No further error information available (%Rrc)", vrc);
            if (!mProgress.isNull()) /* Progress object is optional. */
            {
#ifdef VBOX_STRICT
                /* If we forgot to set the progress object accordingly, let us know. */
                LONG rcProgress;
                AssertMsg(   SUCCEEDED(mProgress->COMGETTER(ResultCode(&rcProgress)))
                          && FAILED(rcProgress), ("Task indicated an error (%Rrc), but progress did not indicate this (%Rhrc)\n",
                                                  vrc, rcProgress));
#endif
                com::ProgressErrorInfo errorInfo(mProgress);
                if (   errorInfo.isFullAvailable()
                    || errorInfo.isBasicAvailable())
                {
                    strError = errorInfo.getText();
                }
            }

            LogRel(("Guest Additions Update: Automatic update failed: %s (vrc=%Rrc, hrc=%Rhrc)\n",
                    strError.c_str(), vrc, hrc));
        }

        LogRel(("Guest Additions Update: An error has occurred (see above). Please install Guest Additions manually\n"));
    }

    /** @todo Clean up copied / left over installation files. */

    LogFlowFuncLeaveRC(vrc);
    return vrc;
}

