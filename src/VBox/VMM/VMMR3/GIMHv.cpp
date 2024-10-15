/* $Id: GIMHv.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GIM - Guest Interface Manager, Hyper-V implementation.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_GIM
#include <VBox/vmm/apic.h>
#include <VBox/vmm/gim.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/ssm.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/pdmapi.h>
#include <VBox/vmm/em.h>
#include "GIMInternal.h"
#include <VBox/vmm/vm.h>

#include <VBox/err.h>
#include <VBox/version.h>

#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/mem.h>
#include <iprt/semaphore.h>
#include <iprt/spinlock.h>
#include <iprt/zero.h>
#ifdef DEBUG_ramshankar
# include <iprt/udp.h>
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/**
 * GIM Hyper-V saved-state version.
 */
#define GIM_HV_SAVED_STATE_VERSION                      UINT32_C(4)
/** Saved states, priot to saving debug UDP source/destination ports.  */
#define GIM_HV_SAVED_STATE_VERSION_PRE_DEBUG_UDP_PORTS  UINT32_C(3)
/** Saved states, prior to any synthetic interrupt controller support. */
#define GIM_HV_SAVED_STATE_VERSION_PRE_SYNIC            UINT32_C(2)
/** Vanilla saved states, prior to any debug support. */
#define GIM_HV_SAVED_STATE_VERSION_PRE_DEBUG            UINT32_C(1)

#ifdef VBOX_WITH_STATISTICS
# define GIMHV_MSRRANGE(a_uFirst, a_uLast, a_szName) \
    { (a_uFirst), (a_uLast), kCpumMsrRdFn_Gim, kCpumMsrWrFn_Gim, 0, 0, 0, 0, 0, a_szName, { 0 }, { 0 }, { 0 }, { 0 } }
#else
# define GIMHV_MSRRANGE(a_uFirst, a_uLast, a_szName) \
    { (a_uFirst), (a_uLast), kCpumMsrRdFn_Gim, kCpumMsrWrFn_Gim, 0, 0, 0, 0, 0, a_szName }
#endif


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * Array of MSR ranges supported by Hyper-V.
 */
static CPUMMSRRANGE const g_aMsrRanges_HyperV[] =
{
    GIMHV_MSRRANGE(MSR_GIM_HV_RANGE0_FIRST,  MSR_GIM_HV_RANGE0_LAST,  "Hyper-V range 0"),
    GIMHV_MSRRANGE(MSR_GIM_HV_RANGE1_FIRST,  MSR_GIM_HV_RANGE1_LAST,  "Hyper-V range 1"),
    GIMHV_MSRRANGE(MSR_GIM_HV_RANGE2_FIRST,  MSR_GIM_HV_RANGE2_LAST,  "Hyper-V range 2"),
    GIMHV_MSRRANGE(MSR_GIM_HV_RANGE3_FIRST,  MSR_GIM_HV_RANGE3_LAST,  "Hyper-V range 3"),
    GIMHV_MSRRANGE(MSR_GIM_HV_RANGE4_FIRST,  MSR_GIM_HV_RANGE4_LAST,  "Hyper-V range 4"),
    GIMHV_MSRRANGE(MSR_GIM_HV_RANGE5_FIRST,  MSR_GIM_HV_RANGE5_LAST,  "Hyper-V range 5"),
    GIMHV_MSRRANGE(MSR_GIM_HV_RANGE6_FIRST,  MSR_GIM_HV_RANGE6_LAST,  "Hyper-V range 6"),
    GIMHV_MSRRANGE(MSR_GIM_HV_RANGE7_FIRST,  MSR_GIM_HV_RANGE7_LAST,  "Hyper-V range 7"),
    GIMHV_MSRRANGE(MSR_GIM_HV_RANGE8_FIRST,  MSR_GIM_HV_RANGE8_LAST,  "Hyper-V range 8"),
    GIMHV_MSRRANGE(MSR_GIM_HV_RANGE9_FIRST,  MSR_GIM_HV_RANGE9_LAST,  "Hyper-V range 9"),
    GIMHV_MSRRANGE(MSR_GIM_HV_RANGE10_FIRST, MSR_GIM_HV_RANGE10_LAST, "Hyper-V range 10"),
    GIMHV_MSRRANGE(MSR_GIM_HV_RANGE11_FIRST, MSR_GIM_HV_RANGE11_LAST, "Hyper-V range 11"),
    GIMHV_MSRRANGE(MSR_GIM_HV_RANGE12_FIRST, MSR_GIM_HV_RANGE12_LAST, "Hyper-V range 12")
};
#undef GIMHV_MSRRANGE

/**
 * DHCP OFFER packet response to the guest (client) over the Hyper-V debug
 * transport.
 *
 * - MAC: Destination: broadcast.
 * - MAC: Source: 00:00:00:00:01 (hypervisor). It's important that it's
 *   different from the client's MAC address which is all 0's.
 * - IP: Source: 10.0.5.1 (hypervisor)
 * - IP: Destination: broadcast.
 * - IP: Checksum included.
 * - BOOTP: Client IP address: 10.0.5.5.
 * - BOOTP: Server IP address: 10.0.5.1.
 * - DHCP options: Subnet mask, router, lease-time, DHCP server identifier.
 *   Options are kept to a minimum required for making Windows guests happy.
 */
#define GIMHV_DEBUGCLIENT_IPV4          RT_H2N_U32_C(0x0a000505)    /* 10.0.5.5 */
#define GIMHV_DEBUGSERVER_IPV4          RT_H2N_U32_C(0x0a000501)    /* 10.0.5.1 */
static const uint8_t g_abDhcpOffer[] =
{
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x45, 0x10,
    0x01, 0x28, 0x00, 0x00, 0x00, 0x00, 0x40, 0x11, 0x6a, 0xb5, 0x0a, 0x00, 0x05, 0x01, 0xff, 0xff,
    0xff, 0xff, 0x00, 0x43, 0x00, 0x44, 0x01, 0x14, 0x00, 0x00, 0x02, 0x01, 0x06, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x05, 0x05, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x63, 0x82, 0x53, 0x63, 0x35, 0x01, 0x02, 0x01, 0x04, 0xff,
    0xff, 0xff, 0x00, 0x03, 0x04, 0x0a, 0x00, 0x05, 0x01, 0x33, 0x04, 0xff, 0xff, 0xff, 0xff, 0x36,
    0x04, 0x0a, 0x00, 0x05, 0x01, 0xff
};

/**
 * DHCP ACK packet response to the guest (client) over the Hyper-V debug
 * transport.
 *
 * - MAC: Destination: 00:00:00:00:00 (client).
 * - IP: Destination: 10.0.5.5 (client).
 * - Rest are mostly similar to the DHCP offer.
 */
static const uint8_t g_abDhcpAck[] =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x00, 0x45, 0x10,
    0x01, 0x28, 0x00, 0x00, 0x00, 0x00, 0x40, 0x11, 0x5b, 0xb0, 0x0a, 0x00, 0x05, 0x01, 0x0a, 0x00,
    0x05, 0x05, 0x00, 0x43, 0x00, 0x44, 0x01, 0x14, 0x00, 0x00, 0x02, 0x01, 0x06, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x05, 0x05, 0x0a, 0x00, 0x05, 0x05, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x63, 0x82, 0x53, 0x63, 0x35, 0x01, 0x05, 0x01, 0x04, 0xff,
    0xff, 0xff, 0x00, 0x03, 0x04, 0x0a, 0x00, 0x05, 0x01, 0x33, 0x04, 0xff, 0xff, 0xff, 0xff, 0x36,
    0x04, 0x0a, 0x00, 0x05, 0x01, 0xff
};

/**
 * ARP reply to the guest (client) over the Hyper-V debug transport.
 *
 * - MAC: Destination: 00:00:00:00:00 (client)
 * - MAC: Source: 00:00:00:00:01 (hypervisor)
 * - ARP: Reply: 10.0.5.1 is at Source MAC address.
 */
static const uint8_t g_abArpReply[] =
{
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x01,
    0x08, 0x00, 0x06, 0x04, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x0a, 0x00, 0x05, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x00, 0x05, 0x05
};


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int    gimR3HvInitHypercallSupport(PVM pVM);
static void   gimR3HvTermHypercallSupport(PVM pVM);
static int    gimR3HvInitDebugSupport(PVM pVM);
static void   gimR3HvTermDebugSupport(PVM pVM);
static DECLCALLBACK(void) gimR3HvTimerCallback(PVM pVM, TMTIMERHANDLE pTimer, void *pvUser);

/**
 * Initializes the Hyper-V GIM provider.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pGimCfg     The GIM CFGM node.
 */
