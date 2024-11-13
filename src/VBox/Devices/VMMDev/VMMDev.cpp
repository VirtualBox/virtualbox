/* $Id: VMMDev.cpp 107002 2024-11-13 12:21:13Z brent.paulson@oracle.com $ */
/** @file
 * VMMDev - Guest <-> VMM/Host communication device.
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

/** @page pg_vmmdev   The VMM Device.
 *
 * The VMM device is a custom hardware device emulation for communicating with
 * the guest additions.
 *
 * Whenever host wants to inform guest about something an IRQ notification will
 * be raised.
 *
 * VMMDev PDM interface will contain the guest notification method.
 *
 * There is a 32 bit event mask which will be read by guest on an interrupt.  A
 * non zero bit in the mask means that the specific event occurred and requires
 * processing on guest side.
 *
 * After reading the event mask guest must issue a generic request
 * AcknowlegdeEvents.
 *
 * IRQ line is set to 1 (request) if there are unprocessed events, that is the
 * event mask is not zero.
 *
 * After receiving an interrupt and checking event mask, the guest must process
 * events using the event specific mechanism.
 *
 * That is if mouse capabilities were changed, guest will use
 * VMMDev_GetMouseStatus generic request.
 *
 * Event mask is only a set of flags indicating that guest must proceed with a
 * procedure.
 *
 * Unsupported events are therefore ignored. The guest additions must inform
 * host which events they want to receive, to avoid unnecessary IRQ processing.
 * By default no events are signalled to guest.
 *
 * This seems to be fast method. It requires only one context switch for an
 * event processing.
 *
 *
 * @section sec_vmmdev_heartbeat    Heartbeat
 *
 * The heartbeat is a feature to monitor whether the guest OS is hung or not.
 *
 * The main kernel component of the guest additions, VBoxGuest, sets up a timer
 * at a frequency returned by VMMDevReq_HeartbeatConfigure
 * (VMMDevReqHeartbeat::cNsInterval, VMMDEV::cNsHeartbeatInterval) and performs
 * a VMMDevReq_GuestHeartbeat request every time the timer ticks.
 *
 * The host side (VMMDev) arms a timer with a more distant deadline
 * (VMMDEV::cNsHeartbeatTimeout), twice cNsHeartbeatInterval by default.  Each
 * time a VMMDevReq_GuestHeartbeat request comes in, the timer is rearmed with
 * the same relative deadline.  So, as long as VMMDevReq_GuestHeartbeat comes
 * when they should, the host timer will never fire.
 *
 * When the timer fires, we consider the guest as hung / flatlined / dead.
 * Currently we only LogRel that, but it's easy to extend this with an event in
 * Main API.
 *
 * Should the guest reawaken at some later point, we LogRel that event and
 * continue as normal.  Again something which would merit an API event.
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
/* Enable dev_vmm Log3 statements to get IRQ-related logging. */
#define LOG_GROUP LOG_GROUP_DEV_VMM
#include <VBox/AssertGuest.h>
#include <VBox/VMMDev.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/mm.h>
#include <VBox/log.h>
#include <VBox/param.h>
#include <iprt/path.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <VBox/vmm/pgm.h>
#include <VBox/err.h>
#include <VBox/dbg.h>
#include <VBox/version.h>

#include <iprt/asm.h>
#if defined(RT_ARCH_AMD64) || defined(RT_ARCH_X86)
# include <iprt/asm-amd64-x86.h> /* ASMReadTsc */
#endif
#include <iprt/assert.h>
#include <iprt/buildconfig.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/time.h>
#ifndef IN_RC
# include <iprt/mem.h>
# include <iprt/memsafer.h>
#endif
#ifdef IN_RING3
# include <iprt/uuid.h>
#endif

#include "VMMDevState.h"
#ifdef VBOX_WITH_HGCM
# include "VMMDevHGCM.h"
#endif
#ifndef VBOX_WITHOUT_TESTING_FEATURES
# include "VMMDevTesting.h"
#endif


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#define VMMDEV_INTERFACE_VERSION_IS_1_03(s) \
    (   RT_HIWORD((s)->guestInfo.interfaceVersion) == 1 \
     && RT_LOWORD((s)->guestInfo.interfaceVersion) == 3 )

#define VMMDEV_INTERFACE_VERSION_IS_OK(additionsVersion) \
      (   RT_HIWORD(additionsVersion) == RT_HIWORD(VMMDEV_VERSION) \
       && RT_LOWORD(additionsVersion) <= RT_LOWORD(VMMDEV_VERSION) )

#define VMMDEV_INTERFACE_VERSION_IS_OLD(additionsVersion) \
      (   (RT_HIWORD(additionsVersion) < RT_HIWORD(VMMDEV_VERSION) \
       || (   RT_HIWORD(additionsVersion) == RT_HIWORD(VMMDEV_VERSION) \
           && RT_LOWORD(additionsVersion) <= RT_LOWORD(VMMDEV_VERSION) ) )

#define VMMDEV_INTERFACE_VERSION_IS_TOO_OLD(additionsVersion) \
      ( RT_HIWORD(additionsVersion) < RT_HIWORD(VMMDEV_VERSION) )

#define VMMDEV_INTERFACE_VERSION_IS_NEW(additionsVersion) \
      (   RT_HIWORD(additionsVersion) > RT_HIWORD(VMMDEV_VERSION) \
       || (   RT_HIWORD(additionsVersion) == RT_HIWORD(VMMDEV_VERSION) \
           && RT_LOWORD(additionsVersion) >  RT_LOWORD(VMMDEV_VERSION) ) )

/** Default interval in nanoseconds between guest heartbeats.
 *  Used when no HeartbeatInterval is set in CFGM and for setting
 *  HB check timer if the guest's heartbeat frequency is less than 1Hz. */
#define VMMDEV_HEARTBEAT_DEFAULT_INTERVAL                       (2U*RT_NS_1SEC_64)


#ifndef VBOX_DEVICE_STRUCT_TESTCASE
#ifdef IN_RING3

/** DISPLAYCHANGEDATA field descriptors for the v18+ saved state. */
static SSMFIELD const g_aSSMDISPLAYCHANGEDATAStateFields[] =
{
    SSMFIELD_ENTRY(DISPLAYCHANGEDATA, iCurrentMonitor),
    SSMFIELD_ENTRY(DISPLAYCHANGEDATA, fGuestSentChangeEventAck),
    SSMFIELD_ENTRY(DISPLAYCHANGEDATA, afAlignment),
    SSMFIELD_ENTRY(DISPLAYCHANGEDATA, aRequests),
    SSMFIELD_ENTRY_TERM()
};

/* -=-=-=-=- Misc Helpers -=-=-=-=- */

/**
 * Log information about the Guest Additions.
 *
 * @param   pGuestInfo  The information we've got from the Guest Additions driver.
 */
static void vmmdevLogGuestOsInfo(VBoxGuestInfo *pGuestInfo)
{
    const char *pszOs;
    switch (pGuestInfo->osType & ~VBOXOSTYPE_x64)
    {
        case VBOXOSTYPE_DOS:                              pszOs = "DOS";            break;
        case VBOXOSTYPE_Win31:                            pszOs = "Windows 3.1";    break;
        case VBOXOSTYPE_Win9x:                            pszOs = "Windows 9x";     break;
        case VBOXOSTYPE_Win95:                            pszOs = "Windows 95";     break;
        case VBOXOSTYPE_Win98:                            pszOs = "Windows 98";     break;
        case VBOXOSTYPE_WinMe:                            pszOs = "Windows Me";     break;
        case VBOXOSTYPE_WinNT:                            pszOs = "Windows NT";     break;
        case VBOXOSTYPE_WinNT3x:                          pszOs = "Windows NT 3.x"; break;
        case VBOXOSTYPE_WinNT4:                           pszOs = "Windows NT4";    break;
        case VBOXOSTYPE_Win2k:                            pszOs = "Windows 2k";     break;
        case VBOXOSTYPE_WinXP:                            pszOs = "Windows XP";     break;
        case VBOXOSTYPE_Win2k3:                           pszOs = "Windows 2k3";    break;
        case VBOXOSTYPE_WinVista:                         pszOs = "Windows Vista";  break;
        case VBOXOSTYPE_Win2k8:                           pszOs = "Windows 2k8";    break;
        case VBOXOSTYPE_Win7:                             pszOs = "Windows 7";      break;
        case VBOXOSTYPE_Win8:                             pszOs = "Windows 8";      break;
        case VBOXOSTYPE_Win2k12_x64 & ~VBOXOSTYPE_x64:    pszOs = "Windows 2k12";   break;
        case VBOXOSTYPE_Win81:                            pszOs = "Windows 8.1";    break;
        case VBOXOSTYPE_Win10:                            pszOs = "Windows 10";     break;
        case VBOXOSTYPE_Win2k16_x64 & ~VBOXOSTYPE_x64:    pszOs = "Windows 2k16";   break;
        case VBOXOSTYPE_Win2k19_x64 & ~VBOXOSTYPE_x64:    pszOs = "Windows 2k19";   break;
        case VBOXOSTYPE_Win2k22_x64 & ~VBOXOSTYPE_x64:    pszOs = "Windows 2k22";   break;
        case VBOXOSTYPE_Win2k25_x64 & ~VBOXOSTYPE_x64:    pszOs = "Windows 2k25";   break;
        case VBOXOSTYPE_Win11_x64 & ~VBOXOSTYPE_x64:      pszOs = "Windows 11";     break;
        case VBOXOSTYPE_OS2:                              pszOs = "OS/2";           break;
        case VBOXOSTYPE_OS2Warp3:                         pszOs = "OS/2 Warp 3";    break;
        case VBOXOSTYPE_OS2Warp4:                         pszOs = "OS/2 Warp 4";    break;
        case VBOXOSTYPE_OS2Warp45:                        pszOs = "OS/2 Warp 4.5";  break;
        case VBOXOSTYPE_ECS:                              pszOs = "OS/2 ECS";       break;
        case VBOXOSTYPE_ArcaOS:                           pszOs = "OS/2 ArcaOS";    break;
        case VBOXOSTYPE_OS21x:                            pszOs = "OS/2 2.1x";      break;
        case VBOXOSTYPE_Linux:                            pszOs = "Linux";          break;
        case VBOXOSTYPE_Linux22:                          pszOs = "Linux 2.2";      break;
        case VBOXOSTYPE_Linux24:                          pszOs = "Linux 2.4";      break;
        case VBOXOSTYPE_Linux26:                          pszOs = "Linux >= 2.6";   break;
        case VBOXOSTYPE_ArchLinux:                        pszOs = "ArchLinux";      break;
        case VBOXOSTYPE_Debian:                           pszOs = "Debian";         break;
        case VBOXOSTYPE_Debian31:                         pszOs = "Debian 3.1";     break;
        case VBOXOSTYPE_Debian4:                          pszOs = "Debian 4.0";     break;
        case VBOXOSTYPE_Debian5:                          pszOs = "Debian 5.0";     break;
        case VBOXOSTYPE_Debian6:                          pszOs = "Debian 6.0";     break;
        case VBOXOSTYPE_Debian7:                          pszOs = "Debian 7";       break;
        case VBOXOSTYPE_Debian8:                          pszOs = "Debian 8";       break;
        case VBOXOSTYPE_Debian9:                          pszOs = "Debian 9";       break;
        case VBOXOSTYPE_Debian10:                         pszOs = "Debian 10";      break;
        case VBOXOSTYPE_Debian11:                         pszOs = "Debian 11";      break;
        case VBOXOSTYPE_Debian12:                         pszOs = "Debian 12";      break;
        case VBOXOSTYPE_OpenSUSE:                         pszOs = "openSUSE";       break;
        case VBOXOSTYPE_OpenSUSE_Leap_x64 & ~VBOXOSTYPE_x64: pszOs = "openSUSE Leap";      break;
        case VBOXOSTYPE_OpenSUSE_Tumbleweed:              pszOs = "openSUSE Tumbleweed";   break;
        case VBOXOSTYPE_SUSE_LE:                          pszOs = "SUSE Linux Enterprise"; break;
        case VBOXOSTYPE_FedoraCore:                       pszOs = "Fedora";         break;
        case VBOXOSTYPE_Gentoo:                           pszOs = "Gentoo";         break;
        case VBOXOSTYPE_Mandriva:                         pszOs = "Mandriva";       break;
        case VBOXOSTYPE_OpenMandriva_Lx:                  pszOs = "OpenMandriva Lx"; break;
        case VBOXOSTYPE_PCLinuxOS:                        pszOs = "PCLinuxOS";      break;
        case VBOXOSTYPE_Mageia:                           pszOs = "Mageia";         break;
        case VBOXOSTYPE_RedHat:                           pszOs = "Red Hat";        break;
        case VBOXOSTYPE_RedHat3:                          pszOs = "Red Hat 3";      break;
        case VBOXOSTYPE_RedHat4:                          pszOs = "Red Hat 4";      break;
        case VBOXOSTYPE_RedHat5:                          pszOs = "Red Hat 5";      break;
        case VBOXOSTYPE_RedHat6:                          pszOs = "Red Hat 6";      break;
        case VBOXOSTYPE_RedHat7_x64 & ~VBOXOSTYPE_x64:    pszOs = "Red Hat 7";      break;
        case VBOXOSTYPE_RedHat8_x64 & ~VBOXOSTYPE_x64:    pszOs = "Red Hat 8";      break;
        case VBOXOSTYPE_RedHat9_x64 & ~VBOXOSTYPE_x64:    pszOs = "Red Hat 9";      break;
        case VBOXOSTYPE_Turbolinux:                       pszOs = "TurboLinux";     break;
        case VBOXOSTYPE_Ubuntu:                           pszOs = "Ubuntu";         break;
        case VBOXOSTYPE_Ubuntu10_LTS:                     pszOs = "Ubuntu 10.04 LTS"; break;
        case VBOXOSTYPE_Ubuntu10:                         pszOs = "Ubuntu 10.10";   break;
        case VBOXOSTYPE_Ubuntu11:                         pszOs = "Ubuntu 11.x";    break;
        case VBOXOSTYPE_Ubuntu12_LTS:                     pszOs = "Ubuntu 12.04 LTS"; break;
        case VBOXOSTYPE_Ubuntu12:                         pszOs = "Ubuntu 12.10";   break;
        case VBOXOSTYPE_Ubuntu13:                         pszOs = "Ubuntu 13.x";    break;
        case VBOXOSTYPE_Ubuntu14_LTS:                     pszOs = "Ubuntu 14.04 LTS"; break;
        case VBOXOSTYPE_Ubuntu14:                         pszOs = "Ubuntu 14.10";   break;
        case VBOXOSTYPE_Ubuntu15:                         pszOs = "Ubuntu 15.x";    break;
        case VBOXOSTYPE_Ubuntu16_LTS:                     pszOs = "Ubuntu 16.04 LTS"; break;
        case VBOXOSTYPE_Ubuntu16:                         pszOs = "Ubuntu 16.10";   break;
        case VBOXOSTYPE_Ubuntu17:                         pszOs = "Ubuntu 17.x";    break;
        case VBOXOSTYPE_Ubuntu18_LTS:                     pszOs = "Ubuntu 18.04 LTS"; break;
        case VBOXOSTYPE_Ubuntu18:                         pszOs = "Ubuntu 18.10";   break;
        case VBOXOSTYPE_Ubuntu19:                         pszOs = "Ubuntu 19.x";    break;
        case VBOXOSTYPE_Ubuntu20_LTS_x64 & ~VBOXOSTYPE_x64: pszOs = "Ubuntu 20.04 LTS"; break;
        case VBOXOSTYPE_Ubuntu20_x64 & ~VBOXOSTYPE_x64:   pszOs = "Ubuntu 20.10";   break;
        case VBOXOSTYPE_Ubuntu21_x64 & ~VBOXOSTYPE_x64:   pszOs = "Ubuntu 21.x";    break;
        case VBOXOSTYPE_Ubuntu22_LTS_x64 & ~VBOXOSTYPE_x64: pszOs = "Ubuntu 22.04 LTS"; break;
        case VBOXOSTYPE_Ubuntu22_x64 & ~VBOXOSTYPE_x64:   pszOs = "Ubuntu 22.10";   break;
        case VBOXOSTYPE_Ubuntu23_x64 & ~VBOXOSTYPE_x64:   pszOs = "Ubuntu 23.04";   break;
        case VBOXOSTYPE_Lubuntu:                          pszOs = "Lubuntu";        break;
        case VBOXOSTYPE_Xubuntu:                          pszOs = "Xubuntu";        break;
        case VBOXOSTYPE_Xandros:                          pszOs = "Xandros";        break;
        case VBOXOSTYPE_Oracle:                           pszOs = "Oracle Linux";   break;
        case VBOXOSTYPE_Oracle4:                          pszOs = "Oracle Linux 4"; break;
        case VBOXOSTYPE_Oracle5:                          pszOs = "Oracle Linux 5"; break;
        case VBOXOSTYPE_Oracle6:                          pszOs = "Oracle Linux 6"; break;
        case VBOXOSTYPE_Oracle7_x64 & ~VBOXOSTYPE_x64:    pszOs = "Oracle Linux 7"; break;
        case VBOXOSTYPE_Oracle8_x64 & ~VBOXOSTYPE_x64:    pszOs = "Oracle Linux 8"; break;
        case VBOXOSTYPE_Oracle9_x64 & ~VBOXOSTYPE_x64:    pszOs = "Oracle Linux 9"; break;
        case VBOXOSTYPE_FreeBSD:                          pszOs = "FreeBSD";        break;
        case VBOXOSTYPE_OpenBSD:                          pszOs = "OpenBSD";        break;
        case VBOXOSTYPE_NetBSD:                           pszOs = "NetBSD";         break;
        case VBOXOSTYPE_Netware:                          pszOs = "Netware";        break;
        case VBOXOSTYPE_Solaris:                          pszOs = "Solaris";        break;
        case VBOXOSTYPE_Solaris10U8_or_later:             pszOs = "Solaris 10";     break;
        case VBOXOSTYPE_OpenSolaris:                      pszOs = "OpenSolaris";    break;
        case VBOXOSTYPE_Solaris11_x64 & ~VBOXOSTYPE_x64:  pszOs = "Solaris 11";     break;
        case VBOXOSTYPE_MacOS:                            pszOs = "Mac OS X";       break;
        case VBOXOSTYPE_MacOS106:                         pszOs = "Mac OS X 10.6";  break;
        case VBOXOSTYPE_MacOS107_x64 & ~VBOXOSTYPE_x64:   pszOs = "Mac OS X 10.7";  break;
        case VBOXOSTYPE_MacOS108_x64 & ~VBOXOSTYPE_x64:   pszOs = "Mac OS X 10.8";  break;
        case VBOXOSTYPE_MacOS109_x64 & ~VBOXOSTYPE_x64:   pszOs = "Mac OS X 10.9";  break;
        case VBOXOSTYPE_MacOS1010_x64 & ~VBOXOSTYPE_x64:  pszOs = "Mac OS X 10.10"; break;
        case VBOXOSTYPE_MacOS1011_x64 & ~VBOXOSTYPE_x64:  pszOs = "Mac OS X 10.11"; break;
        case VBOXOSTYPE_MacOS1012_x64 & ~VBOXOSTYPE_x64:  pszOs = "macOS 10.12";    break;
        case VBOXOSTYPE_MacOS1013_x64 & ~VBOXOSTYPE_x64:  pszOs = "macOS 10.13";    break;
        case VBOXOSTYPE_Haiku:                            pszOs = "Haiku";          break;
        case VBOXOSTYPE_VBoxBS_x64 & ~VBOXOSTYPE_x64:     pszOs = "VBox Bootsector"; break;
        default:                                          pszOs = "unknown";        break;
    }
    LogRel(("VMMDev: Guest Additions information report: Interface = 0x%08X osType = 0x%08X (%s, %u-bit)\n",
            pGuestInfo->interfaceVersion, pGuestInfo->osType, pszOs,
            pGuestInfo->osType & VBOXOSTYPE_x64 ? 64 : 32));
}


/**
 * Sets the IRQ (raise it or lower it) for 1.03 additions.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @thread  Any.
 * @remarks Must be called owning the critical section.
 */
static void vmmdevSetIRQ_Legacy(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC)
{
    if (pThis->fu32AdditionsOk)
    {
        /* Filter unsupported events */
        uint32_t fEvents = pThis->fHostEventFlags & pThisCC->CTX_SUFF(pVMMDevRAM)->V.V1_03.u32GuestEventMask;

        Log(("vmmdevSetIRQ: fEvents=%#010x, fHostEventFlags=%#010x, u32GuestEventMask=%#010x.\n",
             fEvents, pThis->fHostEventFlags, pThisCC->CTX_SUFF(pVMMDevRAM)->V.V1_03.u32GuestEventMask));

        /* Move event flags to VMMDev RAM */
        pThisCC->CTX_SUFF(pVMMDevRAM)->V.V1_03.u32HostEvents = fEvents;

        uint32_t uIRQLevel = 0;
        if (fEvents)
        {
            /* Clear host flags which will be delivered to guest. */
            pThis->fHostEventFlags &= ~fEvents;
            Log(("vmmdevSetIRQ: fHostEventFlags=%#010x\n", pThis->fHostEventFlags));
            uIRQLevel = 1;
        }

        /* Set IRQ level for pin 0 (see NoWait comment in vmmdevMaybeSetIRQ). */
        /** @todo make IRQ pin configurable, at least a symbolic constant */
        PDMDevHlpPCISetIrqNoWait(pDevIns, 0, uIRQLevel);
        Log(("vmmdevSetIRQ: IRQ set %d\n", uIRQLevel));
    }
    else
        Log(("vmmdevSetIRQ: IRQ is not generated, guest has not yet reported to us.\n"));
}


/**
 * Sets the IRQ if there are events to be delivered.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @thread  Any.
 * @remarks Must be called owning the critical section.
 */
static void vmmdevMaybeSetIRQ(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC)
{
    Log3(("vmmdevMaybeSetIRQ: fHostEventFlags=%#010x, fGuestFilterMask=%#010x.\n",
          pThis->fHostEventFlags, pThis->fGuestFilterMask));

    if (pThis->fHostEventFlags & pThis->fGuestFilterMask)
    {
        /*
         * Note! No need to wait for the IRQs to be set (if we're not luck
         *       with the locks, etc).  It is a notification about something,
         *       which has already happened.
         */
        pThisCC->pVMMDevRAMR3->V.V1_04.fHaveEvents = true;
        PDMDevHlpPCISetIrqNoWait(pDevIns, 0, 1);
        Log3(("vmmdevMaybeSetIRQ: IRQ set.\n"));
    }
}

/**
 * Notifies the guest about new events (@a fAddEvents).
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   fAddEvents      New events to add.
 * @thread  Any.
 * @remarks Must be called owning the critical section.
 */
static void vmmdevNotifyGuestWorker(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC, uint32_t fAddEvents)
{
    Log3(("vmmdevNotifyGuestWorker: fAddEvents=%#010x.\n", fAddEvents));
    Assert(PDMDevHlpCritSectIsOwner(pDevIns, &pThis->CritSect));

    if (!VMMDEV_INTERFACE_VERSION_IS_1_03(pThis))
    {
        Log3(("vmmdevNotifyGuestWorker: New additions detected.\n"));

        if (pThis->fu32AdditionsOk)
        {
            const bool fHadEvents = (pThis->fHostEventFlags & pThis->fGuestFilterMask) != 0;

            Log3(("vmmdevNotifyGuestWorker: fHadEvents=%d, fHostEventFlags=%#010x, fGuestFilterMask=%#010x.\n",
                  fHadEvents, pThis->fHostEventFlags, pThis->fGuestFilterMask));

            pThis->fHostEventFlags |= fAddEvents;

            if (!fHadEvents)
                vmmdevMaybeSetIRQ(pDevIns, pThis, pThisCC);
        }
        else
        {
            pThis->fHostEventFlags |= fAddEvents;
            Log(("vmmdevNotifyGuestWorker: IRQ is not generated, guest has not yet reported to us.\n"));
        }
    }
    else
    {
        Log3(("vmmdevNotifyGuestWorker: Old additions detected.\n"));

        pThis->fHostEventFlags |= fAddEvents;
        vmmdevSetIRQ_Legacy(pDevIns, pThis, pThisCC);
    }
}



/* -=-=-=-=- Interfaces shared with VMMDevHGCM.cpp  -=-=-=-=- */

/**
 * Notifies the guest about new events (@a fAddEvents).
 *
 * This is used by VMMDev.cpp as well as VMMDevHGCM.cpp.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   fAddEvents      New events to add.
 * @thread  Any.
 */
void VMMDevNotifyGuest(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC, uint32_t fAddEvents)
{
    Log3(("VMMDevNotifyGuest: fAddEvents=%#010x\n", fAddEvents));

    /*
     * Only notify the VM when it's running.
     */
    VMSTATE enmVMState = PDMDevHlpVMState(pDevIns);
    if (   enmVMState == VMSTATE_RUNNING
        || enmVMState == VMSTATE_RUNNING_LS
        || enmVMState == VMSTATE_LOADING
        || enmVMState == VMSTATE_RESUMING
        || enmVMState == VMSTATE_SUSPENDING
        || enmVMState == VMSTATE_SUSPENDING_LS
        || enmVMState == VMSTATE_SUSPENDING_EXT_LS
        || enmVMState == VMSTATE_DEBUGGING
        || enmVMState == VMSTATE_DEBUGGING_LS
       )
    {
        int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);

        vmmdevNotifyGuestWorker(pDevIns, pThis, pThisCC, fAddEvents);

        PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    }
    else
        LogRel(("VMMDevNotifyGuest: fAddEvents=%#x ignored because enmVMState=%d\n", fAddEvents, enmVMState));
}

/**
 * Code shared by VMMDevReq_CtlGuestFilterMask and HGCM for controlling the
 * events the guest are interested in.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   fOrMask         Events to add (VMMDEV_EVENT_XXX). Pass 0 for no
 *                          change.
 * @param   fNotMask        Events to remove (VMMDEV_EVENT_XXX). Pass 0 for no
 *                          change.
 *
 * @remarks When HGCM will automatically enable VMMDEV_EVENT_HGCM when the guest
 *          starts submitting HGCM requests.  Otherwise, the events are
 *          controlled by the guest.
 */
void VMMDevCtlSetGuestFilterMask(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC, uint32_t fOrMask, uint32_t fNotMask)
{
    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);

    const bool fHadEvents = (pThis->fHostEventFlags & pThis->fGuestFilterMask) != 0;

    Log(("VMMDevCtlSetGuestFilterMask: fOrMask=%#010x, u32NotMask=%#010x, fHadEvents=%d.\n", fOrMask, fNotMask, fHadEvents));
    if (fHadEvents)
    {
        if (!pThis->fNewGuestFilterMaskValid)
            pThis->fNewGuestFilterMask = pThis->fGuestFilterMask;

        pThis->fNewGuestFilterMask |= fOrMask;
        pThis->fNewGuestFilterMask &= ~fNotMask;
        pThis->fNewGuestFilterMaskValid = true;
    }
    else
    {
        pThis->fGuestFilterMask |= fOrMask;
        pThis->fGuestFilterMask &= ~fNotMask;
        vmmdevMaybeSetIRQ(pDevIns, pThis, pThisCC);
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
}



/* -=-=-=-=- Request processing functions. -=-=-=-=- */

/**
 * Handles VMMDevReq_ReportGuestInfo.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pRequestHeader  The header of the request to handle.
 */
