/* $Id: VBoxGuest-win.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxGuest - Windows specifics.
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
 * The contents of this file may alternatively be used under the terms
 * of the Common Development and Distribution License Version 1.0
 * (CDDL), a copy of it is provided in the "COPYING.CDDL" file included
 * in the VirtualBox distribution, in which case the provisions of the
 * CDDL are applicable instead of those of the GPL.
 *
 * You may elect to license modified versions of this file under the
 * terms and conditions of either the GPL or the CDDL or both.
 *
 * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_SUP_DRV
#include <iprt/nt/nt.h>

#include "VBoxGuestInternal.h"
#include <VBox/VBoxGuestLib.h>
#include <VBox/log.h>

#include <iprt/asm.h>
#include <iprt/asm-amd64-x86.h>
#include <iprt/buildconfig.h>
#include <iprt/critsect.h>
#include <iprt/dbg.h>
#include <iprt/err.h>
#include <iprt/initterm.h>
#include <iprt/memobj.h>
#include <iprt/mem.h>
#include <iprt/mp.h>
#include <iprt/spinlock.h>
#include <iprt/string.h>
#include <iprt/utf16.h>

#ifdef TARGET_NT4
# include <VBox/pci.h>
# define PIMAGE_NT_HEADERS32 PIMAGE_NT_HEADERS32_IPRT
# include <iprt/formats/mz.h>
# include <iprt/formats/pecoff.h>
extern "C" IMAGE_DOS_HEADER __ImageBase;
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#undef ExFreePool

#ifndef PCI_MAX_BUSES
# define PCI_MAX_BUSES 256
#endif

/** CM_RESOURCE_MEMORY_* flags which were used on XP or earlier. */
#define VBOX_CM_PRE_VISTA_MASK (0x3f)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Possible device states for our state machine.
 */
typedef enum VGDRVNTDEVSTATE
{
    /** @name Stable states
     * @{ */
    VGDRVNTDEVSTATE_REMOVED = 0,
    VGDRVNTDEVSTATE_STOPPED,
    VGDRVNTDEVSTATE_OPERATIONAL,
    /** @} */

    /** @name Transitional states
     *  @{ */
    VGDRVNTDEVSTATE_PENDINGSTOP,
    VGDRVNTDEVSTATE_PENDINGREMOVE,
    VGDRVNTDEVSTATE_SURPRISEREMOVED
    /** @} */
} VGDRVNTDEVSTATE;


/**
 * Subclassing the device extension for adding windows-specific bits.
 */
typedef struct VBOXGUESTDEVEXTWIN
{
    /** The common device extension core. */
    VBOXGUESTDEVEXT         Core;

    /** Our functional driver object. */
    PDEVICE_OBJECT          pDeviceObject;
    /** Top of the stack. */
    PDEVICE_OBJECT          pNextLowerDriver;

    /** @name PCI bus and slot (device+function) set by for legacy NT only.
     * @{ */
    /** Bus number where the device is located. */
    ULONG                   uBus;
    /** Slot number where the device is located (PCI_SLOT_NUMBER). */
    ULONG                   uSlot;
    /** @} */

    /** @name Interrupt stuff.
     * @{  */
    /** Interrupt object pointer. */
    PKINTERRUPT             pInterruptObject;
    /** Device interrupt level. */
    ULONG                   uInterruptLevel;
    /** Device interrupt vector. */
    ULONG                   uInterruptVector;
    /** Affinity mask. */
    KAFFINITY               fInterruptAffinity;
    /** LevelSensitive or Latched. */
    KINTERRUPT_MODE         enmInterruptMode;
    /** @} */

    /** Physical address and length of VMMDev memory. */
    PHYSICAL_ADDRESS        uVmmDevMemoryPhysAddr;
    /** Length of VMMDev memory.   */
    ULONG                   cbVmmDevMemory;

    /** Device state. */
    VGDRVNTDEVSTATE volatile enmDevState;
    /** The previous stable device state. */
    VGDRVNTDEVSTATE         enmPrevDevState;

    /** Last system power action set (see VBoxGuestPower). */
    POWER_ACTION            enmLastSystemPowerAction;
    /** Preallocated generic request for shutdown. */
    VMMDevPowerStateRequest *pPowerStateRequest;

    /** Spinlock protecting MouseNotifyCallback. Required since the consumer is
     *  in a DPC callback and not the ISR. */
    KSPIN_LOCK              MouseEventAccessSpinLock;

    /** Read/write critical section for handling race between checking for idle
     * driver (in IRP_MN_QUERY_REMOVE_DEVICE & IRP_MN_QUERY_STOP_DEVICE) and
     * creating new sessions.  The session creation code enteres the critical
     * section in  read (shared) access mode, whereas the idle checking code
     * enteres is in write (exclusive) access mode.  */
    RTCRITSECTRW            SessionCreateCritSect;
} VBOXGUESTDEVEXTWIN;
typedef VBOXGUESTDEVEXTWIN *PVBOXGUESTDEVEXTWIN;


/** NT (windows) version identifier. */
typedef enum VGDRVNTVER
{
    VGDRVNTVER_INVALID = 0,
    VGDRVNTVER_WINNT310,
    VGDRVNTVER_WINNT350,
    VGDRVNTVER_WINNT351,
    VGDRVNTVER_WINNT4,
    VGDRVNTVER_WIN2K,
    VGDRVNTVER_WINXP,
    VGDRVNTVER_WIN2K3,
    VGDRVNTVER_WINVISTA,
    VGDRVNTVER_WIN7,
    VGDRVNTVER_WIN8,
    VGDRVNTVER_WIN81,
    VGDRVNTVER_WIN10,
    VGDRVNTVER_WIN11
} VGDRVNTVER;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
RT_C_DECLS_BEGIN
static NTSTATUS NTAPI vgdrvNtNt5PlusAddDevice(PDRIVER_OBJECT pDrvObj, PDEVICE_OBJECT pDevObj);
static NTSTATUS NTAPI vgdrvNtNt5PlusPnP(PDEVICE_OBJECT pDevObj, PIRP pIrp);
static NTSTATUS NTAPI vgdrvNtNt5PlusPower(PDEVICE_OBJECT pDevObj, PIRP pIrp);
static NTSTATUS NTAPI vgdrvNtNt5PlusSystemControl(PDEVICE_OBJECT pDevObj, PIRP pIrp);
static void     NTAPI vgdrvNtUnload(PDRIVER_OBJECT pDrvObj);
static NTSTATUS NTAPI vgdrvNtCreate(PDEVICE_OBJECT pDevObj, PIRP pIrp);
static NTSTATUS NTAPI vgdrvNtClose(PDEVICE_OBJECT pDevObj, PIRP pIrp);
static NTSTATUS NTAPI vgdrvNtDeviceControl(PDEVICE_OBJECT pDevObj, PIRP pIrp);
static NTSTATUS       vgdrvNtDeviceControlSlow(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession,
                                               PIRP pIrp, PIO_STACK_LOCATION pStack);
static NTSTATUS NTAPI vgdrvNtInternalIOCtl(PDEVICE_OBJECT pDevObj, PIRP pIrp);
static void           vgdrvNtReadConfiguration(PVBOXGUESTDEVEXTWIN pDevExt);
static NTSTATUS NTAPI vgdrvNtShutdown(PDEVICE_OBJECT pDevObj, PIRP pIrp);
static NTSTATUS NTAPI vgdrvNtNotSupportedStub(PDEVICE_OBJECT pDevObj, PIRP pIrp);
static VOID     NTAPI vgdrvNtBugCheckCallback(PVOID pvBuffer, ULONG cbBuffer);
static VOID     NTAPI vgdrvNtDpcHandler(PKDPC pDPC, PDEVICE_OBJECT pDevObj, PIRP pIrp, PVOID pContext);
static BOOLEAN  NTAPI vgdrvNtIsrHandler(PKINTERRUPT interrupt, PVOID serviceContext);
#ifdef VBOX_STRICT
static void           vgdrvNtDoTests(void);
#endif
#ifdef TARGET_NT4
static ULONG NTAPI    vgdrvNt31GetBusDataByOffset(BUS_DATA_TYPE enmBusDataType, ULONG idxBus, ULONG uSlot,
                                                  void *pvData, ULONG offData, ULONG cbData);
static ULONG NTAPI    vgdrvNt31SetBusDataByOffset(BUS_DATA_TYPE enmBusDataType, ULONG idxBus, ULONG uSlot,
                                                  void *pvData, ULONG offData, ULONG cbData);
#endif

/*
 * We only do INIT allocations.  PAGE is too much work and risk for little gain.
 */
#ifdef ALLOC_PRAGMA
NTSTATUS DriverEntry(PDRIVER_OBJECT pDrvObj, PUNICODE_STRING pRegPath);
# pragma alloc_text(INIT, DriverEntry)
# ifdef TARGET_NT4
static NTSTATUS       vgdrvNt4CreateDevice(PDRIVER_OBJECT pDrvObj, PUNICODE_STRING pRegPath);
#  pragma alloc_text(INIT, vgdrvNt4CreateDevice)
static NTSTATUS       vgdrvNt4FindPciDevice(PULONG puluBusNumber, PPCI_SLOT_NUMBER puSlotNumber);
#  pragma alloc_text(INIT, vgdrvNt4FindPciDevice)
# endif
#endif
RT_C_DECLS_END


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The detected NT (windows) version. */
static VGDRVNTVER                               g_enmVGDrvNtVer = VGDRVNTVER_INVALID;
/** Pointer to the PoStartNextPowerIrp routine (in the NT kernel).
 * Introduced in Windows 2000. */
static decltype(PoStartNextPowerIrp)           *g_pfnPoStartNextPowerIrp = NULL;
/** Pointer to the PoCallDriver routine (in the NT kernel).
 * Introduced in Windows 2000. */
static decltype(PoCallDriver)                  *g_pfnPoCallDriver = NULL;
#ifdef TARGET_NT4
/** Pointer to the HalAssignSlotResources routine (in the HAL).
 * Introduced in NT 3.50.  */
static decltype(HalAssignSlotResources)        *g_pfnHalAssignSlotResources= NULL;
/** Pointer to the HalGetBusDataByOffset routine (in the HAL).
 * Introduced in NT 3.50.  */
static decltype(HalGetBusDataByOffset)         *g_pfnHalGetBusDataByOffset = NULL;
/** Pointer to the HalSetBusDataByOffset routine (in the HAL).
 * Introduced in NT 3.50 (we provide fallback and use it only for NT 3.1).  */
static decltype(HalSetBusDataByOffset)         *g_pfnHalSetBusDataByOffset = NULL;
#endif
/** Pointer to the KeRegisterBugCheckCallback routine (in the NT kernel).
 * Introduced in Windows 3.50. */
static decltype(KeRegisterBugCheckCallback)    *g_pfnKeRegisterBugCheckCallback = NULL;
/** Pointer to the KeRegisterBugCheckCallback routine (in the NT kernel).
 * Introduced in Windows 3.50. */
static decltype(KeDeregisterBugCheckCallback)  *g_pfnKeDeregisterBugCheckCallback = NULL;
/** Pointer to the KiBugCheckData array (in the NT kernel).
 * Introduced in Windows 4. */
static uintptr_t const                         *g_pauKiBugCheckData = NULL;
/** Set if the callback was successfully registered and needs deregistering.  */
static bool                                     g_fBugCheckCallbackRegistered = false;
/** The bugcheck callback record. */
static KBUGCHECK_CALLBACK_RECORD                g_BugCheckCallbackRec;



/**
 * Driver entry point.
 *
 * @returns appropriate status code.
 * @param   pDrvObj     Pointer to driver object.
 * @param   pRegPath    Registry base path.
 */
NTSTATUS DriverEntry(PDRIVER_OBJECT pDrvObj, PUNICODE_STRING pRegPath)
{
    RT_NOREF1(pRegPath);
#ifdef TARGET_NT4
    /*
     * Looks like NT 3.1 doesn't necessarily zero our uninitialized data segments
     * (like ".bss"), at least not when loading at runtime, so do that.
     */
    PIMAGE_DOS_HEADER   pMzHdr  = &__ImageBase;
    PIMAGE_NT_HEADERS32 pNtHdrs = (PIMAGE_NT_HEADERS32)((uint8_t *)pMzHdr + pMzHdr->e_lfanew);
    if (   pNtHdrs->Signature == IMAGE_NT_SIGNATURE
        && pNtHdrs->FileHeader.NumberOfSections > 2
        && pNtHdrs->FileHeader.NumberOfSections < 64)
    {
        uint32_t                iShdr   = pNtHdrs->FileHeader.NumberOfSections;
        uint32_t                uRvaEnd = pNtHdrs->OptionalHeader.SizeOfImage; /* (may be changed to exclude tail sections) */
        PIMAGE_SECTION_HEADER   paShdrs;
        paShdrs = (PIMAGE_SECTION_HEADER)&pNtHdrs->OptionalHeader.DataDirectory[pNtHdrs->OptionalHeader.NumberOfRvaAndSizes];
        while (iShdr-- > 0)
        {
            if (   !(paShdrs[iShdr].Characteristics & IMAGE_SCN_TYPE_NOLOAD)
                && paShdrs[iShdr].VirtualAddress < uRvaEnd)
            {
                uint32_t const cbSection        = uRvaEnd - paShdrs[iShdr].VirtualAddress;
                uint32_t const offUninitialized = paShdrs[iShdr].SizeOfRawData;
                //RTLogBackdoorPrintf("section #%u: rva=%#x  size=%#x calcsize=%#x) rawsize=%#x\n", iShdr,
                //                    paShdrs[iShdr].VirtualAddress, paShdrs[iShdr].Misc.VirtualSize, cbSection, offUninitialized);
                if (   offUninitialized < cbSection
                    && (paShdrs[iShdr].Characteristics & IMAGE_SCN_MEM_WRITE))
                    memset((uint8_t *)pMzHdr + paShdrs[iShdr].VirtualAddress + offUninitialized, 0, cbSection - offUninitialized);
                uRvaEnd = paShdrs[iShdr].VirtualAddress;
            }
        }
    }
    else
        RTLogBackdoorPrintf("VBoxGuest: Bad pNtHdrs=%p: %#x\n", pNtHdrs, pNtHdrs->Signature);
#endif

    /*
     * Start by initializing IPRT.
     */
    int rc = RTR0Init(0);
    if (RT_FAILURE(rc))
    {
        RTLogBackdoorPrintf("VBoxGuest: RTR0Init failed: %Rrc!\n", rc);
        return STATUS_UNSUCCESSFUL;
    }
    VGDrvCommonInitLoggers();

    LogFunc(("Driver built: %s %s\n", __DATE__, __TIME__));

    /*
     * Check if the NT version is supported and initialize g_enmVGDrvNtVer.
     */
    ULONG ulMajorVer;
    ULONG ulMinorVer;
    ULONG ulBuildNo;
    BOOLEAN fCheckedBuild = PsGetVersion(&ulMajorVer, &ulMinorVer, &ulBuildNo, NULL);

    /* Use RTLogBackdoorPrintf to make sure that this goes to VBox.log on the host. */
    RTLogBackdoorPrintf("VBoxGuest: %s r%s Windows version %u.%u, build %u%s\n",
                        RTBldCfgVersion(), RTBldCfgRevisionStr(), ulMajorVer, ulMinorVer, ulBuildNo,
                        fCheckedBuild ? ", checked build" : "");

#ifdef VBOX_STRICT
    vgdrvNtDoTests();
#endif
    NTSTATUS rcNt = STATUS_SUCCESS;
    switch (ulMajorVer)
    {
        case 10:
            /* Windows 10 Preview builds starting with 9926. */
            g_enmVGDrvNtVer = VGDRVNTVER_WIN10;
            /* Windows 11 Preview builds starting with 22000. */
            if (ulBuildNo >= 22000)
                g_enmVGDrvNtVer = VGDRVNTVER_WIN11;
            break;
        case 6: /* Windows Vista or Windows 7 (based on minor ver) */
            switch (ulMinorVer)
            {
                case 0: /* Note: Also could be Windows 2008 Server! */
                    g_enmVGDrvNtVer = VGDRVNTVER_WINVISTA;
                    break;
                case 1: /* Note: Also could be Windows 2008 Server R2! */
                    g_enmVGDrvNtVer = VGDRVNTVER_WIN7;
                    break;
                case 2:
                    g_enmVGDrvNtVer = VGDRVNTVER_WIN8;
                    break;
                case 3:
                    g_enmVGDrvNtVer = VGDRVNTVER_WIN81;
                    break;
                case 4: /* Windows 10 Preview builds. */
                default:
                    g_enmVGDrvNtVer = VGDRVNTVER_WIN10;
                    break;
            }
            break;
        case 5:
            switch (ulMinorVer)
            {
                default:
                case 2:
                    g_enmVGDrvNtVer = VGDRVNTVER_WIN2K3;
                    break;
                case 1:
                    g_enmVGDrvNtVer = VGDRVNTVER_WINXP;
                    break;
                case 0:
                    g_enmVGDrvNtVer = VGDRVNTVER_WIN2K;
                    break;
            }
            break;
        case 4:
            g_enmVGDrvNtVer = VGDRVNTVER_WINNT4;
            break;
        case 3:
            if (ulMinorVer > 50)
                g_enmVGDrvNtVer = VGDRVNTVER_WINNT351;
            else if (ulMinorVer >= 50)
                g_enmVGDrvNtVer = VGDRVNTVER_WINNT350;
            else
                g_enmVGDrvNtVer = VGDRVNTVER_WINNT310;
            break;
        default:
            /* Major versions above 6 gets classified as windows 10. */
            if (ulMajorVer > 6)
                g_enmVGDrvNtVer = VGDRVNTVER_WIN10;
            else
            {
                RTLogBackdoorPrintf("At least Windows NT 3.10 required! Found %u.%u!\n", ulMajorVer, ulMinorVer);
                rcNt = STATUS_DRIVER_UNABLE_TO_LOAD;
            }
            break;
    }
    if (NT_SUCCESS(rcNt))
    {
        /*
         * Dynamically resolve symbols not present in NT4.
         */
        RTDBGKRNLINFO hKrnlInfo;
        rc = RTR0DbgKrnlInfoOpen(&hKrnlInfo, 0 /*fFlags*/);
        if (RT_SUCCESS(rc))
        {
            g_pfnKeRegisterBugCheckCallback   = (decltype(KeRegisterBugCheckCallback) *)  RTR0DbgKrnlInfoGetSymbol(hKrnlInfo, NULL, "KeRegisterBugCheckCallback");
            g_pfnKeDeregisterBugCheckCallback = (decltype(KeDeregisterBugCheckCallback) *)RTR0DbgKrnlInfoGetSymbol(hKrnlInfo, NULL, "KeDeregisterBugCheckCallback");
            g_pauKiBugCheckData               = (uintptr_t const *)                       RTR0DbgKrnlInfoGetSymbol(hKrnlInfo, NULL, "KiBugCheckData");
            g_pfnPoCallDriver                 = (decltype(PoCallDriver) *)                RTR0DbgKrnlInfoGetSymbol(hKrnlInfo, NULL, "PoCallDriver");
            g_pfnPoStartNextPowerIrp          = (decltype(PoStartNextPowerIrp) *)         RTR0DbgKrnlInfoGetSymbol(hKrnlInfo, NULL, "PoStartNextPowerIrp");
#ifdef TARGET_NT4
            if (g_enmVGDrvNtVer > VGDRVNTVER_WINNT4)
#endif
            {
                if (!g_pfnPoCallDriver)        { LogRelFunc(("Missing PoCallDriver!\n"));        rc = VERR_SYMBOL_NOT_FOUND; }
                if (!g_pfnPoStartNextPowerIrp) { LogRelFunc(("Missing PoStartNextPowerIrp!\n")); rc = VERR_SYMBOL_NOT_FOUND; }
            }

#ifdef TARGET_NT4
            g_pfnHalAssignSlotResources       = (decltype(HalAssignSlotResources) *)RTR0DbgKrnlInfoGetSymbol(hKrnlInfo, NULL, "HalAssignSlotResources");
            if (!g_pfnHalAssignSlotResources && g_enmVGDrvNtVer >= VGDRVNTVER_WINNT350 && g_enmVGDrvNtVer < VGDRVNTVER_WIN2K)
            {
                RTLogBackdoorPrintf("VBoxGuest: Missing HalAssignSlotResources!\n");
                rc = VERR_SYMBOL_NOT_FOUND;
            }

            g_pfnHalGetBusDataByOffset        = (decltype(HalGetBusDataByOffset) *)RTR0DbgKrnlInfoGetSymbol(hKrnlInfo, NULL, "HalGetBusDataByOffset");
            if (!g_pfnHalGetBusDataByOffset && g_enmVGDrvNtVer >= VGDRVNTVER_WINNT350 && g_enmVGDrvNtVer < VGDRVNTVER_WIN2K)
            {
                RTLogBackdoorPrintf("VBoxGuest: Missing HalGetBusDataByOffset!\n");
                rc = VERR_SYMBOL_NOT_FOUND;
            }
            if (!g_pfnHalGetBusDataByOffset)
                g_pfnHalGetBusDataByOffset = vgdrvNt31GetBusDataByOffset;

            g_pfnHalSetBusDataByOffset        = (decltype(HalSetBusDataByOffset) *)RTR0DbgKrnlInfoGetSymbol(hKrnlInfo, NULL, "HalSetBusDataByOffset");
            if (!g_pfnHalSetBusDataByOffset && g_enmVGDrvNtVer >= VGDRVNTVER_WINNT350 && g_enmVGDrvNtVer < VGDRVNTVER_WIN2K)
            {
                RTLogBackdoorPrintf("VBoxGuest: Missing HalSetBusDataByOffset!\n");
                rc = VERR_SYMBOL_NOT_FOUND;
            }
            if (!g_pfnHalSetBusDataByOffset)
                g_pfnHalSetBusDataByOffset = vgdrvNt31SetBusDataByOffset;
#endif
            RTR0DbgKrnlInfoRelease(hKrnlInfo);
        }
        if (RT_SUCCESS(rc))
        {
            /*
             * Setup the driver entry points in pDrvObj.
             */
            pDrvObj->DriverUnload                                  = vgdrvNtUnload;
            pDrvObj->MajorFunction[IRP_MJ_CREATE]                  = vgdrvNtCreate;
            pDrvObj->MajorFunction[IRP_MJ_CLOSE]                   = vgdrvNtClose;
            pDrvObj->MajorFunction[IRP_MJ_DEVICE_CONTROL]          = vgdrvNtDeviceControl;
            pDrvObj->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = vgdrvNtInternalIOCtl;
            /** @todo Need to call IoRegisterShutdownNotification or
             *        IoRegisterLastChanceShutdownNotification, possibly hooking the
             *        HalReturnToFirmware import in NTOSKRNL on older systems (<= ~NT4) and
             *        check for power off requests. */
            pDrvObj->MajorFunction[IRP_MJ_SHUTDOWN]                = vgdrvNtShutdown;
            pDrvObj->MajorFunction[IRP_MJ_READ]                    = vgdrvNtNotSupportedStub;
            pDrvObj->MajorFunction[IRP_MJ_WRITE]                   = vgdrvNtNotSupportedStub;
#ifdef TARGET_NT4
            if (g_enmVGDrvNtVer <= VGDRVNTVER_WINNT4)
                rcNt = vgdrvNt4CreateDevice(pDrvObj, pRegPath);
            else
#endif
            {
                pDrvObj->MajorFunction[IRP_MJ_PNP]                     = vgdrvNtNt5PlusPnP;
                pDrvObj->MajorFunction[IRP_MJ_POWER]                   = vgdrvNtNt5PlusPower;
                pDrvObj->MajorFunction[IRP_MJ_SYSTEM_CONTROL]          = vgdrvNtNt5PlusSystemControl;
                pDrvObj->DriverExtension->AddDevice                    = vgdrvNtNt5PlusAddDevice;
            }
            if (NT_SUCCESS(rcNt))
            {
                /*
                 * Try register the bugcheck callback (non-fatal).
                 */
                if (   g_pfnKeRegisterBugCheckCallback
                    && g_pfnKeDeregisterBugCheckCallback)
                {
                    AssertCompile(BufferEmpty == 0);
                    KeInitializeCallbackRecord(&g_BugCheckCallbackRec);
                    if (g_pfnKeRegisterBugCheckCallback(&g_BugCheckCallbackRec, vgdrvNtBugCheckCallback,
                                                        NULL, 0, (PUCHAR)"VBoxGuest"))
                        g_fBugCheckCallbackRegistered = true;
                    else
                        g_fBugCheckCallbackRegistered = false;
                }
                else
                    Assert(g_pfnKeRegisterBugCheckCallback == NULL && g_pfnKeDeregisterBugCheckCallback == NULL);

                LogFlowFunc(("Returning %#x\n", rcNt));
                return rcNt;
            }
        }
        else
            rcNt = STATUS_PROCEDURE_NOT_FOUND;
    }

    /*
     * Failed.
     */
    LogRelFunc(("Failed! rcNt=%#x\n", rcNt));
    VGDrvCommonDestroyLoggers();
    RTR0Term();
    return rcNt;
}


