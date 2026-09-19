/** @file
 * Hyper-V related types and definitions.
 */

/*
 * Copyright (C) 2018-2026 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_nt_hyperv_h
#define IPRT_INCLUDED_nt_hyperv_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


#ifndef IN_IDA_PRO
# include <iprt/types.h>
# include <iprt/assertcompile.h>
#else
# define RT_FLEXIBLE_ARRAY
# define RT_FLEXIBLE_ARRAY_EXTENSION
# define AssertCompile(expr)
# define AssertCompileSize(type, size)
# define AssertCompileMemberOffset(type, member, off)
typedef unsigned char uint8_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;
#endif


/** Hyper-V partition ID. */
typedef uint64_t HV_PARTITION_ID;
/** Invalid Hyper-V partition ID. */
#define HV_PARTITION_ID_INVALID UINT64_C(0)
/** Hyper-V virtual processor index (== VMCPUID). */
typedef uint32_t HV_VP_INDEX;
/** Guest physical address (== RTGCPHYS). */
typedef uint64_t HV_GPA;
/** Guest physical page number. */
typedef uint64_t HV_GPA_PAGE_NUMBER;
/** System(/parent) physical page number. */
typedef uint64_t HV_SPA_PAGE_NUMBER;
/** Hyper-V unsigned 128-bit integer type.   */
typedef struct { uint64_t Low64, High64; } HV_UINT128;
/** Hyper-V port ID. */
typedef union
{
    uint32_t        AsUINT32;
    struct
    {
        uint32_t    Id       : 24;
        uint32_t    Reserved : 8;
    };
} HV_PORT_ID;
/** Pointer to a Hyper-V port ID. */
typedef HV_PORT_ID *PHV_PORT_ID;


/**
 * Hypercall IDs.
 */
typedef enum
{
    HvCallReserved0000 = 0,

    HvCallSwitchVirtualAddressSpace,
    HvCallFlushVirtualAddressSpace,
    HvCallFlushVirtualAddressList,
    HvCallGetLogicalProcessorRunTime,
    /* 5, 6 & 7 are deprecated / reserved. */
    HvCallNotifyLongSpinWait = 8,
    HvCallParkLogicalProcessors,        /**< @since v2 */
    HvCallInvokeHypervisorDebugger,     /**< @since v2 - not mentioned in TLFS v5.0b  */
    HvCallSendSyntheticClusterIpi,      /**< @since v? */
    HvCallModifyVtlProtectionMask,      /**< @since v? */
    HvCallEnablePartitionVtl,           /**< @since v? */
    HvCallDisablePartitionVtl,          /**< @since v? */
    HvCallEnableVpVtl,                  /**< @since v? */
    HvCallDisableVpVtl,                 /**< @since v? */
    HvCallVtlCall,                      /**< @since v? */
    HvCallVtlReturn,                    /**< @since v? */
    HvCallFlushVirtualAddressSpaceEx,   /**< @since v? */
    HvCallFlushVirtualAddressListEx,    /**< @since v? */
    HvCallSendSyntheticClusterIpiEx,    /**< @since v? */
    /* Reserved: 0x16..0x3f */

    HvCallCreatePartition = 0x40,
    HvCallInitializePartition,
    HvCallFinalizePartition,
    HvCallDeletePartition,
    HvCallGetPartitionProperty,
    HvCallSetPartitionProperty,
    HvCallGetPartitionId,
    HvCallGetNextChildPartition,
    HvCallDepositMemory,                /**< 0x48 - Repeat call. */
    HvCallWithdrawMemory,               /**< 0x49 - Repeat call. */
    HvCallGetMemoryBalance,
    HvCallMapGpaPages,                  /**< 0X4b - Repeat call. */
    HvCallUnmapGpaPages,                /**< 0X4c - Repeat call. */
    HvCallInstallIntercept,
    HvCallCreateVp,
    HvCallDeleteVp,                     /**< 0x4f - Fast call.  */
    HvCallGetVpRegisters,               /**< 0x50 - Repeat call. */
    HvCallSetVpRegisters,               /**< 0x51 - Repeat call. */
    HvCallTranslateVirtualAddress,
    HvCallReadGpa,
    HvCallWriteGpa,
    HvCallAssertVirtualInterruptV1,
    HvCallClearVirtualInterrupt,        /**< 0x56 - Fast call. */
    HvCallCreatePortV1,
    HvCallDeletePort,                   /**< 0x58 - Fast call. */
    HvCallConnectPortV1,
    HvCallGetPortProperty,
    HvCallDisconnectPort,
    HvCallPostMessage,
    HvCallSignalEvent,
    HvCallSavePartitionState,
    HvCallRestorePartitionState,
    HvCallInitializeEventLogBufferGroup,
    HvCallFinalizeEventLogBufferGroup,
    HvCallCreateEventLogBuffer,
    HvCallDeleteEventLogBuffer,
    HvCallMapEventLogBuffer,
    HvCallUnmapEventLogBuffer,
    HvCallSetEventLogGroupSources,
    HvCallReleaseEventLogBuffer,
    HvCallFlushEventLogBuffer,
    HvCallPostDebugData,
    HvCallRetrieveDebugData,
    HvCallResetDebugSession,
    HvCallMapStatsPage,
    HvCallUnmapStatsPage,
    HvCallMapSparseGpaPages,            /**< @since v2 */
    HvCallSetSystemProperty,            /**< @since v2 */
    HvCallSetPortProperty,              /**< @since v2 */
    /* 0x71..0x75 reserved/deprecated (was v2 test IDs). */
    HvCallAddLogicalProcessor = 0x76,
    HvCallRemoveLogicalProcessor,
    HvCallQueryNumaDistance,
    HvCallSetLogicalProcessorProperty,
    HvCallGetLogicalProcessorProperty,
    HvCallGetSystemProperty,
    HvCallMapDeviceInterrupt,
    HvCallUnmapDeviceInterrupt,
    HvCallRetargetDeviceInterrupt,
    /* 0x7f is reserved. */
    HvCallMapDevicePages = 0x80,
    HvCallUnmapDevicePages,
    HvCallAttachDevice,
    HvCallDetachDevice,
    HvCallNotifyStandbyTransition,
    HvCallPrepareForSleep,
    HvCallPrepareForHibernate,
    HvCallNotifyPartitionEvent,
    HvCallGetLogicalProcessorRegisters,
    HvCallSetLogicalProcessorRegisters,
    HvCallQueryAssociatedLpsforMca,
    HvCallNotifyRingEmpty,
    HvCallInjectSyntheticMachineCheck,
    HvCallScrubPartition,
    HvCallCollectLivedump,
    HvCallDisableHypervisor,
    HvCallModifySparseGpaPages,
    HvCallRegisterInterceptResult,
    HvCallUnregisterInterceptResult,
    /* 0x93 is reserved/undocumented. */
    HvCallAssertVirtualInterrupt = 0x94,
    HvCallCreatePort,
    HvCallConnectPort,
    HvCallGetSpaPageList,
    /* 0x98 is reserved. */
    HvCallStartVirtualProcessor = 0x99,
    HvCallGetVpIndexFromApicId,
    /* 0x9b..0xae are reserved/undocumented.
       0xad: New version of HvCallGetVpRegisters? Perhaps on logical CPU or smth. */
    HvCallFlushGuestPhysicalAddressSpace = 0xaf,
    HvCallFlushGuestPhysicalAddressList,
    /* 0xb1..0xb4 are unknown */
    HvCallCreateCpuGroup = 0xb5,
    HvCallDeleteCpuGroup,
    HvCallGetCpuGroupProperty,
    HvCallSetCpuGroupProperty,
    HvCallGetCpuGroupAffinit,
    HvCallGetNextCpuGroup = 0xba,
    HvCallGetNextCpuGroupPartition,
    HvCallPrecommitGpaPages = 0xbe,
    HvCallUncommitGpaPages,             /**< Happens when VidDestroyGpaRangeCheckSecure/WHvUnmapGpaRange is called. */
    /* 0xc0 is unknown */
    HvCallVpRunloopRelated = 0xc2,      /**< Fast */
    /* 0xc3..0xcb are unknown */
    HvCallQueryVtlProtectionMaskRange = 0xcc,
    HvCallModifyVtlProtectionMaskRange,
    /* 0xce..0xd1 are unknown */
    HvCallAcquireSparseGpaPageHostAccess = 0xd2,
    HvCallReleaseSparseGpaPageHostAccess,
    HvCallCheckSparseGpaPageVtlAccess,
    HvCallAcquireSparseSpaPageHostAccess = 0xd7,
    HvCallReleaseSparseSpaPageHostAccess,
    HvCallAcceptGpaPages,                       /**< 0x18 byte input, zero rep, no output. */
    /* 0xda..0xe0 are unknown (not dug out yet) */
    HvCallMapVpRegisterPage = 0xe1,             /**< Takes partition id + VP index (16 bytes). Returns a physical address (8 bytes). */
    HvCallUnmapVpRegisterPage,                  /**< Takes partition id + VP index. */
    HvCallUnknownE3,
    HvCallUnknownE4,
    HvCallUnknownE5,
    HvCallUnknownE6,
    /** Number of defined hypercalls (varies with version). */
    HvCallCount
} HV_CALL_CODE;
AssertCompile(HvCallSendSyntheticClusterIpiEx == 0x15);
AssertCompile(HvCallMapGpaPages == 0x4b);
AssertCompile(HvCallSetPortProperty == 0x70);
AssertCompile(HvCallRetargetDeviceInterrupt == 0x7e);
AssertCompile(HvCallUnregisterInterceptResult == 0x92);
AssertCompile(HvCallGetSpaPageList == 0x97);
AssertCompile(HvCallFlushGuestPhysicalAddressList == 0xb0);
AssertCompile(HvCallUncommitGpaPages == 0xbf);
AssertCompile(HvCallCount == 0xe7);

/** Makes the first parameter to a hypercall (rcx).  */
#define HV_MAKE_CALL_INFO(a_enmCallCode, a_cReps) ( (uint64_t)(a_enmCallCode) | ((uint64_t)(a_cReps) << 32) )
/** Makes the return value (success) for a rep hypercall. */
#define HV_MAKE_CALL_REP_RET(a_cReps)    ((uint64_t)(a_cReps) << 32)

/** Hypercall status code. */
typedef uint16_t HV_STATUS;

/** @name Hyper-V Hypercall status codes
 * @{ */
