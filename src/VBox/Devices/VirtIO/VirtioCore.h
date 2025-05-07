/* $Id: VirtioCore.h 109452 2025-05-07 08:16:10Z aleksey.ilyushin@oracle.com $ */

/** @file
 * VirtioCore.h - Virtio Declarations
 */

/*
 * Copyright (C) 2009-2024 Oracle and/or its affiliates.
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

#ifndef VBOX_INCLUDED_SRC_VirtIO_VirtioCore_h
#define VBOX_INCLUDED_SRC_VirtIO_VirtioCore_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/ctype.h>
#include <iprt/sg.h>
#include <iprt/types.h>

/*
 * WARNING! NEVER ENABLE IN PRODUCTION BUILDS!
 * Enables temporary printouts to release log in descriptor chain handling code.
 */
//#define VIRTIO_REL_INFO_DUMP 1

#ifdef LOG_ENABLED
# define VIRTIO_HEX_DUMP(logLevel, pv, cb, base, title) \
    do { \
        if (LogIsItEnabled(logLevel, LOG_GROUP)) \
            virtioCoreHexDump((pv), (cb), (base), (title)); \
    } while (0)
#else
# define VIRTIO_HEX_DUMP(logLevel, pv, cb, base, title) do { } while (0)
#endif

/** Marks the start of the virtio saved state (just for sanity). */
#define VIRTIO_SAVEDSTATE_MARKER                        UINT64_C(0x1133557799bbddff)

/** Pointer to the shared VirtIO state. */
typedef struct VIRTIOCORE *PVIRTIOCORE;
/** Pointer to the ring-3 VirtIO state. */
typedef struct VIRTIOCORER3 *PVIRTIOCORER3;
/** Pointer to the ring-0 VirtIO state. */
typedef struct VIRTIOCORER0 *PVIRTIOCORER0;
/** Pointer to the raw-mode VirtIO state. */
typedef struct VIRTIOCORERC *PVIRTIOCORERC;
/** Pointer to the instance data for the current context. */
typedef CTX_SUFF(PVIRTIOCORE) PVIRTIOCORECC;

#define VIRTIO_MAX_VIRTQ_NAME_SIZE          32                   /**< Maximum length of a queue name           */
#define VIRTQ_SIZE                        1024                   /**< Max size (# entries) of a virtq          */
#define VIRTQ_MAX_COUNT                     24                   /**< Max queues we allow guest to create      */
#define VIRTIO_NOTIFY_OFFSET_MULTIPLIER     2                    /**< VirtIO Notify Cap. MMIO config param     */
#define VIRTIO_REGION_LEGACY_IO             0                    /**< BAR for VirtIO legacy drivers MBZ        */
#define VIRTIO_REGION_PCI_CAP               2                    /**< BAR for VirtIO Cap. MMIO (impl specific) */
#define VIRTIO_REGION_MSIX_CAP              0                    /**< Bar for MSI-X handling                   */
#define VIRTIO_PAGE_SIZE                 4096                    /**< Page size used by VirtIO specification   */

/** Virtio-over-MMIO transport region size, excluding the device specific configuration area. */
#define VIRTIO_MMIO_SIZE                  256
/** Register containing a magic value identifying a VirtIO over MMIO device - readonly. */
#define VIRTIO_MMIO_REG_MAGIC_OFF           0
/** Magic value read from the register. */
# define VIRTIO_MMIO_REG_MAGIC_VALUE        UINT32_C(0x74726976)
/** Register containing th version of the MMIO transport - readonly. */
#define VIRTIO_MMIO_REG_VERSION_OFF         4
/** Version value read from the register. */
# define VIRTIO_MMIO_REG_VERSION_VALUE      UINT32_C(0x2)
/** Register containing the device ID of the device - readonly. */
#define VIRTIO_MMIO_REG_DEVICEID_OFF        8
/** Register containing the vendor ID of the device - readonly. */
#define VIRTIO_MMIO_REG_VENDORID_OFF       12
/** Register containing the features the device supports - readonly. */
#define VIRTIO_MMIO_REG_DEVICEFEAT_OFF     16
/** Device features selection - writeonly. */
#define VIRTIO_MMIO_REG_DEVICEFEATSEL_OFF  20
/** Features of the device selected by the driver - writeonly. */
#define VIRTIO_MMIO_REG_DRIVERFEAT_OFF     32
/** - writeonly. */
#define VIRTIO_MMIO_REG_DRIVERFEATSEL_OFF  36
/** Virtual queue selection - writeonly. */
#define VIRTIO_MMIO_REG_QUEUESEL_OFF       48
/**  - readonly */
#define VIRTIO_MMIO_REG_QUEUENUMMAX_OFF    52
/**  - writeonly */
#define VIRTIO_MMIO_REG_QUEUENUM_OFF       56
/** - read/write. */
#define VIRTIO_MMIO_REG_QUEUEALIGN_LEGACY_OFF   60
/** - read/write. */
#define VIRTIO_MMIO_REG_QUEUERDY_OFF       68
/**  - writeonly */
#define VIRTIO_MMIO_REG_QUEUENOTIFY_OFF    80
/**  - readonly */
#define VIRTIO_MMIO_REG_INTRSTATUS_OFF     96
/**  - writeonly */
#define VIRTIO_MMIO_REG_INTRACK_OFF       100
/**  - read/write */
#define VIRTIO_MMIO_REG_DEVSTATUS_OFF     112
/**  - writeonly */
#define VIRTIO_MMIO_REG_QUEUEDESCLOW_OFF  128
/**  - writeonly */
#define VIRTIO_MMIO_REG_QUEUEDESCHIGH_OFF 132
/**  - writeonly */
#define VIRTIO_MMIO_REG_QUEUEDRVLOW_OFF   144
/**  - writeonly */
#define VIRTIO_MMIO_REG_QUEUEDRVHIGH_OFF  148
/**  - writeonly */
#define VIRTIO_MMIO_REG_QUEUEDEVLOW_OFF   160
/**  - writeonly */
#define VIRTIO_MMIO_REG_QUEUEDEVHIGH_OFF  164
/**  - readonly */
#define VIRTIO_MMIO_REG_CFGGEN_OFF        252


/**
 * @todo Move the following virtioCoreGCPhysChain*() functions mimic the functionality of the related
 *       into some VirtualBox source tree common location and out of this code.
 *
 *       They behave identically to the S/G utilities in the RT library, except they work with that
 *       GCPhys data type specifically instead of void *, to avoid potentially disastrous mismatch
 *       between sizeof(void *) and sizeof(GCPhys).
 *
 */
typedef struct VIRTIOSGSEG                                      /**< An S/G entry                              */
{
    RTGCPHYS GCPhys;                                            /**< Pointer to the segment buffer             */
    size_t  cbSeg;                                              /**< Size of the segment buffer                */
} VIRTIOSGSEG;

typedef VIRTIOSGSEG *PVIRTIOSGSEG, **PPVIRTIOSGSEG;
typedef const VIRTIOSGSEG *PCVIRTIOSGSEG;

typedef struct VIRTIOSGBUF
{
    PVIRTIOSGSEG paSegs;                                        /**< Pointer to the scatter/gather array       */
    unsigned  cSegs;                                            /**< Number of segs in scatter/gather array    */
    unsigned  idxSeg;                                           /**< Current segment we are in                 */
    RTGCPHYS  GCPhysCur;                                        /**< Ptr to byte within the current seg        */
    size_t    cbSegLeft;                                        /**< # of bytes left in the current segment    */
} VIRTIOSGBUF;

typedef VIRTIOSGBUF *PVIRTIOSGBUF, **PPVIRTIOSGBUF;
typedef const VIRTIOSGBUF *PCVIRTIOSGBUF;

/**
 * VirtIO buffers are descriptor chains (e.g. scatter-gather vectors). A VirtIO buffer is referred to by the index
 * of its head descriptor. Each descriptor optionally chains to another descriptor, and so on.
 *
 * For any given descriptor, each length and GCPhys pair in the chain represents either an OUT segment (e.g. guest-to-host)
 * or an IN segment (host-to-guest).
 *
 * A VIRTQBUF is created and retured from a call to to either virtioCoreR3VirtqAvailBufPeek() or virtioCoreR3VirtqAvailBufGet().
 *
 * Those functions consolidate the VirtIO descriptor chain into a single representation where:
 *
 *     pSgPhysSend    GCPhys s/g buffer containing all of the (VirtIO) OUT descriptors
 *     pSgPhysReturn  GCPhys s/g buffer containing all of the (VirtIO)  IN descriptors
 *
 * The OUT descriptors are data sent from guest to host (dev-specific commands and/or data)
 * The IN are to be filled with data (converted to physical) on host, to be returned to guest
 *
 */
typedef struct VIRTQBUF
{
    uint32_t            u32Magic;                                /**< Magic value, VIRTQBUF_MAGIC.             */
    uint16_t            uVirtq;                                  /**< VirtIO index of associated virtq         */
    uint16_t            pad;
    uint32_t volatile   cRefs;                                   /**< Reference counter.                       */
    uint32_t            uHeadIdx;                                /**< Head idx of associated desc chain        */
    size_t              cbPhysSend;                              /**< Total size of src buffer                 */
    PVIRTIOSGBUF        pSgPhysSend;                             /**< Phys S/G buf for data from guest         */
    size_t              cbPhysReturn;                            /**< Total size of dst buffer                 */
    PVIRTIOSGBUF        pSgPhysReturn;                           /**< Phys S/G buf to store result for guest   */

    /** @name Internal (bird combined 5 allocations into a single), fingers off.
     * @{ */
    VIRTIOSGBUF         SgBufIn;
    VIRTIOSGBUF         SgBufOut;
    VIRTIOSGSEG         aSegsIn[VIRTQ_SIZE];
    VIRTIOSGSEG         aSegsOut[VIRTQ_SIZE];
    /** @} */
} VIRTQBUF_T;

/** Pointers to a Virtio descriptor chain. */
typedef VIRTQBUF_T *PVIRTQBUF, **PPVIRTQBUF;

/** Magic value for VIRTQBUF_T::u32Magic. */
#define VIRTQBUF_MAGIC             UINT32_C(0x19600219)