/**
 * Translates our internal NT version enum to VBox OS.
 *
 * @returns VBox OS type.
 * @param   enmNtVer            The NT version.
 */
static VBOXOSTYPE vgdrvNtVersionToOSType(VGDRVNTVER enmNtVer)
{
    VBOXOSTYPE enmOsType;
    switch (enmNtVer)
    {
        case VGDRVNTVER_WINNT310:   enmOsType = VBOXOSTYPE_WinNT3x; break;
        case VGDRVNTVER_WINNT350:   enmOsType = VBOXOSTYPE_WinNT3x; break;
        case VGDRVNTVER_WINNT351:   enmOsType = VBOXOSTYPE_WinNT3x; break;
        case VGDRVNTVER_WINNT4:     enmOsType = VBOXOSTYPE_WinNT4; break;
        case VGDRVNTVER_WIN2K:      enmOsType = VBOXOSTYPE_Win2k; break;
        case VGDRVNTVER_WINXP:      enmOsType = VBOXOSTYPE_WinXP; break;
        case VGDRVNTVER_WIN2K3:     enmOsType = VBOXOSTYPE_Win2k3; break;
        case VGDRVNTVER_WINVISTA:   enmOsType = VBOXOSTYPE_WinVista; break;
        case VGDRVNTVER_WIN7:       enmOsType = VBOXOSTYPE_Win7; break;
        case VGDRVNTVER_WIN8:       enmOsType = VBOXOSTYPE_Win8; break;
        case VGDRVNTVER_WIN81:      enmOsType = VBOXOSTYPE_Win81; break;
        case VGDRVNTVER_WIN10:      enmOsType = VBOXOSTYPE_Win10; break;
        case VGDRVNTVER_WIN11:      enmOsType = VBOXOSTYPE_Win11_x64; break;

        default:
            /* We don't know, therefore NT family. */
            enmOsType = VBOXOSTYPE_WinNT;
            break;
    }
#if ARCH_BITS == 64
    enmOsType = (VBOXOSTYPE)((int)enmOsType | VBOXOSTYPE_x64);
#endif
    return enmOsType;
}


/**
 * Does the fundamental device extension initialization.
 *
 * @returns NT status.
 * @param   pDevExt             The device extension.
 * @param   pDevObj             The device object.
 */
static NTSTATUS vgdrvNtInitDevExtFundament(PVBOXGUESTDEVEXTWIN pDevExt, PDEVICE_OBJECT pDevObj)
{
    RT_ZERO(*pDevExt);

    KeInitializeSpinLock(&pDevExt->MouseEventAccessSpinLock);
    pDevExt->pDeviceObject   = pDevObj;
    pDevExt->enmPrevDevState = VGDRVNTDEVSTATE_STOPPED;
    pDevExt->enmDevState     = VGDRVNTDEVSTATE_STOPPED;

    int rc = RTCritSectRwInit(&pDevExt->SessionCreateCritSect);
    if (RT_SUCCESS(rc))
    {
        rc = VGDrvCommonInitDevExtFundament(&pDevExt->Core);
        if (RT_SUCCESS(rc))
        {
            LogFlow(("vgdrvNtInitDevExtFundament: returning success\n"));
            return STATUS_SUCCESS;
        }

        RTCritSectRwDelete(&pDevExt->SessionCreateCritSect);
    }
    Log(("vgdrvNtInitDevExtFundament: failed: rc=%Rrc\n", rc));
    return STATUS_UNSUCCESSFUL;
}


/**
 * Counter part to vgdrvNtInitDevExtFundament.
 *
 * @param   pDevExt             The device extension.
 */
static void vgdrvNtDeleteDevExtFundament(PVBOXGUESTDEVEXTWIN pDevExt)
{
    LogFlow(("vgdrvNtDeleteDevExtFundament:\n"));
    VGDrvCommonDeleteDevExtFundament(&pDevExt->Core);
    RTCritSectRwDelete(&pDevExt->SessionCreateCritSect);
}


#ifdef LOG_ENABLED
/**
 * Debug helper to dump a device resource list.
 *
 * @param pResourceList  list of device resources.
 */
static void vgdrvNtShowDeviceResources(PCM_RESOURCE_LIST pRsrcList)
{
    for (uint32_t iList = 0; iList < pRsrcList->Count; iList++)
    {
        PCM_FULL_RESOURCE_DESCRIPTOR pList = &pRsrcList->List[iList];
        LogFunc(("List #%u: InterfaceType=%#x BusNumber=%#x ListCount=%u ListRev=%#x ListVer=%#x\n",
                 iList, pList->InterfaceType, pList->BusNumber, pList->PartialResourceList.Count,
                 pList->PartialResourceList.Revision, pList->PartialResourceList.Version ));

        PCM_PARTIAL_RESOURCE_DESCRIPTOR pResource = pList->PartialResourceList.PartialDescriptors;
        for (ULONG i = 0; i < pList->PartialResourceList.Count; ++i, ++pResource)
        {
            ULONG uType = pResource->Type;
            static char const * const s_apszName[] =
            {
                "CmResourceTypeNull",
                "CmResourceTypePort",
                "CmResourceTypeInterrupt",
                "CmResourceTypeMemory",
                "CmResourceTypeDma",
                "CmResourceTypeDeviceSpecific",
                "CmResourceTypeuBusNumber",
                "CmResourceTypeDevicePrivate",
                "CmResourceTypeAssignedResource",
                "CmResourceTypeSubAllocateFrom",
            };

            if (uType < RT_ELEMENTS(s_apszName))
                LogFunc(("  %.30s Flags=%#x Share=%#x", s_apszName[uType], pResource->Flags, pResource->ShareDisposition));
            else
                LogFunc(("  Type=%#x Flags=%#x Share=%#x", uType, pResource->Flags, pResource->ShareDisposition));
            switch (uType)
            {
                case CmResourceTypePort:
                case CmResourceTypeMemory:
                    Log(("  Start %#RX64, length=%#x\n", pResource->u.Port.Start.QuadPart, pResource->u.Port.Length));
                    break;

                case CmResourceTypeInterrupt:
                    Log(("  Level=%X, vector=%#x, affinity=%#x\n",
                             pResource->u.Interrupt.Level, pResource->u.Interrupt.Vector, pResource->u.Interrupt.Affinity));
                    break;

                case CmResourceTypeDma:
                    Log(("  Channel %d, Port %#x\n", pResource->u.Dma.Channel, pResource->u.Dma.Port));
                    break;

                default:
                    Log(("\n"));
                    break;
            }
        }
    }
}
#endif /* LOG_ENABLED */


/**
 * Helper to scan the PCI resource list and remember stuff.
 *
 * @param   pDevExt         The device extension.
 * @param   pResList        Resource list
 * @param   fTranslated     Whether the addresses are translated or not.
 */
static NTSTATUS vgdrvNtScanPCIResourceList(PVBOXGUESTDEVEXTWIN pDevExt, PCM_RESOURCE_LIST pResList, bool fTranslated)
{
    LogFlowFunc(("Found %d resources\n", pResList->List->PartialResourceList.Count));
    PCM_PARTIAL_RESOURCE_DESCRIPTOR pPartialData = NULL;
    bool                            fGotIrq      = false;
    bool                            fGotMmio     = false;
    bool                            fGotIoPorts  = false;
    NTSTATUS                        rc           = STATUS_SUCCESS;
    for (ULONG i = 0; i < pResList->List->PartialResourceList.Count; i++)
    {
        pPartialData = &pResList->List->PartialResourceList.PartialDescriptors[i];
        switch (pPartialData->Type)
        {
            case CmResourceTypePort:
                LogFlowFunc(("I/O range: Base=%#RX64, length=%08x\n",
                             pPartialData->u.Port.Start.QuadPart, pPartialData->u.Port.Length));
                /* Save the first I/O port base. */
                if (!fGotIoPorts)
                {
                    pDevExt->Core.IOPortBase = (RTIOPORT)pPartialData->u.Port.Start.LowPart;
                    fGotIoPorts = true;
                    LogFunc(("I/O range for VMMDev found! Base=%#RX64, length=%08x\n",
                             pPartialData->u.Port.Start.QuadPart, pPartialData->u.Port.Length));
                }
                else
                    LogRelFunc(("More than one I/O port range?!?\n"));
                break;

            case CmResourceTypeInterrupt:
                LogFunc(("Interrupt: Level=%x, vector=%x, mode=%x\n",
                         pPartialData->u.Interrupt.Level, pPartialData->u.Interrupt.Vector, pPartialData->Flags));
                if (!fGotIrq)
                {
                    /* Save information. */
                    pDevExt->uInterruptLevel    = pPartialData->u.Interrupt.Level;
                    pDevExt->uInterruptVector   = pPartialData->u.Interrupt.Vector;
                    pDevExt->fInterruptAffinity = pPartialData->u.Interrupt.Affinity;

                    /* Check interrupt mode. */
                    if (pPartialData->Flags & CM_RESOURCE_INTERRUPT_LATCHED)
                        pDevExt->enmInterruptMode = Latched;
                    else
                        pDevExt->enmInterruptMode = LevelSensitive;
                    fGotIrq = true;
                    LogFunc(("Interrupt for VMMDev found! Vector=%#x Level=%#x Affinity=%zx Mode=%d\n", pDevExt->uInterruptVector,
                             pDevExt->uInterruptLevel, pDevExt->fInterruptAffinity, pDevExt->enmInterruptMode));
                }
                else
                    LogFunc(("More than one IRQ resource!\n"));
                break;

            case CmResourceTypeMemory:
                LogFlowFunc(("Memory range: Base=%#RX64, length=%08x\n",
                             pPartialData->u.Memory.Start.QuadPart, pPartialData->u.Memory.Length));
                /* We only care about the first read/write memory range. */
                if (   !fGotMmio
                    && (pPartialData->Flags & CM_RESOURCE_MEMORY_WRITEABILITY_MASK) == CM_RESOURCE_MEMORY_READ_WRITE)
                {
                    /* Save physical MMIO base + length for VMMDev. */
                    pDevExt->uVmmDevMemoryPhysAddr = pPartialData->u.Memory.Start;
                    pDevExt->cbVmmDevMemory = (ULONG)pPartialData->u.Memory.Length;

                    if (!fTranslated)
                    {
                        /* Technically we need to make the HAL translate the address.  since we
                           didn't used to do this and it probably just returns the input address,
                           we allow ourselves to ignore failures. */
                        ULONG               uAddressSpace = 0;
                        PHYSICAL_ADDRESS    PhysAddr = pPartialData->u.Memory.Start;
                        if (HalTranslateBusAddress(pResList->List->InterfaceType, pResList->List->BusNumber, PhysAddr,
                                                   &uAddressSpace, &PhysAddr))
                        {
                            Log(("HalTranslateBusAddress(%#RX64) -> %RX64, type %#x\n",
                                 pPartialData->u.Memory.Start.QuadPart, PhysAddr.QuadPart, uAddressSpace));
                            if (pPartialData->u.Memory.Start.QuadPart != PhysAddr.QuadPart)
                                pDevExt->uVmmDevMemoryPhysAddr = PhysAddr;
                        }
                        else
                            Log(("HalTranslateBusAddress(%#RX64) -> failed!\n", pPartialData->u.Memory.Start.QuadPart));
                    }

                    fGotMmio = true;
                    LogFunc(("Found memory range for VMMDev! Base = %#RX64, Length = %08x\n",
                             pPartialData->u.Memory.Start.QuadPart, pPartialData->u.Memory.Length));
                }
                else
                    LogFunc(("Ignoring memory: Flags=%08x Base=%#RX64\n",
                             pPartialData->Flags, pPartialData->u.Memory.Start.QuadPart));
                break;

            default:
                LogFunc(("Unhandled resource found, type=%d\n", pPartialData->Type));
                break;
        }
    }
    return rc;
}


#ifdef TARGET_NT4

/**
 * Scans the PCI resources on NT 3.1.
 *
 * @returns STATUS_SUCCESS or STATUS_DEVICE_CONFIGURATION_ERROR.
 * @param   pDevExt     The device extension.
 * @param   uBus        The bus number.
 * @param   uSlot       The PCI slot to scan.
 */
