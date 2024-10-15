/* $Id: SrvIntNetR0.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Internal networking - The ring 0 service.
 *
 * @remarks No lazy code changes.  If you don't understand exactly what you're
 *          doing, get an understanding or forget it.
 *          All changes shall be reviewed by bird before commit.  If not around,
 *          email and let Frank and/or Klaus OK the changes before committing.
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
#define LOG_GROUP LOG_GROUP_SRV_INTNET
#include <VBox/intnet.h>
#include <VBox/intnetinline.h>
#include <VBox/vmm/pdmnetinline.h>
#include <VBox/sup.h>
#include <VBox/vmm/pdm.h>
#include <VBox/log.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/handletable.h>
#include <iprt/mp.h>
#include <iprt/mem.h>
#include <iprt/net.h>
#include <iprt/semaphore.h>
#include <iprt/spinlock.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/time.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** @def INTNET_WITH_DHCP_SNOOPING
 * Enabled DHCP snooping when in shared-mac-on-the-wire mode. */
#define INTNET_WITH_DHCP_SNOOPING

/** The maximum number of interface in a network. */
#define INTNET_MAX_IFS              (1023 + 1 + 16)

/** The number of entries to grow the destination tables with. */
#if 0
# define INTNET_GROW_DSTTAB_SIZE    16
#else
# define INTNET_GROW_DSTTAB_SIZE    1
#endif

/** The wakeup bit in the INTNETIF::cBusy and INTNETRUNKIF::cBusy counters. */
#define INTNET_BUSY_WAKEUP_MASK     RT_BIT_32(30)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * MAC address lookup table entry.
 */
typedef struct INTNETMACTABENTRY
{
    /** The MAC address of this entry. */
    RTMAC                   MacAddr;
    /** Is it is effectively promiscuous mode. */
    bool                    fPromiscuousEff;
    /** Is it promiscuous and should it see unrelated trunk traffic. */
    bool                    fPromiscuousSeeTrunk;
    /** Is it active.
     * We ignore the entry if this is clear and may end up sending packets addressed
     * to this interface onto the trunk.  The reasoning for this is that this could
     * be the interface of a VM that just has been teleported to a different host. */
    bool                    fActive;
    /** Pointer to the network interface. */
    struct INTNETIF        *pIf;
} INTNETMACTABENTRY;
/** Pointer to a MAC address lookup table entry. */
typedef INTNETMACTABENTRY *PINTNETMACTABENTRY;

/**
 * MAC address lookup table.
 *
 * @todo Having this in a separate structure didn't work out as well as it
 *       should.  Consider merging it into INTNETNETWORK.
 */
typedef struct INTNETMACTAB
{
    /** The current number of entries. */
    uint32_t                cEntries;
    /** The number of entries we've allocated space for. */
    uint32_t                cEntriesAllocated;
    /** Table entries. */
    PINTNETMACTABENTRY      paEntries;

    /** The number of interface entries currently in promicuous mode. */
    uint32_t                cPromiscuousEntries;
    /** The number of interface entries currently in promicuous mode that
     * shall not see unrelated trunk traffic. */
    uint32_t                cPromiscuousNoTrunkEntries;

    /** The host MAC address (reported). */
    RTMAC                   HostMac;
    /** The effective host promiscuous setting (reported). */
    bool                    fHostPromiscuousEff;
    /** The real host promiscuous setting (reported). */
    bool                    fHostPromiscuousReal;
    /** Whether the host is active. */
    bool                    fHostActive;

    /** Whether the wire is promiscuous (config). */
    bool                    fWirePromiscuousEff;
    /** Whether the wire is promiscuous (config).
     * (Shadows INTNET_OPEN_FLAGS_TRUNK_WIRE_PROMISC_MODE in
     * INTNETNETWORK::fFlags.) */
    bool                    fWirePromiscuousReal;
    /** Whether the wire is active. */
    bool                    fWireActive;

    /** Pointer to the trunk interface. */
    struct INTNETTRUNKIF   *pTrunk;
} INTNETMACTAB;
/** Pointer to a MAC address .  */
typedef INTNETMACTAB *PINTNETMACTAB;

/**
 * Destination table.
 */
typedef struct INTNETDSTTAB
{
    /** The trunk destinations. */
    uint32_t                fTrunkDst;
    /** Pointer to the trunk interface (referenced) if fTrunkDst is non-zero. */
    struct INTNETTRUNKIF   *pTrunk;
    /** The number of destination interfaces. */
    uint32_t                cIfs;
    /** The interfaces (referenced).  Variable sized array. */
    struct
    {
        /** The destination interface. */
        struct INTNETIF    *pIf;
        /** Whether to replace the destination MAC address.
         * This is used when sharing MAC address with the host on the wire(less).  */
        bool                fReplaceDstMac;
    }                       aIfs[1];
} INTNETDSTTAB;
/** Pointer to a destination table. */
typedef INTNETDSTTAB *PINTNETDSTTAB;
/** Pointer to a const destination table. */
typedef INTNETDSTTAB const *PCINTNETDSTTAB;

/**
 * Address and type.
 */
typedef struct INTNETADDR
{
    /** The address type. */
    INTNETADDRTYPE          enmType;
    /** The address. */
    RTNETADDRU              Addr;
} INTNETADDR;
/** Pointer to an address. */
typedef INTNETADDR *PINTNETADDR;
/** Pointer to a const address. */
typedef INTNETADDR const *PCINTNETADDR;


/**
 * Address cache for a specific network layer.
 */
typedef struct INTNETADDRCACHE
{
    /** Pointer to the table of addresses. */
    uint8_t                *pbEntries;
    /** The number of valid address entries. */
    uint8_t                 cEntries;
    /** The number of allocated address entries. */
    uint8_t                 cEntriesAlloc;
    /** The address size. */
    uint8_t                 cbAddress;
    /** The size of an entry. */
    uint8_t                 cbEntry;
} INTNETADDRCACHE;
/** Pointer to an address cache. */
typedef INTNETADDRCACHE *PINTNETADDRCACHE;
/** Pointer to a const address cache. */
typedef INTNETADDRCACHE const *PCINTNETADDRCACHE;


/**
 * A network interface.
 *
 * Unless explicitly stated, all members are protect by the network semaphore.
 */
typedef struct INTNETIF
{
    /** The MAC address.
     * This is shadowed by INTNETMACTABENTRY::MacAddr. */
    RTMAC                   MacAddr;
    /** Set if the INTNET::MacAddr member has been explicitly set. */
    bool                    fMacSet;
    /** Tracks the desired promiscuous setting of the interface. */
    bool                    fPromiscuousReal;
    /** Whether the interface is active or not.
     * This is shadowed by INTNETMACTABENTRY::fActive. */
    bool                    fActive;
    /** Whether someone has indicated that the end is nigh by means of IntNetR0IfAbortWait. */
    bool volatile           fNoMoreWaits;
    /** The flags specified when opening this interface. */
    uint32_t                fOpenFlags;
    /** Number of yields done to try make the interface read pending data.
     * We will stop yielding when this reaches a threshold assuming that the VM is
     * paused or that it simply isn't worth all the delay. It is cleared when a
     * successful send has been done. */
    uint32_t                cYields;
    /** Pointer to the current exchange buffer (ring-0). */
    PINTNETBUF              pIntBuf;
    /** Pointer to ring-3 mapping of the current exchange buffer. */
    R3PTRTYPE(PINTNETBUF)   pIntBufR3;
    /** Pointer to the default exchange buffer for the interface. */
    PINTNETBUF              pIntBufDefault;
    /** Pointer to ring-3 mapping of the default exchange buffer. */
    R3PTRTYPE(PINTNETBUF)   pIntBufDefaultR3;
#if !defined(VBOX_WITH_INTNET_SERVICE_IN_R3) || !defined(IN_RING3)
    /** Event semaphore which a receiver/consumer thread will sleep on while
     * waiting for data to arrive. */
    RTSEMEVENT volatile     hRecvEvent;
    /** Number of threads sleeping on the event semaphore. */
    uint32_t volatile       cSleepers;
#else
    /** The callback to call when there is something to receive/consume. */
    PFNINTNETIFRECVAVAIL    pfnRecvAvail;
    /** Opaque user data to pass to the receive avail callback (pfnRecvAvail). */
    void                   *pvUserRecvAvail;
#endif
    /** The interface handle.
     * When this is INTNET_HANDLE_INVALID a sleeper which is waking up
     * should return with the appropriate error condition. */
    INTNETIFHANDLE volatile hIf;
    /** The native handle of the destructor thread.  This is NIL_RTNATIVETHREAD when
     * the object is valid and set when intnetR0IfDestruct is in progress.  This is
     * used to cover an unlikely (impossible?)  race between SUPDRVSESSION cleanup
     * and lingering threads waiting for recv or similar. */
    RTNATIVETHREAD volatile hDestructorThread;
    /** Pointer to the network this interface is connected to.
     * This is protected by the INTNET::hMtxCreateOpenDestroy. */
    struct INTNETNETWORK   *pNetwork;
    /** The session this interface is associated with. */
    PSUPDRVSESSION          pSession;
    /** The SUPR0 object id. */
    void                   *pvObj;
    /** The network layer address cache. (Indexed by type, 0 entry isn't used.)
     * This is protected by the address spinlock of the network. */
    INTNETADDRCACHE         aAddrCache[kIntNetAddrType_End];
    /** Spinlock protecting the input (producer) side of the receive ring. */
    RTSPINLOCK              hRecvInSpinlock;
    /** Busy count for tracking destination table references and active sends.
     * Usually incremented while owning the switch table spinlock.  The 30th bit
     * is used to indicate wakeup. */
    uint32_t volatile       cBusy;
    /** The preallocated destination table.
     * This is NULL when it's in use as a precaution against unserialized
     * transmitting.  This is grown when new interfaces are added to the network. */
    PINTNETDSTTAB volatile  pDstTab;
    /** Pointer to the trunk's per interface data.  Can be NULL. */
    void                   *pvIfData;
    /** Header buffer for when we're carving GSO frames. */
    uint8_t                 abGsoHdrs[256];
} INTNETIF;
/** Pointer to an internal network interface. */
typedef INTNETIF *PINTNETIF;


/**
 * A trunk interface.
 */
typedef struct INTNETTRUNKIF
{
    /** The port interface we present to the component. */
    INTNETTRUNKSWPORT       SwitchPort;
    /** The port interface we get from the component. */
    PINTNETTRUNKIFPORT      pIfPort;
    /** Pointer to the network we're connect to.
     * This may be NULL if we're orphaned? */
    struct INTNETNETWORK   *pNetwork;
    /** The current MAC address for the interface. (reported)
     * Updated while owning the switch table spinlock.  */
    RTMAC                   MacAddr;
    /** Whether to supply physical addresses with the outbound SGs. (reported) */
    bool                    fPhysSG;
    /** Explicit alignment. */
    bool                    fUnused;
    /** Busy count for tracking destination table references and active sends.
     * Usually incremented while owning the switch table spinlock.  The 30th bit
     * is used to indicate wakeup. */
    uint32_t volatile       cBusy;
    /** Mask of destinations that pfnXmit cope with disabled preemption for. */
    uint32_t                fNoPreemptDsts;
    /** The GSO capabilities of the wire destination. (reported) */
    uint32_t                fWireGsoCapabilites;
    /** The GSO capabilities of the host destination. (reported)
     * This is as bit map where each bit represents the GSO type with the same
     * number. */
    uint32_t                fHostGsoCapabilites;
    /** The destination table spinlock, interrupt safe.
     * Protects apTaskDstTabs and apIntDstTabs. */
    RTSPINLOCK              hDstTabSpinlock;
    /** The number of entries in apIntDstTabs. */
    uint32_t                cIntDstTabs;
    /** The task time destination tables.
     * @remarks intnetR0NetworkEnsureTabSpace and others ASSUMES this immediately
     *          precedes apIntDstTabs so that these two tables can be used as one
     *          contiguous one. */
    PINTNETDSTTAB           apTaskDstTabs[2];
    /** The interrupt / disabled-preemption time destination tables.
     * This is a variable sized array.  */
    PINTNETDSTTAB           apIntDstTabs[1];
} INTNETTRUNKIF;
/** Pointer to a trunk interface. */
typedef INTNETTRUNKIF *PINTNETTRUNKIF;

/** Converts a pointer to INTNETTRUNKIF::SwitchPort to a PINTNETTRUNKIF. */
#define INTNET_SWITCHPORT_2_TRUNKIF(pSwitchPort) ((PINTNETTRUNKIF)(pSwitchPort))


/**
 * Internal representation of a network.
 */
typedef struct INTNETNETWORK
{
    /** The Next network in the chain.
     * This is protected by the INTNET::hMtxCreateOpenDestroy. */
    struct INTNETNETWORK   *pNext;

    /** The spinlock protecting MacTab, aAddrBlacklist and INTNETIF::aAddrCache.
     *  Interrupt safe. */
    RTSPINLOCK              hAddrSpinlock;
    /** MAC address table.
     * This doubles as interface collection. */
    INTNETMACTAB            MacTab;

    /** The network layer address cache. (Indexed by type, 0 entry isn't used.
     * Contains host addresses.  We don't let guests spoof them. */
    INTNETADDRCACHE         aAddrBlacklist[kIntNetAddrType_End];

    /** Wait for an interface to stop being busy so it can be removed or have its
     * destination table replaced.  We have to wait upon this while owning the
     * network mutex.  Will only ever have one waiter because of the big mutex. */
    RTSEMEVENT              hEvtBusyIf;
    /** Pointer to the instance data. */
    struct INTNET          *pIntNet;
    /** The SUPR0 object id. */
    void                   *pvObj;
    /** The trunk reconnection system thread. The thread gets started at trunk
     * disconnection. It tries to reconnect the trunk to the bridged filter instance.
     * The thread erases this handle right before it terminates.
     */
    RTTHREAD                hTrunkReconnectThread;
    /** Trunk reconnection thread termination flag. */
    bool volatile           fTerminateReconnectThread;
    /** Pointer to the temporary buffer that is used when snooping fragmented packets.
     * This is allocated after this structure if we're sharing the MAC address with
     * the host. The buffer is INTNETNETWORK_TMP_SIZE big and aligned on a 64-byte boundary. */
    uint8_t                *pbTmp;
    /** Network creation flags (INTNET_OPEN_FLAGS_*). */
    uint32_t                fFlags;
    /** Any restrictive policies required as a minimum by some interface.
     * (INTNET_OPEN_FLAGS_REQUIRE_AS_RESTRICTIVE_POLICIES) */
    uint32_t                fMinFlags;
    /** The number of active interfaces (excluding the trunk). */
    uint32_t                cActiveIFs;
    /** The length of the network name. */
    uint8_t                 cchName;
    /** The network name. */
    char                    szName[INTNET_MAX_NETWORK_NAME];
    /** The trunk type. */
    INTNETTRUNKTYPE         enmTrunkType;
    /** The trunk name. */
    char                    szTrunk[INTNET_MAX_TRUNK_NAME];
} INTNETNETWORK;
/** Pointer to an internal network. */
typedef INTNETNETWORK *PINTNETNETWORK;
/** Pointer to a const internal network. */
typedef const INTNETNETWORK *PCINTNETNETWORK;

/** The size of the buffer INTNETNETWORK::pbTmp points at. */
#define INTNETNETWORK_TMP_SIZE  2048


/**
 * Internal networking instance.
 */
typedef struct INTNET
{
    /** Magic number (INTNET_MAGIC). */
    uint32_t volatile       u32Magic;
    /** Mutex protecting the creation, opening and destruction of both networks and
     * interfaces.  (This means all operations affecting the pNetworks list.) */
    RTSEMMUTEX              hMtxCreateOpenDestroy;
    /** List of networks. Protected by INTNET::Spinlock. */
    PINTNETNETWORK volatile pNetworks;
    /** Handle table for the interfaces. */
    RTHANDLETABLE           hHtIfs;
} INTNET;
/** Pointer to an internal network ring-0 instance. */
typedef struct INTNET *PINTNET;

/** Magic number for the internal network instance data (Hayao Miyazaki). */
#define INTNET_MAGIC        UINT32_C(0x19410105)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Pointer to the internal network instance data. */
static PINTNET volatile g_pIntNet = NULL;

static const struct INTNETOPENNETWORKFLAGS
{
    uint32_t fRestrictive;  /**< The restrictive flag (deny/disabled). */
    uint32_t fRelaxed;      /**< The relaxed flag (allow/enabled). */
    uint32_t fFixed;        /**< The config-fixed flag. */
    uint32_t fPair;         /**< The pair of restrictive and relaxed flags. */
}
/** Open network policy flags relating to the network. */
g_afIntNetOpenNetworkNetFlags[] =
{
    { INTNET_OPEN_FLAGS_ACCESS_RESTRICTED,       INTNET_OPEN_FLAGS_ACCESS_PUBLIC,            INTNET_OPEN_FLAGS_ACCESS_FIXED,  INTNET_OPEN_FLAGS_ACCESS_RESTRICTED       | INTNET_OPEN_FLAGS_ACCESS_PUBLIC            },
    { INTNET_OPEN_FLAGS_PROMISC_DENY_CLIENTS,    INTNET_OPEN_FLAGS_PROMISC_ALLOW_CLIENTS,    INTNET_OPEN_FLAGS_PROMISC_FIXED, INTNET_OPEN_FLAGS_PROMISC_DENY_CLIENTS    | INTNET_OPEN_FLAGS_PROMISC_ALLOW_CLIENTS    },
    { INTNET_OPEN_FLAGS_PROMISC_DENY_TRUNK_HOST, INTNET_OPEN_FLAGS_PROMISC_ALLOW_TRUNK_HOST, INTNET_OPEN_FLAGS_PROMISC_FIXED, INTNET_OPEN_FLAGS_PROMISC_DENY_TRUNK_HOST | INTNET_OPEN_FLAGS_PROMISC_ALLOW_TRUNK_HOST },
    { INTNET_OPEN_FLAGS_PROMISC_DENY_TRUNK_WIRE, INTNET_OPEN_FLAGS_PROMISC_ALLOW_TRUNK_WIRE, INTNET_OPEN_FLAGS_PROMISC_FIXED, INTNET_OPEN_FLAGS_PROMISC_DENY_TRUNK_WIRE | INTNET_OPEN_FLAGS_PROMISC_ALLOW_TRUNK_WIRE },
    { INTNET_OPEN_FLAGS_TRUNK_HOST_DISABLED,     INTNET_OPEN_FLAGS_TRUNK_HOST_ENABLED,       INTNET_OPEN_FLAGS_TRUNK_FIXED,   INTNET_OPEN_FLAGS_TRUNK_HOST_DISABLED     | INTNET_OPEN_FLAGS_TRUNK_HOST_ENABLED       },
    { INTNET_OPEN_FLAGS_TRUNK_HOST_CHASTE_MODE,  INTNET_OPEN_FLAGS_TRUNK_HOST_PROMISC_MODE,  INTNET_OPEN_FLAGS_TRUNK_FIXED,   INTNET_OPEN_FLAGS_TRUNK_HOST_CHASTE_MODE  | INTNET_OPEN_FLAGS_TRUNK_HOST_PROMISC_MODE  },
    { INTNET_OPEN_FLAGS_TRUNK_WIRE_DISABLED,     INTNET_OPEN_FLAGS_TRUNK_WIRE_ENABLED,       INTNET_OPEN_FLAGS_TRUNK_FIXED,   INTNET_OPEN_FLAGS_TRUNK_WIRE_DISABLED     | INTNET_OPEN_FLAGS_TRUNK_WIRE_ENABLED       },
    { INTNET_OPEN_FLAGS_TRUNK_WIRE_CHASTE_MODE,  INTNET_OPEN_FLAGS_TRUNK_WIRE_PROMISC_MODE,  INTNET_OPEN_FLAGS_TRUNK_FIXED,   INTNET_OPEN_FLAGS_TRUNK_WIRE_CHASTE_MODE  | INTNET_OPEN_FLAGS_TRUNK_WIRE_PROMISC_MODE  },
},
/** Open network policy flags relating to the new interface. */
g_afIntNetOpenNetworkIfFlags[] =
{
    { INTNET_OPEN_FLAGS_IF_PROMISC_DENY,        INTNET_OPEN_FLAGS_IF_PROMISC_ALLOW,          INTNET_OPEN_FLAGS_IF_FIXED,      INTNET_OPEN_FLAGS_IF_PROMISC_DENY         | INTNET_OPEN_FLAGS_IF_PROMISC_ALLOW         },
    { INTNET_OPEN_FLAGS_IF_PROMISC_NO_TRUNK,    INTNET_OPEN_FLAGS_IF_PROMISC_SEE_TRUNK,      INTNET_OPEN_FLAGS_IF_FIXED,      INTNET_OPEN_FLAGS_IF_PROMISC_NO_TRUNK     | INTNET_OPEN_FLAGS_IF_PROMISC_SEE_TRUNK     },
};


/*********************************************************************************************************************************
*   Forward Declarations                                                                                                         *
*********************************************************************************************************************************/
static void intnetR0TrunkIfDestroy(PINTNETTRUNKIF pThis, PINTNETNETWORK pNetwork);


/**
 * Checks if a pointer belongs to the list of known networks without
 * accessing memory it points to.
 *
 * @returns true, if such network is in the list.
 * @param   pIntNet     The pointer to the internal network instance (global).
 * @param   pNetwork    The pointer that must be validated.
 */
DECLINLINE(bool) intnetR0NetworkIsValid(PINTNET pIntNet, PINTNETNETWORK pNetwork)
{
    for (PINTNETNETWORK pCurr = pIntNet->pNetworks; pCurr; pCurr = pCurr->pNext)
        if (pCurr == pNetwork)
            return true;
    return false;
}


/**
 * Worker for intnetR0SgWritePart that deals with the case where the
 * request doesn't fit into the first segment.
 *
 * @returns true, unless the request or SG invalid.
 * @param   pSG         The SG list to write to.
 * @param   off         Where to start writing (offset into the SG).
 * @param   cb          How much to write.
 * @param   pvBuf       The buffer to containing the bits to write.
 */
static bool intnetR0SgWritePartSlow(PCINTNETSG pSG, uint32_t off, uint32_t cb, void const *pvBuf)
{
    if (RT_UNLIKELY(off + cb > pSG->cbTotal))
        return false;

    /*
     * Skip ahead to the segment where off starts.
     */
    unsigned const cSegs = pSG->cSegsUsed; Assert(cSegs == pSG->cSegsUsed);
    unsigned iSeg = 0;
    while (off > pSG->aSegs[iSeg].cb)
    {
        off -= pSG->aSegs[iSeg++].cb;
        AssertReturn(iSeg < cSegs, false);
    }

    /*
     * Copy the data, hoping that it's all from one segment...
     */
    uint32_t cbCanCopy = pSG->aSegs[iSeg].cb - off;
    if (cbCanCopy >= cb)
        memcpy((uint8_t *)pSG->aSegs[iSeg].pv + off, pvBuf, cb);
    else
    {
        /* copy the portion in the current segment. */
        memcpy((uint8_t *)pSG->aSegs[iSeg].pv + off, pvBuf, cbCanCopy);
        cb -= cbCanCopy;

        /* copy the portions in the other segments. */
        do
        {
            pvBuf = (uint8_t const *)pvBuf + cbCanCopy;
            iSeg++;
            AssertReturn(iSeg < cSegs, false);

            cbCanCopy = RT_MIN(cb, pSG->aSegs[iSeg].cb);
            memcpy(pSG->aSegs[iSeg].pv, pvBuf, cbCanCopy);

            cb -= cbCanCopy;
        } while (cb > 0);
    }

    return true;
}


/**
 * Writes to a part of an SG.
 *
 * @returns true on success, false on failure (out of bounds).
 * @param   pSG         The SG list to write to.
 * @param   off         Where to start writing (offset into the SG).
 * @param   cb          How much to write.
 * @param   pvBuf       The buffer to containing the bits to write.
 */
DECLINLINE(bool) intnetR0SgWritePart(PCINTNETSG pSG, uint32_t off, uint32_t cb, void const *pvBuf)
{
    Assert(off + cb > off);

    /* The optimized case. */
    if (RT_LIKELY(    pSG->cSegsUsed == 1
                  ||  pSG->aSegs[0].cb >= off + cb))
    {
        Assert(pSG->cbTotal == pSG->aSegs[0].cb);
        memcpy((uint8_t *)pSG->aSegs[0].pv + off, pvBuf, cb);
        return true;
    }
    return intnetR0SgWritePartSlow(pSG, off, cb, pvBuf);
}


/**
 * Reads a byte from a SG list.
 *
 * @returns The byte on success. 0xff on failure.
 * @param   pSG         The SG list to read.
 * @param   off         The offset (into the SG) off the byte.
 */
DECLINLINE(uint8_t) intnetR0SgReadByte(PCINTNETSG pSG, uint32_t off)
{
    if (RT_LIKELY(pSG->aSegs[0].cb > off))
        return ((uint8_t const *)pSG->aSegs[0].pv)[off];

    off -= pSG->aSegs[0].cb;
    unsigned const cSegs = pSG->cSegsUsed; Assert(cSegs == pSG->cSegsUsed);
    for (unsigned iSeg = 1; iSeg < cSegs; iSeg++)
    {
        if (pSG->aSegs[iSeg].cb > off)
            return ((uint8_t const *)pSG->aSegs[iSeg].pv)[off];
        off -= pSG->aSegs[iSeg].cb;
    }
    return false;
}


/**
 * Worker for intnetR0SgReadPart that deals with the case where the
 * requested data isn't in the first segment.
 *
 * @returns true, unless the SG is invalid.
 * @param   pSG         The SG list to read.
 * @param   off         Where to start reading (offset into the SG).
 * @param   cb          How much to read.
 * @param   pvBuf       The buffer to read into.
 */
static bool intnetR0SgReadPartSlow(PCINTNETSG pSG, uint32_t off, uint32_t cb, void *pvBuf)
{
    if (RT_UNLIKELY(off + cb > pSG->cbTotal))
        return false;

    /*
     * Skip ahead to the segment where off starts.
     */
    unsigned const cSegs = pSG->cSegsUsed; Assert(cSegs == pSG->cSegsUsed);
    unsigned iSeg = 0;
    while (off > pSG->aSegs[iSeg].cb)
    {
        off -= pSG->aSegs[iSeg++].cb;
        AssertReturn(iSeg < cSegs, false);
    }

    /*
     * Copy the data, hoping that it's all from one segment...
     */
    uint32_t cbCanCopy = pSG->aSegs[iSeg].cb - off;
    if (cbCanCopy >= cb)
        memcpy(pvBuf, (uint8_t const *)pSG->aSegs[iSeg].pv + off, cb);
    else
    {
        /* copy the portion in the current segment. */
        memcpy(pvBuf, (uint8_t const *)pSG->aSegs[iSeg].pv + off, cbCanCopy);
        cb -= cbCanCopy;

        /* copy the portions in the other segments. */
        do
        {
            pvBuf = (uint8_t *)pvBuf + cbCanCopy;
            iSeg++;
            AssertReturn(iSeg < cSegs, false);

            cbCanCopy = RT_MIN(cb, pSG->aSegs[iSeg].cb);
            memcpy(pvBuf, (uint8_t const *)pSG->aSegs[iSeg].pv, cbCanCopy);

            cb -= cbCanCopy;
        } while (cb > 0);
    }

    return true;
}


/**
 * Reads a part of an SG into a buffer.
 *
 * @returns true on success, false on failure (out of bounds).
 * @param   pSG         The SG list to read.
 * @param   off         Where to start reading (offset into the SG).
 * @param   cb          How much to read.
 * @param   pvBuf       The buffer to read into.
 */
DECLINLINE(bool) intnetR0SgReadPart(PCINTNETSG pSG, uint32_t off, uint32_t cb, void *pvBuf)
{
    Assert(off + cb > off);

    /* The optimized case. */
    if (RT_LIKELY(pSG->aSegs[0].cb >= off + cb))
    {
        AssertMsg(pSG->cbTotal >= pSG->aSegs[0].cb, ("%#x vs %#x\n", pSG->cbTotal, pSG->aSegs[0].cb));
        memcpy(pvBuf, (uint8_t const *)pSG->aSegs[0].pv + off, cb);
        return true;
    }
    return intnetR0SgReadPartSlow(pSG, off, cb, pvBuf);
}


/**
 * Wait for a busy counter to reach zero.
 *
 * @param   pNetwork            The network.
 * @param   pcBusy              The busy counter.
 */
static void intnetR0BusyWait(PINTNETNETWORK pNetwork, uint32_t volatile *pcBusy)
{
    if (ASMAtomicReadU32(pcBusy) == 0)
        return;

    /*
     * We have to be a bit cautious here so we don't destroy the network or the
     * semaphore before intnetR0BusyDec has signalled us.
     */

    /* Reset the semaphore and flip the wakeup bit. */
    RTSemEventWait(pNetwork->hEvtBusyIf, 0); /* clear it */
    uint32_t cCurBusy = ASMAtomicReadU32(pcBusy);
    do
    {
        if (cCurBusy == 0)
            return;
        AssertMsg(!(cCurBusy & INTNET_BUSY_WAKEUP_MASK), ("%#x\n", cCurBusy));
        AssertMsg((cCurBusy & ~INTNET_BUSY_WAKEUP_MASK) < INTNET_MAX_IFS * 3, ("%#x\n", cCurBusy));
    } while (!ASMAtomicCmpXchgExU32(pcBusy, cCurBusy | INTNET_BUSY_WAKEUP_MASK, cCurBusy, &cCurBusy));

    /* Wait for the count to reach zero. */
    do
    {
        int rc2 = RTSemEventWait(pNetwork->hEvtBusyIf, 30000); NOREF(rc2);
        //AssertMsg(RT_SUCCESS(rc2),  ("rc=%Rrc *pcBusy=%#x (%#x)\n", rc2, ASMAtomicReadU32(pcBusy), cCurBusy ));
        cCurBusy = ASMAtomicReadU32(pcBusy);
        AssertMsg((cCurBusy & INTNET_BUSY_WAKEUP_MASK), ("%#x\n", cCurBusy));
        AssertMsg((cCurBusy & ~INTNET_BUSY_WAKEUP_MASK) < INTNET_MAX_IFS * 3, ("%#x\n", cCurBusy));
    } while (   cCurBusy != INTNET_BUSY_WAKEUP_MASK
             || !ASMAtomicCmpXchgU32(pcBusy, 0, INTNET_BUSY_WAKEUP_MASK));
}


/**
 * Decrements the busy counter and maybe wakes up any threads waiting for it to
 * reach zero.
 *
 * @param   pNetwork            The network.
 * @param   pcBusy              The busy counter.
 */
DECLINLINE(void) intnetR0BusyDec(PINTNETNETWORK pNetwork, uint32_t volatile *pcBusy)
{
    uint32_t cNewBusy = ASMAtomicDecU32(pcBusy);
    if (RT_UNLIKELY(   cNewBusy == INTNET_BUSY_WAKEUP_MASK
                    && pNetwork))
        RTSemEventSignal(pNetwork->hEvtBusyIf);
    AssertMsg((cNewBusy & ~INTNET_BUSY_WAKEUP_MASK) < INTNET_MAX_IFS * 3, ("%#x\n", cNewBusy));
}


/**
 * Increments the busy count of the specified interface.
 *
 * The caller must own the MAC address table spinlock.
 *
 * @param   pIf                 The interface.
 */
DECLINLINE(void) intnetR0BusyDecIf(PINTNETIF pIf)
{
    intnetR0BusyDec(pIf->pNetwork, &pIf->cBusy);
}


/**
 * Increments the busy count of the specified interface.
 *
 * The caller must own the MAC address table spinlock or an explicity reference.
 *
 * @param   pTrunk              The trunk.
 */
DECLINLINE(void) intnetR0BusyDecTrunk(PINTNETTRUNKIF pTrunk)
{
    if (pTrunk)
        intnetR0BusyDec(pTrunk->pNetwork, &pTrunk->cBusy);
}


/**
 * Increments the busy count of the specified interface.
 *
 * The caller must own the MAC address table spinlock or an explicity reference.
 *
 * @param   pIf                 The interface.
 */
DECLINLINE(void) intnetR0BusyIncIf(PINTNETIF pIf)
{
    uint32_t cNewBusy = ASMAtomicIncU32(&pIf->cBusy);
    AssertMsg((cNewBusy & ~INTNET_BUSY_WAKEUP_MASK) < INTNET_MAX_IFS * 3, ("%#x\n", cNewBusy));
    NOREF(cNewBusy);
}


/**
 * Increments the busy count of the specified interface.
 *
 * The caller must own the MAC address table spinlock or an explicity reference.
 *
 * @param   pTrunk              The trunk.
 */
DECLINLINE(void) intnetR0BusyIncTrunk(PINTNETTRUNKIF pTrunk)
{
    if (!pTrunk) return;
    uint32_t cNewBusy = ASMAtomicIncU32(&pTrunk->cBusy);
    AssertMsg((cNewBusy & ~INTNET_BUSY_WAKEUP_MASK) < INTNET_MAX_IFS * 3, ("%#x\n", cNewBusy));
    NOREF(cNewBusy);
}


/**
 * Retain an interface.
 *
 * @returns VBox status code, can assume success in most situations.
 * @param   pIf                 The interface instance.
 * @param   pSession            The current session.
 */
DECLINLINE(int) intnetR0IfRetain(PINTNETIF pIf, PSUPDRVSESSION pSession)
{
    Assert(pIf->hDestructorThread == NIL_RTNATIVETHREAD);

    int rc = SUPR0ObjAddRefEx(pIf->pvObj, pSession, true /* fNoBlocking */);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}


/**
 * Release an interface previously retained by intnetR0IfRetain or
 * by handle lookup/freeing.
 *
 * @returns true if destroyed, false if not.
 * @param   pIf                 The interface instance.
 * @param   pSession            The current session.
 */
DECLINLINE(bool) intnetR0IfRelease(PINTNETIF pIf, PSUPDRVSESSION pSession)
{
    Assert(pIf->hDestructorThread == NIL_RTNATIVETHREAD);

    int rc = SUPR0ObjRelease(pIf->pvObj, pSession);
    AssertRC(rc);

    return rc == VINF_OBJECT_DESTROYED;
}


/**
 * RTHandleCreateEx callback that retains an object in the
 * handle table before returning it.
 *
 * (Avoids racing the freeing of the handle.)
 *
 * @returns VBox status code.
 * @param   hHandleTable        The handle table (ignored).
 * @param   pvObj               The object (INTNETIF).
 * @param   pvCtx               The context (SUPDRVSESSION).
 * @param   pvUser              The user context (ignored).
 */
static DECLCALLBACK(int) intnetR0IfRetainHandle(RTHANDLETABLE hHandleTable, void *pvObj, void *pvCtx, void *pvUser)
{
    NOREF(pvUser);
    NOREF(hHandleTable);

    PINTNETIF      pIf = (PINTNETIF)pvObj;
    RTNATIVETHREAD hDtorThrd;
    ASMAtomicUoReadHandle(&pIf->hDestructorThread, &hDtorThrd);
    if (hDtorThrd == NIL_RTNATIVETHREAD)
        return intnetR0IfRetain(pIf, (PSUPDRVSESSION)pvCtx);

    /* Allow intnetR0IfDestruct to call RTHandleTableFreeWithCtx to free
       the handle, but not even think about retaining a referenceas we don't
       want to confuse SUPDrv and risk having the destructor called twice. */
    if (hDtorThrd == RTThreadNativeSelf())
        return VINF_SUCCESS;

    return VERR_SEM_DESTROYED;
}



/**
 * Checks if the interface has a usable MAC address or not.
 *
 * @returns true if MacAddr is usable, false if not.
 * @param   pIf                 The interface.
 */
DECL_FORCE_INLINE(bool) intnetR0IfHasMacAddr(PINTNETIF pIf)
{
    return pIf->fMacSet || !(pIf->MacAddr.au8[0] & 1);
}


/**
 * Locates the MAC address table entry for the given interface.
 *
 * The caller holds the MAC address table spinlock, obviously.
 *
 * @returns Pointer to the entry on if found, NULL if not.
 * @param   pNetwork        The network.
 * @param   pIf             The interface.
 */
DECLINLINE(PINTNETMACTABENTRY) intnetR0NetworkFindMacAddrEntry(PINTNETNETWORK pNetwork, PINTNETIF pIf)
{
    uint32_t iIf = pNetwork->MacTab.cEntries;
    while (iIf-- > 0)
    {
        if (pNetwork->MacTab.paEntries[iIf].pIf == pIf)
            return &pNetwork->MacTab.paEntries[iIf];
    }
    return NULL;
}


/**
 * Checks if the IPv6 address is a good interface address.
 * @returns true/false.
 * @param   addr        The address, network endian.
 */
DECLINLINE(bool) intnetR0IPv6AddrIsGood(RTNETADDRIPV6 addr)
{
    return  !(   (   addr.QWords.qw0 == 0 && addr.QWords.qw1 == 0)                       /* :: */
              || (  (addr.Words.w0 & RT_H2BE_U16(0xff00)) == RT_H2BE_U16(0xff00)) /* multicast */
              || (   addr.Words.w0 == 0 && addr.Words.w1 == 0
                  && addr.Words.w2 == 0 && addr.Words.w3 == 0
                  && addr.Words.w4 == 0 && addr.Words.w5 == 0
                  && addr.Words.w6 == 0 && addr.Words.w7 == RT_H2BE_U16(0x0001)));      /* ::1 */
}


#if 0 /* unused */
/**
 * Checks if the IPv4 address is a broadcast address.
 * @returns true/false.
 * @param   Addr        The address, network endian.
 */
DECLINLINE(bool) intnetR0IPv4AddrIsBroadcast(RTNETADDRIPV4 Addr)
{
    /* Just check for 255.255.255.255 atm. */
    return Addr.u == UINT32_MAX;
}
#endif /* unused */


