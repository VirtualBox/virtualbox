/* $Id: DevIommuAmd.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IOMMU - Input/Output Memory Management Unit - AMD implementation.
 */

/*
 * Copyright (C) 2020-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DEV_IOMMU
#include <VBox/msi.h>
#include <VBox/iommu-amd.h>
#include <VBox/vmm/pdmdev.h>

#include <iprt/x86.h>
#include <iprt/string.h>
#include <iprt/avl.h>
#ifdef IN_RING3
# include <iprt/mem.h>
#endif

#include "VBoxDD.h"
#include "DevIommuAmd.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Release log prefix string. */
#define IOMMU_LOG_PFX                               "AMD-IOMMU"
/** The current saved state version. */
#define IOMMU_SAVED_STATE_VERSION                   1
/** The IOMMU device instance magic. */
#define IOMMU_MAGIC                                 0x10acce55

/** Enable the IOTLBE cache only in ring-3 for now, see @bugref{9654#c95}. */
#ifdef IN_RING3
# define IOMMU_WITH_IOTLBE_CACHE
#endif
/** Enable the interrupt cache. */
#define IOMMU_WITH_IRTE_CACHE

/* The DTE cache is mandatory for the IOTLB or interrupt cache to work. */
#if defined(IOMMU_WITH_IOTLBE_CACHE) || defined(IOMMU_WITH_IRTE_CACHE)
# define IOMMU_WITH_DTE_CACHE
/** The maximum number of device IDs in the cache. */
# define IOMMU_DEV_CACHE_COUNT                      16
/** An empty device ID. */
# define IOMMU_DTE_CACHE_KEY_NIL                    0
#endif

#ifdef IOMMU_WITH_IRTE_CACHE
/** The maximum number of IRTE cache entries. */
# define IOMMU_IRTE_CACHE_COUNT                     32
/** A NIL IRTE cache entry key. */
# define IOMMU_IRTE_CACHE_KEY_NIL                   (~(uint32_t)0U)
/** Gets the device ID from an IRTE cache entry key. */
#define IOMMU_IRTE_CACHE_KEY_GET_DEVICE_ID(a_Key)   RT_HIWORD(a_Key)
/** Gets the IOVA from the IOTLB entry key. */
# define IOMMU_IRTE_CACHE_KEY_GET_OFF(a_Key)        RT_LOWORD(a_Key)
/** Makes an IRTE cache entry key.
 *
 * Bits 31:16 is the device ID (Bus, Device, Function).
 * Bits  15:0 is the the offset into the IRTE table.
 */
# define IOMMU_IRTE_CACHE_KEY_MAKE(a_DevId, a_off)  RT_MAKE_U32(a_off, a_DevId)
#endif  /* IOMMU_WITH_IRTE_CACHE */

#ifdef IOMMU_WITH_IOTLBE_CACHE
/** The maximum number of IOTLB entries. */
# define IOMMU_IOTLBE_MAX                           64
/** The mask of bits covering the domain ID in the IOTLBE key. */
# define IOMMU_IOTLB_DOMAIN_ID_MASK                 UINT64_C(0xffffff0000000000)
/** The mask of bits covering the IOVA in the IOTLBE key. */
# define IOMMU_IOTLB_IOVA_MASK                     (~IOMMU_IOTLB_DOMAIN_ID_MASK)
/** The number of bits to shift for the domain ID of the IOTLBE key. */
# define IOMMU_IOTLB_DOMAIN_ID_SHIFT                40
/** A NIL IOTLB key. */
# define IOMMU_IOTLB_KEY_NIL                        UINT64_C(0)
/** Gets the domain ID from an IOTLB entry key. */
# define IOMMU_IOTLB_KEY_GET_DOMAIN_ID(a_Key)       ((a_Key) >> IOMMU_IOTLB_DOMAIN_ID_SHIFT)
/** Gets the IOVA from the IOTLB entry key. */
# define IOMMU_IOTLB_KEY_GET_IOVA(a_Key)            (((a_Key) & IOMMU_IOTLB_IOVA_MASK) << X86_PAGE_4K_SHIFT)
/** Makes an IOTLB entry key.
 *
 * Address bits 63:52 of the IOVA are zero extended, so top 12 bits are free.
 * Address bits 11:0 of the IOVA are offset into the minimum page size of 4K,
 * so bottom 12 bits are free.
 *
 * Thus we use the top 24 bits of key to hold bits 15:0 of the domain ID.
 * We use the bottom 40 bits of the key to hold bits 51:12 of the IOVA.
 */
# define IOMMU_IOTLB_KEY_MAKE(a_DomainId, a_uIova)  (  ((uint64_t)(a_DomainId) << IOMMU_IOTLB_DOMAIN_ID_SHIFT) \
                                                     | (((a_uIova) >> X86_PAGE_4K_SHIFT) & IOMMU_IOTLB_IOVA_MASK))
#endif  /* IOMMU_WITH_IOTLBE_CACHE */

#ifdef IOMMU_WITH_DTE_CACHE
/** @name IOMMU_DTE_CACHE_F_XXX: DTE cache flags.
 *
 *  Some of these flags are "basic" i.e. they correspond directly to their bits in
 *  the DTE. The rest of the flags are based on checks or operations on several DTE
 *  bits.
 *
 *  The basic flags are:
 *    - VALID                (DTE.V)
 *    - IO_PERM_READ         (DTE.IR)
 *    - IO_PERM_WRITE        (DTE.IW)
 *    - IO_PERM_RSVD         (bit following DTW.IW reserved for future & to keep
 *                            masking consistent)
 *    - SUPPRESS_ALL_IOPF    (DTE.SA)
 *    - SUPPRESS_IOPF        (DTE.SE)
 *    - INTR_MAP_VALID       (DTE.IV)
 *    - IGNORE_UNMAPPED_INTR (DTE.IG)
 *
 *  @see iommuAmdGetBasicDevFlags()
 *  @{ */
/** The DTE is present. */
# define IOMMU_DTE_CACHE_F_PRESENT                       RT_BIT(0)
/** The DTE is valid. */
# define IOMMU_DTE_CACHE_F_VALID                         RT_BIT(1)
/** The DTE permissions apply for address translations. */
# define IOMMU_DTE_CACHE_F_IO_PERM                       RT_BIT(2)
/** DTE permission - I/O read allowed. */
# define IOMMU_DTE_CACHE_F_IO_PERM_READ                  RT_BIT(3)
/** DTE permission - I/O write allowed. */
# define IOMMU_DTE_CACHE_F_IO_PERM_WRITE                 RT_BIT(4)
/** DTE permission - reserved. */
# define IOMMU_DTE_CACHE_F_IO_PERM_RSVD                  RT_BIT(5)
/** Address translation required. */
# define IOMMU_DTE_CACHE_F_ADDR_TRANSLATE                RT_BIT(6)
/** Suppress all I/O page faults. */
# define IOMMU_DTE_CACHE_F_SUPPRESS_ALL_IOPF             RT_BIT(7)
/** Suppress I/O page faults. */
# define IOMMU_DTE_CACHE_F_SUPPRESS_IOPF                 RT_BIT(8)
/** Interrupt map valid. */
# define IOMMU_DTE_CACHE_F_INTR_MAP_VALID                RT_BIT(9)
/** Ignore unmapped interrupts. */
# define IOMMU_DTE_CACHE_F_IGNORE_UNMAPPED_INTR          RT_BIT(10)
/** An I/O page fault has been raised for this device. */
# define IOMMU_DTE_CACHE_F_IO_PAGE_FAULT_RAISED          RT_BIT(11)
/** Fixed and arbitrary interrupt control: Target Abort. */
# define IOMMU_DTE_CACHE_F_INTR_CTRL_TARGET_ABORT        RT_BIT(12)
/** Fixed and arbitrary interrupt control: Forward unmapped. */
# define IOMMU_DTE_CACHE_F_INTR_CTRL_FWD_UNMAPPED        RT_BIT(13)
/** Fixed and arbitrary interrupt control: Remapped. */
# define IOMMU_DTE_CACHE_F_INTR_CTRL_REMAPPED            RT_BIT(14)
/** Fixed and arbitrary interrupt control: Reserved. */
# define IOMMU_DTE_CACHE_F_INTR_CTRL_RSVD                RT_BIT(15)
/** @} */

/** The number of bits to shift I/O device flags for DTE permissions. */
# define IOMMU_DTE_CACHE_F_IO_PERM_SHIFT                 3
/** The mask of DTE permissions in I/O device flags. */
# define IOMMU_DTE_CACHE_F_IO_PERM_MASK                  0x3
/** The number of bits to shift I/O device flags for interrupt control bits. */
# define IOMMU_DTE_CACHE_F_INTR_CTRL_SHIFT               12
/** The mask of interrupt control bits in I/O device flags. */
# define IOMMU_DTE_CACHE_F_INTR_CTRL_MASK                0x3
/** The number of bits to shift for ignore-unmapped interrupts bit. */
# define IOMMU_DTE_CACHE_F_IGNORE_UNMAPPED_INTR_SHIFT    10

/** Acquires the cache lock. */
# define IOMMU_CACHE_LOCK(a_pDevIns, a_pThis) \
    do { \
        int const rcLock = PDMDevHlpCritSectEnter((a_pDevIns), &(a_pThis)->CritSectCache, VINF_SUCCESS); \
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV((a_pDevIns), &(a_pThis)->CritSectCache, rcLock); \
    } while (0)

/** Releases the cache lock.  */
# define IOMMU_CACHE_UNLOCK(a_pDevIns, a_pThis)     PDMDevHlpCritSectLeave((a_pDevIns), &(a_pThis)->CritSectCache)
#endif  /* IOMMU_WITH_DTE_CACHE */

/** Acquires the IOMMU lock (returns a_rcBusy on contention). */
#define IOMMU_LOCK_RET(a_pDevIns, a_pThisCC, a_rcBusy)  \
    do { \
        int const rcLock = (a_pThisCC)->CTX_SUFF(pIommuHlp)->pfnLock((a_pDevIns), (a_rcBusy)); \
        if (RT_LIKELY(rcLock == VINF_SUCCESS)) \
        { /* likely */ } \
        else \
            return rcLock; \
    } while (0)

/** Acquires the IOMMU lock (can fail under extraordinary circumstance in ring-0). */
#define IOMMU_LOCK(a_pDevIns, a_pThisCC) \
    do { \
        int const rcLock = (a_pThisCC)->CTX_SUFF(pIommuHlp)->pfnLock((a_pDevIns), VINF_SUCCESS); \
        PDM_CRITSECT_RELEASE_ASSERT_RC_DEV((a_pDevIns), NULL, rcLock); \
    } while (0)

/** Checks if the current thread owns the PDM lock. */
# define IOMMU_ASSERT_LOCK_IS_OWNER(a_pDevIns, a_pThisCC) \
    do \
    { \
        Assert((a_pThisCC)->CTX_SUFF(pIommuHlp)->pfnLockIsOwner((a_pDevIns))); \
        NOREF(a_pThisCC); \
    } while (0)

/** Releases the PDM lock.   */
# define IOMMU_UNLOCK(a_pDevIns, a_pThisCC)         (a_pThisCC)->CTX_SUFF(pIommuHlp)->pfnUnlock((a_pDevIns))

/** Gets the maximum valid IOVA for the given I/O page-table level. */
#define IOMMU_GET_MAX_VALID_IOVA(a_Level)           ((X86_PAGE_4K_SIZE << ((a_Level) * 9)) - 1)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * IOMMU operation (transaction).
 */
typedef enum IOMMUOP
{
    /** Address translation request. */
    IOMMUOP_TRANSLATE_REQ = 0,
    /** Memory read request. */
    IOMMUOP_MEM_READ,
    /** Memory write request. */
    IOMMUOP_MEM_WRITE,
    /** Interrupt request. */
    IOMMUOP_INTR_REQ,
    /** Command. */
    IOMMUOP_CMD
} IOMMUOP;
/** Pointer to a IOMMU operation. */
typedef IOMMUOP *PIOMMUOP;

/**
 * I/O page lookup.
 */
typedef struct IOPAGELOOKUP
{
    /** The translated system physical address. */
    RTGCPHYS        GCPhysSpa;
    /** The number of offset bits in the system physical address. */
    uint8_t         cShift;
    /** The I/O permissions for this translation, see IOMMU_IO_PERM_XXX. */
    uint8_t         fPerm;
} IOPAGELOOKUP;
/** Pointer to an I/O page lookup. */
typedef IOPAGELOOKUP *PIOPAGELOOKUP;
/** Pointer to a const I/O page lookup. */
typedef IOPAGELOOKUP const *PCIOPAGELOOKUP;

/**
 * I/O address range.
 */
typedef struct IOADDRRANGE
{
    /** The address (virtual or physical). */
    uint64_t        uAddr;
    /** The size of the access in bytes. */
    size_t          cb;
    /** The I/O permissions for this translation, see IOMMU_IO_PERM_XXX. */
    uint8_t         fPerm;
} IOADDRRANGE;
/** Pointer to an I/O address range. */
typedef IOADDRRANGE *PIOADDRRANGE;
/** Pointer to a const I/O address range. */
typedef IOADDRRANGE const *PCIOADDRRANGE;

#ifdef IOMMU_WITH_DTE_CACHE
/**
 * Device Table Entry Cache.
 */
typedef struct DTECACHE
{
    /** This device's flags, see IOMMU_DTE_CACHE_F_XXX. */
    uint16_t        fFlags;
    /** The domain ID assigned for this device by software. */
    uint16_t        idDomain;
} DTECACHE;
/** Pointer to an I/O device struct. */
typedef DTECACHE *PDTECACHE;
/** Pointer to a const I/O device struct. */
typedef DTECACHE *PCDTECACHE;
AssertCompileSize(DTECACHE, 4);
#endif  /* IOMMU_WITH_DTE_CACHE */

#ifdef IOMMU_WITH_IOTLBE_CACHE
/**
 * I/O TLB Entry.
 * Keep this as small and aligned as possible.
 */
typedef struct IOTLBE
{
    /** The AVL tree node. */
    AVLU64NODECORE      Core;
    /** The least recently used (LRU) list node. */
    RTLISTNODE          NdLru;
    /** The I/O page lookup results of the translation. */
    IOPAGELOOKUP        PageLookup;
    /** Whether the entry needs to be evicted from the cache. */
    bool                fEvictPending;
} IOTLBE;
/** Pointer to an IOMMU I/O TLB entry struct. */
typedef IOTLBE *PIOTLBE;
/** Pointer to a const IOMMU I/O TLB entry struct. */
typedef IOTLBE const *PCIOTLBE;
AssertCompileSizeAlignment(IOTLBE, 8);
AssertCompileMemberOffset(IOTLBE, Core, 0);
#endif  /* IOMMU_WITH_IOTLBE_CACHE */

#ifdef IOMMU_WITH_IRTE_CACHE
/**
 * Interrupt Remap Table Entry Cache.
 */
typedef struct IRTECACHE
{
    /** The key, see IOMMU_IRTE_CACHE_KEY_MAKE. */
    uint32_t            uKey;
    /** The IRTE. */
    IRTE_T              Irte;
} IRTECACHE;
/** Pointer to an IRTE cache struct. */
typedef IRTECACHE *PIRTECACHE;
/** Pointer to a const IRTE cache struct. */
typedef IRTECACHE const *PCIRTECACHE;
AssertCompileSizeAlignment(IRTECACHE, 4);
#endif /* IOMMU_WITH_IRTE_CACHE */

/**
 * The shared IOMMU device state.
 */
typedef struct IOMMU
{
    /** IOMMU device index (0 is at the top of the PCI tree hierarchy). */
    uint32_t                    idxIommu;
    /** IOMMU magic. */
    uint32_t                    u32Magic;

    /** The MMIO handle. */
    IOMMMIOHANDLE               hMmio;
    /** The event semaphore the command thread waits on. */
    SUPSEMEVENT                 hEvtCmdThread;
    /** Whether the command thread has been signaled for wake up. */
    bool volatile               fCmdThreadSignaled;
    /** Padding. */
    bool                        afPadding0[3];
    /** The IOMMU PCI address. */
    PCIBDF                      uPciAddress;

#ifdef IOMMU_WITH_DTE_CACHE
    /** The critsect that protects the cache from concurrent access. */
    PDMCRITSECT                 CritSectCache;
    /** Array of device IDs. */
    uint16_t                    aDeviceIds[IOMMU_DEV_CACHE_COUNT];
    /** Array of DTE cache entries. */
    DTECACHE                    aDteCache[IOMMU_DEV_CACHE_COUNT];
#endif
#ifdef IOMMU_WITH_IRTE_CACHE
    /** Array of IRTE cache entries. */
    IRTECACHE                   aIrteCache[IOMMU_IRTE_CACHE_COUNT];
#endif

    /** @name PCI: Base capability block registers.
     * @{ */
    IOMMU_BAR_T                 IommuBar;               /**< IOMMU base address register. */
    /** @} */

    /** @name MMIO: Control and status registers.
     * @{ */
    DEV_TAB_BAR_T               aDevTabBaseAddrs[8];    /**< Device table base address registers. */
    CMD_BUF_BAR_T               CmdBufBaseAddr;         /**< Command buffer base address register. */
    EVT_LOG_BAR_T               EvtLogBaseAddr;         /**< Event log base address register. */
    IOMMU_CTRL_T                Ctrl;                   /**< IOMMU control register. */
    IOMMU_EXCL_RANGE_BAR_T      ExclRangeBaseAddr;      /**< IOMMU exclusion range base register. */
    IOMMU_EXCL_RANGE_LIMIT_T    ExclRangeLimit;         /**< IOMMU exclusion range limit. */
    IOMMU_EXT_FEAT_T            ExtFeat;                /**< IOMMU extended feature register. */
    /** @} */

    /** @name MMIO: Peripheral Page Request (PPR) Log registers.
     * @{ */
    PPR_LOG_BAR_T               PprLogBaseAddr;         /**< PPR Log base address register. */
    IOMMU_HW_EVT_HI_T           HwEvtHi;                /**< IOMMU hardware event register (Hi). */
    IOMMU_HW_EVT_LO_T           HwEvtLo;                /**< IOMMU hardware event register (Lo). */
    IOMMU_HW_EVT_STATUS_T       HwEvtStatus;            /**< IOMMU hardware event status. */
    /** @} */

    /** @todo IOMMU: SMI filter. */

    /** @name MMIO: Guest Virtual-APIC Log registers.
     * @{ */
    GALOG_BAR_T                 GALogBaseAddr;          /**< Guest Virtual-APIC Log base address register. */
    GALOG_TAIL_ADDR_T           GALogTailAddr;          /**< Guest Virtual-APIC Log Tail address register. */
    /** @} */

    /** @name MMIO: Alternate PPR and Event Log registers.
     *  @{ */
    PPR_LOG_B_BAR_T             PprLogBBaseAddr;        /**< PPR Log B base address register. */
    EVT_LOG_B_BAR_T             EvtLogBBaseAddr;        /**< Event Log B base address register. */
    /** @} */

    /** @name MMIO: Device-specific feature registers.
     * @{ */
    DEV_SPECIFIC_FEAT_T         DevSpecificFeat;        /**< Device-specific feature extension register (DSFX). */
    DEV_SPECIFIC_CTRL_T         DevSpecificCtrl;        /**< Device-specific control extension register (DSCX). */
    DEV_SPECIFIC_STATUS_T       DevSpecificStatus;      /**< Device-specific status extension register (DSSX). */
    /** @} */

    /** @name MMIO: MSI Capability Block registers.
     * @{ */
    MSI_MISC_INFO_T             MiscInfo;               /**< MSI Misc. info registers / MSI Vector registers. */
    /** @} */

    /** @name MMIO: Performance Optimization Control registers.
     *  @{ */
    IOMMU_PERF_OPT_CTRL_T       PerfOptCtrl;            /**< IOMMU Performance optimization control register. */
    /** @} */

    /** @name MMIO: x2APIC Control registers.
     * @{ */
    IOMMU_XT_GEN_INTR_CTRL_T    XtGenIntrCtrl;          /**< IOMMU X2APIC General interrupt control register. */
    IOMMU_XT_PPR_INTR_CTRL_T    XtPprIntrCtrl;          /**< IOMMU X2APIC PPR interrupt control register. */
    IOMMU_XT_GALOG_INTR_CTRL_T  XtGALogIntrCtrl;        /**< IOMMU X2APIC Guest Log interrupt control register. */
    /** @} */

    /** @name MMIO: Memory Address Routing & Control (MARC) registers.
     * @{ */
    MARC_APER_T                 aMarcApers[4];          /**< MARC Aperture Registers. */
    /** @} */

    /** @name MMIO: Reserved register.
     *  @{ */
    IOMMU_RSVD_REG_T            RsvdReg;                /**< IOMMU Reserved Register. */
    /** @} */

    /** @name MMIO: Command and Event Log pointer registers.
     * @{ */
    CMD_BUF_HEAD_PTR_T          CmdBufHeadPtr;          /**< Command buffer head pointer register. */
    CMD_BUF_TAIL_PTR_T          CmdBufTailPtr;          /**< Command buffer tail pointer register. */
    EVT_LOG_HEAD_PTR_T          EvtLogHeadPtr;          /**< Event log head pointer register. */
    EVT_LOG_TAIL_PTR_T          EvtLogTailPtr;          /**< Event log tail pointer register. */
    /** @} */

    /** @name MMIO: Command and Event Status register.
     * @{ */
    IOMMU_STATUS_T              Status;                 /**< IOMMU status register. */
    /** @} */

    /** @name MMIO: PPR Log Head and Tail pointer registers.
     * @{ */
    PPR_LOG_HEAD_PTR_T          PprLogHeadPtr;          /**< IOMMU PPR log head pointer register. */
    PPR_LOG_TAIL_PTR_T          PprLogTailPtr;          /**< IOMMU PPR log tail pointer register. */
    /** @} */

    /** @name MMIO: Guest Virtual-APIC Log Head and Tail pointer registers.
     * @{ */
    GALOG_HEAD_PTR_T            GALogHeadPtr;           /**< Guest Virtual-APIC log head pointer register. */
    GALOG_TAIL_PTR_T            GALogTailPtr;           /**< Guest Virtual-APIC log tail pointer register. */
    /** @} */

    /** @name MMIO: PPR Log B Head and Tail pointer registers.
     *  @{ */
    PPR_LOG_B_HEAD_PTR_T        PprLogBHeadPtr;         /**< PPR log B head pointer register. */
    PPR_LOG_B_TAIL_PTR_T        PprLogBTailPtr;         /**< PPR log B tail pointer register. */
    /** @} */

    /** @name MMIO: Event Log B Head and Tail pointer registers.
     * @{ */
    EVT_LOG_B_HEAD_PTR_T        EvtLogBHeadPtr;         /**< Event log B head pointer register. */
    EVT_LOG_B_TAIL_PTR_T        EvtLogBTailPtr;         /**< Event log B tail pointer register. */
    /** @} */

    /** @name MMIO: PPR Log Overflow protection registers.
     * @{ */
    PPR_LOG_AUTO_RESP_T         PprLogAutoResp;         /**< PPR Log Auto Response register. */
    PPR_LOG_OVERFLOW_EARLY_T    PprLogOverflowEarly;    /**< PPR Log Overflow Early Indicator register. */
    PPR_LOG_B_OVERFLOW_EARLY_T  PprLogBOverflowEarly;   /**< PPR Log B Overflow Early Indicator register. */
    /** @} */

    /** @todo IOMMU: IOMMU Event counter registers. */

#ifdef VBOX_WITH_STATISTICS
    /** @name IOMMU: Stat counters.
     * @{ */
    STAMCOUNTER                 StatMmioReadR3;            /**< Number of MMIO reads in R3. */
    STAMCOUNTER                 StatMmioReadRZ;            /**< Number of MMIO reads in RZ. */
    STAMCOUNTER                 StatMmioWriteR3;           /**< Number of MMIO writes in R3. */
    STAMCOUNTER                 StatMmioWriteRZ;           /**< Number of MMIO writes in RZ. */

    STAMCOUNTER                 StatMsiRemapR3;            /**< Number of MSI remap requests in R3. */
    STAMCOUNTER                 StatMsiRemapRZ;            /**< Number of MSI remap requests in RZ. */

    STAMCOUNTER                 StatMemReadR3;             /**< Number of memory read translation requests in R3. */
    STAMCOUNTER                 StatMemReadRZ;             /**< Number of memory read translation requests in RZ. */
    STAMCOUNTER                 StatMemWriteR3;            /**< Number of memory write translation requests in R3. */
    STAMCOUNTER                 StatMemWriteRZ;            /**< Number of memory write translation requests in RZ. */

    STAMCOUNTER                 StatMemBulkReadR3;         /**< Number of memory read bulk translation requests in R3. */
    STAMCOUNTER                 StatMemBulkReadRZ;         /**< Number of memory read bulk translation requests in RZ. */
    STAMCOUNTER                 StatMemBulkWriteR3;        /**< Number of memory write bulk translation requests in R3. */
    STAMCOUNTER                 StatMemBulkWriteRZ;        /**< Number of memory write bulk translation requests in RZ. */

    STAMCOUNTER                 StatCmd;                   /**< Number of commands processed in total. */
    STAMCOUNTER                 StatCmdCompWait;           /**< Number of Completion Wait commands processed. */
    STAMCOUNTER                 StatCmdInvDte;             /**< Number of Invalidate DTE commands processed. */
    STAMCOUNTER                 StatCmdInvIommuPages;      /**< Number of Invalidate IOMMU pages commands processed. */
    STAMCOUNTER                 StatCmdInvIotlbPages;      /**< Number of Invalidate IOTLB pages commands processed. */
    STAMCOUNTER                 StatCmdInvIntrTable;       /**< Number of Invalidate Interrupt Table commands processed. */
    STAMCOUNTER                 StatCmdPrefIommuPages;     /**< Number of Prefetch IOMMU Pages commands processed. */
    STAMCOUNTER                 StatCmdCompletePprReq;     /**< Number of Complete PPR Requests commands processed. */
    STAMCOUNTER                 StatCmdInvIommuAll;        /**< Number of Invalidate IOMMU All commands processed. */

    STAMCOUNTER                 StatIotlbeCached;          /**< Number of IOTLB entries in the cache. */
    STAMCOUNTER                 StatIotlbeLazyEvictReuse;  /**< Number of IOTLB entries re-used after lazy eviction. */

    STAMPROFILEADV              StatProfDteLookup;         /**< Profiling of I/O page walk (from memory). */
    STAMPROFILEADV              StatProfIotlbeLookup;      /**< Profiling of IOTLB entry lookup (from cache). */

    STAMPROFILEADV              StatProfIrteLookup;        /**< Profiling of IRTE entry lookup (from memory). */
    STAMPROFILEADV              StatProfIrteCacheLookup;   /**< Profiling of IRTE entry lookup (from cache). */

    STAMCOUNTER                 StatAccessCacheHit;        /**< Number of IOTLB cache hits. */
    STAMCOUNTER                 StatAccessCacheHitFull;    /**< Number of accesses that were fully looked up from the cache. */
    STAMCOUNTER                 StatAccessCacheMiss;       /**< Number of cache misses (resulting in DTE lookups). */
    STAMCOUNTER                 StatAccessCacheNonContig;  /**< Number of cache accesses resulting in non-contiguous access. */
    STAMCOUNTER                 StatAccessCachePermDenied; /**< Number of cache accesses resulting in insufficient permissions. */
    STAMCOUNTER                 StatAccessDteNonContig;    /**< Number of DTE accesses resulting in non-contiguous access. */
    STAMCOUNTER                 StatAccessDtePermDenied;   /**< Number of DTE accesses resulting in insufficient permissions. */

    STAMCOUNTER                 StatIntrCacheHit;          /**< Number of interrupt cache hits. */
    STAMCOUNTER                 StatIntrCacheMiss;         /**< Number of interrupt cache misses. */

    STAMCOUNTER                 StatNonStdPageSize;        /**< Number of non-standard page size translations. */
    STAMCOUNTER                 StatIopfs;                 /**< Number of I/O page faults. */
    /** @} */
#endif
} IOMMU;
/** Pointer to the IOMMU device state. */
typedef IOMMU *PIOMMU;
/** Pointer to the const IOMMU device state. */
typedef const IOMMU *PCIOMMU;
AssertCompileMemberAlignment(IOMMU, hMmio, 8);
#ifdef IOMMU_WITH_DTE_CACHE
AssertCompileMemberAlignment(IOMMU, CritSectCache, 8);
AssertCompileMemberAlignment(IOMMU, aDeviceIds, 8);
AssertCompileMemberAlignment(IOMMU, aDteCache, 8);
#endif
#ifdef IOMMU_WITH_IRTE_CACHE
AssertCompileMemberAlignment(IOMMU, aIrteCache, 8);
#endif
AssertCompileMemberAlignment(IOMMU, IommuBar, 8);
AssertCompileMemberAlignment(IOMMU, aDevTabBaseAddrs, 8);
AssertCompileMemberAlignment(IOMMU, CmdBufHeadPtr, 8);
AssertCompileMemberAlignment(IOMMU, Status, 8);

/**
 * The ring-3 IOMMU device state.
 */
typedef struct IOMMUR3
{
    /** Device instance. */
    PPDMDEVINSR3                pDevInsR3;
    /** The IOMMU helpers. */
    R3PTRTYPE(PCPDMIOMMUHLPR3)  pIommuHlpR3;
    /** The command thread handle. */
    R3PTRTYPE(PPDMTHREAD)       pCmdThread;
#ifdef IOMMU_WITH_IOTLBE_CACHE
    /** Pointer to array of pre-allocated IOTLBEs. */
    PIOTLBE                     paIotlbes;
    /** Maps [DomainId,Iova] to [IOTLBE]. */
    AVLU64TREE                  TreeIotlbe;
    /** LRU list anchor for IOTLB entries. */
    RTLISTANCHOR                LstLruIotlbe;
    /** Index of the next unused IOTLB. */
    uint32_t                    idxUnusedIotlbe;
    /** Number of cached IOTLB entries in the tree. */
    uint32_t                    cCachedIotlbes;
#endif
} IOMMUR3;
/** Pointer to the ring-3 IOMMU device state. */
typedef IOMMUR3 *PIOMMUR3;
/** Pointer to the const ring-3 IOMMU device state. */
typedef const IOMMUR3 *PCIOMMUR3;
#ifdef IOMMU_WITH_IOTLBE_CACHE
AssertCompileMemberAlignment(IOMMUR3, paIotlbes, 8);
AssertCompileMemberAlignment(IOMMUR3, TreeIotlbe, 8);
AssertCompileMemberAlignment(IOMMUR3, LstLruIotlbe, 8);
#endif

/**
 * The ring-0 IOMMU device state.
 */
typedef struct IOMMUR0
{
    /** Device instance. */
    PPDMDEVINSR0                pDevInsR0;
    /** The IOMMU helpers. */
    R0PTRTYPE(PCPDMIOMMUHLPR0)  pIommuHlpR0;
} IOMMUR0;
/** Pointer to the ring-0 IOMMU device state. */
typedef IOMMUR0 *PIOMMUR0;

/**
 * The raw-mode IOMMU device state.
 */
typedef struct IOMMURC
{
    /** Device instance. */
    PPDMDEVINSRC                pDevInsRC;
    /** The IOMMU helpers. */
    RCPTRTYPE(PCPDMIOMMUHLPRC)  pIommuHlpRC;
} IOMMURC;
/** Pointer to the raw-mode IOMMU device state. */
typedef IOMMURC *PIOMMURC;

/** The IOMMU device state for the current context. */
typedef CTX_SUFF(IOMMU)  IOMMUCC;
/** Pointer to the IOMMU device state for the current context. */
typedef CTX_SUFF(PIOMMU) PIOMMUCC;

/**
 * IOMMU register access.
 */
typedef struct IOMMUREGACC
{
    const char   *pszName;
    VBOXSTRICTRC (*pfnRead)(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value);
    VBOXSTRICTRC (*pfnWrite)(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value);
} IOMMUREGACC;
/** Pointer to an IOMMU register access. */
typedef IOMMUREGACC *PIOMMUREGACC;
/** Pointer to a const IOMMU register access. */
typedef IOMMUREGACC const *PCIOMMUREGACC;

#ifdef IOMMU_WITH_IOTLBE_CACHE
/**
 * IOTLBE flush argument.
 */
typedef struct IOTLBEFLUSHARG
{
    /** The ring-3 IOMMU device state. */
    PIOMMUR3            pIommuR3;
    /** The domain ID to flush. */
    uint16_t            idDomain;
} IOTLBEFLUSHARG;
/** Pointer to an IOTLBE flush argument. */
typedef IOTLBEFLUSHARG *PIOTLBEFLUSHARG;
/** Pointer to a const IOTLBE flush argument. */
typedef IOTLBEFLUSHARG const *PCIOTLBEFLUSHARG;

/**
 * IOTLBE Info. argument.
 */
typedef struct IOTLBEINFOARG
{
    /** The ring-3 IOMMU device state. */
    PIOMMUR3            pIommuR3;
    /** The info helper. */
    PCDBGFINFOHLP       pHlp;
    /** The domain ID to dump IOTLB entry. */
    uint16_t            idDomain;
} IOTLBEINFOARG;
/** Pointer to an IOTLBE flush argument. */
typedef IOTLBEINFOARG *PIOTLBEINFOARG;
/** Pointer to a const IOTLBE flush argument. */
typedef IOTLBEINFOARG const *PCIOTLBEINFOARG;
#endif

/**
 * IOMMU operation auxiliary info.
 */
typedef struct IOMMUOPAUX
{
    /** The IOMMU operation being performed. */
    IOMMUOP         enmOp;
    /** The device table entry (can be NULL). */
    PCDTE_T         pDte;
    /** The device ID (bus, device, function). */
    uint16_t        idDevice;
    /** The domain ID (when the DTE isn't provided). */
    uint16_t        idDomain;
} IOMMUOPAUX;
/** Pointer to an I/O address lookup struct. */
typedef IOMMUOPAUX *PIOMMUOPAUX;
/** Pointer to a const I/O address lookup struct. */
typedef IOMMUOPAUX const *PCIOMMUOPAUX;

typedef DECLCALLBACKTYPE(int, FNIOPAGELOOKUP,(PPDMDEVINS pDevIns, uint64_t uIovaPage, uint8_t fPerm, PCIOMMUOPAUX pAux,
                                              PIOPAGELOOKUP pPageLookup));
typedef FNIOPAGELOOKUP *PFNIOPAGELOOKUP;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
#ifdef IN_RING3
/**
 * An array of the number of device table segments supported.
 * Indexed by u2DevTabSegSup.
 */
static uint8_t const g_acDevTabSegs[] = { 0, 2, 4, 8 };
#endif

#if (defined(IN_RING3) && defined(IOMMU_WITH_IOTLBE_CACHE)) || defined(LOG_ENABLED)
/**
 * The IOMMU I/O permission names.
 */
static const char * const g_aszPerm[] = { "none", "read", "write", "read+write" };
#endif

/**
 * An array of the masks to select the device table segment index from a device ID.
 */
static uint16_t const g_auDevTabSegMasks[] = { 0x0, 0x8000, 0xc000, 0xe000 };

/**
 * An array of the shift values to select the device table segment index from a
 * device ID.
 */
static uint8_t const g_auDevTabSegShifts[] = { 0, 15, 14, 13 };

/**
 * The maximum size (inclusive) of each device table segment (0 to 7).
 * Indexed by the device table segment index.
 */
static uint16_t const g_auDevTabSegMaxSizes[] = { 0x1ff, 0xff, 0x7f, 0x7f, 0x3f, 0x3f, 0x3f, 0x3f };


#ifndef VBOX_DEVICE_STRUCT_TESTCASE
/**
 * Gets the maximum number of buffer entries for the given buffer length.
 *
 * @returns Number of buffer entries.
 * @param   uEncodedLen     The length (power-of-2 encoded).
 */
DECLINLINE(uint32_t) iommuAmdGetBufMaxEntries(uint8_t uEncodedLen)
{
    Assert(uEncodedLen > 7);
    Assert(uEncodedLen < 16);
    return 2 << (uEncodedLen - 1);
}


/**
 * Gets the total length of the buffer given a base register's encoded length.
 *
 * @returns The length of the buffer in bytes.
 * @param   uEncodedLen     The length (power-of-2 encoded).
 */
DECLINLINE(uint32_t) iommuAmdGetTotalBufLength(uint8_t uEncodedLen)
{
    Assert(uEncodedLen > 7);
    Assert(uEncodedLen < 16);
    return (2 << (uEncodedLen - 1)) << 4;
}


/**
 * Gets the number of (unconsumed) entries in the event log.
 *
 * @returns The number of entries in the event log.
 * @param   pThis   The shared IOMMU device state.
 */
static uint32_t iommuAmdGetEvtLogEntryCount(PIOMMU pThis)
{
    uint32_t const idxTail = pThis->EvtLogTailPtr.n.off >> IOMMU_EVT_GENERIC_SHIFT;
    uint32_t const idxHead = pThis->EvtLogHeadPtr.n.off >> IOMMU_EVT_GENERIC_SHIFT;
    if (idxTail >= idxHead)
        return idxTail - idxHead;

    uint32_t const cMaxEvts = iommuAmdGetBufMaxEntries(pThis->EvtLogBaseAddr.n.u4Len);
    return cMaxEvts - idxHead + idxTail;
}


#if (defined(IN_RING3) && defined(IOMMU_WITH_IOTLBE_CACHE)) || defined(LOG_ENABLED)
/**
 * Gets the descriptive I/O permission name for a memory access.
 *
 * @returns The I/O permission name.
 * @param   fPerm   The I/O permissions for the access, see IOMMU_IO_PERM_XXX.
 */
static const char *iommuAmdMemAccessGetPermName(uint8_t fPerm)
{
    /* We shouldn't construct an access with "none" or "read+write" (must be read or write) permissions. */
    Assert(fPerm > 0 && fPerm < RT_ELEMENTS(g_aszPerm));
    return g_aszPerm[fPerm & IOMMU_IO_PERM_MASK];
}
#endif


#ifdef IOMMU_WITH_DTE_CACHE
/**
 * Gets the basic I/O device flags for the given device table entry.
 *
 * @returns The basic I/O device flags.
 * @param   pDte    The device table entry.
 */
static uint16_t iommuAmdGetBasicDevFlags(PCDTE_T pDte)
{
    /* Extract basic flags from bits 127:0 of the DTE. */
    uint16_t fFlags = 0;
    if (pDte->n.u1Valid)
    {
        fFlags |= IOMMU_DTE_CACHE_F_VALID;

        /** @todo Skip the if checks here (shift/mask the relevant bits over).  */
        if (pDte->n.u1SuppressAllPfEvents)
            fFlags |= IOMMU_DTE_CACHE_F_SUPPRESS_ALL_IOPF;
        if (pDte->n.u1SuppressPfEvents)
            fFlags |= IOMMU_DTE_CACHE_F_SUPPRESS_IOPF;

        uint16_t const fDtePerm = (pDte->au64[0] >> IOMMU_IO_PERM_SHIFT) & IOMMU_IO_PERM_MASK;
        AssertCompile(IOMMU_DTE_CACHE_F_IO_PERM_MASK == IOMMU_IO_PERM_MASK);
        fFlags |= fDtePerm << IOMMU_DTE_CACHE_F_IO_PERM_SHIFT;
    }

    /* Extract basic flags from bits 255:128 of the DTE. */
    if (pDte->n.u1IntrMapValid)
    {
        fFlags |= IOMMU_DTE_CACHE_F_INTR_MAP_VALID;

        /** @todo Skip the if check here (shift/mask the relevant bit over).  */
        if (pDte->n.u1IgnoreUnmappedIntrs)
            fFlags |= IOMMU_DTE_CACHE_F_IGNORE_UNMAPPED_INTR;

        uint16_t const fIntrCtrl = IOMMU_DTE_GET_INTR_CTRL(pDte);
        AssertCompile(IOMMU_DTE_CACHE_F_INTR_CTRL_MASK == IOMMU_DTE_INTR_CTRL_MASK);
        fFlags |= fIntrCtrl << IOMMU_DTE_CACHE_F_INTR_CTRL_SHIFT;
    }
    return fFlags;
}
#endif


/**
 * Remaps the source MSI to the destination MSI given the IRTE.
 *
 * @param   pMsiIn      The source MSI.
 * @param   pMsiOut     Where to store the remapped MSI.
 * @param   pIrte       The IRTE used for the remapping.
 */
static void iommuAmdIrteRemapMsi(PCMSIMSG pMsiIn, PMSIMSG pMsiOut, PCIRTE_T pIrte)
{
    /* Preserve all bits from the source MSI address and data that don't map 1:1 from the IRTE. */
    *pMsiOut = *pMsiIn;

    pMsiOut->Addr.n.u1DestMode = pIrte->n.u1DestMode;
    pMsiOut->Addr.n.u8DestId   = pIrte->n.u8Dest;

    pMsiOut->Data.n.u8Vector       = pIrte->n.u8Vector;
    pMsiOut->Data.n.u3DeliveryMode = pIrte->n.u3IntrType;
}


#ifdef IOMMU_WITH_DTE_CACHE
/**
 * Looks up an entry in the DTE cache for the given device ID.
 *
 * @returns The index of the entry, or the cache capacity if no entry was found.
 * @param   pThis       The shared IOMMU device state.
 * @param   idDevice    The device ID (bus, device, function).
 */
DECLINLINE(uint16_t) iommuAmdDteCacheEntryLookup(PIOMMU pThis, uint16_t idDevice)
{
    uint16_t const cDeviceIds = RT_ELEMENTS(pThis->aDeviceIds);
    for (uint16_t i = 0; i < cDeviceIds; i++)
    {
        if (pThis->aDeviceIds[i] == idDevice)
            return i;
    }
    return cDeviceIds;
}


/**
 * Gets an free/unused DTE cache entry.
 *
 * @returns The index of an unused entry, or cache capacity if the cache is full.
 * @param   pThis   The shared IOMMU device state.
 */
DECLINLINE(uint16_t) iommuAmdDteCacheEntryGetUnused(PCIOMMU pThis)
{
    /*
     * ASSUMES device ID 0 is the PCI host bridge or the IOMMU itself
     * (the latter being an ugly hack) and cannot be a valid device ID.
     */
    uint16_t const cDeviceIds = RT_ELEMENTS(pThis->aDeviceIds);
    for (uint16_t i = 0; i < cDeviceIds; i++)
    {
        if (!pThis->aDeviceIds[i])
            return i;
    }
    return cDeviceIds;
}


/**
 * Adds a DTE cache entry at the given index.
 *
 * @param   pThis       The shared IOMMU device state.
 * @param   idxDte      The index of the DTE cache entry.
 * @param   idDevice    The device ID (bus, device, function).
 * @param   fFlags      Device flags to set, see IOMMU_DTE_CACHE_F_XXX.
 * @param   idDomain    The domain ID.
 *
 * @remarks Requires the cache lock to be taken.
 */
DECL_FORCE_INLINE(void) iommuAmdDteCacheAddAtIndex(PIOMMU pThis, uint16_t idxDte, uint16_t idDevice, uint16_t fFlags,
                                                   uint16_t idDomain)
{
    pThis->aDeviceIds[idxDte]         = idDevice;
    pThis->aDteCache[idxDte].fFlags   = fFlags;
    pThis->aDteCache[idxDte].idDomain = idDomain;
}


/**
 * Adds a DTE cache entry.
 *
 * @param   pDevIns     The IOMMU instance data.
 * @param   idDevice    The device ID (bus, device, function).
 * @param   pDte        The device table entry.
 */
static void iommuAmdDteCacheAdd(PPDMDEVINS pDevIns, uint16_t idDevice, PCDTE_T pDte)
{
    uint16_t const fFlags   = iommuAmdGetBasicDevFlags(pDte) | IOMMU_DTE_CACHE_F_PRESENT;
    uint16_t const idDomain = pDte->n.u16DomainId;

    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    IOMMU_CACHE_LOCK(pDevIns, pThis);

    uint16_t const cDteCache = RT_ELEMENTS(pThis->aDteCache);
    uint16_t idxDte = iommuAmdDteCacheEntryLookup(pThis, idDevice);
    if (   idxDte >= cDteCache                                              /* Not found. */
        && (idxDte = iommuAmdDteCacheEntryGetUnused(pThis)) < cDteCache)    /* Get new/unused slot index. */
        iommuAmdDteCacheAddAtIndex(pThis, idxDte, idDevice, fFlags, idDomain);

    IOMMU_CACHE_UNLOCK(pDevIns, pThis);
}


/**
 * Updates flags for an existing DTE cache entry given its index.
 *
 * @param   pThis       The shared IOMMU device state.
 * @param   idxDte      The index of the DTE cache entry.
 * @param   fOrMask     Device flags to add to the existing flags, see
 *                      IOMMU_DTE_CACHE_F_XXX.
 * @param   fAndMask    Device flags to remove from the existing flags, see
 *                      IOMMU_DTE_CACHE_F_XXX.
 *
 * @remarks Requires the cache lock to be taken.
 */
DECL_FORCE_INLINE(void) iommuAmdDteCacheUpdateFlagsForIndex(PIOMMU pThis, uint16_t idxDte, uint16_t fOrMask, uint16_t fAndMask)
{
    uint16_t const fOldFlags = pThis->aDteCache[idxDte].fFlags;
    uint16_t const fNewFlags = (fOldFlags | fOrMask) & ~fAndMask;
    Assert(fOldFlags & IOMMU_DTE_CACHE_F_PRESENT);
    pThis->aDteCache[idxDte].fFlags = fNewFlags;
}


#ifdef IOMMU_WITH_IOTLBE_CACHE
/**
 * Adds a new DTE cache entry or updates flags for an existing DTE cache entry.
 * If the cache is full, nothing happens.
 *
 * @param   pDevIns     The IOMMU instance data.
 * @param   pDte                The device table entry.
 * @param   idDevice    The device ID (bus, device, function).
 * @param   fOrMask     Device flags to add to the existing flags, see
 *                      IOMMU_DTE_CACHE_F_XXX.
 * @param   fAndMask    Device flags to remove from the existing flags, see
 *                      IOMMU_DTE_CACHE_F_XXX.
 */
static void iommuAmdDteCacheAddOrUpdateFlags(PPDMDEVINS pDevIns, PCDTE_T pDte, uint16_t idDevice, uint16_t fOrMask,
                                             uint16_t fAndMask)
{
    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    IOMMU_CACHE_LOCK(pDevIns, pThis);

    uint16_t const cDteCache = RT_ELEMENTS(pThis->aDteCache);
    uint16_t idxDte = iommuAmdDteCacheEntryLookup(pThis, idDevice);
    if (idxDte < cDteCache)
        iommuAmdDteCacheUpdateFlagsForIndex(pThis, idxDte, fOrMask, fAndMask);
    else if ((idxDte = iommuAmdDteCacheEntryGetUnused(pThis)) < cDteCache)
    {
        uint16_t const fFlags = (iommuAmdGetBasicDevFlags(pDte) | IOMMU_DTE_CACHE_F_PRESENT | fOrMask) & ~fAndMask;
        iommuAmdDteCacheAddAtIndex(pThis, idxDte, idDevice, fFlags, pDte->n.u16DomainId);
    }
    /* else: cache is full, shouldn't really happen. */

    IOMMU_CACHE_UNLOCK(pDevIns, pThis);
}
#endif


/**
 * Updates flags for an existing DTE cache entry.
 *
 * @param   pDevIns     The IOMMU instance data.
 * @param   idDevice    The device ID (bus, device, function).
 * @param   fOrMask     Device flags to add to the existing flags, see
 *                      IOMMU_DTE_CACHE_F_XXX.
 * @param   fAndMask    Device flags to remove from the existing flags, see
 *                      IOMMU_DTE_CACHE_F_XXX.
 */
static void iommuAmdDteCacheUpdateFlags(PPDMDEVINS pDevIns, uint16_t idDevice, uint16_t fOrMask, uint16_t fAndMask)
{
    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    IOMMU_CACHE_LOCK(pDevIns, pThis);

    uint16_t const cDteCache = RT_ELEMENTS(pThis->aDteCache);
    uint16_t const idxDte = iommuAmdDteCacheEntryLookup(pThis, idDevice);
    if (idxDte < cDteCache)
        iommuAmdDteCacheUpdateFlagsForIndex(pThis, idxDte, fOrMask, fAndMask);

    IOMMU_CACHE_UNLOCK(pDevIns, pThis);
}


# ifdef IN_RING3
/**
 * Removes a DTE cache entry.
 *
 * @param   pDevIns     The IOMMU instance data.
 * @param   idDevice    The device ID to remove cache entries for.
 */
static void iommuAmdDteCacheRemove(PPDMDEVINS pDevIns, uint16_t idDevice)
{
    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    IOMMU_CACHE_LOCK(pDevIns, pThis);

    uint16_t const cDteCache = RT_ELEMENTS(pThis->aDteCache);
    uint16_t const idxDte    = iommuAmdDteCacheEntryLookup(pThis, idDevice);
    if (idxDte < cDteCache)
    {
        pThis->aDteCache[idxDte].fFlags   = 0;
        pThis->aDteCache[idxDte].idDomain = 0;
    }

    IOMMU_CACHE_UNLOCK(pDevIns, pThis);
}


/**
 * Removes all entries in the device table entry cache.
 *
 * @param   pDevIns     The IOMMU instance data.
 */
static void iommuAmdDteCacheRemoveAll(PPDMDEVINS pDevIns)
{
    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    IOMMU_CACHE_LOCK(pDevIns, pThis);
    RT_ZERO(pThis->aDeviceIds);
    RT_ZERO(pThis->aDteCache);
    IOMMU_CACHE_UNLOCK(pDevIns, pThis);
}
# endif  /* IN_RING3 */
#endif  /* IOMMU_WITH_DTE_CACHE */


#ifdef IOMMU_WITH_IOTLBE_CACHE
/**
 * Moves the IOTLB entry to the least recently used slot.
 *
 * @param   pThisR3     The ring-3 IOMMU device state.
 * @param   pIotlbe     The IOTLB entry to move.
 */
DECLINLINE(void) iommuAmdIotlbEntryMoveToLru(PIOMMUR3 pThisR3, PIOTLBE pIotlbe)
{
    if (!RTListNodeIsFirst(&pThisR3->LstLruIotlbe, &pIotlbe->NdLru))
    {
        RTListNodeRemove(&pIotlbe->NdLru);
        RTListPrepend(&pThisR3->LstLruIotlbe, &pIotlbe->NdLru);
    }
}


/**
 * Moves the IOTLB entry to the most recently used slot.
 *
 * @param   pThisR3     The ring-3 IOMMU device state.
 * @param   pIotlbe     The IOTLB entry to move.
 */
DECLINLINE(void) iommuAmdIotlbEntryMoveToMru(PIOMMUR3 pThisR3, PIOTLBE pIotlbe)
{
    if (!RTListNodeIsLast(&pThisR3->LstLruIotlbe, &pIotlbe->NdLru))
    {
        RTListNodeRemove(&pIotlbe->NdLru);
        RTListAppend(&pThisR3->LstLruIotlbe, &pIotlbe->NdLru);
    }
}


# ifdef IN_RING3
/**
 * Dumps the IOTLB entry via the debug info helper.
 *
 * @returns VINF_SUCCESS.
 * @param   pNode       Pointer to an IOTLB entry to dump info.
 * @param   pvUser      Pointer to an IOTLBEINFOARG.
 */
static DECLCALLBACK(int) iommuAmdR3IotlbEntryInfo(PAVLU64NODECORE pNode, void *pvUser)
{
    /* Validate. */
    PCIOTLBEINFOARG pArgs = (PCIOTLBEINFOARG)pvUser;
    AssertPtr(pArgs);
    AssertPtr(pArgs->pIommuR3);
    AssertPtr(pArgs->pHlp);
    //Assert(pArgs->pIommuR3->u32Magic == IOMMU_MAGIC);

    uint16_t const idDomain = IOMMU_IOTLB_KEY_GET_DOMAIN_ID(pNode->Key);
    if (idDomain == pArgs->idDomain)
    {
        PCIOTLBE pIotlbe = (PCIOTLBE)pNode;
        AVLU64KEY const  uKey          = pIotlbe->Core.Key;
        uint64_t const   uIova         = IOMMU_IOTLB_KEY_GET_IOVA(uKey);
        RTGCPHYS const   GCPhysSpa     = pIotlbe->PageLookup.GCPhysSpa;
        uint8_t const    cShift        = pIotlbe->PageLookup.cShift;
        size_t const     cbPage        = RT_BIT_64(cShift);
        uint8_t const    fPerm         = pIotlbe->PageLookup.fPerm;
        const char      *pszPerm       = iommuAmdMemAccessGetPermName(fPerm);
        bool const       fEvictPending = pIotlbe->fEvictPending;

        PCDBGFINFOHLP pHlp = pArgs->pHlp;
        pHlp->pfnPrintf(pHlp, " Key           = %#RX64 (%#RX64)\n", uKey, uIova);
        pHlp->pfnPrintf(pHlp, " GCPhys        = %#RGp\n",           GCPhysSpa);
        pHlp->pfnPrintf(pHlp, " cShift        = %u (%zu bytes)\n",  cShift, cbPage);
        pHlp->pfnPrintf(pHlp, " fPerm         = %#x (%s)\n",        fPerm, pszPerm);
        pHlp->pfnPrintf(pHlp, " fEvictPending = %RTbool\n",         fEvictPending);
    }

    return VINF_SUCCESS;
}
# endif /* IN_RING3 */


/**
 * Removes the IOTLB entry if it's associated with the specified domain ID.
 *
 * @returns VINF_SUCCESS.
 * @param   pNode       Pointer to an IOTLBE.
 * @param   pvUser      Pointer to an IOTLBEFLUSHARG containing the domain ID.
 */
static DECLCALLBACK(int) iommuAmdIotlbEntryRemoveDomainId(PAVLU64NODECORE pNode, void *pvUser)
{
    /* Validate. */
    PCIOTLBEFLUSHARG pArgs = (PCIOTLBEFLUSHARG)pvUser;
    AssertPtr(pArgs);
    AssertPtr(pArgs->pIommuR3);
    //Assert(pArgs->pIommuR3->u32Magic == IOMMU_MAGIC);

    uint16_t const idDomain = IOMMU_IOTLB_KEY_GET_DOMAIN_ID(pNode->Key);
    if (idDomain == pArgs->idDomain)
    {
        /* Mark this entry is as invalidated and needs to be evicted later. */
        PIOTLBE pIotlbe = (PIOTLBE)pNode;
        pIotlbe->fEvictPending = true;
        iommuAmdIotlbEntryMoveToLru(pArgs->pIommuR3, (PIOTLBE)pNode);
    }
    return VINF_SUCCESS;
}


/**
 * Destroys an IOTLB entry that's in the tree.
 *
 * @returns VINF_SUCCESS.
 * @param   pNode       Pointer to an IOTLBE.
 * @param   pvUser      Opaque data. Currently not used, will be NULL.
 */
static DECLCALLBACK(int) iommuAmdIotlbEntryDestroy(PAVLU64NODECORE pNode, void *pvUser)
{
    RT_NOREF(pvUser);
    PIOTLBE pIotlbe = (PIOTLBE)pNode;
    Assert(pIotlbe);
    pIotlbe->NdLru.pNext = NULL;
    pIotlbe->NdLru.pPrev = NULL;
    RT_ZERO(pIotlbe->PageLookup);
    pIotlbe->fEvictPending = false;
    return VINF_SUCCESS;
}


/**
 * Inserts an IOTLB entry into the cache.
 *
 * @param   pThis           The shared IOMMU device state.
 * @param   pThisR3         The ring-3 IOMMU device state.
 * @param   pIotlbe         The IOTLB entry to initialize and insert.
 * @param   idDomain        The domain ID.
 * @param   uIova           The I/O virtual address.
 * @param   pPageLookup     The I/O page lookup result of the access.
 */
static void iommuAmdIotlbEntryInsert(PIOMMU pThis, PIOMMUR3 pThisR3, PIOTLBE pIotlbe, uint16_t idDomain, uint64_t uIova,
                                     PCIOPAGELOOKUP pPageLookup)
{
    /* Initialize the IOTLB entry with results of the I/O page walk. */
    AVLU64KEY const uKey = IOMMU_IOTLB_KEY_MAKE(idDomain, uIova);
    Assert(uKey != IOMMU_IOTLB_KEY_NIL);

    /* Check if the entry already exists. */
    PIOTLBE pFound = (PIOTLBE)RTAvlU64Get(&pThisR3->TreeIotlbe, uKey);
    if (!pFound)
    {
        /* Insert the entry into the cache. */
        pIotlbe->Core.Key   = uKey;
        pIotlbe->PageLookup = *pPageLookup;
        Assert(!pIotlbe->fEvictPending);

        bool const fInserted = RTAvlU64Insert(&pThisR3->TreeIotlbe, &pIotlbe->Core);
        Assert(fInserted); NOREF(fInserted);
        Assert(pThisR3->cCachedIotlbes < IOMMU_IOTLBE_MAX);
        ++pThisR3->cCachedIotlbes;
        STAM_COUNTER_INC(&pThis->StatIotlbeCached); NOREF(pThis);
    }
    else
    {
        /* Update the existing entry. */
        Assert(pFound->Core.Key == uKey);
        if (pFound->fEvictPending)
        {
            pFound->fEvictPending = false;
            STAM_COUNTER_INC(&pThis->StatIotlbeLazyEvictReuse); NOREF(pThis);
        }
        pFound->PageLookup = *pPageLookup;
    }
}


/**
 * Removes an IOTLB entry from the cache for the given key.
 *
 * @returns Pointer to the removed IOTLB entry, NULL if the entry wasn't found in
 *          the tree.
 * @param   pThis       The shared IOMMU device state.
 * @param   pThisR3     The ring-3 IOMMU device state.
 * @param   uKey        The key of the IOTLB entry to remove.
 */
static PIOTLBE iommuAmdIotlbEntryRemove(PIOMMU pThis, PIOMMUR3 pThisR3, AVLU64KEY uKey)
{
    PIOTLBE pIotlbe = (PIOTLBE)RTAvlU64Remove(&pThisR3->TreeIotlbe, uKey);
    if (pIotlbe)
    {
        if (pIotlbe->fEvictPending)
            STAM_COUNTER_INC(&pThis->StatIotlbeLazyEvictReuse);

        RT_ZERO(pIotlbe->Core);
        RT_ZERO(pIotlbe->PageLookup);
        /* We must not erase the LRU node connections here! */
        pIotlbe->fEvictPending = false;
        Assert(pIotlbe->Core.Key == IOMMU_IOTLB_KEY_NIL);

        Assert(pThisR3->cCachedIotlbes > 0);
        --pThisR3->cCachedIotlbes;
        STAM_COUNTER_DEC(&pThis->StatIotlbeCached); NOREF(pThis);
    }
    return pIotlbe;
}


/**
 * Looks up an IOTLB from the cache.
 *
 * @returns Pointer to IOTLB entry if found, NULL otherwise.
 * @param   pThis       The shared IOMMU device state.
 * @param   pThisR3     The ring-3 IOMMU device state.
 * @param   idDomain    The domain ID.
 * @param   uIova       The I/O virtual address.
 */
static PIOTLBE iommuAmdIotlbLookup(PIOMMU pThis, PIOMMUR3 pThisR3, uint64_t idDomain, uint64_t uIova)
{
    RT_NOREF(pThis);

    uint64_t const uKey = IOMMU_IOTLB_KEY_MAKE(idDomain, uIova);
    PIOTLBE pIotlbe = (PIOTLBE)RTAvlU64Get(&pThisR3->TreeIotlbe, uKey);
    if (    pIotlbe
        && !pIotlbe->fEvictPending)
        return pIotlbe;

    /*
     * Domain Id wildcard invalidations only marks entries for eviction later but doesn't remove
     * them from the cache immediately. We found an entry pending eviction, just return that
     * nothing was found (rather than evicting now).
     */
    return NULL;
}


/**
 * Adds an IOTLB entry to the cache.
 *
 * @param   pThis           The shared IOMMU device state.
 * @param   pThisR3         The ring-3 IOMMU device state.
 * @param   idDomain        The domain ID.
 * @param   uIovaPage       The I/O virtual address (must be 4K aligned).
 * @param   pPageLookup     The I/O page lookup result of the access.
 */
static void iommuAmdIotlbAdd(PIOMMU pThis, PIOMMUR3 pThisR3, uint16_t idDomain, uint64_t uIovaPage, PCIOPAGELOOKUP pPageLookup)
{
    Assert(!(uIovaPage & X86_PAGE_4K_OFFSET_MASK));
    Assert(pPageLookup);
    Assert(pPageLookup->cShift <= 51);
    Assert(pPageLookup->fPerm != IOMMU_IO_PERM_NONE);

    /*
     * If there are no unused IOTLB entries, evict the LRU entry.
     * Otherwise, get a new IOTLB entry from the pre-allocated list.
     */
    if (pThisR3->idxUnusedIotlbe == IOMMU_IOTLBE_MAX)
    {
        /* Grab the least recently used entry. */
        PIOTLBE pIotlbe = RTListGetFirst(&pThisR3->LstLruIotlbe, IOTLBE, NdLru);
        Assert(pIotlbe);

        /* If the entry is in the cache, remove it. */
        if (pIotlbe->Core.Key != IOMMU_IOTLB_KEY_NIL)
            iommuAmdIotlbEntryRemove(pThis, pThisR3, pIotlbe->Core.Key);

        /* Initialize and insert the IOTLB entry into the cache. */
        iommuAmdIotlbEntryInsert(pThis, pThisR3, pIotlbe, idDomain, uIovaPage, pPageLookup);

        /* Move the entry to the most recently used slot. */
        iommuAmdIotlbEntryMoveToMru(pThisR3, pIotlbe);
    }
    else
    {
        /* Grab an unused IOTLB entry from the pre-allocated list. */
        PIOTLBE pIotlbe = &pThisR3->paIotlbes[pThisR3->idxUnusedIotlbe];
        ++pThisR3->idxUnusedIotlbe;

        /* Initialize and insert the IOTLB entry into the cache. */
        iommuAmdIotlbEntryInsert(pThis, pThisR3, pIotlbe, idDomain, uIovaPage, pPageLookup);

        /* Add the entry to the most recently used slot. */
        RTListAppend(&pThisR3->LstLruIotlbe, &pIotlbe->NdLru);
    }
}


/**
 * Removes all IOTLB entries from the cache.
 *
 * @param   pDevIns     The IOMMU instance data.
 */
static void iommuAmdIotlbRemoveAll(PPDMDEVINS pDevIns)
{
    PIOMMU   pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PIOMMUR3 pThisR3 = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUR3);
    IOMMU_CACHE_LOCK(pDevIns, pThis);

    if (pThisR3->cCachedIotlbes > 0)
    {
        RTAvlU64Destroy(&pThisR3->TreeIotlbe, iommuAmdIotlbEntryDestroy, NULL /* pvParam */);
        RTListInit(&pThisR3->LstLruIotlbe);
        pThisR3->idxUnusedIotlbe = 0;
        pThisR3->cCachedIotlbes  = 0;
        STAM_COUNTER_RESET(&pThis->StatIotlbeCached);
    }

    IOMMU_CACHE_UNLOCK(pDevIns, pThis);
}


/**
 * Removes IOTLB entries for the range of I/O virtual addresses and the specified
 * domain ID from the cache.
 *
 * @param   pDevIns         The IOMMU instance data.
 * @param   idDomain        The domain ID.
 * @param   uIova           The I/O virtual address to invalidate.
 * @param   cbInvalidate    The size of the invalidation (must be 4K aligned).
 */
static void iommuAmdIotlbRemoveRange(PPDMDEVINS pDevIns, uint16_t idDomain, uint64_t uIova, size_t cbInvalidate)
{
    /* Validate. */
    Assert(!(uIova & X86_PAGE_4K_OFFSET_MASK));
    Assert(!(cbInvalidate & X86_PAGE_4K_OFFSET_MASK));
    Assert(cbInvalidate >= X86_PAGE_4K_SIZE);

    PIOMMU   pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PIOMMUR3 pThisR3 = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUR3);
    IOMMU_CACHE_LOCK(pDevIns, pThis);

    do
    {
        uint64_t const uKey = IOMMU_IOTLB_KEY_MAKE(idDomain, uIova);
        PIOTLBE pIotlbe = iommuAmdIotlbEntryRemove(pThis, pThisR3, uKey);
        if (pIotlbe)
            iommuAmdIotlbEntryMoveToLru(pThisR3, pIotlbe);
        uIova        += X86_PAGE_4K_SIZE;
        cbInvalidate -= X86_PAGE_4K_SIZE;
    } while (cbInvalidate > 0);

    IOMMU_CACHE_UNLOCK(pDevIns, pThis);
}


/**
 * Removes all IOTLB entries for the specified domain ID.
 *
 * @param   pDevIns     The IOMMU instance data.
 * @param   idDomain    The domain ID.
 */
static void iommuAmdIotlbRemoveDomainId(PPDMDEVINS pDevIns, uint16_t idDomain)
{
    /*
     * We need to iterate the tree and search based on the domain ID.
     * But it seems we cannot remove items while iterating the tree.
     * Thus, we simply mark entries for eviction later but move them to the LRU
     * so they will eventually get evicted and re-cycled as the cache gets re-populated.
     */
    PIOMMU   pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PIOMMUR3 pThisR3 = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUR3);
    IOMMU_CACHE_LOCK(pDevIns, pThis);

    IOTLBEFLUSHARG Args;
    Args.pIommuR3 = pThisR3;
    Args.idDomain = idDomain;
    RTAvlU64DoWithAll(&pThisR3->TreeIotlbe, true /* fFromLeft */, iommuAmdIotlbEntryRemoveDomainId, &Args);

    IOMMU_CACHE_UNLOCK(pDevIns, pThis);
}


/**
 * Adds or updates IOTLB entries for the given range of I/O virtual addresses.
 *
 * @param   pDevIns         The IOMMU instance data.
 * @param   idDomain        The domain ID.
 * @param   uIovaPage       The I/O virtual address (must be 4K aligned).
 * @param   cbContiguous    The size of the access.
 * @param   pAddrOut        The translated I/O address lookup.
 *
 * @remarks All pages in the range specified by @c cbContiguous must have identical
 *          permissions and page sizes.
 */
static void iommuAmdIotlbAddRange(PPDMDEVINS pDevIns, uint16_t idDomain, uint64_t uIovaPage, size_t cbContiguous,
                                  PCIOPAGELOOKUP pAddrOut)
{
    Assert(!(uIovaPage & X86_PAGE_4K_OFFSET_MASK));

    PIOMMU   pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PIOMMUR3 pThisR3 = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUR3);

    IOPAGELOOKUP PageLookup;
    PageLookup.GCPhysSpa = pAddrOut->GCPhysSpa & X86_PAGE_4K_BASE_MASK;
    PageLookup.cShift    = pAddrOut->cShift;
    PageLookup.fPerm     = pAddrOut->fPerm;

    size_t const cbIova = RT_ALIGN_Z(cbContiguous, X86_PAGE_4K_SIZE);
    Assert(!(cbIova & X86_PAGE_4K_OFFSET_MASK));
    Assert(cbIova >= X86_PAGE_4K_SIZE);

    size_t cPages = cbIova / X86_PAGE_4K_SIZE;
    cPages = RT_MIN(cPages, IOMMU_IOTLBE_MAX);

    IOMMU_CACHE_LOCK(pDevIns, pThis);
    /** @todo Re-check DTE cache? */
    /*
     * Add IOTLB entries for every page in the access.
     * The page size and permissions are assumed to be identical to every
     * page in this access.
     */
    while (cPages > 0)
    {
        iommuAmdIotlbAdd(pThis, pThisR3, idDomain, uIovaPage, &PageLookup);
        uIovaPage            += X86_PAGE_4K_SIZE;
        PageLookup.GCPhysSpa += X86_PAGE_4K_SIZE;
        --cPages;
    }
    IOMMU_CACHE_UNLOCK(pDevIns, pThis);
}
#endif  /* IOMMU_WITH_IOTLBE_CACHE */


#ifdef IOMMU_WITH_IRTE_CACHE
/**
 * Looks up an IRTE cache entry.
 *
 * @returns Index of the found entry, or cache capacity if not found.
 * @param   pThis       The shared IOMMU device state.
 * @param   idDevice    The device ID (bus, device, function).
 * @param   offIrte     The offset into the interrupt remap table.
 */
static uint16_t iommuAmdIrteCacheEntryLookup(PCIOMMU pThis, uint16_t idDevice, uint16_t offIrte)
{
    /** @todo Consider sorting and binary search when the cache capacity grows.
     *  For the IRTE cache this should be okay since typically guests do not alter the
     *  interrupt remapping once programmed, so hopefully sorting shouldn't happen
     *  often. */
    uint32_t const uKey = IOMMU_IRTE_CACHE_KEY_MAKE(idDevice, offIrte);
    uint16_t const cIrteCache = RT_ELEMENTS(pThis->aIrteCache);
    for (uint16_t i = 0; i < cIrteCache; i++)
        if (pThis->aIrteCache[i].uKey == uKey)
            return i;
    return cIrteCache;
}


/**
 * Gets a free/unused IRTE cache entry.
 *
 * @returns The index of an unused entry, or cache capacity if the cache is full.
 * @param   pThis   The shared IOMMU device state.
 */
static uint16_t iommuAmdIrteCacheEntryGetUnused(PCIOMMU pThis)
{
    uint16_t const cIrteCache = RT_ELEMENTS(pThis->aIrteCache);
    for (uint16_t i = 0; i < cIrteCache; i++)
        if (pThis->aIrteCache[i].uKey == IOMMU_IRTE_CACHE_KEY_NIL)
        {
            Assert(!pThis->aIrteCache[i].Irte.u32);
            return i;
        }
    return cIrteCache;
}


/**
 * Looks up the IRTE cache for the given MSI.
 *
 * @returns VBox status code.
 * @param   pDevIns     The IOMMU instance data.
 * @param   idDevice    The device ID (bus, device, function).
 * @param   enmOp       The IOMMU operation being performed.
 * @param   pMsiIn      The source MSI.
 * @param   pMsiOut     Where to store the remapped MSI.
 */
static int iommuAmdIrteCacheLookup(PPDMDEVINS pDevIns, uint16_t idDevice, IOMMUOP enmOp, PCMSIMSG pMsiIn, PMSIMSG pMsiOut)
{
    RT_NOREF(enmOp); /* May need it if we have to report errors (currently we fallback to the slower path to do that). */

    int rc = VERR_NOT_FOUND;
    /* Deal with such cases in the slower/fallback path. */
    if ((pMsiIn->Addr.u64 & VBOX_MSI_ADDR_ADDR_MASK) == VBOX_MSI_ADDR_BASE)
    { /* likely */ }
    else
        return rc;

    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    IOMMU_CACHE_LOCK(pDevIns, pThis);

    uint16_t const idxDteCache = iommuAmdDteCacheEntryLookup(pThis, idDevice);
    if (idxDteCache < RT_ELEMENTS(pThis->aDteCache))
    {
        PCDTECACHE pDteCache = &pThis->aDteCache[idxDteCache];
        if ((pDteCache->fFlags & (IOMMU_DTE_CACHE_F_PRESENT | IOMMU_DTE_CACHE_F_INTR_MAP_VALID))
                              == (IOMMU_DTE_CACHE_F_PRESENT | IOMMU_DTE_CACHE_F_INTR_MAP_VALID))
        {
            Assert((pMsiIn->Addr.u64 & VBOX_MSI_ADDR_ADDR_MASK) == VBOX_MSI_ADDR_BASE);        /* Paranoia. */

            /* Currently, we only cache remapping of fixed and arbitrated interrupts. */
            uint8_t const u8DeliveryMode = pMsiIn->Data.n.u3DeliveryMode;
            if (u8DeliveryMode <= VBOX_MSI_DELIVERY_MODE_LOWEST_PRIO)
            {
                uint8_t const uIntrCtrl = (pDteCache->fFlags >> IOMMU_DTE_CACHE_F_INTR_CTRL_SHIFT)
                                        & IOMMU_DTE_CACHE_F_INTR_CTRL_MASK;
                if (uIntrCtrl == IOMMU_INTR_CTRL_REMAP)
                {
                    /* Interrupt table length has been verified prior to adding entries to the cache. */
                    uint16_t const offIrte      = IOMMU_GET_IRTE_OFF(pMsiIn->Data.u32);
                    uint16_t const idxIrteCache = iommuAmdIrteCacheEntryLookup(pThis, idDevice, offIrte);
                    if (idxIrteCache < RT_ELEMENTS(pThis->aIrteCache))
                    {
                        PCIRTE_T pIrte = &pThis->aIrteCache[idxIrteCache].Irte;
                        Assert(pIrte->n.u1RemapEnable);
                        Assert(pIrte->n.u3IntrType <= VBOX_MSI_DELIVERY_MODE_LOWEST_PRIO);
                        iommuAmdIrteRemapMsi(pMsiIn, pMsiOut, pIrte);
                        rc = VINF_SUCCESS;
                    }
                }
                else if (uIntrCtrl == IOMMU_INTR_CTRL_FWD_UNMAPPED)
                {
                    *pMsiOut = *pMsiIn;
                    rc = VINF_SUCCESS;
                }
            }
        }
        else if (pDteCache->fFlags & IOMMU_DTE_CACHE_F_PRESENT)
        {
            *pMsiOut = *pMsiIn;
            rc = VINF_SUCCESS;
        }
    }

    IOMMU_CACHE_UNLOCK(pDevIns, pThis);
    return rc;
}


/**
 * Adds or updates the IRTE cache for the given IRTE.
 *
 * @returns VBox status code.
 * @retval  VERR_OUT_OF_RESOURCES if the cache is full.
 *
 * @param   pDevIns     The IOMMU instance data.
 * @param   idDevice    The device ID (bus, device, function).
 * @param   offIrte     The offset into the interrupt remap table.
 * @param   pIrte       The IRTE to cache.
 */
static int iommuAmdIrteCacheAdd(PPDMDEVINS pDevIns, uint16_t idDevice, uint16_t offIrte, PCIRTE_T pIrte)
{
    Assert(offIrte != 0xffff);  /* Shouldn't be a valid IRTE table offset since sizeof(IRTE) is a multiple of 4. */

    int rc = VINF_SUCCESS;
    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    Assert(idDevice != pThis->uPciAddress);
    IOMMU_CACHE_LOCK(pDevIns, pThis);

    /* Find an existing entry or get an unused slot. */
    uint16_t const cIrteCache = RT_ELEMENTS(pThis->aIrteCache);
    uint16_t idxIrteCache     = iommuAmdIrteCacheEntryLookup(pThis, idDevice, offIrte);
    if (   idxIrteCache < cIrteCache
        || (idxIrteCache = iommuAmdIrteCacheEntryGetUnused(pThis)) < cIrteCache)
    {
        pThis->aIrteCache[idxIrteCache].uKey = IOMMU_IRTE_CACHE_KEY_MAKE(idDevice, offIrte);
        pThis->aIrteCache[idxIrteCache].Irte = *pIrte;
    }
    else
        rc = VERR_OUT_OF_RESOURCES;

    IOMMU_CACHE_UNLOCK(pDevIns, pThis);
    return rc;
}


# ifdef IN_RING3
/**
 * Removes IRTE cache entries for the given device ID.
 *
 * @param   pDevIns     The IOMMU instance data.
 * @param   idDevice    The device ID (bus, device, function).
 */
static void iommuAmdIrteCacheRemove(PPDMDEVINS pDevIns, uint16_t idDevice)
{
    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    IOMMU_CACHE_LOCK(pDevIns, pThis);
    uint16_t const cIrteCache = RT_ELEMENTS(pThis->aIrteCache);
    for (uint16_t i = 0; i < cIrteCache; i++)
    {
        PIRTECACHE pIrteCache = &pThis->aIrteCache[i];
        if (idDevice == IOMMU_IRTE_CACHE_KEY_GET_DEVICE_ID(pIrteCache->uKey))
        {
            pIrteCache->uKey     = IOMMU_IRTE_CACHE_KEY_NIL;
            pIrteCache->Irte.u32 = 0;
            /* There could multiple IRTE entries for a device ID, continue searching. */
        }
    }
    IOMMU_CACHE_UNLOCK(pDevIns, pThis);
}


/**
 * Removes all IRTE cache entries.
 *
 * @param   pDevIns     The IOMMU instance data.
 */
static void iommuAmdIrteCacheRemoveAll(PPDMDEVINS pDevIns)
{
    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    IOMMU_CACHE_LOCK(pDevIns, pThis);
    uint16_t const cIrteCache = RT_ELEMENTS(pThis->aIrteCache);
    for (uint16_t i = 0; i < cIrteCache; i++)
    {
        pThis->aIrteCache[i].uKey     = IOMMU_IRTE_CACHE_KEY_NIL;
        pThis->aIrteCache[i].Irte.u32 = 0;
    }
    IOMMU_CACHE_UNLOCK(pDevIns, pThis);
}
# endif /* IN_RING3 */
#endif  /* IOMMU_WITH_IRTE_CACHE */


/**
 * Atomically reads the control register without locking the IOMMU device.
 *
 * @returns The control register.
 * @param   pThis   The shared IOMMU device state.
 */
DECL_FORCE_INLINE(IOMMU_CTRL_T) iommuAmdGetCtrlUnlocked(PCIOMMU pThis)
{
    IOMMU_CTRL_T Ctrl;
    Ctrl.u64 = ASMAtomicReadU64((volatile uint64_t *)&pThis->Ctrl.u64);
    return Ctrl;
}


/**
 * Returns whether MSI is enabled for the IOMMU.
 *
 * @returns Whether MSI is enabled.
 * @param   pDevIns     The IOMMU device instance.
 *
 * @note There should be a PCIDevXxx function for this.
 */
static bool iommuAmdIsMsiEnabled(PPDMDEVINS pDevIns)
{
    MSI_CAP_HDR_T MsiCapHdr;
    MsiCapHdr.u32 = PDMPciDevGetDWord(pDevIns->apPciDevs[0], IOMMU_PCI_OFF_MSI_CAP_HDR);
    return MsiCapHdr.n.u1MsiEnable;
}


/**
 * Signals a PCI target abort.
 *
 * @param   pDevIns     The IOMMU device instance.
 */
static void iommuAmdSetPciTargetAbort(PPDMDEVINS pDevIns)
{
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    uint16_t const u16Status = PDMPciDevGetStatus(pPciDev) | VBOX_PCI_STATUS_SIG_TARGET_ABORT;
    PDMPciDevSetStatus(pPciDev, u16Status);
}


/**
 * Wakes up the command thread if there are commands to be processed.
 *
 * @param   pDevIns     The IOMMU device instance.
 *
 * @remarks The IOMMU lock must be held while calling this!
 */
static void iommuAmdCmdThreadWakeUpIfNeeded(PPDMDEVINS pDevIns)
{
    Log4Func(("\n"));

    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    if (    pThis->Status.n.u1CmdBufRunning
        &&  pThis->CmdBufTailPtr.n.off != pThis->CmdBufHeadPtr.n.off
        && !ASMAtomicXchgBool(&pThis->fCmdThreadSignaled, true))
    {
        Log4Func(("Signaling command thread\n"));
        PDMDevHlpSUPSemEventSignal(pDevIns, pThis->hEvtCmdThread);
    }
}


/**
 * Reads the Device Table Base Address Register.
 */
static VBOXSTRICTRC iommuAmdDevTabBar_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->aDevTabBaseAddrs[0].u64;
    return VINF_SUCCESS;
}


/**
 * Reads the Command Buffer Base Address Register.
 */
static VBOXSTRICTRC iommuAmdCmdBufBar_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->CmdBufBaseAddr.u64;
    return VINF_SUCCESS;
}


/**
 * Reads the Event Log Base Address Register.
 */
static VBOXSTRICTRC iommuAmdEvtLogBar_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->EvtLogBaseAddr.u64;
    return VINF_SUCCESS;
}


/**
 * Reads the Control Register.
 */
static VBOXSTRICTRC iommuAmdCtrl_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->Ctrl.u64;
    return VINF_SUCCESS;
}


/**
 * Reads the Exclusion Range Base Address Register.
 */
static VBOXSTRICTRC iommuAmdExclRangeBar_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->ExclRangeBaseAddr.u64;
    return VINF_SUCCESS;
}


/**
 * Reads to the Exclusion Range Limit Register.
 */
static VBOXSTRICTRC iommuAmdExclRangeLimit_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->ExclRangeLimit.u64;
    return VINF_SUCCESS;
}


/**
 * Reads to the Extended Feature Register.
 */
static VBOXSTRICTRC iommuAmdExtFeat_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->ExtFeat.u64;
    return VINF_SUCCESS;
}


/**
 * Reads to the PPR Log Base Address Register.
 */
static VBOXSTRICTRC iommuAmdPprLogBar_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->PprLogBaseAddr.u64;
    return VINF_SUCCESS;
}


/**
 * Writes the Hardware Event Register (Hi).
 */
static VBOXSTRICTRC iommuAmdHwEvtHi_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->HwEvtHi.u64;
    return VINF_SUCCESS;
}


/**
 * Reads the Hardware Event Register (Lo).
 */
static VBOXSTRICTRC iommuAmdHwEvtLo_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->HwEvtLo;
    return VINF_SUCCESS;
}


/**
 * Reads the Hardware Event Status Register.
 */
static VBOXSTRICTRC iommuAmdHwEvtStatus_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->HwEvtStatus.u64;
    return VINF_SUCCESS;
}


/**
 * Reads to the GA Log Base Address Register.
 */
static VBOXSTRICTRC iommuAmdGALogBar_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->GALogBaseAddr.u64;
    return VINF_SUCCESS;
}


/**
 * Reads to the PPR Log B Base Address Register.
 */
static VBOXSTRICTRC iommuAmdPprLogBBaseAddr_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->PprLogBBaseAddr.u64;
    return VINF_SUCCESS;
}


/**
 * Reads to the Event Log B Base Address Register.
 */
static VBOXSTRICTRC iommuAmdEvtLogBBaseAddr_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->EvtLogBBaseAddr.u64;
    return VINF_SUCCESS;
}


/**
 * Reads the Device Table Segment Base Address Register.
 */
static VBOXSTRICTRC iommuAmdDevTabSegBar_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns);

    /* Figure out which segment is being written. */
    uint8_t const offSegment = (offReg - IOMMU_MMIO_OFF_DEV_TAB_SEG_FIRST) >> 3;
    uint8_t const idxSegment = offSegment + 1;
    Assert(idxSegment < RT_ELEMENTS(pThis->aDevTabBaseAddrs));

    *pu64Value = pThis->aDevTabBaseAddrs[idxSegment].u64;
    return VINF_SUCCESS;
}


/**
 * Reads the Device Specific Feature Extension (DSFX) Register.
 */
static VBOXSTRICTRC iommuAmdDevSpecificFeat_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->DevSpecificFeat.u64;
    return VINF_SUCCESS;
}

/**
 * Reads the Device Specific Control Extension (DSCX) Register.
 */
static VBOXSTRICTRC iommuAmdDevSpecificCtrl_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->DevSpecificCtrl.u64;
    return VINF_SUCCESS;
}


/**
 * Reads the Device Specific Status Extension (DSSX) Register.
 */
static VBOXSTRICTRC iommuAmdDevSpecificStatus_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->DevSpecificStatus.u64;
    return VINF_SUCCESS;
}


/**
 * Reads the MSI Vector Register 0 (32-bit) and the MSI Vector Register 1 (32-bit).
 */
static VBOXSTRICTRC iommuAmdDevMsiVector_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    uint32_t const uLo = pThis->MiscInfo.au32[0];
    uint32_t const uHi = pThis->MiscInfo.au32[1];
    *pu64Value = RT_MAKE_U64(uLo, uHi);
    return VINF_SUCCESS;
}


/**
 * Reads the MSI Capability Header Register (32-bit) and the MSI Address (Lo)
 * Register (32-bit).
 */
static VBOXSTRICTRC iommuAmdMsiCapHdrAndAddrLo_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pThis, offReg);
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);
    uint32_t const uLo = PDMPciDevGetDWord(pPciDev, IOMMU_PCI_OFF_MSI_CAP_HDR);
    uint32_t const uHi = PDMPciDevGetDWord(pPciDev, IOMMU_PCI_OFF_MSI_ADDR_LO);
    *pu64Value = RT_MAKE_U64(uLo, uHi);
    return VINF_SUCCESS;
}


/**
 * Reads the MSI Address (Hi) Register (32-bit) and the MSI data register (32-bit).
 */
static VBOXSTRICTRC iommuAmdMsiAddrHiAndData_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pThis, offReg);
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);
    uint32_t const uLo = PDMPciDevGetDWord(pPciDev, IOMMU_PCI_OFF_MSI_ADDR_HI);
    uint32_t const uHi = PDMPciDevGetDWord(pPciDev, IOMMU_PCI_OFF_MSI_DATA);
    *pu64Value = RT_MAKE_U64(uLo, uHi);
    return VINF_SUCCESS;
}


/**
 * Reads the Command Buffer Head Pointer Register.
 */
static VBOXSTRICTRC iommuAmdCmdBufHeadPtr_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->CmdBufHeadPtr.u64;
    return VINF_SUCCESS;
}


/**
 * Reads the Command Buffer Tail Pointer Register.
 */
static VBOXSTRICTRC iommuAmdCmdBufTailPtr_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->CmdBufTailPtr.u64;
    return VINF_SUCCESS;
}


/**
 * Reads the Event Log Head Pointer Register.
 */
static VBOXSTRICTRC iommuAmdEvtLogHeadPtr_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->EvtLogHeadPtr.u64;
    return VINF_SUCCESS;
}


/**
 * Reads the Event Log Tail Pointer Register.
 */
static VBOXSTRICTRC iommuAmdEvtLogTailPtr_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->EvtLogTailPtr.u64;
    return VINF_SUCCESS;
}


/**
 * Reads the Status Register.
 */
static VBOXSTRICTRC iommuAmdStatus_r(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, offReg);
    *pu64Value = pThis->Status.u64;
    return VINF_SUCCESS;
}


/**
 * Writes the Device Table Base Address Register.
 */
static VBOXSTRICTRC iommuAmdDevTabBar_w(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value)
{
    RT_NOREF(pDevIns, offReg);

    /* Mask out all unrecognized bits. */
    u64Value &= IOMMU_DEV_TAB_BAR_VALID_MASK;

    /* Update the register. */
    pThis->aDevTabBaseAddrs[0].u64 = u64Value;

    /* Paranoia. */
    Assert(pThis->aDevTabBaseAddrs[0].n.u9Size <= g_auDevTabSegMaxSizes[0]);
    return VINF_SUCCESS;
}


/**
 * Writes the Command Buffer Base Address Register.
 */
static VBOXSTRICTRC iommuAmdCmdBufBar_w(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value)
{
    RT_NOREF(pDevIns, offReg);

    /*
     * While this is not explicitly specified like the event log base address register,
     * the AMD IOMMU spec. does specify "CmdBufRun must be 0b to modify the command buffer registers properly".
     * Inconsistent specs :/
     */
    if (pThis->Status.n.u1CmdBufRunning)
    {
        LogFunc(("Setting CmdBufBar (%#RX64) when command buffer is running -> Ignored\n", u64Value));
        return VINF_SUCCESS;
    }

    /* Mask out all unrecognized bits. */
    CMD_BUF_BAR_T CmdBufBaseAddr;
    CmdBufBaseAddr.u64 = u64Value & IOMMU_CMD_BUF_BAR_VALID_MASK;

    /* Validate the length. */
    if (CmdBufBaseAddr.n.u4Len >= 8)
    {
        /* Update the register. */
        pThis->CmdBufBaseAddr.u64 = CmdBufBaseAddr.u64;

        /*
         * Writing the command buffer base address, clears the command buffer head and tail pointers.
         * See AMD IOMMU spec. 2.4 "Commands".
         */
        pThis->CmdBufHeadPtr.u64 = 0;
        pThis->CmdBufTailPtr.u64 = 0;
    }
    else
        LogFunc(("Command buffer length (%#x) invalid -> Ignored\n", CmdBufBaseAddr.n.u4Len));

    return VINF_SUCCESS;
}


/**
 * Writes the Event Log Base Address Register.
 */
static VBOXSTRICTRC iommuAmdEvtLogBar_w(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value)
{
    RT_NOREF(pDevIns, offReg);

    /*
     * IOMMU behavior is undefined when software writes this register when event logging is running.
     * In our emulation, we ignore the write entirely.
     * See AMD IOMMU spec. "Event Log Base Address Register".
     */
    if (pThis->Status.n.u1EvtLogRunning)
    {
        LogFunc(("Setting EvtLogBar (%#RX64) when event logging is running -> Ignored\n", u64Value));
        return VINF_SUCCESS;
    }

    /* Mask out all unrecognized bits. */
    u64Value &= IOMMU_EVT_LOG_BAR_VALID_MASK;
    EVT_LOG_BAR_T EvtLogBaseAddr;
    EvtLogBaseAddr.u64 = u64Value;

    /* Validate the length. */
    if (EvtLogBaseAddr.n.u4Len >= 8)
    {
        /* Update the register. */
        pThis->EvtLogBaseAddr.u64 = EvtLogBaseAddr.u64;

        /*
         * Writing the event log base address, clears the event log head and tail pointers.
         * See AMD IOMMU spec. 2.5 "Event Logging".
         */
        pThis->EvtLogHeadPtr.u64 = 0;
        pThis->EvtLogTailPtr.u64 = 0;
    }
    else
        LogFunc(("Event log length (%#x) invalid -> Ignored\n", EvtLogBaseAddr.n.u4Len));

    return VINF_SUCCESS;
}


/**
 * Writes the Control Register.
 */
static VBOXSTRICTRC iommuAmdCtrl_w(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value)
{
    RT_NOREF(pDevIns, offReg);

    /* Mask out all unrecognized bits. */
    u64Value &= IOMMU_CTRL_VALID_MASK;
    IOMMU_CTRL_T NewCtrl;
    NewCtrl.u64 = u64Value;

    /* Ensure the device table segments are within limits. */
    if (NewCtrl.n.u3DevTabSegEn <= pThis->ExtFeat.n.u2DevTabSegSup)
    {
        IOMMU_CTRL_T const OldCtrl = pThis->Ctrl;

        /* Update the register. */
        ASMAtomicWriteU64(&pThis->Ctrl.u64, NewCtrl.u64);

        bool const fNewIommuEn = NewCtrl.n.u1IommuEn;
        bool const fOldIommuEn = OldCtrl.n.u1IommuEn;

        /* Enable or disable event logging when the bit transitions. */
        bool const fOldEvtLogEn = OldCtrl.n.u1EvtLogEn;
        bool const fNewEvtLogEn = NewCtrl.n.u1EvtLogEn;
        if (   fOldEvtLogEn != fNewEvtLogEn
            || fOldIommuEn != fNewIommuEn)
        {
            if (   fNewIommuEn
                && fNewEvtLogEn)
            {
                ASMAtomicAndU64(&pThis->Status.u64, ~IOMMU_STATUS_EVT_LOG_OVERFLOW);
                ASMAtomicOrU64(&pThis->Status.u64, IOMMU_STATUS_EVT_LOG_RUNNING);
            }
            else
                ASMAtomicAndU64(&pThis->Status.u64, ~IOMMU_STATUS_EVT_LOG_RUNNING);
        }

        /* Enable or disable command buffer processing when the bit transitions. */
        bool const fOldCmdBufEn = OldCtrl.n.u1CmdBufEn;
        bool const fNewCmdBufEn = NewCtrl.n.u1CmdBufEn;
        if (   fOldCmdBufEn != fNewCmdBufEn
            || fOldIommuEn != fNewIommuEn)
        {
            if (   fNewCmdBufEn
                && fNewIommuEn)
            {
                ASMAtomicOrU64(&pThis->Status.u64, IOMMU_STATUS_CMD_BUF_RUNNING);
                LogFunc(("Command buffer enabled\n"));

                /* Wake up the command thread to start processing commands if any. */
                iommuAmdCmdThreadWakeUpIfNeeded(pDevIns);
            }
            else
            {
                ASMAtomicAndU64(&pThis->Status.u64, ~IOMMU_STATUS_CMD_BUF_RUNNING);
                LogFunc(("Command buffer disabled\n"));
            }
        }
    }
    else
    {
        LogFunc(("Invalid number of device table segments enabled, exceeds %#x (%#RX64) -> Ignored!\n",
                 pThis->ExtFeat.n.u2DevTabSegSup, NewCtrl.u64));
    }

    return VINF_SUCCESS;
}


/**
 * Writes to the Exclusion Range Base Address Register.
 */
static VBOXSTRICTRC iommuAmdExclRangeBar_w(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value)
{
    RT_NOREF(pDevIns, offReg);
    pThis->ExclRangeBaseAddr.u64 = u64Value & IOMMU_EXCL_RANGE_BAR_VALID_MASK;
    return VINF_SUCCESS;
}


/**
 * Writes to the Exclusion Range Limit Register.
 */
static VBOXSTRICTRC iommuAmdExclRangeLimit_w(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value)
{
    RT_NOREF(pDevIns, offReg);
    u64Value &= IOMMU_EXCL_RANGE_LIMIT_VALID_MASK;
    u64Value |= UINT64_C(0xfff);
    pThis->ExclRangeLimit.u64 = u64Value;
    return VINF_SUCCESS;
}


/**
 * Writes the Hardware Event Register (Hi).
 */
static VBOXSTRICTRC iommuAmdHwEvtHi_w(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value)
{
    /** @todo IOMMU: Why the heck is this marked read/write by the AMD IOMMU spec? */
    RT_NOREF(pDevIns, offReg);
    LogFlowFunc(("Writing %#RX64 to hardware event (Hi) register!\n", u64Value));
    pThis->HwEvtHi.u64 = u64Value;
    return VINF_SUCCESS;
}


/**
 * Writes the Hardware Event Register (Lo).
 */
static VBOXSTRICTRC iommuAmdHwEvtLo_w(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value)
{
    /** @todo IOMMU: Why the heck is this marked read/write by the AMD IOMMU spec? */
    RT_NOREF(pDevIns, offReg);
    LogFlowFunc(("Writing %#RX64 to hardware event (Lo) register!\n", u64Value));
    pThis->HwEvtLo = u64Value;
    return VINF_SUCCESS;
}


/**
 * Writes the Hardware Event Status Register.
 */
static VBOXSTRICTRC iommuAmdHwEvtStatus_w(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value)
{
    RT_NOREF(pDevIns, offReg);

    /* Mask out all unrecognized bits. */
    u64Value &= IOMMU_HW_EVT_STATUS_VALID_MASK;

    /*
     * The two bits (HEO and HEV) are RW1C (Read/Write 1-to-Clear; writing 0 has no effect).
     * If the current status bits or the bits being written are both 0, we've nothing to do.
     * The Overflow bit (bit 1) is only valid when the Valid bit (bit 0) is 1.
     */
    uint64_t HwStatus = pThis->HwEvtStatus.u64;
    if (!(HwStatus & RT_BIT(0)))
        return VINF_SUCCESS;
    if (u64Value & HwStatus & RT_BIT_64(0))
        HwStatus &= ~RT_BIT_64(0);
    if (u64Value & HwStatus & RT_BIT_64(1))
        HwStatus &= ~RT_BIT_64(1);

    /* Update the register. */
    pThis->HwEvtStatus.u64 = HwStatus;
    return VINF_SUCCESS;
}


/**
 * Writes the Device Table Segment Base Address Register.
 */
static VBOXSTRICTRC iommuAmdDevTabSegBar_w(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value)
{
    RT_NOREF(pDevIns);

    /* Figure out which segment is being written. */
    uint8_t const offSegment = (offReg - IOMMU_MMIO_OFF_DEV_TAB_SEG_FIRST) >> 3;
    uint8_t const idxSegment = offSegment + 1;
    Assert(idxSegment < RT_ELEMENTS(pThis->aDevTabBaseAddrs));

    /* Mask out all unrecognized bits. */
    u64Value &= IOMMU_DEV_TAB_SEG_BAR_VALID_MASK;
    DEV_TAB_BAR_T DevTabSegBar;
    DevTabSegBar.u64 = u64Value;

    /* Validate the size. */
    uint16_t const uSegSize    = DevTabSegBar.n.u9Size;
    uint16_t const uMaxSegSize = g_auDevTabSegMaxSizes[idxSegment];
    if (uSegSize <= uMaxSegSize)
    {
        /* Update the register. */
        pThis->aDevTabBaseAddrs[idxSegment].u64 = u64Value;
    }
    else
        LogFunc(("Device table segment (%u) size invalid (%#RX32) -> Ignored\n", idxSegment, uSegSize));

    return VINF_SUCCESS;
}


/**
 * Writes the MSI Vector Register 0 (32-bit) and the MSI Vector Register 1 (32-bit).
 */
static VBOXSTRICTRC iommuAmdDevMsiVector_w(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value)
{
    RT_NOREF(pDevIns, offReg);

    /* MSI Vector Register 0 is read-only. */
    /* MSI Vector Register 1. */
    uint32_t const uReg = u64Value >> 32;
    pThis->MiscInfo.au32[1] = uReg & IOMMU_MSI_VECTOR_1_VALID_MASK;
    return VINF_SUCCESS;
}


/**
 * Writes the MSI Capability Header Register (32-bit) or the MSI Address (Lo)
 * Register (32-bit).
 */
static VBOXSTRICTRC iommuAmdMsiCapHdrAndAddrLo_w(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value)
{
    RT_NOREF(pThis, offReg);
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    /* MSI capability header. */
    {
        uint32_t const uReg = u64Value;
        MSI_CAP_HDR_T MsiCapHdr;
        MsiCapHdr.u32           = PDMPciDevGetDWord(pPciDev, IOMMU_PCI_OFF_MSI_CAP_HDR);
        MsiCapHdr.n.u1MsiEnable = RT_BOOL(uReg & IOMMU_MSI_CAP_HDR_MSI_EN_MASK);
        PDMPciDevSetDWord(pPciDev, IOMMU_PCI_OFF_MSI_CAP_HDR, MsiCapHdr.u32);
    }

    /* MSI Address Lo. */
    {
        uint32_t const uReg = u64Value >> 32;
        uint32_t const uMsiAddrLo = uReg & VBOX_MSI_ADDR_VALID_MASK;
        PDMPciDevSetDWord(pPciDev, IOMMU_PCI_OFF_MSI_ADDR_LO, uMsiAddrLo);
    }

    return VINF_SUCCESS;
}


/**
 * Writes the MSI Address (Hi) Register (32-bit) or the MSI data register (32-bit).
 */
static VBOXSTRICTRC iommuAmdMsiAddrHiAndData_w(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value)
{
    RT_NOREF(pThis, offReg);
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    /* MSI Address Hi. */
    {
        uint32_t const uReg = u64Value;
        PDMPciDevSetDWord(pPciDev, IOMMU_PCI_OFF_MSI_ADDR_HI, uReg);
    }

    /* MSI Data. */
    {
        uint32_t const uReg = u64Value >> 32;
        uint32_t const uMsiData = uReg & VBOX_MSI_DATA_VALID_MASK;
        PDMPciDevSetDWord(pPciDev, IOMMU_PCI_OFF_MSI_DATA, uMsiData);
    }

    return VINF_SUCCESS;
}


/**
 * Writes the Command Buffer Head Pointer Register.
 */
static VBOXSTRICTRC iommuAmdCmdBufHeadPtr_w(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value)
{
    RT_NOREF(pDevIns, offReg);

    /*
     * IOMMU behavior is undefined when software writes this register when the command buffer is running.
     * In our emulation, we ignore the write entirely.
     * See AMD IOMMU spec. 3.3.13 "Command and Event Log Pointer Registers".
     */
    if (pThis->Status.n.u1CmdBufRunning)
    {
        LogFunc(("Setting CmdBufHeadPtr (%#RX64) when command buffer is running -> Ignored\n", u64Value));
        return VINF_SUCCESS;
    }

    /*
     * IOMMU behavior is undefined when software writes a value outside the buffer length.
     * In our emulation, we ignore the write entirely.
     */
    uint32_t const offBuf = u64Value & IOMMU_CMD_BUF_HEAD_PTR_VALID_MASK;
    uint32_t const cbBuf  = iommuAmdGetTotalBufLength(pThis->CmdBufBaseAddr.n.u4Len);
    Assert(cbBuf <= _512K);
    if (offBuf >= cbBuf)
    {
        LogFunc(("Setting CmdBufHeadPtr (%#RX32) to a value that exceeds buffer length (%#RX23) -> Ignored\n", offBuf, cbBuf));
        return VINF_SUCCESS;
    }

    /* Update the register. */
    pThis->CmdBufHeadPtr.au32[0] = offBuf;

    iommuAmdCmdThreadWakeUpIfNeeded(pDevIns);

    Log4Func(("Set CmdBufHeadPtr to %#RX32\n", offBuf));
    return VINF_SUCCESS;
}


/**
 * Writes the Command Buffer Tail Pointer Register.
 */
static VBOXSTRICTRC iommuAmdCmdBufTailPtr_w(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value)
{
    RT_NOREF(pDevIns, offReg);

    /*
     * IOMMU behavior is undefined when software writes a value outside the buffer length.
     * In our emulation, we ignore the write entirely.
     * See AMD IOMMU spec. 3.3.13 "Command and Event Log Pointer Registers".
     */
    uint32_t const offBuf = u64Value & IOMMU_CMD_BUF_TAIL_PTR_VALID_MASK;
    uint32_t const cbBuf  = iommuAmdGetTotalBufLength(pThis->CmdBufBaseAddr.n.u4Len);
    Assert(cbBuf <= _512K);
    if (offBuf >= cbBuf)
    {
        LogFunc(("Setting CmdBufTailPtr (%#RX32) to a value that exceeds buffer length (%#RX32) -> Ignored\n", offBuf, cbBuf));
        return VINF_SUCCESS;
    }

    /*
     * IOMMU behavior is undefined if software advances the tail pointer equal to or beyond the
     * head pointer after adding one or more commands to the buffer.
     *
     * However, we cannot enforce this strictly because it's legal for software to shrink the
     * command queue (by reducing the offset) as well as wrap around the pointer (when head isn't
     * at 0). Software might even make the queue empty by making head and tail equal which is
     * allowed. I don't think we can or should try too hard to prevent software shooting itself
     * in the foot here. As long as we make sure the offset value is within the circular buffer
     * bounds (which we do by masking bits above) it should be sufficient.
     */
    pThis->CmdBufTailPtr.au32[0] = offBuf;

    iommuAmdCmdThreadWakeUpIfNeeded(pDevIns);

    Log4Func(("Set CmdBufTailPtr to %#RX32\n", offBuf));
    return VINF_SUCCESS;
}


/**
 * Writes the Event Log Head Pointer Register.
 */
static VBOXSTRICTRC iommuAmdEvtLogHeadPtr_w(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value)
{
    RT_NOREF(pDevIns, offReg);

    /*
     * IOMMU behavior is undefined when software writes a value outside the buffer length.
     * In our emulation, we ignore the write entirely.
     * See AMD IOMMU spec. 3.3.13 "Command and Event Log Pointer Registers".
     */
    uint32_t const offBuf = u64Value & IOMMU_EVT_LOG_HEAD_PTR_VALID_MASK;
    uint32_t const cbBuf  = iommuAmdGetTotalBufLength(pThis->EvtLogBaseAddr.n.u4Len);
    Assert(cbBuf <= _512K);
    if (offBuf >= cbBuf)
    {
        LogFunc(("Setting EvtLogHeadPtr (%#RX32) to a value that exceeds buffer length (%#RX32) -> Ignored\n", offBuf, cbBuf));
        return VINF_SUCCESS;
    }

    /* Update the register. */
    pThis->EvtLogHeadPtr.au32[0] = offBuf;

    Log4Func(("Set EvtLogHeadPtr to %#RX32\n", offBuf));
    return VINF_SUCCESS;
}


/**
 * Writes the Event Log Tail Pointer Register.
 */
static VBOXSTRICTRC iommuAmdEvtLogTailPtr_w(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value)
{
    RT_NOREF(pDevIns, offReg);
    NOREF(pThis);

    /*
     * IOMMU behavior is undefined when software writes this register when the event log is running.
     * In our emulation, we ignore the write entirely.
     * See AMD IOMMU spec. 3.3.13 "Command and Event Log Pointer Registers".
     */
    if (pThis->Status.n.u1EvtLogRunning)
    {
        LogFunc(("Setting EvtLogTailPtr (%#RX64) when event log is running -> Ignored\n", u64Value));
        return VINF_SUCCESS;
    }

    /*
     * IOMMU behavior is undefined when software writes a value outside the buffer length.
     * In our emulation, we ignore the write entirely.
     */
    uint32_t const offBuf = u64Value & IOMMU_EVT_LOG_TAIL_PTR_VALID_MASK;
    uint32_t const cbBuf  = iommuAmdGetTotalBufLength(pThis->EvtLogBaseAddr.n.u4Len);
    Assert(cbBuf <= _512K);
    if (offBuf >= cbBuf)
    {
        LogFunc(("Setting EvtLogTailPtr (%#RX32) to a value that exceeds buffer length (%#RX32) -> Ignored\n", offBuf, cbBuf));
        return VINF_SUCCESS;
    }

    /* Update the register. */
    pThis->EvtLogTailPtr.au32[0] = offBuf;

    Log4Func(("Set EvtLogTailPtr to %#RX32\n", offBuf));
    return VINF_SUCCESS;
}


/**
 * Writes the Status Register.
 */
static VBOXSTRICTRC iommuAmdStatus_w(PPDMDEVINS pDevIns, PIOMMU pThis, uint32_t offReg, uint64_t u64Value)
{
    RT_NOREF(pDevIns, offReg);

    /* Mask out all unrecognized bits. */
    u64Value &= IOMMU_STATUS_VALID_MASK;

    /*
     * Compute RW1C (read-only, write-1-to-clear) bits and preserve the rest (which are read-only).
     * Writing 0 to an RW1C bit has no effect. Writing 1 to an RW1C bit, clears the bit if it's already 1.
     */
    IOMMU_STATUS_T const OldStatus = pThis->Status;
    uint64_t const fOldRw1cBits = (OldStatus.u64 &  IOMMU_STATUS_RW1C_MASK);
    uint64_t const fOldRoBits   = (OldStatus.u64 & ~IOMMU_STATUS_RW1C_MASK);
    uint64_t const fNewRw1cBits = (u64Value      &  IOMMU_STATUS_RW1C_MASK);

    uint64_t const uNewStatus = (fOldRw1cBits & ~fNewRw1cBits) | fOldRoBits;

    /* Update the register. */
    ASMAtomicWriteU64(&pThis->Status.u64, uNewStatus);
    return VINF_SUCCESS;
}


/**
 * Register access table 0.
 * The MMIO offset of each entry must be a multiple of 8!
 */
static const IOMMUREGACC g_aRegAccess0[] =
{
    /* MMIO off.   Register name                           Read function                 Write function */
    { /* 0x00  */  "DEV_TAB_BAR",                          iommuAmdDevTabBar_r,          iommuAmdDevTabBar_w           },
    { /* 0x08  */  "CMD_BUF_BAR",                          iommuAmdCmdBufBar_r,          iommuAmdCmdBufBar_w           },
    { /* 0x10  */  "EVT_LOG_BAR",                          iommuAmdEvtLogBar_r,          iommuAmdEvtLogBar_w           },
    { /* 0x18  */  "CTRL",                                 iommuAmdCtrl_r,               iommuAmdCtrl_w                },
    { /* 0x20  */  "EXCL_BAR",                             iommuAmdExclRangeBar_r,       iommuAmdExclRangeBar_w        },
    { /* 0x28  */  "EXCL_RANGE_LIMIT",                     iommuAmdExclRangeLimit_r,     iommuAmdExclRangeLimit_w      },
    { /* 0x30  */  "EXT_FEAT",                             iommuAmdExtFeat_r,            NULL                          },
    { /* 0x38  */  "PPR_LOG_BAR",                          iommuAmdPprLogBar_r,          NULL                          },
    { /* 0x40  */  "HW_EVT_HI",                            iommuAmdHwEvtHi_r,            iommuAmdHwEvtHi_w             },
    { /* 0x48  */  "HW_EVT_LO",                            iommuAmdHwEvtLo_r,            iommuAmdHwEvtLo_w             },
    { /* 0x50  */  "HW_EVT_STATUS",                        iommuAmdHwEvtStatus_r,        iommuAmdHwEvtStatus_w         },
    { /* 0x58  */  NULL,                                   NULL,                         NULL                          },

    { /* 0x60  */  "SMI_FLT_0",                            NULL,                         NULL                          },
    { /* 0x68  */  "SMI_FLT_1",                            NULL,                         NULL                          },
    { /* 0x70  */  "SMI_FLT_2",                            NULL,                         NULL                          },
    { /* 0x78  */  "SMI_FLT_3",                            NULL,                         NULL                          },
    { /* 0x80  */  "SMI_FLT_4",                            NULL,                         NULL                          },
    { /* 0x88  */  "SMI_FLT_5",                            NULL,                         NULL                          },
    { /* 0x90  */  "SMI_FLT_6",                            NULL,                         NULL                          },
    { /* 0x98  */  "SMI_FLT_7",                            NULL,                         NULL                          },
    { /* 0xa0  */  "SMI_FLT_8",                            NULL,                         NULL                          },
    { /* 0xa8  */  "SMI_FLT_9",                            NULL,                         NULL                          },
    { /* 0xb0  */  "SMI_FLT_10",                           NULL,                         NULL                          },
    { /* 0xb8  */  "SMI_FLT_11",                           NULL,                         NULL                          },
    { /* 0xc0  */  "SMI_FLT_12",                           NULL,                         NULL                          },
    { /* 0xc8  */  "SMI_FLT_13",                           NULL,                         NULL                          },
    { /* 0xd0  */  "SMI_FLT_14",                           NULL,                         NULL                          },
    { /* 0xd8  */  "SMI_FLT_15",                           NULL,                         NULL                          },

    { /* 0xe0  */  "GALOG_BAR",                            iommuAmdGALogBar_r,           NULL                          },
    { /* 0xe8  */  "GALOG_TAIL_ADDR",                      NULL,                         NULL                          },
    { /* 0xf0  */  "PPR_LOG_B_BAR",                        iommuAmdPprLogBBaseAddr_r,    NULL                          },
    { /* 0xf8  */  "PPR_EVT_B_BAR",                        iommuAmdEvtLogBBaseAddr_r,    NULL                          },

    { /* 0x100 */  "DEV_TAB_SEG_1",                        iommuAmdDevTabSegBar_r,       iommuAmdDevTabSegBar_w        },
    { /* 0x108 */  "DEV_TAB_SEG_2",                        iommuAmdDevTabSegBar_r,       iommuAmdDevTabSegBar_w        },
    { /* 0x110 */  "DEV_TAB_SEG_3",                        iommuAmdDevTabSegBar_r,       iommuAmdDevTabSegBar_w        },
    { /* 0x118 */  "DEV_TAB_SEG_4",                        iommuAmdDevTabSegBar_r,       iommuAmdDevTabSegBar_w        },
    { /* 0x120 */  "DEV_TAB_SEG_5",                        iommuAmdDevTabSegBar_r,       iommuAmdDevTabSegBar_w        },
    { /* 0x128 */  "DEV_TAB_SEG_6",                        iommuAmdDevTabSegBar_r,       iommuAmdDevTabSegBar_w        },
    { /* 0x130 */  "DEV_TAB_SEG_7",                        iommuAmdDevTabSegBar_r,       iommuAmdDevTabSegBar_w        },

    { /* 0x138 */  "DEV_SPECIFIC_FEAT",                    iommuAmdDevSpecificFeat_r,    NULL                          },
    { /* 0x140 */  "DEV_SPECIFIC_CTRL",                    iommuAmdDevSpecificCtrl_r,    NULL                          },
    { /* 0x148 */  "DEV_SPECIFIC_STATUS",                  iommuAmdDevSpecificStatus_r,  NULL                          },

    { /* 0x150 */  "MSI_VECTOR_0 or MSI_VECTOR_1",         iommuAmdDevMsiVector_r,       iommuAmdDevMsiVector_w        },
    { /* 0x158 */  "MSI_CAP_HDR or MSI_ADDR_LO",           iommuAmdMsiCapHdrAndAddrLo_r, iommuAmdMsiCapHdrAndAddrLo_w  },
    { /* 0x160 */  "MSI_ADDR_HI or MSI_DATA",              iommuAmdMsiAddrHiAndData_r,   iommuAmdMsiAddrHiAndData_w    },
    { /* 0x168 */  "MSI_MAPPING_CAP_HDR or PERF_OPT_CTRL", NULL,                         NULL                          },

    { /* 0x170 */  "XT_GEN_INTR_CTRL",                     NULL,                         NULL                          },
    { /* 0x178 */  "XT_PPR_INTR_CTRL",                     NULL,                         NULL                          },
    { /* 0x180 */  "XT_GALOG_INT_CTRL",                    NULL,                         NULL                          },
};
AssertCompile(RT_ELEMENTS(g_aRegAccess0) == (IOMMU_MMIO_OFF_QWORD_TABLE_0_END - IOMMU_MMIO_OFF_QWORD_TABLE_0_START) / 8);

/**
 * Register access table 1.
 * The MMIO offset of each entry must be a multiple of 8!
 */
static const IOMMUREGACC g_aRegAccess1[] =
{
    /* MMIO offset   Register name         Read function   Write function */
    { /* 0x200 */    "MARC_APER_BAR_0",    NULL,           NULL },
    { /* 0x208 */    "MARC_APER_RELOC_0",  NULL,           NULL },
    { /* 0x210 */    "MARC_APER_LEN_0",    NULL,           NULL },
    { /* 0x218 */    "MARC_APER_BAR_1",    NULL,           NULL },
    { /* 0x220 */    "MARC_APER_RELOC_1",  NULL,           NULL },
    { /* 0x228 */    "MARC_APER_LEN_1",    NULL,           NULL },
    { /* 0x230 */    "MARC_APER_BAR_2",    NULL,           NULL },
    { /* 0x238 */    "MARC_APER_RELOC_2",  NULL,           NULL },
    { /* 0x240 */    "MARC_APER_LEN_2",    NULL,           NULL },
    { /* 0x248 */    "MARC_APER_BAR_3",    NULL,           NULL },
    { /* 0x250 */    "MARC_APER_RELOC_3",  NULL,           NULL },
    { /* 0x258 */    "MARC_APER_LEN_3",    NULL,           NULL }
};
AssertCompile(RT_ELEMENTS(g_aRegAccess1) == (IOMMU_MMIO_OFF_QWORD_TABLE_1_END - IOMMU_MMIO_OFF_QWORD_TABLE_1_START) / 8);

/**
 * Register access table 2.
 * The MMIO offset of each entry must be a multiple of 8!
 */
static const IOMMUREGACC g_aRegAccess2[] =
{
    /* MMIO offset    Register name               Read Function             Write function */
    { /* 0x1ff8 */    "RSVD_REG",                 NULL,                     NULL                    },

    { /* 0x2000 */    "CMD_BUF_HEAD_PTR",         iommuAmdCmdBufHeadPtr_r,  iommuAmdCmdBufHeadPtr_w },
    { /* 0x2008 */    "CMD_BUF_TAIL_PTR",         iommuAmdCmdBufTailPtr_r , iommuAmdCmdBufTailPtr_w },
    { /* 0x2010 */    "EVT_LOG_HEAD_PTR",         iommuAmdEvtLogHeadPtr_r,  iommuAmdEvtLogHeadPtr_w },
    { /* 0x2018 */    "EVT_LOG_TAIL_PTR",         iommuAmdEvtLogTailPtr_r,  iommuAmdEvtLogTailPtr_w },

    { /* 0x2020 */    "STATUS",                   iommuAmdStatus_r,         iommuAmdStatus_w        },
    { /* 0x2028 */    NULL,                       NULL,                     NULL                    },

    { /* 0x2030 */    "PPR_LOG_HEAD_PTR",         NULL,                     NULL                    },
    { /* 0x2038 */    "PPR_LOG_TAIL_PTR",         NULL,                     NULL                    },

    { /* 0x2040 */    "GALOG_HEAD_PTR",           NULL,                     NULL                    },
    { /* 0x2048 */    "GALOG_TAIL_PTR",           NULL,                     NULL                    },

    { /* 0x2050 */    "PPR_LOG_B_HEAD_PTR",       NULL,                     NULL                    },
    { /* 0x2058 */    "PPR_LOG_B_TAIL_PTR",       NULL,                     NULL                    },

    { /* 0x2060 */    NULL,                       NULL,                     NULL                    },
    { /* 0x2068 */    NULL,                       NULL,                     NULL                    },

    { /* 0x2070 */    "EVT_LOG_B_HEAD_PTR",       NULL,                     NULL                    },
    { /* 0x2078 */    "EVT_LOG_B_TAIL_PTR",       NULL,                     NULL                    },

    { /* 0x2080 */    "PPR_LOG_AUTO_RESP",        NULL,                     NULL                    },
    { /* 0x2088 */    "PPR_LOG_OVERFLOW_EARLY",   NULL,                     NULL                    },
    { /* 0x2090 */    "PPR_LOG_B_OVERFLOW_EARLY", NULL,                     NULL                    }
};
AssertCompile(RT_ELEMENTS(g_aRegAccess2) == (IOMMU_MMIO_OFF_QWORD_TABLE_2_END - IOMMU_MMIO_OFF_QWORD_TABLE_2_START) / 8);


/**
 * Gets the register access structure given its MMIO offset.
 *
 * @returns The register access structure, or NULL if the offset is invalid.
 * @param   off     The MMIO offset of the register being accessed.
 */
static PCIOMMUREGACC iommuAmdGetRegAccess(uint32_t off)
{
    /* Figure out which table the register belongs to and validate its index. */
    PCIOMMUREGACC pReg;
    if (off < IOMMU_MMIO_OFF_QWORD_TABLE_0_END)
    {
        uint32_t const idxReg = off >> 3;
        Assert(idxReg < RT_ELEMENTS(g_aRegAccess0));
        pReg = &g_aRegAccess0[idxReg];
    }
    else if (   off <  IOMMU_MMIO_OFF_QWORD_TABLE_1_END
             && off >= IOMMU_MMIO_OFF_QWORD_TABLE_1_START)
    {
        uint32_t const idxReg = (off - IOMMU_MMIO_OFF_QWORD_TABLE_1_START) >> 3;
        Assert(idxReg < RT_ELEMENTS(g_aRegAccess1));
        pReg = &g_aRegAccess1[idxReg];
    }
    else if (   off <  IOMMU_MMIO_OFF_QWORD_TABLE_2_END
             && off >= IOMMU_MMIO_OFF_QWORD_TABLE_2_START)
    {
        uint32_t const idxReg = (off - IOMMU_MMIO_OFF_QWORD_TABLE_2_START) >> 3;
        Assert(idxReg < RT_ELEMENTS(g_aRegAccess2));
        pReg = &g_aRegAccess2[idxReg];
    }
    else
        pReg = NULL;
    return pReg;
}


/**
 * Writes an IOMMU register (32-bit and 64-bit).
 *
 * @returns Strict VBox status code.
 * @param   pDevIns     The IOMMU device instance.
 * @param   off         MMIO byte offset to the register.
 * @param   cb          The size of the write access.
 * @param   uValue      The value being written.
 *
 * @thread  EMT.
 */
static VBOXSTRICTRC iommuAmdRegisterWrite(PPDMDEVINS pDevIns, uint32_t off, uint8_t cb, uint64_t uValue)
{
    /*
     * Validate the access in case of IOM bug or incorrect assumption.
     */
    Assert(off < IOMMU_MMIO_REGION_SIZE);
    AssertMsgReturn(cb == 4 || cb == 8, ("Invalid access size %u\n", cb), VINF_SUCCESS);
    AssertMsgReturn(!(off & 3), ("Invalid offset %#x\n", off), VINF_SUCCESS);

    Log4Func(("off=%#x cb=%u uValue=%#RX64\n", off, cb, uValue));

    PIOMMU        pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PIOMMUCC      pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUCC);
    PCIOMMUREGACC pReg    = iommuAmdGetRegAccess(off);
    if (pReg)
    { /* likely */ }
    else
    {
        LogFunc(("Writing unknown register %#x with %#RX64 -> Ignored\n", off, uValue));
        return VINF_SUCCESS;
    }

    /* If a write handler doesn't exist, it's either a reserved or read-only register. */
    if (pReg->pfnWrite)
    { /* likely */ }
    else
    {
        LogFunc(("Writing reserved or read-only register off=%#x (cb=%u) with %#RX64 -> Ignored\n", off, cb, uValue));
        return VINF_SUCCESS;
    }

    /*
     * If the write access is 64-bits and aligned on a 64-bit boundary, dispatch right away.
     * This handles writes to 64-bit registers as well as aligned, 64-bit writes to two
     * consecutive 32-bit registers.
     */
    if (cb == 8)
    {
        if (!(off & 7))
        {
            IOMMU_LOCK_RET(pDevIns, pThisCC, VINF_IOM_R3_MMIO_WRITE);
            VBOXSTRICTRC rcStrict = pReg->pfnWrite(pDevIns, pThis, off, uValue);
            IOMMU_UNLOCK(pDevIns, pThisCC);
            return rcStrict;
        }

        LogFunc(("Misaligned access while writing register at off=%#x (cb=%u) with %#RX64 -> Ignored\n", off, cb, uValue));
        return VINF_SUCCESS;
    }

    /* We shouldn't get sizes other than 32 bits here as we've specified so with IOM. */
    Assert(cb == 4);
    if (!(off & 7))
    {
        VBOXSTRICTRC rcStrict;
        IOMMU_LOCK_RET(pDevIns, pThisCC, VINF_IOM_R3_MMIO_WRITE);

        /*
         * Lower 32 bits of a 64-bit register or a 32-bit register is being written.
         * Merge with higher 32 bits (after reading the full 64-bits) and perform a 64-bit write.
         */
        uint64_t u64Read;
        if (pReg->pfnRead)
            rcStrict = pReg->pfnRead(pDevIns, pThis, off, &u64Read);
        else
        {
            rcStrict = VINF_SUCCESS;
            u64Read = 0;
        }

        if (RT_SUCCESS(rcStrict))
        {
            uValue = (u64Read & UINT64_C(0xffffffff00000000)) | uValue;
            rcStrict = pReg->pfnWrite(pDevIns, pThis, off, uValue);
        }
        else
            LogFunc(("Reading off %#x during split write failed! rc=%Rrc\n -> Ignored", off, VBOXSTRICTRC_VAL(rcStrict)));

        IOMMU_UNLOCK(pDevIns, pThisCC);
        return rcStrict;
    }

    /*
     * Higher 32 bits of a 64-bit register or a 32-bit register at a 32-bit boundary is being written.
     * Merge with lower 32 bits (after reading the full 64-bits) and perform a 64-bit write.
     */
    VBOXSTRICTRC rcStrict;
    Assert(!(off & 3));
    Assert(off & 7);
    Assert(off >= 4);
    uint64_t u64Read;
    IOMMU_LOCK_RET(pDevIns, pThisCC, VINF_IOM_R3_MMIO_WRITE);
    if (pReg->pfnRead)
        rcStrict = pReg->pfnRead(pDevIns, pThis, off - 4, &u64Read);
    else
    {
        rcStrict = VINF_SUCCESS;
        u64Read = 0;
    }

    if (RT_SUCCESS(rcStrict))
    {
        uValue = (uValue << 32) | (u64Read & UINT64_C(0xffffffff));
        rcStrict = pReg->pfnWrite(pDevIns, pThis, off - 4, uValue);
    }
    else
        LogFunc(("Reading off %#x during split write failed! rc=%Rrc\n -> Ignored", off, VBOXSTRICTRC_VAL(rcStrict)));

    IOMMU_UNLOCK(pDevIns, pThisCC);
    return rcStrict;
}


/**
 * Reads an IOMMU register (64-bit) given its MMIO offset.
 *
 * All reads are 64-bit but reads to 32-bit registers that are aligned on an 8-byte
 * boundary include the lower half of the subsequent register.
 *
 * This is because most registers are 64-bit and aligned on 8-byte boundaries but
 * some are really 32-bit registers aligned on an 8-byte boundary. We cannot assume
 * software will only perform 32-bit reads on those 32-bit registers that are
 * aligned on 8-byte boundaries.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns     The IOMMU device instance.
 * @param   off         The MMIO offset of the register in bytes.
 * @param   puResult    Where to store the value being read.
 *
 * @thread  EMT.
 */
static VBOXSTRICTRC iommuAmdRegisterRead(PPDMDEVINS pDevIns, uint32_t off, uint64_t *puResult)
{
    Assert(off < IOMMU_MMIO_REGION_SIZE);
    Assert(!(off & 7) || !(off & 3));

    Log4Func(("off=%#x\n", off));

    PIOMMU      pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PIOMMUCC    pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUCC);
    PCPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev); NOREF(pPciDev);

    PCIOMMUREGACC pReg = iommuAmdGetRegAccess(off);
    if (pReg)
    { /* likely */ }
    else
    {
        LogFunc(("Reading unknown register %#x -> Ignored\n", off));
        return VINF_IOM_MMIO_UNUSED_FF;
    }

    /* If a read handler doesn't exist, it's a reserved or unknown register. */
    if (pReg->pfnRead)
    { /* likely */ }
    else
    {
        LogFunc(("Reading reserved or unknown register off=%#x -> returning 0s\n", off));
        return VINF_IOM_MMIO_UNUSED_00;
    }

    /*
     * If the read access is aligned on a 64-bit boundary, read the full 64-bits and return.
     * The caller takes care of truncating upper 32 bits for 32-bit reads.
     */
    if (!(off & 7))
    {
        IOMMU_LOCK_RET(pDevIns, pThisCC, VINF_IOM_R3_MMIO_READ);
        VBOXSTRICTRC rcStrict = pReg->pfnRead(pDevIns, pThis, off, puResult);
        IOMMU_UNLOCK(pDevIns, pThisCC);
        return rcStrict;
    }

    /*
     * High 32 bits of a 64-bit register or a 32-bit register at a non 64-bit boundary is being read.
     * Read full 64 bits at the previous 64-bit boundary but return only the high 32 bits.
     */
    Assert(!(off & 3));
    Assert(off & 7);
    Assert(off >= 4);
    IOMMU_LOCK_RET(pDevIns, pThisCC, VINF_IOM_R3_MMIO_READ);
    VBOXSTRICTRC rcStrict = pReg->pfnRead(pDevIns, pThis, off - 4, puResult);
    IOMMU_UNLOCK(pDevIns, pThisCC);
    if (RT_SUCCESS(rcStrict))
        *puResult >>= 32;
    else
    {
        *puResult = 0;
        LogFunc(("Reading off %#x during split read failed! rc=%Rrc\n -> Ignored", off, VBOXSTRICTRC_VAL(rcStrict)));
    }

    return rcStrict;
}


/**
 * Raises the MSI interrupt for the IOMMU device.
 *
 * @param   pDevIns     The IOMMU device instance.
 *
 * @thread  Any.
 * @remarks The IOMMU lock may or may not be held.
 */
static void iommuAmdMsiInterruptRaise(PPDMDEVINS pDevIns)
{
    LogFlowFunc(("\n"));
    if (iommuAmdIsMsiEnabled(pDevIns))
    {
        LogFunc(("Raising MSI\n"));
        PDMDevHlpPCISetIrq(pDevIns, 0, PDM_IRQ_LEVEL_HIGH);
    }
}

#if 0
/**
 * Clears the MSI interrupt for the IOMMU device.
 *
 * @param   pDevIns     The IOMMU device instance.
 *
 * @thread  Any.
 * @remarks The IOMMU lock may or may not be held.
 */
static void iommuAmdMsiInterruptClear(PPDMDEVINS pDevIns)
{
    if (iommuAmdIsMsiEnabled(pDevIns))
        PDMDevHlpPCISetIrq(pDevIns, 0, PDM_IRQ_LEVEL_LOW);
}
#endif

/**
 * Writes an entry to the event log in memory.
 *
 * @returns VBox status code.
 * @param   pDevIns     The IOMMU device instance.
 * @param   pEvent      The event to log.
 *
 * @thread  Any.
 * @remarks The IOMMU lock must be held while calling this function.
 */
static int iommuAmdEvtLogEntryWrite(PPDMDEVINS pDevIns, PCEVT_GENERIC_T pEvent)
{
    PIOMMU   pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PIOMMUCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUCC);

    IOMMU_LOCK(pDevIns, pThisCC);

    /* Check if event logging is active and the log has not overflowed. */
    IOMMU_STATUS_T const Status = pThis->Status;
    if (   Status.n.u1EvtLogRunning
        && !Status.n.u1EvtOverflow)
    {
        uint32_t const cbEvt = sizeof(*pEvent);

        /* Get the offset we need to write the event to in memory (circular buffer offset). */
        uint32_t const offEvt = pThis->EvtLogTailPtr.n.off;
        Assert(!(offEvt & ~IOMMU_EVT_LOG_TAIL_PTR_VALID_MASK));

        /* Ensure we have space in the event log. */
        uint32_t const cMaxEvts = iommuAmdGetBufMaxEntries(pThis->EvtLogBaseAddr.n.u4Len);
        uint32_t const cEvts    = iommuAmdGetEvtLogEntryCount(pThis);
        if (cEvts + 1 < cMaxEvts)
        {
            /* Write the event log entry to memory. */
            RTGCPHYS const GCPhysEvtLog      = pThis->EvtLogBaseAddr.n.u40Base << X86_PAGE_4K_SHIFT;
            RTGCPHYS const GCPhysEvtLogEntry = GCPhysEvtLog + offEvt;
            int rc = PDMDevHlpPCIPhysWrite(pDevIns, GCPhysEvtLogEntry, pEvent, cbEvt);
            if (RT_FAILURE(rc))
                LogFunc(("Failed to write event log entry at %#RGp. rc=%Rrc\n", GCPhysEvtLogEntry, rc));

            /* Increment the event log tail pointer. */
            uint32_t const cbEvtLog = iommuAmdGetTotalBufLength(pThis->EvtLogBaseAddr.n.u4Len);
            pThis->EvtLogTailPtr.n.off = (offEvt + cbEvt) % cbEvtLog;

            /* Indicate that an event log entry was written. */
            ASMAtomicOrU64(&pThis->Status.u64, IOMMU_STATUS_EVT_LOG_INTR);

            /* Check and signal an interrupt if software wants to receive one when an event log entry is written. */
            if (pThis->Ctrl.n.u1EvtIntrEn)
                iommuAmdMsiInterruptRaise(pDevIns);
        }
        else
        {
            /* Indicate that the event log has overflowed. */
            ASMAtomicOrU64(&pThis->Status.u64, IOMMU_STATUS_EVT_LOG_OVERFLOW);

            /* Check and signal an interrupt if software wants to receive one when the event log has overflowed. */
            if (pThis->Ctrl.n.u1EvtIntrEn)
                iommuAmdMsiInterruptRaise(pDevIns);
        }
    }

    IOMMU_UNLOCK(pDevIns, pThisCC);

    return VINF_SUCCESS;
}


/**
 * Sets an event in the hardware error registers.
 *
 * @param   pDevIns     The IOMMU device instance.
 * @param   pEvent      The event.
 *
 * @thread  Any.
 */
static void iommuAmdHwErrorSet(PPDMDEVINS pDevIns, PCEVT_GENERIC_T pEvent)
{
    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    if (pThis->ExtFeat.n.u1HwErrorSup)
    {
        if (pThis->HwEvtStatus.n.u1Valid)
            pThis->HwEvtStatus.n.u1Overflow = 1;
        pThis->HwEvtStatus.n.u1Valid = 1;
        pThis->HwEvtHi.u64 = RT_MAKE_U64(pEvent->au32[0], pEvent->au32[1]);
        pThis->HwEvtLo     = RT_MAKE_U64(pEvent->au32[2], pEvent->au32[3]);
        Assert(   pThis->HwEvtHi.n.u4EvtCode == IOMMU_EVT_DEV_TAB_HW_ERROR
               || pThis->HwEvtHi.n.u4EvtCode == IOMMU_EVT_PAGE_TAB_HW_ERROR
               || pThis->HwEvtHi.n.u4EvtCode == IOMMU_EVT_COMMAND_HW_ERROR);
    }
}


/**
 * Initializes a PAGE_TAB_HARDWARE_ERROR event.
 *
 * @param   idDevice            The device ID (bus, device, function).
 * @param   idDomain            The domain ID.
 * @param   GCPhysPtEntity      The system physical address of the page table
 *                              entity.
 * @param   enmOp               The IOMMU operation being performed.
 * @param   pEvtPageTabHwErr    Where to store the initialized event.
 */
static void iommuAmdPageTabHwErrorEventInit(uint16_t idDevice, uint16_t idDomain, RTGCPHYS GCPhysPtEntity, IOMMUOP enmOp,
                                            PEVT_PAGE_TAB_HW_ERR_T pEvtPageTabHwErr)
{
    memset(pEvtPageTabHwErr, 0, sizeof(*pEvtPageTabHwErr));
    pEvtPageTabHwErr->n.u16DevId           = idDevice;
    pEvtPageTabHwErr->n.u16DomainOrPasidLo = idDomain;
    pEvtPageTabHwErr->n.u1GuestOrNested    = 0;
    pEvtPageTabHwErr->n.u1Interrupt        = RT_BOOL(enmOp == IOMMUOP_INTR_REQ);
    pEvtPageTabHwErr->n.u1ReadWrite        = RT_BOOL(enmOp == IOMMUOP_MEM_WRITE);
    pEvtPageTabHwErr->n.u1Translation      = RT_BOOL(enmOp == IOMMUOP_TRANSLATE_REQ);
    pEvtPageTabHwErr->n.u2Type             = enmOp == IOMMUOP_CMD ? HWEVTTYPE_DATA_ERROR : HWEVTTYPE_TARGET_ABORT;
    pEvtPageTabHwErr->n.u4EvtCode          = IOMMU_EVT_PAGE_TAB_HW_ERROR;
    pEvtPageTabHwErr->n.u64Addr            = GCPhysPtEntity;
}


/**
 * Raises a PAGE_TAB_HARDWARE_ERROR event.
 *
 * @param   pDevIns             The IOMMU device instance.
 * @param   enmOp               The IOMMU operation being performed.
 * @param   pEvtPageTabHwErr    The page table hardware error event.
 *
 * @thread  Any.
 */
static void iommuAmdPageTabHwErrorEventRaise(PPDMDEVINS pDevIns, IOMMUOP enmOp, PEVT_PAGE_TAB_HW_ERR_T pEvtPageTabHwErr)
{
    AssertCompile(sizeof(EVT_GENERIC_T) == sizeof(EVT_PAGE_TAB_HW_ERR_T));
    PCEVT_GENERIC_T pEvent = (PCEVT_GENERIC_T)pEvtPageTabHwErr;

    PIOMMUCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUCC);
    IOMMU_LOCK(pDevIns, pThisCC);

    iommuAmdHwErrorSet(pDevIns, (PCEVT_GENERIC_T)pEvent);
    iommuAmdEvtLogEntryWrite(pDevIns, (PCEVT_GENERIC_T)pEvent);
    if (enmOp != IOMMUOP_CMD)
        iommuAmdSetPciTargetAbort(pDevIns);

    IOMMU_UNLOCK(pDevIns, pThisCC);

    LogFunc(("Raised PAGE_TAB_HARDWARE_ERROR. idDevice=%#x idDomain=%#x GCPhysPtEntity=%#RGp enmOp=%u u2Type=%u\n",
         pEvtPageTabHwErr->n.u16DevId, pEvtPageTabHwErr->n.u16DomainOrPasidLo, pEvtPageTabHwErr->n.u64Addr, enmOp,
         pEvtPageTabHwErr->n.u2Type));
}


#ifdef IN_RING3
/**
 * Initializes a COMMAND_HARDWARE_ERROR event.
 *
 * @param   GCPhysAddr      The system physical address the IOMMU attempted to access.
 * @param   pEvtCmdHwErr    Where to store the initialized event.
 */
static void iommuAmdCmdHwErrorEventInit(RTGCPHYS GCPhysAddr, PEVT_CMD_HW_ERR_T pEvtCmdHwErr)
{
    memset(pEvtCmdHwErr, 0, sizeof(*pEvtCmdHwErr));
    pEvtCmdHwErr->n.u2Type    = HWEVTTYPE_DATA_ERROR;
    pEvtCmdHwErr->n.u4EvtCode = IOMMU_EVT_COMMAND_HW_ERROR;
    pEvtCmdHwErr->n.u64Addr   = GCPhysAddr;
}


/**
 * Raises a COMMAND_HARDWARE_ERROR event.
 *
 * @param   pDevIns         The IOMMU device instance.
 * @param   pEvtCmdHwErr    The command hardware error event.
 *
 * @thread  Any.
 */
static void iommuAmdCmdHwErrorEventRaise(PPDMDEVINS pDevIns, PCEVT_CMD_HW_ERR_T pEvtCmdHwErr)
{
    AssertCompile(sizeof(EVT_GENERIC_T) == sizeof(EVT_CMD_HW_ERR_T));
    PCEVT_GENERIC_T pEvent = (PCEVT_GENERIC_T)pEvtCmdHwErr;
    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);

    PIOMMUCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUCC);
    IOMMU_LOCK(pDevIns, pThisCC);

    iommuAmdHwErrorSet(pDevIns, (PCEVT_GENERIC_T)pEvent);
    iommuAmdEvtLogEntryWrite(pDevIns, (PCEVT_GENERIC_T)pEvent);
    ASMAtomicAndU64(&pThis->Status.u64, ~IOMMU_STATUS_CMD_BUF_RUNNING);

    IOMMU_UNLOCK(pDevIns, pThisCC);

    LogFunc(("Raised COMMAND_HARDWARE_ERROR. GCPhysCmd=%#RGp u2Type=%u\n", pEvtCmdHwErr->n.u64Addr, pEvtCmdHwErr->n.u2Type));
}
#endif /* IN_RING3 */


/**
 * Initializes a DEV_TAB_HARDWARE_ERROR event.
 *
 * @param   idDevice            The device ID (bus, device, function).
 * @param   GCPhysDte           The system physical address of the failed device table
 *                              access.
 * @param   enmOp               The IOMMU operation being performed.
 * @param   pEvtDevTabHwErr     Where to store the initialized event.
 */
static void iommuAmdDevTabHwErrorEventInit(uint16_t idDevice, RTGCPHYS GCPhysDte, IOMMUOP enmOp,
                                           PEVT_DEV_TAB_HW_ERROR_T pEvtDevTabHwErr)
{
    memset(pEvtDevTabHwErr, 0, sizeof(*pEvtDevTabHwErr));
    pEvtDevTabHwErr->n.u16DevId      = idDevice;
    pEvtDevTabHwErr->n.u1Intr        = RT_BOOL(enmOp == IOMMUOP_INTR_REQ);
    /** @todo IOMMU: Any other transaction type that can set read/write bit? */
    pEvtDevTabHwErr->n.u1ReadWrite   = RT_BOOL(enmOp == IOMMUOP_MEM_WRITE);
    pEvtDevTabHwErr->n.u1Translation = RT_BOOL(enmOp == IOMMUOP_TRANSLATE_REQ);
    pEvtDevTabHwErr->n.u2Type        = enmOp == IOMMUOP_CMD ? HWEVTTYPE_DATA_ERROR : HWEVTTYPE_TARGET_ABORT;
    pEvtDevTabHwErr->n.u4EvtCode     = IOMMU_EVT_DEV_TAB_HW_ERROR;
    pEvtDevTabHwErr->n.u64Addr       = GCPhysDte;
}


/**
 * Raises a DEV_TAB_HARDWARE_ERROR event.
 *
 * @param   pDevIns             The IOMMU device instance.
 * @param   enmOp               The IOMMU operation being performed.
 * @param   pEvtDevTabHwErr     The device table hardware error event.
 *
 * @thread  Any.
 */
static void iommuAmdDevTabHwErrorEventRaise(PPDMDEVINS pDevIns, IOMMUOP enmOp, PEVT_DEV_TAB_HW_ERROR_T pEvtDevTabHwErr)
{
    AssertCompile(sizeof(EVT_GENERIC_T) == sizeof(EVT_DEV_TAB_HW_ERROR_T));
    PCEVT_GENERIC_T pEvent = (PCEVT_GENERIC_T)pEvtDevTabHwErr;

    PIOMMUCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUCC);
    IOMMU_LOCK(pDevIns, pThisCC);

    iommuAmdHwErrorSet(pDevIns, (PCEVT_GENERIC_T)pEvent);
    iommuAmdEvtLogEntryWrite(pDevIns, (PCEVT_GENERIC_T)pEvent);
    if (enmOp != IOMMUOP_CMD)
        iommuAmdSetPciTargetAbort(pDevIns);

    IOMMU_UNLOCK(pDevIns, pThisCC);

    LogFunc(("Raised DEV_TAB_HARDWARE_ERROR. idDevice=%#x GCPhysDte=%#RGp enmOp=%u u2Type=%u\n", pEvtDevTabHwErr->n.u16DevId,
             pEvtDevTabHwErr->n.u64Addr, enmOp, pEvtDevTabHwErr->n.u2Type));
}


#ifdef IN_RING3
/**
 * Initializes an ILLEGAL_COMMAND_ERROR event.
 *
 * @param   GCPhysCmd       The system physical address of the failed command
 *                          access.
 * @param   pEvtIllegalCmd  Where to store the initialized event.
 */
static void iommuAmdIllegalCmdEventInit(RTGCPHYS GCPhysCmd, PEVT_ILLEGAL_CMD_ERR_T pEvtIllegalCmd)
{
    Assert(!(GCPhysCmd & UINT64_C(0xf)));
    memset(pEvtIllegalCmd, 0, sizeof(*pEvtIllegalCmd));
    pEvtIllegalCmd->n.u4EvtCode = IOMMU_EVT_ILLEGAL_CMD_ERROR;
    pEvtIllegalCmd->n.u64Addr   = GCPhysCmd;
}


/**
 * Raises an ILLEGAL_COMMAND_ERROR event.
 *
 * @param   pDevIns         The IOMMU device instance.
 * @param   pEvtIllegalCmd  The illegal command error event.
 */
static void iommuAmdIllegalCmdEventRaise(PPDMDEVINS pDevIns, PCEVT_ILLEGAL_CMD_ERR_T pEvtIllegalCmd)
{
    AssertCompile(sizeof(EVT_GENERIC_T) == sizeof(EVT_ILLEGAL_DTE_T));
    PCEVT_GENERIC_T pEvent = (PCEVT_GENERIC_T)pEvtIllegalCmd;
    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);

    iommuAmdEvtLogEntryWrite(pDevIns, pEvent);
    ASMAtomicAndU64(&pThis->Status.u64, ~IOMMU_STATUS_CMD_BUF_RUNNING);

    LogFunc(("Raised ILLEGAL_COMMAND_ERROR. Addr=%#RGp\n", pEvtIllegalCmd->n.u64Addr));
}
#endif  /* IN_RING3 */


/**
 * Initializes an ILLEGAL_DEV_TABLE_ENTRY event.
 *
 * @param   idDevice        The device ID (bus, device, function).
 * @param   uIova           The I/O virtual address.
 * @param   fRsvdNotZero    Whether reserved bits are not zero. Pass @c false if the
 *                          event was caused by an invalid level encoding in the
 *                          DTE.
 * @param   enmOp           The IOMMU operation being performed.
 * @param   pEvtIllegalDte  Where to store the initialized event.
 */
static void iommuAmdIllegalDteEventInit(uint16_t idDevice, uint64_t uIova, bool fRsvdNotZero, IOMMUOP enmOp,
                                        PEVT_ILLEGAL_DTE_T pEvtIllegalDte)
{
    memset(pEvtIllegalDte, 0, sizeof(*pEvtIllegalDte));
    pEvtIllegalDte->n.u16DevId      = idDevice;
    pEvtIllegalDte->n.u1Interrupt   = RT_BOOL(enmOp == IOMMUOP_INTR_REQ);
    pEvtIllegalDte->n.u1ReadWrite   = RT_BOOL(enmOp == IOMMUOP_MEM_WRITE);
    pEvtIllegalDte->n.u1RsvdNotZero = fRsvdNotZero;
    pEvtIllegalDte->n.u1Translation = RT_BOOL(enmOp == IOMMUOP_TRANSLATE_REQ);
    pEvtIllegalDte->n.u4EvtCode     = IOMMU_EVT_ILLEGAL_DEV_TAB_ENTRY;
    pEvtIllegalDte->n.u64Addr       = uIova & ~UINT64_C(0x3);
    /** @todo r=ramshankar: Not sure why the last 2 bits are marked as reserved by the
     *        IOMMU spec here but not for this field for I/O page fault event. */
    Assert(!(uIova & UINT64_C(0x3)));
}


/**
 * Raises an ILLEGAL_DEV_TABLE_ENTRY event.
 *
 * @param   pDevIns         The IOMMU instance data.
 * @param   enmOp           The IOMMU operation being performed.
 * @param   pEvtIllegalDte  The illegal device table entry event.
 * @param   enmEvtType      The illegal device table entry event type.
 *
 * @thread  Any.
 */
static void iommuAmdIllegalDteEventRaise(PPDMDEVINS pDevIns, IOMMUOP enmOp, PCEVT_ILLEGAL_DTE_T pEvtIllegalDte,
                                         EVT_ILLEGAL_DTE_TYPE_T enmEvtType)
{
    AssertCompile(sizeof(EVT_GENERIC_T) == sizeof(EVT_ILLEGAL_DTE_T));
    PCEVT_GENERIC_T pEvent = (PCEVT_GENERIC_T)pEvtIllegalDte;

    iommuAmdEvtLogEntryWrite(pDevIns, pEvent);
    if (enmOp != IOMMUOP_CMD)
        iommuAmdSetPciTargetAbort(pDevIns);

    LogFunc(("Raised ILLEGAL_DTE_EVENT. idDevice=%#x uIova=%#RX64 enmOp=%u enmEvtType=%u\n", pEvtIllegalDte->n.u16DevId,
             pEvtIllegalDte->n.u64Addr, enmOp, enmEvtType));
    NOREF(enmEvtType);
}


/**
 * Initializes an IO_PAGE_FAULT event.
 *
 * @param   idDevice            The device ID (bus, device, function).
 * @param   idDomain            The domain ID.
 * @param   uIova               The I/O virtual address being accessed.
 * @param   fPresent            Transaction to a page marked as present (including
 *                              DTE.V=1) or interrupt marked as remapped
 *                              (IRTE.RemapEn=1).
 * @param   fRsvdNotZero        Whether reserved bits are not zero. Pass @c false if
 *                              the I/O page fault was caused by invalid level
 *                              encoding.
 * @param   fPermDenied         Permission denied for the address being accessed.
 * @param   enmOp               The IOMMU operation being performed.
 * @param   pEvtIoPageFault     Where to store the initialized event.
 */
static void iommuAmdIoPageFaultEventInit(uint16_t idDevice, uint16_t idDomain, uint64_t uIova, bool fPresent, bool fRsvdNotZero,
                                         bool fPermDenied, IOMMUOP enmOp, PEVT_IO_PAGE_FAULT_T pEvtIoPageFault)
{
    Assert(!fPermDenied || fPresent);
    memset(pEvtIoPageFault, 0, sizeof(*pEvtIoPageFault));
    pEvtIoPageFault->n.u16DevId            = idDevice;
    //pEvtIoPageFault->n.u4PasidHi         = 0;
    pEvtIoPageFault->n.u16DomainOrPasidLo  = idDomain;
    //pEvtIoPageFault->n.u1GuestOrNested   = 0;
    //pEvtIoPageFault->n.u1NoExecute       = 0;
    //pEvtIoPageFault->n.u1User            = 0;
    pEvtIoPageFault->n.u1Interrupt         = RT_BOOL(enmOp == IOMMUOP_INTR_REQ);
    pEvtIoPageFault->n.u1Present           = fPresent;
    pEvtIoPageFault->n.u1ReadWrite         = RT_BOOL(enmOp == IOMMUOP_MEM_WRITE);
    pEvtIoPageFault->n.u1PermDenied        = fPermDenied;
    pEvtIoPageFault->n.u1RsvdNotZero       = fRsvdNotZero;
    pEvtIoPageFault->n.u1Translation       = RT_BOOL(enmOp == IOMMUOP_TRANSLATE_REQ);
    pEvtIoPageFault->n.u4EvtCode           = IOMMU_EVT_IO_PAGE_FAULT;
    pEvtIoPageFault->n.u64Addr             = uIova;
}


/**
 * Raises an IO_PAGE_FAULT event.
 *
 * @param   pDevIns             The IOMMU instance data.
 * @param   fIoDevFlags         The I/O device flags, see IOMMU_DTE_CACHE_F_XXX.
 * @param   pIrte               The interrupt remapping table entry, can be NULL.
 * @param   enmOp               The IOMMU operation being performed.
 * @param   pEvtIoPageFault     The I/O page fault event.
 * @param   enmEvtType          The I/O page fault event type.
 *
 * @thread  Any.
 */
static void iommuAmdIoPageFaultEventRaise(PPDMDEVINS pDevIns, uint16_t fIoDevFlags, PCIRTE_T pIrte, IOMMUOP enmOp,
                                          PCEVT_IO_PAGE_FAULT_T pEvtIoPageFault, EVT_IO_PAGE_FAULT_TYPE_T enmEvtType)
{
    AssertCompile(sizeof(EVT_GENERIC_T) == sizeof(EVT_IO_PAGE_FAULT_T));
    PCEVT_GENERIC_T pEvent = (PCEVT_GENERIC_T)pEvtIoPageFault;
    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    STAM_COUNTER_INC(&pThis->StatIopfs); NOREF(pThis);

#ifdef IOMMU_WITH_DTE_CACHE
# define IOMMU_DTE_CACHE_SET_PF_RAISED(a_pDevIns, a_DevId)  iommuAmdDteCacheUpdateFlags((a_pDevIns), (a_DevId), \
                                                                                        IOMMU_DTE_CACHE_F_IO_PAGE_FAULT_RAISED, \
                                                                                        0 /* fAndMask */)
#else
# define IOMMU_DTE_CACHE_SET_PF_RAISED(a_pDevIns, a_DevId)  do { } while (0)
#endif

    bool fSuppressEvtLogging = false;
    if (   enmOp == IOMMUOP_MEM_READ
        || enmOp == IOMMUOP_MEM_WRITE)
    {
        uint16_t const fSuppressIopf    = IOMMU_DTE_CACHE_F_VALID
                                        | IOMMU_DTE_CACHE_F_SUPPRESS_IOPF | IOMMU_DTE_CACHE_F_IO_PAGE_FAULT_RAISED;
        uint16_t const fSuppressAllIopf = IOMMU_DTE_CACHE_F_VALID | IOMMU_DTE_CACHE_F_SUPPRESS_ALL_IOPF;
        if (   (fIoDevFlags & fSuppressAllIopf) == fSuppressAllIopf
            || (fIoDevFlags & fSuppressIopf) == fSuppressIopf)
        {
            fSuppressEvtLogging = true;
        }
    }
    else if (enmOp == IOMMUOP_INTR_REQ)
    {
        uint16_t const fSuppressIopf = IOMMU_DTE_CACHE_F_INTR_MAP_VALID | IOMMU_DTE_CACHE_F_IGNORE_UNMAPPED_INTR;
        if ((fIoDevFlags & fSuppressIopf) == fSuppressIopf)
            fSuppressEvtLogging = true;
        else if (pIrte)     /** @todo Make this compulsary and assert if it isn't provided. */
            fSuppressEvtLogging = pIrte->n.u1SuppressIoPf;
    }
    /* else: Events are never suppressed for commands. */

    switch (enmEvtType)
    {
        case kIoPageFaultType_PermDenied:
        {
            /* Cannot be triggered by a command. */
            Assert(enmOp != IOMMUOP_CMD);
            RT_FALL_THRU();
        }
        case kIoPageFaultType_DteRsvdPagingMode:
        case kIoPageFaultType_PteInvalidPageSize:
        case kIoPageFaultType_PteInvalidLvlEncoding:
        case kIoPageFaultType_SkippedLevelIovaNotZero:
        case kIoPageFaultType_PteRsvdNotZero:
        case kIoPageFaultType_PteValidNotSet:
        case kIoPageFaultType_DteTranslationDisabled:
        case kIoPageFaultType_PasidInvalidRange:
        {
            /*
             * For a translation request, the IOMMU doesn't signal an I/O page fault nor does it
             * create an event log entry. See AMD IOMMU spec. 2.1.3.2 "I/O Page Faults".
             */
            if (enmOp != IOMMUOP_TRANSLATE_REQ)
            {
                if (!fSuppressEvtLogging)
                {
                    iommuAmdEvtLogEntryWrite(pDevIns, pEvent);
                    IOMMU_DTE_CACHE_SET_PF_RAISED(pDevIns, pEvtIoPageFault->n.u16DevId);
                }
                if (enmOp != IOMMUOP_CMD)
                    iommuAmdSetPciTargetAbort(pDevIns);
            }
            break;
        }

        case kIoPageFaultType_UserSupervisor:
        {
            /* Access is blocked and only creates an event log entry. */
            if (!fSuppressEvtLogging)
            {
                iommuAmdEvtLogEntryWrite(pDevIns, pEvent);
                IOMMU_DTE_CACHE_SET_PF_RAISED(pDevIns, pEvtIoPageFault->n.u16DevId);
            }
            break;
        }

        case kIoPageFaultType_IrteAddrInvalid:
        case kIoPageFaultType_IrteRsvdNotZero:
        case kIoPageFaultType_IrteRemapEn:
        case kIoPageFaultType_IrteRsvdIntType:
        case kIoPageFaultType_IntrReqAborted:
        case kIoPageFaultType_IntrWithPasid:
        {
            /* Only trigerred by interrupt requests. */
            Assert(enmOp == IOMMUOP_INTR_REQ);
            if (!fSuppressEvtLogging)
            {
                iommuAmdEvtLogEntryWrite(pDevIns, pEvent);
                IOMMU_DTE_CACHE_SET_PF_RAISED(pDevIns, pEvtIoPageFault->n.u16DevId);
            }
            iommuAmdSetPciTargetAbort(pDevIns);
            break;
        }

        case kIoPageFaultType_SmiFilterMismatch:
        {
            /* Not supported and probably will never be, assert. */
            AssertMsgFailed(("kIoPageFaultType_SmiFilterMismatch - Upstream SMI requests not supported/implemented."));
            break;
        }

        case kIoPageFaultType_DevId_Invalid:
        {
            /* Cannot be triggered by a command. */
            Assert(enmOp != IOMMUOP_CMD);
            Assert(enmOp != IOMMUOP_TRANSLATE_REQ); /** @todo IOMMU: We don't support translation requests yet. */
            if (!fSuppressEvtLogging)
            {
                iommuAmdEvtLogEntryWrite(pDevIns, pEvent);
                IOMMU_DTE_CACHE_SET_PF_RAISED(pDevIns, pEvtIoPageFault->n.u16DevId);
            }
            if (   enmOp == IOMMUOP_MEM_READ
                || enmOp == IOMMUOP_MEM_WRITE)
                iommuAmdSetPciTargetAbort(pDevIns);
            break;
        }
    }

#undef IOMMU_DTE_CACHE_SET_PF_RAISED
}


/**
 * Raises an IO_PAGE_FAULT event given the DTE.
 *
 * @param   pDevIns             The IOMMU instance data.
 * @param   pDte                The device table entry.
 * @param   pIrte               The interrupt remapping table entry, can be NULL.
 * @param   enmOp               The IOMMU operation being performed.
 * @param   pEvtIoPageFault     The I/O page fault event.
 * @param   enmEvtType          The I/O page fault event type.
 *
 * @thread  Any.
 */
static void iommuAmdIoPageFaultEventRaiseWithDte(PPDMDEVINS pDevIns, PCDTE_T pDte, PCIRTE_T pIrte, IOMMUOP enmOp,
                                                 PCEVT_IO_PAGE_FAULT_T pEvtIoPageFault, EVT_IO_PAGE_FAULT_TYPE_T enmEvtType)
{
    Assert(pDte);
    uint16_t const fIoDevFlags = iommuAmdGetBasicDevFlags(pDte);
    return iommuAmdIoPageFaultEventRaise(pDevIns, fIoDevFlags, pIrte, enmOp, pEvtIoPageFault, enmEvtType);
}


/**
 * Reads a device table entry for the given the device ID.
 *
 * @returns VBox status code.
 * @param   pDevIns     The IOMMU device instance.
 * @param   idDevice    The device ID (bus, device, function).
 * @param   enmOp       The IOMMU operation being performed.
 * @param   pDte        Where to store the device table entry.
 *
 * @thread  Any.
 */
static int iommuAmdDteRead(PPDMDEVINS pDevIns, uint16_t idDevice, IOMMUOP enmOp, PDTE_T pDte)
{
    PCIOMMU  pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PIOMMUCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUCC);

    IOMMU_LOCK(pDevIns, pThisCC);

    /* Figure out which device table segment is being accessed. */
    uint8_t const idxSegsEn = pThis->Ctrl.n.u3DevTabSegEn;
    Assert(idxSegsEn < RT_ELEMENTS(g_auDevTabSegShifts));

    uint8_t const idxSeg = (idDevice & g_auDevTabSegMasks[idxSegsEn]) >> g_auDevTabSegShifts[idxSegsEn];
    Assert(idxSeg < RT_ELEMENTS(pThis->aDevTabBaseAddrs));
    AssertCompile(RT_ELEMENTS(g_auDevTabSegShifts) == RT_ELEMENTS(g_auDevTabSegMasks));

    RTGCPHYS const GCPhysDevTab = pThis->aDevTabBaseAddrs[idxSeg].n.u40Base << X86_PAGE_4K_SHIFT;
    uint32_t const offDte       = (idDevice & ~g_auDevTabSegMasks[idxSegsEn]) * sizeof(DTE_T);
    RTGCPHYS const GCPhysDte    = GCPhysDevTab + offDte;

    /* Ensure the DTE falls completely within the device table segment. */
    uint32_t const cbDevTabSeg  = (pThis->aDevTabBaseAddrs[idxSeg].n.u9Size + 1) << X86_PAGE_4K_SHIFT;

    IOMMU_UNLOCK(pDevIns, pThisCC);

    if (offDte + sizeof(DTE_T) <= cbDevTabSeg)
    {
        /* Read the device table entry from guest memory. */
        Assert(!(GCPhysDevTab & X86_PAGE_4K_OFFSET_MASK));
        int rc = PDMDevHlpPCIPhysRead(pDevIns, GCPhysDte, pDte, sizeof(*pDte));
        if (RT_SUCCESS(rc))
            return VINF_SUCCESS;

        /* Raise a device table hardware error. */
        LogFunc(("Failed to read device table entry at %#RGp. rc=%Rrc -> DevTabHwError\n", GCPhysDte, rc));

        EVT_DEV_TAB_HW_ERROR_T EvtDevTabHwErr;
        iommuAmdDevTabHwErrorEventInit(idDevice, GCPhysDte, enmOp, &EvtDevTabHwErr);
        iommuAmdDevTabHwErrorEventRaise(pDevIns, enmOp, &EvtDevTabHwErr);
        return VERR_IOMMU_DTE_READ_FAILED;
    }

    /* Raise an I/O page fault for out-of-bounds acccess. */
    LogFunc(("Out-of-bounds device table entry. idDevice=%#x offDte=%u cbDevTabSeg=%u -> IOPF\n", idDevice, offDte, cbDevTabSeg));
    EVT_IO_PAGE_FAULT_T EvtIoPageFault;
    iommuAmdIoPageFaultEventInit(idDevice, 0 /* idDomain */, 0 /* uIova */, false /* fPresent */, false /* fRsvdNotZero */,
                                 false /* fPermDenied */, enmOp, &EvtIoPageFault);
    iommuAmdIoPageFaultEventRaise(pDevIns, 0 /* fIoDevFlags */, NULL /* pIrte */, enmOp, &EvtIoPageFault,
                                  kIoPageFaultType_DevId_Invalid);
    return VERR_IOMMU_DTE_BAD_OFFSET;
}


/**
 * Performs pre-translation checks for the given device table entry.
 *
 * @returns VBox status code.
 * @retval VINF_SUCCESS if the DTE is valid and supports address translation.
 * @retval VINF_IOMMU_ADDR_TRANSLATION_DISABLED if the DTE is valid but address
 *         translation is disabled.
 * @retval VERR_IOMMU_ADDR_TRANSLATION_FAILED if an error occurred and any
 *         corresponding event was raised.
 * @retval VERR_IOMMU_ADDR_ACCESS_DENIED if the DTE denies the requested
 *         permissions.
 *
 * @param   pDevIns         The IOMMU device instance.
 * @param   uIova           The I/O virtual address to translate.
 * @param   idDevice        The device ID (bus, device, function).
 * @param   fPerm           The I/O permissions for this access, see
 *                          IOMMU_IO_PERM_XXX.
 * @param   pDte            The device table entry.
 * @param   enmOp           The IOMMU operation being performed.
 *
 * @thread  Any.
 */
static int iommuAmdPreTranslateChecks(PPDMDEVINS pDevIns, uint16_t idDevice, uint64_t uIova, uint8_t fPerm, PCDTE_T pDte,
                                      IOMMUOP enmOp)
{
    /*
     * Check if the translation is valid, otherwise raise an I/O page fault.
     */
    if (pDte->n.u1TranslationValid)
    { /* likely */ }
    else
    {
        /** @todo r=ramshankar: The AMD IOMMU spec. says page walk is terminated but
         *        doesn't explicitly say whether an I/O page fault is raised. From other
         *        places in the spec. it seems early page walk terminations (starting with
         *        the DTE) return the state computed so far and raises an I/O page fault. So
         *        returning an invalid translation rather than skipping translation. */
        LogFunc(("Translation valid bit not set -> IOPF\n"));
        EVT_IO_PAGE_FAULT_T EvtIoPageFault;
        iommuAmdIoPageFaultEventInit(idDevice, pDte->n.u16DomainId, uIova, false /* fPresent */, false /* fRsvdNotZero */,
                                     false /* fPermDenied */, enmOp, &EvtIoPageFault);
        iommuAmdIoPageFaultEventRaiseWithDte(pDevIns, pDte, NULL /* pIrte */, enmOp, &EvtIoPageFault,
                                             kIoPageFaultType_DteTranslationDisabled);
        return VERR_IOMMU_ADDR_TRANSLATION_FAILED;
    }

    /*
     * Check permissions bits in the DTE.
     * Note: This MUST be checked prior to checking the root page table level below!
     */
    uint8_t const fDtePerm = (pDte->au64[0] >> IOMMU_IO_PERM_SHIFT) & IOMMU_IO_PERM_MASK;
    if ((fPerm & fDtePerm) == fPerm)
    { /* likely */ }
    else
    {
        LogFunc(("Permission denied by DTE (fPerm=%#x fDtePerm=%#x) -> IOPF\n", fPerm, fDtePerm));
        EVT_IO_PAGE_FAULT_T EvtIoPageFault;
        iommuAmdIoPageFaultEventInit(idDevice, pDte->n.u16DomainId, uIova, true /* fPresent */, false /* fRsvdNotZero */,
                                     true /* fPermDenied */, enmOp, &EvtIoPageFault);
        iommuAmdIoPageFaultEventRaiseWithDte(pDevIns, pDte, NULL /* pIrte */, enmOp, &EvtIoPageFault,
                                             kIoPageFaultType_PermDenied);
        return VERR_IOMMU_ADDR_ACCESS_DENIED;
    }

    /*
     * If the root page table level is 0, translation is disabled and GPA=SPA and
     * the DTE.IR and DTE.IW bits control permissions (verified above).
     */
    uint8_t const uMaxLevel = pDte->n.u3Mode;
    if (uMaxLevel != 0)
    { /* likely */ }
    else
    {
        Assert((fPerm & fDtePerm) == fPerm);   /* Verify we've checked permissions. */
        return VINF_IOMMU_ADDR_TRANSLATION_DISABLED;
    }

    /*
     * If the root page table level exceeds the allowed host-address translation level,
     * page walk is terminated and translation fails.
     */
    if (uMaxLevel <= IOMMU_MAX_HOST_PT_LEVEL)
    { /* likely */ }
    else
    {
        /** @todo r=ramshankar: I cannot make out from the AMD IOMMU spec. if I should be
         *        raising an ILLEGAL_DEV_TABLE_ENTRY event or an IO_PAGE_FAULT event here.
         *        I'm just going with I/O page fault. */
        LogFunc(("Invalid root page table level %#x (idDevice=%#x) -> IOPF\n", uMaxLevel, idDevice));
        EVT_IO_PAGE_FAULT_T EvtIoPageFault;
        iommuAmdIoPageFaultEventInit(idDevice, pDte->n.u16DomainId, uIova, true /* fPresent */, false /* fRsvdNotZero */,
                                     false /* fPermDenied */, enmOp, &EvtIoPageFault);
        iommuAmdIoPageFaultEventRaiseWithDte(pDevIns, pDte, NULL /* pIrte */, enmOp, &EvtIoPageFault,
                                             kIoPageFaultType_PteInvalidLvlEncoding);
        return VERR_IOMMU_ADDR_TRANSLATION_FAILED;
    }

    /* The DTE allows translations for this device. */
    return VINF_SUCCESS;
}


/**
 * Walks the I/O page table to translate the I/O virtual address to a system
 * physical address.
 *
 * @returns VBox status code.
 * @param   pDevIns         The IOMMU device instance.
 * @param   uIova           The I/O virtual address to translate. Must be 4K aligned.
 * @param   fPerm           The I/O permissions for this access, see
 *                          IOMMU_IO_PERM_XXX.
 * @param   idDevice        The device ID (bus, device, function).
 * @param   pDte            The device table entry.
 * @param   enmOp           The IOMMU operation being performed.
 * @param   pPageLookup     Where to store the results of the I/O page lookup. This
 *                          is only updated when VINF_SUCCESS is returned.
 *
 * @thread  Any.
 */
static int iommuAmdIoPageTableWalk(PPDMDEVINS pDevIns, uint64_t uIova, uint8_t fPerm, uint16_t idDevice, PCDTE_T pDte,
                                   IOMMUOP enmOp, PIOPAGELOOKUP pPageLookup)
{
    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    Assert(pDte->n.u1Valid);
    Assert(!(uIova & X86_PAGE_4K_OFFSET_MASK));

    /* The virtual address bits indexing table. */
    static uint8_t const  s_acIovaLevelShifts[] = { 0, 12, 21, 30, 39, 48, 57, 0 };
    AssertCompile(RT_ELEMENTS(s_acIovaLevelShifts) > IOMMU_MAX_HOST_PT_LEVEL);

    /*
     * Traverse the I/O page table starting with the page directory in the DTE.
     *
     * The Valid (Present bit), Translation Valid and Mode (Next-Level bits) in
     * the DTE have been validated already, see iommuAmdPreTranslateChecks.
     */
    IOPTENTITY_T PtEntity;
    PtEntity.u64 = pDte->au64[0];
    for (;;)
    {
        uint8_t const uLevel = PtEntity.n.u3NextLevel;

        /* Read the page table entity at the current level. */
        {
            Assert(uLevel > 0 && uLevel < RT_ELEMENTS(s_acIovaLevelShifts));
            Assert(uLevel <= IOMMU_MAX_HOST_PT_LEVEL);
            uint16_t const idxPte         = (uIova >> s_acIovaLevelShifts[uLevel]) & UINT64_C(0x1ff);
            uint64_t const offPte         = idxPte << 3;
            RTGCPHYS const GCPhysPtEntity = (PtEntity.u64 & IOMMU_PTENTITY_ADDR_MASK) + offPte;
            int rc = PDMDevHlpPCIPhysRead(pDevIns, GCPhysPtEntity, &PtEntity.u64, sizeof(PtEntity));
            if (RT_FAILURE(rc))
            {
                LogFunc(("Failed to read page table entry at %#RGp. rc=%Rrc -> PageTabHwError\n", GCPhysPtEntity, rc));
                EVT_PAGE_TAB_HW_ERR_T EvtPageTabHwErr;
                iommuAmdPageTabHwErrorEventInit(idDevice, pDte->n.u16DomainId, GCPhysPtEntity, enmOp, &EvtPageTabHwErr);
                iommuAmdPageTabHwErrorEventRaise(pDevIns, enmOp, &EvtPageTabHwErr);
                return VERR_IOMMU_IPE_2;
            }
        }

        /* Check present bit. */
        if (PtEntity.n.u1Present)
        { /* likely */ }
        else
        {
            LogFunc(("Page table entry not present. idDevice=%#x uIova=%#RX64 -> IOPF\n", idDevice, uIova));
            EVT_IO_PAGE_FAULT_T EvtIoPageFault;
            iommuAmdIoPageFaultEventInit(idDevice, pDte->n.u16DomainId, uIova, false /* fPresent */, false /* fRsvdNotZero */,
                                         false /* fPermDenied */, enmOp, &EvtIoPageFault);
            iommuAmdIoPageFaultEventRaiseWithDte(pDevIns, pDte, NULL /* pIrte */, enmOp, &EvtIoPageFault,
                                                 kIoPageFaultType_PermDenied);
            return VERR_IOMMU_ADDR_TRANSLATION_FAILED;
        }

        /* Validate the encoding of the next level. */
        uint8_t const uNextLevel = PtEntity.n.u3NextLevel;
#if IOMMU_MAX_HOST_PT_LEVEL < 6
        if (uNextLevel <= IOMMU_MAX_HOST_PT_LEVEL)
        { /* likely */ }
        else
        {
            LogFunc(("Next-level/paging-mode field of the paging entity invalid. uNextLevel=%#x -> IOPF\n", uNextLevel));
            EVT_IO_PAGE_FAULT_T EvtIoPageFault;
            iommuAmdIoPageFaultEventInit(idDevice, pDte->n.u16DomainId, uIova, true /* fPresent */, true /* fRsvdNotZero */,
                                         false /* fPermDenied */, enmOp, &EvtIoPageFault);
            iommuAmdIoPageFaultEventRaiseWithDte(pDevIns, pDte, NULL /* pIrte */, enmOp, &EvtIoPageFault,
                                                 kIoPageFaultType_PteInvalidLvlEncoding);
            return VERR_IOMMU_ADDR_TRANSLATION_FAILED;
        }
#endif

        /* Check reserved bits. */
        uint64_t const fRsvdMask = uNextLevel == 0 || uNextLevel == 7 ? IOMMU_PTE_RSVD_MASK : IOMMU_PDE_RSVD_MASK;
        if (!(PtEntity.u64 & fRsvdMask))
        { /* likely */ }
        else
        {
            LogFunc(("Page table entity (%#RX64 level=%u) reserved bits set -> IOPF\n", PtEntity.u64, uNextLevel));
            EVT_IO_PAGE_FAULT_T EvtIoPageFault;
            iommuAmdIoPageFaultEventInit(idDevice, pDte->n.u16DomainId, uIova, true /* fPresent */, true /* fRsvdNotZero */,
                                         false /* fPermDenied */, enmOp, &EvtIoPageFault);
            iommuAmdIoPageFaultEventRaiseWithDte(pDevIns, pDte, NULL /* pIrte */, enmOp, &EvtIoPageFault,
                                                 kIoPageFaultType_PteRsvdNotZero);
            return VERR_IOMMU_ADDR_TRANSLATION_FAILED;
        }

        /* Check permission bits. */
        uint8_t const fPtePerm = (PtEntity.u64 >> IOMMU_IO_PERM_SHIFT) & IOMMU_IO_PERM_MASK;
        if ((fPerm & fPtePerm) == fPerm)
        { /* likely */ }
        else
        {
            LogFunc(("Page table entry access denied. idDevice=%#x fPerm=%#x fPtePerm=%#x -> IOPF\n", idDevice, fPerm, fPtePerm));
            EVT_IO_PAGE_FAULT_T EvtIoPageFault;
            iommuAmdIoPageFaultEventInit(idDevice, pDte->n.u16DomainId, uIova, true /* fPresent */, false /* fRsvdNotZero */,
                                         true /* fPermDenied */, enmOp, &EvtIoPageFault);
            iommuAmdIoPageFaultEventRaiseWithDte(pDevIns, pDte, NULL /* pIrte */, enmOp, &EvtIoPageFault,
                                                 kIoPageFaultType_PermDenied);
            return VERR_IOMMU_ADDR_ACCESS_DENIED;
        }

        /* If the next level is 0 or 7, this is the final level PTE. */
        if (uNextLevel == 0)
        {
            /* The page size of the translation is the default size for the level. */
            uint8_t const  cShift    = s_acIovaLevelShifts[uLevel];
            RTGCPHYS const GCPhysPte = PtEntity.u64 & IOMMU_PTENTITY_ADDR_MASK;
            pPageLookup->GCPhysSpa = GCPhysPte & X86_GET_PAGE_BASE_MASK(cShift);
            pPageLookup->cShift    = cShift;
            pPageLookup->fPerm     = fPtePerm;
            return VINF_SUCCESS;
        }
        if (uNextLevel == 7)
        {
            /* The default page size of the translation is overridden. */
            uint8_t        cShift    = X86_PAGE_4K_SHIFT;
            RTGCPHYS const GCPhysPte = PtEntity.u64 & IOMMU_PTENTITY_ADDR_MASK;
            while (GCPhysPte & RT_BIT_64(cShift++))
                ;

            /* The page size must be larger than the default size and lower than the default size of the higher level. */
            if (   cShift > s_acIovaLevelShifts[uLevel]
                && cShift < s_acIovaLevelShifts[uLevel + 1])
            {
                pPageLookup->GCPhysSpa = GCPhysPte & X86_GET_PAGE_BASE_MASK(cShift);
                pPageLookup->cShift    = cShift;
                pPageLookup->fPerm     = fPtePerm;
                STAM_COUNTER_INC(&pThis->StatNonStdPageSize); NOREF(pThis);
                return VINF_SUCCESS;
            }

            LogFunc(("Page size invalid. idDevice=%#x cShift=%u -> IOPF\n", idDevice, cShift));
            EVT_IO_PAGE_FAULT_T EvtIoPageFault;
            iommuAmdIoPageFaultEventInit(idDevice, pDte->n.u16DomainId, uIova, true /* fPresent */, false /* fRsvdNotZero */,
                                         false /* fPermDenied */, enmOp, &EvtIoPageFault);
            iommuAmdIoPageFaultEventRaiseWithDte(pDevIns, pDte, NULL /* pIrte */, enmOp, &EvtIoPageFault,
                                                 kIoPageFaultType_PteInvalidPageSize);
            return VERR_IOMMU_ADDR_TRANSLATION_FAILED;
        }

        /* Validate level transition. */
        if (uNextLevel < uLevel)
        { /* likely */ }
        else
        {
            LogFunc(("Next level (%#x) must be less than the current level (%#x) -> IOPF\n", uNextLevel, uLevel));
            EVT_IO_PAGE_FAULT_T EvtIoPageFault;
            iommuAmdIoPageFaultEventInit(idDevice, pDte->n.u16DomainId, uIova, true /* fPresent */, false /* fRsvdNotZero */,
                                         false /* fPermDenied */, enmOp, &EvtIoPageFault);
            iommuAmdIoPageFaultEventRaiseWithDte(pDevIns, pDte, NULL /* pIrte */, enmOp, &EvtIoPageFault,
                                                 kIoPageFaultType_PteInvalidLvlEncoding);
            return VERR_IOMMU_ADDR_TRANSLATION_FAILED;
        }

        /* Ensure IOVA bits of skipped levels (if any) are zero. */
        uint64_t const fIovaSkipMask = IOMMU_GET_MAX_VALID_IOVA(uLevel - 1) - IOMMU_GET_MAX_VALID_IOVA(uNextLevel);
        if (!(uIova & fIovaSkipMask))
        { /* likely */ }
        else
        {
            LogFunc(("IOVA of skipped levels are not zero. uIova=%#RX64 fSkipMask=%#RX64 -> IOPF\n", uIova, fIovaSkipMask));
            EVT_IO_PAGE_FAULT_T EvtIoPageFault;
            iommuAmdIoPageFaultEventInit(idDevice, pDte->n.u16DomainId, uIova, true /* fPresent */, false /* fRsvdNotZero */,
                                         false /* fPermDenied */, enmOp, &EvtIoPageFault);
            iommuAmdIoPageFaultEventRaiseWithDte(pDevIns, pDte, NULL /* pIrte */, enmOp, &EvtIoPageFault,
                                                 kIoPageFaultType_SkippedLevelIovaNotZero);
            return VERR_IOMMU_ADDR_TRANSLATION_FAILED;
        }

        /* Traverse to the next level. */
    }
}


/**
 * Page lookup callback for finding an I/O page from guest memory.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when the page is found and has the right permissions.
 * @retval  VERR_IOMMU_ADDR_TRANSLATION_FAILED when address translation fails.
 * @retval  VERR_IOMMU_ADDR_ACCESS_DENIED when the page is found but permissions are
 *          insufficient to what is requested.
 *
 * @param   pDevIns         The IOMMU instance data.
 * @param   uIovaPage       The I/O virtual address to lookup in the cache (must be
 *                          4K aligned).
 * @param   fPerm           The I/O permissions for this access, see
 *                          IOMMU_IO_PERM_XXX.
 * @param   pAux            The auxiliary information required during lookup.
 * @param   pPageLookup     Where to store the looked up I/O page.
 */
static DECLCALLBACK(int) iommuAmdDteLookupPage(PPDMDEVINS pDevIns, uint64_t uIovaPage, uint8_t fPerm, PCIOMMUOPAUX pAux,
                                               PIOPAGELOOKUP pPageLookup)
{
    AssertPtr(pAux);
    AssertPtr(pPageLookup);
    Assert(!(uIovaPage & X86_PAGE_4K_OFFSET_MASK));

    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    STAM_PROFILE_ADV_START(&pThis->StatProfDteLookup, a);
    int rc = iommuAmdIoPageTableWalk(pDevIns, uIovaPage, fPerm, pAux->idDevice, pAux->pDte, pAux->enmOp, pPageLookup);
    STAM_PROFILE_ADV_STOP(&pThis->StatProfDteLookup, a); NOREF(pThis);
    return rc;
}


/**
 * Looks up a range of I/O virtual addresses.
 *
 * @returns VBox status code.
 * @param   pDevIns             The IOMMU instance data.
 * @param   pfnIoPageLookup     The lookup function to use.
 * @param   pAddrIn             The I/O address range to lookup.
 * @param   pAux                The auxiliary information required by the lookup
 *                              function.
 * @param   pAddrOut            Where to store the translated I/O address page
 *                              lookup.
 * @param   pcbContiguous       Where to store the size of the access.
 */
static int iommuAmdLookupIoAddrRange(PPDMDEVINS pDevIns, PFNIOPAGELOOKUP pfnIoPageLookup, PCIOADDRRANGE pAddrIn,
                                     PCIOMMUOPAUX pAux, PIOPAGELOOKUP pAddrOut, size_t *pcbContiguous)
{
    int            rc;
    size_t const   cbIova      = pAddrIn->cb;
    uint8_t const  fPerm       = pAddrIn->fPerm;
    uint64_t const uIova       = pAddrIn->uAddr;
    RTGCPHYS       GCPhysSpa   = NIL_RTGCPHYS;
    size_t         cbRemaining = cbIova;
    uint64_t       uIovaPage   = pAddrIn->uAddr & X86_PAGE_4K_BASE_MASK;
    uint64_t       offIova     = pAddrIn->uAddr & X86_PAGE_4K_OFFSET_MASK;
    size_t const   cbPage      = X86_PAGE_4K_SIZE;

    IOPAGELOOKUP PageLookupPrev;
    RT_ZERO(PageLookupPrev);
    for (;;)
    {
        /* Lookup the physical page corresponding to the I/O virtual address. */
        IOPAGELOOKUP PageLookup;
        rc = pfnIoPageLookup(pDevIns, uIovaPage, fPerm, pAux, &PageLookup);
        if (RT_SUCCESS(rc))
        {
            /*
             * Validate results of the translation.
             */
            /* The IOTLB cache preserves the original page sizes even though the IOVAs are split into 4K pages. */
            Assert(PageLookup.cShift >= X86_PAGE_4K_SHIFT && PageLookup.cShift <= 51);
            Assert(   pfnIoPageLookup != iommuAmdDteLookupPage
                   || !(PageLookup.GCPhysSpa & X86_GET_PAGE_OFFSET_MASK(PageLookup.cShift)));
            Assert((PageLookup.fPerm & fPerm) == fPerm);

            /* Store the translated address before continuing to access more pages. */
            if (cbRemaining == cbIova)
            {
                uint64_t const offSpa = uIova & X86_GET_PAGE_OFFSET_MASK(PageLookup.cShift);
                GCPhysSpa = PageLookup.GCPhysSpa | offSpa;
            }
            /*
             * Check if translated address results in a physically contiguous region.
             *
             * Also ensure that the permissions for all pages in this range are identical
             * because we specify a common permission while adding pages in this range
             * to the IOTLB cache.
             *
             * The page size must also be identical since we need to know how many offset
             * bits to copy into the final translated address (while retrieving 4K sized
             * pages from the IOTLB cache).
             */
            else if (   PageLookup.GCPhysSpa == PageLookupPrev.GCPhysSpa + cbPage
                     && PageLookup.fPerm     == PageLookupPrev.fPerm
                     && PageLookup.cShift    == PageLookupPrev.cShift)
            { /* likely */ }
            else
            {
                Assert(cbRemaining > 0);
                rc = VERR_OUT_OF_RANGE;
                break;
            }

            /* Store the page lookup result from the first/previous page. */
            PageLookupPrev = PageLookup;

            /* Check if we need to access more pages. */
            if (cbRemaining > cbPage - offIova)
            {
                cbRemaining -= (cbPage - offIova); /* Calculate how much more we need to access. */
                uIovaPage   += cbPage;             /* Update address of the next access. */
                offIova      = 0;                  /* After the first page, remaining pages are accessed from offset 0. */
            }
            else
            {
                /* Caller (PDM) doesn't expect more data accessed than what was requested. */
                cbRemaining = 0;
                break;
            }
        }
        else
            break;
    }

    pAddrOut->GCPhysSpa = GCPhysSpa;                  /* Update the translated address. */
    pAddrOut->cShift    = PageLookupPrev.cShift;      /* Update the page size of the lookup. */
    pAddrOut->fPerm     = PageLookupPrev.fPerm;       /* Update the allowed permissions for this access. */
    *pcbContiguous      = cbIova - cbRemaining;       /* Update the size of the contiguous memory region. */
    return rc;
}


/**
 * Looks up an I/O virtual address from the device table.
 *
 * @returns VBox status code.
 * @param   pDevIns         The IOMMU instance data.
 * @param   idDevice        The device ID (bus, device, function).
 * @param   uIova           The I/O virtual address to lookup.
 * @param   cbIova          The size of the access.
 * @param   fPerm           The I/O permissions for this access, see
 *                          IOMMU_IO_PERM_XXX.
 * @param   enmOp           The IOMMU operation being performed.
 * @param   pGCPhysSpa      Where to store the translated system physical address.
 * @param   pcbContiguous   Where to store the number of contiguous bytes translated
 *                          and permission-checked.
 *
 * @thread  Any.
 */
static int iommuAmdDteLookup(PPDMDEVINS pDevIns, uint16_t idDevice, uint64_t uIova, size_t cbIova, uint8_t fPerm, IOMMUOP enmOp,
                             PRTGCPHYS pGCPhysSpa, size_t *pcbContiguous)
{
    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    RTGCPHYS GCPhysSpa    = NIL_RTGCPHYS;
    size_t   cbContiguous = 0;

    /* Read the device table entry from memory. */
    DTE_T Dte;
    int rc = iommuAmdDteRead(pDevIns, idDevice, enmOp, &Dte);
    if (RT_SUCCESS(rc))
    {
        if (Dte.n.u1Valid)
        {
            /* Validate bits 127:0 of the device table entry when DTE.V is 1. */
            uint64_t const fRsvd0 = Dte.au64[0] & ~(IOMMU_DTE_QWORD_0_VALID_MASK & ~IOMMU_DTE_QWORD_0_FEAT_MASK);
            uint64_t const fRsvd1 = Dte.au64[1] & ~(IOMMU_DTE_QWORD_1_VALID_MASK & ~IOMMU_DTE_QWORD_1_FEAT_MASK);
            if (RT_LIKELY(!fRsvd0 && !fRsvd1))
            {
                /*
                 * Check if the DTE is configured for translating addresses.
                 * Note: Addresses cannot be subject to exclusion as we do -not- support remote IOTLBs,
                 *       so there's no need to check the address exclusion base/limit here.
                 */
                rc = iommuAmdPreTranslateChecks(pDevIns, idDevice, uIova, fPerm, &Dte, enmOp);
                if (rc == VINF_SUCCESS)
                {
                    IOADDRRANGE AddrIn;
                    AddrIn.uAddr = uIova;
                    AddrIn.cb    = cbIova;
                    AddrIn.fPerm = fPerm;

                    IOMMUOPAUX Aux;
                    Aux.enmOp    = enmOp;
                    Aux.pDte     = &Dte;
                    Aux.idDevice = idDevice;
                    Aux.idDomain = Dte.n.u16DomainId;

                    /* Lookup the address from the DTE and I/O page tables.*/
                    IOPAGELOOKUP AddrOut;
                    rc = iommuAmdLookupIoAddrRange(pDevIns, iommuAmdDteLookupPage, &AddrIn, &Aux, &AddrOut, &cbContiguous);
                    GCPhysSpa = AddrOut.GCPhysSpa;

                    /*
                     * If we stopped since translation resulted in non-contiguous physical addresses
                     * or permissions aren't identical for all pages in the access, what we translated
                     * thus far is still valid.
                     */
                    if (rc == VERR_OUT_OF_RANGE)
                    {
                        Assert(cbContiguous > 0 && cbContiguous < cbIova);
                        rc = VINF_SUCCESS;
                        STAM_COUNTER_INC(&pThis->StatAccessDteNonContig); NOREF(pThis);
                    }
                    else if (rc == VERR_IOMMU_ADDR_ACCESS_DENIED)
                        STAM_COUNTER_INC(&pThis->StatAccessDtePermDenied);

#ifdef IOMMU_WITH_IOTLBE_CACHE
                    if (RT_SUCCESS(rc))
                    {
                        /* Update that addresses requires translation (cumulative permissions of DTE and I/O page tables). */
                        iommuAmdDteCacheAddOrUpdateFlags(pDevIns, &Dte, idDevice, IOMMU_DTE_CACHE_F_ADDR_TRANSLATE,
                                                         0 /* fAndMask */);
                        /* Update IOTLB for the contiguous range of I/O virtual addresses. */
                        iommuAmdIotlbAddRange(pDevIns, Aux.idDomain, uIova & X86_PAGE_4K_BASE_MASK, cbContiguous, &AddrOut);
                    }
#endif
                }
                else if (rc == VINF_IOMMU_ADDR_TRANSLATION_DISABLED)
                {
                    /*
                     * Translation is disabled for this device (root paging mode is 0).
                     * GPA=SPA, but the permission bits are important and controls accesses.
                     */
                    GCPhysSpa    = uIova;
                    cbContiguous = cbIova;
                    rc = VINF_SUCCESS;

#ifdef IOMMU_WITH_IOTLBE_CACHE
                    /* Update that addresses permissions of DTE apply (but omit address translation). */
                    iommuAmdDteCacheAddOrUpdateFlags(pDevIns, &Dte, idDevice, IOMMU_DTE_CACHE_F_IO_PERM,
                                                     IOMMU_DTE_CACHE_F_ADDR_TRANSLATE);
#endif
                }
                else
                {
                    /* Address translation failed or access is denied. */
                    Assert(rc == VERR_IOMMU_ADDR_ACCESS_DENIED || rc == VERR_IOMMU_ADDR_TRANSLATION_FAILED);
                    GCPhysSpa    = NIL_RTGCPHYS;
                    cbContiguous = 0;
                    STAM_COUNTER_INC(&pThis->StatAccessDtePermDenied);
                }
            }
            else
            {
                /* Invalid reserved  bits in the DTE, raise an error event. */
                LogFunc(("Invalid DTE reserved bits (u64[0]=%#RX64 u64[1]=%#RX64) -> Illegal DTE\n", fRsvd0, fRsvd1));
                EVT_ILLEGAL_DTE_T Event;
                iommuAmdIllegalDteEventInit(idDevice, uIova, true /* fRsvdNotZero */, enmOp, &Event);
                iommuAmdIllegalDteEventRaise(pDevIns, enmOp, &Event, kIllegalDteType_RsvdNotZero);
                rc = VERR_IOMMU_ADDR_TRANSLATION_FAILED;
            }
        }
        else
        {
            /*
             * The DTE is not valid, forward addresses untranslated.
             * See AMD IOMMU spec. "Table 5: Feature Enablement for Address Translation".
             */
            GCPhysSpa    = uIova;
            cbContiguous = cbIova;
        }
    }
    else
    {
        LogFunc(("Failed to read device table entry. idDevice=%#x rc=%Rrc\n", idDevice, rc));
        rc = VERR_IOMMU_ADDR_TRANSLATION_FAILED;
    }

    *pGCPhysSpa    = GCPhysSpa;
    *pcbContiguous = cbContiguous;
    AssertMsg(rc != VINF_SUCCESS || cbContiguous > 0, ("cbContiguous=%zu\n", cbContiguous));
    return rc;
}


#ifdef IOMMU_WITH_IOTLBE_CACHE
/**
 * I/O page lookup callback for finding an I/O page from the IOTLB.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when the page is found and has the right permissions.
 * @retval  VERR_NOT_FOUND when the page is not found.
 * @retval  VERR_IOMMU_ADDR_ACCESS_DENIED when the page is found but permissions are
 *          insufficient to what is requested.
 *
 * @param   pDevIns         The IOMMU instance data.
 * @param   uIovaPage       The I/O virtual address to lookup in the cache (must be
 *                          4K aligned).
 * @param   fPerm           The I/O permissions for this access, see
 *                          IOMMU_IO_PERM_XXX.
 * @param   pAux            The auxiliary information required during lookup.
 * @param   pPageLookup     Where to store the looked up I/O page.
 */
static DECLCALLBACK(int) iommuAmdCacheLookupPage(PPDMDEVINS pDevIns, uint64_t uIovaPage, uint8_t fPerm, PCIOMMUOPAUX pAux,
                                                 PIOPAGELOOKUP pPageLookup)
{
    Assert(pAux);
    Assert(pPageLookup);
    Assert(!(uIovaPage & X86_PAGE_4K_OFFSET_MASK));

    PIOMMU   pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PIOMMUR3 pThisR3 = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUR3);

    STAM_PROFILE_ADV_START(&pThis->StatProfIotlbeLookup, a);
    PCIOTLBE pIotlbe = iommuAmdIotlbLookup(pThis, pThisR3, pAux->idDomain, uIovaPage);
    STAM_PROFILE_ADV_STOP(&pThis->StatProfIotlbeLookup, a);
    if (pIotlbe)
    {
        *pPageLookup = pIotlbe->PageLookup;
        if ((pPageLookup->fPerm & fPerm) == fPerm)
        {
            STAM_COUNTER_INC(&pThis->StatAccessCacheHit);
            return VINF_SUCCESS;
        }
        return VERR_IOMMU_ADDR_ACCESS_DENIED;
    }
    return VERR_NOT_FOUND;
}


/**
 * Lookups a memory access from the IOTLB cache.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if the access was cached and permissions are verified.
 * @retval  VERR_OUT_OF_RANGE if the access resulted in a non-contiguous physical
 *          address region.
 * @retval  VERR_NOT_FOUND if the access was not cached.
 * @retval  VERR_IOMMU_ADDR_ACCESS_DENIED if the access was cached but permissions
 *          are insufficient.
 *
 * @param   pDevIns         The IOMMU instance data.
 * @param   idDevice        The device ID (bus, device, function).
 * @param   uIova           The I/O virtual address to lookup.
 * @param   cbIova          The size of the access.
 * @param   fPerm           The I/O permissions for this access, see
 *                          IOMMU_IO_PERM_XXX.
 * @param   enmOp           The IOMMU operation being performed.
 * @param   pGCPhysSpa      Where to store the translated system physical address.
 * @param   pcbContiguous   Where to store the number of contiguous bytes translated
 *                          and permission-checked.
 */
static int iommuAmdIotlbCacheLookup(PPDMDEVINS pDevIns, uint16_t idDevice, uint64_t uIova, size_t cbIova, uint8_t fPerm,
                                    IOMMUOP enmOp, PRTGCPHYS pGCPhysSpa, size_t *pcbContiguous)
{
    int rc;
    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);

#define IOMMU_IOTLB_LOOKUP_FAILED(a_rc) \
    do {                                \
        *pGCPhysSpa    = NIL_RTGCPHYS;  \
        *pcbContiguous = 0;             \
        rc = (a_rc);                    \
    } while (0)

    /*
     * We hold the cache lock across both the DTE and the IOTLB lookups (if any) because
     * we don't want the DTE cache to be invalidate while we perform IOTBL lookups.
     */
    IOMMU_CACHE_LOCK(pDevIns, pThis);

    /* Lookup the DTE cache entry. */
    uint16_t const idxDteCache = iommuAmdDteCacheEntryLookup(pThis, idDevice);
    if (idxDteCache < RT_ELEMENTS(pThis->aDteCache))
    {
        PCDTECACHE pDteCache = &pThis->aDteCache[idxDteCache];
        if ((pDteCache->fFlags & (IOMMU_DTE_CACHE_F_PRESENT | IOMMU_DTE_CACHE_F_VALID | IOMMU_DTE_CACHE_F_ADDR_TRANSLATE))
                              == (IOMMU_DTE_CACHE_F_PRESENT | IOMMU_DTE_CACHE_F_VALID | IOMMU_DTE_CACHE_F_ADDR_TRANSLATE))
        {
            /* Lookup IOTLB entries. */
            IOADDRRANGE AddrIn;
            AddrIn.uAddr = uIova;
            AddrIn.cb    = cbIova;
            AddrIn.fPerm = fPerm;

            IOMMUOPAUX Aux;
            Aux.enmOp    = enmOp;
            Aux.pDte     = NULL;
            Aux.idDevice = idDevice;
            Aux.idDomain = pDteCache->idDomain;

            IOPAGELOOKUP AddrOut;
            rc = iommuAmdLookupIoAddrRange(pDevIns, iommuAmdCacheLookupPage, &AddrIn, &Aux, &AddrOut, pcbContiguous);
            *pGCPhysSpa = AddrOut.GCPhysSpa;
            Assert(*pcbContiguous <= cbIova);
        }
        else if ((pDteCache->fFlags & (IOMMU_DTE_CACHE_F_PRESENT | IOMMU_DTE_CACHE_F_VALID | IOMMU_DTE_CACHE_F_IO_PERM))
                                   == (IOMMU_DTE_CACHE_F_PRESENT | IOMMU_DTE_CACHE_F_VALID | IOMMU_DTE_CACHE_F_IO_PERM))
        {
            /* Address translation is disabled, but DTE permissions apply. */
            Assert(!(pDteCache->fFlags & IOMMU_DTE_CACHE_F_ADDR_TRANSLATE));
            uint8_t const fDtePerm = (pDteCache->fFlags >> IOMMU_DTE_CACHE_F_IO_PERM_SHIFT) & IOMMU_DTE_CACHE_F_IO_PERM_MASK;
            if ((fDtePerm & fPerm) == fPerm)
            {
                *pGCPhysSpa    = uIova;
                *pcbContiguous = cbIova;
                rc = VINF_SUCCESS;
            }
            else
                IOMMU_IOTLB_LOOKUP_FAILED(VERR_IOMMU_ADDR_ACCESS_DENIED);
        }
        else if (pDteCache->fFlags & IOMMU_DTE_CACHE_F_PRESENT)
        {
            /* Forward addresses untranslated, without checking permissions. */
            *pGCPhysSpa    = uIova;
            *pcbContiguous = cbIova;
            rc = VINF_SUCCESS;
        }
        else
            IOMMU_IOTLB_LOOKUP_FAILED(VERR_NOT_FOUND);
    }
    else
        IOMMU_IOTLB_LOOKUP_FAILED(VERR_NOT_FOUND);

    IOMMU_CACHE_UNLOCK(pDevIns, pThis);

    return rc;

#undef IOMMU_IOTLB_LOOKUP_FAILED
}
#endif /* IOMMU_WITH_IOTLBE_CACHE */


/**
 * Gets the I/O permission and IOMMU operation type for the given access flags.
 *
 * @param   pThis       The shared IOMMU device state.
 * @param   fFlags      The PDM IOMMU flags, PDMIOMMU_MEM_F_XXX.
 * @param   penmOp      Where to store the IOMMU operation.
 * @param   pfPerm      Where to store the IOMMU I/O permission.
 * @param   fBulk       Whether this is a bulk read or write.
 */
DECLINLINE(void) iommuAmdMemAccessGetPermAndOp(PIOMMU pThis, uint32_t fFlags, PIOMMUOP penmOp, uint8_t *pfPerm, bool fBulk)
{
    if (fFlags & PDMIOMMU_MEM_F_WRITE)
    {
        *penmOp = IOMMUOP_MEM_WRITE;
        *pfPerm = IOMMU_IO_PERM_WRITE;
#ifdef VBOX_WITH_STATISTICS
        if (!fBulk)
            STAM_COUNTER_INC(&pThis->CTX_SUFF_Z(StatMemWrite));
        else
            STAM_COUNTER_INC(&pThis->CTX_SUFF_Z(StatMemBulkWrite));
#else
        RT_NOREF2(pThis, fBulk);
#endif
    }
    else
    {
        Assert(fFlags & PDMIOMMU_MEM_F_READ);
        *penmOp = IOMMUOP_MEM_READ;
        *pfPerm = IOMMU_IO_PERM_READ;
#ifdef VBOX_WITH_STATISTICS
        if (!fBulk)
            STAM_COUNTER_INC(&pThis->CTX_SUFF_Z(StatMemRead));
        else
            STAM_COUNTER_INC(&pThis->CTX_SUFF_Z(StatMemBulkRead));
#else
        RT_NOREF2(pThis, fBulk);
#endif
    }
}


/**
 * Memory access transaction from a device.
 *
 * @returns VBox status code.
 * @param   pDevIns         The IOMMU device instance.
 * @param   idDevice        The device ID (bus, device, function).
 * @param   uIova           The I/O virtual address being accessed.
 * @param   cbIova          The size of the access.
 * @param   fFlags          The access flags, see PDMIOMMU_MEM_F_XXX.
 * @param   pGCPhysSpa      Where to store the translated system physical address.
 * @param   pcbContiguous   Where to store the number of contiguous bytes translated
 *                          and permission-checked.
 *
 * @thread  Any.
 */
static DECLCALLBACK(int) iommuAmdMemAccess(PPDMDEVINS pDevIns, uint16_t idDevice, uint64_t uIova, size_t cbIova,
                                           uint32_t fFlags, PRTGCPHYS pGCPhysSpa, size_t *pcbContiguous)
{
    /* Validate. */
    AssertPtr(pDevIns);
    AssertPtr(pGCPhysSpa);
    Assert(cbIova > 0);
    Assert(!(fFlags & ~PDMIOMMU_MEM_F_VALID_MASK));

    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    IOMMU_CTRL_T const Ctrl = iommuAmdGetCtrlUnlocked(pThis);
    if (Ctrl.n.u1IommuEn)
    {
        IOMMUOP enmOp;
        uint8_t fPerm;
        iommuAmdMemAccessGetPermAndOp(pThis, fFlags, &enmOp, &fPerm, false /* fBulk */);
        LogFlowFunc(("%s: idDevice=%#x uIova=%#RX64 cb=%zu\n", iommuAmdMemAccessGetPermName(fPerm), idDevice, uIova, cbIova));

        int rc;
#ifdef IOMMU_WITH_IOTLBE_CACHE
        /* Lookup the IOVA from the cache. */
        rc = iommuAmdIotlbCacheLookup(pDevIns, idDevice, uIova, cbIova, fPerm, enmOp, pGCPhysSpa, pcbContiguous);
        if (rc == VINF_SUCCESS)
        {
            /* All pages in the access were found in the cache with sufficient permissions. */
            Assert(*pcbContiguous == cbIova);
            Assert(*pGCPhysSpa != NIL_RTGCPHYS);
            STAM_COUNTER_INC(&pThis->StatAccessCacheHitFull);
            return VINF_SUCCESS;
        }
        if (rc != VERR_OUT_OF_RANGE)
        { /* likely */ }
        else
        {
            /* Access stopped since translations resulted in non-contiguous memory, let caller resume access. */
            Assert(*pcbContiguous > 0 && *pcbContiguous < cbIova);
            STAM_COUNTER_INC(&pThis->StatAccessCacheNonContig);
            return VINF_SUCCESS;
        }

        /*
         * Access incomplete as not all pages were in the cache.
         * Or permissions were denied for the access (which typically doesn't happen)
         * so go through the slower path and raise the required event.
         */
        AssertMsg(*pcbContiguous < cbIova, ("Invalid size: cbContiguous=%zu cbIova=%zu\n", *pcbContiguous, cbIova));
        uIova  += *pcbContiguous;
        cbIova -= *pcbContiguous;
        /* We currently are including any permission denied pages as cache misses too.*/
        STAM_COUNTER_INC(&pThis->StatAccessCacheMiss);
#endif

        /* Lookup the IOVA from the device table. */
        rc = iommuAmdDteLookup(pDevIns, idDevice, uIova, cbIova, fPerm, enmOp, pGCPhysSpa, pcbContiguous);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
        {
            Assert(rc != VERR_OUT_OF_RANGE);
            LogFunc(("DTE lookup failed! idDevice=%#x uIova=%#RX64 fPerm=%u cbIova=%zu rc=%#Rrc\n", idDevice, uIova, fPerm,
                     cbIova, rc));
        }

        return rc;
    }

    /* Addresses are forwarded without translation when the IOMMU is disabled. */
    *pGCPhysSpa    = uIova;
    *pcbContiguous = cbIova;
    return VINF_SUCCESS;
}


/**
 * Memory access bulk (one or more 4K pages) request from a device.
 *
 * @returns VBox status code.
 * @param   pDevIns         The IOMMU device instance.
 * @param   idDevice        The device ID (bus, device, function).
 * @param   cIovas          The number of addresses being accessed.
 * @param   pauIovas        The I/O virtual addresses for each page being accessed.
 * @param   fFlags          The access flags, see PDMIOMMU_MEM_F_XXX.
 * @param   paGCPhysSpa     Where to store the translated physical addresses.
 *
 * @thread  Any.
 */
static DECLCALLBACK(int) iommuAmdMemBulkAccess(PPDMDEVINS pDevIns, uint16_t idDevice, size_t cIovas, uint64_t const *pauIovas,
                                               uint32_t fFlags, PRTGCPHYS paGCPhysSpa)
{
    /* Validate. */
    AssertPtr(pDevIns);
    Assert(cIovas > 0);
    AssertPtr(pauIovas);
    AssertPtr(paGCPhysSpa);
    Assert(!(fFlags & ~PDMIOMMU_MEM_F_VALID_MASK));

    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    IOMMU_CTRL_T const Ctrl = iommuAmdGetCtrlUnlocked(pThis);
    if (Ctrl.n.u1IommuEn)
    {
        IOMMUOP enmOp;
        uint8_t fPerm;
        iommuAmdMemAccessGetPermAndOp(pThis, fFlags, &enmOp, &fPerm, true /* fBulk */);
        LogFlowFunc(("%s: idDevice=%#x cIovas=%zu\n", iommuAmdMemAccessGetPermName(fPerm), idDevice, cIovas));

        for (size_t i = 0; i < cIovas; i++)
        {
            int    rc;
            size_t cbContig;

#ifdef IOMMU_WITH_IOTLBE_CACHE
            /* Lookup the IOVA from the IOTLB cache. */
            rc = iommuAmdIotlbCacheLookup(pDevIns, idDevice, pauIovas[i], X86_PAGE_SIZE, fPerm, enmOp, &paGCPhysSpa[i],
                                          &cbContig);
            if (rc == VINF_SUCCESS)
            {
                Assert(cbContig == X86_PAGE_SIZE);
                Assert(paGCPhysSpa[i] != NIL_RTGCPHYS);
                STAM_COUNTER_INC(&pThis->StatAccessCacheHitFull);
                continue;
            }
            Assert(rc == VERR_NOT_FOUND || rc == VERR_IOMMU_ADDR_ACCESS_DENIED);
            STAM_COUNTER_INC(&pThis->StatAccessCacheMiss);
#endif

            /* Lookup the IOVA from the device table. */
            rc = iommuAmdDteLookup(pDevIns, idDevice, pauIovas[i], X86_PAGE_SIZE, fPerm, enmOp, &paGCPhysSpa[i], &cbContig);
            if (RT_SUCCESS(rc))
            { /* likely */ }
            else
            {
                LogFunc(("Failed! idDevice=%#x uIova=%#RX64 fPerm=%u rc=%Rrc\n", idDevice, pauIovas[i], fPerm, rc));
                return rc;
            }
            Assert(cbContig == X86_PAGE_SIZE);
        }
    }
    else
    {
        /* Addresses are forwarded without translation when the IOMMU is disabled. */
        for (size_t i = 0; i < cIovas; i++)
            paGCPhysSpa[i] = pauIovas[i];
    }

    return VINF_SUCCESS;
}


/**
 * Reads an interrupt remapping table entry from guest memory given its DTE.
 *
 * @returns VBox status code.
 * @param   pDevIns     The IOMMU device instance.
 * @param   idDevice    The device ID (bus, device, function).
 * @param   pDte        The device table entry.
 * @param   GCPhysIn    The source MSI address (used for reporting errors).
 * @param   uDataIn     The source MSI data.
 * @param   enmOp       The IOMMU operation being performed.
 * @param   pIrte       Where to store the interrupt remapping table entry.
 *
 * @thread  Any.
 */
static int iommuAmdIrteRead(PPDMDEVINS pDevIns, uint16_t idDevice, PCDTE_T pDte, RTGCPHYS GCPhysIn, uint32_t uDataIn,
                            IOMMUOP enmOp, PIRTE_T pIrte)
{
    /* Ensure the IRTE length is valid. */
    Assert(pDte->n.u4IntrTableLength < IOMMU_DTE_INTR_TAB_LEN_MAX);

    RTGCPHYS const GCPhysIntrTable = pDte->au64[2] & IOMMU_DTE_IRTE_ROOT_PTR_MASK;
    uint16_t const cbIntrTable     = IOMMU_DTE_GET_INTR_TAB_LEN(pDte);
    uint16_t const offIrte         = IOMMU_GET_IRTE_OFF(uDataIn);
    RTGCPHYS const GCPhysIrte      = GCPhysIntrTable + offIrte;

    /* Ensure the IRTE falls completely within the interrupt table. */
    if (offIrte + sizeof(IRTE_T) <= cbIntrTable)
    { /* likely */ }
    else
    {
        LogFunc(("IRTE exceeds table length (GCPhysIntrTable=%#RGp cbIntrTable=%u offIrte=%#x uDataIn=%#x) -> IOPF\n",
                 GCPhysIntrTable, cbIntrTable, offIrte, uDataIn));

        EVT_IO_PAGE_FAULT_T EvtIoPageFault;
        iommuAmdIoPageFaultEventInit(idDevice, pDte->n.u16DomainId, GCPhysIn, false /* fPresent */, false /* fRsvdNotZero */,
                                     false /* fPermDenied */, enmOp, &EvtIoPageFault);
        iommuAmdIoPageFaultEventRaiseWithDte(pDevIns, pDte, NULL /* pIrte */, enmOp, &EvtIoPageFault,
                                             kIoPageFaultType_IrteAddrInvalid);
        return VERR_IOMMU_ADDR_TRANSLATION_FAILED;
    }

    /* Read the IRTE from memory. */
    Assert(!(GCPhysIrte & 3));
    int rc = PDMDevHlpPCIPhysRead(pDevIns, GCPhysIrte, pIrte, sizeof(*pIrte));
    if (RT_SUCCESS(rc))
        return VINF_SUCCESS;

    /** @todo The IOMMU spec. does not tell what kind of error is reported in this
     *        situation. Is it an I/O page fault or a device table hardware error?
     *        There's no interrupt table hardware error event, but it's unclear what
     *        we should do here. */
    LogFunc(("Failed to read interrupt table entry at %#RGp. rc=%Rrc -> ???\n", GCPhysIrte, rc));
    return VERR_IOMMU_IPE_4;
}


/**
 * Remaps the interrupt using the interrupt remapping table.
 *
 * @returns VBox status code.
 * @param   pDevIns     The IOMMU instance data.
 * @param   idDevice    The device ID (bus, device, function).
 * @param   pDte        The device table entry.
 * @param   enmOp       The IOMMU operation being performed.
 * @param   pMsiIn      The source MSI.
 * @param   pMsiOut     Where to store the remapped MSI.
 *
 * @thread  Any.
 */
static int iommuAmdIntrRemap(PPDMDEVINS pDevIns, uint16_t idDevice, PCDTE_T pDte, IOMMUOP enmOp, PCMSIMSG pMsiIn,
                             PMSIMSG pMsiOut)
{
    Assert(pDte->n.u2IntrCtrl == IOMMU_INTR_CTRL_REMAP);

    IRTE_T Irte;
    uint32_t const uMsiInData = pMsiIn->Data.u32;
    int rc = iommuAmdIrteRead(pDevIns, idDevice, pDte, pMsiIn->Addr.u64, uMsiInData, enmOp, &Irte);
    if (RT_SUCCESS(rc))
    {
        if (Irte.n.u1RemapEnable)
        {
            if (!Irte.n.u1GuestMode)
            {
                if (Irte.n.u3IntrType <= VBOX_MSI_DELIVERY_MODE_LOWEST_PRIO)
                {
                    iommuAmdIrteRemapMsi(pMsiIn, pMsiOut, &Irte);
#ifdef IOMMU_WITH_IRTE_CACHE
                    iommuAmdIrteCacheAdd(pDevIns, idDevice, IOMMU_GET_IRTE_OFF(uMsiInData), &Irte);
#endif
                    return VINF_SUCCESS;
                }

                LogFunc(("Interrupt type (%#x) invalid -> IOPF\n", Irte.n.u3IntrType));
                EVT_IO_PAGE_FAULT_T EvtIoPageFault;
                iommuAmdIoPageFaultEventInit(idDevice, pDte->n.u16DomainId, pMsiIn->Addr.u64, Irte.n.u1RemapEnable,
                                             true /* fRsvdNotZero */, false /* fPermDenied */, enmOp, &EvtIoPageFault);
                iommuAmdIoPageFaultEventRaiseWithDte(pDevIns, pDte, &Irte, enmOp, &EvtIoPageFault,
                                                     kIoPageFaultType_IrteRsvdIntType);
                return VERR_IOMMU_ADDR_TRANSLATION_FAILED;
            }

            LogFunc(("Guest mode not supported -> IOPF\n"));
            EVT_IO_PAGE_FAULT_T EvtIoPageFault;
            iommuAmdIoPageFaultEventInit(idDevice, pDte->n.u16DomainId, pMsiIn->Addr.u64, Irte.n.u1RemapEnable,
                                         true /* fRsvdNotZero */, false /* fPermDenied */, enmOp, &EvtIoPageFault);
            iommuAmdIoPageFaultEventRaiseWithDte(pDevIns, pDte, &Irte, enmOp, &EvtIoPageFault, kIoPageFaultType_IrteRsvdNotZero);
            return VERR_IOMMU_ADDR_TRANSLATION_FAILED;
        }

        LogFunc(("Remapping disabled -> IOPF\n"));
        EVT_IO_PAGE_FAULT_T EvtIoPageFault;
        iommuAmdIoPageFaultEventInit(idDevice, pDte->n.u16DomainId, pMsiIn->Addr.u64, Irte.n.u1RemapEnable,
                                     false /* fRsvdNotZero */, false /* fPermDenied */, enmOp, &EvtIoPageFault);
        iommuAmdIoPageFaultEventRaiseWithDte(pDevIns, pDte, &Irte, enmOp, &EvtIoPageFault, kIoPageFaultType_IrteRemapEn);
        return VERR_IOMMU_ADDR_TRANSLATION_FAILED;
    }

    return rc;
}


/**
 * Looks up an MSI interrupt from the interrupt remapping table.
 *
 * @returns VBox status code.
 * @param   pDevIns     The IOMMU instance data.
 * @param   idDevice    The device ID (bus, device, function).
 * @param   enmOp       The IOMMU operation being performed.
 * @param   pMsiIn      The source MSI.
 * @param   pMsiOut     Where to store the remapped MSI.
 *
 * @thread  Any.
 */
static int iommuAmdIntrTableLookup(PPDMDEVINS pDevIns, uint16_t idDevice, IOMMUOP enmOp, PCMSIMSG pMsiIn, PMSIMSG pMsiOut)
{
    LogFlowFunc(("idDevice=%#x (%#x:%#x:%#x) enmOp=%u\n", idDevice, ((idDevice >> VBOX_PCI_BUS_SHIFT) & VBOX_PCI_BUS_MASK),
                 ((idDevice >> VBOX_PCI_DEVFN_DEV_SHIFT) & VBOX_PCI_DEVFN_DEV_MASK), (idDevice & VBOX_PCI_DEVFN_FUN_MASK),
                 enmOp));

    /* Read the device table entry from memory. */
    DTE_T Dte;
    int rc = iommuAmdDteRead(pDevIns, idDevice, enmOp, &Dte);
    if (RT_SUCCESS(rc))
    {
#ifdef IOMMU_WITH_IRTE_CACHE
        iommuAmdDteCacheAdd(pDevIns, idDevice, &Dte);
#endif
        /* If the DTE is not valid, all interrupts are forwarded without remapping. */
        if (Dte.n.u1IntrMapValid)
        {
            /* Validate bits 255:128 of the device table entry when DTE.IV is 1. */
            uint64_t const fRsvd0 = Dte.au64[2] & ~IOMMU_DTE_QWORD_2_VALID_MASK;
            uint64_t const fRsvd1 = Dte.au64[3] & ~IOMMU_DTE_QWORD_3_VALID_MASK;
            if (RT_LIKELY(!fRsvd0 && !fRsvd1))
            { /* likely */ }
            else
            {
                LogFunc(("Invalid reserved bits in DTE (u64[2]=%#RX64 u64[3]=%#RX64) -> Illegal DTE\n", fRsvd0, fRsvd1));
                EVT_ILLEGAL_DTE_T Event;
                iommuAmdIllegalDteEventInit(idDevice, pMsiIn->Addr.u64, true /* fRsvdNotZero */, enmOp, &Event);
                iommuAmdIllegalDteEventRaise(pDevIns, enmOp, &Event, kIllegalDteType_RsvdNotZero);
                return VERR_IOMMU_INTR_REMAP_FAILED;
            }

            /*
             * LINT0/LINT1 pins cannot be driven by PCI(e) devices. Perhaps for a Southbridge
             * that's connected through HyperTransport it might be possible; but for us, it
             * doesn't seem we need to specially handle these pins.
             */

            /*
             * Validate the MSI source address.
             *
             * 64-bit MSIs are supported by the PCI and AMD IOMMU spec. However as far as the
             * CPU is concerned, the MSI region is fixed and we must ensure no other device
             * claims the region as I/O space.
             *
             * See PCI spec. 6.1.4. "Message Signaled Interrupt (MSI) Support".
             * See AMD IOMMU spec. 2.8 "IOMMU Interrupt Support".
             * See Intel spec. 10.11.1 "Message Address Register Format".
             */
            if ((pMsiIn->Addr.u64 & VBOX_MSI_ADDR_ADDR_MASK) == VBOX_MSI_ADDR_BASE)
            {
                /*
                 * The IOMMU remaps fixed and arbitrated interrupts using the IRTE.
                 * See AMD IOMMU spec. "2.2.5.1 Interrupt Remapping Tables, Guest Virtual APIC Not Enabled".
                 */
                uint8_t const u8DeliveryMode = pMsiIn->Data.n.u3DeliveryMode;
                bool fPassThru = false;
                switch (u8DeliveryMode)
                {
                    case VBOX_MSI_DELIVERY_MODE_FIXED:
                    case VBOX_MSI_DELIVERY_MODE_LOWEST_PRIO:
                    {
                        uint8_t const uIntrCtrl = Dte.n.u2IntrCtrl;
                        if (uIntrCtrl == IOMMU_INTR_CTRL_REMAP)
                        {
                            /* Validate the encoded interrupt table length when IntCtl specifies remapping. */
                            uint8_t const uIntrTabLen = Dte.n.u4IntrTableLength;
                            if (uIntrTabLen < IOMMU_DTE_INTR_TAB_LEN_MAX)
                            {
                                /*
                                 * We don't support guest interrupt remapping yet. When we do, we'll need to
                                 * check Ctrl.u1GstVirtApicEn and use the guest Virtual APIC Table Root Pointer
                                 * in the DTE rather than the Interrupt Root Table Pointer. Since the caller
                                 * already reads the control register, add that as a parameter when we eventually
                                 * support guest interrupt remapping. For now, just assert.
                                 */
                                PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
                                Assert(!pThis->ExtFeat.n.u1GstVirtApicSup);
                                NOREF(pThis);

                                return iommuAmdIntrRemap(pDevIns, idDevice, &Dte, enmOp, pMsiIn, pMsiOut);
                            }

                            LogFunc(("Invalid interrupt table length %#x -> Illegal DTE\n", uIntrTabLen));
                            EVT_ILLEGAL_DTE_T Event;
                            iommuAmdIllegalDteEventInit(idDevice, pMsiIn->Addr.u64, false /* fRsvdNotZero */, enmOp, &Event);
                            iommuAmdIllegalDteEventRaise(pDevIns, enmOp, &Event, kIllegalDteType_RsvdIntTabLen);
                            return VERR_IOMMU_INTR_REMAP_FAILED;
                        }

                        if (uIntrCtrl == IOMMU_INTR_CTRL_FWD_UNMAPPED)
                        {
                            fPassThru = true;
                            break;
                        }

                        if (uIntrCtrl == IOMMU_INTR_CTRL_TARGET_ABORT)
                        {
                            LogRelMax(10, ("%s: Remapping disallowed for fixed/arbitrated interrupt %#x -> Target abort\n",
                                           IOMMU_LOG_PFX, pMsiIn->Data.n.u8Vector));
                            iommuAmdSetPciTargetAbort(pDevIns);
                            return VERR_IOMMU_INTR_REMAP_DENIED;
                        }

                        Assert(uIntrCtrl == IOMMU_INTR_CTRL_RSVD); /* Paranoia. */
                        LogRelMax(10, ("%s: IntCtl mode invalid %#x -> Illegal DTE\n", IOMMU_LOG_PFX, uIntrCtrl));
                        EVT_ILLEGAL_DTE_T Event;
                        iommuAmdIllegalDteEventInit(idDevice, pMsiIn->Addr.u64, true /* fRsvdNotZero */, enmOp, &Event);
                        iommuAmdIllegalDteEventRaise(pDevIns, enmOp, &Event, kIllegalDteType_RsvdIntCtl);
                        return VERR_IOMMU_INTR_REMAP_FAILED;
                    }

                    /* SMIs are passed through unmapped. We don't implement SMI filters. */
                    case VBOX_MSI_DELIVERY_MODE_SMI:        fPassThru = true;                   break;
                    case VBOX_MSI_DELIVERY_MODE_NMI:        fPassThru = Dte.n.u1NmiPassthru;    break;
                    case VBOX_MSI_DELIVERY_MODE_INIT:       fPassThru = Dte.n.u1InitPassthru;   break;
                    case VBOX_MSI_DELIVERY_MODE_EXT_INT:    fPassThru = Dte.n.u1ExtIntPassthru; break;
                    default:
                    {
                        LogRelMax(10, ("%s: MSI data delivery mode invalid %#x -> Target abort\n", IOMMU_LOG_PFX,
                                       u8DeliveryMode));
                        iommuAmdSetPciTargetAbort(pDevIns);
                        return VERR_IOMMU_INTR_REMAP_FAILED;
                    }
                }

                /*
                 * For those other than fixed and arbitrated interrupts, destination mode must be 0 (physical).
                 * See AMD IOMMU spec. The note below Table 19: "IOMMU Controls and Actions for Upstream Interrupts".
                 */
                if (   u8DeliveryMode <= VBOX_MSI_DELIVERY_MODE_LOWEST_PRIO
                    || !pMsiIn->Addr.n.u1DestMode)
                {
                    if (fPassThru)
                    {
                        *pMsiOut = *pMsiIn;
                        return VINF_SUCCESS;
                    }
                    LogRelMax(10, ("%s: Remapping/passthru disallowed for interrupt %#x -> Target abort\n", IOMMU_LOG_PFX,
                                   pMsiIn->Data.n.u8Vector));
                }
                else
                    LogRelMax(10, ("%s: Logical destination mode invalid for delivery mode %#x\n -> Target abort\n",
                                   IOMMU_LOG_PFX, u8DeliveryMode));

                iommuAmdSetPciTargetAbort(pDevIns);
                return VERR_IOMMU_INTR_REMAP_DENIED;
            }
            else
            {
                /** @todo should be cause a PCI target abort here? */
                LogRelMax(10, ("%s: MSI address region invalid %#RX64\n", IOMMU_LOG_PFX, pMsiIn->Addr.u64));
                return VERR_IOMMU_INTR_REMAP_FAILED;
            }
        }
        else
        {
            LogFlowFunc(("DTE interrupt map not valid\n"));
            *pMsiOut = *pMsiIn;
            return VINF_SUCCESS;
        }
    }

    LogFunc(("Failed to read device table entry. idDevice=%#x rc=%Rrc\n", idDevice, rc));
    return VERR_IOMMU_INTR_REMAP_FAILED;
}


/**
 * Interrupt remap request from a device.
 *
 * @returns VBox status code.
 * @param   pDevIns     The IOMMU device instance.
 * @param   idDevice    The device ID (bus, device, function).
 * @param   pMsiIn      The source MSI.
 * @param   pMsiOut     Where to store the remapped MSI.
 */
static DECLCALLBACK(int) iommuAmdMsiRemap(PPDMDEVINS pDevIns, uint16_t idDevice, PCMSIMSG pMsiIn, PMSIMSG pMsiOut)
{
    /* Validate. */
    Assert(pDevIns);
    Assert(pMsiIn);
    Assert(pMsiOut);

    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);

    /* If this MSI was generated by the IOMMU itself, it's not subject to remapping, see @bugref{9654#c104}. */
    if (idDevice == pThis->uPciAddress)
        return VERR_IOMMU_CANNOT_CALL_SELF;

    /* Interrupts are forwarded with remapping when the IOMMU is disabled. */
    IOMMU_CTRL_T const Ctrl = iommuAmdGetCtrlUnlocked(pThis);
    if (Ctrl.n.u1IommuEn)
    {
        STAM_COUNTER_INC(&pThis->CTX_SUFF_Z(StatMsiRemap));

        int rc;
#ifdef IOMMU_WITH_IRTE_CACHE
        STAM_PROFILE_ADV_START(&pThis->StatProfIrteCacheLookup, a);
        rc = iommuAmdIrteCacheLookup(pDevIns, idDevice, IOMMUOP_INTR_REQ, pMsiIn, pMsiOut);
        STAM_PROFILE_ADV_STOP(&pThis->StatProfIrteCacheLookup, a);
        if (RT_SUCCESS(rc))
        {
            STAM_COUNTER_INC(&pThis->StatIntrCacheHit);
            return VINF_SUCCESS;
        }
        STAM_COUNTER_INC(&pThis->StatIntrCacheMiss);
#endif

        STAM_PROFILE_ADV_START(&pThis->StatProfIrteLookup, a);
        rc = iommuAmdIntrTableLookup(pDevIns, idDevice, IOMMUOP_INTR_REQ, pMsiIn, pMsiOut);
        STAM_PROFILE_ADV_STOP(&pThis->StatProfIrteLookup, a);
        return rc;
    }

    *pMsiOut = *pMsiIn;
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) iommuAmdMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    NOREF(pvUser);
    Assert(cb == 4 || cb == 8);
    Assert(!(off & (cb - 1)));

    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    STAM_COUNTER_INC(&pThis->CTX_SUFF_Z(StatMmioWrite)); NOREF(pThis);

    uint64_t const uValue = cb == 8 ? *(uint64_t const *)pv : *(uint32_t const *)pv;
    return iommuAmdRegisterWrite(pDevIns, off, cb, uValue);
}


/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) iommuAmdMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    NOREF(pvUser);
    Assert(cb == 4 || cb == 8);
    Assert(!(off & (cb - 1)));

    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    STAM_COUNTER_INC(&pThis->CTX_SUFF_Z(StatMmioRead)); NOREF(pThis);

    uint64_t uResult;
    VBOXSTRICTRC rcStrict = iommuAmdRegisterRead(pDevIns, off, &uResult);
    if (rcStrict == VINF_SUCCESS)
    {
        if (cb == 8)
            *(uint64_t *)pv = uResult;
        else
            *(uint32_t *)pv = (uint32_t)uResult;
    }

    return rcStrict;
}


#ifdef IN_RING3
/**
 * Processes an IOMMU command.
 *
 * @returns VBox status code.
 * @param   pDevIns         The IOMMU device instance.
 * @param   pCmd            The command to process.
 * @param   GCPhysCmd       The system physical address of the command.
 * @param   pEvtError       Where to store the error event in case of failures.
 *
 * @thread  Command thread.
 */
static int iommuAmdR3CmdProcess(PPDMDEVINS pDevIns, PCCMD_GENERIC_T pCmd, RTGCPHYS GCPhysCmd, PEVT_GENERIC_T pEvtError)
{
    PIOMMU   pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PIOMMUR3 pThisR3 = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUR3);

    STAM_COUNTER_INC(&pThis->StatCmd);

    uint8_t const bCmd = pCmd->n.u4Opcode;
    switch (bCmd)
    {
        case IOMMU_CMD_COMPLETION_WAIT:
        {
            STAM_COUNTER_INC(&pThis->StatCmdCompWait);

            PCCMD_COMWAIT_T pCmdComWait = (PCCMD_COMWAIT_T)pCmd;
            AssertCompile(sizeof(*pCmdComWait) == sizeof(*pCmd));

            /* Validate reserved bits in the command. */
            if (!(pCmdComWait->au64[0] & ~IOMMU_CMD_COM_WAIT_QWORD_0_VALID_MASK))
            {
                /* If Completion Store is requested, write the StoreData to the specified address. */
                if (pCmdComWait->n.u1Store)
                {
                    RTGCPHYS const GCPhysStore = RT_MAKE_U64(pCmdComWait->n.u29StoreAddrLo << 3, pCmdComWait->n.u20StoreAddrHi);
                    uint64_t const u64Data     = pCmdComWait->n.u64StoreData;
                    int rc = PDMDevHlpPCIPhysWrite(pDevIns, GCPhysStore, &u64Data, sizeof(u64Data));
                    if (RT_FAILURE(rc))
                    {
                        LogFunc(("Cmd(%#x): Failed to write StoreData (%#RX64) to %#RGp, rc=%Rrc\n", bCmd, u64Data,
                             GCPhysStore, rc));
                        iommuAmdCmdHwErrorEventInit(GCPhysStore, (PEVT_CMD_HW_ERR_T)pEvtError);
                        return VERR_IOMMU_CMD_HW_ERROR;
                    }
                }

                /* If the command requests an interrupt and completion wait interrupts are enabled, raise it. */
                if (pCmdComWait->n.u1Interrupt)
                {
                    IOMMU_LOCK(pDevIns, pThisR3);
                    ASMAtomicOrU64(&pThis->Status.u64, IOMMU_STATUS_COMPLETION_WAIT_INTR);
                    bool const fRaiseInt = pThis->Ctrl.n.u1CompWaitIntrEn;
                    IOMMU_UNLOCK(pDevIns, pThisR3);
                    if (fRaiseInt)
                        iommuAmdMsiInterruptRaise(pDevIns);
                }
                return VINF_SUCCESS;
            }
            iommuAmdIllegalCmdEventInit(GCPhysCmd, (PEVT_ILLEGAL_CMD_ERR_T)pEvtError);
            return VERR_IOMMU_CMD_INVALID_FORMAT;
        }

        case IOMMU_CMD_INV_DEV_TAB_ENTRY:
        {
            STAM_COUNTER_INC(&pThis->StatCmdInvDte);
            PCCMD_INV_DTE_T pCmdInvDte = (PCCMD_INV_DTE_T)pCmd;
            AssertCompile(sizeof(*pCmdInvDte) == sizeof(*pCmd));

            /* Validate reserved bits in the command. */
            if (   !(pCmdInvDte->au64[0] & ~IOMMU_CMD_INV_DTE_QWORD_0_VALID_MASK)
                && !(pCmdInvDte->au64[1] & ~IOMMU_CMD_INV_DTE_QWORD_1_VALID_MASK))
            {
#ifdef IOMMU_WITH_DTE_CACHE
                iommuAmdDteCacheRemove(pDevIns, pCmdInvDte->n.u16DevId);
#endif
                return VINF_SUCCESS;
            }
            iommuAmdIllegalCmdEventInit(GCPhysCmd, (PEVT_ILLEGAL_CMD_ERR_T)pEvtError);
            return VERR_IOMMU_CMD_INVALID_FORMAT;
        }

        case IOMMU_CMD_INV_IOMMU_PAGES:
        {
            STAM_COUNTER_INC(&pThis->StatCmdInvIommuPages);
            PCCMD_INV_IOMMU_PAGES_T pCmdInvPages = (PCCMD_INV_IOMMU_PAGES_T)pCmd;
            AssertCompile(sizeof(*pCmdInvPages) == sizeof(*pCmd));

            /* Validate reserved bits in the command. */
            if (   !(pCmdInvPages->au64[0] & ~IOMMU_CMD_INV_IOMMU_PAGES_QWORD_0_VALID_MASK)
                && !(pCmdInvPages->au64[1] & ~IOMMU_CMD_INV_IOMMU_PAGES_QWORD_1_VALID_MASK))
            {
#ifdef IOMMU_WITH_IOTLBE_CACHE
                uint64_t const uIova = RT_MAKE_U64(pCmdInvPages->n.u20AddrLo << X86_PAGE_4K_SHIFT, pCmdInvPages->n.u32AddrHi);
                uint16_t const idDomain  = pCmdInvPages->n.u16DomainId;
                uint8_t cShift;
                if (!pCmdInvPages->n.u1Size)
                    cShift = X86_PAGE_4K_SHIFT;
                else
                {
                    /* Find the first clear bit starting from bit 12 to 64 of the I/O virtual address. */
                    unsigned const uFirstZeroBit = ASMBitLastSetU64(~(uIova >> X86_PAGE_4K_SHIFT));
                    cShift = X86_PAGE_4K_SHIFT + uFirstZeroBit;

                    /*
                     * For the address 0x7ffffffffffff000, cShift would be 76 (12+64) and the code below
                     * would do the right thing by clearing the entire cache for the specified domain ID.
                     *
                     * However, for the address 0xfffffffffffff000, cShift would be computed as 12.
                     * IOMMU behavior is undefined in this case, so it's safe to invalidate just one page.
                     * A debug-time assert is in place here to let us know if any software tries this.
                     *
                     * See AMD IOMMU spec. 2.4.3 "INVALIDATE_IOMMU_PAGES".
                     * See AMD IOMMU spec. Table 14: "Example Page Size Encodings".
                     */
                    Assert(uIova != UINT64_C(0xfffffffffffff000));
                }

                /*
                 * Validate invalidation size.
                 * See AMD IOMMU spec. 2.2.3 "I/O Page Tables for Host Translations".
                 */
                if (   cShift >= 12 /* 4 KB */
                    && cShift <= 51 /* 2 PB */)
                {
                    /* Remove the range of I/O virtual addresses requesting to be invalidated. */
                    size_t const cbIova = RT_BIT_64(cShift);
                    iommuAmdIotlbRemoveRange(pDevIns, idDomain, uIova, cbIova);
                }
                else
                {
                    /*
                     * The guest provided size is invalid or exceeds the largest, meaningful page size.
                     * In such situations we must remove all ranges for the specified domain ID.
                     */
                    iommuAmdIotlbRemoveDomainId(pDevIns, idDomain);
                }
#endif
                return VINF_SUCCESS;
            }
            iommuAmdIllegalCmdEventInit(GCPhysCmd, (PEVT_ILLEGAL_CMD_ERR_T)pEvtError);
            return VERR_IOMMU_CMD_INVALID_FORMAT;
        }

        case IOMMU_CMD_INV_IOTLB_PAGES:
        {
            STAM_COUNTER_INC(&pThis->StatCmdInvIotlbPages);

            uint32_t const uCapHdr = PDMPciDevGetDWord(pDevIns->apPciDevs[0], IOMMU_PCI_OFF_CAP_HDR);
            if (RT_BF_GET(uCapHdr, IOMMU_BF_CAPHDR_IOTLB_SUP))
            {
                /** @todo IOMMU: Implement remote IOTLB invalidation. */
                return VERR_NOT_IMPLEMENTED;
            }
            iommuAmdIllegalCmdEventInit(GCPhysCmd, (PEVT_ILLEGAL_CMD_ERR_T)pEvtError);
            return VERR_IOMMU_CMD_NOT_SUPPORTED;
        }

        case IOMMU_CMD_INV_INTR_TABLE:
        {
            STAM_COUNTER_INC(&pThis->StatCmdInvIntrTable);

            PCCMD_INV_INTR_TABLE_T pCmdInvIntrTable = (PCCMD_INV_INTR_TABLE_T)pCmd;
            AssertCompile(sizeof(*pCmdInvIntrTable) == sizeof(*pCmd));

            /* Validate reserved bits in the command. */
            if (   !(pCmdInvIntrTable->au64[0] & ~IOMMU_CMD_INV_INTR_TABLE_QWORD_0_VALID_MASK)
                && !(pCmdInvIntrTable->au64[1] & ~IOMMU_CMD_INV_INTR_TABLE_QWORD_1_VALID_MASK))
            {
#ifdef IOMMU_WITH_IRTE_CACHE
                iommuAmdIrteCacheRemove(pDevIns, pCmdInvIntrTable->u.u16DevId);
#endif
                return VINF_SUCCESS;
            }
            iommuAmdIllegalCmdEventInit(GCPhysCmd, (PEVT_ILLEGAL_CMD_ERR_T)pEvtError);
            return VERR_IOMMU_CMD_INVALID_FORMAT;
        }

        case IOMMU_CMD_PREFETCH_IOMMU_PAGES:
        {
            /* Linux doesn't use prefetching of IOMMU pages, so we don't bother for now. */
            STAM_COUNTER_INC(&pThis->StatCmdPrefIommuPages);
            Assert(!pThis->ExtFeat.n.u1PrefetchSup);
            iommuAmdIllegalCmdEventInit(GCPhysCmd, (PEVT_ILLEGAL_CMD_ERR_T)pEvtError);
            return VERR_IOMMU_CMD_NOT_SUPPORTED;
        }

        case IOMMU_CMD_COMPLETE_PPR_REQ:
        {
            STAM_COUNTER_INC(&pThis->StatCmdCompletePprReq);

            /* We don't support PPR requests yet. */
            Assert(!pThis->ExtFeat.n.u1PprSup);
            iommuAmdIllegalCmdEventInit(GCPhysCmd, (PEVT_ILLEGAL_CMD_ERR_T)pEvtError);
            return VERR_IOMMU_CMD_NOT_SUPPORTED;
        }

        case IOMMU_CMD_INV_IOMMU_ALL:
        {
            STAM_COUNTER_INC(&pThis->StatCmdInvIommuAll);
            if (pThis->ExtFeat.n.u1InvAllSup)
            {
                PCCMD_INV_IOMMU_ALL_T pCmdInvAll = (PCCMD_INV_IOMMU_ALL_T)pCmd;
                AssertCompile(sizeof(*pCmdInvAll) == sizeof(*pCmd));

                /* Validate reserved bits in the command. */
                if (   !(pCmdInvAll->au64[0] & ~IOMMU_CMD_INV_IOMMU_ALL_QWORD_0_VALID_MASK)
                    && !(pCmdInvAll->au64[1] & ~IOMMU_CMD_INV_IOMMU_ALL_QWORD_1_VALID_MASK))
                {
#ifdef IOMMU_WITH_DTE_CACHE
                    iommuAmdDteCacheRemoveAll(pDevIns);
#endif
#ifdef IOMMU_WITH_IOTLBE_CACHE
                    iommuAmdIotlbRemoveAll(pDevIns);
#endif
                    return VINF_SUCCESS;
                }
                iommuAmdIllegalCmdEventInit(GCPhysCmd, (PEVT_ILLEGAL_CMD_ERR_T)pEvtError);
                return VERR_IOMMU_CMD_INVALID_FORMAT;
            }
            iommuAmdIllegalCmdEventInit(GCPhysCmd, (PEVT_ILLEGAL_CMD_ERR_T)pEvtError);
            return VERR_IOMMU_CMD_NOT_SUPPORTED;
        }
    }

    STAM_COUNTER_DEC(&pThis->StatCmd);
    LogFunc(("Cmd(%#x): Unrecognized\n", bCmd));
    iommuAmdIllegalCmdEventInit(GCPhysCmd, (PEVT_ILLEGAL_CMD_ERR_T)pEvtError);
    return VERR_IOMMU_CMD_NOT_SUPPORTED;
}


/**
 * The IOMMU command thread.
 *
 * @returns VBox status code.
 * @param   pDevIns     The IOMMU device instance.
 * @param   pThread     The command thread.
 */
static DECLCALLBACK(int) iommuAmdR3CmdThread(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    PIOMMU   pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PIOMMUR3 pThisR3 = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUR3);

    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    /*
     * Pre-allocate the maximum command buffer size supported by the IOMMU.
     * This avoid trashing the heap as well as not wasting time allocating
     * and freeing buffers while processing commands.
     */
    size_t const cbMaxCmdBuf = sizeof(CMD_GENERIC_T) * iommuAmdGetBufMaxEntries(15);
    void *pvCmds = RTMemAllocZ(cbMaxCmdBuf);
    AssertPtrReturn(pvCmds, VERR_NO_MEMORY);

    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        /*
         * Sleep perpetually until we are woken up to process commands.
         */
        bool const fSignaled = ASMAtomicXchgBool(&pThis->fCmdThreadSignaled, false);
        if (!fSignaled)
        {
            int rc = PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pThis->hEvtCmdThread, RT_INDEFINITE_WAIT);
            AssertLogRelMsgReturnStmt(RT_SUCCESS(rc) || rc == VERR_INTERRUPTED, ("%Rrc\n", rc), RTMemFree(pvCmds), rc);
            if (RT_UNLIKELY(pThread->enmState != PDMTHREADSTATE_RUNNING))
                break;
            Log4Func(("Woken up with rc=%Rrc\n", rc));
            ASMAtomicWriteBool(&pThis->fCmdThreadSignaled, false);
        }

        /*
         * Fetch and process IOMMU commands.
         */
        /** @todo r=ramshankar: We currently copy all commands from guest memory into a
         *        temporary host buffer before processing them as a batch. If we want to
         *        save on host memory a bit, we could (once PGM has the necessary APIs)
         *        lock the page mappings page mappings and access them directly. */
        IOMMU_LOCK(pDevIns, pThisR3);

        if (pThis->Status.n.u1CmdBufRunning)
        {
            /* Get the offsets we need to read commands from memory (circular buffer offset). */
            uint32_t const cbCmdBuf = iommuAmdGetTotalBufLength(pThis->CmdBufBaseAddr.n.u4Len);
            uint32_t const offTail  = pThis->CmdBufTailPtr.n.off;
            uint32_t       offHead  = pThis->CmdBufHeadPtr.n.off;

            /* Validate. */
            Assert(!(offHead & ~IOMMU_CMD_BUF_HEAD_PTR_VALID_MASK));
            Assert(offHead < cbCmdBuf);
            Assert(cbCmdBuf <= cbMaxCmdBuf);

            if (offHead != offTail)
            {
                /* Read the entire command buffer from memory (avoids multiple PGM calls). */
                RTGCPHYS const GCPhysCmdBufBase = pThis->CmdBufBaseAddr.n.u40Base << X86_PAGE_4K_SHIFT;

                IOMMU_UNLOCK(pDevIns, pThisR3);
                int rc = PDMDevHlpPCIPhysRead(pDevIns, GCPhysCmdBufBase, pvCmds, cbCmdBuf);
                IOMMU_LOCK(pDevIns, pThisR3);

                if (RT_SUCCESS(rc))
                {
                    /* Indicate to software we've fetched all commands from the buffer. */
                    pThis->CmdBufHeadPtr.n.off = offTail;

                    /* Allow IOMMU to do other work while we process commands. */
                    IOMMU_UNLOCK(pDevIns, pThisR3);

                    /* Process the fetched commands. */
                    EVT_GENERIC_T EvtError;
                    do
                    {
                        PCCMD_GENERIC_T pCmd = (PCCMD_GENERIC_T)((uintptr_t)pvCmds + offHead);
                        rc = iommuAmdR3CmdProcess(pDevIns, pCmd, GCPhysCmdBufBase + offHead, &EvtError);
                        if (RT_FAILURE(rc))
                        {
                            if (   rc == VERR_IOMMU_CMD_NOT_SUPPORTED
                                || rc == VERR_IOMMU_CMD_INVALID_FORMAT)
                            {
                                Assert(EvtError.n.u4EvtCode == IOMMU_EVT_ILLEGAL_CMD_ERROR);
                                iommuAmdIllegalCmdEventRaise(pDevIns, (PCEVT_ILLEGAL_CMD_ERR_T)&EvtError);
                            }
                            else if (rc == VERR_IOMMU_CMD_HW_ERROR)
                            {
                                Assert(EvtError.n.u4EvtCode == IOMMU_EVT_COMMAND_HW_ERROR);
                                LogFunc(("Raising command hardware error. Cmd=%#x -> COMMAND_HW_ERROR\n", pCmd->n.u4Opcode));
                                iommuAmdCmdHwErrorEventRaise(pDevIns, (PCEVT_CMD_HW_ERR_T)&EvtError);
                            }
                            break;
                        }

                        /* Move to the next command in the circular buffer. */
                        offHead = (offHead + sizeof(CMD_GENERIC_T)) % cbCmdBuf;
                    } while (offHead != offTail);
                }
                else
                {
                    LogFunc(("Failed to read command at %#RGp. rc=%Rrc -> COMMAND_HW_ERROR\n", GCPhysCmdBufBase, rc));
                    EVT_CMD_HW_ERR_T EvtCmdHwErr;
                    iommuAmdCmdHwErrorEventInit(GCPhysCmdBufBase, &EvtCmdHwErr);
                    iommuAmdCmdHwErrorEventRaise(pDevIns, &EvtCmdHwErr);

                    IOMMU_UNLOCK(pDevIns, pThisR3);
                }
            }
            else
                IOMMU_UNLOCK(pDevIns, pThisR3);
        }
        else
            IOMMU_UNLOCK(pDevIns, pThisR3);
    }

    RTMemFree(pvCmds);
    LogFlowFunc(("Command thread terminating\n"));
    return VINF_SUCCESS;
}


/**
 * Wakes up the command thread so it can respond to a state change.
 *
 * @returns VBox status code.
 * @param   pDevIns     The IOMMU device instance.
 * @param   pThread     The command thread.
 */
static DECLCALLBACK(int) iommuAmdR3CmdThreadWakeUp(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    RT_NOREF(pThread);
    Log4Func(("\n"));
    PCIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    return PDMDevHlpSUPSemEventSignal(pDevIns, pThis->hEvtCmdThread);
}


/**
 * @callback_method_impl{FNPCICONFIGREAD}
 */
static DECLCALLBACK(VBOXSTRICTRC) iommuAmdR3PciConfigRead(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t uAddress,
                                                          unsigned cb, uint32_t *pu32Value)
{
    /** @todo IOMMU: PCI config read stat counter. */
    VBOXSTRICTRC rcStrict = PDMDevHlpPCIConfigRead(pDevIns, pPciDev, uAddress, cb, pu32Value);
    Log3Func(("uAddress=%#x (cb=%u) -> %#x. rc=%Rrc\n", uAddress, cb, *pu32Value, VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/**
 * Sets up the IOMMU MMIO region (usually in response to an IOMMU base address
 * register write).
 *
 * @returns VBox status code.
 * @param   pDevIns     The IOMMU instance data.
 *
 * @remarks Call this function only when the IOMMU BAR is enabled.
 */
static int iommuAmdR3MmioSetup(PPDMDEVINS pDevIns)
{
    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    Assert(pThis->IommuBar.n.u1Enable);
    Assert(pThis->hMmio != NIL_IOMMMIOHANDLE);     /* Paranoia. Ensure we have a valid IOM MMIO handle. */
    Assert(!pThis->ExtFeat.n.u1PerfCounterSup);    /* Base is 16K aligned when performance counters aren't supported. */
    RTGCPHYS const GCPhysMmioBase     = RT_MAKE_U64(pThis->IommuBar.au32[0] & 0xffffc000, pThis->IommuBar.au32[1]);
    RTGCPHYS const GCPhysMmioBasePrev = PDMDevHlpMmioGetMappingAddress(pDevIns, pThis->hMmio);

    /* If the MMIO region is already mapped at the specified address, we're done. */
    Assert(GCPhysMmioBase != NIL_RTGCPHYS);
    if (GCPhysMmioBasePrev == GCPhysMmioBase)
        return VINF_SUCCESS;

    /* Unmap the previous MMIO region (which is at a different address). */
    if (GCPhysMmioBasePrev != NIL_RTGCPHYS)
    {
        LogFlowFunc(("Unmapping previous MMIO region at %#RGp\n", GCPhysMmioBasePrev));
        int rc = PDMDevHlpMmioUnmap(pDevIns, pThis->hMmio);
        if (RT_FAILURE(rc))
        {
            LogFunc(("Failed to unmap MMIO region at %#RGp. rc=%Rrc\n", GCPhysMmioBasePrev, rc));
            return rc;
        }
    }

    /* Map the newly specified MMIO region. */
    LogFlowFunc(("Mapping MMIO region at %#RGp\n", GCPhysMmioBase));
    int rc = PDMDevHlpMmioMap(pDevIns, pThis->hMmio, GCPhysMmioBase);
    if (RT_FAILURE(rc))
    {
        LogFunc(("Failed to unmap MMIO region at %#RGp. rc=%Rrc\n", GCPhysMmioBase, rc));
        return rc;
    }

    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNPCICONFIGWRITE}
 */
static DECLCALLBACK(VBOXSTRICTRC) iommuAmdR3PciConfigWrite(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t uAddress,
                                                           unsigned cb, uint32_t u32Value)
{
    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);

    /*
     * Discard writes to read-only registers that are specific to the IOMMU.
     * Other common PCI registers are handled by the generic code, see devpciR3IsConfigByteWritable().
     * See PCI spec. 6.1. "Configuration Space Organization".
     */
    switch (uAddress)
    {
        case IOMMU_PCI_OFF_CAP_HDR:         /* All bits are read-only. */
        case IOMMU_PCI_OFF_RANGE_REG:       /* We don't have any devices integrated with the IOMMU. */
        case IOMMU_PCI_OFF_MISCINFO_REG_0:  /* We don't support MSI-X. */
        case IOMMU_PCI_OFF_MISCINFO_REG_1:  /* We don't support guest-address translation. */
        {
            LogFunc(("PCI config write (%#RX32) to read-only register %#x -> Ignored\n", u32Value, uAddress));
            return VINF_SUCCESS;
        }
    }

    PIOMMUR3 pThisR3 = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUR3);
    IOMMU_LOCK(pDevIns, pThisR3);

    VBOXSTRICTRC rcStrict;
    switch (uAddress)
    {
        case IOMMU_PCI_OFF_BASE_ADDR_REG_LO:
        {
            if (!pThis->IommuBar.n.u1Enable)
            {
                pThis->IommuBar.au32[0] = u32Value & IOMMU_BAR_VALID_MASK;
                if (pThis->IommuBar.n.u1Enable)
                    rcStrict = iommuAmdR3MmioSetup(pDevIns);
                else
                    rcStrict = VINF_SUCCESS;
            }
            else
            {
                LogFunc(("Writing Base Address (Lo) when it's already enabled -> Ignored\n"));
                rcStrict = VINF_SUCCESS;
            }
            break;
        }

        case IOMMU_PCI_OFF_BASE_ADDR_REG_HI:
        {
            if (!pThis->IommuBar.n.u1Enable)
            {
                AssertCompile((IOMMU_BAR_VALID_MASK >> 32) == 0xffffffff);
                pThis->IommuBar.au32[1] = u32Value;
            }
            else
                LogFunc(("Writing Base Address (Hi) when it's already enabled -> Ignored\n"));
            rcStrict = VINF_SUCCESS;
            break;
        }

        case IOMMU_PCI_OFF_MSI_CAP_HDR:
        {
            u32Value |= RT_BIT(23);     /* 64-bit MSI addressess must always be enabled for IOMMU. */
            RT_FALL_THRU();
        }
        default:
        {
            rcStrict = PDMDevHlpPCIConfigWrite(pDevIns, pPciDev, uAddress, cb, u32Value);
            break;
        }
    }

    IOMMU_UNLOCK(pDevIns, pThisR3);

    Log3Func(("uAddress=%#x (cb=%u) with %#x. rc=%Rrc\n", uAddress, cb, u32Value, VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV}
 */
static DECLCALLBACK(void) iommuAmdR3DbgInfo(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    PCIOMMU    pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PCPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    bool const fVerbose = RTStrCmp(pszArgs, "verbose") == 0;

    pHlp->pfnPrintf(pHlp, "AMD-IOMMU:\n");
    /* Device Table Base Addresses (all segments). */
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aDevTabBaseAddrs); i++)
    {
        DEV_TAB_BAR_T const DevTabBar = pThis->aDevTabBaseAddrs[i];
        pHlp->pfnPrintf(pHlp, "  Device Table BAR %u                      = %#RX64\n", i, DevTabBar.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Size                                    = %#x (%u bytes)\n", DevTabBar.n.u9Size,
                            IOMMU_GET_DEV_TAB_LEN(&DevTabBar));
            pHlp->pfnPrintf(pHlp, "    Base address                            = %#RX64\n",
                            DevTabBar.n.u40Base << X86_PAGE_4K_SHIFT);
        }
    }
    /* Command Buffer Base Address Register. */
    {
        CMD_BUF_BAR_T const CmdBufBar = pThis->CmdBufBaseAddr;
        uint8_t const  uEncodedLen = CmdBufBar.n.u4Len;
        uint32_t const cEntries    = iommuAmdGetBufMaxEntries(uEncodedLen);
        uint32_t const cbBuffer    = iommuAmdGetTotalBufLength(uEncodedLen);
        pHlp->pfnPrintf(pHlp, "  Command Buffer BAR                      = %#RX64\n", CmdBufBar.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Base address                            = %#RX64\n",
                            CmdBufBar.n.u40Base << X86_PAGE_4K_SHIFT);
            pHlp->pfnPrintf(pHlp, "    Length                                  = %u (%u entries, %u bytes)\n", uEncodedLen,
                            cEntries, cbBuffer);
        }
    }
    /* Event Log Base Address Register. */
    {
        EVT_LOG_BAR_T const EvtLogBar = pThis->EvtLogBaseAddr;
        uint8_t const  uEncodedLen = EvtLogBar.n.u4Len;
        uint32_t const cEntries    = iommuAmdGetBufMaxEntries(uEncodedLen);
        uint32_t const cbBuffer    = iommuAmdGetTotalBufLength(uEncodedLen);
        pHlp->pfnPrintf(pHlp, "  Event Log BAR                           = %#RX64\n", EvtLogBar.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Base address                            = %#RX64\n",
                            EvtLogBar.n.u40Base << X86_PAGE_4K_SHIFT);
            pHlp->pfnPrintf(pHlp, "    Length                                  = %u (%u entries, %u bytes)\n", uEncodedLen,
                            cEntries, cbBuffer);
        }
    }
    /* IOMMU Control Register. */
    {
        IOMMU_CTRL_T const Ctrl = pThis->Ctrl;
        pHlp->pfnPrintf(pHlp, "  Control                                 = %#RX64\n", Ctrl.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    IOMMU enable                            = %RTbool\n", Ctrl.n.u1IommuEn);
            pHlp->pfnPrintf(pHlp, "    HT Tunnel translation enable            = %RTbool\n", Ctrl.n.u1HtTunEn);
            pHlp->pfnPrintf(pHlp, "    Event log enable                        = %RTbool\n", Ctrl.n.u1EvtLogEn);
            pHlp->pfnPrintf(pHlp, "    Event log interrupt enable              = %RTbool\n", Ctrl.n.u1EvtIntrEn);
            pHlp->pfnPrintf(pHlp, "    Completion wait interrupt enable        = %RTbool\n", Ctrl.n.u1EvtIntrEn);
            pHlp->pfnPrintf(pHlp, "    Invalidation timeout                    = %u\n",      Ctrl.n.u3InvTimeOut);
            pHlp->pfnPrintf(pHlp, "    Pass posted write                       = %RTbool\n", Ctrl.n.u1PassPW);
            pHlp->pfnPrintf(pHlp, "    Respose Pass posted write               = %RTbool\n", Ctrl.n.u1ResPassPW);
            pHlp->pfnPrintf(pHlp, "    Coherent                                = %RTbool\n", Ctrl.n.u1Coherent);
            pHlp->pfnPrintf(pHlp, "    Isochronous                             = %RTbool\n", Ctrl.n.u1Isoc);
            pHlp->pfnPrintf(pHlp, "    Command buffer enable                   = %RTbool\n", Ctrl.n.u1CmdBufEn);
            pHlp->pfnPrintf(pHlp, "    PPR log enable                          = %RTbool\n", Ctrl.n.u1PprLogEn);
            pHlp->pfnPrintf(pHlp, "    PPR interrupt enable                    = %RTbool\n", Ctrl.n.u1PprIntrEn);
            pHlp->pfnPrintf(pHlp, "    PPR enable                              = %RTbool\n", Ctrl.n.u1PprEn);
            pHlp->pfnPrintf(pHlp, "    Guest translation eanble                = %RTbool\n", Ctrl.n.u1GstTranslateEn);
            pHlp->pfnPrintf(pHlp, "    Guest virtual-APIC enable               = %RTbool\n", Ctrl.n.u1GstVirtApicEn);
            pHlp->pfnPrintf(pHlp, "    CRW                                     = %#x\n",     Ctrl.n.u4Crw);
            pHlp->pfnPrintf(pHlp, "    SMI filter enable                       = %RTbool\n", Ctrl.n.u1SmiFilterEn);
            pHlp->pfnPrintf(pHlp, "    Self-writeback disable                  = %RTbool\n", Ctrl.n.u1SelfWriteBackDis);
            pHlp->pfnPrintf(pHlp, "    SMI filter log enable                   = %RTbool\n", Ctrl.n.u1SmiFilterLogEn);
            pHlp->pfnPrintf(pHlp, "    Guest virtual-APIC mode enable          = %#x\n",     Ctrl.n.u3GstVirtApicModeEn);
            pHlp->pfnPrintf(pHlp, "    Guest virtual-APIC GA log enable        = %RTbool\n", Ctrl.n.u1GstLogEn);
            pHlp->pfnPrintf(pHlp, "    Guest virtual-APIC interrupt enable     = %RTbool\n", Ctrl.n.u1GstIntrEn);
            pHlp->pfnPrintf(pHlp, "    Dual PPR log enable                     = %#x\n",     Ctrl.n.u2DualPprLogEn);
            pHlp->pfnPrintf(pHlp, "    Dual event log enable                   = %#x\n",     Ctrl.n.u2DualEvtLogEn);
            pHlp->pfnPrintf(pHlp, "    Device table segmentation enable        = %#x\n",     Ctrl.n.u3DevTabSegEn);
            pHlp->pfnPrintf(pHlp, "    Privilege abort enable                  = %#x\n",     Ctrl.n.u2PrivAbortEn);
            pHlp->pfnPrintf(pHlp, "    PPR auto response enable                = %RTbool\n", Ctrl.n.u1PprAutoRespEn);
            pHlp->pfnPrintf(pHlp, "    MARC enable                             = %RTbool\n", Ctrl.n.u1MarcEn);
            pHlp->pfnPrintf(pHlp, "    Block StopMark enable                   = %RTbool\n", Ctrl.n.u1BlockStopMarkEn);
            pHlp->pfnPrintf(pHlp, "    PPR auto response always-on enable      = %RTbool\n", Ctrl.n.u1PprAutoRespAlwaysOnEn);
            pHlp->pfnPrintf(pHlp, "    Domain IDPNE                            = %RTbool\n", Ctrl.n.u1DomainIDPNE);
            pHlp->pfnPrintf(pHlp, "    Enhanced PPR handling                   = %RTbool\n", Ctrl.n.u1EnhancedPpr);
            pHlp->pfnPrintf(pHlp, "    Host page table access/dirty bit update = %#x\n",     Ctrl.n.u2HstAccDirtyBitUpdate);
            pHlp->pfnPrintf(pHlp, "    Guest page table dirty bit disable      = %RTbool\n", Ctrl.n.u1GstDirtyUpdateDis);
            pHlp->pfnPrintf(pHlp, "    x2APIC enable                           = %RTbool\n", Ctrl.n.u1X2ApicEn);
            pHlp->pfnPrintf(pHlp, "    x2APIC interrupt enable                 = %RTbool\n", Ctrl.n.u1X2ApicIntrGenEn);
            pHlp->pfnPrintf(pHlp, "    Guest page table access bit update      = %RTbool\n", Ctrl.n.u1GstAccessUpdateDis);
        }
    }
    /* Exclusion Base Address Register. */
    {
        IOMMU_EXCL_RANGE_BAR_T const ExclRangeBar = pThis->ExclRangeBaseAddr;
        pHlp->pfnPrintf(pHlp, "  Exclusion BAR                           = %#RX64\n", ExclRangeBar.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Exclusion enable                        = %RTbool\n", ExclRangeBar.n.u1ExclEnable);
            pHlp->pfnPrintf(pHlp, "    Allow all devices                       = %RTbool\n", ExclRangeBar.n.u1AllowAll);
            pHlp->pfnPrintf(pHlp, "    Base address                            = %#RX64\n",
                            ExclRangeBar.n.u40ExclRangeBase << X86_PAGE_4K_SHIFT);
        }
    }
    /* Exclusion Range Limit Register. */
    {
        IOMMU_EXCL_RANGE_LIMIT_T const ExclRangeLimit = pThis->ExclRangeLimit;
        pHlp->pfnPrintf(pHlp, "  Exclusion Range Limit                   = %#RX64\n", ExclRangeLimit.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Range limit                             = %#RX64\n",
                            (ExclRangeLimit.n.u40ExclRangeLimit << X86_PAGE_4K_SHIFT) | X86_PAGE_4K_OFFSET_MASK);
        }
    }
    /* Extended Feature Register. */
    {
        IOMMU_EXT_FEAT_T ExtFeat = pThis->ExtFeat;
        pHlp->pfnPrintf(pHlp, "  Extended Feature Register               = %#RX64\n", ExtFeat.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Prefetch support                        = %RTbool\n",  ExtFeat.n.u1PrefetchSup);
            pHlp->pfnPrintf(pHlp, "    PPR support                             = %RTbool\n",  ExtFeat.n.u1PprSup);
            pHlp->pfnPrintf(pHlp, "    x2APIC support                          = %RTbool\n",  ExtFeat.n.u1X2ApicSup);
            pHlp->pfnPrintf(pHlp, "    NX and privilege level support          = %RTbool\n",  ExtFeat.n.u1NoExecuteSup);
            pHlp->pfnPrintf(pHlp, "    Guest translation support               = %RTbool\n",  ExtFeat.n.u1GstTranslateSup);
            pHlp->pfnPrintf(pHlp, "    Invalidate-All command support          = %RTbool\n",  ExtFeat.n.u1InvAllSup);
            pHlp->pfnPrintf(pHlp, "    Guest virtual-APIC support              = %RTbool\n",  ExtFeat.n.u1GstVirtApicSup);
            pHlp->pfnPrintf(pHlp, "    Hardware error register support         = %RTbool\n",  ExtFeat.n.u1HwErrorSup);
            pHlp->pfnPrintf(pHlp, "    Performance counters support            = %RTbool\n",  ExtFeat.n.u1PerfCounterSup);
            pHlp->pfnPrintf(pHlp, "    Host address translation size           = %#x\n",      ExtFeat.n.u2HostAddrTranslateSize);
            pHlp->pfnPrintf(pHlp, "    Guest address translation size          = %#x\n",      ExtFeat.n.u2GstAddrTranslateSize);
            pHlp->pfnPrintf(pHlp, "    Guest CR3 root table level support      = %#x\n",      ExtFeat.n.u2GstCr3RootTblLevel);
            pHlp->pfnPrintf(pHlp, "    SMI filter register support             = %#x\n",      ExtFeat.n.u2SmiFilterSup);
            pHlp->pfnPrintf(pHlp, "    SMI filter register count               = %#x\n",      ExtFeat.n.u3SmiFilterCount);
            pHlp->pfnPrintf(pHlp, "    Guest virtual-APIC modes support        = %#x\n",      ExtFeat.n.u3GstVirtApicModeSup);
            pHlp->pfnPrintf(pHlp, "    Dual PPR log support                    = %#x\n",      ExtFeat.n.u2DualPprLogSup);
            pHlp->pfnPrintf(pHlp, "    Dual event log support                  = %#x\n",      ExtFeat.n.u2DualEvtLogSup);
            pHlp->pfnPrintf(pHlp, "    Maximum PASID                           = %#x\n",      ExtFeat.n.u5MaxPasidSup);
            pHlp->pfnPrintf(pHlp, "    User/supervisor page protection support = %RTbool\n",  ExtFeat.n.u1UserSupervisorSup);
            pHlp->pfnPrintf(pHlp, "    Device table segments supported         = %#x (%u)\n", ExtFeat.n.u2DevTabSegSup,
                            g_acDevTabSegs[ExtFeat.n.u2DevTabSegSup]);
            pHlp->pfnPrintf(pHlp, "    PPR log overflow early warning support  = %RTbool\n",  ExtFeat.n.u1PprLogOverflowWarn);
            pHlp->pfnPrintf(pHlp, "    PPR auto response support               = %RTbool\n",  ExtFeat.n.u1PprAutoRespSup);
            pHlp->pfnPrintf(pHlp, "    MARC support                            = %#x\n",      ExtFeat.n.u2MarcSup);
            pHlp->pfnPrintf(pHlp, "    Block StopMark message support          = %RTbool\n",  ExtFeat.n.u1BlockStopMarkSup);
            pHlp->pfnPrintf(pHlp, "    Performance optimization support        = %RTbool\n",  ExtFeat.n.u1PerfOptSup);
            pHlp->pfnPrintf(pHlp, "    MSI capability MMIO access support      = %RTbool\n",  ExtFeat.n.u1MsiCapMmioSup);
            pHlp->pfnPrintf(pHlp, "    Guest I/O protection support            = %RTbool\n",  ExtFeat.n.u1GstIoSup);
            pHlp->pfnPrintf(pHlp, "    Host access support                     = %RTbool\n",  ExtFeat.n.u1HostAccessSup);
            pHlp->pfnPrintf(pHlp, "    Enhanced PPR handling support           = %RTbool\n",  ExtFeat.n.u1EnhancedPprSup);
            pHlp->pfnPrintf(pHlp, "    Attribute forward supported             = %RTbool\n",  ExtFeat.n.u1AttrForwardSup);
            pHlp->pfnPrintf(pHlp, "    Host dirty support                      = %RTbool\n",  ExtFeat.n.u1HostDirtySup);
            pHlp->pfnPrintf(pHlp, "    Invalidate IOTLB type support           = %RTbool\n",  ExtFeat.n.u1InvIoTlbTypeSup);
            pHlp->pfnPrintf(pHlp, "    Guest page table access bit hw disable  = %RTbool\n",  ExtFeat.n.u1GstUpdateDisSup);
            pHlp->pfnPrintf(pHlp, "    Force physical dest for remapped intr.  = %RTbool\n",  ExtFeat.n.u1ForcePhysDstSup);
        }
    }
    /* PPR Log Base Address Register. */
    {
        PPR_LOG_BAR_T PprLogBar = pThis->PprLogBaseAddr;
        uint8_t const  uEncodedLen = PprLogBar.n.u4Len;
        uint32_t const cEntries    = iommuAmdGetBufMaxEntries(uEncodedLen);
        uint32_t const cbBuffer    = iommuAmdGetTotalBufLength(uEncodedLen);
        pHlp->pfnPrintf(pHlp, "  PPR Log BAR                             = %#RX64\n",   PprLogBar.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Base address                            = %#RX64\n",
                            PprLogBar.n.u40Base << X86_PAGE_4K_SHIFT);
            pHlp->pfnPrintf(pHlp, "    Length                                  = %u (%u entries, %u bytes)\n", uEncodedLen,
                            cEntries, cbBuffer);
        }
    }
    /* Hardware Event (Hi) Register. */
    {
        IOMMU_HW_EVT_HI_T HwEvtHi = pThis->HwEvtHi;
        pHlp->pfnPrintf(pHlp, "  Hardware Event (Hi)                     = %#RX64\n",   HwEvtHi.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    First operand                           = %#RX64\n", HwEvtHi.n.u60FirstOperand);
            pHlp->pfnPrintf(pHlp, "    Event code                              = %#RX8\n",  HwEvtHi.n.u4EvtCode);
        }
    }
    /* Hardware Event (Lo) Register. */
    pHlp->pfnPrintf(pHlp, "  Hardware Event (Lo)                     = %#RX64\n", pThis->HwEvtLo);
    /* Hardware Event Status. */
    {
        IOMMU_HW_EVT_STATUS_T HwEvtStatus = pThis->HwEvtStatus;
        pHlp->pfnPrintf(pHlp, "  Hardware Event Status                   = %#RX64\n",    HwEvtStatus.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Valid                                   = %RTbool\n", HwEvtStatus.n.u1Valid);
            pHlp->pfnPrintf(pHlp, "    Overflow                                = %RTbool\n", HwEvtStatus.n.u1Overflow);
        }
    }
    /* Guest Virtual-APIC Log Base Address Register. */
    {
        GALOG_BAR_T const GALogBar = pThis->GALogBaseAddr;
        uint8_t const  uEncodedLen = GALogBar.n.u4Len;
        uint32_t const cEntries    = iommuAmdGetBufMaxEntries(uEncodedLen);
        uint32_t const cbBuffer    = iommuAmdGetTotalBufLength(uEncodedLen);
        pHlp->pfnPrintf(pHlp, "  Guest Log BAR                           = %#RX64\n",    GALogBar.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Base address                            = %#RX64\n",
                            GALogBar.n.u40Base << X86_PAGE_4K_SHIFT);
            pHlp->pfnPrintf(pHlp, "    Length                                  = %u (%u entries, %u bytes)\n", uEncodedLen,
                            cEntries, cbBuffer);
        }
    }
    /* Guest Virtual-APIC Log Tail Address Register. */
    {
        GALOG_TAIL_ADDR_T GALogTail = pThis->GALogTailAddr;
        pHlp->pfnPrintf(pHlp, "  Guest Log Tail Address                  = %#RX64\n",   GALogTail.u64);
        if (fVerbose)
            pHlp->pfnPrintf(pHlp, "    Tail address                            = %#RX64\n", GALogTail.n.u40GALogTailAddr);
    }
    /* PPR Log B Base Address Register. */
    {
        PPR_LOG_B_BAR_T PprLogBBar = pThis->PprLogBBaseAddr;
        uint8_t const uEncodedLen  = PprLogBBar.n.u4Len;
        uint32_t const cEntries    = iommuAmdGetBufMaxEntries(uEncodedLen);
        uint32_t const cbBuffer    = iommuAmdGetTotalBufLength(uEncodedLen);
        pHlp->pfnPrintf(pHlp, "  PPR Log B BAR                           = %#RX64\n",   PprLogBBar.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Base address                            = %#RX64\n",
                            PprLogBBar.n.u40Base << X86_PAGE_4K_SHIFT);
            pHlp->pfnPrintf(pHlp, "    Length                                  = %u (%u entries, %u bytes)\n", uEncodedLen,
                            cEntries, cbBuffer);
        }
    }
    /* Event Log B Base Address Register. */
    {
        EVT_LOG_B_BAR_T EvtLogBBar = pThis->EvtLogBBaseAddr;
        uint8_t const  uEncodedLen = EvtLogBBar.n.u4Len;
        uint32_t const cEntries    = iommuAmdGetBufMaxEntries(uEncodedLen);
        uint32_t const cbBuffer    = iommuAmdGetTotalBufLength(uEncodedLen);
        pHlp->pfnPrintf(pHlp, "  Event Log B BAR                         = %#RX64\n",   EvtLogBBar.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Base address                            = %#RX64\n",
                            EvtLogBBar.n.u40Base << X86_PAGE_4K_SHIFT);
            pHlp->pfnPrintf(pHlp, "    Length                                  = %u (%u entries, %u bytes)\n", uEncodedLen,
                            cEntries, cbBuffer);
        }
    }
    /* Device-Specific Feature Extension Register. */
    {
        DEV_SPECIFIC_FEAT_T const DevSpecificFeat = pThis->DevSpecificFeat;
        pHlp->pfnPrintf(pHlp, "  Device-specific Feature                 = %#RX64\n",   DevSpecificFeat.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Feature                                 = %#RX32\n", DevSpecificFeat.n.u24DevSpecFeat);
            pHlp->pfnPrintf(pHlp, "    Minor revision ID                       = %#x\n",    DevSpecificFeat.n.u4RevMinor);
            pHlp->pfnPrintf(pHlp, "    Major revision ID                       = %#x\n",    DevSpecificFeat.n.u4RevMajor);
        }
    }
    /* Device-Specific Control Extension Register. */
    {
        DEV_SPECIFIC_CTRL_T const DevSpecificCtrl = pThis->DevSpecificCtrl;
        pHlp->pfnPrintf(pHlp, "  Device-specific Control                 = %#RX64\n",   DevSpecificCtrl.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Control                                 = %#RX32\n", DevSpecificCtrl.n.u24DevSpecCtrl);
            pHlp->pfnPrintf(pHlp, "    Minor revision ID                       = %#x\n",    DevSpecificCtrl.n.u4RevMinor);
            pHlp->pfnPrintf(pHlp, "    Major revision ID                       = %#x\n",    DevSpecificCtrl.n.u4RevMajor);
        }
    }
    /* Device-Specific Status Extension Register. */
    {
        DEV_SPECIFIC_STATUS_T const DevSpecificStatus = pThis->DevSpecificStatus;
        pHlp->pfnPrintf(pHlp, "  Device-specific Status                  = %#RX64\n",   DevSpecificStatus.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Status                                  = %#RX32\n", DevSpecificStatus.n.u24DevSpecStatus);
            pHlp->pfnPrintf(pHlp, "    Minor revision ID                       = %#x\n",    DevSpecificStatus.n.u4RevMinor);
            pHlp->pfnPrintf(pHlp, "    Major revision ID                       = %#x\n",    DevSpecificStatus.n.u4RevMajor);
        }
    }
    /* Miscellaneous Information Register (Lo and Hi). */
    {
        MSI_MISC_INFO_T const MiscInfo = pThis->MiscInfo;
        pHlp->pfnPrintf(pHlp, "  Misc. Info. Register                    = %#RX64\n",    MiscInfo.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Event Log MSI number                    = %#x\n",     MiscInfo.n.u5MsiNumEvtLog);
            pHlp->pfnPrintf(pHlp, "    Guest Virtual-Address Size              = %#x\n",     MiscInfo.n.u3GstVirtAddrSize);
            pHlp->pfnPrintf(pHlp, "    Physical Address Size                   = %#x\n",     MiscInfo.n.u7PhysAddrSize);
            pHlp->pfnPrintf(pHlp, "    Virtual-Address Size                    = %#x\n",     MiscInfo.n.u7VirtAddrSize);
            pHlp->pfnPrintf(pHlp, "    HT Transport ATS Range Reserved         = %RTbool\n", MiscInfo.n.u1HtAtsResv);
            pHlp->pfnPrintf(pHlp, "    PPR MSI number                          = %#x\n",     MiscInfo.n.u5MsiNumPpr);
            pHlp->pfnPrintf(pHlp, "    GA Log MSI number                       = %#x\n",     MiscInfo.n.u5MsiNumGa);
        }
    }
    /* MSI Capability Header. */
    {
        MSI_CAP_HDR_T MsiCapHdr;
        MsiCapHdr.u32 = PDMPciDevGetDWord(pPciDev, IOMMU_PCI_OFF_MSI_CAP_HDR);
        pHlp->pfnPrintf(pHlp, "  MSI Capability Header                   = %#RX32\n",    MsiCapHdr.u32);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Capability ID                           = %#x\n",     MsiCapHdr.n.u8MsiCapId);
            pHlp->pfnPrintf(pHlp, "    Capability Ptr (PCI config offset)      = %#x\n",     MsiCapHdr.n.u8MsiCapPtr);
            pHlp->pfnPrintf(pHlp, "    Enable                                  = %RTbool\n", MsiCapHdr.n.u1MsiEnable);
            pHlp->pfnPrintf(pHlp, "    Multi-message capability                = %#x\n",     MsiCapHdr.n.u3MsiMultiMessCap);
            pHlp->pfnPrintf(pHlp, "    Multi-message enable                    = %#x\n",     MsiCapHdr.n.u3MsiMultiMessEn);
        }
    }
    /* MSI Address Register (Lo and Hi). */
    {
        uint32_t const uMsiAddrLo = PDMPciDevGetDWord(pPciDev, IOMMU_PCI_OFF_MSI_ADDR_LO);
        uint32_t const uMsiAddrHi = PDMPciDevGetDWord(pPciDev, IOMMU_PCI_OFF_MSI_ADDR_HI);
        MSIADDR MsiAddr;
        MsiAddr.u64 = RT_MAKE_U64(uMsiAddrLo, uMsiAddrHi);
        pHlp->pfnPrintf(pHlp, "  MSI Address                             = %#RX64\n",   MsiAddr.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Destination mode                        = %#x\n",    MsiAddr.n.u1DestMode);
            pHlp->pfnPrintf(pHlp, "    Redirection hint                        = %#x\n",    MsiAddr.n.u1RedirHint);
            pHlp->pfnPrintf(pHlp, "    Destination Id                          = %#x\n",    MsiAddr.n.u8DestId);
            pHlp->pfnPrintf(pHlp, "    Address                                 = %#RX32\n", MsiAddr.n.u12Addr);
            pHlp->pfnPrintf(pHlp, "    Address (Hi) / Rsvd?                    = %#RX32\n", MsiAddr.n.u32Rsvd0);
        }
    }
    /* MSI Data. */
    {
        MSIDATA MsiData;
        MsiData.u32 = PDMPciDevGetDWord(pPciDev, IOMMU_PCI_OFF_MSI_DATA);
        pHlp->pfnPrintf(pHlp, "  MSI Data                                = %#RX32\n", MsiData.u32);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Vector                                  = %#x (%u)\n", MsiData.n.u8Vector,
                            MsiData.n.u8Vector);
            pHlp->pfnPrintf(pHlp, "    Delivery mode                           = %#x\n", MsiData.n.u3DeliveryMode);
            pHlp->pfnPrintf(pHlp, "    Level                                   = %#x\n", MsiData.n.u1Level);
            pHlp->pfnPrintf(pHlp, "    Trigger mode                            = %s\n",  MsiData.n.u1TriggerMode ?
                                                                                         "level" : "edge");
        }
    }
    /* MSI Mapping Capability Header (HyperTransport, reporting all 0s currently). */
    {
        MSI_MAP_CAP_HDR_T MsiMapCapHdr;
        MsiMapCapHdr.u32 = 0;
        pHlp->pfnPrintf(pHlp, "  MSI Mapping Capability Header           = %#RX32\n",    MsiMapCapHdr.u32);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Capability ID                           = %#x\n",     MsiMapCapHdr.n.u8MsiMapCapId);
            pHlp->pfnPrintf(pHlp, "    Map enable                              = %RTbool\n", MsiMapCapHdr.n.u1MsiMapEn);
            pHlp->pfnPrintf(pHlp, "    Map fixed                               = %RTbool\n", MsiMapCapHdr.n.u1MsiMapFixed);
            pHlp->pfnPrintf(pHlp, "    Map capability type                     = %#x\n",     MsiMapCapHdr.n.u5MapCapType);
        }
    }
    /* Performance Optimization Control Register. */
    {
        IOMMU_PERF_OPT_CTRL_T const PerfOptCtrl = pThis->PerfOptCtrl;
        pHlp->pfnPrintf(pHlp, "  Performance Optimization Control        = %#RX32\n",    PerfOptCtrl.u32);
        if (fVerbose)
            pHlp->pfnPrintf(pHlp, "    Enable                                  = %RTbool\n", PerfOptCtrl.n.u1PerfOptEn);
    }
    /* XT (x2APIC) General Interrupt Control Register. */
    {
        IOMMU_XT_GEN_INTR_CTRL_T const XtGenIntrCtrl = pThis->XtGenIntrCtrl;
        pHlp->pfnPrintf(pHlp, "  XT General Interrupt Control            = %#RX64\n", XtGenIntrCtrl.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Interrupt destination mode              = %s\n",
                            !XtGenIntrCtrl.n.u1X2ApicIntrDstMode ? "physical" : "logical");
            pHlp->pfnPrintf(pHlp, "    Interrupt destination                   = %#RX64\n",
                            RT_MAKE_U64(XtGenIntrCtrl.n.u24X2ApicIntrDstLo, XtGenIntrCtrl.n.u7X2ApicIntrDstHi));
            pHlp->pfnPrintf(pHlp, "    Interrupt vector                        = %#x\n", XtGenIntrCtrl.n.u8X2ApicIntrVector);
            pHlp->pfnPrintf(pHlp, "    Interrupt delivery mode                 = %s\n",
                            !XtGenIntrCtrl.n.u8X2ApicIntrVector ? "fixed" : "arbitrated");
        }
    }
    /* XT (x2APIC) PPR Interrupt Control Register. */
    {
        IOMMU_XT_PPR_INTR_CTRL_T const XtPprIntrCtrl = pThis->XtPprIntrCtrl;
        pHlp->pfnPrintf(pHlp, "  XT PPR Interrupt Control                = %#RX64\n", XtPprIntrCtrl.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "   Interrupt destination mode               = %s\n",
                            !XtPprIntrCtrl.n.u1X2ApicIntrDstMode ? "physical" : "logical");
            pHlp->pfnPrintf(pHlp, "   Interrupt destination                    = %#RX64\n",
                            RT_MAKE_U64(XtPprIntrCtrl.n.u24X2ApicIntrDstLo, XtPprIntrCtrl.n.u7X2ApicIntrDstHi));
            pHlp->pfnPrintf(pHlp, "   Interrupt vector                         = %#x\n", XtPprIntrCtrl.n.u8X2ApicIntrVector);
            pHlp->pfnPrintf(pHlp, "   Interrupt delivery mode                  = %s\n",
                            !XtPprIntrCtrl.n.u8X2ApicIntrVector ? "fixed" : "arbitrated");
        }
    }
    /* XT (X2APIC) GA Log Interrupt Control Register. */
    {
        IOMMU_XT_GALOG_INTR_CTRL_T const XtGALogIntrCtrl = pThis->XtGALogIntrCtrl;
        pHlp->pfnPrintf(pHlp, "  XT PPR Interrupt Control                = %#RX64\n", XtGALogIntrCtrl.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Interrupt destination mode              = %s\n",
                            !XtGALogIntrCtrl.n.u1X2ApicIntrDstMode ? "physical" : "logical");
            pHlp->pfnPrintf(pHlp, "    Interrupt destination                   = %#RX64\n",
                            RT_MAKE_U64(XtGALogIntrCtrl.n.u24X2ApicIntrDstLo, XtGALogIntrCtrl.n.u7X2ApicIntrDstHi));
            pHlp->pfnPrintf(pHlp, "    Interrupt vector                        = %#x\n", XtGALogIntrCtrl.n.u8X2ApicIntrVector);
            pHlp->pfnPrintf(pHlp, "    Interrupt delivery mode                 = %s\n",
                            !XtGALogIntrCtrl.n.u8X2ApicIntrVector ? "fixed" : "arbitrated");
        }
    }
    /* MARC Registers. */
    {
        for (unsigned i = 0; i < RT_ELEMENTS(pThis->aMarcApers); i++)
        {
            pHlp->pfnPrintf(pHlp, " MARC Aperature %u:\n", i);
            MARC_APER_BAR_T const MarcAperBar = pThis->aMarcApers[i].Base;
            pHlp->pfnPrintf(pHlp, "   Base    = %#RX64\n", MarcAperBar.n.u40MarcBaseAddr << X86_PAGE_4K_SHIFT);

            MARC_APER_RELOC_T const MarcAperReloc = pThis->aMarcApers[i].Reloc;
            pHlp->pfnPrintf(pHlp, "   Reloc   = %#RX64 (addr: %#RX64, read-only: %RTbool, enable: %RTbool)\n",
                            MarcAperReloc.u64, MarcAperReloc.n.u40MarcRelocAddr << X86_PAGE_4K_SHIFT,
                            MarcAperReloc.n.u1ReadOnly, MarcAperReloc.n.u1RelocEn);

            MARC_APER_LEN_T const MarcAperLen = pThis->aMarcApers[i].Length;
            pHlp->pfnPrintf(pHlp, "   Length  = %u pages\n", MarcAperLen.n.u40MarcLength);
        }
    }
    /* Reserved Register. */
    pHlp->pfnPrintf(pHlp, "  Reserved Register                       = %#RX64\n", pThis->RsvdReg);
    /* Command Buffer Head Pointer Register. */
    {
        CMD_BUF_HEAD_PTR_T const CmdBufHeadPtr = pThis->CmdBufHeadPtr;
        pHlp->pfnPrintf(pHlp, "  Command Buffer Head Pointer             = %#RX64 (off: %#x)\n", CmdBufHeadPtr.u64,
                        CmdBufHeadPtr.n.off);
    }
    /* Command Buffer Tail Pointer Register. */
    {
        CMD_BUF_HEAD_PTR_T const CmdBufTailPtr = pThis->CmdBufTailPtr;
        pHlp->pfnPrintf(pHlp, "  Command Buffer Tail Pointer             = %#RX64 (off: %#x)\n", CmdBufTailPtr.u64,
                        CmdBufTailPtr.n.off);
    }
    /* Event Log Head Pointer Register. */
    {
        EVT_LOG_HEAD_PTR_T const EvtLogHeadPtr = pThis->EvtLogHeadPtr;
        pHlp->pfnPrintf(pHlp, "  Event Log Head Pointer                  = %#RX64 (off: %#x)\n", EvtLogHeadPtr.u64,
                        EvtLogHeadPtr.n.off);
    }
    /* Event Log Tail Pointer Register. */
    {
        EVT_LOG_TAIL_PTR_T const EvtLogTailPtr = pThis->EvtLogTailPtr;
        pHlp->pfnPrintf(pHlp, "  Event Log Head Pointer                  = %#RX64 (off: %#x)\n", EvtLogTailPtr.u64,
                        EvtLogTailPtr.n.off);
    }
    /* Status Register. */
    {
        IOMMU_STATUS_T const Status = pThis->Status;
        pHlp->pfnPrintf(pHlp, "  Status Register                         = %#RX64\n", Status.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Event log overflow                      = %RTbool\n", Status.n.u1EvtOverflow);
            pHlp->pfnPrintf(pHlp, "    Event log interrupt                     = %RTbool\n", Status.n.u1EvtLogIntr);
            pHlp->pfnPrintf(pHlp, "    Completion wait interrupt               = %RTbool\n", Status.n.u1CompWaitIntr);
            pHlp->pfnPrintf(pHlp, "    Event log running                       = %RTbool\n", Status.n.u1EvtLogRunning);
            pHlp->pfnPrintf(pHlp, "    Command buffer running                  = %RTbool\n", Status.n.u1CmdBufRunning);
            pHlp->pfnPrintf(pHlp, "    PPR overflow                            = %RTbool\n", Status.n.u1PprOverflow);
            pHlp->pfnPrintf(pHlp, "    PPR interrupt                           = %RTbool\n", Status.n.u1PprIntr);
            pHlp->pfnPrintf(pHlp, "    PPR log running                         = %RTbool\n", Status.n.u1PprLogRunning);
            pHlp->pfnPrintf(pHlp, "    Guest log running                       = %RTbool\n", Status.n.u1GstLogRunning);
            pHlp->pfnPrintf(pHlp, "    Guest log interrupt                     = %RTbool\n", Status.n.u1GstLogIntr);
            pHlp->pfnPrintf(pHlp, "    PPR log B overflow                      = %RTbool\n", Status.n.u1PprOverflowB);
            pHlp->pfnPrintf(pHlp, "    PPR log active                          = %RTbool\n", Status.n.u1PprLogActive);
            pHlp->pfnPrintf(pHlp, "    Event log B overflow                    = %RTbool\n", Status.n.u1EvtOverflowB);
            pHlp->pfnPrintf(pHlp, "    Event log active                        = %RTbool\n", Status.n.u1EvtLogActive);
            pHlp->pfnPrintf(pHlp, "    PPR log B overflow early warning        = %RTbool\n", Status.n.u1PprOverflowEarlyB);
            pHlp->pfnPrintf(pHlp, "    PPR log overflow early warning          = %RTbool\n", Status.n.u1PprOverflowEarly);
        }
    }
    /* PPR Log Head Pointer. */
    {
        PPR_LOG_HEAD_PTR_T const PprLogHeadPtr = pThis->PprLogHeadPtr;
        pHlp->pfnPrintf(pHlp, "  PPR Log Head Pointer                    = %#RX64 (off: %#x)\n", PprLogHeadPtr.u64,
                        PprLogHeadPtr.n.off);
    }
    /* PPR Log Tail Pointer. */
    {
        PPR_LOG_TAIL_PTR_T const PprLogTailPtr = pThis->PprLogTailPtr;
        pHlp->pfnPrintf(pHlp, "  PPR Log Tail Pointer                    = %#RX64 (off: %#x)\n", PprLogTailPtr.u64,
                        PprLogTailPtr.n.off);
    }
    /* Guest Virtual-APIC Log Head Pointer. */
    {
        GALOG_HEAD_PTR_T const GALogHeadPtr = pThis->GALogHeadPtr;
        pHlp->pfnPrintf(pHlp, "  Guest Virtual-APIC Log Head Pointer     = %#RX64 (off: %#x)\n", GALogHeadPtr.u64,
                        GALogHeadPtr.n.u12GALogPtr);
    }
    /* Guest Virtual-APIC Log Tail Pointer. */
    {
        GALOG_HEAD_PTR_T const GALogTailPtr = pThis->GALogTailPtr;
        pHlp->pfnPrintf(pHlp, "  Guest Virtual-APIC Log Tail Pointer     = %#RX64 (off: %#x)\n", GALogTailPtr.u64,
                        GALogTailPtr.n.u12GALogPtr);
    }
    /* PPR Log B Head Pointer. */
    {
        PPR_LOG_B_HEAD_PTR_T const PprLogBHeadPtr = pThis->PprLogBHeadPtr;
        pHlp->pfnPrintf(pHlp, "  PPR Log B Head Pointer                  = %#RX64 (off: %#x)\n", PprLogBHeadPtr.u64,
                        PprLogBHeadPtr.n.off);
    }
    /* PPR Log B Tail Pointer. */
    {
        PPR_LOG_B_TAIL_PTR_T const PprLogBTailPtr = pThis->PprLogBTailPtr;
        pHlp->pfnPrintf(pHlp, "  PPR Log B Tail Pointer                  = %#RX64 (off: %#x)\n", PprLogBTailPtr.u64,
                        PprLogBTailPtr.n.off);
    }
    /* Event Log B Head Pointer. */
    {
        EVT_LOG_B_HEAD_PTR_T const EvtLogBHeadPtr = pThis->EvtLogBHeadPtr;
        pHlp->pfnPrintf(pHlp, "  Event Log B Head Pointer                = %#RX64 (off: %#x)\n", EvtLogBHeadPtr.u64,
                        EvtLogBHeadPtr.n.off);
    }
    /* Event Log B Tail Pointer. */
    {
        EVT_LOG_B_TAIL_PTR_T const EvtLogBTailPtr = pThis->EvtLogBTailPtr;
        pHlp->pfnPrintf(pHlp, "  Event Log B Tail Pointer                = %#RX64 (off: %#x)\n", EvtLogBTailPtr.u64,
                        EvtLogBTailPtr.n.off);
    }
    /* PPR Log Auto Response Register. */
    {
        PPR_LOG_AUTO_RESP_T const PprLogAutoResp = pThis->PprLogAutoResp;
        pHlp->pfnPrintf(pHlp, "  PPR Log Auto Response Register          = %#RX64\n",     PprLogAutoResp.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Code                                    = %#x\n",      PprLogAutoResp.n.u4AutoRespCode);
            pHlp->pfnPrintf(pHlp, "    Mask Gen.                               = %RTbool\n",  PprLogAutoResp.n.u1AutoRespMaskGen);
        }
    }
    /* PPR Log Overflow Early Warning Indicator Register. */
    {
        PPR_LOG_OVERFLOW_EARLY_T const PprLogOverflowEarly = pThis->PprLogOverflowEarly;
        pHlp->pfnPrintf(pHlp, "  PPR Log overflow early warning          = %#RX64\n",    PprLogOverflowEarly.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Threshold                               = %#x\n",     PprLogOverflowEarly.n.u15Threshold);
            pHlp->pfnPrintf(pHlp, "    Interrupt enable                        = %RTbool\n", PprLogOverflowEarly.n.u1IntrEn);
            pHlp->pfnPrintf(pHlp, "    Enable                                  = %RTbool\n", PprLogOverflowEarly.n.u1Enable);
        }
    }
    /* PPR Log Overflow Early Warning Indicator Register. */
    {
        PPR_LOG_OVERFLOW_EARLY_T const PprLogBOverflowEarly = pThis->PprLogBOverflowEarly;
        pHlp->pfnPrintf(pHlp, "  PPR Log B overflow early warning        = %#RX64\n",    PprLogBOverflowEarly.u64);
        if (fVerbose)
        {
            pHlp->pfnPrintf(pHlp, "    Threshold                               = %#x\n",     PprLogBOverflowEarly.n.u15Threshold);
            pHlp->pfnPrintf(pHlp, "    Interrupt enable                        = %RTbool\n", PprLogBOverflowEarly.n.u1IntrEn);
            pHlp->pfnPrintf(pHlp, "    Enable                                  = %RTbool\n", PprLogBOverflowEarly.n.u1Enable);
        }
    }
}


/**
 * Dumps the DTE via the info callback helper.
 *
 * @param   pHlp        The info helper.
 * @param   pDte        The device table entry.
 * @param   pszPrefix   The string prefix.
 */
static void iommuAmdR3DbgInfoDteWorker(PCDBGFINFOHLP pHlp, PCDTE_T pDte, const char *pszPrefix)
{
    AssertReturnVoid(pHlp);
    AssertReturnVoid(pDte);
    AssertReturnVoid(pszPrefix);

    pHlp->pfnPrintf(pHlp, "%sValid                      = %RTbool\n", pszPrefix, pDte->n.u1Valid);
    pHlp->pfnPrintf(pHlp, "%sTranslation Valid          = %RTbool\n", pszPrefix, pDte->n.u1TranslationValid);
    pHlp->pfnPrintf(pHlp, "%sHost Access Dirty          = %#x\n",     pszPrefix, pDte->n.u2Had);
    pHlp->pfnPrintf(pHlp, "%sPaging Mode                = %u\n",      pszPrefix, pDte->n.u3Mode);
    pHlp->pfnPrintf(pHlp, "%sPage Table Root Ptr        = %#RX64 (addr=%#RGp)\n", pszPrefix, pDte->n.u40PageTableRootPtrLo,
                    pDte->n.u40PageTableRootPtrLo << 12);
    pHlp->pfnPrintf(pHlp, "%sPPR enable                 = %RTbool\n", pszPrefix, pDte->n.u1Ppr);
    pHlp->pfnPrintf(pHlp, "%sGuest PPR Resp w/ PASID    = %RTbool\n", pszPrefix, pDte->n.u1GstPprRespPasid);
    pHlp->pfnPrintf(pHlp, "%sGuest I/O Prot Valid       = %RTbool\n", pszPrefix, pDte->n.u1GstIoValid);
    pHlp->pfnPrintf(pHlp, "%sGuest Translation Valid    = %RTbool\n", pszPrefix, pDte->n.u1GstTranslateValid);
    pHlp->pfnPrintf(pHlp, "%sGuest Levels Translated    = %#x\n",     pszPrefix, pDte->n.u2GstMode);
    pHlp->pfnPrintf(pHlp, "%sGuest Root Page Table Ptr  = %#x %#x %#x (addr=%#RGp)\n", pszPrefix,
                    pDte->n.u3GstCr3TableRootPtrLo, pDte->n.u16GstCr3TableRootPtrMid, pDte->n.u21GstCr3TableRootPtrHi,
                      (pDte->n.u21GstCr3TableRootPtrHi  << 31)
                    | (pDte->n.u16GstCr3TableRootPtrMid << 15)
                    | (pDte->n.u3GstCr3TableRootPtrLo   << 12));
    pHlp->pfnPrintf(pHlp, "%sI/O Read                   = %s\n",      pszPrefix, pDte->n.u1IoRead ? "allowed" : "denied");
    pHlp->pfnPrintf(pHlp, "%sI/O Write                  = %s\n",      pszPrefix, pDte->n.u1IoWrite ? "allowed" : "denied");
    pHlp->pfnPrintf(pHlp, "%sReserved (MBZ)             = %#x\n",     pszPrefix, pDte->n.u1Rsvd0);
    pHlp->pfnPrintf(pHlp, "%sDomain ID                  = %u (%#x)\n", pszPrefix, pDte->n.u16DomainId, pDte->n.u16DomainId);
    pHlp->pfnPrintf(pHlp, "%sIOTLB Enable               = %RTbool\n", pszPrefix, pDte->n.u1IoTlbEnable);
    pHlp->pfnPrintf(pHlp, "%sSuppress I/O PFs           = %RTbool\n", pszPrefix, pDte->n.u1SuppressPfEvents);
    pHlp->pfnPrintf(pHlp, "%sSuppress all I/O PFs       = %RTbool\n", pszPrefix, pDte->n.u1SuppressAllPfEvents);
    pHlp->pfnPrintf(pHlp, "%sPort I/O Control           = %#x\n",     pszPrefix, pDte->n.u2IoCtl);
    pHlp->pfnPrintf(pHlp, "%sIOTLB Cache Hint           = %s\n",      pszPrefix, pDte->n.u1Cache ? "no caching" : "cache");
    pHlp->pfnPrintf(pHlp, "%sSnoop Disable              = %RTbool\n", pszPrefix, pDte->n.u1SnoopDisable);
    pHlp->pfnPrintf(pHlp, "%sAllow Exclusion            = %RTbool\n", pszPrefix, pDte->n.u1AllowExclusion);
    pHlp->pfnPrintf(pHlp, "%sSysMgt Message Enable      = %RTbool\n", pszPrefix, pDte->n.u2SysMgt);
    pHlp->pfnPrintf(pHlp, "%sInterrupt Map Valid        = %RTbool\n", pszPrefix, pDte->n.u1IntrMapValid);
    uint8_t const uIntrTabLen = pDte->n.u4IntrTableLength;
    if (uIntrTabLen < IOMMU_DTE_INTR_TAB_LEN_MAX)
    {
        uint16_t const cEntries    = IOMMU_DTE_GET_INTR_TAB_ENTRIES(pDte);
        uint16_t const cbIntrTable = IOMMU_DTE_GET_INTR_TAB_LEN(pDte);
        pHlp->pfnPrintf(pHlp, "%sInterrupt Table Length     = %#x (%u entries, %u bytes)\n", pszPrefix, uIntrTabLen, cEntries,
                        cbIntrTable);
    }
    else
        pHlp->pfnPrintf(pHlp, "%sInterrupt Table Length     = %#x (invalid!)\n", pszPrefix, uIntrTabLen);
    pHlp->pfnPrintf(pHlp, "%sIgnore Unmapped Interrupts = %RTbool\n", pszPrefix, pDte->n.u1IgnoreUnmappedIntrs);
    pHlp->pfnPrintf(pHlp, "%sInterrupt Table Root Ptr   = %#RX64 (addr=%#RGp)\n", pszPrefix,
                    pDte->n.u46IntrTableRootPtr, pDte->au64[2] & IOMMU_DTE_IRTE_ROOT_PTR_MASK);
    pHlp->pfnPrintf(pHlp, "%sReserved (MBZ)             = %#x\n",     pszPrefix, pDte->n.u4Rsvd0);
    pHlp->pfnPrintf(pHlp, "%sINIT passthru              = %RTbool\n", pszPrefix, pDte->n.u1InitPassthru);
    pHlp->pfnPrintf(pHlp, "%sExtInt passthru            = %RTbool\n", pszPrefix, pDte->n.u1ExtIntPassthru);
    pHlp->pfnPrintf(pHlp, "%sNMI passthru               = %RTbool\n", pszPrefix, pDte->n.u1NmiPassthru);
    pHlp->pfnPrintf(pHlp, "%sReserved (MBZ)             = %#x\n",     pszPrefix, pDte->n.u1Rsvd2);
    pHlp->pfnPrintf(pHlp, "%sInterrupt Control          = %#x\n",     pszPrefix, pDte->n.u2IntrCtrl);
    pHlp->pfnPrintf(pHlp, "%sLINT0 passthru             = %RTbool\n", pszPrefix, pDte->n.u1Lint0Passthru);
    pHlp->pfnPrintf(pHlp, "%sLINT1 passthru             = %RTbool\n", pszPrefix, pDte->n.u1Lint1Passthru);
    pHlp->pfnPrintf(pHlp, "%sReserved (MBZ)             = %#x\n",     pszPrefix, pDte->n.u32Rsvd0);
    pHlp->pfnPrintf(pHlp, "%sReserved (MBZ)             = %#x\n",     pszPrefix, pDte->n.u22Rsvd0);
    pHlp->pfnPrintf(pHlp, "%sAttribute Override Valid   = %RTbool\n", pszPrefix, pDte->n.u1AttrOverride);
    pHlp->pfnPrintf(pHlp, "%sMode0FC                    = %#x\n",     pszPrefix, pDte->n.u1Mode0FC);
    pHlp->pfnPrintf(pHlp, "%sSnoop Attribute            = %#x\n",     pszPrefix, pDte->n.u8SnoopAttr);
    pHlp->pfnPrintf(pHlp, "\n");
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV}
 */
static DECLCALLBACK(void) iommuAmdR3DbgInfoDte(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    if (pszArgs)
    {
        uint16_t idDevice = 0;
        int rc = RTStrToUInt16Full(pszArgs, 0 /* uBase */, &idDevice);
        if (RT_SUCCESS(rc))
        {
            DTE_T Dte;
            rc = iommuAmdDteRead(pDevIns, idDevice, IOMMUOP_TRANSLATE_REQ,  &Dte);
            if (RT_SUCCESS(rc))
            {
                pHlp->pfnPrintf(pHlp, "DTE for device %#x\n", idDevice);
                iommuAmdR3DbgInfoDteWorker(pHlp, &Dte, " ");
                return;
            }
            pHlp->pfnPrintf(pHlp, "Failed to read DTE for device ID %u (%#x). rc=%Rrc\n", idDevice, idDevice, rc);
        }
        else
            pHlp->pfnPrintf(pHlp, "Failed to parse a valid 16-bit device ID. rc=%Rrc\n", rc);
    }
    else
        pHlp->pfnPrintf(pHlp, "Missing device ID.\n");
}


# ifdef IOMMU_WITH_DTE_CACHE
/**
 * @callback_method_impl{FNDBGFHANDLERDEV}
 */
static DECLCALLBACK(void) iommuAmdR3DbgInfoDteCache(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);
    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    IOMMU_CACHE_LOCK(pDevIns, pThis);

    uint16_t const cDteCache = RT_ELEMENTS(pThis->aDeviceIds);
    pHlp->pfnPrintf(pHlp, "DTE Cache: Capacity=%u entries\n", cDteCache);
    for (uint16_t i = 0; i < cDteCache; i++)
    {
        uint16_t const idDevice = pThis->aDeviceIds[i];
        if (idDevice)
        {
            pHlp->pfnPrintf(pHlp, " Entry[%u]: Device=%#x (BDF %02x:%02x.%d)\n", i, idDevice,
                            (idDevice >> VBOX_PCI_BUS_SHIFT) & VBOX_PCI_BUS_MASK,
                            (idDevice >> VBOX_PCI_DEVFN_DEV_SHIFT) & VBOX_PCI_DEVFN_DEV_MASK,
                            idDevice & VBOX_PCI_DEVFN_FUN_MASK);

            PCDTECACHE pDteCache = &pThis->aDteCache[i];
            pHlp->pfnPrintf(pHlp, "  Flags            = %#x\n", pDteCache->fFlags);
            pHlp->pfnPrintf(pHlp, "  Domain Id        = %u\n",  pDteCache->idDomain);
            pHlp->pfnPrintf(pHlp, "\n");
        }
    }
    IOMMU_CACHE_UNLOCK(pDevIns, pThis);
}
# endif /* IOMMU_WITH_DTE_CACHE */


# ifdef IOMMU_WITH_IOTLBE_CACHE
/**
 * @callback_method_impl{FNDBGFHANDLERDEV}
 */
static DECLCALLBACK(void) iommuAmdR3DbgInfoIotlb(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    if (pszArgs)
    {
        uint16_t idDomain = 0;
        int rc = RTStrToUInt16Full(pszArgs, 0 /* uBase */, &idDomain);
        if (RT_SUCCESS(rc))
        {
            pHlp->pfnPrintf(pHlp, "IOTLBEs for domain %u (%#x):\n", idDomain, idDomain);
            PIOMMU   pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
            PIOMMUR3 pThisR3 = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUR3);
            IOTLBEINFOARG Args;
            Args.pIommuR3 = pThisR3;
            Args.pHlp     = pHlp;
            Args.idDomain = idDomain;

            IOMMU_CACHE_LOCK(pDevIns, pThis);
            RTAvlU64DoWithAll(&pThisR3->TreeIotlbe, true /* fFromLeft */, iommuAmdR3IotlbEntryInfo, &Args);
            IOMMU_CACHE_UNLOCK(pDevIns, pThis);
        }
        else
            pHlp->pfnPrintf(pHlp, "Failed to parse a valid 16-bit domain ID. rc=%Rrc\n", rc);
    }
    else
        pHlp->pfnPrintf(pHlp, "Missing domain ID.\n");
}
# endif  /* IOMMU_WITH_IOTLBE_CACHE */


# ifdef IOMMU_WITH_IRTE_CACHE
/**
 * Gets the interrupt type name for an interrupt type in the IRTE.
 *
 * @returns The interrupt type name.
 * @param   uIntrType       The interrupt type (as specified in the IRTE).
 */
static const char *iommuAmdIrteGetIntrTypeName(uint8_t uIntrType)
{
    switch (uIntrType)
    {
        case VBOX_MSI_DELIVERY_MODE_FIXED:          return "Fixed";
        case VBOX_MSI_DELIVERY_MODE_LOWEST_PRIO:    return "Arbitrated";
        default:                                    return "<Reserved>";
    }
}


/**
 * @callback_method_impl{FNDBGFHANDLERDEV}
 */
static DECLCALLBACK(void) iommuAmdR3DbgInfoIrteCache(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);

    PIOMMU pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    IOMMU_CACHE_LOCK(pDevIns, pThis);

    uint16_t const cIrteCache = RT_ELEMENTS(pThis->aIrteCache);
    pHlp->pfnPrintf(pHlp, "IRTE Cache: Capacity=%u entries\n", cIrteCache);
    for (uint16_t idxIrte = 0; idxIrte < cIrteCache; idxIrte++)
    {
        PCIRTECACHE pIrteCache = &pThis->aIrteCache[idxIrte];
        uint32_t const uKey = pIrteCache->uKey;
        if (uKey != IOMMU_IRTE_CACHE_KEY_NIL)
        {
            uint16_t const idDevice = IOMMU_IRTE_CACHE_KEY_GET_DEVICE_ID(uKey);
            uint16_t const offIrte  = IOMMU_IRTE_CACHE_KEY_GET_OFF(uKey);
            pHlp->pfnPrintf(pHlp, " Entry[%u]: Offset=%#x Device=%#x (BDF %02x:%02x.%d)\n",
                            idxIrte, offIrte, idDevice,
                            (idDevice >> VBOX_PCI_BUS_SHIFT) & VBOX_PCI_BUS_MASK,
                            (idDevice >> VBOX_PCI_DEVFN_DEV_SHIFT) & VBOX_PCI_DEVFN_DEV_MASK,
                            idDevice & VBOX_PCI_DEVFN_FUN_MASK);

            PCIRTE_T pIrte = &pIrteCache->Irte;
            pHlp->pfnPrintf(pHlp, "  Remap Enable     = %RTbool\n", pIrte->n.u1RemapEnable);
            pHlp->pfnPrintf(pHlp, "  Suppress IOPF    = %RTbool\n", pIrte->n.u1SuppressIoPf);
            pHlp->pfnPrintf(pHlp, "  Interrupt Type   = %#x (%s)\n", pIrte->n.u3IntrType,
                            iommuAmdIrteGetIntrTypeName(pIrte->n.u3IntrType));
            pHlp->pfnPrintf(pHlp, "  Request EOI      = %RTbool\n", pIrte->n.u1ReqEoi);
            pHlp->pfnPrintf(pHlp, "  Destination mode = %s\n", pIrte->n.u1DestMode ? "Logical" : "Physical");
            pHlp->pfnPrintf(pHlp, "  Destination Id   = %u\n", pIrte->n.u8Dest);
            pHlp->pfnPrintf(pHlp, "  Vector           = %#x (%u)\n", pIrte->n.u8Vector, pIrte->n.u8Vector);
            pHlp->pfnPrintf(pHlp, "\n");
        }
    }
    IOMMU_CACHE_UNLOCK(pDevIns, pThis);
}
# endif /* IOMMU_WITH_IRTE_CACHE */


/**
 * @callback_method_impl{FNDBGFHANDLERDEV}
 */
static DECLCALLBACK(void) iommuAmdR3DbgInfoDevTabs(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);

    PCIOMMU    pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PCPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);
    NOREF(pPciDev);

    uint8_t cSegments = 0;
    for (uint8_t i = 0; i < RT_ELEMENTS(pThis->aDevTabBaseAddrs); i++)
    {
        DEV_TAB_BAR_T const DevTabBar = pThis->aDevTabBaseAddrs[i];
        RTGCPHYS const GCPhysDevTab   = DevTabBar.n.u40Base << X86_PAGE_4K_SHIFT;
        if (GCPhysDevTab)
            ++cSegments;
    }

    pHlp->pfnPrintf(pHlp, "AMD-IOMMU device tables with address translations enabled:\n");
    pHlp->pfnPrintf(pHlp, " DTE Segments=%u\n", cSegments);
    if (!cSegments)
        return;

    for (uint8_t i = 0; i < RT_ELEMENTS(pThis->aDevTabBaseAddrs); i++)
    {
        DEV_TAB_BAR_T const DevTabBar = pThis->aDevTabBaseAddrs[i];
        RTGCPHYS const GCPhysDevTab   = DevTabBar.n.u40Base << X86_PAGE_4K_SHIFT;
        if (GCPhysDevTab)
        {
            uint32_t const cbDevTab = IOMMU_GET_DEV_TAB_LEN(&DevTabBar);
            uint32_t const cDtes    = cbDevTab / sizeof(DTE_T);

            void *pvDevTab = RTMemAllocZ(cbDevTab);
            if (RT_LIKELY(pvDevTab))
            {
                int rc = PDMDevHlpPCIPhysRead(pDevIns, GCPhysDevTab, pvDevTab, cbDevTab);
                if (RT_SUCCESS(rc))
                {
                    for (uint32_t idxDte = 0; idxDte < cDtes; idxDte++)
                    {
                        PCDTE_T pDte = (PCDTE_T)((uintptr_t)pvDevTab + idxDte * sizeof(DTE_T));
                        if (   pDte->n.u1Valid
                            && pDte->n.u1TranslationValid
                            && pDte->n.u3Mode != 0)
                        {
                            pHlp->pfnPrintf(pHlp, " DTE %u (BDF %02x:%02x.%d)\n", idxDte,
                                            (idxDte >> VBOX_PCI_BUS_SHIFT) & VBOX_PCI_BUS_MASK,
                                            (idxDte >> VBOX_PCI_DEVFN_DEV_SHIFT) & VBOX_PCI_DEVFN_DEV_MASK,
                                            idxDte & VBOX_PCI_DEVFN_FUN_MASK);
                            iommuAmdR3DbgInfoDteWorker(pHlp, pDte, " ");
                            pHlp->pfnPrintf(pHlp, "\n");
                        }
                    }
                    pHlp->pfnPrintf(pHlp, "\n");
                }
                else
                {
                    pHlp->pfnPrintf(pHlp, " Failed to read table at %#RGp of size %zu bytes. rc=%Rrc!\n", GCPhysDevTab,
                                    cbDevTab, rc);
                }

                RTMemFree(pvDevTab);
            }
            else
            {
                pHlp->pfnPrintf(pHlp, " Allocating %zu bytes for reading the device table failed!\n", cbDevTab);
                return;
            }
        }
    }
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) iommuAmdR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PCIOMMU       pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PCPDMDEVHLPR3 pHlp  = pDevIns->pHlpR3;
    LogFlowFunc(("\n"));

    /* First, save ExtFeat and other registers that cannot be modified by the guest. */
    pHlp->pfnSSMPutU64(pSSM, pThis->ExtFeat.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->DevSpecificFeat.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->DevSpecificCtrl.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->DevSpecificStatus.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->MiscInfo.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->RsvdReg);

    /* Next, save all registers that can be modified by the guest. */
    pHlp->pfnSSMPutU64(pSSM, pThis->IommuBar.u64);

    uint8_t const cDevTabBaseAddrs = RT_ELEMENTS(pThis->aDevTabBaseAddrs);
    pHlp->pfnSSMPutU8(pSSM, cDevTabBaseAddrs);
    for (uint8_t i = 0; i < cDevTabBaseAddrs; i++)
        pHlp->pfnSSMPutU64(pSSM, pThis->aDevTabBaseAddrs[i].u64);

    AssertReturn(pThis->CmdBufBaseAddr.n.u4Len >= 8, VERR_IOMMU_IPE_4);
    pHlp->pfnSSMPutU64(pSSM, pThis->CmdBufBaseAddr.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->EvtLogBaseAddr.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->Ctrl.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->ExclRangeBaseAddr.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->ExclRangeLimit.u64);
#if 0
    pHlp->pfnSSMPutU64(pSSM, pThis->ExtFeat.u64);  /* read-only, done already (above). */
#endif

    pHlp->pfnSSMPutU64(pSSM, pThis->PprLogBaseAddr.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->HwEvtHi.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->HwEvtLo);
    pHlp->pfnSSMPutU64(pSSM, pThis->HwEvtStatus.u64);

    pHlp->pfnSSMPutU64(pSSM, pThis->GALogBaseAddr.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->GALogTailAddr.u64);

    pHlp->pfnSSMPutU64(pSSM, pThis->PprLogBBaseAddr.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->EvtLogBBaseAddr.u64);

#if 0
    pHlp->pfnSSMPutU64(pSSM, pThis->DevSpecificFeat.u64);       /* read-only, done already (above). */
    pHlp->pfnSSMPutU64(pSSM, pThis->DevSpecificCtrl.u64);       /* read-only, done already (above). */
    pHlp->pfnSSMPutU64(pSSM, pThis->DevSpecificStatus.u64);     /* read-only, done already (above). */

    pHlp->pfnSSMPutU64(pSSM, pThis->MiscInfo.u64);              /* read-only, done already (above). */
#endif
    pHlp->pfnSSMPutU32(pSSM, pThis->PerfOptCtrl.u32);

    pHlp->pfnSSMPutU64(pSSM, pThis->XtGenIntrCtrl.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->XtPprIntrCtrl.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->XtGALogIntrCtrl.u64);

    size_t const cMarcApers = RT_ELEMENTS(pThis->aMarcApers);
    pHlp->pfnSSMPutU8(pSSM, cMarcApers);
    for (size_t i = 0; i < cMarcApers; i++)
    {
        pHlp->pfnSSMPutU64(pSSM, pThis->aMarcApers[i].Base.u64);
        pHlp->pfnSSMPutU64(pSSM, pThis->aMarcApers[i].Reloc.u64);
        pHlp->pfnSSMPutU64(pSSM, pThis->aMarcApers[i].Length.u64);
    }

#if 0
    pHlp->pfnSSMPutU64(pSSM, pThis->RsvdReg);       /* read-only, done already (above). */
#endif

    pHlp->pfnSSMPutU64(pSSM, pThis->CmdBufHeadPtr.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->CmdBufTailPtr.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->EvtLogHeadPtr.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->EvtLogTailPtr.u64);

    pHlp->pfnSSMPutU64(pSSM, pThis->Status.u64);

    pHlp->pfnSSMPutU64(pSSM, pThis->PprLogHeadPtr.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->PprLogTailPtr.u64);

    pHlp->pfnSSMPutU64(pSSM, pThis->GALogHeadPtr.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->GALogTailPtr.u64);

    pHlp->pfnSSMPutU64(pSSM, pThis->PprLogBHeadPtr.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->PprLogBTailPtr.u64);

    pHlp->pfnSSMPutU64(pSSM, pThis->EvtLogBHeadPtr.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->EvtLogBTailPtr.u64);

    pHlp->pfnSSMPutU64(pSSM, pThis->PprLogAutoResp.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->PprLogOverflowEarly.u64);
    pHlp->pfnSSMPutU64(pSSM, pThis->PprLogBOverflowEarly.u64);

    return pHlp->pfnSSMPutU32(pSSM, UINT32_MAX);
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) iommuAmdR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PIOMMU        pThis = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PCPDMDEVHLPR3 pHlp  = pDevIns->pHlpR3;
    int const     rcErr = VERR_SSM_UNEXPECTED_DATA;
    LogFlowFunc(("\n"));

    /* Validate. */
    AssertReturn(uPass == SSM_PASS_FINAL, VERR_WRONG_ORDER);
    if (uVersion != IOMMU_SAVED_STATE_VERSION)
    {
        LogRel(("%s: Invalid saved-state version %#x\n", IOMMU_LOG_PFX, uVersion));
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;
    }

    /* Load ExtFeat and other read-only registers first. */
    int rc = pHlp->pfnSSMGetU64(pSSM, &pThis->ExtFeat.u64);
    AssertRCReturn(rc, rc);
    AssertLogRelMsgReturn(pThis->ExtFeat.n.u2HostAddrTranslateSize < 0x3,
                          ("ExtFeat.HATS register invalid %#RX64\n", pThis->ExtFeat.u64), rcErr);
    pHlp->pfnSSMGetU64(pSSM, &pThis->DevSpecificFeat.u64);
    pHlp->pfnSSMGetU64(pSSM, &pThis->DevSpecificCtrl.u64);
    pHlp->pfnSSMGetU64(pSSM, &pThis->DevSpecificStatus.u64);
    pHlp->pfnSSMGetU64(pSSM, &pThis->MiscInfo.u64);
    pHlp->pfnSSMGetU64(pSSM, &pThis->RsvdReg);

    /* IOMMU base address register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->IommuBar.u64);
    AssertRCReturn(rc, rc);
    pThis->IommuBar.u64 &= IOMMU_BAR_VALID_MASK;

    /* Device table base address registers. */
    uint8_t cDevTabBaseAddrs;
    rc = pHlp->pfnSSMGetU8(pSSM, &cDevTabBaseAddrs);
    AssertRCReturn(rc, rc);
    AssertLogRelMsgReturn(cDevTabBaseAddrs > 0 && cDevTabBaseAddrs <= RT_ELEMENTS(pThis->aDevTabBaseAddrs),
                          ("Device table segment count invalid %#x\n", cDevTabBaseAddrs), rcErr);
    AssertCompile(RT_ELEMENTS(pThis->aDevTabBaseAddrs) == RT_ELEMENTS(g_auDevTabSegMaxSizes));
    for (uint8_t i = 0; i < cDevTabBaseAddrs; i++)
    {
        rc = pHlp->pfnSSMGetU64(pSSM, &pThis->aDevTabBaseAddrs[i].u64);
        AssertRCReturn(rc, rc);
        pThis->aDevTabBaseAddrs[i].u64 &= IOMMU_DEV_TAB_BAR_VALID_MASK;
        uint16_t const uSegSize    = pThis->aDevTabBaseAddrs[i].n.u9Size;
        uint16_t const uMaxSegSize = g_auDevTabSegMaxSizes[i];
        AssertLogRelMsgReturn(uSegSize <= uMaxSegSize,
                              ("Device table [%u] segment size invalid %u (max %u)\n", i, uSegSize, uMaxSegSize), rcErr);
    }

    /* Command buffer base address register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->CmdBufBaseAddr.u64);
    AssertRCReturn(rc, rc);
    pThis->CmdBufBaseAddr.u64 &= IOMMU_CMD_BUF_BAR_VALID_MASK;
    AssertLogRelMsgReturn(pThis->CmdBufBaseAddr.n.u4Len >= 8,
                          ("Command buffer base address invalid %#RX64\n", pThis->CmdBufBaseAddr.u64), rcErr);

    /* Event log base address register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->EvtLogBaseAddr.u64);
    AssertRCReturn(rc, rc);
    pThis->EvtLogBaseAddr.u64 &= IOMMU_EVT_LOG_BAR_VALID_MASK;
    AssertLogRelMsgReturn(pThis->EvtLogBaseAddr.n.u4Len >= 8,
                          ("Event log base address invalid %#RX64\n", pThis->EvtLogBaseAddr.u64), rcErr);

    /* Control register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->Ctrl.u64);
    AssertRCReturn(rc, rc);
    pThis->Ctrl.u64 &= IOMMU_CTRL_VALID_MASK;
    AssertLogRelMsgReturn(pThis->Ctrl.n.u3DevTabSegEn <= pThis->ExtFeat.n.u2DevTabSegSup,
                          ("Control register invalid %#RX64\n", pThis->Ctrl.u64), rcErr);

    /* Exclusion range base address register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->ExclRangeBaseAddr.u64);
    AssertRCReturn(rc, rc);
    pThis->ExclRangeBaseAddr.u64 &= IOMMU_EXCL_RANGE_BAR_VALID_MASK;

    /* Exclusion range limit register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->ExclRangeLimit.u64);
    AssertRCReturn(rc, rc);
    pThis->ExclRangeLimit.u64 &= IOMMU_EXCL_RANGE_LIMIT_VALID_MASK;
    pThis->ExclRangeLimit.u64 |= UINT64_C(0xfff);

#if 0
    pHlp->pfnSSMGetU64(pSSM, &pThis->ExtFeat.u64);  /* read-only, done already (above). */
#endif

    /* PPR log base address register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->PprLogBaseAddr.u64);
    AssertRCReturn(rc, rc);
    Assert(!pThis->ExtFeat.n.u1PprSup);

    /* Hardware event (Hi) register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->HwEvtHi.u64);
    AssertRCReturn(rc, rc);

    /* Hardware event (Lo) register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->HwEvtLo);
    AssertRCReturn(rc, rc);

    /* Hardware event status register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->HwEvtStatus.u64);
    AssertRCReturn(rc, rc);
    pThis->HwEvtStatus.u64 &= IOMMU_HW_EVT_STATUS_VALID_MASK;

    /* Guest Virtual-APIC log base address register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->GALogBaseAddr.u64);
    AssertRCReturn(rc, rc);
    Assert(!pThis->ExtFeat.n.u1GstVirtApicSup);

    /* Guest Virtual-APIC log tail address register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->GALogTailAddr.u64);
    AssertRCReturn(rc, rc);
    Assert(!pThis->ExtFeat.n.u1GstVirtApicSup);

    /* PPR log-B base address register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->PprLogBBaseAddr.u64);
    AssertRCReturn(rc, rc);
    Assert(!pThis->ExtFeat.n.u1PprSup);

    /* Event log-B base address register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->EvtLogBBaseAddr.u64);
    AssertRCReturn(rc, rc);
    Assert(!pThis->ExtFeat.n.u2DualPprLogSup);

#if 0
    pHlp->pfnSSMGetU64(pSSM, &pThis->DevSpecificFeat.u64);       /* read-only, done already (above). */
    pHlp->pfnSSMGetU64(pSSM, &pThis->DevSpecificCtrl.u64);       /* read-only, done already (above). */
    pHlp->pfnSSMGetU64(pSSM, &pThis->DevSpecificStatus.u64);     /* read-only, done already (above). */

    pHlp->pfnSSMGetU64(pSSM, &pThis->MiscInfo.u64);              /* read-only, done already (above). */
#endif

    /* Performance optimization control register. */
    rc = pHlp->pfnSSMGetU32(pSSM, &pThis->PerfOptCtrl.u32);
    AssertRCReturn(rc, rc);
    Assert(!pThis->ExtFeat.n.u1PerfOptSup);

    /* x2APIC registers. */
    {
        Assert(!pThis->ExtFeat.n.u1X2ApicSup);

        /* x2APIC general interrupt control register. */
        pHlp->pfnSSMGetU64(pSSM, &pThis->XtGenIntrCtrl.u64);
        AssertRCReturn(rc, rc);

        /* x2APIC PPR interrupt control register. */
        rc = pHlp->pfnSSMGetU64(pSSM, &pThis->XtPprIntrCtrl.u64);
        AssertRCReturn(rc, rc);

        /* x2APIC GA log interrupt control register. */
        rc = pHlp->pfnSSMGetU64(pSSM, &pThis->XtGALogIntrCtrl.u64);
        AssertRCReturn(rc, rc);
    }

    /* MARC (Memory Access and Routing) registers. */
    {
        uint8_t cMarcApers;
        rc = pHlp->pfnSSMGetU8(pSSM, &cMarcApers);
        AssertRCReturn(rc, rc);
        AssertLogRelMsgReturn(cMarcApers > 0 && cMarcApers <= RT_ELEMENTS(pThis->aMarcApers),
                              ("MARC register count invalid %#x\n", cMarcApers), rcErr);
        for (uint8_t i = 0; i < cMarcApers; i++)
        {
            rc = pHlp->pfnSSMGetU64(pSSM, &pThis->aMarcApers[i].Base.u64);
            AssertRCReturn(rc, rc);

            rc = pHlp->pfnSSMGetU64(pSSM, &pThis->aMarcApers[i].Reloc.u64);
            AssertRCReturn(rc, rc);

            rc = pHlp->pfnSSMGetU64(pSSM, &pThis->aMarcApers[i].Length.u64);
            AssertRCReturn(rc, rc);
        }
        Assert(!pThis->ExtFeat.n.u2MarcSup);
    }

#if 0
    pHlp->pfnSSMGetU64(pSSM, &pThis->RsvdReg);  /* read-only, done already (above). */
#endif

    /* Command buffer head pointer register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->CmdBufHeadPtr.u64);
    AssertRCReturn(rc, rc);
    {
        /*
         * IOMMU behavior is undefined when software writes a value outside the buffer length.
         * In our emulation, since we ignore the write entirely (see iommuAmdCmdBufHeadPtr_w)
         * we shouldn't see such values in the saved state.
         */
        uint32_t const offBuf = pThis->CmdBufHeadPtr.u64 & IOMMU_CMD_BUF_HEAD_PTR_VALID_MASK;
        uint32_t const cbBuf  = iommuAmdGetTotalBufLength(pThis->CmdBufBaseAddr.n.u4Len);
        Assert(cbBuf <= _512K);
        AssertLogRelMsgReturn(offBuf < cbBuf,
                              ("Command buffer head pointer invalid %#x\n", pThis->CmdBufHeadPtr.u64), rcErr);
    }

    /* Command buffer tail pointer register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->CmdBufTailPtr.u64);
    AssertRCReturn(rc, rc);
    {
        uint32_t const offBuf = pThis->CmdBufTailPtr.u64 & IOMMU_CMD_BUF_TAIL_PTR_VALID_MASK;
        uint32_t const cbBuf  = iommuAmdGetTotalBufLength(pThis->CmdBufBaseAddr.n.u4Len);
        Assert(cbBuf <= _512K);
        AssertLogRelMsgReturn(offBuf < cbBuf,
                              ("Command buffer tail pointer invalid %#x\n", pThis->CmdBufTailPtr.u64), rcErr);
    }

    /* Event log head pointer register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->EvtLogHeadPtr.u64);
    AssertRCReturn(rc, rc);
    {
        uint32_t const offBuf = pThis->EvtLogHeadPtr.u64 & IOMMU_EVT_LOG_HEAD_PTR_VALID_MASK;
        uint32_t const cbBuf  = iommuAmdGetTotalBufLength(pThis->EvtLogBaseAddr.n.u4Len);
        Assert(cbBuf <= _512K);
        AssertLogRelMsgReturn(offBuf < cbBuf,
                              ("Event log head pointer invalid %#x\n", pThis->EvtLogHeadPtr.u64), rcErr);
    }

    /* Event log tail pointer register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->EvtLogTailPtr.u64);
    AssertRCReturn(rc, rc);
    {
        uint32_t const offBuf = pThis->EvtLogTailPtr.u64 & IOMMU_EVT_LOG_TAIL_PTR_VALID_MASK;
        uint32_t const cbBuf  = iommuAmdGetTotalBufLength(pThis->EvtLogBaseAddr.n.u4Len);
        Assert(cbBuf <= _512K);
        AssertLogRelMsgReturn(offBuf < cbBuf,
                              ("Event log tail pointer invalid %#x\n", pThis->EvtLogTailPtr.u64), rcErr);
    }

    /* Status register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->Status.u64);
    AssertRCReturn(rc, rc);
    pThis->Status.u64 &= IOMMU_STATUS_VALID_MASK;

    /* PPR log head pointer register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->PprLogHeadPtr.u64);
    AssertRCReturn(rc, rc);
    Assert(!pThis->ExtFeat.n.u1PprSup);

    /* PPR log tail pointer register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->PprLogTailPtr.u64);
    AssertRCReturn(rc, rc);
    Assert(!pThis->ExtFeat.n.u1PprSup);

    /* Guest Virtual-APIC log head pointer register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->GALogHeadPtr.u64);
    AssertRCReturn(rc, rc);
    Assert(!pThis->ExtFeat.n.u1GstVirtApicSup);

    /* Guest Virtual-APIC log tail pointer register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->GALogTailPtr.u64);
    AssertRCReturn(rc, rc);
    Assert(!pThis->ExtFeat.n.u1GstVirtApicSup);

    /* PPR log-B head pointer register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->PprLogBHeadPtr.u64);
    AssertRCReturn(rc, rc);
    Assert(!pThis->ExtFeat.n.u1PprSup);

    /* PPR log-B head pointer register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->PprLogBTailPtr.u64);
    AssertRCReturn(rc, rc);
    Assert(!pThis->ExtFeat.n.u1PprSup);

    /* Event log-B head pointer register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->EvtLogBHeadPtr.u64);
    AssertRCReturn(rc, rc);
    Assert(!pThis->ExtFeat.n.u2DualEvtLogSup);

    /* Event log-B tail pointer register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->EvtLogBTailPtr.u64);
    AssertRCReturn(rc, rc);
    Assert(!pThis->ExtFeat.n.u2DualEvtLogSup);

    /* PPR log auto response register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->PprLogAutoResp.u64);
    AssertRCReturn(rc, rc);
    Assert(!pThis->ExtFeat.n.u1PprAutoRespSup);

    /* PPR log overflow early indicator register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->PprLogOverflowEarly.u64);
    AssertRCReturn(rc, rc);
    Assert(!pThis->ExtFeat.n.u1PprLogOverflowWarn);

    /* PPR log-B overflow early indicator register. */
    rc = pHlp->pfnSSMGetU64(pSSM, &pThis->PprLogBOverflowEarly.u64);
    AssertRCReturn(rc, rc);
    Assert(!pThis->ExtFeat.n.u1PprLogOverflowWarn);

    /* End marker. */
    {
        uint32_t uEndMarker;
        rc = pHlp->pfnSSMGetU32(pSSM, &uEndMarker);
        AssertLogRelMsgRCReturn(rc, ("Failed to read end marker. rc=%Rrc\n", rc), VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
        AssertLogRelMsgReturn(uEndMarker == UINT32_MAX, ("End marker invalid (%#x expected %#x)\n", uEndMarker, UINT32_MAX),
                              rcErr);
    }

    return rc;
}


/**
 * @callback_method_impl{FNSSMDEVLOADDONE}
 */
static DECLCALLBACK(int) iommuAmdR3LoadDone(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PIOMMU   pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PIOMMUR3 pThisR3 = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUR3);
    RT_NOREF(pSSM);
    LogFlowFunc(("\n"));

    /* Sanity. */
    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertPtrReturn(pThisR3, VERR_INVALID_POINTER);

    int rc;
    IOMMU_LOCK(pDevIns, pThisR3);

    /* Map MMIO regions if the IOMMU BAR is enabled. */
    if (pThis->IommuBar.n.u1Enable)
        rc = iommuAmdR3MmioSetup(pDevIns);
    else
        rc = VINF_SUCCESS;

    /* Wake up the command thread if commands need processing. */
    iommuAmdCmdThreadWakeUpIfNeeded(pDevIns);

    IOMMU_UNLOCK(pDevIns, pThisR3);

    LogRel(("%s: Restored: DSFX=%u.%u DSCX=%u.%u DSSX=%u.%u ExtFeat=%#RX64\n", IOMMU_LOG_PFX,
            pThis->DevSpecificFeat.n.u4RevMajor, pThis->DevSpecificFeat.n.u4RevMinor,
            pThis->DevSpecificCtrl.n.u4RevMajor, pThis->DevSpecificCtrl.n.u4RevMinor,
            pThis->DevSpecificStatus.n.u4RevMajor, pThis->DevSpecificStatus.n.u4RevMinor,
            pThis->ExtFeat.u64));
    return rc;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) iommuAmdR3Reset(PPDMDEVINS pDevIns)
{
    /*
     * Resets read-write portion of the IOMMU state.
     *
     * NOTE! State not initialized here is expected to be initialized during
     * device construction and remain read-only through the lifetime of the VM.
     */
    PIOMMU     pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PIOMMUR3   pThisR3 = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUR3);
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);
    LogFlowFunc(("\n"));

    IOMMU_LOCK(pDevIns, pThisR3);

    RT_ZERO(pThis->aDevTabBaseAddrs);

    pThis->CmdBufBaseAddr.u64        = 0;
    pThis->CmdBufBaseAddr.n.u4Len    = 8;

    pThis->EvtLogBaseAddr.u64        = 0;
    pThis->EvtLogBaseAddr.n.u4Len    = 8;

    pThis->Ctrl.u64                  = 0;
    pThis->Ctrl.n.u1Coherent         = 1;
    Assert(!pThis->ExtFeat.n.u1BlockStopMarkSup);

    pThis->ExclRangeBaseAddr.u64     = 0;
    pThis->ExclRangeLimit.u64        = 0;

    pThis->PprLogBaseAddr.u64        = 0;
    pThis->PprLogBaseAddr.n.u4Len    = 8;

    pThis->HwEvtHi.u64               = 0;
    pThis->HwEvtLo                   = 0;
    pThis->HwEvtStatus.u64           = 0;

    pThis->GALogBaseAddr.u64         = 0;
    pThis->GALogBaseAddr.n.u4Len     = 8;
    pThis->GALogTailAddr.u64         = 0;

    pThis->PprLogBBaseAddr.u64       = 0;
    pThis->PprLogBBaseAddr.n.u4Len   = 8;

    pThis->EvtLogBBaseAddr.u64       = 0;
    pThis->EvtLogBBaseAddr.n.u4Len   = 8;

    pThis->PerfOptCtrl.u32           = 0;

    pThis->XtGenIntrCtrl.u64         = 0;
    pThis->XtPprIntrCtrl.u64         = 0;
    pThis->XtGALogIntrCtrl.u64       = 0;

    RT_ZERO(pThis->aMarcApers);

    pThis->CmdBufHeadPtr.u64         = 0;
    pThis->CmdBufTailPtr.u64         = 0;
    pThis->EvtLogHeadPtr.u64         = 0;
    pThis->EvtLogTailPtr.u64         = 0;

    pThis->Status.u64                = 0;

    pThis->PprLogHeadPtr.u64         = 0;
    pThis->PprLogTailPtr.u64         = 0;

    pThis->GALogHeadPtr.u64          = 0;
    pThis->GALogTailPtr.u64          = 0;

    pThis->PprLogBHeadPtr.u64        = 0;
    pThis->PprLogBTailPtr.u64        = 0;

    pThis->EvtLogBHeadPtr.u64        = 0;
    pThis->EvtLogBTailPtr.u64        = 0;

    pThis->PprLogAutoResp.u64        = 0;
    pThis->PprLogOverflowEarly.u64   = 0;
    pThis->PprLogBOverflowEarly.u64  = 0;

    pThis->IommuBar.u64              = 0;
    PDMPciDevSetDWord(pPciDev, IOMMU_PCI_OFF_BASE_ADDR_REG_LO, 0);
    PDMPciDevSetDWord(pPciDev, IOMMU_PCI_OFF_BASE_ADDR_REG_HI, 0);

    PDMPciDevSetCommand(pPciDev, VBOX_PCI_COMMAND_MASTER);

    IOMMU_UNLOCK(pDevIns, pThisR3);

#ifdef IOMMU_WITH_DTE_CACHE
    iommuAmdDteCacheRemoveAll(pDevIns);
#endif
#ifdef IOMMU_WITH_IOTLBE_CACHE
    iommuAmdIotlbRemoveAll(pDevIns);
#endif
#ifdef IOMMU_WITH_IRTE_CACHE
    iommuAmdIrteCacheRemoveAll(pDevIns);
#endif
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) iommuAmdR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PIOMMU   pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PIOMMUR3 pThisR3 = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUR3);
    LogFlowFunc(("\n"));

    IOMMU_LOCK(pDevIns, pThisR3);

    if (pThis->hEvtCmdThread != NIL_SUPSEMEVENT)
    {
        PDMDevHlpSUPSemEventClose(pDevIns, pThis->hEvtCmdThread);
        pThis->hEvtCmdThread = NIL_SUPSEMEVENT;
    }

#ifdef IOMMU_WITH_IOTLBE_CACHE
    if (pThisR3->paIotlbes)
    {
        PDMDevHlpMMHeapFree(pDevIns, pThisR3->paIotlbes);
        pThisR3->paIotlbes = NULL;
        pThisR3->idxUnusedIotlbe = 0;
    }
#endif

    IOMMU_UNLOCK(pDevIns, pThisR3);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) iommuAmdR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);

    PIOMMU        pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PIOMMUR3      pThisR3 = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUR3);
    PCPDMDEVHLPR3 pHlp    = pDevIns->pHlpR3;

    pThis->u32Magic = IOMMU_MAGIC;
    pThisR3->pDevInsR3 = pDevIns;

    LogFlowFunc(("iInstance=%d\n", iInstance));

    /*
     * Validate and read the configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns, "PCIAddress", "");
    int rc = pHlp->pfnCFGMQueryU32Def(pCfg, "PCIAddress", &pThis->uPciAddress, NIL_PCIBDF);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed to query 32-bit integer \"PCIAddress\""));
    if (!PCIBDF_IS_VALID(pThis->uPciAddress))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Configuration error: Failed \"PCIAddress\" of the AMD IOMMU cannot be invalid"));

    /*
     * Register the IOMMU with PDM.
     */
    PDMIOMMUREGR3 IommuReg;
    RT_ZERO(IommuReg);
    IommuReg.u32Version       = PDM_IOMMUREGCC_VERSION;
    IommuReg.pfnMemAccess     = iommuAmdMemAccess;
    IommuReg.pfnMemBulkAccess = iommuAmdMemBulkAccess;
    IommuReg.pfnMsiRemap      = iommuAmdMsiRemap;
    IommuReg.u32TheEnd        = PDM_IOMMUREGCC_VERSION;
    rc = PDMDevHlpIommuRegister(pDevIns, &IommuReg, &pThisR3->CTX_SUFF(pIommuHlp), &pThis->idxIommu);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("Failed to register ourselves as an IOMMU device"));
    if (pThisR3->CTX_SUFF(pIommuHlp)->u32Version != PDM_IOMMUHLPR3_VERSION)
        return PDMDevHlpVMSetError(pDevIns, VERR_VERSION_MISMATCH, RT_SRC_POS,
                                   N_("IOMMU helper version mismatch; got %#x expected %#x"),
                                   pThisR3->CTX_SUFF(pIommuHlp)->u32Version, PDM_IOMMUHLPR3_VERSION);
    if (pThisR3->CTX_SUFF(pIommuHlp)->u32TheEnd != PDM_IOMMUHLPR3_VERSION)
        return PDMDevHlpVMSetError(pDevIns, VERR_VERSION_MISMATCH, RT_SRC_POS,
                                   N_("IOMMU helper end-version mismatch; got %#x expected %#x"),
                                   pThisR3->CTX_SUFF(pIommuHlp)->u32TheEnd, PDM_IOMMUHLPR3_VERSION);
    AssertPtr(pThisR3->pIommuHlpR3->pfnLock);
    AssertPtr(pThisR3->pIommuHlpR3->pfnUnlock);
    AssertPtr(pThisR3->pIommuHlpR3->pfnLockIsOwner);
    AssertPtr(pThisR3->pIommuHlpR3->pfnSendMsi);

    /*
     * We will use PDM's critical section (via helpers) for the IOMMU device.
     */
    rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /*
     * Initialize read-only PCI configuration space.
     */
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    /* Header. */
    PDMPciDevSetVendorId(pPciDev,          IOMMU_PCI_VENDOR_ID);       /* AMD */
    PDMPciDevSetDeviceId(pPciDev,          IOMMU_PCI_DEVICE_ID);       /* VirtualBox IOMMU device */
    PDMPciDevSetCommand(pPciDev,           VBOX_PCI_COMMAND_MASTER);   /* Enable bus master (as we directly access main memory) */
    PDMPciDevSetStatus(pPciDev,            VBOX_PCI_STATUS_CAP_LIST);  /* Capability list supported */
    PDMPciDevSetRevisionId(pPciDev,        IOMMU_PCI_REVISION_ID);     /* VirtualBox specific device implementation revision */
    PDMPciDevSetClassBase(pPciDev,         VBOX_PCI_CLASS_SYSTEM);     /* System Base Peripheral */
    PDMPciDevSetClassSub(pPciDev,          VBOX_PCI_SUB_SYSTEM_IOMMU); /* IOMMU */
    PDMPciDevSetClassProg(pPciDev,         0x0);                       /* IOMMU Programming interface */
    PDMPciDevSetHeaderType(pPciDev,        0x0);                       /* Single function, type 0 */
    PDMPciDevSetSubSystemId(pPciDev,       IOMMU_PCI_DEVICE_ID);       /* AMD */
    PDMPciDevSetSubSystemVendorId(pPciDev, IOMMU_PCI_VENDOR_ID);       /* VirtualBox IOMMU device */
    PDMPciDevSetCapabilityList(pPciDev,    IOMMU_PCI_OFF_CAP_HDR);     /* Offset into capability registers */
    PDMPciDevSetInterruptPin(pPciDev,      0x1);                       /* INTA#. */
    PDMPciDevSetInterruptLine(pPciDev,     0x0);                       /* For software compatibility; no effect on hardware */

    /* Capability Header. */
    /* NOTE! Fields (e.g, EFR) must match what we expose in the ACPI tables. */
    PDMPciDevSetDWord(pPciDev, IOMMU_PCI_OFF_CAP_HDR,
                               RT_BF_MAKE(IOMMU_BF_CAPHDR_CAP_ID,    0xf)     /* RO - Secure Device capability block */
                             | RT_BF_MAKE(IOMMU_BF_CAPHDR_CAP_PTR,   IOMMU_PCI_OFF_MSI_CAP_HDR)  /* RO - Next capability offset */
                             | RT_BF_MAKE(IOMMU_BF_CAPHDR_CAP_TYPE,  0x3)     /* RO - IOMMU capability block */
                             | RT_BF_MAKE(IOMMU_BF_CAPHDR_CAP_REV,   0x1)     /* RO - IOMMU interface revision */
                             | RT_BF_MAKE(IOMMU_BF_CAPHDR_IOTLB_SUP, 0x0)     /* RO - Remote IOTLB support */
                             | RT_BF_MAKE(IOMMU_BF_CAPHDR_HT_TUNNEL, 0x0)     /* RO - HyperTransport Tunnel support */
                             | RT_BF_MAKE(IOMMU_BF_CAPHDR_NP_CACHE,  0x0)     /* RO - Cache NP page table entries */
                             | RT_BF_MAKE(IOMMU_BF_CAPHDR_EFR_SUP,   0x1)     /* RO - Extended Feature Register support */
                             | RT_BF_MAKE(IOMMU_BF_CAPHDR_CAP_EXT,   0x1));   /* RO - Misc. Information Register support */

    /* Base Address Register. */
    PDMPciDevSetDWord(pPciDev, IOMMU_PCI_OFF_BASE_ADDR_REG_LO, 0x0);   /* RW - Base address (Lo) and enable bit */
    PDMPciDevSetDWord(pPciDev, IOMMU_PCI_OFF_BASE_ADDR_REG_HI, 0x0);   /* RW - Base address (Hi) */

    /* IOMMU Range Register. */
    PDMPciDevSetDWord(pPciDev, IOMMU_PCI_OFF_RANGE_REG, 0x0);          /* RW - Range register (implemented as RO by us) */

    /* Misc. Information Register. */
    /* NOTE! Fields (e.g, GVA size) must match what we expose in the ACPI tables. */
    uint32_t const  uMiscInfoReg0 = RT_BF_MAKE(IOMMU_BF_MISCINFO_0_MSI_NUM,      0)   /* RO - MSI number */
                                  | RT_BF_MAKE(IOMMU_BF_MISCINFO_0_GVA_SIZE,     2)   /* RO - Guest Virt. Addr size (2=48 bits) */
                                  | RT_BF_MAKE(IOMMU_BF_MISCINFO_0_PA_SIZE,     48)   /* RO - Physical Addr size (48 bits) */
                                  | RT_BF_MAKE(IOMMU_BF_MISCINFO_0_VA_SIZE,     64)   /* RO - Virt. Addr size (64 bits) */
                                  | RT_BF_MAKE(IOMMU_BF_MISCINFO_0_HT_ATS_RESV,  0)   /* RW - HT ATS reserved */
                                  | RT_BF_MAKE(IOMMU_BF_MISCINFO_0_MSI_NUM_PPR,  0);  /* RW - PPR interrupt number */
    uint32_t const uMiscInfoReg1  = 0;
    PDMPciDevSetDWord(pPciDev, IOMMU_PCI_OFF_MISCINFO_REG_0, uMiscInfoReg0);
    PDMPciDevSetDWord(pPciDev, IOMMU_PCI_OFF_MISCINFO_REG_1, uMiscInfoReg1);

    /* MSI Capability Header register. */
    PDMMSIREG MsiReg;
    RT_ZERO(MsiReg);
    MsiReg.cMsiVectors    = 1;
    MsiReg.iMsiCapOffset  = IOMMU_PCI_OFF_MSI_CAP_HDR;
    MsiReg.iMsiNextOffset = 0; /* IOMMU_PCI_OFF_MSI_MAP_CAP_HDR */
    MsiReg.fMsi64bit      = 1; /* 64-bit addressing support is mandatory; See AMD IOMMU spec. 2.8 "IOMMU Interrupt Support". */

    /* MSI Address (Lo, Hi) and MSI data are read-write PCI config registers handled by our generic PCI config space code. */
#if 0
    /* MSI Address Lo. */
    PDMPciDevSetDWord(pPciDev, IOMMU_PCI_OFF_MSI_ADDR_LO, 0);         /* RW - MSI message address (Lo) */
    /* MSI Address Hi. */
    PDMPciDevSetDWord(pPciDev, IOMMU_PCI_OFF_MSI_ADDR_HI, 0);         /* RW - MSI message address (Hi) */
    /* MSI Data. */
    PDMPciDevSetDWord(pPciDev, IOMMU_PCI_OFF_MSI_DATA, 0);            /* RW - MSI data */
#endif

#if 0
    /** @todo IOMMU: I don't know if we need to support this, enable later if
     *        required. */
    /* MSI Mapping Capability Header register. */
    PDMPciDevSetDWord(pPciDev, IOMMU_PCI_OFF_MSI_MAP_CAP_HDR,
                        RT_BF_MAKE(IOMMU_BF_MSI_MAP_CAPHDR_CAP_ID,   0x8)       /* RO - Capability ID */
                      | RT_BF_MAKE(IOMMU_BF_MSI_MAP_CAPHDR_CAP_PTR,  0x0)       /* RO - Offset to next capability (NULL) */
                      | RT_BF_MAKE(IOMMU_BF_MSI_MAP_CAPHDR_EN,       0x1)       /* RO - MSI mapping capability enable */
                      | RT_BF_MAKE(IOMMU_BF_MSI_MAP_CAPHDR_FIXED,    0x1)       /* RO - MSI mapping range is fixed */
                      | RT_BF_MAKE(IOMMU_BF_MSI_MAP_CAPHDR_CAP_TYPE, 0x15));    /* RO - MSI mapping capability */
    /* When implementing don't forget to copy this to its MMIO shadow register (MsiMapCapHdr) in iommuAmdR3Init. */
#endif

    /*
     * Register the PCI function with PDM.
     */
    rc = PDMDevHlpPCIRegister(pDevIns, pPciDev);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Register MSI support for the PCI device.
     * This must be done -after- registering it as a PCI device!
     */
    rc = PDMDevHlpPCIRegisterMsi(pDevIns, &MsiReg);
    AssertRCReturn(rc, rc);

    /*
     * Intercept PCI config. space accesses.
     */
    rc = PDMDevHlpPCIInterceptConfigAccesses(pDevIns, pPciDev, iommuAmdR3PciConfigRead, iommuAmdR3PciConfigWrite);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Create the MMIO region.
     * Mapping of the region is done when software configures it via PCI config space.
     */
    rc = PDMDevHlpMmioCreate(pDevIns, IOMMU_MMIO_REGION_SIZE, pPciDev, 0 /* iPciRegion */, iommuAmdMmioWrite, iommuAmdMmioRead,
                             NULL /* pvUser */,
                               IOMMMIO_FLAGS_READ_DWORD_QWORD
                             | IOMMMIO_FLAGS_WRITE_DWORD_QWORD_READ_MISSING
                             | IOMMMIO_FLAGS_DBGSTOP_ON_COMPLICATED_READ
                             | IOMMMIO_FLAGS_DBGSTOP_ON_COMPLICATED_WRITE,
                             "AMD-IOMMU", &pThis->hMmio);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Register saved state handlers.
     */
    rc = PDMDevHlpSSMRegisterEx(pDevIns, IOMMU_SAVED_STATE_VERSION, sizeof(IOMMU), NULL /* pszBefore */,
                                NULL /* pfnLivePrep */,  NULL /* pfnLiveExec */,  NULL /* pfnLiveVote */,
                                NULL /* pfnSavePrep */,  iommuAmdR3SaveExec, NULL /* pfnSaveDone */,
                                NULL /* pfnLoadPrep */,  iommuAmdR3LoadExec, iommuAmdR3LoadDone);
    AssertLogRelRCReturn(rc, rc);

    /*
     * Register debugger info items.
     */
    PDMDevHlpDBGFInfoRegister(pDevIns, "iommu",    "Display IOMMU state.", iommuAmdR3DbgInfo);
    PDMDevHlpDBGFInfoRegister(pDevIns, "iommudte", "Display the DTE for a device (from memory). Arguments: DeviceID.", iommuAmdR3DbgInfoDte);
    PDMDevHlpDBGFInfoRegister(pDevIns, "iommudevtabs", "Display I/O device tables with translation enabled.", iommuAmdR3DbgInfoDevTabs);
#ifdef IOMMU_WITH_IOTLBE_CACHE
    PDMDevHlpDBGFInfoRegister(pDevIns, "iommutlb", "Display IOTLBs for a domain. Arguments: DomainID.", iommuAmdR3DbgInfoIotlb);
#endif
#ifdef IOMMU_WITH_DTE_CACHE
    PDMDevHlpDBGFInfoRegister(pDevIns, "iommudtecache", "Display the DTE cache.", iommuAmdR3DbgInfoDteCache);
#endif
#ifdef IOMMU_WITH_IRTE_CACHE
    PDMDevHlpDBGFInfoRegister(pDevIns, "iommuirtecache", "Display the IRTE cache.", iommuAmdR3DbgInfoIrteCache);
#endif

# ifdef VBOX_WITH_STATISTICS
    /*
     * Statistics.
     */
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMmioReadR3,  STAMTYPE_COUNTER, "R3/MmioRead",  STAMUNIT_OCCURENCES, "Number of MMIO reads in R3");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMmioReadRZ,  STAMTYPE_COUNTER, "RZ/MmioRead",  STAMUNIT_OCCURENCES, "Number of MMIO reads in RZ.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMmioWriteR3, STAMTYPE_COUNTER, "R3/MmioWrite", STAMUNIT_OCCURENCES, "Number of MMIO writes in R3.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMmioWriteRZ, STAMTYPE_COUNTER, "RZ/MmioWrite", STAMUNIT_OCCURENCES, "Number of MMIO writes in RZ.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMsiRemapR3, STAMTYPE_COUNTER, "R3/MsiRemap", STAMUNIT_OCCURENCES, "Number of interrupt remap requests in R3.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMsiRemapRZ, STAMTYPE_COUNTER, "RZ/MsiRemap", STAMUNIT_OCCURENCES, "Number of interrupt remap requests in RZ.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMemReadR3,  STAMTYPE_COUNTER, "R3/MemRead",  STAMUNIT_OCCURENCES, "Number of memory read translation requests in R3.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMemReadRZ,  STAMTYPE_COUNTER, "RZ/MemRead",  STAMUNIT_OCCURENCES, "Number of memory read translation requests in RZ.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMemWriteR3,  STAMTYPE_COUNTER, "R3/MemWrite",  STAMUNIT_OCCURENCES, "Number of memory write translation requests in R3.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMemWriteRZ,  STAMTYPE_COUNTER, "RZ/MemWrite",  STAMUNIT_OCCURENCES, "Number of memory write translation requests in RZ.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMemBulkReadR3,  STAMTYPE_COUNTER, "R3/MemBulkRead",  STAMUNIT_OCCURENCES, "Number of memory bulk read translation requests in R3.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMemBulkReadRZ,  STAMTYPE_COUNTER, "RZ/MemBulkRead",  STAMUNIT_OCCURENCES, "Number of memory bulk read translation requests in RZ.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMemBulkWriteR3, STAMTYPE_COUNTER, "R3/MemBulkWrite", STAMUNIT_OCCURENCES, "Number of memory bulk write translation requests in R3.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatMemBulkWriteRZ, STAMTYPE_COUNTER, "RZ/MemBulkWrite", STAMUNIT_OCCURENCES, "Number of memory bulk write translation requests in RZ.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatCmd, STAMTYPE_COUNTER, "R3/Commands", STAMUNIT_OCCURENCES, "Number of commands processed (total).");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatCmdCompWait, STAMTYPE_COUNTER, "R3/Commands/CompWait", STAMUNIT_OCCURENCES, "Number of Completion Wait commands processed.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatCmdInvDte, STAMTYPE_COUNTER, "R3/Commands/InvDte", STAMUNIT_OCCURENCES, "Number of Invalidate DTE commands processed.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatCmdInvIommuPages, STAMTYPE_COUNTER, "R3/Commands/InvIommuPages", STAMUNIT_OCCURENCES, "Number of Invalidate IOMMU Pages commands processed.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatCmdInvIotlbPages, STAMTYPE_COUNTER, "R3/Commands/InvIotlbPages", STAMUNIT_OCCURENCES, "Number of Invalidate IOTLB Pages commands processed.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatCmdInvIntrTable, STAMTYPE_COUNTER, "R3/Commands/InvIntrTable", STAMUNIT_OCCURENCES, "Number of Invalidate Interrupt Table commands processed.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatCmdPrefIommuPages, STAMTYPE_COUNTER, "R3/Commands/PrefIommuPages", STAMUNIT_OCCURENCES, "Number of Prefetch IOMMU Pages commands processed.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatCmdCompletePprReq, STAMTYPE_COUNTER, "R3/Commands/CompletePprReq", STAMUNIT_OCCURENCES, "Number of Complete PPR Requests commands processed.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatCmdInvIommuAll, STAMTYPE_COUNTER, "R3/Commands/InvIommuAll", STAMUNIT_OCCURENCES, "Number of Invalidate IOMMU All commands processed.");


    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIotlbeCached, STAMTYPE_COUNTER, "IOTLB/Cached", STAMUNIT_OCCURENCES, "Number of IOTLB entries in the cache.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIotlbeLazyEvictReuse, STAMTYPE_COUNTER, "IOTLB/LazyEvictReuse", STAMUNIT_OCCURENCES, "Number of IOTLB entries reused after lazy eviction.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatProfDteLookup, STAMTYPE_PROFILE, "Profile/DteLookup", STAMUNIT_TICKS_PER_CALL, "Profiling DTE lookup.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatProfIotlbeLookup, STAMTYPE_PROFILE, "Profile/IotlbeLookup", STAMUNIT_TICKS_PER_CALL, "Profiling IOTLBE lookup.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatProfIrteLookup, STAMTYPE_PROFILE, "Profile/IrteLookup", STAMUNIT_TICKS_PER_CALL, "Profiling IRTE lookup.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatProfIrteCacheLookup, STAMTYPE_PROFILE, "Profile/IrteCacheLookup", STAMUNIT_TICKS_PER_CALL, "Profiling IRTE cache lookup.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatAccessCacheHit, STAMTYPE_COUNTER, "MemAccess/CacheHit", STAMUNIT_OCCURENCES, "Number of cache hits.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatAccessCacheMiss, STAMTYPE_COUNTER, "MemAccess/CacheMiss", STAMUNIT_OCCURENCES, "Number of cache misses.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatAccessCacheHitFull, STAMTYPE_COUNTER, "MemAccess/CacheHitFull", STAMUNIT_OCCURENCES, "Number of accesses that was entirely in the cache.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatAccessCacheNonContig, STAMTYPE_COUNTER, "MemAccess/CacheNonContig", STAMUNIT_OCCURENCES, "Number of cache accesses that resulted in non-contiguous translated regions.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatAccessCachePermDenied, STAMTYPE_COUNTER, "MemAccess/CacheAddrDenied", STAMUNIT_OCCURENCES, "Number of cache accesses that resulted in denied permissions.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatAccessDteNonContig, STAMTYPE_COUNTER, "MemAccess/DteNonContig", STAMUNIT_OCCURENCES, "Number of DTE accesses that resulted in non-contiguous translated regions.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatAccessDtePermDenied, STAMTYPE_COUNTER, "MemAccess/DtePermDenied", STAMUNIT_OCCURENCES, "Number of DTE accesses that resulted in denied permissions.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIntrCacheHit, STAMTYPE_COUNTER, "Interrupt/CacheHit", STAMUNIT_OCCURENCES, "Number of cache hits.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIntrCacheMiss, STAMTYPE_COUNTER, "Interrupt/CacheMiss", STAMUNIT_OCCURENCES, "Number of cache misses.");

    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatNonStdPageSize, STAMTYPE_COUNTER, "MemAccess/NonStdPageSize", STAMUNIT_OCCURENCES, "Number of non-standard page size translations.");
    PDMDevHlpSTAMRegister(pDevIns, &pThis->StatIopfs, STAMTYPE_COUNTER, "MemAccess/IOPFs", STAMUNIT_OCCURENCES, "Number of I/O page faults.");
# endif

    /*
     * Create the command thread and its event semaphore.
     */
    char szDevIommu[64];
    RT_ZERO(szDevIommu);
    RTStrPrintf(szDevIommu, sizeof(szDevIommu), "IOMMU-%u", iInstance);
    rc = PDMDevHlpThreadCreate(pDevIns, &pThisR3->pCmdThread, pThis, iommuAmdR3CmdThread, iommuAmdR3CmdThreadWakeUp,
                               0 /* cbStack */, RTTHREADTYPE_IO, szDevIommu);
    AssertLogRelRCReturn(rc, rc);

    rc = PDMDevHlpSUPSemEventCreate(pDevIns, &pThis->hEvtCmdThread);
    AssertLogRelRCReturn(rc, rc);

#ifdef IOMMU_WITH_DTE_CACHE
    /*
     * Initialize the critsect of the cache.
     */
    rc = PDMDevHlpCritSectInit(pDevIns, &pThis->CritSectCache, RT_SRC_POS, "IOMMUCache-#%u", pDevIns->iInstance);
    AssertLogRelRCReturn(rc, rc);

    /* Several places in this code relies on this basic assumption - assert it! */
    AssertCompile(RT_ELEMENTS(pThis->aDeviceIds) == RT_ELEMENTS(pThis->aDteCache));
#endif

#ifdef IOMMU_WITH_IOTLBE_CACHE
    /*
     * Allocate IOTLB entries.
     * This is allocated upfront since we expect a relatively small number of entries,
     * is more cache-line efficient and easier to track least recently used entries for
     * eviction when the cache is full. This also avoids unpredictable behavior during
     * the lifetime of the VM if the hyperheap gets full.
     */
    size_t const cbIotlbes = sizeof(IOTLBE) * IOMMU_IOTLBE_MAX;
    pThisR3->paIotlbes = (PIOTLBE)PDMDevHlpMMHeapAllocZ(pDevIns, cbIotlbes);
    if (!pThisR3->paIotlbes)
        return PDMDevHlpVMSetError(pDevIns, VERR_NO_MEMORY, RT_SRC_POS,
                                   N_("Failed to allocate %zu bytes from the hyperheap for the IOTLB cache."), cbIotlbes);
    RTListInit(&pThisR3->LstLruIotlbe);
    LogRel(("%s: Allocated %zu bytes from the hyperheap for the IOTLB cache\n", IOMMU_LOG_PFX, cbIotlbes));
#endif

    /*
     * Initialize read-only registers.
     * NOTE! Fields here must match their corresponding field in the ACPI tables.
     */
    /* Don't remove the commented lines below as it lets us see all features at a glance. */
    pThis->ExtFeat.u64 = 0;
    //pThis->ExtFeat.n.u1PrefetchSup           = 0;
    //pThis->ExtFeat.n.u1PprSup                = 0;
    //pThis->ExtFeat.n.u1X2ApicSup             = 0;
    //pThis->ExtFeat.n.u1NoExecuteSup          = 0;
    //pThis->ExtFeat.n.u1GstTranslateSup       = 0;
    pThis->ExtFeat.n.u1InvAllSup               = 1;
    //pThis->ExtFeat.n.u1GstVirtApicSup        = 0;
    pThis->ExtFeat.n.u1HwErrorSup              = 1;
    //pThis->ExtFeat.n.u1PerfCounterSup        = 0;
    AssertCompile((IOMMU_MAX_HOST_PT_LEVEL & 0x3) < 3);
    pThis->ExtFeat.n.u2HostAddrTranslateSize = (IOMMU_MAX_HOST_PT_LEVEL & 0x3);
    //pThis->ExtFeat.n.u2GstAddrTranslateSize  = 0;   /* Requires GstTranslateSup */
    //pThis->ExtFeat.n.u2GstCr3RootTblLevel    = 0;   /* Requires GstTranslateSup */
    //pThis->ExtFeat.n.u2SmiFilterSup          = 0;
    //pThis->ExtFeat.n.u3SmiFilterCount        = 0;
    //pThis->ExtFeat.n.u3GstVirtApicModeSup    = 0;   /* Requires GstVirtApicSup */
    //pThis->ExtFeat.n.u2DualPprLogSup         = 0;
    //pThis->ExtFeat.n.u2DualEvtLogSup         = 0;
    //pThis->ExtFeat.n.u5MaxPasidSup           = 0;   /* Requires GstTranslateSup */
    //pThis->ExtFeat.n.u1UserSupervisorSup     = 0;
    AssertCompile(IOMMU_MAX_DEV_TAB_SEGMENTS <= 3);
    pThis->ExtFeat.n.u2DevTabSegSup            = IOMMU_MAX_DEV_TAB_SEGMENTS;
    //pThis->ExtFeat.n.u1PprLogOverflowWarn    = 0;
    //pThis->ExtFeat.n.u1PprAutoRespSup        = 0;
    //pThis->ExtFeat.n.u2MarcSup               = 0;
    //pThis->ExtFeat.n.u1BlockStopMarkSup      = 0;
    //pThis->ExtFeat.n.u1PerfOptSup            = 0;
    pThis->ExtFeat.n.u1MsiCapMmioSup           = 1;
    //pThis->ExtFeat.n.u1GstIoSup              = 0;
    //pThis->ExtFeat.n.u1HostAccessSup         = 0;
    //pThis->ExtFeat.n.u1EnhancedPprSup        = 0;
    //pThis->ExtFeat.n.u1AttrForwardSup        = 0;
    //pThis->ExtFeat.n.u1HostDirtySup          = 0;
    //pThis->ExtFeat.n.u1InvIoTlbTypeSup       = 0;
    //pThis->ExtFeat.n.u1GstUpdateDisSup       = 0;
    //pThis->ExtFeat.n.u1ForcePhysDstSup       = 0;

    pThis->DevSpecificFeat.u64   = 0;
    pThis->DevSpecificFeat.n.u4RevMajor = IOMMU_DEVSPEC_FEAT_MAJOR_VERSION;
    pThis->DevSpecificFeat.n.u4RevMinor = IOMMU_DEVSPEC_FEAT_MINOR_VERSION;

    pThis->DevSpecificCtrl.u64 = 0;
    pThis->DevSpecificCtrl.n.u4RevMajor = IOMMU_DEVSPEC_CTRL_MAJOR_VERSION;
    pThis->DevSpecificCtrl.n.u4RevMinor = IOMMU_DEVSPEC_CTRL_MINOR_VERSION;

    pThis->DevSpecificStatus.u64 = 0;
    pThis->DevSpecificStatus.n.u4RevMajor = IOMMU_DEVSPEC_STATUS_MAJOR_VERSION;
    pThis->DevSpecificStatus.n.u4RevMinor = IOMMU_DEVSPEC_STATUS_MINOR_VERSION;

    pThis->MiscInfo.u64 = RT_MAKE_U64(uMiscInfoReg0, uMiscInfoReg1);

    pThis->RsvdReg = 0;

    /*
     * Initialize parts of the IOMMU state as it would during reset.
     * Also initializes non-zero initial values like IRTE cache keys.
     * Must be called -after- initializing PCI config. space registers.
     */
    iommuAmdR3Reset(pDevIns);

    LogRel(("%s: DSFX=%u.%u DSCX=%u.%u DSSX=%u.%u ExtFeat=%#RX64\n", IOMMU_LOG_PFX,
            pThis->DevSpecificFeat.n.u4RevMajor, pThis->DevSpecificFeat.n.u4RevMinor,
            pThis->DevSpecificCtrl.n.u4RevMajor, pThis->DevSpecificCtrl.n.u4RevMinor,
            pThis->DevSpecificStatus.n.u4RevMajor, pThis->DevSpecificStatus.n.u4RevMinor,
            pThis->ExtFeat.u64));
    return VINF_SUCCESS;
}

#else

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) iommuAmdRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PIOMMU   pThis   = PDMDEVINS_2_DATA(pDevIns, PIOMMU);
    PIOMMUCC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PIOMMUCC);
    pThisCC->CTX_SUFF(pDevIns) = pDevIns;

    /* We will use PDM's critical section (via helpers) for the IOMMU device. */
    int rc = PDMDevHlpSetDeviceCritSect(pDevIns, PDMDevHlpCritSectGetNop(pDevIns));
    AssertRCReturn(rc, rc);

    /* Set up the MMIO RZ handlers. */
    rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmio, iommuAmdMmioWrite, iommuAmdMmioRead, NULL /* pvUser */);
    AssertRCReturn(rc, rc);

    /* Set up the IOMMU RZ callbacks. */
    PDMIOMMUREGCC IommuReg;
    RT_ZERO(IommuReg);
    IommuReg.u32Version       = PDM_IOMMUREGCC_VERSION;
    IommuReg.idxIommu         = pThis->idxIommu;
    IommuReg.pfnMemAccess     = iommuAmdMemAccess;
    IommuReg.pfnMemBulkAccess = iommuAmdMemBulkAccess;
    IommuReg.pfnMsiRemap      = iommuAmdMsiRemap;
    IommuReg.u32TheEnd        = PDM_IOMMUREGCC_VERSION;
    rc = PDMDevHlpIommuSetUpContext(pDevIns, &IommuReg, &pThisCC->CTX_SUFF(pIommuHlp));
    AssertRCReturn(rc, rc);
    AssertPtrReturn(pThisCC->CTX_SUFF(pIommuHlp), VERR_IOMMU_IPE_1);
    AssertReturn(pThisCC->CTX_SUFF(pIommuHlp)->u32Version == CTX_MID(PDM_IOMMUHLP,_VERSION), VERR_VERSION_MISMATCH);
    AssertReturn(pThisCC->CTX_SUFF(pIommuHlp)->u32TheEnd  == CTX_MID(PDM_IOMMUHLP,_VERSION), VERR_VERSION_MISMATCH);
    AssertPtr(pThisCC->CTX_SUFF(pIommuHlp)->pfnLock);
    AssertPtr(pThisCC->CTX_SUFF(pIommuHlp)->pfnUnlock);
    AssertPtr(pThisCC->CTX_SUFF(pIommuHlp)->pfnLockIsOwner);
    AssertPtr(pThisCC->CTX_SUFF(pIommuHlp)->pfnSendMsi);
    return VINF_SUCCESS;
}
#endif


/**
 * The device registration structure.
 */
const PDMDEVREG g_DeviceIommuAmd =
{
    /* .u32Version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "iommu-amd",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE,
    /* .fClass = */                 PDM_DEVREG_CLASS_PCI_BUILTIN,
    /* .cMaxInstances = */          1,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(IOMMU),
    /* .cbInstanceCC = */           sizeof(IOMMUCC),
    /* .cbInstanceRC = */           sizeof(IOMMURC),
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        0,
    /* .pszDescription = */         "IOMMU (AMD)",
#if defined(IN_RING3)
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
    /* .pfnConstruct = */           iommuAmdR3Construct,
    /* .pfnDestruct = */            iommuAmdR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               iommuAmdR3Reset,
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
    /* .pfnConstruct = */           iommuAmdRZConstruct,
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
    /* .pfnConstruct = */           iommuAmdRZConstruct,
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