VMMR3_INT_DECL(int) gimR3HvInit(PVM pVM, PCFGMNODE pGimCfg)
{
    AssertReturn(pVM, VERR_INVALID_PARAMETER);
    AssertReturn(pVM->gim.s.enmProviderId == GIMPROVIDERID_HYPERV, VERR_INTERNAL_ERROR_5);

    PGIMHV pHv = &pVM->gim.s.u.Hv;

    /*
     * Initialize timer handles and such.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU       pVCpu     = pVM->apCpusR3[idCpu];
        PGIMHVCPU    pHvCpu    = &pVCpu->gim.s.u.HvCpu;
        for (uint8_t idxStimer = 0; idxStimer < RT_ELEMENTS(pHvCpu->aStimers); idxStimer++)
            pHvCpu->aStimers[idxStimer].hTimer = NIL_TMTIMERHANDLE;
    }

    /*
     * Read configuration.
     */
    PCFGMNODE pCfgHv = CFGMR3GetChild(pGimCfg, "HyperV");
    if (pCfgHv)
    {
        /*
         * Validate the Hyper-V settings.
         */
        int rc2 = CFGMR3ValidateConfig(pCfgHv, "/HyperV/",
                                  "VendorID"
                                  "|VSInterface"
                                  "|HypercallDebugInterface",
                                  "" /* pszValidNodes */, "GIM/HyperV" /* pszWho */, 0 /* uInstance */);
        if (RT_FAILURE(rc2))
            return rc2;
    }

    /** @cfgm{/GIM/HyperV/VendorID, string, 'VBoxVBoxVBox'}
     * The Hyper-V vendor signature, must be 12 characters. */
    char szVendor[13];
    int rc = CFGMR3QueryStringDef(pCfgHv, "VendorID", szVendor, sizeof(szVendor), "VBoxVBoxVBox");
    AssertLogRelRCReturn(rc, rc);
    AssertLogRelMsgReturn(strlen(szVendor) == 12,
                          ("The VendorID config value must be exactly 12 chars, '%s' isn't!\n", szVendor),
                          VERR_INVALID_PARAMETER);

    LogRel(("GIM: HyperV: Reporting vendor as '%s'\n", szVendor));
    /** @todo r=bird: GIM_HV_VENDOR_MICROSOFT is 12 char and the string is max
     *        12+terminator, so the NCmp is a little bit misleading. */
    if (!RTStrNCmp(szVendor, GIM_HV_VENDOR_MICROSOFT, sizeof(GIM_HV_VENDOR_MICROSOFT) - 1))
    {
        LogRel(("GIM: HyperV: Warning! Posing as the Microsoft vendor may alter guest behaviour!\n"));
        pHv->fIsVendorMsHv = true;
    }

    /** @cfgm{/GIM/HyperV/VSInterface, bool, true}
     * The Microsoft virtualization service interface (debugging). */
    rc = CFGMR3QueryBoolDef(pCfgHv, "VSInterface", &pHv->fIsInterfaceVs, false);
    AssertLogRelRCReturn(rc, rc);

    /** @cfgm{/GIM/HyperV/HypercallDebugInterface, bool, false}
     * Whether we specify the guest to use hypercalls for debugging rather than MSRs. */
    rc = CFGMR3QueryBoolDef(pCfgHv, "HypercallDebugInterface", &pHv->fDbgHypercallInterface, false);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Determine interface capabilities based on the version.
     */
    if (!pVM->gim.s.u32Version)
    {
        /* Basic features. */
        pHv->uBaseFeat = 0
                     //| GIM_HV_BASE_FEAT_VP_RUNTIME_MSR
                       | GIM_HV_BASE_FEAT_PART_TIME_REF_COUNT_MSR
                     //| GIM_HV_BASE_FEAT_BASIC_SYNIC_MSRS          // Both required for synethetic timers
                     //| GIM_HV_BASE_FEAT_STIMER_MSRS               // Both required for synethetic timers
                       | GIM_HV_BASE_FEAT_APIC_ACCESS_MSRS
                       | GIM_HV_BASE_FEAT_HYPERCALL_MSRS
                       | GIM_HV_BASE_FEAT_VP_ID_MSR
                       | GIM_HV_BASE_FEAT_VIRT_SYS_RESET_MSR
                     //| GIM_HV_BASE_FEAT_STAT_PAGES_MSR
                       | GIM_HV_BASE_FEAT_PART_REF_TSC_MSR
                     //| GIM_HV_BASE_FEAT_GUEST_IDLE_STATE_MSR
                       | GIM_HV_BASE_FEAT_TIMER_FREQ_MSRS
                     //| GIM_HV_BASE_FEAT_DEBUG_MSRS
                       ;

        /* Miscellaneous features. */
        pHv->uMiscFeat = 0
                       //| GIM_HV_MISC_FEAT_GUEST_DEBUGGING
                       //| GIM_HV_MISC_FEAT_XMM_HYPERCALL_INPUT
                         | GIM_HV_MISC_FEAT_TIMER_FREQ
                         | GIM_HV_MISC_FEAT_GUEST_CRASH_MSRS
                       //| GIM_HV_MISC_FEAT_DEBUG_MSRS
                         ;

        /* Hypervisor recommendations to the guest. */
        pHv->uHyperHints = GIM_HV_HINT_MSR_FOR_SYS_RESET
                         | GIM_HV_HINT_RELAX_TIME_CHECKS
                         | GIM_HV_HINT_X2APIC_MSRS
                         ;

        /* Partition features. */
        pHv->uPartFlags |= GIM_HV_PART_FLAGS_EXTENDED_HYPERCALLS;

        /* Expose more if we're posing as Microsoft. We can, if needed, force MSR-based Hv
           debugging by not exposing these bits while exposing the VS interface. The better
           way is what we do currently, via the GIM_HV_DEBUG_OPTIONS_USE_HYPERCALLS bit. */
        if (pHv->fIsVendorMsHv)
        {
            pHv->uMiscFeat  |= GIM_HV_MISC_FEAT_GUEST_DEBUGGING
                            |  GIM_HV_MISC_FEAT_DEBUG_MSRS;

            pHv->uPartFlags |= GIM_HV_PART_FLAGS_DEBUGGING;
        }
    }

    /*
     * Populate the required fields in MMIO2 region records for registering.
     */
    for (size_t i = 0; i < RT_ELEMENTS(pHv->aMmio2Regions); i++)
        pHv->aMmio2Regions[i].hMmio2 = NIL_PGMMMIO2HANDLE;

    AssertCompile(GIM_HV_PAGE_SIZE == GUEST_PAGE_SIZE);
    PGIMMMIO2REGION pRegion = &pHv->aMmio2Regions[GIM_HV_HYPERCALL_PAGE_REGION_IDX];
    pRegion->iRegion    = GIM_HV_HYPERCALL_PAGE_REGION_IDX;
    pRegion->fRCMapping = false;
    pRegion->cbRegion   = GIM_HV_PAGE_SIZE; /* Sanity checked in gimR3HvLoad(), gimR3HvEnableTscPage() & gimR3HvEnableHypercallPage() */
    pRegion->GCPhysPage = NIL_RTGCPHYS;
    RTStrCopy(pRegion->szDescription, sizeof(pRegion->szDescription), "Hyper-V hypercall page");

    pRegion = &pHv->aMmio2Regions[GIM_HV_REF_TSC_PAGE_REGION_IDX];
    pRegion->iRegion    = GIM_HV_REF_TSC_PAGE_REGION_IDX;
    pRegion->fRCMapping = false;
    pRegion->cbRegion   = GIM_HV_PAGE_SIZE; /* Sanity checked in gimR3HvLoad(), gimR3HvEnableTscPage() & gimR3HvEnableHypercallPage() */
    pRegion->GCPhysPage = NIL_RTGCPHYS;
    RTStrCopy(pRegion->szDescription, sizeof(pRegion->szDescription), "Hyper-V TSC page");

    /*
     * Make sure the CPU ID bit are in accordance with the Hyper-V
     * requirement and other paranoia checks.
     * See "Requirements for implementing the Microsoft hypervisor interface" spec.
     */
    Assert(!(pHv->uPartFlags & (  GIM_HV_PART_FLAGS_CREATE_PART
                                | GIM_HV_PART_FLAGS_ACCESS_MEMORY_POOL
                                | GIM_HV_PART_FLAGS_ACCESS_PART_ID
                                | GIM_HV_PART_FLAGS_ADJUST_MSG_BUFFERS
                                | GIM_HV_PART_FLAGS_CREATE_PORT
                                | GIM_HV_PART_FLAGS_ACCESS_STATS
                                | GIM_HV_PART_FLAGS_CPU_MGMT
                                | GIM_HV_PART_FLAGS_CPU_PROFILER)));
    Assert((pHv->uBaseFeat & (GIM_HV_BASE_FEAT_HYPERCALL_MSRS | GIM_HV_BASE_FEAT_VP_ID_MSR))
                          == (GIM_HV_BASE_FEAT_HYPERCALL_MSRS | GIM_HV_BASE_FEAT_VP_ID_MSR));
#ifdef VBOX_STRICT
    for (unsigned i = 0; i < RT_ELEMENTS(pHv->aMmio2Regions); i++)
    {
        PCGIMMMIO2REGION pCur = &pHv->aMmio2Regions[i];
        Assert(!pCur->fRCMapping);
        Assert(!pCur->fMapped);
        Assert(pCur->GCPhysPage == NIL_RTGCPHYS);
    }
#endif

    /*
     * Expose HVP (Hypervisor Present) bit to the guest.
     */
    CPUMR3SetGuestCpuIdFeature(pVM, CPUMCPUIDFEATURE_HVP);

    /*
     * Modify the standard hypervisor leaves for Hyper-V.
     */
    CPUMCPUIDLEAF HyperLeaf;
    RT_ZERO(HyperLeaf);
    HyperLeaf.uLeaf        = UINT32_C(0x40000000);
    if (   pHv->fIsVendorMsHv
        && pHv->fIsInterfaceVs)
        HyperLeaf.uEax     = UINT32_C(0x40000082); /* Since we expose 0x40000082 below for the Hyper-V PV-debugging case. */
    else
        HyperLeaf.uEax     = UINT32_C(0x40000006); /* Minimum value for Hyper-V default is 0x40000005. */
    /*
     * Don't report vendor as 'Microsoft Hv'[1] by default, see @bugref{7270#c152}.
     * [1]: ebx=0x7263694d ('rciM') ecx=0x666f736f ('foso') edx=0x76482074 ('vH t')
     */
    {
        uint32_t uVendorEbx;
        uint32_t uVendorEcx;
        uint32_t uVendorEdx;
        uVendorEbx = ((uint32_t)szVendor[ 3]) << 24 | ((uint32_t)szVendor[ 2]) << 16 | ((uint32_t)szVendor[1]) << 8
                    | (uint32_t)szVendor[ 0];
        uVendorEcx = ((uint32_t)szVendor[ 7]) << 24 | ((uint32_t)szVendor[ 6]) << 16 | ((uint32_t)szVendor[5]) << 8
                    | (uint32_t)szVendor[ 4];
        uVendorEdx = ((uint32_t)szVendor[11]) << 24 | ((uint32_t)szVendor[10]) << 16 | ((uint32_t)szVendor[9]) << 8
                    | (uint32_t)szVendor[ 8];
        HyperLeaf.uEbx         = uVendorEbx;
        HyperLeaf.uEcx         = uVendorEcx;
        HyperLeaf.uEdx         = uVendorEdx;
    }
    rc = CPUMR3CpuIdInsert(pVM, &HyperLeaf);
    AssertLogRelRCReturn(rc, rc);

    HyperLeaf.uLeaf        = UINT32_C(0x40000001);
    HyperLeaf.uEax         = 0x31237648;           /* 'Hv#1' */
    HyperLeaf.uEbx         = 0;                    /* Reserved */
    HyperLeaf.uEcx         = 0;                    /* Reserved */
    HyperLeaf.uEdx         = 0;                    /* Reserved */
    rc = CPUMR3CpuIdInsert(pVM, &HyperLeaf);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Add Hyper-V specific leaves.
     */
    HyperLeaf.uLeaf        = UINT32_C(0x40000002); /* MBZ until MSR_GIM_HV_GUEST_OS_ID is set by the guest. */
    HyperLeaf.uEax         = 0;
    HyperLeaf.uEbx         = 0;
    HyperLeaf.uEcx         = 0;
    HyperLeaf.uEdx         = 0;
    rc = CPUMR3CpuIdInsert(pVM, &HyperLeaf);
    AssertLogRelRCReturn(rc, rc);

    HyperLeaf.uLeaf        = UINT32_C(0x40000003);
    HyperLeaf.uEax         = pHv->uBaseFeat;
    HyperLeaf.uEbx         = pHv->uPartFlags;
    HyperLeaf.uEcx         = pHv->uPowMgmtFeat;
    HyperLeaf.uEdx         = pHv->uMiscFeat;
    rc = CPUMR3CpuIdInsert(pVM, &HyperLeaf);
    AssertLogRelRCReturn(rc, rc);

    HyperLeaf.uLeaf        = UINT32_C(0x40000004);
    HyperLeaf.uEax         = pHv->uHyperHints;
    HyperLeaf.uEbx         = 0xffffffff;
    HyperLeaf.uEcx         = 0;
    HyperLeaf.uEdx         = 0;
    rc = CPUMR3CpuIdInsert(pVM, &HyperLeaf);
    AssertLogRelRCReturn(rc, rc);

    RT_ZERO(HyperLeaf);
    HyperLeaf.uLeaf        = UINT32_C(0x40000005);
    rc = CPUMR3CpuIdInsert(pVM, &HyperLeaf);
    AssertLogRelRCReturn(rc, rc);

    /* Leaf 0x40000006 is inserted in gimR3HvInitCompleted(). */

    if (   pHv->fIsVendorMsHv
        && pHv->fIsInterfaceVs)
    {
        HyperLeaf.uLeaf        = UINT32_C(0x40000080);
        HyperLeaf.uEax         = 0;
        HyperLeaf.uEbx         = 0x7263694d;        /* 'rciM' */
        HyperLeaf.uEcx         = 0x666f736f;        /* 'foso'*/
        HyperLeaf.uEdx         = 0x53562074;        /* 'SV t' */
        rc = CPUMR3CpuIdInsert(pVM, &HyperLeaf);
        AssertLogRelRCReturn(rc, rc);

        HyperLeaf.uLeaf        = UINT32_C(0x40000081);
        HyperLeaf.uEax         = 0x31235356;        /* '1#SV' */
        HyperLeaf.uEbx         = 0;
        HyperLeaf.uEcx         = 0;
        HyperLeaf.uEdx         = 0;
        rc = CPUMR3CpuIdInsert(pVM, &HyperLeaf);
        AssertLogRelRCReturn(rc, rc);

        HyperLeaf.uLeaf        = UINT32_C(0x40000082);
        HyperLeaf.uEax         = RT_BIT_32(1);
        HyperLeaf.uEbx         = 0;
        HyperLeaf.uEcx         = 0;
        HyperLeaf.uEdx         = 0;
        rc = CPUMR3CpuIdInsert(pVM, &HyperLeaf);
        AssertLogRelRCReturn(rc, rc);
    }

    /*
     * Insert all MSR ranges of Hyper-V.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(g_aMsrRanges_HyperV); i++)
    {
        int rc2 = CPUMR3MsrRangesInsert(pVM, &g_aMsrRanges_HyperV[i]);
        AssertLogRelRCReturn(rc2, rc2);
    }

    /*
     * Setup non-zero MSRs.
     */
    if (pHv->uMiscFeat & GIM_HV_MISC_FEAT_GUEST_CRASH_MSRS)
        pHv->uCrashCtlMsr = MSR_GIM_HV_CRASH_CTL_NOTIFY;
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PGIMHVCPU pHvCpu = &pVM->apCpusR3[idCpu]->gim.s.u.HvCpu;
        for (uint8_t idxSintMsr = 0; idxSintMsr < RT_ELEMENTS(pHvCpu->auSintMsrs); idxSintMsr++)
            pHvCpu->auSintMsrs[idxSintMsr] = MSR_GIM_HV_SINT_MASKED;
    }

    /*
     * Setup hypercall support.
     */
    rc = gimR3HvInitHypercallSupport(pVM);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Setup debug support.
     */
    rc = gimR3HvInitDebugSupport(pVM);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Setup up the per-VCPU synthetic timers.
     */
    if (   (pHv->uBaseFeat & GIM_HV_BASE_FEAT_STIMER_MSRS)
        || (pHv->uBaseFeat & GIM_HV_BASE_FEAT_BASIC_SYNIC_MSRS))
    {
        for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        {
            PVMCPU       pVCpu     = pVM->apCpusR3[idCpu];
            PGIMHVCPU    pHvCpu    = &pVCpu->gim.s.u.HvCpu;

            for (uint8_t idxStimer = 0; idxStimer < RT_ELEMENTS(pHvCpu->aStimers); idxStimer++)
            {
                PGIMHVSTIMER pHvStimer = &pHvCpu->aStimers[idxStimer];

                /* Associate the synthetic timer with its corresponding VCPU. */
                pHvStimer->idCpu     = pVCpu->idCpu;
                pHvStimer->idxStimer = idxStimer;

                /* Create the timer and associate the context pointers. */
                char szName[32];
                RTStrPrintf(szName, sizeof(szName), "Hyper-V[%u] Timer%u", pVCpu->idCpu, idxStimer);
                rc = TMR3TimerCreate(pVM, TMCLOCK_VIRTUAL_SYNC, gimR3HvTimerCallback, pHvStimer /* pvUser */,
                                     TMTIMER_FLAGS_RING0, szName, &pHvStimer->hTimer);
                AssertLogRelRCReturn(rc, rc);
            }
        }
    }

    /*
     * Register statistics.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU    pVCpu  = pVM->apCpusR3[idCpu];
        PGIMHVCPU pHvCpu = &pVCpu->gim.s.u.HvCpu;

        for (size_t idxStimer = 0; idxStimer < RT_ELEMENTS(pHvCpu->aStatStimerFired); idxStimer++)
        {
            int rc2 = STAMR3RegisterF(pVM, &pHvCpu->aStatStimerFired[idxStimer], STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS,
                                     STAMUNIT_OCCURENCES, "Number of times the synthetic timer fired.",
                                     "/GIM/HyperV/%u/Stimer%u_Fired", idCpu, idxStimer);
            AssertLogRelRCReturn(rc2, rc2);
        }
    }

    return VINF_SUCCESS;
}


/**
 * Initializes remaining bits of the Hyper-V provider.
 *
 * This is called after initializing HM and almost all other VMM components.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(int) gimR3HvInitCompleted(PVM pVM)
{
    PGIMHV pHv = &pVM->gim.s.u.Hv;
    pHv->cTscTicksPerSecond = TMCpuTicksPerSecond(pVM);

    /*
     * Determine interface capabilities based on the version.
     */
    if (!pVM->gim.s.u32Version)
    {
        /* Hypervisor capabilities; features used by the hypervisor. */
        pHv->uHyperCaps  = HMIsNestedPagingActive(pVM) ? GIM_HV_HOST_FEAT_NESTED_PAGING : 0;
        pHv->uHyperCaps |= HMIsMsrBitmapActive(pVM)    ? GIM_HV_HOST_FEAT_MSR_BITMAP    : 0;
    }

    CPUMCPUIDLEAF HyperLeaf;
    RT_ZERO(HyperLeaf);
    HyperLeaf.uLeaf        = UINT32_C(0x40000006);
    HyperLeaf.uEax         = pHv->uHyperCaps;
    HyperLeaf.uEbx         = 0;
    HyperLeaf.uEcx         = 0;
    HyperLeaf.uEdx         = 0;
    int rc = CPUMR3CpuIdInsert(pVM, &HyperLeaf);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Inform APIC whether Hyper-V compatibility mode is enabled or not.
     * Do this here rather than on gimR3HvInit() as it gets called after APIC
     * has finished inserting/removing the x2APIC MSR range.
     */
    if (pHv->uHyperHints & GIM_HV_HINT_X2APIC_MSRS)
        APICR3HvSetCompatMode(pVM, true);

    return rc;
}