/**
 * Checks if the IPv4 address is a good interface address.
 * @returns true/false.
 * @param   Addr        The address, network endian.
 */
DECLINLINE(bool) intnetR0IPv4AddrIsGood(RTNETADDRIPV4 Addr)
{
    /* Usual suspects. */
    if (    Addr.u == UINT32_MAX    /* 255.255.255.255 - broadcast. */
        ||  Addr.au8[0] == 0)       /* Current network, can be used as source address. */
        return false;

    /* Unusual suspects. */
    if (RT_UNLIKELY(     Addr.au8[0]         == 127  /* Loopback */
                    ||  (Addr.au8[0] & 0xf0) == 224 /* Multicast */
                    ))
        return false;
    return true;
}


/**
 * Gets the address size of a network layer type.
 *
 * @returns size in bytes.
 * @param   enmType             The type.
 */
DECLINLINE(uint8_t) intnetR0AddrSize(INTNETADDRTYPE enmType)
{
    switch (enmType)
    {
        case kIntNetAddrType_IPv4:  return 4;
        case kIntNetAddrType_IPv6:  return 16;
        case kIntNetAddrType_IPX:   return 4 + 6;
        default:                    AssertFailedReturn(0);
    }
}


/**
 * Compares two address to see if they are equal, assuming naturally align structures.
 *
 * @returns true if equal, false if not.
 * @param   pAddr1          The first address.
 * @param   pAddr2          The second address.
 * @param   cbAddr          The address size.
 */
DECLINLINE(bool) intnetR0AddrUIsEqualEx(PCRTNETADDRU pAddr1, PCRTNETADDRU pAddr2, uint8_t const cbAddr)
{
    switch (cbAddr)
    {
        case 4:  /* IPv4 */
            return pAddr1->au32[0] == pAddr2->au32[0];
        case 16: /* IPv6 */
            return pAddr1->au64[0] == pAddr2->au64[0]
                && pAddr1->au64[1] == pAddr2->au64[1];
        case 10: /* IPX */
            return pAddr1->au64[0] == pAddr2->au64[0]
                && pAddr1->au16[4] == pAddr2->au16[4];
        default:
            AssertFailedReturn(false);
    }
}


/**
 * Worker for intnetR0IfAddrCacheLookup that performs the lookup
 * in the remaining cache entries after the caller has check the
 * most likely ones.
 *
 * @returns -1 if not found, the index of the cache entry if found.
 * @param   pCache      The cache.
 * @param   pAddr       The address.
 * @param   cbAddr      The address size (optimization).
 */
static int intnetR0IfAddrCacheLookupSlow(PCINTNETADDRCACHE pCache, PCRTNETADDRU pAddr, uint8_t const cbAddr)
{
    unsigned i = pCache->cEntries - 2;
    uint8_t const *pbEntry = pCache->pbEntries + pCache->cbEntry * i;
    while (i >= 1)
    {
        if (intnetR0AddrUIsEqualEx((PCRTNETADDRU)pbEntry, pAddr, cbAddr))
            return i;
        pbEntry -= pCache->cbEntry;
        i--;
    }

    return -1;
}

/**
 * Lookup an address in a cache without any expectations.
 *
 * @returns -1 if not found, the index of the cache entry if found.
 * @param   pCache          The cache.
 * @param   pAddr           The address.
 * @param   cbAddr          The address size (optimization).
 */
DECLINLINE(int) intnetR0IfAddrCacheLookup(PCINTNETADDRCACHE pCache, PCRTNETADDRU pAddr, uint8_t const cbAddr)
{
    Assert(pCache->cbAddress == cbAddr);

    /*
     * The optimized case is when there is one cache entry and
     * it doesn't match.
     */
    unsigned i = pCache->cEntries;
    if (    i > 0
        &&  intnetR0AddrUIsEqualEx((PCRTNETADDRU)pCache->pbEntries, pAddr, cbAddr))
        return 0;
    if (i <= 1)
        return -1;

    /*
     * Check the last entry.
     */
    i--;
    if (intnetR0AddrUIsEqualEx((PCRTNETADDRU)(pCache->pbEntries + pCache->cbEntry * i), pAddr, cbAddr))
        return i;
    if (i <= 1)
        return -1;

    return intnetR0IfAddrCacheLookupSlow(pCache, pAddr, cbAddr);
}


/** Same as intnetR0IfAddrCacheLookup except we expect the address to be present already. */
DECLINLINE(int) intnetR0IfAddrCacheLookupLikely(PCINTNETADDRCACHE pCache, PCRTNETADDRU pAddr, uint8_t const cbAddr)
{
    /** @todo implement this. */
    return intnetR0IfAddrCacheLookup(pCache, pAddr, cbAddr);
}

#if 0 /* unused */

/**
 * Worker for intnetR0IfAddrCacheLookupUnlikely that performs
 * the lookup in the remaining cache entries after the caller
 * has check the most likely ones.
 *
 * The routine is expecting not to find the address.
 *
 * @returns -1 if not found, the index of the cache entry if found.
 * @param   pCache      The cache.
 * @param   pAddr       The address.
 * @param   cbAddr      The address size (optimization).
 */
static int intnetR0IfAddrCacheInCacheUnlikelySlow(PCINTNETADDRCACHE pCache, PCRTNETADDRU pAddr, uint8_t const cbAddr)
{
    /*
     * Perform a full table lookup.
     */
    unsigned i = pCache->cEntries - 2;
    uint8_t const *pbEntry = pCache->pbEntries + pCache->cbEntry * i;
    while (i >= 1)
    {
        if (RT_UNLIKELY(intnetR0AddrUIsEqualEx((PCRTNETADDRU)pbEntry, pAddr, cbAddr)))
            return i;
        pbEntry -= pCache->cbEntry;
        i--;
    }

    return -1;
}


/**
 * Lookup an address in a cache expecting not to find it.
 *
 * @returns -1 if not found, the index of the cache entry if found.
 * @param   pCache          The cache.
 * @param   pAddr           The address.
 * @param   cbAddr          The address size (optimization).
 */
DECLINLINE(int) intnetR0IfAddrCacheLookupUnlikely(PCINTNETADDRCACHE pCache, PCRTNETADDRU pAddr, uint8_t const cbAddr)
{
    Assert(pCache->cbAddress == cbAddr);

    /*
     * The optimized case is when there is one cache entry and
     * it doesn't match.
     */
    unsigned i = pCache->cEntries;
    if (RT_UNLIKELY(   i > 0
                    && intnetR0AddrUIsEqualEx((PCRTNETADDRU)pCache->pbEntries, pAddr, cbAddr)))
        return 0;
    if (RT_LIKELY(i <= 1))
        return -1;

    /*
     * Then check the last entry and return if there are just two cache entries.
     */
    i--;
    if (RT_UNLIKELY(intnetR0AddrUIsEqualEx((PCRTNETADDRU)(pCache->pbEntries + pCache->cbEntry * i), pAddr, cbAddr)))
        return i;
    if (i <= 1)
        return -1;

    return intnetR0IfAddrCacheInCacheUnlikelySlow(pCache, pAddr, cbAddr);
}

#endif /* unused */


/**
 * Deletes a specific cache entry.
 *
 * Worker for intnetR0NetworkAddrCacheDelete and intnetR0NetworkAddrCacheDeleteMinusIf.
 *
 * @param   pIf             The interface (for logging).
 * @param   pCache          The cache.
 * @param   iEntry          The entry to delete.
 * @param   pszMsg          Log message.
 */
static void intnetR0IfAddrCacheDeleteIt(PINTNETIF pIf, PINTNETADDRCACHE pCache, int iEntry, const char *pszMsg)
{
    AssertReturnVoid(iEntry < pCache->cEntries);
    AssertReturnVoid(iEntry >= 0);
#ifdef LOG_ENABLED
    INTNETADDRTYPE enmAddrType = (INTNETADDRTYPE)(uintptr_t)(pCache - &pIf->aAddrCache[0]);
    PCRTNETADDRU pAddr = (PCRTNETADDRU)(pCache->pbEntries + iEntry * pCache->cbEntry);
    switch (enmAddrType)
    {
        case kIntNetAddrType_IPv4:
            Log(("intnetR0IfAddrCacheDeleteIt: hIf=%#x MAC=%.6Rhxs IPv4 deleted #%d  %RTnaipv4 %s\n",
                 pIf->hIf, &pIf->MacAddr, iEntry, pAddr->IPv4, pszMsg));
            break;
        case kIntNetAddrType_IPv6:
            Log(("intnetR0IfAddrCacheDeleteIt: hIf=%#x MAC=%.6Rhxs IPv6 deleted #%d %RTnaipv6 %s\n",
                pIf->hIf, &pIf->MacAddr, iEntry, &pAddr->IPv6, pszMsg));
            break;
        default:
            Log(("intnetR0IfAddrCacheDeleteIt: hIf=%RX32 MAC=%.6Rhxs type=%d #%d %.*Rhxs %s\n",
                 pIf->hIf, &pIf->MacAddr, enmAddrType, iEntry, pCache->cbAddress, pAddr, pszMsg));
            break;
    }
#else
    RT_NOREF2(pIf, pszMsg);
#endif

    pCache->cEntries--;
    if (iEntry < pCache->cEntries)
        memmove(pCache->pbEntries +      iEntry  * pCache->cbEntry,
                pCache->pbEntries + (iEntry + 1) * pCache->cbEntry,
                (pCache->cEntries - iEntry)      * pCache->cbEntry);
}


/**
 * Deletes an address from the cache, assuming it isn't actually in the cache.
 *
 * May or may not own the spinlock when calling this.
 *
 * @param   pIf             The interface (for logging).
 * @param   pCache          The cache.
 * @param   pAddr           The address.
 * @param   cbAddr          The address size (optimization).
 */
DECLINLINE(void) intnetR0IfAddrCacheDelete(PINTNETIF pIf, PINTNETADDRCACHE pCache, PCRTNETADDRU pAddr, uint8_t const cbAddr, const char *pszMsg)
{
    int i = intnetR0IfAddrCacheLookup(pCache, pAddr, cbAddr);
    if (RT_UNLIKELY(i >= 0))
        intnetR0IfAddrCacheDeleteIt(pIf, pCache, i, pszMsg);
}


/**
 * Deletes the address from all the interface caches.
 *
 * This is used to remove stale entries that has been reassigned to
 * other machines on the network.
 *
 * @param   pNetwork        The network.
 * @param   pAddr           The address.
 * @param   enmType         The address type.
 * @param   cbAddr          The address size (optimization).
 * @param   pszMsg          Log message.
 */
DECLINLINE(void) intnetR0NetworkAddrCacheDeleteLocked(PINTNETNETWORK pNetwork,
                                                      PCRTNETADDRU pAddr, INTNETADDRTYPE enmType,
                                                      uint8_t const cbAddr,
                                                      const char *pszMsg)
{
    uint32_t iIf = pNetwork->MacTab.cEntries;
    while (iIf--)
    {
        PINTNETIF pIf = pNetwork->MacTab.paEntries[iIf].pIf;

        int i = intnetR0IfAddrCacheLookup(&pIf->aAddrCache[enmType], pAddr, cbAddr);
        if (RT_UNLIKELY(i >= 0))
            intnetR0IfAddrCacheDeleteIt(pIf, &pIf->aAddrCache[enmType], i, pszMsg);
    }
}


/**
 * Deletes the address from all the interface caches.
 *
 * This is used to remove stale entries that has been reassigned to
 * other machines on the network.
 *
 * @param   pNetwork        The network.
 * @param   pAddr           The address.
 * @param   enmType         The address type.
 * @param   cbAddr          The address size (optimization).
 * @param   pszMsg          Log message.
 */
DECLINLINE(void) intnetR0NetworkAddrCacheDelete(PINTNETNETWORK pNetwork, PCRTNETADDRU pAddr, INTNETADDRTYPE const enmType,
                                                uint8_t const cbAddr, const char *pszMsg)
{
    RTSpinlockAcquire(pNetwork->hAddrSpinlock);

    intnetR0NetworkAddrCacheDeleteLocked(pNetwork, pAddr, enmType, cbAddr, pszMsg);

    RTSpinlockRelease(pNetwork->hAddrSpinlock);
}


#if 0 /* unused */
/**
 * Deletes the address from all the interface caches except the specified one.
 *
 * This is used to remove stale entries that has been reassigned to
 * other machines on the network.
 *
 * @param   pNetwork        The network.
 * @param   pAddr           The address.
 * @param   enmType         The address type.
 * @param   cbAddr          The address size (optimization).
 * @param   pszMsg          Log message.
 */
DECLINLINE(void) intnetR0NetworkAddrCacheDeleteMinusIf(PINTNETNETWORK pNetwork, PINTNETIF pIfSender, PCRTNETADDRU pAddr,
                                                       INTNETADDRTYPE const enmType, uint8_t const cbAddr, const char *pszMsg)
{
    RTSpinlockAcquire(pNetwork->hAddrSpinlock);

    uint32_t iIf = pNetwork->MacTab.cEntries;
    while (iIf--)
    {
        PINTNETIF pIf = pNetwork->MacTab.paEntries[iIf].pIf;
        if (pIf != pIfSender)
        {
            int i = intnetR0IfAddrCacheLookup(&pIf->aAddrCache[enmType], pAddr, cbAddr);
            if (RT_UNLIKELY(i >= 0))
                intnetR0IfAddrCacheDeleteIt(pIf, &pIf->aAddrCache[enmType], i, pszMsg);
        }
    }

    RTSpinlockRelease(pNetwork->hAddrSpinlock);
}
#endif /* unused */


/**
 * Lookup an address on the network, returning the (first) interface having it
 * in its address cache.
 *
 * @returns Pointer to the interface on success, NULL if not found.  The caller
 *          must release the interface by calling intnetR0BusyDecIf.
 * @param   pNetwork        The network.
 * @param   pAddr           The address to lookup.
 * @param   enmType         The address type.
 * @param   cbAddr          The size of the address.
 */
DECLINLINE(PINTNETIF) intnetR0NetworkAddrCacheLookupIf(PINTNETNETWORK pNetwork, PCRTNETADDRU pAddr, INTNETADDRTYPE const enmType, uint8_t const cbAddr)
{
    RTSpinlockAcquire(pNetwork->hAddrSpinlock);

    uint32_t iIf = pNetwork->MacTab.cEntries;
    while (iIf--)
    {
        PINTNETIF pIf = pNetwork->MacTab.paEntries[iIf].pIf;
        int i = intnetR0IfAddrCacheLookup(&pIf->aAddrCache[enmType], pAddr, cbAddr);
        if (i >= 0)
        {
            intnetR0BusyIncIf(pIf);
            RTSpinlockRelease(pNetwork->hAddrSpinlock);
            return pIf;
        }
    }

    RTSpinlockRelease(pNetwork->hAddrSpinlock);
    return NULL;
}


/**
 * Look up specified address in the network's blacklist.
 *
 * @param pNetwork      The network.
 * @param enmType       The address type.
 * @param pAddr         The address.
 */
static bool intnetR0NetworkBlacklistLookup(PINTNETNETWORK pNetwork,
                                           PCRTNETADDRU pAddr, INTNETADDRTYPE enmType)
{
    PINTNETADDRCACHE pCache = &pNetwork->aAddrBlacklist[enmType];

    if (RT_UNLIKELY(pCache->cEntriesAlloc == 0))
        return false;

    const uint8_t cbAddr = pCache->cbAddress;
    Assert(cbAddr == intnetR0AddrSize(enmType));

    for (unsigned i = 0; i < pCache->cEntries; ++i)
    {
        uint8_t *pbEntry = pCache->pbEntries + pCache->cbEntry * i;
        if (intnetR0AddrUIsEqualEx((PCRTNETADDRU)pbEntry, pAddr, cbAddr))
            return true;
    }

    return false;
}


/**
 * Deletes specified address from network's blacklist.
 *
 * @param pNetwork      The network.
 * @param enmType       The address type.
 * @param pAddr         The address.
 */
static void intnetR0NetworkBlacklistDelete(PINTNETNETWORK pNetwork,
                                           PCRTNETADDRU pAddr, INTNETADDRTYPE enmType)
{
    PINTNETADDRCACHE pCache = &pNetwork->aAddrBlacklist[enmType];

    if (RT_UNLIKELY(pCache->cEntriesAlloc == 0))
        return;

    const uint8_t cbAddr = pCache->cbAddress;
    Assert(cbAddr == intnetR0AddrSize(enmType));

    for (unsigned i = 0; i < pCache->cEntries; ++i)
    {
        uint8_t *pbEntry = pCache->pbEntries + pCache->cbEntry * i;
        if (!intnetR0AddrUIsEqualEx((PCRTNETADDRU)pbEntry, pAddr, cbAddr))
            continue;

        --pCache->cEntries;
        memmove(pCache->pbEntries + i * pCache->cbEntry,
                pCache->pbEntries + (i + 1) * pCache->cbEntry,
                (pCache->cEntries - i) * pCache->cbEntry);
        return;
    }
}


/**
 * Adds specified address from network's blacklist.
 *
 * @param pNetwork      The network.
 * @param enmType       The address type.
 * @param pAddr         The address.
 */
static void intnetR0NetworkBlacklistAdd(PINTNETNETWORK pNetwork,
                                        PCRTNETADDRU pAddr, INTNETADDRTYPE enmType)
{
    PINTNETADDRCACHE pCache = &pNetwork->aAddrBlacklist[enmType];

    if (RT_UNLIKELY(pCache->cEntriesAlloc == 0))
        return;

    const uint8_t cbAddr = pCache->cbAddress;
    Assert(cbAddr == intnetR0AddrSize(enmType));

    /* lookup */
    for (unsigned i = 0; i < pCache->cEntries; ++i)
    {
        uint8_t *pbEntry = pCache->pbEntries + pCache->cbEntry * i;
        if (RT_UNLIKELY(intnetR0AddrUIsEqualEx((PCRTNETADDRU)pbEntry, pAddr, cbAddr)))
            return; /* already exists */
    }

    if (pCache->cEntries >= pCache->cEntriesAlloc)
    {
        /* shift */
        memmove(pCache->pbEntries, pCache->pbEntries + pCache->cbEntry,
                pCache->cbEntry * (pCache->cEntries - 1));
        --pCache->cEntries;
    }

    Assert(pCache->cEntries < pCache->cEntriesAlloc);

    /* push */
    uint8_t *pbEntry = pCache->pbEntries + pCache->cEntries * pCache->cbEntry;
    memcpy(pbEntry, pAddr, cbAddr);
    memset(pbEntry + pCache->cbAddress, '\0', pCache->cbEntry - cbAddr);
    ++pCache->cEntries;

    Assert(pCache->cEntries <= pCache->cEntriesAlloc);
}


/**
 * Adds an address to the cache, the caller is responsible for making sure it's
 * not already in the cache.
 *
 * The caller must not
 *
 * @param   pIf         The interface (for logging).
 * @param   pCache      The address cache.
 * @param   pAddr       The address.
 * @param   pszMsg      log message.
 */
static void intnetR0IfAddrCacheAddIt(PINTNETIF pIf, INTNETADDRTYPE enmAddrType, PCRTNETADDRU pAddr,
                                     const char *pszMsg)
{
    PINTNETNETWORK  pNetwork = pIf->pNetwork;
    AssertReturnVoid(pNetwork);

    PINTNETADDRCACHE pCache = &pIf->aAddrCache[enmAddrType];

#if defined(LOG_ENABLED) || defined(VBOX_STRICT)
    const uint8_t cbAddr = pCache->cbAddress;
    Assert(cbAddr == intnetR0AddrSize(enmAddrType));
#endif

    RTSpinlockAcquire(pNetwork->hAddrSpinlock);

    bool fBlacklisted = intnetR0NetworkBlacklistLookup(pNetwork, pAddr, enmAddrType);
    if (fBlacklisted)
    {
        RTSpinlockRelease(pNetwork->hAddrSpinlock);

#ifdef LOG_ENABLED
        switch (enmAddrType)
        {
            case kIntNetAddrType_IPv4:
                Log(("%s: spoofing attempt for %RTnaipv4\n",
                     __FUNCTION__, pAddr->IPv4));
                break;
            case kIntNetAddrType_IPv6:
                Log(("%s: spoofing attempt for %RTnaipv6\n",
                     __FUNCTION__, &pAddr->IPv6));
                break;
            default:
                Log(("%s: spoofing attempt for %.*Rhxs (type %d)\n",
                     __FUNCTION__, cbAddr, pAddr, enmAddrType));
                break;
        }
#endif
        return;
    }

    if (RT_UNLIKELY(!pCache->cEntriesAlloc))
    {
        /* This shouldn't happen*/
        RTSpinlockRelease(pNetwork->hAddrSpinlock);
        return;
    }

    /* When the table is full, drop the older entry (FIFO). Do proper ageing? */
    if (pCache->cEntries >= pCache->cEntriesAlloc)
    {
        Log(("intnetR0IfAddrCacheAddIt: type=%d replacing %.*Rhxs\n",
             (int)(uintptr_t)(pCache - &pIf->aAddrCache[0]), pCache->cbAddress, pCache->pbEntries));
        memmove(pCache->pbEntries, pCache->pbEntries + pCache->cbEntry, pCache->cbEntry * (pCache->cEntries - 1));
        pCache->cEntries--;
        Assert(pCache->cEntries < pCache->cEntriesAlloc);
    }

    /*
     * Add the new entry to the end of the array.
     */
    uint8_t *pbEntry = pCache->pbEntries + pCache->cEntries * pCache->cbEntry;
    memcpy(pbEntry, pAddr, pCache->cbAddress);
    memset(pbEntry + pCache->cbAddress, '\0', pCache->cbEntry - pCache->cbAddress);

#ifdef LOG_ENABLED
    switch (enmAddrType)
    {
        case kIntNetAddrType_IPv4:
            Log(("intnetR0IfAddrCacheAddIt: hIf=%#x MAC=%.6Rhxs IPv4 added #%d %RTnaipv4 %s\n",
                 pIf->hIf, &pIf->MacAddr, pCache->cEntries, pAddr->IPv4, pszMsg));
            break;
        case kIntNetAddrType_IPv6:
            Log(("intnetR0IfAddrCacheAddIt: hIf=%#x MAC=%.6Rhxs IPv6 added #%d %RTnaipv6 %s\n",
                 pIf->hIf, &pIf->MacAddr, pCache->cEntries, &pAddr->IPv6, pszMsg));
            break;
        default:
            Log(("intnetR0IfAddrCacheAddIt: hIf=%#x MAC=%.6Rhxs type=%d added #%d %.*Rhxs %s\n",
                 pIf->hIf, &pIf->MacAddr, enmAddrType, pCache->cEntries, pCache->cbAddress, pAddr, pszMsg));
            break;
    }
#else
    RT_NOREF1(pszMsg);
#endif
    pCache->cEntries++;
    Assert(pCache->cEntries <= pCache->cEntriesAlloc);

    RTSpinlockRelease(pNetwork->hAddrSpinlock);
}


/**
 * A intnetR0IfAddrCacheAdd worker that performs the rest of the lookup.
 *
 * @param   pIf         The interface (for logging).
 * @param   pCache      The address cache.
 * @param   pAddr       The address.
 * @param   cbAddr      The size of the address (optimization).
 * @param   pszMsg      Log message.
 */
static void intnetR0IfAddrCacheAddSlow(PINTNETIF pIf, INTNETADDRTYPE enmAddrType, PCRTNETADDRU pAddr,
                                       const char *pszMsg)
{
    PINTNETADDRCACHE pCache = &pIf->aAddrCache[enmAddrType];

    const uint8_t cbAddr = pCache->cbAddress;
    Assert(cbAddr == intnetR0AddrSize(enmAddrType));

    /*
     * Check all but the first and last entries, the caller
     * has already checked those.
     */
    int i = pCache->cEntries - 2;
    uint8_t const *pbEntry = pCache->pbEntries + pCache->cbEntry;
    while (i >= 1)
    {
        if (RT_LIKELY(intnetR0AddrUIsEqualEx((PCRTNETADDRU)pbEntry, pAddr, cbAddr)))
            return;
        pbEntry += pCache->cbEntry;
        i--;
    }

    /*
     * Not found, add it.
     */
    intnetR0IfAddrCacheAddIt(pIf, enmAddrType, pAddr, pszMsg);
}


/**
 * Adds an address to the cache if it's not already there.
 *
 * Must not own any spinlocks when calling this function.
 *
 * @param   pIf         The interface (for logging).
 * @param   pCache      The address cache.
 * @param   pAddr       The address.
 * @param   cbAddr      The size of the address (optimization).
 * @param   pszMsg      Log message.
 */
DECLINLINE(void) intnetR0IfAddrCacheAdd(PINTNETIF pIf, INTNETADDRTYPE enmAddrType, PCRTNETADDRU pAddr,
                                        const char *pszMsg)
{
    PINTNETADDRCACHE pCache = &pIf->aAddrCache[enmAddrType];

    const uint8_t cbAddr = pCache->cbAddress;
    Assert(cbAddr == intnetR0AddrSize(enmAddrType));

    /*
     * The optimized case is when the address the first or last cache entry.
     */
    unsigned i = pCache->cEntries;
    if (RT_LIKELY(   i > 0
                  && (   intnetR0AddrUIsEqualEx((PCRTNETADDRU)pCache->pbEntries, pAddr, cbAddr)
                      || (i > 1
                          && intnetR0AddrUIsEqualEx((PCRTNETADDRU)(pCache->pbEntries + pCache->cbEntry * (i-1)), pAddr, cbAddr))) ))
        return;

    intnetR0IfAddrCacheAddSlow(pIf, enmAddrType, pAddr, pszMsg);
}


/**
 * Destroys the specified address cache.
 * @param   pCache              The address cache.
 */
static void intnetR0IfAddrCacheDestroy(PINTNETADDRCACHE pCache)
{
    void *pvFree = pCache->pbEntries;
    pCache->pbEntries     = NULL;
    pCache->cEntries      = 0;
    pCache->cEntriesAlloc = 0;
    RTMemFree(pvFree);
}


/**
 * Initialize the address cache for the specified address type.
 *
 * The cache storage is preallocated and fixed size so that we can handle
 * inserts from problematic contexts.
 *
 * @returns VINF_SUCCESS or VERR_NO_MEMORY.
 * @param   pCache              The cache to initialize.
 * @param   enmAddrType         The address type.
 * @param   fEnabled            Whether the address cache is enabled or not.
 */
static int intnetR0IfAddrCacheInit(PINTNETADDRCACHE pCache, INTNETADDRTYPE enmAddrType, bool fEnabled)
{
    pCache->cEntries  = 0;
    pCache->cbAddress = intnetR0AddrSize(enmAddrType);
    pCache->cbEntry   = RT_ALIGN(pCache->cbAddress, 4);
    if (fEnabled)
    {
        pCache->cEntriesAlloc = 32;
        pCache->pbEntries     = (uint8_t *)RTMemAllocZ(pCache->cEntriesAlloc * pCache->cbEntry);
        if (!pCache->pbEntries)
            return VERR_NO_MEMORY;
    }
    else
    {
        pCache->cEntriesAlloc = 0;
        pCache->pbEntries     = NULL;
    }
    return VINF_SUCCESS;
}


/**
 * Is it a multicast or broadcast MAC address?
 *
 * @returns true if multicast, false if not.
 * @param   pMacAddr            The address to inspect.
 */
DECL_FORCE_INLINE(bool) intnetR0IsMacAddrMulticast(PCRTMAC pMacAddr)
{
    return !!(pMacAddr->au8[0] & 0x01);
}


/**
 * Is it a dummy MAC address?
 *
 * We use dummy MAC addresses for interfaces which we don't know the MAC
 * address of because they haven't sent anything (learning) or explicitly set
 * it.
 *
 * @returns true if dummy, false if not.
 * @param   pMacAddr            The address to inspect.
 */
DECL_FORCE_INLINE(bool) intnetR0IsMacAddrDummy(PCRTMAC pMacAddr)
{
    /* The dummy address are broadcast addresses, don't bother check it all. */
    return pMacAddr->au16[0] == 0xffff;
}


/**
 * Compares two MAC addresses.
 *
 * @returns true if equal, false if not.
 * @param   pDstAddr1           Address 1.
 * @param   pDstAddr2           Address 2.
 */
DECL_FORCE_INLINE(bool) intnetR0AreMacAddrsEqual(PCRTMAC pDstAddr1, PCRTMAC pDstAddr2)
{
    return pDstAddr1->au16[2] == pDstAddr2->au16[2]
        && pDstAddr1->au16[1] == pDstAddr2->au16[1]
        && pDstAddr1->au16[0] == pDstAddr2->au16[0];
}


/**
 * Switch a unicast frame based on the network layer address (OSI level 3) and
 * return a destination table.
 *
 * @returns INTNETSWDECISION_DROP, INTNETSWDECISION_TRUNK,
 *          INTNETSWDECISION_INTNET or INTNETSWDECISION_BROADCAST (misnomer).
 * @param   pNetwork            The network to switch on.
 * @param   pDstMacAddr         The destination MAC address.
 * @param   enmL3AddrType       The level-3 destination address type.
 * @param   pL3Addr             The level-3 destination address.
 * @param   cbL3Addr            The size of the level-3 destination address.
 * @param   fSrc                The frame source (INTNETTRUNKDIR_WIRE).
 * @param   pDstTab             The destination output table.
 */
static INTNETSWDECISION intnetR0NetworkSwitchLevel3(PINTNETNETWORK pNetwork, PCRTMAC pDstMacAddr,
                                                    INTNETADDRTYPE enmL3AddrType, PCRTNETADDRU pL3Addr, uint8_t cbL3Addr,
                                                    uint32_t fSrc, PINTNETDSTTAB pDstTab)
{
    Assert(fSrc == INTNETTRUNKDIR_WIRE);

    /*
     * Grab the spinlock first and do the switching.
     */
    PINTNETMACTAB   pTab    = &pNetwork->MacTab;
    RTSpinlockAcquire(pNetwork->hAddrSpinlock);

    pDstTab->fTrunkDst  = 0;
    pDstTab->pTrunk     = 0;
    pDstTab->cIfs       = 0;

    /* Find exactly matching or promiscuous interfaces. */
    uint32_t cExactHits = 0;
    uint32_t iIfMac     = pTab->cEntries;
    while (iIfMac-- > 0)
    {
        if (pTab->paEntries[iIfMac].fActive)
        {
            PINTNETIF pIf    = pTab->paEntries[iIfMac].pIf;         AssertPtr(pIf); Assert(pIf->pNetwork == pNetwork);
            bool      fExact = intnetR0IfAddrCacheLookup(&pIf->aAddrCache[enmL3AddrType], pL3Addr, cbL3Addr) >= 0;
            if (fExact || pTab->paEntries[iIfMac].fPromiscuousSeeTrunk)
            {
                cExactHits += fExact;

                uint32_t iIfDst = pDstTab->cIfs++;
                pDstTab->aIfs[iIfDst].pIf            = pIf;
                pDstTab->aIfs[iIfDst].fReplaceDstMac = fExact;
                intnetR0BusyIncIf(pIf);

                if (fExact)
                    pDstMacAddr = &pIf->MacAddr; /* Avoids duplicates being sent to the host. */
            }
        }
    }

    /* Network only promicuous mode ifs should see related trunk traffic. */
    if (   cExactHits
        && fSrc
        && pNetwork->MacTab.cPromiscuousNoTrunkEntries)
    {
        iIfMac = pTab->cEntries;
        while (iIfMac-- > 0)
        {
            if (   pTab->paEntries[iIfMac].fActive
                && pTab->paEntries[iIfMac].fPromiscuousEff
                && !pTab->paEntries[iIfMac].fPromiscuousSeeTrunk)
            {
                PINTNETIF pIf = pTab->paEntries[iIfMac].pIf;        AssertPtr(pIf); Assert(pIf->pNetwork == pNetwork);
                if (intnetR0IfAddrCacheLookup(&pIf->aAddrCache[enmL3AddrType], pL3Addr, cbL3Addr) < 0)
                {
                    uint32_t iIfDst = pDstTab->cIfs++;
                    pDstTab->aIfs[iIfDst].pIf            = pIf;
                    pDstTab->aIfs[iIfDst].fReplaceDstMac = false;
                    intnetR0BusyIncIf(pIf);
                }
            }
        }
    }

    /* Does it match the host, or is the host promiscuous? */
    if (pTab->fHostActive)
    {
        bool fExact = intnetR0AreMacAddrsEqual(&pTab->HostMac, pDstMacAddr);
        if (   fExact
            || intnetR0IsMacAddrDummy(&pTab->HostMac)
            || pTab->fHostPromiscuousEff)
        {
            cExactHits += fExact;
            pDstTab->fTrunkDst |= INTNETTRUNKDIR_HOST;
        }
    }

    /* Hit the wire if there are no exact matches or if it's in promiscuous mode. */
    if (pTab->fWireActive && (!cExactHits || pTab->fWirePromiscuousEff))
        pDstTab->fTrunkDst |= INTNETTRUNKDIR_WIRE;
    pDstTab->fTrunkDst &= ~fSrc;
    if (pDstTab->fTrunkDst)
    {
        PINTNETTRUNKIF pTrunk = pTab->pTrunk;
        pDstTab->pTrunk = pTrunk;
        intnetR0BusyIncTrunk(pTrunk);
    }

    RTSpinlockRelease(pNetwork->hAddrSpinlock);
    return pDstTab->cIfs
         ? (!pDstTab->fTrunkDst ? INTNETSWDECISION_INTNET : INTNETSWDECISION_BROADCAST)
         : (!pDstTab->fTrunkDst ? INTNETSWDECISION_DROP   : INTNETSWDECISION_TRUNK);
}


/**
 * Pre-switch a unicast MAC address.
 *
 * @returns INTNETSWDECISION_DROP, INTNETSWDECISION_TRUNK,
 *          INTNETSWDECISION_INTNET or INTNETSWDECISION_BROADCAST (misnomer).
 * @param   pNetwork            The network to switch on.
 * @param   fSrc                The frame source.
 * @param   pSrcAddr            The source address of the frame.
 * @param   pDstAddr            The destination address of the frame.
 */
static INTNETSWDECISION intnetR0NetworkPreSwitchUnicast(PINTNETNETWORK pNetwork, uint32_t fSrc, PCRTMAC pSrcAddr,
                                                        PCRTMAC pDstAddr)
{
    Assert(!intnetR0IsMacAddrMulticast(pDstAddr));
    Assert(fSrc);

    /*
     * Grab the spinlock first and do the switching.
     */
    INTNETSWDECISION    enmSwDecision   = INTNETSWDECISION_BROADCAST;
    PINTNETMACTAB       pTab            = &pNetwork->MacTab;
    RTSpinlockAcquire(pNetwork->hAddrSpinlock);

    /* Iterate the internal network interfaces and look for matching source and
       destination addresses. */
    uint32_t iIfMac = pTab->cEntries;
    while (iIfMac-- > 0)
    {
        if (pTab->paEntries[iIfMac].fActive)
        {
            /* Unknown interface address? */
            if (intnetR0IsMacAddrDummy(&pTab->paEntries[iIfMac].MacAddr))
                break;

            /* Paranoia - this shouldn't happen, right? */
            if (    pSrcAddr
                &&  intnetR0AreMacAddrsEqual(&pTab->paEntries[iIfMac].MacAddr, pSrcAddr))
                break;

            /* Exact match? */
            if (intnetR0AreMacAddrsEqual(&pTab->paEntries[iIfMac].MacAddr, pDstAddr))
            {
                enmSwDecision = pTab->fHostPromiscuousEff && fSrc == INTNETTRUNKDIR_WIRE
                              ? INTNETSWDECISION_BROADCAST
                              : INTNETSWDECISION_INTNET;
                break;
            }
        }
    }

    RTSpinlockRelease(pNetwork->hAddrSpinlock);
    return enmSwDecision;
}


/**
 * Switch a unicast MAC address and return a destination table.
 *
 * @returns INTNETSWDECISION_DROP, INTNETSWDECISION_TRUNK,
 *          INTNETSWDECISION_INTNET or INTNETSWDECISION_BROADCAST (misnomer).
 * @param   pNetwork            The network to switch on.
 * @param   fSrc                The frame source.
 * @param   pIfSender           The sender interface, NULL if trunk.  Used to
 *                              prevent sending an echo to the sender.
 * @param   pDstAddr            The destination address of the frame.
 * @param   pDstTab             The destination output table.
 */