static NTSTATUS vgdrvNt31ScanSlotResources(PVBOXGUESTDEVEXTWIN pDevExt, ULONG uBus, ULONG uSlot)
{
    /*
     * Disable memory mappings so we can determin the BAR lengths
     * without upsetting other mappings.
     */
    uint16_t fCmd = 0;
    g_pfnHalGetBusDataByOffset(PCIConfiguration, uBus, uSlot, &fCmd, VBOX_PCI_COMMAND, sizeof(fCmd));
    if (fCmd & VBOX_PCI_COMMAND_MEMORY)
    {
        uint16_t fCmdTmp = fCmd & ~VBOX_PCI_COMMAND_MEMORY;
        g_pfnHalSetBusDataByOffset(PCIConfiguration, uBus, uSlot, &fCmdTmp, VBOX_PCI_COMMAND, sizeof(fCmdTmp));
    }

    /*
     * Scan the address resources first.
     */
    uint32_t aBars[6] = { UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX };
    g_pfnHalGetBusDataByOffset(PCIConfiguration, uBus, uSlot, &aBars, VBOX_PCI_BASE_ADDRESS_0, sizeof(aBars));

    bool fGotMmio    = false;
    bool fGotIoPorts = false;
    for (uint32_t i = 0; i < RT_ELEMENTS(aBars); i++)
    {
        uint32_t uBar = aBars[i];
        if (uBar == UINT32_MAX)
            continue;
        if ((uBar & 1) == PCI_ADDRESS_SPACE_IO)
        {
            uint32_t uAddr = uBar & UINT32_C(0xfffffffc);
            if (!uAddr)
                continue;
            if (!fGotIoPorts)
            {
                pDevExt->Core.IOPortBase = (uint16_t)uAddr & UINT16_C(0xfffc);
                fGotIoPorts = true;
                LogFunc(("I/O range for VMMDev found in BAR%u! %#x\n", i, pDevExt->Core.IOPortBase));
            }
            else
                LogRelFunc(("More than one I/O port range?!? BAR%u=%#x\n", i, uBar));
        }
        else
        {
            uint32_t uAddr = uBar & UINT32_C(0xfffffff0);
            if (!uAddr)
                continue;

            if (!fGotMmio)
            {
                /* Figure the length by trying to set all address bits and seeing
                   how many we're allowed to set. */
                uint32_t iBit = 4;
                while (!(uAddr & RT_BIT_32(iBit)))
                    iBit++;

                uint32_t const offPciBar = VBOX_PCI_BASE_ADDRESS_0 + i * 4;
                uint32_t       uTmpBar   = uBar | ((RT_BIT_32(iBit) - 1) & UINT32_C(0xfffffff0));
                g_pfnHalSetBusDataByOffset(PCIConfiguration, uBus, uSlot, &uTmpBar, offPciBar, sizeof(uTmpBar));
                uTmpBar = uBar;
                g_pfnHalGetBusDataByOffset(PCIConfiguration, uBus, uSlot, &uTmpBar, offPciBar, sizeof(uTmpBar));
                g_pfnHalSetBusDataByOffset(PCIConfiguration, uBus, uSlot, &uBar,    offPciBar, sizeof(uBar));

                while (iBit > 4 && (uTmpBar & RT_BIT_32(iBit - 1)))
                    iBit--;

                /* got it */
                pDevExt->cbVmmDevMemory = RT_BIT_32(iBit);
                pDevExt->uVmmDevMemoryPhysAddr.QuadPart = uAddr;
                fGotMmio = true;
                LogFunc(("Found memory range for VMMDev in BAR%u! %#RX64 LB %#x (raw %#x)\n",
                         i, pDevExt->uVmmDevMemoryPhysAddr.QuadPart, pDevExt->cbVmmDevMemory, uBar));
            }
            else
                LogFunc(("Ignoring memory: BAR%u=%#x\n", i, uBar));
        }
    }

    /*
     * Get the IRQ
     */
    struct
    {
        uint8_t bInterruptLine;
        uint8_t bInterruptPin;
    } Buf = { 0, 0 };
    g_pfnHalGetBusDataByOffset(PCIConfiguration, uBus, uSlot, &Buf, VBOX_PCI_INTERRUPT_LINE, sizeof(Buf));
    if (Buf.bInterruptPin != 0)
    {
        pDevExt->uInterruptVector   = Buf.bInterruptLine;
        pDevExt->uInterruptLevel    = Buf.bInterruptLine;
        pDevExt->enmInterruptMode   = LevelSensitive;
        pDevExt->fInterruptAffinity = RT_BIT_32(RTMpGetCount()) - 1;
        LogFunc(("Interrupt for VMMDev found! Vector=%#x Level=%#x Affinity=%zx Mode=%d\n",
                 pDevExt->uInterruptVector, pDevExt->uInterruptLevel, pDevExt->fInterruptAffinity, pDevExt->enmInterruptMode));
    }

    /*
     * Got what we need?
     */
    if (fGotIoPorts && (!fGotMmio || Buf.bInterruptPin != 0))
    {
        /*
         * Enable both MMIO, I/O space and busmastering so we can use the device.
         */
        uint16_t fCmdNew = fCmd | VBOX_PCI_COMMAND_IO | VBOX_PCI_COMMAND_MEMORY | VBOX_PCI_COMMAND_MASTER;
        g_pfnHalSetBusDataByOffset(PCIConfiguration, uBus, uSlot, &fCmdNew, VBOX_PCI_COMMAND, sizeof(fCmdNew));

        return STATUS_SUCCESS;
    }

    /* No. Complain, restore device command value and return failure. */
    if (!fGotIoPorts)
        LogRel(("VBoxGuest: Did not find I/O port range: %#x %#x %#x %#x %#x %#x\n",
                aBars[0], aBars[1], aBars[2], aBars[3], aBars[4], aBars[5]));
    if (!fGotMmio || Buf.bInterruptPin != 0)
        LogRel(("VBoxGuest: Got MMIO but no interrupts!\n"));

    g_pfnHalSetBusDataByOffset(PCIConfiguration, uBus, uSlot, &fCmd, VBOX_PCI_COMMAND, sizeof(fCmd));
    return STATUS_DEVICE_CONFIGURATION_ERROR;
}

#endif /* TARGET_NT4 */

/**
 * Unmaps the VMMDev I/O range from kernel space.
 *
 * @param   pDevExt     The device extension.
 */
static void vgdrvNtUnmapVMMDevMemory(PVBOXGUESTDEVEXTWIN pDevExt)
{
    LogFlowFunc(("pVMMDevMemory = %#x\n", pDevExt->Core.pVMMDevMemory));
    if (pDevExt->Core.pVMMDevMemory)
    {
        MmUnmapIoSpace((void*)pDevExt->Core.pVMMDevMemory, pDevExt->cbVmmDevMemory);
        pDevExt->Core.pVMMDevMemory = NULL;
    }

    pDevExt->uVmmDevMemoryPhysAddr.QuadPart = 0;
    pDevExt->cbVmmDevMemory = 0;
}


/**
 * Maps the I/O space from VMMDev to virtual kernel address space.
 *
 * @return NTSTATUS
 *
 * @param pDevExt           The device extension.
 * @param PhysAddr          Physical address to map.
 * @param cbToMap           Number of bytes to map.
 * @param ppvMMIOBase       Pointer of mapped I/O base.
 * @param pcbMMIO           Length of mapped I/O base.
 */
static NTSTATUS vgdrvNtMapVMMDevMemory(PVBOXGUESTDEVEXTWIN pDevExt, PHYSICAL_ADDRESS PhysAddr, ULONG cbToMap,
                                       void **ppvMMIOBase, uint32_t *pcbMMIO)
{
    AssertPtrReturn(pDevExt, VERR_INVALID_POINTER);
    AssertPtrReturn(ppvMMIOBase, VERR_INVALID_POINTER);
    /* pcbMMIO is optional. */

    NTSTATUS rc = STATUS_SUCCESS;
    if (PhysAddr.LowPart > 0) /* We're mapping below 4GB. */
    {
         VMMDevMemory *pVMMDevMemory = (VMMDevMemory *)MmMapIoSpace(PhysAddr, cbToMap, MmNonCached);
         LogFlowFunc(("pVMMDevMemory = %#x\n", pVMMDevMemory));
         if (pVMMDevMemory)
         {
             LogFunc(("VMMDevMemory: Version = %#x, Size = %d\n", pVMMDevMemory->u32Version, pVMMDevMemory->u32Size));

             /* Check version of the structure; do we have the right memory version? */
             if (pVMMDevMemory->u32Version == VMMDEV_MEMORY_VERSION)
             {
                 /* Save results. */
                 *ppvMMIOBase = pVMMDevMemory;
                 if (pcbMMIO) /* Optional. */
                     *pcbMMIO = pVMMDevMemory->u32Size;

                 LogFlowFunc(("VMMDevMemory found and mapped! pvMMIOBase = 0x%p\n", *ppvMMIOBase));
             }
             else
             {
                 /* Not our version, refuse operation and unmap the memory. */
                 LogFunc(("Wrong version (%u), refusing operation!\n", pVMMDevMemory->u32Version));

                 vgdrvNtUnmapVMMDevMemory(pDevExt);
                 rc = STATUS_UNSUCCESSFUL;
             }
         }
         else
             rc = STATUS_UNSUCCESSFUL;
    }
    return rc;
}


/**
 * Sets up the device and its resources.
 *
 * @param   pDevExt     Our device extension data.
 * @param   pDevObj     The device object.
 * @param   pIrp        The request packet if NT5+, NULL for NT4 and earlier.
 * @param   pDrvObj     The driver object for NT4, NULL for NT5+.
 * @param   pRegPath    The registry path for NT4, NULL for NT5+.
 */
static NTSTATUS vgdrvNtSetupDevice(PVBOXGUESTDEVEXTWIN pDevExt, PDEVICE_OBJECT pDevObj,
                                   PIRP pIrp, PDRIVER_OBJECT pDrvObj, PUNICODE_STRING pRegPath)
{
    LogFlowFunc(("ENTER: pDevExt=%p pDevObj=%p pIrq=%p pDrvObj=%p pRegPath=%p\n", pDevExt, pDevObj, pIrp, pDrvObj, pRegPath));

    NTSTATUS rcNt;
    if (!pIrp)
    {
#ifdef TARGET_NT4
        /*
         * NT4, NT3.x: Let's have a look at what our PCI adapter offers.
         */
        LogFlowFunc(("Starting to scan PCI resources of VBoxGuest ...\n"));

        /* Assign the PCI resources. */
        UNICODE_STRING      ClassName;
        RtlInitUnicodeString(&ClassName, L"VBoxGuestAdapter");
        PCM_RESOURCE_LIST   pResourceList = NULL;
        if (g_pfnHalAssignSlotResources)
        {
            rcNt = g_pfnHalAssignSlotResources(pRegPath, &ClassName, pDrvObj, pDevObj, PCIBus, pDevExt->uBus, pDevExt->uSlot,
                                               &pResourceList);
# ifdef LOG_ENABLED
            if (pResourceList)
                vgdrvNtShowDeviceResources(pResourceList);
# endif
            if (NT_SUCCESS(rcNt))
            {
                rcNt = vgdrvNtScanPCIResourceList(pDevExt, pResourceList, false /*fTranslated*/);
                ExFreePool(pResourceList);
            }
        }
        else
            rcNt = vgdrvNt31ScanSlotResources(pDevExt, pDevExt->uBus, pDevExt->uSlot);

# else  /* !TARGET_NT4 */
        AssertFailed();
        RT_NOREF(pDevObj, pDrvObj, pRegPath);
        rcNt = STATUS_INTERNAL_ERROR;
# endif /* !TARGET_NT4 */
    }
    else
    {
        /*
         * NT5+: Scan the PCI resource list from the IRP.
         */
        PIO_STACK_LOCATION pStack = IoGetCurrentIrpStackLocation(pIrp);
# ifdef LOG_ENABLED
        vgdrvNtShowDeviceResources(pStack->Parameters.StartDevice.AllocatedResourcesTranslated);
# endif
        rcNt = vgdrvNtScanPCIResourceList(pDevExt, pStack->Parameters.StartDevice.AllocatedResourcesTranslated,
                                          true /*fTranslated*/);
    }
    if (NT_SUCCESS(rcNt))
    {
        /*
         * Map physical address of VMMDev memory into MMIO region
         * and init the common device extension bits.
         */
        void    *pvMMIOBase = NULL;
        uint32_t cbMMIO     = 0;
        rcNt = vgdrvNtMapVMMDevMemory(pDevExt,
                                      pDevExt->uVmmDevMemoryPhysAddr,
                                      pDevExt->cbVmmDevMemory,
                                      &pvMMIOBase,
                                      &cbMMIO);
        if (NT_SUCCESS(rcNt))
        {
            pDevExt->Core.pVMMDevMemory = (VMMDevMemory *)pvMMIOBase;

            LogFunc(("pvMMIOBase=0x%p, pDevExt=0x%p, pDevExt->Core.pVMMDevMemory=0x%p\n",
                     pvMMIOBase, pDevExt, pDevExt ? pDevExt->Core.pVMMDevMemory : NULL));

            int vrc = VGDrvCommonInitDevExtResources(&pDevExt->Core,
                                                     pDevExt->Core.IOPortBase,
                                                     NULL /*pvMmioReq*/,
                                                     pvMMIOBase, cbMMIO,
                                                     vgdrvNtVersionToOSType(g_enmVGDrvNtVer),
                                                     VMMDEV_EVENT_MOUSE_POSITION_CHANGED);
            if (RT_SUCCESS(vrc))
            {

                vrc = VbglR0GRAlloc((VMMDevRequestHeader **)&pDevExt->pPowerStateRequest,
                                    sizeof(VMMDevPowerStateRequest), VMMDevReq_SetPowerStatus);
                if (RT_SUCCESS(vrc))
                {
                    /*
                     * Register DPC and ISR.
                     */
                    LogFlowFunc(("Initializing DPC/ISR (pDevObj=%p)...\n", pDevExt->pDeviceObject));
                    IoInitializeDpcRequest(pDevExt->pDeviceObject, vgdrvNtDpcHandler);

                    ULONG uInterruptVector = pDevExt->uInterruptVector;
                    KIRQL uHandlerIrql     = (KIRQL)pDevExt->uInterruptLevel;
#ifdef TARGET_NT4
                    if (!pIrp)
                    {
                        /* NT4: Get an interrupt vector.  Only proceed if the device provides an interrupt. */
                        if (   uInterruptVector
                            || pDevExt->uInterruptLevel)
                        {
                            LogFlowFunc(("Getting interrupt vector (HAL): Bus=%u, IRQL=%u, Vector=%u\n",
                                         pDevExt->uBus, pDevExt->uInterruptLevel, pDevExt->uInterruptVector));
                            uInterruptVector = HalGetInterruptVector(g_enmVGDrvNtVer == VGDRVNTVER_WINNT310 ? Isa : PCIBus,
                                                                     pDevExt->uBus,
                                                                     pDevExt->uInterruptLevel,
                                                                     pDevExt->uInterruptVector,
                                                                     &uHandlerIrql,
                                                                     &pDevExt->fInterruptAffinity);
                            LogFlowFunc(("HalGetInterruptVector returns vector=%u\n", uInterruptVector));
                        }
                        else
                            LogFunc(("Device does not provide an interrupt!\n"));
                    }
#endif
                    if (uInterruptVector)
                    {
                        LogFlowFunc(("Connecting interrupt (IntVector=%#u), uHandlerIrql=%u) ...\n",
                                     uInterruptVector, uHandlerIrql));

                        rcNt = IoConnectInterrupt(&pDevExt->pInterruptObject,                 /* Out: interrupt object. */
                                                  vgdrvNtIsrHandler,                          /* Our ISR handler. */
                                                  pDevExt,                                    /* Device context. */
                                                  NULL,                                       /* Optional spinlock. */
                                                  uInterruptVector,                           /* Interrupt vector. */
                                                  uHandlerIrql,                               /* Irql. */
                                                  uHandlerIrql,                               /* SynchronizeIrql. */
                                                  pDevExt->enmInterruptMode,                  /* LevelSensitive or Latched. */
                                                  TRUE,                                       /* Shareable interrupt. */
                                                  pDevExt->fInterruptAffinity,                /* CPU affinity. */
                                                  FALSE);                                     /* Don't save FPU stack. */
                        if (NT_ERROR(rcNt))
                            LogFunc(("Could not connect interrupt: rcNt=%#x!\n", rcNt));
                    }
                    else
                        LogFunc(("No interrupt vector found!\n"));
                    if (NT_SUCCESS(rcNt))
                    {
                        /*
                         * Once we've read configuration from register and host, we're finally read.
                         */
                        /** @todo clean up guest ring-3 logging, keeping it separate from the kernel to avoid sharing limits with it. */
                        pDevExt->Core.fLoggingEnabled = true;
                        vgdrvNtReadConfiguration(pDevExt);

                        /* Ready to rumble! */
                        LogRelFunc(("Device is ready!\n"));
                        pDevExt->enmDevState     = VGDRVNTDEVSTATE_OPERATIONAL;
                        pDevExt->enmPrevDevState = VGDRVNTDEVSTATE_OPERATIONAL;
                        return STATUS_SUCCESS;
                    }

                    pDevExt->pInterruptObject = NULL;

                    VbglR0GRFree(&pDevExt->pPowerStateRequest->header);
                    pDevExt->pPowerStateRequest = NULL;
                }
                else
                {
                    LogFunc(("Alloc for pPowerStateRequest failed, vrc=%Rrc\n", vrc));
                    rcNt = STATUS_UNSUCCESSFUL;
                }

                VGDrvCommonDeleteDevExtResources(&pDevExt->Core);
            }
            else
            {
                LogFunc(("Could not init device extension resources: vrc=%Rrc\n", vrc));
                rcNt = STATUS_DEVICE_CONFIGURATION_ERROR;
            }
            vgdrvNtUnmapVMMDevMemory(pDevExt);
        }
        else
            LogFunc(("Could not map physical address of VMMDev, rcNt=%#x\n", rcNt));
    }

    LogFunc(("Returned with rcNt=%#x\n", rcNt));
    return rcNt;
}




#ifdef TARGET_NT4
# define PCI_CFG_ADDR   0xcf8
# define PCI_CFG_DATA   0xcfc

/**
 * NT 3.1 doesn't do PCI nor HalSetBusDataByOffset, this is our fallback.
 */
static ULONG NTAPI vgdrvNt31SetBusDataByOffset(BUS_DATA_TYPE enmBusDataType, ULONG idxBus, ULONG uSlot,
                                               void *pvData, ULONG offData, ULONG cbData)
{
    /*
     * Validate input a little bit.
     */
    RT_NOREF(enmBusDataType);
    Assert(idxBus  <= 255);
    Assert(uSlot   <= 255);
    Assert(offData <= 255);
    Assert(cbData > 0);

    PCI_SLOT_NUMBER PciSlot;
    PciSlot.u.AsULONG = uSlot;
    uint32_t const idxAddrTop = UINT32_C(0x80000000)
                              | (idxBus                        << 16)
                              | (PciSlot.u.bits.DeviceNumber   << 11)
                              | (PciSlot.u.bits.FunctionNumber << 8);

    /*
     * Write the given bytes.
     */
    uint8_t const *pbData = (uint8_t const *)pvData;
    uint32_t       off    = offData;
    uint32_t       cbRet  = 0;

    /* Unaligned start. */
    if (off & 3)
    {
        ASMOutU32(PCI_CFG_ADDR, idxAddrTop | (off & ~3));
        switch (off & 3)
        {
            case 1:
                ASMOutU8(PCI_CFG_DATA + 1, pbData[cbRet++]);
                if (cbRet >= cbData)
                    break;
                RT_FALL_THRU();
            case 2:
                ASMOutU8(PCI_CFG_DATA + 2, pbData[cbRet++]);
                if (cbRet >= cbData)
                    break;
                RT_FALL_THRU();
            case 3:
                ASMOutU8(PCI_CFG_DATA + 3, pbData[cbRet++]);
                break;
        }
        off = (off | 3) + 1;
    }

    /* Bulk. */
    while (off < 256 && cbRet < cbData)
    {
        ASMOutU32(PCI_CFG_ADDR, idxAddrTop | off);
        switch (cbData - cbRet)
        {
            case 1:
                ASMOutU8(PCI_CFG_DATA, pbData[cbRet]);
                cbRet += 1;
                break;
            case 2:
                ASMOutU16(PCI_CFG_DATA, RT_MAKE_U16(pbData[cbRet], pbData[cbRet + 1]));
                cbRet += 2;
                break;
            case 3:
                ASMOutU16(PCI_CFG_DATA, RT_MAKE_U16(pbData[cbRet], pbData[cbRet + 1]));
                ASMOutU8(PCI_CFG_DATA + 2, pbData[cbRet + 2]);
                cbRet += 3;
                break;
            default:
                ASMOutU32(PCI_CFG_DATA, RT_MAKE_U32_FROM_U8(pbData[cbRet], pbData[cbRet + 1],
                                                            pbData[cbRet + 2], pbData[cbRet + 3]));
                cbRet += 4;
                break;
        }
        off += 4;
    }

    return cbRet;
}


