/* $Id: VBoxNetFltCmn-win.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxNetFltCmn-win.h - Bridged Networking Driver, Windows Specific Code.
 * Common header with configuration defines and global defs
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_VBoxNetFlt_win_drv_VBoxNetFltCmn_win_h
#define VBOX_INCLUDED_SRC_VBoxNetFlt_win_drv_VBoxNetFltCmn_win_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#define LOG_GROUP LOG_GROUP_NET_FLT_DRV

/* debugging flags */
#ifdef DEBUG
//# define DEBUG_NETFLT_PACKETS
# ifndef DEBUG_misha
#  define RT_NO_STRICT
# endif
/* # define DEBUG_NETFLT_LOOPBACK */
/* receive logic has several branches */
/* the DEBUG_NETFLT_RECV* macros used to debug the ProtocolReceive callback
 * which is typically not used in case the underlying miniport indicates the packets with NdisMIndicateReceivePacket
 * the best way to debug the ProtocolReceive (which in turn has several branches) is to enable the DEBUG_NETFLT_RECV
 * one by one in the below order, i.e.
 * first DEBUG_NETFLT_RECV
 * then DEBUG_NETFLT_RECV + DEBUG_NETFLT_RECV_NOPACKET */
//# define DEBUG_NETFLT_RECV
//# define DEBUG_NETFLT_RECV_NOPACKET
//# define DEBUG_NETFLT_RECV_TRANSFERDATA
/* use ExAllocatePoolWithTag instead of NdisAllocateMemoryWithTag */
// #define DEBUG_NETFLT_USE_EXALLOC
#endif

#include <VBox/intnet.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/version.h>
#include <iprt/initterm.h>
#include <iprt/assert.h>
#include <iprt/spinlock.h>
#include <iprt/semaphore.h>
#include <iprt/process.h>
#include <iprt/alloc.h>
#include <iprt/alloca.h>
#include <iprt/time.h>
#include <iprt/net.h>
#include <iprt/list.h>

#include <iprt/nt/ntddk.h>
#include <iprt/nt/ndis.h>

#define VBOXNETFLT_OS_SPECFIC 1

/** version
 * NOTE: we are NOT using NDIS 5.1 features now */
#ifdef NDIS51_MINIPORT
# define VBOXNETFLT_VERSION_MP_NDIS_MAJOR     5
# define VBOXNETFLT_VERSION_MP_NDIS_MINOR     1
#else
# define VBOXNETFLT_VERSION_MP_NDIS_MAJOR     5
# define VBOXNETFLT_VERSION_MP_NDIS_MINOR     0
#endif

#ifndef VBOXNETADP
#ifdef NDIS51
# define VBOXNETFLT_VERSION_PT_NDIS_MAJOR     5
# define VBOXNETFLT_VERSION_PT_NDIS_MINOR     1 /* todo: use 0 here as well ? */
#else
# define VBOXNETFLT_VERSION_PT_NDIS_MAJOR     5
# define VBOXNETFLT_VERSION_PT_NDIS_MINOR     0
#endif

# define VBOXNETFLT_NAME_PROTOCOL             L"VBoxNetFlt"
/** device to be used to prevent the driver unload & ioctl interface (if necessary in the future) */
# define VBOXNETFLT_NAME_LINK                 L"\\DosDevices\\Global\\VBoxNetFlt"
# define VBOXNETFLT_NAME_DEVICE               L"\\Device\\VBoxNetFlt"
#else
# define VBOXNETFLT_NAME_LINK                 L"\\DosDevices\\Global\\VBoxNetAdp"
# define VBOXNETFLT_NAME_DEVICE               L"\\Device\\VBoxNetAdp"
#endif

typedef struct VBOXNETFLTINS *PVBOXNETFLTINS;

/** configuration */

/** Ndis Packet pool settings
 * these are applied to both receive and send packet pools */
/* number of packets for normal used */
#define VBOXNETFLT_PACKET_POOL_SIZE_NORMAL    0x000000FF
/* number of additional overflow packets */
#define VBOXNETFLT_PACKET_POOL_SIZE_OVERFLOW  0x0000FF00

