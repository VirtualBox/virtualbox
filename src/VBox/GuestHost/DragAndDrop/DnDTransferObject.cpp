/* $Id: DnDTransferObject.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DnD - Transfer object implemenation for handling creation/reading/writing to files and directories on host or guest side.
 */

/*
 * Copyright (C) 2014-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_GUEST_DND
#include <VBox/GuestHost/DragAndDrop.h>

#include <iprt/dir.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/fs.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/uri.h>

#include <VBox/log.h>


/*********************************************************************************************************************************
*   Prototypes                                                                                                                   *
*********************************************************************************************************************************/
static int dndTransferObjectCloseInternal(PDNDTRANSFEROBJECT pObj);
static int dndTransferObjectQueryInfoInternal(PDNDTRANSFEROBJECT pObj);


/**
 * Initializes the object, internal version.
 *
 * @returns VBox status code.
 * @param   pObj                DnD transfer object to initialize.
 */
static int dndTransferObjectInitInternal(PDNDTRANSFEROBJECT pObj)
{
    AssertPtrReturn(pObj, VERR_INVALID_POINTER);

    pObj->enmType = DNDTRANSFEROBJTYPE_UNKNOWN;
    pObj->idxDst  = 0;
    pObj->pszPath = NULL;

    RT_ZERO(pObj->u);

    return VINF_SUCCESS;
}

/**
 * Initializes the object.
 *
 * @returns VBox status code.
 * @param   pObj                DnD transfer object to initialize.
 */
int DnDTransferObjectInit(PDNDTRANSFEROBJECT pObj)
{
    return dndTransferObjectInitInternal(pObj);
}

/**
 * Initializes the object with an expected object type and file path.
 *
 * @returns VBox status code.
 * @param   pObj                DnD transfer object to initialize.
 * @param   enmType             Type we expect this object to be.
 * @param   pcszPathSrcAbs      Absolute source (local) path of file this object represents. Can be empty (e.g. for root stuff).
 * @param   pcszPathDst         Relative path of file this object represents at the destination.
 *                              Together with \a pcszPathSrcAbs this represents the complete absolute local path.
 */
int DnDTransferObjectInitEx(PDNDTRANSFEROBJECT pObj,
                            DNDTRANSFEROBJTYPE enmType, const char *pcszPathSrcAbs, const char *pcszPathDst)
{
    AssertPtrReturn(pObj, VERR_INVALID_POINTER);
    AssertReturn(pObj->enmType == DNDTRANSFEROBJTYPE_UNKNOWN, VERR_WRONG_ORDER); /* Already initialized? */
    /* pcszPathSrcAbs can be empty. */
    AssertPtrReturn(pcszPathDst, VERR_INVALID_POINTER);

    int rc = dndTransferObjectInitInternal(pObj);
    AssertRCReturn(rc, rc);

    rc = DnDPathValidate(pcszPathDst, false /* Does not need to exist */);
    AssertRCReturn(rc, rc);

    char szPath[RTPATH_MAX + 1];

    /* Save the index (in characters) where the first destination segment starts. */
    if (   pcszPathSrcAbs
        && RTStrNLen(pcszPathSrcAbs, RTSTR_MAX))
    {
        rc = DnDPathValidate(pcszPathSrcAbs, false /* Does not need to exist */);
        if (RT_FAILURE(rc))
            return rc;

        rc = RTStrCopy(szPath, sizeof(szPath), pcszPathSrcAbs);
        if (RT_SUCCESS(rc))
            rc = RTPathEnsureTrailingSeparator(szPath, sizeof(szPath)) == 0 ? VERR_BUFFER_OVERFLOW : VINF_SUCCESS;

        /* Save the index (in characters) where the destination part starts. */
        pObj->idxDst = (uint16_t)RTStrNLen(szPath, RTPATH_MAX);
        AssertReturn(pObj->idxDst <= RTPATH_MAX, VERR_INVALID_PARAMETER);
    }
    else
    {
        szPath[0]    = '\0'; /* Init empty string. */
        pObj->idxDst = 0;
    }

    if (RT_FAILURE(rc))
        return rc;

    /* Append the destination part. */
    rc = RTPathAppend(szPath, sizeof(szPath), pcszPathDst);
    if (   RT_SUCCESS(rc)
        && enmType == DNDTRANSFEROBJTYPE_DIRECTORY)
        rc = RTPathEnsureTrailingSeparator(szPath, sizeof(szPath)) == 0 ? VERR_BUFFER_OVERFLOW : VINF_SUCCESS;

    if (RT_FAILURE(rc))
        return rc;

    pObj->pszPath = RTStrDup(szPath);
    if (!pObj->pszPath)
        return VERR_NO_MEMORY;

    /* Convert paths into transport format. */
    rc = DnDPathConvert(pObj->pszPath, strlen(pObj->pszPath), DNDPATHCONVERT_FLAGS_TRANSPORT);
    if (RT_FAILURE(rc))
    {
        RTStrFree(pObj->pszPath);
        pObj->pszPath = NULL;
        return rc;
    }

    LogFlowFunc(("enmType=%RU32, pcszPathSrcAbs=%s, pcszPathDst=%s -> pszPath=%s\n",
                 enmType, pcszPathSrcAbs, pcszPathDst, pObj->pszPath));

    pObj->enmType = enmType;

    return VINF_SUCCESS;
}

