/* $Id: VBoxNetFltInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetFlt - Network Filter Driver (Host), Internal Header.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_VBoxNetFlt_VBoxNetFltInternal_h
#define VBOX_INCLUDED_SRC_VBoxNetFlt_VBoxNetFltInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/sup.h>
#include <VBox/intnet.h>
#include <iprt/semaphore.h>
#include <iprt/assert.h>


RT_C_DECLS_BEGIN

/** Pointer to the globals. */
typedef struct VBOXNETFLTGLOBALS *PVBOXNETFLTGLOBALS;


/**
 * The state of a filter driver instance.
 *
 * The state machine differs a bit between the platforms because of
 * the way we hook into the stack. On some hosts we can dynamically
 * attach when required (on CreateInstance) and on others we will
 * have to connect when the network stack is bound up. These modes
 * are called static and dynamic config and governed at compile time
 * by the VBOXNETFLT_STATIC_CONFIG define.
 *
 * See sec_netflt_msc for more details on locking and synchronization.
 */
typedef enum VBOXNETFTLINSSTATE
{
    /** The usual invalid state. */
    kVBoxNetFltInsState_Invalid = 0,
    /** Initialization.
     * We've reserved the interface name but need to attach to the actual
     * network interface outside the lock to avoid deadlocks.
     * In the dynamic case this happens during a Create(Instance) call.
     * In the static case it happens during driver initialization. */
    kVBoxNetFltInsState_Initializing,
#ifdef VBOXNETFLT_STATIC_CONFIG
    /** Unconnected, not hooked up to a switch (static only).
     * The filter driver instance has been instantiated and hooked up,
     * waiting to be connected to an internal network. */
    kVBoxNetFltInsState_Unconnected,
#endif
    /** Connected to an internal network. */
    kVBoxNetFltInsState_Connected,
    /** Disconnecting from the internal network and possibly the host network interface.
     * Partly for reasons of deadlock avoidance again. */
    kVBoxNetFltInsState_Disconnecting,
    /** The instance has been disconnected from both the host and the internal network. */
    kVBoxNetFltInsState_Destroyed,

    /** The habitual 32-bit enum hack.  */
    kVBoxNetFltInsState_32BitHack = 0x7fffffff
} VBOXNETFTLINSSTATE;


/**
 * The per-instance data of the VBox filter driver.
 *
 * This is data associated with a network interface / NIC / wossname which
 * the filter driver has been or may be attached to. When possible it is
 * attached dynamically, but this may not be possible on all OSes so we have
 * to be flexible about things.
 *
 * A network interface / NIC / wossname can only have one filter driver
 * instance attached to it. So, attempts at connecting an internal network
 * to an interface that's already in use (connected to another internal network)
 * will result in a VERR_SHARING_VIOLATION.
 *
 * Only one internal network can connect to a filter driver instance.
 */
