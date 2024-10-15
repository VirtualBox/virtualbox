/* $Id: vbsf.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Folders - VBox Shared Folders.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_SHARED_FOLDERS
#ifdef UNITTEST
# include "testcase/tstSharedFolderService.h"
#endif

#include "vbsfpath.h"
#include "mappings.h"
#include "vbsf.h"
#include "shflhandle.h"

#include <VBox/AssertGuest.h>
#include <VBox/param.h>
#include <VBox/VMMDev.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/fs.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/symlink.h>
#include <iprt/uni.h>
#include <iprt/stream.h>
#ifdef RT_OS_DARWIN
# include <Carbon/Carbon.h>
#endif

#ifdef UNITTEST
# include "teststubs.h"
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define SHFL_RT_LINK(pClient) ((pClient)->fu32Flags & SHFL_CF_SYMLINKS ? RTPATH_F_ON_LINK : RTPATH_F_FOLLOW_LINK)

/**
 * @todo find a better solution for supporting the execute bit for non-windows
 * guests on windows host. Search for "0111" to find all the relevant places.
 */


#ifndef RT_OS_WINDOWS

/**
 * Helps to check if pszPath deserves a VERR_PATH_NOT_FOUND status when catering
 * to windows guests.
 */
static bool vbsfErrorStyleIsWindowsPathNotFound(char *pszPath)
{
    /*
     * Check if the parent directory actually exists.  We temporarily modify the path here.
     */
    size_t cchParent = RTPathParentLength(pszPath);
    char chSaved = pszPath[cchParent];
    pszPath[cchParent] = '\0';
    RTFSOBJINFO ObjInfo;
    int vrc = RTPathQueryInfoEx(pszPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
    pszPath[cchParent] = chSaved;
    if (RT_SUCCESS(vrc))
    {
        if (RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode))
            return false;
        return true;
    }
    if (vrc == VERR_FILE_NOT_FOUND || vrc == VERR_PATH_NOT_FOUND)
        return true;
    return false;
}

/**
 * Helps to check if pszPath deserves a VERR_PATH_NOT_FOUND status when catering
 * to windows guests.
 */
