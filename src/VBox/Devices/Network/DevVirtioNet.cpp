/* $Id: DevVirtioNet.cpp 169002 2025-05-26 19:54:46Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox storage devices - Virtio NET Driver
 *
 * Log-levels used:
 *    - Level 1:   The most important (but usually rare) things to note
 *    - Level 2:   NET command logging
 *    - Level 3:   Vector and I/O transfer summary (shows what client sent an expects and fulfillment)
 *    - Level 6:   Device <-> Guest Driver negotation, traffic, notifications and state handling
 *    - Level 12:  Brief formatted hex dumps of I/O data
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

/*******************************************************************************************************************************
*   Header Files                                                                                                               *
***************************************************************************************************************************** **/
#define LOG_GROUP LOG_GROUP_DEV_VIRTIO
#define VIRTIONET_WITH_GSO

#include <iprt/types.h>
#include <iprt/errcore.h>
#include <iprt/assert.h>
#include <iprt/string.h>

#include <VBox/sup.h>
#include <VBox/vmm/pdmdev.h>
#include <VBox/vmm/stam.h>
#include <VBox/vmm/pdmcritsect.h>
#include <VBox/vmm/pdmnetifs.h>
#include <VBox/msi.h>
#include <VBox/version.h>
#include <VBox/log.h>
#include <VBox/pci.h>


#ifdef IN_RING3
# include <VBox/VBoxPktDmp.h>
# include <iprt/alloc.h>
# include <iprt/memcache.h>
# include <iprt/semaphore.h>
# include <iprt/sg.h>
# include <iprt/param.h>
# include <iprt/uuid.h>
#endif
#include "../VirtIO/VirtioCore.h"

#include "VBoxDD.h"

#define VIRTIONET_TRANSITIONAL_ENABLE_FLAG             1       /** < If set behave as VirtIO "transitional" device  */

/** The current saved state version for the virtio core. */
#define VIRTIONET_SAVEDSTATE_VERSION                   UINT32_C(1)
#define VIRTIONET_SAVEDSTATE_VERSION_3_1_BETA1_LEGACY  UINT32_C(1) /**< Grandfathered in from DevVirtioNet.cpp      */
#define VIRTIONET_SAVEDSTATE_VERSION_LEGACY            UINT32_C(2) /**< Grandfathered in from DevVirtioNet.cpp      */
#define VIRTIONET_VERSION_MARKER_MAC_ADDR { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff } /**  SSM handling                  */

/*
 * Glossary of networking acronyms used in feature names below:
 *
 * GSO = Generic Segmentation Offload
 * TSO = TCP Segmentation Offload
 * UFO = UDP Fragmentation Offload
 * ECN = Explicit Congestion Notification
 */

/** @name VirtIO 1.0 NET Host feature bits (See VirtIO 1.0 specification, Section 5.6.3)
 * @{  */
#define VIRTIONET_F_CSUM                 RT_BIT_64(0)          /**< Handle packets with partial checksum            */
#define VIRTIONET_F_GUEST_CSUM           RT_BIT_64(1)          /**< Handles packets with partial checksum           */
#define VIRTIONET_F_CTRL_GUEST_OFFLOADS  RT_BIT_64(2)          /**< Control channel offloads reconfig support       */
#define VIRTIONET_F_MAC                  RT_BIT_64(5)          /**< Device has given MAC address                    */
#define VIRTIONET_F_GUEST_TSO4           RT_BIT_64(7)          /**< Driver can receive TSOv4                        */
#define VIRTIONET_F_GUEST_TSO6           RT_BIT_64(8)          /**< Driver can receive TSOv6                        */
#define VIRTIONET_F_GUEST_ECN            RT_BIT_64(9)          /**< Driver can receive TSO with ECN                 */
#define VIRTIONET_F_GUEST_UFO            RT_BIT_64(10)         /**< Driver can receive UFO                          */
#define VIRTIONET_F_HOST_TSO4            RT_BIT_64(11)         /**< Device can receive TSOv4                        */
#define VIRTIONET_F_HOST_TSO6            RT_BIT_64(12)         /**< Device can receive TSOv6                        */
#define VIRTIONET_F_HOST_ECN             RT_BIT_64(13)         /**< Device can receive TSO with ECN                 */
#define VIRTIONET_F_HOST_UFO             RT_BIT_64(14)         /**< Device can receive UFO                          */
#define VIRTIONET_F_MRG_RXBUF            RT_BIT_64(15)         /**< Driver can merge receive buffers                */
#define VIRTIONET_F_STATUS               RT_BIT_64(16)         /**< Config status field is available                */
#define VIRTIONET_F_CTRL_VQ              RT_BIT_64(17)         /**< Control channel is available                    */
#define VIRTIONET_F_CTRL_RX              RT_BIT_64(18)         /**< Control channel RX mode + MAC addr filtering    */
#define VIRTIONET_F_CTRL_VLAN            RT_BIT_64(19)         /**< Control channel VLAN filtering                  */
#define VIRTIONET_F_CTRL_RX_EXTRA        RT_BIT_64(20)         /**< Control channel RX mode extra functions         */
#define VIRTIONET_F_GUEST_ANNOUNCE       RT_BIT_64(21)         /**< Driver can send gratuitous packets              */
#define VIRTIONET_F_MQ                   RT_BIT_64(22)         /**< Support ultiqueue with auto receive steering    */
#define VIRTIONET_F_CTRL_MAC_ADDR        RT_BIT_64(23)         /**< Set MAC address through control channel         */
/** @} */

#ifdef IN_RING3
static const VIRTIO_FEATURES_LIST s_aDevSpecificFeatures[] =
{
    { VIRTIONET_F_STATUS,              "   STATUS               Configuration status field is available.\n" },
    { VIRTIONET_F_MAC,                 "   MAC                  Host has given MAC address.\n" },
    { VIRTIONET_F_CTRL_VQ,             "   CTRL_VQ              Control channel is available.\n" },
    { VIRTIONET_F_CTRL_MAC_ADDR,       "   CTRL_MAC_ADDR        Set MAC address through control channel.\n" },
    { VIRTIONET_F_CTRL_RX,             "   CTRL_RX              Control channel RX mode support.\n" },
    { VIRTIONET_F_CTRL_VLAN,           "   CTRL_VLAN            Control channel VLAN filtering.\n" },
    { VIRTIONET_F_CTRL_GUEST_OFFLOADS, "   CTRL_GUEST_OFFLOADS  Control channel offloads reconfiguration support.\n" },
    { VIRTIONET_F_GUEST_CSUM,          "   GUEST_CSUM           Guest handles packets with partial checksum.\n" },
    { VIRTIONET_F_GUEST_ANNOUNCE,      "   GUEST_ANNOUNCE       Guest can send gratuitous packets.\n" },
    { VIRTIONET_F_GUEST_TSO4,          "   GUEST_TSO4           Guest can receive TSOv4.\n" },
    { VIRTIONET_F_GUEST_TSO6,          "   GUEST_TSO6           Guest can receive TSOv6.\n" },
    { VIRTIONET_F_GUEST_ECN,           "   GUEST_ECN            Guest can receive TSO with ECN.\n" },
    { VIRTIONET_F_GUEST_UFO,           "   GUEST_UFO            Guest can receive UFO.\n" },
    { VIRTIONET_F_HOST_TSO4,           "   HOST_TSO4            Host can receive TSOv4.\n" },
    { VIRTIONET_F_HOST_TSO6,           "   HOST_TSO6            Host can receive TSOv6.\n" },
    { VIRTIONET_F_HOST_ECN,            "   HOST_ECN             Host can receive TSO with ECN.\n" },
    { VIRTIONET_F_HOST_UFO,            "   HOST_UFO             Host can receive UFO.\n" },
    { VIRTIONET_F_MQ,                  "   MQ                   Host supports multiqueue with automatic receive steering.\n" },
    { VIRTIONET_F_CSUM,                "   CSUM                 Host handles packets with partial checksum.\n" },
    { VIRTIONET_F_MRG_RXBUF,           "   MRG_RXBUF            Guest can merge receive buffers.\n" },
};
#endif

#ifdef VIRTIONET_WITH_GSO
# define VIRTIONET_HOST_FEATURES_GSO    \
    ( VIRTIONET_F_CSUM                  \
    | VIRTIONET_F_HOST_TSO4             \
    | VIRTIONET_F_HOST_TSO6             \
    | VIRTIONET_F_HOST_UFO              \
    | VIRTIONET_F_GUEST_TSO4            \
    | VIRTIONET_F_GUEST_TSO6            \
    | VIRTIONET_F_GUEST_UFO             \
    | VIRTIONET_F_GUEST_CSUM)                                  /* @bugref(4796) Guest must handle partial chksums   */
#else
# define VIRTIONET_HOST_FEATURES_GSO
#endif

#define VIRTIONET_HOST_FEATURES_OFFERED \
      VIRTIONET_F_STATUS                \
    | VIRTIONET_F_GUEST_ANNOUNCE        \
    | VIRTIONET_F_MAC                   \
    | VIRTIONET_F_CTRL_VQ               \
    | VIRTIONET_F_CTRL_RX               \
    | VIRTIONET_F_CTRL_VLAN             \
    | VIRTIONET_HOST_FEATURES_GSO       \
    | VIRTIONET_F_MRG_RXBUF

#define FEATURE_ENABLED(feature)        RT_BOOL(!!(pThis->fNegotiatedFeatures & VIRTIONET_F_##feature))
#define FEATURE_DISABLED(feature)       (!FEATURE_ENABLED(feature))
#define FEATURE_OFFERED(feature)        VIRTIONET_HOST_FEATURES_OFFERED & VIRTIONET_F_##feature

#if FEATURE_OFFERED(MQ)
/* Instance data doesn't allow an array large enough to contain VIRTIONET_CTRL_MQ_VQ_PAIRS_MAX entries */
#   define VIRTIONET_MAX_QPAIRS         1  /* This should be increased at some point and made to work */
#else
#   define VIRTIONET_MAX_QPAIRS         VIRTIONET_CTRL_MQ_VQ_PAIRS_MIN /* default, VirtIO 1.0, 5.1.6.5.5 */
#endif

#define VIRTIONET_CTRL_MQ_VQ_PAIRS      64
#define VIRTIONET_MAX_WORKERS           VIRTIONET_MAX_QPAIRS + 1
#define VIRTIONET_MAX_VIRTQS            (VIRTIONET_MAX_QPAIRS * 2 + 1)
#define VIRTIONET_MAX_FRAME_SIZE        65535 + 18  /**< Max IP pkt size + Eth. header w/VLAN tag  */
#define VIRTIONET_MAC_FILTER_LEN        64
#define VIRTIONET_MAX_VLAN_ID           4096
#define VIRTIONET_RX_SEG_COUNT          32

#define VIRTQNAME(uVirtqNbr)            (pThis->aVirtqs[uVirtqNbr]->szName)
#define CBVIRTQNAME(uVirtqNbr)          RTStrNLen(VIRTQNAME(uVirtqNbr), sizeof(VIRTQNAME(uVirtqNbr)))

#define IS_TX_VIRTQ(n)                  ((n) != CTRLQIDX && ((n) & 1))
#define IS_RX_VIRTQ(n)                  ((n) != CTRLQIDX && !IS_TX_VIRTQ(n))
#define IS_CTRL_VIRTQ(n)                ((n) == CTRLQIDX)

/*
 * Macros to calculate queue type-pecific index number regardless of scale. VirtIO 1.0, 5.1.2
 */
#define RXQIDX(qPairIdx)                (qPairIdx * 2)
#define TXQIDX(qPairIdx)                (RXQIDX(qPairIdx) + 1)
#define CTRLQIDX                        (FEATURE_ENABLED(MQ) ? ((VIRTIONET_MAX_QPAIRS - 1) * 2 + 2) : 2)

#define IS_LINK_UP(pState)              !!(pState->virtioNetConfig.uStatus & VIRTIONET_F_LINK_UP)
#define IS_LINK_DOWN(pState)            !IS_LINK_UP(pState)

#define SET_LINK_UP(pState) \
            LogFunc(("SET_LINK_UP\n")); \
            pState->virtioNetConfig.uStatus |= VIRTIONET_F_LINK_UP; \
            virtioCoreNotifyConfigChanged(&pThis->Virtio)

#define SET_LINK_DOWN(pState) \
            LogFunc(("SET_LINK_DOWN\n")); \
            pState->virtioNetConfig.uStatus &= ~VIRTIONET_F_LINK_UP; \
            virtioCoreNotifyConfigChanged(&pThis->Virtio)

#define IS_VIRTQ_EMPTY(pDevIns, pVirtio, uVirtqNbr) \
            (virtioCoreVirtqAvailBufCount(pDevIns, pVirtio, uVirtqNbr) == 0)

#define PCI_DEVICE_ID_VIRTIONET_HOST               0x1000      /**< VirtIO transitional device ID for network card  */
#define PCI_CLASS_PROG_UNSPECIFIED                 0x00        /**< Programming interface. N/A.                     */
#define VIRTIONET_PCI_CLASS                        0x01        /**< Base class Mass Storage?                        */

/**
 * VirtIO Network (virtio-net) device-specific configuration subregion (VirtIO 1.0, 5.1.4)
 * Guest MMIO is processed through callback to VirtIO core which forwards references to network configuration
 * fields to this device-specific code through a callback.
 */
#pragma pack(1)

    typedef struct virtio_net_config
    {
        RTMAC  uMacAddress;                                     /**< mac                                            */

#if     FEATURE_OFFERED(STATUS)
               uint16_t uStatus;                                /**< status                                         */
#endif

#if     FEATURE_OFFERED(MQ)
               uint16_t uMaxVirtqPairs;                         /**< max_virtq_pairs                                */
#endif

    } VIRTIONET_CONFIG_T, PVIRTIONET_CONFIG_T;

#pragma pack()

#define VIRTIONET_F_LINK_UP                          1          /**< config status: Link is up                      */
#define VIRTIONET_F_ANNOUNCE                         2          /**< config status: Announce                        */

/** @name VirtIO 1.0 NET Host Device device specific control types
 * @{  */
#define VIRTIONET_HDR_F_NEEDS_CSUM                   1          /**< flags: Packet needs checksum                   */
#define VIRTIONET_HDR_GSO_NONE                       0          /**< gso_type: No Global Segmentation Offset        */
#define VIRTIONET_HDR_GSO_TCPV4                      1          /**< gso_type: Global Segment Offset for TCPV4      */
#define VIRTIONET_HDR_GSO_UDP                        3          /**< gso_type: Global Segment Offset for UDP        */
#define VIRTIONET_HDR_GSO_TCPV6                      4          /**< gso_type: Global Segment Offset for TCPV6      */
#define VIRTIONET_HDR_GSO_ECN                     0x80          /**< gso_type: Explicit Congestion Notification     */
/** @} */

/* Device operation: Net header packet (VirtIO 1.0, 5.1.6) */
#pragma pack(1)
struct virtio_net_pkt_hdr {
    uint8_t  uFlags;                                            /**< flags                                          */
    uint8_t  uGsoType;                                          /**< gso_type                                       */
    uint16_t uHdrLen;                                           /**< hdr_len                                        */
    uint16_t uGsoSize;                                          /**< gso_size                                       */
    uint16_t uChksumStart;                                      /**< Chksum_start                                   */
    uint16_t uChksumOffset;                                     /**< Chksum_offset                                  */
    uint16_t uNumBuffers;                                       /**< num_buffers                                    */
};
#pragma pack()
typedef virtio_net_pkt_hdr VIRTIONETPKTHDR, *PVIRTIONETPKTHDR;
AssertCompileSize(VIRTIONETPKTHDR, 12);

/* Control virtq: Command entry (VirtIO 1.0, 5.1.6.5) */
#pragma pack(1)
struct virtio_net_ctrl_hdr {
    uint8_t uClass;                                             /**< class                                          */
    uint8_t uCmd;                                               /**< command                                        */
};
#pragma pack()
typedef virtio_net_ctrl_hdr VIRTIONET_CTRL_HDR_T, *PVIRTIONET_CTRL_HDR_T;

typedef uint8_t VIRTIONET_CTRL_HDR_T_ACK;

/* Command entry fAck values */
#define VIRTIONET_OK                               0            /**< Internal success status                        */
#define VIRTIONET_ERROR                            1            /**< Internal failure status                        */

/** @name Control virtq: Receive filtering flags (VirtIO 1.0, 5.1.6.5.1)
 * @{  */
#define VIRTIONET_CTRL_RX                           0           /**< Control class: Receive filtering               */
#define VIRTIONET_CTRL_RX_PROMISC                   0           /**< Promiscuous mode                               */
#define VIRTIONET_CTRL_RX_ALLMULTI                  1           /**< All-multicast receive                          */
#define VIRTIONET_CTRL_RX_ALLUNI                    2           /**< All-unicast receive                            */
#define VIRTIONET_CTRL_RX_NOMULTI                   3           /**< No multicast receive                           */
#define VIRTIONET_CTRL_RX_NOUNI                     4           /**< No unicast receive                             */
#define VIRTIONET_CTRL_RX_NOBCAST                   5           /**< No broadcast receive                           */
/** @} */

typedef uint8_t  VIRTIONET_MAC_ADDRESS[6];
typedef uint32_t VIRTIONET_CTRL_MAC_TABLE_LEN;
typedef uint8_t  VIRTIONET_CTRL_MAC_ENTRIES[][6];

/** @name Control virtq: MAC address filtering flags (VirtIO 1.0, 5.1.6.5.2)
 * @{  */
#define VIRTIONET_CTRL_MAC                          1           /**< Control class: MAC address filtering           */
#define VIRTIONET_CTRL_MAC_TABLE_SET                0           /**< Set MAC table                                  */
#define VIRTIONET_CTRL_MAC_ADDR_SET                 1           /**< Set default MAC address                        */
/** @} */

/** @name Control virtq: MAC address filtering flags (VirtIO 1.0, 5.1.6.5.3)
 * @{  */
#define VIRTIONET_CTRL_VLAN                         2           /**< Control class: VLAN filtering                  */
#define VIRTIONET_CTRL_VLAN_ADD                     0           /**< Add VLAN to filter table                       */
#define VIRTIONET_CTRL_VLAN_DEL                     1           /**< Delete VLAN from filter table                  */
/** @} */

/** @name Control virtq: Gratuitous packet sending (VirtIO 1.0, 5.1.6.5.4)
 * @{  */
#define VIRTIONET_CTRL_ANNOUNCE                     3           /**< Control class: Gratuitous Packet Sending       */
#define VIRTIONET_CTRL_ANNOUNCE_ACK                 0           /**< Gratuitous Packet Sending ACK                  */
/** @} */

struct virtio_net_ctrl_mq {
    uint16_t    uVirtqueuePairs;                                /**<  virtqueue_pairs                               */
};

/** @name Control virtq: Receive steering in multiqueue mode (VirtIO 1.0, 5.1.6.5.5)
 * @{  */
#define VIRTIONET_CTRL_MQ                           4           /**< Control class: Receive steering                */
#define VIRTIONET_CTRL_MQ_VQ_PAIRS_SET              0           /**< Set number of TX/RX queues                     */
#define VIRTIONET_CTRL_MQ_VQ_PAIRS_MIN              1           /**< Minimum number of TX/RX queues                 */
#define VIRTIONET_CTRL_MQ_VQ_PAIRS_MAX         0x8000           /**< Maximum number of TX/RX queues                 */
/** @} */

uint64_t    uOffloads;                                          /**< offloads                                       */

/** @name Control virtq: Setting Offloads State (VirtIO 1.0, 5.1.6.5.6.1)
 * @{  */
#define VIRTIONET_CTRL_GUEST_OFFLOADS             5            /**< Control class: Offloads state configuration    */
#define VIRTIONET_CTRL_GUEST_OFFLOADS_SET         0            /**< Apply new offloads configuration               */
/** @} */

typedef enum VIRTIONETPKTHDRTYPE
{
    kVirtioNetUninitializedPktHdrType   = 0,                   /**< Uninitialized (default) packet header type      */
    kVirtioNetModernPktHdrWithoutMrgRx  = 1,                   /**< Packets should not be merged (modern driver)    */
    kVirtioNetModernPktHdrWithMrgRx     = 2,                   /**< Packets should be merged (modern driver)        */
    kVirtioNetLegacyPktHdrWithoutMrgRx  = 3,                   /**< Packets should not be merged (legacy driver)    */
    kVirtioNetLegacyPktHdrWithMrgRx     = 4,                   /**< Packets should be merged (legacy driver)        */
    kVirtioNetFor32BitHack              = 0x7fffffff
} VIRTIONETPKTHDRTYPE;

/**
 * device-specific queue info
 */
struct VIRTIONETWORKER;
struct VIRTIONETWORKERR3;

typedef struct VIRTIONETVIRTQ
{
    uint16_t                       uIdx;                        /**< Index of this queue                            */
    uint16_t                       align;
    bool                           fCtlVirtq;                   /**< If set this queue is the control queue         */
    bool                           fHasWorker;                  /**< If set this queue has an associated worker     */
    bool                           fAttachedToVirtioCore;       /**< Set if queue attached to virtio core           */
    char                           szName[VIRTIO_MAX_VIRTQ_NAME_SIZE]; /**< Virtq name                              */
} VIRTIONETVIRTQ, *PVIRTIONETVIRTQ;

/**
 * Worker thread context, shared state.
 */
typedef struct VIRTIONETWORKER
{
    SUPSEMEVENT                     hEvtProcess;                /**< handle of associated sleep/wake-up semaphore   */
    uint16_t                        uIdx;                       /**< Index of this worker                           */
    bool volatile                   fSleeping;                  /**< Flags whether worker thread is sleeping or not */
    bool volatile                   fNotified;                  /**< Flags whether worker thread notified           */
    bool                            fAssigned;                  /**< Flags whether worker thread has been set up    */
    uint8_t                         pad;
} VIRTIONETWORKER;
/** Pointer to a virtio net worker. */
typedef VIRTIONETWORKER *PVIRTIONETWORKER;

/**
 * Worker thread context, ring-3 state.
 */
typedef struct VIRTIONETWORKERR3
{
    R3PTRTYPE(PPDMTHREAD)           pThread;                    /**< pointer to worker thread's handle              */
    uint16_t                        uIdx;                       /**< Index of this worker                           */
    uint16_t                        pad;
} VIRTIONETWORKERR3;
/** Pointer to a virtio net worker. */
typedef VIRTIONETWORKERR3 *PVIRTIONETWORKERR3;

/**
 * VirtIO Host NET device state, shared edition.
 *
 * @extends     VIRTIOCORE
 */
