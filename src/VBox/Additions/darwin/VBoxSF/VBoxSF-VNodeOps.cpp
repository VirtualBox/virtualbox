/* $Id: VBoxSF-VNodeOps.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxSF - Darwin Shared Folders, VNode Operations.
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

#include <iprt/mem.h>
#include <iprt/assert.h>
#include <VBox/log.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
struct default_error_args_hack
{
    struct default_error_vdesc_hack
    {
        int vdesc_offset;
        const char *vdesc_name;
    } const *a_desc;
};



/**
 * Default implementation that returns ENOTSUP.
 */
static int vboxSfDwnVnDefaultError(struct default_error_args_hack *pArgs)
{
    Log(("vboxSfDwnVnDefaultError: %s\n", RT_VALID_PTR(pArgs) && RT_VALID_PTR(pArgs->a_desc) ? pArgs->a_desc->vdesc_name : "??"));
    RT_NOREF(pArgs);
    return ENOTSUP;
}


static int vboxFsDwnVnGetAttr(struct vnop_getattr_args *pArgs)
{
#if 1
    RT_NOREF(pArgs);
    return ENOTSUP;
#else

    vboxvfs_mount_t   *pMount;
    struct vnode_attr *vnode_args;
    vboxvfs_vnode_t   *pVnodeData;

    struct timespec timespec;

    SHFLFSOBJINFO Info;
    mount_t       mp;
    vnode_t       vnode;
    int           rc;

    PDEBUG("Getting vnode attribute...");

    AssertReturn(pArgs, EINVAL);

    vnode                = pArgs->a_vp;                                   AssertReturn(vnode,           EINVAL);
    vnode_args           = pArgs->a_vap;                                  AssertReturn(vnode_args,      EINVAL);
    mp                   = vnode_mount(vnode);                           AssertReturn(mp,              EINVAL);
    pMount      = (vboxvfs_mount_t *)vfs_fsprivate(mp);     AssertReturn(pMount, EINVAL);
    pVnodeData           = (vboxvfs_vnode_t *)vnode_fsnode(vnode);  AssertReturn(pVnodeData,      EINVAL);

    lck_rw_lock_shared(pVnodeData->pLock);

    rc = vboxvfs_get_info_internal(mp, pVnodeData->pPath, &Info);
    if (rc == 0)
    {
        /* Set timestamps */
        RTTimeSpecGetTimespec(&Info.BirthTime,        &timespec); VATTR_RETURN(vnode_args, va_create_time, timespec);
        RTTimeSpecGetTimespec(&Info.AccessTime,       &timespec); VATTR_RETURN(vnode_args, va_access_time, timespec);
        RTTimeSpecGetTimespec(&Info.ModificationTime, &timespec); VATTR_RETURN(vnode_args, va_modify_time, timespec);
        RTTimeSpecGetTimespec(&Info.ChangeTime,       &timespec); VATTR_RETURN(vnode_args, va_change_time, timespec);
        VATTR_CLEAR_ACTIVE(vnode_args, va_backup_time);

        /* Set owner info. */
        VATTR_RETURN(vnode_args, va_uid, pMount->owner);
        VATTR_CLEAR_ACTIVE(vnode_args, va_gid);

        /* Access mode and flags */
        VATTR_RETURN(vnode_args, va_mode,  vboxvfs_h2g_mode_inernal(Info.Attr.fMode));
        VATTR_RETURN(vnode_args, va_flags, Info.Attr.u.Unix.fFlags);

        /* The current generation number (0 if this information is not available) */
        VATTR_RETURN(vnode_args, va_gen, Info.Attr.u.Unix.GenerationId);

        VATTR_RETURN(vnode_args, va_rdev,  0);
        VATTR_RETURN(vnode_args, va_nlink, 2);

        VATTR_RETURN(vnode_args, va_data_size, sizeof(struct dirent)); /* Size of data returned per each readdir() request */

        /* Hope, when it overflows nothing catastrophical will heppen! If we will not assign
         * a uniq va_fileid to each vnode, `ls`, 'find' (and simmilar tools that uses fts_read() calls) will think that
         * each sub-directory is self-cycled. */
        VATTR_RETURN(vnode_args, va_fileid, (pMount->cFileIdCounter++));

        /* Not supported */
        VATTR_CLEAR_ACTIVE(vnode_args, va_linkid);
        VATTR_CLEAR_ACTIVE(vnode_args, va_parentid);
        VATTR_CLEAR_ACTIVE(vnode_args, va_fsid);
        VATTR_CLEAR_ACTIVE(vnode_args, va_filerev);

        /* Not present on 10.6 */
        //VATTR_CLEAR_ACTIVE(vnode_args, va_addedtime);

        /** @todo take care about va_encoding (file name encoding) */
        VATTR_CLEAR_ACTIVE(vnode_args, va_encoding);
        /** @todo take care about: va_acl */
        VATTR_CLEAR_ACTIVE(vnode_args, va_acl);

        VATTR_CLEAR_ACTIVE(vnode_args, va_name);
        VATTR_CLEAR_ACTIVE(vnode_args, va_uuuid);
        VATTR_CLEAR_ACTIVE(vnode_args, va_guuid);

        VATTR_CLEAR_ACTIVE(vnode_args, va_total_size);
        VATTR_CLEAR_ACTIVE(vnode_args, va_total_alloc);
        VATTR_CLEAR_ACTIVE(vnode_args, va_data_alloc);
        VATTR_CLEAR_ACTIVE(vnode_args, va_iosize);

        VATTR_CLEAR_ACTIVE(vnode_args, va_nchildren);
        VATTR_CLEAR_ACTIVE(vnode_args, va_dirlinkcount);
    }
    else
    {
        PDEBUG("getattr: unable to get VBoxVFS object info");
    }

    lck_rw_unlock_shared(pVnodeData->pLock);

    return rc;
#endif
}

