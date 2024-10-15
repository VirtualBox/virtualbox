/* $Id: DevLpc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DevLPC - Minimal ICH9 LPC device emulation.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV_LPC
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/stam.h>
#include <VBox/log.h>

#include <iprt/assert.h>
#include <iprt/string.h>

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define LPC_REG_HPET_CONFIG_POINTER     0x3404
#define LPC_REG_GCS                     0x3410


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * The ICH9 LPC state.
 */
typedef struct LPCSTATE
{
    /** The root complex base address. */
    RTGCPHYS32      GCPhys32Rcba;
    /** The ICH version (7 or 9). */
    uint8_t         uIchVersion;
    /** Explicit padding. */
    uint8_t         abPadding[HC_ARCH_BITS == 32 ? 3 : 7];

    /** Number of MMIO reads. */
    STAMCOUNTER     StatMmioReads;
    /** Number of MMIO writes. */
    STAMCOUNTER     StatMmioWrites;
    /** Number of PCI config space reads. */
    STAMCOUNTER     StatPciCfgReads;
    /** Number of PCI config space writes. */
    STAMCOUNTER     StatPciCfgWrites;

    /** Handle to the MMIO region. */
    IOMMMIOHANDLE   hMmio;
} LPCSTATE;
/** Pointer to the LPC state. */
typedef LPCSTATE *PLPCSTATE;


#ifndef VBOX_DEVICE_STRUCT_TESTCASE

/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) lpcMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    RT_NOREF(pvUser, cb);
    PLPCSTATE        pThis  = PDMDEVINS_2_DATA(pDevIns, PLPCSTATE);
    Assert(cb == 4); Assert(!(off & 3)); /* IOMMMIO_FLAGS_READ_DWORD should make sure of this */

    uint32_t *puValue = (uint32_t *)pv;
    if (off == LPC_REG_HPET_CONFIG_POINTER)
    {
        *puValue = 0xf0;
        Log(("lpcMmioRead: HPET_CONFIG_POINTER: %#x\n", *puValue));
    }
    else if (off == LPC_REG_GCS)
    {
        *puValue = 0;
        Log(("lpcMmioRead: GCS: %#x\n", *puValue));
    }
    else
    {
        *puValue = 0;
        Log(("lpcMmioRead: WARNING! Unknown register %#RGp!\n", off));
    }

    STAM_REL_COUNTER_INC(&pThis->StatMmioReads);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) lpcMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PLPCSTATE        pThis  = PDMDEVINS_2_DATA(pDevIns, PLPCSTATE);
    RT_NOREF(pvUser, pv, cb);
    Assert(cb == 4); Assert(!(off & 3)); /* IOMMMIO_FLAGS_WRITE_DWORD_xxx should make sure of this */

    if (off == LPC_REG_GCS)
        Log(("lpcMmioWrite: Ignorning write to GCS: %.*Rhxs\n", cb, pv));
    else
        Log(("lpcMmioWrite: Ignorning write to unknown register %#RGp: %.*Rhxs\n", off, cb, pv));

    STAM_REL_COUNTER_INC(&pThis->StatMmioWrites);
    return VINF_SUCCESS;
}

#ifdef IN_RING3

