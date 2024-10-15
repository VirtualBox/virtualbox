/* $Id: PGMSharedPage.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * PGM - Page Manager and Monitor, Shared page handling
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
#define LOG_GROUP LOG_GROUP_PGM_SHARED
#define VBOX_WITHOUT_PAGING_BIT_FIELDS /* 64-bit bitfields are just asking for trouble. See @bugref{9841} and others. */
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/uvm.h>
#include "PGMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/sup.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/VMMDev.h>
#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/mem.h>
#include <iprt/string.h>

#include "PGMInline.h"


#ifdef VBOX_WITH_PAGE_SHARING


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
# ifdef VBOX_STRICT
/** Keep a copy of all registered shared modules for the .pgmcheckduppages debugger command. */
static PGMMREGISTERSHAREDMODULEREQ  g_apSharedModules[512] = {0};
static unsigned                     g_cSharedModules = 0;
# endif /* VBOX_STRICT */


/**
 * Registers a new shared module for the VM
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   enmGuestOS          Guest OS type.
 * @param   pszModuleName       Module name.
 * @param   pszVersion          Module version.
 * @param   GCBaseAddr          Module base address.
 * @param   cbModule            Module size.
 * @param   cRegions            Number of shared region descriptors.
 * @param   paRegions           Shared region(s).
 *
 * @todo    This should be a GMMR3 call. No need to involve GMM here.
 */
VMMR3DECL(int) PGMR3SharedModuleRegister(PVM pVM, VBOXOSFAMILY enmGuestOS, char *pszModuleName, char *pszVersion,
                                         RTGCPTR GCBaseAddr, uint32_t cbModule, uint32_t cRegions,
                                         VMMDEVSHAREDREGIONDESC const *paRegions)
{
    Log(("PGMR3SharedModuleRegister family=%d name=%s version=%s base=%RGv size=%x cRegions=%d\n",
         enmGuestOS, pszModuleName, pszVersion, GCBaseAddr, cbModule, cRegions));

    /*
     * Sanity check.
     */
    AssertReturn(cRegions <= VMMDEVSHAREDREGIONDESC_MAX, VERR_INVALID_PARAMETER);
    if (!pVM->pgm.s.fPageFusionAllowed)
        return VERR_NOT_SUPPORTED;

    /*
     * Allocate and initialize a GMM request.
     */
    PGMMREGISTERSHAREDMODULEREQ pReq;
    pReq = (PGMMREGISTERSHAREDMODULEREQ)RTMemAllocZ(RT_UOFFSETOF_DYN(GMMREGISTERSHAREDMODULEREQ, aRegions[cRegions]));
    AssertReturn(pReq, VERR_NO_MEMORY);

    pReq->enmGuestOS    = enmGuestOS;
    pReq->GCBaseAddr    = GCBaseAddr;
    pReq->cbModule      = cbModule;
    pReq->cRegions      = cRegions;
    for (uint32_t i = 0; i < cRegions; i++)
        pReq->aRegions[i] = paRegions[i];

    int rc = RTStrCopy(pReq->szName, sizeof(pReq->szName), pszModuleName);
    if (RT_SUCCESS(rc))
    {
        rc = RTStrCopy(pReq->szVersion, sizeof(pReq->szVersion), pszVersion);
        if (RT_SUCCESS(rc))
        {
            /*
             * Issue the request.  In strict builds, do some local tracking.
             */
            pgmR3PhysAssertSharedPageChecksums(pVM);
            rc = GMMR3RegisterSharedModule(pVM, pReq);
            if (RT_SUCCESS(rc))
                rc = pReq->rc;
            AssertMsg(rc == VINF_SUCCESS || rc == VINF_GMM_SHARED_MODULE_ALREADY_REGISTERED, ("%Rrc\n", rc));

# ifdef VBOX_STRICT
            if (   rc == VINF_SUCCESS
                && g_cSharedModules < RT_ELEMENTS(g_apSharedModules))
            {
                unsigned i;
                for (i = 0; i < RT_ELEMENTS(g_apSharedModules); i++)
                    if (g_apSharedModules[i] == NULL)
                    {

                        size_t const cbSharedModule = RT_UOFFSETOF_DYN(GMMREGISTERSHAREDMODULEREQ, aRegions[cRegions]);
                        g_apSharedModules[i] = (PGMMREGISTERSHAREDMODULEREQ)RTMemDup(pReq, cbSharedModule);
                        g_cSharedModules++;
                        break;
                    }
                Assert(i < RT_ELEMENTS(g_apSharedModules));
            }
# endif /* VBOX_STRICT */
            if (RT_SUCCESS(rc))
                rc = VINF_SUCCESS;
        }
    }

    RTMemFree(pReq);
    return rc;
}