#if 0
/**
 * Helper function for vboxvfs_vnode_lookup(): create new vnode.
 */
static int
vboxvfs_vnode_lookup_instantinate_vnode(vnode_t parent_vnode, char *entry_name, vnode_t *result_vnode)
{
    /* We need to construct full path to vnode in order to get
     * vboxvfs_get_info_internal() to understand us! */

    char *pszCurDirPath;
    int   cbCurDirPath = MAXPATHLEN;

    mount_t mp = vnode_mount(parent_vnode); AssertReturn(mp,  EINVAL);
    vnode_t vnode;

    int rc;

    pszCurDirPath = (char *)RTMemAllocZ(cbCurDirPath);
    if (pszCurDirPath)
    {
        rc = vn_getpath(parent_vnode, pszCurDirPath, &cbCurDirPath);
        if (rc == 0 && cbCurDirPath < MAXPATHLEN)
        {
            SHFLFSOBJINFO Info;
            PSHFLSTRING   pSHFLPath;

            /* Add '/' between path parts and truncate name if it is too long */
            strncat(pszCurDirPath, "/", 1); strncat(pszCurDirPath, entry_name, MAXPATHLEN - cbCurDirPath - 1);

            rc = vboxvfs_guest_path_to_shflstring_path_internal(mp, pszCurDirPath, strlen(pszCurDirPath) + 1, &pSHFLPath);
            if (rc == 0)
            {
                rc = vboxvfs_get_info_internal(mp, pSHFLPath, (PSHFLFSOBJINFO)&Info);
                if (rc == 0)
                {
                    enum vtype type;

                    if      (RTFS_IS_DIRECTORY(Info.Attr.fMode)) type = VDIR;
                    else if (RTFS_IS_FILE     (Info.Attr.fMode)) type = VREG;
                    else
                    {
                        PDEBUG("Not supported VFS object (%s) type: mode 0x%X",
                               entry_name,
                               Info.Attr.fMode);

                        RTMemFree(pszCurDirPath);
                        vboxvfs_put_path_internal((void **)&pSHFLPath);
                        return ENOENT;
                    }
                    /* Create new vnode */
                    rc = vboxvfs_create_vnode_internal(mp, type, parent_vnode, FALSE, pSHFLPath, &vnode);
                    if (rc == 0)
                    {
                        PDEBUG("new vnode object '%s' has been created", entry_name);

                        *result_vnode = vnode;
                        RTMemFree(pszCurDirPath);

                        return 0;
                    }
                    else
                        PDEBUG("Unable to create vnode: %d", rc);
                }
                else
                    PDEBUG("Unable to get host object info: %d", rc);

                vboxvfs_put_path_internal((void **)&pSHFLPath);
            }
            else
                PDEBUG("Unable to convert guest<->host path");
        }
        else
            PDEBUG("Unable to construct vnode path: %d", rc);

        RTMemFree(pszCurDirPath);
    }
    else
    {
        PDEBUG("Unable to allocate memory for path buffer");
        rc = ENOMEM;
    }

    return rc;
}