typedef struct VIRTIOPCIPARAMS
{
    uint16_t  uDeviceId;                                         /**< PCI Cfg Device ID                         */
    uint16_t  uClassBase;                                        /**< PCI Cfg Base Class                        */
    uint16_t  uClassSub;                                         /**< PCI Cfg Subclass                          */
    uint16_t  uClassProg;                                        /**< PCI Cfg Programming Interface Class       */
    uint16_t  uSubsystemId;                                      /**< PCI Cfg Card Manufacturer Vendor ID       */
    uint16_t  uInterruptLine;                                    /**< PCI Cfg Interrupt line                    */
    uint16_t  uInterruptPin;                                     /**< PCI Cfg Interrupt pin                     */
    uint8_t   uDeviceType;                                       /**< Device type (used for Virtio-over-MMIO)   */
} VIRTIOPCIPARAMS, *PVIRTIOPCIPARAMS;


/* Virtio Platform Independent Reserved Feature Bits (see 1.1 specification section 6) */

#define VIRTIO_F_NOTIFY_ON_EMPTY            RT_BIT_64(24)        /**< Legacy feature: Force intr if no AVAIL    */
#define VIRTIO_F_ANY_LAYOUT                 RT_BIT_64(27)        /**< Doc bug: Goes under two names in spec     */
#define VIRTIO_F_RING_INDIRECT_DESC         RT_BIT_64(28)        /**< Doc bug: Goes under two names in spec     */
#define VIRTIO_F_INDIRECT_DESC              RT_BIT_64(28)        /**< Allow descs to point to list of descs     */
#define VIRTIO_F_RING_EVENT_IDX             RT_BIT_64(29)        /**< Doc bug: Goes under two names in spec     */
#define VIRTIO_F_EVENT_IDX                  RT_BIT_64(29)        /**< Allow notification disable for n elems    */
#define VIRTIO_F_BAD_FEATURE                RT_BIT_64(30)        /**< QEMU kludge.  UNUSED as of >= VirtIO 1.0  */
#define VIRTIO_F_VERSION_1                  RT_BIT_64(32)        /**< Required feature bit for 1.0 devices      */
#define VIRTIO_F_ACCESS_PLATFORM            RT_BIT_64(33)        /**< Funky guest mem access   (VirtIO 1.1 NYI) */
#define VIRTIO_F_RING_PACKED                RT_BIT_64(34)        /**< Packed Queue Layout      (VirtIO 1.1 NYI) */
#define VIRTIO_F_IN_ORDER                   RT_BIT_64(35)        /**< Honor guest buf order    (VirtIO 1.1 NYI) */
#define VIRTIO_F_ORDER_PLATFORM             RT_BIT_64(36)        /**< Host mem access honored  (VirtIO 1.1 NYI) */
#define VIRTIO_F_SR_IOV                     RT_BIT_64(37)        /**< Dev Single Root I/O virt (VirtIO 1.1 NYI) */
#define VIRTIO_F_NOTIFICAITON_DATA          RT_BIT_64(38)        /**< Driver passes extra data (VirtIO 1.1 NYI) */

typedef struct VIRTIO_FEATURES_LIST
{
    uint64_t fFeatureBit;
    const char *pcszDesc;
} VIRTIO_FEATURES_LIST, *PVIRTIO_FEATURES_LIST;

static const VIRTIO_FEATURES_LIST s_aCoreFeatures[] =
{
    { VIRTIO_F_VERSION_1,               "   VERSION_1            Guest driver supports VirtIO specification V1.0+ (e.g. \"modern\")\n" },
    { VIRTIO_F_RING_EVENT_IDX,          "   RING_EVENT_IDX       Enables use_event and avail_event fields described in 2.4.7, 2.4.8\n" },
    { VIRTIO_F_RING_INDIRECT_DESC,      "   RING_INDIRECT_DESC   Driver can use descriptors with VIRTQ_DESC_F_INDIRECT flag set\n" },
};

#define VIRTIO_DEV_INDEPENDENT_FEATURES_OFFERED ( 0 )            /**< TBD: Add VIRTIO_F_INDIRECT_DESC           */
#define VIRTIO_DEV_INDEPENDENT_LEGACY_FEATURES_OFFERED ( 0 )     /**< Only offered to legacy drivers            */

#define VIRTIO_ISR_VIRTQ_INTERRUPT           RT_BIT_32(0)        /**< Virtq interrupt bit of ISR register       */
#define VIRTIO_ISR_DEVICE_CONFIG             RT_BIT_32(1)        /**< Device configuration changed bit of ISR   */
#define DEVICE_PCI_NETWORK_SUBSYSTEM                    1        /**< Network Card, per VirtIO legacy spec.     */
#define DEVICE_PCI_REVISION_ID_VIRTIO_TRANS             0        /**< VirtIO Transitional device revision (MBZ) */
#define DEVICE_PCI_REVISION_ID_VIRTIO_V1                1        /**< VirtIO device revision (SHOULD be >= 1)   */

#define DEVICE_PCI_VENDOR_ID_VIRTIO                0x1AF4        /**< Guest driver locates dev via (mandatory)  */

/**
 * Start of the PCI device id range for non-transitional devices.
 *
 * "Devices ... have the PCI Device ID calculated by adding 0x1040 to
 * the Virtio Device ID, as indicated in section [Device Types]. ...
 * Non-transitional devices SHOULD have a PCI Device ID in the range
 * 0x1040 to 0x107f.
 */
#define DEVICE_PCI_DEVICE_ID_VIRTIO_BASE           0x1040

/**
 * @name Virtio Device types as outlined in chapter 5.
 * @{ */
#define VIRTIO_DEVICE_TYPE_INVALID                      0
#define VIRTIO_DEVICE_TYPE_NETWORK                      1
#define VIRTIO_DEVICE_TYPE_BLOCK                        2
#define VIRTIO_DEVICE_TYPE_CONSOLE                      3
#define VIRTIO_DEVICE_TYPE_ENTROPY_SOURCE               4
#define VIRTIO_DEVICE_TYPE_MEMORY_BALLOONING_TRAD       5
#define VIRTIO_DEVICE_TYPE_IOMEM                        6
#define VIRTIO_DEVICE_TYPE_RPMSG                        7
#define VIRTIO_DEVICE_TYPE_SCSI_HOST                    8
#define VIRTIO_DEVICE_TYPE_9P_TRANSPORT                 9
#define VIRTIO_DEVICE_TYPE_MAC80211_WLAN               10
#define VIRTIO_DEVICE_TYPE_RPROC_SERIAL                11
#define VIRTIO_DEVICE_TYPE_CAIF                        12
#define VIRTIO_DEVICE_TYPE_MEMORY_BALLOONING           13
#define VIRTIO_DEVICE_TYPE_GPU                         16
#define VIRTIO_DEVICE_TYPE_TIMER                       17
#define VIRTIO_DEVICE_TYPE_INPUT                       18
#define VIRTIO_DEVICE_TYPE_SOCKET                      19
#define VIRTIO_DEVICE_TYPE_CRYPTO                      20
#define VIRTIO_DEVICE_TYPE_SIGNAL_DIST_MOD             21
#define VIRTIO_DEVICE_TYPE_PSTORE                      22
#define VIRTIO_DEVICE_TYPE_IOMMU                       23
#define VIRTIO_DEVICE_TYPE_MEMORY                      24
/** @} */

/** Reserved (*negotiated*) Feature Bits (e.g. device independent features, VirtIO 1.0 spec,section 6) */

#define VIRTIO_MSI_NO_VECTOR                       0xffff        /**< Vector value to disable MSI for queue     */

/** Device Status field constants (from Virtio 1.0 spec) */
#define VIRTIO_STATUS_ACKNOWLEDGE                    0x01        /**< Guest driver: Located this VirtIO device  */
#define VIRTIO_STATUS_DRIVER                         0x02        /**< Guest driver: Can drive this VirtIO dev.  */
#define VIRTIO_STATUS_DRIVER_OK                      0x04        /**< Guest driver: Driver set-up and ready     */
#define VIRTIO_STATUS_FEATURES_OK                    0x08        /**< Guest driver: Feature negotiation done    */
#define VIRTIO_STATUS_FAILED                         0x80        /**< Guest driver: Fatal error, gave up        */
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET             0x40        /**< Device experienced unrecoverable error    */

typedef enum VIRTIOVMSTATECHANGED
{
    kvirtIoVmStateChangedInvalid = 0,
    kvirtIoVmStateChangedReset,
    kvirtIoVmStateChangedSuspend,
    kvirtIoVmStateChangedPowerOff,
    kvirtIoVmStateChangedResume,
    kvirtIoVmStateChangedFor32BitHack = 0x7fffffff
} VIRTIOVMSTATECHANGED;

/** @def Virtio Device PCI Capabilities type codes */
#define VIRTIO_PCI_CAP_COMMON_CFG                       1        /**< Common configuration PCI capability ID    */
#define VIRTIO_PCI_CAP_NOTIFY_CFG                       2        /**< Notification area PCI capability ID       */
#define VIRTIO_PCI_CAP_ISR_CFG                          3        /**< ISR PCI capability id                     */
#define VIRTIO_PCI_CAP_DEVICE_CFG                       4        /**< Device-specific PCI cfg capability ID     */
#define VIRTIO_PCI_CAP_PCI_CFG                          5        /**< PCI CFG capability ID                     */

#define VIRTIO_PCI_CAP_ID_VENDOR                     0x09        /**< Vendor-specific PCI CFG Device Cap. ID    */

/**
 * The following is the PCI capability struct common to all VirtIO capability types
 */
typedef struct virtio_pci_cap
{
    /* All little-endian */
    uint8_t   uCapVndr;                                          /**< Generic PCI field: PCI_CAP_ID_VNDR        */
    uint8_t   uCapNext;                                          /**< Generic PCI field: next ptr.              */
    uint8_t   uCapLen;                                           /**< Generic PCI field: capability length      */
    uint8_t   uCfgType;                                          /**< Identifies the structure.                 */
    uint8_t   uBar;                                              /**< Where to find it.                         */
    uint8_t   uPadding[3];                                       /**< Pad to full dword.                        */
    uint32_t  uOffset;                                           /**< Offset within bar.  (L.E.)                */
    uint32_t  uLength;                                           /**< Length of struct, in bytes. (L.E.)        */
}  VIRTIO_PCI_CAP_T, *PVIRTIO_PCI_CAP_T;

