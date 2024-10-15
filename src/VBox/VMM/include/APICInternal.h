/* $Id: APICInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * APIC - Advanced Programmable Interrupt Controller.
 */

/*
 * Copyright (C) 2016-2024 Oracle and/or its affiliates.
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

#ifndef VMM_INCLUDED_SRC_include_APICInternal_h
#define VMM_INCLUDED_SRC_include_APICInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <VBox/apic.h>
#include <VBox/sup.h>
#include <VBox/vmm/pdmdev.h>

/** @defgroup grp_apic_int       Internal
 * @ingroup grp_apic
 * @internal
 * @{
 */

/** The APIC hardware version we are emulating. */
#define XAPIC_HARDWARE_VERSION               XAPIC_HARDWARE_VERSION_P4

#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
#define XAPIC_SVR_VALID                      XAPIC_SVR_VALID_P4
#define XAPIC_ID_BROADCAST_MASK              XAPIC_ID_BROADCAST_MASK_P4
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif

#define VMCPU_TO_XAPICPAGE(a_pVCpu)          ((PXAPICPAGE)(CTX_SUFF((a_pVCpu)->apic.s.pvApicPage)))
#define VMCPU_TO_CXAPICPAGE(a_pVCpu)         ((PCXAPICPAGE)(CTX_SUFF((a_pVCpu)->apic.s.pvApicPage)))

#define VMCPU_TO_X2APICPAGE(a_pVCpu)         ((PX2APICPAGE)(CTX_SUFF((a_pVCpu)->apic.s.pvApicPage)))
#define VMCPU_TO_CX2APICPAGE(a_pVCpu)        ((PCX2APICPAGE)(CTX_SUFF((a_pVCpu)->apic.s.pvApicPage)))

#define VMCPU_TO_APICCPU(a_pVCpu)            (&(a_pVCpu)->apic.s)
#define VM_TO_APIC(a_pVM)                    (&(a_pVM)->apic.s)
#define VM_TO_APICDEV(a_pVM)                 CTX_SUFF(VM_TO_APIC(a_pVM)->pApicDev)
#ifdef IN_RING3
# define VMCPU_TO_DEVINS(a_pVCpu)           ((a_pVCpu)->pVMR3->apic.s.pDevInsR3)
#elif defined(IN_RING0)
# define VMCPU_TO_DEVINS(a_pVCpu)           ((a_pVCpu)->pGVM->apicr0.s.pDevInsR0)
#endif

#define APICCPU_TO_XAPICPAGE(a_ApicCpu)      ((PXAPICPAGE)(CTX_SUFF((a_ApicCpu)->pvApicPage)))
#define APICCPU_TO_CXAPICPAGE(a_ApicCpu)     ((PCXAPICPAGE)(CTX_SUFF((a_ApicCpu)->pvApicPage)))

/** Vector offset in an APIC 256-bit sparse register. */
#define XAPIC_REG256_VECTOR_OFF(a_Vector)    (((a_Vector) & UINT32_C(0xe0)) >> 1)
/** Bit position at offset in an APIC 256-bit sparse register. */
#define XAPIC_REG256_VECTOR_BIT(a_Vector)    ((a_Vector) & UINT32_C(0x1f))

/** Maximum valid offset for a register (16-byte aligned, 4 byte wide access). */
#define XAPIC_OFF_MAX_VALID                  (sizeof(XAPICPAGE) - 4 * sizeof(uint32_t))

/** Whether the APIC is in X2APIC mode or not. */
#define XAPIC_IN_X2APIC_MODE(a_pVCpu)        (   (  ((a_pVCpu)->apic.s.uApicBaseMsr) \
                                                  & (MSR_IA32_APICBASE_EN | MSR_IA32_APICBASE_EXTD)) \
                                              ==    (MSR_IA32_APICBASE_EN | MSR_IA32_APICBASE_EXTD) )

/**
 * The xAPIC sparse 256-bit register.
 */
typedef union XAPIC256BITREG
{
    /** The sparse-bitmap view. */
    struct
    {
        uint32_t    u32Reg;
        uint32_t    uReserved0[3];
    } u[8];
    /** The 32-bit view. */
    uint32_t        au32[32];
} XAPIC256BITREG;
/** Pointer to an xAPIC sparse bitmap register. */
typedef XAPIC256BITREG *PXAPIC256BITREG;
/** Pointer to a const xAPIC sparse bitmap register. */
typedef XAPIC256BITREG const *PCXAPIC256BITREG;
AssertCompileSize(XAPIC256BITREG, 128);

/**
 * The xAPIC memory layout as per Intel/AMD specs.
 */