static INTNETSWDECISION intnetR0NetworkSwitchUnicast(PINTNETNETWORK pNetwork, uint32_t fSrc, PINTNETIF pIfSender,
                                                     PCRTMAC pDstAddr, PINTNETDSTTAB pDstTab)
{
    AssertPtr(pDstTab);
    Assert(!intnetR0IsMacAddrMulticast(pDstAddr));

    /*
     * Grab the spinlock first and do the switching.
     */
    PINTNETMACTAB   pTab = &pNetwork->MacTab;
    RTSpinlockAcquire(pNetwork->hAddrSpinlock);

    pDstTab->fTrunkDst  = 0;
    pDstTab->pTrunk     = 0;
    pDstTab->cIfs       = 0;

    /* Find exactly matching or promiscuous interfaces. */
    uint32_t cExactHits = 0;
    uint32_t iIfMac     = pTab->cEntries;
    while (iIfMac-- > 0)
    {
        if (pTab->paEntries[iIfMac].fActive)
        {
            bool fExact = intnetR0AreMacAddrsEqual(&pTab->paEntries[iIfMac].MacAddr, pDstAddr);
            if (   fExact
                || intnetR0IsMacAddrDummy(&pTab->paEntries[iIfMac].MacAddr)
                || (   pTab->paEntries[iIfMac].fPromiscuousSeeTrunk
                    || (!fSrc && pTab->paEntries[iIfMac].fPromiscuousEff) )
               )
            {
                cExactHits += fExact;

                PINTNETIF pIf = pTab->paEntries[iIfMac].pIf;        AssertPtr(pIf); Assert(pIf->pNetwork == pNetwork);
                if (RT_LIKELY(pIf != pIfSender)) /* paranoia */
                {
                    uint32_t iIfDst = pDstTab->cIfs++;
                    pDstTab->aIfs[iIfDst].pIf            = pIf;
                    pDstTab->aIfs[iIfDst].fReplaceDstMac = false;
                    intnetR0BusyIncIf(pIf);
                }
            }
        }
    }

    /* Network only promicuous mode ifs should see related trunk traffic. */
    if (   cExactHits
        && fSrc
        && pNetwork->MacTab.cPromiscuousNoTrunkEntries)
    {
        iIfMac = pTab->cEntries;
        while (iIfMac-- > 0)
        {
            if (   pTab->paEntries[iIfMac].fPromiscuousEff
                && !pTab->paEntries[iIfMac].fPromiscuousSeeTrunk
                && pTab->paEntries[iIfMac].fActive
                && !intnetR0AreMacAddrsEqual(&pTab->paEntries[iIfMac].MacAddr, pDstAddr)
                && !intnetR0IsMacAddrDummy(&pTab->paEntries[iIfMac].MacAddr) )
            {
                PINTNETIF pIf    = pTab->paEntries[iIfMac].pIf;     AssertPtr(pIf); Assert(pIf->pNetwork == pNetwork);
                uint32_t  iIfDst = pDstTab->cIfs++;
                pDstTab->aIfs[iIfDst].pIf            = pIf;
                pDstTab->aIfs[iIfDst].fReplaceDstMac = false;
                intnetR0BusyIncIf(pIf);
            }
        }
    }

    /* Does it match the host, or is the host promiscuous? */
    if (   fSrc != INTNETTRUNKDIR_HOST
        && pTab->fHostActive)
    {
        bool fExact = intnetR0AreMacAddrsEqual(&pTab->HostMac, pDstAddr);
        if (   fExact
            || intnetR0IsMacAddrDummy(&pTab->HostMac)
            || pTab->fHostPromiscuousEff)
        {
            cExactHits += fExact;
            pDstTab->fTrunkDst |= INTNETTRUNKDIR_HOST;
        }
    }

    /* Hit the wire if there are no exact matches or if it's in promiscuous mode. */
    if (   fSrc != INTNETTRUNKDIR_WIRE
        && pTab->fWireActive
        && (!cExactHits || pTab->fWirePromiscuousEff)
       )
        pDstTab->fTrunkDst |= INTNETTRUNKDIR_WIRE;

    /* Grab the trunk if we're sending to it. */
    if (pDstTab->fTrunkDst)
    {
        PINTNETTRUNKIF pTrunk = pTab->pTrunk;
        pDstTab->pTrunk = pTrunk;
        intnetR0BusyIncTrunk(pTrunk);
    }

    RTSpinlockRelease(pNetwork->hAddrSpinlock);
    return pDstTab->cIfs
         ? (!pDstTab->fTrunkDst ? INTNETSWDECISION_INTNET : INTNETSWDECISION_BROADCAST)
         : (!pDstTab->fTrunkDst ? INTNETSWDECISION_DROP   : INTNETSWDECISION_TRUNK);
}


/**
 * Create a destination table for a broadcast frame.
 *
 * @returns INTNETSWDECISION_BROADCAST.
 * @param   pNetwork            The network to switch on.
 * @param   fSrc                The frame source.
 * @param   pIfSender           The sender interface, NULL if trunk.  Used to
 *                              prevent sending an echo to the sender.
 * @param   pDstTab             The destination output table.
 */
static INTNETSWDECISION intnetR0NetworkSwitchBroadcast(PINTNETNETWORK pNetwork, uint32_t fSrc, PINTNETIF pIfSender,
                                                       PINTNETDSTTAB pDstTab)
{
    AssertPtr(pDstTab);

    /*
     * Grab the spinlock first and record all active interfaces.
     */
    PINTNETMACTAB   pTab    = &pNetwork->MacTab;
    RTSpinlockAcquire(pNetwork->hAddrSpinlock);

    pDstTab->fTrunkDst  = 0;
    pDstTab->pTrunk     = 0;
    pDstTab->cIfs       = 0;

    /* Regular interfaces. */
    uint32_t iIfMac = pTab->cEntries;
    while (iIfMac-- > 0)
    {
        if (pTab->paEntries[iIfMac].fActive)
        {
            PINTNETIF pIf = pTab->paEntries[iIfMac].pIf;            AssertPtr(pIf); Assert(pIf->pNetwork == pNetwork);
            if (pIf != pIfSender)
            {
                uint32_t iIfDst = pDstTab->cIfs++;
                pDstTab->aIfs[iIfDst].pIf            = pIf;
                pDstTab->aIfs[iIfDst].fReplaceDstMac = false;
                intnetR0BusyIncIf(pIf);
            }
        }
    }

    /* The trunk interface. */
    if (pTab->fHostActive)
        pDstTab->fTrunkDst |= INTNETTRUNKDIR_HOST;
    if (pTab->fWireActive)
        pDstTab->fTrunkDst |= INTNETTRUNKDIR_WIRE;
    pDstTab->fTrunkDst &= ~fSrc;
    if (pDstTab->fTrunkDst)
    {
        PINTNETTRUNKIF pTrunk = pTab->pTrunk;
        pDstTab->pTrunk = pTrunk;
        intnetR0BusyIncTrunk(pTrunk);
    }

    RTSpinlockRelease(pNetwork->hAddrSpinlock);
    return INTNETSWDECISION_BROADCAST;
}


/**
 * Create a destination table with the trunk and any promiscuous interfaces.
 *
 * This is only used in a fallback case of the level-3 switching, so we can
 * assume the wire as source and skip the sender interface filtering.
 *
 * @returns INTNETSWDECISION_DROP, INTNETSWDECISION_TRUNK,
 *          INTNETSWDECISION_INTNET or INTNETSWDECISION_BROADCAST (misnomer).
 * @param   pNetwork            The network to switch on.
 * @param   fSrc                The frame source.
 * @param   pDstTab             The destination output table.
 */
static INTNETSWDECISION intnetR0NetworkSwitchTrunkAndPromisc(PINTNETNETWORK pNetwork, uint32_t fSrc, PINTNETDSTTAB pDstTab)
{
    Assert(fSrc == INTNETTRUNKDIR_WIRE);

    /*
     * Grab the spinlock first and do the switching.
     */
    PINTNETMACTAB   pTab    = &pNetwork->MacTab;
    RTSpinlockAcquire(pNetwork->hAddrSpinlock);

    pDstTab->fTrunkDst  = 0;
    pDstTab->pTrunk     = 0;
    pDstTab->cIfs       = 0;

    /* Find promiscuous interfaces. */
    uint32_t iIfMac = pTab->cEntries;
    while (iIfMac-- > 0)
    {
        if (   pTab->paEntries[iIfMac].fActive
            && (   pTab->paEntries[iIfMac].fPromiscuousSeeTrunk
                || (!fSrc && pTab->paEntries[iIfMac].fPromiscuousEff) )
           )
        {
            PINTNETIF pIf    = pTab->paEntries[iIfMac].pIf;         AssertPtr(pIf); Assert(pIf->pNetwork == pNetwork);
            uint32_t  iIfDst = pDstTab->cIfs++;
            pDstTab->aIfs[iIfDst].pIf            = pIf;
            pDstTab->aIfs[iIfDst].fReplaceDstMac = false;
            intnetR0BusyIncIf(pIf);
        }
    }

    /* The trunk interface. */
    if (pTab->fHostActive)
        pDstTab->fTrunkDst |= INTNETTRUNKDIR_HOST;
    if (pTab->fWireActive)
        pDstTab->fTrunkDst |= INTNETTRUNKDIR_WIRE;
    pDstTab->fTrunkDst &= ~fSrc;
    if (pDstTab->fTrunkDst)
    {
        PINTNETTRUNKIF pTrunk = pTab->pTrunk;
        pDstTab->pTrunk = pTrunk;
        intnetR0BusyIncTrunk(pTrunk);
    }

    RTSpinlockRelease(pNetwork->hAddrSpinlock);
    return !pDstTab->cIfs
        ? (!pDstTab->fTrunkDst ? INTNETSWDECISION_DROP   : INTNETSWDECISION_TRUNK)
        : (!pDstTab->fTrunkDst ? INTNETSWDECISION_INTNET : INTNETSWDECISION_BROADCAST);
}


/**
 * Create a destination table for a trunk frame.
 *
 * @returns INTNETSWDECISION_BROADCAST.
 * @param   pNetwork            The network to switch on.
 * @param   fSrc                The frame source.
 * @param   pDstTab             The destination output table.
 */
static INTNETSWDECISION intnetR0NetworkSwitchTrunk(PINTNETNETWORK pNetwork, uint32_t fSrc, PINTNETDSTTAB pDstTab)
{
    AssertPtr(pDstTab);

    /*
     * Grab the spinlock first and record all active interfaces.
     */
    PINTNETMACTAB   pTab= &pNetwork->MacTab;
    RTSpinlockAcquire(pNetwork->hAddrSpinlock);

    pDstTab->fTrunkDst  = 0;
    pDstTab->pTrunk     = 0;
    pDstTab->cIfs       = 0;

    /* The trunk interface. */
    if (pTab->fHostActive)
        pDstTab->fTrunkDst |= INTNETTRUNKDIR_HOST;
    if (pTab->fWireActive)
        pDstTab->fTrunkDst |= INTNETTRUNKDIR_WIRE;
    pDstTab->fTrunkDst &= ~fSrc;
    if (pDstTab->fTrunkDst)
    {
        PINTNETTRUNKIF pTrunk = pTab->pTrunk;
        pDstTab->pTrunk = pTrunk;
        intnetR0BusyIncTrunk(pTrunk);
    }

    RTSpinlockRelease(pNetwork->hAddrSpinlock);
    return pDstTab->fTrunkDst ? INTNETSWDECISION_TRUNK : INTNETSWDECISION_DROP;
}


/**
 * Wrapper around RTMemAlloc for allocating a destination table.
 *
 * @returns VINF_SUCCESS or VERR_NO_MEMORY.
 * @param   cEntries            The size given as an entry count.
 * @param   ppDstTab            Where to store the pointer (always).
 */
DECLINLINE(int) intnetR0AllocDstTab(uint32_t cEntries, PINTNETDSTTAB *ppDstTab)
{
    PINTNETDSTTAB pDstTab;
    *ppDstTab = pDstTab = (PINTNETDSTTAB)RTMemAlloc(RT_UOFFSETOF_DYN(INTNETDSTTAB, aIfs[cEntries]));
    if (RT_UNLIKELY(!pDstTab))
        return VERR_NO_MEMORY;
    return VINF_SUCCESS;
}


/**
 * Ensures that there is space for another interface in the MAC address lookup
 * table as well as all the destination tables.
 *
 * The caller must own the create/open/destroy mutex.
 *
 * @returns VINF_SUCCESS, VERR_NO_MEMORY or VERR_OUT_OF_RANGE.
 * @param   pNetwork        The network to operate on.
 */
static int intnetR0NetworkEnsureTabSpace(PINTNETNETWORK pNetwork)
{
    /*
     * The cEntries and cEntriesAllocated members are only updated while
     * owning the big mutex, so we only need the spinlock when doing the
     * actual table replacing.
     */
    PINTNETMACTAB pTab = &pNetwork->MacTab;
    int rc = VINF_SUCCESS;
    AssertReturn(pTab->cEntries <= pTab->cEntriesAllocated, VERR_INTERNAL_ERROR_2);
    if (pTab->cEntries + 1 > pTab->cEntriesAllocated)
    {
        uint32_t const cAllocated = pTab->cEntriesAllocated + INTNET_GROW_DSTTAB_SIZE;
        if (cAllocated <= INTNET_MAX_IFS)
        {
            /*
             * Resize the destination tables first, this can be kind of tedious.
             */
            for (uint32_t i = 0; i < pTab->cEntries; i++)
            {
                PINTNETIF       pIf = pTab->paEntries[i].pIf; AssertPtr(pIf);
                PINTNETDSTTAB   pNew;
                rc = intnetR0AllocDstTab(cAllocated, &pNew);
                if (RT_FAILURE(rc))
                    break;

                for (;;)
                {
                    PINTNETDSTTAB pOld = pIf->pDstTab;
                    if (   pOld
                        && ASMAtomicCmpXchgPtr(&pIf->pDstTab, pNew, pOld))
                    {
                        RTMemFree(pOld);
                        break;
                    }
                    intnetR0BusyWait(pNetwork, &pIf->cBusy);
                }
            }

            /*
             * The trunk.
             */
            if (    RT_SUCCESS(rc)
                &&  pNetwork->MacTab.pTrunk)
            {
                AssertCompileAdjacentMembers(INTNETTRUNKIF, apTaskDstTabs, apIntDstTabs);
                PINTNETTRUNKIF          pTrunk      = pNetwork->MacTab.pTrunk;
                PINTNETDSTTAB * const   ppEndDstTab = &pTrunk->apIntDstTabs[pTrunk->cIntDstTabs];
                for (PINTNETDSTTAB     *ppDstTab    = &pTrunk->apTaskDstTabs[0];
                     ppDstTab != ppEndDstTab && RT_SUCCESS(rc);
                     ppDstTab++)
                {
                    PINTNETDSTTAB pNew;
                    rc = intnetR0AllocDstTab(cAllocated, &pNew);
                    if (RT_FAILURE(rc))
                        break;

                    for (;;)
                    {
                        RTSpinlockAcquire(pTrunk->hDstTabSpinlock);
                        void *pvOld = *ppDstTab;
                        if (pvOld)
                            *ppDstTab = pNew;
                        RTSpinlockRelease(pTrunk->hDstTabSpinlock);
                        if (pvOld)
                        {
                            RTMemFree(pvOld);
                            break;
                        }
                        intnetR0BusyWait(pNetwork, &pTrunk->cBusy);
                    }
                }
            }

            /*
             * The MAC Address table itself.
             */
            if (RT_SUCCESS(rc))
            {
                PINTNETMACTABENTRY paNew = (PINTNETMACTABENTRY)RTMemAlloc(sizeof(INTNETMACTABENTRY) * cAllocated);
                if (paNew)
                {
                    RTSpinlockAcquire(pNetwork->hAddrSpinlock);

                    PINTNETMACTABENTRY  paOld = pTab->paEntries;
                    uint32_t            i     = pTab->cEntries;
                    while (i-- > 0)
                    {
                        paNew[i] = paOld[i];

                        paOld[i].fActive = false;
                        paOld[i].pIf     = NULL;
                    }

                    pTab->paEntries         = paNew;
                    pTab->cEntriesAllocated = cAllocated;

                    RTSpinlockRelease(pNetwork->hAddrSpinlock);

                    RTMemFree(paOld);
                }
                else
                    rc = VERR_NO_MEMORY;
            }
        }
        else
            rc = VERR_OUT_OF_RANGE;
    }
    return rc;
}




#ifdef INTNET_WITH_DHCP_SNOOPING

/**
 * Snoops IP assignments and releases from the DHCPv4 traffic.
 *
 * The caller is responsible for making sure this traffic between the
 * BOOTPS and BOOTPC ports and validate the IP header. The UDP packet
 * need not be validated beyond the ports.
 *
 * @param   pNetwork        The network this frame was seen on.
 * @param   pIpHdr          Pointer to a valid IP header. This is for pseudo
 *                          header validation, so only the minimum header size
 *                          needs to be available and valid here.
 * @param   pUdpHdr         Pointer to the UDP header in the frame.
 * @param   cbUdpPkt        What's left of the frame when starting at the UDP header.
 * @param   fGso            Set if this is a GSO frame, clear if regular.
 */
static void intnetR0NetworkSnoopDhcp(PINTNETNETWORK pNetwork, PCRTNETIPV4 pIpHdr, PCRTNETUDP pUdpHdr, uint32_t cbUdpPkt)
{
    /*
     * Check if the DHCP message is valid and get the type.
     */
    if (!RTNetIPv4IsUDPValid(pIpHdr, pUdpHdr, pUdpHdr + 1, cbUdpPkt, true /*fCheckSum*/))
    {
        Log6(("Bad UDP packet\n"));
        return;
    }
    PCRTNETBOOTP pDhcp = (PCRTNETBOOTP)(pUdpHdr + 1);
    uint8_t MsgType;
    if (!RTNetIPv4IsDHCPValid(pUdpHdr, pDhcp, cbUdpPkt - sizeof(*pUdpHdr), &MsgType))
    {
        Log6(("Bad DHCP packet\n"));
        return;
    }

#ifdef LOG_ENABLED
    /*
     * Log it.
     */
    const char *pszType = "unknown";
    switch (MsgType)
    {
        case RTNET_DHCP_MT_DISCOVER: pszType = "discover";  break;
        case RTNET_DHCP_MT_OFFER:    pszType = "offer"; break;
        case RTNET_DHCP_MT_REQUEST:  pszType = "request"; break;
        case RTNET_DHCP_MT_DECLINE:  pszType = "decline"; break;
        case RTNET_DHCP_MT_ACK:      pszType = "ack"; break;
        case RTNET_DHCP_MT_NAC:      pszType = "nac"; break;
        case RTNET_DHCP_MT_RELEASE:  pszType = "release"; break;
        case RTNET_DHCP_MT_INFORM:   pszType = "inform"; break;
    }
    Log6(("DHCP msg: %d (%s) client %.6Rhxs ciaddr=%d.%d.%d.%d yiaddr=%d.%d.%d.%d\n", MsgType, pszType, &pDhcp->bp_chaddr,
          pDhcp->bp_ciaddr.au8[0], pDhcp->bp_ciaddr.au8[1], pDhcp->bp_ciaddr.au8[2], pDhcp->bp_ciaddr.au8[3],
          pDhcp->bp_yiaddr.au8[0], pDhcp->bp_yiaddr.au8[1], pDhcp->bp_yiaddr.au8[2], pDhcp->bp_yiaddr.au8[3]));
#endif /* LOG_EANBLED */

    /*
     * Act upon the message.
     */
    switch (MsgType)
    {
#if 0
        case RTNET_DHCP_MT_REQUEST:
            /** @todo Check for valid non-broadcast requests w/ IP for any of the MACs we
             *        know, and add the IP to the cache. */
            break;
#endif


        /*
         * Lookup the interface by its MAC address and insert the IPv4 address into the cache.
         * Delete the old client address first, just in case it changed in a renewal.
         */
        case RTNET_DHCP_MT_ACK:
            if (intnetR0IPv4AddrIsGood(pDhcp->bp_yiaddr))
            {
                PINTNETIF       pMatchingIf = NULL;
                RTSpinlockAcquire(pNetwork->hAddrSpinlock);

                uint32_t iIf = pNetwork->MacTab.cEntries;
                while (iIf-- > 0)
                {
                    PINTNETIF pCur = pNetwork->MacTab.paEntries[iIf].pIf;
                    if (    intnetR0IfHasMacAddr(pCur)
                        &&  !memcmp(&pCur->MacAddr, &pDhcp->bp_chaddr, sizeof(RTMAC)))
                    {
                        intnetR0IfAddrCacheDelete(pCur, &pCur->aAddrCache[kIntNetAddrType_IPv4],
                                                  (PCRTNETADDRU)&pDhcp->bp_ciaddr, sizeof(RTNETADDRIPV4), "DHCP_MT_ACK");
                        if (!pMatchingIf)
                        {
                            pMatchingIf = pCur;
                            intnetR0BusyIncIf(pMatchingIf);
                        }
                    }
                }

                RTSpinlockRelease(pNetwork->hAddrSpinlock);

                if (pMatchingIf)
                {
                    intnetR0IfAddrCacheAdd(pMatchingIf, kIntNetAddrType_IPv4,
                                           (PCRTNETADDRU)&pDhcp->bp_yiaddr, "DHCP_MT_ACK");
                    intnetR0BusyDecIf(pMatchingIf);
                }
            }
            return;


        /*
         * Lookup the interface by its MAC address and remove the IPv4 address(es) from the cache.
         */
        case RTNET_DHCP_MT_RELEASE:
        {
            RTSpinlockAcquire(pNetwork->hAddrSpinlock);

            uint32_t iIf = pNetwork->MacTab.cEntries;
            while (iIf-- > 0)
            {
                PINTNETIF pCur = pNetwork->MacTab.paEntries[iIf].pIf;
                if (    intnetR0IfHasMacAddr(pCur)
                    &&  !memcmp(&pCur->MacAddr, &pDhcp->bp_chaddr, sizeof(RTMAC)))
                {
                    intnetR0IfAddrCacheDelete(pCur, &pCur->aAddrCache[kIntNetAddrType_IPv4],
                                              (PCRTNETADDRU)&pDhcp->bp_ciaddr, sizeof(RTNETADDRIPV4), "DHCP_MT_RELEASE");
                    intnetR0IfAddrCacheDelete(pCur, &pCur->aAddrCache[kIntNetAddrType_IPv4],
                                              (PCRTNETADDRU)&pDhcp->bp_yiaddr, sizeof(RTNETADDRIPV4), "DHCP_MT_RELEASE");
                }
            }

            RTSpinlockRelease(pNetwork->hAddrSpinlock);
            break;
        }
    }

}


/**
 * Worker for intnetR0TrunkIfSnoopAddr that takes care of what
 * is likely to be a DHCP message.
 *
 * The caller has already check that the UDP source and destination ports
 * are BOOTPS or BOOTPC.
 *
 * @param   pNetwork        The network this frame was seen on.
 * @param   pSG             The gather list for the frame.
 */
static void intnetR0TrunkIfSnoopDhcp(PINTNETNETWORK pNetwork, PCINTNETSG pSG)
{
    /*
     * Get a pointer to a linear copy of the full packet, using the
     * temporary buffer if necessary.
     */
    PCRTNETIPV4 pIpHdr = (PCRTNETIPV4)((PCRTNETETHERHDR)pSG->aSegs[0].pv + 1);
    uint32_t cbPacket = pSG->cbTotal - sizeof(RTNETETHERHDR);
    if (pSG->cSegsUsed > 1)
    {
        cbPacket = RT_MIN(cbPacket, INTNETNETWORK_TMP_SIZE);
        Log6(("intnetR0TrunkIfSnoopDhcp: Copying IPv4/UDP/DHCP pkt %u\n", cbPacket));
        if (!intnetR0SgReadPart(pSG, sizeof(RTNETETHERHDR), cbPacket, pNetwork->pbTmp))
            return;
        //pSG->fFlags |= INTNETSG_FLAGS_PKT_CP_IN_TMP;
        pIpHdr = (PCRTNETIPV4)pNetwork->pbTmp;
    }

    /*
     * Validate the IP header and find the UDP packet.
     */
    if (!RTNetIPv4IsHdrValid(pIpHdr, cbPacket, pSG->cbTotal - sizeof(RTNETETHERHDR), true /*fChecksum*/))
    {
        Log(("intnetR0TrunkIfSnoopDhcp: bad ip header\n"));
        return;
    }
    uint32_t cbIpHdr = pIpHdr->ip_hl * 4;

    /*
     * Hand it over to the common DHCP snooper.
     */
    intnetR0NetworkSnoopDhcp(pNetwork, pIpHdr, (PCRTNETUDP)((uintptr_t)pIpHdr + cbIpHdr), cbPacket - cbIpHdr);
}

#endif /* INTNET_WITH_DHCP_SNOOPING */


/**
 * Snoops up source addresses from ARP requests and purge these from the address
 * caches.
 *
 * The purpose of this purging is to get rid of stale addresses.
 *
 * @param   pNetwork        The network this frame was seen on.
 * @param   pSG             The gather list for the frame.
 */
static void intnetR0TrunkIfSnoopArp(PINTNETNETWORK pNetwork, PCINTNETSG pSG)
{
    /*
     * Check the minimum size first.
     */
    if (RT_UNLIKELY(pSG->cbTotal < sizeof(RTNETETHERHDR) + sizeof(RTNETARPIPV4)))
        return;

    /*
     * Copy to temporary buffer if necessary.
     */
    uint32_t cbPacket = RT_MIN(pSG->cbTotal, sizeof(RTNETARPIPV4));
    PCRTNETARPIPV4 pArpIPv4 = (PCRTNETARPIPV4)((uintptr_t)pSG->aSegs[0].pv + sizeof(RTNETETHERHDR));
    if (    pSG->cSegsUsed != 1
        &&  pSG->aSegs[0].cb < cbPacket)
    {
        if (        (pSG->fFlags & (INTNETSG_FLAGS_ARP_IPV4 | INTNETSG_FLAGS_PKT_CP_IN_TMP))
                !=  (INTNETSG_FLAGS_ARP_IPV4 | INTNETSG_FLAGS_PKT_CP_IN_TMP)
            && !intnetR0SgReadPart(pSG, sizeof(RTNETETHERHDR), cbPacket, pNetwork->pbTmp))
                return;
        pArpIPv4 = (PCRTNETARPIPV4)pNetwork->pbTmp;
    }

    /*
     * Ignore packets which doesn't interest us or we perceive as malformed.
     */
    if (RT_UNLIKELY(    pArpIPv4->Hdr.ar_hlen  != sizeof(RTMAC)
                    ||  pArpIPv4->Hdr.ar_plen  != sizeof(RTNETADDRIPV4)
                    ||  pArpIPv4->Hdr.ar_htype != RT_H2BE_U16(RTNET_ARP_ETHER)
                    ||  pArpIPv4->Hdr.ar_ptype != RT_H2BE_U16(RTNET_ETHERTYPE_IPV4)))
        return;
    uint16_t ar_oper = RT_H2BE_U16(pArpIPv4->Hdr.ar_oper);
    if (RT_UNLIKELY(    ar_oper != RTNET_ARPOP_REQUEST
                    &&  ar_oper != RTNET_ARPOP_REPLY))
    {
        Log6(("ts-ar: op=%#x\n", ar_oper));
        return;
    }

    /*
     * Delete the source address if it's OK.
     */
    if (    !intnetR0IsMacAddrMulticast(&pArpIPv4->ar_sha)
        &&  (   pArpIPv4->ar_sha.au16[0]
             || pArpIPv4->ar_sha.au16[1]
             || pArpIPv4->ar_sha.au16[2])
        &&  intnetR0IPv4AddrIsGood(pArpIPv4->ar_spa))
    {
        Log6(("ts-ar: %d.%d.%d.%d / %.6Rhxs\n", pArpIPv4->ar_spa.au8[0], pArpIPv4->ar_spa.au8[1],
              pArpIPv4->ar_spa.au8[2], pArpIPv4->ar_spa.au8[3], &pArpIPv4->ar_sha));
        intnetR0NetworkAddrCacheDelete(pNetwork, (PCRTNETADDRU)&pArpIPv4->ar_spa,
                                       kIntNetAddrType_IPv4, sizeof(pArpIPv4->ar_spa), "tif/arp");
    }
}


#ifdef INTNET_WITH_DHCP_SNOOPING
/**
 * Snoop up addresses from ARP and DHCP traffic from frames coming
 * over the trunk connection.
 *
 * The caller is responsible for do some basic filtering before calling
 * this function.
 * For IPv4 this means checking against the minimum DHCPv4 frame size.
 *
 * @param   pNetwork        The network.
 * @param   pSG             The SG list for the frame.
 * @param   EtherType       The Ethertype of the frame.
 */
static void intnetR0TrunkIfSnoopAddr(PINTNETNETWORK pNetwork, PCINTNETSG pSG, uint16_t EtherType)
{
    switch (EtherType)
    {
        case RTNET_ETHERTYPE_IPV4:
        {
            uint32_t    cbIpHdr;
            uint8_t     b;

            Assert(pSG->cbTotal >= sizeof(RTNETETHERHDR) + RTNETIPV4_MIN_LEN + RTNETUDP_MIN_LEN + RTNETBOOTP_DHCP_MIN_LEN);
            if (pSG->aSegs[0].cb >= sizeof(RTNETETHERHDR) + RTNETIPV4_MIN_LEN)
            {
                /* check if the protocol is UDP */
                PCRTNETIPV4 pIpHdr = (PCRTNETIPV4)((uint8_t const *)pSG->aSegs[0].pv + sizeof(RTNETETHERHDR));
                if (pIpHdr->ip_p != RTNETIPV4_PROT_UDP)
                    return;

                /* get the TCP header length */
                cbIpHdr = pIpHdr->ip_hl * 4;
            }
            else
            {
                /* check if the protocol is UDP */
                if (    intnetR0SgReadByte(pSG, sizeof(RTNETETHERHDR) + RT_UOFFSETOF(RTNETIPV4, ip_p))
                    !=  RTNETIPV4_PROT_UDP)
                    return;

                /* get the TCP header length */
                b = intnetR0SgReadByte(pSG, sizeof(RTNETETHERHDR) + 0); /* (IPv4 first byte, a bitfield) */
                cbIpHdr = (b & 0x0f) * 4;
            }
            if (cbIpHdr < RTNETIPV4_MIN_LEN)
                return;

            /* compare the ports. */
            if (pSG->aSegs[0].cb >= sizeof(RTNETETHERHDR) + cbIpHdr + RTNETUDP_MIN_LEN)
            {
                PCRTNETUDP pUdpHdr = (PCRTNETUDP)((uint8_t const *)pSG->aSegs[0].pv + sizeof(RTNETETHERHDR) + cbIpHdr);
                if (    (   RT_BE2H_U16(pUdpHdr->uh_sport) != RTNETIPV4_PORT_BOOTPS
                         && RT_BE2H_U16(pUdpHdr->uh_dport) != RTNETIPV4_PORT_BOOTPS)
                    ||  (   RT_BE2H_U16(pUdpHdr->uh_dport) != RTNETIPV4_PORT_BOOTPC
                         && RT_BE2H_U16(pUdpHdr->uh_sport) != RTNETIPV4_PORT_BOOTPC))
                    return;
            }
            else
            {
                /* get the lower byte of the UDP source port number. */
                b = intnetR0SgReadByte(pSG, sizeof(RTNETETHERHDR) + cbIpHdr + RT_UOFFSETOF(RTNETUDP, uh_sport) + 1);
                if (    b != RTNETIPV4_PORT_BOOTPS
                    &&  b != RTNETIPV4_PORT_BOOTPC)
                    return;
                uint8_t SrcPort = b;
                b = intnetR0SgReadByte(pSG, sizeof(RTNETETHERHDR) + cbIpHdr + RT_UOFFSETOF(RTNETUDP, uh_sport));
                if (b)
                    return;

                /* get the lower byte of the UDP destination port number. */
                b = intnetR0SgReadByte(pSG, sizeof(RTNETETHERHDR) + cbIpHdr + RT_UOFFSETOF(RTNETUDP, uh_dport) + 1);
                if (    b != RTNETIPV4_PORT_BOOTPS
                    &&  b != RTNETIPV4_PORT_BOOTPC)
                    return;
                if (b == SrcPort)
                    return;
                b = intnetR0SgReadByte(pSG, sizeof(RTNETETHERHDR) + cbIpHdr + RT_UOFFSETOF(RTNETUDP, uh_dport));
                if (b)
                    return;
            }
            intnetR0TrunkIfSnoopDhcp(pNetwork, pSG);
            break;
        }

        case RTNET_ETHERTYPE_ARP:
            intnetR0TrunkIfSnoopArp(pNetwork, pSG);
            break;
    }
}
#endif /* INTNET_WITH_DHCP_SNOOPING */

/**
 * Deals with an IPv6 packet.
 *
 * This will fish out the source IP address and add it to the cache.
 * Then it will look for DHCPRELEASE requests (?) and anything else
 * that we might find useful later.
 *
 * @param   pIf             The interface that's sending the frame.
 * @param   pIpHdr          Pointer to the IPv4 header in the frame.
 * @param   cbPacket        The size of the packet, or more correctly the
 *                          size of the frame without the ethernet header.
 * @param   fGso            Set if this is a GSO frame, clear if regular.
 */
static void intnetR0IfSnoopIPv6SourceAddr(PINTNETIF pIf, PCRTNETIPV6 pIpHdr, uint32_t cbPacket, bool fGso)
{
    NOREF(fGso);

    /*
     * Check the header size first to prevent access invalid data.
     */
    if (cbPacket < RTNETIPV6_MIN_LEN)
        return;

    /*
     * If the source address is good (not multicast) and
     * not already in the address cache of the sender, add it.
     */
    RTNETADDRU Addr;
    Addr.IPv6 = pIpHdr->ip6_src;

    if (    intnetR0IPv6AddrIsGood(Addr.IPv6) && (pIpHdr->ip6_hlim == 0xff)
        &&  intnetR0IfAddrCacheLookupLikely(&pIf->aAddrCache[kIntNetAddrType_IPv6], &Addr, sizeof(Addr.IPv6)) < 0)
    {
        intnetR0IfAddrCacheAdd(pIf, kIntNetAddrType_IPv6, &Addr, "if/ipv6");
    }
}


/**
 * Deals with an IPv4 packet.
 *
 * This will fish out the source IP address and add it to the cache.
 * Then it will look for DHCPRELEASE requests (?) and anything else
 * that we might find useful later.
 *
 * @param   pIf             The interface that's sending the frame.
 * @param   pIpHdr          Pointer to the IPv4 header in the frame.
 * @param   cbPacket        The size of the packet, or more correctly the
 *                          size of the frame without the ethernet header.
 * @param   fGso            Set if this is a GSO frame, clear if regular.
 */
static void intnetR0IfSnoopIPv4SourceAddr(PINTNETIF pIf, PCRTNETIPV4 pIpHdr, uint32_t cbPacket, bool fGso)
{
    /*
     * Check the header size first to prevent access invalid data.
     */
    if (cbPacket < RTNETIPV4_MIN_LEN)
        return;
    uint32_t cbHdr = (uint32_t)pIpHdr->ip_hl * 4;
    if (    cbHdr < RTNETIPV4_MIN_LEN
        ||  cbPacket < cbHdr)
        return;

    /*
     * If the source address is good (not broadcast or my network) and
     * not already in the address cache of the sender, add it. Validate
     * the IP header before adding it.
     */
    bool fValidatedIpHdr = false;
    RTNETADDRU Addr;
    Addr.IPv4 = pIpHdr->ip_src;
    if (    intnetR0IPv4AddrIsGood(Addr.IPv4)
        &&  intnetR0IfAddrCacheLookupLikely(&pIf->aAddrCache[kIntNetAddrType_IPv4], &Addr, sizeof(Addr.IPv4)) < 0)
    {
        if (!RTNetIPv4IsHdrValid(pIpHdr, cbPacket, cbPacket, !fGso /*fChecksum*/))
        {
            Log(("intnetR0IfSnoopIPv4SourceAddr: bad ip header\n"));
            return;
        }

        intnetR0IfAddrCacheAddIt(pIf, kIntNetAddrType_IPv4, &Addr, "if/ipv4");
        fValidatedIpHdr = true;
    }

#ifdef INTNET_WITH_DHCP_SNOOPING
    /*
     * Check for potential DHCP packets.
     */
    if (    pIpHdr->ip_p == RTNETIPV4_PROT_UDP                              /* DHCP is UDP. */
        &&  cbPacket >= cbHdr + RTNETUDP_MIN_LEN + RTNETBOOTP_DHCP_MIN_LEN  /* Min DHCP packet len. */
        &&  !fGso)                                                          /* GSO is not applicable to DHCP traffic. */
    {
        PCRTNETUDP pUdpHdr = (PCRTNETUDP)((uint8_t const *)pIpHdr + cbHdr);
        if (    (   RT_BE2H_U16(pUdpHdr->uh_dport) == RTNETIPV4_PORT_BOOTPS
                 || RT_BE2H_U16(pUdpHdr->uh_sport) == RTNETIPV4_PORT_BOOTPS)
            &&  (   RT_BE2H_U16(pUdpHdr->uh_sport) == RTNETIPV4_PORT_BOOTPC
                 || RT_BE2H_U16(pUdpHdr->uh_dport) == RTNETIPV4_PORT_BOOTPC))
        {
            if (    fValidatedIpHdr
                ||  RTNetIPv4IsHdrValid(pIpHdr, cbPacket, cbPacket, !fGso /*fChecksum*/))
                intnetR0NetworkSnoopDhcp(pIf->pNetwork, pIpHdr, pUdpHdr, cbPacket - cbHdr);
            else
                Log(("intnetR0IfSnoopIPv4SourceAddr: bad ip header (dhcp)\n"));
        }
    }
#endif /* INTNET_WITH_DHCP_SNOOPING */
}


/**
 * Snoop up source addresses from an ARP request or reply.
 *
 * @param   pIf             The interface that's sending the frame.
 * @param   pHdr            The ARP header.
 * @param   cbPacket        The size of the packet (might be larger than the ARP
 *                          request 'cause of min ethernet frame size).
 * @param   pfSgFlags       Pointer to the SG flags. This is used to tag the packet so we
 *                          don't have to repeat the frame parsing in intnetR0TrunkIfSend.
 */