static int vmmdevReqHandler_ReportGuestInfo(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC,
                                            VMMDevRequestHeader *pRequestHeader)
{
    AssertMsgReturn(pRequestHeader->size == sizeof(VMMDevReportGuestInfo), ("%u\n", pRequestHeader->size), VERR_INVALID_PARAMETER);
    VBoxGuestInfo const *pInfo = &((VMMDevReportGuestInfo *)pRequestHeader)->guestInfo;

    if (memcmp(&pThis->guestInfo, pInfo, sizeof(*pInfo)) != 0)
    {
        /* Make a copy of supplied information. */
        pThis->guestInfo = *pInfo;

        /* Check additions interface version. */
        pThis->fu32AdditionsOk = VMMDEV_INTERFACE_VERSION_IS_OK(pThis->guestInfo.interfaceVersion);

        vmmdevLogGuestOsInfo(&pThis->guestInfo);

        if (pThisCC->pDrv && pThisCC->pDrv->pfnUpdateGuestInfo)
            pThisCC->pDrv->pfnUpdateGuestInfo(pThisCC->pDrv, &pThis->guestInfo);
    }

    if (!pThis->fu32AdditionsOk)
        return VERR_VERSION_MISMATCH;

    /* Clear our IRQ in case it was high for whatever reason. */
    PDMDevHlpPCISetIrqNoWait(pDevIns, 0, 0);

    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_GuestHeartbeat.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 */
static int vmmDevReqHandler_GuestHeartbeat(PPDMDEVINS pDevIns, PVMMDEV pThis)
{
    int rc;
    if (pThis->fHeartbeatActive)
    {
        uint64_t const nsNowTS = PDMDevHlpTimerGetNano(pDevIns, pThis->hFlatlinedTimer);
        if (!pThis->fFlatlined)
        { /* likely */ }
        else
        {
            LogRel(("VMMDev: GuestHeartBeat: Guest is alive (gone %'llu ns)\n", nsNowTS - pThis->nsLastHeartbeatTS));
            ASMAtomicWriteBool(&pThis->fFlatlined, false);
        }
        ASMAtomicWriteU64(&pThis->nsLastHeartbeatTS, nsNowTS);

        /* Postpone (or restart if we missed a beat) the timeout timer. */
        rc = PDMDevHlpTimerSetNano(pDevIns, pThis->hFlatlinedTimer, pThis->cNsHeartbeatTimeout);
    }
    else
        rc = VINF_SUCCESS;
    return rc;
}


/**
 * Timer that fires when where have been no heartbeats for a given time.
 *
 * @remarks Does not take the VMMDev critsect.
 */
static DECLCALLBACK(void) vmmDevHeartbeatFlatlinedTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PVMMDEV pThis = (PVMMDEV)pvUser;
    Assert(hTimer == pThis->hFlatlinedTimer);
    if (pThis->fHeartbeatActive)
    {
        uint64_t cNsElapsed = PDMDevHlpTimerGetNano(pDevIns, hTimer) - pThis->nsLastHeartbeatTS;
        if (   !pThis->fFlatlined
            && cNsElapsed >= pThis->cNsHeartbeatInterval)
        {
            LogRel(("VMMDev: vmmDevHeartbeatFlatlinedTimer: Guest seems to be unresponsive. Last heartbeat received %RU64 seconds ago\n",
                    cNsElapsed / RT_NS_1SEC));
            ASMAtomicWriteBool(&pThis->fFlatlined, true);
        }
    }
}


/**
 * Handles VMMDevReq_HeartbeatConfigure.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns   The device instance.
 * @param   pThis     The VMMDev shared instance data.
 * @param   pReqHdr   The header of the request to handle.
 */
static int vmmDevReqHandler_HeartbeatConfigure(PPDMDEVINS pDevIns, PVMMDEV pThis, VMMDevRequestHeader *pReqHdr)
{
    AssertMsgReturn(pReqHdr->size == sizeof(VMMDevReqHeartbeat), ("%u\n", pReqHdr->size), VERR_INVALID_PARAMETER);
    VMMDevReqHeartbeat *pReq = (VMMDevReqHeartbeat *)pReqHdr;
    int rc;

    pReq->cNsInterval = pThis->cNsHeartbeatInterval;

    if (pReq->fEnabled != pThis->fHeartbeatActive)
    {
        ASMAtomicWriteBool(&pThis->fHeartbeatActive, pReq->fEnabled);
        if (pReq->fEnabled)
        {
            /*
             * Activate the heartbeat monitor.
             */
            pThis->nsLastHeartbeatTS = PDMDevHlpTimerGetNano(pDevIns, pThis->hFlatlinedTimer);
            rc = PDMDevHlpTimerSetNano(pDevIns, pThis->hFlatlinedTimer, pThis->cNsHeartbeatTimeout);
            if (RT_SUCCESS(rc))
                LogRel(("VMMDev: Heartbeat flatline timer set to trigger after %'RU64 ns\n", pThis->cNsHeartbeatTimeout));
            else
                LogRel(("VMMDev: Error starting flatline timer (heartbeat): %Rrc\n", rc));
        }
        else
        {
            /*
             * Deactivate the heartbeat monitor.
             */
            rc = PDMDevHlpTimerStop(pDevIns, pThis->hFlatlinedTimer);
            LogRel(("VMMDev: Heartbeat checking timer has been stopped (rc=%Rrc)\n", rc));
        }
    }
    else
    {
        LogRel(("VMMDev: vmmDevReqHandler_HeartbeatConfigure: No change (fHeartbeatActive=%RTbool)\n", pThis->fHeartbeatActive));
        rc = VINF_SUCCESS;
    }

    return rc;
}


/**
 * Handles VMMDevReq_NtBugCheck.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns   The device instance.
 * @param   pReqHdr   The header of the request to handle.
 */
static int vmmDevReqHandler_NtBugCheck(PPDMDEVINS pDevIns, VMMDevRequestHeader *pReqHdr)
{
    if (pReqHdr->size == sizeof(VMMDevReqNtBugCheck))
    {
        VMMDevReqNtBugCheck const *pReq = (VMMDevReqNtBugCheck const *)pReqHdr;
        PDMDevHlpDBGFReportBugCheck(pDevIns, DBGFEVENT_BSOD_VMMDEV,
                                    pReq->uBugCheck, pReq->auParameters[0], pReq->auParameters[1],
                                    pReq->auParameters[2], pReq->auParameters[3]);
    }
    else if (pReqHdr->size == sizeof(VMMDevRequestHeader))
    {
        LogRel(("VMMDev: NT BugCheck w/o data.\n"));
        PDMDevHlpDBGFReportBugCheck(pDevIns, DBGFEVENT_BSOD_VMMDEV, 0, 0, 0, 0, 0);
    }
    else
        return VERR_INVALID_PARAMETER;
    return VINF_SUCCESS;
}


/**
 * Validates a publisher tag.
 *
 * @returns true / false.
 * @param   pszTag              Tag to validate.
 */
static bool vmmdevReqIsValidPublisherTag(const char *pszTag)
{
    /* Note! This character set is also found in Config.kmk. */
    static char const s_szValidChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz()[]{}+-.,";

    while (*pszTag != '\0')
    {
        if (!strchr(s_szValidChars, *pszTag))
            return false;
        pszTag++;
    }
    return true;
}


/**
 * Validates a build tag.
 *
 * @returns true / false.
 * @param   pszTag              Tag to validate.
 */