#define HV_STATUS_SUCCESS                                               (0x0000)
#define HV_STATUS_RESERVED_1                                            (0x0001)
#define HV_STATUS_INVALID_HYPERCALL_CODE                                (0x0002)
#define HV_STATUS_INVALID_HYPERCALL_INPUT                               (0x0003)
#define HV_STATUS_INVALID_ALIGNMENT                                     (0x0004)
#define HV_STATUS_INVALID_PARAMETER                                     (0x0005)
#define HV_STATUS_ACCESS_DENIED                                         (0x0006)
#define HV_STATUS_INVALID_PARTITION_STATE                               (0x0007)
#define HV_STATUS_OPERATION_DENIED                                      (0x0008)
#define HV_STATUS_UNKNOWN_PROPERTY                                      (0x0009)
#define HV_STATUS_PROPERTY_VALUE_OUT_OF_RANGE                           (0x000a)
#define HV_STATUS_INSUFFICIENT_MEMORY                                   (0x000b)
#define HV_STATUS_PARTITION_TOO_DEEP                                    (0x000c)
#define HV_STATUS_INVALID_PARTITION_ID                                  (0x000d)
#define HV_STATUS_INVALID_VP_INDEX                                      (0x000e)
#define HV_STATUS_RESERVED_F                                            (0x000f)
#define HV_STATUS_NOT_FOUND                                             (0x0010)
#define HV_STATUS_INVALID_PORT_ID                                       (0x0011)
#define HV_STATUS_INVALID_CONNECTION_ID                                 (0x0012)
#define HV_STATUS_INSUFFICIENT_BUFFERS                                  (0x0013)
#define HV_STATUS_NOT_ACKNOWLEDGED                                      (0x0014)
#define HV_STATUS_INVALID_VP_STATE                                      (0x0015)
#define HV_STATUS_ACKNOWLEDGED                                          (0x0016)
#define HV_STATUS_INVALID_SAVE_RESTORE_STATE                            (0x0017)
#define HV_STATUS_INVALID_SYNIC_STATE                                   (0x0018)
#define HV_STATUS_OBJECT_IN_USE                                         (0x0019)
#define HV_STATUS_INVALID_PROXIMITY_DOMAIN_INFO                         (0x001a)
#define HV_STATUS_NO_DATA                                               (0x001b)
#define HV_STATUS_INACTIVE                                              (0x001c)
#define HV_STATUS_NO_RESOURCES                                          (0x001d)
#define HV_STATUS_FEATURE_UNAVAILABLE                                   (0x001e)
#define HV_STATUS_PARTIAL_PACKET                                        (0x001f)
#define HV_STATUS_PROCESSOR_FEATURE_SSE3_NOT_SUPPORTED                  (0x0020)
#define HV_STATUS_PROCESSOR_FEATURE_LAHFSAHF_NOT_SUPPORTED              (0x0021)
#define HV_STATUS_PROCESSOR_FEATURE_SSSE3_NOT_SUPPORTED                 (0x0022)
#define HV_STATUS_PROCESSOR_FEATURE_SSE4_1_NOT_SUPPORTED                (0x0023)
#define HV_STATUS_PROCESSOR_FEATURE_SSE4_2_NOT_SUPPORTED                (0x0024)
#define HV_STATUS_PROCESSOR_FEATURE_SSE4A_NOT_SUPPORTED                 (0x0025)
#define HV_STATUS_PROCESSOR_FEATURE_XOP_NOT_SUPPORTED                   (0x0026)
#define HV_STATUS_PROCESSOR_FEATURE_POPCNT_NOT_SUPPORTED                (0x0027)
#define HV_STATUS_PROCESSOR_FEATURE_CMPXCHG16B_NOT_SUPPORTED            (0x0028)
#define HV_STATUS_PROCESSOR_FEATURE_ALTMOVCR8_NOT_SUPPORTED             (0x0029)
#define HV_STATUS_PROCESSOR_FEATURE_LZCNT_NOT_SUPPORTED                 (0x002a)
#define HV_STATUS_PROCESSOR_FEATURE_MISALIGNED_SSE_NOT_SUPPORTED        (0x002b)
#define HV_STATUS_PROCESSOR_FEATURE_MMX_EXT_NOT_SUPPORTED               (0x002c)
#define HV_STATUS_PROCESSOR_FEATURE_3DNOW_NOT_SUPPORTED                 (0x002d)
#define HV_STATUS_PROCESSOR_FEATURE_EXTENDED_3DNOW_NOT_SUPPORTED        (0x002e)
#define HV_STATUS_PROCESSOR_FEATURE_PAGE_1GB_NOT_SUPPORTED              (0x002f)
#define HV_STATUS_PROCESSOR_CACHE_LINE_FLUSH_SIZE_INCOMPATIBLE          (0x0030)
#define HV_STATUS_PROCESSOR_FEATURE_XSAVE_NOT_SUPPORTED                 (0x0031)
#define HV_STATUS_PROCESSOR_FEATURE_XSAVEOPT_NOT_SUPPORTED              (0x0032)
#define HV_STATUS_INSUFFICIENT_BUFFER                                   (0x0033)
#define HV_STATUS_PROCESSOR_FEATURE_XSAVE_AVX_NOT_SUPPORTED             (0x0034)
#define HV_STATUS_PROCESSOR_FEATURE_XSAVE_ FEATURE_NOT_SUPPORTED        (0x0035)
#define HV_STATUS_PROCESSOR_XSAVE_SAVE_AREA_INCOMPATIBLE                (0x0036)
#define HV_STATUS_INCOMPATIBLE_PROCESSOR                                (0x0037)
#define HV_STATUS_INSUFFICIENT_DEVICE_DOMAINS                           (0x0038)
#define HV_STATUS_PROCESSOR_FEATURE_AES_NOT_SUPPORTED                   (0x0039)
#define HV_STATUS_PROCESSOR_FEATURE_PCLMULQDQ_NOT_SUPPORTED             (0x003a)
#define HV_STATUS_PROCESSOR_FEATURE_INCOMPATIBLE_XSAVE_FEATURES         (0x003b)
#define HV_STATUS_CPUID_FEATURE_VALIDATION_ERROR                        (0x003c)
#define HV_STATUS_CPUID_XSAVE_FEATURE_VALIDATION_ERROR                  (0x003d)
#define HV_STATUS_PROCESSOR_STARTUP_TIMEOUT                             (0x003e)
#define HV_STATUS_SMX_ENABLED                                           (0x003f)
#define HV_STATUS_PROCESSOR_FEATURE_PCID_NOT_SUPPORTED                  (0x0040)
#define HV_STATUS_INVALID_LP_INDEX                                      (0x0041)
#define HV_STATUS_FEATURE_FMA4_NOT_SUPPORTED                            (0x0042)
#define HV_STATUS_FEATURE_F16C_NOT_SUPPORTED                            (0x0043)
#define HV_STATUS_PROCESSOR_FEATURE_RDRAND_NOT_SUPPORTED                (0x0044)
#define HV_STATUS_PROCESSOR_FEATURE_RDWRFSGS_NOT_SUPPORTED              (0x0045)
#define HV_STATUS_PROCESSOR_FEATURE_SMEP_NOT_SUPPORTED                  (0x0046)
#define HV_STATUS_PROCESSOR_FEATURE_ENHANCED_FAST_STRING_NOT_SUPPORTED  (0x0047)
#define HV_STATUS_PROCESSOR_FEATURE_MOVBE_NOT_SUPPORTED                 (0x0048)
#define HV_STATUS_PROCESSOR_FEATURE_BMI1_NOT_SUPPORTED                  (0x0049)
#define HV_STATUS_PROCESSOR_FEATURE_BMI2_NOT_SUPPORTED                  (0x004a)
#define HV_STATUS_PROCESSOR_FEATURE_HLE_NOT_SUPPORTED                   (0x004b)
#define HV_STATUS_PROCESSOR_FEATURE_RTM_NOT_SUPPORTED                   (0x004c)
#define HV_STATUS_PROCESSOR_FEATURE_XSAVE_FMA_NOT_SUPPORTED             (0x004d)
#define HV_STATUS_PROCESSOR_FEATURE_XSAVE_AVX2_NOT_SUPPORTED            (0x004e)
#define HV_STATUS_PROCESSOR_FEATURE_NPIEP1_NOT_SUPPORTED                (0x004f)
#define HV_STATUS_INVALID_REGISTER_VALUE                                (0x0050)
#define HV_STATUS_PROCESSOR_FEATURE_RDSEED_NOT_SUPPORTED                (0x0052)
#define HV_STATUS_PROCESSOR_FEATURE_ADX_NOT_SUPPORTED                   (0x0053)
#define HV_STATUS_PROCESSOR_FEATURE_SMAP_NOT_SUPPORTED                  (0x0054)
#define HV_STATUS_NX_NOT_DETECTED                                       (0x0055)
#define HV_STATUS_PROCESSOR_FEATURE_INTEL_PREFETCH_NOT_SUPPORTED        (0x0056)
#define HV_STATUS_INVALID_DEVICE_ID                                     (0x0057)
#define HV_STATUS_INVALID_DEVICE_STATE                                  (0x0058)
#define HV_STATUS_PENDING_PAGE_REQUESTS                                 (0x0059)
#define HV_STATUS_PAGE_REQUEST_INVALID                                  (0x0060)
#define HV_STATUS_OPERATION_FAILED                                      (0x0071)
#define HV_STATUS_NOT_ALLOWED_WITH_NESTED_VIRT_ACTIVE                   (0x0072)
/** @} */


/** Hyper-V partition property value. */
typedef uint64_t HV_PARTITION_PROPERTY;
/** Pointer to a partition property value. */
typedef HV_PARTITION_PROPERTY *PHV_PARTITION_PROPERTY;
/**
 * Hyper-V partition property code.
 * This is documented in TLFS, except version 5.x.
 */
typedef enum
{
    HvPartitionPropertyPrivilegeFlags = 0x00010000,
    HvPartitionPropertySyntheticProcessorFeaturesBanks, /**< Read by WHvApi::Capabilities::GetSyntheticProcessorFeaturesBanks (build 22000) */

    HvPartitionPropertyCpuReserve = 0x00020001,
    HvPartitionPropertyCpuCap,
    HvPartitionPropertyCpuWeight,
    HvPartitionPropertyUnknown20004,                /**< On exo partition (build 17134), initial value zero. */

    HvPartitionPropertyEmulatedTimerPeriod = 0x00030000, /**< @note Fails on exo partition (build 17134). */
    HvPartitionPropertyEmulatedTimerControl,        /**< @note Fails on exo partition (build 17134). */
    HvPartitionPropertyPmTimerAssist,               /**< @note Fails on exo partition (build 17134). */
    HvPartitionPropertyUnknown30003,                /**< @note WHvSetupPartition writes this (build 22000). */
    HvPartitionPropertyUnknown30004,                /**< ? */
    HvPartitionPropertyUnknown30005,                /**< WHvPartitionPropertyCodeReferenceTime maps to this (build 22000) */

    HvPartitionPropertyDebugChannelId = 0x00040000, /**< @note Hangs system on exo partition hangs (build 17134). */

    HvPartitionPropertyVirtualTlbPageCount = 0x00050000,
    HvPartitionPropertyUnknown50001,                /**< On exo partition (build 17134), initial value zero. */
    HvPartitionPropertyUnknown50002,                /**< On exo partition (build 17134), initial value zero. */
    HvPartitionPropertyUnknown50003,                /**< On exo partition (build 17134), initial value zero. */
    HvPartitionPropertyUnknown50004,                /**< On exo partition (build 17134), initial value zero. */
    HvPartitionPropertyUnknown50005,                /**< On exo partition (build 17134), initial value one. */
    HvPartitionPropertyUnknown50006,                /**< On exo partition (build 17134), initial value zero.
                                                     * @note build 22000/w11-ga fends this off in VID.SYS. */
    HvPartitionPropertyUnknown50007,
    HvPartitionPropertyUnknown50008,
    HvPartitionPropertyUnknown50009,
    HvPartitionPropertyUnknown5000a,
    HvPartitionPropertyUnknown5000b,
    HvPartitionPropertyUnknown5000c,
    HvPartitionPropertyUnknown5000d,
    HvPartitionPropertyUnknown5000e,
    HvPartitionPropertyUnknown5000f,
    HvPartitionPropertyUnknown50010,
    HvPartitionPropertyUnknown50012,
    HvPartitionPropertyUnknown50013,                /**< Set by WHvSetupPartition (build 22000) */
    HvPartitionPropertyUnknown50014,
    HvPartitionPropertyUnknown50015,
    HvPartitionPropertyUnknown50016,
    HvPartitionPropertyUnknown50017,                /**< Set by WHvSetupPartition (build 22000) */

    HvPartitionPropertyProcessorVendor = 0x00060000,
    HvPartitionPropertyProcessorFeatures,           /**< On exo/17134/threadripper: 0x6cb26f39fbf */
    HvPartitionPropertyProcessorXsaveFeatures,
    HvPartitionPropertyProcessorCLFlushSize,        /**< On exo/17134/threadripper: 8 */
    HvPartitionPropertyUnknown60004,                /**< On exo partition (build 17134), initial value zero. */
    HvPartitionPropertyUnknown60005,                /**< On exo partition (build 17134), initial value 0x603. */
    HvPartitionPropertyUnknown60006,                /**< On exo partition (build 17134), initial value 0x2c. */
    HvPartitionPropertyUnknown60007,                /**< WHvSetupPartition reads this (build 22000). */
    HvPartitionPropertyUnknown60008,                /**< WHvSetupPartition reads this (build 22000). */
    HvPartitionPropertyProcessorClockFrequency,     /**< Read by WHvApi::Capabilities::GetProcessorClockFrequency (build 22000). */
    HvPartitionPropertyProcessorFeaturesBank0,      /**< Read by WHvApi::Capabilities::GetProcessorFeaturesBanks (build 22000). */
    HvPartitionPropertyProcessorFeaturesBank1,      /**< Read by WHvApi::Capabilities::GetProcessorFeaturesBanks (build 22000). */

    HvPartitionPropertyGuestOsId = 0x00070000,      /**< @since v4 */

    HvPartitionPropertyUnknown800000 = 0x00080000   /**< On exo partition (build 17134), initial value zero. */
} HV_PARTITION_PROPERTY_CODE;
AssertCompileSize(HV_PARTITION_PROPERTY_CODE, 4);
/** Pointer to a partition property code. */
typedef HV_PARTITION_PROPERTY_CODE *PHV_PARTITION_PROPERTY_CODE;


