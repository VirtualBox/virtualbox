/* $Id: VBoxSharedFoldersSvc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Shared Folders - Host service entry points.
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
#include <VBox/shflsvc.h>

#include "shfl.h"
#include "mappings.h"
#include "shflhandle.h"
#include "vbsf.h"
#include <iprt/alloc.h>
#include <iprt/string.h>
#include <iprt/assert.h>
#include <VBox/AssertGuest.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/pdmifs.h>
#include <VBox/vmm/vmmr3vtable.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define SHFL_SAVED_STATE_VERSION_FOLDERNAME_UTF16       2
#define SHFL_SAVED_STATE_VERSION_PRE_AUTO_MOUNT_POINT   3
#define SHFL_SAVED_STATE_VERSION_PRE_ERROR_STYLE        4
#define SHFL_SAVED_STATE_VERSION                        5


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
PVBOXHGCMSVCHELPERS g_pHelpers;
static PPDMLED      g_pStatusLed = NULL;

/** @name Shared folder statistics.
 * @{ */
static STAMPROFILE g_StatQueryMappings;
static STAMPROFILE g_StatQueryMappingsFail;
static STAMPROFILE g_StatQueryMapName;
static STAMPROFILE g_StatCreate;
static STAMPROFILE g_StatCreateFail;
static STAMPROFILE g_StatLookup;
static STAMPROFILE g_StatLookupFail;
static STAMPROFILE g_StatClose;
static STAMPROFILE g_StatCloseFail;
static STAMPROFILE g_StatRead;
static STAMPROFILE g_StatReadFail;
static STAMPROFILE g_StatWrite;
static STAMPROFILE g_StatWriteFail;
static STAMPROFILE g_StatLock;
static STAMPROFILE g_StatLockFail;
static STAMPROFILE g_StatList;
static STAMPROFILE g_StatListFail;
static STAMPROFILE g_StatReadLink;
static STAMPROFILE g_StatReadLinkFail;
static STAMPROFILE g_StatMapFolderOld;
static STAMPROFILE g_StatMapFolder;
static STAMPROFILE g_StatMapFolderFail;
static STAMPROFILE g_StatUnmapFolder;
static STAMPROFILE g_StatUnmapFolderFail;
static STAMPROFILE g_StatInformationFail;
static STAMPROFILE g_StatInformationSetFile;
static STAMPROFILE g_StatInformationSetFileFail;
static STAMPROFILE g_StatInformationSetSize;
static STAMPROFILE g_StatInformationSetSizeFail;
static STAMPROFILE g_StatInformationGetFile;
static STAMPROFILE g_StatInformationGetFileFail;
static STAMPROFILE g_StatInformationGetVolume;
static STAMPROFILE g_StatInformationGetVolumeFail;
static STAMPROFILE g_StatRemove;
static STAMPROFILE g_StatRemoveFail;
static STAMPROFILE g_StatCloseAndRemove;
static STAMPROFILE g_StatCloseAndRemoveFail;
static STAMPROFILE g_StatRename;
static STAMPROFILE g_StatRenameFail;
static STAMPROFILE g_StatFlush;
static STAMPROFILE g_StatFlushFail;
static STAMPROFILE g_StatSetErrorStyle;
static STAMPROFILE g_StatSetUtf8;
static STAMPROFILE g_StatSetFileSize;
static STAMPROFILE g_StatSetFileSizeFail;
static STAMPROFILE g_StatSymlink;
static STAMPROFILE g_StatSymlinkFail;
static STAMPROFILE g_StatSetSymlinks;
static STAMPROFILE g_StatQueryMapInfo;
static STAMPROFILE g_StatQueryFeatures;
static STAMPROFILE g_StatCopyFile;
static STAMPROFILE g_StatCopyFileFail;
static STAMPROFILE g_StatCopyFilePart;
static STAMPROFILE g_StatCopyFilePartFail;
static STAMPROFILE g_StatWaitForMappingsChanges;
static STAMPROFILE g_StatWaitForMappingsChangesFail;
static STAMPROFILE g_StatCancelMappingsChangesWait;
static STAMPROFILE g_StatUnknown;
static STAMPROFILE g_StatMsgStage1;
/** @} */


/** @page pg_shfl_svc   Shared Folders Host Service
 *
 * Shared Folders map a host file system to guest logical filesystem.
 * A mapping represents 'host name'<->'guest name' translation and a root
 * identifier to be used to access this mapping.
 * Examples: "C:\WINNT"<->"F:", "C:\WINNT\System32"<->"/mnt/host/system32".
 *
 * Therefore, host name and guest name are strings interpreted
 * only by host service and guest client respectively. Host name is
 * passed to guest only for informational purpose. Guest may for example
 * display the string or construct volume label out of the string.
 *
 * Root identifiers are unique for whole guest life,
 * that is until next guest reset/fresh start.
 * 32 bit value incremented for each new mapping is used.
 *
 * Mapping strings are taken from VM XML configuration on VM startup.
 * The service DLL takes mappings during initialization. There is
 * also API for changing mappings at runtime.
 *
 * Current mappings and root identifiers are saved when VM is saved.
 *
 * Guest may use any of these mappings. Full path information
 * about an object on a mapping consists of the root identifier and
 * a full path of object.
 *
 * Guest IFS connects to the service and calls SHFL_FN_QUERY_MAP
 * function which returns current mappings. For guest convenience,
 * removed mappings also returned with REMOVED flag and new mappings
 * are marked with NEW flag.
 *
 * To access host file system guest just forwards file system calls
 * to the service, and specifies full paths or handles for objects.
 *
 *
 */



static DECLCALLBACK(int) svcUnload(void *)
{
    int rc = VINF_SUCCESS;

    Log(("svcUnload\n"));
    vbsfFreeHandleTable();

    if (g_pHelpers)
        HGCMSvcHlpStamDeregister(g_pHelpers, "/HGCM/VBoxSharedFolders/*");
    return rc;
}

static DECLCALLBACK(int) svcConnect(void *, uint32_t u32ClientID, void *pvClient, uint32_t fRequestor, bool fRestoring)
{
    RT_NOREF(u32ClientID, fRequestor, fRestoring);
    SHFLCLIENTDATA *pClient = (SHFLCLIENTDATA *)pvClient;
    Log(("SharedFolders host service: connected, u32ClientID = %u\n", u32ClientID));

    pClient->fHasMappingCounts = true;
    pClient->enmErrorStyle = SHFLERRORSTYLE_NATIVE;
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) svcDisconnect(void *, uint32_t u32ClientID, void *pvClient)
{
    RT_NOREF1(u32ClientID);
    SHFLCLIENTDATA *pClient = (SHFLCLIENTDATA *)pvClient;

    /* When a client disconnects, make sure that outstanding change waits are being canceled.
     *
     * Usually this will be done actively by VBoxService on the guest side when shutting down,
     * but the VM could be reset without having VBoxService the chance of cancelling those waits.
     *
     * This in turn will eat up the call completion handle restrictions on the HGCM host side, throwing assertions. */
    int rc = vbsfMappingsCancelChangesWaits(pClient);

    Log(("SharedFolders host service: disconnected, u32ClientID = %u, rc = %Rrc\n", u32ClientID, rc));

    vbsfDisconnect(pClient);
    return rc;
}

/** @note We only save as much state as required to access the shared folder again after restore.
 *        All I/O requests pending at the time of saving will never be completed or result in errors.
 *        (file handles no longer valid etc)
 *        This works as designed at the moment. A full state save would be difficult and not always possible
 *        as the contents of a shared folder might change in between save and restore.
 */
static DECLCALLBACK(int) svcSaveState(void *, uint32_t u32ClientID, void *pvClient, PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM)
{
#ifndef UNITTEST  /* Read this as not yet tested */
    RT_NOREF1(u32ClientID);
    SHFLCLIENTDATA *pClient = (SHFLCLIENTDATA *)pvClient;

    Log(("SharedFolders host service: saving state, u32ClientID = %u\n", u32ClientID));

    int rc = pVMM->pfnSSMR3PutU32(pSSM, SHFL_SAVED_STATE_VERSION);
    AssertRCReturn(rc, rc);

    rc = pVMM->pfnSSMR3PutU32(pSSM, SHFL_MAX_MAPPINGS);
    AssertRCReturn(rc, rc);

    /* Save client structure length & contents */
    rc = pVMM->pfnSSMR3PutU32(pSSM, sizeof(*pClient));
    AssertRCReturn(rc, rc);

    rc = pVMM->pfnSSMR3PutMem(pSSM, pClient, sizeof(*pClient));
    AssertRCReturn(rc, rc);

    /* Save all the active mappings. */
    for (int i=0;i<SHFL_MAX_MAPPINGS;i++)
    {
        /* Mapping are saved in the order of increasing root handle values. */
        MAPPING *pFolderMapping = vbsfMappingGetByRoot(i);

        rc = pVMM->pfnSSMR3PutU32(pSSM, pFolderMapping? pFolderMapping->cMappings: 0);
        AssertRCReturn(rc, rc);

        rc = pVMM->pfnSSMR3PutBool(pSSM, pFolderMapping? pFolderMapping->fValid: false);
        AssertRCReturn(rc, rc);

        if (pFolderMapping && pFolderMapping->fValid)
        {
            uint32_t len = (uint32_t)strlen(pFolderMapping->pszFolderName);
            pVMM->pfnSSMR3PutU32(pSSM, len);
            pVMM->pfnSSMR3PutStrZ(pSSM, pFolderMapping->pszFolderName);

            len = ShflStringSizeOfBuffer(pFolderMapping->pMapName);
            pVMM->pfnSSMR3PutU32(pSSM, len);
            pVMM->pfnSSMR3PutMem(pSSM, pFolderMapping->pMapName, len);

            pVMM->pfnSSMR3PutBool(pSSM, pFolderMapping->fHostCaseSensitive);

            pVMM->pfnSSMR3PutBool(pSSM, pFolderMapping->fGuestCaseSensitive);

            len = ShflStringSizeOfBuffer(pFolderMapping->pAutoMountPoint);
            pVMM->pfnSSMR3PutU32(pSSM, len);
            rc = pVMM->pfnSSMR3PutMem(pSSM, pFolderMapping->pAutoMountPoint, len);
            AssertRCReturn(rc, rc);
        }
    }

#else
    RT_NOREF(u32ClientID, pvClient, pSSM, pVMM);
#endif
    return VINF_SUCCESS;
}