static bool vmmdevReqIsValidBuildTag(const char *pszTag)
{
    int cchPrefix;
    if (!strncmp(pszTag, "RC", 2))
        cchPrefix = 2;
    else if (!strncmp(pszTag, "BETA", 4))
        cchPrefix = 4;
    else if (!strncmp(pszTag, "ALPHA", 5))
        cchPrefix = 5;
    else
        return false;

    if (pszTag[cchPrefix] == '\0')
        return true;

    uint8_t u8;
    int rc = RTStrToUInt8Full(&pszTag[cchPrefix], 10, &u8);
    return rc == VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_ReportGuestInfo2.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_ReportGuestInfo2(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    AssertMsgReturn(pReqHdr->size == sizeof(VMMDevReportGuestInfo2), ("%u\n", pReqHdr->size), VERR_INVALID_PARAMETER);
    VBoxGuestInfo2 const *pInfo2 = &((VMMDevReportGuestInfo2 *)pReqHdr)->guestInfo;

    LogRel(("VMMDev: Guest Additions information report: Version %d.%d.%d r%d '%.*s'\n",
            pInfo2->additionsMajor, pInfo2->additionsMinor, pInfo2->additionsBuild,
            pInfo2->additionsRevision, sizeof(pInfo2->szName), pInfo2->szName));

    /* The interface was introduced in 3.2 and will definitely not be
       backported beyond 3.0 (bird). */
    AssertMsgReturn(pInfo2->additionsMajor >= 3,
                    ("%u.%u.%u\n", pInfo2->additionsMajor, pInfo2->additionsMinor, pInfo2->additionsBuild),
                    VERR_INVALID_PARAMETER);

    /* The version must fit in a full version compression. */
    uint32_t uFullVersion = VBOX_FULL_VERSION_MAKE(pInfo2->additionsMajor, pInfo2->additionsMinor, pInfo2->additionsBuild);
    AssertMsgReturn(   VBOX_FULL_VERSION_GET_MAJOR(uFullVersion) == pInfo2->additionsMajor
                    && VBOX_FULL_VERSION_GET_MINOR(uFullVersion) == pInfo2->additionsMinor
                    && VBOX_FULL_VERSION_GET_BUILD(uFullVersion) == pInfo2->additionsBuild,
                    ("%u.%u.%u\n", pInfo2->additionsMajor, pInfo2->additionsMinor, pInfo2->additionsBuild),
                    VERR_OUT_OF_RANGE);

    /*
     * Validate the name.
     * Be less strict towards older additions (< v4.1.50).
     */
    AssertCompile(sizeof(pThis->guestInfo2.szName) == sizeof(pInfo2->szName));
    AssertReturn(RTStrEnd(pInfo2->szName, sizeof(pInfo2->szName)) != NULL, VERR_INVALID_PARAMETER);
    const char *pszName = pInfo2->szName;

    /* The version number which shouldn't be there. */
    char        szTmp[sizeof(pInfo2->szName)];
    size_t      cchStart = RTStrPrintf(szTmp, sizeof(szTmp), "%u.%u.%u", pInfo2->additionsMajor, pInfo2->additionsMinor, pInfo2->additionsBuild);
    AssertMsgReturn(!strncmp(pszName, szTmp, cchStart), ("%s != %s\n", pszName, szTmp), VERR_INVALID_PARAMETER);
    pszName += cchStart;

    /* Now we can either have nothing or a build tag or/and a publisher tag. */
    if (*pszName != '\0')
    {
        const char *pszRelaxedName = "";
        bool const fStrict = pInfo2->additionsMajor > 4
                          || (pInfo2->additionsMajor == 4 && pInfo2->additionsMinor > 1)
                          || (pInfo2->additionsMajor == 4 && pInfo2->additionsMinor == 1 && pInfo2->additionsBuild >= 50);
        bool fOk = false;
        if (*pszName == '_')
        {
            pszName++;
            strcpy(szTmp, pszName);
            char *pszTag2 = strchr(szTmp, '_');
            if (!pszTag2)
            {
                fOk = vmmdevReqIsValidBuildTag(szTmp)
                   || vmmdevReqIsValidPublisherTag(szTmp);
            }
            else
            {
                *pszTag2++ = '\0';
                fOk = vmmdevReqIsValidBuildTag(szTmp);
                if (fOk)
                {
                    fOk = vmmdevReqIsValidPublisherTag(pszTag2);
                    if (!fOk)
                        pszRelaxedName = szTmp;
                }
            }
        }

        if (!fOk)
        {
            AssertLogRelMsgReturn(!fStrict, ("%s", pszName), VERR_INVALID_PARAMETER);

            /* non-strict mode, just zap the extra stuff. */
            LogRel(("VMMDev: ReportGuestInfo2: Ignoring unparsable version name bits: '%s' -> '%s'.\n", pszName, pszRelaxedName));
            pszName = pszRelaxedName;
        }
    }

    /*
     * Save the info and tell Main or whoever is listening.
     */
    pThis->guestInfo2.uFullVersion  = uFullVersion;
    pThis->guestInfo2.uRevision     = pInfo2->additionsRevision;
    pThis->guestInfo2.fFeatures     = pInfo2->additionsFeatures;
    strcpy(pThis->guestInfo2.szName, pszName);

    if (pThisCC->pDrv && pThisCC->pDrv->pfnUpdateGuestInfo2)
        pThisCC->pDrv->pfnUpdateGuestInfo2(pThisCC->pDrv, uFullVersion, pszName, pInfo2->additionsRevision,
                                           pInfo2->additionsFeatures);

    /* Clear our IRQ in case it was high for whatever reason. */
    PDMDevHlpPCISetIrqNoWait(pDevIns, 0, 0);

    return VINF_SUCCESS;
}


/**
 * Allocates a new facility status entry, initializing it to inactive.
 *
 * @returns Pointer to a facility status entry on success, NULL on failure
 *          (table full).
 * @param   pThis           The VMMDev shared instance data.
 * @param   enmFacility     The facility type code.
 * @param   fFixed          This is set when allocating the standard entries
 *                          from the constructor.
 * @param   pTimeSpecNow    Optionally giving the entry timestamp to use (ctor).
 */
static PVMMDEVFACILITYSTATUSENTRY
vmmdevAllocFacilityStatusEntry(PVMMDEV pThis, VBoxGuestFacilityType enmFacility, bool fFixed, PCRTTIMESPEC pTimeSpecNow)
{
    /* If full, expunge one inactive entry. */
    if (pThis->cFacilityStatuses == RT_ELEMENTS(pThis->aFacilityStatuses))
    {
        uint32_t i = pThis->cFacilityStatuses;
        while (i-- > 0)
        {
            if (   pThis->aFacilityStatuses[i].enmStatus == VBoxGuestFacilityStatus_Inactive
                && !pThis->aFacilityStatuses[i].fFixed)
            {
                pThis->cFacilityStatuses--;
                int cToMove = pThis->cFacilityStatuses - i;
                if (cToMove)
                    memmove(&pThis->aFacilityStatuses[i], &pThis->aFacilityStatuses[i + 1],
                            cToMove * sizeof(pThis->aFacilityStatuses[i]));
                RT_ZERO(pThis->aFacilityStatuses[pThis->cFacilityStatuses]);
                break;
            }
        }

        if (pThis->cFacilityStatuses == RT_ELEMENTS(pThis->aFacilityStatuses))
            return NULL;
    }

    /* Find location in array (it's sorted). */
    uint32_t i = pThis->cFacilityStatuses;
    while (i-- > 0)
        if ((uint32_t)pThis->aFacilityStatuses[i].enmFacility < (uint32_t)enmFacility)
            break;
    i++;

    /* Move. */
    int cToMove = pThis->cFacilityStatuses - i;
    if (cToMove > 0)
        memmove(&pThis->aFacilityStatuses[i + 1], &pThis->aFacilityStatuses[i],
                cToMove * sizeof(pThis->aFacilityStatuses[i]));
    pThis->cFacilityStatuses++;

    /* Initialize. */
    pThis->aFacilityStatuses[i].enmFacility  = enmFacility;
    pThis->aFacilityStatuses[i].enmStatus    = VBoxGuestFacilityStatus_Inactive;
    pThis->aFacilityStatuses[i].fFixed       = fFixed;
    pThis->aFacilityStatuses[i].afPadding[0] = 0;
    pThis->aFacilityStatuses[i].afPadding[1] = 0;
    pThis->aFacilityStatuses[i].afPadding[2] = 0;
    pThis->aFacilityStatuses[i].fFlags       = 0;
    if (pTimeSpecNow)
        pThis->aFacilityStatuses[i].TimeSpecTS = *pTimeSpecNow;
    else
        RTTimeSpecSetNano(&pThis->aFacilityStatuses[i].TimeSpecTS, 0);

    return &pThis->aFacilityStatuses[i];
}


/**
 * Gets a facility status entry, allocating a new one if not already present.
 *
 * @returns Pointer to a facility status entry on success, NULL on failure
 *          (table full).
 * @param   pThis           The VMMDev shared instance data.
 * @param   enmFacility     The facility type code.
 */
static PVMMDEVFACILITYSTATUSENTRY vmmdevGetFacilityStatusEntry(PVMMDEV pThis, VBoxGuestFacilityType enmFacility)
{
    /** @todo change to binary search. */
    uint32_t i = pThis->cFacilityStatuses;
    while (i-- > 0)
    {
        if (pThis->aFacilityStatuses[i].enmFacility == enmFacility)
            return &pThis->aFacilityStatuses[i];
        if ((uint32_t)pThis->aFacilityStatuses[i].enmFacility < (uint32_t)enmFacility)
            break;
    }
    return vmmdevAllocFacilityStatusEntry(pThis, enmFacility, false /*fFixed*/, NULL);
}


/**
 * Handles VMMDevReq_ReportGuestStatus.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_ReportGuestStatus(PVMMDEV pThis, PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    /*
     * Validate input.
     */
    AssertMsgReturn(pReqHdr->size == sizeof(VMMDevReportGuestStatus), ("%u\n", pReqHdr->size), VERR_INVALID_PARAMETER);
    VBoxGuestStatus *pStatus = &((VMMDevReportGuestStatus *)pReqHdr)->guestStatus;
    AssertMsgReturn(   pStatus->facility > VBoxGuestFacilityType_Unknown
                    && pStatus->facility <= VBoxGuestFacilityType_All,
                    ("%d\n", pStatus->facility),
                    VERR_INVALID_PARAMETER);
    AssertMsgReturn(pStatus->status == (VBoxGuestFacilityStatus)(uint16_t)pStatus->status,
                    ("%#x (%u)\n", pStatus->status, pStatus->status),
                    VERR_OUT_OF_RANGE);

    /*
     * Do the update.
     */
    RTTIMESPEC Now;
    RTTimeNow(&Now);
    if (pStatus->facility == VBoxGuestFacilityType_All)
    {
        uint32_t i = pThis->cFacilityStatuses;
        while (i-- > 0)
        {
            pThis->aFacilityStatuses[i].TimeSpecTS = Now;
            pThis->aFacilityStatuses[i].enmStatus  = pStatus->status;
            pThis->aFacilityStatuses[i].fFlags     = pStatus->flags;
        }
    }
    else
    {
        PVMMDEVFACILITYSTATUSENTRY pEntry = vmmdevGetFacilityStatusEntry(pThis, pStatus->facility);
        if (!pEntry)
        {
            LogRelMax(10, ("VMMDev: Facility table is full - facility=%u status=%u\n", pStatus->facility, pStatus->status));
            return VERR_OUT_OF_RESOURCES;
        }

        pEntry->TimeSpecTS = Now;
        pEntry->enmStatus  = pStatus->status;
        pEntry->fFlags     = pStatus->flags;
    }

    if (pThisCC->pDrv && pThisCC->pDrv->pfnUpdateGuestStatus)
        pThisCC->pDrv->pfnUpdateGuestStatus(pThisCC->pDrv, pStatus->facility, pStatus->status, pStatus->flags, &Now);

    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_ReportGuestUserState.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_ReportGuestUserState(PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    /*
     * Validate input.
     */
    VMMDevReportGuestUserState *pReq = (VMMDevReportGuestUserState *)pReqHdr;
    AssertMsgReturn(pReq->header.size >= sizeof(*pReq), ("%u\n", pReqHdr->size), VERR_INVALID_PARAMETER);

    if (   pThisCC->pDrv
        && pThisCC->pDrv->pfnUpdateGuestUserState)
    {
        /* Play safe. */
        AssertReturn(pReq->header.size      <= _2K, VERR_TOO_MUCH_DATA);
        AssertReturn(pReq->status.cbUser    <= 256, VERR_TOO_MUCH_DATA);
        AssertReturn(pReq->status.cbDomain  <= 256, VERR_TOO_MUCH_DATA);
        AssertReturn(pReq->status.cbDetails <= _1K, VERR_TOO_MUCH_DATA);

        /* pbDynamic marks the beginning of the struct's dynamically
         * allocated data area. */
        uint8_t *pbDynamic = (uint8_t *)&pReq->status.szUser;
        uint32_t cbLeft    = pReqHdr->size - RT_UOFFSETOF(VMMDevReportGuestUserState, status.szUser);

        /* The user. */
        AssertReturn(pReq->status.cbUser > 0, VERR_INVALID_PARAMETER); /* User name is required. */
        AssertReturn(pReq->status.cbUser <= cbLeft, VERR_INVALID_PARAMETER);
        const char *pszUser = (const char *)pbDynamic;
        AssertReturn(RTStrEnd(pszUser, pReq->status.cbUser), VERR_INVALID_PARAMETER);
        int rc = RTStrValidateEncoding(pszUser);
        AssertRCReturn(rc, rc);

        /* Advance to the next field. */
        pbDynamic += pReq->status.cbUser;
        cbLeft    -= pReq->status.cbUser;

        /* pszDomain can be NULL. */
        AssertReturn(pReq->status.cbDomain <= cbLeft, VERR_INVALID_PARAMETER);
        const char *pszDomain = NULL;
        if (pReq->status.cbDomain)
        {
            pszDomain = (const char *)pbDynamic;
            AssertReturn(RTStrEnd(pszDomain, pReq->status.cbDomain), VERR_INVALID_PARAMETER);
            rc = RTStrValidateEncoding(pszDomain);
            AssertRCReturn(rc, rc);

            /* Advance to the next field. */
            pbDynamic += pReq->status.cbDomain;
            cbLeft    -= pReq->status.cbDomain;
        }

        /* pbDetails can be NULL. */
        const uint8_t *pbDetails = NULL;
        AssertReturn(pReq->status.cbDetails <= cbLeft, VERR_INVALID_PARAMETER);
        if (pReq->status.cbDetails > 0)
            pbDetails = pbDynamic;

        pThisCC->pDrv->pfnUpdateGuestUserState(pThisCC->pDrv, pszUser, pszDomain, (uint32_t)pReq->status.state,
                                               pbDetails, pReq->status.cbDetails);
    }

    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_ReportGuestCapabilities.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_ReportGuestCapabilities(PVMMDEV pThis, PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    VMMDevReqGuestCapabilities *pReq = (VMMDevReqGuestCapabilities *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    /* Enable VMMDEV_GUEST_SUPPORTS_GRAPHICS automatically for guests using the old
     * request to report their capabilities.
     */
    const uint32_t fu32Caps = pReq->caps | VMMDEV_GUEST_SUPPORTS_GRAPHICS;

    if (pThis->fGuestCaps != fu32Caps)
    {
        /* make a copy of supplied information */
        pThis->fGuestCaps = fu32Caps;

        LogRel(("VMMDev: Guest Additions capability report (legacy): (0x%x) seamless: %s, hostWindowMapping: %s, graphics: yes\n",
                fu32Caps,
                fu32Caps & VMMDEV_GUEST_SUPPORTS_SEAMLESS ? "yes" : "no",
                fu32Caps & VMMDEV_GUEST_SUPPORTS_GUEST_HOST_WINDOW_MAPPING ? "yes" : "no"));

        if (pThisCC->pDrv && pThisCC->pDrv->pfnUpdateGuestCapabilities)
            pThisCC->pDrv->pfnUpdateGuestCapabilities(pThisCC->pDrv, fu32Caps);
    }
    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_SetGuestCapabilities.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_SetGuestCapabilities(PVMMDEV pThis, PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    VMMDevReqGuestCapabilities2 *pReq = (VMMDevReqGuestCapabilities2 *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    uint32_t fu32Caps = pThis->fGuestCaps;
    fu32Caps |= pReq->u32OrMask;
    fu32Caps &= ~pReq->u32NotMask;

    LogRel(("VMMDev: Guest Additions capability report: (%#x -> %#x) seamless: %s, hostWindowMapping: %s, graphics: %s\n",
            pThis->fGuestCaps, fu32Caps,
            fu32Caps & VMMDEV_GUEST_SUPPORTS_SEAMLESS ? "yes" : "no",
            fu32Caps & VMMDEV_GUEST_SUPPORTS_GUEST_HOST_WINDOW_MAPPING ? "yes" : "no",
            fu32Caps & VMMDEV_GUEST_SUPPORTS_GRAPHICS ? "yes" : "no"));

    pThis->fGuestCaps = fu32Caps;

    if (pThisCC->pDrv && pThisCC->pDrv->pfnUpdateGuestCapabilities)
        pThisCC->pDrv->pfnUpdateGuestCapabilities(pThisCC->pDrv, fu32Caps);

    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_GetMouseStatus.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_GetMouseStatus(PVMMDEV pThis, VMMDevRequestHeader *pReqHdr)
{
    VMMDevReqMouseStatus *pReq = (VMMDevReqMouseStatus *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    pReq->mouseFeatures = pThis->fMouseCapabilities
                        & VMMDEV_MOUSE_MASK;
    pReq->pointerXPos   = pThis->xMouseAbs;
    pReq->pointerYPos   = pThis->yMouseAbs;
    LogRel2(("VMMDev: vmmdevReqHandler_GetMouseStatus: mouseFeatures=%#x, xAbs=%d, yAbs=%d\n",
             pReq->mouseFeatures, pReq->pointerXPos, pReq->pointerYPos));
    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_GetMouseStatusEx.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_GetMouseStatusEx(PVMMDEV pThis, VMMDevRequestHeader *pReqHdr)
{
    VMMDevReqMouseStatusEx *pReq = (VMMDevReqMouseStatusEx *)pReqHdr;
    AssertMsgReturn(pReq->Core.header.size == sizeof(*pReq), ("%u\n", pReq->Core.header.size), VERR_INVALID_PARAMETER);

    /* Main will convert host mouse buttons state obtained from GUI
     * into PDMIMOUSEPORT_BUTTON_XXX representation. Guest will expect it
     * to VMMDEV_MOUSE_BUTTON_XXX representaion. Make sure both
     * representations are identical.  */
    AssertCompile(VMMDEV_MOUSE_BUTTON_LEFT   == PDMIMOUSEPORT_BUTTON_LEFT);
    AssertCompile(VMMDEV_MOUSE_BUTTON_RIGHT  == PDMIMOUSEPORT_BUTTON_RIGHT);
    AssertCompile(VMMDEV_MOUSE_BUTTON_MIDDLE == PDMIMOUSEPORT_BUTTON_MIDDLE);
    AssertCompile(VMMDEV_MOUSE_BUTTON_X1     == PDMIMOUSEPORT_BUTTON_X1);
    AssertCompile(VMMDEV_MOUSE_BUTTON_X2     == PDMIMOUSEPORT_BUTTON_X2);

    pReq->Core.mouseFeatures = pThis->fMouseCapabilities & VMMDEV_MOUSE_MASK;
    pReq->Core.pointerXPos   = pThis->xMouseAbs;
    pReq->Core.pointerYPos   = pThis->yMouseAbs;
    pReq->dz                 = pThis->dzMouse;
    pReq->dw                 = pThis->dwMouse;
    pReq->fButtons           = pThis->fMouseButtons;
    LogRel2(("VMMDev: vmmdevReqHandler_GetMouseStatusEx: mouseFeatures=%#x, xAbs=%d, yAbs=%d, zAbs=%d, wMouseRel=%d, fButtons=0x%x\n",
             pReq->Core.mouseFeatures, pReq->Core.pointerXPos, pReq->Core.pointerYPos, pReq->dz, pReq->dw, pReq->fButtons));
    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_SetMouseStatus.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_SetMouseStatus(PVMMDEV pThis, PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    VMMDevReqMouseStatus *pReq = (VMMDevReqMouseStatus *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    LogRelFlow(("VMMDev: vmmdevReqHandler_SetMouseStatus: mouseFeatures=%#x\n", pReq->mouseFeatures));

    bool fNotify = false;
    if (   (pReq->mouseFeatures & VMMDEV_MOUSE_NOTIFY_HOST_MASK)
        != (  pThis->fMouseCapabilities
            & VMMDEV_MOUSE_NOTIFY_HOST_MASK))
        fNotify = true;

    pThis->fMouseCapabilities &= ~VMMDEV_MOUSE_GUEST_MASK;
    pThis->fMouseCapabilities |= (pReq->mouseFeatures & VMMDEV_MOUSE_GUEST_MASK);

    LogRelFlow(("VMMDev: vmmdevReqHandler_SetMouseStatus: New host capabilities: %#x\n", pThis->fMouseCapabilities));

    /*
     * Notify connector if something changed.
     */
    if (fNotify)
    {
        LogRelFlow(("VMMDev: vmmdevReqHandler_SetMouseStatus: Notifying connector\n"));
        pThisCC->pDrv->pfnUpdateMouseCapabilities(pThisCC->pDrv, pThis->fMouseCapabilities);
    }

    return VINF_SUCCESS;
}

static int vmmdevVerifyPointerShape(VMMDevReqMousePointer *pReq)
{
    /* Should be enough for most mouse pointers. */
    if (pReq->width > 8192 || pReq->height > 8192)
        return VERR_INVALID_PARAMETER;

    uint32_t cbShape = (pReq->width + 7) / 8 * pReq->height; /* size of the AND mask */
    cbShape = ((cbShape + 3) & ~3) + pReq->width * 4 * pReq->height; /* + gap + size of the XOR mask */
    if (RT_UOFFSETOF(VMMDevReqMousePointer, pointerData) + cbShape > pReq->header.size)
        return VERR_INVALID_PARAMETER;

    return VINF_SUCCESS;
}

/**
 * Handles VMMDevReq_SetPointerShape.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_SetPointerShape(PVMMDEV pThis, PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    VMMDevReqMousePointer *pReq = (VMMDevReqMousePointer *)pReqHdr;
    if (pReq->header.size < sizeof(*pReq))
    {
        AssertMsg(pReq->header.size == 0x10028 && pReq->header.version == 10000,  /* don't complain about legacy!!! */
                  ("VMMDev mouse shape structure has invalid size %d (%#x) version=%d!\n",
                   pReq->header.size, pReq->header.size, pReq->header.version));
        return VERR_INVALID_PARAMETER;
    }

    bool fVisible = RT_BOOL(pReq->fFlags & VBOX_MOUSE_POINTER_VISIBLE);
    bool fAlpha   = RT_BOOL(pReq->fFlags & VBOX_MOUSE_POINTER_ALPHA);
    bool fShape   = RT_BOOL(pReq->fFlags & VBOX_MOUSE_POINTER_SHAPE);

    Log(("VMMDevReq_SetPointerShape: visible: %d, alpha: %d, shape = %d, width: %d, height: %d\n",
         fVisible, fAlpha, fShape, pReq->width, pReq->height));

    if (pReq->header.size == sizeof(VMMDevReqMousePointer))
    {
        /* The guest did not provide the shape actually. */
        fShape = false;
    }

    /* forward call to driver */
    if (fShape)
    {
        int rc = vmmdevVerifyPointerShape(pReq);
        if (RT_FAILURE(rc))
            return rc;

        pThisCC->pDrv->pfnUpdatePointerShape(pThisCC->pDrv,
                                             fVisible,
                                             fAlpha,
                                             pReq->xHot, pReq->yHot,
                                             pReq->width, pReq->height,
                                             pReq->pointerData);
    }
    else
    {
        pThisCC->pDrv->pfnUpdatePointerShape(pThisCC->pDrv,
                                             fVisible,
                                             0,
                                             0, 0,
                                             0, 0,
                                             NULL);
    }

    pThis->fHostCursorRequested = fVisible;
    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_GetHostTime.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_GetHostTime(PPDMDEVINS pDevIns, PVMMDEV pThis, VMMDevRequestHeader *pReqHdr)
{
    VMMDevReqHostTime *pReq = (VMMDevReqHostTime *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    if (RT_LIKELY(!pThis->fGetHostTimeDisabled))
    {
        RTTIMESPEC now;
        pReq->time = RTTimeSpecGetMilli(PDMDevHlpTMUtcNow(pDevIns, &now));
        return VINF_SUCCESS;
    }
    return VERR_NOT_SUPPORTED;
}


/**
 * Handles VMMDevReq_GetHypervisorInfo.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_GetHypervisorInfo(PPDMDEVINS pDevIns, VMMDevRequestHeader *pReqHdr)
{
    VMMDevReqHypervisorInfo *pReq = (VMMDevReqHypervisorInfo *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

#if 1 /* Obsolete for now, only used for raw-mode. */
    RT_NOREF(pDevIns);
    pReq->hypervisorSize = 0;
    return VINF_SUCCESS;
#else
    return PGMR3MappingsSize(PDMDevHlpGetVM(pDevIns), &pReq->hypervisorSize);
#endif
}


/**
 * Handles VMMDevReq_SetHypervisorInfo.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_SetHypervisorInfo(PPDMDEVINS pDevIns, VMMDevRequestHeader *pReqHdr)
{
    VMMDevReqHypervisorInfo *pReq = (VMMDevReqHypervisorInfo *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    int rc;
#if 1 /* Obsolete for now, only used for raw-mode. */
    RT_NOREF(pDevIns);
    if (pReq->hypervisorStart == 0 || pReq->hypervisorSize == 0)
        rc = VINF_SUCCESS;
    else
        rc = VERR_TRY_AGAIN;
#else
    PVM pVM = PDMDevHlpGetVM(pDevIns);
    if (pReq->hypervisorStart == 0)
        rc = PGMR3MappingsUnfix(pVM);
    else
    {
        /* only if the client has queried the size before! */
        uint32_t cbMappings;
        rc = PGMR3MappingsSize(pVM, &cbMappings);
        if (RT_SUCCESS(rc) && pReq->hypervisorSize == cbMappings)
        {
            /* new reservation */
            rc = PGMR3MappingsFix(pVM, pReq->hypervisorStart, pReq->hypervisorSize);
            LogRel(("VMMDev: Guest reported fixed hypervisor window at 0%010x LB %#x (rc=%Rrc)\n",
                    pReq->hypervisorStart, pReq->hypervisorSize, rc));
        }
        else if (RT_FAILURE(rc)) /** @todo r=bird: This should've been RT_SUCCESS(rc)) */
            rc = VERR_TRY_AGAIN;
    }
#endif
    return rc;
}


/**
 * Handles VMMDevReq_RegisterPatchMemory.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_RegisterPatchMemory(PPDMDEVINS pDevIns, VMMDevRequestHeader *pReqHdr)
{
    VMMDevReqPatchMemory *pReq = (VMMDevReqPatchMemory *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    return PDMDevHlpVMMRegisterPatchMemory(pDevIns, pReq->pPatchMem, pReq->cbPatchMem);
}


/**
 * Handles VMMDevReq_DeregisterPatchMemory.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_DeregisterPatchMemory(PPDMDEVINS pDevIns, VMMDevRequestHeader *pReqHdr)
{
    VMMDevReqPatchMemory *pReq = (VMMDevReqPatchMemory *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    return PDMDevHlpVMMDeregisterPatchMemory(pDevIns, pReq->pPatchMem, pReq->cbPatchMem);
}


/**
 * Handles VMMDevReq_SetPowerStatus.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_SetPowerStatus(PPDMDEVINS pDevIns, PVMMDEV pThis, VMMDevRequestHeader *pReqHdr)
{
    VMMDevPowerStateRequest *pReq = (VMMDevPowerStateRequest *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    switch (pReq->powerState)
    {
        case VMMDevPowerState_Pause:
        {
            LogRel(("VMMDev: Guest requests the VM to be suspended (paused)\n"));
            return PDMDevHlpVMSuspend(pDevIns);
        }

        case VMMDevPowerState_PowerOff:
        {
            LogRel(("VMMDev: Guest requests the VM to be turned off\n"));
            return PDMDevHlpVMPowerOff(pDevIns);
        }

        case VMMDevPowerState_SaveState:
        {
            if (pThis->fAllowGuestToSaveState)
            {
                LogRel(("VMMDev: Guest requests the VM to be saved and powered off\n"));
                return PDMDevHlpVMSuspendSaveAndPowerOff(pDevIns);
            }
            LogRel(("VMMDev: Guest requests the VM to be saved and powered off, declined\n"));
            return VERR_ACCESS_DENIED;
        }

        default:
            AssertMsgFailed(("VMMDev: Invalid power state request: %d\n", pReq->powerState));
            return VERR_INVALID_PARAMETER;
    }
}


/**
 * Handles VMMDevReq_GetDisplayChangeRequest
 *
 * @returns VBox status code that the guest should see.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pReqHdr         The header of the request to handle.
 * @remarks Deprecated.
 */
static int vmmdevReqHandler_GetDisplayChangeRequest(PVMMDEV pThis, VMMDevRequestHeader *pReqHdr)
{
    VMMDevDisplayChangeRequest *pReq = (VMMDevDisplayChangeRequest *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    DISPLAYCHANGEREQUEST *pDispRequest = &pThis->displayChangeData.aRequests[0];

    if (pReq->eventAck == VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST)
    {
        /* Current request has been read at least once. */
        pDispRequest->fPending = false;

        /* Remember which resolution the client has queried, subsequent reads
         * will return the same values. */
        pDispRequest->lastReadDisplayChangeRequest = pDispRequest->displayChangeRequest;
        pThis->displayChangeData.fGuestSentChangeEventAck = true;
    }

    /* If not a response to a VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST, just
     * read the last valid video mode hint. This happens when the guest X server
     * determines the initial mode. */
    VMMDevDisplayDef const *pDisplayDef = pThis->displayChangeData.fGuestSentChangeEventAck ?
                                              &pDispRequest->lastReadDisplayChangeRequest :
                                              &pDispRequest->displayChangeRequest;
    pReq->xres = RT_BOOL(pDisplayDef->fDisplayFlags & VMMDEV_DISPLAY_CX)  ? pDisplayDef->cx : 0;
    pReq->yres = RT_BOOL(pDisplayDef->fDisplayFlags & VMMDEV_DISPLAY_CY)  ? pDisplayDef->cy : 0;
    pReq->bpp  = RT_BOOL(pDisplayDef->fDisplayFlags & VMMDEV_DISPLAY_BPP) ? pDisplayDef->cBitsPerPixel : 0;

    Log(("VMMDev: returning display change request xres = %d, yres = %d, bpp = %d\n", pReq->xres, pReq->yres, pReq->bpp));

    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_GetDisplayChangeRequest2.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_GetDisplayChangeRequest2(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC,
                                                     VMMDevRequestHeader *pReqHdr)
{
    VMMDevDisplayChangeRequest2 *pReq = (VMMDevDisplayChangeRequest2 *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    DISPLAYCHANGEREQUEST *pDispRequest = NULL;

    if (pReq->eventAck == VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST)
    {
        /* Select a pending request to report. */
        unsigned i;
        for (i = 0; i < RT_ELEMENTS(pThis->displayChangeData.aRequests); i++)
        {
            if (pThis->displayChangeData.aRequests[i].fPending)
            {
                pDispRequest = &pThis->displayChangeData.aRequests[i];
                /* Remember which request should be reported. */
                pThis->displayChangeData.iCurrentMonitor = i;
                Log3(("VMMDev: will report pending request for %u\n", i));
                break;
            }
        }

        /* Check if there are more pending requests. */
        i++;
        for (; i < RT_ELEMENTS(pThis->displayChangeData.aRequests); i++)
        {
            if (pThis->displayChangeData.aRequests[i].fPending)
            {
                VMMDevNotifyGuest(pDevIns, pThis, pThisCC, VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST);
                Log3(("VMMDev: another pending at %u\n", i));
                break;
            }
        }

        if (pDispRequest)
        {
            /* Current request has been read at least once. */
            pDispRequest->fPending = false;

            /* Remember which resolution the client has queried, subsequent reads
             * will return the same values. */
            pDispRequest->lastReadDisplayChangeRequest = pDispRequest->displayChangeRequest;
            pThis->displayChangeData.fGuestSentChangeEventAck = true;
        }
        else
        {
             Log3(("VMMDev: no pending request!!!\n"));
        }
    }

    if (!pDispRequest)
    {
        Log3(("VMMDev: default to %d\n", pThis->displayChangeData.iCurrentMonitor));
        pDispRequest = &pThis->displayChangeData.aRequests[pThis->displayChangeData.iCurrentMonitor];
    }

    /* If not a response to a VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST, just
     * read the last valid video mode hint. This happens when the guest X server
     * determines the initial mode. */
    VMMDevDisplayDef const *pDisplayDef = pThis->displayChangeData.fGuestSentChangeEventAck ?
                                              &pDispRequest->lastReadDisplayChangeRequest :
                                              &pDispRequest->displayChangeRequest;
    pReq->xres    = RT_BOOL(pDisplayDef->fDisplayFlags & VMMDEV_DISPLAY_CX)  ? pDisplayDef->cx : 0;
    pReq->yres    = RT_BOOL(pDisplayDef->fDisplayFlags & VMMDEV_DISPLAY_CY)  ? pDisplayDef->cy : 0;
    pReq->bpp     = RT_BOOL(pDisplayDef->fDisplayFlags & VMMDEV_DISPLAY_BPP) ? pDisplayDef->cBitsPerPixel : 0;
    pReq->display = pDisplayDef->idDisplay;

    Log(("VMMDev: returning display change request xres = %d, yres = %d, bpp = %d at %d\n",
         pReq->xres, pReq->yres, pReq->bpp, pReq->display));

    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_GetDisplayChangeRequestEx.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_GetDisplayChangeRequestEx(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC,
                                                      VMMDevRequestHeader *pReqHdr)
{
    VMMDevDisplayChangeRequestEx *pReq = (VMMDevDisplayChangeRequestEx *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    DISPLAYCHANGEREQUEST *pDispRequest = NULL;

    if (pReq->eventAck == VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST)
    {
        /* Select a pending request to report. */
        unsigned i;
        for (i = 0; i < RT_ELEMENTS(pThis->displayChangeData.aRequests); i++)
        {
            if (pThis->displayChangeData.aRequests[i].fPending)
            {
                pDispRequest = &pThis->displayChangeData.aRequests[i];
                /* Remember which request should be reported. */
                pThis->displayChangeData.iCurrentMonitor = i;
                Log3(("VMMDev: will report pending request for %d\n",
                      i));
                break;
            }
        }

        /* Check if there are more pending requests. */
        i++;
        for (; i < RT_ELEMENTS(pThis->displayChangeData.aRequests); i++)
        {
            if (pThis->displayChangeData.aRequests[i].fPending)
            {
                VMMDevNotifyGuest(pDevIns, pThis, pThisCC, VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST);
                Log3(("VMMDev: another pending at %d\n",
                      i));
                break;
            }
        }

        if (pDispRequest)
        {
            /* Current request has been read at least once. */
            pDispRequest->fPending = false;

            /* Remember which resolution the client has queried, subsequent reads
             * will return the same values. */
            pDispRequest->lastReadDisplayChangeRequest = pDispRequest->displayChangeRequest;
            pThis->displayChangeData.fGuestSentChangeEventAck = true;
        }
        else
        {
             Log3(("VMMDev: no pending request!!!\n"));
        }
    }

    if (!pDispRequest)
    {
        Log3(("VMMDev: default to %d\n",
              pThis->displayChangeData.iCurrentMonitor));
        pDispRequest = &pThis->displayChangeData.aRequests[pThis->displayChangeData.iCurrentMonitor];
    }

    /* If not a response to a VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST, just
     * read the last valid video mode hint. This happens when the guest X server
     * determines the initial mode. */
    VMMDevDisplayDef const *pDisplayDef = pThis->displayChangeData.fGuestSentChangeEventAck ?
                                              &pDispRequest->lastReadDisplayChangeRequest :
                                              &pDispRequest->displayChangeRequest;
    pReq->xres          = RT_BOOL(pDisplayDef->fDisplayFlags & VMMDEV_DISPLAY_CX)  ? pDisplayDef->cx : 0;
    pReq->yres          = RT_BOOL(pDisplayDef->fDisplayFlags & VMMDEV_DISPLAY_CY)  ? pDisplayDef->cy : 0;
    pReq->bpp           = RT_BOOL(pDisplayDef->fDisplayFlags & VMMDEV_DISPLAY_BPP) ? pDisplayDef->cBitsPerPixel : 0;
    pReq->display       = pDisplayDef->idDisplay;
    pReq->cxOrigin      = pDisplayDef->xOrigin;
    pReq->cyOrigin      = pDisplayDef->yOrigin;
    pReq->fEnabled      = !RT_BOOL(pDisplayDef->fDisplayFlags & VMMDEV_DISPLAY_DISABLED);
    pReq->fChangeOrigin = RT_BOOL(pDisplayDef->fDisplayFlags & VMMDEV_DISPLAY_ORIGIN);

    Log(("VMMDevEx: returning display change request xres = %d, yres = %d, bpp = %d id %d xPos = %d, yPos = %d & Enabled=%d\n",
         pReq->xres, pReq->yres, pReq->bpp, pReq->display, pReq->cxOrigin, pReq->cyOrigin, pReq->fEnabled));

    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_GetDisplayChangeRequestMulti.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_GetDisplayChangeRequestMulti(PVMMDEV pThis, VMMDevRequestHeader *pReqHdr)
{
    VMMDevDisplayChangeRequestMulti *pReq = (VMMDevDisplayChangeRequestMulti *)pReqHdr;
    unsigned i;

    ASSERT_GUEST_MSG_RETURN(pReq->header.size >= sizeof(*pReq),
                            ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();

    uint32_t const cDisplays = pReq->cDisplays;
    ASSERT_GUEST_MSG_RETURN(cDisplays > 0 && cDisplays <= RT_ELEMENTS(pThis->displayChangeData.aRequests),
                            ("cDisplays %u\n", cDisplays), VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();

    ASSERT_GUEST_MSG_RETURN(pReq->header.size >= sizeof(*pReq) + (cDisplays - 1) * sizeof(VMMDevDisplayDef),
                            ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();

    if (pReq->eventAck == VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST)
    {
        uint32_t cDisplaysOut = 0;
        /* Remember which resolution the client has queried, subsequent reads
         * will return the same values. */
        for (i = 0; i < RT_ELEMENTS(pThis->displayChangeData.aRequests); ++i)
        {
            DISPLAYCHANGEREQUEST *pDCR = &pThis->displayChangeData.aRequests[i];

            pDCR->lastReadDisplayChangeRequest = pDCR->displayChangeRequest;

            if (pDCR->fPending)
            {
                if (cDisplaysOut < cDisplays)
                    pReq->aDisplays[cDisplaysOut] = pDCR->lastReadDisplayChangeRequest;

                cDisplaysOut++;
                pDCR->fPending = false;
            }
        }

        pReq->cDisplays = cDisplaysOut;
        pThis->displayChangeData.fGuestSentChangeEventAck = true;
    }
    else
    {
        /* Fill the guest request with monitor layout data. */
        for (i = 0; i < cDisplays; ++i)
        {
            /* If not a response to a VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST, just
             * read the last valid video mode hint. This happens when the guest X server
             * determines the initial mode. */
            DISPLAYCHANGEREQUEST const *pDCR = &pThis->displayChangeData.aRequests[i];
            VMMDevDisplayDef const *pDisplayDef = pThis->displayChangeData.fGuestSentChangeEventAck ?
                &pDCR->lastReadDisplayChangeRequest :
                &pDCR->displayChangeRequest;
            pReq->aDisplays[i] = *pDisplayDef;
        }
    }

    Log(("VMMDev: returning multimonitor display change request cDisplays %d\n", cDisplays));

    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_VideoModeSupported.
 *
 * Query whether the given video mode is supported.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_VideoModeSupported(PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    VMMDevVideoModeSupportedRequest *pReq = (VMMDevVideoModeSupportedRequest *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    /* forward the call */
    return pThisCC->pDrv->pfnVideoModeSupported(pThisCC->pDrv,
                                                0, /* primary screen. */
                                                pReq->width,
                                                pReq->height,
                                                pReq->bpp,
                                                &pReq->fSupported);
}


/**
 * Handles VMMDevReq_VideoModeSupported2.
 *
 * Query whether the given video mode is supported for a specific display
 *
 * @returns VBox status code that the guest should see.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_VideoModeSupported2(PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    VMMDevVideoModeSupportedRequest2 *pReq = (VMMDevVideoModeSupportedRequest2 *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    /* forward the call */
    return pThisCC->pDrv->pfnVideoModeSupported(pThisCC->pDrv,
                                                pReq->display,
                                                pReq->width,
                                                pReq->height,
                                                pReq->bpp,
                                                &pReq->fSupported);
}



/**
 * Handles VMMDevReq_GetHeightReduction.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_GetHeightReduction(PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    VMMDevGetHeightReductionRequest *pReq = (VMMDevGetHeightReductionRequest *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    /* forward the call */
    return pThisCC->pDrv->pfnGetHeightReduction(pThisCC->pDrv, &pReq->heightReduction);
}


/**
 * Handles VMMDevReq_AcknowledgeEvents.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_AcknowledgeEvents(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    VMMDevEvents *pReq = (VMMDevEvents *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);
    STAM_REL_COUNTER_INC(&pThis->StatSlowIrqAck);

    if (!VMMDEV_INTERFACE_VERSION_IS_1_03(pThis))
    {
        /*
         * Note! This code is duplicated in vmmdevPioFastRequestIrqAck.
         */
        if (pThis->fNewGuestFilterMaskValid)
        {
            pThis->fNewGuestFilterMaskValid = false;
            pThis->fGuestFilterMask = pThis->fNewGuestFilterMask;
        }

        pReq->events = pThis->fHostEventFlags & pThis->fGuestFilterMask;

        pThis->fHostEventFlags &= ~pThis->fGuestFilterMask;
        pThisCC->CTX_SUFF(pVMMDevRAM)->V.V1_04.fHaveEvents = false;

        PDMDevHlpPCISetIrqNoWait(pDevIns, 0, 0);
    }
    else
        vmmdevSetIRQ_Legacy(pDevIns, pThis, pThisCC);
    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_CtlGuestFilterMask.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_CtlGuestFilterMask(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    VMMDevCtlGuestFilterMask *pReq = (VMMDevCtlGuestFilterMask *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    LogRelFlow(("VMMDev: vmmdevReqHandler_CtlGuestFilterMask: OR mask: %#x, NOT mask: %#x\n", pReq->u32OrMask, pReq->u32NotMask));

    /* HGCM event notification is enabled by the VMMDev device
     * automatically when any HGCM command is issued.  The guest
     * cannot disable these notifications. */
    VMMDevCtlSetGuestFilterMask(pDevIns, pThis, pThisCC, pReq->u32OrMask, pReq->u32NotMask & ~VMMDEV_EVENT_HGCM);
    return VINF_SUCCESS;
}

#ifdef VBOX_WITH_HGCM

/**
 * Handles VMMDevReq_HGCMConnect.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 * @param   GCPhysReqHdr    The guest physical address of the request header.
 */
static int vmmdevReqHandler_HGCMConnect(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC,
                                        VMMDevRequestHeader *pReqHdr, RTGCPHYS GCPhysReqHdr)
{
    VMMDevHGCMConnect *pReq = (VMMDevHGCMConnect *)pReqHdr;
    AssertMsgReturn(pReq->header.header.size >= sizeof(*pReq), ("%u\n", pReq->header.header.size), VERR_INVALID_PARAMETER); /** @todo Not sure why this is >= ... */

    if (pThisCC->pHGCMDrv)
    {
        Log(("VMMDevReq_HGCMConnect\n"));
        return vmmdevR3HgcmConnect(pDevIns, pThis, pThisCC, pReq, GCPhysReqHdr);
    }

    Log(("VMMDevReq_HGCMConnect: HGCM Connector is NULL!\n"));
    return VERR_NOT_SUPPORTED;
}


/**
 * Handles VMMDevReq_HGCMDisconnect.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 * @param   GCPhysReqHdr    The guest physical address of the request header.
 */
static int vmmdevReqHandler_HGCMDisconnect(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC,
                                           VMMDevRequestHeader *pReqHdr, RTGCPHYS GCPhysReqHdr)
{
    VMMDevHGCMDisconnect *pReq = (VMMDevHGCMDisconnect *)pReqHdr;
    AssertMsgReturn(pReq->header.header.size >= sizeof(*pReq), ("%u\n", pReq->header.header.size), VERR_INVALID_PARAMETER);  /** @todo Not sure why this >= ... */

    if (pThisCC->pHGCMDrv)
    {
        Log(("VMMDevReq_VMMDevHGCMDisconnect\n"));
        return vmmdevR3HgcmDisconnect(pDevIns, pThis, pThisCC, pReq, GCPhysReqHdr);
    }

    Log(("VMMDevReq_VMMDevHGCMDisconnect: HGCM Connector is NULL!\n"));
    return VERR_NOT_SUPPORTED;
}


/**
 * Handles VMMDevReq_HGCMCall32 and VMMDevReq_HGCMCall64.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 * @param   GCPhysReqHdr    The guest physical address of the request header.
 * @param   tsArrival       The STAM_GET_TS() value when the request arrived.
 * @param   ppLock          Pointer to the lock info pointer (latter can be
 *                          NULL).  Set to NULL if HGCM takes lock ownership.
 */
static int vmmdevReqHandler_HGCMCall(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr,
                                     RTGCPHYS GCPhysReqHdr, uint64_t tsArrival, PVMMDEVREQLOCK *ppLock)
{
    VMMDevHGCMCall *pReq = (VMMDevHGCMCall *)pReqHdr;
    AssertMsgReturn(pReq->header.header.size >= sizeof(*pReq), ("%u\n", pReq->header.header.size), VERR_INVALID_PARAMETER);

    if (pThisCC->pHGCMDrv)
    {
        Log2(("VMMDevReq_HGCMCall: sizeof(VMMDevHGCMRequest) = %04X\n", sizeof(VMMDevHGCMCall)));
        Log2(("%.*Rhxd\n", pReq->header.header.size, pReq));

        return vmmdevR3HgcmCall(pDevIns, pThis, pThisCC, pReq, pReq->header.header.size, GCPhysReqHdr,
                                pReq->header.header.requestType, tsArrival, ppLock);
    }

    Log(("VMMDevReq_HGCMCall: HGCM Connector is NULL!\n"));
    return VERR_NOT_SUPPORTED;
}

/**
 * Handles VMMDevReq_HGCMCancel.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 * @param   GCPhysReqHdr    The guest physical address of the request header.
 */
static int vmmdevReqHandler_HGCMCancel(PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr, RTGCPHYS GCPhysReqHdr)
{
    VMMDevHGCMCancel *pReq = (VMMDevHGCMCancel *)pReqHdr;
    AssertMsgReturn(pReq->header.header.size >= sizeof(*pReq), ("%u\n", pReq->header.header.size), VERR_INVALID_PARAMETER);  /** @todo Not sure why this >= ... */

    if (pThisCC->pHGCMDrv)
    {
        Log(("VMMDevReq_VMMDevHGCMCancel\n"));
        return vmmdevR3HgcmCancel(pThisCC, pReq, GCPhysReqHdr);
    }

    Log(("VMMDevReq_VMMDevHGCMCancel: HGCM Connector is NULL!\n"));
    return VERR_NOT_SUPPORTED;
}


/**
 * Handles VMMDevReq_HGCMCancel2.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_HGCMCancel2(PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    /* Note! Using '>=' for the size check because that simplifies amending the
             structure (like we did already) */
    RTGCPHYS GCPhysReqToCancel;
    if (pReqHdr->size >= sizeof(VMMDevHGCMCancel2))
        GCPhysReqToCancel = ((VMMDevHGCMCancel2 const *)pReqHdr)->physReqToCancel;
    else if (pReqHdr->size == sizeof(VMMDevHGCMCancel2Old))
        GCPhysReqToCancel = ((VMMDevHGCMCancel2Old const *)pReqHdr)->physReqToCancel;
    else
        AssertMsgFailedReturn(("%u\n", pReqHdr->size), VERR_INVALID_PARAMETER);
    RT_UNTRUSTED_VALIDATED_FENCE();

    if (pThisCC->pHGCMDrv)
    {
        Log(("VMMDevReq_HGCMCancel2: %RGp\n", GCPhysReqToCancel));
        return vmmdevR3HgcmCancel2(pThisCC, GCPhysReqToCancel);
    }

    Log(("VMMDevReq_HGCMCancel2: HGCM Connector is NULL!\n"));
    return VERR_NOT_SUPPORTED;
}

#endif /* VBOX_WITH_HGCM */


/**
 * Handles VMMDevReq_VideoAccelEnable.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_VideoAccelEnable(PVMMDEV pThis, PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    VMMDevVideoAccelEnable *pReq = (VMMDevVideoAccelEnable *)pReqHdr;
    AssertMsgReturn(pReq->header.size >= sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);  /** @todo Not sure why this >= ... */

    if (!pThisCC->pDrv)
    {
        Log(("VMMDevReq_VideoAccelEnable Connector is NULL!!\n"));
        return VERR_NOT_SUPPORTED;
    }

    if (pReq->cbRingBuffer != VMMDEV_VBVA_RING_BUFFER_SIZE)
    {
        /* The guest driver seems compiled with different headers. */
        LogRelMax(16,("VMMDevReq_VideoAccelEnable guest ring buffer size %#x, should be %#x!!\n", pReq->cbRingBuffer, VMMDEV_VBVA_RING_BUFFER_SIZE));
        return VERR_INVALID_PARAMETER;
    }

    /* The request is correct. */
    pReq->fu32Status |= VBVA_F_STATUS_ACCEPTED;

    LogFlow(("VMMDevReq_VideoAccelEnable pReq->u32Enable = %d\n", pReq->u32Enable));

    int rc = pReq->u32Enable
           ? pThisCC->pDrv->pfnVideoAccelEnable(pThisCC->pDrv, true, &pThisCC->pVMMDevRAMR3->vbvaMemory)
           : pThisCC->pDrv->pfnVideoAccelEnable(pThisCC->pDrv, false, NULL);

    if (   pReq->u32Enable
        && RT_SUCCESS(rc))
    {
        pReq->fu32Status |= VBVA_F_STATUS_ENABLED;

        /* Remember that guest successfully enabled acceleration.
         * We need to reestablish it on restoring the VM from saved state.
         */
        pThis->u32VideoAccelEnabled = 1;
    }
    else
    {
        /* The acceleration was not enabled. Remember that. */
        pThis->u32VideoAccelEnabled = 0;
    }
    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_VideoAccelFlush.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_VideoAccelFlush(PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    VMMDevVideoAccelFlush *pReq = (VMMDevVideoAccelFlush *)pReqHdr;
    AssertMsgReturn(pReq->header.size >= sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);  /** @todo Not sure why this >= ... */

    if (!pThisCC->pDrv)
    {
        Log(("VMMDevReq_VideoAccelFlush: Connector is NULL!!!\n"));
        return VERR_NOT_SUPPORTED;
    }

    pThisCC->pDrv->pfnVideoAccelFlush(pThisCC->pDrv);
    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_VideoSetVisibleRegion.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_VideoSetVisibleRegion(PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    VMMDevVideoSetVisibleRegion *pReq = (VMMDevVideoSetVisibleRegion *)pReqHdr;
    AssertMsgReturn(pReq->header.size + sizeof(RTRECT) >= sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    if (!pThisCC->pDrv)
    {
        Log(("VMMDevReq_VideoSetVisibleRegion: Connector is NULL!!!\n"));
        return VERR_NOT_SUPPORTED;
    }

    if (   pReq->cRect > _1M /* restrict to sane range */
        || pReq->header.size != sizeof(VMMDevVideoSetVisibleRegion) + pReq->cRect * sizeof(RTRECT) - sizeof(RTRECT))
    {
        Log(("VMMDevReq_VideoSetVisibleRegion: cRects=%#x doesn't match size=%#x or is out of bounds\n",
             pReq->cRect, pReq->header.size));
        return VERR_INVALID_PARAMETER;
    }

    Log(("VMMDevReq_VideoSetVisibleRegion %d rectangles\n", pReq->cRect));
    /* forward the call */
    return pThisCC->pDrv->pfnSetVisibleRegion(pThisCC->pDrv, pReq->cRect, &pReq->Rect);
}

/**
 * Handles VMMDevReq_VideoUpdateMonitorPositions.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_VideoUpdateMonitorPositions(PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    VMMDevVideoUpdateMonitorPositions *pReq = (VMMDevVideoUpdateMonitorPositions *)pReqHdr;
    AssertMsgReturn(pReq->header.size + sizeof(RTRECT) >= sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);
    if (!pThisCC->pDrv)
    {
        Log(("VMMDevReq_VideoUpdateMonitorPositions: Connector is NULL!!!\n"));
        return VERR_NOT_SUPPORTED;
    }
    if (   pReq->cPositions > _1M /* restrict to sane range */
        || pReq->header.size != sizeof(VMMDevVideoUpdateMonitorPositions) + pReq->cPositions * sizeof(RTPOINT) - sizeof(RTPOINT))
    {
        Log(("VMMDevReq_VideoUpdateMonitorPositions: cRects=%#x doesn't match size=%#x or is out of bounds\n",
             pReq->cPositions, pReq->header.size));
        return VERR_INVALID_PARAMETER;
    }
    Log(("VMMDevReq_VideoUpdateMonitorPositions %d rectangles\n", pReq->cPositions));
    /* forward the call */
    return pThisCC->pDrv->pfnUpdateMonitorPositions(pThisCC->pDrv, pReq->cPositions, &(pReq->aPositions[0]));
}

/**
 * Handles VMMDevReq_GetSeamlessChangeRequest.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_GetSeamlessChangeRequest(PVMMDEV pThis, VMMDevRequestHeader *pReqHdr)
{
    VMMDevSeamlessChangeRequest *pReq = (VMMDevSeamlessChangeRequest *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    /* just pass on the information */
    Log(("VMMDev: returning seamless change request mode=%d\n", pThis->fSeamlessEnabled));
    if (pThis->fSeamlessEnabled)
        pReq->mode = VMMDev_Seamless_Visible_Region;
    else
        pReq->mode = VMMDev_Seamless_Disabled;

    if (pReq->eventAck == VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST)
    {
        /* Remember which mode the client has queried. */
        pThis->fLastSeamlessEnabled = pThis->fSeamlessEnabled;
    }

    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_GetVRDPChangeRequest.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_GetVRDPChangeRequest(PVMMDEV pThis, VMMDevRequestHeader *pReqHdr)
{
    VMMDevVRDPChangeRequest *pReq = (VMMDevVRDPChangeRequest *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    /* just pass on the information */
    Log(("VMMDev: returning VRDP status %d level %d\n", pThis->fVRDPEnabled, pThis->uVRDPExperienceLevel));

    pReq->u8VRDPActive = pThis->fVRDPEnabled;
    pReq->u32VRDPExperienceLevel = pThis->uVRDPExperienceLevel;

    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_GetMemBalloonChangeRequest.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_GetMemBalloonChangeRequest(PVMMDEV pThis, VMMDevRequestHeader *pReqHdr)
{
    VMMDevGetMemBalloonChangeRequest *pReq = (VMMDevGetMemBalloonChangeRequest *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    /* just pass on the information */
    Log(("VMMDev: returning memory balloon size =%d\n", pThis->cMbMemoryBalloon));
    pReq->cBalloonChunks = pThis->cMbMemoryBalloon;
    pReq->cPhysMemChunks = pThis->cbGuestRAM / (uint64_t)_1M;

    if (pReq->eventAck == VMMDEV_EVENT_BALLOON_CHANGE_REQUEST)
    {
        /* Remember which mode the client has queried. */
        pThis->cMbMemoryBalloonLast = pThis->cMbMemoryBalloon;
    }

    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_ChangeMemBalloon.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_ChangeMemBalloon(PPDMDEVINS pDevIns, PVMMDEV pThis, VMMDevRequestHeader *pReqHdr)
{
    VMMDevChangeMemBalloon *pReq = (VMMDevChangeMemBalloon *)pReqHdr;
    AssertMsgReturn(pReq->header.size >= sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);
    AssertMsgReturn(pReq->cPages      == VMMDEV_MEMORY_BALLOON_CHUNK_PAGES, ("%u\n", pReq->cPages), VERR_INVALID_PARAMETER);
    AssertMsgReturn(pReq->header.size == (uint32_t)RT_UOFFSETOF_DYN(VMMDevChangeMemBalloon, aPhysPage[pReq->cPages]),
                    ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    Log(("VMMDevReq_ChangeMemBalloon\n"));
    int rc = PDMDevHlpPhysChangeMemBalloon(pDevIns, !!pReq->fInflate, pReq->cPages, pReq->aPhysPage);
    if (pReq->fInflate)
        STAM_REL_U32_INC(&pThis->StatMemBalloonChunks);
    else
        STAM_REL_U32_DEC(&pThis->StatMemBalloonChunks);
    return rc;
}


/**
 * Handles VMMDevReq_GetStatisticsChangeRequest.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_GetStatisticsChangeRequest(PVMMDEV pThis, VMMDevRequestHeader *pReqHdr)
{
    VMMDevGetStatisticsChangeRequest *pReq = (VMMDevGetStatisticsChangeRequest *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    Log(("VMMDevReq_GetStatisticsChangeRequest\n"));
    /* just pass on the information */
    Log(("VMMDev: returning statistics interval %d seconds\n", pThis->cSecsStatInterval));
    pReq->u32StatInterval = pThis->cSecsStatInterval;

    if (pReq->eventAck == VMMDEV_EVENT_STATISTICS_INTERVAL_CHANGE_REQUEST)
    {
        /* Remember which mode the client has queried. */
        pThis->cSecsLastStatInterval = pThis->cSecsStatInterval;
    }

    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_ReportGuestStats.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_ReportGuestStats(PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    VMMDevReportGuestStats *pReq = (VMMDevReportGuestStats *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    Log(("VMMDevReq_ReportGuestStats\n"));
#ifdef LOG_ENABLED
    VBoxGuestStatistics *pGuestStats = &pReq->guestStats;

    Log(("Current statistics:\n"));
    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_CPU_LOAD_IDLE)
        Log(("CPU%u: CPU Load Idle          %-3d%%\n", pGuestStats->u32CpuId, pGuestStats->u32CpuLoad_Idle));

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_CPU_LOAD_KERNEL)
        Log(("CPU%u: CPU Load Kernel        %-3d%%\n", pGuestStats->u32CpuId, pGuestStats->u32CpuLoad_Kernel));

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_CPU_LOAD_USER)
        Log(("CPU%u: CPU Load User          %-3d%%\n", pGuestStats->u32CpuId, pGuestStats->u32CpuLoad_User));

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_THREADS)
        Log(("CPU%u: Thread                 %d\n", pGuestStats->u32CpuId, pGuestStats->u32Threads));

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_PROCESSES)
        Log(("CPU%u: Processes              %d\n", pGuestStats->u32CpuId, pGuestStats->u32Processes));

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_HANDLES)
        Log(("CPU%u: Handles                %d\n", pGuestStats->u32CpuId, pGuestStats->u32Handles));

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_MEMORY_LOAD)
        Log(("CPU%u: Memory Load            %d%%\n", pGuestStats->u32CpuId, pGuestStats->u32MemoryLoad));

    /* Note that reported values are in pages; upper layers expect them in megabytes */
    Log(("CPU%u: Page size              %-4d bytes\n", pGuestStats->u32CpuId, pGuestStats->u32PageSize));
    Assert(pGuestStats->u32PageSize == 4096);

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_PHYS_MEM_TOTAL)
        Log(("CPU%u: Total physical memory  %-4d MB\n", pGuestStats->u32CpuId, (pGuestStats->u32PhysMemTotal + (_1M/_4K)-1) / (_1M/_4K)));

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_PHYS_MEM_AVAIL)
        Log(("CPU%u: Free physical memory   %-4d MB\n", pGuestStats->u32CpuId, pGuestStats->u32PhysMemAvail / (_1M/_4K)));

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_PHYS_MEM_BALLOON)
        Log(("CPU%u: Memory balloon size    %-4d MB\n", pGuestStats->u32CpuId, pGuestStats->u32PhysMemBalloon / (_1M/_4K)));

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_MEM_COMMIT_TOTAL)
        Log(("CPU%u: Committed memory       %-4d MB\n", pGuestStats->u32CpuId, pGuestStats->u32MemCommitTotal / (_1M/_4K)));

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_MEM_KERNEL_TOTAL)
        Log(("CPU%u: Total kernel memory    %-4d MB\n", pGuestStats->u32CpuId, pGuestStats->u32MemKernelTotal / (_1M/_4K)));

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_MEM_KERNEL_PAGED)
        Log(("CPU%u: Paged kernel memory    %-4d MB\n", pGuestStats->u32CpuId, pGuestStats->u32MemKernelPaged / (_1M/_4K)));

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_MEM_KERNEL_NONPAGED)
        Log(("CPU%u: Nonpaged kernel memory %-4d MB\n", pGuestStats->u32CpuId, pGuestStats->u32MemKernelNonPaged / (_1M/_4K)));

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_MEM_SYSTEM_CACHE)
        Log(("CPU%u: System cache size      %-4d MB\n", pGuestStats->u32CpuId, pGuestStats->u32MemSystemCache / (_1M/_4K)));

    if (pGuestStats->u32StatCaps & VBOX_GUEST_STAT_PAGE_FILE_SIZE)
        Log(("CPU%u: Page file size         %-4d MB\n", pGuestStats->u32CpuId, pGuestStats->u32PageFileSize / (_1M/_4K)));
    Log(("Statistics end *******************\n"));
#endif /* LOG_ENABLED */

    /* forward the call */
    return pThisCC->pDrv->pfnReportStatistics(pThisCC->pDrv, &pReq->guestStats);
}


/**
 * Handles VMMDevReq_QueryCredentials.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_QueryCredentials(PVMMDEV pThis, PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    VMMDevCredentials *pReq = (VMMDevCredentials *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);
    VMMDEVCREDS *pCredentials = pThisCC->pCredentials;
    AssertPtrReturn(pCredentials, VERR_NOT_SUPPORTED);

    /* let's start by nulling out the data */
    RT_ZERO(pReq->szUserName);
    RT_ZERO(pReq->szPassword);
    RT_ZERO(pReq->szDomain);

    /* should we return whether we got credentials for a logon? */
    if (pReq->u32Flags & VMMDEV_CREDENTIALS_QUERYPRESENCE)
    {
        if (   pCredentials->Logon.szUserName[0]
            || pCredentials->Logon.szPassword[0]
            || pCredentials->Logon.szDomain[0])
            pReq->u32Flags |= VMMDEV_CREDENTIALS_PRESENT;
        else
            pReq->u32Flags &= ~VMMDEV_CREDENTIALS_PRESENT;
    }

    /* does the guest want to read logon credentials? */
    if (pReq->u32Flags & VMMDEV_CREDENTIALS_READ)
    {
        if (pCredentials->Logon.szUserName[0])
            RTStrCopy(pReq->szUserName, sizeof(pReq->szUserName), pCredentials->Logon.szUserName);
        if (pCredentials->Logon.szPassword[0])
            RTStrCopy(pReq->szPassword, sizeof(pReq->szPassword), pCredentials->Logon.szPassword);
        if (pCredentials->Logon.szDomain[0])
            RTStrCopy(pReq->szDomain, sizeof(pReq->szDomain), pCredentials->Logon.szDomain);
        if (!pCredentials->Logon.fAllowInteractiveLogon)
            pReq->u32Flags |= VMMDEV_CREDENTIALS_NOLOCALLOGON;
        else
            pReq->u32Flags &= ~VMMDEV_CREDENTIALS_NOLOCALLOGON;
    }

    if (!pThis->fKeepCredentials)
    {
        /* does the caller want us to destroy the logon credentials? */
        if (pReq->u32Flags & VMMDEV_CREDENTIALS_CLEAR)
        {
            RT_ZERO(pCredentials->Logon.szUserName);
            RT_ZERO(pCredentials->Logon.szPassword);
            RT_ZERO(pCredentials->Logon.szDomain);
        }
    }

    /* does the guest want to read credentials for verification? */
    if (pReq->u32Flags & VMMDEV_CREDENTIALS_READJUDGE)
    {
        if (pCredentials->Judge.szUserName[0])
            RTStrCopy(pReq->szUserName, sizeof(pReq->szUserName), pCredentials->Judge.szUserName);
        if (pCredentials->Judge.szPassword[0])
            RTStrCopy(pReq->szPassword, sizeof(pReq->szPassword), pCredentials->Judge.szPassword);
        if (pCredentials->Judge.szDomain[0])
            RTStrCopy(pReq->szDomain, sizeof(pReq->szDomain), pCredentials->Judge.szDomain);
    }

    /* does the caller want us to destroy the judgement credentials? */
    if (pReq->u32Flags & VMMDEV_CREDENTIALS_CLEARJUDGE)
    {
        RT_ZERO(pCredentials->Judge.szUserName);
        RT_ZERO(pCredentials->Judge.szPassword);
        RT_ZERO(pCredentials->Judge.szDomain);
    }

    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_ReportCredentialsJudgement.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_ReportCredentialsJudgement(PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    VMMDevCredentials *pReq = (VMMDevCredentials *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    /* what does the guest think about the credentials? (note: the order is important here!) */
    if (pReq->u32Flags & VMMDEV_CREDENTIALS_JUDGE_DENY)
        pThisCC->pDrv->pfnSetCredentialsJudgementResult(pThisCC->pDrv, VMMDEV_CREDENTIALS_JUDGE_DENY);
    else if (pReq->u32Flags & VMMDEV_CREDENTIALS_JUDGE_NOJUDGEMENT)
        pThisCC->pDrv->pfnSetCredentialsJudgementResult(pThisCC->pDrv, VMMDEV_CREDENTIALS_JUDGE_NOJUDGEMENT);
    else if (pReq->u32Flags & VMMDEV_CREDENTIALS_JUDGE_OK)
        pThisCC->pDrv->pfnSetCredentialsJudgementResult(pThisCC->pDrv, VMMDEV_CREDENTIALS_JUDGE_OK);
    else
    {
        Log(("VMMDevReq_ReportCredentialsJudgement: invalid flags: %d!!!\n", pReq->u32Flags));
        /** @todo why don't we return VERR_INVALID_PARAMETER to the guest? */
    }

    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_GetHostVersion.
 *
 * @returns VBox status code that the guest should see.
 * @param   pReqHdr         The header of the request to handle.
 * @since   3.1.0
 * @note    The ring-0 VBoxGuestLib uses this to check whether
 *          VMMDevHGCMParmType_PageList is supported.
 */
static int vmmdevReqHandler_GetHostVersion(VMMDevRequestHeader *pReqHdr)
{
    VMMDevReqHostVersion *pReq = (VMMDevReqHostVersion *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    pReq->major     = RTBldCfgVersionMajor();
    pReq->minor     = RTBldCfgVersionMinor();
    pReq->build     = RTBldCfgVersionBuild();
    pReq->revision  = RTBldCfgRevision();
    pReq->features  = VMMDEV_HVF_HGCM_PHYS_PAGE_LIST
                    | VMMDEV_HVF_HGCM_EMBEDDED_BUFFERS
                    | VMMDEV_HVF_HGCM_CONTIGUOUS_PAGE_LIST
                    | VMMDEV_HVF_HGCM_NO_BOUNCE_PAGE_LIST
                    | VMMDEV_HVF_FAST_IRQ_ACK;
    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_GetCpuHotPlugRequest.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_GetCpuHotPlugRequest(PVMMDEV pThis, VMMDevRequestHeader *pReqHdr)
{
    VMMDevGetCpuHotPlugRequest *pReq = (VMMDevGetCpuHotPlugRequest *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    pReq->enmEventType  = pThis->enmCpuHotPlugEvent;
    pReq->idCpuCore     = pThis->idCpuCore;
    pReq->idCpuPackage  = pThis->idCpuPackage;

    /* Clear the event */
    pThis->enmCpuHotPlugEvent = VMMDevCpuEventType_None;
    pThis->idCpuCore          = UINT32_MAX;
    pThis->idCpuPackage       = UINT32_MAX;

    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_SetCpuHotPlugStatus.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_SetCpuHotPlugStatus(PVMMDEV pThis, VMMDevRequestHeader *pReqHdr)
{
    VMMDevCpuHotPlugStatusRequest *pReq = (VMMDevCpuHotPlugStatusRequest *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    if (pReq->enmStatusType == VMMDevCpuStatusType_Disable)
        pThis->fCpuHotPlugEventsEnabled = false;
    else if (pReq->enmStatusType == VMMDevCpuStatusType_Enable)
        pThis->fCpuHotPlugEventsEnabled = true;
    else
        return VERR_INVALID_PARAMETER;
    return VINF_SUCCESS;
}


#ifdef DEBUG
/**
 * Handles VMMDevReq_LogString.
 *
 * @returns VBox status code that the guest should see.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_LogString(VMMDevRequestHeader *pReqHdr)
{
    VMMDevReqLogString *pReq = (VMMDevReqLogString *)pReqHdr;
    AssertMsgReturn(pReq->header.size >= sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);
    AssertMsgReturn(pReq->szString[pReq->header.size - RT_UOFFSETOF(VMMDevReqLogString, szString) - 1] == '\0',
                    ("not null terminated\n"), VERR_INVALID_PARAMETER);

    LogIt(RTLOGGRPFLAGS_LEVEL_1, LOG_GROUP_DEV_VMM_BACKDOOR, ("DEBUG LOG: %s", pReq->szString));
    return VINF_SUCCESS;
}
#endif /* DEBUG */

/**
 * Handles VMMDevReq_GetSessionId.
 *
 * Get a unique "session" ID for this VM, where the ID will be different after each
 * start, reset or restore of the VM.  This can be used for restore detection
 * inside the guest.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_GetSessionId(PVMMDEV pThis, VMMDevRequestHeader *pReqHdr)
{
    VMMDevReqSessionId *pReq = (VMMDevReqSessionId *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(*pReq), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    pReq->idSession = pThis->idSession;
    return VINF_SUCCESS;
}


#ifdef VBOX_WITH_PAGE_SHARING

/**
 * Handles VMMDevReq_RegisterSharedModule.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_RegisterSharedModule(PPDMDEVINS pDevIns, VMMDevRequestHeader *pReqHdr)
{
    /*
     * Basic input validation (more done by GMM).
     */
    VMMDevSharedModuleRegistrationRequest *pReq = (VMMDevSharedModuleRegistrationRequest *)pReqHdr;
    AssertMsgReturn(pReq->header.size >= sizeof(VMMDevSharedModuleRegistrationRequest),
                    ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);
    AssertMsgReturn(pReq->header.size == RT_UOFFSETOF_DYN(VMMDevSharedModuleRegistrationRequest, aRegions[pReq->cRegions]),
                    ("%u cRegions=%u\n", pReq->header.size, pReq->cRegions), VERR_INVALID_PARAMETER);

    AssertReturn(RTStrEnd(pReq->szName, sizeof(pReq->szName)), VERR_INVALID_PARAMETER);
    AssertReturn(RTStrEnd(pReq->szVersion, sizeof(pReq->szVersion)), VERR_INVALID_PARAMETER);
    int rc = RTStrValidateEncoding(pReq->szName);
    AssertRCReturn(rc, rc);
    rc = RTStrValidateEncoding(pReq->szVersion);
    AssertRCReturn(rc, rc);

    /*
     * Forward the request to the VMM.
     */
    return PDMDevHlpSharedModuleRegister(pDevIns, pReq->enmGuestOS, pReq->szName, pReq->szVersion,
                                         pReq->GCBaseAddr, pReq->cbModule, pReq->cRegions, pReq->aRegions);
}

/**
 * Handles VMMDevReq_UnregisterSharedModule.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_UnregisterSharedModule(PPDMDEVINS pDevIns, VMMDevRequestHeader *pReqHdr)
{
    /*
     * Basic input validation.
     */
    VMMDevSharedModuleUnregistrationRequest *pReq = (VMMDevSharedModuleUnregistrationRequest *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(VMMDevSharedModuleUnregistrationRequest),
                    ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    AssertReturn(RTStrEnd(pReq->szName, sizeof(pReq->szName)), VERR_INVALID_PARAMETER);
    AssertReturn(RTStrEnd(pReq->szVersion, sizeof(pReq->szVersion)), VERR_INVALID_PARAMETER);
    int rc = RTStrValidateEncoding(pReq->szName);
    AssertRCReturn(rc, rc);
    rc = RTStrValidateEncoding(pReq->szVersion);
    AssertRCReturn(rc, rc);

    /*
     * Forward the request to the VMM.
     */
    return PDMDevHlpSharedModuleUnregister(pDevIns, pReq->szName, pReq->szVersion,
                                           pReq->GCBaseAddr, pReq->cbModule);
}

/**
 * Handles VMMDevReq_CheckSharedModules.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_CheckSharedModules(PPDMDEVINS pDevIns, VMMDevRequestHeader *pReqHdr)
{
    VMMDevSharedModuleCheckRequest *pReq = (VMMDevSharedModuleCheckRequest *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(VMMDevSharedModuleCheckRequest),
                    ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);
    return PDMDevHlpSharedModuleCheckAll(pDevIns);
}

/**
 * Handles VMMDevReq_GetPageSharingStatus.
 *
 * @returns VBox status code that the guest should see.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_GetPageSharingStatus(PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr)
{
    VMMDevPageSharingStatusRequest *pReq = (VMMDevPageSharingStatusRequest *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(VMMDevPageSharingStatusRequest),
                    ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    pReq->fEnabled = false;
    int rc = pThisCC->pDrv->pfnIsPageFusionEnabled(pThisCC->pDrv, &pReq->fEnabled);
    if (RT_FAILURE(rc))
        pReq->fEnabled = false;
    return VINF_SUCCESS;
}


/**
 * Handles VMMDevReq_DebugIsPageShared.
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pReqHdr         The header of the request to handle.
 */
static int vmmdevReqHandler_DebugIsPageShared(PPDMDEVINS pDevIns, VMMDevRequestHeader *pReqHdr)
{
    VMMDevPageIsSharedRequest *pReq = (VMMDevPageIsSharedRequest *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(VMMDevPageIsSharedRequest),
                    ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    return PDMDevHlpSharedModuleGetPageState(pDevIns, pReq->GCPtrPage, &pReq->fShared, &pReq->uPageFlags);
}

#endif /* VBOX_WITH_PAGE_SHARING */


/**
 * Handles VMMDevReq_WriteCoreDumpe
 *
 * @returns VBox status code that the guest should see.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pReqHdr         Pointer to the request header.
 */
static int vmmdevReqHandler_WriteCoreDump(PPDMDEVINS pDevIns, PVMMDEV pThis, VMMDevRequestHeader *pReqHdr)
{
    VMMDevReqWriteCoreDump *pReq = (VMMDevReqWriteCoreDump *)pReqHdr;
    AssertMsgReturn(pReq->header.size == sizeof(VMMDevReqWriteCoreDump), ("%u\n", pReq->header.size), VERR_INVALID_PARAMETER);

    /*
     * Only available if explicitly enabled by the user.
     */
    if (!pThis->fGuestCoreDumpEnabled)
        return VERR_ACCESS_DENIED;

    /*
     * User makes sure the directory exists before composing the path.
     */
    if (!RTDirExists(pThis->szGuestCoreDumpDir))
        return VERR_PATH_NOT_FOUND;

    char szCorePath[RTPATH_MAX];
    RTStrCopy(szCorePath, sizeof(szCorePath), pThis->szGuestCoreDumpDir);
    RTPathAppend(szCorePath, sizeof(szCorePath), "VBox.core");

    /*
     * Rotate existing cores based on number of additional cores to keep around.
     */
    if (pThis->cGuestCoreDumps > 0)
        for (int64_t i = pThis->cGuestCoreDumps - 1; i >= 0; i--)
        {
            char szFilePathOld[RTPATH_MAX];
            if (i == 0)
                RTStrCopy(szFilePathOld, sizeof(szFilePathOld), szCorePath);
            else
                RTStrPrintf(szFilePathOld, sizeof(szFilePathOld), "%s.%lld", szCorePath, i);

            char szFilePathNew[RTPATH_MAX];
            RTStrPrintf(szFilePathNew, sizeof(szFilePathNew), "%s.%lld", szCorePath, i + 1);
            int vrc = RTFileMove(szFilePathOld, szFilePathNew, RTFILEMOVE_FLAGS_REPLACE);
            if (vrc == VERR_FILE_NOT_FOUND)
                RTFileDelete(szFilePathNew);
        }

    /*
     * Write the core file.
     */
    return PDMDevHlpDBGFCoreWrite(pDevIns, szCorePath, true /*fReplaceFile*/);
}


/**
 * Sets request status to VINF_HGCM_ASYNC_EXECUTE.
 *
 * @param   pDevIns         The device instance.
 * @param   GCPhysReqHdr    The guest physical address of the request.
 * @param   pLock           Pointer to the request locking info.  NULL if not
 *                          locked.
 */
DECLINLINE(void) vmmdevReqHdrSetHgcmAsyncExecute(PPDMDEVINS pDevIns, RTGCPHYS GCPhysReqHdr, PVMMDEVREQLOCK pLock)
{
    if (pLock)
        ((VMMDevRequestHeader volatile *)pLock->pvReq)->rc = VINF_HGCM_ASYNC_EXECUTE;
    else
    {
        int32_t rcReq = VINF_HGCM_ASYNC_EXECUTE;
        PDMDevHlpPhysWrite(pDevIns, GCPhysReqHdr + RT_UOFFSETOF(VMMDevRequestHeader, rc), &rcReq, sizeof(rcReq));
    }
}


/** @name VMMDEVREQDISP_POST_F_XXX - post dispatcher optimizations.
 * @{ */
#define VMMDEVREQDISP_POST_F_NO_WRITE_OUT    RT_BIT_32(0)
/** @} */


/**
 * Dispatch the request to the appropriate handler function.
 *
 * @returns Port I/O handler exit code.
 * @param   pDevIns         The device instance.
 * @param   pThis           The VMMDev shared instance data.
 * @param   pThisCC         The VMMDev ring-3 instance data.
 * @param   pReqHdr         The request header (cached in host memory).
 * @param   GCPhysReqHdr    The guest physical address of the request (for
 *                          HGCM).
 * @param   tsArrival       The STAM_GET_TS() value when the request arrived.
 * @param   pfPostOptimize  HGCM optimizations, VMMDEVREQDISP_POST_F_XXX.
 * @param   ppLock          Pointer to the lock info pointer (latter can be
 *                          NULL).  Set to NULL if HGCM takes lock ownership.
 */
static VBOXSTRICTRC vmmdevReqDispatcher(PPDMDEVINS pDevIns, PVMMDEV pThis, PVMMDEVCC pThisCC, VMMDevRequestHeader *pReqHdr,
                                        RTGCPHYS GCPhysReqHdr, uint64_t tsArrival, uint32_t *pfPostOptimize,
                                        PVMMDEVREQLOCK *ppLock)
{
    int rcRet = VINF_SUCCESS;
    Assert(*pfPostOptimize == 0);
    switch (pReqHdr->requestType)
    {
        case VMMDevReq_ReportGuestInfo:
            pReqHdr->rc = vmmdevReqHandler_ReportGuestInfo(pDevIns, pThis, pThisCC, pReqHdr);
            break;

        case VMMDevReq_ReportGuestInfo2:
            pReqHdr->rc = vmmdevReqHandler_ReportGuestInfo2(pDevIns, pThis, pThisCC, pReqHdr);
            break;

        case VMMDevReq_ReportGuestStatus:
            pReqHdr->rc = vmmdevReqHandler_ReportGuestStatus(pThis, pThisCC, pReqHdr);
            break;

        case VMMDevReq_ReportGuestUserState:
            pReqHdr->rc = vmmdevReqHandler_ReportGuestUserState(pThisCC, pReqHdr);
            break;

        case VMMDevReq_ReportGuestCapabilities:
            pReqHdr->rc = vmmdevReqHandler_ReportGuestCapabilities(pThis, pThisCC, pReqHdr);
            break;

        case VMMDevReq_SetGuestCapabilities:
            pReqHdr->rc = vmmdevReqHandler_SetGuestCapabilities(pThis, pThisCC, pReqHdr);
            break;

        case VMMDevReq_WriteCoreDump:
            pReqHdr->rc = vmmdevReqHandler_WriteCoreDump(pDevIns, pThis, pReqHdr);
            break;

        case VMMDevReq_GetMouseStatus:
            pReqHdr->rc = vmmdevReqHandler_GetMouseStatus(pThis, pReqHdr);
            break;

        case VMMDevReq_GetMouseStatusEx:
            pReqHdr->rc = vmmdevReqHandler_GetMouseStatusEx(pThis, pReqHdr);
            break;

        case VMMDevReq_SetMouseStatus:
            pReqHdr->rc = vmmdevReqHandler_SetMouseStatus(pThis, pThisCC, pReqHdr);
            break;

        case VMMDevReq_SetPointerShape:
            pReqHdr->rc = vmmdevReqHandler_SetPointerShape(pThis, pThisCC, pReqHdr);
            break;

        case VMMDevReq_GetHostTime:
            pReqHdr->rc = vmmdevReqHandler_GetHostTime(pDevIns, pThis, pReqHdr);
            break;

        case VMMDevReq_GetHypervisorInfo:
            pReqHdr->rc = vmmdevReqHandler_GetHypervisorInfo(pDevIns, pReqHdr);
            break;

        case VMMDevReq_SetHypervisorInfo:
            pReqHdr->rc = vmmdevReqHandler_SetHypervisorInfo(pDevIns, pReqHdr);
            break;

        case VMMDevReq_RegisterPatchMemory:
            pReqHdr->rc = vmmdevReqHandler_RegisterPatchMemory(pDevIns, pReqHdr);
            break;

        case VMMDevReq_DeregisterPatchMemory:
            pReqHdr->rc = vmmdevReqHandler_DeregisterPatchMemory(pDevIns, pReqHdr);
            break;

        case VMMDevReq_SetPowerStatus:
        {
            int rc = pReqHdr->rc = vmmdevReqHandler_SetPowerStatus(pDevIns, pThis, pReqHdr);
            if (rc != VINF_SUCCESS && RT_SUCCESS(rc))
                rcRet = rc;
            break;
        }

        case VMMDevReq_GetDisplayChangeRequest:
            pReqHdr->rc = vmmdevReqHandler_GetDisplayChangeRequest(pThis, pReqHdr);
            break;

        case VMMDevReq_GetDisplayChangeRequest2:
            pReqHdr->rc = vmmdevReqHandler_GetDisplayChangeRequest2(pDevIns, pThis, pThisCC, pReqHdr);
            break;

        case VMMDevReq_GetDisplayChangeRequestEx:
            pReqHdr->rc = vmmdevReqHandler_GetDisplayChangeRequestEx(pDevIns, pThis, pThisCC, pReqHdr);
            break;

        case VMMDevReq_GetDisplayChangeRequestMulti:
            pReqHdr->rc = vmmdevReqHandler_GetDisplayChangeRequestMulti(pThis, pReqHdr);
            break;

        case VMMDevReq_VideoModeSupported:
            pReqHdr->rc = vmmdevReqHandler_VideoModeSupported(pThisCC, pReqHdr);
            break;

        case VMMDevReq_VideoModeSupported2:
            pReqHdr->rc = vmmdevReqHandler_VideoModeSupported2(pThisCC, pReqHdr);
            break;

        case VMMDevReq_GetHeightReduction:
            pReqHdr->rc = vmmdevReqHandler_GetHeightReduction(pThisCC, pReqHdr);
            break;

        case VMMDevReq_AcknowledgeEvents:
            pReqHdr->rc = vmmdevReqHandler_AcknowledgeEvents(pDevIns, pThis, pThisCC, pReqHdr);
            break;

        case VMMDevReq_CtlGuestFilterMask:
            pReqHdr->rc = vmmdevReqHandler_CtlGuestFilterMask(pDevIns, pThis, pThisCC, pReqHdr);
            break;

#ifdef VBOX_WITH_HGCM
        case VMMDevReq_HGCMConnect:
            vmmdevReqHdrSetHgcmAsyncExecute(pDevIns, GCPhysReqHdr, *ppLock);
            pReqHdr->rc = vmmdevReqHandler_HGCMConnect(pDevIns, pThis, pThisCC, pReqHdr, GCPhysReqHdr);
            Assert(pReqHdr->rc == VINF_HGCM_ASYNC_EXECUTE || RT_FAILURE_NP(pReqHdr->rc));
            if (RT_SUCCESS(pReqHdr->rc))
                *pfPostOptimize |= VMMDEVREQDISP_POST_F_NO_WRITE_OUT;
            break;

        case VMMDevReq_HGCMDisconnect:
            vmmdevReqHdrSetHgcmAsyncExecute(pDevIns, GCPhysReqHdr, *ppLock);
            pReqHdr->rc = vmmdevReqHandler_HGCMDisconnect(pDevIns, pThis, pThisCC, pReqHdr, GCPhysReqHdr);
            Assert(pReqHdr->rc == VINF_HGCM_ASYNC_EXECUTE || RT_FAILURE_NP(pReqHdr->rc));
            if (RT_SUCCESS(pReqHdr->rc))
                *pfPostOptimize |= VMMDEVREQDISP_POST_F_NO_WRITE_OUT;
            break;

# ifdef VBOX_WITH_64_BITS_GUESTS
        case VMMDevReq_HGCMCall64:
# endif
        case VMMDevReq_HGCMCall32:
            vmmdevReqHdrSetHgcmAsyncExecute(pDevIns, GCPhysReqHdr, *ppLock);
            pReqHdr->rc = vmmdevReqHandler_HGCMCall(pDevIns, pThis, pThisCC, pReqHdr, GCPhysReqHdr, tsArrival, ppLock);
            Assert(pReqHdr->rc == VINF_HGCM_ASYNC_EXECUTE || RT_FAILURE_NP(pReqHdr->rc));
            if (RT_SUCCESS(pReqHdr->rc))
                *pfPostOptimize |= VMMDEVREQDISP_POST_F_NO_WRITE_OUT;
            break;

        case VMMDevReq_HGCMCancel:
            pReqHdr->rc = vmmdevReqHandler_HGCMCancel(pThisCC, pReqHdr, GCPhysReqHdr);
            break;

        case VMMDevReq_HGCMCancel2:
            pReqHdr->rc = vmmdevReqHandler_HGCMCancel2(pThisCC, pReqHdr);
            break;
#endif /* VBOX_WITH_HGCM */

        case VMMDevReq_VideoAccelEnable:
            pReqHdr->rc = vmmdevReqHandler_VideoAccelEnable(pThis, pThisCC, pReqHdr);
            break;

        case VMMDevReq_VideoAccelFlush:
            pReqHdr->rc = vmmdevReqHandler_VideoAccelFlush(pThisCC, pReqHdr);
            break;

        case VMMDevReq_VideoSetVisibleRegion:
            pReqHdr->rc = vmmdevReqHandler_VideoSetVisibleRegion(pThisCC, pReqHdr);
            break;

        case VMMDevReq_VideoUpdateMonitorPositions:
            pReqHdr->rc = vmmdevReqHandler_VideoUpdateMonitorPositions(pThisCC, pReqHdr);
            break;

        case VMMDevReq_GetSeamlessChangeRequest:
            pReqHdr->rc = vmmdevReqHandler_GetSeamlessChangeRequest(pThis, pReqHdr);
            break;

        case VMMDevReq_GetVRDPChangeRequest:
            pReqHdr->rc = vmmdevReqHandler_GetVRDPChangeRequest(pThis, pReqHdr);
            break;

        case VMMDevReq_GetMemBalloonChangeRequest:
            pReqHdr->rc = vmmdevReqHandler_GetMemBalloonChangeRequest(pThis, pReqHdr);
            break;

        case VMMDevReq_ChangeMemBalloon:
            pReqHdr->rc = vmmdevReqHandler_ChangeMemBalloon(pDevIns, pThis, pReqHdr);
            break;

        case VMMDevReq_GetStatisticsChangeRequest:
            pReqHdr->rc = vmmdevReqHandler_GetStatisticsChangeRequest(pThis, pReqHdr);
            break;

        case VMMDevReq_ReportGuestStats:
            pReqHdr->rc = vmmdevReqHandler_ReportGuestStats(pThisCC, pReqHdr);
            break;

        case VMMDevReq_QueryCredentials:
            pReqHdr->rc = vmmdevReqHandler_QueryCredentials(pThis, pThisCC, pReqHdr);
            break;

        case VMMDevReq_ReportCredentialsJudgement:
            pReqHdr->rc = vmmdevReqHandler_ReportCredentialsJudgement(pThisCC, pReqHdr);
            break;

        case VMMDevReq_GetHostVersion:
            pReqHdr->rc = vmmdevReqHandler_GetHostVersion(pReqHdr);
            break;

        case VMMDevReq_GetCpuHotPlugRequest:
            pReqHdr->rc = vmmdevReqHandler_GetCpuHotPlugRequest(pThis, pReqHdr);
            break;

        case VMMDevReq_SetCpuHotPlugStatus:
            pReqHdr->rc = vmmdevReqHandler_SetCpuHotPlugStatus(pThis, pReqHdr);
            break;

#ifdef VBOX_WITH_PAGE_SHARING
        case VMMDevReq_RegisterSharedModule:
            pReqHdr->rc = vmmdevReqHandler_RegisterSharedModule(pDevIns, pReqHdr);
            break;

        case VMMDevReq_UnregisterSharedModule:
            pReqHdr->rc = vmmdevReqHandler_UnregisterSharedModule(pDevIns, pReqHdr);
            break;

        case VMMDevReq_CheckSharedModules:
            pReqHdr->rc = vmmdevReqHandler_CheckSharedModules(pDevIns, pReqHdr);
            break;

        case VMMDevReq_GetPageSharingStatus:
            pReqHdr->rc = vmmdevReqHandler_GetPageSharingStatus(pThisCC, pReqHdr);
            break;

        case VMMDevReq_DebugIsPageShared:
            pReqHdr->rc = vmmdevReqHandler_DebugIsPageShared(pDevIns, pReqHdr);
            break;

#endif /* VBOX_WITH_PAGE_SHARING */

#ifdef DEBUG
        case VMMDevReq_LogString:
            pReqHdr->rc = vmmdevReqHandler_LogString(pReqHdr);
            break;
#endif

        case VMMDevReq_GetSessionId:
            pReqHdr->rc = vmmdevReqHandler_GetSessionId(pThis, pReqHdr);
            break;

        /*
         * Guest wants to give up a timeslice.
         * Note! This was only ever used by experimental GAs!
         */
        /** @todo maybe we could just remove this? */
        case VMMDevReq_Idle:
        {
            /* just return to EMT telling it that we want to halt */
            rcRet = VINF_EM_HALT;
            break;
        }

        case VMMDevReq_GuestHeartbeat:
            pReqHdr->rc = vmmDevReqHandler_GuestHeartbeat(pDevIns, pThis);
            break;

        case VMMDevReq_HeartbeatConfigure:
            pReqHdr->rc = vmmDevReqHandler_HeartbeatConfigure(pDevIns, pThis, pReqHdr);
            break;

        case VMMDevReq_NtBugCheck:
            pReqHdr->rc = vmmDevReqHandler_NtBugCheck(pDevIns, pReqHdr);
            break;

        default:
        {
            pReqHdr->rc = VERR_NOT_IMPLEMENTED;
            Log(("VMMDev unknown request type %d\n", pReqHdr->requestType));
            break;
        }
    }
    return rcRet;
}


/**
 * The request handler shared by the PIO and MMIO access paths.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns         The device instance.
 * @param   GCPhysReqHdr    Physical address of the request header.
 */
static VBOXSTRICTRC vmmdevRequestHandler(PPDMDEVINS pDevIns, RTGCPHYS GCPhysReqHdr)
{
    uint64_t tsArrival;
    STAM_GET_TS(tsArrival);

    /*
     * The caller has passed the guest context physical address of the request
     * structure. We'll copy all of it into a heap buffer eventually, but we
     * will have to start off with the header.
     */
    VMMDevRequestHeader requestHeader;
    RT_ZERO(requestHeader);
    PDMDevHlpPhysRead(pDevIns, GCPhysReqHdr, &requestHeader, sizeof(requestHeader));

    /* The structure size must be greater or equal to the header size. */
    if (requestHeader.size < sizeof(VMMDevRequestHeader))
    {
        Log(("VMMDev request header size too small! size = %d\n", requestHeader.size));
        return VINF_SUCCESS;
    }

    /* Check the version of the header structure. */
    if (requestHeader.version != VMMDEV_REQUEST_HEADER_VERSION)
    {
        Log(("VMMDev: guest header version (0x%08X) differs from ours (0x%08X)\n", requestHeader.version, VMMDEV_REQUEST_HEADER_VERSION));
        return VINF_SUCCESS;
    }

    Log2(("VMMDev request issued: %d\n", requestHeader.requestType));

    VBOXSTRICTRC rcRet = VINF_SUCCESS;
    /* Check that is doesn't exceed the max packet size. */
    if (requestHeader.size <= VMMDEV_MAX_VMMDEVREQ_SIZE)
    {
        PVMMDEV   pThis   = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
        PVMMDEVCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVMMDEVCC);

        /*
         * We require the GAs to report it's information before we let it have
         * access to all the functions.  The VMMDevReq_ReportGuestInfo request
         * is the one which unlocks the access.  Newer additions will first
         * issue VMMDevReq_ReportGuestInfo2, older ones doesn't know this one.
         * Two exceptions: VMMDevReq_GetHostVersion and VMMDevReq_WriteCoreDump.
         */
        if (   pThis->fu32AdditionsOk
            || requestHeader.requestType == VMMDevReq_ReportGuestInfo2
            || requestHeader.requestType == VMMDevReq_ReportGuestInfo
            || requestHeader.requestType == VMMDevReq_WriteCoreDump
            || requestHeader.requestType == VMMDevReq_GetHostVersion
           )
        {
            /*
             * The request looks fine.  Copy it into a buffer.
             *
             * The buffer is only used while on this thread, and this thread is one
             * of the EMTs, so we keep a 4KB buffer for each EMT around to avoid
             * wasting time with the heap.  Larger allocations goes to the heap, though.
             */
            VMCPUID              iCpu = PDMDevHlpGetCurrentCpuId(pDevIns);
            VMMDevRequestHeader *pRequestHeaderFree = NULL;
            VMMDevRequestHeader *pRequestHeader     = NULL;
            if (   requestHeader.size <= _4K
                && iCpu < RT_ELEMENTS(pThisCC->apReqBufs))
            {
                pRequestHeader = pThisCC->apReqBufs[iCpu];
                if (pRequestHeader)
                { /* likely */ }
                else
                    pThisCC->apReqBufs[iCpu] = pRequestHeader = (VMMDevRequestHeader *)RTMemPageAlloc(_4K);
            }
            else
            {
                Assert(iCpu != NIL_VMCPUID);
                STAM_REL_COUNTER_INC(&pThisCC->StatReqBufAllocs);
                pRequestHeaderFree = pRequestHeader = (VMMDevRequestHeader *)RTMemAlloc(RT_MAX(requestHeader.size, 512));
            }
            if (pRequestHeader)
            {
                memcpy(pRequestHeader, &requestHeader, sizeof(VMMDevRequestHeader));

                /* Try lock the request if it's a HGCM call and not crossing a page boundrary.
                   Saves on PGM interaction. */
                VMMDEVREQLOCK   Lock   = { NULL, { 0, NULL } };
                PVMMDEVREQLOCK  pLock  = NULL;
                size_t          cbLeft = requestHeader.size - sizeof(VMMDevRequestHeader);
                if (cbLeft)
                {
                    if (   (   requestHeader.requestType == VMMDevReq_HGCMCall32
                            || requestHeader.requestType == VMMDevReq_HGCMCall64)
                        && ((GCPhysReqHdr + requestHeader.size) >> VMMDEV_PAGE_SHIFT) == (GCPhysReqHdr >> VMMDEV_PAGE_SHIFT)
                        && RT_SUCCESS(PDMDevHlpPhysGCPhys2CCPtr(pDevIns, GCPhysReqHdr, 0 /*fFlags*/, &Lock.pvReq, &Lock.Lock)) )
                    {
                        memcpy((uint8_t *)pRequestHeader + sizeof(VMMDevRequestHeader),
                               (uint8_t *)Lock.pvReq     + sizeof(VMMDevRequestHeader), cbLeft);
                        pLock = &Lock;
                    }
                    else
                        PDMDevHlpPhysRead(pDevIns,
                                          GCPhysReqHdr              + sizeof(VMMDevRequestHeader),
                                          (uint8_t *)pRequestHeader + sizeof(VMMDevRequestHeader),
                                          cbLeft);
                }

                /*
                 * Feed buffered request thru the dispatcher.
                 */
                uint32_t fPostOptimize = 0;
                int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
                PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);

                rcRet = vmmdevReqDispatcher(pDevIns, pThis, pThisCC, pRequestHeader, GCPhysReqHdr, tsArrival, &fPostOptimize, &pLock);

                PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);

                /*
                 * Write the result back to guest memory (unless it is a locked HGCM call).
                 */
                if (!(fPostOptimize & VMMDEVREQDISP_POST_F_NO_WRITE_OUT))
                {
                    if (pLock)
                        memcpy(pLock->pvReq, pRequestHeader, pRequestHeader->size);
                    else
                        PDMDevHlpPhysWrite(pDevIns, GCPhysReqHdr, pRequestHeader, pRequestHeader->size);
                }

                if (!pRequestHeaderFree)
                { /* likely */ }
                else
                    RTMemFreeZ(pRequestHeaderFree, RT_MAX(requestHeader.size, 512));
                return rcRet;
            }

            Log(("VMMDev: RTMemAlloc failed!\n"));
            requestHeader.rc = VERR_NO_MEMORY;
        }
        else
        {
            LogRelMax(10, ("VMMDev: Guest has not yet reported to us -- refusing operation of request #%d\n",
                           requestHeader.requestType));
            requestHeader.rc = VERR_NOT_SUPPORTED;
        }
    }
    else
    {
        LogRelMax(50, ("VMMDev: Request packet too big (%x), refusing operation\n", requestHeader.size));
        requestHeader.rc = VERR_NOT_SUPPORTED;
    }

    /*
     * Write the result back to guest memory.
     */
    PDMDevHlpPhysWrite(pDevIns, GCPhysReqHdr, &requestHeader, sizeof(requestHeader));

    return rcRet;
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT,
 * Port I/O write andler for the generic request interface.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
vmmdevPioRequestHandler(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(offPort, cb, pvUser);

    return vmmdevRequestHandler(pDevIns, u32);
}

#endif /* IN_RING3 */


/**
 * Common worker for hanlding the fast interrupt acknowledge path from both
 * PIO and MMIO access handlers.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pu32            Where to store the host event flags.
 * @param   rcToR3          The status code to return when locking failed in
 *                          non ring-3/userspace environments (R0 or RC).
 */
static VBOXSTRICTRC vmmdevFastReqIrqAck(PPDMDEVINS pDevIns, uint32_t *pu32, int rcToR3)
{
    PVMMDEV   pThis   = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    PVMMDEVCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVMMDEVCC);
    Assert(PDMDEVINS_2_DATA(pDevIns, PVMMDEV) == pThis);

#ifdef IN_RING3
    RT_NOREF(rcToR3);
#endif

    /* The VMMDev memory mapping might've failed, go to ring-3 in that case. */
    VBOXSTRICTRC rcStrict;
#ifndef IN_RING3
    if (pThisCC->CTX_SUFF(pVMMDevRAM) != NULL)
#endif
    {
        /* Enter critical section and check that the additions has been properly
           initialized and that we're not in legacy v1.3 device mode. */
        rcStrict = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, rcToR3);
        if (rcStrict == VINF_SUCCESS)
        {
            if (   pThis->fu32AdditionsOk
                && !VMMDEV_INTERFACE_VERSION_IS_1_03(pThis))
            {
                /*
                 * Do the job.
                 *
                 * Note! This code is duplicated in vmmdevReqHandler_AcknowledgeEvents.
                 */
                STAM_REL_COUNTER_INC(&pThis->CTX_SUFF_Z(StatFastIrqAck));

                if (pThis->fNewGuestFilterMaskValid)
                {
                    pThis->fNewGuestFilterMaskValid = false;
                    pThis->fGuestFilterMask = pThis->fNewGuestFilterMask;
                }

                *pu32 = pThis->fHostEventFlags & pThis->fGuestFilterMask;

                pThis->fHostEventFlags &= ~pThis->fGuestFilterMask;
                pThisCC->CTX_SUFF(pVMMDevRAM)->V.V1_04.fHaveEvents = false;

                PDMDevHlpPCISetIrqNoWait(pDevIns, 0, 0);
            }
            else
            {
                Log(("vmmdevFastRequestIrqAck: fu32AdditionsOk=%d interfaceVersion=%#x\n", pThis->fu32AdditionsOk,
                     pThis->guestInfo.interfaceVersion));
                *pu32 = UINT32_MAX;
            }

            PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
        }
    }
#ifndef IN_RING3
    else
        rcStrict = rcToR3;
#endif
    return rcStrict;
}


/**
 * @callback_method_impl{FNIOMIOPORTOUT, Port I/O write handler for requests
 * that can be handled w/o going to ring-3.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
vmmdevPioFastRequestHandler(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
#ifndef IN_RING3
# if 0 /* This functionality is offered through reading the port (vmmdevPioFastRequestIrqAck). Leaving it here for later. */
    PVMMDEV pThis = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    RT_NOREF(pvUser, Port, cb);

    /*
     * We only process a limited set of requests here, reflecting the rest down
     * to ring-3.  So, try read the whole request into a stack buffer and check
     * if we can handle it.
     */
    union
    {
        VMMDevRequestHeader Hdr;
        VMMDevEvents        Ack;
    } uReq;
    RT_ZERO(uReq);

    VBOXSTRICTRC rcStrict;
    if (pThis->fu32AdditionsOk)
    {
        /* Read it into memory. */
        uint32_t cbToRead = sizeof(uReq); /* (Adjust to stay within a page if we support more than ack requests.) */
        rcStrict = PDMDevHlpPhysRead(pDevIns, u32, &uReq, cbToRead);
        if (rcStrict == VINF_SUCCESS)
        {
            /*
             * Validate the request and check that we want to handle it here.
             */
            if (   uReq.Hdr.size        >= sizeof(uReq.Hdr)
                && uReq.Hdr.version     == VMMDEV_REQUEST_HEADER_VERSION
                && (   uReq.Hdr.requestType == VMMDevReq_AcknowledgeEvents
                    && uReq.Hdr.size        == sizeof(uReq.Ack)
                    && cbToRead             == sizeof(uReq.Ack)
                    && pThisCC->CTX_SUFF(pVMMDevRAM) != NULL)
               )
            {
                RT_UNTRUSTED_VALIDATED_FENCE();

                /*
                 * Try grab the critical section.
                 */
                int rc2 = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VINF_IOM_R3_IOPORT_WRITE);
                if (rc2 == VINF_SUCCESS)
                {
                    /*
                     * Handle the request and write back the result to the guest.
                     */
                    uReq.Hdr.rc = vmmdevReqHandler_AcknowledgeEvents(pThis, &uReq.Hdr);

                    rcStrict = PDMDevHlpPhysWrite(pDevIns, u32, &uReq, uReq.Hdr.size);
                    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
                    if (rcStrict == VINF_SUCCESS)
                    { /* likely */ }
                    else
                        Log(("vmmdevFastRequestHandler: PDMDevHlpPhysWrite(%#RX32+rc,4) -> %Rrc (%RTbool)\n",
                             u32, VBOXSTRICTRC_VAL(rcStrict), PGM_PHYS_RW_IS_SUCCESS(rcStrict) ));
                }
                else
                {
                    Log(("vmmdevFastRequestHandler: PDMDevHlpPDMCritSectEnter -> %Rrc\n", rc2));
                    rcStrict = rc2;
                }
            }
            else
            {
                Log(("vmmdevFastRequestHandler: size=%#x version=%#x requestType=%d (pVMMDevRAM=%p) -> R3\n",
                     uReq.Hdr.size, uReq.Hdr.version, uReq.Hdr.requestType, pThisCC->CTX_SUFF(pVMMDevRAM) ));
                rcStrict = VINF_IOM_R3_IOPORT_WRITE;
            }
        }
        else
            Log(("vmmdevFastRequestHandler: PDMDevHlpPhysRead(%#RX32,%#RX32) -> %Rrc\n", u32, cbToRead, VBOXSTRICTRC_VAL(rcStrict)));
    }
    else
    {
        Log(("vmmdevFastRequestHandler: additions nok-okay\n"));
        rcStrict = VINF_IOM_R3_IOPORT_WRITE;
    }

    return VBOXSTRICTRC_VAL(rcStrict);
# else
    RT_NOREF(pDevIns, pvUser, offPort, u32, cb);
    return VINF_IOM_R3_IOPORT_WRITE;
# endif

#else  /* IN_RING3 */
    return vmmdevPioRequestHandler(pDevIns, pvUser, offPort, u32, cb);
#endif /* IN_RING3 */
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWIN,
 * Port I/O read handler for IRQ acknowledging and getting pending events (same
 * as VMMDevReq_AcknowledgeEvents - just faster).}
 */
static DECLCALLBACK(VBOXSTRICTRC)
vmmdevPioFastRequestIrqAck(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(pvUser, offPort);

    /* Only 32-bit accesses. */
    ASSERT_GUEST_MSG_RETURN(cb == sizeof(uint32_t), ("cb=%d\n", cb), VERR_IOM_IOPORT_UNUSED);

    return vmmdevFastReqIrqAck(pDevIns, pu32, VINF_IOM_R3_IOPORT_READ);
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD, Read a MMIO register.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vmmdevMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    const uint32_t offReg = (uint32_t)off;
    RT_NOREF(pvUser);

    /* Only 32-bit accesses. */
    ASSERT_GUEST_MSG_RETURN(cb == sizeof(uint32_t), ("cb=%d\n", cb), VINF_IOM_MMIO_UNUSED_FF);

    Log2(("vmmdevMmioRead %RGp (offset %04X) size=%u\n", off, offReg, cb));

    VBOXSTRICTRC rcStrict;
    switch (offReg)
    {
        case VMMDEV_MMIO_OFF_REQUEST_FAST:
            rcStrict = vmmdevFastReqIrqAck(pDevIns, (uint32_t *)pv, VINF_IOM_R3_MMIO_READ);
            break;
        case VMMDEV_MMIO_OFF_REQUEST:
        default:
            Log(("VMMDev: Trying to read unimplemented register at offset %04X!\n", offReg));
            rcStrict = VINF_IOM_MMIO_UNUSED_FF;
            break;
    }

    return rcStrict;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE, Write to a MMIO register.}
 */
static DECLCALLBACK(VBOXSTRICTRC) vmmdevMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    const uint32_t offReg = (uint32_t)off;
    RT_NOREF(pvUser);
#ifndef IN_RING3
    RT_NOREF(pDevIns);
#endif

    /* Only 32-bit and 64-bit accesses. */
    ASSERT_GUEST_MSG_RETURN(cb == sizeof(uint32_t) || cb == sizeof(uint64_t),
                            ("cb=%u\n", cb), VINF_IOM_MMIO_UNUSED_FF);

    uint64_t u64Val = 0; /* shut up MSC */
    if (cb == sizeof(uint64_t))
        u64Val = *(uint64_t *)pv;
    else if (cb == sizeof(uint32_t))
        u64Val = *(uint32_t *)pv;

    Log2(("vmmdevMmioWrite %RGp (offset %04X) %#RX64 size=%u\n", off, offReg, u64Val, cb));

    VBOXSTRICTRC rcStrict;
    switch (offReg)
    {
        case VMMDEV_MMIO_OFF_REQUEST:
#ifndef IN_RING3
            rcStrict = VINF_IOM_R3_MMIO_WRITE;
#else
            rcStrict = vmmdevRequestHandler(pDevIns, u64Val);
#endif
            break;
        case VMMDEV_MMIO_OFF_REQUEST_FAST:
#ifndef IN_RING3
            rcStrict = VINF_IOM_R3_MMIO_WRITE;
#else
            rcStrict = vmmdevRequestHandler(pDevIns, u64Val);
#endif
            break;
        default:
            /* Ignore writes to unimplemented or read-only registers. */
            Log(("VMMDev: Trying to write unimplemented or R/O register at offset %04X!\n", offReg));
            rcStrict = VINF_SUCCESS;
            break;
    }

    return rcStrict;
}



#ifdef IN_RING3

/* -=-=-=-=-=- PCI Device -=-=-=-=-=- */

/**
 * @callback_method_impl{FNPCIIOREGIONMAP,I/O Port Region}
 */
static DECLCALLBACK(int) vmmdevIOPortRegionMap(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                               RTGCPHYS GCPhysAddress, RTGCPHYS cb, PCIADDRESSSPACE enmType)
{
    PVMMDEV pThis = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    LogFlow(("vmmdevIOPortRegionMap: iRegion=%d GCPhysAddress=%RGp cb=%RGp enmType=%d\n", iRegion, GCPhysAddress, cb, enmType));
    RT_NOREF(pPciDev, iRegion, cb, enmType);

    Assert(pPciDev == pDevIns->apPciDevs[0]);
    Assert(enmType == PCI_ADDRESS_SPACE_IO);
    Assert(iRegion == 0);

    int rc;
    if (GCPhysAddress != NIL_RTGCPHYS)
    {
        AssertMsg(RT_ALIGN(GCPhysAddress, 8) == GCPhysAddress, ("Expected 8 byte alignment. GCPhysAddress=%#RGp\n", GCPhysAddress));

        rc = PDMDevHlpIoPortMap(pDevIns, pThis->hIoPortReq, (RTIOPORT)GCPhysAddress + VMMDEV_PORT_OFF_REQUEST);
        AssertLogRelRCReturn(rc, rc);

        rc = PDMDevHlpIoPortMap(pDevIns, pThis->hIoPortFast, (RTIOPORT)GCPhysAddress + VMMDEV_PORT_OFF_REQUEST_FAST);
        AssertLogRelRCReturn(rc, rc);
    }
    else
    {
        rc = PDMDevHlpIoPortUnmap(pDevIns, pThis->hIoPortReq);
        AssertLogRelRCReturn(rc, rc);

        rc = PDMDevHlpIoPortUnmap(pDevIns, pThis->hIoPortFast);
        AssertLogRelRCReturn(rc, rc);
    }
    return rc;
}


/**
 * @callback_method_impl{FNPCIIOREGIONMAP,VMMDev heap (MMIO2)}
 */
static DECLCALLBACK(int) vmmdevMmio2HeapRegionMap(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                                  RTGCPHYS GCPhysAddress, RTGCPHYS cb, PCIADDRESSSPACE enmType)
{
    PVMMDEVCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVMMDEVCC);
    LogFlow(("vmmdevR3IORAMRegionMap: iRegion=%d GCPhysAddress=%RGp cb=%RGp enmType=%d\n", iRegion, GCPhysAddress, cb, enmType));
    RT_NOREF(cb, pPciDev);

    Assert(pPciDev == pDevIns->apPciDevs[0]);
    AssertReturn(iRegion == 2, VERR_INTERNAL_ERROR_2);
    AssertReturn(enmType == PCI_ADDRESS_SPACE_MEM_PREFETCH, VERR_INTERNAL_ERROR_3);
    Assert(pThisCC->pVMMDevHeapR3 != NULL);

    int rc;
    if (GCPhysAddress != NIL_RTGCPHYS)
    {
        rc = PDMDevHlpRegisterVMMDevHeap(pDevIns, GCPhysAddress, pThisCC->pVMMDevHeapR3, VMMDEV_HEAP_SIZE);
        AssertRC(rc);
    }
    else
    {
        rc = PDMDevHlpRegisterVMMDevHeap(pDevIns, NIL_RTGCPHYS, pThisCC->pVMMDevHeapR3, VMMDEV_HEAP_SIZE);
        AssertRCStmt(rc, rc = VINF_SUCCESS);
    }

    return rc;
}


/* -=-=-=-=-=- Backdoor Logging and Time Sync. -=-=-=-=-=- */

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, Backdoor Logging.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
vmmdevBackdoorLog(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PVMMDEV pThis = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    RT_NOREF(pvUser, offPort);
    Assert(offPort == 0);

    if (!pThis->fBackdoorLogDisabled && cb == 1)
    {

        /* The raw version. */
        switch (u32)
        {
            case '\r': LogIt(RTLOGGRPFLAGS_LEVEL_2, LOG_GROUP_DEV_VMM_BACKDOOR, ("vmmdev: <return>\n")); break;
            case '\n': LogIt(RTLOGGRPFLAGS_LEVEL_2, LOG_GROUP_DEV_VMM_BACKDOOR, ("vmmdev: <newline>\n")); break;
            case '\t': LogIt(RTLOGGRPFLAGS_LEVEL_2, LOG_GROUP_DEV_VMM_BACKDOOR, ("vmmdev: <tab>\n")); break;
            default:   LogIt(RTLOGGRPFLAGS_LEVEL_2, LOG_GROUP_DEV_VMM_BACKDOOR, ("vmmdev: %c (%02x)\n", u32, u32)); break;
        }

        /* The readable, buffered version. */
        uint32_t offMsg = RT_MIN(pThis->offMsg, sizeof(pThis->szMsg) - 1);
        if (u32 == '\n' || u32 == '\r')
        {
            pThis->szMsg[offMsg] = '\0';
            if (offMsg)
                LogRelIt(RTLOGGRPFLAGS_LEVEL_1, LOG_GROUP_DEV_VMM_BACKDOOR, ("VMMDev: Guest Log: %.*s\n", offMsg, pThis->szMsg));
            pThis->offMsg = 0;
        }
        else
        {
            if (offMsg >= sizeof(pThis->szMsg) - 1)
            {
                pThis->szMsg[sizeof(pThis->szMsg) - 1] = '\0';
                LogRelIt(RTLOGGRPFLAGS_LEVEL_1, LOG_GROUP_DEV_VMM_BACKDOOR,
                         ("VMMDev: Guest Log: %.*s\n", sizeof(pThis->szMsg) - 1, pThis->szMsg));
                offMsg = 0;
            }
            pThis->szMsg[offMsg++] = (char )u32;
            pThis->szMsg[offMsg]   = '\0';
            pThis->offMsg = offMsg;
        }
    }
    return VINF_SUCCESS;
}

#ifdef VMMDEV_WITH_ALT_TIMESYNC

/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT, Alternative time synchronization.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
vmmdevAltTimeSyncWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    RT_NOREF(pvUser, offPort);
    PVMMDEV pThis = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    if (cb == 4)
    {
        /* Selects high (0) or low (1) DWORD. The high has to be read first. */
        switch (u32)
        {
            case 0:
                pThis->fTimesyncBackdoorLo = false;
                break;
            case 1:
                pThis->fTimesyncBackdoorLo = true;
                break;
            default:
                Log(("vmmdevAltTimeSyncWrite: Invalid access cb=%#x u32=%#x\n", cb, u32));
                break;
        }
    }
    else
        Log(("vmmdevAltTimeSyncWrite: Invalid access cb=%#x u32=%#x\n", cb, u32));
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNIOMIOPORTOUT, Alternative time synchronization.}
 */
static DECLCALLBACK(VBOXSTRICTRC)
vmmdevAltTimeSyncRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    RT_NOREF(pvUser, offPort);
    PVMMDEV      pThis = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    VBOXSTRICTRC rc;
    if (cb == 4)
    {
        if (pThis->fTimesyncBackdoorLo)
            *pu32 = (uint32_t)pThis->msLatchedHostTime;
        else
        {
            /* Reading the high dword gets and saves the current time. */
            RTTIMESPEC Now;
            pThis->msLatchedHostTime = RTTimeSpecGetMilli(PDMDevHlpTMUtcNow(pDevIns, &Now));
            *pu32 = (uint32_t)(pThis->msLatchedHostTime >> 32);
        }
        rc = VINF_SUCCESS;
    }
    else
    {
        Log(("vmmdevAltTimeSyncRead: Invalid access cb=%#x\n", cb));
        rc = VERR_IOM_IOPORT_UNUSED;
    }
    return rc;
}

#endif /* VMMDEV_WITH_ALT_TIMESYNC */


/* -=-=-=-=-=- IBase -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) vmmdevPortQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PVMMDEVCC pThisCC = RT_FROM_MEMBER(pInterface, VMMDEVCC, IBase);

    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIVMMDEVPORT, &pThisCC->IPort);
#ifdef VBOX_WITH_HGCM
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIHGCMPORT, &pThisCC->IHGCMPort);
#endif
    /* Currently only for shared folders. */
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMILEDPORTS, &pThisCC->SharedFolders.ILeds);
    return NULL;
}


/* -=-=-=-=-=- ILeds -=-=-=-=-=- */

/**
 * Gets the pointer to the status LED of a unit.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   iLUN            The unit which status LED we desire.
 * @param   ppLed           Where to store the LED pointer.
 */
static DECLCALLBACK(int) vmmdevQueryStatusLed(PPDMILEDPORTS pInterface, unsigned iLUN, PPDMLED *ppLed)
{
    PVMMDEVCC pThisCC = RT_FROM_MEMBER(pInterface, VMMDEVCC, SharedFolders.ILeds);
    if (iLUN == 0) /* LUN 0 is shared folders */
    {
        *ppLed = &pThisCC->SharedFolders.Led;
        return VINF_SUCCESS;
    }
    return VERR_PDM_LUN_NOT_FOUND;
}


/* -=-=-=-=-=- PDMIVMMDEVPORT (VMMDEV::IPort) -=-=-=-=-=- */

/**
 * @interface_method_impl{PDMIVMMDEVPORT,pfnQueryAbsoluteMouse}
 */
static DECLCALLBACK(int) vmmdevIPort_QueryAbsoluteMouse(PPDMIVMMDEVPORT pInterface, int32_t *pxAbs, int32_t *pyAbs)
{
    PVMMDEVCC pThisCC = RT_FROM_MEMBER(pInterface, VMMDEVCC, IPort);
    PVMMDEV   pThis   = PDMDEVINS_2_DATA(pThisCC->pDevIns, PVMMDEV);

    /** @todo at the first sign of trouble in this area, just enter the critsect.
     * As indicated by the comment below, the atomic reads serves no real purpose
     * here since we can assume cache coherency protocoles and int32_t alignment
     * rules making sure we won't see a halfwritten value. */
    if (pxAbs)
        *pxAbs = ASMAtomicReadS32(&pThis->xMouseAbs); /* why the atomic read? */
    if (pyAbs)
        *pyAbs = ASMAtomicReadS32(&pThis->yMouseAbs);

    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIVMMDEVPORT,pfnSetAbsoluteMouse}
 */
static DECLCALLBACK(int) vmmdevIPort_SetAbsoluteMouse(PPDMIVMMDEVPORT pInterface, int32_t xAbs, int32_t yAbs,
                                                      int32_t dz, int32_t dw, uint32_t fButtons)
{
    PVMMDEVCC  pThisCC = RT_FROM_MEMBER(pInterface, VMMDEVCC, IPort);
    PPDMDEVINS pDevIns = pThisCC->pDevIns;
    PVMMDEV    pThis   = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    int const  rcLock  = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    AssertRCReturn(rcLock, rcLock);

    if (   pThis->xMouseAbs != xAbs
        || pThis->yMouseAbs != yAbs
        || dz
        || dw
        || pThis->fMouseButtons != fButtons)
    {
        Log2(("vmmdevIPort_SetAbsoluteMouse : settings absolute position to x = %d, y = %d, z = %d, w = %d, fButtons = 0x%x\n",
              xAbs, yAbs, dz, dw, fButtons));

        pThis->xMouseAbs = xAbs;
        pThis->yMouseAbs = yAbs;
        pThis->dzMouse = dz;
        pThis->dwMouse = dw;
        pThis->fMouseButtons = fButtons;

        VMMDevNotifyGuest(pDevIns, pThis, pThisCC, VMMDEV_EVENT_MOUSE_POSITION_CHANGED);
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIVMMDEVPORT,pfnQueryMouseCapabilities}
 */
static DECLCALLBACK(int) vmmdevIPort_QueryMouseCapabilities(PPDMIVMMDEVPORT pInterface, uint32_t *pfCapabilities)
{
    PVMMDEVCC pThisCC = RT_FROM_MEMBER(pInterface, VMMDEVCC, IPort);
    PVMMDEV   pThis   = PDMDEVINS_2_DATA(pThisCC->pDevIns, PVMMDEV);
    AssertPtrReturn(pfCapabilities, VERR_INVALID_PARAMETER);

    *pfCapabilities = pThis->fMouseCapabilities;
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIVMMDEVPORT,pfnUpdateMouseCapabilities}
 */
static DECLCALLBACK(int)
vmmdevIPort_UpdateMouseCapabilities(PPDMIVMMDEVPORT pInterface, uint32_t fCapsAdded, uint32_t fCapsRemoved)
{
    PVMMDEVCC  pThisCC = RT_FROM_MEMBER(pInterface, VMMDEVCC, IPort);
    PPDMDEVINS pDevIns = pThisCC->pDevIns;
    PVMMDEV    pThis   = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    int const  rcLock  = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    AssertRCReturn(rcLock, rcLock);

    uint32_t fOldCaps = pThis->fMouseCapabilities;
    pThis->fMouseCapabilities &= ~(fCapsRemoved & VMMDEV_MOUSE_HOST_MASK);
    pThis->fMouseCapabilities |= (fCapsAdded & VMMDEV_MOUSE_HOST_MASK)
                              |  VMMDEV_MOUSE_HOST_RECHECKS_NEEDS_HOST_CURSOR
                              |  VMMDEV_MOUSE_HOST_SUPPORTS_FULL_STATE_PROTOCOL;
    bool fNotify = fOldCaps != pThis->fMouseCapabilities;

    LogRelFlow(("VMMDev: vmmdevIPort_UpdateMouseCapabilities: fCapsAdded=0x%x, fCapsRemoved=0x%x, fNotify=%RTbool\n", fCapsAdded,
                fCapsRemoved, fNotify));

    if (fNotify)
        VMMDevNotifyGuest(pDevIns, pThis, pThisCC, VMMDEV_EVENT_MOUSE_CAPABILITIES_CHANGED);

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return VINF_SUCCESS;
}

static bool vmmdevIsMonitorDefEqual(VMMDevDisplayDef const *pNew, VMMDevDisplayDef const *pOld)
{
    bool     fEqual = pNew->idDisplay == pOld->idDisplay;

    fEqual = fEqual && (   !RT_BOOL(pNew->fDisplayFlags & VMMDEV_DISPLAY_ORIGIN)    /* No change. */
                        || (   RT_BOOL(pOld->fDisplayFlags & VMMDEV_DISPLAY_ORIGIN) /* Old value exists and */
                            && pNew->xOrigin == pOld->xOrigin                       /* the old is equal to the new. */
                            && pNew->yOrigin == pOld->yOrigin));

    fEqual = fEqual && (   !RT_BOOL(pNew->fDisplayFlags & VMMDEV_DISPLAY_CX)
                        || (   RT_BOOL(pOld->fDisplayFlags & VMMDEV_DISPLAY_CX)
                            && pNew->cx == pOld->cx));

    fEqual = fEqual && (   !RT_BOOL(pNew->fDisplayFlags & VMMDEV_DISPLAY_CY)
                        || (   RT_BOOL(pOld->fDisplayFlags & VMMDEV_DISPLAY_CY)
                            && pNew->cy == pOld->cy));

    fEqual = fEqual && (   !RT_BOOL(pNew->fDisplayFlags & VMMDEV_DISPLAY_BPP)
                        || (   RT_BOOL(pOld->fDisplayFlags & VMMDEV_DISPLAY_BPP)
                            && pNew->cBitsPerPixel == pOld->cBitsPerPixel));

    fEqual = fEqual && (   RT_BOOL(pNew->fDisplayFlags & VMMDEV_DISPLAY_DISABLED)
                        == RT_BOOL(pOld->fDisplayFlags & VMMDEV_DISPLAY_DISABLED));

    fEqual = fEqual && (   RT_BOOL(pNew->fDisplayFlags & VMMDEV_DISPLAY_PRIMARY)
                        == RT_BOOL(pOld->fDisplayFlags & VMMDEV_DISPLAY_PRIMARY));

    return fEqual;
}

/**
 * @interface_method_impl{PDMIVMMDEVPORT,pfnRequestDisplayChange}
 */
static DECLCALLBACK(int)
vmmdevIPort_RequestDisplayChange(PPDMIVMMDEVPORT pInterface, uint32_t cDisplays, VMMDevDisplayDef const *paDisplays, bool fForce, bool fMayNotify)
{
    PVMMDEVCC   pThisCC      = RT_FROM_MEMBER(pInterface, VMMDEVCC, IPort);
    PPDMDEVINS  pDevIns      = pThisCC->pDevIns;
    PVMMDEV     pThis        = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    int         rc           = VINF_SUCCESS;
    bool        fNotifyGuest = false;
    int const   rcLock       = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    AssertRCReturn(rcLock, rcLock);

    uint32_t i;
    for (i = 0; i < cDisplays; ++i)
    {
        VMMDevDisplayDef const *p = &paDisplays[i];

        /* Either one display definition is provided or the display id must be equal to the array index. */
        AssertBreakStmt(cDisplays == 1 || p->idDisplay == i, rc = VERR_INVALID_PARAMETER);
        AssertBreakStmt(p->idDisplay < RT_ELEMENTS(pThis->displayChangeData.aRequests), rc = VERR_INVALID_PARAMETER);

        DISPLAYCHANGEREQUEST *pRequest = &pThis->displayChangeData.aRequests[p->idDisplay];

        VMMDevDisplayDef const *pLastRead = &pRequest->lastReadDisplayChangeRequest;

        /* Verify that the new resolution is different and that guest does not yet know about it. */
        bool const fDifferentResolution = fForce || !vmmdevIsMonitorDefEqual(p, pLastRead);

        LogFunc(("same=%d. New: %dx%d, cBits=%d, id=%d. Old: %dx%d, cBits=%d, id=%d. @%d,%d, Enabled=%d, ChangeOrigin=%d\n",
                 !fDifferentResolution, p->cx, p->cy, p->cBitsPerPixel, p->idDisplay,
                 pLastRead->cx, pLastRead->cy, pLastRead->cBitsPerPixel, pLastRead->idDisplay,
                 p->xOrigin, p->yOrigin,
                 !RT_BOOL(p->fDisplayFlags & VMMDEV_DISPLAY_DISABLED),
                 RT_BOOL(p->fDisplayFlags & VMMDEV_DISPLAY_ORIGIN)));

        /* We could validate the information here but hey, the guest can do that as well! */
        pRequest->displayChangeRequest = *p;
        pRequest->fPending = fDifferentResolution && fMayNotify;

        fNotifyGuest = fNotifyGuest || fDifferentResolution;
    }

    if (RT_SUCCESS(rc) && fMayNotify)
    {
        if (fNotifyGuest)
        {
            for (i = 0; i < RT_ELEMENTS(pThis->displayChangeData.aRequests); ++i)
            {
                DISPLAYCHANGEREQUEST *pRequest = &pThis->displayChangeData.aRequests[i];
                if (pRequest->fPending)
                {
                    VMMDevDisplayDef const *p = &pRequest->displayChangeRequest;
                    LogRel(("VMMDev: SetVideoModeHint: Got a video mode hint (%dx%dx%d)@(%dx%d),(%d;%d) at %d\n",
                            p->cx, p->cy, p->cBitsPerPixel, p->xOrigin, p->yOrigin,
                            !RT_BOOL(p->fDisplayFlags & VMMDEV_DISPLAY_DISABLED),
                            RT_BOOL(p->fDisplayFlags & VMMDEV_DISPLAY_ORIGIN), i));
                }
            }

            /* IRQ so the guest knows what's going on */
            VMMDevNotifyGuest(pDevIns, pThis, pThisCC, VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST);
        }
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return rc;
}

/**
 * @interface_method_impl{PDMIVMMDEVPORT,pfnRequestSeamlessChange}
 */
static DECLCALLBACK(int) vmmdevIPort_RequestSeamlessChange(PPDMIVMMDEVPORT pInterface, bool fEnabled)
{
    PVMMDEVCC  pThisCC = RT_FROM_MEMBER(pInterface, VMMDEVCC, IPort);
    PPDMDEVINS pDevIns = pThisCC->pDevIns;
    PVMMDEV    pThis   = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    int const  rcLock  = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    AssertRCReturn(rcLock, rcLock);

    /* Verify that the new resolution is different and that guest does not yet know about it. */
    bool fSameMode = (pThis->fLastSeamlessEnabled == fEnabled);

    Log(("vmmdevIPort_RequestSeamlessChange: same=%d. new=%d\n", fSameMode, fEnabled));

    if (!fSameMode)
    {
        /* we could validate the information here but hey, the guest can do that as well! */
        pThis->fSeamlessEnabled = fEnabled;

        /* IRQ so the guest knows what's going on */
        VMMDevNotifyGuest(pDevIns, pThis, pThisCC, VMMDEV_EVENT_SEAMLESS_MODE_CHANGE_REQUEST);
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIVMMDEVPORT,pfnSetMemoryBalloon}
 */
static DECLCALLBACK(int) vmmdevIPort_SetMemoryBalloon(PPDMIVMMDEVPORT pInterface, uint32_t cMbBalloon)
{
    PVMMDEVCC  pThisCC = RT_FROM_MEMBER(pInterface, VMMDEVCC, IPort);
    PPDMDEVINS pDevIns = pThisCC->pDevIns;
    PVMMDEV    pThis   = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    int const  rcLock  = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    AssertRCReturn(rcLock, rcLock);

    /* Verify that the new resolution is different and that guest does not yet know about it. */
    Log(("vmmdevIPort_SetMemoryBalloon: old=%u new=%u\n", pThis->cMbMemoryBalloonLast, cMbBalloon));
    if (pThis->cMbMemoryBalloonLast != cMbBalloon)
    {
        /* we could validate the information here but hey, the guest can do that as well! */
        pThis->cMbMemoryBalloon = cMbBalloon;

        /* IRQ so the guest knows what's going on */
        VMMDevNotifyGuest(pDevIns, pThis, pThisCC, VMMDEV_EVENT_BALLOON_CHANGE_REQUEST);
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIVMMDEVPORT,pfnVRDPChange}
 */
static DECLCALLBACK(int) vmmdevIPort_VRDPChange(PPDMIVMMDEVPORT pInterface, bool fVRDPEnabled, uint32_t uVRDPExperienceLevel)
{
    PVMMDEVCC  pThisCC = RT_FROM_MEMBER(pInterface, VMMDEVCC, IPort);
    PPDMDEVINS pDevIns = pThisCC->pDevIns;
    PVMMDEV    pThis   = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    int const  rcLock  = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    AssertRCReturn(rcLock, rcLock);

    bool fSame = (pThis->fVRDPEnabled == fVRDPEnabled);

    Log(("vmmdevIPort_VRDPChange: old=%d. new=%d\n", pThis->fVRDPEnabled, fVRDPEnabled));

    if (!fSame)
    {
        pThis->fVRDPEnabled = fVRDPEnabled;
        pThis->uVRDPExperienceLevel = uVRDPExperienceLevel;

        VMMDevNotifyGuest(pDevIns, pThis, pThisCC, VMMDEV_EVENT_VRDP);
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIVMMDEVPORT,pfnSetStatisticsInterval}
 */
static DECLCALLBACK(int) vmmdevIPort_SetStatisticsInterval(PPDMIVMMDEVPORT pInterface, uint32_t cSecsStatInterval)
{
    PVMMDEVCC  pThisCC = RT_FROM_MEMBER(pInterface, VMMDEVCC, IPort);
    PPDMDEVINS pDevIns = pThisCC->pDevIns;
    PVMMDEV    pThis   = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    int const  rcLock  = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    AssertRCReturn(rcLock, rcLock);

    /* Verify that the new resolution is different and that guest does not yet know about it. */
    bool fSame = (pThis->cSecsLastStatInterval == cSecsStatInterval);

    Log(("vmmdevIPort_SetStatisticsInterval: old=%d. new=%d\n", pThis->cSecsLastStatInterval, cSecsStatInterval));

    if (!fSame)
    {
        /* we could validate the information here but hey, the guest can do that as well! */
        pThis->cSecsStatInterval = cSecsStatInterval;

        /* IRQ so the guest knows what's going on */
        VMMDevNotifyGuest(pDevIns, pThis, pThisCC, VMMDEV_EVENT_STATISTICS_INTERVAL_CHANGE_REQUEST);
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIVMMDEVPORT,pfnSetCredentials}
 */
static DECLCALLBACK(int) vmmdevIPort_SetCredentials(PPDMIVMMDEVPORT pInterface, const char *pszUsername,
                                                    const char *pszPassword, const char *pszDomain, uint32_t fFlags)
{
    PVMMDEVCC  pThisCC = RT_FROM_MEMBER(pInterface, VMMDEVCC, IPort);
    PPDMDEVINS pDevIns = pThisCC->pDevIns;
    PVMMDEV    pThis   = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);

    AssertReturn(fFlags & (VMMDEV_SETCREDENTIALS_GUESTLOGON | VMMDEV_SETCREDENTIALS_JUDGE), VERR_INVALID_PARAMETER);
    size_t const cchUsername = strlen(pszUsername);
    AssertReturn(cchUsername < VMMDEV_CREDENTIALS_SZ_SIZE, VERR_BUFFER_OVERFLOW);
    size_t const cchPassword = strlen(pszPassword);
    AssertReturn(cchPassword < VMMDEV_CREDENTIALS_SZ_SIZE, VERR_BUFFER_OVERFLOW);
    size_t const cchDomain   = strlen(pszDomain);
    AssertReturn(cchDomain < VMMDEV_CREDENTIALS_SZ_SIZE, VERR_BUFFER_OVERFLOW);

    VMMDEVCREDS *pCredentials = pThisCC->pCredentials;
    AssertPtrReturn(pCredentials, VERR_NOT_SUPPORTED);

    int const rcLock = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    AssertRCReturn(rcLock, rcLock);

    /*
     * Logon mode
     */
    if (fFlags & VMMDEV_SETCREDENTIALS_GUESTLOGON)
    {
        /* memorize the data */
        memcpy(pCredentials->Logon.szUserName, pszUsername, cchUsername);
        pThisCC->pCredentials->Logon.szUserName[cchUsername] = '\0';
        memcpy(pCredentials->Logon.szPassword, pszPassword, cchPassword);
        pCredentials->Logon.szPassword[cchPassword] = '\0';
        memcpy(pCredentials->Logon.szDomain,   pszDomain, cchDomain);
        pCredentials->Logon.szDomain[cchDomain]     = '\0';
        pCredentials->Logon.fAllowInteractiveLogon = !(fFlags & VMMDEV_SETCREDENTIALS_NOLOCALLOGON);
    }
    /*
     * Credentials verification mode?
     */
    else
    {
        /* memorize the data */
        memcpy(pCredentials->Judge.szUserName, pszUsername, cchUsername);
        pCredentials->Judge.szUserName[cchUsername] = '\0';
        memcpy(pCredentials->Judge.szPassword, pszPassword, cchPassword);
        pCredentials->Judge.szPassword[cchPassword] = '\0';
        memcpy(pCredentials->Judge.szDomain,   pszDomain,   cchDomain);
        pCredentials->Judge.szDomain[cchDomain]     = '\0';

        VMMDevNotifyGuest(pDevIns, pThis, pThisCC, VMMDEV_EVENT_JUDGE_CREDENTIALS);
    }

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIVMMDEVPORT,pfnVBVAChange}
 *
 * Notification from the Display.  Especially useful when acceleration is
 * disabled after a video mode change.
 */
static DECLCALLBACK(void) vmmdevIPort_VBVAChange(PPDMIVMMDEVPORT pInterface, bool fEnabled)
{
    PVMMDEVCC pThisCC = RT_FROM_MEMBER(pInterface, VMMDEVCC, IPort);
    PVMMDEV   pThis   = PDMDEVINS_2_DATA(pThisCC->pDevIns, PVMMDEV);
    Log(("vmmdevIPort_VBVAChange: fEnabled = %d\n", fEnabled));

    /* Only used by saved state, which I guess is why we don't bother with locking here. */
    pThis->u32VideoAccelEnabled = fEnabled;
}

/**
 * @interface_method_impl{PDMIVMMDEVPORT,pfnCpuHotUnplug}
 */
static DECLCALLBACK(int) vmmdevIPort_CpuHotUnplug(PPDMIVMMDEVPORT pInterface, uint32_t idCpuCore, uint32_t idCpuPackage)
{
    PVMMDEVCC   pThisCC = RT_FROM_MEMBER(pInterface, VMMDEVCC, IPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PVMMDEV     pThis   = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);

    Log(("vmmdevIPort_CpuHotUnplug: idCpuCore=%u idCpuPackage=%u\n", idCpuCore, idCpuPackage));

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    AssertRCReturn(rc, rc);

    if (pThis->fCpuHotPlugEventsEnabled)
    {
        pThis->enmCpuHotPlugEvent = VMMDevCpuEventType_Unplug;
        pThis->idCpuCore          = idCpuCore;
        pThis->idCpuPackage       = idCpuPackage;
        VMMDevNotifyGuest(pDevIns, pThis, pThisCC, VMMDEV_EVENT_CPU_HOTPLUG);
    }
    else
        rc = VERR_VMMDEV_CPU_HOTPLUG_NOT_MONITORED_BY_GUEST;

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return rc;
}

/**
 * @interface_method_impl{PDMIVMMDEVPORT,pfnCpuHotPlug}
 */
static DECLCALLBACK(int) vmmdevIPort_CpuHotPlug(PPDMIVMMDEVPORT pInterface, uint32_t idCpuCore, uint32_t idCpuPackage)
{
    PVMMDEVCC   pThisCC = RT_FROM_MEMBER(pInterface, VMMDEVCC, IPort);
    PPDMDEVINS  pDevIns = pThisCC->pDevIns;
    PVMMDEV     pThis   = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);

    Log(("vmmdevCpuPlug: idCpuCore=%u idCpuPackage=%u\n", idCpuCore, idCpuPackage));

    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    AssertRCReturn(rc, rc);

    if (pThis->fCpuHotPlugEventsEnabled)
    {
        pThis->enmCpuHotPlugEvent = VMMDevCpuEventType_Plug;
        pThis->idCpuCore          = idCpuCore;
        pThis->idCpuPackage       = idCpuPackage;
        VMMDevNotifyGuest(pDevIns, pThis, pThisCC, VMMDEV_EVENT_CPU_HOTPLUG);
    }
    else
        rc = VERR_VMMDEV_CPU_HOTPLUG_NOT_MONITORED_BY_GUEST;

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return rc;
}


/* -=-=-=-=-=- Saved State -=-=-=-=-=- */

/**
 * @callback_method_impl{FNSSMDEVLIVEEXEC}
 */
static DECLCALLBACK(int) vmmdevLiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    RT_NOREF(uPass);
    PVMMDEV         pThis = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    PCPDMDEVHLPR3   pHlp  = pDevIns->pHlpR3;

    pHlp->pfnSSMPutBool(pSSM, pThis->fGetHostTimeDisabled);
    pHlp->pfnSSMPutBool(pSSM, pThis->fBackdoorLogDisabled);
    pHlp->pfnSSMPutBool(pSSM, pThis->fKeepCredentials);
    pHlp->pfnSSMPutBool(pSSM, pThis->fHeapEnabled);
    pHlp->pfnSSMPutBool(pSSM, pThis->fMmioReq);

    return VINF_SSM_DONT_CALL_AGAIN;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) vmmdevSaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PVMMDEV         pThis   = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    PVMMDEVCC       pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVMMDEVCC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int rc = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    AssertRCReturn(rc, rc);

    vmmdevLiveExec(pDevIns, pSSM, SSM_PASS_FINAL);

    pHlp->pfnSSMPutU32(pSSM, 0 /*was pThis->hypervisorSize, which was always zero*/);
    pHlp->pfnSSMPutU32(pSSM, pThis->fMouseCapabilities);
    pHlp->pfnSSMPutS32(pSSM, pThis->xMouseAbs);
    pHlp->pfnSSMPutS32(pSSM, pThis->yMouseAbs);
    pHlp->pfnSSMPutS32(pSSM, pThis->dzMouse);
    pHlp->pfnSSMPutS32(pSSM, pThis->dwMouse);
    pHlp->pfnSSMPutU32(pSSM, pThis->fMouseButtons);

    pHlp->pfnSSMPutBool(pSSM, pThis->fNewGuestFilterMaskValid);
    pHlp->pfnSSMPutU32(pSSM, pThis->fNewGuestFilterMask);
    pHlp->pfnSSMPutU32(pSSM, pThis->fGuestFilterMask);
    pHlp->pfnSSMPutU32(pSSM, pThis->fHostEventFlags);
    /* The following is not strictly necessary as PGM restores MMIO2, keeping it for historical reasons. */
    pHlp->pfnSSMPutMem(pSSM, &pThisCC->pVMMDevRAMR3->V, sizeof(pThisCC->pVMMDevRAMR3->V));

    pHlp->pfnSSMPutMem(pSSM, &pThis->guestInfo, sizeof(pThis->guestInfo));
    pHlp->pfnSSMPutU32(pSSM, pThis->fu32AdditionsOk);
    pHlp->pfnSSMPutU32(pSSM, pThis->u32VideoAccelEnabled);
    pHlp->pfnSSMPutBool(pSSM, pThis->displayChangeData.fGuestSentChangeEventAck);

    pHlp->pfnSSMPutU32(pSSM, pThis->fGuestCaps);

#ifdef VBOX_WITH_HGCM
    vmmdevR3HgcmSaveState(pThisCC, pSSM);
#endif /* VBOX_WITH_HGCM */

    pHlp->pfnSSMPutU32(pSSM, pThis->fHostCursorRequested);

    pHlp->pfnSSMPutU32(pSSM, pThis->guestInfo2.uFullVersion);
    pHlp->pfnSSMPutU32(pSSM, pThis->guestInfo2.uRevision);
    pHlp->pfnSSMPutU32(pSSM, pThis->guestInfo2.fFeatures);
    pHlp->pfnSSMPutStrZ(pSSM, pThis->guestInfo2.szName);
    pHlp->pfnSSMPutU32(pSSM, pThis->cFacilityStatuses);
    for (uint32_t i = 0; i < pThis->cFacilityStatuses; i++)
    {
        pHlp->pfnSSMPutU32(pSSM, pThis->aFacilityStatuses[i].enmFacility);
        pHlp->pfnSSMPutU32(pSSM, pThis->aFacilityStatuses[i].fFlags);
        pHlp->pfnSSMPutU16(pSSM, (uint16_t)pThis->aFacilityStatuses[i].enmStatus);
        pHlp->pfnSSMPutS64(pSSM, RTTimeSpecGetNano(&pThis->aFacilityStatuses[i].TimeSpecTS));
    }

    /* Heartbeat: */
    pHlp->pfnSSMPutBool(pSSM, pThis->fHeartbeatActive);
    pHlp->pfnSSMPutBool(pSSM, pThis->fFlatlined);
    pHlp->pfnSSMPutU64(pSSM, pThis->nsLastHeartbeatTS);
    PDMDevHlpTimerSave(pDevIns, pThis->hFlatlinedTimer, pSSM);

    pHlp->pfnSSMPutStructEx(pSSM, &pThis->displayChangeData, sizeof(pThis->displayChangeData), 0,
                            g_aSSMDISPLAYCHANGEDATAStateFields, NULL);

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) vmmdevLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PVMMDEV         pThis   = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    PVMMDEVCC       pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVMMDEVCC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int             rc;

    if (   uVersion > VMMDEV_SAVED_STATE_VERSION
        || uVersion < 6)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /* config */
    if (uVersion > VMMDEV_SAVED_STATE_VERSION_VBOX_30)
    {
        bool f;
        rc = pHlp->pfnSSMGetBool(pSSM, &f); AssertRCReturn(rc, rc);
        if (pThis->fGetHostTimeDisabled != f)
            LogRel(("VMMDev: Config mismatch - fGetHostTimeDisabled: config=%RTbool saved=%RTbool\n", pThis->fGetHostTimeDisabled, f));

        rc = pHlp->pfnSSMGetBool(pSSM, &f); AssertRCReturn(rc, rc);
        if (pThis->fBackdoorLogDisabled != f)
            LogRel(("VMMDev: Config mismatch - fBackdoorLogDisabled: config=%RTbool saved=%RTbool\n", pThis->fBackdoorLogDisabled, f));

        rc = pHlp->pfnSSMGetBool(pSSM, &f); AssertRCReturn(rc, rc);
        if (pThis->fKeepCredentials != f)
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - fKeepCredentials: config=%RTbool saved=%RTbool"),
                                           pThis->fKeepCredentials, f);
        rc = pHlp->pfnSSMGetBool(pSSM, &f); AssertRCReturn(rc, rc);
        if (pThis->fHeapEnabled != f)
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - fHeapEnabled: config=%RTbool saved=%RTbool"),
                                           pThis->fHeapEnabled, f);

        f = false;
        if (uVersion >= VMMDEV_SAVED_STATE_VERSION_MMIO_ACCESS)
        {
            rc = pHlp->pfnSSMGetBool(pSSM, &f);
            AssertRCReturn(rc, rc);
        }
        if (pThis->fMmioReq != f)
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch - fMmioReq: config=%RTbool saved=%RTbool"),
                                           pThis->fMmioReq, f);
    }

    if (uPass != SSM_PASS_FINAL)
        return VINF_SUCCESS;

    /* state */
    uint32_t uIgn;
    pHlp->pfnSSMGetU32(pSSM, &uIgn);
    pHlp->pfnSSMGetU32(pSSM, &pThis->fMouseCapabilities);
    pHlp->pfnSSMGetS32(pSSM, &pThis->xMouseAbs);
    pHlp->pfnSSMGetS32(pSSM, &pThis->yMouseAbs);
    if (uVersion >= VMMDEV_SAVED_STATE_VERSION_VMM_MOUSE_EXTENDED_DATA)
    {
        pHlp->pfnSSMGetS32(pSSM, &pThis->dzMouse);
        pHlp->pfnSSMGetS32(pSSM, &pThis->dwMouse);
        pHlp->pfnSSMGetU32(pSSM, &pThis->fMouseButtons);
    }

    pHlp->pfnSSMGetBool(pSSM, &pThis->fNewGuestFilterMaskValid);
    pHlp->pfnSSMGetU32(pSSM, &pThis->fNewGuestFilterMask);
    pHlp->pfnSSMGetU32(pSSM, &pThis->fGuestFilterMask);
    pHlp->pfnSSMGetU32(pSSM, &pThis->fHostEventFlags);

    //pHlp->pfnSSMGetBool(pSSM, &pThis->pVMMDevRAMR3->fHaveEvents);
    // here be dragons (probably)
    pHlp->pfnSSMGetMem(pSSM, &pThisCC->pVMMDevRAMR3->V, sizeof(pThisCC->pVMMDevRAMR3->V));

    pHlp->pfnSSMGetMem(pSSM, &pThis->guestInfo, sizeof(pThis->guestInfo));
    pHlp->pfnSSMGetU32(pSSM, &pThis->fu32AdditionsOk);
    pHlp->pfnSSMGetU32(pSSM, &pThis->u32VideoAccelEnabled);
    if (uVersion > 10)
        pHlp->pfnSSMGetBool(pSSM, &pThis->displayChangeData.fGuestSentChangeEventAck);

    rc = pHlp->pfnSSMGetU32(pSSM, &pThis->fGuestCaps);

    /* Attributes which were temporarily introduced in r30072 */
    if (uVersion == 7)
    {
        uint32_t temp;
        pHlp->pfnSSMGetU32(pSSM, &temp);
        rc = pHlp->pfnSSMGetU32(pSSM, &temp);
    }
    AssertRCReturn(rc, rc);

