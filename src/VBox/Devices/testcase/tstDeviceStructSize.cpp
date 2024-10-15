/* $Id: tstDeviceStructSize.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * tstDeviceStructSize - testcase for check structure sizes/alignment
 *                       and to verify that HC and RC uses the same
 *                       representation of the structures.
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
#include <VBox/types.h>
#include <iprt/x86.h>

#define VBOX_WITH_HGCM                  /* grumble */
#define VBOX_DEVICE_STRUCT_TESTCASE

/* Check that important preprocessor macros does not get redefined: */
#include <VBox/cdefs.h>
#include <VBox/log.h>
#ifdef DEBUG
# define VBOX_DEVICE_STRUCT_TESTCASE_CHECK_DEBUG
#else
# undef  VBOX_DEVICE_STRUCT_TESTCASE_CHECK_DEBUG
#endif
#ifdef LOG_ENABLED
# define VBOX_DEVICE_STRUCT_TESTCASE_CHECK_LOG_ENABLED
#else
# undef  VBOX_DEVICE_STRUCT_TESTCASE_CHECK_LOG_ENABLED
#endif
#ifdef VBOX_STRICT
# define VBOX_DEVICE_STRUCT_TESTCASE_CHECK_VBOX_STRICT
#else
# undef  VBOX_DEVICE_STRUCT_TESTCASE_CHECK_VBOX_STRICT
#endif
#ifdef RT_STRICT
# define VBOX_DEVICE_STRUCT_TESTCASE_CHECK_RT_STRICT
#else
# undef  VBOX_DEVICE_STRUCT_TESTCASE_CHECK_RT_STRICT
#endif

/* The structures we're checking: */
#undef LOG_GROUP
#include "../Bus/DevPciInternal.h"
#undef LOG_GROUP
#include "../Graphics/DevVGA.cpp"
#undef LOG_GROUP
#include "../Input/DevPS2.h"
#ifdef VBOX_WITH_E1000
# undef LOG_GROUP
# include "../Network/DevE1000.cpp"
#endif
#undef LOG_GROUP
#include "../Network/DevPCNet.cpp"
#undef LOG_GROUP
#include "../PC/DevACPI.cpp"
#undef LOG_GROUP
#include "../PC/DevPIC.cpp"
#undef LOG_GROUP
#include "../PC/DevPit-i8254.cpp"
#undef LOG_GROUP
#include "../PC/DevRTC.cpp"
# undef LOG_GROUP
# include "../../VMM/VMMR3/APIC.cpp"
#undef LOG_GROUP
#include "../PC/DevIoApic.cpp"
#undef LOG_GROUP
#include "../PC/DevHPET.cpp"
#undef LOG_GROUP
#include "../PC/DevDMA.cpp"
#undef LOG_GROUP
#include "../EFI/DevSmc.cpp"
#undef LOG_GROUP
#include "../Storage/DevATA.cpp"
#ifdef VBOX_WITH_USB
# undef LOG_GROUP
# include "../USB/DevOHCI.cpp"
# ifdef VBOX_WITH_EHCI_IMPL
#  undef LOG_GROUP
#  include "../USB/DevEHCI.cpp"
# endif
# ifdef VBOX_WITH_XHCI_IMPL
#  undef LOG_GROUP
#  include "../USB/DevXHCI.cpp"
# endif
#endif
#undef LOG_GROUP
#include "../VMMDev/VMMDev.cpp"
#undef LOG_GROUP
#include "../Parallel/DevParallel.cpp"
#undef LOG_GROUP
#include "../Serial/DevSerial.cpp"
#undef LOG_GROUP
#include "../Serial/DevOxPcie958.cpp"
#ifdef VBOX_WITH_AHCI
# undef LOG_GROUP
# include "../Storage/DevAHCI.cpp"
#endif
#ifdef VBOX_WITH_BUSLOGIC
# undef LOG_GROUP
# include "../Storage/DevBusLogic.cpp"
#endif
#ifdef VBOX_WITH_LSILOGIC
# undef LOG_GROUP
# include "../Storage/DevLsiLogicSCSI.cpp"
#endif
#ifdef VBOX_WITH_NVME_IMPL
# undef LOG_GROUP
# include "../Storage/DevNVMe.cpp"
#endif