/**
 * Unregisters a shared module for the VM
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pszModuleName       Module name.
 * @param   pszVersion          Module version.
 * @param   GCBaseAddr          Module base address.
 * @param   cbModule            Module size.
 *
 * @todo    This should be a GMMR3 call. No need to involve GMM here.
 */
VMMR3DECL(int) PGMR3SharedModuleUnregister(PVM pVM, char *pszModuleName, char *pszVersion, RTGCPTR GCBaseAddr, uint32_t cbModule)
{
    Log(("PGMR3SharedModuleUnregister name=%s version=%s base=%RGv size=%x\n", pszModuleName, pszVersion, GCBaseAddr, cbModule));

    AssertMsgReturn(cbModule > 0 && cbModule < _1G, ("%u\n", cbModule), VERR_OUT_OF_RANGE);
    if (!pVM->pgm.s.fPageFusionAllowed)
        return VERR_NOT_SUPPORTED;

    /*
     * Forward the request to GMM (ring-0).
     */
    PGMMUNREGISTERSHAREDMODULEREQ pReq = (PGMMUNREGISTERSHAREDMODULEREQ)RTMemAlloc(sizeof(*pReq));
    AssertReturn(pReq, VERR_NO_MEMORY);

    pReq->GCBaseAddr    = GCBaseAddr;
    pReq->u32Alignment  = 0;
    pReq->cbModule      = cbModule;

    int rc = RTStrCopy(pReq->szName, sizeof(pReq->szName), pszModuleName);
    if (RT_SUCCESS(rc))
    {
        rc = RTStrCopy(pReq->szVersion, sizeof(pReq->szVersion), pszVersion);
        if (RT_SUCCESS(rc))
        {
            pgmR3PhysAssertSharedPageChecksums(pVM);
            rc = GMMR3UnregisterSharedModule(pVM, pReq);
            pgmR3PhysAssertSharedPageChecksums(pVM);

# ifdef VBOX_STRICT
            /*
             * Update our local tracking.
             */
            for (unsigned i = 0; i < g_cSharedModules; i++)
            {
                if (    g_apSharedModules[i]
                    &&  !strcmp(g_apSharedModules[i]->szName, pszModuleName)
                    &&  !strcmp(g_apSharedModules[i]->szVersion, pszVersion))
                {
                    RTMemFree(g_apSharedModules[i]);
                    g_apSharedModules[i] = NULL;
                    g_cSharedModules--;
                    break;
                }
            }
# endif /* VBOX_STRICT */
        }
    }

    RTMemFree(pReq);
    return rc;
}


/**
 * Rendezvous callback that will be called once.
 *
 * @returns VBox strict status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 * @param   pvUser              Pointer to a VMCPUID with the requester's ID.
 */
static DECLCALLBACK(VBOXSTRICTRC) pgmR3SharedModuleRegRendezvous(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    VMCPUID idCpu = *(VMCPUID *)pvUser;

    /* Execute on the VCPU that issued the original request to make sure we're in the right cr3 context. */
    if (pVCpu->idCpu != idCpu)
    {
        Assert(pVM->cCpus > 1);
        return VINF_SUCCESS;
    }


    /* Flush all pending handy page operations before changing any shared page assignments. */
    int rc = PGMR3PhysAllocateHandyPages(pVM);
    AssertRC(rc);

    /*
     * Lock it here as we can't deal with busy locks in this ring-0 path.
     */
    LogFlow(("pgmR3SharedModuleRegRendezvous: start (%d)\n", pVM->pgm.s.cSharedPages));

    PGM_LOCK_VOID(pVM);
    pgmR3PhysAssertSharedPageChecksums(pVM);
    rc = GMMR3CheckSharedModules(pVM);
    pgmR3PhysAssertSharedPageChecksums(pVM);
    PGM_UNLOCK(pVM);
    AssertLogRelRC(rc);

    LogFlow(("pgmR3SharedModuleRegRendezvous: done (%d)\n", pVM->pgm.s.cSharedPages));
    return rc;
}