/** Input for HvCallGetPartitionProperty. */
typedef struct
{
    HV_PARTITION_ID             PartitionId;
    HV_PARTITION_PROPERTY_CODE  PropertyCode;
    uint32_t                    uPadding;
} HV_INPUT_GET_PARTITION_PROPERTY;
AssertCompileSize(HV_INPUT_GET_PARTITION_PROPERTY, 16);
/** Pointer to input for HvCallGetPartitionProperty. */
typedef HV_INPUT_GET_PARTITION_PROPERTY *PHV_INPUT_GET_PARTITION_PROPERTY;

/** Output for HvCallGetPartitionProperty. */
typedef struct
{
    HV_PARTITION_PROPERTY       PropertyValue;
} HV_OUTPUT_GET_PARTITION_PROPERTY;
/** Pointer to output for HvCallGetPartitionProperty. */
typedef HV_OUTPUT_GET_PARTITION_PROPERTY *PHV_OUTPUT_GET_PARTITION_PROPERTY;


/** Input for HvCallSetPartitionProperty. */
typedef struct
{
    HV_PARTITION_ID             PartitionId;
    HV_PARTITION_PROPERTY_CODE  PropertyCode;
    uint32_t                    uPadding;
    HV_PARTITION_PROPERTY       PropertyValue;
} HV_INPUT_SET_PARTITION_PROPERTY;
AssertCompileSize(HV_INPUT_SET_PARTITION_PROPERTY, 24);
/** Pointer to input for HvCallSetPartitionProperty. */
typedef HV_INPUT_SET_PARTITION_PROPERTY *PHV_INPUT_SET_PARTITION_PROPERTY;


/** Hyper-V NUMA node ID.
 * On systems without NUMA, i.e. a single node, it uses 0 as identifier.  */
typedef uint32_t HV_PROXIMITY_DOMAIN_ID;
/** Pointer to NUMA node ID. */
typedef HV_PROXIMITY_DOMAIN_ID *PHV_PROXIMITY_DOMAIN_ID;

/** Hyper-V NUMA flags. */
typedef struct
{
    uint32_t    ProximityPreferred      : 1;    /**< When set, allocations may come from other NUMA nodes.  */
    uint32_t    Reserved                : 30;   /**< Reserved for future (as of circa v2). */
    uint32_t    ProxyimityInfoValid     : 1;    /**< Set if the NUMA information is valid. */
} HV_PROXIMITY_DOMAIN_FLAGS;
/** Pointer to Hyper-V NUMA flags. */
typedef HV_PROXIMITY_DOMAIN_FLAGS *PHV_PROXIMITY_DOMAIN_FLAGS;

/** Hyper-V NUMA information. */
typedef struct
{
    HV_PROXIMITY_DOMAIN_ID      Id;             /**< NUMA node identifier.  */
    HV_PROXIMITY_DOMAIN_FLAGS   Flags;          /**< NUMA flags. */
} HV_PROXIMITY_DOMAIN_INFO;
/** Pointer to Hyper-V NUMA information. */
typedef HV_PROXIMITY_DOMAIN_INFO *PHV_PROXIMITY_DOMAIN_INFO;

/** Input for HvCallGetMemoryBalance. */
typedef struct
{
    HV_PARTITION_ID             TargetPartitionId;
    HV_PROXIMITY_DOMAIN_INFO    ProximityDomainInfo;
} HV_INPUT_GET_MEMORY_BALANCE;
AssertCompileSize(HV_INPUT_GET_MEMORY_BALANCE, 16);
/** Pointer to the input for HvCallGetMemoryBalance. */
typedef HV_INPUT_GET_MEMORY_BALANCE *PHV_INPUT_GET_MEMORY_BALANCE;

/** Output for HvCallGetMemoryBalance. */
typedef struct
{
    uint64_t                    PagesAvailable;
    uint64_t                    PagesInUse;
} HV_OUTPUT_GET_MEMORY_BALANCE;
/** Pointer to the output for HvCallGetMemoryBalance. */
typedef HV_OUTPUT_GET_MEMORY_BALANCE *PHV_OUTPUT_GET_MEMORY_BALANCE;


/** @name Flags used with HvCallMapGpaPages and HvCallMapSparseGpaPages.
 * @note There seems to be a more flags defined after v2.
 * @{ */
typedef uint32_t HV_MAP_GPA_FLAGS;
#define HV_MAP_GPA_READABLE             UINT32_C(0x0001)
#define HV_MAP_GPA_WRITABLE             UINT32_C(0x0002)
#define HV_MAP_GPA_EXECUTABLE           UINT32_C(0x0004)
/** Seems this have to be set when HV_MAP_GPA_EXECUTABLE is (17101). */
#define HV_MAP_GPA_EXECUTABLE_AGAIN     UINT32_C(0x0008)
/** Dunno what this is yet, but it requires HV_MAP_GPA_DUNNO_1000.
 * The readable bit gets put here when both HV_MAP_GPA_DUNNO_1000 and
 * HV_MAP_GPA_DUNNO_MASK_0700 are clear. */
#define HV_MAP_GPA_DUNNO_ACCESS         UINT32_C(0x0010)
/** Guess work. */
#define HV_MAP_GPA_MAYBE_ACCESS_MASK    UINT32_C(0x001f)
/** Some kind of mask. */
#define HV_MAP_GPA_DUNNO_MASK_0700      UINT32_C(0x0700)
/** Dunno what this is, but required for HV_MAP_GPA_DUNNO_ACCESS. */
#define HV_MAP_GPA_DUNNO_1000           UINT32_C(0x1000)
/** Working with large 2MB pages. */
#define HV_MAP_GPA_LARGE                UINT32_C(0x2000)
/** Valid mask as per build 17101. */
#define HV_MAP_GPA_VALID_MASK           UINT32_C(0x7f1f)
/** @}  */

/** Input for HvCallMapGpaPages. */
typedef struct
{
    HV_PARTITION_ID     TargetPartitionId;
    HV_GPA_PAGE_NUMBER  TargetGpaBase;
    HV_MAP_GPA_FLAGS    MapFlags;
    uint32_t            u32ExplicitPadding;
    /* The repeating part: */
    RT_FLEXIBLE_ARRAY_EXTENSION
    HV_SPA_PAGE_NUMBER  PageList[RT_FLEXIBLE_ARRAY];
} HV_INPUT_MAP_GPA_PAGES;
AssertCompileMemberOffset(HV_INPUT_MAP_GPA_PAGES, PageList, 24);
/** Pointer to the input for HvCallMapGpaPages. */
typedef HV_INPUT_MAP_GPA_PAGES *PHV_INPUT_MAP_GPA_PAGES;


/** A parent to guest mapping pair for HvCallMapSparseGpaPages. */
typedef struct
{
    HV_GPA_PAGE_NUMBER TargetGpaPageNumber;
    HV_SPA_PAGE_NUMBER SourceSpaPageNumber;
} HV_GPA_MAPPING;
/** Pointer to a parent->guest mapping pair for HvCallMapSparseGpaPages. */
typedef HV_GPA_MAPPING *PHV_GPA_MAPPING;

/** Input for HvCallMapSparseGpaPages. */
typedef struct
{
    HV_PARTITION_ID     TargetPartitionId;
    HV_MAP_GPA_FLAGS    MapFlags;
    uint32_t            u32ExplicitPadding;
    /* The repeating part: */
    RT_FLEXIBLE_ARRAY_EXTENSION
    HV_GPA_MAPPING      PageList[RT_FLEXIBLE_ARRAY];
} HV_INPUT_MAP_SPARSE_GPA_PAGES;
AssertCompileMemberOffset(HV_INPUT_MAP_SPARSE_GPA_PAGES, PageList, 16);
/** Pointer to the input for HvCallMapSparseGpaPages. */
typedef HV_INPUT_MAP_SPARSE_GPA_PAGES *PHV_INPUT_MAP_SPARSE_GPA_PAGES;


/** Input for HvCallUnmapGpaPages. */
typedef struct
{
    HV_PARTITION_ID     TargetPartitionId;
    HV_GPA_PAGE_NUMBER  TargetGpaBase;
    /** This field is either an omission in the 7600 WDK or a later additions.
     *  Anyway, not quite sure what it does.  Bit 2 seems to indicate 2MB pages. */
    uint64_t            fFlags;
} HV_INPUT_UNMAP_GPA_PAGES;
AssertCompileSize(HV_INPUT_UNMAP_GPA_PAGES, 24);
/** Pointer to the input for HvCallUnmapGpaPages. */
typedef HV_INPUT_UNMAP_GPA_PAGES *PHV_INPUT_UNMAP_GPA_PAGES;



/** Cache types used by HvCallReadGpa and HvCallWriteGpa. */
typedef enum
{
    HvCacheTypeX64Uncached = 0,
    HvCacheTypeX64WriteCombining,
    /* 2 & 3 are undefined. */
    HvCacheTypeX64WriteThrough = 4,
    HvCacheTypeX64WriteProtected,
    HvCacheTypeX64WriteBack
} HV_CACHE_TYPE;

/** Control flags for HvCallReadGpa and HvCallWriteGpa. */
typedef union
{
    uint64_t            AsUINT64;
    struct
    {
        uint64_t        CacheType : 8;      /**< HV_CACHE_TYPE */
#ifndef IN_IDA_PRO
        uint64_t        Reserved  : 56;
#endif
    };
} HV_ACCESS_GPA_CONTROL_FLAGS;

/** Results codes for HvCallReadGpa and HvCallWriteGpa. */
typedef enum
{
    HvAccessGpaSuccess = 0,
    HvAccessGpaUnmapped,
    HvAccessGpaReadIntercept,
    HvAccessGpaWriteIntercept,
    HvAccessGpaIllegalOverlayAccess
} HV_ACCESS_GPA_RESULT_CODE;