/**
 * Helper function for vboxvfs_vnode_lookup(): take care
 * about '.' and '..' directory entries.
 */
static int
vboxvfs_vnode_lookup_dot_handler(struct vnop_lookup_args *pArgs, vnode_t *result_vnode)
{
    vnode_t vnode = NULL;

    if (pArgs->a_cnp->cn_flags & ISDOTDOT)
    {
        vnode = vnode_getparent(pArgs->a_dvp);
        if (vnode)
        {
            PDEBUG("return parent directory");
            *result_vnode = vnode;
            return 0;
        }
        else
        {
            PDEBUG("return parent directory not found, return current directory");
            *result_vnode = pArgs->a_dvp;
            return 0;
        }
    }
    else if ((strncmp(pArgs->a_cnp->cn_nameptr, ".", 1) == 0) &&
             pArgs->a_cnp->cn_namelen == 1)
    {
        PDEBUG("return current directory");
        *result_vnode = pArgs->a_dvp;
        return 0;
    }

    return ENOENT;
}
#endif

static int vboxSfDwnVnLookup(struct vnop_lookup_args *pArgs)
{
#if 1
    RT_NOREF(pArgs);
    return ENOTSUP;
#else
    int rc;

    vnode_t          vnode;
    vboxvfs_vnode_t *pVnodeData;

    PDEBUG("Looking up for vnode...");

    AssertReturn(pArgs,                      EINVAL);
    AssertReturn(pArgs->a_dvp,               EINVAL);
    AssertReturn(vnode_isdir(pArgs->a_dvp),  EINVAL);
    AssertReturn(pArgs->a_cnp,               EINVAL);
    AssertReturn(pArgs->a_cnp->cn_nameptr,   EINVAL);
    AssertReturn(pArgs->a_vpp,               EINVAL);

    pVnodeData = (vboxvfs_vnode_t *)vnode_fsnode(pArgs->a_dvp);
    AssertReturn(pVnodeData, EINVAL);
    AssertReturn(pVnodeData->pLock, EINVAL);

    /*
    todo: take care about pArgs->a_cnp->cn_nameiop
    */

    if      (pArgs->a_cnp->cn_nameiop == LOOKUP) PDEBUG("LOOKUP");
    else if (pArgs->a_cnp->cn_nameiop == CREATE) PDEBUG("CREATE");
    else if (pArgs->a_cnp->cn_nameiop == RENAME) PDEBUG("RENAME");
    else if (pArgs->a_cnp->cn_nameiop == DELETE) PDEBUG("DELETE");
    else PDEBUG("Unknown cn_nameiop: 0x%X", (int)pArgs->a_cnp->cn_nameiop);

    lck_rw_lock_exclusive(pVnodeData->pLock);

    /* Take care about '.' and '..' entries */
    if (vboxvfs_vnode_lookup_dot_handler(pArgs, &vnode) == 0)
    {
        vnode_get(vnode);
        *pArgs->a_vpp = vnode;

        lck_rw_unlock_exclusive(pVnodeData->pLock);

        return 0;
    }

    /* Look into VFS cache and attempt to find previously allocated vnode there. */
    rc = cache_lookup(pArgs->a_dvp, &vnode, pArgs->a_cnp);
    if (rc == -1) /* Record found */
    {
        PDEBUG("Found record in VFS cache");

        /* Check if VFS object still exist on a host side */
        if (vboxvfs_exist_internal(vnode))
        {
            /* Prepare & return cached vnode */
            vnode_get(vnode);
            *pArgs->a_vpp = vnode;

            rc = 0;
        }
        else
        {
            /* If vnode exist in guets VFS cache, but not exist on a host -- just forget it. */
            cache_purge(vnode);
            /** @todo free vnode data here */
            rc = ENOENT;
        }
    }
    else
    {
        PDEBUG("cache_lookup() returned %d, create new VFS vnode", rc);

        rc = vboxvfs_vnode_lookup_instantinate_vnode(pArgs->a_dvp, pArgs->a_cnp->cn_nameptr, &vnode);
        if (rc == 0)
        {
            cache_enter(pArgs->a_dvp, vnode, pArgs->a_cnp);
            *pArgs->a_vpp = vnode;
        }
        else
        {
            rc = ENOENT;
        }
    }

    lck_rw_unlock_exclusive(pVnodeData->pLock);

    return rc;
#endif
}

