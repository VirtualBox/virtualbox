/* $Id: GIM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GIM - Guest Interface Manager.
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

/** @page pg_gim        GIM - The Guest Interface Manager
 *
 * The Guest Interface Manager abstracts an interface provider through which
 * guests may interact with the hypervisor.
 *
 * @see grp_gim
 *
 *
 * @section sec_gim_provider   Providers
 *
 * A GIM provider implements a particular hypervisor interface such as Microsoft
 * Hyper-V, Linux KVM and so on. It hooks into various components in the VMM to
 * ease the guest in running under a recognized, virtualized environment.
 *
 * The GIM provider configured for the VM needs to be recognized by the guest OS
 * in order to make use of features supported by the interface. Since it
 * requires co-operation from the guest OS, a GIM provider may also be referred to
 * as a paravirtualization interface.
 *
 * One of the goals of having a paravirtualized interface is for enabling guests
 * to be more accurate and efficient when operating in a virtualized
 * environment. For instance, a guest OS which interfaces to VirtualBox through
 * a GIM provider may rely on the provider for supplying the correct TSC
 * frequency of the host processor. The guest can then avoid caliberating the
 * TSC itself, resulting in higher accuracy and better performance.
 *
 * At most, only one GIM provider can be active for a running VM and cannot be
 * changed during the lifetime of the VM.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_GIM
#include <VBox/vmm/gim.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/pdmdev.h>
#include "GIMInternal.h"
#include <VBox/vmm/vm.h>

#include <VBox/log.h>

#include <iprt/err.h>
#include <iprt/semaphore.h>
#include <iprt/string.h>

#if !defined(VBOX_VMM_TARGET_ARMV8)
/* Include all GIM providers. */
# include "GIMMinimalInternal.h"
# include "GIMHvInternal.h"
# include "GIMKvmInternal.h"
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static FNSSMINTSAVEEXEC  gimR3Save;
static FNSSMINTLOADEXEC  gimR3Load;
static FNSSMINTLOADDONE  gimR3LoadDone;