/** packet queue size used when the driver is working in the "active" mode */
#define VBOXNETFLT_PACKET_INFO_POOL_SIZE      0x0000FFFF

#ifndef VBOXNETADP
/** memory tag used for memory allocations
 * (VBNF stands for VBox NetFlt) */
# define VBOXNETFLT_MEM_TAG                   'FNBV'
#else
/** memory tag used for memory allocations
 * (VBNA stands for VBox NetAdp) */
# define VBOXNETFLT_MEM_TAG                   'ANBV'
#endif

/** receive and transmit Ndis buffer pool size */
#define VBOXNETFLT_BUFFER_POOL_SIZE_TX        128
#define VBOXNETFLT_BUFFER_POOL_SIZE_RX        128

#define VBOXNETFLT_PACKET_ETHEADER_SIZE       14
#define VBOXNETFLT_PACKET_HEADER_MATCH_SIZE   24
#define VBOXNETFLT_PACKET_QUEUE_SG_SEGS_ALLOC 32


#if defined(DEBUG_NETFLT_PACKETS) || !defined(VBOX_LOOPBACK_USEFLAGS)
# define VBOXNETFLT_PACKETMATCH_LENGTH        (VBOXNETFLT_PACKET_ETHEADER_SIZE + 2)
#endif

#ifdef VBOXNETADP
#define VBOXNETADP_HEADER_SIZE                14
#define VBOXNETADP_MAX_DATA_SIZE              1500
#define VBOXNETADP_MAX_PACKET_SIZE            (VBOXNETADP_HEADER_SIZE + VBOXNETADP_MAX_DATA_SIZE)
#define VBOXNETADP_MIN_PACKET_SIZE            60
/* link speed 100Mbps (measured in 100 bps) */
#define     VBOXNETADP_LINK_SPEED             1000000
#define VBOXNETADP_MAX_LOOKAHEAD_SIZE         VBOXNETADP_MAX_DATA_SIZE
#define VBOXNETADP_VENDOR_ID 0x080027
#define VBOXNETADP_VENDOR_DRIVER_VERSION      0x00010000
#define VBOXNETADP_VENDOR_DESC                "Sun"
#define VBOXNETADP_MAX_MCAST_LIST             32
#define VBOXNETADP_ETH_ADDRESS_LENGTH         6

//#define VBOXNETADP_REPORT_DISCONNECTED
#endif
/* type defs */

/** Flag specifying that the type of enqueued packet
 * if set the info contains the PINTNETSG packet
 * if clear the packet info contains the PNDIS_PACKET packet
 * Typically the packet queue we are maintaining contains PNDIS_PACKETs only,
 * however in case the underlying miniport indicates a packet with the NDIS_STATUS_RESOURCES status
 * we MUST return the packet back to the miniport immediately
 * this is why we are creating the INTNETSG, copying the ndis packet info there and enqueueing it */
#define VBOXNETFLT_PACKET_SG                  0x00000001

/** the flag specifying that the packet source
 * if set the packet comes from the host (upperlying protocol)
 * if clear the packet comes from the wire (underlying miniport) */
#define VBOXNETFLT_PACKET_SRC_HOST            0x00000002

#ifndef VBOXNETFLT_NO_PACKET_QUEUE
/** flag specifying the packet was originated by our driver
 * i.e. we could use it on our needs and should not return it
 * we are enqueueing "our" packets on ProtocolReceive call-back when
 * Ndis does not give us a receive packet (the driver below us has called NdisM..IndicateReceive)
 * this is supported for Ndis Packet only */
#define VBOXNETFLT_PACKET_MINE                0x00000004

/** flag passed to vboxNetFltWinQuEnqueuePacket specifying that the packet should be copied
 * this is supported for Ndis Packet only */
#define VBOXNETFLT_PACKET_COPY                0x00000008
#endif

