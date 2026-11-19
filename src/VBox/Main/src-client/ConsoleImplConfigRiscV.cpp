/* $Id$ */
/** @file
 * VBox Console COM Class implementation - VM Configuration Bits for RISC-V.
 */

/*
 * Copyright (C) 2026 Alexander Eichner <github@aeichner.de>.
 * Copyright (C) 2023-2026 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_MAIN_CONSOLE
#include "LoggingNew.h"

#include "ConsoleImpl.h"
#include "ResourceStoreImpl.h"
#include "Global.h"
#include "VMMDev.h"

// generated header
#include "SchemaDefs.h"

#include "AutoCaller.h"

#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/fdt.h>
#include <iprt/file.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/system.h>
#if 0 /* enable to play with lots of memory. */
# include <iprt/env.h>
#endif
#include <iprt/stream.h>

#include <VBox/vmm/vmmr3vtable.h>
#include <VBox/vmm/vmapi.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/version.h>
#include <VBox/platforms/vbox-armv8.h>

#include "BusAssignmentManager.h"
#include "ResourceAssignmentManager.h"
#include "SystemTableBuilder.h"
#ifdef VBOX_WITH_EXTPACK
# include "ExtPackManagerImpl.h"
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/* Darwin compile kludge */
#undef PVM

#ifdef VBOX_WITH_VIRT_RISCV
/**
 * Worker for configConstructor.
 *
 * @return  VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   pVM         The cross context VM handle.
 * @param   pVMM        The VMM vtable.
 * @param   pAlock      The automatic lock instance.  This is for when we have
 *                      to leave it in order to avoid deadlocks (ext packs and
 *                      more).
 */