/**
 * Terminates the Hyper-V GIM provider.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) gimR3HvTerm(PVM pVM)
{
    gimR3HvReset(pVM);
    gimR3HvTermHypercallSupport(pVM);
    gimR3HvTermDebugSupport(pVM);

    PCGIMHV pHv = &pVM->gim.s.u.Hv;
    if (   (pHv->uBaseFeat & GIM_HV_BASE_FEAT_STIMER_MSRS)
        || (pHv->uBaseFeat & GIM_HV_BASE_FEAT_BASIC_SYNIC_MSRS))
    {
        for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        {
            PGIMHVCPU pHvCpu = &pVM->apCpusR3[idCpu]->gim.s.u.HvCpu;
            for (uint8_t idxStimer = 0; idxStimer < RT_ELEMENTS(pHvCpu->aStimers); idxStimer++)
            {
                PGIMHVSTIMER pHvStimer = &pHvCpu->aStimers[idxStimer];
                TMR3TimerDestroy(pVM, pHvStimer->hTimer);
                pHvStimer->hTimer = NIL_TMTIMERHANDLE;
            }
        }
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
VMMR3_INT_DECL(void) gimR3HvRelocate(PVM pVM, RTGCINTPTR offDelta)
{
    RT_NOREF(pVM, offDelta);
}


/**
 * This resets Hyper-V provider MSRs and unmaps whatever Hyper-V regions that
 * the guest may have mapped.
 *
 * This is called when the VM is being reset.
 *
 * @param   pVM     The cross context VM structure.
 *
 * @thread  EMT(0)
 */
VMMR3_INT_DECL(void) gimR3HvReset(PVM pVM)
{
    VM_ASSERT_EMT0(pVM);

    /*
     * Unmap MMIO2 pages that the guest may have setup.
     */
    LogRel(("GIM: HyperV: Resetting MMIO2 regions and MSRs\n"));
    PGIMHV pHv = &pVM->gim.s.u.Hv;
    for (unsigned i = 0; i < RT_ELEMENTS(pHv->aMmio2Regions); i++)
    {
        PGIMMMIO2REGION pRegion = &pHv->aMmio2Regions[i];
#if 0
        gimR3Mmio2Unmap(pVM, pRegion);
#else
        pRegion->fMapped    = false;
        pRegion->GCPhysPage = NIL_RTGCPHYS;
#endif
    }

    /*
     * Reset MSRs.
     */
    pHv->u64GuestOsIdMsr      = 0;
    pHv->u64HypercallMsr      = 0;
    pHv->u64TscPageMsr        = 0;
    pHv->uCrashP0Msr          = 0;
    pHv->uCrashP1Msr          = 0;
    pHv->uCrashP2Msr          = 0;
    pHv->uCrashP3Msr          = 0;
    pHv->uCrashP4Msr          = 0;
    pHv->uDbgStatusMsr        = 0;
    pHv->uDbgPendingBufferMsr = 0;
    pHv->uDbgSendBufferMsr    = 0;
    pHv->uDbgRecvBufferMsr    = 0;
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PGIMHVCPU pHvCpu = &pVM->apCpusR3[idCpu]->gim.s.u.HvCpu;
        pHvCpu->uSControlMsr = 0;
        pHvCpu->uSimpMsr  = 0;
        pHvCpu->uSiefpMsr = 0;
        pHvCpu->uApicAssistPageMsr = 0;

        for (uint8_t idxSint = 0; idxSint < RT_ELEMENTS(pHvCpu->auSintMsrs); idxSint++)
            pHvCpu->auSintMsrs[idxSint] = MSR_GIM_HV_SINT_MASKED;

        for (uint8_t idxStimer = 0; idxStimer < RT_ELEMENTS(pHvCpu->aStimers); idxStimer++)
        {
            PGIMHVSTIMER pHvStimer = &pHvCpu->aStimers[idxStimer];
            pHvStimer->uStimerConfigMsr = 0;
            pHvStimer->uStimerCountMsr  = 0;
        }
    }
}


/**
 * Callback for when debug data is available over the debugger connection.
 *
 * @param pVM       The cross context VM structure.
 */
static DECLCALLBACK(void) gimR3HvDebugBufAvail(PVM pVM)
{
    PGIMHV   pHv = &pVM->gim.s.u.Hv;
    RTGCPHYS GCPhysPendingBuffer = pHv->uDbgPendingBufferMsr;
    if (   GCPhysPendingBuffer
        && PGMPhysIsGCPhysNormal(pVM, GCPhysPendingBuffer))
    {
        uint8_t bPendingData = 1;
        int rc = PGMPhysSimpleWriteGCPhys(pVM, GCPhysPendingBuffer, &bPendingData, sizeof(bPendingData));
        if (RT_FAILURE(rc))
        {
            LogRelMax(5, ("GIM: HyperV: Failed to set pending debug receive buffer at %#RGp, rc=%Rrc\n", GCPhysPendingBuffer,
                          rc));
        }
    }
}


/**
 * Callback for when debug data has been read from the debugger connection.
 *
 * This will be invoked before signalling read of the next debug buffer.
 *
 * @param pVM       The cross context VM structure.
 */
static DECLCALLBACK(void) gimR3HvDebugBufReadCompleted(PVM pVM)
{
    PGIMHV   pHv = &pVM->gim.s.u.Hv;
    RTGCPHYS GCPhysPendingBuffer = pHv->uDbgPendingBufferMsr;
    if (   GCPhysPendingBuffer
        && PGMPhysIsGCPhysNormal(pVM, GCPhysPendingBuffer))
    {
        uint8_t bPendingData = 0;
        int rc = PGMPhysSimpleWriteGCPhys(pVM, GCPhysPendingBuffer, &bPendingData, sizeof(bPendingData));
        if (RT_FAILURE(rc))
        {
            LogRelMax(5, ("GIM: HyperV: Failed to clear pending debug receive buffer at %#RGp, rc=%Rrc\n", GCPhysPendingBuffer,
                          rc));
        }
    }
}


/**
 * Get Hyper-V debug setup parameters.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pDbgSetup   Where to store the debug setup details.
 */
VMMR3_INT_DECL(int) gimR3HvGetDebugSetup(PVM pVM, PGIMDEBUGSETUP pDbgSetup)
{
    Assert(pDbgSetup);
    PGIMHV pHv = &pVM->gim.s.u.Hv;
    if (pHv->fDbgEnabled)
    {
        pDbgSetup->pfnDbgRecvBufAvail = gimR3HvDebugBufAvail;
        pDbgSetup->cbDbgRecvBuf       = GIM_HV_PAGE_SIZE;
        return VINF_SUCCESS;
    }
    return VERR_GIM_NO_DEBUG_CONNECTION;
}


/**
 * Hyper-V state-save operation.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 * @param   pSSM    The saved state handle.
 */
VMMR3_INT_DECL(int) gimR3HvSave(PVM pVM, PSSMHANDLE pSSM)
{
    PCGIMHV pHv = &pVM->gim.s.u.Hv;

    /*
     * Save the Hyper-V SSM version.
     */
    SSMR3PutU32(pSSM, GIM_HV_SAVED_STATE_VERSION);

    /*
     * Save per-VM MSRs.
     */
    SSMR3PutU64(pSSM, pHv->u64GuestOsIdMsr);
    SSMR3PutU64(pSSM, pHv->u64HypercallMsr);
    SSMR3PutU64(pSSM, pHv->u64TscPageMsr);

    /*
     * Save Hyper-V features / capabilities.
     */
    SSMR3PutU32(pSSM, pHv->uBaseFeat);
    SSMR3PutU32(pSSM, pHv->uPartFlags);
    SSMR3PutU32(pSSM, pHv->uPowMgmtFeat);
    SSMR3PutU32(pSSM, pHv->uMiscFeat);
    SSMR3PutU32(pSSM, pHv->uHyperHints);
    SSMR3PutU32(pSSM, pHv->uHyperCaps);

    /*
     * Save the Hypercall region.
     */
    PCGIMMMIO2REGION pRegion = &pHv->aMmio2Regions[GIM_HV_HYPERCALL_PAGE_REGION_IDX];
    SSMR3PutU8(pSSM,     pRegion->iRegion);
    SSMR3PutBool(pSSM,   pRegion->fRCMapping);
    SSMR3PutU32(pSSM,    pRegion->cbRegion);
    SSMR3PutGCPhys(pSSM, pRegion->GCPhysPage);
    SSMR3PutStrZ(pSSM,   pRegion->szDescription);

    /*
     * Save the reference TSC region.
     */
    pRegion = &pHv->aMmio2Regions[GIM_HV_REF_TSC_PAGE_REGION_IDX];
    SSMR3PutU8(pSSM,     pRegion->iRegion);
    SSMR3PutBool(pSSM,   pRegion->fRCMapping);
    SSMR3PutU32(pSSM,    pRegion->cbRegion);
    SSMR3PutGCPhys(pSSM, pRegion->GCPhysPage);
    SSMR3PutStrZ(pSSM,   pRegion->szDescription);
    /* Save the TSC sequence so we can bump it on restore (as the CPU frequency/offset may change). */
    uint32_t uTscSequence = 0;
    if (   pRegion->fMapped
        && MSR_GIM_HV_REF_TSC_IS_ENABLED(pHv->u64TscPageMsr))
    {
        PCGIMHVREFTSC pRefTsc = (PCGIMHVREFTSC)pRegion->pvPageR3;
        uTscSequence = pRefTsc->u32TscSequence;
    }
    SSMR3PutU32(pSSM, uTscSequence);

    /*
     * Save debug support data.
     */
    SSMR3PutU64(pSSM, pHv->uDbgPendingBufferMsr);
    SSMR3PutU64(pSSM, pHv->uDbgSendBufferMsr);
    SSMR3PutU64(pSSM, pHv->uDbgRecvBufferMsr);
    SSMR3PutU64(pSSM, pHv->uDbgStatusMsr);
    SSMR3PutU32(pSSM, pHv->enmDbgReply);
    SSMR3PutU32(pSSM, pHv->uDbgBootpXId);
    SSMR3PutU32(pSSM, pHv->DbgGuestIp4Addr.u);
    SSMR3PutU16(pSSM, pHv->uUdpGuestDstPort);
    SSMR3PutU16(pSSM, pHv->uUdpGuestSrcPort);

    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PGIMHVCPU pHvCpu = &pVM->apCpusR3[idCpu]->gim.s.u.HvCpu;
        SSMR3PutU64(pSSM, pHvCpu->uSimpMsr);
        for (size_t idxSintMsr = 0; idxSintMsr < RT_ELEMENTS(pHvCpu->auSintMsrs); idxSintMsr++)
            SSMR3PutU64(pSSM, pHvCpu->auSintMsrs[idxSintMsr]);
    }

    return SSMR3PutU8(pSSM, UINT8_MAX);
}