/** packet queue element containing the packet info */
typedef struct VBOXNETFLT_PACKET_INFO
{
    /** list entry used for enqueueing the info */
    LIST_ENTRY ListEntry;
    /** pointer to the pool containing this packet info */
    struct VBOXNETFLT_PACKET_INFO_POOL *pPool;
    /** flags describing the referenced packet. Contains PACKET_xxx flags (i.e. PACKET_SG, PACKET_SRC_HOST) */
    uint32_t fFlags;
    /** pointer to the packet this info represents */
    PVOID pPacket;
} VBOXNETFLT_PACKET_INFO, *PVBOXNETFLT_PACKET_INFO;

/* paranoid check to make sure the elements in the packet info array are properly aligned */
AssertCompile((sizeof(VBOXNETFLT_PACKET_INFO) & (sizeof(PVOID) - 1)) == 0);

/** represents the packet queue */
typedef LIST_ENTRY PVBOXNETFLT_ACKET_QUEUE, *PVBOXNETFLT_PACKET_QUEUE;

/*
 * we are using non-interlocked versions of LIST_ENTRY-related operations macros and synchronize
 * access to the queue and its elements by acquiring/releasing a spinlock using Ndis[Acquire,Release]Spinlock
 *
 * we are NOT using interlocked versions of insert/remove head/tail list functions because we need to iterate though
 * the queue elements as well as remove elements from the midle of the queue
 *
 * * @todo: it seems that we can switch to using interlocked versions of list-entry functions
 * since we have removed all functionality (mentioned above, i.e. queue elements iteration, etc.) that might prevent us from doing this
 */
typedef struct VBOXNETFLT_INTERLOCKED_PACKET_QUEUE
{
    /** queue */
    PVBOXNETFLT_ACKET_QUEUE Queue;
    /** queue lock */
    NDIS_SPIN_LOCK Lock;
} VBOXNETFLT_INTERLOCKED_PACKET_QUEUE, *PVBOXNETFLT_INTERLOCKED_PACKET_QUEUE;

typedef struct VBOXNETFLT_SINGLE_LIST
{
    /** queue */
    SINGLE_LIST_ENTRY Head;
    /** pointer to the list tail. used to enqueue elements to the tail of the list */
    PSINGLE_LIST_ENTRY pTail;
} VBOXNETFLT_SINGLE_LIST, *PVBOXNETFLT_SINGLE_LIST;

typedef struct VBOXNETFLT_INTERLOCKED_SINGLE_LIST
{
    /** queue */
    VBOXNETFLT_SINGLE_LIST List;
    /** queue lock */
    NDIS_SPIN_LOCK Lock;
} VBOXNETFLT_INTERLOCKED_SINGLE_LIST, *PVBOXNETFLT_INTERLOCKED_SINGLE_LIST;

/** packet info pool contains free packet info elements to be used for the packet queue
 * we are using the pool mechanism to allocate packet queue elements
 * the pool mechanism is pretty simple now, we are allocating a bunch of memory
 * for maintaining VBOXNETFLT_PACKET_INFO_POOL_SIZE queue elements and just returning null when the pool is exhausted
 * This mechanism seems to be enough for now since we are using VBOXNETFLT_PACKET_INFO_POOL_SIZE = 0xffff which is
 * the maximum size of packets the ndis packet pool supports */
typedef struct VBOXNETFLT_PACKET_INFO_POOL
{
    /** free packet info queue */
    VBOXNETFLT_INTERLOCKED_PACKET_QUEUE Queue;
    /** memory bugger used by the pool */
    PVOID pBuffer;
} VBOXNETFLT_PACKET_INFO_POOL, *PVBOXNETFLT_PACKET_INFO_POOL;

typedef enum VBOXNETDEVOPSTATE
{
    kVBoxNetDevOpState_InvalidValue = 0,
    kVBoxNetDevOpState_Initializing,
    kVBoxNetDevOpState_Initialized,
    kVBoxNetDevOpState_Deinitializing,
    kVBoxNetDevOpState_Deinitialized,

} VBOXNETDEVOPSTATE;