/**
 * Initializes the GIM.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) GIMR3Init(PVM pVM)
{
    LogFlow(("GIMR3Init\n"));

    /*
     * Assert alignment and sizes.
     */
    AssertCompile(sizeof(pVM->gim.s) <= sizeof(pVM->gim.padding));
    AssertCompile(sizeof(pVM->apCpusR3[0]->gim.s) <= sizeof(pVM->apCpusR3[0]->gim.padding));

    /*
     * Initialize members.
     */
    pVM->gim.s.hSemiReadOnlyMmio2Handler = NIL_PGMPHYSHANDLERTYPE;

    /*
     * Register the saved state data unit.
     */
    int rc = SSMR3RegisterInternal(pVM, "GIM", 0 /* uInstance */, GIM_SAVED_STATE_VERSION, sizeof(GIM),
                                   NULL /* pfnLivePrep */, NULL /* pfnLiveExec */, NULL /* pfnLiveVote*/,
                                   NULL /* pfnSavePrep */, gimR3Save,              NULL /* pfnSaveDone */,
                                   NULL /* pfnLoadPrep */, gimR3Load,              gimR3LoadDone);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Read configuration.
     */
    PCFGMNODE pCfgNode = CFGMR3GetChild(CFGMR3GetRoot(pVM), "GIM/");

    /*
     * Validate the GIM settings.
     */
    rc = CFGMR3ValidateConfig(pCfgNode, "/GIM/", /* pszNode */
                              "Provider"         /* pszValidValues */
                              "|Version",
                              "HyperV",          /* pszValidNodes */
                              "GIM",             /* pszWho */
                              0);                /* uInstance */
    if (RT_FAILURE(rc))
        return rc;

    /** @cfgm{/GIM/Provider, string}
     * The name of the GIM provider. The default is "none". */
    char szProvider[64];
    rc = CFGMR3QueryStringDef(pCfgNode, "Provider", szProvider, sizeof(szProvider), "None");
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/GIM/Version, uint32_t}
     * The interface version. The default is 0, which means "provide the most
     * up-to-date implementation". */
    uint32_t uVersion;
    rc = CFGMR3QueryU32Def(pCfgNode, "Version", &uVersion, 0 /* default */);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Setup the GIM provider for this VM.
     */
    LogRel(("GIM: Using provider '%s' (Implementation version: %u)\n", szProvider, uVersion));
    if (!RTStrCmp(szProvider, "None"))
        pVM->gim.s.enmProviderId = GIMPROVIDERID_NONE;
    else
    {
        pVM->gim.s.u32Version = uVersion;
        /** @todo r=bird: Because u32Version is saved, it should be translated to the
         *        'most up-to-date implementation' version number when 0. Otherwise,
         *        we'll have abiguities when loading the state of older VMs. */
#if !defined(VBOX_VMM_TARGET_ARMV8)
        if (!RTStrCmp(szProvider, "Minimal"))
        {
            pVM->gim.s.enmProviderId = GIMPROVIDERID_MINIMAL;
            rc = gimR3MinimalInit(pVM);
        }
        else if (!RTStrCmp(szProvider, "HyperV"))
        {
            pVM->gim.s.enmProviderId = GIMPROVIDERID_HYPERV;
            rc = gimR3HvInit(pVM, pCfgNode);
        }
        else if (!RTStrCmp(szProvider, "KVM"))
        {
            pVM->gim.s.enmProviderId = GIMPROVIDERID_KVM;
            rc = gimR3KvmInit(pVM);
        }
        else
#endif
            rc = VMR3SetError(pVM->pUVM, VERR_GIM_INVALID_PROVIDER, RT_SRC_POS, "Provider '%s' unknown.", szProvider);
    }

    /*
     * Statistics.
     */
    STAM_REL_REG_USED(pVM, &pVM->gim.s.StatDbgXmit,      STAMTYPE_COUNTER, "/GIM/Debug/Transmit",      STAMUNIT_OCCURENCES, "Debug packets sent.");
    STAM_REL_REG_USED(pVM, &pVM->gim.s.StatDbgXmitBytes, STAMTYPE_COUNTER, "/GIM/Debug/TransmitBytes", STAMUNIT_OCCURENCES, "Debug bytes sent.");
    STAM_REL_REG_USED(pVM, &pVM->gim.s.StatDbgRecv,      STAMTYPE_COUNTER, "/GIM/Debug/Receive",       STAMUNIT_OCCURENCES, "Debug packets received.");
    STAM_REL_REG_USED(pVM, &pVM->gim.s.StatDbgRecvBytes, STAMTYPE_COUNTER, "/GIM/Debug/ReceiveBytes",  STAMUNIT_OCCURENCES, "Debug bytes received.");

    STAM_REL_REG_USED(pVM, &pVM->gim.s.StatHypercalls,   STAMTYPE_COUNTER, "/GIM/Hypercalls",          STAMUNIT_OCCURENCES, "Number of hypercalls initiated.");
    return rc;
}


/**
 * Initializes the remaining bits of the GIM provider.
 *
 * This is called after initializing HM and most other VMM components.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @thread  EMT(0)
 */
VMMR3_INT_DECL(int) GIMR3InitCompleted(PVM pVM)
{
    switch (pVM->gim.s.enmProviderId)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
        case GIMPROVIDERID_MINIMAL:
            return gimR3MinimalInitCompleted(pVM);

        case GIMPROVIDERID_HYPERV:
            return gimR3HvInitCompleted(pVM);

        case GIMPROVIDERID_KVM:
            return gimR3KvmInitCompleted(pVM);
#endif
        default:
            break;
    }

    if (!TMR3CpuTickIsFixedRateMonotonic(pVM, true /* fWithParavirtEnabled */))
        LogRel(("GIM: Warning!!! Host TSC is unstable. The guest may behave unpredictably with a paravirtualized clock.\n"));

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMINTSAVEEXEC}
 */
static DECLCALLBACK(int) gimR3Save(PVM pVM, PSSMHANDLE pSSM)
{
    AssertReturn(pVM,  VERR_INVALID_PARAMETER);
    AssertReturn(pSSM, VERR_SSM_INVALID_STATE);

    int rc = VINF_SUCCESS;
#if 0
    /* Save per-CPU data. */
    SSMR3PutU32(pSSM, pVM->cCpus);
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        rc = SSMR3PutXYZ(pSSM, pVCpu->gim.s.XYZ);
    }