/** The result of HvCallReadGpa and HvCallWriteGpa. */
typedef union
{
    uint64_t                        AsUINT64;
    struct
    {
        HV_ACCESS_GPA_RESULT_CODE   ResultCode;
        uint32_t                    Reserved;
    };
} HV_ACCESS_GPA_RESULT;


/** Input for HvCallReadGpa. */
typedef struct
{
    HV_PARTITION_ID             PartitionId;
    HV_VP_INDEX                 VpIndex;
    uint32_t                    ByteCount;
    HV_GPA                      BaseGpa;
    HV_ACCESS_GPA_CONTROL_FLAGS ControlFlags;
} HV_INPUT_READ_GPA;
AssertCompileSize(HV_INPUT_READ_GPA, 32);
/** Pointer to the input for HvCallReadGpa. */
typedef HV_INPUT_READ_GPA *PHV_INPUT_READ_GPA;

/** Output for HvCallReadGpa. */
typedef struct
{
    HV_ACCESS_GPA_RESULT        AccessResult;
    uint8_t                     Data[16];
} HV_OUTPUT_READ_GPA;
AssertCompileSize(HV_OUTPUT_READ_GPA, 24);
/** Pointer to the output for HvCallReadGpa. */
typedef HV_OUTPUT_READ_GPA *PHV_OUTPUT_READ_GPA;


/** Input for HvCallWriteGpa. */
typedef struct
{
    HV_PARTITION_ID             PartitionId;
    HV_VP_INDEX                 VpIndex;
    uint32_t                    ByteCount;
    HV_GPA                      BaseGpa;
    HV_ACCESS_GPA_CONTROL_FLAGS ControlFlags;
    uint8_t                     Data[16];
} HV_INPUT_WRITE_GPA;
AssertCompileSize(HV_INPUT_READ_GPA, 32);
/** Pointer to the input for HvCallWriteGpa. */
typedef HV_INPUT_READ_GPA *PHV_INPUT_READ_GPA;

/** Output for HvCallWriteGpa. */
typedef struct
{
    HV_ACCESS_GPA_RESULT        AccessResult;
} HV_OUTPUT_WRITE_GPA;
AssertCompileSize(HV_OUTPUT_WRITE_GPA, 8);
/** Pointer to the output for HvCallWriteGpa. */
typedef HV_OUTPUT_WRITE_GPA *PHV_OUTPUT_WRITE_GPA;


/**
 * Register names used by HvCallGetVpRegisters and HvCallSetVpRegisters.
 */
