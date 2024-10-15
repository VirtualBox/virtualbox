/* $Id: ConsoleImplConfigArmV8.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Console COM Class implementation - VM Configuration Bits for ARMv8.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#include <iprt/formats/arm-psci.h>

#include <VBox/vmm/vmmr3vtable.h>
#include <VBox/vmm/vmapi.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/version.h>
#include <VBox/platforms/vbox-armv8.h>

#include "BusAssignmentManager.h"
#include "ResourceAssignmentManager.h"
#ifdef VBOX_WITH_EXTPACK
# include "ExtPackManagerImpl.h"
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/* Darwin compile kludge */
#undef PVM

#ifdef VBOX_WITH_VIRT_ARMV8
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
int Console::i_configConstructorArmV8(PUVM pUVM, PVM pVM, PCVMMR3VTABLE pVMM, AutoWriteLock *pAlock)
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

    /* Note: Should be guarded by VBOX_WITH_VIRT_ARMV8, but we check this anyway here. */
#if 1 /* For now we only support running ARM VMs on ARM hosts. */
    PlatformArchitecture_T platformArchMachine;
    hrc = platform->COMGETTER(Architecture)(&platformArchMachine);                          H();
    if (platformArchMachine != platformArchHost)
        return pVMM->pfnVMR3SetError(pUVM, VERR_PLATFORM_ARCH_NOT_SUPPORTED, RT_SRC_POS,
                                     N_("VM platform architecture (%s) not supported on this host (%s)."),
                                     Global::stringifyPlatformArchitecture(platformArchMachine),
                                     Global::stringifyPlatformArchitecture(platformArchHost));
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
                                     N_("The ARM backend doesn't support any other execution engine than 'default' or 'native-api' right now."));
    }

    LogRel(("Guest architecture: ARM\n"));

    Bstr osTypeId;
    hrc = pMachine->COMGETTER(OSTypeId)(osTypeId.asOutParam());                             H();
    LogRel(("Guest OS type: '%s'\n", Utf8Str(osTypeId).c_str()));

    BusAssignmentManager *pBusMgr = mBusMgr = BusAssignmentManager::createInstance(pVMM, chipsetType, IommuType_None);
    ResourceAssignmentManager *pResMgr = ResourceAssignmentManager::createInstance(pVMM, chipsetType, IommuType_None,
                                                                                   RT_MAX(_1G + cbRam, _4G),                  /*GCPhysMmio*/
                                                                                   _1G,                                       /*GCPhysRam*/
                                                                                   VBOXPLATFORMARMV8_PHYS_ADDR + _1M,         /*GCPhysMmio32Start*/
                                                                                   _1G - (VBOXPLATFORMARMV8_PHYS_ADDR + _1M), /*cbMmio32*/
                                                                                   32                                         /*cInterrupts*/);

    /*
     * Get root node first.
     * This is the only node in the tree.
     */
    PCFGMNODE pRoot = pVMM->pfnCFGMR3GetRootU(pUVM);
    Assert(pRoot);

    RTGCPHYS GCPhysRam = NIL_RTGCPHYS;

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
        uint32_t idPHandleIntCtrlMsi = RTFdtPHandleAllocate(hFdt);
        Assert(idPHandleIntCtrlMsi != UINT32_MAX); RT_NOREF(idPHandleIntCtrlMsi);
        uint32_t idPHandleAbpPClk = RTFdtPHandleAllocate(hFdt);
        Assert(idPHandleAbpPClk != UINT32_MAX);
        uint32_t idPHandleGpio = RTFdtPHandleAllocate(hFdt);
        Assert(idPHandleGpio != UINT32_MAX);

        uint32_t aidPHandleCpus[VMM_MAX_CPU_COUNT];
        for (uint32_t i = 0; i < cCpus; i++)
        {
            aidPHandleCpus[i] = RTFdtPHandleAllocate(hFdt);
            Assert(aidPHandleCpus[i] != UINT32_MAX);
        }

        vrc = RTFdtNodePropertyAddU32(   hFdt, "interrupt-parent", idPHandleIntCtrl);       VRC();
        vrc = RTFdtNodePropertyAddString(hFdt, "model",            "linux,dummy-virt");     VRC();
        vrc = RTFdtNodePropertyAddU32(   hFdt, "#size-cells",      2);                      VRC();
        vrc = RTFdtNodePropertyAddU32(   hFdt, "#address-cells",   2);                      VRC();
        vrc = RTFdtNodePropertyAddString(hFdt, "compatible",       "linux,dummy-virt");     VRC();

        /* Configure the Power State Coordination Interface. */
        vrc = RTFdtNodeAdd(hFdt, "psci");                                                   VRC();
        vrc = RTFdtNodePropertyAddU32(   hFdt, "migrate",          ARM_PSCI_FUNC_ID_CREATE_FAST_32(ARM_PSCI_FUNC_ID_MIGRATE));             VRC();
        vrc = RTFdtNodePropertyAddU32(   hFdt, "cpu_on",           ARM_PSCI_FUNC_ID_CREATE_FAST_32(ARM_PSCI_FUNC_ID_CPU_ON));              VRC();
        vrc = RTFdtNodePropertyAddU32(   hFdt, "cpu_off",          ARM_PSCI_FUNC_ID_CREATE_FAST_32(ARM_PSCI_FUNC_ID_CPU_OFF));             VRC();
        vrc = RTFdtNodePropertyAddU32(   hFdt, "cpu_suspend",      ARM_PSCI_FUNC_ID_CREATE_FAST_32(ARM_PSCI_FUNC_ID_CPU_SUSPEND));         VRC();
        vrc = RTFdtNodePropertyAddString(hFdt, "method",           "hvc");                  VRC();
        vrc = RTFdtNodePropertyAddStringList(hFdt, "compatible",   3,
                                             "arm,psci-1.0", "arm,psci-0.2", "arm,psci");   VRC();
        vrc = RTFdtNodeFinalize(hFdt);                                                      VRC();

        /* Configure the timer and clock. */
        InsertConfigInteger(pNEM, "VTimerInterrupt", 0xb);
        vrc = RTFdtNodeAdd(hFdt, "timer");                                                  VRC();
        vrc = RTFdtNodePropertyAddCellsU32(hFdt, "interrupts", 12,
                                           0x01, 0x0d, 0x104,
                                           0x01, 0x0e, 0x104,
                                           0x01, 0x0b, 0x104,
                                           0x01, 0x0a, 0x104);                              VRC();
        vrc = RTFdtNodePropertyAddEmpty(   hFdt, "always-on");                              VRC();
        vrc = RTFdtNodePropertyAddString(  hFdt, "compatible",       "arm,armv8-timer");    VRC();
        vrc = RTFdtNodeFinalize(hFdt);

        vrc = RTFdtNodeAdd(hFdt, "apb-clk");                                                VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "phandle", idPHandleAbpPClk);              VRC();
        vrc = RTFdtNodePropertyAddString(  hFdt, "clock-output-names", "clk24mhz");         VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "clock-frequency",    24 * 1000 * 1000);   VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "#clock-cells",       0);                  VRC();
        vrc = RTFdtNodePropertyAddString(  hFdt, "compatible",         "fixed-clock");      VRC();
        vrc = RTFdtNodeFinalize(hFdt);

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

        hrc = pResMgr->assignRamRegion("Conventional", cbRam, &GCPhysRam);                  H();

        PCFGMNODE pMemRegion = NULL;
        InsertConfigNode(pMem, "Conventional", &pMemRegion);
        InsertConfigInteger(pMemRegion, "GCPhysStart", GCPhysRam);
        InsertConfigInteger(pMemRegion, "Size", cbRam);

        vrc = RTFdtNodeAddF(hFdt, "memory@%RGp", GCPhysRam);                                VRC();
        vrc = RTFdtNodePropertyAddCellsU64(hFdt, "reg", 2, GCPhysRam, cbRam);               VRC();
        vrc = RTFdtNodePropertyAddString(  hFdt, "device_type",      "memory");             VRC();
        vrc = RTFdtNodeFinalize(hFdt);                                                      VRC();

        /* Configure the CPUs in the system, only one socket and cluster at the moment. */
        vrc = RTFdtNodeAdd(hFdt, "cpus");                                                   VRC();
        vrc = RTFdtNodePropertyAddU32(hFdt, "#size-cells",    0);                           VRC();
        vrc = RTFdtNodePropertyAddU32(hFdt, "#address-cells", 1);                           VRC();

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
            vrc = RTFdtNodePropertyAddString(hFdt, "compatible",  "arm,cortex-a15");        VRC();
            vrc = RTFdtNodePropertyAddString(hFdt, "device_type", "cpu");                   VRC();
            if (cCpus > 1)
            {
                vrc = RTFdtNodePropertyAddString(hFdt, "enable-method",  "psci");           VRC();
            }
            vrc = RTFdtNodeFinalize(hFdt);                                                  VRC();
        }

        vrc = RTFdtNodeFinalize(hFdt);                                                      VRC();


        /*
         * CPUM values.
         */
        PCFGMNODE pCpum;
        InsertConfigNode(pRoot, "CPUM", &pCpum);


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

        InsertConfigNode(pDevices, "pci-generic-ecam-bridge", NULL);

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
        InsertConfigInteger(pRes,       "GCPhysLoadAddress",     0);

        /** @todo r=aeichner 32-bit guests and query the firmware type from VBoxSVC. */
        /*
         * Firmware.
         */
        FirmwareType_T eFwType =  FirmwareType_EFI64;
