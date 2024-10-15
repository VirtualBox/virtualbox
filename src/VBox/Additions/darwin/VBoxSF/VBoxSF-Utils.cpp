/* $Id: VBoxSF-Utils.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxSF - Darwin Shared Folders, Utility Functions.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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
#include "VBoxSFInternal.h"

#include <iprt/assert.h>
#include <iprt/mem.h>
#include <VBox/log.h>

#if 0
/**
 * Helper function to create XNU VFS vnode object.
 *
 * @param mp        Mount data structure
 * @param type      vnode type (directory, regular file, etc)
 * @param pParent   Parent vnode object (NULL for VBoxVFS root vnode)
 * @param fIsRoot   Flag that indicates if created vnode object is
 *                  VBoxVFS root vnode (TRUE for VBoxVFS root vnode, FALSE
 *                  for all aother vnodes)
 * @param           Path within Shared Folder
 * @param ret       Returned newly created vnode
 *
 * @return 0 on success, error code otherwise
 */
int
vboxvfs_create_vnode_internal(struct mount *mp, enum vtype type, vnode_t pParent, int fIsRoot, PSHFLSTRING Path, vnode_t *ret)
{
    int     rc;
    vnode_t vnode;

    vboxvfs_vnode_t  *pVnodeData;
    vboxvfs_mount_t  *pMount;

    AssertReturn(mp, EINVAL);

    pMount = (vboxvfs_mount_t *)vfs_fsprivate(mp);
    AssertReturn(pMount, EINVAL);
    AssertReturn(pMount->pLockGroup, EINVAL);

    AssertReturn(Path, EINVAL);

    pVnodeData = (vboxvfs_vnode_t *)RTMemAllocZ(sizeof(vboxvfs_vnode_t));
    AssertReturn(pVnodeData, ENOMEM);

    /* Initialize private data */
    pVnodeData->pHandle = SHFL_HANDLE_NIL;
    pVnodeData->pPath   = Path;

    pVnodeData->pLockAttr = lck_attr_alloc_init();
    if (pVnodeData->pLockAttr)
    {
        pVnodeData->pLock = lck_rw_alloc_init(pMount->pLockGroup, pVnodeData->pLockAttr);
        if (pVnodeData->pLock)
        {
            struct vnode_fsparam vnode_params;

            vnode_params.vnfs_mp         = mp;
            vnode_params.vnfs_vtype      = type;
            vnode_params.vnfs_str        = NULL;
            vnode_params.vnfs_dvp        = pParent;
            vnode_params.vnfs_fsnode     = pVnodeData;  /** Private data attached per xnu's vnode object */
            vnode_params.vnfs_vops       = g_papfnVBoxVFSVnodeDirOpsVector;

            vnode_params.vnfs_markroot   = fIsRoot;
            vnode_params.vnfs_marksystem = FALSE;
            vnode_params.vnfs_rdev       = 0;
            vnode_params.vnfs_filesize   = 0;
            vnode_params.vnfs_cnp        = NULL;

            vnode_params.vnfs_flags      = VNFS_ADDFSREF | VNFS_NOCACHE;

            rc = vnode_create(VNCREATE_FLAVOR, sizeof(vnode_params), &vnode_params, &vnode);
            if (rc == 0)
                *ret = vnode;

            return 0;
        }
        else
        {
            PDEBUG("Unable to allocate lock");
            rc = ENOMEM;
        }

        lck_attr_free(pVnodeData->pLockAttr);
    }
    else
    {
        PDEBUG("Unable to allocate lock attr");
        rc = ENOMEM;
    }

    return rc;
}

/**
 * Convert guest absolute VFS path (starting from VFS root) to a host path
 * within mounted shared folder (returning it as a char *).
 *
 * @param mp            Mount data structure
 * @param pszGuestPath  Guest absolute VFS path (starting from VFS root)
 * @param cbGuestPath   Size of pszGuestPath
 * @param pszHostPath   Returned char * wich contains host path
 * @param cbHostPath    Returned pszHostPath size
 *
 * @return 0 on success, error code otherwise
 */
