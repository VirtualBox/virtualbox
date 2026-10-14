/* $Id: SystemTableBuilder.cpp 112403 2026-01-11 19:29:08Z knut.osmundsen@oracle.com $ */
/** @file
 * VirtualBox bus slots assignment manager
 */

/*
 * Copyright (C) 2010-2026 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_MAIN
#include "LoggingNew.h"

#include "SystemTableBuilder.h"

#include <VBox/gic.h>

#include <iprt/asm.h>
#include <iprt/string.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** Locality CRB request register. */
#define TPM_CRB_LOCALITY_REG_CTRL_REQ                        0x40


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * A system table device.
 */
typedef struct SYSTEMTABLEDEVICE
{
    const char *pszVBoxName;
    const char *pszFdtName;
    const char *pszFdtCompatible;
    const char *pszAcpiName;
    const char *pszAcpiHid;
    bool       fEisaId;
} SYSTEMTABLEDEVICE;
typedef SYSTEMTABLEDEVICE *PSYSTEMTABLEDEVICE;
typedef const SYSTEMTABLEDEVICE *PCSYSTEMTABLEDEVICE;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const SYSTEMTABLEDEVICE g_aSysTblDevices[] =
{
    { "qemu-fw-cfg",        "fw-cfg",   "qemu,fw-cfg-mmio",         "FWC", "QEMU0002", false },
    { "arm-pl011",          "pl011",    "arm,pl011",                "SRL", "ARMH0011", false },
    { "arm-pl061-gpio",     "pl061",    "arm,pl061",                "GPI", "ARMH0061", false },
    { "pci-generic-ecam",   "pcie",     "pci-host-ecam-generic",    "PCI", "PNP0A08",  true  },
};


static PCSYSTEMTABLEDEVICE systemTableVBoxDevName2SysTblDevice(const char *pszVBoxName)
{
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aSysTblDevices); i++)
        if (!strcmp(pszVBoxName, g_aSysTblDevices[i].pszVBoxName))
            return &g_aSysTblDevices[i];

    return NULL;
}


static int systemTableAcpiMmioDevResource(RTACPITBL hDsdt, RTACPIRES hAcpiRes, uint64_t u64AddrBase,
                                          uint64_t cbMmio, uint32_t uIrq)
{
    uint32_t const fAddrSpace =   RTACPI_RESOURCE_ADDR_RANGE_F_DECODE_TYPE_POS
                                | RTACPI_RESOURCE_ADDR_RANGE_F_MIN_ADDR_FIXED
                                | RTACPI_RESOURCE_ADDR_RANGE_F_MAX_ADDR_FIXED
                                | RTACPI_RESOURCE_ADDR_RANGE_F_PRODUCER;

    RTAcpiResourceReset(hAcpiRes);
    int vrc;
    if (u64AddrBase + cbMmio <= _4G)
        vrc = RTAcpiResourceAdd32BitFixedMemoryRange(hAcpiRes, u64AddrBase, cbMmio, true /*fRw*/);
    else
        vrc = RTAcpiResourceAddQWordMemoryRange(hAcpiRes, kAcpiResMemRangeCacheability_NonCacheable, kAcpiResMemType_Memory, true /*fRw*/,
                                                fAddrSpace, u64AddrBase, u64AddrBase + cbMmio - 1, 0 /*u64OffTrans*/, 0 /*u64Granularity*/,
                                                cbMmio);
    if (RT_SUCCESS(vrc))
        vrc = RTAcpiResourceAddExtendedInterrupt(hAcpiRes, true /*fConsumer*/, false /*fEdgeTriggered*/, false /*fActiveLow*/,
                                                 false /*fShared*/, false /*fWakeCapable*/, 1, &uIrq);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTAcpiResourceSeal(hAcpiRes);
        if (RT_SUCCESS(vrc))
            vrc = RTAcpiTblResourceAppend(hDsdt, hAcpiRes);
    }

    return vrc;
}


static int systemTableAcpiMmioDevResourceNoIrq(RTACPITBL hDsdt, RTACPIRES hAcpiRes, uint64_t u64AddrBase,
                                               uint64_t cbMmio)
{
    uint32_t const fAddrSpace =   RTACPI_RESOURCE_ADDR_RANGE_F_DECODE_TYPE_POS
                                | RTACPI_RESOURCE_ADDR_RANGE_F_MIN_ADDR_FIXED
                                | RTACPI_RESOURCE_ADDR_RANGE_F_MAX_ADDR_FIXED
                                | RTACPI_RESOURCE_ADDR_RANGE_F_PRODUCER;

    RTAcpiResourceReset(hAcpiRes);
    int vrc = RTAcpiResourceAddQWordMemoryRange(hAcpiRes, kAcpiResMemRangeCacheability_NonCacheable, kAcpiResMemType_Memory, true /*fRw*/,
                                                fAddrSpace, u64AddrBase, u64AddrBase + cbMmio - 1, 0 /*u64OffTrans*/, 0 /*u64Granularity*/,
                                                cbMmio);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTAcpiResourceSeal(hAcpiRes);
        if (RT_SUCCESS(vrc))
            vrc = RTAcpiTblResourceAppend(hDsdt, hAcpiRes);
    }

    return vrc;
}


int SystemTableBuilderAcpi::initInstance(void)
{
    m_fTpm20 = false;
    m_fPsciUseHvc = true;
    m_u32MaintenanceGsiv = 0;

    int vrc = RTAcpiTblCreate(&m_hAcpiDsdt, ACPI_TABLE_HDR_SIGNATURE_DSDT, 6, "ORCL  ", "VBOXDSDT", 1, "VBOX", 1);
    AssertRCReturn(vrc, vrc);

    vrc = RTAcpiResourceCreate(&m_hAcpiRes);
    AssertRCReturn(vrc, vrc);

    /* Append _SB Scope. */
    return RTAcpiTblScopeStart(m_hAcpiDsdt, "\\_SB");
}


int SystemTableBuilderAcpi::finishTables(RTGCPHYS GCPhysTblsStart, RTVFSIOSTREAM hVfsIos,
                                         PRTGCPHYS pGCPhysTblRoot, size_t *pcbTblRoot, size_t *pcbTbls)
{
    int vrc = RTAcpiTblScopeFinalize(m_hAcpiDsdt); /* End \_SB scope */
    AssertRCReturn(vrc, vrc);

    vrc = RTAcpiTblFinalize(m_hAcpiDsdt);
    AssertRCReturn(vrc, vrc);

    RTGCPHYS GCPhysDsdt = GCPhysTblsStart;

    size_t cbAcpiTbls = RTAcpiTblGetSize(m_hAcpiDsdt);
    Assert(cbAcpiTbls);

    /* Write the DSDT. */
    vrc = RTAcpiTblDumpToVfsIoStrm(m_hAcpiDsdt, RTACPITBLTYPE_AML, hVfsIos);
    AssertRCReturn(vrc, vrc);

    GCPhysTblsStart += cbAcpiTbls;

    uint32_t cTbls = 0;
    uint8_t abXsdt[36 + 32 * sizeof(uint64_t)]; RT_ZERO(abXsdt);
    PACPIXSDT pXsdt = (PACPIXSDT)&abXsdt[0];

    /* Build the FADT. */
    size_t cbTbl = 0;
    vrc = buildFadt(hVfsIos, GCPhysDsdt, &cbTbl);
    AssertRCReturn(vrc, vrc);

    pXsdt->au64AddrTbl[cTbls++] = GCPhysTblsStart;
    cbAcpiTbls      += cbTbl;
    GCPhysTblsStart += cbTbl;

    /* Build the GTDT. */
    vrc = buildGtdt(hVfsIos, &cbTbl);
    AssertRCReturn(vrc, vrc);

    pXsdt->au64AddrTbl[cTbls++] = GCPhysTblsStart;
    cbAcpiTbls      += cbTbl;
    GCPhysTblsStart += cbTbl;

    /* Build the MADT. */
    vrc = buildMadt(hVfsIos, &cbTbl);
    AssertRCReturn(vrc, vrc);

    pXsdt->au64AddrTbl[cTbls++] = GCPhysTblsStart;
    cbAcpiTbls      += cbTbl;
    GCPhysTblsStart += cbTbl;

    /* Build the MCFG. */
    vrc = buildMcfg(hVfsIos, &cbTbl);
    AssertRCReturn(vrc, vrc);

    pXsdt->au64AddrTbl[cTbls++] = GCPhysTblsStart;
    cbAcpiTbls      += cbTbl;
    GCPhysTblsStart += cbTbl;

    /* Build TPM2 table if configured. */
    if (m_fTpm20)
    {
        vrc = buildTpm20(hVfsIos, &cbTbl);
        AssertRCReturn(vrc, vrc);

        pXsdt->au64AddrTbl[cTbls++] = GCPhysTblsStart;
        cbAcpiTbls      += cbTbl;
        GCPhysTblsStart += cbTbl;
    }

    /* Build the IORT if an ITS is configured. */
    if (m_GCPhysIntcIts)
    {
        Assert(m_cbMmioIntcIts > 0);
        Assert(m_GCPhysIntcIts != NIL_RTGCPHYS);
        vrc = buildIort(hVfsIos, &cbTbl);
        AssertRCReturn(vrc, vrc);

        pXsdt->au64AddrTbl[cTbls++] = GCPhysTblsStart;
        cbAcpiTbls      += cbTbl;
        GCPhysTblsStart += cbTbl;
    }

    /* Build XSDT. */
    RTGCPHYS GCPhysXsdt = GCPhysTblsStart;
    size_t const cbXsdt = RT_UOFFSETOF_DYN(ACPIXSDT, au64AddrTbl[cTbls]);
    pXsdt->Hdr.u32Signature = ACPI_TABLE_HDR_SIGNATURE_XSDT;
    pXsdt->Hdr.cbTbl        = RT_UOFFSETOF_DYN(ACPIXSDT, au64AddrTbl[cTbls]);
    pXsdt->Hdr.bRevision    = 6;
    pXsdt->Hdr.bChkSum      = 0;
    pXsdt->Hdr.u32OemRevision = 1;
    pXsdt->Hdr.u32CreatorRevision = 1;

    memcpy(&pXsdt->Hdr.abOemId[0],    "ORCLVB",                 6);
    memcpy(&pXsdt->Hdr.abOemTblId[0], "ORCL",                   4);
    memcpy(&pXsdt->Hdr.abOemTblId[4], &pXsdt->Hdr.u32Signature,   4);
    memcpy(&pXsdt->Hdr.abCreatorId[0], "ORCL", 4);
    RTAcpiTblHdrChecksumGenerate(&pXsdt->Hdr, cbXsdt);
    vrc = RTVfsIoStrmWrite(hVfsIos, &abXsdt[0], cbXsdt, true /*fBlocking*/, NULL /*pcbWritten*/);
    AssertRCReturn(vrc, vrc);

    GCPhysTblsStart += cbXsdt;
    cbAcpiTbls      += cbXsdt;

    /* Build XSDP */
    ACPIRSDP Xsdp; RT_ZERO(Xsdp);

    /* ACPI 1.0 part (RSDP) */
    memcpy(Xsdp.abSignature, "RSD PTR ", 8);
    memcpy(Xsdp.abOemId,     "ORCLVB", 6);
    Xsdp.bRevision    = 3;
    Xsdp.u32AddrRsdt  = 0;
    Xsdp.bChkSum      = RTAcpiChecksumGenerate(&Xsdp, RT_OFFSETOF(ACPIRSDP, cbRsdp));

    /* ACPI 2.0 part (XSDP) */
    Xsdp.cbRsdp       = RT_H2LE_U32(sizeof(ACPIRSDP));
    Xsdp.u64AddrXsdt  = RT_H2LE_U64(GCPhysXsdt);
    Xsdp.bExtChkSum   = RTAcpiChecksumGenerate(&Xsdp, sizeof(ACPIRSDP));

    vrc = RTVfsIoStrmWrite(hVfsIos, &Xsdp, sizeof(Xsdp), true /*fBlocking*/, NULL /*pcbWritten*/);
    AssertRCReturn(vrc, vrc);
    cbAcpiTbls     += sizeof(Xsdp);

    *pGCPhysTblRoot = GCPhysTblsStart;
    *pcbTblRoot     = sizeof(Xsdp);
    *pcbTbls        = cbAcpiTbls;

    return VINF_SUCCESS;
}