typedef enum VBOXNETFLT_WINIFSTATE
{
   /** The usual invalid state. */
    kVBoxWinIfState_Invalid = 0,
    /** Initialization. */
    kVBoxWinIfState_Connecting,
    /** Connected fuly functional state */
    kVBoxWinIfState_Connected,
    /** Disconnecting  */
    kVBoxWinIfState_Disconnecting,
    /** Disconnected  */
    kVBoxWinIfState_Disconnected,
} VBOXNETFLT_WINIFSTATE;

/** structure used to maintain the state and reference count of the miniport and protocol */
typedef struct VBOXNETFLT_WINIF_DEVICE
{
    /** initialize state */
    VBOXNETDEVOPSTATE OpState;
    /** ndis power state */
    NDIS_DEVICE_POWER_STATE PowerState;
    /** reference count */
    uint32_t cReferences;
} VBOXNETFLT_WINIF_DEVICE, *PVBOXNETFLT_WINIF_DEVICE;

#define VBOXNDISREQUEST_INPROGRESS  1
#define VBOXNDISREQUEST_QUEUED      2

typedef struct VBOXNETFLTWIN_STATE
{
    union
    {
        struct
        {
            UINT fRequestInfo : 2;
            UINT fInterfaceClosing : 1;
            UINT fStandBy : 1;
            UINT fProcessingPacketFilter : 1;
            UINT fPPFNetFlt : 1;
            UINT fUpperProtSetFilterInitialized : 1;
            UINT Reserved : 25;
        };
        UINT Value;
    };
} VBOXNETFLTWIN_STATE, *PVBOXNETFLTWIN_STATE;

DECLINLINE(VBOXNETFLTWIN_STATE) vboxNetFltWinAtomicUoReadWinState(VBOXNETFLTWIN_STATE State)
{
    UINT fValue = ASMAtomicUoReadU32((volatile uint32_t *)&State.Value);
    return *((PVBOXNETFLTWIN_STATE)((void*)&fValue));
}

/* miniport layer globals */
typedef struct VBOXNETFLTGLOBALS_MP
{
    /** our miniport handle */
    NDIS_HANDLE hMiniport;
    /** ddis wrapper handle */
    NDIS_HANDLE hNdisWrapper;
} VBOXNETFLTGLOBALS_MP, *PVBOXNETFLTGLOBALS_MP;

#ifndef VBOXNETADP
/* protocol layer globals */
typedef struct VBOXNETFLTGLOBALS_PT
{
    /** our protocol handle */
    NDIS_HANDLE hProtocol;
} VBOXNETFLTGLOBALS_PT, *PVBOXNETFLTGLOBALS_PT;
#endif /* #ifndef VBOXNETADP */

typedef struct VBOXNETFLTGLOBALS_WIN
{
    /** synch event used for device creation synchronization */
    KEVENT SynchEvent;
    /** Device reference count */
    int cDeviceRefs;
    /** ndis device */
    NDIS_HANDLE hDevice;
    /** device object */
    PDEVICE_OBJECT pDevObj;
    /* loopback flags */
    /* ndis packet flags to disable packet loopback */
    UINT fPacketDontLoopBack;
    /* ndis packet flags specifying whether the packet is looped back */
    UINT fPacketIsLoopedBack;
    /* Minport info */
    VBOXNETFLTGLOBALS_MP Mp;
#ifndef VBOXNETADP
    /* Protocol info */
    VBOXNETFLTGLOBALS_PT Pt;
    /** lock protecting the filter list */
    NDIS_SPIN_LOCK lockFilters;
    /** the head of filter list */
    RTLISTANCHOR listFilters;
    /** IP address change notifier handle */
    HANDLE hNotifier;
#endif
} VBOXNETFLTGLOBALS_WIN, *PVBOXNETFLTGLOBALS_WIN;

extern VBOXNETFLTGLOBALS_WIN g_VBoxNetFltGlobalsWin;