#endif

    /*
     * Save per-VM data.
     */
    SSMR3PutU32(pSSM, pVM->gim.s.enmProviderId);
    SSMR3PutU32(pSSM, pVM->gim.s.u32Version);

    /*
     * Save provider-specific data.
     */
    switch (pVM->gim.s.enmProviderId)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
        case GIMPROVIDERID_HYPERV:
            rc = gimR3HvSave(pVM, pSSM);
            AssertRCReturn(rc, rc);
            break;

        case GIMPROVIDERID_KVM:
            rc = gimR3KvmSave(pVM, pSSM);
            AssertRCReturn(rc, rc);
            break;
#endif
        default:
            break;
    }

    return rc;
}


/**
 * @callback_method_impl{FNSSMINTLOADEXEC}
 */
static DECLCALLBACK(int) gimR3Load(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    if (uPass != SSM_PASS_FINAL)
        return VINF_SUCCESS;
    if (uVersion != GIM_SAVED_STATE_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    int rc;
#if 0
    /* Load per-CPU data. */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        rc = SSMR3PutXYZ(pSSM, pVCpu->gim.s.XYZ);
    }
#endif

    /*
     * Load per-VM data.
     */
    uint32_t uProviderId;
    uint32_t uProviderVersion;

    SSMR3GetU32(pSSM, &uProviderId);
    rc = SSMR3GetU32(pSSM, &uProviderVersion);
    AssertRCReturn(rc, rc);

    if ((GIMPROVIDERID)uProviderId != pVM->gim.s.enmProviderId)
        return SSMR3SetCfgError(pSSM, RT_SRC_POS, N_("Saved GIM provider %u differs from the configured one (%u)."),
                                uProviderId, pVM->gim.s.enmProviderId);
#if 0 /** @todo r=bird: Figure out what you mean to do here with the version. */
    if (uProviderVersion != pVM->gim.s.u32Version)
        return SSMR3SetCfgError(pSSM, RT_SRC_POS, N_("Saved GIM provider version %u differs from the configured one (%u)."),
                                uProviderVersion, pVM->gim.s.u32Version);
#else
    pVM->gim.s.u32Version = uProviderVersion;
#endif

    /*
     * Load provider-specific data.
     */
    switch (pVM->gim.s.enmProviderId)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
        case GIMPROVIDERID_HYPERV:
            rc = gimR3HvLoad(pVM, pSSM);
            AssertRCReturn(rc, rc);
            break;

        case GIMPROVIDERID_KVM:
            rc = gimR3KvmLoad(pVM, pSSM);
            AssertRCReturn(rc, rc);
            break;
#endif
        default:
            break;
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMINTLOADDONE}
 */
static DECLCALLBACK(int) gimR3LoadDone(PVM pVM, PSSMHANDLE pSSM)
{
#if defined(VBOX_VMM_TARGET_ARMV8)
    RT_NOREF(pSSM);
#endif

    switch (pVM->gim.s.enmProviderId)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
        case GIMPROVIDERID_HYPERV:
            return gimR3HvLoadDone(pVM, pSSM);
#endif
        default:
            return VINF_SUCCESS;
    }
}


/**
 * Terminates the GIM.
 *
 * Termination means cleaning up and freeing all resources,
 * the VM itself is, at this point, powered off or suspended.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) GIMR3Term(PVM pVM)
{
    switch (pVM->gim.s.enmProviderId)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
        case GIMPROVIDERID_HYPERV:
            return gimR3HvTerm(pVM);

        case GIMPROVIDERID_KVM:
            return gimR3KvmTerm(pVM);
#endif
        default:
            break;
    }
    return VINF_SUCCESS;
}


/**
 * Applies relocations to data and code managed by this
 * component. This function will be called at init and
 * whenever the VMM need to relocate it self inside the GC.
 *
 * @param   pVM         The cross context VM structure.
 * @param   offDelta    Relocation delta relative to old location.
 */