/**
 * Hyper-V state-load operation, final pass.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            The saved state handle.
 */
VMMR3_INT_DECL(int) gimR3HvLoad(PVM pVM, PSSMHANDLE pSSM)
{
    /*
     * Load the Hyper-V SSM version first.
     */
    uint32_t uHvSavedStateVersion;
    int rc = SSMR3GetU32(pSSM, &uHvSavedStateVersion);
    AssertRCReturn(rc, rc);
    if (   uHvSavedStateVersion != GIM_HV_SAVED_STATE_VERSION
        && uHvSavedStateVersion != GIM_HV_SAVED_STATE_VERSION_PRE_DEBUG_UDP_PORTS
        && uHvSavedStateVersion != GIM_HV_SAVED_STATE_VERSION_PRE_SYNIC
        && uHvSavedStateVersion != GIM_HV_SAVED_STATE_VERSION_PRE_DEBUG)
        return SSMR3SetLoadError(pSSM, VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION, RT_SRC_POS,
                                 N_("Unsupported Hyper-V saved-state version %u (current %u)!"),
                                 uHvSavedStateVersion, GIM_HV_SAVED_STATE_VERSION);

    /*
     * Update the TSC frequency from TM.
     */
    PGIMHV pHv = &pVM->gim.s.u.Hv;
    pHv->cTscTicksPerSecond = TMCpuTicksPerSecond(pVM);

    /*
     * Load per-VM MSRs.
     */
    SSMR3GetU64(pSSM, &pHv->u64GuestOsIdMsr);
    SSMR3GetU64(pSSM, &pHv->u64HypercallMsr);
    SSMR3GetU64(pSSM, &pHv->u64TscPageMsr);

    /*
     * Load Hyper-V features / capabilities.
     */
    SSMR3GetU32(pSSM, &pHv->uBaseFeat);
    SSMR3GetU32(pSSM, &pHv->uPartFlags);
    SSMR3GetU32(pSSM, &pHv->uPowMgmtFeat);
    SSMR3GetU32(pSSM, &pHv->uMiscFeat);
    SSMR3GetU32(pSSM, &pHv->uHyperHints);
    SSMR3GetU32(pSSM, &pHv->uHyperCaps);

    /*
     * Load and enable the Hypercall region.
     */
    PGIMMMIO2REGION pRegion = &pHv->aMmio2Regions[GIM_HV_HYPERCALL_PAGE_REGION_IDX];
    SSMR3GetU8(pSSM,     &pRegion->iRegion);
    SSMR3GetBool(pSSM,   &pRegion->fRCMapping);
    SSMR3GetU32(pSSM,    &pRegion->cbRegion);
    SSMR3GetGCPhys(pSSM, &pRegion->GCPhysPage);
    rc = SSMR3GetStrZ(pSSM, pRegion->szDescription, sizeof(pRegion->szDescription));
    AssertRCReturn(rc, rc);

    if (pRegion->cbRegion != GIM_HV_PAGE_SIZE)
        return SSMR3SetCfgError(pSSM, RT_SRC_POS, N_("Hypercall page region size %#x invalid, expected %#x"),
                                pRegion->cbRegion, GIM_HV_PAGE_SIZE);

    if (MSR_GIM_HV_HYPERCALL_PAGE_IS_ENABLED(pHv->u64HypercallMsr))
    {
        Assert(pRegion->GCPhysPage != NIL_RTGCPHYS);
        if (RT_LIKELY(pRegion->fRegistered))
        {
            rc = gimR3HvEnableHypercallPage(pVM, pRegion->GCPhysPage);
            if (RT_FAILURE(rc))
                return SSMR3SetCfgError(pSSM, RT_SRC_POS, N_("Failed to enable the hypercall page. GCPhys=%#RGp rc=%Rrc"),
                                        pRegion->GCPhysPage, rc);
        }
        else
            return SSMR3SetCfgError(pSSM, RT_SRC_POS, N_("Hypercall MMIO2 region not registered. Missing GIM device?!"));
    }

    /*
     * Load and enable the reference TSC region.
     */
    uint32_t uTscSequence;
    pRegion = &pHv->aMmio2Regions[GIM_HV_REF_TSC_PAGE_REGION_IDX];
    SSMR3GetU8(pSSM,     &pRegion->iRegion);
    SSMR3GetBool(pSSM,   &pRegion->fRCMapping);
    SSMR3GetU32(pSSM,    &pRegion->cbRegion);
    SSMR3GetGCPhys(pSSM, &pRegion->GCPhysPage);
    SSMR3GetStrZ(pSSM,    pRegion->szDescription, sizeof(pRegion->szDescription));
    rc = SSMR3GetU32(pSSM, &uTscSequence);
    AssertRCReturn(rc, rc);

    if (pRegion->cbRegion != GIM_HV_PAGE_SIZE)
        return SSMR3SetCfgError(pSSM, RT_SRC_POS, N_("TSC page region size %#x invalid, expected %#x"),
                                pRegion->cbRegion, GIM_HV_PAGE_SIZE);

    if (MSR_GIM_HV_REF_TSC_IS_ENABLED(pHv->u64TscPageMsr))
    {
        Assert(pRegion->GCPhysPage != NIL_RTGCPHYS);
        if (pRegion->fRegistered)
        {
            rc = gimR3HvEnableTscPage(pVM, pRegion->GCPhysPage, true /* fUseThisTscSeq */, uTscSequence);
            if (RT_FAILURE(rc))
                return SSMR3SetCfgError(pSSM, RT_SRC_POS, N_("Failed to enable the TSC page. GCPhys=%#RGp rc=%Rrc"),
                                        pRegion->GCPhysPage, rc);
        }
        else
            return SSMR3SetCfgError(pSSM, RT_SRC_POS, N_("TSC-page MMIO2 region not registered. Missing GIM device?!"));
    }

    /*
     * Load the debug support data.
     */
    if (uHvSavedStateVersion > GIM_HV_SAVED_STATE_VERSION_PRE_DEBUG)
    {
        SSMR3GetU64(pSSM, &pHv->uDbgPendingBufferMsr);
        SSMR3GetU64(pSSM, &pHv->uDbgSendBufferMsr);
        SSMR3GetU64(pSSM, &pHv->uDbgRecvBufferMsr);
        SSMR3GetU64(pSSM, &pHv->uDbgStatusMsr);
        SSM_GET_ENUM32_RET(pSSM, pHv->enmDbgReply, GIMHVDEBUGREPLY);
        SSMR3GetU32(pSSM, &pHv->uDbgBootpXId);
        rc = SSMR3GetU32(pSSM, &pHv->DbgGuestIp4Addr.u);
        AssertRCReturn(rc, rc);
        if (uHvSavedStateVersion > GIM_HV_SAVED_STATE_VERSION_PRE_DEBUG_UDP_PORTS)
        {
            rc = SSMR3GetU16(pSSM, &pHv->uUdpGuestDstPort);     AssertRCReturn(rc, rc);
            rc = SSMR3GetU16(pSSM, &pHv->uUdpGuestSrcPort);     AssertRCReturn(rc, rc);
        }

        for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
        {
            PGIMHVCPU pHvCpu = &pVM->apCpusR3[idCpu]->gim.s.u.HvCpu;
            SSMR3GetU64(pSSM, &pHvCpu->uSimpMsr);
            if (uHvSavedStateVersion <= GIM_HV_SAVED_STATE_VERSION_PRE_SYNIC)
                SSMR3GetU64(pSSM, &pHvCpu->auSintMsrs[GIM_HV_VMBUS_MSG_SINT]);
            else
            {
                for (uint8_t idxSintMsr = 0; idxSintMsr < RT_ELEMENTS(pHvCpu->auSintMsrs); idxSintMsr++)
                    SSMR3GetU64(pSSM, &pHvCpu->auSintMsrs[idxSintMsr]);
            }
        }

        uint8_t bDelim;
        rc = SSMR3GetU8(pSSM, &bDelim);
    }
    else
        rc = VINF_SUCCESS;

    return rc;
}


/**
 * Hyper-V load-done callback.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pSSM            The saved state handle.
 */
VMMR3_INT_DECL(int) gimR3HvLoadDone(PVM pVM, PSSMHANDLE pSSM)
{
    if (RT_SUCCESS(SSMR3HandleGetStatus(pSSM)))
    {
        /*
         * Update EM on whether MSR_GIM_HV_GUEST_OS_ID allows hypercall instructions.
         */
        if (pVM->gim.s.u.Hv.u64GuestOsIdMsr)
            for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
                EMSetHypercallInstructionsEnabled(pVM->apCpusR3[idCpu], true);
        else
            for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
                EMSetHypercallInstructionsEnabled(pVM->apCpusR3[idCpu], false);
    }
    return VINF_SUCCESS;
}


/**
 * Enables the Hyper-V APIC-assist page.
 *
 * @returns VBox status code.
 * @param   pVCpu                   The cross context virtual CPU structure.
 * @param   GCPhysApicAssistPage    Where to map the APIC-assist page.
 */
VMMR3_INT_DECL(int) gimR3HvEnableApicAssistPage(PVMCPU pVCpu, RTGCPHYS GCPhysApicAssistPage)
{
    PVM             pVM     = pVCpu->CTX_SUFF(pVM);
    PPDMDEVINSR3    pDevIns = pVM->gim.s.pDevInsR3;
    AssertPtrReturn(pDevIns, VERR_GIM_DEVICE_NOT_REGISTERED);

    /*
     * Map the APIC-assist-page at the specified address.
     */
    /** @todo this is buggy when large pages are used due to a PGM limitation, see
     *        @bugref{7532}. Instead of the overlay style mapping, we just
     *        rewrite guest memory directly. */
    AssertCompile(sizeof(g_abRTZero64K) >= GUEST_PAGE_SIZE);
    int rc = PGMPhysSimpleWriteGCPhys(pVM, GCPhysApicAssistPage, g_abRTZero64K, GUEST_PAGE_SIZE);
    if (RT_SUCCESS(rc))
    {
        /** @todo Inform APIC. */
        LogRel(("GIM%u: HyperV: Enabled APIC-assist page at %#RGp\n", pVCpu->idCpu, GCPhysApicAssistPage));
    }
    else
    {
        LogRelFunc(("GIM%u: HyperV: PGMPhysSimpleWriteGCPhys failed. rc=%Rrc\n", pVCpu->idCpu, rc));
        rc = VERR_GIM_OPERATION_FAILED;
    }
    return rc;
}


/**
 * Disables the Hyper-V APIC-assist page.
 *
 * @returns VBox status code.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VMMR3_INT_DECL(int) gimR3HvDisableApicAssistPage(PVMCPU pVCpu)
{
    LogRel(("GIM%u: HyperV: Disabled APIC-assist page\n", pVCpu->idCpu));
    /** @todo inform APIC */
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNTMTIMERINT, Hyper-V synthetic timer callback.}
 */
static DECLCALLBACK(void) gimR3HvTimerCallback(PVM pVM, TMTIMERHANDLE hTimer, void *pvUser)
{
    PGIMHVSTIMER pHvStimer = (PGIMHVSTIMER)pvUser;
    Assert(pHvStimer);
    Assert(TMTimerIsLockOwner(pVM, hTimer));
    Assert(pHvStimer->idCpu < pVM->cCpus);
    Assert(pHvStimer->hTimer == hTimer);
    RT_NOREF(hTimer);

    PVMCPU    pVCpu  = pVM->apCpusR3[pHvStimer->idCpu];
    PGIMHVCPU pHvCpu = &pVCpu->gim.s.u.HvCpu;
    Assert(pHvStimer->idxStimer < RT_ELEMENTS(pHvCpu->aStatStimerFired));

    STAM_COUNTER_INC(&pHvCpu->aStatStimerFired[pHvStimer->idxStimer]);

    uint64_t const uStimerConfig = pHvStimer->uStimerConfigMsr;
    uint16_t const idxSint       = MSR_GIM_HV_STIMER_GET_SINTX(uStimerConfig);
    if (RT_LIKELY(idxSint < RT_ELEMENTS(pHvCpu->auSintMsrs)))
    {
        uint64_t const uSint = pHvCpu->auSintMsrs[idxSint];
        if (!MSR_GIM_HV_SINT_IS_MASKED(uSint))
        {
            uint8_t const uVector  = MSR_GIM_HV_SINT_GET_VECTOR(uSint);
            bool const    fAutoEoi = MSR_GIM_HV_SINT_IS_AUTOEOI(uSint);
            APICHvSendInterrupt(pVCpu, uVector, fAutoEoi, XAPICTRIGGERMODE_EDGE);
        }
    }

    /* Re-arm the timer if it's periodic. */
    if (MSR_GIM_HV_STIMER_IS_PERIODIC(uStimerConfig))
        gimHvStartStimer(pVCpu, pHvStimer);
}