int
vboxvfs_guest_path_to_char_path_internal(mount_t mp, char *pszGuestPath, int cbGuestPath, char **pszHostPath, int *cbHostPath)
{
    vboxvfs_mount_t *pMount;

    /* Guest side: mount point path buffer and its size */
    char       *pszMntPointPath;
    int         cbMntPointPath = MAXPATHLEN;

    /* Host side: path within mounted shared folder and its size */
    char       *pszHostPathInternal;
    size_t      cbHostPathInternal;

    int rc;

    AssertReturn(mp, EINVAL);
    AssertReturn(pszGuestPath, EINVAL); AssertReturn(cbGuestPath >= 0, EINVAL);
    AssertReturn(pszHostPath,  EINVAL); AssertReturn(cbHostPath,       EINVAL);

    pMount = (vboxvfs_mount_t *)vfs_fsprivate(mp); AssertReturn(pMount, EINVAL); AssertReturn(pMount->pRootVnode, EINVAL);

    /* Get mount point path */
    pszMntPointPath = (char *)RTMemAllocZ(cbMntPointPath);
    if (pszMntPointPath)
    {
        rc = vn_getpath(pMount->pRootVnode, pszMntPointPath, &cbMntPointPath);
        if (rc == 0 && cbGuestPath >= cbMntPointPath)
        {
            cbHostPathInternal  = cbGuestPath - cbMntPointPath + 1;
            pszHostPathInternal = (char *)RTMemAllocZ(cbHostPathInternal);
            if (pszHostPathInternal)
            {
                memcpy(pszHostPathInternal, pszGuestPath + cbMntPointPath, cbGuestPath - cbMntPointPath);
                PDEBUG("guest<->host path converion result: '%s' mounted to '%s'", pszHostPathInternal, pszMntPointPath);

                RTMemFree(pszMntPointPath);

                *pszHostPath = pszHostPathInternal;
                *cbHostPath  = cbGuestPath - cbMntPointPath;

                return 0;

            }
            else
            {
                PDEBUG("No memory to allocate buffer for guest<->host path conversion (cbHostPathInternal)");
                rc = ENOMEM;
            }

        }
        else
        {
            PDEBUG("Unable to get guest vnode path: %d", rc);
        }

        RTMemFree(pszMntPointPath);
    }
    else
    {
        PDEBUG("No memory to allocate buffer for guest<->host path conversion (pszMntPointPath)");
        rc = ENOMEM;
    }

    return rc;
}

/**
 * Convert guest absolute VFS path (starting from VFS root) to a host path
 * within mounted shared folder.
 *
 * @param mp            Mount data structure
 * @param pszGuestPath  Guest absolute VFS path (starting from VFS root)
 * @param cbGuestPath   Size of pszGuestPath
 * @param ppResult      Returned PSHFLSTRING object wich contains host path
 *
 * @return 0 on success, error code otherwise
 */
int
vboxvfs_guest_path_to_shflstring_path_internal(mount_t mp, char *pszGuestPath, int cbGuestPath, PSHFLSTRING *ppResult)
{
    vboxvfs_mount_t *pMount;

    /* Guest side: mount point path buffer and its size */
    char       *pszMntPointPath;
    int         cbMntPointPath = MAXPATHLEN;

    /* Host side: path within mounted shared folder and its size */
    PSHFLSTRING pSFPath;
    size_t      cbSFPath;

    int rc;

    AssertReturn(mp, EINVAL);
    AssertReturn(pszGuestPath, EINVAL);
    AssertReturn(cbGuestPath >= 0, EINVAL);

    char *pszHostPath;
    int   cbHostPath;

    rc = vboxvfs_guest_path_to_char_path_internal(mp, pszGuestPath, cbGuestPath, &pszHostPath, &cbHostPath);
    if (rc == 0)
    {
        cbSFPath = offsetof(SHFLSTRING, String.utf8) + (size_t)cbHostPath + 1;
        pSFPath  = (PSHFLSTRING)RTMemAllocZ(cbSFPath);
        if (pSFPath)
        {
            pSFPath->u16Length = cbHostPath;
            pSFPath->u16Size   = cbHostPath + 1;
            memcpy(pSFPath->String.utf8, pszHostPath, cbHostPath);
            vboxvfs_put_path_internal((void **)&pszHostPath);

            *ppResult = pSFPath;
        }
    }

    return rc;
}

/**
 * Wrapper function for vboxvfs_guest_path_to_char_path_internal() which
 * converts guest path to host path using vnode object information.
 *
 * @param vnode         Guest's VFS object
 * @param ppHostPath    Allocated  char * which contain a path
 * @param pcbPath       Size of ppPath
 *
 * @return 0 on success, error code otherwise.
 */