typedef struct XAPICPAGE
{
    /* 0x00 - Reserved. */
    uint32_t                    uReserved0[8];
    /* 0x20 - APIC ID. */
    struct
    {
        uint8_t                 u8Reserved0[3];
        uint8_t                 u8ApicId;
        uint32_t                u32Reserved0[3];
    } id;
    /* 0x30 - APIC version register. */
    union
    {
        struct
        {
#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
            uint8_t             u8Version;
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif
            uint8_t             uReserved0;
            uint8_t             u8MaxLvtEntry;
            uint8_t             fEoiBroadcastSupression : 1;
            uint8_t             u7Reserved1   : 7;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32Version;
            uint32_t            u32Reserved0[3];
        } all;
    } version;
    /* 0x40 - Reserved. */
    uint32_t                    uReserved1[16];
    /* 0x80 - Task Priority Register (TPR). */
    struct
    {
        uint8_t                 u8Tpr;
        uint8_t                 u8Reserved0[3];
        uint32_t                u32Reserved0[3];
    } tpr;
    /* 0x90 - Arbitration Priority Register (APR). */
    struct
    {
        uint8_t                 u8Apr;
        uint8_t                 u8Reserved0[3];
        uint32_t                u32Reserved0[3];
    } apr;
    /* 0xA0 - Processor Priority Register (PPR). */
    struct
    {
        uint8_t                 u8Ppr;
        uint8_t                 u8Reserved0[3];
        uint32_t                u32Reserved0[3];
    } ppr;
    /* 0xB0 - End Of Interrupt Register (EOI). */
    struct
    {
        uint32_t                u32Eoi;
        uint32_t                u32Reserved0[3];
    } eoi;
    /* 0xC0 - Remote Read Register (RRD). */
    struct
    {
        uint32_t                u32Rrd;
        uint32_t                u32Reserved0[3];
    } rrd;
    /* 0xD0 - Logical Destination Register (LDR). */
    union
    {
        struct
        {
            uint8_t             u8Reserved0[3];
            uint8_t             u8LogicalApicId;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32Ldr;
            uint32_t            u32Reserved0[3];
        } all;
    } ldr;
    /* 0xE0 - Destination Format Register (DFR). */
    union
    {
        struct
        {
            uint32_t            u28ReservedMb1 : 28;    /* MB1 */
            uint32_t            u4Model        : 4;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32Dfr;
            uint32_t            u32Reserved0[3];
        } all;
    } dfr;
    /* 0xF0 - Spurious-Interrupt Vector Register (SVR). */
    union
    {
        struct
        {
            uint32_t            u8SpuriousVector     : 8;
            uint32_t            fApicSoftwareEnable  : 1;
            uint32_t            u3Reserved0          : 3;
            uint32_t            fSupressEoiBroadcast : 1;
            uint32_t            u19Reserved1         : 19;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32Svr;
            uint32_t            u32Reserved0[3];
        } all;
    } svr;
    /* 0x100 - In-service Register (ISR). */
    XAPIC256BITREG              isr;
    /* 0x180 - Trigger Mode Register (TMR). */
    XAPIC256BITREG              tmr;
    /* 0x200 - Interrupt Request Register (IRR). */
    XAPIC256BITREG              irr;
    /* 0x280 - Error Status Register (ESR). */
    union
    {
        struct
        {
#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
            uint32_t            u4Reserved0        : 4;
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif
            uint32_t            fRedirectableIpi   : 1;
            uint32_t            fSendIllegalVector : 1;
            uint32_t            fRcvdIllegalVector : 1;
            uint32_t            fIllegalRegAddr    : 1;
            uint32_t            u24Reserved1       : 24;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32Errors;
            uint32_t            u32Reserved0[3];
        } all;
    } esr;
    /* 0x290 - Reserved. */
    uint32_t                    uReserved2[28];
    /* 0x300 - Interrupt Command Register (ICR) - Low. */
    union
    {
        struct
        {
            uint32_t            u8Vector         : 8;
            uint32_t            u3DeliveryMode   : 3;
            uint32_t            u1DestMode       : 1;
            uint32_t            u1DeliveryStatus : 1;
            uint32_t            fReserved0       : 1;
            uint32_t            u1Level          : 1;
            uint32_t            u1TriggerMode    : 1;
            uint32_t            u2Reserved1      : 2;
            uint32_t            u2DestShorthand  : 2;
            uint32_t            u12Reserved2     : 12;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32IcrLo;
            uint32_t            u32Reserved0[3];
        } all;
    } icr_lo;
    /* 0x310 - Interrupt Comannd Register (ICR) - High. */
    union
    {
        struct
        {
            uint32_t            u24Reserved0 : 24;
            uint32_t            u8Dest       : 8;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32IcrHi;
            uint32_t            u32Reserved0[3];
        } all;
    } icr_hi;
    /* 0x320 - Local Vector Table (LVT) Timer Register. */
    union
    {
        struct
        {
            uint32_t            u8Vector         : 8;
            uint32_t            u4Reserved0      : 4;
            uint32_t            u1DeliveryStatus : 1;
            uint32_t            u3Reserved1      : 3;
            uint32_t            u1Mask           : 1;
            uint32_t            u2TimerMode      : 2;
            uint32_t            u13Reserved2     : 13;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32LvtTimer;
            uint32_t            u32Reserved0[3];
        } all;
    } lvt_timer;
    /* 0x330 - Local Vector Table (LVT) Thermal Sensor Register. */
#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
    union
    {
        struct
        {
            uint32_t            u8Vector         : 8;
            uint32_t            u3DeliveryMode   : 3;
            uint32_t            u1Reserved0      : 1;
            uint32_t            u1DeliveryStatus : 1;
            uint32_t            u3Reserved1      : 3;
            uint32_t            u1Mask           : 1;
            uint32_t            u15Reserved2     : 15;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32LvtThermal;
            uint32_t            u32Reserved0[3];
        } all;
    } lvt_thermal;
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif
    /* 0x340 - Local Vector Table (LVT) Performance Monitor Counter (PMC) Register. */
    union
    {
        struct
        {
            uint32_t            u8Vector         : 8;
            uint32_t            u3DeliveryMode   : 3;
            uint32_t            u1Reserved0      : 1;
            uint32_t            u1DeliveryStatus : 1;
            uint32_t            u3Reserved1      : 3;
            uint32_t            u1Mask           : 1;
            uint32_t            u15Reserved2     : 15;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32LvtPerf;
            uint32_t            u32Reserved0[3];
        } all;
    } lvt_perf;
    /* 0x350 - Local Vector Table (LVT) LINT0 Register. */
    union
    {
        struct
        {
            uint32_t            u8Vector         : 8;
            uint32_t            u3DeliveryMode   : 3;
            uint32_t            u1Reserved0      : 1;
            uint32_t            u1DeliveryStatus : 1;
            uint32_t            u1IntrPolarity   : 1;
            uint32_t            u1RemoteIrr      : 1;
            uint32_t            u1TriggerMode    : 1;
            uint32_t            u1Mask           : 1;
            uint32_t            u15Reserved2     : 15;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32LvtLint0;
            uint32_t            u32Reserved0[3];
        } all;
    } lvt_lint0;
    /* 0x360 - Local Vector Table (LVT) LINT1 Register. */
    union
    {
        struct
        {
            uint32_t            u8Vector         : 8;
            uint32_t            u3DeliveryMode   : 3;
            uint32_t            u1Reserved0      : 1;
            uint32_t            u1DeliveryStatus : 1;
            uint32_t            u1IntrPolarity   : 1;
            uint32_t            u1RemoteIrr      : 1;
            uint32_t            u1TriggerMode    : 1;
            uint32_t            u1Mask           : 1;
            uint32_t            u15Reserved2     : 15;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32LvtLint1;
            uint32_t            u32Reserved0[3];
        } all;
    } lvt_lint1;
    /* 0x370 - Local Vector Table (LVT) Error Register. */
    union
    {
        struct
        {
            uint32_t            u8Vector         : 8;
            uint32_t            u4Reserved0      : 4;
            uint32_t            u1DeliveryStatus : 1;
            uint32_t            u3Reserved1      : 3;
            uint32_t            u1Mask           : 1;
            uint32_t            u15Reserved2     : 15;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32LvtError;
            uint32_t            u32Reserved0[3];
        } all;
    } lvt_error;
    /* 0x380 - Timer Initial Counter Register. */
    struct
    {
        uint32_t                u32InitialCount;
        uint32_t                u32Reserved0[3];
    } timer_icr;
    /* 0x390 - Timer Current Counter Register. */
    struct
    {
        uint32_t                u32CurrentCount;
        uint32_t                u32Reserved0[3];
    } timer_ccr;
    /* 0x3A0 - Reserved. */
    uint32_t                    u32Reserved3[16];
    /* 0x3E0 - Timer Divide Configuration Register. */
    union
    {
        struct
        {
            uint32_t            u2DivideValue0 : 2;
            uint32_t            u1Reserved0    : 1;
            uint32_t            u1DivideValue1 : 1;
            uint32_t            u28Reserved1   : 28;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32DivideValue;
            uint32_t            u32Reserved0[3];
        } all;
    } timer_dcr;
    /* 0x3F0 - Reserved. */
    uint8_t                     u8Reserved0[3088];
} XAPICPAGE;
/** Pointer to a XAPICPAGE struct. */
typedef XAPICPAGE *PXAPICPAGE;
/** Pointer to a const XAPICPAGE struct. */
typedef const XAPICPAGE *PCXAPICPAGE;
AssertCompileSize(XAPICPAGE, 4096);
AssertCompileMemberOffset(XAPICPAGE, id,          XAPIC_OFF_ID);
AssertCompileMemberOffset(XAPICPAGE, version,     XAPIC_OFF_VERSION);
AssertCompileMemberOffset(XAPICPAGE, tpr,         XAPIC_OFF_TPR);
AssertCompileMemberOffset(XAPICPAGE, apr,         XAPIC_OFF_APR);
AssertCompileMemberOffset(XAPICPAGE, ppr,         XAPIC_OFF_PPR);
AssertCompileMemberOffset(XAPICPAGE, eoi,         XAPIC_OFF_EOI);
AssertCompileMemberOffset(XAPICPAGE, rrd,         XAPIC_OFF_RRD);
AssertCompileMemberOffset(XAPICPAGE, ldr,         XAPIC_OFF_LDR);
AssertCompileMemberOffset(XAPICPAGE, dfr,         XAPIC_OFF_DFR);
AssertCompileMemberOffset(XAPICPAGE, svr,         XAPIC_OFF_SVR);
AssertCompileMemberOffset(XAPICPAGE, isr,         XAPIC_OFF_ISR0);
AssertCompileMemberOffset(XAPICPAGE, tmr,         XAPIC_OFF_TMR0);
AssertCompileMemberOffset(XAPICPAGE, irr,         XAPIC_OFF_IRR0);
AssertCompileMemberOffset(XAPICPAGE, esr,         XAPIC_OFF_ESR);
AssertCompileMemberOffset(XAPICPAGE, icr_lo,      XAPIC_OFF_ICR_LO);
AssertCompileMemberOffset(XAPICPAGE, icr_hi,      XAPIC_OFF_ICR_HI);
AssertCompileMemberOffset(XAPICPAGE, lvt_timer,   XAPIC_OFF_LVT_TIMER);
AssertCompileMemberOffset(XAPICPAGE, lvt_thermal, XAPIC_OFF_LVT_THERMAL);
AssertCompileMemberOffset(XAPICPAGE, lvt_perf,    XAPIC_OFF_LVT_PERF);
AssertCompileMemberOffset(XAPICPAGE, lvt_lint0,   XAPIC_OFF_LVT_LINT0);
AssertCompileMemberOffset(XAPICPAGE, lvt_lint1,   XAPIC_OFF_LVT_LINT1);
AssertCompileMemberOffset(XAPICPAGE, lvt_error,   XAPIC_OFF_LVT_ERROR);
AssertCompileMemberOffset(XAPICPAGE, timer_icr,   XAPIC_OFF_TIMER_ICR);
AssertCompileMemberOffset(XAPICPAGE, timer_ccr,   XAPIC_OFF_TIMER_CCR);
AssertCompileMemberOffset(XAPICPAGE, timer_dcr,   XAPIC_OFF_TIMER_DCR);