static void intnetR0IfSnoopArpAddr(PINTNETIF pIf, PCRTNETARPIPV4 pArpIPv4, uint32_t cbPacket, uint16_t *pfSgFlags)
{
    /*
     * Ignore packets which doesn't interest us or we perceive as malformed.
     */
    if (RT_UNLIKELY(cbPacket < sizeof(RTNETARPIPV4)))
        return;
    if (RT_UNLIKELY(    pArpIPv4->Hdr.ar_hlen  != sizeof(RTMAC)
                    ||  pArpIPv4->Hdr.ar_plen  != sizeof(RTNETADDRIPV4)
                    ||  pArpIPv4->Hdr.ar_htype != RT_H2BE_U16(RTNET_ARP_ETHER)
                    ||  pArpIPv4->Hdr.ar_ptype != RT_H2BE_U16(RTNET_ETHERTYPE_IPV4)))
        return;
    uint16_t ar_oper = RT_H2BE_U16(pArpIPv4->Hdr.ar_oper);
    if (RT_UNLIKELY(    ar_oper != RTNET_ARPOP_REQUEST
                    &&  ar_oper != RTNET_ARPOP_REPLY))
    {
        Log6(("ar_oper=%#x\n", ar_oper));
        return;
    }

    /*
     * Tag the SG as ARP IPv4 for later editing, then check for addresses
     * which can be removed or added to the address cache of the sender.
     */
    *pfSgFlags |= INTNETSG_FLAGS_ARP_IPV4;

    if (    ar_oper == RTNET_ARPOP_REPLY
        &&  !intnetR0IsMacAddrMulticast(&pArpIPv4->ar_tha)
        &&  (   pArpIPv4->ar_tha.au16[0]
             || pArpIPv4->ar_tha.au16[1]
             || pArpIPv4->ar_tha.au16[2])
        &&  intnetR0IPv4AddrIsGood(pArpIPv4->ar_tpa))
        intnetR0IfAddrCacheDelete(pIf, &pIf->aAddrCache[kIntNetAddrType_IPv4],
                                  (PCRTNETADDRU)&pArpIPv4->ar_tpa, sizeof(RTNETADDRIPV4), "if/arp");

    if (    !memcmp(&pArpIPv4->ar_sha, &pIf->MacAddr, sizeof(RTMAC))
        &&  intnetR0IPv4AddrIsGood(pArpIPv4->ar_spa))
    {
        intnetR0IfAddrCacheAdd(pIf, kIntNetAddrType_IPv4, (PCRTNETADDRU)&pArpIPv4->ar_spa, "if/arp");
    }
}



/**
 * Checks packets send by a normal interface for new network
 * layer addresses.
 *
 * @param   pIf             The interface that's sending the frame.
 * @param   pbFrame         The frame.
 * @param   cbFrame         The size of the frame.
 * @param   fGso            Set if this is a GSO frame, clear if regular.
 * @param   pfSgFlags       Pointer to the SG flags. This is used to tag the packet so we
 *                          don't have to repeat the frame parsing in intnetR0TrunkIfSend.
 */
static void intnetR0IfSnoopAddr(PINTNETIF pIf, uint8_t const *pbFrame, uint32_t cbFrame, bool fGso, uint16_t *pfSgFlags)
{
    /*
     * Fish out the ethertype and look for stuff we can handle.
     */
    if (cbFrame <= sizeof(RTNETETHERHDR))
        return;
    cbFrame -= sizeof(RTNETETHERHDR);

    uint16_t EtherType = RT_H2BE_U16(((PCRTNETETHERHDR)pbFrame)->EtherType);
    switch (EtherType)
    {
        case RTNET_ETHERTYPE_IPV4:
            intnetR0IfSnoopIPv4SourceAddr(pIf, (PCRTNETIPV4)((PCRTNETETHERHDR)pbFrame + 1), cbFrame, fGso);
            break;

        case RTNET_ETHERTYPE_IPV6:
            intnetR0IfSnoopIPv6SourceAddr(pIf, (PCRTNETIPV6)((PCRTNETETHERHDR)pbFrame + 1), cbFrame, fGso);
            break;

#if 0 /** @todo IntNet: implement IPX for wireless MAC sharing? */
        case RTNET_ETHERTYPE_IPX_1:
        case RTNET_ETHERTYPE_IPX_2:
        case RTNET_ETHERTYPE_IPX_3:
            intnetR0IfSnoopIpxSourceAddr(pIf, (PCINTNETIPX)((PCRTNETETHERHDR)pbFrame + 1), cbFrame, pfSgFlags);
            break;
#endif
        case RTNET_ETHERTYPE_ARP:
            intnetR0IfSnoopArpAddr(pIf, (PCRTNETARPIPV4)((PCRTNETETHERHDR)pbFrame + 1), cbFrame, pfSgFlags);
            break;
    }
}


/**
 * Writes a frame packet to the ring buffer.
 *
 * @returns VBox status code.
 * @param   pBuf            The buffer.
 * @param   pRingBuf        The ring buffer to read from.
 * @param   pSG             The gather list.
 * @param   pNewDstMac      Set the destination MAC address to the address if specified.
 */
static int intnetR0RingWriteFrame(PINTNETRINGBUF pRingBuf, PCINTNETSG pSG, PCRTMAC pNewDstMac)
{
    PINTNETHDR  pHdr  = NULL; /* shut up gcc*/
    void       *pvDst = NULL; /* ditto */
    int         rc;
    if (pSG->GsoCtx.u8Type == PDMNETWORKGSOTYPE_INVALID)
        rc = IntNetRingAllocateFrame(pRingBuf, pSG->cbTotal, &pHdr, &pvDst);
    else
        rc = IntNetRingAllocateGsoFrame(pRingBuf, pSG->cbTotal, &pSG->GsoCtx, &pHdr, &pvDst);
    if (RT_SUCCESS(rc))
    {
        IntNetSgRead(pSG, pvDst);
        if (pNewDstMac)
            ((PRTNETETHERHDR)pvDst)->DstMac = *pNewDstMac;

        IntNetRingCommitFrame(pRingBuf, pHdr);
        return VINF_SUCCESS;
    }
    return rc;
}


/**
 * Notifies consumers of incoming data from @a pIf that data is available.
 */
DECL_FORCE_INLINE(void) intnetR0IfNotifyRecv(PINTNETIF pIf)
{
#if !defined(VBOX_WITH_INTNET_SERVICE_IN_R3) || !defined(IN_RING3)
    RTSemEventSignal(pIf->hRecvEvent);
#else
    pIf->pfnRecvAvail(pIf->hIf, pIf->pvUserRecvAvail);
#endif
}


/**
 * Sends a frame to a specific interface.
 *
 * @param   pIf             The interface.
 * @param   pIfSender       The interface sending the frame. This is NULL if it's the trunk.
 * @param   pSG             The gather buffer which data is being sent to the interface.
 * @param   pNewDstMac      Set the destination MAC address to the address if specified.
 */
static void intnetR0IfSend(PINTNETIF pIf, PINTNETIF pIfSender, PINTNETSG pSG, PCRTMAC pNewDstMac)
{
    /*
     * Grab the receive/producer lock and copy over the frame.
     */
    RTSpinlockAcquire(pIf->hRecvInSpinlock);
    int rc = intnetR0RingWriteFrame(&pIf->pIntBuf->Recv, pSG, pNewDstMac);
    RTSpinlockRelease(pIf->hRecvInSpinlock);
    if (RT_SUCCESS(rc))
    {
        pIf->cYields = 0;
        intnetR0IfNotifyRecv(pIf);
        return;
    }

    Log(("intnetR0IfSend: overflow cb=%d hIf=%RX32\n", pSG->cbTotal, pIf->hIf));

    /*
     * Scheduling hack, for unicore machines primarily.
     */
    if (    pIf->fActive
        &&  pIf->cYields < 4 /* just twice */
        &&  pIfSender /* but not if it's from the trunk */
        &&  RTThreadPreemptIsEnabled(NIL_RTTHREAD)
       )
    {
        unsigned cYields = 2;
        while (--cYields > 0)
        {
            intnetR0IfNotifyRecv(pIf);
            RTThreadYield();

            RTSpinlockAcquire(pIf->hRecvInSpinlock);
            rc = intnetR0RingWriteFrame(&pIf->pIntBuf->Recv, pSG, pNewDstMac);
            RTSpinlockRelease(pIf->hRecvInSpinlock);
            if (RT_SUCCESS(rc))
            {
                STAM_REL_COUNTER_INC(&pIf->pIntBuf->cStatYieldsOk);
                intnetR0IfNotifyRecv(pIf);
                return;
            }
            pIf->cYields++;
        }
        STAM_REL_COUNTER_INC(&pIf->pIntBuf->cStatYieldsNok);
    }

    /* ok, the frame is lost. */
    STAM_REL_COUNTER_INC(&pIf->pIntBuf->cStatLost);
    intnetR0IfNotifyRecv(pIf);
}


/**
 * Fallback path that does the GSO segmenting before passing the frame on to the
 * trunk interface.
 *
 * The caller holds the trunk lock.
 *
 * @param   pThis           The trunk.
 * @param   pIfSender       The IF sending the frame.
 * @param   pSG             Pointer to the gather list.
 * @param   fDst            The destination flags.
 */
static int intnetR0TrunkIfSendGsoFallback(PINTNETTRUNKIF pThis, PINTNETIF pIfSender, PINTNETSG pSG, uint32_t fDst)
{
    /*
     * Since we're only using this for GSO frame coming from the internal
     * network interfaces and never the trunk, we can assume there is only
     * one segment.  This simplifies the code quite a bit.
     */
    Assert(PDMNetGsoIsValid(&pSG->GsoCtx, sizeof(pSG->GsoCtx), pSG->cbTotal));
    AssertReturn(pSG->cSegsUsed == 1, VERR_INTERNAL_ERROR_4);

    union
    {
        uint8_t     abBuf[sizeof(INTNETSG) + 2 * sizeof(INTNETSEG)];
        INTNETSG    SG;
    } u;

    /** @todo We have to adjust MSS so it does not exceed the value configured for
     * the host's interface.
     */

    /*
     * Carve out the frame segments with the header and frame in different
     * scatter / gather segments.
     */
    uint32_t const cSegs = PDMNetGsoCalcSegmentCount(&pSG->GsoCtx, pSG->cbTotal);
    for (uint32_t iSeg = 0; iSeg < cSegs; iSeg++)
    {
        uint32_t cbSegPayload, cbSegHdrs;
        uint32_t offSegPayload = PDMNetGsoCarveSegment(&pSG->GsoCtx, (uint8_t *)pSG->aSegs[0].pv, pSG->cbTotal, iSeg, cSegs,
                                                       pIfSender->abGsoHdrs, &cbSegHdrs, &cbSegPayload);

        IntNetSgInitTempSegs(&u.SG, cbSegHdrs + cbSegPayload, 2, 2);
        u.SG.aSegs[0].Phys = NIL_RTHCPHYS;
        u.SG.aSegs[0].pv   = pIfSender->abGsoHdrs;
        u.SG.aSegs[0].cb   = cbSegHdrs;
        u.SG.aSegs[1].Phys = NIL_RTHCPHYS;
        u.SG.aSegs[1].pv   = (uint8_t *)pSG->aSegs[0].pv + offSegPayload;
        u.SG.aSegs[1].cb   = (uint32_t)cbSegPayload;

        int rc = pThis->pIfPort->pfnXmit(pThis->pIfPort, pIfSender->pvIfData, &u.SG, fDst);
        if (RT_FAILURE(rc))
            return rc;
    }
    return VINF_SUCCESS;
}


/**
 * Checks if any of the given trunk destinations can handle this kind of GSO SG.
 *
 * @returns true if it can, false if it cannot.
 * @param   pThis               The trunk.
 * @param   pSG                 The scatter / gather buffer.
 * @param   fDst                The destination mask.
 */
DECLINLINE(bool) intnetR0TrunkIfCanHandleGsoFrame(PINTNETTRUNKIF pThis, PINTNETSG pSG, uint32_t fDst)
{
    uint8_t     u8Type = pSG->GsoCtx.u8Type;
    AssertReturn(u8Type < 32, false);   /* paranoia */
    uint32_t    fMask  = RT_BIT_32(u8Type);

    if (fDst == INTNETTRUNKDIR_HOST)
        return !!(pThis->fHostGsoCapabilites & fMask);
    if (fDst == INTNETTRUNKDIR_WIRE)
        return !!(pThis->fWireGsoCapabilites & fMask);
    Assert(fDst == (INTNETTRUNKDIR_WIRE | INTNETTRUNKDIR_HOST));
    return !!(pThis->fHostGsoCapabilites & pThis->fWireGsoCapabilites & fMask);
}


/**
 * Calculates the checksum of a full ipv6 frame.
 *
 * @returns 16-bit hecksum value.
 * @param   pIpHdr          The IPv6 header (network endian (big)).
 * @param   bProtocol       The protocol number.  This can be the same as the
 *                          ip6_nxt field, but doesn't need to be.
 * @param   cbPkt           The packet size (host endian of course).  This can
 *                          be the same as the ip6_plen field, but as with @a
 *                          bProtocol it won't be when extension headers are
 *                          present.  For UDP this will be uh_ulen converted to
 *                          host endian.
 */
static uint16_t computeIPv6FullChecksum(PCRTNETIPV6 pIpHdr)
{
    uint16_t const *data;
    int len         = RT_BE2H_U16(pIpHdr->ip6_plen);
    uint32_t sum    = RTNetIPv6PseudoChecksum(pIpHdr);

    /* add the payload */
    data = (uint16_t *) (pIpHdr + 1);
    while(len > 1)
    {
        sum += *(data);
        data++;
        len -= 2;
    }

    if(len > 0)
        sum += *((uint8_t *) data);

    while(sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);

    return (uint16_t) ~sum;
}


/**
 * Rewrite VM MAC address with shared host MAC address inside IPv6
 * Neighbor Discovery datagrams.
 */
static void intnetR0TrunkSharedMacEditIPv6FromIntNet(PINTNETTRUNKIF pThis, PINTNETIF pIfSender,
                                                     PRTNETETHERHDR pEthHdr, uint32_t cb)
{
    if (RT_UNLIKELY(cb < sizeof(*pEthHdr)))
        return;

    /* have IPv6 header */
    PRTNETIPV6 pIPv6 = (PRTNETIPV6)(pEthHdr + 1);
    cb -= sizeof(*pEthHdr);
    if (RT_UNLIKELY(cb < sizeof(*pIPv6)))
        return;

    if (   pIPv6->ip6_nxt  != RTNETIPV6_PROT_ICMPV6
        || pIPv6->ip6_hlim != 0xff)
        return;

    PRTNETICMPV6HDR pICMPv6 = (PRTNETICMPV6HDR)(pIPv6 + 1);
    cb -= sizeof(*pIPv6);
    if (RT_UNLIKELY(cb < sizeof(*pICMPv6)))
        return;

    uint32_t hdrlen = 0;
    uint8_t llaopt = RTNETIPV6_ICMP_ND_SLLA_OPT;

    uint8_t type = pICMPv6->icmp6_type;
    switch (type)
    {
        case RTNETIPV6_ICMP_TYPE_RS:
            hdrlen = 8;
            break;

        case RTNETIPV6_ICMP_TYPE_RA:
            hdrlen = 16;
            break;

        case RTNETIPV6_ICMP_TYPE_NS:
            hdrlen = 24;
            break;

        case RTNETIPV6_ICMP_TYPE_NA:
            hdrlen = 24;
            llaopt = RTNETIPV6_ICMP_ND_TLLA_OPT;
            break;

        default:
            return;
    }

    AssertReturnVoid(hdrlen > 0);
    if (RT_UNLIKELY(cb < hdrlen))
        return;

    if (RT_UNLIKELY(pICMPv6->icmp6_code != 0))
        return;

    PRTNETNDP_LLA_OPT pLLAOpt = NULL;
    char *pOpt = (char *)pICMPv6 + hdrlen;
    cb -= hdrlen;

    while (cb >= 8)
    {
        uint8_t opt = ((uint8_t *)pOpt)[0];
        uint32_t optlen = (uint32_t)((uint8_t *)pOpt)[1] * 8;
        if (RT_UNLIKELY(cb < optlen))
            return;

        if (opt == llaopt)
        {
            if (RT_UNLIKELY(optlen != 8))
                return;
            pLLAOpt = (PRTNETNDP_LLA_OPT)pOpt;
            break;
        }

        pOpt += optlen;
        cb -= optlen;
    }

    if (pLLAOpt == NULL)
        return;

    if (memcmp(&pLLAOpt->lla, &pIfSender->MacAddr, sizeof(RTMAC)) != 0)
        return;

    /* overwrite VM's MAC with host's MAC */
    pLLAOpt->lla = pThis->MacAddr;

    /* recompute the checksum */
    pICMPv6->icmp6_cksum = 0;
    pICMPv6->icmp6_cksum = computeIPv6FullChecksum(pIPv6);
}


/**
 * Sends a frame down the trunk.
 *
 * @param   pThis           The trunk.
 * @param   pNetwork        The network the frame is being sent to.
 * @param   pIfSender       The IF sending the frame.  Used for MAC address
 *                          checks in shared MAC mode.
 * @param   fDst            The destination flags.
 * @param   pSG             Pointer to the gather list.
 */
static void intnetR0TrunkIfSend(PINTNETTRUNKIF pThis, PINTNETNETWORK pNetwork, PINTNETIF pIfSender,
                                uint32_t fDst, PINTNETSG pSG)
{
    /*
     * Quick sanity check.
     */
    AssertPtr(pThis);
    AssertPtr(pNetwork);
    AssertPtr(pIfSender);
    AssertPtr(pSG);
    Assert(fDst);
    AssertReturnVoid(pThis->pIfPort);

    /*
     * Edit the frame if we're sharing the MAC address with the host on the wire.
     *
     * If the frame is headed for both the host and the wire, we'll have to send
     * it to the host before making any modifications, and force the OS specific
     * backend to copy it. We do this by marking it as TEMP (which is always the
     * case right now).
     */
    if (    (pNetwork->fFlags & INTNET_OPEN_FLAGS_SHARED_MAC_ON_WIRE)
        &&  (fDst & INTNETTRUNKDIR_WIRE))
    {
        /*
         * Dispatch it to the host before making changes.
         */
        if (fDst & INTNETTRUNKDIR_HOST)
        {
            Assert(pSG->fFlags & INTNETSG_FLAGS_TEMP); /* make sure copy is forced */
            intnetR0TrunkIfSend(pThis, pNetwork, pIfSender, INTNETTRUNKDIR_HOST, pSG);
            fDst &= ~INTNETTRUNKDIR_HOST;
        }

        /*
         * Edit the source address so that it it's the same as the host.
         */
        /* ASSUME frame from IntNetR0IfSend! */
        AssertReturnVoid(pSG->cSegsUsed == 1);
        AssertReturnVoid(pSG->cbTotal >= sizeof(RTNETETHERHDR));
        AssertReturnVoid(pIfSender);
        PRTNETETHERHDR pEthHdr = (PRTNETETHERHDR)pSG->aSegs[0].pv;

        pEthHdr->SrcMac = pThis->MacAddr;

        /*
         * Deal with tags from the snooping phase.
         */
        if (pSG->fFlags & INTNETSG_FLAGS_ARP_IPV4)
        {
            /*
             * APR IPv4: replace hardware (MAC) addresses because these end up
             *           in ARP caches. So, if we don't the other machines will
             *           send the packets to the MAC address of the guest
             *           instead of the one of the host, which won't work on
             *           wireless of course...
             */
            PRTNETARPIPV4 pArp = (PRTNETARPIPV4)(pEthHdr + 1);
            if (!memcmp(&pArp->ar_sha, &pIfSender->MacAddr, sizeof(RTMAC)))
            {
                Log6(("tw: ar_sha %.6Rhxs -> %.6Rhxs\n", &pArp->ar_sha, &pThis->MacAddr));
                pArp->ar_sha = pThis->MacAddr;
            }
            if (!memcmp(&pArp->ar_tha, &pIfSender->MacAddr, sizeof(RTMAC))) /* just in case... */
            {
                Log6(("tw: ar_tha %.6Rhxs -> %.6Rhxs\n", &pArp->ar_tha, &pThis->MacAddr));
                pArp->ar_tha = pThis->MacAddr;
            }
        }
        else if (pEthHdr->EtherType == RT_H2N_U16_C(RTNET_ETHERTYPE_IPV6))
        {
            intnetR0TrunkSharedMacEditIPv6FromIntNet(pThis, pIfSender, pEthHdr, pSG->cbTotal);
        }
    }

    /*
     * Send the frame, handling the GSO fallback.
     *
     * Note! The trunk implementation will re-check that the trunk is active
     *       before sending, so we don't have to duplicate that effort here.
     */
    STAM_REL_PROFILE_START(&pIfSender->pIntBuf->StatSend2, a);
    int rc;
    if (   pSG->GsoCtx.u8Type == PDMNETWORKGSOTYPE_INVALID
        || intnetR0TrunkIfCanHandleGsoFrame(pThis, pSG, fDst) )
        rc = pThis->pIfPort->pfnXmit(pThis->pIfPort, pIfSender->pvIfData, pSG, fDst);
    else
        rc = intnetR0TrunkIfSendGsoFallback(pThis, pIfSender, pSG, fDst);
    STAM_REL_PROFILE_STOP(&pIfSender->pIntBuf->StatSend2, a);

    /** @todo failure statistics? */
    Log2(("intnetR0TrunkIfSend: %Rrc fDst=%d\n", rc, fDst)); NOREF(rc);
}


/**
 * Detect broadcasts packaged as unicast and convert them back to broadcast.
 *
 * WiFi routers try to use ethernet unicast instead of broadcast or
 * multicast when possible.  Look inside the packet and fix up
 * ethernet destination to be proper broadcast or multicast if
 * necessary.
 *
 * @returns true broadcast (pEthHdr & pSG are modified), false if not.
 * @param   pNetwork        The network the frame is being sent to.
 * @param   pSG             Pointer to the gather list for the frame.  The
 *                          ethernet destination address is modified when
 *                          returning true.
 * @param   pEthHdr         Pointer to the ethernet header.  The ethernet
 *                          destination address is modified when returning true.
 */
static bool intnetR0NetworkSharedMacDetectAndFixBroadcast(PINTNETNETWORK pNetwork, PINTNETSG pSG, PRTNETETHERHDR pEthHdr)
{
    NOREF(pNetwork);

    switch (pEthHdr->EtherType)
    {
        case RT_H2N_U16_C(RTNET_ETHERTYPE_ARP):
        {
            uint16_t ar_oper;
            if (!intnetR0SgReadPart(pSG, sizeof(RTNETETHERHDR) + RT_UOFFSETOF(RTNETARPHDR, ar_oper),
                                    sizeof(ar_oper), &ar_oper))
                return false;

            if (ar_oper == RT_H2N_U16_C(RTNET_ARPOP_REQUEST))
            {
                /* change to broadcast */
                pEthHdr->DstMac.au16[0] = 0xffff;
                pEthHdr->DstMac.au16[1] = 0xffff;
                pEthHdr->DstMac.au16[2] = 0xffff;
            }
            else
                return false;
            break;
        }

        case RT_H2N_U16_C(RTNET_ETHERTYPE_IPV4):
        {
            RTNETADDRIPV4 ip_dst;
            if (!intnetR0SgReadPart(pSG, sizeof(RTNETETHERHDR) + RT_UOFFSETOF(RTNETIPV4, ip_dst),
                                    sizeof(ip_dst), &ip_dst))
                return false;

            if (ip_dst.u == 0xffffffff) /* 255.255.255.255? */
            {
                /* change to broadcast */
                pEthHdr->DstMac.au16[0] = 0xffff;
                pEthHdr->DstMac.au16[1] = 0xffff;
                pEthHdr->DstMac.au16[2] = 0xffff;
            }
            else if ((ip_dst.au8[0] & 0xf0) == 0xe0) /* IPv4 multicast? */
            {
                /* change to 01:00:5e:xx:xx:xx multicast ... */
                pEthHdr->DstMac.au8[0] = 0x01;
                pEthHdr->DstMac.au8[1] = 0x00;
                pEthHdr->DstMac.au8[2] = 0x5e;
                /* ... with lower 23 bits from the multicast IP address */
                pEthHdr->DstMac.au8[3] = ip_dst.au8[1] & 0x7f;
                pEthHdr->DstMac.au8[4] = ip_dst.au8[2];
                pEthHdr->DstMac.au8[5] = ip_dst.au8[3];
            }
            else
                return false;
            break;
        }

        case RT_H2N_U16_C(RTNET_ETHERTYPE_IPV6):
        {
            RTNETADDRIPV6 ip6_dst;
            if (!intnetR0SgReadPart(pSG, sizeof(RTNETETHERHDR) + RT_UOFFSETOF(RTNETIPV6, ip6_dst),
                                    sizeof(ip6_dst), &ip6_dst))
                return false;

            if (ip6_dst.au8[0] == 0xff) /* IPv6 multicast? */
            {
                pEthHdr->DstMac.au16[0] = 0x3333;
                pEthHdr->DstMac.au16[1] = ip6_dst.au16[6];
                pEthHdr->DstMac.au16[2] = ip6_dst.au16[7];
            }
            else
                return false;
            break;
        }

        default:
            return false;
    }


    /*
     * Update ethernet destination in the segment.
     */
    intnetR0SgWritePart(pSG, RT_UOFFSETOF(RTNETETHERHDR, DstMac), sizeof(pEthHdr->DstMac), &pEthHdr->DstMac);

    return true;
}


/**
 * Snoops a multicast ICMPv6 ND DAD from the wire via the trunk connection.
 *
 * @param   pNetwork        The network the frame is being sent to.
 * @param   pSG             Pointer to the gather list for the frame.
 * @param   pEthHdr         Pointer to the ethernet header.
 */
static void intnetR0NetworkSnoopNAFromWire(PINTNETNETWORK pNetwork, PINTNETSG pSG, PRTNETETHERHDR pEthHdr)
{
    NOREF(pEthHdr);

    /*
     * Check the minimum size and get a linear copy of the thing to work on,
     * using the temporary buffer if necessary.
     */
    if (RT_UNLIKELY(pSG->cbTotal < sizeof(RTNETETHERHDR) + sizeof(RTNETIPV6) +
                                            sizeof(RTNETNDP)))
        return;
    PRTNETIPV6 pIPv6 = (PRTNETIPV6)((uint8_t *)pSG->aSegs[0].pv + sizeof(RTNETETHERHDR));
    if (    pSG->cSegsUsed != 1
        &&  pSG->aSegs[0].cb < sizeof(RTNETETHERHDR) + sizeof(RTNETIPV6) +
                                                        sizeof(RTNETNDP))
    {
        Log6(("fw: Copying IPv6 pkt %u\n", sizeof(RTNETIPV6)));
        if (!intnetR0SgReadPart(pSG, sizeof(RTNETETHERHDR), sizeof(RTNETIPV6)
                                               + sizeof(RTNETNDP), pNetwork->pbTmp))
            return;
        pSG->fFlags |= INTNETSG_FLAGS_PKT_CP_IN_TMP;
        pIPv6 = (PRTNETIPV6)pNetwork->pbTmp;
    }

    PCRTNETNDP pNd  = (PCRTNETNDP) (pIPv6 + 1);

    /*
     * a multicast NS with :: as source address means a DAD packet.
     * if it comes from the wire and we have the DAD'd address in our cache,
     * flush the entry as the address is being acquired by someone else on
     * the network.
     */
    if (    pIPv6->ip6_hlim == 0xff
        &&  pIPv6->ip6_nxt  == RTNETIPV6_PROT_ICMPV6
        &&  pNd->Hdr.icmp6_type == RTNETIPV6_ICMP_TYPE_NS
        &&  pNd->Hdr.icmp6_code == 0
        &&  pIPv6->ip6_src.QWords.qw0 == 0
        &&  pIPv6->ip6_src.QWords.qw1 == 0)
    {

        intnetR0NetworkAddrCacheDelete(pNetwork, (PCRTNETADDRU) &pNd->target_address,
                                        kIntNetAddrType_IPv6, sizeof(RTNETADDRIPV6), "tif/ip6");
    }
}
/**
 * Edits an ARP packet arriving from the wire via the trunk connection.
 *
 * @param   pNetwork        The network the frame is being sent to.
 * @param   pSG             Pointer to the gather list for the frame.
 *                          The flags and data content may be updated.
 * @param   pEthHdr         Pointer to the ethernet header. This may also be
 *                          updated if it's a unicast...
 */
static void intnetR0NetworkEditArpFromWire(PINTNETNETWORK pNetwork, PINTNETSG pSG, PRTNETETHERHDR pEthHdr)
{
    /*
     * Check the minimum size and get a linear copy of the thing to work on,
     * using the temporary buffer if necessary.
     */
    if (RT_UNLIKELY(pSG->cbTotal < sizeof(RTNETETHERHDR) + sizeof(RTNETARPIPV4)))
        return;
    PRTNETARPIPV4 pArpIPv4 = (PRTNETARPIPV4)((uint8_t *)pSG->aSegs[0].pv + sizeof(RTNETETHERHDR));
    if (    pSG->cSegsUsed != 1
        &&  pSG->aSegs[0].cb < sizeof(RTNETETHERHDR) + sizeof(RTNETARPIPV4))
    {
        Log6(("fw: Copying ARP pkt %u\n", sizeof(RTNETARPIPV4)));
        if (!intnetR0SgReadPart(pSG, sizeof(RTNETETHERHDR), sizeof(RTNETARPIPV4), pNetwork->pbTmp))
            return;
        pSG->fFlags |= INTNETSG_FLAGS_PKT_CP_IN_TMP;
        pArpIPv4 = (PRTNETARPIPV4)pNetwork->pbTmp;
    }

    /*
     * Ignore packets which doesn't interest us or we perceive as malformed.
     */
    if (RT_UNLIKELY(    pArpIPv4->Hdr.ar_hlen  != sizeof(RTMAC)
                    ||  pArpIPv4->Hdr.ar_plen  != sizeof(RTNETADDRIPV4)
                    ||  pArpIPv4->Hdr.ar_htype != RT_H2BE_U16(RTNET_ARP_ETHER)
                    ||  pArpIPv4->Hdr.ar_ptype != RT_H2BE_U16(RTNET_ETHERTYPE_IPV4)))
        return;
    uint16_t ar_oper = RT_H2BE_U16(pArpIPv4->Hdr.ar_oper);
    if (RT_UNLIKELY(    ar_oper != RTNET_ARPOP_REQUEST
                    &&  ar_oper != RTNET_ARPOP_REPLY))
    {
        Log6(("ar_oper=%#x\n", ar_oper));
        return;
    }

    /* Tag it as ARP IPv4. */
    pSG->fFlags |= INTNETSG_FLAGS_ARP_IPV4;

    /*
     * The thing we're interested in here is a reply to a query made by a guest
     * since we modified the MAC in the initial request the guest made.
     */
    RTSpinlockAcquire(pNetwork->hAddrSpinlock);
    RTMAC MacAddrTrunk;
    if (pNetwork->MacTab.pTrunk)
        MacAddrTrunk = pNetwork->MacTab.pTrunk->MacAddr;
    else
        memset(&MacAddrTrunk, 0, sizeof(MacAddrTrunk));
    RTSpinlockRelease(pNetwork->hAddrSpinlock);
    if (    ar_oper == RTNET_ARPOP_REPLY
        &&  !memcmp(&pArpIPv4->ar_tha, &MacAddrTrunk, sizeof(RTMAC)))
    {
        PINTNETIF pIf = intnetR0NetworkAddrCacheLookupIf(pNetwork, (PCRTNETADDRU)&pArpIPv4->ar_tpa,
                                                         kIntNetAddrType_IPv4, sizeof(pArpIPv4->ar_tpa));
        if (pIf)
        {
            Log6(("fw: ar_tha %.6Rhxs -> %.6Rhxs\n", &pArpIPv4->ar_tha, &pIf->MacAddr));
            pArpIPv4->ar_tha = pIf->MacAddr;
            if (!memcmp(&pEthHdr->DstMac, &MacAddrTrunk, sizeof(RTMAC)))
            {
                Log6(("fw: DstMac %.6Rhxs -> %.6Rhxs\n", &pEthHdr->DstMac, &pIf->MacAddr));
                pEthHdr->DstMac = pIf->MacAddr;
                if ((void *)pEthHdr != pSG->aSegs[0].pv)
                    intnetR0SgWritePart(pSG, RT_UOFFSETOF(RTNETETHERHDR, DstMac), sizeof(RTMAC), &pIf->MacAddr);
            }
            intnetR0BusyDecIf(pIf);

            /* Write back the packet if we've been making changes to a buffered copy. */
            if (pSG->fFlags & INTNETSG_FLAGS_PKT_CP_IN_TMP)
                intnetR0SgWritePart(pSG, sizeof(RTNETETHERHDR), sizeof(PRTNETARPIPV4), pArpIPv4);
        }
    }
}


/**
 * Detects and edits an DHCP packet arriving from the internal net.
 *
 * @param   pNetwork        The network the frame is being sent to.
 * @param   pSG             Pointer to the gather list for the frame.
 *                          The flags and data content may be updated.
 * @param   pEthHdr         Pointer to the ethernet header. This may also be
 *                          updated if it's a unicast...
 */
static void intnetR0NetworkEditDhcpFromIntNet(PINTNETNETWORK pNetwork, PINTNETSG pSG, PRTNETETHERHDR pEthHdr)
{
    NOREF(pEthHdr);

    /*
     * Check the minimum size and get a linear copy of the thing to work on,
     * using the temporary buffer if necessary.
     */
    if (RT_UNLIKELY(pSG->cbTotal < sizeof(RTNETETHERHDR) + RTNETIPV4_MIN_LEN + RTNETUDP_MIN_LEN + RTNETBOOTP_DHCP_MIN_LEN))
        return;
    /*
     * Get a pointer to a linear copy of the full packet, using the
     * temporary buffer if necessary.
     */
    PCRTNETIPV4 pIpHdr = (PCRTNETIPV4)((PCRTNETETHERHDR)pSG->aSegs[0].pv + 1);
    uint32_t cbPacket = pSG->cbTotal - sizeof(RTNETETHERHDR);
    if (pSG->cSegsUsed > 1)
    {
        cbPacket = RT_MIN(cbPacket, INTNETNETWORK_TMP_SIZE);
        Log6(("intnetR0NetworkEditDhcpFromIntNet: Copying IPv4/UDP/DHCP pkt %u\n", cbPacket));
        if (!intnetR0SgReadPart(pSG, sizeof(RTNETETHERHDR), cbPacket, pNetwork->pbTmp))
            return;
        //pSG->fFlags |= INTNETSG_FLAGS_PKT_CP_IN_TMP;
        pIpHdr = (PCRTNETIPV4)pNetwork->pbTmp;
    }

    /*
     * Validate the IP header and find the UDP packet.
     */
    if (!RTNetIPv4IsHdrValid(pIpHdr, cbPacket, pSG->cbTotal - sizeof(RTNETETHERHDR), true /*fCheckSum*/))
    {
        Log6(("intnetR0NetworkEditDhcpFromIntNet: bad ip header\n"));
        return;
    }
    size_t cbIpHdr = pIpHdr->ip_hl * 4;
    if (    pIpHdr->ip_p != RTNETIPV4_PROT_UDP                               /* DHCP is UDP. */
        ||  cbPacket < cbIpHdr + RTNETUDP_MIN_LEN + RTNETBOOTP_DHCP_MIN_LEN) /* Min DHCP packet len */
        return;

    size_t cbUdpPkt = cbPacket - cbIpHdr;
    PCRTNETUDP pUdpHdr = (PCRTNETUDP)((uintptr_t)pIpHdr + cbIpHdr);
    /* We are only interested in DHCP packets coming from client to server. */
    if (    RT_BE2H_U16(pUdpHdr->uh_dport) != RTNETIPV4_PORT_BOOTPS
         || RT_BE2H_U16(pUdpHdr->uh_sport) != RTNETIPV4_PORT_BOOTPC)
        return;

    /*
     * Check if the DHCP message is valid and get the type.
     */
    if (!RTNetIPv4IsUDPValid(pIpHdr, pUdpHdr, pUdpHdr + 1, cbUdpPkt, true /*fCheckSum*/))
    {
        Log6(("intnetR0NetworkEditDhcpFromIntNet: Bad UDP packet\n"));
        return;
    }
    PCRTNETBOOTP pDhcp = (PCRTNETBOOTP)(pUdpHdr + 1);
    uint8_t bMsgType;
    if (!RTNetIPv4IsDHCPValid(pUdpHdr, pDhcp, cbUdpPkt - sizeof(*pUdpHdr), &bMsgType))
    {
        Log6(("intnetR0NetworkEditDhcpFromIntNet: Bad DHCP packet\n"));
        return;
    }

    switch (bMsgType)
    {
        case RTNET_DHCP_MT_DISCOVER:
        case RTNET_DHCP_MT_REQUEST:
            /*
             * Must set the broadcast flag or we won't catch the respons.
             */
            if (!(pDhcp->bp_flags & RT_H2BE_U16_C(RTNET_DHCP_FLAG_BROADCAST)))
            {
                Log6(("intnetR0NetworkEditDhcpFromIntNet: Setting broadcast flag in DHCP %#x, previously %x\n",
                      bMsgType, pDhcp->bp_flags));

                /* Patch flags */
                uint16_t uFlags = pDhcp->bp_flags | RT_H2BE_U16_C(RTNET_DHCP_FLAG_BROADCAST);
                intnetR0SgWritePart(pSG, (uintptr_t)&pDhcp->bp_flags - (uintptr_t)pIpHdr + sizeof(RTNETETHERHDR), sizeof(uFlags), &uFlags);

                /* Patch UDP checksum */
                if (pUdpHdr->uh_sum != 0)
                {
                    uint32_t uChecksum = (uint32_t)~pUdpHdr->uh_sum + RT_H2BE_U16_C(RTNET_DHCP_FLAG_BROADCAST);
                    while (uChecksum >> 16)
                        uChecksum = (uChecksum >> 16) + (uChecksum & 0xFFFF);
                    uChecksum = ~uChecksum;
                    intnetR0SgWritePart(pSG,
                                        (uintptr_t)&pUdpHdr->uh_sum - (uintptr_t)pIpHdr + sizeof(RTNETETHERHDR),
                                        sizeof(pUdpHdr->uh_sum),
                                        &uChecksum);
                }
            }

#ifdef RT_OS_DARWIN
            /*
             * Work around little endian checksum issue in mac os x 10.7.0 GM.
             */
            if (   pIpHdr->ip_tos
                && (pNetwork->fFlags & INTNET_OPEN_FLAGS_WORKAROUND_1))
            {
                /* Patch it. */
                uint8_t uTos  = pIpHdr->ip_tos;
                uint8_t uZero = 0;
                intnetR0SgWritePart(pSG, sizeof(RTNETETHERHDR) + 1, sizeof(uZero), &uZero);

                /* Patch the IP header checksum. */
                uint32_t uChecksum = (uint32_t)~pIpHdr->ip_sum - (uTos << 8);
                while (uChecksum >> 16)
                    uChecksum = (uChecksum >> 16) + (uChecksum & 0xFFFF);
                uChecksum = ~uChecksum;

                Log(("intnetR0NetworkEditDhcpFromIntNet: cleared ip_tos (was %#04x); ip_sum=%#06x -> %#06x\n",
                     uTos, RT_BE2H_U16(pIpHdr->ip_sum), RT_BE2H_U16(uChecksum) ));
                intnetR0SgWritePart(pSG, sizeof(RTNETETHERHDR) + RT_UOFFSETOF(RTNETIPV4, ip_sum),
                                    sizeof(pIpHdr->ip_sum), &uChecksum);
            }
#endif
            break;
    }
}