VMMR3_INT_DECL(void) GIMR3Relocate(PVM pVM, RTGCINTPTR offDelta)
{
#if defined(VBOX_VMM_TARGET_ARMV8)
    RT_NOREF(offDelta);
#endif

    switch (pVM->gim.s.enmProviderId)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
        case GIMPROVIDERID_HYPERV:
            gimR3HvRelocate(pVM, offDelta);
            break;
#endif
        default:
            break;
    }
}


/**
 * The VM is being reset.
 *
 * For the GIM component this means unmapping and unregistering MMIO2 regions
 * and other provider-specific resets.
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(void) GIMR3Reset(PVM pVM)
{
    switch (pVM->gim.s.enmProviderId)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
        case GIMPROVIDERID_HYPERV:
            return gimR3HvReset(pVM);

        case GIMPROVIDERID_KVM:
            return gimR3KvmReset(pVM);
#endif
        default:
            break;
    }
}


/**
 * Registers the GIM device with VMM.
 *
 * @param   pVM             The cross context VM structure.
 * @param   pDevIns         Pointer to the GIM device instance.
 * @param   pDbg            Pointer to the GIM device debug structure, can be
 *                          NULL.
 */
VMMR3DECL(void) GIMR3GimDeviceRegister(PVM pVM, PPDMDEVINS pDevIns, PGIMDEBUG pDbg)
{
    pVM->gim.s.pDevInsR3 = pDevIns;
    pVM->gim.s.pDbgR3    = pDbg;
}


/**
 * Gets debug setup specified by the provider.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pDbgSetup       Where to store the debug setup details.
 */
VMMR3DECL(int) GIMR3GetDebugSetup(PVM pVM, PGIMDEBUGSETUP pDbgSetup)
{
    AssertReturn(pVM, VERR_INVALID_PARAMETER);
    AssertReturn(pDbgSetup, VERR_INVALID_PARAMETER);

    switch (pVM->gim.s.enmProviderId)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
        case GIMPROVIDERID_HYPERV:
            return gimR3HvGetDebugSetup(pVM, pDbgSetup);
#endif
        default:
            break;
    }
    return VERR_GIM_NO_DEBUG_CONNECTION;
}


/**
 * Read data from a host debug session.
 *
 * @returns VBox status code.
 *
 * @param   pVM                 The cross context VM structure.
 * @param   pvRead              The read buffer.
 * @param   pcbRead             The size of the read buffer as well as where to store
 *                              the number of bytes read.
 * @param   pfnReadComplete     Callback when the buffer has been read and
 *                              before signalling reading of the next buffer.
 *                              Optional, can be NULL.
 * @thread  EMT.
 */
VMMR3_INT_DECL(int) gimR3DebugRead(PVM pVM, void *pvRead, size_t *pcbRead, PFNGIMDEBUGBUFREADCOMPLETED pfnReadComplete)
{
    PGIMDEBUG pDbg = pVM->gim.s.pDbgR3;
    if (pDbg)
    {
        if (ASMAtomicReadBool(&pDbg->fDbgRecvBufRead) == true)
        {
            STAM_REL_COUNTER_INC(&pVM->gim.s.StatDbgRecv);
            STAM_REL_COUNTER_ADD(&pVM->gim.s.StatDbgRecvBytes, pDbg->cbDbgRecvBufRead);

            memcpy(pvRead, pDbg->pvDbgRecvBuf, pDbg->cbDbgRecvBufRead);
            *pcbRead = pDbg->cbDbgRecvBufRead;
            if (pfnReadComplete)
                pfnReadComplete(pVM);
            RTSemEventMultiSignal(pDbg->hDbgRecvThreadSem);
            ASMAtomicWriteBool(&pDbg->fDbgRecvBufRead, false);
            return VINF_SUCCESS;
        }
        else
            *pcbRead = 0;
        return VERR_NO_DATA;
    }
    return VERR_GIM_NO_DEBUG_CONNECTION;
}


/**
 * Write data to a host debug session.
 *
 * @returns VBox status code.
 *
 * @param   pVM         The cross context VM structure.
 * @param   pvWrite     The write buffer.
 * @param   pcbWrite    The size of the write buffer as well as where to store
 *                      the number of bytes written.
 * @thread  EMT.
 */