int Console::i_configConstructorRiscV(PUVM pUVM, PVM pVM, PCVMMR3VTABLE pVMM, AutoWriteLock *pAlock)
{
    RT_NOREF(pVM /* when everything is disabled */);
    ComPtr<IMachine> pMachine = i_machine();

    HRESULT         hrc;
    Utf8Str         strTmp;
    Bstr            bstr;

    RTFDT hFdt = NIL_RTFDT;
    int vrc = RTFdtCreateEmpty(&hFdt);
    AssertRCReturn(vrc, vrc);

#define H()         AssertLogRelMsgReturnStmt(!FAILED(hrc), ("hrc=%Rhrc\n", hrc), RTFdtDestroy(hFdt), VERR_MAIN_CONFIG_CONSTRUCTOR_COM_ERROR)
#define VRC()       AssertLogRelMsgReturnStmt(RT_SUCCESS(vrc), ("vrc=%Rrc\n", vrc), RTFdtDestroy(hFdt), vrc)

    /*
     * Get necessary objects and frequently used parameters.
     */
    ComPtr<IVirtualBox> virtualBox;
    hrc = pMachine->COMGETTER(Parent)(virtualBox.asOutParam());                             H();

    ComPtr<IHost> host;
    hrc = virtualBox->COMGETTER(Host)(host.asOutParam());                                   H();

    PlatformArchitecture_T platformArchHost;
    hrc = host->COMGETTER(Architecture)(&platformArchHost);                                 H();

    ComPtr<ISystemProperties> systemProperties;
    hrc = virtualBox->COMGETTER(SystemProperties)(systemProperties.asOutParam());           H();

    ComPtr<IFirmwareSettings> firmwareSettings;
    hrc = pMachine->COMGETTER(FirmwareSettings)(firmwareSettings.asOutParam());             H();

    ComPtr<INvramStore> nvramStore;
    hrc = pMachine->COMGETTER(NonVolatileStore)(nvramStore.asOutParam());                   H();

    hrc = pMachine->COMGETTER(HardwareUUID)(bstr.asOutParam());                             H();
    RTUUID HardwareUuid;
    vrc = RTUuidFromUtf16(&HardwareUuid, bstr.raw());
    AssertRCReturn(vrc, vrc);

    ULONG cRamMBs;
    hrc = pMachine->COMGETTER(MemorySize)(&cRamMBs);                                        H();
    uint64_t const cbRam   = cRamMBs * (uint64_t)_1M;

    ComPtr<IPlatform> platform;
    hrc = pMachine->COMGETTER(Platform)(platform.asOutParam());                             H();

#if 0 /** @todo */
    /* Get the ARM platform object. */
    ComPtr<IPlatformARM> platformARM;
    hrc = platform->COMGETTER(ARM)(platformARM.asOutParam());                               H();
#endif

    ComPtr<IPlatformProperties> pPlatformProperties;
    hrc = platform->COMGETTER(Properties)(pPlatformProperties.asOutParam());                H();

    ChipsetType_T chipsetType;
    hrc = platform->COMGETTER(ChipsetType)(&chipsetType);                                   H();

    ULONG cCpus = 1;
    hrc = pMachine->COMGETTER(CPUCount)(&cCpus);                                            H();
    Assert(cCpus);

    ULONG ulCpuExecutionCap = 100;
    hrc = pMachine->COMGETTER(CPUExecutionCap)(&ulCpuExecutionCap);                         H();

    VMExecutionEngine_T enmExecEngine = VMExecutionEngine_NotSet;
    hrc = pMachine->COMGETTER(VMExecutionEngine)(&enmExecEngine);                           H();

    if (   enmExecEngine != VMExecutionEngine_Default
        && enmExecEngine != VMExecutionEngine_NativeApi)
    {
        return pVMM->pfnVMR3SetError(pUVM, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                     N_("The RISC-V backend doesn't support any other execution engine than 'default' or 'native-api' right now."));
    }

    LogRel(("Guest architecture: ARM\n"));

    Bstr osTypeId;
    hrc = pMachine->COMGETTER(OSTypeId)(osTypeId.asOutParam());                             H();
    LogRel(("Guest OS type: '%s'\n", Utf8Str(osTypeId).c_str()));

    BusAssignmentManager *pBusMgr = mBusMgr = BusAssignmentManager::createInstance(pVMM, chipsetType, IommuType_None);
    ResourceAssignmentManager *pResMgr = ResourceAssignmentManager::createInstance(pVMM, chipsetType, IommuType_None, 32 /*cInterrupts*/,
                                                                                   _4G); /* Start looking for free MMIO regions at 4GiB downwards. */
    SystemTableBuilder *pSysTblsBldAcpi = NULL;

    /*
     * ACPI
     */
    BOOL fACPI;
    hrc = firmwareSettings->COMGETTER(ACPIEnabled)(&fACPI);                                 H();
    if (fACPI)
        pSysTblsBldAcpi = SystemTableBuilder::createInstance(kSystemTableType_Acpi);


    /*
     * Get root node first.
     * This is the only node in the tree.
     */
    PCFGMNODE pRoot = pVMM->pfnCFGMR3GetRootU(pUVM);
    Assert(pRoot);

    /*
     * The VBox platform descriptor, FDT and ACPI tables will reside at the end of the 4GiB
     * address space and we reserve 2MiB for those.
     */
    //RTGCPHYS cbPlatformDesc     = _2M;
    //RTGCPHYS GCPhysPlatformDesc = VBOXPLATFORMARMV8_PHYS_ADDR - (cbPlatformDesc - _64K);

    RTGCPHYS GCPhysRamBase = 0x80000000;
    RTGCPHYS cbRamBase     = RT_MIN(cbRam, _4G - _512M - 0x80000000);

    RTGCPHYS GCPhysFw = 0x20000000;
    RTGCPHYS cbFw     = _32M;

    RTGCPHYS GCPhysFdt = 0x1000;
    RTGCPHYS cbFwFdt   = _64K;

    // catching throws from InsertConfigString and friends.
    try
    {

        /*
         * Set the root (and VMM) level values.
         */
        hrc = pMachine->COMGETTER(Name)(bstr.asOutParam());                                 H();
        InsertConfigString(pRoot,  "Name",                 bstr);
        InsertConfigBytes(pRoot,   "UUID", &HardwareUuid, sizeof(HardwareUuid));
        InsertConfigInteger(pRoot, "NumCPUs",              cCpus);
        InsertConfigInteger(pRoot, "CpuExecutionCap",      ulCpuExecutionCap);
        InsertConfigInteger(pRoot, "TimerMillies",         10);

        /*
         * NEM
         */
        PCFGMNODE pNEM;
        InsertConfigNode(pRoot, "NEM", &pNEM);

        uint32_t idPHandleIntCtrl = RTFdtPHandleAllocate(hFdt);
        Assert(idPHandleIntCtrl != UINT32_MAX);

        uint32_t aidPHandleCpus[VMM_MAX_CPU_COUNT];
        uint32_t aidPHandleCpusIntc[VMM_MAX_CPU_COUNT];
        for (uint32_t i = 0; i < cCpus; i++)
        {
            aidPHandleCpus[i] = RTFdtPHandleAllocate(hFdt);
            Assert(aidPHandleCpus[i] != UINT32_MAX);

            aidPHandleCpusIntc[i] = RTFdtPHandleAllocate(hFdt);
            Assert(aidPHandleCpusIntc[i] != UINT32_MAX);
        }

        vrc = RTFdtNodePropertyAddU32(   hFdt, "interrupt-parent", idPHandleIntCtrl);       VRC();
        vrc = RTFdtNodePropertyAddString(hFdt, "model",            "linux,dummy-virt");     VRC();
        vrc = RTFdtNodePropertyAddU32(   hFdt, "#size-cells",      2);                      VRC();
        vrc = RTFdtNodePropertyAddU32(   hFdt, "#address-cells",   2);                      VRC();
        vrc = RTFdtNodePropertyAddString(hFdt, "compatible",       "linux,dummy-virt");     VRC();

        /*
         * MM values.
         */
        PCFGMNODE pMM;
        InsertConfigNode(pRoot, "MM", &pMM);

        /*
         * Memory setup.
         */
        PCFGMNODE pMem = NULL;
        InsertConfigNode(pMM, "MemRegions", &pMem);

#if 0 /** @todo TPM */
        /*
         * Windows requires the TPM to be available at 0xfed40000 so reserve this region first, even
         * if no TPM is configured.
         */
        RTGCPHYS GCPhysTpm = 0xfed40000;
        RTGCPHYS cbTpm     = 0x5000 + 0x1000; /* TPM + PPI region. */
        hrc = pResMgr->assignFixedMmioRegion("tpm", GCPhysTpm, cbTpm);                              H();
#endif

        /*
         * The firmware ROM will start at the beginning of the address space and span 64MiB
         * After that comes the flash and spans another 64MiB (even if the real size is smaller).
         */
        hrc = pResMgr->assignFixedRomRegion("firmware", GCPhysFw, cbFw);                            H();

        RTGCPHYS GCPhysFlash = 0x22000000;
        RTGCPHYS cbFlash     = _32M;
        hrc = pResMgr->assignFixedMmioRegion("flash", GCPhysFlash, cbFlash);                        H();

        //hrc = pResMgr->assignFixedRomRegion("platform-tables", GCPhysPlatformDesc, cbPlatformDesc); H();

        /*
         * The base RAM will start at 128MiB (end of flash region) and goes up to 4GiB - 512MiB
         * (for the MMIO hole).
         * If more RAM is configured the high region will start at 4GiB.
         */
        hrc = pResMgr->assignFixedRamRegion("RAM Base", GCPhysRamBase, cbRamBase);                  H();
        hrc = pResMgr->assignFixedRamRegion("FDT", GCPhysFdt, cbFwFdt);                             H();

        PCFGMNODE pMemRegion = NULL;
        InsertConfigNode(pMem, "Base", &pMemRegion);
        InsertConfigInteger(pMemRegion, "GCPhysStart", GCPhysRamBase);
        InsertConfigInteger(pMemRegion, "Size", cbRamBase);

        vrc = RTFdtNodeAddF(hFdt, "memory@%RGp", GCPhysRamBase);                            VRC();
        vrc = RTFdtNodePropertyAddCellsU64(hFdt, "reg", 2, GCPhysRamBase, cbRamBase);       VRC();
        vrc = RTFdtNodePropertyAddString(  hFdt, "device_type",      "memory");             VRC();
        vrc = RTFdtNodeFinalize(hFdt);                                                      VRC();

        InsertConfigNode(pMem, "Dtb", &pMemRegion);
        InsertConfigInteger(pMemRegion, "GCPhysStart", GCPhysFdt);
        InsertConfigInteger(pMemRegion, "Size", cbFwFdt);

        if (pSysTblsBldAcpi)
        {
            vrc = pSysTblsBldAcpi->addMemory(GCPhysRamBase, cbRamBase);
            VRC();
        }

        if (cbRamBase < cbRam)
        {
            RTGCPHYS GCPhysRamHigh = _4G;
            RTGCPHYS cbRamHigh     = cbRam - cbRamBase;

            hrc = pResMgr->assignFixedRamRegion("RAM High", GCPhysRamHigh, cbRamHigh);        H();

            InsertConfigNode(pMem, "High", &pMemRegion);
            InsertConfigInteger(pMemRegion, "GCPhysStart", GCPhysRamHigh);
            InsertConfigInteger(pMemRegion, "Size", cbRamHigh);

            vrc = RTFdtNodeAddF(hFdt, "memory@%RGp", GCPhysRamHigh);                        VRC();
            vrc = RTFdtNodePropertyAddCellsU64(hFdt, "reg", 2, GCPhysRamHigh, cbRamHigh);   VRC();
            vrc = RTFdtNodePropertyAddString(  hFdt, "device_type",      "memory");         VRC();
            vrc = RTFdtNodeFinalize(hFdt);                                                  VRC();

            if (pSysTblsBldAcpi)
            {
                vrc = pSysTblsBldAcpi->addMemory(GCPhysRamHigh, cbRamHigh);
                VRC();
            }
        }

        /* Configure the CPUs in the system, only one socket and cluster at the moment. */
        vrc = RTFdtNodeAdd(hFdt, "cpus");                                                   VRC();
        vrc = RTFdtNodePropertyAddU32(hFdt, "#size-cells",    0);                           VRC();
        vrc = RTFdtNodePropertyAddU32(hFdt, "#address-cells", 1);                           VRC();
        vrc = RTFdtNodePropertyAddU32(hFdt, "timebase-frequency", 0x989680);                VRC();

        vrc = RTFdtNodeAdd(hFdt, "socket0");                                                VRC();
        vrc = RTFdtNodeAdd(hFdt, "cluster0");                                               VRC();

        for (uint32_t i = 0; i < cCpus; i++)
        {
            vrc = RTFdtNodeAddF(hFdt, "core%u", i);                                         VRC();
            vrc = RTFdtNodePropertyAddU32(hFdt, "cpu", aidPHandleCpus[i]);                  VRC();
            vrc = RTFdtNodeFinalize(hFdt);                                                  VRC();
        }

        vrc = RTFdtNodeFinalize(hFdt);                                                      VRC();
        vrc = RTFdtNodeFinalize(hFdt);                                                      VRC();

        for (uint32_t i = 0; i < cCpus; i++)
        {
            vrc = RTFdtNodeAddF(hFdt, "cpu@%u", i);                                         VRC();
            vrc = RTFdtNodePropertyAddU32(hFdt, "phandle", aidPHandleCpus[i]);              VRC();
            vrc = RTFdtNodePropertyAddU32(hFdt, "reg", i);                                  VRC();
            vrc = RTFdtNodePropertyAddString(hFdt, "compatible",  "riscv");                 VRC();
            vrc = RTFdtNodePropertyAddString(hFdt, "riscv,isa",  "rv64imafdcbh");           VRC();
            vrc = RTFdtNodePropertyAddStringList(hFdt, "riscv,isa-extensions", 35,
                                                 "i", "m", "a", "f", "d", "c", "h",
                                                 "zba", "zbb", "zbc", "zbs", "zicntr",
                                                 "zicsr", "zifencei", "zihpm", "ziccif",
                                                 "ziccrse", "ziccamoa", "za64rs", "zic64b",
                                                 "zicbom", "zicbop", "zicboz", "zkt",
                                                 "zama16b", "svade", "ssccptr", "sstvecd",
                                                 "sscounterenw", "shcounterenw", "shtvala",
                                                 "shvstvecd", "shvsatpa", "ssstrict",
                                                 "svvptc");                                 VRC();
            vrc = RTFdtNodePropertyAddString(hFdt, "device_type", "cpu");                   VRC();
            vrc = RTFdtNodePropertyAddString(hFdt, "mmu-type",  "riscv,sv48");              VRC();
            vrc = RTFdtNodePropertyAddU32(hFdt, "clock-frequency",  0x77359400);            VRC();

            vrc = RTFdtNodeAdd(hFdt, "interrupt-controller");                               VRC();
            vrc = RTFdtNodePropertyAddU32(hFdt, "phandle", aidPHandleCpusIntc[i]);          VRC();
            vrc = RTFdtNodePropertyAddEmpty(hFdt, "interrupt-controller");                  VRC();
            vrc = RTFdtNodePropertyAddString(hFdt, "compatible",  "riscv,cpu-intc");        VRC();
            vrc = RTFdtNodePropertyAddU32(hFdt, "#interrupt-cells",    1);                  VRC();
            vrc = RTFdtNodePropertyAddU32(hFdt, "#address-cells", 1);                       VRC();
            vrc = RTFdtNodeFinalize(hFdt);                                                  VRC();

            vrc = RTFdtNodeFinalize(hFdt);                                                  VRC();

            if (pSysTblsBldAcpi)
            {
                vrc = pSysTblsBldAcpi->addCpu(i);
                VRC();
            }
        }

        vrc = RTFdtNodeFinalize(hFdt);                                                      VRC();


        /*
         * CPUM values.
         */
        PCFGMNODE pCpum;
        InsertConfigNode(pRoot, "CPUM", &pCpum);
        InsertConfigInteger(pCpum, "ResetPcValue", GCPhysFw);
        InsertConfigInteger(pCpum, "ResetA1Value", GCPhysFdt);

        /*
         * PDM config.
         *  Load drivers in VBoxC.[so|dll]
         */
        vrc = i_configPdm(pMachine, pVMM, pUVM, pRoot);                                      VRC();


        /*
         * VGA.
         */
        ComPtr<IGraphicsAdapter> pGraphicsAdapter;
        hrc = pMachine->COMGETTER(GraphicsAdapter)(pGraphicsAdapter.asOutParam());           H();
        GraphicsControllerType_T enmGraphicsController;
        hrc = pGraphicsAdapter->COMGETTER(GraphicsControllerType)(&enmGraphicsController);   H();

        /*
         * Devices
         */
        PCFGMNODE pDevices = NULL;      /* /Devices */
        PCFGMNODE pDev = NULL;          /* /Devices/Dev/ */
        PCFGMNODE pInst = NULL;         /* /Devices/Dev/0/ */
        PCFGMNODE pCfg = NULL;          /* /Devices/Dev/.../Config/ */
        PCFGMNODE pLunL0 = NULL;        /* /Devices/Dev/0/LUN#0/ */

        InsertConfigNode(pRoot, "Devices", &pDevices);

        InsertConfigNode(pDevices, "pci-generic-ecam-bridge", &pDev);
        InsertConfigNode(pDev,     "0",                       &pInst);
        InsertConfigNode(pInst,    "Config",                  &pCfg);

        InsertConfigNode(pDevices, "platform",              &pDev);
        InsertConfigNode(pDev,     "0",                     &pInst);
        InsertConfigNode(pInst,    "Config",                &pCfg);
        InsertConfigNode(pInst,    "LUN#0",                 &pLunL0);
        InsertConfigString(pLunL0, "Driver",                "ResourceStore");

        /* Add the resources. */
        PCFGMNODE pResources = NULL;    /* /Devices/platform/Config/Resources */
        PCFGMNODE pRes = NULL;          /* /Devices/platform/Config/Resources/<Resource> */
        InsertConfigString(pCfg,        "ResourceNamespace",     "resources");
        InsertConfigNode(pCfg,          "Resources",             &pResources);
        InsertConfigNode(pResources,    "EfiRom",                &pRes);
        InsertConfigInteger(pRes,       "RegisterAsRom",         1);
        InsertConfigInteger(pRes,       "GCPhysLoadAddress",     GCPhysFw);

        /** @todo r=aeichner 32-bit guests and query the firmware type from VBoxSVC. */
        /*
         * Firmware.
         */
        FirmwareType_T eFwType =  FirmwareType_EFI64;
#ifdef VBOX_WITH_EFI_IN_DD2
        const char *pszEfiRomFile = eFwType == FirmwareType_EFIDUAL ? "<INVALID>"
                                  : eFwType == FirmwareType_EFI32   ? "VBoxEFI-riscv32.fd"
                                  :                                   "VBoxEFI-riscv64.fd";
        const char *pszKey = "ResourceId";
#else
        Utf8Str efiRomFile;
        vrc = findEfiRom(virtualBox, PlatformArchitecture_RISCV, eFwType, &efiRomFile);
        AssertRCReturn(vrc, vrc);
        const char *pszEfiRomFile = efiRomFile.c_str();
        const char *pszKey = "Filename";
#endif
        InsertConfigString(pRes,        pszKey,                  pszEfiRomFile);

        InsertConfigNode(pResources,    "Dtb",             &pRes);
        InsertConfigInteger(pRes,       "RegisterAsRom",         0);
        InsertConfigInteger(pRes,       "GCPhysLoadAddress",     GCPhysFdt);
        InsertConfigString(pRes,        "ResourceId",            "VBoxArmV8Desc");

        /*
         * Configure the interrupt controller.
         */
        RTGCPHYS GCPhysMmioStart;
        RTGCPHYS cbMmio;
        hrc = pResMgr->assignMmioRegion("plic", 64 * _1M, &GCPhysMmioStart, &cbMmio); H();

        InsertConfigNode(pDevices, "plic",         &pDev);
        InsertConfigNode(pDev,     "0",            &pInst);
        InsertConfigInteger(pInst, "Trusted",      1); /* boolean */
        InsertConfigNode(pInst,    "Config",       &pCfg);
        InsertConfigInteger(pCfg,  "PlicMmioBase", GCPhysMmioStart);

        vrc = RTFdtNodeAddF(hFdt, "plic@%RGp", GCPhysMmioStart);                        VRC();
        vrc = RTFdtNodePropertyAddU32(hFdt, "#address-cells", 0);                       VRC();
        vrc = RTFdtNodePropertyAddU32(hFdt, "#interrupt-cells", 1);                     VRC();
        vrc = RTFdtNodePropertyAddCellsU64(hFdt, "reg", 2, GCPhysMmioStart, cbMmio);    VRC();
        vrc = RTFdtNodePropertyAddString(  hFdt, "compatible", "riscv,plic0");          VRC();
        vrc = RTFdtNodePropertyAddU32(hFdt, "phandle", idPHandleIntCtrl);               VRC();
        vrc = RTFdtNodePropertyAddU32(hFdt, "riscv,ndev", 0xa0);                        VRC();
        vrc = RTFdtNodePropertyAddEmpty(   hFdt, "interrupt-controller");               VRC();

        uint32_t aIntrExtended[VMM_MAX_CPU_COUNT * 2];
        for (uint32_t i = 0; i < cCpus; i++)
        {
            aIntrExtended[i * 2 + 0] = aidPHandleCpusIntc[i];
            aIntrExtended[i * 2 + 1] = 9;
        }

        vrc = RTFdtNodePropertyAddCellsU32AsArray(hFdt, "interrupts-extended", cCpus * 2, &aIntrExtended[0]); VRC();
        vrc = RTFdtNodeFinalize(hFdt);                                                  VRC();

        if (enmGraphicsController == GraphicsControllerType_QemuRamFB)
        {
            hrc = pResMgr->assignMmioRegion("qemu-fw-cfg", _4K, &GCPhysMmioStart, &cbMmio); H();

            InsertConfigNode(pDevices, "qemu-fw-cfg",   &pDev);
            InsertConfigNode(pDev,     "0",            &pInst);
            InsertConfigNode(pInst,    "Config",        &pCfg);
            InsertConfigInteger(pCfg,  "MmioSize",       cbMmio);
            InsertConfigInteger(pCfg,  "MmioBase", GCPhysMmioStart);
            InsertConfigInteger(pCfg,  "DmaEnabled",        1);
            InsertConfigInteger(pCfg,  "QemuRamfbSupport",  1);
            InsertConfigNode(pInst,    "LUN#0",           &pLunL0);
            InsertConfigString(pLunL0, "Driver",          "MainDisplay");

            vrc = RTFdtNodeAddF(hFdt, "fw-cfg@%RGp", GCPhysMmioStart);                      VRC();
            vrc = RTFdtNodePropertyAddEmpty(   hFdt, "dma-coherent");                       VRC();
            vrc = RTFdtNodePropertyAddCellsU64(hFdt, "reg", 2, GCPhysMmioStart, cbMmio);    VRC();
            vrc = RTFdtNodePropertyAddString(  hFdt, "compatible", "qemu,fw-cfg-mmio");     VRC();
            vrc = RTFdtNodeFinalize(hFdt);                                                  VRC();

            if (pSysTblsBldAcpi)
            {
                vrc = pSysTblsBldAcpi->addMmioDeviceNoIrq("qemu-fw-cfg", 0, GCPhysMmioStart, cbMmio);
                VRC();
            }
        }

        InsertConfigNode(pDevices, "flash-cfi",         &pDev);
        InsertConfigNode(pDev,     "0",            &pInst);
        InsertConfigNode(pInst,    "Config",        &pCfg);
        InsertConfigInteger(pCfg,  "BaseAddress",  GCPhysFlash);
        InsertConfigInteger(pCfg,  "Size",        cbFlash);
        InsertConfigString(pCfg,   "FlashFile",   "nvram");
        /* Attach the NVRAM storage driver. */
        InsertConfigNode(pInst,    "LUN#0",       &pLunL0);
        InsertConfigString(pLunL0, "Driver",      "NvramStore");

        vrc = RTFdtNodeAddF(hFdt, "flash@%RX32", 0);                                        VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "bank-width", 4);                          VRC();
        vrc = RTFdtNodePropertyAddCellsU64(hFdt, "reg", 4,
                                           GCPhysFw,    cbFw,     /* First region (EFI). */
                                           GCPhysFlash, 3 * _256K); /* Second region (NVRAM), see NvramStoreImpl.cpp for an explanation of the size choice. */ VRC();
        vrc = RTFdtNodePropertyAddString(  hFdt, "compatible", "cfi-flash");                VRC();
        vrc = RTFdtNodeFinalize(hFdt);                                                      VRC();

        InsertConfigNode(pDevices, "serial",     &pDev);
        for (ULONG ulInstance = 0; ulInstance < 1 /** @todo SchemaDefs::SerialPortCount*/; ++ulInstance)
        {
            ComPtr<ISerialPort> serialPort;
            hrc = pMachine->GetSerialPort(ulInstance, serialPort.asOutParam());             H();
            BOOL fEnabledSerPort = FALSE;
            if (serialPort)
            {
                hrc = serialPort->COMGETTER(Enabled)(&fEnabledSerPort);                     H();
            }
            if (!fEnabledSerPort)
            {
                m_aeSerialPortMode[ulInstance] = PortMode_Disconnected;
                continue;
            }

            InsertConfigNode(pDev, Utf8StrFmt("%u", ulInstance).c_str(), &pInst);
            InsertConfigInteger(pInst, "Trusted", 1); /* boolean */
            InsertConfigNode(pInst, "Config", &pCfg);

            uint32_t iIrq = 0;
            hrc = pResMgr->assignSingleInterrupt("serial", &iIrq);                               H();
            hrc = pResMgr->assignFixedMmioRegion("serial", 0x10000000, _4K);           H();

            InsertConfigInteger(pCfg,  "IRQ",      iIrq);
            InsertConfigInteger(pCfg,  "MmioBase", 0x10000000);

            vrc = RTFdtNodeAddF(hFdt, "serial@%RGp", 0x10000000);                                VRC();
            vrc = RTFdtNodePropertyAddCellsU32(hFdt, "interrupts", 1, iIrq);                     VRC();
            vrc = RTFdtNodePropertyAddU32(hFdt, "reg-offset", 0);                                VRC();
            vrc = RTFdtNodePropertyAddU32(hFdt, "reg-shift",  2);                                VRC();
            vrc = RTFdtNodePropertyAddU32(hFdt, "current-speed",     0x1c200);                   VRC(); /** @todo */
            vrc = RTFdtNodePropertyAddU32(hFdt, "clock-frequency", 0x3b9aca0);                   VRC(); /** @todo */ 
            vrc = RTFdtNodePropertyAddCellsU64(hFdt, "reg", 2, 0x10000000, _4K);                 VRC();
            vrc = RTFdtNodePropertyAddStringList(hFdt, "compatible", 1, "ns16550");              VRC();
            vrc = RTFdtNodeFinalize(hFdt);                                                       VRC();

            if (pSysTblsBldAcpi)
            {
                vrc = pSysTblsBldAcpi->addMmioDevice("arm-pl011", ulInstance, GCPhysMmioStart, cbMmio, iIrq);
                VRC();
            }

            BOOL  fServer;
            hrc = serialPort->COMGETTER(Server)(&fServer);                                  H();
            hrc = serialPort->COMGETTER(Path)(bstr.asOutParam());                           H();

            PortMode_T eHostMode;
            hrc = serialPort->COMGETTER(HostMode)(&eHostMode);                              H();

            m_aeSerialPortMode[ulInstance] = eHostMode;
            if (eHostMode != PortMode_Disconnected)
            {
                vrc = i_configSerialPort(pInst, eHostMode, Utf8Str(bstr).c_str(), RT_BOOL(fServer));
                if (RT_FAILURE(vrc))
                    return vrc;
            }
        }

        BOOL fRTCUseUTC;
        hrc = platform->COMGETTER(RTCUseUTC)(&fRTCUseUTC);                                  H();

        uint32_t iIrq = 0;
        /** @todo RTC */

        /* Configure gpio keys. */
        /** @todo GPIO */

#if defined(VBOX_WITH_TPM)
        /*
         * Configure the Trusted Platform Module.
         */
        ComObjPtr<ITrustedPlatformModule> ptrTpm;
        TpmType_T enmTpmType = TpmType_None;

        hrc = pMachine->COMGETTER(TrustedPlatformModule)(ptrTpm.asOutParam());                  H();
        hrc = ptrTpm->COMGETTER(Type)(&enmTpmType);                                             H();
        if (enmTpmType != TpmType_None)
        {
            /** @todo TPM */
        }
#endif

        hrc = pResMgr->assignInterrupts("pci-generic-ecam", 4 /*cInterrupts*/, &iIrq);      H();
        uint32_t aPinIrqs[] = { iIrq, iIrq + 1, iIrq + 2, iIrq + 3 };
        RTGCPHYS GCPhysPciMmioEcam, GCPhysPciMmio, GCPhysPciMmio32;
        RTGCPHYS cbPciMmioEcam, cbPciMmio, cbPciMmio32;

        hrc = pResMgr->assignMmioRegionAligned("pci-pio",    _64K, _64K, &GCPhysMmioStart,   &cbMmio, false /*fOnly32Bit*/); H();
        hrc = pResMgr->assignMmioRegion(       "pci-ecam",   16 * _1M,   &GCPhysPciMmioEcam, &cbPciMmioEcam);              H();
        hrc = pResMgr->assignMmio64Region(     "pci-mmio",   _2G,        &GCPhysPciMmio,     &cbPciMmio);                  H();
        hrc = pResMgr->assignMmio32Region(     "pci-mmio32", _256M,      &GCPhysPciMmio32,   &cbPciMmio32);                H();

        InsertConfigNode(pDevices, "pci-generic-ecam",  &pDev);
        InsertConfigNode(pDev,     "0",            &pInst);
        InsertConfigNode(pInst,    "Config",        &pCfg);
        InsertConfigInteger(pCfg,  "MmioEcamBase",   GCPhysPciMmioEcam);
        InsertConfigInteger(pCfg,  "MmioEcamLength", cbPciMmioEcam);
        InsertConfigInteger(pCfg,  "MmioPioBase",    GCPhysMmioStart);
        InsertConfigInteger(pCfg,  "MmioPioSize",    cbMmio);
        InsertConfigInteger(pCfg,  "IntPinA",        aPinIrqs[0]);
        InsertConfigInteger(pCfg,  "IntPinB",        aPinIrqs[1]);
        InsertConfigInteger(pCfg,  "IntPinC",        aPinIrqs[2]);
        InsertConfigInteger(pCfg,  "IntPinD",        aPinIrqs[3]);
        vrc = RTFdtNodeAddF(hFdt, "pcie@%RGp", GCPhysPciMmio);                              VRC();
        vrc = RTFdtNodePropertyAddCellsU32(hFdt, "interrupt-map-mask", 4, 0xf800, 0, 0, 7); VRC();

        uint32_t aIrqCells[32 * 4 * 6]; RT_ZERO(aIrqCells); /* Maximum of 32 devices on the root bus, each supporting 4 interrupts (INTA# ... INTD#). */
        uint32_t *pau32IrqCell = &aIrqCells[0];
        uint32_t iIrqPinSwizzle = 0;

        for (uint32_t i = 0; i < 32; i++)
        {
            for (uint32_t iIrqPin = 0; iIrqPin < 4; iIrqPin++)
            {
                pau32IrqCell[0] = i << 11; /* The dev part, composed as dev.fn. */
                pau32IrqCell[1] = 0;
                pau32IrqCell[2] = 0;
                pau32IrqCell[3] = iIrqPin + 1;
                pau32IrqCell[4] = idPHandleIntCtrl;
                pau32IrqCell[5] = aPinIrqs[(iIrqPinSwizzle + iIrqPin) % RT_ELEMENTS(aPinIrqs)];
                pau32IrqCell += 6;
            }

            iIrqPinSwizzle++;
        }

        vrc = RTFdtNodePropertyAddCellsU32AsArray(hFdt, "interrupt-map", RT_ELEMENTS(aIrqCells), &aIrqCells[0]); VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "#interrupt-cells", 1);                        VRC();
        vrc = RTFdtNodePropertyAddCellsU32(hFdt, "ranges", 21,
                                           0x1000000, 0, 0,
                                           GCPhysMmioStart >> 32, GCPhysMmioStart, cbMmio >> 32, cbMmio,
                                           0x2000000, GCPhysPciMmio32 >> 32, GCPhysPciMmio32, GCPhysPciMmio32 >> 32, GCPhysPciMmio32,
                                           cbPciMmio32 >> 32, cbPciMmio32,
                                           0x3000000, GCPhysPciMmio >> 32, GCPhysPciMmio, GCPhysPciMmio >> 32, GCPhysPciMmio,
                                           cbPciMmio >> 32, cbPciMmio);                         VRC();
        vrc = RTFdtNodePropertyAddCellsU64(hFdt, "reg", 2, GCPhysPciMmioEcam, cbPciMmioEcam);   VRC();
        vrc = RTFdtNodePropertyAddEmpty(   hFdt, "dma-coherent");                               VRC();
        vrc = RTFdtNodePropertyAddCellsU32(hFdt, "bus-range", 2, 0, 0xf);                       VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "linux,pci-domain", 0);                        VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "#size-cells", 2);                             VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "#address-cells", 3);                          VRC();
        vrc = RTFdtNodePropertyAddString(  hFdt, "device_type", "pci");                         VRC();
        vrc = RTFdtNodePropertyAddString(  hFdt, "compatible", "pci-host-ecam-generic");        VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "interrupt-parent", idPHandleIntCtrl);         VRC();
        vrc = RTFdtNodeFinalize(hFdt);                                                          VRC();

        if (pSysTblsBldAcpi)
        {
            vrc = pSysTblsBldAcpi->configurePcieRootBus("pci-generic-ecam", aPinIrqs, GCPhysMmioStart, GCPhysPciMmioEcam,
                                                        cbPciMmioEcam, GCPhysMmioStart, cbMmio, GCPhysPciMmio32, cbPciMmio32);
            VRC();
        }

        /*
         * VMSVGA compliant graphics controller.
         */
        if (   enmGraphicsController != GraphicsControllerType_QemuRamFB
            && enmGraphicsController != GraphicsControllerType_Null)
        {
            vrc = i_configGraphicsController(pDevices, enmGraphicsController, pBusMgr, pMachine,
                                             pGraphicsAdapter, firmwareSettings,
                                             true /*fForceVmSvga3*/, false /*fExposeLegacyVga*/);   VRC();
        }

        /*
         * The USB Controllers and input devices.
         */