int SystemTableBuilderAcpi::addCpu(uint32_t idCpu)
{
    RTAcpiTblDeviceStartF(m_hAcpiDsdt, "CP%02RX32", idCpu);

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_HID");
    RTAcpiTblStringAppend(m_hAcpiDsdt, "ACPI0007");

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_UID");
    RTAcpiTblIntegerAppend(m_hAcpiDsdt, idCpu);

    return RTAcpiTblDeviceFinalize(m_hAcpiDsdt);
}


int SystemTableBuilderAcpi::addMemory(RTGCPHYS GCPhysStart, RTGCPHYS cbMem)
{
    RT_NOREF(GCPhysStart, cbMem);
    return VINF_SUCCESS;
}


int SystemTableBuilderAcpi::addMmioDeviceNoIrq(const char *pszVBoxName, uint32_t uInstance, RTGCPHYS GCPhysMmio, RTGCPHYS cbMmio)
{
    PCSYSTEMTABLEDEVICE pSysTblDev = systemTableVBoxDevName2SysTblDevice(pszVBoxName);
    AssertPtrReturn(pSysTblDev, VERR_NOT_FOUND);

    RTAcpiTblDeviceStartF(m_hAcpiDsdt, "%s%RX32", pSysTblDev->pszAcpiName, uInstance);

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_HID");
    if (pSysTblDev->fEisaId)
        RTAcpiTblEisaIdAppend(m_hAcpiDsdt, pSysTblDev->pszAcpiHid);
    else
        RTAcpiTblStringAppend(m_hAcpiDsdt, pSysTblDev->pszAcpiHid);

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_UID");
    RTAcpiTblIntegerAppend(m_hAcpiDsdt, uInstance);

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_CRS");
    int vrc = systemTableAcpiMmioDevResourceNoIrq(m_hAcpiDsdt, m_hAcpiRes, GCPhysMmio, cbMmio);
    AssertRCReturn(vrc, vrc);

    return RTAcpiTblDeviceFinalize(m_hAcpiDsdt);
}


int SystemTableBuilderAcpi::addMmioDevice(const char *pszVBoxName, uint32_t uInstance, RTGCPHYS GCPhysMmio, RTGCPHYS cbMmio,
                                          uint32_t u32Irq)
{
    PCSYSTEMTABLEDEVICE pSysTblDev = systemTableVBoxDevName2SysTblDevice(pszVBoxName);
    AssertPtrReturn(pSysTblDev, VERR_NOT_FOUND);

    RTAcpiTblDeviceStartF(m_hAcpiDsdt, "%s%RX32", pSysTblDev->pszAcpiName, uInstance);

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_HID");
    if (pSysTblDev->fEisaId)
        RTAcpiTblEisaIdAppend(m_hAcpiDsdt, pSysTblDev->pszAcpiHid);
    else
        RTAcpiTblStringAppend(m_hAcpiDsdt, pSysTblDev->pszAcpiHid);

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_UID");
    RTAcpiTblIntegerAppend(m_hAcpiDsdt, uInstance);

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_CRS");
    int vrc = systemTableAcpiMmioDevResource(m_hAcpiDsdt, m_hAcpiRes, GCPhysMmio, cbMmio, u32Irq + GIC_INTID_RANGE_SPI_START);
    AssertRCReturn(vrc, vrc);

    return RTAcpiTblDeviceFinalize(m_hAcpiDsdt);
}


int SystemTableBuilderAcpi::configureGic(uint32_t cCpus, RTGCPHYS GCPhysIntcDist, RTGCPHYS cbMmioIntcDist, RTGCPHYS GCPhysIntcReDist,
                                         RTGCPHYS cbMmioIntcReDist, RTGCPHYS GCPhysIntcIts, RTGCPHYS cbMmioIntcIts)
{
    m_cCpus            = cCpus;
    m_GCPhysIntcDist   = GCPhysIntcDist;
    m_cbMmioIntcDist   = cbMmioIntcDist;
    m_GCPhysIntcReDist = GCPhysIntcReDist;
    m_cbMmioIntcReDist = cbMmioIntcReDist;
    m_GCPhysIntcIts    = GCPhysIntcIts;
    m_cbMmioIntcIts    = cbMmioIntcIts;
    return VINF_SUCCESS;
}


int SystemTableBuilderAcpi::configureArmVirtualization(bool fPsciUseHvc, uint32_t u32MaintenanceGsiv)
{
    m_fPsciUseHvc = fPsciUseHvc;
    m_u32MaintenanceGsiv = u32MaintenanceGsiv;
    return VINF_SUCCESS;
}


int SystemTableBuilderAcpi::configureClock(void)
{
    return VINF_SUCCESS;
}