VMMR3_INT_DECL(int) gimR3DebugWrite(PVM pVM, void *pvWrite, size_t *pcbWrite)
{
    PGIMDEBUG pDbg = pVM->gim.s.pDbgR3;
    if (pDbg)
    {
        PPDMISTREAM pDbgStream = pDbg->pDbgDrvStream;
        if (pDbgStream)
        {
            size_t cbWrite = *pcbWrite;
            int rc = pDbgStream->pfnWrite(pDbgStream, pvWrite, pcbWrite);
            if (   RT_SUCCESS(rc)
                && *pcbWrite == cbWrite)
            {
                STAM_REL_COUNTER_INC(&pVM->gim.s.StatDbgXmit);
                STAM_REL_COUNTER_ADD(&pVM->gim.s.StatDbgXmitBytes, *pcbWrite);
            }
            return rc;
        }
    }
    return VERR_GIM_NO_DEBUG_CONNECTION;
}

#if 0 /* ??? */

/**
 * @callback_method_impl{FNPGMPHYSHANDLER,
 *      Write access handler for mapped MMIO2 pages.  Currently ignores writes.}
 *
 * @todo In the future we might want to let the GIM provider decide what the
 *       handler should do (like throwing \#GP faults).
 */
static DECLCALLBACK(VBOXSTRICTRC) gimR3Mmio2WriteHandler(PVM pVM, PVMCPU pVCpu, RTGCPHYS GCPhys, void *pvPhys, void *pvBuf,
                                                         size_t cbBuf, PGMACCESSTYPE enmAccessType, PGMACCESSORIGIN enmOrigin,
                                                         void *pvUser)
{
    RT_NOREF6(pVM, pVCpu, GCPhys, pvPhys, pvBuf, cbBuf);
    RT_NOREF3(enmAccessType, enmOrigin, pvUser);

    /*
     * Ignore writes to the mapped MMIO2 page.
     */
    Assert(enmAccessType == PGMACCESSTYPE_WRITE);
    return VINF_SUCCESS;        /** @todo Hyper-V says we should \#GP(0) fault for writes to the Hypercall and TSC page. */
}


/**
 * Unmaps a registered MMIO2 region in the guest address space and removes any
 * access handlers for it.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pRegion     Pointer to the GIM MMIO2 region.
 */
VMMR3_INT_DECL(int) gimR3Mmio2Unmap(PVM pVM, PGIMMMIO2REGION pRegion)
{
    AssertPtr(pVM);
    AssertPtr(pRegion);

    PPDMDEVINS pDevIns = pVM->gim.s.pDevInsR3;
    AssertPtr(pDevIns);
    if (pRegion->fMapped)
    {
        int rc = PGMHandlerPhysicalDeregister(pVM, pRegion->GCPhysPage);
        AssertRC(rc);

        rc = PDMDevHlpMMIO2Unmap(pDevIns, pRegion->iRegion, pRegion->GCPhysPage);
        if (RT_SUCCESS(rc))
        {
            pRegion->fMapped    = false;
            pRegion->GCPhysPage = NIL_RTGCPHYS;
        }
    }
    return VINF_SUCCESS;
}


/**
 * Maps a registered MMIO2 region in the guest address space.
 *
 * The region will be made read-only and writes from the guest will be ignored.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pRegion         Pointer to the GIM MMIO2 region.
 * @param   GCPhysRegion    Where in the guest address space to map the region.
 */