/**
 * Enables the Hyper-V SIEF page.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   GCPhysSiefPage  Where to map the SIEF page.
 */
VMMR3_INT_DECL(int) gimR3HvEnableSiefPage(PVMCPU pVCpu, RTGCPHYS GCPhysSiefPage)
{
    PVM             pVM     = pVCpu->CTX_SUFF(pVM);
    PPDMDEVINSR3    pDevIns = pVM->gim.s.pDevInsR3;
    AssertPtrReturn(pDevIns, VERR_GIM_DEVICE_NOT_REGISTERED);

    /*
     * Map the SIEF page at the specified address.
     */
    /** @todo this is buggy when large pages are used due to a PGM limitation, see
     *        @bugref{7532}. Instead of the overlay style mapping, we just
     *        rewrite guest memory directly. */
    AssertCompile(sizeof(g_abRTZero64K) >= GUEST_PAGE_SIZE);
    int rc = PGMPhysSimpleWriteGCPhys(pVM, GCPhysSiefPage, g_abRTZero64K, GUEST_PAGE_SIZE);
    if (RT_SUCCESS(rc))
    {
        /** @todo SIEF setup. */
        LogRel(("GIM%u: HyperV: Enabled SIEF page at %#RGp\n", pVCpu->idCpu, GCPhysSiefPage));
    }
    else
    {
        LogRelFunc(("GIM%u: HyperV: PGMPhysSimpleWriteGCPhys failed. rc=%Rrc\n", pVCpu->idCpu, rc));
        rc = VERR_GIM_OPERATION_FAILED;
    }
    return rc;
}


/**
 * Disables the Hyper-V SIEF page.
 *
 * @returns VBox status code.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VMMR3_INT_DECL(int) gimR3HvDisableSiefPage(PVMCPU pVCpu)
{
    LogRel(("GIM%u: HyperV: Disabled APIC-assist page\n", pVCpu->idCpu));
    /** @todo SIEF teardown. */
    return VINF_SUCCESS;
}


/**
 * Enables the Hyper-V TSC page.
 *
 * @returns VBox status code.
 * @param   pVM                The cross context VM structure.
 * @param   GCPhysTscPage      Where to map the TSC page.
 * @param   fUseThisTscSeq     Whether to set the TSC sequence number to the one
 *                             specified in @a uTscSeq.
 * @param   uTscSeq            The TSC sequence value to use. Ignored if
 *                             @a fUseThisTscSeq is false.
 */
VMMR3_INT_DECL(int) gimR3HvEnableTscPage(PVM pVM, RTGCPHYS GCPhysTscPage, bool fUseThisTscSeq, uint32_t uTscSeq)
{
    PPDMDEVINSR3    pDevIns = pVM->gim.s.pDevInsR3;
    PGIMMMIO2REGION pRegion = &pVM->gim.s.u.Hv.aMmio2Regions[GIM_HV_REF_TSC_PAGE_REGION_IDX];
    AssertPtrReturn(pDevIns, VERR_GIM_DEVICE_NOT_REGISTERED);

    int rc;
    if (pRegion->fMapped)
    {
        /*
         * Is it already enabled at the given guest-address?
         */
        if (pRegion->GCPhysPage == GCPhysTscPage)
            return VINF_SUCCESS;

        /*
         * If it's mapped at a different address, unmap the previous address.
         */
        rc = gimR3HvDisableTscPage(pVM);
        AssertRC(rc);
    }

    /*
     * Map the TSC-page at the specified address.
     */
    Assert(!pRegion->fMapped);

    /** @todo this is buggy when large pages are used due to a PGM limitation, see
     *        @bugref{7532}. Instead of the overlay style mapping, we just
     *        rewrite guest memory directly. */
#if 0
    rc = gimR3Mmio2Map(pVM, pRegion, GCPhysTscPage);
    if (RT_SUCCESS(rc))
    {
        Assert(pRegion->GCPhysPage == GCPhysTscPage);

        /*
         * Update the TSC scale. Windows guests expect a non-zero TSC sequence, otherwise
         * they fallback to using the reference count MSR which is not ideal in terms of VM-exits.
         *
         * Also, Hyper-V normalizes the time in 10 MHz, see:
         * http://technet.microsoft.com/it-it/sysinternals/dn553408%28v=vs.110%29
         */
        PGIMHVREFTSC pRefTsc = (PGIMHVREFTSC)pRegion->pvPageR3;
        Assert(pRefTsc);

        PGIMHV pHv = &pVM->gim.s.u.Hv;
        uint64_t const u64TscKHz = pHv->cTscTicksPerSecond / UINT64_C(1000);
        uint32_t       u32TscSeq = 1;
        if (   fUseThisTscSeq
            && uTscSeq < UINT32_C(0xfffffffe))
            u32TscSeq = uTscSeq + 1;
        pRefTsc->u32TscSequence  = u32TscSeq;
        pRefTsc->u64TscScale     = ((INT64_C(10000) << 32) / u64TscKHz) << 32;
        pRefTsc->i64TscOffset    = 0;

        LogRel(("GIM: HyperV: Enabled TSC page at %#RGp - u64TscScale=%#RX64 u64TscKHz=%#RX64 (%'RU64) Seq=%#RU32\n",
                GCPhysTscPage, pRefTsc->u64TscScale, u64TscKHz, u64TscKHz, pRefTsc->u32TscSequence));

        TMR3CpuTickParavirtEnable(pVM);
        return VINF_SUCCESS;
    }
    else
        LogRelFunc(("gimR3Mmio2Map failed. rc=%Rrc\n", rc));
    return VERR_GIM_OPERATION_FAILED;
#else
    AssertReturn(pRegion->cbRegion == GUEST_PAGE_SIZE, VERR_GIM_IPE_2);
    PGIMHVREFTSC pRefTsc = (PGIMHVREFTSC)RTMemAllocZ(GUEST_PAGE_SIZE);
    if (RT_UNLIKELY(!pRefTsc))
    {
        LogRelFunc(("Failed to alloc %#x bytes\n", GUEST_PAGE_SIZE));
        return VERR_NO_MEMORY;
    }

    PGIMHV pHv = &pVM->gim.s.u.Hv;
    uint64_t const u64TscKHz = pHv->cTscTicksPerSecond / UINT64_C(1000);
    uint32_t       u32TscSeq = 1;
    if (   fUseThisTscSeq
        && uTscSeq < UINT32_C(0xfffffffe))
        u32TscSeq = uTscSeq + 1;
    pRefTsc->u32TscSequence  = u32TscSeq;
    pRefTsc->u64TscScale     = ((INT64_C(10000) << 32) / u64TscKHz) << 32;
    pRefTsc->i64TscOffset    = 0;

    rc = PGMPhysSimpleWriteGCPhys(pVM, GCPhysTscPage, pRefTsc, sizeof(*pRefTsc));
    if (RT_SUCCESS(rc))
    {
        LogRel(("GIM: HyperV: Enabled TSC page at %#RGp - u64TscScale=%#RX64 u64TscKHz=%#RX64 (%'RU64) Seq=%#RU32\n",
                GCPhysTscPage, pRefTsc->u64TscScale, u64TscKHz, u64TscKHz, pRefTsc->u32TscSequence));

        pRegion->GCPhysPage = GCPhysTscPage;
        pRegion->fMapped = true;
        TMR3CpuTickParavirtEnable(pVM);
    }
    else
    {
        LogRelFunc(("GIM: HyperV: PGMPhysSimpleWriteGCPhys failed. rc=%Rrc\n", rc));
        rc = VERR_GIM_OPERATION_FAILED;
    }
    RTMemFree(pRefTsc);
    return rc;
#endif
}


/**
 * Enables the Hyper-V SIM page.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   GCPhysSimPage   Where to map the SIM page.
 */
VMMR3_INT_DECL(int) gimR3HvEnableSimPage(PVMCPU pVCpu, RTGCPHYS GCPhysSimPage)
{
    PVM             pVM     = pVCpu->CTX_SUFF(pVM);
    PPDMDEVINSR3    pDevIns = pVM->gim.s.pDevInsR3;
    AssertPtrReturn(pDevIns, VERR_GIM_DEVICE_NOT_REGISTERED);

    /*
     * Map the SIMP page at the specified address.
     */
    /** @todo this is buggy when large pages are used due to a PGM limitation, see
     *        @bugref{7532}. Instead of the overlay style mapping, we just
     *        rewrite guest memory directly. */
    AssertCompile(sizeof(g_abRTZero64K) >= GUEST_PAGE_SIZE);
    int rc = PGMPhysSimpleWriteGCPhys(pVM, GCPhysSimPage, g_abRTZero64K, GUEST_PAGE_SIZE);
    if (RT_SUCCESS(rc))
    {
        /** @todo SIM setup. */
        LogRel(("GIM%u: HyperV: Enabled SIM page at %#RGp\n", pVCpu->idCpu, GCPhysSimPage));
    }
    else
    {
        LogRelFunc(("GIM%u: HyperV: PGMPhysSimpleWriteGCPhys failed. rc=%Rrc\n", pVCpu->idCpu, rc));
        rc = VERR_GIM_OPERATION_FAILED;
    }
    return rc;
}


/**
 * Disables the Hyper-V SIM page.
 *
 * @returns VBox status code.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VMMR3_INT_DECL(int) gimR3HvDisableSimPage(PVMCPU pVCpu)
{
    LogRel(("GIM%u: HyperV: Disabled SIM page\n", pVCpu->idCpu));
    /** @todo SIM teardown. */
    return VINF_SUCCESS;
}



/**
 * Disables the Hyper-V TSC page.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(int) gimR3HvDisableTscPage(PVM pVM)
{
    PGIMHV pHv = &pVM->gim.s.u.Hv;
    PGIMMMIO2REGION pRegion = &pHv->aMmio2Regions[GIM_HV_REF_TSC_PAGE_REGION_IDX];
    if (pRegion->fMapped)
    {
#if 0
        gimR3Mmio2Unmap(pVM, pRegion);
        Assert(!pRegion->fMapped);
#else
        pRegion->fMapped = false;
#endif
        LogRel(("GIM: HyperV: Disabled TSC page\n"));

        TMR3CpuTickParavirtDisable(pVM);
        return VINF_SUCCESS;
    }
    return VERR_GIM_PVTSC_NOT_ENABLED;
}


/**
 * Disables the Hyper-V Hypercall page.
 *
 * @returns VBox status code.
 */
VMMR3_INT_DECL(int) gimR3HvDisableHypercallPage(PVM pVM)
{
    PGIMHV pHv = &pVM->gim.s.u.Hv;
    PGIMMMIO2REGION pRegion = &pHv->aMmio2Regions[GIM_HV_HYPERCALL_PAGE_REGION_IDX];
    if (pRegion->fMapped)
    {
#if 0
        gimR3Mmio2Unmap(pVM, pRegion);
        Assert(!pRegion->fMapped);
#else
        pRegion->fMapped = false;
#endif
        LogRel(("GIM: HyperV: Disabled Hypercall-page\n"));
        return VINF_SUCCESS;
    }
    return VERR_GIM_HYPERCALLS_NOT_ENABLED;
}


/**
 * Enables the Hyper-V Hypercall page.
 *
 * @returns VBox status code.
 * @param   pVM                     The cross context VM structure.
 * @param   GCPhysHypercallPage     Where to map the hypercall page.
 */