/**
 * The x2APIC memory layout as per Intel/AMD specs.
 */
typedef struct X2APICPAGE
{
    /* 0x00 - Reserved. */
    uint32_t                    uReserved0[8];
    /* 0x20 - APIC ID. */
    struct
    {
        uint32_t                u32ApicId;
        uint32_t                u32Reserved0[3];
    } id;
    /* 0x30 - APIC version register. */
    union
    {
        struct
        {
            uint8_t             u8Version;
            uint8_t             u8Reserved0;
            uint8_t             u8MaxLvtEntry;
            uint8_t             fEoiBroadcastSupression : 1;
            uint8_t             u7Reserved1   : 7;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32Version;
            uint32_t            u32Reserved2[3];
        } all;
    } version;
    /* 0x40 - Reserved. */
    uint32_t                    uReserved1[16];
    /* 0x80 - Task Priority Register (TPR). */
    struct
    {
        uint8_t                 u8Tpr;
        uint8_t                 u8Reserved0[3];
        uint32_t                u32Reserved0[3];
    } tpr;
    /* 0x90 - Reserved. */
    uint32_t                    uReserved2[4];
    /* 0xA0 - Processor Priority Register (PPR). */
    struct
    {
        uint8_t                 u8Ppr;
        uint8_t                 u8Reserved0[3];
        uint32_t                u32Reserved0[3];
    } ppr;
    /* 0xB0 - End Of Interrupt Register (EOI). */
    struct
    {
        uint32_t                u32Eoi;
        uint32_t                u32Reserved0[3];
    } eoi;
    /* 0xC0 - Remote Read Register (RRD). */
    struct
    {
        uint32_t                u32Rrd;
        uint32_t                u32Reserved0[3];
    } rrd;
    /* 0xD0 - Logical Destination Register (LDR). */
    struct
    {
        uint32_t                u32LogicalApicId;
        uint32_t                u32Reserved1[3];
    } ldr;
    /* 0xE0 - Reserved. */
    uint32_t                    uReserved3[4];
    /* 0xF0 - Spurious-Interrupt Vector Register (SVR). */
    union
    {
        struct
        {
            uint32_t            u8SpuriousVector     : 8;
            uint32_t            fApicSoftwareEnable  : 1;
            uint32_t            u3Reserved0          : 3;
            uint32_t            fSupressEoiBroadcast : 1;
            uint32_t            u19Reserved1         : 19;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32Svr;
            uint32_t            uReserved0[3];
        } all;
    } svr;
    /* 0x100 - In-service Register (ISR). */
    XAPIC256BITREG              isr;
    /* 0x180 - Trigger Mode Register (TMR). */
    XAPIC256BITREG              tmr;
    /* 0x200 - Interrupt Request Register (IRR). */
    XAPIC256BITREG              irr;
    /* 0x280 - Error Status Register (ESR). */
    union
    {
        struct
        {
#if XAPIC_HARDWARE_VERSION == XAPIC_HARDWARE_VERSION_P4
            uint32_t            u4Reserved0        : 4;
#else
# error "Implement Pentium and P6 family APIC architectures"
#endif
            uint32_t            fRedirectableIpi   : 1;
            uint32_t            fSendIllegalVector : 1;
            uint32_t            fRcvdIllegalVector : 1;
            uint32_t            fIllegalRegAddr    : 1;
            uint32_t            u24Reserved1       : 24;
            uint32_t            uReserved0[3];
        } u;
        struct
        {
            uint32_t            u32Errors;
            uint32_t            u32Reserved0[3];
        } all;
    } esr;
    /* 0x290 - Reserved. */
    uint32_t                    uReserved4[28];
    /* 0x300 - Interrupt Command Register (ICR) - Low. */
    union
    {
        struct
        {
            uint32_t            u8Vector         : 8;
            uint32_t            u3DeliveryMode   : 3;
            uint32_t            u1DestMode       : 1;
            uint32_t            u2Reserved0      : 2;
            uint32_t            u1Level          : 1;
            uint32_t            u1TriggerMode    : 1;
            uint32_t            u2Reserved1      : 2;
            uint32_t            u2DestShorthand  : 2;
            uint32_t            u12Reserved2     : 12;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32IcrLo;
            uint32_t            u32Reserved3[3];
        } all;
    } icr_lo;
    /* 0x310 - Interrupt Comannd Register (ICR) - High. */
    struct
    {
        uint32_t                u32IcrHi;
        uint32_t                uReserved1[3];
    } icr_hi;
    /* 0x320 - Local Vector Table (LVT) Timer Register. */
    union
    {
        struct
        {
            uint32_t            u8Vector         : 8;
            uint32_t            u4Reserved0      : 4;
            uint32_t            u1DeliveryStatus : 1;
            uint32_t            u3Reserved1      : 3;
            uint32_t            u1Mask           : 1;
            uint32_t            u2TimerMode      : 2;
            uint32_t            u13Reserved2     : 13;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32LvtTimer;
            uint32_t            u32Reserved0[3];
        } all;
    } lvt_timer;
    /* 0x330 - Local Vector Table (LVT) Thermal Sensor Register. */
    union
    {
        struct
        {
            uint32_t            u8Vector         : 8;
            uint32_t            u3DeliveryMode   : 3;
            uint32_t            u1Reserved0      : 1;
            uint32_t            u1DeliveryStatus : 1;
            uint32_t            u3Reserved1      : 3;
            uint32_t            u1Mask           : 1;
            uint32_t            u15Reserved2     : 15;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32LvtThermal;
            uint32_t            uReserved0[3];
        } all;
    } lvt_thermal;
    /* 0x340 - Local Vector Table (LVT) Performance Monitor Counter (PMC) Register. */
    union
    {
        struct
        {
            uint32_t            u8Vector         : 8;
            uint32_t            u3DeliveryMode   : 3;
            uint32_t            u1Reserved0      : 1;
            uint32_t            u1DeliveryStatus : 1;
            uint32_t            u3Reserved1      : 3;
            uint32_t            u1Mask           : 1;
            uint32_t            u15Reserved2     : 15;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32LvtPerf;
            uint32_t            u32Reserved0[3];
        } all;
    } lvt_perf;
    /* 0x350 - Local Vector Table (LVT) LINT0 Register. */
    union
    {
        struct
        {
            uint32_t            u8Vector         : 8;
            uint32_t            u3DeliveryMode   : 3;
            uint32_t            u1Reserved0      : 1;
            uint32_t            u1DeliveryStatus : 1;
            uint32_t            u1IntrPolarity   : 1;
            uint32_t            u1RemoteIrr      : 1;
            uint32_t            u1TriggerMode    : 1;
            uint32_t            u1Mask           : 1;
            uint32_t            u15Reserved2     : 15;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32LvtLint0;
            uint32_t            u32Reserved0[3];
        } all;
    } lvt_lint0;
    /* 0x360 - Local Vector Table (LVT) LINT1 Register. */
    union
    {
        struct
        {
            uint32_t            u8Vector         : 8;
            uint32_t            u3DeliveryMode   : 3;
            uint32_t            u1Reserved0      : 1;
            uint32_t            u1DeliveryStatus : 1;
            uint32_t            u1IntrPolarity   : 1;
            uint32_t            u1RemoteIrr      : 1;
            uint32_t            u1TriggerMode    : 1;
            uint32_t            u1Mask           : 1;
            uint32_t            u15Reserved2     : 15;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32LvtLint1;
            uint32_t            u32Reserved0[3];
        } all;
    } lvt_lint1;
    /* 0x370 - Local Vector Table (LVT) Error Register. */
    union
    {
        struct
        {
            uint32_t            u8Vector         : 8;
            uint32_t            u4Reserved0      : 4;
            uint32_t            u1DeliveryStatus : 1;
            uint32_t            u3Reserved1      : 3;
            uint32_t            u1Mask           : 1;
            uint32_t            u15Reserved2     : 15;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32LvtError;
            uint32_t            u32Reserved0[3];
        } all;
    } lvt_error;
    /* 0x380 - Timer Initial Counter Register. */
    struct
    {
        uint32_t                u32InitialCount;
        uint32_t                u32Reserved0[3];
    } timer_icr;
    /* 0x390 - Timer Current Counter Register. */
    struct
    {
        uint32_t                u32CurrentCount;
        uint32_t                u32Reserved0[3];
    } timer_ccr;
    /* 0x3A0 - Reserved. */
    uint32_t                    uReserved5[16];
    /* 0x3E0 - Timer Divide Configuration Register. */
    union
    {
        struct
        {
            uint32_t            u2DivideValue0 : 2;
            uint32_t            u1Reserved0    : 1;
            uint32_t            u1DivideValue1 : 1;
            uint32_t            u28Reserved1   : 28;
            uint32_t            u32Reserved0[3];
        } u;
        struct
        {
            uint32_t            u32DivideValue;
            uint32_t            u32Reserved0[3];
        } all;
    } timer_dcr;
    /* 0x3F0 - Self IPI Register. */
    struct
    {
        uint32_t                u8Vector     : 8;
        uint32_t                u24Reserved0 : 24;
        uint32_t                u32Reserved0[3];
    } self_ipi;
    /* 0x400 - Reserved. */
    uint8_t                     u8Reserved0[3072];
} X2APICPAGE;
/** Pointer to a X2APICPAGE struct. */
typedef X2APICPAGE *PX2APICPAGE;
/** Pointer to a const X2APICPAGE struct. */
typedef const X2APICPAGE *PCX2APICPAGE;
AssertCompileSize(X2APICPAGE, 4096);
AssertCompileSize(X2APICPAGE, sizeof(XAPICPAGE));
AssertCompileMemberOffset(X2APICPAGE, id,          XAPIC_OFF_ID);
AssertCompileMemberOffset(X2APICPAGE, version,     XAPIC_OFF_VERSION);
AssertCompileMemberOffset(X2APICPAGE, tpr,         XAPIC_OFF_TPR);
AssertCompileMemberOffset(X2APICPAGE, ppr,         XAPIC_OFF_PPR);
AssertCompileMemberOffset(X2APICPAGE, eoi,         XAPIC_OFF_EOI);
AssertCompileMemberOffset(X2APICPAGE, rrd,         XAPIC_OFF_RRD);
AssertCompileMemberOffset(X2APICPAGE, ldr,         XAPIC_OFF_LDR);
AssertCompileMemberOffset(X2APICPAGE, svr,         XAPIC_OFF_SVR);
AssertCompileMemberOffset(X2APICPAGE, isr,         XAPIC_OFF_ISR0);
AssertCompileMemberOffset(X2APICPAGE, tmr,         XAPIC_OFF_TMR0);
AssertCompileMemberOffset(X2APICPAGE, irr,         XAPIC_OFF_IRR0);
AssertCompileMemberOffset(X2APICPAGE, esr,         XAPIC_OFF_ESR);
AssertCompileMemberOffset(X2APICPAGE, icr_lo,      XAPIC_OFF_ICR_LO);
AssertCompileMemberOffset(X2APICPAGE, icr_hi,      XAPIC_OFF_ICR_HI);
AssertCompileMemberOffset(X2APICPAGE, lvt_timer,   XAPIC_OFF_LVT_TIMER);
AssertCompileMemberOffset(X2APICPAGE, lvt_thermal, XAPIC_OFF_LVT_THERMAL);
AssertCompileMemberOffset(X2APICPAGE, lvt_perf,    XAPIC_OFF_LVT_PERF);
AssertCompileMemberOffset(X2APICPAGE, lvt_lint0,   XAPIC_OFF_LVT_LINT0);
AssertCompileMemberOffset(X2APICPAGE, lvt_lint1,   XAPIC_OFF_LVT_LINT1);
AssertCompileMemberOffset(X2APICPAGE, lvt_error,   XAPIC_OFF_LVT_ERROR);
AssertCompileMemberOffset(X2APICPAGE, timer_icr,   XAPIC_OFF_TIMER_ICR);
AssertCompileMemberOffset(X2APICPAGE, timer_ccr,   XAPIC_OFF_TIMER_CCR);
AssertCompileMemberOffset(X2APICPAGE, timer_dcr,   XAPIC_OFF_TIMER_DCR);
AssertCompileMemberOffset(X2APICPAGE, self_ipi,    X2APIC_OFF_SELF_IPI);