/**
 * @callback_method_impl{FNPCICONFIGREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) lpcR3PciConfigRead(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                     uint32_t uAddress, unsigned cb, uint32_t *pu32Value)
{
    PLPCSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PLPCSTATE);
    Assert(pPciDev == pDevIns->apPciDevs[0]);

    STAM_REL_COUNTER_INC(&pThis->StatPciCfgReads);
    VBOXSTRICTRC rcStrict = PDMDevHlpPCIConfigRead(pDevIns, pPciDev, uAddress, cb, pu32Value);
    switch (cb)
    {
        case 1: Log(("lpcR3PciConfigRead: %#04x -> %#04x (%Rrc)\n",  uAddress, *pu32Value, VBOXSTRICTRC_VAL(rcStrict))); break;
        case 2: Log(("lpcR3PciConfigRead: %#04x -> %#06x (%Rrc)\n",  uAddress, *pu32Value, VBOXSTRICTRC_VAL(rcStrict))); break;
        case 4: Log(("lpcR3PciConfigRead: %#04x -> %#010x (%Rrc)\n", uAddress, *pu32Value, VBOXSTRICTRC_VAL(rcStrict))); break;
    }
    return rcStrict;
}


/**
 * @callback_method_impl{FNPCICONFIGWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) lpcR3PciConfigWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev,
                                                      uint32_t uAddress, unsigned cb, uint32_t u32Value)
{
    PLPCSTATE pThis = PDMDEVINS_2_DATA(pDevIns, PLPCSTATE);
    Assert(pPciDev == pDevIns->apPciDevs[0]);

    STAM_REL_COUNTER_INC(&pThis->StatPciCfgWrites);
    switch (cb)
    {
        case 1: Log(("lpcR3PciConfigWrite: %#04x <- %#04x\n",  uAddress, u32Value)); break;
        case 2: Log(("lpcR3PciConfigWrite: %#04x <- %#06x\n",  uAddress, u32Value)); break;
        case 4: Log(("lpcR3PciConfigWrite: %#04x <- %#010x\n", uAddress, u32Value)); break;
    }

    return PDMDevHlpPCIConfigWrite(pDevIns, pPciDev, uAddress, cb, u32Value);
}


/**
 * Info handler, device version.
 *
 * @param   pDevIns     Device instance which registered the info.
 * @param   pHlp        Callback functions for doing output.
 * @param   pszArgs     Argument string. Optional and specific to the handler.
 */
