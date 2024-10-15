/* $Id: VBoxSF-VfsOps.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxFS - Darwin Shared Folders, Virtual File System Operations.
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
#include <iprt/asm.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <VBox/log.h>



/**
 * vfsops::vfs_getattr implementation.
 *
 * @returns 0 on success or errno.h value on failure.
 * @param   pMount      The mount data structure.
 * @param   pFsAttr     Input & output structure.
 * @param   pContext    Unused kAuth parameter.
 */
static int vboxSfDwnVfsGetAttr(mount_t pMount, struct vfs_attr *pFsAttr, vfs_context_t pContext)
{
    PVBOXSFMNTDATA pThis = (PVBOXSFMNTDATA)vfs_fsprivate(pMount);
    AssertReturn(pThis, EBADMSG);
    LogFlow(("vboxSfDwnVfsGetAttr: %s\n", pThis->MntInfo.szFolder));
    RT_NOREF(pContext);

    /*
     * Get the file system stats from the host.
     */
    int rc;
    struct MyEmbReq
    {
        VBGLIOCIDCHGCMFASTCALL  Hdr;
        VMMDevHGCMCall          Call;
        VBoxSFParmInformation   Parms;
        SHFLVOLINFO             VolInfo;
    } *pReq = (struct MyEmbReq *)VbglR0PhysHeapAlloc(sizeof(*pReq));
    if (pReq)
    {
        RT_ZERO(pReq->VolInfo);

        VBGLIOCIDCHGCMFASTCALL_INIT(&pReq->Hdr, VbglR0PhysHeapGetPhysAddr(pReq), &pReq->Call, g_SfClientDarwin.idClient,
                                    SHFL_FN_INFORMATION, SHFL_CPARMS_INFORMATION, sizeof(*pReq));
        pReq->Parms.id32Root.type               = VMMDevHGCMParmType_32bit;
        pReq->Parms.id32Root.u.value32          = pThis->hHostFolder.root;
        pReq->Parms.u64Handle.type              = VMMDevHGCMParmType_64bit;
        pReq->Parms.u64Handle.u.value64         = 0;
        pReq->Parms.f32Flags.type               = VMMDevHGCMParmType_32bit;
        pReq->Parms.f32Flags.u.value32          = SHFL_INFO_VOLUME | SHFL_INFO_GET;
        pReq->Parms.cb32.type                   = VMMDevHGCMParmType_32bit;
        pReq->Parms.cb32.u.value32              = sizeof(pReq->VolInfo);
        pReq->Parms.pInfo.type                  = VMMDevHGCMParmType_Embedded;
        pReq->Parms.pInfo.u.Embedded.cbData     = sizeof(pReq->VolInfo);
        pReq->Parms.pInfo.u.Embedded.offData    = RT_UOFFSETOF(struct MyEmbReq, VolInfo) - sizeof(VBGLIOCIDCHGCMFASTCALL);
        pReq->Parms.pInfo.u.Embedded.fFlags     = VBOX_HGCM_F_PARM_DIRECTION_FROM_HOST;

        int vrc = VbglR0HGCMFastCall(g_SfClientDarwin.handle, &pReq->Hdr, sizeof(*pReq));
        if (RT_SUCCESS(vrc))
            vrc = pReq->Call.header.result;
        if (RT_SUCCESS(vrc))
        {
            /*
             * Fill in stuff.
             */
            /* Copy over the results we got from the host. */
            uint32_t cbUnit = pReq->VolInfo.ulBytesPerSector * pReq->VolInfo.ulBytesPerAllocationUnit;
            VFSATTR_RETURN(pFsAttr, f_bsize, cbUnit);
            VFSATTR_RETURN(pFsAttr, f_iosize, _64K); /** @todo what's a good block size...  */
            VFSATTR_RETURN(pFsAttr, f_blocks, (uint64_t)pReq->VolInfo.ullTotalAllocationBytes / cbUnit);
            VFSATTR_RETURN(pFsAttr, f_bavail, (uint64_t)pReq->VolInfo.ullAvailableAllocationBytes / cbUnit);
            VFSATTR_RETURN(pFsAttr, f_bfree,  (uint64_t)pReq->VolInfo.ullAvailableAllocationBytes / cbUnit);
            VFSATTR_RETURN(pFsAttr, f_bused,
                           ((uint64_t)pReq->VolInfo.ullTotalAllocationBytes - (uint64_t)pReq->VolInfo.ullAvailableAllocationBytes) / cbUnit);
            fsid_t const fsid = { { vfs_statfs(pMount)->f_fsid.val[0], vfs_typenum(pMount) } };
            VFSATTR_RETURN(pFsAttr, f_fsid, fsid);

            /* f_owner is handled by caller. */
            /* f_signature is handled by caller. */

            struct timespec TmpTv = { 1084190406, 0 };
            VFSATTR_RETURN(pFsAttr, f_create_time, TmpTv);

            /*
             * Unsupported bits.
             */
            /* Dummies for some values we don't support. */
            VFSATTR_RETURN(pFsAttr, f_objcount, 0);
            VFSATTR_RETURN(pFsAttr, f_filecount, 0);
            VFSATTR_RETURN(pFsAttr, f_dircount, 0);
            VFSATTR_RETURN(pFsAttr, f_maxobjcount, UINT32_MAX);
            VFSATTR_RETURN(pFsAttr, f_files, UINT32_MAX);
            VFSATTR_RETURN(pFsAttr, f_ffree, UINT32_MAX);
            VFSATTR_RETURN(pFsAttr, f_fssubtype, 0);
            VFSATTR_RETURN(pFsAttr, f_carbon_fsid, 0);

            /* Totally not supported: */
            VFSATTR_CLEAR_ACTIVE(pFsAttr, f_modify_time);
            VFSATTR_CLEAR_ACTIVE(pFsAttr, f_access_time);
            VFSATTR_CLEAR_ACTIVE(pFsAttr, f_backup_time);

            /*
             * Annoying capability stuff.
             * The 'valid' bits are only supposed to be set when we know for sure.
             */
            if (VFSATTR_IS_ACTIVE(pFsAttr, f_capabilities))
            {
                vol_capabilities_attr_t *pCaps = &pFsAttr->f_capabilities;

                pCaps->valid[VOL_CAPABILITIES_FORMAT]            = VOL_CAP_FMT_PERSISTENTOBJECTIDS
                                                                 | VOL_CAP_FMT_SYMBOLICLINKS
                                                                 | VOL_CAP_FMT_HARDLINKS
                                                                 | VOL_CAP_FMT_JOURNAL
                                                                 | VOL_CAP_FMT_JOURNAL_ACTIVE
                                                                 | VOL_CAP_FMT_NO_ROOT_TIMES
                                                                 | VOL_CAP_FMT_SPARSE_FILES
                                                                 | VOL_CAP_FMT_ZERO_RUNS
                                                                 | VOL_CAP_FMT_CASE_SENSITIVE
                                                                 | VOL_CAP_FMT_CASE_PRESERVING
                                                                 | VOL_CAP_FMT_FAST_STATFS
                                                                 | VOL_CAP_FMT_2TB_FILESIZE
                                                                 | VOL_CAP_FMT_OPENDENYMODES
                                                                 | VOL_CAP_FMT_HIDDEN_FILES
                                                                 | VOL_CAP_FMT_PATH_FROM_ID
                                                                 | VOL_CAP_FMT_NO_VOLUME_SIZES
                                                                 | VOL_CAP_FMT_DECMPFS_COMPRESSION
                                                                 | VOL_CAP_FMT_64BIT_OBJECT_IDS;
                pCaps->capabilities[VOL_CAPABILITIES_FORMAT]     = VOL_CAP_FMT_2TB_FILESIZE
                                                                 /// @todo | VOL_CAP_FMT_SYMBOLICLINKS - later
                                                                 /// @todo | VOL_CAP_FMT_SPARSE_FILES - probably, needs testing.
                                                                 /*| VOL_CAP_FMT_CASE_SENSITIVE - case-insensitive */
                                                                 | VOL_CAP_FMT_CASE_PRESERVING
                                                                 /// @todo | VOL_CAP_FMT_HIDDEN_FILES - if windows host.
                                                                 /// @todo | VOL_CAP_FMT_OPENDENYMODES - if windows host.
                                                                 ;
                pCaps->valid[VOL_CAPABILITIES_INTERFACES]        = VOL_CAP_INT_SEARCHFS
                                                                 | VOL_CAP_INT_ATTRLIST
                                                                 | VOL_CAP_INT_NFSEXPORT
                                                                 | VOL_CAP_INT_READDIRATTR
                                                                 | VOL_CAP_INT_EXCHANGEDATA
                                                                 | VOL_CAP_INT_COPYFILE
                                                                 | VOL_CAP_INT_ALLOCATE
                                                                 | VOL_CAP_INT_VOL_RENAME
                                                                 | VOL_CAP_INT_ADVLOCK
                                                                 | VOL_CAP_INT_FLOCK
                                                                 | VOL_CAP_INT_EXTENDED_SECURITY
                                                                 | VOL_CAP_INT_USERACCESS
                                                                 | VOL_CAP_INT_MANLOCK
                                                                 | VOL_CAP_INT_NAMEDSTREAMS
                                                                 | VOL_CAP_INT_EXTENDED_ATTR;
                pCaps->capabilities[VOL_CAPABILITIES_INTERFACES] = 0
                                                                 /// @todo | VOL_CAP_INT_SEARCHFS
                                                                 /// @todo | VOL_CAP_INT_COPYFILE
                                                                 /// @todo | VOL_CAP_INT_READDIRATTR
                                                                 ;

                pCaps->valid[VOL_CAPABILITIES_RESERVED1]         = 0;
                pCaps->capabilities[VOL_CAPABILITIES_RESERVED1]  = 0;

                pCaps->valid[VOL_CAPABILITIES_RESERVED2]         = 0;
                pCaps->capabilities[VOL_CAPABILITIES_RESERVED2]  = 0;

                VFSATTR_SET_SUPPORTED(pFsAttr, f_capabilities);
            }


            /*
             * Annoying attribute stuff.
             * The 'valid' bits are only supposed to be set when we know for sure.
             */
            if (VFSATTR_IS_ACTIVE(pFsAttr, f_attributes))
            {
                vol_attributes_attr_t *pAt = &pFsAttr->f_attributes;

                pAt->validattr.commonattr  = ATTR_CMN_NAME
                                           | ATTR_CMN_DEVID
                                           | ATTR_CMN_FSID
                                           | ATTR_CMN_OBJTYPE
                                           | ATTR_CMN_OBJTAG
                                           | ATTR_CMN_OBJID
                                           | ATTR_CMN_OBJPERMANENTID
                                           | ATTR_CMN_PAROBJID
                                           | ATTR_CMN_SCRIPT
                                           | ATTR_CMN_CRTIME
                                           | ATTR_CMN_MODTIME
                                           | ATTR_CMN_CHGTIME
                                           | ATTR_CMN_ACCTIME
                                           | ATTR_CMN_BKUPTIME
                                           | ATTR_CMN_FNDRINFO
                                           | ATTR_CMN_OWNERID
                                           | ATTR_CMN_GRPID
                                           | ATTR_CMN_ACCESSMASK
                                           | ATTR_CMN_FLAGS
                                           | ATTR_CMN_USERACCESS
                                           | ATTR_CMN_EXTENDED_SECURITY
                                           | ATTR_CMN_UUID
                                           | ATTR_CMN_GRPUUID
                                           | ATTR_CMN_FILEID
                                           | ATTR_CMN_PARENTID
                                           | ATTR_CMN_FULLPATH
                                           | ATTR_CMN_ADDEDTIME;
                pAt->nativeattr.commonattr = ATTR_CMN_NAME
                                           | ATTR_CMN_DEVID
                                           | ATTR_CMN_FSID
                                           | ATTR_CMN_OBJTYPE
                                           | ATTR_CMN_OBJTAG
                                           | ATTR_CMN_OBJID
                                           //| ATTR_CMN_OBJPERMANENTID
                                           | ATTR_CMN_PAROBJID
                                           //| ATTR_CMN_SCRIPT
                                           | ATTR_CMN_CRTIME
                                           | ATTR_CMN_MODTIME
                                           | ATTR_CMN_CHGTIME
                                           | ATTR_CMN_ACCTIME
                                           //| ATTR_CMN_BKUPTIME
                                           //| ATTR_CMN_FNDRINFO
                                           //| ATTR_CMN_OWNERID
                                           //| ATTR_CMN_GRPID
                                           | ATTR_CMN_ACCESSMASK
                                           //| ATTR_CMN_FLAGS
                                           //| ATTR_CMN_USERACCESS
                                           //| ATTR_CMN_EXTENDED_SECURITY
                                           //| ATTR_CMN_UUID
                                           //| ATTR_CMN_GRPUUID
                                           | ATTR_CMN_FILEID
                                           | ATTR_CMN_PARENTID
                                           | ATTR_CMN_FULLPATH
                                           //| ATTR_CMN_ADDEDTIME
                                           ;
                pAt->validattr.volattr     = ATTR_VOL_FSTYPE
                                           | ATTR_VOL_SIGNATURE
                                           | ATTR_VOL_SIZE
                                           | ATTR_VOL_SPACEFREE
                                           | ATTR_VOL_SPACEAVAIL
                                           | ATTR_VOL_MINALLOCATION
                                           | ATTR_VOL_ALLOCATIONCLUMP
                                           | ATTR_VOL_IOBLOCKSIZE
                                           | ATTR_VOL_OBJCOUNT
                                           | ATTR_VOL_FILECOUNT
                                           | ATTR_VOL_DIRCOUNT
                                           | ATTR_VOL_MAXOBJCOUNT
                                           | ATTR_VOL_MOUNTPOINT
                                           | ATTR_VOL_NAME
                                           | ATTR_VOL_MOUNTFLAGS
                                           | ATTR_VOL_MOUNTEDDEVICE
                                           | ATTR_VOL_ENCODINGSUSED
                                           | ATTR_VOL_CAPABILITIES
                                           | ATTR_VOL_UUID
                                           | ATTR_VOL_ATTRIBUTES
                                           | ATTR_VOL_INFO;
                pAt->nativeattr.volattr     = ATTR_VOL_FSTYPE
                                            //| ATTR_VOL_SIGNATURE
                                            | ATTR_VOL_SIZE
                                            | ATTR_VOL_SPACEFREE
                                            | ATTR_VOL_SPACEAVAIL
                                            | ATTR_VOL_MINALLOCATION
                                            | ATTR_VOL_ALLOCATIONCLUMP
                                            | ATTR_VOL_IOBLOCKSIZE
                                            //| ATTR_VOL_OBJCOUNT
                                            //| ATTR_VOL_FILECOUNT
                                            //| ATTR_VOL_DIRCOUNT
                                            //| ATTR_VOL_MAXOBJCOUNT
                                            //| ATTR_VOL_MOUNTPOINT - ??
                                            | ATTR_VOL_NAME
                                            | ATTR_VOL_MOUNTFLAGS
                                            | ATTR_VOL_MOUNTEDDEVICE
                                            //| ATTR_VOL_ENCODINGSUSED
                                            | ATTR_VOL_CAPABILITIES
                                            //| ATTR_VOL_UUID
                                            | ATTR_VOL_ATTRIBUTES
                                            //| ATTR_VOL_INFO
                                            ;
                pAt->validattr.dirattr      = ATTR_DIR_LINKCOUNT
                                            | ATTR_DIR_ENTRYCOUNT
                                            | ATTR_DIR_MOUNTSTATUS;
                pAt->nativeattr.dirattr     = 0 //ATTR_DIR_LINKCOUNT
                                            | ATTR_DIR_ENTRYCOUNT
                                            | ATTR_DIR_MOUNTSTATUS
                                            ;
                pAt->validattr.fileattr     = ATTR_FILE_LINKCOUNT
                                            | ATTR_FILE_TOTALSIZE
                                            | ATTR_FILE_ALLOCSIZE
                                            | ATTR_FILE_IOBLOCKSIZE
                                            | ATTR_FILE_DEVTYPE
                                            | ATTR_FILE_FORKCOUNT
                                            | ATTR_FILE_FORKLIST
                                            | ATTR_FILE_DATALENGTH
                                            | ATTR_FILE_DATAALLOCSIZE
                                            | ATTR_FILE_RSRCLENGTH
                                            | ATTR_FILE_RSRCALLOCSIZE;
                pAt->nativeattr.fileattr    = 0
                                            //|ATTR_FILE_LINKCOUNT
                                            | ATTR_FILE_TOTALSIZE
                                            | ATTR_FILE_ALLOCSIZE
                                            //| ATTR_FILE_IOBLOCKSIZE
                                            | ATTR_FILE_DEVTYPE
                                            //| ATTR_FILE_FORKCOUNT
                                            //| ATTR_FILE_FORKLIST
                                            | ATTR_FILE_DATALENGTH
                                            | ATTR_FILE_DATAALLOCSIZE
                                            | ATTR_FILE_RSRCLENGTH
                                            | ATTR_FILE_RSRCALLOCSIZE
                                            ;
                pAt->validattr.forkattr     = ATTR_FORK_TOTALSIZE
                                            | ATTR_FORK_ALLOCSIZE;
                pAt->nativeattr.forkattr    = 0
                                            //| ATTR_FORK_TOTALSIZE
                                            //| ATTR_FORK_ALLOCSIZE
                                            ;
                VFSATTR_SET_SUPPORTED(pFsAttr, f_attributes);
            }

            if (VFSATTR_IS_ACTIVE(pFsAttr, f_vol_name))
            {
                RTStrCopy(pFsAttr->f_vol_name, MAXPATHLEN, pThis->MntInfo.szFolder);
                VFSATTR_SET_SUPPORTED(pFsAttr, f_vol_name);
            }

            rc = 0;
        }
        else
        {
            Log(("vboxSfOs2QueryFileInfo: VbglR0SfFsInfo failed: %Rrc\n", vrc));
            rc = RTErrConvertToErrno(vrc);
        }

        VbglR0PhysHeapFree(pReq);
    }
    else
        rc = ENOMEM;
    return rc;
}