/**
 * Checks if the callers context is okay for sending to the specified
 * destinations.
 *
 * @returns true if it's okay, false if it isn't.
 * @param   pNetwork            The network.
 * @param   pIfSender           The interface sending or NULL if it's the trunk.
 * @param   pDstTab             The destination table.
 */
DECLINLINE(bool) intnetR0NetworkIsContextOk(PINTNETNETWORK pNetwork, PINTNETIF pIfSender, PCINTNETDSTTAB pDstTab)
{
    NOREF(pNetwork);

    /* Sending to the trunk is the problematic path.  If the trunk is the
       sender we won't be sending to it, so no problem..
       Note! fTrunkDst may be set event if if the trunk is the sender. */
    if (!pIfSender)
        return true;

    uint32_t const fTrunkDst = pDstTab->fTrunkDst;
    if (!fTrunkDst)
        return true;

    /* ASSUMES: that the trunk won't change its report while we're checking. */
    PINTNETTRUNKIF  pTrunk = pDstTab->pTrunk;
    if (pTrunk && (fTrunkDst & pTrunk->fNoPreemptDsts) == fTrunkDst)
        return true;

    /* ASSUMES: That a preemption test detects HM contexts. (Will work on
                non-preemptive systems as well.) */
    if (RTThreadPreemptIsEnabled(NIL_RTTHREAD))
        return true;
    return false;
}


/**
 * Checks if the callers context is okay for doing a broadcast given the
 * specified source.
 *
 * @returns true if it's okay, false if it isn't.
 * @param   pNetwork            The network.
 * @param   fSrc                The source of the packet.  (0 (intnet),
 *                              INTNETTRUNKDIR_HOST or INTNETTRUNKDIR_WIRE).
 */
DECLINLINE(bool) intnetR0NetworkIsContextOkForBroadcast(PINTNETNETWORK pNetwork, uint32_t fSrc)
{
    /* Sending to the trunk is the problematic path.  If the trunk is the
       sender we won't be sending to it, so no problem. */
    if (fSrc)
        return true;

    /* ASSUMES: That a preemption test detects HM contexts. (Will work on
                non-preemptive systems as well.) */
    if (RTThreadPreemptIsEnabled(NIL_RTTHREAD))
        return true;

    /* PARANOIA: Grab the spinlock to make sure the trunk structure cannot be
                 freed while we're touching it. */
    RTSpinlockAcquire(pNetwork->hAddrSpinlock);
    PINTNETTRUNKIF pTrunk = pNetwork->MacTab.pTrunk;

    bool fRc = !pTrunk
            || pTrunk->fNoPreemptDsts == (INTNETTRUNKDIR_HOST | INTNETTRUNKDIR_WIRE)
            || (   (!pNetwork->MacTab.fHostActive || (pTrunk->fNoPreemptDsts & INTNETTRUNKDIR_HOST) )
                && (!pNetwork->MacTab.fWireActive || (pTrunk->fNoPreemptDsts & INTNETTRUNKDIR_WIRE) ) );

    RTSpinlockRelease(pNetwork->hAddrSpinlock);

    return fRc;
}


/**
 * Check context, edit, snoop and switch a broadcast frame when sharing MAC
 * address on the wire.
 *
 * The caller must hold at least one interface on the network busy to prevent it
 * from destructing beath us.
 *
 * @param   pNetwork            The network the frame is being sent to.
 * @param   fSrc                The source of the packet.  (0 (intnet),
 *                              INTNETTRUNKDIR_HOST or INTNETTRUNKDIR_WIRE).
 * @param   pIfSender           The sender interface, NULL if trunk.  Used to
 *                              prevent sending an echo to the sender.
 * @param   pSG                 Pointer to the gather list.
 * @param   pEthHdr             Pointer to the ethernet header.
 * @param   pDstTab             The destination output table.
 */
static INTNETSWDECISION intnetR0NetworkSharedMacFixAndSwitchBroadcast(PINTNETNETWORK pNetwork,
                                                                      uint32_t fSrc, PINTNETIF pIfSender,
                                                                      PINTNETSG pSG, PRTNETETHERHDR pEthHdr,
                                                                      PINTNETDSTTAB pDstTab)
{
    /*
     * Before doing any work here, we need to figure out if we can handle it
     * in the current context.  The restrictions are solely on the trunk.
     *
     * Note! Since at least one interface is busy, there won't be any changes
     *       to the parameters here (unless the trunk changes its capability
     *       report, which it shouldn't).
     */
    if (!intnetR0NetworkIsContextOkForBroadcast(pNetwork, fSrc))
        return INTNETSWDECISION_BAD_CONTEXT;

    /*
     * Check for ICMPv6 Neighbor Advertisements coming from the trunk.
     * If we see an advertisement for an IP in our cache, we can safely remove
     * it as the IP has probably moved.
     */
    if (    (fSrc & INTNETTRUNKDIR_WIRE)
        &&  RT_BE2H_U16(pEthHdr->EtherType) == RTNET_ETHERTYPE_IPV6
        &&  pSG->GsoCtx.u8Type == PDMNETWORKGSOTYPE_INVALID)
        intnetR0NetworkSnoopNAFromWire(pNetwork, pSG, pEthHdr);


    /*
     * Check for ARP packets from the wire since we'll have to make
     * modification to them if we're sharing the MAC address with the host.
     */
    if (    (fSrc & INTNETTRUNKDIR_WIRE)
        &&  RT_BE2H_U16(pEthHdr->EtherType) == RTNET_ETHERTYPE_ARP
        &&  pSG->GsoCtx.u8Type == PDMNETWORKGSOTYPE_INVALID)
        intnetR0NetworkEditArpFromWire(pNetwork, pSG, pEthHdr);

    /*
     * Check for DHCP packets from the internal net since we'll have to set
     * broadcast flag in DHCP requests if we're sharing the MAC address with
     * the host.  GSO is not applicable to DHCP traffic.
     */
    if (    !fSrc
        &&  RT_BE2H_U16(pEthHdr->EtherType) == RTNET_ETHERTYPE_IPV4
        &&  pSG->GsoCtx.u8Type == PDMNETWORKGSOTYPE_INVALID)
        intnetR0NetworkEditDhcpFromIntNet(pNetwork, pSG, pEthHdr);

    /*
     * Snoop address info from packet originating from the trunk connection.
     */
    if (fSrc)
    {
#ifdef INTNET_WITH_DHCP_SNOOPING
        uint16_t EtherType = RT_BE2H_U16(pEthHdr->EtherType);
        if (    (   EtherType == RTNET_ETHERTYPE_IPV4       /* for DHCP */
                 && pSG->cbTotal >= sizeof(RTNETETHERHDR) + RTNETIPV4_MIN_LEN + RTNETUDP_MIN_LEN + RTNETBOOTP_DHCP_MIN_LEN
                 && pSG->GsoCtx.u8Type == PDMNETWORKGSOTYPE_INVALID )
            ||  (pSG->fFlags & INTNETSG_FLAGS_ARP_IPV4) )
            intnetR0TrunkIfSnoopAddr(pNetwork, pSG, EtherType);
#else
       if (pSG->fFlags & INTNETSG_FLAGS_ARP_IPV4)
           intnetR0TrunkIfSnoopArp(pNetwork, pSG);
#endif
    }

    /*
     * Create the broadcast destination table.
     */
    return intnetR0NetworkSwitchBroadcast(pNetwork, fSrc, pIfSender, pDstTab);
}


/**
 * Check context, snoop and switch a unicast frame using the network layer
 * address of the link layer one (when sharing MAC address on the wire).
 *
 * This function is only used for frames coming from the wire (trunk).
 *
 * @returns true if it's addressed to someone on the network, otherwise false.
 * @param   pNetwork        The network the frame is being sent to.
 * @param   pSG             Pointer to the gather list.
 * @param   pEthHdr         Pointer to the ethernet header.
 * @param   pDstTab         The destination output table.
 */
static INTNETSWDECISION intnetR0NetworkSharedMacFixAndSwitchUnicast(PINTNETNETWORK pNetwork, PINTNETSG pSG,
                                                                    PRTNETETHERHDR pEthHdr, PINTNETDSTTAB pDstTab)
{
    /*
     * Extract the network address from the packet.
     */
    RTNETADDRU      Addr;
    INTNETADDRTYPE  enmAddrType;
    uint8_t         cbAddr;
    switch (RT_BE2H_U16(pEthHdr->EtherType))
    {
        case RTNET_ETHERTYPE_IPV4:
            if (RT_UNLIKELY(!intnetR0SgReadPart(pSG, sizeof(RTNETETHERHDR) + RT_UOFFSETOF(RTNETIPV4, ip_dst), sizeof(Addr.IPv4), &Addr)))
            {
                Log(("intnetshareduni: failed to read ip_dst! cbTotal=%#x\n", pSG->cbTotal));
                return intnetR0NetworkSwitchTrunk(pNetwork, INTNETTRUNKDIR_WIRE, pDstTab);
            }
            enmAddrType = kIntNetAddrType_IPv4;
            cbAddr = sizeof(Addr.IPv4);
            Log6(("intnetshareduni: IPv4 %d.%d.%d.%d\n", Addr.au8[0], Addr.au8[1], Addr.au8[2], Addr.au8[3]));
            break;

        case RTNET_ETHERTYPE_IPV6:
            if (RT_UNLIKELY(!intnetR0SgReadPart(pSG, sizeof(RTNETETHERHDR) + RT_UOFFSETOF(RTNETIPV6, ip6_dst), sizeof(Addr.IPv6), &Addr)))
            {
                Log(("intnetshareduni: failed to read ip6_dst! cbTotal=%#x\n", pSG->cbTotal));
                return intnetR0NetworkSwitchTrunk(pNetwork, INTNETTRUNKDIR_WIRE, pDstTab);
            }
            enmAddrType = kIntNetAddrType_IPv6;
            cbAddr = sizeof(Addr.IPv6);
            break;
#if 0 /** @todo IntNet: implement IPX for wireless MAC sharing? */
        case RTNET_ETHERTYPE_IPX_1:
        case RTNET_ETHERTYPE_IPX_2:
        case RTNET_ETHERTYPE_IPX_3:
            if (RT_UNLIKELY(!intnetR0SgReadPart(pSG, sizeof(RTNETETHERHDR) + RT_OFFSETOF(RTNETIPX, ipx_dstnet), sizeof(Addr.IPX), &Addr)))
            {
                Log(("intnetshareduni: failed to read ipx_dstnet! cbTotal=%#x\n", pSG->cbTotal));
                return intnetR0NetworkSwitchTrunk(pNetwork, INTNETTRUNKDIR_WIRE, pDstTab);
            }
            enmAddrType = kIntNetAddrType_IPX;
            cbAddr = sizeof(Addr.IPX);
            break;
#endif

        /*
         * Treat ARP as broadcast (it shouldn't end up here normally,
         * so it goes last in the switch).
         */
        case RTNET_ETHERTYPE_ARP:
            Log6(("intnetshareduni: ARP\n"));
            /** @todo revisit this broadcasting of unicast ARP frames! */
            return intnetR0NetworkSharedMacFixAndSwitchBroadcast(pNetwork, INTNETTRUNKDIR_WIRE, NULL, pSG, pEthHdr, pDstTab);

        /*
         * Unknown packets are sent to the trunk and any promiscuous interfaces.
         */
        default:
        {
            Log6(("intnetshareduni: unknown ethertype=%#x\n", RT_BE2H_U16(pEthHdr->EtherType)));
            return intnetR0NetworkSwitchTrunkAndPromisc(pNetwork, INTNETTRUNKDIR_WIRE, pDstTab);
        }
    }

    /*
     * Do level-3 switching.
     */
    INTNETSWDECISION enmSwDecision = intnetR0NetworkSwitchLevel3(pNetwork, &pEthHdr->DstMac,
                                                                 enmAddrType, &Addr, cbAddr,
                                                                 INTNETTRUNKDIR_WIRE, pDstTab);

#ifdef INTNET_WITH_DHCP_SNOOPING
    /*
     * Perform DHCP snooping. GSO is not applicable to DHCP traffic
     */
    if (    enmAddrType == kIntNetAddrType_IPv4
        &&  pSG->cbTotal >= sizeof(RTNETETHERHDR) + RTNETIPV4_MIN_LEN + RTNETUDP_MIN_LEN + RTNETBOOTP_DHCP_MIN_LEN
        &&  pSG->GsoCtx.u8Type == PDMNETWORKGSOTYPE_INVALID)
        intnetR0TrunkIfSnoopAddr(pNetwork, pSG, RT_BE2H_U16(pEthHdr->EtherType));
#endif /* INTNET_WITH_DHCP_SNOOPING */

    return enmSwDecision;
}


/**
 * Release all the interfaces in the destination table when we realize that
 * we're in a context where we cannot get the job done.
 *
 * @param   pNetwork            The network.
 * @param   pDstTab             The destination table.
 */
static void intnetR0NetworkReleaseDstTab(PINTNETNETWORK pNetwork, PINTNETDSTTAB pDstTab)
{
    /* The trunk interface. */
    if (pDstTab->fTrunkDst)
    {
        PINTNETTRUNKIF pTrunk = pDstTab->pTrunk;
        if (pTrunk)
            intnetR0BusyDec(pNetwork, &pTrunk->cBusy);
        pDstTab->pTrunk    = NULL;
        pDstTab->fTrunkDst = 0;
    }

    /* Regular interfaces. */
    uint32_t iIf = pDstTab->cIfs;
    while (iIf-- > 0)
    {
        PINTNETIF pIf = pDstTab->aIfs[iIf].pIf;
        intnetR0BusyDecIf(pIf);
        pDstTab->aIfs[iIf].pIf = NULL;
    }
    pDstTab->cIfs = 0;
}


/**
 * Deliver the frame to the interfaces specified in the destination table.
 *
 * @param   pNetwork            The network.
 * @param   pDstTab             The destination table.
 * @param   pSG                 The frame to send.
 * @param   pIfSender           The sender interface.  NULL if it originated via
 *                              the trunk.
 */
static void intnetR0NetworkDeliver(PINTNETNETWORK pNetwork, PINTNETDSTTAB pDstTab, PINTNETSG pSG, PINTNETIF pIfSender)
{
    /*
     * Do the interfaces first before sending it to the wire and risk having to
     * modify it.
     */
    uint32_t iIf = pDstTab->cIfs;
    while (iIf-- > 0)
    {
        PINTNETIF pIf = pDstTab->aIfs[iIf].pIf;
        intnetR0IfSend(pIf, pIfSender, pSG,
                       pDstTab->aIfs[iIf].fReplaceDstMac ? &pIf->MacAddr: NULL);
        intnetR0BusyDecIf(pIf);
        pDstTab->aIfs[iIf].pIf = NULL;
    }
    pDstTab->cIfs = 0;

    /*
     * Send to the trunk.
     *
     * Note! The switching functions will include the trunk even when the frame
     *       source is the trunk.  This is because we need it to figure out
     *       whether the other half of the trunk should see the frame or not
     *       and let the caller know.
     *
     *       So, we'll ignore trunk sends here if the frame origin is
     *       INTNETTRUNKSWPORT::pfnRecv.
     */
    if (pDstTab->fTrunkDst)
    {
        PINTNETTRUNKIF pTrunk = pDstTab->pTrunk;
        if (pTrunk)
        {
            if (pIfSender)
                intnetR0TrunkIfSend(pTrunk, pNetwork, pIfSender, pDstTab->fTrunkDst, pSG);
            intnetR0BusyDec(pNetwork, &pTrunk->cBusy);
        }
        pDstTab->pTrunk    = NULL;
        pDstTab->fTrunkDst = 0;
    }
}


/**
 * Sends a frame.
 *
 * This function will distribute the frame to the interfaces it is addressed to.
 * It will also update the MAC address of the sender.
 *
 * The caller must own the network mutex.
 *
 * @returns The switching decision.
 * @param   pNetwork        The network the frame is being sent to.
 * @param   pIfSender       The interface sending the frame. This is NULL if it's the trunk.
 * @param   fSrc            The source flags. This 0 if it's not from the trunk.
 * @param   pSG             Pointer to the gather list.
 * @param   pDstTab         The destination table to use.
 */
static INTNETSWDECISION intnetR0NetworkSend(PINTNETNETWORK pNetwork, PINTNETIF pIfSender, uint32_t fSrc,
                                            PINTNETSG pSG, PINTNETDSTTAB pDstTab)
{
    /*
     * Assert reality.
     */
    AssertPtr(pNetwork);
    AssertPtrNull(pIfSender);
    Assert(pIfSender ? fSrc == 0 : fSrc != 0);
    Assert(!pIfSender || pNetwork == pIfSender->pNetwork);
    AssertPtr(pSG);
    Assert(pSG->cSegsUsed >= 1);
    Assert(pSG->cSegsUsed <= pSG->cSegsAlloc);
    if (pSG->cbTotal < sizeof(RTNETETHERHDR))
        return INTNETSWDECISION_INVALID;

    /*
     * Get the ethernet header (might theoretically involve multiple segments).
     */
    RTNETETHERHDR EthHdr;
    if (pSG->aSegs[0].cb >= sizeof(EthHdr))
        EthHdr = *(PCRTNETETHERHDR)pSG->aSegs[0].pv;
    else if (!intnetR0SgReadPart(pSG, 0, sizeof(EthHdr), &EthHdr))
        return INTNETSWDECISION_INVALID;
    if (    (EthHdr.DstMac.au8[0] == 0x08 && EthHdr.DstMac.au8[1] == 0x00 && EthHdr.DstMac.au8[2] == 0x27)
        ||  (EthHdr.SrcMac.au8[0] == 0x08 && EthHdr.SrcMac.au8[1] == 0x00 && EthHdr.SrcMac.au8[2] == 0x27)
        ||  (EthHdr.DstMac.au8[0] == 0x00 && EthHdr.DstMac.au8[1] == 0x16 && EthHdr.DstMac.au8[2] == 0xcb)
        ||  (EthHdr.SrcMac.au8[0] == 0x00 && EthHdr.SrcMac.au8[1] == 0x16 && EthHdr.SrcMac.au8[2] == 0xcb)
        ||  EthHdr.DstMac.au8[0] == 0xff
        ||  EthHdr.SrcMac.au8[0] == 0xff)
        Log2(("D=%.6Rhxs  S=%.6Rhxs  T=%04x f=%x z=%x\n",
              &EthHdr.DstMac, &EthHdr.SrcMac, RT_BE2H_U16(EthHdr.EtherType), fSrc, pSG->cbTotal));

    /*
     * Learn the MAC address of the sender.  No re-learning as the interface
     * user will normally tell us the right MAC address.
     *
     * Note! We don't notify the trunk about these mainly because of the
     *       problematic contexts we might be called in.
     */
    if (RT_UNLIKELY(    pIfSender
                    &&  !pIfSender->fMacSet
                    &&  memcmp(&EthHdr.SrcMac, &pIfSender->MacAddr, sizeof(pIfSender->MacAddr))
                    &&  !intnetR0IsMacAddrMulticast(&EthHdr.SrcMac)
                    ))
    {
        Log2(("IF MAC: %.6Rhxs -> %.6Rhxs\n", &pIfSender->MacAddr, &EthHdr.SrcMac));
        RTSpinlockAcquire(pNetwork->hAddrSpinlock);

        PINTNETMACTABENTRY pIfEntry = intnetR0NetworkFindMacAddrEntry(pNetwork, pIfSender);
        if (pIfEntry)
            pIfEntry->MacAddr = EthHdr.SrcMac;
        pIfSender->MacAddr    = EthHdr.SrcMac;

        RTSpinlockRelease(pNetwork->hAddrSpinlock);
    }

    /*
     * Deal with MAC address sharing as that may required editing of the
     * packets before we dispatch them anywhere.
     */
    INTNETSWDECISION enmSwDecision;
    if (pNetwork->fFlags & INTNET_OPEN_FLAGS_SHARED_MAC_ON_WIRE)
    {
        if (intnetR0IsMacAddrMulticast(&EthHdr.DstMac))
            enmSwDecision = intnetR0NetworkSharedMacFixAndSwitchBroadcast(pNetwork, fSrc, pIfSender, pSG, &EthHdr, pDstTab);
        else if (fSrc & INTNETTRUNKDIR_WIRE)
        {
            if (intnetR0NetworkSharedMacDetectAndFixBroadcast(pNetwork, pSG, &EthHdr))
                enmSwDecision = intnetR0NetworkSharedMacFixAndSwitchBroadcast(pNetwork, fSrc, pIfSender, pSG, &EthHdr, pDstTab);
            else
                enmSwDecision = intnetR0NetworkSharedMacFixAndSwitchUnicast(pNetwork, pSG, &EthHdr, pDstTab);
        }
        else
            enmSwDecision = intnetR0NetworkSwitchUnicast(pNetwork, fSrc, pIfSender, &EthHdr.DstMac, pDstTab);
    }
    else if (intnetR0IsMacAddrMulticast(&EthHdr.DstMac))
        enmSwDecision = intnetR0NetworkSwitchBroadcast(pNetwork, fSrc, pIfSender, pDstTab);
    else
        enmSwDecision = intnetR0NetworkSwitchUnicast(pNetwork, fSrc, pIfSender, &EthHdr.DstMac, pDstTab);

    /*
     * Deliver to the destinations if we can.
     */
    if (enmSwDecision != INTNETSWDECISION_BAD_CONTEXT)
    {
        if (intnetR0NetworkIsContextOk(pNetwork, pIfSender, pDstTab))
            intnetR0NetworkDeliver(pNetwork, pDstTab, pSG, pIfSender);
        else
        {
            intnetR0NetworkReleaseDstTab(pNetwork, pDstTab);
            enmSwDecision = INTNETSWDECISION_BAD_CONTEXT;
        }
    }

    return enmSwDecision;
}


/**
 * Sends one or more frames.
 *
 * The function will first the frame which is passed as the optional arguments
 * pvFrame and cbFrame. These are optional since it also possible to chain
 * together one or more frames in the send buffer which the function will
 * process after considering it's arguments.
 *
 * The caller is responsible for making sure that there are no concurrent calls
 * to this method (with the same handle).
 *
 * @returns VBox status code.
 * @param   hIf         The interface handle.
 * @param   pSession    The caller's session.
 */
INTNETR0DECL(int) IntNetR0IfSend(INTNETIFHANDLE hIf, PSUPDRVSESSION pSession)
{
    Log5(("IntNetR0IfSend: hIf=%RX32\n", hIf));

    /*
     * Validate input and translate the handle.
     */
    PINTNET pIntNet = g_pIntNet;
    AssertPtrReturn(pIntNet, VERR_INVALID_PARAMETER);
    AssertReturn(pIntNet->u32Magic, VERR_INVALID_MAGIC);

    PINTNETIF pIf = (PINTNETIF)RTHandleTableLookupWithCtx(pIntNet->hHtIfs, hIf, pSession);
    if (!pIf)
        return VERR_INVALID_HANDLE;
    STAM_REL_PROFILE_START(&pIf->pIntBuf->StatSend1, a);

    /*
     * Make sure we've got a network.
     */
    int rc  = VINF_SUCCESS;
    intnetR0BusyIncIf(pIf);
    PINTNETNETWORK pNetwork = pIf->pNetwork;
    if (RT_LIKELY(pNetwork))
    {
        /*
         * Grab the destination table.
         */
        PINTNETDSTTAB pDstTab = ASMAtomicXchgPtrT(&pIf->pDstTab, NULL, PINTNETDSTTAB);
        if (RT_LIKELY(pDstTab))
        {
            /*
             * Process the send buffer.
             */
            INTNETSWDECISION    enmSwDecision = INTNETSWDECISION_BROADCAST;

            /** @todo this will have to be changed if we're going to use async sending
             * with buffer sharing for some OS or service. Darwin copies everything so
             * I won't bother allocating and managing SGs right now. Sorry. */
            union
            {
                uint8_t     abBuf[sizeof(INTNETSG) + sizeof(INTNETSEG)];
                INTNETSG    SG;
            } u;

            PINTNETHDR          pHdr;
            while ((pHdr = IntNetRingGetNextFrameToRead(&pIf->pIntBuf->Send)) != NULL)
            {
                uint8_t const      u8Type = pHdr->u8Type;
                if (u8Type == INTNETHDR_TYPE_FRAME)
                {
                    /* Send regular frame. */
                    void *pvCurFrame = IntNetHdrGetFramePtr(pHdr, pIf->pIntBuf);
                    IntNetSgInitTemp(&u.SG, pvCurFrame, pHdr->cbFrame);
                    if (pNetwork->fFlags & INTNET_OPEN_FLAGS_SHARED_MAC_ON_WIRE)
                        intnetR0IfSnoopAddr(pIf, (uint8_t *)pvCurFrame, pHdr->cbFrame, false /*fGso*/, (uint16_t *)&u.SG.fFlags);
                    enmSwDecision = intnetR0NetworkSend(pNetwork, pIf,  0 /*fSrc*/, &u.SG, pDstTab);
                }
                else if (u8Type == INTNETHDR_TYPE_GSO)
                {
                    /* Send GSO frame if sane. */
                    PPDMNETWORKGSO  pGso       = IntNetHdrGetGsoContext(pHdr, pIf->pIntBuf);
                    uint32_t        cbFrame    = pHdr->cbFrame - sizeof(*pGso);
                    if (RT_LIKELY(PDMNetGsoIsValid(pGso, pHdr->cbFrame, cbFrame)))
                    {
                        void       *pvCurFrame = pGso + 1;
                        IntNetSgInitTempGso(&u.SG, pvCurFrame, cbFrame, pGso);
                        if (pNetwork->fFlags & INTNET_OPEN_FLAGS_SHARED_MAC_ON_WIRE)
                            intnetR0IfSnoopAddr(pIf, (uint8_t *)pvCurFrame, cbFrame, true /*fGso*/, (uint16_t *)&u.SG.fFlags);
                        enmSwDecision = intnetR0NetworkSend(pNetwork, pIf, 0 /*fSrc*/, &u.SG, pDstTab);
                    }
                    else
                    {
                        STAM_REL_COUNTER_INC(&pIf->pIntBuf->cStatBadFrames); /* ignore */
                        enmSwDecision = INTNETSWDECISION_DROP;
                    }
                }
                /* Unless it's a padding frame, we're getting babble from the producer. */
                else
                {
                    if (u8Type != INTNETHDR_TYPE_PADDING)
                        STAM_REL_COUNTER_INC(&pIf->pIntBuf->cStatBadFrames); /* ignore */
                    enmSwDecision = INTNETSWDECISION_DROP;
                }
                if (enmSwDecision == INTNETSWDECISION_BAD_CONTEXT)
                {
                    rc = VERR_TRY_AGAIN;
                    break;
                }

                /* Skip to the next frame. */
                IntNetRingSkipFrame(&pIf->pIntBuf->Send);
            }

            /*
             * Put back the destination table.
             */
            Assert(!pIf->pDstTab);
            ASMAtomicWritePtr(&pIf->pDstTab, pDstTab);
        }
        else
            rc = VERR_INTERNAL_ERROR_4;
    }
    else
        rc = VERR_INTERNAL_ERROR_3;

    /*
     * Release the interface.
     */
    intnetR0BusyDecIf(pIf);
    STAM_REL_PROFILE_STOP(&pIf->pIntBuf->StatSend1, a);
    intnetR0IfRelease(pIf, pSession);
    return rc;
}


/**
 * VMMR0 request wrapper for IntNetR0IfSend.
 *
 * @returns see IntNetR0IfSend.
 * @param   pSession        The caller's session.
 * @param   pReq            The request packet.
 */
INTNETR0DECL(int) IntNetR0IfSendReq(PSUPDRVSESSION pSession, PINTNETIFSENDREQ pReq)
{
    if (RT_UNLIKELY(pReq->Hdr.cbReq != sizeof(*pReq)))
        return VERR_INVALID_PARAMETER;
    return IntNetR0IfSend(pReq->hIf, pSession);
}


/**
 * Maps the default buffer into ring 3.
 *
 * @returns VBox status code.
 * @param   hIf             The interface handle.
 * @param   pSession        The caller's session.
 * @param   ppRing3Buf      Where to store the address of the ring-3 mapping
 *                          (optional).
 * @param   ppRing0Buf      Where to store the address of the ring-0 mapping
 *                          (optional).
 */
INTNETR0DECL(int)       IntNetR0IfGetBufferPtrs(INTNETIFHANDLE hIf, PSUPDRVSESSION pSession,
                                                R3PTRTYPE(PINTNETBUF) *ppRing3Buf, R0PTRTYPE(PINTNETBUF) *ppRing0Buf)
{
    LogFlow(("IntNetR0IfGetBufferPtrs: hIf=%RX32 ppRing3Buf=%p ppRing0Buf=%p\n", hIf, ppRing3Buf, ppRing0Buf));

    /*
     * Validate input.
     */
    PINTNET pIntNet = g_pIntNet;
    AssertPtrReturn(pIntNet, VERR_INVALID_PARAMETER);
    AssertReturn(pIntNet->u32Magic, VERR_INVALID_MAGIC);

    AssertPtrNullReturn(ppRing3Buf, VERR_INVALID_PARAMETER);
    AssertPtrNullReturn(ppRing0Buf, VERR_INVALID_PARAMETER);
    if (ppRing3Buf)
        *ppRing3Buf = 0;
    if (ppRing0Buf)
        *ppRing0Buf = 0;

    PINTNETIF pIf = (PINTNETIF)RTHandleTableLookupWithCtx(pIntNet->hHtIfs, hIf, pSession);
    if (!pIf)
        return VERR_INVALID_HANDLE;

    /*
     * ASSUMES that only the process that created an interface can use it.
     * ASSUMES that we created the ring-3 mapping when selecting or
     * allocating the buffer.
     */
    int rc = RTSemMutexRequest(pIntNet->hMtxCreateOpenDestroy, RT_INDEFINITE_WAIT);
    if (RT_SUCCESS(rc))
    {
        if (ppRing3Buf)
            *ppRing3Buf = pIf->pIntBufR3;
        if (ppRing0Buf)
            *ppRing0Buf = (R0PTRTYPE(PINTNETBUF))pIf->pIntBuf; /* tstIntNetR0 mess */

        rc = RTSemMutexRelease(pIntNet->hMtxCreateOpenDestroy);
    }

    intnetR0IfRelease(pIf, pSession);
    LogFlow(("IntNetR0IfGetBufferPtrs: returns %Rrc *ppRing3Buf=%p *ppRing0Buf=%p\n",
             rc, ppRing3Buf ? *ppRing3Buf : NIL_RTR3PTR, ppRing0Buf ? *ppRing0Buf : NIL_RTR0PTR));
    return rc;
}


/**
 * VMMR0 request wrapper for IntNetR0IfGetBufferPtrs.
 *
 * @returns see IntNetR0IfGetRing3Buffer.
 * @param   pSession        The caller's session.
 * @param   pReq            The request packet.
 */
INTNETR0DECL(int) IntNetR0IfGetBufferPtrsReq(PSUPDRVSESSION pSession, PINTNETIFGETBUFFERPTRSREQ pReq)
{
    if (RT_UNLIKELY(pReq->Hdr.cbReq != sizeof(*pReq)))
        return VERR_INVALID_PARAMETER;
    return IntNetR0IfGetBufferPtrs(pReq->hIf, pSession, &pReq->pRing3Buf, &pReq->pRing0Buf);
}


#if 0
/**
 * Gets the physical addresses of the default interface buffer.
 *
 * @returns VBox status code.
 * @param   hIF         The interface handle.
 * @param   paPages     Where to store the addresses. (The reserved fields will be set to zero.)
 * @param   cPages
 */
INTNETR0DECL(int) IntNetR0IfGetPhysBuffer(INTNETIFHANDLE hIf, PSUPPAGE paPages, unsigned cPages)
{
    /*
     * Validate input.
     */
    PINTNET pIntNet = g_pIntNet;
    AssertPtrReturn(pIntNet, VERR_INVALID_PARAMETER);
    AssertReturn(pIntNet->u32Magic, VERR_INVALID_MAGIC);

    AssertPtrReturn(paPages, VERR_INVALID_PARAMETER);
    AssertPtrReturn((uint8_t *)&paPages[cPages] - 1, VERR_INVALID_PARAMETER);
    PINTNETIF pIf = (PINTNETIF)RTHandleTableLookupWithCtx(pIntNet->hHtIfs, hIf, pSession);
    if (!pIf)
        return VERR_INVALID_HANDLE;

    /*
     * Grab the lock and get the data.
     * ASSUMES that the handle isn't closed while we're here.
     */
    int rc = RTSemFastMutexRequest(pIf->pNetwork->FastMutex);
    if (RT_SUCCESS(rc))
    {
        /** @todo make a SUPR0 api for obtaining the array. SUPR0/IPRT is keeping track of everything, there
         * is no need for any extra bookkeeping here.. */

        rc = RTSemFastMutexRelease(pIf->pNetwork->FastMutex);
    }
    intnetR0IfRelease(pIf, pSession);
    return VERR_NOT_IMPLEMENTED;
}
#endif


/**
 * Sets the promiscuous mode property of an interface.
 *
 * @returns VBox status code.
 * @param   hIf             The interface handle.
 * @param   pSession        The caller's session.
 * @param   fPromiscuous    Set if the interface should be in promiscuous mode, clear if not.
 */
INTNETR0DECL(int) IntNetR0IfSetPromiscuousMode(INTNETIFHANDLE hIf, PSUPDRVSESSION pSession, bool fPromiscuous)
{
    LogFlow(("IntNetR0IfSetPromiscuousMode: hIf=%RX32 fPromiscuous=%d\n", hIf, fPromiscuous));

    /*
     * Validate & translate input.
     */
    PINTNET pIntNet = g_pIntNet;
    AssertPtrReturn(pIntNet, VERR_INVALID_PARAMETER);
    AssertReturn(pIntNet->u32Magic, VERR_INVALID_MAGIC);

    PINTNETIF pIf = (PINTNETIF)RTHandleTableLookupWithCtx(pIntNet->hHtIfs, hIf, pSession);
    if (!pIf)
    {
        Log(("IntNetR0IfSetPromiscuousMode: returns VERR_INVALID_HANDLE\n"));
        return VERR_INVALID_HANDLE;
    }

    /*
     * Get the network, take the address spinlock, and make the change.
     * Paranoia^2: Mark ourselves busy to prevent anything from being destroyed.
     */
    int rc = VINF_SUCCESS;
    intnetR0BusyIncIf(pIf);
    PINTNETNETWORK pNetwork = pIf->pNetwork;
    if (pNetwork)
    {
        RTSpinlockAcquire(pNetwork->hAddrSpinlock);

        if (pIf->fPromiscuousReal != fPromiscuous)
        {
            const bool fPromiscuousEff = fPromiscuous
                                      && (pIf->fOpenFlags  & INTNET_OPEN_FLAGS_IF_PROMISC_ALLOW)
                                      && (pNetwork->fFlags & INTNET_OPEN_FLAGS_PROMISC_ALLOW_CLIENTS);
            Log(("IntNetR0IfSetPromiscuousMode: hIf=%RX32: Changed from %d -> %d (%d)\n",
                 hIf, !fPromiscuous, !!fPromiscuous, fPromiscuousEff));

            pIf->fPromiscuousReal = fPromiscuous;

            PINTNETMACTABENTRY pEntry = intnetR0NetworkFindMacAddrEntry(pNetwork, pIf); Assert(pEntry);
            if (RT_LIKELY(pEntry))
            {
                if (pEntry->fPromiscuousEff)
                {
                    pNetwork->MacTab.cPromiscuousEntries--;
                    if (!pEntry->fPromiscuousSeeTrunk)
                        pNetwork->MacTab.cPromiscuousNoTrunkEntries--;
                    Assert(pNetwork->MacTab.cPromiscuousEntries        < pNetwork->MacTab.cEntries);
                    Assert(pNetwork->MacTab.cPromiscuousNoTrunkEntries < pNetwork->MacTab.cEntries);
                }

                pEntry->fPromiscuousEff      = fPromiscuousEff;
                pEntry->fPromiscuousSeeTrunk = fPromiscuousEff
                                            && (pIf->fOpenFlags & INTNET_OPEN_FLAGS_IF_PROMISC_SEE_TRUNK);

                if (pEntry->fPromiscuousEff)
                {
                    pNetwork->MacTab.cPromiscuousEntries++;
                    if (!pEntry->fPromiscuousSeeTrunk)
                        pNetwork->MacTab.cPromiscuousNoTrunkEntries++;
                }
                Assert(pNetwork->MacTab.cPromiscuousEntries        <= pNetwork->MacTab.cEntries);
                Assert(pNetwork->MacTab.cPromiscuousNoTrunkEntries <= pNetwork->MacTab.cEntries);
            }
        }

        RTSpinlockRelease(pNetwork->hAddrSpinlock);
    }
    else
        rc = VERR_WRONG_ORDER;

    intnetR0BusyDecIf(pIf);
    intnetR0IfRelease(pIf, pSession);
    return rc;
}