/**
 * NT 3.1 doesn't do PCI nor HalGetBusDataByOffset, this is our fallback.
 */
static ULONG NTAPI vgdrvNt31GetBusDataByOffset(BUS_DATA_TYPE enmBusDataType, ULONG idxBus, ULONG uSlot,
                                               void *pvData, ULONG offData, ULONG cbData)
{
    /*
     * Validate input a little bit.
     */
    RT_NOREF(enmBusDataType);
    Assert(idxBus  <= 255);
    Assert(uSlot   <= 255);
    Assert(offData <= 255);
    Assert(cbData > 0);

    PCI_SLOT_NUMBER PciSlot;
    PciSlot.u.AsULONG = uSlot;
    uint32_t const idxAddrTop = UINT32_C(0x80000000)
                              | (idxBus                        << 16)
                              | (PciSlot.u.bits.DeviceNumber   << 11)
                              | (PciSlot.u.bits.FunctionNumber << 8);

    /*
     * Read the header type.
     */
    ASMOutU32(PCI_CFG_ADDR,  idxAddrTop | (VBOX_PCI_HEADER_TYPE & ~3));
    uint8_t bHdrType = ASMInU8(PCI_CFG_DATA + (VBOX_PCI_HEADER_TYPE & 3));
    if (bHdrType == 0xff)
        return idxBus < 8 ? 2 : 0; /* No device here */
    if (   offData == VBOX_PCI_HEADER_TYPE
        && cbData == 1)
    {
        *(uint8_t *)pvData = bHdrType;
        /*Log("vgdrvNt31GetBusDataByOffset: PCI %#x/%#x -> %02x\n", idxAddrTop, offData, bHdrType);*/
        return 1;
    }

    /*
     * Read the requested bytes.
     */
    uint8_t *pbData = (uint8_t *)pvData;
    uint32_t off    = offData;
    uint32_t cbRet  = 0;

    /* Unaligned start. */
    if (off & 3)
    {
        ASMOutU32(PCI_CFG_ADDR, idxAddrTop | (off & ~3));
        uint32_t uValue = ASMInU32(PCI_CFG_DATA);
        switch (off & 3)
        {
            case 1:
                pbData[cbRet++] = (uint8_t)(uValue >> 8);
                if (cbRet >= cbData)
                    break;
                RT_FALL_THRU();
            case 2:
                pbData[cbRet++] = (uint8_t)(uValue >> 16);
                if (cbRet >= cbData)
                    break;
                RT_FALL_THRU();
            case 3:
                pbData[cbRet++] = (uint8_t)(uValue >> 24);
                break;
        }
        off = (off | 3) + 1;
    }

    /* Bulk. */
    while (off < 256 && cbRet < cbData)
    {
        ASMOutU32(PCI_CFG_ADDR, idxAddrTop | off);
        uint32_t uValue = ASMInU32(PCI_CFG_DATA);
        switch (cbData - cbRet)
        {
            case 1:
                pbData[cbRet++] = (uint8_t)uValue;
                break;
            case 2:
                pbData[cbRet++] = (uint8_t)uValue;
                pbData[cbRet++] = (uint8_t)(uValue >> 8);
                break;
            case 3:
                pbData[cbRet++] = (uint8_t)uValue;
                pbData[cbRet++] = (uint8_t)(uValue >> 8);
                pbData[cbRet++] = (uint8_t)(uValue >> 16);
                break;
            default:
                pbData[cbRet++] = (uint8_t)uValue;
                pbData[cbRet++] = (uint8_t)(uValue >> 8);
                pbData[cbRet++] = (uint8_t)(uValue >> 16);
                pbData[cbRet++] = (uint8_t)(uValue >> 24);
                break;
        }
        off += 4;
    }

    Log(("vgdrvNt31GetBusDataByOffset: PCI %#x/%#x -> %.*Rhxs\n", idxAddrTop, offData, cbRet, pvData));
    return cbRet;
}


/**
 * Helper function to handle the PCI device lookup.
 *
 * @returns NT status code.
 *
 * @param   puBus           Where to return the bus number on success.
 * @param   pSlot           Where to return the slot number on success.
 */
static NTSTATUS vgdrvNt4FindPciDevice(PULONG puBus, PPCI_SLOT_NUMBER pSlot)
{
    Log(("vgdrvNt4FindPciDevice\n"));

    PCI_SLOT_NUMBER Slot;
    Slot.u.AsULONG = 0;

    /* Scan each bus. */
    for (ULONG uBus = 0; uBus < PCI_MAX_BUSES; uBus++)
    {
        /* Scan each device. */
        for (ULONG idxDevice = 0; idxDevice < PCI_MAX_DEVICES; idxDevice++)
        {
            Slot.u.bits.DeviceNumber   = idxDevice;
            Slot.u.bits.FunctionNumber = 0;

            /* Check the device header. */
            uint8_t bHeaderType = 0xff;
            ULONG cbRet = g_pfnHalGetBusDataByOffset(PCIConfiguration, uBus, Slot.u.AsULONG,
                                                     &bHeaderType, VBOX_PCI_HEADER_TYPE, sizeof(bHeaderType));
            if (cbRet == 0)
                break;
            if (cbRet == 2 || bHeaderType == 0xff)
                continue;

            /* Scan functions. */
            uint32_t const cFunctionStep = bHeaderType & 0x80 ? 1 : 8;
            Log(("vgdrvNt4FindPciDevice: %#x:%#x cFunctionStep=%d bHeaderType=%#x\n", uBus, idxDevice, cFunctionStep, bHeaderType));
            for (ULONG idxFunction = 0; idxFunction < PCI_MAX_FUNCTION; idxFunction += cFunctionStep)
            {
                Slot.u.bits.FunctionNumber = idxFunction;

                /* Read the vendor and device IDs of this device and compare with the VMMDev. */
                struct
                {
                    uint16_t idVendor;
                    uint16_t idDevice;
                } Buf = { PCI_INVALID_VENDORID, PCI_INVALID_VENDORID };
                cbRet = g_pfnHalGetBusDataByOffset(PCIConfiguration, uBus, Slot.u.AsULONG, &Buf, VBOX_PCI_VENDOR_ID, sizeof(Buf));
                if (   cbRet == sizeof(Buf)
                    && Buf.idVendor == VMMDEV_VENDORID
                    && Buf.idDevice == VMMDEV_DEVICEID)
                {
                    /* Hooray, we've found it! */
                    Log(("vgdrvNt4FindPciDevice: Device found! Bus=%#x Slot=%#u (dev %#x, fun %#x, rvd %#x)\n",
                         uBus, Slot.u.AsULONG, Slot.u.bits.DeviceNumber, Slot.u.bits.FunctionNumber, Slot.u.bits.Reserved));

                    *puBus  = uBus;
                    *pSlot  = Slot;
                    return STATUS_SUCCESS;
                }
            }
        }
    }

    return STATUS_DEVICE_DOES_NOT_EXIST;
}


/**
 * Legacy helper function to create the device object.
 *
 * @returns NT status code.
 *
 * @param   pDrvObj         The driver object.
 * @param   pRegPath        The driver registry path.
 */
static NTSTATUS vgdrvNt4CreateDevice(PDRIVER_OBJECT pDrvObj, PUNICODE_STRING pRegPath)
{
    Log(("vgdrvNt4CreateDevice: pDrvObj=%p, pRegPath=%p\n", pDrvObj, pRegPath));

    /*
     * Find our virtual PCI device
     */
    ULONG           uBus;
    PCI_SLOT_NUMBER uSlot;
    NTSTATUS rc = vgdrvNt4FindPciDevice(&uBus, &uSlot);
    if (NT_ERROR(rc))
    {
        Log(("vgdrvNt4CreateDevice: Device not found!\n"));
        return rc;
    }

    /*
     * Create device.
     */
    UNICODE_STRING DevName;
    RtlInitUnicodeString(&DevName, VBOXGUEST_DEVICE_NAME_NT);
    PDEVICE_OBJECT pDeviceObject = NULL;
    rc = IoCreateDevice(pDrvObj, sizeof(VBOXGUESTDEVEXTWIN), &DevName, FILE_DEVICE_UNKNOWN, 0, FALSE, &pDeviceObject);
    if (NT_SUCCESS(rc))
    {
        Log(("vgdrvNt4CreateDevice: Device created\n"));

        UNICODE_STRING DosName;
        RtlInitUnicodeString(&DosName, VBOXGUEST_DEVICE_NAME_DOS);
        rc = IoCreateSymbolicLink(&DosName, &DevName);
        if (NT_SUCCESS(rc))
        {
            Log(("vgdrvNt4CreateDevice: Symlink created\n"));

            /*
             * Setup the device extension.
             */
            Log(("vgdrvNt4CreateDevice: Setting up device extension ...\n"));
            PVBOXGUESTDEVEXTWIN pDevExt = (PVBOXGUESTDEVEXTWIN)pDeviceObject->DeviceExtension;
            int vrc = vgdrvNtInitDevExtFundament(pDevExt, pDeviceObject);
            if (RT_SUCCESS(vrc))
            {
                /* Store bus and slot number we've queried before. */
                pDevExt->uBus  = uBus;
                pDevExt->uSlot = uSlot.u.AsULONG;

                Log(("vgdrvNt4CreateDevice: Device extension created\n"));

                /* Do the actual VBox init ... */
                rc = vgdrvNtSetupDevice(pDevExt, pDeviceObject, NULL /*pIrp*/, pDrvObj, pRegPath);
                if (NT_SUCCESS(rc))
                {
                    Log(("vgdrvNt4CreateDevice: Returning rc = 0x%x (success)\n", rc));
                    return rc;
                }

                /* bail out */
                vgdrvNtDeleteDevExtFundament(pDevExt);
            }
            IoDeleteSymbolicLink(&DosName);
        }
        else
            Log(("vgdrvNt4CreateDevice: IoCreateSymbolicLink failed with rc = %#x\n", rc));
        IoDeleteDevice(pDeviceObject);
    }
    else
        Log(("vgdrvNt4CreateDevice: IoCreateDevice failed with rc = %#x\n", rc));
    Log(("vgdrvNt4CreateDevice: Returning rc = 0x%x\n", rc));
    return rc;
}

#endif /* TARGET_NT4 */

/**
 * Handle request from the Plug & Play subsystem.
 *
 * @returns NT status code
 * @param   pDrvObj   Driver object
 * @param   pDevObj   Device object
 *
 * @remarks Parts of this is duplicated in VBoxGuest-win-legacy.cpp.
 */
static NTSTATUS NTAPI vgdrvNtNt5PlusAddDevice(PDRIVER_OBJECT pDrvObj, PDEVICE_OBJECT pDevObj)
{
    LogFlowFuncEnter();

    /*
     * Create device.
     */
    UNICODE_STRING DevName;
    RtlInitUnicodeString(&DevName, VBOXGUEST_DEVICE_NAME_NT);
    PDEVICE_OBJECT pDeviceObject = NULL;
    NTSTATUS rcNt = IoCreateDevice(pDrvObj, sizeof(VBOXGUESTDEVEXTWIN), &DevName, FILE_DEVICE_UNKNOWN, 0, FALSE, &pDeviceObject);
    if (NT_SUCCESS(rcNt))
    {
        /*
         * Create symbolic link (DOS devices).
         */
        UNICODE_STRING DosName;
        RtlInitUnicodeString(&DosName, VBOXGUEST_DEVICE_NAME_DOS);
        rcNt = IoCreateSymbolicLink(&DosName, &DevName);
        if (NT_SUCCESS(rcNt))
        {
            /*
             * Setup the device extension.
             */
            PVBOXGUESTDEVEXTWIN pDevExt = (PVBOXGUESTDEVEXTWIN)pDeviceObject->DeviceExtension;
            rcNt = vgdrvNtInitDevExtFundament(pDevExt, pDeviceObject);
            if (NT_SUCCESS(rcNt))
            {
                pDevExt->pNextLowerDriver = IoAttachDeviceToDeviceStack(pDeviceObject, pDevObj);
                if (pDevExt->pNextLowerDriver != NULL)
                {
                    /* Ensure we are not called at elevated IRQL, even if our code isn't pagable any more. */
                    pDeviceObject->Flags |= DO_POWER_PAGABLE;

                    /* Driver is ready now. */
                    pDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
                    LogFlowFunc(("Returning with rcNt=%#x (success)\n", rcNt));
                    return rcNt;
                }
                LogFunc(("IoAttachDeviceToDeviceStack did not give a nextLowerDriver!\n"));
                rcNt = STATUS_DEVICE_NOT_CONNECTED;
                vgdrvNtDeleteDevExtFundament(pDevExt);
            }

            IoDeleteSymbolicLink(&DosName);
        }
        else
            LogFunc(("IoCreateSymbolicLink failed with rcNt=%#x!\n", rcNt));
        IoDeleteDevice(pDeviceObject);
    }
    else
        LogFunc(("IoCreateDevice failed with rcNt=%#x!\n", rcNt));

    LogFunc(("Returning with rcNt=%#x\n", rcNt));
    return rcNt;
}


/**
 * Irp completion routine for PnP Irps we send.
 *
 * @returns NT status code.
 * @param   pDevObj   Device object.
 * @param   pIrp      Request packet.
 * @param   pEvent    Semaphore.
 */