#ifdef VBOX_WITH_HGCM
    rc = vmmdevR3HgcmLoadState(pDevIns, pThis, pThisCC, pSSM, uVersion);
    AssertRCReturn(rc, rc);
#endif /* VBOX_WITH_HGCM */

    if (uVersion >= 10)
        rc = pHlp->pfnSSMGetU32(pSSM, &pThis->fHostCursorRequested);
    AssertRCReturn(rc, rc);

    if (uVersion > VMMDEV_SAVED_STATE_VERSION_MISSING_GUEST_INFO_2)
    {
        pHlp->pfnSSMGetU32(pSSM, &pThis->guestInfo2.uFullVersion);
        pHlp->pfnSSMGetU32(pSSM, &pThis->guestInfo2.uRevision);
        pHlp->pfnSSMGetU32(pSSM, &pThis->guestInfo2.fFeatures);
        rc = pHlp->pfnSSMGetStrZ(pSSM, &pThis->guestInfo2.szName[0], sizeof(pThis->guestInfo2.szName));
        AssertRCReturn(rc, rc);
    }

    if (uVersion > VMMDEV_SAVED_STATE_VERSION_MISSING_FACILITY_STATUSES)
    {
        uint32_t cFacilityStatuses;
        rc = pHlp->pfnSSMGetU32(pSSM, &cFacilityStatuses);
        AssertRCReturn(rc, rc);

        for (uint32_t i = 0; i < cFacilityStatuses; i++)
        {
            uint32_t uFacility, fFlags;
            uint16_t uStatus;
            int64_t  iTimeStampNano;

            pHlp->pfnSSMGetU32(pSSM, &uFacility);
            pHlp->pfnSSMGetU32(pSSM, &fFlags);
            pHlp->pfnSSMGetU16(pSSM, &uStatus);
            rc = pHlp->pfnSSMGetS64(pSSM, &iTimeStampNano);
            AssertRCReturn(rc, rc);

            PVMMDEVFACILITYSTATUSENTRY pEntry = vmmdevGetFacilityStatusEntry(pThis, (VBoxGuestFacilityType)uFacility);
            AssertLogRelMsgReturn(pEntry,
                                  ("VMMDev: Ran out of entries restoring the guest facility statuses. Saved state has %u.\n", cFacilityStatuses),
                                  VERR_OUT_OF_RESOURCES);
            pEntry->enmStatus = (VBoxGuestFacilityStatus)uStatus;
            pEntry->fFlags    = fFlags;
            RTTimeSpecSetNano(&pEntry->TimeSpecTS, iTimeStampNano);
        }
    }

    /*
     * Heartbeat.
     */
    if (uVersion >= VMMDEV_SAVED_STATE_VERSION_HEARTBEAT)
    {
        pHlp->pfnSSMGetBoolV(pSSM, &pThis->fHeartbeatActive);
        pHlp->pfnSSMGetBoolV(pSSM, &pThis->fFlatlined);
        pHlp->pfnSSMGetU64V(pSSM, &pThis->nsLastHeartbeatTS);
        rc = PDMDevHlpTimerLoad(pDevIns, pThis->hFlatlinedTimer, pSSM);
        AssertRCReturn(rc, rc);
        if (pThis->fFlatlined)
            LogRel(("vmmdevLoadState: Guest has flatlined. Last heartbeat %'RU64 ns before state was saved.\n",
                    PDMDevHlpTimerGetNano(pDevIns, pThis->hFlatlinedTimer) - pThis->nsLastHeartbeatTS));
    }

    if (uVersion >= VMMDEV_SAVED_STATE_VERSION_DISPLAY_CHANGE_DATA)
    {
        pHlp->pfnSSMGetStructEx(pSSM, &pThis->displayChangeData, sizeof(pThis->displayChangeData), 0,
                                g_aSSMDISPLAYCHANGEDATAStateFields, NULL);
    }

    /*
     * On a resume, we send the capabilities changed message so
     * that listeners can sync their state again
     */
    Log(("vmmdevLoadState: capabilities changed (%x), informing connector\n", pThis->fMouseCapabilities));
    if (pThisCC->pDrv)
    {
        pThisCC->pDrv->pfnUpdateMouseCapabilities(pThisCC->pDrv, pThis->fMouseCapabilities);
        if (uVersion >= 10)
            pThisCC->pDrv->pfnUpdatePointerShape(pThisCC->pDrv,
                                               /*fVisible=*/!!pThis->fHostCursorRequested,
                                               /*fAlpha=*/false,
                                               /*xHot=*/0, /*yHot=*/0,
                                               /*cx=*/0, /*cy=*/0,
                                               /*pvShape=*/NULL);
    }

    if (pThis->fu32AdditionsOk)
    {
        vmmdevLogGuestOsInfo(&pThis->guestInfo);
        if (pThisCC->pDrv)
        {
            if (pThis->guestInfo2.uFullVersion && pThisCC->pDrv->pfnUpdateGuestInfo2)
                pThisCC->pDrv->pfnUpdateGuestInfo2(pThisCC->pDrv, pThis->guestInfo2.uFullVersion, pThis->guestInfo2.szName,
                                                 pThis->guestInfo2.uRevision, pThis->guestInfo2.fFeatures);
            if (pThisCC->pDrv->pfnUpdateGuestInfo)
                pThisCC->pDrv->pfnUpdateGuestInfo(pThisCC->pDrv, &pThis->guestInfo);

            if (pThisCC->pDrv->pfnUpdateGuestStatus)
            {
                for (uint32_t i = 0; i < pThis->cFacilityStatuses; i++) /* ascending order! */
                    if (   pThis->aFacilityStatuses[i].enmStatus != VBoxGuestFacilityStatus_Inactive
                        || !pThis->aFacilityStatuses[i].fFixed)
                        pThisCC->pDrv->pfnUpdateGuestStatus(pThisCC->pDrv,
                                                          pThis->aFacilityStatuses[i].enmFacility,
                                                          (uint16_t)pThis->aFacilityStatuses[i].enmStatus,
                                                          pThis->aFacilityStatuses[i].fFlags,
                                                          &pThis->aFacilityStatuses[i].TimeSpecTS);
            }
        }
    }
    if (pThisCC->pDrv && pThisCC->pDrv->pfnUpdateGuestCapabilities)
        pThisCC->pDrv->pfnUpdateGuestCapabilities(pThisCC->pDrv, pThis->fGuestCaps);

    return VINF_SUCCESS;
}