/**
 * VMMR0 request wrapper for IntNetR0IfSetPromiscuousMode.
 *
 * @returns see IntNetR0IfSetPromiscuousMode.
 * @param   pSession        The caller's session.
 * @param   pReq            The request packet.
 */
INTNETR0DECL(int) IntNetR0IfSetPromiscuousModeReq(PSUPDRVSESSION pSession, PINTNETIFSETPROMISCUOUSMODEREQ pReq)
{
    if (RT_UNLIKELY(pReq->Hdr.cbReq != sizeof(*pReq)))
        return VERR_INVALID_PARAMETER;
    return IntNetR0IfSetPromiscuousMode(pReq->hIf, pSession, pReq->fPromiscuous);
}


/**
 * Sets the MAC address of an interface.
 *
 * @returns VBox status code.
 * @param   hIf             The interface handle.
 * @param   pSession        The caller's session.
 * @param   pMAC            The new MAC address.
 */
INTNETR0DECL(int) IntNetR0IfSetMacAddress(INTNETIFHANDLE hIf, PSUPDRVSESSION pSession, PCRTMAC pMac)
{
    LogFlow(("IntNetR0IfSetMacAddress: hIf=%RX32 pMac=%p:{%.6Rhxs}\n", hIf, pMac, pMac));

    /*
     * Validate & translate input.
     */
    PINTNET pIntNet = g_pIntNet;
    AssertPtrReturn(pIntNet, VERR_INVALID_PARAMETER);
    AssertReturn(pIntNet->u32Magic, VERR_INVALID_MAGIC);

    AssertPtrReturn(pMac, VERR_INVALID_PARAMETER);
    PINTNETIF pIf = (PINTNETIF)RTHandleTableLookupWithCtx(pIntNet->hHtIfs, hIf, pSession);
    if (!pIf)
    {
        Log(("IntNetR0IfSetMacAddress: returns VERR_INVALID_HANDLE\n"));
        return VERR_INVALID_HANDLE;
    }

    /*
     * Get the network, take the address spinlock, and make the change.
     * Paranoia^2: Mark ourselves busy to prevent anything from being destroyed.
     */
    int rc = VINF_SUCCESS;
    intnetR0BusyIncIf(pIf);
    PINTNETNETWORK pNetwork = pIf->pNetwork;
    if (pNetwork)
    {
        PINTNETTRUNKIF  pTrunk = NULL;

        RTSpinlockAcquire(pNetwork->hAddrSpinlock);

        if (memcmp(&pIf->MacAddr, pMac, sizeof(pIf->MacAddr)))
        {
            Log(("IntNetR0IfSetMacAddress: hIf=%RX32: Changed from %.6Rhxs -> %.6Rhxs\n",
                 hIf, &pIf->MacAddr, pMac));

            /* Update the two copies. */
            PINTNETMACTABENTRY pEntry = intnetR0NetworkFindMacAddrEntry(pNetwork, pIf); Assert(pEntry);
            if (RT_LIKELY(pEntry))
                pEntry->MacAddr = *pMac;
            pIf->MacAddr        = *pMac;
            pIf->fMacSet        = true;

            /* Grab a busy reference to the trunk so we release the lock before notifying it. */
            pTrunk = pNetwork->MacTab.pTrunk;
            if (pTrunk)
                intnetR0BusyIncTrunk(pTrunk);
        }

        RTSpinlockRelease(pNetwork->hAddrSpinlock);

        if (pTrunk)
        {
            Log(("IntNetR0IfSetMacAddress: pfnNotifyMacAddress hIf=%RX32\n", hIf));
            PINTNETTRUNKIFPORT pIfPort = pTrunk->pIfPort;
            if (pIfPort)
                pIfPort->pfnNotifyMacAddress(pIfPort, pIf->pvIfData, pMac);
            intnetR0BusyDecTrunk(pTrunk);
        }
    }
    else
        rc = VERR_WRONG_ORDER;

    intnetR0BusyDecIf(pIf);
    intnetR0IfRelease(pIf, pSession);
    return rc;
}


/**
 * VMMR0 request wrapper for IntNetR0IfSetMacAddress.
 *
 * @returns see IntNetR0IfSetMacAddress.
 * @param   pSession        The caller's session.
 * @param   pReq            The request packet.
 */
INTNETR0DECL(int) IntNetR0IfSetMacAddressReq(PSUPDRVSESSION pSession, PINTNETIFSETMACADDRESSREQ pReq)
{
    if (RT_UNLIKELY(pReq->Hdr.cbReq != sizeof(*pReq)))
        return VERR_INVALID_PARAMETER;
    return IntNetR0IfSetMacAddress(pReq->hIf, pSession, &pReq->Mac);
}


/**
 * Worker for intnetR0IfSetActive and intnetR0IfDestruct.
 *
 * This function will update the active interface count on the network and
 * activate or deactivate the trunk connection if necessary.
 *
 * The call must own the giant lock (we cannot take it here).
 *
 * @returns VBox status code.
 * @param   pNetwork        The network.
 * @param   fIf             The interface.
 * @param   fActive         What to do.
 */
static int intnetR0NetworkSetIfActive(PINTNETNETWORK pNetwork, PINTNETIF pIf, bool fActive)
{
    /* quick sanity check */
    AssertPtr(pNetwork);
    AssertPtr(pIf);

    /*
     * The address spinlock of the network protects the variables, while the
     * big lock protects the calling of pfnSetState.  Grab both lock at once
     * to save us the extra hassle.
     */
    PINTNETTRUNKIF  pTrunk  = NULL;
    RTSpinlockAcquire(pNetwork->hAddrSpinlock);

    /*
     * Do the update.
     */
    if (pIf->fActive != fActive)
    {
        PINTNETMACTABENTRY pEntry = intnetR0NetworkFindMacAddrEntry(pNetwork, pIf); Assert(pEntry);
        if (RT_LIKELY(pEntry))
        {
            pEntry->fActive = fActive;
            pIf->fActive    = fActive;

            if (fActive)
            {
                pNetwork->cActiveIFs++;
                if (pNetwork->cActiveIFs == 1)
                {
                    pTrunk = pNetwork->MacTab.pTrunk;
                    if (pTrunk)
                    {
                        pNetwork->MacTab.fHostActive = RT_BOOL(pNetwork->fFlags & INTNET_OPEN_FLAGS_TRUNK_HOST_ENABLED);
                        pNetwork->MacTab.fWireActive = RT_BOOL(pNetwork->fFlags & INTNET_OPEN_FLAGS_TRUNK_WIRE_ENABLED);
                    }
                }
            }
            else
            {
                pNetwork->cActiveIFs--;
                if (pNetwork->cActiveIFs == 0)
                {
                    pTrunk = pNetwork->MacTab.pTrunk;
                    pNetwork->MacTab.fHostActive = false;
                    pNetwork->MacTab.fWireActive = false;
                }
            }
        }
    }

    RTSpinlockRelease(pNetwork->hAddrSpinlock);

    /*
     * Tell the trunk if necessary.
     * The wait for !busy is for the Solaris streams trunk driver (mostly).
     */
    if (pTrunk && pTrunk->pIfPort)
    {
        if (!fActive)
            intnetR0BusyWait(pNetwork, &pTrunk->cBusy);

        pTrunk->pIfPort->pfnSetState(pTrunk->pIfPort, fActive ? INTNETTRUNKIFSTATE_ACTIVE : INTNETTRUNKIFSTATE_INACTIVE);
    }

    return VINF_SUCCESS;
}


/**
 * Sets the active property of an interface.
 *
 * @returns VBox status code.
 * @param   hIf             The interface handle.
 * @param   pSession        The caller's session.
 * @param   fActive         The new state.
 */
INTNETR0DECL(int) IntNetR0IfSetActive(INTNETIFHANDLE hIf, PSUPDRVSESSION pSession, bool fActive)
{
    LogFlow(("IntNetR0IfSetActive: hIf=%RX32 fActive=%RTbool\n", hIf, fActive));

    /*
     * Validate & translate input.
     */
    PINTNET pIntNet = g_pIntNet;
    AssertPtrReturn(pIntNet, VERR_INVALID_PARAMETER);
    AssertReturn(pIntNet->u32Magic, VERR_INVALID_MAGIC);

    PINTNETIF pIf = (PINTNETIF)RTHandleTableLookupWithCtx(pIntNet->hHtIfs, hIf, pSession);
    if (!pIf)
    {
        Log(("IntNetR0IfSetActive: returns VERR_INVALID_HANDLE\n"));
        return VERR_INVALID_HANDLE;
    }

    /*
     * Hand it to the network since it might involve the trunk and things are
     * tricky there wrt to locking order.
     *
     * 1. We take the giant lock here.  This makes sure nobody is re-enabling
     *    the network while we're pausing it and vice versa.  This also enables
     *    us to wait for the network to become idle before telling the trunk.
     *    (Important on Solaris.)
     *
     * 2. For paranoid reasons, we grab a busy reference to the calling
     *    interface.  This is totally unnecessary but should hurt (when done
     *    after grabbing the giant lock).
     */
    int rc = RTSemMutexRequest(pIntNet->hMtxCreateOpenDestroy, RT_INDEFINITE_WAIT);
    if (RT_SUCCESS(rc))
    {
        intnetR0BusyIncIf(pIf);

        PINTNETNETWORK pNetwork = pIf->pNetwork;
        if (pNetwork)
            rc = intnetR0NetworkSetIfActive(pNetwork, pIf, fActive);
        else
            rc = VERR_WRONG_ORDER;

        intnetR0BusyDecIf(pIf);
        RTSemMutexRelease(pIntNet->hMtxCreateOpenDestroy);
    }

    intnetR0IfRelease(pIf, pSession);
    LogFlow(("IntNetR0IfSetActive: returns %Rrc\n", rc));
    return rc;
}


/**
 * VMMR0 request wrapper for IntNetR0IfSetActive.
 *
 * @returns see IntNetR0IfSetActive.
 * @param   pIntNet         The internal networking instance.
 * @param   pSession        The caller's session.
 * @param   pReq            The request packet.
 */
INTNETR0DECL(int) IntNetR0IfSetActiveReq(PSUPDRVSESSION pSession, PINTNETIFSETACTIVEREQ pReq)
{
    if (RT_UNLIKELY(pReq->Hdr.cbReq != sizeof(*pReq)))
        return VERR_INVALID_PARAMETER;
    return IntNetR0IfSetActive(pReq->hIf, pSession, pReq->fActive);
}


/**
 * Wait for the interface to get signaled.
 * The interface will be signaled when is put into the receive buffer.
 *
 * @returns VBox status code.
 * @param   hIf             The interface handle.
 * @param   pSession        The caller's session.
 * @param   cMillies        Number of milliseconds to wait. RT_INDEFINITE_WAIT should be
 *                          used if indefinite wait is desired.
 */
INTNETR0DECL(int) IntNetR0IfWait(INTNETIFHANDLE hIf, PSUPDRVSESSION pSession, uint32_t cMillies)
{
    Log4(("IntNetR0IfWait: hIf=%RX32 cMillies=%u\n", hIf, cMillies));

    /*
     * Get and validate essential handles.
     */
    PINTNET pIntNet = g_pIntNet;
    AssertPtrReturn(pIntNet, VERR_INVALID_PARAMETER);
    AssertReturn(pIntNet->u32Magic, VERR_INVALID_MAGIC);

    PINTNETIF pIf = (PINTNETIF)RTHandleTableLookupWithCtx(pIntNet->hHtIfs, hIf, pSession);
    if (!pIf)
    {
        Log(("IntNetR0IfWait: returns VERR_INVALID_HANDLE\n"));
        return VERR_INVALID_HANDLE;
    }

#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3) && defined(IN_RING3)
    AssertReleaseFailed(); /* Should never be called. */
    RT_NOREF(cMillies);
    return VERR_NOT_SUPPORTED;
#else
    const RTSEMEVENT hRecvEvent  = pIf->hRecvEvent;
    const bool       fNoMoreWaits = ASMAtomicUoReadBool(&pIf->fNoMoreWaits);
    RTNATIVETHREAD   hDtorThrd;
    ASMAtomicReadHandle(&pIf->hDestructorThread, &hDtorThrd);
    if (hDtorThrd != NIL_RTNATIVETHREAD)
    {
        /* See IntNetR0IfAbortWait for an explanation of hDestructorThread. */
        Log(("IntNetR0IfWait: returns VERR_SEM_DESTROYED\n"));
        return VERR_SEM_DESTROYED;
    }

    /* Check whether further waits have been barred by IntNetR0IfAbortWait. */
    int rc;
    if (   !fNoMoreWaits
        && hRecvEvent != NIL_RTSEMEVENT)
    {
        /*
         * It is tempting to check if there is data to be read here,
         * but the problem with such an approach is that it will cause
         * one unnecessary supervisor->user->supervisor trip. There is
         * already a slight risk for such, so no need to increase it.
         */

        /*
         * Increment the number of waiters before starting the wait.
         * Upon wakeup we must assert reality, checking that we're not
         * already destroyed or in the process of being destroyed. This
         * code must be aligned with the waiting code in intnetR0IfDestruct.
         */
        ASMAtomicIncU32(&pIf->cSleepers);
        rc = RTSemEventWaitNoResume(hRecvEvent, cMillies);
        if (pIf->hRecvEvent == hRecvEvent)
        {
            ASMAtomicDecU32(&pIf->cSleepers);
            ASMAtomicReadHandle(&pIf->hDestructorThread, &hDtorThrd);
            if (hDtorThrd == NIL_RTNATIVETHREAD)
            {
                if (intnetR0IfRelease(pIf, pSession))
                    rc = VERR_SEM_DESTROYED;
            }
            else
                rc = VERR_SEM_DESTROYED;
        }
        else
            rc = VERR_SEM_DESTROYED;
    }
    else
    {
        rc = VERR_SEM_DESTROYED;
        intnetR0IfRelease(pIf, pSession);
    }

    Log4(("IntNetR0IfWait: returns %Rrc\n", rc));
    return rc;
#endif
}


/**
 * VMMR0 request wrapper for IntNetR0IfWait.
 *
 * @returns see IntNetR0IfWait.
 * @param   pSession        The caller's session.
 * @param   pReq            The request packet.
 */
INTNETR0DECL(int) IntNetR0IfWaitReq(PSUPDRVSESSION pSession, PINTNETIFWAITREQ pReq)
{
    if (RT_UNLIKELY(pReq->Hdr.cbReq != sizeof(*pReq)))
        return VERR_INVALID_PARAMETER;
    return IntNetR0IfWait(pReq->hIf, pSession, pReq->cMillies);
}


/**
 * Wake up any threads waiting on the interface.
 *
 * @returns VBox status code.
 * @param   hIf             The interface handle.
 * @param   pSession        The caller's session.
 * @param   fNoMoreWaits    When set, no more waits are permitted.
 */
INTNETR0DECL(int) IntNetR0IfAbortWait(INTNETIFHANDLE hIf, PSUPDRVSESSION pSession, bool fNoMoreWaits)
{
    Log4(("IntNetR0IfAbortWait: hIf=%RX32 fNoMoreWaits=%RTbool\n", hIf, fNoMoreWaits));

    /*
     * Get and validate essential handles.
     */
    PINTNET pIntNet = g_pIntNet;
    AssertPtrReturn(pIntNet, VERR_INVALID_PARAMETER);
    AssertReturn(pIntNet->u32Magic, VERR_INVALID_MAGIC);

    PINTNETIF pIf = (PINTNETIF)RTHandleTableLookupWithCtx(pIntNet->hHtIfs, hIf, pSession);
    if (!pIf)
    {
        Log(("IntNetR0IfAbortWait: returns VERR_INVALID_HANDLE\n"));
        return VERR_INVALID_HANDLE;
    }

#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3) && defined(IN_RING3)
    AssertReleaseFailed();
    RT_NOREF(fNoMoreWaits);
    return VERR_NOT_SUPPORTED;
#else
    const RTSEMEVENT hRecvEvent  = pIf->hRecvEvent;
    RTNATIVETHREAD   hDtorThrd;
    ASMAtomicReadHandle(&pIf->hDestructorThread, &hDtorThrd);
    if (hDtorThrd != NIL_RTNATIVETHREAD)
    {
        /* This can only happen if we for some reason race SUPDRVSESSION cleanup,
           i.e. the object count is set to zero without yet having removed it from
           the object table, so we got a spurious "reference".  We must drop that
           reference and let the destructor get on with its work.  (Not entirely sure
           if this is practically possible on any of the platforms, i.e. whether it's
           we can actually close a SUPDrv handle/descriptor with active threads still
           in NtDeviceIoControlFile/ioctl, but better safe than sorry.) */
        Log(("IntNetR0IfAbortWait: returns VERR_SEM_DESTROYED\n"));
        return VERR_SEM_DESTROYED;
    }

    /* a bit of paranoia */
    int rc = VINF_SUCCESS;
    if (hRecvEvent != NIL_RTSEMEVENT)
    {
        /*
         * Set fNoMoreWaits if requested to do so and then wake up all the sleeping
         * threads (usually just one).   We leave the semaphore in the signalled
         * state so the next caller will return immediately.
         */
        if (fNoMoreWaits)
            ASMAtomicWriteBool(&pIf->fNoMoreWaits, true);

        uint32_t cSleepers = ASMAtomicReadU32(&pIf->cSleepers) + 1;
        while (cSleepers-- > 0)
        {
            int rc2 = RTSemEventSignal(pIf->hRecvEvent);
            AssertRC(rc2);
        }
    }
    else
        rc = VERR_SEM_DESTROYED;

    intnetR0IfRelease(pIf, pSession);

    Log4(("IntNetR0IfWait: returns %Rrc\n", VINF_SUCCESS));
    return VINF_SUCCESS;
#endif
}


/**
 * VMMR0 request wrapper for IntNetR0IfAbortWait.
 *
 * @returns see IntNetR0IfWait.
 * @param   pSession        The caller's session.
 * @param   pReq            The request packet.
 */
INTNETR0DECL(int) IntNetR0IfAbortWaitReq(PSUPDRVSESSION pSession, PINTNETIFABORTWAITREQ pReq)
{
    if (RT_UNLIKELY(pReq->Hdr.cbReq != sizeof(*pReq)))
        return VERR_INVALID_PARAMETER;
    return IntNetR0IfAbortWait(pReq->hIf, pSession, pReq->fNoMoreWaits);
}


/**
 * Close an interface.
 *
 * @returns VBox status code.
 * @param   pIntNet     The instance handle.
 * @param   hIf         The interface handle.
 * @param   pSession        The caller's session.
 */
INTNETR0DECL(int) IntNetR0IfClose(INTNETIFHANDLE hIf, PSUPDRVSESSION pSession)
{
    LogFlow(("IntNetR0IfClose: hIf=%RX32\n", hIf));

    /*
     * Validate and free the handle.
     */
    PINTNET pIntNet = g_pIntNet;
    AssertPtrReturn(pIntNet, VERR_INVALID_PARAMETER);
    AssertReturn(pIntNet->u32Magic, VERR_INVALID_MAGIC);

    PINTNETIF pIf = (PINTNETIF)RTHandleTableFreeWithCtx(pIntNet->hHtIfs, hIf, pSession);
    if (!pIf)
        return VERR_INVALID_HANDLE;

    /* Mark the handle as freed so intnetR0IfDestruct won't free it again. */
    ASMAtomicWriteU32(&pIf->hIf, INTNET_HANDLE_INVALID);

#if !defined(VBOX_WITH_INTNET_SERVICE_IN_R3) || !defined(IN_RING3)
    /*
     * Signal the event semaphore to wake up any threads in IntNetR0IfWait
     * and give them a moment to get out and release the interface.
     */
    uint32_t i = pIf->cSleepers;
    while (i-- > 0)
    {
        RTSemEventSignal(pIf->hRecvEvent);
        RTThreadYield();
    }
    RTSemEventSignal(pIf->hRecvEvent);
#endif

    /*
     * Release the references to the interface object (handle + free lookup).
     */
    void *pvObj = pIf->pvObj;
    intnetR0IfRelease(pIf, pSession); /* (RTHandleTableFreeWithCtx) */

    int rc = SUPR0ObjRelease(pvObj, pSession);
    LogFlow(("IntNetR0IfClose: returns %Rrc\n", rc));
    return rc;
}


/**
 * VMMR0 request wrapper for IntNetR0IfCloseReq.
 *
 * @returns see IntNetR0IfClose.
 * @param   pSession        The caller's session.
 * @param   pReq            The request packet.
 */
INTNETR0DECL(int) IntNetR0IfCloseReq(PSUPDRVSESSION pSession, PINTNETIFCLOSEREQ pReq)
{
    if (RT_UNLIKELY(pReq->Hdr.cbReq != sizeof(*pReq)))
        return VERR_INVALID_PARAMETER;
    return IntNetR0IfClose(pReq->hIf, pSession);
}


/**
 * Interface destructor callback.
 * This is called for reference counted objectes when the count reaches 0.
 *
 * @param   pvObj       The object pointer.
 * @param   pvUser1     Pointer to the interface.
 * @param   pvUser2     Pointer to the INTNET instance data.
 */
static DECLCALLBACK(void) intnetR0IfDestruct(void *pvObj, void *pvUser1, void *pvUser2)
{
    PINTNETIF pIf     = (PINTNETIF)pvUser1;
    PINTNET   pIntNet = (PINTNET)pvUser2;
    Log(("intnetR0IfDestruct: pvObj=%p pIf=%p pIntNet=%p hIf=%RX32\n", pvObj, pIf, pIntNet, pIf->hIf));
    RT_NOREF1(pvObj);

    /*
     * For paranoid reasons we must now mark the interface as destroyed.
     * This is so that any waiting threads can take evasive action (kind
     * of theoretical case), and we can reject everyone else referencing
     * the object via the handle table before we get around to removing it.
     */
    ASMAtomicWriteHandle(&pIf->hDestructorThread, RTThreadNativeSelf());

    /*
     * We grab the INTNET create/open/destroy semaphore to make sure nobody is
     * adding or removing interfaces while we're in here.
     */
    RTSemMutexRequest(pIntNet->hMtxCreateOpenDestroy, RT_INDEFINITE_WAIT);

    /*
     * Delete the interface handle so the object no longer can be used.
     * (Can happen if the client didn't close its session.)
     */
    INTNETIFHANDLE hIf = ASMAtomicXchgU32(&pIf->hIf, INTNET_HANDLE_INVALID);
    if (hIf != INTNET_HANDLE_INVALID)
    {
        void *pvObj2 = RTHandleTableFreeWithCtx(pIntNet->hHtIfs, hIf, pIf->pSession); NOREF(pvObj2);
        AssertMsg(pvObj2 == pIf, ("%p, %p, hIf=%RX32 pSession=%p\n", pvObj2, pIf, hIf, pIf->pSession));
    }

    /*
     * If we've got a network deactivate and detach ourselves from it.  Because
     * of cleanup order we might have been orphaned by the network destructor.
     */
    PINTNETNETWORK pNetwork = pIf->pNetwork;
    if (pNetwork)
    {
        /* set inactive. */
        intnetR0NetworkSetIfActive(pNetwork, pIf, false /*fActive*/);

        /* remove ourselves from the switch table. */
        RTSpinlockAcquire(pNetwork->hAddrSpinlock);

        uint32_t iIf = pNetwork->MacTab.cEntries;
        while (iIf-- > 0)
            if (pNetwork->MacTab.paEntries[iIf].pIf == pIf)
            {
                if (pNetwork->MacTab.paEntries[iIf].fPromiscuousEff)
                {
                    pNetwork->MacTab.cPromiscuousEntries--;
                    if (!pNetwork->MacTab.paEntries[iIf].fPromiscuousSeeTrunk)
                        pNetwork->MacTab.cPromiscuousNoTrunkEntries--;
                }
                Assert(pNetwork->MacTab.cPromiscuousEntries        < pNetwork->MacTab.cEntries);
                Assert(pNetwork->MacTab.cPromiscuousNoTrunkEntries < pNetwork->MacTab.cEntries);

                if (iIf + 1 < pNetwork->MacTab.cEntries)
                    memmove(&pNetwork->MacTab.paEntries[iIf],
                            &pNetwork->MacTab.paEntries[iIf + 1],
                            (pNetwork->MacTab.cEntries - iIf - 1) * sizeof(pNetwork->MacTab.paEntries[0]));
                pNetwork->MacTab.cEntries--;
                break;
            }

        /* recalc the min flags. */
        if (pIf->fOpenFlags & INTNET_OPEN_FLAGS_REQUIRE_AS_RESTRICTIVE_POLICIES)
        {
            uint32_t fMinFlags = 0;
            iIf = pNetwork->MacTab.cEntries;
            while (iIf-- > 0)
            {
                PINTNETIF pIf2 = pNetwork->MacTab.paEntries[iIf].pIf;
                if (   pIf2 /* paranoia */
                    && (pIf2->fOpenFlags & INTNET_OPEN_FLAGS_REQUIRE_AS_RESTRICTIVE_POLICIES))
                    fMinFlags |= pIf2->fOpenFlags & INTNET_OPEN_FLAGS_STRICT_MASK;
            }
            pNetwork->fMinFlags = fMinFlags;
        }

        PINTNETTRUNKIF pTrunk = pNetwork->MacTab.pTrunk;

        RTSpinlockRelease(pNetwork->hAddrSpinlock);

        /* Notify the trunk about the interface being destroyed. */
        if (pTrunk && pTrunk->pIfPort)
            pTrunk->pIfPort->pfnDisconnectInterface(pTrunk->pIfPort, pIf->pvIfData);

        /* Wait for the interface to quiesce while we still can. */
        intnetR0BusyWait(pNetwork, &pIf->cBusy);

        /* Release our reference to the network. */
        RTSpinlockAcquire(pNetwork->hAddrSpinlock);
        pIf->pNetwork = NULL;
        RTSpinlockRelease(pNetwork->hAddrSpinlock);

        SUPR0ObjRelease(pNetwork->pvObj, pIf->pSession);
    }

    RTSemMutexRelease(pIntNet->hMtxCreateOpenDestroy);

#if !defined(VBOX_WITH_INTNET_SERVICE_IN_R3) || !defined(IN_RING3)
    /*
     * Wakeup anyone waiting on this interface. (Kind of unlikely, but perhaps
     * not quite impossible.)
     *
     * We *must* make sure they have woken up properly and realized
     * that the interface is no longer valid.
     */
    if (pIf->hRecvEvent != NIL_RTSEMEVENT)
    {
        RTSEMEVENT  hRecvEvent = pIf->hRecvEvent;
        unsigned    cMaxWait   = 0x1000;
        while (pIf->cSleepers && cMaxWait-- > 0)
        {
            RTSemEventSignal(hRecvEvent);
            RTThreadYield();
        }
        if (pIf->cSleepers)
        {
            RTThreadSleep(1);

            cMaxWait = pIf->cSleepers;
            while (pIf->cSleepers && cMaxWait-- > 0)
            {
                RTSemEventSignal(hRecvEvent);
                RTThreadSleep(10);
            }
        }

        RTSemEventDestroy(hRecvEvent);
        pIf->hRecvEvent = NIL_RTSEMEVENT;
    }
#endif

    /*
     * Unmap user buffer.
     */
    if (pIf->pIntBuf != pIf->pIntBufDefault)
    {
        /** @todo user buffer */
    }

    /*
     * Unmap and Free the default buffer.
     */
    if (pIf->pIntBufDefault)
    {
        SUPR0MemFree(pIf->pSession, (RTHCUINTPTR)pIf->pIntBufDefault);
        pIf->pIntBufDefault     = NULL;
        pIf->pIntBufDefaultR3   = 0;
        pIf->pIntBuf            = NULL;
        pIf->pIntBufR3          = 0;
    }

    /*
     * Free remaining resources
     */
    RTSpinlockDestroy(pIf->hRecvInSpinlock);
    pIf->hRecvInSpinlock = NIL_RTSPINLOCK;

    RTMemFree(pIf->pDstTab);
    pIf->pDstTab = NULL;

    for (int i = kIntNetAddrType_Invalid + 1; i < kIntNetAddrType_End; i++)
        intnetR0IfAddrCacheDestroy(&pIf->aAddrCache[i]);

    pIf->pvObj = NULL;
    RTMemFree(pIf);
}


/* Forward declaration of trunk reconnection thread function. */
static DECLCALLBACK(int) intnetR0TrunkReconnectThread(RTTHREAD hThread, void *pvUser);

/**
 * Creates a new network interface.
 *
 * The call must have opened the network for the new interface and is
 * responsible for closing it on failure.  On success it must leave the network
 * opened so the interface destructor can close it.
 *
 * @returns VBox status code.
 * @param   pNetwork        The network, referenced.  The reference is consumed
 *                          on success.
 * @param   pSession        The session handle.
 * @param   cbSend          The size of the send buffer.
 * @param   cbRecv          The size of the receive buffer.
 * @param   fFlags          The open network flags.
 * @param   pfnRecvAvail    The receive available callback to call instead of
 *                          signalling the semaphore (R3 service only).
 * @param   pvUser          The opaque user data to pass to the callback.
 * @param   phIf            Where to store the interface handle.
 */
static int intnetR0NetworkCreateIf(PINTNETNETWORK pNetwork, PSUPDRVSESSION pSession, unsigned cbSend, unsigned cbRecv,
                                   uint32_t fFlags, PFNINTNETIFRECVAVAIL pfnRecvAvail, void *pvUser, PINTNETIFHANDLE phIf)
{
    LogFlow(("intnetR0NetworkCreateIf: pNetwork=%p pSession=%p cbSend=%u cbRecv=%u fFlags=%#x phIf=%p\n",
             pNetwork, pSession, cbSend, cbRecv, fFlags, phIf));

    /*
     * Assert input.
     */
    AssertPtr(pNetwork);
    AssertPtr(phIf);
#if !defined(VBOX_WITH_INTNET_SERVICE_IN_R3) || !defined(IN_RING3)
    Assert(pfnRecvAvail == NULL);
    Assert(pvUser == NULL);
    RT_NOREF(pfnRecvAvail, pvUser);
#endif

    /*
     * Adjust the flags with defaults for the interface policies.
     * Note: Main restricts promiscuous mode per interface.
     */
    uint32_t const  fDefFlags = INTNET_OPEN_FLAGS_IF_PROMISC_ALLOW
                              | INTNET_OPEN_FLAGS_IF_PROMISC_SEE_TRUNK;
    for (uint32_t i = 0; i < RT_ELEMENTS(g_afIntNetOpenNetworkIfFlags); i++)
        if (!(fFlags & g_afIntNetOpenNetworkIfFlags[i].fPair))
            fFlags |= g_afIntNetOpenNetworkIfFlags[i].fPair & fDefFlags;

    /*
     * Make sure that all destination tables as well as the  have space of
     */
    int rc = intnetR0NetworkEnsureTabSpace(pNetwork);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Allocate the interface and initialize it.
     */
    PINTNETIF pIf = (PINTNETIF)RTMemAllocZ(sizeof(*pIf));
    if (!pIf)
        return VERR_NO_MEMORY;

    memset(&pIf->MacAddr, 0xff, sizeof(pIf->MacAddr)); /* broadcast */
    //pIf->fMacSet          = false;
    //pIf->fPromiscuousReal = false;
    //pIf->fActive          = false;
    //pIf->fNoMoreWaits     = false;
    pIf->fOpenFlags         = fFlags;
    //pIf->cYields          = 0;
    //pIf->pIntBuf          = 0;
    //pIf->pIntBufR3        = NIL_RTR3PTR;
    //pIf->pIntBufDefault   = 0;
    //pIf->pIntBufDefaultR3 = NIL_RTR3PTR;
#if !defined(VBOX_WITH_INTNET_SERVICE_IN_R3) || !defined(IN_RING3)
    pIf->hRecvEvent         = NIL_RTSEMEVENT;
#else
    pIf->pfnRecvAvail       = pfnRecvAvail;
    pIf->pvUserRecvAvail    = pvUser;
#endif
    //pIf->cSleepers        = 0;
    pIf->hIf                = INTNET_HANDLE_INVALID;
    pIf->hDestructorThread  = NIL_RTNATIVETHREAD;
    pIf->pNetwork           = pNetwork;
    pIf->pSession           = pSession;
    //pIf->pvObj            = NULL;
    //pIf->aAddrCache       = {0};
    pIf->hRecvInSpinlock    = NIL_RTSPINLOCK;
    pIf->cBusy              = 0;
    //pIf->pDstTab          = NULL;
    //pIf->pvIfData         = NULL;

    for (int i = kIntNetAddrType_Invalid + 1; i < kIntNetAddrType_End && RT_SUCCESS(rc); i++)
        rc = intnetR0IfAddrCacheInit(&pIf->aAddrCache[i], (INTNETADDRTYPE)i,
                                     !!(pNetwork->fFlags & INTNET_OPEN_FLAGS_SHARED_MAC_ON_WIRE));
    if (RT_SUCCESS(rc))
        rc = intnetR0AllocDstTab(pNetwork->MacTab.cEntriesAllocated, (PINTNETDSTTAB *)&pIf->pDstTab);
#if !defined(VBOX_WITH_INTNET_SERVICE_IN_R3) || !defined(IN_RING3)
    if (RT_SUCCESS(rc))
        rc = RTSemEventCreate((PRTSEMEVENT)&pIf->hRecvEvent);
#endif
    if (RT_SUCCESS(rc))
        rc = RTSpinlockCreate(&pIf->hRecvInSpinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "hRecvInSpinlock");
    if (RT_SUCCESS(rc))
    {
        /*
         * Create the default buffer.
         */
        /** @todo adjust with minimums and apply defaults here. */
        cbRecv = RT_ALIGN(RT_MAX(cbRecv, sizeof(INTNETHDR) * 4), INTNETRINGBUF_ALIGNMENT);
        cbSend = RT_ALIGN(RT_MAX(cbSend, sizeof(INTNETHDR) * 4), INTNETRINGBUF_ALIGNMENT);
        const unsigned cbBuf = RT_ALIGN(sizeof(*pIf->pIntBuf), INTNETRINGBUF_ALIGNMENT) + cbRecv + cbSend;
        rc = SUPR0MemAlloc(pIf->pSession, cbBuf, (PRTR0PTR)&pIf->pIntBufDefault, (PRTR3PTR)&pIf->pIntBufDefaultR3);
        if (RT_SUCCESS(rc))
        {
            RT_BZERO(pIf->pIntBufDefault, cbBuf); /** @todo I thought I specified these buggers as clearing the memory... */

            pIf->pIntBuf   = pIf->pIntBufDefault;
            pIf->pIntBufR3 = pIf->pIntBufDefaultR3;
            IntNetBufInit(pIf->pIntBuf, cbBuf, cbRecv, cbSend);

            /*
             * Register the interface with the session and create a handle for it.
             */
            pIf->pvObj = SUPR0ObjRegister(pSession, SUPDRVOBJTYPE_INTERNAL_NETWORK_INTERFACE,
                                          intnetR0IfDestruct, pIf, pNetwork->pIntNet);
            if (pIf->pvObj)
            {
                rc = RTHandleTableAllocWithCtx(pNetwork->pIntNet->hHtIfs, pIf, pSession, (uint32_t *)&pIf->hIf);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Finally add the interface to the network, consuming the
                     * network reference of the caller.
                     */
                    RTSpinlockAcquire(pNetwork->hAddrSpinlock);

                    uint32_t iIf = pNetwork->MacTab.cEntries;
                    Assert(iIf + 1 <= pNetwork->MacTab.cEntriesAllocated);

                    pNetwork->MacTab.paEntries[iIf].MacAddr              = pIf->MacAddr;
                    pNetwork->MacTab.paEntries[iIf].fActive              = false;
                    pNetwork->MacTab.paEntries[iIf].fPromiscuousEff      = false;
                    pNetwork->MacTab.paEntries[iIf].fPromiscuousSeeTrunk = false;
                    pNetwork->MacTab.paEntries[iIf].pIf                  = pIf;

                    pNetwork->MacTab.cEntries = iIf + 1;
                    pIf->pNetwork = pNetwork;

                    /*
                     * Grab a busy reference (paranoia) to the trunk before releasing
                     * the spinlock and then notify it about the new interface.
                     */
                    PINTNETTRUNKIF pTrunk = pNetwork->MacTab.pTrunk;
                    if (pTrunk)
                        intnetR0BusyIncTrunk(pTrunk);

                    RTSpinlockRelease(pNetwork->hAddrSpinlock);

                    if (pTrunk)
                    {
                        Log(("intnetR0NetworkCreateIf: pfnConnectInterface hIf=%RX32\n", pIf->hIf));
                        if (pTrunk->pIfPort)
                            rc = pTrunk->pIfPort->pfnConnectInterface(pTrunk->pIfPort, pIf, &pIf->pvIfData);
                        intnetR0BusyDecTrunk(pTrunk);
                    }
                    if (RT_SUCCESS(rc))
                    {
                        /*
                         * We're good!
                         */
                        *phIf = pIf->hIf;
                        Log(("intnetR0NetworkCreateIf: returns VINF_SUCCESS *phIf=%RX32 cbSend=%u cbRecv=%u cbBuf=%u\n",
                             *phIf, pIf->pIntBufDefault->cbSend, pIf->pIntBufDefault->cbRecv, pIf->pIntBufDefault->cbBuf));
                        return VINF_SUCCESS;
                    }
                }

                SUPR0ObjAddRef(pNetwork->pvObj, pSession);
                SUPR0ObjRelease(pIf->pvObj, pSession);
                LogFlow(("intnetR0NetworkCreateIf: returns %Rrc\n", rc));
                return rc;
            }

            /* clean up */
            SUPR0MemFree(pIf->pSession, (RTHCUINTPTR)pIf->pIntBufDefault);
            pIf->pIntBufDefault = NULL;
            pIf->pIntBuf = NULL;
        }
    }

    RTSpinlockDestroy(pIf->hRecvInSpinlock);
    pIf->hRecvInSpinlock = NIL_RTSPINLOCK;