VMMR3_INT_DECL(int) gimR3HvEnableHypercallPage(PVM pVM, RTGCPHYS GCPhysHypercallPage)
{
    PPDMDEVINSR3    pDevIns = pVM->gim.s.pDevInsR3;
    PGIMMMIO2REGION pRegion = &pVM->gim.s.u.Hv.aMmio2Regions[GIM_HV_HYPERCALL_PAGE_REGION_IDX];
    AssertPtrReturn(pDevIns, VERR_GIM_DEVICE_NOT_REGISTERED);

    if (pRegion->fMapped)
    {
        /*
         * Is it already enabled at the given guest-address?
         */
        if (pRegion->GCPhysPage == GCPhysHypercallPage)
            return VINF_SUCCESS;

        /*
         * If it's mapped at a different address, unmap the previous address.
         */
        int rc2 = gimR3HvDisableHypercallPage(pVM);
        AssertRC(rc2);
    }

    /*
     * Map the hypercall-page at the specified address.
     */
    Assert(!pRegion->fMapped);

    /** @todo this is buggy when large pages are used due to a PGM limitation, see
     *        @bugref{7532}. Instead of the overlay style mapping, we just
     *        rewrite guest memory directly. */
#if 0
    int rc = gimR3Mmio2Map(pVM, pRegion, GCPhysHypercallPage);
    if (RT_SUCCESS(rc))
    {
        Assert(pRegion->GCPhysPage == GCPhysHypercallPage);

        /*
         * Patch the hypercall-page.
         */
        size_t cbWritten = 0;
        rc = VMMPatchHypercall(pVM, pRegion->pvPageR3, GUEST_PAGE_SIZE, &cbWritten);
        if (   RT_SUCCESS(rc)
            && cbWritten < GUEST_PAGE_SIZE)
        {
            uint8_t *pbLast = (uint8_t *)pRegion->pvPageR3 + cbWritten;
            *pbLast = 0xc3;  /* RET */

            /*
             * Notify VMM that hypercalls are now enabled for all VCPUs.
             */
            for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
                VMMHypercallsEnable(pVM->apCpusR3[idCpu]);

            LogRel(("GIM: HyperV: Enabled hypercall page at %#RGp\n", GCPhysHypercallPage));
            return VINF_SUCCESS;
        }
        if (rc == VINF_SUCCESS)
            rc = VERR_GIM_OPERATION_FAILED;
        LogRel(("GIM: HyperV: VMMPatchHypercall failed. rc=%Rrc cbWritten=%u\n", rc, cbWritten));

        gimR3Mmio2Unmap(pVM, pRegion);
    }

    LogRel(("GIM: HyperV: gimR3Mmio2Map failed. rc=%Rrc\n", rc));
    return rc;
#else
    AssertReturn(pRegion->cbRegion == GUEST_PAGE_SIZE, VERR_GIM_IPE_3);
    void *pvHypercallPage = RTMemAllocZ(GUEST_PAGE_SIZE);
    if (RT_UNLIKELY(!pvHypercallPage))
    {
        LogRelFunc(("Failed to alloc %#x bytes\n", GUEST_PAGE_SIZE));
        return VERR_NO_MEMORY;
    }

    /*
     * Patch the hypercall-page.
     */
    size_t cbHypercall = 0;
    int rc = GIMQueryHypercallOpcodeBytes(pVM, pvHypercallPage, GUEST_PAGE_SIZE, &cbHypercall, NULL /*puDisOpcode*/);
    if (   RT_SUCCESS(rc)
        && cbHypercall < GUEST_PAGE_SIZE)
    {
        uint8_t *pbLast = (uint8_t *)pvHypercallPage + cbHypercall;
        *pbLast = 0xc3;  /* RET */

        rc = PGMPhysSimpleWriteGCPhys(pVM, GCPhysHypercallPage, pvHypercallPage, GUEST_PAGE_SIZE);
        if (RT_SUCCESS(rc))
        {
            pRegion->GCPhysPage = GCPhysHypercallPage;
            pRegion->fMapped = true;
            LogRel(("GIM: HyperV: Enabled hypercall page at %#RGp\n", GCPhysHypercallPage));
        }
        else
            LogRel(("GIM: HyperV: PGMPhysSimpleWriteGCPhys failed during hypercall page setup. rc=%Rrc\n", rc));
    }
    else
    {
        if (rc == VINF_SUCCESS)
            rc = VERR_GIM_OPERATION_FAILED;
        LogRel(("GIM: HyperV: VMMPatchHypercall failed. rc=%Rrc cbHypercall=%u\n", rc, cbHypercall));
    }

    RTMemFree(pvHypercallPage);
    return rc;
#endif
}


/**
 * Initializes Hyper-V guest hypercall support.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
static int gimR3HvInitHypercallSupport(PVM pVM)
{
    PGIMHV pHv = &pVM->gim.s.u.Hv;
    pHv->pbHypercallIn = (uint8_t *)RTMemAllocZ(GIM_HV_PAGE_SIZE);
    if (RT_LIKELY(pHv->pbHypercallIn))
    {
        pHv->pbHypercallOut = (uint8_t *)RTMemAllocZ(GIM_HV_PAGE_SIZE);
        if (RT_LIKELY(pHv->pbHypercallOut))
            return VINF_SUCCESS;
        RTMemFree(pHv->pbHypercallIn);
    }
    return VERR_NO_MEMORY;
}


/**
 * Terminates Hyper-V guest hypercall support.
 *
 * @param   pVM     The cross context VM structure.
 */
static void gimR3HvTermHypercallSupport(PVM pVM)
{
    PGIMHV pHv = &pVM->gim.s.u.Hv;
    RTMemFree(pHv->pbHypercallIn);
    pHv->pbHypercallIn = NULL;

    RTMemFree(pHv->pbHypercallOut);
    pHv->pbHypercallOut = NULL;
}


/**
 * Initializes Hyper-V guest debug support.
 *
 * @returns VBox status code.
 * @param   pVM     The cross context VM structure.
 */
static int gimR3HvInitDebugSupport(PVM pVM)
{
    PGIMHV pHv = &pVM->gim.s.u.Hv;
    if (   (pHv->uPartFlags & GIM_HV_PART_FLAGS_DEBUGGING)
        || pHv->fIsInterfaceVs)
    {
        pHv->fDbgEnabled = true;
        pHv->pvDbgBuffer = RTMemAllocZ(GIM_HV_PAGE_SIZE);
        if (!pHv->pvDbgBuffer)
            return VERR_NO_MEMORY;
    }
    return VINF_SUCCESS;
}


/**
 * Terminates Hyper-V guest debug support.
 *
 * @param   pVM     The cross context VM structure.
 */
static void gimR3HvTermDebugSupport(PVM pVM)
{
    PGIMHV pHv = &pVM->gim.s.u.Hv;
    if (pHv->pvDbgBuffer)
    {
        RTMemFree(pHv->pvDbgBuffer);
        pHv->pvDbgBuffer = NULL;
    }
}


/**
 * Reads data from a debugger connection, asynchronous.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pvBuf       Where to read the data.
 * @param   cbBuf       Size of the read buffer @a pvBuf, must be >= @a cbRead.
 * @param   cbRead      Number of bytes to read.
 * @param   pcbRead     Where to store how many bytes were really read.
 * @param   cMsTimeout  Timeout of the read operation in milliseconds.
 * @param   fUdpPkt     Whether the debug data returned in @a pvBuf needs to be
 *                      encapsulated in a UDP frame.
 *
 * @thread  EMT.
 */
VMMR3_INT_DECL(int) gimR3HvDebugRead(PVM pVM, void *pvBuf, uint32_t cbBuf, uint32_t cbRead, uint32_t *pcbRead,
                                     uint32_t cMsTimeout, bool fUdpPkt)
{
    NOREF(cMsTimeout);      /** @todo implement timeout. */
    AssertCompile(sizeof(size_t) >= sizeof(uint32_t));
    AssertReturn(cbBuf >= cbRead, VERR_INVALID_PARAMETER);

    int rc;
    if (!fUdpPkt)
    {
        /*
         * Read the raw debug data.
         */
        size_t cbReallyRead = cbRead;
        rc = gimR3DebugRead(pVM, pvBuf, &cbReallyRead, gimR3HvDebugBufReadCompleted);
        *pcbRead = (uint32_t)cbReallyRead;
    }
    else
    {
        /*
         * Guest requires UDP encapsulated frames.
         */
        PGIMHV pHv = &pVM->gim.s.u.Hv;
        rc = VERR_GIM_IPE_1;
        switch (pHv->enmDbgReply)
        {
            case GIMHVDEBUGREPLY_UDP:
            {
                size_t cbReallyRead = cbRead;
                rc = gimR3DebugRead(pVM, pvBuf, &cbReallyRead, gimR3HvDebugBufReadCompleted);
                if (   RT_SUCCESS(rc)
                    && cbReallyRead > 0)
                {
                    uint8_t abFrame[sizeof(RTNETETHERHDR) + RTNETIPV4_MIN_LEN + sizeof(RTNETUDP)];
                    if (cbReallyRead + sizeof(abFrame) <= cbBuf)
                    {
                        /*
                         * Windows guests pumps ethernet frames over the Hyper-V debug connection as
                         * explained in gimR3HvHypercallPostDebugData(). Here, we reconstruct the packet
                         * with the guest's self-chosen IP ARP address we saved in pHv->DbgGuestAddr.
                         *
                         * Note! We really need to pass the minimum IPv4 header length. The Windows 10 guest
                         * is -not- happy if we include the IPv4 options field, i.e. using sizeof(RTNETIPV4)
                         * instead of RTNETIPV4_MIN_LEN.
                         */
                        RT_ZERO(abFrame);
                        PRTNETETHERHDR pEthHdr = (PRTNETETHERHDR)&abFrame[0];
                        PRTNETIPV4     pIpHdr  = (PRTNETIPV4)    (pEthHdr + 1);
                        PRTNETUDP      pUdpHdr = (PRTNETUDP)     ((uint8_t *)pIpHdr + RTNETIPV4_MIN_LEN);

                        /* Ethernet */
                        pEthHdr->EtherType = RT_H2N_U16_C(RTNET_ETHERTYPE_IPV4);
                        /* IPv4 */
                        pIpHdr->ip_v       = 4;
                        pIpHdr->ip_hl      = RTNETIPV4_MIN_LEN / sizeof(uint32_t);
                        pIpHdr->ip_tos     = 0;
                        pIpHdr->ip_len     = RT_H2N_U16((uint16_t)cbReallyRead + sizeof(RTNETUDP) + RTNETIPV4_MIN_LEN);
                        pIpHdr->ip_id      = 0;
                        pIpHdr->ip_off     = 0;
                        pIpHdr->ip_ttl     = 255;
                        pIpHdr->ip_p       = RTNETIPV4_PROT_UDP;
                        pIpHdr->ip_sum     = 0;
                        pIpHdr->ip_src.u   = 0;
                        pIpHdr->ip_dst.u   = pHv->DbgGuestIp4Addr.u;
                        pIpHdr->ip_sum     = RTNetIPv4HdrChecksum(pIpHdr);
                        /* UDP */
                        pUdpHdr->uh_dport  = pHv->uUdpGuestSrcPort;
                        pUdpHdr->uh_sport  = pHv->uUdpGuestDstPort;
                        pUdpHdr->uh_ulen   = RT_H2N_U16_C((uint16_t)cbReallyRead + sizeof(*pUdpHdr));

                        /* Make room by moving the payload and prepending the headers. */
                        uint8_t *pbData = (uint8_t *)pvBuf;
                        memmove(pbData + sizeof(abFrame), pbData, cbReallyRead);
                        memcpy(pbData, &abFrame[0], sizeof(abFrame));

                        /* Update the adjusted sizes. */
                        cbReallyRead += sizeof(abFrame);
                    }
                    else
                        rc = VERR_BUFFER_UNDERFLOW;
                }
                *pcbRead = (uint32_t)cbReallyRead;
                break;
            }

            case GIMHVDEBUGREPLY_ARP_REPLY:
            {
                uint32_t const cbArpReplyPkt =  sizeof(g_abArpReply);
                if (cbBuf >= cbArpReplyPkt)
                {
                    memcpy(pvBuf, g_abArpReply, cbArpReplyPkt);
                    rc = VINF_SUCCESS;
                    *pcbRead = cbArpReplyPkt;
                    pHv->enmDbgReply = GIMHVDEBUGREPLY_ARP_REPLY_SENT;
                }
                else
                {
                    rc = VERR_BUFFER_UNDERFLOW;
                    *pcbRead = 0;
                }
                break;
            }

            case GIMHVDEBUGREPLY_DHCP_OFFER:
            {
                uint32_t const cbDhcpOfferPkt = sizeof(g_abDhcpOffer);
                if (cbBuf >= cbDhcpOfferPkt)
                {
                    memcpy(pvBuf, g_abDhcpOffer, cbDhcpOfferPkt);
                    PRTNETETHERHDR pEthHdr   = (PRTNETETHERHDR)pvBuf;
                    PRTNETIPV4     pIpHdr    = (PRTNETIPV4)    (pEthHdr + 1);
                    PRTNETUDP      pUdpHdr   = (PRTNETUDP)     ((uint8_t *)pIpHdr + RTNETIPV4_MIN_LEN);
                    PRTNETBOOTP    pBootpHdr = (PRTNETBOOTP)   (pUdpHdr + 1);
                    pBootpHdr->bp_xid = pHv->uDbgBootpXId;

                    rc = VINF_SUCCESS;
                    *pcbRead = cbDhcpOfferPkt;
                    pHv->enmDbgReply = GIMHVDEBUGREPLY_DHCP_OFFER_SENT;
                    LogRel(("GIM: HyperV: Debug DHCP offered IP address %RTnaipv4, transaction Id %#x\n", pBootpHdr->bp_yiaddr,
                            RT_N2H_U32(pHv->uDbgBootpXId)));
                }
                else
                {
                    rc = VERR_BUFFER_UNDERFLOW;
                    *pcbRead = 0;
                }
                break;
            }

            case GIMHVDEBUGREPLY_DHCP_ACK:
            {
                uint32_t const cbDhcpAckPkt = sizeof(g_abDhcpAck);
                if (cbBuf >= cbDhcpAckPkt)
                {
                    memcpy(pvBuf, g_abDhcpAck, cbDhcpAckPkt);
                    PRTNETETHERHDR pEthHdr   = (PRTNETETHERHDR)pvBuf;
                    PRTNETIPV4     pIpHdr    = (PRTNETIPV4)    (pEthHdr + 1);
                    PRTNETUDP      pUdpHdr   = (PRTNETUDP)     ((uint8_t *)pIpHdr + RTNETIPV4_MIN_LEN);
                    PRTNETBOOTP    pBootpHdr = (PRTNETBOOTP)   (pUdpHdr + 1);
                    pBootpHdr->bp_xid = pHv->uDbgBootpXId;

                    rc = VINF_SUCCESS;
                    *pcbRead = cbDhcpAckPkt;
                    pHv->enmDbgReply = GIMHVDEBUGREPLY_DHCP_ACK_SENT;
                    LogRel(("GIM: HyperV: Debug DHCP acknowledged IP address %RTnaipv4, transaction Id %#x\n",
                            pBootpHdr->bp_yiaddr, RT_N2H_U32(pHv->uDbgBootpXId)));
                }
                else
                {
                    rc = VERR_BUFFER_UNDERFLOW;
                    *pcbRead = 0;
                }
                break;
            }

            case GIMHVDEBUGREPLY_ARP_REPLY_SENT:
            case GIMHVDEBUGREPLY_DHCP_OFFER_SENT:
            case GIMHVDEBUGREPLY_DHCP_ACK_SENT:
            {
                rc = VINF_SUCCESS;
                *pcbRead = 0;
                break;
            }

            default:
            {
                AssertMsgFailed(("GIM: HyperV: Invalid/unimplemented debug reply type %u\n", pHv->enmDbgReply));
                rc = VERR_INTERNAL_ERROR_2;
            }
        }
        Assert(rc != VERR_GIM_IPE_1);

#ifdef DEBUG_ramshankar
        if (   rc == VINF_SUCCESS
            && *pcbRead > 0)
        {
            RTSOCKET hSocket;
            int rc2 = RTUdpCreateClientSocket("localhost", 52000, NULL, &hSocket);
            if (RT_SUCCESS(rc2))
            {
                size_t cbTmpWrite = *pcbRead;
                RTSocketWriteNB(hSocket, pvBuf, *pcbRead, &cbTmpWrite); NOREF(cbTmpWrite);
                RTSocketClose(hSocket);
            }
        }
#endif
    }

    return rc;
}