typedef struct VIRTIONET
{
    /** The core virtio state.   */
    VIRTIOCORE              Virtio;

    /** Virtio device-specific configuration */
    VIRTIONET_CONFIG_T      virtioNetConfig;

    /** Per device-bound virtq worker-thread contexts (eventq slot unused) */
    VIRTIONETWORKER         aWorkers[VIRTIONET_MAX_VIRTQS];

    /** Track which VirtIO queues we've attached to */
    VIRTIONETVIRTQ          aVirtqs[VIRTIONET_MAX_VIRTQS];

    /** PDM device Instance name */
    char                    szInst[16];

    /** VirtIO features negotiated with the guest, including generic core and device specific */
    uint64_t                fNegotiatedFeatures;

    /** Number of Rx/Tx queue pairs (only one if MQ feature not negotiated */
    uint16_t                cVirtqPairs;

    /** Number of Rx/Tx queue pairs that have already been initialized */
    uint16_t                cInitializedVirtqPairs;

    /** Number of virtqueues total (which includes each queue of each pair plus one control queue */
    uint16_t                cVirtqs;

    /** Number of worker threads (one for the control queue and one for each Tx queue) */
    uint16_t                cWorkers;

    /** Alignment */
    uint16_t                alignment;

    /** Indicates transmission in progress -- only one thread is allowed. */
    uint32_t                uIsTransmitting;

    /** Link up delay (in milliseconds). */
    uint32_t                cMsLinkUpDelay;

    /** The number of actually used slots in aMacMulticastFilter. */
    uint32_t                cMulticastFilterMacs;

    /** The number of actually used slots in aMacUniicastFilter. */
    uint32_t                cUnicastFilterMacs;

    /** Semaphore leaf device's thread waits on until guest driver sends empty Rx bufs */
    SUPSEMEVENT             hEventRxDescAvail;

    /** Array of MAC multicast addresses accepted by RX filter. */
    RTMAC                   aMacMulticastFilter[VIRTIONET_MAC_FILTER_LEN];

    /** Array of MAC unicast addresses accepted by RX filter. */
    RTMAC                   aMacUnicastFilter[VIRTIONET_MAC_FILTER_LEN];

    /** Default MAC address which rx filtering accepts */
    RTMAC                   rxFilterMacDefault;

    /** MAC address obtained from the configuration. */
    RTMAC                   macConfigured;

    /** Bit array of VLAN filter, one bit per VLAN ID. */
    uint8_t                 aVlanFilter[VIRTIONET_MAX_VLAN_ID / sizeof(uint8_t)];

    /** Set if PDM leaf device at the network interface is starved for Rx buffers */
    bool volatile           fLeafWantsEmptyRxBufs;

    /** Number of packet being sent/received to show in debug log. */
    uint32_t                uPktNo;

    /** Flags whether VirtIO core is in ready state */
    uint8_t                 fVirtioReady;

    /** Resetting flag */
    uint8_t                 fResetting;

    /** Promiscuous mode -- RX filter accepts all packets. */
    uint8_t                 fPromiscuous;

    /** All multicast mode -- RX filter accepts all multicast packets. */
    uint8_t                 fAllMulticast;

    /** All unicast mode -- RX filter accepts all unicast packets. */
    uint8_t                 fAllUnicast;

    /** No multicast mode - Suppresses multicast receive */
    uint8_t                 fNoMulticast;

    /** No unicast mode - Suppresses unicast receive */
    uint8_t                 fNoUnicast;

    /** No broadcast mode - Suppresses broadcast receive */
    uint8_t                 fNoBroadcast;

    /** Type of network pkt header based on guest driver version/features */
    VIRTIONETPKTHDRTYPE     ePktHdrType;

    /** Size of network pkt header based on guest driver version/features */
    uint16_t                cbPktHdr;

    /** True if physical cable is attached in configuration. */
    bool                    fCableConnected;

    /** True if this device should offer legacy virtio support to the guest */
    bool                    fOfferLegacy;

    /** @name Statistic
     * @{ */
    STAMCOUNTER             StatReceiveBytes;
    STAMCOUNTER             StatTransmitBytes;
    STAMCOUNTER             StatReceiveGSO;
    STAMCOUNTER             StatTransmitPackets;
    STAMCOUNTER             StatTransmitGSO;
    STAMCOUNTER             StatTransmitCSum;
#ifdef VBOX_WITH_STATISTICS
    STAMPROFILE             StatReceive;
    STAMPROFILE             StatReceiveStore;
    STAMPROFILEADV          StatTransmit;
    STAMPROFILE             StatTransmitSend;
    STAMPROFILE             StatRxOverflow;
    STAMCOUNTER             StatRxOverflowWakeup;
    STAMCOUNTER             StatTransmitByNetwork;
    STAMCOUNTER             StatTransmitByThread;
    /** @}  */
#endif
} VIRTIONET;
/** Pointer to the shared state of the VirtIO Host NET device. */
typedef VIRTIONET *PVIRTIONET;

/**
 * VirtIO Host NET device state, ring-3 edition.
 *
 * @extends     VIRTIOCORER3
 */
typedef struct VIRTIONETR3
{
    /** The core virtio ring-3 state. */
    VIRTIOCORER3                    Virtio;

    /** Per device-bound virtq worker-thread contexts (eventq slot unused) */
    VIRTIONETWORKERR3               aWorkers[VIRTIONET_MAX_VIRTQS];

    /** The device instance.
     * @note This is _only_ for use whxen dealing with interface callbacks. */
    PPDMDEVINSR3                    pDevIns;

    /** Status LUN: Base interface. */
    PDMIBASE                        IBase;

    /** Status LUN: LED port interface. */
    PDMILEDPORTS                    ILeds;

    /** Status LUN: LED connector (peer). */
    R3PTRTYPE(PPDMILEDCONNECTORS)   pLedsConnector;

    /** Status: LED */
    PDMLED                          led;

    /** Attached network driver. */
    R3PTRTYPE(PPDMIBASE)            pDrvBase;

    /** Network port interface (down) */
    PDMINETWORKDOWN                 INetworkDown;

    /** Network config port interface (main). */
    PDMINETWORKCONFIG               INetworkConfig;

    /** Connector of attached network driver. */
    R3PTRTYPE(PPDMINETWORKUP)       pDrv;

    /** Link Up(/Restore) Timer. */
    TMTIMERHANDLE                   hLinkUpTimer;

} VIRTIONETR3;

/** Pointer to the ring-3 state of the VirtIO Host NET device. */
typedef VIRTIONETR3 *PVIRTIONETR3;

/**
 * VirtIO Host NET device state, ring-0 edition.
 */
typedef struct VIRTIONETR0
{
    /** The core virtio ring-0 state. */
    VIRTIOCORER0                    Virtio;
} VIRTIONETR0;
/** Pointer to the ring-0 state of the VirtIO Host NET device. */
typedef VIRTIONETR0 *PVIRTIONETR0;

/**
 * VirtIO Host NET device state, raw-mode edition.
 */
typedef struct VIRTIONETRC
{
    /** The core virtio raw-mode state. */
    VIRTIOCORERC                    Virtio;
} VIRTIONETRC;
/** Pointer to the ring-0 state of the VirtIO Host NET device. */
typedef VIRTIONETRC *PVIRTIONETRC;

/** @typedef VIRTIONETCC
 * The instance data for the current context. */
typedef CTX_SUFF(VIRTIONET) VIRTIONETCC;

/** @typedef PVIRTIONETCC
 * Pointer to the instance data for the current context. */
typedef CTX_SUFF(PVIRTIONET) PVIRTIONETCC;

#ifdef IN_RING3
static DECLCALLBACK(int) virtioNetR3WorkerThread(PPDMDEVINS pDevIns, PPDMTHREAD pThread);
static int virtioNetR3CreateWorkerThreads(PPDMDEVINS, PVIRTIONET, PVIRTIONETCC);

/**
 * Helper function used when logging state of a VM thread.
 *
 * @param   Thread
 *
 * @return  Associated name of thread as a pointer to a zero-terminated string.
 */
DECLINLINE(const char *) virtioNetThreadStateName(PPDMTHREAD pThread)
{
    if (!pThread)
        return "<null>";

    switch(pThread->enmState)
    {
        case PDMTHREADSTATE_INVALID:
            return "invalid state";
        case PDMTHREADSTATE_INITIALIZING:
            return "initializing";
        case PDMTHREADSTATE_SUSPENDING:
            return "suspending";
        case PDMTHREADSTATE_SUSPENDED:
            return "suspended";
        case PDMTHREADSTATE_RESUMING:
            return "resuming";
        case PDMTHREADSTATE_RUNNING:
            return "running";
        case PDMTHREADSTATE_TERMINATING:
            return "terminating";
        case PDMTHREADSTATE_TERMINATED:
            return "terminated";
        default:
            return "unknown state";
    }
}
#endif

/**
 * Wakeup PDM managed downstream (e.g. hierarchically inferior device's) RX thread
 */
static DECLCALLBACK(void) virtioNetWakeupRxBufWaiter(PPDMDEVINS pDevIns)
{
    PVIRTIONET pThis = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);

    AssertReturnVoid(pThis->hEventRxDescAvail != NIL_SUPSEMEVENT);

    STAM_COUNTER_INC(&pThis->StatRxOverflowWakeup);
    if (pThis->hEventRxDescAvail != NIL_SUPSEMEVENT)
    {
        Log10Func(("[%s] Waking downstream device's Rx buf waiter thread\n", pThis->szInst));
        int rc = PDMDevHlpSUPSemEventSignal(pDevIns, pThis->hEventRxDescAvail);
        AssertRC(rc);
    }
}

/**
 * Guest notifying us of its activity with a queue. Figure out which queue and respond accordingly.
 *
 * @callback_method_impl{VIRTIOCORER0,pfnVirtqNotified}
 */
static DECLCALLBACK(void) virtioNetVirtqNotified(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtqNbr)
{
    RT_NOREF(pVirtio);
    PVIRTIONET pThis = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);

    PVIRTIONETVIRTQ  pVirtq  = &pThis->aVirtqs[uVirtqNbr];
    PVIRTIONETWORKER pWorker = &pThis->aWorkers[uVirtqNbr];

#ifdef VIRTIO_REL_INFO_DUMP
    if (ASMAtomicReadBool(&pVirtio->fRecovering))
        LogRel(("[%s] Received notification from the guest on queue %u\n", pThis->szInst, uVirtqNbr));
#endif /* VIRTIO_REL_INFO_DUMP */
#if defined (IN_RING3) && defined (LOG_ENABLED)
    RTLogFlush(NULL);
#endif
    if (IS_RX_VIRTQ(uVirtqNbr))
    {
        uint16_t cBufsAvailable = virtioCoreVirtqAvailBufCount(pDevIns, pVirtio, uVirtqNbr);

        if (cBufsAvailable)
        {
            Log10Func(("%s %u empty bufs added to %s by guest (notifying leaf device)\n",
                       pThis->szInst, cBufsAvailable, pVirtq->szName));
            virtioNetWakeupRxBufWaiter(pDevIns);
        }
        else
            Log10Func(("%s \n\n***WARNING: %s notified but no empty bufs added by guest! (skip leaf dev. notification)\n\n",
                    pThis->szInst, pVirtq->szName));
    }
    else if (IS_TX_VIRTQ(uVirtqNbr) || IS_CTRL_VIRTQ(uVirtqNbr))
    {
        /* Wake queue's worker thread up if sleeping (e.g. a Tx queue, or the control queue */
        if (!ASMAtomicXchgBool(&pWorker->fNotified, true))
        {
            if (ASMAtomicReadBool(&pWorker->fSleeping))
            {
                Log10Func(("[%s] %s has available buffers - waking worker.\n", pThis->szInst, pVirtq->szName));

                int rc = PDMDevHlpSUPSemEventSignal(pDevIns, pWorker->hEvtProcess);
                AssertRC(rc);
            }
            else
                Log10Func(("[%s] %s has available buffers - worker already awake\n", pThis->szInst, pVirtq->szName));
        }
        else
            Log10Func(("[%s] %s has available buffers - waking worker.\n", pThis->szInst, pVirtq->szName));
    }
    else
        LogRelFunc(("[%s] unrecognized queue %s (idx=%d) notified\n", pThis->szInst, pVirtq->szName, uVirtqNbr));
}

#ifdef IN_RING3 /* spans most of the file, at the moment. */

/**
 * @callback_method_impl{FNPDMTHREADWAKEUPDEV}
 */
static DECLCALLBACK(int) virtioNetR3WakeupWorker(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    PVIRTIONET       pThis = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);
    PVIRTIONETWORKER pWorker = (PVIRTIONETWORKER)pThread->pvUser;

    Log10Func(("[%s]\n", pThis->szInst));
    RT_NOREF(pThis);
    return PDMDevHlpSUPSemEventSignal(pDevIns, pWorker->hEvtProcess);
}

/**
 * Set queue names, distinguishing between modern or legacy mode.
 *
 * @note This makes it obvious during logging which mode this transitional device is
 *       operating in, legacy or modern.
 *
 * @param  pThis        Device specific device state
 * @param  fLegacy      (input) true if running in legacy mode
 *                              false if running in modern mode
 */
DECLINLINE(void) virtioNetR3SetVirtqNames(PVIRTIONET pThis, uint32_t fLegacy)
{
    RTStrCopy(pThis->aVirtqs[CTRLQIDX].szName, VIRTIO_MAX_VIRTQ_NAME_SIZE, fLegacy ? "legacy-ctrlq" : " modern-ctrlq");
    for (uint16_t qPairIdx = 0; qPairIdx < pThis->cVirtqPairs; qPairIdx++)
    {
        RTStrPrintf(pThis->aVirtqs[RXQIDX(qPairIdx)].szName, VIRTIO_MAX_VIRTQ_NAME_SIZE, "%s-recvq<%d>", fLegacy ? "legacy" : "modern", qPairIdx);
        RTStrPrintf(pThis->aVirtqs[TXQIDX(qPairIdx)].szName, VIRTIO_MAX_VIRTQ_NAME_SIZE, "%s-xmitq<%d>", fLegacy ? "legacy" : "modern", qPairIdx);
    }
}

/**
 * Dump a packet to debug log.
 *
 * @param   pThis       The virtio-net shared instance data.
 * @param   pbPacket    The packet.
 * @param   cb          The size of the packet.
 * @param   pszText     A string denoting direction of packet transfer.
 */
DECLINLINE(void) virtioNetR3PacketDump(PVIRTIONET pThis, const uint8_t *pbPacket, size_t cb, const char *pszText)
{
#ifdef LOG_ENABLED
    if (!LogIs12Enabled())
        return;
#endif
    vboxEthPacketDump(pThis->szInst, pszText, pbPacket, (uint32_t)cb);
}

#ifdef LOG_ENABLED
void virtioNetDumpGcPhysRxBuf(PPDMDEVINS pDevIns, PVIRTIONETPKTHDR pRxPktHdr,
                     uint16_t cVirtqBufs, uint8_t *pvBuf, uint16_t cb, RTGCPHYS GCPhysRxBuf, uint8_t cbRxBuf)
{
    PVIRTIONET pThis = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);
    pRxPktHdr->uNumBuffers = cVirtqBufs;
    if (pRxPktHdr)
    {
        LogFunc(("%*c\nrxPktHdr\n"
                 "    uFlags ......... %2.2x\n    uGsoType ....... %2.2x\n    uHdrLen ........ %4.4x\n"
                 "    uGsoSize ....... %4.4x\n    uChksumStart ... %4.4x\n    uChksumOffset .. %4.4x\n",
                        60, ' ', pRxPktHdr->uFlags, pRxPktHdr->uGsoType, pRxPktHdr->uHdrLen, pRxPktHdr->uGsoSize,
                        pRxPktHdr->uChksumStart, pRxPktHdr->uChksumOffset));
        if (!virtioCoreIsLegacyMode(&pThis->Virtio) || FEATURE_ENABLED(MRG_RXBUF))
            LogFunc(("    uNumBuffers .... %4.4x\n", pRxPktHdr->uNumBuffers));
        virtioCoreHexDump((uint8_t *)pRxPktHdr, sizeof(VIRTIONETPKTHDR), 0, "Dump of virtual rPktHdr");
    }
    virtioNetR3PacketDump(pThis, (const uint8_t *)pvBuf, cb, "<-- Incoming");
    LogFunc((". . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . . .\n"));
    virtioCoreGCPhysHexDump(pDevIns, GCPhysRxBuf, cbRxBuf, 0, "Phys Mem Dump of Rx pkt");
    LogFunc(("%*c", 60, '-'));
}

#endif /* LOG_ENABLED */

/**
 * @callback_method_impl{FNDBGFHANDLERDEV, virtio-net debugger info callback.}
 */