VMMR3_INT_DECL(int) GIMR3Mmio2Map(PVM pVM, PGIMMMIO2REGION pRegion, RTGCPHYS GCPhysRegion)
{
    PPDMDEVINS pDevIns = pVM->gim.s.pDevInsR3;
    AssertPtr(pDevIns);

    /* The guest-physical address must be page-aligned. */
    if (GCPhysRegion & GUEST_PAGE_OFFSET_MASK)
    {
        LogFunc(("%s: %#RGp not paging aligned\n", pRegion->szDescription, GCPhysRegion));
        return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
    }

    /* Allow only normal pages to be overlaid using our MMIO2 pages (disallow MMIO, ROM, reserved pages). */
    /** @todo Hyper-V doesn't seem to be very strict about this, may be relax
     *        later if some guest really requires it. */
    if (!PGMPhysIsGCPhysNormal(pVM, GCPhysRegion))
    {
        LogFunc(("%s: %#RGp is not normal memory\n", pRegion->szDescription, GCPhysRegion));
        return VERR_PGM_INVALID_GC_PHYSICAL_ADDRESS;
    }

    if (!pRegion->fRegistered)
    {
        LogFunc(("%s: Region has not been registered.\n", pRegion->szDescription));
        return VERR_GIM_IPE_1;
    }

    /*
     * Map the MMIO2 region over the specified guest-physical address.
     */
    int rc = PDMDevHlpMMIOExMap(pDevIns, NULL, pRegion->iRegion, GCPhysRegion);
    if (RT_SUCCESS(rc))
    {
        /*
         * Install access-handlers for the mapped page to prevent (ignore) writes to it
         * from the guest.
         */
        if (pVM->gim.s.hSemiReadOnlyMmio2Handler == NIL_PGMPHYSHANDLERTYPE)
            rc = PGMR3HandlerPhysicalTypeRegister(pVM, PGMPHYSHANDLERKIND_WRITE,
                                                  gimR3Mmio2WriteHandler,
                                                  NULL /* pszModR0 */, NULL /* pszHandlerR0 */, NULL /* pszPfHandlerR0 */,
                                                  NULL /* pszModRC */, NULL /* pszHandlerRC */, NULL /* pszPfHandlerRC */,
                                                  "GIM read-only MMIO2 handler",
                                                  &pVM->gim.s.hSemiReadOnlyMmio2Handler);
        if (RT_SUCCESS(rc))
        {
            rc = PGMHandlerPhysicalRegister(pVM,  GCPhysRegion, GCPhysRegion + (pRegion->cbRegion - 1),
                                            pVM->gim.s.hSemiReadOnlyMmio2Handler,
                                            NULL /* pvUserR3 */, NIL_RTR0PTR /* pvUserR0 */, NIL_RTRCPTR /* pvUserRC */,
                                            pRegion->szDescription);
            if (RT_SUCCESS(rc))
            {
                pRegion->fMapped    = true;
                pRegion->GCPhysPage = GCPhysRegion;
                return rc;
            }
        }

        PDMDevHlpMMIO2Unmap(pDevIns, pRegion->iRegion, GCPhysRegion);
    }

    return rc;
}


/**
 * Registers the physical handler for the registered and mapped MMIO2 region.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pRegion     Pointer to the GIM MMIO2 region.
 */
VMMR3_INT_DECL(int) gimR3Mmio2HandlerPhysicalRegister(PVM pVM, PGIMMMIO2REGION pRegion)
{
    AssertPtr(pRegion);
    AssertReturn(pRegion->fRegistered, VERR_GIM_IPE_2);
    AssertReturn(pRegion->fMapped, VERR_GIM_IPE_3);

    return PGMR3HandlerPhysicalRegister(pVM,
                                        PGMPHYSHANDLERKIND_WRITE,
                                        pRegion->GCPhysPage, pRegion->GCPhysPage + (pRegion->cbRegion - 1),
                                        gimR3Mmio2WriteHandler,  NULL /* pvUserR3 */,
                                        NULL /* pszModR0 */, NULL /* pszHandlerR0 */, NIL_RTR0PTR /* pvUserR0 */,
                                        NULL /* pszModRC */, NULL /* pszHandlerRC */, NIL_RTRCPTR /* pvUserRC */,
                                        pRegion->szDescription);
}


/**
 * Deregisters the physical handler for the MMIO2 region.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pRegion     Pointer to the GIM MMIO2 region.
 */
VMMR3_INT_DECL(int) gimR3Mmio2HandlerPhysicalDeregister(PVM pVM, PGIMMMIO2REGION pRegion)
{
    return PGMHandlerPhysicalDeregister(pVM, pRegion->GCPhysPage);
}

#endif