static DECLCALLBACK(void) lpcInfo(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PLPCSTATE  pThis   = PDMDEVINS_2_DATA(pDevIns, PLPCSTATE);
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    RT_NOREF(pszArgs);

    if (pThis->uIchVersion == 7)
    {
        uint8_t b1 = PDMPciDevGetByte(pPciDev, 0xde);
        uint8_t b2 = PDMPciDevGetByte(pPciDev, 0xad);
        if (   b1 == 0xbe
            && b2 == 0xef)
            pHlp->pfnPrintf(pHlp, "APIC backdoor activated\n");
        else
            pHlp->pfnPrintf(pHlp, "APIC backdoor closed: %02x %02x\n", b1, b2);
    }

    for (unsigned iLine = 0; iLine < 8; iLine++)
    {
        unsigned offBase = iLine < 4 ? 0x60 : 0x68 - 4;
        uint8_t  bMap    = PDMPciDevGetByte(pPciDev, offBase + iLine);
        if (bMap & 0x80)
            pHlp->pfnPrintf(pHlp, "PIRQ%c_ROUT disabled\n", 'A' + iLine);
        else
            pHlp->pfnPrintf(pHlp, "PIRQ%c_ROUT -> IRQ%d\n", 'A' + iLine, bMap & 0xf);
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) lpcConstruct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PLPCSTATE       pThis = PDMDEVINS_2_DATA(pDevIns, PLPCSTATE);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;
    Assert(iInstance == 0); RT_NOREF(iInstance);

    /*
     * Read configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "RCBA|ICHVersion", "");

    int rc = pHlp->pfnCFGMQueryU8Def(pCfg, "ICHVersion", &pThis->uIchVersion, 7 /** @todo 9 */);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to query boolean value \"ICHVersion\""));
    if (   pThis->uIchVersion != 7
        && pThis->uIchVersion != 9)
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Invalid \"ICHVersion\" value (must be 7 or 9)"));

    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "RCBA", &pThis->GCPhys32Rcba, UINT32_C(0xfed1c000));
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to query boolean value \"RCBA\""));


    /*
     * Register the PCI device.
     *
     * See sections 13.1 (page 371) and section 13.8.1 (page 429) in the ICH9
     * specification.
     *
     * We set these up so they don't need much/any configuration from the
     * guest.  This is quite possibly wrong, but at the moment we just need to
     * have this device working w/o lots of firmware fun.
     */
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    PDMPciDevSetVendorId(pPciDev,              0x8086);  /* Intel */
    if (pThis->uIchVersion == 7)
        PDMPciDevSetDeviceId(pPciDev,          0x27b9);
    else if (pThis->uIchVersion == 9)
        PDMPciDevSetDeviceId(pPciDev,          0x2918); /** @todo unsure if 0x2918 is the right PCI ID... */
    else
        AssertFailedReturn(VERR_INTERNAL_ERROR_3);
    PDMPciDevSetCommand(pPciDev,                  PCI_COMMAND_IOACCESS | PCI_COMMAND_MEMACCESS | PCI_COMMAND_BUSMASTER);
    PDMPciDevSetStatus(pPciDev,                 0x0210);  /* Note! Used to be 0x0200 for ICH7. */
    PDMPciDevSetRevisionId(pPciDev,               0x02);
    PDMPciDevSetClassSub(pPciDev,                 0x01);  /* PCI-to-ISA bridge */
    PDMPciDevSetClassBase(pPciDev,                0x06);  /* bridge */
    PDMPciDevSetHeaderType(pPciDev,               0x80);  /* Normal, multifunction device (so that other devices can be its functions) */
    if (pThis->uIchVersion == 7)
    {
        PDMPciDevSetSubSystemVendorId(pPciDev,  0x8086);
        PDMPciDevSetSubSystemId(pPciDev,        0x7270);
    }
    else if (pThis->uIchVersion == 9)
    {
        PDMPciDevSetSubSystemVendorId(pPciDev,  0x0000); /** @todo  docs stays subsystem IDs are zero, check real HW */
        PDMPciDevSetSubSystemId(pPciDev,        0x0000);
    }
    PDMPciDevSetInterruptPin(pPciDev,             0x00);  /* The LPC device itself generates no interrupts */
    PDMPciDevSetDWord(pPciDev,      0x40,   0x00008001);  /* PMBASE: ACPI base address; (PM_PORT_BASE (?) * 2 | PCI_ADDRESS_SPACE_IO) */
    PDMPciDevSetByte(pPciDev,       0x44,         0x80);  /* ACPI_CNTL: SCI is IRQ9, ACPI enabled  */ /** @todo documented as defaulting to 0x00. */
    PDMPciDevSetDWord(pPciDev,      0x48,   0x00000001);  /* GPIOBASE (note: used to be zero) */
    PDMPciDevSetByte(pPciDev,       0x4c,         0x4d);  /* GC - GPIO control: ??? */ /** @todo documented as defaulting to 0x00. */
    if (pThis->uIchVersion == 7)
        PDMPciDevSetByte(pPciDev,   0x4e,         0x03);  /* ??? */
    PDMPciDevSetByte(pPciDev,       0x60,         0x0b);  /* PIRQA_ROUT: PCI A -> IRQ 11 (documented default is 0x80) */
    PDMPciDevSetByte(pPciDev,       0x61,         0x09);  /* PIRQB_ROUT: PCI B -> IRQ 9  (documented default is 0x80) */
    PDMPciDevSetByte(pPciDev,       0x62,         0x0b);  /* PIRQC_ROUT: PCI C -> IRQ 11 (documented default is 0x80) */
    PDMPciDevSetByte(pPciDev,       0x63,         0x09);  /* PIRQD_ROUT: PCI D -> IRQ 9  (documented default is 0x80) */
    PDMPciDevSetByte(pPciDev,       0x64,         0x10);  /* SIRQ_CNTL: Serial IRQ Control 10h R/W, RO */
    PDMPciDevSetByte(pPciDev,       0x68,         0x80);  /* PIRQE_ROUT */
    PDMPciDevSetByte(pPciDev,       0x69,         0x80);  /* PIRQF_ROUT */
    PDMPciDevSetByte(pPciDev,       0x6a,         0x80);  /* PIRQG_ROUT */
    PDMPciDevSetByte(pPciDev,       0x6b,         0x80);  /* PIRQH_ROUT */
    PDMPciDevSetWord(pPciDev,       0x6c,       0x00f8);  /* IPC_IBDF: IOxAPIC bus:device:function.  (Note! Used to be zero.) */
    if (pThis->uIchVersion == 7)
    {
        /* No idea what this is/was yet: */
        PDMPciDevSetByte(pPciDev,   0x70,         0x80);
        PDMPciDevSetByte(pPciDev,   0x76,         0x0c);
        PDMPciDevSetByte(pPciDev,   0x77,         0x0c);
        PDMPciDevSetByte(pPciDev,   0x78,         0x02);
        PDMPciDevSetByte(pPciDev,   0x79,         0x00);
    }
    PDMPciDevSetWord(pPciDev,       0x80,       0x0000);  /* LPC_I/O_DEC: I/O decode ranges. */
    PDMPciDevSetWord(pPciDev,       0x82,       0x0000);  /* LPC_EN: LPC I/F enables. */
    PDMPciDevSetDWord(pPciDev,      0x84,   0x00000000);  /* GEN1_DEC: LPC I/F generic decode range 1. */
    PDMPciDevSetDWord(pPciDev,      0x88,   0x00000000);  /* GEN2_DEC: LPC I/F generic decode range 2. */
    PDMPciDevSetDWord(pPciDev,      0x8c,   0x00000000);  /* GEN3_DEC: LPC I/F generic decode range 3. */
    PDMPciDevSetDWord(pPciDev,      0x90,   0x00000000);  /* GEN4_DEC: LPC I/F generic decode range 4. */

    PDMPciDevSetWord(pPciDev,       0xa0,       0x0008);  /* GEN_PMCON_1: Documented default is 0x0000 */
    PDMPciDevSetByte(pPciDev,       0xa2,         0x00);  /* GEN_PMON_2: */
    PDMPciDevSetByte(pPciDev,       0xa4,         0x00);  /* GEN_PMON_3: */
    PDMPciDevSetByte(pPciDev,       0xa6,         0x00);  /* GEN_PMON_LOCK: Configuration lock. */
    if (pThis->uIchVersion == 7)
        PDMPciDevSetByte(pPciDev,   0xa8,         0x0f);  /* Is this part of GEN_PMON_LOCK? */
    PDMPciDevSetByte(pPciDev,       0xab,         0x00);  /* BM_BREAK_EN */
    PDMPciDevSetDWord(pPciDev,      0xac,   0x00000000);  /* PMIR: Power */
    PDMPciDevSetDWord(pPciDev,      0xb8,   0x00000000);  /* GPI_ROUT: GPI Route Control */
    if (pThis->uIchVersion == 9)
    {
        /** @todo the next two values looks bogus.   */
        PDMPciDevSetDWord(pPciDev,  0xd0,   0x00112233); /* FWH_SEL1: Firmware Hub Select 1  */
        PDMPciDevSetWord(pPciDev,   0xd4,       0x4567); /* FWH_SEL2: Firmware Hub Select 2 */
        PDMPciDevSetWord(pPciDev,   0xd8,       0xffcf); /* FWH_DEC_EN1: Firmware Hub Decode Enable 1 */
        PDMPciDevSetByte(pPciDev,   0xdc,         0x00); /* BIOS_CNTL: BIOS control */
        PDMPciDevSetWord(pPciDev,   0xe0,       0x0009); /* FDCAP: Feature Detection Capability ID */
        PDMPciDevSetByte(pPciDev,   0xe2,         0x0c); /* FDLEN: Feature Detection Capability Length */
        PDMPciDevSetByte(pPciDev,   0xe3,         0x10); /* FDVER: Feature Detection Version */
        PDMPciDevSetByte(pPciDev,   0xe4,         0x20); /* FDVCT[0]: 5=SATA RAID 0/1/5/10 capability (1=disabled) */
        PDMPciDevSetByte(pPciDev,   0xe5,         0x00); /* FDVCT[1]: */
        PDMPciDevSetByte(pPciDev,   0xe6,         0x00); /* FDVCT[2]: */
        PDMPciDevSetByte(pPciDev,   0xe7,         0x00); /* FDVCT[3]: */
        PDMPciDevSetByte(pPciDev,   0xe8,         0xc0); /* FDVCT[4]: 6-7=Intel active magament technology capability (11=disabled). */
        PDMPciDevSetByte(pPciDev,   0xe9,         0x00); /* FDVCT[5]: */
        PDMPciDevSetByte(pPciDev,   0xea,         0x00); /* FDVCT[6]: */
        PDMPciDevSetByte(pPciDev,   0xeb,         0x00); /* FDVCT[7]: */
        PDMPciDevSetByte(pPciDev,   0xec,         0x00); /* FDVCT[8]: */
        PDMPciDevSetByte(pPciDev,   0xed,         0x00); /* FDVCT[9]: */
        PDMPciDevSetByte(pPciDev,   0xee,         0x00); /* FDVCT[a]: */
        PDMPciDevSetByte(pPciDev,   0xef,         0x00); /* FDVCT[b]: */
    }

    /* RCBA: Root complex base address (documented default is 0x00000000). Bit 0 is enable bit. */
    Assert(!(pThis->GCPhys32Rcba & 0x3fff)); /* 16KB aligned */
    PDMPciDevSetDWord(pPciDev, 0xf0, pThis->GCPhys32Rcba | 1);

    rc = PDMDevHlpPCIRegisterEx(pDevIns, pPciDev, PDMPCIDEVREG_F_NOT_MANDATORY_NO, 31 /*uPciDevNo*/, 0 /*uPciFunNo*/, "lpc");
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpPCIInterceptConfigAccesses(pDevIns, pPciDev, lpcR3PciConfigRead, lpcR3PciConfigWrite);
    AssertRCReturn(rc, rc);

    /*
     * Register the MMIO regions.
     */
    /** @todo This should actually be done when RCBA is enabled, but was
     *        mentioned above we just want this working. */
    rc = PDMDevHlpMmioCreateAndMap(pDevIns, pThis->GCPhys32Rcba, 0x4000, lpcMmioWrite, lpcMmioRead,
                                   IOMMMIO_FLAGS_READ_DWORD | IOMMMIO_FLAGS_WRITE_DWORD_ZEROED,
                                   "LPC Memory", &pThis->hMmio);
    AssertRCReturn(rc, rc);


    /*
     * Debug info and stats.
     */
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMmioReads,    STAMTYPE_COUNTER, "MMIOReads", STAMUNIT_OCCURENCES, "MMIO reads");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMmioWrites,   STAMTYPE_COUNTER, "MMIOWrites", STAMUNIT_OCCURENCES, "MMIO writes");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatPciCfgReads,  STAMTYPE_COUNTER, "ConfigReads", STAMUNIT_OCCURENCES, "PCI config reads");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatPciCfgWrites, STAMTYPE_COUNTER, "ConfigWrites", STAMUNIT_OCCURENCES, "PCI config writes");

    PDMDevHlpDBGFInfoRegister(pDevIns, "lpc", "Display LPC status. (no arguments)", lpcInfo);

    return VINF_SUCCESS;
}