#ifdef VIRTIO_REL_INFO_DUMP
DECLCALLBACK(void) virtioNetR3Info(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
#else  /* !VIRTIO_REL_INFO_DUMP */
static DECLCALLBACK(void) virtioNetR3Info(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
#endif /* !VIRTIO_REL_INFO_DUMP */
{
    PVIRTIONET     pThis   = PDMDEVINS_2_DATA(pDevIns,  PVIRTIONET);
    PVIRTIONETCC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIONETCC);

    bool fNone     = pszArgs && *pszArgs == '\0';
    bool fAll      = pszArgs && (*pszArgs == 'a' || *pszArgs == 'A'); /* "all"      */
    bool fNetwork  = pszArgs && (*pszArgs == 'n' || *pszArgs == 'N'); /* "network"  */
    bool fFeatures = pszArgs && (*pszArgs == 'f' || *pszArgs == 'F'); /* "features" */
    bool fState    = pszArgs && (*pszArgs == 's' || *pszArgs == 'S'); /* "state"    */
    bool fPointers = pszArgs && (*pszArgs == 'p' || *pszArgs == 'P'); /* "pointers" */
    bool fVirtqs   = pszArgs && (*pszArgs == 'q' || *pszArgs == 'Q'); /* "queues    */
#ifdef VIRTIO_REL_INFO_DUMP
    if (pszArgs && (*pszArgs == 't' || *pszArgs == 'T'))
    {
        ASMAtomicWriteBool(&pThis->Virtio.fTestRecovery, true);
        pHlp->pfnPrintf(pHlp, "Triggering recovery for %s\n", pThis->szInst);
        return;
    }
#endif /* VIRTIO_REL_INFO_DUMP */

    /* Show basic information. */
    pHlp->pfnPrintf(pHlp,
        "\n"
        "---------------------------------------------------------------------------\n"
        "Debug Info: %s\n"
        "        (options: [a]ll, [n]et, [f]eatures, [s]tate, [p]ointers, [q]ueues)\n"
        "---------------------------------------------------------------------------\n\n",
        pThis->szInst);

    if (fNone)
        return;

    /* Show offered/unoffered, accepted/rejected features */
    if (fAll || fFeatures)
    {
        virtioCorePrintDeviceFeatures(&pThis->Virtio, pHlp, s_aDevSpecificFeatures,
            RT_ELEMENTS(s_aDevSpecificFeatures));
        pHlp->pfnPrintf(pHlp, "\n");
    }

    /* Show queues (and associate worker info if applicable) */
    if (fAll || fVirtqs)
    {
        pHlp->pfnPrintf(pHlp, "Virtq information:\n\n");
        for (int uVirtqNbr = 0; uVirtqNbr < pThis->cVirtqs; uVirtqNbr++)
        {
            PVIRTIONETVIRTQ pVirtq = &pThis->aVirtqs[uVirtqNbr];

            if (pVirtq->fHasWorker)
            {
                PVIRTIONETWORKER   pWorker   = &pThis->aWorkers[uVirtqNbr];
                PVIRTIONETWORKERR3 pWorkerR3 = &pThisCC->aWorkers[uVirtqNbr];

                Assert((pWorker->uIdx == pVirtq->uIdx));
                Assert((pWorkerR3->uIdx == pVirtq->uIdx));

                if (pWorker->fAssigned)
                {
                    pHlp->pfnPrintf(pHlp, "    %-15s (pThread: %p %s) ",
                        pVirtq->szName,
                        pWorkerR3->pThread,
                        virtioNetThreadStateName(pWorkerR3->pThread));
                    if (pVirtq->fAttachedToVirtioCore)
                    {
                        pHlp->pfnPrintf(pHlp, "worker: ");
                        pHlp->pfnPrintf(pHlp, "%s", pWorker->fSleeping ? "blocking" : "unblocked");
                        pHlp->pfnPrintf(pHlp, "%s", pWorker->fNotified ? ", notified" : "");
                    }
                    else
                    if (pWorker->fNotified)
                        pHlp->pfnPrintf(pHlp, "not attached to virtio core");
                }
            }
            else
            {
                pHlp->pfnPrintf(pHlp, "    %-15s (INetworkDown's thread) %s", pVirtq->szName,
                    pVirtq->fAttachedToVirtioCore  ? "" : "not attached to virtio core");
            }
            pHlp->pfnPrintf(pHlp, "\n");
            virtioCoreR3VirtqInfo(pDevIns, pHlp, &pThis->Virtio, pszArgs, uVirtqNbr);
            pHlp->pfnPrintf(pHlp, "    ---------------------------------------------------------------------\n");
            pHlp->pfnPrintf(pHlp, "\n");
        }
        pHlp->pfnPrintf(pHlp, "\n");
    }

    /* Show various pointers */
    if (fAll || fPointers)
    {
        pHlp->pfnPrintf(pHlp, "Internal Pointers (for instance \"%s\"):\n\n", pThis->szInst);
        pHlp->pfnPrintf(pHlp, "    pDevIns ................... %p\n",   pDevIns);
        pHlp->pfnPrintf(pHlp, "    PVIRTIOCORE ............... %p\n",   &pThis->Virtio);
        pHlp->pfnPrintf(pHlp, "    PVIRTIONET ................ %p\n",   pThis);
        pHlp->pfnPrintf(pHlp, "    PVIRTIONETCC .............. %p\n",   pThisCC);
        pHlp->pfnPrintf(pHlp, "    VIRTIONETVIRTQ[] .......... %p\n",   pThis->aVirtqs);
        pHlp->pfnPrintf(pHlp, "    pDrvBase .................. %p\n",   pThisCC->pDrvBase);
        pHlp->pfnPrintf(pHlp, "    pDrv ...................... %p\n",   pThisCC->pDrv);
        pHlp->pfnPrintf(pHlp, "\n");
    }

    /* Show device state info */
    if (fAll || fState)
    {
        pHlp->pfnPrintf(pHlp, "Device state:\n\n");
        uint32_t fTransmitting = ASMAtomicReadU32(&pThis->uIsTransmitting);

        pHlp->pfnPrintf(pHlp, "    Transmitting: ............. %s\n", fTransmitting ? "true" : "false");
        pHlp->pfnPrintf(pHlp, "\n");
        pHlp->pfnPrintf(pHlp, "Misc state\n");
        pHlp->pfnPrintf(pHlp, "\n");
        pHlp->pfnPrintf(pHlp, "    fOfferLegacy .............. %d\n",   pThis->fOfferLegacy);
        pHlp->pfnPrintf(pHlp, "    fVirtioReady .............. %d\n",   pThis->fVirtioReady);
        pHlp->pfnPrintf(pHlp, "    fResetting ................ %d\n",   pThis->fResetting);
        pHlp->pfnPrintf(pHlp, "    fGenUpdatePending ......... %d\n",   pThis->Virtio.fGenUpdatePending);
        pHlp->pfnPrintf(pHlp, "    fMsiSupport ............... %d\n",   pThis->Virtio.fMsiSupport);
        pHlp->pfnPrintf(pHlp, "    uConfigGeneration ......... %d\n",   pThis->Virtio.uConfigGeneration);
        pHlp->pfnPrintf(pHlp, "    uDeviceStatus ............. 0x%x\n", pThis->Virtio.fDeviceStatus);
        pHlp->pfnPrintf(pHlp, "    cVirtqPairs .,............. %d\n",   pThis->cVirtqPairs);
        pHlp->pfnPrintf(pHlp, "    cVirtqs .,................. %d\n",   pThis->cVirtqs);
        pHlp->pfnPrintf(pHlp, "    cWorkers .................. %d\n",   pThis->cWorkers);
        pHlp->pfnPrintf(pHlp, "    MMIO mapping name ......... %s\n",   pThisCC->Virtio.szMmioName);
        pHlp->pfnPrintf(pHlp, "\n");
    }

    /* Show network related information */
    if (fAll || fNetwork)
    {
        pHlp->pfnPrintf(pHlp, "Network configuration:\n\n");
        pHlp->pfnPrintf(pHlp, "    MAC: ...................... %RTmac\n", &pThis->macConfigured);
        pHlp->pfnPrintf(pHlp, "\n");
        pHlp->pfnPrintf(pHlp, "    Cable: .................... %s\n",      pThis->fCableConnected ? "connected" : "disconnected");
        pHlp->pfnPrintf(pHlp, "    Link-up delay: ............ %d ms\n",   pThis->cMsLinkUpDelay);
        pHlp->pfnPrintf(pHlp, "\n");
        pHlp->pfnPrintf(pHlp, "    Accept all multicast: ..... %s\n",      pThis->fAllMulticast  ? "true" : "false");
        pHlp->pfnPrintf(pHlp, "    Suppress broadcast: ....... %s\n",      pThis->fNoBroadcast   ? "true" : "false");
        pHlp->pfnPrintf(pHlp, "    Suppress unicast: ......... %s\n",      pThis->fNoUnicast     ? "true" : "false");
        pHlp->pfnPrintf(pHlp, "    Suppress multicast: ....... %s\n",      pThis->fNoMulticast   ? "true" : "false");
        pHlp->pfnPrintf(pHlp, "    Promiscuous: .............. %s\n",      pThis->fPromiscuous   ? "true" : "false");
        pHlp->pfnPrintf(pHlp, "\n");
        pHlp->pfnPrintf(pHlp, "    Default Rx MAC filter: .... %RTmac\n", &pThis->rxFilterMacDefault);
        pHlp->pfnPrintf(pHlp, "\n");

        pHlp->pfnPrintf(pHlp, "    Unicast filter MACs:\n");

        if (!pThis->cUnicastFilterMacs)
            pHlp->pfnPrintf(pHlp, "        <none>\n");

        for (uint32_t i = 0; i < pThis->cUnicastFilterMacs; i++)
            pHlp->pfnPrintf(pHlp, "        %RTmac\n", &pThis->aMacUnicastFilter[i]);

        pHlp->pfnPrintf(pHlp, "\n    Multicast filter MACs:\n");

        if (!pThis->cMulticastFilterMacs)
            pHlp->pfnPrintf(pHlp, "        <none>\n");

        for (uint32_t i = 0; i < pThis->cMulticastFilterMacs; i++)
            pHlp->pfnPrintf(pHlp, "        %RTmac\n", &pThis->aMacMulticastFilter[i]);

        pHlp->pfnPrintf(pHlp, "\n\n");
        pHlp->pfnPrintf(pHlp, "    Leaf starved: ............. %s\n",      pThis->fLeafWantsEmptyRxBufs ? "true" : "false");
        pHlp->pfnPrintf(pHlp, "\n");
    }
    /** @todo implement this
      * pHlp->pfnPrintf(pHlp, "\n");
      * virtioCoreR3Info(pDevIns, pHlp, pszArgs);
      */
    pHlp->pfnPrintf(pHlp, "\n");
}

/**
 * Checks whether certain mutually dependent negotiated features are clustered in required combinations.
 *
 * @note See VirtIO 1.0 spec, Section 5.1.3.1
 *
 * @param fFeatures     Bitmask of negotiated features to evaluate
 *
 * @returns             true if valid feature combination(s) found.
 *                      false if non-valid feature set.
 */
DECLINLINE(bool) virtioNetValidateRequiredFeatures(uint32_t fFeatures)
{
    uint32_t fGuestChksumRequired =   fFeatures & VIRTIONET_F_GUEST_TSO4
                                   || fFeatures & VIRTIONET_F_GUEST_TSO6
                                   || fFeatures & VIRTIONET_F_GUEST_UFO;

    uint32_t fHostChksumRequired =    fFeatures & VIRTIONET_F_HOST_TSO4
                                   || fFeatures & VIRTIONET_F_HOST_TSO6
                                   || fFeatures & VIRTIONET_F_HOST_UFO;

    uint32_t fCtrlVqRequired =        fFeatures & VIRTIONET_F_CTRL_RX
                                   || fFeatures & VIRTIONET_F_CTRL_VLAN
                                   || fFeatures & VIRTIONET_F_GUEST_ANNOUNCE
                                   || fFeatures & VIRTIONET_F_MQ
                                   || fFeatures & VIRTIONET_F_CTRL_MAC_ADDR;

    if (fGuestChksumRequired && !(fFeatures & VIRTIONET_F_GUEST_CSUM))
        return false;

    if (fHostChksumRequired && !(fFeatures & VIRTIONET_F_CSUM))
        return false;

    if (fCtrlVqRequired && !(fFeatures & VIRTIONET_F_CTRL_VQ))
        return false;

    if (   fFeatures & VIRTIONET_F_GUEST_ECN
        && !(   fFeatures & VIRTIONET_F_GUEST_TSO4
             || fFeatures & VIRTIONET_F_GUEST_TSO6))
                    return false;

    if (   fFeatures & VIRTIONET_F_HOST_ECN
        && !(   fFeatures & VIRTIONET_F_HOST_TSO4
             || fFeatures & VIRTIONET_F_HOST_TSO6))
                    return false;
    return true;
}

/**
 * Read or write device-specific configuration parameters.
 * This is called by VirtIO core code a guest-initiated MMIO access is made to access device-specific
 * configuration
 *
 * @note  See VirtIO 1.0 spec, 2.3 Device Configuration Space
 *
 * @param pThis             Pointer to device-specific state
 * @param uOffsetOfAccess   Offset (within VIRTIONET_CONFIG_T)
 * @param pv                Pointer to data to read or write
 * @param cb                Number of bytes to read or write
 * @param fWrite            True if writing, false if reading
 *
 * @returns VINF_SUCCESS if successful, or VINF_IOM_MMIO_UNUSED if fails (bad offset or size)
 */
static int virtioNetR3DevCfgAccess(PVIRTIONET pThis, uint32_t uOffsetOfAccess, void *pv, uint32_t cb, bool fWrite)
{
    AssertReturn(pv && cb <= sizeof(uint32_t), fWrite ? VINF_SUCCESS : VINF_IOM_MMIO_UNUSED_00);

    if (VIRTIO_DEV_CONFIG_SUBMATCH_MEMBER( uMacAddress,      VIRTIONET_CONFIG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS_READONLY( uMacAddress,      VIRTIONET_CONFIG_T, uOffsetOfAccess, &pThis->virtioNetConfig);
#if FEATURE_OFFERED(STATUS)
    else
    if (VIRTIO_DEV_CONFIG_SUBMATCH_MEMBER( uStatus,          VIRTIONET_CONFIG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS_READONLY( uStatus,          VIRTIONET_CONFIG_T, uOffsetOfAccess, &pThis->virtioNetConfig);
#endif
#if FEATURE_OFFERED(MQ)
    else
    if (VIRTIO_DEV_CONFIG_MATCH_MEMBER(    uMaxVirtqPairs,   VIRTIONET_CONFIG_T, uOffsetOfAccess))
        VIRTIO_DEV_CONFIG_ACCESS_READONLY( uMaxVirtqPairs,   VIRTIONET_CONFIG_T, uOffsetOfAccess, &pThis->virtioNetConfig);
#endif
    else
    {
        LogFunc(("%s Bad access by guest to virtio_net_config: off=%u (%#x), cb=%u\n",
                pThis->szInst, uOffsetOfAccess, uOffsetOfAccess, cb));
        return fWrite ? VINF_SUCCESS : VINF_IOM_MMIO_UNUSED_00;
    }
#ifdef VIRTIO_REL_INFO_DUMP
    if (ASMAtomicReadBool(&pThis->Virtio.fRecovering))
        LogRel(("[%s] %s %u bytes @ dev cfg + %u: %.*Rhxs\n", pThis->szInst, fWrite ? "wrote" : "read", cb, uOffsetOfAccess, cb, pv));
#endif /* VIRTIO_REL_INFO_DUMP */
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{VIRTIOCORER3,pfnDevCapRead}
 */
static DECLCALLBACK(int) virtioNetR3DevCapRead(PPDMDEVINS pDevIns, uint32_t uOffset, void *pv, uint32_t cb)
{
    PVIRTIONET pThis = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);

    RT_NOREF(pThis);
    return virtioNetR3DevCfgAccess(PDMDEVINS_2_DATA(pDevIns, PVIRTIONET), uOffset, pv, cb, false /*fRead*/);
}

/**
 * @callback_method_impl{VIRTIOCORER3,pfnDevCapWrite}
 */
static DECLCALLBACK(int) virtioNetR3DevCapWrite(PPDMDEVINS pDevIns, uint32_t uOffset, const void *pv, uint32_t cb)
{
    PVIRTIONET pThis = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);

    Log10Func(("[%s] uOffset: %u, cb: %u: %.*Rhxs\n", pThis->szInst, uOffset, cb, cb, pv));
    RT_NOREF(pThis);
    return virtioNetR3DevCfgAccess(PDMDEVINS_2_DATA(pDevIns, PVIRTIONET), uOffset, (void *)pv, cb, true /*fWrite*/);
}

static int virtioNetR3VirtqDestroy(PVIRTIOCORE pVirtio, PVIRTIONETVIRTQ pVirtq)
{
    PVIRTIONET         pThis     = RT_FROM_MEMBER(pVirtio, VIRTIONET, Virtio);
    PVIRTIONETCC       pThisCC   = PDMDEVINS_2_DATA_CC(pVirtio->pDevInsR3, PVIRTIONETCC);
    PVIRTIONETWORKER   pWorker   = &pThis->aWorkers[pVirtq->uIdx];
    PVIRTIONETWORKERR3 pWorkerR3 = &pThisCC->aWorkers[pVirtq->uIdx];

    int rc = VINF_SUCCESS, rcThread;
    Log10Func(("[%s] Destroying \"%s\"", pThis->szInst, pVirtq->szName));
    if (pVirtq->fHasWorker)
    {
        Log10((" and its worker"));
        rc = PDMDevHlpThreadDestroy(pVirtio->pDevInsR3,  pWorkerR3->pThread, &rcThread);
        AssertRCReturn(rc, rc);
        pWorkerR3->pThread = 0;

        rc = PDMDevHlpSUPSemEventClose(pVirtio->pDevInsR3, pWorker->hEvtProcess);
        AssertRCReturn(rc, rc);
        pWorker->hEvtProcess = 0;

        pVirtq->fHasWorker = false;
    }
    pWorker->fAssigned = false;
    pVirtq->fCtlVirtq  = false;
    Log10(("\n"));
    return rc;
}

/**
 * Takes down the link temporarily if its current status is up.
 *
 * This is used during restore and when replumbing the network link.
 *
 * The temporary link outage is supposed to indicate to the OS that all network
 * connections have been lost and that it for instance is appropriate to
 * renegotiate any DHCP lease.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The virtio-net shared instance data.
 * @param   pThisCC     The virtio-net ring-3 instance data.
 */
static void virtioNetR3TempLinkDown(PPDMDEVINS pDevIns, PVIRTIONET pThis, PVIRTIONETCC pThisCC)
{
    if (IS_LINK_UP(pThis))
    {
        SET_LINK_DOWN(pThis);

        /* Re-establish link in 5 seconds. */
        int rc = PDMDevHlpTimerSetMillies(pDevIns, pThisCC->hLinkUpTimer, pThis->cMsLinkUpDelay);
        AssertRC(rc);

        LogFunc(("[%s] Link is down temporarily\n", pThis->szInst));
    }
}


static void virtioNetConfigurePktHdr(PVIRTIONET pThis, uint32_t fLegacy)
{
    /* Calculate network packet header type and size based on what we know now */
    pThis->cbPktHdr = sizeof(VIRTIONETPKTHDR);
    if (!fLegacy)
        /* Modern (e.g. >= VirtIO 1.0) device specification's pkt size rules */
        if (FEATURE_ENABLED(MRG_RXBUF))
            pThis->ePktHdrType = kVirtioNetModernPktHdrWithMrgRx;
        else /* Modern guest driver with MRG_RX feature disabled */
            pThis->ePktHdrType = kVirtioNetModernPktHdrWithoutMrgRx;
    else
    {
        /* Legacy (e.g. < VirtIO 1.0) device specification's pkt size rules */
        if (FEATURE_ENABLED(MRG_RXBUF))
            pThis->ePktHdrType = kVirtioNetLegacyPktHdrWithMrgRx;
        else /* Legacy guest with MRG_RX feature disabled */
        {
            pThis->ePktHdrType = kVirtioNetLegacyPktHdrWithoutMrgRx;
            pThis->cbPktHdr -= RT_SIZEOFMEMB(VIRTIONETPKTHDR, uNumBuffers);
        }
    }
}


/*********************************************************************************************************************************
*   Saved state                                                                                                                  *
*********************************************************************************************************************************/

/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 *
 * @note: This is included to accept and migrate VMs that had used the original VirtualBox legacy-only virtio-net (network card)
 *        controller device emulator ("DevVirtioNet.cpp") to work with this superset of VirtIO compatibility known
 *        as a transitional device (see PDM-invoked device constructor comments for more information)
 */
static DECLCALLBACK(int) virtioNetR3LegacyDeviceLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass,
                                                         RTMAC uMacLoaded)
{
    PVIRTIONET     pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);
    PVIRTIONETCC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIONETCC);
    PCPDMDEVHLPR3  pHlp    = pDevIns->pHlpR3;
    int rc;

    Log7Func(("[%s] LOAD EXEC (LEGACY)!!\n", pThis->szInst));

    if (   memcmp(&uMacLoaded.au8, &pThis->macConfigured.au8, sizeof(uMacLoaded))
        && (   uPass == 0
            || !PDMDevHlpVMTeleportedAndNotFullyResumedYet(pDevIns)))
        LogRelFunc(("[%s]: The mac address differs: config=%RTmac saved=%RTmac\n",
            pThis->szInst, &pThis->macConfigured, &uMacLoaded));

    if (uPass == SSM_PASS_FINAL)
    {
        /* Call the virtio core to have it load legacy device state */
        rc = virtioCoreR3LegacyDeviceLoadExec(&pThis->Virtio, pDevIns->pHlpR3, pSSM, uVersion, VIRTIONET_SAVEDSTATE_VERSION_3_1_BETA1_LEGACY);
        AssertRCReturn(rc, rc);
        /*
         * Scan constructor-determined virtqs to determine if they are all valid-as-restored.
         * If so, nudge them with a signal, otherwise destroy the unusable queue(s)
         * to avoid tripping up the other queue processing logic.
         */
        int cVirtqsToRemove = 0;
        for (int uVirtqNbr = 0; uVirtqNbr < pThis->cVirtqs; uVirtqNbr++)
        {
            PVIRTIONETVIRTQ pVirtq = &pThis->aVirtqs[uVirtqNbr];
            if (pVirtq->fHasWorker)
            {
                if (!virtioCoreR3VirtqIsEnabled(&pThis->Virtio, uVirtqNbr))
                {
                    virtioNetR3VirtqDestroy(&pThis->Virtio, pVirtq);
                    ++cVirtqsToRemove;
                }
                else
                {
                    if (virtioCoreR3VirtqIsAttached(&pThis->Virtio, uVirtqNbr))
                    {
                        Log7Func(("[%s] Waking %s worker.\n", pThis->szInst, pVirtq->szName));
                        rc = PDMDevHlpSUPSemEventSignal(pDevIns, pThis->aWorkers[pVirtq->uIdx].hEvtProcess);
                        AssertRCReturn(rc, rc);
                    }
                }
            }
        }
        AssertMsg(cVirtqsToRemove < 2, ("Multiple unusable queues in saved state unexpected\n"));
        pThis->cVirtqs -= cVirtqsToRemove;

        pThis->virtioNetConfig.uStatus = pThis->Virtio.fDeviceStatus;
        pThis->fVirtioReady = pThis->Virtio.fDeviceStatus & VIRTIO_STATUS_DRIVER_OK;

        rc = pHlp->pfnSSMGetMem(pSSM, pThis->virtioNetConfig.uMacAddress.au8, sizeof(pThis->virtioNetConfig.uMacAddress));
        AssertRCReturn(rc, rc);

        if (uVersion > VIRTIONET_SAVEDSTATE_VERSION_3_1_BETA1_LEGACY)
        {
            /* Zero-out the the Unicast/Multicast filter table */
            memset(&pThis->aMacUnicastFilter[0], 0, VIRTIONET_MAC_FILTER_LEN * sizeof(RTMAC));

            rc = pHlp->pfnSSMGetU8( pSSM, &pThis->fPromiscuous);
            AssertRCReturn(rc, rc);
            rc = pHlp->pfnSSMGetU8( pSSM, &pThis->fAllMulticast);
            AssertRCReturn(rc, rc);
            /*
             * The 0.95 legacy virtio spec defines a control queue command VIRTIO_NET_CTRL_MAC_TABLE_SET,
             * wherein guest driver configures two variable length mac filter tables: A unicast filter,
             * and a multicast filter. However original VBox virtio-net saved both sets of filter entries
             * in a single table, abandoning the distinction between unicast and multicast filters. It preserved
             * only *one* filter's table length, leaving no way to separate table back out into respective unicast
             * and multicast tables this device implementation preserves. Deduced from legacy code, the original
             * assumption was that the both MAC filters are whitelists that can be processed identically
             * (from the standpoint of a *single* host receiver), such that the distinction between unicast and
             * multicast doesn't matter in any one VM's context. Little choice here but to save the undifferentiated
             * unicast & multicast MACs to the unicast filter table and leave multicast table empty/unused.
             */
            uint32_t cCombinedUnicastMulticastEntries;
            rc = pHlp->pfnSSMGetU32(pSSM, &cCombinedUnicastMulticastEntries);
            AssertRCReturn(rc, rc);
            AssertReturn(cCombinedUnicastMulticastEntries <= VIRTIONET_MAC_FILTER_LEN, VERR_OUT_OF_RANGE);
            pThis->cUnicastFilterMacs = cCombinedUnicastMulticastEntries;
            rc = pHlp->pfnSSMGetMem(pSSM, pThis->aMacUnicastFilter, cCombinedUnicastMulticastEntries * sizeof(RTMAC));
            AssertRCReturn(rc, rc);
            rc = pHlp->pfnSSMGetMem(pSSM, pThis->aVlanFilter, sizeof(pThis->aVlanFilter));
            AssertRCReturn(rc, rc);
        }
        else
        {
            pThis->fAllMulticast = false;
            pThis->cUnicastFilterMacs = 0;
            memset(&pThis->aMacUnicastFilter, 0, VIRTIONET_MAC_FILTER_LEN * sizeof(RTMAC));

            memset(pThis->aVlanFilter, 0, sizeof(pThis->aVlanFilter));

            pThis->fPromiscuous = true;
            if (pThisCC->pDrv)
                pThisCC->pDrv->pfnSetPromiscuousMode(pThisCC->pDrv, true);
        }

        /*
         * Log the restored VirtIO feature selection.
         */
        pThis->fNegotiatedFeatures = virtioCoreGetNegotiatedFeatures(&pThis->Virtio);
        /** @todo shouldn't we update the virtio header size here? it depends on the negotiated features. */
        virtioCorePrintDeviceFeatures(&pThis->Virtio, NULL, s_aDevSpecificFeatures, RT_ELEMENTS(s_aDevSpecificFeatures));

        /*
         * Configure remaining transitional device parameters presumably or deductively
         * as these weren't part of the legacy device code thus it didn't save them to SSM
         */
        pThis->fCableConnected = 1;
        pThis->fAllUnicast     = 0;
        pThis->fNoMulticast    = 0;
        pThis->fNoUnicast      = 0;
        pThis->fNoBroadcast    = 0;

        /* Zero out the multicast table and count, all MAC filters, if any, are in the unicast filter table */
        pThis->cMulticastFilterMacs = 0;
        memset(&pThis->aMacMulticastFilter, 0, VIRTIONET_MAC_FILTER_LEN * sizeof(RTMAC));
    }
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 *
 * @note: This loads state saved by a Modern (VirtIO 1.0+) device, of which this transitional device is one,
 *        and thus supports both legacy and modern guest virtio drivers.
 */
static DECLCALLBACK(int) virtioNetR3ModernLoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PVIRTIONET     pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);
    PVIRTIONETCC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIONETCC);
    PCPDMDEVHLPR3  pHlp    = pDevIns->pHlpR3;
    int rc;

    RT_NOREF(pThisCC);

    RTMAC uMacLoaded, uVersionMarkerMac = { VIRTIONET_VERSION_MARKER_MAC_ADDR };
    rc = pHlp->pfnSSMGetMem(pSSM, &uMacLoaded.au8, sizeof(uMacLoaded.au8));
    AssertRCReturn(rc, rc);
    if (memcmp(&uMacLoaded.au8, uVersionMarkerMac.au8, sizeof(uVersionMarkerMac.au8)))
    {
        rc = virtioNetR3LegacyDeviceLoadExec(pDevIns, pSSM, uVersion, uPass, uMacLoaded);
        return rc;
    }

    Log7Func(("[%s] LOAD EXEC!!\n", pThis->szInst));

    AssertReturn(uPass == SSM_PASS_FINAL, VERR_SSM_UNEXPECTED_PASS);
    AssertLogRelMsgReturn(uVersion == VIRTIONET_SAVEDSTATE_VERSION,
                          ("uVersion=%u\n", uVersion), VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION);

    virtioNetR3SetVirtqNames(pThis, false /* fLegacy */);

    pHlp->pfnSSMGetU64(     pSSM, &pThis->fNegotiatedFeatures);

    pHlp->pfnSSMGetU16(     pSSM, &pThis->cVirtqs);
    AssertReturn(pThis->cVirtqs <= (VIRTIONET_MAX_QPAIRS * 2) + 1, VERR_OUT_OF_RANGE);
    pHlp->pfnSSMGetU16(     pSSM, &pThis->cWorkers);
    AssertReturn(pThis->cWorkers <= VIRTIONET_MAX_WORKERS , VERR_OUT_OF_RANGE);

    for (int uVirtqNbr = 0; uVirtqNbr < pThis->cVirtqs; uVirtqNbr++)
            pHlp->pfnSSMGetBool(pSSM, &pThis->aVirtqs[uVirtqNbr].fAttachedToVirtioCore);

    /* Config checks */
    RTMAC macConfigured;
    rc = pHlp->pfnSSMGetMem(pSSM, &macConfigured.au8, sizeof(macConfigured.au8));
    AssertRCReturn(rc, rc);
    if (memcmp(&macConfigured.au8, &pThis->macConfigured.au8, sizeof(macConfigured.au8))
        && (uPass == 0 || !PDMDevHlpVMTeleportedAndNotFullyResumedYet(pDevIns)))
            LogRel(("%s: The mac address differs: config=%RTmac saved=%RTmac\n",
                    pThis->szInst, &pThis->macConfigured, &macConfigured));
    memcpy(pThis->virtioNetConfig.uMacAddress.au8, macConfigured.au8, sizeof(macConfigured.au8));

#if FEATURE_OFFERED(STATUS)
    uint16_t fChkStatus;
    pHlp->pfnSSMGetU16(     pSSM, &fChkStatus);
    if (fChkStatus == 0xffff)
    {
        /* Dummy value in saved state because status feature wasn't enabled at the time */
        pThis->virtioNetConfig.uStatus = 0; /* VIRTIO_NET_S_ANNOUNCE disabled */
        pThis->virtioNetConfig.uStatus = !!IS_LINK_UP(pThis); /* VIRTIO_NET_IS_LINK_UP (bit 0) */
    }
    else
        pThis->virtioNetConfig.uStatus = fChkStatus;
#else
    uint16_t fDiscard;
    pHlp->pfnSSMGetU16(     pSSM, &fDiscard);
#endif

#if FEATURE_OFFERED(MQ)
    uint16_t uCheckMaxVirtqPairs;
    pHlp->pfnSSMGetU16(     pSSM, &uCheckMaxVirtqPairs);
    if (uCheckMaxVirtqPairs)
        pThis->virtioNetConfig.uMaxVirtqPairs = uCheckMaxVirtqPairs;
    else
        pThis->virtioNetConfig.uMaxVirtqPairs = VIRTIONET_CTRL_MQ_VQ_PAIRS;
#else
    uint16_t fDiscard;
    pHlp->pfnSSMGetU16(     pSSM, &fDiscard);
#endif

    /* Save device-specific part */
    pHlp->pfnSSMGetBool(    pSSM, &pThis->fCableConnected);
    pHlp->pfnSSMGetU8(      pSSM, &pThis->fPromiscuous);
    pHlp->pfnSSMGetU8(      pSSM, &pThis->fAllMulticast);
    pHlp->pfnSSMGetU8(      pSSM, &pThis->fAllUnicast);
    pHlp->pfnSSMGetU8(      pSSM, &pThis->fNoMulticast);
    pHlp->pfnSSMGetU8(      pSSM, &pThis->fNoUnicast);
    pHlp->pfnSSMGetU8(      pSSM, &pThis->fNoBroadcast);

    pHlp->pfnSSMGetU32(     pSSM, &pThis->cMulticastFilterMacs);
    AssertReturn(pThis->cMulticastFilterMacs <= VIRTIONET_MAC_FILTER_LEN, VERR_OUT_OF_RANGE);
    pHlp->pfnSSMGetMem(     pSSM, pThis->aMacMulticastFilter, pThis->cMulticastFilterMacs * sizeof(RTMAC));

    if (pThis->cMulticastFilterMacs < VIRTIONET_MAC_FILTER_LEN)
        memset(&pThis->aMacMulticastFilter[pThis->cMulticastFilterMacs], 0,
               (VIRTIONET_MAC_FILTER_LEN - pThis->cMulticastFilterMacs) * sizeof(RTMAC));

    pHlp->pfnSSMGetU32(     pSSM, &pThis->cUnicastFilterMacs);
    AssertReturn(pThis->cUnicastFilterMacs <= VIRTIONET_MAC_FILTER_LEN, VERR_OUT_OF_RANGE);
    pHlp->pfnSSMGetMem(     pSSM, pThis->aMacUnicastFilter, pThis->cUnicastFilterMacs * sizeof(RTMAC));

    if (pThis->cUnicastFilterMacs < VIRTIONET_MAC_FILTER_LEN)
        memset(&pThis->aMacUnicastFilter[pThis->cUnicastFilterMacs], 0,
               (VIRTIONET_MAC_FILTER_LEN - pThis->cUnicastFilterMacs) * sizeof(RTMAC));

    rc = pHlp->pfnSSMGetMem(pSSM, pThis->aVlanFilter, sizeof(pThis->aVlanFilter));
    AssertRCReturn(rc, rc);
    /*
     * Call the virtio core to let it load its state.
     */
    rc = virtioCoreR3ModernDeviceLoadExec(&pThis->Virtio, pDevIns->pHlpR3, pSSM, uVersion,
                                          VIRTIONET_SAVEDSTATE_VERSION, pThis->cVirtqs);
    AssertRCReturn(rc, rc);
    /*
     * Since the control queue is created proactively in the constructor to accomodate worst-case
     * legacy guests, even though the queue may have been deducted from queue count while saving state,
     * we must explicitly remove queue and associated worker thread and context at this point,
     * or presence of bogus control queue will confuse operations.
     */
    PVIRTIONETVIRTQ pVirtq = &pThis->aVirtqs[CTRLQIDX];
    if (FEATURE_DISABLED(CTRL_VQ) || !virtioCoreIsVirtqEnabled(&pThis->Virtio, CTRLQIDX))
    {
        virtioCoreR3VirtqDetach(&pThis->Virtio, CTRLQIDX);
        virtioNetR3VirtqDestroy(&pThis->Virtio, pVirtq);
        pVirtq->fAttachedToVirtioCore = false;
        --pThis->cWorkers;
    }
    /*
     * Nudge queue workers
     */
    for (int uVirtqNbr = 0; uVirtqNbr < pThis->cVirtqs; uVirtqNbr++)
    {
        pVirtq  = &pThis->aVirtqs[uVirtqNbr];
        if (pVirtq->fAttachedToVirtioCore)
        {
            if (pVirtq->fHasWorker)
            {
                PVIRTIONETWORKER pWorker = &pThis->aWorkers[uVirtqNbr];
                Log7Func(("[%s] Waking %s worker.\n", pThis->szInst, pVirtq->szName));
                rc = PDMDevHlpSUPSemEventSignal(pDevIns, pWorker->hEvtProcess);
                AssertRCReturn(rc, rc);
            }
        }
    }
    pThis->virtioNetConfig.uStatus = pThis->Virtio.fDeviceStatus; /* reflects state to guest driver */
    pThis->fVirtioReady = pThis->Virtio.fDeviceStatus & VIRTIO_STATUS_DRIVER_OK;
    virtioNetConfigurePktHdr(pThis, pThis->Virtio.fLegacyDriver);
    return rc;
}

