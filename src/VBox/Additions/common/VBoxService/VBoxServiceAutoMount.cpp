/* $Id: VBoxServiceAutoMount.cpp 108985 2025-03-28 08:58:13Z andreas.loeffler@oracle.com $ */
/** @file
 * VBoxService - Auto-mounting for Shared Folders, only Linux & Solaris atm.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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


/** @page pg_vgsvc_automount VBoxService - Shared Folder Automounter
 *
 * The Shared Folder Automounter subservice mounts shared folders upon request
 * from the host.
 *
 * This retrieves shared folder automount requests from Main via the VMMDev.
 * The current implemention only does this once, for some inexplicable reason,
 * so the run-time addition of automounted shared folders are not heeded.
 *
 * This subservice is only used on linux and solaris.  On Windows the current
 * thinking is this is better of done from VBoxTray, some one argue that for
 * drive letter assigned shared folders it would be better to do some magic here
 * (obviously not involving NDAddConnection).
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/semaphore.h>
#include <iprt/sort.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <VBox/err.h>
#include <VBox/VBoxGuestLib.h>
#include <VBox/shflsvc.h>
#include "VBoxServiceInternal.h"
#include "VBoxServiceUtils.h"

#ifdef RT_OS_WINDOWS
#elif defined(RT_OS_OS2)
# define INCL_DOSFILEMGR
# define INCL_ERRORS
# define OS2EMX_PLAIN_CHAR
# include <os2emx.h>
#else
# include <alloca.h>
# include <errno.h>
# include <grp.h>
# include <sys/mount.h>
# ifdef RT_OS_SOLARIS
#  include <sys/mntent.h>
#  include <sys/mnttab.h>
#  include <sys/vfs.h>
# elif defined(RT_OS_LINUX)
#  include <mntent.h>
#  include <paths.h>
#  include <sys/utsname.h>
RT_C_DECLS_BEGIN
#  include "../../linux/sharedfolders/vbsfmount.h"
RT_C_DECLS_END
# else
#  error "Port me!"
# endif
# include <unistd.h>
#endif



/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def VBOXSERVICE_AUTOMOUNT_DEFAULT_DIR
 * Default mount directory (unix only).
 */
#ifndef VBOXSERVICE_AUTOMOUNT_DEFAULT_DIR
# define VBOXSERVICE_AUTOMOUNT_DEFAULT_DIR      "/media"
#endif

/** @def VBOXSERVICE_AUTOMOUNT_DEFAULT_PREFIX
 * Default mount prefix (unix only).
 */
#ifndef VBOXSERVICE_AUTOMOUNT_DEFAULT_PREFIX
# define VBOXSERVICE_AUTOMOUNT_DEFAULT_PREFIX   "sf_"
#endif

#ifndef _PATH_MOUNTED
# ifdef RT_OS_SOLARIS
#  define _PATH_MOUNTED                          "/etc/mnttab"
# else
#  define _PATH_MOUNTED                          "/etc/mtab"
# endif
#endif

/** @def VBOXSERVICE_AUTOMOUNT_MIQF
 * The drive letter / path mount point flag.  */
#if defined(RT_OS_WINDOWS) || defined(RT_OS_OS2)
# define VBOXSERVICE_AUTOMOUNT_MIQF             SHFL_MIQF_DRIVE_LETTER
#else
# define VBOXSERVICE_AUTOMOUNT_MIQF             SHFL_MIQF_PATH
#endif


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Automounter mount table entry.
 *
 * This holds the information returned by SHFL_FN_QUERY_MAP_INFO and
 * additional mount state info.  We only keep entries for mounted mappings.
 */
typedef struct VBSVCAUTOMOUNTERENTRY
{
    /** The root ID. */
    uint32_t     idRoot;
    /** The root ID version. */
    uint32_t     uRootIdVersion;
    /** Map info flags, SHFL_MIF_XXX. */
    uint64_t     fFlags;
    /** The shared folder (mapping) name. */
    char        *pszName;
    /** The configured mount point, NULL if none. */
    char        *pszMountPoint;
    /** The actual mount point, NULL if not mount.  */
    char        *pszActualMountPoint;
} VBSVCAUTOMOUNTERENTRY;
/** Pointer to an automounter entry.   */
typedef VBSVCAUTOMOUNTERENTRY *PVBSVCAUTOMOUNTERENTRY;

/** Automounter mount table. */
typedef struct VBSVCAUTOMOUNTERTABLE
{
    /** Current number of entries in the array. */
    uint32_t                cEntries;
    /** Max number of entries the array can hold w/o growing it. */
    uint32_t                cAllocated;
    /** Pointer to an array of entry pointers. */
    PVBSVCAUTOMOUNTERENTRY   *papEntries;
} VBSVCAUTOMOUNTERTABLE;
/** Pointer to an automounter mount table.   */
typedef  VBSVCAUTOMOUNTERTABLE *PVBSVCAUTOMOUNTERTABLE;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The semaphore we're blocking on. */
static RTSEMEVENTMULTI  g_hAutoMountEvent = NIL_RTSEMEVENTMULTI;
/** The Shared Folders service client ID. */
static uint32_t         g_idClientSharedFolders = 0;
/** Set if we can wait on changes to the mappings. */
static bool             g_fHostSupportsWaitAndInfoQuery = false;

#ifdef RT_OS_OS2
/** The attachment tag we use to identify attchments that belongs to us. */
static char const       g_szTag[] = "VBoxAutomounter";
#elif defined(RT_OS_LINUX)
/** Tag option value that lets us identify mounts that belongs to us. */
static char const       g_szTag[] = "VBoxAutomounter";
#elif defined(RT_OS_SOLARIS)
/** Dummy mount option that lets us identify mounts that belongs to us. */
static char const       g_szTag[] = "VBoxAutomounter";
#endif



/**
 * @interface_method_impl{VBOXSERVICE,pfnInit}
 */
static DECLCALLBACK(int) vbsvcAutomounterInit(void)
{
    VGSvcVerbose(3, "vbsvcAutomounterInit\n");

    int rc = RTSemEventMultiCreate(&g_hAutoMountEvent);
    AssertRCReturn(rc, rc);

    rc = VbglR3SharedFolderConnect(&g_idClientSharedFolders);
    if (RT_SUCCESS(rc))
    {
        VGSvcVerbose(3, "vbsvcAutomounterInit: Service Client ID: %#x\n", g_idClientSharedFolders);
        g_fHostSupportsWaitAndInfoQuery = RT_SUCCESS(VbglR3SharedFolderCancelMappingsChangesWaits(g_idClientSharedFolders));
    }
    else
    {
        /* If the service was not found, we disable this service without
           causing VBoxService to fail. */
        if (   rc == VERR_HGCM_SERVICE_NOT_FOUND                         /* Host service is not available. */
#ifdef RT_OS_WINDOWS
            || RTSystemGetNtVersion() <= RTSYSTEM_MAKE_NT_VERSION(4,0,0) /* On <= NT4 guests no Shared Folders are available. */
#endif
           )
        {
            VGSvcVerbose(0, "vbsvcAutomounterInit: Shared Folders service is not available\n");
            rc = VERR_SERVICE_DISABLED;
        }
        else
            VGSvcError("Control: Failed to connect to the Shared Folders service! Error: %Rrc\n", rc);
        RTSemEventMultiDestroy(g_hAutoMountEvent);
        g_hAutoMountEvent = NIL_RTSEMEVENTMULTI;
    }

    return rc;
}


#if defined(RT_OS_SOLARIS) || defined(RT_OS_LINUX) /* The old code: */

/**
 * @todo Integrate into RTFsQueryMountpoint()?
 */
static bool vbsvcAutoMountShareIsMountedOld(const char *pszShare, char *pszMountPoint, size_t cbMountPoint)
{
    AssertPtrReturn(pszShare, false);
    AssertPtrReturn(pszMountPoint, false);
    AssertReturn(cbMountPoint, false);

    bool fMounted = false;

# if defined(RT_OS_SOLARIS)
    /** @todo What to do if we have a relative path in mtab instead
     *       of an absolute one ("temp" vs. "/media/temp")?
     * procfs contains the full path but not the actual share name ...
     * FILE *pFh = setmntent("/proc/mounts", "r+t"); */
    FILE *pFh = fopen(_PATH_MOUNTED, "r");
    if (!pFh)
        VGSvcError("vbsvcAutoMountShareIsMountedOld: Could not open mount tab '%s'!\n", _PATH_MOUNTED);
    else
    {
        mnttab mntTab;
        while ((getmntent(pFh, &mntTab)))
        {
            if (!RTStrICmp(mntTab.mnt_special, pszShare))
            {
                fMounted = RTStrPrintf(pszMountPoint, cbMountPoint, "%s", mntTab.mnt_mountp)
                         ? true : false;
                break;
            }
        }
        fclose(pFh);
    }
# elif defined(RT_OS_LINUX)
    FILE *pFh = setmntent(_PATH_MOUNTED, "r+t"); /** @todo r=bird: why open it for writing? (the '+') */
    if (pFh == NULL)
        VGSvcError("vbsvcAutoMountShareIsMountedOld: Could not open mount tab '%s'!\n", _PATH_MOUNTED);
    else
    {
        mntent *pMntEnt;
        while ((pMntEnt = getmntent(pFh)))
        {
            if (!RTStrICmp(pMntEnt->mnt_fsname, pszShare))
            {
                fMounted = RTStrPrintf(pszMountPoint, cbMountPoint, "%s", pMntEnt->mnt_dir)
                         ? true : false;
                break;
            }
        }
        endmntent(pFh);
    }
# else
#  error "PORTME!"
# endif

    VGSvcVerbose(4, "vbsvcAutoMountShareIsMountedOld: Share '%s' at mount point '%s' = %s\n",
                       pszShare, fMounted ? pszMountPoint : "<None>", fMounted ? "Yes" : "No");
    return fMounted;
}


/**
 * Unmounts a shared folder.
 *
 * @returns VBox status code
 * @param   pszMountPoint   The shared folder mount point.
 */