/**
 * VirtIO Legacy Capabilities' related MMIO-mapped structs (see virtio-0.9.5 spec)
 *
 * Note: virtio_pci_device_cap is dev-specific, implemented by client. Definition unknown here.
 */
typedef struct virtio_legacy_pci_common_cfg
{
    /* Device-specific fields */
    uint32_t  uDeviceFeatures;                                   /**< RO (device reports features to driver)    */
    uint32_t  uDriverFeatures;                                   /**< RW (driver-accepted device features)      */
    uint32_t  uVirtqPfn;                                         /**< RW (driver writes queue page number)      */
    uint16_t  uQueueSize;                                        /**< RW (queue size, 0 - 2^n)                  */
    uint16_t  uVirtqSelect;                                      /**< RW (selects queue focus for these fields) */
    uint16_t  uQueueNotify;                                      /**< RO (offset into virtqueue; see spec)      */
    uint8_t   fDeviceStatus;                                     /**< RW (driver writes device status, 0=reset) */
    uint8_t   fIsrStatus;                                        /**< RW (driver writes ISR status, 0=reset)    */
#ifdef LEGACY_MSIX_SUPPORTED
    uint16_t  uMsixConfig;                                       /**< RW (driver sets MSI-X config vector)      */
    uint16_t  uMsixVector;                                       /**< RW (driver sets MSI-X config vector)      */
#endif
} VIRTIO_LEGACY_PCI_COMMON_CFG_T, *PVIRTIO_LEGACY_PCI_COMMON_CFG_T;

/**
 * VirtIO 1.0 Capabilities' related MMIO-mapped structs:
 *
 * Note: virtio_pci_device_cap is dev-specific, implemented by client. Definition unknown here.
 */
typedef struct virtio_pci_common_cfg
{
    /* Device-specific fields */
    uint32_t  uDeviceFeaturesSelect;                             /**< RW (driver selects device features)       */
    uint32_t  uDeviceFeatures;                                   /**< RO (device reports features to driver)    */
    uint32_t  uDriverFeaturesSelect;                             /**< RW (driver selects driver features)       */
    uint32_t  uDriverFeatures;                                   /**< RW (driver-accepted device features)      */
    uint16_t  uMsixConfig;                                       /**< RW (driver sets MSI-X config vector)      */
    uint16_t  uNumVirtqs;                                        /**< RO (device specifies max queues)          */
    uint8_t   fDeviceStatus;                                     /**< RW (driver writes device status, 0=reset) */
    uint8_t   uConfigGeneration;                                 /**< RO (device changes when changing configs) */

    /* Virtq-specific fields (values reflect (via MMIO) info related to queue indicated by uVirtqSelect. */
    uint16_t  uVirtqSelect;                                      /**< RW (selects queue focus for these fields) */
    uint16_t  uQueueSize;                                        /**< RW (queue size, 0 - 2^n)                  */
    uint16_t  uMsixVector;                                       /**< RW (driver selects MSI-X queue vector)    */
    uint16_t  uEnable;                                           /**< RW (driver controls usability of queue)   */
    uint16_t  uNotifyOffset;                                     /**< RO (offset into virtqueue; see spec)      */
    uint64_t  GCPhysVirtqDesc;                                   /**< RW (driver writes desc table phys addr)   */
    uint64_t  GCPhysVirtqAvail;                                  /**< RW (driver writes avail ring phys addr)   */
    uint64_t  GCPhysVirtqUsed;                                   /**< RW (driver writes used ring  phys addr)   */
} VIRTIO_PCI_COMMON_CFG_T, *PVIRTIO_PCI_COMMON_CFG_T;

typedef struct virtio_pci_notify_cap
{
    struct virtio_pci_cap pciCap;                                /**< Notification MMIO mapping capability      */
    uint32_t uNotifyOffMultiplier;                               /**< notify_off_multiplier                     */
} VIRTIO_PCI_NOTIFY_CAP_T, *PVIRTIO_PCI_NOTIFY_CAP_T;

typedef struct virtio_pci_cfg_cap
{
    struct virtio_pci_cap pciCap;                                /**< Cap. defines the BAR/off/len to access    */
    uint8_t uPciCfgData[4];                                      /**< I/O buf for above cap.                    */
} VIRTIO_PCI_CFG_CAP_T, *PVIRTIO_PCI_CFG_CAP_T;

/**
 * PCI capability data locations (PCI CFG and MMIO).
 */
typedef struct VIRTIO_PCI_CAP_LOCATIONS_T
{
    uint16_t        offMmio;
    uint16_t        cbMmio;
    uint16_t        offPci;
    uint16_t        cbPci;
} VIRTIO_PCI_CAP_LOCATIONS_T;

typedef struct VIRTQUEUE
{
    RTGCPHYS                    GCPhysVirtqDesc;                  /**< (MMIO) Addr of virtq's desc  ring   GUEST */
    RTGCPHYS                    GCPhysVirtqAvail;                 /**< (MMIO) Addr of virtq's avail ring   GUEST */
    RTGCPHYS                    GCPhysVirtqUsed;                  /**< (MMIO) Addr of virtq's used  ring   GUEST */
    uint16_t                    uMsixVector;                      /**< (MMIO) MSI-X vector                 GUEST */
    uint16_t                    uEnable;                          /**< (MMIO) Queue enable flag            GUEST */
    uint16_t                    uNotifyOffset;                    /**< (MMIO) Notification offset for queue HOST */
    uint16_t                    uQueueSize;                       /**< (MMIO) Size of queue           HOST/GUEST */
    uint16_t                    uAvailIdxShadow;                  /**< Consumer's position in avail ring         */
    uint16_t                    uUsedIdxShadow;                   /**< Consumer's position in used ring          */
    uint16_t                    uVirtq;                           /**< Index of this queue                       */
    char                        szName[32];                       /**< Dev-specific name of queue                */
    bool                        fUsedRingEvent;                   /**< Flags if used idx to notify guest reached */
    bool                        fAttached;                        /**< Flags if dev-specific client attached     */
} VIRTQUEUE, *PVIRTQUEUE;

/**
 * The core/common state of the VirtIO PCI devices, shared edition.
 */
typedef struct VIRTIOCORE
{
    char                        szInstance[16];                   /**< Instance name, e.g. "VIRTIOSCSI0"         */
    PPDMDEVINS                  pDevInsR0;                        /**< Client device instance                    */
    PPDMDEVINS                  pDevInsR3;                        /**< Client device instance                    */
    VIRTQUEUE                   aVirtqueues[VIRTQ_MAX_COUNT];     /**< (MMIO) VirtIO contexts for queues         */
    uint64_t                    uDeviceFeatures;                  /**< (MMIO) Host features offered         HOST */
    uint64_t                    uDriverFeatures;                  /**< (MMIO) Host features accepted       GUEST */
    uint32_t                    fDriverFeaturesWritten;           /**< (MMIO) Host features complete tracking    */
    uint32_t                    uDeviceFeaturesSelect;            /**< (MMIO) hi/lo select uDeviceFeatures GUEST */
    uint32_t                    uDriverFeaturesSelect;            /**< (MMIO) hi/lo select uDriverFeatures GUEST */
    uint32_t                    uMsixConfig;                      /**< (MMIO) MSI-X vector                 GUEST */
    uint8_t                     fDeviceStatus;                    /**< (MMIO) Device Status                GUEST */
    uint8_t                     fPrevDeviceStatus;                /**< (MMIO) Prev Device Status           GUEST */
    uint8_t                     uConfigGeneration;                /**< (MMIO) Device config sequencer       HOST */
    uint16_t                    uQueueNotify;                     /**< Caches queue idx in legacy mode     GUEST */
    bool                        fGenUpdatePending;                /**< If set, update cfg gen after driver reads */
    uint8_t                     uPciCfgDataOff;                   /**< Offset to PCI configuration data area     */
    uint8_t                     uISR;                             /**< Interrupt Status Register.                */
    uint8_t                     fMsiSupport;                      /**< Flag set if using MSI instead of ISR      */
    uint16_t                    uVirtqSelect;                     /**< (MMIO) queue selector               GUEST */
    uint32_t                    fLegacyDriver;                    /**< Set if guest drv < VirtIO 1.0 and allowed */
    uint32_t                    fOfferLegacy;                     /**< Set at init call from dev-specific code   */
    uint16_t                    uIrqMmio;                         /**< The interrupt number when Virtio-over-MMIO is used */
    uint8_t                     uDeviceType;                      /**< The implemented device type for Virtio-over-MMIO   */
#ifdef VIRTIO_REL_INFO_DUMP
    bool                        fRecovering;
    bool                        fTestRecovery;
#define VIRTIO_CORE_TRACE_BUF_SIZE (256 /* header */ + 256 * 256 /* buffer*/)
    RTTRACEBUF                  hTraceBuf;
    uint8_t                     aTraceBuf[VIRTIO_CORE_TRACE_BUF_SIZE];
#endif /* VIRTIO_REL_INFO_DUMP */

    /** @name The locations of the capability structures in PCI config space and the BAR.
     * @{ */
    VIRTIO_PCI_CAP_LOCATIONS_T  LocPciCfgCap;                     /**< VIRTIO_PCI_CFG_CAP_T                      */
    VIRTIO_PCI_CAP_LOCATIONS_T  LocNotifyCap;                     /**< VIRTIO_PCI_NOTIFY_CAP_T                   */
    VIRTIO_PCI_CAP_LOCATIONS_T  LocCommonCfgCap;                  /**< VIRTIO_PCI_CAP_T                          */
    VIRTIO_PCI_CAP_LOCATIONS_T  LocIsrCap;                        /**< VIRTIO_PCI_CAP_T                          */
    VIRTIO_PCI_CAP_LOCATIONS_T  LocDeviceCap;                     /**< VIRTIO_PCI_CAP_T + custom data.           */
    /** @} */

    IOMMMIOHANDLE               hMmioPciCap;                      /**< MMIO handle of PCI cap. region (\#2)      */
    IOMIOPORTHANDLE             hLegacyIoPorts;                   /**< Handle of legacy I/O port range.          */

#ifdef VBOX_WITH_STATISTICS
    /** @name Statistics
     * @{ */
    STAMCOUNTER                 StatDescChainsAllocated;
    STAMCOUNTER                 StatDescChainsFreed;
    STAMCOUNTER                 StatDescChainsSegsIn;
    STAMCOUNTER                 StatDescChainsSegsOut;
    STAMPROFILEADV              StatReadR3;                        /** I/O port and MMIO R3 Read profiling       */
    STAMPROFILEADV              StatReadR0;                        /** I/O port and MMIO R0 Read profiling       */
    STAMPROFILEADV              StatReadRC;                        /** I/O port and MMIO R3 Read profiling       */
    STAMPROFILEADV              StatWriteR3;                       /** I/O port and MMIO R3 Write profiling      */
    STAMPROFILEADV              StatWriteR0;                       /** I/O port and MMIO R3 Write profiling      */
    STAMPROFILEADV              StatWriteRC;                       /** I/O port and MMIO R3 Write profiling      */
#endif
    /** @} */

} VIRTIOCORE;