/**
 * @callback_method_impl{FNSSMDEVLOADDONE, Link status adjustments after
 *                      loading.}
 */
static DECLCALLBACK(int) virtioNetR3ModernLoadDone(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PVIRTIONET     pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);
    PVIRTIONETCC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIONETCC);
    RT_NOREF(pSSM);

    if (pThisCC->pDrv)
        pThisCC->pDrv->pfnSetPromiscuousMode(pThisCC->pDrv, (pThis->fPromiscuous | pThis->fAllMulticast));

    /*
     * Indicate link down to the guest OS that all network connections have
     * been lost, unless we've been teleported here.
     */
    if (!PDMDevHlpVMTeleportedAndNotFullyResumedYet(pDevIns))
        virtioNetR3TempLinkDown(pDevIns, pThis, pThisCC);

    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) virtioNetR3ModernSaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PVIRTIONET     pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);
    PVIRTIONETCC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIONETCC);
    PCPDMDEVHLPR3  pHlp    = pDevIns->pHlpR3;

    RT_NOREF(pThisCC);
    Log7Func(("[%s] SAVE EXEC!!\n", pThis->szInst));

    /* Store a dummy MAC address that would never be actually assigned to a NIC
     * so that when load exec handler is called it can be easily determined
     * whether saved state is modern or legacy. This works because original
     * legacy code stored assigned NIC address as the first item of SSM state
     */
    RTMAC uVersionMarkerMac = { VIRTIONET_VERSION_MARKER_MAC_ADDR };
    pHlp->pfnSSMPutMem(pSSM, &uVersionMarkerMac.au8, sizeof(uVersionMarkerMac.au8));

    pHlp->pfnSSMPutU64(     pSSM, pThis->fNegotiatedFeatures);

    pHlp->pfnSSMPutU16(     pSSM, pThis->cVirtqs);
    pHlp->pfnSSMPutU16(     pSSM, pThis->cWorkers);

    for (int uVirtqNbr = 0; uVirtqNbr < pThis->cVirtqs; uVirtqNbr++)
        pHlp->pfnSSMPutBool(pSSM, pThis->aVirtqs[uVirtqNbr].fAttachedToVirtioCore);
    /*

     * Save device config area (accessed via MMIO)
     */
    pHlp->pfnSSMPutMem(     pSSM, pThis->virtioNetConfig.uMacAddress.au8,
                            sizeof(pThis->virtioNetConfig.uMacAddress.au8));
#if FEATURE_OFFERED(STATUS)
    pHlp->pfnSSMPutU16(     pSSM, pThis->virtioNetConfig.uStatus);
#else
    /*
     * Relevant values are lower bits. Forcing this to 0xffff let's loadExec know this
     * feature was not enabled in saved state. VirtIO 1.0, 5.1.4
     */
    pHlp->pfnSSMPutU16(     pSSM, 0xffff);

#endif
#if FEATURE_OFFERED(MQ)
    pHlp->pfnSSMPutU16(     pSSM, pThis->virtioNetConfig.uMaxVirtqPairs);
#else
    /*
     * Legal values for max_virtqueue_pairs are 0x1 -> 0x8000 *. Forcing zero let's loadExec know this
     * feature was not enabled in saved state. VirtIO 1.0, 5.1.4.1
     */
    pHlp->pfnSSMPutU16(     pSSM, 0);
#endif

    /* Save device-specific part */
    pHlp->pfnSSMPutBool(    pSSM, pThis->fCableConnected);
    pHlp->pfnSSMPutU8(      pSSM, pThis->fPromiscuous);
    pHlp->pfnSSMPutU8(      pSSM, pThis->fAllMulticast);
    pHlp->pfnSSMPutU8(      pSSM, pThis->fAllUnicast);
    pHlp->pfnSSMPutU8(      pSSM, pThis->fNoMulticast);
    pHlp->pfnSSMPutU8(      pSSM, pThis->fNoUnicast);
    pHlp->pfnSSMPutU8(      pSSM, pThis->fNoBroadcast);

    pHlp->pfnSSMPutU32(     pSSM, pThis->cMulticastFilterMacs);
    pHlp->pfnSSMPutMem(     pSSM, pThis->aMacMulticastFilter, pThis->cMulticastFilterMacs * sizeof(RTMAC));

    pHlp->pfnSSMPutU32(     pSSM, pThis->cUnicastFilterMacs);
    pHlp->pfnSSMPutMem(     pSSM, pThis->aMacUnicastFilter, pThis->cUnicastFilterMacs * sizeof(RTMAC));

    int rc = pHlp->pfnSSMPutMem(pSSM, pThis->aVlanFilter, sizeof(pThis->aVlanFilter));
    AssertRCReturn(rc, rc);

    /*
     * Call the virtio core to let it save its state.
     */
    return virtioCoreR3SaveExec(&pThis->Virtio, pDevIns->pHlpR3, pSSM, VIRTIONET_SAVEDSTATE_VERSION, pThis->cVirtqs);
}


/*********************************************************************************************************************************
*   Device interface.                                                                                                            *
*********************************************************************************************************************************/

#ifdef IN_RING3

#ifdef VIRTIO_REL_INFO_DUMP
/**
 * Attempt to recover from the state when a descriptor loop is discovered in adapter queue.
 * The idea is to force device reset via setting the DEVICE_NEEDS_RESET bit in the device status field.
 * To prevent further accesses to the queue in invalid state we need to bring down the link temporarily.
 * Unfortunately it does not seem to be handled by Linux virtio at all, so the faulty queue will remain
 * disabled, preventing recovery.
 *
 * @param pDevIns               PDM instance
 *
 */
DECLHIDDEN(int) virtioNetAttemptToRecover(PPDMDEVINS pDevIns)
{
    PVIRTIONET     pThis   = PDMDEVINS_2_DATA(pDevIns,  PVIRTIONET);
    PVIRTIONETCC   pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIONETCC);

    if (IS_LINK_UP(pThis))
    {
        SET_LINK_DOWN(pThis);

        /* Re-establish link in 10 seconds. */
        int rc = PDMDevHlpTimerSetMillies(pDevIns, pThisCC->hLinkUpTimer, 10000);
        AssertRC(rc);

        LogRel(("[%s] Virtio disabled until device reset\n", pThis->szInst));
        pThis->fVirtioReady = false;
    }
    virtioCoreResetAll(&pThis->Virtio);

    if (ASMAtomicXchgBool(&pThis->Virtio.fRecovering, true))
        LogRel(("[%s] Attempting recovery while in recovery!\n", pThis->szInst));

    return VINF_SUCCESS;
}
#endif /* VIRTIO_REL_INFO_DUMP */

/**
 * Perform 16-bit 1's compliment checksum on provided packet in accordance with VirtIO specification,
 * pertinent to VIRTIO_NET_F_CSUM feature, which 'offloads' the Checksum feature from the driver
 * to save processor cycles, which is ironic in our case, where the controller device ('network card')
 * is emulated on the virtualization host.
 *
 * @note See VirtIO 1.0 spec, 5.1.6.2 Packet Transmission
 *
 * @param pBuf          Pointer to r/w buffer with any portion to calculate checksum for
 * @param cbSize        Number of bytes to checksum
 * @param uStart        Where to start the checksum within the buffer
 * @param uOffset       Offset past uStart point in the buffer to store checksum result
 *
 */
DECLINLINE(void) virtioNetR3Calc16BitChecksum(uint8_t *pBuf, size_t cb, uint16_t uStart, uint16_t uOffset)
{
    AssertReturnVoid(uStart < cb);
    AssertReturnVoid(uStart + uOffset + sizeof(uint16_t) <= cb);

    uint32_t  chksum = 0;
    uint16_t *pu = (uint16_t *)(pBuf + uStart);

    cb -= uStart;
    while (cb > 1)
    {
        chksum += *pu++;
        cb -= 2;
    }
    if (cb)
        chksum += *(uint8_t *)pu;
    while (chksum >> 16)
        chksum = (chksum >> 16) + (chksum & 0xFFFF);

    /* Store 1's compliment of calculated sum */
    *(uint16_t *)(pBuf + uStart + uOffset) = ~chksum;
}

/**
 * Turns on/off the read status LED.
 *
 * @returns VBox status code.
 * @param   pThis          Pointer to the device state structure.
 * @param   fOn             New LED state.
 */
static void virtioNetR3SetReadLed(PVIRTIONETR3 pThisR3, bool fOn)
{
    if (fOn)
        pThisR3->led.Asserted.s.fReading = pThisR3->led.Actual.s.fReading = 1;
    else
        pThisR3->led.Actual.s.fReading = fOn;
}

/**
 * Turns on/off the write status LED.
 *
 * @returns VBox status code.
 * @param   pThis          Pointer to the device state structure.
 * @param   fOn            New LED state.
 */
static void virtioNetR3SetWriteLed(PVIRTIONETR3 pThisR3, bool fOn)
{
    if (fOn)
        pThisR3->led.Asserted.s.fWriting = pThisR3->led.Actual.s.fWriting = 1;
    else
        pThisR3->led.Actual.s.fWriting = fOn;
}

/**
 * Check that the core is setup and ready and co-configured with guest virtio driver,
 * and verifies that the VM is running.
 *
 * @returns true if VirtIO core and device are in a running and operational state
 */
DECLINLINE(bool) virtioNetIsOperational(PVIRTIONET pThis, PPDMDEVINS pDevIns)
{
    if (RT_LIKELY(pThis->fVirtioReady))
    {
        VMSTATE enmVMState = PDMDevHlpVMState(pDevIns);
        if (RT_LIKELY(enmVMState == VMSTATE_RUNNING || enmVMState == VMSTATE_RUNNING_LS))
            return true;
    }
    return false;
}

/**
 * Check whether specific queue is ready and has Rx buffers (virtqueue descriptors)
 * available. This must be called before the pfnRecieve() method is called.
 *
 * @remarks As a side effect this function enables queue notification
 *          if it cannot receive because the queue is empty.
 *          It disables notification if it can receive.
 *
 * @returns VERR_NET_NO_BUFFER_SPACE if it cannot.
 * @thread  RX
 */
static int virtioNetR3CheckRxBufsAvail(PPDMDEVINS pDevIns, PVIRTIONET pThis, PVIRTIONETVIRTQ pRxVirtq)
{
    int rc = VERR_INVALID_STATE;
    Log8Func(("[%s] ", pThis->szInst));
    if (!virtioNetIsOperational(pThis, pDevIns))
        Log8(("No Rx bufs available. (VirtIO core not ready)\n"));

    else if (!virtioCoreIsVirtqEnabled(&pThis->Virtio, pRxVirtq->uIdx))
        Log8(("[No Rx bufs available. (%s not enabled)\n", pRxVirtq->szName));

    else if (IS_VIRTQ_EMPTY(pDevIns, &pThis->Virtio,  pRxVirtq->uIdx))
        Log8(("No Rx bufs available. (%s empty)\n", pRxVirtq->szName));

    else
    {
        Log8(("%s has %d empty guest bufs in avail ring\n", pRxVirtq->szName,
                virtioCoreVirtqAvailBufCount(pDevIns,  &pThis->Virtio, pRxVirtq->uIdx)));
        rc = VINF_SUCCESS;
    }
#ifdef VIRTIO_REL_INFO_DUMP
    if (ASMAtomicReadBool(&pThis->Virtio.fRecovering))
        LogRel(("[%s] %sRX buffers available on %s\n", pThis->szInst, rc == VERR_INVALID_STATE ? "Enabling notify as no " : "", pRxVirtq->szName));
#endif /* VIRTIO_REL_INFO_DUMP */
    virtioCoreVirtqEnableNotify(&pThis->Virtio, pRxVirtq->uIdx, rc == VERR_INVALID_STATE /* fEnable */);
    return rc;
}

/**
 * Find an Rx queue that has Rx packets in it, if *any* do.
 *
 * @todo When multiqueue (MQ) mode is fully supported and tested, some kind of round-robin
 *       or randomization scheme should probably be incorporated here.
 *
 * @returns true if Rx pkts avail on queue and sets pRxVirtq to point to queue w/pkts found
 * @thread  RX
 *
 */
static bool virtioNetR3AreRxBufsAvail(PPDMDEVINS pDevIns, PVIRTIONET pThis, PVIRTIONETVIRTQ *pRxVirtq)
{
    for (int uVirtqPair = 0; uVirtqPair < pThis->cVirtqPairs; uVirtqPair++)
    {
        PVIRTIONETVIRTQ pThisRxVirtq = &pThis->aVirtqs[RXQIDX(uVirtqPair)];
        if (RT_SUCCESS(virtioNetR3CheckRxBufsAvail(pDevIns, pThis, pThisRxVirtq)))
        {
            if (pRxVirtq)
                *pRxVirtq = pThisRxVirtq;
            return true;
        }
    }
    return false;
}

/**
 * @interface_method_impl{PDMINETWORKDOWN,pfnWaitReceiveAvail}
 */
static DECLCALLBACK(int) virtioNetR3NetworkDown_WaitReceiveAvail(PPDMINETWORKDOWN pInterface, RTMSINTERVAL timeoutMs)
{
    PVIRTIONETCC pThisCC = RT_FROM_MEMBER(pInterface, VIRTIONETCC, INetworkDown);
    PPDMDEVINS   pDevIns = pThisCC->pDevIns;
    PVIRTIONET   pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);

    if (!virtioNetIsOperational(pThis, pDevIns))
        return VERR_INTERRUPTED;

    if (virtioNetR3AreRxBufsAvail(pDevIns, pThis, NULL /* pRxVirtq */))
    {
        Log10Func(("[%s] Rx bufs available, releasing waiter...\n", pThis->szInst));
        return VINF_SUCCESS;
    }
    if (!timeoutMs)
        return VERR_NET_NO_BUFFER_SPACE;

    LogFunc(("[%s] %s\n", pThis->szInst, timeoutMs == RT_INDEFINITE_WAIT ? "<indefinite wait>" : ""));

    ASMAtomicXchgBool(&pThis->fLeafWantsEmptyRxBufs, true);
    STAM_PROFILE_START(&pThis->StatRxOverflow, a);

    do {
        if (virtioNetR3AreRxBufsAvail(pDevIns, pThis, NULL /* pRxVirtq */))
        {
            Log10Func(("[%s] Rx bufs now available, releasing waiter...\n", pThis->szInst));
            ASMAtomicXchgBool(&pThis->fLeafWantsEmptyRxBufs, false);
            return VINF_SUCCESS;
        }
        Log9Func(("[%s] Starved for empty guest Rx bufs. Waiting...\n", pThis->szInst));

        int rc = PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pThis->hEventRxDescAvail, timeoutMs);

        if (rc == VERR_TIMEOUT || rc == VERR_INTERRUPTED)
        {
            LogFunc(("Woken due to %s\n", rc == VERR_TIMEOUT ? "timeout" : "getting interrupted"));

            if (!virtioNetIsOperational(pThis, pDevIns))
                break;

            continue;
        }
        if (RT_FAILURE(rc)) {
            LogFunc(("Waken due to failure %Rrc\n", rc));
            RTThreadSleep(1);
        }
    } while (virtioNetIsOperational(pThis, pDevIns));

    STAM_PROFILE_STOP(&pThis->StatRxOverflow, a);
    ASMAtomicXchgBool(&pThis->fLeafWantsEmptyRxBufs, false);

    Log7Func(("[%s] Wait for Rx buffers available was interrupted\n", pThis->szInst));
    return VERR_INTERRUPTED;
}

/**
 * Sets up the GSO context according to the Virtio header.
 *
 * @param   pGso                The GSO context to setup.
 * @param   pCtx                The context descriptor.
 */
DECLINLINE(PPDMNETWORKGSO) virtioNetR3SetupGsoCtx(PPDMNETWORKGSO pGso, VIRTIONETPKTHDR const *pPktHdr)
{
    pGso->u8Type = PDMNETWORKGSOTYPE_INVALID;

    if (pPktHdr->uGsoType & VIRTIONET_HDR_GSO_ECN)
    {
        AssertMsgFailed(("Unsupported flag in virtio header: ECN\n"));
        return NULL;
    }
    switch (pPktHdr->uGsoType & ~VIRTIONET_HDR_GSO_ECN)
    {
        case VIRTIONET_HDR_GSO_TCPV4:
            pGso->u8Type = PDMNETWORKGSOTYPE_IPV4_TCP;
            pGso->cbHdrsSeg = pPktHdr->uHdrLen;
            break;
        case VIRTIONET_HDR_GSO_TCPV6:
            pGso->u8Type = PDMNETWORKGSOTYPE_IPV6_TCP;
            pGso->cbHdrsSeg = pPktHdr->uHdrLen;
            break;
        case VIRTIONET_HDR_GSO_UDP:
            pGso->u8Type = PDMNETWORKGSOTYPE_IPV4_UDP;
            pGso->cbHdrsSeg = pPktHdr->uChksumStart;
            break;
        default:
            return NULL;
    }
    if (pPktHdr->uFlags & VIRTIONET_HDR_F_NEEDS_CSUM)
        pGso->offHdr2  = pPktHdr->uChksumStart;
    else
    {
        AssertMsgFailed(("GSO without checksum offloading!\n"));
        return NULL;
    }
    pGso->offHdr1     = sizeof(RTNETETHERHDR);
    pGso->cbHdrsTotal = pPktHdr->uHdrLen;
    pGso->cbMaxSeg    = pPktHdr->uGsoSize;
    /* Mark GSO frames with zero MSS as PDMNETWORKGSOTYPE_INVALID, so they will be ignored by send. */
    if (pPktHdr->uGsoType != VIRTIONET_HDR_GSO_NONE && pPktHdr->uGsoSize == 0)
        pGso->u8Type = PDMNETWORKGSOTYPE_INVALID;
    return pGso;
}

/**
 * @interface_method_impl{PDMINETWORKCONFIG,pfnGetMac}
 */
static DECLCALLBACK(int) virtioNetR3NetworkConfig_GetMac(PPDMINETWORKCONFIG pInterface, PRTMAC pMac)
{
    PVIRTIONETCC    pThisCC = RT_FROM_MEMBER(pInterface, VIRTIONETCC, INetworkConfig);
    PVIRTIONET      pThis   = PDMDEVINS_2_DATA(pThisCC->pDevIns, PVIRTIONET);
    memcpy(pMac, pThis->virtioNetConfig.uMacAddress.au8, sizeof(RTMAC));
    return VINF_SUCCESS;
}

/**
 * Returns true if it is a broadcast packet.
 *
 * @returns true if destination address indicates broadcast.
 * @param   pvBuf           The ethernet packet.
 */