int SystemTableBuilderAcpi::configurePcieRootBus(const char *pszVBoxName, uint32_t aPinIrqs[4], RTGCPHYS GCPhysMmioPio, RTGCPHYS GCPhysMmioEcam, size_t cbPciMmioEcam,
                                                 RTGCPHYS GCPhysPciMmioBase, RTGCPHYS cbPciMmio, RTGCPHYS GCPhysPciMmio32Base, RTGCPHYS cbPciMmio32)
{
    PCSYSTEMTABLEDEVICE pSysTblDev = systemTableVBoxDevName2SysTblDevice(pszVBoxName);
    AssertPtrReturn(pSysTblDev, VERR_NOT_FOUND);

    m_GCPhysPciMmioEcam = GCPhysMmioEcam; /* Need that for MCFG later. */
    m_bPciBusMax        = 15; /** @todo Make parameter. */

    RTAcpiTblDeviceStartF(m_hAcpiDsdt, "%s%RX32", pSysTblDev->pszAcpiName, 0);

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_HID");
    RTAcpiTblStringAppend(m_hAcpiDsdt, pSysTblDev->pszAcpiHid);

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_CID");
    RTAcpiTblEisaIdAppend(m_hAcpiDsdt, "PNP0A03");

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_UID");
    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_CCA"); /* Cache coherency attribute. */
    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1);

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_CRS");

    uint32_t const fAddrSpace =   RTACPI_RESOURCE_ADDR_RANGE_F_DECODE_TYPE_POS
                                | RTACPI_RESOURCE_ADDR_RANGE_F_MIN_ADDR_FIXED
                                | RTACPI_RESOURCE_ADDR_RANGE_F_MAX_ADDR_FIXED
                                | RTACPI_RESOURCE_ADDR_RANGE_F_PRODUCER;

    RTAcpiResourceReset(m_hAcpiRes);
    int vrc = RTAcpiResourceAddWordBusNumber(m_hAcpiRes, fAddrSpace, 0 /*u16BusMin*/, m_bPciBusMax /*u16BusMax*/,
                                             0 /*u16OffTrans*/, 0 /*u16Granularity*/, m_bPciBusMax + 1 /*u16Length*/);
    AssertRCReturn(vrc, vrc);

    vrc = RTAcpiResourceAddQWordIoRange(m_hAcpiRes, kAcpiResIoRangeType_Translation_Dense, kAcpiResIoRange_Whole,
                                        fAddrSpace, 0 /*u64AddrMin*/, UINT16_MAX /*u64AddrMax*/, GCPhysMmioPio,
                                        0 /*u64Granularity*/, _64K);
    AssertRCReturn(vrc, vrc);

    vrc = RTAcpiResourceAddDWordMemoryRange(m_hAcpiRes, kAcpiResMemRangeCacheability_NonCacheable, kAcpiResMemType_Memory, true /*fRw*/,
                                            fAddrSpace, (uint32_t)GCPhysPciMmio32Base, (uint32_t)(GCPhysPciMmio32Base + cbPciMmio32 - 1),
                                            0 /*u32OffTrans*/, 0 /*u32Granularity*/, (uint32_t)cbPciMmio32);
    AssertRCReturn(vrc, vrc);

    vrc = RTAcpiResourceAddQWordMemoryRange(m_hAcpiRes, kAcpiResMemRangeCacheability_NonCacheable, kAcpiResMemType_Memory, true /*fRw*/,
                                            fAddrSpace, GCPhysPciMmioBase, GCPhysPciMmioBase + cbPciMmio - 1,
                                            0 /*u64OffTrans*/, 0 /*u64Granularity*/, cbPciMmio);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTAcpiResourceSeal(m_hAcpiRes);
        if (RT_SUCCESS(vrc))
            vrc = RTAcpiTblResourceAppend(m_hAcpiDsdt, m_hAcpiRes);
    }
    AssertRCReturn(vrc, vrc);

    /* For the ECAM base we need to define a new device with a new resource template inside the PCI device. */
    RTAcpiTblDeviceStart(m_hAcpiDsdt, "RES0");

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_HID");
    RTAcpiTblEisaIdAppend(m_hAcpiDsdt, "PNP0C02");

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_CRS");
    RTAcpiResourceReset(m_hAcpiRes);
    vrc = RTAcpiResourceAddQWordMemoryRange(m_hAcpiRes, kAcpiResMemRangeCacheability_NonCacheable, kAcpiResMemType_Memory, true /*fRw*/,
                                            fAddrSpace, GCPhysMmioEcam, GCPhysMmioEcam + cbPciMmioEcam - 1,
                                            0 /*u64OffTrans*/, 0 /*u64Granularity*/, cbPciMmioEcam);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTAcpiResourceSeal(m_hAcpiRes);
        if (RT_SUCCESS(vrc))
            vrc = RTAcpiTblResourceAppend(m_hAcpiDsdt, m_hAcpiRes);
    }
    AssertRCReturn(vrc, vrc);

    /* Finish RES0 device. */
    vrc = RTAcpiTblDeviceFinalize(m_hAcpiDsdt);
    AssertRCReturn(vrc, vrc);

    /* Build the PCI interrupt routing table (_PRT). */
    RTAcpiTblNameAppend(m_hAcpiDsdt, "_PRT");
    RTAcpiTblPackageStart(m_hAcpiDsdt, 32 * 4);

    uint32_t iIrqPinSwizzle = 0;

    for (uint32_t i = 0; i < 32; i++)
    {
        for (uint32_t iIrqPin = 0; iIrqPin < 4; iIrqPin++)
        {
            RTAcpiTblPackageStart(m_hAcpiDsdt, 4);
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, (i << 16) | 0xffff);                                             /* ACPI PCI address. */
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, iIrqPin);                                                        /* Interrupt pin (INTA, INTB, ...). */
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);                                                              /* Interrupt destination, unused. */
            RTAcpiTblIntegerAppend(m_hAcpiDsdt,   GIC_INTID_RANGE_SPI_START
                                                + aPinIrqs[(iIrqPinSwizzle + iIrqPin) % 4]);                     /* GSI of the interrupt. */
            RTAcpiTblPackageFinalize(m_hAcpiDsdt);
        }

        iIrqPinSwizzle++;
    }

    RTAcpiTblPackageFinalize(m_hAcpiDsdt);

    /* Create _CBA for the ECAM base. */
    RTAcpiTblNameAppend(m_hAcpiDsdt, "_CBA");
    RTAcpiTblIntegerAppend(m_hAcpiDsdt, GCPhysMmioEcam);

    return RTAcpiTblDeviceFinalize(m_hAcpiDsdt);
}


int SystemTableBuilderAcpi::dumpTables(const char *pszFilename)
{
    return RTAcpiTblDumpToFile(m_hAcpiDsdt, RTACPITBLTYPE_AML, pszFilename);
}


int SystemTableBuilderAcpi::buildIort(RTVFSIOSTREAM hVfsIos, size_t *pcbIort)
{
    uint8_t abIort[  sizeof(ACPIIORT)
                   + sizeof(ACPIIORTNODEITSGROUP)
                   + sizeof(ACPIIORTNODEPCIRC)
                   + sizeof(ACPIIIORTIDMAPPING)];
    RT_ZERO(abIort);
    uint32_t cbIort = 0;
    uint8_t  cNodes = 0;

    PACPIIORT             pIort         = (PACPIIORT)&abIort[0];
    PACPIIORTNODEITSGROUP pItsGroup     = (PACPIIORTNODEITSGROUP)(pIort + 1);
    PACPIIORTNODEPCIRC    pPciRc        = (PACPIIORTNODEPCIRC)(pItsGroup + 1);
    PACPIIIORTIDMAPPING   pPciRcMapping = (PACPIIIORTIDMAPPING)(pPciRc + 1);
    cbIort += sizeof(*pIort);

    /* Add ITS group node (we support only a single ITS). */
    ++cNodes;
    {
        pItsGroup->Hdr.bType  = ACPI_IORT_NODE_TYPE_ITS_GROUP;
        pItsGroup->Hdr.cbThis = sizeof(*pItsGroup);
        pItsGroup->cIts       = 1;
        cbIort += sizeof(*pItsGroup);
    }

    /* Add PCI root complex for a single ITS with a 1:1 mapping of IDs. */
    ++cNodes;
    {
        pPciRc->Hdr.bType         = ACPI_IORT_NODE_TYPE_ROOT_COMPLEX;
        pPciRc->Hdr.cbThis        = sizeof(*pPciRc) + sizeof(*pPciRcMapping);
        pPciRc->Hdr.bRevision     = 2;
        pPciRc->Hdr.cIdMappings   = 1;
        pPciRc->Hdr.offIdArray    = sizeof(*pPciRc);
        pPciRc->bMemAddrSizeLimit = 64;
        cbIort += sizeof(*pPciRc);

        /* Add the ID mapping. */
        pPciRcMapping->fFlags       = ACPI_IORT_ID_MAPPING_SINGLE;
        pPciRcMapping->offOutputRef = sizeof(*pIort);  /** Offset from start of IORT to start of ITS GROUP 0. */
        cbIort += sizeof(*pPciRcMapping);
    }

    /* Finalize the IORT. */
    pIort->Hdr.u32Signature       = ACPI_TABLE_HDR_SIGNATURE_RSVD_IORT;
    pIort->Hdr.cbTbl              = cbIort;
    pIort->Hdr.bRevision          = 1;
    pIort->Hdr.bChkSum            = 0;
    pIort->Hdr.u32OemRevision     = 1;
    pIort->Hdr.u32CreatorRevision = 1;
    pIort->cIortNodes             = cNodes;
    pIort->offFirstNode           = sizeof(ACPIIORT);

    memcpy(&pIort->Hdr.abOemId[0],    "ORCLVB",                 6);
    memcpy(&pIort->Hdr.abOemTblId[0], "ORCL",                   4);
    memcpy(&pIort->Hdr.abOemTblId[4], &pIort->Hdr.u32Signature, 4);
    memcpy(&pIort->Hdr.abCreatorId[0], "ORCL", 4);

    RTAcpiTblHdrChecksumGenerate(&pIort->Hdr, cbIort);
    *pcbIort = cbIort;
    return RTVfsIoStrmWrite(hVfsIos, pIort, cbIort, true /*fBlocking*/, NULL /*pcbWritten*/);
}