#define MAX_NAME 64

/**
 * The core/common state of the VirtIO PCI devices, ring-3 edition.
 */
typedef struct VIRTIOCORER3
{
    /** @name Callbacks filled by the device before calling virtioCoreR3Init.
     * @{  */
    /**
     * Implementation-specific client callback to report VirtIO when feature negotiation is
     * complete. It should be invoked by the VirtIO core only once.
     *
     * @param   pVirtio           Pointer to the shared virtio state.
     * @param   fDriverFeatures   Bitmask of features the guest driver has accepted/declined.
     * @param   fLegacy           true if legacy mode offered and until guest driver identifies itself
     *                            as modern(e.g. VirtIO 1.0 featured)
     */
    DECLCALLBACKMEMBER(void, pfnFeatureNegotiationComplete, (PVIRTIOCORE pVirtio, uint64_t fDriverFeatures, uint32_t fLegacy));

    /**
     * Implementation-specific client callback to notify client of significant device status
     * changes.
     *
     * @param   pVirtio    Pointer to the shared virtio state.
     * @param   pVirtioCC  Pointer to the ring-3 virtio state.
     * @param   fDriverOk  True if guest driver is okay (thus queues, etc... are
     *                     valid)
     */
    DECLCALLBACKMEMBER(void, pfnStatusChanged,(PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC, uint32_t fDriverOk));

    /**
     * Implementation-specific client callback to access VirtIO Device-specific capabilities
     * (other VirtIO capabilities and features are handled in VirtIO implementation)
     *
     * @param   pDevIns    The device instance.
     * @param   offCap     Offset within device specific capabilities struct.
     * @param   pvBuf      Buffer in which to save read data.
     * @param   cbToRead   Number of bytes to read.
     */
    DECLCALLBACKMEMBER(int, pfnDevCapRead,(PPDMDEVINS pDevIns, uint32_t offCap, void *pvBuf, uint32_t cbToRead));

    /**
     * Implementation-specific client callback to access VirtIO Device-specific capabilities
     * (other VirtIO capabilities and features are handled in VirtIO implementation)
     *
     * @param   pDevIns    The device instance.
     * @param   offCap     Offset within device specific capabilities struct.
     * @param   pvBuf      Buffer with the bytes to write.
     * @param   cbToWrite  Number of bytes to write.
     */
    DECLCALLBACKMEMBER(int, pfnDevCapWrite,(PPDMDEVINS pDevIns, uint32_t offCap, const void *pvBuf, uint32_t cbWrite));

    /**
     * When guest-to-host queue notifications are enabled, the guest driver notifies the host
     * that the avail queue has buffers, and this callback informs the client.
     *
     * @param   pVirtio    Pointer to the shared virtio state.
     * @param   pVirtioCC  Pointer to the ring-3 virtio state.
     * @param   uVirtqNbr   Index of the notified queue
     */
    DECLCALLBACKMEMBER(void, pfnVirtqNotified,(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtqNbr));

    /** @} */

    R3PTRTYPE(PVIRTIO_PCI_CFG_CAP_T)    pPciCfgCap;                /**< Pointer to struct in PCI config area.     */
    R3PTRTYPE(PVIRTIO_PCI_NOTIFY_CAP_T) pNotifyCap;                /**< Pointer  to struct in PCI config area.    */
    R3PTRTYPE(PVIRTIO_PCI_CAP_T)        pCommonCfgCap;             /**< Pointer to struct in PCI config area.     */
    R3PTRTYPE(PVIRTIO_PCI_CAP_T)        pIsrCap;                   /**< Pointer to struct in PCI config area.     */
    R3PTRTYPE(PVIRTIO_PCI_CAP_T)        pDeviceCap;                /**< Pointer to struct in PCI config area.     */

    uint32_t                            cbDevSpecificCfg;          /**< Size of client's dev-specific config data */
    R3PTRTYPE(uint8_t *)                pbDevSpecificCfg;          /**< Pointer to client's struct                */
    R3PTRTYPE(uint8_t *)                pbPrevDevSpecificCfg;      /**< Previous read dev-specific cfg of client  */
    bool                                fGenUpdatePending;         /**< If set, update cfg gen after driver reads */
    char                                szMmioName[MAX_NAME];      /**< MMIO mapping name                         */
    char                                szPortIoName[MAX_NAME];    /**< PORT mapping name                         */
} VIRTIOCORER3;

/**
 * The core/common state of the VirtIO PCI devices, ring-0 edition.
 */
typedef struct VIRTIOCORER0
{
    /**
     * This callback notifies the device-specific portion of this device implementation (if guest-to-host
     * queue notifications are enabled), that the guest driver has notified the host (this device)
     * that the VirtIO "avail" ring of a queue has some new s/g buffers added by the guest VirtIO driver.
     *
     * @param   pVirtio    Pointer to the shared virtio state.
     * @param   pVirtioCC  Pointer to the ring-3 virtio state.
     * @param   uVirtqNbr  Index of the notified queue
     */
    DECLCALLBACKMEMBER(void, pfnVirtqNotified,(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtqNbr));

} VIRTIOCORER0;

/**
 * The core/common state of the VirtIO PCI devices, raw-mode edition.
 */
typedef struct VIRTIOCORERC
{
    uint64_t                    uUnusedAtTheMoment;
} VIRTIOCORERC;

/** @typedef VIRTIOCORECC
 * The instance data for the current context. */
typedef CTX_SUFF(VIRTIOCORE) VIRTIOCORECC;

/** @name API for VirtIO parent device
 * @{ */

/**
 * Setup PCI device controller and Virtio state
 *
 * This should be called from PDMDEVREGR3::pfnConstruct.
 *
 * @param   pDevIns                 Device instance.
 * @param   pVirtio                 Pointer to the shared virtio state.  This
 *                                  must be the first member in the shared
 *                                  device instance data!
 * @param   pVirtioCC               Pointer to the ring-3 virtio state.  This
 *                                  must be the first member in the ring-3
 *                                  device instance data!
 * @param   pPciParams              Values to populate industry standard PCI Configuration Space data structure
 * @param   pcszInstance            Device instance name (format-specifier)
 * @param   fDevSpecificFeatures    VirtIO device-specific features offered by
 *                                  client
 * @param   cbDevSpecificCfg        Size of virtio_pci_device_cap device-specific struct
 * @param   pvDevSpecificCfg        Address of client's dev-specific
 *                                  configuration struct.
 */
DECLHIDDEN(int) virtioCoreR3Init(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC,
                                 PVIRTIOPCIPARAMS pPciParams, const char *pcszInstance,
                                 uint64_t fDevSpecificFeatures, uint32_t fOfferLegacy, void *pvDevSpecificCfg, uint16_t cbDevSpecificCfg);
/**
 * Initiate orderly reset procedure. This is an exposed API for clients that might need it.
 * Invoked by client to reset the device and driver (see VirtIO 1.0 section 2.1.1/2.1.2)
 *
 * @param   pVirtio     Pointer to the virtio state.
 */
DECLHIDDEN(void) virtioCoreResetAll(PVIRTIOCORE pVirtio);

/**
 * Resets the device state upon a VM reset for instance.
 *
 * @param   pVirtio     Pointer to the virtio state.
 *
 * @note Calls back into the upper device when the status changes.
 */
DECLHIDDEN(void) virtioCoreR3ResetDevice(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC);

/**
 * 'Attaches' host device-specific implementation's queue state to host VirtIO core
 * virtqueue management infrastructure, informing the virtio core of the name of the
 * queue to associate with the queue number.

 * Note: uVirtqNbr (ordinal index) is used as the 'handle' for virtqs in this VirtioCore
 * implementation's API (as an opaque selector into the VirtIO core's array of queues' states).
 *
 * Virtqueue numbers are actually VirtIO-specification defined device-specifically
 * (i.e. they are unique within each VirtIO device type), but are in some cases scalable
 * so only the pattern of queue numbers is defined by the spec and implementations may contain
 * a self-determined plurality of queues.
 *
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   uVirtqNbr   Virtq number
 * @param   pcszName    Name to give queue
 *
 * @returns VBox status code.
 */
DECLHIDDEN(int) virtioCoreR3VirtqAttach(PVIRTIOCORE pVirtio, uint16_t uVirtqNbr, const char *pcszName);

/**
 * Detaches host device-specific implementation's queue state from the host VirtIO core
 * virtqueue management infrastructure, informing the VirtIO core that the queue is
 * not utilized by the device-specific code.
 *
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   uVirtqNbr   Virtq number
 * @param   pcszName    Name to give queue
 *
 * @returns VBox status code.
 */
DECLHIDDEN(int) virtioCoreR3VirtqDetach(PVIRTIOCORE pVirtio, uint16_t uVirtqNbr);

/**
 * Checks to see whether queue is attached to core.
 *
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   uVirtqNbr   Virtq number
 *
 * Returns boolean true or false indicating whether dev-specific reflection
 * of queue is attached to core.
 */