#ifdef VBOX_WITH_EFI_IN_DD2
        const char *pszEfiRomFile = eFwType == FirmwareType_EFIDUAL ? "<INVALID>"
                                  : eFwType == FirmwareType_EFI32   ? "VBoxEFIAArch32.fd"
                                  :                                   "VBoxEFIAArch64.fd";
        const char *pszKey = "ResourceId";
#else
        Utf8Str efiRomFile;
        vrc = findEfiRom(virtualBox, PlatformArchitecture_ARM, eFwType, &efiRomFile);
        AssertRCReturn(vrc, vrc);
        const char *pszEfiRomFile = efiRomFile.c_str();
        const char *pszKey = "Filename";
#endif
        InsertConfigString(pRes,        pszKey,                  pszEfiRomFile);

        InsertConfigNode(pResources,    "ArmV8Desc",             &pRes);
        InsertConfigInteger(pRes,       "RegisterAsRom",         1);
        InsertConfigInteger(pRes,       "GCPhysLoadAddress",     VBOXPLATFORMARMV8_PHYS_ADDR);
        InsertConfigString(pRes,        "ResourceId",            "VBoxArmV8Desc");

        /*
         * Configure the interrupt controller.
         */
        RTGCPHYS GCPhysIntcDist;
        RTGCPHYS GCPhysIntcReDist;
        RTGCPHYS cbMmioIntcDist;
        RTGCPHYS cbMmioIntcReDist;

        /* Each vCPU needs on re-distributor, this would allow for up to 256 vCPUs in the future. */
        hrc = pResMgr->assignMmioRegion("gic", 256 * _64K, &GCPhysIntcReDist, &cbMmioIntcReDist);       H();
        hrc = pResMgr->assignMmioRegion("gic", _64K, &GCPhysIntcDist, &cbMmioIntcDist);                 H();