int
vboxvfs_guest_vnode_to_char_path_internal(vnode_t vnode, char **ppHostPath, int *pcbHostPath)
{
    mount_t     mp;
    int         rc;

    char       *pszPath;
    int         cbPath = MAXPATHLEN;

    AssertReturn(ppHostPath,   EINVAL);
    AssertReturn(pcbHostPath,  EINVAL);
    AssertReturn(vnode,    EINVAL);
    mp = vnode_mount(vnode); AssertReturn(mp, EINVAL);

    pszPath = (char *)RTMemAllocZ(cbPath);
    if (pszPath)
    {
        rc = vn_getpath(vnode, pszPath, &cbPath);
        if (rc == 0)
        {
            return vboxvfs_guest_path_to_char_path_internal(mp, pszPath, cbPath, ppHostPath, pcbHostPath);
        }
    }
    else
    {
        rc = ENOMEM;
    }

    return rc;
}

/**
 * Wrapper function for vboxvfs_guest_path_to_shflstring_path_internal() which
 * converts guest path to host path using vnode object information.
 *
 * @param vnode     Guest's VFS object
 * @param ppResult  Allocated  PSHFLSTRING object which contain a path
 *
 * @return 0 on success, error code otherwise.
 */
int
vboxvfs_guest_vnode_to_shflstring_path_internal(vnode_t vnode, PSHFLSTRING *ppResult)
{
    mount_t     mp;
    int         rc;

    char       *pszPath;
    int         cbPath = MAXPATHLEN;

    AssertReturn(ppResult, EINVAL);
    AssertReturn(vnode,    EINVAL);
    mp = vnode_mount(vnode); AssertReturn(mp, EINVAL);

    pszPath = (char *)RTMemAllocZ(cbPath);
    if (pszPath)
    {
        rc = vn_getpath(vnode, pszPath, &cbPath);
        if (rc == 0)
        {
            return vboxvfs_guest_path_to_shflstring_path_internal(mp, pszPath, cbPath, ppResult);
        }
    }
    else
    {
        rc = ENOMEM;
    }

    return rc;
}


/**
 * Free resources allocated by vboxvfs_path_internal() and vboxvfs_guest_vnode_to_shflstring_path_internal().
 *
 * @param ppHandle  Reference to object to be freed.
 */
void
vboxvfs_put_path_internal(void **ppHandle)
{
    AssertReturnVoid(ppHandle);
    AssertReturnVoid(*ppHandle);
    RTMemFree(*ppHandle);
}

static void
vboxvfs_g2h_mode_dump_inernal(uint32_t fHostMode)
{
    PDEBUG("Host VFS object  flags (0x%X) dump:", (int)fHostMode);

    if (fHostMode & SHFL_CF_ACCESS_READ)                PDEBUG("SHFL_CF_ACCESS_READ");
    if (fHostMode & SHFL_CF_ACCESS_WRITE)               PDEBUG("SHFL_CF_ACCESS_WRITE");
    if (fHostMode & SHFL_CF_ACCESS_APPEND)              PDEBUG("SHFL_CF_ACCESS_APPEND");

    if ((fHostMode & (SHFL_CF_ACT_FAIL_IF_EXISTS    |
                      SHFL_CF_ACT_REPLACE_IF_EXISTS |
                      SHFL_CF_ACT_OVERWRITE_IF_EXISTS)) == 0)
                                                        PDEBUG("SHFL_CF_ACT_OPEN_IF_EXISTS");

    if (fHostMode & SHFL_CF_ACT_CREATE_IF_NEW)          PDEBUG("SHFL_CF_ACT_CREATE_IF_NEW");
    if (fHostMode & SHFL_CF_ACT_FAIL_IF_NEW)            PDEBUG("SHFL_CF_ACT_FAIL_IF_NEW");
    if (fHostMode & SHFL_CF_ACT_OVERWRITE_IF_EXISTS)    PDEBUG("SHFL_CF_ACT_OVERWRITE_IF_EXISTS");
    if (fHostMode & SHFL_CF_DIRECTORY)                  PDEBUG("SHFL_CF_DIRECTORY");

    PDEBUG("Done");
}


/**
 * Open existing VBoxVFS object and return its handle.
 *
 * @param pMount   Mount session data.
 * @param pPath             VFS path to the object relative to mount point.
 * @param fFlags            For directory object it should be
 *                          SHFL_CF_DIRECTORY and 0 for any other object.
 * @param pHandle           Returned handle.
 *
 * @return 0 on success, error code otherwise.
 */