/**
 * vfsops::vfs_root implementation.
 *
 * @returns 0 on success or errno.h value on failure.
 * @param   pMount      The mount data structure.
 * @param   ppVnode     Where to return the referenced root node on success.
 * @param   pContext    Unused kAuth parameter.
 */
static int vboxSfDwnVfsRoot(mount_t pMount, vnode_t *ppVnode, vfs_context_t pContext)
{
    PVBOXSFMNTDATA pThis = (PVBOXSFMNTDATA)vfs_fsprivate(pMount);
    AssertReturn(pThis, EBADMSG);
    LogFlow(("vboxSfDwnVfsRoot: pThis=%p:{%s}\n", pThis, pThis->MntInfo.szFolder));
    RT_NOREF(pContext);

    /*
     * We shouldn't be callable during unmount, should we?
     */
    AssertReturn(vfs_isunmount(pMount), EBUSY);

    /*
     * There should always be a root node around.
     */
    if (pThis->pVnRoot)
    {
        int rc = vnode_get(pThis->pVnRoot);
        if (rc == 0)
        {
            *ppVnode = pThis->pVnRoot;
            LogFlow(("vboxSfDwnVfsRoot: return %p\n", *ppVnode));
            return 0;
        }
        Log(("vboxSfDwnVfsRoot: vnode_get failed! %d\n", rc));
        return rc;
    }

    LogRel(("vboxSfDwnVfsRoot: pVnRoot is NULL!\n"));
    return EILSEQ;
}