/** represents filter driver device context*/
typedef struct VBOXNETFLTWIN
{
    /** handle used by miniport edge for ndis calls */
    NDIS_HANDLE hMiniport;
    /** miniport edge state */
    VBOXNETFLT_WINIF_DEVICE MpState;
    /** ndis packet pool used for receives */
    NDIS_HANDLE hRecvPacketPool;
    /** ndis buffer pool used for receives */
    NDIS_HANDLE hRecvBufferPool;
    /** driver bind adapter state. */
    VBOXNETFLT_WINIFSTATE enmState;
#ifndef VBOXNETADP
    /* misc state flags */
    VBOXNETFLTWIN_STATE StateFlags;
    /** handle used by protocol edge for ndis calls */
    NDIS_HANDLE hBinding;
    /** protocol edge state */
    VBOXNETFLT_WINIF_DEVICE PtState;
    /** ndis packet pool used for receives */
    NDIS_HANDLE hSendPacketPool;
    /** ndis buffer pool used for receives */
    NDIS_HANDLE hSendBufferPool;
    /** used for maintaining the pending send packets for handling packet loopback */
    VBOXNETFLT_INTERLOCKED_SINGLE_LIST SendPacketQueue;
    /** used for serializing calls to the NdisRequest in the vboxNetFltWinSynchNdisRequest */
    RTSEMFASTMUTEX hSynchRequestMutex;
    /** event used to synchronize with the Ndis Request completion in the vboxNetFltWinSynchNdisRequest */
    KEVENT hSynchCompletionEvent;
    /** status of the Ndis Request initiated by the vboxNetFltWinSynchNdisRequest */
    NDIS_STATUS volatile SynchCompletionStatus;
    /** pointer to the Ndis Request being executed by the vboxNetFltWinSynchNdisRequest */
    PNDIS_REQUEST volatile pSynchRequest;
    /** open/close adapter status.
     * Since ndis adapter open and close requests may complete asynchronously,
     * we are using event mechanism to wait for open/close completion
     * the status field is being set by the completion call-back */
    NDIS_STATUS OpenCloseStatus;
    /** open/close adaptor completion event */
    NDIS_EVENT OpenCloseEvent;
    /** medium we are attached to */
    NDIS_MEDIUM enmMedium;
    /**
     * Passdown request info
     */
    /** ndis request we pass down to the miniport below */
    NDIS_REQUEST PassDownRequest;
    /** Ndis pass down request bytes read or written original pointer */
    PULONG pcPDRBytesRW;
    /** Ndis pass down request bytes needed original pointer */
    PULONG pcPDRBytesNeeded;
    /** true if we should indicate the receive complete used by the ProtocolReceive mechanism.
     * We need to indicate it only with the ProtocolReceive + NdisMEthIndicateReceive path.
     * Note: we're using KeGetCurrentProcessorNumber, which is not entirely correct in case
     * we're running on 64bit win7+, which can handle > 64 CPUs, however since KeGetCurrentProcessorNumber
     * always returns the number < than the number of CPUs in the first group, we're guaranteed to have CPU index < 64
     * @todo: use KeGetCurrentProcessorNumberEx for Win7+ 64 and dynamically extended array */
    bool abIndicateRxComplete[64];
    /** Pending transfer data packet queue (i.e. packets that were indicated as pending on NdisTransferData call */
    VBOXNETFLT_INTERLOCKED_SINGLE_LIST TransferDataList;
    /* mac options initialized on OID_GEN_MAC_OPTIONS */
    ULONG fMacOptions;
    /** our miniport devuice name */
    NDIS_STRING MpDeviceName;
    /** synchronize with unbind with Miniport initialization */
    NDIS_EVENT MpInitCompleteEvent;
    /** media connect status that we indicated */
    NDIS_STATUS MpIndicatedMediaStatus;
    /** media connect status pending to indicate */
    NDIS_STATUS MpUnindicatedMediaStatus;
    /** packet filter flags set by the upper protocols */
    ULONG fUpperProtocolSetFilter;
    /** packet filter flags set by the upper protocols */
    ULONG fSetFilterBuffer;
    /** packet filter flags set by us */
    ULONG fOurSetFilter;
    /** our own list of filters, needed by notifier */
    RTLISTNODE node;
#else
    volatile ULONG cTxSuccess;
    volatile ULONG cRxSuccess;
    volatile ULONG cTxError;
    volatile ULONG cRxError;
#endif
} VBOXNETFLTWIN, *PVBOXNETFLTWIN;

