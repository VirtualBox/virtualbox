/* $Id: VBoxSF.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxSF - Darwin Shared Folders, KEXT entry points.
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

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <VBox/version.h>
#include <VBox/log.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static kern_return_t vboxSfDwnModuleLoad(struct kmod_info *pKModInfo, void *pvData);
static kern_return_t vboxSfDwnModuleUnload(struct kmod_info *pKModInfo, void *pvData);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The VBoxGuest service if we've managed to connect to it already. */
static IOService               *g_pVBoxGuest = NULL;
/** The shared folder service client structure. */
VBGLSFCLIENT                    g_SfClientDarwin = { UINT32_MAX, NULL };
/** Number of active mounts.  Used for unload prevention. */
uint32_t volatile               g_cVBoxSfMounts = 0;

/** VFS table entry for our file system (for vfs_fsremove). */
static vfstable_t               g_pVBoxSfVfsTableEntry;
/** For vfs_fsentry. */
static struct vnodeopv_desc    *g_apVBoxSfVnodeOpDescList[] =
{
    &g_VBoxSfVnodeOpvDesc,
};
/** VFS registration structure. */
static struct vfs_fsentry       g_VBoxSfFsEntry =
{
    .vfe_vfsops     = &g_VBoxSfVfsOps,
    .vfe_vopcnt     = RT_ELEMENTS(g_apVBoxSfVnodeOpDescList),
    .vfe_opvdescs   = g_apVBoxSfVnodeOpDescList,
    .vfe_fstypenum  = -1,
    .vfe_fsname     = VBOXSF_DARWIN_FS_NAME,
    .vfe_flags      = VFS_TBLTHREADSAFE     /* Required. */
                    | VFS_TBLFSNODELOCK     /* Required. */
                    | VFS_TBLNOTYPENUM      /* No historic file system number. */
                    | VFS_TBL64BITREADY,    /* Can handle 64-bit processes */
    /** @todo add VFS_TBLREADDIR_EXTENDED */
    .vfe_reserv     = { NULL, NULL },
};


/**
 * Declare the module stuff.
 */
RT_C_DECLS_BEGIN
extern kern_return_t _start(struct kmod_info *pKModInfo, void *pvData);
extern kern_return_t _stop(struct kmod_info *pKModInfo, void *pvData);

KMOD_EXPLICIT_DECL(VBoxSF, VBOX_VERSION_STRING, _start, _stop)
DECL_HIDDEN_DATA(kmod_start_func_t *) _realmain      = vboxSfDwnModuleLoad;
DECL_HIDDEN_DATA(kmod_stop_func_t *)  _antimain      = vboxSfDwnModuleUnload;
DECL_HIDDEN_DATA(int)                 _kext_apple_cc = __APPLE_CC__;
RT_C_DECLS_END


/**
 * Connect to VBoxGuest and host shared folders service.
 *
 * @returns true if connected, false if not.
 */
bool vboxSfDwnConnect(void)
{
    /*
     * Grab VBoxGuest - since it's a dependency of this module, it shouldn't be hard.
     */
    if (!g_pVBoxGuest)
    {
        OSDictionary *pServiceMatcher = IOService::serviceMatching("org_virtualbox_VBoxGuest");
        if (pServiceMatcher)
        {
            IOService *pVBoxGuest = IOService::waitForMatchingService(pServiceMatcher, 10 * RT_NS_1SEC);
            if (pVBoxGuest)
                g_pVBoxGuest = pVBoxGuest;
            else
                LogRel(("vboxSfDwnConnect: IOService::waitForMatchingService failed!!\n"));
        }
        else
            LogRel(("vboxSfDwnConnect: serviceMatching failed\n"));
    }

    if (g_pVBoxGuest)
    {
        /*
         * Get hold of the shared folders service if we haven't already.
         */
        if (g_SfClientDarwin.handle != NULL)
            return true;

        int rc = VbglR0SfConnect(&g_SfClientDarwin);
        if (RT_SUCCESS(rc))
        {
            rc = VbglR0SfSetUtf8(&g_SfClientDarwin);
            if (RT_SUCCESS(rc))
                return true;

            LogRel(("VBoxSF: VbglR0SfSetUtf8 failed: %Rrc\n", rc));

            VbglR0SfDisconnect(&g_SfClientDarwin);
            g_SfClientDarwin.handle = NULL;
        }
        else
            LogRel(("VBoxSF: VbglR0SfConnect failed: %Rrc\n", rc));
    }

    return false;
}