/**
 * vfsops::vfs_umount implementation.
 *
 * @returns 0 on success or errno.h value on failure.
 * @param   pMount      The mount data.
 * @param   fFlags      Unmount flags.
 * @param   pContext    kAuth context which we don't care much about.
 *
 */
static int vboxSfDwnVfsUnmount(mount_t pMount, int fFlags, vfs_context_t pContext)
{
    PVBOXSFMNTDATA pThis = (PVBOXSFMNTDATA)vfs_fsprivate(pMount);
    AssertReturn(pThis, 0);
    LogFlowFunc(("pThis=%p:{%s} fFlags=%#x\n", pThis, pThis->MntInfo.szFolder, fFlags));
    RT_NOREF(pContext);

    /*
     * Flush vnodes.
     */
    int rc = vflush(pMount, pThis->pVnRoot, fFlags & MNT_FORCE ? FORCECLOSE : 0);
    if (rc == 0)
    {
        /*
         * Is the file system still busy?
         *
         * Until we find a way of killing any active host calls, we cannot properly
         * respect the MNT_FORCE flag here. So, MNT_FORCE is ignored here.
         */
        if (   !pThis->pVnRoot
            || !vnode_isinuse(pThis->pVnRoot, 1))
        {
            /*
             * Release our root vnode reference and do another flush.
             */
            if (pThis->pVnRoot)
            {
                vnode_put(pThis->pVnRoot);
                pThis->pVnRoot = NULL;
            }
            vflush(pMount, NULLVP, FORCECLOSE);

            /*
             * Unmap the shared folder and destroy our mount info structure.
             */
            vfs_setfsprivate(pMount, NULL);

            rc = VbglR0SfUnmapFolder(&g_SfClientDarwin, &pThis->hHostFolder);
            AssertRC(rc);

            RT_ZERO(*pThis);
            RTMemFree(pThis);

            vfs_clearflags(pMount, MNT_LOCAL); /* ?? */
            rc = 0;

            g_cVBoxSfMounts--;
        }
        else
        {
            Log(("VBoxSF: umount failed: file system busy! (%s)\n", pThis->MntInfo.szFolder));
            rc = EBUSY;
        }
    }
    return rc;
}