int SystemTableBuilderAcpi::buildMadt(RTVFSIOSTREAM hVfsIos, size_t *pcbMadt)
{
    uint8_t  abMadt[_4K];
    uint32_t cbMadt = 0;

    RT_ZERO(abMadt);

    PACPIMADT     pMadt = (PACPIMADT)&abMadt[0];
    PACPIMADTGICC pGicc = (PACPIMADTGICC)(pMadt + 1);

    cbMadt += sizeof(*pMadt);

    /* Include a GIC CPU interface for each CPU. */
    for (uint32_t i = 0; i < m_cCpus; i++)
    {
        pGicc->bType               = ACPI_MADT_INTR_CTRL_TYPE_GICC;
        pGicc->cbThis              = sizeof(*pGicc);
        pGicc->u32CpuId            = i;
        pGicc->u32AcpiCpuUid       = i;
        pGicc->fGicc               = ACPI_MADT_GICC_F_ENABLED;
        pGicc->u64Mpidr            = i;
        pGicc->u32VGicMaintenanceGsiv = m_u32MaintenanceGsiv;

        cbMadt += sizeof(*pGicc);
        pGicc++;
    }

    /* Build the GICD. */
    PACPIMADTGICD pGicd = (PACPIMADTGICD)pGicc;
    pGicd->bType           = ACPI_MADT_INTR_CTRL_TYPE_GICD;
    pGicd->cbThis          = sizeof(*pGicd);
    pGicd->u64PhysAddrBase = m_GCPhysIntcDist;
    pGicd->bGicVersion     = ACPI_MADT_GICD_VERSION_GICv3;

    cbMadt += sizeof(*pGicd);

    /* Build the GICR. */
    PACPIMADTGICR pGicr = (PACPIMADTGICR)(pGicd + 1);
    pGicr->bType                    = ACPI_MADT_INTR_CTRL_TYPE_GICR;
    pGicr->cbThis                   = sizeof(*pGicr);
    pGicr->u64PhysAddrGicrRangeBase = m_GCPhysIntcReDist;
    pGicr->cbGicrRange              = m_cbMmioIntcReDist;

    cbMadt += sizeof(*pGicr);

    /* Build the GITS only if it's enabled for the VM. */
    if (m_GCPhysIntcIts)
    {
        Assert(m_cbMmioIntcIts > 0);
        Assert(m_GCPhysIntcIts != NIL_RTGCPHYS);
        PACPIMADTGICITS pGits = (PACPIMADTGICITS)(pGicr + 1);
        pGits->bType           = ACPI_MADT_INTR_CTRL_TYPE_GIC_ITS;
        pGits->cbThis          = sizeof(*pGits);
        pGits->u64PhysAddrBase = m_GCPhysIntcIts;
        pGits->u32GicItsId     = 0;

        cbMadt += sizeof(*pGits);
    }

    /* Finalize the MADT. */
    pMadt->Hdr.u32Signature       = ACPI_TABLE_HDR_SIGNATURE_APIC;
    pMadt->Hdr.cbTbl              = cbMadt;
    pMadt->Hdr.bRevision          = 6;
    pMadt->Hdr.bChkSum            = 0;
    pMadt->Hdr.u32OemRevision     = 1;
    pMadt->Hdr.u32CreatorRevision = 1;

    memcpy(&pMadt->Hdr.abOemId[0],    "ORCLVB",                 6);
    memcpy(&pMadt->Hdr.abOemTblId[0], "ORCL",                   4);
    memcpy(&pMadt->Hdr.abOemTblId[4], &pMadt->Hdr.u32Signature, 4);
    memcpy(&pMadt->Hdr.abCreatorId[0], "ORCL", 4);
    RTAcpiTblHdrChecksumGenerate(&pMadt->Hdr, cbMadt);
    *pcbMadt = cbMadt;
    return RTVfsIoStrmWrite(hVfsIos, pMadt, cbMadt, true /*fBlocking*/, NULL /*pcbWritten*/);
}


int SystemTableBuilderAcpi::buildMcfg(RTVFSIOSTREAM hVfsIos, size_t *pcbMcfg)
{
    uint8_t  abMcfg[_1K];
    uint32_t cbMcfg = 0;

    RT_ZERO(abMcfg);

    PACPIMCFG      pMcfg  = (PACPIMCFG)&abMcfg[0];
    PACPIMCFGALLOC pAlloc = (PACPIMCFGALLOC)(pMcfg + 1);

    cbMcfg += sizeof(*pMcfg) + sizeof(*pAlloc);

    pAlloc->u64PhysAddrBase = m_GCPhysPciMmioEcam;
    pAlloc->u16PciSegGrpNr  = 0;
    pAlloc->bPciBusFirst    = 0;
    pAlloc->bPciBusLast     = m_bPciBusMax;

    /* Finalize the MADT. */
    pMcfg->Hdr.u32Signature       = ACPI_TABLE_HDR_SIGNATURE_RSVD_MCFG;
    pMcfg->Hdr.cbTbl              = cbMcfg;
    pMcfg->Hdr.bRevision          = 6;
    pMcfg->Hdr.bChkSum            = 0;
    pMcfg->Hdr.u32OemRevision     = 1;
    pMcfg->Hdr.u32CreatorRevision = 1;

    memcpy(&pMcfg->Hdr.abOemId[0],    "ORCLVB",                 6);
    memcpy(&pMcfg->Hdr.abOemTblId[0], "ORCL",                   4);
    memcpy(&pMcfg->Hdr.abOemTblId[4], &pMcfg->Hdr.u32Signature, 4);
    memcpy(&pMcfg->Hdr.abCreatorId[0], "ORCL", 4);
    RTAcpiTblHdrChecksumGenerate(&pMcfg->Hdr, cbMcfg);
    *pcbMcfg = cbMcfg;
    return RTVfsIoStrmWrite(hVfsIos, pMcfg, cbMcfg, true /*fBlocking*/, NULL /*pcbWritten*/);
}


int SystemTableBuilderAcpi::buildGtdt(RTVFSIOSTREAM hVfsIos, size_t *pcbGtdt)
{
    ACPIGTDT Gtdt; RT_ZERO(Gtdt);

#if 1
    Gtdt.u64PhysAddrCntControlBase = UINT64_MAX;
    Gtdt.u32Rsvd                   = 0;
    Gtdt.u32El1SecTimerGsiv        = 0;
    Gtdt.fEl1SecTimer              = 0;
    Gtdt.u32El1NonSecTimerGsiv     = 0x1e; /* Same physical timer INTID as the FDT. */
    Gtdt.fEl1NonSecTimer           = ACPI_GTDT_TIMER_F_INTR_MODE_LEVEL | ACPI_GTDT_TIMER_F_INTR_POLARITY_ACTIVE_HIGH | ACPI_GTDT_TIMER_F_ALWAYS_ON_CAP;
    Gtdt.u32El1VirtTimerGsiv       = 0x1b; /** @todo */
    Gtdt.fEl1VirtTimer             = ACPI_GTDT_TIMER_F_INTR_MODE_LEVEL | ACPI_GTDT_TIMER_F_INTR_POLARITY_ACTIVE_HIGH;
    Gtdt.u32El2TimerGsiv           = 0x1a;
    Gtdt.fEl2Timer                 = ACPI_GTDT_TIMER_F_INTR_MODE_LEVEL | ACPI_GTDT_TIMER_F_INTR_POLARITY_ACTIVE_HIGH;
    Gtdt.u64PhysAddrCndReadBase    = UINT64_MAX;
    Gtdt.cPlatformTimers           = 0;
    Gtdt.offPlatformTimers         = 0;
    Gtdt.u32El2VirtTimerGsiv       = 0;
    Gtdt.fEl2VirtTimer             = 0;
#else /* Nested virt config on AppleSilicon. */
    Gtdt.u64PhysAddrCntControlBase = UINT64_MAX;
    Gtdt.u32Rsvd                   = 0;
    Gtdt.u32El1SecTimerGsiv        = 0;
    Gtdt.fEl1SecTimer              = 0;
    Gtdt.u32El1NonSecTimerGsiv     = 0x1e; /** @todo */
    Gtdt.fEl1NonSecTimer           = ACPI_GTDT_TIMER_F_INTR_MODE_LEVEL | ACPI_GTDT_TIMER_F_INTR_POLARITY_ACTIVE_HIGH | ACPI_GTDT_TIMER_F_ALWAYS_ON_CAP;
    Gtdt.u32El1VirtTimerGsiv       = 0x1b; /** @todo */
    Gtdt.fEl1VirtTimer             = ACPI_GTDT_TIMER_F_INTR_MODE_LEVEL | ACPI_GTDT_TIMER_F_INTR_POLARITY_ACTIVE_HIGH;
    Gtdt.u32El2TimerGsiv           = 0x1a;
    Gtdt.fEl2Timer                 = ACPI_GTDT_TIMER_F_INTR_MODE_LEVEL | ACPI_GTDT_TIMER_F_INTR_POLARITY_ACTIVE_HIGH;
    Gtdt.u64PhysAddrCndReadBase    = UINT64_MAX;
    Gtdt.cPlatformTimers           = 0;
    Gtdt.offPlatformTimers         = 0;
    Gtdt.u32El2VirtTimerGsiv       = 0;
    Gtdt.fEl2VirtTimer             = 0;
#endif

    Gtdt.Hdr.u32Signature = ACPI_TABLE_HDR_SIGNATURE_GTDT;
    Gtdt.Hdr.cbTbl        = sizeof(Gtdt);
    Gtdt.Hdr.bRevision    = 6;
    Gtdt.Hdr.bChkSum      = 0;
    Gtdt.Hdr.u32OemRevision = 1;
    Gtdt.Hdr.u32CreatorRevision = 1;

    memcpy(&Gtdt.Hdr.abOemId[0],    "ORCLVB",                 6);
    memcpy(&Gtdt.Hdr.abOemTblId[0], "ORCL",                   4);
    memcpy(&Gtdt.Hdr.abOemTblId[4], &Gtdt.Hdr.u32Signature,   4);
    memcpy(&Gtdt.Hdr.abCreatorId[0], "ORCL", 4);
    RTAcpiTblHdrChecksumGenerate(&Gtdt.Hdr, sizeof(Gtdt));
    *pcbGtdt = sizeof(Gtdt);
    return RTVfsIoStrmWrite(hVfsIos, &Gtdt, sizeof(Gtdt), true /*fBlocking*/, NULL /*pcbWritten*/);
}