/**
 * APIC MSR access error.
 * @note The values must match the array indices in apicMsrAccessError().
 */
typedef enum APICMSRACCESS
{
    /** MSR read while not in x2APIC. */
    APICMSRACCESS_INVALID_READ_MODE = 0,
    /** MSR write while not in x2APIC. */
    APICMSRACCESS_INVALID_WRITE_MODE,
    /** MSR read for a reserved/unknown/invalid MSR. */
    APICMSRACCESS_READ_RSVD_OR_UNKNOWN,
    /** MSR write for a reserved/unknown/invalid MSR. */
    APICMSRACCESS_WRITE_RSVD_OR_UNKNOWN,
    /** MSR read for a write-only MSR. */
    APICMSRACCESS_READ_WRITE_ONLY,
    /** MSR write for a read-only MSR. */
    APICMSRACCESS_WRITE_READ_ONLY,
    /** MSR read to reserved bits. */
    APICMSRACCESS_READ_RSVD_BITS,
    /** MSR write to reserved bits. */
    APICMSRACCESS_WRITE_RSVD_BITS,
    /** MSR write with invalid value. */
    APICMSRACCESS_WRITE_INVALID,
    /** MSR write disallowed due to incompatible config. */
    APICMSRACCESS_WRITE_DISALLOWED_CONFIG,
    /** MSR read disallowed due to incompatible config. */
    APICMSRACCESS_READ_DISALLOWED_CONFIG,
    /** Count of enum members (don't use). */
    APICMSRACCESS_COUNT
} APICMSRACCESS;