/**
 * Load state done callback. Notify guest of restore event.
 *
 * @returns VBox status code.
 * @param   pDevIns    The device instance.
 * @param   pSSM The handle to the saved state.
 */
static DECLCALLBACK(int) vmmdevLoadStateDone(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PVMMDEV   pThis = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    PVMMDEVCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVMMDEVCC);
    RT_NOREF(pSSM);

#ifdef VBOX_WITH_HGCM
    int rc = vmmdevR3HgcmLoadStateDone(pDevIns, pThis, pThisCC);
    AssertLogRelRCReturn(rc, rc);
#endif /* VBOX_WITH_HGCM */

    /* Reestablish the acceleration status. */
    if (    pThis->u32VideoAccelEnabled
        &&  pThisCC->pDrv)
        pThisCC->pDrv->pfnVideoAccelEnable(pThisCC->pDrv, !!pThis->u32VideoAccelEnabled, &pThisCC->pVMMDevRAMR3->vbvaMemory);

    VMMDevNotifyGuest(pDevIns, pThis, pThisCC, VMMDEV_EVENT_RESTORED);

    return VINF_SUCCESS;
}


/* -=-=-=-=- PDMDEVREG -=-=-=-=- */

/**
 * (Re-)initializes the MMIO2 data.
 *
 * @param   pThisCC         The VMMDev ring-3 instance data.
 */