#ifdef VBOX_WITH_PCI_PASSTHROUGH_IMPL
# undef LOG_GROUP
# include "../Bus/DevPciRaw.cpp"
#endif

#ifdef VBOX_WITH_IOMMU_AMD
# undef LOG_GROUP
# include "../Bus/DevIommuAmd.cpp"
#endif
#ifdef VBOX_WITH_IOMMU_INTEL
# undef LOG_GROUP
# include "../Bus/DevIommuIntel.cpp"
#endif

#include <VBox/vmm/pdmaudioifs.h>

#undef LOG_GROUP
#include "../Audio/DevIchAc97.cpp"
#undef LOG_GROUP
#include "../Audio/DevHda.h"


/* Check that important preprocessor macros didn't get redefined: */
#if defined(DEBUG)       != defined(VBOX_DEVICE_STRUCT_TESTCASE_CHECK_DEBUG)
# error "DEBUG was modified!  This may throw off structure tests."
#endif
#if defined(LOG_ENABLED) != defined(VBOX_DEVICE_STRUCT_TESTCASE_CHECK_LOG_ENABLED)
# error "LOG_ENABLED was modified!  This may throw off structure tests."
#endif
#if defined(RT_STRICT)   != defined(VBOX_DEVICE_STRUCT_TESTCASE_CHECK_RT_STRICT)
# error "RT_STRICT was modified!  This may throw off structure tests."
#endif
#if defined(VBOX_STRICT) != defined(VBOX_DEVICE_STRUCT_TESTCASE_CHECK_VBOX_STRICT)
# error "VBOX_STRICT was modified!  This may throw off structure tests."
#endif


#include <iprt/stream.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/**
 * Checks the offset of a data member.
 * @param   type    Type.
 * @param   off     Correct offset.
 * @param   m       Member name.
 */