/** @def APIC_CACHE_LINE_SIZE
 * Padding (in bytes) for aligning data in different cache lines. Present
 * generation x86 CPUs use 64-byte cache lines[1]. However, Intel NetBurst
 * architecture supposedly uses 128-byte cache lines[2]. Since 128 is a
 * multiple of 64, we use the larger one here.
 *
 * [1] - Intel spec "Table 11-1. Characteristics of the Caches, TLBs, Store
 *       Buffer, and Write Combining Buffer in Intel 64 and IA-32 Processors"
 * [2] - Intel spec. 8.10.6.7 "Place Locks and Semaphores in Aligned, 128-Byte
 *       Blocks of Memory".
 */
#define APIC_CACHE_LINE_SIZE              128

/**
 * APIC Pending-Interrupt Bitmap (PIB).
 */
typedef struct APICPIB
{
    uint64_t volatile au64VectorBitmap[4];
    uint32_t volatile fOutstandingNotification;
    uint8_t           au8Reserved[APIC_CACHE_LINE_SIZE - sizeof(uint32_t) - (sizeof(uint64_t) * 4)];
} APICPIB;
AssertCompileMemberOffset(APICPIB, fOutstandingNotification, 256 / 8);
AssertCompileSize(APICPIB, APIC_CACHE_LINE_SIZE);
/** Pointer to a pending-interrupt bitmap. */
typedef APICPIB *PAPICPIB;
/** Pointer to a const pending-interrupt bitmap. */
typedef const APICPIB *PCAPICPIB;