DECLINLINE(bool) virtioNetR3IsBroadcast(const void *pvBuf)
{
    static const uint8_t s_abBcastAddr[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    return memcmp(pvBuf, s_abBcastAddr, sizeof(s_abBcastAddr)) == 0;
}

/**
 * Returns true if it is a multicast packet.
 *
 * @remarks returns true for broadcast packets as well.
 * @returns true if destination address indicates multicast.
 * @param   pvBuf           The ethernet packet.
 */
DECLINLINE(bool) virtioNetR3IsMulticast(const void *pvBuf)
{
    return (*(char*)pvBuf) & 1;
}

/**
 * Determines if the packet is to be delivered to upper layer.
 *
 * @returns true if packet is intended for this node.
 * @param   pThis          Pointer to the state structure.
 * @param   pvBuf          The ethernet packet.
 * @param   cb             Number of bytes available in the packet.
 */
static bool virtioNetR3AddressFilter(PVIRTIONET pThis, const void *pvBuf, size_t cb)
{

RT_NOREF(cb);

#ifdef LOG_ENABLED
    if (LogIs11Enabled())
    {
        char *pszType;
        if (virtioNetR3IsMulticast(pvBuf))
            pszType = (char *)"mcast";
        else if (virtioNetR3IsBroadcast(pvBuf))
            pszType = (char *)"bcast";
        else
            pszType = (char *)"ucast";

        LogFunc(("node(%RTmac%s%s), pkt(%RTmac, %s) ",
                 pThis->virtioNetConfig.uMacAddress.au8,
                 pThis->fPromiscuous ? " promisc" : "",
                 pThis->fAllMulticast ? " all-mcast" : "",
                 pvBuf,  pszType));
    }
#endif

    if (pThis->fPromiscuous) {
        Log11(("\n"));
        return true;
    }

    /* Ignore everything outside of our VLANs */
    uint16_t *uPtr = (uint16_t *)pvBuf;

    /* Compare TPID with VLAN Ether Type */
    if (   uPtr[6] == RT_H2BE_U16(0x8100)
        && !ASMBitTest(pThis->aVlanFilter, RT_BE2H_U16(uPtr[7]) & 0xFFF))
    {
        Log11Func(("\n[%s] not our VLAN, returning false\n", pThis->szInst));
        return false;
    }

    if (virtioNetR3IsBroadcast(pvBuf))
    {
        Log11(("acpt (bcast)\n"));
#ifdef LOG_ENABLED
        if (LogIs12Enabled())
            virtioNetR3PacketDump(pThis, (const uint8_t *)pvBuf, cb, "<-- Incoming");
#endif
        return true;
    }
    if (pThis->fAllMulticast && virtioNetR3IsMulticast(pvBuf))
    {
        Log11(("acpt (all-mcast)\n"));
#ifdef LOG_ENABLED
        if (LogIs12Enabled())
            virtioNetR3PacketDump(pThis, (const uint8_t *)pvBuf, cb, "<-- Incoming");
#endif
        return true;
    }

    if (!memcmp(pThis->virtioNetConfig.uMacAddress.au8, pvBuf, sizeof(RTMAC)))
    {
        Log11(("acpt (to-node)\n"));
#ifdef LOG_ENABLED
        if (LogIs12Enabled())
            virtioNetR3PacketDump(pThis, (const uint8_t *)pvBuf, cb, "<-- Incoming");
#endif
        return true;
    }

    for (uint16_t i = 0; i < pThis->cMulticastFilterMacs; i++)
    {
        if (!memcmp(&pThis->aMacMulticastFilter[i], pvBuf, sizeof(RTMAC)))
        {
            Log11(("acpt (mcast whitelist)\n"));
#ifdef LOG_ENABLED
            if (LogIs12Enabled())
                virtioNetR3PacketDump(pThis, (const uint8_t *)pvBuf, cb, "<-- Incoming");
#endif
            return true;
        }
    }

    for (uint16_t i = 0; i < pThis->cUnicastFilterMacs; i++)
        if (!memcmp(&pThis->aMacUnicastFilter[i], pvBuf, sizeof(RTMAC)))
        {
            Log11(("acpt (ucast whitelist)\n"));
            return true;
        }
#ifdef LOG_ENABLED
    if (LogIs11Enabled())
        Log(("... reject\n"));
#endif

    return false;
}


/**
 * This handles the case where Rx packet must be transfered to guest driver via multiple buffers using
 * copy tactics slower than preferred method using a single virtq buf. Yet this is an available option
 * for guests. Although cited in the spec it's to accomodate guest that perhaps have memory constraints
 * wherein guest may benefit from smaller buffers (see MRG_RXBUF feature), in practice it is seen
 * that without MRG_RXBUF the linux guest enqueues 'huge' multi-segment buffers so that the largest
 * conceivable Rx packet can be contained in a single buffer, where for most transactions most of that
 * memory will be unfilled, so it is typically both wasteful and *slower* to avoid MRG_RXBUF.
 *
 * As an optimization, this multi-buffer copy is only used when:
 *
 *     A. Guest has negotiated MRG_RXBUF
 *     B. Next packet in the Rx avail queue isn't big enough to contain Rx pkt hdr+data.
 *
 * Architecture is defined in VirtIO 1.1 5.1.6 (Device Operations), which has improved
 * wording over the VirtIO 1.0 specification, but, as an implementation note, there is one
 * ambiguity that needs clarification:
 *
 *    The VirtIO 1.1, 5.1.6.4 explains something in a potentially misleading way. And note,
 *    the VirtIO spec makes a document-wide assertion that the distinction between
 *    "SHOULD" and "MUST" is to be taken quite literally.
 *
 *    The confusion is that VirtIO 1.1, 5.1.6.3.1 essentially says guest driver "SHOULD" populate
 *    Rx queue with buffers large enough to accomodate full pkt hdr + data. That's a grammatical
 *    error (dangling participle).
 *
 *    In practice we MUST assume "SHOULD" strictly applies to the word *populate*, -not- to buffer
 *    size, because ultimately buffer minimum size is predicated on configuration parameters,
 *    specifically, when MRG_RXBUF feature is disabled, the driver *MUST* provide Rx bufs
 *    (if and when it can provide them), that are *large enough* to hold pkt hdr + payload.
 *
 *    Therefore, proper interpretation of 5.1.6.3.1 is, the guest *should* (ideally) keep Rx virtq
 *    populated with appropriately sized buffers to *prevent starvation* (i.e. starvation may be
 *    unavoidable thus can't be prohibited). As it would be a ludicrous to presume 5.1.6.3.1 is
 *    giving guests leeway to violate MRG_RXBUF feature buf size constraints.
 *
 * @param pDevIns               PDM instance
 * @param pThis                 Device instance
 * @param pvBuf                 Pointer to incoming GSO Rx data from downstream device
 * @param cb                    Amount of data given
 * @param rxPktHdr              Rx pkt Header that's been massaged into VirtIO semantics
 * @param pRxVirtq              Pointer to Rx virtq
 * @param pVirtqBuf             Initial virtq buffer to start copying Rx hdr/pkt to guest into
 *
 */
static int virtioNetR3RxPktMultibufXfer(PPDMDEVINS pDevIns, PVIRTIONET pThis, uint8_t *pvPktBuf, size_t cb,
                                        PVIRTIONETPKTHDR pRxPktHdr, PVIRTIONETVIRTQ pRxVirtq, PVIRTQBUF pVirtqBuf)
{

    size_t cbBufRemaining = pVirtqBuf->cbPhysReturn;
    size_t cbPktHdr = pThis->cbPktHdr;

    AssertMsgReturn(cbBufRemaining >= pThis->cbPktHdr,
                    ("guest-provided Rx buf not large enough to store pkt hdr"), VERR_INTERNAL_ERROR);

    Log7Func(("  Sending packet header to guest...\n"));

    /* Copy packet header to rx buf provided by caller. */
    size_t cbHdrEnqueued = pVirtqBuf->cbPhysReturn == cbPktHdr ? cbPktHdr : 0;
    virtioCoreR3VirtqUsedBufPut(pDevIns, &pThis->Virtio, pRxVirtq->uIdx, cbPktHdr, pRxPktHdr, pVirtqBuf, cbHdrEnqueued);

    /* Cache address of uNumBuffers field of pkthdr to update ex post facto */
    RTGCPHYS GCPhysNumBuffers = pVirtqBuf->pSgPhysReturn->paSegs[0].GCPhys + RT_UOFFSETOF(VIRTIONETPKTHDR, uNumBuffers);
    uint16_t cVirtqBufsUsed = 0;
    cbBufRemaining -= cbPktHdr;
    /*
     * Copy packet to guest using as many buffers as necessary, tracking and handling whether
     * the buf containing the packet header was already written to the Rx queue's used buffer ring.
     */
    uint64_t uPktOffset = 0;
    while(uPktOffset < cb)
    {
        Log7Func(("  Sending packet data (in buffer #%d) to guest...\n", cVirtqBufsUsed));
        size_t cbBounded = RT_MIN(cbBufRemaining, cb - uPktOffset);
        (void) virtioCoreR3VirtqUsedBufPut(pDevIns, &pThis->Virtio, pRxVirtq->uIdx, cbBounded,
                                    pvPktBuf + uPktOffset, pVirtqBuf, cbBounded + (cbPktHdr - cbHdrEnqueued) /* cbEnqueue */);
        ++cVirtqBufsUsed;
        cbBufRemaining -= cbBounded;
        uPktOffset += cbBounded;
        if (uPktOffset < cb)
        {
            cbHdrEnqueued = cbPktHdr;
            int rc = virtioCoreR3VirtqAvailBufGet(pDevIns, &pThis->Virtio, pRxVirtq->uIdx, pVirtqBuf, true);
#ifdef VIRTIO_REL_INFO_DUMP
            if (rc == VERR_INVALID_STATE)
                return virtioNetAttemptToRecover(pDevIns);
            if (rc == VERR_NOT_AVAILABLE)
                return VINF_SUCCESS;
#endif /* VIRTIO_REL_INFO_DUMP */
            AssertMsgReturn(rc == VINF_SUCCESS || rc == VERR_NOT_AVAILABLE, ("%Rrc\n", rc), rc);
            AssertMsgReturn(rc == VINF_SUCCESS && pVirtqBuf->cbPhysReturn,
                            ("Not enough Rx buffers in queue to accomodate ethernet packet\n"),
                            VERR_INTERNAL_ERROR);
            cbBufRemaining = pVirtqBuf->cbPhysReturn;
        }
    }

    /* Fix-up pkthdr (in guest phys. memory) with number of buffers (descriptors) that were processed */
    int rc = virtioCoreGCPhysWrite(&pThis->Virtio, pDevIns, GCPhysNumBuffers, &cVirtqBufsUsed, sizeof(cVirtqBufsUsed));
    AssertMsgRCReturn(rc, ("Failure updating descriptor count in pkt hdr in guest physical memory\n"), rc);

    virtioCoreVirtqUsedRingSync(pDevIns, &pThis->Virtio, pRxVirtq->uIdx);
    Log7(("\n"));
    return rc;
}

/**
 * Pad and store received packet.
 *
 * @remarks Make sure that the packet appears to upper layer as one coming
 *          from real Ethernet: pad it and insert FCS.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pThis           The virtio-net shared instance data.
 * @param   pvBuf           The available data.
 * @param   cb              Number of bytes available in the buffer.
 * @param   pGso            Pointer to Global Segmentation Offload structure
 * @param   pRxVirtq        Pointer to Rx virtqueue
 * @thread  RX
 */

static int virtioNetR3CopyRxPktToGuest(PPDMDEVINS pDevIns, PVIRTIONET pThis, PVIRTIONETCC pThisCC, const void *pvBuf, size_t cb,
                                       PVIRTIONETPKTHDR pRxPktHdr, uint8_t cbPktHdr, PVIRTIONETVIRTQ pRxVirtq)
{
    RT_NOREF(pThisCC);
    VIRTQBUF_T VirtqBuf;

    VirtqBuf.u32Magic  = VIRTQBUF_MAGIC;
    VirtqBuf.cRefs     = 1;

    PVIRTQBUF pVirtqBuf = &VirtqBuf;
    int rc = virtioCoreR3VirtqAvailBufGet(pDevIns, &pThis->Virtio, pRxVirtq->uIdx, pVirtqBuf, true);
#ifdef VIRTIO_REL_INFO_DUMP
    if (rc == VERR_INVALID_STATE)
        return virtioNetAttemptToRecover(pDevIns);
    if (rc == VERR_NOT_AVAILABLE)
        return VINF_SUCCESS;
#endif /* VIRTIO_REL_INFO_DUMP */
    AssertMsgReturn(rc == VINF_SUCCESS || rc == VERR_NOT_AVAILABLE, ("%Rrc\n", rc), rc);
    AssertMsgReturn(rc == VINF_SUCCESS && pVirtqBuf->cbPhysReturn,
                    ("Not enough Rx buffers or capacity to accommodate ethernet packet\n"),
                    VERR_INTERNAL_ERROR);

    /*
     * Try to do fast (e.g. single-buffer) copy to guest, even if MRG_RXBUF feature is enabled
     */
    STAM_PROFILE_START(&pThis->StatReceiveStore, a);
    if (RT_LIKELY(FEATURE_DISABLED(MRG_RXBUF))
        || RT_LIKELY(pVirtqBuf->cbPhysReturn > cb + cbPktHdr))
    {
        Log7Func(("Send Rx packet header and data to guest (single-buffer copy)...\n"));
        pRxPktHdr->uNumBuffers = 1;
        rc = virtioCoreR3VirtqUsedBufPut(pDevIns, &pThis->Virtio, pRxVirtq->uIdx, cbPktHdr,  pRxPktHdr, pVirtqBuf, 0  /* cbEnqueue */);
        if (rc == VINF_SUCCESS)
            rc = virtioCoreR3VirtqUsedBufPut(pDevIns, &pThis->Virtio, pRxVirtq->uIdx, cb, pvBuf, pVirtqBuf, cbPktHdr + cb /* cbEnqueue */);
        virtioCoreVirtqUsedRingSync(pDevIns, &pThis->Virtio, pRxVirtq->uIdx);
        AssertMsgReturn(rc == VINF_SUCCESS, ("%Rrc\n", rc), rc);
    }
    else
    {
        Log7Func(("Send Rx pkt to guest (merged-buffer copy [MRG_RXBUF feature])...\n"));
        rc = virtioNetR3RxPktMultibufXfer(pDevIns, pThis, (uint8_t *)pvBuf, cb, pRxPktHdr, pRxVirtq, pVirtqBuf);
        return rc;
    }
    STAM_PROFILE_STOP(&pThis->StatReceiveStore, a);
#ifdef VIRTIO_REL_INFO_DUMP
    if (ASMAtomicXchgBool(&pThis->Virtio.fRecovering, false))
        LogRel(("[%s] Recovery complete on successfully receiving a packet\n", pThis->szInst));
#endif /* VIRTIO_REL_INFO_DUMP */
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMINETWORKDOWN,pfnReceiveGso}
 */
static DECLCALLBACK(int) virtioNetR3NetworkDown_ReceiveGso(PPDMINETWORKDOWN pInterface, const void *pvBuf, size_t cb,
                                                           PCPDMNETWORKGSO pGso)
{
    PVIRTIONETCC    pThisCC  = RT_FROM_MEMBER(pInterface, VIRTIONETCC, INetworkDown);
    PPDMDEVINS      pDevIns  = pThisCC->pDevIns;
    PVIRTIONET      pThis    = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);
    VIRTIONETPKTHDR rxPktHdr = { 0, VIRTIONET_HDR_GSO_NONE, 0, 0, 0, 0, 0 };

    if (!pThis->fVirtioReady)
    {
        LogRelFunc(("VirtIO not ready, aborting downstream receive\n"));
        return VERR_INTERRUPTED;
    }
    /*
     * If GSO (Global Segment Offloading) was received from downstream PDM network device, massage the
     * PDM-provided GSO parameters into VirtIO semantics, which get passed to guest virtio-net via
     * Rx pkt header.  See VirtIO 1.1, 5.1.6 Device Operation for more information.
     */
    if (pGso)
    {
        LogFunc(("[%s] (%RTmac) \n", pThis->szInst, pvBuf));

        rxPktHdr.uFlags        = VIRTIONET_HDR_F_NEEDS_CSUM;
        rxPktHdr.uHdrLen       = pGso->cbHdrsTotal;
        rxPktHdr.uGsoSize      = pGso->cbMaxSeg;
        rxPktHdr.uChksumStart  = pGso->offHdr2;

        switch (pGso->u8Type)
        {
            case PDMNETWORKGSOTYPE_IPV4_TCP:
                rxPktHdr.uGsoType = VIRTIONET_HDR_GSO_TCPV4;
                rxPktHdr.uChksumOffset = RT_OFFSETOF(RTNETTCP, th_sum);
                break;
            case PDMNETWORKGSOTYPE_IPV6_TCP:
                rxPktHdr.uGsoType = VIRTIONET_HDR_GSO_TCPV6;
                rxPktHdr.uChksumOffset = RT_OFFSETOF(RTNETTCP, th_sum);
                break;
            case PDMNETWORKGSOTYPE_IPV4_UDP:
                rxPktHdr.uGsoType = VIRTIONET_HDR_GSO_UDP;
                rxPktHdr.uChksumOffset = RT_OFFSETOF(RTNETUDP, uh_sum);
                break;
            default:
                LogFunc(("[%s] GSO type (0x%x) not supported\n", pThis->szInst, pGso->u8Type));
                return VERR_NOT_SUPPORTED;
        }
        STAM_REL_COUNTER_INC(&pThis->StatReceiveGSO);
        Log2Func(("[%s] gso type=%#x, cbHdrsTotal=%u cbHdrsSeg=%u mss=%u offHdr1=%#x offHdr2=%#x\n",
                        pThis->szInst, pGso->u8Type, pGso->cbHdrsTotal, pGso->cbHdrsSeg,
                        pGso->cbMaxSeg, pGso->offHdr1, pGso->offHdr2));
    }

    /*
     * Find a virtq with Rx bufs on avail ring, if any, and copy the packet to the guest's Rx buffer.
     * @todo pk: PROBABLY NOT A SOPHISTICATED ENOUGH QUEUE SELECTION ALGORTITH FOR OPTIMAL MQ (FEATURE) SUPPORT
     */
    for (int uVirtqPair = 0; uVirtqPair < pThis->cVirtqPairs; uVirtqPair++)
    {
        PVIRTIONETVIRTQ pRxVirtq = &pThis->aVirtqs[RXQIDX(uVirtqPair)];
        if (RT_SUCCESS(virtioNetR3CheckRxBufsAvail(pDevIns, pThis, pRxVirtq)))
        {
            int rc = VINF_SUCCESS;
            STAM_PROFILE_START(&pThis->StatReceive, a);
            virtioNetR3SetReadLed(pThisCC, true);
            if (virtioNetR3AddressFilter(pThis, pvBuf, cb))
            {
                /* rxPktHdr is local stack variable that should not go out of scope in this use */
                rc = virtioNetR3CopyRxPktToGuest(pDevIns, pThis, pThisCC, pvBuf, cb, &rxPktHdr, pThis->cbPktHdr, pRxVirtq);
                STAM_REL_COUNTER_ADD(&pThis->StatReceiveBytes, cb);
            }
            virtioNetR3SetReadLed(pThisCC, false);
            STAM_PROFILE_STOP(&pThis->StatReceive, a);
            return rc;
        }
    }
    return VERR_INTERRUPTED;
}

/**
 * @interface_method_impl{PDMINETWORKDOWN,pfnReceive}
 */
static DECLCALLBACK(int) virtioNetR3NetworkDown_Receive(PPDMINETWORKDOWN pInterface, const void *pvBuf, size_t cb)
{

#ifdef LOG_ENABLED
    PVIRTIONETCC    pThisCC  = RT_FROM_MEMBER(pInterface, VIRTIONETCC, INetworkDown);
    PPDMDEVINS      pDevIns  = pThisCC->pDevIns;
    PVIRTIONET      pThis    = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);
    LogFunc(("[%s] (%RTmac)\n", pThis->szInst, pvBuf));
#endif

    return virtioNetR3NetworkDown_ReceiveGso(pInterface, pvBuf, cb, NULL);
}

/*
 * Dispatched to here from virtioNetR3Ctrl() to configure this virtio-net device's Rx packet receive filtering.
 * See VirtIO 1.0, 5.1.6.5.1
 *
 * @param pThis         virtio-net instance
 * @param pCtrlPktHdr   Control packet header (which includes command parameters)
 * @param pVirtqBuf     Buffer from ctrlq buffer (contains command data)
 */
static uint8_t virtioNetR3CtrlRx(PVIRTIONET pThis, PVIRTIONETCC pThisCC,
                                 PVIRTIONET_CTRL_HDR_T pCtrlPktHdr, PVIRTQBUF pVirtqBuf)
{

#define LOG_VIRTIONET_FLAG(fld) LogFunc(("[%s] Setting %s=%d\n", pThis->szInst, #fld, pThis->fld))

    LogFunc(("[%s] Processing CTRL Rx command\n", pThis->szInst));
    switch(pCtrlPktHdr->uCmd)
    {
      case VIRTIONET_CTRL_RX_PROMISC:
        break;
      case VIRTIONET_CTRL_RX_ALLMULTI:
        break;
      case VIRTIONET_CTRL_RX_ALLUNI:
        /* fallthrough */
      case VIRTIONET_CTRL_RX_NOMULTI:
        /* fallthrough */
      case VIRTIONET_CTRL_RX_NOUNI:
        /* fallthrough */
      case VIRTIONET_CTRL_RX_NOBCAST:
        AssertMsgReturn(FEATURE_ENABLED(CTRL_RX_EXTRA),
                        ("CTRL 'extra' cmd w/o VIRTIONET_F_CTRL_RX_EXTRA feature negotiated - skipping\n"),
                        VIRTIONET_ERROR);
        /* fall out */
    }

    uint8_t fOn, fPromiscChanged = false;
    virtioCoreR3VirtqBufDrain(&pThis->Virtio, pVirtqBuf, &fOn, (size_t)RT_MIN(pVirtqBuf->cbPhysSend, sizeof(fOn)));

    switch(pCtrlPktHdr->uCmd)
    {
      case VIRTIONET_CTRL_RX_PROMISC:
        pThis->fPromiscuous = RT_BOOL(fOn);
        fPromiscChanged = true;
        LOG_VIRTIONET_FLAG(fPromiscuous);
        break;
      case VIRTIONET_CTRL_RX_ALLMULTI:
        pThis->fAllMulticast = RT_BOOL(fOn);
        fPromiscChanged = true;
        LOG_VIRTIONET_FLAG(fAllMulticast);
        break;
      case VIRTIONET_CTRL_RX_ALLUNI:
        pThis->fAllUnicast = RT_BOOL(fOn);
        LOG_VIRTIONET_FLAG(fAllUnicast);
        break;
      case VIRTIONET_CTRL_RX_NOMULTI:
        pThis->fNoMulticast = RT_BOOL(fOn);
        LOG_VIRTIONET_FLAG(fNoMulticast);
        break;
      case VIRTIONET_CTRL_RX_NOUNI:
        pThis->fNoUnicast = RT_BOOL(fOn);
        LOG_VIRTIONET_FLAG(fNoUnicast);
        break;
      case VIRTIONET_CTRL_RX_NOBCAST:
        pThis->fNoBroadcast = RT_BOOL(fOn);
        LOG_VIRTIONET_FLAG(fNoBroadcast);
        break;
    }

    if (pThisCC->pDrv && fPromiscChanged)
        pThisCC->pDrv->pfnSetPromiscuousMode(pThisCC->pDrv, (pThis->fPromiscuous || pThis->fAllMulticast));

    return VIRTIONET_OK;
}

/*
 * Dispatched to here from virtioNetR3Ctrl() to configure this virtio-net device's MAC filter tables
 * See VirtIO 1.0, 5.1.6.5.2
 *
 * @param pThis         virtio-net instance
 * @param pCtrlPktHdr   Control packet header (which includes command parameters)
 * @param pVirtqBuf     Buffer from ctrlq buffer (contains command data)
 */
static uint8_t virtioNetR3CtrlMac(PVIRTIONET pThis, PVIRTIONET_CTRL_HDR_T pCtrlPktHdr, PVIRTQBUF pVirtqBuf)
{
    LogFunc(("[%s] Processing CTRL MAC command\n", pThis->szInst));


    AssertMsgReturn(pVirtqBuf->cbPhysSend >= sizeof(*pCtrlPktHdr),
                   ("insufficient descriptor space for ctrl pkt hdr"),
                   VIRTIONET_ERROR);

    size_t cbRemaining = pVirtqBuf->cbPhysSend;
    switch(pCtrlPktHdr->uCmd)
    {
        case VIRTIONET_CTRL_MAC_ADDR_SET:
        {
            /* Set default Rx filter MAC */
            AssertMsgReturn(cbRemaining >= sizeof(pThis->rxFilterMacDefault),
                            ("DESC chain too small to process CTRL_MAC_ADDR_SET cmd\n"), VIRTIONET_ERROR);

            virtioCoreR3VirtqBufDrain(&pThis->Virtio, pVirtqBuf, &pThis->rxFilterMacDefault, sizeof(RTMAC));
            break;
        }
        case VIRTIONET_CTRL_MAC_TABLE_SET:
        {
            VIRTIONET_CTRL_MAC_TABLE_LEN cMacs;

            /* Load unicast MAC filter table */
            AssertMsgReturn(cbRemaining >= sizeof(cMacs),
                           ("DESC chain too small to process CTRL_MAC_TABLE_SET cmd\n"), VIRTIONET_ERROR);

            /* Fetch count of unicast filter MACs from guest buffer */
            virtioCoreR3VirtqBufDrain(&pThis->Virtio, pVirtqBuf, &cMacs, sizeof(cMacs));
            cbRemaining -= sizeof(cMacs);

            Log7Func(("[%s] Guest provided %d unicast MAC Table entries\n", pThis->szInst, cMacs));

            AssertMsgReturn(cMacs <= RT_ELEMENTS(pThis->aMacUnicastFilter),
                            ("Guest provided Unicast MAC filter table exceeds hardcoded table size"), VIRTIONET_ERROR);

            if (cMacs)
            {
                uint32_t cbMacs = cMacs * sizeof(RTMAC);

                AssertMsgReturn(cbRemaining >= cbMacs,
                                ("Virtq buffer too small to process CTRL_MAC_TABLE_SET cmd\n"), VIRTIONET_ERROR);


                /* Fetch unicast table contents from guest buffer */
                virtioCoreR3VirtqBufDrain(&pThis->Virtio, pVirtqBuf, &pThis->aMacUnicastFilter, cbMacs);
                cbRemaining -= cbMacs;
            }
            pThis->cUnicastFilterMacs = cMacs;

            /* Load multicast MAC filter table */
            AssertMsgReturn(cbRemaining >= sizeof(cMacs),
                            ("Virtq buffer too small to process CTRL_MAC_TABLE_SET cmd\n"), VIRTIONET_ERROR);

            /* Fetch count of multicast filter MACs from guest buffer */
            virtioCoreR3VirtqBufDrain(&pThis->Virtio, pVirtqBuf, &cMacs, sizeof(cMacs));
            cbRemaining -= sizeof(cMacs);

            Log10Func(("[%s] Guest provided %d multicast MAC Table entries\n", pThis->szInst, cMacs));

            AssertMsgReturn(cMacs <= RT_ELEMENTS(pThis->aMacMulticastFilter),
                            ("Guest provided Unicast MAC filter table exceeds hardcoded table size"), VIRTIONET_ERROR);

            if (cMacs)
            {
                uint32_t cbMacs = cMacs * sizeof(RTMAC);

                AssertMsgReturn(cbRemaining >= cbMacs,
                                ("Virtq buffer too small to process CTRL_MAC_TABLE_SET cmd\n"), VIRTIONET_ERROR);

                /* Fetch multicast table contents from guest buffer */
                virtioCoreR3VirtqBufDrain(&pThis->Virtio, pVirtqBuf, &pThis->aMacMulticastFilter, cbMacs);
                cbRemaining -= cbMacs;
            }
            pThis->cMulticastFilterMacs = cMacs;

#ifdef LOG_ENABLED
            LogFunc(("[%s] unicast MACs:\n", pThis->szInst));
            for(unsigned i = 0; i < pThis->cUnicastFilterMacs; i++)
                LogFunc(("         %RTmac\n", &pThis->aMacUnicastFilter[i]));

            LogFunc(("[%s] multicast MACs:\n", pThis->szInst));
            for(unsigned i = 0; i < pThis->cMulticastFilterMacs; i++)
                LogFunc(("         %RTmac\n", &pThis->aMacMulticastFilter[i]));
#endif
            break;
        }
        default:
            LogRelFunc(("Unrecognized MAC subcommand in CTRL pkt from guest\n"));
            return VIRTIONET_ERROR;
    }
    return VIRTIONET_OK;
}