#if 0 /** @todo Make us of this and disallow PS/2 for ARM VMs for now. */
        KeyboardHIDType_T aKbdHID;
        hrc = pMachine->COMGETTER(KeyboardHIDType)(&aKbdHID);                               H();
#endif

        PointingHIDType_T aPointingHID;
        hrc = pMachine->COMGETTER(PointingHIDType)(&aPointingHID);                          H();

        PCFGMNODE pUsbDevices = NULL;
        vrc = i_configUsb(pMachine, pBusMgr, pRoot, pDevices, KeyboardHIDType_USBKeyboard, aPointingHID, &pUsbDevices);

        /*
         * Storage controllers.
         */
        bool fFdcEnabled = false;
        vrc = i_configStorageCtrls(pMachine, pBusMgr, pVMM, pUVM,
                                   pDevices, pUsbDevices, NULL /*pBiosCfg*/, &fFdcEnabled);      VRC();

        /*
         * Network adapters
         */
        std::list<BootNic> llBootNics;
        vrc = i_configNetworkCtrls(pMachine, pPlatformProperties, chipsetType, pBusMgr,
                                   pVMM, pUVM, pDevices, pUsbDevices, llBootNics);               VRC();

        /*
         * The VMM device.
         */
        vrc = i_configVmmDev(pMachine, pBusMgr, pDevices, true /*fMmioReq*/);                    VRC();

        /*
         * Audio configuration.
         */
        bool fAudioEnabled = false;
        vrc = i_configAudioCtrl(virtualBox, pMachine, pBusMgr, pDevices,
                                false /*fOsXGuest*/, &fAudioEnabled);                            VRC();

        /*
         * Configure DBGF (Debug(ger) Facility) and DBGC (Debugger Console).
         */
        vrc = i_configGuestDbg(virtualBox, pMachine, pRoot);                                     VRC();
    }
    catch (ConfigError &x)
    {
        RTFdtDestroy(hFdt);

        // InsertConfig threw something:
        pVMM->pfnVMR3SetError(pUVM, x.m_vrc, RT_SRC_POS, "Caught ConfigError: %Rrc - %s", x.m_vrc, x.what());
        return x.m_vrc;
    }
    catch (HRESULT hrcXcpt)
    {
        RTFdtDestroy(hFdt);
        AssertLogRelMsgFailedReturn(("hrc=%Rhrc\n", hrcXcpt), VERR_MAIN_CONFIG_CONSTRUCTOR_COM_ERROR);
    }