/**
 * APIC PDM instance data (per-VM).
 */
typedef struct APICDEV
{
    /** The MMIO handle. */
    IOMMMIOHANDLE               hMmio;
} APICDEV;
/** Pointer to an APIC device. */
typedef APICDEV *PAPICDEV;
/** Pointer to a const APIC device. */
typedef APICDEV const *PCAPICDEV;


/**
 * The APIC GVM instance data.
 */
typedef struct APICR0PERVM
{
    /** The ring-0 device instance. */
    PPDMDEVINSR0                pDevInsR0;
} APICR0PERVM;


/**
 * APIC VM Instance data.
 */
typedef struct APIC
{
    /** The ring-3 device instance. */
    PPDMDEVINSR3                pDevInsR3;

    /** @name The APIC pending-interrupt bitmap (PIB).
     * @{ */
    /** The host-context physical address of the PIB. */
    RTHCPHYS                    HCPhysApicPib;
    /** The ring-0 memory object of the PIB. */
    RTR0MEMOBJ                  hMemObjApicPibR0;
    /** The ring-3 mapping of the memory object of the PIB. */
    RTR0MEMOBJ                  hMapObjApicPibR0;
    /** The APIC PIB virtual address - R0 ptr. */
    R0PTRTYPE(void *)           pvApicPibR0;
    /** The APIC PIB virtual address - R3 ptr. */
    R3PTRTYPE(void *)           pvApicPibR3;
    /** The size of the page in bytes. */
    uint32_t                    cbApicPib;
    /** @} */

    /** @name Other miscellaneous data.
     * @{ */
    /** Whether full APIC register virtualization is enabled. */
    bool                        fVirtApicRegsEnabled;
    /** Whether posted-interrupt processing is enabled. */
    bool                        fPostedIntrsEnabled;
    /** Whether TSC-deadline timer mode is supported for the guest. */
    bool                        fSupportsTscDeadline;
    /** Whether this VM has an IO-APIC. */
    bool                        fIoApicPresent;
    /** Whether R0 is enabled or not (applies to MSR handling as well). */
    bool                        fR0Enabled;
    /** Whether RC is enabled or not (applies to MSR handling as well). */
    bool                        fRCEnabled;
    /** Whether Hyper-V x2APIC compatibility mode is enabled. */
    bool                        fHyperVCompatMode;
    /** Enable horrible macOS workaround where the ID register has the value
     * shifted up 24 bits to be compatible with buggy code in
     * i386_init.c/vstart().  Only applied if we're in typical macOS 64-bit
     * kernel load area and macOS kernel selector value (8), as we must not ever
     * apply this to the EFI code. */
    bool                        fMacOSWorkaround;
    /** The max supported APIC mode from CFGM.  */
    PDMAPICMODE                 enmMaxMode;
    /** @} */
} APIC;
/** Pointer to APIC VM instance data. */
typedef APIC *PAPIC;
/** Pointer to const APIC VM instance data. */
typedef APIC const *PCAPIC;
AssertCompileMemberAlignment(APIC, cbApicPib, 8);
AssertCompileSizeAlignment(APIC, 8);