typedef enum _HV_REGISTER_NAME
{
    HvRegisterExplicitSuspend = 0x00000000,
    HvRegisterInterceptSuspend,
    HvRegisterUnknown02,                                /**< Reads as 0 initially on exo part. */
    HvRegisterUnknown03,                                /**< Reads as 0 initially on exo part. */
    HvRegisterInternalActivityState,                    /**< @since about build 17758 */

    HvRegisterHypervisorVersion = 0x00000100,           /**< @since v5 @note Not readable on exo part. */

    HvRegisterPrivilegesAndFeaturesInfo = 0x00000200,   /**< @since v5 @note Not readable on exo part. */
    HvRegisterFeaturesInfo,                             /**< @since v5 @note Not readable on exo part. */
    HvRegisterImplementationLimitsInfo,                 /**< @since v5 @note Not readable on exo part. */
    HvRegisterHardwareFeaturesInfo,                     /**< @since v5 @note Not readable on exo part. */

    HvRegisterGuestCrashP0 = 0x00000210,                /**< @since v5 @note Not readable on exo part. */
    HvRegisterGuestCrashP1,                             /**< @since v5 @note Not readable on exo part. */
    HvRegisterGuestCrashP2,                             /**< @since v5 @note Not readable on exo part. */
    HvRegisterGuestCrashP3,                             /**< @since v5 @note Not readable on exo part. */
    HvRegisterGuestCrashP4,                             /**< @since v5 @note Not readable on exo part. */
    HvRegisterGuestCrashCtl,                            /**< @since v5 @note Not readable on exo part. */

    HvRegisterPowerStateConfigC1 = 0x00000220,          /**< @since v5 @note Not readable on exo part. */
    HvRegisterPowerStateTriggerC1,                      /**< @since v5 @note Not readable on exo part. */
    HvRegisterPowerStateConfigC2,                       /**< @since v5 @note Not readable on exo part. */
    HvRegisterPowerStateTriggerC2,                      /**< @since v5 @note Not readable on exo part. */
    HvRegisterPowerStateConfigC3,                       /**< @since v5 @note Not readable on exo part. */
    HvRegisterPowerStateTriggerC3,                      /**< @since v5 @note Not readable on exo part. */

    HvRegisterSystemReset = 0x00000230,                 /**< @since v5 @note Not readable on exo part. */

    HvRegisterProcessorClockFrequency = 0x00000240,     /**< @since v5 @note Not readable on exo part. */
    HvRegisterInterruptClockFrequency,                  /**< @since v5 @note Not readable on exo part. */

    HvRegisterGuestIdle = 0x00000250,                   /**< @since v5 @note Not readable on exo part. */

    HvRegisterDebugDeviceOptions = 0x00000260,          /**< @since v5 @note Not readable on exo part. */

    HvRegisterPendingInterruption = 0x00010002,
    HvRegisterInterruptState,
    HvRegisterPendingEvent0,                            /**< @since v5 */
    HvRegisterPendingEvent1,                            /**< @since v5 */
    HvX64RegisterDeliverabilityNotifications,           /**< @since v5c? Late 2017? */

    HvX64RegisterRax = 0x00020000,
    HvX64RegisterRcx,
    HvX64RegisterRdx,
    HvX64RegisterRbx,
    HvX64RegisterRsp,
    HvX64RegisterRbp,
    HvX64RegisterRsi,
    HvX64RegisterRdi,
    HvX64RegisterR8,
    HvX64RegisterR9,
    HvX64RegisterR10,
    HvX64RegisterR11,
    HvX64RegisterR12,
    HvX64RegisterR13,
    HvX64RegisterR14,
    HvX64RegisterR15,
    HvX64RegisterRip,
    HvX64RegisterRflags,

    HvX64RegisterXmm0 = 0x00030000,
    HvX64RegisterXmm1,
    HvX64RegisterXmm2,
    HvX64RegisterXmm3,
    HvX64RegisterXmm4,
    HvX64RegisterXmm5,
    HvX64RegisterXmm6,
    HvX64RegisterXmm7,
    HvX64RegisterXmm8,
    HvX64RegisterXmm9,
    HvX64RegisterXmm10,
    HvX64RegisterXmm11,
    HvX64RegisterXmm12,
    HvX64RegisterXmm13,
    HvX64RegisterXmm14,
    HvX64RegisterXmm15,
    HvX64RegisterFpMmx0,
    HvX64RegisterFpMmx1,
    HvX64RegisterFpMmx2,
    HvX64RegisterFpMmx3,
    HvX64RegisterFpMmx4,
    HvX64RegisterFpMmx5,
    HvX64RegisterFpMmx6,
    HvX64RegisterFpMmx7,
    HvX64RegisterFpControlStatus,
    HvX64RegisterXmmControlStatus,

    HvX64RegisterCr0 = 0x00040000,
    HvX64RegisterCr2,
    HvX64RegisterCr3,
    HvX64RegisterCr4,
    HvX64RegisterCr8,
    HvX64RegisterXfem,

    HvX64RegisterIntermediateCr0 = 0x00041000,          /**< @since v5 */
    HvX64RegisterIntermediateCr4 = 0x00041003,          /**< @since v5 */
    HvX64RegisterIntermediateCr8,                       /**< @since v5 */

    HvX64RegisterDr0 = 0x00050000,
    HvX64RegisterDr1,
    HvX64RegisterDr2,
    HvX64RegisterDr3,
    HvX64RegisterDr6,
    HvX64RegisterDr7,

    HvX64RegisterEs = 0x00060000,
    HvX64RegisterCs,
    HvX64RegisterSs,
    HvX64RegisterDs,
    HvX64RegisterFs,
    HvX64RegisterGs,
    HvX64RegisterLdtr,
    HvX64RegisterTr,

    HvX64RegisterIdtr = 0x00070000,
    HvX64RegisterGdtr,

    HvX64RegisterTsc = 0x00080000,
    HvX64RegisterEfer,
    HvX64RegisterKernelGsBase,
    HvX64RegisterApicBase,
    HvX64RegisterPat,
    HvX64RegisterSysenterCs,
    HvX64RegisterSysenterEip,
    HvX64RegisterSysenterEsp,
    HvX64RegisterStar,
    HvX64RegisterLstar,
    HvX64RegisterCstar,
    HvX64RegisterSfmask,
    HvX64RegisterInitialApicId,

    HvX64RegisterMtrrCap,                           /**< Not readable in exo partitions? */
    HvX64RegisterMtrrDefType,

    HvX64RegisterMtrrPhysBase0 = 0x00080010,
    HvX64RegisterMtrrPhysBase1,
    HvX64RegisterMtrrPhysBase2,
    HvX64RegisterMtrrPhysBase3,
    HvX64RegisterMtrrPhysBase4,
    HvX64RegisterMtrrPhysBase5,
    HvX64RegisterMtrrPhysBase6,
    HvX64RegisterMtrrPhysBase7,
    HvX64RegisterMtrrPhysBase8,                     /**< @since v4 @note Appears not to be readable on exo partition (Threadripper). */
    HvX64RegisterMtrrPhysBase9,                     /**< @since v4 @note Appears not to be readable on exo partition (Threadripper). */
    HvX64RegisterMtrrPhysBaseA,                     /**< @since v4 @note Appears not to be readable on exo partition (Threadripper). */
    HvX64RegisterMtrrPhysBaseB,                     /**< @since v4 @note Appears not to be readable on exo partition (Threadripper). */
    HvX64RegisterMtrrPhysBaseC,                     /**< @since v4 @note Appears not to be readable on exo partition (Threadripper). */
    HvX64RegisterMtrrPhysBaseD,                     /**< @since v4 @note Appears not to be readable on exo partition (Threadripper). */
    HvX64RegisterMtrrPhysBaseE,                     /**< @since v4 @note Appears not to be readable on exo partition (Threadripper). */
    HvX64RegisterMtrrPhysBaseF,                     /**< @since v4 @note Appears not to be readable on exo partition (Threadripper). */

    HvX64RegisterMtrrPhysMask0 = 0x00080040,
    HvX64RegisterMtrrPhysMask1,
    HvX64RegisterMtrrPhysMask2,
    HvX64RegisterMtrrPhysMask3,
    HvX64RegisterMtrrPhysMask4,
    HvX64RegisterMtrrPhysMask5,
    HvX64RegisterMtrrPhysMask6,
    HvX64RegisterMtrrPhysMask7,
    HvX64RegisterMtrrPhysMask8,                     /**< @since v4 @note Appears not to be readable on exo partition (Threadripper). */
    HvX64RegisterMtrrPhysMask9,                     /**< @since v4 @note Appears not to be readable on exo partition (Threadripper). */
    HvX64RegisterMtrrPhysMaskA,                     /**< @since v4 @note Appears not to be readable on exo partition (Threadripper). */
    HvX64RegisterMtrrPhysMaskB,                     /**< @since v4 @note Appears not to be readable on exo partition (Threadripper). */
    HvX64RegisterMtrrPhysMaskC,                     /**< @since v4 @note Appears not to be readable on exo partition (Threadripper). */
    HvX64RegisterMtrrPhysMaskD,                     /**< @since v4 @note Appears not to be readable on exo partition (Threadripper). */
    HvX64RegisterMtrrPhysMaskE,                     /**< @since v4 @note Appears not to be readable on exo partition (Threadripper). */
    HvX64RegisterMtrrPhysMaskF,                     /**< @since v4 @note Appears not to be readable on exo partition (Threadripper). */

    HvX64RegisterMtrrFix64k00000 = 0x00080070,
    HvX64RegisterMtrrFix16k80000,
    HvX64RegisterMtrrFix16kA0000,
    HvX64RegisterMtrrFix4kC0000,
    HvX64RegisterMtrrFix4kC8000,
    HvX64RegisterMtrrFix4kD0000,
    HvX64RegisterMtrrFix4kD8000,
    HvX64RegisterMtrrFix4kE0000,
    HvX64RegisterMtrrFix4kE8000,
    HvX64RegisterMtrrFix4kF0000,
    HvX64RegisterMtrrFix4kF8000,
    HvX64RegisterTscAux,                            /**< @since v5c? late 2017? */

    HvX64RegisterUnknown8007d = 0x0008007d,         /**< Readable on exo partition (17134), initial value is zero. */

    HvX64RegisterSpecCtrl = 0x00080084,             /**< @since build about 17758 */
    HvX64RegisterPredCmd,                           /**< @since build about 17758 */

    HvX64RegisterIa32MiscEnable = 0x000800a0,       /**< @since v5 @note Appears not to be readable on exo partition (Threadripper). */
    HvX64RegisterIa32FeatureControl,                /**< @since v5 @note Appears not to be readable on exo partition (Threadripper). */

    HvX64RegisterApicId = 0x00084802,               /**< @since build 17758 */
    HvX64RegisterApicVersion,                       /**< @since build 17758 */

    /** Uptime counter or some such thing.  Unit is different than HvRegisterTimeRefCount or the accounting is different. */
    HvX64RegisterVpRuntime = 0x00090000,
    HvX64RegisterHypercall,
    HvRegisterGuestOsId,
    HvRegisterVpIndex,
    HvRegisterTimeRefCount,                         /**< Time counter since partition creation, 100ns units. */

    HvRegisterCpuManagementVersion = 0x00090007,    /**< @since v5 @note Appears not to be readable on exo partition. */

    HvX64RegisterEoi = 0x00090010,                  /**< @note Appears not to be readable on exo partition. */
    HvX64RegisterIcr,                               /**< @note Appears not to be readable on exo partition. */
    HvX64RegisterTpr,                               /**< @note Appears not to be readable on exo partition. */
    HvRegisterVpAssistPage,
    /** Readable on exo partition (17134). Some kind of counter. */
    HvRegisterUnknown90014,

    HvRegisterStatsPartitionRetail = 0x00090020,
    HvRegisterStatsPartitionInternal,
    HvRegisterStatsVpRetail,
    HvRegisterStatsVpInternal,

    HvRegisterSint0 = 0x000a0000,
    HvRegisterSint1,
    HvRegisterSint2,
    HvRegisterSint3,
    HvRegisterSint4,
    HvRegisterSint5,
    HvRegisterSint6,
    HvRegisterSint7,
    HvRegisterSint8,
    HvRegisterSint9,
    HvRegisterSint10,
    HvRegisterSint11,
    HvRegisterSint12,
    HvRegisterSint13,
    HvRegisterSint14,
    HvRegisterSint15,
    HvRegisterScontrol,
    HvRegisterSversion,
    HvRegisterSifp,
    HvRegisterSipp,
    HvRegisterEom,
    HvRegisterSirbp,                                /**< @since v4 */

    HvRegisterStimer0Config = 0x000b0000,
    HvRegisterStimer0Count,
    HvRegisterStimer1Config,
    HvRegisterStimer1Count,
    HvRegisterStimer2Config,
    HvRegisterStimer2Count,
    HvRegisterStimer3Config,
    HvRegisterStimer3Count,

    HvRegisterUnknown0b0100 = 0x000b0100,           /**< Readable on exo partition (17134), initial value is zero. */
    HvRegisterUnknown0b0101,                        /**< Readable on exo partition (17134), initial value is zero. */

    HvX64RegisterYmm0Low = 0x000c0000,              /**< @note Not readable on exo partition.  Need something enabled? */
    HvX64RegisterYmm1Low,
    HvX64RegisterYmm2Low,
    HvX64RegisterYmm3Low,
    HvX64RegisterYmm4Low,
    HvX64RegisterYmm5Low,
    HvX64RegisterYmm6Low,
    HvX64RegisterYmm7Low,
    HvX64RegisterYmm8Low,
    HvX64RegisterYmm9Low,
    HvX64RegisterYmm10Low,
    HvX64RegisterYmm11Low,
    HvX64RegisterYmm12Low,
    HvX64RegisterYmm13Low,
    HvX64RegisterYmm14Low,
    HvX64RegisterYmm15Low,
    HvX64RegisterYmm0High,
    HvX64RegisterYmm1High,
    HvX64RegisterYmm2High,
    HvX64RegisterYmm3High,
    HvX64RegisterYmm4High,
    HvX64RegisterYmm5High,
    HvX64RegisterYmm6High,
    HvX64RegisterYmm7High,
    HvX64RegisterYmm8High,
    HvX64RegisterYmm9High,
    HvX64RegisterYmm10High,
    HvX64RegisterYmm11High,
    HvX64RegisterYmm12High,
    HvX64RegisterYmm13High,
    HvX64RegisterYmm14High,
    HvX64RegisterYmm15High,

    HvRegisterVsmVpVtlControl = 0x000d0000,         /**< @note Not readable on exo partition. */

    HvRegisterVsmCodePageOffsets = 0x000d0002,
    HvRegisterVsmVpStatus,
    HvRegisterVsmPartitionStatus,
    HvRegisterVsmVina,                              /**< @note Not readable on exo partition. */
    HvRegisterVsmCapabilities,
    HvRegisterVsmPartitionConfig,                   /**< @note Not readable on exo partition. */

    HvRegisterVsmVpSecureConfigVtl0 = 0x000d0010,   /**< @since v5 */
    HvRegisterVsmVpSecureConfigVtl1,                /**< @since v5 */
    HvRegisterVsmVpSecureConfigVtl2,                /**< @since v5 */
    HvRegisterVsmVpSecureConfigVtl3,                /**< @since v5 */
    HvRegisterVsmVpSecureConfigVtl4,                /**< @since v5 */
    HvRegisterVsmVpSecureConfigVtl5,                /**< @since v5 */
    HvRegisterVsmVpSecureConfigVtl6,                /**< @since v5 */
    HvRegisterVsmVpSecureConfigVtl7,                /**< @since v5 */
    HvRegisterVsmVpSecureConfigVtl8,                /**< @since v5 */
    HvRegisterVsmVpSecureConfigVtl9,                /**< @since v5 */
    HvRegisterVsmVpSecureConfigVtl10,               /**< @since v5 */
    HvRegisterVsmVpSecureConfigVtl11,               /**< @since v5 */
    HvRegisterVsmVpSecureConfigVtl12,               /**< @since v5 */
    HvRegisterVsmVpSecureConfigVtl13,               /**< @since v5 */
    HvRegisterVsmVpSecureConfigVtl14,               /**< @since v5 */

    HvRegisterUnknown0e0000 = 0x000e0000,           /**< Readable on exo partition (17134), initial value zero. */
    HvRegisterUnknown0e0001,                        /**< Readable on exo partition (17134), initial value zero. */
    HvRegisterUnknown0e0002,                        /**< Readable on exo partition (17134), initial value zero. */
    HvRegisterUnknown0e0003                         /**< Readable on exo partition (17134), initial value zero. */
} HV_REGISTER_NAME;
AssertCompile(HvRegisterInterceptSuspend == 0x00000001);
AssertCompile(HvRegisterPendingEvent1 == 0x00010005);
AssertCompile(HvX64RegisterDeliverabilityNotifications == 0x00010006);
AssertCompile(HvX64RegisterRflags == 0x00020011);
AssertCompile(HvX64RegisterXmmControlStatus == 0x00030019);
AssertCompile(HvX64RegisterXfem == 0x00040005);
AssertCompile(HvX64RegisterIntermediateCr0 == 0x00041000);
AssertCompile(HvX64RegisterIntermediateCr4 == 0x00041003);
AssertCompile(HvX64RegisterDr7 == 0x00050005);
AssertCompile(HvX64RegisterTr == 0x00060007);
AssertCompile(HvX64RegisterGdtr == 0x00070001);
AssertCompile(HvX64RegisterInitialApicId == 0x0008000c);
AssertCompile(HvX64RegisterMtrrCap == 0x0008000d);
AssertCompile(HvX64RegisterMtrrDefType == 0x0008000e);
AssertCompile(HvX64RegisterMtrrPhysBaseF == 0x0008001f);
AssertCompile(HvX64RegisterMtrrPhysMaskF == 0x0008004f);
AssertCompile(HvX64RegisterMtrrFix4kF8000 == 0x0008007a);
AssertCompile(HvRegisterTimeRefCount == 0x00090004);
AssertCompile(HvRegisterCpuManagementVersion == 0x00090007);
AssertCompile(HvRegisterVpAssistPage == 0x00090013);
AssertCompile(HvRegisterStatsVpInternal == 0x00090023);
AssertCompile(HvRegisterSirbp == 0x000a0015);
AssertCompile(HvRegisterStimer3Count == 0x000b0007);
AssertCompile(HvX64RegisterYmm15High == 0x000c001f);
AssertCompile(HvRegisterVsmVpSecureConfigVtl14 == 0x000d001e);
AssertCompileSize(HV_REGISTER_NAME, 4);


/** Value format for HvRegisterExplicitSuspend. */
typedef union
{
    uint64_t            AsUINT64;
    struct
    {
        uint64_t        Suspended : 1;
#ifndef IN_IDA_PRO
        uint64_t        Reserved  : 63;
#endif
    };
} HV_EXPLICIT_SUSPEND_REGISTER;
/** Pointer to a value of HvRegisterExplicitSuspend. */
typedef HV_EXPLICIT_SUSPEND_REGISTER *PHV_EXPLICIT_SUSPEND_REGISTER;

/** Value format for HvRegisterInterceptSuspend. */
typedef union
{
    uint64_t            AsUINT64;
    struct
    {
        uint64_t        Suspended : 1;
        uint64_t        TlbLocked : 1;
#ifndef IN_IDA_PRO
        uint64_t        Reserved  : 62;
#endif
    };
} HV_INTERCEPT_SUSPEND_REGISTER;
/** Pointer to a value of HvRegisterInterceptSuspend. */
typedef HV_INTERCEPT_SUSPEND_REGISTER *PHV_INTERCEPT_SUSPEND_REGISTER;

/** Value format for HvRegisterInterruptState.
 * @sa WHV_X64_INTERRUPT_STATE_REGISTER */
typedef union
{
    uint64_t            AsUINT64;
    struct
    {
        uint64_t        InterruptShadow : 1;
        uint64_t        NmiMasked       : 1;
#ifndef IN_IDA_PRO
        uint64_t        Reserved        : 62;
#endif
    };
} HV_X64_INTERRUPT_STATE_REGISTER;
/** Pointer to a value of HvRegisterInterruptState. */
typedef HV_X64_INTERRUPT_STATE_REGISTER *PHV_X64_INTERRUPT_STATE_REGISTER;

/** Pending exception type for HvRegisterPendingInterruption.
 * @sa WHV_X64_PENDING_INTERRUPTION_TYPE */