/**
 * Shared module check helper (called on the way out).
 *
 * @param   pVM         The cross context VM structure.
 * @param   idCpu       VCPU id.
 */
static DECLCALLBACK(void) pgmR3CheckSharedModulesHelper(PVM pVM, VMCPUID idCpu)
{
    /* We must stall other VCPUs as we'd otherwise have to send IPI flush commands for every single change we make. */
    STAM_REL_PROFILE_START(&pVM->pgm.s.StatShModCheck, a);
    int rc = VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE, pgmR3SharedModuleRegRendezvous, &idCpu);
    AssertRCSuccess(rc);
    STAM_REL_PROFILE_STOP(&pVM->pgm.s.StatShModCheck, a);
}


/**
 * Check all registered modules for changes.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 */
VMMR3DECL(int) PGMR3SharedModuleCheckAll(PVM pVM)
{
    if (!pVM->pgm.s.fPageFusionAllowed)
        return VERR_NOT_SUPPORTED;

    /* Queue the actual registration as we are under the IOM lock right now. Perform this operation on the way out. */
    return VMR3ReqCallNoWait(pVM, VMCPUID_ANY_QUEUE, (PFNRT)pgmR3CheckSharedModulesHelper, 2, pVM, VMMGetCpuId(pVM));
}


# ifdef DEBUG
/**
 * Query the state of a page in a shared module
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   GCPtrPage           Page address.
 * @param   pfShared            Shared status (out).
 * @param   pfPageFlags         Page flags (out).
 */
VMMR3DECL(int) PGMR3SharedModuleGetPageState(PVM pVM, RTGCPTR GCPtrPage, bool *pfShared, uint64_t *pfPageFlags)
{
    /* Debug only API for the page fusion testcase. */
    PGMPTWALK Walk;

    PGM_LOCK_VOID(pVM);

    int rc = PGMGstGetPage(VMMGetCpu(pVM), GCPtrPage, &Walk);
    switch (rc)
    {
        case VINF_SUCCESS:
        {
            PPGMPAGE pPage = pgmPhysGetPage(pVM, Walk.GCPhys);
            if (pPage)
            {
                *pfShared    = PGM_PAGE_IS_SHARED(pPage);
                *pfPageFlags = Walk.fEffective;
            }
            else
                rc = VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
            break;
        }

        case VERR_PAGE_NOT_PRESENT:
        case VERR_PAGE_TABLE_NOT_PRESENT:
        case VERR_PAGE_MAP_LEVEL4_NOT_PRESENT:
        case VERR_PAGE_DIRECTORY_PTR_NOT_PRESENT:
            *pfShared    = false;
            *pfPageFlags = 0;
            rc = VINF_SUCCESS;
            break;

        default:
            break;
    }

    PGM_UNLOCK(pVM);
    return rc;
}
# endif /* DEBUG */

# ifdef VBOX_STRICT

/**
 * @callback_method_impl{FNDBGCCMD, The '.pgmcheckduppages' command.}
 */