#if !defined(VBOX_WITH_INTNET_SERVICE_IN_R3) || !defined(IN_RING3)
    RTSemEventDestroy(pIf->hRecvEvent);
    pIf->hRecvEvent = NIL_RTSEMEVENT;
#else
    pIf->pfnRecvAvail    = NULL;
    pIf->pvUserRecvAvail = NULL;
#endif
    RTMemFree(pIf->pDstTab);
    for (int i = kIntNetAddrType_Invalid + 1; i < kIntNetAddrType_End; i++)
        intnetR0IfAddrCacheDestroy(&pIf->aAddrCache[i]);
    RTMemFree(pIf);
    LogFlow(("intnetR0NetworkCreateIf: returns %Rrc\n", rc));
    return rc;
}


/** @interface_method_impl{INTNETTRUNKSWPORT,pfnSetSGPhys} */
static DECLCALLBACK(bool) intnetR0TrunkIfPortSetSGPhys(PINTNETTRUNKSWPORT pSwitchPort, bool fEnable)
{
    PINTNETTRUNKIF pThis = INTNET_SWITCHPORT_2_TRUNKIF(pSwitchPort);
    AssertMsgFailed(("Not implemented because it wasn't required on Darwin\n"));
    return ASMAtomicXchgBool(&pThis->fPhysSG, fEnable);
}


/** @interface_method_impl{INTNETTRUNKSWPORT,pfnReportMacAddress} */
static DECLCALLBACK(void) intnetR0TrunkIfPortReportMacAddress(PINTNETTRUNKSWPORT pSwitchPort, PCRTMAC pMacAddr)
{
    PINTNETTRUNKIF pThis = INTNET_SWITCHPORT_2_TRUNKIF(pSwitchPort);

    /*
     * Get the network instance and grab the address spinlock before making
     * any changes.
     */
    intnetR0BusyIncTrunk(pThis);
    PINTNETNETWORK pNetwork = pThis->pNetwork;
    if (pNetwork)
    {
        RTSpinlockAcquire(pNetwork->hAddrSpinlock);

        pNetwork->MacTab.HostMac = *pMacAddr;
        pThis->MacAddr           = *pMacAddr;

        RTSpinlockRelease(pNetwork->hAddrSpinlock);
    }
    else
        pThis->MacAddr = *pMacAddr;
    intnetR0BusyDecTrunk(pThis);
}


/** @interface_method_impl{INTNETTRUNKSWPORT,pfnReportPromiscuousMode} */
static DECLCALLBACK(void) intnetR0TrunkIfPortReportPromiscuousMode(PINTNETTRUNKSWPORT pSwitchPort, bool fPromiscuous)
{
    PINTNETTRUNKIF pThis = INTNET_SWITCHPORT_2_TRUNKIF(pSwitchPort);

    /*
     * Get the network instance and grab the address spinlock before making
     * any changes.
     */
    intnetR0BusyIncTrunk(pThis);
    PINTNETNETWORK pNetwork = pThis->pNetwork;
    if (pNetwork)
    {
        RTSpinlockAcquire(pNetwork->hAddrSpinlock);

        pNetwork->MacTab.fHostPromiscuousReal = fPromiscuous
                                             || (pNetwork->fFlags & INTNET_OPEN_FLAGS_TRUNK_HOST_PROMISC_MODE);
        pNetwork->MacTab.fHostPromiscuousEff  = pNetwork->MacTab.fHostPromiscuousReal
                                             && (pNetwork->fFlags & INTNET_OPEN_FLAGS_PROMISC_ALLOW_TRUNK_HOST);

        RTSpinlockRelease(pNetwork->hAddrSpinlock);
    }
    intnetR0BusyDecTrunk(pThis);
}


/** @interface_method_impl{INTNETTRUNKSWPORT,pfnReportGsoCapabilities} */
static DECLCALLBACK(void) intnetR0TrunkIfPortReportGsoCapabilities(PINTNETTRUNKSWPORT pSwitchPort,
                                                                   uint32_t fGsoCapabilities, uint32_t fDst)
{
    PINTNETTRUNKIF pThis = INTNET_SWITCHPORT_2_TRUNKIF(pSwitchPort);

    for (unsigned iBit = PDMNETWORKGSOTYPE_END; iBit < 32; iBit++)
        Assert(!(fGsoCapabilities & RT_BIT_32(iBit)));
    Assert(!(fDst & ~INTNETTRUNKDIR_VALID_MASK));
    Assert(fDst);

    if (fDst & INTNETTRUNKDIR_HOST)
        pThis->fHostGsoCapabilites = fGsoCapabilities;

    if (fDst & INTNETTRUNKDIR_WIRE)
        pThis->fWireGsoCapabilites = fGsoCapabilities;
}


/** @interface_method_impl{INTNETTRUNKSWPORT,pfnReportNoPreemptDsts} */
static DECLCALLBACK(void) intnetR0TrunkIfPortReportNoPreemptDsts(PINTNETTRUNKSWPORT pSwitchPort, uint32_t fNoPreemptDsts)
{
    PINTNETTRUNKIF pThis = INTNET_SWITCHPORT_2_TRUNKIF(pSwitchPort);
    Assert(!(fNoPreemptDsts & ~INTNETTRUNKDIR_VALID_MASK));

    pThis->fNoPreemptDsts = fNoPreemptDsts;
}


/** @interface_method_impl{INTNETTRUNKSWPORT,pfnDisconnect} */
static DECLCALLBACK(void) intnetR0TrunkIfPortDisconnect(PINTNETTRUNKSWPORT pSwitchPort, PINTNETTRUNKIFPORT pIfPort,
                                                        PFNINTNETTRUNKIFPORTRELEASEBUSY pfnReleaseBusy)
{
    PINTNETTRUNKIF pThis = INTNET_SWITCHPORT_2_TRUNKIF(pSwitchPort);

    /*
     * The caller has marked the trunk instance busy on his side before making
     * the call (see method docs) to let us safely grab the network and internal
     * network instance pointers without racing the network destruction code
     * (intnetR0TrunkIfDestroy (called by intnetR0TrunkIfDestroy) will wait for
     * the interface to stop being busy before setting pNetwork to NULL and
     * freeing up the resources).
     */
    PINTNETNETWORK pNetwork = pThis->pNetwork;
    if (pNetwork)
    {
        PINTNET pIntNet = pNetwork->pIntNet;
        Assert(pNetwork->pIntNet);

        /*
         * We must decrease the callers busy count here to prevent deadlocking
         * when requesting the big mutex ownership.  This will of course
         * unblock anyone stuck in intnetR0TrunkIfDestroy doing pfnWaitForIdle
         * (the other deadlock party), so we have to revalidate the network
         * pointer after taking ownership of the big mutex.
         */
        if (pfnReleaseBusy)
            pfnReleaseBusy(pIfPort);

        RTSemMutexRequest(pIntNet->hMtxCreateOpenDestroy, RT_INDEFINITE_WAIT);

        if (intnetR0NetworkIsValid(pIntNet, pNetwork))
        {
            Assert(pNetwork->MacTab.pTrunk == pThis); /* Must be valid as long as tehre are no concurrent calls to this method. */
            Assert(pThis->pIfPort == pIfPort);        /* Ditto */

            /*
             * Disconnect the trunk and destroy it, similar to what is done int
             * intnetR0NetworkDestruct.
             */
            pIfPort->pfnSetState(pIfPort, INTNETTRUNKIFSTATE_DISCONNECTING);

            RTSpinlockAcquire(pNetwork->hAddrSpinlock);
            pNetwork->MacTab.pTrunk = NULL;
            RTSpinlockRelease(pNetwork->hAddrSpinlock);

            /*
             * Create a system thread that will attempt to re-connect this trunk periodically
             * hoping that the corresponding filter module reappears in the system. The thread
             * will go away if it succeeds in re-connecting the trunk or if it is signalled.
             */
            int rc = RTThreadCreate(&pNetwork->hTrunkReconnectThread, intnetR0TrunkReconnectThread, pNetwork,
                                    0, RTTHREADTYPE_INFREQUENT_POLLER, RTTHREADFLAGS_WAITABLE, "TRNKRECON");
            AssertRC(rc);

            intnetR0TrunkIfDestroy(pThis, pNetwork);
        }

        RTSemMutexRelease(pIntNet->hMtxCreateOpenDestroy);
    }
    /*
     * We must always release the busy reference.
     */
    else if (pfnReleaseBusy)
        pfnReleaseBusy(pIfPort);
}


/** @interface_method_impl{INTNETTRUNKSWPORT,pfnPreRecv} */
static DECLCALLBACK(INTNETSWDECISION) intnetR0TrunkIfPortPreRecv(PINTNETTRUNKSWPORT pSwitchPort,
                                                                 void const *pvSrc, size_t cbSrc, uint32_t fSrc)
{
    PINTNETTRUNKIF pThis = INTNET_SWITCHPORT_2_TRUNKIF(pSwitchPort);

    /* assert some sanity */
    AssertPtr(pvSrc);
    AssertReturn(cbSrc >= 6, INTNETSWDECISION_BROADCAST);
    Assert(fSrc);

    /*
     * Mark the trunk as busy, make sure we've got a network and that there are
     * some active interfaces around.
     */
    INTNETSWDECISION enmSwDecision = INTNETSWDECISION_TRUNK;
    intnetR0BusyIncTrunk(pThis);
    PINTNETNETWORK pNetwork = pThis->pNetwork;
    if (RT_LIKELY(   pNetwork
                  && pNetwork->cActiveIFs > 0 ))
    {
        /*
         * Lazy bird! No pre-switching of multicast and shared-MAC-on-wire.
         */
        PCRTNETETHERHDR pEthHdr = (PCRTNETETHERHDR)pvSrc;
        if (intnetR0IsMacAddrMulticast(&pEthHdr->DstMac))
            enmSwDecision = INTNETSWDECISION_BROADCAST;
        else if (   fSrc == INTNETTRUNKDIR_WIRE
                 && (pNetwork->fFlags & INTNET_OPEN_FLAGS_SHARED_MAC_ON_WIRE))
            enmSwDecision = INTNETSWDECISION_BROADCAST;
        else
            enmSwDecision = intnetR0NetworkPreSwitchUnicast(pNetwork,
                                                            fSrc,
                                                            cbSrc >= 12 ? &pEthHdr->SrcMac : NULL,
                                                            &pEthHdr->DstMac);
    }

    intnetR0BusyDecTrunk(pThis);
    return enmSwDecision;
}


/** @interface_method_impl{INTNETTRUNKSWPORT,pfnRecv} */
static DECLCALLBACK(bool) intnetR0TrunkIfPortRecv(PINTNETTRUNKSWPORT pSwitchPort, void *pvIf, PINTNETSG pSG, uint32_t fSrc)
{
    PINTNETTRUNKIF pThis = INTNET_SWITCHPORT_2_TRUNKIF(pSwitchPort);

    /* assert some sanity */
    AssertPtr(pSG);
    Assert(fSrc);
    NOREF(pvIf); /* later */

    /*
     * Mark the trunk as busy, make sure we've got a network and that there are
     * some active interfaces around.
     */
    bool fRc = false /* don't drop it */;
    intnetR0BusyIncTrunk(pThis);
    PINTNETNETWORK pNetwork = pThis->pNetwork;
    if (RT_LIKELY(   pNetwork
                  && pNetwork->cActiveIFs > 0 ))
    {
        /*
         * Grab or allocate a destination table.
         */
        bool const      fIntCtx = RTThreadPreemptIsEnabled(NIL_RTTHREAD) || RTThreadIsInInterrupt(NIL_RTTHREAD);
        unsigned        iDstTab = 0;
        PINTNETDSTTAB   pDstTab = NULL;
        RTSpinlockAcquire(pThis->hDstTabSpinlock);
        if (fIntCtx)
        {
            /* Interrupt or restricted context. */
            iDstTab  = RTMpCpuIdToSetIndex(RTMpCpuId());
            iDstTab %= pThis->cIntDstTabs;
            pDstTab  = pThis->apIntDstTabs[iDstTab];
            if (RT_LIKELY(pDstTab))
                pThis->apIntDstTabs[iDstTab] = NULL;
            else
            {
                iDstTab = pThis->cIntDstTabs;
                while (iDstTab-- > 0)
                {
                    pDstTab = pThis->apIntDstTabs[iDstTab];
                    if (pDstTab)
                    {
                        pThis->apIntDstTabs[iDstTab] = NULL;
                        break;
                    }
                }
            }
            RTSpinlockRelease(pThis->hDstTabSpinlock);
            Assert(!pDstTab || iDstTab < pThis->cIntDstTabs);
        }
        else
        {
            /* Task context, fallback is to allocate a table. */
            AssertCompile(RT_ELEMENTS(pThis->apTaskDstTabs) == 2); /* for loop rollout */
            pDstTab = pThis->apIntDstTabs[iDstTab = 0];
            if (!pDstTab)
                pDstTab = pThis->apIntDstTabs[iDstTab = 1];
            if (pDstTab)
            {
                pThis->apIntDstTabs[iDstTab] = NULL;
                RTSpinlockRelease(pThis->hDstTabSpinlock);
                Assert(iDstTab < RT_ELEMENTS(pThis->apTaskDstTabs));
            }
            else
            {
                RTSpinlockRelease(pThis->hDstTabSpinlock);
                intnetR0AllocDstTab(pNetwork->MacTab.cEntriesAllocated, &pDstTab);
                iDstTab = 65535;
            }
        }
        if (RT_LIKELY(pDstTab))
        {
            /*
             * Finally, get down to business of sending the frame.
             */
            INTNETSWDECISION enmSwDecision = intnetR0NetworkSend(pNetwork, NULL, fSrc, pSG, pDstTab);
            AssertMsg(enmSwDecision != INTNETSWDECISION_BAD_CONTEXT, ("fSrc=%#x fTrunkDst=%#x hdr=%.14Rhxs\n", fSrc, pDstTab->fTrunkDst, pSG->aSegs[0].pv));
            if (enmSwDecision == INTNETSWDECISION_INTNET)
                fRc = true; /* drop it */

            /*
             * Free the destination table.
             */
            if (iDstTab == 65535)
                RTMemFree(pDstTab);
            else
            {
                RTSpinlockAcquire(pThis->hDstTabSpinlock);
                if (fIntCtx && !pThis->apIntDstTabs[iDstTab])
                    pThis->apIntDstTabs[iDstTab]  = pDstTab;
                else if (!fIntCtx && !pThis->apTaskDstTabs[iDstTab])
                    pThis->apTaskDstTabs[iDstTab] = pDstTab;
                else
                {
                    /* this shouldn't happen! */
                    PINTNETDSTTAB *papDstTabs = fIntCtx ? &pThis->apIntDstTabs[0] :            &pThis->apTaskDstTabs[0];
                    iDstTab                   = fIntCtx ? pThis->cIntDstTabs      : RT_ELEMENTS(pThis->apTaskDstTabs);
                    while (iDstTab-- > 0)
                        if (!papDstTabs[iDstTab])
                        {
                            papDstTabs[iDstTab] = pDstTab;
                            break;
                        }
                }
                RTSpinlockRelease(pThis->hDstTabSpinlock);
                Assert(iDstTab < RT_MAX(RT_ELEMENTS(pThis->apTaskDstTabs), pThis->cIntDstTabs));
            }
        }
    }

    intnetR0BusyDecTrunk(pThis);
    return fRc;
}


/** @interface_method_impl{INTNETTRUNKSWPORT,pfnSGRetain} */
static DECLCALLBACK(void) intnetR0TrunkIfPortSGRetain(PINTNETTRUNKSWPORT pSwitchPort, PINTNETSG pSG)
{
    PINTNETTRUNKIF pThis = INTNET_SWITCHPORT_2_TRUNKIF(pSwitchPort);
    PINTNETNETWORK pNetwork = pThis->pNetwork;

    /* assert some sanity */
    AssertPtrReturnVoid(pNetwork);
    AssertReturnVoid(pNetwork->hEvtBusyIf != NIL_RTSEMEVENT);
    AssertPtr(pSG);
    Assert(pSG->cUsers > 0 && pSG->cUsers < 256);

    /* do it. */
    ++pSG->cUsers;
}


/** @interface_method_impl{INTNETTRUNKSWPORT,pfnSGRelease} */
static DECLCALLBACK(void) intnetR0TrunkIfPortSGRelease(PINTNETTRUNKSWPORT pSwitchPort, PINTNETSG pSG)
{
    PINTNETTRUNKIF pThis = INTNET_SWITCHPORT_2_TRUNKIF(pSwitchPort);
    PINTNETNETWORK pNetwork = pThis->pNetwork;

    /* assert some sanity */
    AssertPtrReturnVoid(pNetwork);
    AssertReturnVoid(pNetwork->hEvtBusyIf != NIL_RTSEMEVENT);
    AssertPtr(pSG);
    Assert(pSG->cUsers > 0);

    /*
     * Free it?
     */
    if (!--pSG->cUsers)
    {
        /** @todo later */
    }
}


/** @interface_method_impl{INTNETTRUNKSWPORT,pfnNotifyHostAddress} */
static DECLCALLBACK(void) intnetR0NetworkNotifyHostAddress(PINTNETTRUNKSWPORT pSwitchPort,
                                                           bool fAdded,
                                                           INTNETADDRTYPE enmType, const void *pvAddr)
{
    PINTNETTRUNKIF pTrunkIf = INTNET_SWITCHPORT_2_TRUNKIF(pSwitchPort);
    PINTNETNETWORK pNetwork = pTrunkIf->pNetwork;
    PCRTNETADDRU pAddr = (PCRTNETADDRU)pvAddr;
    uint8_t cbAddr;

    if (enmType == kIntNetAddrType_IPv4)
    {
        Log(("%s: %s %RTnaipv4\n",
             __FUNCTION__, (fAdded ? "add" : "del"),
             pAddr->IPv4));
        cbAddr = 4;
    }
    else if (enmType == kIntNetAddrType_IPv6)
    {
        Log(("%s: %s %RTnaipv6\n",
             __FUNCTION__, (fAdded ? "add" : "del"),
             pAddr));
        cbAddr = 16;
    }
    else
    {
        Log(("%s: unexpected address type %d\n", __FUNCTION__, enmType));
        return;
    }

    RTSpinlockAcquire(pNetwork->hAddrSpinlock);
    if (fAdded)         /* one of host interfaces got a new address */
    {
        /* blacklist it to prevent spoofing by guests */
        intnetR0NetworkBlacklistAdd(pNetwork, pAddr, enmType);

        /* kick out any guest that uses it */
        intnetR0NetworkAddrCacheDeleteLocked(pNetwork, pAddr, enmType, cbAddr, "tif/host");
    }
    else                /* address deleted from one of host interfaces */
    {
        /* stop blacklisting it, guests may use it now */
        intnetR0NetworkBlacklistDelete(pNetwork, pAddr, enmType);
    }
    RTSpinlockRelease(pNetwork->hAddrSpinlock);
}


/**
 * Shutdown the trunk interface.
 *
 * @param   pThis       The trunk.
 * @param   pNetworks   The network.
 *
 * @remarks The caller must hold the global lock.
 */
static void intnetR0TrunkIfDestroy(PINTNETTRUNKIF pThis, PINTNETNETWORK pNetwork)
{
    /* assert sanity */
    if (!pThis)
        return;
    AssertPtr(pThis);
    Assert(pThis->pNetwork == pNetwork);
    AssertPtrNull(pThis->pIfPort);

    /*
     * The interface has already been deactivated, we just to wait for
     * it to become idle before we can disconnect and release it.
     */
    PINTNETTRUNKIFPORT pIfPort = pThis->pIfPort;
    if (pIfPort)
    {
        /* unset it */
        pThis->pIfPort = NULL;

        /* wait in portions so we can complain every now an then. */
        uint64_t StartTS = RTTimeSystemNanoTS();
        int rc = pIfPort->pfnWaitForIdle(pIfPort, 10*1000);
        if (RT_FAILURE(rc))
        {
            LogRel(("intnet: '%s' didn't become idle in %RU64 ns (%Rrc).\n",
                    pNetwork->szName, RTTimeSystemNanoTS() - StartTS, rc));
            Assert(rc == VERR_TIMEOUT);
            while (     RT_FAILURE(rc)
                   &&   RTTimeSystemNanoTS() - StartTS < UINT64_C(30000000000)) /* 30 sec */
                rc = pIfPort->pfnWaitForIdle(pIfPort, 10*1000);
            if (rc == VERR_TIMEOUT)
            {
                LogRel(("intnet: '%s' didn't become idle in %RU64 ns (%Rrc).\n",
                        pNetwork->szName, RTTimeSystemNanoTS() - StartTS, rc));
                while (     rc == VERR_TIMEOUT
                       &&   RTTimeSystemNanoTS() - StartTS < UINT64_C(360000000000)) /* 360 sec */
                    rc = pIfPort->pfnWaitForIdle(pIfPort, 30*1000);
                if (RT_FAILURE(rc))
                {
                    LogRel(("intnet: '%s' didn't become idle in %RU64 ns (%Rrc), giving up.\n",
                            pNetwork->szName, RTTimeSystemNanoTS() - StartTS, rc));
                    AssertRC(rc);
                }
            }
        }

        /* disconnect & release it. */
        pIfPort->pfnDisconnectAndRelease(pIfPort);
    }

    /*
     * Free up the resources.
     */
    pThis->pNetwork = NULL; /* Must not be cleared while busy, see intnetR0TrunkIfPortDisconnect. */
    RTSpinlockDestroy(pThis->hDstTabSpinlock);
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->apTaskDstTabs); i++)
    {
        Assert(pThis->apTaskDstTabs[i]);
        RTMemFree(pThis->apTaskDstTabs[i]);
        pThis->apTaskDstTabs[i] = NULL;
    }
    for (unsigned i = 0; i < pThis->cIntDstTabs; i++)
    {
        Assert(pThis->apIntDstTabs[i]);
        RTMemFree(pThis->apIntDstTabs[i]);
        pThis->apIntDstTabs[i] = NULL;
    }
    RTMemFree(pThis);
}


/**
 * Creates the trunk connection (if any).
 *
 * @returns VBox status code.
 *
 * @param   pNetwork    The newly created network.
 * @param   pSession    The session handle.
 */
static int intnetR0NetworkCreateTrunkIf(PINTNETNETWORK pNetwork, PSUPDRVSESSION pSession)
{
    const char *pszName;
    switch (pNetwork->enmTrunkType)
    {
        /*
         * The 'None' case, simple.
         */
        case kIntNetTrunkType_None:
        case kIntNetTrunkType_WhateverNone:
#ifdef VBOX_WITH_NAT_SERVICE
            /*
             * Well, here we don't want load anything special,
             * just communicate between processes via internal network.
             */
        case kIntNetTrunkType_SrvNat:
#endif
            return VINF_SUCCESS;

        /* Can't happen, but makes GCC happy. */
        default:
            return VERR_NOT_IMPLEMENTED;

        /*
         * Translate enum to component factory name.
         */
        case kIntNetTrunkType_NetFlt:
            pszName = "VBoxNetFlt";
            break;
        case kIntNetTrunkType_NetAdp:
#if defined(RT_OS_DARWIN) && !defined(VBOXNETADP_DO_NOT_USE_NETFLT)
            pszName = "VBoxNetFlt";
#else /* VBOXNETADP_DO_NOT_USE_NETFLT */
            pszName = "VBoxNetAdp";
#endif /* VBOXNETADP_DO_NOT_USE_NETFLT */
            break;
#ifndef VBOX_WITH_NAT_SERVICE
        case kIntNetTrunkType_SrvNat:
            pszName = "VBoxSrvNat";
            break;
#endif
    }

    /*
     * Allocate the trunk interface and associated destination tables.
     *
     * We take a very optimistic view on the parallelism of the host
     * network stack and NIC driver.  So, we allocate one table for each
     * possible CPU to deal with interrupt time requests and one for task
     * time calls.
     */
    RTCPUID         cCpus    = RTMpGetCount(); Assert(cCpus > 0);
    PINTNETTRUNKIF  pTrunk = (PINTNETTRUNKIF)RTMemAllocZ(RT_UOFFSETOF_DYN(INTNETTRUNKIF, apIntDstTabs[cCpus]));
    if (!pTrunk)
        return VERR_NO_MEMORY;

    Assert(pNetwork->MacTab.cEntriesAllocated > 0);
    int rc = VINF_SUCCESS;
    pTrunk->cIntDstTabs = cCpus;
    for (unsigned i = 0; i < cCpus && RT_SUCCESS(rc); i++)
        rc = intnetR0AllocDstTab(pNetwork->MacTab.cEntriesAllocated, &pTrunk->apIntDstTabs[i]);
    for (unsigned i = 0; i < RT_ELEMENTS(pTrunk->apTaskDstTabs) && RT_SUCCESS(rc); i++)
        rc = intnetR0AllocDstTab(pNetwork->MacTab.cEntriesAllocated, &pTrunk->apTaskDstTabs[i]);

    if (RT_SUCCESS(rc))
    {
        pTrunk->SwitchPort.u32Version                 = INTNETTRUNKSWPORT_VERSION;
        pTrunk->SwitchPort.pfnPreRecv                 = intnetR0TrunkIfPortPreRecv;
        pTrunk->SwitchPort.pfnRecv                    = intnetR0TrunkIfPortRecv;
        pTrunk->SwitchPort.pfnSGRetain                = intnetR0TrunkIfPortSGRetain;
        pTrunk->SwitchPort.pfnSGRelease               = intnetR0TrunkIfPortSGRelease;
        pTrunk->SwitchPort.pfnSetSGPhys               = intnetR0TrunkIfPortSetSGPhys;
        pTrunk->SwitchPort.pfnReportMacAddress        = intnetR0TrunkIfPortReportMacAddress;
        pTrunk->SwitchPort.pfnReportPromiscuousMode   = intnetR0TrunkIfPortReportPromiscuousMode;
        pTrunk->SwitchPort.pfnReportGsoCapabilities   = intnetR0TrunkIfPortReportGsoCapabilities;
        pTrunk->SwitchPort.pfnReportNoPreemptDsts     = intnetR0TrunkIfPortReportNoPreemptDsts;
        if (pNetwork->fFlags & INTNET_OPEN_FLAGS_SHARED_MAC_ON_WIRE)
            pTrunk->SwitchPort.pfnNotifyHostAddress   = intnetR0NetworkNotifyHostAddress;
        pTrunk->SwitchPort.pfnDisconnect              = intnetR0TrunkIfPortDisconnect;
        pTrunk->SwitchPort.u32VersionEnd              = INTNETTRUNKSWPORT_VERSION;
        //pTrunk->pIfPort                 = NULL;
        pTrunk->pNetwork                  = pNetwork;
        pTrunk->MacAddr.au8[0]            = 0xff;
        pTrunk->MacAddr.au8[1]            = 0xff;
        pTrunk->MacAddr.au8[2]            = 0xff;
        pTrunk->MacAddr.au8[3]            = 0xff;
        pTrunk->MacAddr.au8[4]            = 0xff;
        pTrunk->MacAddr.au8[5]            = 0xff;
        //pTrunk->fPhysSG                 = false;
        //pTrunk->fUnused                 = false;
        //pTrunk->cBusy                   = 0;
        //pTrunk->fNoPreemptDsts          = 0;
        //pTrunk->fWireGsoCapabilites     = 0;
        //pTrunk->fHostGsoCapabilites     = 0;
        //pTrunk->abGsoHdrs               = {0};
        pTrunk->hDstTabSpinlock           = NIL_RTSPINLOCK;
        //pTrunk->apTaskDstTabs           = above;
        //pTrunk->cIntDstTabs             = above;
        //pTrunk->apIntDstTabs            = above;

        /*
         * Create the lock (we've NIL'ed the members above to simplify cleanup).
         */
        rc = RTSpinlockCreate(&pTrunk->hDstTabSpinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "hDstTabSpinlock");
        if (RT_SUCCESS(rc))
        {
            /*
             * There are a couple of bits in MacTab as well pertaining to the
             * trunk.  We have to set this before it's reported.
             *
             * Note! We don't need to lock the MacTab here - creation time.
             */
            pNetwork->MacTab.pTrunk               = pTrunk;
            pNetwork->MacTab.HostMac              = pTrunk->MacAddr;
            pNetwork->MacTab.fHostPromiscuousReal = false;
            pNetwork->MacTab.fHostPromiscuousEff  = (pNetwork->fFlags & INTNET_OPEN_FLAGS_TRUNK_HOST_PROMISC_MODE)
                                                 && (pNetwork->fFlags & INTNET_OPEN_FLAGS_PROMISC_ALLOW_TRUNK_HOST);
            pNetwork->MacTab.fHostActive          = false;
            pNetwork->MacTab.fWirePromiscuousReal = RT_BOOL(pNetwork->fFlags & INTNET_OPEN_FLAGS_TRUNK_WIRE_PROMISC_MODE);
            pNetwork->MacTab.fWirePromiscuousEff  = pNetwork->MacTab.fWirePromiscuousReal
                                                 && (pNetwork->fFlags & INTNET_OPEN_FLAGS_PROMISC_ALLOW_TRUNK_WIRE);
            pNetwork->MacTab.fWireActive          = false;

#ifdef IN_RING0 /* (testcase is ring-3) */
            /*
             * Query the factory we want, then use it create and connect the trunk.
             */
            PINTNETTRUNKFACTORY pTrunkFactory = NULL;
            rc = SUPR0ComponentQueryFactory(pSession, pszName, INTNETTRUNKFACTORY_UUID_STR, (void **)&pTrunkFactory);
            if (RT_SUCCESS(rc))
            {
                rc = pTrunkFactory->pfnCreateAndConnect(pTrunkFactory,
                                                        pNetwork->szTrunk,
                                                        &pTrunk->SwitchPort,
                                                        pNetwork->fFlags & INTNET_OPEN_FLAGS_SHARED_MAC_ON_WIRE
                                                        ? INTNETTRUNKFACTORY_FLAG_NO_PROMISC
                                                        : 0,
                                                        &pTrunk->pIfPort);
                pTrunkFactory->pfnRelease(pTrunkFactory);
                if (RT_SUCCESS(rc))
                {
                    Assert(pTrunk->pIfPort);

                    Log(("intnetR0NetworkCreateTrunkIf: VINF_SUCCESS - pszName=%s szTrunk=%s%s Network=%s\n",
                         pszName, pNetwork->szTrunk, pNetwork->fFlags & INTNET_OPEN_FLAGS_SHARED_MAC_ON_WIRE ? " shared-mac" : "", pNetwork->szName));
                    return VINF_SUCCESS;
                }
            }
#else  /* IN_RING3 */
            NOREF(pSession);
            rc = VERR_NOT_SUPPORTED;
#endif /* IN_RING3 */

            pNetwork->MacTab.pTrunk      = NULL;
        }

        /* bail out and clean up. */
        RTSpinlockDestroy(pTrunk->hDstTabSpinlock);
    }

    for (unsigned i = 0; i < RT_ELEMENTS(pTrunk->apTaskDstTabs); i++)
        RTMemFree(pTrunk->apTaskDstTabs[i]);
    for (unsigned i = 0; i < pTrunk->cIntDstTabs; i++)
        RTMemFree(pTrunk->apIntDstTabs[i]);
    RTMemFree(pTrunk);

    LogFlow(("intnetR0NetworkCreateTrunkIf: %Rrc - pszName=%s szTrunk=%s Network=%s\n",
             rc, pszName, pNetwork->szTrunk, pNetwork->szName));
    return rc;
}


/**
 * Trunk reconnection thread function. It runs until signalled by another thread or by itself (upon
 * successful trunk re-connection).
 *
 * Note that this function erases pNetwork->hTrunkReconnectThread right before it terminates!
 */
static DECLCALLBACK(int) intnetR0TrunkReconnectThread(RTTHREAD hThread, void *pvUser)
{
    RT_NOREF1(hThread);
    PINTNETNETWORK pNetwork = (PINTNETNETWORK)pvUser;
    PINTNET pIntNet = pNetwork->pIntNet;
    Assert(pNetwork->pIntNet);

    /*
     * We attempt to reconnect the trunk every 5 seconds until somebody signals us.
     */
    while (!pNetwork->fTerminateReconnectThread && RTThreadUserWait(hThread, 5 * RT_MS_1SEC) == VERR_TIMEOUT)
    {
        /*
         * Make sure nobody else is modifying networks.
         * It is essential we give up on waiting for the big mutex much earlier than intnetR0NetworkDestruct
         * gives up on waiting for us to terminate! This is why we wait for 1 second while network destruction
         * code waits for 5 seconds. Otherwise the network may be already gone by the time we get the mutex.
         */
        if (RT_FAILURE(RTSemMutexRequestNoResume(pIntNet->hMtxCreateOpenDestroy, RT_MS_1SEC)))
            continue;
#if 0
        /*
         * This thread should be long gone by the time the network has been destroyed, but if we are
         * really paranoid we should include the following code.
         */
        /*
         * The network could have been destroyed while we were waiting on the big mutex, let us verify
         * it is still valid by going over the list of existing networks.
         */
        PINTNETNETWORK pExistingNetwork = pIntNet->pNetworks;
        for (; pExistingNetwork; pExistingNetwork = pExistingNetwork->pNext)
            if (pExistingNetwork == pNetwork)
                break;
        /* We need the network to exist and to have at least one interface. */
        if (pExistingNetwork && pNetwork->MacTab.cEntries)
#else
        /* We need the network to have at least one interface. */
        if (pNetwork->MacTab.cEntries)
#endif
        {
            PINTNETIF pAnyIf = pNetwork->MacTab.paEntries[0].pIf;
            PSUPDRVSESSION pAnySession = pAnyIf ? pAnyIf->pSession : NULL;
            if (pAnySession)
            {
                /* Attempt to re-connect trunk and if successful, terminate thread. */
                if (RT_SUCCESS(intnetR0NetworkCreateTrunkIf(pNetwork, pAnySession)))
                {
                    /* The network has active interfaces, we need to activate the trunk. */
                    if (pNetwork->cActiveIFs)
                    {
                        PINTNETTRUNKIF pTrunk = pNetwork->MacTab.pTrunk;
                        /* The intnetR0NetworkCreateTrunkIf call resets fHostActive and fWireActive. */
                        RTSpinlockAcquire(pNetwork->hAddrSpinlock);
                        pNetwork->MacTab.fHostActive = RT_BOOL(pNetwork->fFlags & INTNET_OPEN_FLAGS_TRUNK_HOST_ENABLED);
                        pNetwork->MacTab.fWireActive = RT_BOOL(pNetwork->fFlags & INTNET_OPEN_FLAGS_TRUNK_WIRE_ENABLED);
                        RTSpinlockRelease(pNetwork->hAddrSpinlock);
                        pTrunk->pIfPort->pfnSetState(pTrunk->pIfPort, INTNETTRUNKIFSTATE_ACTIVE);
                    }
                    pNetwork->fTerminateReconnectThread = true;
                    RTThreadUserSignal(hThread); /* Signal ourselves, so we break the loop after releasing the mutex */
                }
            }
        }
        RTSemMutexRelease(pIntNet->hMtxCreateOpenDestroy);
    }

    /*
     * Destroy our handle in INTNETNETWORK so everyone knows we are gone.
     * Note that this is the only place where this handle gets wiped out.
     */
    pNetwork->hTrunkReconnectThread = NIL_RTTHREAD;

    return VINF_SUCCESS;
}



/**
 * Object destructor callback.
 * This is called for reference counted objectes when the count reaches 0.
 *
 * @param   pvObj       The object pointer.
 * @param   pvUser1     Pointer to the network.
 * @param   pvUser2     Pointer to the INTNET instance data.
 */