DECLHIDDEN(bool) virtioCoreR3VirtqIsAttached(PVIRTIOCORE pVirtio, uint16_t uVirtqNbr);

/**
 * Checks to see whether queue is enabled.
 *
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   uVirtqNbr   Virtq number
 *
 * Returns boolean true or false indicating core queue enable state.
 * There is no API function to enable the queue, because the actual enabling is handled
 * by the guest via MMIO.
 *
 * NOTE: Guest VirtIO driver's claim over this state is overridden (which violates VirtIO 1.0 spec
 * in a carefully controlled manner) in the case where the queue MUST be disabled, due to observed
 * control queue corruption (e.g. null GCPhys virtq base addr) while restoring legacy-only device's
 * (DevVirtioNet.cpp) as a way to flag that the queue is unusable-as-saved and must to be removed.
 * That is all handled in the load/save exec logic. Device reset could potentially, depending on
 * parameters passed from host VirtIO device to guest VirtIO driver, result in guest re-establishing
 * queue, except, in that situation, the queue operational state would be valid.
 */
DECLHIDDEN(bool) virtioCoreR3VirtqIsEnabled(PVIRTIOCORE pVirtio, uint16_t uVirtqNbr);

/**
 * Enable or disable notification for the specified queue.
 *
 * When queue notifications are enabled, the guest VirtIO driver notifies host VirtIO device
 * (via MMIO, see VirtIO 1.0, 4.1.4.4 "Notification Structure Layout") whenever guest driver adds
 * a new s/g buffer to the "avail" ring of the queue.
 *
 * Note: VirtIO queue layout includes flags the device controls in "used" ring to inform guest
 * driver if it should notify host of guest's buffer additions to the "avail" ring, and
 * conversely, the guest driver sets flags in the "avail" ring to communicate to host device
 * whether or not to interrupt guest when it adds buffers to used ring.
 *
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   uVirtqNbr   Virtq number
 * @param   fEnable     Selects notification mode (enabled or disabled)
 */
DECLHIDDEN(void) virtioCoreVirtqEnableNotify(PVIRTIOCORE pVirtio, uint16_t uVirtqNbr, bool fEnable);

/**
 * Notifies guest (via ISR or MSI-X) of device configuration change
 *
 * @param   pVirtio     Pointer to the shared virtio state.
 */
DECLHIDDEN(void) virtioCoreNotifyConfigChanged(PVIRTIOCORE pVirtio);

/**
 * Displays a well-formatted human-readable translation of otherwise inscrutable bitmasks
 * that embody features VirtIO specification definitions, indicating: Totality of features
 * that can be implemented by host and guest, which features were offered by the host, and
 * which were actually accepted by the guest. It displays it as a summary view of the device's
 * finalized operational state (host-guest negotiated architecture) in such a way that shows
 * which options are available for implementing or enabling.
 *
 * The non-device-specific VirtIO features list are managed by core API (e.g. implied).
 * Only dev-specific features must be passed as parameter.

 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   pHlp        Pointer to the debug info hlp struct
 * @param   s_aDevSpecificFeatures  Dev-specific features (virtio-net, virtio-scsi...)
 * @param   cFeatures   Number of features in aDevSpecificFeatures
 */
DECLHIDDEN(void) virtioCorePrintDeviceFeatures(VIRTIOCORE *pVirtio, PCDBGFINFOHLP pHlp,
                                               const VIRTIO_FEATURES_LIST *aDevSpecificFeatures, int cFeatures);

/*
 * Debug-assist utility function to display state of the VirtIO core code, including
 * an overview of the state of all of the queues.
 *
 * This can be invoked when running the VirtualBox debugger, or from the command line
 * using the command: "VboxManage debugvm <VM name or id> info <device name> [args]"
 *
 * Example:  VBoxManage debugvm myVnetVm info "virtio-net" help
 *
 * This is implemented currently to be invoked by the inheriting device-specific code
 * (see the the VirtualBox virtio-net (VirtIO network controller device implementation)
 * for an example of code that receive debugvm callback directly).
 *
 * DevVirtioNet lists available sub-options if no arguments are provided. In that
 * example this virtq info related function is invoked hierarchically when virtio-net
 * displays its device-specific queue info.
 *
 * @param   pDevIns     The device instance.
 * @param   pHlp        Pointer to the debug info hlp struct
 * @param   pVirtio     Pointer to the virtio core embedded into the calling device
 * @param   pszArgs     Arguments to function
 */
DECLHIDDEN(void) virtioCoreR3VirtqInfo(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, PVIRTIOCORE pVirtio, const char *pszArgs, int uVirtqNbr);

/**
 * Returns the number of avail bufs in the virtq.
 *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   uVirtqNbr   Virtqueue to return the count of buffers available for.
 */
DECLHIDDEN(uint16_t) virtioCoreVirtqAvailBufCount(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtqNbr);

#ifdef VIRTIO_REL_INFO_DUMP
/**
 * Copies all available chains to used ring. Only indices of chains are copied, not the chains
 * themselves. This can only be done for virtionet TX ring to work around the issue in NetKVM
 * driver, which does not complete NBLs on device reset, if these NBLs are already in 'avail' ring.
 *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   uVirtqNbr   Virtqueue to modify.
 */
void virtioCorePutAllAvailBufsToUsedRing(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtqNbr);
#endif /* VIRTIO_REL_INFO_DUMP */

/**
 * This function is identical to virtioCoreR3VirtqAvailBufGet(), *except* it doesn't consume
 * peeked buffer from avail ring of the virtq. The function *becomes* identical to the
 * virtioCoreR3VirtqAvailBufGet() only if virtioCoreR3VirtqAvailRingNext() is invoked to
 * consume buf from the queue's avail ring, followed by invocation of virtioCoreR3VirtqUsedBufPut(),
 * to hand host-processed buffer back to guest, which completes guest-initiated virtq buffer circuit.
 *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   uVirtqNbr   Virtq number
 * @param   pVirtqBuf   Pointer to descriptor chain that contains the
 *                      pre-processed transaction information pulled from the virtq.
 *
 * @returns VBox status code:
 * @retval  VINF_SUCCESS         Success
 * @retval  VERR_INVALID_STATE   VirtIO not in ready state (asserted).
 * @retval  VERR_NOT_AVAILABLE   If the queue is empty.
 */
DECLHIDDEN(int) virtioCoreR3VirtqAvailBufPeek(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtqNbr,
                                              PVIRTQBUF pVirtqBuf);

/**
 * This function fetches the next buffer (descriptor chain) from the VirtIO "avail" ring of
 * indicated queue, separating the buf's s/g vectors into OUT (e.g. guest-to-host)
 * components and and IN (host-to-guest) components.
 *
 * Caller is responsible for GCPhys to host virtual memory conversions. If the
 * virtq buffer being peeked at is "consumed", virtioCoreR3VirtqAvailRingNext() must
 * be called, and after that virtioCoreR3VirtqUsedBufPut() must be called to
 * complete the buffer transfer cycle with the guest.
 *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   uVirtqNbr   Virtq number
 * @param   pVirtqBuf   Pointer to descriptor chain that contains the
 *                      pre-processed transaction information pulled from the virtq.
 * @param   fRemove     flags whether to remove desc chain from queue (false = peek)
 *
 * @returns VBox status code:
 * @retval  VINF_SUCCESS         Success
 * @retval  VERR_INVALID_STATE   VirtIO not in ready state (asserted).
 * @retval  VERR_NOT_AVAILABLE   If the queue is empty.
 */
DECLHIDDEN(int) virtioCoreR3VirtqAvailBufGet(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtqNbr,
                                             PVIRTQBUF pVirtqBuf, bool fRemove);

/**
 * Fetches a specific descriptor chain using avail ring of indicated queue and converts the
 * descriptor chain into its OUT (to device) and IN (to guest) components.
 *
 * The caller is responsible for GCPhys to host virtual memory conversions and *must*
 * return the virtq buffer using virtioCoreR3VirtqUsedBufPut() to complete the roundtrip
 * virtq transaction.
 * *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   uVirtqNbr   Virtq number
 * @param   pVirtqBuf   Pointer to descriptor chain that contains the
 *                      pre-processed transaction information pulled from the virtq.
 * @param   fRemove     flags whether to remove desc chain from queue (false = peek)
 *
 * @returns VBox status code:
 * @retval  VINF_SUCCESS         Success
 * @retval  VERR_INVALID_STATE   VirtIO not in ready state (asserted).
 * @retval  VERR_NOT_AVAILABLE   If the queue is empty.
 */
DECLHIDDEN(int) virtioCoreR3VirtqAvailBufGet(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtqNbr,
                                             uint16_t uHeadIdx, PVIRTQBUF pVirtqBuf);

/**
 * Returns data to the guest to complete a transaction initiated by virtioCoreR3VirtqAvailBufGet(),
 * (or virtioCoreR3VirtqAvailBufPeek()/virtioCoreR3VirtqBufSync() call pair), to complete each
 * buffer transfer transaction (guest-host buffer cycle), ultimately moving each descriptor chain
 * from the avail ring of a queue onto the used ring of the queue. Note that VirtIO buffer
 * transactions are *always* initiated by the guest and completed by the host. In other words,
 * for the host to send any I/O related data to the guest (and in some cases configuration data),
 * the guest must provide buffers via the virtq's avail ring, for the host to fill.
 *
 * At some some point virtioCoreR3VirtqUsedRingSync() must be called to return data to the guest,
 * completing all pending virtioCoreR3VirtqAvailBufPut() operations that have accumulated since
 * the last call to virtioCoreR3VirtqUsedRingSync().

 * @note This function effectively performs write-ahead to the used ring of the virtq.
 *       Data written won't be seen by the guest until the next call to virtioCoreVirtqUsedRingSync()
 *
 * @param   pDevIns         The device instance (for reading).
 * @param   pVirtio         Pointer to the shared virtio state.
 * @param   uVirtqNbr       Virtq number
 *
 * @param   pSgVirtReturn   Points to scatter-gather buffer of virtual memory
 *                          segments the caller is returning to the guest.
 *
 * @param   pVirtqBuf       This contains the context of the scatter-gather
 *                          buffer originally pulled from the queue.
 *
 * @param   fFence          If true (default), put up copy-fence (memory barrier) after
 *                          copying to guest phys. mem.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS       Success
 * @retval  VERR_INVALID_STATE VirtIO not in ready state
 * @retval  VERR_NOT_AVAILABLE Virtq is empty
 *
 * @note    This function will not release any reference to pVirtqBuf.  The
 *          caller must take care of that.
 */