int SystemTableBuilderAcpi::buildFadt(RTVFSIOSTREAM hVfsIos, RTGCPHYS GCPhysXDsdt, size_t *pcbFadt)
{
    /* Build FADT. */
    ACPIFADT Fadt; RT_ZERO(Fadt);

    Fadt.fFeatures         = ACPI_FADT_F_HW_REDUCED_ACPI;
    Fadt.fArmBootArch      = ACPI_FADT_ARM_BOOT_ARCH_F_PSCI_COMP;
    if (m_fPsciUseHvc)
        Fadt.fArmBootArch |= ACPI_FADT_ARM_BOOT_ARCH_F_PSCI_USE_HVC;
    Fadt.bFadtVersionMinor = 3;
    Fadt.u64AddrXDsdt      = GCPhysXDsdt;

    Fadt.Hdr.u32Signature = ACPI_TABLE_HDR_SIGNATURE_FACP;
    Fadt.Hdr.cbTbl        = sizeof(Fadt);
    Fadt.Hdr.bRevision    = 6;
    Fadt.Hdr.bChkSum      = 0;
    Fadt.Hdr.u32OemRevision = 1;
    Fadt.Hdr.u32CreatorRevision = 1;

    memcpy(&Fadt.Hdr.abOemId[0],    "ORCLVB",                 6);
    memcpy(&Fadt.Hdr.abOemTblId[0], "ORCL",                   4);
    memcpy(&Fadt.Hdr.abOemTblId[4], &Fadt.Hdr.u32Signature,   4);
    memcpy(&Fadt.Hdr.abCreatorId[0], "ORCL", 4);
    RTAcpiTblHdrChecksumGenerate(&Fadt.Hdr, sizeof(Fadt));
    *pcbFadt = sizeof(Fadt);
    return RTVfsIoStrmWrite(hVfsIos, &Fadt, sizeof(Fadt), true /*fBlocking*/, NULL /*pcbWritten*/);
}


int SystemTableBuilderAcpi::buildTpm20(RTVFSIOSTREAM hVfsIos, size_t *pcbTpm20)
{
    Assert(m_fTpm20);

    ACPITPM20 Tpm2;
    RT_ZERO(Tpm2);

    Tpm2.u32StartMethod       = m_fCrb ? ACPITBL_TPM20_START_METHOD_CRB : ACPITBL_TPM20_START_METHOD_TIS12;
    Tpm2.u64BaseAddrCrbOrFifo = m_fCrb ? m_GCPhysTpm20Mmio + TPM_CRB_LOCALITY_REG_CTRL_REQ : m_GCPhysTpm20Mmio;
    Tpm2.u16PlatCls           = ACPITBL_TPM20_PLAT_CLS_CLIENT;

    Tpm2.Hdr.u32Signature = ACPI_TABLE_HDR_SIGNATURE_RSVD_TPM2;
    Tpm2.Hdr.cbTbl        = sizeof(Tpm2);
    Tpm2.Hdr.bRevision    = ACPI_TPM20_REVISION;
    Tpm2.Hdr.bChkSum      = 0;
    Tpm2.Hdr.u32OemRevision = 1;
    Tpm2.Hdr.u32CreatorRevision = 1;

    memcpy(&Tpm2.Hdr.abOemId[0],    "ORCLVB",                 6);
    memcpy(&Tpm2.Hdr.abOemTblId[0], "ORCL",                   4);
    memcpy(&Tpm2.Hdr.abOemTblId[4], &Tpm2.Hdr.u32Signature,   4);
    memcpy(&Tpm2.Hdr.abCreatorId[0], "ORCL", 4);
    RTAcpiTblHdrChecksumGenerate(&Tpm2.Hdr, sizeof(Tpm2));
    *pcbTpm20 = sizeof(Tpm2);
    return RTVfsIoStrmWrite(hVfsIos, &Tpm2, sizeof(Tpm2), true /*fBlocking*/, NULL /*pcbWritten*/);
}