/**
 * Start the kernel module.
 */
static kern_return_t vboxSfDwnModuleLoad(struct kmod_info *pKModInfo, void *pvData)
{
    RT_NOREF(pKModInfo, pvData);
#ifdef DEBUG
    printf("vboxSfDwnModuleLoad\n");
    RTLogBackdoorPrintf("vboxSfDwnModuleLoad\n");
#endif

    /*
     * Initialize IPRT and the ring-0 guest library.
     */
    int rc = RTR0Init(0);
    if (RT_SUCCESS(rc))
    {
        rc = VbglR0SfInit();
        if (RT_SUCCESS(rc))
        {
            /*
             * Register the file system.
             */
            rc = vfs_fsadd(&g_VBoxSfFsEntry, &g_pVBoxSfVfsTableEntry);
            if (rc == 0)
            {
                /*
                 * Try find VBoxGuest and connect to the shared folders service on the host.
                 */
                /** @todo should we just ignore the error here and retry at mount time?
                 * Technically, VBoxGuest should be available since it's one of our
                 * dependencies... */
                vboxSfDwnConnect();

                /*
                 * We're done for now.  We'll deal with
                 */
                LogRel(("VBoxSF: loaded\n"));
                return KERN_SUCCESS;
            }

            printf("VBoxSF: vfs_fsadd failed: %d\n", rc);
            RTLogBackdoorPrintf("VBoxSF: vfs_fsadd failed: %d\n", rc);
            VbglR0SfTerm();
        }
        else
        {
            printf("VBoxSF: VbglR0SfInit failed: %d\n", rc);
            RTLogBackdoorPrintf("VBoxSF: VbglR0SfInit failed: %Rrc\n", rc);
        }
        RTR0Term();
    }
    else
    {
        printf("VBoxSF: RTR0Init failed: %d\n", rc);
        RTLogBackdoorPrintf("VBoxSF: RTR0Init failed: %Rrc\n", rc);
    }
    return KERN_FAILURE;
}


/**
 * Stop the kernel module.
 */
static kern_return_t vboxSfDwnModuleUnload(struct kmod_info *pKModInfo, void *pvData)
{
    RT_NOREF(pKModInfo, pvData);
#ifdef DEBUG
    printf("vboxSfDwnModuleUnload\n");
    RTLogBackdoorPrintf("vboxSfDwnModuleUnload\n");
#endif


    /*
     * Are we busy?  If so fail.  Otherwise try deregister the file system.
     */
    if (g_cVBoxSfMounts > 0)
    {
        LogRel(("VBoxSF: Refusing to unload with %u active mounts\n", g_cVBoxSfMounts));
        return KERN_NO_ACCESS;
    }

    if (g_pVBoxSfVfsTableEntry)
    {
        int rc = vfs_fsremove(g_pVBoxSfVfsTableEntry);
        if (rc != 0)
        {
            LogRel(("VBoxSF: vfs_fsremove failed: %d\n", rc));
            return KERN_NO_ACCESS;
        }
    }

    /*
     * Disconnect and terminate libraries we're using.
     */
    if (g_SfClientDarwin.handle != NULL)
    {
        VbglR0SfDisconnect(&g_SfClientDarwin);
        g_SfClientDarwin.handle = NULL;
    }

    if (g_pVBoxGuest)
    {
        g_pVBoxGuest->release();
        g_pVBoxGuest = NULL;
    }

    VbglR0SfTerm();
    RTR0Term();
    return KERN_SUCCESS;
}