static int vboxSfDwnVnOpen(struct vnop_open_args *pArgs)
{
#if 1
    RT_NOREF(pArgs);
    return ENOTSUP;
#else
    vnode_t           vnode;
    vboxvfs_vnode_t  *pVnodeData;
    uint32_t          fHostFlags;
    mount_t           mp;
    vboxvfs_mount_t  *pMount;

    int rc;

    PDEBUG("Opening vnode...");

    AssertReturn(pArgs, EINVAL);

    vnode           = pArgs->a_vp;                              AssertReturn(vnode,      EINVAL);
    pVnodeData      = (vboxvfs_vnode_t *)vnode_fsnode(vnode);  AssertReturn(pVnodeData, EINVAL);
    mp              = vnode_mount(vnode);                      AssertReturn(mp,         EINVAL);
    pMount = (vboxvfs_mount_t *)vfs_fsprivate(mp);             AssertReturn(pMount,     EINVAL);

    lck_rw_lock_exclusive(pVnodeData->pLock);

    if (vnode_isinuse(vnode, 0))
    {
        PDEBUG("vnode '%s' (handle 0x%X) already has VBoxVFS object handle assigned, just return ok",
               (char *)pVnodeData->pPath->String.utf8,
               (int)pVnodeData->pHandle);

        lck_rw_unlock_exclusive(pVnodeData->pLock);
        return 0;
    }

    /* At this point we must make sure that nobody is using VBoxVFS object handle */
    //if (pVnodeData->Handle != SHFL_HANDLE_NIL)
    //{
    //    PDEBUG("vnode has active VBoxVFS object handle set, aborting");
    //    lck_rw_unlock_exclusive(pVnodeData->pLock);
    //    return EINVAL;
    //}

    fHostFlags  = vboxvfs_g2h_mode_inernal(pArgs->a_mode);
    fHostFlags |= (vnode_isdir(vnode) ? SHFL_CF_DIRECTORY : 0);

    SHFLHANDLE Handle;
    rc = vboxvfs_open_internal(pMount, pVnodeData->pPath, fHostFlags, &Handle);
    if (rc == 0)
    {
        PDEBUG("Open success: '%s' (handle 0x%X)",
               (char *)pVnodeData->pPath->String.utf8,
               (int)Handle);

        pVnodeData->pHandle = Handle;
    }
    else
    {
        PDEBUG("Unable to open: '%s': %d",
               (char *)pVnodeData->pPath->String.utf8,
               rc);
    }

    lck_rw_unlock_exclusive(pVnodeData->pLock);

    return rc;
#endif
}