static int vbsvcAutoMountUnmountOld(const char *pszMountPoint)
{
    AssertPtrReturn(pszMountPoint, VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;
    uint8_t uTries = 0;
    int r;
    while (uTries++ < 3)
    {
        r = umount(pszMountPoint);
        if (r == 0)
            break;
/** @todo r=bird: Why do sleep 5 seconds after the final retry?
 *  May also be a good idea to check for EINVAL or other signs that someone
 *  else have already unmounted the share. */
        RTThreadSleep(5000); /* Wait a while ... */
    }
    if (r == -1)  /** @todo r=bird: RTThreadSleep set errno.  */
        rc = RTErrConvertFromErrno(errno);
    return rc;
}


/**
 * Prepares a mount point (create it, set group and mode).
 *
 * @returns VBox status code
 * @param   pszMountPoint   The mount point.
 * @param   pszShareName    Unused.
 * @param   gidGroup        The group ID.
 */
static int vbsvcAutoMountPrepareMountPointOld(const char *pszMountPoint, const char *pszShareName, RTGID gidGroup)
{
    AssertPtrReturn(pszMountPoint, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszShareName, VERR_INVALID_PARAMETER);

    /** @todo r=bird: There is no reason why gidGroup should have write access?
     *        Seriously, what kind of non-sense is this? */

    RTFMODE fMode = RTFS_UNIX_IRWXU | RTFS_UNIX_IRWXG; /* Owner (=root) and the group (=vboxsf) have full access. */
    int rc = RTDirCreateFullPath(pszMountPoint, fMode);
    if (RT_SUCCESS(rc))
    {
        rc = RTPathSetOwnerEx(pszMountPoint, NIL_RTUID /* Owner, unchanged */, gidGroup, RTPATH_F_ON_LINK);
        if (RT_SUCCESS(rc))
        {
            rc = RTPathSetMode(pszMountPoint, fMode);
            if (RT_FAILURE(rc))
            {
                if (rc == VERR_WRITE_PROTECT)
                {
                    VGSvcVerbose(3, "vbsvcAutoMountPrepareMountPointOld: Mount directory '%s' already is used/mounted\n",
                                 pszMountPoint);
                    rc = VINF_SUCCESS;
                }
                else
                    VGSvcError("vbsvcAutoMountPrepareMountPointOld: Could not set mode %RTfmode for mount directory '%s', rc = %Rrc\n",
                               fMode, pszMountPoint, rc);
            }
        }
        else
            VGSvcError("vbsvcAutoMountPrepareMountPointOld: Could not set permissions for mount directory '%s', rc = %Rrc\n",
                       pszMountPoint, rc);
    }
    else
        VGSvcError("vbsvcAutoMountPrepareMountPointOld: Could not create mount directory '%s' with mode %RTfmode, rc = %Rrc\n",
                   pszMountPoint, fMode, rc);
    return rc;
}


/**
 * Mounts a shared folder.
 *
 * @returns VBox status code reflecting unmount and mount point preparation
 *          results, but not actual mounting
 *
 * @param   pszShareName    The shared folder name.
 * @param   pszMountPoint   The mount point.
 */
static int vbsvcAutoMountSharedFolderOld(const char *pszShareName, const char *pszMountPoint)
{
    /*
     * Linux and solaris share the same mount structure.
     */
    struct group *grp_vboxsf = getgrnam("vboxsf");
    if (!grp_vboxsf)
    {
        VGSvcError("vbsvcAutoMountWorker: Group 'vboxsf' does not exist\n");
        return VINF_SUCCESS;
    }

    int rc = vbsvcAutoMountPrepareMountPointOld(pszMountPoint, pszShareName, grp_vboxsf->gr_gid);
    if (RT_SUCCESS(rc))
    {
# ifdef RT_OS_SOLARIS
        int const fFlags = MS_OPTIONSTR;
        char szOptBuf[MAX_MNTOPT_STR] = { '\0', };
        RTStrPrintf(szOptBuf, sizeof(szOptBuf), "uid=0,gid=%d,dmode=0770,fmode=0770,dmask=0000,fmask=0000", grp_vboxsf->gr_gid);
        int r = mount(pszShareName,
                      pszMountPoint,
                      fFlags,
                      "vboxfs",
                      NULL,                     /* char *dataptr */
                      0,                        /* int datalen */
                      szOptBuf,
                      sizeof(szOptBuf));
        if (r == 0)
            VGSvcVerbose(0, "vbsvcAutoMountWorker: Shared folder '%s' was mounted to '%s'\n", pszShareName, pszMountPoint);
        else if (errno != EBUSY) /* Share is already mounted? Then skip error msg. */
            VGSvcError("vbsvcAutoMountWorker: Could not mount shared folder '%s' to '%s', error = %s\n",
                       pszShareName, pszMountPoint, strerror(errno));

# else /* RT_OS_LINUX */
        uint32_t cbOpts = RTSystemGetPageSize();
        char *pszOpts = (char *)alloca(cbOpts);
        RT_BZERO(pszOpts, cbOpts);
        struct utsname uts;
        AssertStmt(uname(&uts) != -1, strcpy(uts.release, "4.4.0"));

        unsigned long const fFlags = MS_NODEV;
        ssize_t cchOpts = RTStrPrintf2(pszOpts, cbOpts, "uid=0,gid=%d,dmode=0770,fmode=0770,dmask=0000,fmask=0000",
                                       grp_vboxsf->gr_gid);
        if (cchOpts > 0 && RTStrVersionCompare(uts.release, "2.6.0") < 0)
            cchOpts = RTStrPrintf2(&pszOpts[cchOpts], cbOpts - cchOpts, ",sf_name=%s", pszShareName);
        if (cchOpts <= 0)
        {
            VGSvcError("vbsvcAutomounterMountIt: pszOpts overflow! %zd (share %s)\n", cchOpts, pszShareName);
            return VERR_BUFFER_OVERFLOW;
        }

        int r = mount(pszShareName,
                      pszMountPoint,
                      "vboxsf",
                      fFlags,
                      pszOpts);
        if (r == 0)
        {
            VGSvcVerbose(0, "vbsvcAutoMountWorker: Shared folder '%s' was mounted to '%s'\n", pszShareName, pszMountPoint);

            r = vbsfmount_complete(pszShareName, pszMountPoint, fFlags, pszOpts);
            switch (r)
            {
                case 0: /* Success. */
                    errno = 0; /* Clear all errors/warnings. */
                    break;
                case 1:
                    VGSvcError("vbsvcAutoMountWorker: Could not update mount table (malloc failure)\n");
                    break;
                case 2:
                    VGSvcError("vbsvcAutoMountWorker: Could not open mount table for update: %s\n", strerror(errno));
                    break;
                case 3:
                    /* VGSvcError("vbsvcAutoMountWorker: Could not add an entry to the mount table: %s\n", strerror(errno)); */
                    errno = 0;
                    break;
                default:
                    VGSvcError("vbsvcAutoMountWorker: Unknown error while completing mount operation: %d\n", r);
                    break;
            }
        }
        else /* r == -1, we got some error in errno.  */
        {
            switch (errno)
            {
                /* If we get EINVAL here, the system already has mounted the Shared Folder to another
                 * mount point. */
                case EINVAL:
                    VGSvcVerbose(0, "vbsvcAutoMountWorker: Shared folder '%s' is already mounted!\n", pszShareName);
                    /* Ignore this error! */
                    break;
                case EBUSY:
                    /* Ignore these errors! */
                    break;
                default:
                    VGSvcError("vbsvcAutoMountWorker: Could not mount shared folder '%s' to '%s': %s (%d)\n",
                               pszShareName, pszMountPoint, strerror(errno), errno);
                    rc = RTErrConvertFromErrno(errno);
                    break;
            }
        }
# endif
    }
    VGSvcVerbose(3, "vbsvcAutoMountWorker: Mounting returned with rc=%Rrc\n", rc);
    return rc;
}


/**
 * Processes shared folder mappings retrieved from the host.
 *
 * @returns VBox status code.
 * @param   paMappings      The mappings.
 * @param   cMappings       The number of mappings.
 * @param   pszMountDir     The mount directory.
 * @param   pszSharePrefix  The share prefix.
 * @param   uClientID       The shared folder service (HGCM) client ID.
 */
static int vbsvcAutoMountProcessMappingsOld(PCVBGLR3SHAREDFOLDERMAPPING paMappings, uint32_t cMappings,
                                            const char *pszMountDir, const char *pszSharePrefix, uint32_t uClientID)
{
    if (cMappings == 0)
        return VINF_SUCCESS;
    AssertPtrReturn(paMappings, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszMountDir, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pszSharePrefix, VERR_INVALID_PARAMETER);
    AssertReturn(uClientID > 0, VERR_INVALID_PARAMETER);

    /** @todo r=bird: Why is this loop schitzoid about status codes? It quits if
     * RTPathJoin fails (i.e. if the user specifies a very long name), but happily
     * continues if RTStrAPrintf failes (mem alloc).
     *
     * It also happily continues if the 'vboxsf' group is missing, which is a waste
     * of effort... In fact, retrieving the group ID could probably be done up
     * front, outside the loop. */
    int rc = VINF_SUCCESS;
    for (uint32_t i = 0; i < cMappings && RT_SUCCESS(rc); i++)
    {
        char *pszShareName = NULL;
        rc = VbglR3SharedFolderGetName(uClientID, paMappings[i].u32Root, &pszShareName);
        if (   RT_SUCCESS(rc)
            && *pszShareName)
        {
            VGSvcVerbose(3, "vbsvcAutoMountWorker: Connecting share %u (%s) ...\n", i+1, pszShareName);

            /** @todo r=bird: why do you copy things twice here and waste heap space?
             * szMountPoint has a fixed size.
             * @code
             * char szMountPoint[RTPATH_MAX];
             * rc = RTPathJoin(szMountPoint, sizeof(szMountPoint), pszMountDir, *pszSharePrefix ? pszSharePrefix : pszShareName);
             * if (RT_SUCCESS(rc) && *pszSharePrefix)
             *     rc = RTStrCat(szMountPoint, sizeof(szMountPoint), pszShareName);
             * @endcode */
            char *pszShareNameFull = NULL;
            if (RTStrAPrintf(&pszShareNameFull, "%s%s", pszSharePrefix, pszShareName) > 0)
            {
                char szMountPoint[RTPATH_MAX];
                rc = RTPathJoin(szMountPoint, sizeof(szMountPoint), pszMountDir, pszShareNameFull);
                if (RT_SUCCESS(rc))
                {
                    VGSvcVerbose(4, "vbsvcAutoMountWorker: Processing mount point '%s'\n", szMountPoint);

                    /*
                     * Already mounted?
                     */
                    /** @todo r-bird: this does not take into account that a shared folder could
                     *        be mounted twice... We're really just interested in whether the
                     *        folder is mounted on 'szMountPoint', no where else... */
                    bool fSkip = false;
                    char szAlreadyMountedOn[RTPATH_MAX];
                    if (vbsvcAutoMountShareIsMountedOld(pszShareName, szAlreadyMountedOn, sizeof(szAlreadyMountedOn)))
                    {
                        /* Do if it not mounted to our desired mount point */
                        if (RTStrICmp(szMountPoint, szAlreadyMountedOn))
                        {
                            VGSvcVerbose(3, "vbsvcAutoMountWorker: Shared folder '%s' already mounted on '%s', unmounting ...\n",
                                         pszShareName, szAlreadyMountedOn);
                            rc = vbsvcAutoMountUnmountOld(szAlreadyMountedOn);
                            if (RT_SUCCESS(rc))
                                fSkip = false;
                            else
                                VGSvcError("vbsvcAutoMountWorker: Failed to unmount '%s', %s (%d)! (rc=%Rrc)\n",
                                           szAlreadyMountedOn, strerror(errno), errno, rc); /** @todo errno isn't reliable at this point */
                        }
                        if (fSkip)
                            VGSvcVerbose(3, "vbsvcAutoMountWorker: Shared folder '%s' already mounted on '%s', skipping\n",
                                         pszShareName, szAlreadyMountedOn);
                    }
                    if (!fSkip)
                    {
                        /*
                         * Mount it.
                         */
                        rc = vbsvcAutoMountSharedFolderOld(pszShareName, szMountPoint);
                    }
                }
                else
                    VGSvcError("vbsvcAutoMountWorker: Unable to join mount point/prefix/shrae, rc = %Rrc\n", rc);
                RTStrFree(pszShareNameFull);
            }
            else
                VGSvcError("vbsvcAutoMountWorker: Unable to allocate full share name\n");
            RTStrFree(pszShareName);
        }
        else
            VGSvcError("vbsvcAutoMountWorker: Error while getting the shared folder name for root node = %u, rc = %Rrc\n",
                             paMappings[i].u32Root, rc);
    } /* for cMappings. */
    return rc;
}

#endif /* defined(RT_OS_SOLARIS) || defined(RT_OS_LINUX) - the old code*/


/**
 * Service worker function for old host.
 *
 * This only mount stuff on startup.
 *
 * @returns VBox status code.
 * @param   pfShutdown          Shutdown indicator.
 */
static int vbsvcAutoMountWorkerOld(bool volatile *pfShutdown)
{
#if defined(RT_OS_SOLARIS) || defined(RT_OS_LINUX)
    /*
     * We only do a single pass here.
     */
    uint32_t cMappings;
    PVBGLR3SHAREDFOLDERMAPPING paMappings;
    int rc = VbglR3SharedFolderGetMappings(g_idClientSharedFolders, true /* Only process auto-mounted folders */,
                                           &paMappings, &cMappings);
    if (   RT_SUCCESS(rc)
        && cMappings)
    {
        char *pszMountDir;
        rc = VbglR3SharedFolderGetMountDir(&pszMountDir);
        if (rc == VERR_NOT_FOUND)
            rc = RTStrDupEx(&pszMountDir, VBOXSERVICE_AUTOMOUNT_DEFAULT_DIR);
        if (RT_SUCCESS(rc))
        {
            VGSvcVerbose(3, "vbsvcAutoMountWorker: Shared folder mount dir set to '%s'\n", pszMountDir);

            char *pszSharePrefix;
            rc = VbglR3SharedFolderGetMountPrefix(&pszSharePrefix);
            if (RT_SUCCESS(rc))
            {
                VGSvcVerbose(3, "vbsvcAutoMountWorker: Shared folder mount prefix set to '%s'\n", pszSharePrefix);
# ifdef USE_VIRTUAL_SHARES
                /* Check for a fixed/virtual auto-mount share. */
                if (VbglR3SharedFolderExists(g_idClientSharedFolders, "vbsfAutoMount"))
                    VGSvcVerbose(3, "vbsvcAutoMountWorker: Host supports auto-mount root\n");
                else
                {
# endif
                    VGSvcVerbose(3, "vbsvcAutoMountWorker: Got %u shared folder mappings\n", cMappings);
                    rc = vbsvcAutoMountProcessMappingsOld(paMappings, cMappings, pszMountDir, pszSharePrefix,
                                                          g_idClientSharedFolders);
# ifdef USE_VIRTUAL_SHARES
                }
# endif
                RTStrFree(pszSharePrefix);
            } /* Mount share prefix. */
            else
                VGSvcError("vbsvcAutoMountWorker: Error while getting the shared folder mount prefix, rc = %Rrc\n", rc);
            RTStrFree(pszMountDir);
        }
        else
            VGSvcError("vbsvcAutoMountWorker: Error while getting the shared folder directory, rc = %Rrc\n", rc);
        VbglR3SharedFolderFreeMappings(paMappings);
    }
    else if (RT_FAILURE(rc))
        VGSvcError("vbsvcAutoMountWorker: Error while getting the shared folder mappings, rc = %Rrc\n", rc);
    else
        VGSvcVerbose(3, "vbsvcAutoMountWorker: No shared folder mappings found\n");

#else
    int rc = VINF_SUCCESS;
#endif /* defined(RT_OS_SOLARIS) || defined(RT_OS_LINUX) */


    /*
     * Wait on shutdown (this used to be a silly RTThreadSleep(500) loop).
     */
    while (!*pfShutdown)
    {
        rc = RTSemEventMultiWait(g_hAutoMountEvent, RT_MS_1MIN);
        if (rc != VERR_TIMEOUT)
            break;
    }

    VGSvcVerbose(3, "vbsvcAutoMountWorkerOld: Finished with rc=%Rrc\n", rc);
    return rc;
}

#if !defined(RT_OS_WINDOWS) && !defined(RT_OS_OS2)
/**
 * Assembles the mount directory and prefix into @a pszDst.
 *
 * Will fall back on defaults if we have trouble with the configuration from the
 * host.  This ASSUMES that @a cbDst is rather large and won't cause trouble
 * with the default.
 *
 * @returns IPRT status code.
 * @param   pszDst          Where to return the prefix.
 * @param   cbDst           The size of the prefix buffer.
 */
static int vbsvcAutomounterQueryMountDirAndPrefix(char *pszDst, size_t cbDst)
{
    /*
     * Query the config first.
     */
    /* Mount directory: */
    const char *pszDir = VBOXSERVICE_AUTOMOUNT_DEFAULT_DIR;
    char       *pszCfgDir;
    int rc = VbglR3SharedFolderGetMountDir(&pszCfgDir);
    if (RT_SUCCESS(rc))
    {
        if (*pszCfgDir == '/')
            pszDir = pszCfgDir;
    }
    else
        pszCfgDir = NULL;

    /* Prefix: */
    const char *pszPrefix = VBOXSERVICE_AUTOMOUNT_DEFAULT_PREFIX;
    char *pszCfgPrefix;
    rc = VbglR3SharedFolderGetMountPrefix(&pszCfgPrefix);
    if (RT_SUCCESS(rc))
    {
        if (   strchr(pszCfgPrefix, '/')  == NULL
            && strchr(pszCfgPrefix, '\\') == NULL
            && strcmp(pszCfgPrefix, "..") != 0)
            pszPrefix = pszCfgPrefix;
    }
    else
        pszCfgPrefix = NULL;

    /*
     * Try combine the two.
     */
    rc = RTPathAbs(pszDir, pszDst, cbDst);
    if (RT_SUCCESS(rc))
    {
        if (*pszPrefix)
        {
            rc = RTPathAppend(pszDst, cbDst, pszPrefix);
            if (RT_FAILURE(rc))
                VGSvcError("vbsvcAutomounterQueryMountDirAndPrefix: RTPathAppend(%s,,%s) -> %Rrc\n", pszDst, pszPrefix, rc);
        }
        else
        {
            rc = RTPathEnsureTrailingSeparator(pszDst, cbDst);
            if (RT_FAILURE(rc))
                VGSvcError("vbsvcAutomounterQueryMountDirAndPrefix: RTPathEnsureTrailingSeparator(%s) -> %Rrc\n", pszDst, rc);
        }
    }
    else
        VGSvcError("vbsvcAutomounterQueryMountDirAndPrefix: RTPathAbs(%s) -> %Rrc\n", pszDir, rc);


    /*
     * Return the default dir + prefix if the above failed.
     */
    if (RT_FAILURE(rc))
    {
        rc = RTStrCopy(pszDst, cbDst, VBOXSERVICE_AUTOMOUNT_DEFAULT_DIR "/" VBOXSERVICE_AUTOMOUNT_DEFAULT_PREFIX);
        AssertRC(rc);
    }

    RTStrFree(pszCfgDir);
    RTStrFree(pszCfgPrefix);
    return rc;
}
#endif /* !RT_OS_WINDOW && !RT_OS_OS2 */


/**
 * @callback_method_impl{FNRTSORTCMP, For sorting mount table by root ID. }
 */
static DECLCALLBACK(int) vbsvcAutomounterCompareEntry(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF_PV(pvUser);
    PVBSVCAUTOMOUNTERENTRY pEntry1 = (PVBSVCAUTOMOUNTERENTRY)pvElement1;
    PVBSVCAUTOMOUNTERENTRY pEntry2 = (PVBSVCAUTOMOUNTERENTRY)pvElement2;
    return pEntry1->idRoot < pEntry2->idRoot ? -1
         : pEntry1->idRoot > pEntry2->idRoot ? 1 : 0;
}


/**
 * Worker for vbsvcAutomounterPopulateTable for adding discovered entries.
 *
 * This is puts dummies in for missing values, depending on
 * vbsvcAutomounterPopulateTable to query them later.
 *
 * @returns VINF_SUCCESS or VERR_NO_MEMORY;
 * @param   pMountTable     The mount table to add an entry to.
 * @param   pszName         The shared folder name.
 * @param   pszMountPoint   The mount point.
 */
static int vbsvcAutomounterAddEntry(PVBSVCAUTOMOUNTERTABLE pMountTable, const char *pszName, const char *pszMountPoint)
{
    VGSvcVerbose(2, "vbsvcAutomounterAddEntry: %s -> %s\n", pszMountPoint, pszName);
    PVBSVCAUTOMOUNTERENTRY pEntry = (PVBSVCAUTOMOUNTERENTRY)RTMemAlloc(sizeof(*pEntry));
    pEntry->idRoot              = UINT32_MAX;
    pEntry->uRootIdVersion      = UINT32_MAX;
    pEntry->fFlags              = UINT64_MAX;
    pEntry->pszName             = RTStrDup(pszName);
    pEntry->pszMountPoint       = NULL;
    pEntry->pszActualMountPoint = RTStrDup(pszMountPoint);
    if (pEntry->pszName && pEntry->pszActualMountPoint)
    {
        if (pMountTable->cEntries + 1 <= pMountTable->cAllocated)
        {
            pMountTable->papEntries[pMountTable->cEntries++] = pEntry;
            return VINF_SUCCESS;
        }

        void *pvNew = RTMemRealloc(pMountTable->papEntries, (pMountTable->cAllocated + 8) * sizeof(pMountTable->papEntries[0]));
        if (pvNew)
        {
            pMountTable->cAllocated += 8;
            pMountTable->papEntries = (PVBSVCAUTOMOUNTERENTRY *)pvNew;

            pMountTable->papEntries[pMountTable->cEntries++] = pEntry;
            return VINF_SUCCESS;
        }
    }
    RTMemFree(pEntry->pszActualMountPoint);
    RTMemFree(pEntry->pszName);
    RTMemFree(pEntry);
    return VERR_NO_MEMORY;
}


/**
 * Populates the mount table as best we can with existing automount entries.
 *
 * @returns VINF_SUCCESS or VERR_NO_MEMORY;
 * @param   pMountTable     The mount table (empty).
 */
static int vbsvcAutomounterPopulateTable(PVBSVCAUTOMOUNTERTABLE pMountTable)
{
    int rc;

#ifdef RT_OS_WINDOWS
    /*
     * Loop thru the drive letters and check out each of them using QueryDosDeviceW.
     */
    static const char s_szDevicePath[] = "\\Device\\VBoxMiniRdr\\;";
    for (char chDrive = 'Z'; chDrive >= 'A'; chDrive--)
    {
        RTUTF16 const wszMountPoint[4] = { (RTUTF16)chDrive, ':', '\0', '\0' };
        RTUTF16       wszTargetPath[RTPATH_MAX];
        DWORD const   cwcResult = QueryDosDeviceW(wszMountPoint, wszTargetPath, RT_ELEMENTS(wszTargetPath));
        if (   cwcResult > sizeof(s_szDevicePath)
            && RTUtf16NICmpAscii(wszTargetPath, RT_STR_TUPLE(s_szDevicePath)) == 0)
        {
            PCRTUTF16 pwsz = &wszTargetPath[RT_ELEMENTS(s_szDevicePath) - 1];
            Assert(pwsz[-1] == ';');
            if (   (pwsz[0] & ~(RTUTF16)0x20) == chDrive
                && pwsz[1] == ':'
                && pwsz[2] == '\\')
            {
                /* For now we'll just use the special capitalization of the
                   "server" name to identify it as our work.  We could check
                   if the symlink is from \Global?? or \??, but that trick does
                   work for older OS versions (<= XP) or when running the
                   service manually for testing/wathever purposes. */
                /** @todo Modify the windows shared folder driver to allow tagging drives.*/
                if (RTUtf16NCmpAscii(&pwsz[3], RT_STR_TUPLE("VBoxSvr\\")) == 0)
                {
                    pwsz += 3 + 8;
                    if (*pwsz != '\\' && *pwsz)
                    {
                        /* The shared folder name should follow immediately after the server prefix. */
                        char *pszMountedName = NULL;
                        rc = RTUtf16ToUtf8(pwsz, &pszMountedName);
                        if (RT_SUCCESS(rc))
                        {
                            char const szMountPoint[4] = { chDrive, ':', '\0', '\0' };
                            rc = vbsvcAutomounterAddEntry(pMountTable, pszMountedName, szMountPoint);
                            RTStrFree(pszMountedName);
                        }
                        if (RT_FAILURE(rc))
                            return rc;
                    }
                    else
                        VGSvcVerbose(2, "vbsvcAutomounterPopulateTable: Malformed, not ours: %ls -> %ls\n",
                                     wszMountPoint, wszTargetPath);
                }
                else
                    VGSvcVerbose(3, "vbsvcAutomounterPopulateTable: Not ours: %ls -> %ls\n", wszMountPoint, wszTargetPath);
            }
        }
    }

#elif defined(RT_OS_OS2)
    /*
     * Just loop thru the drive letters and check the attachment of each.
     */
    for (char chDrive = 'Z'; chDrive >= 'A'; chDrive--)
    {
        char const szMountPoint[4] = { chDrive, ':', '\0', '\0' };
        union
        {
            FSQBUFFER2  FsQueryBuf;
            char        achPadding[1024];
        } uBuf;
        RT_ZERO(uBuf);
        ULONG  cbBuf = sizeof(uBuf) - 2;
        APIRET rcOs2 = DosQueryFSAttach(szMountPoint, 0, FSAIL_QUERYNAME, &uBuf.FsQueryBuf, &cbBuf);
        if (rcOs2 == NO_ERROR)
        {
            const char *pszFsdName = (const char *)&uBuf.FsQueryBuf.szName[uBuf.FsQueryBuf.cbName + 1];
            if (   uBuf.FsQueryBuf.iType == FSAT_REMOTEDRV
                && RTStrICmpAscii(pszFsdName, "VBOXSF") == 0)
            {
                const char *pszMountedName = (const char *)&pszFsdName[uBuf.FsQueryBuf.cbFSDName + 1];
                const char *pszTag = pszMountedName + strlen(pszMountedName) + 1; /* (Safe. Always two trailing zero bytes, see above.) */
                if (strcmp(pszTag, g_szTag) == 0)
                {
                    rc = vbsvcAutomounterAddEntry(pMountTable, pszMountedName, szMountPoint);
                    if (RT_FAILURE(rc))
                        return rc;
                }
            }
        }
    }

#elif defined(RT_OS_LINUX)
    /*
     * Scan the mount table file for the mount point and then match file system
     * and device/share.  We identify our mounts by mount path + prefix for now,
     * but later we may use the same approach as on solaris.
     */
    FILE *pFile = setmntent("/proc/mounts", "r");
    int iErrMounts = errno;
    if (!pFile)
        pFile = setmntent("/etc/mtab", "r");
    if (pFile)
    {
        rc = VWRN_NOT_FOUND;
        struct mntent *pEntry;
        while ((pEntry = getmntent(pFile)) != NULL)
            if (strcmp(pEntry->mnt_type, "vboxsf") == 0)
                if (strstr(pEntry->mnt_opts, g_szTag) != NULL)
                {
                    rc = vbsvcAutomounterAddEntry(pMountTable, pEntry->mnt_fsname, pEntry->mnt_dir);
                    if (RT_FAILURE(rc))
                    {
                        endmntent(pFile);
                        return rc;
                    }
                }
        endmntent(pFile);
    }
    else
        VGSvcError("vbsvcAutomounterQueryMountPoint: Could not open mount tab '%s' (errno=%d) or '/proc/mounts' (errno=%d)\n",
                   _PATH_MOUNTED, errno, iErrMounts);

#elif defined(RT_OS_SOLARIS)
    /*
     * Look thru the system mount table and inspect the vboxsf mounts.
     */
    FILE *pFile = fopen(_PATH_MOUNTED, "r");
    if (pFile)
    {
        rc = VINF_SUCCESS;
        struct mnttab Entry;
        while (getmntent(pFile, &Entry) == 0)
            if (strcmp(Entry.mnt_fstype, "vboxfs") == 0)
            {
                /* Look for the dummy automounter option. */
                if (   Entry.mnt_mntopts != NULL
                    && strstr(Entry.mnt_mntopts, g_szTag) != NULL)
                {
                    rc = vbsvcAutomounterAddEntry(pMountTable, Entry.mnt_special, Entry.mnt_mountp);
                    if (RT_FAILURE(rc))
                    {
                        fclose(pFile);
                        return rc;
                    }
                }
            }
        fclose(pFile);
    }
    else
        VGSvcError("vbsvcAutomounterQueryMountPoint: Could not open mount tab '%s' (errno=%d)\n", _PATH_MOUNTED, errno);

#else
# error "PORTME!"
#endif

    /*
     * Try reconcile the detected folders with data from the host.
     */
    uint32_t                    cMappings = 0;
    PVBGLR3SHAREDFOLDERMAPPING  paMappings = NULL;
    rc = VbglR3SharedFolderGetMappings(g_idClientSharedFolders, true /*fAutoMountOnly*/, &paMappings, &cMappings);
    if (RT_SUCCESS(rc))
    {
        for (uint32_t i = 0; i < cMappings && RT_SUCCESS(rc); i++)
        {
            uint32_t const idRootSrc = paMappings[i].u32Root;

            uint32_t uRootIdVer = UINT32_MAX;
            uint64_t fFlags     = 0;
            char    *pszName    = NULL;
            char    *pszMntPt   = NULL;
            int rc2 = VbglR3SharedFolderQueryFolderInfo(g_idClientSharedFolders, idRootSrc,  VBOXSERVICE_AUTOMOUNT_MIQF,
                                                        &pszName, &pszMntPt, &fFlags, &uRootIdVer);
            if (RT_SUCCESS(rc2))
            {
                uint32_t iPrevHit = UINT32_MAX;
                for (uint32_t iTable = 0; iTable < pMountTable->cEntries; iTable++)
                {
                    PVBSVCAUTOMOUNTERENTRY pEntry = pMountTable->papEntries[iTable];
                    if (RTStrICmp(pEntry->pszName, pszName) == 0)
                    {
                        VGSvcVerbose(2, "vbsvcAutomounterPopulateTable: Identified %s -> %s: idRoot=%u ver=%u fFlags=%#x AutoMntPt=%s\n",
                                     pEntry->pszActualMountPoint, pEntry->pszName, idRootSrc, uRootIdVer, fFlags, pszMntPt);
                        pEntry->fFlags         = fFlags;
                        pEntry->idRoot         = idRootSrc;
                        pEntry->uRootIdVersion = uRootIdVer;
                        RTStrFree(pEntry->pszMountPoint);
                        pEntry->pszMountPoint = RTStrDup(pszMntPt);
                        if (!pEntry->pszMountPoint)
                        {
                            rc = VERR_NO_MEMORY;
                            break;
                        }

                        /* If multiple mappings of the same folder, pick the first or the one
                           with matching mount point. */
                        if (iPrevHit == UINT32_MAX)
                            iPrevHit = iTable;
                        else if (RTPathCompare(pszMntPt, pEntry->pszActualMountPoint) == 0)
                        {
                            if (iPrevHit != UINT32_MAX)
                                pMountTable->papEntries[iPrevHit]->uRootIdVersion -= 1;
                            iPrevHit = iTable;
                        }
                        else
                            pEntry->uRootIdVersion -= 1;
                    }
                }

                RTStrFree(pszName);
                RTStrFree(pszMntPt);
            }
            else
                VGSvcError("vbsvcAutomounterPopulateTable: VbglR3SharedFolderQueryFolderInfo(%u) failed: %Rrc\n", idRootSrc, rc2);
        }

        VbglR3SharedFolderFreeMappings(paMappings);

        /*
         * Sort the table by root ID.
         */
        if (pMountTable->cEntries > 1)
            RTSortApvShell((void **)pMountTable->papEntries, pMountTable->cEntries, vbsvcAutomounterCompareEntry, NULL);

        for (uint32_t iTable = 0; iTable < pMountTable->cEntries; iTable++)
        {
            PVBSVCAUTOMOUNTERENTRY pEntry = pMountTable->papEntries[iTable];
            if (pMountTable->papEntries[iTable]->idRoot != UINT32_MAX)
                VGSvcVerbose(1, "vbsvcAutomounterPopulateTable: #%u: %s -> %s idRoot=%u ver=%u fFlags=%#x AutoMntPt=%s\n",
                             iTable, pEntry->pszActualMountPoint, pEntry->pszName, pEntry->idRoot, pEntry->uRootIdVersion,
                             pEntry->fFlags, pEntry->pszMountPoint);
            else
                VGSvcVerbose(1, "vbsvcAutomounterPopulateTable: #%u: %s -> %s - not identified!\n",
                             iTable, pEntry->pszActualMountPoint, pEntry->pszName);
        }
    }
    else
        VGSvcError("vbsvcAutomounterPopulateTable: VbglR3SharedFolderGetMappings failed: %Rrc\n", rc);
    return rc;
}


/**
 * Checks whether the shared folder @a pszName is mounted on @a pszMountPoint.
 *
 * @returns Exactly one of the following IPRT status codes;
 * @retval  VINF_SUCCESS if mounted
 * @retval  VWRN_NOT_FOUND if nothing is mounted at @a pszMountPoint.
 * @retval  VERR_RESOURCE_BUSY if a different shared folder is mounted there.
 * @retval  VERR_ACCESS_DENIED if a non-shared folder file system is mounted
 *          there.
 *
 * @param   pszMountPoint   The mount point to check.
 * @param   pszName         The name of the shared folder (mapping).
 */
static int vbsvcAutomounterQueryMountPoint(const char *pszMountPoint, const char *pszName)
{
    VGSvcVerbose(4, "vbsvcAutomounterQueryMountPoint: pszMountPoint=%s pszName=%s\n", pszMountPoint, pszName);

#ifdef RT_OS_WINDOWS
    /*
     * We could've used RTFsQueryType here but would then have to
     * calling RTFsQueryLabel for the share name hint, ending up
     * doing the same work twice.  We could also use QueryDosDeviceW,
     * but output is less clear...
     */
    PRTUTF16 pwszMountPoint = NULL;
    int rc = RTStrToUtf16(pszMountPoint, &pwszMountPoint);
    if (RT_SUCCESS(rc))
    {
        DWORD   uSerial = 0;
        DWORD   cchCompMax = 0;
        DWORD   fFlags = 0;
        RTUTF16 wszLabel[512];
        RTUTF16 wszFileSystem[256];
        RT_ZERO(wszLabel);
        RT_ZERO(wszFileSystem);
        if (GetVolumeInformationW(pwszMountPoint, wszLabel, RT_ELEMENTS(wszLabel) - 1, &uSerial, &cchCompMax, &fFlags,
                                  wszFileSystem, RT_ELEMENTS(wszFileSystem) - 1))
        {
            if (RTUtf16ICmpAscii(wszFileSystem, "VBoxSharedFolderFS") == 0)
            {
                char *pszLabel = NULL;
                rc = RTUtf16ToUtf8(wszLabel, &pszLabel);
                if (RT_SUCCESS(rc))
                {
                    const char *pszMountedName = pszLabel;
                    if (RTStrStartsWith(pszMountedName, "VBOX_"))
                        pszMountedName += sizeof("VBOX_") - 1;
                    if (RTStrICmp(pszMountedName, pszName) == 0)
                    {
                        VGSvcVerbose(3, "vbsvcAutomounterQueryMountPoint: Found shared folder '%s' at '%s'.\n",
                                     pszName, pszMountPoint);
                        rc = VINF_SUCCESS;
                    }
                    else
                    {
                        VGSvcVerbose(3, "vbsvcAutomounterQueryMountPoint: Found shared folder '%s' at '%s', not '%s'...\n",
                                     pszMountedName, pszMountPoint, pszName);
                        rc = VERR_RESOURCE_BUSY;
                    }
                    RTStrFree(pszLabel);
                }
                else
                {
                    VGSvcVerbose(3, "vbsvcAutomounterQueryMountPoint: RTUtf16ToUtf8(%ls,) failed: %Rrc\n", wszLabel, rc);
                    rc = VERR_RESOURCE_BUSY;
                }
            }
            else
            {
                VGSvcVerbose(3, "vbsvcAutomounterQueryMountPoint: Found a '%ls' with label '%ls' mount at '%s', not '%s'...\n",
                             wszFileSystem, wszLabel, pszMountPoint, pszName);
                rc = VERR_ACCESS_DENIED;
            }
        }
        else
        {
            rc = GetLastError();
            if (rc != ERROR_PATH_NOT_FOUND || g_cVerbosity >= 4)
                VGSvcVerbose(3, "vbsvcAutomounterQueryMountPoint: GetVolumeInformationW('%ls',,,,) failed: %u\n", pwszMountPoint, rc);
            if (rc == ERROR_PATH_NOT_FOUND)
                rc = VWRN_NOT_FOUND;
            else if (   RT_C_IS_ALPHA(pszMountPoint[0])
                     && pszMountPoint[1] == ':'
                     && (   pszMountPoint[2] == '\0'
                         || (RTPATH_IS_SLASH(pszMountPoint[2]) && pszMountPoint[3] == '\0')))
            {
                /* See whether QueryDosDeviceW thinks its a malfunctioning shared folder or
                   something else (it doesn't access the file system).  We've seen
                   VERR_NET_HOST_NOT_FOUND here for shared folders that was removed on the
                   host side.

                   Note! This duplicates code from vbsvcAutomounterPopulateTable. */
                rc = VERR_ACCESS_DENIED;
                static const char s_szDevicePath[] = "\\Device\\VBoxMiniRdr\\;";
                wszFileSystem[0] = pwszMountPoint[0];
                wszFileSystem[1] = pwszMountPoint[1];
                wszFileSystem[2] = '\0';
                DWORD const cwcResult = QueryDosDeviceW(wszFileSystem, wszLabel, RT_ELEMENTS(wszLabel));
                if (   cwcResult > sizeof(s_szDevicePath)
                    && RTUtf16NICmpAscii(wszLabel, RT_STR_TUPLE(s_szDevicePath)) == 0)
                {
                    PCRTUTF16 pwsz = &wszLabel[RT_ELEMENTS(s_szDevicePath) - 1];
                    Assert(pwsz[-1] == ';');
                    if (   (pwsz[0] & ~(RTUTF16)0x20) == (wszFileSystem[0] & ~(RTUTF16)0x20)
                        && pwsz[1] == ':'
                        && pwsz[2] == '\\')
                    {
                        if (RTUtf16NICmpAscii(&pwsz[3], RT_STR_TUPLE("VBoxSvr\\")) == 0)
                        {
                            pwsz += 3 + 8;
                            char *pszMountedName = NULL;
                            rc = RTUtf16ToUtf8(pwsz, &pszMountedName);
                            if (RT_SUCCESS(rc))
                            {
                                if (RTStrICmp(pszMountedName, pszName) == 0)
                                {
                                    rc = VINF_SUCCESS;
                                    VGSvcVerbose(2, "vbsvcAutomounterQueryMountPoint: Found shared folder '%s' at '%s' (using QueryDosDeviceW).\n",
                                                 pszName, pszMountPoint);
                                }
                                else
                                {
                                    VGSvcVerbose(2, "vbsvcAutomounterQueryMountPoint: Found shared folder '%s' at '%s' (using QueryDosDeviceW), not '%s'...\n",
                                                 pszMountedName, pszMountPoint, pszName);
                                    rc = VERR_RESOURCE_BUSY;
                                }
                                RTStrFree(pszMountedName);
                            }
                            else
                            {
                                VGSvcVerbose(2, "vbsvcAutomounterQueryMountPoint: RTUtf16ToUtf8 failed: %Rrc\n", rc);
                                AssertRC(rc);
                                rc = VERR_RESOURCE_BUSY;
                            }
                        }
                    }
                }
            }
            else
                rc = VERR_ACCESS_DENIED;
        }
        RTUtf16Free(pwszMountPoint);
    }
    else
    {
        VGSvcError("vbsvcAutomounterQueryMountPoint: RTStrToUtf16(%s,) -> %Rrc\n", pszMountPoint, rc);
        rc = VWRN_NOT_FOUND;
    }
    return rc;

#elif defined(RT_OS_OS2)
    /*
     * Query file system attachment info for the given drive letter.
     */
    union
    {
        FSQBUFFER2  FsQueryBuf;
        char        achPadding[512];
    } uBuf;
    RT_ZERO(uBuf);

    ULONG cbBuf = sizeof(uBuf);
    APIRET rcOs2 = DosQueryFSAttach(pszMountPoint, 0, FSAIL_QUERYNAME, &uBuf.FsQueryBuf, &cbBuf);
    int rc;
    if (rcOs2 == NO_ERROR)
    {
        const char *pszFsdName = (const char *)&uBuf.FsQueryBuf.szName[uBuf.FsQueryBuf.cbName + 1];
        if (   uBuf.FsQueryBuf.iType == FSAT_REMOTEDRV
            && RTStrICmpAscii(pszFsdName, "VBOXSF") == 0)
        {
            const char *pszMountedName = (const char *)&pszFsdName[uBuf.FsQueryBuf.cbFSDName + 1];
            if (RTStrICmp(pszMountedName, pszName) == 0)
            {
                VGSvcVerbose(3, "vbsvcAutomounterQueryMountPoint: Found shared folder '%s' at '%s'.\n",
                             pszName, pszMountPoint);
                rc = VINF_SUCCESS;
            }
            else
            {
                VGSvcVerbose(3, "vbsvcAutomounterQueryMountPoint: Found shared folder '%s' at '%s', not '%s'...\n",
                             pszMountedName, pszMountPoint, pszName);
                rc = VERR_RESOURCE_BUSY;
            }
        }
        else
        {
            VGSvcVerbose(3, "vbsvcAutomounterQueryMountPoint: Found a '%s' type %u mount at '%s', not '%s'...\n",
                         pszFsdName, uBuf.FsQueryBuf.iType, pszMountPoint, pszName);
            rc = VERR_ACCESS_DENIED;
        }
    }
    else
    {
        rc = VWRN_NOT_FOUND;
        VGSvcVerbose(3, "vbsvcAutomounterQueryMountPoint: DosQueryFSAttach(%s) -> %u\n", pszMountPoint, rcOs2);
        AssertMsgStmt(rcOs2 != ERROR_BUFFER_OVERFLOW && rcOs2 != ERROR_INVALID_PARAMETER,
                      ("%s -> %u\n", pszMountPoint, rcOs2), rc = VERR_ACCESS_DENIED);
    }
    return rc;

#elif defined(RT_OS_LINUX)
    /*
     * Scan one of the mount table file for the mount point and then
     * match file system and device/share.
     */
    FILE *pFile = setmntent("/proc/mounts", "r");
    int rc = errno;
    if (!pFile)
        pFile = setmntent(_PATH_MOUNTED, "r");
    if (pFile)
    {
        rc = VWRN_NOT_FOUND;
        struct mntent *pEntry;
        while ((pEntry = getmntent(pFile)) != NULL)
            if (RTPathCompare(pEntry->mnt_dir, pszMountPoint) == 0)
            {
                if (strcmp(pEntry->mnt_type, "vboxsf") == 0)
                {
                    if (RTStrICmp(pEntry->mnt_fsname, pszName) == 0)
                    {
                        VGSvcVerbose(3, "vbsvcAutomounterQueryMountPoint: Found shared folder '%s' at '%s'.\n",
                                     pszName, pszMountPoint);
                        rc = VINF_SUCCESS;
                    }
                    else
                    {
                        VGSvcVerbose(3, "vbsvcAutomounterQueryMountPoint: Found shared folder '%s' at '%s', not '%s'...\n",
                                     pEntry->mnt_fsname, pszMountPoint, pszName);
                        rc = VERR_RESOURCE_BUSY;
                    }
                }
                else
                {
                    VGSvcVerbose(3, "vbsvcAutomounterQueryMountPoint: Found a '%s' mount of '%s' at '%s', not '%s'...\n",
                                 pEntry->mnt_type, pEntry->mnt_fsname, pszMountPoint, pszName);
                    rc = VERR_ACCESS_DENIED;
                }
                /* We continue searching in case of stacked mounts, we want the last one. */
            }
        endmntent(pFile);
    }
    else
    {
        VGSvcError("vbsvcAutomounterQueryMountPoint: Could not open mount tab '/proc/mounts' (errno=%d) or '%s' (errno=%d)\n",
                   rc, _PATH_MOUNTED, errno);
        rc = VERR_ACCESS_DENIED;
    }
    return rc;

#elif defined(RT_OS_SOLARIS)
    /*
     * Similar to linux.
     */
    int rc;
    FILE *pFile = fopen(_PATH_MOUNTED, "r");
    if (pFile)
    {
        rc = VWRN_NOT_FOUND;
        struct mnttab Entry;
        while (getmntent(pFile, &Entry) == 0)
            if (RTPathCompare(Entry.mnt_mountp, pszMountPoint) == 0)
            {
                if (strcmp(Entry.mnt_fstype, "vboxfs") == 0)
                {
                    if (RTStrICmp(Entry.mnt_special, pszName) == 0)
                    {
                        VGSvcVerbose(3, "vbsvcAutomounterQueryMountPoint: Found shared folder '%s' at '%s'.\n",
                                     pszName, pszMountPoint);
                        rc = VINF_SUCCESS;
                    }
                    else
                    {
                        VGSvcVerbose(3, "vbsvcAutomounterQueryMountPoint: Found shared folder '%s' at '%s', not '%s'...\n",
                                     Entry.mnt_special, pszMountPoint, pszName);
                        rc = VERR_RESOURCE_BUSY;
                    }
                }
                else
                {
                    VGSvcVerbose(3, "vbsvcAutomounterQueryMountPoint: Found a '%s' mount of '%s' at '%s', not '%s'...\n",
                                 Entry.mnt_fstype, Entry.mnt_special, pszMountPoint, pszName);
                    rc = VERR_ACCESS_DENIED;
                }
                /* We continue searching in case of stacked mounts, we want the last one. */
            }
        fclose(pFile);
    }
    else
    {
        VGSvcError("vbsvcAutomounterQueryMountPoint: Could not open mount tab '%s' (errno=%d)\n", _PATH_MOUNTED, errno);
        rc = VERR_ACCESS_DENIED;
    }
    return rc;
#else
# error "PORTME"
#endif
}


/**
 * Worker for vbsvcAutomounterMountNewEntry that does the OS mounting.
 *
 * @returns IPRT status code.
 * @param   pEntry      The entry to try mount.
 */
static int vbsvcAutomounterMountIt(PVBSVCAUTOMOUNTERENTRY pEntry)
{
    VGSvcVerbose(3, "vbsvcAutomounterMountIt: Trying to mount '%s' (idRoot=%#x) on '%s'...\n",
                 pEntry->pszName, pEntry->idRoot, pEntry->pszActualMountPoint);
#ifdef RT_OS_WINDOWS
    /*
     * Attach the shared folder using WNetAddConnection2W.
     *
     * According to google we should get a drive symlink in \\GLOBAL?? when
     * we are running under the system account.  Otherwise it will be a session
     * local link (\\??).
     */
    Assert(RT_C_IS_UPPER(pEntry->pszActualMountPoint[0]) && pEntry->pszActualMountPoint[1] == ':' && pEntry->pszActualMountPoint[2] == '\0');
    RTUTF16 wszDrive[4] = { (RTUTF16)pEntry->pszActualMountPoint[0], ':', '\0', '\0' };

    RTUTF16 wszPrefixedName[RTPATH_MAX];
    int rc = RTUtf16CopyAscii(wszPrefixedName, RT_ELEMENTS(wszPrefixedName), "\\\\VBoxSvr\\");
    AssertRC(rc);

    size_t const offName = RTUtf16Len(wszPrefixedName);
    PRTUTF16 pwszName = &wszPrefixedName[offName];
    rc = RTStrToUtf16Ex(pEntry->pszName, RTSTR_MAX, &pwszName, sizeof(wszPrefixedName) - offName, NULL);
    if (RT_FAILURE(rc))
    {
        VGSvcError("vbsvcAutomounterMountIt: RTStrToUtf16Ex failed on '%s': %Rrc\n", pEntry->pszName, rc);
        return rc;
    }

    VGSvcVerbose(3, "vbsvcAutomounterMountIt: wszDrive='%ls', wszPrefixedName='%ls'\n",
                 wszDrive, wszPrefixedName);

    NETRESOURCEW NetRsrc;
    RT_ZERO(NetRsrc);
    NetRsrc.dwType          = RESOURCETYPE_DISK;
    NetRsrc.lpLocalName     = wszDrive;
    NetRsrc.lpRemoteName    = wszPrefixedName;
    NetRsrc.lpProvider      = (PWSTR)L"VirtualBox Shared Folders"; /* Only try our provider. */
    NetRsrc.lpComment       = pwszName;

    DWORD dwErr = WNetAddConnection2W(&NetRsrc, NULL /*pwszPassword*/, NULL /*pwszUserName*/, 0 /*dwFlags*/);
    if (dwErr == NO_ERROR)
    {
        VGSvcVerbose(0, "vbsvcAutomounterMountIt: Successfully mounted '%s' on '%s'\n",
                     pEntry->pszName, pEntry->pszActualMountPoint);
        return VINF_SUCCESS;
    }
    else if (dwErr == ERROR_BAD_PROVIDER)
        VGSvcError("vbsvcAutomounterMountIt: Failed to attach '%s' to '%s': VirtualBox Shared Folders provider not or " \
                   "incorrectly installed, can't continue\n",
                   pEntry->pszName, pEntry->pszActualMountPoint);
    else
        VGSvcError("vbsvcAutomounterMountIt: Failed to attach '%s' to '%s': %Rrc (%u)\n",
                   pEntry->pszName, pEntry->pszActualMountPoint, RTErrConvertFromWin32(dwErr), dwErr);
    return VERR_OPEN_FAILED;

#elif defined(RT_OS_OS2)
    /*
     * It's a rather simple affair on OS/2.
     *
     * In order to be able to detect our mounts we add a 2nd string after
     * the folder name that tags the attachment.  The IFS will remember this
     * and return it when DosQueryFSAttach is called.
     *
     * Note! Kernel currently accepts limited 7-bit ASCII names.  We could
     *       change that to UTF-8 if we like as that means no extra string
     *       encoding conversion fun here.
     */
    char    szzNameAndTag[256];
    size_t  cchName = strlen(pEntry->pszName);
    if (cchName + 1 + sizeof(g_szTag) <= sizeof(szzNameAndTag))
    {
        memcpy(szzNameAndTag, pEntry->pszName, cchName);
        szzNameAndTag[cchName] = '\0';
        memcpy(&szzNameAndTag[cchName + 1], g_szTag, sizeof(g_szTag));

        APIRET rc = DosFSAttach(pEntry->pszActualMountPoint, "VBOXSF", szzNameAndTag, cchName + 1 + sizeof(g_szTag), FS_ATTACH);
        if (rc == NO_ERROR)
        {
            VGSvcVerbose(0, "vbsvcAutomounterMountIt: Successfully mounted '%s' on '%s'\n",
                         pEntry->pszName, pEntry->pszActualMountPoint);
            return VINF_SUCCESS;
        }
        VGSvcError("vbsvcAutomounterMountIt: DosFSAttach failed to attach '%s' to '%s': %u\n",
                   pEntry->pszName, pEntry->pszActualMountPoint, rc);
    }
    else
        VGSvcError("vbsvcAutomounterMountIt: Share name for attach to '%s' is too long: %u chars - '%s'\n",
                   pEntry->pszActualMountPoint, cchName, pEntry->pszName);
    return VERR_OPEN_FAILED;

#else
    /*
     * Common work for unix-like systems: Get group, make sure mount directory exist.
     */
    int rc = RTDirCreateFullPath(pEntry->pszActualMountPoint,
                                 RTFS_UNIX_IRWXU | RTFS_UNIX_IXGRP | RTFS_UNIX_IRGRP | RTFS_UNIX_IXOTH | RTFS_UNIX_IROTH);
    if (RT_FAILURE(rc))
    {
        VGSvcError("vbsvcAutomounterMountIt: Failed to create mount path '%s' for share '%s': %Rrc\n",
                   pEntry->pszActualMountPoint, pEntry->pszName, rc);
        return rc;
    }

    gid_t gidMount;
    struct group *grp_vboxsf = getgrnam("vboxsf");
    if (grp_vboxsf)
        gidMount = grp_vboxsf->gr_gid;
    else
    {
        VGSvcError("vbsvcAutomounterMountIt: Group 'vboxsf' does not exist\n");
        gidMount = 0;
    }

#  if defined(RT_OS_LINUX)
    /*
     * Linux a bit more work...
     */
    struct utsname uts;
    AssertStmt(uname(&uts) != -1, strcpy(uts.release, "4.4.0"));
    /*
     * Allocate options string buffer which is limited to a page on most systems.
     */
    uint32_t cbOpts = RTSystemGetPageSize();
    char *pszOpts = (char *)alloca(cbOpts);

    /* Built mount option string.  Need st_name for pre 2.6.0 kernels. */
    unsigned long const fFlags = MS_NODEV;
    ssize_t cchOpts = RTStrPrintf2(pszOpts, cbOpts,
                                   "uid=0,gid=%d,dmode=0770,fmode=0770,dmask=0000,fmask=0000,tag=%s", gidMount, g_szTag);
    if (RTStrVersionCompare(uts.release, "2.6.0") < 0 && cchOpts > 0)
        cchOpts += RTStrPrintf2(&pszOpts[cchOpts], cbOpts - cchOpts, ",sf_name=%s", pEntry->pszName);
    if (cchOpts <= 0)
    {
        VGSvcError("vbsvcAutomounterMountIt: pszOpts overflow! %zd\n", cchOpts);
        return VERR_BUFFER_OVERFLOW;
    }

    /* Do the mounting. The fallback w/o tag is for the Linux vboxsf fork
       which lagged a lot behind when it first appeared in 5.6. */
    errno = 0;
    rc = mount(pEntry->pszName, pEntry->pszActualMountPoint, "vboxsf", fFlags, pszOpts);
    if (rc != 0 && errno == EINVAL && RTStrVersionCompare(uts.release, "5.6.0") >= 0)
    {
        VGSvcVerbose(2, "vbsvcAutomounterMountIt: mount returned EINVAL, retrying without the tag.\n");
        *strstr(pszOpts, ",tag=") = '\0';
        errno = 0;
        rc = mount(pEntry->pszName, pEntry->pszActualMountPoint, "vboxsf", fFlags, pszOpts);
        if (rc == 0)
            VGSvcVerbose(0, "vbsvcAutomounterMountIt: Running outdated vboxsf module without support for the 'tag' option?\n");
    }
    if (rc == 0)
    {
        VGSvcVerbose(0, "vbsvcAutomounterMountIt: Successfully mounted '%s' on '%s'\n",
                     pEntry->pszName, pEntry->pszActualMountPoint);

        errno = 0;
        rc = vbsfmount_complete(pEntry->pszName, pEntry->pszActualMountPoint, fFlags, pszOpts);
        if (rc != 0) /* Ignorable. /etc/mtab is probably a link to /proc/mounts. */
            VGSvcVerbose(1, "vbsvcAutomounterMountIt: vbsfmount_complete failed: %s (%d/%d)\n",
                         rc == 1 ? "malloc" : rc == 2 ? "setmntent" : rc == 3 ? "addmntent" : "unknown", rc, errno);
        return VINF_SUCCESS;
    }

    if (errno == EINVAL)
        VGSvcError("vbsvcAutomounterMountIt: Failed to mount '%s' on '%s' because it is probably mounted elsewhere arleady! (%d,%d)\n",
                   pEntry->pszName, pEntry->pszActualMountPoint, rc, errno);
    else
        VGSvcError("vbsvcAutomounterMountIt: Failed to mount '%s' on '%s': %s (%d,%d)\n",
                   pEntry->pszName, pEntry->pszActualMountPoint, strerror(errno), rc, errno);
    return VERR_WRITE_ERROR;

#  elif defined(RT_OS_SOLARIS)
    /*
     * Solaris is rather simple compared to linux.
     *
     * The ',VBoxService=auto' option (g_szTag) is ignored by the kernel but helps
     * us identify our own mounts on restart.  See vbsvcAutomounterPopulateTable().
     *
     * Note! Must pass MAX_MNTOPT_STR rather than cchOpts to mount, as it may fail
     *       with EOVERFLOW in vfs_buildoptionstr() during domount() otherwise.
     */
    char szOpts[MAX_MNTOPT_STR] = { '\0', };
    ssize_t cchOpts = RTStrPrintf2(szOpts, sizeof(szOpts),
                                   "uid=0,gid=%d,dmode=0770,fmode=0770,dmask=0000,fmask=0000,tag=%s", gidMount, g_szTag);
    if (cchOpts <= 0)
    {
        VGSvcError("vbsvcAutomounterMountIt: szOpts overflow! %zd\n", cchOpts);
        return VERR_BUFFER_OVERFLOW;
    }

    rc = mount(pEntry->pszName, pEntry->pszActualMountPoint, MS_OPTIONSTR, "vboxfs",
               NULL /*dataptr*/, 0 /* datalen */, szOpts, MAX_MNTOPT_STR);
    if (rc == 0)
    {
        VGSvcVerbose(0, "vbsvcAutomounterMountIt: Successfully mounted '%s' on '%s'\n",
                     pEntry->pszName, pEntry->pszActualMountPoint);
        return VINF_SUCCESS;
    }

    rc = errno;
    VGSvcError("vbsvcAutomounterMountIt: mount failed for '%s' on '%s' (szOpts=%s): %s (%d)\n",
               pEntry->pszName, pEntry->pszActualMountPoint, szOpts, strerror(rc), rc);
    return VERR_OPEN_FAILED;

# else
#  error "PORTME!"
# endif
#endif
}


/**
 * Attempts to mount the given shared folder, adding it to the mount table on
 * success.
 *
 * @returns iTable + 1 on success, iTable on failure.
 * @param   pTable          The mount table.
 * @param   iTable          The mount table index at which to add the mount.
 * @param   pszName         The name of the shared folder mapping.
 * @param   pszMntPt        The mount point (hint) specified by the host.
 * @param   fFlags          The shared folder flags, SHFL_MIF_XXX.
 * @param   idRoot          The root ID.
 * @param   uRootIdVersion  The root ID version.
 * @param   fAutoMntPt      Whether to try automatically assign a mount point if
 *                          pszMntPt doesn't work out.  This is set in pass \#3.
 */
static uint32_t vbsvcAutomounterMountNewEntry(PVBSVCAUTOMOUNTERTABLE pTable, uint32_t iTable,
                                              const char *pszName, const char *pszMntPt, uint64_t fFlags,
                                              uint32_t idRoot, uint32_t uRootIdVersion, bool fAutoMntPt)
{
    VGSvcVerbose(3, "vbsvcAutomounterMountNewEntry: #%u: '%s' at '%s'%s\n",
                 iTable, pszName, pszMntPt, fAutoMntPt ? " auto-assign" : "");

    /*
     * First we need to figure out the actual mount point.
     */
    char szActualMountPoint[RTPATH_MAX];

#if defined(RT_OS_OS2) || defined(RT_OS_WINDOWS)
    /*
     * Drive letter based.  We only care about the first two characters
     * and ignore the rest (see further down).
     */
    char chNextLetter = 'Z';
    if (RT_C_IS_ALPHA(pszMntPt[0]) && pszMntPt[1] == ':')
        szActualMountPoint[0] = RT_C_TO_UPPER(pszMntPt[0]);
    else if (!fAutoMntPt)
        return iTable;
    else
        szActualMountPoint[0] = chNextLetter--;
    szActualMountPoint[1] = ':';
    szActualMountPoint[2] = '\0';

    int rc;
    for (;;)
    {
        rc = vbsvcAutomounterQueryMountPoint(szActualMountPoint, pszName);
        if (rc == VWRN_NOT_FOUND)
            break;

        /* next */
        if (chNextLetter == 'A' || !fAutoMntPt)
            return iTable;
        szActualMountPoint[0] = chNextLetter--;
    }

#else
    /*
     * Path based #1: Host specified mount point.
     */

    /* Skip DOS drive letter if there is a UNIX mount point path following it: */
    if (   pszMntPt[0] != '/'
        && pszMntPt[0] != '\0'
        && pszMntPt[1] == ':'
        && pszMntPt[2] == '/')
        pszMntPt += 2;

    /* Try specified mount point if it starts with a UNIX slash: */
    int rc = VERR_ACCESS_DENIED;
    if (*pszMntPt == '/')
    {
        rc = RTPathAbs(pszMntPt, szActualMountPoint, sizeof(szActualMountPoint));
        if (RT_SUCCESS(rc))
        {
            static const char * const s_apszBlacklist[] =
            { "/", "/dev", "/bin", "/sbin", "/lib", "/etc", "/var", "/tmp", "/usr", "/usr/bin", "/usr/sbin", "/usr/lib" };
            for (size_t i = 0; i < RT_ELEMENTS(s_apszBlacklist); i++)
                if (strcmp(szActualMountPoint, s_apszBlacklist[i]) == 0)
                {
                    rc = VERR_ACCESS_DENIED;
                    break;
                }
            if (RT_SUCCESS(rc))
                rc = vbsvcAutomounterQueryMountPoint(szActualMountPoint, pszName);
        }
    }
    if (rc != VWRN_NOT_FOUND)
    {
        if (!fAutoMntPt)
            return iTable;

        /*
         * Path based #2: Mount dir + prefix + share.
         */
        rc = vbsvcAutomounterQueryMountDirAndPrefix(szActualMountPoint, sizeof(szActualMountPoint));
        if (RT_SUCCESS(rc))
        {
            /* Append a sanitized share name: */
            size_t const offShare = strlen(szActualMountPoint);
            size_t offDst = offShare;
            size_t offSrc = 0;
            for (;;)
            {
                char ch = pszName[offSrc++];
                if (ch == ' ' || ch == '/' || ch == '\\' || ch == ':' || ch == '$')
                    ch = '_';
                else if (!ch)
                    break;
                else if (ch < 0x20 || ch == 0x7f)
                    continue;
                if (offDst < sizeof(szActualMountPoint) - 1)
                    szActualMountPoint[offDst++] = ch;
            }
            szActualMountPoint[offDst] = '\0';
            if (offDst > offShare)
            {
                rc = vbsvcAutomounterQueryMountPoint(szActualMountPoint, pszName);
                if (rc != VWRN_NOT_FOUND)
                {
                    /*
                     * Path based #3: Mount dir + prefix + share + _ + number.
                     */
                    if (offDst + 2 >= sizeof(szActualMountPoint))
                        return iTable;

                    szActualMountPoint[offDst++] = '_';
                    for (uint32_t iTry = 1; iTry < 10 && rc != VWRN_NOT_FOUND; iTry++)
                    {
                        szActualMountPoint[offDst] = '0' + iTry;
                        szActualMountPoint[offDst + 1] = '\0';
                        rc = vbsvcAutomounterQueryMountPoint(szActualMountPoint, pszName);
                    }
                    if (rc != VWRN_NOT_FOUND)
                       return iTable;
                }
            }
            else
                VGSvcError("vbsvcAutomounterMountNewEntry: Bad share name: %.*Rhxs", strlen(pszName), pszName);
        }
        else
            VGSvcError("vbsvcAutomounterMountNewEntry: Failed to construct basic auto mount point for '%s'", pszName);
    }
#endif

    /*
     * Prepare a table entry and ensure space in the table..
     */
    if (pTable->cEntries + 1 > pTable->cAllocated)
    {
        void *pvEntries = RTMemRealloc(pTable->papEntries, sizeof(pTable->papEntries[0]) * (pTable->cAllocated + 8));
        if (!pvEntries)
        {
            VGSvcError("vbsvcAutomounterMountNewEntry: Out of memory for growing table (size %u)\n", pTable->cAllocated);
            return iTable;
        }
        pTable->cAllocated += 8;
        pTable->papEntries = (PVBSVCAUTOMOUNTERENTRY *)pvEntries;
    }

    PVBSVCAUTOMOUNTERENTRY pEntry = (PVBSVCAUTOMOUNTERENTRY)RTMemAlloc(sizeof(*pEntry));
    if (pEntry)
    {
        pEntry->idRoot              = idRoot;
        pEntry->uRootIdVersion      = uRootIdVersion;
        pEntry->fFlags              = fFlags;
        pEntry->pszName             = RTStrDup(pszName);
        pEntry->pszMountPoint       = RTStrDup(pszMntPt);
        pEntry->pszActualMountPoint = RTStrDup(szActualMountPoint);
        if (pEntry->pszName && pEntry->pszMountPoint && pEntry->pszActualMountPoint)
        {
            /*
             * Now try mount it.
             */
            rc = vbsvcAutomounterMountIt(pEntry);
            if (RT_SUCCESS(rc))
            {
                uint32_t cToMove = pTable->cEntries - iTable;
                if (cToMove > 0)
                    memmove(&pTable->papEntries[iTable + 1], &pTable->papEntries[iTable], cToMove * sizeof(pTable->papEntries[0]));
                pTable->papEntries[iTable] = pEntry;
                pTable->cEntries++;
                return iTable + 1;
            }
        }
        else
            VGSvcError("vbsvcAutomounterMountNewEntry: Out of memory for table entry!\n");
        RTMemFree(pEntry->pszActualMountPoint);
        RTMemFree(pEntry->pszMountPoint);
        RTMemFree(pEntry->pszName);
        RTMemFree(pEntry);
    }
    else
        VGSvcError("vbsvcAutomounterMountNewEntry: Out of memory for table entry!\n");
    return iTable;
}



/**
 * Does the actual unmounting.
 *
 * @returns Exactly one of the following IPRT status codes;
 * @retval  VINF_SUCCESS if successfully umounted or nothing was mounted there.
 * @retval  VERR_TRY_AGAIN if the shared folder is busy.
 * @retval  VERR_RESOURCE_BUSY if a different shared folder is mounted there.
 * @retval  VERR_ACCESS_DENIED if a non-shared folder file system is mounted
 *          there.
 *
 * @param   pszMountPoint       The mount point.
 * @param   pszName             The shared folder (mapping) name.
 */
static int vbsvcAutomounterUnmount(const char *pszMountPoint, const char *pszName)
{
    /*
     * Retry for 5 seconds in a hope that busy mounts will quiet down.
     */
    for (unsigned iTry = 0; ; iTry++)
    {
        /*
         * Check what's mounted there before we start umounting stuff.
         */
        int rc = vbsvcAutomounterQueryMountPoint(pszMountPoint, pszName);
        if (rc == VINF_SUCCESS)
        { /* pszName is mounted there */ }
        else if (rc == VWRN_NOT_FOUND) /* nothing mounted there */
            return VINF_SUCCESS;
        else
        {
            Assert(rc == VERR_RESOURCE_BUSY || rc == VERR_ACCESS_DENIED);
            return VERR_RESOURCE_BUSY;
        }

        /*
         * Do host specific unmounting.
         */
#ifdef RT_OS_WINDOWS
        Assert(RT_C_IS_UPPER(pszMountPoint[0]) && pszMountPoint[1] == ':' && pszMountPoint[2] == '\0');
        RTUTF16 const wszDrive[4] = { (RTUTF16)pszMountPoint[0], ':', '\0', '\0' };
        DWORD dwErr = WNetCancelConnection2W(wszDrive, 0 /*dwFlags*/, FALSE /*fForce*/);
        if (dwErr == NO_ERROR)
            return VINF_SUCCESS;
        VGSvcVerbose(2, "vbsvcAutomounterUnmount: WNetCancelConnection2W returns %u for '%s' ('%s')\n", dwErr, pszMountPoint, pszName);
        if (dwErr == ERROR_NOT_CONNECTED)
            return VINF_SUCCESS;

#elif defined(RT_OS_OS2)
        APIRET rcOs2 = DosFSAttach(pszMountPoint, "VBOXSF", NULL, 0, FS_DETACH);
        if (rcOs2 == NO_ERROR)
            return VINF_SUCCESS;
        VGSvcVerbose(2, "vbsvcAutomounterUnmount: DosFSAttach failed on '%s' ('%s'): %u\n", pszMountPoint, pszName, rcOs2);
        if (rcOs2 == ERROR_INVALID_FSD_NAME)
            return VERR_ACCESS_DENIED;
        if (   rcOs2 == ERROR_INVALID_DRIVE
            || rcOs2 == ERROR_INVALID_PATH)
            return VERR_TRY_AGAIN;

#else
        int rc2 = umount(pszMountPoint);
        if (rc2 == 0)
        {
            /* Remove the mount directory if not directly under the root dir. */
            RTPATHPARSED Parsed;
            RT_ZERO(Parsed);
            RTPathParse(pszMountPoint, &Parsed, sizeof(Parsed), RTPATH_STR_F_STYLE_HOST);
            if (Parsed.cComps >= 3)
                RTDirRemove(pszMountPoint);

            return VINF_SUCCESS;
        }
        rc2 = errno;
        VGSvcVerbose(2, "vbsvcAutomounterUnmount: umount failed on '%s' ('%s'): %d\n", pszMountPoint, pszName, rc2);
        if (rc2 != EBUSY && rc2 != EAGAIN)
            return VERR_ACCESS_DENIED;
#endif

        /*
         * Check what's mounted there before we start delaying.
         */
        RTThreadSleep(8); /* fudge */
        rc = vbsvcAutomounterQueryMountPoint(pszMountPoint, pszName);
        if (rc == VINF_SUCCESS)
        { /* pszName is mounted there */ }
        else if (rc == VWRN_NOT_FOUND) /* nothing mounted there */
            return VINF_SUCCESS;
        else
        {
            Assert(rc == VERR_RESOURCE_BUSY || rc == VERR_ACCESS_DENIED);
            return VERR_RESOURCE_BUSY;
        }

        if (iTry >= 5)
            return VERR_TRY_AGAIN;
        RTThreadSleep(1000);
    }
}


/**
 * Unmounts a mount table entry and evicts it from the table if successful.
 *
 * @returns The next iTable (same value on success, +1 on failure).
 * @param   pTable              The mount table.
 * @param   iTable              The table entry.
 * @param   pszReason           Why we're here.
 */
static uint32_t vbsvcAutomounterUnmountEntry(PVBSVCAUTOMOUNTERTABLE pTable, uint32_t iTable, const char *pszReason)
{
    Assert(iTable < pTable->cEntries);
    PVBSVCAUTOMOUNTERENTRY pEntry = pTable->papEntries[iTable];
    VGSvcVerbose(2, "vbsvcAutomounterUnmountEntry: #%u: '%s' at '%s' (reason: %s)\n",
                 iTable, pEntry->pszName, pEntry->pszActualMountPoint, pszReason);

    /*
     * Do we need to umount the entry?  Return if unmount fails and we .
     */
    if (pEntry->pszActualMountPoint)
    {
        int rc = vbsvcAutomounterUnmount(pEntry->pszActualMountPoint, pEntry->pszName);
        if (rc == VERR_TRY_AGAIN)
        {
            VGSvcVerbose(1, "vbsvcAutomounterUnmountEntry: Keeping '%s' -> '%s' (VERR_TRY_AGAIN)\n",
                         pEntry->pszActualMountPoint, pEntry->pszName);
            return iTable + 1;
        }
    }

    /*
     * Remove the entry by shifting up the ones after it.
     */
    pTable->cEntries -= 1;
    uint32_t cAfter = pTable->cEntries - iTable;
    if (cAfter)
        memmove(&pTable->papEntries[iTable], &pTable->papEntries[iTable + 1], cAfter * sizeof(pTable->papEntries[0]));
    pTable->papEntries[pTable->cEntries] = NULL;

    RTStrFree(pEntry->pszActualMountPoint);
    pEntry->pszActualMountPoint = NULL;
    RTStrFree(pEntry->pszMountPoint);
    pEntry->pszMountPoint = NULL;
    RTStrFree(pEntry->pszName);
    pEntry->pszName = NULL;
    RTMemFree(pEntry);

    return iTable;
}


/**
 * @callback_method_impl{FNRTSORTCMP, For sorting the mappings by ID. }
 */
static DECLCALLBACK(int) vbsvcSharedFolderMappingCompare(void const *pvElement1, void const *pvElement2, void *pvUser)
{
    RT_NOREF_PV(pvUser);
    PVBGLR3SHAREDFOLDERMAPPING pMapping1 = (PVBGLR3SHAREDFOLDERMAPPING)pvElement1;
    PVBGLR3SHAREDFOLDERMAPPING pMapping2 = (PVBGLR3SHAREDFOLDERMAPPING)pvElement2;
    return pMapping1->u32Root < pMapping2->u32Root ? -1 : pMapping1->u32Root != pMapping2->u32Root ? 1 : 0;
}


/**
 * Refreshes the mount table.
 *
 * @returns true if we've processed the current config, false if we failed to
 *          query the mappings.
 * @param   pTable          The mount table to refresh.
 */
static bool vbsvcAutomounterRefreshTable(PVBSVCAUTOMOUNTERTABLE pTable)
{
    /*
     * Query the root IDs of all auto-mountable shared folder mappings.
     */
    uint32_t                    cMappings = 0;
    PVBGLR3SHAREDFOLDERMAPPING  paMappings = NULL;
    int rc = VbglR3SharedFolderGetMappings(g_idClientSharedFolders, true /*fAutoMountOnly*/, &paMappings, &cMappings);
    if (RT_FAILURE(rc))
    {
        VGSvcError("vbsvcAutomounterRefreshTable: VbglR3SharedFolderGetMappings failed: %Rrc\n", rc);
        return false;
    }

    /*
     * Walk the table and the mappings in parallel, so we have to make sure
     * they are both sorted by root ID.
     */
    if (cMappings > 1)
        RTSortShell(paMappings, cMappings, sizeof(paMappings[0]), vbsvcSharedFolderMappingCompare, NULL);

    /*
     * Pass #1: Do all the umounting.
     *
     * By doing the umount pass separately from the mount pass, we can
     * better handle changing involving the same mount points (switching
     * mount points between two shares, new share on same mount point but
     * with lower root ID, ++).
     */
    uint32_t iTable = 0;
    for (uint32_t iSrc = 0; iSrc < cMappings; iSrc++)
    {
        /*
         * Unmount table entries up to idRootSrc.
         */
        uint32_t const idRootSrc = paMappings[iSrc].u32Root;
        while (   iTable < pTable->cEntries
               && pTable->papEntries[iTable]->idRoot < idRootSrc)
            iTable = vbsvcAutomounterUnmountEntry(pTable, iTable, "dropped");

        /*
         * If the paMappings entry and the mount table entry has the same
         * root ID, umount if anything has changed or if we cannot query
         * the mapping data.
         */
        if (iTable < pTable->cEntries)
        {
            PVBSVCAUTOMOUNTERENTRY pEntry = pTable->papEntries[iTable];
            if (pEntry->idRoot == idRootSrc)
            {
                uint32_t uRootIdVer = UINT32_MAX;
                uint64_t fFlags     = 0;
                char    *pszName    = NULL;
                char    *pszMntPt   = NULL;
                rc = VbglR3SharedFolderQueryFolderInfo(g_idClientSharedFolders, idRootSrc, VBOXSERVICE_AUTOMOUNT_MIQF,
                                                       &pszName, &pszMntPt, &fFlags, &uRootIdVer);
                if (RT_FAILURE(rc))
                    iTable = vbsvcAutomounterUnmountEntry(pTable, iTable, "VbglR3SharedFolderQueryFolderInfo failed");
                else if (pEntry->uRootIdVersion != uRootIdVer)
                    iTable = vbsvcAutomounterUnmountEntry(pTable, iTable, "root ID version changed");
                else if (RTPathCompare(pEntry->pszMountPoint, pszMntPt) != 0)
                    iTable = vbsvcAutomounterUnmountEntry(pTable, iTable, "mount point changed");
                else if (RTStrICmp(pEntry->pszName, pszName) != 0)
                    iTable = vbsvcAutomounterUnmountEntry(pTable, iTable, "name changed");
                else
                {
                    VGSvcVerbose(3, "vbsvcAutomounterRefreshTable: Unchanged: %s -> %s\n", pEntry->pszMountPoint, pEntry->pszName);
                    iTable++;
                }
                if (RT_SUCCESS(rc))
                {
                    RTStrFree(pszName);
                    RTStrFree(pszMntPt);
                }
            }
        }
    }

    while (iTable < pTable->cEntries)
        iTable = vbsvcAutomounterUnmountEntry(pTable, iTable, "dropped (tail)");

    VGSvcVerbose(4, "vbsvcAutomounterRefreshTable: %u entries in mount table after pass #1.\n", pTable->cEntries);

    /*
     * Pass #2: Try mount new folders that has mount points assigned.
     * Pass #3: Try mount new folders not mounted in pass #2.
     */
    for (uint32_t iPass = 2; iPass <= 3; iPass++)
    {
        iTable = 0;
        for (uint32_t iSrc = 0; iSrc < cMappings; iSrc++)
        {
            uint32_t const idRootSrc = paMappings[iSrc].u32Root;

            /*
             * Skip tabel entries we couldn't umount in pass #1.
             */
            while (   iTable < pTable->cEntries
                   && pTable->papEntries[iTable]->idRoot < idRootSrc)
            {
                VGSvcVerbose(4, "vbsvcAutomounterRefreshTable: %u/#%u/%#u: Skipping idRoot=%u %s\n",
                             iPass, iSrc, iTable, pTable->papEntries[iTable]->idRoot, pTable->papEntries[iTable]->pszName);
                iTable++;
            }

            /*
             * New share?
             */
            if (   iTable >= pTable->cEntries
                || pTable->papEntries[iTable]->idRoot != idRootSrc)
            {
                uint32_t uRootIdVer = UINT32_MAX;
                uint64_t fFlags     = 0;
                char    *pszName    = NULL;
                char    *pszMntPt   = NULL;
                rc = VbglR3SharedFolderQueryFolderInfo(g_idClientSharedFolders, idRootSrc, VBOXSERVICE_AUTOMOUNT_MIQF,
                                                       &pszName, &pszMntPt, &fFlags, &uRootIdVer);
                if (RT_SUCCESS(rc))
                {
                    VGSvcVerbose(4, "vbsvcAutomounterRefreshTable: %u/#%u/%#u: Mounting idRoot=%u/%u %s\n", iPass, iSrc, iTable,
                                 idRootSrc, iTable >= pTable->cEntries ? UINT32_MAX : pTable->papEntries[iTable]->idRoot, pszName);
                    iTable = vbsvcAutomounterMountNewEntry(pTable, iTable, pszName, pszMntPt, fFlags,
                                                           idRootSrc, uRootIdVer, iPass == 3);

                    RTStrFree(pszName);
                    RTStrFree(pszMntPt);
                }
                else
                    VGSvcVerbose(1, "vbsvcAutomounterRefreshTable: VbglR3SharedFolderQueryFolderInfo failed: %Rrc\n", rc);
            }
            else
                VGSvcVerbose(4, "vbsvcAutomounterRefreshTable: %u/#%u/%#u: idRootSrc=%u vs idRoot=%u %s\n", iPass, iSrc,
                             iTable, idRootSrc, pTable->papEntries[iTable]->idRoot, pTable->papEntries[iTable]->pszName);
        }
    }

    VbglR3SharedFolderFreeMappings(paMappings);
    return true;
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnWorker}
 */
static DECLCALLBACK(int) vbsvcAutomounterWorker(bool volatile *pfShutdown)
{
    /*
     * Tell the control thread that it can continue spawning services.
     */
    RTThreadUserSignal(RTThreadSelf());

    /* Divert old hosts to original auto-mount code. */
    if (!g_fHostSupportsWaitAndInfoQuery)
        return vbsvcAutoMountWorkerOld(pfShutdown);

    /*
     * Initialize the state in case we're restarted...
     */
    VBSVCAUTOMOUNTERTABLE MountTable  = { 0, 0, NULL };
    int rc = vbsvcAutomounterPopulateTable(&MountTable);
    if (RT_FAILURE(rc))
    {
        VGSvcError("vbsvcAutomounterWorker: vbsvcAutomounterPopulateTable failed (%Rrc), quitting!\n", rc);
        return rc;
    }

    /*
     * Work loop.
     */
    uint32_t uConfigVer    = UINT32_MAX;
    uint32_t uNewVersion   = 0;
    bool     fForceRefresh = true;
    while (!*pfShutdown)
    {
        /*
         * Update the mounts.
         */
        if (   uConfigVer != uNewVersion
            || fForceRefresh)
        {
            fForceRefresh = !vbsvcAutomounterRefreshTable(&MountTable);
            uConfigVer    = uNewVersion;
        }

        /*
         * Wait for more to do.
         */
        if (!*pfShutdown)
        {
            uNewVersion = uConfigVer - 1;
            VGSvcVerbose(2, "vbsvcAutomounterWorker: Waiting with uConfigVer=%u\n", uConfigVer);
            rc = VbglR3SharedFolderWaitForMappingsChanges(g_idClientSharedFolders, uConfigVer, &uNewVersion);
            VGSvcVerbose(2, "vbsvcAutomounterWorker: Woke up with uNewVersion=%u and rc=%Rrc\n", uNewVersion, rc);

            /* Delay a little before doing a table refresh so the GUI can finish
               all its updates.  Delay a little longer on non-shutdown failure to
               avoid eating too many CPU cycles if something goes wrong here... */
            if (!*pfShutdown)
                RTSemEventMultiWait(g_hAutoMountEvent, RT_SUCCESS(rc) ? 256 : 1000);
        }
    }

    /*
     * Destroy the mount table.
     */
    while (MountTable.cEntries-- > 0)
        RTMemFree(MountTable.papEntries[MountTable.cEntries]);
    MountTable.papEntries = NULL;

    VGSvcVerbose(3, "vbsvcAutomounterWorker: Finished\n");
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnStop}
 */
static DECLCALLBACK(void) vbsvcAutomounterStop(void)
{
    RTSemEventMultiSignal(g_hAutoMountEvent);
    if (g_fHostSupportsWaitAndInfoQuery)
        VbglR3SharedFolderCancelMappingsChangesWaits(g_idClientSharedFolders);
}


/**
 * @interface_method_impl{VBOXSERVICE,pfnTerm}
 */
static DECLCALLBACK(void) vbsvcAutomounterTerm(void)
{
    VGSvcVerbose(3, "vbsvcAutoMountTerm\n");

    if (g_fHostSupportsWaitAndInfoQuery)
        VbglR3SharedFolderCancelMappingsChangesWaits(g_idClientSharedFolders);

    VbglR3SharedFolderDisconnect(g_idClientSharedFolders);
    g_idClientSharedFolders = 0;

    if (g_hAutoMountEvent != NIL_RTSEMEVENTMULTI)
    {
        RTSemEventMultiDestroy(g_hAutoMountEvent);
        g_hAutoMountEvent = NIL_RTSEMEVENTMULTI;
    }
}


/**
 * The 'automount' service description.
 */
VBOXSERVICE g_AutoMount =
{
    /* pszName. */
    "automount",
    /* pszDescription. */
    "Automounter for Shared Folders",
    /* pszUsage. */
    NULL,
    /* pszOptions. */
    NULL,
    /* methods */
    VGSvcDefaultPreInit,
    VGSvcDefaultOption,
    vbsvcAutomounterInit,
    vbsvcAutomounterWorker,
    vbsvcAutomounterStop,
    vbsvcAutomounterTerm
};