/*
 * Dispatched to here from virtioNetR3Ctrl() to configure this virtio-net device's MQ (multiqueue) operations.
 * See VirtIO 1.0, 5.1.6.5.5
 *
 * @param pThis         virtio-net instance
 * @param pCtrlPktHdr   Control packet header (which includes command parameters)
 * @param pVirtqBuf     Buffer from ctrlq buffer (contains command data)
 */
static uint8_t virtioNetR3CtrlMultiQueue(PVIRTIONET pThis, PVIRTIONETCC pThisCC, PPDMDEVINS pDevIns, PVIRTIONET_CTRL_HDR_T pCtrlPktHdr, PVIRTQBUF pVirtqBuf)
{
    LogFunc(("[%s] Processing CTRL MQ command\n", pThis->szInst));

    uint16_t cVirtqPairs;
    switch(pCtrlPktHdr->uCmd)
    {
        case VIRTIONET_CTRL_MQ_VQ_PAIRS_SET:
        {
            size_t cbRemaining = pVirtqBuf->cbPhysSend;

            AssertMsgReturn(cbRemaining >= sizeof(cVirtqPairs),
                ("DESC chain too small for VIRTIONET_CTRL_MQ cmd processing"), VIRTIONET_ERROR);

            /* Fetch number of virtq pairs from guest buffer */
            virtioCoreR3VirtqBufDrain(&pThis->Virtio, pVirtqBuf, &cVirtqPairs, sizeof(cVirtqPairs));

            AssertMsgReturn(cVirtqPairs <= VIRTIONET_MAX_QPAIRS,
                ("[%s] Guest CTRL MQ virtq pair count out of range [%d])\n", pThis->szInst, cVirtqPairs), VIRTIONET_ERROR);

            LogFunc(("[%s] Guest specifies %d VQ pairs in use\n", pThis->szInst, cVirtqPairs));
            pThis->cVirtqPairs = cVirtqPairs;
            break;
        }
        default:
            LogRelFunc(("Unrecognized multiqueue subcommand in CTRL pkt from guest\n"));
            return VIRTIONET_ERROR;
    }

    /*
     * The MQ control function is invoked by the guest in an RPC like manner to change
     * the Rx/Tx queue pair count. If the new value exceeds the number of queues
     * (and associated workers) already initialized initialize only the new queues and
     * respective workers.
     */
    if (pThis->cVirtqPairs > pThis->cInitializedVirtqPairs)
    {
        virtioNetR3SetVirtqNames(pThis, virtioCoreIsLegacyMode(&pThis->Virtio));
        int rc = virtioNetR3CreateWorkerThreads(pDevIns, pThis, pThisCC);
        if (RT_FAILURE(rc))
        {
            LogRelFunc(("Failed to create worker threads\n"));
            return VIRTIONET_ERROR;
        }
    }
    return VIRTIONET_OK;
}

/*
 * Dispatched to here from virtioNetR3Ctrl() to configure this virtio-net device's VLAN filtering.
 * See VirtIO 1.0, 5.1.6.5.3
 *
 * @param pThis         virtio-net instance
 * @param pCtrlPktHdr   Control packet header (which includes command parameters)
 * @param pVirtqBuf     Buffer from ctrlq buffer (contains command data)
 */
static uint8_t virtioNetR3CtrlVlan(PVIRTIONET pThis, PVIRTIONET_CTRL_HDR_T pCtrlPktHdr, PVIRTQBUF pVirtqBuf)
{
    LogFunc(("[%s] Processing CTRL VLAN command\n", pThis->szInst));

    uint16_t uVlanId;
    size_t cbRemaining = pVirtqBuf->cbPhysSend;

    AssertMsgReturn(cbRemaining >= sizeof(uVlanId),
        ("DESC chain too small for VIRTIONET_CTRL_VLAN cmd processing"), VIRTIONET_ERROR);

    /* Fetch VLAN ID from guest buffer */
    virtioCoreR3VirtqBufDrain(&pThis->Virtio, pVirtqBuf, &uVlanId, sizeof(uVlanId));

    AssertMsgReturn(uVlanId < VIRTIONET_MAX_VLAN_ID,
        ("%s VLAN ID out of range (VLAN ID=%u)\n", pThis->szInst, uVlanId), VIRTIONET_ERROR);

    LogFunc(("[%s] uCommand=%u VLAN ID=%u\n", pThis->szInst, pCtrlPktHdr->uCmd, uVlanId));

    switch (pCtrlPktHdr->uCmd)
    {
        case VIRTIONET_CTRL_VLAN_ADD:
            ASMBitSet(pThis->aVlanFilter, uVlanId);
            break;
        case VIRTIONET_CTRL_VLAN_DEL:
            ASMBitClear(pThis->aVlanFilter, uVlanId);
            break;
        default:
            LogRelFunc(("Unrecognized VLAN subcommand in CTRL pkt from guest\n"));
            return VIRTIONET_ERROR;
    }
    return VIRTIONET_OK;
}

/**
 * Processes control command from guest.
 * See VirtIO 1.0 spec, 5.1.6 "Device Operation" and 5.1.6.5 "Control Virtqueue".
 *
 * The control command is contained in a virtio buffer pulled from the virtio-net defined control queue (ctrlq).
 * Command type is parsed is dispatched to a command-specific device-configuration handler function (e.g. RX, MAC, VLAN, MQ
 * and ANNOUNCE).
 *
 * This function handles all parts of the host-side of the ctrlq round-trip buffer processing.
 *
 * Invoked by worker for virtio-net control queue to process a queued control command buffer.
 *
 * @param pDevIns       PDM device instance
 * @param pThis         virtio-net device instance
 * @param pThisCC       virtio-net device instance
 * @param pVirtqBuf     pointer to buffer pulled from virtq (input to this function)
 */
static void virtioNetR3Ctrl(PPDMDEVINS pDevIns, PVIRTIONET pThis, PVIRTIONETCC pThisCC,
                            PVIRTQBUF pVirtqBuf)
{
    if (!(pThis->fNegotiatedFeatures & VIRTIONET_F_CTRL_VQ))
        LogFunc(("[%s] WARNING: Guest using CTRL queue w/o negotiating VIRTIONET_F_CTRL_VQ feature\n", pThis->szInst));

    LogFunc(("[%s] Received CTRL packet from guest\n", pThis->szInst));

    if (pVirtqBuf->cbPhysSend < 2)
    {
        LogFunc(("[%s] CTRL packet from guest driver incomplete. Skipping ctrl cmd\n", pThis->szInst));
        return;
    }
    else if (pVirtqBuf->cbPhysReturn < sizeof(VIRTIONET_CTRL_HDR_T_ACK))
    {
        LogFunc(("[%s] Guest driver didn't allocate memory to receive ctrl pkt ACK. Skipping ctrl cmd\n", pThis->szInst));
        return;
    }

    /*
     * Allocate buffer and read in the control command
     */
    VIRTIONET_CTRL_HDR_T CtrlPktHdr; RT_ZERO(CtrlPktHdr);
    AssertLogRelMsgReturnVoid(pVirtqBuf->cbPhysSend >= sizeof(CtrlPktHdr),
                              ("DESC chain too small for CTRL pkt header"));
    virtioCoreR3VirtqBufDrain(&pThis->Virtio, pVirtqBuf, &CtrlPktHdr, sizeof(CtrlPktHdr));

    Log7Func(("[%s] CTRL COMMAND: class=%d command=%d\n", pThis->szInst, CtrlPktHdr.uClass, CtrlPktHdr.uCmd));

    uint8_t uAck;
    switch (CtrlPktHdr.uClass)
    {
        case VIRTIONET_CTRL_RX:
            uAck = virtioNetR3CtrlRx(pThis, pThisCC, &CtrlPktHdr, pVirtqBuf);
            break;
        case VIRTIONET_CTRL_MAC:
            uAck = virtioNetR3CtrlMac(pThis, &CtrlPktHdr, pVirtqBuf);
            break;
        case VIRTIONET_CTRL_VLAN:
            uAck = virtioNetR3CtrlVlan(pThis, &CtrlPktHdr, pVirtqBuf);
            break;
        case VIRTIONET_CTRL_MQ:
            uAck = virtioNetR3CtrlMultiQueue(pThis, pThisCC, pDevIns, &CtrlPktHdr, pVirtqBuf);
            break;
        case VIRTIONET_CTRL_ANNOUNCE:
            uAck = VIRTIONET_OK;
            if (FEATURE_DISABLED(STATUS) || FEATURE_DISABLED(GUEST_ANNOUNCE))
            {
                LogFunc(("%s Ignoring CTRL class VIRTIONET_CTRL_ANNOUNCE.\n"
                         "VIRTIO_F_STATUS or VIRTIO_F_GUEST_ANNOUNCE feature not enabled\n", pThis->szInst));
                break;
            }
            if (CtrlPktHdr.uCmd != VIRTIONET_CTRL_ANNOUNCE_ACK)
            {
                LogFunc(("[%s] Ignoring CTRL class VIRTIONET_CTRL_ANNOUNCE. Unrecognized uCmd\n", pThis->szInst));
                break;
            }
#if FEATURE_OFFERED(STATUS)
            pThis->virtioNetConfig.uStatus &= ~VIRTIONET_F_ANNOUNCE;
#endif
            Log7Func(("[%s] Clearing VIRTIONET_F_ANNOUNCE in config status\n", pThis->szInst));
            break;
        default:
            LogRelFunc(("Unrecognized CTRL pkt hdr class (%d)\n", CtrlPktHdr.uClass));
            uAck = VIRTIONET_ERROR;
    }

    /* Return CTRL packet Ack byte (result code) to guest driver */
    RTSGSEG aStaticSegs[] = { { &uAck, sizeof(uAck) } };
    RTSGBUF SgBuf;

    RTSgBufInit(&SgBuf, aStaticSegs, RT_ELEMENTS(aStaticSegs));
    virtioCoreR3VirtqUsedBufPut(pDevIns, &pThis->Virtio, CTRLQIDX, &SgBuf, pVirtqBuf, true /* fFence */);
    virtioCoreVirtqUsedRingSync(pDevIns, &pThis->Virtio, CTRLQIDX);

    LogFunc(("%s Finished processing CTRL command with status %s\n",
             pThis->szInst, uAck == VIRTIONET_OK ? "VIRTIONET_OK" : "VIRTIONET_ERROR"));
}

/**
 * Reads virtio-net pkt header from provided Phy. addr of virtio descriptor chain
 * (e.g. S/G segment from guest-driver provided buffer pulled from Tx virtq)
 * Verifies state and supported modes, sets TCP header size.
 *
 * @param pVirtio      VirtIO core instance data
 * @param pThis        virtio-net instance
 * @param pDevIns      PDM device instance
 * @param GCPhys       Phys. Address from where to read virtio-net pkt header
 * @param pPktHdr      Where to store read Tx pkt hdr (virtio pkt hdr size is determined from instance configuration)
 * @param cbFrame      Total pkt frame size to inform bounds check
 */
static int virtioNetR3ReadVirtioTxPktHdr(PVIRTIOCORE pVirtio, PVIRTIONET pThis, PPDMDEVINS pDevIns, RTGCPHYS GCPhys, PVIRTIONETPKTHDR pPktHdr, size_t cbFrame)
{
    int rc = virtioCoreGCPhysRead(pVirtio, pDevIns, GCPhys, pPktHdr, pThis->cbPktHdr);
    if (RT_FAILURE(rc))
        return rc;

    LogFunc(("pktHdr (flags=%x gso-type=%x len=%x gso-size=%x Chksum-start=%x Chksum-offset=%x) cbFrame=%d\n",
          pPktHdr->uFlags, pPktHdr->uGsoType, pPktHdr->uHdrLen,
          pPktHdr->uGsoSize, pPktHdr->uChksumStart, pPktHdr->uChksumOffset, cbFrame));

    if (pPktHdr->uGsoType)
    {
        /* Segmentation offloading cannot be done without checksumming, and we do not support ECN */
        AssertMsgReturn(    RT_LIKELY(pPktHdr->uFlags & VIRTIONET_HDR_F_NEEDS_CSUM)
                         && !(RT_UNLIKELY(pPktHdr->uGsoType & VIRTIONET_HDR_GSO_ECN)),
                         ("Unsupported ECN request in pkt header\n"), VERR_NOT_SUPPORTED);

        uint32_t uTcpHdrSize;
        switch (pPktHdr->uGsoType)
        {
            case VIRTIONET_HDR_GSO_TCPV4:
            case VIRTIONET_HDR_GSO_TCPV6:
                uTcpHdrSize = sizeof(RTNETTCP);
                break;
            case VIRTIONET_HDR_GSO_UDP:
                uTcpHdrSize = 0;
                break;
            default:
                LogFunc(("Bad GSO type in packet header\n"));
                return VERR_INVALID_PARAMETER;
        }
        /* Header + MSS must not exceed the packet size. */
        AssertMsgReturn(RT_LIKELY(uTcpHdrSize + pPktHdr->uChksumStart + pPktHdr->uGsoSize <= cbFrame),
                    ("Header plus message exceeds packet size"), VERR_BUFFER_OVERFLOW);
    }

    AssertMsgReturn( !(pPktHdr->uFlags & VIRTIONET_HDR_F_NEEDS_CSUM)
                    || sizeof(uint16_t) + pPktHdr->uChksumStart + pPktHdr->uChksumOffset <= cbFrame,
                 ("Checksum (%d bytes) doesn't fit into pkt header (%d bytes)\n",
                 sizeof(uint16_t) + pPktHdr->uChksumStart + pPktHdr->uChksumOffset, cbFrame),
                 VERR_BUFFER_OVERFLOW);

    return VINF_SUCCESS;
}

/**
 * Transmits single GSO frame via PDM framework to downstream PDM device, to emit from virtual NIC.
 *
 * This does final prep of GSO parameters including checksum calculation if configured
 * (e.g. if VIRTIONET_HDR_F_NEEDS_CSUM flag is set).
 *
 * @param pThis         virtio-net instance
 * @param pThisCC       virtio-net instance
 * @param pSgBuf        PDM S/G buffer containing pkt and hdr to transmit
 * @param pGso          GSO parameters used for the packet
 * @param pPktHdr       virtio-net pkt header to adapt to PDM semantics
 */
static int virtioNetR3TransmitFrame(PVIRTIONET pThis, PVIRTIONETCC pThisCC, PPDMSCATTERGATHER pSgBuf,
                                    PPDMNETWORKGSO pGso, PVIRTIONETPKTHDR pPktHdr)
{

    virtioNetR3PacketDump(pThis, (uint8_t *)pSgBuf->aSegs[0].pvSeg, pSgBuf->cbUsed, "--> Outgoing");
    if (pGso)
    {
        /* Some guests (RHEL) may report HdrLen excluding transport layer header!
         * Thus cannot use cdHdrs provided by the guest because of different ways
         * it gets filled out by different versions of kernels. */
        Log4Func(("%s HdrLen before adjustment %d.\n", pThis->szInst, pGso->cbHdrsTotal));
        switch (pGso->u8Type)
        {
            case PDMNETWORKGSOTYPE_IPV4_TCP:
            case PDMNETWORKGSOTYPE_IPV6_TCP:
                pGso->cbHdrsTotal = pPktHdr->uChksumStart +
                    ((PRTNETTCP)(((uint8_t*)pSgBuf->aSegs[0].pvSeg) + pPktHdr->uChksumStart))->th_off * 4;
                AssertMsgReturn(pSgBuf->cbUsed > pGso->cbHdrsTotal,
                    ("cbHdrsTotal exceeds size of frame"), VERR_BUFFER_OVERFLOW);
                pGso->cbHdrsSeg   = pGso->cbHdrsTotal;
                break;
            case PDMNETWORKGSOTYPE_IPV4_UDP:
                pGso->cbHdrsTotal = (uint8_t)(pPktHdr->uChksumStart + sizeof(RTNETUDP));
                pGso->cbHdrsSeg = pPktHdr->uChksumStart;
                break;
            case PDMNETWORKGSOTYPE_INVALID:
                LogFunc(("%s ignoring invalid GSO frame\n", pThis->szInst));
                return VERR_INVALID_PARAMETER;
        }
        /* Update GSO structure embedded into the frame */
        ((PPDMNETWORKGSO)pSgBuf->pvUser)->cbHdrsTotal = pGso->cbHdrsTotal;
        ((PPDMNETWORKGSO)pSgBuf->pvUser)->cbHdrsSeg   = pGso->cbHdrsSeg;
        Log4Func(("%s adjusted HdrLen to %d.\n",
              pThis->szInst, pGso->cbHdrsTotal));
        Log2Func(("%s gso type=%x cbHdrsTotal=%u cbHdrsSeg=%u mss=%u off1=0x%x off2=0x%x\n",
                  pThis->szInst, pGso->u8Type, pGso->cbHdrsTotal, pGso->cbHdrsSeg,
                  pGso->cbMaxSeg, pGso->offHdr1, pGso->offHdr2));
        STAM_REL_COUNTER_INC(&pThis->StatTransmitGSO);
    }
    else if (pPktHdr->uFlags & VIRTIONET_HDR_F_NEEDS_CSUM)
    {
        STAM_REL_COUNTER_INC(&pThis->StatTransmitCSum);
        /*
         * This is not GSO frame but checksum offloading is requested.
         */
        virtioNetR3Calc16BitChecksum((uint8_t*)pSgBuf->aSegs[0].pvSeg, pSgBuf->cbUsed,
                             pPktHdr->uChksumStart, pPktHdr->uChksumOffset);
    }

    return pThisCC->pDrv->pfnSendBuf(pThisCC->pDrv, pSgBuf, true /* fOnWorkerThread */);
}

/**
 * Non-reentrant function transmits all available packets from specified Tx virtq to downstream
 * PDM device (if cable is connected). For each Tx pkt, virtio-net pkt header is converted
 * to required GSO information (VBox host network stack semantics)
 *
 * @param pDevIns           PDM device instance
 * @param pThis             virtio-net device instance
 * @param pThisCC           virtio-net device instance
 * @param pTxVirtq          Address of transmit virtq
 * @param fOnWorkerThread   Flag to PDM whether to use caller's or or PDM transmit worker's thread.
 */
static int virtioNetR3TransmitPkts(PPDMDEVINS pDevIns, PVIRTIONET pThis, PVIRTIONETCC pThisCC,
                                   PVIRTIONETVIRTQ pTxVirtq, bool fOnWorkerThread)
{
    PVIRTIOCORE pVirtio = &pThis->Virtio;


    if (!pThis->fVirtioReady)
    {
        LogFunc(("%s Ignoring Tx requests. VirtIO not ready (status=0x%x)\n",
                pThis->szInst, pThis->virtioNetConfig.uStatus));
        return VERR_IGNORED;
    }

    if (!pThis->fCableConnected)
    {
        Log(("[%s] Ignoring transmit requests while cable is disconnected.\n", pThis->szInst));
        return VERR_IGNORED;
    }

    /*
     * Only one thread is allowed to transmit at a time, others should skip transmission as the packets
     * will be picked up by the transmitting thread.
     */
    if (!ASMAtomicCmpXchgU32(&pThis->uIsTransmitting, 1, 0))
        return VERR_IGNORED;

    PPDMINETWORKUP pDrv = pThisCC->pDrv;
    if (pDrv)
    {
        int rc = pDrv->pfnBeginXmit(pDrv, fOnWorkerThread);
        Assert(rc == VINF_SUCCESS || rc == VERR_TRY_AGAIN);
        if (rc == VERR_TRY_AGAIN)
        {
            ASMAtomicWriteU32(&pThis->uIsTransmitting, 0);
            return VERR_TRY_AGAIN;
        }
    }
    int cPkts = virtioCoreVirtqAvailBufCount(pVirtio->pDevInsR3, pVirtio, pTxVirtq->uIdx);
    if (!cPkts)
    {
#ifdef VIRTIO_REL_INFO_DUMP
        if (ASMAtomicReadBool(&pThis->Virtio.fRecovering))
            LogRel(("[%s] No packets to send found on %s\n", pThis->szInst, pTxVirtq->szName));
#endif /* VIRTIO_REL_INFO_DUMP */
        LogFunc(("[%s] No packets to send found on %s\n", pThis->szInst, pTxVirtq->szName));

        if (pDrv)
            pDrv->pfnEndXmit(pDrv);

        ASMAtomicWriteU32(&pThis->uIsTransmitting, 0);
        return VERR_MISSING;
    }
#ifdef VIRTIO_REL_INFO_DUMP
    if (ASMAtomicReadBool(&pThis->Virtio.fRecovering))
        LogRel(("[%s] About to transmit %d pending packet%c\n", pThis->szInst, cPkts, cPkts == 1 ? ' ' : 's'));
#endif /* VIRTIO_REL_INFO_DUMP */
    LogFunc(("[%s] About to transmit %d pending packet%c\n", pThis->szInst, cPkts, cPkts == 1 ? ' ' : 's'));

    virtioNetR3SetWriteLed(pThisCC, true);

    /* Disable notifications until all available descriptors have been processed */
    if (!(pVirtio->uDriverFeatures & VIRTIO_F_EVENT_IDX))
        virtioCoreVirtqEnableNotify(&pThis->Virtio, pTxVirtq->uIdx, false /* fEnable */);

    int rc;
    VIRTQBUF_T VirtqBuf;

    VirtqBuf.u32Magic  = VIRTQBUF_MAGIC;
    VirtqBuf.cRefs     = 1;

    PVIRTQBUF pVirtqBuf = &VirtqBuf;
    while ((rc = virtioCoreR3VirtqAvailBufPeek(pVirtio->pDevInsR3, pVirtio, pTxVirtq->uIdx, pVirtqBuf)) == VINF_SUCCESS)
    {
        Log10Func(("[%s] fetched descriptor chain from %s\n", pThis->szInst, pTxVirtq->szName));

        PVIRTIOSGBUF pSgPhysSend = pVirtqBuf->pSgPhysSend;
        PVIRTIOSGSEG paSegsFromGuest = pSgPhysSend->paSegs;
        uint32_t cSegsFromGuest = pSgPhysSend->cSegs;
        size_t uFrameSize = 0;

        AssertMsgReturn(paSegsFromGuest[0].cbSeg >= pThis->cbPktHdr,
                        ("Desc chain's first seg has insufficient space for pkt header!\n"),
                        VERR_INTERNAL_ERROR);

        VIRTIONETPKTHDR PktHdr;
        PVIRTIONETPKTHDR pPktHdr = &PktHdr;

        /* Compute total frame size from guest (including virtio-net pkt hdr) */
        for (unsigned i = 0; i < cSegsFromGuest && uFrameSize < VIRTIONET_MAX_FRAME_SIZE; i++)
            uFrameSize +=  paSegsFromGuest[i].cbSeg;

        Log5Func(("[%s] complete frame is %u bytes.\n", pThis->szInst, uFrameSize));
        Assert(uFrameSize <= VIRTIONET_MAX_FRAME_SIZE);

        /* Truncate oversized frames. */
        if (uFrameSize > VIRTIONET_MAX_FRAME_SIZE)
            uFrameSize = VIRTIONET_MAX_FRAME_SIZE;

        if (pThisCC->pDrv)
        {
            uFrameSize -= pThis->cbPktHdr;
            /*
             * Peel off pkt header and convert to PDM/GSO semantics.
             */
            rc = virtioNetR3ReadVirtioTxPktHdr(pVirtio, pThis, pDevIns, paSegsFromGuest[0].GCPhys, pPktHdr, uFrameSize /* cbFrame */);
            if (RT_FAILURE(rc))
                return rc;
            virtioCoreGCPhysChainAdvance(pSgPhysSend, pThis->cbPktHdr);

            PDMNETWORKGSO  Gso, *pGso = virtioNetR3SetupGsoCtx(&Gso, pPktHdr);

            /* Allocate PDM transmit buffer to send guest provided network frame from to VBox network leaf device */
            PPDMSCATTERGATHER pSgBufToPdmLeafDevice;
            rc = pThisCC->pDrv->pfnAllocBuf(pThisCC->pDrv, uFrameSize, pGso, &pSgBufToPdmLeafDevice);

            /*
             * Copy virtio-net guest S/G buffer to PDM leaf driver S/G buffer
             * converting from GCphys to virt memory at the same time
             */
            if (RT_SUCCESS(rc))
            {
                STAM_REL_COUNTER_INC(&pThis->StatTransmitPackets);
                STAM_PROFILE_START(&pThis->StatTransmitSend, a);

                size_t cbCopied = 0;
                size_t cbRemain = pSgBufToPdmLeafDevice->cbUsed = uFrameSize;
                uint64_t uOffset = 0;
                while (cbRemain)
                {
                    PVIRTIOSGSEG paSeg  = &pSgPhysSend->paSegs[pSgPhysSend->idxSeg];
                    uint64_t srcSgStart = (uint64_t)paSeg->GCPhys;
                    uint64_t srcSgLen   = (uint64_t)paSeg->cbSeg;
                    uint64_t srcSgCur   = (uint64_t)pSgPhysSend->GCPhysCur;
                    cbCopied = RT_MIN((uint64_t)cbRemain, srcSgLen - (srcSgCur - srcSgStart));
                    /*
                     * Guest sent a bogus S/G chain, there doesn't seem to be a way to report an error but
                     * as this shouldn't happen anyway we just stop proccessing this chain.
                     */
                    if (RT_UNLIKELY(!cbCopied))
                        break;
                    virtioCoreGCPhysRead(pVirtio, pDevIns,
                                         (RTGCPHYS)pSgPhysSend->GCPhysCur,
                                         ((uint8_t *)pSgBufToPdmLeafDevice->aSegs[0].pvSeg) + uOffset, cbCopied);
                    virtioCoreGCPhysChainAdvance(pSgPhysSend, cbCopied);
                    cbRemain -= cbCopied;
                    uOffset += cbCopied;
                }

                LogFunc((".... Copied %lu/%lu bytes to %lu byte guest buffer. Buf residual=%lu\n",
                     uOffset, uFrameSize, pVirtqBuf->cbPhysSend, virtioCoreGCPhysChainCalcLengthLeft(pSgPhysSend)));

                rc = virtioNetR3TransmitFrame(pThis, pThisCC, pSgBufToPdmLeafDevice, pGso, pPktHdr);
                if (RT_FAILURE(rc))
                {
                    LogFunc(("[%s] Failed to transmit frame, rc = %Rrc\n", pThis->szInst, rc));
                    STAM_PROFILE_STOP(&pThis->StatTransmitSend, a);
                    STAM_PROFILE_ADV_STOP(&pThis->StatTransmit, a);
                    pThisCC->pDrv->pfnFreeBuf(pThisCC->pDrv, pSgBufToPdmLeafDevice);
                }
                STAM_PROFILE_STOP(&pThis->StatTransmitSend, a);
                STAM_REL_COUNTER_ADD(&pThis->StatTransmitBytes, uOffset);
            }
            else
            {
                Log4Func(("Failed to allocate S/G buffer: frame size=%u rc=%Rrc\n", uFrameSize, rc));
                /* Stop trying to fetch TX descriptors until we get more bandwidth. */
                break;
            }

            virtioCoreR3VirtqAvailBufNext(pVirtio, pTxVirtq->uIdx);

            /* No data to return to guest, but necessary to put elem (e.g. desc chain head idx) on used ring */
            virtioCoreR3VirtqUsedBufPut(pVirtio->pDevInsR3, pVirtio, pTxVirtq->uIdx, NULL, pVirtqBuf, true /* fFence */);
            virtioCoreVirtqUsedRingSync(pVirtio->pDevInsR3, pVirtio, pTxVirtq->uIdx);
        }

        /* Before we break the loop we need to check if the queue is empty,
         * re-enable notifications, and then re-check again to avoid missing
         * a notification for the descriptor that is added to the queue
         * after we have checked it on being empty, but before we re-enabled
         * notifications.
         */
        if (!(pVirtio->uDriverFeatures & VIRTIO_F_EVENT_IDX)
            && IS_VIRTQ_EMPTY(pDevIns, &pThis->Virtio, pTxVirtq->uIdx))
            virtioCoreVirtqEnableNotify(&pThis->Virtio, pTxVirtq->uIdx, true /* fEnable */);
    }
#ifdef VIRTIO_REL_INFO_DUMP
    if (rc == VERR_INVALID_STATE)
    {
        /*
         * Our best bet for recovering from a bad queue state is to do a device reset, but
         * it is not enough because NetKVM driver does not complete NBLs on reset. We need to lower
         * the link in order to prevent adding new descriptors to the TX queue first. Then we
         * need to simulate processing of all outstanding descriptor chains by simply copying
         * all head indices from avail to used ring. Note that we have only 'peeked' into the avail
         * ring, so the avail index should not have moved yet. This is not the case for other queues.
         */
        virtioNetAttemptToRecover(pDevIns);
        virtioCorePutAllAvailBufsToUsedRing(pDevIns, &pThis->Virtio, pTxVirtq->uIdx);
    }
    else if (ASMAtomicXchgBool(&pThis->Virtio.fRecovering, false))
        LogRel(("[%s] Recovery complete on successfully transmitting a packet\n", pThis->szInst));
#endif /* VIRTIO_REL_INFO_DUMP */
    virtioNetR3SetWriteLed(pThisCC, false);

    if (pDrv)
        pDrv->pfnEndXmit(pDrv);

    ASMAtomicWriteU32(&pThis->uIsTransmitting, 0);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMINETWORKDOWN,pfnXmitPending}
 */