/**
 * vfsops::vfs_start implementation.
 */
static int vboxSfDwnVfsStart(mount_t pMount, int fFlags, vfs_context_t pContext)
{
    RT_NOREF(pMount, fFlags, pContext);
    return 0;
}


/**
 * vfsops::vfs_mount implementation.
 *
 * @returns 0 on success or errno.h value on failure.
 * @param   pMount      The mount data structure.
 * @param   pDevVp      The device to mount.  Not used by us.
 * @param   pUserData   User space address of parameters supplied to mount().
 *                      We expect a VBOXSFDRWNMOUNTINFO structure.
 * @param   pContext    kAuth context needed in order to authentificate mount
 *                      operation.
 */
static int vboxSfDwnVfsMount(mount_t pMount, vnode_t pDevVp, user_addr_t pUserData, vfs_context_t pContext)
{
    RT_NOREF(pDevVp, pContext);

    /*
     * We don't support mount updating.
     */
    if (vfs_isupdate(pMount))
    {
        LogRel(("VBoxSF: mount: MNT_UPDATE is not supported.\n"));
        return ENOTSUP;
    }
    if (pUserData == USER_ADDR_NULL)
    {
        LogRel(("VBoxSF: mount: pUserData is NULL.\n"));
        return EINVAL;
    }
    struct vfsstatfs *pFsStats = vfs_statfs(pMount);
    AssertReturn(pFsStats, EINVAL);

    /*
     * Get the mount information from userland.
     */
    PVBOXSFMNTDATA pThis = (PVBOXSFMNTDATA)RTMemAllocZ(sizeof(*pThis));
    if (!pThis)
        return ENOMEM;
    pThis->uidMounter = pFsStats->f_owner;

    int rc = RTR0MemUserCopyFrom(&pThis->MntInfo, (RTR3PTR)pUserData, sizeof(pThis->MntInfo));
    if (RT_FAILURE(rc))
    {
        LogRel(("VBoxSF: mount: Failed to copy in mount user data: %Rrc\n", rc));
        rc = EFAULT;
    }
    else if (pThis->MntInfo.u32Magic != VBOXSFDRWNMOUNTINFO_MAGIC)
    {
        LogRel(("VBoxSF: mount: Invalid user data magic (%#x)\n", pThis->MntInfo.u32Magic));
        rc = EINVAL;
    }
    else if (   (rc = RTStrValidateEncodingEx(pThis->MntInfo.szFolder, sizeof(pThis->MntInfo.szFolder),
                                              RTSTR_VALIDATE_ENCODING_ZERO_TERMINATED)) != VINF_SUCCESS
             || pThis->MntInfo.szFolder[0] == '\0')
    {
        LogRel(("VBoxSF: mount: Invalid or empty share name!\n"));
        rc = EINVAL;
    }
    else
    {
        /*
         * Try map the shared folder.
         */
        if (vboxSfDwnConnect())
        {
            PSHFLSTRING pName = ShflStringDupUtf8(pThis->MntInfo.szFolder);
            if (pName)
            {
                rc = VbglR0SfMapFolder(&g_SfClientDarwin, pName, &pThis->hHostFolder);
                RTMemFree(pName);
                if (RT_SUCCESS(rc))
                {

                    /*
                     * Create a root node, that avoid races later.
                     */
                    pThis->pVnRoot = vboxSfDwnVnAlloc(pMount, VDIR, NULL /*pParent*/, 0);
                    if (pThis->pVnRoot)
                    {
                        /*
                         * Fill file system stats with dummy data.
                         */
                        pFsStats->f_bsize  = 512;
                        pFsStats->f_iosize = _64K;
                        pFsStats->f_blocks = _1M;
                        pFsStats->f_bavail = _1M / 4 * 3;
                        pFsStats->f_bused  = _1M / 4;
                        pFsStats->f_files  = 1024;
                        pFsStats->f_ffree  = _64K;
                        vfs_getnewfsid(pMount); /* f_fsid */
                        /* pFsStats->f_fowner - don't touch */
                        /* pFsStats->f_fstypename - don't touch */
                        /* pFsStats->f_mntonname - don't touch */
                        RTStrCopy(pFsStats->f_mntfromname, sizeof(pFsStats->f_mntfromname), pThis->MntInfo.szFolder);
                        /* pFsStats->f_fssubtype - don't touch? */
                        /* pFsStats->f_reserved[0] - don't touch? */
                        /* pFsStats->f_reserved[1] - don't touch? */

                        /*
                         * We're good. Set private data and flags.
                         */
                        vfs_setfsprivate(pMount, pThis);
                        vfs_setflags(pMount, MNT_SYNCHRONOUS | MNT_NOSUID | MNT_NODEV);
                        /** @todo Consider flags like MNT_NOEXEC ? */

                        /// @todo vfs_setauthopaque(pMount)?
                        /// @todo vfs_clearauthopaqueaccess(pMount)?
                        /// @todo vfs_clearextendedsecurity(pMount)?

                        LogRel(("VBoxSF: mount: Successfully mounted '%s' (uidMounter=%u).\n",
                                pThis->MntInfo.szFolder, pThis->uidMounter));
                        return 0;
                    }

                    LogRel(("VBoxSF: mount: Failed to allocate root node!\n"));
                    rc = ENOMEM;
                }
                else
                {
                    LogRel(("VBoxSF: mount: VbglR0SfMapFolder failed on '%s': %Rrc\n", pThis->MntInfo.szFolder, rc));
                    rc = ENOENT;
                }
            }
            else
                rc = ENOMEM;
        }
        else
        {
            LogRel(("VBoxSF: mount: Not connected to shared folders service!\n"));
            rc = ENOTCONN;
        }
    }
    RTMemFree(pThis);
    return rc;
}


/**
 * VFS operations
 */
struct vfsops g_VBoxSfVfsOps =
{
    vboxSfDwnVfsMount,
    vboxSfDwnVfsStart,
    vboxSfDwnVfsUnmount,
    vboxSfDwnVfsRoot,
    NULL,               /* Skipped: vfs_quotactl */
    vboxSfDwnVfsGetAttr,
    NULL,               /* Skipped: vfs_sync */
    NULL,               /* Skipped: vfs_vget */
    NULL,               /* Skipped: vfs_fhtovp */
    NULL,               /* Skipped: vfs_vptofh */
    NULL,               /* Skipped: vfs_init */
    NULL,               /* Skipped: vfs_sysctl */
    NULL,               /* Skipped: vfs_setattr */
    /* Reserved */
    { NULL, NULL, NULL, NULL, NULL, NULL, NULL, },
};