static int vboxSfDwnVnClose(struct vnop_close_args *pArgs)
{
#if 1
    RT_NOREF(pArgs);
    return ENOTSUP;
#else

    vnode_t          vnode;
    mount_t          mp;
    vboxvfs_vnode_t *pVnodeData;
    vboxvfs_mount_t *pMount;

    int rc;

    PDEBUG("Closing vnode...");

    AssertReturn(pArgs, EINVAL);

    vnode           = pArgs->a_vp;                              AssertReturn(vnode,      EINVAL);
    pVnodeData      = (vboxvfs_vnode_t *)vnode_fsnode(vnode);  AssertReturn(pVnodeData, EINVAL);
    mp              = vnode_mount(vnode);                      AssertReturn(mp,         EINVAL);
    pMount = (vboxvfs_mount_t *)vfs_fsprivate(mp);             AssertReturn(pMount,     EINVAL);

    lck_rw_lock_exclusive(pVnodeData->pLock);

    if (vnode_isinuse(vnode, 0))
    {
        PDEBUG("vnode '%s' (handle 0x%X) is still in use, just return ok",
               (char *)pVnodeData->pPath->String.utf8,
               (int)pVnodeData->pHandle);

        lck_rw_unlock_exclusive(pVnodeData->pLock);
        return 0;
    }

    /* At this point we must make sure that vnode has VBoxVFS object handle assigned */
    if (pVnodeData->pHandle == SHFL_HANDLE_NIL)
    {
        PDEBUG("vnode has no active VBoxVFS object handle set, aborting");
        lck_rw_unlock_exclusive(pVnodeData->pLock);
        return EINVAL;
    }

    rc = vboxvfs_close_internal(pMount, pVnodeData->pHandle);
    if (rc == 0)
    {
        PDEBUG("Close success: '%s' (handle 0x%X)",
               (char *)pVnodeData->pPath->String.utf8,
               (int)pVnodeData->pHandle);

        /* Forget about previously assigned VBoxVFS object handle */
        pVnodeData->pHandle = SHFL_HANDLE_NIL;
    }
    else
    {
        PDEBUG("Unable to close: '%s' (handle 0x%X): %d",
               (char *)pVnodeData->pPath->String.utf8,
               (int)pVnodeData->pHandle, rc);
    }

    lck_rw_unlock_exclusive(pVnodeData->pLock);

    return rc;
#endif
}

#if 0
/**
 * Convert SHFLDIRINFO to struct dirent and copy it back to user.
 */
static int
vboxvfs_vnode_readdir_copy_data(ino_t index, SHFLDIRINFO *Info, struct uio *uio, int *numdirent)
{
    struct dirent entry;

    int rc;

    entry.d_ino = index;
    entry.d_reclen = (__uint16_t)sizeof(entry);

    /* Detect dir entry type */
    if (RTFS_IS_DIRECTORY(Info->Info.Attr.fMode))
        entry.d_type = DT_DIR;
    else if (RTFS_IS_FILE(Info->Info.Attr.fMode))
        entry.d_type = DT_REG;
    else
    {
        PDEBUG("Unknown type of host file: mode 0x%X", (int)Info->Info.Attr.fMode);
        return ENOTSUP;
    }

    entry.d_namlen = (__uint8_t)min(sizeof(entry.d_name), Info->name.u16Size);
    memcpy(entry.d_name, Info->name.String.utf8, entry.d_namlen);

    rc = uiomove((char *)&entry, sizeof(entry), uio);
    if (rc == 0)
    {
        uio_setoffset(uio, index * sizeof(struct dirent));
        *numdirent = (int)index;

        PDEBUG("discovered entry: '%s' (%d bytes), item #%d", entry.d_name, (int)entry.d_namlen, (int)index);
    }
    else
    {
        PDEBUG("Failed to return dirent data item #%d (%d)", (int)index, rc);
    }

    return rc;
}
#endif