#ifdef VBOX_WITH_EXTPACK
    /*
     * Call the extension pack hooks if everything went well thus far.
     */
    if (RT_SUCCESS(vrc))
    {
        pAlock->release();
        vrc = mptrExtPackManager->i_callAllVmConfigureVmmHooks(this, pVM, pVMM);
        pAlock->acquire();
        AssertRCReturnStmt(vrc, RTFdtDestroy(hFdt), vrc);
    }
#endif

#if 1
    vrc = RTFdtNodeAdd(hFdt, "chosen");                                                 VRC();
    vrc = RTFdtNodePropertyAddString(  hFdt, "stdout-path", "serial@10000000");         VRC();
    vrc = RTFdtNodeFinalize(hFdt);
#endif

    /* Finalize the FDT and add it to the resource store. */
    vrc = RTFdtFinalize(hFdt);
    AssertRCReturnStmt(vrc, RTFdtDestroy(hFdt), vrc);

    RTVFSFILE hVfsFileDesc = NIL_RTVFSFILE;
    vrc = RTVfsMemFileCreate(NIL_RTVFSIOSTREAM, 0 /*cbEstimate*/, &hVfsFileDesc);
    AssertRCReturnStmt(vrc, RTFdtDestroy(hFdt), vrc);
    RTVFSIOSTREAM hVfsIosDesc = RTVfsFileToIoStream(hVfsFileDesc);
    AssertRelease(hVfsIosDesc != NIL_RTVFSIOSTREAM);

    /* Initialize the VBox platform descriptor. */
    vrc = RTFdtDumpToVfsIoStrm(hFdt, RTFDTTYPE_DTB, 0 /*fFlags*/, hVfsIosDesc, NULL /*pErrInfo*/);
    uint64_t cbFdt = 0;
    if (RT_SUCCESS(vrc))
        vrc = RTVfsFileQuerySize(hVfsFileDesc, &cbFdt);