static DECLCALLBACK(int) svcLoadState(void *, uint32_t u32ClientID, void *pvClient,
                                      PSSMHANDLE pSSM, PCVMMR3VTABLE pVMM, uint32_t uVersion)
{
#ifndef UNITTEST  /* Read this as not yet tested */
    RT_NOREF(u32ClientID, uVersion);
    uint32_t        nrMappings;
    SHFLCLIENTDATA *pClient = (SHFLCLIENTDATA *)pvClient;
    uint32_t        len;

    Log(("SharedFolders host service: loading state, u32ClientID = %u\n", u32ClientID));

    uint32_t uShfVersion = 0;
    int rc = pVMM->pfnSSMR3GetU32(pSSM, &uShfVersion);
    AssertRCReturn(rc, rc);

    if (   uShfVersion > SHFL_SAVED_STATE_VERSION
        || uShfVersion < SHFL_SAVED_STATE_VERSION_FOLDERNAME_UTF16)
        return pVMM->pfnSSMR3SetLoadError(pSSM, VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION, RT_SRC_POS,
                                          "Unknown shared folders state version %u!", uShfVersion);

    rc = pVMM->pfnSSMR3GetU32(pSSM, &nrMappings);
    AssertRCReturn(rc, rc);
    if (nrMappings != SHFL_MAX_MAPPINGS)
        return VERR_SSM_DATA_UNIT_FORMAT_CHANGED;

    /* Restore the client data (flags + path delimiter + mapping counts (new) at the moment) */
    rc = pVMM->pfnSSMR3GetU32(pSSM, &len);
    AssertRCReturn(rc, rc);

    if (len == RT_UOFFSETOF(SHFLCLIENTDATA, acMappings))
        pClient->fHasMappingCounts = false;
    else if (len != sizeof(*pClient))
        return pVMM->pfnSSMR3SetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                          "Saved SHFLCLIENTDATA size %u differs from current %u!", len, sizeof(*pClient));

    rc = pVMM->pfnSSMR3GetMem(pSSM, pClient, len);
    AssertRCReturn(rc, rc);

    /* For older saved state, use the default native error style, otherwise
       check that the restored value makes sense to us. */
    if (uShfVersion <= SHFL_SAVED_STATE_VERSION_PRE_ERROR_STYLE)
        pClient->enmErrorStyle = SHFLERRORSTYLE_NATIVE;
    else if (   pClient->enmErrorStyle <= kShflErrorStyle_Invalid
             || pClient->enmErrorStyle >= kShflErrorStyle_End)
        return pVMM->pfnSSMR3SetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                          "Saved SHFLCLIENTDATA enmErrorStyle value %d is not known/valid!", pClient->enmErrorStyle);

    /* Drop the root IDs of all configured mappings before restoring: */
    vbsfMappingLoadingStart();

    /* We don't actually (fully) restore the state; we simply check if the current state is as we it expect it to be. */
    for (SHFLROOT i = 0; i < SHFL_MAX_MAPPINGS; i++)
    {
        /* Load the saved mapping description and try to find it in the mappings. */
        MAPPING mapping;
        RT_ZERO(mapping);

        /* restore the folder mapping counter. */
        rc = pVMM->pfnSSMR3GetU32(pSSM, &mapping.cMappings);
        AssertRCReturn(rc, rc);

        rc = pVMM->pfnSSMR3GetBool(pSSM, &mapping.fValid);
        AssertRCReturn(rc, rc);

        if (mapping.fValid)
        {
            /* Load the host path name. */
            uint32_t cb;
            rc = pVMM->pfnSSMR3GetU32(pSSM, &cb);
            AssertRCReturn(rc, rc);

            char *pszFolderName;
            if (uShfVersion == SHFL_SAVED_STATE_VERSION_FOLDERNAME_UTF16) /* (See version range check above.) */
            {
                AssertReturn(cb > SHFLSTRING_HEADER_SIZE && cb <= UINT16_MAX + SHFLSTRING_HEADER_SIZE && !(cb & 1),
                             pVMM->pfnSSMR3SetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                                        "Bad folder name size: %#x", cb));
                PSHFLSTRING pFolderName = (PSHFLSTRING)RTMemAlloc(cb);
                AssertReturn(pFolderName != NULL, VERR_NO_MEMORY);

                rc = pVMM->pfnSSMR3GetMem(pSSM, pFolderName, cb);
                AssertRCReturn(rc, rc);
                AssertReturn(pFolderName->u16Size < cb && pFolderName->u16Length < pFolderName->u16Size,
                             pVMM->pfnSSMR3SetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                                        "Bad folder name string: %#x/%#x cb=%#x",
                                                        pFolderName->u16Size, pFolderName->u16Length, cb));

                rc = RTUtf16ToUtf8(pFolderName->String.utf16, &pszFolderName);
                RTMemFree(pFolderName);
                AssertRCReturn(rc, rc);
            }
            else
            {
                pszFolderName = (char *)RTStrAlloc(cb + 1);
                AssertReturn(pszFolderName, VERR_NO_MEMORY);

                rc = pVMM->pfnSSMR3GetStrZ(pSSM, pszFolderName, cb + 1);
                AssertRCReturn(rc, rc);
                mapping.pszFolderName = pszFolderName;
            }

            /* Load the map name. */
            rc = pVMM->pfnSSMR3GetU32(pSSM, &cb);
            AssertRCReturn(rc, rc);
            AssertReturn(cb > SHFLSTRING_HEADER_SIZE && cb <= UINT16_MAX + SHFLSTRING_HEADER_SIZE && !(cb & 1),
                         pVMM->pfnSSMR3SetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                                    "Bad map name size: %#x", cb));

            PSHFLSTRING pMapName = (PSHFLSTRING)RTMemAlloc(cb);
            AssertReturn(pMapName != NULL, VERR_NO_MEMORY);

            rc = pVMM->pfnSSMR3GetMem(pSSM, pMapName, cb);
            AssertRCReturn(rc, rc);
            AssertReturn(pMapName->u16Size < cb && pMapName->u16Length < pMapName->u16Size,
                         pVMM->pfnSSMR3SetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                                    "Bad map name string: %#x/%#x cb=%#x",
                                                    pMapName->u16Size, pMapName->u16Length, cb));

            /* Load case sensitivity config. */
            rc = pVMM->pfnSSMR3GetBool(pSSM, &mapping.fHostCaseSensitive);
            AssertRCReturn(rc, rc);

            rc = pVMM->pfnSSMR3GetBool(pSSM, &mapping.fGuestCaseSensitive);
            AssertRCReturn(rc, rc);

            /* Load the auto mount point. */
            PSHFLSTRING pAutoMountPoint;
            if (uShfVersion > SHFL_SAVED_STATE_VERSION_PRE_AUTO_MOUNT_POINT)
            {
                rc = pVMM->pfnSSMR3GetU32(pSSM, &cb);
                AssertRCReturn(rc, rc);
                AssertReturn(cb > SHFLSTRING_HEADER_SIZE && cb <= UINT16_MAX + SHFLSTRING_HEADER_SIZE && !(cb & 1),
                             pVMM->pfnSSMR3SetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                                        "Bad auto mount point size: %#x", cb));

                pAutoMountPoint = (PSHFLSTRING)RTMemAlloc(cb);
                AssertReturn(pAutoMountPoint != NULL, VERR_NO_MEMORY);

                rc = pVMM->pfnSSMR3GetMem(pSSM, pAutoMountPoint, cb);
                AssertRCReturn(rc, rc);
                AssertReturn(pAutoMountPoint->u16Size < cb && pAutoMountPoint->u16Length < pAutoMountPoint->u16Size,
                             pVMM->pfnSSMR3SetLoadError(pSSM, VERR_SSM_DATA_UNIT_FORMAT_CHANGED, RT_SRC_POS,
                                                        "Bad auto mount point string: %#x/%#x cb=%#x",
                                                        pAutoMountPoint->u16Size, pAutoMountPoint->u16Length, cb));

            }
            else
            {
                pAutoMountPoint = ShflStringDupUtf8("");
                AssertReturn(pAutoMountPoint, VERR_NO_MEMORY);
            }

            mapping.pszFolderName = pszFolderName;
            mapping.pMapName = pMapName;
            mapping.pAutoMountPoint = pAutoMountPoint;

            /* 'i' is the root handle of the saved mapping. */
            rc = vbsfMappingLoaded(&mapping, i);
            if (RT_FAILURE(rc))
            {
                LogRel(("SharedFolders host service: %Rrc loading %d [%ls] -> [%s]\n",
                        rc, i, pMapName->String.utf16, pszFolderName));
            }

            RTMemFree(pAutoMountPoint);
            RTMemFree(pMapName);
            RTStrFree(pszFolderName);

            AssertRCReturn(rc, rc);
        }
    }

    /* Make sure all mappings have root IDs (global folders changes, VM
       config changes (paranoia)): */
    vbsfMappingLoadingDone();

    Log(("SharedFolders host service: successfully loaded state\n"));
#else
    RT_NOREF(u32ClientID, pvClient, pSSM, pVMM, uVersion);
#endif
    return VINF_SUCCESS;
}