static int vboxSfDwnVnReadDir(struct vnop_readdir_args *pArgs)
{
#if 1
    RT_NOREF(pArgs);
    return ENOTSUP;
#else
    vboxvfs_mount_t *pMount;
    vboxvfs_vnode_t *pVnodeData;
    SHFLDIRINFO     *Info;
    uint32_t         cbInfo;
    mount_t          mp;
    vnode_t          vnode;
    struct uio      *uio;

    int rc = 0, rc2;

    PDEBUG("Reading directory...");

    AssertReturn(pArgs,              EINVAL);
    AssertReturn(pArgs->a_eofflag,   EINVAL);
    AssertReturn(pArgs->a_numdirent, EINVAL);

    uio             = pArgs->a_uio;                             AssertReturn(uio,        EINVAL);
    vnode           = pArgs->a_vp;                              AssertReturn(vnode,      EINVAL); AssertReturn(vnode_isdir(vnode), EINVAL);
    pVnodeData      = (vboxvfs_vnode_t *)vnode_fsnode(vnode);  AssertReturn(pVnodeData, EINVAL);
    mp              = vnode_mount(vnode);                      AssertReturn(mp,         EINVAL);
    pMount = (vboxvfs_mount_t *)vfs_fsprivate(mp);             AssertReturn(pMount,     EINVAL);

    lck_rw_lock_shared(pVnodeData->pLock);

    cbInfo = sizeof(Info) + MAXPATHLEN;
    Info   = (SHFLDIRINFO *)RTMemAllocZ(cbInfo);
    if (!Info)
    {
        PDEBUG("No memory to allocate internal data");
        lck_rw_unlock_shared(pVnodeData->pLock);
        return ENOMEM;
    }

    uint32_t index = (uint32_t)uio_offset(uio) / (uint32_t)sizeof(struct dirent);
    uint32_t cFiles = 0;

    PDEBUG("Exploring VBoxVFS directory (%s), handle (0x%.8X), offset (0x%X), count (%d)", (char *)pVnodeData->pPath->String.utf8, (int)pVnodeData->pHandle, index, uio_iovcnt(uio));

    /* Currently, there is a problem when VbglR0SfDirInfo() is not able to
     * continue retrieve directory content if the same VBoxVFS handle is used.
     * This macro forces to use a new handle in readdir() callback. If enabled,
     * the original handle (obtained in open() callback is ignored). */

    SHFLHANDLE Handle;
    rc = vboxvfs_open_internal(pMount,
                               pVnodeData->pPath,
                               SHFL_CF_DIRECTORY | SHFL_CF_ACCESS_READ | SHFL_CF_ACT_OPEN_IF_EXISTS | SHFL_CF_ACT_FAIL_IF_NEW,
                               &Handle);
    if (rc != 0)
    {
        PDEBUG("Unable to open dir: %d", rc);
        RTMemFree(Info);
        lck_rw_unlock_shared(pVnodeData->pLock);
        return rc;
    }

#if 0
    rc = VbglR0SfDirInfo(&g_vboxSFClient, &pMount->pMap, Handle, 0, 0, index, &cbInfo, (PSHFLDIRINFO)Info, &cFiles);
#else
    SHFLSTRING *pMask = vboxvfs_construct_shflstring("*", strlen("*"));
    if (pMask)
    {
        for (uint32_t cSkip = 0; (cSkip < index + 1) && (rc == VINF_SUCCESS); cSkip++)
        {
            //rc = VbglR0SfDirInfo(&g_vboxSFClient, &pMount->pMap, Handle, 0 /* pMask */, 0 /* SHFL_LIST_RETURN_ONE */, 0, &cbInfo, (PSHFLDIRINFO)Info, &cFiles);

            uint32_t cbReturned = cbInfo;
            //rc = VbglR0SfDirInfo(&g_vboxSFClient, &pMount->pMap, Handle, pMask, SHFL_LIST_RETURN_ONE, 0, &cbReturned, (PSHFLDIRINFO)Info, &cFiles);
            rc = VbglR0SfDirInfo(&g_SfClientDarwin, &pMount->pMap, Handle, 0, SHFL_LIST_RETURN_ONE, 0,
                                 &cbReturned, (PSHFLDIRINFO)Info, &cFiles);

        }

        PDEBUG("read %d files", cFiles);
        RTMemFree(pMask);
    }
    else
    {
        PDEBUG("Can't alloc mask");
        rc = ENOMEM;
    }
#endif
    rc2 = vboxvfs_close_internal(pMount, Handle);
    if (rc2 != 0)
    {
        PDEBUG("Unable to close directory: %s: %d",
               pVnodeData->pPath->String.utf8,
               rc2);
    }

    switch (rc)
    {
        case VINF_SUCCESS:
        {
            rc = vboxvfs_vnode_readdir_copy_data((ino_t)(index + 1), Info, uio, pArgs->a_numdirent);
            break;
        }

        case VERR_NO_MORE_FILES:
        {
            PDEBUG("No more entries in directory");
            *(pArgs->a_eofflag) = 1;
            break;
        }

        default:
        {
            PDEBUG("VbglR0SfDirInfo() for item #%d has failed: %d", (int)index, (int)rc);
            rc = EINVAL;
            break;
        }
    }

    RTMemFree(Info);
    lck_rw_unlock_shared(pVnodeData->pLock);

    return rc;
#endif
}