typedef enum
{
    HvX64PendingInterrupt = 0,
    /* what is/was 1? */
    HvX64PendingNmi = 2,
    HvX64PendingException
    /* any more? */
} HV_X64_PENDING_INTERRUPTION_TYPE;

/** Value format for HvRegisterPendingInterruption.
 * @sa WHV_X64_PENDING_INTERRUPTION_REGISTER  */
typedef union
{
    uint64_t            AsUINT64;
    struct
    {
        uint32_t        InterruptionPending : 1;
        uint32_t        InterruptionType    : 3;    /**< HV_X64_PENDING_INTERRUPTION_TYPE */
        uint32_t        DeliverErrorCode    : 1;
        uint32_t        InstructionLength   : 4;    /**< @since v5? Wasn't in 7600 WDK */
        uint32_t        NestedEvent         : 1;    /**< @since v5? Wasn't in 7600 WDK */
        uint32_t        Reserved            : 6;
        uint32_t        InterruptionVector  : 16;
        uint32_t        ErrorCode;
    };
} HV_X64_PENDING_INTERRUPTION_REGISTER;
/** Pointer to a value of HvRegisterPendingInterruption. */
typedef HV_X64_PENDING_INTERRUPTION_REGISTER *PHV_X64_PENDING_INTERRUPTION_REGISTER;

/** Value format for HvX64RegisterDeliverabilityNotifications.
 *  Value format for HvRegisterPendingEvent0/1.
 * @sa WHV_X64_DELIVERABILITY_NOTIFICATIONS_REGISTER  */
typedef union
{
    uint64_t            AsUINT64;
    struct
    {
        uint64_t        NmiNotification         : 1;
        uint64_t        InterruptNotification   : 1;
        uint64_t        InterruptPriority       : 4;
#ifndef IN_IDA_PRO
        uint64_t        Reserved                : 58;
#endif
    };
} HV_X64_DELIVERABILITY_NOTIFICATIONS_REGISTER;
/** Pointer to a value of HvRegisterPendingEvent0/1. */
typedef HV_X64_DELIVERABILITY_NOTIFICATIONS_REGISTER *PHV_X64_DELIVERABILITY_NOTIFICATIONS_REGISTER;


/** Value format for HvX64RegisterEs..Tr.
 * @sa WHV_X64_SEGMENT_REGISTER  */
typedef struct
{
    uint64_t            Base;
    uint32_t            Limit;
    uint16_t            Selector;
    union
    {
        struct
        {
            uint16_t    SegmentType              : 4;
            uint16_t    NonSystemSegment         : 1;
            uint16_t    DescriptorPrivilegeLevel : 2;
            uint16_t    Present                  : 1;
            uint16_t    Reserved                 : 4;
            uint16_t    Available                : 1;
            uint16_t    Long                     : 1;
            uint16_t    Default                  : 1;
            uint16_t    Granularity              : 1;
        };
        uint16_t        Attributes;
    };
} HV_X64_SEGMENT_REGISTER;
AssertCompileSize(HV_X64_SEGMENT_REGISTER, 16);
/** Pointer to a value of HvX64RegisterEs..Tr. */
typedef HV_X64_SEGMENT_REGISTER *PHV_X64_SEGMENT_REGISTER;

/** Value format for HvX64RegisterIdtr/Gdtr.
 * @sa WHV_X64_TABLE_REGISTER */
typedef struct
{
    uint16_t            Pad[3];
    uint16_t            Limit;
    uint64_t            Base;
} HV_X64_TABLE_REGISTER;
AssertCompileSize(HV_X64_TABLE_REGISTER, 16);
/** Pointer to a value of HvX64RegisterIdtr/Gdtrr. */
typedef HV_X64_TABLE_REGISTER *PHV_X64_TABLE_REGISTER;

/** Value format for HvX64RegisterFpMmx0..7 in floating pointer mode.
 * @sa WHV_X64_FP_REGISTER, RTFLOAT80U2 */
typedef union
{
    HV_UINT128          AsUINT128;
    struct
    {
        uint64_t        Mantissa;
        uint64_t        BiasedExponent  : 15;
        uint64_t        Sign            : 1;
#ifndef IN_IDA_PRO
        uint64_t        Reserved        : 48;
#endif
    };
} HV_X64_FP_REGISTER;
/** Pointer to a value of HvX64RegisterFpMmx0..7 in floating point mode. */
typedef HV_X64_FP_REGISTER *PHV_X64_FP_REGISTER;

/** Value union for HvX64RegisterFpMmx0..7. */
typedef union
{
    HV_UINT128          AsUINT128;
    HV_X64_FP_REGISTER  Fp;
    uint64_t            Mmx;
} HV_X64_FP_MMX_REGISTER;
/** Pointer to a value of HvX64RegisterFpMmx0..7. */
typedef HV_X64_FP_MMX_REGISTER *PHV_X64_FP_MMX_REGISTER;

/** Value format for HvX64RegisterFpControlStatus.
 * @sa WHV_X64_FP_CONTROL_STATUS_REGISTER  */
typedef union
{
    HV_UINT128              AsUINT128;
    struct
    {
        uint16_t            FpControl;
        uint16_t            FpStatus;
        uint8_t             FpTag;
        uint8_t             IgnNe    : 1;
        uint8_t             Reserved : 7;
        uint16_t            LastFpOp;
        union
        {
            uint64_t        LastFpRip;
            struct
            {
                uint32_t    LastFpEip;
                uint16_t    LastFpCs;
            };
        };
    };
} HV_X64_FP_CONTROL_STATUS_REGISTER;
/** Pointer to a value of HvX64RegisterFpControlStatus. */
typedef HV_X64_FP_CONTROL_STATUS_REGISTER *PHV_X64_FP_CONTROL_STATUS_REGISTER;

/** Value format for HvX64RegisterXmmControlStatus.
 * @sa WHV_X64_XMM_CONTROL_STATUS_REGISTER  */
typedef union
{
    HV_UINT128 AsUINT128;
    struct
    {
        union
        {
            uint64_t        LastFpRdp;
            struct
            {
                uint32_t    LastFpDp;
                uint16_t    LastFpDs;
            };
        };
        uint32_t            XmmStatusControl;
        uint32_t            XmmStatusControlMask;
    };
} HV_X64_XMM_CONTROL_STATUS_REGISTER;
/** Pointer to a value of HvX64RegisterXmmControlStatus. */
typedef HV_X64_XMM_CONTROL_STATUS_REGISTER *PHV_X64_XMM_CONTROL_STATUS_REGISTER;

/** Register value union.
 * @sa WHV_REGISTER_VALUE  */
typedef union
{
    HV_UINT128                                      Reg128;
    uint64_t                                        Reg64;
    uint32_t                                        Reg32;
    uint16_t                                        Reg16;
    uint8_t                                         Reg8;
    HV_EXPLICIT_SUSPEND_REGISTER                    ExplicitSuspend;
    HV_INTERCEPT_SUSPEND_REGISTER                   InterceptSuspend;
    HV_X64_INTERRUPT_STATE_REGISTER                 InterruptState;
    HV_X64_PENDING_INTERRUPTION_REGISTER            PendingInterruption;
    HV_X64_DELIVERABILITY_NOTIFICATIONS_REGISTER    DeliverabilityNotifications;
    HV_X64_TABLE_REGISTER                           Table;
    HV_X64_SEGMENT_REGISTER                         Segment;
    HV_X64_FP_REGISTER                              Fp;
    HV_X64_FP_CONTROL_STATUS_REGISTER               FpControlStatus;
    HV_X64_XMM_CONTROL_STATUS_REGISTER              XmmControlStatus;
} HV_REGISTER_VALUE;
AssertCompileSize(HV_REGISTER_VALUE, 16);
/** Pointer to a Hyper-V register value union. */
typedef HV_REGISTER_VALUE *PHV_REGISTER_VALUE;
/** Pointer to a const Hyper-V register value union. */
typedef HV_REGISTER_VALUE const *PCHV_REGISTER_VALUE;


/** Input for HvCallGetVpRegisters. */
typedef struct
{
    HV_PARTITION_ID     PartitionId;
    HV_VP_INDEX         VpIndex;
    /** Was this introduced after v2? Dunno what it it really is. */
    uint32_t            fFlags;
    /* The repeating part: */
    RT_FLEXIBLE_ARRAY_EXTENSION
    HV_REGISTER_NAME    Names[RT_FLEXIBLE_ARRAY];
} HV_INPUT_GET_VP_REGISTERS;
AssertCompileMemberOffset(HV_INPUT_GET_VP_REGISTERS, Names, 16);
/** Pointer to input for HvCallGetVpRegisters. */
typedef HV_INPUT_GET_VP_REGISTERS *PHV_INPUT_GET_VP_REGISTERS;
/* Output for HvCallGetVpRegisters is an array of HV_REGISTER_VALUE parallel to HV_INPUT_GET_VP_REGISTERS::Names. */


/** Register and value pair for HvCallSetVpRegisters. */
typedef struct
{
    HV_REGISTER_NAME    Name;
    uint32_t            Pad0;
    uint64_t            Pad1;
    HV_REGISTER_VALUE   Value;
} HV_REGISTER_ASSOC;
AssertCompileSize(HV_REGISTER_ASSOC, 32);
AssertCompileMemberOffset(HV_REGISTER_ASSOC, Value, 16);
/** Pointer to a register and value pair for HvCallSetVpRegisters. */
typedef HV_REGISTER_ASSOC *PHV_REGISTER_ASSOC;
/** Helper for clearing the alignment padding members. */
#define HV_REGISTER_ASSOC_ZERO_PADDING(a_pRegAssoc) do { (a_pRegAssoc)->Pad0 = 0; (a_pRegAssoc)->Pad1 = 0; } while (0)
/** Helper for clearing the alignment padding members and the high 64-bit
 * part of the value. */
#define HV_REGISTER_ASSOC_ZERO_PADDING_AND_HI64(a_pRegAssoc) \
    do { (a_pRegAssoc)->Pad0 = 0; (a_pRegAssoc)->Pad1 = 0; (a_pRegAssoc)->Value.Reg128.High64 = 0; } while (0)

/** Input for HvCallSetVpRegisters. */
typedef struct
{
    HV_PARTITION_ID     PartitionId;
    HV_VP_INDEX         VpIndex;
    uint32_t            RsvdZ;
    /* The repeating part: */
    RT_FLEXIBLE_ARRAY_EXTENSION
    HV_REGISTER_ASSOC   Elements[RT_FLEXIBLE_ARRAY];
} HV_INPUT_SET_VP_REGISTERS;
AssertCompileMemberOffset(HV_INPUT_SET_VP_REGISTERS, Elements, 16);
/** Pointer to input for HvCallSetVpRegisters. */
typedef HV_INPUT_SET_VP_REGISTERS *PHV_INPUT_SET_VP_REGISTERS;



/**
 * Hyper-V SyncIC message types.
 */