static void vmmdevInitRam(PVMMDEVCC pThisCC)
{
    memset(pThisCC->pVMMDevRAMR3, 0, sizeof(VMMDevMemory));
    pThisCC->pVMMDevRAMR3->u32Size    = sizeof(VMMDevMemory);
    pThisCC->pVMMDevRAMR3->u32Version = VMMDEV_MEMORY_VERSION;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) vmmdevReset(PPDMDEVINS pDevIns)
{
    PVMMDEV   pThis   = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    PVMMDEVCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVMMDEVCC);
    int const rcLock  = PDMDevHlpCritSectEnter(pDevIns, &pThis->CritSect, VERR_IGNORED);
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pThis->CritSect, rcLock);

    /*
     * Reset the mouse integration feature bits
     */
    if (pThis->fMouseCapabilities & VMMDEV_MOUSE_GUEST_MASK)
    {
        pThis->fMouseCapabilities &= ~VMMDEV_MOUSE_GUEST_MASK;
        /* notify the connector */
        Log(("vmmdevReset: capabilities changed (%x), informing connector\n", pThis->fMouseCapabilities));
        pThisCC->pDrv->pfnUpdateMouseCapabilities(pThisCC->pDrv, pThis->fMouseCapabilities);
    }
    pThis->fHostCursorRequested = false;

    /* re-initialize the VMMDev memory */
    if (pThisCC->pVMMDevRAMR3)
        vmmdevInitRam(pThisCC);

    /* credentials have to go away (by default) */
    VMMDEVCREDS *pCredentials = pThisCC->pCredentials;
    if (pCredentials)
    {
        if (!pThis->fKeepCredentials)
        {
            RT_ZERO(pCredentials->Logon.szUserName);
            RT_ZERO(pCredentials->Logon.szPassword);
            RT_ZERO(pCredentials->Logon.szDomain);
        }
        RT_ZERO(pCredentials->Judge.szUserName);
        RT_ZERO(pCredentials->Judge.szPassword);
        RT_ZERO(pCredentials->Judge.szDomain);
    }

    /* Reset means that additions will report again. */
    const bool fVersionChanged = pThis->fu32AdditionsOk
                              || pThis->guestInfo.interfaceVersion
                              || pThis->guestInfo.osType != VBOXOSTYPE_Unknown;
    if (fVersionChanged)
        Log(("vmmdevReset: fu32AdditionsOk=%d additionsVersion=%x osType=%#x\n",
             pThis->fu32AdditionsOk, pThis->guestInfo.interfaceVersion, pThis->guestInfo.osType));
    pThis->fu32AdditionsOk = false;
    memset (&pThis->guestInfo, 0, sizeof (pThis->guestInfo));
    RT_ZERO(pThis->guestInfo2);
    const bool fCapsChanged = pThis->fGuestCaps != 0; /* Report transition to 0. */
    pThis->fGuestCaps = 0;

    /* Clear facilities. No need to tell Main as it will get a
       pfnUpdateGuestInfo callback. */
    RTTIMESPEC TimeStampNow;
    RTTimeNow(&TimeStampNow);
    uint32_t iFacility = pThis->cFacilityStatuses;
    while (iFacility-- > 0)
    {
        pThis->aFacilityStatuses[iFacility].enmStatus  = VBoxGuestFacilityStatus_Inactive;
        pThis->aFacilityStatuses[iFacility].TimeSpecTS = TimeStampNow;
    }

    /* clear pending display change request. */
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->displayChangeData.aRequests); i++)
    {
        DISPLAYCHANGEREQUEST *pRequest = &pThis->displayChangeData.aRequests[i];
        memset(&pRequest->lastReadDisplayChangeRequest, 0, sizeof(pRequest->lastReadDisplayChangeRequest));
        pRequest->lastReadDisplayChangeRequest.fDisplayFlags = VMMDEV_DISPLAY_DISABLED;
        pRequest->lastReadDisplayChangeRequest.idDisplay = i;
    }
    pThis->displayChangeData.iCurrentMonitor = 0;
    pThis->displayChangeData.fGuestSentChangeEventAck = false;

    /* disable seamless mode */
    pThis->fLastSeamlessEnabled = false;

    /* disabled memory ballooning */
    pThis->cMbMemoryBalloonLast = 0;

    /* disabled statistics updating */
    pThis->cSecsLastStatInterval = 0;