static int vboxSfDwnVnPathConf(struct vnop_pathconf_args *pArgs)
{
    Log(("vboxSfDwnVnPathConf:\n"));
    RT_NOREF(pArgs);
    return 0;
}


/**
 * vnop_reclaim implementation.
 *
 * VBoxVFS reclaim callback.
 * Called when vnode is going to be deallocated. Should release
 * all the VBoxVFS resources that correspond to current vnode object.
 *
 * @param pArgs     Operation arguments passed from VFS layer.
 *
 * @return 0 on success, BSD error code otherwise.
 */
static int vboxSfDwnVnReclaim(struct vnop_reclaim_args *pArgs)
{
    AssertReturn(pArgs && pArgs->a_vp, EINVAL);

    /* Check that it's not a root node that's in use. */
    PVBOXSFMNTDATA pMntData = (PVBOXSFMNTDATA)vfs_fsprivate(vnode_mount(pArgs->a_vp));
    AssertReturn(!pMntData || pMntData->pVnRoot != pArgs->a_vp, EBUSY);

    /* Get the private data and free it. */
    PVBOXSFDWNVNDATA pVnData = (PVBOXSFDWNVNDATA)vnode_fsnode(pArgs->a_vp);
    AssertPtrReturn(pVnData, 0);

    if (pVnData->hHandle != SHFL_HANDLE_NIL)
    {
        /** @todo can this happen? */
        pVnData->hHandle = SHFL_HANDLE_NIL;
    }

    RTMemFree(pVnData);
    return 0;
}


/**
 * Allocates a vnode.
 *
 * @returns Pointer to the new VNode, NULL if out of memory.
 * @param   pMount          The file system mount structure.
 * @param   enmType         The vnode type.
 * @param   pParent         The parent vnode, NULL if root.
 * @param   cbFile          The file size
 */
vnode_t vboxSfDwnVnAlloc(mount_t pMount, enum vtype enmType, vnode_t pParent, uint64_t cbFile)
{
    /*
     * Create our private data.
     */
    PVBOXSFDWNVNDATA pVnData = (PVBOXSFDWNVNDATA)RTMemAllocZ(sizeof(*pVnData));
    if (pVnData)
    {
        pVnData->hHandle = SHFL_HANDLE_NIL;

        struct vnode_fsparam VnParms;
        RT_ZERO(VnParms);
        VnParms.vnfs_mp         = pMount;
        VnParms.vnfs_vtype      = enmType;
        VnParms.vnfs_str        = "vboxsf";
        VnParms.vnfs_dvp        = pParent;
        VnParms.vnfs_fsnode     = pVnData;
        VnParms.vnfs_vops       = g_papfnVBoxSfDwnVnDirOpsVector;
        VnParms.vnfs_markroot   = pParent == NULL;
        VnParms.vnfs_marksystem = 0;
        VnParms.vnfs_rdev       = 0;
        VnParms.vnfs_filesize   = cbFile;
        VnParms.vnfs_cnp        = 0;
        VnParms.vnfs_flags      = VNFS_NOCACHE;

        vnode_t pVnRet;
        int rc = vnode_create(VNCREATE_FLAVOR, VCREATESIZE, &VnParms, &pVnRet);
        if (rc == 0)
            return pVnRet;
        RTMemFree(pVnData);
    }
    printf("vboxSfDwnVnAlloc: out of memory!\n");
    return NULL;
}