typedef enum
{
    HvMessageTypeNone = 0x00000000,

    HvMessageTypeUnmappedGpa = 0x80000000,
    HvMessageTypeGpaIntercept,

    HvMessageTimerExpired = 0x80000010,

    HvMessageTypeInvalidVpRegisterValue = 0x80000020,
    HvMessageTypeUnrecoverableException,
    HvMessageTypeUnsupportedFeature,
    HvMessageTypeTlbPageSizeMismatch,                   /**< @since v5 */

    /** @note Same as HvMessageTypeX64ApicEoi? Gone in 5.0.  Missing from 7600 WDK
     *        headers even if it's in the 2.0 docs.  */
    HvMessageTypeApicEoi = 0x80000030,
    /** @note Same as HvMessageTypeX64LegacyFpError? Gone in 5.0, whereas 4.0b
     *        calls it HvMessageTypeX64LegacyFpError.  Missing from 7600 WDK
     *        headers even if it's in the 2.0 docs. */
    HvMessageTypeFerrAsserted,

    HvMessageTypeEventLogBufferComplete = 0x80000040,

    HvMessageTypeX64IoPortIntercept = 0x80010000,
    HvMessageTypeX64MsrIntercept,
    HvMessageTypeX64CpuidIntercept,
    HvMessageTypeX64ExceptionIntercept,
    /** @note Appeared in 5.0 docs, but were here in 7600 WDK headers already. */
    HvMessageTypeX64ApicEoi,
    /** @note Appeared in 5.0 docs, but were here in 7600 WDK headers already. */
    HvMessageTypeX64LegacyFpError,
    /** @since v5   */
    HvMessageTypeX64RegisterIntercept,
    /** @since WinHvPlatform? */
    HvMessageTypeX64Halt,
    /** @since WinHvPlatform? */
    HvMessageTypeX64InterruptWindow

} HV_MESSAGE_TYPE;
AssertCompileSize(HV_MESSAGE_TYPE, 4);
AssertCompile(HvMessageTypeX64RegisterIntercept == 0x80010006);
AssertCompile(HvMessageTypeX64Halt == 0x80010007);
AssertCompile(HvMessageTypeX64InterruptWindow == 0x80010008);
/** Pointer to a Hyper-V SyncIC message type. */
typedef HV_MESSAGE_TYPE *PHV_MESSAGE_TYPE;

/** Flag set for hypervisor messages, guest cannot send messages with this
 *  flag set. */
#define HV_MESSAGE_TYPE_HYPERVISOR_MASK     UINT32_C(0x80000000)

/** Hyper-V SynIC message size (they are fixed sized). */
#define HV_MESSAGE_SIZE                     256
/** Maximum Hyper-V SynIC message payload size in bytes. */
#define HV_MESSAGE_MAX_PAYLOAD_BYTE_COUNT   (HV_MESSAGE_SIZE - 16)
/** Maximum Hyper-V SynIC message payload size in QWORDs (uint64_t). */
#define HV_MESSAGE_MAX_PAYLOAD_QWORD_COUNT  (HV_MESSAGE_MAX_PAYLOAD_BYTE_COUNT / 8)

/** SynIC message flags.   */
typedef union
{
    uint8_t             AsUINT8;
    struct
    {
        /** Messages are pending in the queue. */
        uint8_t         MessagePending : 1;
        uint8_t         Reserved : 7;
    };
} HV_MESSAGE_FLAGS;
AssertCompileSize(HV_MESSAGE_FLAGS, 1);

/** SynIC message header. */
typedef struct
{
    HV_MESSAGE_TYPE     MessageType;
    /** The 2.0-5.0b docs all have this incorrectly switched with 'Reserved', WDK 7600 got it right. */
    uint8_t             PayloadSize;
    HV_MESSAGE_FLAGS    MessageFlags;
    uint16_t            Reserved;
    union
    {
        uint64_t        OriginationId;
        HV_PARTITION_ID Sender;
        HV_PORT_ID      Port;
    };
} HV_MESSAGE_HEADER;
AssertCompileSize(HV_MESSAGE_HEADER, 16);
/** Pointer to a Hyper-V message header. */
typedef HV_MESSAGE_HEADER *PHV_MESSAGE_HEADER;
/** Pointer to a const Hyper-V message header. */
typedef HV_MESSAGE_HEADER const *PCHV_MESSAGE_HEADER;



/** @name Intercept access type.
 * @{ */
typedef uint8_t HV_INTERCEPT_ACCESS_TYPE;
#define HV_INTERCEPT_ACCESS_READ            0
#define HV_INTERCEPT_ACCESS_WRITE           1
#define HV_INTERCEPT_ACCESS_EXECUTE         2
/** @} */

/** @name Intercept access type mask.
 * @{ */
typedef uint32_t HV_INTERCEPT_ACCESS_TYPE_MASK;
#define HV_INTERCEPT_ACCESS_MASK_NONE       0
#define HV_INTERCEPT_ACCESS_MASK_READ       1
#define HV_INTERCEPT_ACCESS_MASK_WRITE      2
#define HV_INTERCEPT_ACCESS_MASK_EXECUTE    4
/** @} */

/** X64 intercept execution state.
 * @sa WHV_X64_VP_EXECUTION_STATE */
typedef union
{
    uint16_t            AsUINT16;
    struct
    {
        uint16_t        Cpl                 : 2;
        uint16_t        Cr0Pe               : 1;
        uint16_t        Cr0Am               : 1;
        uint16_t        EferLma             : 1;
        uint16_t        DebugActive         : 1;
        uint16_t        InterruptionPending : 1;
        uint16_t        Reserved0           : 5;
        uint16_t        InterruptShadow     : 1;
        uint16_t        Reserved1           : 3;
    };
} HV_X64_VP_EXECUTION_STATE;
AssertCompileSize(HV_X64_VP_EXECUTION_STATE, 2);
/** Pointer to X86 intercept execution state. */
typedef HV_X64_VP_EXECUTION_STATE *PHV_X64_VP_EXECUTION_STATE;
/** Pointer to const X86 intercept execution state. */
typedef HV_X64_VP_EXECUTION_STATE const *PCHV_X64_VP_EXECUTION_STATE;

/** X64 intercept message header. */
typedef struct
{
    HV_VP_INDEX                     VpIndex;                /**< 0x00 */
    uint8_t                         InstructionLength : 4;  /**< 0x04[3:0]: Zero if not available, instruction fetch exit, ... */
    uint8_t                         Cr8 : 4;                /**< 0x04[7:4]: Not sure since when, but after v2. */
    HV_INTERCEPT_ACCESS_TYPE        InterceptAccessType;    /**< 0x05 */
    HV_X64_VP_EXECUTION_STATE       ExecutionState;         /**< 0x06 */
    HV_X64_SEGMENT_REGISTER         CsSegment;              /**< 0x08 */
    uint64_t                        Rip;                    /**< 0x18 */
    uint64_t                        Rflags;                 /**< 0x20 */
} HV_X64_INTERCEPT_MESSAGE_HEADER;
AssertCompileSize(HV_X64_INTERCEPT_MESSAGE_HEADER, 40);
/** Pointer to a x86 intercept message header. */
typedef HV_X64_INTERCEPT_MESSAGE_HEADER *PHV_X64_INTERCEPT_MESSAGE_HEADER;


/** X64 memory access flags (HvMessageTypeGpaIntercept, HvMessageTypeUnmappedGpa).
 * @sa WHV_MEMORY_ACCESS_INFO */
typedef union
{
    uint8_t             AsUINT8;
    struct
    {
        uint8_t         GvaValid : 1;
        uint8_t         Reserved : 7;
    };
} HV_X64_MEMORY_ACCESS_INFO;
AssertCompileSize(HV_X64_MEMORY_ACCESS_INFO, 1);

/** The payload format for HvMessageTypeGpaIntercept and HvMessageTypeUnmappedGpa.
 * @sa   WHV_MEMORY_ACCESS_CONTEXT
 * @note max message size. */
typedef struct
{
    HV_X64_INTERCEPT_MESSAGE_HEADER Header;                 /**< 0x00 */
    HV_CACHE_TYPE                   CacheType;              /**< 0x28 */
    uint8_t                         InstructionByteCount;   /**< 0x2c */
    HV_X64_MEMORY_ACCESS_INFO       MemoryAccessInfo;       /**< 0x2d */
    uint16_t                        Reserved1;              /**< 0x2e */
    uint64_t                        GuestVirtualAddress;    /**< 0x30 */
    uint64_t                        GuestPhysicalAddress;   /**< 0x38 */
    uint8_t                         InstructionBytes[16];   /**< 0x40 */
    /* We don't the following (v5 / WinHvPlatform): */
    HV_X64_SEGMENT_REGISTER         DsSegment;              /**< 0x50 */
    HV_X64_SEGMENT_REGISTER         SsSegment;              /**< 0x60 */
    uint64_t                        Rax;                    /**< 0x70 */
    uint64_t                        Rcx;                    /**< 0x78 */
    uint64_t                        Rdx;                    /**< 0x80 */
    uint64_t                        Rbx;                    /**< 0x88 */
    uint64_t                        Rsp;                    /**< 0x90 */
    uint64_t                        Rbp;                    /**< 0x98 */
    uint64_t                        Rsi;                    /**< 0xa0 */
    uint64_t                        Rdi;                    /**< 0xa8 */
    uint64_t                        R8;                     /**< 0xb0 */
    uint64_t                        R9;                     /**< 0xb8 */
    uint64_t                        R10;                    /**< 0xc0 */
    uint64_t                        R11;                    /**< 0xc8 */
    uint64_t                        R12;                    /**< 0xd0 */
    uint64_t                        R13;                    /**< 0xd8 */
    uint64_t                        R14;                    /**< 0xe0 */
    uint64_t                        R15;                    /**< 0xe8 */
} HV_X64_MEMORY_INTERCEPT_MESSAGE;
AssertCompileSize(HV_X64_MEMORY_INTERCEPT_MESSAGE, 0xf0);
AssertCompileMemberOffset(HV_X64_MEMORY_INTERCEPT_MESSAGE, DsSegment, 0x50);
/** Pointer to a HvMessageTypeGpaIntercept or HvMessageTypeUnmappedGpa payload. */
typedef HV_X64_MEMORY_INTERCEPT_MESSAGE *PHV_X64_MEMORY_INTERCEPT_MESSAGE;
/** Pointer to a const HvMessageTypeGpaIntercept or HvMessageTypeUnmappedGpa payload. */
typedef HV_X64_MEMORY_INTERCEPT_MESSAGE const *PCHV_X64_MEMORY_INTERCEPT_MESSAGE;


/** The payload format for HvMessageTypeX64MsrIntercept. */
typedef struct
{
    HV_X64_INTERCEPT_MESSAGE_HEADER     Header;                 /**< 0x00 */
    uint32_t                            MsrNumber;              /**< 0x28 (ecx) */
    uint32_t                            Reserved;               /**< 0x2c */
    uint64_t                            Rdx;                    /**< 0x30 */
    uint64_t                            Rax;                    /**< 0x38 */
} HV_X64_MSR_INTERCEPT_MESSAGE;
AssertCompileSize(HV_X64_MSR_INTERCEPT_MESSAGE, 0x40);
/** Pointer to a HvMessageTypeX64MsrIntercept payload. */
typedef HV_X64_MSR_INTERCEPT_MESSAGE *PHV_X64_MSR_INTERCEPT_MESSAGE;
/** Pointer to a const HvMessageTypeX64MsrIntercept payload. */
typedef HV_X64_MSR_INTERCEPT_MESSAGE const *PCHV_X64_MSR_INTERCEPT_MESSAGE;

/** Full MSR message. */
typedef struct
{
    HV_MESSAGE_HEADER                   MsgHdr;
    HV_X64_MSR_INTERCEPT_MESSAGE        Payload;
} HV_X64_MSR_INTERCEPT_MESSAGE_FULL;


/** X64 I/O port access information (HvMessageTypeX64IoPortIntercept). */
typedef union HV_X64_IO_PORT_ACCESS_INFO
{
    uint8_t             AsUINT8;
    struct
    {
        uint8_t         AccessSize  : 3;
        uint8_t         StringOp    : 1;
        uint8_t         RepPrefix   : 1;
        uint8_t         Reserved    : 3;
    };
} HV_X64_IO_PORT_ACCESS_INFO;
AssertCompileSize(HV_X64_IO_PORT_ACCESS_INFO, 1);