static NTSTATUS vgdrvNt5PlusPnpIrpComplete(PDEVICE_OBJECT pDevObj, PIRP pIrp, PKEVENT pEvent)
{
    RT_NOREF2(pDevObj, pIrp);
    KeSetEvent(pEvent, 0, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}


/**
 * Helper to send a PnP IRP and wait until it's done.
 *
 * @returns NT status code.
 * @param    pDevObj    Device object.
 * @param    pIrp       Request packet.
 * @param    fStrict    When set, returns an error if the IRP gives an error.
 */
static NTSTATUS vgdrvNt5PlusPnPSendIrpSynchronously(PDEVICE_OBJECT pDevObj, PIRP pIrp, BOOLEAN fStrict)
{
    KEVENT Event;

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    IoCopyCurrentIrpStackLocationToNext(pIrp);
    IoSetCompletionRoutine(pIrp, (PIO_COMPLETION_ROUTINE)vgdrvNt5PlusPnpIrpComplete, &Event, TRUE, TRUE, TRUE);

    NTSTATUS rcNt = IoCallDriver(pDevObj, pIrp);
    if (rcNt == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        rcNt = pIrp->IoStatus.Status;
    }

    if (   !fStrict
        && (rcNt == STATUS_NOT_SUPPORTED || rcNt == STATUS_INVALID_DEVICE_REQUEST))
    {
        rcNt = STATUS_SUCCESS;
    }

    Log(("vgdrvNt5PlusPnPSendIrpSynchronously: Returning %#x\n", rcNt));
    return rcNt;
}


/**
 * Deletes the device hardware resources.
 *
 * Used during removal, stopping and legacy module unloading.
 *
 * @param   pDevExt         The device extension.
 */
static void vgdrvNtDeleteDeviceResources(PVBOXGUESTDEVEXTWIN pDevExt)
{
    if (pDevExt->pInterruptObject)
    {
        IoDisconnectInterrupt(pDevExt->pInterruptObject);
        pDevExt->pInterruptObject = NULL;
    }
    pDevExt->pPowerStateRequest = NULL; /* Will be deleted by the following call. */
    if (pDevExt->Core.uInitState == VBOXGUESTDEVEXT_INIT_STATE_RESOURCES)
        VGDrvCommonDeleteDevExtResources(&pDevExt->Core);
    vgdrvNtUnmapVMMDevMemory(pDevExt);
}


/**
 * Deletes the device extension fundament and unlinks the device
 *
 * Used during removal and legacy module unloading.  Must have called
 * vgdrvNtDeleteDeviceResources.
 *
 * @param   pDevObj         Device object.
 * @param   pDevExt         The device extension.
 */
static void vgdrvNtDeleteDeviceFundamentAndUnlink(PDEVICE_OBJECT pDevObj, PVBOXGUESTDEVEXTWIN pDevExt)
{
    /*
     * Delete the remainder of the device extension.
     */
    vgdrvNtDeleteDevExtFundament(pDevExt);

    /*
     * Delete the DOS symlink to the device and finally the device itself.
     */
    UNICODE_STRING DosName;
    RtlInitUnicodeString(&DosName, VBOXGUEST_DEVICE_NAME_DOS);
    IoDeleteSymbolicLink(&DosName);

    Log(("vgdrvNtDeleteDeviceFundamentAndUnlink: Deleting device ...\n"));
    IoDeleteDevice(pDevObj);
}


/**
 * Checks if the device is idle.
 * @returns STATUS_SUCCESS if idle, STATUS_UNSUCCESSFUL if busy.
 * @param   pDevExt             The device extension.
 * @param   pszQueryNm          The query name.
 */
static NTSTATUS vgdrvNtCheckIdle(PVBOXGUESTDEVEXTWIN pDevExt, const char *pszQueryNm)
{
    uint32_t cSessions = pDevExt->Core.cSessions;
    if (cSessions == 0)
        return STATUS_SUCCESS;
    LogRel(("vgdrvNtCheckIdle/%s: cSessions=%d\n", pszQueryNm, cSessions));
    return STATUS_UNSUCCESSFUL;
}


/**
 * PnP Request handler.
 *
 * @param  pDevObj    Device object.
 * @param  pIrp       Request packet.
 */
static NTSTATUS NTAPI vgdrvNtNt5PlusPnP(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    PVBOXGUESTDEVEXTWIN pDevExt = (PVBOXGUESTDEVEXTWIN)pDevObj->DeviceExtension;
    PIO_STACK_LOCATION  pStack  = IoGetCurrentIrpStackLocation(pIrp);

#ifdef LOG_ENABLED
    static char const * const s_apszFnctName[] =
    {
        "IRP_MN_START_DEVICE",
        "IRP_MN_QUERY_REMOVE_DEVICE",
        "IRP_MN_REMOVE_DEVICE",
        "IRP_MN_CANCEL_REMOVE_DEVICE",
        "IRP_MN_STOP_DEVICE",
        "IRP_MN_QUERY_STOP_DEVICE",
        "IRP_MN_CANCEL_STOP_DEVICE",
        "IRP_MN_QUERY_DEVICE_RELATIONS",
        "IRP_MN_QUERY_INTERFACE",
        "IRP_MN_QUERY_CAPABILITIES",
        "IRP_MN_QUERY_RESOURCES",
        "IRP_MN_QUERY_RESOURCE_REQUIREMENTS",
        "IRP_MN_QUERY_DEVICE_TEXT",
        "IRP_MN_FILTER_RESOURCE_REQUIREMENTS",
        "IRP_MN_0xE",
        "IRP_MN_READ_CONFIG",
        "IRP_MN_WRITE_CONFIG",
        "IRP_MN_EJECT",
        "IRP_MN_SET_LOCK",
        "IRP_MN_QUERY_ID",
        "IRP_MN_QUERY_PNP_DEVICE_STATE",
        "IRP_MN_QUERY_BUS_INFORMATION",
        "IRP_MN_DEVICE_USAGE_NOTIFICATION",
        "IRP_MN_SURPRISE_REMOVAL",
    };
    Log(("vgdrvNtNt5PlusPnP: MinorFunction: %s\n",
         pStack->MinorFunction < RT_ELEMENTS(s_apszFnctName) ? s_apszFnctName[pStack->MinorFunction] : "Unknown"));
#endif

    NTSTATUS rc = STATUS_SUCCESS;
    uint8_t bMinorFunction = pStack->MinorFunction;
    switch (bMinorFunction)
    {
        case IRP_MN_START_DEVICE:
        {
            Log(("vgdrvNtNt5PlusPnP: START_DEVICE\n"));

            /* This must be handled first by the lower driver. */
            rc = vgdrvNt5PlusPnPSendIrpSynchronously(pDevExt->pNextLowerDriver, pIrp, TRUE);
            if (   NT_SUCCESS(rc)
                && NT_SUCCESS(pIrp->IoStatus.Status))
            {
                Log(("vgdrvNtNt5PlusPnP: START_DEVICE: pStack->Parameters.StartDevice.AllocatedResources = %p\n",
                     pStack->Parameters.StartDevice.AllocatedResources));
                if (pStack->Parameters.StartDevice.AllocatedResources)
                {
                    rc = vgdrvNtSetupDevice(pDevExt, pDevObj, pIrp, NULL, NULL);
                    if (NT_SUCCESS(rc))
                        Log(("vgdrvNtNt5PlusPnP: START_DEVICE: success\n"));
                    else
                        Log(("vgdrvNtNt5PlusPnP: START_DEVICE: vgdrvNtSetupDevice failed: %#x\n", rc));
                }
                else
                {
                    Log(("vgdrvNtNt5PlusPnP: START_DEVICE: No resources, pDevExt = %p, nextLowerDriver = %p!\n",
                         pDevExt, pDevExt ? pDevExt->pNextLowerDriver : NULL));
                    rc = STATUS_UNSUCCESSFUL;
                }
            }
            else
                Log(("vgdrvNtNt5PlusPnP: START_DEVICE: vgdrvNt5PlusPnPSendIrpSynchronously failed: %#x + %#x\n",
                     rc, pIrp->IoStatus.Status));

            pIrp->IoStatus.Status = rc;
            IoCompleteRequest(pIrp, IO_NO_INCREMENT);
            return rc;
        }


        /*
         * Sent before removing the device and/or driver.
         */
        case IRP_MN_QUERY_REMOVE_DEVICE:
        {
            Log(("vgdrvNtNt5PlusPnP: QUERY_REMOVE_DEVICE\n"));

            RTCritSectRwEnterExcl(&pDevExt->SessionCreateCritSect);
#ifdef VBOX_REBOOT_ON_UNINSTALL
            Log(("vgdrvNtNt5PlusPnP: QUERY_REMOVE_DEVICE: Device cannot be removed without a reboot.\n"));
            rc = STATUS_UNSUCCESSFUL;
#endif
            if (NT_SUCCESS(rc))
                rc = vgdrvNtCheckIdle(pDevExt, "QUERY_REMOVE_DEVICE");
            if (NT_SUCCESS(rc))
            {
                pDevExt->enmDevState = VGDRVNTDEVSTATE_PENDINGREMOVE;
                RTCritSectRwLeaveExcl(&pDevExt->SessionCreateCritSect);

                /* This IRP passed down to lower driver. */
                pIrp->IoStatus.Status = STATUS_SUCCESS;

                IoSkipCurrentIrpStackLocation(pIrp);
                rc = IoCallDriver(pDevExt->pNextLowerDriver, pIrp);
                Log(("vgdrvNtNt5PlusPnP: QUERY_REMOVE_DEVICE: Next lower driver replied rc = 0x%x\n", rc));

                /* We must not do anything the IRP after doing IoSkip & CallDriver
                   since the driver below us will complete (or already have completed) the IRP.
                   I.e. just return the status we got from IoCallDriver */
            }
            else
            {
                RTCritSectRwLeaveExcl(&pDevExt->SessionCreateCritSect);
                pIrp->IoStatus.Status = rc;
                IoCompleteRequest(pIrp, IO_NO_INCREMENT);
            }

            Log(("vgdrvNtNt5PlusPnP: QUERY_REMOVE_DEVICE: Returning with rc = 0x%x\n", rc));
            return rc;
        }

        /*
         * Cancels a pending remove, IRP_MN_QUERY_REMOVE_DEVICE.
         * We only have to revert the state.
         */
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        {
            Log(("vgdrvNtNt5PlusPnP: CANCEL_REMOVE_DEVICE\n"));

            /* This must be handled first by the lower driver. */
            rc = vgdrvNt5PlusPnPSendIrpSynchronously(pDevExt->pNextLowerDriver, pIrp, TRUE);
            if (   NT_SUCCESS(rc)
                && pDevExt->enmDevState == VGDRVNTDEVSTATE_PENDINGREMOVE)
            {
                /* Return to the state prior to receiving the IRP_MN_QUERY_REMOVE_DEVICE request. */
                pDevExt->enmDevState = pDevExt->enmPrevDevState;
            }

            /* Complete the IRP. */
            pIrp->IoStatus.Status = rc;
            IoCompleteRequest(pIrp, IO_NO_INCREMENT);
            return rc;
        }

        /*
         * We do nothing here actually, esp. since this request is not expected for VBoxGuest.
         * The cleanup will be done in IRP_MN_REMOVE_DEVICE, which follows this call.
         */
        case IRP_MN_SURPRISE_REMOVAL:
        {
            Log(("vgdrvNtNt5PlusPnP: IRP_MN_SURPRISE_REMOVAL\n"));
            pDevExt->enmDevState = VGDRVNTDEVSTATE_SURPRISEREMOVED;
            LogRel(("VBoxGuest: unexpected device removal\n"));

            /* Pass to the lower driver. */
            pIrp->IoStatus.Status = STATUS_SUCCESS;

            IoSkipCurrentIrpStackLocation(pIrp);
            rc = IoCallDriver(pDevExt->pNextLowerDriver, pIrp);

            /* Do not complete the IRP. */
            return rc;
        }

        /*
         * Device and/or driver removal.  Destroy everything.
         */
        case IRP_MN_REMOVE_DEVICE:
        {
            Log(("vgdrvNtNt5PlusPnP: REMOVE_DEVICE\n"));
            pDevExt->enmDevState = VGDRVNTDEVSTATE_REMOVED;

            /*
             * Disconnect interrupts and delete all hardware resources.
             * Note! This may already have been done if we're STOPPED already, if that's a possibility.
             */
            vgdrvNtDeleteDeviceResources(pDevExt);

            /*
             * We need to send the remove down the stack before we detach, but we don't need
             * to wait for the completion of this operation (nor register a completion routine).
             */
            pIrp->IoStatus.Status = STATUS_SUCCESS;

            IoSkipCurrentIrpStackLocation(pIrp);
            rc = IoCallDriver(pDevExt->pNextLowerDriver, pIrp);
            Log(("vgdrvNtNt5PlusPnP: REMOVE_DEVICE: Next lower driver replied rc = 0x%x\n", rc));

            IoDetachDevice(pDevExt->pNextLowerDriver);
            Log(("vgdrvNtNt5PlusPnP: REMOVE_DEVICE: Removing device ...\n"));

            /*
             * Delete the remainder of the device extension data, unlink it from the namespace and delete it.
             */
            vgdrvNtDeleteDeviceFundamentAndUnlink(pDevObj, pDevExt);

            pDevObj = NULL; /* invalid */
            pDevExt = NULL; /* invalid */

            Log(("vgdrvNtNt5PlusPnP: REMOVE_DEVICE: Device removed!\n"));
            return rc; /* Propagating rc from IoCallDriver. */
        }


        /*
         * Sent before stopping the device/driver to check whether it is okay to do so.
         */
        case IRP_MN_QUERY_STOP_DEVICE:
        {
            Log(("vgdrvNtNt5PlusPnP: QUERY_STOP_DEVICE\n"));
            RTCritSectRwEnterExcl(&pDevExt->SessionCreateCritSect);
            rc = vgdrvNtCheckIdle(pDevExt, "QUERY_STOP_DEVICE");
            if (NT_SUCCESS(rc))
            {
                pDevExt->enmPrevDevState = pDevExt->enmDevState;
                pDevExt->enmDevState = VGDRVNTDEVSTATE_PENDINGSTOP;
                RTCritSectRwLeaveExcl(&pDevExt->SessionCreateCritSect);

                /* This IRP passed down to lower driver. */
                pIrp->IoStatus.Status = STATUS_SUCCESS;

                IoSkipCurrentIrpStackLocation(pIrp);

                rc = IoCallDriver(pDevExt->pNextLowerDriver, pIrp);
                Log(("vgdrvNtNt5PlusPnP: QUERY_STOP_DEVICE: Next lower driver replied rc = 0x%x\n", rc));

                /* we must not do anything with the IRP after doing IoSkip & CallDriver since the
                   driver below us will complete (or already have completed) the IRP.  I.e. just
                   return the status we got from IoCallDriver. */
            }
            else
            {
                RTCritSectRwLeaveExcl(&pDevExt->SessionCreateCritSect);
                pIrp->IoStatus.Status = rc;
                IoCompleteRequest(pIrp, IO_NO_INCREMENT);
            }

            Log(("vgdrvNtNt5PlusPnP: QUERY_STOP_DEVICE: Returning with rc = 0x%x\n", rc));
            return rc;
        }

        /*
         * Cancels a pending remove, IRP_MN_QUERY_STOP_DEVICE.
         * We only have to revert the state.
         */
        case IRP_MN_CANCEL_STOP_DEVICE:
        {
            Log(("vgdrvNtNt5PlusPnP: CANCEL_STOP_DEVICE\n"));

            /* This must be handled first by the lower driver. */
            rc = vgdrvNt5PlusPnPSendIrpSynchronously(pDevExt->pNextLowerDriver, pIrp, TRUE);
            if (   NT_SUCCESS(rc)
                && pDevExt->enmDevState == VGDRVNTDEVSTATE_PENDINGSTOP)
            {
                /* Return to the state prior to receiving the IRP_MN_QUERY_STOP_DEVICE request. */
                pDevExt->enmDevState = pDevExt->enmPrevDevState;
            }

            /* Complete the IRP. */
            pIrp->IoStatus.Status = rc;
            IoCompleteRequest(pIrp, IO_NO_INCREMENT);
            return rc;
        }

        /*
         * Stop the device.
         */
        case IRP_MN_STOP_DEVICE:
        {
            Log(("vgdrvNtNt5PlusPnP: STOP_DEVICE\n"));
            pDevExt->enmDevState = VGDRVNTDEVSTATE_STOPPED;

            /*
             * Release the hardware resources.
             */
            vgdrvNtDeleteDeviceResources(pDevExt);

            /*
             * Pass the request to the lower driver.
             */
            pIrp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(pIrp);
            rc = IoCallDriver(pDevExt->pNextLowerDriver, pIrp);
            Log(("vgdrvNtNt5PlusPnP: STOP_DEVICE: Next lower driver replied rc = 0x%x\n", rc));
            return rc;
        }

        default:
        {
            IoSkipCurrentIrpStackLocation(pIrp);
            rc = IoCallDriver(pDevExt->pNextLowerDriver, pIrp);
            Log(("vgdrvNtNt5PlusPnP: Unknown request %#x: Lower driver replied: %x\n", bMinorFunction, rc));
            return rc;
        }
    }
}


/**
 * Handle the power completion event.
 *
 * @returns NT status code.
 * @param pDevObj   Targetted device object.
 * @param pIrp      IO request packet.
 * @param pContext  Context value passed to IoSetCompletionRoutine in VBoxGuestPower.
 */
static NTSTATUS vgdrvNtNt5PlusPowerComplete(IN PDEVICE_OBJECT pDevObj, IN PIRP pIrp, IN PVOID pContext)
{
#ifdef VBOX_STRICT
    RT_NOREF1(pDevObj);
    PVBOXGUESTDEVEXTWIN pDevExt = (PVBOXGUESTDEVEXTWIN)pContext;
    PIO_STACK_LOCATION  pIrpSp  = IoGetCurrentIrpStackLocation(pIrp);

    Assert(pDevExt);

    if (pIrpSp)
    {
        Assert(pIrpSp->MajorFunction == IRP_MJ_POWER);
        if (NT_SUCCESS(pIrp->IoStatus.Status))
        {
            switch (pIrpSp->MinorFunction)
            {
                case IRP_MN_SET_POWER:
                    switch (pIrpSp->Parameters.Power.Type)
                    {
                        case DevicePowerState:
                            switch (pIrpSp->Parameters.Power.State.DeviceState)
                            {
                                case PowerDeviceD0:
                                    break;
                                default:  /* Shut up MSC */
                                    break;
                            }
                            break;
                        default: /* Shut up MSC */
                            break;
                    }
                    break;
            }
        }
    }
#else
    RT_NOREF3(pDevObj, pIrp, pContext);
#endif

    return STATUS_SUCCESS;
}


/**
 * Handle the Power requests.
 *
 * @returns   NT status code
 * @param     pDevObj   device object
 * @param     pIrp      IRP
 */
static NTSTATUS NTAPI vgdrvNtNt5PlusPower(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    PIO_STACK_LOCATION  pStack   = IoGetCurrentIrpStackLocation(pIrp);
    PVBOXGUESTDEVEXTWIN pDevExt  = (PVBOXGUESTDEVEXTWIN)pDevObj->DeviceExtension;
    POWER_STATE_TYPE    enmPowerType   = pStack->Parameters.Power.Type;
    POWER_STATE         PowerState     = pStack->Parameters.Power.State;
    POWER_ACTION        enmPowerAction = pStack->Parameters.Power.ShutdownType;

    Log(("vgdrvNtNt5PlusPower:\n"));

    switch (pStack->MinorFunction)
    {
        case IRP_MN_SET_POWER:
        {
            Log(("vgdrvNtNt5PlusPower: IRP_MN_SET_POWER, type= %d\n", enmPowerType));
            switch (enmPowerType)
            {
                case SystemPowerState:
                {
                    Log(("vgdrvNtNt5PlusPower: SystemPowerState, action = %d, state = %d/%d\n",
                         enmPowerAction, PowerState.SystemState, PowerState.DeviceState));

                    switch (enmPowerAction)
                    {
                        case PowerActionSleep:

                            /* System now is in a working state. */
                            if (PowerState.SystemState == PowerSystemWorking)
                            {
                                if (   pDevExt
                                    && pDevExt->enmLastSystemPowerAction == PowerActionHibernate)
                                {
                                    Log(("vgdrvNtNt5PlusPower: Returning from hibernation!\n"));
                                    int rc = VGDrvCommonReinitDevExtAfterHibernation(&pDevExt->Core,
                                                                                     vgdrvNtVersionToOSType(g_enmVGDrvNtVer));
                                    if (RT_FAILURE(rc))
                                        Log(("vgdrvNtNt5PlusPower: Cannot re-init VMMDev chain, rc = %d!\n", rc));
                                }
                            }
                            break;

                        case PowerActionShutdownReset:
                        {
                            Log(("vgdrvNtNt5PlusPower: Power action reset!\n"));

                            /* Tell the VMM that we no longer support mouse pointer integration. */
                            VMMDevReqMouseStatus *pReq = NULL;
                            int vrc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReq, sizeof (VMMDevReqMouseStatus),
                                                    VMMDevReq_SetMouseStatus);
                            if (RT_SUCCESS(vrc))
                            {
                                pReq->mouseFeatures = 0;
                                pReq->pointerXPos = 0;
                                pReq->pointerYPos = 0;

                                vrc = VbglR0GRPerform(&pReq->header);
                                if (RT_FAILURE(vrc))
                                {
                                    Log(("vgdrvNtNt5PlusPower: error communicating new power status to VMMDev. vrc = %Rrc\n", vrc));
                                }

                                VbglR0GRFree(&pReq->header);
                            }

                            /* Don't do any cleanup here; there might be still coming in some IOCtls after we got this
                             * power action and would assert/crash when we already cleaned up all the stuff! */
                            break;
                        }

                        case PowerActionShutdown:
                        case PowerActionShutdownOff:
                        {
                            Log(("vgdrvNtNt5PlusPower: Power action shutdown!\n"));
                            if (PowerState.SystemState >= PowerSystemShutdown)
                            {
                                Log(("vgdrvNtNt5PlusPower: Telling the VMMDev to close the VM ...\n"));

                                VMMDevPowerStateRequest *pReq = pDevExt->pPowerStateRequest;
                                int vrc = VERR_NOT_IMPLEMENTED;
                                if (pReq)
                                {
                                    pReq->header.requestType = VMMDevReq_SetPowerStatus;
                                    pReq->powerState = VMMDevPowerState_PowerOff;

                                    vrc = VbglR0GRPerform(&pReq->header);
                                }
                                if (RT_FAILURE(vrc))
                                    Log(("vgdrvNtNt5PlusPower: Error communicating new power status to VMMDev. vrc = %Rrc\n", vrc));

                                /* No need to do cleanup here; at this point we should've been
                                 * turned off by VMMDev already! */
                            }
                            break;
                        }

                        case PowerActionHibernate:
                            Log(("vgdrvNtNt5PlusPower: Power action hibernate!\n"));
                            break;

                        case PowerActionWarmEject:
                            Log(("vgdrvNtNt5PlusPower: PowerActionWarmEject!\n"));
                            break;

                        default:
                            Log(("vgdrvNtNt5PlusPower: %d\n", enmPowerAction));
                            break;
                    }

                    /*
                     * Save the current system power action for later use.
                     * This becomes handy when we return from hibernation for example.
                     */
                    if (pDevExt)
                        pDevExt->enmLastSystemPowerAction = enmPowerAction;

                    break;
                }
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }

    /*
     * Whether we are completing or relaying this power IRP,
     * we must call PoStartNextPowerIrp.
     */
    g_pfnPoStartNextPowerIrp(pIrp);

    /*
     * Send the IRP down the driver stack, using PoCallDriver
     * (not IoCallDriver, as for non-power irps).
     */
    IoCopyCurrentIrpStackLocationToNext(pIrp);
    IoSetCompletionRoutine(pIrp,
                           vgdrvNtNt5PlusPowerComplete,
                           (PVOID)pDevExt,
                           TRUE,
                           TRUE,
                           TRUE);
    return g_pfnPoCallDriver(pDevExt->pNextLowerDriver, pIrp);
}


/**
 * IRP_MJ_SYSTEM_CONTROL handler.
 *
 * @returns NT status code
 * @param   pDevObj     Device object.
 * @param   pIrp        IRP.
 */
static NTSTATUS NTAPI vgdrvNtNt5PlusSystemControl(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    PVBOXGUESTDEVEXTWIN pDevExt = (PVBOXGUESTDEVEXTWIN)pDevObj->DeviceExtension;

    LogFlowFuncEnter();

    /* Always pass it on to the next driver. */
    IoSkipCurrentIrpStackLocation(pIrp);

    return IoCallDriver(pDevExt->pNextLowerDriver, pIrp);
}


/**
 * Unload the driver.
 *
 * @param   pDrvObj     Driver object.
 */
static void NTAPI vgdrvNtUnload(PDRIVER_OBJECT pDrvObj)
{
    LogFlowFuncEnter();

#ifdef TARGET_NT4
    /*
     * We need to destroy the device object here on NT4 and earlier.
     */
    PDEVICE_OBJECT pDevObj = pDrvObj->DeviceObject;
    if (pDevObj)
    {
        if (g_enmVGDrvNtVer <= VGDRVNTVER_WINNT4)
        {
            PVBOXGUESTDEVEXTWIN pDevExt = (PVBOXGUESTDEVEXTWIN)pDevObj->DeviceExtension;
            AssertPtr(pDevExt);
            AssertMsg(pDevExt->Core.uInitState == VBOXGUESTDEVEXT_INIT_STATE_RESOURCES,
                      ("uInitState=%#x\n", pDevExt->Core.uInitState));

            vgdrvNtDeleteDeviceResources(pDevExt);
            vgdrvNtDeleteDeviceFundamentAndUnlink(pDevObj, pDevExt);
        }
    }
#else  /* !TARGET_NT4 */
    /*
     * On a PnP driver this routine will be called after IRP_MN_REMOVE_DEVICE
     * where we already did the cleanup, so don't do anything here (yet).
     */
    RT_NOREF1(pDrvObj);
#endif /* !TARGET_NT4 */

    VGDrvCommonDestroyLoggers();
    RTR0Term();

    /*
     * Finally deregister the bugcheck callback.  Do it late to catch trouble in RTR0Term.
     */
    if (g_fBugCheckCallbackRegistered)
    {
        g_pfnKeDeregisterBugCheckCallback(&g_BugCheckCallbackRec);
        g_fBugCheckCallbackRegistered = false;
    }
}


/**
 * For simplifying request completion into a simple return statement, extended
 * version.
 *
 * @returns rcNt
 * @param   rcNt                The status code.
 * @param   uInfo               Extra info value.
 * @param   pIrp                The IRP.
 */
DECLINLINE(NTSTATUS) vgdrvNtCompleteRequestEx(NTSTATUS rcNt, ULONG_PTR uInfo, PIRP pIrp)
{
    pIrp->IoStatus.Status       = rcNt;
    pIrp->IoStatus.Information  = uInfo;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);
    return rcNt;
}


/**
 * For simplifying request completion into a simple return statement.
 *
 * @returns rcNt
 * @param   rcNt                The status code.
 * @param   pIrp                The IRP.
 */
DECLINLINE(NTSTATUS) vgdrvNtCompleteRequest(NTSTATUS rcNt, PIRP pIrp)
{
    return vgdrvNtCompleteRequestEx(rcNt, 0 /*uInfo*/, pIrp);
}


/**
 * Checks if NT authority rev 1 SID (SECURITY_NT_AUTHORITY).
 *
 * @returns true / false.
 * @param   pSid                The SID to check.
 */
DECLINLINE(bool) vgdrvNtIsSidNtAuth(struct _SID const *pSid)
{
    return pSid != NULL
        && pSid->Revision == 1
        && pSid->IdentifierAuthority.Value[5] == 5
        && pSid->IdentifierAuthority.Value[4] == 0
        && pSid->IdentifierAuthority.Value[3] == 0
        && pSid->IdentifierAuthority.Value[2] == 0
        && pSid->IdentifierAuthority.Value[1] == 0
        && pSid->IdentifierAuthority.Value[0] == 0;
}


/**
 * Matches SID with local system user (S-1-5-18 / SECURITY_LOCAL_SYSTEM_RID).
 */
DECLINLINE(bool) vgdrvNtIsSidLocalSystemUser(SID const *pSid)
{
    return vgdrvNtIsSidNtAuth(pSid)
        && pSid->SubAuthorityCount == 1
        && pSid->SubAuthority[0]   == SECURITY_LOCAL_SYSTEM_RID;
}


/**
 * Matches SID with NT system admin user (S-1-5-*-500 / DOMAIN_USER_RID_ADMIN).
 */
DECLINLINE(bool) vgdrvNtIsSidAdminUser(SID const *pSid)
{
    /** @todo restrict to SECURITY_NT_NON_UNIQUE? */
    return vgdrvNtIsSidNtAuth(pSid)
        && pSid->SubAuthorityCount >= 2
        && pSid->SubAuthorityCount <= SID_MAX_SUB_AUTHORITIES
        && pSid->SubAuthority[pSid->SubAuthorityCount - 1] == DOMAIN_USER_RID_ADMIN;
}


/**
 * Matches SID with NT system guest user (S-1-5-*-501 / DOMAIN_USER_RID_GUEST).
 */
DECLINLINE(bool) vgdrvNtIsSidGuestUser(SID const *pSid)
{
    /** @todo restrict to SECURITY_NT_NON_UNIQUE? */
    return vgdrvNtIsSidNtAuth(pSid)
        && pSid->SubAuthorityCount >= 2
        && pSid->SubAuthorityCount <= SID_MAX_SUB_AUTHORITIES
        && pSid->SubAuthority[pSid->SubAuthorityCount - 1] == DOMAIN_USER_RID_GUEST;
}


/**
 * Matches SID with NT system admins group (S-1-5-32-544, S-1-5-*-512).
 */
DECLINLINE(bool) vgdrvNtIsSidAdminsGroup(SID const *pSid)
{
    return vgdrvNtIsSidNtAuth(pSid)
        && (   (   pSid->SubAuthorityCount == 2
                && pSid->SubAuthority[0] == SECURITY_BUILTIN_DOMAIN_RID
                && pSid->SubAuthority[1] == DOMAIN_ALIAS_RID_ADMINS)
#if 0
            /** @todo restrict to SECURITY_NT_NON_UNIQUE? */
            || (   pSid->SubAuthorityCount >= 2
                && pSid->SubAuthorityCount <= SID_MAX_SUB_AUTHORITIES
                && pSid->SubAuthority[pSid->SubAuthorityCount - 1] == DOMAIN_GROUP_RID_ADMINS)
#endif
           );
}


/**
 * Matches SID with NT system users group (S-1-5-32-545, S-1-5-32-547, S-1-5-*-512).
 */
DECLINLINE(bool) vgdrvNtIsSidUsersGroup(SID const *pSid)
{
    return vgdrvNtIsSidNtAuth(pSid)
        && (   (   pSid->SubAuthorityCount == 2
                && pSid->SubAuthority[0] == SECURITY_BUILTIN_DOMAIN_RID
                && (   pSid->SubAuthority[1] == DOMAIN_ALIAS_RID_USERS
                    || pSid->SubAuthority[1] == DOMAIN_ALIAS_RID_POWER_USERS) )
#if 0
           /** @todo restrict to SECURITY_NT_NON_UNIQUE? */
           || (   pSid->SubAuthorityCount >= 2
               && pSid->SubAuthorityCount <= SID_MAX_SUB_AUTHORITIES
               && pSid->SubAuthority[pSid->SubAuthorityCount - 1] == DOMAIN_GROUP_RID_USERS)
#endif
           );
}


/**
 * Matches SID with NT system guests group (S-1-5-32-546, S-1-5-*-512).
 */
DECLINLINE(bool) vgdrvNtIsSidGuestsGroup(SID const *pSid)
{
    return vgdrvNtIsSidNtAuth(pSid)
        && (   (   pSid->SubAuthorityCount == 2
                && pSid->SubAuthority[0] == SECURITY_BUILTIN_DOMAIN_RID
                && pSid->SubAuthority[1] == DOMAIN_ALIAS_RID_GUESTS)
#if 0
           /** @todo restrict to SECURITY_NT_NON_UNIQUE? */
           || (   pSid->SubAuthorityCount >= 2
               && pSid->SubAuthorityCount <= SID_MAX_SUB_AUTHORITIES
               && pSid->SubAuthority[pSid->SubAuthorityCount - 1] == DOMAIN_GROUP_RID_GUESTS)
#endif
           );
}


/**
 * Checks if local authority rev 1 SID (SECURITY_LOCAL_SID_AUTHORITY).
 *
 * @returns true / false.
 * @param   pSid                The SID to check.
 */
DECLINLINE(bool) vgdrvNtIsSidLocalAuth(struct _SID const *pSid)
{
    return pSid != NULL
        && pSid->Revision == 1
        && pSid->IdentifierAuthority.Value[5] == 2
        && pSid->IdentifierAuthority.Value[4] == 0
        && pSid->IdentifierAuthority.Value[3] == 0
        && pSid->IdentifierAuthority.Value[2] == 0
        && pSid->IdentifierAuthority.Value[1] == 0
        && pSid->IdentifierAuthority.Value[0] == 0;
}


/**
 * Matches SID with console logon group (S-1-2-1 / SECURITY_LOCAL_LOGON_RID).
 */
DECLINLINE(bool) vgdrvNtIsSidConsoleLogonGroup(SID const *pSid)
{
    return vgdrvNtIsSidLocalAuth(pSid)
        && pSid->SubAuthorityCount == 1
        && pSid->SubAuthority[0] == SECURITY_LOCAL_LOGON_RID;
}


/**
 * Checks if mandatory label authority rev 1 SID (SECURITY_MANDATORY_LABEL_AUTHORITY).
 *
 * @returns true / false.
 * @param   pSid                The SID to check.
 */
DECLINLINE(bool) vgdrvNtIsSidMandatoryLabelAuth(struct _SID const *pSid)
{
    return pSid != NULL
        && pSid->Revision == 1
        && pSid->IdentifierAuthority.Value[5] == 16
        && pSid->IdentifierAuthority.Value[4] == 0
        && pSid->IdentifierAuthority.Value[3] == 0
        && pSid->IdentifierAuthority.Value[2] == 0
        && pSid->IdentifierAuthority.Value[1] == 0
        && pSid->IdentifierAuthority.Value[0] == 0;
}


#ifdef LOG_ENABLED
/** Format an SID for logging.  */
static const char *vgdrvNtFormatSid(char *pszBuf, size_t cbBuf, struct _SID const *pSid)
{
    uint64_t uAuth = RT_MAKE_U64_FROM_U8(pSid->IdentifierAuthority.Value[5], pSid->IdentifierAuthority.Value[4],
                                         pSid->IdentifierAuthority.Value[3], pSid->IdentifierAuthority.Value[2],
                                         pSid->IdentifierAuthority.Value[1], pSid->IdentifierAuthority.Value[0],
                                         0,                                  0);
    ssize_t offCur = RTStrPrintf2(pszBuf, cbBuf, "S-%u-%RU64", pSid->Revision, uAuth);
    ULONG const *puSubAuth = &pSid->SubAuthority[0];
    unsigned     cSubAuths = pSid->SubAuthorityCount;
    while (cSubAuths > 0 && (size_t)offCur < cbBuf)
    {
        ssize_t cchThis = RTStrPrintf2(&pszBuf[offCur], cbBuf - (size_t)offCur, "-%u", *puSubAuth);
        if (cchThis > 0)
        {
            offCur += cchThis;
            puSubAuth++;
            cSubAuths--;
        }
        else
        {
            Assert(cbBuf >= 5);
            pszBuf[cbBuf - 4] = '.';
            pszBuf[cbBuf - 3] = '.';
            pszBuf[cbBuf - 2] = '.';
            pszBuf[cbBuf - 1] = '\0';
            break;
        }
    }
    return pszBuf;
}
#endif


/**
 * Calculate requestor flags for the current process.
 *
 * ASSUMES vgdrvNtCreate is executed in the context of the process and thread
 * doing the NtOpenFile call.
 *
 * @returns VMMDEV_REQUESTOR_XXX
 */
static uint32_t vgdrvNtCalcRequestorFlags(void)
{
    uint32_t fRequestor = VMMDEV_REQUESTOR_USERMODE
                        | VMMDEV_REQUESTOR_USR_NOT_GIVEN
                        | VMMDEV_REQUESTOR_CON_DONT_KNOW
                        | VMMDEV_REQUESTOR_TRUST_NOT_GIVEN
                        | VMMDEV_REQUESTOR_NO_USER_DEVICE;
    HANDLE   hToken = NULL;
    NTSTATUS rcNt = ZwOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY, &hToken);
    if (NT_SUCCESS(rcNt))
    {
        union
        {
            TOKEN_USER      CurUser;
            TOKEN_GROUPS    CurGroups;
            uint8_t         abPadding[256];
        } Buf;
#ifdef LOG_ENABLED
        char szSid[200];
#endif

        /*
         * Get the user SID and see if it's a standard one.
         */
        RT_ZERO(Buf.CurUser);
        ULONG cbReturned = 0;
        rcNt = ZwQueryInformationToken(hToken, TokenUser, &Buf.CurUser, sizeof(Buf), &cbReturned);
        if (NT_SUCCESS(rcNt))
        {
            struct _SID const *pSid = (struct _SID const *)Buf.CurUser.User.Sid;
            Log5(("vgdrvNtCalcRequestorFlags: TokenUser: %#010x %s\n",
                  Buf.CurUser.User.Attributes, vgdrvNtFormatSid(szSid, sizeof(szSid), pSid)));

            if (vgdrvNtIsSidLocalSystemUser(pSid))
                fRequestor = (fRequestor & ~VMMDEV_REQUESTOR_USR_MASK) | VMMDEV_REQUESTOR_USR_SYSTEM;
            else if (vgdrvNtIsSidAdminUser(pSid))
                fRequestor = (fRequestor & ~VMMDEV_REQUESTOR_USR_MASK) | VMMDEV_REQUESTOR_USR_ROOT;
            else if (vgdrvNtIsSidGuestUser(pSid))
                fRequestor = (fRequestor & ~VMMDEV_REQUESTOR_USR_MASK) | VMMDEV_REQUESTOR_USR_GUEST;
        }
        else
            LogRel(("vgdrvNtCalcRequestorFlags: TokenUser query failed: %#x\n", rcNt));

        /*
         * Get the groups.
         */
        TOKEN_GROUPS *pCurGroupsFree = NULL;
        TOKEN_GROUPS *pCurGroups     = &Buf.CurGroups;
        uint32_t      cbCurGroups    = sizeof(Buf);
        cbReturned = 0;
        RT_ZERO(Buf);
        rcNt = ZwQueryInformationToken(hToken, TokenGroups, pCurGroups, cbCurGroups, &cbReturned);
        if (rcNt == STATUS_BUFFER_TOO_SMALL)
        {
            uint32_t cTries = 8;
            do
            {
                RTMemTmpFree(pCurGroupsFree);
                if (cbCurGroups < cbReturned)
                    cbCurGroups = RT_ALIGN_32(cbCurGroups + 32, 64);
                else
                    cbCurGroups += 64;
                pCurGroupsFree = pCurGroups = (TOKEN_GROUPS *)RTMemTmpAllocZ(cbCurGroups);
                if (pCurGroupsFree)
                    rcNt = ZwQueryInformationToken(hToken, TokenGroups, pCurGroups, cbCurGroups, &cbReturned);
                else
                    rcNt = STATUS_NO_MEMORY;
            } while (rcNt == STATUS_BUFFER_TOO_SMALL && cTries-- > 0);
        }
        if (NT_SUCCESS(rcNt))
        {
            bool fGuestsMember = false;
            bool fUsersMember  = false;
            if (g_enmVGDrvNtVer >= VGDRVNTVER_WIN7)
                fRequestor = (fRequestor & ~VMMDEV_REQUESTOR_CON_MASK) | VMMDEV_REQUESTOR_CON_NO;

            for (uint32_t iGrp = 0; iGrp < pCurGroups->GroupCount; iGrp++)
            {
                uint32_t const     fAttribs = pCurGroups->Groups[iGrp].Attributes;
                struct _SID const *pSid     = (struct _SID const *)pCurGroups->Groups[iGrp].Sid;
                Log5(("vgdrvNtCalcRequestorFlags: TokenGroups[%u]: %#10x %s\n",
                      iGrp, fAttribs, vgdrvNtFormatSid(szSid, sizeof(szSid), pSid)));

                if (   (fAttribs & SE_GROUP_INTEGRITY_ENABLED)
                    && vgdrvNtIsSidMandatoryLabelAuth(pSid)
                    && pSid->SubAuthorityCount == 1
                    && (fRequestor & VMMDEV_REQUESTOR_TRUST_MASK) == VMMDEV_REQUESTOR_TRUST_NOT_GIVEN)
                {
                    fRequestor &= ~VMMDEV_REQUESTOR_TRUST_MASK;
                    if (pSid->SubAuthority[0] < SECURITY_MANDATORY_LOW_RID)
                        fRequestor |= VMMDEV_REQUESTOR_TRUST_UNTRUSTED;
                    else if (pSid->SubAuthority[0] < SECURITY_MANDATORY_MEDIUM_RID)
                        fRequestor |= VMMDEV_REQUESTOR_TRUST_LOW;
                    else if (pSid->SubAuthority[0] < SECURITY_MANDATORY_MEDIUM_PLUS_RID)
                        fRequestor |= VMMDEV_REQUESTOR_TRUST_MEDIUM;
                    else if (pSid->SubAuthority[0] < SECURITY_MANDATORY_HIGH_RID)
                        fRequestor |= VMMDEV_REQUESTOR_TRUST_MEDIUM_PLUS;
                    else if (pSid->SubAuthority[0] < SECURITY_MANDATORY_SYSTEM_RID)
                        fRequestor |= VMMDEV_REQUESTOR_TRUST_HIGH;
                    else if (pSid->SubAuthority[0] < SECURITY_MANDATORY_PROTECTED_PROCESS_RID)
                        fRequestor |= VMMDEV_REQUESTOR_TRUST_SYSTEM;
                    else
                        fRequestor |= VMMDEV_REQUESTOR_TRUST_PROTECTED;
                    Log5(("vgdrvNtCalcRequestorFlags: mandatory label %u: => %#x\n", pSid->SubAuthority[0], fRequestor));
                }
                else if (      (fAttribs & (SE_GROUP_ENABLED | SE_GROUP_MANDATORY | SE_GROUP_USE_FOR_DENY_ONLY))
                            ==             (SE_GROUP_ENABLED | SE_GROUP_MANDATORY)
                         && vgdrvNtIsSidConsoleLogonGroup(pSid))
                {
                    fRequestor = (fRequestor & ~VMMDEV_REQUESTOR_CON_MASK) | VMMDEV_REQUESTOR_CON_YES;
                    Log5(("vgdrvNtCalcRequestorFlags: console: => %#x\n", fRequestor));
                }
                else if (      (fAttribs & (SE_GROUP_ENABLED | SE_GROUP_MANDATORY | SE_GROUP_USE_FOR_DENY_ONLY))
                            ==             (SE_GROUP_ENABLED | SE_GROUP_MANDATORY)
                         && vgdrvNtIsSidNtAuth(pSid))
                {
                    if (vgdrvNtIsSidAdminsGroup(pSid))
                    {
                        fRequestor |= VMMDEV_REQUESTOR_GRP_WHEEL;
                        Log5(("vgdrvNtCalcRequestorFlags: admins group: => %#x\n", fRequestor));
                    }
                    else if (vgdrvNtIsSidUsersGroup(pSid))
                    {
                        Log5(("vgdrvNtCalcRequestorFlags: users group\n"));
                        fUsersMember = true;
                    }
                    else if (vgdrvNtIsSidGuestsGroup(pSid))
                    {
                        Log5(("vgdrvNtCalcRequestorFlags: guests group\n"));
                        fGuestsMember = true;
                    }
                }
            }
            if ((fRequestor & VMMDEV_REQUESTOR_USR_MASK) == VMMDEV_REQUESTOR_USR_NOT_GIVEN)
            {
                if (fUsersMember)
                    fRequestor = (fRequestor & ~VMMDEV_REQUESTOR_USR_MASK) | VMMDEV_REQUESTOR_USR_USER;
                else if (fGuestsMember)
                    fRequestor = (fRequestor & ~VMMDEV_REQUESTOR_USR_MASK) | VMMDEV_REQUESTOR_USR_GUEST;
            }
        }
        else
            LogRel(("vgdrvNtCalcRequestorFlags: TokenGroups query failed: %#x\n", rcNt));

        RTMemTmpFree(pCurGroupsFree);
        ZwClose(hToken);

        /*
         * Determine whether we should set VMMDEV_REQUESTOR_USER_DEVICE or not.
         *
         * The purpose here is to differentiate VBoxService accesses
         * from VBoxTray and VBoxControl, as VBoxService should be allowed to
         * do more than the latter two.  VBoxService normally runs under the
         * system account which is easily detected, but for debugging and
         * similar purposes we also allow an elevated admin to run it as well.
         */
        if (   (fRequestor & VMMDEV_REQUESTOR_TRUST_MASK) == VMMDEV_REQUESTOR_TRUST_UNTRUSTED /* general paranoia wrt system account */
            || (fRequestor & VMMDEV_REQUESTOR_TRUST_MASK) == VMMDEV_REQUESTOR_TRUST_LOW       /* ditto */
            || (fRequestor & VMMDEV_REQUESTOR_TRUST_MASK) == VMMDEV_REQUESTOR_TRUST_MEDIUM    /* ditto */
            || !(   (fRequestor & VMMDEV_REQUESTOR_USR_MASK) == VMMDEV_REQUESTOR_USR_SYSTEM
                 || (   (   (fRequestor & VMMDEV_REQUESTOR_GRP_WHEEL)
                         || (fRequestor & VMMDEV_REQUESTOR_USR_MASK) == VMMDEV_REQUESTOR_USR_ROOT)
                     && (   (fRequestor & VMMDEV_REQUESTOR_TRUST_MASK) >= VMMDEV_REQUESTOR_TRUST_HIGH
                         || (fRequestor & VMMDEV_REQUESTOR_TRUST_MASK) == VMMDEV_REQUESTOR_TRUST_NOT_GIVEN)) ))
            fRequestor |= VMMDEV_REQUESTOR_USER_DEVICE;
    }
    else
    {
        LogRel(("vgdrvNtCalcRequestorFlags: NtOpenProcessToken query failed: %#x\n", rcNt));
        fRequestor |= VMMDEV_REQUESTOR_USER_DEVICE;
    }

    Log5(("vgdrvNtCalcRequestorFlags: returns %#x\n", fRequestor));
    return fRequestor;
}