/**
 * Writes data to the debugger connection, asynchronous.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   pvData      Pointer to the data to be written.
 * @param   cbWrite     Size of the write buffer @a pvData.
 * @param   pcbWritten  Where to store the number of bytes written.
 * @param   fUdpPkt     Whether the debug data in @a pvData is encapsulated in a
 *                      UDP frame.
 *
 * @thread  EMT.
 */
VMMR3_INT_DECL(int) gimR3HvDebugWrite(PVM pVM, void *pvData, uint32_t cbWrite, uint32_t *pcbWritten, bool fUdpPkt)
{
    Assert(cbWrite > 0);

    PGIMHV    pHv        = &pVM->gim.s.u.Hv;
    bool      fIgnorePkt = false;
    uint8_t  *pbData     = (uint8_t *)pvData;
    if (fUdpPkt)
    {
#ifdef DEBUG_ramshankar
        RTSOCKET hSocket;
        int rc2 = RTUdpCreateClientSocket("localhost", 52000, NULL, &hSocket);
        if (RT_SUCCESS(rc2))
        {
            size_t cbTmpWrite = cbWrite;
            RTSocketWriteNB(hSocket, pbData, cbWrite, &cbTmpWrite);  NOREF(cbTmpWrite);
            RTSocketClose(hSocket);
        }
#endif
        /*
         * Windows guests sends us ethernet frames over the Hyper-V debug connection.
         * It sends DHCP/ARP queries with zero'd out MAC addresses and requires fudging up the
         * packets somewhere.
         *
         * The Microsoft WinDbg debugger talks UDP and thus only expects the actual debug
         * protocol payload.
         *
         * If the guest is configured with the "nodhcp" option it sends ARP queries with
         * a self-chosen IP and after a couple of attempts of receiving no replies, the guest
         * picks its own IP address. After this, the guest starts sending the UDP packets
         * we require. We thus ignore the initial ARP packets until the guest eventually
         * starts talking UDP. Then we can finally feed the UDP payload over the debug
         * connection.
         *
         * When 'kdvm.dll' is the debug transport in the guest (Windows 7), it doesn't bother
         * with this DHCP/ARP phase. It starts sending debug data in a UDP frame right away.
         */
        if (cbWrite > sizeof(RTNETETHERHDR))
        {
            PCRTNETETHERHDR pEtherHdr = (PCRTNETETHERHDR)pbData;
            if (pEtherHdr->EtherType == RT_H2N_U16_C(RTNET_ETHERTYPE_IPV4))
            {
                if (cbWrite > sizeof(RTNETETHERHDR) + RTNETIPV4_MIN_LEN + RTNETUDP_MIN_LEN)
                {
                    size_t const cbMaxIpHdr = cbWrite - sizeof(RTNETETHERHDR) - sizeof(RTNETUDP) - 1;
                    size_t const cbMaxIpPkt = cbWrite - sizeof(RTNETETHERHDR);
                    PCRTNETIPV4  pIp4Hdr    = (PCRTNETIPV4)(pbData + sizeof(RTNETETHERHDR));
                    bool const   fValidIp4  = RTNetIPv4IsHdrValid(pIp4Hdr, cbMaxIpHdr, cbMaxIpPkt, false /*fChecksum*/);
                    if (   fValidIp4
                        && pIp4Hdr->ip_p == RTNETIPV4_PROT_UDP)
                    {
                        uint32_t const cbIpHdr     = pIp4Hdr->ip_hl * 4;
                        uint32_t const cbMaxUdpPkt = cbWrite - sizeof(RTNETETHERHDR) - cbIpHdr;
                        PCRTNETUDP pUdpHdr       = (PCRTNETUDP)((uint8_t *)pIp4Hdr + cbIpHdr);
                        if (   pUdpHdr->uh_ulen >  RT_H2N_U16(sizeof(RTNETUDP))
                            && pUdpHdr->uh_ulen <= RT_H2N_U16((uint16_t)cbMaxUdpPkt))
                        {
                            /*
                             * Check for DHCP.
                             */
                            bool fBuggyPkt = false;
                            size_t const cbUdpPkt = cbMaxIpPkt - cbIpHdr;
                            if (   pUdpHdr->uh_dport == RT_N2H_U16_C(RTNETIPV4_PORT_BOOTPS)
                                && pUdpHdr->uh_sport == RT_N2H_U16_C(RTNETIPV4_PORT_BOOTPC))
                            {
                                PCRTNETBOOTP pDhcpPkt = (PCRTNETBOOTP)(pUdpHdr + 1);
                                uint8_t bMsgType;
                                if (   cbMaxIpPkt >= cbIpHdr + RTNETUDP_MIN_LEN + RTNETBOOTP_DHCP_MIN_LEN
                                    && RTNetIPv4IsDHCPValid(pUdpHdr, pDhcpPkt, cbUdpPkt - sizeof(*pUdpHdr), &bMsgType))
                                {
                                    switch (bMsgType)
                                    {
                                        case RTNET_DHCP_MT_DISCOVER:
                                            pHv->enmDbgReply = GIMHVDEBUGREPLY_DHCP_OFFER;
                                            pHv->uDbgBootpXId = pDhcpPkt->bp_xid;
                                            break;
                                        case RTNET_DHCP_MT_REQUEST:
                                            pHv->enmDbgReply = GIMHVDEBUGREPLY_DHCP_ACK;
                                            pHv->uDbgBootpXId = pDhcpPkt->bp_xid;
                                            break;
                                        default:
                                            LogRelMax(5, ("GIM: HyperV: Debug DHCP MsgType %#x not implemented! Packet dropped\n",
                                                          bMsgType));
                                            break;
                                    }
                                    fIgnorePkt = true;
                                }
                                else if (   pIp4Hdr->ip_src.u == GIMHV_DEBUGCLIENT_IPV4
                                         && pIp4Hdr->ip_dst.u == 0)
                                {
                                    /*
                                     * Windows 8.1 seems to be sending malformed BOOTP packets at the final stage of the
                                     * debugger sequence. It appears that a previously sent DHCP request buffer wasn't cleared
                                     * in the guest and they re-use it instead of sending a zero destination+source port packet
                                     * as expected below.
                                     *
                                     * We workaround Microsoft's bug here, or at least, I'm classifying it as a bug to
                                     * preserve my own sanity, see @bugref{8006#c54}.
                                     */
                                    fBuggyPkt = true;
                                }
                            }

                            if (  (   !pUdpHdr->uh_dport
                                   && !pUdpHdr->uh_sport)
                                || fBuggyPkt)
                            {
                                /*
                                 * Extract the UDP payload and pass it to the debugger and record the guest IP address.
                                 *
                                 * Hyper-V sends UDP debugger packets with source and destination port as 0 except in the
                                 * aforementioned buggy case. The buggy packet case requires us to remember the ports and
                                 * reply to them, otherwise the guest won't receive the replies we sent with port 0.
                                 */
                                uint32_t const cbFrameHdr = sizeof(RTNETETHERHDR) + cbIpHdr + sizeof(RTNETUDP);
                                pbData  += cbFrameHdr;
                                cbWrite -= cbFrameHdr;
                                pHv->DbgGuestIp4Addr.u = pIp4Hdr->ip_src.u;
                                pHv->uUdpGuestDstPort  = pUdpHdr->uh_dport;
                                pHv->uUdpGuestSrcPort  = pUdpHdr->uh_sport;
                                pHv->enmDbgReply       = GIMHVDEBUGREPLY_UDP;
                            }
                            else
                            {
                                LogFlow(("GIM: HyperV: Ignoring UDP packet SourcePort=%u DstPort=%u\n", pUdpHdr->uh_sport,
                                         pUdpHdr->uh_dport));
                                fIgnorePkt = true;
                            }
                        }
                        else
                        {
                            LogFlow(("GIM: HyperV: Ignoring malformed UDP packet. cbMaxUdpPkt=%u UdpPkt.len=%u\n", cbMaxUdpPkt,
                                     RT_N2H_U16(pUdpHdr->uh_ulen)));
                            fIgnorePkt = true;
                        }
                    }
                    else
                    {
                        LogFlow(("GIM: HyperV: Ignoring non-IP / non-UDP packet. fValidIp4=%RTbool Proto=%u\n", fValidIp4,
                                  pIp4Hdr->ip_p));
                        fIgnorePkt = true;
                    }
                }
                else
                {
                    LogFlow(("GIM: HyperV: Ignoring IPv4 packet; too short to be valid UDP. cbWrite=%u\n", cbWrite));
                    fIgnorePkt = true;
                }
            }
            else if (pEtherHdr->EtherType == RT_H2N_U16_C(RTNET_ETHERTYPE_ARP))
            {
                /*
                 * Check for targetted ARP query.
                 */
                PCRTNETARPHDR pArpHdr = (PCRTNETARPHDR)(pbData + sizeof(RTNETETHERHDR));
                if (   pArpHdr->ar_hlen  == sizeof(RTMAC)
                    && pArpHdr->ar_plen  == sizeof(RTNETADDRIPV4)
                    && pArpHdr->ar_htype == RT_H2N_U16(RTNET_ARP_ETHER)
                    && pArpHdr->ar_ptype == RT_H2N_U16(RTNET_ETHERTYPE_IPV4))
                {
                    uint16_t uArpOp = pArpHdr->ar_oper;
                    if (uArpOp == RT_H2N_U16_C(RTNET_ARPOP_REQUEST))
                    {
                        PCRTNETARPIPV4 pArpPkt = (PCRTNETARPIPV4)pArpHdr;
                        bool fGratuitous = pArpPkt->ar_spa.u == pArpPkt->ar_tpa.u;
                        if (   !fGratuitous
                            &&  pArpPkt->ar_spa.u == GIMHV_DEBUGCLIENT_IPV4
                            &&  pArpPkt->ar_tpa.u == GIMHV_DEBUGSERVER_IPV4)
                        {
                            pHv->enmDbgReply = GIMHVDEBUGREPLY_ARP_REPLY;
                        }
                    }
                }
                fIgnorePkt = true;
            }
            else
            {
                LogFlow(("GIM: HyperV: Ignoring non-IP packet. Ethertype=%#x\n", RT_N2H_U16(pEtherHdr->EtherType)));
                fIgnorePkt = true;
            }
        }
    }

    if (!fIgnorePkt)
    {
        AssertCompile(sizeof(size_t) >= sizeof(uint32_t));
        size_t cbWriteBuf = cbWrite;
        int rc = gimR3DebugWrite(pVM, pbData, &cbWriteBuf);
        if (   RT_SUCCESS(rc)
            && cbWriteBuf == cbWrite)
            *pcbWritten = (uint32_t)cbWriteBuf;
        else
            *pcbWritten = 0;
    }
    else
        *pcbWritten = cbWrite;

    return VINF_SUCCESS;
}