DECLHIDDEN(int) virtioCoreR3VirtqUsedBufPut(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtqNbr, PRTSGBUF pSgVirtReturn,
                                            PVIRTQBUF pVirtqBuf, bool fFence = true);


/**
 * Quicker variant of same-named function (directly above) that it overloads,
 * Instead, this variant accepts as input a pointer to a buffer and count,
 * instead of S/G buffer thus doesn't have to copy between two S/G buffers and avoids some overhead.
 *
 * @param   pDevIns         The device instance (for reading).
 * @param   pVirtio         Pointer to the shared virtio state.
 * @param   uVirtqNbr       Virtq number
 * @param   cb              Number of bytes to add to copy to phys. buf.
 * @param   pv              Virtual mem buf to copy to phys buf.
 * @param   cbEnqueue       How many bytes in packet to enqueue (0 = don't enqueue)
 * @param   fFence          If true (default), put up copy-fence (memory barrier) after
 *                          copying to guest phys. mem.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS       Success
 * @retval  VERR_INVALID_STATE VirtIO not in ready state
 * @retval  VERR_NOT_AVAILABLE Virtq is empty
 *
 * @note    This function will not release any reference to pVirtqBuf.  The
 *          caller must take care of that.
 */
DECLHIDDEN(int) virtioCoreR3VirtqUsedBufPut(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtq, size_t cb, const void *pv,
                                            PVIRTQBUF pVirtqBuf, size_t cbEnqueue, bool fFence = true);


/**
 * Advance index of avail ring to next entry in specified virtq (see virtioCoreR3VirtqAvailBufPeek())
 *
 * @param   pVirtio      Pointer to the virtio state.
 * @param   uVirtqNbr    Index of queue
 */
DECLHIDDEN(int) virtioCoreR3VirtqAvailBufNext(PVIRTIOCORE pVirtio, uint16_t uVirtqNbr);

/**
 * Checks to see if guest has accepted host device's VIRTIO_F_VERSION_1 (i.e. "modern")
 * behavioral modeling, indicating guest agreed to comply with the modern VirtIO 1.0+ specification.
 * Otherwise unavoidable presumption is that the host device is dealing with legacy VirtIO
 * guest driver, thus must be prepared to cope with less mature architecture and behaviors
 * from prototype era of VirtIO. (see comments in PDM-invoked device constructor for more
 * information).
 *
 * @param   pVirtio      Pointer to the virtio state.
 */
DECLHIDDEN(int) virtioCoreIsLegacyMode(PVIRTIOCORE pVirtio);

/**
 * This VirtIO transitional device supports "modern" (rev 1.0+) as well as "legacy" (e.g. < 1.0) VirtIO drivers.
 * Some legacy guest drivers are known to mishandle PCI bus mastering wherein the PCI flavor of GC phys
 * access functions can't be used. The following wrappers select the memory access method based on whether the
 * device is operating in legacy mode or not.
 */
DECLINLINE(int) virtioCoreGCPhysWrite(PVIRTIOCORE pVirtio, PPDMDEVINS pDevIns, RTGCPHYS GCPhys, void *pvBuf, size_t cbWrite)
{
    int rc;
    if (   virtioCoreIsLegacyMode(pVirtio)
        || pVirtio->uIrqMmio)
        rc = PDMDevHlpPhysWrite(pDevIns, GCPhys, pvBuf, cbWrite);
    else
        rc = PDMDevHlpPCIPhysWrite(pDevIns, GCPhys, pvBuf, cbWrite);
    return rc;
}

DECLINLINE(int) virtioCoreGCPhysRead(PVIRTIOCORE pVirtio, PPDMDEVINS pDevIns, RTGCPHYS GCPhys, void *pvBuf, size_t cbRead)
{
    int rc;
    if (   virtioCoreIsLegacyMode(pVirtio)
        || pVirtio->uIrqMmio)
        rc = PDMDevHlpPhysRead(pDevIns, GCPhys, pvBuf, cbRead);
    else
        rc = PDMDevHlpPCIPhysRead(pDevIns, GCPhys, pvBuf, cbRead);
    return rc;
}

/*
 * (See comments for corresponding function in sg.h)
 */
DECLINLINE(void) virtioCoreGCPhysChainInit(PVIRTIOSGBUF pGcSgBuf, PVIRTIOSGSEG paSegs, size_t cSegs)
{
    AssertPtr(pGcSgBuf);
    Assert((cSegs > 0 && RT_VALID_PTR(paSegs)) || (!cSegs && !paSegs));
    Assert(cSegs < (~(unsigned)0 >> 1));

    pGcSgBuf->paSegs = paSegs;
    pGcSgBuf->cSegs  = (unsigned)cSegs;
    pGcSgBuf->idxSeg = 0;
    if (cSegs && paSegs)
    {
        pGcSgBuf->GCPhysCur = paSegs[0].GCPhys;
        pGcSgBuf->cbSegLeft = paSegs[0].cbSeg;
    }
    else
    {
        pGcSgBuf->GCPhysCur = 0;
        pGcSgBuf->cbSegLeft = 0;
    }
}

/*
 * (See comments for corresponding function in sg.h)
 */
DECLINLINE(RTGCPHYS) virtioCoreGCPhysChainGet(PVIRTIOSGBUF pGcSgBuf, size_t *pcbData)
{
    size_t cbData;
    RTGCPHYS pGcBuf;

    /* Check that the S/G buffer has memory left. */
    if (RT_LIKELY(pGcSgBuf->idxSeg < pGcSgBuf->cSegs || pGcSgBuf->cbSegLeft))
    { /* likely */ }
    else
    {
        *pcbData = 0;
        return 0;
    }

    AssertMsg(    pGcSgBuf->cbSegLeft <= 128 * _1M
              && (RTGCPHYS)pGcSgBuf->GCPhysCur >= (RTGCPHYS)pGcSgBuf->paSegs[pGcSgBuf->idxSeg].GCPhys
              && (RTGCPHYS)pGcSgBuf->GCPhysCur + pGcSgBuf->cbSegLeft <=
                   (RTGCPHYS)pGcSgBuf->paSegs[pGcSgBuf->idxSeg].GCPhys + pGcSgBuf->paSegs[pGcSgBuf->idxSeg].cbSeg,
                 ("pGcSgBuf->idxSeg=%d pGcSgBuf->cSegs=%d pGcSgBuf->GCPhysCur=%p pGcSgBuf->cbSegLeft=%zd "
                  "pGcSgBuf->paSegs[%d].GCPhys=%p pGcSgBuf->paSegs[%d].cbSeg=%zd\n",
                  pGcSgBuf->idxSeg, pGcSgBuf->cSegs, pGcSgBuf->GCPhysCur, pGcSgBuf->cbSegLeft,
                  pGcSgBuf->idxSeg, pGcSgBuf->paSegs[pGcSgBuf->idxSeg].GCPhys, pGcSgBuf->idxSeg,
                  pGcSgBuf->paSegs[pGcSgBuf->idxSeg].cbSeg));

    cbData = RT_MIN(*pcbData, pGcSgBuf->cbSegLeft);
    pGcBuf = pGcSgBuf->GCPhysCur;
    pGcSgBuf->cbSegLeft -= cbData;
    if (!pGcSgBuf->cbSegLeft)
    {
        pGcSgBuf->idxSeg++;

        if (pGcSgBuf->idxSeg < pGcSgBuf->cSegs)
        {
            pGcSgBuf->GCPhysCur = pGcSgBuf->paSegs[pGcSgBuf->idxSeg].GCPhys;
            pGcSgBuf->cbSegLeft = pGcSgBuf->paSegs[pGcSgBuf->idxSeg].cbSeg;
        }
        *pcbData = cbData;
    }
    else
        pGcSgBuf->GCPhysCur = pGcSgBuf->GCPhysCur + cbData;

    return pGcBuf;
}

/*
 * (See comments for corresponding function in sg.h)
 */
DECLINLINE(void) virtioCoreGCPhysChainReset(PVIRTIOSGBUF pGcSgBuf)
{
    AssertPtrReturnVoid(pGcSgBuf);

    pGcSgBuf->idxSeg = 0;
    if (pGcSgBuf->cSegs)
    {
        pGcSgBuf->GCPhysCur = pGcSgBuf->paSegs[0].GCPhys;
        pGcSgBuf->cbSegLeft = pGcSgBuf->paSegs[0].cbSeg;
    }
    else
    {
        pGcSgBuf->GCPhysCur = 0;
        pGcSgBuf->cbSegLeft = 0;
    }
}

/*
 * (See comments for corresponding function in sg.h)
 */
DECLINLINE(RTGCPHYS) virtioCoreGCPhysChainAdvance(PVIRTIOSGBUF pGcSgBuf, size_t cbAdvance)
{
    AssertReturn(pGcSgBuf, 0);

    size_t cbLeft = cbAdvance;
    while (cbLeft)
    {
        size_t cbThisAdvance = cbLeft;
        virtioCoreGCPhysChainGet(pGcSgBuf, &cbThisAdvance);
        if (!cbThisAdvance)
            break;

        cbLeft -= cbThisAdvance;
    }
    return cbAdvance - cbLeft;
}

/*
 * (See comments for corresponding function in sg.h)
 */
DECLINLINE(RTGCPHYS) virtioCoreGCPhysChainGetNextSeg(PVIRTIOSGBUF pGcSgBuf, size_t *pcbSeg)
{
    AssertReturn(pGcSgBuf, 0);
    AssertPtrReturn(pcbSeg, 0);

    if (!*pcbSeg)
        *pcbSeg = pGcSgBuf->cbSegLeft;

    return virtioCoreGCPhysChainGet(pGcSgBuf, pcbSeg);
}