/**
 * Destroys a DnD transfer object.
 *
 * @param   pObj                DnD transfer object to destroy.
 */
void DnDTransferObjectDestroy(PDNDTRANSFEROBJECT pObj)
{
    if (!pObj)
        return;

    DnDTransferObjectReset(pObj);
}

/**
 * Closes the object's internal handles (to files / ...).
 *
 * @returns VBox status code.
 * @param   pObj                DnD transfer object to close internally.
 */
static int dndTransferObjectCloseInternal(PDNDTRANSFEROBJECT pObj)
{
    AssertPtrReturn(pObj, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;

    if (pObj->pszPath)
        LogRel2(("DnD: Closing '%s'\n", pObj->pszPath));

    switch (pObj->enmType)
    {
        case DNDTRANSFEROBJTYPE_FILE:
        {
            if (RTFileIsValid(pObj->u.File.hFile))
            {
                rc = RTFileClose(pObj->u.File.hFile);
                if (RT_SUCCESS(rc))
                {
                    pObj->u.File.hFile = NIL_RTFILE;
                    RT_ZERO(pObj->u.File.objInfo);
                }
                else
                    LogRel(("DnD: Closing file '%s' failed with %Rrc\n", pObj->pszPath, rc));
            }
            break;
        }

        case DNDTRANSFEROBJTYPE_DIRECTORY:
        {
            if (RTDirIsValid(pObj->u.Dir.hDir))
            {
                rc = RTDirClose(pObj->u.Dir.hDir);
                if (RT_SUCCESS(rc))
                {
                    pObj->u.Dir.hDir = NIL_RTDIR;
                    RT_ZERO(pObj->u.Dir.objInfo);
                }
                else
                    LogRel(("DnD: Closing directory '%s' failed with %Rrc\n", pObj->pszPath, rc));
            }
            break;
        }

        default:
            break;
    }

    return rc;
}

/**
 * Closes the object.
 * This also closes the internal handles associated with the object (to files / ...).
 *
 * @returns VBox status code.
 * @param   pObj                DnD transfer object to close.
 */
int DnDTransferObjectClose(PDNDTRANSFEROBJECT pObj)
{
    AssertPtrReturn(pObj, VERR_INVALID_POINTER);

    return dndTransferObjectCloseInternal(pObj);
}

/**
 * Returns the absolute source path of the object.
 *
 * @return  Absolute source path of the object.
 * @param   pObj                DnD transfer object to get source path for.
 */
const char *DnDTransferObjectGetSourcePath(PDNDTRANSFEROBJECT pObj)
{
    AssertPtrReturn(pObj, NULL);
    return pObj->pszPath;
}

/**
 * Returns the (relative) destination path of the object, in transport style.
 *
 * @return  Relative destination path of the object, or NULL if not set.
 * @param   pObj                DnD transfer object to get destination path for.
 */
const char *DnDTransferObjectGetDestPath(PDNDTRANSFEROBJECT pObj)
{
    AssertPtrReturn(pObj, NULL);

    if (!pObj->pszPath)
        return NULL;

    AssertReturn(strlen(pObj->pszPath) >= pObj->idxDst, NULL);

    return &pObj->pszPath[pObj->idxDst];
}

/**
 * Returns the (relative) destination path of the object, extended version.
 *
 * @return  VBox status code, or VERR_NOT_FOUND if not initialized yet.
 * @param   pObj                DnD transfer object to get destination path for.
 * @param   enmStyle            Which path style to return.
 * @param   pszBuf              Where to store the path.
 * @param   cbBuf               Size (in bytes) where to store the path.
 */
int DnDTransferObjectGetDestPathEx(PDNDTRANSFEROBJECT pObj, DNDTRANSFEROBJPATHSTYLE enmStyle, char *pszBuf, size_t cbBuf)
{
    AssertPtrReturn(pObj, VERR_INVALID_POINTER);
    AssertPtrReturn(pszBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf, VERR_INVALID_PARAMETER);

    if (!pObj->pszPath)
        return VERR_NOT_FOUND;

    AssertReturn(strlen(pObj->pszPath) >= pObj->idxDst, VERR_INTERNAL_ERROR);

    int rc = RTStrCopy(pszBuf, cbBuf, &pObj->pszPath[pObj->idxDst]);
    if (   RT_SUCCESS(rc)
        && enmStyle == DNDTRANSFEROBJPATHSTYLE_DOS)
        rc = DnDPathConvert(pszBuf, cbBuf, DNDPATHCONVERT_FLAGS_TO_DOS);

    return rc;
}

/**
 * Returns the directory / file mode of the object.
 *
 * @return  File / directory mode.
 * @param   pObj                DnD transfer object to get directory / file mode for.
 */
RTFMODE DnDTransferObjectGetMode(PDNDTRANSFEROBJECT pObj)
{
    AssertPtrReturn(pObj, 0);

    switch (pObj->enmType)
    {
        case DNDTRANSFEROBJTYPE_FILE:
            return pObj->u.File.objInfo.Attr.fMode;

        case DNDTRANSFEROBJTYPE_DIRECTORY:
            return pObj->u.Dir.objInfo.Attr.fMode;

        default:
            break;
    }

    return 0;
}

/**
 * Returns the bytes already processed (read / written).
 *
 * Note: Only applies if the object is of type DnDTransferObjectType_File.
 *
 * @return  Bytes already processed (read / written).
 * @param   pObj                DnD transfer object to get processed bytes for.
 */
uint64_t DnDTransferObjectGetProcessed(PDNDTRANSFEROBJECT pObj)
{
    if (pObj->enmType == DNDTRANSFEROBJTYPE_FILE)
        return pObj->u.File.cbProcessed;

    return 0;
}

/**
 * Returns the file's logical size (in bytes).
 *
 * Note: Only applies if the object is of type DnDTransferObjectType_File.
 *
 * @return  The file's logical size (in bytes).
 * @param   pObj                DnD transfer object to get size for.
 */
uint64_t DnDTransferObjectGetSize(PDNDTRANSFEROBJECT pObj)
{
    if (pObj->enmType == DNDTRANSFEROBJTYPE_FILE)
        return pObj->u.File.cbToProcess;

    return 0;
}

/**
 * Returns the object's type.
 *
 * @return  The object's type.
 * @param   pObj                DnD transfer object to get type for.
 */
DNDTRANSFEROBJTYPE DnDTransferObjectGetType(PDNDTRANSFEROBJECT pObj)
{
    return pObj->enmType;
}

/**
 * Returns whether the processing of the object is complete or not.
 * For file objects this means that all bytes have been processed.
 *
 * @return  True if complete, False if not.
 * @param   pObj                DnD transfer object to get completion status for.
 */
bool DnDTransferObjectIsComplete(PDNDTRANSFEROBJECT pObj)
{
    bool fComplete;

    switch (pObj->enmType)
    {
        case DNDTRANSFEROBJTYPE_FILE:
            Assert(pObj->u.File.cbProcessed <= pObj->u.File.cbToProcess);
            fComplete = pObj->u.File.cbProcessed == pObj->u.File.cbToProcess;
            break;

        case DNDTRANSFEROBJTYPE_DIRECTORY:
            fComplete = true;
            break;

        default:
            fComplete = true;
            break;
    }

    return fComplete;
}

/**
 * Returns whether the object is in an open state or not.
 * @param   pObj                DnD transfer object to get open status for.
 */
bool DnDTransferObjectIsOpen(PDNDTRANSFEROBJECT pObj)
{
    switch (pObj->enmType)
    {
        case DNDTRANSFEROBJTYPE_FILE:      return RTFileIsValid(pObj->u.File.hFile);
        case DNDTRANSFEROBJTYPE_DIRECTORY: return RTDirIsValid(pObj->u.Dir.hDir);
        default:                           break;
    }

    return false;
}

/**
 * Open the object with a specific file type, and, depending on the type, specifying additional parameters.
 *
 * @return  IPRT status code.
 * @param   pObj                DnD transfer object to open.
 * @param   fOpen               Open mode to use; only valid for file objects.
 * @param   fMode               File mode to set; only valid for file objects. Depends on fOpen and and can be 0.
 * @param   fFlags              Additional DnD transfer object flags.
 */
int DnDTransferObjectOpen(PDNDTRANSFEROBJECT pObj, uint64_t fOpen, RTFMODE fMode, DNDTRANSFEROBJECTFLAGS fFlags)
{
    AssertPtrReturn(pObj, VERR_INVALID_POINTER);
    AssertReturn(fOpen, VERR_INVALID_FLAGS);
    /* fMode is optional. */
    AssertReturn(!(fFlags & ~DNDTRANSFEROBJECT_FLAGS_VALID_MASK), VERR_INVALID_FLAGS);
    RT_NOREF1(fFlags);

    int rc = VINF_SUCCESS;

    LogFlowFunc(("pszPath=%s, fOpen=0x%x, fMode=0x%x, fFlags=0x%x\n", pObj->pszPath, fOpen, fMode, fFlags));

    switch (pObj->enmType)
    {
        case DNDTRANSFEROBJTYPE_FILE:
        {
            LogRel2(("DnD: Opening file '%s'\n", pObj->pszPath));

            /*
             * Open files on the source with RTFILE_O_DENY_WRITE to prevent races
             * where the OS writes to the file while the destination side transfers
             * it over.
             */
            rc = RTFileOpen(&pObj->u.File.hFile, pObj->pszPath, fOpen);
            if (RT_SUCCESS(rc))
            {
                if (   (fOpen & RTFILE_O_WRITE) /* Only set the file mode on write. */
                    &&  fMode                   /* Some file mode to set specified? */)
                {
                    rc = RTFileSetMode(pObj->u.File.hFile, fMode);
                    if (RT_FAILURE(rc))
                        LogRel(("DnD: Setting mode %#x for file '%s' failed with %Rrc\n", fMode, pObj->pszPath, rc));
                }
                else if (fOpen & RTFILE_O_READ)
                {
                    rc = dndTransferObjectQueryInfoInternal(pObj);
                }
            }
            else
                LogRel(("DnD: Opening file '%s' failed with %Rrc\n", pObj->pszPath, rc));

            if (RT_SUCCESS(rc))
            {
                LogFlowFunc(("File cbObject=%RU64, fMode=0x%x\n",
                             pObj->u.File.objInfo.cbObject, pObj->u.File.objInfo.Attr.fMode));
                pObj->u.File.cbToProcess = pObj->u.File.objInfo.cbObject;
                pObj->u.File.cbProcessed = 0;
            }

            break;
        }

        case DNDTRANSFEROBJTYPE_DIRECTORY:
        {
            LogRel2(("DnD: Opening directory '%s'\n", pObj->pszPath));

            rc = RTDirOpen(&pObj->u.Dir.hDir, pObj->pszPath);
            if (RT_SUCCESS(rc))
            {
                rc = dndTransferObjectQueryInfoInternal(pObj);
            }
            else
                LogRel(("DnD: Opening directory '%s' failed with %Rrc\n", pObj->pszPath, rc));
            break;
        }

        default:
            rc = VERR_NOT_IMPLEMENTED;
            break;
    }

    LogFlowFuncLeaveRC(rc);
    return rc;
}

/**
 * Queries information about the object using a specific view, internal version.
 *
 * @return  IPRT status code.
 * @param   pObj                DnD transfer object to query info for.
 */
static int dndTransferObjectQueryInfoInternal(PDNDTRANSFEROBJECT pObj)
{
    int rc;

    switch (pObj->enmType)
    {
        case DNDTRANSFEROBJTYPE_FILE:
            AssertMsgReturn(RTFileIsValid(pObj->u.File.hFile), ("Object has invalid file handle\n"), VERR_INVALID_STATE);
            rc = RTFileQueryInfo(pObj->u.File.hFile, &pObj->u.File.objInfo, RTFSOBJATTRADD_NOTHING);
            break;

        case DNDTRANSFEROBJTYPE_DIRECTORY:
            AssertMsgReturn(RTDirIsValid(pObj->u.Dir.hDir), ("Object has invalid directory handle\n"), VERR_INVALID_STATE);
            rc = RTDirQueryInfo(pObj->u.Dir.hDir, &pObj->u.Dir.objInfo, RTFSOBJATTRADD_NOTHING);
            break;

        default:
            rc = VERR_NOT_IMPLEMENTED;
            break;
    }

    if (RT_FAILURE(rc))
        LogRel(("DnD: Querying information for '%s' failed with %Rrc\n", pObj->pszPath, rc));

    return rc;
}

/**
 * Queries information about the object using a specific view.
 *
 * @return  IPRT status code.
 * @param   pObj                DnD transfer object to query info for.
 */
int DnDTransferObjectQueryInfo(PDNDTRANSFEROBJECT pObj)
{
    AssertPtrReturn(pObj, VERR_INVALID_POINTER);
    return dndTransferObjectQueryInfoInternal(pObj);
}

/**
 * Reads data from the object. Only applies to files objects.
 *
 * @return  IPRT status code.
 * @param   pObj                DnD transfer object to read data from.
 * @param   pvBuf               Buffer where to store the read data.
 * @param   cbBuf               Size (in bytes) of the buffer.
 * @param   pcbRead             Pointer where to store how many bytes were read. Optional.
 */
int DnDTransferObjectRead(PDNDTRANSFEROBJECT pObj, void *pvBuf, size_t cbBuf, uint32_t *pcbRead)
{
    AssertPtrReturn(pObj, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf, VERR_INVALID_PARAMETER);
    /* pcbRead is optional. */

    size_t cbRead = 0;

    int rc;
    switch (pObj->enmType)
    {
        case DNDTRANSFEROBJTYPE_FILE:
        {
            rc = RTFileRead(pObj->u.File.hFile, pvBuf, cbBuf, &cbRead);
            if (RT_SUCCESS(rc))
            {
                pObj->u.File.cbProcessed += cbRead;
                Assert(pObj->u.File.cbProcessed <= pObj->u.File.cbToProcess);

                /* End of file reached or error occurred? */
                if (   pObj->u.File.cbToProcess
                    && pObj->u.File.cbProcessed == pObj->u.File.cbToProcess)
                {
                    rc = VINF_EOF;
                }
            }
            else
                LogRel(("DnD: Reading from file '%s' failed with %Rrc\n", pObj->pszPath, rc));
            break;
        }

        case DNDTRANSFEROBJTYPE_DIRECTORY:
        {
            rc = VINF_SUCCESS;
            break;
        }

        default:
            rc = VERR_NOT_IMPLEMENTED;
            break;
    }

    if (RT_SUCCESS(rc))
    {
        if (pcbRead)
            *pcbRead = (uint32_t)cbRead;
    }

    LogFlowFunc(("Returning cbRead=%zu, rc=%Rrc\n", cbRead, rc));
    return rc;
}

/**
 * Resets the object's state and closes all related handles.
 *
 * @param   pObj                DnD transfer object to reset.
 */
void DnDTransferObjectReset(PDNDTRANSFEROBJECT pObj)
{
    AssertPtrReturnVoid(pObj);

    LogFlowFuncEnter();

    int vrc2 = dndTransferObjectCloseInternal(pObj);
    AssertRCReturnVoid(vrc2);

    pObj->enmType    = DNDTRANSFEROBJTYPE_UNKNOWN;
    pObj->idxDst     = 0;

    RTStrFree(pObj->pszPath);
    pObj->pszPath = NULL;

    RT_ZERO(pObj->u);
}

/**
 * Sets the bytes to process by the object.
 *
 * Note: Only applies if the object is of type DnDTransferObjectType_File.
 *
 * @return  IPRT return code.
 * @param   pObj                DnD transfer object to set size for.
 * @param   cbSize              Size (in bytes) to process.
 */
int DnDTransferObjectSetSize(PDNDTRANSFEROBJECT pObj, uint64_t cbSize)
{
    AssertPtrReturn(pObj, VERR_INVALID_POINTER);
    AssertReturn(pObj->enmType == DNDTRANSFEROBJTYPE_FILE, VERR_INVALID_PARAMETER);

    /** @todo Implement sparse file support here. */

    pObj->u.File.cbToProcess = cbSize;
    return VINF_SUCCESS;
}

/**
 * Writes data to an object. Only applies to file objects.
 *
 * @return  IPRT status code.
 * @param   pObj                DnD transfer object to write to.
 * @param   pvBuf               Buffer of data to write.
 * @param   cbBuf               Size (in bytes) of data to write.
 * @param   pcbWritten          Pointer where to store how many bytes were written. Optional.
 */
int DnDTransferObjectWrite(PDNDTRANSFEROBJECT pObj, const void *pvBuf, size_t cbBuf, uint32_t *pcbWritten)
{
    AssertPtrReturn(pObj, VERR_INVALID_POINTER);
    AssertPtrReturn(pvBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf, VERR_INVALID_PARAMETER);
    /* pcbWritten is optional. */

    size_t cbWritten = 0;

    int rc;
    switch (pObj->enmType)
    {
        case DNDTRANSFEROBJTYPE_FILE:
        {
            rc = RTFileWrite(pObj->u.File.hFile, pvBuf, cbBuf, &cbWritten);
            if (RT_SUCCESS(rc))
            {
                pObj->u.File.cbProcessed += cbWritten;
            }
            else
                LogRel(("DnD: Writing to file '%s' failed with %Rrc\n", pObj->pszPath, rc));
            break;
        }

        case DNDTRANSFEROBJTYPE_DIRECTORY:
        {
            rc = VINF_SUCCESS;
            break;
        }

        default:
            rc = VERR_NOT_IMPLEMENTED;
            break;
    }

    if (RT_SUCCESS(rc))
    {
        if (pcbWritten)
            *pcbWritten = (uint32_t)cbWritten;
    }

    LogFlowFunc(("Returning cbWritten=%zu, rc=%Rrc\n", cbWritten, rc));
    return rc;
}

