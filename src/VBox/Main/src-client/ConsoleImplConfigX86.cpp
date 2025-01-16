/* $Id: ConsoleImplConfigX86.cpp 107891 2025-01-16 00:33:11Z knut.osmundsen@oracle.com $ */
/** @file
 * VBox Console COM Class implementation - VM Configuration Bits.
 *
 * @remark  We've split out the code that the 64-bit VC++ v8 compiler finds
 *          problematic to optimize so we can disable optimizations and later,
 *          perhaps, find a real solution for it (like rewriting the code and
 *          to stop resemble a tonne of spaghetti).
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
#define LOG_GROUP LOG_GROUP_MAIN_CONSOLE
#include "LoggingNew.h"

#include "ConsoleImpl.h"
#include "DisplayImpl.h"
#include "NvramStoreImpl.h"
#include "PlatformImpl.h"
#include "VMMDev.h"
#include "Global.h"
#ifdef VBOX_WITH_PCI_PASSTHROUGH
# include "PCIRawDevImpl.h"
#endif

// generated header
#include "SchemaDefs.h"

#include "AutoCaller.h"

#include <iprt/base64.h>
#include <iprt/buildconfig.h>
#include <iprt/ctype.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/string.h>
#include <iprt/system.h>
#if 0 /* enable to play with lots of memory. */
# include <iprt/env.h>
#endif
#include <iprt/stream.h>

#include <iprt/http.h>
#include <iprt/socket.h>
#include <iprt/uri.h>

#include <VBox/vmm/vmmr3vtable.h>
#include <VBox/vmm/vmapi.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/settings.h> /* For MachineConfigFile::getHostDefaultAudioDriver(). */
#include <VBox/vmm/pdmapi.h> /* For PDMR3DriverAttach/PDMR3DriverDetach. */
#include <VBox/vmm/pdmusb.h> /* For PDMR3UsbCreateEmulatedDevice. */
#include <VBox/vmm/pdmdev.h> /* For PDMAPICMODE enum. */
#include <VBox/vmm/pdmstorageifs.h>
#include <VBox/version.h>

#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/array.h>

#include "NetworkServiceRunner.h"
#include "BusAssignmentManager.h"
#ifdef VBOX_WITH_EXTPACK
# include "ExtPackManagerImpl.h"
#endif

/** The TPM PPI MMIO base default (compatible with qemu). */
#define TPM_PPI_MMIO_BASE_DEFAULT UINT64_C(0xfed45000)


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/* Darwin compile kludge */
#undef PVM

/**
 * @throws HRESULT on extra data retrival error.
 */
static int getSmcDeviceKey(IVirtualBox *pVirtualBox, IMachine *pMachine, Utf8Str *pStrKey, bool *pfGetKeyFromRealSMC)
{
    *pfGetKeyFromRealSMC = false;

    /*
     * The extra data takes precedence (if non-zero).
     */
    GetExtraDataBoth(pVirtualBox, pMachine, "VBoxInternal2/SmcDeviceKey", pStrKey);
    if (pStrKey->isNotEmpty())
        return VINF_SUCCESS;

#ifdef RT_OS_DARWIN

    /*
     * Work done in EFI/DevSmc
     */
    *pfGetKeyFromRealSMC = true;
    int vrc = VINF_SUCCESS;

#else
    /*
     * Is it apple hardware in bootcamp?
     */
    /** @todo implement + test RTSYSDMISTR_MANUFACTURER on all hosts.
     *        Currently falling back on the product name. */
    char szManufacturer[256];
    szManufacturer[0] = '\0';
    RTSystemQueryDmiString(RTSYSDMISTR_MANUFACTURER, szManufacturer, sizeof(szManufacturer));
    if (szManufacturer[0] != '\0')
    {
        if (   !strcmp(szManufacturer, "Apple Computer, Inc.")
            || !strcmp(szManufacturer, "Apple Inc.")
            )
            *pfGetKeyFromRealSMC = true;
    }
    else
    {
        char szProdName[256];
        szProdName[0] = '\0';
        RTSystemQueryDmiString(RTSYSDMISTR_PRODUCT_NAME, szProdName, sizeof(szProdName));
        if (   (   !strncmp(szProdName, RT_STR_TUPLE("Mac"))
                || !strncmp(szProdName, RT_STR_TUPLE("iMac"))
                || !strncmp(szProdName, RT_STR_TUPLE("Xserve"))
               )
            && !strchr(szProdName, ' ')                             /* no spaces */
            && RT_C_IS_DIGIT(szProdName[strlen(szProdName) - 1])    /* version number */
           )
            *pfGetKeyFromRealSMC = true;
    }

    int vrc = VINF_SUCCESS;
#endif

    return vrc;
}


/*
 * VC++ 8 / amd64 has some serious trouble with the next functions.
 * As a temporary measure, we'll drop global optimizations.
 */
#if defined(_MSC_VER) && defined(RT_ARCH_AMD64)
# if _MSC_VER >= RT_MSC_VER_VC80 && _MSC_VER < RT_MSC_VER_VC100
#  pragma optimize("g", off)
# endif
#endif

/** Helper that finds out the next HBA port used
 */
static LONG GetNextUsedPort(LONG aPortUsed[30], LONG lBaseVal, uint32_t u32Size)
{
    LONG lNextPortUsed = 30;
    for (size_t j = 0; j < u32Size; ++j)
    {
        if (   aPortUsed[j] >  lBaseVal
            && aPortUsed[j] <= lNextPortUsed)
           lNextPortUsed = aPortUsed[j];
    }
    return lNextPortUsed;
}

#define MAX_BIOS_LUN_COUNT   4

int Console::SetBiosDiskInfo(ComPtr<IMachine> pMachine, PCFGMNODE pCfg, PCFGMNODE pBiosCfg,
                             Bstr controllerName, const char * const s_apszBiosConfig[4])
{
    RT_NOREF(pCfg);
    HRESULT             hrc;
#define MAX_DEVICES     30
#define H()     AssertLogRelMsgReturn(!FAILED(hrc), ("hrc=%Rhrc\n", hrc), VERR_MAIN_CONFIG_CONSTRUCTOR_COM_ERROR)
#define VRC()   AssertLogRelMsgReturn(RT_SUCCESS(vrc), ("vrc=%Rrc\n", vrc), vrc)

    LONG lPortLUN[MAX_BIOS_LUN_COUNT];
    LONG lPortUsed[MAX_DEVICES];
    uint32_t u32HDCount = 0;

    /* init to max value */
    lPortLUN[0] = MAX_DEVICES;

    com::SafeIfaceArray<IMediumAttachment> atts;
    hrc = pMachine->GetMediumAttachmentsOfController(controllerName.raw(),
                                        ComSafeArrayAsOutParam(atts));  H();
    size_t uNumAttachments = atts.size();
    if (uNumAttachments > MAX_DEVICES)
    {
        LogRel(("Number of Attachments > Max=%d.\n", uNumAttachments));
        uNumAttachments = MAX_DEVICES;
    }

    /* Find the relevant ports/IDs, i.e the ones to which a HD is attached. */
    for (size_t j = 0; j < uNumAttachments; ++j)
    {
        IMediumAttachment *pMediumAtt = atts[j];
        LONG lPortNum = 0;
        hrc = pMediumAtt->COMGETTER(Port)(&lPortNum);                   H();
        if (SUCCEEDED(hrc))
        {
            DeviceType_T lType;
            hrc = pMediumAtt->COMGETTER(Type)(&lType);                  H();
            if (SUCCEEDED(hrc) && lType == DeviceType_HardDisk)
            {
                /* find min port number used for HD */
                if (lPortNum < lPortLUN[0])
                    lPortLUN[0] = lPortNum;
                lPortUsed[u32HDCount++] = lPortNum;
                LogFlowFunc(("HD port Count=%d\n", u32HDCount));
            }
        }
    }


    /* Pick only the top 4 used HD Ports as CMOS doesn't have space
     * to save details for all 30 ports
     */
    uint32_t u32MaxPortCount = MAX_BIOS_LUN_COUNT;
    if (u32HDCount < MAX_BIOS_LUN_COUNT)
        u32MaxPortCount = u32HDCount;
    for (size_t j = 1; j < u32MaxPortCount; j++)
        lPortLUN[j] = GetNextUsedPort(lPortUsed, lPortLUN[j-1], u32HDCount);
    if (pBiosCfg)
    {
        for (size_t j = 0; j < u32MaxPortCount; j++)
        {
            InsertConfigInteger(pBiosCfg, s_apszBiosConfig[j], lPortLUN[j]);
            LogFlowFunc(("Top %d HBA ports = %s, %d\n", j, s_apszBiosConfig[j], lPortLUN[j]));
        }
    }
    return VINF_SUCCESS;
}