/**
 * Vnode operations.
 */
static struct vnodeopv_entry_desc g_VBoxSfDirOpsDescList[] =
{
#define VNODEOPFUNC int(*)(void *)
    { &vnop_default_desc,     (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    //{ &vnop_access_desc,      (VNODEOPFUNC)vboxSfDwnVnDefaultError }, - probably not needed.
    //{ &vnop_advlock_desc,     (VNODEOPFUNC)vboxSfDwnVnDefaultError }, - later.
    //{ &vnop_allocate_desc,    (VNODEOPFUNC)vboxSfDwnVnDefaultError }, - maybe, need shfl function
    { &vnop_blktooff_desc,    (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    //{ &vnop_blockmap_desc,    (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    //{ &vnop_bwrite_desc,      (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_close_desc,       (VNODEOPFUNC)vboxSfDwnVnClose },
    //{ &vnop_copyfile_desc,    (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_create_desc,      (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    //{ &vnop_exchange_desc,    (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_fsync_desc,       (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_getattr_desc,     (VNODEOPFUNC)vboxFsDwnVnGetAttr },
    //{ &vnop_getnamedstream_desc, (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    //{ &vnop_getxattr_desc,    (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_inactive_desc,    (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_ioctl_desc,       (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_link_desc,        (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    //{ &vnop_listxattr_desc,   (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_lookup_desc,      (VNODEOPFUNC)vboxSfDwnVnLookup },
    { &vnop_mkdir_desc,       (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_mknod_desc,       (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_mmap_desc,        (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_mnomap_desc,      (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_offtoblk_desc,    (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_open_desc,        (VNODEOPFUNC)vboxSfDwnVnOpen },
    { &vnop_pagein_desc,      (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_pageout_desc,     (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_pathconf_desc,    (VNODEOPFUNC)vboxSfDwnVnPathConf },
    /* { &vnop_print_desc,       (VNODEOPFUNC)vboxSfDwnVnDefaultError }, undefined in ML */
    { &vnop_read_desc,        (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_readdir_desc,     (VNODEOPFUNC)vboxSfDwnVnReadDir },
    //{ &vnop_readdirattr_desc, (VNODEOPFUNC)vboxSfDwnVnDefaultError }, - hfs specific.
    { &vnop_readlink_desc,    (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_reclaim_desc,     (VNODEOPFUNC)vboxSfDwnVnReclaim },
    { &vnop_remove_desc,      (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    //{ &vnop_removexattr_desc, (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_rename_desc,      (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    //{ &vnop_revoke_desc,      (VNODEOPFUNC)vboxSfDwnVnDefaultError }, - not needed
    { &vnop_rmdir_desc,       (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_searchfs_desc,    (VNODEOPFUNC)err_searchfs },
    //{ &vnop_select_desc,      (VNODEOPFUNC)vboxSfDwnVnDefaultError }, - not needed
    { &vnop_setattr_desc,     (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { &vnop_setxattr_desc,    (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    //{ &vnop_strategy_desc,    (VNODEOPFUNC)vboxSfDwnVnDefaultError }, - not needed
    { &vnop_symlink_desc,     (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    /* { &vnop_truncate_desc,    (VNODEOPFUNC)vboxSfDwnVnDefaultError }, undefined in ML */
    //{ &vnop_whiteout_desc,    (VNODEOPFUNC)vboxSfDwnVnDefaultError }, - not needed/supported
    { &vnop_write_desc,       (VNODEOPFUNC)vboxSfDwnVnDefaultError },
    { NULL,                   (VNODEOPFUNC)NULL              },
#undef VNODEOPFUNC
};

/** ??? */
int (**g_papfnVBoxSfDwnVnDirOpsVector)(void *);

/**
 * VNode operation descriptors.
 */
struct vnodeopv_desc g_VBoxSfVnodeOpvDesc =
{
    &g_papfnVBoxSfDwnVnDirOpsVector,
    g_VBoxSfDirOpsDescList
};