#ifndef RT_OS_LINUX
        InsertConfigNode(pDevices, "gic",                   &pDev);
#else
        /* On Linux we default to the KVM in-kernel GIC for now. */
        InsertConfigNode(pDevices, "gic-nem",               &pDev);
#endif
        InsertConfigNode(pDev,     "0",                     &pInst);
        InsertConfigInteger(pInst, "Trusted",               1);
        InsertConfigNode(pInst,    "Config",                &pCfg);
        InsertConfigInteger(pCfg,  "DistributorMmioBase",   GCPhysIntcDist);
        InsertConfigInteger(pCfg,  "RedistributorMmioBase", GCPhysIntcReDist);

        vrc = RTFdtNodeAddF(hFdt, "intc@%RGp", GCPhysIntcDist);                                         VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "phandle",          idPHandleIntCtrl);                 VRC();
        vrc = RTFdtNodePropertyAddCellsU64(hFdt, "reg", 4,
                                           GCPhysIntcDist, cbMmioIntcDist,      /* Distributor */
                                           GCPhysIntcReDist, cbMmioIntcReDist); /* Re-Distributor */    VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "#redistributor-regions", 1);                          VRC();
        vrc = RTFdtNodePropertyAddString(  hFdt, "compatible",       "arm,gic-v3");                     VRC();
        vrc = RTFdtNodePropertyAddEmpty(   hFdt, "ranges");                                             VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "#size-cells",      2);                                VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "#address-cells",   2);                                VRC();
        vrc = RTFdtNodePropertyAddEmpty(   hFdt, "interrupt-controller");                               VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "#interrupt-cells", 3);                                VRC();