static DECLCALLBACK(void) svcCall(void *, VBOXHGCMCALLHANDLE callHandle, uint32_t u32ClientID, void *pvClient,
                                  uint32_t u32Function, uint32_t cParms, VBOXHGCMSVCPARM paParms[], uint64_t tsArrival)
{
    RT_NOREF(u32ClientID, tsArrival);
#ifndef VBOX_WITHOUT_RELEASE_STATISTICS
    uint64_t tsStart;
    STAM_GET_TS(tsStart);
    STAM_REL_PROFILE_ADD_PERIOD(&g_StatMsgStage1, tsStart - tsArrival);
#endif
    Log(("SharedFolders host service: svcCall: u32ClientID = %u, fn = %u, cParms = %u, pparms = %p\n", u32ClientID, u32Function, cParms, paParms));

    SHFLCLIENTDATA *pClient = (SHFLCLIENTDATA *)pvClient;

    bool fAsynchronousProcessing = false;

#ifdef LOG_ENABLED
    for (uint32_t i = 0; i < cParms; i++)
    {
        /** @todo parameters other than 32 bit */
        Log(("    pparms[%d]: type %u, value %u\n", i, paParms[i].type, paParms[i].u.uint32));
    }
#endif

    int rc = VINF_SUCCESS;
    PSTAMPROFILE pStat, pStatFail;
    switch (u32Function)
    {
        case SHFL_FN_QUERY_MAPPINGS:
        {
            pStat     = &g_StatQueryMappings;
            pStatFail = &g_StatQueryMappingsFail;
            Log(("SharedFolders host service: svcCall: SHFL_FN_QUERY_MAPPINGS\n"));

            /* Verify parameter count and types. */
            if (cParms != SHFL_CPARMS_QUERY_MAPPINGS)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_32BIT   /* flags */
                     || paParms[1].type != VBOX_HGCM_SVC_PARM_32BIT   /* numberOfMappings */
                     || paParms[2].type != VBOX_HGCM_SVC_PARM_PTR     /* mappings */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                /* Fetch parameters. */
                uint32_t fu32Flags     = paParms[0].u.uint32;
                uint32_t cMappings     = paParms[1].u.uint32;
                SHFLMAPPING *pMappings = (SHFLMAPPING *)paParms[2].u.pointer.addr;
                uint32_t cbMappings    = paParms[2].u.pointer.size;

                /* Verify parameters values. */
                if (   (fu32Flags & ~SHFL_MF_MASK) != 0
                    || cbMappings / sizeof (SHFLMAPPING) != cMappings
                   )
                {
                    rc = VERR_INVALID_PARAMETER;
                }
                else
                {
                    /* Execute the function. */
                    if (fu32Flags & SHFL_MF_UTF8)
                        pClient->fu32Flags |= SHFL_CF_UTF8;
                    /// @todo r=bird: Someone please explain this amusing code (r63916):
                    //if (fu32Flags & SHFL_MF_AUTOMOUNT)
                    //    pClient->fu32Flags |= SHFL_MF_AUTOMOUNT;
                    //
                    //rc = vbsfMappingsQuery(pClient, pMappings, &cMappings);

                    rc = vbsfMappingsQuery(pClient, RT_BOOL(fu32Flags & SHFL_MF_AUTOMOUNT), pMappings, &cMappings);
                    if (RT_SUCCESS(rc))
                    {
                        /* Report that there are more mappings to get if
                         * handed in buffer is too small. */
                        if (paParms[1].u.uint32 < cMappings)
                            rc = VINF_BUFFER_OVERFLOW;

                        /* Update parameters. */
                        paParms[1].u.uint32 = cMappings;
                    }
                }
            }


        } break;

        case SHFL_FN_QUERY_MAP_NAME:
        {
            pStatFail = pStat = &g_StatQueryMapName;
            Log(("SharedFolders host service: svcCall: SHFL_FN_QUERY_MAP_NAME\n"));

            /* Verify parameter count and types. */
            if (cParms != SHFL_CPARMS_QUERY_MAP_NAME)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_32BIT /* Root. */
                     || paParms[1].type != VBOX_HGCM_SVC_PARM_PTR   /* Name. */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                /* Fetch parameters. */
                SHFLROOT  root         = (SHFLROOT)paParms[0].u.uint32;
                SHFLSTRING *pString    = (SHFLSTRING *)paParms[1].u.pointer.addr;

                /* Verify parameters values. */
                if (!ShflStringIsValidOut(pString, paParms[1].u.pointer.size))
                {
                    rc = VERR_INVALID_PARAMETER;
                }
                else
                {
                    /* Execute the function. */
                    rc = vbsfMappingsQueryName(pClient, root, pString);

                    if (RT_SUCCESS(rc))
                    {
                        /* Update parameters.*/
                        ; /* None. */
                    }
                }
            }

        } break;

        case SHFL_FN_CREATE:
        {
            pStat     = &g_StatCreate;
            pStatFail = &g_StatCreateFail;
            Log(("SharedFolders host service: svcCall: SHFL_FN_CREATE\n"));

            /* Verify parameter count and types. */
            if (cParms != SHFL_CPARMS_CREATE)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_32BIT /* root */
                     || paParms[1].type != VBOX_HGCM_SVC_PARM_PTR   /* path */
                     || paParms[2].type != VBOX_HGCM_SVC_PARM_PTR   /* parms */
                    )
            {
                Log(("SharedFolders host service: Invalid parameters types\n"));
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                /* Fetch parameters. */
                SHFLROOT  root          = (SHFLROOT)paParms[0].u.uint32;
                SHFLSTRING *pPath       = (SHFLSTRING *)paParms[1].u.pointer.addr;
                uint32_t cbPath         = paParms[1].u.pointer.size;
                SHFLCREATEPARMS *pParms = (SHFLCREATEPARMS *)paParms[2].u.pointer.addr;
                uint32_t cbParms        = paParms[2].u.pointer.size;

                /* Verify parameters values. */
                if (   !ShflStringIsValidIn(pPath, cbPath, RT_BOOL(pClient->fu32Flags & SHFL_CF_UTF8))
                    || (cbParms != sizeof (SHFLCREATEPARMS))
                   )
                {
                    AssertMsgFailed(("Invalid parameters cbPath or cbParms (%x, %x - expected >=%x, %x)\n",
                                      cbPath, cbParms, sizeof(SHFLSTRING), sizeof (SHFLCREATEPARMS)));
                    rc = VERR_INVALID_PARAMETER;
                }
                else
                {
                    if (pParms->CreateFlags & SHFL_CF_LOOKUP)
                    {
                        pStat     = &g_StatLookup;
                        pStatFail = &g_StatLookupFail;
                    }

                    /* Execute the function. */
                    rc = vbsfCreate(pClient, root, pPath, cbPath, pParms);

                    if (RT_SUCCESS(rc))
                    {
                        /* Update parameters.*/
                        ; /* none */
                    }
                }
            }
            break;
        }

        case SHFL_FN_CLOSE:
        {
            pStat     = &g_StatClose;
            pStatFail = &g_StatCloseFail;
            Log(("SharedFolders host service: svcCall: SHFL_FN_CLOSE\n"));

            /* Verify parameter count and types. */
            if (cParms != SHFL_CPARMS_CLOSE)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_32BIT   /* root */
                     || paParms[1].type != VBOX_HGCM_SVC_PARM_64BIT   /* handle */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                /* Fetch parameters. */
                SHFLROOT   root   = (SHFLROOT)paParms[0].u.uint32;
                SHFLHANDLE Handle = paParms[1].u.uint64;

                /* Verify parameters values. */
                if (Handle == SHFL_HANDLE_ROOT)
                {
                    rc = VERR_INVALID_PARAMETER;
                }
                else
                if (Handle == SHFL_HANDLE_NIL)
                {
                    AssertMsgFailed(("Invalid handle!\n"));
                    rc = VERR_INVALID_HANDLE;
                }
                else
                {
                    /* Execute the function. */
                    rc = vbsfClose(pClient, root, Handle);

                    if (RT_SUCCESS(rc))
                    {
                        /* Update parameters.*/
                        ; /* none */
                    }
                }
            }
            break;

        }

        /* Read object content. */
        case SHFL_FN_READ:
        {
            pStat     = &g_StatRead;
            pStatFail = &g_StatReadFail;
            Log(("SharedFolders host service: svcCall: SHFL_FN_READ\n"));
            /* Verify parameter count and types. */
            ASSERT_GUEST_STMT_BREAK(cParms == SHFL_CPARMS_READ, rc = VERR_WRONG_PARAMETER_COUNT);
            ASSERT_GUEST_STMT_BREAK(paParms[0].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* root */
            ASSERT_GUEST_STMT_BREAK(paParms[1].type == VBOX_HGCM_SVC_PARM_64BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* handle */
            ASSERT_GUEST_STMT_BREAK(paParms[2].type == VBOX_HGCM_SVC_PARM_64BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* offset */
            ASSERT_GUEST_STMT_BREAK(paParms[3].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* count */
            ASSERT_GUEST_STMT_BREAK(   paParms[4].type == VBOX_HGCM_SVC_PARM_PTR
                                    || paParms[4].type == VBOX_HGCM_SVC_PARM_PAGES, rc = VERR_WRONG_PARAMETER_TYPE); /* buffer */

            /* Fetch parameters. */
            SHFLROOT   const idRoot  = (SHFLROOT)paParms[0].u.uint32;
            SHFLHANDLE const hFile   = paParms[1].u.uint64;
            uint64_t   const offFile = paParms[2].u.uint64;
            uint32_t         cbRead  = paParms[3].u.uint32;

            /* Verify parameters values. */
            ASSERT_GUEST_STMT_BREAK(hFile != SHFL_HANDLE_ROOT, rc = VERR_INVALID_PARAMETER);
            ASSERT_GUEST_STMT_BREAK(hFile != SHFL_HANDLE_NIL,  rc = VERR_INVALID_HANDLE);
            if (paParms[4].type == VBOX_HGCM_SVC_PARM_PTR)
                ASSERT_GUEST_STMT_BREAK(cbRead <= paParms[4].u.pointer.size, rc = VERR_INVALID_HANDLE);
            else
                ASSERT_GUEST_STMT_BREAK(cbRead <= paParms[4].u.Pages.cb, rc = VERR_OUT_OF_RANGE);

            /* Execute the function. */
            if (g_pStatusLed)
            {
                Assert(g_pStatusLed->u32Magic == PDMLED_MAGIC);
                g_pStatusLed->Asserted.s.fReading = g_pStatusLed->Actual.s.fReading = 1;
            }

            if (paParms[4].type == VBOX_HGCM_SVC_PARM_PTR)
                rc = vbsfRead(pClient, idRoot, hFile, offFile, &cbRead, (uint8_t *)paParms[4].u.pointer.addr);
            else
                rc = vbsfReadPages(pClient, idRoot, hFile, offFile, &cbRead, &paParms[4].u.Pages);

            if (g_pStatusLed)
                g_pStatusLed->Actual.s.fReading = 0;

            /* Update parameters.*/
            paParms[3].u.uint32 = RT_SUCCESS(rc) ? cbRead : 0  /* nothing read */;
            break;
        }

        /* Write new object content. */
        case SHFL_FN_WRITE:
         {
            pStat     = &g_StatWrite;
            pStatFail = &g_StatWriteFail;
            Log(("SharedFolders host service: svcCall: SHFL_FN_WRITE\n"));

            /* Verify parameter count and types. */
            ASSERT_GUEST_STMT_BREAK(cParms == SHFL_CPARMS_WRITE, rc = VERR_WRONG_PARAMETER_COUNT);
            ASSERT_GUEST_STMT_BREAK(paParms[0].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* root */
            ASSERT_GUEST_STMT_BREAK(paParms[1].type == VBOX_HGCM_SVC_PARM_64BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* handle */
            ASSERT_GUEST_STMT_BREAK(paParms[2].type == VBOX_HGCM_SVC_PARM_64BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* offset */
            ASSERT_GUEST_STMT_BREAK(paParms[3].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* count */
            ASSERT_GUEST_STMT_BREAK(   paParms[4].type == VBOX_HGCM_SVC_PARM_PTR
                                    || paParms[4].type == VBOX_HGCM_SVC_PARM_PAGES, rc = VERR_WRONG_PARAMETER_TYPE); /* buffer */
            /* Fetch parameters. */
            SHFLROOT   const idRoot   = (SHFLROOT)paParms[0].u.uint32;
            SHFLHANDLE const hFile    = paParms[1].u.uint64;
            uint64_t         offFile  = paParms[2].u.uint64;
            uint32_t         cbWrite  = paParms[3].u.uint32;

            /* Verify parameters values. */
            ASSERT_GUEST_STMT_BREAK(hFile != SHFL_HANDLE_ROOT, rc = VERR_INVALID_PARAMETER);
            ASSERT_GUEST_STMT_BREAK(hFile != SHFL_HANDLE_NIL,  rc = VERR_INVALID_HANDLE);
            if (paParms[4].type == VBOX_HGCM_SVC_PARM_PTR)
                ASSERT_GUEST_STMT_BREAK(cbWrite <= paParms[4].u.pointer.size, rc = VERR_INVALID_HANDLE);
            else
                ASSERT_GUEST_STMT_BREAK(cbWrite <= paParms[4].u.Pages.cb, rc = VERR_OUT_OF_RANGE);

            /* Execute the function. */
            if (g_pStatusLed)
            {
                Assert(g_pStatusLed->u32Magic == PDMLED_MAGIC);
                g_pStatusLed->Asserted.s.fWriting = g_pStatusLed->Actual.s.fWriting = 1;
            }

            if (paParms[4].type == VBOX_HGCM_SVC_PARM_PTR)
                rc = vbsfWrite(pClient, idRoot, hFile, &offFile, &cbWrite, (uint8_t *)paParms[4].u.pointer.addr);
            else
                rc = vbsfWritePages(pClient, idRoot, hFile, &offFile, &cbWrite, &paParms[4].u.Pages);

            if (g_pStatusLed)
                g_pStatusLed->Actual.s.fWriting = 0;

            /* Update parameters.*/
            if (RT_SUCCESS(rc))
            {
                paParms[3].u.uint32 = cbWrite;
                paParms[2].u.uint64 = offFile;
            }
            else
                paParms[3].u.uint32 = 0;
            break;
        }

        /* Lock/unlock a range in the object. */
        case SHFL_FN_LOCK:
            pStat     = &g_StatLock;
            pStatFail = &g_StatLockFail;
            Log(("SharedFolders host service: svcCall: SHFL_FN_LOCK\n"));

            /* Verify parameter count and types. */
            if (cParms != SHFL_CPARMS_LOCK)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            if (   paParms[0].type != VBOX_HGCM_SVC_PARM_32BIT   /* root */
                || paParms[1].type != VBOX_HGCM_SVC_PARM_64BIT   /* handle */
                || paParms[2].type != VBOX_HGCM_SVC_PARM_64BIT   /* offset */
                || paParms[3].type != VBOX_HGCM_SVC_PARM_64BIT   /* length */
                || paParms[4].type != VBOX_HGCM_SVC_PARM_32BIT   /* flags */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                /* Fetch parameters. */
                SHFLROOT  root     = (SHFLROOT)paParms[0].u.uint32;
                SHFLHANDLE Handle  = paParms[1].u.uint64;
                uint64_t   offset  = paParms[2].u.uint64;
                uint64_t   length  = paParms[3].u.uint64;
                uint32_t   flags   = paParms[4].u.uint32;

                /* Verify parameters values. */
                if (Handle == SHFL_HANDLE_ROOT)
                {
                    rc = VERR_INVALID_PARAMETER;
                }
                else
                if (Handle == SHFL_HANDLE_NIL)
                {
                    AssertMsgFailed(("Invalid handle!\n"));
                    rc = VERR_INVALID_HANDLE;
                }
                else if (flags & SHFL_LOCK_WAIT)
                {
                    /** @todo This should be properly implemented by the shared folders service.
                     *       The service thread must never block. If an operation requires
                     *       blocking, it must be processed by another thread and when it is
                     *       completed, the another thread must call
                     *
                     *           g_pHelpers->pfnCallComplete(callHandle, rc);
                     *
                     * The operation is async.
                     * fAsynchronousProcessing = true;
                     */

                    /* Here the operation must be posted to another thread. At the moment it is not implemented.
                     * Until it is implemented, try to perform the operation without waiting.
                     */
                    flags &= ~SHFL_LOCK_WAIT;

                    /* Execute the function. */
                    if ((flags & SHFL_LOCK_MODE_MASK) == SHFL_LOCK_CANCEL)
                        rc = vbsfUnlock(pClient, root, Handle, offset, length, flags);
                    else
                        rc = vbsfLock(pClient, root, Handle, offset, length, flags);

                    if (RT_SUCCESS(rc))
                    {
                        /* Update parameters.*/
                        /* none */
                    }
                }
                else
                {
                    /* Execute the function. */
                    if ((flags & SHFL_LOCK_MODE_MASK) == SHFL_LOCK_CANCEL)
                        rc = vbsfUnlock(pClient, root, Handle, offset, length, flags);
                    else
                        rc = vbsfLock(pClient, root, Handle, offset, length, flags);

                    if (RT_SUCCESS(rc))
                    {
                        /* Update parameters.*/
                        /* none */
                    }
                }
            }
            break;

        /* List object content. */
        case SHFL_FN_LIST:
        {
            pStat     = &g_StatList;
            pStatFail = &g_StatListFail;
            Log(("SharedFolders host service: svcCall: SHFL_FN_LIST\n"));

            /* Verify parameter count and types. */
            if (cParms != SHFL_CPARMS_LIST)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            if (   paParms[0].type != VBOX_HGCM_SVC_PARM_32BIT   /* root */
                || paParms[1].type != VBOX_HGCM_SVC_PARM_64BIT   /* handle */
                || paParms[2].type != VBOX_HGCM_SVC_PARM_32BIT   /* flags */
                || paParms[3].type != VBOX_HGCM_SVC_PARM_32BIT   /* cb */
                || paParms[4].type != VBOX_HGCM_SVC_PARM_PTR     /* pPath */
                || paParms[5].type != VBOX_HGCM_SVC_PARM_PTR     /* buffer */
                || paParms[6].type != VBOX_HGCM_SVC_PARM_32BIT   /* resumePoint */
                || paParms[7].type != VBOX_HGCM_SVC_PARM_32BIT   /* cFiles (out) */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                /* Fetch parameters. */
                SHFLROOT  root     = (SHFLROOT)paParms[0].u.uint32;
                SHFLHANDLE Handle  = paParms[1].u.uint64;
                uint32_t   flags   = paParms[2].u.uint32;
                uint32_t   length  = paParms[3].u.uint32;
                SHFLSTRING *pPath  = (paParms[4].u.pointer.size == 0) ? 0 : (SHFLSTRING *)paParms[4].u.pointer.addr;
                uint8_t   *pBuffer = (uint8_t *)paParms[5].u.pointer.addr;
                uint32_t   resumePoint = paParms[6].u.uint32;
                uint32_t   cFiles = 0;

                /* Verify parameters values. */
                if (   (length < sizeof (SHFLDIRINFO))
                    ||  length > paParms[5].u.pointer.size
                    ||  !ShflStringIsValidOrNullIn(pPath, paParms[4].u.pointer.size, RT_BOOL(pClient->fu32Flags & SHFL_CF_UTF8))
                   )
                {
                    rc = VERR_INVALID_PARAMETER;
                }
                else
                {
                    if (g_pStatusLed)
                    {
                        Assert(g_pStatusLed->u32Magic == PDMLED_MAGIC);
                        g_pStatusLed->Asserted.s.fReading = g_pStatusLed->Actual.s.fReading = 1;
                    }

                    /* Execute the function. */
                    rc = vbsfDirList(pClient, root, Handle, pPath, flags, &length, pBuffer, &resumePoint, &cFiles);

                    if (g_pStatusLed)
                        g_pStatusLed->Actual.s.fReading = 0;

                    if (rc == VERR_NO_MORE_FILES && cFiles != 0)
                        rc = VINF_SUCCESS; /* Successfully return these files. */

                    if (RT_SUCCESS(rc))
                    {
                        /* Update parameters.*/
                        paParms[3].u.uint32 = length;
                        paParms[6].u.uint32 = resumePoint;
                        paParms[7].u.uint32 = cFiles;
                    }
                    else
                    {
                        paParms[3].u.uint32 = 0;  /* nothing read */
                        paParms[6].u.uint32 = 0;
                        paParms[7].u.uint32 = cFiles;
                    }
                }
            }
            break;
        }

        /* Read symlink destination */
        case SHFL_FN_READLINK:
        {
            pStat     = &g_StatReadLink;
            pStatFail = &g_StatReadLinkFail;
            Log(("SharedFolders host service: svcCall: SHFL_FN_READLINK\n"));

            /* Verify parameter count and types. */
            if (cParms != SHFL_CPARMS_READLINK)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            if (   paParms[0].type != VBOX_HGCM_SVC_PARM_32BIT   /* root */
                || paParms[1].type != VBOX_HGCM_SVC_PARM_PTR     /* path */
                || paParms[2].type != VBOX_HGCM_SVC_PARM_PTR     /* buffer */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                /* Fetch parameters. */
                SHFLROOT  root     = (SHFLROOT)paParms[0].u.uint32;
                SHFLSTRING *pPath  = (SHFLSTRING *)paParms[1].u.pointer.addr;
                uint32_t cbPath    = paParms[1].u.pointer.size;
                uint8_t   *pBuffer = (uint8_t *)paParms[2].u.pointer.addr;
                uint32_t  cbBuffer = paParms[2].u.pointer.size;

                /* Verify parameters values. */
                if (!ShflStringIsValidOrNullIn(pPath, paParms[1].u.pointer.size, RT_BOOL(pClient->fu32Flags & SHFL_CF_UTF8)))
                {
                    rc = VERR_INVALID_PARAMETER;
                }
                else
                {
                    /* Execute the function. */
                    rc = vbsfReadLink(pClient, root, pPath, cbPath, pBuffer, cbBuffer);

                    if (RT_SUCCESS(rc))
                    {
                        /* Update parameters.*/
                        ; /* none */
                    }
                }
            }

            break;
        }

        /* Legacy interface */
        case SHFL_FN_MAP_FOLDER_OLD:
        {
            pStatFail = pStat = &g_StatMapFolderOld;
            Log(("SharedFolders host service: svcCall: SHFL_FN_MAP_FOLDER_OLD\n"));

            /* Verify parameter count and types. */
            if (cParms != SHFL_CPARMS_MAP_FOLDER_OLD)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_PTR     /* path */
                     || paParms[1].type != VBOX_HGCM_SVC_PARM_32BIT   /* root */
                     || paParms[2].type != VBOX_HGCM_SVC_PARM_32BIT   /* delimiter */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                /* Fetch parameters. */
                PSHFLSTRING pszMapName = (PSHFLSTRING)paParms[0].u.pointer.addr;
                SHFLROOT    root       = (SHFLROOT)paParms[1].u.uint32;
                RTUTF16     delimiter  = (RTUTF16)paParms[2].u.uint32;

                /* Verify parameters values. */
                if (!ShflStringIsValidIn(pszMapName, paParms[0].u.pointer.size, RT_BOOL(pClient->fu32Flags & SHFL_CF_UTF8)))
                {
                    rc = VERR_INVALID_PARAMETER;
                }
                else
                {
                    /* Execute the function. */
                    rc = vbsfMapFolder(pClient, pszMapName, delimiter, false,  &root);

                    if (RT_SUCCESS(rc))
                    {
                        /* Update parameters.*/
                        paParms[1].u.uint32 = root;
                    }
                }
            }
            break;
        }

        case SHFL_FN_MAP_FOLDER:
        {
            pStat     = &g_StatMapFolder;
            pStatFail = &g_StatMapFolderFail;
            Log(("SharedFolders host service: svcCall: SHFL_FN_MAP_FOLDER\n"));
            if (BIT_FLAG(pClient->fu32Flags, SHFL_CF_UTF8))
                Log(("SharedFolders host service: request to map folder '%s'\n",
                     ((PSHFLSTRING)paParms[0].u.pointer.addr)->String.utf8));
            else
                Log(("SharedFolders host service: request to map folder '%ls'\n",
                     ((PSHFLSTRING)paParms[0].u.pointer.addr)->String.utf16));

            /* Verify parameter count and types. */
            if (cParms != SHFL_CPARMS_MAP_FOLDER)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_PTR     /* path */
                     || paParms[1].type != VBOX_HGCM_SVC_PARM_32BIT   /* root */
                     || paParms[2].type != VBOX_HGCM_SVC_PARM_32BIT   /* delimiter */
                     || paParms[3].type != VBOX_HGCM_SVC_PARM_32BIT   /* fCaseSensitive */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                /* Fetch parameters. */
                PSHFLSTRING pszMapName = (PSHFLSTRING)paParms[0].u.pointer.addr;
                SHFLROOT    root       = (SHFLROOT)paParms[1].u.uint32;
                RTUTF16     delimiter  = (RTUTF16)paParms[2].u.uint32;
                bool        fCaseSensitive = !!paParms[3].u.uint32;

                /* Verify parameters values. */
                if (ShflStringIsValidIn(pszMapName, paParms[0].u.pointer.size, RT_BOOL(pClient->fu32Flags & SHFL_CF_UTF8)))
                {
                    rc = VINF_SUCCESS;
                }
                else
                {
                    rc = VERR_INVALID_PARAMETER;

                    /* Fudge for windows GAs getting the length wrong by one char. */
                    if (   !(pClient->fu32Flags & SHFL_CF_UTF8)
                        && paParms[0].u.pointer.size >= sizeof(SHFLSTRING)
                        && pszMapName->u16Length >= 2
                        && pszMapName->String.utf16[pszMapName->u16Length / 2 - 1] == 0x0000)
                    {
                        pszMapName->u16Length -= 2;
                        if (ShflStringIsValidIn(pszMapName, paParms[0].u.pointer.size, false /*fUtf8Not16*/))
                            rc = VINF_SUCCESS;
                        else
                            pszMapName->u16Length += 2;
                    }
                }

                /* Execute the function. */
                if (RT_SUCCESS(rc))
                    rc = vbsfMapFolder(pClient, pszMapName, delimiter, fCaseSensitive, &root);

                if (RT_SUCCESS(rc))
                {
                    /* Update parameters.*/
                    paParms[1].u.uint32 = root;
                }
            }
            Log(("SharedFolders host service: map operation result %Rrc\n", rc));
            if (RT_SUCCESS(rc))
                Log(("SharedFolders host service: mapped to handle %d\n", paParms[1].u.uint32));
            break;
        }

        case SHFL_FN_UNMAP_FOLDER:
        {
            pStat     = &g_StatUnmapFolder;
            pStatFail = &g_StatUnmapFolderFail;
            Log(("SharedFolders host service: svcCall: SHFL_FN_UNMAP_FOLDER\n"));
            Log(("SharedFolders host service: request to unmap folder handle %u\n",
                 paParms[0].u.uint32));

            /* Verify parameter count and types. */
            if (cParms != SHFL_CPARMS_UNMAP_FOLDER)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if ( paParms[0].type != VBOX_HGCM_SVC_PARM_32BIT   /* root */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                /* Fetch parameters. */
                SHFLROOT    root       = (SHFLROOT)paParms[0].u.uint32;

                /* Execute the function. */
                rc = vbsfUnmapFolder(pClient, root);

                if (RT_SUCCESS(rc))
                {
                    /* Update parameters.*/
                    /* nothing */
                }
            }
            Log(("SharedFolders host service: unmap operation result %Rrc\n", rc));
            break;
        }

        /* Query/set object information. */
        case SHFL_FN_INFORMATION:
        {
            pStatFail = pStat = &g_StatInformationFail;
            Log(("SharedFolders host service: svcCall: SHFL_FN_INFORMATION\n"));

            /* Verify parameter count and types. */
            if (cParms != SHFL_CPARMS_INFORMATION)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            if (   paParms[0].type != VBOX_HGCM_SVC_PARM_32BIT   /* root */
                || paParms[1].type != VBOX_HGCM_SVC_PARM_64BIT   /* handle */
                || paParms[2].type != VBOX_HGCM_SVC_PARM_32BIT   /* flags */
                || paParms[3].type != VBOX_HGCM_SVC_PARM_32BIT   /* cb */
                || paParms[4].type != VBOX_HGCM_SVC_PARM_PTR     /* buffer */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                /* Fetch parameters. */
                SHFLROOT  root     = (SHFLROOT)paParms[0].u.uint32;
                SHFLHANDLE Handle  = paParms[1].u.uint64;
                uint32_t   flags   = paParms[2].u.uint32;
                uint32_t   length  = paParms[3].u.uint32;
                uint8_t   *pBuffer = (uint8_t *)paParms[4].u.pointer.addr;

                /* Verify parameters values. */
                if (length > paParms[4].u.pointer.size)
                {
                    rc = VERR_INVALID_PARAMETER;
                }
                else
                {
                    /* Execute the function. */
                    if (flags & SHFL_INFO_SET)
                    {
                        rc = vbsfSetFSInfo(pClient, root, Handle, flags, &length, pBuffer);

                        if (flags & SHFL_INFO_FILE)
                        {
                            pStat     = &g_StatInformationSetFile;
                            pStatFail = &g_StatInformationSetFileFail;
                        }
                        else if (flags & SHFL_INFO_SIZE)
                        {
                            pStat     = &g_StatInformationSetSize;
                            pStatFail = &g_StatInformationSetSizeFail;
                        }
                    }
                    else /* SHFL_INFO_GET */
                    {
                        rc = vbsfQueryFSInfo(pClient, root, Handle, flags, &length, pBuffer);

                        if (flags & SHFL_INFO_FILE)
                        {
                            pStat     = &g_StatInformationGetFile;
                            pStatFail = &g_StatInformationGetFileFail;
                        }
                        else if (flags & SHFL_INFO_VOLUME)
                        {
                            pStat     = &g_StatInformationGetVolume;
                            pStatFail = &g_StatInformationGetVolumeFail;
                        }
                    }

                    if (RT_SUCCESS(rc))
                    {
                        /* Update parameters.*/
                        paParms[3].u.uint32 = length;
                    }
                    else
                    {
                        paParms[3].u.uint32 = 0;  /* nothing read */
                    }
                }
            }
            break;
        }

        /* Remove or rename object */
        case SHFL_FN_REMOVE:
        {
            pStat     = &g_StatRemove;
            pStatFail = &g_StatRemoveFail;
            Log(("SharedFolders host service: svcCall: SHFL_FN_REMOVE\n"));

            /* Verify parameter count and types. */
            ASSERT_GUEST_STMT_BREAK(cParms == SHFL_CPARMS_REMOVE, rc = VERR_WRONG_PARAMETER_COUNT);
            ASSERT_GUEST_STMT_BREAK(paParms[0].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* root */
            ASSERT_GUEST_STMT_BREAK(paParms[1].type == VBOX_HGCM_SVC_PARM_PTR,   rc = VERR_WRONG_PARAMETER_TYPE); /* path */
            PCSHFLSTRING pStrPath = (PCSHFLSTRING)paParms[1].u.pointer.addr;
            ASSERT_GUEST_STMT_BREAK(ShflStringIsValidIn(pStrPath, paParms[1].u.pointer.size,
                                                        RT_BOOL(pClient->fu32Flags & SHFL_CF_UTF8)),
                                    rc = VERR_INVALID_PARAMETER);
            ASSERT_GUEST_STMT_BREAK(paParms[2].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* flags */
            uint32_t const fFlags = paParms[2].u.uint32;
            ASSERT_GUEST_STMT_BREAK(!(fFlags & ~(SHFL_REMOVE_FILE | SHFL_REMOVE_DIR | SHFL_REMOVE_SYMLINK)),
                                    rc = VERR_INVALID_FLAGS);

            /* Execute the function. */
            rc = vbsfRemove(pClient, paParms[0].u.uint32, pStrPath, paParms[1].u.pointer.size, fFlags, SHFL_HANDLE_NIL);
            break;
        }

        case SHFL_FN_CLOSE_AND_REMOVE:
        {
            pStat     = &g_StatCloseAndRemove;
            pStatFail = &g_StatCloseAndRemoveFail;
            Log(("SharedFolders host service: svcCall: SHFL_FN_CLOSE_AND_REMOVE\n"));

            /* Verify parameter count and types. */
            ASSERT_GUEST_STMT_BREAK(cParms == SHFL_CPARMS_CLOSE_AND_REMOVE, rc = VERR_WRONG_PARAMETER_COUNT);
            ASSERT_GUEST_STMT_BREAK(paParms[0].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* root */
            ASSERT_GUEST_STMT_BREAK(paParms[1].type == VBOX_HGCM_SVC_PARM_PTR,   rc = VERR_WRONG_PARAMETER_TYPE); /* path */
            PCSHFLSTRING pStrPath = (PCSHFLSTRING)paParms[1].u.pointer.addr;
            ASSERT_GUEST_STMT_BREAK(ShflStringIsValidIn(pStrPath, paParms[1].u.pointer.size,
                                                        RT_BOOL(pClient->fu32Flags & SHFL_CF_UTF8)),
                                    rc = VERR_INVALID_PARAMETER);
            ASSERT_GUEST_STMT_BREAK(paParms[2].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* flags */
            uint32_t const fFlags = paParms[2].u.uint32;
            ASSERT_GUEST_STMT_BREAK(!(fFlags & ~(SHFL_REMOVE_FILE | SHFL_REMOVE_DIR | SHFL_REMOVE_SYMLINK)),
                                    rc = VERR_INVALID_FLAGS);
            SHFLHANDLE const hToClose = paParms[3].u.uint64;
            ASSERT_GUEST_STMT_BREAK(hToClose != SHFL_HANDLE_ROOT, rc = VERR_INVALID_HANDLE);

            /* Execute the function. */
            rc = vbsfRemove(pClient, paParms[0].u.uint32, pStrPath, paParms[1].u.pointer.size, fFlags, hToClose);
            break;
        }

        case SHFL_FN_RENAME:
        {
            pStat     = &g_StatRename;
            pStatFail = &g_StatRenameFail;
            Log(("SharedFolders host service: svcCall: SHFL_FN_RENAME\n"));

            /* Verify parameter count and types. */
            if (cParms != SHFL_CPARMS_RENAME)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_32BIT   /* root */
                     || paParms[1].type != VBOX_HGCM_SVC_PARM_PTR   /* src */
                     || paParms[2].type != VBOX_HGCM_SVC_PARM_PTR   /* dest */
                     || paParms[3].type != VBOX_HGCM_SVC_PARM_32BIT /* flags */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                /* Fetch parameters. */
                SHFLROOT    root        = (SHFLROOT)paParms[0].u.uint32;
                SHFLSTRING *pSrc        = (SHFLSTRING *)paParms[1].u.pointer.addr;
                SHFLSTRING *pDest       = (SHFLSTRING *)paParms[2].u.pointer.addr;
                uint32_t    flags       = paParms[3].u.uint32;

                /* Verify parameters values. */
                if (    !ShflStringIsValidIn(pSrc, paParms[1].u.pointer.size, RT_BOOL(pClient->fu32Flags & SHFL_CF_UTF8))
                    ||  !ShflStringIsValidIn(pDest, paParms[2].u.pointer.size, RT_BOOL(pClient->fu32Flags & SHFL_CF_UTF8))
                   )
                {
                    rc = VERR_INVALID_PARAMETER;
                }
                else
                {
                    /* Execute the function. */
                    rc = vbsfRename(pClient, root, pSrc, pDest, flags);
                    if (RT_SUCCESS(rc))
                    {
                        /* Update parameters.*/
                        ; /* none */
                    }
                }
            }
            break;
        }

        case SHFL_FN_FLUSH:
        {
            pStat     = &g_StatFlush;
            pStatFail = &g_StatFlushFail;
            Log(("SharedFolders host service: svcCall: SHFL_FN_FLUSH\n"));

            /* Verify parameter count and types. */
            if (cParms != SHFL_CPARMS_FLUSH)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            if (   paParms[0].type != VBOX_HGCM_SVC_PARM_32BIT   /* root */
                || paParms[1].type != VBOX_HGCM_SVC_PARM_64BIT   /* handle */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                /* Fetch parameters. */
                SHFLROOT   root    = (SHFLROOT)paParms[0].u.uint32;
                SHFLHANDLE Handle  = paParms[1].u.uint64;

                /* Verify parameters values. */
                if (Handle == SHFL_HANDLE_ROOT)
                {
                    rc = VERR_INVALID_PARAMETER;
                }
                else
                if (Handle == SHFL_HANDLE_NIL)
                {
                    AssertMsgFailed(("Invalid handle!\n"));
                    rc = VERR_INVALID_HANDLE;
                }
                else
                {
                    /* Execute the function. */

                    rc = vbsfFlush(pClient, root, Handle);

                    if (RT_SUCCESS(rc))
                    {
                        /* Nothing to do */
                    }
                }
            }
        } break;

        case SHFL_FN_SET_UTF8:
        {
            pStatFail = pStat = &g_StatSetUtf8;

            pClient->fu32Flags |= SHFL_CF_UTF8;
            rc = VINF_SUCCESS;
            break;
        }

        case SHFL_FN_SYMLINK:
        {
            pStat     = &g_StatSymlink;
            pStatFail = &g_StatSymlinkFail;
            Log(("SharedFolders host service: svnCall: SHFL_FN_SYMLINK\n"));

            /* Verify parameter count and types. */
            if (cParms != SHFL_CPARMS_SYMLINK)
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_32BIT   /* root */
                     || paParms[1].type != VBOX_HGCM_SVC_PARM_PTR     /* newPath */
                     || paParms[2].type != VBOX_HGCM_SVC_PARM_PTR     /* oldPath */
                     || paParms[3].type != VBOX_HGCM_SVC_PARM_PTR     /* info */
                    )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                /* Fetch parameters. */
                SHFLROOT     root     = (SHFLROOT)paParms[0].u.uint32;
                SHFLSTRING  *pNewPath = (SHFLSTRING *)paParms[1].u.pointer.addr;
                SHFLSTRING  *pOldPath = (SHFLSTRING *)paParms[2].u.pointer.addr;
                SHFLFSOBJINFO *pInfo  = (SHFLFSOBJINFO *)paParms[3].u.pointer.addr;
                uint32_t     cbInfo   = paParms[3].u.pointer.size;

                /* Verify parameters values. */
                if (    !ShflStringIsValidIn(pNewPath, paParms[1].u.pointer.size, RT_BOOL(pClient->fu32Flags & SHFL_CF_UTF8))
                    ||  !ShflStringIsValidIn(pOldPath, paParms[2].u.pointer.size, RT_BOOL(pClient->fu32Flags & SHFL_CF_UTF8))
                    ||  (cbInfo != sizeof(SHFLFSOBJINFO))
                   )
                {
                    rc = VERR_INVALID_PARAMETER;
                }
                else
                {
                    /* Execute the function. */
                    rc = vbsfSymlink(pClient, root, pNewPath, pOldPath, pInfo);
                    if (RT_SUCCESS(rc))
                    {
                        /* Update parameters.*/
                        ; /* none */
                    }
                }
            }
        }
        break;

        case SHFL_FN_SET_SYMLINKS:
        {
            pStatFail = pStat = &g_StatSetSymlinks;

            pClient->fu32Flags |= SHFL_CF_SYMLINKS;
            rc = VINF_SUCCESS;
            break;
        }

        case SHFL_FN_QUERY_MAP_INFO:
        {
            pStatFail = pStat = &g_StatQueryMapInfo;
            Log(("SharedFolders host service: svnCall: SHFL_FN_QUERY_MAP_INFO\n"));

            /* Validate input: */
            rc = VERR_INVALID_PARAMETER;
            ASSERT_GUEST_BREAK(cParms == SHFL_CPARMS_QUERY_MAP_INFO);
            ASSERT_GUEST_BREAK(paParms[0].type == VBOX_HGCM_SVC_PARM_32BIT); /* root */
            ASSERT_GUEST_BREAK(paParms[1].type == VBOX_HGCM_SVC_PARM_PTR);   /* name */
            PSHFLSTRING  pNameBuf  = (PSHFLSTRING)paParms[1].u.pointer.addr;
            ASSERT_GUEST_BREAK(ShflStringIsValidOut(pNameBuf, paParms[1].u.pointer.size));
            ASSERT_GUEST_BREAK(paParms[2].type == VBOX_HGCM_SVC_PARM_PTR);   /* mountPoint */
            PSHFLSTRING  pMntPtBuf = (PSHFLSTRING)paParms[2].u.pointer.addr;
            ASSERT_GUEST_BREAK(ShflStringIsValidOut(pMntPtBuf, paParms[2].u.pointer.size));
            ASSERT_GUEST_BREAK(paParms[3].type == VBOX_HGCM_SVC_PARM_64BIT); /* flags */
            ASSERT_GUEST_BREAK(!(paParms[3].u.uint64 & ~(SHFL_MIQF_DRIVE_LETTER | SHFL_MIQF_PATH))); /* flags */
            ASSERT_GUEST_BREAK(paParms[4].type == VBOX_HGCM_SVC_PARM_32BIT); /* version */

            /* Execute the function: */
            rc = vbsfMappingsQueryInfo(pClient, paParms[0].u.uint32, pNameBuf, pMntPtBuf,
                                       &paParms[3].u.uint64, &paParms[4].u.uint32);
            break;
        }

        case SHFL_FN_WAIT_FOR_MAPPINGS_CHANGES:
        {
            pStat = &g_StatWaitForMappingsChanges;
            pStatFail = &g_StatWaitForMappingsChangesFail;
            Log(("SharedFolders host service: svnCall: SHFL_FN_WAIT_FOR_MAPPINGS_CHANGES\n"));

            /* Validate input: */
            rc = VERR_INVALID_PARAMETER;
            ASSERT_GUEST_BREAK(cParms == SHFL_CPARMS_WAIT_FOR_MAPPINGS_CHANGES);
            ASSERT_GUEST_BREAK(paParms[0].type == VBOX_HGCM_SVC_PARM_32BIT); /* uFolderMappingsVersion */

            /* Execute the function: */
            rc = vbsfMappingsWaitForChanges(pClient, callHandle, paParms, g_pHelpers->pfnIsCallRestored(callHandle));
            fAsynchronousProcessing = rc == VINF_HGCM_ASYNC_EXECUTE;
            break;
        }

        case SHFL_FN_CANCEL_MAPPINGS_CHANGES_WAITS:
        {
            pStatFail = pStat = &g_StatCancelMappingsChangesWait;
            Log(("SharedFolders host service: svnCall: SHFL_FN_CANCEL_WAIT_FOR_CHANGES\n"));

            /* Validate input: */
            rc = VERR_INVALID_PARAMETER;
            ASSERT_GUEST_BREAK(cParms == SHFL_CPARMS_CANCEL_MAPPINGS_CHANGES_WAITS);

            /* Execute the function: */
            rc = vbsfMappingsCancelChangesWaits(pClient);
            break;
        }

        case SHFL_FN_SET_FILE_SIZE:
        {
            pStat     = &g_StatSetFileSize;
            pStatFail = &g_StatSetFileSizeFail;
            Log(("SharedFolders host service: svcCall: SHFL_FN_SET_FILE_SIZE\n"));

            /* Validate input: */
            ASSERT_GUEST_STMT_BREAK(cParms == SHFL_CPARMS_SET_FILE_SIZE,         rc = VERR_WRONG_PARAMETER_COUNT);
            ASSERT_GUEST_STMT_BREAK(paParms[0].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* id32Root */
            ASSERT_GUEST_STMT_BREAK(paParms[1].type == VBOX_HGCM_SVC_PARM_64BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* u64Handle */
            ASSERT_GUEST_STMT_BREAK(paParms[2].type == VBOX_HGCM_SVC_PARM_64BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* cb64NewSize */

            /* Execute the function: */
            rc = vbsfSetFileSize(pClient, paParms[0].u.uint32, paParms[1].u.uint64, paParms[2].u.uint64);
            break;
        }

        case SHFL_FN_QUERY_FEATURES:
        {
            pStat = pStatFail = &g_StatQueryFeatures;

            /* Validate input: */
            ASSERT_GUEST_STMT_BREAK(cParms == SHFL_CPARMS_QUERY_FEATURES, rc = VERR_WRONG_PARAMETER_COUNT);
            ASSERT_GUEST_STMT_BREAK(paParms[0].type == VBOX_HGCM_SVC_PARM_64BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* f64Features */
            ASSERT_GUEST_STMT_BREAK(paParms[1].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* u32LastFunction */

            /* Execute the function: */
            paParms[0].u.uint64 = SHFL_FEATURE_WRITE_UPDATES_OFFSET;
            paParms[1].u.uint32 = SHFL_FN_LAST;
            rc = VINF_SUCCESS;
            break;
        }

        case SHFL_FN_COPY_FILE:
        {
            pStat     = &g_StatCopyFile;
            pStatFail = &g_StatCopyFileFail;

            /* Validate input: */
            ASSERT_GUEST_STMT_BREAK(cParms == SHFL_CPARMS_COPY_FILE, rc = VERR_WRONG_PARAMETER_COUNT);
            ASSERT_GUEST_STMT_BREAK(paParms[0].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* i32RootSrc  */
            ASSERT_GUEST_STMT_BREAK(paParms[1].type == VBOX_HGCM_SVC_PARM_PTR, rc = VERR_WRONG_PARAMETER_TYPE);   /* pStrPathSrc */
            PCSHFLSTRING pStrPathSrc = (PCSHFLSTRING)paParms[1].u.pointer.addr;
            ASSERT_GUEST_STMT_BREAK(ShflStringIsValidIn(pStrPathSrc, paParms[1].u.pointer.size,
                                                        RT_BOOL(pClient->fu32Flags & SHFL_CF_UTF8)),
                                    rc = VERR_INVALID_PARAMETER);
            ASSERT_GUEST_STMT_BREAK(paParms[2].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* i32RootDst  */
            ASSERT_GUEST_STMT_BREAK(paParms[3].type == VBOX_HGCM_SVC_PARM_PTR, rc = VERR_WRONG_PARAMETER_TYPE);   /* pStrPathDst */
            PCSHFLSTRING pStrPathDst = (PCSHFLSTRING)paParms[3].u.pointer.addr;
            ASSERT_GUEST_STMT_BREAK(ShflStringIsValidIn(pStrPathDst, paParms[3].u.pointer.size,
                                                        RT_BOOL(pClient->fu32Flags & SHFL_CF_UTF8)),
                                    rc = VERR_INVALID_PARAMETER);
            ASSERT_GUEST_STMT_BREAK(paParms[4].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* f32Flags */
            ASSERT_GUEST_STMT_BREAK(paParms[4].u.uint32 == 0, rc = VERR_INVALID_FLAGS);

            /* Execute the function: */
            rc = vbsfCopyFile(pClient, paParms[0].u.uint32, pStrPathSrc, paParms[2].u.uint64, pStrPathDst, paParms[3].u.uint32);
            break;
        }


        case SHFL_FN_COPY_FILE_PART:
        {
            pStat     = &g_StatCopyFilePart;
            pStatFail = &g_StatCopyFilePartFail;

            /* Validate input: */
            ASSERT_GUEST_STMT_BREAK(cParms == SHFL_CPARMS_COPY_FILE_PART, rc = VERR_WRONG_PARAMETER_COUNT);
            ASSERT_GUEST_STMT_BREAK(paParms[0].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* i32RootSrc  */
            ASSERT_GUEST_STMT_BREAK(paParms[1].type == VBOX_HGCM_SVC_PARM_64BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* u64HandleSrc */
            ASSERT_GUEST_STMT_BREAK(paParms[2].type == VBOX_HGCM_SVC_PARM_64BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* off64Src */
            ASSERT_GUEST_STMT_BREAK((int64_t)paParms[2].u.uint64 >= 0, rc = VERR_NEGATIVE_SEEK);
            ASSERT_GUEST_STMT_BREAK(paParms[3].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* i32RootDst  */
            ASSERT_GUEST_STMT_BREAK(paParms[4].type == VBOX_HGCM_SVC_PARM_64BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* u64HandleDst */
            ASSERT_GUEST_STMT_BREAK(paParms[5].type == VBOX_HGCM_SVC_PARM_64BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* off64Dst */
            ASSERT_GUEST_STMT_BREAK((int64_t)paParms[5].u.uint64 >= 0, rc = VERR_NEGATIVE_SEEK);
            ASSERT_GUEST_STMT_BREAK(paParms[6].type == VBOX_HGCM_SVC_PARM_64BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* cb64ToCopy */
            ASSERT_GUEST_STMT_BREAK(paParms[6].u.uint64 < _1E, rc = VERR_OUT_OF_RANGE);
            ASSERT_GUEST_STMT_BREAK(paParms[7].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* f32Flags */
            ASSERT_GUEST_STMT_BREAK(paParms[7].u.uint32 == 0, rc = VERR_INVALID_FLAGS);

            /* Execute the function: */
            rc = vbsfCopyFilePart(pClient,
                                  paParms[0].u.uint32, paParms[1].u.uint64, paParms[2].u.uint64,
                                  paParms[3].u.uint32, paParms[4].u.uint64, paParms[5].u.uint64,
                                  &paParms[6].u.uint64, paParms[7].u.uint64);
            break;
        }

        case SHFL_FN_SET_ERROR_STYLE:
        {
            pStatFail = pStat = &g_StatSetErrorStyle;

            /* Validate input: */
            ASSERT_GUEST_STMT_BREAK(cParms == SHFL_CPARMS_SET_ERROR_STYLE, rc = VERR_WRONG_PARAMETER_COUNT);
            ASSERT_GUEST_STMT_BREAK(paParms[0].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* enm32Style  */
            ASSERT_GUEST_STMT_BREAK(   paParms[0].u.uint32 > (uint32_t)kShflErrorStyle_Invalid
                                    && paParms[0].u.uint32 < (uint32_t)kShflErrorStyle_End, rc = VERR_WRONG_PARAMETER_TYPE);
            ASSERT_GUEST_STMT_BREAK(paParms[1].type == VBOX_HGCM_SVC_PARM_32BIT, rc = VERR_WRONG_PARAMETER_TYPE); /* u32Reserved */
            ASSERT_GUEST_STMT_BREAK(paParms[1].u.uint32 == 0, rc = VERR_WRONG_PARAMETER_TYPE);

            /* Do the work: */
            pClient->enmErrorStyle = (uint8_t)paParms[0].u.uint32;
            rc = VINF_SUCCESS;
            break;
        }

        default:
        {
            pStatFail = pStat = &g_StatUnknown;
            rc = VERR_NOT_IMPLEMENTED;
            break;
        }
    }

    LogFlow(("SharedFolders host service: svcCall: rc=%Rrc\n", rc));

    if (   !fAsynchronousProcessing
        || RT_FAILURE (rc))
    {
        /* Complete the operation if it was unsuccessful or
         * it was processed synchronously.
         */
        g_pHelpers->pfnCallComplete(callHandle, rc);
    }

#ifndef VBOX_WITHOUT_RELEASE_STATISTICS
    /* Statistics: */
    uint64_t cTicks;
    STAM_GET_TS(cTicks);
    cTicks -= tsStart;
    if (RT_SUCCESS(rc))
        STAM_REL_PROFILE_ADD_PERIOD(pStat, cTicks);
    else
        STAM_REL_PROFILE_ADD_PERIOD(pStatFail, cTicks);
#endif

    LogFlow(("\n"));        /* Add a new line to differentiate between calls more easily. */
}

/*
 * We differentiate between a function handler for the guest (svcCall) and one
 * for the host. The guest is not allowed to add or remove mappings for obvious
 * security reasons.
 */
static DECLCALLBACK(int) svcHostCall(void *, uint32_t u32Function, uint32_t cParms, VBOXHGCMSVCPARM paParms[])
{
    int rc = VINF_SUCCESS;

    Log(("svcHostCall: fn = %d, cParms = %d, pparms = %d\n", u32Function, cParms, paParms));

#ifdef DEBUG
    uint32_t i;

    for (i = 0; i < cParms; i++)
    {
        /** @todo parameters other than 32 bit */
        Log(("    pparms[%d]: type %d value %d\n", i, paParms[i].type, paParms[i].u.uint32));
    }
#endif

    switch (u32Function)
    {
    case SHFL_FN_ADD_MAPPING:
    {
        Log(("SharedFolders host service: svcCall: SHFL_FN_ADD_MAPPING\n"));
        LogRel(("SharedFolders host service: Adding host mapping\n"));
        /* Verify parameter count and types. */
        if (   (cParms != SHFL_CPARMS_ADD_MAPPING)
           )
        {
            rc = VERR_INVALID_PARAMETER;
        }
        else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_PTR     /* host folder path */
                 || paParms[1].type != VBOX_HGCM_SVC_PARM_PTR     /* map name */
                 || paParms[2].type != VBOX_HGCM_SVC_PARM_32BIT   /* fFlags */
                 || paParms[3].type != VBOX_HGCM_SVC_PARM_PTR     /* auto mount point */
                 || paParms[4].type != VBOX_HGCM_SVC_PARM_32BIT   /* symlink policy */
                )
        {
            rc = VERR_INVALID_PARAMETER;
        }
        else
        {
            /* Fetch parameters. */
            SHFLSTRING *pHostPath       = (SHFLSTRING *)paParms[0].u.pointer.addr;
            SHFLSTRING *pMapName        = (SHFLSTRING *)paParms[1].u.pointer.addr;
            uint32_t fFlags             = paParms[2].u.uint32;
            SHFLSTRING *pAutoMountPoint = (SHFLSTRING *)paParms[3].u.pointer.addr;
            SymlinkPolicy_T enmSymlinkPolicy = (SymlinkPolicy_T)paParms[4].u.uint32;

            /* Verify parameters values. */
            if (    !ShflStringIsValidIn(pHostPath, paParms[0].u.pointer.size, false /*fUtf8Not16*/)
                ||  !ShflStringIsValidIn(pMapName, paParms[1].u.pointer.size, false /*fUtf8Not16*/)
                ||  !ShflStringIsValidIn(pAutoMountPoint, paParms[3].u.pointer.size, false /*fUtf8Not16*/)
               )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                LogRel(("    Host path '%ls', map name '%ls', %s, automount=%s, automntpnt=%ls, create_symlinks=%s, missing=%s\n",
                        pHostPath->String.utf16, pMapName->String.utf16,
                        RT_BOOL(fFlags & SHFL_ADD_MAPPING_F_WRITABLE) ? "writable" : "read-only",
                        RT_BOOL(fFlags & SHFL_ADD_MAPPING_F_AUTOMOUNT) ? "true" : "false",
                        pAutoMountPoint->String.utf16,
                        RT_BOOL(fFlags & SHFL_ADD_MAPPING_F_CREATE_SYMLINKS) ? "true" : "false",
                        RT_BOOL(fFlags & SHFL_ADD_MAPPING_F_MISSING) ? "true" : "false"));

                char *pszHostPath;
                rc = RTUtf16ToUtf8(pHostPath->String.utf16, &pszHostPath);
                if (RT_SUCCESS(rc))
                {
                    /* Execute the function. */
                    rc = vbsfMappingsAdd(pszHostPath, pMapName,
                                         RT_BOOL(fFlags & SHFL_ADD_MAPPING_F_WRITABLE),
                                         RT_BOOL(fFlags & SHFL_ADD_MAPPING_F_AUTOMOUNT),
                                         pAutoMountPoint,
                                         RT_BOOL(fFlags & SHFL_ADD_MAPPING_F_CREATE_SYMLINKS),
                                         RT_BOOL(fFlags & SHFL_ADD_MAPPING_F_MISSING),
                                         /* fPlaceholder = */ false,
                                         enmSymlinkPolicy);
                    if (RT_SUCCESS(rc))
                    {
                        /* Update parameters.*/
                        ; /* none */
                    }
                    RTStrFree(pszHostPath);
                }
            }
        }
        if (RT_FAILURE(rc))
            LogRel(("SharedFolders host service: Adding host mapping failed with rc=%Rrc\n", rc));
        break;
    }

    case SHFL_FN_REMOVE_MAPPING:
    {
        Log(("SharedFolders host service: svcCall: SHFL_FN_REMOVE_MAPPING\n"));
        LogRel(("SharedFolders host service: Removing host mapping '%ls'\n",
                ((SHFLSTRING *)paParms[0].u.pointer.addr)->String.utf16));

        /* Verify parameter count and types. */
        if (cParms != SHFL_CPARMS_REMOVE_MAPPING)
        {
            rc = VERR_INVALID_PARAMETER;
        }
        else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_PTR     /* folder name */
                )
        {
            rc = VERR_INVALID_PARAMETER;
        }
        else
        {
            /* Fetch parameters. */
            SHFLSTRING *pString = (SHFLSTRING *)paParms[0].u.pointer.addr;

            /* Verify parameters values. */
            if (!ShflStringIsValidIn(pString, paParms[0].u.pointer.size, false /*fUtf8Not16*/))
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                /* Execute the function. */
                rc = vbsfMappingsRemove(pString);

                if (RT_SUCCESS(rc))
                {
                    /* Update parameters.*/
                    ; /* none */
                }
            }
        }
        if (RT_FAILURE(rc))
            LogRel(("SharedFolders host service: Removing host mapping failed with rc=%Rrc\n", rc));
        break;
    }

    case SHFL_FN_SET_STATUS_LED:
    {
        Log(("SharedFolders host service: svcCall: SHFL_FN_SET_STATUS_LED\n"));

        /* Verify parameter count and types. */
        if (cParms != SHFL_CPARMS_SET_STATUS_LED)
        {
            rc = VERR_INVALID_PARAMETER;
        }
        else if (   paParms[0].type != VBOX_HGCM_SVC_PARM_PTR     /* folder name */
                )
        {
            rc = VERR_INVALID_PARAMETER;
        }
        else
        {
            /* Fetch parameters. */
            PPDMLED  pLed     = (PPDMLED)paParms[0].u.pointer.addr;
            uint32_t cbLed    = paParms[0].u.pointer.size;

            /* Verify parameters values. */
            if (   (cbLed != sizeof (PDMLED))
               )
            {
                rc = VERR_INVALID_PARAMETER;
            }
            else
            {
                /* Execute the function. */
                g_pStatusLed = pLed;
                rc = VINF_SUCCESS;
            }
        }
        break;
    }

    default:
        rc = VERR_NOT_IMPLEMENTED;
        break;
    }

    LogFlow(("SharedFolders host service: svcHostCall ended with rc=%Rrc\n", rc));
    return rc;
}

extern "C" DECLCALLBACK(DECLEXPORT(int)) VBoxHGCMSvcLoad(VBOXHGCMSVCFNTABLE *ptable)
{
    int rc = VINF_SUCCESS;

    Log(("SharedFolders host service: VBoxHGCMSvcLoad: ptable = %p\n", ptable));

    if (!RT_VALID_PTR(ptable))
    {
        LogRelFunc(("SharedFolders host service: Bad value of ptable (%p)\n", ptable));
        rc = VERR_INVALID_PARAMETER;
    }
    else
    {
        Log(("SharedFolders host service: VBoxHGCMSvcLoad: ptable->cbSize = %u, ptable->u32Version = 0x%08X\n",
             ptable->cbSize, ptable->u32Version));

        if (    ptable->cbSize != sizeof (VBOXHGCMSVCFNTABLE)
            ||  ptable->u32Version != VBOX_HGCM_SVC_VERSION)
        {
            LogRelFunc(("SharedFolders host service: Version mismatch while loading: ptable->cbSize = %u (should be %u), ptable->u32Version = 0x%08X (should be 0x%08X)\n",
                        ptable->cbSize, sizeof (VBOXHGCMSVCFNTABLE), ptable->u32Version, VBOX_HGCM_SVC_VERSION));
            rc = VERR_VERSION_MISMATCH;
        }
        else
        {
            g_pHelpers = ptable->pHelpers;

            ptable->cbClient = sizeof (SHFLCLIENTDATA);

            /* Map legacy clients to the kernel category. */
            ptable->idxLegacyClientCategory = HGCM_CLIENT_CATEGORY_KERNEL;

            /* Only 64K pending calls per kernel client, root gets 16K and regular users 1K. */
            ptable->acMaxCallsPerClient[HGCM_CLIENT_CATEGORY_KERNEL] = _64K;
            ptable->acMaxCallsPerClient[HGCM_CLIENT_CATEGORY_ROOT]   = _16K;
            ptable->acMaxCallsPerClient[HGCM_CLIENT_CATEGORY_USER]   = _1K;

            /* Reduce the number of clients to SHFL_MAX_MAPPINGS + 2 in each category,
               so the increased calls-per-client value causes less trouble.
               ((64 + 2) * 3 * 65536 = 12 976 128) */
            for (uintptr_t i = 0; i < RT_ELEMENTS(ptable->acMaxClients); i++)
                ptable->acMaxClients[i] = SHFL_MAX_MAPPINGS + 2;

            ptable->pfnUnload     = svcUnload;
            ptable->pfnConnect    = svcConnect;
            ptable->pfnDisconnect = svcDisconnect;
            ptable->pfnCall       = svcCall;
            ptable->pfnHostCall   = svcHostCall;
            ptable->pfnSaveState  = svcSaveState;
            ptable->pfnLoadState  = svcLoadState;
            ptable->pfnNotify     = NULL;
            ptable->pvService     = NULL;
        }

        /* Init handle table */
        rc = vbsfInitHandleTable();
        AssertRC(rc);

        vbsfMappingInit();

        /* Finally, register statistics if everything went well: */
        if (RT_SUCCESS(rc))
        {
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatQueryMappings,             STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_QUERY_MAPPINGS successes",          "/HGCM/VBoxSharedFolders/FnQueryMappings");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatQueryMappingsFail,         STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_QUERY_MAPPINGS failures",           "/HGCM/VBoxSharedFolders/FnQueryMappingsFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatQueryMapName,              STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_QUERY_MAP_NAME",                    "/HGCM/VBoxSharedFolders/FnQueryMapName");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatCreate,                    STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_CREATE/CREATE successes",           "/HGCM/VBoxSharedFolders/FnCreate");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatCreateFail,                STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_CREATE/CREATE failures",            "/HGCM/VBoxSharedFolders/FnCreateFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatLookup,                    STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_CREATE/LOOKUP successes",           "/HGCM/VBoxSharedFolders/FnLookup");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatLookupFail,                STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_CREATE/LOOKUP failures",            "/HGCM/VBoxSharedFolders/FnLookupFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatClose,                     STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_CLOSE successes",                   "/HGCM/VBoxSharedFolders/FnClose");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatCloseFail,                 STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_CLOSE failures",                    "/HGCM/VBoxSharedFolders/FnCloseFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatRead,                      STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_READ successes",                    "/HGCM/VBoxSharedFolders/FnRead");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatReadFail,                  STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_READ failures",                     "/HGCM/VBoxSharedFolders/FnReadFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatWrite,                     STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_WRITE successes",                   "/HGCM/VBoxSharedFolders/FnWrite");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatWriteFail,                 STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_WRITE failures",                    "/HGCM/VBoxSharedFolders/FnWriteFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatLock,                      STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_LOCK successes",                    "/HGCM/VBoxSharedFolders/FnLock");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatLockFail,                  STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_LOCK failures",                     "/HGCM/VBoxSharedFolders/FnLockFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatList,                      STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_LIST successes",                    "/HGCM/VBoxSharedFolders/FnList");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatListFail,                  STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_LIST failures",                     "/HGCM/VBoxSharedFolders/FnListFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatReadLink,                  STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_READLINK successes",                "/HGCM/VBoxSharedFolders/FnReadLink");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatReadLinkFail,              STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_READLINK failures",                 "/HGCM/VBoxSharedFolders/FnReadLinkFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatMapFolderOld,              STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_MAP_FOLDER_OLD",                    "/HGCM/VBoxSharedFolders/FnMapFolderOld");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatMapFolder,                 STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_MAP_FOLDER successes",              "/HGCM/VBoxSharedFolders/FnMapFolder");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatMapFolderFail,             STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_MAP_FOLDER failures",               "/HGCM/VBoxSharedFolders/FnMapFolderFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatUnmapFolder,               STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_UNMAP_FOLDER successes",            "/HGCM/VBoxSharedFolders/FnUnmapFolder");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatUnmapFolderFail,           STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_UNMAP_FOLDER failures",             "/HGCM/VBoxSharedFolders/FnUnmapFolderFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatInformationFail,           STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_INFORMATION early failures",        "/HGCM/VBoxSharedFolders/FnInformationFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatInformationSetFile,        STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_INFORMATION/SET/FILE successes",    "/HGCM/VBoxSharedFolders/FnInformationSetFile");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatInformationSetFileFail,    STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_INFORMATION/SET/FILE failures",     "/HGCM/VBoxSharedFolders/FnInformationSetFileFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatInformationSetSize,        STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_INFORMATION/SET/SIZE successes",    "/HGCM/VBoxSharedFolders/FnInformationSetSize");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatInformationSetSizeFail,    STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_INFORMATION/SET/SIZE failures",     "/HGCM/VBoxSharedFolders/FnInformationSetSizeFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatInformationGetFile,        STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_INFORMATION/GET/FILE successes",    "/HGCM/VBoxSharedFolders/FnInformationGetFile");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatInformationGetFileFail,    STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_INFORMATION/GET/FILE failures",     "/HGCM/VBoxSharedFolders/FnInformationGetFileFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatInformationGetVolume,      STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_INFORMATION/GET/VOLUME successes",  "/HGCM/VBoxSharedFolders/FnInformationGetVolume");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatInformationGetVolumeFail,  STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_INFORMATION/GET/VOLUME failures",   "/HGCM/VBoxSharedFolders/FnInformationGetVolumeFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatRemove,                    STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_REMOVE successes",                  "/HGCM/VBoxSharedFolders/FnRemove");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatRemoveFail,                STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_REMOVE failures",                   "/HGCM/VBoxSharedFolders/FnRemoveFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatCloseAndRemove,            STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_CLOSE_AND_REMOVE successes",        "/HGCM/VBoxSharedFolders/FnCloseAndRemove");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatCloseAndRemoveFail,        STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_CLOSE_AND_REMOVE failures",         "/HGCM/VBoxSharedFolders/FnCloseAndRemoveFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatRename,                    STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_RENAME successes",                  "/HGCM/VBoxSharedFolders/FnRename");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatRenameFail,                STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_RENAME failures",                   "/HGCM/VBoxSharedFolders/FnRenameFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatFlush,                     STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_FLUSH successes",                   "/HGCM/VBoxSharedFolders/FnFlush");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatFlushFail,                 STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_FLUSH failures",                    "/HGCM/VBoxSharedFolders/FnFlushFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatSetErrorStyle,             STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_SET_ERROR_STYLE",                   "/HGCM/VBoxSharedFolders/FnSetErrorStyle");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatSetUtf8,                   STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_SET_UTF8",                          "/HGCM/VBoxSharedFolders/FnSetUtf8");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatSymlink,                   STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_SYMLINK successes",                 "/HGCM/VBoxSharedFolders/FnSymlink");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatSymlinkFail,               STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_SYMLINK failures",                  "/HGCM/VBoxSharedFolders/FnSymlinkFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatSetSymlinks,               STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_SET_SYMLINKS",                      "/HGCM/VBoxSharedFolders/FnSetSymlink");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatQueryMapInfo,              STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_QUERY_MAP_INFO",                    "/HGCM/VBoxSharedFolders/FnQueryMapInfo");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatQueryFeatures,             STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_QUERY_FEATURES",                    "/HGCM/VBoxSharedFolders/FnQueryFeatures");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatCopyFile,                  STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_COPY_FILE successes",               "/HGCM/VBoxSharedFolders/FnCopyFile");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatCopyFileFail,              STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_COPY_FILE failures",                "/HGCM/VBoxSharedFolders/FnCopyFileFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatCopyFilePart,              STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_COPY_FILE_PART successes",          "/HGCM/VBoxSharedFolders/FnCopyFilePart");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatCopyFilePartFail,          STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_COPY_FILE_PART failures",           "/HGCM/VBoxSharedFolders/FnCopyFilePartFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatWaitForMappingsChanges,    STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_WAIT_FOR_MAPPINGS_CHANGES successes", "/HGCM/VBoxSharedFolders/FnWaitForMappingsChanges");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatWaitForMappingsChangesFail,STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_WAIT_FOR_MAPPINGS_CHANGES failures","/HGCM/VBoxSharedFolders/FnWaitForMappingsChangesFail");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatCancelMappingsChangesWait, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_CANCEL_MAPPINGS_CHANGES_WAITS",     "/HGCM/VBoxSharedFolders/FnCancelMappingsChangesWaits");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatUnknown,                   STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "SHFL_FN_???",                               "/HGCM/VBoxSharedFolders/FnUnknown");
            HGCMSvcHlpStamRegister(g_pHelpers, &g_StatMsgStage1,                 STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL, "Time from VMMDev arrival to worker thread.","/HGCM/VBoxSharedFolders/MsgStage1");
        }
    }

    return rc;
}