int SystemTableBuilderAcpi::configureTpm2(bool fCrb, RTGCPHYS GCPhysMmioStart, RTGCPHYS cbMmio, uint32_t u32Irq)
{
    m_fTpm20          = true;
    m_fCrb            = fCrb;
    m_GCPhysTpm20Mmio = GCPhysMmioStart;

    RTAcpiTblDeviceStartF(m_hAcpiDsdt, "TPM0");

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_HID");
    RTAcpiTblStringAppend(m_hAcpiDsdt, "MSFT0101");

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_CID");
    RTAcpiTblStringAppend(m_hAcpiDsdt, "MSFT0101");

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_STR");
    RTAcpiTblStringAppend(m_hAcpiDsdt, "TPM 2.0 Device");

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_UID");
    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_CRS");
    int vrc = systemTableAcpiMmioDevResource(m_hAcpiDsdt, m_hAcpiRes, GCPhysMmioStart, cbMmio, u32Irq + GIC_INTID_RANGE_SPI_START);
    AssertRCReturn(vrc, vrc);

    RTAcpiTblMethodStart(m_hAcpiDsdt, "_STA", 0, RTACPI_METHOD_F_NOT_SERIALIZED, 0 /*uSyncLvl*/);
    RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0x0f);
    RTAcpiTblMethodFinalize(m_hAcpiDsdt);

    /* Build the PPI interface (this is as a verbatim translation from src/VBox/Devices/PC/vbox-tpm.dsl as possible). */
    RTAcpiTblMethodStart(m_hAcpiDsdt, "TPFS", 1, RTACPI_METHOD_F_SERIALIZED, 0 /*uSyncLvl*/);

        RTAcpiTblIfStart(m_hAcpiDsdt);
            /* LGreaterEqual(Arg0, 0x100). */
            RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LGreaterEqual);
            RTAcpiTblArgOpAppend(m_hAcpiDsdt, 0);
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0x100);

            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);
        RTAcpiTblIfFinalize(m_hAcpiDsdt);

        RTAcpiTblOpRegionAppendEx(m_hAcpiDsdt, "TPP1", kAcpiOperationRegionSpace_SystemMemory);
        RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Add); /* Region offset */
        RTAcpiTblIntegerAppend(m_hAcpiDsdt, m_GCPhysTpm20Mmio + 0x5000);
        RTAcpiTblArgOpAppend(m_hAcpiDsdt, 0); /* Arg0 */
        RTAcpiTblNullNameAppend(m_hAcpiDsdt); /* Target -> NullName */
        RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1); /* Region size */

        /* Define the field accessor. */
        const RTACPIFIELDENTRY aPpiTpp1Fields[] =
        {
            { "TPPF",  8 },
        };
        RTAcpiTblFieldAppend(m_hAcpiDsdt, "TPP1", kAcpiFieldAcc_Any, false /*fLock*/, kAcpiFieldUpdate_Preserve,
                             &aPpiTpp1Fields[0], RT_ELEMENTS(aPpiTpp1Fields));
        RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
        RTAcpiTblNameStringAppend(m_hAcpiDsdt, "TPPF");

    RTAcpiTblMethodFinalize(m_hAcpiDsdt);


    /* Device specific method. */
    RTAcpiTblMethodStart(m_hAcpiDsdt, "_DSM", 4, RTACPI_METHOD_F_SERIALIZED, 0 /*uSyncLvl*/);

    /* Define the MMIO region for the PPI interface. */
    RTAcpiTblOpRegionAppend(m_hAcpiDsdt, "TPMP", kAcpiOperationRegionSpace_SystemMemory,
                            m_GCPhysTpm20Mmio + 0x5100, 0x5a /*cbRegion*/);

    /* Define the field accessors for the PPI interface. */
    const RTACPIFIELDENTRY aPpiFields[] =
    {
        { "PPIN",  8 },
        { "PPIP", 32 },
        { "PPRP", 32 },
        { "PPRQ", 32 },
        { "PPRM", 32 },
        { "LPPR", 32 }
    };
    RTAcpiTblFieldAppend(m_hAcpiDsdt, "TPMP", kAcpiFieldAcc_Any, false /*fLock*/, kAcpiFieldUpdate_Preserve,
                         &aPpiFields[0], RT_ELEMENTS(aPpiFields));

    /* Define some packages we need later on. */
    RTAcpiTblNameAppend(m_hAcpiDsdt, "TPB2");
        RTAcpiTblPackageStart(m_hAcpiDsdt, 2 /*cElements*/);
        RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);
        RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);
        RTAcpiTblPackageFinalize(m_hAcpiDsdt);

    RTAcpiTblNameAppend(m_hAcpiDsdt, "TPB3");
        RTAcpiTblPackageStart(m_hAcpiDsdt, 3 /*cElements*/);
        RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);
        RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);
        RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);
        RTAcpiTblPackageFinalize(m_hAcpiDsdt);


    /*
     * Check that the UUID in Arg0 contains the Physical Presence Interface Specification UUID.
     */
    RTAcpiTblIfStart(m_hAcpiDsdt);

    /* Predicate (LEqual(Arg0, ToUUID("3dddfaa6-361b-4eb4-a424-8d10089d1653")))*/
    RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
    RTAcpiTblArgOpAppend(m_hAcpiDsdt, 0);
    RTAcpiTblUuidAppendFromStr(m_hAcpiDsdt, "3dddfaa6-361b-4eb4-a424-8d10089d1653");

        /* Standard _DSM query function. */
        RTAcpiTblIfStart(m_hAcpiDsdt);

            /* LEqual(Arg2, Zero). */
            RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
            RTAcpiTblArgOpAppend(m_hAcpiDsdt, 2);
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);

            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
            uint8_t abDsmQuery[2] = { 0xff, 0x01 };
            RTAcpiTblBufferAppend(m_hAcpiDsdt, &abDsmQuery[0], sizeof(abDsmQuery));

        RTAcpiTblIfFinalize(m_hAcpiDsdt);

        /* Query supported PPI revision . */
        RTAcpiTblIfStart(m_hAcpiDsdt);

            /* LEqual(Arg2, Zero). */
            RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
            RTAcpiTblArgOpAppend(m_hAcpiDsdt, 2);
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1);

            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
            RTAcpiTblStringAppend(m_hAcpiDsdt, "1.3");

        RTAcpiTblIfFinalize(m_hAcpiDsdt);


        /* Submit TPM Operation Requested to pre-OS environment. */
        RTAcpiTblIfStart(m_hAcpiDsdt);

            /* LEqual(Arg2, Zero). */
            RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
            RTAcpiTblArgOpAppend(m_hAcpiDsdt, 2);
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 2);

            /* Store(DerefOf(Index(Arg3, Zero)), Local0) */
            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_DerefOf);
                    RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Index);
                    RTAcpiTblArgOpAppend(m_hAcpiDsdt, 3);   /* Arg3 */
                    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0); /* Zero */
                    RTAcpiTblNullNameAppend(m_hAcpiDsdt);   /* Target -> NullName */
                RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 0); /* Local0 */

            /* Store (TPFS (Local0), Local1) */
            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                RTAcpiTblNameStringAppend(m_hAcpiDsdt, "TPFS");
                    RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 0); /* Local0 */
                RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 1); /* Local1 */

            /* If (LEqual (And (Local1, 0x07), Zero)) */
            RTAcpiTblIfStart(m_hAcpiDsdt);
                RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
                    RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_And);
                        RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 1); /* Local1 */
                        RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0x7);
                        RTAcpiTblNullNameAppend(m_hAcpiDsdt);   /* Target -> NullName */
                    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);

                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1);

            RTAcpiTblIfFinalize(m_hAcpiDsdt);

            /* Store (Local0, PPRQ) */
            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 0); /* Local0 */
                RTAcpiTblNameStringAppend(m_hAcpiDsdt, "PPRQ");
            /* Store (Zero, PPRM) */
            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);
                RTAcpiTblNameStringAppend(m_hAcpiDsdt, "PPRM");

            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);

        RTAcpiTblIfFinalize(m_hAcpiDsdt);


        /* Get Pending TPM Operation Requested by the OS. */
        RTAcpiTblIfStart(m_hAcpiDsdt);

            /* LEqual(Arg2, Zero). */
            RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
            RTAcpiTblArgOpAppend(m_hAcpiDsdt, 2);
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 3);

            RTAcpiTblIfStart(m_hAcpiDsdt);

                /* LEqual(Arg1, One). */
                RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
                RTAcpiTblArgOpAppend(m_hAcpiDsdt, 1);
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1);

                /* Store (PPRQ, Index(TPB2, One) */
                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                    RTAcpiTblNameStringAppend(m_hAcpiDsdt, "PPRQ");
                    RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Index);
                        RTAcpiTblNameStringAppend(m_hAcpiDsdt, "TPB2");
                        RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1);
                        RTAcpiTblNullNameAppend(m_hAcpiDsdt);   /* Target -> NullName */

                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
                    RTAcpiTblNameStringAppend(m_hAcpiDsdt, "TPB2");

            RTAcpiTblIfFinalize(m_hAcpiDsdt);

            RTAcpiTblIfStart(m_hAcpiDsdt);

                /* LEqual(Arg1, 0x02). */
                RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
                RTAcpiTblArgOpAppend(m_hAcpiDsdt, 1);
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 2);

                /* Store (PPRQ, Index(TPB3, One) */
                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                    RTAcpiTblNameStringAppend(m_hAcpiDsdt, "PPRQ");
                    RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Index);
                        RTAcpiTblNameStringAppend(m_hAcpiDsdt, "TPB3");
                        RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1);
                        RTAcpiTblNullNameAppend(m_hAcpiDsdt);   /* Target -> NullName */

                /* Store (PPRM, Index(TPB3, 0x2) */
                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                    RTAcpiTblNameStringAppend(m_hAcpiDsdt, "PPRM");
                    RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Index);
                        RTAcpiTblNameStringAppend(m_hAcpiDsdt, "TPB3");
                        RTAcpiTblIntegerAppend(m_hAcpiDsdt, 2);
                        RTAcpiTblNullNameAppend(m_hAcpiDsdt);   /* Target -> NullName */

                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
                    RTAcpiTblNameStringAppend(m_hAcpiDsdt, "TPB2");

            RTAcpiTblIfFinalize(m_hAcpiDsdt);

            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
                RTAcpiTblNameStringAppend(m_hAcpiDsdt, "TPB3");

        RTAcpiTblIfFinalize(m_hAcpiDsdt);


        /* Get Platform-specific Action to Transition to Pre-OS Environment. */
        RTAcpiTblIfStart(m_hAcpiDsdt);

            /* LEqual(Arg2, Zero). */
            RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
            RTAcpiTblArgOpAppend(m_hAcpiDsdt, 2);
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 4);

            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 2); /* Reboot */

        RTAcpiTblIfFinalize(m_hAcpiDsdt);


        /* Return TPM Operation Response to OS Environment. */
        RTAcpiTblIfStart(m_hAcpiDsdt);

            /* LEqual(Arg2, Zero). */
            RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
            RTAcpiTblArgOpAppend(m_hAcpiDsdt, 2);
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 5);

            /* Store (LPPR, Index(TPB3, One) */
            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                RTAcpiTblNameStringAppend(m_hAcpiDsdt, "LPPR");
                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Index);
                    RTAcpiTblNameStringAppend(m_hAcpiDsdt, "TPB3");
                    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1);
                    RTAcpiTblNullNameAppend(m_hAcpiDsdt);   /* Target -> NullName */

            /* Store (PPRP, Index(TPB3, 0x2) */
            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                RTAcpiTblNameStringAppend(m_hAcpiDsdt, "PPRP");
                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Index);
                    RTAcpiTblNameStringAppend(m_hAcpiDsdt, "TPB3");
                    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 2);
                    RTAcpiTblNullNameAppend(m_hAcpiDsdt);   /* Target -> NullName */

            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
                RTAcpiTblNameStringAppend(m_hAcpiDsdt, "TPB3");

        RTAcpiTblIfFinalize(m_hAcpiDsdt);


        /* Submit Preferred user language - deprecated. */
        RTAcpiTblIfStart(m_hAcpiDsdt);

            /* LEqual(Arg2, Zero). */
            RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
            RTAcpiTblArgOpAppend(m_hAcpiDsdt, 2);
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 6);

            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 3); /* Not implemented */

        RTAcpiTblIfFinalize(m_hAcpiDsdt);


        /* Submit TPM Operation Request to Pre-OS Environment 2. */
        RTAcpiTblIfStart(m_hAcpiDsdt);

            /* LEqual(Arg2, Zero). */
            RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
            RTAcpiTblArgOpAppend(m_hAcpiDsdt, 2);
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 7);

            /* Store(DerefOf(Index(Arg3, Zero)), Local0) */
            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_DerefOf);
                    RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Index);
                    RTAcpiTblArgOpAppend(m_hAcpiDsdt, 3);   /* Arg3 */
                    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0); /* Zero */
                    RTAcpiTblNullNameAppend(m_hAcpiDsdt);   /* Target -> NullName */
                RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 0); /* Local0 */

            /* Store (TPFS (Local0), Local1) */
            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                RTAcpiTblNameStringAppend(m_hAcpiDsdt, "TPFS");
                    RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 0); /* Local0 */
                RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 1); /* Local1 */

            /* Store(And(Local1, 0x07), Local1) */
            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_And);
                    RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 1); /* Local1 */
                    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0x7);
                    RTAcpiTblNullNameAppend(m_hAcpiDsdt);   /* Target -> NullName */
                RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 1); /* Local1 */

            /* LEqual(Local1, Zero) */
            RTAcpiTblIfStart(m_hAcpiDsdt);

                RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
                RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 1);
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);

                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
                    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1);

            RTAcpiTblIfFinalize(m_hAcpiDsdt);

            /* LEqual(Local1, 2) */
            RTAcpiTblIfStart(m_hAcpiDsdt);

                RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
                RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 1);
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 2);

                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
                    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 3);

            RTAcpiTblIfFinalize(m_hAcpiDsdt);

            /* LEqual(Arg1, One) */
            RTAcpiTblIfStart(m_hAcpiDsdt);

                RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
                RTAcpiTblArgOpAppend(m_hAcpiDsdt, 1);
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1);

                /* Store(Local0, PPRQ) */
                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                    RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 0);
                    RTAcpiTblNameStringAppend(m_hAcpiDsdt, "PPRQ");

                /* Store(Zero, PPRM) */
                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);
                    RTAcpiTblNameStringAppend(m_hAcpiDsdt, "PPRM");

            RTAcpiTblIfFinalize(m_hAcpiDsdt);

            /* LEqual(Arg1, 2) */
            RTAcpiTblIfStart(m_hAcpiDsdt);

                RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
                RTAcpiTblArgOpAppend(m_hAcpiDsdt, 1);
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 2);

                /* Store(DerefOf(Index(Arg3, One)), Local2) */
                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                    RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_DerefOf);
                        RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Index);
                        RTAcpiTblArgOpAppend(m_hAcpiDsdt, 3);   /* Arg3 */
                        RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1); /* ONe */
                        RTAcpiTblNullNameAppend(m_hAcpiDsdt);   /* Target -> NullName */
                    RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 2); /* Local2 */

                /* Store(Local0, PPRQ) */
                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                    RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 0);
                    RTAcpiTblNameStringAppend(m_hAcpiDsdt, "PPRQ");

                /* Store(Local2, PPRM) */
                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                    RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 2);
                    RTAcpiTblNameStringAppend(m_hAcpiDsdt, "PPRM");

            RTAcpiTblIfFinalize(m_hAcpiDsdt);

            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);

        RTAcpiTblIfFinalize(m_hAcpiDsdt);


        /* Get User Confirmation Status for Operation. */
        RTAcpiTblIfStart(m_hAcpiDsdt);

            /* LEqual(Arg2, Zero). */
            RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
            RTAcpiTblArgOpAppend(m_hAcpiDsdt, 2);
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 8);

            /* Store(DerefOf(Index(Arg3, Zero)), Local0) */
            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_DerefOf);
                    RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Index);
                    RTAcpiTblArgOpAppend(m_hAcpiDsdt, 3);   /* Arg3 */
                    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0); /* Zero */
                    RTAcpiTblNullNameAppend(m_hAcpiDsdt);   /* Target -> NullName */
                RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 0); /* Local0 */

            /* Store (TPFS (Local0), Local1) */
            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Store);
                RTAcpiTblNameStringAppend(m_hAcpiDsdt, "TPFS");
                    RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 0); /* Local0 */
                RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 1); /* Local1 */

            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_And);
                    RTAcpiTblLocalOpAppend(m_hAcpiDsdt, 1); /* Local1 */
                    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0x7);
                    RTAcpiTblNullNameAppend(m_hAcpiDsdt);   /* Target -> NullName */

        RTAcpiTblIfFinalize(m_hAcpiDsdt);


        /* Return Unknown function. */
        uint8_t bUnkFunc = 0x00;
        RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
        RTAcpiTblBufferAppend(m_hAcpiDsdt, &bUnkFunc, sizeof(bUnkFunc));

    RTAcpiTblIfFinalize(m_hAcpiDsdt);


    /*
     * TCG Platform Reset Attack Mitigation Specification interface.
     */
    RTAcpiTblIfStart(m_hAcpiDsdt);

    /* Predicate (LEqual(Arg0, ToUUID("376054ed-cc13-4675-901c-4756d7f2d45d")))*/
    RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
    RTAcpiTblArgOpAppend(m_hAcpiDsdt, 0);
    RTAcpiTblUuidAppendFromStr(m_hAcpiDsdt, "376054ed-cc13-4675-901c-4756d7f2d45d");

        /* Standard _DSM query function. */
        RTAcpiTblIfStart(m_hAcpiDsdt);

            /* LEqual(Arg2, Zero). */
            RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
            RTAcpiTblArgOpAppend(m_hAcpiDsdt, 2);
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);

            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
            uint8_t bBuf = 0x03;
            RTAcpiTblBufferAppend(m_hAcpiDsdt, &bBuf, sizeof(bBuf));

        RTAcpiTblIfFinalize(m_hAcpiDsdt);

        /* Set Memory Overwrite Request (MOR) bit to specified value. */
        RTAcpiTblIfStart(m_hAcpiDsdt);

            /* LEqual(Arg2, Zero). */
            RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
            RTAcpiTblArgOpAppend(m_hAcpiDsdt, 2);
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1);

            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0); /* Memory always zeroed on reset. */

        RTAcpiTblIfFinalize(m_hAcpiDsdt);

        /* Return Unknown function. */
        bUnkFunc = 0x00;
        RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
        RTAcpiTblBufferAppend(m_hAcpiDsdt, &bUnkFunc, sizeof(bUnkFunc));

    RTAcpiTblIfFinalize(m_hAcpiDsdt);

    /* Return Unknown function. */
    bUnkFunc = 0x00;
    RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
    RTAcpiTblBufferAppend(m_hAcpiDsdt, &bUnkFunc, sizeof(bUnkFunc));

    RTAcpiTblMethodFinalize(m_hAcpiDsdt);

    return RTAcpiTblDeviceFinalize(m_hAcpiDsdt);
}