/**
 * APIC VMCPU Instance data.
 */
typedef struct APICCPU
{
    /** @name The APIC page.
     * @{ */
    /** The host-context physical address of the page. */
    RTHCPHYS                    HCPhysApicPage;
    /** The ring-0 memory object of the page. */
    RTR0MEMOBJ                  hMemObjApicPageR0;
    /** The ring-3 mapping of the memory object of the page. */
    RTR0MEMOBJ                  hMapObjApicPageR0;
    /** The APIC page virtual address - R0 ptr. */
    R0PTRTYPE(void *)           pvApicPageR0;
    /** The APIC page virtual address - R3 ptr. */
    R3PTRTYPE(void *)           pvApicPageR3;
    /** The size of the page in bytes. */
    uint32_t                    cbApicPage;
    /** @} */

    /** @name Auxiliary state.
     * @{ */
    /** The error status register's internal state. */
    uint32_t                    uEsrInternal;
    /** The APIC base MSR.*/
    uint64_t volatile           uApicBaseMsr;
    /** @} */

    /** @name The pending-interrupt bitmaps (PIB).
     * @{ */
    /** The host-context physical address of the page. */
    RTHCPHYS                    HCPhysApicPib;
    /** The APIC PIB virtual address - R0 ptr. */
    R0PTRTYPE(void *)           pvApicPibR0;
    /** The APIC PIB virtual address - R3 ptr. */
    R3PTRTYPE(void *)           pvApicPibR3;
    /** The APIC PIB for level-sensitive interrupts. */
    APICPIB                     ApicPibLevel;
    /** @} */

    /** @name Other miscellaneous data.
     * @{ */
    /** Whether the LINT0 interrupt line is active. */
    bool volatile               fActiveLint0;
    /** Whether the LINT1 interrupt line is active. */
    bool volatile               fActiveLint1;
    /** Alignment padding. */
    uint8_t                     auAlignment2[6];
    /** The source tags corresponding to each interrupt vector (debugging). */
    uint32_t                    auSrcTags[256];
    /** @} */

    /** @name The APIC timer.
     * @{ */
    /** The timer. */
    TMTIMERHANDLE               hTimer;
    /** The time stamp when the timer was initialized.
     * @note Access protected by the timer critsect.  */
    uint64_t                    u64TimerInitial;
    /** Cache of timer initial count of the frequency hint to TM. */
    uint32_t                    uHintedTimerInitialCount;
    /** Cache of timer shift of the frequency hint to TM. */
    uint32_t                    uHintedTimerShift;
    /** The timer description. */
    char                        szTimerDesc[16];
    /** @} */

    /** @name Log Max counters
     * @{ */
    uint32_t                    cLogMaxAccessError;
    uint32_t                    cLogMaxSetApicBaseAddr;
    uint32_t                    cLogMaxGetApicBaseAddr;
    uint32_t                    uAlignment4;
    /** @} */

    /** @name APIC statistics.
     * @{ */
#ifdef VBOX_WITH_STATISTICS
    /** Number of MMIO reads in RZ. */
    STAMCOUNTER                 StatMmioReadRZ;
    /** Number of MMIO reads in R3. */
    STAMCOUNTER                 StatMmioReadR3;

    /** Number of MMIO writes in RZ. */
    STAMCOUNTER                 StatMmioWriteRZ;
    /** Number of MMIO writes in R3. */
    STAMCOUNTER                 StatMmioWriteR3;

    /** Number of MSR reads in RZ. */
    STAMCOUNTER                 StatMsrReadRZ;
    /** Number of MSR reads in R3. */
    STAMCOUNTER                 StatMsrReadR3;

    /** Number of MSR writes in RZ. */
    STAMCOUNTER                 StatMsrWriteRZ;
    /** Number of MSR writes in R3. */
    STAMCOUNTER                 StatMsrWriteR3;

    /** Profiling of APICUpdatePendingInterrupts().  */
    STAMPROFILE                 StatUpdatePendingIntrs;
    /** Profiling of apicPostInterrupt().  */
    STAMPROFILE                 StatPostIntr;
    /** Number of times an interrupt is already pending in
     *  apicPostInterrupts().*/
    STAMCOUNTER                 StatPostIntrAlreadyPending;
    /** Number of times the timer callback is invoked. */
    STAMCOUNTER                 StatTimerCallback;
    /** Number of times the TPR is written. */
    STAMCOUNTER                 StatTprWrite;
    /** Number of times the TPR is read. */
    STAMCOUNTER                 StatTprRead;
    /** Number of times the EOI is written. */
    STAMCOUNTER                 StatEoiWrite;
    /** Number of times TPR masks an interrupt in apicGetInterrupt(). */
    STAMCOUNTER                 StatMaskedByTpr;
    /** Number of times PPR masks an interrupt in apicGetInterrupt(). */
    STAMCOUNTER                 StatMaskedByPpr;
    /** Number of times the timer ICR is written. */
    STAMCOUNTER                 StatTimerIcrWrite;
    /** Number of times the ICR Lo (send IPI) is written. */
    STAMCOUNTER                 StatIcrLoWrite;
    /** Number of times the ICR Hi is written. */
    STAMCOUNTER                 StatIcrHiWrite;
    /** Number of times the full ICR (x2APIC send IPI) is written. */
    STAMCOUNTER                 StatIcrFullWrite;
    /** Number of times the DCR is written. */
    STAMCOUNTER                 StatDcrWrite;
    /** Number of times the DFR is written. */
    STAMCOUNTER                 StatDfrWrite;
    /** Number of times the LDR is written. */
    STAMCOUNTER                 StatLdrWrite;
    /** Number of times the APIC-ID MSR is read. */
    STAMCOUNTER                 StatIdMsrRead;
    /** Number of times the LVT timer is written. */
    STAMCOUNTER                 StatLvtTimerWrite;
#endif
    /** Number of apicPostInterrupt() calls. */
    STAMCOUNTER                 StatPostIntrCnt;
    /** Number of interrupts broken down by vector. */
    STAMCOUNTER                 aStatVectors[256];
    /** @} */
} APICCPU;
/** Pointer to APIC VMCPU instance data. */
typedef APICCPU *PAPICCPU;
/** Pointer to a const APIC VMCPU instance data. */
typedef APICCPU const *PCAPICCPU;
AssertCompileMemberAlignment(APICCPU, uApicBaseMsr, 8);