/**
 * Calculate the length of a GCPhys s/g buffer by tallying the size of each segment.
 *
 * @param   pGcSgBuf        Guest Context (GCPhys) S/G buffer to calculate length of
 */
DECLINLINE(size_t) virtioCoreGCPhysChainCalcBufSize(PCVIRTIOSGBUF pGcSgBuf)
{
    size_t   cb = 0;
    unsigned i  = pGcSgBuf->cSegs;
    while (i-- > 0)
        cb += pGcSgBuf->paSegs[i].cbSeg;
    return cb;
}

/*
 * (See comments for corresponding function in sg.h)
 */
DECLINLINE(size_t) virtioCoreGCPhysChainCalcLengthLeft(PVIRTIOSGBUF pGcSgBuf)
{
    size_t   cb = pGcSgBuf->cbSegLeft;
    unsigned i  = pGcSgBuf->cSegs;
    while (i-- > pGcSgBuf->idxSeg + 1)
        cb += pGcSgBuf->paSegs[i].cbSeg;
    return cb;
}
#define VIRTQNAME(a_pVirtio, a_uVirtq) ((a_pVirtio)->aVirtqueues[(a_uVirtq)].szName)

/**
 * Convert and append bytes from a virtual-memory simple buffer to VirtIO guest's
 * physical memory described by a buffer pulled form the avail ring of a virtq.
 *
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   pVirtqBuf   VirtIO buffer to fill
 * @param   pv          input: virtual memory buffer to receive bytes
 * @param   cb          number of bytes to add to the s/g buffer.
 */
DECLINLINE(void) virtioCoreR3VirqBufFill(PVIRTIOCORE pVirtio, PVIRTQBUF pVirtqBuf, void *pv, size_t cb)
{
    uint8_t *pvBuf = (uint8_t *)pv;
    size_t cbRemain = cb, cbTotal = 0;
    PVIRTIOSGBUF pSgPhysReturn = pVirtqBuf->pSgPhysReturn;
    while (cbRemain)
    {
        size_t cbBounded = RT_MIN(pSgPhysReturn->cbSegLeft, cbRemain);
        Assert(cbBounded > 0);
        virtioCoreGCPhysWrite(pVirtio, CTX_SUFF(pVirtio->pDevIns), (RTGCPHYS)pSgPhysReturn->GCPhysCur, pvBuf, cbBounded);
        virtioCoreGCPhysChainAdvance(pSgPhysReturn, cbBounded);
        pvBuf += cbBounded;
        cbRemain -= cbBounded;
        cbTotal += cbBounded;
    }
    LogFunc(("Appended %d bytes to guest phys buf [head: %u]. %d bytes unused in buf.)\n",
             cbTotal, pVirtqBuf->uHeadIdx, virtioCoreGCPhysChainCalcLengthLeft(pSgPhysReturn)));
}

/**
 * Extract some bytes from of a virtq s/g buffer, converting them from GCPhys space to
 * to ordinary virtual memory (i.e. making data directly accessible to host device code)
 *
 * As a performance optimization, it is left to the caller to validate buffer size.
 *
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   pVirtqBuf   input: virtq buffer
 * @param   pv          output: virtual memory buffer to receive bytes
 * @param   cb          number of bytes to Drain from buffer
 */
DECLINLINE(void) virtioCoreR3VirtqBufDrain(PVIRTIOCORE pVirtio, PVIRTQBUF pVirtqBuf, void *pv, size_t cb)
{
    uint8_t *pb = (uint8_t *)pv;
    size_t cbLim = RT_MIN(pVirtqBuf->cbPhysSend, cb);
    while (cbLim)
    {
        size_t cbSeg = cbLim;
        RTGCPHYS GCPhys = virtioCoreGCPhysChainGetNextSeg(pVirtqBuf->pSgPhysSend, &cbSeg);
        if (pVirtio->uIrqMmio)
            PDMDevHlpPhysRead(pVirtio->pDevInsR3, GCPhys, pb, cbSeg);
        else
            PDMDevHlpPCIPhysRead(pVirtio->pDevInsR3, GCPhys, pb, cbSeg);
        pb += cbSeg;
        cbLim -= cbSeg;
        pVirtqBuf->cbPhysSend -= cbSeg;
    }
    LogFunc(("Drained %d/%d bytes from %s buffer, head idx: %u (%d bytes left)\n",
             cb - cbLim, cb, VIRTQNAME(pVirtio, pVirtqBuf->uVirtq),
             pVirtqBuf->uHeadIdx, virtioCoreGCPhysChainCalcLengthLeft(pVirtqBuf->pSgPhysReturn)));
}

#undef VIRTQNAME

/**
 * Updates indicated virtq's "used ring" descriptor index to match "shadow" index that tracks
 * pending buffers added to the used ring, thus exposing all the data added by virtioCoreR3VirtqUsedBufPut()
 * to the "used ring" since the last virtioCoreVirtqUsedRingSync().
 *
 * This *must* be invoked after one or more virtioCoreR3VirtqUsedBufPut() calls to inform guest driver
 * there is data in the queue. If enabled by guest, IRQ or MSI-X signalling will notify guest
 * proactively, otherwise guest detects updates by polling. (see VirtIO 1.0, Section 2.4 "Virtqueues").
 *
 * @param   pDevIns     The device instance.
 * @param   pVirtio     Pointer to the shared virtio state.
 * @param   uVirtqNbr   Virtq number
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS       Success
 * @retval  VERR_INVALID_STATE VirtIO not in ready state
 */
DECLHIDDEN(int) virtioCoreVirtqUsedRingSync(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, uint16_t uVirtqNbr);

/**
 * Allocates a descriptor chain object with the reference count of one. Copying the reference
 * to this object requires a call to virtioCoreR3VirtqBufRetain. All references must be later
 * released with virtioCoreR3VirtqBufRelease. Just to be clear, one alloc plus one retain will
 * require two releases.
 *
 * @returns A descriptor chain object.
 *
 * @retval  NULL if out of memory.
 *
 * NOTE: VIRTQBUF_T objects allocated on the stack will have garbage in the u32Magic field,
 * triggering an assertion if virtioCoreR3VirtqBufRelease is called on them.
 */
DECLHIDDEN(PVIRTQBUF) virtioCoreR3VirtqBufAlloc(void);

/**
 * Retains a reference to the given descriptor chain.
 *
 * @param   pVirtqBuf      The descriptor chain to reference.
 *
 * @returns New reference count.
 * @retval  UINT32_MAX on invalid parameter.
 */
DECLHIDDEN(uint32_t) virtioCoreR3VirtqBufRetain(PVIRTQBUF pVirtqBuf);

/**
 * Releases a reference to the given descriptor chain.
 *
 * @param   pVirtio         Pointer to the shared virtio state.
 * @param   pVirtqBuf       The descriptor chain to reference.  NULL is quietly
 *                          ignored (returns 0).
 * @returns New reference count.
 * @retval  0 if freed or invalid parameter.
 */
DECLHIDDEN(uint32_t) virtioCoreR3VirtqBufRelease(PVIRTIOCORE pVirtio, PVIRTQBUF pVirtqBuf);

/**
 * Return queue enable state
 *
 * @param   pVirtio      Pointer to the virtio state.
 * @param   uVirtqNbr    Virtq number.
 *
 * @returns true or false indicating queue is enabled or not.
 */
DECLINLINE(bool) virtioCoreIsVirtqEnabled(PVIRTIOCORE pVirtio, uint16_t uVirtqNbr)
{
    Assert(uVirtqNbr < RT_ELEMENTS(pVirtio->aVirtqueues));
    if (pVirtio->fLegacyDriver)
        return pVirtio->aVirtqueues[uVirtqNbr].GCPhysVirtqDesc != 0;
    return pVirtio->aVirtqueues[uVirtqNbr].uEnable != 0;
}

/**
 * Get name of queue, via uVirtqNbr, assigned during virtioCoreR3VirtqAttach()
 *
 * @param   pVirtio     Pointer to the virtio state.
 * @param   uVirtqNbr   Virtq number.
 *
 * @returns Pointer to read-only queue name.
 */
DECLINLINE(const char *) virtioCoreVirtqGetName(PVIRTIOCORE pVirtio, uint16_t uVirtqNbr)
{
    Assert((size_t)uVirtqNbr < RT_ELEMENTS(pVirtio->aVirtqueues));
    return pVirtio->aVirtqueues[uVirtqNbr].szName;
}

/**
 * Get the bitmask of features VirtIO is running with. This is called by the device-specific
 * VirtIO implementation to identify this device's operational configuration after features
 * have been negotiated with guest VirtIO driver. Feature negotiation entails host indicating
 * to guest which features it supports, then guest accepting from among the offered, which features
 * it will enable. That becomes the agreement between the host and guest. The bitmask containing
 * virtio core features plus device-specific features is provided as a parameter to virtioCoreR3Init()
 * by the host side device-specific virtio implementation.
 *
 * @param   pVirtio     Pointer to the virtio state.
 *
 * @returns Features the guest driver has accepted, finalizing the operational features
 */
DECLINLINE(uint64_t) virtioCoreGetNegotiatedFeatures(PVIRTIOCORE pVirtio)
{
    return pVirtio->uDriverFeatures;
}

/**
 * Get name of the VM state change associated with the enumeration variable
 *
 * @param enmState       VM state (enumeration value)
 *
 * @returns associated text.
 */
DECLHIDDEN(const char *) virtioCoreGetStateChangeText(VIRTIOVMSTATECHANGED enmState);

/**
 * Debug assist code for any consumer that inherits VIRTIOCORE.
 * Log memory-mapped I/O input or output value.
 *
 * This is to be invoked by macros that assume they are invoked in functions with
 * the relevant arguments. (See Virtio_1_0.cpp).
 *
 * It is exposed via the API so inheriting device-specific clients can provide similar
 * logging capabilities for a consistent look-and-feel.
 *
 * @param   pszFunc     To avoid displaying this function's name via __FUNCTION__ or LogFunc()
 * @param   pszMember   Name of struct member
 * @param   pv          pointer to value
 * @param   cb          size of value
 * @param   uOffset     offset into member where value starts
 * @param   fWrite      True if write I/O
 * @param   fHasIndex   True if the member is indexed
 * @param   idx         The index if fHasIndex
 */