/**
 * Create (i.e. Open) file entry point.
 *
 * @param   pDevObj     Device object.
 * @param   pIrp        Request packet.
 */
static NTSTATUS NTAPI vgdrvNtCreate(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    Log(("vgdrvNtCreate: RequestorMode=%d\n", pIrp->RequestorMode));
    PIO_STACK_LOCATION  pStack   = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT        pFileObj = pStack->FileObject;
    PVBOXGUESTDEVEXTWIN pDevExt  = (PVBOXGUESTDEVEXTWIN)pDevObj->DeviceExtension;

    Assert(pFileObj->FsContext == NULL);

    /*
     * We are not remotely similar to a directory...
     */
    NTSTATUS rcNt;
    if (!(pStack->Parameters.Create.Options & FILE_DIRECTORY_FILE))
    {
        /*
         * Check the device state.  We enter the critsect in shared mode to
         * prevent race with PnP system requests checking whether we're idle.
         */
        RTCritSectRwEnterShared(&pDevExt->SessionCreateCritSect);
        VGDRVNTDEVSTATE const enmDevState = pDevExt->enmDevState;
        if (enmDevState == VGDRVNTDEVSTATE_OPERATIONAL)
        {
            /*
             * Create a client session.
             */
            int                 rc;
            PVBOXGUESTSESSION   pSession;
            if (pIrp->RequestorMode == KernelMode)
                rc = VGDrvCommonCreateKernelSession(&pDevExt->Core, &pSession);
            else
                rc = VGDrvCommonCreateUserSession(&pDevExt->Core, vgdrvNtCalcRequestorFlags(), &pSession);
            RTCritSectRwLeaveShared(&pDevExt->SessionCreateCritSect);
            if (RT_SUCCESS(rc))
            {
                pFileObj->FsContext = pSession;
                Log(("vgdrvNtCreate: Successfully created %s session %p (fRequestor=%#x)\n",
                     pIrp->RequestorMode == KernelMode ? "kernel" : "user", pSession, pSession->fRequestor));

                return vgdrvNtCompleteRequestEx(STATUS_SUCCESS, FILE_OPENED, pIrp);
            }

            /* Note. the IoStatus is completely ignored on error. */
            Log(("vgdrvNtCreate: Failed to create session: rc=%Rrc\n", rc));
            if (rc == VERR_NO_MEMORY)
                rcNt = STATUS_NO_MEMORY;
            else
                rcNt = STATUS_UNSUCCESSFUL;
        }
        else
        {
            RTCritSectRwLeaveShared(&pDevExt->SessionCreateCritSect);
            LogFlow(("vgdrvNtCreate: Failed. Device is not in 'working' state: %d\n", enmDevState));
            rcNt = STATUS_DEVICE_NOT_READY;
        }
    }
    else
    {
        LogFlow(("vgdrvNtCreate: Failed. FILE_DIRECTORY_FILE set\n"));
        rcNt = STATUS_NOT_A_DIRECTORY;
    }
    return vgdrvNtCompleteRequest(rcNt, pIrp);
}