/** @todo r=bird: leaking hVfsIosDesc among other stuff here. */
    AssertRCReturnStmt(vrc, RTFdtDestroy(hFdt), vrc);

    vrc = RTVfsIoStrmZeroFill(hVfsIosDesc, (RTFOFF)(RT_ALIGN_64(cbFdt, _64K) - cbFdt));
/** @todo r=bird: Leaks everything here (just restructure the code;
 *        what if we throw errors?) */
    AssertRCReturn(vrc, vrc);

    cbFdt = RT_ALIGN_64(cbFdt, _64K);

    RTGCPHYS GCPhysMmioStart = NIL_RTGCPHYS;
    RTGCPHYS cbMmio = 0;
    hrc = pResMgr->queryMmioRegion(&GCPhysMmioStart, &cbMmio);
    Assert(SUCCEEDED(hrc));

    RTGCPHYS GCPhysMmio32Start = NIL_RTGCPHYS;
    RTGCPHYS cbMmio32 = 0;
    hrc = pResMgr->queryMmio32Region(&GCPhysMmio32Start, &cbMmio32);
    Assert(SUCCEEDED(hrc));

#if 0
    RTGCPHYS GCPhysXsdp = NIL_RTGCPHYS;
    size_t cbAcpiXsdp = 0;
    size_t cbAcpi     = 0;
    if (pSysTblsBldAcpi)
    {
        vrc = pSysTblsBldAcpi->finishTables(GCPhysPlatformDesc + cbFdt,
                                            hVfsIosDesc, &GCPhysXsdp, &cbAcpiXsdp, &cbAcpi);
        AssertRCReturn(vrc, vrc);
        Assert(   GCPhysXsdp > GCPhysPlatformDesc
               && GCPhysXsdp < VBOXPLATFORMARMV8_PHYS_ADDR);

        /* Dump the ACPI table for debugging purposes if requested. */
        Bstr SysTblsDumpVal;
        hrc = mMachine->GetExtraData(Bstr("VBoxInternal2/DumpSysTables").raw(),
                                     SysTblsDumpVal.asOutParam());
        if (   hrc == S_OK
            && SysTblsDumpVal.isNotEmpty())
        {
            vrc = pSysTblsBldAcpi->dumpTables(Utf8Str(SysTblsDumpVal).c_str());
/** @todo r=bird: Leaks everything here */
            AssertRCReturn(vrc, vrc);
        }

        delete pSysTblsBldAcpi;

        vrc = RTVfsIoStrmZeroFill(hVfsIosDesc, (RTFOFF)(RT_ALIGN_64(cbAcpi, _64K) - cbAcpi));
        AssertRCReturn(vrc, vrc);

        cbAcpi = RT_ALIGN_64(cbAcpi, _64K);
    }

    /* Fill the room until the end where the platform descriptor lives. */
    vrc = RTVfsIoStrmZeroFill(hVfsIosDesc, cbPlatformDesc - sizeof(VBOXPLATFORMARMV8) - cbFdt - cbAcpi);