#ifdef VBOX_WITH_HGCM
    /* Clear the "HGCM event enabled" flag so the event can be automatically reenabled.  */
    pThisCC->u32HGCMEnabled = 0;
#endif

    /*
     * Deactive heartbeat.
     */
    if (pThis->fHeartbeatActive)
    {
        PDMDevHlpTimerStop(pDevIns, pThis->hFlatlinedTimer);
        pThis->fFlatlined       = false;
        pThis->fHeartbeatActive = true;
    }

    /*
     * Clear the event variables.
     *
     * XXX By design we should NOT clear pThis->fHostEventFlags because it is designed
     *     that way so host events do not depend on guest resets. However, the pending
     *     event flags actually _were_ cleared since ages so we mask out events from
     *     clearing which we really need to survive the reset. See xtracker 5767.
     */
    pThis->fHostEventFlags    &= VMMDEV_EVENT_DISPLAY_CHANGE_REQUEST;
    pThis->fGuestFilterMask    = 0;
    pThis->fNewGuestFilterMask = 0;
    pThis->fNewGuestFilterMaskValid   = 0;

    /*
     * Call the update functions as required.
     */
    if (fVersionChanged && pThisCC->pDrv && pThisCC->pDrv->pfnUpdateGuestInfo)
        pThisCC->pDrv->pfnUpdateGuestInfo(pThisCC->pDrv, &pThis->guestInfo);
    if (fCapsChanged && pThisCC->pDrv && pThisCC->pDrv->pfnUpdateGuestCapabilities)
        pThisCC->pDrv->pfnUpdateGuestCapabilities(pThisCC->pDrv, pThis->fGuestCaps);

    /*
     * Generate a unique session id for this VM; it will be changed for each start, reset or restore.
     * This can be used for restore detection inside the guest.
     */
    pThis->idSession = ASMReadTSC();

    PDMDevHlpCritSectLeave(pDevIns, &pThis->CritSect);
}


#ifdef VBOX_WITH_RAW_MODE_KEEP
/**
 * @interface_method_impl{PDMDEVREG,pfnRelocate}
 */
static DECLCALLBACK(void) vmmdevRelocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta)
{
    if (offDelta)
    {
        PVMMDEV pThis = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
        LogFlow(("vmmdevRelocate: offDelta=%RGv\n", offDelta));

        if (pThis->pVMMDevRAMRC)
            pThis->pVMMDevRAMRC += offDelta;
        pThis->pDevInsRC = PDMDEVINS_2_RCPTR(pDevIns);
    }
}
#endif


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) vmmdevDestruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PVMMDEVCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVMMDEVCC);

    /*
     * Wipe and free the credentials.
     */
    VMMDEVCREDS *pCredentials = pThisCC->pCredentials;
    pThisCC->pCredentials = NULL;
    if (pCredentials)
    {
        if (pThisCC->fSaferCredentials)
            RTMemSaferFree(pCredentials, sizeof(*pCredentials));
        else
        {
            RTMemWipeThoroughly(pCredentials, sizeof(*pCredentials), 10);
            RTMemFree(pCredentials);
        }
    }

#ifdef VBOX_WITH_HGCM
    /*
     * Everything HGCM.
     */
    vmmdevR3HgcmDestroy(pDevIns, PDMDEVINS_2_DATA(pDevIns, PVMMDEV), pThisCC);
#endif

    /*
     * Free the request buffers.
     */
    for (uint32_t iCpu = 0; iCpu < RT_ELEMENTS(pThisCC->apReqBufs); iCpu++)
    {
        RTMemPageFree(pThisCC->apReqBufs[iCpu], _4K);
        pThisCC->apReqBufs[iCpu] = NULL;
    }

#ifndef VBOX_WITHOUT_TESTING_FEATURES
    /*
     * Clean up the testing device.
     */
    vmmdevR3TestingTerminate(pDevIns);
#endif

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) vmmdevConstruct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PVMMDEVCC       pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVMMDEVCC);
    PVMMDEV         pThis   = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    int             rc;

    Assert(iInstance == 0);
    RT_NOREF(iInstance);

    /*
     * Initialize data (most of it anyway).
     */
    pThisCC->pDevIns = pDevIns;

    pThis->hFlatlinedTimer      = NIL_TMTIMERHANDLE;
    pThis->hIoPortBackdoorLog   = NIL_IOMIOPORTHANDLE;
    pThis->hIoPortAltTimesync   = NIL_IOMIOPORTHANDLE;
    pThis->hIoPortReq           = NIL_IOMIOPORTHANDLE;
    pThis->hIoPortFast          = NIL_IOMIOPORTHANDLE;
    pThis->hMmio2VMMDevRAM      = NIL_PGMMMIO2HANDLE;
    pThis->hMmio2Heap           = NIL_PGMMMIO2HANDLE;