/**
 * APIC operating modes as returned by apicGetMode().
 *
 * The values match hardware states.
 * See Intel spec. 10.12.1 "Detecting and Enabling x2APIC Mode".
 */
typedef enum APICMODE
{
    APICMODE_DISABLED = 0,
    APICMODE_INVALID,
    APICMODE_XAPIC,
    APICMODE_X2APIC
} APICMODE;

/**
 * Gets the timer shift value.
 *
 * @returns The timer shift value.
 * @param   pXApicPage      The xAPIC page.
 */
DECLINLINE(uint8_t) apicGetTimerShift(PCXAPICPAGE pXApicPage)
{
    /* See Intel spec. 10.5.4 "APIC Timer". */
    uint32_t uShift = pXApicPage->timer_dcr.u.u2DivideValue0 | (pXApicPage->timer_dcr.u.u1DivideValue1 << 2);
    return (uShift + 1) & 7;
}


const char                   *apicGetModeName(APICMODE enmMode);
const char                   *apicGetDestFormatName(XAPICDESTFORMAT enmDestFormat);
const char                   *apicGetDeliveryModeName(XAPICDELIVERYMODE enmDeliveryMode);
const char                   *apicGetDestModeName(XAPICDESTMODE enmDestMode);
const char                   *apicGetTriggerModeName(XAPICTRIGGERMODE enmTriggerMode);
const char                   *apicGetDestShorthandName(XAPICDESTSHORTHAND enmDestShorthand);
const char                   *apicGetTimerModeName(XAPICTIMERMODE enmTimerMode);
void                          apicHintTimerFreq(PPDMDEVINS pDevIns, PAPICCPU pApicCpu, uint32_t uInitialCount, uint8_t uTimerShift);
APICMODE                      apicGetMode(uint64_t uApicBaseMsr);

DECLCALLBACK(VBOXSTRICTRC)    apicReadMmio(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void *pv, unsigned cb);
DECLCALLBACK(VBOXSTRICTRC)    apicWriteMmio(PPDMDEVINS pDevIns, void *pvUser, RTGCPHYS off, void const *pv, unsigned cb);

bool                          apicPostInterrupt(PVMCPUCC pVCpu, uint8_t uVector, XAPICTRIGGERMODE enmTriggerMode, uint32_t uSrcTag);
void                          apicStartTimer(PVMCPUCC pVCpu, uint32_t uInitialCount);
void                          apicClearInterruptFF(PVMCPUCC pVCpu, PDMAPICIRQ enmType);
void                          apicInitIpi(PVMCPUCC pVCpu);
void                          apicResetCpu(PVMCPUCC pVCpu, bool fResetApicBaseMsr);

DECLCALLBACK(int)             apicR3Construct(PPDMDEVINS pDevIns, int iInstance, PCFGMNODE pCfg);
DECLCALLBACK(int)             apicR3Destruct(PPDMDEVINS pDevIns);
DECLCALLBACK(void)            apicR3Relocate(PPDMDEVINS pDevIns, RTGCINTPTR offDelta);
DECLCALLBACK(void)            apicR3Reset(PPDMDEVINS pDevIns);
DECLCALLBACK(int)             apicR3InitComplete(PPDMDEVINS pDevIns);

/** @} */

#endif /* !VMM_INCLUDED_SRC_include_APICInternal_h */