static DECLCALLBACK(void) virtioNetR3NetworkDown_XmitPending(PPDMINETWORKDOWN pInterface)
{
    LogFunc(("\n"));
    PVIRTIONETCC    pThisCC = RT_FROM_MEMBER(pInterface, VIRTIONETCC, INetworkDown);
    PPDMDEVINS      pDevIns = pThisCC->pDevIns;
    PVIRTIONET      pThis   = PDMDEVINS_2_DATA(pThisCC->pDevIns, PVIRTIONET);
    PVIRTIONETVIRTQ pTxVirtq  = &pThis->aVirtqs[TXQIDX(0)];
    STAM_COUNTER_INC(&pThis->StatTransmitByNetwork);

    (void)virtioNetR3TransmitPkts(pDevIns, pThis, pThisCC, pTxVirtq, true /*fOnWorkerThread*/);
}

/**
 * @callback_method_impl{FNTMTIMERDEV, Link Up Timer handler.}
 */
static DECLCALLBACK(void) virtioNetR3LinkUpTimer(PPDMDEVINS pDevIns, TMTIMERHANDLE hTimer, void *pvUser)
{
    PVIRTIONET   pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);
    PVIRTIONETCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIONETCC);

#ifdef VIRTIO_REL_INFO_DUMP
    if (RT_UNLIKELY(!pThis->fVirtioReady))
    {
        if (ASMAtomicXchgBool(&pThis->Virtio.fRecovering, false))
        {
            LogRel(("[%s] Recovery failed, VM reset is needed!\n", pThis->szInst));
            return;
        }
    }
#endif /* VIRTIO_REL_INFO_DUMP */

    SET_LINK_UP(pThis);
#ifdef VIRTIO_REL_INFO_DUMP
    if (ASMAtomicReadBool(&pThis->Virtio.fRecovering))
        LogRel(("[%s] Link is up\n", pThis->szInst));
#endif /* VIRTIO_REL_INFO_DUMP */
    virtioNetWakeupRxBufWaiter(pDevIns);

    if (pThisCC->pDrv)
        pThisCC->pDrv->pfnNotifyLinkChanged(pThisCC->pDrv, PDMNETWORKLINKSTATE_UP);

    LogFunc(("[%s] Link is up\n", pThis->szInst));
    RT_NOREF(hTimer, pvUser);
}

/**
 * @interface_method_impl{PDMINETWORKCONFIG,pfnSetLinkState}
 */
static DECLCALLBACK(int) virtioNetR3NetworkConfig_SetLinkState(PPDMINETWORKCONFIG pInterface, PDMNETWORKLINKSTATE enmState)
{
    PVIRTIONETCC pThisCC = RT_FROM_MEMBER(pInterface, VIRTIONETCC, INetworkConfig);
    PPDMDEVINS   pDevIns = pThisCC->pDevIns;
    PVIRTIONET   pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);

    bool fRequestedLinkStateIsUp = (enmState == PDMNETWORKLINKSTATE_UP);

#ifdef LOG_ENABLED
    if (LogIs7Enabled())
    {
        LogFunc(("[%s]", pThis->szInst));
        switch(enmState)
        {
        case PDMNETWORKLINKSTATE_UP:
            Log(("UP\n"));
            break;
        case PDMNETWORKLINKSTATE_DOWN:
            Log(("DOWN\n"));
            break;
        case PDMNETWORKLINKSTATE_DOWN_RESUME:
            Log(("DOWN (RESUME)\n"));
            break;
        default:
            Log(("UNKNOWN)\n"));
        }
    }
#endif

    if (enmState == PDMNETWORKLINKSTATE_DOWN_RESUME)
    {
        if (IS_LINK_UP(pThis))
        {
            /*
             * We bother to bring the link down only if it was up previously. The UP link state
             * notification will be sent when the link actually goes up in virtioNetR3LinkUpTimer().
             */
            virtioNetR3TempLinkDown(pDevIns, pThis, pThisCC);
            if (pThisCC->pDrv)
                pThisCC->pDrv->pfnNotifyLinkChanged(pThisCC->pDrv, enmState);
        }
    }
    else if (fRequestedLinkStateIsUp != IS_LINK_UP(pThis))
    {
        if (fRequestedLinkStateIsUp)
        {
            Log(("[%s] Link is up\n", pThis->szInst));
            pThis->fCableConnected = true;
            SET_LINK_UP(pThis);
        }
        else /* Link requested to be brought down */
        {
            /* The link was brought down explicitly, make sure it won't come up by timer.  */
            PDMDevHlpTimerStop(pDevIns, pThisCC->hLinkUpTimer);
            Log(("[%s] Link is down\n", pThis->szInst));
            pThis->fCableConnected = false;
            SET_LINK_DOWN(pThis);
        }
        if (pThisCC->pDrv)
            pThisCC->pDrv->pfnNotifyLinkChanged(pThisCC->pDrv, enmState);
    }
    return VINF_SUCCESS;
}
/**
 * @interface_method_impl{PDMINETWORKCONFIG,pfnGetLinkState}
 */
static DECLCALLBACK(PDMNETWORKLINKSTATE) virtioNetR3NetworkConfig_GetLinkState(PPDMINETWORKCONFIG pInterface)
{
    PVIRTIONETCC pThisCC = RT_FROM_MEMBER(pInterface, VIRTIONETCC, INetworkConfig);
    PVIRTIONET pThis = PDMDEVINS_2_DATA(pThisCC->pDevIns, PVIRTIONET);

    return IS_LINK_UP(pThis) ? PDMNETWORKLINKSTATE_UP : PDMNETWORKLINKSTATE_DOWN;
}

static int virtioNetR3DestroyWorkerThreads(PPDMDEVINS pDevIns, PVIRTIONET pThis, PVIRTIONETCC pThisCC)
{
    Log10Func(("[%s]\n", pThis->szInst));
    int rc = VINF_SUCCESS;
    for (unsigned uIdxWorker = 0; uIdxWorker < pThis->cWorkers; uIdxWorker++)
    {
        PVIRTIONETWORKER   pWorker   = &pThis->aWorkers[uIdxWorker];
        PVIRTIONETWORKERR3 pWorkerR3 = &pThisCC->aWorkers[uIdxWorker];

        if (pWorker->hEvtProcess != NIL_SUPSEMEVENT)
        {
            PDMDevHlpSUPSemEventClose(pDevIns, pWorker->hEvtProcess);
            pWorker->hEvtProcess = NIL_SUPSEMEVENT;
        }
        if (pWorkerR3->pThread)
        {
            int rcThread;
            rc = PDMDevHlpThreadDestroy(pDevIns, pWorkerR3->pThread, &rcThread);
            if (RT_FAILURE(rc) || RT_FAILURE(rcThread))
                AssertMsgFailed(("%s Failed to destroythread rc=%Rrc rcThread=%Rrc\n", __FUNCTION__, rc, rcThread));
            pWorkerR3->pThread = NULL;
        }
    }
    return rc;
}

/**
 * Creates a worker for specified queue, along with semaphore to throttle the worker.
 *
 * @param pDevIns       - PDM device instance
 * @param pThis         - virtio-net instance
 * @param pWorker       - Pointer to worker state
 * @param pWorkerR3     - Pointer to worker state
 * @param pVirtq        - Pointer to virtq
 */
static int virtioNetR3CreateOneWorkerThread(PPDMDEVINS pDevIns, PVIRTIONET pThis,
                                            PVIRTIONETWORKER pWorker, PVIRTIONETWORKERR3 pWorkerR3,
                                            PVIRTIONETVIRTQ pVirtq)
{
    Log10Func(("[%s]\n", pThis->szInst));
    RT_NOREF(pThis);

    int rc = PDMDevHlpSUPSemEventCreate(pDevIns, &pWorker->hEvtProcess);

    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("DevVirtioNET: Failed to create SUP event semaphore"));

    LogFunc(("creating thread for queue %s\n", pVirtq->szName));

    rc = PDMDevHlpThreadCreate(pDevIns, &pWorkerR3->pThread,
                               (void *)pWorker, virtioNetR3WorkerThread,
                               virtioNetR3WakeupWorker, 0, RTTHREADTYPE_IO, pVirtq->szName);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("Error creating thread for Virtual Virtq %s\n"), pVirtq->uIdx);

    pWorker->fAssigned = true;  /* Because worker's state in fixed-size array initialized w/empty slots */

    LogFunc(("%s pThread: %p\n", pVirtq->szName, pWorkerR3->pThread));

    return rc;
}

static int virtioNetR3CreateWorkerThreads(PPDMDEVINS pDevIns, PVIRTIONET pThis, PVIRTIONETCC pThisCC)
{
    Log10Func(("[%s]\n", pThis->szInst));
    int rc;

    /* Create the Control Queue worker anyway whether or not it is feature-negotiated or utilized by the guest.
     * See related comment for queue construction in the device constructor function for more context.
     */

    PVIRTIONETVIRTQ pCtlVirtq = &pThis->aVirtqs[CTRLQIDX];
    rc = virtioNetR3CreateOneWorkerThread(pDevIns, pThis,
                                          &pThis->aWorkers[CTRLQIDX], &pThisCC->aWorkers[CTRLQIDX], pCtlVirtq);
    AssertRCReturn(rc, rc);

    pCtlVirtq->fHasWorker = true;

    for (uint16_t uVirtqPair = pThis->cInitializedVirtqPairs; uVirtqPair < pThis->cVirtqPairs; uVirtqPair++)
    {
        PVIRTIONETVIRTQ pTxVirtq = &pThis->aVirtqs[TXQIDX(uVirtqPair)];
        PVIRTIONETVIRTQ pRxVirtq = &pThis->aVirtqs[RXQIDX(uVirtqPair)];

        rc = virtioNetR3CreateOneWorkerThread(pDevIns, pThis, &pThis->aWorkers[TXQIDX(uVirtqPair)],
                                              &pThisCC->aWorkers[TXQIDX(uVirtqPair)], pTxVirtq);
        AssertRCReturn(rc, rc);

        pTxVirtq->fHasWorker = true;
        pRxVirtq->fHasWorker = false;
    }

    if (pThis->cVirtqPairs > pThis->cInitializedVirtqPairs)
        pThis->cInitializedVirtqPairs = pThis->cVirtqPairs;

    pThis->cWorkers = pThis->cVirtqPairs + 1 /* One control virtq */;

    return rc;
}


/**
 * @callback_method_impl{FNPDMTHREADDEV}
 */
static DECLCALLBACK(int) virtioNetR3WorkerThread(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    PVIRTIONET         pThis     = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);
    PVIRTIONETCC       pThisCC   = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIONETCC);
    PVIRTIONETWORKER   pWorker   = (PVIRTIONETWORKER)pThread->pvUser;
    PVIRTIONETVIRTQ    pVirtq    = &pThis->aVirtqs[pWorker->uIdx];
    uint16_t           uIdx      = pWorker->uIdx;

    ASMAtomicWriteBool(&pWorker->fSleeping, false);

    Assert(pWorker->uIdx == pVirtq->uIdx);

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    LogFunc(("[%s] worker thread idx=%d started for %s (virtq idx=%d)\n", pThis->szInst,  pWorker->uIdx, pVirtq->szName, pVirtq->uIdx));

    /** @todo Race w/guest enabling/disabling guest notifications cyclically.
              See BugRef #8651, Comment #82 */
    virtioCoreVirtqEnableNotify(&pThis->Virtio, uIdx, true /* fEnable */);

    while (   pThread->enmState != PDMTHREADSTATE_TERMINATING
           && pThread->enmState != PDMTHREADSTATE_TERMINATED)
    {
        if (IS_VIRTQ_EMPTY(pDevIns, &pThis->Virtio,  pVirtq->uIdx))
        {
            /*  Precisely coordinated atomic interlocks avoid a race condition that results in hung thread
             *  wherein a sloppily coordinated wake-up notification during a transition into or out
             * of sleep leaves notifier and target mutually confused about actual & intended state.
             */
            ASMAtomicWriteBool(&pWorker->fSleeping, true);
            bool fNotificationSent = ASMAtomicXchgBool(&pWorker->fNotified, false);
            if (!fNotificationSent)
            {
                Log10Func(("[%s] %s worker sleeping...\n\n", pThis->szInst, pVirtq->szName));
                Assert(ASMAtomicReadBool(&pWorker->fSleeping));

                int rc = PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pWorker->hEvtProcess, RT_INDEFINITE_WAIT);
                STAM_COUNTER_INC(&pThis->StatTransmitByThread);
                AssertLogRelMsgReturn(RT_SUCCESS(rc) || rc == VERR_INTERRUPTED, ("%Rrc\n", rc), rc);
                if (RT_UNLIKELY(pThread->enmState != PDMTHREADSTATE_RUNNING))
                    return VINF_SUCCESS;
                if (rc == VERR_INTERRUPTED)
                    continue;
               ASMAtomicWriteBool(&pWorker->fNotified, false);
            }
            ASMAtomicWriteBool(&pWorker->fSleeping, false);
        }
        /*
         * Dispatch to the handler for the queue this worker is set up to drive
         */
        if (pVirtq->fCtlVirtq)
        {
            Log10Func(("[%s] %s worker woken. Fetching desc chain\n", pThis->szInst, pVirtq->szName));
            VIRTQBUF_T VirtqBuf;
            PVIRTQBUF pVirtqBuf = &VirtqBuf;
            int rc = virtioCoreR3VirtqAvailBufGet(pDevIns, &pThis->Virtio, pVirtq->uIdx, pVirtqBuf, true);
#ifdef VIRTIO_REL_INFO_DUMP
            if (rc == VERR_INVALID_STATE)
            {
                virtioNetAttemptToRecover(pDevIns);
                continue;
            }
#endif /* VIRTIO_REL_INFO_DUMP */
            if (rc == VERR_NOT_AVAILABLE)
            {
                Log10Func(("[%s] %s worker woken. Nothing found in queue\n", pThis->szInst, pVirtq->szName));
                continue;
            }
            virtioNetR3Ctrl(pDevIns, pThis, pThisCC, pVirtqBuf);
        }
        else /* Must be Tx queue */
        {
            Log10Func(("[%s] %s worker woken. Virtq has data to transmit\n",  pThis->szInst, pVirtq->szName));
            virtioNetR3TransmitPkts(pDevIns, pThis, pThisCC, pVirtq, false /* fOnWorkerThread */);
        }
        /* Note: Surprise! Rx queues aren't handled by local worker threads. Instead, the PDM network leaf driver
         * invokes PDMINETWORKDOWN.pfnWaitReceiveAvail() callback, which waits until woken by virtioNetVirtqNotified()
         * indicating that guest IN buffers have been added to Rx virt queue.
         */
    }
    Log10(("[%s] %s worker thread exiting\n", pThis->szInst, pVirtq->szName));
    return VINF_SUCCESS;
}

/**
 * @callback_method_impl{VIRTIOCORER3,pfnStatusChanged}
 *
 * Called back by the core code when VirtIO's ready state has changed.
 */
static DECLCALLBACK(void) virtioNetR3StatusChg(PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC, uint32_t fVirtioReady)
{
    PVIRTIONET     pThis     = RT_FROM_MEMBER(pVirtio,  VIRTIONET, Virtio);
    PVIRTIONETCC   pThisCC   = RT_FROM_MEMBER(pVirtioCC, VIRTIONETCC, Virtio);

    pThis->fVirtioReady = fVirtioReady;

    if (fVirtioReady)
    {
#ifdef LOG_ENABLED
        Log(("\n%-23s: %s *** VirtIO Ready ***\n\n", __FUNCTION__, pThis->szInst));
        virtioCorePrintDeviceFeatures(&pThis->Virtio, NULL, s_aDevSpecificFeatures, RT_ELEMENTS(s_aDevSpecificFeatures));
#endif
        pThis->fResetting = false;
        pThis->fNegotiatedFeatures = virtioCoreGetNegotiatedFeatures(pVirtio);
        /* Now we can properly figure out the size of virtio header! */
        virtioNetConfigurePktHdr(pThis, pThis->Virtio.fLegacyDriver);
        pThis->virtioNetConfig.uStatus = pThis->fCableConnected ? VIRTIONET_F_LINK_UP : 0;

        for (unsigned uVirtqNbr = 0; uVirtqNbr < pThis->cVirtqs; uVirtqNbr++)
        {
            PVIRTIONETVIRTQ pVirtq = &pThis->aVirtqs[uVirtqNbr];
            PVIRTIONETWORKER pWorker = &pThis->aWorkers[uVirtqNbr];

            Assert(pWorker->uIdx == uVirtqNbr);
            RT_NOREF(pWorker);

            Assert(pVirtq->uIdx == pWorker->uIdx);

            (void) virtioCoreR3VirtqAttach(&pThis->Virtio, pVirtq->uIdx, pVirtq->szName);
            pVirtq->fAttachedToVirtioCore = true;
            if (IS_VIRTQ_EMPTY(pThisCC->pDevIns, &pThis->Virtio, pVirtq->uIdx))
                virtioCoreVirtqEnableNotify(&pThis->Virtio, pVirtq->uIdx, true /* fEnable */);
        }

        virtioNetWakeupRxBufWaiter(pThisCC->pDevIns);
    }
    else
    {
#ifdef VIRTIO_REL_INFO_DUMP
        if (ASMAtomicReadBool(&pThis->Virtio.fRecovering))
            LogRel(("[%s] Resetting...\n", pThis->szInst));
#endif /* VIRTIO_REL_INFO_DUMP */
        Log(("\n%-23s: %s VirtIO is resetting ***\n", __FUNCTION__, pThis->szInst));

        pThis->virtioNetConfig.uStatus = pThis->fCableConnected ? VIRTIONET_F_LINK_UP : 0;
        Log7(("%-23s: %s Link is %s\n", __FUNCTION__, pThis->szInst, pThis->fCableConnected ? "up" : "down"));

        pThis->fPromiscuous         = true;
        pThis->fAllMulticast        = false;
        pThis->fAllUnicast          = false;
        pThis->fNoMulticast         = false;
        pThis->fNoUnicast           = false;
        pThis->fNoBroadcast         = false;
        pThis->uIsTransmitting      = 0;
        pThis->cUnicastFilterMacs   = 0;
        pThis->cMulticastFilterMacs = 0;

        memset(pThis->aMacMulticastFilter,  0, sizeof(pThis->aMacMulticastFilter));
        memset(pThis->aMacUnicastFilter,    0, sizeof(pThis->aMacUnicastFilter));
        memset(pThis->aVlanFilter,          0, sizeof(pThis->aVlanFilter));

        if (pThisCC->pDrv)
            pThisCC->pDrv->pfnSetPromiscuousMode(pThisCC->pDrv, true);

        for (uint16_t uVirtqNbr = 0; uVirtqNbr < pThis->cVirtqs; uVirtqNbr++)
        {
            virtioCoreR3VirtqDetach(&pThis->Virtio, uVirtqNbr);
            pThis->aVirtqs[uVirtqNbr].fAttachedToVirtioCore = false;
        }
    }

#ifdef VIRTIO_REL_INFO_DUMP
    if (pThisCC->pDrv)
        pThisCC->pDrv->pfnNotifyLinkChanged(pThisCC->pDrv, pThis->virtioNetConfig.uStatus ? PDMNETWORKLINKSTATE_UP : PDMNETWORKLINKSTATE_DOWN);
#endif /* VIRTIO_REL_INFO_DUMP */
}