/** @todo r=bird: Leaks hVfsIosDesc and more */
    AssertRCReturnStmt(vrc, RTFdtDestroy(hFdt), vrc);

    RTGCPHYS GCPhysMmio    = 0;
    RTGCPHYS cbMmioAbove4G = 0;
    pResMgr->queryMmioRegion(&GCPhysMmio, &cbMmioAbove4G);

    VBOXPLATFORMARMV8 ArmV8Platform; RT_ZERO(ArmV8Platform);
    AssertCompile(sizeof(ArmV8Platform) < _128K); /** @todo ArmV8Platform is mostly zeros... Core structure w/ zero fill? */
    ArmV8Platform.u32Magic            = VBOXPLATFORMARMV8_MAGIC;
    ArmV8Platform.u32Version          = VBOXPLATFORMARMV8_VERSION;
    ArmV8Platform.cbDesc              = sizeof(ArmV8Platform);
    ArmV8Platform.fFlags              = 0;
    ArmV8Platform.u64PhysAddrRamBase  = GCPhysRamBase;
    ArmV8Platform.cbRamBase           = cbRamBase;
    ArmV8Platform.i64OffFdt           = (int64_t)GCPhysPlatformDesc - VBOXPLATFORMARMV8_PHYS_ADDR;
    ArmV8Platform.cbFdt               = cbFdt;
    if (cbAcpi)
    {
        ArmV8Platform.i64OffAcpi      = (int64_t)(GCPhysPlatformDesc + cbFdt) - VBOXPLATFORMARMV8_PHYS_ADDR;
        ArmV8Platform.cbAcpi          = cbAcpi;
        ArmV8Platform.i64OffAcpiXsdp  = (int64_t)GCPhysXsdp - VBOXPLATFORMARMV8_PHYS_ADDR;
        ArmV8Platform.cbAcpiXsdp      = cbAcpiXsdp;
    }
    ArmV8Platform.i64OffUefiRom       = (int64_t)GCPhysFw - VBOXPLATFORMARMV8_PHYS_ADDR;
    ArmV8Platform.cbUefiRom           = _64M;
    ArmV8Platform.i64OffMmio          = GCPhysMmio ? (int64_t)GCPhysMmio - VBOXPLATFORMARMV8_PHYS_ADDR : 0;
    ArmV8Platform.cbMmio              = cbMmioAbove4G;
    ArmV8Platform.i64OffMmio32        = (int64_t)(_4G - _512M) - VBOXPLATFORMARMV8_PHYS_ADDR;
    ArmV8Platform.cbMmio32            = _512M - _2M; /* Just assign the whole MMIO hole (except for the platform descriptor region). */

    /* Add the VBox platform descriptor to the resource store. */
    vrc = RTVfsIoStrmWrite(hVfsIosDesc, &ArmV8Platform, sizeof(ArmV8Platform), true /*fBlocking*/, NULL /*pcbWritten*/);
    RTVfsIoStrmRelease(hVfsIosDesc);
    AssertRCReturnStmt(vrc, RTVfsFileRelease(hVfsFileDesc), vrc);