typedef struct VBOXNETFLTINS
{
    /** Pointer to the next interface in the list. (VBOXNETFLTGLOBAL::pInstanceHead) */
    struct VBOXNETFLTINS *pNext;
    /** Our RJ-45 port.
     * This is what the internal network plugs into. */
    INTNETTRUNKIFPORT MyPort;
    /** The RJ-45 port on the INTNET "switch".
     * This is what we're connected to. */
    PINTNETTRUNKSWPORT pSwitchPort;
    /** Pointer to the globals. */
    PVBOXNETFLTGLOBALS pGlobals;

    /** The spinlock protecting the state variables and host interface handle. */
    RTSPINLOCK hSpinlock;
    /** The current interface state. */
    VBOXNETFTLINSSTATE volatile enmState;
    /** The trunk state. */
    INTNETTRUNKIFSTATE volatile enmTrunkState;
    bool volatile fActive;
    /** Disconnected from the host network interface. */
    bool volatile fDisconnectedFromHost;
    /** Rediscovery is pending.
     * cBusy will never reach zero during rediscovery, so which
     * takes care of serializing rediscovery and disconnecting. */
    bool volatile fRediscoveryPending;
    /** Whether we should not attempt to set promiscuous mode at all. */
    bool fDisablePromiscuous;
#if (ARCH_BITS == 32) && defined(__GNUC__)
#if 0
    uint32_t u32Padding;    /**< Alignment padding, will assert in ASMAtomicUoWriteU64 otherwise. */
#endif
#endif
    /** The timestamp of the last rediscovery. */
    uint64_t volatile NanoTSLastRediscovery;
    /** Reference count. */
    uint32_t volatile cRefs;
    /** The busy count.
     * This counts the number of current callers and pending packet. */
    uint32_t volatile cBusy;
    /** The event that is signaled when we go idle and that pfnWaitForIdle blocks on. */
    RTSEMEVENT hEventIdle;

    /** @todo move MacAddr out of this structure!  */
    union
    {
#ifdef VBOXNETFLT_OS_SPECFIC
        struct
        {
# if defined(RT_OS_DARWIN)
            /** @name Darwin instance data.
             * @{ */
            /** Pointer to the darwin network interface we're attached to.
             * This is treated as highly volatile and should only be read and retained
             * while owning hSpinlock. Releasing references to this should not be done
             * while owning it though as we might end up destroying it in some paths. */
            ifnet_t volatile pIfNet;
            /** The interface filter handle.
             * Same access rules as with pIfNet. */
            interface_filter_t volatile pIfFilter;
            /** Whether we've need to set promiscuous mode when the interface comes up. */
            bool volatile fNeedSetPromiscuous;
            /** Whether we've successfully put the interface into to promiscuous mode.
             * This is for dealing with the ENETDOWN case. */
            bool volatile fSetPromiscuous;
            /** The MAC address of the interface. */
            RTMAC MacAddr;
            /** PF_SYSTEM socket to listen for events (XXX: globals?) */
            socket_t pSysSock;
            /** @} */
# elif defined(RT_OS_LINUX)
            /** @name Linux instance data
             * @{ */
            /** Pointer to the device. */
            struct net_device * volatile pDev;
            /** MTU of host's interface. */
            uint32_t cbMtu;
            /** Whether we've successfully put the interface into to promiscuous mode.
             * This is for dealing with the ENETDOWN case. */
            bool volatile fPromiscuousSet;
            /** Whether device exists and physically attached. */
            bool volatile fRegistered;
            /** Whether our packet handler is installed. */
            bool volatile fPacketHandler;
            /** The MAC address of the interface. */
            RTMAC MacAddr;
            struct notifier_block Notifier; /* netdevice */
            struct notifier_block NotifierIPv4;
            struct notifier_block NotifierIPv6;
            struct packet_type    PacketType;
#  ifndef VBOXNETFLT_LINUX_NO_XMIT_QUEUE
            struct sk_buff_head   XmitQueue;
            struct work_struct    XmitTask;
#  endif
            /** Unique identifier of network namespace of device to attach to. */
            uint32_t uNamespaceInode;
            /** @} */
# elif defined(RT_OS_SOLARIS)
            /** @name Solaris instance data.
             * @{ */
#  ifdef VBOX_WITH_NETFLT_CROSSBOW
            /** Whether the underlying interface is a VNIC or not. */
            bool fIsVNIC;
            /** Whether the underlying interface is a VNIC template or not. */
            bool fIsVNICTemplate;
            /** Handle to list of created VNICs. */
            list_t hVNICs;
            /** The MAC address of the host interface. */
            RTMAC MacAddr;
            /** Handle of this interface (lower MAC). */
            mac_handle_t hInterface;
            /** Handle to link state notifier. */
            mac_notify_handle_t hNotify;
#  else
            /** Pointer to the bound IPv4 stream. */
            struct vboxnetflt_stream_t * volatile pIp4Stream;
            /** Pointer to the bound IPv6 stream. */
            struct vboxnetflt_stream_t * volatile pIp6Stream;
            /** Pointer to the bound ARP stream. */
            struct vboxnetflt_stream_t * volatile pArpStream;
            /** Pointer to the unbound promiscuous stream. */
            struct vboxnetflt_promisc_stream_t * volatile pPromiscStream;
            /** Whether we are attaching to IPv6 stream dynamically now. */
            bool volatile fAttaching;
            /** Whether this is a VLAN interface or not. */
            bool volatile fVLAN;
            /** Layered device handle to the interface. */
            ldi_handle_t hIface;
            /** The MAC address of the interface. */
            RTMAC MacAddr;
            /** Mutex protection used for loopback. */
            kmutex_t hMtx;
            /** Mutex protection used for dynamic IPv6 attaches. */
            RTSEMFASTMUTEX hPollMtx;
#  endif
            /** @} */
# elif defined(RT_OS_FREEBSD)
            /** @name FreeBSD instance data.
             * @{ */
            /** Interface handle */
            struct ifnet *ifp;
            /** Netgraph node handle */
            node_p node;
            /** Input hook */
            hook_p input;
            /** Output hook */
            hook_p output;
            /** Original interface flags */
            unsigned int flags;
            /** Input queue */
            struct ifqueue inq;
            /** Output queue */
            struct ifqueue outq;
            /** Input task */
            struct task tskin;
            /** Output task */
            struct task tskout;
            /** The MAC address of the interface. */
            RTMAC MacAddr;
            /** @} */
# elif defined(RT_OS_WINDOWS)
            /** @name Windows instance data.
             * @{ */
            /** Filter driver device context. */
            VBOXNETFLTWIN WinIf;

            volatile uint32_t cModeNetFltRefs;
            volatile uint32_t cModePassThruRefs;
#ifndef VBOXNETFLT_NO_PACKET_QUEUE
            /** Packet worker thread info */
            PACKET_QUEUE_WORKER PacketQueueWorker;
#endif
            /** The MAC address of the interface. Caching MAC for performance reasons. */
            RTMAC MacAddr;
            /** mutex used to synchronize WinIf init/deinit */
            RTSEMMUTEX hWinIfMutex;
            /** @}  */
# else
#  error "PORTME"
# endif
        } s;
#endif
        /** Padding. */
#if defined(RT_OS_WINDOWS)
# if defined(VBOX_NETFLT_ONDEMAND_BIND)
        uint8_t abPadding[192];
# elif defined(VBOXNETADP)
        uint8_t abPadding[256];
# else
        uint8_t abPadding[1024];
# endif
#elif defined(RT_OS_LINUX)
        uint8_t abPadding[320];
#elif defined(RT_OS_FREEBSD)
        uint8_t abPadding[320];
#else
        uint8_t abPadding[128];
#endif
    } u;

    /** The interface name. */
    char szName[1];
} VBOXNETFLTINS;
/** Pointer to the instance data of a host network filter driver. */
typedef struct VBOXNETFLTINS *PVBOXNETFLTINS;