typedef struct VBOXNETFLT_PACKET_QUEUE_WORKER
{
    /** this event is used to initiate a packet queue worker thread kill */
    KEVENT KillEvent;
    /** this event is used to notify a worker thread that the packets are added to the queue */
    KEVENT NotifyEvent;
    /** pointer to the packet queue worker thread object */
    PKTHREAD pThread;
    /** pointer to the SG used by the packet queue for IntNet receive notifications */
    PINTNETSG pSG;
    /** Packet queue */
    VBOXNETFLT_INTERLOCKED_PACKET_QUEUE PacketQueue;
    /** Packet info pool, i.e. the pool for the packet queue elements */
    VBOXNETFLT_PACKET_INFO_POOL PacketInfoPool;
} VBOXNETFLT_PACKET_QUEUE_WORKER, *PVBOXNETFLT_PACKET_QUEUE_WORKER;

/* protocol reserved data held in ndis packet */
typedef struct VBOXNETFLT_PKTRSVD_PT
{
    /** original packet received from the upperlying protocol
     * can be null if the packet was originated by intnet */
    PNDIS_PACKET pOrigPacket;
    /** pointer to the buffer to be freed on send completion
     * can be null if no buffer is to be freed */
    PVOID pBufToFree;
#if !defined(VBOX_LOOPBACK_USEFLAGS) || defined(DEBUG_NETFLT_PACKETS)
    SINGLE_LIST_ENTRY ListEntry;
    /* true if the packet is from IntNet */
    bool bFromIntNet;
#endif
} VBOXNETFLT_PKTRSVD_PT, *PVBOXNETFLT_PKTRSVD_PT;

/** miniport reserved data held in ndis packet */
typedef struct VBOXNETFLT_PKTRSVD_MP
{
    /** original packet received from the underling miniport
     * can be null if the packet was originated by intnet */
    PNDIS_PACKET pOrigPacket;
    /** pointer to the buffer to be freed on receive completion
     * can be null if no buffer is to be freed */
    PVOID pBufToFree;
} VBOXNETFLT_PKTRSVD_MP, *PVBOXNETFLT_PKTRSVD_MP;

/** represents the data stored in the protocol reserved field of ndis packet on NdisTransferData processing */
typedef struct VBOXNETFLT_PKTRSVD_TRANSFERDATA_PT
{
    /** next packet in a list */
    SINGLE_LIST_ENTRY ListEntry;
    /* packet buffer start */
    PNDIS_BUFFER pOrigBuffer;
} VBOXNETFLT_PKTRSVD_TRANSFERDATA_PT, *PVBOXNETFLT_PKTRSVD_TRANSFERDATA_PT;

/* VBOXNETFLT_PKTRSVD_TRANSFERDATA_PT should fit into PROTOCOL_RESERVED_SIZE_IN_PACKET because we use protocol reserved part
 * of our miniport edge on transfer data processing for honding our own info */
AssertCompile(sizeof (VBOXNETFLT_PKTRSVD_TRANSFERDATA_PT) <= PROTOCOL_RESERVED_SIZE_IN_PACKET);
/* this should fit in MiniportReserved */
AssertCompile(sizeof (VBOXNETFLT_PKTRSVD_MP) <= RT_SIZEOFMEMB(NDIS_PACKET, MiniportReserved));
/* we use RTAsmAtomic*U32 for those, make sure we're correct */
AssertCompile(sizeof (NDIS_DEVICE_POWER_STATE) == sizeof (uint32_t));
AssertCompile(sizeof (UINT) == sizeof (uint32_t));


#define NDIS_FLAGS_SKIP_LOOPBACK_W2K    0x400

#include "../../VBoxNetFltInternal.h"
#include "VBoxNetFltRt-win.h"
#ifndef VBOXNETADP
# include "VBoxNetFltP-win.h"
#endif
#include "VBoxNetFltM-win.h"

#endif /* !VBOX_INCLUDED_SRC_VBoxNetFlt_win_drv_VBoxNetFltCmn_win_h */