/**
 * Close file entry point.
 *
 * @param   pDevObj     Device object.
 * @param   pIrp        Request packet.
 */
static NTSTATUS NTAPI vgdrvNtClose(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    PVBOXGUESTDEVEXTWIN pDevExt  = (PVBOXGUESTDEVEXTWIN)pDevObj->DeviceExtension;
    PIO_STACK_LOCATION  pStack   = IoGetCurrentIrpStackLocation(pIrp);
    PFILE_OBJECT        pFileObj = pStack->FileObject;

    LogFlowFunc(("pDevExt=0x%p, pFileObj=0x%p, FsContext=0x%p\n", pDevExt, pFileObj, pFileObj->FsContext));

#ifdef VBOX_WITH_HGCM
    /* Close both, R0 and R3 sessions. */
    PVBOXGUESTSESSION pSession = (PVBOXGUESTSESSION)pFileObj->FsContext;
    if (pSession)
        VGDrvCommonCloseSession(&pDevExt->Core, pSession);
#endif

    pFileObj->FsContext = NULL;
    pIrp->IoStatus.Information = 0;
    pIrp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}


/**
 * Device I/O Control entry point.
 *
 * @param   pDevObj     Device object.
 * @param   pIrp        Request packet.
 */
NTSTATUS NTAPI vgdrvNtDeviceControl(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    PVBOXGUESTDEVEXTWIN pDevExt  = (PVBOXGUESTDEVEXTWIN)pDevObj->DeviceExtension;
    PIO_STACK_LOCATION  pStack   = IoGetCurrentIrpStackLocation(pIrp);
    PVBOXGUESTSESSION   pSession = pStack->FileObject ? (PVBOXGUESTSESSION)pStack->FileObject->FsContext : NULL;

    if (!RT_VALID_PTR(pSession))
        return vgdrvNtCompleteRequest(STATUS_TRUST_FAILURE, pIrp);

#if 0 /* No fast I/O controls defined yet. */
    /*
     * Deal with the 2-3 high-speed IOCtl that takes their arguments from
     * the session and iCmd, and does not return anything.
     */
    if (pSession->fUnrestricted)
    {
        ULONG ulCmd = pStack->Parameters.DeviceIoControl.IoControlCode;
        if (   ulCmd == SUP_IOCTL_FAST_DO_RAW_RUN
            || ulCmd == SUP_IOCTL_FAST_DO_HM_RUN
            || ulCmd == SUP_IOCTL_FAST_DO_NOP)
        {
            int rc = supdrvIOCtlFast(ulCmd, (unsigned)(uintptr_t)pIrp->UserBuffer /* VMCPU id */, pDevExt, pSession);

            /* Complete the I/O request. */
            supdrvSessionRelease(pSession);
            return vgdrvNtCompleteRequest(RT_SUCCESS(rc) ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER, pIrp);
        }
    }
#endif

    return vgdrvNtDeviceControlSlow(&pDevExt->Core, pSession, pIrp, pStack);
}


/**
 * Device I/O Control entry point.
 *
 * @param   pDevExt     The device extension.
 * @param   pSession    The session.
 * @param   pIrp        Request packet.
 * @param   pStack      The request stack pointer.
 */
static NTSTATUS vgdrvNtDeviceControlSlow(PVBOXGUESTDEVEXT pDevExt, PVBOXGUESTSESSION pSession,
                                         PIRP pIrp, PIO_STACK_LOCATION pStack)
{
    NTSTATUS    rcNt;
    uint32_t    cbOut = 0;
    int         rc = 0;
    Log2(("vgdrvNtDeviceControlSlow(%p,%p): ioctl=%#x pBuf=%p cbIn=%#x cbOut=%#x pSession=%p\n",
          pDevExt, pIrp, pStack->Parameters.DeviceIoControl.IoControlCode,
          pIrp->AssociatedIrp.SystemBuffer, pStack->Parameters.DeviceIoControl.InputBufferLength,
          pStack->Parameters.DeviceIoControl.OutputBufferLength, pSession));

#if 0 /*def RT_ARCH_AMD64*/
    /* Don't allow 32-bit processes to do any I/O controls. */
    if (!IoIs32bitProcess(pIrp))
#endif
    {
        /* Verify that it's a buffered CTL. */
        if ((pStack->Parameters.DeviceIoControl.IoControlCode & 0x3) == METHOD_BUFFERED)
        {
            /* Verify that the sizes in the request header are correct. */
            PVBGLREQHDR pHdr = (PVBGLREQHDR)pIrp->AssociatedIrp.SystemBuffer;
            if (   pStack->Parameters.DeviceIoControl.InputBufferLength  >= sizeof(*pHdr)
                && pStack->Parameters.DeviceIoControl.InputBufferLength  == pHdr->cbIn
                && pStack->Parameters.DeviceIoControl.OutputBufferLength == pHdr->cbOut)
            {
                /* Zero extra output bytes to make sure we don't leak anything. */
                if (pHdr->cbIn < pHdr->cbOut)
                    RtlZeroMemory((uint8_t *)pHdr + pHdr->cbIn, pHdr->cbOut - pHdr->cbIn);

                /*
                 * Do the job.
                 */
                rc = VGDrvCommonIoCtl(pStack->Parameters.DeviceIoControl.IoControlCode, pDevExt, pSession, pHdr,
                                      RT_MAX(pHdr->cbIn, pHdr->cbOut));
                if (RT_SUCCESS(rc))
                {
                    rcNt  = STATUS_SUCCESS;
                    cbOut = pHdr->cbOut;
                    if (cbOut > pStack->Parameters.DeviceIoControl.OutputBufferLength)
                    {
                        cbOut = pStack->Parameters.DeviceIoControl.OutputBufferLength;
                        LogRel(("vgdrvNtDeviceControlSlow: too much output! %#x > %#x; uCmd=%#x!\n",
                                pHdr->cbOut, cbOut, pStack->Parameters.DeviceIoControl.IoControlCode));
                    }

                    /* If IDC successful disconnect request, we must set the context pointer to NULL. */
                    if (   pStack->Parameters.DeviceIoControl.IoControlCode == VBGL_IOCTL_IDC_DISCONNECT
                        && RT_SUCCESS(pHdr->rc))
                        pStack->FileObject->FsContext = NULL;
                }
                else if (rc == VERR_NOT_SUPPORTED)
                    rcNt = STATUS_NOT_SUPPORTED;
                else
                    rcNt = STATUS_INVALID_PARAMETER;
                Log2(("vgdrvNtDeviceControlSlow: returns %#x cbOut=%d rc=%#x\n", rcNt, cbOut, rc));
            }
            else
            {
                Log(("vgdrvNtDeviceControlSlow: Mismatching sizes (%#x) - Hdr=%#lx/%#lx Irp=%#lx/%#lx!\n",
                     pStack->Parameters.DeviceIoControl.IoControlCode,
                     pStack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(*pHdr) ? pHdr->cbIn  : 0,
                     pStack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(*pHdr) ? pHdr->cbOut : 0,
                     pStack->Parameters.DeviceIoControl.InputBufferLength,
                     pStack->Parameters.DeviceIoControl.OutputBufferLength));
                rcNt = STATUS_INVALID_PARAMETER;
            }
        }
        else
        {
            Log(("vgdrvNtDeviceControlSlow: not buffered request (%#x) - not supported\n",
                 pStack->Parameters.DeviceIoControl.IoControlCode));
            rcNt = STATUS_NOT_SUPPORTED;
        }
    }
#if 0 /*def RT_ARCH_AMD64*/
    else
    {
        Log(("VBoxDrvNtDeviceControlSlow: WOW64 req - not supported\n"));
        rcNt = STATUS_NOT_SUPPORTED;
    }
#endif

    return vgdrvNtCompleteRequestEx(rcNt, cbOut, pIrp);
}


/**
 * Internal Device I/O Control entry point (for IDC).
 *
 * @param   pDevObj     Device object.
 * @param   pIrp        Request packet.
 */
static NTSTATUS NTAPI vgdrvNtInternalIOCtl(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    /* Currently no special code here. */
    return vgdrvNtDeviceControl(pDevObj, pIrp);
}


/**
 * IRP_MJ_SHUTDOWN handler.
 *
 * @returns NT status code
 * @param pDevObj    Device object.
 * @param pIrp       IRP.
 */
static NTSTATUS NTAPI vgdrvNtShutdown(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    PVBOXGUESTDEVEXTWIN pDevExt = (PVBOXGUESTDEVEXTWIN)pDevObj->DeviceExtension;
    LogFlowFuncEnter();

    VMMDevPowerStateRequest *pReq = pDevExt->pPowerStateRequest;
    if (pReq)
    {
        pReq->header.requestType = VMMDevReq_SetPowerStatus;
        pReq->powerState = VMMDevPowerState_PowerOff;

        int rc = VbglR0GRPerform(&pReq->header);
        if (RT_FAILURE(rc))
            LogFunc(("Error performing request to VMMDev, rc=%Rrc\n", rc));
    }

    /* just in case, since we shouldn't normally get here. */
    pIrp->IoStatus.Information = 0;
    pIrp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}


/**
 * Stub function for functions we don't implemented.
 *
 * @returns STATUS_NOT_SUPPORTED
 * @param   pDevObj     Device object.
 * @param   pIrp        IRP.
 */
static NTSTATUS vgdrvNtNotSupportedStub(PDEVICE_OBJECT pDevObj, PIRP pIrp)
{
    RT_NOREF1(pDevObj);
    LogFlowFuncEnter();

    pIrp->IoStatus.Information = 0;
    pIrp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoCompleteRequest(pIrp, IO_NO_INCREMENT);

    return STATUS_NOT_SUPPORTED;
}


/**
 * Bug check callback (KBUGCHECK_CALLBACK_ROUTINE).
 *
 * This adds a log entry on the host, in case Hyper-V isn't active or the guest
 * is too old for reporting it itself via the crash MSRs.
 *
 * @param   pvBuffer            Not used.
 * @param   cbBuffer            Not used.
 */
static VOID NTAPI vgdrvNtBugCheckCallback(PVOID pvBuffer, ULONG cbBuffer)
{
    if (g_pauKiBugCheckData)
    {
        RTLogBackdoorPrintf("VBoxGuest: BugCheck! P0=%#zx P1=%#zx P2=%#zx P3=%#zx P4=%#zx\n", g_pauKiBugCheckData[0],
                            g_pauKiBugCheckData[1], g_pauKiBugCheckData[2], g_pauKiBugCheckData[3], g_pauKiBugCheckData[4]);

        VMMDevReqNtBugCheck *pReq = NULL;
        int rc = VbglR0GRAlloc((VMMDevRequestHeader **)&pReq, sizeof(*pReq), VMMDevReq_NtBugCheck);
        if (RT_SUCCESS(rc))
        {
            pReq->uBugCheck       = g_pauKiBugCheckData[0];
            pReq->auParameters[0] = g_pauKiBugCheckData[1];
            pReq->auParameters[1] = g_pauKiBugCheckData[2];
            pReq->auParameters[2] = g_pauKiBugCheckData[3];
            pReq->auParameters[3] = g_pauKiBugCheckData[4];
            VbglR0GRPerform(&pReq->header);
            VbglR0GRFree(&pReq->header);
        }
    }
    else
    {
        RTLogBackdoorPrintf("VBoxGuest: BugCheck!\n");

        VMMDevRequestHeader *pReqHdr = NULL;
        int rc = VbglR0GRAlloc(&pReqHdr, sizeof(*pReqHdr), VMMDevReq_NtBugCheck);
        if (RT_SUCCESS(rc))
        {
            VbglR0GRPerform(pReqHdr);
            VbglR0GRFree(pReqHdr);
        }
    }

    RT_NOREF(pvBuffer, cbBuffer);
}


/**
 * Sets the mouse notification callback.
 *
 * @returns VBox status code.
 * @param   pDevExt   Pointer to the device extension.
 * @param   pNotify   Pointer to the mouse notify struct.
 */