int SystemTableBuilderAcpi::configureGpioDevice(const char *pszVBoxName, uint32_t uInstance, RTGCPHYS GCPhysMmio, RTGCPHYS cbMmio, uint32_t u32Irq,
                                                uint16_t u16PinShutdown, uint16_t u16PinSuspend)
{
    PCSYSTEMTABLEDEVICE pSysTblDev = systemTableVBoxDevName2SysTblDevice(pszVBoxName);
    AssertPtrReturn(pSysTblDev, VERR_NOT_FOUND);

    RTAcpiTblDeviceStartF(m_hAcpiDsdt, "%s%RX32", pSysTblDev->pszAcpiName, uInstance);

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_HID");
    RTAcpiTblStringAppend(m_hAcpiDsdt, pSysTblDev->pszAcpiHid);

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_UID");
    RTAcpiTblIntegerAppend(m_hAcpiDsdt, uInstance);

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_CRS");

    int vrc = systemTableAcpiMmioDevResource(m_hAcpiDsdt, m_hAcpiRes, GCPhysMmio, cbMmio, u32Irq + GIC_INTID_RANGE_SPI_START);
    AssertRCReturn(vrc, vrc);

#if 0 /* Works fine with Linux (also older ones), doesn't with Windows (talk about "standards"). */
    /* Construct the _AEI containing the GPIO configuration. */
    RTAcpiTblNameAppend(m_hAcpiDsdt, "_AEI");

    RTAcpiResourceReset(m_hAcpiRes);
    vrc = RTAcpiResourceAddGpioInt(m_hAcpiRes, kAcpiResGpioMod_Level, kAcpiResGpioPol_ActiveHigh, kAcpiResGpioShr_Exclusive,
                                   kAcpiResGpioPpi_PullDown, 0 /*u16DebounceWait*/, "GPI0",
                                   &u16PinShutdown, 1 /* cPins */);
    if (RT_SUCCESS(vrc))
        vrc = RTAcpiResourceAddGpioInt(m_hAcpiRes, kAcpiResGpioMod_Level, kAcpiResGpioPol_ActiveHigh, kAcpiResGpioShr_Exclusive,
                                       kAcpiResGpioPpi_PullDown, 0 /*u16DebounceWait*/, "GPI0",
                                       &u16PinSuspend, 1 /* cPins */);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTAcpiResourceSeal(m_hAcpiRes);
        if (RT_SUCCESS(vrc))
            vrc = RTAcpiTblResourceAppend(m_hAcpiDsdt, m_hAcpiRes);
    }
    AssertRCReturn(vrc, vrc);

    /* The interrupt methods for the pins. */
    RTAcpiTblMethodStartF(m_hAcpiDsdt, 0 /*cArgs*/, RTACPI_METHOD_F_NOT_SERIALIZED, 0 /*uSyncLvl*/,
                          "_L%02RX16", u16PinShutdown);
        RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Notify);
            RTAcpiTblNameStringAppend(m_hAcpiDsdt, "PBTN");
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0x80);
    RTAcpiTblMethodFinalize(m_hAcpiDsdt);

    RTAcpiTblMethodStartF(m_hAcpiDsdt, 0 /*cArgs*/, RTACPI_METHOD_F_NOT_SERIALIZED, 0 /*uSyncLvl*/,
                          "_L%02RX16", u16PinSuspend);
        RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Notify);
            RTAcpiTblNameStringAppend(m_hAcpiDsdt, "SBTN");
            RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0x80);
    RTAcpiTblMethodFinalize(m_hAcpiDsdt);

    RTAcpiTblDeviceFinalize(m_hAcpiDsdt);

    /* Now create the shutdown and suspend button devices. */
    RTAcpiTblDeviceStart(m_hAcpiDsdt, "PBTN");

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_HID");
    RTAcpiTblEisaIdAppend(m_hAcpiDsdt, "PNP0C0C");

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_UID");
    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);

    RTAcpiTblDeviceFinalize(m_hAcpiDsdt);

    RTAcpiTblDeviceStart(m_hAcpiDsdt, "SBTN");

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_HID");
    RTAcpiTblEisaIdAppend(m_hAcpiDsdt, "PNP0C0E");

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_UID");
    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);

    return RTAcpiTblDeviceFinalize(m_hAcpiDsdt);