#ifdef VBOX_WITH_PCI_PASSTHROUGH
HRESULT Console::i_attachRawPCIDevices(PUVM pUVM, BusAssignmentManager *pBusMgr, PCFGMNODE pDevices)
{
# ifndef VBOX_WITH_EXTPACK
    RT_NOREF(pUVM);
# endif
    HRESULT hrc = S_OK;
    PCFGMNODE pInst, pCfg, pLunL0, pLunL1;

    SafeIfaceArray<IPCIDeviceAttachment> assignments;
    ComPtr<IMachine> aMachine = i_machine();

    hrc = aMachine->COMGETTER(PCIDeviceAssignments)(ComSafeArrayAsOutParam(assignments));
    if (   hrc != S_OK
        || assignments.size() < 1)
        return hrc;

    /*
     * PCI passthrough is only available if the proper ExtPack is installed.
     *
     * Note. Configuring PCI passthrough here and providing messages about
     * the missing extpack isn't exactly clean, but it is a necessary evil
     * to patch over legacy compatability issues introduced by the new
     * distribution model.
     */
# ifdef VBOX_WITH_EXTPACK
    static const char *s_pszPCIRawExtPackName = VBOX_PUEL_PRODUCT;
    if (   !mptrExtPackManager->i_isExtPackUsable(s_pszPCIRawExtPackName)
        && !mptrExtPackManager->i_isExtPackUsable("Oracle VM VirtualBox Extension Pack")) /* Legacy name -- see @bugref{10690}. */
        /* Always fatal! */
        return pVMM->pfnVMR3SetError(pUVM, VERR_NOT_FOUND, RT_SRC_POS,
                                     N_("Implementation of the PCI passthrough framework not found!\n"
                                        "The VM cannot be started. To fix this problem, either "
                                        "install the '%s' or disable PCI passthrough via VBoxManage"),
                                     s_pszPCIRawExtPackName);
# endif

    /* Now actually add devices */
    PCFGMNODE pPCIDevs = NULL;

    if (assignments.size() > 0)
    {
        InsertConfigNode(pDevices, "pciraw",  &pPCIDevs);

        PCFGMNODE pRoot = CFGMR3GetParent(pDevices); Assert(pRoot);

        /* Tell PGM to tell GPCIRaw about guest mappings. */
        CFGMR3InsertNode(pRoot, "PGM", NULL);
        InsertConfigInteger(CFGMR3GetChild(pRoot, "PGM"), "PciPassThrough", 1);

        /*
         * Currently, using IOMMU needed for PCI passthrough
         * requires RAM preallocation.
         */
        /** @todo check if we can lift this requirement */
        CFGMR3RemoveValue(pRoot, "RamPreAlloc");
        InsertConfigInteger(pRoot, "RamPreAlloc", 1);
    }

    for (size_t iDev = 0; iDev < assignments.size(); iDev++)
    {
        ComPtr<IPCIDeviceAttachment> const assignment = assignments[iDev];

        LONG host;
        hrc = assignment->COMGETTER(HostAddress)(&host);            H();
        LONG guest;
        hrc = assignment->COMGETTER(GuestAddress)(&guest);          H();
        Bstr bstrDevName;
        hrc = assignment->COMGETTER(Name)(bstrDevName.asOutParam());   H();

        InsertConfigNodeF(pPCIDevs, &pInst, "%d", iDev);
        InsertConfigInteger(pInst, "Trusted", 1);

        PCIBusAddress HostPCIAddress(host);
        Assert(HostPCIAddress.valid());
        InsertConfigNode(pInst,        "Config",  &pCfg);
        InsertConfigString(pCfg,       "DeviceName",  bstrDevName);

        InsertConfigInteger(pCfg,      "DetachHostDriver",  1);
        InsertConfigInteger(pCfg,      "HostPCIBusNo",      HostPCIAddress.miBus);
        InsertConfigInteger(pCfg,      "HostPCIDeviceNo",   HostPCIAddress.miDevice);
        InsertConfigInteger(pCfg,      "HostPCIFunctionNo", HostPCIAddress.miFn);

        PCIBusAddress GuestPCIAddress(guest);
        Assert(GuestPCIAddress.valid());
        hrc = pBusMgr->assignHostPCIDevice("pciraw", pInst, HostPCIAddress, GuestPCIAddress, true);
        if (hrc != S_OK)
            return hrc;

        InsertConfigInteger(pCfg,      "GuestPCIBusNo",      GuestPCIAddress.miBus);
        InsertConfigInteger(pCfg,      "GuestPCIDeviceNo",   GuestPCIAddress.miDevice);
        InsertConfigInteger(pCfg,      "GuestPCIFunctionNo", GuestPCIAddress.miFn);

        /* the driver */
        InsertConfigNode(pInst,        "LUN#0",   &pLunL0);
        InsertConfigString(pLunL0,     "Driver", "pciraw");
        InsertConfigNode(pLunL0,       "AttachedDriver", &pLunL1);

        /* the Main driver */
        InsertConfigString(pLunL1,     "Driver", "MainPciRaw");
        InsertConfigNode(pLunL1,       "Config", &pCfg);
        PCIRawDev *pMainDev = new PCIRawDev(this);
# error This is not allowed any more
        InsertConfigInteger(pCfg,      "Object", (uintptr_t)pMainDev);
    }

    return hrc;
}
#endif


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
int Console::i_configConstructorX86(PUVM pUVM, PVM pVM, PCVMMR3VTABLE pVMM, AutoWriteLock *pAlock)
{
    RT_NOREF(pVM /* when everything is disabled */);
    ComPtr<IMachine> pMachine = i_machine();

    int             vrc;
    HRESULT         hrc;
    Utf8Str         strTmp;
    Bstr            bstr;

#define H()         AssertLogRelMsgReturn(!FAILED(hrc), ("hrc=%Rhrc\n", hrc), VERR_MAIN_CONFIG_CONSTRUCTOR_COM_ERROR)

    /*
     * Get necessary objects and frequently used parameters.
     */
    ComPtr<IVirtualBox> virtualBox;
    hrc = pMachine->COMGETTER(Parent)(virtualBox.asOutParam());                             H();

    ComPtr<IHost> host;
    hrc = virtualBox->COMGETTER(Host)(host.asOutParam());                                   H();

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
#if 0 /* enable to play with lots of memory. */
    if (RTEnvExist("VBOX_RAM_SIZE"))
        cRamMBs = RTStrToUInt64(RTEnvGet("VBOX_RAM_SIZE"));
#endif
    uint64_t const cbRam   = cRamMBs * (uint64_t)_1M;
    uint32_t cbRamHole     = MM_RAM_HOLE_SIZE_DEFAULT;
    uint64_t uMcfgBase     = 0;
    uint32_t cbMcfgLength  = 0;

    ParavirtProvider_T enmParavirtProvider;
    hrc = pMachine->GetEffectiveParavirtProvider(&enmParavirtProvider);                     H();

    Bstr strParavirtDebug;
    hrc = pMachine->COMGETTER(ParavirtDebug)(strParavirtDebug.asOutParam());                H();

    BOOL fIOAPIC;
    uint32_t uIoApicPciAddress = NIL_PCIBDF;
    hrc = firmwareSettings->COMGETTER(IOAPICEnabled)(&fIOAPIC);                             H();

    ComPtr<IPlatform> platform;
    pMachine->COMGETTER(Platform)(platform.asOutParam());                                   H();

    /* We have to increase the RAM hole if lots of VRAM is assigned. We stupidly
       have to do this before the MCFG region is subtracted, even if there
       should be ample space for it after the VRAM due to alignment. See
       assumptions in ich9pciFakePCIBIOS(). */
    ComPtr<IGraphicsAdapter> ptrGraphicsAdapter;
    hrc = pMachine->COMGETTER(GraphicsAdapter)(ptrGraphicsAdapter.asOutParam());            H();
    ULONG cVRamMBs = 0;
    hrc = ptrGraphicsAdapter->COMGETTER(VRAMSize)(&cVRamMBs);                               H();
    if (cVRamMBs > 256)
    {
        uint32_t cVRamMBsPowerOfTwo = RT_MIN(cVRamMBs, 1024); /* 1GB is the absolute max given PCI alignment. */
        if (!RT_IS_POWER_OF_TWO(cVRamMBsPowerOfTwo))
            cVRamMBsPowerOfTwo = RT_BIT_32(ASMBitLastSetU32(cVRamMBsPowerOfTwo)); /* returns [1..32] */
        if (cbRamHole / _1M < cVRamMBsPowerOfTwo * 2)
        {
            cbRamHole = cVRamMBsPowerOfTwo * 2 * _1M; /* We must double the VRAM size due to PCI alignment. */
/** @todo sort out the MCFG placement to better use available physical memory. */
            //if (uMcfgBase)
            //    uMcfgBase = _4G - cbRamHole + cVRamMBsPowerOfTwo * _1M;
        }
    }

    ChipsetType_T chipsetType;
    hrc = platform->COMGETTER(ChipsetType)(&chipsetType);                                   H();
    if (chipsetType == ChipsetType_ICH9)
    {
        /* We'd better have 0x10000000 region, to cover 256 buses but this put
         * too much load on hypervisor heap. Linux 4.8 currently complains with
         * ``acpi PNP0A03:00: [Firmware Info]: MMCONFIG for domain 0000 [bus 00-3f]
         *   only partially covers this bridge'' */
        cbMcfgLength = 0x4000000; //0x10000000;
        cbRamHole += cbMcfgLength;
        uMcfgBase = _4G - cbRamHole;
    }

    /* Get the CPU profile name. */
    Bstr bstrCpuProfile;
    hrc = pMachine->COMGETTER(CPUProfile)(bstrCpuProfile.asOutParam());                     H();

    /* Get the X86 platform object. */
    ComPtr<IPlatformX86> platformX86;
    hrc = platform->COMGETTER(X86)(platformX86.asOutParam());                               H();

    /* Check if long mode is enabled. */
    BOOL fIsGuest64Bit;
    hrc = platformX86->GetCPUProperty(CPUPropertyTypeX86_LongMode, &fIsGuest64Bit);         H();

    /*
     * Figure out the IOMMU config.
     */
#if defined(VBOX_WITH_IOMMU_AMD) || defined(VBOX_WITH_IOMMU_INTEL)
    IommuType_T enmIommuType;
    hrc = platform->COMGETTER(IommuType)(&enmIommuType);                                    H();

    /* Resolve 'automatic' type to an Intel or AMD IOMMU based on the host CPU. */
    if (enmIommuType == IommuType_Automatic)
    {
        if (   bstrCpuProfile.startsWith("AMD")
            || bstrCpuProfile.startsWith("Quad-Core AMD")
            || bstrCpuProfile.startsWith("Hygon"))
            enmIommuType = IommuType_AMD;
        else if (bstrCpuProfile.startsWith("Intel"))
        {
            if (   bstrCpuProfile.equals("Intel 8086")
                || bstrCpuProfile.equals("Intel 80186")
                || bstrCpuProfile.equals("Intel 80286")
                || bstrCpuProfile.equals("Intel 80386")
                || bstrCpuProfile.equals("Intel 80486"))
                enmIommuType = IommuType_None;
            else
                enmIommuType = IommuType_Intel;
        }
# if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
        else if (ASMIsAmdCpu())
            enmIommuType = IommuType_AMD;
        else if (ASMIsIntelCpu())
            enmIommuType = IommuType_Intel;
# endif
        else
        {
            /** @todo Should we handle other CPUs like Shanghai, VIA etc. here? */
            LogRel(("WARNING! Unrecognized CPU type, IOMMU disabled.\n"));
            enmIommuType = IommuType_None;
        }
    }

    if (enmIommuType == IommuType_AMD)
    {
# ifdef VBOX_WITH_IOMMU_AMD
        /*
         * Reserve the specific PCI address of the "SB I/O APIC" when using
         * an AMD IOMMU. Required by Linux guests, see @bugref{9654#c23}.
         */
        uIoApicPciAddress = VBOX_PCI_BDF_SB_IOAPIC;
# else
        LogRel(("WARNING! AMD IOMMU not supported, IOMMU disabled.\n"));
        enmIommuType = IommuType_None;
# endif
    }

    if (enmIommuType == IommuType_Intel)
    {
# ifdef VBOX_WITH_IOMMU_INTEL
        /*
         * Reserve a unique PCI address for the I/O APIC when using
         * an Intel IOMMU. For convenience we use the same address as
         * we do on AMD, see @bugref{9967#c13}.
         */
        uIoApicPciAddress = VBOX_PCI_BDF_SB_IOAPIC;
# else
        LogRel(("WARNING! Intel IOMMU not supported, IOMMU disabled.\n"));
        enmIommuType = IommuType_None;
# endif
    }

    if (   enmIommuType == IommuType_AMD
        || enmIommuType == IommuType_Intel)
    {
        if (chipsetType != ChipsetType_ICH9)
            return pVMM->pfnVMR3SetError(pUVM, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                         N_("IOMMU uses MSIs which requires the ICH9 chipset implementation."));
        if (!fIOAPIC)
            return pVMM->pfnVMR3SetError(pUVM, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                         N_("IOMMU requires an I/O APIC for remapping interrupts."));
    }
#else
    IommuType_T const enmIommuType = IommuType_None;
#endif

    /* Instantiate the bus assignment manager. */
    Assert(enmIommuType != IommuType_Automatic);
    BusAssignmentManager *pBusMgr = mBusMgr = BusAssignmentManager::createInstance(pVMM, chipsetType, enmIommuType);

    ULONG cCpus = 1;
    hrc = pMachine->COMGETTER(CPUCount)(&cCpus);                                            H();

    ULONG ulCpuExecutionCap = 100;
    hrc = pMachine->COMGETTER(CPUExecutionCap)(&ulCpuExecutionCap);                         H();

    VMExecutionEngine_T enmExecEngine = VMExecutionEngine_NotSet;
    hrc = pMachine->COMGETTER(VMExecutionEngine)(&enmExecEngine);                           H();

    LogRel(("Guest architecture: x86\n"));

    Bstr osTypeId;
    hrc = pMachine->COMGETTER(OSTypeId)(osTypeId.asOutParam());                             H();
    LogRel(("Guest OS type: '%s'\n", Utf8Str(osTypeId).c_str()));

    APICMode_T apicMode;
    hrc = firmwareSettings->COMGETTER(APICMode)(&apicMode);                                 H();
    uint32_t uFwAPIC;
    switch (apicMode)
    {
        case APICMode_Disabled:
            uFwAPIC = 0;
            break;
        case APICMode_APIC:
            uFwAPIC = 1;
            break;
        case APICMode_X2APIC:
            uFwAPIC = 2;
            break;
        default:
            AssertMsgFailed(("Invalid APICMode=%d\n", apicMode));
            uFwAPIC = 1;
            break;
    }

    ComPtr<IGuestOSType> pGuestOSType;
    virtualBox->GetGuestOSType(osTypeId.raw(), pGuestOSType.asOutParam());

    BOOL fOsXGuest = FALSE;
    BOOL fWinGuest = FALSE;
    BOOL fOs2Guest = FALSE;
    BOOL fW9xGuest = FALSE;
    BOOL fDosGuest = FALSE;
    if (pGuestOSType.isNotNull())
    {
        Bstr guestTypeFamilyId;
        hrc = pGuestOSType->COMGETTER(FamilyId)(guestTypeFamilyId.asOutParam());            H();
        fOsXGuest = guestTypeFamilyId == Bstr("MacOS");
        fWinGuest = guestTypeFamilyId == Bstr("Windows");
        fOs2Guest = osTypeId.startsWith(GUEST_OS_ID_STR_PARTIAL("OS2"));
        fW9xGuest = osTypeId.startsWith(GUEST_OS_ID_STR_PARTIAL("Windows9"));    /* Does not include Windows Me. */
        fDosGuest = osTypeId.equals(GUEST_OS_ID_STR_X86("DOS")) || osTypeId.equals(GUEST_OS_ID_STR_X86("Windows31"));
    }

    ComPtr<IPlatformProperties> platformProperties;
    virtualBox->GetPlatformProperties(PlatformArchitecture_x86, platformProperties.asOutParam());

    /*
     * Get root node first.
     * This is the only node in the tree.
     */
    PCFGMNODE pRoot = pVMM->pfnCFGMR3GetRootU(pUVM);
    Assert(pRoot);

    // catching throws from InsertConfigString and friends.
    try
    {

        /*
         * Set the root (and VMM) level values.
         */
        hrc = pMachine->COMGETTER(Name)(bstr.asOutParam());                                 H();
        InsertConfigString(pRoot,  "Name",                 bstr);
        InsertConfigBytes(pRoot,   "UUID", &HardwareUuid, sizeof(HardwareUuid));
        InsertConfigInteger(pRoot, "RamSize",              cbRam);
        InsertConfigInteger(pRoot, "RamHoleSize",          cbRamHole);
        InsertConfigInteger(pRoot, "NumCPUs",              cCpus);
        InsertConfigInteger(pRoot, "CpuExecutionCap",      ulCpuExecutionCap);
        InsertConfigInteger(pRoot, "TimerMillies",         10);

        BOOL fPageFusion = FALSE;
        hrc = pMachine->COMGETTER(PageFusionEnabled)(&fPageFusion);                         H();
        InsertConfigInteger(pRoot, "PageFusionAllowed",    fPageFusion); /* boolean */

        /* Not necessary, but makes sure this setting ends up in the release log. */
        ULONG ulBalloonSize = 0;
        hrc = pMachine->COMGETTER(MemoryBalloonSize)(&ulBalloonSize);                       H();
        InsertConfigInteger(pRoot, "MemBalloonSize",       ulBalloonSize);

        /*
         * EM values (before CPUM as it may need to set IemExecutesAll).
         */
        PCFGMNODE pEM;
        InsertConfigNode(pRoot, "EM", &pEM);

        /* Triple fault behavior. */
        BOOL fTripleFaultReset = false;
        hrc = platformX86->GetCPUProperty(CPUPropertyTypeX86_TripleFaultReset, &fTripleFaultReset); H();
        InsertConfigInteger(pEM, "TripleFaultReset", fTripleFaultReset);

        /*
         * CPUM values.
         */
        PCFGMNODE pCPUM;
        InsertConfigNode(pRoot, "CPUM", &pCPUM);
        PCFGMNODE pIsaExts;
        InsertConfigNode(pCPUM, "IsaExts", &pIsaExts);

        /* Host CPUID leaf overrides. */
        for (uint32_t iOrdinal = 0; iOrdinal < _4K; iOrdinal++)
        {
            ULONG uLeaf, uSubLeaf, uEax, uEbx, uEcx, uEdx;
            hrc = platformX86->GetCPUIDLeafByOrdinal(iOrdinal, &uLeaf, &uSubLeaf, &uEax, &uEbx, &uEcx, &uEdx);
            if (hrc == E_INVALIDARG)
                break;
            H();
            PCFGMNODE pLeaf;
            InsertConfigNode(pCPUM, Utf8StrFmt("HostCPUID/%RX32", uLeaf).c_str(), &pLeaf);
            /** @todo Figure out how to tell the VMM about uSubLeaf   */
            InsertConfigInteger(pLeaf, "eax", uEax);
            InsertConfigInteger(pLeaf, "ebx", uEbx);
            InsertConfigInteger(pLeaf, "ecx", uEcx);
            InsertConfigInteger(pLeaf, "edx", uEdx);
        }

        /* We must limit CPUID count for Windows NT 4, as otherwise it stops
        with error 0x3e (MULTIPROCESSOR_CONFIGURATION_NOT_SUPPORTED). */
        if (osTypeId == GUEST_OS_ID_STR_X86("WindowsNT4"))
        {
            LogRel(("Limiting CPUID leaf count for NT4 guests\n"));
            InsertConfigInteger(pCPUM, "NT4LeafLimit", true);
        }

        if (fOsXGuest)
        {
            /* Expose extended MWAIT features to Mac OS X guests. */
            LogRel(("Using MWAIT extensions\n"));
            InsertConfigInteger(pIsaExts, "MWaitExtensions", true);

            /* Fake the CPU family/model so the guest works.  This is partly
               because older mac releases really doesn't work on newer cpus,
               and partly because mac os x expects more from systems with newer
               cpus (MSRs, power features, whatever). */
            uint32_t uMaxIntelFamilyModelStep = UINT32_MAX;
            if (   osTypeId == GUEST_OS_ID_STR_X86("MacOS")
                || osTypeId == GUEST_OS_ID_STR_X64("MacOS"))
                uMaxIntelFamilyModelStep = RT_MAKE_U32_FROM_U8(1, 23, 6, 0); /* Penryn / X5482. */
            else if (   osTypeId == GUEST_OS_ID_STR_X86("MacOS106")
                     || osTypeId == GUEST_OS_ID_STR_X64("MacOS106"))
                uMaxIntelFamilyModelStep = RT_MAKE_U32_FROM_U8(1, 23, 6, 0); /* Penryn / X5482 */
            else if (   osTypeId == GUEST_OS_ID_STR_X86("MacOS107")
                     || osTypeId == GUEST_OS_ID_STR_X64("MacOS107"))
                uMaxIntelFamilyModelStep = RT_MAKE_U32_FROM_U8(1, 23, 6, 0); /* Penryn / X5482 */ /** @todo figure out
                                                                                what is required here. */
            else if (   osTypeId == GUEST_OS_ID_STR_X86("MacOS108")
                     || osTypeId == GUEST_OS_ID_STR_X64("MacOS108"))
                uMaxIntelFamilyModelStep = RT_MAKE_U32_FROM_U8(1, 23, 6, 0); /* Penryn / X5482 */ /** @todo figure out
                                                                                what is required here. */
            else if (   osTypeId == GUEST_OS_ID_STR_X86("MacOS109")
                     || osTypeId == GUEST_OS_ID_STR_X64("MacOS109"))
                uMaxIntelFamilyModelStep = RT_MAKE_U32_FROM_U8(1, 23, 6, 0); /* Penryn / X5482 */ /** @todo figure
                                                                                out what is required here. */
            if (uMaxIntelFamilyModelStep != UINT32_MAX)
                InsertConfigInteger(pCPUM, "MaxIntelFamilyModelStep", uMaxIntelFamilyModelStep);
        }

        /* CPU Portability level, */
        ULONG uCpuIdPortabilityLevel = 0;
        hrc = pMachine->COMGETTER(CPUIDPortabilityLevel)(&uCpuIdPortabilityLevel);          H();
        InsertConfigInteger(pCPUM, "PortableCpuIdLevel", uCpuIdPortabilityLevel);

        /* Physical Address Extension (PAE) */
        BOOL fEnablePAE = false;
        hrc = platformX86->GetCPUProperty(CPUPropertyTypeX86_PAE, &fEnablePAE);             H();
        fEnablePAE |= fIsGuest64Bit;
        InsertConfigInteger(pRoot, "EnablePAE", fEnablePAE);

        /* 64-bit guests (long mode) */
        InsertConfigInteger(pCPUM, "Enable64bit", fIsGuest64Bit);

        /* APIC/X2APIC configuration */
        BOOL fEnableAPIC = true;
        BOOL fEnableX2APIC = true;
        hrc = platformX86->GetCPUProperty(CPUPropertyTypeX86_APIC, &fEnableAPIC);           H();
        hrc = platformX86->GetCPUProperty(CPUPropertyTypeX86_X2APIC, &fEnableX2APIC);       H();
        if (fEnableX2APIC)
            Assert(fEnableAPIC);

        /* CPUM profile name. */
        InsertConfigString(pCPUM, "GuestCpuName", bstrCpuProfile);

        /*
         * Temporary(?) hack to make sure we emulate the ancient 16-bit CPUs
         * correctly.   There are way too many #UDs we'll miss using VT-x,
         * raw-mode or qemu for the 186 and 286, while we'll get undefined opcodes
         * dead wrong on 8086 (see http://www.os2museum.com/wp/undocumented-8086-opcodes/).
         */
        if (   bstrCpuProfile.equals("Intel 80386") /* just for now */
            || bstrCpuProfile.equals("Intel 80286")
            || bstrCpuProfile.equals("Intel 80186")
            || bstrCpuProfile.equals("Nec V20")
            || bstrCpuProfile.equals("Intel 8086") )
        {
            InsertConfigInteger(pEM, "IemExecutesAll", true);
            if (!bstrCpuProfile.equals("Intel 80386"))
            {
                fEnableAPIC = false;
                fIOAPIC     = false;
            }
            fEnableX2APIC = false;
        }

        /* Adjust firmware APIC handling to stay within the VCPU limits. */
        if (uFwAPIC == 2 && !fEnableX2APIC)
        {
            if (fEnableAPIC)
                uFwAPIC = 1;
            else
                uFwAPIC = 0;
            LogRel(("Limiting the firmware APIC level from x2APIC to %s\n", fEnableAPIC ? "APIC" : "Disabled"));
        }
        else if (uFwAPIC == 1 && !fEnableAPIC)
        {
            uFwAPIC = 0;
            LogRel(("Limiting the firmware APIC level from APIC to Disabled\n"));
        }

        /* Speculation Control. */
        BOOL fSpecCtrl = FALSE;
        hrc = platformX86->GetCPUProperty(CPUPropertyTypeX86_SpecCtrl, &fSpecCtrl);   H();
        InsertConfigInteger(pCPUM, "SpecCtrl", fSpecCtrl);

        /* Nested VT-x / AMD-V. */
        BOOL fNestedHWVirt = FALSE;
        hrc = platformX86->GetCPUProperty(CPUPropertyTypeX86_HWVirt, &fNestedHWVirt); H();
        InsertConfigInteger(pCPUM, "NestedHWVirt", fNestedHWVirt ? true : false);

        /*
         * Hardware virtualization extensions.
         */
        /* Sanitize valid/useful APIC combinations, see @bugref{8868}. */
        if (!fEnableAPIC)
        {
            if (fIsGuest64Bit)
                return pVMM->pfnVMR3SetError(pUVM, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                             N_("Cannot disable the APIC for a 64-bit guest."));
            if (cCpus > 1)
                return pVMM->pfnVMR3SetError(pUVM, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                             N_("Cannot disable the APIC for an SMP guest."));
            if (fIOAPIC)
                return pVMM->pfnVMR3SetError(pUVM, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                             N_("Cannot disable the APIC when the I/O APIC is present."));
        }

        BOOL fHMEnabled;
        hrc = platformX86->GetHWVirtExProperty(HWVirtExPropertyType_Enabled, &fHMEnabled);      H();
        if (cCpus > 1 && !fHMEnabled)
        {
            LogRel(("Forced fHMEnabled to TRUE by SMP guest.\n"));
            fHMEnabled = TRUE;
        }

        BOOL fHMForced;
        fHMEnabled = fHMForced = TRUE;
        LogRel(("fHMForced=true - No raw-mode support in this build!\n"));
        if (!fHMForced) /* No need to query if already forced above. */
        {
            hrc = platformX86->GetHWVirtExProperty(HWVirtExPropertyType_Force, &fHMForced);     H();
            if (fHMForced)
                LogRel(("fHMForced=true - HWVirtExPropertyType_Force\n"));
        }
        InsertConfigInteger(pRoot, "HMEnabled", fHMEnabled);

        /* /HM/xyz */
        PCFGMNODE pHM;
        InsertConfigNode(pRoot, "HM", &pHM);
        InsertConfigInteger(pHM, "HMForced", fHMForced);
        if (fHMEnabled)
        {
            /* Indicate whether 64-bit guests are supported or not. */
            InsertConfigInteger(pHM, "64bitEnabled", fIsGuest64Bit);

            /** @todo Not exactly pretty to check strings; VBOXOSTYPE would be better,
                but that requires quite a bit of API change in Main. */
            if (    fIOAPIC
                &&  (   osTypeId == GUEST_OS_ID_STR_X86("WindowsNT4")
                     || osTypeId == GUEST_OS_ID_STR_X86("Windows2000")
                     || osTypeId == GUEST_OS_ID_STR_X86("WindowsXP")
                     || osTypeId == GUEST_OS_ID_STR_X86("Windows2003")))
            {
                /* Only allow TPR patching for NT, Win2k, XP and Windows Server 2003. (32 bits mode)
                 * We may want to consider adding more guest OSes (Solaris) later on.
                 */
                InsertConfigInteger(pHM, "TPRPatchingEnabled", 1);
            }
        }

        /*
         * Set VM execution engine.
         */
        /** @todo r=aeichner Maybe provide a better VMM API for this instead of the different CFGM knobs. */
        LogRel(("Using execution engine %u\n", enmExecEngine));
        switch (enmExecEngine)
        {
            case VMExecutionEngine_HwVirt:
                InsertConfigInteger(pEM, "IemExecutesAll", 0);
                InsertConfigInteger(pEM, "IemRecompiled",  0);
                InsertConfigInteger(pHM, "UseNEMInstead",  0);
                InsertConfigInteger(pHM, "FallbackToNEM",  0);
                InsertConfigInteger(pHM, "FallbackToIEM",  0);
                break;
            case VMExecutionEngine_NativeApi:
                InsertConfigInteger(pEM, "IemExecutesAll", 0);
                InsertConfigInteger(pEM, "IemRecompiled",  0);
                InsertConfigInteger(pHM, "UseNEMInstead",  1);
                InsertConfigInteger(pHM, "FallbackToNEM",  1);
                InsertConfigInteger(pHM, "FallbackToIEM",  0);
                break;
            case VMExecutionEngine_Interpreter:
            case VMExecutionEngine_Recompiler:
                InsertConfigInteger(pEM, "IemExecutesAll", 1);
                InsertConfigInteger(pEM, "IemRecompiled",  enmExecEngine == VMExecutionEngine_Recompiler);
                InsertConfigInteger(pHM, "UseNEMInstead",  0);
                InsertConfigInteger(pHM, "FallbackToNEM",  0);
                InsertConfigInteger(pHM, "FallbackToIEM",  1);
                break;
            case VMExecutionEngine_Default:
                break; /* Nothing to do, let VMM decide. */
            default:
                AssertLogRelFailed();
        }

        /* HWVirtEx exclusive mode */
        BOOL fHMExclusive = true;
        hrc = platformProperties->COMGETTER(ExclusiveHwVirt)(&fHMExclusive);                  H();
        InsertConfigInteger(pHM, "Exclusive", fHMExclusive);

        /* Nested paging (VT-x/AMD-V) */
        BOOL fEnableNestedPaging = false;
        hrc = platformX86->GetHWVirtExProperty(HWVirtExPropertyType_NestedPaging, &fEnableNestedPaging); H();
        InsertConfigInteger(pHM, "EnableNestedPaging", fEnableNestedPaging);

        /* Large pages; requires nested paging */
        BOOL fEnableLargePages = false;
        hrc = platformX86->GetHWVirtExProperty(HWVirtExPropertyType_LargePages, &fEnableLargePages); H();
        InsertConfigInteger(pHM, "EnableLargePages", fEnableLargePages);

        /* VPID (VT-x) */
        BOOL fEnableVPID = false;
        hrc = platformX86->GetHWVirtExProperty(HWVirtExPropertyType_VPID, &fEnableVPID);       H();
        InsertConfigInteger(pHM, "EnableVPID", fEnableVPID);

        /* Unrestricted execution aka UX (VT-x) */
        BOOL fEnableUX = false;
        hrc = platformX86->GetHWVirtExProperty(HWVirtExPropertyType_UnrestrictedExecution, &fEnableUX); H();
        InsertConfigInteger(pHM, "EnableUX", fEnableUX);

        /* Virtualized VMSAVE/VMLOAD (AMD-V) */
        BOOL fVirtVmsaveVmload = true;
        hrc = host->GetProcessorFeature(ProcessorFeature_VirtVmsaveVmload, &fVirtVmsaveVmload);     H();
        InsertConfigInteger(pHM, "SvmVirtVmsaveVmload", fVirtVmsaveVmload);

        /* Indirect branch prediction boundraries. */
        BOOL fIBPBOnVMExit = false;
        hrc = platformX86->GetCPUProperty(CPUPropertyTypeX86_IBPBOnVMExit, &fIBPBOnVMExit); H();
        InsertConfigInteger(pHM, "IBPBOnVMExit", fIBPBOnVMExit);

        BOOL fIBPBOnVMEntry = false;
        hrc = platformX86->GetCPUProperty(CPUPropertyTypeX86_IBPBOnVMEntry, &fIBPBOnVMEntry); H();
        InsertConfigInteger(pHM, "IBPBOnVMEntry", fIBPBOnVMEntry);

        BOOL fSpecCtrlByHost = false;
        hrc = platformX86->GetCPUProperty(CPUPropertyTypeX86_SpecCtrlByHost, &fSpecCtrlByHost); H();
        InsertConfigInteger(pHM, "SpecCtrlByHost", fSpecCtrlByHost);

        BOOL fL1DFlushOnSched = true;
        hrc = platformX86->GetCPUProperty(CPUPropertyTypeX86_L1DFlushOnEMTScheduling, &fL1DFlushOnSched); H();
        InsertConfigInteger(pHM, "L1DFlushOnSched", fL1DFlushOnSched);

        BOOL fL1DFlushOnVMEntry = false;
        hrc = platformX86->GetCPUProperty(CPUPropertyTypeX86_L1DFlushOnVMEntry, &fL1DFlushOnVMEntry); H();
        InsertConfigInteger(pHM, "L1DFlushOnVMEntry", fL1DFlushOnVMEntry);

        BOOL fMDSClearOnSched = true;
        hrc = platformX86->GetCPUProperty(CPUPropertyTypeX86_MDSClearOnEMTScheduling, &fMDSClearOnSched); H();
        InsertConfigInteger(pHM, "MDSClearOnSched", fMDSClearOnSched);

        BOOL fMDSClearOnVMEntry = false;
        hrc = platformX86->GetCPUProperty(CPUPropertyTypeX86_MDSClearOnVMEntry, &fMDSClearOnVMEntry); H();
        InsertConfigInteger(pHM, "MDSClearOnVMEntry", fMDSClearOnVMEntry);

        /* Reset overwrite. */
        mfTurnResetIntoPowerOff = GetExtraDataBoth(virtualBox, pMachine,
                                                   "VBoxInternal2/TurnResetIntoPowerOff", &strTmp)->equals("1");
        if (mfTurnResetIntoPowerOff)
            InsertConfigInteger(pRoot, "PowerOffInsteadOfReset", 1);

        /** @todo This is a bit redundant but in order to avoid forcing setting version bumps later on
         * and don't magically change behavior of old VMs we have to keep this for now. As soon as the user sets
         * the execution to anything else than Default the execution engine setting takes precedence.
         */
        if (enmExecEngine == VMExecutionEngine_Default)
        {
            /* Use NEM rather than HM. */
            BOOL fUseNativeApi = false;
            hrc = platformX86->GetHWVirtExProperty(HWVirtExPropertyType_UseNativeApi, &fUseNativeApi); H();
            InsertConfigInteger(pHM, "UseNEMInstead", fUseNativeApi);
        }

        /* Enable workaround for missing TLB flush for OS/2 guests, see ticketref:20625. */
        if (fOs2Guest)
            InsertConfigInteger(pHM, "MissingOS2TlbFlushWorkaround", 1);

        /*
         * NEM
         */
        PCFGMNODE pNEM;
        InsertConfigNode(pRoot, "NEM", &pNEM);
        InsertConfigInteger(pNEM, "Allow64BitGuests", fIsGuest64Bit);

        InsertConfigInteger(pNEM, "IBPBOnVMExit", fIBPBOnVMExit);
        InsertConfigInteger(pNEM, "IBPBOnVMEntry", fIBPBOnVMEntry);
        InsertConfigInteger(pNEM, "L1DFlushOnSched", fL1DFlushOnSched);
        InsertConfigInteger(pNEM, "L1DFlushOnVMEntry", fL1DFlushOnVMEntry);
        InsertConfigInteger(pNEM, "MDSClearOnSched", fMDSClearOnSched);
        InsertConfigInteger(pNEM, "MDSClearOnVMEntry", fMDSClearOnVMEntry);

        /*
         * Paravirt. provider.
         */
        PCFGMNODE pParavirtNode;
        InsertConfigNode(pRoot, "GIM", &pParavirtNode);
        const char *pcszParavirtProvider;
        bool fGimDeviceNeeded = true;
        switch (enmParavirtProvider)
        {
            case ParavirtProvider_None:
                pcszParavirtProvider = "None";
                fGimDeviceNeeded = false;
                break;

            case ParavirtProvider_Minimal:
                pcszParavirtProvider = "Minimal";
                break;

            case ParavirtProvider_HyperV:
                pcszParavirtProvider = "HyperV";
                break;

            case ParavirtProvider_KVM:
                pcszParavirtProvider = "KVM";
                break;

            default:
                AssertMsgFailed(("Invalid enmParavirtProvider=%d\n", enmParavirtProvider));
                return pVMM->pfnVMR3SetError(pUVM, VERR_INVALID_PARAMETER, RT_SRC_POS, N_("Invalid paravirt. provider '%d'"),
                                             enmParavirtProvider);
        }
        InsertConfigString(pParavirtNode, "Provider", pcszParavirtProvider);

        /*
         * Parse paravirt. debug options.
         */
        bool         fGimDebug          = false;
        com::Utf8Str strGimDebugAddress = "127.0.0.1";
        uint32_t     uGimDebugPort      = 50000;
        if (strParavirtDebug.isNotEmpty())
        {
            /* Hyper-V debug options. */
            if (enmParavirtProvider == ParavirtProvider_HyperV)
            {
                bool         fGimHvDebug = false;
                com::Utf8Str strGimHvVendor;
                bool         fGimHvVsIf = false;
                bool         fGimHvHypercallIf = false;

                size_t       uPos = 0;
                com::Utf8Str strDebugOptions = strParavirtDebug;
                com::Utf8Str strKey;
                com::Utf8Str strVal;
                while ((uPos = strDebugOptions.parseKeyValue(strKey, strVal, uPos)) != com::Utf8Str::npos)
                {
                    if (strKey == "enabled")
                    {
                        if (strVal.toUInt32() == 1)
                        {
                            /* Apply defaults.
                               The defaults are documented in the user manual,
                               changes need to be reflected accordingly. */
                            fGimHvDebug       = true;
                            strGimHvVendor    = "Microsoft Hv";
                            fGimHvVsIf        = true;
                            fGimHvHypercallIf = false;
                        }
                        /* else: ignore, i.e. don't assert below with 'enabled=0'. */
                    }
                    else if (strKey == "address")
                        strGimDebugAddress = strVal;
                    else if (strKey == "port")
                        uGimDebugPort = strVal.toUInt32();
                    else if (strKey == "vendor")
                        strGimHvVendor = strVal;
                    else if (strKey == "vsinterface")
                        fGimHvVsIf = RT_BOOL(strVal.toUInt32());
                    else if (strKey == "hypercallinterface")
                        fGimHvHypercallIf = RT_BOOL(strVal.toUInt32());
                    else
                    {
                        AssertMsgFailed(("Unrecognized Hyper-V debug option '%s'\n", strKey.c_str()));
                        return pVMM->pfnVMR3SetError(pUVM, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                                     N_("Unrecognized Hyper-V debug option '%s' in '%s'"), strKey.c_str(),
                                                     strDebugOptions.c_str());
                    }
                }

                /* Update HyperV CFGM node with active debug options. */
                if (fGimHvDebug)
                {
                    PCFGMNODE pHvNode;
                    InsertConfigNode(pParavirtNode, "HyperV", &pHvNode);
                    InsertConfigString(pHvNode,  "VendorID", strGimHvVendor);
                    InsertConfigInteger(pHvNode, "VSInterface", fGimHvVsIf ? 1 : 0);
                    InsertConfigInteger(pHvNode, "HypercallDebugInterface", fGimHvHypercallIf ? 1 : 0);
                    fGimDebug = true;
                }
            }
        }

        /*
         * Guest Compatibility Manager.
         */
        PCFGMNODE pGcmNode;
        InsertConfigNode(pRoot, "GCM", &pGcmNode);
        /* OS/2 and Win9x guests can run DOS apps so they get the DOS specific
           fixes as well. */
        if (fDosGuest || fOs2Guest || fW9xGuest)
            InsertConfigInteger(pGcmNode, "DivByZeroDOS", 1);
        if (fOs2Guest)
            InsertConfigInteger(pGcmNode, "DivByZeroOS2", 1);
        if (fW9xGuest)
            InsertConfigInteger(pGcmNode, "DivByZeroWin9x", 1);
        /* MesaVmsvgaDrv (formerly LovelyMesaDrvWorkaround) is set futher down. */

        /*
         * MM values.
         */
        PCFGMNODE pMM;
        InsertConfigNode(pRoot, "MM", &pMM);
        InsertConfigInteger(pMM, "CanUseLargerHeap", chipsetType == ChipsetType_ICH9);

        /*
         * PDM config.
         *  Load drivers in VBoxC.[so|dll]
         */
        vrc = i_configPdm(pMachine, pVMM, pUVM, pRoot);                                          VRC();

        /*
         * Devices
         */
        PCFGMNODE pDevices = NULL;      /* /Devices */
        PCFGMNODE pDev = NULL;          /* /Devices/Dev/ */
        PCFGMNODE pInst = NULL;         /* /Devices/Dev/0/ */
        PCFGMNODE pCfg = NULL;          /* /Devices/Dev/.../Config/ */
        PCFGMNODE pLunL0 = NULL;        /* /Devices/Dev/0/LUN#0/ */
        PCFGMNODE pLunL1 = NULL;        /* /Devices/Dev/0/LUN#0/AttachedDriver/ */
        PCFGMNODE pBiosCfg = NULL;      /* /Devices/pcbios/0/Config/ */
        PCFGMNODE pNetBootCfg = NULL;   /* /Devices/pcbios/0/Config/NetBoot/ */

        InsertConfigNode(pRoot, "Devices", &pDevices);

        /*
         * GIM Device
         */
        if (fGimDeviceNeeded)
        {
            InsertConfigNode(pDevices, "GIMDev", &pDev);
            InsertConfigNode(pDev,     "0", &pInst);
            InsertConfigInteger(pInst, "Trusted",              1); /* boolean */
            //InsertConfigNode(pInst,    "Config", &pCfg);

            if (fGimDebug)
            {
                InsertConfigNode(pInst,     "LUN#998", &pLunL0);
                InsertConfigString(pLunL0,  "Driver", "UDP");
                InsertConfigNode(pLunL0,    "Config", &pLunL1);
                InsertConfigString(pLunL1,  "ServerAddress", strGimDebugAddress);
                InsertConfigInteger(pLunL1, "ServerPort", uGimDebugPort);
            }
        }

        /*
         * PC Arch.
         */
        InsertConfigNode(pDevices, "pcarch", &pDev);
        InsertConfigNode(pDev,     "0", &pInst);
        InsertConfigInteger(pInst, "Trusted",              1); /* boolean */
        InsertConfigNode(pInst,    "Config", &pCfg);

        /*
         * The time offset
         */
        LONG64 timeOffset;
        hrc = firmwareSettings->COMGETTER(TimeOffset)(&timeOffset);                             H();
        PCFGMNODE pTMNode;
        InsertConfigNode(pRoot, "TM", &pTMNode);
        InsertConfigInteger(pTMNode, "UTCOffset", timeOffset * 1000000);

        /*
         * DMA
         */
        InsertConfigNode(pDevices, "8237A", &pDev);
        InsertConfigNode(pDev,     "0", &pInst);
        InsertConfigInteger(pInst, "Trusted", 1); /* boolean */

        /*
         * PCI buses.
         */
        uint32_t uIocPCIAddress, uHbcPCIAddress;
        switch (chipsetType)
        {
            default:
                AssertFailed();
                RT_FALL_THRU();
            case ChipsetType_PIIX3:
                /* Create the base for adding bridges on demand */
                InsertConfigNode(pDevices, "pcibridge", NULL);

                InsertConfigNode(pDevices, "pci", &pDev);
                uHbcPCIAddress = (0x0 << 16) | 0;
                uIocPCIAddress = (0x1 << 16) | 0; // ISA controller
                break;
            case ChipsetType_ICH9:
                /* Create the base for adding bridges on demand */
                InsertConfigNode(pDevices, "ich9pcibridge", NULL);

                InsertConfigNode(pDevices, "ich9pci", &pDev);
                uHbcPCIAddress = (0x1e << 16) | 0;
                uIocPCIAddress = (0x1f << 16) | 0; // LPC controller
                break;
        }
        InsertConfigNode(pDev,     "0", &pInst);
        InsertConfigInteger(pInst, "Trusted",              1); /* boolean */
        InsertConfigNode(pInst,    "Config", &pCfg);
        InsertConfigInteger(pCfg, "IOAPIC", fIOAPIC);
        if (chipsetType == ChipsetType_ICH9)
        {
            /* Provide MCFG info */
            InsertConfigInteger(pCfg,  "McfgBase",   uMcfgBase);
            InsertConfigInteger(pCfg,  "McfgLength", cbMcfgLength);

#ifdef VBOX_WITH_PCI_PASSTHROUGH
            /* Add PCI passthrough devices */
            hrc = i_attachRawPCIDevices(pUVM, pBusMgr, pDevices);                           H();
#endif

            if (enmIommuType == IommuType_AMD)
            {
                /* AMD IOMMU. */
                InsertConfigNode(pDevices, "iommu-amd", &pDev);
                InsertConfigNode(pDev,     "0", &pInst);
                InsertConfigInteger(pInst, "Trusted",   1); /* boolean */
                InsertConfigNode(pInst,    "Config", &pCfg);
                hrc = pBusMgr->assignPCIDevice("iommu-amd", pInst);                         H();

                /* The AMD IOMMU device needs to know which PCI slot it's in, see @bugref{9654#c104}. */
                {
                    PCIBusAddress Address;
                    if (pBusMgr->findPCIAddress("iommu-amd", 0, Address))
                    {
                        uint32_t const u32IommuAddress = (Address.miDevice << 16) | Address.miFn;
                        InsertConfigInteger(pCfg, "PCIAddress", u32IommuAddress);
                    }
                    else
                        return pVMM->pfnVMR3SetError(pUVM, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                                     N_("Failed to find PCI address of the assigned IOMMU device!"));
                }

                PCIBusAddress PCIAddr = PCIBusAddress((int32_t)uIoApicPciAddress);
                hrc = pBusMgr->assignPCIDevice("sb-ioapic", NULL /* pCfg */, PCIAddr, true /*fGuestAddressRequired*/);  H();
            }
            else if (enmIommuType == IommuType_Intel)
            {
                /* Intel IOMMU. */
                InsertConfigNode(pDevices, "iommu-intel", &pDev);
                InsertConfigNode(pDev,     "0", &pInst);
                InsertConfigInteger(pInst, "Trusted",   1); /* boolean */
                InsertConfigNode(pInst,    "Config", &pCfg);
                hrc = pBusMgr->assignPCIDevice("iommu-intel", pInst);                       H();

                PCIBusAddress PCIAddr = PCIBusAddress((int32_t)uIoApicPciAddress);
                hrc = pBusMgr->assignPCIDevice("sb-ioapic", NULL /* pCfg */, PCIAddr, true /*fGuestAddressRequired*/);  H();
            }
        }

        /*
         * Enable the following devices: HPET, SMC and LPC on MacOS X guests or on ICH9 chipset
         */

        /*
         * High Precision Event Timer (HPET)
         */
        BOOL fHPETEnabled;
        /* Other guests may wish to use HPET too, but MacOS X not functional without it */
        hrc = platformX86->COMGETTER(HPETEnabled)(&fHPETEnabled);                              H();
        /* so always enable HPET in extended profile */
        fHPETEnabled |= fOsXGuest;
        /* HPET is always present on ICH9 */
        fHPETEnabled |= (chipsetType == ChipsetType_ICH9);
        if (fHPETEnabled)
        {
            InsertConfigNode(pDevices, "hpet", &pDev);
            InsertConfigNode(pDev,     "0", &pInst);
            InsertConfigInteger(pInst, "Trusted",   1); /* boolean */
            InsertConfigNode(pInst,    "Config", &pCfg);
            InsertConfigInteger(pCfg,  "ICH9", (chipsetType == ChipsetType_ICH9) ? 1 : 0);  /* boolean */
        }

        /*
         * System Management Controller (SMC)
         */
        BOOL fSmcEnabled;
        fSmcEnabled = fOsXGuest;
        if (fSmcEnabled)
        {
            InsertConfigNode(pDevices, "smc", &pDev);
            InsertConfigNode(pDev,     "0", &pInst);
            InsertConfigInteger(pInst, "Trusted",   1); /* boolean */
            InsertConfigNode(pInst,    "Config", &pCfg);

            bool fGetKeyFromRealSMC;
            Utf8Str strKey;
            vrc = getSmcDeviceKey(virtualBox, pMachine, &strKey, &fGetKeyFromRealSMC);
            AssertRCReturn(vrc, vrc);

            if (!fGetKeyFromRealSMC)
                InsertConfigString(pCfg,   "DeviceKey", strKey);
            InsertConfigInteger(pCfg,  "GetKeyFromRealSMC", fGetKeyFromRealSMC);
        }

        /*
         * Low Pin Count (LPC) bus
         */
        BOOL fLpcEnabled;
        /** @todo implement appropriate getter */
        fLpcEnabled = fOsXGuest || (chipsetType == ChipsetType_ICH9);
        if (fLpcEnabled)
        {
            InsertConfigNode(pDevices, "lpc", &pDev);
            InsertConfigNode(pDev,     "0", &pInst);
            hrc = pBusMgr->assignPCIDevice("lpc", pInst);                                   H();
            InsertConfigInteger(pInst, "Trusted",   1); /* boolean */
        }

        BOOL fShowRtc;
        fShowRtc = fOsXGuest || (chipsetType == ChipsetType_ICH9);

        /*
         * PS/2 keyboard & mouse.
         */
        InsertConfigNode(pDevices, "pckbd", &pDev);
        InsertConfigNode(pDev,     "0", &pInst);
        InsertConfigInteger(pInst, "Trusted",              1); /* boolean */
        InsertConfigNode(pInst,    "Config", &pCfg);

        KeyboardHIDType_T aKbdHID;
        hrc = pMachine->COMGETTER(KeyboardHIDType)(&aKbdHID);                               H();
        if (aKbdHID != KeyboardHIDType_None)
        {
            InsertConfigNode(pInst,    "LUN#0", &pLunL0);
            InsertConfigString(pLunL0, "Driver",               "KeyboardQueue");
            InsertConfigNode(pLunL0,   "Config", &pCfg);
            InsertConfigInteger(pCfg,  "QueueSize",            64);

            InsertConfigNode(pLunL0,   "AttachedDriver", &pLunL1);
            InsertConfigString(pLunL1, "Driver",               "MainKeyboard");
        }

        PointingHIDType_T aPointingHID;
        hrc = pMachine->COMGETTER(PointingHIDType)(&aPointingHID);                          H();
        if (aPointingHID != PointingHIDType_None)
        {
            InsertConfigNode(pInst,    "LUN#1", &pLunL0);
            InsertConfigString(pLunL0, "Driver",               "MouseQueue");
            InsertConfigNode(pLunL0,   "Config", &pCfg);
            InsertConfigInteger(pCfg, "QueueSize",            128);

            InsertConfigNode(pLunL0,   "AttachedDriver", &pLunL1);
            InsertConfigString(pLunL1, "Driver",               "MainMouse");
        }

        /*
         * i8254 Programmable Interval Timer And Dummy Speaker
         */
        InsertConfigNode(pDevices, "i8254", &pDev);
        InsertConfigNode(pDev,     "0", &pInst);
        InsertConfigNode(pInst,    "Config", &pCfg);
#ifdef DEBUG
        InsertConfigInteger(pInst, "Trusted",              1); /* boolean */
#endif

        /*
         * i8259 Programmable Interrupt Controller.
         */
        InsertConfigNode(pDevices, "i8259", &pDev);
        InsertConfigNode(pDev,     "0", &pInst);
        InsertConfigInteger(pInst, "Trusted",              1); /* boolean */
        InsertConfigNode(pInst,    "Config", &pCfg);

        /*
         * Advanced Programmable Interrupt Controller.
         * SMP: Each CPU has a LAPIC, but we have a single device representing all LAPICs states,
         *      thus only single insert
         */
        if (fEnableAPIC)
        {
            InsertConfigNode(pDevices, "apic", &pDev);
            InsertConfigNode(pDev, "0", &pInst);
            InsertConfigInteger(pInst, "Trusted",          1); /* boolean */
            InsertConfigNode(pInst,    "Config", &pCfg);
            InsertConfigInteger(pCfg,  "IOAPIC", fIOAPIC);
            PDMAPICMODE enmAPICMode = PDMAPICMODE_APIC;
            if (fEnableX2APIC)
                enmAPICMode = PDMAPICMODE_X2APIC;
            else if (!fEnableAPIC)
                enmAPICMode = PDMAPICMODE_NONE;
            InsertConfigInteger(pCfg,  "Mode", enmAPICMode);
            InsertConfigInteger(pCfg,  "NumCPUs", cCpus);

            if (fIOAPIC)
            {
                /*
                 * I/O Advanced Programmable Interrupt Controller.
                 */
                InsertConfigNode(pDevices, "ioapic", &pDev);
                InsertConfigNode(pDev,     "0", &pInst);
                InsertConfigInteger(pInst, "Trusted",      1); /* boolean */
                InsertConfigNode(pInst,    "Config", &pCfg);
                InsertConfigInteger(pCfg,  "NumCPUs", cCpus);
                if (enmIommuType == IommuType_AMD)
                    InsertConfigInteger(pCfg, "PCIAddress", uIoApicPciAddress);
                else if (enmIommuType == IommuType_Intel)
                {
                    InsertConfigString(pCfg, "ChipType", "DMAR");
                    InsertConfigInteger(pCfg, "PCIAddress", uIoApicPciAddress);
                }
            }
        }

        /*
         * RTC MC146818.
         */
        InsertConfigNode(pDevices, "mc146818", &pDev);
        InsertConfigNode(pDev,     "0", &pInst);
        InsertConfigNode(pInst,    "Config", &pCfg);
        BOOL fRTCUseUTC;
        hrc = platform->COMGETTER(RTCUseUTC)(&fRTCUseUTC);                                   H();
        InsertConfigInteger(pCfg,  "UseUTC", fRTCUseUTC ? 1 : 0);

        /*
         * VGA.
         */
        ComPtr<IGraphicsAdapter> pGraphicsAdapter;
        hrc = pMachine->COMGETTER(GraphicsAdapter)(pGraphicsAdapter.asOutParam());           H();
        GraphicsControllerType_T enmGraphicsController;
        hrc = pGraphicsAdapter->COMGETTER(GraphicsControllerType)(&enmGraphicsController);          H();
        switch (enmGraphicsController)
        {
            case GraphicsControllerType_Null:
                break;
#ifdef VBOX_WITH_VMSVGA
            case GraphicsControllerType_VMSVGA:
                InsertConfigInteger(pHM, "LovelyMesaDrvWorkaround", 1);  /* hits someone else's logging backdoor. */
                InsertConfigInteger(pNEM, "LovelyMesaDrvWorkaround", 1); /* hits someone else's logging backdoor. */
                InsertConfigInteger(pGcmNode, "MesaVmsvgaDrv", 1);       /* hits someone else's logging backdoor. */
                RT_FALL_THROUGH();
            case GraphicsControllerType_VBoxSVGA:
#endif
            case GraphicsControllerType_VBoxVGA:
                vrc = i_configGraphicsController(pDevices, enmGraphicsController, pBusMgr, pMachine, pGraphicsAdapter, firmwareSettings);
                if (FAILED(vrc))
                    return vrc;
                break;
            default:
                AssertMsgFailed(("Invalid graphicsController=%d\n", enmGraphicsController));
                return pVMM->pfnVMR3SetError(pUVM, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                             N_("Invalid graphics controller type '%d'"), enmGraphicsController);
        }

#if defined(VBOX_WITH_TPM)
        /*
         * Configure the Trusted Platform Module.
         */
        ComObjPtr<ITrustedPlatformModule> ptrTpm;
        TpmType_T enmTpmType = TpmType_None;

        hrc = pMachine->COMGETTER(TrustedPlatformModule)(ptrTpm.asOutParam());              H();
        hrc = ptrTpm->COMGETTER(Type)(&enmTpmType);                                         H();
        if (enmTpmType != TpmType_None)
        {
            InsertConfigNode(pDevices, "tpm", &pDev);
            InsertConfigNode(pDev,     "0", &pInst);
            InsertConfigInteger(pInst, "Trusted", 1); /* boolean */
            InsertConfigNode(pInst,    "Config", &pCfg);
            InsertConfigNode(pInst,    "LUN#0", &pLunL0);

            switch (enmTpmType)
            {
                case TpmType_v1_2:
                case TpmType_v2_0:
                    InsertConfigString(pLunL0, "Driver",               "TpmEmuTpms");
                    InsertConfigNode(pLunL0,   "Config", &pCfg);
                    InsertConfigInteger(pCfg, "TpmVersion", enmTpmType == TpmType_v1_2 ? 1 : 2);
                    InsertConfigNode(pLunL0, "AttachedDriver", &pLunL1);
                    InsertConfigString(pLunL1, "Driver", "NvramStore");
                    break;
                case TpmType_Host:
#if defined(RT_OS_LINUX) || defined(RT_OS_WINDOWS)
                    InsertConfigString(pLunL0, "Driver",               "TpmHost");
                    InsertConfigNode(pLunL0,   "Config", &pCfg);
#endif
                    break;
                case TpmType_Swtpm:
                    hrc = ptrTpm->COMGETTER(Location)(bstr.asOutParam());                   H();
                    InsertConfigString(pLunL0, "Driver",               "TpmEmu");
                    InsertConfigNode(pLunL0,   "Config", &pCfg);
                    InsertConfigString(pCfg,   "Location", bstr);
                    break;
                default:
                    AssertFailedBreak();
            }

            /* Add the device for the physical presence interface. */
            InsertConfigNode(   pDevices, "tpm-ppi",  &pDev);
            InsertConfigNode(   pDev,     "0",        &pInst);
            InsertConfigInteger(pInst,    "Trusted",  1); /* boolean */
            InsertConfigNode(   pInst,    "Config",   &pCfg);
            InsertConfigInteger(pCfg,     "MmioBase", TPM_PPI_MMIO_BASE_DEFAULT);
        }
#endif

        /*
         * Firmware.
         */
        FirmwareType_T eFwType =  FirmwareType_BIOS;
        hrc = firmwareSettings->COMGETTER(FirmwareType)(&eFwType);                          H();

#ifdef VBOX_WITH_EFI
        BOOL fEfiEnabled = (eFwType >= FirmwareType_EFI) && (eFwType <= FirmwareType_EFIDUAL);
#else
        BOOL fEfiEnabled = false;
#endif
        if (!fEfiEnabled)
        {
            /*
             * PC Bios.
             */
            InsertConfigNode(pDevices, "pcbios", &pDev);
            InsertConfigNode(pDev,     "0", &pInst);
            InsertConfigInteger(pInst, "Trusted",              1); /* boolean */
            InsertConfigNode(pInst,    "Config", &pBiosCfg);
            InsertConfigInteger(pBiosCfg,  "NumCPUs",              cCpus);
            InsertConfigString(pBiosCfg,   "HardDiskDevice",       "piix3ide");
            InsertConfigString(pBiosCfg,   "FloppyDevice",         "i82078");
            InsertConfigInteger(pBiosCfg,  "IOAPIC",               fIOAPIC);
            InsertConfigInteger(pBiosCfg,  "APIC",                 uFwAPIC);
            BOOL fPXEDebug;
            hrc = firmwareSettings->COMGETTER(PXEDebugEnabled)(&fPXEDebug);                     H();
            InsertConfigInteger(pBiosCfg,  "PXEDebug",             fPXEDebug);
            InsertConfigBytes(pBiosCfg,    "UUID", &HardwareUuid,sizeof(HardwareUuid));
            BOOL fUuidLe;
            hrc = firmwareSettings->COMGETTER(SMBIOSUuidLittleEndian)(&fUuidLe);                H();
            InsertConfigInteger(pBiosCfg,  "UuidLe",               fUuidLe);
            BOOL fAutoSerialNumGen;
            hrc = firmwareSettings->COMGETTER(AutoSerialNumGen)(&fAutoSerialNumGen);            H();
            if (fAutoSerialNumGen)
                InsertConfigString(pBiosCfg,  "DmiSystemSerial", "VirtualBox-<DmiSystemUuid>");
            InsertConfigNode(pBiosCfg,     "NetBoot", &pNetBootCfg);
            InsertConfigInteger(pBiosCfg,  "McfgBase",   uMcfgBase);
            InsertConfigInteger(pBiosCfg,  "McfgLength", cbMcfgLength);

            AssertMsgReturn(SchemaDefs::MaxBootPosition <= 9, ("Too many boot devices %d\n", SchemaDefs::MaxBootPosition),
                            VERR_INVALID_PARAMETER);

            for (ULONG pos = 1; pos <= SchemaDefs::MaxBootPosition; ++pos)
            {
                DeviceType_T enmBootDevice;
                hrc = pMachine->GetBootOrder(pos, &enmBootDevice);                          H();

                char szParamName[] = "BootDeviceX";
                szParamName[sizeof(szParamName) - 2] = (char)(pos - 1 + '0');

                const char *pszBootDevice;
                switch (enmBootDevice)
                {
                    case DeviceType_Null:
                        pszBootDevice = "NONE";
                        break;
                    case DeviceType_HardDisk:
                        pszBootDevice = "IDE";
                        break;
                    case DeviceType_DVD:
                        pszBootDevice = "DVD";
                        break;
                    case DeviceType_Floppy:
                        pszBootDevice = "FLOPPY";
                        break;
                    case DeviceType_Network:
                        pszBootDevice = "LAN";
                        break;
                    default:
                        AssertMsgFailed(("Invalid enmBootDevice=%d\n", enmBootDevice));
                        return pVMM->pfnVMR3SetError(pUVM, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                                     N_("Invalid boot device '%d'"), enmBootDevice);
                }
                InsertConfigString(pBiosCfg, szParamName, pszBootDevice);
            }

            /** @todo @bugref{7145}: We might want to enable this by default for new VMs. For now,
             *        this is required for Windows 2012 guests. */
            if (osTypeId == GUEST_OS_ID_STR_X64("Windows2012"))
                InsertConfigInteger(pBiosCfg, "DmiExposeMemoryTable", 1); /* boolean */
        }
        else
        {
            /* Autodetect firmware type, basing on guest type */
            if (eFwType == FirmwareType_EFI)
                eFwType = fIsGuest64Bit ? FirmwareType_EFI64 : FirmwareType_EFI32;
            bool const f64BitEntry = eFwType == FirmwareType_EFI64;

            Assert(eFwType == FirmwareType_EFI64 || eFwType == FirmwareType_EFI32 || eFwType == FirmwareType_EFIDUAL);
#ifdef VBOX_WITH_EFI_IN_DD2
            const char *pszEfiRomFile = eFwType == FirmwareType_EFIDUAL ? "VBoxEFIDual.fd"
                                      : eFwType == FirmwareType_EFI32   ? "VBoxEFI32.fd"
                                      :                                   "VBoxEFI64.fd";
#else
            Utf8Str efiRomFile;
            vrc = findEfiRom(virtualBox, PlatformArchitecture_x86, eFwType, &efiRomFile);
            AssertRCReturn(vrc, vrc);
            const char *pszEfiRomFile = efiRomFile.c_str();
#endif

            /* Get boot args */
            Utf8Str bootArgs;
            GetExtraDataBoth(virtualBox, pMachine, "VBoxInternal2/EfiBootArgs", &bootArgs);

            /* Get device props */
            Utf8Str deviceProps;
            GetExtraDataBoth(virtualBox, pMachine, "VBoxInternal2/EfiDeviceProps", &deviceProps);

            /* Get NVRAM file name */
            Utf8Str strNvram = mptrNvramStore->i_getNonVolatileStorageFile();

            BOOL fUuidLe;
            hrc = firmwareSettings->COMGETTER(SMBIOSUuidLittleEndian)(&fUuidLe);                H();

            BOOL fAutoSerialNumGen;
            hrc = firmwareSettings->COMGETTER(AutoSerialNumGen)(&fAutoSerialNumGen);            H();

            /* Get graphics mode settings */
            uint32_t u32GraphicsMode = UINT32_MAX;
            GetExtraDataBoth(virtualBox, pMachine, "VBoxInternal2/EfiGraphicsMode", &strTmp);
            if (strTmp.isEmpty())
                GetExtraDataBoth(virtualBox, pMachine, "VBoxInternal2/EfiGopMode", &strTmp);
            if (!strTmp.isEmpty())
                u32GraphicsMode = strTmp.toUInt32();

            /* Get graphics resolution settings, with some sanity checking */
            Utf8Str strResolution;
            GetExtraDataBoth(virtualBox, pMachine, "VBoxInternal2/EfiGraphicsResolution", &strResolution);
            if (!strResolution.isEmpty())
            {
                size_t pos = strResolution.find("x");
                if (pos != strResolution.npos)
                {
                    Utf8Str strH, strV;
                    strH.assignEx(strResolution, 0, pos);
                    strV.assignEx(strResolution, pos+1, strResolution.length()-pos-1);
                    uint32_t u32H = strH.toUInt32();
                    uint32_t u32V = strV.toUInt32();
                    if (u32H == 0 || u32V == 0)
                        strResolution.setNull();
                }
                else
                    strResolution.setNull();
            }
            else
            {
                uint32_t u32H = 0;
                uint32_t u32V = 0;
                GetExtraDataBoth(virtualBox, pMachine, "VBoxInternal2/EfiHorizontalResolution", &strTmp);
                if (strTmp.isEmpty())
                    GetExtraDataBoth(virtualBox, pMachine, "VBoxInternal2/EfiUgaHorizontalResolution", &strTmp);
                if (!strTmp.isEmpty())
                    u32H = strTmp.toUInt32();

                GetExtraDataBoth(virtualBox, pMachine, "VBoxInternal2/EfiVerticalResolution", &strTmp);
                if (strTmp.isEmpty())
                    GetExtraDataBoth(virtualBox, pMachine, "VBoxInternal2/EfiUgaVerticalResolution", &strTmp);
                if (!strTmp.isEmpty())
                    u32V = strTmp.toUInt32();
                if (u32H != 0 && u32V != 0)
                    strResolution = Utf8StrFmt("%ux%u", u32H, u32V);
            }

            /*
             * EFI subtree.
             */
            InsertConfigNode(pDevices, "efi", &pDev);
            InsertConfigNode(pDev,     "0", &pInst);
            InsertConfigInteger(pInst, "Trusted", 1); /* boolean */
            InsertConfigNode(pInst,    "Config", &pCfg);
            InsertConfigInteger(pCfg,  "NumCPUs",     cCpus);
            InsertConfigInteger(pCfg,  "McfgBase",    uMcfgBase);
            InsertConfigInteger(pCfg,  "McfgLength",  cbMcfgLength);
            InsertConfigString(pCfg,   "EfiRom",      pszEfiRomFile);
            InsertConfigString(pCfg,   "BootArgs",    bootArgs);
            InsertConfigString(pCfg,   "DeviceProps", deviceProps);
            InsertConfigInteger(pCfg,  "IOAPIC",      fIOAPIC);
            InsertConfigInteger(pCfg,  "APIC",        uFwAPIC);
            InsertConfigBytes(pCfg,    "UUID", &HardwareUuid,sizeof(HardwareUuid));
            InsertConfigInteger(pCfg,  "UuidLe",      fUuidLe);
            if (fAutoSerialNumGen)
                InsertConfigString(pCfg,  "DmiSystemSerial", "VirtualBox-<DmiSystemUuid>");
            InsertConfigInteger(pCfg,  "64BitEntry",  f64BitEntry); /* boolean */
            InsertConfigString(pCfg,   "NvramFile",   strNvram);
            if (u32GraphicsMode != UINT32_MAX)
                InsertConfigInteger(pCfg,  "GraphicsMode",  u32GraphicsMode);
            if (!strResolution.isEmpty())
                InsertConfigString(pCfg,   "GraphicsResolution", strResolution);

            /* For OS X guests we'll force passing host's DMI info to the guest */
            if (fOsXGuest)
            {
                InsertConfigInteger(pCfg, "DmiUseHostInfo", 1);
                InsertConfigInteger(pCfg, "DmiExposeMemoryTable", 1);
            }

#if defined(VBOX_WITH_TPM)
            if (enmTpmType != TpmType_None)
                InsertConfigInteger(pCfg, "TpmPpiBase", TPM_PPI_MMIO_BASE_DEFAULT);
#endif

            /* Attach the NVRAM storage driver. */
            InsertConfigNode(pInst,    "LUN#0",     &pLunL0);
            InsertConfigString(pLunL0, "Driver",    "NvramStore");
        }

        /*
         * The USB Controllers.
         */
        PCFGMNODE pUsbDevices = NULL;
        vrc = i_configUsb(pMachine, pBusMgr, pRoot, pDevices, aKbdHID, aPointingHID, &pUsbDevices);

        /*
         * Storage controllers.
         */
        bool fFdcEnabled = false;
        vrc = i_configStorageCtrls(pMachine, pBusMgr, pVMM, pUVM,
                                   pDevices, pUsbDevices, pBiosCfg, &fFdcEnabled);           VRC();

        /*
         * Network adapters
         */
        std::list<BootNic> llBootNics;
        vrc = i_configNetworkCtrls(pMachine, platformProperties, chipsetType, pBusMgr,
                                   pVMM, pUVM, pDevices, llBootNics);                        VRC();

        /*
         * Build network boot information and transfer it to the BIOS.
         */
        if (pNetBootCfg && !llBootNics.empty())  /* NetBoot node doesn't exist for EFI! */
        {
            llBootNics.sort();  /* Sort the list by boot priority. */

            char        achBootIdx[] = "0";
            unsigned    uBootIdx = 0;

            for (std::list<BootNic>::iterator it = llBootNics.begin(); it != llBootNics.end(); ++it)
            {
                /* A NIC with priority 0 is only used if it's first in the list. */
                if (it->mBootPrio == 0 && uBootIdx != 0)
                    break;

                PCFGMNODE pNetBtDevCfg;
                achBootIdx[0] = (char)('0' + uBootIdx++);   /* Boot device order. */
                InsertConfigNode(pNetBootCfg, achBootIdx, &pNetBtDevCfg);
                InsertConfigInteger(pNetBtDevCfg, "NIC", it->mInstance);
                InsertConfigInteger(pNetBtDevCfg, "PCIBusNo",      it->mPCIAddress.miBus);
                InsertConfigInteger(pNetBtDevCfg, "PCIDeviceNo",   it->mPCIAddress.miDevice);
                InsertConfigInteger(pNetBtDevCfg, "PCIFunctionNo", it->mPCIAddress.miFn);
            }
        }

        /*
         * Serial (UART) Ports
         */
        /* serial enabled mask to be passed to dev ACPI */
        uint16_t auSerialIoPortBase[SchemaDefs::SerialPortCount] = {0};
        uint8_t auSerialIrq[SchemaDefs::SerialPortCount] = {0};
        InsertConfigNode(pDevices, "serial", &pDev);
        for (ULONG ulInstance = 0; ulInstance < SchemaDefs::SerialPortCount; ++ulInstance)
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

            ULONG ulIRQ;
            hrc = serialPort->COMGETTER(IRQ)(&ulIRQ);                                       H();
            InsertConfigInteger(pCfg, "IRQ", ulIRQ);
            auSerialIrq[ulInstance] = (uint8_t)ulIRQ;

            ULONG ulIOBase;
            hrc = serialPort->COMGETTER(IOAddress)(&ulIOBase);                              H();
            InsertConfigInteger(pCfg, "IOAddress", ulIOBase);
            auSerialIoPortBase[ulInstance] = (uint16_t)ulIOBase;

            BOOL  fServer;
            hrc = serialPort->COMGETTER(Server)(&fServer);                                  H();
            hrc = serialPort->COMGETTER(Path)(bstr.asOutParam());                           H();
            UartType_T eUartType;
            const char *pszUartType;
            hrc = serialPort->COMGETTER(UartType)(&eUartType);                              H();
            switch (eUartType)
            {
                case UartType_U16450: pszUartType = "16450"; break;
                case UartType_U16750: pszUartType = "16750"; break;
                default: AssertFailed(); RT_FALL_THRU();
                case UartType_U16550A: pszUartType = "16550A"; break;
            }
            InsertConfigString(pCfg, "UartType", pszUartType);

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

        /*
         * Parallel (LPT) Ports
         */
        /* parallel enabled mask to be passed to dev ACPI */
        uint16_t auParallelIoPortBase[SchemaDefs::ParallelPortCount] = {0};
        uint8_t auParallelIrq[SchemaDefs::ParallelPortCount] = {0};
        InsertConfigNode(pDevices, "parallel", &pDev);
        for (ULONG ulInstance = 0; ulInstance < SchemaDefs::ParallelPortCount; ++ulInstance)
        {
            ComPtr<IParallelPort> parallelPort;
            hrc = pMachine->GetParallelPort(ulInstance, parallelPort.asOutParam());         H();
            BOOL fEnabledParPort = FALSE;
            if (parallelPort)
            {
                hrc = parallelPort->COMGETTER(Enabled)(&fEnabledParPort);                   H();
            }
            if (!fEnabledParPort)
                continue;

            InsertConfigNode(pDev, Utf8StrFmt("%u", ulInstance).c_str(), &pInst);
            InsertConfigNode(pInst, "Config", &pCfg);

            ULONG ulIRQ;
            hrc = parallelPort->COMGETTER(IRQ)(&ulIRQ);                                     H();
            InsertConfigInteger(pCfg, "IRQ", ulIRQ);
            auParallelIrq[ulInstance] = (uint8_t)ulIRQ;
            ULONG ulIOBase;
            hrc = parallelPort->COMGETTER(IOBase)(&ulIOBase);                               H();
            InsertConfigInteger(pCfg,   "IOBase", ulIOBase);
            auParallelIoPortBase[ulInstance] = (uint16_t)ulIOBase;

            hrc = parallelPort->COMGETTER(Path)(bstr.asOutParam());                         H();
            if (!bstr.isEmpty())
            {
                InsertConfigNode(pInst,     "LUN#0", &pLunL0);
                InsertConfigString(pLunL0,  "Driver", "HostParallel");
                InsertConfigNode(pLunL0,    "Config", &pLunL1);
                InsertConfigString(pLunL1,  "DevicePath", bstr);
            }
        }

        vrc = i_configVmmDev(pMachine, pBusMgr, pDevices);                                      VRC();

        /*
         * Audio configuration.
         */
        bool fAudioEnabled = false;
        vrc = i_configAudioCtrl(virtualBox, pMachine, pBusMgr, pDevices,
                                fOsXGuest, &fAudioEnabled);                                     VRC();

        /*
         * ACPI
         */
        BOOL fACPI;
        hrc = firmwareSettings->COMGETTER(ACPIEnabled)(&fACPI);                                 H();
        if (fACPI)
        {
            /* Always show the CPU leafs when we have multiple VCPUs or when the IO-APIC is enabled.
             * The Windows SMP kernel needs a CPU leaf or else its idle loop will burn cpu cycles; the
             * intelppm driver refuses to register an idle state handler.
             * Always show CPU leafs for OS X guests. */
            BOOL fShowCpu = fOsXGuest;
            if (cCpus > 1 || fIOAPIC)
                fShowCpu = true;

            BOOL fCpuHotPlug;
            hrc = pMachine->COMGETTER(CPUHotPlugEnabled)(&fCpuHotPlug);                     H();

            InsertConfigNode(pDevices, "acpi", &pDev);
            InsertConfigNode(pDev,     "0", &pInst);
            InsertConfigInteger(pInst, "Trusted", 1); /* boolean */
            InsertConfigNode(pInst,    "Config", &pCfg);
            hrc = pBusMgr->assignPCIDevice("acpi", pInst);                                  H();

            InsertConfigInteger(pCfg,  "NumCPUs",          cCpus);

            InsertConfigInteger(pCfg,  "IOAPIC", fIOAPIC);
            InsertConfigInteger(pCfg,  "FdcEnabled", fFdcEnabled);
            InsertConfigInteger(pCfg,  "HpetEnabled", fHPETEnabled);
            InsertConfigInteger(pCfg,  "SmcEnabled", fSmcEnabled);
            InsertConfigInteger(pCfg,  "ShowRtc",    fShowRtc);
            if (fOsXGuest && !llBootNics.empty())
            {
                BootNic aNic = llBootNics.front();
                uint32_t u32NicPCIAddr = (aNic.mPCIAddress.miDevice << 16) | aNic.mPCIAddress.miFn;
                InsertConfigInteger(pCfg, "NicPciAddress",    u32NicPCIAddr);
            }
            if (fOsXGuest && fAudioEnabled)
            {
                PCIBusAddress Address;
                if (pBusMgr->findPCIAddress("hda", 0, Address))
                {
                    uint32_t u32AudioPCIAddr = (Address.miDevice << 16) | Address.miFn;
                    InsertConfigInteger(pCfg, "AudioPciAddress",    u32AudioPCIAddr);
                }
            }
            if (fOsXGuest)
            {
                PCIBusAddress Address;
                if (pBusMgr->findPCIAddress("nvme", 0, Address))
                {
                    uint32_t u32NvmePCIAddr = (Address.miDevice << 16) | Address.miFn;
                    InsertConfigInteger(pCfg, "NvmePciAddress",    u32NvmePCIAddr);
                }
            }
            if (enmIommuType == IommuType_AMD)
            {
                PCIBusAddress Address;
                if (pBusMgr->findPCIAddress("iommu-amd", 0, Address))
                {
                    uint32_t u32IommuAddress = (Address.miDevice << 16) | Address.miFn;
                    InsertConfigInteger(pCfg, "IommuAmdEnabled", true);
                    InsertConfigInteger(pCfg, "IommuPciAddress", u32IommuAddress);
                    if (pBusMgr->findPCIAddress("sb-ioapic", 0, Address))
                    {
                        uint32_t const u32SbIoapicAddress = (Address.miDevice << 16) | Address.miFn;
                        InsertConfigInteger(pCfg, "SbIoApicPciAddress", u32SbIoapicAddress);
                    }
                    else
                        return pVMM->pfnVMR3SetError(pUVM, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                                     N_("AMD IOMMU is enabled, but the I/O APIC is not assigned a PCI address!"));
                }
            }
            else if (enmIommuType == IommuType_Intel)
            {
                PCIBusAddress Address;
                if (pBusMgr->findPCIAddress("iommu-intel", 0, Address))
                {
                    uint32_t u32IommuAddress = (Address.miDevice << 16) | Address.miFn;
                    InsertConfigInteger(pCfg, "IommuIntelEnabled", true);
                    InsertConfigInteger(pCfg, "IommuPciAddress", u32IommuAddress);
                    if (pBusMgr->findPCIAddress("sb-ioapic", 0, Address))
                    {
                        uint32_t const u32SbIoapicAddress = (Address.miDevice << 16) | Address.miFn;
                        InsertConfigInteger(pCfg, "SbIoApicPciAddress", u32SbIoapicAddress);
                    }
                    else
                        return pVMM->pfnVMR3SetError(pUVM, VERR_INVALID_PARAMETER, RT_SRC_POS,
                                                     N_("Intel IOMMU is enabled, but the I/O APIC is not assigned a PCI address!"));
                }
            }

            InsertConfigInteger(pCfg,  "IocPciAddress", uIocPCIAddress);
            if (chipsetType == ChipsetType_ICH9)
            {
                InsertConfigInteger(pCfg,  "McfgBase",   uMcfgBase);
                InsertConfigInteger(pCfg,  "McfgLength", cbMcfgLength);
                /* 64-bit prefetch window root resource: Only for ICH9 and if PAE or Long Mode is enabled (@bugref{5454}). */
                if (fIsGuest64Bit || fEnablePAE)
                    InsertConfigInteger(pCfg,  "PciPref64Enabled", 1);
            }
            InsertConfigInteger(pCfg,  "HostBusPciAddress", uHbcPCIAddress);
            InsertConfigInteger(pCfg,  "ShowCpu", fShowCpu);
            InsertConfigInteger(pCfg,  "CpuHotPlug", fCpuHotPlug);

            InsertConfigInteger(pCfg,  "Serial0IoPortBase", auSerialIoPortBase[0]);
            InsertConfigInteger(pCfg,  "Serial0Irq", auSerialIrq[0]);

            InsertConfigInteger(pCfg,  "Serial1IoPortBase", auSerialIoPortBase[1]);
            InsertConfigInteger(pCfg,  "Serial1Irq", auSerialIrq[1]);

            if (auSerialIoPortBase[2])
            {
                InsertConfigInteger(pCfg,  "Serial2IoPortBase", auSerialIoPortBase[2]);
                InsertConfigInteger(pCfg,  "Serial2Irq", auSerialIrq[2]);
            }

            if (auSerialIoPortBase[3])
            {
                InsertConfigInteger(pCfg,  "Serial3IoPortBase", auSerialIoPortBase[3]);
                InsertConfigInteger(pCfg,  "Serial3Irq", auSerialIrq[3]);
            }

            InsertConfigInteger(pCfg,  "Parallel0IoPortBase", auParallelIoPortBase[0]);
            InsertConfigInteger(pCfg,  "Parallel0Irq", auParallelIrq[0]);

            InsertConfigInteger(pCfg,  "Parallel1IoPortBase", auParallelIoPortBase[1]);
            InsertConfigInteger(pCfg,  "Parallel1Irq", auParallelIrq[1]);

#if defined(VBOX_WITH_TPM)
            switch (enmTpmType)
            {
                case TpmType_v1_2:
                    InsertConfigString(pCfg, "TpmMode", "tis1.2");
                    break;
                case TpmType_v2_0:
                    InsertConfigString(pCfg, "TpmMode", "fifo2.0");
                    break;
                /** @todo Host and swtpm. */
                default:
                    break;
            }
#endif

            InsertConfigNode(pInst,    "LUN#0", &pLunL0);
            InsertConfigString(pLunL0, "Driver",               "ACPIHost");
            InsertConfigNode(pLunL0,   "Config", &pCfg);

            /* Attach the dummy CPU drivers */
            for (ULONG iCpuCurr = 1; iCpuCurr < cCpus; iCpuCurr++)
            {
                BOOL fCpuAttached = true;

                if (fCpuHotPlug)
                {
                    hrc = pMachine->GetCPUStatus(iCpuCurr, &fCpuAttached);                  H();
                }

                if (fCpuAttached)
                {
                    InsertConfigNode(pInst, Utf8StrFmt("LUN#%u", iCpuCurr).c_str(), &pLunL0);
                    InsertConfigString(pLunL0, "Driver",           "ACPICpu");
                    InsertConfigNode(pLunL0,   "Config", &pCfg);
                }
            }
        }

        /*
         * Configure DBGF (Debug(ger) Facility) and DBGC (Debugger Console).
         */
        vrc = i_configGuestDbg(virtualBox, pMachine, pRoot);                                VRC();
    }
    catch (ConfigError &x)
    {
        // InsertConfig threw something:
        pVMM->pfnVMR3SetError(pUVM, x.m_vrc, RT_SRC_POS, "Caught ConfigError: %Rrc - %s", x.m_vrc, x.what());
        return x.m_vrc;
    }
    catch (HRESULT hrcXcpt)
    {
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