AssertCompileMemberAlignment(VBOXNETFLTINS, NanoTSLastRediscovery, 8);
#ifdef VBOXNETFLT_OS_SPECFIC
AssertCompile(RT_SIZEOFMEMB(VBOXNETFLTINS, u.s) <= RT_SIZEOFMEMB(VBOXNETFLTINS, u.abPadding));
#endif


/**
 * The global data of the VBox filter driver.
 *
 * This contains the bit required for communicating with support driver, VBoxDrv
 * (start out as SupDrv).
 */
typedef struct VBOXNETFLTGLOBALS
{
    /** Mutex protecting the list of instances and state changes. */
    RTSEMFASTMUTEX hFastMtx;
    /** Pointer to a list of instance data. */
    PVBOXNETFLTINS pInstanceHead;

    /** The INTNET trunk network interface factory. */
    INTNETTRUNKFACTORY TrunkFactory;
    /** The SUPDRV component factory registration. */
    SUPDRVFACTORY SupDrvFactory;
    /** The number of current factory references. */
    int32_t volatile cFactoryRefs;
    /** Whether the IDC connection is open or not.
     * This is only for cleaning up correctly after the separate IDC init on Windows. */
    bool fIDCOpen;
    /** The SUPDRV IDC handle (opaque struct). */
    SUPDRVIDCHANDLE SupDrvIDC;
} VBOXNETFLTGLOBALS;


DECLHIDDEN(int) vboxNetFltInitGlobalsAndIdc(PVBOXNETFLTGLOBALS pGlobals);
DECLHIDDEN(int) vboxNetFltInitGlobals(PVBOXNETFLTGLOBALS pGlobals);
DECLHIDDEN(int) vboxNetFltInitIdc(PVBOXNETFLTGLOBALS pGlobals);
DECLHIDDEN(int) vboxNetFltTryDeleteIdcAndGlobals(PVBOXNETFLTGLOBALS pGlobals);
DECLHIDDEN(void) vboxNetFltDeleteGlobals(PVBOXNETFLTGLOBALS pGlobals);
DECLHIDDEN(int) vboxNetFltTryDeleteIdc(PVBOXNETFLTGLOBALS pGlobals);

DECLHIDDEN(bool) vboxNetFltCanUnload(PVBOXNETFLTGLOBALS pGlobals);
DECLHIDDEN(PVBOXNETFLTINS) vboxNetFltFindInstance(PVBOXNETFLTGLOBALS pGlobals, const char *pszName);

DECL_HIDDEN_CALLBACK(void) vboxNetFltPortReleaseBusy(PINTNETTRUNKIFPORT pIfPort);
DECLHIDDEN(void) vboxNetFltRetain(PVBOXNETFLTINS pThis, bool fBusy);
DECLHIDDEN(bool) vboxNetFltTryRetainBusyActive(PVBOXNETFLTINS pThis);
DECLHIDDEN(bool) vboxNetFltTryRetainBusyNotDisconnected(PVBOXNETFLTINS pThis);
DECLHIDDEN(void) vboxNetFltRelease(PVBOXNETFLTINS pThis, bool fBusy);

#ifdef VBOXNETFLT_STATIC_CONFIG
DECLHIDDEN(int) vboxNetFltSearchCreateInstance(PVBOXNETFLTGLOBALS pGlobals, const char *pszName, PVBOXNETFLTINS *ppInstance, void * pContext);
#endif



/** @name The OS specific interface.
 * @{ */
/**
 * Try rediscover the host interface.
 *
 * This is called periodically from the transmit path if we're marked as
 * disconnected from the host. There is no chance of a race here.
 *
 * @returns true if the interface was successfully rediscovered and reattach,
 *          otherwise false.
 * @param   pThis           The new instance.
 */