#else /* Works on new Linux guests but not Windows so far... */
    /* Use the approach from https://learn.microsoft.com/en-us/windows-hardware/drivers/hid/acpi-button-device . */

        /* Device specific method. */
        RTAcpiTblMethodStart(m_hAcpiDsdt, "_DSM", 4, RTACPI_METHOD_F_SERIALIZED, 0 /*uSyncLvl*/);

        RTAcpiTblIfStart(m_hAcpiDsdt);

        /* Predicate (LEqual(Arg0, ToUUID("4f248f40-d5e2-499f-834c-27758ea1cd3f")))*/
        RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
        RTAcpiTblArgOpAppend(m_hAcpiDsdt, 0);
        RTAcpiTblUuidAppendFromStr(m_hAcpiDsdt, "4f248f40-d5e2-499f-834c-27758ea1cd3f");

            /* Standard _DSM query function. */
            RTAcpiTblIfStart(m_hAcpiDsdt);

                /* LEqual(Arg2, Zero). */
                RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
                RTAcpiTblArgOpAppend(m_hAcpiDsdt, 2);
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);

                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
                uint8_t bBuf = 0x03;
                RTAcpiTblBufferAppend(m_hAcpiDsdt, &bBuf, sizeof(bBuf));

            RTAcpiTblIfFinalize(m_hAcpiDsdt);

            /* Mark the pin for the power button as ActiveHigh. */
            RTAcpiTblIfStart(m_hAcpiDsdt);

                /* LEqual(Arg2, One). */
                RTAcpiTblBinaryOpAppend(m_hAcpiDsdt, kAcpiBinaryOp_LEqual);
                RTAcpiTblArgOpAppend(m_hAcpiDsdt, 2);
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1);

                RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
                RTAcpiTblPackageStart(m_hAcpiDsdt, 2 /*cElements*/);
                    RTAcpiTblIntegerAppend(m_hAcpiDsdt, u16PinShutdown);
                    RTAcpiTblIntegerAppend(m_hAcpiDsdt, u16PinSuspend);
                RTAcpiTblPackageFinalize(m_hAcpiDsdt);

            RTAcpiTblIfFinalize(m_hAcpiDsdt);

            /* Return Unknown function. */
            uint8_t bUnkFunc = 0x00;
            RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
            RTAcpiTblBufferAppend(m_hAcpiDsdt, &bUnkFunc, sizeof(bUnkFunc));

        RTAcpiTblIfFinalize(m_hAcpiDsdt);

        /* Return Unknown function. */
        bUnkFunc = 0x00;
        RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
        RTAcpiTblBufferAppend(m_hAcpiDsdt, &bUnkFunc, sizeof(bUnkFunc));

        RTAcpiTblMethodFinalize(m_hAcpiDsdt);

    RTAcpiTblDeviceFinalize(m_hAcpiDsdt); /* Finish the GPI0 GPIO device. */

    RTAcpiTblDeviceStart(m_hAcpiDsdt, "BTNS");

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_HID");
    RTAcpiTblStringAppend(m_hAcpiDsdt, "ACPI0011");

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_UID");
    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);

    RTAcpiTblMethodStart(m_hAcpiDsdt, "_STA", 0, RTACPI_METHOD_F_NOT_SERIALIZED, 0 /*uSyncLvl*/);
    RTAcpiTblStmtSimpleAppend(m_hAcpiDsdt, kAcpiStmt_Return);
    RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0x0f);
    RTAcpiTblMethodFinalize(m_hAcpiDsdt);

    /* Build the resource template. */
    RTAcpiTblNameAppend(m_hAcpiDsdt, "_CRS");
    RTAcpiResourceReset(m_hAcpiRes);
    vrc = RTAcpiResourceAddGpioInt(m_hAcpiRes, kAcpiResGpioMod_Edge, kAcpiResGpioPol_ActiveBoth, kAcpiResGpioShr_Exclusive,
                                   kAcpiResGpioPpi_PullDown, 0 /*u16DebounceWait*/, "\\_SB.GPI0",
                                   &u16PinShutdown, 1 /* cPins */);
    if (RT_SUCCESS(vrc))
        vrc = RTAcpiResourceAddGpioInt(m_hAcpiRes, kAcpiResGpioMod_Edge, kAcpiResGpioPol_ActiveBoth, kAcpiResGpioShr_Exclusive,
                                       kAcpiResGpioPpi_PullDown, 0 /*u16DebounceWait*/, "\\_SB.GPI0",
                                       &u16PinSuspend, 1 /* cPins */);
    if (RT_SUCCESS(vrc))
    {
        vrc = RTAcpiResourceSeal(m_hAcpiRes);
        if (RT_SUCCESS(vrc))
            vrc = RTAcpiTblResourceAppend(m_hAcpiDsdt, m_hAcpiRes);
    }
    AssertRCReturn(vrc, vrc);

    RTAcpiTblNameAppend(m_hAcpiDsdt, "_DSD");
    RTAcpiTblPackageStart(m_hAcpiDsdt, 2 /*cElements*/);
        RTAcpiTblUuidAppendFromStr(m_hAcpiDsdt, "fa6bd625-9ce8-470d-a2c7-b3ca36c4282e");
        RTAcpiTblPackageStart(m_hAcpiDsdt, 3 /*cElements*/);
            RTAcpiTblPackageStart(m_hAcpiDsdt, 5 /*cElements*/);
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);    /* Collection */
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1);    /* Unique collection ID */
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);    /* Top-Level collection */
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0x01); /* Usage Page ("Generic Desktop Page") */
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0x0d); /* Usage ("Portable Device Control") */
            RTAcpiTblPackageFinalize(m_hAcpiDsdt);

            RTAcpiTblPackageStart(m_hAcpiDsdt, 5 /*cElements*/);
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1);    /* Control */
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0);    /* Interrupt index in _CRS for power button */
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1);    /* Unique ID of parent collection */
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0x01); /* Usage Page ("Generic Desktop Page") */
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0x81); /* Usage ("System Power Down") */
            RTAcpiTblPackageFinalize(m_hAcpiDsdt);

            RTAcpiTblPackageStart(m_hAcpiDsdt, 5 /*cElements*/);
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1);    /* Control */
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1);    /* Interrupt index in _CRS for power button */
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 1);    /* Unique ID of parent collection */
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0x01); /* Usage Page ("Generic Desktop Page") */
                RTAcpiTblIntegerAppend(m_hAcpiDsdt, 0x82); /* Usage ("System Sleep") */
            RTAcpiTblPackageFinalize(m_hAcpiDsdt);

        RTAcpiTblPackageFinalize(m_hAcpiDsdt);
    RTAcpiTblPackageFinalize(m_hAcpiDsdt);

    return RTAcpiTblDeviceFinalize(m_hAcpiDsdt);
#endif
}


int SystemTableBuilder::initInstance(void)
{
    return VERR_NOT_IMPLEMENTED;
}


int SystemTableBuilder::finishTables(RTGCPHYS GCPhysTblsStart, RTVFSIOSTREAM hVfsIos,
                                     PRTGCPHYS pGCPhysTblRoot, size_t *pcbTblRoot, size_t *pcbTbls)
{
    RT_NOREF(GCPhysTblsStart, hVfsIos, pGCPhysTblRoot, pcbTblRoot, pcbTbls);
    return VERR_NOT_IMPLEMENTED;
}


int SystemTableBuilder::addCpu(uint32_t idCpu)
{
    RT_NOREF(idCpu);
    return VERR_NOT_IMPLEMENTED;
}


int SystemTableBuilder::addMemory(RTGCPHYS GCPhysStart, RTGCPHYS cbMem)
{
    RT_NOREF(GCPhysStart, cbMem);
    return VERR_NOT_IMPLEMENTED;
}


int SystemTableBuilder::addMmioDeviceNoIrq(const char *pszVBoxName, uint32_t uInstance, RTGCPHYS GCPhysMmio, RTGCPHYS cbMmio)
{
    RT_NOREF(pszVBoxName, uInstance, GCPhysMmio, cbMmio);
    return VERR_NOT_IMPLEMENTED;
}


int SystemTableBuilder::addMmioDevice(const char *pszVBoxName, uint32_t uInstance, RTGCPHYS GCPhysMmio, RTGCPHYS cbMmio, uint32_t u32Irq)
{
    RT_NOREF(pszVBoxName, uInstance, GCPhysMmio, cbMmio, u32Irq);
    return VERR_NOT_IMPLEMENTED;
}


int SystemTableBuilder::configureGic(uint32_t cCpus, RTGCPHYS GCPhysIntcDist, RTGCPHYS cbMmioIntcDist, RTGCPHYS GCPhysIntcReDist,
                                     RTGCPHYS cbMmioIntcReDist, RTGCPHYS GCPhysIntcIts, RTGCPHYS cbMmioIntcIts)
{
    RT_NOREF(cCpus, GCPhysIntcDist, cbMmioIntcDist, GCPhysIntcReDist, cbMmioIntcReDist, GCPhysIntcIts, cbMmioIntcIts);
    return VERR_NOT_IMPLEMENTED;
}


int SystemTableBuilder::configureArmVirtualization(bool fPsciUseHvc, uint32_t u32MaintenanceGsiv)
{
    RT_NOREF(fPsciUseHvc, u32MaintenanceGsiv);
    return VINF_SUCCESS;
}


int SystemTableBuilder::configureClock(void)
{
    return VERR_NOT_IMPLEMENTED;
}


int SystemTableBuilder::configurePcieRootBus(const char *pszVBoxName, uint32_t aPinIrqs[4], RTGCPHYS GCPhysMmioPio, RTGCPHYS GCPhysMmioEcam, size_t cbPciMmioEcam,
                                             RTGCPHYS GCPhysPciMmioBase, RTGCPHYS cbPciMmio, RTGCPHYS GCPhysPciMmio32Base, RTGCPHYS cbPciMmio32)
{
    RT_NOREF(pszVBoxName, aPinIrqs, GCPhysMmioPio, GCPhysMmioEcam, cbPciMmioEcam,
             GCPhysPciMmioBase, cbPciMmio, GCPhysPciMmio32Base, cbPciMmio32);
    return VERR_NOT_IMPLEMENTED;
}


int SystemTableBuilder::configureTpm2(bool fCrb, RTGCPHYS GCPhysMmioStart, RTGCPHYS cbMmio, uint32_t u32Irq)
{
    RT_NOREF(fCrb, GCPhysMmioStart, cbMmio, u32Irq);
    return VERR_NOT_IMPLEMENTED;
}


int SystemTableBuilder::configureGpioDevice(const char *pszVBoxName, uint32_t uInstance, RTGCPHYS GCPhysMmio, RTGCPHYS cbMmio, uint32_t u32Irq,
                                            uint16_t u16PinShutdown, uint16_t u16PinSuspend)
{
    RT_NOREF(pszVBoxName, uInstance, GCPhysMmio, cbMmio, u32Irq, u16PinShutdown, u16PinSuspend);
    return VERR_NOT_IMPLEMENTED;
}


int SystemTableBuilder::dumpTables(const char *pszFilename)
{
    RT_NOREF(pszFilename);
    return VERR_NOT_IMPLEMENTED;
}


SystemTableBuilder *SystemTableBuilder::createInstance(SYSTEMTABLETYPE enmTableType)
{
    AssertReturn(enmTableType == kSystemTableType_Acpi, NULL);

    SystemTableBuilder *pInstance = new SystemTableBuilderAcpi();
    Assert(pInstance);

    int vrc = pInstance->initInstance();
    if (RT_FAILURE(vrc))
    {
        delete pInstance;
        pInstance = NULL;
    }

    return pInstance;
}