/**
 * @callback_method_impl{VIRTIOCORER3,pfnFeatureNegotiationComplete}
 */
static DECLCALLBACK(void) pfnFeatureNegotiationComplete(PVIRTIOCORE pVirtio, uint64_t fDriverFeatures, uint32_t fLegacy)
{
    PVIRTIONET   pThis   = PDMDEVINS_2_DATA(pVirtio->pDevInsR3, PVIRTIONET);

    LogFunc(("[Feature Negotiation Complete] Guest Driver version is: %s\n", fLegacy ? "legacy" : "modern"));
#ifdef VIRTIO_REL_INFO_DUMP
    if (ASMAtomicReadBool(&pThis->Virtio.fRecovering))
        LogRel(("[%s] Feature Negotiation Complete for %s driver\n", pThis->szInst, fLegacy ? "legacy" : "modern"));
#endif /* VIRTIO_REL_INFO_DUMP */
    virtioNetConfigurePktHdr(pThis, fLegacy);
    virtioNetR3SetVirtqNames(pThis, fLegacy);

    /** @todo r=aeichner We can't just destroy the control queue here because the UEFI firmware and the guest OS might have different
     * opinions on how to use the device and if the UEFI firmware causes the control queue to be destroyed Linux guests
     * will have a hard time using it. */
#if 0
    /* Senseless for modern guest to use control queue in this case. (See Note 1 in PDM-invoked device constructor) */
    if (!fLegacy && !(fDriverFeatures & VIRTIONET_F_CTRL_VQ))
        virtioNetR3VirtqDestroy(pVirtio, &pThis->aVirtqs[CTRLQIDX]);
#else
    RT_NOREF(fDriverFeatures);
#endif
}

#endif /* IN_RING3 */

/**
 * @interface_method_impl{PDMDEVREGR3,pfnDetach}
 *
 * The VM is suspended at this point.
 */
static DECLCALLBACK(void) virtioNetR3Detach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    RT_NOREF(fFlags);

    PVIRTIONET   pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);
    PVIRTIONETCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIONETCC);

    Log7Func(("[%s]\n", pThis->szInst));
    RT_NOREF(pThis);

    AssertLogRelReturnVoid(iLUN == 0);

    pThisCC->pDrvBase = NULL;
    pThisCC->pDrv     = NULL;
}

/**
 * @interface_method_impl{PDMDEVREGR3,pfnAttach}
 *
 * This is called when we change block driver.
 */
static DECLCALLBACK(int) virtioNetR3Attach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PVIRTIONET       pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);
    PVIRTIONETCC     pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIONETCC);

    Log7Func(("[%s]", pThis->szInst));
    AssertLogRelReturn(iLUN == 0, VERR_PDM_NO_SUCH_LUN);

    int rc = PDMDevHlpDriverAttach(pDevIns, 0, &pThisCC->IBase, &pThisCC->pDrvBase, "Network Port");
    if (RT_SUCCESS(rc))
    {
        pThisCC->pDrv = PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMINETWORKUP);
        AssertMsgStmt(pThisCC->pDrv, ("Failed to obtain the PDMINETWORKUP interface!\n"),
                      rc = VERR_PDM_MISSING_INTERFACE_BELOW);
    }
    else if (   rc == VERR_PDM_NO_ATTACHED_DRIVER
             || rc == VERR_PDM_CFG_MISSING_DRIVER_NAME)
    {
        /* This should never happen because this function is not called
         * if there is no driver to attach! */
        Log(("[%s] No attached driver!\n", pThis->szInst));
    }

    RT_NOREF2(pThis, fFlags);
    return rc;
}

/**
 * @interface_method_impl{PDMILEDPORTS,pfnQueryStatusLed}
 */
static DECLCALLBACK(int) virtioNetR3QueryStatusLed(PPDMILEDPORTS pInterface, unsigned iLUN, PPDMLED *ppLed)
{
    PVIRTIONETR3 pThisR3 = RT_FROM_MEMBER(pInterface, VIRTIONETR3, ILeds);
    if (iLUN)
        return VERR_PDM_LUN_NOT_FOUND;
    *ppLed = &pThisR3->led;
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) virtioNetR3QueryInterface(struct PDMIBASE *pInterface, const char *pszIID)
{
    PVIRTIONETR3 pThisCC = RT_FROM_MEMBER(pInterface, VIRTIONETCC, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKDOWN,   &pThisCC->INetworkDown);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMINETWORKCONFIG, &pThisCC->INetworkConfig);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE,          &pThisCC->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMILEDPORTS,      &pThisCC->ILeds);
    return NULL;
}

/**
 * @interface_method_impl{PDMDEVREGR3,pfnReset}
 */
static DECLCALLBACK(void) virtioNetR3Reset(PPDMDEVINS pDevIns)
{
    PVIRTIONET   pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);
    PVIRTIONETCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIONETCC);

    virtioCoreR3ResetDevice(pDevIns, &pThis->Virtio, &pThisCC->Virtio);
}

/**
 * @interface_method_impl{PDMDEVREGR3,pfnDestruct}
 */
static DECLCALLBACK(int) virtioNetR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);

    PVIRTIONET   pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);
    PVIRTIONETCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIONETCC);

    Log(("[%s] Destroying instance\n", pThis->szInst));
    if (pThis->hEventRxDescAvail != NIL_SUPSEMEVENT)
    {
        PDMDevHlpSUPSemEventSignal(pDevIns, pThis->hEventRxDescAvail);
        PDMDevHlpSUPSemEventClose(pDevIns, pThis->hEventRxDescAvail);
        pThis->hEventRxDescAvail = NIL_SUPSEMEVENT;
    }

    virtioNetR3DestroyWorkerThreads(pDevIns, pThis, pThisCC);
    virtioCoreR3Term(pDevIns, &pThis->Virtio, &pThisCC->Virtio);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMDEVREGR3,pfnConstruct}
 *
 * Notes about revising originally VirtIO 1.0+ only virtio-net device emulator to be "transitional",
 * a VirtIO term meaning this now interoperates with both "legacy" (e.g. pre-1.0) and "modern" (1.0+)
 * guest virtio-net drivers. The changes include migrating VMs saved using prior DevVirtioNet.cpp (0.95)
 * saveExec/loadExec semantics to use 1.0 save/load semantics.
 *
 * Regardless of the 1.0 spec's overall helpful guidance for implementing transitional devices,
 * A bit is left to the imagination, e.g. some things have to be determined deductively
 * (AKA "the hard way").
 *
 * Case in point: According to VirtIO 0.95 ("legacy") specification, section 2.2.1, "historically"
 * drivers may start driving prior to feature negotiation and prior to drivers setting DRIVER_OK
 * status, "provided driver doesn't use features that alter early use of this device".  Interpreted
 * here to mean a virtio-net driver must respect default settings (such as implicit pkt header default
 * size, as determined per Note 1 below).
 *
 * ----------------------------------------------------------------------------------------------
 * Transitional device initialization Note 1:  Identifying default value for network Rx pkt hdr size.
 * (VirtIO 1.0 specification section 5.1.6.1)
 *
 * Guest virtio legacy drivers may begin operations prematurely, regardless of early spec's
 * initialization sequence (see note 2 below). Legacy drivers implicitly default to using the
 * (historically) shortest-length network packet header *unless* VIRTIONET_F_MRG_RXBUF feature is
 * negotiated. If feature negotiation phase is [optionally] enacted by a legacy guest (i.e. we strictly
 * enforce full initialization protocol for modern guests), virtioNetConfigurePktHdr() is invoked again to
 * finalize device's network packet header size. Best-guess at default packet header size is deduced, e.g.
 * isn't documented, as follows: A legacy guest with VIRTIONET_F_MRG_RXBUF not-yet-negotiated is the only
 * case where network I/O could possibly occur with any reasonable assumption about packet type/size,
 * because logically other permutations couldn't possibly be inferred until feature negotiation
 * is complete. Specifically, those cases are:
 *
 * 1. A modern driver (detected only when VIRTIONET_F_VERSION_1 feature is ack'd by guest, and,
 * simultaneously, VIRTIONET_F_MRG_RXBUF feature is accepted or declined (determining network receive-packet
 * processing behavior).
 *
 * 2. A legacy driver that has agreed to use VIRTIONET_F_MRG_RXBUF feature, resulting in a two-byte larger pkt hdr,
 * (as well as deciding Rx packet processing behavior).
 *
 * ----------------------------------------------------------------------------------------------
 * Transitional device initialization Note 2:  Creating unnegotiated control queue.
 * (VirtIO 1.0 spec, sections 5.1.5 and 5.1.6.5)
 *
 * Create all queues immediately, prior to feature negotiation, including control queue (irrespective
 * of the fact it's too early in initialization for control feature to be approved by guest). This
 * transitional device must deal with legacy guests which *can* (and on linux have been seen to) use
 * the control queue prior to feature negotiation.
 *
 * The initial assumption is *modern" guest virtio-net drivers out in the wild could never reasonably
 * attempt something as obviously risky as using ctrlq without first acking VIRTIO_NET_F_CTRL_VQ
 * feature to establish it. For now, we create the control queue proactively to accomodate a potentially
 * badly behaved but officially sanctioned legacy virtio-net driver, but *destroy* that same queue
 * if a driver announces as 'modern' during feature finalization yet leaves VIRTIO_NET_F_CTRL_VQ un-ack'd.
 */
static DECLCALLBACK(int) virtioNetR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PVIRTIONET   pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);
    PVIRTIONETCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIONETCC);
    PCPDMDEVHLPR3 pHlp   = pDevIns->pHlpR3;

    /*
     * Quickly initialize state data to ensure destructor always works.
     */
    Log7Func(("PDM device instance: %d\n", iInstance));
    RTStrPrintf(pThis->szInst, sizeof(pThis->szInst), "virtio-net #%d", iInstance);

    pThisCC->pDevIns                          = pDevIns;
    pThisCC->IBase.pfnQueryInterface          = virtioNetR3QueryInterface;
    pThisCC->ILeds.pfnQueryStatusLed          = virtioNetR3QueryStatusLed;
    pThisCC->led.u32Magic = PDMLED_MAGIC;

    /* Interfaces */
    pThisCC->INetworkDown.pfnWaitReceiveAvail = virtioNetR3NetworkDown_WaitReceiveAvail;
    pThisCC->INetworkDown.pfnReceive          = virtioNetR3NetworkDown_Receive;
    pThisCC->INetworkDown.pfnReceiveGso       = virtioNetR3NetworkDown_ReceiveGso;
    pThisCC->INetworkDown.pfnXmitPending      = virtioNetR3NetworkDown_XmitPending;
    pThisCC->INetworkConfig.pfnGetMac         = virtioNetR3NetworkConfig_GetMac;
    pThisCC->INetworkConfig.pfnGetLinkState   = virtioNetR3NetworkConfig_GetLinkState;
    pThisCC->INetworkConfig.pfnSetLinkState   = virtioNetR3NetworkConfig_SetLinkState;

    pThis->hEventRxDescAvail = NIL_SUPSEMEVENT;

    /*
     * Validate configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "MAC"
                                           "|CableConnected"
                                           "|LineSpeed"
                                           "|LinkUpDelay"
                                           "|StatNo"
                                           "|Legacy"
                                           "|MmioBase"
                                           "|Irq", "");

    /* Get config params */
    int rc = pHlp->pfnCFGMQueryBytes(pCfg, "MAC", pThis->macConfigured.au8, sizeof(pThis->macConfigured));
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get MAC address"));

    rc = pHlp->pfnCFGMQueryBool(pCfg, "CableConnected", &pThis->fCableConnected);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the value of 'CableConnected'"));

    uint32_t uStatNo = iInstance;
    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "StatNo", &uStatNo, iInstance);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the \"StatNo\" value"));

    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "LinkUpDelay", &pThis->cMsLinkUpDelay, 5000); /* ms */
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to get the value of 'LinkUpDelay'"));

    Assert(pThis->cMsLinkUpDelay <= 300000); /* less than 5 minutes */

    if (pThis->cMsLinkUpDelay > 5000 || pThis->cMsLinkUpDelay < 100)
        LogRel(("%s WARNING! Link up delay is set to %u seconds!\n",
                pThis->szInst, pThis->cMsLinkUpDelay / 1000));

    Log(("[%s] Link up delay is set to %u seconds\n", pThis->szInst, pThis->cMsLinkUpDelay / 1000));

    /* Copy the MAC address configured for the VM to the MMIO accessible Virtio dev-specific config area */
    memcpy(pThis->virtioNetConfig.uMacAddress.au8, pThis->macConfigured.au8, sizeof(pThis->virtioNetConfig.uMacAddress)); /* TBD */

    Log(("Using MAC address for %s: %2x:%2x:%2x:%2x:%2x:%2x\n", pThis->szInst,
            pThis->macConfigured.au8[0], pThis->macConfigured.au8[1], pThis->macConfigured.au8[2],
            pThis->macConfigured.au8[3], pThis->macConfigured.au8[4], pThis->macConfigured.au8[5]));

    LogFunc(("RC=%RTbool R0=%RTbool\n", pDevIns->fRCEnabled, pDevIns->fR0Enabled));

    /*
     * Configure Virtio core (generic Virtio queue and infrastructure management) parameters.
     */
#   if FEATURE_OFFERED(STATUS)
        pThis->virtioNetConfig.uStatus = 0;
#   endif

    pThis->virtioNetConfig.uMaxVirtqPairs          = VIRTIONET_MAX_QPAIRS;
    pThisCC->Virtio.pfnFeatureNegotiationComplete  = pfnFeatureNegotiationComplete;
    pThisCC->Virtio.pfnVirtqNotified               = virtioNetVirtqNotified;
    pThisCC->Virtio.pfnStatusChanged               = virtioNetR3StatusChg;
    pThisCC->Virtio.pfnDevCapRead                  = virtioNetR3DevCapRead;
    pThisCC->Virtio.pfnDevCapWrite                 = virtioNetR3DevCapWrite;

    VIRTIOPCIPARAMS VirtioPciParams;
    VirtioPciParams.uDeviceId                      = PCI_DEVICE_ID_VIRTIONET_HOST;
    VirtioPciParams.uClassBase                     = VBOX_PCI_CLASS_NETWORK;
    VirtioPciParams.uClassSub                      = VBOX_PCI_SUB_NETWORK_ETHERNET;
    VirtioPciParams.uClassProg                     = PCI_CLASS_PROG_UNSPECIFIED;
    VirtioPciParams.uSubsystemId                   = DEVICE_PCI_NETWORK_SUBSYSTEM;  /* VirtIO 1.0 allows PCI Device ID here */
    VirtioPciParams.uInterruptLine                 = 0x00;
    VirtioPciParams.uInterruptPin                  = 0x01;
    VirtioPciParams.uDeviceType                    = VIRTIO_DEVICE_TYPE_NETWORK;

    /* Create semaphore used to synchronize/throttle the downstream LUN's Rx waiter thread. */
    rc = PDMDevHlpSUPSemEventCreate(pDevIns, &pThis->hEventRxDescAvail);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to create event semaphore"));

    pThis->fOfferLegacy = VIRTIONET_TRANSITIONAL_ENABLE_FLAG;
    virtioNetConfigurePktHdr(pThis, pThis->fOfferLegacy); /* set defaults */

    /* Initialize VirtIO core. (*pfnStatusChanged)() callback occurs when both host VirtIO core & guest driver are ready) */
    rc = virtioCoreR3Init(pDevIns, &pThis->Virtio, &pThisCC->Virtio, &VirtioPciParams, pThis->szInst,
                          VIRTIONET_HOST_FEATURES_OFFERED, pThis->fOfferLegacy,
                          &pThis->virtioNetConfig /*pvDevSpecificCap*/, sizeof(pThis->virtioNetConfig));
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("virtio-net: failed to initialize VirtIO"));

    pThis->fNegotiatedFeatures = virtioCoreGetNegotiatedFeatures(&pThis->Virtio);
    /** @todo validating features at this point is most probably pointless, as the negotiation hasn't started yet. */
    if (!virtioNetValidateRequiredFeatures(pThis->fNegotiatedFeatures))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("virtio-net: Required features not successfully negotiated."));
    pThis->cVirtqPairs = pThis->virtioNetConfig.uMaxVirtqPairs;
    pThis->cVirtqs += pThis->cVirtqPairs * 2 + 1;
    pThis->aVirtqs[CTRLQIDX].fCtlVirtq = true;

    virtioNetR3SetVirtqNames(pThis, pThis->fOfferLegacy);
    for (unsigned uVirtqNbr = 0; uVirtqNbr < pThis->cVirtqs; uVirtqNbr++)
    {
        PVIRTIONETVIRTQ pVirtq = &pThis->aVirtqs[uVirtqNbr];
        PVIRTIONETWORKER pWorker = &pThis->aWorkers[uVirtqNbr];
        PVIRTIONETWORKERR3 pWorkerR3 = &pThisCC->aWorkers[uVirtqNbr];
        pVirtq->uIdx = pWorker->uIdx = pWorkerR3->uIdx = uVirtqNbr;
    }
    /*
     * Create queue workers for life of instance. (I.e. they persist through VirtIO bounces)
     */
    rc = virtioNetR3CreateWorkerThreads(pDevIns, pThis, pThisCC);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to create worker threads"));

    /* Create Link Up Timer */
    rc = PDMDevHlpTimerCreate(pDevIns, TMCLOCK_VIRTUAL, virtioNetR3LinkUpTimer, NULL,
                              TMTIMER_FLAGS_NO_CRIT_SECT | TMTIMER_FLAGS_NO_RING0,
                              "VirtioNet Link Up", &pThisCC->hLinkUpTimer);
    AssertRCReturn(rc, rc);

    /*
     * Attach network driver instance
     */
    rc = PDMDevHlpDriverAttach(pDevIns, 0, &pThisCC->IBase, &pThisCC->pDrvBase, "Network Port");
    if (RT_SUCCESS(rc))
    {
        pThisCC->pDrv = PDMIBASE_QUERY_INTERFACE(pThisCC->pDrvBase, PDMINETWORKUP);
        AssertMsgStmt(pThisCC->pDrv, ("Failed to obtain the PDMINETWORKUP interface!\n"),
                      rc = VERR_PDM_MISSING_INTERFACE_BELOW);
    }
    else if (   rc == VERR_PDM_NO_ATTACHED_DRIVER
             || rc == VERR_PDM_CFG_MISSING_DRIVER_NAME)
    {
        /* No error! */
        Log(("[%s] No attached driver!\n", pThis->szInst));
    }
    else
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to attach the network LUN"));
    /*
     * Status driver
     */
    PPDMIBASE pUpBase;
    rc = PDMDevHlpDriverAttach(pDevIns, PDM_STATUS_LUN, &pThisCC->IBase, &pUpBase, "Status Port");
    if (RT_SUCCESS(rc))
        pThisCC->pLedsConnector = PDMIBASE_QUERY_INTERFACE(pUpBase, PDMILEDCONNECTORS);
    else if (rc != VERR_PDM_NO_ATTACHED_DRIVER)
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to attach the status LUN"));

    /*
     * Register saved state.
     */
    rc = PDMDevHlpSSMRegisterEx(pDevIns, VIRTIONET_SAVEDSTATE_VERSION, sizeof(*pThis), NULL,
                                NULL, NULL, NULL, /** @todo r=aeichner Teleportation? */
                                NULL, virtioNetR3ModernSaveExec, NULL,
                                NULL, virtioNetR3ModernLoadExec, virtioNetR3ModernLoadDone);
    AssertRCReturn(rc, rc);
    /*
     * Statistics and debug stuff.
     * The /Public/ bits are official and used by session info in the GUI.
     */
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatReceiveBytes,  STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                           "Amount of data received",    "/Public/NetAdapter/%u/BytesReceived", uStatNo);
    PDMDevHlpSTAMRegisterF(pDevIns, &pThis->StatTransmitBytes, STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                           "Amount of data transmitted", "/Public/NetAdapter/%u/BytesTransmitted", uStatNo);
    PDMDevHlpSTAMRegisterF(pDevIns, &pDevIns->iInstance,       STAMTYPE_U32,     STAMVISIBILITY_ALWAYS, STAMUNIT_NONE,
                           "Device instance number",     "/Public/NetAdapter/%u/%s", uStatNo, pDevIns->pReg->szName);

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatReceiveBytes,        STAMTYPE_COUNTER, "ReceiveBytes",           STAMUNIT_BYTES,          "Amount of data received");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitBytes,       STAMTYPE_COUNTER, "TransmitBytes",          STAMUNIT_BYTES,          "Amount of data transmitted");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatReceiveGSO,          STAMTYPE_COUNTER, "Packets/ReceiveGSO",     STAMUNIT_COUNT,          "Number of received GSO packets");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitPackets,     STAMTYPE_COUNTER, "Packets/Transmit",       STAMUNIT_COUNT,          "Number of sent packets");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitGSO,         STAMTYPE_COUNTER, "Packets/Transmit-Gso",   STAMUNIT_COUNT,          "Number of sent GSO packets");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitCSum,        STAMTYPE_COUNTER, "Packets/Transmit-Csum",  STAMUNIT_COUNT,          "Number of completed TX checksums");
# ifdef VBOX_WITH_STATISTICS
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatReceive,             STAMTYPE_PROFILE, "Receive/Total",          STAMUNIT_TICKS_PER_CALL, "Profiling receive");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatReceiveStore,        STAMTYPE_PROFILE, "Receive/Store",          STAMUNIT_TICKS_PER_CALL, "Profiling receive storing");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRxOverflow,          STAMTYPE_PROFILE, "RxOverflow",             STAMUNIT_TICKS_PER_OCCURENCE, "Profiling RX overflows");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatRxOverflowWakeup,    STAMTYPE_COUNTER, "RxOverflowWakeup",       STAMUNIT_OCCURENCES,     "Nr of RX overflow wakeups");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmit,            STAMTYPE_PROFILE, "Transmit/Total",         STAMUNIT_TICKS_PER_CALL, "Profiling transmits in HC");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitSend,        STAMTYPE_PROFILE, "Transmit/Send",          STAMUNIT_TICKS_PER_CALL, "Profiling send transmit in HC");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitByNetwork,   STAMTYPE_COUNTER, "Transmit/ByNetwork",     STAMUNIT_COUNT,          "Network-initiated transmissions");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatTransmitByThread,    STAMTYPE_COUNTER, "Transmit/ByThread",      STAMUNIT_COUNT,          "Thread-initiated transmissions");
# endif
    /*
     * Register the debugger info callback (ignore errors).
     */
    char szTmp[128];
    RTStrPrintf(szTmp, sizeof(szTmp), "virtionet%d", iInstance);
    rc = PDMDevHlpDBGFInfoRegister(pDevIns, szTmp, "Display virtionet info (help, net, features, state, pointers, queues, all)", virtioNetR3Info);
    if (RT_FAILURE(rc))
        LogRel(("Failed to register DBGF info for device %s\n", szTmp));
    return rc;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) virtioNetRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PVIRTIONET   pThis   = PDMDEVINS_2_DATA(pDevIns, PVIRTIONET);
    PVIRTIONETCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PVIRTIONETCC);
    pThisCC->Virtio.pfnVirtqNotified = virtioNetVirtqNotified;
    return virtioCoreRZInit(pDevIns, &pThis->Virtio);
}

#endif /* !IN_RING3 */

/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceVirtioNet =
{
    /* .uVersion = */               PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "virtio-net",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_NEW_STYLE | PDM_DEVREG_FLAGS_RZ,
    /* .fClass = */                 PDM_DEVREG_CLASS_NETWORK,
    /* .cMaxInstances = */          ~0U,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(VIRTIONET),
    /* .cbInstanceCC = */           sizeof(VIRTIONETCC),
    /* .cbInstanceRC = */           sizeof(VIRTIONETRC),
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        VBOX_MSIX_MAX_ENTRIES,
    /* .pszDescription = */         "Virtio Host NET.\n",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           virtioNetR3Construct,
    /* .pfnDestruct = */            virtioNetR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               virtioNetR3Reset,
    /* .pfnSuspend = */             virtioNetWakeupRxBufWaiter,
    /* .pfnResume = */              NULL,
    /* .pfnAttach = */              virtioNetR3Attach,
    /* .pfnDetach = */              virtioNetR3Detach,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            virtioNetWakeupRxBufWaiter,
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
    /* .pfnConstruct = */           virtioNetRZConstruct,
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
    /* .pfnConstruct = */           virtioNetRZConstruct,
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
    /* .uVersionEnd = */          PDM_DEVREG_VERSION
};