static bool vbsfErrorStyleIsWindowsPathNotFound2(char *pszSrcPath, char *pszDstPath)
{
    /*
     * Do the source parent first.
     */
    size_t cchParent = RTPathParentLength(pszSrcPath);
    char chSaved = pszSrcPath[cchParent];
    pszSrcPath[cchParent] = '\0';
    RTFSOBJINFO ObjInfo;
    int vrc = RTPathQueryInfoEx(pszSrcPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
    pszSrcPath[cchParent] = chSaved;
    if (   (RT_SUCCESS(vrc) && !RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode))
        || vrc == VERR_FILE_NOT_FOUND
        || vrc == VERR_PATH_NOT_FOUND)
        return true;
    if (RT_FAILURE(vrc))
        return false;

    /*
     * The source itself.
     */
    vrc = RTPathQueryInfoEx(pszSrcPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
    if (RT_SUCCESS(vrc))
    {
        /*
         * The source is fine, continue with the destination.
         */
        cchParent = RTPathParentLength(pszDstPath);
        chSaved = pszDstPath[cchParent];
        pszDstPath[cchParent] = '\0';
        vrc = RTPathQueryInfoEx(pszDstPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
        pszDstPath[cchParent] = chSaved;
        if (   (RT_SUCCESS(vrc) && !RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode))
            || vrc == VERR_FILE_NOT_FOUND
            || vrc == VERR_PATH_NOT_FOUND)
            return true;
    }
    return false;
}

/**
 * Helps checking if the specified path happens to exist but not be a directory.
 */
static bool vbsfErrorStyleIsWindowsNotADirectory(const char *pszPath)
{
    RTFSOBJINFO ObjInfo;
    int vrc = RTPathQueryInfoEx(pszPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
    if (RT_SUCCESS(vrc))
    {
        if (RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode))
            return false;
        return true;
    }
    return false;
}

/**
 * Helps to check if pszPath deserves a VERR_INVALID_NAME status when catering
 * to windows guests.
 */
static bool vbsfErrorStyleIsWindowsInvalidNameForNonDir(char *pszPath)
{
    /*
     * This only applies to paths with trailing slashes.
     */
    size_t const cchPath = strlen(pszPath);
    if (cchPath > 0 && RTPATH_IS_SLASH(pszPath[cchPath - 1]))
    {
        /*
         * However it doesn't if an earlier path component is missing or not a file.
         */
        size_t cchParent = RTPathParentLength(pszPath);
        char chSaved = pszPath[cchParent];
        pszPath[cchParent] = '\0';
        RTFSOBJINFO ObjInfo;
        int vrc = RTPathQueryInfoEx(pszPath, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
        pszPath[cchParent] = chSaved;
        if (RT_SUCCESS(vrc) && RTFS_IS_DIRECTORY(ObjInfo.Attr.fMode))
            return true;
    }
    return false;
}

#endif /* RT_OS_WINDOWS */

static void vbsfStripLastComponent(char *pszFullPath, uint32_t cbFullPathRoot)
{
    RTUNICP cp;

    /* Do not strip root. */
    char *s = pszFullPath + cbFullPathRoot;
    char *delimSecondLast = NULL;
    char *delimLast = NULL;

    LogFlowFunc(("%s -> %s\n", pszFullPath, s));

    for (;;)
    {
        cp = RTStrGetCp(s);

        if (cp == RTUNICP_INVALID || cp == 0)
        {
            break;
        }

        if (cp == RTPATH_DELIMITER)
        {
            if (delimLast != NULL)
            {
                delimSecondLast = delimLast;
            }

            delimLast = s;
        }

        s = RTStrNextCp(s);
    }

    if (cp == 0)
    {
        if (delimLast + 1 == s)
        {
            if (delimSecondLast)
            {
                *delimSecondLast = 0;
            }
            else if (delimLast)
            {
                *delimLast = 0;
            }
        }
        else
        {
            if (delimLast)
            {
                *delimLast = 0;
            }
        }
    }

    LogFlowFunc(("%s, %s, %s\n", pszFullPath, delimLast, delimSecondLast));
}

static int vbsfBuildFullPath(SHFLCLIENTDATA *pClient, SHFLROOT root, PCSHFLSTRING pPath,
                             uint32_t cbPath, char **ppszFullPath, uint32_t *pcbFullPathRoot,
                             bool fWildCard = false, bool fPreserveLastComponent = false)
{
    char *pszHostPath = NULL;
    uint32_t fu32PathFlags = 0;
    uint32_t fu32Options =   VBSF_O_PATH_CHECK_ROOT_ESCAPE
                           | (fWildCard ? VBSF_O_PATH_WILDCARD : 0)
                           | (fPreserveLastComponent ? VBSF_O_PATH_PRESERVE_LAST_COMPONENT : 0);

    int rc = vbsfPathGuestToHost(pClient, root, pPath, cbPath,
                                 &pszHostPath, pcbFullPathRoot, fu32Options, &fu32PathFlags);
    if (BIT_FLAG(pClient->fu32Flags, SHFL_CF_UTF8))
    {
        LogRel2(("SharedFolders: GuestToHost 0x%RX32 [%.*s]->[%s] %Rrc\n", fu32PathFlags, pPath->u16Length, &pPath->String.utf8[0], pszHostPath, rc));
    }
    else
    {
        LogRel2(("SharedFolders: GuestToHost 0x%RX32 [%.*ls]->[%s] %Rrc\n", fu32PathFlags, pPath->u16Length / 2, &pPath->String.utf16[0], pszHostPath, rc));
    }

    if (RT_SUCCESS(rc))
    {
        if (ppszFullPath)
            *ppszFullPath = pszHostPath;
    }
    return rc;
}

static void vbsfFreeFullPath(char *pszFullPath)
{
    vbsfFreeHostPath(pszFullPath);
}

typedef enum VBSFCHECKACCESS
{
    VBSF_CHECK_ACCESS_READ = 0,
    VBSF_CHECK_ACCESS_WRITE = 1
} VBSFCHECKACCESS;

/**
 * Check if the handle data is valid and the operation is allowed on the shared folder.
 *
 * @returns IPRT status code
 * @param  pClient               Data structure describing the client accessing the shared folder
 * @param  root                  The index of the shared folder in the table of mappings.
 * @param  pHandle               Information about the file or directory object.
 * @param  enmCheckAccess        Whether the operation needs read only or write access.
 */
static int vbsfCheckHandleAccess(SHFLCLIENTDATA *pClient, SHFLROOT root,
                                 SHFLFILEHANDLE *pHandle, VBSFCHECKACCESS enmCheckAccess)
{
    /* Handle from the same 'root' index? */
    if (RT_LIKELY(RT_VALID_PTR(pHandle) && root == pHandle->root))
    { /* likely */ }
    else
        return VERR_INVALID_HANDLE;

    /* Check if the guest is still allowed to access this share.
     * vbsfMappingsQueryWritable returns error if the shared folder has been removed from the VM settings.
     */
    bool fWritable;
    int rc = vbsfMappingsQueryWritable(pClient, root, &fWritable);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else
        return VERR_ACCESS_DENIED;

    if (enmCheckAccess == VBSF_CHECK_ACCESS_WRITE)
    {
        /* Operation requires write access. Check if the shared folder is writable too. */
        if (RT_LIKELY(fWritable))
        { /* likely */ }
        else
            return VERR_WRITE_PROTECT;
    }

    return VINF_SUCCESS;
}

/**
 * Convert shared folder create flags (see include/iprt/shflsvc.h) into iprt create flags.
 *
 * @returns iprt status code
 * @param  fWritable  whether the shared folder is writable
 * @param  fShflFlags shared folder create flags
 * @param  fMode      file attributes
 * @param  handleInitial initial handle
 * @param  pfOpen     Where to return iprt create flags
 */
static int vbsfConvertFileOpenFlags(bool fWritable, unsigned fShflFlags, RTFMODE fMode, SHFLHANDLE handleInitial, uint64_t *pfOpen)
{
    uint64_t fOpen = 0;
    int rc = VINF_SUCCESS;

    if (   (fMode & RTFS_DOS_MASK) != 0
        && (fMode & RTFS_UNIX_MASK) == 0)
    {
        /* A DOS/Windows guest, make RTFS_UNIX_* from RTFS_DOS_*.
         * @todo this is based on rtFsModeNormalize/rtFsModeFromDos.
         *       May be better to use RTFsModeNormalize here.
         */
        fMode |= RTFS_UNIX_IRUSR | RTFS_UNIX_IRGRP | RTFS_UNIX_IROTH;
        /* x for directories. */
        if (fMode & RTFS_DOS_DIRECTORY)
            fMode |= RTFS_TYPE_DIRECTORY | RTFS_UNIX_IXUSR | RTFS_UNIX_IXGRP | RTFS_UNIX_IXOTH;
        /* writable? */
        if (!(fMode & RTFS_DOS_READONLY))
            fMode |= RTFS_UNIX_IWUSR | RTFS_UNIX_IWGRP | RTFS_UNIX_IWOTH;

        /* Set the requested mode using only allowed bits. */
        fOpen |= ((fMode & RTFS_UNIX_MASK) << RTFILE_O_CREATE_MODE_SHIFT) & RTFILE_O_CREATE_MODE_MASK;
    }
    else
    {
        /* Old linux and solaris additions did not initialize the Info.Attr.fMode field
         * and it contained random bits from stack. Detect this using the handle field value
         * passed from the guest: old additions set it (incorrectly) to 0, new additions
         * set it to SHFL_HANDLE_NIL(~0).
         */
        if (handleInitial == 0)
        {
            /* Old additions. Do nothing, use default mode. */
        }
        else
        {
            /* New additions or Windows additions. Set the requested mode using only allowed bits.
             * Note: Windows guest set RTFS_UNIX_MASK bits to 0, which means a default mode
             *       will be set in fOpen.
             */
            fOpen |= ((fMode & RTFS_UNIX_MASK) << RTFILE_O_CREATE_MODE_SHIFT) & RTFILE_O_CREATE_MODE_MASK;
        }
    }

    switch (BIT_FLAG(fShflFlags, SHFL_CF_ACCESS_MASK_RW))
    {
        default:
        case SHFL_CF_ACCESS_NONE:
        {
#ifdef RT_OS_WINDOWS
            if (BIT_FLAG(fShflFlags, SHFL_CF_ACCESS_MASK_ATTR) != SHFL_CF_ACCESS_ATTR_NONE)
                fOpen |= RTFILE_O_ATTR_ONLY;
            else
#endif
                fOpen |= RTFILE_O_READ;
            Log(("FLAG: SHFL_CF_ACCESS_NONE\n"));
            break;
        }

        case SHFL_CF_ACCESS_READ:
        {
            fOpen |= RTFILE_O_READ;
            Log(("FLAG: SHFL_CF_ACCESS_READ\n"));
            break;
        }

        case SHFL_CF_ACCESS_WRITE:
        {
            fOpen |= RTFILE_O_WRITE;
            Log(("FLAG: SHFL_CF_ACCESS_WRITE\n"));
            break;
        }

        case SHFL_CF_ACCESS_READWRITE:
        {
            fOpen |= RTFILE_O_READWRITE;
            Log(("FLAG: SHFL_CF_ACCESS_READWRITE\n"));
            break;
        }
    }

    if (fShflFlags & SHFL_CF_ACCESS_APPEND)
    {
        fOpen |= RTFILE_O_APPEND;
    }

    switch (BIT_FLAG(fShflFlags, SHFL_CF_ACCESS_MASK_ATTR))
    {
        default:
        case SHFL_CF_ACCESS_ATTR_NONE:
        {
            fOpen |= RTFILE_O_ACCESS_ATTR_DEFAULT;
            Log(("FLAG: SHFL_CF_ACCESS_ATTR_NONE\n"));
            break;
        }

        case SHFL_CF_ACCESS_ATTR_READ:
        {
            fOpen |= RTFILE_O_ACCESS_ATTR_READ;
            Log(("FLAG: SHFL_CF_ACCESS_ATTR_READ\n"));
            break;
        }

        case SHFL_CF_ACCESS_ATTR_WRITE:
        {
            fOpen |= RTFILE_O_ACCESS_ATTR_WRITE;
            Log(("FLAG: SHFL_CF_ACCESS_ATTR_WRITE\n"));
            break;
        }

        case SHFL_CF_ACCESS_ATTR_READWRITE:
        {
            fOpen |= RTFILE_O_ACCESS_ATTR_READWRITE;
            Log(("FLAG: SHFL_CF_ACCESS_ATTR_READWRITE\n"));
            break;
        }
    }

    /* Sharing mask */
    switch (BIT_FLAG(fShflFlags, SHFL_CF_ACCESS_MASK_DENY))
    {
        default:
        case SHFL_CF_ACCESS_DENYNONE:
            fOpen |= RTFILE_O_DENY_NONE;
            Log(("FLAG: SHFL_CF_ACCESS_DENYNONE\n"));
            break;

        case SHFL_CF_ACCESS_DENYREAD:
            fOpen |= RTFILE_O_DENY_READ;
            Log(("FLAG: SHFL_CF_ACCESS_DENYREAD\n"));
            break;

        case SHFL_CF_ACCESS_DENYWRITE:
            fOpen |= RTFILE_O_DENY_WRITE;
            Log(("FLAG: SHFL_CF_ACCESS_DENYWRITE\n"));
            break;

        case SHFL_CF_ACCESS_DENYALL:
            fOpen |= RTFILE_O_DENY_ALL;
            Log(("FLAG: SHFL_CF_ACCESS_DENYALL\n"));
            break;
    }

    /* Open/Create action mask */
    switch (BIT_FLAG(fShflFlags, SHFL_CF_ACT_MASK_IF_EXISTS))
    {
        case SHFL_CF_ACT_OPEN_IF_EXISTS:
            if (SHFL_CF_ACT_CREATE_IF_NEW == BIT_FLAG(fShflFlags, SHFL_CF_ACT_MASK_IF_NEW))
            {
                fOpen |= RTFILE_O_OPEN_CREATE;
                Log(("FLAGS: SHFL_CF_ACT_OPEN_IF_EXISTS and SHFL_CF_ACT_CREATE_IF_NEW\n"));
            }
            else if (SHFL_CF_ACT_FAIL_IF_NEW == BIT_FLAG(fShflFlags, SHFL_CF_ACT_MASK_IF_NEW))
            {
                fOpen |= RTFILE_O_OPEN;
                Log(("FLAGS: SHFL_CF_ACT_OPEN_IF_EXISTS and SHFL_CF_ACT_FAIL_IF_NEW\n"));
            }
            else
            {
                Log(("FLAGS: invalid open/create action combination\n"));
                rc = VERR_INVALID_PARAMETER;
            }
            break;
        case SHFL_CF_ACT_FAIL_IF_EXISTS:
            if (SHFL_CF_ACT_CREATE_IF_NEW == BIT_FLAG(fShflFlags, SHFL_CF_ACT_MASK_IF_NEW))
            {
                fOpen |= RTFILE_O_CREATE;
                Log(("FLAGS: SHFL_CF_ACT_FAIL_IF_EXISTS and SHFL_CF_ACT_CREATE_IF_NEW\n"));
            }
            else
            {
                Log(("FLAGS: invalid open/create action combination\n"));
                rc = VERR_INVALID_PARAMETER;
            }
            break;
        case SHFL_CF_ACT_REPLACE_IF_EXISTS:
            if (SHFL_CF_ACT_CREATE_IF_NEW == BIT_FLAG(fShflFlags, SHFL_CF_ACT_MASK_IF_NEW))
            {
                fOpen |= RTFILE_O_CREATE_REPLACE;
                Log(("FLAGS: SHFL_CF_ACT_REPLACE_IF_EXISTS and SHFL_CF_ACT_CREATE_IF_NEW\n"));
            }
            else if (SHFL_CF_ACT_FAIL_IF_NEW == BIT_FLAG(fShflFlags, SHFL_CF_ACT_MASK_IF_NEW))
            {
                fOpen |= RTFILE_O_OPEN | RTFILE_O_TRUNCATE;
                Log(("FLAGS: SHFL_CF_ACT_REPLACE_IF_EXISTS and SHFL_CF_ACT_FAIL_IF_NEW\n"));
            }
            else
            {
                Log(("FLAGS: invalid open/create action combination\n"));
                rc = VERR_INVALID_PARAMETER;
            }
            break;
        case SHFL_CF_ACT_OVERWRITE_IF_EXISTS:
            if (SHFL_CF_ACT_CREATE_IF_NEW == BIT_FLAG(fShflFlags, SHFL_CF_ACT_MASK_IF_NEW))
            {
                fOpen |= RTFILE_O_CREATE_REPLACE;
                Log(("FLAGS: SHFL_CF_ACT_OVERWRITE_IF_EXISTS and SHFL_CF_ACT_CREATE_IF_NEW\n"));
            }
            else if (SHFL_CF_ACT_FAIL_IF_NEW == BIT_FLAG(fShflFlags, SHFL_CF_ACT_MASK_IF_NEW))
            {
                fOpen |= RTFILE_O_OPEN | RTFILE_O_TRUNCATE;
                Log(("FLAGS: SHFL_CF_ACT_OVERWRITE_IF_EXISTS and SHFL_CF_ACT_FAIL_IF_NEW\n"));
            }
            else
            {
                Log(("FLAGS: invalid open/create action combination\n"));
                rc = VERR_INVALID_PARAMETER;
            }
            break;
        default:
            rc = VERR_INVALID_PARAMETER;
            Log(("FLAG: SHFL_CF_ACT_MASK_IF_EXISTS - invalid parameter\n"));
    }

    if (RT_SUCCESS(rc))
    {
        if (!fWritable)
            fOpen &= ~RTFILE_O_WRITE;

        *pfOpen = fOpen;
    }
    return rc;
}

/**
 * Open a file or create and open a new one.
 *
 * @returns IPRT status code
 * @param  pClient  Data structure describing the client accessing the shared folder
 * @param  root     The index of the shared folder in the table of mappings.
 * @param  pszPath  Path to the file or folder on the host.
 * @param  pParms   Input:
 *                    - @a CreateFlags: Creation or open parameters, see include/VBox/shflsvc.h
 *                    - @a Info:        When a new file is created this specifies the initial parameters.
 *                                      When a file is created or overwritten, it also specifies the
 *                                      initial size.
 *                  Output:
 *                    - @a Result:      Shared folder status code, see include/VBox/shflsvc.h
 *                    - @a Handle:      On success the (shared folder) handle of the file opened or
 *                                      created
 *                    - @a Info:        On success the parameters of the file opened or created
 */
static int vbsfOpenFile(SHFLCLIENTDATA *pClient, SHFLROOT root, char *pszPath, SHFLCREATEPARMS *pParms)
{
    LogFlow(("vbsfOpenFile: pszPath = %s, pParms = %p\n", pszPath, pParms));
    Log(("SHFL create flags %08x\n", pParms->CreateFlags));

    RTFILEACTION    enmActionTaken = RTFILEACTION_INVALID;
    SHFLHANDLE      handle         = SHFL_HANDLE_NIL;
    SHFLFILEHANDLE *pHandle        = NULL;

    /* is the guest allowed to write to this share? */
    bool fWritable;
    int rc = vbsfMappingsQueryWritable(pClient, root, &fWritable);
    if (RT_FAILURE(rc))
        fWritable = false;

    uint64_t fOpen = 0;
    rc = vbsfConvertFileOpenFlags(fWritable, pParms->CreateFlags, pParms->Info.Attr.fMode, pParms->Handle, &fOpen);
    if (RT_SUCCESS(rc))
    {
        rc = VERR_NO_MEMORY;  /* Default error. */
        handle  = vbsfAllocFileHandle(pClient);
        if (handle != SHFL_HANDLE_NIL)
        {
            pHandle = vbsfQueryFileHandle(pClient, handle);
            if (pHandle)
            {
                pHandle->root = root;
                pHandle->file.fOpenFlags = fOpen;
                rc = RTFileOpenEx(pszPath, fOpen, &pHandle->file.Handle, &enmActionTaken);
            }
        }
    }
    bool fNoError = false;
    if (RT_FAILURE(rc))
    {
        switch (rc)
        {
            case VERR_FILE_NOT_FOUND:
                pParms->Result = SHFL_FILE_NOT_FOUND;
#ifndef RT_OS_WINDOWS
                if (   SHFL_CLIENT_NEED_WINDOWS_ERROR_STYLE_ADJUST_ON_POSIX(pClient)
                    && vbsfErrorStyleIsWindowsPathNotFound(pszPath))
                    pParms->Result = SHFL_PATH_NOT_FOUND;
#endif
                /* This actually isn't an error, so correct the rc before return later,
                   because the driver (VBoxSF.sys) expects rc = VINF_SUCCESS and checks the result code. */
                fNoError = true;
                break;

            case VERR_PATH_NOT_FOUND:
#ifndef RT_OS_WINDOWS
                if (   SHFL_CLIENT_NEED_WINDOWS_ERROR_STYLE_ADJUST_ON_POSIX(pClient)
                    && vbsfErrorStyleIsWindowsInvalidNameForNonDir(pszPath))
                {
                    rc = VERR_INVALID_NAME;
                    pParms->Result = SHFL_NO_RESULT;
                    break;
                }
#endif
                pParms->Result = SHFL_PATH_NOT_FOUND;
                fNoError = true; /* Not an error either (see above). */
                break;

            case VERR_ALREADY_EXISTS:
            {
                RTFSOBJINFO info;

                /** @todo Possible race left here. */
                if (RT_SUCCESS(RTPathQueryInfoEx(pszPath, &info, RTFSOBJATTRADD_NOTHING, SHFL_RT_LINK(pClient))))
                {
#ifdef RT_OS_WINDOWS
                    info.Attr.fMode |= 0111;
#endif
                    vbfsCopyFsObjInfoFromIprt(&pParms->Info, &info);
                }
                pParms->Result = SHFL_FILE_EXISTS;

                /* This actually isn't an error, so correct the rc before return later,
                   because the driver (VBoxSF.sys) expects rc = VINF_SUCCESS and checks the result code. */
                fNoError = true;
                break;
            }

            case VERR_TOO_MANY_OPEN_FILES:
            {
                static int s_cErrors;
                if (s_cErrors < 32)
                {
                    LogRel(("SharedFolders host service: Cannot open '%s' -- too many open files.\n", pszPath));
#if defined RT_OS_LINUX || defined(RT_OS_SOLARIS)
                    if (s_cErrors < 1)
                        LogRel(("SharedFolders host service: Try to increase the limit for open files (ulimit -n)\n"));
#endif
                    s_cErrors++;
                }
                pParms->Result = SHFL_NO_RESULT;
                break;
            }

            default:
                pParms->Result = SHFL_NO_RESULT;
        }
    }
    else
    {
        switch (enmActionTaken)
        {
            default:
                AssertFailed();
                RT_FALL_THRU();
            case RTFILEACTION_OPENED:
                pParms->Result = SHFL_FILE_EXISTS;
                break;
            case RTFILEACTION_CREATED:
                pParms->Result = SHFL_FILE_CREATED;
                break;
            case RTFILEACTION_REPLACED:
            case RTFILEACTION_TRUNCATED: /* not quite right */
                pParms->Result = SHFL_FILE_REPLACED;
                break;
        }

        if (   (pParms->CreateFlags & SHFL_CF_ACT_MASK_IF_EXISTS) == SHFL_CF_ACT_REPLACE_IF_EXISTS
            || (pParms->CreateFlags & SHFL_CF_ACT_MASK_IF_EXISTS) == SHFL_CF_ACT_OVERWRITE_IF_EXISTS)
        {
            /* For now, we do not treat a failure here as fatal. */
            /** @todo Also set the size for SHFL_CF_ACT_CREATE_IF_NEW if SHFL_CF_ACT_FAIL_IF_EXISTS is set. */
            /** @todo r=bird: Exactly document cbObject usage and see what we can get
             *        away with here.  I suspect it is only needed for windows and only
             *        with SHFL_FILE_CREATED and SHFL_FILE_REPLACED, and only if
             *        cbObject is non-zero. */
            RTFileSetSize(pHandle->file.Handle, pParms->Info.cbObject);
        }
#if 0
        /** @todo */
        /* Set new attributes. */
        if (   (   SHFL_CF_ACT_REPLACE_IF_EXISTS
                == BIT_FLAG(pParms->CreateFlags, SHFL_CF_ACT_MASK_IF_EXISTS))
            || (   SHFL_CF_ACT_CREATE_IF_NEW
                == BIT_FLAG(pParms->CreateFlags, SHFL_CF_ACT_MASK_IF_NEW)))
        {
            RTFileSetTimes(pHandle->file.Handle,
                          &pParms->Info.AccessTime,
                          &pParms->Info.ModificationTime,
                          &pParms->Info.ChangeTime,
                          &pParms->Info.BirthTime
                          );

            RTFileSetMode (pHandle->file.Handle, pParms->Info.Attr.fMode);
        }
#endif
        RTFSOBJINFO info;

        /* Get file information */
        rc = RTFileQueryInfo(pHandle->file.Handle, &info, RTFSOBJATTRADD_NOTHING);
        if (RT_SUCCESS(rc))
        {
#ifdef RT_OS_WINDOWS
            info.Attr.fMode |= 0111;
#endif
            vbfsCopyFsObjInfoFromIprt(&pParms->Info, &info);
        }
    }
    /* Free resources if any part of the function has failed. */
    if (RT_FAILURE(rc))
    {
        if (   (0 != pHandle)
            && (NIL_RTFILE != pHandle->file.Handle)
            && (0 != pHandle->file.Handle))
        {
            RTFileClose(pHandle->file.Handle);
            pHandle->file.Handle = NIL_RTFILE;
        }
        if (SHFL_HANDLE_NIL != handle)
        {
            vbsfFreeFileHandle(pClient, handle);
        }
        pParms->Handle = SHFL_HANDLE_NIL;
    }
    else
    {
        pParms->Handle = handle;
    }

    /* Report the driver that all is okay, we're done here */
    if (fNoError)
        rc = VINF_SUCCESS;

    LogFlow(("vbsfOpenFile: rc = %Rrc\n", rc));
    return rc;
}

/**
 * Open a folder or create and open a new one.
 *
 * @returns IPRT status code
 * @param  pClient  Data structure describing the client accessing the shared
 *                  folder
 * @param  root     The index of the shared folder in the table of mappings.
 * @param  pszPath  Path to the file or folder on the host.
 * @param  pParms   Input: @a CreateFlags Creation or open parameters, see
 *                  include/VBox/shflsvc.h
 *                  Output:
 *                    - @a Result: Shared folder status code, see include/VBox/shflsvc.h
 *                    - @a Handle: On success the (shared folder) handle of the folder opened or
 *                                 created
 *                    - @a Info:   On success the parameters of the folder opened or created
 *
 * @note folders are created with fMode = 0777
 */
static int vbsfOpenDir(SHFLCLIENTDATA *pClient, SHFLROOT root, char *pszPath,
                       SHFLCREATEPARMS *pParms)
{
    LogFlow(("vbsfOpenDir: pszPath = %s, pParms = %p\n", pszPath, pParms));
    Log(("SHFL create flags %08x\n", pParms->CreateFlags));

    int rc = VERR_NO_MEMORY;
    SHFLHANDLE      handle = vbsfAllocDirHandle(pClient);
    SHFLFILEHANDLE *pHandle = vbsfQueryDirHandle(pClient, handle);
    if (0 != pHandle)
    {
        pHandle->root = root;
        pParms->Result = SHFL_FILE_EXISTS;  /* May be overwritten with SHFL_FILE_CREATED. */
        /** @todo Can anyone think of a sensible, race-less way to do this?  Although
                  I suspect that the race is inherent, due to the API available... */
        /* Try to create the folder first if "create if new" is specified.  If this
           fails, and "open if exists" is specified, then we ignore the failure and try
           to open the folder anyway. */
        if (   SHFL_CF_ACT_CREATE_IF_NEW
            == BIT_FLAG(pParms->CreateFlags, SHFL_CF_ACT_MASK_IF_NEW))
        {
            /** @todo render supplied attributes.
            * bird: The guest should specify this. For windows guests RTFS_DOS_DIRECTORY should suffice. */
            RTFMODE fMode = 0777;

            pParms->Result = SHFL_FILE_CREATED;
            rc = RTDirCreate(pszPath, fMode, 0);
            if (RT_FAILURE(rc))
            {
                /** @todo we still return 'rc' as failure here, so this is mostly pointless.  */
                switch (rc)
                {
                    case VERR_ALREADY_EXISTS:
                        pParms->Result = SHFL_FILE_EXISTS;
                        break;
                    case VERR_PATH_NOT_FOUND:
                        pParms->Result = SHFL_PATH_NOT_FOUND;
                        break;
                    case VERR_FILE_NOT_FOUND: /* may happen on posix */
                        pParms->Result = SHFL_FILE_NOT_FOUND;
#ifndef RT_OS_WINDOWS
                        if (   SHFL_CLIENT_NEED_WINDOWS_ERROR_STYLE_ADJUST_ON_POSIX(pClient)
                            && vbsfErrorStyleIsWindowsPathNotFound(pszPath))
                        {
                            pParms->Result = SHFL_PATH_NOT_FOUND;
                            rc = VERR_PATH_NOT_FOUND;
                        }
#endif
                        break;
                    default:
                        pParms->Result = SHFL_NO_RESULT;
                }
            }
        }
        else
            rc = VINF_SUCCESS;
        if (   RT_SUCCESS(rc)
            || (SHFL_CF_ACT_OPEN_IF_EXISTS == BIT_FLAG(pParms->CreateFlags, SHFL_CF_ACT_MASK_IF_EXISTS)))
        {
            /* Open the directory now */
            rc = RTDirOpenFiltered(&pHandle->dir.Handle, pszPath, RTDIRFILTER_NONE, 0 /*fFlags*/);
            if (RT_SUCCESS(rc))
            {
                RTFSOBJINFO info;

                rc = RTDirQueryInfo(pHandle->dir.Handle, &info, RTFSOBJATTRADD_NOTHING);
                if (RT_SUCCESS(rc))
                {
                    vbfsCopyFsObjInfoFromIprt(&pParms->Info, &info);
                }
            }
            else
            {
                /** @todo we still return 'rc' as failure here, so this is mostly pointless.  */
                switch (rc)
                {
                    case VERR_FILE_NOT_FOUND:
                        pParms->Result = SHFL_FILE_NOT_FOUND;
#ifndef RT_OS_WINDOWS
                        if (   SHFL_CLIENT_NEED_WINDOWS_ERROR_STYLE_ADJUST_ON_POSIX(pClient)
                            && vbsfErrorStyleIsWindowsPathNotFound(pszPath))
                        {
                            pParms->Result = SHFL_PATH_NOT_FOUND;
                            rc = VERR_PATH_NOT_FOUND;
                        }
#endif
                        break;
                    case VERR_PATH_NOT_FOUND:
                        pParms->Result = SHFL_PATH_NOT_FOUND;
#ifndef RT_OS_WINDOWS
                        if (   SHFL_CLIENT_NEED_WINDOWS_ERROR_STYLE_ADJUST_ON_POSIX(pClient)
                            && vbsfErrorStyleIsWindowsNotADirectory(pszPath))
                        {
                            pParms->Result = SHFL_FILE_EXISTS;
                            rc = VERR_NOT_A_DIRECTORY;
                            break;
                        }
#endif
                        break;
                    case VERR_ACCESS_DENIED:
                        pParms->Result = SHFL_FILE_EXISTS;
                        break;
                    default:
                        pParms->Result = SHFL_NO_RESULT;
                }
            }
        }
    }
    if (RT_FAILURE(rc))
    {
        if (   (0 != pHandle)
            && (0 != pHandle->dir.Handle))
        {
            RTDirClose(pHandle->dir.Handle);
            pHandle->dir.Handle = 0;
        }
        if (SHFL_HANDLE_NIL != handle)
        {
            vbsfFreeFileHandle(pClient, handle);
        }
        pParms->Handle = SHFL_HANDLE_NIL;
    }
    else
    {
        pParms->Handle = handle;
    }
    LogFlow(("vbsfOpenDir: rc = %Rrc\n", rc));
    return rc;
}

static int vbsfCloseDir(SHFLFILEHANDLE *pHandle)
{
    int rc = VINF_SUCCESS;

    LogFlow(("vbsfCloseDir: Handle = %08X Search Handle = %08X\n",
             pHandle->dir.Handle, pHandle->dir.SearchHandle));

    RTDirClose(pHandle->dir.Handle);

    if (pHandle->dir.SearchHandle)
        RTDirClose(pHandle->dir.SearchHandle);

    if (pHandle->dir.pLastValidEntry)
    {
        RTMemFree(pHandle->dir.pLastValidEntry);
        pHandle->dir.pLastValidEntry = NULL;
    }

    LogFlow(("vbsfCloseDir: rc = %d\n", rc));

    return rc;
}


static int vbsfCloseFile(SHFLFILEHANDLE *pHandle)
{
    int rc = VINF_SUCCESS;

    LogFlow(("vbsfCloseFile: Handle = %08X\n",
             pHandle->file.Handle));

    rc = RTFileClose(pHandle->file.Handle);

    LogFlow(("vbsfCloseFile: rc = %d\n", rc));

    return rc;
}

/**
 * Look up file or folder information by host path.
 *
 * @returns iprt status code (currently VINF_SUCCESS)
 * @param   pClient    client data
 * @param   pszPath    The path of the file to be looked up
 * @param   pParms     Output:
 *                      - @a Result: Status of the operation (success or error)
 *                      - @a Info:   On success, information returned about the
 *                                   file
 */
static int vbsfLookupFile(SHFLCLIENTDATA *pClient, char *pszPath, SHFLCREATEPARMS *pParms)
{
    RTFSOBJINFO info;
    int rc;

    rc = RTPathQueryInfoEx(pszPath, &info, RTFSOBJATTRADD_NOTHING, SHFL_RT_LINK(pClient));
    LogFlow(("SHFL_CF_LOOKUP\n"));
    /* Client just wants to know if the object exists. */
    switch (rc)
    {
        case VINF_SUCCESS:
        {
#ifdef RT_OS_WINDOWS
            info.Attr.fMode |= 0111;
#endif
            vbfsCopyFsObjInfoFromIprt(&pParms->Info, &info);
            pParms->Result = SHFL_FILE_EXISTS;
            break;
        }

        case VERR_FILE_NOT_FOUND:
        {
            pParms->Result = SHFL_FILE_NOT_FOUND;
            rc = VINF_SUCCESS;
            break;
        }

        case VERR_PATH_NOT_FOUND:
        {
            pParms->Result = SHFL_PATH_NOT_FOUND;
            rc = VINF_SUCCESS;
            break;
        }
    }
    pParms->Handle = SHFL_HANDLE_NIL;
    return rc;
}

#ifdef UNITTEST
/** Unit test the SHFL_FN_CREATE API.  Located here as a form of API
 * documentation. */
void testCreate(RTTEST hTest)
{
    /* Simple opening of an existing file. */
    testCreateFileSimple(hTest);
    testCreateFileSimpleCaseInsensitive(hTest);
    /* Simple opening of an existing directory. */
    /** @todo How do wildcards in the path name work? */
    testCreateDirSimple(hTest);
    /* If the number or types of parameters are wrong the API should fail. */
    testCreateBadParameters(hTest);
    /* Add tests as required... */
}
#endif

/**
 * Create or open a file or folder.  Perform character set and case
 * conversion on the file name if necessary.
 *
 * @returns IPRT status code, but see note below
 * @param   pClient     Data structure describing the client accessing the
 *                      shared folder
 * @param   root        The index of the shared folder in the table of mappings.
 *                      The host path of the shared folder is found using this.
 * @param   pPath       The path of the file or folder relative to the host path
 *                      indexed by root.
 * @param   cbPath      Presumably the length of the path in pPath. Actually
 *                      ignored, as pPath contains a length parameter.
 * @param   pParms      Input: If a new file is created or an old one
 *                      overwritten, set the @a Info attribute.
 *
 *                      Output:
 *                        - @a Result Shared folder result code, see include/VBox/shflsvc.h
 *                        - @a Handle Shared folder handle to the newly opened file
 *                        - @a Info Attributes of the file or folder opened
 *
 * @note This function returns success if a "non-exceptional" error occurred,
 *       such as "no such file".  In this case, the caller should check the
 *       pParms->Result return value and whether pParms->Handle is valid.
 */
int vbsfCreate(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLSTRING *pPath, uint32_t cbPath, SHFLCREATEPARMS *pParms)
{
    int rc = VINF_SUCCESS;

    LogFlow(("vbsfCreate: pClient = %p, pPath = %p, cbPath = %d, pParms = %p CreateFlags=%x\n",
             pClient, pPath, cbPath, pParms, pParms->CreateFlags));

    /* Check the client access rights to the root. */
    /** @todo */

    /* Build a host full path for the given path, handle file name case issues (if the guest
     * expects case-insensitive paths but the host is case-sensitive) and convert utf16 to utf8 if
     * necessary.
     */
    char *pszFullPath = NULL;
    uint32_t cbFullPathRoot = 0;

    rc = vbsfBuildFullPath(pClient, root, pPath, cbPath, &pszFullPath, &cbFullPathRoot);
    if (RT_SUCCESS(rc))
    {
        /* Reset return value in case client forgot to do so.
         * pParms->Handle must not be reset here, as it is used
         * in vbsfOpenFile to detect old additions.
         */
        pParms->Result = SHFL_NO_RESULT;

        if (BIT_FLAG(pParms->CreateFlags, SHFL_CF_LOOKUP))
        {
            rc = vbsfLookupFile(pClient, pszFullPath, pParms);
        }
        else
        {
            /* Query path information. */
            RTFSOBJINFO info;

            rc = RTPathQueryInfoEx(pszFullPath, &info, RTFSOBJATTRADD_NOTHING, SHFL_RT_LINK(pClient));
            LogFlow(("RTPathQueryInfoEx returned %Rrc\n", rc));

            if (RT_SUCCESS(rc))
            {
                /* Mark it as a directory in case the caller didn't. */
                /**
                  * @todo I left this in in order not to change the behaviour of the
                  *       function too much.  Is it really needed, and should it really be
                  *       here?
                  */
                if (BIT_FLAG(info.Attr.fMode, RTFS_DOS_DIRECTORY))
                {
                    pParms->CreateFlags |= SHFL_CF_DIRECTORY;
                }

                /**
                  * @todo This should be in the Windows Guest Additions, as no-one else
                  *       needs it.
                  */
                if (BIT_FLAG(pParms->CreateFlags, SHFL_CF_OPEN_TARGET_DIRECTORY))
                {
                    vbsfStripLastComponent(pszFullPath, cbFullPathRoot);
                    pParms->CreateFlags &= ~SHFL_CF_ACT_MASK_IF_EXISTS;
                    pParms->CreateFlags &= ~SHFL_CF_ACT_MASK_IF_NEW;
                    pParms->CreateFlags |= SHFL_CF_DIRECTORY;
                    pParms->CreateFlags |= SHFL_CF_ACT_OPEN_IF_EXISTS;
                    pParms->CreateFlags |= SHFL_CF_ACT_FAIL_IF_NEW;
                }
            }

            rc = VINF_SUCCESS;

            /* Note: do not check the SHFL_CF_ACCESS_WRITE here, only check if the open operation
             * will cause changes.
             *
             * Actual operations (write, set attr, etc), which can write to a shared folder, have
             * the check and will return VERR_WRITE_PROTECT if the folder is not writable.
             */
            if (   (pParms->CreateFlags & SHFL_CF_ACT_MASK_IF_EXISTS) == SHFL_CF_ACT_REPLACE_IF_EXISTS
                || (pParms->CreateFlags & SHFL_CF_ACT_MASK_IF_EXISTS) == SHFL_CF_ACT_OVERWRITE_IF_EXISTS
                || (pParms->CreateFlags & SHFL_CF_ACT_MASK_IF_NEW) == SHFL_CF_ACT_CREATE_IF_NEW
               )
            {
                /* is the guest allowed to write to this share? */
                bool fWritable;
                rc = vbsfMappingsQueryWritable(pClient, root, &fWritable);
                if (RT_FAILURE(rc) || !fWritable)
                    rc = VERR_WRITE_PROTECT;
            }

            if (RT_SUCCESS(rc))
            {
                if (BIT_FLAG(pParms->CreateFlags, SHFL_CF_DIRECTORY))
                {
                    rc = vbsfOpenDir(pClient, root, pszFullPath, pParms);
                }
                else
                {
                    rc = vbsfOpenFile(pClient, root, pszFullPath, pParms);
                }
            }
            else
            {
                pParms->Handle = SHFL_HANDLE_NIL;
            }
        }

        /* free the path string */
        vbsfFreeFullPath(pszFullPath);
    }

    Log(("vbsfCreate: handle = %RX64 rc = %Rrc result=%x\n", (uint64_t)pParms->Handle, rc, pParms->Result));

    return rc;
}

#ifdef UNITTEST
/** Unit test the SHFL_FN_CLOSE API.  Located here as a form of API
 * documentation. */
void testClose(RTTEST hTest)
{
    /* If the API parameters are invalid the API should fail. */
    testCloseBadParameters(hTest);
    /* Add tests as required... */
}
#endif

int vbsfClose(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle)
{
    LogFunc(("pClient = %p, root 0x%RX32, Handle = 0x%RX64\n",
             pClient, root, Handle));

    int rc = VERR_INVALID_HANDLE;
    uint32_t type = vbsfQueryHandleType(pClient, Handle);
    Assert((type & ~(SHFL_HF_TYPE_DIR | SHFL_HF_TYPE_FILE)) == 0);
    switch (type & (SHFL_HF_TYPE_DIR | SHFL_HF_TYPE_FILE))
    {
        case SHFL_HF_TYPE_DIR:
        {
            SHFLFILEHANDLE *pHandle = vbsfQueryDirHandle(pClient, Handle);
            if (RT_LIKELY(pHandle && root == pHandle->root))
            {
                rc = vbsfCloseDir(pHandle);
                vbsfFreeFileHandle(pClient, Handle);
            }
            break;
        }
        case SHFL_HF_TYPE_FILE:
        {
            SHFLFILEHANDLE *pHandle = vbsfQueryFileHandle(pClient, Handle);
            if (RT_LIKELY(pHandle && root == pHandle->root))
            {
                rc = vbsfCloseFile(pHandle);
                vbsfFreeFileHandle(pClient, Handle);
            }
            break;
        }
        default:
            break;
    }

    LogFunc(("rc = %Rrc\n", rc));
    return rc;
}

/**
 * Helper for vbsfReadPages and vbsfWritePages that creates a S/G buffer from a
 * pages parameter.
 */
static int vbsfPagesToSgBuf(VBOXHGCMSVCPARMPAGES const *pPages, uint32_t cbLeft, PRTSGBUF pSgBuf)
{
    PRTSGSEG paSegs = (PRTSGSEG)RTMemTmpAlloc(sizeof(paSegs[0]) * pPages->cPages);
    if (paSegs)
    {
        /*
         * Convert the pages to segments.
         */
        uint32_t iSeg  = 0;
        uint32_t iPage = 0;
        for (;;)
        {
            Assert(iSeg < pPages->cPages);
            Assert(iPage < pPages->cPages);

            /** @todo r=aeichner This was just using PAGE_SIZE before which doesn't work if the host and guest use different
             * page sizes (think of 16KiB on macOS.arm64 vs linux.arm64) causing data corruption, yay! VMMDev now imposes a page size of 4KiB and
             * VBOXHGCMSVCPARMPAGES should really have a cbPage member to indicate the used page size instead of dragging
             * in VMMDev.h here. OTOH the size of VBOXHGCMSVCPARMPAGES can be part of a saved state which would mean increasing
             * the HGCM saved state version... */

            /* Current page. */
            void *pvSeg;
            paSegs[iSeg].pvSeg = pvSeg = pPages->papvPages[iPage];
            uint32_t cbSeg = VMMDEV_PAGE_SIZE - (uint32_t)((uintptr_t)pvSeg & VMMDEV_PAGE_OFFSET_MASK);
            iPage++;

            /* Adjacent to the next page? */
            while (   iPage < pPages->cPages
                   && (uintptr_t)pvSeg + cbSeg == (uintptr_t)pPages->papvPages[iPage])
            {
                iPage++;
                cbSeg += VMMDEV_PAGE_SIZE;
            }

            /* Adjust for max size. */
            if (cbLeft <= cbSeg)
            {
                paSegs[iSeg++].cbSeg = cbLeft;
                break;
            }
            paSegs[iSeg++].cbSeg = cbSeg;
            cbLeft -= cbSeg;
        }

        /*
         * Initialize the s/g buffer and execute the read.
         */
        RTSgBufInit(pSgBuf, paSegs, iSeg);
        return VINF_SUCCESS;
    }
    pSgBuf->paSegs = NULL;
    return VERR_NO_TMP_MEMORY;
}


#ifdef UNITTEST
/** Unit test the SHFL_FN_READ API.  Located here as a form of API
 * documentation. */
void testRead(RTTEST hTest)
{
    /* If the number or types of parameters are wrong the API should fail. */
    testReadBadParameters(hTest);
    /* Basic reading from a file. */
    testReadFileSimple(hTest);
    /* Add tests as required... */
}
#endif
int vbsfRead(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle, uint64_t offset, uint32_t *pcbBuffer, uint8_t *pBuffer)
{
    LogFunc(("pClient %p, root 0x%RX32, Handle 0x%RX64, offset 0x%RX64, bytes 0x%RX32\n",
             pClient, root, Handle, offset, pcbBuffer? *pcbBuffer: 0));

    AssertPtrReturn(pClient, VERR_INVALID_PARAMETER);

    SHFLFILEHANDLE *pHandle = vbsfQueryFileHandle(pClient, Handle);
    int rc = vbsfCheckHandleAccess(pClient, root, pHandle, VBSF_CHECK_ACCESS_READ);
    if (RT_SUCCESS(rc))
    {
        size_t const cbToRead = *pcbBuffer;
        if (RT_LIKELY(cbToRead > 0))
        {
            size_t cbActual = 0;
            rc = RTFileReadAt(pHandle->file.Handle, offset, pBuffer, cbToRead, &cbActual);
            *pcbBuffer = (uint32_t)cbActual;
        }
        else
        {
            /* Reading zero bytes always succeeds. */
            rc = VINF_SUCCESS;
        }
    }
    else
        *pcbBuffer = 0;

    LogFunc(("%Rrc bytes read 0x%RX32\n", rc, *pcbBuffer));
    return rc;
}

/**
 * SHFL_FN_READ w/o bounce buffering.
 */
int vbsfReadPages(SHFLCLIENTDATA *pClient, SHFLROOT idRoot, SHFLHANDLE hFile, uint64_t offFile,
                  uint32_t *pcbRead, PVBOXHGCMSVCPARMPAGES pPages)
{
    LogFunc(("pClient %p, idRoot %#RX32, hFile %#RX64, offFile %#RX64, cbRead %#RX32, cPages %#x\n",
             pClient, idRoot, hFile, offFile, *pcbRead, pPages->cPages));

    AssertPtrReturn(pClient, VERR_INVALID_PARAMETER);

    size_t          cbTotal = 0;
    SHFLFILEHANDLE *pHandle = vbsfQueryFileHandle(pClient, hFile);
    int rc = vbsfCheckHandleAccess(pClient, idRoot, pHandle, VBSF_CHECK_ACCESS_READ);
    if (RT_SUCCESS(rc))
    {
        uint32_t const cbToRead = *pcbRead;
        if (cbToRead > 0)
        {
            ASSERT_GUEST_RETURN(pPages->cPages > 0, VERR_INTERNAL_ERROR_3);

            /*
             * Convert to a scatter-gather buffer.
             *
             * We need not do any platform specific code here as the RTSGBUF
             * segment array maps directly onto the posix iovec structure.
             * Windows does currently benefit much from this conversion, but
             * so be it.
             */
            RTSGBUF SgBuf;
            rc = vbsfPagesToSgBuf(pPages, cbToRead, &SgBuf);
            if (RT_SUCCESS(rc))
            {
                rc = RTFileSgReadAt(pHandle->file.Handle, offFile, &SgBuf, cbToRead, &cbTotal);
                while (rc == VERR_INTERRUPTED)
                {
                    RTSgBufReset(&SgBuf);
                    rc = RTFileSgReadAt(pHandle->file.Handle, offFile, &SgBuf, cbToRead, &cbTotal);
                }

                RTMemTmpFree((void *)SgBuf.paSegs);
            }
            else
                rc = VERR_NO_TMP_MEMORY;

            *pcbRead = (uint32_t)cbTotal;
        }
        else
        {
            /* Reading zero bytes always succeeds. */
            rc = VINF_SUCCESS;
        }
    }
    else
        *pcbRead = 0;

    LogFunc(("%Rrc bytes read %#zx\n", rc, cbTotal));
    return rc;
}

/**
 * Helps with writes to RTFILE_O_APPEND files.
 */
static uint64_t vbsfWriteCalcPostAppendFilePosition(RTFILE hFile, uint64_t offGuessed)
{
    RTFSOBJINFO ObjInfo;
    int rc2 = RTFileQueryInfo(hFile, &ObjInfo, RTFSOBJATTRADD_NOTHING);
    if (RT_SUCCESS(rc2) && (uint64_t)ObjInfo.cbObject >= offGuessed)
        return ObjInfo.cbObject;
    return offGuessed;
}

#ifdef UNITTEST
/** Unit test the SHFL_FN_WRITE API.  Located here as a form of API
 * documentation. */
void testWrite(RTTEST hTest)
{
    /* If the number or types of parameters are wrong the API should fail. */
    testWriteBadParameters(hTest);
    /* Simple test of writing to a file. */
    testWriteFileSimple(hTest);
    /* Add tests as required... */
}
#endif
int vbsfWrite(SHFLCLIENTDATA *pClient, SHFLROOT idRoot, SHFLHANDLE hFile, uint64_t *poffFile,
              uint32_t *pcbBuffer, uint8_t *pBuffer)
{
    uint64_t offFile = *poffFile;
    LogFunc(("pClient %p, root 0x%RX32, Handle 0x%RX64, offFile 0x%RX64, bytes 0x%RX32\n",
             pClient, idRoot, hFile, offFile, *pcbBuffer));

    AssertPtrReturn(pClient, VERR_INVALID_PARAMETER);

    SHFLFILEHANDLE *pHandle = vbsfQueryFileHandle(pClient, hFile);
    int rc = vbsfCheckHandleAccess(pClient, idRoot, pHandle, VBSF_CHECK_ACCESS_WRITE);
    if (RT_SUCCESS(rc))
    {
        size_t const cbToWrite = *pcbBuffer;
        if (RT_LIKELY(cbToWrite != 0))
        {
            size_t cbWritten = 0;
            if (!(pHandle->file.fOpenFlags & RTFILE_O_APPEND))
                rc = RTFileWriteAt(pHandle->file.Handle, offFile, pBuffer, cbToWrite, &cbWritten);
            else
            {
                rc = RTFileSeek(pHandle->file.Handle, offFile, RTFILE_SEEK_BEGIN, NULL);
                AssertRC(rc);
                if (RT_SUCCESS(rc))
                {
                    rc = RTFileWrite(pHandle->file.Handle, pBuffer, cbToWrite, &cbWritten);
                    *pcbBuffer = (uint32_t)cbWritten;
                }
            }

            /* Update the file offset (mainly for RTFILE_O_APPEND), */
            if (RT_SUCCESS(rc))
            {
                offFile += cbWritten;
                if (!(pHandle->file.fOpenFlags & RTFILE_O_APPEND))
                    *poffFile = offFile;
                else
                    *poffFile = vbsfWriteCalcPostAppendFilePosition(pHandle->file.Handle, offFile);
            }
        }
        else
        {
            /** @todo What writing zero bytes should do? */
            rc = VINF_SUCCESS;
        }
    }
    else
        *pcbBuffer = 0;
    LogFunc(("%Rrc bytes written 0x%RX32\n", rc, *pcbBuffer));
    return rc;
}

/**
 * SHFL_FN_WRITE w/o bounce buffering.
 */
int vbsfWritePages(SHFLCLIENTDATA *pClient, SHFLROOT idRoot, SHFLHANDLE hFile, uint64_t *poffFile,
                   uint32_t *pcbWrite, PVBOXHGCMSVCPARMPAGES pPages)
{
    uint64_t offFile = *poffFile;
    LogFunc(("pClient %p, idRoot %#RX32, hFile %#RX64, offFile %#RX64, cbWrite %#RX32, cPages %#x\n",
             pClient, idRoot, hFile, offFile, *pcbWrite, pPages->cPages));

    AssertPtrReturn(pClient, VERR_INVALID_PARAMETER);

    size_t          cbTotal = 0;
    SHFLFILEHANDLE *pHandle = vbsfQueryFileHandle(pClient, hFile);
    int rc = vbsfCheckHandleAccess(pClient, idRoot, pHandle, VBSF_CHECK_ACCESS_WRITE);
    if (RT_SUCCESS(rc))
    {
        uint32_t const cbToWrite = *pcbWrite;
        if (cbToWrite > 0)
        {
            ASSERT_GUEST_RETURN(pPages->cPages > 0, VERR_INTERNAL_ERROR_3);

            /*
             * Convert to a scatter-gather buffer.
             *
             * We need not do any platform specific code here as the RTSGBUF
             * segment array maps directly onto the posix iovec structure.
             * Windows does currently benefit much from this conversion, but
             * so be it.
             */
            RTSGBUF SgBuf;
            rc = vbsfPagesToSgBuf(pPages, cbToWrite, &SgBuf);
            if (RT_SUCCESS(rc))
            {
#ifndef RT_OS_LINUX
                /* Cannot use RTFileSgWriteAt or RTFileWriteAt when opened with
                   RTFILE_O_APPEND, except for on linux where the offset is
                   then ignored by the low level kernel API. */
                if (pHandle->file.fOpenFlags & RTFILE_O_APPEND)
                {
                    /* paranoia */
                    RTFileSeek(pHandle->file.Handle, 0, RTFILE_SEEK_END, NULL);

                    for (size_t iSeg = 0; iSeg < SgBuf.cSegs; iSeg++)
                    {
                        size_t cbWrittenNow = 0;
                        do
                            rc = RTFileWrite(pHandle->file.Handle, SgBuf.paSegs[iSeg].pvSeg,
                                             SgBuf.paSegs[iSeg].cbSeg, &cbWrittenNow);
                        while (rc == VERR_INTERRUPTED);
                        if (RT_SUCCESS(rc))
                        {
                            cbTotal += cbWrittenNow;
                            if (cbWrittenNow < SgBuf.paSegs[iSeg].cbSeg)
                                break;
                        }
                        else
                        {
                            if (cbTotal > 0)
                                rc = VINF_SUCCESS;
                            break;
                        }
                    }
                }
                else
#endif
                {
                    rc = RTFileSgWriteAt(pHandle->file.Handle, offFile, &SgBuf, cbToWrite, &cbTotal);
                    while (rc == VERR_INTERRUPTED)
                    {
                        RTSgBufReset(&SgBuf);
                        rc = RTFileSgWriteAt(pHandle->file.Handle, offFile, &SgBuf, cbToWrite, &cbTotal);
                    }
                }

                RTMemTmpFree((void *)SgBuf.paSegs);

                /* Update the file offset (mainly for RTFILE_O_APPEND), */
                if (RT_SUCCESS(rc))
                {
                    offFile += cbTotal;
                    if (!(pHandle->file.fOpenFlags & RTFILE_O_APPEND))
                        *poffFile = offFile;
                    else
                        *poffFile = vbsfWriteCalcPostAppendFilePosition(pHandle->file.Handle, offFile);
                }
            }
            else
                rc = VERR_NO_TMP_MEMORY;

            *pcbWrite = (uint32_t)cbTotal;
        }
        else
        {
            /* Writing zero bytes always succeeds. */
            rc = VINF_SUCCESS;
        }
    }
    else
        *pcbWrite = 0;

    LogFunc(("%Rrc bytes written %#zx\n", rc, cbTotal));
    return rc;
}

/**
 * Implements SHFL_FN_COPY_FILE_PART (wrapping RTFileCopyPart).
 */
int vbsfCopyFilePart(SHFLCLIENTDATA *pClient, SHFLROOT idRootSrc, SHFLHANDLE hFileSrc, uint64_t offSrc,
                     SHFLROOT idRootDst, SHFLHANDLE hFileDst, uint64_t offDst, uint64_t *pcbToCopy, uint32_t fFlags)
{
    /*
     * Validate and translates handles.
     */
    uint64_t const cbToCopy = *pcbToCopy;
    *pcbToCopy = 0;
    LogFunc(("pClient %p, idRootSrc %#RX32, hFileSrc %#RX64, offSrc %#RX64, idRootSrc %#RX32, hFileSrc %#RX64, offSrc %#RX64, cbToCopy %#RX64, fFlags %#x\n",
             pClient, idRootSrc, hFileSrc, offSrc, idRootDst, hFileDst, offDst, cbToCopy, fFlags));

    AssertPtrReturn(pClient, VERR_INVALID_PARAMETER);

    uint64_t cbTotal = 0;

    SHFLFILEHANDLE *pHandleSrc = vbsfQueryFileHandle(pClient, hFileSrc);
    int rc = vbsfCheckHandleAccess(pClient, idRootSrc, pHandleSrc, VBSF_CHECK_ACCESS_READ);
    if (RT_SUCCESS(rc))
    {
        SHFLFILEHANDLE *pHandleDst = vbsfQueryFileHandle(pClient, hFileDst);
        rc = vbsfCheckHandleAccess(pClient, idRootDst, pHandleDst, VBSF_CHECK_ACCESS_WRITE);
        if (RT_SUCCESS(rc))
        {
            /*
             * Do the job.
             */
            rc = RTFileCopyPart(pHandleSrc->file.Handle, offSrc, pHandleDst->file.Handle, offDst, cbToCopy, 0, &cbTotal);
            *pcbToCopy = cbTotal;
        }
    }

    RT_NOREF(fFlags);
    LogFunc(("%Rrc bytes written %#zx\n", rc, cbTotal));
    return rc;
}

#ifdef UNITTEST
/** Unit test the SHFL_FN_FLUSH API.  Located here as a form of API
 * documentation. */
void testFlush(RTTEST hTest)
{
    /* If the number or types of parameters are wrong the API should fail. */
    testFlushBadParameters(hTest);
    /* Simple opening and flushing of a file. */
    testFlushFileSimple(hTest);
    /* Add tests as required... */
}
#endif

int vbsfFlush(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle)
{
    LogFunc(("pClient %p, root 0x%RX32, Handle 0x%RX64\n",
             pClient, root, Handle));

    AssertPtrReturn(pClient, VERR_INVALID_PARAMETER);

    SHFLFILEHANDLE *pHandle = vbsfQueryFileHandle(pClient, Handle);
    int rc = vbsfCheckHandleAccess(pClient, root, pHandle, VBSF_CHECK_ACCESS_WRITE);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else
        return rc;

    rc = RTFileFlush(pHandle->file.Handle);

    LogFunc(("%Rrc\n", rc));
    return rc;
}

#ifdef UNITTEST
/** Unit test the SHFL_FN_LIST API.  Located here as a form of API
 * documentation. */
void testDirList(RTTEST hTest)
{
    /* If the number or types of parameters are wrong the API should fail. */
    testDirListBadParameters(hTest);
    /* Test listing an empty directory (simple edge case). */
    testDirListEmpty(hTest);
    /* Add tests as required... */
}
#endif
int vbsfDirList(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle, SHFLSTRING *pPath, uint32_t flags,
                uint32_t *pcbBuffer, uint8_t *pBuffer, uint32_t *pIndex, uint32_t *pcFiles)
{
    PRTDIRENTRYEX  pDirEntry = 0, pDirEntryOrg;
    uint32_t       cbDirEntry, cbBufferOrg;
    PSHFLDIRINFO   pSFDEntry;
    PRTUTF16       pwszString;
    RTDIR          hDir;
    const bool     fUtf8 = BIT_FLAG(pClient->fu32Flags, SHFL_CF_UTF8) != 0;

    AssertPtrReturn(pClient, VERR_INVALID_PARAMETER);

    SHFLFILEHANDLE *pHandle = vbsfQueryDirHandle(pClient, Handle);
    int rc = vbsfCheckHandleAccess(pClient, root, pHandle, VBSF_CHECK_ACCESS_READ);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else
        return rc;

    Assert(*pIndex == 0);

    cbDirEntry = 4096;
    pDirEntryOrg = pDirEntry  = (PRTDIRENTRYEX)RTMemAlloc(cbDirEntry);
    if (pDirEntry == 0)
    {
        AssertFailed();
        return VERR_NO_MEMORY;
    }

    cbBufferOrg = *pcbBuffer;
    *pcbBuffer  = 0;
    pSFDEntry   = (PSHFLDIRINFO)pBuffer;

    *pIndex = 1; /* not yet complete */
    *pcFiles = 0;

    if (!pPath)
        hDir = pHandle->dir.Handle;
    else
    {
        if (pHandle->dir.SearchHandle == 0)
        {
            /* Build a host full path for the given path
             * and convert utf16 to utf8 if necessary.
             */
            char *pszFullPath = NULL;

            Assert(pHandle->dir.pLastValidEntry == 0);

            rc = vbsfBuildFullPath(pClient, root, pPath, pPath->u16Size + SHFLSTRING_HEADER_SIZE, &pszFullPath, NULL, true);

            if (RT_SUCCESS(rc))
            {
                rc = RTDirOpenFiltered(&pHandle->dir.SearchHandle, pszFullPath, RTDIRFILTER_WINNT, 0 /*fFlags*/);

                /* free the path string */
                vbsfFreeFullPath(pszFullPath);

                if (RT_FAILURE(rc))
                    goto end;
            }
            else
                goto end;
            flags &= ~SHFL_LIST_RESTART;
        }
        Assert(pHandle->dir.SearchHandle);
        hDir = pHandle->dir.SearchHandle;
    }

    if (flags & SHFL_LIST_RESTART)
    {
        rc = RTDirRewind(hDir);
        if (RT_FAILURE(rc))
            goto end;
    }

    while (cbBufferOrg)
    {
        size_t cbDirEntrySize = cbDirEntry;
        uint32_t cbNeeded;

        /* Do we still have a valid last entry for the active search? If so, then return it here */
        if (pHandle->dir.pLastValidEntry)
        {
            pDirEntry = pHandle->dir.pLastValidEntry;
        }
        else
        {
            pDirEntry = pDirEntryOrg;

            rc = RTDirReadEx(hDir, pDirEntry, &cbDirEntrySize, RTFSOBJATTRADD_NOTHING, SHFL_RT_LINK(pClient));
            if (rc == VERR_NO_MORE_FILES)
            {
                *pIndex = 0; /* listing completed */
                break;
            }

            if (   rc != VINF_SUCCESS
                && rc != VWRN_NO_DIRENT_INFO)
            {
                //AssertFailed();
                if (   rc == VERR_NO_TRANSLATION
                    || rc == VERR_INVALID_UTF8_ENCODING)
                    continue;
                break;
            }
        }

        cbNeeded = RT_OFFSETOF(SHFLDIRINFO, name.String);
        if (fUtf8)
            cbNeeded += pDirEntry->cbName + 1;
        else
            /* Overestimating, but that's ok */
            cbNeeded += (pDirEntry->cbName + 1) * 2;

        if (cbBufferOrg < cbNeeded)
        {
            /* No room, so save this directory entry, or else it's lost forever */
            pHandle->dir.pLastValidEntry = pDirEntry;

            if (*pcFiles == 0)
            {
                AssertFailed();
                return VINF_BUFFER_OVERFLOW;    /* Return directly and don't free pDirEntry */
            }
            return VINF_SUCCESS;    /* Return directly and don't free pDirEntry */
        }

#ifdef RT_OS_WINDOWS
        pDirEntry->Info.Attr.fMode |= 0111;
#endif
        vbfsCopyFsObjInfoFromIprt(&pSFDEntry->Info, &pDirEntry->Info);

        /* The shortname (only used by OS/2 atm): */
        Assert(pDirEntry->cwcShortName < RT_ELEMENTS(pSFDEntry->uszShortName));
        Assert(pDirEntry->wszShortName[pDirEntry->cwcShortName] == '\0');
        pSFDEntry->cucShortName = pDirEntry->cwcShortName;
        if (pDirEntry->cwcShortName)
            memcpy(pSFDEntry->uszShortName, pDirEntry->wszShortName, sizeof(pSFDEntry->uszShortName));

        /* The name: */
        if (fUtf8)
        {
            void *src, *dst;

            src = &pDirEntry->szName[0];
            dst = &pSFDEntry->name.String.utf8[0];

            memcpy(dst, src, pDirEntry->cbName + 1);

            pSFDEntry->name.u16Size = pDirEntry->cbName + 1;
            pSFDEntry->name.u16Length = pDirEntry->cbName;
        }
        else
        {
            pSFDEntry->name.String.utf16[0] = 0;
            pwszString = pSFDEntry->name.String.utf16;
            int rc2 = RTStrToUtf16Ex(pDirEntry->szName, RTSTR_MAX, &pwszString, pDirEntry->cbName+1, NULL);
            AssertRC(rc2);

#ifdef RT_OS_DARWIN
/** @todo This belongs in rtPathToNative or in the windows shared folder file system driver...
 * The question is simply whether the NFD normalization is actually applied on a (virtual) file
 * system level in darwin, or just by the user mode application libs. */
            {
                // Convert to
                // Normalization Form C (composed Unicode). We need this because
                // Mac OS X file system uses NFD (Normalization Form D :decomposed Unicode)
                // while most other OS', server-side programs usually expect NFC.
                uint16_t ucs2Length;
                CFRange rangeCharacters;
                CFMutableStringRef inStr = ::CFStringCreateMutable(NULL, 0);

                ::CFStringAppendCharacters(inStr, (UniChar *)pwszString, RTUtf16Len(pwszString));
                ::CFStringNormalize(inStr, kCFStringNormalizationFormC);
                ucs2Length = ::CFStringGetLength(inStr);

                rangeCharacters.location = 0;
                rangeCharacters.length = ucs2Length;
                ::CFStringGetCharacters(inStr, rangeCharacters, pwszString);
                pwszString[ucs2Length] = 0x0000; // NULL terminated

                CFRelease(inStr);
            }
#endif
            pSFDEntry->name.u16Length = (uint32_t)RTUtf16Len(pSFDEntry->name.String.utf16) * 2;
            pSFDEntry->name.u16Size = pSFDEntry->name.u16Length + 2;

            Log(("SHFL: File name size %d\n", pSFDEntry->name.u16Size));
            Log(("SHFL: File name %ls\n", &pSFDEntry->name.String.utf16));

            // adjust cbNeeded (it was overestimated before)
            cbNeeded = RT_OFFSETOF(SHFLDIRINFO, name.String) + pSFDEntry->name.u16Size;
        }

        /* Advance */
        pSFDEntry   = (PSHFLDIRINFO)((uintptr_t)pSFDEntry + cbNeeded);
        *pcbBuffer += cbNeeded;
        cbBufferOrg-= cbNeeded;

        *pcFiles   += 1;

        /* Free the saved last entry, that we've just returned */
        if (pHandle->dir.pLastValidEntry)
        {
            RTMemFree(pHandle->dir.pLastValidEntry);
            pHandle->dir.pLastValidEntry = NULL;

            /* And use the newly allocated buffer from now. */
            pDirEntry = pDirEntryOrg;
        }

        if (flags & SHFL_LIST_RETURN_ONE)
            break; /* we're done */
    }
    Assert(rc != VINF_SUCCESS || *pcbBuffer > 0);

end:
    if (pDirEntry)
        RTMemFree(pDirEntry);

    return rc;
}

#ifdef UNITTEST
/** Unit test the SHFL_FN_READLINK API.  Located here as a form of API
 * documentation. */
void testReadLink(RTTEST hTest)
{
    /* If the number or types of parameters are wrong the API should fail. */
    testReadLinkBadParameters(hTest);
    /* Add tests as required... */
}
#endif
int vbsfReadLink(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLSTRING *pPath, uint32_t cbPath, uint8_t *pBuffer, uint32_t cbBuffer)
{
    int rc = VINF_SUCCESS;

    if (pPath == 0 || pBuffer == 0)
    {
        AssertFailed();
        return VERR_INVALID_PARAMETER;
    }

    /* Build a host full path for the given path, handle file name case issues
     * (if the guest expects case-insensitive paths but the host is
     * case-sensitive) and convert utf16 to utf8 if necessary.
     */
    char *pszFullPath = NULL;
    uint32_t cbFullPathRoot = 0;

    rc = vbsfBuildFullPath(pClient, root, pPath, cbPath, &pszFullPath, &cbFullPathRoot);

    if (RT_SUCCESS(rc))
    {
        rc = RTSymlinkRead(pszFullPath, (char *) pBuffer, cbBuffer, 0);
        if (RT_SUCCESS(rc))
        {
            /* Convert the slashes in the link target to the guest path separator characters. */
            /** @todo r=bird: for some messed up reason, we return UTF-8 here rather than
             * the character set selected by the client.  We also don't return the
             * length, so the clients are paranoid about the zero termination behavior. */
            char ch;
            char *psz = (char *)pBuffer;
            while ((ch = *psz) != '\0')
            {
                if (RTPATH_IS_SLASH(ch))
                    *psz = pClient->PathDelimiter;
                psz++;
            }
        }

        /* free the path string */
        vbsfFreeFullPath(pszFullPath);
    }

    return rc;
}

int vbsfQueryFileInfo(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle, uint32_t flags,
                      uint32_t *pcbBuffer, uint8_t *pBuffer)
{
    RT_NOREF1(flags);
    uint32_t type = vbsfQueryHandleType(pClient, Handle);
    int            rc = VINF_SUCCESS;
    SHFLFSOBJINFO   *pObjInfo = (SHFLFSOBJINFO *)pBuffer;
    RTFSOBJINFO    fileinfo;


    AssertReturn(type == SHFL_HF_TYPE_DIR || type == SHFL_HF_TYPE_FILE, VERR_INVALID_PARAMETER);
    AssertReturn(pcbBuffer != NULL, VERR_INVALID_PARAMETER);
    AssertReturn(pObjInfo != NULL, VERR_INVALID_PARAMETER);
    AssertReturn(*pcbBuffer >= sizeof(SHFLFSOBJINFO), VERR_INVALID_PARAMETER);

    /** @todo other options */
    Assert(flags == (SHFL_INFO_GET|SHFL_INFO_FILE));

    *pcbBuffer  = 0;

    if (type == SHFL_HF_TYPE_DIR)
    {
        SHFLFILEHANDLE *pHandle = vbsfQueryDirHandle(pClient, Handle);
        rc = vbsfCheckHandleAccess(pClient, root, pHandle, VBSF_CHECK_ACCESS_READ);
        if (RT_SUCCESS(rc))
            rc = RTDirQueryInfo(pHandle->dir.Handle, &fileinfo, RTFSOBJATTRADD_NOTHING);
    }
    else
    {
        SHFLFILEHANDLE *pHandle = vbsfQueryFileHandle(pClient, Handle);
        rc = vbsfCheckHandleAccess(pClient, root, pHandle, VBSF_CHECK_ACCESS_READ);
        if (RT_SUCCESS(rc))
            rc = RTFileQueryInfo(pHandle->file.Handle, &fileinfo, RTFSOBJATTRADD_NOTHING);
#ifdef RT_OS_WINDOWS
        if (RT_SUCCESS(rc) && RTFS_IS_FILE(pObjInfo->Attr.fMode))
            pObjInfo->Attr.fMode |= 0111;
#endif
    }
    if (rc == VINF_SUCCESS)
    {
        vbfsCopyFsObjInfoFromIprt(pObjInfo, &fileinfo);
        *pcbBuffer = sizeof(SHFLFSOBJINFO);
    }
    else
        AssertFailed();

    return rc;
}

static int vbsfSetFileInfo(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle, uint32_t flags,
                           uint32_t *pcbBuffer, uint8_t *pBuffer)
{
    RT_NOREF1(flags);
    uint32_t type = vbsfQueryHandleType(pClient, Handle);
    int             rc = VINF_SUCCESS;
    SHFLFSOBJINFO  *pSFDEntry;

    if (   !(type == SHFL_HF_TYPE_DIR || type == SHFL_HF_TYPE_FILE)
        || pcbBuffer == 0
        || pBuffer == 0
        || *pcbBuffer < sizeof(SHFLFSOBJINFO))
    {
        AssertFailed();
        return VERR_INVALID_PARAMETER;
    }

    *pcbBuffer  = 0;
    pSFDEntry   = (SHFLFSOBJINFO *)pBuffer;

    Assert(flags == (SHFL_INFO_SET | SHFL_INFO_FILE));

    /*
     * Get the handle.
     */
    SHFLFILEHANDLE *pHandle;
    if (type == SHFL_HF_TYPE_FILE)
    {
        pHandle = vbsfQueryFileHandle(pClient, Handle);
        rc = vbsfCheckHandleAccess(pClient, root, pHandle, VBSF_CHECK_ACCESS_WRITE);
    }
    else
    {
        Assert(type == SHFL_HF_TYPE_DIR);
        pHandle = vbsfQueryDirHandle(pClient, Handle);
        rc = vbsfCheckHandleAccess(pClient, root, pHandle, VBSF_CHECK_ACCESS_WRITE);
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Any times to set?
         */
        if (   RTTimeSpecGetNano(&pSFDEntry->AccessTime)
            || RTTimeSpecGetNano(&pSFDEntry->ModificationTime)
            || RTTimeSpecGetNano(&pSFDEntry->ChangeTime)
            || RTTimeSpecGetNano(&pSFDEntry->BirthTime))
        {

            /* Change only the time values that are not zero */
            if (type == SHFL_HF_TYPE_FILE)
                rc = RTFileSetTimes(pHandle->file.Handle,
                                    RTTimeSpecGetNano(&pSFDEntry->AccessTime)       ? &pSFDEntry->AccessTime         : NULL,
                                    RTTimeSpecGetNano(&pSFDEntry->ModificationTime) ? &pSFDEntry->ModificationTime   : NULL,
                                    RTTimeSpecGetNano(&pSFDEntry->ChangeTime)       ? &pSFDEntry->ChangeTime         : NULL,
                                    RTTimeSpecGetNano(&pSFDEntry->BirthTime)        ? &pSFDEntry->BirthTime          : NULL);
            else
                rc = RTDirSetTimes( pHandle->dir.Handle,
                                    RTTimeSpecGetNano(&pSFDEntry->AccessTime)       ? &pSFDEntry->AccessTime         : NULL,
                                    RTTimeSpecGetNano(&pSFDEntry->ModificationTime) ? &pSFDEntry->ModificationTime   : NULL,
                                    RTTimeSpecGetNano(&pSFDEntry->ChangeTime)       ? &pSFDEntry->ChangeTime         : NULL,
                                    RTTimeSpecGetNano(&pSFDEntry->BirthTime)        ? &pSFDEntry->BirthTime          : NULL);
            if (RT_FAILURE(rc))
            {
                Log(("RT%sSetTimes failed with %Rrc\n", type == SHFL_HF_TYPE_FILE ? "File" : "Dir", rc));
                Log(("AccessTime       %#RX64\n", RTTimeSpecGetNano(&pSFDEntry->AccessTime)));
                Log(("ModificationTime %#RX64\n", RTTimeSpecGetNano(&pSFDEntry->ModificationTime)));
                Log(("ChangeTime       %#RX64\n", RTTimeSpecGetNano(&pSFDEntry->ChangeTime)));
                Log(("BirthTime        %#RX64\n", RTTimeSpecGetNano(&pSFDEntry->BirthTime)));
                /* "temporary" hack */
                rc = VINF_SUCCESS;
            }
        }

        /*
         * Any mode changes?
         */
        if (pSFDEntry->Attr.fMode)
        {
            RTFMODE fMode = pSFDEntry->Attr.fMode;

            if (type == SHFL_HF_TYPE_FILE)
            {
#ifndef RT_OS_WINDOWS
                /* Don't allow the guest to clear the read own bit, otherwise the guest wouldn't
                 * be able to access this file anymore. Only for guests, which set the UNIX mode.
                 * Also, clear bits which we don't pass through for security reasons. */
                if (fMode & RTFS_UNIX_MASK)
                {
                    fMode |= RTFS_UNIX_IRUSR;
                    fMode &= ~(RTFS_UNIX_ISUID | RTFS_UNIX_ISGID | RTFS_UNIX_ISTXT);
                }
#endif
                rc = RTFileSetMode(pHandle->file.Handle, fMode);
            }
            else
            {
#ifndef RT_OS_WINDOWS
                /* Don't allow the guest to clear the read+execute own bits, otherwise the guest
                 * wouldn't be able to access this directory anymore.  Only for guests, which set
                 * the UNIX mode.  Also, clear bits which we don't pass through for security reasons. */
                if (fMode & RTFS_UNIX_MASK)
                {
                    fMode |= RTFS_UNIX_IRUSR | RTFS_UNIX_IXUSR;
                    fMode &= ~(RTFS_UNIX_ISUID | RTFS_UNIX_ISGID | RTFS_UNIX_ISTXT /*?*/);
                }
#endif
                rc = RTDirSetMode(pHandle->dir.Handle, fMode);
            }
            if (RT_FAILURE(rc))
            {
                Log(("RT%sSetMode %#x (%#x) failed with %Rrc\n", type == SHFL_HF_TYPE_FILE ? "File" : "Dir",
                     fMode, pSFDEntry->Attr.fMode, rc));
                /* silent failure, because this tends to fail with e.g. windows guest & linux host */
                rc = VINF_SUCCESS;
            }
        }

        /*
         * Return the current file info on success.
         */
        if (RT_SUCCESS(rc))
        {
            uint32_t bufsize = sizeof(*pSFDEntry);
            rc = vbsfQueryFileInfo(pClient, root, Handle, SHFL_INFO_GET | SHFL_INFO_FILE, &bufsize, (uint8_t *)pSFDEntry);
            if (RT_SUCCESS(rc))
                *pcbBuffer = sizeof(SHFLFSOBJINFO);
            else
                AssertFailed();
        }
    }
    return rc;
}


/**
 * Handles SHFL_FN_SET_FILE_SIZE.
 */
int vbsfSetFileSize(SHFLCLIENTDATA *pClient, SHFLROOT idRoot, SHFLHANDLE hHandle, uint64_t cbNewSize)
{
    /*
     * Resolve handle and validate write access.
     */
    SHFLFILEHANDLE *pHandle = vbsfQueryFileHandle(pClient, hHandle);
    ASSERT_GUEST_RETURN(pHandle, VERR_INVALID_HANDLE);

    int rc = vbsfCheckHandleAccess(pClient, idRoot, pHandle, VBSF_CHECK_ACCESS_WRITE);
    if (RT_SUCCESS(rc))
    {
        /*
         * Execute the request.
         */
        rc = RTFileSetSize(pHandle->file.Handle, cbNewSize);
    }
    return rc;
}


static int vbsfSetEndOfFile(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle, uint32_t flags,
                            uint32_t *pcbBuffer, uint8_t *pBuffer)
{
    RT_NOREF1(flags);
    SHFLFILEHANDLE *pHandle = vbsfQueryFileHandle(pClient, Handle);
    SHFLFSOBJINFO  *pSFDEntry;

    if (pHandle == 0 || pcbBuffer == 0 || pBuffer == 0 || *pcbBuffer < sizeof(SHFLFSOBJINFO))
    {
        AssertFailed();
        return VERR_INVALID_PARAMETER;
    }

    int rc = vbsfCheckHandleAccess(pClient, root, pHandle, VBSF_CHECK_ACCESS_WRITE);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else
        return rc;

    *pcbBuffer  = 0;
    pSFDEntry   = (SHFLFSOBJINFO *)pBuffer;

    if (flags & SHFL_INFO_SIZE)
    {
        rc = RTFileSetSize(pHandle->file.Handle, pSFDEntry->cbObject);
        if (rc != VINF_SUCCESS)
            AssertFailed();
    }
    else
        AssertFailed();

    if (rc == VINF_SUCCESS)
    {
        RTFSOBJINFO fileinfo;

        /* Query the new object info and return it */
        rc = RTFileQueryInfo(pHandle->file.Handle, &fileinfo, RTFSOBJATTRADD_NOTHING);
        if (rc == VINF_SUCCESS)
        {
#ifdef RT_OS_WINDOWS
            fileinfo.Attr.fMode |= 0111;
#endif
            vbfsCopyFsObjInfoFromIprt(pSFDEntry, &fileinfo);
            *pcbBuffer = sizeof(SHFLFSOBJINFO);
        }
        else
            AssertFailed();
    }

    return rc;
}

static int vbsfQueryVolumeInfo(SHFLCLIENTDATA *pClient, SHFLROOT root, uint32_t flags, uint32_t *pcbBuffer, uint8_t *pBuffer)
{
    RT_NOREF2(root, flags);
    int            rc = VINF_SUCCESS;
    SHFLVOLINFO   *pSFDEntry;
    char          *pszFullPath = NULL;
    union
    {
        SHFLSTRING  Dummy;
        uint8_t     abDummy[SHFLSTRING_HEADER_SIZE + sizeof(RTUTF16)];
    } Buf;

    if (pcbBuffer == 0 || pBuffer == 0 || *pcbBuffer < sizeof(SHFLVOLINFO))
    {
        AssertFailed();
        return VERR_INVALID_PARAMETER;
    }

    /** @todo other options */
    Assert(flags == (SHFL_INFO_GET|SHFL_INFO_VOLUME));

    *pcbBuffer  = 0;
    pSFDEntry   = (PSHFLVOLINFO)pBuffer;

    ShflStringInitBuffer(&Buf.Dummy, sizeof(Buf));
    Buf.Dummy.String.utf16[0] = '\0';
    rc = vbsfBuildFullPath(pClient, root, &Buf.Dummy, sizeof(Buf), &pszFullPath, NULL);

    if (RT_SUCCESS(rc))
    {
        rc = RTFsQuerySizes(pszFullPath, &pSFDEntry->ullTotalAllocationBytes, &pSFDEntry->ullAvailableAllocationBytes, &pSFDEntry->ulBytesPerAllocationUnit, &pSFDEntry->ulBytesPerSector);
        if (rc != VINF_SUCCESS)
            goto exit;

        rc = RTFsQuerySerial(pszFullPath, &pSFDEntry->ulSerial);
        if (rc != VINF_SUCCESS)
            goto exit;

        RTFSPROPERTIES FsProperties;
        rc = RTFsQueryProperties(pszFullPath, &FsProperties);
        if (rc != VINF_SUCCESS)
            goto exit;
        vbfsCopyFsPropertiesFromIprt(&pSFDEntry->fsProperties, &FsProperties);

        *pcbBuffer = sizeof(SHFLVOLINFO);
    }
    else AssertFailed();

exit:
    AssertMsg(rc == VINF_SUCCESS, ("failure: rc = %Rrc\n", rc));
    /* free the path string */
    vbsfFreeFullPath(pszFullPath);
    return rc;
}

int vbsfQueryFSInfo(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle, uint32_t flags, uint32_t *pcbBuffer, uint8_t *pBuffer)
{
    if (pcbBuffer == 0 || pBuffer == 0)
    {
        AssertFailed();
        return VERR_INVALID_PARAMETER;
    }

    if (flags & SHFL_INFO_FILE)
        return vbsfQueryFileInfo(pClient, root, Handle, flags, pcbBuffer, pBuffer);

    if (flags & SHFL_INFO_VOLUME)
        return vbsfQueryVolumeInfo(pClient, root, flags, pcbBuffer, pBuffer);

    AssertFailed();
    return VERR_INVALID_PARAMETER;
}

#ifdef UNITTEST
/** Unit test the SHFL_FN_INFORMATION API.  Located here as a form of API
 * documentation. */
void testFSInfo(RTTEST hTest)
{
    /* If the number or types of parameters are wrong the API should fail. */
    testFSInfoBadParameters(hTest);
    /* Basic get and set file size test. */
    testFSInfoQuerySetFMode(hTest);
    /* Basic get and set dir atime test. */
    testFSInfoQuerySetDirATime(hTest);
    /* Basic get and set file atime test. */
    testFSInfoQuerySetFileATime(hTest);
    /* Basic set end of file. */
    testFSInfoQuerySetEndOfFile(hTest);
    /* Add tests as required... */
}
#endif
int vbsfSetFSInfo(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle, uint32_t flags, uint32_t *pcbBuffer, uint8_t *pBuffer)
{
    uint32_t type =   vbsfQueryHandleType(pClient, Handle)
                    & (SHFL_HF_TYPE_DIR|SHFL_HF_TYPE_FILE|SHFL_HF_TYPE_VOLUME);

    if (type == 0 || pcbBuffer == 0 || pBuffer == 0)
    {
        AssertFailed();
        return VERR_INVALID_PARAMETER;
    }

    if (flags & SHFL_INFO_FILE)
        return vbsfSetFileInfo(pClient, root, Handle, flags, pcbBuffer, pBuffer);

    if (flags & SHFL_INFO_SIZE)
        return vbsfSetEndOfFile(pClient, root, Handle, flags, pcbBuffer, pBuffer);

//    if (flags & SHFL_INFO_VOLUME)
//        return vbsfVolumeInfo(pClient, root, Handle, flags, pcbBuffer, pBuffer);
    AssertFailed();
    return VERR_INVALID_PARAMETER;
}

#ifdef UNITTEST
/** Unit test the SHFL_FN_LOCK API.  Located here as a form of API
 * documentation. */
void testLock(RTTEST hTest)
{
    /* If the number or types of parameters are wrong the API should fail. */
    testLockBadParameters(hTest);
    /* Simple file locking and unlocking test. */
    testLockFileSimple(hTest);
    /* Add tests as required... */
}
#endif

int vbsfLock(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle, uint64_t offset, uint64_t length, uint32_t flags)
{
    SHFLFILEHANDLE *pHandle = vbsfQueryFileHandle(pClient, Handle);
    uint32_t        fRTLock = 0;

    Assert((flags & SHFL_LOCK_MODE_MASK) != SHFL_LOCK_CANCEL);

    int rc = vbsfCheckHandleAccess(pClient, root, pHandle, VBSF_CHECK_ACCESS_READ);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else
        return rc;

    if (   ((flags & SHFL_LOCK_MODE_MASK) == SHFL_LOCK_CANCEL)
        || (flags & SHFL_LOCK_ENTIRE)
       )
    {
        AssertFailed();
        return VERR_INVALID_PARAMETER;
    }

    /* Lock type */
    switch(flags & SHFL_LOCK_MODE_MASK)
    {
    case SHFL_LOCK_SHARED:
        fRTLock = RTFILE_LOCK_READ;
        break;

    case SHFL_LOCK_EXCLUSIVE:
        fRTLock = RTFILE_LOCK_READ | RTFILE_LOCK_WRITE;
        break;

    default:
        AssertFailed();
        return VERR_INVALID_PARAMETER;
    }

    /* Lock wait type */
    if (flags & SHFL_LOCK_WAIT)
        fRTLock |= RTFILE_LOCK_WAIT;
    else
        fRTLock |= RTFILE_LOCK_IMMEDIATELY;

#ifdef RT_OS_WINDOWS
    rc = RTFileLock(pHandle->file.Handle, fRTLock, offset, length);
    if (rc != VINF_SUCCESS)
        Log(("RTFileLock %RTfile %RX64 %RX64 failed with %Rrc\n", pHandle->file.Handle, offset, length, rc));
#else
    Log(("vbsfLock: Pretend success handle=%x\n", Handle));
    rc = VINF_SUCCESS;
    RT_NOREF2(offset,  length);
#endif
    return rc;
}

int vbsfUnlock(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLHANDLE Handle, uint64_t offset, uint64_t length, uint32_t flags)
{
    SHFLFILEHANDLE *pHandle = vbsfQueryFileHandle(pClient, Handle);

    Assert((flags & SHFL_LOCK_MODE_MASK) == SHFL_LOCK_CANCEL);

    int rc = vbsfCheckHandleAccess(pClient, root, pHandle, VBSF_CHECK_ACCESS_READ);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else
        return rc;

    if (   ((flags & SHFL_LOCK_MODE_MASK) != SHFL_LOCK_CANCEL)
        || (flags & SHFL_LOCK_ENTIRE)
       )
    {
       return VERR_INVALID_PARAMETER;
    }

#ifdef RT_OS_WINDOWS
    rc = RTFileUnlock(pHandle->file.Handle, offset, length);
    if (rc != VINF_SUCCESS)
        Log(("RTFileUnlock %RTfile %RX64 %RTX64 failed with %Rrc\n", pHandle->file.Handle, offset, length, rc));
#else
    Log(("vbsfUnlock: Pretend success handle=%x\n", Handle));
    rc = VINF_SUCCESS;
    RT_NOREF2(offset,  length);
#endif

    return rc;
}


#ifdef UNITTEST
/** Unit test the SHFL_FN_REMOVE API.  Located here as a form of API
 * documentation. */
void testRemove(RTTEST hTest)
{
    /* If the number or types of parameters are wrong the API should fail. */
    testRemoveBadParameters(hTest);
    /* Add tests as required... */
}
#endif
int vbsfRemove(SHFLCLIENTDATA *pClient, SHFLROOT root, PCSHFLSTRING pPath, uint32_t cbPath, uint32_t flags, SHFLHANDLE hToClose)
{

    /* Validate input */
    Assert(pPath);
    AssertReturn(pPath->u16Size > 0, VERR_INVALID_PARAMETER);

    /*
     * Close the handle if specified.
     */
    int rc = VINF_SUCCESS;
    if (hToClose != SHFL_HANDLE_NIL)
        rc = vbsfClose(pClient, root, hToClose);
    if (RT_SUCCESS(rc))
    {
        /*
         * Build a host full path for the given path and convert utf16 to utf8 if necessary.
         */
        char *pszFullPath = NULL;
        rc = vbsfBuildFullPath(pClient, root, pPath, cbPath, &pszFullPath, NULL);
        if (RT_SUCCESS(rc))
        {
            /*
             * Is the guest allowed to write to this share?
             */
            bool fWritable;
            rc = vbsfMappingsQueryWritable(pClient, root, &fWritable);
            if (RT_SUCCESS(rc) && fWritable)
            {
                /*
                 * Do the removal/deletion according to the type flags.
                 */
                if (flags & SHFL_REMOVE_SYMLINK)
                    rc = RTSymlinkDelete(pszFullPath, 0);
                else if (flags & SHFL_REMOVE_FILE)
                    rc = RTFileDelete(pszFullPath);
                else
                    rc = RTDirRemove(pszFullPath);

#if 0 //ndef RT_OS_WINDOWS
                /* There are a few adjustments to be made here: */
                if (   rc == VERR_FILE_NOT_FOUND
                    && SHFL_CLIENT_NEED_WINDOWS_ERROR_STYLE_ADJUST_ON_POSIX(pClient)
                    && vbsfErrorStyleIsWindowsPathNotFound(pszFullPath))
                    rc = VERR_PATH_NOT_FOUND;
                else if (   rc == VERR_PATH_NOT_FOUND
                         && SHFL_CLIENT_NEED_WINDOWS_ERROR_STYLE_ADJUST_ON_POSIX(pClient))
                {
                    if (flags & (SHFL_REMOVE_FILE | SHFL_REMOVE_SYMLINK))
                    {
                        size_t cchFullPath = strlen(pszFullPath);
                        if (cchFullPath > 0 && RTPATH_IS_SLASH(pszFullPath[cchFullPath - 1]))
                            rc = VERR_INVALID_NAME;
                    }
                    else if (vbsfErrorStyleIsWindowsNotADirectory(pszFullPath))
                        rc = VERR_NOT_A_DIRECTORY;
                }
#endif
            }
            else
                rc = VERR_WRITE_PROTECT;

            /* free the path string */
            vbsfFreeFullPath(pszFullPath);
        }
    }
    return rc;
}


#ifdef UNITTEST
/** Unit test the SHFL_FN_RENAME API.  Located here as a form of API
 * documentation. */
void testRename(RTTEST hTest)
{
    /* If the number or types of parameters are wrong the API should fail. */
    testRenameBadParameters(hTest);
    /* Add tests as required... */
}
#endif
int vbsfRename(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLSTRING *pSrc, SHFLSTRING *pDest, uint32_t flags)
{
    int rc = VINF_SUCCESS;

    /* Validate input */
    if (   flags & ~(SHFL_RENAME_FILE|SHFL_RENAME_DIR|SHFL_RENAME_REPLACE_IF_EXISTS)
        || pSrc == 0
        || pDest == 0)
    {
        AssertFailed();
        return VERR_INVALID_PARAMETER;
    }

    /* Build a host full path for the given path
     * and convert utf16 to utf8 if necessary.
     */
    char *pszFullPathSrc = NULL;
    char *pszFullPathDest = NULL;

    rc = vbsfBuildFullPath(pClient, root, pSrc, pSrc->u16Size + SHFLSTRING_HEADER_SIZE, &pszFullPathSrc, NULL);
    if (rc != VINF_SUCCESS)
        return rc;

    rc = vbsfBuildFullPath(pClient, root, pDest, pDest->u16Size + SHFLSTRING_HEADER_SIZE, &pszFullPathDest, NULL, false, true);
    if (RT_SUCCESS (rc))
    {
        Log(("Rename %s to %s\n", pszFullPathSrc, pszFullPathDest));

        /* is the guest allowed to write to this share? */
        bool fWritable;
        rc = vbsfMappingsQueryWritable(pClient, root, &fWritable);
        if (RT_FAILURE(rc) || !fWritable)
            rc = VERR_WRITE_PROTECT;

        if (RT_SUCCESS(rc))
        {
            if ((flags & (SHFL_RENAME_FILE | SHFL_RENAME_DIR)) == (SHFL_RENAME_FILE | SHFL_RENAME_DIR))
            {
                rc = RTPathRename(pszFullPathSrc, pszFullPathDest,
                                  flags & SHFL_RENAME_REPLACE_IF_EXISTS ? RTPATHRENAME_FLAGS_REPLACE : 0);
            }
            else if (flags & SHFL_RENAME_FILE)
            {
                rc = RTFileMove(pszFullPathSrc, pszFullPathDest,
                                ((flags & SHFL_RENAME_REPLACE_IF_EXISTS) ? RTFILEMOVE_FLAGS_REPLACE : 0));
            }
            else
            {
                /* NT ignores the REPLACE flag and simply return and already exists error. */
                rc = RTDirRename(pszFullPathSrc, pszFullPathDest,
                                 ((flags & SHFL_RENAME_REPLACE_IF_EXISTS) ? RTPATHRENAME_FLAGS_REPLACE : 0));
            }
#ifndef RT_OS_WINDOWS
            if (   rc == VERR_FILE_NOT_FOUND
                && SHFL_CLIENT_NEED_WINDOWS_ERROR_STYLE_ADJUST_ON_POSIX(pClient)
                && vbsfErrorStyleIsWindowsPathNotFound2(pszFullPathSrc, pszFullPathDest))
                rc = VERR_PATH_NOT_FOUND;
#endif
        }

        /* free the path string */
        vbsfFreeFullPath(pszFullPathDest);
    }
    /* free the path string */
    vbsfFreeFullPath(pszFullPathSrc);
    return rc;
}

#ifdef UNITTEST
/** Unit test the SHFL_FN_COPY_FILE API.  Located here as a form of API
 * documentation. */
void testCopyFile(RTTEST hTest)
{
    /* Verify copying a file within a read-write share. */
    testCopyFileReadWrite(hTest);

    /* Verify attempts to copy a file within a read-only share returns EROFS. */
    testCopyFileReadOnly(hTest);
    /* Add tests as required... */
}
#endif

/**
 * Implements SHFL_FN_COPY_FILE (wrapping RTFileCopy).
 */
int vbsfCopyFile(SHFLCLIENTDATA *pClient, SHFLROOT idRootSrc, PCSHFLSTRING pStrPathSrc,
                 SHFLROOT idRootDst, PCSHFLSTRING pStrPathDst, uint32_t fFlags)
{
    AssertPtrReturn(pClient, VERR_INVALID_PARAMETER);
    if (pClient->fu32Flags & SHFL_CF_UTF8)
        LogFunc(("pClient %p, idRootSrc %#RX32, '%.*s', idRootSrc %#RX32, '%.*s', fFlags %#x\n", pClient, idRootSrc,
                 pStrPathSrc->u16Length, pStrPathSrc->String.ach, idRootDst, pStrPathDst->u16Length, pStrPathDst->String.ach, fFlags));
    else
        LogFunc(("pClient %p, idRootSrc %#RX32, '%.*ls', idRootSrc %#RX32, '%.*ls', fFlags %#x\n", pClient,
                 idRootSrc, pStrPathSrc->u16Length / sizeof(RTUTF16), pStrPathSrc->String.ach,
                 idRootDst, pStrPathDst->u16Length / sizeof(RTUTF16), pStrPathDst->String.ach, fFlags));

    /*
     * Build host paths.
     */
    char *pszPathSrc = NULL;
    int rc = vbsfBuildFullPath(pClient, idRootSrc, pStrPathSrc, pStrPathSrc->u16Size + SHFLSTRING_HEADER_SIZE, &pszPathSrc, NULL);
    if (RT_SUCCESS(rc))
    {
        char *pszPathDst = NULL;
        rc = vbsfBuildFullPath(pClient, idRootDst, pStrPathDst, pStrPathDst->u16Size + SHFLSTRING_HEADER_SIZE, &pszPathDst, NULL);
        if (RT_SUCCESS(rc))
        {
            /* is the guest allowed to write to this share? */
            bool fWritable;
            rc = vbsfMappingsQueryWritable(pClient, idRootDst, &fWritable);
            if (RT_FAILURE(rc) || !fWritable)
                rc = VERR_WRITE_PROTECT;

            /*
             * Do the job.
             */
            if (RT_SUCCESS(rc))
                rc = RTFileCopy(pszPathSrc, pszPathDst);

            vbsfFreeFullPath(pszPathDst);
        }
        vbsfFreeFullPath(pszPathSrc);
    }

    RT_NOREF(fFlags);
    LogFunc(("returns %Rrc\n", rc));
    return rc;
}

#ifdef UNITTEST
/** Unit test the SHFL_FN_SYMLINK API.  Located here as a form of API
 * documentation. */
void testSymlink(RTTEST hTest)
{
    /* If the number or types of parameters are wrong the API should fail. */
    testSymlinkBadParameters(hTest);
    /* Exercise symbolic link creation with various symlink policies in place. */
    testSymlinkCreation(hTest);
    /* Verify attempts to create a symbolic link within a read-only share returns EROFS. */
    testSymlinkReadOnlyCreation(hTest);
    /* Add additional tests as required... */
}
#endif

int vbsfSymlink(SHFLCLIENTDATA *pClient, SHFLROOT root, SHFLSTRING *pSymlinkPath, SHFLSTRING *pSourcePath, SHFLFSOBJINFO *pInfo)
{
    int rc = VINF_SUCCESS;

    char *pszFullSymlinkPath = NULL;
    char *pszFullSourcePath = NULL;

    /* XXX: no support for UCS2 at the moment. */
    if (!BIT_FLAG(pClient->fu32Flags, SHFL_CF_UTF8))
        return VERR_NOT_IMPLEMENTED;

    /* is the guest allowed to create symlinks in this share? */
    bool fSymlinksCreate;
    rc = vbsfMappingsQuerySymlinksCreate(pClient, root, &fSymlinksCreate);
    if (RT_FAILURE(rc) || !fSymlinksCreate)
        return VERR_WRITE_PROTECT; /* XXX or VERR_TOO_MANY_SYMLINKS? */

    /* is the guest allowed to write to this share? */
    bool fWritable;
    rc = vbsfMappingsQueryWritable(pClient, root, &fWritable);
    if (RT_FAILURE(rc) || !fWritable)
        return VERR_WRITE_PROTECT;

    rc = vbsfBuildFullPath(pClient, root, pSymlinkPath, pSymlinkPath->u16Size + SHFLSTRING_HEADER_SIZE, &pszFullSymlinkPath,
                           NULL);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * The symbolic link source path may be located outside of the shared folder so thus
     * we don't call vbsfBuildFullPath() which includes VBSF_O_PATH_CHECK_ROOT_ESCAPE to
     * verify that the pathname resides within the shared folder.  Instead we call the
     * heart of vbsfBuildFullPath() which is vbsfPathGuestToHost() to perform a subset
     * of its checks to verify that the symbolic link source is a valid path by checking
     * for invalid characters, replacing path delimiters if the guest uses a different
     * slash than the host, and evaluating the symbolic link policy if one has been set.
     * We don't collapse path components of '..' for example or alter the path otherwise
     * as this is the documented behavior of symbolic links: the source pathname can be
     * any pathname and isn't required to exist.
     */
    uint32_t fu32PathFlags = 0;
    uint32_t fu32Options = VBSF_O_PATH_CHECK_SYMLINK_POLICY;
    rc = vbsfPathGuestToHost(pClient, root, pSourcePath, pSourcePath->u16Size + SHFLSTRING_HEADER_SIZE,
                             &pszFullSourcePath, NULL, fu32Options, &fu32PathFlags);
    if (RT_FAILURE(rc))
    {
        vbsfFreeFullPath(pszFullSymlinkPath);
        return rc;
    }

    rc = RTSymlinkCreate(pszFullSymlinkPath, pszFullSourcePath, RTSYMLINKTYPE_UNKNOWN, 0);
    if (RT_SUCCESS(rc))
    {
        RTFSOBJINFO info;
        rc = RTPathQueryInfoEx(pszFullSymlinkPath, &info, RTFSOBJATTRADD_NOTHING, RTPATH_F_ON_LINK);
        if (RT_SUCCESS(rc))
            vbfsCopyFsObjInfoFromIprt(pInfo, &info);
    }

    vbsfFreeFullPath(pszFullSourcePath);
    vbsfFreeFullPath(pszFullSymlinkPath);

    return rc;
}

/*
 * Clean up our mess by freeing all handles that are still valid.
 *
 */
int vbsfDisconnect(SHFLCLIENTDATA *pClient)
{
    for (int i = 0; i < SHFLHANDLE_MAX; ++i)
    {
        SHFLFILEHANDLE *pHandle = NULL;
        SHFLHANDLE Handle = (SHFLHANDLE)i;

        uint32_t type = vbsfQueryHandleType(pClient, Handle);
        switch (type & (SHFL_HF_TYPE_DIR | SHFL_HF_TYPE_FILE))
        {
            case SHFL_HF_TYPE_DIR:
            {
                pHandle = vbsfQueryDirHandle(pClient, Handle);
                break;
            }
            case SHFL_HF_TYPE_FILE:
            {
                pHandle = vbsfQueryFileHandle(pClient, Handle);
                break;
            }
            default:
                break;
        }

        if (pHandle)
        {
            LogFunc(("Opened handle 0x%08x\n", i));
            vbsfClose(pClient, pHandle->root, Handle);
        }
    }

    for (uint32_t i = 0; i < RT_ELEMENTS(pClient->acMappings); i++)
        if (pClient->acMappings[i])
        {
            uint16_t cMappings = pClient->acMappings[i];
            while (cMappings-- > 0)
                vbsfUnmapFolder(pClient, i);
        }

    return VINF_SUCCESS;
}

