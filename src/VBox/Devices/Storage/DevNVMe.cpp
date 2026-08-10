/* $Id: DevNVMe.cpp 114921 2026-08-10 12:13:36Z alexander.eichner@oracle.com $ */
/** @file
 * DevNVMe - Non Volatile Memory express (previous name: NVMHCI)
 */

/*
 * Copyright (C) 2015-2026 Oracle and/or its affiliates.
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

/** @page pg_dev_nvme   NVMe - Non Volatile Memory express Host Controller Interface Emulation.
 *  @todo Write something
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DEV_NVME
#include <VBox/pci.h>
#include <VBox/msi.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/pdmstorageifs.h>
#include <VBox/AssertGuest.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/assert.h>
#include <iprt/list.h>
#include <iprt/asm.h>
#ifdef IN_RING3
# include <iprt/uuid.h>
# include <iprt/critsect.h>
# include <iprt/semaphore.h>
# include <iprt/sg.h>
# include <iprt/time.h>
# include <iprt/mem.h>
# include <iprt/req.h>
#endif

#include "VBoxDD.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The current saved state version. */
#define NVME_SAVED_STATE_VERSION                     3
/** The saved state version before the NVMEIOREQ::cbPrp field was made 64-bit. */
#define NVME_SAVED_STATE_VERSION_PRE_PRP_SIZE_64BIT  2
/** The saved state version before the controller memory buffer feature was introduced. */
#define NVME_SAVED_STATE_VERSION_PRE_CMB             1

/** @name Valid ranges for certain configuration values.
 * @{ */
/** Minimum number of submission queues the controller supports. */
#define NVME_QUEUES_SUBMISSION_MIN        1
/** Maximum number of submission queues the controller supports. */
#define NVME_QUEUES_SUBMISSION_MAX        64
/** Minimum number of completion queues the controller supports. */
#define NVME_QUEUES_COMPLETION_MIN        1
/** Maximum number of completion queues the controller supports. */
#define NVME_QUEUES_COMPLETION_MAX        64
/** @} */

/** @name Default values for certain configuration values.
 * @{ */
/** Maximum number of submission queues the controller supports. */
#define NVME_QUEUES_SUBMISSION_MAX_DEF    32
/** Maximum number of completion queues the controller supports. */
#define NVME_QUEUES_COMPLETION_MAX_DEF    32
/** Maximum number of entries in one queue. */
#define NVME_QUEUE_ENTRIES_MAX_DEF        4096
/** The worst case time to wait for the controller to transition between ready and idle states.
 * (in 500ms units) */
#define NVME_TIMEOUT_DEF                  10
/** Maximum number of worker threads to create before assigning multiple queues to one worker. */
#define NVME_WORK_THREADS_MAX_DEF         2
/** Maximum number of namespaces. */
#define NVME_NAMESPACES_MAX_DEF           100
/** Maximum number of simultaneously outstanding "Asynchronous Event Request" commands. */
#define NVME_ASYNC_EVT_REQ_MAX_DEF        5
/** Maximum number of completion queue waiter entries before the completion queue is marked as overloaded
 * and associated submission queues should stop processing requests. */
#define NVME_COMP_QUEUES_WAITERS_MAX      3
/** Default serial number to report. */
#define NVME_SERIAL_NUMBER_DEF            "VB1234-56789"
/** Default model number to report. */
#define NVME_MODEL_NUMBER_DEF             "ORCL-VBOX-NVME-VER12"
/** Default firmware revision to report. */
#define NVME_FIRMWARE_REVISION_DEF        "1.0"
/** Default controller memory buffer size in bytes. */
#define NVME_CTRL_MEM_BUF_SIZE_DEF        (5 * _1M)
/** Default controller memory buffer granularity. */
#define NVME_CTRL_MEM_BUF_GRANULARITY_DEF "4KB"
/** @} */

/** @name PCI device related constants.
 * @{ */
/** The PCI vendor ID. */
#define NVME_PCI_VENDOR_ID             0x80ee
/** Where the MSI capability starts. */
#define NVME_PCI_MSI_CAP_OFS           0x80
/** Where the MSI-X capability starts. */
#define NVME_PCI_MSIX_CAP_OFS          (NVME_PCI_MSI_CAP_OFS + VBOX_MSI_CAP_SIZE_64)
/** The BAR for the MSI-X related functionality. */
#define NVME_PCI_MSIX_BAR              5
/** The BAR for the memory controller buffer. */
#define NVME_PCI_MEM_CTRL_BUF_BAR      3
/** @} */

/** Size of the NVMe common registers, including the reserved and command set specific part. */
#define NVME_HC_REG_SIZE               0x1000
/** Size of the NVMe Host controller registers defined by version 1.2 of the spec. */
#define NVME_HC_CTRL_REG_SIZE          0x40
/** Start of the queue doorbell registers. */
#define NVME_HC_DOORBELL_REG_OFF       0x1000
/** Maximum number of interrupt vectors. */
#define NVME_INTR_VEC_MAX              32
/** Error page entries stored by our emulation. */
#define NVME_LOG_PAGE_ERROR_ENTRIES    1
/** Maximum serial number length */
#define NVME_SERIAL_NUMBER_LENGTH      20
/** Maximum model number length */
#define NVME_MODEL_NUMBER_LENGTH       40
/** Maximum firmware length */
#define NVME_FIRMWARE_REVISION_LENGTH   8

/** Admin queue identifier. */
#define NVME_ADM_QUEUE_ID               0

/** NVMe Register Bits. */

/** @name Controller Capabilities (CAP) register
 * @{ */
/** Maximum supported memory page size. */
#define NVME_CAP_MPSMAX(a_cPgSizeMax) (((a_cPgSizeMax) & UINT64_C(0xf)) << 52)
/** Minimum supported memory page size. */
#define NVME_CAP_MPSMIN(a_cPgSizeMin) (((a_cPgSizeMin) & UINT64_C(0xf)) << 48)
/** NVM command set supported. */
#define NVME_CAP_CCS_NVM              RT_BIT_64(37)
/** NVM Subsystem Reset supported. */
#define NMVE_CAP_NSSRS                RT_BIT_64(36)
/** Doorbell Stride. */
#define NVME_CAP_DSTRD(a_cDrBlStrd)   (((a_cDrBlStrd) & UINT64_C(0xf)) << 32)
/** Worst case timeout host software shall wait for CSTS.RDY transition. */
#define NVME_CAP_TO(a_cTimeout)       (((a_cTimeout) & UINT64_C(0xff)) << 24)
/** Arbitration mechanism: Weighted Round Robin with Urgen Priority Class. */
#define NVME_CAP_AMS_WRR              RT_BIT_64(17)
/** Arbitration mechanism: Vendor specific. */
#define NVME_CAP_AMS_VEND_SPEC        RT_BIT_64(18)
/** Contiguous Queues Required. */
#define NVME_CAP_CQR                  RT_BIT_64(16)
/** Maximum Queue Entries Supported. */
#define NVME_CAP_MQES(a_cQueueEntMax) ((((a_cQueueEntMax) - 1) & UINT64_C(0xffff)))
/** @} */

/** @name Version (VS) register
 * @{ */
/** Major version part of the register. */
#define NVME_VS_MJR(a_iVsMajor)       (((a_iVsMajor) & 0xffff) << 16)
/** Minor version part of the register. */
#define NVME_VS_MNR(a_iVsMinor)       (((a_iVsMinor) & 0xff) << 8)
/** @} */

/** @name Controller Configuration (CC) register
 * @{ */
/** I/O Completion Queue Entry Size. */
#define NVME_CC_IOCQES_SET(a_cQueueEntSize) (((a_cQueueEntSize) & 0xf) << 20)
#define NVME_CC_IOCQES_RET(a_u32Reg)        (((a_u32Reg) >> 20) & 0xf)
/** I/O Submission Queue Entry Size. */
#define NVME_CC_IOSQES_SET(a_cQueueEntSize) (((a_cQueueEntSize) & 0xf) << 16)
#define NVME_CC_IOSQES_RET(a_u32Reg)        (((a_u32Reg) >> 16) & 0xf)
/** Shutdown Notification. */
#define NVME_CC_SHN_SET(a_u2ShtDwnNtfr)     (((a_u2ShtDwnNtfr) & 0x3) << 14)
#define NVME_CC_SHN_RET(a_u32Reg)           (((a_u32Reg) >> 14) & 0x3)
/** Shutdown Notifier: No notification. */
#define NVME_CC_SHN_NO_NOTIFICATION         0x0
/** Shutdown Notifier: Normal shutdown notification. */
#define NVME_CC_SHN_NORMAL                  0x1
/** Shutdown Notifier: Abrupt shutdown notification. */
#define NVME_CC_SHN_ABRUPT                  0x2
/** Shutdown Notifier: Reserved. */
#define NVME_CC_SHN_RESERVED                0x3
/** Arbitration Mechanism Selected. */
#define NVME_CC_AMS_SET(a_u3Ams)            (((a_u3Ams) & 0x7) << 11)
#define NVME_CC_AMS_RET(a_u32Reg)           (((a_u32Reg) >> 11) & 0x7)
/** Arbitration Mechanism Selected: Round Robin. */
#define NVME_CC_AMS_RR                      0x0
/** Arbitration Mechanism Selected: Weighted Round Robin with Urgent Priority Class. */
#define NVME_CC_AMS_WRR                     0x1
/** Arbitration Mechanism Selected: Vendor Specific. */
#define NVME_CC_AMS_VEND_SPEC               0x7
/** Memory Page Size. */
#define NVME_CC_MPS_SET(a_cPgSize)          (((a_cPgSize - 12) & 0xf) << 7)
#define NVME_CC_MPS_RET(a_u32Reg)           ((((a_u32Reg) >> 7) & 0xf) + 12)
/** I/O Command Set Selected. */
#define NVME_CC_CSS_SET(a_u3CmdSet)         (((a_u3CmdSet) & 0x7) << 4)
#define NVME_CC_CSS_RET(a_u32Reg)           (((a_u32Reg) >> 4) & 0x7)
/** I/O Command Set Selected: NVM Command Set. */
#define NVME_CC_CSS_NVM                     0x0
/** Enable. */
#define NVME_CC_EN                          RT_BIT_32(0)
/** @} */

/** @name Controller Status (CSTS) register
 * @{ */
/** Processing paused. */
#define NVME_CSTS_PP                        RT_BIT_32(5)
/** NVM Subsystem Reset Occurred. */
#define NVME_CSTS_NSRRO                     RT_BIT_32(4)
/** Shutdown Status. */
#define NVME_CSTS_SHST_SET(a_u2ShtDwnSts)   (((a_u2ShtDwnSts) & 0x3) << 2)
/** Shutdown status: Normal operation. */
#define NVME_CSTS_SHST_NORMAL               0x0
/** Shutdown status: Shutdown processing occurring. */
#define NVME_CSTS_SHST_OCCURRING            0x1
/** Shutdown status: Shutdown processing complete. */
#define NVME_CSTS_SHST_COMPLETE             0x2
/** Shutdown status: Reserved. */
#define NVME_CSTS_SHST_RESERVED             0x3
/** Controller Fatal Status. */
#define NVME_CSTS_CFS                       RT_BIT_32(1)
/** Ready. */
#define NVME_CSTS_RDY                       RT_BIT_32(0)
/** @} */

/** @name NVM Subsystem Reset (NSSR) register
 * @{ */
/** NVM Subsystem Reset Control magic to initiate a reset ("NVMe"). */
#define NVME_NSSR_NSSRC_MAGIC               UINT32_C(0x4e564d65)
/** @} */

/** @name Admin Queue Attributes (AQA) register
 * @{ */
/** Admin Completion Queue Size. */
#define NVME_AQA_ACQS_SET(a_cQueueSize)     (((a_cQueueSize) & 0xfff) << 16)
#define NVME_AQA_ACQS_RET(a_u32Reg)         (((a_u32Reg) >> 16) & 0xfff)
/** Admin Submission Queue Size. */
#define NVME_AQA_ASQS_SET(a_cQueueSize)     ((a_cQueueSize) & 0xfff)
#define NVME_AQA_ASQS_RET(a_u32Reg)         ((a_u32Reg) & 0xfff)
/** @} */

/** @name Admin Submission Queue Base Address (ASQ) register
 * @{ */
/** Admin Submission Queue Base. */
#define NVME_ASQ_ASQB_RET(a_u64Reg)         ((a_u64Reg) & UINT64_C(0xfffffffffffff000))
/** @} */

/** @name Admin Completion Queue Base Address (ACQ) register
 * @{ */
/** Admin Completion Queue Base. */
#define NVME_ACQ_ACQB_RET(a_u64Reg)         ((a_u64Reg) & UINT64_C(0xfffffffffffff000))
/** @} */

/** @name Controller Memory Buffer Location (CMBLOC) register
 * @{ */
/** Offset. */
#define NVME_CMBLOC_OFST(a_Off)             (((a_Off) & UINT32_C(0xfffff)) << 12)
/** Base Indicator Register. */
#define NVME_CMBLOC_BIR(a_BaseIndicator)    ((a_BaseIndicator) & 0x7)
/** @} */

/** @name Controller Memory Buffer Size (CMBSZ) register
 * @{ */
/** Size */
#define NVME_CMBSZ_SZ(a_Size)               (((a_Size) & UINT32_C(0xfffff)) << 12)
/** Size Units. */
#define NVME_CMBSZ_SZU(a_Unit)              (((a_Unit) & 0xf) << 8)
/** Size Unit: 4KB */
#define NVME_CMBSZ_SZU_4K                   0x0
/** Size Unit: 64KB */
#define NVME_CMBSZ_SZU_64K                  0x1
/** Size Unit: 1MB */
#define NVME_CMBSZ_SZU_1M                   0x2
/** Size Unit: 16MB */
#define NVME_CMBSZ_SZU_16M                  0x3
/** Size Unit: 256MB */
#define NVME_CMBSZ_SZU_256M                 0x4
/** Size Unit: 4GB */
#define NVME_CMBSZ_SZU_4G                   0x5
/** Size Unit: 64GB */
#define NVME_CMBSZ_SZU_64G                  0x6
/** Write Data Support, bit index. */
#define NVME_CMBSZ_WDS_BIT_IDX              4
/** Write Data Support, bit. */
#define NVME_CMBSZ_WDS                      RT_BIT_32(NVME_CMBSZ_WDS_BIT_IDX)
/** Read Data Support, bit index. */
#define NVME_CMBSZ_RDS_BIT_IDX              3
/** Read Data Support, bit. */
#define NVME_CMBSZ_RDS                      RT_BIT_32(NVME_CMBSZ_RDS_BIT_IDX)
/** PRP SGL List Support, bit index. */
#define NVME_CMBSZ_LISTS_BIT_IDX            2
/** PRP SGL List Support, bit. */
#define NVME_CMBSZ_LISTS                    RT_BIT_32(NVME_CMBSZ_LISTS_BIT_IDX)
/** Completion Queue Support, bit index. */
#define NVME_CMBSZ_CQS_BIT_IDX              1
/** Completion Queue Support, bit. */
#define NVME_CMBSZ_CQS                      RT_BIT_32(NVME_CMBSZ_CQS_BIT_IDX)
/** Submission Queue Support, bit index. */
#define NVME_CMBSZ_SQS_BIT_IDX              0
/** Submission Queue Support, bit. */
#define NVME_CMBSZ_SQS                      RT_BIT_32(NVME_CMBSZ_SQS_BIT_IDX)
/** The maximum defined supported bit index. */
#define NVME_CMBSZ_SUPP_BIT_IDX_MAX         NVME_CMBSZ_WDS_BIT_IDX
/** @} */

/** @name Submission Queue Tail Doorbell (SQTDBL) register
 * @{ */
/** Submission Queue Tail. */
#define NVME_SQTDBL_SQT_SET(a_u16Tail)      ((a_u16Tail) & UINT16_C(0xffff))
#define NVME_SQTDBL_SQT_RET(a_u32Reg)       ((a_u32Reg) & UINT16_C(0xffff))
/** @} */

/** @name Completion Queue Head Doorbell (CQHDBL) register
 * @{ */
/** Submission Queue Tail. */
#define NVME_CQHDBL_CQG_SET(a_u16Head)      ((a_u16Head) & UINT16_C(0xffff))
#define NVME_CQHDBL_CQH_RET(a_u32Reg)       ((a_u32Reg) & UINT16_C(0xffff))
/** @} */

/** @name NVMe data structures defined in the specification (chapter 4).
 * @{ */
/**
 * NVMe SGL descriptor.
 */
#pragma pack(1)
typedef struct NVMESGL
{
    /** Descriptor type specific data. */
    union
    {
        /** Data block descriptor. */
        struct
        {
            /** Starting address of the data block. */
            RTGCPHYS                GCPhysAddress;
            /** Size of the data block in bytes. */
            uint32_t                cbBlock;
            /** Reserved. */
            uint8_t                 au8Rsvd[3];
        } DataBlock;
        /** Bit Bucket descriptor. */
        struct
        {
            /** Reserved. */
            uint64_t                u64Rsvd;
            /** Length of the bit bucket. */
            uint32_t                cbBucket;
            /** Reserved. */
            uint8_t                 au8Rsvd[3];
        } BitBucket;
        /** Segment/Last segment descriptor. */
        struct
        {
            /** Starting address of the next SGL segment. */
            RTGCPHYS                GCPhysAddress;
            /** Length of the SGL segment. */
            uint32_t                cbSglSegment;
            /** Reserved. */
            uint8_t                 au8Rsvd[3];
        } SegmentLast;
    }u;
    /** SGL Identifier. */
    uint8_t                         u8SglId;
} NVMESGL;
#pragma pack()
/** Pointer to a SGL descriptor. */
typedef NVMESGL *PNVMESGL;

AssertCompileSize(NVMESGL, 16);

/** Return the SGL descriptor type. */
#define NVME_SGL_DESC_ID_GET_TYPE(a_u8Id) (((a_u8Id) & 0xf0 ) >> 4)
/** SGL descriptor type: Data Block descriptor. */
#define NVME_SGL_DESC_TYPE_DATA_BLOCK   0x0
/** SGL descriptor type: Bit Bucket descriptor. */
#define NVME_SGL_DESC_TYPE_BIT_BUCKET   0x1
/** SGL descriptor type: Segment descriptor. */
#define NVME_SGL_DESC_TYPE_SEGMENT      0x2
/** SGL descriptor type: Last Segment descriptor. */
#define NVME_SGL_DESC_TYPE_LAST_SEGMENT 0x3
/** Return SGL descriptor type specific part. */
#define NVME_SGL_DESC_ID_GET_DESC_SPEC(a_u8Id) ((a_u8Id) & 0xf)


/**
 * Command header format.
 */
typedef struct NVMECMDHDR
{
    /** Command opcode. */
    uint8_t                         u8Opc;
    /** Field indicating fused operations. */
    uint8_t                         u2Fuse  : 2;
    /** Reserved */
    uint8_t                         u4Rsvd0 : 4;
    /** Field indicating PRP or SGL usage. */
    uint8_t                         u2Psdt  : 2;
    /** Command identifier. */
    uint16_t                        u16Cid;
} NVMECMDHDR;
AssertCompileSize(NVMECMDHDR, 4);
/** Pointer to a NVMe command header. */
typedef NVMECMDHDR *PNVMECMDHDR;


/** Fused operation: Normal operation. */
#define NVME_CMD_HDR_FUSE_NORMAL                       0x0
/** Fused operation: Fused operation, first command. */
#define NVME_CMD_HDR_FUSE_FIRST                        0x1
/** Fused operation: Fused operation, second command. */
#define NVME_CMD_HDR_FUSE_SECOND                       0x2
/** Fused operation: Reserved. */
#define NVME_CMD_HDR_FUSE_RSVD                         0x3
/** Returns whether a command is normal. */
#define NVME_CMD_HDR_FUSE_IS_NORMAL_OP(a_u2Fuse)       ((a_u2Fuse) == NVME_CMD_HDR_FUSE_NORMAL)
/** Returns whether a command is fused. */
#define NVME_CMD_HDR_FUSE_IS_FUSED_OP(a_u2Fuse)        (   (a_u2Fuse) == NVME_CMD_HDR_FUSE_FIRST \
                                                        || (a_u2Fuse) == NVME_CMD_HDR_FUSE_SECOND)

/** PRPs are used for this transfer. */
#define NVME_CMD_HDR_PSDT_PRP                          0x0
/** SGLs are used for this transfer. */
#define NVME_CMD_HDR_PSDT_SGL                          0x1
/** SGLs are used for this transfer. */
#define NVME_CMD_HDR_PSDT_SGL2                         0x2
/** Reserved. */
#define NVME_CMD_HDR_PSDT_RSVD                         0x3
/** Returns whether the command uses SGLs for data transfers. */
#define NVME_CMD_HDR_PSDT_IS_SGL(a_u2Psdt)             (   (a_u2Psdt) == NVME_CMD_HDR_PSDT_SGL \
                                                        || (a_u2Psdt) == NVME_CMD_HDR_PSDT_SGL2)
/** Returns whether the command uses PRPs for data transfers. */
#define NVME_CMD_HDR_PSDT_IS_PRP(a_u2Psdt)             ((a_u2Psdt) == NVME_CMD_HDR_PSDT_PRP)

/** Namespace identifier type. */
typedef uint32_t NVMENSID;
/** Pointer to a namespace identifier. */
typedef NVMENSID *PNVMENSID;

/** NVME PRP entry. */
typedef uint64_t NVMEPRP;
/** Pointer to a PRP entry. */
typedef NVMEPRP *PNVMEPRP;

/** Returns the page base address of the given PRP entry. */
#define NVME_PRP_GET_PAGE_ADDR(a_Prp, a_Mps)     ((a_Prp) & ~(RT_BIT_64(a_Mps) - 1))
/** Returns the offset of the given PRP entry. */
#define NVME_PRP_GET_PAGE_OFF(a_Prp, a_Mps)      ((a_Prp) & (RT_BIT_64(a_Mps) - 1))
/** Returns the starting guest physical address for the PRP entry. */
#define NVME_PRP_TO_GCPHYS(a_Prp, a_Mps)         ((RTGCPHYS)(NVME_PRP_GET_PAGE_ADDR(a_Prp, a_Mps) + NVME_PRP_GET_PAGE_OFF(a_Prp, a_Mps)))
/** Returns the adjacent page base address of the given PRP. */
#define NVME_PRP_GET_PAGE_ADDR_ADJ(a_Prp, a_Mps) (NVME_PRP_GET_PAGE_ADDR(a_Prp, a_Mps) + RT_BIT_64(a_Mps))
/** Returns whether a given PRP entry is page aligned. */
#define NVME_PRP_IS_PAGE_ALIGNED(a_Prp, a_Mps)   (NVME_PRP_GET_PAGE_OFF(a_Prp, a_Mps) == 0)
/** Returns the amount of data available in the given PRP entry. */
#define NVME_PRP_GET_SIZE(a_Prp, a_Mps)          (  NVME_PRP_GET_PAGE_ADDR_ADJ(a_Prp, a_Mps) \
                                                  - NVME_PRP_GET_PAGE_ADDR(a_Prp, a_Mps) \
                                                  - NVME_PRP_GET_PAGE_OFF(a_Prp, a_Mps))

/**
 * Admin command set - command format.
 */
#pragma pack(1) /* only needed for u.Field.u.SetFeatures */
typedef struct NVMECMDADM
{
    /** View dependent data. */
    union
    {
        /** Field view. */
        struct
        {
            /** Command header. */
            NVMECMDHDR              Hdr;
            /** Namespace identifier this command applies to. */
            NVMENSID                uNsId;
            /** Reserved */
            uint8_t                 au8Rsvd[8];
            /** Metadata pointer. */
            RTGCPHYS64              GCPhysMetaPtr;
            /** PRP Entry 1. */
            NVMEPRP                 Prp1;
            /** PRP Entry 2. */
            NVMEPRP                 Prp2;
            /** Command dependent fields. */
            union
            {
                /** Abort. */
                struct
                {
                    /** Submission queue identifier. */
                    uint16_t        u16SqId;
                    /** Command identifier. */
                    uint16_t        u16Cid;
                } Abort;
                /** Asynchronous event request - has no further fields. */
                /** Create I/O completion queue. */
                struct
                {
                    /** Queue identifier. */
                    uint16_t        u16CqId;
                    /** Number of entries in the queue. */
                    uint16_t        u16CqSize;
                    /** Interrupts enabled and physically contiguous bits. */
                    uint16_t        u16Flags;
                    /** Interrupt vector. */
                    uint16_t        u16IntrVec;
                } CreateIoCq;
                /** Create I/O submission queue. */
                struct
                {
                    /** Queue identifier. */
                    uint16_t        u16SqId;
                    /** Number of entries in the queue. */
                    uint16_t        u16SqSize;
                    /** Priority and physically contiguous bits. */
                    uint16_t        u16Flags;
                    /** Completion queue identifier the created submission queue is associated with. */
                    uint16_t        u16CqId;
                } CreateIoSq;
                /** Delete I/O completion queue. */
                struct
                {
                    /** Queue identifier. */
                    uint16_t        u16CqId;
                } DeleteIoCq;
                /** Delete I/O submission queue. */
                struct
                {
                    /** Queue identifier. */
                    uint16_t        u16SqId;
                } DeleteIoSq;
                /** Get features. */
                struct
                {
                    /** Feature identifier. */
                    uint8_t         u8FeatId;
                    /** Select bits. */
                    uint8_t         u8Sel;
                } GetFeatures;
                /** Get log page. */
                struct
                {
                    /** Log page identifier. */
                    uint8_t         u8LogPgId;
                    /** Reserved. */
                    uint8_t         u8Rsvd;
                    /** Number of dwords. */
                    uint16_t        u16NumDwords;
                } GetLogPage;
                /** Identify. */
                struct
                {
                    /** Controller or namespace structure. */
                    uint8_t         u8Cns;
                    /** Reserved. */
                    uint8_t         u8Rsvd;
                    /** Controller identifier. */
                    uint16_t        u16CntId;
                } Identify;
                /** Set features. */
                struct
                {
                    /** Feature identifier. */
                    uint8_t         u8FeatId;
                    /** Reserved. */
                    uint16_t        u16Rsvd;
                    /** Save bit */
                    uint8_t         u8Save;
                } SetFeatures;
            } u;
        } Field;
        /** DWord view. */
        uint32_t                    au32[16];
        /** Byte view. */
        uint8_t                     au8[64];
    } u;
} NVMECMDADM;
#pragma pack()
AssertCompileSize(NVMECMDADM, 64);
/** Pointer to a NVMe admin command. */
typedef NVMECMDADM *PNVMECMDADM;

/** @} */ /** @todo probably incorrect chapter 4 group termination, but you can't nest them... */

/** @name Admin command specific defines.
 * @{ */
/** Opcode: Delete I/O submission queue. */
#define NVME_CMD_ADM_OPC_SQ_DEL                        0x00
/** Opcode: Create I/O submission queue. */
#define NVME_CMD_ADM_OPC_SQ_CREATE                     0x01
/** Opcode: Get log page. */
#define NVME_CMD_ADM_OPC_GET_LOG_PG                    0x02
/** Opcode: Delete I/O completion queue. */
#define NVME_CMD_ADM_OPC_CQ_DEL                        0x04
/** Opcode: Create I/O completion queue. */
#define NVME_CMD_ADM_OPC_CQ_CREATE                     0x05
/** Opcode: Identify. */
#define NVME_CMD_ADM_OPC_IDENTIFY                      0x06
/** Opcode: Abort. */
#define NVME_CMD_ADM_OPC_ABORT                         0x08
/** Opcode: Set features. */
#define NVME_CMD_ADM_OPC_SET_FEAT                      0x09
/** Opcode: Get features. */
#define NVME_CMD_ADM_OPC_GET_FEAT                      0x0a
/** Opcode: Asynchronous event requested. */
#define NVME_CMD_ADM_OPC_ASYNC_EVT_REQ                 0x0c
/** Opcode: Namespace management. */
#define NVME_CMD_ADM_OPC_NS_MGMT                       0x0d
/** Opcode: Firmware commit. */
#define NVME_CMD_ADM_OPC_FW_COMMIT                     0x10
/** Opcode: Firmware image download. */
#define NVME_CMD_ADM_OPC_FW_IMG_DWNLD                  0x11
/** Opcode: Namespace attachment. */
#define NVME_CMD_ADM_OPC_NS_ATTACHMENT                 0x15

/** Create I/O completion queue, physically contiguous bit. */
#define NVME_CMD_ADM_CREATE_IO_CQ_PC                   RT_BIT(0)
/** Create I/O completion queue, interrupts enabled. */
#define NVME_CMD_ADM_CREATE_IO_CQ_IEN                  RT_BIT(1)

/** Create I/O submission queue, physically contiguous bit. */
#define NVME_CMD_ADM_CREATE_IO_SQ_PC                   RT_BIT(0)
/** Create I/O submission queue, set queue priority. */
#define NVME_CMD_ADM_CREATE_IO_SQ_PRIO_SET(a_u2Prio)   (((a_u2Prio) & 0x3) << 1)
/** Create I/O submission queue, get queue priority. */
#define NVME_CMD_ADM_CREATE_IO_SQ_PRIO_GET(a_u16Flags) (((a_u16Flags) >> 1) & 0x3)
/** Create I/O submission queue, queue priority: Urgent */
#define NVME_CMD_ADM_CREATE_IO_SQ_PRIO_URGENT          0x0
/** Create I/O submission queue, queue priority: High */
#define NVME_CMD_ADM_CREATE_IO_SQ_PRIO_HIGH            0x1
/** Create I/O submission queue, queue priority: Medium */
#define NVME_CMD_ADM_CREATE_IO_SQ_PRIO_MEDIUM          0x2
/** Create I/O submission queue, queue priority: Low */
#define NVME_CMD_ADM_CREATE_IO_SQ_PRIO_LOW             0x3

/** Get features, select bits */
#define NVME_CMD_ADM_GET_FEAT_SELECT_GET(a_u8Flags)    ((a_u8Flags) & 0x7)
/** Get features, select: Current */
/** Get features, select: Default */
#define NVME_CMD_ADM_GET_FEAT_SELECT_DEFAULT           0x1
/** Get features, select: Saved */
#define NVME_CMD_ADM_GET_FEAT_SELECT_SAVED             0x2
/** Get features, select: Supported capabilities */
#define NVME_CMD_ADM_GET_FEAT_SELECT_SUPPORTED_CAPS    0x3

/** Get log page, number of DWords. */
#define NVME_CMD_ADM_GET_LOG_PG_NUMD_GET(a_u16NumDwords) ((a_u16NumDwords) & 0xfff)

/** Identify, Controller or namespace structure: Identify namespace structure. */
#define NVME_CMD_ADM_IDENTIFY_CNS_IDENTIFY_NS          0x00
/** Identify, Controller or namespace structure: Identify controller structure. */
#define NVME_CMD_ADM_IDENTIFY_CNS_IDENTIFY_CTL         0x01
/** Identify, Controller or namespace structure: Active namespace IDs. */
#define NVME_CMD_ADM_IDENTIFY_CNS_ACTIVE_NS            0x02
/** Identify, Controller or namespace structure: Present namespace IDs. */
#define NVME_CMD_ADM_IDENTIFY_CNS_PRESENT_NS           0x10
/** Identify, Controller or namespace structure: Identify namespace structure. */
#define NVME_CMD_ADM_IDENTIFY_CNS_IDENTIFY_NS2         0x11
/** Identify, Controller or namespace structure: Controller list attached to specified namespace. */
#define NVME_CMD_ADM_IDENTIFY_CNS_CTL_LST_NS           0x12
/** Identify, Controller or namespace structure: Controller list. */
#define NVME_CMD_ADM_IDENTIFY_CNS_CTL_LST              0x13

/** Set features, save bit. */
#define NVME_CMD_ADM_SET_FEAT_SV                       RT_BIT(7)
/** @} */

/**
 * @name Structures returned by the "Identify" command.
 * @{ */
/**
 * Power state descriptor.
 */
typedef struct NVMEPWRSTATESDESC
{
    /** Maximum power. */
    uint16_t                        u16PwrMax;
    /** Reserved. */
    uint8_t                         u8Rsvd0;
    /** Maximum power scale and non operational state bits. */
    uint8_t                         u8MaxPwrScaleAndOpState;
    /** Entry latency. */
    uint32_t                        u32EntryLat;
    /** Exit latency. */
    uint32_t                        u32ExitLat;
    /** Relative read throughput. */
    uint8_t                         u8ReadThroughputRel;
    /** Relative read latency. */
    uint8_t                         u8ReadLatencyRel;
    /** Relative write throughput. */
    uint8_t                         u8WriteThroughputRel;
    /** Relative write latency. */
    uint8_t                         u8WriteLatencyRel;
    /** Idle power. */
    uint16_t                        u16PwrIdle;
    /** Idle power scale. */
    uint8_t                         u8PwrScaleIdle;
    /** Reserved. */
    uint8_t                         u8Rsvd1;
    /** Active power. */
    uint16_t                        u16PwrActive;
    /** Active power workload and scale. */
    uint8_t                         u8PwrLoadScaleActive;
    /** Reserved. */
    uint8_t                         au8Rsvd2[9];
} NVMEPWRSTATESDESC;
AssertCompileSize(NVMEPWRSTATESDESC, 32);
/** Pointer to a power state descriptor. */
typedef NVMEPWRSTATESDESC *PNVMEPWRSTATESDESC;


/**
 * Identify Controller Data Structure.
 */
typedef struct NVMEIDENTIFYCTRL
{
    /** PCI vendor ID. */
    uint16_t                        u16VendorId;
    /** PCI Subsystem vendor ID. */
    uint16_t                        u16SubSysVendorId;
    /** Serial number as ASCII string. */
    uint8_t                         achSerialNumber[20];
    /** Model number as ASCII string. */
    uint8_t                         achModelNumber[40];
    /** Firmware revision as ASCII string. */
    uint8_t                         achFirmwareRevision[8];
    /** Recommend Arbitration Burst. */
    uint8_t                         u8RecomdArbitBurst;
    /** IEEE OUI Identifier. */
    uint8_t                         au8IeeeOui[3];
    /** Multipath nd namespace sharing capabilities. */
    uint8_t                         u8MultPathIoNsSharing;
    /** Maximum data transfer size. */
    uint8_t                         u8DataXferSzMax;
    /** Controller ID. */
    uint16_t                        u16CtrlId;
    /** Version. */
    uint32_t                        u32Version;
    /** RTD3 resume latency. */
    uint32_t                        u32Rtd3ResumeLat;
    /** RTD3 entry latency. */
    uint32_t                        u32Rtd3EntryLat;
    /** Optional Asynchronous events supported. */
    uint32_t                        u32OptAsyncEvtsSupported;
    /** Reserved */
    uint8_t                         au8Rsvd0[144];
    /** NVMe management interface specific settings, uninteresting for us. */
    uint8_t                         au8IfMgmtSettings[16];
    /** Optional admin command support. */
    uint16_t                        u16OptAdmCmdSupported;
    /** Abort command limit .*/
    uint8_t                         u8AbrtCmdLimit;
    /** Asynchronous event request limit. */
    uint8_t                         u8AsyncEvtReqLimit;
    /** Firmware updates. */
    uint8_t                         u8FwUpdates;
    /** Log page attributes. */
    uint8_t                         u8LgPgAttr;
    /** Error log page entries. */
    uint8_t                         u8ErrLgPgEnt;
    /** Number of supported power states. */
    uint8_t                         u8PwrStatesSupported;
    /** Admin vendor specific command configuration. */
    uint8_t                         u8AdmVendorSpecCmdCfg;
    /** Autonomous power state transition attributes. */
    uint8_t                         u8AutPwrStateTransAttr;
    /** Warning composite temperature threshold. */
    uint16_t                        u16WarnCompTempThreshold;
    /** Critical composite temperature threshold. */
    uint16_t                        u16CritCompTempThreshold;
    /** Maximum time for firmware activation. */
    uint16_t                        u16MaxTimeFwAct;
    /** Host memory buffer preferred size. */
    uint32_t                        u32HostMemBufPrefSz;
    /** Host memory buffer minimum size. */
    uint32_t                        u32HostMemBufMinSz;
    /** Total NVM capacity. */
    uint8_t                         au8NvmCapTotal[16];
    /** Unallocated NVM capacity. */
    uint8_t                         au8NvmCapUnallocated[16];
    /** Replay protected memory block support. */
    uint32_t                        u32ReplayProtMemBlockSupport;
    /** Reserved. */
    uint8_t                         au8Rsvd1[196];
    /** Submission queue entry size. */
    uint8_t                         u8SubmQueueEntSz;
    /** Completion queue entry size. */
    uint8_t                         u8CompQueueEntSz;
    /** Reserved */
    uint16_t                        u16Rsvd2;
    /** Number of namespaces. */
    uint32_t                        u32Namespaces;
    /** Optional NVM command support. */
    uint16_t                        u16OptNvmCmdSupported;
    /** Fused opertion support. */
    uint16_t                        u16FusedOpSupported;
    /** Format NVM attributes. */
    uint8_t                         u8NvmFmtAttr;
    /** Volatile write cache. */
    uint8_t                         u8VolWriteCache;
    /** Atomic write unit normal. */
    uint16_t                        u16AtomicWriteUnitNormal;
    /** Atomic write unit power fail. */
    uint16_t                        u16AtomicWriteUnitPwrFail;
    /** NVM vendor specific command configuration. */
    uint8_t                         u8NvmVendorSpecCmdCfg;
    /** Reserved. */
    uint8_t                         u8Rsvd3;
    /** Atomic Compare & Write unit. */
    uint16_t                        u16AtomicWriteCmpUnit;
    /** Reserved. */
    uint16_t                        u16Rsvd4;
    /** SGL support. */
    uint32_t                        u32SglSupported;
    /** Reserved. */
    uint8_t                         au8Rsvd5[164];
    /** Reserved I/O command set attributes. */
    uint8_t                         au8Rsvd6[1344];
    /** Power states descriptors. */
    NVMEPWRSTATESDESC               aPwrStateDesc[32];
    /** Vendor specific data. */
    uint8_t                         au8VendorSpec[1024];
} NVMEIDENTIFYCTRL;
/** Pointer to a Identify Controller Data structure. */
typedef NVMEIDENTIFYCTRL *PNVMEIDENTIFYCTRL;

AssertCompileMemberOffset(NVMEIDENTIFYCTRL, u16OptAdmCmdSupported, 256);
AssertCompileMemberOffset(NVMEIDENTIFYCTRL, u8SubmQueueEntSz, 512);
AssertCompileMemberOffset(NVMEIDENTIFYCTRL, u32Namespaces, 516);
AssertCompileSize(NVMEIDENTIFYCTRL, _4K);

/** @} */ /** @todo probably incorrect "Identify" group termination, but you can't nest them... */

/**
 * LBA format descriptor.
 */
typedef struct NVMELBAFMTDESC
{
    /** Metadata size. */
    uint16_t                        u16MetadataSz;
    /** LBA data size, power of two. */
    uint8_t                         u8LbaSz;
    /** Relative performance. */
    uint8_t                         u8PerfRel;
} NVMELBAFMTDESC;
AssertCompileSize(NVMELBAFMTDESC, 4);
/** Pointer to a LBA format descriptor. */
typedef NVMELBAFMTDESC *PNVMELBAFMTDESC;


/** @name LBA format descriptor defines.
 * @{ */
/** Best performance. */
#define NVMELBAFMTDESC_PERF_RELATIVE_BEST     0x00
/** Best performance. */
#define NVMELBAFMTDESC_PERF_RELATIVE_BETTER   0x01
/** Best performance. */
#define NVMELBAFMTDESC_PERF_RELATIVE_GOOD     0x02
/** Best performance. */
#define NVMELBAFMTDESC_PERF_RELATIVE_DEGRADED 0x03
/** @} */

/**
 * Identify Namespace Data Structure.
 */
typedef struct NVMEIDENTIFYNS
{
    /** Namespace size. */
    uint64_t                        u64NsSz;
    /** Namespace capacity. */
    uint64_t                        u64NsCap;
    /** Namespace utilization. */
    uint64_t                        u64NsUtil;
    /** Namespace features. */
    uint8_t                         u8NsFeat;
    /** Number of LBA formats. */
    uint8_t                         u8LbaFmts;
    /** Formatted LBA size. */
    uint8_t                         u8LbaSzFmt;
    /** Metadata capabilities. */
    uint8_t                         u8MetadataCap;
    /** Ent-to-end data protection capabilities. */
    uint8_t                         u8End2EndProtCap;
    /** End-to-end data protection type settings. */
    uint8_t                         u8End2EndProtCfg;
    /** Namespace Multi-path I/O and namespace sharing capabilities. */
    uint8_t                         u8NsMultPathAndNsSharingCap;
    /** Reservation capabilities. */
    uint8_t                         u8ReservCap;
    /** Format progress indicator. */
    uint8_t                         u8FmtProgressInd;
    /** Reserved. */
    uint8_t                         u8Rsvd0;
    /** Namespace atomic write unit normal. */
    uint16_t                        u16NsAtomicWriteUnitNormal;
    /** Namespace atomic write unit power fail. */
    uint16_t                        u16NsAtomicWriteUnitPwrFail;
    /** Namespace atomic compare & write unit. */
    uint16_t                        u16NsAtomicWriteCmpUnit;
    /** Namespace atomic boundary size normal. */
    uint16_t                        u16NsAtomicBoundarySzNormal;
    /** Namespace atomic boundary offset. */
    uint16_t                        u16NsAtomicBoundaryOffset;
    /** Namespace atomic boundary size power fail. */
    uint16_t                        u16NsAtomicBoundarySzPwrFail;
    /** Reserved. */
    uint16_t                        u16Rsvd1;
    /** NVM capacity. */
    uint8_t                         au8NvmCapacity[16];
    /** Reserved. */
    uint8_t                         au8Rsvd2[40];
    /** Namespace globally unique identifier. */
    uint8_t                         au8NsGuid[16];
    /** IEEE extended unique identifier. */
    uint64_t                        u64IeeeUi64;
    /** LBA formats. */
    NVMELBAFMTDESC                  aLbaFmts[16];
    /** Reserved. */
    uint8_t                         au8Rsvd3[192];
    /** Vendor specific. */
    uint8_t                         au8VendorSpecific[3712];
} NVMEIDENTIFYNS;
AssertCompileSize(NVMEIDENTIFYNS, _4K);
/** Pointer to a identify namespace structure. */
typedef NVMEIDENTIFYNS *PNVMEIDENTIFYNS;


/* @ } */ /** identify chapter 4 group term, disabled due to illegal nesting */

/**
 * NVM command set - command format.
 */
typedef struct NVMECMDNVM
{
    /** View dependent data. */
    union /** @todo r=bird: why isn't NVMECMDNVM a union? */
    {
        /** Field view. */
        struct
        {
            /** Command header. */
            NVMECMDHDR              Hdr;
            /** Namespace identifier this command applies to. */
            NVMENSID                uNsId;
            /** Reserved */
            uint8_t                 au8Rsvd[8];
            /** Metadata pointer. */
            RTGCPHYS64              GCPhysMetaPtr;
            /** Data Pointer, view depends on command. */
            union
            {
                /** PRP type addressing. */
                struct
                {
                    /** PRP Entry 1. */
                    NVMEPRP         Prp1;
                    /** PRP Entry 2. */
                    NVMEPRP         Prp2;
                } Prp;
                /** First SGL descriptor. */
                NVMESGL             Sgl;
            } DataPtr;
            /** Command dependent data. */
            union
            {
                /** Read/Write specific fields. */
                struct
                {
                    /** Starting LBA. */
                    uint64_t        u64LbaStart;
                    /** Number of logical blocks to transfer. */
                    uint16_t        u16Blocks;
                    /** @todo There are other fields following but they are not
                     * interesting for us as we don't implement any of the associated features. */
                } ReadWrite;
            } u;
        } Field;
        /** DWord view. */
        uint32_t                    au32[16];
        /** Byte view. */
        uint8_t                     au8[64];
    } u;
} NVMECMDNVM;
AssertCompileSize(NVMECMDNVM, 64);
/** Pointer to a NVMe admin command. */
typedef NVMECMDNVM *PNVMECMDNVM;


/** @name NVM command specific defines.
 * @{ */
/** Opcode: Flush */
#define NVME_CMD_NVM_FLUSH                              0x00
/** Opcode: Write */
#define NVME_CMD_NVM_WRITE                              0x01
/** Opcode: Read */
#define NVME_CMD_NVM_READ                               0x02
/** @} */

/**
 * Completion queue entry.
 */
typedef struct NVMECQENT
{
    /** Command specific value. */
    uint32_t                        u32CmdSpecific;
    /** Reserved */
    uint32_t                        u32Rsvd;
    /** Submission queue head pointer. */
    uint16_t                        u16SqHead;
    /** Submission queue identifier. */
    uint16_t                        u16SqId;
    /** Command identifier. */
    uint16_t                        u16Cid;
    /** Status field, including the phase tag field. */
    uint16_t                        u16StsPh;
} NVMECQENT;
AssertCompileSize(NVMECQENT, 16);
/** Pointer to completion queue entry. */
typedef NVMECQENT *PNVMECQENT;


/** Phase tag bit of a completion queue entry. */
#define NVME_CQ_ENTRY_PHASE_TAG                        RT_BIT(0)
/** Get status code. */
#define NVME_CQ_ENTRY_SC_GET(a_u16StsPh)               (((a_u16StsPh) >> 1) & 0xff)
/** Set status code. */
#define NVME_CQ_ENTRY_SC_SET(a_u8Sc)                   (((a_u8Sc) & 0xff) << 1)

/** @name Generic status codes.
 * @{ */
/** Generic Status code: Successful completion. */
#define NVME_CQ_ENTRY_SC_GEN_SUCCESS                   0x00
/** Generic Status code: Invalid command opcode. */
#define NVME_CQ_ENTRY_SC_GEN_INV_OPC                   0x01
/** Generic Status code: Invalid field in command. */
#define NVME_CQ_ENTRY_SC_GEN_INV_CMD_FIELD             0x02
/** Generic Status code: Command ID conflict. */
#define NVME_CQ_ENTRY_SC_GEN_CID_CONFLICT              0x03
/** Generic Status code: Data transfer error. */
#define NVME_CQ_ENTRY_SC_GEN_DATA_XFER_ERR             0x04
/** Generic Status code: Commands aborted to power loss notification. */
#define NVME_CQ_ENTRY_SC_GEN_CMD_ABRT_PWR_LOSS         0x05
/** Generic Status code: Internal error. */
#define NVME_CQ_ENTRY_SC_GEN_INTERNAL_ERR              0x06
/** Generic Status code: Command abort requested. */
#define NVME_CQ_ENTRY_SC_GEN_CMD_ABRT_REQ              0x07
/** Generic Status code: Command aborted due to submission queue deletion. */
#define NVME_CQ_ENTRY_SC_GEN_CMD_ABRT_SQ_DEL           0x08
/** Generic Status code: Command aborted due to failed fused command. */
#define NVME_CQ_ENTRY_SC_GEN_CMD_ABRT_FUSE_ERR         0x09
/** Generic Status code: Command aborted due to missing fused command. */
#define NVME_CQ_ENTRY_SC_GEN_CMD_ABRT_FUSED_MISSING    0x0a
/** Generic Status code: Invalid Namespace or format. */
#define NVME_CQ_ENTRY_SC_GEN_INV_NS_OR_FMT             0x0b
/** Generic Status code: Command sequence error. */
#define NVME_CQ_ENTRY_SC_GEN_CMD_SEQ_ERR               0x0c
/** Generic Status code: Invalid SGL segment descriptor. */
#define NVME_CQ_ENTRY_SC_GEN_INV_SGL_DESC              0x0d
/** Generic Status code: Invalid number of SGL descriptors. */
#define NVME_CQ_ENTRY_SC_GEN_INV_SGL_DESC_CNT          0x0e
/** Generic Status code: Data SGL length invalid. */
#define NVME_CQ_ENTRY_SC_GEN_INV_SGL_LENGTH            0x0f
/** Generic Status code: Metadata SGL length invalid. */
#define NVME_CQ_ENTRY_SC_GEN_INV_MD_SGL_LENGTH         0x10
/** Generic Status code: SGL descriptor type invalid. */
#define NVME_CQ_ENTRY_SC_GEN_INV_SGL_DESC_TYPE         0x11
/** Generic Status code: Invalid use of controller memory buffer. */
#define NVME_CQ_ENTRY_SC_GEN_INV_CMB_USE               0x12
/** Generic Status code: PRP offset invalid. */
#define NVME_CQ_ENTRY_SC_GEN_INV_PRP_OFFSET            0x13
/** Generic Status code: Atomic write unit exceeded. */
#define NVME_CQ_ENTRY_SC_GEN_ATOMIC_WR_UNIT_EXCD       0x14
/** Generic Status code NVM: LBA out of range. */
#define NVME_CQ_ENTRY_SC_GEN_NVM_LBA_OUT_OF_RANGE      0x80
/** Generic Status code NVM: Capacity exceeded. */
#define NVME_CQ_ENTRY_SC_GEN_NVM_CAP_EXCEEDED          0x81
/** Generic Status code NVM: Namespace not ready. */
#define NVME_CQ_ENTRY_SC_GEN_NVM_NS_NOT_RDY            0x82
/** Generic Status code NVM: Reservation conflict. */
#define NVME_CQ_ENTRY_SC_GEN_NVM_RES_CONFLICT          0x83
/** Generic Status code NVM: Format in progress. */
#define NVME_CQ_ENTRY_SC_GEN_NVM_FMT_IN_PROGRESS       0x84
/** @} */

/** @name Command specific status codes.
 * @{ */
/** Completion queue invalid. */
#define NVME_CQ_ENTRY_SC_CMD_INV_CQ                    0x00
/** Invalid queue ID. */
#define NVME_CQ_ENTRY_SC_CMD_INV_QID                   0x01
/** Invalid queue size. */
#define NVME_CQ_ENTRY_SC_CMD_INV_Q_SZ                  0x02
/** Abort command limit exceeded. */
#define NVME_CQ_ENTRY_SC_CMD_ABRT_LIM_EXCD             0x03
/** Reserved. */
#define NVME_CQ_ENTRY_SC_CMD_RSVD                      0x04
/** Asynchronous event request limit exceeded. */
#define NVME_CQ_ENTRY_SC_CMD_ASYNC_EVT_LIM_EXCD        0x05
/** Invalid firmware slot. */
#define NVME_CQ_ENTRY_SC_CMD_INV_FW_SLOT               0x06
/** Invalid firmware image. */
#define NVME_CQ_ENTRY_SC_CMD_INV_FW_IMG                0x07
/** Invalid interrupt vector. */
#define NVME_CQ_ENTRY_SC_CMD_INV_INTR_VEC              0x08
/** Invalid log page. */
#define NVME_CQ_ENTRY_SC_CMD_INV_LOG_PAGE              0x09
/** Invalid firmware image. */
#define NVME_CQ_ENTRY_SC_CMD_INV_FORMAT                0x0a
/** Firmware activation requires conventional reset. */
#define NVME_CQ_ENTRY_SC_CMD_FW_ACT_CONV_RST_REQ       0x0b
/** Invalid queue deletion. */
#define NVME_CQ_ENTRY_SC_CMD_INV_Q_DEL                 0x0c
/** Feature identifier not saveable. */
#define NVME_CQ_ENTRY_SC_CMD_FEAT_ID_NOT_SAVEABLE      0x0d
/** Feature not changeable. */
#define NVME_CQ_ENTRY_SC_CMD_FEAT_NOT_CHANGEABLE       0x0e
/** Feature not namespace specific. */
#define NVME_CQ_ENTRY_SC_CMD_FEAT_NOT_NS_SPECIFIC      0x0f
/** Firmware activation requires NVM subsystem reset. */
#define NVME_CQ_ENTRY_SC_CMD_FW_ACT_NVM_SUBSYS_RST_REQ 0x10
/** Firmware activation requires reset. */
#define NVME_CQ_ENTRY_SC_CMD_FW_ACT_RST_REQ            0x11
/** Firmware activation requires maximum time violation. */
#define NVME_CQ_ENTRY_SC_CMD_FW_ACT_MAX_TIME_VIOL_REQ  0x12
/** Firmware activation prohibited. */
#define NVME_CQ_ENTRY_SC_CMD_FW_ACT_PROHIBITED         0x13
/** Overlapping range. */
#define NVME_CQ_ENTRY_SC_CMD_RANGES_OVERLAPPING        0x14
/** Namespace insufficient capacity. */
#define NVME_CQ_ENTRY_SC_CMD_NS_INSUFFICIENT_CAP       0x15
/** Namespace identifier unavailable. */
#define NVME_CQ_ENTRY_SC_CMD_NS_ID_UNAVAILABLE         0x16
/** Reserved. */
#define NVME_CQ_ENTRY_SC_CMD_RSVD2                     0x17
/** Namespace already attached. */
#define NVME_CQ_ENTRY_SC_CMD_NS_ALREADY_ATTACHED       0x18
/** Namespace is private. */
#define NVME_CQ_ENTRY_SC_CMD_NS_IS_PRIVATE             0x19
/** Namespace not attached. */
#define NVME_CQ_ENTRY_SC_CMD_NS_NOT_ATTACHED           0x1a
/** Thin provisioning not supported. */
#define NVME_CQ_ENTRY_SC_CMD_THIN_PROV_NOT_SUPPORTED   0x1b
/** Controller list invalid. */
#define NVME_CQ_ENTRY_SC_CMD_INV_CTRL_LST              0x1c

/** Conflicting attributes. */
#define NVME_CQ_ENTRY_SC_CMD_NVM_ATTR_CONFLICT         0x80
/** Invalid protection information. */
#define NVME_CQ_ENTRY_SC_CMD_NVM_INV_PROT_INF          0x81
/** Attempted write to readonly range. */
#define NVME_CQ_ENTRY_SC_CMD_NVM_RANGE_READONLY        0x82
/** @} */

/** @name Media and data integrity errors.
 * @{ */
/** Write fault */
#define NVME_CQ_ENTRY_SC_INTEG_WRITE_FAULT             0x80
/** Unrecoverable read error. */
#define NVME_CQ_ENTRY_SC_INTEG_UNRECOV_READ_ERR        0x81
/** End-to-end guard check error. */
#define NVME_CQ_ENTRY_SC_INTEG_GRD_CHK_ERR             0x82
/** End-to-end application tag check error. */
#define NVME_CQ_ENTRY_SC_INTEG_APP_TAG_CHK_ERR         0x83
/** End-to-end reference tag check error. */
#define NVME_CQ_ENTRY_SC_INTEG_REF_TAG_CHK_ERR         0x84
/** Compare failure. */
#define NVME_CQ_ENTRY_SC_INTEG_COMPARE_ERR             0x85
/** Access denied. */
#define NVME_CQ_ENTRY_SC_INTEG_ACCESS_DENIED           0x86
/** Deallocated or unwritten logical block. */
#define NVME_CQ_ENTRY_SC_INTEG_LOG_BLK_DEALLOC         0x87
/** @} */

/** @name ???
 * @{ */
/** Get status code tpe. */
#define NVME_CQ_ENTRY_SCT_GET(a_u16StsPh)              (((a_u16StsPh) >> 9) & 0x7)
/** Set status code tpe. */
#define NVME_CQ_ENTRY_SCT_SET(a_u3Sct)                 (((a_u3Sct) & 0x7) << 9)
/** Status code type: Generic command status. */
#define NVME_CQ_ENTRY_SCT_CMD_GENERIC                  0x0
/** Status code type: Command specific status. */
#define NVME_CQ_ENTRY_SCT_CMD_SPECIFIC                 0x1
/** Status code type: Media and data integrity errors. */
#define NVME_CQ_ENTRY_SCT_MEDIA_ERR                    0x2
/** More bit. */
#define NVME_CQ_ENTRY_MORE                             RT_BIT(14)
/** Do not retry bit. */
#define NVME_CQ_ENTRY_DNR                              RT_BIT(15)
/** @} */

/** @name Feature Identifiers - General.
 * @{ */
/** Reserved */
#define NVME_FEAT_ID_RSVD                              0x00
/** Arbitration. */
#define NVME_FEAT_ID_ARBITRATION                       0x01
/** Power management. */
#define NVME_FEAT_ID_POWER_MGMT                        0x02
/** LBA range type. */
#define NVME_FEAT_ID_LBA_RANGE_TYPE                    0x03
/** Temperature threshold. */
#define NVME_FEAT_ID_TEMP_THRESHOLD                    0x04
/** Error recovery. */
#define NVME_FEAT_ID_ERROR_RECOVERY                    0x05
/** Volatile write cache. */
#define NVME_FEAT_ID_VOLATILE_WRITE_CACHE              0x06
/** Number of queues. */
#define NVME_FEAT_ID_NUMBER_OF_QUEUES                  0x07
/** Interrupt coalescing. */
#define NVME_FEAT_ID_INTERRUPT_COALESCING              0x08
/** Interrupt vector configuration. */
#define NVME_FEAT_ID_INTERRUPT_VEC_CFG                 0x09
/** Write atomicity norma. */
#define NVME_FEAT_ID_WRITE_ATOMICITY_NORMAL            0x0a
/** Asynchronous event configuration. */
#define NVME_FEAT_ID_ASYNC_EVT_CFG                     0x0b
/** Autonomous power state transition. */
#define NVME_FEAT_ID_AUTONOMOUS_PWR_STATE_TRANS        0x0c
/** Host memory buffer. */
#define NVME_FEAT_ID_HOST_MEMORY_BUFFER                0x0d
/** @} */

/** @name Feature Identifiers - NVM command set.
 * @{ */
/** Software progress marker. */
#define NVME_FEAT_ID_NVM_SOFTWARE_PROGRESS_MARKER       0x80
/** Host identifier. */
#define NVME_FEAT_ID_NVM_HOST_IDENTIFIER                0x81
/** Reservation notification mask. */
#define NVME_FEAT_ID_NVM_RESERV_NOTIFICATION_MASK       0x82
/** Reservation persistence. */
#define NVME_FEAT_ID_NVM_RESERV_PERSISTANCE             0x83
/** @} */

/* @ name Log page specific information.
 * @ { */                                /** @todo disabled due to illegal name nesting */

/** @name Log page identifiers - General.
 * @{ */
/** Reserved. */
#define NVME_LOG_PAGE_ID_RSVD                           0x00
/** Error information. */
#define NVME_LOG_PAGE_ID_ERROR_INFORMATION              0x01
/** SMART/Heath information. */
#define NVME_LOG_PAGE_ID_HEALTH_INFORMATION             0x02
/** Firmware slot information. */
#define NVME_LOG_PAGE_ID_FIRMWARE_SLOT_INFORMATION      0x03
/** Changed namespace list. */
#define NVME_LOG_PAGE_ID_CHANGED_NS_LIST                0x04
/** Command effects log. */
#define NVME_LOG_PAGE_ID_CMD_EFFECTS                    0x05
/** @} */

/** @name Log page identifiers - NVM.
 * @{ */
/** Reservation notification. */
#define NVME_LOG_PAGE_ID_NVM_RESERVATION_NOTIFICATION   0x80
/** @} */

/**
 * Error information log page entry.
 */
typedef struct NVMELOGPGERRENT
{
    /** Error counter */
    uint64_t                        u64ErrCount;
    /** Submission queue ID. */
    uint16_t                        u16SubmQueueId;
    /** Command ID. */
    uint16_t                        u16Cid;
    /** Status field. */
    uint16_t                        u16StsField;
    /** Parameter error location. */
    uint16_t                        u16ParamErr;
    /** First LBA experiencing the error condition. */
    uint64_t                        u64LbaErr;
    /** Namespace ID. */
    NVMENSID                        uNsId;
    /** Vendor specific information available. */
    uint8_t                         u8VendSpecInfAvail;
    /** Reserved. */
    uint8_t                         au8Rsvd0[3];
    /** Command specific information. */
    uint64_t                        u64CmdSpecInf;
    /** Reserved. */
    uint8_t                         au8Rsvd1[24];
} NVMELOGPGERRENT;
AssertCompileSize(NVMELOGPGERRENT, 64);
/** Pointer to a error information log page entry. */
typedef NVMELOGPGERRENT *PNVMELOGPGERRENT;


/**
 * SMART/Health information log page
 */
#pragma pack(1)
typedef struct NVMELOGPGHEALTH
{
    /** Critical warning flags. */
    uint8_t                         u8Critical;
    /** Composite temperature. */
    uint16_t                        u16CompTemp;
    /** Available spare. */
    uint8_t                         u8SpareAvail;
    /** Available spare threshold. */
    uint8_t                         u8SpareAvailThreshold;
    /** Lifetime percentage used. */
    uint8_t                         u8LifetimePercentageUsed;
    /** Reserved. */
    uint8_t                         au8Rsvd0[26];
    /** Data units read. */
    uint8_t                         au8DataUnitsRead[16];
    /** Data units written. */
    uint8_t                         au8DataUnitsWritten[16];
    /** Read commands completed b the controller. */
    uint8_t                         au8ReadCmdsCompleted[16];
    /** Write commands completed b the controller. */
    uint8_t                         au8WriteCmdsCompleted[16];
    /** Controller Busy Time (reported in minutes). */
    uint8_t                         au8CtrlBusyTime[16];
    /** Number of power cycles. */
    uint8_t                         au8PwrCycles[16];
    /** Power on hours. */
    uint8_t                         au8PwrOnHours[16];
    /** Unsafe shutdowns. */
    uint8_t                         au8UnsafeShutdowns[16];
    /** Media and data integrity errors. */
    uint8_t                         au8MediaDataIntegrityErros[16];
    /** Number of error information log entries over the lifetime. */
    uint8_t                         au8ErrorInformationLogEntries[16];
    /** Warning composite temperature time. */
    uint32_t                        u32CompTempWarnTime;
    /** Critical composite temperature time. */
    uint32_t                        u32CompTempCritTime;
    /** Temperature sensors current value in Kelvin. */
    uint16_t                        aTempSensors[8];
    /** Reserved. */
    uint8_t                         au8Rsvd1[296];
} NVMELOGPGHEALTH;
#pragma pack()
AssertCompileSize(NVMELOGPGHEALTH, 512);
/** Pointer to a SMART/Health information log page. */
typedef NVMELOGPGHEALTH *PNVMELOGPGHEALTH;


/** @name Defines for the SMART/Health information log page.
 * @{ */
/** Available spare space has fallen below threshold. */
#define NVME_LOG_PG_HEALTH_AVAIL_SPARE_BELOW_THRESHOLD  RT_BIT(0)
/** A temperature is above an over temperature threshold or below an under temperature threshold. */
#define NVME_LOG_PG_HEALTH_NOT_WITHIN_TEMP_THRESHOLDS   RT_BIT(1)
/** NVMe reliability degraded due to significant media related errors. */
#define NVME_LOG_PG_HEALTH_RELIABILITY_DEGRADED         RT_BIT(2)
/** Media is readonly. */
#define NVME_LOG_PG_HEALTH_MEDIA_READONLY               RT_BIT(3)
/** Volatile memory backup device failed. */
#define NVME_LOG_PG_HEALTH_VOL_MEM_BACKUP_DEVICE_FAILED RT_BIT(4)
/** @} */

/**
 * Firmware slot information log page.
 */
typedef struct NVMELOGPGFWSLOT
{
    /** Active firmware information. */
    uint8_t                         u8ActiveFwInfo;
    /** Reserved. */
    uint8_t                         au8Rsvd0[7];
    /** Firmware slot revisions. */
    uint64_t                        au64FwRevisions[7];
    /** Reserved. */
    uint8_t                         au8Rsvd1[448];
} NVMELOGPGFWSLOT;
AssertCompileSize(NVMELOGPGFWSLOT, 512);
/** Pointer to a Firmware slot information log page. */
typedef NVMELOGPGFWSLOT *PNVMELOGPGFWSLOT;


/* @} */ /** @todo disabled illegal nesting of "Log page specific information." */

/* @ name Asynchronous event request specific defines.
 * @ { */   /** @todo illegal nesting */

/** @name Asynchronous event types.
 * @{ */
/** Error status. */
#define NVME_ASYNC_EVT_TYPE_ERROR                       0
/** SMART/Health status. */
#define NVME_ASYNC_EVT_TYPE_SMART_HELATH                1
/** Notice */
#define NVME_ASYNC_EVT_TYPE_NOTICE                      2
/** I/O command set specific status. */
#define NVME_ASYNC_EVT_TYPE_CMD_SET                     6
/** Vendor specific. */
#define NVME_ASYNC_EVT_TYPE_VEDOR                       7
/** @} */

/** @name Async event information - Error status.
 * @{ */
/** Write to invalid doorbell register. */
#define NVME_ASYNC_EVT_INFO_ERROR_DOORBELL_INVALID       0
/** Invalid doorbell write value. */
#define NVME_ASYNC_EVT_INFO_ERROR_DOORBELL_VALUE_INVALID 1
/** Diagnostic failure. */
#define NVME_ASYNC_EVT_INFO_ERROR_DIAGNOSTIC             2
/** Persistent internal error. */
#define NVME_ASYNC_EVT_INFO_ERROR_PERSISTENT             3
/** Transient failure. */
#define NVME_ASYNC_EVT_INFO_ERROR_TRANSIENT              4
/** @} */

/* @ } */  /** @todo disabled illegal nesting of "Asynchronous event request specific defines." */


/* Forward declarations: */
/** Pointer to a shared NVMe device state. */
typedef struct NVME *PNVME;
/** Pointer to a ring-3 NVMe device state. */
typedef struct NVMER3 *PNVMER3;
/** Pointer to a ring-0 NVMe device state. */
typedef struct NVMER0 *PNVMER0;
/** Pointer to a raw-mode NVMe device state. */
typedef struct NVMERC *PNVMERC;
/** Current context NVMe device state. */
typedef struct CTX_SUFF(NVME) NVMECC;
/** Pointer to a current context NVMe device state. */
typedef CTX_SUFF(PNVME) PNVMECC;
/** Pointer to a NVMe I/O request. */
typedef struct NVMEIOREQ *PNVMEIOREQ;


/**
 * NVMe worker thread structure.
 */
typedef struct NVMEWRKTHRD
{
    /** List node for the global worker list. */
    RTLISTNODE                      NodeWrkThrdList;
    /** Id of the worker.  */
    uint32_t                        uId;
    /** Handle of the event sempahore for associated worker thread responsible for this queue. */
    SUPSEMEVENT                     hEvtProcess;
    /** The async worker thread. */
    R3PTRTYPE(PPDMTHREAD)           pThrd;
    /** Pointer back to the NVMe instance data. */
    R3PTRTYPE(PNVME)                pNvmeR3;
    /** Flag indicating a sleeping worker thread. */
    volatile bool                   fWrkThrdSleeping;
    /** Number of submission queues this instance is responsible for. */
    volatile uint32_t               cSubmQueues;
    /** List of assigned submission queues (PNVMEQUEUESUBM). */
    RTLISTANCHOR                    ListSubmQueuesAssgnd;
    /** Request queue for assigning and unassigning submission queues. */
    R3PTRTYPE(RTREQQUEUE)           hReqQueue;
} NVMEWRKTHRD;
/** Pointer to a NVMe worker thread structure. */
typedef NVMEWRKTHRD *PNVMEWRKTHRD;

/**
 * NVMe name space instance data.
 */
typedef struct NVMENAMESPACE
{
    /** Namespace ID (for validation purposes). */
    uint32_t                        u32Id;

    /** Block size for this namespace. */
    size_t                          cbBlock;
    /** Number of blocks in this namespace. */
    uint64_t                        cBlocks;

    /** @name Namespace specific settings (R3 only stuff).
     * @{ */
    /** Pointer to the attached driver's base interface. */
    R3PTRTYPE(PPDMIBASE)            pDrvBase;
    /** Pointer to the attached driver's media interface. */
    R3PTRTYPE(PPDMIMEDIA)           pDrvMedia;
    /** Pointer to the attached driver's extended interface. */
    R3PTRTYPE(PPDMIMEDIAEX)         pDrvMediaEx;
    /** The base interface. */
    PDMIBASE                        IBase;
    /** The media port interface. */
    PDMIMEDIAPORT                   IPort;
    /** The extended media port interface. */
    PDMIMEDIAEXPORT                 IPortEx;
    /** The status LED state for this namespace. */
    PDMLED                          Led;

    /** Pointer to the NVMe device instance. */
    PPDMDEVINS                      pDevIns;
    /** @} */

} NVMENAMESPACE;
/** Pointer to a NVMe name space instance. */
typedef NVMENAMESPACE *PNVMENAMESPACE;

/**
 * NVMe queue type.
 */
typedef enum NVMEQUEUETYPE
{
    /** Invalid queue type - marker for not existing queues. */
    NVMEQUEUETYPE_INVALID = 0,
    /** Submission queue. */
    NVMEQUEUETYPE_SUBMISSION,
    /** Completion queue. */
    NVMEQUEUETYPE_COMPLETION,
    /** 32bit hack. */
    NVMEQUEUETYPE_32BIT_HACK = 0x7fffffff
} NVMEQUEUETYPE;

/**
 * NVMe queue state.
 */
typedef enum NVMEQUEUESTATE
{
    /** Invalid state. */
    NVMEQUEUESTATE_INVALID = 0,
    /** Deallocated state. */
    NVMEQUEUESTATE_DEALLOCATED,
    /** Allocated and ready to accept commands or process completed commands. */
    NVMEQUEUESTATE_ALLOCATED,
    /** Queue is in the process of being destroyed and doesn't accept any new commands. */
    NVMEQUEUESTATE_DEALLOCATING,
    /** 32bit hack. */
    NVMEQUEUESTATE_32BIT_HACK = 0x7fffffff
} NVMEQUEUESTATE;

/**
 * NVMe submissin queue priority.
 */
typedef enum NVMEQUEUESUBMPRIO
{
    /** Invalid priority. */
    NVMEQUEUESUBMPRIO_INVALID = 0,
    /** Urgent priority. */
    NVMEQUEUESUBMPRIO_URGENT,
    /** High priority. */
    NVMEQUEUESUBMPRIO_HIGH,
    /** Medium priority. */
    NVMEQUEUESUBMPRIO_MEDIUM,
    /** Low priority. */
    NVMEQUEUESUBMPRIO_LOW,
    /** 32bit hack. */
    NVMEQUEUESUBMPRIO_32BIT_HACK = 0x7fffffff
} NVMEQUEUESUBMPRIO;

/**
 * NVMe queue header - common for submission and completion queues.
 */
typedef struct NVMEQUEUEHDR
{
    /** The queue ID. */
    uint16_t                        u16Id;
    /** The number of entries in the queue. */
    uint16_t                        cEntries;
    /** State of the queue. */
    volatile NVMEQUEUESTATE         enmState;
    /** Base address of the queue. */
    RTGCPHYS                        GCPhysBase;
    /** Size of one entry in bytes. */
    uint32_t                        cbEntry;
    /** The current head pointer. */
    volatile uint32_t               idxHead;
    /** The current tail pointer. */
    volatile uint32_t               idxTail;
    /** Flag whether the queue pointed to by GCPhysBase is physically contiguous. */
    bool                            fPhysCont;
    /** Alignment. */
    bool                            afAlignment[3];
    /** Queue type. */
    NVMEQUEUETYPE                   enmType;
    /** Alignment to 8 byte boundary. */
    uint32_t                        u32Alignment0;
} NVMEQUEUEHDR;
/** Pointer to a NVMe queue header. */
typedef NVMEQUEUEHDR *PNVMEQUEUEHDR;

/** Magic value for a full queue. */
#define NVME_QUEUE_IS_FULL           UINT32_MAX
/** Magic value for an empty queue. */
#define NVME_QUEUE_IS_EMPTY          (UINT32_MAX - 1)
/** Magic value for a full queue. */
#define NVME_QUEUE_IS_FULL_RTGCPHYS  UINT64_MAX
/** Magic value for an empty queue. */
#define NVME_QUEUE_IS_EMPTY_RTGCPHYS (UINT64_MAX - 1)

/**
 * NVMe submission queue shared state.
 */
typedef struct NVMEQUEUESUBM
{
    /** Queue header. */
    NVMEQUEUEHDR                    Hdr;
    /** ID of the associated completion queue. */
    uint16_t                        u16CompletionQueueId;
    /** The command identifier which issued the "Delete I/O submission queue" command.
     * Only valid if the queue is in the "Deallocating" state. */
    uint16_t                        u16CidDeallocating;
    /** Queue priority. */
    NVMEQUEUESUBMPRIO               enmPriority;
    /** Handle of the event sempahore for associated worker thread responsible for this queue. */
    SUPSEMEVENT                     hEvtProcess;
    /** Number of requests still active submitted from this queue. */
    volatile uint32_t               cReqsActive;
    /** Alignment. */
    uint32_t                        u32Alignment0;
} NVMEQUEUESUBM;
/** Pointer to the shared state of a submission queue. */
typedef NVMEQUEUESUBM *PNVMEQUEUESUBM;

/**
 * NVMe submission queue ring-3 state.
 */
typedef struct NVMEQUEUESUBMR3
{
    /** R3 pointer to the worker thread structure. */
    R3PTRTYPE(PNVMEWRKTHRD)         pWrkThrdR3;
    /** Node for the list of assigned submission queues for the assigned worker thread. */
    RTLISTNODER3                    NdLstWrkThrdAssgnd;
} NVMEQUEUESUBMR3;
/** Pointer to the ring-3 state of a submission queue. */
typedef NVMEQUEUESUBMR3 *PNVMEQUEUESUBMR3;

/**
 * NVMe completion queue shared state.
 */
typedef struct NVMEQUEUECOMP
{
    /** Queue header. */
    NVMEQUEUEHDR                    Hdr;
    /** Flag whether interrupts are enabled. */
    bool                            fIntrEnabled;
    /** Flag whther the completion queue is overloaded and any associated submission queue
     * should stop processing requests. */
    volatile bool                   fOverloaded;
    /** Alignment. */
    bool                            afAlignment[2];
    /** Interrupt vector used for this queue. */
    uint32_t                        u32IntrVec;
    /** Number of submission queues assigned to this completion queue. */
    volatile uint32_t               cSubmQueuesRef;
    /** Current number of requests waiting for completion because of a full queue. */
    volatile uint32_t               cWaiters;
} NVMEQUEUECOMP;
/** Pointer to the shared state of a completion queue. */
typedef NVMEQUEUECOMP *PNVMEQUEUECOMP;

/**
 * NVMe completion queue ring-3 state.
 */
typedef struct NVMEQUEUECOMPR3
{
    /** List for completion entries waiting because the queue is fill */
    RTLISTANCHORR3                  LstCompletionsWaiting;
    /** Fast mutex semaphore protecting the completion queue against concurrent access. */
    R3PTRTYPE(RTSEMFASTMUTEX)       hMtx;
} NVMEQUEUECOMPR3;
/** Pointer to the ring-3 state of a completion queue. */
typedef NVMEQUEUECOMPR3 *PNVMEQUEUECOMPR3;

/**
 * NVMe interrupt vector state.
 */
typedef struct NVMEINTRVEC
{
    /** Number of not acknowledged events keeping the interrupter asserted */
    volatile int32_t                cEvtsWaiting;
    /** Flag whether the interrupt vector is currently masked out. */
    volatile bool                   fIntrDisabled;
    /** Alignment */
    bool                            afAlignemnt0[3];
    /** PDM critical section protecting the interrupt vector. */
    PDMCRITSECT                     CritSectIntrVec;
} NVMEINTRVEC;
/** Pointer to a NVMe interrupt vector state. */
typedef NVMEINTRVEC *PNVMEINTRVEC;

/**
 * NVMe I/O Request.
 */
typedef struct NVMEIOREQ
{
    /** PDM I/O request for this one. */
    PDMMEDIAEXIOREQ                 hIoReq;
    /** The namespace the request is for. */
    PNVMENAMESPACE                  pNamespace;
    /** Associated submission queue. */
    PNVMEQUEUESUBM                  pQueueSubm;
    /** First PRP pointer. */
    NVMEPRP                         Prp1;
    /** Second PRP pointer. */
    NVMEPRP                         Prp2;
    /** Page lock when the buffer is mapped. */
    PGMPAGEMAPLOCK                  PgLck;
    /** Complete size of the PRP buffer. */
    size_t                          cbPrp;
    /** Command identifier of the command. */
    uint16_t                        u16Cid;
    /** Flag when the buffer is mapped. */
    bool                            fMapped;
} NVMEIOREQ;

/**
 * NVMe controller state.
 */
typedef enum NVMESTATE
{
    /** Invalid state. */
    NVMESTATE_INVALID = 0,
    /** Initialization state. */
    NVMESTATE_INIT,
    /** Controller is ready to accept commands through the submission queues. */
    NVMESTATE_READY,
    /** Processing is temporarily paused. */
    NVMESTATE_PAUSED,
    /** Transitioning from READY or PAUSED to RESETTING and waiting for all workers
     * to go idle. */
    NVMESTATE_QUIESCING,
    /** Controller is currently resetting. */
    NVMESTATE_RESETTING,
    /** Controller is in a fatal error state and needs to be reset. */
    NVMESTATE_FAULT,
    /** Shutdown processing occurring. */
    NVMESTATE_SHUTDOWN_PROCESSING,
    /** Shutdown processing complete. */
    NVMESTATE_SHUTDOWN_COMPLETE,
    /** 32bit Hack. */
    NVMESTATE_32BIT_HACK = 0x7fffffff
} NVMESTATE;
/** Pointer to a NVMe state. */
typedef NVMESTATE *PNVMESTATE;

/**
 * Wake queue item.
 */
typedef struct NVMEWAKEQUEUEITEM
{
    /** The core part owned by the queue manager. */
    PDMQUEUEITEMCORE                Core;
    /** The queue ID which got written. */
    uint32_t                        iQueueId;
} NVMEWAKEQUEUEITEM;
/** Pointer */
typedef NVMEWAKEQUEUEITEM *PNVMEWAKEQUEUEITEM;

/**
 * Completion queue waiting entry.
 */
typedef struct NVMECOMPQUEUEWAITER
{
    /** List node for the completion queue it is waiting on. */
    RTLISTNODE                      NdLstQueue;
    /** The submission queue the original request was assoicated to. */
    PNVMEQUEUESUBM                  pQueueSubm;
    /** The command identifier of the original request. */
    uint16_t                        u16Cid;
    /** Status code type the original request completed with. */
    uint8_t                         u8Sct;
    /** Status code the original request completed with. */
    uint8_t                         u8Sc;
    /** The command specific double word the original request completed with. */
    uint32_t                        u32CmdSpecific;
    /** The more information in log page bit the original request completed with. */
    bool                            fMore;
    /** The Do Not Retry bit the original request completed with. */
    bool                            fDnr;
} NVMECOMPQUEUEWAITER;
/** Pointer to a Completion queue waiter. */
typedef struct NVMECOMPQUEUEWAITER *PNVMECOMPQUEUEWAITER;

#ifdef VBOX_WITH_STATISTICS
/**
 * Statistics for a physical memory read/write type.
 */
typedef struct NVMEPHYSTYPESTAT
{
    /** Amount of data read from the controller memory buffer. */
    STAMCOUNTER                     StatReadCtrlMemBuf;
    /** Amount of data read from the guest memory. */
    STAMCOUNTER                     StatReadGuestMem;
    /** Amount of data written to the controller memory buffer. */
    STAMCOUNTER                     StatWrittenCtrlMemBuf;
    /** Amount of data written to the guest memory. */
    STAMCOUNTER                     StatWrittenGuestMem;
} NVMEPHYSTYPESTAT;
typedef NVMEPHYSTYPESTAT *PNVMEPHYSTYPESTAT;
#endif

/**
 * Shared NVMe instance data.
 */
typedef struct NVME
{
    /** @name Configuration values which are adjustable through CFGM.
     * @{ */
    /** Maximum number of submission queues supported. */
    uint16_t                        cQueuesSubmMax;
    /** Maximum number of completion queues supported. */
    uint16_t                        cQueuesCompMax;
    /** Maximum number of queue entries supported. */
    uint16_t                        cQueueEntriesMax;
    /** Worst case timeout in 500ms units. */
    uint8_t                         cTimeoutMax;
    uint8_t                         abPadding0[1+4];
    /** Maximum number of worker threads. */
    uint32_t                        cWrkThrdsMax;
    /** Maximum number of completion queue waiter entries if the queue is full. */
    uint32_t                        cCompQueuesWaitersMax;
    /** Number of namespaces configured for this controller. */
    uint32_t                        cNamespaces;
    /** Reported serial number. */
    char                            szSerialNumber[NVME_SERIAL_NUMBER_LENGTH+1]; /** < one extra byte for termination */
    /** Reported model number. */
    char                            szModelNumber[NVME_MODEL_NUMBER_LENGTH+1]; /** < one extra byte for termination */
    /** Reported firmware revision. */
    char                            szFirmwareRevision[NVME_FIRMWARE_REVISION_LENGTH+1]; /** < one extra byte for termination */
    /** @} */

    /** Current NVMe controller state. */
    volatile NVMESTATE              enmState;
    /** Current number of requests and threads active.
     * If this reaches 0 and the controller is in the QUEISCING state
     * we can reset it. */
    volatile uint32_t               cActivities;

    /** @name Interrupt handling related state.
     * @{ */
    /** Interrupt mask. */
    volatile uint32_t               u32IntrMask;
    uint32_t                        u32Alignment1;
    /** Interrupt vectors states. */
    NVMEINTRVEC                     aIntrVecs[NVME_INTR_VEC_MAX];
    /** @} */

    /** @name Controller configuration (CC) register bits.
     * @{ */
    /** I/O completion queue entry size as 2^n. */
    uint32_t                        u32IoCompletionQueueEntrySize;
    /** I/O submission queue entry size as 2^n. */
    uint32_t                        u32IoSubmissionQueueEntrySize;
    /** Last written shutdown notifier value by the guest. */
    uint8_t                         uShutdwnNotifierLast;
    /** Arbitration mechanism selected by the guest. */
    uint8_t                         uAmsSet;
    /** Memory page size selected by the guest. */
    uint8_t                         uMpsSet;
    /** Command set selected by the guest. */
    uint8_t                         uCssSet;
    /** @} */

    /** @name Index/Data pair register state.
     * @{ */
    /** Index register .*/
    uint32_t                        u32RegIdx;
    /** @} */

    /** Currently set page size, derived from NVME::uMpsSet. */
    uint32_t                        cbPage;
    /** Alignment. */
    uint32_t                        u32Alignment2;

    /** @name I/O Submission/Completion queue related controller state.
     * @{ */
    /** Array of possible submission queues. */
    NVMEQUEUESUBM                   aQueuesSubm[NVME_QUEUES_SUBMISSION_MAX];
    /** Array of possible completion queues. */
    NVMEQUEUECOMP                   aQueuesComp[NVME_QUEUES_COMPLETION_MAX];
    /** @} */

    /** @name Controller Memory Buffer (CMB) related state.
     * @{ */
    /** Guest physical address where the memory buffer is mapped. */
    RTGCPHYS                        GCPhysCtrlMemBuf;
    /** Size of the controller memory buffer in bytes. */
    uint64_t                        cbCtrlMemBuf;
    /** Content of the controller memory buffer size (CMBSZ) register. */
    uint32_t                        u32CtrlMemBufSz;
#if HC_ARCH_BITS == 64
    uint32_t                        u32Alignment3;
#endif
    /** @} */

    /** PCI Region \#0: MMIO */
    IOMMMIOHANDLE                   hMmio;
    /** PCI Region \#2: MMIO */
    IOMIOPORTHANDLE                 hIoPorts;
    /** PCI Region \#0: MMIO2 / ram, optional. */
    PGMMMIO2HANDLE                  hMmio2;

#ifdef VBOX_WITH_STATISTICS
    /** @name Statiscs related members.
     * @{ */
    NVMEPHYSTYPESTAT                aStatMemXfer[NVME_CMBSZ_SUPP_BIT_IDX_MAX+1];
    /** @} */
#endif

} NVME;

/* AssertCompileMemberAlignment(NVME, aIntrVecs[0].CritSectIntrVec, 8); */

/**
 * NVMe instance data, ring-3 edition.
 * @implements  PDMILEDPORTS
 */
typedef struct NVMER3
{
    /** Pointer to the device instance.
     * @note Only for getting our bearings when arriving on an interface. */
    PPDMDEVINSR3                    pDevIns;

    /** Status LUN: The base interface. */
    PDMIBASE                        IBase;
    /** Status LUN: Leds interface. */
    PDMILEDPORTS                    ILeds;
    /** Status LUN: Partner of ILeds. */
    R3PTRTYPE(PPDMILEDCONNECTORS)   pLedsConnector;

    /** @name I/O Submission/Completion queue related controller state.
     * @{ */
    /** Array of possible submission queues. */
    NVMEQUEUESUBMR3                 aQueuesSubm[NVME_QUEUES_SUBMISSION_MAX];
    /** Array of possible completion queues. */
    NVMEQUEUECOMPR3                 aQueuesComp[NVME_QUEUES_COMPLETION_MAX];
    /** @} */

    /** @name Controller Memory Buffer (CMB) related ring-3 state.
     * @{ */
    /** Base R3 pointer to the controller buffer. */
    R3PTRTYPE(void *)               pvCtrlMemBufR3;
    /** @} */

    /** @name Async Event Request specific state.
     * @{ */
    /** Maximum number of simultaneously outstanding "Asynchronous Event Requests". */
    uint32_t                        cAsyncEvtReqsMax;
    /** Number of available requests submitted by the guest. */
    uint32_t                        cAsyncEvtReqsCur;
    /** Critical section protecting the array of command identifiers. */
    RTCRITSECT                      CritSectAsyncEvtReqs;
    /** Pointer to the array holding the "Async Event Requests" command identifiers. */
    R3PTRTYPE(uint16_t *)           paAsyncEvtReqCids;
    /** @} */

    /** @name Namespace related controller state.
     * @{ */
    /** Pointer to the array of namespaces, indexed by the ID. */
    R3PTRTYPE(PNVMENAMESPACE)       paNamespaces;
    /** @} */

    /** @name Worker thread related controller state.
     * @{ */
    /** Current number of worker threads. */
    uint32_t                        cWrkThrdsCur;
    /** Number of threads currently working, used for synchronization.
     * @todo r=bird: Cannot spot anyone using this... */
    volatile uint32_t               cWrkThrdsActive;
    /** Pointer to the worker thread list anchor (NVMEWRKTHRD). */
    RTLISTANCHORR3                  LstWrkThrds;
    /** Critical section protecting the list of worker threads and the work threadd counter. */
    RTCRITSECT                      CritSectWrkThrds;
    /** @} */

    /** @name State related to signalling PDM when all requests have completed when suspending.
     * @{ */
    /** Indicates that PDMDevHlpAsyncNotificationCompleted should be called when
     * a submission queue is entering the idle state. */
    bool volatile                   fSignalIdle;
    bool                            afAlignment5[7];
    /** @} */
} NVMER3;


/**
 * NVMe instance data, ring-0 edition.
 */
typedef struct NVMER0
{
    uint64_t                        uUnused;
} NVMER0;


/**
 * NVMe instance data, raw-mode edition.
 */
typedef struct NVMERC
{
    uint64_t                        uUnused;
} NVMERC;



/**
 * Creates a I/O request ID from the given queue ID and CID.
 */
#define NVME_IOREQID_FROM_QID_AND_CID(a_u16QId, a_u16Cid) (((PDMMEDIAEXIOREQID)(a_u16QId) << 16) | (a_u16Cid))

#ifdef IN_RING3
/**
 * Memory buffer callback.
 *
 * @param   pDevIns The device instance.
 * @param   pThis   The NVMe controller shared instance data.
 * @param   pThisCC The NVMe controller ring-3 instance data.
 * @param   GCPhys  The guest physical address of the memory buffer.
 * @param   pSgBuf  The pointer to the host R3 S/G buffer.
 * @param   cbCopy  How many bytes to copy between the two buffers.
 * @param   pcbSkip Initially contains the amount of bytes to skip
 *                  starting from the guest physical address before
 *                  accessing the S/G buffer and start copying data.
 *                  On return this contains the remaining amount if
 *                  cbCopy < *pcbSkip or 0 otherwise.
 */
typedef DECLCALLBACKTYPE(void, FNNVMER3MEMCOPYCALLBACK,(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC, RTGCPHYS GCPhys,
                                                        PRTSGBUF pSgBuf, size_t cbCopy, size_t *pcbSkip));
/** Pointer to a memory copy buffer callback. */
typedef FNNVMER3MEMCOPYCALLBACK *PFNNVMER3MEMCOPYCALLBACK;
#endif

#ifndef VBOX_DEVICE_STRUCT_TESTCASE


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
#ifdef IN_RING3
static int nvmeR3SubmQueueAssignToWorker(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                         PNVMEQUEUESUBM pQueue, PNVMEQUEUESUBMR3 pQueueR3);
static void nvmeR3AsyncEvtComplete(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                   uint8_t u8AsyncEvtType, uint8_t u8AsyncEvtInfo, uint8_t u8LogPgId);
static void nvmeR3CtrlReset(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC);
static int nvmeR3CompQueueEntryPost(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC, RTGCPHYS GCPhysCe,
                                    PNVMEQUEUECOMP pQueueComp, PNVMEQUEUESUBM pQueueSubm, uint16_t u16Cid,
                                    uint8_t u8Sct, uint8_t u8Sc, uint32_t u32CmdSpecific, bool fMore, bool fDnr);
static DECLCALLBACK(void) nvmeR3WrkThrdRemoveWorker(PNVMEWRKTHRD pWrkThrd, PNVMEQUEUESUBM pQueue, PNVMEQUEUESUBMR3 pQueueR3);
static int nvmeR3WrkThrdRemoveSubmissionQueue(PPDMDEVINS pDevIns, PNVMEQUEUESUBM pQueue, PNVMEQUEUESUBMR3 pQueueR3);
DECLINLINE(int) nvmeR3WrkThrdKick(PPDMDEVINS pDevIns, PNVMEWRKTHRD pWrkThrd);
#endif


#ifdef IN_RING3

/**
 * Check whether the queue is empty.
 *
 * @returns true if the queue is empty,
 *          false otherwise.
 * @param   pQueue    The queue to check.
 */
DECLINLINE(bool) nvmeQueueIsEmpty(PNVMEQUEUEHDR pQueue)
{
    return pQueue->idxHead == pQueue->idxTail;
}

/**
 * Check whether the queue is full.
 *
 * @returns true if the queue is full,
            false otherwise.
 * @param   pQueue    The queue to check.
 */
DECLINLINE(bool) nvmeQueueIsFull(PNVMEQUEUEHDR pQueue)
{
    return ((pQueue->idxTail + 1) % RT_MAX(pQueue->cEntries, 1) == pQueue->idxHead);
}

#endif /* IN_RING3 */

/**
 * Returns the number of entries residing between a given producer and consumer
 * pointer.
 *
 * @returns Amount of entries between the given producer and consumer pointers.
 * @param   cEntries    The number of entries in the queue.
 * @param   idxProducer The producer pointer.
 * @param   idxConsumer The consumer pointer.
 */
DECLINLINE(uint16_t) nvmeQueueGetEntriesBetweenPointers(uint32_t cEntries, uint32_t idxProducer,
                                                        uint32_t idxConsumer)
{
    if (idxProducer >= idxConsumer)
        return idxProducer - idxConsumer;
    return cEntries - idxConsumer + idxProducer;
}

#if 0 /* unused */
/**
 * Returns the number of occupied entries in the queue.
 *
 * @returns Number of occupied entries in the queue.
 * @param   pQueue    The queue to check.
 */
DECLINLINE(uint16_t) nvmeQueueGetOccupiedEntries(PNVMEQUEUEHDR pQueue)
{
    return nvmeQueueGetEntriesBetweenPointers(pQueue->cEntries, pQueue->idxTail, pQueue->idxHead);
}
#endif /* unused */

/**
 * Sets the consumer pointer (Head) to the given value.
 *
 * @returns Amount of entries the new pointer advanced compared to the previous one.
 * @param   pQueue     The queue to set the pointer in.
 * @param   idxHeadNew The new head pointer of the queue.
 */
DECLINLINE(uint32_t) nvmeQueueConsumerSet(PNVMEQUEUEHDR pQueue, uint32_t idxHeadNew)
{
    uint32_t cEntriesConsumed = nvmeQueueGetEntriesBetweenPointers(pQueue->cEntries, idxHeadNew, pQueue->idxHead);
    ASMAtomicWriteU32(&pQueue->idxHead, idxHeadNew);

    return cEntriesConsumed;
}

#ifdef IN_RING3

/**
 * Returns the next free queue entry index or NVME_QUEUE_IS_FULL if the queue is full.
 *
 * @returns Index of the next free entry.
 * @retval NVME_QUEUE_IS_FULL if the queue is full.
 * @param   pQueue    The queue to check.
 */
DECLINLINE(uint32_t) nvmeQueueProducerGetFreeEntry(PNVMEQUEUEHDR pQueue)
{
    if (RT_UNLIKELY(nvmeQueueIsFull(pQueue)))
        return NVME_QUEUE_IS_FULL;

    return pQueue->idxTail;
}


/**
 * Returns the next occupied queue entry index or NVME_QUEUE_IS_EMPTY if the queue is empty.
 *
 * @returns Index of the next free entry.
 * @retval NVME_QUEUE_IS_FULL if the queue is full.
 * @param   pQueue    The queue to check.
 */
DECLINLINE(uint32_t) nvmeQueueConsumerGetNextEntry(PNVMEQUEUEHDR pQueue)
{
    if (nvmeQueueIsEmpty(pQueue))
    {
        Log2(("nvmeQueueConsumerGetNextEntry: Empty\n"));
        return NVME_QUEUE_IS_EMPTY;
    }

    Log2(("nvmeQueueConsumerGetNextEntry: %u\n", pQueue->idxHead));
    return pQueue->idxHead;
}


/**
 * Get the guest physical address of the next free entry in the queue.
 *
 * @returns Physical address of the next free entry.
 * @retval  NVME_QUEUE_IS_FULL_RTGCPHYS if the queue is full.
 */
DECLINLINE(RTGCPHYS) nvmeQueueProducerGetFreeEntryAddress(PNVMEQUEUEHDR pQueue)
{
    uint32_t idxEntry = nvmeQueueProducerGetFreeEntry(pQueue);
    if (RT_UNLIKELY(idxEntry == NVME_QUEUE_IS_FULL))
        return NVME_QUEUE_IS_FULL_RTGCPHYS;

    Assert(pQueue->fPhysCont);

    return pQueue->GCPhysBase + idxEntry * pQueue->cbEntry;
}


/**
 * Get the guest physical address of the next entry in the queue.
 *
 * @returns Physical address of the next free entry.
 * @retval  NVME_QUEUE_IS_EMPTY_RTGCPHYS if the queue is full.
 */
DECLINLINE(RTGCPHYS) nvmeQueueConsumerGetNextEntryAddress(PNVMEQUEUEHDR pQueue)
{
    uint32_t idxEntry = nvmeQueueConsumerGetNextEntry(pQueue);
    if (RT_UNLIKELY(idxEntry == NVME_QUEUE_IS_EMPTY))
        return NVME_QUEUE_IS_EMPTY_RTGCPHYS;

    Assert(pQueue->fPhysCont);

    return pQueue->GCPhysBase + idxEntry * pQueue->cbEntry;
}

/**
 * Advances producer pointer of the queue to the next entry.
 *
 * @returns Flag whether a wrap around occured.
 * @param   pQueue    The queue to advance.
 */
DECLINLINE(bool) nvmeQueueProducerAdvance(PNVMEQUEUEHDR pQueue)
{
    pQueue->idxTail = (pQueue->idxTail + 1) % RT_MAX(pQueue->cEntries, 1);
    Log2(("nvmeQueueProducerAdvance: %u\n", pQueue->idxTail));
    return pQueue->idxTail == 0;
}

/**
 * Advances consumer pointer of the queue to the next entry.
 *
 * @returns Flag whether a wrap around occured.
 * @param   pQueue    The queue to advance.
 */
DECLINLINE(bool) nvmeQueueConsumerAdvance(PNVMEQUEUEHDR pQueue)
{
    pQueue->idxHead = (pQueue->idxHead + 1) % RT_MAX(pQueue->cEntries, 1);
    Log2(("nvmeQueueConsumerAdvance: %u\n", pQueue->idxHead));
    return pQueue->idxHead == 0;
}

#endif /* IN_RING3 */

/**
 * Determine whether MSI/MSI-X is enabled for this PCI device. This
 * influences interrupt handling in NVMe.
 *
 * @note There should be a PCIDevXxx function for this.
 */
static bool nvmeIsMSIEnabled(PCPDMPCIDEV pPciDev)
{
    uint16_t uMsgCtl = PCIDevGetWord(pPciDev, NVME_PCI_MSI_CAP_OFS + VBOX_MSI_CAP_MESSAGE_CONTROL);
    uint16_t uMsixMsgCtl = PCIDevGetWord(pPciDev, NVME_PCI_MSIX_CAP_OFS + VBOX_MSIX_CAP_MESSAGE_CONTROL);
    return RT_BOOL(uMsgCtl & VBOX_PCI_MSI_FLAGS_ENABLE) || RT_BOOL(uMsixMsgCtl & VBOX_PCI_MSIX_FLAGS_ENABLE);
}

#ifdef LOG_ENABLED
/**
 * Returns the string version of the given state name.
 *
 * @returns Pointer to the string name of the given state.
 * @param   enmState    The state enum.
 */
static const char *nvmeGetStateName(NVMESTATE enmState)
{
    const char *pszState = NULL;

# define NVME_STATE_CASE(a_enmState) case NVMESTATE_##a_enmState: pszState = #a_enmState; break

    switch (enmState)
    {
        NVME_STATE_CASE(INVALID);
        NVME_STATE_CASE(INIT);
        NVME_STATE_CASE(READY);
        NVME_STATE_CASE(PAUSED);
        NVME_STATE_CASE(QUIESCING);
        NVME_STATE_CASE(RESETTING);
        NVME_STATE_CASE(FAULT);
        NVME_STATE_CASE(SHUTDOWN_PROCESSING);
        NVME_STATE_CASE(SHUTDOWN_COMPLETE);
        default:
            pszState = "<UNKNOWN>";
    }

# undef NVME_STATE_CASE

    return pszState;
}
#endif /* LOG_ENABLED */

/**
 * Updates the given interrupt vector state
 *
 * @param   pDevIns     The device instance.
 * @param   u32IntrVec  The interrupt vector to update.
 * @param   fSet        Flag whether to set the interrupt or clear it.
 */
static void nvmeIntrUpdate(PPDMDEVINS pDevIns, uint32_t u32IntrVec, bool fSet)
{
    if (fSet)
    {
        Log4(("Setting interrupt on vector %u\n", u32IntrVec));
        PDMDevHlpPCISetIrq(pDevIns, u32IntrVec, PDM_IRQ_LEVEL_HIGH);
    }
    else
    {
        Log4(("Clearing interrupt on vector %u\n", u32IntrVec));
        PDMDevHlpPCISetIrq(pDevIns, u32IntrVec, PDM_IRQ_LEVEL_LOW);
    }
}

/**
 * Locks the interrupt vector state.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   u32IntrVec  The interrupt vector.
 * @param   rcBusy      Status code to return on contention if not in R3.
 */
DECLINLINE(int) nvmeIntrVecLock(PPDMDEVINS pDevIns, PNVME pThis, uint32_t u32IntrVec, int rcBusy)
{
    PNVMEINTRVEC pIntr = &pThis->aIntrVecs[u32IntrVec];
    return PDMDevHlpCritSectEnter(pDevIns, &pIntr->CritSectIntrVec, rcBusy);
}

/**
 * Unlocks the interrupt vector state.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   u32IntrVec  The interrupt vector.
 */
DECLINLINE(void) nvmeIntrVecUnlock(PPDMDEVINS pDevIns, PNVME pThis, uint32_t u32IntrVec)
{
    PNVMEINTRVEC pIntr = &pThis->aIntrVecs[u32IntrVec];
    PDMDevHlpCritSectLeave(pDevIns, &pIntr->CritSectIntrVec);
}


#ifdef IN_RING3
/**
 * Posts the given number of events and updates the interrupt state of the given
 * interrupt vector.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   u32IntrVec  The interrupt vector.
 * @param   cEvts       Number of events.
 */
static void nvmeIntrVecPostEvents(PPDMDEVINS pDevIns, PNVME pThis, uint32_t u32IntrVec, int32_t cEvts)
{
    PNVMEINTRVEC pIntr = &pThis->aIntrVecs[u32IntrVec];

    int const rcLock = nvmeIntrVecLock(pDevIns, pThis, u32IntrVec, VERR_IGNORED); /* R3 only. */
    PDM_CRITSECT_RELEASE_ASSERT_RC_DEV(pDevIns, &pIntr->CritSectIntrVec, rcLock);

    int32_t cEvtsOld = ASMAtomicAddS32(&pIntr->cEvtsWaiting, cEvts);

    LogFlowFunc(("Posted %d events (%d waiting on interrupter to complete)\n",
                  cEvts, cEvtsOld + cEvts));

    if (   cEvtsOld + cEvts > 0
        && !pIntr->fIntrDisabled)
        nvmeIntrUpdate(pDevIns, u32IntrVec, true);

    nvmeIntrVecUnlock(pDevIns, pThis, u32IntrVec);
}
#endif /* IN_RING3 */


/**
 * Completes the given number of events for the given interrupt vector updating the
 * interrupt state.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   u32IntrVec  The interrupt vector.
 * @param   cEvts       Number of events to complete.
 */
static void nvmeIntrVecCompleteEvents(PPDMDEVINS pDevIns, PNVME pThis, uint32_t u32IntrVec, int32_t cEvts)
{
    PNVMEINTRVEC pIntr = &pThis->aIntrVecs[u32IntrVec];
    int32_t cEvtsOld = ASMAtomicSubS32(&pIntr->cEvtsWaiting, cEvts);

    LogFlowFunc(("Completed %d events (%d waiting on interrupter to complete)\n",
                  cEvts, cEvtsOld - cEvts));

    if (   cEvtsOld - cEvts <= 0
        && !pIntr->fIntrDisabled)
        nvmeIntrUpdate(pDevIns, u32IntrVec, false);
}

/**
 * Transitions to the fault state of the controller when a fatal error occurs
 * the controller can't recover from.
 *
 * @param   pThis     The NVMe controller shared instance data.
 */
static void nvmeStateSetFatalError(PNVME pThis)
{
    Log(("Setting controller into fault state (old state %s)\n", nvmeGetStateName(pThis->enmState)));
    ASMAtomicXchgSize(&pThis->enmState, NVMESTATE_FAULT);
}

/**
 * Read the capability (CAP) register.
 */
static VBOXSTRICTRC HcCap_r(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, iReg);
    *pu64Value = NVME_CAP_MPSMAX(0) /* Indicates 4K page size. */
               | NVME_CAP_MPSMIN(0) /* Indicates 4K page size. */
               | NVME_CAP_CCS_NVM
               /** @todo | NMVE_CAP_NSSRS */
               | NVME_CAP_DSTRD(0) /* Doorbell registers are packed. */
               | NVME_CAP_TO(pThis->cTimeoutMax)
               /** @todo | NVME_CAP_AMS_WRR */
               /** @todo | NVME_CAP_AMS_VEND_SPEC */
               | NVME_CAP_CQR
               | NVME_CAP_MQES(pThis->cQueueEntriesMax);
    return VINF_SUCCESS;
}

/**
 * Read the version (VS) register.
 */
static VBOXSTRICTRC HcVs_r(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    *pu64Value = NVME_VS_MJR(1) | NVME_VS_MNR(2); /* NVM Express version 1.2 supported. */
    return VINF_SUCCESS;
}

/**
 * Read the interrupt mask set (INTMS) register.
 */
static VBOXSTRICTRC HcIntrMaskSet_r(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, iReg);
    *pu64Value = pThis->u32IntrMask;
    return VINF_SUCCESS;
}

/**
 * Write the interrupt mask set (INTMS) register.
 */
static VBOXSTRICTRC HcIntrMaskSet_w(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t u64Value)
{
    RT_NOREF(iReg);
    uint32_t u32Mask = (uint32_t)u64Value;

    /*
     * Clear the interrupt if the corresponding vector was masked out and has an interrupt pending.
     *
     * The stock Windows driver shipping with Windows 10 at least has a bug in the
     * interrupt handler if pin based interrupts are used. Even though there is only one
     * interrupt vector available vector 0 it always tries to mask vector 1 resulting in
     * a stuck interrupt handler because the interrupt is never deasserted blocking the CPU.
     *
     * See chapter 7.5.1 "Pin Based, Single MSI, and Multiple MSI Behavior" of the 1.2 NVMe spec:
     *
     *     If MSIs are not enabled, IS[0] being a one causes the PCI interrupt line to be active (electrical '0'). If
     *     MSIs are enabled, any change to the IS register that causes an unmasked status bit to transition from
     *     zero to one or clearing of a mask bit whose corresponding status bit is set shall cause an MSI to be sent.
     *     Therefore, while in wire mode, a single wire remains active, while in MSI mode, several messages may
     *     be sent, as each edge triggered event on a port shall cause a new message.
     *
     * As a workaround call nvmeIntrUpdate always on the first interrupt vector if MSI is not enabled.
     */
    if (!nvmeIsMSIEnabled(pDevIns->apPciDevs[0]))
        u32Mask |= 0x1;

    for (uint32_t idxIntrVec = 0; idxIntrVec < NVME_INTR_VEC_MAX; idxIntrVec++)
    {
        if (   u32Mask & RT_BIT_32(idxIntrVec)
            && !ASMAtomicXchgBool(&pThis->aIntrVecs[idxIntrVec].fIntrDisabled, true)
            && ASMAtomicReadS32(&pThis->aIntrVecs[idxIntrVec].cEvtsWaiting) > 0)
            nvmeIntrUpdate(pDevIns, idxIntrVec, false /* fSet */);
    }
    ASMAtomicOrU32(&pThis->u32IntrMask, u32Mask);
    return VINF_SUCCESS;
}

/**
 * Read the interrupt mask clear (INTMC) register.
 */
static VBOXSTRICTRC HcIntrMaskClr_r(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, iReg);
    *pu64Value = pThis->u32IntrMask;
    return VINF_SUCCESS;
}

/**
 * Write the interrupt mask clear (INTMC) register.
 */
static VBOXSTRICTRC HcIntrMaskClr_w(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t u64Value)
{
    RT_NOREF(iReg);
    uint32_t u32Mask = (uint32_t)u64Value;

    /* See comment in HcIntrMaskSet_w() above. */
    if (!nvmeIsMSIEnabled(pDevIns->apPciDevs[0]))
        u32Mask |= 0x1;

    for (uint32_t idxIntrVec = 0; idxIntrVec < NVME_INTR_VEC_MAX; idxIntrVec++)
    {
        /* Set the interrupt if the vector was unmasked and has an interrupt pending. */
        if (   u32Mask & RT_BIT_32(idxIntrVec)
            && ASMAtomicXchgBool(&pThis->aIntrVecs[idxIntrVec].fIntrDisabled, false)
            && ASMAtomicReadS32(&pThis->aIntrVecs[idxIntrVec].cEvtsWaiting) > 0)
            nvmeIntrUpdate(pDevIns, idxIntrVec, true /* fSet */);
    }
    ASMAtomicAndU32(&pThis->u32IntrMask, ~u32Mask);
    return VINF_SUCCESS;
}

/**
 * Read the controller configuration (CC) register.
 */
static VBOXSTRICTRC HcCtrlCfg_r(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, iReg);
    uint32_t u32Val = 0;
    u32Val |= NVME_CC_IOCQES_SET(pThis->u32IoCompletionQueueEntrySize);
    u32Val |= NVME_CC_IOSQES_SET(pThis->u32IoSubmissionQueueEntrySize);
    u32Val |= NVME_CC_SHN_SET(pThis->uShutdwnNotifierLast);
    u32Val |= NVME_CC_AMS_SET(pThis->uAmsSet);
    u32Val |= NVME_CC_MPS_SET(pThis->uMpsSet);
    u32Val |= NVME_CC_CSS_SET(pThis->uCssSet);

    NVMESTATE enmState = (NVMESTATE)ASMAtomicReadU32((volatile uint32_t *)&pThis->enmState);
    if (   enmState == NVMESTATE_READY
        || enmState == NVMESTATE_PAUSED
        || enmState == NVMESTATE_SHUTDOWN_PROCESSING
        || enmState == NVMESTATE_SHUTDOWN_COMPLETE
        || enmState == NVMESTATE_QUIESCING)
        u32Val |= NVME_CC_EN;

    *pu64Value = u32Val;
    return VINF_SUCCESS;
}

/**
 * Write the controller configuration (CC) register.
 */
static VBOXSTRICTRC HcCtrlCfg_w(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t u64Value)
{
    RT_NOREF(pDevIns, iReg);

    pThis->u32IoCompletionQueueEntrySize = NVME_CC_IOCQES_RET(u64Value);
    pThis->u32IoSubmissionQueueEntrySize = NVME_CC_IOSQES_RET(u64Value);
    pThis->uShutdwnNotifierLast          = NVME_CC_SHN_RET(u64Value);
    pThis->uAmsSet                       = NVME_CC_AMS_RET(u64Value);
    pThis->uMpsSet                       = NVME_CC_MPS_RET(u64Value);
    pThis->uCssSet                       = NVME_CC_CSS_RET(u64Value);

    pThis->cbPage                        = RT_BIT_32(pThis->uMpsSet);

    NVMESTATE enmStateOld = (NVMESTATE)ASMAtomicReadU32((volatile uint32_t *)&pThis->enmState);

    if (   u64Value & NVME_CC_EN
        && enmStateOld == NVMESTATE_INIT)
    {
        /* Check for a sane state to start processing. */
        if (   pThis->uAmsSet != NVME_CC_AMS_RR
            || pThis->uCssSet != NVME_CC_CSS_NVM
            || pThis->uMpsSet != 12 /* 4KB */)
        {
            /* Switch to fault state. */
            ASMAtomicXchgSize(&pThis->enmState, NVMESTATE_FAULT);
        }
        else
        {
            /* Transition to ready state. */
            bool fXchg = false;
            ASMAtomicCmpXchgSize(&pThis->enmState, NVMESTATE_READY, enmStateOld, fXchg);
            Assert(fXchg);
            ASMAtomicIncU32(&pThis->cActivities);
        }
    }
    else if (   !(u64Value & NVME_CC_EN)
             && (   enmStateOld == NVMESTATE_READY
                 || enmStateOld == NVMESTATE_PAUSED
                 || enmStateOld == NVMESTATE_SHUTDOWN_COMPLETE))
    {
#ifndef IN_RING3
        return VINF_IOM_R3_MMIO_WRITE;
#else
        /* Disable processing. */
        bool fXchg = false;
        ASMAtomicCmpXchgSize(&pThis->enmState, NVMESTATE_QUIESCING, enmStateOld, fXchg);
        /*
         * If there is no outstanding request waiting for completion or an active thread
         * we can reset the controller state now.
         *
         * When a shutdown processing completed in the previous state there shouldn't be
         * any activity.
         */
        uint32_t cActivitiesNew = 0;
        if (enmStateOld != NVMESTATE_SHUTDOWN_COMPLETE)
            cActivitiesNew = ASMAtomicDecU32(&pThis->cActivities);
        else
            Assert(pThis->cActivities == 0);

        if (!cActivitiesNew)
            nvmeR3CtrlReset(pDevIns, pThis, PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC));
#endif
    }

    if (   pThis->uShutdwnNotifierLast != NVME_CC_SHN_NO_NOTIFICATION
        && enmStateOld != NVMESTATE_FAULT
        && enmStateOld != NVMESTATE_SHUTDOWN_PROCESSING
        && enmStateOld != NVMESTATE_SHUTDOWN_COMPLETE)
    {
        bool fXchg = false;
        uint32_t cActivities = 0;

        if (   enmStateOld != NVMESTATE_QUIESCING
            && enmStateOld != NVMESTATE_INIT)
        {
            ASMAtomicXchgSize(&pThis->enmState, NVMESTATE_SHUTDOWN_PROCESSING);
            enmStateOld = NVMESTATE_SHUTDOWN_PROCESSING;

            /*
             * If there is no activity on the controller except than ours we can mark the shutdown
             * as complete.
             */
            cActivities = ASMAtomicDecU32(&pThis->cActivities);
        }

        if (!cActivities)
            ASMAtomicCmpXchgSize(&pThis->enmState, NVMESTATE_SHUTDOWN_COMPLETE, enmStateOld, fXchg);
    }

    return VINF_SUCCESS;
}

/**
 * Read the controller status (CSTS) register.
 */
static VBOXSTRICTRC HcCtrlSts_r(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, iReg);

    uint32_t u32Val;
    switch (pThis->enmState)
    {
        case NVMESTATE_PAUSED:
            u32Val = NVME_CSTS_PP | NVME_CSTS_RDY | NVME_CSTS_SHST_SET(NVME_CSTS_SHST_NORMAL);
            break;
        case NVMESTATE_QUIESCING:
        case NVMESTATE_RESETTING:
        case NVMESTATE_INIT:
            u32Val = NVME_CSTS_PP | NVME_CSTS_SHST_SET(NVME_CSTS_SHST_NORMAL);
            break;
        case NVMESTATE_READY:
            u32Val = NVME_CSTS_RDY | NVME_CSTS_SHST_SET(NVME_CSTS_SHST_NORMAL);
            break;
        case NVMESTATE_FAULT:
            u32Val = NVME_CSTS_CFS | NVME_CSTS_SHST_SET(NVME_CSTS_SHST_NORMAL);
            break;
        case NVMESTATE_SHUTDOWN_PROCESSING:
            u32Val = NVME_CSTS_PP | NVME_CSTS_SHST_SET(NVME_CSTS_SHST_OCCURRING);
            break;
        case NVMESTATE_SHUTDOWN_COMPLETE:
            u32Val = NVME_CSTS_RDY | NVME_CSTS_SHST_SET(NVME_CSTS_SHST_COMPLETE);
            break;
        default:
            u32Val = NVME_CSTS_SHST_SET(NVME_CSTS_SHST_NORMAL);
    }

    /** @todo NVMe Subsystem reset bit. */

    *pu64Value = u32Val;
    return VINF_SUCCESS;
}

/**
 * Write the controller status (CSTS) register.
 */
static VBOXSTRICTRC HcCtrlSts_w(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t u64Value)
{
    /*
     * Only the NVM Subsystem Reset Occurred bit is writeable.
     * Because the feature is not supported yet there is nothing to do here.
     */
    RT_NOREF(pDevIns, pThis, iReg, u64Value);
    return VINF_SUCCESS;
}

/**
 * Read the NVM subsystem reset (NSSR) register.
 */
static VBOXSTRICTRC HcNvmRst_r(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, pThis, iReg);
    *pu64Value = 0;
    return VINF_SUCCESS;
}

/**
 * Write the NVM subsystem reset (NSSR) register.
 */
static VBOXSTRICTRC HcNvmRst_w(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t u64Value)
{
    /* Not supported for now. */
    RT_NOREF(pDevIns, pThis, iReg, u64Value);
    return VINF_SUCCESS;
}

/**
 * Read the admin queue attributes (AQA) register.
 */
static VBOXSTRICTRC HcAdmQueueAttr_r(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t *pu64Value)
{
    PNVMEQUEUESUBM pAdmQueueSubm = &pThis->aQueuesSubm[NVME_ADM_QUEUE_ID];
    PNVMEQUEUECOMP pAdmQueueComp = &pThis->aQueuesComp[NVME_ADM_QUEUE_ID];

    *pu64Value =  NVME_AQA_ACQS_SET(pAdmQueueSubm->Hdr.cEntries - 1)  /* 0's based value */
                | NVME_AQA_ASQS_SET(pAdmQueueComp->Hdr.cEntries - 1); /* 0's based value */

    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Write the admin queue attributes (AQA) register.
 */
static VBOXSTRICTRC HcAdmQueueAttr_w(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t u64Value)
{
    PNVMEQUEUESUBM pAdmQueueSubm = &pThis->aQueuesSubm[NVME_ADM_QUEUE_ID];
    PNVMEQUEUECOMP pAdmQueueComp = &pThis->aQueuesComp[NVME_ADM_QUEUE_ID];
    uint32_t u32Val = (uint32_t)u64Value;

    if (pThis->enmState == NVMESTATE_INIT)
    {
        pAdmQueueSubm->Hdr.cEntries = NVME_AQA_ACQS_RET(u32Val) + 1;
        pAdmQueueComp->Hdr.cEntries = NVME_AQA_ASQS_RET(u32Val) + 1;
    }

    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Read the admin submission queue base address (ASQ) register.
 */
static VBOXSTRICTRC HcAdmQueueSubmBase_r(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t *pu64Value)
{
    PNVMEQUEUESUBM pAdmQueueSubm = &pThis->aQueuesSubm[NVME_ADM_QUEUE_ID];

    *pu64Value = pAdmQueueSubm->Hdr.GCPhysBase;

    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Write the admin submission queue base address (ASQ) register.
 */
static VBOXSTRICTRC HcAdmQueueSubmBase_w(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t u64Value)
{
    PNVMEQUEUESUBM pAdmQueueSubm = &pThis->aQueuesSubm[NVME_ADM_QUEUE_ID];

    if (pThis->enmState == NVMESTATE_INIT)
    {
        pAdmQueueSubm->Hdr.GCPhysBase = NVME_ASQ_ASQB_RET(u64Value);
        ASMAtomicXchgU32((volatile uint32_t *)&pAdmQueueSubm->Hdr.enmState, NVMEQUEUESTATE_ALLOCATED);
    }

    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Read the admin completion queue base address (ACQ) register.
 */
static VBOXSTRICTRC HcAdmQueueComplBase_r(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t *pu64Value)
{
    PNVMEQUEUECOMP pAdmQueueComp = &pThis->aQueuesComp[NVME_ADM_QUEUE_ID];

    *pu64Value = pAdmQueueComp->Hdr.GCPhysBase;

    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Write the admin completion queue base address (ACQ) register.
 */
static VBOXSTRICTRC HcAdmQueueComplBase_w(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t u64Value)
{
    PNVMEQUEUECOMP pAdmQueueComp = &pThis->aQueuesComp[NVME_ADM_QUEUE_ID];

    if (pThis->enmState == NVMESTATE_INIT)
    {
        pAdmQueueComp->Hdr.GCPhysBase = NVME_ACQ_ACQB_RET(u64Value);
        ASMAtomicXchgU32((volatile uint32_t *)&pAdmQueueComp->Hdr.enmState, NVMEQUEUESTATE_ALLOCATED);
    }

    RT_NOREF(pDevIns, iReg);
    return VINF_SUCCESS;
}

/**
 * Read the controller memory buffer location (CMBLOC) register.
 */
static VBOXSTRICTRC HcCtrlMemBufLoc_r(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, iReg);
    if (pThis->cbCtrlMemBuf)
        *pu64Value = NVME_CMBLOC_OFST(0) | NVME_CMBLOC_BIR(NVME_PCI_MEM_CTRL_BUF_BAR);
    else
        *pu64Value = 0;
    return VINF_SUCCESS;
}

/**
 * Read the controller memory buffer size (CMBSZ) register.
 */
static VBOXSTRICTRC HcCtrlMemBufSize_r(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t *pu64Value)
{
    RT_NOREF(pDevIns, iReg);
    *pu64Value = pThis->u32CtrlMemBufSz;
    return VINF_SUCCESS;
}

/**
 * Write to a submission queue doorbell register.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   iQueueId    The queue ID.
 * @param   u32Tail     The new value for the queue tail.
 */
static VBOXSTRICTRC nvmeQueueSubmWrite(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iQueueId, uint32_t u32Tail)
{
    LogFlowFunc(("pThis=%#p iQueueId=%u u32Tail=%u\n", pThis, iQueueId, u32Tail));

    if (   iQueueId < pThis->cQueuesSubmMax
        && pThis->enmState == NVMESTATE_READY)
    {
        PNVMEQUEUESUBM pQueue = &pThis->aQueuesSubm[iQueueId];

        if (RT_LIKELY(ASMAtomicUoReadU32((volatile uint32_t *)&pQueue->Hdr.enmState) == NVMEQUEUESTATE_ALLOCATED))
        {
            if (RT_LIKELY(   u32Tail < pQueue->Hdr.cEntries
                          && u32Tail != pQueue->Hdr.idxTail))
            {
                ASMAtomicWriteU32(&pQueue->Hdr.idxTail, u32Tail);
                int rc = PDMDevHlpSUPSemEventSignal(pDevIns, pQueue->hEvtProcess);
                if (RT_FAILURE(rc))
                    nvmeStateSetFatalError(pThis);
            }
            else
            {
#ifndef IN_RING3
                return VINF_IOM_R3_MMIO_WRITE;
#else
                /* Notify the guest about the invalid write. */
                nvmeR3AsyncEvtComplete(pDevIns, pThis, PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC), NVME_ASYNC_EVT_TYPE_ERROR,
                                       NVME_ASYNC_EVT_INFO_ERROR_DOORBELL_VALUE_INVALID, NVME_LOG_PAGE_ID_ERROR_INFORMATION);
#endif
            }
        }
        else if (iQueueId == NVME_ADM_QUEUE_ID)
        {
#ifndef IN_RING3
            return VINF_IOM_R3_MMIO_WRITE;
#else
            /* Notify the guest about the invalid write. */
            nvmeR3AsyncEvtComplete(pDevIns, pThis, PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC), NVME_ASYNC_EVT_TYPE_ERROR,
                                   NVME_ASYNC_EVT_INFO_ERROR_DOORBELL_INVALID, NVME_LOG_PAGE_ID_ERROR_INFORMATION);
#endif
        }
    }
    else
        nvmeStateSetFatalError(pThis);

    return VINF_SUCCESS;
}

/**
 * Write to a completion queue doorbell register.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   iQueueId    The queue ID.
 * @param   u32Head     The new value for the queue head.
 */
static VBOXSTRICTRC nvmeQueueCompWrite(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iQueueId, uint32_t u32Head)
{
    LogFlowFunc(("pThis=%#p iQueueId=%u u32Head=%u\n", pThis, iQueueId, u32Head));

    if (   iQueueId < RT_MIN(pThis->cQueuesSubmMax, RT_ELEMENTS(pThis->aQueuesComp))
        && pThis->enmState == NVMESTATE_READY)
    {
        PNVMEQUEUECOMP pQueue = &pThis->aQueuesComp[iQueueId];

        if (RT_LIKELY(ASMAtomicUoReadU32((volatile uint32_t *)&pQueue->Hdr.enmState) == NVMEQUEUESTATE_ALLOCATED))
        {
#ifndef IN_RING3
            /*
             * For the case where where we have waiting entries because the queue is full we need to return
             * to R3 to complete them .Shouldn't be a performance bottleneck as this problem should not
             * happen frequently (any checked OS will not overload the queues).
             */
            if (RT_UNLIKELY(pQueue->cWaiters))
                return VINF_IOM_R3_MMIO_WRITE;
#endif

            /* Lock the interrupt vector if the queue can generate interrupts. */
            if (pQueue->fIntrEnabled)
            {
                int rc = nvmeIntrVecLock(pDevIns, pThis, pQueue->u32IntrVec, VINF_IOM_R3_MMIO_WRITE);
                if (rc != VINF_SUCCESS)
                    return rc;
            }

            if (RT_LIKELY(   u32Head < pQueue->Hdr.cEntries
                          && u32Head != pQueue->Hdr.idxHead))
            {
                LogFlowFunc(("u32Head=%u cEntries=%u pQueue->Hdr.idxTail=%u pQueue->Hdr.idxHead=%u\n",
                             u32Head, pQueue->Hdr.cEntries, pQueue->Hdr.idxTail, pQueue->Hdr.idxHead));

                /*
                 * Ignore writes which are outside of the range of the current head and tail pointers.
                 * The Windows 10 stornvme.sys driver occasionally tries to set the head backwards in an
                 * SMP configuration causing the controller emulation to trip and not generate any interrupts.
                 * See @bugref{9916}.
                 */
                if (   (   pQueue->Hdr.idxHead < pQueue->Hdr.idxTail
                        && u32Head > pQueue->Hdr.idxHead
                        && u32Head <= pQueue->Hdr.idxTail)
                    || (   pQueue->Hdr.idxHead > pQueue->Hdr.idxTail
                        && (   u32Head > pQueue->Hdr.idxHead
                            || u32Head <= pQueue->Hdr.idxTail)))
                {
                    uint32_t cCqEntriesCompleted = nvmeQueueConsumerSet(&pQueue->Hdr, u32Head);

                    if (pQueue->fIntrEnabled)
                        nvmeIntrVecCompleteEvents(pDevIns, pThis, pQueue->u32IntrVec, cCqEntriesCompleted);
                    else
                        LogFlowFunc(("Completed %u commands on a completion queue with interrupts disabled\n", cCqEntriesCompleted));

#ifdef IN_RING3
                    /* Complete any pending waiters while we are here. */
                    if (RT_UNLIKELY(pQueue->cWaiters))
                    {
                        PNVMER3 pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC);
                        PNVMEQUEUECOMPR3 pQueueR3 = &pThisCC->aQueuesComp[iQueueId];

                        if (pQueue->fIntrEnabled)
                            nvmeIntrVecUnlock(pDevIns, pThis, pQueue->u32IntrVec);

                        RTSemFastMutexRequest(pQueueR3->hMtx);
                        while (cCqEntriesCompleted && pQueue->cWaiters)
                        {
                            PNVMECOMPQUEUEWAITER pWaiter = RTListGetFirst(&pQueueR3->LstCompletionsWaiting,
                                                                          NVMECOMPQUEUEWAITER, NdLstQueue);
                            RTGCPHYS GCPhysCe = nvmeQueueProducerGetFreeEntryAddress(&pQueue->Hdr);

                            int rc = nvmeR3CompQueueEntryPost(pDevIns,  pThis, pThisCC, GCPhysCe, pQueue, pWaiter->pQueueSubm,
                                                              pWaiter->u16Cid, pWaiter->u8Sct, pWaiter->u8Sc,
                                                              pWaiter->u32CmdSpecific, pWaiter->fMore, pWaiter->fDnr);
                            if (RT_FAILURE(rc))
                                nvmeStateSetFatalError(pThis);

                            RTListNodeRemove(&pWaiter->NdLstQueue);
                            cCqEntriesCompleted--;
                            pQueue->cWaiters--;
                            pQueue->fOverloaded = false;

                            uintptr_t idxQueueSubm = pWaiter->pQueueSubm - &pThis->aQueuesSubm[0];
                            Assert(idxQueueSubm < RT_ELEMENTS(pThisCC->aQueuesSubm));
                            nvmeR3WrkThrdKick(pDevIns, pThisCC->aQueuesSubm[idxQueueSubm].pWrkThrdR3);

                            RTMemFree(pWaiter);
                        }
                        RTSemFastMutexRelease(pQueueR3->hMtx);


                        if (pQueue->fIntrEnabled)
                        {
                            int rc = nvmeIntrVecLock(pDevIns, pThis, pQueue->u32IntrVec, VERR_IGNORED);
                            AssertRC(rc);
                        }
                    }
#endif
                }
            }
#if 0
            else
            {
#ifndef IN_RING3
                return VINF_IOM_R3_MMIO_WRITE;
#else
                /* Notify the guest about the invalid write. */
                nvmeR3AsyncEvtComplete(pDevIns, pThis, PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC), NVME_ASYNC_EVT_TYPE_ERROR,
                                       NVME_ASYNC_EVT_INFO_ERROR_DOORBELL_VALUE_INVALID, NVME_LOG_PAGE_ID_ERROR_INFORMATION);
#endif
            }
#endif

            if (pQueue->fIntrEnabled)
                nvmeIntrVecUnlock(pDevIns, pThis, pQueue->u32IntrVec);
        }
        else if (iQueueId == NVME_ADM_QUEUE_ID)
        {
            /* Set the fatal error bit as we can't report any error through the admin completion queue. */
            nvmeStateSetFatalError(pThis);
        }
        else
        {
#ifndef IN_RING3
            return VINF_IOM_R3_MMIO_WRITE;
#else
            /* Notify the guest about the invalid write. */
            nvmeR3AsyncEvtComplete(pDevIns, pThis, PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC), NVME_ASYNC_EVT_TYPE_ERROR,
                                   NVME_ASYNC_EVT_INFO_ERROR_DOORBELL_INVALID, NVME_LOG_PAGE_ID_ERROR_INFORMATION);
#endif
        }
    }
    else
        nvmeStateSetFatalError(pThis);

    return VINF_SUCCESS;
}

/**
 * NVMe register access routines.
 */
typedef struct
{
    const char    *pszName;
    VBOXSTRICTRC (*pfnRead )(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t *pu64Value);
    VBOXSTRICTRC (*pfnWrite)(PPDMDEVINS pDevIns, PNVME pThis, uint32_t iReg, uint64_t u64Value);
    bool           f64BitReg;
} NVMEREGACC;
typedef const NVMEREGACC *PCNVMEREGACC;

/**
 * Operational registers descriptor table.
 */
static const NVMEREGACC g_aHcRegs[] =
{
    {"CAP" ,            HcCap_r,                   NULL,                  true  },
    {"CAPP" ,           NULL,                      NULL,                  true  },
    {"VS",              HcVs_r,                    NULL,                  false },
    {"INTMS",           HcIntrMaskSet_r,           HcIntrMaskSet_w,       false },
    {"INTMC",           HcIntrMaskClr_r,           HcIntrMaskClr_w,       false },
    {"CC",              HcCtrlCfg_r,               HcCtrlCfg_w,           false },
    {"Reserved",        NULL,                      NULL,                  false },
    {"CSTS",            HcCtrlSts_r,               HcCtrlSts_w,           false },
    {"NSSR",            HcNvmRst_r,                HcNvmRst_w,            false },
    {"AQA",             HcAdmQueueAttr_r,          HcAdmQueueAttr_w,      false },
    {"ASQ",             HcAdmQueueSubmBase_r,      HcAdmQueueSubmBase_w,  true  },
    {"ASQP" ,           NULL,                      NULL,                  true  },
    {"ACQ",             HcAdmQueueComplBase_r,     HcAdmQueueComplBase_w, true  },
    {"ACQP" ,           NULL,                      NULL,                  true  },
    {"CMBLOC",          HcCtrlMemBufLoc_r,         NULL,                  false },
    {"CMBSZ",           HcCtrlMemBufSize_r,        NULL,                  false }
};

/**
 * Read from a NVMe controller register.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   offReg      Register offset.
 * @param   pv          Where to store the read data.
 * @param   cb          Amount of bytes to read.
 */
static VBOXSTRICTRC nvmeRegRead(PPDMDEVINS pDevIns, PNVME pThis, uint32_t offReg, void *pv, unsigned cb)
{
    VBOXSTRICTRC rcStrict = VINF_IOM_MMIO_UNUSED_FF;

    LogFlowFunc(("pThis=%#p offReg=%u pv=%#p cb=%u\n", pThis, offReg, pv, cb));

    AssertReturn(cb == 4 || cb == 8, VINF_IOM_MMIO_UNUSED_FF);
    AssertReturn(!(offReg & 0x3), VINF_IOM_MMIO_UNUSED_FF);

    if (offReg < NVME_HC_CTRL_REG_SIZE)
    {
        uint32_t iReg = offReg >> 2;
        if (iReg < RT_ELEMENTS(g_aHcRegs))
        {
            bool fReadHigh32BitsOf64BitReg = false;
            PCNVMEREGACC pReg = &g_aHcRegs[iReg];

            if (pReg->f64BitReg && !pReg->pfnRead)
            {
                pReg--;
                iReg--;
                fReadHigh32BitsOf64BitReg = true;
            }

            if (RT_LIKELY(pReg->pfnRead))
            {
                uint64_t u64Val = 0;
                rcStrict = pReg->pfnRead(pDevIns, pThis, iReg, &u64Val);
                LogFlow(("Read register \"%s\" -> %Rrc (%llx)\n", pReg->pszName, VBOXSTRICTRC_VAL(rcStrict), u64Val));
                if (RT_SUCCESS(rcStrict))
                {
                    if (cb == 8)
                        *(uint64_t *)pv = u64Val;
                    else if (fReadHigh32BitsOf64BitReg)
                        *(uint32_t *)pv = (uint32_t)(u64Val >> 32);
                    else
                        *(uint32_t *)pv = (uint32_t)u64Val;
                }
            }
        }
    }
    else if (offReg >= NVME_HC_DOORBELL_REG_OFF)
    {
        /*
         * These registers should not be read by the guest (chapter 3.1.13 and 3.1.14)
         * and the return value is vendor specific, so we will just return 0 here
         * to keep it simple.
         */
        rcStrict = VINF_IOM_MMIO_UNUSED_00;
    }
    /* else: Reserved and command set specific space between the global and doorbell registers. */

    return rcStrict;
}

/**
 * Write to a NVMe controller register.
 *
 * @returns Strict VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   offReg      Register offset.
 * @param   pv          The data to write.
 * @param   cb          Amount of bytes to write.
 */
static VBOXSTRICTRC nvmeRegWrite(PPDMDEVINS pDevIns, PNVME pThis, uint32_t offReg, void const *pv, unsigned cb)
{
    VBOXSTRICTRC rcStrict = VINF_SUCCESS;

    LogFlowFunc(("pThis=%#p offReg=%u pv=%#p cb=%u\n", pThis, offReg, pv, cb));

    AssertReturn(cb == 4 || cb == 8, VINF_SUCCESS);
    AssertReturn(!(offReg & 0x3), VINF_SUCCESS);

    if (offReg < NVME_HC_CTRL_REG_SIZE)
    {
        uint32_t iReg = offReg >> 2;
        if (iReg < RT_ELEMENTS(g_aHcRegs))
        {
            uint64_t u64Val = 0;
            PCNVMEREGACC pReg = &g_aHcRegs[iReg];

            if (cb == 8)
                u64Val = *(uint64_t *)pv;
            else
            {
                Assert(cb == 4);
                u64Val = *(uint32_t *)pv;
            }

            /* If the write size matches the register just do it. */
            if (   (cb == 8 && pReg->f64BitReg)
                || (cb == 4 && !pReg->f64BitReg))
            {
                if (pReg->pfnWrite)
                {
                    rcStrict = pReg->pfnWrite(pDevIns, pThis, iReg, u64Val);
                    LogFlow(("Written register \"%s\" (%llx) -> %Rrc\n", pReg->pszName, u64Val, VBOXSTRICTRC_VAL(rcStrict)));
                }
            }
            else if (cb == 4)
            {
                /*
                 * Only aligned 32bit writes to a 64bit register are allowed. The other
                 * accesses are not covered by the specification.
                 */
                uint64_t u64Read = 0;
                bool fWriteHigh32BitsOf64BitReg = false;

                if (   iReg > 0
                    && pReg->f64BitReg
                    && !pReg->pfnWrite)
                {
                    pReg--;
                    iReg--;
                    fWriteHigh32BitsOf64BitReg = true;
                }

                if (pReg->pfnWrite)
                {
                    Assert(pReg->pfnRead);
                    /* Read complete value first. */
                    rcStrict = pReg->pfnRead(pDevIns, pThis, iReg, &u64Read);
                    if (RT_SUCCESS(rcStrict))
                    {
                        /* Assemble the final value and write back. */
                        if (fWriteHigh32BitsOf64BitReg)
                            u64Read = (u64Read & UINT64_C(0xffffffff)) | (u64Val << 32);
                        else
                            u64Read = (u64Read & UINT64_C(0xffffffff00000000)) | u64Val;

                        rcStrict = pReg->pfnWrite(pDevIns, pThis, iReg, u64Read);
                        LogFlow(("Written register \"%s\" (%llx) -> %Rrc\n", pReg->pszName, u64Read, VBOXSTRICTRC_VAL(rcStrict)));
                    }
                }
            }
        }
    }
    else if (offReg >= NVME_HC_DOORBELL_REG_OFF)
    {
        /* Writes should be aligned and 32bits wide. */
        AssertReturn(cb == 4, VINF_SUCCESS);

        uint32_t iQueue = (offReg - NVME_HC_DOORBELL_REG_OFF) >> 2;
        uint32_t u32Val = NVME_SQTDBL_SQT_RET(*(uint32_t *)pv);

        if (iQueue & 0x1)
            rcStrict = nvmeQueueCompWrite(pDevIns, pThis, (iQueue - 1) >> 1, u32Val);
        else
            rcStrict = nvmeQueueSubmWrite(pDevIns, pThis, iQueue >> 1, u32Val);
    }
    /* else: Reserved and command set specific space between the global and doorbell registers. */

    return rcStrict;
}

/**
 * @callback_method_impl{FNIOMMMIONEWREAD}
 *
 * We only accept 32-bit and 64-bit writes that are aligned.
 */
static DECLCALLBACK(VBOXSTRICTRC) nvmeMmioRead(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb)
{
    PNVME pThis = PDMDEVINS_2_DATA(pDevIns, PNVME);
    RT_NOREF(pvUser);
    Assert(off == (uint32_t)off);

    return nvmeRegRead(pDevIns, pThis, (uint32_t)off, pv, cb);
}


/**
 * @callback_method_impl{FNIOMMMIONEWWRITE}
 *
 * We only accept 32-bit writes that are 32-bit aligned.
 */
static DECLCALLBACK(VBOXSTRICTRC) nvmeMmioWrite(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb)
{
    PNVME pThis = PDMDEVINS_2_DATA(pDevIns, PNVME);
    RT_NOREF(pvUser);
    Assert(off == (uint32_t)off);

    return nvmeRegWrite(pDevIns, pThis, (uint32_t)off, pv, cb);
}


/**
 * @callback_method_impl{FNIOMIOPORTNEWOUT}
 *
 * I/O port handler for writes to the index/data register pair.
 */
static DECLCALLBACK(VBOXSTRICTRC) nvmeIdxDataWrite(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t u32, unsigned cb)
{
    PNVME        pThis    = PDMDEVINS_2_DATA(pDevIns, PNVME);
    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    RT_NOREF(pvUser);

    AssertReturn(offPort < 8, VINF_SUCCESS); /* Paranoia^2 given that we only register 8 ports. */
    ASSERT_GUEST_MSG(cb == 4, ("%#x LB %u\n", offPort, cb));

    if (offPort == 0)
    {
        /* Write the index register. */
        pThis->u32RegIdx = u32;
    }
    else
    {
        ASSERT_GUEST_MSG(offPort == 4, ("%#x LB %u\n", offPort, cb));
        rcStrict = nvmeRegWrite(pDevIns, pThis, pThis->u32RegIdx, &u32, cb);
        if (rcStrict == VINF_IOM_R3_MMIO_WRITE)
            rcStrict = VINF_IOM_R3_IOPORT_WRITE;
    }

    Log2(("#%d nvmeIdxDataWrite: pu32=%p:{%.*Rhxs} cb=%d offPort=%#x rc=%Rrc\n",
          pDevIns->iInstance, &u32, cb, &u32, cb, offPort, VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}

/**
 * @callback_method_impl{FNIOMIOPORTNEWIN}
 *
 * I/O port handler for reads from the index/data register pair.
 */
static DECLCALLBACK(VBOXSTRICTRC) nvmeIdxDataRead(PPDMDEVINS pDevIns, void *pvUser, RTIOPORT offPort, uint32_t *pu32, unsigned cb)
{
    PNVME        pThis    = PDMDEVINS_2_DATA(pDevIns, PNVME);
    VBOXSTRICTRC rcStrict = VINF_SUCCESS;
    RT_NOREF(pvUser);

    AssertReturn(offPort < 8, VERR_IOM_IOPORT_UNUSED); /* Paranoia^2 given that we only register 8 ports. */
    ASSERT_GUEST_MSG(cb == 4, ("%#x LB %u\n", offPort, cb));

    if (offPort == 0)
    {
        /* Read the index register. */
        *pu32 = pThis->u32RegIdx;
    }
    else
    {
        ASSERT_GUEST_MSG(offPort == 4, ("%#x LB %u\n", offPort, cb));
        rcStrict = nvmeRegRead(pDevIns, pThis, pThis->u32RegIdx, pu32, cb);
        if (rcStrict == VINF_IOM_R3_MMIO_READ)
            rcStrict = VINF_IOM_R3_IOPORT_READ;
        else if (   rcStrict == VINF_IOM_MMIO_UNUSED_00
                 || rcStrict == VINF_IOM_MMIO_UNUSED_FF)
            rcStrict = VERR_IOM_IOPORT_UNUSED;
    }

    Log2(("#%d nvmeIdxDataRead: pu32=%p:{%.*Rhxs} cb=%d offPort=%#x rc=%Rrc\n",
          pDevIns->iInstance, pu32, cb, pu32, cb, offPort, VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}

#ifdef IN_RING3

# ifdef LOG_ENABLED

static void nvmeR3CmdHdrDump(PNVMEWRKTHRD pWrkThrd, PNVMECMDHDR pHdr, bool fCmdAdmin)
{
    const char *pszCmd = "<Invalid>";
    const char *pszBuffer = "<Invalid>";
    const char *pszFuse = "<Invalid>";

    if (fCmdAdmin)
    {
        switch (pHdr->u8Opc)
        {
            case NVME_CMD_ADM_OPC_SQ_DEL:
                pszCmd = "Delete I/O Submission Queue";
                break;
            case NVME_CMD_ADM_OPC_SQ_CREATE:
                pszCmd = "Create I/O Submission Queue";
                break;
            case NVME_CMD_ADM_OPC_GET_LOG_PG:
                pszCmd = "Get Log Page";
                break;
            case NVME_CMD_ADM_OPC_CQ_DEL:
                pszCmd = "Delete I/O Completion Queue";
                break;
            case NVME_CMD_ADM_OPC_CQ_CREATE:
                pszCmd = "Create I/O Completion Queue";
                break;
            case NVME_CMD_ADM_OPC_IDENTIFY:
                pszCmd = "Identify";
                break;
            case NVME_CMD_ADM_OPC_ABORT:
                pszCmd = "Abort";
                break;
            case NVME_CMD_ADM_OPC_SET_FEAT:
                pszCmd = "Set Feature";
                break;
            case NVME_CMD_ADM_OPC_GET_FEAT:
                pszCmd = "Get Feature";
                break;
            case NVME_CMD_ADM_OPC_ASYNC_EVT_REQ:
                pszCmd = "Asynchronous Event Request";
                break;
            case NVME_CMD_ADM_OPC_NS_MGMT:
                pszCmd = "Namespace Management";
                break;
            case NVME_CMD_ADM_OPC_FW_COMMIT:
                pszCmd = "Firmware Commmit";
                break;
            case NVME_CMD_ADM_OPC_FW_IMG_DWNLD:
                pszCmd = "Firmware Image Download";
                break;
            case NVME_CMD_ADM_OPC_NS_ATTACHMENT:
                pszCmd = "Namespace Attachment";
                break;
            default:
                pszCmd = "<Invalid Command Opcode>";
        }
    }
    else
    {
        switch (pHdr->u8Opc)
        {
            case NVME_CMD_NVM_FLUSH:
                pszCmd = "Flush";
                break;
            case NVME_CMD_NVM_WRITE:
                pszCmd = "Write";
                break;
            case NVME_CMD_NVM_READ:
                pszCmd = "Read";
                break;
            default:
                pszCmd = "<Invalid/Unsupported Command Opcode>";
        }
    }

    if (pHdr->u2Fuse == NVME_CMD_HDR_FUSE_NORMAL)
        pszFuse = "Normal Operation";
    else if (pHdr->u2Fuse == NVME_CMD_HDR_FUSE_FIRST)
        pszFuse = "Fused command - First";
    else if (pHdr->u2Fuse == NVME_CMD_HDR_FUSE_SECOND)
        pszFuse = "Fused command - Second";

    if (pHdr->u2Psdt == NVME_CMD_HDR_PSDT_PRP)
        pszBuffer = "PRP";
    else if (   pHdr->u2Psdt == NVME_CMD_HDR_PSDT_SGL
             || pHdr->u2Psdt == NVME_CMD_HDR_PSDT_SGL2)
        pszBuffer = "SGL";
    else
        pszBuffer = "RESERVED";

    Log3(("Wrk#%u: Dumping %s command header\n"
          "Wrk#%u:     CID:    %#x\n"
          "Wrk#%u:     FUSING: %s\n"
          "Wrk#%u:     BUFFER: %s\n"
          "Wrk#%u:     CMD:    %s (%#x)\n"
          "Wrk#%u:==========================\n",
          pWrkThrd->uId, fCmdAdmin ? "ADMIN" : "NVM",
          pWrkThrd->uId, pHdr->u16Cid,
          pWrkThrd->uId, pszFuse,
          pWrkThrd->uId, pszBuffer,
          pWrkThrd->uId, pszCmd, pHdr->u8Opc,
          pWrkThrd->uId));
}

/**
 * Dumps the given admin command to the debug log.
 *
 * @param   pWrkThrd   The work thread processing the command.
 * @param   pCmdAdm    The admin command to dump.
 */
static void nvmeR3CmdAdminDump(PNVMEWRKTHRD pWrkThrd, PNVMECMDADM pCmdAdm)
{
    nvmeR3CmdHdrDump(pWrkThrd, &pCmdAdm->u.Field.Hdr, true /* fCmdAdmin*/);
}

/**
 * Dumps the given NVM command to the debug log.
 *
 * @param   pWrkThrd   The work thread processing the command.
 * @param   pCmdNvm    The NVM command to dump.
 */
static void nvmeR3CmdNvmDump(PNVMEWRKTHRD pWrkThrd, PNVMECMDNVM pCmdNvm)
{
    nvmeR3CmdHdrDump(pWrkThrd, &pCmdNvm->u.Field.Hdr, false /* fCmdAdmin*/);

    Log3(("Wrk#%u:     NSID:    %#x\n"
          "Wrk#%u:     METAPTR: %RGp\n"
          "Wrk#%u:     PRP1:    %RGp\n"
          "Wrk#%u:     PRP2:    %RGp\n",
          pWrkThrd->uId, pCmdNvm->u.Field.uNsId,
          pWrkThrd->uId, pCmdNvm->u.Field.GCPhysMetaPtr,
          pWrkThrd->uId, pCmdNvm->u.Field.DataPtr.Prp.Prp1,
          pWrkThrd->uId, pCmdNvm->u.Field.DataPtr.Prp.Prp2));
    if (pCmdNvm->u.Field.Hdr.u8Opc != NVME_CMD_NVM_FLUSH)
        Log3(("Wrk#%u:     LBA:     %llu\n"
              "Wrk#%u:     BLOCKS:  %u\n",
              pWrkThrd->uId, pCmdNvm->u.Field.u.ReadWrite.u64LbaStart,
              pWrkThrd->uId, pCmdNvm->u.Field.u.ReadWrite.u16Blocks + 1));
}

# else  /* !LOG_ENABLED */
#  define nvmeR3CmdAdminDump(a_pWrkThrd, a_pCmdAdm)  do {} while (0)
#  define nvmeR3CmdNvmDump(a_pWrkThrd, a_pCmdNvm)    do {} while (0)
# endif /* !LOG_ENABLED */

/**
 * Reads from the given guest physical address, fetching the data
 * from the controller memory buffer if enabled.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   GCPhysAddr  The guest physical address to read from.
 * @param   pv          Where to store the data.
 * @param   cb          How many bytes to read.
 * @param   u32Type     The type of memory read (CQ, SQ, Lists, Data).
 */
static int nvmeR3PhysRead(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                          RTGCPHYS GCPhysAddr, void *pv, size_t cb, uint32_t u32Type)
{
    Assert(u32Type <= NVME_CMBSZ_SUPP_BIT_IDX_MAX); RT_NOREF(u32Type);

    if (   pThis->cbCtrlMemBuf > 0
        && pThis->GCPhysCtrlMemBuf != NIL_RTGCPHYS
        && GCPhysAddr >= pThis->GCPhysCtrlMemBuf
        && GCPhysAddr < pThis->GCPhysCtrlMemBuf + pThis->cbCtrlMemBuf)
    {
        uint32_t const off = GCPhysAddr - pThis->GCPhysCtrlMemBuf;
        if (cb <= pThis->cbCtrlMemBuf - off)
        {
            memcpy(pv, (uint8_t *)pThisCC->pvCtrlMemBufR3 + off, cb);
            STAM_COUNTER_ADD(&pThis->aStatMemXfer[u32Type].StatReadCtrlMemBuf, cb);
            return VINF_SUCCESS;
        }
    }

    STAM_COUNTER_ADD(&pThis->aStatMemXfer[u32Type].StatReadGuestMem, cb);
    return PDMDevHlpPCIPhysRead(pDevIns, GCPhysAddr, pv, cb);
}

/**
 * Writes data to the given guest physical address, taking the controller memory
 * buffer into account.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   GCPhysAddr  The guest physical address to write to.
 * @param   pv          The data to write.
 * @param   cb          How many bytes to write.
 * @param   u32Type     The type of memory written (CQ, SQ, Lists, Data).
 */
static int nvmeR3PhysWrite(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                           RTGCPHYS GCPhysAddr, const void *pv, size_t cb, uint32_t u32Type)
{
    Assert(u32Type <= NVME_CMBSZ_SUPP_BIT_IDX_MAX); RT_NOREF(u32Type);

    if (   pThis->cbCtrlMemBuf > 0
        && pThis->GCPhysCtrlMemBuf != NIL_RTGCPHYS
        && GCPhysAddr >= pThis->GCPhysCtrlMemBuf
        && GCPhysAddr < pThis->GCPhysCtrlMemBuf + pThis->cbCtrlMemBuf)
    {
        uint32_t const off = GCPhysAddr - pThis->GCPhysCtrlMemBuf;
        if (cb <= pThis->cbCtrlMemBuf - off)
        {
            memcpy((uint8_t *)pThisCC->pvCtrlMemBufR3 + off, pv, cb);
            STAM_COUNTER_ADD(&pThis->aStatMemXfer[u32Type].StatWrittenCtrlMemBuf, cb);
            return VINF_SUCCESS;
        }
    }

    STAM_COUNTER_ADD(&pThis->aStatMemXfer[u32Type].StatWrittenGuestMem, cb);
    return PDMDevHlpPCIPhysWrite(pDevIns, GCPhysAddr, pv, cb);
}

/**
 * Posts a completion queue entry at the given guest physical address.
 *
 * @returns VBox status code.
 * @param   pDevIns           The device instance.
 * @param   pThis             The NVMe controller shared instance data.
 * @param   pThisCC           The NVMe controller ring-3 instance data.
 * @param   GCPhysCe          The physical address to write the entry to.
 * @param   pQueueComp        The completion queue the entry is posted in.
 * @param   pQueueSubm        The submission queue responsible for this command.
 * @param   u16Cid            The command identifier.
 * @param   u8Sct             The status code type.
 * @param   u8Sc              The status code.
 * @param   u32CmdSpecific    The command specific word.
 * @param   fMore             Flag whether the guest can query additional details through
 *                            the "Get Log Page" command.
 * @param   fDnr              Flag whether the guest might retry the command.
 */
static int nvmeR3CompQueueEntryPost(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC, RTGCPHYS GCPhysCe,
                                    PNVMEQUEUECOMP pQueueComp, PNVMEQUEUESUBM pQueueSubm, uint16_t u16Cid,
                                    uint8_t u8Sct, uint8_t u8Sc, uint32_t u32CmdSpecific, bool fMore, bool fDnr)
{
    NVMECQENT Ce;

    Assert(pQueueComp->u32IntrVec == 0 || nvmeIsMSIEnabled(pDevIns->apPciDevs[0]));
    AssertReturn(GCPhysCe != NVME_QUEUE_IS_FULL_RTGCPHYS, VERR_INTERNAL_ERROR);

    /* Reading the completion entry is unfortunately necessary due to the phase tag inversion... */
    nvmeR3PhysRead(pDevIns, pThis, pThisCC, GCPhysCe, &Ce, sizeof(Ce), NVME_CMBSZ_CQS_BIT_IDX);

    bool fPhaseTag = RT_BOOL(Ce.u16StsPh & NVME_CQ_ENTRY_PHASE_TAG);
    Ce.u32CmdSpecific = u32CmdSpecific;
    Ce.u16SqHead      = (uint16_t)ASMAtomicReadU32(&pQueueSubm->Hdr.idxHead);
    Ce.u16SqId        = pQueueSubm->Hdr.u16Id;
    Ce.u16Cid         = u16Cid;
    Ce.u16StsPh       = fPhaseTag ? 0 : NVME_CQ_ENTRY_PHASE_TAG;
    Ce.u16StsPh      |= fMore ? NVME_CQ_ENTRY_MORE : 0;
    Ce.u16StsPh      |= fDnr  ? NVME_CQ_ENTRY_DNR  : 0;
    Ce.u16StsPh      |= NVME_CQ_ENTRY_SCT_SET(u8Sct);
    Ce.u16StsPh      |= NVME_CQ_ENTRY_SC_SET(u8Sc);

    nvmeQueueProducerAdvance(&pQueueComp->Hdr);

    /* Write back. */
    int rc = nvmeR3PhysWrite(pDevIns, pThis, pThisCC, GCPhysCe, &Ce, sizeof(Ce), NVME_CMBSZ_CQS_BIT_IDX);
    if (   RT_SUCCESS(rc)
        && pQueueComp->fIntrEnabled)
        nvmeIntrVecPostEvents(pDevIns, pThis, pQueueComp->u32IntrVec, 1);

    return rc;
}

/**
 * Completes the given command identifier with the given status by posting a
 * complete entry to the appropriate completion queue.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pThis           The NVMe controller shared instance data.
 * @param   pThisCC         The NVMe controller ring-3 instance data.
 * @param   pQueueSubm      The submission queue responsible for this command.
 * @param   u16Cid          The command identifier.
 * @param   u8Sct           The status code type.
 * @param   u8Sc            The status code.
 * @param   u32CmdSpecific  The command specific word.
 * @param   fMore           Flag whether the guest can query additional details through
 *                          the "Get Log Page" command.
 * @param   fDnr            Flag whether the guest might retry the command.
 */
static int nvmeR3CmdCompleteWithStatus(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC, PNVMEQUEUESUBM pQueueSubm,
                                       uint16_t u16Cid, uint8_t u8Sct, uint8_t u8Sc,
                                       uint32_t u32CmdSpecific, bool fMore, bool fDnr)
{
    int rc = VINF_SUCCESS;
    AssertReturn(pQueueSubm->u16CompletionQueueId < RT_ELEMENTS(pThis->aQueuesComp), VERR_INTERNAL_ERROR_2);
    PNVMEQUEUECOMP   pQueueComp   = &pThis->aQueuesComp[pQueueSubm->u16CompletionQueueId];
    PNVMEQUEUECOMPR3 pQueueCompR3 = &pThisCC->aQueuesComp[pQueueSubm->u16CompletionQueueId];

    Log2(("Completing command from queue %u CID %#x\n", pQueueSubm->Hdr.u16Id, u16Cid));

    /* The completion queue should be valid. */
    AssertReturn(pQueueComp->Hdr.cEntries, VERR_INTERNAL_ERROR);

    rc = RTSemFastMutexRequest(pQueueCompR3->hMtx);
    AssertRCReturn(rc, rc);

    /* Check whether we have a free completion queue entry to use. */
    RTGCPHYS GCPhysCe = nvmeQueueProducerGetFreeEntryAddress(&pQueueComp->Hdr);
    if (GCPhysCe == NVME_QUEUE_IS_FULL_RTGCPHYS)
    {
        /* Defer the completion until there is a free entry in the queue. */
        PNVMECOMPQUEUEWAITER pWaiter = (PNVMECOMPQUEUEWAITER)RTMemAllocZ(sizeof(NVMECOMPQUEUEWAITER));
        if (pWaiter)
        {
            pWaiter->pQueueSubm     = pQueueSubm;
            pWaiter->u16Cid         = u16Cid;
            pWaiter->u8Sct          = u8Sct;
            pWaiter->u8Sc           = u8Sc;
            pWaiter->u32CmdSpecific = u32CmdSpecific;
            pWaiter->fMore          = fMore;
            pWaiter->fDnr           = fDnr;
            RTListAppend(&pQueueCompR3->LstCompletionsWaiting, &pWaiter->NdLstQueue);
            pQueueComp->cWaiters++;
            /*
             * If there are too many entries waiting for completion mark the queue as overlaoded
             * stopping new commands from the submission queues to be processed.
             */
            if (pQueueComp->cWaiters == pThis->cCompQueuesWaitersMax)
            {
                pQueueComp->fOverloaded = true;
                LogRelMax(10, ("NVME%#u: Completion queue %u is overloaded, stopping command procession on associated submission queues\n",
                               pDevIns->iInstance, pQueueSubm->u16CompletionQueueId));
            }
        }
        else
        {
            LogRel(("NVME%#u: Failed to allocate completion queue waiter\n", pDevIns->iInstance));
            rc = VERR_NO_MEMORY;
        }
    }
    else
        rc = nvmeR3CompQueueEntryPost(pDevIns, pThis, pThisCC, GCPhysCe, pQueueComp, pQueueSubm,
                                      u16Cid, u8Sct, u8Sc, u32CmdSpecific, fMore, fDnr);

    int rc2 = RTSemFastMutexRelease(pQueueCompR3->hMtx);
    AssertRCReturn(rc2, rc2);

    return rc;
}

/**
 * Completes a command with a success status.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pThis           The NVMe controller shared instance data.
 * @param   pThisCC         The NVMe controller ring-3 instance data.
 * @param   pQueueSubm      The submission queue responsible for this command.
 * @param   u16Cid          The command identifier.
 * @param   u32CmdSpecific  The command specific word.
 */
static int nvmeR3CmdCompleteWithSuccess(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                        PNVMEQUEUESUBM pQueueSubm, uint16_t u16Cid, uint32_t u32CmdSpecific)
{
    LogFlowFunc(("pThis=%#p pQueueSubm=%#p u16Cid=%#x u32CmdSpecific=%#x\n",
                 pThis, pQueueSubm, u16Cid, u32CmdSpecific));
    return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, u16Cid, NVME_CQ_ENTRY_SCT_CMD_GENERIC,
                                       NVME_CQ_ENTRY_SC_GEN_SUCCESS, u32CmdSpecific,
                                       false /* fMore */, false /* fDnr */);
}

/**
 * Removes one async event request ID and completes it with the given information.
 *
 * @param   pDevIns         The device instance.
 * @param   pThis           The NVMe controller shared instance data.
 * @param   pThisCC         The NVMe controller ring-3 instance data.
 * @param   u8AsyncEvtType  The async event type.
 * @param   u8AsyncEvtInfo  The async event info.
 * @param   u8LogPgId       The log page identifier.
 */
static void nvmeR3AsyncEvtComplete(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                   uint8_t u8AsyncEvtType, uint8_t u8AsyncEvtInfo, uint8_t u8LogPgId)
{
    LogFlowFunc(("pThis=%#p u8AsyncEvtType=%u u8AsyncEvtInfo=%u u8LogPgId=%u\n",
                 pThis, u8AsyncEvtType, u8AsyncEvtInfo, u8LogPgId));

    int rc = RTCritSectEnter(&pThisCC->CritSectAsyncEvtReqs);
    if (RT_SUCCESS(rc))
    {
        if (pThisCC->cAsyncEvtReqsCur)
        {
            uint16_t u16Cid = pThisCC->paAsyncEvtReqCids[--pThisCC->cAsyncEvtReqsCur];
            uint32_t u32AsyncEvtInfo = (u8LogPgId << 16) | (u8AsyncEvtInfo << 8) | (u8AsyncEvtType & 0x7);

            rc = nvmeR3CmdCompleteWithSuccess(pDevIns, pThis, pThisCC, &pThis->aQueuesSubm[NVME_ADM_QUEUE_ID],
                                              u16Cid, u32AsyncEvtInfo);
            if (RT_FAILURE(rc))
                nvmeStateSetFatalError(pThis);
        }
        else
            nvmeStateSetFatalError(pThis);

        rc = RTCritSectLeave(&pThisCC->CritSectAsyncEvtReqs);
        AssertRC(rc);
    }
    else
        nvmeStateSetFatalError(pThis);
}

/**
 * Copy from guest to host memory worker.
 *
 * @copydoc FNNVMER3MEMCOPYCALLBACK
 */
static DECLCALLBACK(void) nvmeR3CopyBufferFromPrpWorker(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                                        RTGCPHYS GCPhys, PRTSGBUF pSgBuf, size_t cbCopy, size_t *pcbSkip)
{
    size_t cbSkipped = RT_MIN(cbCopy, *pcbSkip);
    cbCopy   -= cbSkipped;
    GCPhys   += cbSkipped;
    *pcbSkip -= cbSkipped;

    while (cbCopy)
    {
        size_t cbSeg = cbCopy;
        void *pvSeg = RTSgBufGetNextSegment(pSgBuf, &cbSeg);

        AssertPtr(pvSeg);
        nvmeR3PhysRead(pDevIns, pThis, pThisCC, GCPhys, pvSeg, cbSeg, NVME_CMBSZ_RDS_BIT_IDX);
        GCPhys += cbSeg;
        cbCopy -= cbSeg;
    }
}

/**
 * Copy from host to guest memory worker.
 *
 * @copydoc FNNVMER3MEMCOPYCALLBACK
 */
static DECLCALLBACK(void) nvmeR3CopyBufferToPrpWorker(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                                      RTGCPHYS GCPhys, PRTSGBUF pSgBuf, size_t cbCopy, size_t *pcbSkip)
{
    size_t cbSkipped = RT_MIN(cbCopy, *pcbSkip);
    cbCopy   -= cbSkipped;
    GCPhys   += cbSkipped;
    *pcbSkip -= cbSkipped;

    while (cbCopy)
    {
        size_t cbSeg = cbCopy;
        void *pvSeg = RTSgBufGetNextSegment(pSgBuf, &cbSeg);

        AssertPtr(pvSeg);
        nvmeR3PhysWrite(pDevIns, pThis, pThisCC, GCPhys, pvSeg, cbSeg, NVME_CMBSZ_WDS_BIT_IDX);
        GCPhys += cbSeg;
        cbCopy -= cbSeg;
    }
}

/**
 * Walks the PRP list pointed to by the given PRP entry and copies memory between the described
 * guest and given host buffer.
 *
 * @returns Flag whether walking the PRP list succeeded.
 * @retval  true if walking the PRP list succeeded.
 * @retval  false if a malformed PRP entry was encoountered while walking the list.
 * @param   pDevIns         The device instance.
 * @param   pThis           The NVMe controller shared instance data.
 * @param   pThisCC         The NVMe controller ring-3 instance data.
 * @param   pfnCopyWorker   The copy worker to use.
 * @param   PrpList         The PRP entry pointing to the PRP list.
 * @param   cbPrp           The size of the complete PRP buffer.
 * @param   pSgBuf          The host memory S/G buffer.
 * @param   cbHost          Size of the host buffer.
 * @param   cbSkip          How many bytes to skip before copying real data.
 */
static bool nvmeR3PrpListWalk(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC, PFNNVMER3MEMCOPYCALLBACK pfnCopyWorker,
                              NVMEPRP PrpList, size_t cbPrp, PRTSGBUF pSgBuf,
                              size_t cbHost, size_t cbSkip)
{
    size_t cbLeft = cbPrp;
    size_t cbPage = RT_MAX(RT_BIT_32(pThis->uMpsSet), 1);
    size_t cPrpsLeft = (cbLeft / cbPage) + ((cbLeft % cbPage) ? 1 : 0);
    size_t cPrpsLeftInPage = RT_MIN(NVME_PRP_GET_SIZE(PrpList, pThis->uMpsSet) / sizeof(NVMEPRP), cPrpsLeft);
    RTGCPHYS GCPhysPrpList = NVME_PRP_TO_GCPHYS(PrpList, pThis->uMpsSet);
    bool fSuccess = true;

    Assert(!(cbPage % sizeof(NVMEPRP)));

    LogFlowFunc(("pThis=%#p pfnCopyWorker=%#p PrpList=%#llx pSgBuf=%#p cbHost=%zu cbSkip=%zu\n",
                 pThis, pfnCopyWorker, PrpList, pSgBuf, cbHost, cbSkip));

    ASSERT_GUEST_RETURN(cPrpsLeftInPage, false);

    do
    {
        NVMEPRP aPrps[32];
        size_t  cPrpsRead = RT_MIN(RT_ELEMENTS(aPrps), cPrpsLeftInPage);

        /* Read part of PRP list in. */
        LogFlowFunc(("Reading PRP list: GCPhysPrpList=%RGp cPrpsRead=%u\n", GCPhysPrpList, cPrpsRead));
        nvmeR3PhysRead(pDevIns, pThis, pThisCC, GCPhysPrpList, &aPrps[0], cPrpsRead * sizeof(NVMEPRP), NVME_CMBSZ_LISTS_BIT_IDX);

        /* Check whether the last element in the read PRPs points us to another PRP list. */
        if (   cPrpsRead == cPrpsLeftInPage
            && cPrpsRead < cPrpsLeft)
        {
            cPrpsRead--;

            cPrpsLeft -= cPrpsRead;
            GCPhysPrpList = NVME_PRP_TO_GCPHYS(aPrps[cPrpsRead], pThis->uMpsSet);
            cPrpsLeftInPage = RT_MIN(NVME_PRP_GET_SIZE(aPrps[cPrpsRead], pThis->uMpsSet) / sizeof(NVMEPRP), cPrpsLeft);
            ASSERT_GUEST_RETURN(cPrpsLeftInPage, false);
        }
        else
        {
            cPrpsLeft       -= cPrpsRead;
            cPrpsLeftInPage -= cPrpsRead;
            GCPhysPrpList   += cPrpsRead * sizeof(NVMEPRP);
        }

        for (unsigned idxPrp = 0; idxPrp < cPrpsRead && cbHost; idxPrp++)
        {
            /* Copy from the start till the end of the page. */
            RTGCPHYS GCPhysCur = NVME_PRP_TO_GCPHYS(aPrps[idxPrp], pThis->uMpsSet);
            size_t cbCopy = RT_MIN(NVME_PRP_GET_SIZE(aPrps[idxPrp], pThis->uMpsSet), cbLeft);

            LogFlowFunc(("Prp%u=%#llx => GCPhys=%RGp cbCopy=%zu\n", idxPrp, aPrps[idxPrp], GCPhysCur, cbCopy));

            if (NVME_PRP_GET_PAGE_OFF(aPrps[idxPrp], pThis->uMpsSet) != 0)
            {
                LogFlowFunc(("PRP Offset at index %u is not 0 (%#x)\n", idxPrp, NVME_PRP_GET_PAGE_OFF(aPrps[idxPrp], pThis->uMpsSet)));
                fSuccess = false;
                break;
            }

            LogFlowFunc(("Writing %zu bytes to %RGp\n", cbCopy, GCPhysCur));

            cbCopy = RT_MIN(cbCopy, cbHost);

            pfnCopyWorker(pDevIns, pThis, pThisCC, GCPhysCur, pSgBuf, cbCopy, &cbSkip);
            cbHost -= cbCopy;
            cbLeft -= cbCopy;
        }

    } while (   cPrpsLeft
             && cbLeft
             && fSuccess
             && cbHost);

    return fSuccess;
}

/**
 * Copies memory between the guest memory buffer pointed to by the PRP entries
 * and the given host buffer.
 * @returns Flag whether the copy was successful.
 * @retval  true if copying succeded.
 * @retval  false if a malformed PRP entry was encountered.
 * @param   pDevIns         The device instance.
 * @param   pThis           The NVMe controller shared instance data.
 * @param   pThisCC         The NVMe controller ring-3 instance data.
 * @param   pfnCopyWorker   Callback copying the data to a contigous part of the buffer.
 * @param   Prp1            The first PRP entry from the command.
 * @param   Prp2            The second PRP entry from the command.
 * @param   cbPrp           The size of the complete PRP buffer.
 * @param   pSgBuf          The host memory S/G buffer.
 * @param   cbHost          Size of the host memory buffer.
 * @param   cbSkip          How many bytes to skip before copying real data.
 * @param   fListsAllowed   Flag whether the command allows a list for the second PRP entry.
 */
static bool nvmeR3PrpCopy(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC, PFNNVMER3MEMCOPYCALLBACK pfnCopyWorker,
                          NVMEPRP Prp1, NVMEPRP Prp2, size_t cbPrp, PRTSGBUF pSgBuf,
                          size_t cbHost, size_t cbSkip, bool fListsAllowed)
{
    bool fSuccess = true;
    size_t cbLeft = cbPrp;
    RTGCPHYS GCPhysCur = NVME_PRP_TO_GCPHYS(Prp1, pThis->uMpsSet);
    size_t cbCopy = RT_MIN(NVME_PRP_GET_SIZE(Prp1, pThis->uMpsSet), cbLeft);

    LogFlowFunc(("pThis=%#p pfnCopyWorker=%#p Prp1=%#llx Prp2=%#llx cbPrp=%zu pSgBuf=%#p cbHost=%zu cbSkip=%zu fListsAllowed=%RTbool\n",
                 pThis, pfnCopyWorker, Prp1, Prp2, cbPrp, pSgBuf, cbHost, cbSkip, fListsAllowed));

    /*
     * Add the amount to skip to the host buffer size to avoid a
     * few conditionals later on.
     */
    cbHost += cbSkip;

    /* The first PRP should be at least word aligned. */
    if (Prp1 & 0x3)
        return false;

    LogFlowFunc(("Prp1=%#llx => GCPhys=%RGp cbCopy=%zu\n", Prp1, GCPhysCur, cbCopy));

    Assert(cbHost);
    cbCopy = RT_MIN(cbCopy, cbHost);
    pfnCopyWorker(pDevIns, pThis, pThisCC, GCPhysCur, pSgBuf, cbCopy, &cbSkip);
    cbLeft -= cbCopy;
    cbHost -= cbCopy;

    if (cbLeft && cbHost)
    {
        /*
         * Check whether the remaining data fits into the second PRP2 entry.
         * If not assume the second entry points to a PRP list if the command allows
         * them and copy the data into the PRP list.
         */
        LogFlowFunc(("Prp2=%#llx => GCPhys=%RGp cbSize=%zu\n", Prp2, NVME_PRP_TO_GCPHYS(Prp2, pThis->uMpsSet),
                                                               NVME_PRP_GET_SIZE(Prp2, pThis->uMpsSet)));
        if (NVME_PRP_GET_SIZE(Prp2, pThis->uMpsSet) < cbLeft)
        {
            if (   RT_LIKELY(fListsAllowed)
                && !(Prp2 & 0x3))
            {
                LogFlowFunc(("PRP list required cbLeft=%zu\n", cbLeft));
                fSuccess = nvmeR3PrpListWalk(pDevIns, pThis, pThisCC, pfnCopyWorker, Prp2, cbLeft, pSgBuf, cbHost, cbSkip);
            }
            else
                fSuccess = false;
        }
        else
        {
            LogFlowFunc(("Remaining data fits into buffer pointed to by Prp2\n"));
            GCPhysCur = NVME_PRP_TO_GCPHYS(Prp2, pThis->uMpsSet);

            pfnCopyWorker(pDevIns, pThis, pThisCC, GCPhysCur, pSgBuf, RT_MIN(cbLeft, cbHost), &cbSkip);
        }
    }

    return fSuccess;
}

/**
 * Copies a given S/G buffer into guest memory using the provided PRPs.
 *
 * @returns Flag whether the copy was successful.
 * @retval  true if copying succeded.
 * @retval  false if a malformed PRP entry was encountered.
 * @param   pDevIns         The device instance.
 * @param   pThis           The NVMe controller shared instance data.
 * @param   pThisCC         The NVMe controller ring-3 instance data.
 * @param   Prp1            The first PRP entry from the command.
 * @param   Prp2            The second PRP entry from the command.
 * @param   cbPrp           The size of the PRP buffer in bytes.
 * @param   pSgBuf          S/G buffer to read data from.
 * @param   cbSrc           Size of the source buffer.
 * @param   cbSkip          How many bytes to skip before copying real data.
 * @param   fListsAllowed   Flag whether the command allows a list for the second PRP entry.
 */
DECLINLINE(bool) nvmeR3CopySgBufToPrps(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                       NVMEPRP Prp1, NVMEPRP Prp2, size_t cbPrp,
                                       PRTSGBUF pSgBuf, size_t cbSrc, size_t cbSkip,
                                       bool fListsAllowed)
{
    return nvmeR3PrpCopy(pDevIns, pThis, pThisCC, nvmeR3CopyBufferToPrpWorker, Prp1, Prp2, cbPrp,
                         pSgBuf, cbSrc, cbSkip, fListsAllowed);
}

/**
 * Copies a buffer from guest memory into a provided S/G buffer using the provided array of PRPs.
 *
 * @returns Flag whether the copy was successful.
 * @retval  true if copying succeded.
 * @retval  false if a malformed PRP entry was encountered.
 * @param   pDevIns         The device instance.
 * @param   pThis           The NVMe controller shared instance data.
 * @param   pThisCC         The NVMe controller ring-3 instance data.
 * @param   Prp1            The first PRP entry from the command.
 * @param   Prp2            The second PRP entry from the command.
 * @param   cbPrp           The size of the PRP buffer in bytes.
 * @param   pSgBuf          S/G buffer to store the data in.
 * @param   cbDst           Size of the destinatin buffer.
 * @param   cbSkip          How many bytes to skip before copying real data.
 * @param   fListsAllowed   Flag whether the command allows a list for the second PRP entry.
 */
DECLINLINE(bool) nvmeR3CopySgBufFromPrps(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                         NVMEPRP Prp1, NVMEPRP Prp2, size_t cbPrp,
                                         PRTSGBUF pSgBuf, size_t cbDst, size_t cbSkip,
                                         bool fListsAllowed)
{
    return nvmeR3PrpCopy(pDevIns, pThis, pThisCC, nvmeR3CopyBufferFromPrpWorker, Prp1, Prp2, cbPrp,
                         pSgBuf, cbDst, cbSkip, fListsAllowed);
}

/**
 * Copies a given buffer into guest memory using the provided PRPs.
 *
 * @returns Flag whether the copy was successful.
 * @retval  true if copying succeded.
 * @retval  false if a malformed PRP entry was encountered.
 * @param   pDevIns         The device instance.
 * @param   pThis           The NVMe controller shared instance data.
 * @param   pThisCC         The NVMe controller ring-3 instance data.
 * @param   Prp1            The first PRP entry from the command.
 * @param   Prp2            The second PRP entry from the command.
 * @param   cbPrp           The size of the PRP buffer in bytes.
 * @param   pvSrc           Where to read bytes from.
 * @param   cbSrc           Size of the source buffer.
 * @param   cbSkip          How many bytes to skip before copying real data.
 * @param   fListsAllowed   Flag whether the command allows a list for the second PRP entry.
 */
DECLINLINE(bool) nvmeR3CopyBufferToPrps(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                        NVMEPRP Prp1, NVMEPRP Prp2, size_t cbPrp,
                                        const void *pvSrc, size_t cbSrc, size_t cbSkip,
                                        bool fListsAllowed)
{
    RTSGSEG Seg;
    RTSGBUF SgBuf;
    Seg.pvSeg = (void *)pvSrc;
    Seg.cbSeg = cbSrc;
    RTSgBufInit(&SgBuf, &Seg, 1);
    return nvmeR3CopySgBufToPrps(pDevIns, pThis, pThisCC, Prp1, Prp2, cbPrp,
                                 &SgBuf, cbSrc, cbSkip, fListsAllowed);
}

#if 0 /* unused */
/**
 * Copies a buffer from guest memory into a provided destination using the provided array of PRPs.
 *
 * @returns Flag whether the copy was successful.
 * @retval  true if copying succeded.
 * @retval  false if a malformed PRP entry was encountered.
 * @param   pDevIns         The device instance.
 * @param   pThis           The NVMe controller shared instance data.
 * @param   pThisCC         The NVMe controller ring-3 instance data.
 * @param   Prp1            The first PRP entry from the command.
 * @param   Prp2            The second PRP entry from the command.
 * @param   cbPrp           The size of the PRP buffer in bytes.
 * @param   pvDst           Where to store the data.
 * @param   cbDst           Size of the destinatin buffer.
 * @param   cbSkip          How many bytes to skip before copying real data.
 * @param   fListsAllowed   Flag whether the command allows a list for the second PRP entry.
 */
DECLINLINE(bool) nvmeR3CopyBufferFromPrps(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                          NVMEPRP Prp1, NVMEPRP Prp2, size_t cbPrp,
                                          void *pvDst, size_t cbDst, size_t cbSkip,
                                          bool fListsAllowed)
{
    RTSGSEG Seg;
    RTSGBUF SgBuf;
    Seg.pvSeg = pvDst;
    Seg.cbSeg = cbDst;
    RTSgBufInit(&SgBuf, &Seg, 1);
    return nvmeR3CopySgBufFromPrps(pDevIns, pThis, pThisCC, Prp1, Prp2, cbPrp,
                                   &SgBuf, cbDst, cbSkip, fListsAllowed);
}
#endif /* unused */

/**
 * Worker for the submission queue deallocation, can either happen directly while the
 * "Delete I/O Submission Queue" command is executed or deferred after the last active request.
 * completed.
 *
 * @returns VBox status code.
 * @param   pDevIns         The device instance.
 * @param   pThis           The NVMe controller shared instance data.
 * @param   pThisCC         The NVMe controller ring-3 instance data.
 * @param   pQueueSubm      The submission queue which initiated the deallocation (admin command queue)
 * @param   pIoSubmQueue    The I/O submission queue to deallocate.
 */
static int nvmeR3QueueSubmDeallocateDeferred(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                             PNVMEQUEUESUBM pQueueSubm, PNVMEQUEUESUBM pIoSubmQueue)
{
    Assert(pIoSubmQueue->Hdr.enmState == NVMEQUEUESTATE_DEALLOCATING);

    ASMAtomicWriteU32((volatile uint32_t *)&pIoSubmQueue->Hdr.enmState, NVMEQUEUESTATE_DEALLOCATED);
    pIoSubmQueue->Hdr.u16Id            = 0;
    pIoSubmQueue->Hdr.cEntries         = 0;
    pIoSubmQueue->Hdr.cbEntry          = 0;
    pIoSubmQueue->Hdr.fPhysCont        = true;
    pIoSubmQueue->Hdr.enmType          = NVMEQUEUETYPE_INVALID;
    pIoSubmQueue->Hdr.GCPhysBase       = NIL_RTGCPHYS;
    pIoSubmQueue->Hdr.idxHead          = 0;
    pIoSubmQueue->Hdr.idxTail          = 0;

    /* Release reference from associated completion queue. */
    PNVMEQUEUECOMP pIoQueueComp = &pThis->aQueuesComp[pIoSubmQueue->u16CompletionQueueId];
    ASMAtomicDecU32(&pIoQueueComp->cSubmQueuesRef);

    /* Remove submission queue from the worker thread. */
    uintptr_t idxIoSubmQueue = pIoSubmQueue - &pThis->aQueuesSubm[0];
    Assert(idxIoSubmQueue < RT_ELEMENTS(pThis->aQueuesSubm));
    PNVMEQUEUESUBMR3 pIoSubmQueueR3 = &pThisCC->aQueuesSubm[idxIoSubmQueue];
    if (pIoSubmQueueR3->pWrkThrdR3->pThrd->Thread == RTThreadSelf())
        nvmeR3WrkThrdRemoveWorker(pIoSubmQueueR3->pWrkThrdR3, pIoSubmQueue, pIoSubmQueueR3);
    else
        nvmeR3WrkThrdRemoveSubmissionQueue(pDevIns, pIoSubmQueue, pIoSubmQueueR3);

    return nvmeR3CmdCompleteWithSuccess(pDevIns, pThis, pThisCC, pQueueSubm, pIoSubmQueue->u16CidDeallocating,
                                        0 /* Command specific */);
}

/**
 * Processes a "Delete I/O submission queue" command.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pQueueSubm  The submission queue responsible for this command.
 * @param   pCmdAdm     The admin command to process.
 */
static int nvmeR3CmdAdminProcessSqDelete(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                         PNVMEQUEUESUBM pQueueSubm, PNVMECMDADM pCmdAdm)
{
    uint16_t u16SqId = pCmdAdm->u.Field.u.DeleteIoSq.u16SqId;

    /* Check for an invalid queue ID.*/
    if (   u16SqId >= pThis->cQueuesSubmMax
        || u16SqId == NVME_ADM_QUEUE_ID)
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_CMD_INV_QID,
                                           0, false /* fMore */, true /* fDnr */);

    PNVMEQUEUESUBM pIoQueueSubm = &pThis->aQueuesSubm[u16SqId];

    /* The queue must be allocatd to be deleted. */
    if (pIoQueueSubm->Hdr.enmState != NVMEQUEUESTATE_ALLOCATED)
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_CMD_INV_QID,
                                           0, false /* fMore */, true /* fDnr */);

    /* Mark the queue as deallocating. */
    pIoQueueSubm->u16CidDeallocating = pCmdAdm->u.Field.Hdr.u16Cid;
    ASMAtomicWriteU32((volatile uint32_t *)&pIoQueueSubm->Hdr.enmState, NVMEQUEUESTATE_DEALLOCATING);

    /*
     * If there is no request waiting for completion we can do the real deallocation here,
     * otherwise it has to be deferred until the last one completed.
     */
    if (ASMAtomicReadU32(&pIoQueueSubm->cReqsActive) == 0)
        return nvmeR3QueueSubmDeallocateDeferred(pDevIns, pThis, pThisCC, pQueueSubm, pIoQueueSubm);

    return VINF_SUCCESS;
}

/**
 * Converts the priority of the "Create I/O Submission Queue" command to our internal
 * enumeration.
 *
 * @returns The internal enum representation.
 * @param   uPrio      The priority of the command.
 */
static NVMEQUEUESUBMPRIO nvmeR3CmdAdminSqCreatePrio2Enum(uint8_t uPrio)
{
    NVMEQUEUESUBMPRIO enmPrio = NVMEQUEUESUBMPRIO_INVALID;

    switch (uPrio)
    {
        case NVME_CMD_ADM_CREATE_IO_SQ_PRIO_URGENT:
            enmPrio = NVMEQUEUESUBMPRIO_URGENT;
            break;
        case NVME_CMD_ADM_CREATE_IO_SQ_PRIO_HIGH:
            enmPrio = NVMEQUEUESUBMPRIO_HIGH;
            break;
        case NVME_CMD_ADM_CREATE_IO_SQ_PRIO_MEDIUM:
            enmPrio = NVMEQUEUESUBMPRIO_MEDIUM;
            break;
        case NVME_CMD_ADM_CREATE_IO_SQ_PRIO_LOW:
            enmPrio = NVMEQUEUESUBMPRIO_LOW;
            break;
        default:
            AssertMsgFailed(("Impossible to happen!\n"));
    }

    return enmPrio;
}

/**
 * Processes a "Create I/O submission queue" command.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pQueueSubm  The submission queue responsible for this command.
 * @param   pCmdAdm     The admin command to process.
 */
static int nvmeR3CmdAdminProcessSqCreate(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                         PNVMEQUEUESUBM pQueueSubm, PNVMECMDADM pCmdAdm)
{
    uint16_t u16SqId = pCmdAdm->u.Field.u.CreateIoSq.u16SqId;
    uint16_t u16SqSize = pCmdAdm->u.Field.u.CreateIoSq.u16SqSize;
    uint16_t u16CqId = pCmdAdm->u.Field.u.CreateIoSq.u16CqId;
    uint8_t uPrio = NVME_CMD_ADM_CREATE_IO_SQ_PRIO_GET(pCmdAdm->u.Field.u.CreateIoSq.u16Flags);
    NVMEQUEUESUBMPRIO enmPrio = nvmeR3CmdAdminSqCreatePrio2Enum(uPrio);

    /* Check for an invalid queue ID.*/
    if (   u16SqId >= pThis->cQueuesSubmMax
        || u16SqId == 0)
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_CMD_INV_QID,
                                           0, false /* fMore */, true /* fDnr */);

    /* Check for valid queue size. */
    if (   u16SqSize >= pThis->cQueueEntriesMax
        || u16SqSize == 0)
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_CMD_INV_Q_SZ,
                                           0, false /* fMore */, true /* fDnr */);

    /* Check whether the queue is in use already. */
    if (ASMAtomicReadU32((volatile uint32_t *)&pThis->aQueuesSubm[u16SqId].Hdr.enmState) != NVMEQUEUESTATE_DEALLOCATED)
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_CMD_INV_QID,
                                           0, false /* fMore */, true /* fDnr */);

    /* Our emulation works only with physically contiguous queues at the moment. */
    if (!(pCmdAdm->u.Field.u.CreateIoSq.u16Flags & NVME_CMD_ADM_CREATE_IO_SQ_PC))
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_GENERIC, NVME_CQ_ENTRY_SC_GEN_INV_CMD_FIELD,
                                           0, false /* fMore */, true /* fDnr */);

    /* Check that the completion queue is valid and was created. */
    if (   u16CqId >= pThis->cQueuesCompMax
        || u16CqId == NVME_ADM_QUEUE_ID
        || ASMAtomicReadU32((volatile uint32_t *)&pThis->aQueuesComp[u16CqId].Hdr.enmState) != NVMEQUEUESTATE_ALLOCATED)
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_CMD_INV_QID,
                                           0, false /* fMore */, true /* fDnr */);

    /* Retain reference of associated completion queue. */
    PNVMEQUEUECOMP pIoQueueComp = &pThis->aQueuesComp[u16CqId];
    ASMAtomicIncU32(&pIoQueueComp->cSubmQueuesRef);

    PNVMEQUEUESUBM pIoQueueSubm = &pThis->aQueuesSubm[u16SqId];
    pIoQueueSubm->Hdr.u16Id            = u16SqId;
    pIoQueueSubm->Hdr.cEntries         = u16SqSize + 1; /* 0 based value. */
    pIoQueueSubm->Hdr.cbEntry          = RT_BIT_32(pThis->u32IoSubmissionQueueEntrySize);
    pIoQueueSubm->Hdr.fPhysCont        = true;
    pIoQueueSubm->Hdr.enmType          = NVMEQUEUETYPE_SUBMISSION;
    pIoQueueSubm->Hdr.GCPhysBase       = pCmdAdm->u.Field.Prp1;
    pIoQueueSubm->Hdr.idxHead          = 0;
    pIoQueueSubm->Hdr.idxTail          = 0;
    pIoQueueSubm->u16CompletionQueueId = u16CqId;
    pIoQueueSubm->enmPriority          = enmPrio;
    pIoQueueSubm->cReqsActive          = 0;

    ASMAtomicWriteU32((volatile uint32_t *)&pIoQueueSubm->Hdr.enmState, NVMEQUEUESTATE_ALLOCATED);
    int rc = nvmeR3SubmQueueAssignToWorker(pDevIns, pThis, pThisCC, pIoQueueSubm, &pThisCC->aQueuesSubm[u16SqId]);
    if (RT_FAILURE(rc))
    {
        ASMAtomicDecU32(&pIoQueueComp->cSubmQueuesRef);
        rc = nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                         NVME_CQ_ENTRY_SCT_CMD_GENERIC, NVME_CQ_ENTRY_SC_GEN_INTERNAL_ERR,
                                         0, false /* fMore */, true /* fDnr */);
    }
    else
        rc = nvmeR3CmdCompleteWithSuccess(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                          0 /* Command specific */);

    return rc;
}

/**
 * Processes a "Get Log Page" command.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pQueueSubm  The submission queue responsible for this command.
 * @param   pCmdAdm     The admin command to process.
 */
static int nvmeR3CmdAdminProcessGetLogPg(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                         PNVMEQUEUESUBM pQueueSubm, PNVMECMDADM pCmdAdm)
{
    switch (pCmdAdm->u.Field.u.GetLogPage.u8LogPgId)
    {
        case NVME_LOG_PAGE_ID_ERROR_INFORMATION:
            break;
        case NVME_LOG_PAGE_ID_HEALTH_INFORMATION:
            break;
        case NVME_LOG_PAGE_ID_FIRMWARE_SLOT_INFORMATION:
            break;
        default:
            return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                               NVME_CQ_ENTRY_SCT_CMD_GENERIC, NVME_CQ_ENTRY_SC_GEN_INV_CMD_FIELD,
                                               0, false /* fMore */, true /* fDnr */);
    }

    return nvmeR3CmdCompleteWithSuccess(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                        0 /* Command specific */);
}

/**
 * Processes a "Delete I/O completion queue" command.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pQueueSubm  The submission queue responsible for this command.
 * @param   pCmdAdm     The admin command to process.
 */
static int nvmeR3CmdAdminProcessCqDelete(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                         PNVMEQUEUESUBM pQueueSubm, PNVMECMDADM pCmdAdm)
{
    uint16_t u16CqId = pCmdAdm->u.Field.u.DeleteIoCq.u16CqId;

    /* Check for an invalid queue ID.*/
    if (   u16CqId >= pThis->cQueuesCompMax
        || u16CqId == NVME_ADM_QUEUE_ID)
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_CMD_INV_QID,
                                           0, false /* fMore */, true /* fDnr */);

    PNVMEQUEUECOMP pIoQueueComp = &pThis->aQueuesComp[u16CqId];

    /* Check that there is no assigned submission queue. */
    if (ASMAtomicReadU32(&pIoQueueComp->cSubmQueuesRef) != 0)
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_CMD_INV_Q_DEL,
                                           0, false /* fMore */, true /* fDnr */);

    /*
     * Mark queue as deallocated and clear a few state members (paranoia).
     *
     * There is no need to go through the deallocating state as there can't be any requests
     * active which will complete on this queue if all assigned submission queues are deallocated.
     */
    ASMAtomicWriteU32((volatile uint32_t *)&pIoQueueComp->Hdr.enmState, NVMEQUEUESTATE_DEALLOCATED);

    uintptr_t idxIoQueueComp = pIoQueueComp - &pThis->aQueuesComp[0];
    Assert(idxIoQueueComp < RT_ELEMENTS(pThis->aQueuesComp));

    PNVMEQUEUECOMPR3 pNvmeQueueR3 = &pThisCC->aQueuesComp[idxIoQueueComp];
    RTSemFastMutexRequest(pNvmeQueueR3->hMtx);
    /* Destroy all waiters. */
    PNVMECOMPQUEUEWAITER pWaiter;
    PNVMECOMPQUEUEWAITER pWaiterNext;
    RTListForEachSafe(&pNvmeQueueR3->LstCompletionsWaiting, pWaiter, pWaiterNext, NVMECOMPQUEUEWAITER, NdLstQueue)
    {
        RTListNodeRemove(&pWaiter->NdLstQueue);
        RTMemFree(pWaiter);
    }
    Assert(RTListIsEmpty(&pNvmeQueueR3->LstCompletionsWaiting));
    RTSemFastMutexRelease(pNvmeQueueR3->hMtx);

    int rc = RTSemFastMutexDestroy(pNvmeQueueR3->hMtx);
    AssertRC(rc);

    pIoQueueComp->Hdr.u16Id            = 0;
    pIoQueueComp->Hdr.cEntries         = 0;
    pIoQueueComp->Hdr.cbEntry          = 0;
    pIoQueueComp->Hdr.fPhysCont        = true;
    pIoQueueComp->Hdr.enmType          = NVMEQUEUETYPE_INVALID;
    pIoQueueComp->Hdr.GCPhysBase       = NIL_RTGCPHYS;
    pIoQueueComp->Hdr.idxHead          = 0;
    pIoQueueComp->Hdr.idxTail          = 0;
    pIoQueueComp->fIntrEnabled         = false;
    pIoQueueComp->u32IntrVec           = 0;
    pIoQueueComp->fOverloaded          = false;
    pIoQueueComp->cWaiters             = 0;
    pThisCC->aQueuesComp[idxIoQueueComp].hMtx = NIL_RTSEMFASTMUTEX;

    return nvmeR3CmdCompleteWithSuccess(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                        0 /* Command specific */);
}

/**
 * Processes a "Create I/O completion queue" command.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pQueueSubm  The submission queue responsible for this command.
 * @param   pCmdAdm     The admin command to process.
 */
static int nvmeR3CmdAdminProcessCqCreate(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                         PNVMEQUEUESUBM pQueueSubm, PNVMECMDADM pCmdAdm)
{
    uint16_t u16CqId = pCmdAdm->u.Field.u.CreateIoCq.u16CqId;
    uint16_t u16CqSize = pCmdAdm->u.Field.u.CreateIoCq.u16CqSize;
    uint32_t u32IntrVec = pCmdAdm->u.Field.u.CreateIoCq.u16IntrVec;

    /* Check for an invalid queue ID.*/
    if (   u16CqId >= pThis->cQueuesCompMax
        || u16CqId == NVME_ADM_QUEUE_ID)
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_CMD_INV_QID,
                                           0, false /* fMore */, true /* fDnr */);

    /* Check for valid queue size. */
    if (   u16CqSize >= pThis->cQueueEntriesMax
        || u16CqSize == 0)
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_CMD_INV_Q_SZ,
                                           0, false /* fMore */, true /* fDnr */);

    /* Check whether the queue is in use already. */
    if (ASMAtomicReadU32((volatile uint32_t *)&pThis->aQueuesComp[u16CqId].Hdr.enmState) != NVMEQUEUESTATE_DEALLOCATED)
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_CMD_INV_QID,
                                           0, false /* fMore */, true /* fDnr */);

    /* Our emulation works only with physically contiguous queues at the moment. */
    if (!(pCmdAdm->u.Field.u.CreateIoCq.u16Flags & NVME_CMD_ADM_CREATE_IO_CQ_PC))
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_GENERIC, NVME_CQ_ENTRY_SC_GEN_INV_CMD_FIELD,
                                           0, false /* fMore */, true /* fDnr */);

    /* Check that the specified interrupt vector is valid. */
    if (   u32IntrVec >= NVME_INTR_VEC_MAX
        || (!nvmeIsMSIEnabled(pDevIns->apPciDevs[0]) && u32IntrVec != 0))
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_CMD_INV_INTR_VEC,
                                           0, false /* fMore */, true /* fDnr */);

    PNVMEQUEUECOMP   pIoQueueComp   = &pThis->aQueuesComp[u16CqId];
    PNVMEQUEUECOMPR3 pIoQueueCompR3 = &pThisCC->aQueuesComp[u16CqId];

    int rc = RTSemFastMutexCreate(&pIoQueueCompR3->hMtx);
    if (RT_FAILURE(rc))
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_GENERIC, NVME_CQ_ENTRY_SC_GEN_INTERNAL_ERR,
                                           0, false /* fMore */, true /* fDnr */);

    pIoQueueComp->Hdr.u16Id            = u16CqId;
    pIoQueueComp->Hdr.cEntries         = u16CqSize + 1; /* 0 based value */
    pIoQueueComp->Hdr.cbEntry          = RT_BIT_32(pThis->u32IoCompletionQueueEntrySize);
    pIoQueueComp->Hdr.fPhysCont        = true;
    pIoQueueComp->Hdr.enmType          = NVMEQUEUETYPE_COMPLETION;
    pIoQueueComp->Hdr.GCPhysBase       = pCmdAdm->u.Field.Prp1;
    pIoQueueComp->Hdr.idxHead          = 0;
    pIoQueueComp->Hdr.idxTail          = 0;
    pIoQueueComp->fIntrEnabled         = RT_BOOL(pCmdAdm->u.Field.u.CreateIoSq.u16Flags & NVME_CMD_ADM_CREATE_IO_CQ_IEN);
    pIoQueueComp->u32IntrVec           = u32IntrVec;
    pIoQueueComp->cSubmQueuesRef       = 0;
    pIoQueueComp->fOverloaded          = false;
    RTListInit(&pIoQueueCompR3->LstCompletionsWaiting);

    ASMAtomicWriteU32((volatile uint32_t *)&pIoQueueComp->Hdr.enmState, NVMEQUEUESTATE_ALLOCATED);

    return nvmeR3CmdCompleteWithSuccess(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                        0 /* Command specific */);
}

/**
 * Copies a given string to the destination padding the remainder with spaces.
 *
 * @param   pszDst    Where to store the destination string.
 * @param   pszSrc    The source to copy and pad.
 * @param   cchDst    Size of the destination buffer.
 */
static void nvmeR3CopyStrPad(uint8_t *pszDst, const char *pszSrc, size_t cchDst)
{
    size_t cchSrc = strlen(pszSrc);

    Assert(cchSrc <= cchDst);
    cchSrc = RT_MIN(cchSrc, cchDst);
    memcpy(pszDst, pszSrc, cchSrc);

    /* Pad the remainder with spaces. */
    size_t cchLeft = cchDst - cchSrc;
    pszDst += cchSrc;
    while (cchLeft)
    {
        *pszDst++ = ' ';
        cchLeft--;
    }
}

/**
 * Fills a power state descriptor.
 *
 * @param   pPwrStateDesc The power state descriptor to fill.
 */
static void nvmeR3IdentifyCtrlFillPwrStateDesc(PNVMEPWRSTATESDESC pPwrStateDesc)
{
    pPwrStateDesc->u16PwrMax               = 1;
    pPwrStateDesc->u8MaxPwrScaleAndOpState = 0; /* Processing I/O commands */
    pPwrStateDesc->u32EntryLat             = 0;
    pPwrStateDesc->u32ExitLat              = 0;
    pPwrStateDesc->u8ReadThroughputRel     = 0; /* Highest read throughput. */
    pPwrStateDesc->u8ReadLatencyRel        = 0; /* Lowest read latency. */
    pPwrStateDesc->u8WriteThroughputRel    = 0; /* Highest write throughput. */
    pPwrStateDesc->u8WriteLatencyRel       = 0; /* Lowest write latency. */
    pPwrStateDesc->u16PwrIdle              = 0;
}

/**
 * Fills the Identify Controller data structure and copies it to the guest.
 *
 * @returns Flag whther the copy to guest memory succeeded.
 * @retval  true if the copy succeeded
 * @retval  false if a misaligned PRP entry was encountered.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pCmdAdm     The Identify admin command.
 */
static bool nvmeR3IdentifyCtrl(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC, PNVMECMDADM pCmdAdm)
{
    NVMEIDENTIFYCTRL IdCtrl;

    RT_ZERO(IdCtrl);

    IdCtrl.u16VendorId        = NVME_PCI_VENDOR_ID;
    IdCtrl.u16SubSysVendorId  = NVME_PCI_VENDOR_ID;
    nvmeR3CopyStrPad(&IdCtrl.achSerialNumber[0], &pThis->szSerialNumber[0], NVME_SERIAL_NUMBER_LENGTH);
    nvmeR3CopyStrPad(&IdCtrl.achModelNumber[0], &pThis->szModelNumber[0], NVME_MODEL_NUMBER_LENGTH);
    nvmeR3CopyStrPad(&IdCtrl.achFirmwareRevision[0], &pThis->szFirmwareRevision[0], NVME_FIRMWARE_REVISION_LENGTH);
    IdCtrl.u8RecomdArbitBurst = 0;
    IdCtrl.au8IeeeOui[0] = IdCtrl.au8IeeeOui[1] = IdCtrl.au8IeeeOui[2] = 0; /** @todo Find a proper value */
    IdCtrl.u8MultPathIoNsSharing = 0; /* No Multi-Path I/O or namespace sharing supported. */
    IdCtrl.u8DataXferSzMax       = 0; /* No restriction on the maximum data transfer size. */
    IdCtrl.u16CtrlId             = 0; /* One controller only. */
    IdCtrl.u32Version            = NVME_VS_MJR(1) | NVME_VS_MNR(2);
    IdCtrl.u32Rtd3ResumeLat      = 1; /* Must be non-zero for 1.2 compliant devices. */
    IdCtrl.u32Rtd3EntryLat       = 1; /* Must be non-zero for 1.2 compliant devices. */
    IdCtrl.u32OptAsyncEvtsSupported = 0; /* Optional Asynchronous Events not supported */
    IdCtrl.u16OptAdmCmdSupported    = 0; /* Optional admin commands not supported */
    IdCtrl.u8AbrtCmdLimit           = 4; /* Recommendation from the spec but in theory unlimited for our implementation */
    IdCtrl.u8AsyncEvtReqLimit       = RT_MIN(pThisCC->cAsyncEvtReqsMax - 1, 0xff); /* Zero based limit. */
    IdCtrl.u8FwUpdates              = (1 << 1) | RT_BIT(1); /* Only one firmware slot supported, slot 0 is readonly => no firmware upgrade possible */
    IdCtrl.u8LgPgAttr               = 0; /* No command effects log page, no per namespace SMART information */
    IdCtrl.u8ErrLgPgEnt             = NVME_LOG_PAGE_ERROR_ENTRIES - 1; /* 0's based value */
    IdCtrl.u8PwrStatesSupported     = 0; /* Just ine power state supported (0's based value) */
    IdCtrl.u8AdmVendorSpecCmdCfg    = 0; /* Vendor specific command format is vendor specific (not that we implement any yet) */
    IdCtrl.u8AutPwrStateTransAttr   = 0; /* No autonomous power state transitions */
    IdCtrl.u16WarnCompTempThreshold = 0x157; /* Recommend value from the specification, not that is of any use for an emulated device */
    IdCtrl.u16CritCompTempThreshold = 0x157; /* Same as warning temperature threshold. */
    IdCtrl.u16MaxTimeFwAct          = 0; /* Undefined time required */
    IdCtrl.u32HostMemBufMinSz       = 0; /* Host memory feature unsupported */
    IdCtrl.u32HostMemBufPrefSz      = 0; /* Host memory feature unsupported */
#if 0 /* Filling these values is not required as we don't support namespace management. */
    IdCtrl.au8NvmCapacity;
    IdCtrl.au8NvmCapUnallocated;
#endif
    IdCtrl.u32ReplayProtMemBlockSupport = 0; /* Replay protected memory blocks not supported. */
    IdCtrl.u8SubmQueueEntSz             = (6 << 4) | 6; /* Maximum and preferred size, in 2^n (64 bytes) */
    IdCtrl.u8CompQueueEntSz             = (4 << 4) | 4; /* Maximum and preferred size, in 2^n (16 bytes) */
    IdCtrl.u32Namespaces                = pThis->cNamespaces;
    IdCtrl.u16OptNvmCmdSupported        = 0; /* No optional NVM command set supported */
    IdCtrl.u16FusedOpSupported          = 0; /* Fused operations are not supported */
    IdCtrl.u8NvmFmtAttr                 = 0; /* No special NVM format options supported */
    IdCtrl.u8VolWriteCache              = 0; /* No volatile write cache present. */
    IdCtrl.u16AtomicWriteUnitNormal     = 0; /* Atomic write is only one block large. */
    IdCtrl.u16AtomicWriteUnitPwrFail    = 0; /* Atomic write is only one block large. */
    IdCtrl.u8NvmVendorSpecCmdCfg        = 0; /* NVM vendor specific commands have a vendor specific format. */
    IdCtrl.u16AtomicWriteCmpUnit        = 0; /* Fused Compare & Write operation not supported, so this field is not valid. */
    IdCtrl.u32SglSupported              = 0; /** @todo No SGL support for NVM commands, this needs to be implemented for better performance. */
    nvmeR3IdentifyCtrlFillPwrStateDesc(&IdCtrl.aPwrStateDesc[0]);

    return nvmeR3CopyBufferToPrps(pDevIns, pThis, pThisCC, pCmdAdm->u.Field.Prp1, pCmdAdm->u.Field.Prp2, sizeof(IdCtrl),
                                  &IdCtrl, sizeof(IdCtrl), 0 /* cbSkip */, false /* fListsAllowed */);
}

/**
 * Returns the power of two (2^n) value for the given unsigned integer.
 *
 * @returns n as in 2^n
 * @param   u64Val    The value to get the power of two from.
 *
 * @note This works only if u64Val is aligned to a power of two (asserts otherwise).
 */
static uint8_t nvmeR3GetPowerOfTwo(uint64_t u64Val)
{
    uint8_t u8Pwr = 0;

    while (RT_BIT_64(u8Pwr) < u64Val)
        u8Pwr++;

    AssertMsg(RT_BIT_64(u8Pwr) == u64Val,
              ("The given value %RU64 (0x%RX64) is not aligned to a power of two\n",
               u64Val, u64Val));

    return u8Pwr;
}

/**
 * Fills the LBA format descriptor for the given namespace.
 *
 * @param   pNamespace  The namespace.
 * @param   pLbaFmtDesc The LBA format descriptor.
 */
static void nvmeR3IdentifyNsFillLbaFmtDesc(PNVMENAMESPACE pNamespace, PNVMELBAFMTDESC pLbaFmtDesc)
{
    pLbaFmtDesc->u16MetadataSz = 0; /* No metadata supported. */
    pLbaFmtDesc->u8LbaSz       = nvmeR3GetPowerOfTwo(pNamespace->cbBlock);
    pLbaFmtDesc->u8PerfRel     = NVMELBAFMTDESC_PERF_RELATIVE_BEST;
}

/**
 * Fills the Identify Namespace data structure and copies it to the guest.
 *
 * @returns Flag whther the copy to guest memory succeeded.
 * @retval  true if the copy succeeded
 * @retval  false if a misaligned PRP entry was encountered.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pCmdAdm     The Identify admin command.
 * @param   uNsId       The namespace ID the Identify command is for.
 */
static bool nvmeR3IdentifyNamespace(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                    PNVMECMDADM pCmdAdm, NVMENSID uNsId)
{
    NVMEIDENTIFYNS IdNs;
    RT_ZERO(IdNs);

    PNVMENAMESPACE pNamespace = &pThisCC->paNamespaces[uNsId];
    if (pNamespace->cBlocks)
    {
        RTUUID Uuid;

        RTUuidClear(&Uuid);
        pNamespace->pDrvMedia->pfnGetUuid(pNamespace->pDrvMedia, &Uuid);

        /* Active namespace, fill in data. */
        IdNs.u64NsSz                      = pNamespace->cBlocks;
        IdNs.u64NsCap                     = pNamespace->cBlocks;
        IdNs.u64NsUtil                    = pNamespace->cBlocks;
        IdNs.u8NsFeat                     = 0; /** @todo No special features, implement discarding. */
        IdNs.u8LbaFmts                    = 0; /* Zero based value, support only one LBA format at the moment. */
        IdNs.u8LbaSzFmt                   = 0; /* The first LBA format. */
        IdNs.u8MetadataCap                = 0; /* No support for transferring metadata. */
        IdNs.u8End2EndProtCap             = 0; /* No support for sending protection information. */
        IdNs.u8End2EndProtCfg             = 0; /* No protection information enabled. */
        IdNs.u8NsMultPathAndNsSharingCap  = 0; /* No multiparth supported. */
        IdNs.u8ReservCap                  = 0; /* No reservation capabilities. */
        IdNs.u8FmtProgressInd             = 0; /* Format complete. */
        IdNs.u16NsAtomicWriteUnitNormal   = 0; /* Same as in the controller identify structure. */
        IdNs.u16NsAtomicWriteUnitPwrFail  = 0; /* Same as in the controller identify structure. */
        IdNs.u16NsAtomicWriteCmpUnit      = 0; /* Same as in the controller identify structure. */
        IdNs.u16NsAtomicBoundarySzNormal  = 0; /* Same as in the controller identify structure. */
        IdNs.u16NsAtomicBoundaryOffset    = 0; /* Same as in the controller identify structure. */
        IdNs.u16NsAtomicBoundarySzPwrFail = 0; /* Same as in the controller identify structure. */
        memcpy(&IdNs.au8NsGuid[0], &Uuid, sizeof(IdNs.au8NsGuid)); /* Copy UUID over. */
        IdNs.u64IeeeUi64                  = 0; /* Keep cleared to 0 (see section 7.9 of NVME specification). */
        nvmeR3IdentifyNsFillLbaFmtDesc(pNamespace, &IdNs.aLbaFmts[0]);
    }

    return nvmeR3CopyBufferToPrps(pDevIns, pThis, pThisCC, pCmdAdm->u.Field.Prp1, pCmdAdm->u.Field.Prp2, sizeof(IdNs),
                                  &IdNs, sizeof(IdNs), 0 /* cbSkip */, false /* fListsAllowed */);
}

/**
 * Fills the Identify Namespace data structure which is common to all attached namespaces
 * and copies it to the guest.
 *
 * @returns Flag whther the copy to guest memory succeeded.
 * @retval  true if the copy succeeded
 * @retval  false if a misaligned PRP entry was encountered.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pCmdAdm     The Identify admin command.
 */
static bool nvmeR3IdentifyNamespaceCommon(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC, PNVMECMDADM pCmdAdm)
{
    NVMEIDENTIFYNS IdNs;
    RT_ZERO(IdNs);

    return nvmeR3CopyBufferToPrps(pDevIns, pThis, pThisCC, pCmdAdm->u.Field.Prp1, pCmdAdm->u.Field.Prp2, sizeof(IdNs),
                                  &IdNs, sizeof(IdNs), 0 /* cbSkip */, false /* fListsAllowed */);
}

/**
 * Creates a list of active namespaces starting at the given ID.
 *
 * @returns Flag whther the copy to guest memory succeeded.
 * @retval  true if the copy succeeded
 * @retval  false if a misaligned PRP entry was encountered.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pCmdAdm     The Identify admin command.
 * @param   uNsIdStart  The namespace identifier to start at.
 */
static bool nvmeR3IdentifyActiveNamespaceList(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                              PNVMECMDADM pCmdAdm, NVMENSID uNsIdStart)
{
    unsigned idxNs = 0;
    NVMENSID aNsIds[_1K];

    RT_BZERO(&aNsIds[0], sizeof(aNsIds));

    uNsIdStart++; /* We report active namespace IDs greater than the specified value. */
    while (   uNsIdStart <= pThis->cNamespaces
           && idxNs < RT_ELEMENTS(aNsIds))
    {
        PNVMENAMESPACE pNamespace = &pThisCC->paNamespaces[idxNs];
        if (pNamespace->cBlocks)
            aNsIds[idxNs++] = uNsIdStart;

        uNsIdStart++;
    }

    return nvmeR3CopyBufferToPrps(pDevIns, pThis, pThisCC, pCmdAdm->u.Field.Prp1, pCmdAdm->u.Field.Prp2, sizeof(aNsIds),
                                  &aNsIds, sizeof(aNsIds), 0 /* cbSkip */, false /* fListsAllowed */);
}

/**
 * Processes a "Identify" command.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pQueueSubm  The submission queue responsible for this command.
 * @param   pCmdAdm     The admin command to process.
 */
static int nvmeR3CmdAdminProcessIdentify(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                         PNVMEQUEUESUBM pQueueSubm, PNVMECMDADM pCmdAdm)
{
    NVMENSID uNsId = pCmdAdm->u.Field.uNsId;
    uint8_t u8Cns = pCmdAdm->u.Field.u.Identify.u8Cns;
    int rc = VINF_SUCCESS;
    bool fCopySucceeded = true;

    switch (u8Cns)
    {
        case NVME_CMD_ADM_IDENTIFY_CNS_IDENTIFY_CTL:
            fCopySucceeded = nvmeR3IdentifyCtrl(pDevIns, pThis, pThisCC, pCmdAdm);
            break;
        case NVME_CMD_ADM_IDENTIFY_CNS_ACTIVE_NS:
            fCopySucceeded = nvmeR3IdentifyActiveNamespaceList(pDevIns, pThis, pThisCC, pCmdAdm, uNsId);
            break;
        case NVME_CMD_ADM_IDENTIFY_CNS_IDENTIFY_NS:
            if (   uNsId > 0
                && uNsId <= pThis->cNamespaces)
                fCopySucceeded = nvmeR3IdentifyNamespace(pDevIns, pThis, pThisCC, pCmdAdm, uNsId - 1);
            else if (uNsId == UINT32_C(0xffffffff))
                fCopySucceeded = nvmeR3IdentifyNamespaceCommon(pDevIns, pThis, pThisCC, pCmdAdm);
            else
                return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                                   NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_CMD_NS_ID_UNAVAILABLE,
                                                   0, false /* fMore */, true /* fDnr */);
            break;
        default:
            return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                               NVME_CQ_ENTRY_SCT_CMD_GENERIC, NVME_CQ_ENTRY_SC_GEN_INV_CMD_FIELD,
                                               0, false /* fMore */, true /* fDnr */);
    }

    if (fCopySucceeded)
        rc = nvmeR3CmdCompleteWithSuccess(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                          0 /* Command specific */);
    else
        rc = nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                         NVME_CQ_ENTRY_SCT_CMD_GENERIC, NVME_CQ_ENTRY_SC_GEN_INV_PRP_OFFSET,
                                         0, false /* fMore */, true /* fDnr */);

    return rc;
}

/**
 * Processes a "Abort" command.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pQueueSubm  The submission queue responsible for this command.
 * @param   pCmdAdm     The admin command to process.
 */
static int nvmeR3CmdAdminProcessAbort(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                      PNVMEQUEUESUBM pQueueSubm, PNVMECMDADM pCmdAdm)
{
    uint16_t u16SqId = pCmdAdm->u.Field.u.Abort.u16SqId;
    uint16_t u16Cid  = pCmdAdm->u.Field.u.Abort.u16Cid;

    /* Check for valid submission queue identifier. */
    if (   u16SqId >= pThis->cQueuesSubmMax
        || u16SqId == 0)
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_CMD_INV_QID,
                                           0, false /* fMore */, true /* fDnr */);

    /* Get the I/O submission queue to abort the command for. */
#ifdef VBOX_STRICT
    PNVMEQUEUESUBM pQueueSubmAbrt = &pThis->aQueuesSubm[u16SqId];
    Assert(   pQueueSubmAbrt->Hdr.enmState == NVMEQUEUESTATE_ALLOCATED
           || pQueueSubmAbrt->Hdr.enmState == NVMEQUEUESTATE_DEALLOCATING);
#endif

    bool fFound = false;
    for (uint32_t i = 0; i < pThis->cNamespaces; i++)
    {
        PNVMENAMESPACE pNvmeNs = &pThisCC->paNamespaces[i];
        int rc = pNvmeNs->pDrvMediaEx->pfnIoReqCancel(pNvmeNs->pDrvMediaEx, NVME_IOREQID_FROM_QID_AND_CID(pQueueSubm->Hdr.u16Id, u16Cid));
        if (rc != VERR_PDM_MEDIAEX_IOREQID_NOT_FOUND)
        {
            fFound = true;
            break;
        }
    }

    int rc = VINF_SUCCESS;
    if (!fFound)
        rc = nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                         NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_GEN_INV_CMD_FIELD,
                                         0, false /* fMore */, true /* fDnr */);

    return rc;
}

/**
 * Processes a "Set Features" command.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pQueueSubm  The submission queue responsible for this command.
 * @param   pCmdAdm     The admin command to process.
 */
static int nvmeR3CmdAdminProcessSetFeatures(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                            PNVMEQUEUESUBM pQueueSubm, PNVMECMDADM pCmdAdm)
{
    int rc = VINF_SUCCESS;

    if (pCmdAdm->u.Field.u.SetFeatures.u8Save & NVME_CMD_ADM_SET_FEAT_SV)
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_CMD_FEAT_ID_NOT_SAVEABLE,
                                           0, false /* fMore */, true /* fDnr */);

    switch (pCmdAdm->u.Field.u.SetFeatures.u8FeatId)
    {
        case NVME_FEAT_ID_NUMBER_OF_QUEUES:
            /*
             * Always report back the maximum number of queues we support, the returned
             * value is allowed to be smaller or bigger than the specified value, see
             * chapter 5.14.1.7.
             */
            rc = nvmeR3CmdCompleteWithSuccess(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                              ((uint32_t)pThis->cQueuesCompMax << 16) | pThis->cQueuesSubmMax);
            break;
        case NVME_FEAT_ID_ASYNC_EVT_CFG:
            /* Nothing to do as the we don't generate any events which can be configured here. */
            rc = nvmeR3CmdCompleteWithSuccess(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid, 0);
            break;
        default: /* All other features are not changeable at the moment. */
            rc = nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                             NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_CMD_FEAT_NOT_CHANGEABLE,
                                             0, false /* fMore */, true /* fDnr */);
    }

    return rc;
}

/**
 * Processes a "Get Features" command.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pQueueSubm  The submission queue responsible for this command.
 * @param   pCmdAdm     The admin command to process.
 */
static int nvmeR3CmdAdminProcessGetFeatures(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                            PNVMEQUEUESUBM pQueueSubm, PNVMECMDADM pCmdAdm)
{
    uint32_t u32CmdSpecific = 0;

    /* If the guest asks only for the supported capabilities of the feature return them. */
    if (pCmdAdm->u.Field.u.GetFeatures.u8Sel == NVME_CMD_ADM_GET_FEAT_SELECT_SUPPORTED_CAPS)
    {
        switch (pCmdAdm->u.Field.u.GetFeatures.u8FeatId)
        {
            case NVME_FEAT_ID_ARBITRATION:
            case NVME_FEAT_ID_POWER_MGMT:
            case NVME_FEAT_ID_TEMP_THRESHOLD:
            case NVME_FEAT_ID_ERROR_RECOVERY:
            case NVME_FEAT_ID_INTERRUPT_COALESCING:
            case NVME_FEAT_ID_INTERRUPT_VEC_CFG:
            case NVME_FEAT_ID_WRITE_ATOMICITY_NORMAL:
            case NVME_FEAT_ID_ASYNC_EVT_CFG:
                u32CmdSpecific = 0; /* Feature is not changeable, saveable and applies to complete controller. */
                break;
            case NVME_FEAT_ID_NUMBER_OF_QUEUES:
                u32CmdSpecific = RT_BIT(2); /* Feature is changeable. */
                break;
            default:
                return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                                   NVME_CQ_ENTRY_SCT_CMD_GENERIC, NVME_CQ_ENTRY_SC_GEN_INV_CMD_FIELD,
                                                   0, false /* fMore */, true /* fDnr */);
        }
    }
    else
    {
        switch (pCmdAdm->u.Field.u.GetFeatures.u8FeatId)
        {
            case NVME_FEAT_ID_ARBITRATION:
                break;
            case NVME_FEAT_ID_POWER_MGMT:
                break;
            case NVME_FEAT_ID_TEMP_THRESHOLD:
                break;
            case NVME_FEAT_ID_ERROR_RECOVERY:
                break;
            case NVME_FEAT_ID_NUMBER_OF_QUEUES:
                u32CmdSpecific = ((uint32_t)pThis->cQueuesCompMax << 16) | pThis->cQueuesSubmMax;
                break;
            case NVME_FEAT_ID_INTERRUPT_COALESCING:
                break;
            case NVME_FEAT_ID_INTERRUPT_VEC_CFG:
                break;
            case NVME_FEAT_ID_WRITE_ATOMICITY_NORMAL:
                break;
            case NVME_FEAT_ID_ASYNC_EVT_CFG:
                break;
            default:
                return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                                   NVME_CQ_ENTRY_SCT_CMD_GENERIC, NVME_CQ_ENTRY_SC_GEN_INV_CMD_FIELD,
                                                   0, false /* fMore */, true /* fDnr */);
        }
    }

    return nvmeR3CmdCompleteWithSuccess(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                        u32CmdSpecific);
}

/**
 * Processes a "Asynchronous event request" command.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pQueueSubm  The submission queue responsible for this command.
 * @param   pCmdAdm     The admin command to process.
 */
static int nvmeR3CmdAdminProcessAsyncEvtReq(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                            PNVMEQUEUESUBM pQueueSubm, PNVMECMDADM pCmdAdm)
{
    /* If there is a free entry for the async event request store it but don't complete the command. */
    if (pThisCC->cAsyncEvtReqsCur == pThisCC->cAsyncEvtReqsMax)
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_CMD_ASYNC_EVT_LIM_EXCD,
                                           0, false /* fMore */, true /* fDnr */);

    RTCritSectEnter(&pThisCC->CritSectAsyncEvtReqs);
    pThisCC->paAsyncEvtReqCids[pThisCC->cAsyncEvtReqsCur] = pCmdAdm->u.Field.Hdr.u16Cid;
    pThisCC->cAsyncEvtReqsCur++;
    RTCritSectLeave(&pThisCC->CritSectAsyncEvtReqs);
    return VINF_SUCCESS;
}

/**
 * Process a admin comand.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pWrkThrd    The work thread processing the command.
 * @param   pQueueSubm  The submission queue responsible for this command.
 * @param   pCmdAdm     The admin command to process.
 */
static int nvmeR3CmdAdminProcess(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC, PNVMEWRKTHRD pWrkThrd,
                                 PNVMEQUEUESUBM pQueueSubm, PNVMECMDADM pCmdAdm)
{
    int rc = VINF_SUCCESS;

    LogFlow(("Wrk#%u: Processing admin command %#x\n", pWrkThrd->uId, pCmdAdm->u.Field.Hdr.u16Cid));
    nvmeR3CmdAdminDump(pWrkThrd, pCmdAdm);
    RT_NOREF(pWrkThrd);

    if (RT_UNLIKELY(   !NVME_CMD_HDR_PSDT_IS_PRP(pCmdAdm->u.Field.Hdr.u2Psdt)
                    || !NVME_CMD_HDR_FUSE_IS_NORMAL_OP(pCmdAdm->u.Field.Hdr.u2Fuse)))
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_GENERIC, NVME_CQ_ENTRY_SC_GEN_INV_CMD_FIELD,
                                           0, false /* fMore */, true /* fDnr */);

    switch (pCmdAdm->u.Field.Hdr.u8Opc)
    {
        case NVME_CMD_ADM_OPC_SQ_DEL:
            rc = nvmeR3CmdAdminProcessSqDelete(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm);
            break;
        case NVME_CMD_ADM_OPC_SQ_CREATE:
            rc = nvmeR3CmdAdminProcessSqCreate(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm);
            break;
        case NVME_CMD_ADM_OPC_GET_LOG_PG:
            rc = nvmeR3CmdAdminProcessGetLogPg(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm);
            break;
        case NVME_CMD_ADM_OPC_CQ_DEL:
            rc = nvmeR3CmdAdminProcessCqDelete(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm);
            break;
        case NVME_CMD_ADM_OPC_CQ_CREATE:
            rc = nvmeR3CmdAdminProcessCqCreate(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm);
            break;
        case NVME_CMD_ADM_OPC_IDENTIFY:
            rc = nvmeR3CmdAdminProcessIdentify(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm);
            break;
        case NVME_CMD_ADM_OPC_ABORT:
            rc = nvmeR3CmdAdminProcessAbort(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm);
            break;
        case NVME_CMD_ADM_OPC_SET_FEAT:
            rc = nvmeR3CmdAdminProcessSetFeatures(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm);
            break;
        case NVME_CMD_ADM_OPC_GET_FEAT:
            rc = nvmeR3CmdAdminProcessGetFeatures(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm);
            break;
        case NVME_CMD_ADM_OPC_ASYNC_EVT_REQ:
            rc = nvmeR3CmdAdminProcessAsyncEvtReq(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm);
            break;
        case NVME_CMD_ADM_OPC_NS_MGMT:
        case NVME_CMD_ADM_OPC_FW_COMMIT:
        case NVME_CMD_ADM_OPC_FW_IMG_DWNLD:
        case NVME_CMD_ADM_OPC_NS_ATTACHMENT:
        default:
            LogRel(("NVMe#%u: Guest queued invalid command %#x\n", pDevIns->iInstance,
                    pCmdAdm->u.Field.Hdr.u8Opc));
            rc = nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdAdm->u.Field.Hdr.u16Cid,
                                             NVME_CQ_ENTRY_SCT_CMD_GENERIC, NVME_CQ_ENTRY_SC_GEN_INV_OPC,
                                             0, false /* fMore */, true /* fDnr */);
    }

    return rc;
}


/**
 * Allocates a new I/O request which has enough memory for the size of the request.
 *
 * @returns Pointer to the NVMe I/O request on success or NULL if out of memory.
 * @param   pNamespace The namespace.
 * @param   u16Cid     The command identifier.
 * @param   pQueueSubm The associated I/O submission queue.
 * @param   Prp1       The first PRP entry found in the command.
 * @param   Prp2       The second PRP entry found in the command.
 * @param   cbPrp      SIze of the guest buffer in bytes.
 */
static PNVMEIOREQ nvmeR3IoReqAlloc(PNVMENAMESPACE pNamespace,
                                   uint16_t u16Cid, PNVMEQUEUESUBM pQueueSubm,
                                   NVMEPRP Prp1, NVMEPRP Prp2, size_t cbPrp)
{
    PNVMEIOREQ pIoReq = NULL;
    PDMMEDIAEXIOREQ hIoReq = NULL;

    int rc = pNamespace->pDrvMediaEx->pfnIoReqAlloc(pNamespace->pDrvMediaEx, &hIoReq, (void **)&pIoReq,
                                                    NVME_IOREQID_FROM_QID_AND_CID(pQueueSubm->Hdr.u16Id, u16Cid),
                                                    PDMIMEDIAEX_F_SUSPEND_ON_RECOVERABLE_ERR);
    if (RT_SUCCESS(rc))
    {
        pIoReq->hIoReq        = hIoReq;
        pIoReq->pNamespace    = pNamespace;
        pIoReq->u16Cid        = u16Cid;
        pIoReq->pQueueSubm    = pQueueSubm;
        pIoReq->Prp1          = Prp1;
        pIoReq->Prp2          = Prp2;
        pIoReq->cbPrp         = cbPrp;
        pIoReq->fMapped       = false;
    }
    else
        LogFlowFunc(("Failed to allocate I/O request with %Rrc\n", rc));

    return pIoReq;
}

/**
 * Free a I/O request.
 *
 * @param   pNamespace The namespace the request was for.
 * @param   pIoReq     The I/O request to free.
 */
DECLINLINE(void) nvmeR3IoReqFree(PNVMENAMESPACE pNamespace, PNVMEIOREQ pIoReq)
{
    int rc = pNamespace->pDrvMediaEx->pfnIoReqFree(pNamespace->pDrvMediaEx, pIoReq->hIoReq);
    AssertRC(rc);
}

/**
 * Completes the given I/O request and freeing all associated resources and notifying
 * the guest if possible.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pNamespace  The namespace.
 * @param   pIoReq      The completed I/O request.
 * @param   rcReq       The status code of the completed I/O request.
 */
static void nvmeR3IoReqComplete(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                PNVMENAMESPACE pNamespace, PNVMEIOREQ pIoReq, int rcReq)
{
    PNVMEQUEUESUBM pQueueSubm = pIoReq->pQueueSubm;
    bool fXchg = false;
    uint16_t u16Cid = pIoReq->u16Cid;
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pNamespace=%#p pIoReq=%#p rcReq=%Rrc\n", pNamespace, pIoReq, rcReq));

    pNamespace->Led.Asserted.s.fReading = pNamespace->Led.Actual.s.fReading = 0;
    pNamespace->Led.Asserted.s.fWriting = pNamespace->Led.Actual.s.fWriting = 0;

    if (pIoReq->fMapped)
        PDMDevHlpPhysReleasePageMappingLock(pDevIns, &pIoReq->PgLck);

    nvmeR3IoReqFree(pNamespace, pIoReq);

    if (rcReq != VERR_PDM_MEDIAEX_IOREQ_CANCELED)
    {
        uint32_t cActivities = ASMAtomicDecU32(&pThis->cActivities);
        ASMAtomicDecU32(&pQueueSubm->cReqsActive);

        /* Deallocate queue if it was deferred previously. */
        if (   ASMAtomicReadU32(&pQueueSubm->cReqsActive) == 0
            && ASMAtomicUoReadU32((volatile uint32_t *)&pQueueSubm->Hdr.enmState) == NVMEQUEUESTATE_DEALLOCATING)
        {
            /* The original submission queue for the deallocation request must be the admin queue. */
            PNVMEQUEUESUBM pAdmQueueSubm = &pThis->aQueuesSubm[NVME_ADM_QUEUE_ID];
            nvmeR3QueueSubmDeallocateDeferred(pDevIns, pThis, pThisCC, pAdmQueueSubm, pQueueSubm);
        }

        if (RT_SUCCESS(rcReq))
            rc = nvmeR3CmdCompleteWithSuccess(pDevIns, pThis, pThisCC, pQueueSubm, u16Cid, 0);
        else if (   rcReq == VERR_PDM_MEDIAEX_IOBUF_OVERFLOW
                 || rcReq == VERR_PDM_MEDIAEX_IOBUF_UNDERRUN)
            rc = nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, u16Cid,
                                             NVME_CQ_ENTRY_SCT_CMD_GENERIC, NVME_CQ_ENTRY_SC_GEN_INV_PRP_OFFSET,
                                             0, false /* fMore */, true /* fDnr */);
        else
            rc = nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, u16Cid,
                                             NVME_CQ_ENTRY_SCT_CMD_GENERIC, NVME_CQ_ENTRY_SC_GEN_DATA_XFER_ERR,
                                             0, false /* fMore */, true /* fDnr */);

        if (RT_FAILURE(rc))
            nvmeStateSetFatalError(pThis);

        /* If the controller processes a shutdown mark it as complete when this was the last request. */
        if (   !cActivities
            && pThis->enmState == NVMESTATE_SHUTDOWN_PROCESSING)
            ASMAtomicCmpXchgSize(&pThis->enmState, NVMESTATE_SHUTDOWN_COMPLETE, NVMESTATE_SHUTDOWN_PROCESSING, fXchg);
    }

    if (pThis->cActivities == 0 && pThisCC->fSignalIdle)
        PDMDevHlpAsyncNotificationCompleted(pDevIns);
}

/**
 * Submits a given a I/O request for execution.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pNamespace  The namespace the I/O request is for.
 * @param   pIoReq      The I/O request to submit.
 * @param   enmType     Request type.
 * @param   offStart    Start offset for the request if applicable.
 * @param   cbReq       Number of bytes to transfer if applicable.
 */
static void nvmeR3IoReqSubmit(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                              PNVMENAMESPACE pNamespace, PNVMEIOREQ pIoReq,
                              PDMMEDIAEXIOREQTYPE enmType, uint64_t offStart,
                              size_t cbReq)
{
    int rc;

    ASMAtomicIncU32(&pIoReq->pQueueSubm->cReqsActive);
    ASMAtomicIncU32(&pThis->cActivities);

    if (enmType == PDMMEDIAEXIOREQTYPE_FLUSH)
        rc = pNamespace->pDrvMediaEx->pfnIoReqFlush(pNamespace->pDrvMediaEx, pIoReq->hIoReq);
    else if (enmType == PDMMEDIAEXIOREQTYPE_READ)
    {
        pNamespace->Led.Asserted.s.fReading = pNamespace->Led.Actual.s.fReading = 1;
        rc = pNamespace->pDrvMediaEx->pfnIoReqRead(pNamespace->pDrvMediaEx, pIoReq->hIoReq,
                                                   offStart, cbReq);
    }
    else
    {
        pNamespace->Led.Asserted.s.fWriting = pNamespace->Led.Actual.s.fWriting = 1;
        rc = pNamespace->pDrvMediaEx->pfnIoReqWrite(pNamespace->pDrvMediaEx, pIoReq->hIoReq,
                                                    offStart, cbReq);
    }

    if (rc == VINF_SUCCESS)
        nvmeR3IoReqComplete(pDevIns, pThis, pThisCC, pNamespace, pIoReq, VINF_SUCCESS);
    else if (rc != VINF_PDM_MEDIAEX_IOREQ_IN_PROGRESS)
        nvmeR3IoReqComplete(pDevIns, pThis, pThisCC, pNamespace, pIoReq, rc);
}

/**
 * Process a NVM comand.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pWrkThrd    The work thread processing the command.
 * @param   pQueueSubm  The submission queue responsible for this command.
 * @param   pCmdNvm     The NVM command to process.
 */
static int nvmeR3CmdNvmProcess(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                               PNVMEWRKTHRD pWrkThrd, PNVMEQUEUESUBM pQueueSubm, PNVMECMDNVM pCmdNvm)
{
    RT_NOREF(pWrkThrd);
    NVMENSID uNsId = pCmdNvm->u.Field.uNsId;
    PNVMENAMESPACE pNamespace = NULL;
    PDMMEDIAEXIOREQTYPE enmType = PDMMEDIAEXIOREQTYPE_INVALID;
    PNVMEIOREQ pIoReq = NULL;
    size_t cbReq = 0;
    uint64_t u64OffsetStart = 0;
    int rc = VINF_SUCCESS;

    LogFlow(("Wrk#%u: Processing NVM command %#x\n", pWrkThrd->uId, pCmdNvm->u.Field.Hdr.u16Cid));

    nvmeR3CmdNvmDump(pWrkThrd, pCmdNvm);

    if (RT_UNLIKELY(!NVME_CMD_HDR_PSDT_IS_PRP(pCmdNvm->u.Field.Hdr.u2Psdt)))
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdNvm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_GENERIC, NVME_CQ_ENTRY_SC_GEN_INV_CMD_FIELD,
                                           0, false /* fMore */, true /* fDnr */);

    /* Check that the namespace is valid and has something attached. */
    if (   uNsId == 0
        || uNsId > pThis->cNamespaces)
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdNvm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_GEN_INV_NS_OR_FMT,
                                           0, false /* fMore */, true /* fDnr */);

    pNamespace = &pThisCC->paNamespaces[uNsId - 1];
    if (!pNamespace->cBlocks)
        return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdNvm->u.Field.Hdr.u16Cid,
                                           NVME_CQ_ENTRY_SCT_CMD_SPECIFIC, NVME_CQ_ENTRY_SC_GEN_INV_NS_OR_FMT,
                                           0, false /* fMore */, true /* fDnr */);

    switch (pCmdNvm->u.Field.Hdr.u8Opc)
    {
        case NVME_CMD_NVM_FLUSH:
            enmType = PDMMEDIAEXIOREQTYPE_FLUSH;
            break;
        case NVME_CMD_NVM_READ:
            enmType = PDMMEDIAEXIOREQTYPE_READ;
            cbReq = pNamespace->cbBlock * (pCmdNvm->u.Field.u.ReadWrite.u16Blocks + 1);
            u64OffsetStart = pNamespace->cbBlock * pCmdNvm->u.Field.u.ReadWrite.u64LbaStart;
            break;
        case NVME_CMD_NVM_WRITE:
            enmType = PDMMEDIAEXIOREQTYPE_WRITE;
            cbReq = pNamespace->cbBlock * (pCmdNvm->u.Field.u.ReadWrite.u16Blocks + 1);
            u64OffsetStart = pNamespace->cbBlock * pCmdNvm->u.Field.u.ReadWrite.u64LbaStart;
            break;
        default:
            LogRel(("NVMe#%u: Guest queued invalid command %#x\n", pDevIns->iInstance,
                    pCmdNvm->u.Field.Hdr.u8Opc));
            return nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdNvm->u.Field.Hdr.u16Cid,
                                               NVME_CQ_ENTRY_SCT_CMD_GENERIC, NVME_CQ_ENTRY_SC_GEN_INV_OPC,
                                               0, false /* fMore */, true /* fDnr */);
    }

    pIoReq = nvmeR3IoReqAlloc(pNamespace, pCmdNvm->u.Field.Hdr.u16Cid, pQueueSubm,
                              pCmdNvm->u.Field.DataPtr.Prp.Prp1, pCmdNvm->u.Field.DataPtr.Prp.Prp2,
                              cbReq);
    if (pIoReq)
        nvmeR3IoReqSubmit(pDevIns, pThis, pThisCC, pNamespace, pIoReq, enmType, u64OffsetStart, cbReq);
    else
        rc = nvmeR3CmdCompleteWithStatus(pDevIns, pThis, pThisCC, pQueueSubm, pCmdNvm->u.Field.Hdr.u16Cid,
                                         NVME_CQ_ENTRY_SCT_CMD_GENERIC, NVME_CQ_ENTRY_SC_GEN_DATA_XFER_ERR,
                                         0, false /* fMore */, true /* fDnr */);

    return rc;
}

/**
 * The NVMe asynchronous worker thread.
 *
 * @returns VBox status code.
 * @param   pDevIns     The NVMe device instance.
 * @param   pThread     The worker thread.
 */
static DECLCALLBACK(int) nvmeR3WorkerLoop(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    RT_NOREF(pDevIns);
    PNVMEWRKTHRD    pWrkThrd = (PNVMEWRKTHRD)pThread->pvUser;
    PNVME           pThis    = PDMDEVINS_2_DATA(pDevIns, PNVME);
    PNVMECC         pThisCC  = PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC);
    int             rc       = VINF_SUCCESS;

    LogFlow(("Wrk#%u: Entering work loop\n", pWrkThrd->uId));
    if (pThread->enmState == PDMTHREADSTATE_INITIALIZING)
        return VINF_SUCCESS;

    ASMAtomicIncU32(&pThis->cActivities);

    while (pThread->enmState == PDMTHREADSTATE_RUNNING)
    {
        ASMAtomicWriteBool(&pWrkThrd->fWrkThrdSleeping, true);
        uint32_t cActivities = ASMAtomicDecU32(&pThis->cActivities);
        if (!cActivities)
        {
            if (RT_UNLIKELY(pThis->enmState == NVMESTATE_SHUTDOWN_PROCESSING))
            {
                bool fXchg = false;
                ASMAtomicCmpXchgSize(&pThis->enmState, NVMESTATE_SHUTDOWN_COMPLETE, NVMESTATE_SHUTDOWN_PROCESSING, fXchg);
            }

            /* Signal idle state (caused by a suspend or power off event) if requested. */
            if (RT_UNLIKELY(pThisCC->fSignalIdle))
                PDMDevHlpAsyncNotificationCompleted(pDevIns);
        }

        rc = PDMDevHlpSUPSemEventWaitNoResume(pDevIns, pWrkThrd->hEvtProcess, RT_INDEFINITE_WAIT);
        AssertLogRelMsgReturn(RT_SUCCESS(rc) || rc == VERR_INTERRUPTED, ("%Rrc\n", rc), rc);
        ASMAtomicIncU32(&pThis->cActivities);

        if (RT_UNLIKELY(pThread->enmState != PDMTHREADSTATE_RUNNING))
            break;
        LogFlowFunc(("Woken up with rc=%Rrc\n", rc));
        ASMAtomicWriteBool(&pWrkThrd->fWrkThrdSleeping, false);

        rc = RTReqQueueProcess(pWrkThrd->hReqQueue, 0);
        Assert(rc == VERR_TIMEOUT || RT_SUCCESS(rc));

        /* Don't process anything if the controller is not ready. */
        if (pThis->enmState != NVMESTATE_READY)
            continue;

        /* Walk all assigned submission queues and check for new work. */
        PNVMEQUEUESUBMR3 pSubmQueueR3;
        RTListForEach(&pWrkThrd->ListSubmQueuesAssgnd, pSubmQueueR3, NVMEQUEUESUBMR3, NdLstWrkThrdAssgnd)
        {
            uintptr_t idxSubmQueue = pSubmQueueR3 - &pThisCC->aQueuesSubm[0];
            Assert(idxSubmQueue < RT_ELEMENTS(pThis->aQueuesSubm));
            PNVMEQUEUESUBM pSubmQueue = &pThis->aQueuesSubm[idxSubmQueue];

            RTGCPHYS GCPhysCmd = nvmeQueueConsumerGetNextEntryAddress(&pSubmQueue->Hdr);

            while (   GCPhysCmd != NVME_QUEUE_IS_EMPTY_RTGCPHYS
                   && RT_LIKELY(ASMAtomicUoReadU32((volatile uint32_t *)&pSubmQueue->Hdr.enmState) != NVMEQUEUESTATE_DEALLOCATED))
            {
                uint16_t u16Cid;
                LogFlow(("Wrk#%u: New command found on submission queue %u\n", pWrkThrd->uId, pSubmQueue->Hdr.u16Id));

                /* Check whether the associated completion queue is overloaded preventing new commands from being processed. */
                PNVMEQUEUECOMP pCompQueue = &pThis->aQueuesComp[pSubmQueue->u16CompletionQueueId];
                if (RT_UNLIKELY(pCompQueue->fOverloaded))
                {
                    LogRelMax(10, ("Wrk#%u: Associated completion queue is overloaded, stopping command processing\n", pWrkThrd->uId));
                    break;
                }

                /* Read command from guest memory and process. */
                if (pSubmQueue->Hdr.u16Id == NVME_ADM_QUEUE_ID)
                {
                    NVMECMDADM CmdAdm;
                    nvmeR3PhysRead(pDevIns, pThis, pThisCC, GCPhysCmd, &CmdAdm, sizeof(CmdAdm), NVME_CMBSZ_SQS_BIT_IDX);

                    u16Cid = CmdAdm.u.Field.Hdr.u16Cid;
                    rc = nvmeR3CmdAdminProcess(pDevIns, pThis, pThisCC, pWrkThrd, pSubmQueue, &CmdAdm);
                }
                else
                {
                    NVMECMDNVM CmdNvm;
                    nvmeR3PhysRead(pDevIns, pThis, pThisCC, GCPhysCmd, &CmdNvm, sizeof(CmdNvm), NVME_CMBSZ_SQS_BIT_IDX);

                    u16Cid = CmdNvm.u.Field.Hdr.u16Cid;
                    rc = nvmeR3CmdNvmProcess(pDevIns, pThis, pThisCC, pWrkThrd, pSubmQueue, &CmdNvm);
                }

                if (RT_FAILURE(rc))
                {
                    LogRel(("NVMe#%uWrk#%u: Processing %s command %#x returned with error: %Rrc\n",
                            pDevIns->iInstance, pWrkThrd->uId,
                            pSubmQueue->Hdr.u16Id == NVME_ADM_QUEUE_ID ? "admin" : "NVM",
                            u16Cid, rc));
                    nvmeStateSetFatalError(pThis);
                    break;
                }

                nvmeQueueConsumerAdvance(&pSubmQueue->Hdr);
                GCPhysCmd = nvmeQueueConsumerGetNextEntryAddress(&pSubmQueue->Hdr);
            }

            /** @todo Destroy if the submission queue is deallocating and we processed all requests. */

            if (RT_UNLIKELY(pThis->enmState != NVMESTATE_READY))
                break;
        }
    }

    ASMAtomicDecU32(&pThis->cActivities);

    return rc;
}

/**
 * Kicks the given worker thread into action.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pWrkThrd    The worker thread.
 */
DECLINLINE(int) nvmeR3WrkThrdKick(PPDMDEVINS pDevIns, PNVMEWRKTHRD pWrkThrd)
{
    return PDMDevHlpSUPSemEventSignal(pDevIns, pWrkThrd->hEvtProcess);
}

/**
 * Unblock the worker thread so it can respond to a state change.
 *
 * @returns VBox status code.
 * @param   pDevIns     The NVMe device instance.
 * @param   pThread     The worker thread.
 */
static DECLCALLBACK(int) nvmeR3WorkerWakeUp(PPDMDEVINS pDevIns, PPDMTHREAD pThread)
{
    PNVMEWRKTHRD pWrkThrd = (PNVMEWRKTHRD)pThread->pvUser;
    return nvmeR3WrkThrdKick(pDevIns, pWrkThrd);
}

/**
 * Destroys the given worker thread.
 *
 * @param   pDevIns     The device instance.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pWrkThrd    The work thread to destroy.
 */
static void nvmeR3WrkThrdDestroy(PPDMDEVINS pDevIns, PNVMECC pThisCC, PNVMEWRKTHRD pWrkThrd)
{
    LogFlow(("Wrk#%u: Destroying worker\n", pWrkThrd->uId));

    int rc = RTCritSectEnter(&pThisCC->CritSectWrkThrds);
    AssertRC(rc);

    RTListNodeRemove(&pWrkThrd->NodeWrkThrdList);
    Assert(pThisCC->cWrkThrdsCur > 0);
    pThisCC->cWrkThrdsCur--;

    RTCritSectLeave(&pThisCC->CritSectWrkThrds);

    int rcThread = VINF_SUCCESS;
    rc = PDMDevHlpThreadDestroy(pDevIns, pWrkThrd->pThrd, &rcThread);
    AssertRC(rc); AssertRC(rcThread);

    RTReqQueueDestroy(pWrkThrd->hReqQueue);
    PDMDevHlpMMHeapFree(pDevIns, pWrkThrd);
}

/**
 * Releases a reference to the given worker thread destroying it when it reaches 0.
 *
 * @returns New reference count.
 * @param   pWrkThrd    The work thread to release.
 */
static uint32_t nvmeR3WrkThrdRelease(PNVMEWRKTHRD pWrkThrd)
{
#if 0
    PNVME pThis = pWrkThrd->pNvmeR3;
#endif

    LogFlow(("Wrk#%u: Releasing one reference of worker\n", pWrkThrd->uId));

    uint32_t cRefs = ASMAtomicDecU32(&pWrkThrd->cSubmQueues);
#if 0
    if (!cRefs)
        nvmeR3WrkThrdDestroy(pThis, pWrkThrd);
#endif

    return cRefs;
}

/**
 * Worker for assigning a submission queue to the worker thread this method is
 * executed on.
 *
 * @param   pWrkThrd    The worker thread.
 * @param   pQueueR3    The submission queue to assign (ring-3 manifestation).
 */
static DECLCALLBACK(void) nvmeR3WrkThrdAssignWorker(PNVMEWRKTHRD pWrkThrd, PNVMEQUEUESUBMR3 pQueueR3)
{
    ASMAtomicIncU32(&pWrkThrd->cSubmQueues);
    RTListAppend(&pWrkThrd->ListSubmQueuesAssgnd, &pQueueR3->NdLstWrkThrdAssgnd);
}

/**
 * Worker for removing a submission queue from the worker thread this method is
 * executed on.
 *
 * @param   pWrkThrd    The worker thread.
 * @param   pQueue      The submission queue to assign, shared bits.
 * @param   pQueueR3    The submission queue to assign, ring-3 bits.
 */
static DECLCALLBACK(void) nvmeR3WrkThrdRemoveWorker(PNVMEWRKTHRD pWrkThrd, PNVMEQUEUESUBM pQueue, PNVMEQUEUESUBMR3 pQueueR3)
{
    LogFlowFunc(("\n"));
    Assert(pQueueR3->pWrkThrdR3 == pWrkThrd);

    RTListNodeRemove(&pQueueR3->NdLstWrkThrdAssgnd);
    pQueueR3->pWrkThrdR3 = NULL;
    pQueue->hEvtProcess = NIL_SUPSEMEVENT;
    nvmeR3WrkThrdRelease(pWrkThrd);
}

/**
 * Assigns a given submission queue to the given worker thread.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pWrkThrd    The worker thread.
 * @param   pQueue      The submission queue to assign.
 * @param   pQueueR3    The submission queue to assign, ring-3 bits.
 */
static int nvmeR3WrkThrdAssignSubmQueue(PPDMDEVINS pDevIns, PNVMEWRKTHRD pWrkThrd, PNVMEQUEUESUBM pQueue, PNVMEQUEUESUBMR3 pQueueR3)
{
    Assert(pQueueR3->pWrkThrdR3 == NULL);
    Assert(pQueue->hEvtProcess == NIL_SUPSEMEVENT);

    pQueueR3->pWrkThrdR3 = pWrkThrd;
    pQueue->hEvtProcess  = pWrkThrd->hEvtProcess;

    int rc = RTReqQueueCallEx(pWrkThrd->hReqQueue, NULL /*phReq*/, 0 /*cMillies*/, RTREQFLAGS_VOID | RTREQFLAGS_NO_WAIT,
                              (PFNRT)nvmeR3WrkThrdAssignWorker, 2, pWrkThrd, pQueueR3);
    if (RT_SUCCESS(rc))
        rc = nvmeR3WrkThrdKick(pDevIns, pWrkThrd);

    return rc;
}

/**
 * Removes a given submission queue from the assigned worker thread.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pQueue      The submission queue to assign, shared bits.
 * @param   pQueueR3    The submission queue to assign, ring-3 bits.
 */
static int nvmeR3WrkThrdRemoveSubmissionQueue(PPDMDEVINS pDevIns, PNVMEQUEUESUBM pQueue, PNVMEQUEUESUBMR3 pQueueR3)
{
    PNVMEWRKTHRD pWrkThrd = pQueueR3->pWrkThrdR3;

    AssertPtr(pWrkThrd);
    Assert(pQueue->hEvtProcess != NIL_SUPSEMEVENT);

    PRTREQ pReq = NULL;
    int rc = RTReqQueueCallEx(pWrkThrd->hReqQueue, &pReq, 0, RTREQFLAGS_VOID, (PFNRT)nvmeR3WrkThrdRemoveWorker, 3,
                              pWrkThrd, pQueue, pQueueR3);
    if (rc == VERR_TIMEOUT)
    {
        rc = nvmeR3WrkThrdKick(pDevIns, pWrkThrd);
        if (RT_SUCCESS(rc))
            rc = RTReqWait(pReq, 60 * RT_MS_1SEC);
    }

    if (RT_SUCCESS(rc))
    {
        RTReqRelease(pReq);
        nvmeR3WrkThrdRelease(pWrkThrd);
    }

    return rc;
}

/**
 * Creates a new worker thread and links it into the list of workers on success.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   ppWrkThrd   Where to store the pointer to the new work thread on success.
 */
static int nvmeR3WrkThrdCreate(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC, PNVMEWRKTHRD *ppWrkThrd)
{
    int rc;
    PNVMEWRKTHRD pWrkThrd = (PNVMEWRKTHRD)PDMDevHlpMMHeapAllocZ(pDevIns, sizeof(NVMEWRKTHRD));
    if (pWrkThrd)
    {
        pWrkThrd->pNvmeR3          = pThis;
        pWrkThrd->fWrkThrdSleeping = true;
        pWrkThrd->cSubmQueues      = 0;
        RTListInit(&pWrkThrd->ListSubmQueuesAssgnd);

        rc = RTReqQueueCreate(&pWrkThrd->hReqQueue);
        if (RT_SUCCESS(rc))
        {
            rc = PDMDevHlpSUPSemEventCreate(pDevIns, &pWrkThrd->hEvtProcess);
            if (RT_SUCCESS(rc))
            {
                char szThrdId[10];
                RT_ZERO(szThrdId);

                RTStrPrintf(szThrdId, sizeof(szThrdId), "NVMe#%u", pThisCC->cWrkThrdsCur); /* (may produce duplicates) */
                rc = PDMDevHlpThreadCreate(pDevIns, &pWrkThrd->pThrd, pWrkThrd, nvmeR3WorkerLoop, nvmeR3WorkerWakeUp,
                                           0, RTTHREADTYPE_IO, szThrdId);
                if (RT_SUCCESS(rc))
                {
                    int rc2 = RTCritSectEnter(&pThisCC->CritSectWrkThrds);
                    AssertRC(rc2);
                    RTListAppend(&pThisCC->LstWrkThrds, &pWrkThrd->NodeWrkThrdList);
                    pWrkThrd->uId = pThisCC->cWrkThrdsCur;
                    pThisCC->cWrkThrdsCur++;
                    RTCritSectLeave(&pThisCC->CritSectWrkThrds);

                    /*
                     * Resume the thread immediately if the VM is running so it can start
                     * processing requests.
                     */
                    if (PDMDevHlpVMState(pDevIns) == VMSTATE_RUNNING)
                    {
                        rc = PDMDevHlpThreadResume(pDevIns, pWrkThrd->pThrd);
                        AssertRC(rc);
                    }

                    LogFlow(("Wrk#%u: Created new worker\n", pWrkThrd->uId));
                    *ppWrkThrd = pWrkThrd;
                    return VINF_SUCCESS;
                }

                int rc2 = PDMDevHlpSUPSemEventClose(pDevIns, pWrkThrd->hEvtProcess);
                Assert(rc2 == VINF_OBJECT_DESTROYED); NOREF(rc2);
            }

            RTReqQueueDestroy(pWrkThrd->hReqQueue);
        }

        PDMDevHlpMMHeapFree(pDevIns, pWrkThrd);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

/**
 * Finds the best available worker thread (the one with the least assigned submission queues).
 *
 * @returns Pointer to the most suitable worker thread.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 */
static PNVMEWRKTHRD nvmeR3WrkThrdFindAvailable(PNVMECC pThisCC)
{
    PNVMEWRKTHRD pWrkThrd = RTListGetFirst(&pThisCC->LstWrkThrds, NVMEWRKTHRD, NodeWrkThrdList);

    int rc = RTCritSectEnter(&pThisCC->CritSectWrkThrds);
    AssertRC(rc);

    Assert(!RTListIsEmpty(&pThisCC->LstWrkThrds));
    PNVMEWRKTHRD pIt;
    RTListForEach(&pThisCC->LstWrkThrds, pIt, NVMEWRKTHRD, NodeWrkThrdList)
    {
        if (pIt->cSubmQueues < pWrkThrd->cSubmQueues)
            pWrkThrd = pIt;
    }

    RTCritSectLeave(&pThisCC->CritSectWrkThrds);

    AssertPtr(pWrkThrd);
    return pWrkThrd;
}

/**
 * Assigns the given submission queue to a worker thread, creating a new one
 * if the maximum amount of workers is not yet reached.
 *
 * @returns VBox status code.
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 * @param   pQueue      The submission queue to assign, shared bits.
 * @param   pQueueR3    The submission queue to assign, ring-3 bits.
 */
static int nvmeR3SubmQueueAssignToWorker(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC,
                                         PNVMEQUEUESUBM pQueue, PNVMEQUEUESUBMR3 pQueueR3)
{
    PNVMEWRKTHRD pWrkThrd = NULL;

    if (pThisCC->cWrkThrdsCur < pThis->cWrkThrdsMax)
    {
        /* Try to create a new worker. */
        int rc = nvmeR3WrkThrdCreate(pDevIns, pThis, pThisCC, &pWrkThrd);
        if (RT_FAILURE(rc))
        {
            LogRel(("NVME%u: Failed to create a new worker thread with %Rrc, continuing with what is available\n",
                    pDevIns->iInstance, rc));
            pWrkThrd = nvmeR3WrkThrdFindAvailable(pThisCC);
        }
    }
    else
        pWrkThrd = nvmeR3WrkThrdFindAvailable(pThisCC);

    return nvmeR3WrkThrdAssignSubmQueue(pDevIns, pWrkThrd, pQueue, pQueueR3);
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCopyFromBuf}
 */
static DECLCALLBACK(int) nvmeR3IoReqCopyFromBuf(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                void *pvIoReqAlloc, uint32_t offDst, PRTSGBUF pSgBuf,
                                                size_t cbCopy)
{
    RT_NOREF(hIoReq);
    PNVMENAMESPACE  pNvmeNs = RT_FROM_MEMBER(pInterface, NVMENAMESPACE, IPortEx);
    PPDMDEVINS      pDevIns = pNvmeNs->pDevIns;
    PNVME           pThis   = PDMDEVINS_2_DATA(pDevIns, PNVME);
    PNVMECC         pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC);
    PNVMEIOREQ      pIoReq  = (PNVMEIOREQ)pvIoReqAlloc;
    bool fPrpValid = nvmeR3CopySgBufToPrps(pDevIns, pThis, pThisCC, pIoReq->Prp1, pIoReq->Prp2, pIoReq->cbPrp,
                                           pSgBuf, cbCopy, offDst, true /* fListsAllowed */);
    return fPrpValid ? VINF_SUCCESS : VERR_PDM_MEDIAEX_IOBUF_OVERFLOW;
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCopyToBuf}
 */
static DECLCALLBACK(int) nvmeR3IoReqCopyToBuf(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                              void *pvIoReqAlloc, uint32_t offSrc, PRTSGBUF pSgBuf,
                                              size_t cbCopy)
{
    RT_NOREF(hIoReq);
    PNVMENAMESPACE  pNvmeNs = RT_FROM_MEMBER(pInterface, NVMENAMESPACE, IPortEx);
    PPDMDEVINS      pDevIns = pNvmeNs->pDevIns;
    PNVME           pThis   = PDMDEVINS_2_DATA(pDevIns, PNVME);
    PNVMECC         pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC);
    PNVMEIOREQ      pIoReq  = (PNVMEIOREQ)pvIoReqAlloc;
    bool fPrpValid = nvmeR3CopySgBufFromPrps(pDevIns, pThis, pThisCC, pIoReq->Prp1, pIoReq->Prp2, pIoReq->cbPrp,
                                             pSgBuf, cbCopy, offSrc, true /* fListsAllowed */);
    return fPrpValid ? VINF_SUCCESS : VERR_PDM_MEDIAEX_IOBUF_UNDERRUN;
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqQueryBuf}
 */
static DECLCALLBACK(int) nvmeR3IoReqQueryBuf(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                             void *pvIoReqAlloc, void **ppvBuf, size_t *pcbBuf)
{
    RT_NOREF(hIoReq);
    int             rc      = VERR_NOT_SUPPORTED;
    PNVMENAMESPACE  pNvmeNs = RT_FROM_MEMBER(pInterface, NVMENAMESPACE, IPortEx);
    PPDMDEVINS      pDevIns = pNvmeNs->pDevIns;
    PNVME           pThis   = PDMDEVINS_2_DATA(pDevIns, PNVME);
    PNVMEIOREQ      pIoReq  = (PNVMEIOREQ)pvIoReqAlloc;
    size_t          cbPrp   = NVME_PRP_GET_SIZE(pIoReq->Prp1, pThis->uMpsSet);

    /*
     * The first PRP should start on a page boundary and cover the whole buffer
     * (maximum single page).
     */
    if (   cbPrp >= pIoReq->cbPrp
        && NVME_PRP_IS_PAGE_ALIGNED(pIoReq->Prp1, pThis->uMpsSet))
    {
        RTGCPHYS GCPhysBase = NVME_PRP_GET_PAGE_ADDR(pIoReq->Prp1, pThis->uMpsSet);

        rc = PDMDevHlpPhysGCPhys2CCPtr(pDevIns, GCPhysBase, 0, ppvBuf, &pIoReq->PgLck);
        if (RT_SUCCESS(rc))
        {
            pIoReq->fMapped = true;
            *pcbBuf = pIoReq->cbPrp;
            Assert(*pcbBuf <= PDMDevHlpPhysGetPageSize(pDevIns));
        }
        else
            rc = VERR_NOT_SUPPORTED;
    }

    return rc;
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqCompleteNotify}
 */
static DECLCALLBACK(int) nvmeR3IoReqCompleteNotify(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                   void *pvIoReqAlloc, int rcReq)
{
    RT_NOREF(hIoReq);
    PNVMENAMESPACE  pNvmeNs = RT_FROM_MEMBER(pInterface, NVMENAMESPACE, IPortEx);
    PPDMDEVINS      pDevIns = pNvmeNs->pDevIns;
    PNVME           pThis   = PDMDEVINS_2_DATA(pDevIns, PNVME);
    PNVMECC         pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC);
    nvmeR3IoReqComplete(pDevIns, pThis, pThisCC, pNvmeNs, (PNVMEIOREQ)pvIoReqAlloc, rcReq);
    return VINF_SUCCESS;
}

/**
 * @interface_method_impl{PDMIMEDIAEXPORT,pfnIoReqStateChanged}
 */
static DECLCALLBACK(void) nvmeR3IoReqStateChanged(PPDMIMEDIAEXPORT pInterface, PDMMEDIAEXIOREQ hIoReq,
                                                  void *pvIoReqAlloc, PDMMEDIAEXIOREQSTATE enmState)
{
    RT_NOREF(hIoReq);
    PNVMENAMESPACE  pNvmeNs = RT_FROM_MEMBER(pInterface, NVMENAMESPACE, IPortEx);
    PPDMDEVINS      pDevIns = pNvmeNs->pDevIns;
    PNVME           pThis   = PDMDEVINS_2_DATA(pDevIns, PNVME);
    PNVMECC         pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC);
    PNVMEIOREQ      pIoReq  = (PNVMEIOREQ)pvIoReqAlloc;

    switch (enmState)
    {
        case PDMMEDIAEXIOREQSTATE_SUSPENDED:
        {
            /* Make sure the request is not accounted for so the VM can suspend successfully. */
            ASMAtomicDecU32(&pIoReq->pQueueSubm->cReqsActive);

            uint32_t cActivities = ASMAtomicDecU32(&pThis->cActivities);
            if (!cActivities && pThisCC->fSignalIdle)
                PDMDevHlpAsyncNotificationCompleted(pDevIns);
            break;
        }
        case PDMMEDIAEXIOREQSTATE_ACTIVE:
            /* Make sure the request is accounted for so the VM suspends only when the request is complete. */
            ASMAtomicIncU32(&pIoReq->pQueueSubm->cReqsActive);
            ASMAtomicIncU32(&pThis->cActivities);
            break;
        default:
            AssertMsgFailed(("Invalid request state given %u\n", enmState));
    }
}

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) nvmeR3NamespaceQueryInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PNVMENAMESPACE pNvmeNs = RT_FROM_MEMBER(pInterface, NVMENAMESPACE, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pNvmeNs->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAPORT, &pNvmeNs->IPort);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIMEDIAEXPORT, &pNvmeNs->IPortEx);
    return NULL;
}

/**
 * @interface_method_impl{PDMIMEDIAPORT,pfnQueryDeviceLocation}
 */
static DECLCALLBACK(int) nvmeR3NamespaceQueryDeviceLocation(PPDMIMEDIAPORT pInterface, const char **ppcszController,
                                                            uint32_t *piInstance, uint32_t *piLUN)
{
    PNVMENAMESPACE  pNvmeNs = RT_FROM_MEMBER(pInterface, NVMENAMESPACE, IPort);
    PPDMDEVINS      pDevIns = pNvmeNs->pDevIns;

    AssertPtrReturn(ppcszController, VERR_INVALID_POINTER);
    AssertPtrReturn(piInstance, VERR_INVALID_POINTER);
    AssertPtrReturn(piLUN, VERR_INVALID_POINTER);

    *ppcszController = pDevIns->pReg->szName;
    *piInstance = pDevIns->iInstance;
    *piLUN = pNvmeNs->u32Id;

    return VINF_SUCCESS;
}

/**
 * Configure the given name space.
 *
 * Used by nvmeR3Construct.
 *
 * @returns VBox status code
 * @param   pDevIns     The device instance.
 * @param   pNvmeNs     The namespace to configure.
 * @param   fReAttach   Flag whether the underlying driver is re-attached
 *                      while the VM is running to enable additional checks.
 *                      false if the namespace is configured during construction.
 */
static int nvmeR3NamespaceConfigure(PPDMDEVINS pDevIns, PNVMENAMESPACE pNvmeNs, bool fReAttach)
{
    int          rc = VINF_SUCCESS;
    PDMMEDIATYPE enmType;

    Assert(pNvmeNs->pDevIns == pDevIns);

    /*
     * Query the block and blockbios interfaces.
     */
    pNvmeNs->pDrvMedia = PDMIBASE_QUERY_INTERFACE(pNvmeNs->pDrvBase, PDMIMEDIA);
    if (!pNvmeNs->pDrvMedia)
        return PDMDevHlpVMSetError(pDevIns, VERR_PDM_MISSING_INTERFACE, RT_SRC_POS,
                                   N_("NVMe configuration error: LUN#%u doesn't has a media interface!"),
                                   pNvmeNs->u32Id);

    /* Get the extended media interface. */
    pNvmeNs->pDrvMediaEx = PDMIBASE_QUERY_INTERFACE(pNvmeNs->pDrvBase, PDMIMEDIAEX);
    if (!pNvmeNs->pDrvMediaEx)
        return PDMDevHlpVMSetError(pDevIns, VERR_PDM_MISSING_INTERFACE, RT_SRC_POS,
                                   N_("NVMe configuration error: LUN#%u doesn't has a extended media interface!"),
                                   pNvmeNs->u32Id);

    rc = pNvmeNs->pDrvMediaEx->pfnIoReqAllocSizeSet(pNvmeNs->pDrvMediaEx, sizeof(NVMEIOREQ));
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("NVMe configuration error: LUN#%u: Failed to set I/O request size!"),
                                   pNvmeNs->u32Id);

    /*
     * Validate type.
     */
    enmType = pNvmeNs->pDrvMedia->pfnGetType(pNvmeNs->pDrvMedia);
    if (enmType != PDMMEDIATYPE_HARD_DISK)
        return PDMDevHlpVMSetError(pDevIns, VERR_PDM_UNSUPPORTED_BLOCK_TYPE, RT_SRC_POS,
                                   N_("NVMe configuration error: LUN#%u isn't a disk! enmType=%d"),
                                   pNvmeNs->u32Id, enmType);

    if (fReAttach)
    {
        size_t cbBlock = pNvmeNs->pDrvMedia->pfnGetSectorSize(pNvmeNs->pDrvMedia);
        uint64_t cBlocks = pNvmeNs->pDrvMedia->pfnGetSize(pNvmeNs->pDrvMedia) / RT_MAX(cbBlock, 1);

        AssertLogRelMsgReturn(   pNvmeNs->cbBlock == cbBlock
                              && pNvmeNs->cBlocks == cBlocks,
                              ("Block size and/or number of blocks differs from the previous attachment!\n"), VERR_INVALID_STATE);
    }
    else
    {
        pNvmeNs->cbBlock = pNvmeNs->pDrvMedia->pfnGetSectorSize(pNvmeNs->pDrvMedia);
        AssertLogRelMsgReturn(pNvmeNs->cbBlock > 0, ("Block size should not be 0!\n"), VERR_INVALID_STATE);
        pNvmeNs->cBlocks = pNvmeNs->pDrvMedia->pfnGetSize(pNvmeNs->pDrvMedia) / RT_MAX(pNvmeNs->cbBlock, 1);
    }

    LogRel(("NVMe#%uNs%u: disk, total number of blocks %Ld\n", pDevIns->iInstance, pNvmeNs->u32Id,  pNvmeNs->cBlocks));
#if 0 /** @todo */
    if (pNvmeNs->pDrvMedia->pfnDiscard)
        LogRel(("NVMe#%uNs%u: Enabled TRIM support\n", pDevIns->iInstance, pNvmeNs->u32Id));
#endif

    return rc;
}

/**
 * Performs a controller level reset as defined in chapter 7.3.2 of the spec
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 */
static void nvmeR3CtrlReset(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC)
{
    bool fRcXchg;

    ASMAtomicCmpXchgSize(&pThis->enmState, NVMESTATE_RESETTING, NVMESTATE_QUIESCING, fRcXchg);
    Assert(fRcXchg || pThis->enmState == NVMESTATE_INIT);

    pThisCC->cAsyncEvtReqsCur = 0;

    /*
     * Initialize the interrupt vector states.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aIntrVecs); i++)
    {
        pThis->aIntrVecs[i].cEvtsWaiting = 0;
        pThis->aIntrVecs[i].fIntrDisabled = false;
    }

    /*
     * Init queue states, except for the admin queues which are treated differently.
     */
    for (unsigned i = 1; i < pThis->cQueuesSubmMax; i++)
    {
        PNVMEQUEUESUBM   pNvmeQueue   = &pThis->aQueuesSubm[i];
        PNVMEQUEUESUBMR3 pNvmeQueueR3 = &pThisCC->aQueuesSubm[i];

        if (pNvmeQueueR3->pWrkThrdR3)
            nvmeR3WrkThrdRemoveSubmissionQueue(pDevIns, pNvmeQueue, pNvmeQueueR3);

        pNvmeQueue->Hdr.u16Id          = 0;
        pNvmeQueue->Hdr.cEntries       = 0;
        pNvmeQueue->Hdr.enmState       = NVMEQUEUESTATE_DEALLOCATED;
        pNvmeQueue->Hdr.cbEntry        = 0;
        pNvmeQueue->Hdr.GCPhysBase     = NIL_RTGCPHYS;
        pNvmeQueue->Hdr.idxHead        = 0;
        pNvmeQueue->Hdr.idxTail        = 0;
        pNvmeQueue->Hdr.fPhysCont      = false;
        pNvmeQueue->Hdr.enmType        = NVMEQUEUETYPE_INVALID;
        pNvmeQueue->cReqsActive        = 0;
        pNvmeQueue->u16CidDeallocating = 0;
    }

    for (unsigned i = 1; i < pThis->cQueuesCompMax; i++)
    {
        PNVMEQUEUECOMP   pNvmeQueue   = &pThis->aQueuesComp[i];
        PNVMEQUEUECOMPR3 pNvmeQueueR3 = &pThisCC->aQueuesComp[i];

        if (pNvmeQueueR3->hMtx != NIL_RTSEMFASTMUTEX)
            RTSemFastMutexDestroy(pNvmeQueueR3->hMtx);

        if (   pNvmeQueue->Hdr.enmState != NVMEQUEUESTATE_DEALLOCATED
            && pNvmeQueue->cWaiters)
        {
            /* Destroy all waiters. */
            PNVMECOMPQUEUEWAITER pWaiter;
            PNVMECOMPQUEUEWAITER pWaiterNext;
            RTListForEachSafe(&pNvmeQueueR3->LstCompletionsWaiting, pWaiter, pWaiterNext, NVMECOMPQUEUEWAITER, NdLstQueue)
            {
                RTListNodeRemove(&pWaiter->NdLstQueue);
                RTMemFree(pWaiter);
            }
        }

        pNvmeQueue->Hdr.u16Id      = 0;
        pNvmeQueue->Hdr.cEntries   = 0;
        pNvmeQueue->Hdr.enmState   = NVMEQUEUESTATE_DEALLOCATED;
        pNvmeQueue->Hdr.cbEntry    = 0;
        pNvmeQueue->Hdr.GCPhysBase = NIL_RTGCPHYS;
        pNvmeQueue->Hdr.idxHead    = 0;
        pNvmeQueue->Hdr.idxTail    = 0;
        pNvmeQueue->Hdr.fPhysCont  = false;
        pNvmeQueue->Hdr.enmType    = NVMEQUEUETYPE_INVALID;
        pNvmeQueue->cSubmQueuesRef = 0;
        pNvmeQueue->cWaiters       = 0;
        pNvmeQueueR3->hMtx         = NIL_RTSEMFASTMUTEX;
    }

    /*
     * For the admin queues we only reset the head and tail pointers.
     */
    pThis->aQueuesSubm[NVME_ADM_QUEUE_ID].Hdr.idxHead = 0;
    pThis->aQueuesSubm[NVME_ADM_QUEUE_ID].Hdr.idxTail = 0;

    pThis->aQueuesComp[NVME_ADM_QUEUE_ID].Hdr.idxHead = 0;
    pThis->aQueuesComp[NVME_ADM_QUEUE_ID].Hdr.idxTail = 0;

    ASMAtomicCmpXchgSize(&pThis->enmState, NVMESTATE_INIT, NVMESTATE_RESETTING, fRcXchg);
    Assert(fRcXchg || pThis->enmState == NVMESTATE_INIT);
}

/**
 * Resets the complete hardware state of the given controller including the admin queues.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 * @param   pThisCC     The NVMe controller ring-3 instance data.
 */
static void nvmeR3HwReset(PPDMDEVINS pDevIns, PNVME pThis, PNVMECC pThisCC)
{
    pThis->enmState = NVMESTATE_INIT;
    pThis->cActivities = 0;

    nvmeR3CtrlReset(pDevIns, pThis, pThisCC);

    /*
     * Initialise admin queues.
     */
    PNVMEQUEUESUBM pAdmQueueSubm = &pThis->aQueuesSubm[NVME_ADM_QUEUE_ID];

    pAdmQueueSubm->Hdr.u16Id            = NVME_ADM_QUEUE_ID;
    pAdmQueueSubm->Hdr.cEntries         = 1;
    pAdmQueueSubm->Hdr.cbEntry          = sizeof(NVMECMDADM);
    pAdmQueueSubm->Hdr.fPhysCont        = true;
    pAdmQueueSubm->Hdr.enmType          = NVMEQUEUETYPE_SUBMISSION;
    pAdmQueueSubm->u16CompletionQueueId = 0;
    pAdmQueueSubm->enmPriority          = NVMEQUEUESUBMPRIO_URGENT;

    PNVMEQUEUECOMP pAdmQueueComp = &pThis->aQueuesComp[NVME_ADM_QUEUE_ID];

    pAdmQueueComp->Hdr.u16Id      = NVME_ADM_QUEUE_ID;
    pAdmQueueComp->Hdr.cEntries   = 1;
    pAdmQueueComp->Hdr.cbEntry    = sizeof(NVMECQENT);
    pAdmQueueComp->Hdr.fPhysCont  = true;
    pAdmQueueComp->Hdr.enmType    = NVMEQUEUETYPE_COMPLETION;
    pAdmQueueComp->fIntrEnabled   = true;
    pAdmQueueComp->u32IntrVec     = 0;
    pAdmQueueComp->cSubmQueuesRef = 1;
}

/**
 * Fills in the given controller memory buffer size (CMBSZ) register from the given
 * controller memory buffer size and granularity.
 *
 * @returns VBox status code.
 * @param   pu32CtrlMemBufSz Where to store the content of the CMBSZ register on success.
 * @param   cbCtrlMemBuf     The size of the controller memory buffer in bytes.
 * @param   pszGranularity   The granularity as a string.
 */
static int nvmeR3CtrlMemBufFillCmbSz(uint32_t *pu32CtrlMemBufSz, uint64_t cbCtrlMemBuf, const char *pszGranularity)
{
    static struct
    {
        const char *pszGranularity;
        uint8_t     uSzUnit;
        uint64_t    cbUnit;
    } s_aGranularity2Sz[] =
    {
        { "4KB",   NVME_CMBSZ_SZU_4K,   _4K         },
        { "64KB",  NVME_CMBSZ_SZU_64K,  _64K        },
        { "1MB",   NVME_CMBSZ_SZU_1M,   _1M         },
        { "16MB",  NVME_CMBSZ_SZU_16M,  _16M        },
        { "256MB", NVME_CMBSZ_SZU_256M, _256M       },
        { "4GB",   NVME_CMBSZ_SZU_4G,   _4G         },
        { "64GB",  NVME_CMBSZ_SZU_64K,  64 * _1G64  }
    };

    for (unsigned i = 0; i < RT_ELEMENTS(s_aGranularity2Sz); i++)
    {
        if (!strcmp(s_aGranularity2Sz[i].pszGranularity, pszGranularity))
        {
            *pu32CtrlMemBufSz |= NVME_CMBSZ_SZU(s_aGranularity2Sz[i].uSzUnit);
            *pu32CtrlMemBufSz |= NVME_CMBSZ_SZ(cbCtrlMemBuf / s_aGranularity2Sz[i].cbUnit);
            return VINF_SUCCESS;
        }
    }

    return VERR_NOT_FOUND;
}

#ifdef VBOX_WITH_STATISTICS

/**
 * Registers all statistics for one memory type transfer.
 *
 * @param   pDevIns     The device instance.
 * @param   pStat       The physical memory type statistics entry.
 * @param   pszType     The type name.
 * @param   pszDesc     Description for the statistics in this entry.
 */
static void nvmeR3StatsPhysRegister(PPDMDEVINS pDevIns, PNVMEPHYSTYPESTAT pStat, const char *pszType, const char *pszDesc)
{
    PDMDevHlpSTAMRegisterF(pDevIns, &pStat->StatReadGuestMem,
                           STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                           pszDesc, "%s/ReadGuestMem", pszType);

    PDMDevHlpSTAMRegisterF(pDevIns, &pStat->StatReadCtrlMemBuf,
                           STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                           pszDesc, "%s/ReadCtrlMemBuf", pszType);

    PDMDevHlpSTAMRegisterF(pDevIns, &pStat->StatWrittenGuestMem,
                           STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                           pszDesc, "%s/WrittenGuestMem", pszType);

    PDMDevHlpSTAMRegisterF(pDevIns, &pStat->StatWrittenCtrlMemBuf,
                           STAMTYPE_COUNTER, STAMVISIBILITY_ALWAYS, STAMUNIT_BYTES,
                           pszDesc, "%s/WrittenCtrlMemBuf", pszType);
}

/**
 * Registers all statistics for the controller.
 *
 * @param   pDevIns     The device instance.
 * @param   pThis       The NVMe controller shared instance data.
 */
static void nvmeR3StatsRegister(PPDMDEVINS pDevIns, PNVME pThis)
{
    nvmeR3StatsPhysRegister(pDevIns, &pThis->aStatMemXfer[NVME_CMBSZ_SQS_BIT_IDX],   "SQS",   "Memory transfered for submission queues");
    nvmeR3StatsPhysRegister(pDevIns, &pThis->aStatMemXfer[NVME_CMBSZ_CQS_BIT_IDX],   "CQS",   "Memory transfered for completion queues");
    nvmeR3StatsPhysRegister(pDevIns, &pThis->aStatMemXfer[NVME_CMBSZ_LISTS_BIT_IDX], "LISTS", "Memory transfered for PRP or SGL lists");
    nvmeR3StatsPhysRegister(pDevIns, &pThis->aStatMemXfer[NVME_CMBSZ_RDS_BIT_IDX],   "RDS",   "Memory transfered for data reads");
    nvmeR3StatsPhysRegister(pDevIns, &pThis->aStatMemXfer[NVME_CMBSZ_WDS_BIT_IDX],   "WDS",   "Memory transfered for data writes");
}

#endif /* VBOX_WITH_STATISTICS */

/**
 * @interface_method_impl{PDMIBASE,pfnQueryInterface}
 */
static DECLCALLBACK(void *) nvmeR3QueryStatusInterface(PPDMIBASE pInterface, const char *pszIID)
{
    PNVMECC pThisCC = RT_FROM_MEMBER(pInterface, NVMER3, IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMIBASE, &pThisCC->IBase);
    PDMIBASE_RETURN_INTERFACE(pszIID, PDMILEDPORTS, &pThisCC->ILeds);
    return NULL;
}

/**
 * Gets the pointer to the status LED of a unit.
 *
 * @returns VBox status code.
 * @param   pInterface      Pointer to the interface structure containing the called function pointer.
 * @param   iLUN            The unit which status LED we desire.
 * @param   ppLed           Where to store the LED pointer.
 */
static DECLCALLBACK(int) nvmeR3QueryStatusLed(PPDMILEDPORTS pInterface, unsigned iLUN, PPDMLED *ppLed)
{
    PNVMECC pThisCC = RT_FROM_MEMBER(pInterface, NVMECC, ILeds);
    PNVME   pThis   = PDMDEVINS_2_DATA(pThisCC->pDevIns, PNVME);

    if (iLUN < pThis->cNamespaces)
    {
        *ppLed = &pThisCC->paNamespaces[iLUN].Led;
        Assert((*ppLed)->u32Magic == PDMLED_MAGIC);
        return VINF_SUCCESS;
    }
    return VERR_PDM_LUN_NOT_FOUND;
}


/**
 * @callback_method_impl{FNPCIIOREGIONMAP}
 */
static DECLCALLBACK(int) nvmeR3MapUnmapCtrlMemBuf(PPDMDEVINS pDevIns, PPDMPCIDEV pPciDev, uint32_t iRegion,
                                                  RTGCPHYS GCPhysAddress, RTGCPHYS cb, PCIADDRESSSPACE enmType)
{
    PNVME pThis = PDMDEVINS_2_DATA(pDevIns, PNVME);
    LogFlow(("nvmeR3Map: pDevIns=%#p pPciDev=%#p iRegion=%u GCPhysAddress=%RGp cb=%llu enmType=%d\n",
             pDevIns, pPciDev, iRegion, GCPhysAddress, cb, enmType));
    RT_NOREF(pPciDev, cb, enmType);

    Assert(pPciDev == pDevIns->apPciDevs[0]);
    AssertReturn(iRegion == NVME_PCI_MEM_CTRL_BUF_BAR, VERR_INTERNAL_ERROR_3);

    LogFlowFunc(("Memory controller buffer address = %RGp\n", GCPhysAddress));
    pThis->GCPhysCtrlMemBuf = GCPhysAddress;

    return VINF_SUCCESS;
}


/* -=-=-=-=- Helper -=-=-=-=- */

/**
 * Checks if all asynchronous I/O requests have finished.
 *
 * Used by nvmeR3Reset, nvmeR3Suspend and nvmeR3PowerOff. nvmeR3SavePrep makes
 * use of it in strict builds (which is why it's up here).
 *
 * @returns true if quiesced, false if busy.
 * @param   pDevIns         The device instance.
 */
static bool nvmeR3IoReqAllCompleted(PPDMDEVINS pDevIns)
{
    PNVME pThis = PDMDEVINS_2_DATA(pDevIns, PNVME);

    if (ASMAtomicReadU32(&pThis->cActivities) > 0)
        return false;
    return true;
}

/* -=-=-=-=- Saved State -=-=-=-=- */

/**
 * Saves a submission or completion queue header to the saved state.
 *
 * @param   pHlp        Pointer to the device helpers.
 * @param   pSSM        The saved state manager handle.
 * @param   pQueueHdr   The queue header to save.
 */
static void nvmeR3SaveQueueHdrExec(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM, PNVMEQUEUEHDR pQueueHdr)
{
    pHlp->pfnSSMPutU16(pSSM,    pQueueHdr->u16Id);
    pHlp->pfnSSMPutU16(pSSM,    pQueueHdr->cEntries);
    pHlp->pfnSSMPutU32(pSSM,    (uint32_t)pQueueHdr->enmState);
    pHlp->pfnSSMPutU64(pSSM,    pQueueHdr->cbEntry);
    pHlp->pfnSSMPutGCPhys(pSSM, pQueueHdr->GCPhysBase);
    pHlp->pfnSSMPutU32(pSSM,    pQueueHdr->idxHead);
    pHlp->pfnSSMPutU32(pSSM,    pQueueHdr->idxTail);
    pHlp->pfnSSMPutBool(pSSM,   pQueueHdr->fPhysCont);
    pHlp->pfnSSMPutU32(pSSM,    pQueueHdr->enmType);
}

/**
 * Loads a submission or completion queue header from the saved state.
 *
 * @returns VBox status code.
 * @param   pHlp        Pointer to the device helpers.
 * @param   pSSM         The saved state manager handle.
 * @param   pQueueHdr    The queue header to save.
 */
static int nvmeR3LoadQueueHdrExec(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM, PNVMEQUEUEHDR pQueueHdr)
{
    pHlp->pfnSSMGetU16(pSSM,    &pQueueHdr->u16Id);
    pHlp->pfnSSMGetU16(pSSM,    &pQueueHdr->cEntries);
    PDMDEVHLP_SSM_GET_ENUM32_RET(pHlp, pSSM, pQueueHdr->enmState, NVMEQUEUESTATE);

    uint64_t u64 = 0;
    int rc = pHlp->pfnSSMGetU64(pSSM, &u64);
    AssertRCReturn(rc, rc);
    pQueueHdr->cbEntry = u64;

    pHlp->pfnSSMGetGCPhys(pSSM, &pQueueHdr->GCPhysBase);
    pHlp->pfnSSMGetU32V(pSSM,   &pQueueHdr->idxHead);
    pHlp->pfnSSMGetU32V(pSSM,   &pQueueHdr->idxTail);
    pHlp->pfnSSMGetBool(pSSM,   &pQueueHdr->fPhysCont);
    PDMDEVHLP_SSM_GET_ENUM32_RET(pHlp, pSSM, pQueueHdr->enmType, NVMEQUEUETYPE);
    return VINF_SUCCESS;
}

/**
 * Saves all completion queue waiters to the saved state.
 *
 * @returns VBox status code.
 * @param   pHlp        Pointer to the device helpers.
 * @param   pSSM         The saved state manager handle.
 * @param   pQueueCompR3 The completion queue to save all waiters to, ring-3 bits.
 */
static int nvmeR3SaveCompQueueWaiters(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM, PNVMEQUEUECOMPR3 pQueueCompR3)
{
    int rc = VINF_SUCCESS;

    PNVMECOMPQUEUEWAITER pWaiter;
    RTListForEach(&pQueueCompR3->LstCompletionsWaiting, pWaiter, NVMECOMPQUEUEWAITER, NdLstQueue)
    {
        pHlp->pfnSSMPutU16(pSSM,  pWaiter->pQueueSubm->Hdr.u16Id);
        pHlp->pfnSSMPutU16(pSSM,  pWaiter->u16Cid);
        pHlp->pfnSSMPutU8(pSSM,   pWaiter->u8Sct);
        pHlp->pfnSSMPutU8(pSSM,   pWaiter->u8Sc);
        pHlp->pfnSSMPutU32(pSSM,  pWaiter->u32CmdSpecific);
        pHlp->pfnSSMPutBool(pSSM, pWaiter->fMore);
        rc = pHlp->pfnSSMPutBool(pSSM, pWaiter->fDnr);
        if (RT_FAILURE(rc))
            break;
    }

    return rc;
}

/**
 * Loads a completion queue waiter from the saved states.
 *
 * @returns VBox status code.
 * @param   pHlp        Pointer to the device helpers.
 * @param   pSSM         The saved state manager handle.
 * @param   pThis        The NVMe controller shared instance data.
 * @param   pQueueCompR3 The completion the waiter should be assigned to, ring-3 part.
 */
static int nvmeR3LoadCompQueueWaiter(PCPDMDEVHLPR3 pHlp, PSSMHANDLE pSSM, PNVME pThis, PNVMEQUEUECOMPR3 pQueueCompR3)
{
    int rc = VINF_SUCCESS;
    PNVMECOMPQUEUEWAITER pWaiter = (PNVMECOMPQUEUEWAITER)RTMemAllocZ(sizeof(NVMECOMPQUEUEWAITER));
    if (RT_LIKELY(pWaiter))
    {
        uint16_t u16SqId = 0;
        rc = pHlp->pfnSSMGetU16(pSSM, &u16SqId);
        if (   RT_SUCCESS(rc)
            && u16SqId < pThis->cQueuesSubmMax)
        {
            pWaiter->pQueueSubm = &pThis->aQueuesSubm[u16SqId];
            pHlp->pfnSSMGetU16(pSSM,  &pWaiter->u16Cid);
            pHlp->pfnSSMGetU8(pSSM,   &pWaiter->u8Sct);
            pHlp->pfnSSMGetU8(pSSM,   &pWaiter->u8Sc);
            pHlp->pfnSSMGetU32(pSSM,  &pWaiter->u32CmdSpecific);
            pHlp->pfnSSMGetBool(pSSM, &pWaiter->fMore);
            rc = pHlp->pfnSSMGetBool(pSSM, &pWaiter->fDnr);
            if (RT_SUCCESS(rc))
                RTListAppend(&pQueueCompR3->LstCompletionsWaiting, &pWaiter->NdLstQueue);
        }
        else if (RT_SUCCESS(rc))
            rc = VERR_SSM_DATA_UNIT_FORMAT_CHANGED;

        if (RT_FAILURE(rc))
            RTMemFree(pWaiter);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}

/**
 * @callback_method_impl{FNSSMDEVLIVEEXEC}
 */
static DECLCALLBACK(int) nvmeR3LiveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uPass)
{
    PNVME         pThis   = PDMDEVINS_2_DATA(pDevIns, PNVME);
    PNVMECC       pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC);
    PCPDMDEVHLPR3 pHlp    = pDevIns->pHlpR3;
    RT_NOREF(uPass);

    /* Save the part of the config used for verification purposes when restoring. */
    pHlp->pfnSSMPutU16(pSSM,  pThis->cQueuesSubmMax);
    pHlp->pfnSSMPutU16(pSSM,  pThis->cQueuesCompMax);
    pHlp->pfnSSMPutU16(pSSM,  pThis->cQueueEntriesMax);
    pHlp->pfnSSMPutU8(pSSM,   pThis->cTimeoutMax);
    pHlp->pfnSSMPutU32(pSSM,  pThis->cNamespaces);
    pHlp->pfnSSMPutU32(pSSM,  pThisCC->cAsyncEvtReqsMax);
    pHlp->pfnSSMPutStrZ(pSSM, pThis->szSerialNumber);
    pHlp->pfnSSMPutStrZ(pSSM, pThis->szModelNumber);
    pHlp->pfnSSMPutStrZ(pSSM, pThis->szFirmwareRevision);
    pHlp->pfnSSMPutU64(pSSM,  pThis->cbCtrlMemBuf);
    pHlp->pfnSSMPutU32(pSSM,  pThis->u32CtrlMemBufSz);
    for (unsigned i = 0; i < pThis->cNamespaces; i++)
    {
        pHlp->pfnSSMPutBool(pSSM, pThisCC->paNamespaces[i].pDrvBase != NULL);
        pHlp->pfnSSMPutU64(pSSM,  pThisCC->paNamespaces[i].cbBlock);
        pHlp->pfnSSMPutU64(pSSM,  pThisCC->paNamespaces[i].cBlocks);
    }

    return VINF_SSM_DONT_CALL_AGAIN;
}

/**
 * @callback_method_impl{FNSSMDEVSAVEPREP}
 */
static DECLCALLBACK(int) nvmeR3SavePrep(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    RT_NOREF(pDevIns, pSSM);
    LogFlow(("nvmeR3SavePrep:\n"));
    Assert(nvmeR3IoReqAllCompleted(pDevIns));
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEEXEC}
 */
static DECLCALLBACK(int) nvmeR3SaveExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    PNVME         pThis   = PDMDEVINS_2_DATA(pDevIns, PNVME);
    PNVMECC       pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC);
    PCPDMDEVHLPR3 pHlp    = pDevIns->pHlpR3;

    int rc = nvmeR3LiveExec(pDevIns, pSSM, SSM_PASS_FINAL);
    AssertRCReturn(rc, rc);

    pHlp->pfnSSMPutU32(pSSM, (uint32_t)pThis->enmState);

    pHlp->pfnSSMPutU32(pSSM, pThis->u32IntrMask);
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aIntrVecs); i++)
        pHlp->pfnSSMPutS32(pSSM, pThis->aIntrVecs[i].cEvtsWaiting);

    pHlp->pfnSSMPutU32(pSSM, pThis->u32IoCompletionQueueEntrySize);
    pHlp->pfnSSMPutU32(pSSM, pThis->u32IoSubmissionQueueEntrySize);
    pHlp->pfnSSMPutU8(pSSM,  pThis->uShutdwnNotifierLast);
    pHlp->pfnSSMPutU8(pSSM,  pThis->uAmsSet);
    pHlp->pfnSSMPutU8(pSSM,  pThis->uMpsSet);
    pHlp->pfnSSMPutU8(pSSM,  pThis->uCssSet);
    pHlp->pfnSSMPutU32(pSSM, pThis->u32RegIdx);
    pHlp->pfnSSMPutU32(pSSM, pThis->cbPage);

    /* Submission queue states. */
    for (unsigned i = 0; i < pThis->cQueuesSubmMax; i++)
    {
        PNVMEQUEUESUBM pQueueSubm = &pThis->aQueuesSubm[i];

        Assert(!pQueueSubm->cReqsActive);

        nvmeR3SaveQueueHdrExec(pHlp, pSSM, &pQueueSubm->Hdr);
        pHlp->pfnSSMPutU16(pSSM, pQueueSubm->u16CompletionQueueId);
        pHlp->pfnSSMPutU32(pSSM, (uint32_t)pQueueSubm->enmPriority);
        pHlp->pfnSSMPutU16(pSSM, pQueueSubm->u16CidDeallocating);
    }

    /* Completion queue states. */
    for (unsigned i = 0; i < pThis->cQueuesCompMax; i++)
    {
        PNVMEQUEUECOMP pQueueComp = &pThis->aQueuesComp[i];

        nvmeR3SaveQueueHdrExec(pHlp, pSSM, &pQueueComp->Hdr);
        pHlp->pfnSSMPutBool(pSSM, pQueueComp->fIntrEnabled);
        pHlp->pfnSSMPutBool(pSSM, pQueueComp->fOverloaded);
        pHlp->pfnSSMPutU32(pSSM, pQueueComp->u32IntrVec);
        pHlp->pfnSSMPutU32(pSSM, pQueueComp->cSubmQueuesRef);
        pHlp->pfnSSMPutU32(pSSM, pQueueComp->cWaiters);
        if (pQueueComp->cWaiters)
            rc = nvmeR3SaveCompQueueWaiters(pHlp, pSSM, &pThisCC->aQueuesComp[i]);
        AssertRCReturn(rc, rc);
    }

    pHlp->pfnSSMPutU32(pSSM, pThisCC->cAsyncEvtReqsCur);
    for (unsigned i = 0; i < pThisCC->cAsyncEvtReqsCur; i++)
        pHlp->pfnSSMPutU16(pSSM, pThisCC->paAsyncEvtReqCids[i]);

    /* Save namespace states. */
    for (unsigned i = 0; i < pThis->cNamespaces; i++)
    {
        PNVMENAMESPACE pNamespace = &pThisCC->paNamespaces[i];

        pHlp->pfnSSMPutU32(pSSM, pNamespace->u32Id);

        /* Save suspended requests. */
        if (pNamespace->pDrvMediaEx)
        {
            uint32_t cReqsRedo = pNamespace->pDrvMediaEx->pfnIoReqGetSuspendedCount(pNamespace->pDrvMediaEx);

            pHlp->pfnSSMPutU32(pSSM, cReqsRedo);
            if (cReqsRedo)
            {
                PDMMEDIAEXIOREQ hIoReq;
                PNVMEIOREQ pNvmeIoReq;
                rc = pNamespace->pDrvMediaEx->pfnIoReqQuerySuspendedStart(pNamespace->pDrvMediaEx, &hIoReq, (void **)&pNvmeIoReq);
                AssertRCBreak(rc);

                for (;;)
                {
                    pHlp->pfnSSMPutU16(pSSM, pNvmeIoReq->u16Cid);
                    pHlp->pfnSSMPutU16(pSSM, pNvmeIoReq->pQueueSubm->Hdr.u16Id);
                    pHlp->pfnSSMPutU64(pSSM, pNvmeIoReq->Prp1);
                    pHlp->pfnSSMPutU64(pSSM, pNvmeIoReq->Prp2);
                    pHlp->pfnSSMPutU64(pSSM, pNvmeIoReq->cbPrp);

                    rc = pNamespace->pDrvMediaEx->pfnIoReqSuspendedSave(pNamespace->pDrvMediaEx, pSSM, pNvmeIoReq->hIoReq);
                    if (RT_FAILURE(rc))
                        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                                N_("Failed to save an I/O request in the saved state for namespace %u"),
                                                pNamespace->u32Id);

                    cReqsRedo--;
                    if (!cReqsRedo)
                        break;

                    rc = pNamespace->pDrvMediaEx->pfnIoReqQuerySuspendedNext(pNamespace->pDrvMediaEx, hIoReq, &hIoReq, (void **)&pNvmeIoReq);
                    AssertRCBreak(rc);
                }
            }
        }
    }

    if (RT_SUCCESS(rc))
        pHlp->pfnSSMPutU32(pSSM, UINT32_MAX); /* sanity/terminator */

    return rc;
}


/**
 * @callback_method_impl{FNSSMDEVSAVEDONE}
 */
static DECLCALLBACK(int) nvmeR3SaveDone(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    RT_NOREF(pDevIns, pSSM);
    LogFlow(("nvmeR3SaveDone:\n"));
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVLOADPREP}
 */
static DECLCALLBACK(int) nvmeR3LoadPrep(PPDMDEVINS pDevIns, PSSMHANDLE pSSM)
{
    RT_NOREF(pDevIns, pSSM);
    LogFlow(("nvmeR3LoadPrep:\n"));
    Assert(nvmeR3IoReqAllCompleted(pDevIns));
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNSSMDEVLOADEXEC}
 */
static DECLCALLBACK(int) nvmeR3LoadExec(PPDMDEVINS pDevIns, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    PNVME         pThis   = PDMDEVINS_2_DATA(pDevIns, PNVME);
    PNVMECC       pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC);
    PCPDMDEVHLPR3 pHlp    = pDevIns->pHlpR3;
    uint8_t u8;
    uint16_t u16;
    uint32_t u32;

    LogFlow(("nvmeR3LoadExec:\n"));

    Assert(uPass == SSM_PASS_FINAL); NOREF(uPass);
    if (uVersion > NVME_SAVED_STATE_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /* Verify the config first. */
    int rc = pHlp->pfnSSMGetU16(pSSM,  &u16);
    AssertRCReturn(rc, rc);
    if (u16 != pThis->cQueuesSubmMax)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch: cQueuesSubmMax - saved=%u config=%u"),
                                       u16, pThis->cQueuesSubmMax);

    rc = pHlp->pfnSSMGetU16(pSSM,  &u16);
    AssertRCReturn(rc, rc);
    if (u16 != pThis->cQueuesCompMax)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch: cQueuesCompMax - saved=%u config=%u"),
                                       u16, pThis->cQueuesCompMax);

    rc = pHlp->pfnSSMGetU16(pSSM,  &u16);
    AssertRCReturn(rc, rc);
    if (u16 != pThis->cQueueEntriesMax)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch: cQueueEntriesMax - saved=%u config=%u"),
                                       u16, pThis->cQueueEntriesMax);

    rc = pHlp->pfnSSMGetU8(pSSM,  &u8);
    AssertRCReturn(rc, rc);
    if (u8 != pThis->cTimeoutMax)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch: cTimeoutMax - saved=%u config=%u"),
                                       u8, pThis->cTimeoutMax);

    rc = pHlp->pfnSSMGetU32(pSSM,  &u32);
    AssertRCReturn(rc, rc);
    if (u32 != pThis->cNamespaces)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch: cNamespaces - saved=%u config=%u"),
                                       u32, pThis->cNamespaces);

    rc = pHlp->pfnSSMGetU32(pSSM,  &u32);
    AssertRCReturn(rc, rc);
    if (u32 != pThisCC->cAsyncEvtReqsMax)
        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch: cAsyncEvtReqsMax - saved=%u config=%u"),
                                       u32, pThisCC->cAsyncEvtReqsMax);

    char szSerialNumber[NVME_SERIAL_NUMBER_LENGTH+1];
    rc = pHlp->pfnSSMGetStrZ(pSSM, szSerialNumber, sizeof(szSerialNumber));
    AssertRCReturn(rc, rc);
    if (strcmp(szSerialNumber, pThis->szSerialNumber))
        LogRel(("NVME%u: config mismatch: Serial number - saved='%s' config='%s'\n",
                pDevIns->iInstance, szSerialNumber, pThis->szSerialNumber));

    char szModelNumber[NVME_MODEL_NUMBER_LENGTH+1];
    rc = pHlp->pfnSSMGetStrZ(pSSM, szModelNumber, sizeof(szModelNumber));
    AssertRCReturn(rc, rc);
    if (strcmp(szModelNumber, pThis->szModelNumber))
        LogRel(("NVME%u: config mismatch: Model number - saved='%s' config='%s'\n",
                pDevIns->iInstance, szModelNumber, pThis->szModelNumber));

    char szFirmwareRevision[NVME_FIRMWARE_REVISION_LENGTH+1];
    rc = pHlp->pfnSSMGetStrZ(pSSM, szFirmwareRevision, sizeof(szFirmwareRevision));
    AssertRCReturn(rc, rc);
    if (strcmp(szFirmwareRevision, pThis->szFirmwareRevision))
        LogRel(("NVME%u: config mismatch: Firmware revision - saved='%s' config='%s'\n",
                pDevIns->iInstance, szFirmwareRevision, pThis->szFirmwareRevision));

    if (uVersion > NVME_SAVED_STATE_VERSION_PRE_CMB)
    {
        /* Verify size, granularity and supported features in both configs. */
        uint64_t u64;
        rc = pHlp->pfnSSMGetU64(pSSM, &u64);
        AssertRCReturn(rc, rc);
        if (u64 != pThis->cbCtrlMemBuf)
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch: cbCtrlMemBuf - saved=%llu config %llu"),
                                           u64, pThis->cbCtrlMemBuf);

        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        AssertRCReturn(rc, rc);
        if (u32 != pThis->u32CtrlMemBufSz)
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS, N_("Config mismatch: u32CtrlMemBufSz - saved=%#x config %#x"),
                                           u32, pThis->u32CtrlMemBufSz);
    }

    for (unsigned i = 0; i < pThis->cNamespaces; i++)
    {
        bool fInUse;
        uint64_t u64;
        rc = pHlp->pfnSSMGetBool(pSSM, &fInUse);
        AssertRCReturn(rc, rc);
        if (fInUse != (pThisCC->paNamespaces[i].pDrvBase != NULL))
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                           N_("The %s VM is missing a device on namespace %u. Please make sure the source and target VMs have compatible storage configurations"),
                                           fInUse ? "target" : "source", i );

        rc = pHlp->pfnSSMGetU64(pSSM, &u64);
        AssertRCReturn(rc, rc);
        if (u64 != pThisCC->paNamespaces[i].cbBlock)
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                           N_("The blocksize differs between source and target VM on namespace %u. Please make sure the source and target VMs have compatible storage configurations"),
                                           i);

        rc = pHlp->pfnSSMGetU64(pSSM, &u64);
        AssertRCReturn(rc, rc);
        if (u64 != pThisCC->paNamespaces[i].cBlocks)
            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                           N_("The number of blocks differs between source and target VM on namespace %u. Please make sure the source and target VMs have compatible storage configurations"),
                                           i);
    }

    if (uPass == SSM_PASS_FINAL)
    {
        /* Restore the controller instance data. */
        PDMDEVHLP_SSM_GET_ENUM32_RET(pHlp, pSSM, pThis->enmState, NVMESTATE);
        pHlp->pfnSSMGetU32V(pSSM, &pThis->u32IntrMask);
        for (unsigned i = 0; i < RT_ELEMENTS(pThis->aIntrVecs); i++)
        {
            rc = pHlp->pfnSSMGetS32V(pSSM, &pThis->aIntrVecs[i].cEvtsWaiting);
            AssertRCReturn(rc, rc);
            /* Update the interrupt disabled bit from the mask field. */
            if (pThis->u32IntrMask & RT_BIT_32(i))
                pThis->aIntrVecs[i].fIntrDisabled = true;
            else
                pThis->aIntrVecs[i].fIntrDisabled = false;
        }

        pHlp->pfnSSMGetU32(pSSM, &pThis->u32IoCompletionQueueEntrySize);
        pHlp->pfnSSMGetU32(pSSM, &pThis->u32IoSubmissionQueueEntrySize);
        pHlp->pfnSSMGetU8(pSSM, &pThis->uShutdwnNotifierLast);
        pHlp->pfnSSMGetU8(pSSM, &pThis->uAmsSet);
        pHlp->pfnSSMGetU8(pSSM, &pThis->uMpsSet);
        pHlp->pfnSSMGetU8(pSSM, &pThis->uCssSet);
        pHlp->pfnSSMGetU32(pSSM, &pThis->u32RegIdx);
        pHlp->pfnSSMGetU32(pSSM, &pThis->cbPage);

        /* Submission queue states. */
        AssertReturn(pThis->cQueuesSubmMax <= RT_ELEMENTS(pThis->aQueuesSubm), VERR_INTERNAL_ERROR_3);
        for (unsigned i = 0; i < pThis->cQueuesSubmMax; i++)
        {
            PNVMEQUEUESUBM pQueueSubm = &pThis->aQueuesSubm[i];

            Assert(!pQueueSubm->cReqsActive);

            rc = nvmeR3LoadQueueHdrExec(pHlp, pSSM, &pQueueSubm->Hdr);
            AssertRCReturn(rc, rc);

            pHlp->pfnSSMGetU16(pSSM, &pQueueSubm->u16CompletionQueueId);
            PDMDEVHLP_SSM_GET_ENUM32_RET(pHlp, pSSM, pQueueSubm->enmPriority, NVMEQUEUESUBMPRIO);
            rc = pHlp->pfnSSMGetU16(pSSM, &pQueueSubm->u16CidDeallocating);
            AssertRCReturn(rc, rc);

            if (   pQueueSubm->Hdr.enmState == NVMEQUEUESTATE_ALLOCATED
                && pQueueSubm->Hdr.u16Id != NVME_ADM_QUEUE_ID)
            {
                rc = nvmeR3SubmQueueAssignToWorker(pDevIns, pThis, pThisCC, pQueueSubm, &pThisCC->aQueuesSubm[i]);
                AssertRCReturn(rc, rc);
            }
        }

        /* Completion queue states. */
        AssertReturn(pThis->cQueuesCompMax <= RT_ELEMENTS(pThis->aQueuesComp), VERR_INTERNAL_ERROR_3);
        for (unsigned i = 0; i < pThis->cQueuesCompMax; i++)
        {
            PNVMEQUEUECOMP   pQueueComp   = &pThis->aQueuesComp[i];
            PNVMEQUEUECOMPR3 pQueueCompR3 = &pThisCC->aQueuesComp[i];

            rc = nvmeR3LoadQueueHdrExec(pHlp, pSSM, &pQueueComp->Hdr);
            AssertRCReturn(rc, rc);
            pHlp->pfnSSMGetBool(pSSM, &pQueueComp->fIntrEnabled);
            pHlp->pfnSSMGetBoolV(pSSM, &pQueueComp->fOverloaded);
            pHlp->pfnSSMGetU32(pSSM, &pQueueComp->u32IntrVec);

            rc = pHlp->pfnSSMGetU32V(pSSM, &pQueueComp->cSubmQueuesRef);
            AssertRCReturn(rc, rc);

            rc = pHlp->pfnSSMGetU32V(pSSM, &pQueueComp->cWaiters);
            AssertRCReturn(rc, rc);
            AssertLogRelMsgReturn(pQueueComp->cWaiters <= pThis->cCompQueuesWaitersMax,
                                  ("%u, max %u\n", pQueueComp->cWaiters, pThis->cCompQueuesWaitersMax),
                                  VERR_INTERNAL_ERROR_4);

            /** @todo Clear all previous waiting completions. */
            RTListInit(&pQueueCompR3->LstCompletionsWaiting);

            /* Load all waiting entries. */
            uint32_t cWaiters = pQueueComp->cWaiters;
            while (cWaiters-- > 0)
            {
                rc = nvmeR3LoadCompQueueWaiter(pHlp, pSSM, pThis, pQueueCompR3);
                AssertRCReturn(rc, rc);
            }

            /*
             * Create mutex if queue is allocated and not the admin queue
             * (the constructor created the queue in that case already.
             */
            if (   pQueueComp->Hdr.enmState == NVMEQUEUESTATE_ALLOCATED
                && pQueueComp->Hdr.u16Id != NVME_ADM_QUEUE_ID)
            {
                rc = RTSemFastMutexCreate(&pQueueCompR3->hMtx);
                AssertRCReturn(rc, rc);
            }
        }

        rc = pHlp->pfnSSMGetU32(pSSM, &pThisCC->cAsyncEvtReqsCur);
        AssertRCReturn(rc, rc);
        AssertLogRelReturn(pThisCC->cAsyncEvtReqsCur <= pThisCC->cAsyncEvtReqsMax, VERR_OUT_OF_RANGE);

        for (unsigned i = 0; i < pThisCC->cAsyncEvtReqsCur; i++)
            pHlp->pfnSSMGetU16(pSSM, &pThisCC->paAsyncEvtReqCids[i]);

        /* Load namespace states. */
        for (unsigned i = 0; i < pThis->cNamespaces; i++)
        {
            PNVMENAMESPACE pNamespace = &pThisCC->paNamespaces[i];

            rc = pHlp->pfnSSMGetU32(pSSM, &u32);
            AssertRCReturn(rc, rc);
            AssertReturn(u32 == pNamespace->u32Id, VERR_SSM_DATA_UNIT_FORMAT_CHANGED);

            if (pNamespace->pDrvMediaEx)
            {
                uint32_t cReqsRedo = 0;
                rc = pHlp->pfnSSMGetU32(pSSM, &cReqsRedo);
                AssertRCReturn(rc, rc);

                while (cReqsRedo--)
                {
                    uint16_t u16Cid, u16QueueSubmId;
                    NVMEPRP  Prp1, Prp2;
                    uint64_t cbPrp;
                    PNVMEQUEUESUBM pQueueSubm = NULL;

                    /* Restore data first. */
                    pHlp->pfnSSMGetU16(pSSM, &u16Cid);
                    pHlp->pfnSSMGetU16(pSSM, &u16QueueSubmId);
                    pHlp->pfnSSMGetU64(pSSM, &Prp1);
                    pHlp->pfnSSMGetU64(pSSM, &Prp2);

                    if (uVersion > NVME_SAVED_STATE_VERSION_PRE_PRP_SIZE_64BIT)
                        rc = pHlp->pfnSSMGetU64(pSSM, &cbPrp);
                    else
                    {
                        uint32_t cbPrp32 = 0;
                        rc = pHlp->pfnSSMGetU32(pSSM, &cbPrp32);
                        cbPrp = cbPrp32;
                    }
                    AssertRCReturn(rc, rc);

                    if (   u16QueueSubmId >= pThis->cQueuesSubmMax
                        || u16QueueSubmId != pThis->aQueuesSubm[u16QueueSubmId].Hdr.u16Id)
                        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                                       N_("Invalid submission queue ID encountered trying to restore an I/O request from the saved state for namespace %u"),
                                                       pNamespace->u32Id);

                    pQueueSubm = &pThis->aQueuesSubm[u16QueueSubmId];

                    /* Allocate new I/O request and link into redo list. */
                    PNVMEIOREQ pIoReq = nvmeR3IoReqAlloc(pNamespace, u16Cid, pQueueSubm,
                                                         Prp1, Prp2, cbPrp);
                    if (RT_LIKELY(pIoReq))
                    {
                        rc = pNamespace->pDrvMediaEx->pfnIoReqSuspendedLoad(pNamespace->pDrvMediaEx, pSSM, pIoReq->hIoReq);
                        if (RT_FAILURE(rc))
                            return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                                           N_("Failed to restore an I/O request from the saved state for namespace %u"),
                                                           pNamespace->u32Id);
                    }
                    else
                        return pHlp->pfnSSMSetCfgError(pSSM, RT_SRC_POS,
                                                       N_("Failed to restore an I/O request from the saved state for namespace %u"),
                                                       pNamespace->u32Id);
                }
            }
        }

        rc = pHlp->pfnSSMGetU32(pSSM, &u32);
        if (RT_FAILURE(rc))
            return rc;
        AssertMsgReturn(u32 == UINT32_MAX, ("%#x\n", u32), VERR_SSM_DATA_UNIT_FORMAT_CHANGED);
    }

    return rc;
}


/* -=-=-=-=- DBGF -=-=-=-=- */

/**
 * @callback_method_impl{FNDBGFHANDLERDEV, Dumps NVMe state.}
 */
static DECLCALLBACK(void) nvmeR3Info(PPDMDEVINS pDevIns, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RT_NOREF(pszArgs);

    PNVME pThis = PDMDEVINS_2_DATA(pDevIns, PNVME);

    /* Show basic information. */
    pHlp->pfnPrintf(pHlp,
                    "%s#%d: PCI MMIO=%RGp IRQ=%u MSI=%s RC=%RTbool R0=%RTbool\n",
                    pDevIns->pReg->szName,
                    pDevIns->iInstance,
                    PDMDevHlpMmioGetMappingAddress(pDevIns, pThis->hMmio),
                    PCIDevGetInterruptLine(pDevIns->apPciDevs[0]),
#ifdef VBOX_WITH_MSI_DEVICES
                    nvmeIsMSIEnabled(pDevIns->apPciDevs[0]) ? "on" : "off",
#else
                    "none",
#endif
                    pDevIns->fRCEnabled, pDevIns->fR0Enabled);
}


/**
 * Callback employed by nvmeR3Suspend and nvmeR3PowerOff.
 *
 * @returns true if we've quiesced, false if we're still working.
 * @param   pDevIns     The device instance.
 */
static DECLCALLBACK(bool) nvmeR3IsAsyncSuspendOrPowerOffDone(PPDMDEVINS pDevIns)
{
    if (!nvmeR3IoReqAllCompleted(pDevIns))
        return false;

    PNVMECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC);
    ASMAtomicWriteBool(&pThisCC->fSignalIdle, false);

    /** @todo Free cached requests, because we can't free any I/O buffer on destruct
     * because the driver is destroyed before us.
     */

    return true;
}

/**
 * Common worker for nvmeR3Suspend and nvmeR3PowerOff.
 */
static void nvmeR3SuspendOrPowerOff(PPDMDEVINS pDevIns)
{
    PNVME   pThis   = PDMDEVINS_2_DATA(pDevIns, PNVME);
    PNVMECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC);

    /*
     * For the case where the suspend notification is called
     * before the power off one the activity counter is already
     * 0 so skip the decrement to avoid any hangs.
     */
    if (   (   pThis->enmState == NVMESTATE_READY
            || pThis->enmState == NVMESTATE_PAUSED
            || pThis->enmState == NVMESTATE_FAULT)
        && ASMAtomicReadU32(&pThis->cActivities) > 0)
        ASMAtomicDecU32(&pThis->cActivities);

    ASMAtomicWriteBool(&pThisCC->fSignalIdle, true);
    if (!nvmeR3IoReqAllCompleted(pDevIns))
        PDMDevHlpSetAsyncNotification(pDevIns, nvmeR3IsAsyncSuspendOrPowerOffDone);
    else
    {
        /** @todo Free cached requests, because we can't free any I/O buffer on destruct
         * because the driver is destroyed before us.
         */
        ASMAtomicWriteBool(&pThisCC->fSignalIdle, false);
    }

    for (unsigned i = 0; i < pThis->cNamespaces; i++)
    {
        PNVMENAMESPACE pNamespace = &pThisCC->paNamespaces[i];

        if (pNamespace->pDrvMediaEx)
            pNamespace->pDrvMediaEx->pfnNotifySuspend(pNamespace->pDrvMediaEx);
    }
}

/**
 * Callback employed by nvmeR3Reset.
 *
 * @returns true if we've quiesced, false if we're still working.
 * @param   pDevIns     The device instance.
 */
static DECLCALLBACK(bool) nvmeR3IsAsyncResetDone(PPDMDEVINS pDevIns)
{
    PNVME   pThis   = PDMDEVINS_2_DATA(pDevIns, PNVME);
    PNVMECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC);

    if (!nvmeR3IoReqAllCompleted(pDevIns))
        return false;
    ASMAtomicWriteBool(&pThisCC->fSignalIdle, false);

    nvmeR3HwReset(pDevIns, pThis, pThisCC);
    return true;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnSuspend}
 */
static DECLCALLBACK(void) nvmeR3Suspend(PPDMDEVINS pDevIns)
{
    Log(("nvmeR3Suspend:\n"));
    nvmeR3SuspendOrPowerOff(pDevIns);
}

/**
 * @interface_method_impl{PDMDEVREG,pfnResume}
 */
static DECLCALLBACK(void) nvmeR3Resume(PPDMDEVINS pDevIns)
{
    PNVME pThis = PDMDEVINS_2_DATA(pDevIns, PNVME);
    Log(("nvmeR3Resume:\n"));

    if (   pThis->enmState == NVMESTATE_READY
        || pThis->enmState == NVMESTATE_PAUSED)
        ASMAtomicIncU32(&pThis->cActivities);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDetach}
 *
 * One harddisk at one port has been unplugged.
 * The VM is suspended at this point.
 */
static DECLCALLBACK(void) nvmeR3Detach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    RT_NOREF(fFlags);
    PNVME          pThis   = PDMDEVINS_2_DATA(pDevIns, PNVME);
    PNVMECC        pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC);
    PNVMENAMESPACE pNvmeNs = &pThisCC->paNamespaces[iLUN];

    if (iLUN >= pThis->cNamespaces)
        return;

    Assert(pNvmeNs->u32Id == iLUN);
    AssertMsg(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG,
              ("NVMe: Device does not support hotplugging\n"));

    Log(("%s:\n", __FUNCTION__));

    /*
     * Zero some important members.
     */
    pNvmeNs->pDrvBase = NULL;
    pNvmeNs->pDrvMedia = NULL;
    pNvmeNs->pDrvMediaEx = NULL;
}

/**
 * @interface_method_impl{PDMDEVREG,pfnAttach}
 */
static DECLCALLBACK(int) nvmeR3Attach(PPDMDEVINS pDevIns, unsigned iLUN, uint32_t fFlags)
{
    PNVME          pThis   = PDMDEVINS_2_DATA(pDevIns, PNVME);
    PNVMECC        pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC);
    PNVMENAMESPACE pNvmeNs = &pThisCC->paNamespaces[iLUN];

    if (iLUN >= pThis->cNamespaces)
        return VERR_PDM_LUN_NOT_FOUND;

    AssertMsgReturn(fFlags & PDM_TACH_FLAGS_NOT_HOT_PLUG,
                    ("LsiLogic: Device does not support hotplugging\n"),
                    VERR_INVALID_PARAMETER);

    /* the usual paranoia */
    AssertRelease(!pNvmeNs->pDrvBase);
    AssertRelease(!pNvmeNs->pDrvMedia);
    AssertRelease(!pNvmeNs->pDrvMediaEx);
    Assert(pNvmeNs->u32Id == iLUN);

    /*
     * Attach the media driver
     */
    char *pszName;
    if (RTStrAPrintf(&pszName, "NVMe#%uNs%u", pDevIns->iInstance, iLUN) <= 0)
        AssertLogRelFailedReturn(VERR_NO_MEMORY);

    int rc = PDMDevHlpDriverAttach(pDevIns, iLUN, &pNvmeNs->IBase, &pNvmeNs->pDrvBase, pszName);
    if (RT_SUCCESS(rc))
    {
        rc = nvmeR3NamespaceConfigure(pDevIns, pNvmeNs, true /*fReAttach*/);
        if (RT_FAILURE(rc))
            return PDMDEV_SET_ERROR(pDevIns, rc,
                                    N_("NVMe initialisation error: failed to initialize a namespace"));

    }
    else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
    {
        rc = VINF_SUCCESS;
        LogRel(("NVMe#%uNs%u: no driver attached\n", pDevIns->iInstance, iLUN));
    }
    else
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("NVMe: Failed to attach driver to %s"), pszName);

    if (RT_FAILURE(rc))
    {
        pNvmeNs->pDrvBase = NULL;
        pNvmeNs->pDrvMedia = NULL;
        pNvmeNs->pDrvMediaEx = NULL;
    }
    return rc;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnReset}
 */
static DECLCALLBACK(void) nvmeR3Reset(PPDMDEVINS pDevIns)
{
    PNVME   pThis   = PDMDEVINS_2_DATA(pDevIns, PNVME);
    PNVMECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC);
    LogFlow(("nvmeR3Reset:\n"));

    if (   (   pThis->enmState == NVMESTATE_READY
            || pThis->enmState == NVMESTATE_PAUSED
            || pThis->enmState == NVMESTATE_FAULT))
        ASMAtomicDecU32(&pThis->cActivities);

    ASMAtomicWriteBool(&pThisCC->fSignalIdle, true);
    if (!nvmeR3IoReqAllCompleted(pDevIns))
        PDMDevHlpSetAsyncNotification(pDevIns, nvmeR3IsAsyncResetDone);
    else
    {
        ASMAtomicWriteBool(&pThisCC->fSignalIdle, false);
        nvmeR3HwReset(pDevIns, pThis, pThisCC);
    }
}


/**
 * @interface_method_impl{PDMDEVREG,pfnPowerOff}
 */
static DECLCALLBACK(void) nvmeR3PowerOff(PPDMDEVINS pDevIns)
{
    Log(("nvmeR3PowerOff\n"));
    nvmeR3SuspendOrPowerOff(pDevIns);
}


/**
 * @interface_method_impl{PDMDEVREG,pfnDestruct}
 */
static DECLCALLBACK(int) nvmeR3Destruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN_QUIET(pDevIns);
    PNVME   pThis   = PDMDEVINS_2_DATA(pDevIns, PNVME);
    PNVMECC pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC);
    LogFlow(("nvmeR3Destruct:\n"));

    /* Tear down all worker threads. */
    while (pThisCC->cWrkThrdsCur)
    {
        PNVMEWRKTHRD pWrkThrd = RTListGetFirst(&pThisCC->LstWrkThrds, NVMEWRKTHRD, NodeWrkThrdList);
        AssertPtr(pWrkThrd);
        nvmeR3WrkThrdDestroy(pDevIns, pThisCC, pWrkThrd);
    }

    if (RTCritSectIsInitialized(&pThisCC->CritSectWrkThrds))
        RTCritSectDelete(&pThisCC->CritSectWrkThrds);

    if (RTCritSectIsInitialized(&pThisCC->CritSectAsyncEvtReqs))
        RTCritSectDelete(&pThisCC->CritSectAsyncEvtReqs);

    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aIntrVecs); ++i)
    {
        if (PDMDevHlpCritSectIsInitialized(pDevIns, &pThis->aIntrVecs[i].CritSectIntrVec))
            PDMDevHlpCritSectDelete(pDevIns, &pThis->aIntrVecs[i].CritSectIntrVec);
    }

    unsigned const cMaxQueues = RT_MIN(pThis->cQueuesCompMax, RT_ELEMENTS(pThisCC->aQueuesComp));
    for (unsigned i = 0; i < cMaxQueues; i++)
    {
        PNVMEQUEUECOMPR3 pQueueCompR3 = &pThisCC->aQueuesComp[i];

        if (pQueueCompR3->hMtx != NIL_RTSEMFASTMUTEX)
        {
            RTSemFastMutexDestroy(pQueueCompR3->hMtx);
            pQueueCompR3->hMtx = NIL_RTSEMFASTMUTEX;
        }
    }

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{PDMDEVREG,pfnConstruct}
 */
static DECLCALLBACK(int) nvmeR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PNVME           pThis   = PDMDEVINS_2_DATA(pDevIns, PNVME);
    PNVMECC         pThisCC = PDMDEVINS_2_DATA_CC(pDevIns, PNVMECC);
    PCPDMDEVHLPR3   pHlp    = pDevIns->pHlpR3;
    uint16_t        cQueuesSubmMax = 0;
    uint16_t        cQueuesCompMax = 0;
    uint16_t        cQueueEntriesMax = 0;
    uint8_t         cTimeoutMax = 0;
    uint32_t        cWrkThrdsMax = 0;
    uint32_t        cNamespacesMax = 0;
    uint32_t        cAsyncEvtReqsMax = 0;
    uint32_t        cCompQueuesWaitersMax = 0;
    bool            fMsiXSupported = false;
    int             rc;
    LogFlow(("nvmeR3Construct:\n"));

    /*
     * Initialize state sufficiently for the destructor to run cleanly.
     */
    RTListInit(&pThisCC->LstWrkThrds);
    for (unsigned i = 0; i < RT_ELEMENTS(pThisCC->aQueuesComp); i++)
        pThisCC->aQueuesComp[i].hMtx = NIL_RTSEMFASTMUTEX;

    /*
     * Check PGM page size ASSUMPTION in nvmeR3IoReqQueryBuf and
     * HcCtrlCfg_w (pThis->uMpsSet must be set to 12).
     */
    uint32_t const cbPage = PDMDevHlpPhysGetPageSize(pDevIns);
    AssertLogRelMsgReturn(cbPage == _4K, ("NVMe: cbPage=%#x, expected %#x!\n", cbPage, _4K), VERR_INTERNAL_ERROR);

    /*
     * Validate and read configuration.
     */
    PDMDEV_VALIDATE_CONFIG_RETURN(pDevIns,
                                  "QueuesSubmissionMax|"
                                  "QueuesCompletionMax|"
                                  "QueueEntriesMax|"
                                  "TimeoutMax|"
                                  "WorkerThreadsMax|"
                                  "NamespacesMax|"
                                  "AsyncEvtReqsMax|"
                                  "CompletionQueuesWaitersMax|"
                                  "SerialNumber|"
                                  "ModelNumber|"
                                  "FirmwareRevision|"
                                  "CtrlMemBufSize|"
                                  "CtrlMemBufGranularity|"
                                  "CtrlMemBufWds|"
                                  "CtrlMemBufRds|"
                                  "CtrlMemBufLists|"
                                  "CtrlMemBufCqs|"
                                  "CtrlMemBufSqs|"
                                  "MsiXSupported",
                                  "");

    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "QueuesSubmissionMax", &cQueuesSubmMax, NVME_QUEUES_SUBMISSION_MAX_DEF);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("NVMe configuration error: failed to read \"QueuesSubmissionMax\" as integer"));
    if (cQueuesSubmMax < NVME_QUEUES_COMPLETION_MIN || cQueuesSubmMax > NVME_QUEUES_COMPLETION_MAX)
        return PDMDevHlpVMSetError(pDevIns, VERR_OUT_OF_RANGE, RT_SRC_POS,
                                   N_("NVMe configuration error: \"QueuesSubmissionMax\"=%u is out of range (%u..%u)"),
                                   cQueuesSubmMax, NVME_QUEUES_COMPLETION_MIN, NVME_QUEUES_COMPLETION_MAX);

    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "QueuesCompletionMax", &cQueuesCompMax, NVME_QUEUES_COMPLETION_MAX_DEF);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("NVMe configuration error: failed to read \"QueuesCompletionMax\" as integer"));
    if (cQueuesCompMax < NVME_QUEUES_SUBMISSION_MIN || cQueuesSubmMax > NVME_QUEUES_SUBMISSION_MAX)
        return PDMDevHlpVMSetError(pDevIns, VERR_OUT_OF_RANGE, RT_SRC_POS,
                                   N_("NVMe configuration error: \"QueuesCompletionMax\"=%u is out of range (%u..%u)"),
                                   cQueuesCompMax, NVME_QUEUES_SUBMISSION_MIN, NVME_QUEUES_SUBMISSION_MAX);

    rc = pHlp->pfnCFGMQueryU16Def(pCfg, "QueueEntriesMax", &cQueueEntriesMax, NVME_QUEUE_ENTRIES_MAX_DEF);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe configuration error: failed to read \"QueueEntriesMax\" as integer"));

    rc = pHlp->pfnCFGMQueryU8Def(pCfg, "TimeoutMax", &cTimeoutMax, NVME_TIMEOUT_DEF);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe configuration error: failed to read \"TimeoutMax\" as integer"));

    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "WorkerThreadsMax", &cWrkThrdsMax, NVME_WORK_THREADS_MAX_DEF);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe configuration error: failed to read \"WorkerThreadsMax\" as integer"));

    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "NamespacesMax", &cNamespacesMax, NVME_NAMESPACES_MAX_DEF);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe configuration error: failed to read \"NamespacesMax\" as integer"));

    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "AsyncEvtReqsMax", &cAsyncEvtReqsMax, NVME_ASYNC_EVT_REQ_MAX_DEF);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe configuration error: failed to read \"AsyncEvtReqsMax\" as integer"));

    rc = pHlp->pfnCFGMQueryU32Def(pCfg, "CompletionQueuesWaitersMax", &cCompQueuesWaitersMax, NVME_COMP_QUEUES_WAITERS_MAX);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe configuration error: failed to read \"CompletionQueuesWaitersMax\" as integer"));

    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "SerialNumber", pThis->szSerialNumber, sizeof(pThis->szSerialNumber),
                              NVME_SERIAL_NUMBER_DEF);
    if (RT_FAILURE(rc))
    {
        if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
            return PDMDEV_SET_ERROR(pDevIns, VERR_INVALID_PARAMETER,
                                    N_("NVMe configuration error: \"SerialNumber\" is longer than 20 bytes"));
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe configuration error: failed to read \"SerialNumber\" as string"));
    }

    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "ModelNumber", pThis->szModelNumber, sizeof(pThis->szModelNumber),
                              NVME_MODEL_NUMBER_DEF);
    if (RT_FAILURE(rc))
    {
        if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
            return PDMDEV_SET_ERROR(pDevIns, VERR_INVALID_PARAMETER,
                                    N_("NVMe configuration error: \"ModelNumber\" is longer than 40 bytes"));
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe configuration error: failed to read \"ModelNumber\" as string"));
    }

    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "FirmwareRevision", pThis->szFirmwareRevision, sizeof(pThis->szFirmwareRevision),
                              NVME_FIRMWARE_REVISION_DEF);
    if (RT_FAILURE(rc))
    {
        if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
            return PDMDEV_SET_ERROR(pDevIns, VERR_INVALID_PARAMETER,
                                    N_("NVMe configuration error: \"FirmwareRevision\" is longer than 8 bytes"));
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe configuration error: failed to read \"FirmwareRevision\" as string"));
    }

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "MsiXSupported", &fMsiXSupported, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe configuration error: failed to read \"MsiXSupported\" as boolean"));

    /*
     * Parse the controller memory buffer related config.
     */
    bool fTmp = false;
    uint32_t u32CtrlMemBufSz = 0;
    char szCtrlMemBufGranularity[10];
    rc = pHlp->pfnCFGMQueryU64Def(pCfg, "CtrlMemBufSize", &pThis->cbCtrlMemBuf, NVME_CTRL_MEM_BUF_SIZE_DEF);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe configuration error: failed to read \"CtrlMemBufSize\" as integer"));
    rc = pHlp->pfnCFGMQueryStringDef(pCfg, "CtrlMemBufGranularity", szCtrlMemBufGranularity, sizeof(szCtrlMemBufGranularity),
                                     NVME_CTRL_MEM_BUF_GRANULARITY_DEF);
    if (RT_FAILURE(rc))
    {
        if (rc == VERR_CFGM_NOT_ENOUGH_SPACE)
            return PDMDEV_SET_ERROR(pDevIns, VERR_INVALID_PARAMETER,
                                    N_("NVMe configuration error: \"CtrlMemBufGranularity\" is invalid"));
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe configuration error: failed to read \"CtrlMemBufGranularity\" as string"));
    }

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "CtrlMemBufWds", &fTmp, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe configuration error: failed to read \"CtrlMemBufWds\" as boolean"));
    u32CtrlMemBufSz |= fTmp ? NVME_CMBSZ_WDS : 0;

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "CtrlMemBufRds", &fTmp, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe configuration error: failed to read \"CtrlMemBufRds\" as boolean"));
    u32CtrlMemBufSz |= fTmp ? NVME_CMBSZ_RDS : 0;

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "CtrlMemBufLists", &fTmp, false);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe configuration error: failed to read \"CtrlMemBufLists\" as boolean"));
    u32CtrlMemBufSz |= fTmp ? NVME_CMBSZ_LISTS : 0;

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "CtrlMemBufCqs", &fTmp, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe configuration error: failed to read \"CtrlMemBufCqs\" as boolean"));
    u32CtrlMemBufSz |= fTmp ? NVME_CMBSZ_CQS : 0;

    rc = pHlp->pfnCFGMQueryBoolDef(pCfg, "CtrlMemBufSqs", &fTmp, true);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe configuration error: failed to read \"CtrlMemBufSqs\" as boolean"));
    u32CtrlMemBufSz |= fTmp ? NVME_CMBSZ_SQS : 0;

    if (   pThis->cbCtrlMemBuf
        && (u32CtrlMemBufSz & (NVME_CMBSZ_WDS | NVME_CMBSZ_RDS | NVME_CMBSZ_LISTS | NVME_CMBSZ_CQS | NVME_CMBSZ_SQS)))
    {
        rc = nvmeR3CtrlMemBufFillCmbSz(&u32CtrlMemBufSz, pThis->cbCtrlMemBuf, szCtrlMemBufGranularity);
        if (RT_FAILURE(rc))
            return PDMDEV_SET_ERROR(pDevIns, rc,
                                    N_("NVMe configuration error: failed to fill controller memory buffer size register"));
    }
    else
    {
        pThis->cbCtrlMemBuf = 0;
        u32CtrlMemBufSz = 0;
    }

    LogFunc(("fRCEnabled=%RTbool fR0Enabled=%RTbool QueuesSubmissionMax=%u QueuesCompletionMax=%u QueueEntriesMax=%u TimeoutMax=%u WorkerThreadsMax=%u NamespacesMax=%u AsyncEvtReqsMax=%u CompletionQueuesWaitersMax=%u\n",
             pDevIns->fRCEnabled, pDevIns->fR0Enabled, cQueuesSubmMax, cQueuesCompMax, cQueueEntriesMax, cTimeoutMax, cWrkThrdsMax, cNamespacesMax, cAsyncEvtReqsMax, cCompQueuesWaitersMax));

    /*
     * Init instance data.
     */
    pThis->cQueuesSubmMax        = cQueuesSubmMax;
    pThis->cQueuesCompMax        = cQueuesCompMax;
    pThis->cQueueEntriesMax      = cQueueEntriesMax;
    pThis->cTimeoutMax           = cTimeoutMax;
    pThis->cWrkThrdsMax          = cWrkThrdsMax;
    pThis->cNamespaces           = cNamespacesMax;
    pThisCC->cAsyncEvtReqsMax    = cAsyncEvtReqsMax;
    pThis->cCompQueuesWaitersMax = cCompQueuesWaitersMax;
    pThis->u32CtrlMemBufSz       = u32CtrlMemBufSz;
    pThis->GCPhysCtrlMemBuf      = NIL_RTGCPHYS;
    pThisCC->pDevIns             = pDevIns;

    RTListInit(&pThisCC->LstWrkThrds);

    pThisCC->IBase.pfnQueryInterface    = nvmeR3QueryStatusInterface;
    pThisCC->ILeds.pfnQueryStatusLed    = nvmeR3QueryStatusLed;

    /* Fill PCI config space. */
    PPDMPCIDEV pPciDev = pDevIns->apPciDevs[0];
    PDMPCIDEV_ASSERT_VALID(pDevIns, pPciDev);

    PDMPciDevSetVendorId(pPciDev,       NVME_PCI_VENDOR_ID); /* Oracle Corporation */
    PDMPciDevSetDeviceId(pPciDev,       0x4e56); /* "NV" */
    PDMPciDevSetCommand(pPciDev,        0x0000);
#ifdef VBOX_WITH_MSI_DEVICES
    PDMPciDevSetStatus(pPciDev,         VBOX_PCI_STATUS_CAP_LIST);
    PDMPciDevSetCapabilityList(pPciDev, NVME_PCI_MSI_CAP_OFS);
#else
    PDMPciDevSetCapabilityList(pPciDev, 0x70);
#endif
    PDMPciDevSetRevisionId(pPciDev,     0x00);
    PDMPciDevSetClassProg(pPciDev,      0x02); /* NVM Express */
    PDMPciDevSetClassSub(pPciDev,       0x08); /* Non volatile memory controller. */
    PDMPciDevSetClassBase(pPciDev,      0x01); /* Mass storage controller. */

    PDMPciDevSetInterruptLine(pPciDev,  0x00);
    PDMPciDevSetInterruptPin(pPciDev,   0x01);
    /** @todo More Capabilities. */

    /*
     * Register PCI device and I/O region.
     */
    rc = PDMDevHlpPCIRegister(pDevIns, pPciDev);
    if (RT_FAILURE(rc))
        return rc;

#ifdef VBOX_WITH_MSI_DEVICES
    PDMMSIREG MsiReg;
    RT_ZERO(MsiReg);
    MsiReg.cMsiVectors     = 1;
    MsiReg.iMsiCapOffset   = NVME_PCI_MSI_CAP_OFS;
    MsiReg.iMsiNextOffset  = NVME_PCI_MSIX_CAP_OFS;
    MsiReg.fMsi64bit       = true;
    if (fMsiXSupported)
    {
        MsiReg.cMsixVectors    = VBOX_MSIX_MAX_ENTRIES;
        MsiReg.iMsixCapOffset  = NVME_PCI_MSIX_CAP_OFS;
        MsiReg.iMsixNextOffset = 0x00;
        MsiReg.iMsixBar        = NVME_PCI_MSIX_BAR;
    }
    rc = PDMDevHlpPCIRegisterMsi(pDevIns, &MsiReg);
    if (RT_FAILURE(rc))
    {
        PDMPciDevSetCapabilityList(pPciDev, 0x0);
        /* That's OK, we can work without MSI */
    }
#endif

    /* Set up interrupter locks. */
    for (unsigned i = 0; i < RT_ELEMENTS(pThis->aIntrVecs); ++i)
    {
        rc = PDMDevHlpCritSectInit(pDevIns, &pThis->aIntrVecs[i].CritSectIntrVec, RT_SRC_POS,
                                   "NVMeIntr%u#%u", iInstance, i);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("NVMe: Failed to create critical section for interrupter %u"), i);
    }

    /* Region #0: MMIO */
    uint32_t cbRegion = RT_MAX(  NVME_HC_REG_SIZE
                               + pThis->cQueuesSubmMax * sizeof(uint32_t)
                               + pThis->cQueuesCompMax * sizeof(uint32_t),
                               _32K);
    rc = PDMDevHlpPCIIORegionCreateMmio(pDevIns, 0 /*iPciRegion*/, cbRegion, PCI_ADDRESS_SPACE_MEM,
                                        nvmeMmioWrite, nvmeMmioRead, NULL /*pvUser*/,
                                          IOMMMIO_FLAGS_READ_DWORD_QWORD | IOMMMIO_FLAGS_WRITE_DWORD_QWORD_READ_MISSING
                                        | IOMMMIO_FLAGS_DBGSTOP_ON_COMPLICATED_WRITE, "NVMe", &pThis->hMmio);
    AssertRCReturn(rc, rc);


    /* Region #2: I/O ports. */
    static const IOMIOPORTDESC s_aExtDescs[] =
    {
        { "IDX", "IDX", NULL, NULL},   { "unused", "unused", NULL, NULL }, { "unused", "unused", NULL, NULL }, { "unused", "unused", NULL, NULL },
        { "DATA", "DATA", NULL, NULL}, { "unused", "unused", NULL, NULL }, { "unused", "unused", NULL, NULL }, { "unused", "unused", NULL, NULL },
        { NULL, NULL, NULL, NULL },
    };
    rc = PDMDevHlpPCIIORegionCreateIo(pDevIns, 2 /*iPciRegion*/, 8 /*cPorts*/, nvmeIdxDataWrite, nvmeIdxDataRead,
                                      NULL /*pvUser*/, "NVMe IDX/DATA", s_aExtDescs, &pThis->hIoPorts);
    AssertRCReturn(rc, rc);

    /* Region #3: Controller memory buffer region, optional. */
    if (pThis->cbCtrlMemBuf)
    {
        rc = PDMDevHlpPCIIORegionCreateMmio2Ex(pDevIns, pDevIns->apPciDevs[0], NVME_PCI_MEM_CTRL_BUF_BAR, pThis->cbCtrlMemBuf,
                                               PCI_ADDRESS_SPACE_MEM, 0 /*fMmio2Flags*/, nvmeR3MapUnmapCtrlMemBuf,
                                               "NVMe-MemCtrlBuf", &pThisCC->pvCtrlMemBufR3, &pThis->hMmio2);
        if (RT_FAILURE(rc))
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                        N_("Failed to create %llu (%#RX64) bytes of controller memory buffer for the NVMe device"),
                                        pThis->cbCtrlMemBuf, pThis->cbCtrlMemBuf);
    }

    /*
     * Register the saved state data unit.
     */
    rc = PDMDevHlpSSMRegisterEx(pDevIns, NVME_SAVED_STATE_VERSION, sizeof(*pThis), NULL,
                                NULL,           nvmeR3LiveExec, NULL,
                                nvmeR3SavePrep, nvmeR3SaveExec, nvmeR3SaveDone,
                                nvmeR3LoadPrep, nvmeR3LoadExec, NULL);
    if (RT_FAILURE(rc))
        return rc;

    rc = RTCritSectInit(&pThisCC->CritSectWrkThrds);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("NVMe initialisation error: Failed to create critical section for worker thread list"));

    /* Create the first worker thread. */
    PNVMEWRKTHRD pWrkThrd = NULL;
    rc = nvmeR3WrkThrdCreate(pDevIns, pThis, pThisCC, &pWrkThrd);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe initialisation error: failed to create worker thread for admin submission queue"));

    /*
     * Init state to handle async event requests.
     */
    pThisCC->cAsyncEvtReqsCur = 0;

    rc = RTCritSectInit(&pThisCC->CritSectAsyncEvtReqs);
    if (RT_FAILURE(rc))
        return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                   N_("NVMe initialisation error: Failed to create critical section for async event requests"));

    /* Allocate room for the async event requests. */
    pThisCC->paAsyncEvtReqCids = (uint16_t *)PDMDevHlpMMHeapAllocZ(pDevIns, pThisCC->cAsyncEvtReqsMax * sizeof(uint16_t));
    if (RT_UNLIKELY(!pThisCC->paAsyncEvtReqCids))
        return PDMDEV_SET_ERROR(pDevIns, VERR_NO_MEMORY,
                                N_("NVMe initialisation error: failed to allocate enough memory for all async event requests"));

    /*
     * The I/O queues.
     */

    /* Assign submission queue to worker thread. */
    nvmeR3WrkThrdAssignSubmQueue(pDevIns, pWrkThrd, &pThis->aQueuesSubm[NVME_ADM_QUEUE_ID],
                                 &pThisCC->aQueuesSubm[NVME_ADM_QUEUE_ID]);

    /* Create mutex for admin queue. */
    PNVMEQUEUECOMPR3 pAdmQueueCompR3 = &pThisCC->aQueuesComp[NVME_ADM_QUEUE_ID];
    rc = RTSemFastMutexCreate(&pAdmQueueCompR3->hMtx);
    if (RT_FAILURE(rc))
        return PDMDEV_SET_ERROR(pDevIns, rc,
                                N_("NVMe initialisation error: failed to create mutex for admin completion queue"));

    RTListInit(&pAdmQueueCompR3->LstCompletionsWaiting);

    /*
     * Do hardware reset.
     */
    nvmeR3HwReset(pDevIns, pThis, pThisCC);

    /*
     * Allocate room for the namespaces.
     */
    pThisCC->paNamespaces = (PNVMENAMESPACE)PDMDevHlpMMHeapAllocZ(pDevIns, pThis->cNamespaces * sizeof(NVMENAMESPACE));
    if (RT_UNLIKELY(!pThisCC->paNamespaces))
        return PDMDEV_SET_ERROR(pDevIns, VERR_NO_MEMORY,
                                N_("NVMe initialisation error: failed to allocate enough memory for all namespaces"));

    /*
     * Create namespaces.
     */
    for (unsigned i = 0; i < pThis->cNamespaces; i++)
    {
        char *pszName;
        PNVMENAMESPACE pNvmeNs = &pThisCC->paNamespaces[i];
        if (RTStrAPrintf(&pszName, "NVMe#%uNs%u", pDevIns->iInstance, i) <= 0) /* leak? */
            AssertLogRelFailedReturn(VERR_NO_MEMORY);

        pNvmeNs->pDevIns      = pDevIns;
        pNvmeNs->Led.u32Magic = PDMLED_MAGIC;
        pNvmeNs->u32Id        = i;

        /*
         * Init interfaces.
         */
        pNvmeNs->IBase.pfnQueryInterface        = nvmeR3NamespaceQueryInterface;
        pNvmeNs->IPort.pfnQueryDeviceLocation   = nvmeR3NamespaceQueryDeviceLocation;
        pNvmeNs->IPortEx.pfnIoReqCompleteNotify = nvmeR3IoReqCompleteNotify;
        pNvmeNs->IPortEx.pfnIoReqCopyFromBuf    = nvmeR3IoReqCopyFromBuf;
        pNvmeNs->IPortEx.pfnIoReqCopyToBuf      = nvmeR3IoReqCopyToBuf;
        pNvmeNs->IPortEx.pfnIoReqQueryBuf       = nvmeR3IoReqQueryBuf;
        pNvmeNs->IPortEx.pfnIoReqStateChanged   = nvmeR3IoReqStateChanged;

        /*
         * Attach the block driver
         */
        rc = PDMDevHlpDriverAttach(pDevIns, i, &pNvmeNs->IBase, &pNvmeNs->pDrvBase, pszName);
        if (RT_SUCCESS(rc))
        {
            rc = nvmeR3NamespaceConfigure(pDevIns, pNvmeNs, false /*fReAttach*/);
            if (RT_FAILURE(rc))
                return PDMDEV_SET_ERROR(pDevIns, rc,
                                        N_("NVMe initialisation error: failed to initialize a namespace"));

        }
        else if (rc == VERR_PDM_NO_ATTACHED_DRIVER)
        {
            rc = VINF_SUCCESS;
            LogRel(("NVMe#%uNs%u: no driver attached\n", pDevIns->iInstance, i));
        }
        else
            return PDMDevHlpVMSetError(pDevIns, rc, RT_SRC_POS,
                                       N_("NVMe: Failed to attach driver to %s"), pszName);
    }

    /* Create all other worker threads here for now because PDMDevHlpCreateThread() can only be called on EMT. */
    while (   pThisCC->cWrkThrdsCur < pThis->cWrkThrdsMax
           && RT_SUCCESS(rc))
    {
        /* Try to create a new worker. */
        rc = nvmeR3WrkThrdCreate(pDevIns, pThis, pThisCC, &pWrkThrd);
        if (RT_FAILURE(rc))
            LogRel(("NVME%u initialisation error: Failed to create a new worker thread with %Rrc, continuing with what is available\n",
                    pDevIns->iInstance, rc));
    }

    /*
     * Attach the status LED (optional).
     */
    PPDMIBASE pBase;
    rc = PDMDevHlpDriverAttach(pDevIns, PDM_STATUS_LUN, &pThisCC->IBase, &pBase, "Status Port");
    if (RT_SUCCESS(rc))
        pThisCC->pLedsConnector = PDMIBASE_QUERY_INTERFACE(pBase, PDMILEDCONNECTORS);
    else if (rc != VERR_PDM_NO_ATTACHED_DRIVER)
    {
        AssertMsgFailed(("NVMe: Failed to attach to status driver. rc=%Rrc\n", rc));
        return PDMDEV_SET_ERROR(pDevIns, rc, N_("NVMe cannot attach to status driver"));
    }

#ifdef VBOX_WITH_STATISTICS
    /*
     * Register statistics.
     */
    nvmeR3StatsRegister(pDevIns, pThis);
#endif

    /*
     * Register debugger info callbacks.
     */
    PDMDevHlpDBGFInfoRegister(pDevIns, "nvme", "NVMe registers.", nvmeR3Info);

    return VINF_SUCCESS;
}

#else  /* !IN_RING3 */

/**
 * @callback_method_impl{PDMDEVREGR0,pfnConstruct}
 */
static DECLCALLBACK(int) nvmeRZConstruct(PPDMDEVINS pDevIns)
{
    PDMDEV_CHECK_VERSIONS_RETURN(pDevIns);
    PNVME pThis = PDMDEVINS_2_DATA(pDevIns, PNVME);

    int rc = PDMDevHlpMmioSetUpContext(pDevIns, pThis->hMmio, nvmeMmioWrite, nvmeMmioRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    rc = PDMDevHlpIoPortSetUpContext(pDevIns, pThis->hIoPorts, nvmeIdxDataWrite, nvmeIdxDataRead, NULL /*pvUser*/);
    AssertRCReturn(rc, rc);

    return VINF_SUCCESS;
}

#endif /* !IN_RING3 */

const PDMDEVREG g_DeviceNVMe =
{
    /* .u32version = */             PDM_DEVREG_VERSION,
    /* .uReserved0 = */             0,
    /* .szName = */                 "nvme",
    /* .fFlags = */                 PDM_DEVREG_FLAGS_DEFAULT_BITS | PDM_DEVREG_FLAGS_RZ | PDM_DEVREG_FLAGS_NEW_STYLE
                                    | PDM_DEVREG_FLAGS_FIRST_SUSPEND_NOTIFICATION | PDM_DEVREG_FLAGS_FIRST_POWEROFF_NOTIFICATION
                                    | PDM_DEVREG_FLAGS_FIRST_RESET_NOTIFICATION,
    /* .fClass = */                 PDM_DEVREG_CLASS_STORAGE,
    /* .cMaxInstances = */          ~0U,
    /* .uSharedVersion = */         42,
    /* .cbInstanceShared = */       sizeof(NVME),
    /* .cbInstanceCC = */           sizeof(NVMECC),
    /* .cbInstanceRC = */           sizeof(NVMERC),
    /* .cMaxPciDevices = */         1,
    /* .cMaxMsixVectors = */        VBOX_MSIX_MAX_ENTRIES,
    /* .pszDescription = */         "NVMe storage controller.\n",
#if defined(IN_RING3)
# ifdef VBOX_IN_EXTPACK
    /* .pszRCMod = */               "VBoxNvmeRC.rc",
    /* .pszR0Mod = */               "VBoxNvmeR0.r0",
# else
    /* .pszRCMod = */               "VBoxDDRC.rc",
    /* .pszR0Mod = */               "VBoxDDR0.r0",
# endif
    /* .pfnConstruct = */           nvmeR3Construct,
    /* .pfnDestruct = */            nvmeR3Destruct,
    /* .pfnRelocate = */            NULL,
    /* .pfnMemSetup = */            NULL,
    /* .pfnPowerOn = */             NULL,
    /* .pfnReset = */               nvmeR3Reset,
    /* .pfnSuspend = */             nvmeR3Suspend,
    /* .pfnResume = */              nvmeR3Resume,
    /* .pfnAttach = */              nvmeR3Attach,
    /* .pfnDetach = */              nvmeR3Detach,
    /* .pfnQueryInterface = */      NULL,
    /* .pfnInitComplete = */        NULL,
    /* .pfnPowerOff = */            nvmeR3PowerOff,
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
    /* .pfnConstruct = */           nvmeRZConstruct,
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
    /* .pfnConstruct = */           nvmeRZConstruct,
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