/**
 * Performs the HvPostDebugData hypercall.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   prcHv       Where to store the result of the hypercall operation.
 *
 * @thread  EMT.
 */
VMMR3_INT_DECL(int) gimR3HvHypercallPostDebugData(PVM pVM, int *prcHv)
{
    AssertPtr(pVM);
    AssertPtr(prcHv);
    PGIMHV pHv = &pVM->gim.s.u.Hv;
    int    rcHv = GIM_HV_STATUS_OPERATION_DENIED;

    /*
     * Grab the parameters.
     */
    PGIMHVDEBUGPOSTIN pIn = (PGIMHVDEBUGPOSTIN)pHv->pbHypercallIn;
    AssertPtrReturn(pIn, VERR_GIM_IPE_1);
    uint32_t   cbWrite = pIn->cbWrite;
    uint32_t   fFlags = pIn->fFlags;
    uint8_t   *pbData = ((uint8_t *)pIn) + sizeof(PGIMHVDEBUGPOSTIN);

    PGIMHVDEBUGPOSTOUT pOut = (PGIMHVDEBUGPOSTOUT)pHv->pbHypercallOut;

    /*
     * Perform the hypercall.
     */
#if 0
    /* Currently disabled as Windows 10 guest passes us undocumented flags. */
    if (fFlags & ~GIM_HV_DEBUG_POST_OPTIONS_MASK))
        rcHv = GIM_HV_STATUS_INVALID_PARAMETER;
#else
    RT_NOREF1(fFlags);
#endif
    if (cbWrite > GIM_HV_DEBUG_MAX_DATA_SIZE)
        rcHv = GIM_HV_STATUS_INVALID_PARAMETER;
    else if (!cbWrite)
    {
        rcHv = GIM_HV_STATUS_SUCCESS;
        pOut->cbPending = 0;
    }
    else if (cbWrite > 0)
    {
        uint32_t cbWritten = 0;
        int rc2 = gimR3HvDebugWrite(pVM, pbData, cbWrite, &cbWritten, pHv->fIsVendorMsHv /*fUdpPkt*/);
        if (   RT_SUCCESS(rc2)
            && cbWritten == cbWrite)
        {
            pOut->cbPending = 0;
            rcHv = GIM_HV_STATUS_SUCCESS;
        }
        else
            rcHv = GIM_HV_STATUS_INSUFFICIENT_BUFFER;
    }

    /*
     * Update the guest memory with result.
     */
    int rc = PGMPhysSimpleWriteGCPhys(pVM, pHv->GCPhysHypercallOut, pHv->pbHypercallOut, sizeof(GIMHVDEBUGPOSTOUT));
    if (RT_FAILURE(rc))
    {
        LogRelMax(10, ("GIM: HyperV: HvPostDebugData failed to update guest memory. rc=%Rrc\n", rc));
        rc = VERR_GIM_HYPERCALL_MEMORY_WRITE_FAILED;
    }
    else
        Assert(rc == VINF_SUCCESS);

    *prcHv = rcHv;
    return rc;
}


/**
 * Performs the HvRetrieveDebugData hypercall.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   prcHv       Where to store the result of the hypercall operation.
 *
 * @thread  EMT.
 */
VMMR3_INT_DECL(int) gimR3HvHypercallRetrieveDebugData(PVM pVM, int *prcHv)
{
    AssertPtr(pVM);
    AssertPtr(prcHv);
    PGIMHV pHv = &pVM->gim.s.u.Hv;
    int    rcHv = GIM_HV_STATUS_OPERATION_DENIED;

    /*
     * Grab the parameters.
     */
    PGIMHVDEBUGRETRIEVEIN pIn = (PGIMHVDEBUGRETRIEVEIN)pHv->pbHypercallIn;
    AssertPtrReturn(pIn, VERR_GIM_IPE_1);
    uint32_t   cbRead = pIn->cbRead;
    uint32_t   fFlags = pIn->fFlags;
    uint64_t   uTimeout = pIn->u64Timeout;
    uint32_t   cMsTimeout = (fFlags & GIM_HV_DEBUG_RETREIVE_LOOP) ? (uTimeout * 100) / RT_NS_1MS_64 : 0;

    PGIMHVDEBUGRETRIEVEOUT pOut = (PGIMHVDEBUGRETRIEVEOUT)pHv->pbHypercallOut;
    AssertPtrReturn(pOut, VERR_GIM_IPE_2);
    uint32_t   *pcbReallyRead = &pOut->cbRead;
    uint32_t   *pcbRemainingRead = &pOut->cbRemaining;
    void       *pvData = ((uint8_t *)pOut) + sizeof(GIMHVDEBUGRETRIEVEOUT);

    /*
     * Perform the hypercall.
     */
    *pcbReallyRead    = 0;
    *pcbRemainingRead = cbRead;
#if 0
    /* Currently disabled as Windows 10 guest passes us undocumented flags. */
    if (fFlags & ~GIM_HV_DEBUG_RETREIVE_OPTIONS_MASK)
        rcHv = GIM_HV_STATUS_INVALID_PARAMETER;
#endif
    if (cbRead > GIM_HV_DEBUG_MAX_DATA_SIZE)
        rcHv = GIM_HV_STATUS_INVALID_PARAMETER;
    else if (fFlags & GIM_HV_DEBUG_RETREIVE_TEST_ACTIVITY)
        rcHv = GIM_HV_STATUS_SUCCESS; /** @todo implement this. */
    else if (!cbRead)
        rcHv = GIM_HV_STATUS_SUCCESS;
    else if (cbRead > 0)
    {
        int rc2 = gimR3HvDebugRead(pVM, pvData, GIM_HV_PAGE_SIZE, cbRead, pcbReallyRead, cMsTimeout,
                                   pHv->fIsVendorMsHv /*fUdpPkt*/);
        Assert(*pcbReallyRead <= cbRead);
        if (   RT_SUCCESS(rc2)
            && *pcbReallyRead > 0)
        {
            *pcbRemainingRead = cbRead -  *pcbReallyRead;
            rcHv = GIM_HV_STATUS_SUCCESS;
        }
        else
            rcHv = GIM_HV_STATUS_NO_DATA;
    }

    /*
     * Update the guest memory with result.
     */
    int rc = PGMPhysSimpleWriteGCPhys(pVM, pHv->GCPhysHypercallOut, pHv->pbHypercallOut,
                                      sizeof(GIMHVDEBUGRETRIEVEOUT) + *pcbReallyRead);
    if (RT_FAILURE(rc))
    {
        LogRelMax(10, ("GIM: HyperV: HvRetrieveDebugData failed to update guest memory. rc=%Rrc\n", rc));
        rc = VERR_GIM_HYPERCALL_MEMORY_WRITE_FAILED;
    }
    else
        Assert(rc == VINF_SUCCESS);

    *prcHv = rcHv;
    return rc;
}


/**
 * Performs the HvExtCallQueryCapabilities extended hypercall.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   prcHv       Where to store the result of the hypercall operation.
 *
 * @thread  EMT.
 */
VMMR3_INT_DECL(int) gimR3HvHypercallExtQueryCap(PVM pVM, int *prcHv)
{
    AssertPtr(pVM);
    AssertPtr(prcHv);
    PGIMHV pHv  = &pVM->gim.s.u.Hv;

    /*
     * Grab the parameters.
     */
   PGIMHVEXTQUERYCAP pOut = (PGIMHVEXTQUERYCAP)pHv->pbHypercallOut;

    /*
     * Perform the hypercall.
     */
    pOut->fCapabilities = GIM_HV_EXT_HYPERCALL_CAP_ZERO_MEM;

    /*
     * Update the guest memory with result.
     */
    int rcHv;
    int rc = PGMPhysSimpleWriteGCPhys(pVM, pHv->GCPhysHypercallOut, pHv->pbHypercallOut, sizeof(GIMHVEXTQUERYCAP));
    if (RT_SUCCESS(rc))
    {
        rcHv = GIM_HV_STATUS_SUCCESS;
        LogRel(("GIM: HyperV: Queried extended hypercall capabilities %#RX64 at %#RGp\n", pOut->fCapabilities,
                pHv->GCPhysHypercallOut));
    }
    else
    {
        rcHv = GIM_HV_STATUS_OPERATION_DENIED;
        LogRelMax(10, ("GIM: HyperV: HvHypercallExtQueryCap failed to update guest memory. rc=%Rrc\n", rc));
        rc = VERR_GIM_HYPERCALL_MEMORY_WRITE_FAILED;
    }

    *prcHv = rcHv;
    return rc;
}


/**
 * Performs the HvExtCallGetBootZeroedMemory extended hypercall.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 * @param   prcHv       Where to store the result of the hypercall operation.
 *
 * @thread  EMT.
 */
VMMR3_INT_DECL(int) gimR3HvHypercallExtGetBootZeroedMem(PVM pVM, int *prcHv)
{
    AssertPtr(pVM);
    AssertPtr(prcHv);
    PGIMHV pHv  = &pVM->gim.s.u.Hv;

    /*
     * Grab the parameters.
     */
    PGIMHVEXTGETBOOTZEROMEM pOut = (PGIMHVEXTGETBOOTZEROMEM)pHv->pbHypercallOut;

    /*
     * Perform the hypercall.
     */
    AssertCompileMembersSameSizeAndOffset(PGMPHYSRANGES, cRanges, GIMHVEXTGETBOOTZEROMEM, cRanges);
    AssertCompileMembersAtSameOffset(PGMPHYSRANGES,      aRanges, GIMHVEXTGETBOOTZEROMEM, aRanges);
    AssertCompileMembersSameSizeAndOffset(PGMPHYSRANGE, GCPhysStart, GIMHVEXTMEMRANGE, GCPhysStart);
    AssertCompileMembersSameSizeAndOffset(PGMPHYSRANGE, cPages,      GIMHVEXTMEMRANGE, cPages);
    AssertCompile(RT_ELEMENTS(pOut->aRanges) == GIM_HV_MAX_BOOT_ZEROED_MEM_RANGES);

    int rcHv;
    int rc = PGMR3PhysGetRamBootZeroedRanges(pVM, (PPGMPHYSRANGES)pOut, RT_ELEMENTS(pOut->aRanges));
    if (   RT_SUCCESS(rc)
        || rc == VERR_BUFFER_OVERFLOW)
    {
        /*
         * Update the guest memory with result.
         */
        rc = PGMPhysSimpleWriteGCPhys(pVM, pHv->GCPhysHypercallOut, pHv->pbHypercallOut, sizeof(GIMHVEXTGETBOOTZEROMEM));
        if (RT_SUCCESS(rc))
        {
            LogRel(("GIM: HyperV: Queried boot zeroed guest memory as %u ranges\n", pOut->cRanges));
            for (uint32_t i = 0; i < pOut->cRanges; i++)
                LogRel(("GIM: HyperV: RAM range [%u] from %#RGp to %#RGp (%u pages, %' Rhcb)\n", i, pOut->aRanges[i].GCPhysStart,
                        pOut->aRanges[i].GCPhysStart + pOut->aRanges[i].cPages * GUEST_PAGE_SIZE - 1, pOut->aRanges[i].cPages,
                        pOut->aRanges[i].cPages * GUEST_PAGE_SIZE));
            rcHv = GIM_HV_STATUS_SUCCESS;
        }
        else
        {
            LogRelMax(10, ("GIM: HyperV: HvHypercallExtGetBootZeroedMem hypercall failed to update guest memory. rc=%Rrc\n", rc));
            rcHv = GIM_HV_STATUS_OPERATION_DENIED;
            rc   = VERR_GIM_HYPERCALL_MEMORY_WRITE_FAILED;
        }
    }
    else
    {
        LogRelMax(10, ("GIM: HyperV: HvHypercallExtGetBootZeroedMem failed. rc=%Rrc\n", rc));
        rcHv = GIM_HV_STATUS_OPERATION_DENIED;
        rc   = VERR_GIM_HYPERCALL_FAILED;
    }

    *prcHv = rcHv;
    return rc;
}