#if 0
        vrc = RTFdtNodeAddF(hFdt, "its@%RX32", 0x08080000);                                 VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "phandle",          idPHandleIntCtrlMsi);  VRC();
        vrc = RTFdtNodePropertyAddCellsU32(hFdt, "reg", 4, 0, 0x08080000, 0, 0x20000);      VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "#msi-cells", 1);                          VRC();
        vrc = RTFdtNodePropertyAddEmpty(   hFdt, "msi-controller");                         VRC();
        vrc = RTFdtNodePropertyAddString(  hFdt, "compatible", "arm,gic-v3-its");           VRC();
        vrc = RTFdtNodeFinalize(hFdt);                                                      VRC();
#endif

        vrc = RTFdtNodeFinalize(hFdt);                                                      VRC();

        RTGCPHYS GCPhysMmioStart;
        RTGCPHYS cbMmio;
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
        }

        InsertConfigNode(pDevices, "flash-cfi",         &pDev);
        InsertConfigNode(pDev,     "0",            &pInst);
        InsertConfigNode(pInst,    "Config",        &pCfg);
        InsertConfigInteger(pCfg,  "BaseAddress",  64 * _1M);
        InsertConfigInteger(pCfg,  "Size",        768 * _1K);
        InsertConfigString(pCfg,   "FlashFile",   "nvram");
        /* Attach the NVRAM storage driver. */
        InsertConfigNode(pInst,    "LUN#0",       &pLunL0);
        InsertConfigString(pLunL0, "Driver",      "NvramStore");

        vrc = RTFdtNodeAddF(hFdt, "flash@%RX32", 0);                                        VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "bank-width", 4);                          VRC();
        vrc = RTFdtNodePropertyAddCellsU64(hFdt, "reg", 4,
                                           0,          0x04000000,  /* First region (EFI). */
                                           0x04000000, 0x04000000); /* Second region (NVRAM). */ VRC();
        vrc = RTFdtNodePropertyAddString(  hFdt, "compatible", "cfi-flash");                VRC();
        vrc = RTFdtNodeFinalize(hFdt);                                                      VRC();

        InsertConfigNode(pDevices, "arm-pl011",     &pDev);
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
            hrc = pResMgr->assignSingleInterrupt("arm-pl011", &iIrq);                               H();
            hrc = pResMgr->assignMmioRegion("arm-pl011", _4K, &GCPhysMmioStart, &cbMmio);           H();

            InsertConfigInteger(pCfg,  "Irq",      iIrq);
            InsertConfigInteger(pCfg,  "MmioBase", GCPhysMmioStart);

            vrc = RTFdtNodeAddF(hFdt, "pl011@%RGp", GCPhysMmioStart);                               VRC();
            vrc = RTFdtNodePropertyAddStringList(hFdt, "clock-names", 2, "uartclk", "apb_pclk");    VRC();
            vrc = RTFdtNodePropertyAddCellsU32(hFdt, "clocks", 2,
                                               idPHandleAbpPClk, idPHandleAbpPClk);                 VRC();
            vrc = RTFdtNodePropertyAddCellsU32(hFdt, "interrupts", 3, 0x00, iIrq, 0x04);            VRC();
            vrc = RTFdtNodePropertyAddCellsU64(hFdt, "reg", 2, GCPhysMmioStart, cbMmio);            VRC();
            vrc = RTFdtNodePropertyAddStringList(hFdt, "compatible", 2,
                                                 "arm,pl011", "arm,primecell");                     VRC();
            vrc = RTFdtNodeFinalize(hFdt);                                                          VRC();

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
        hrc = pResMgr->assignSingleInterrupt("arm-pl031-rtc", &iIrq);                       H();
        hrc = pResMgr->assignMmioRegion("arm-pl031-rtc", _4K, &GCPhysMmioStart, &cbMmio);   H();
        InsertConfigNode(pDevices, "arm-pl031-rtc", &pDev);
        InsertConfigNode(pDev,     "0",            &pInst);
        InsertConfigNode(pInst,    "Config",        &pCfg);
        InsertConfigInteger(pCfg,  "Irq",            iIrq);
        InsertConfigInteger(pCfg,  "MmioBase", GCPhysMmioStart);
        InsertConfigInteger(pCfg,  "UtcOffset", fRTCUseUTC ? 1 : 0);

        vrc = RTFdtNodeAddF(hFdt, "pl032@%RGp", GCPhysMmioStart);                           VRC();
        vrc = RTFdtNodePropertyAddString(  hFdt, "clock-names", "apb_pclk");                VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "clocks", idPHandleAbpPClk);               VRC();
        vrc = RTFdtNodePropertyAddCellsU32(hFdt, "interrupts", 3, 0x00, iIrq, 0x04);        VRC();
        vrc = RTFdtNodePropertyAddCellsU64(hFdt, "reg", 2, GCPhysMmioStart, cbMmio);        VRC();
        vrc = RTFdtNodePropertyAddStringList(hFdt, "compatible", 2,
                                             "arm,pl031", "arm,primecell");                 VRC();
        vrc = RTFdtNodeFinalize(hFdt);                                                      VRC();

        /* Configure gpio keys. */
        hrc = pResMgr->assignSingleInterrupt("arm-pl061-gpio", &iIrq);                      H();
        hrc = pResMgr->assignMmioRegion("arm-pl061-gpio", _4K, &GCPhysMmioStart, &cbMmio);  H();
        InsertConfigNode(pDevices, "arm-pl061-gpio",&pDev);
        InsertConfigNode(pDev,     "0",            &pInst);
        InsertConfigNode(pInst,    "Config",        &pCfg);
        InsertConfigInteger(pCfg,  "Irq",            iIrq);
        InsertConfigInteger(pCfg,  "MmioBase", GCPhysMmioStart);
        vrc = RTFdtNodeAddF(hFdt, "pl061@%RGp", GCPhysMmioStart);                           VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "phandle", idPHandleGpio);                 VRC();
        vrc = RTFdtNodePropertyAddString(  hFdt, "clock-names", "apb_pclk");                VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "clocks", idPHandleAbpPClk);               VRC();
        vrc = RTFdtNodePropertyAddCellsU32(hFdt, "interrupts", 3, 0x00, iIrq, 0x04);        VRC();
        vrc = RTFdtNodePropertyAddEmpty(   hFdt, "gpio-controller");                        VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "#gpio-cells", 2);                         VRC();
        vrc = RTFdtNodePropertyAddStringList(hFdt, "compatible", 2,
                                             "arm,pl061", "arm,primecell");                 VRC();
        vrc = RTFdtNodePropertyAddCellsU64(hFdt, "reg", 2, GCPhysMmioStart, cbMmio);        VRC();
        vrc = RTFdtNodeFinalize(hFdt);                                                      VRC();

        InsertConfigNode(pInst,    "LUN#0",           &pLunL0);
        InsertConfigString(pLunL0, "Driver",          "GpioButton");
        InsertConfigNode(pLunL0,   "Config",          &pCfg);
        InsertConfigInteger(pCfg,  "PowerButtonGpio", 3);
        InsertConfigInteger(pCfg,  "SleepButtonGpio", 4);

        vrc = RTFdtNodeAdd(hFdt, "gpio-keys");                                              VRC();
        vrc = RTFdtNodePropertyAddString(hFdt, "compatible",           "gpio-keys");        VRC();

        vrc = RTFdtNodeAdd(hFdt, "poweroff");                                               VRC();
        vrc = RTFdtNodePropertyAddCellsU32(hFdt, "gpios", 3, idPHandleGpio, 3, 0);          VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "linux,code", 0x74);                       VRC();
        vrc = RTFdtNodePropertyAddString(  hFdt, "label",      "GPIO Key Poweroff");        VRC();
        vrc = RTFdtNodeFinalize(hFdt);                                                      VRC();

        vrc = RTFdtNodeAdd(hFdt, "suspend");                                                VRC();
        vrc = RTFdtNodePropertyAddCellsU32(hFdt, "gpios", 3, idPHandleGpio, 4, 0);          VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "linux,code", 0xcd);                       VRC();
        vrc = RTFdtNodePropertyAddString(  hFdt, "label",      "GPIO Key Suspend");         VRC();
        vrc = RTFdtNodeFinalize(hFdt);

        vrc = RTFdtNodeFinalize(hFdt);                                                      VRC();

        hrc = pResMgr->assignInterrupts("pci-generic-ecam", 4 /*cInterrupts*/, &iIrq);      H();
        uint32_t aPinIrqs[] = { iIrq, iIrq + 1, iIrq + 2, iIrq + 3 };
        RTGCPHYS GCPhysPciMmioEcam, GCPhysPciMmio, GCPhysPciMmio32;
        RTGCPHYS cbPciMmioEcam, cbPciMmio, cbPciMmio32;
        hrc = pResMgr->assignMmioRegionAligned("pci-pio",    _64K, _64K,                              &GCPhysMmioStart,   &cbMmio);         H();
        hrc = pResMgr->assignMmioRegion(       "pci-ecam",   16 * _1M,                                &GCPhysPciMmioEcam, &cbPciMmioEcam);  H();
        hrc = pResMgr->assignMmioRegion(       "pci-mmio",   _2G,                                     &GCPhysPciMmio,     &cbPciMmio);      H();
        hrc = pResMgr->assignMmio32Region(     "pci-mmio32", _1G - VBOXPLATFORMARMV8_PHYS_ADDR - _1M, &GCPhysPciMmio32,   &cbPciMmio32);    H();

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

        uint32_t aIrqCells[32 * 4 * 10]; RT_ZERO(aIrqCells); /* Maximum of 32 devices on the root bus, each supporting 4 interrupts (INTA# ... INTD#). */
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
                pau32IrqCell[5] = 0;
                pau32IrqCell[6] = 0;
                pau32IrqCell[7] = 0;
                pau32IrqCell[8] = aPinIrqs[(iIrqPinSwizzle + iIrqPin) % RT_ELEMENTS(aPinIrqs)];
                pau32IrqCell[9] = 0x04;
                pau32IrqCell += 10;
            }

            iIrqPinSwizzle++;
        }

        vrc = RTFdtNodePropertyAddCellsU32AsArray(hFdt, "interrupt-map", RT_ELEMENTS(aIrqCells), &aIrqCells[0]);
        vrc = RTFdtNodePropertyAddU32(     hFdt, "#interrupt-cells", 1);                        VRC();
        vrc = RTFdtNodePropertyAddCellsU32(hFdt, "ranges", 21,
                                           0x1000000, 0, 0,
                                           GCPhysMmioStart >> 32, GCPhysMmioStart, cbMmio >> 32, cbMmio,
                                           0x2000000, GCPhysPciMmio32 >> 32, GCPhysPciMmio32, GCPhysPciMmio32 >> 32, GCPhysPciMmio32,
                                           cbPciMmio32 >> 32, cbPciMmio32,
                                           0x3000000, GCPhysPciMmio >> 32, GCPhysPciMmio, GCPhysPciMmio >> 32, GCPhysPciMmio,
                                           cbPciMmio >> 32, cbPciMmio);                         VRC();
        vrc = RTFdtNodePropertyAddCellsU64(hFdt, "reg", 2, GCPhysPciMmioEcam, cbPciMmioEcam);   VRC();
        /** @todo msi-map */
        vrc = RTFdtNodePropertyAddEmpty(   hFdt, "dma-coherent");                               VRC();
        vrc = RTFdtNodePropertyAddCellsU32(hFdt, "bus-range", 2, 0, 0xf);                       VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "linux,pci-domain", 0);                        VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "#size-cells", 2);                             VRC();
        vrc = RTFdtNodePropertyAddU32(     hFdt, "#address-cells", 3);                          VRC();
        vrc = RTFdtNodePropertyAddString(  hFdt, "device_type", "pci");                         VRC();
        vrc = RTFdtNodePropertyAddString(  hFdt, "compatible", "pci-host-ecam-generic");        VRC();
        vrc = RTFdtNodeFinalize(hFdt);                                                          VRC();

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
                                   pVMM, pUVM, pDevices, llBootNics);                            VRC();

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
    }