/** The payload format for HvMessageTypeX64IoPortIntercept.  */
typedef struct
{
    HV_X64_INTERCEPT_MESSAGE_HEADER     Header;                 /**< 0x00 */
    uint16_t                            PortNumber;             /**< 0x28 */
    HV_X64_IO_PORT_ACCESS_INFO          AccessInfo;             /**< 0x2a */
    uint8_t                             InstructionByteCount;   /**< 0x2b */
    uint32_t                            Reserved;               /**< 0x2c */
    uint64_t                            Rax;                    /**< 0x30 */
    uint8_t                             InstructionBytes[16];   /**< 0x38 */
    HV_X64_SEGMENT_REGISTER             DsSegment;              /**< 0x48 */
    HV_X64_SEGMENT_REGISTER             EsSegment;              /**< 0x58 */
    uint64_t                            Rcx;                    /**< 0x68 */
    uint64_t                            Rsi;                    /**< 0x70 */
    uint64_t                            Rdi;                    /**< 0x78 */
} HV_X64_IO_PORT_INTERCEPT_MESSAGE;
AssertCompileSize(HV_X64_IO_PORT_INTERCEPT_MESSAGE, 128);
/** Pointer to a HvMessageTypeX64IoPortIntercept payload. */
typedef HV_X64_IO_PORT_INTERCEPT_MESSAGE *PHV_X64_IO_PORT_INTERCEPT_MESSAGE;
/** Pointer to a const HvMessageTypeX64IoPortIntercept payload. */
typedef HV_X64_IO_PORT_INTERCEPT_MESSAGE const *PCHV_X64_IO_PORT_INTERCEPT_MESSAGE;

/** Full I/O port message. */
typedef struct
{
    HV_MESSAGE_HEADER                   MsgHdr;
    HV_X64_IO_PORT_INTERCEPT_MESSAGE    Payload;
} HV_X64_IO_PORT_INTERCEPT_MESSAGE_FULL;


/**
 * The payload format for HvMessageTypeX64CpuidIntercept,
 *
 * @note This message does not include HV_X64_INTERCEPT_MESSAGE_HEADER!
 */
typedef struct
{
    HV_X64_INTERCEPT_MESSAGE_HEADER     Header;                 /**< 0x00: The usual intercept header. */
    uint64_t                            Rax;                    /**< 0x28: Input RAX. */
    uint64_t                            Rcx;                    /**< 0x30: Input RCX. */
    uint64_t                            Rdx;                    /**< 0x38: Input RDX. */
    uint64_t                            Rbx;                    /**< 0x40: Input RBX. */
    uint64_t                            DefaultResultRax;       /**< 0x48: Default result RAX. */
    uint64_t                            DefaultResultRcx;       /**< 0x50: Default result RCX. */
    uint64_t                            DefaultResultRdx;       /**< 0x58: Default result RDX. */
    uint64_t                            DefaultResultRbx;       /**< 0x60: Default result RBX. */
} HV_X64_CPUID_INTERCEPT_MESSAGE;
AssertCompileSize(HV_X64_CPUID_INTERCEPT_MESSAGE, 0x68);
/** Pointer to a HvMessageTypeX64CpuidIntercept payload. */
typedef HV_X64_CPUID_INTERCEPT_MESSAGE *PHV_X64_CPUID_INTERCEPT_MESSAGE;
/** Pointer to a const HvMessageTypeX64CpuidIntercept payload. */
typedef HV_X64_CPUID_INTERCEPT_MESSAGE const *PCHV_X64_CPUID_INTERCEPT_MESSAGE;

/** Full HvMessageTypeX64CpuidIntercept message. */
typedef struct
{
    HV_MESSAGE_HEADER                   MsgHdr;
    HV_X64_CPUID_INTERCEPT_MESSAGE      Payload;
} HV_X64_CPUID_INTERCEPT_MESSAGE_FULL;


/** X64 exception information (HvMessageTypeX64ExceptionIntercept).
 * @sa WHV_VP_EXCEPTION_INFO */
typedef union
{
    uint8_t             AsUINT8;
    struct
    {
        uint8_t         ErrorCodeValid : 1;
        /** @todo WHV_VP_EXCEPTION_INFO::SoftwareException   */
        uint8_t         Reserved       : 7;
    };
} HV_X64_EXCEPTION_INFO;
AssertCompileSize(HV_X64_EXCEPTION_INFO, 1);

/** The payload format for HvMessageTypeX64ExceptionIntercept.
 * @sa   WHV_VP_EXCEPTION_CONTEXT
 * @note max message size. */
typedef struct
{
    HV_X64_INTERCEPT_MESSAGE_HEADER     Header;                 /**< 0x00 */
    uint16_t                            ExceptionVector;        /**< 0x28 */
    HV_X64_EXCEPTION_INFO               ExceptionInfo;          /**< 0x2a */
    uint8_t                             InstructionByteCount;   /**< 0x2b */
    uint32_t                            ErrorCode;              /**< 0x2c */
    uint64_t                            ExceptionParameter;     /**< 0x30 */
    uint64_t                            Reserved;               /**< 0x38 */
    uint8_t                             InstructionBytes[16];   /**< 0x40 */
    HV_X64_SEGMENT_REGISTER             DsSegment;              /**< 0x50 */
    HV_X64_SEGMENT_REGISTER             SsSegment;              /**< 0x60 */
    uint64_t                            Rax;                    /**< 0x70 */
    uint64_t                            Rcx;                    /**< 0x78 */
    uint64_t                            Rdx;                    /**< 0x80 */
    uint64_t                            Rbx;                    /**< 0x88 */
    uint64_t                            Rsp;                    /**< 0x90 */
    uint64_t                            Rbp;                    /**< 0x98 */
    uint64_t                            Rsi;                    /**< 0xa0 */
    uint64_t                            Rdi;                    /**< 0xa8 */
    uint64_t                            R8;                     /**< 0xb0 */
    uint64_t                            R9;                     /**< 0xb8 */
    uint64_t                            R10;                    /**< 0xc0 */
    uint64_t                            R11;                    /**< 0xc8 */
    uint64_t                            R12;                    /**< 0xd0 */
    uint64_t                            R13;                    /**< 0xd8 */
    uint64_t                            R14;                    /**< 0xe0 */
    uint64_t                            R15;                    /**< 0xe8 */
} HV_X64_EXCEPTION_INTERCEPT_MESSAGE;
AssertCompileSize(HV_X64_EXCEPTION_INTERCEPT_MESSAGE, 0xf0);
/** Pointer to a HvMessageTypeX64ExceptionIntercept payload. */
typedef HV_X64_EXCEPTION_INTERCEPT_MESSAGE *PHV_X64_EXCEPTION_INTERCEPT_MESSAGE;
/** Pointer to a ocnst HvMessageTypeX64ExceptionIntercept payload. */
typedef HV_X64_EXCEPTION_INTERCEPT_MESSAGE const *PCHV_X64_EXCEPTION_INTERCEPT_MESSAGE;


/**
 * The payload format for HvMessageTypeX64Halt,
 *
 * @note This message does not include HV_X64_INTERCEPT_MESSAGE_HEADER!
 */
typedef struct
{
    /** Seems to be a zero 64-bit field here.  */
    uint64_t    u64Reserved;
} HV_X64_HALT_MESSAGE;
/** Pointer to a HvMessageTypeX64Halt payload. */
typedef HV_X64_HALT_MESSAGE *PHV_X64_HALT_MESSAGE;
/** Pointer to a const HvMessageTypeX64Halt payload. */
typedef HV_X64_HALT_MESSAGE const *PCHV_X64_HALT_MESSAGE;

/** Full HvMessageTypeX64Halt message. */
typedef struct
{
    HV_MESSAGE_HEADER                   MsgHdr;
    HV_X64_HALT_MESSAGE                 Payload;
} HV_X64_HALT_MESSAGE_FULL;


/**
 * The payload format for HvMessageTypeX64InterruptWindow,
 *
 * @note This message does not include HV_X64_INTERCEPT_MESSAGE_HEADER!
 */
typedef struct
{
    /** 0x00: The usual intercept header. */
    HV_X64_INTERCEPT_MESSAGE_HEADER     Header;
    /** 0x28: What's pending. */
    HV_X64_PENDING_INTERRUPTION_TYPE    Type;
    /** 0x2c: Explicit structure alignment padding. */
    uint32_t                            u32ExplicitPadding;
} HV_X64_INTERRUPT_WINDOW_MESSAGE;
AssertCompileSize(HV_X64_INTERRUPT_WINDOW_MESSAGE, 0x30);
/** Pointer to a HvMessageTypeX64InterruptWindow payload. */
typedef HV_X64_INTERRUPT_WINDOW_MESSAGE *PHV_X64_INTERRUPT_WINDOW_MESSAGE;
/** Pointer to a const HvMessageTypeX64InterruptWindow payload. */
typedef HV_X64_INTERRUPT_WINDOW_MESSAGE const *PCHV_X64_INTERRUPT_WINDOW_MESSAGE;

/** Full HvMessageTypeX64InterruptWindow message. */
typedef struct
{
    /** Payload size is 0x30.   */
    HV_MESSAGE_HEADER                   MsgHdr;
    HV_X64_INTERRUPT_WINDOW_MESSAGE     Payload;
} HV_X64_INTERRUPT_WINDOW_MESSAGE_FULL;



/** Hyper-V SynIC message. */
typedef struct
{
    HV_MESSAGE_HEADER   Header;
    /** 0x10 */
    union
    {
        uint64_t                            Payload[HV_MESSAGE_MAX_PAYLOAD_QWORD_COUNT];

        /** Common header for X64 intercept messages.
         * The HvMessageTypeUnrecoverableException message only has this.  */
        HV_X64_INTERCEPT_MESSAGE_HEADER     X64InterceptHeader;
        /** HvMessageTypeGpaIntercept, HvMessageTypeUnmappedGpa. */
        HV_X64_MEMORY_INTERCEPT_MESSAGE     X64MemoryIntercept;
        /** HvMessageTypeX64IoPortIntercept */
        HV_X64_IO_PORT_INTERCEPT_MESSAGE    X64IoPortIntercept;
        /** HvMessageTypeX64MsrIntercept */
        HV_X64_MSR_INTERCEPT_MESSAGE        X64MsrIntercept;
        /** HvMessageTypeX64CpuidIntercept */
        HV_X64_CPUID_INTERCEPT_MESSAGE      X64CpuIdIntercept;
        /** HvMessageTypeX64ExceptionIntercept */
        HV_X64_EXCEPTION_INTERCEPT_MESSAGE  X64ExceptionIntercept;
        /** HvMessageTypeX64Halt.
         * @note No intercept header?  */
        HV_X64_HALT_MESSAGE                 X64Halt;
        /** HvMessageTypeX64InterruptWindow. */
        HV_X64_INTERRUPT_WINDOW_MESSAGE     X64InterruptWindow;
    };
} HV_MESSAGE;
AssertCompileSize(HV_MESSAGE, HV_MESSAGE_SIZE);
/** Pointer to a Hyper-V SynIC message. */
typedef HV_MESSAGE *PHV_MESSAGE;
/** Pointer to const a Hyper-V SynIC message. */
typedef HV_MESSAGE const *PCHV_MESSAGE;

#endif /* !IPRT_INCLUDED_nt_hyperv_h */