int VGDrvNativeSetMouseNotifyCallback(PVBOXGUESTDEVEXT pDevExt, PVBGLIOCSETMOUSENOTIFYCALLBACK pNotify)
{
    PVBOXGUESTDEVEXTWIN pDevExtWin = (PVBOXGUESTDEVEXTWIN)pDevExt;
    /* we need a lock here to avoid concurrency with the set event functionality */
    KIRQL OldIrql;
    KeAcquireSpinLock(&pDevExtWin->MouseEventAccessSpinLock, &OldIrql);
    pDevExtWin->Core.pfnMouseNotifyCallback   = pNotify->u.In.pfnNotify;
    pDevExtWin->Core.pvMouseNotifyCallbackArg = pNotify->u.In.pvUser;
    KeReleaseSpinLock(&pDevExtWin->MouseEventAccessSpinLock, OldIrql);
    return VINF_SUCCESS;
}


/**
 * DPC handler.
 *
 * @param   pDPC        DPC descriptor.
 * @param   pDevObj     Device object.
 * @param   pIrp        Interrupt request packet.
 * @param   pContext    Context specific pointer.
 */
static void NTAPI vgdrvNtDpcHandler(PKDPC pDPC, PDEVICE_OBJECT pDevObj, PIRP pIrp, PVOID pContext)
{
    RT_NOREF3(pDPC, pIrp, pContext);
    PVBOXGUESTDEVEXTWIN pDevExt = (PVBOXGUESTDEVEXTWIN)pDevObj->DeviceExtension;
    Log3Func(("pDevExt=0x%p\n", pDevExt));

    /* Test & reset the counter. */
    if (ASMAtomicXchgU32(&pDevExt->Core.u32MousePosChangedSeq, 0))
    {
        /* we need a lock here to avoid concurrency with the set event ioctl handler thread,
         * i.e. to prevent the event from destroyed while we're using it */
        Assert(KeGetCurrentIrql() == DISPATCH_LEVEL);
        KeAcquireSpinLockAtDpcLevel(&pDevExt->MouseEventAccessSpinLock);

        if (pDevExt->Core.pfnMouseNotifyCallback)
            pDevExt->Core.pfnMouseNotifyCallback(pDevExt->Core.pvMouseNotifyCallbackArg);

        KeReleaseSpinLockFromDpcLevel(&pDevExt->MouseEventAccessSpinLock);
    }

    /* Process the wake-up list we were asked by the scheduling a DPC
     * in vgdrvNtIsrHandler(). */
    VGDrvCommonWaitDoWakeUps(&pDevExt->Core);
}


/**
 * ISR handler.
 *
 * @return  BOOLEAN         Indicates whether the IRQ came from us (TRUE) or not (FALSE).
 * @param   pInterrupt      Interrupt that was triggered.
 * @param   pServiceContext Context specific pointer.
 */
static BOOLEAN NTAPI vgdrvNtIsrHandler(PKINTERRUPT pInterrupt, PVOID pServiceContext)
{
    RT_NOREF1(pInterrupt);
    PVBOXGUESTDEVEXTWIN pDevExt = (PVBOXGUESTDEVEXTWIN)pServiceContext;
    if (pDevExt == NULL)
        return FALSE;

    /*Log3Func(("pDevExt=0x%p, pVMMDevMemory=0x%p\n", pDevExt, pDevExt ? pDevExt->pVMMDevMemory : NULL));*/

    /* Enter the common ISR routine and do the actual work. */
    BOOLEAN fIRQTaken = VGDrvCommonISR(&pDevExt->Core);

    /* If we need to wake up some events we do that in a DPC to make
     * sure we're called at the right IRQL. */
    if (fIRQTaken)
    {
        Log3Func(("IRQ was taken! pInterrupt=0x%p, pDevExt=0x%p\n", pInterrupt, pDevExt));
        if (ASMAtomicUoReadU32(   &pDevExt->Core.u32MousePosChangedSeq)
                               || !RTListIsEmpty(&pDevExt->Core.WakeUpList))
        {
            Log3Func(("Requesting DPC...\n"));
            IoRequestDpc(pDevExt->pDeviceObject, NULL /*pIrp*/, NULL /*pvContext*/);
        }
    }
    return fIRQTaken;
}


void VGDrvNativeISRMousePollEvent(PVBOXGUESTDEVEXT pDevExt)
{
    NOREF(pDevExt);
    /* nothing to do here - i.e. since we can not KeSetEvent from ISR level,
     * we rely on the pDevExt->u32MousePosChangedSeq to be set to a non-zero value on a mouse event
     * and queue the DPC in our ISR routine in that case doing KeSetEvent from the DPC routine */
}


/**
 * Hook for handling OS specfic options from the host.
 *
 * @returns true if handled, false if not.
 * @param   pDevExt         The device extension.
 * @param   pszName         The option name.
 * @param   pszValue        The option value.
 */
bool VGDrvNativeProcessOption(PVBOXGUESTDEVEXT pDevExt, const char *pszName, const char *pszValue)
{
    RT_NOREF(pDevExt); RT_NOREF(pszName); RT_NOREF(pszValue);
    return false;
}


/**
 * Implements RTL_QUERY_REGISTRY_ROUTINE for enumerating our registry key.
 */
static NTSTATUS NTAPI vgdrvNtRegistryEnumCallback(PWSTR pwszValueName, ULONG uValueType,
                                                  PVOID pvValue, ULONG cbValue, PVOID pvUser, PVOID pvEntryCtx)
{
    Log4(("vgdrvNtRegistryEnumCallback: pwszValueName=%ls uValueType=%#x Value=%.*Rhxs\n", pwszValueName, uValueType, cbValue, pvValue));

    /*
     * Filter out general service config values.
     */
    if (   RTUtf16ICmpAscii(pwszValueName, "Type") == 0
        || RTUtf16ICmpAscii(pwszValueName, "Start") == 0
        || RTUtf16ICmpAscii(pwszValueName, "ErrorControl") == 0
        || RTUtf16ICmpAscii(pwszValueName, "Tag") == 0
        || RTUtf16ICmpAscii(pwszValueName, "ImagePath") == 0
        || RTUtf16ICmpAscii(pwszValueName, "DisplayName") == 0
        || RTUtf16ICmpAscii(pwszValueName, "Group") == 0
        || RTUtf16ICmpAscii(pwszValueName, "DependOnGroup") == 0
        || RTUtf16ICmpAscii(pwszValueName, "DependOnService") == 0
       )
    {
        return STATUS_SUCCESS;
    }

    /*
     * Convert the value name.
     */
    size_t cch = RTUtf16CalcUtf8Len(pwszValueName);
    if (cch < 64 && cch > 0)
    {
        char szValueName[72];
        char *pszTmp = szValueName;
        int rc = RTUtf16ToUtf8Ex(pwszValueName, RTSTR_MAX, &pszTmp, sizeof(szValueName), NULL);
        if (RT_SUCCESS(rc))
        {
            /*
             * Convert the value.
             */
            char  szValue[72];
            char *pszFree = NULL;
            char *pszValue = NULL;
            szValue[0] = '\0';
            switch (uValueType)
            {
                case REG_SZ:
                case REG_EXPAND_SZ:
                    rc = RTUtf16CalcUtf8LenEx((PCRTUTF16)pvValue, cbValue / sizeof(RTUTF16), &cch);
                    if (RT_SUCCESS(rc) && cch < _1K)
                    {
                        if (cch < sizeof(szValue))
                        {
                            pszValue = szValue;
                            rc = RTUtf16ToUtf8Ex((PCRTUTF16)pvValue, cbValue / sizeof(RTUTF16), &pszValue, sizeof(szValue), NULL);
                        }
                        else
                        {
                            rc = RTUtf16ToUtf8Ex((PCRTUTF16)pvValue, cbValue / sizeof(RTUTF16), &pszValue, sizeof(szValue), NULL);
                            if (RT_SUCCESS(rc))
                                pszFree = pszValue;
                        }
                        if (RT_FAILURE(rc))
                        {
                            LogRel(("VBoxGuest: Failed to convert registry value '%ls' string data to UTF-8: %Rrc\n",
                                    pwszValueName, rc));
                            pszValue = NULL;
                        }
                    }
                    else if (RT_SUCCESS(rc))
                        LogRel(("VBoxGuest: Registry value '%ls' has a too long value: %#x (uvalueType=%#x)\n",
                                pwszValueName, cbValue, uValueType));
                    else
                        LogRel(("VBoxGuest: Registry value '%ls' has an invalid string value (cbValue=%#x, uvalueType=%#x)\n",
                                pwszValueName, cbValue, uValueType));
                    break;

                case REG_DWORD:
                    if (cbValue == sizeof(uint32_t))
                    {
                        RTStrFormatU32(szValue, sizeof(szValue), *(uint32_t const *)pvValue, 10, 0, 0, 0);
                        pszValue = szValue;
                    }
                    else
                        LogRel(("VBoxGuest: Registry value '%ls' has wrong length for REG_DWORD: %#x\n", pwszValueName, cbValue));
                    break;

                case REG_QWORD:
                    if (cbValue == sizeof(uint64_t))
                    {
                        RTStrFormatU32(szValue, sizeof(szValue), *(uint32_t const *)pvValue, 10, 0, 0, 0);
                        pszValue = szValue;
                    }
                    else
                        LogRel(("VBoxGuest: Registry value '%ls' has wrong length for REG_DWORD: %#x\n", pwszValueName, cbValue));
                    break;

                default:
                    LogRel(("VBoxGuest: Ignoring registry value '%ls': Unsupported type %#x\n", pwszValueName, uValueType));
                    break;
            }
            if (pszValue)
            {
                /*
                 * Process it.
                 */
                PVBOXGUESTDEVEXT pDevExt = (PVBOXGUESTDEVEXT)pvUser;
                VGDrvCommonProcessOption(pDevExt, szValueName, pszValue);
                if (pszFree)
                    RTStrFree(pszFree);
            }
        }
    }
    else if (cch > 0)
        LogRel(("VBoxGuest: Ignoring registery value '%ls': name too long\n", pwszValueName));
    else
        LogRel(("VBoxGuest: Ignoring registery value with bad name\n", pwszValueName));
    NOREF(pvEntryCtx);
    return STATUS_SUCCESS;
}


/**
 * Reads configuration from the registry and guest properties.
 *
 * We ignore failures and instead preserve existing configuration values.
 *
 * Thie routine will block.
 *
 * @param   pDevExt             The device extension.
 */
static void vgdrvNtReadConfiguration(PVBOXGUESTDEVEXTWIN pDevExt)
{
    /*
     * First the registry.
     *
     * Note! RTL_QUERY_REGISTRY_NOEXPAND is sensible (no environment) and also necessary to
     *       avoid crash on NT 3.1 because RtlExpandEnvironmentStrings_U thinks its in ring-3
     *       and tries to get the default heap from the PEB via the TEB.  No TEB in ring-0.
     */
    RTL_QUERY_REGISTRY_TABLE aQuery[2];
    RT_ZERO(aQuery);
    aQuery[0].QueryRoutine = vgdrvNtRegistryEnumCallback;
    aQuery[0].Flags        = RTL_QUERY_REGISTRY_NOEXPAND;
    aQuery[0].Name         = NULL;
    aQuery[0].EntryContext = NULL;
    aQuery[0].DefaultType  = REG_NONE;
    NTSTATUS rcNt = RtlQueryRegistryValues(RTL_REGISTRY_SERVICES, L"VBoxGuest", &aQuery[0], pDevExt, NULL /*pwszzEnv*/);
    if (!NT_SUCCESS(rcNt))
        LogRel(("VBoxGuest: RtlQueryRegistryValues failed: %#x\n", rcNt));

    /*
     * Read configuration from the host.
     */
    VGDrvCommonProcessOptionsFromHost(&pDevExt->Core);
}

#ifdef VBOX_STRICT

/**
 * A quick implementation of AtomicTestAndClear for uint32_t and multiple bits.
 */
static uint32_t vgdrvNtAtomicBitsTestAndClear(void *pu32Bits, uint32_t u32Mask)
{
    AssertPtrReturn(pu32Bits, 0);
    LogFlowFunc(("*pu32Bits=%#x, u32Mask=%#x\n", *(uint32_t *)pu32Bits, u32Mask));
    uint32_t u32Result = 0;
    uint32_t u32WorkingMask = u32Mask;
    int iBitOffset = ASMBitFirstSetU32 (u32WorkingMask);

    while (iBitOffset > 0)
    {
        bool fSet = ASMAtomicBitTestAndClear(pu32Bits, iBitOffset - 1);
        if (fSet)
            u32Result |= 1 << (iBitOffset - 1);
        u32WorkingMask &= ~(1 << (iBitOffset - 1));
        iBitOffset = ASMBitFirstSetU32 (u32WorkingMask);
    }
    LogFlowFunc(("Returning %#x\n", u32Result));
    return u32Result;
}


static void vgdrvNtTestAtomicTestAndClearBitsU32(uint32_t u32Mask, uint32_t u32Bits, uint32_t u32Exp)
{
    ULONG u32Bits2 = u32Bits;
    uint32_t u32Result = vgdrvNtAtomicBitsTestAndClear(&u32Bits2, u32Mask);
    if (   u32Result != u32Exp
        || (u32Bits2 & u32Mask)
        || (u32Bits2 & u32Result)
        || ((u32Bits2 | u32Result) != u32Bits)
       )
        AssertLogRelMsgFailed(("TEST FAILED: u32Mask=%#x, u32Bits (before)=%#x, u32Bits (after)=%#x, u32Result=%#x, u32Exp=%#x\n",
                               u32Mask, u32Bits, u32Bits2, u32Result));
}


static void vgdrvNtDoTests(void)
{
    vgdrvNtTestAtomicTestAndClearBitsU32(0x00, 0x23, 0);
    vgdrvNtTestAtomicTestAndClearBitsU32(0x11, 0, 0);
    vgdrvNtTestAtomicTestAndClearBitsU32(0x11, 0x22, 0);
    vgdrvNtTestAtomicTestAndClearBitsU32(0x11, 0x23, 0x1);
    vgdrvNtTestAtomicTestAndClearBitsU32(0x11, 0x32, 0x10);
    vgdrvNtTestAtomicTestAndClearBitsU32(0x22, 0x23, 0x22);
}

#endif /* VBOX_STRICT */

#ifdef VBOX_WITH_DPC_LATENCY_CHECKER

/*
 * DPC latency checker.
 */

/**
 * One DPC latency sample.
 */
typedef struct DPCSAMPLE
{
    LARGE_INTEGER   PerfDelta;
    LARGE_INTEGER   PerfCounter;
    LARGE_INTEGER   PerfFrequency;
    uint64_t        u64TSC;
} DPCSAMPLE;
AssertCompileSize(DPCSAMPLE, 4*8);

/**
 * The DPC latency measurement workset.
 */
typedef struct DPCDATA
{
    KDPC            Dpc;
    KTIMER          Timer;
    KSPIN_LOCK      SpinLock;

    ULONG           ulTimerRes;

    bool volatile   fFinished;

    /** The timer interval (relative). */
    LARGE_INTEGER   DueTime;

    LARGE_INTEGER   PerfCounterPrev;

    /** Align the sample array on a 64 byte boundrary just for the off chance
     * that we'll get cache line aligned memory backing this structure. */
    uint32_t        auPadding[ARCH_BITS == 32 ? 5 : 7];

    int             cSamples;
    DPCSAMPLE       aSamples[8192];
} DPCDATA;

AssertCompileMemberAlignment(DPCDATA, aSamples, 64);

/**
 * DPC callback routine for the DPC latency measurement code.
 *
 * @param   pDpc                The DPC, not used.
 * @param   pvDeferredContext   Pointer to the DPCDATA.
 * @param   SystemArgument1     System use, ignored.
 * @param   SystemArgument2     System use, ignored.
 */
static VOID vgdrvNtDpcLatencyCallback(PKDPC pDpc, PVOID pvDeferredContext, PVOID SystemArgument1, PVOID SystemArgument2)
{
    DPCDATA *pData = (DPCDATA *)pvDeferredContext;
    RT_NOREF(pDpc, SystemArgument1, SystemArgument2);

    KeAcquireSpinLockAtDpcLevel(&pData->SpinLock);

    if (pData->cSamples >= RT_ELEMENTS(pData->aSamples))
        pData->fFinished = true;
    else
    {
        DPCSAMPLE *pSample = &pData->aSamples[pData->cSamples++];

        pSample->u64TSC = ASMReadTSC();
        pSample->PerfCounter = KeQueryPerformanceCounter(&pSample->PerfFrequency);
        pSample->PerfDelta.QuadPart = pSample->PerfCounter.QuadPart - pData->PerfCounterPrev.QuadPart;

        pData->PerfCounterPrev.QuadPart = pSample->PerfCounter.QuadPart;

        KeSetTimer(&pData->Timer, pData->DueTime, &pData->Dpc);
    }

    KeReleaseSpinLockFromDpcLevel(&pData->SpinLock);
}


/**
 * Handles the DPC latency checker request.
 *
 * @returns VBox status code.
 */
int VGDrvNtIOCtl_DpcLatencyChecker(void)
{
    /*
     * Allocate a block of non paged memory for samples and related data.
     */
    DPCDATA *pData = (DPCDATA *)RTMemAlloc(sizeof(DPCDATA));
    if (!pData)
    {
        RTLogBackdoorPrintf("VBoxGuest: DPC: DPCDATA allocation failed.\n");
        return VERR_NO_MEMORY;
    }

    /*
     * Initialize the data.
     */
    KeInitializeDpc(&pData->Dpc, vgdrvNtDpcLatencyCallback, pData);
    KeInitializeTimer(&pData->Timer);
    KeInitializeSpinLock(&pData->SpinLock);

    pData->fFinished = false;
    pData->cSamples = 0;
    pData->PerfCounterPrev.QuadPart = 0;

    pData->ulTimerRes = ExSetTimerResolution(1000 * 10, 1);
    pData->DueTime.QuadPart = -(int64_t)pData->ulTimerRes / 10;

    /*
     * Start the DPC measurements and wait for a full set.
     */
    KeSetTimer(&pData->Timer, pData->DueTime, &pData->Dpc);

    while (!pData->fFinished)
    {
        LARGE_INTEGER Interval;
        Interval.QuadPart = -100 * 1000 * 10;
        KeDelayExecutionThread(KernelMode, TRUE, &Interval);
    }

    ExSetTimerResolution(0, 0);

    /*
     * Log everything to the host.
     */
    RTLogBackdoorPrintf("DPC: ulTimerRes = %d\n", pData->ulTimerRes);
    for (int i = 0; i < pData->cSamples; i++)
    {
        DPCSAMPLE *pSample = &pData->aSamples[i];

        RTLogBackdoorPrintf("[%d] pd %lld pc %lld pf %lld t %lld\n",
                            i,
                            pSample->PerfDelta.QuadPart,
                            pSample->PerfCounter.QuadPart,
                            pSample->PerfFrequency.QuadPart,
                            pSample->u64TSC);
    }

    RTMemFree(pData);
    return VINF_SUCCESS;
}

#endif /* VBOX_WITH_DPC_LATENCY_CHECKER */