#endif /* IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceLPC =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "lpc",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_MISC,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(LPCSTATE),
    /* .cbInstanceCC = */           0,
    /* .cbInstanceRC = */           0,
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "Low Pin Count (LPC) Bus",
#if defined(IN_RING3)
    /* .pszRCMod = */               "",
    /* .pszR0Mod = */               "",
    /* .pfnConstruct = */           lpcConstruct,
    /* .pfnDestruct = */            NULL,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               NULL,
    /* .pfnSuspend = */             NULL,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              NULL,
    /* .pfnDetach = */              NULL,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            NULL,
    /* .pfnSoftReset = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RING0)
    /* .pfnEarlyConstruct = */      NULL,
    /* .pfnConstruct = */           NULL,
    /* .pfnDestruct = */            NULL,
    /* .pfnFinalDestruct = */       NULL,
    /* .pfnRequest = */             NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#elif defined(IN_RC)
    /* .pfnConstruct = */           NULL,
    /* .pfnReserved0 = */           NULL,
    /* .pfnReserved1 = */           NULL,
    /* .pfnReserved2 = */           NULL,
    /* .pfnReserved3 = */           NULL,
    /* .pfnReserved4 = */           NULL,
    /* .pfnReserved5 = */           NULL,
    /* .pfnReserved6 = */           NULL,
    /* .pfnReserved7 = */           NULL,
#else
# error "Not in IN_RING3, IN_RING0 or IN_RC!"
#endif
    /* .u32VersionEnd = */          PDM_DEVREG_VERSION
};

#endif /* VBOX_DEVICE_STRUCT_TESTCASE */