static DECLCALLBACK(void) intnetR0NetworkDestruct(void *pvObj, void *pvUser1, void *pvUser2)
{
    PINTNETNETWORK  pNetwork = (PINTNETNETWORK)pvUser1;
    PINTNET         pIntNet  = (PINTNET)pvUser2;
    Log(("intnetR0NetworkDestruct: pvObj=%p pNetwork=%p pIntNet=%p %s\n", pvObj, pNetwork, pIntNet, pNetwork->szName));
    Assert(pNetwork->pIntNet == pIntNet);
    RT_NOREF1(pvObj);

    /* Take the big create/open/destroy sem. */
    RTSemMutexRequest(pIntNet->hMtxCreateOpenDestroy, RT_INDEFINITE_WAIT);

    /*
     * Tell the trunk, if present, that we're about to disconnect it and wish
     * no further calls from it.
     */
    PINTNETTRUNKIF pTrunk = pNetwork->MacTab.pTrunk;
    if (pTrunk)
        pTrunk->pIfPort->pfnSetState(pTrunk->pIfPort, INTNETTRUNKIFSTATE_DISCONNECTING);

    /*
     * Deactivate and orphan any remaining interfaces and wait for them to idle.
     *
     * Note! Normally there are no more interfaces at this point, however, when
     *       supdrvCloseSession / supdrvCleanupSession release the objects the
     *       order is undefined.  So, it's quite possible that the network will
     *       be dereference and destroyed before the interfaces.
     */
    RTSpinlockAcquire(pNetwork->hAddrSpinlock);

    uint32_t iIf = pNetwork->MacTab.cEntries;
    while (iIf-- > 0)
    {
        pNetwork->MacTab.paEntries[iIf].fActive      = false;
        pNetwork->MacTab.paEntries[iIf].pIf->fActive = false;
    }

    pNetwork->MacTab.fHostActive = false;
    pNetwork->MacTab.fWireActive = false;

    RTSpinlockRelease(pNetwork->hAddrSpinlock);

    /* Wait for all the interfaces to quiesce.  (Interfaces cannot be
       removed / added since we're holding the big lock.) */
    if (pTrunk)
        intnetR0BusyWait(pNetwork, &pTrunk->cBusy);
    else if (pNetwork->hTrunkReconnectThread != NIL_RTTHREAD)
    {
        /*
         * There is no trunk and we have the trunk reconnection thread running.
         * Signal the thread and wait for it to terminate.
         */
        pNetwork->fTerminateReconnectThread = true;
        RTThreadUserSignal(pNetwork->hTrunkReconnectThread);
        /*
         * The tread cannot be re-connecting the trunk at the moment since we hold the big
         * mutex, thus 5 second wait is definitely enough. Note that the wait time must
         * exceed the time the reconnection thread waits on acquiring the big mutex, otherwise
         * we will give up waiting for thread termination prematurely. Unfortunately it seems
         * we have no way to terminate the thread if it failed to stop gracefully.
         *
         * Note that it is ok if the thread has already wiped out hTrunkReconnectThread by now,
         * this means we no longer need to wait for it.
         */
        RTThreadWait(pNetwork->hTrunkReconnectThread, 5 * RT_MS_1SEC, NULL);
    }

    iIf = pNetwork->MacTab.cEntries;
    while (iIf-- > 0)
        intnetR0BusyWait(pNetwork, &pNetwork->MacTab.paEntries[iIf].pIf->cBusy);

    /* Orphan the interfaces (not trunk).  Don't bother with calling
       pfnDisconnectInterface here since the networking is going away. */
    RTSpinlockAcquire(pNetwork->hAddrSpinlock);
    while ((iIf = pNetwork->MacTab.cEntries) > 0)
    {
        PINTNETIF pIf = pNetwork->MacTab.paEntries[iIf - 1].pIf;
        RTSpinlockRelease(pNetwork->hAddrSpinlock);

        intnetR0BusyWait(pNetwork, &pIf->cBusy);

        RTSpinlockAcquire(pNetwork->hAddrSpinlock);
        if (   iIf == pNetwork->MacTab.cEntries /* paranoia */
            && pIf->cBusy)
        {
            pIf->pNetwork = NULL;
            pNetwork->MacTab.cEntries--;
        }
    }

    /*
     * Zap the trunk pointer while we still own the spinlock, destroy the
     * trunk after we've left it.  Note that this might take a while...
     */
    pNetwork->MacTab.pTrunk = NULL;

    RTSpinlockRelease(pNetwork->hAddrSpinlock);

    if (pTrunk)
        intnetR0TrunkIfDestroy(pTrunk, pNetwork);

    /*
     * Unlink the network.
     * Note that it needn't be in the list if we failed during creation.
     */
    PINTNETNETWORK pPrev = pIntNet->pNetworks;
    if (pPrev == pNetwork)
        pIntNet->pNetworks = pNetwork->pNext;
    else
    {
        for (; pPrev; pPrev = pPrev->pNext)
            if (pPrev->pNext == pNetwork)
            {
                pPrev->pNext = pNetwork->pNext;
                break;
            }
    }
    pNetwork->pNext = NULL;
    pNetwork->pvObj = NULL;

    /*
     * Free resources.
     */
    RTSemEventDestroy(pNetwork->hEvtBusyIf);
    pNetwork->hEvtBusyIf = NIL_RTSEMEVENT;
    RTSpinlockDestroy(pNetwork->hAddrSpinlock);
    pNetwork->hAddrSpinlock = NIL_RTSPINLOCK;
    RTMemFree(pNetwork->MacTab.paEntries);
    pNetwork->MacTab.paEntries = NULL;
    for (int i = kIntNetAddrType_Invalid + 1; i < kIntNetAddrType_End; i++)
        intnetR0IfAddrCacheDestroy(&pNetwork->aAddrBlacklist[i]);
    RTMemFree(pNetwork);

    /* Release the create/destroy sem. */
    RTSemMutexRelease(pIntNet->hMtxCreateOpenDestroy);
}


/**
 * Checks if the open network flags are compatible.
 *
 * @returns VBox status code.
 * @param   pNetwork            The network.
 * @param   fFlags              The open network flags.
 */
static int intnetR0CheckOpenNetworkFlags(PINTNETNETWORK pNetwork, uint32_t fFlags)
{
    uint32_t const fNetFlags = pNetwork->fFlags;

    if (  (fFlags    & INTNET_OPEN_FLAGS_SHARED_MAC_ON_WIRE)
        ^ (fNetFlags & INTNET_OPEN_FLAGS_SHARED_MAC_ON_WIRE))
        return VERR_INTNET_INCOMPATIBLE_FLAGS;

    if (fFlags & INTNET_OPEN_FLAGS_REQUIRE_EXACT)
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(g_afIntNetOpenNetworkNetFlags); i++)
            if (   (fFlags & g_afIntNetOpenNetworkNetFlags[i].fPair)
                &&     (fFlags    & g_afIntNetOpenNetworkNetFlags[i].fPair)
                   !=  (fNetFlags & g_afIntNetOpenNetworkNetFlags[i].fPair) )
            return VERR_INTNET_INCOMPATIBLE_FLAGS;
    }

    if (fFlags & INTNET_OPEN_FLAGS_REQUIRE_AS_RESTRICTIVE_POLICIES)
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(g_afIntNetOpenNetworkNetFlags); i++)
            if (    (fFlags    & g_afIntNetOpenNetworkNetFlags[i].fRestrictive)
                && !(fNetFlags & g_afIntNetOpenNetworkNetFlags[i].fRestrictive)
                &&  (fNetFlags & g_afIntNetOpenNetworkNetFlags[i].fFixed) )
                 return VERR_INTNET_INCOMPATIBLE_FLAGS;
    }

    return VINF_SUCCESS;
}


/**
 * Adapts flag changes on network opening.
 *
 * @returns VBox status code.
 * @param   pNetwork            The network.
 * @param   fFlags              The open network flags.
 */
static int intnetR0AdaptOpenNetworkFlags(PINTNETNETWORK pNetwork, uint32_t fFlags)
{
    /*
     * Upgrade the minimum policy flags.
     */
    uint32_t fNetMinFlags = pNetwork->fMinFlags;
    Assert(!(fNetMinFlags & INTNET_OPEN_FLAGS_RELAXED_MASK));
    if (fFlags & INTNET_OPEN_FLAGS_REQUIRE_AS_RESTRICTIVE_POLICIES)
    {
        fNetMinFlags |= fFlags & INTNET_OPEN_FLAGS_STRICT_MASK;
        if (fNetMinFlags != pNetwork->fMinFlags)
        {
            LogRel(("INTNET: %s - min flags changed %#x -> %#x\n", pNetwork->szName, pNetwork->fMinFlags, fNetMinFlags));
            pNetwork->fMinFlags = fNetMinFlags;
        }
    }

    /*
     * Calculate the new network flags.
     * (Depends on fNetMinFlags being recalculated first.)
     */
    uint32_t fNetFlags = pNetwork->fFlags;

    for (uint32_t i = 0; i < RT_ELEMENTS(g_afIntNetOpenNetworkNetFlags); i++)
    {
        Assert(fNetFlags & g_afIntNetOpenNetworkNetFlags[i].fPair);
        Assert(!(fNetMinFlags & g_afIntNetOpenNetworkNetFlags[i].fRelaxed));

        if (!(fFlags & g_afIntNetOpenNetworkNetFlags[i].fPair))
            continue;
        if (fNetFlags & g_afIntNetOpenNetworkNetFlags[i].fFixed)
            continue;

        if (   (fNetMinFlags & g_afIntNetOpenNetworkNetFlags[i].fRestrictive)
            || (fFlags       & g_afIntNetOpenNetworkNetFlags[i].fRestrictive) )
        {
            fNetFlags &= ~g_afIntNetOpenNetworkNetFlags[i].fPair;
            fNetFlags |= g_afIntNetOpenNetworkNetFlags[i].fRestrictive;
        }
        else if (!(fFlags & INTNET_OPEN_FLAGS_REQUIRE_AS_RESTRICTIVE_POLICIES))
        {
            fNetFlags &= ~g_afIntNetOpenNetworkNetFlags[i].fPair;
            fNetFlags |= g_afIntNetOpenNetworkNetFlags[i].fRelaxed;
        }
    }

    for (uint32_t i = 0; i < RT_ELEMENTS(g_afIntNetOpenNetworkNetFlags); i++)
    {
        Assert(fNetFlags & g_afIntNetOpenNetworkNetFlags[i].fPair);
        fNetFlags |= fFlags & g_afIntNetOpenNetworkNetFlags[i].fFixed;
    }

    /*
     * Apply the flags if they changed.
     */
    uint32_t const fOldNetFlags = pNetwork->fFlags;
    if (fOldNetFlags != fNetFlags)
    {
        LogRel(("INTNET: %s - flags changed %#x -> %#x\n", pNetwork->szName, fOldNetFlags, fNetFlags));

        RTSpinlockAcquire(pNetwork->hAddrSpinlock);

        pNetwork->fFlags = fNetFlags;

        /* Recalculate some derived switcher variables. */
        bool fActiveTrunk = pNetwork->MacTab.pTrunk
                         && pNetwork->cActiveIFs > 0;
        pNetwork->MacTab.fHostActive         = fActiveTrunk
                                            && (fNetFlags & INTNET_OPEN_FLAGS_TRUNK_HOST_ENABLED);
        pNetwork->MacTab.fHostPromiscuousEff = (   pNetwork->MacTab.fHostPromiscuousReal
                                                || (fNetFlags & INTNET_OPEN_FLAGS_TRUNK_HOST_PROMISC_MODE))
                                            && (fNetFlags & INTNET_OPEN_FLAGS_PROMISC_ALLOW_TRUNK_HOST);

        pNetwork->MacTab.fWireActive         = fActiveTrunk
                                            && (fNetFlags & INTNET_OPEN_FLAGS_TRUNK_HOST_ENABLED);
        pNetwork->MacTab.fWirePromiscuousReal= RT_BOOL(fNetFlags & INTNET_OPEN_FLAGS_TRUNK_WIRE_PROMISC_MODE);
        pNetwork->MacTab.fWirePromiscuousEff = pNetwork->MacTab.fWirePromiscuousReal
                                            && (fNetFlags & INTNET_OPEN_FLAGS_PROMISC_ALLOW_TRUNK_WIRE);

        if ((fOldNetFlags ^ fNetFlags) & INTNET_OPEN_FLAGS_PROMISC_ALLOW_CLIENTS)
        {
            pNetwork->MacTab.cPromiscuousEntries        = 0;
            pNetwork->MacTab.cPromiscuousNoTrunkEntries = 0;

            uint32_t iIf = pNetwork->MacTab.cEntries;
            while (iIf-- > 0)
            {
                PINTNETMACTABENTRY  pEntry = &pNetwork->MacTab.paEntries[iIf];
                PINTNETIF           pIf2   = pEntry->pIf;
                if (   pIf2 /* paranoia */
                    && pIf2->fPromiscuousReal)
                {
                    bool fPromiscuousEff = (fNetFlags & INTNET_OPEN_FLAGS_PROMISC_ALLOW_CLIENTS)
                                        && (pIf2->fOpenFlags & INTNET_OPEN_FLAGS_IF_PROMISC_ALLOW);
                    pEntry->fPromiscuousEff      = fPromiscuousEff;
                    pEntry->fPromiscuousSeeTrunk = fPromiscuousEff
                                                && (pIf2->fOpenFlags & INTNET_OPEN_FLAGS_IF_PROMISC_SEE_TRUNK);

                    if (pEntry->fPromiscuousEff)
                    {
                        pNetwork->MacTab.cPromiscuousEntries++;
                        if (!pEntry->fPromiscuousSeeTrunk)
                            pNetwork->MacTab.cPromiscuousNoTrunkEntries++;
                    }
                }
            }
        }

        RTSpinlockRelease(pNetwork->hAddrSpinlock);
    }

    return VINF_SUCCESS;
}


/**
 * Opens an existing network.
 *
 * The call must own the INTNET::hMtxCreateOpenDestroy.
 *
 * @returns VBox status code.
 * @param   pIntNet         The instance data.
 * @param   pSession        The current session.
 * @param   pszNetwork      The network name. This has a valid length.
 * @param   enmTrunkType    The trunk type.
 * @param   pszTrunk        The trunk name. Its meaning is specific to the type.
 * @param   fFlags          Flags, see INTNET_OPEN_FLAGS_*.
 * @param   ppNetwork       Where to store the pointer to the network on success.
 */
static int intnetR0OpenNetwork(PINTNET pIntNet, PSUPDRVSESSION pSession, const char *pszNetwork, INTNETTRUNKTYPE enmTrunkType,
                               const char *pszTrunk, uint32_t fFlags, PINTNETNETWORK *ppNetwork)
{
    LogFlow(("intnetR0OpenNetwork: pIntNet=%p pSession=%p pszNetwork=%p:{%s} enmTrunkType=%d pszTrunk=%p:{%s} fFlags=%#x ppNetwork=%p\n",
             pIntNet, pSession, pszNetwork, pszNetwork, enmTrunkType, pszTrunk, pszTrunk, fFlags, ppNetwork));

    /* just pro forma validation, the caller is internal. */
    AssertPtr(pIntNet);
    AssertPtr(pSession);
    AssertPtr(pszNetwork);
    Assert(enmTrunkType > kIntNetTrunkType_Invalid && enmTrunkType < kIntNetTrunkType_End);
    AssertPtr(pszTrunk);
    Assert(!(fFlags & ~INTNET_OPEN_FLAGS_MASK));
    AssertPtr(ppNetwork);
    *ppNetwork = NULL;

    /*
     * Search networks by name.
     */
    PINTNETNETWORK pCur;
    uint8_t cchName = (uint8_t)strlen(pszNetwork);
    Assert(cchName && cchName < sizeof(pCur->szName)); /* caller ensures this */

    pCur = pIntNet->pNetworks;
    while (pCur)
    {
        if (    pCur->cchName == cchName
            &&  !memcmp(pCur->szName, pszNetwork, cchName))
        {
            /*
             * Found the network, now check that we have the same ideas
             * about the trunk setup and security.
             */
            int rc;
            if (   enmTrunkType == kIntNetTrunkType_WhateverNone
#ifdef VBOX_WITH_NAT_SERVICE
                || enmTrunkType == kIntNetTrunkType_SrvNat /** @todo what does it mean */
#endif
                || (   pCur->enmTrunkType == enmTrunkType
                    && !strcmp(pCur->szTrunk, pszTrunk)))
            {
                rc = intnetR0CheckOpenNetworkFlags(pCur, fFlags);
                if (RT_SUCCESS(rc))
                {
                    /*
                     * Increment the reference and check that the session
                     * can access this network.
                     */
                    rc = SUPR0ObjAddRef(pCur->pvObj, pSession);
                    if (RT_SUCCESS(rc))
                    {
                        if (pCur->fFlags & INTNET_OPEN_FLAGS_ACCESS_RESTRICTED)
                            rc = SUPR0ObjVerifyAccess(pCur->pvObj, pSession, pCur->szName);
                        if (RT_SUCCESS(rc))
                            *ppNetwork = pCur;
                        else
                            SUPR0ObjRelease(pCur->pvObj, pSession);
                    }
                    else if (rc == VERR_WRONG_ORDER)
                        rc = VERR_NOT_FOUND; /* destruction race, pretend the other isn't there. */
                }
            }
            else
            {
                rc = VERR_INTNET_INCOMPATIBLE_TRUNK;
                LogRel(("intnetR0OpenNetwork failed. rc=%Rrc pCur->szTrunk=%s pszTrunk=%s pCur->enmTrunkType=%d enmTrunkType=%d\n",
                        rc, pCur->szTrunk, pszTrunk, pCur->enmTrunkType, enmTrunkType));
            }

            LogFlow(("intnetR0OpenNetwork: returns %Rrc *ppNetwork=%p\n", rc, *ppNetwork));
            return rc;
        }

        pCur = pCur->pNext;
    }

    LogFlow(("intnetR0OpenNetwork: returns VERR_NOT_FOUND\n"));
    return VERR_NOT_FOUND;
}


/**
 * Creates a new network.
 *
 * The call must own the INTNET::hMtxCreateOpenDestroy and has already attempted
 * opening the network and found it to be non-existing.
 *
 * @returns VBox status code.
 * @param   pIntNet         The instance data.
 * @param   pSession        The session handle.
 * @param   pszNetwork      The name of the network. This must be at least one character long and no longer
 *                          than the INTNETNETWORK::szName.
 * @param   enmTrunkType    The trunk type.
 * @param   pszTrunk        The trunk name. Its meaning is specific to the type.
 * @param   fFlags          Flags, see INTNET_OPEN_FLAGS_*.
 * @param   ppNetwork       Where to store the network. In the case of failure
 *                          whatever is returned here should be dereferenced
 *                          outside the INTNET::hMtxCreateOpenDestroy.
 */
static int intnetR0CreateNetwork(PINTNET pIntNet, PSUPDRVSESSION pSession, const char *pszNetwork, INTNETTRUNKTYPE enmTrunkType,
                                 const char *pszTrunk, uint32_t fFlags, PINTNETNETWORK *ppNetwork)
{
    LogFlow(("intnetR0CreateNetwork: pIntNet=%p pSession=%p pszNetwork=%p:{%s} enmTrunkType=%d pszTrunk=%p:{%s} fFlags=%#x ppNetwork=%p\n",
             pIntNet, pSession, pszNetwork, pszNetwork, enmTrunkType, pszTrunk, pszTrunk, fFlags, ppNetwork));

    /* just pro forma validation, the caller is internal. */
    AssertPtr(pIntNet);
    AssertPtr(pSession);
    AssertPtr(pszNetwork);
    Assert(enmTrunkType > kIntNetTrunkType_Invalid && enmTrunkType < kIntNetTrunkType_End);
    AssertPtr(pszTrunk);
    Assert(!(fFlags & ~INTNET_OPEN_FLAGS_MASK));
    AssertPtr(ppNetwork);

    *ppNetwork = NULL;

    /*
     * Adjust the flags with defaults for the network policies.
     * Note: Main restricts promiscuous mode on the per interface level.
     */
    fFlags &= ~(  INTNET_OPEN_FLAGS_IF_FIXED
                | INTNET_OPEN_FLAGS_IF_PROMISC_ALLOW
                | INTNET_OPEN_FLAGS_IF_PROMISC_DENY
                | INTNET_OPEN_FLAGS_IF_PROMISC_SEE_TRUNK
                | INTNET_OPEN_FLAGS_IF_PROMISC_NO_TRUNK
                | INTNET_OPEN_FLAGS_REQUIRE_AS_RESTRICTIVE_POLICIES
                | INTNET_OPEN_FLAGS_REQUIRE_EXACT);
    uint32_t fDefFlags = INTNET_OPEN_FLAGS_PROMISC_ALLOW_CLIENTS
                       | INTNET_OPEN_FLAGS_PROMISC_ALLOW_TRUNK_HOST
                       | INTNET_OPEN_FLAGS_PROMISC_ALLOW_TRUNK_WIRE
                       | INTNET_OPEN_FLAGS_TRUNK_HOST_ENABLED
                       | INTNET_OPEN_FLAGS_TRUNK_HOST_CHASTE_MODE
                       | INTNET_OPEN_FLAGS_TRUNK_WIRE_ENABLED
                       | INTNET_OPEN_FLAGS_TRUNK_WIRE_CHASTE_MODE;
    if (   enmTrunkType == kIntNetTrunkType_WhateverNone
#ifdef VBOX_WITH_NAT_SERVICE
        || enmTrunkType == kIntNetTrunkType_SrvNat /* simialar security */
#endif
        || enmTrunkType == kIntNetTrunkType_None)
        fDefFlags |= INTNET_OPEN_FLAGS_ACCESS_RESTRICTED;
    else
        fDefFlags |= INTNET_OPEN_FLAGS_ACCESS_PUBLIC;
    for (uint32_t i = 0; i < RT_ELEMENTS(g_afIntNetOpenNetworkNetFlags); i++)
        if (!(fFlags & g_afIntNetOpenNetworkNetFlags[i].fPair))
            fFlags |= g_afIntNetOpenNetworkNetFlags[i].fPair & fDefFlags;

    /*
     * Allocate and initialize.
     */
    size_t cb = sizeof(INTNETNETWORK);
    if (fFlags & INTNET_OPEN_FLAGS_SHARED_MAC_ON_WIRE)
        cb += INTNETNETWORK_TMP_SIZE + 64;
    PINTNETNETWORK pNetwork = (PINTNETNETWORK)RTMemAllocZ(cb);
    if (!pNetwork)
        return VERR_NO_MEMORY;
    //pNetwork->pNext                       = NULL;
    //pNetwork->pIfs                        = NULL;
    //pNetwork->fTerminateReconnectThread   = false;
    pNetwork->hTrunkReconnectThread         = NIL_RTTHREAD;
    pNetwork->hAddrSpinlock                 = NIL_RTSPINLOCK;
    pNetwork->MacTab.cEntries               = 0;
    pNetwork->MacTab.cEntriesAllocated      = INTNET_GROW_DSTTAB_SIZE;
    //pNetwork->MacTab.cPromiscuousEntries  = 0;
    //pNetwork->MacTab.cPromiscuousNoTrunkEntries = 0;
    pNetwork->MacTab.paEntries              = NULL;
    pNetwork->MacTab.fHostPromiscuousReal   = false;
    pNetwork->MacTab.fHostPromiscuousEff    = false;
    pNetwork->MacTab.fHostActive            = false;
    pNetwork->MacTab.fWirePromiscuousReal   = false;
    pNetwork->MacTab.fWirePromiscuousEff    = false;
    pNetwork->MacTab.fWireActive            = false;
    pNetwork->MacTab.pTrunk                 = NULL;
    pNetwork->hEvtBusyIf                    = NIL_RTSEMEVENT;
    pNetwork->pIntNet                       = pIntNet;
    //pNetwork->pvObj                       = NULL;
    if (fFlags & INTNET_OPEN_FLAGS_SHARED_MAC_ON_WIRE)
        pNetwork->pbTmp                     = RT_ALIGN_PT(pNetwork + 1, 64, uint8_t *);
    //else
    //    pNetwork->pbTmp                   = NULL;
    pNetwork->fFlags                        = fFlags;
    //pNetwork->fMinFlags                   = 0;
    //pNetwork->cActiveIFs                  = 0;
    size_t cchName                          = strlen(pszNetwork);
    pNetwork->cchName                       = (uint8_t)cchName;
    Assert(cchName && cchName < sizeof(pNetwork->szName));  /* caller's responsibility. */
    memcpy(pNetwork->szName, pszNetwork, cchName);          /* '\0' at courtesy of alloc. */
    pNetwork->enmTrunkType                  = enmTrunkType;
    Assert(strlen(pszTrunk) < sizeof(pNetwork->szTrunk));   /* caller's responsibility. */
    strcpy(pNetwork->szTrunk, pszTrunk);

    /*
     * Create the semaphore, spinlock and allocate the interface table.
     */
    int rc = RTSemEventCreate(&pNetwork->hEvtBusyIf);
    if (RT_SUCCESS(rc))
        rc = RTSpinlockCreate(&pNetwork->hAddrSpinlock, RTSPINLOCK_FLAGS_INTERRUPT_SAFE, "hAddrSpinlock");
    if (RT_SUCCESS(rc))
    {
        pNetwork->MacTab.paEntries = (PINTNETMACTABENTRY)RTMemAlloc(sizeof(INTNETMACTABENTRY) * pNetwork->MacTab.cEntriesAllocated);
        if (!pNetwork->MacTab.paEntries)
            rc = VERR_NO_MEMORY;
    }
    if (RT_SUCCESS(rc))
    {
        for (int i = kIntNetAddrType_Invalid + 1; i < kIntNetAddrType_End && RT_SUCCESS(rc); i++)
            rc = intnetR0IfAddrCacheInit(&pNetwork->aAddrBlacklist[i], (INTNETADDRTYPE)i,
                                         !!(pNetwork->fFlags & INTNET_OPEN_FLAGS_SHARED_MAC_ON_WIRE));
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Register the object in the current session and link it into the network list.
         */
        pNetwork->pvObj = SUPR0ObjRegister(pSession, SUPDRVOBJTYPE_INTERNAL_NETWORK, intnetR0NetworkDestruct, pNetwork, pIntNet);
        if (pNetwork->pvObj)
        {
            pNetwork->pNext = pIntNet->pNetworks;
            pIntNet->pNetworks = pNetwork;

            /*
             * Check if the current session is actually allowed to create and
             * open the network.  It is possible to implement network name
             * based policies and these must be checked now.  SUPR0ObjRegister
             * does no such checks.
             */
            rc = SUPR0ObjVerifyAccess(pNetwork->pvObj, pSession, pNetwork->szName);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Connect the trunk.
                 */
                rc = intnetR0NetworkCreateTrunkIf(pNetwork, pSession);
                if (RT_SUCCESS(rc))
                {
                    *ppNetwork = pNetwork;
                    LogFlow(("intnetR0CreateNetwork: returns VINF_SUCCESS *ppNetwork=%p\n", pNetwork));
                    return VINF_SUCCESS;
                }
            }

            SUPR0ObjRelease(pNetwork->pvObj, pSession);
            LogFlow(("intnetR0CreateNetwork: returns %Rrc\n", rc));
            return rc;
        }

        /* cleanup */
        rc = VERR_NO_MEMORY;
    }

    RTSemEventDestroy(pNetwork->hEvtBusyIf);
    pNetwork->hEvtBusyIf = NIL_RTSEMEVENT;
    RTSpinlockDestroy(pNetwork->hAddrSpinlock);
    pNetwork->hAddrSpinlock = NIL_RTSPINLOCK;
    RTMemFree(pNetwork->MacTab.paEntries);
    pNetwork->MacTab.paEntries = NULL;
    RTMemFree(pNetwork);

    LogFlow(("intnetR0CreateNetwork: returns %Rrc\n", rc));
    return rc;
}


/**
 * Opens a network interface and connects it to the specified network.
 *
 * @returns VBox status code.
 * @param   pSession        The session handle.
 * @param   pszNetwork      The network name.
 * @param   enmTrunkType    The trunk type.
 * @param   pszTrunk        The trunk name. Its meaning is specific to the type.
 * @param   fFlags          Flags, see INTNET_OPEN_FLAGS_*.
 * @param   fRestrictAccess Whether new participants should be subjected to
 *                          access check or not.
 * @param   cbSend          The send buffer size.
 * @param   cbRecv          The receive buffer size.
 * @param   pfnRecvAvail    The receive available callback to call instead of
 *                          signalling the semaphore (R3 service only).
 * @param   pvUser          The opaque user data to pass to the callback.
 * @param   phIf            Where to store the handle to the network interface.
 */
INTNETR0DECL(int) IntNetR0Open(PSUPDRVSESSION pSession, const char *pszNetwork,
                               INTNETTRUNKTYPE enmTrunkType, const char *pszTrunk, uint32_t fFlags,
                               uint32_t cbSend, uint32_t cbRecv, PFNINTNETIFRECVAVAIL pfnRecvAvail, void *pvUser,
                               PINTNETIFHANDLE phIf)
{
    LogFlow(("IntNetR0Open: pSession=%p pszNetwork=%p:{%s} enmTrunkType=%d pszTrunk=%p:{%s} fFlags=%#x cbSend=%u cbRecv=%u phIf=%p\n",
             pSession, pszNetwork, pszNetwork, enmTrunkType, pszTrunk, pszTrunk, fFlags, cbSend, cbRecv, phIf));

    /*
     * Validate input.
     */
    PINTNET pIntNet = g_pIntNet;
    AssertPtrReturn(pIntNet, VERR_INVALID_PARAMETER);
    AssertReturn(pIntNet->u32Magic, VERR_INVALID_MAGIC);

    AssertPtrReturn(pszNetwork, VERR_INVALID_PARAMETER);
    const char *pszNetworkEnd = RTStrEnd(pszNetwork, INTNET_MAX_NETWORK_NAME);
    AssertReturn(pszNetworkEnd, VERR_INVALID_PARAMETER);
    size_t cchNetwork = pszNetworkEnd - pszNetwork;
    AssertReturn(cchNetwork, VERR_INVALID_PARAMETER);

    if (pszTrunk)
    {
        AssertPtrReturn(pszTrunk, VERR_INVALID_PARAMETER);
        const char *pszTrunkEnd = RTStrEnd(pszTrunk, INTNET_MAX_TRUNK_NAME);
        AssertReturn(pszTrunkEnd, VERR_INVALID_PARAMETER);
    }
    else
        pszTrunk = "";

    AssertMsgReturn(enmTrunkType > kIntNetTrunkType_Invalid && enmTrunkType < kIntNetTrunkType_End,
                    ("%d\n", enmTrunkType), VERR_INVALID_PARAMETER);
    switch (enmTrunkType)
    {
        case kIntNetTrunkType_None:
        case kIntNetTrunkType_WhateverNone:
#ifdef VBOX_WITH_NAT_SERVICE
        case kIntNetTrunkType_SrvNat:
#endif
            if (*pszTrunk)
                return VERR_INVALID_PARAMETER;
            break;

        case kIntNetTrunkType_NetFlt:
        case kIntNetTrunkType_NetAdp:
            if (!*pszTrunk)
                return VERR_INVALID_PARAMETER;
            break;

        default:
            return VERR_NOT_IMPLEMENTED;
    }

    AssertMsgReturn(!(fFlags & ~INTNET_OPEN_FLAGS_MASK), ("%#x\n", fFlags), VERR_INVALID_PARAMETER);
    for (uint32_t i = 0; i < RT_ELEMENTS(g_afIntNetOpenNetworkNetFlags); i++)
        AssertMsgReturn((fFlags & g_afIntNetOpenNetworkNetFlags[i].fPair) != g_afIntNetOpenNetworkNetFlags[i].fPair,
                        ("%#x (%#x)\n", fFlags, g_afIntNetOpenNetworkNetFlags[i].fPair), VERR_INVALID_PARAMETER);
    for (uint32_t i = 0; i < RT_ELEMENTS(g_afIntNetOpenNetworkIfFlags); i++)
        AssertMsgReturn((fFlags & g_afIntNetOpenNetworkIfFlags[i].fPair) != g_afIntNetOpenNetworkIfFlags[i].fPair,
                        ("%#x (%#x)\n", fFlags, g_afIntNetOpenNetworkIfFlags[i].fPair), VERR_INVALID_PARAMETER);
    AssertPtrReturn(phIf, VERR_INVALID_PARAMETER);

    /*
     * Acquire the mutex to serialize open/create/close.
     */
    int rc = RTSemMutexRequest(pIntNet->hMtxCreateOpenDestroy, RT_INDEFINITE_WAIT);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Try open / create the network and create an interface on it for the
     * caller to use.
     */
    PINTNETNETWORK pNetwork = NULL;
    rc = intnetR0OpenNetwork(pIntNet, pSession, pszNetwork, enmTrunkType, pszTrunk, fFlags, &pNetwork);
    if (RT_SUCCESS(rc))
    {
        rc = intnetR0NetworkCreateIf(pNetwork, pSession, cbSend, cbRecv, fFlags, pfnRecvAvail, pvUser, phIf);
        if (RT_SUCCESS(rc))
        {
            intnetR0AdaptOpenNetworkFlags(pNetwork, fFlags);
            rc = VINF_ALREADY_INITIALIZED;
        }
        else
            SUPR0ObjRelease(pNetwork->pvObj, pSession);
    }
    else if (rc == VERR_NOT_FOUND)
    {
        rc = intnetR0CreateNetwork(pIntNet, pSession, pszNetwork, enmTrunkType, pszTrunk, fFlags, &pNetwork);
        if (RT_SUCCESS(rc))
        {
            rc = intnetR0NetworkCreateIf(pNetwork, pSession, cbSend, cbRecv, fFlags, pfnRecvAvail, pvUser, phIf);
            if (RT_FAILURE(rc))
                SUPR0ObjRelease(pNetwork->pvObj, pSession);
        }
    }

    RTSemMutexRelease(pIntNet->hMtxCreateOpenDestroy);
    LogFlow(("IntNetR0Open: return %Rrc *phIf=%RX32\n", rc, *phIf));
    return rc;
}


/**
 * VMMR0 request wrapper for IntNetR0Open.
 *
 * @returns see GMMR0MapUnmapChunk.
 * @param   pSession        The caller's session.
 * @param   pReq            The request packet.
 */
INTNETR0DECL(int) IntNetR0OpenReq(PSUPDRVSESSION pSession, PINTNETOPENREQ pReq)
{
    if (RT_UNLIKELY(pReq->Hdr.cbReq != sizeof(*pReq)))
        return VERR_INVALID_PARAMETER;
    return IntNetR0Open(pSession, &pReq->szNetwork[0], pReq->enmTrunkType, pReq->szTrunk,
                        pReq->fFlags, pReq->cbSend, pReq->cbRecv, NULL /*pfnRecvAvail*/, NULL /*pvUser*/, &pReq->hIf);
}


#if defined(VBOX_WITH_INTNET_SERVICE_IN_R3) && defined(IN_RING3)
INTNETR3DECL(int) IntNetR3Open(PSUPDRVSESSION pSession, const char *pszNetwork,
                               INTNETTRUNKTYPE enmTrunkType, const char *pszTrunk, uint32_t fFlags,
                               uint32_t cbSend, uint32_t cbRecv, PFNINTNETIFRECVAVAIL pfnRecvAvail, void *pvUser,
                               PINTNETIFHANDLE phIf)
{
    return IntNetR0Open(pSession, pszNetwork, enmTrunkType, pszTrunk, fFlags, cbSend, cbRecv, pfnRecvAvail, pvUser, phIf);
}
#endif


/**
 * Count the internal networks.
 *
 * This is mainly for providing the testcase with some introspection to validate
 * behavior when closing interfaces.
 *
 * @returns The number of networks.
 */
INTNETR0DECL(uint32_t) IntNetR0GetNetworkCount(void)
{
    /*
     * Grab the instance.
     */
    PINTNET pIntNet = g_pIntNet;
    if (!pIntNet)
        return 0;
    AssertPtrReturn(pIntNet, 0);
    AssertReturn(pIntNet->u32Magic == INTNET_MAGIC, 0);

    /*
     * Grab the mutex and count the networks.
     */
    int rc = RTSemMutexRequest(pIntNet->hMtxCreateOpenDestroy, RT_INDEFINITE_WAIT);
    if (RT_FAILURE(rc))
        return 0;

    uint32_t cNetworks = 0;
    for (PINTNETNETWORK pCur = pIntNet->pNetworks; pCur; pCur = pCur->pNext)
        cNetworks++;

    RTSemMutexRelease(pIntNet->hMtxCreateOpenDestroy);

    return cNetworks;
}



/**
 * Destroys an instance of the Ring-0 internal networking service.
 */
INTNETR0DECL(void) IntNetR0Term(void)
{
    LogFlow(("IntNetR0Term:\n"));

    /*
     * Zap the global pointer and validate it.
     */
    PINTNET pIntNet = g_pIntNet;
    g_pIntNet = NULL;
    if (!pIntNet)
        return;
    AssertPtrReturnVoid(pIntNet);
    AssertReturnVoid(pIntNet->u32Magic == INTNET_MAGIC);

    /*
     * There is not supposed to be any networks hanging around at this time.
     */
    AssertReturnVoid(ASMAtomicCmpXchgU32(&pIntNet->u32Magic, ~INTNET_MAGIC, INTNET_MAGIC));
    Assert(pIntNet->pNetworks == NULL);
    /*
     * @todo Do we really need to be paranoid enough to go over the list of networks here,
     * trying to terminate trunk re-connection threads here?
     */
    if (pIntNet->hMtxCreateOpenDestroy != NIL_RTSEMMUTEX)
    {
        RTSemMutexDestroy(pIntNet->hMtxCreateOpenDestroy);
        pIntNet->hMtxCreateOpenDestroy = NIL_RTSEMMUTEX;
    }
    if (pIntNet->hHtIfs != NIL_RTHANDLETABLE)
    {
        /** @todo does it make sense to have a deleter here? */
        RTHandleTableDestroy(pIntNet->hHtIfs, NULL, NULL);
        pIntNet->hHtIfs = NIL_RTHANDLETABLE;
    }

    RTMemFree(pIntNet);
}


/**
 * Initializes the internal network ring-0 service.
 *
 * @returns VBox status code.
 */
INTNETR0DECL(int) IntNetR0Init(void)
{
    LogFlow(("IntNetR0Init:\n"));
    int rc = VERR_NO_MEMORY;
    PINTNET pIntNet = (PINTNET)RTMemAllocZ(sizeof(*pIntNet));
    if (pIntNet)
    {
        //pIntNet->pNetworks = NULL;

        rc = RTSemMutexCreate(&pIntNet->hMtxCreateOpenDestroy);
        if (RT_SUCCESS(rc))
        {
            rc = RTHandleTableCreateEx(&pIntNet->hHtIfs, RTHANDLETABLE_FLAGS_LOCKED | RTHANDLETABLE_FLAGS_CONTEXT,
                                       UINT32_C(0x8ffe0000), 4096, intnetR0IfRetainHandle, NULL);
            if (RT_SUCCESS(rc))
            {
                pIntNet->u32Magic = INTNET_MAGIC;
                g_pIntNet = pIntNet;
                LogFlow(("IntNetR0Init: returns VINF_SUCCESS pIntNet=%p\n", pIntNet));
                return VINF_SUCCESS;
            }

            RTSemMutexDestroy(pIntNet->hMtxCreateOpenDestroy);
        }
        RTMemFree(pIntNet);
    }
    LogFlow(("IntNetR0Init: returns %Rrc\n", rc));
    return rc;
}

