/* $Id: VBoxSFInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxSF - Darwin Shared Folders, internal header.
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

#ifndef GA_INCLUDED_SRC_darwin_VBoxSF_VBoxSFInternal_h
#define GA_INCLUDED_SRC_darwin_VBoxSF_VBoxSFInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "VBoxSFMount.h"

#include <libkern/libkern.h>
#include <iprt/types.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <mach/mach_port.h>
#include <mach/kmod.h>
#include <mach/mach_types.h>
#include <sys/errno.h>
#include <sys/dirent.h>
#include <sys/lock.h>
#include <sys/fcntl.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/vnode.h>
#include <vfs/vfs_support.h>
#undef PVM

#include <iprt/mem.h>
#include <VBox/VBoxGuest.h>
#include <VBox/VBoxGuestLibSharedFolders.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Private data we associate with a mount.
 */
typedef struct VBOXSFMNTDATA
{
    /** The shared folder mapping */
    VBGLSFMAP           hHostFolder;
    /** The root VNode. */
    vnode_t             pVnRoot;
    /** User that mounted shared folder (anyone but root?). */
    uid_t               uidMounter;
    /** The mount info from the mount() call. */
    VBOXSFDRWNMOUNTINFO MntInfo;
} VBOXSFMNTDATA;
/** Pointer to private mount data.  */
typedef VBOXSFMNTDATA *PVBOXSFMNTDATA;

/**
 * Private data we associate with a VNode.
 */
typedef struct VBOXSFDWNVNDATA
{
    /** The handle to the host object.  */
    SHFLHANDLE      hHandle;
    ///PSHFLSTRING     pPath;                  /** Path within shared folder */
    ///lck_attr_t     *pLockAttr;              /** BSD locking stuff */
    ///lck_rw_t       *pLock;                  /** BSD locking stuff */
} VBOXSFDWNVNDATA;
/** Pointer to private vnode data. */
typedef VBOXSFDWNVNDATA *PVBOXSFDWNVNDATA;



/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
extern VBGLSFCLIENT         g_SfClientDarwin;
extern uint32_t volatile    g_cVBoxSfMounts;
extern struct vfsops        g_VBoxSfVfsOps;
extern struct vnodeopv_desc g_VBoxSfVnodeOpvDesc;
extern int (**g_papfnVBoxSfDwnVnDirOpsVector)(void *);



/*********************************************************************************************************************************
*   Functions                                                                                                                    *
*********************************************************************************************************************************/
bool    vboxSfDwnConnect(void);
vnode_t vboxSfDwnVnAlloc(mount_t pMount, enum vtype enmType, vnode_t pParent, uint64_t cbFile);


#endif /* !GA_INCLUDED_SRC_darwin_VBoxSF_VBoxSFInternal_h */