int
vboxvfs_open_internal(vboxvfs_mount_t *pMount, PSHFLSTRING pPath, uint32_t fFlags, SHFLHANDLE *pHandle)
{
    SHFLCREATEPARMS parms;

    int rc;

    AssertReturn(pMount,      EINVAL);
    AssertReturn(pPath,                EINVAL);
    AssertReturn(pHandle,              EINVAL);

    bzero(&parms, sizeof(parms));

    vboxvfs_g2h_mode_dump_inernal(fFlags);

    parms.Handle        = SHFL_HANDLE_NIL;
    parms.Info.cbObject = 0;
    parms.CreateFlags   = fFlags;

    rc = VbglR0SfCreate(&g_SfClientDarwin, &pMount->pMap, pPath, &parms);
    if (RT_SUCCESS(rc))
    {
        *pHandle = parms.Handle;
    }
    else
    {
        PDEBUG("vboxvfs_open_internal() failed: %d", rc);
    }

    return rc;
}

/**
 * Release VBoxVFS object handle openned by vboxvfs_open_internal().
 *
 * @param pMount   Mount session data.
 * @param pHandle           Handle to close.
 *
 * @return 0 on success, IPRT error code otherwise.
 */
int
vboxvfs_close_internal(vboxvfs_mount_t *pMount, SHFLHANDLE pHandle)
{
    AssertReturn(pMount, EINVAL);
    return VbglR0SfClose(&g_SfClientDarwin, &pMount->pMap, pHandle);
}

/**
 * Get information about host VFS object.
 *
 * @param mp           Mount point data
 * @param pSHFLDPath   Path to VFS object within mounted shared folder
 * @param Info         Returned info
 *
 * @return  0 on success, error code otherwise.
 */
int
vboxvfs_get_info_internal(mount_t mp, PSHFLSTRING pSHFLDPath, PSHFLFSOBJINFO Info)
{
    vboxvfs_mount_t        *pMount;
    SHFLCREATEPARMS         parms;

    int rc;

    AssertReturn(mp, EINVAL);
    AssertReturn(pSHFLDPath, EINVAL);
    AssertReturn(Info, EINVAL);

    pMount = (vboxvfs_mount_t *)vfs_fsprivate(mp); AssertReturn(pMount, EINVAL);

    parms.Handle = 0;
    parms.Info.cbObject = 0;
    parms.CreateFlags = SHFL_CF_LOOKUP | SHFL_CF_ACT_FAIL_IF_NEW;

    rc = VbglR0SfCreate(&g_SfClientDarwin, &pMount->pMap, pSHFLDPath, &parms);
    if (rc == 0)
        *Info = parms.Info;

    return rc;
}

/**
 * Check if VFS object exists on a host side.
 *
 * @param vnode     Guest VFS vnode that corresponds to host VFS object
 *
 * @return 1 if exists, 0 otherwise.
 */
int
vboxvfs_exist_internal(vnode_t vnode)
{
    int rc;

    PSHFLSTRING   pSFPath = NULL;
    SHFLHANDLE    handle;
    uint32_t      fFlags;

    vboxvfs_mount_t        *pMount;
    mount_t                 mp;

    /* Return FALSE if invalid parameter given */
    AssertReturn(vnode, 0);

    mp = vnode_mount(vnode); AssertReturn(mp,  EINVAL);
    pMount = (vboxvfs_mount_t *)vfs_fsprivate(mp); AssertReturn(pMount, EINVAL);

    fFlags  = (vnode_isdir(vnode)) ? SHFL_CF_DIRECTORY : 0;
    fFlags |= SHFL_CF_ACCESS_READ | SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW;

    rc = vboxvfs_guest_vnode_to_shflstring_path_internal(vnode, &pSFPath); AssertReturn(rc == 0, rc);
    if (rc == 0)
    {
        rc = vboxvfs_open_internal(pMount, pSFPath, fFlags, &handle);
        if (rc == 0)
        {
            rc = vboxvfs_close_internal(pMount, handle);
            if (rc != 0)
            {
                PDEBUG("Unable to close() VBoxVFS object handle while checking if object exist on host: %d", rc);
            }
        }
    }

    vboxvfs_put_path_internal((void **)&pSFPath);

    return (rc == 0);
}

/**
 * Convert host VFS object mode flags into guest ones.
 *
 * @param fHostMode     Host flags
 *
 * @return Guest flags
 */