#endif

    vrc = mptrResourceStore->i_addItem("resources", "VBoxArmV8Desc", hVfsFileDesc);
    RTVfsFileRelease(hVfsFileDesc);
    AssertRCReturn(vrc, vrc);

    /* Dump the DTB for debugging purposes if requested. */
    Bstr DtbDumpVal;
    hrc = mMachine->GetExtraData(Bstr("VBoxInternal2/DumpDtb").raw(),
                                 DtbDumpVal.asOutParam());
    if (   hrc == S_OK
        && DtbDumpVal.isNotEmpty())
    {
        vrc = RTFdtDumpToFile(hFdt, RTFDTTYPE_DTB, 0 /*fFlags*/, Utf8Str(DtbDumpVal).c_str(), NULL /*pErrInfo*/);
        AssertRCReturnStmt(vrc, RTFdtDestroy(hFdt), vrc);
    }

    pResMgr->dumpMemoryRegionsToReleaseLog();

    delete pResMgr; /* Delete the address/interrupt assignment manager. */

    /*
     * Apply the CFGM overlay.
     */
    if (RT_SUCCESS(vrc))
        vrc = i_configCfgmOverlay(pRoot, virtualBox, pMachine);

    /*
     * Dump all extradata API settings tweaks, both global and per VM.
     */
    if (RT_SUCCESS(vrc))
        vrc = i_configDumpAPISettingsTweaks(virtualBox, pMachine);

#undef H

    pAlock->release(); /* Avoid triggering the lock order inversion check. */

    /*
     * Register VM state change handler.
     */
    int vrc2 = pVMM->pfnVMR3AtStateRegister(pUVM, Console::i_vmstateChangeCallback, this);
    AssertRC(vrc2);
    if (RT_SUCCESS(vrc))
        vrc = vrc2;

    /*
     * Register VM runtime error handler.
     */
    vrc2 = pVMM->pfnVMR3AtRuntimeErrorRegister(pUVM, Console::i_atVMRuntimeErrorCallback, this);
    AssertRC(vrc2);
    if (RT_SUCCESS(vrc))
        vrc = vrc2;

    pAlock->acquire();

    LogFlowFunc(("vrc = %Rrc\n", vrc));
    LogFlowFuncLeave();

    return vrc;
}
#endif /* !VBOX_WITH_VIRT_ARMV8 */