#define CHECK_OFF(type, off, m) \
    do { \
        if (off != RT_OFFSETOF(type, m)) \
        { \
            RTPrintf("tstDeviceStructSize: Error! %#010x %s  Member offset wrong by %d (should be %d -- but is %d)\n", \
                     RT_OFFSETOF(type, m), #type "." #m, off - RT_OFFSETOF(type, m), off, RT_OFFSETOF(type, m)); \
            rc++; \
        } \
        else  \
            RTPrintf("%#08x (%d) %s\n", RT_OFFSETOF(type, m), RT_OFFSETOF(type, m), #type "." #m); \
    } while (0)

/**
 * Checks the size of type.
 * @param   type    Type.
 * @param   size    Correct size.
 */
#define CHECK_SIZE(type, size) \
    do { \
        if (size != sizeof(type)) \
        { \
            RTPrintf("tstDeviceStructSize: Error! sizeof(%s): %#x (%d)  Size wrong by %d (should be %d -- but is %d)\n", \
                     #type, (int)sizeof(type), (int)sizeof(type), (int)sizeof(type) - (int)size, (int)size, (int)sizeof(type)); \
            rc++; \
        } \
        else \
            RTPrintf("tstDeviceStructSize: info: sizeof(%s): %#x (%d)\n", #type, (int)sizeof(type), (int)sizeof(type)); \
    } while (0)

/**
 * Checks the alignment of a struct member.
 */
#define CHECK_MEMBER_ALIGNMENT(strct, member, align) \
    do \
    { \
        if (RT_OFFSETOF(strct, member) & ((align) - 1) ) \
        { \
            RTPrintf("tstDeviceStructSize: error! %s::%s offset=%#x (%u) expected alignment %#x, meaning %#x (%u) off\n", \
                     #strct, #member, \
                     (unsigned)RT_OFFSETOF(strct, member), \
                     (unsigned)RT_OFFSETOF(strct, member), \
                     (unsigned)(align), \
                     (unsigned)(((align) - RT_OFFSETOF(strct, member)) & ((align) - 1)), \
                     (unsigned)(((align) - RT_OFFSETOF(strct, member)) & ((align) - 1)) ); \
            rc++; \
        } \
    } while (0)

/**
 * Checks that the size of a type is aligned correctly.
 */
#define CHECK_SIZE_ALIGNMENT(type, align) \
    do { \
        if (RT_ALIGN_Z(sizeof(type), (align)) != sizeof(type)) \
        { \
            RTPrintf("tstDeviceStructSize: error! %s size=%#x (%u), align=%#x %#x (%u) bytes off\n", \
                     #type, \
                     (unsigned)sizeof(type), \
                     (unsigned)sizeof(type), \
                     (align), \
                     (unsigned)RT_ALIGN_Z(sizeof(type), align) - (unsigned)sizeof(type), \
                     (unsigned)RT_ALIGN_Z(sizeof(type), align) - (unsigned)sizeof(type)); \
            rc++; \
        } \
    } while (0)

/**
 * Checks that a internal struct padding is big enough.
 */
#define CHECK_PADDING(strct, member, align) \
    do \
    { \
        strct *p = NULL; NOREF(p); \
        if (sizeof(p->member.s) > sizeof(p->member.padding)) \
        { \
            RTPrintf("tstDeviceStructSize: error! padding of %s::%s is too small, padding=%d struct=%d correct=%d\n", #strct, #member, \
                     (int)sizeof(p->member.padding), (int)sizeof(p->member.s), (int)RT_ALIGN_Z(sizeof(p->member.s), (align))); \
            rc++; \
        } \
        else if (RT_ALIGN_Z(sizeof(p->member.padding), (align)) != sizeof(p->member.padding)) \
        { \
            RTPrintf("tstDeviceStructSize: error! padding of %s::%s is misaligned, padding=%d correct=%d\n", #strct, #member, \
                     (int)sizeof(p->member.padding), (int)RT_ALIGN_Z(sizeof(p->member.s), (align))); \
            rc++; \
        } \
    } while (0)

/**
 * Checks that a internal struct padding is big enough.
 */
#define CHECK_PADDING2(strct) \
    do \
    { \
        strct *p = NULL; NOREF(p); \
        if (sizeof(p->s) > sizeof(p->padding)) \
        { \
            RTPrintf("tstDeviceStructSize: error! padding of %s is too small, padding=%d struct=%d correct=%d\n", #strct, \
                     (int)sizeof(p->padding), (int)sizeof(p->s), (int)RT_ALIGN_Z(sizeof(p->s), 32)); \
            rc++; \
        } \
    } while (0)

/**
 * Checks that a internal struct padding is big enough.
 */
#define CHECK_PADDING3(strct, member, pad_member) \
    do \
    { \
        strct *p = NULL; NOREF(p); \
        if (sizeof(p->member) > sizeof(p->pad_member)) \
        { \
            RTPrintf("tstDeviceStructSize: error! padding of %s::%s is too small, padding=%d struct=%d\n", #strct, #member, \
                     (int)sizeof(p->pad_member), (int)sizeof(p->member)); \
            rc++; \
        } \
    } while (0)

/**
 * Prints the offset of a struct member.
 */
#define PRINT_OFFSET(strct, member) \
    do \
    { \
        RTPrintf("tstDeviceStructSize: info: %s::%s offset %d sizeof %d\n",  #strct, #member, (int)RT_OFFSETOF(strct, member), (int)RT_SIZEOFMEMB(strct, member)); \
    } while (0)


int main()
{
    int rc = 0;
    RTPrintf("tstDeviceStructSize: TESTING\n");

    /* Assert sanity */
    CHECK_SIZE(uint128_t, 128/8);
    CHECK_SIZE(int128_t, 128/8);
    CHECK_SIZE(uint64_t, 64/8);
    CHECK_SIZE(int64_t, 64/8);
    CHECK_SIZE(uint32_t, 32/8);
    CHECK_SIZE(int32_t, 32/8);
    CHECK_SIZE(uint16_t, 16/8);
    CHECK_SIZE(int16_t, 16/8);
    CHECK_SIZE(uint8_t, 8/8);
    CHECK_SIZE(int8_t, 8/8);

    /* Basic alignment checks. */
    CHECK_MEMBER_ALIGNMENT(PDMDEVINS, achInstanceData, 64);
    CHECK_MEMBER_ALIGNMENT(PDMPCIDEV, Int.s, 16);
    CHECK_MEMBER_ALIGNMENT(PDMPCIDEV, Int.s.aIORegions, 16);

    /*
     * Misc alignment checks (keep this somewhat alphabetical).
     */
    CHECK_MEMBER_ALIGNMENT(AC97STATE, CritSect, 8);

    CHECK_MEMBER_ALIGNMENT(AHCI, lock, 8);
    CHECK_MEMBER_ALIGNMENT(AHCI, aPorts[0], 8);
    CHECK_MEMBER_ALIGNMENT(AHCIR3, aPorts[0], 8);

    CHECK_MEMBER_ALIGNMENT(ATADEVSTATE, cTotalSectors, 8);
    CHECK_MEMBER_ALIGNMENT(ATADEVSTATE, StatATADMA, 8);
    CHECK_MEMBER_ALIGNMENT(ATADEVSTATE, StatReads, 8);
    CHECK_MEMBER_ALIGNMENT(ATACONTROLLER, lock, 8);
    CHECK_MEMBER_ALIGNMENT(ATACONTROLLER, StatAsyncOps, 8);
    CHECK_MEMBER_ALIGNMENT(BUSLOGIC, CritSectIntr, 8);
#ifdef VBOX_WITH_STATISTICS
    CHECK_MEMBER_ALIGNMENT(DEVPIC, StatSetIrqRZ, 8);
#endif
#ifdef VBOX_WITH_E1000
    CHECK_MEMBER_ALIGNMENT(E1KSTATE, cs, 8);
    CHECK_MEMBER_ALIGNMENT(E1KSTATE, csRx, 8);
    CHECK_MEMBER_ALIGNMENT(E1KSTATE, StatReceiveBytes, 8);
#endif
    //CHECK_MEMBER_ALIGNMENT(E1KSTATE, csTx, 8);
#ifdef VBOX_WITH_USB
# ifdef VBOX_WITH_EHCI_IMPL
    CHECK_MEMBER_ALIGNMENT(EHCI, RootHub, 8);
# endif
# ifdef VBOX_WITH_XHCI_IMPL
    CHECK_MEMBER_ALIGNMENT(XHCI, aPorts, 8);
    CHECK_MEMBER_ALIGNMENT(XHCI, aInterrupters, 8);
    CHECK_MEMBER_ALIGNMENT(XHCI, aInterrupters[0].lock, 8);
    CHECK_MEMBER_ALIGNMENT(XHCI, aInterrupters[1].lock, 8);
    CHECK_MEMBER_ALIGNMENT(XHCI, cmdr_dqp, 8);
    CHECK_MEMBER_ALIGNMENT(XHCI, hMmio, 8);
#  ifdef VBOX_WITH_STATISTICS
    CHECK_MEMBER_ALIGNMENT(XHCI, StatErrorIsocUrbs, 8);
    CHECK_MEMBER_ALIGNMENT(XHCI, StatIntrsCleared, 8);
#  endif
# endif
#endif
    CHECK_MEMBER_ALIGNMENT(E1KSTATE, StatReceiveBytes, 8);
    CHECK_MEMBER_ALIGNMENT(IOAPIC, au64RedirTable, 8);
# ifdef VBOX_WITH_STATISTICS
    CHECK_MEMBER_ALIGNMENT(IOAPIC, StatMmioReadRZ, 8);
# endif
    CHECK_MEMBER_ALIGNMENT(LSILOGICSCSI, aMessage, 8);
    CHECK_MEMBER_ALIGNMENT(LSILOGICSCSI, ReplyPostQueueCritSect, 8);
    CHECK_MEMBER_ALIGNMENT(LSILOGICSCSI, ReplyFreeQueueCritSect, 8);
    CHECK_MEMBER_ALIGNMENT(LSILOGICSCSI, uReplyFreeQueueNextEntryFreeWrite, 8);
#ifdef VBOX_WITH_USB
    CHECK_MEMBER_ALIGNMENT(OHCI, RootHub, 8);
# ifdef VBOX_WITH_STATISTICS
    CHECK_MEMBER_ALIGNMENT(OHCI, StatCanceledIsocUrbs, 8);
# endif
#endif
    CHECK_MEMBER_ALIGNMENT(DEVPCIBUS, apDevices, 64);
    CHECK_MEMBER_ALIGNMENT(DEVPCIROOT, auPciApicIrqLevels, 16);
    CHECK_MEMBER_ALIGNMENT(DEVPCIROOT, u.Piix3.auPciLegacyIrqLevels, 16);
    CHECK_MEMBER_ALIGNMENT(PCNETSTATE, u64LastPoll, 8);
    CHECK_MEMBER_ALIGNMENT(PCNETSTATE, CritSect, 8);
    CHECK_MEMBER_ALIGNMENT(PCNETSTATE, StatReceiveBytes, 8);
#ifdef VBOX_WITH_STATISTICS
    CHECK_MEMBER_ALIGNMENT(PCNETSTATE, StatMMIOReadRZ, 8);
#endif
    CHECK_MEMBER_ALIGNMENT(PITSTATE, StatPITIrq, 8);
    CHECK_MEMBER_ALIGNMENT(DEVSERIAL, UartCore, 8);
    CHECK_MEMBER_ALIGNMENT(UARTCORE, CritSect, 8);
#ifdef VBOX_WITH_VMSVGA
    CHECK_SIZE(VMSVGAState, RT_ALIGN_Z(sizeof(VMSVGAState), 8));
    CHECK_MEMBER_ALIGNMENT(VGASTATE, svga, 8);
    CHECK_MEMBER_ALIGNMENT(VGASTATE, svga.au32ScratchRegion, 8);
    CHECK_MEMBER_ALIGNMENT(VGASTATE, svga.StatRegBitsPerPixelWr, 8);
#endif
    CHECK_MEMBER_ALIGNMENT(VGASTATE, cMonitors, 8);
    CHECK_MEMBER_ALIGNMENT(VGASTATE, GCPhysVRAM, 8);
    CHECK_MEMBER_ALIGNMENT(VGASTATE, CritSect, 8);
    CHECK_MEMBER_ALIGNMENT(VGASTATE, StatRZMemoryRead, 8);
    CHECK_MEMBER_ALIGNMENT(VGASTATE, CritSectIRQ, 8);
    CHECK_MEMBER_ALIGNMENT(VGASTATE, bmDirtyBitmap, 8);
    CHECK_MEMBER_ALIGNMENT(VGASTATE, pciRegions, 8);
    CHECK_MEMBER_ALIGNMENT(VMMDEV, CritSect, 8);
#ifdef VBOX_WITH_PCI_PASSTHROUGH_IMPL
    CHECK_MEMBER_ALIGNMENT(PCIRAWSENDREQ, u.aGetRegionInfo.u64RegionSize, 8);
#endif
#ifdef VBOX_WITH_IOMMU_AMD
    CHECK_MEMBER_ALIGNMENT(IOMMU, IommuBar, 8);
    CHECK_MEMBER_ALIGNMENT(IOMMU, aDevTabBaseAddrs, 8);
    CHECK_MEMBER_ALIGNMENT(IOMMU, CmdBufHeadPtr, 8);
    CHECK_MEMBER_ALIGNMENT(IOMMU, Status, 8);
# ifdef VBOX_WITH_STATISTICS
    CHECK_MEMBER_ALIGNMENT(IOMMU, StatMmioReadR3, 8);
# endif
#endif
#ifdef VBOX_WITH_IOMMU_INTEL
    CHECK_MEMBER_ALIGNMENT(DMAR, abRegs0, 8);
    CHECK_MEMBER_ALIGNMENT(DMAR, abRegs1, 8);
    CHECK_MEMBER_ALIGNMENT(DMAR, uIrtaReg, 8);
    CHECK_MEMBER_ALIGNMENT(DMAR, uRtaddrReg, 8);
    CHECK_MEMBER_ALIGNMENT(DMAR, hEvtInvQueue, 8);
# ifdef VBOX_WITH_STATISTICS
    CHECK_MEMBER_ALIGNMENT(DMAR, StatMmioReadR3, 8);
    CHECK_MEMBER_ALIGNMENT(DMAR, StatPasidDevtlbInvDsc, 8);
# endif
#endif

#ifdef VBOX_WITH_RAW_MODE
    /*
     * Compare HC and RC.
     */
    RTPrintf("tstDeviceStructSize: Comparing HC and RC...\n");
# include "tstDeviceStructSizeRC.h"
#endif

    /*
     * Report result.
     */
    if (rc)
        RTPrintf("tstDeviceStructSize: FAILURE - %d errors\n", rc);
    else
        RTPrintf("tstDeviceStructSize: SUCCESS\n");
    return rc;
}