#ifndef VBOX_WITHOUT_TESTING_FEATURES
    pThis->hIoPortTesting       = NIL_IOMIOPORTHANDLE;
    pThis->hMmioTesting         = NIL_IOMMMIOHANDLE;
    pThis->hTestingLockEvt      = NIL_SUPSEMEVENT;
#endif

    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    /* PCI vendor, just a free bogus value */
    PDMPciDevSetVendorId(pPciDev,     0x80ee);
    /* device ID */
    PDMPciDevSetDeviceId(pPciDev,     0xcafe);
    /* class sub code (other type of system peripheral) */
    PDMPciDevSetClassSub(pPciDev,       0x80);
    /* class base code (base system peripheral) */
    PDMPciDevSetClassBase(pPciDev,      0x08);
    /* header type */
    PDMPciDevSetHeaderType(pPciDev,     0x00);
    /* interrupt on pin 0 */
    PDMPciDevSetInterruptPin(pPciDev,   0x01);

    RTTIMESPEC TimeStampNow;
    RTTimeNow(&TimeStampNow);
    vmmdevAllocFacilityStatusEntry(pThis, VBoxGuestFacilityType_VBoxGuestDriver, true /*fFixed*/, &TimeStampNow);
    vmmdevAllocFacilityStatusEntry(pThis, VBoxGuestFacilityType_VBoxService,     true /*fFixed*/, &TimeStampNow);
    vmmdevAllocFacilityStatusEntry(pThis, VBoxGuestFacilityType_VBoxTrayClient,  true /*fFixed*/, &TimeStampNow);
    vmmdevAllocFacilityStatusEntry(pThis, VBoxGuestFacilityType_Seamless,        true /*fFixed*/, &TimeStampNow);
    vmmdevAllocFacilityStatusEntry(pThis, VBoxGuestFacilityType_Graphics,        true /*fFixed*/, &TimeStampNow);
    Assert(pThis->cFacilityStatuses == 5);

    /* disable all screens (no better hints known yet). */
    /** @todo r=klaus need a way to represent "no hint known" */
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->displayChangeData.aRequests); i++)
    {
        DISPLAYCHANGEREQUEST *pRequest = &pThis->displayChangeData.aRequests[i];
        pRequest->displayChangeRequest.fDisplayFlags = VMMDEV_DISPLAY_DISABLED;
        pRequest->displayChangeRequest.idDisplay = i;
        pRequest->lastReadDisplayChangeRequest.fDisplayFlags = VMMDEV_DISPLAY_DISABLED;
        pRequest->lastReadDisplayChangeRequest.idDisplay = i;
    }

    /*
     * Interfaces
     */
    /* IBase */
    pThisCC->IBase.pfnQueryInterface          = vmmdevPortQueryInterface;

    /* VMMDev port */
    pThisCC->IPort.pfnQueryAbsoluteMouse      = vmmdevIPort_QueryAbsoluteMouse;
    pThisCC->IPort.pfnSetAbsoluteMouse        = vmmdevIPort_SetAbsoluteMouse ;
    pThisCC->IPort.pfnQueryMouseCapabilities  = vmmdevIPort_QueryMouseCapabilities;
    pThisCC->IPort.pfnUpdateMouseCapabilities = vmmdevIPort_UpdateMouseCapabilities;
    pThisCC->IPort.pfnRequestDisplayChange    = vmmdevIPort_RequestDisplayChange;
    pThisCC->IPort.pfnSetCredentials          = vmmdevIPort_SetCredentials;
    pThisCC->IPort.pfnVBVAChange              = vmmdevIPort_VBVAChange;
    pThisCC->IPort.pfnRequestSeamlessChange   = vmmdevIPort_RequestSeamlessChange;
    pThisCC->IPort.pfnSetMemoryBalloon        = vmmdevIPort_SetMemoryBalloon;
    pThisCC->IPort.pfnSetStatisticsInterval   = vmmdevIPort_SetStatisticsInterval;
    pThisCC->IPort.pfnVRDPChange              = vmmdevIPort_VRDPChange;
    pThisCC->IPort.pfnCpuHotUnplug            = vmmdevIPort_CpuHotUnplug;
    pThisCC->IPort.pfnCpuHotPlug              = vmmdevIPort_CpuHotPlug;

    /* Shared folder LED */
    pThisCC->SharedFolders.Led.u32Magic       = PDMLED_MAGIC;
    pThisCC->SharedFolders.ILeds.pfnQueryStatusLed = vmmdevQueryStatusLed;

#ifdef VBOX_WITH_HGCM
    /* HGCM port */
    pThisCC->IHGCMPort.pfnCompleted           = hgcmR3Completed;
    pThisCC->IHGCMPort.pfnIsCmdRestored       = hgcmR3IsCmdRestored;
    pThisCC->IHGCMPort.pfnIsCmdCancelled      = hgcmR3IsCmdCancelled;
    pThisCC->IHGCMPort.pfnGetRequestor        = hgcmR3GetRequestor;
    pThisCC->IHGCMPort.pfnGetVMMDevSessionId  = hgcmR3GetVMMDevSessionId;
#endif

    pThisCC->pCredentials = (VMMDEVCREDS *)RTMemSaferAllocZ(sizeof(*pThisCC->pCredentials));
    if (pThisCC->pCredentials)
        pThisCC->fSaferCredentials = true;
    else
    {
        pThisCC->pCredentials = (VMMDEVCREDS *)RTMemAllocZ(sizeof(*pThisCC->pCredentials));
        AssertReturn(pThisCC->pCredentials, VERR_NO_MEMORY);
    }


    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns,
                                  "AllowGuestToSaveState|"
                                  "GetHostTimeDisabled|"
                                  "BackdoorLogDisabled|"
                                  "KeepCredentials|"
                                  "HeapEnabled|"
                                  "GuestCoreDumpEnabled|"
                                  "GuestCoreDumpDir|"
                                  "GuestCoreDumpCount|"
                                  "HeartbeatInterval|"
                                  "HeartbeatTimeout|"
                                  "MmioReq|"
                                  "TestingEnabled|"
                                  "TestingMMIO|"
                                  "TestingXmlOutputFile|"
                                  "TestingCfgDword0|"
                                  "TestingCfgDword1|"
                                  "TestingCfgDword2|"
                                  "TestingCfgDword3|"
                                  "TestingCfgDword4|"
                                  "TestingCfgDword5|"
                                  "TestingCfgDword6|"
                                  "TestingCfgDword7|"
                                  "TestingCfgDword8|"
                                  "TestingCfgDword9|"
                                  "TestingThresholdNativeRecompiler|"
                                  "HGCMHeapBudgetDefault|"
                                  "HGCMHeapBudgetLegacy|"
                                  "HGCMHeapBudgetVBoxGuest|"
                                  "HGCMHeapBudgetOtherDrv|"
                                  "HGCMHeapBudgetRoot|"
                                  "HGCMHeapBudgetSystem|"
                                  "HGCMHeapBudgetReserved1|"
                                  "HGCMHeapBudgetUser|"
                                  "HGCMHeapBudgetGuest"
                                  ,
                                  "");

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "AllowGuestToSaveState", &pThis->fAllowGuestToSaveState, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed querying \"AllowGuestToSaveState\" as a boolean"));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "GetHostTimeDisabled", &pThis->fGetHostTimeDisabled, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed querying \"GetHostTimeDisabled\" as a boolean"));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "BackdoorLogDisabled", &pThis->fBackdoorLogDisabled, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed querying \"BackdoorLogDisabled\" as a boolean"));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "KeepCredentials", &pThis->fKeepCredentials, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed querying \"KeepCredentials\" as a boolean"));

    /* The heap is of no use on non x86 guest architectures. */
    static const bool fHeapEnabledDef = PDMDevHlpCpuIsGuestArchX86(pDevIns);
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "HeapEnabled", &pThis->fHeapEnabled, fHeapEnabledDef);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed querying \"HeapEnabled\" as a boolean"));

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "GuestCoreDumpEnabled", &pThis->fGuestCoreDumpEnabled, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed querying \"GuestCoreDumpEnabled\" as a boolean"));

    char *pszGuestCoreDumpDir = NULL;
    rc = pHlp->pfnCFGMQueryStringAllocDef(pCfg, "GuestCoreDumpDir", &pszGuestCoreDumpDir, "");
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed querying \"GuestCoreDumpDir\" as a string"));

    RTStrCopy(pThis->szGuestCoreDumpDir, sizeof(pThis->szGuestCoreDumpDir), pszGuestCoreDumpDir);
    PDMDevHlpMMHeapFree(pDevIns, pszGuestCoreDumpDir);

    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "GuestCoreDumpCount", &pThis->cGuestCoreDumps, 3);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed querying \"GuestCoreDumpCount\" as a 32-bit unsigned integer"));

    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "HeartbeatInterval", &pThis->cNsHeartbeatInterval, VMMDEV_HEARTBEAT_DEFAULT_INTERVAL);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed querying \"HeartbeatInterval\" as a 64-bit unsigned integer"));
    if (pThis->cNsHeartbeatInterval < RT_NS_100MS / 2)
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Heartbeat interval \"HeartbeatInterval\" too small"));

    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "HeartbeatTimeout", &pThis->cNsHeartbeatTimeout, pThis->cNsHeartbeatInterval * 2);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("Configuration error: Failed querying \"HeartbeatTimeout\" as a 64-bit unsigned integer"));
    if (pThis->cNsHeartbeatTimeout < RT_NS_100MS)
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Heartbeat timeout \"HeartbeatTimeout\" too small"));
    if (pThis->cNsHeartbeatTimeout <= pThis->cNsHeartbeatInterval + RT_NS_10MS)
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Configuration error: Heartbeat timeout \"HeartbeatTimeout\" value (%'ull ns) is too close to the interval (%'ull ns)"),
                                   pThis->cNsHeartbeatTimeout, pThis->cNsHeartbeatInterval);

    /* On everthing els than x86 we have to offer the MMIO interface because port I/O is either not available or emulated through MMIO anyway. */
    static const bool fMmioReqEnabledDef = !PDMDevHlpCpuIsGuestArchX86(pDevIns);
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "MmioReq", &pThis->fMmioReq, fMmioReqEnabledDef);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed querying \"MmioReq\" as a boolean"));

#ifndef VBOX_WITHOUT_TESTING_FEATURES
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "TestingEnabled", &pThis->fTestingEnabled, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed querying \"TestingEnabled\" as a boolean"));
    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "TestingMMIO", &pThis->fTestingMMIO, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed querying \"TestingMMIO\" as a boolean"));
    rc = pHlp->pfnCFGMQueryStringAllocDef(pCfg, "TestingXmlOutputFile", &pThisCC->pszTestingXmlOutput, NULL);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed querying \"TestingXmlOutputFile\" as a string"));

    for (unsigned i = 0; i < RT_ELEMENTS(pThis->au32TestingCfgDwords); i++)
    {
        char szName[32];
        RTStrPrintf(szName, sizeof(szName), "TestingCfgDword%u", i);
        rc = pHlp->pfnCFGMQueryU32Def(pCfg, szName, &pThis->au32TestingCfgDwords[i], 0);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Configuration error: Failed querying \"%s\" as an 32-bit unsigned int"), szName);
    }

    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "TestingThresholdNativeRecompiler", &pThis->cTestingThresholdNativeRecompiler, 0);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Configuration error: Failed querying \"TestingThresholdNativeRecompiler\" as an 16-bit unsigned int"));


    /** @todo image-to-load-filename? */
#endif

#ifdef VBOX_WITH_HGCM
    /*
     * Heap budgets for HGCM requestor categories.  Take the available host
     * memory as a rough hint of how much we can handle.
     */
    uint64_t cbDefaultBudget = 0;
    if (RT_FAILURE(RTSystemQueryTotalRam(&cbDefaultBudget)))
        cbDefaultBudget = 8 * _1G64;
    LogFunc(("RTSystemQueryTotalRam -> %'RU64 (%RX64)\n", cbDefaultBudget, cbDefaultBudget));
# if ARCH_BITS == 32
    cbDefaultBudget  = RT_MIN(cbDefaultBudget, _512M);
# endif
    cbDefaultBudget /= 8;                               /* One eighth of physical memory ... */
    cbDefaultBudget /= RT_ELEMENTS(pThisCC->aHgcmAcc);  /* over 3 accounting categories. (8GiB -> 341MiB) */
    cbDefaultBudget  = RT_MIN(cbDefaultBudget, _1G);    /* max 1024MiB */
    cbDefaultBudget  = RT_MAX(cbDefaultBudget, _32M);   /* min   32MiB */
    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "HGCMHeapBudgetDefault", &cbDefaultBudget, cbDefaultBudget);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed querying \"HGCMHeapBudgetDefault\" as a 64-bit unsigned integer"));

    LogRel(("VMMDev: cbDefaultBudget: %'RU64 (%RX64)\n", cbDefaultBudget, cbDefaultBudget));
    static const struct { const char *pszName; unsigned idx; } s_aCfgHeapBudget[] =
    {
        { "HGCMHeapBudgetKernel",       VMMDEV_HGCM_CATEGORY_KERNEL },
        { "HGCMHeapBudgetRoot",         VMMDEV_HGCM_CATEGORY_ROOT   },
        { "HGCMHeapBudgetUser",         VMMDEV_HGCM_CATEGORY_USER   },
    };
    AssertCompile(RT_ELEMENTS(s_aCfgHeapBudget) == RT_ELEMENTS(pThisCC->aHgcmAcc));
    for (uintptr_t i = 0; i < RT_ELEMENTS(s_aCfgHeapBudget); i++)
    {
        uintptr_t const idx = s_aCfgHeapBudget[i].idx;
        rc = pHlp->pfnCFGMQueryU64Def(pCfg, s_aCfgHeapBudget[i].pszName,
                                      &pThisCC->aHgcmAcc[idx].cbHeapBudgetConfig, cbDefaultBudget);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Configuration error: Failed querying \"%s\" as a 64-bit unsigned integer"),
                                       s_aCfgHeapBudget[i].pszName);
        pThisCC->aHgcmAcc[idx].cbHeapBudget = pThisCC->aHgcmAcc[idx].cbHeapBudgetConfig;
        if (pThisCC->aHgcmAcc[idx].cbHeapBudgetConfig != cbDefaultBudget)
            LogRel(("VMMDev: %s: %'RU64 (%#RX64)\n", s_aCfgHeapBudget[i].pszName,
                    pThisCC->aHgcmAcc[idx].cbHeapBudgetConfig, pThisCC->aHgcmAcc[idx].cbHeapBudgetConfig));

        const char * const pszCatName = &s_aCfgHeapBudget[i].pszName[sizeof("HGCMHeapBudget") - 1];
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aHgcmAcc[idx].cbHeapBudget, STAMTYPE_U64, STAMVISIBILITY_ALWAYS,
                               STAMUNIT_BYTES, "Currently available budget", "HGCM-%s/BudgetAvailable", pszCatName);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aHgcmAcc[idx].cbHeapBudgetConfig, STAMTYPE_U64, STAMVISIBILITY_ALWAYS,
                               STAMUNIT_BYTES, "Configured budget",          "HGCM-%s/BudgetConfig", pszCatName);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aHgcmAcc[idx].StateMsgHeapUsage, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS,
                               STAMUNIT_BYTES_PER_CALL, "Message heap usage", "HGCM-%s/MessageHeapUsage", pszCatName);
        PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->aHgcmAcc[idx].StatBudgetOverruns, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS,
                               STAMUNIT_BYTES, "Budget overruns and allocation errors", "HGCM-%s/BudgetOverruns", pszCatName);
    }
#endif

    /*
     * <missing comment>
     */
    pThis->cbGuestRAM = PDMDevHlpMMPhysGetRamSize(pDevIns);

    /*
     * We do our own locking entirely. So, install NOP critsect for the device
     * and create our own critsect for use where it really matters (++).
     */
    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);
    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->CritSect, RT_SRC_POS, "VMMDev#%u", iInstance);
    AssertRCReturn(rc, rc);

    /*
     * Register the backdoor logging port
     */
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, RTLOG_DEBUG_PORT, 1, vmmdevBackdoorLog, NULL /*pfnIn*/,
                                     "VMMDev backdoor logging", NULL, &pThis->hIoPortBackdoorLog);
    AssertRCReturn(rc, rc);

#ifdef VMMDEV_WITH_ALT_TIMESYNC
    /*
     * Alternative timesync source.
     *
     * This was orignally added for creating a simple time sync service in an
     * OpenBSD guest without requiring VBoxGuest and VBoxService to be ported
     * first.  We keep it in case it comes in handy.
     */
    rc = PDMDevHlpIoPortCreateAndMap(pDevIns, 0x505, 1, vmmdevAltTimeSyncWrite, vmmdevAltTimeSyncRead,
                                     "VMMDev timesync backdoor", NULL /*paExtDescs*/, &pThis->hIoPortAltTimesync);
    AssertRCReturn(rc, rc);
#endif

    /*
     * Register the PCI device.
     */
    rc = PDMDevHlpPCIRegister(pDevIns, pPciDev);
    if (RT_FAILURE(rc))
        return rc;
    if (pPciDev->uDevFn != 32 || iInstance != 0)
        Log(("!!WARNING!!: pThis->PciDev.uDevFn=%d (ignore if testcase or no started by Main)\n", pPciDev->uDevFn));

    /*
     * The I/O ports, PCI region #0.  This has two separate I/O port mappings in it,
     * so we have to do it via the mapper callback.
     */
    rc = PDMDevHlpIoPortCreate(pDevIns, 1 /*cPorts*/, pPciDev, RT_MAKE_U32(0, 0), vmmdevPioRequestHandler, NULL /*pfnIn*/,
                               NULL /*pvUser*/, "VMMDev Request Handler",  NULL, &pThis->hIoPortReq);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpIoPortCreate(pDevIns, 1 /*cPorts*/, pPciDev, RT_MAKE_U32(1, 0),  vmmdevPioFastRequestHandler,
                               vmmdevPioFastRequestIrqAck, NULL, "VMMDev Fast R0/RC Requests", NULL /*pvUser*/, &pThis->hIoPortFast);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpPCIIORegionRegisterIoCustom(pDevIns, 0, 0x20, vmmdevIOPortRegionMap);
    AssertRCReturn(rc, rc);

    /*
     * Allocate and initialize the MMIO2 memory, PCI region #1.
     */
    rc = PDMDevHlpPCIIORegionCreateMmio2(pDevIns, 1 /*iPciRegion*/, VMMDEV_RAM_SIZE, PCI_ADDRESS_SPACE_MEM, "VMMDev",
                                         (void **)&pThisCC->pVMMDevRAMR3, &pThis->hMmio2VMMDevRAM);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Failed to create the %u (%#x) byte MMIO2 region for the VMM device"),
                                   VMMDEV_RAM_SIZE, VMMDEV_RAM_SIZE);
    vmmdevInitRam(pThisCC);

    /*
     * The MMIO2 heap (used for real-mode VT-x trickery), PCI region #2.
     */
    if (pThis->fHeapEnabled)
    {
        rc = PDMDevHlpPCIIORegionCreateMmio2Ex(pDevIns, 2 /*iPciRegion*/, VMMDEV_HEAP_SIZE, PCI_ADDRESS_SPACE_MEM_PREFETCH,
                                               0 /*fFlags*/, vmmdevMmio2HeapRegionMap, "VMMDev Heap",
                                               (void **)&pThisCC->pVMMDevHeapR3, &pThis->hMmio2Heap);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("Failed to create the %u (%#x) bytes MMIO2 heap region for the VMM device"),
                                       VMMDEV_HEAP_SIZE, VMMDEV_HEAP_SIZE);

        /* Register the memory area with PDM so HM can access it before it's mapped. */
        rc = PDMDevHlpRegisterVMMDevHeap(pDevIns, NIL_RTGCPHYS, pThisCC->pVMMDevHeapR3, VMMDEV_HEAP_SIZE);
        AssertLogRelRCReturn(rc, rc);
    }

    if (pThis->fMmioReq)
    {
        rc = PDMDevHlpPCIIORegionCreateMmio(pDevIns, 3 /*iPciRegion*/, VMMDEV_MMIO_SIZE, PCI_ADDRESS_SPACE_MEM,
                                            vmmdevMmioWrite, vmmdevMmioRead, NULL,
                                            IOMMMIO_FLAGS_READ_PASSTHRU | IOMMMIO_FLAGS_WRITE_PASSTHRU,
                                            "VMMDev MMIO Request Handler", &pThis->hMmioReq);
        AssertRCReturn(rc, rc);
    }

#ifndef VBOX_WITHOUT_TESTING_FEATURES
    /*
     * Initialize testing.
     */
    rc = vmmdevR3TestingInitialize(pDevIns);
    if (RT_FAILURE(rc))
        return rc;
#endif

    /*
     * Get the corresponding connector interface
     */
    rc = PDMDevHlpDriverAttach(pDevIns, 0, &pThisCC->IBase, &pThisCC->pDrvBase, "VMM Driver Port");
    if (RT_SUCCESS(rc))
    {
        pThisCC->pDrv = PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMIVMMDEVCONNECTOR);
        AssertMsgReturn(pThisCC->pDrv, ("LUN #0 doesn't have a VMMDev connector interface!\n"), VERR_PDM_MISSING_INTERFACE);
#ifdef VBOX_WITH_HGCM
        pThisCC->pHGCMDrv = PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMIHGCMCONNECTOR);
        if (!pThisCC->pHGCMDrv)
        {
            Log(("LUN #0 doesn't have a HGCM connector interface, HGCM is not supported. rc=%Rrc\n", rc));
            /* this is not actually an error, just means that there is no support for HGCM */
        }
#endif
        /* Query the initial balloon size. */
        AssertPtr(pThisCC->pDrv->pfnQueryBalloonSize);
        rc = pThisCC->pDrv->pfnQueryBalloonSize(pThisCC->pDrv, &pThis->cMbMemoryBalloon);
        AssertRC(rc);

        Log(("Initial balloon size %x\n", pThis->cMbMemoryBalloon));
    }
    else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
    {
        Log(("%s/%d: warning: no driver attached to LUN #0!\n", pDevIns->pReg->szName, pDevIns->iInstance));
        rc = VINF_SUCCESS;
    }
    else
        AssertMsgFailedReturn(("Failed to attach LUN #0! rc=%Rrc\n", rc), rc);

    /*
     * Attach status driver for shared folders (optional).
     */
    PPDMIBASE pBase;
    rc = PDMDevHlpDriverAttach(pDevIns, PDM_STATUS_LUN, &pThisCC->IBase, &pBase, "Status Port");
    if (RT_SUCCESS(rc))
        pThisCC->SharedFolders.pLedsConnector = PDMIBASE_QUERY_INTERFACE(pBase, PDMILEDCONNECTORS);
    else if (rc != VERR_PDM_NO_ATTACHED_DRIVER)
    {
        AssertMsgFailed(("Failed to attach to status driver. rc=%Rrc\n", rc));
        return rc;
    }

    /*
     * Register saved state and init the HGCM CmdList critsect.
     */
    rc = PDMDevHlpSSMRegisterEx(pDevIns, VMMDEV_SAVED_STATE_VERSION, sizeof(*pThis), NULL,
                                NULL, vmmdevLiveExec, NULL,
                                NULL, vmmdevSaveExec, NULL,
                                NULL, vmmdevLoadExec, vmmdevLoadStateDone);
    AssertRCReturn(rc, rc);

    /*
     * Create heartbeat checking timer.
     */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, vmmDevHeartbeatFlatlinedTimer, pThis,
                              TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_RING0, "Heartbeat flatlined", &pThis->hFlatlinedTimer);
    AssertRCReturn(rc, rc);

#ifdef VBOX_WITH_HGCM
    rc = vmmdevR3HgcmInit(pThisCC);
    AssertRCReturn(rc, rc);
#endif

    /*
     * In this version of VirtualBox the GUI checks whether "needs host cursor"
     * changes.
     */
    pThis->fMouseCapabilities |= VMMDEV_MOUSE_HOST_RECHECKS_NEEDS_HOST_CURSOR;

    /*
     * In this version of VirtualBox full mouse state can be provided to the guest over DevVMM.
     */
    pThis->fMouseCapabilities |= VMMDEV_MOUSE_HOST_SUPPORTS_FULL_STATE_PROTOCOL;

    /*
     * Statistics.
     */
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatMemBalloonChunks,    STAMTYPE_U32, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                           "Memory balloon size",                           "BalloonChunks");
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatFastIrqAckR3,        STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                           "Fast IRQ acknowledgments handled in ring-3.",   "FastIrqAckR3");
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatFastIrqAckRZ,        STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                           "Fast IRQ acknowledgments handled in ring-0 or raw-mode.", "FastIrqAckRZ");
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatSlowIrqAck,          STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                           "Slow IRQ acknowledgments (old style).",         "SlowIrqAck");
    PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->StatReqBufAllocs,      STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                           "Times a larger request buffer was required.",   "LargeReqBufAllocs");
#ifdef VBOX_WITH_HGCM
    PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->StatHgcmCmdArrival,    STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                           "Profiling HGCM call arrival processing",        "/HGCM/MsgArrival");
    PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->StatHgcmCmdCompletion, STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                           "Profiling HGCM call completion processing",     "/HGCM/MsgCompletion");
    PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->StatHgcmCmdTotal,      STAMTYPE_PROFILE, STAMVISIBILITY_ALWAYS, STAMUNIT_TICKS_PER_CALL,
                           "Profiling whole HGCM call.",                    "/HGCM/MsgTotal");
    PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->StatHgcmLargeCmdAllocs,STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                           "Times the allocation cache could not be used.", "/HGCM/LargeCmdAllocs");
    PDMDevHlpSTAMRegisterF(pDevIns, &pThisCC->StatHgcmFailedPageListLocking,STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_COUNT,
                           "Times no-bounce page list locking failed.",     "/HGCM/FailedPageListLocking");
#endif

    /*
     * Generate a unique session id for this VM; it will be changed for each
     * start, reset or restore. This can be used for restore detection inside
     * the guest.
     */
    pThis->idSession = ASMReadTSC();
    return rc;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) vmmdevRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PVMMDEV   pThis   = PDMDEVINS_2_DATA(pDevIns, PVMMDEV);
    PVMMDEVCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVMMDEVCC);

    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

#if 0
    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortBackdoorLog, vmmdevBackdoorLog, NULL /*pfnIn*/, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);
#endif
#if 0 && defined(VMMDEV_WITH_ALT_TIMESYNC)
    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortAltTimesync, vmmdevAltTimeSyncWrite, vmmdevAltTimeSyncRead, NULL);
    AssertRCReturn(rc, rc);
#endif

    /*
     * We map the first page of the VMMDevRAM into raw-mode and kernel contexts so we
     * can handle interrupt acknowledge requests more timely (vmmdevPioFastRequestIrqAck).
     */
    rc = PDMDevHlpMmio2SetUpContext(pDevIns, pThis->hMmio2VMMDevRAM, 0, VMMDEV_PAGE_SIZE, (void **)&pThisCC->CTX_SUFF(pVMMDevRAM));
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPortFast, vmmdevPioFastRequestHandler, vmmdevPioFastRequestIrqAck, NULL);
    AssertRCReturn(rc, rc);

    if (pThis->fMmioReq)
    {
        rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmioReq, vmmdevMmioWrite, vmmdevMmioRead, NULL /*pvUser*/);
        AssertRCReturn(rc, rc);
    }

# ifndef VBOX_WITHOUT_TESTING_FEATURES
    /*
     * Initialize testing.
     */
    rc = vmmdevRZTestingInitialize(pDevIns);
    AssertRCReturn(rc, rc);
# endif

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
extern "C" const PDMDEVREG g_DeviceVMMDev =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "VMMDev",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_VMM_DEV,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(VMMDEV),
    /* .cbInstanceCC = */           sizeof(VMMDEVCC),
    /* .cbInstanceRC = */           sizeof(VMMDEVRC),
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "VirtualBox VMM Device\n",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           vmmdevConstruct,
    /* .pfnDestruct = */            vmmdevDestruct,
# ifdef VBOX_WITH_RAW_MODE_KEEP
    /* .pfnRelocate = */            vmmdevRelocate,
# else
    /* .pfnRelocate = */            NULL,
# endif
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               vmmdevReset,
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
    /* .pfnConstruct = */           vmmdevRZConstruct,
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
    /* .pfnConstruct = */           vmmdevRZConstruct,
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

#endif /* !VBOX_DEVICE_STRUCT_TESTCASE */