#endif

#if 0
    vrc = RTFdtNodeAdd(hFdt, "chosen");                                                 VRC();
    vrc = RTFdtNodePropertyAddString(  hFdt, "stdout-path", "pl011@9000000");           VRC();
    vrc = RTFdtNodePropertyAddString(  hFdt, "stdin-path", "pl011@9000000");            VRC();
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
    VBOXPLATFORMARMV8 ArmV8Platform; RT_ZERO(ArmV8Platform);

    /* Make room for the descriptor at the beginning. */
    vrc = RTVfsIoStrmZeroFill(hVfsIosDesc, sizeof(ArmV8Platform));
    AssertRCReturnStmt(vrc, RTFdtDestroy(hFdt), vrc);

    vrc = RTFdtDumpToVfsIoStrm(hFdt, RTFDTTYPE_DTB, 0 /*fFlags*/, hVfsIosDesc, NULL /*pErrInfo*/);
    uint64_t cbFdt = 0;
    if (RT_SUCCESS(vrc))
    {
        vrc = RTVfsFileQuerySize(hVfsFileDesc, &cbFdt);
        cbFdt -= sizeof(ArmV8Platform);
    }
    AssertRCReturnStmt(vrc, RTFdtDestroy(hFdt), vrc);

    vrc = RTVfsIoStrmZeroFill(hVfsIosDesc, (RTFOFF)(RT_ALIGN_64(cbFdt, _64K) - cbFdt));
    AssertRCReturn(vrc, vrc);

    RTGCPHYS GCPhysMmioStart;
    RTGCPHYS cbMmio;
    hrc = pResMgr->queryMmioRegion(&GCPhysMmioStart, &cbMmio);
    Assert(SUCCEEDED(hrc));

    RTGCPHYS GCPhysMmio32Start;
    RTGCPHYS cbMmio32;
    hrc = pResMgr->queryMmio32Region(&GCPhysMmio32Start, &cbMmio32);
    Assert(SUCCEEDED(hrc));

    ArmV8Platform.u32Magic            = VBOXPLATFORMARMV8_MAGIC;
    ArmV8Platform.u32Version          = VBOXPLATFORMARMV8_VERSION;
    ArmV8Platform.cbDesc              = sizeof(ArmV8Platform);
    ArmV8Platform.fFlags              = 0;
    ArmV8Platform.u64PhysAddrRamBase  = GCPhysRam;
    ArmV8Platform.cbRamBase           = cbRam;
    ArmV8Platform.i64OffFdt           = sizeof(ArmV8Platform);
    ArmV8Platform.cbFdt               = RT_ALIGN_64(cbFdt, _64K);
    ArmV8Platform.i64OffAcpiXsdp      = 0;
    ArmV8Platform.cbAcpiXsdp          = 0;
    ArmV8Platform.i64OffUefiRom       = -128 * _1M;
    ArmV8Platform.cbUefiRom           = _64M;
    ArmV8Platform.i64OffMmio          = GCPhysMmioStart - _128M;
    ArmV8Platform.cbMmio              = cbMmio;
    ArmV8Platform.i64OffMmio32        = GCPhysMmio32Start - _128M;
    ArmV8Platform.cbMmio32            = cbMmio32;

    /* Add the VBox platform descriptor to the resource store. */
    vrc = RTVfsIoStrmWriteAt(hVfsIosDesc, 0, &ArmV8Platform, sizeof(ArmV8Platform), true /*fBlocking*/, NULL /*pcbWritten*/);
    RTVfsIoStrmRelease(hVfsIosDesc);
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