DECLCALLBACK(int) pgmR3CmdCheckDuplicatePages(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    unsigned cBallooned = 0;
    unsigned cShared    = 0;
    unsigned cZero      = 0;
    unsigned cUnique    = 0;
    unsigned cDuplicate = 0;
    unsigned cAllocZero = 0;
    unsigned cPages     = 0;
    NOREF(pCmd); NOREF(paArgs); NOREF(cArgs);
    PVM      pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    PGM_LOCK_VOID(pVM);

    uint32_t const idRamRangeMax = RT_MIN(pVM->pgm.s.idRamRangeMax, RT_ELEMENTS(pVM->pgm.s.apRamRanges) - 1U);
    for (uint32_t idRamRange = 0; idRamRange <= idRamRangeMax; idRamRange++)
    {
        PPGMRAMRANGE const pRam = pVM->pgm.s.apRamRanges[idRamRange];
        Assert(pRam || idRamRange == 0);
        if (!pRam) continue;
        Assert(pRam->idRange == idRamRange);

        PPGMPAGE    pPage  = &pRam->aPages[0];
        RTGCPHYS    GCPhys = pRam->GCPhys;
        uint32_t    cLeft  = pRam->cb >> GUEST_PAGE_SHIFT;
        while (cLeft-- > 0)
        {
            if (PGM_PAGE_GET_TYPE(pPage) == PGMPAGETYPE_RAM)
            {
                switch (PGM_PAGE_GET_STATE(pPage))
                {
                    case PGM_PAGE_STATE_ZERO:
                        cZero++;
                        break;

                    case PGM_PAGE_STATE_BALLOONED:
                        cBallooned++;
                        break;

                    case PGM_PAGE_STATE_SHARED:
                        cShared++;
                        break;

                    case PGM_PAGE_STATE_ALLOCATED:
                    case PGM_PAGE_STATE_WRITE_MONITORED:
                    {
                        /* Check if the page was allocated, but completely zero. */
                        PGMPAGEMAPLOCK PgMpLck;
                        const void    *pvPage;
                        int rc = pgmPhysGCPhys2CCPtrInternalReadOnly(pVM, pPage, GCPhys, &pvPage, &PgMpLck);
                        if (    RT_SUCCESS(rc)
                            &&  ASMMemIsZero(pvPage, GUEST_PAGE_SIZE))
                            cAllocZero++;
                        else if (GMMR3IsDuplicatePage(pVM, PGM_PAGE_GET_PAGEID(pPage)))
                            cDuplicate++;
                        else
                            cUnique++;
                        if (RT_SUCCESS(rc))
                            pgmPhysReleaseInternalPageMappingLock(pVM, &PgMpLck);
                        break;
                    }

                    default:
                        AssertFailed();
                        break;
                }
            }

            /* next */
            pPage++;
            GCPhys += GUEST_PAGE_SIZE;
            cPages++;
            /* Give some feedback for every processed megabyte. */
            if ((cPages & 0x7f) == 0)
                pCmdHlp->pfnPrintf(pCmdHlp, NULL, ".");
        }
    }

    PGM_UNLOCK(pVM);

    pCmdHlp->pfnPrintf(pCmdHlp, NULL, "\nNumber of zero pages      %08x (%d MB)\n", cZero, cZero / 256);
    pCmdHlp->pfnPrintf(pCmdHlp, NULL, "Number of alloczero pages %08x (%d MB)\n", cAllocZero, cAllocZero / 256);
    pCmdHlp->pfnPrintf(pCmdHlp, NULL, "Number of ballooned pages %08x (%d MB)\n", cBallooned, cBallooned / 256);
    pCmdHlp->pfnPrintf(pCmdHlp, NULL, "Number of shared pages    %08x (%d MB)\n", cShared, cShared / 256);
    pCmdHlp->pfnPrintf(pCmdHlp, NULL, "Number of unique pages    %08x (%d MB)\n", cUnique, cUnique / 256);
    pCmdHlp->pfnPrintf(pCmdHlp, NULL, "Number of duplicate pages %08x (%d MB)\n", cDuplicate, cDuplicate / 256);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGCCMD, The '.pgmsharedmodules' command.}
 */
DECLCALLBACK(int) pgmR3CmdShowSharedModules(PCDBGCCMD pCmd, PDBGCCMDHLP pCmdHlp, PUVM pUVM, PCDBGCVAR paArgs, unsigned cArgs)
{
    NOREF(pCmd); NOREF(paArgs); NOREF(cArgs);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    PGM_LOCK_VOID(pVM);
    for (unsigned i = 0; i < RT_ELEMENTS(g_apSharedModules); i++)
    {
        if (g_apSharedModules[i])
        {
            pCmdHlp->pfnPrintf(pCmdHlp, NULL, "Shared module %s (%s):\n", g_apSharedModules[i]->szName, g_apSharedModules[i]->szVersion);
            for (unsigned j = 0; j < g_apSharedModules[i]->cRegions; j++)
                pCmdHlp->pfnPrintf(pCmdHlp, NULL, "--- Region %d: base %RGv size %x\n", j, g_apSharedModules[i]->aRegions[j].GCRegionAddr, g_apSharedModules[i]->aRegions[j].cbRegion);
        }
    }
    PGM_UNLOCK(pVM);

    return VINF_SUCCESS;
}

# endif /* VBOX_STRICT*/
#endif /* VBOX_WITH_PAGE_SHARING */