DECLHIDDEN(void) virtioCoreLogMappedIoValue(const char *pszFunc, const char *pszMember, uint32_t uMemberSize,
                                            const void *pv, uint32_t cb, uint32_t uOffset,
                                            int fWrite, int fHasIndex, uint32_t idx);

/**
 * Debug assist for any consumer
 *
 * Does a formatted hex dump using Log(()), recommend using VIRTIO_HEX_DUMP() macro to
 * control enabling of logging efficiently.
 *
 * @param   pv          pointer to buffer to dump contents of
 * @param   cb          count of characters to dump from buffer
 * @param   uBase       base address of per-row address prefixing of hex output
 * @param   pszTitle    Optional title. If present displays title that lists
 *                      provided text with value of cb to indicate VIRTQ_SIZE next to it.
 */
DECLHIDDEN(void) virtioCoreHexDump(uint8_t *pv, uint32_t cb, uint32_t uBase, const char *pszTitle);

/**
 * Debug assist for any consumer device code
 * Do a hex dump of memory in guest physical context
 *
 * @param   GCPhys      pointer to buffer to dump contents of
 * @param   cb          count of characters to dump from buffer
 * @param   uBase       base address of per-row address prefixing of hex output
 * @param   pszTitle    Optional title. If present displays title that lists
 *                      provided text with value of cb to indicate size next to it.
 */
DECLHIDDEN(void) virtioCoreGCPhysHexDump(PPDMDEVINS pDevIns, RTGCPHYS GCPhys, uint16_t cb, uint32_t uBase, const char *pszTitle);

/**
 * The following API is functions identically to the similarly-named calls pertaining to the RTSGBUF
 */

/** Misc VM and PDM boilerplate */
DECLHIDDEN(int)          virtioCoreR3SaveExec(PVIRTIOCORE pVirtio, PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM,
                                              uint32_t uVersion, uint32_t cQueues);
DECLHIDDEN(int)          virtioCoreR3ModernDeviceLoadExec(PVIRTIOCORE pVirtio, PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM,
                                                          uint32_t uVersion, uint32_t uTestVersion, uint32_t cQueues);
DECLHIDDEN(int)          virtioCoreR3LegacyDeviceLoadExec(PVIRTIOCORE pVirtio, PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM,
                                                          uint32_t uVersion, uint32_t uVirtioLegacy_3_1_Beta);
DECLHIDDEN(void)         virtioCoreR3VmStateChanged(PVIRTIOCORE pVirtio, VIRTIOVMSTATECHANGED enmState);
DECLHIDDEN(void)         virtioCoreR3Term(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio, PVIRTIOCORECC pVirtioCC);
DECLHIDDEN(int)          virtioCoreRZInit(PPDMDEVINS pDevIns, PVIRTIOCORE pVirtio);
DECLHIDDEN(const char *) virtioCoreGetStateChangeText(VIRTIOVMSTATECHANGED enmState);

/*
 * The following macros assist with handling/logging MMIO accesses to VirtIO dev-specific config area,
 * in a way that enhances code readability and debug logging consistency.
 *
 * cb, pv and fWrite are implicit parameters and must be defined by the invoker.
 */
#ifdef LOG_ENABLED

# define VIRTIO_DEV_CONFIG_LOG_ACCESS(member, tCfgStruct, uOffsetOfAccess) \
    if (LogIs7Enabled()) { \
        uint32_t uMbrOffset = uOffsetOfAccess - RT_UOFFSETOF(tCfgStruct, member); \
        uint32_t uMbrSize   = RT_SIZEOFMEMB(tCfgStruct, member); \
        virtioCoreLogMappedIoValue(__FUNCTION__, #member, uMbrSize, pv, cb, uMbrOffset, fWrite, false, 0); \
    }

# define VIRTIO_DEV_CONFIG_LOG_INDEXED_ACCESS(member, tCfgStruct, uOffsetOfAccess, uIdx) \
    if (LogIs7Enabled()) { \
        uint32_t uMbrOffset = uOffsetOfAccess - RT_UOFFSETOF(tCfgStruct, member); \
        uint32_t uMbrSize   = RT_SIZEOFMEMB(tCfgStruct, member); \
        virtioCoreLogMappedIoValue(__FUNCTION__, #member, uMbrSize,  pv, cb, uMbrOffset, fWrite, true, uIdx); \
    }
#else
# define VIRTIO_DEV_CONFIG_LOG_ACCESS(member, tCfgStruct, uMbrOffset) do { } while (0)
# define VIRTIO_DEV_CONFIG_LOG_INDEXED_ACCESS(member, tCfgStruct, uMbrOffset, uIdx) do { } while (0)
#endif

DECLINLINE(bool) virtioCoreMatchMember(uint32_t uOffset, uint32_t cb, uint32_t uMemberOff,
                                       size_t uMemberSize, bool fSubFieldMatch)
{
    /* Test for 8-byte field (always accessed as two 32-bit components) */
    if (uMemberSize == 8)
        return (cb == sizeof(uint32_t)) && (uOffset == uMemberOff || uOffset == (uMemberOff + sizeof(uint32_t)));

    if (fSubFieldMatch)
        return (uOffset >= uMemberOff) && (cb <= uMemberSize - (uOffset - uMemberOff));

    /* Test for exact match */
    return (uOffset == uMemberOff) && (cb == uMemberSize);
}

/**
 * Yields boolean true if uOffsetOfAccess falls within bytes of specified member of config struct
 */
#define VIRTIO_DEV_CONFIG_SUBMATCH_MEMBER(member, tCfgStruct, uOffsetOfAccess) \
            virtioCoreMatchMember(uOffsetOfAccess, cb, \
                                  RT_UOFFSETOF(tCfgStruct, member),  \
                                  RT_SIZEOFMEMB(tCfgStruct, member), true /* fSubfieldMatch */)

#define VIRTIO_DEV_CONFIG_MATCH_MEMBER(member, tCfgStruct, uOffsetOfAccess) \
            virtioCoreMatchMember(uOffsetOfAccess, cb, \
                                  RT_UOFFSETOF(tCfgStruct, member),  \
                                  RT_SIZEOFMEMB(tCfgStruct, member), false /* fSubfieldMatch */)



/**
 * Copy reads or copy writes specified member field of config struct (based on fWrite),
 * the memory described by cb and pv.
 *
 * cb, pv and fWrite are implicit parameters and must be defined by invoker.
 */
#define VIRTIO_DEV_CONFIG_ACCESS(member, tCfgStruct, uOffsetOfAccess, pCfgStruct) \
    do \
    { \
        uint32_t uOffsetInMember = uOffsetOfAccess - RT_UOFFSETOF(tCfgStruct, member); \
        if (fWrite) \
            memcpy(((char *)&(pCfgStruct)->member) + uOffsetInMember, pv, cb); \
        else \
            memcpy(pv, ((const char *)&(pCfgStruct)->member) + uOffsetInMember, cb); \
        VIRTIO_DEV_CONFIG_LOG_ACCESS(member, tCfgStruct, uOffsetOfAccess); \
    } while(0)

/**
 * Copies bytes into memory described by cb, pv from the specified member field of the config struct.
 * The operation is a NOP, logging an error if an implied parameter, fWrite, is boolean true.
 *
 * cb, pv and fWrite are implicit parameters and must be defined by the invoker.
 */
#define VIRTIO_DEV_CONFIG_ACCESS_READONLY(member, tCfgStruct, uOffsetOfAccess, pCfgStruct) \
    do \
    { \
        uint32_t uOffsetInMember = uOffsetOfAccess - RT_UOFFSETOF(tCfgStruct, member); \
        if (fWrite) \
            LogFunc(("Guest attempted to write readonly virtio config struct (member %s)\n", #member)); \
        else \
        { \
            memcpy(pv, ((const char *)&(pCfgStruct)->member) + uOffsetInMember, cb); \
            VIRTIO_DEV_CONFIG_LOG_ACCESS(member, tCfgStruct, uOffsetOfAccess); \
        } \
    } while(0)

/**
 * Copies into or out of specified member field of config struct (based on fWrite),
 * the memory described by cb and pv.
 *
 * cb, pv and fWrite are implicit parameters and must be defined by invoker.
 */
#define VIRTIO_DEV_CONFIG_ACCESS_INDEXED(member, uIdx, tCfgStruct, uOffsetOfAccess, pCfgStruct) \
    do \
    { \
        uint32_t uOffsetInMember = uOffsetOfAccess - RT_UOFFSETOF(tCfgStruct, member); \
        if (fWrite) \
            memcpy(((char *)&(pCfgStruct[uIdx].member)) + uOffsetInMember, pv, cb); \
        else \
            memcpy(pv, ((const char *)&(pCfgStruct[uIdx].member)) + uOffsetInMember, cb); \
        VIRTIO_DEV_CONFIG_LOG_INDEXED_ACCESS(member, tCfgStruct, uOffsetOfAccess, uIdx); \
    } while(0)

/**
 * Copies bytes into memory described by cb, pv from the specified member field of the config struct.
 * The operation is a nop and logs error if implied parameter fWrite is true.
 *
 * cb, pv and fWrite are implicit parameters and must be defined by invoker.
 */
#define VIRTIO_DEV_CONFIG_ACCESS_INDEXED_READONLY(member, uidx, tCfgStruct, uOffsetOfAccess, pCfgStruct) \
    do \
    { \
        uint32_t uOffsetInMember = uOffsetOfAccess - RT_UOFFSETOF(tCfgStruct, member); \
        if (fWrite) \
            LogFunc(("Guest attempted to write readonly virtio config struct (member %s)\n", #member)); \
        else \
        { \
            memcpy(pv, ((const char *)&(pCfgStruct[uIdx].member)) + uOffsetInMember, cb); \
            VIRTIO_DEV_CONFIG_LOG_INDEXED_ACCESS(member, tCfgStruct, uOffsetOfAccess, uIdx); \
        } \
    } while(0)

/** @} */

/** @name API for VirtIO parent device
 * @{ */

#endif /* !VBOX_INCLUDED_SRC_VirtIO_VirtioCore_h */