mode_t
vboxvfs_h2g_mode_inernal(RTFMODE fHostMode)
{
    mode_t fGuestMode = 0;

    fGuestMode = /* Owner */
                 ((fHostMode & RTFS_UNIX_IRUSR)  ? S_IRUSR  : 0 ) |
                 ((fHostMode & RTFS_UNIX_IWUSR)  ? S_IWUSR  : 0 ) |
                 ((fHostMode & RTFS_UNIX_IXUSR)  ? S_IXUSR  : 0 ) |
                 /* Group */
                 ((fHostMode & RTFS_UNIX_IRGRP)  ? S_IRGRP  : 0 ) |
                 ((fHostMode & RTFS_UNIX_IWGRP)  ? S_IWGRP  : 0 ) |
                 ((fHostMode & RTFS_UNIX_IXGRP)  ? S_IXGRP  : 0 ) |
                 /* Other */
                 ((fHostMode & RTFS_UNIX_IROTH)  ? S_IROTH  : 0 ) |
                 ((fHostMode & RTFS_UNIX_IWOTH)  ? S_IWOTH  : 0 ) |
                 ((fHostMode & RTFS_UNIX_IXOTH)  ? S_IXOTH  : 0 ) |
                 /* SUID, SGID, SVTXT */
                 ((fHostMode & RTFS_UNIX_ISUID)  ? S_ISUID  : 0 ) |
                 ((fHostMode & RTFS_UNIX_ISGID)  ? S_ISGID  : 0 ) |
                 ((fHostMode & RTFS_UNIX_ISTXT)  ? S_ISVTX  : 0 ) |
                 /* VFS object types */
                 ((RTFS_IS_FIFO(fHostMode))      ? S_IFIFO  : 0 ) |
                 ((RTFS_IS_DEV_CHAR(fHostMode))  ? S_IFCHR  : 0 ) |
                 ((RTFS_IS_DIRECTORY(fHostMode)) ? S_IFDIR  : 0 ) |
                 ((RTFS_IS_DEV_BLOCK(fHostMode)) ? S_IFBLK  : 0 ) |
                 ((RTFS_IS_FILE(fHostMode))      ? S_IFREG  : 0 ) |
                 ((RTFS_IS_SYMLINK(fHostMode))   ? S_IFLNK  : 0 ) |
                 ((RTFS_IS_SOCKET(fHostMode))    ? S_IFSOCK : 0 );

    return fGuestMode;
}

/**
 * Convert guest VFS object mode flags into host ones.
 *
 * @param fGuestMode     Host flags
 *
 * @return Host flags
 */
uint32_t
vboxvfs_g2h_mode_inernal(mode_t fGuestMode)
{
    uint32_t fHostMode = 0;

    fHostMode = ((fGuestMode & FREAD)    ? SHFL_CF_ACCESS_READ   : 0 ) |
                ((fGuestMode & FWRITE)   ? SHFL_CF_ACCESS_WRITE  : 0 ) |
                /* skipped: O_NONBLOCK */
                ((fGuestMode & O_APPEND) ? SHFL_CF_ACCESS_APPEND : 0 ) |
                /* skipped: O_SYNC */
                /* skipped: O_SHLOCK */
                /* skipped: O_EXLOCK */
                /* skipped: O_ASYNC */
                /* skipped: O_FSYNC */
                /* skipped: O_NOFOLLOW */
                ((fGuestMode & O_CREAT)  ? SHFL_CF_ACT_CREATE_IF_NEW | (!(fGuestMode & O_TRUNC) ? SHFL_CF_ACT_OPEN_IF_EXISTS : 0)  : SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW ) |
                ((fGuestMode & O_TRUNC)  ? SHFL_CF_ACT_OVERWRITE_IF_EXISTS | SHFL_CF_ACCESS_WRITE                                  : 0 );
                /* skipped: O_EXCL */

    return fHostMode;
}

/**
 * Mount helper: Contruct SHFLSTRING which contains VBox share name or path.
 *
 * @returns Initialize string buffer on success, NULL if out of memory.
 * @param   pachName    The string to pack in a buffer.  Does not need to be
 *                      zero terminated.
 * @param   cchName     The length of pachName to use.  RTSTR_MAX for strlen.
 */
SHFLSTRING *
vboxvfs_construct_shflstring(const char *pachName, size_t cchName)
{
    AssertReturn(pachName, NULL);

    if (cchName == RTSTR_MAX)
        cchName = strlen(pachName);

    SHFLSTRING *pSHFLString = (SHFLSTRING *)RTMemAlloc(SHFLSTRING_HEADER_SIZE + cchName + 1);
    if (pSHFLString)
    {
        pSHFLString->u16Length = cchName;
        pSHFLString->u16Size   = cchName + 1;
        memcpy(pSHFLString->String.utf8, pachName, cchName);
        pSHFLString->String.utf8[cchName] = '\0';

        return pSHFLString;
    }
    return NULL;
}

#endif