DECLHIDDEN(bool) vboxNetFltOsMaybeRediscovered(PVBOXNETFLTINS pThis);

/**
 * Transmits a frame.
 *
 * @return  IPRT status code.
 * @param   pThis           The new instance.
 * @param   pvIfData        Pointer to the host-private interface data.
 * @param   pSG             The (scatter/)gather list.
 * @param   fDst            The destination mask. At least one bit will be set.
 *
 * @remarks Owns the out-bound trunk port semaphore.
 */
DECLHIDDEN(int) vboxNetFltPortOsXmit(PVBOXNETFLTINS pThis, void *pvIfData, PINTNETSG pSG, uint32_t fDst);

/**
 * This is called when activating or suspending the instance.
 *
 * Use this method to enable and disable promiscuous mode on
 * the interface to prevent unnecessary interrupt load.
 *
 * It is only called when the state changes.
 *
 * @param   pThis           The instance.
 * @param   fActive         Whether to active (@c true) or deactive.
 *
 * @remarks Owns the lock for the out-bound trunk port.
 */
DECLHIDDEN(void) vboxNetFltPortOsSetActive(PVBOXNETFLTINS pThis, bool fActive);

/**
 * This is called when a network interface has obtained a new MAC address.
 *
 * @param   pThis           The instance.
 * @param   pvIfData        Pointer to the private interface data.
 * @param   pMac            Pointer to the new MAC address.
 */
DECLHIDDEN(void) vboxNetFltPortOsNotifyMacAddress(PVBOXNETFLTINS pThis, void *pvIfData, PCRTMAC pMac);

/**
 * This is called when an interface is connected to the network.
 *
 * @return IPRT status code.
 * @param   pThis           The instance.
 * @param   pvIf            Pointer to the interface.
 * @param   ppvIfData       Where to store the private interface data.
 */
DECLHIDDEN(int) vboxNetFltPortOsConnectInterface(PVBOXNETFLTINS pThis, void *pvIf, void **ppvIfData);

/**
 * This is called when a VM host disconnects from the network.
 *
 * @param   pThis           The instance.
 * @param   pvIfData        Pointer to the private interface data.
 */
DECLHIDDEN(int) vboxNetFltPortOsDisconnectInterface(PVBOXNETFLTINS pThis, void *pvIfData);

/**
 * This is called to when disconnecting from a network.
 *
 * @return  IPRT status code.
 * @param   pThis           The new instance.
 *
 * @remarks May own the semaphores for the global list, the network lock and the out-bound trunk port.
 */
DECLHIDDEN(int) vboxNetFltOsDisconnectIt(PVBOXNETFLTINS pThis);

/**
 * This is called to when connecting to a network.
 *
 * @return  IPRT status code.
 * @param   pThis           The new instance.
 *
 * @remarks Owns the semaphores for the global list, the network lock and the out-bound trunk port.
 */
DECLHIDDEN(int) vboxNetFltOsConnectIt(PVBOXNETFLTINS pThis);

/**
 * Counter part to vboxNetFltOsInitInstance().
 *
 * @param   pThis           The new instance.
 *
 * @remarks May own the semaphores for the global list, the network lock and the out-bound trunk port.
 */
DECLHIDDEN(void) vboxNetFltOsDeleteInstance(PVBOXNETFLTINS pThis);

/**
 * This is called to attach to the actual host interface
 * after linking the instance into the list.
 *
 * The MAC address as well promiscuousness and GSO capabilities should be
 * reported by this function.
 *
 * @return  IPRT status code.
 * @param   pThis           The new instance.
 * @param   pvContext       The user supplied context in the static config only.
 *                          NULL in the dynamic config.
 *
 * @remarks Owns no locks.
 */
DECLHIDDEN(int) vboxNetFltOsInitInstance(PVBOXNETFLTINS pThis, void *pvContext);

/**
 * This is called to perform structure initializations.
 *
 * @return  IPRT status code.
 * @param   pThis           The new instance.
 *
 * @remarks Owns no locks.
 */
DECLHIDDEN(int) vboxNetFltOsPreInitInstance(PVBOXNETFLTINS pThis);
/** @} */

#ifdef RT_OS_WINDOWS
/** The NDIS_STRING_CONST macro doesn't work in /permissive- mode, so "fix" that.
 * Putting it in this header as it's the only common to all users.  */
# define VBOX_NDIS_STRING_CONST(a_sz)  { sizeof(L##a_sz) - sizeof(WCHAR), sizeof(L##a_sz), (PWSTR)L##a_sz }
#endif

RT_C_DECLS_END

#endif /* !VBOX_INCLUDED_SRC_VBoxNetFlt_VBoxNetFltInternal_h */

