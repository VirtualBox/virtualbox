/* $Id: IEMAllCImplVmxInstr.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IEM - VT-x instruction implementation.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP   LOG_GROUP_IEM_VMX
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/iem.h>
#include <VBox/vmm/apic.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/gim.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/pgm.h>
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
# include <VBox/vmm/hmvmxinline.h>
#endif
#include <VBox/vmm/tm.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/disopcode-x86-amd64.h>
#include <iprt/asm-math.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/x86.h>

#include "IEMInline.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
/**
 * Gets the ModR/M, SIB and displacement byte(s) from decoded opcodes given their
 * relative offsets.
 */
# ifdef IEM_WITH_CODE_TLB /** @todo IEM TLB */
#  define IEM_MODRM_GET_U8(a_pVCpu, a_bModRm, a_offModRm)         do { a_bModRm  = 0; RT_NOREF(a_offModRm); } while (0)
#  define IEM_SIB_GET_U8(a_pVCpu, a_bSib, a_offSib)               do { a_bSib    = 0; RT_NOREF(a_offSib);  } while (0)
#  define IEM_DISP_GET_U16(a_pVCpu, a_u16Disp, a_offDisp)         do { a_u16Disp = 0; RT_NOREF(a_offDisp); } while (0)
#  define IEM_DISP_GET_S8_SX_U16(a_pVCpu, a_u16Disp, a_offDisp)   do { a_u16Disp = 0; RT_NOREF(a_offDisp); } while (0)
#  define IEM_DISP_GET_U32(a_pVCpu, a_u32Disp, a_offDisp)         do { a_u32Disp = 0; RT_NOREF(a_offDisp); } while (0)
#  define IEM_DISP_GET_S8_SX_U32(a_pVCpu, a_u32Disp, a_offDisp)   do { a_u32Disp = 0; RT_NOREF(a_offDisp); } while (0)
#  define IEM_DISP_GET_S32_SX_U64(a_pVCpu, a_u64Disp, a_offDisp)  do { a_u64Disp = 0; RT_NOREF(a_offDisp); } while (0)
#  define IEM_DISP_GET_S8_SX_U64(a_pVCpu, a_u64Disp, a_offDisp)   do { a_u64Disp = 0; RT_NOREF(a_offDisp); } while (0)
#  if 0
#   error "Implement me: Getting ModR/M, SIB, displacement needs to work even when instruction crosses a page boundary."
#  endif
# else  /* !IEM_WITH_CODE_TLB */
#  define IEM_MODRM_GET_U8(a_pVCpu, a_bModRm, a_offModRm) \
    do \
    { \
        Assert((a_offModRm) < (a_pVCpu)->iem.s.cbOpcode); \
        (a_bModRm) = (a_pVCpu)->iem.s.abOpcode[(a_offModRm)]; \
    } while (0)

#  define IEM_SIB_GET_U8(a_pVCpu, a_bSib, a_offSib)      IEM_MODRM_GET_U8(a_pVCpu, a_bSib, a_offSib)

#  define IEM_DISP_GET_U16(a_pVCpu, a_u16Disp, a_offDisp) \
    do \
    { \
        Assert((a_offDisp) + 1 < (a_pVCpu)->iem.s.cbOpcode); \
        uint8_t const bTmpLo = (a_pVCpu)->iem.s.abOpcode[(a_offDisp)]; \
        uint8_t const bTmpHi = (a_pVCpu)->iem.s.abOpcode[(a_offDisp) + 1]; \
        (a_u16Disp) = RT_MAKE_U16(bTmpLo, bTmpHi); \
    } while (0)

#  define IEM_DISP_GET_S8_SX_U16(a_pVCpu, a_u16Disp, a_offDisp) \
    do \
    { \
        Assert((a_offDisp) < (a_pVCpu)->iem.s.cbOpcode); \
        (a_u16Disp) = (int8_t)((a_pVCpu)->iem.s.abOpcode[(a_offDisp)]); \
    } while (0)

#  define IEM_DISP_GET_U32(a_pVCpu, a_u32Disp, a_offDisp) \
    do \
    { \
        Assert((a_offDisp) + 3 < (a_pVCpu)->iem.s.cbOpcode); \
        uint8_t const bTmp0 = (a_pVCpu)->iem.s.abOpcode[(a_offDisp)]; \
        uint8_t const bTmp1 = (a_pVCpu)->iem.s.abOpcode[(a_offDisp) + 1]; \
        uint8_t const bTmp2 = (a_pVCpu)->iem.s.abOpcode[(a_offDisp) + 2]; \
        uint8_t const bTmp3 = (a_pVCpu)->iem.s.abOpcode[(a_offDisp) + 3]; \
        (a_u32Disp) = RT_MAKE_U32_FROM_U8(bTmp0, bTmp1, bTmp2, bTmp3); \
    } while (0)

#  define IEM_DISP_GET_S8_SX_U32(a_pVCpu, a_u32Disp, a_offDisp) \
    do \
    { \
        Assert((a_offDisp) + 1 < (a_pVCpu)->iem.s.cbOpcode); \
        (a_u32Disp) = (int8_t)((a_pVCpu)->iem.s.abOpcode[(a_offDisp)]); \
    } while (0)

#  define IEM_DISP_GET_S8_SX_U64(a_pVCpu, a_u64Disp, a_offDisp) \
    do \
    { \
        Assert((a_offDisp) + 1 < (a_pVCpu)->iem.s.cbOpcode); \
        (a_u64Disp) = (int8_t)((a_pVCpu)->iem.s.abOpcode[(a_offDisp)]); \
    } while (0)

#  define IEM_DISP_GET_S32_SX_U64(a_pVCpu, a_u64Disp, a_offDisp) \
    do \
    { \
        Assert((a_offDisp) + 3 < (a_pVCpu)->iem.s.cbOpcode); \
        uint8_t const bTmp0 = (a_pVCpu)->iem.s.abOpcode[(a_offDisp)]; \
        uint8_t const bTmp1 = (a_pVCpu)->iem.s.abOpcode[(a_offDisp) + 1]; \
        uint8_t const bTmp2 = (a_pVCpu)->iem.s.abOpcode[(a_offDisp) + 2]; \
        uint8_t const bTmp3 = (a_pVCpu)->iem.s.abOpcode[(a_offDisp) + 3]; \
        (a_u64Disp) = (int32_t)RT_MAKE_U32_FROM_U8(bTmp0, bTmp1, bTmp2, bTmp3); \
    } while (0)
# endif /* !IEM_WITH_CODE_TLB */

/** Check for VMX instructions requiring to be in VMX operation.
 * @note Any changes here, check if IEMOP_HLP_IN_VMX_OPERATION needs updating. */
# define IEM_VMX_IN_VMX_OPERATION(a_pVCpu, a_szInstr, a_InsDiagPrefix) \
    do \
    { \
        if (IEM_VMX_IS_ROOT_MODE(a_pVCpu)) \
        { /* likely */ } \
        else \
        { \
            Log((a_szInstr ": Not in VMX operation (root mode) -> #UD\n")); \
            (a_pVCpu)->cpum.GstCtx.hwvirt.vmx.enmDiag = a_InsDiagPrefix##_VmxRoot; \
            return iemRaiseUndefinedOpcode(a_pVCpu); \
        } \
    } while (0)

/** Marks a VM-entry failure with a diagnostic reason, logs and returns. */
# define IEM_VMX_VMENTRY_FAILED_RET(a_pVCpu, a_pszInstr, a_pszFailure, a_VmxDiag) \
    do \
    { \
        LogRel(("%s: VM-entry failed! enmDiag=%u (%s) -> %s\n", (a_pszInstr), (a_VmxDiag), \
               HMGetVmxDiagDesc(a_VmxDiag), (a_pszFailure))); \
        (a_pVCpu)->cpum.GstCtx.hwvirt.vmx.enmDiag = (a_VmxDiag); \
        return VERR_VMX_VMENTRY_FAILED; \
    } while (0)

/** Marks a VM-entry failure with an return code, diagnostic reason, logs and
 *  returns. */
# define IEM_VMX_VMENTRY_FAILED_RET_2(a_pVCpu, a_pszInstr, a_pszFailure, a_VmxDiag, a_rc) \
    do \
    { \
        LogRel(("%s: VM-entry failed! rc=%Rrc enmDiag=%u (%s) -> %s\n", (a_pszInstr), (a_rc), (a_VmxDiag), \
               HMGetVmxDiagDesc(a_VmxDiag), (a_pszFailure))); \
        (a_pVCpu)->cpum.GstCtx.hwvirt.vmx.enmDiag = (a_VmxDiag); \
        return VERR_VMX_VMENTRY_FAILED; \
    } while (0)

/** Marks a VM-exit failure with a diagnostic reason and logs. */
# define IEM_VMX_VMEXIT_FAILED(a_pVCpu, a_uExitReason, a_pszFailure, a_VmxDiag) \
    do \
    { \
        LogRel(("VM-exit failed! uExitReason=%u enmDiag=%u (%s) -> %s\n", (a_uExitReason), (a_VmxDiag), \
               HMGetVmxDiagDesc(a_VmxDiag), (a_pszFailure))); \
        (a_pVCpu)->cpum.GstCtx.hwvirt.vmx.enmDiag  = (a_VmxDiag); \
    } while (0)

/** Marks a VM-exit failure with a diagnostic reason, logs and returns. */
# define IEM_VMX_VMEXIT_FAILED_RET(a_pVCpu, a_uExitReason, a_pszFailure, a_VmxDiag) \
    do \
    { \
        IEM_VMX_VMEXIT_FAILED(a_pVCpu, a_uExitReason, a_pszFailure, a_VmxDiag); \
        return VERR_VMX_VMEXIT_FAILED; \
    } while (0)


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** @todo NSTVMX: The following VM-exit intercepts are pending:
 *  VMX_EXIT_IO_SMI
 *  VMX_EXIT_SMI
 *  VMX_EXIT_GETSEC
 *  VMX_EXIT_RSM
 *  VMX_EXIT_MONITOR (APIC access VM-exit caused by MONITOR pending)
 *  VMX_EXIT_ERR_MACHINE_CHECK (we never need to raise this?)
 *  VMX_EXIT_VMFUNC
 *  VMX_EXIT_ENCLS
 *  VMX_EXIT_PML_FULL
 *  VMX_EXIT_XSAVES
 *  VMX_EXIT_XRSTORS
 */
/**
 * Map of VMCS field encodings to their virtual-VMCS structure offsets.
 *
 * The first array dimension is VMCS field encoding of Width OR'ed with Type and the
 * second dimension is the Index, see VMXVMCSFIELD.
 */
uint16_t const g_aoffVmcsMap[16][VMX_V_VMCS_MAX_INDEX + 1] =
{
    /* VMX_VMCSFIELD_WIDTH_16BIT | VMX_VMCSFIELD_TYPE_CONTROL: */
    {
        /*     0 */ RT_UOFFSETOF(VMXVVMCS, u16Vpid),
        /*     1 */ RT_UOFFSETOF(VMXVVMCS, u16PostIntNotifyVector),
        /*     2 */ RT_UOFFSETOF(VMXVVMCS, u16EptpIndex),
        /*     3 */ RT_UOFFSETOF(VMXVVMCS, u16HlatPrefixSize),
        /*  4-11 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 12-19 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 20-27 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 28-34 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX
    },
    /* VMX_VMCSFIELD_WIDTH_16BIT | VMX_VMCSFIELD_TYPE_VMEXIT_INFO: */
    {
        /*   0-7 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /*  8-15 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 16-23 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 24-31 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 32-34 */ UINT16_MAX, UINT16_MAX, UINT16_MAX
    },
    /* VMX_VMCSFIELD_WIDTH_16BIT | VMX_VMCSFIELD_TYPE_GUEST_STATE: */
    {
        /*     0 */ RT_UOFFSETOF(VMXVVMCS, GuestEs),
        /*     1 */ RT_UOFFSETOF(VMXVVMCS, GuestCs),
        /*     2 */ RT_UOFFSETOF(VMXVVMCS, GuestSs),
        /*     3 */ RT_UOFFSETOF(VMXVVMCS, GuestDs),
        /*     4 */ RT_UOFFSETOF(VMXVVMCS, GuestFs),
        /*     5 */ RT_UOFFSETOF(VMXVVMCS, GuestGs),
        /*     6 */ RT_UOFFSETOF(VMXVVMCS, GuestLdtr),
        /*     7 */ RT_UOFFSETOF(VMXVVMCS, GuestTr),
        /*     8 */ RT_UOFFSETOF(VMXVVMCS, u16GuestIntStatus),
        /*     9 */ RT_UOFFSETOF(VMXVVMCS, u16PmlIndex),
        /* 10-17 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 18-25 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 26-33 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /*    34 */ UINT16_MAX
    },
    /* VMX_VMCSFIELD_WIDTH_16BIT | VMX_VMCSFIELD_TYPE_HOST_STATE: */
    {
        /*     0 */ RT_UOFFSETOF(VMXVVMCS, HostEs),
        /*     1 */ RT_UOFFSETOF(VMXVVMCS, HostCs),
        /*     2 */ RT_UOFFSETOF(VMXVVMCS, HostSs),
        /*     3 */ RT_UOFFSETOF(VMXVVMCS, HostDs),
        /*     4 */ RT_UOFFSETOF(VMXVVMCS, HostFs),
        /*     5 */ RT_UOFFSETOF(VMXVVMCS, HostGs),
        /*     6 */ RT_UOFFSETOF(VMXVVMCS, HostTr),
        /*  7-14 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 15-22 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 23-30 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 31-34 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX
    },
    /* VMX_VMCSFIELD_WIDTH_64BIT | VMX_VMCSFIELD_TYPE_CONTROL: */
    {
        /*     0 */ RT_UOFFSETOF(VMXVVMCS, u64AddrIoBitmapA),
        /*     1 */ RT_UOFFSETOF(VMXVVMCS, u64AddrIoBitmapB),
        /*     2 */ RT_UOFFSETOF(VMXVVMCS, u64AddrMsrBitmap),
        /*     3 */ RT_UOFFSETOF(VMXVVMCS, u64AddrExitMsrStore),
        /*     4 */ RT_UOFFSETOF(VMXVVMCS, u64AddrExitMsrLoad),
        /*     5 */ RT_UOFFSETOF(VMXVVMCS, u64AddrEntryMsrLoad),
        /*     6 */ RT_UOFFSETOF(VMXVVMCS, u64ExecVmcsPtr),
        /*     7 */ RT_UOFFSETOF(VMXVVMCS, u64AddrPml),
        /*     8 */ RT_UOFFSETOF(VMXVVMCS, u64TscOffset),
        /*     9 */ RT_UOFFSETOF(VMXVVMCS, u64AddrVirtApic),
        /*    10 */ RT_UOFFSETOF(VMXVVMCS, u64AddrApicAccess),
        /*    11 */ RT_UOFFSETOF(VMXVVMCS, u64AddrPostedIntDesc),
        /*    12 */ RT_UOFFSETOF(VMXVVMCS, u64VmFuncCtls),
        /*    13 */ RT_UOFFSETOF(VMXVVMCS, u64EptPtr),
        /*    14 */ RT_UOFFSETOF(VMXVVMCS, u64EoiExitBitmap0),
        /*    15 */ RT_UOFFSETOF(VMXVVMCS, u64EoiExitBitmap1),
        /*    16 */ RT_UOFFSETOF(VMXVVMCS, u64EoiExitBitmap2),
        /*    17 */ RT_UOFFSETOF(VMXVVMCS, u64EoiExitBitmap3),
        /*    18 */ RT_UOFFSETOF(VMXVVMCS, u64AddrEptpList),
        /*    19 */ RT_UOFFSETOF(VMXVVMCS, u64AddrVmreadBitmap),
        /*    20 */ RT_UOFFSETOF(VMXVVMCS, u64AddrVmwriteBitmap),
        /*    21 */ RT_UOFFSETOF(VMXVVMCS, u64AddrXcptVeInfo),
        /*    22 */ RT_UOFFSETOF(VMXVVMCS, u64XssExitBitmap),
        /*    23 */ RT_UOFFSETOF(VMXVVMCS, u64EnclsExitBitmap),
        /*    24 */ RT_UOFFSETOF(VMXVVMCS, u64SppTablePtr),
        /*    25 */ RT_UOFFSETOF(VMXVVMCS, u64TscMultiplier),
        /*    26 */ RT_UOFFSETOF(VMXVVMCS, u64ProcCtls3),
        /*    27 */ RT_UOFFSETOF(VMXVVMCS, u64EnclvExitBitmap),
        /*    28 */ UINT16_MAX,
        /*    29 */ UINT16_MAX,
        /*    30 */ UINT16_MAX,
        /*    31 */ RT_UOFFSETOF(VMXVVMCS, u64PconfigExitBitmap),
        /*    32 */ RT_UOFFSETOF(VMXVVMCS, u64HlatPtr),
        /*    33 */ UINT16_MAX,
        /*    34 */ RT_UOFFSETOF(VMXVVMCS, u64ExitCtls2)
    },
    /* VMX_VMCSFIELD_WIDTH_64BIT | VMX_VMCSFIELD_TYPE_VMEXIT_INFO: */
    {
        /*     0 */ RT_UOFFSETOF(VMXVVMCS, u64RoGuestPhysAddr),
        /*   1-8 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /*  9-16 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 17-24 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 25-32 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 33-34*/  UINT16_MAX, UINT16_MAX
    },
    /* VMX_VMCSFIELD_WIDTH_64BIT | VMX_VMCSFIELD_TYPE_GUEST_STATE: */
    {
        /*     0 */ RT_UOFFSETOF(VMXVVMCS, u64VmcsLinkPtr),
        /*     1 */ RT_UOFFSETOF(VMXVVMCS, u64GuestDebugCtlMsr),
        /*     2 */ RT_UOFFSETOF(VMXVVMCS, u64GuestPatMsr),
        /*     3 */ RT_UOFFSETOF(VMXVVMCS, u64GuestEferMsr),
        /*     4 */ RT_UOFFSETOF(VMXVVMCS, u64GuestPerfGlobalCtlMsr),
        /*     5 */ RT_UOFFSETOF(VMXVVMCS, u64GuestPdpte0),
        /*     6 */ RT_UOFFSETOF(VMXVVMCS, u64GuestPdpte1),
        /*     7 */ RT_UOFFSETOF(VMXVVMCS, u64GuestPdpte2),
        /*     8 */ RT_UOFFSETOF(VMXVVMCS, u64GuestPdpte3),
        /*     9 */ RT_UOFFSETOF(VMXVVMCS, u64GuestBndcfgsMsr),
        /*    10 */ RT_UOFFSETOF(VMXVVMCS, u64GuestRtitCtlMsr),
        /*    11 */ UINT16_MAX,
        /*    12 */ RT_UOFFSETOF(VMXVVMCS, u64GuestPkrsMsr),
        /* 13-20 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 21-28 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 29-34 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX
    },
    /* VMX_VMCSFIELD_WIDTH_64BIT | VMX_VMCSFIELD_TYPE_HOST_STATE: */
    {
        /*     0 */ RT_UOFFSETOF(VMXVVMCS, u64HostPatMsr),
        /*     1 */ RT_UOFFSETOF(VMXVVMCS, u64HostEferMsr),
        /*     2 */ RT_UOFFSETOF(VMXVVMCS, u64HostPerfGlobalCtlMsr),
        /*     3 */ RT_UOFFSETOF(VMXVVMCS, u64HostPkrsMsr),
        /*  4-11 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 12-19 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 20-27 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 28-34 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX
    },
    /* VMX_VMCSFIELD_WIDTH_32BIT | VMX_VMCSFIELD_TYPE_CONTROL: */
    {
        /*     0 */ RT_UOFFSETOF(VMXVVMCS, u32PinCtls),
        /*     1 */ RT_UOFFSETOF(VMXVVMCS, u32ProcCtls),
        /*     2 */ RT_UOFFSETOF(VMXVVMCS, u32XcptBitmap),
        /*     3 */ RT_UOFFSETOF(VMXVVMCS, u32XcptPFMask),
        /*     4 */ RT_UOFFSETOF(VMXVVMCS, u32XcptPFMatch),
        /*     5 */ RT_UOFFSETOF(VMXVVMCS, u32Cr3TargetCount),
        /*     6 */ RT_UOFFSETOF(VMXVVMCS, u32ExitCtls),
        /*     7 */ RT_UOFFSETOF(VMXVVMCS, u32ExitMsrStoreCount),
        /*     8 */ RT_UOFFSETOF(VMXVVMCS, u32ExitMsrLoadCount),
        /*     9 */ RT_UOFFSETOF(VMXVVMCS, u32EntryCtls),
        /*    10 */ RT_UOFFSETOF(VMXVVMCS, u32EntryMsrLoadCount),
        /*    11 */ RT_UOFFSETOF(VMXVVMCS, u32EntryIntInfo),
        /*    12 */ RT_UOFFSETOF(VMXVVMCS, u32EntryXcptErrCode),
        /*    13 */ RT_UOFFSETOF(VMXVVMCS, u32EntryInstrLen),
        /*    14 */ RT_UOFFSETOF(VMXVVMCS, u32TprThreshold),
        /*    15 */ RT_UOFFSETOF(VMXVVMCS, u32ProcCtls2),
        /*    16 */ RT_UOFFSETOF(VMXVVMCS, u32PleGap),
        /*    17 */ RT_UOFFSETOF(VMXVVMCS, u32PleWindow),
        /* 18-25 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 26-33 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /*    34 */ UINT16_MAX
    },
    /* VMX_VMCSFIELD_WIDTH_32BIT | VMX_VMCSFIELD_TYPE_VMEXIT_INFO: */
    {
        /*     0 */ RT_UOFFSETOF(VMXVVMCS, u32RoVmInstrError),
        /*     1 */ RT_UOFFSETOF(VMXVVMCS, u32RoExitReason),
        /*     2 */ RT_UOFFSETOF(VMXVVMCS, u32RoExitIntInfo),
        /*     3 */ RT_UOFFSETOF(VMXVVMCS, u32RoExitIntErrCode),
        /*     4 */ RT_UOFFSETOF(VMXVVMCS, u32RoIdtVectoringInfo),
        /*     5 */ RT_UOFFSETOF(VMXVVMCS, u32RoIdtVectoringErrCode),
        /*     6 */ RT_UOFFSETOF(VMXVVMCS, u32RoExitInstrLen),
        /*     7 */ RT_UOFFSETOF(VMXVVMCS, u32RoExitInstrInfo),
        /*  8-15 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 16-23 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 24-31 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 32-34 */ UINT16_MAX, UINT16_MAX, UINT16_MAX
    },
    /* VMX_VMCSFIELD_WIDTH_32BIT | VMX_VMCSFIELD_TYPE_GUEST_STATE: */
    {
        /*     0 */ RT_UOFFSETOF(VMXVVMCS, u32GuestEsLimit),
        /*     1 */ RT_UOFFSETOF(VMXVVMCS, u32GuestCsLimit),
        /*     2 */ RT_UOFFSETOF(VMXVVMCS, u32GuestSsLimit),
        /*     3 */ RT_UOFFSETOF(VMXVVMCS, u32GuestDsLimit),
        /*     4 */ RT_UOFFSETOF(VMXVVMCS, u32GuestFsLimit),
        /*     5 */ RT_UOFFSETOF(VMXVVMCS, u32GuestGsLimit),
        /*     6 */ RT_UOFFSETOF(VMXVVMCS, u32GuestLdtrLimit),
        /*     7 */ RT_UOFFSETOF(VMXVVMCS, u32GuestTrLimit),
        /*     8 */ RT_UOFFSETOF(VMXVVMCS, u32GuestGdtrLimit),
        /*     9 */ RT_UOFFSETOF(VMXVVMCS, u32GuestIdtrLimit),
        /*    10 */ RT_UOFFSETOF(VMXVVMCS, u32GuestEsAttr),
        /*    11 */ RT_UOFFSETOF(VMXVVMCS, u32GuestCsAttr),
        /*    12 */ RT_UOFFSETOF(VMXVVMCS, u32GuestSsAttr),
        /*    13 */ RT_UOFFSETOF(VMXVVMCS, u32GuestDsAttr),
        /*    14 */ RT_UOFFSETOF(VMXVVMCS, u32GuestFsAttr),
        /*    15 */ RT_UOFFSETOF(VMXVVMCS, u32GuestGsAttr),
        /*    16 */ RT_UOFFSETOF(VMXVVMCS, u32GuestLdtrAttr),
        /*    17 */ RT_UOFFSETOF(VMXVVMCS, u32GuestTrAttr),
        /*    18 */ RT_UOFFSETOF(VMXVVMCS, u32GuestIntrState),
        /*    19 */ RT_UOFFSETOF(VMXVVMCS, u32GuestActivityState),
        /*    20 */ RT_UOFFSETOF(VMXVVMCS, u32GuestSmBase),
        /*    21 */ RT_UOFFSETOF(VMXVVMCS, u32GuestSysenterCS),
        /*    22 */ UINT16_MAX,
        /*    23 */ RT_UOFFSETOF(VMXVVMCS, u32PreemptTimer),
        /* 24-31 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 32-34 */ UINT16_MAX, UINT16_MAX, UINT16_MAX
    },
    /* VMX_VMCSFIELD_WIDTH_32BIT | VMX_VMCSFIELD_TYPE_HOST_STATE: */
    {
        /*     0 */ RT_UOFFSETOF(VMXVVMCS, u32HostSysenterCs),
        /*   1-8 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /*  9-16 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 17-24 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 25-32 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 33-34 */ UINT16_MAX, UINT16_MAX
    },
    /* VMX_VMCSFIELD_WIDTH_NATURAL | VMX_VMCSFIELD_TYPE_CONTROL: */
    {
        /*     0 */ RT_UOFFSETOF(VMXVVMCS, u64Cr0Mask),
        /*     1 */ RT_UOFFSETOF(VMXVVMCS, u64Cr4Mask),
        /*     2 */ RT_UOFFSETOF(VMXVVMCS, u64Cr0ReadShadow),
        /*     3 */ RT_UOFFSETOF(VMXVVMCS, u64Cr4ReadShadow),
        /*     4 */ RT_UOFFSETOF(VMXVVMCS, u64Cr3Target0),
        /*     5 */ RT_UOFFSETOF(VMXVVMCS, u64Cr3Target1),
        /*     6 */ RT_UOFFSETOF(VMXVVMCS, u64Cr3Target2),
        /*     7 */ RT_UOFFSETOF(VMXVVMCS, u64Cr3Target3),
        /*  8-15 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 16-23 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 24-27 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 32-34 */ UINT16_MAX, UINT16_MAX, UINT16_MAX
    },
    /* VMX_VMCSFIELD_WIDTH_NATURAL | VMX_VMCSFIELD_TYPE_VMEXIT_INFO: */
    {
        /*     0 */ RT_UOFFSETOF(VMXVVMCS, u64RoExitQual),
        /*     1 */ RT_UOFFSETOF(VMXVVMCS, u64RoIoRcx),
        /*     2 */ RT_UOFFSETOF(VMXVVMCS, u64RoIoRsi),
        /*     3 */ RT_UOFFSETOF(VMXVVMCS, u64RoIoRdi),
        /*     4 */ RT_UOFFSETOF(VMXVVMCS, u64RoIoRip),
        /*     5 */ RT_UOFFSETOF(VMXVVMCS, u64RoGuestLinearAddr),
        /*  6-13 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 14-21 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 22-29 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 30-34 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX
    },
    /* VMX_VMCSFIELD_WIDTH_NATURAL | VMX_VMCSFIELD_TYPE_GUEST_STATE: */
    {
        /*     0 */ RT_UOFFSETOF(VMXVVMCS, u64GuestCr0),
        /*     1 */ RT_UOFFSETOF(VMXVVMCS, u64GuestCr3),
        /*     2 */ RT_UOFFSETOF(VMXVVMCS, u64GuestCr4),
        /*     3 */ RT_UOFFSETOF(VMXVVMCS, u64GuestEsBase),
        /*     4 */ RT_UOFFSETOF(VMXVVMCS, u64GuestCsBase),
        /*     5 */ RT_UOFFSETOF(VMXVVMCS, u64GuestSsBase),
        /*     6 */ RT_UOFFSETOF(VMXVVMCS, u64GuestDsBase),
        /*     7 */ RT_UOFFSETOF(VMXVVMCS, u64GuestFsBase),
        /*     8 */ RT_UOFFSETOF(VMXVVMCS, u64GuestGsBase),
        /*     9 */ RT_UOFFSETOF(VMXVVMCS, u64GuestLdtrBase),
        /*    10 */ RT_UOFFSETOF(VMXVVMCS, u64GuestTrBase),
        /*    11 */ RT_UOFFSETOF(VMXVVMCS, u64GuestGdtrBase),
        /*    12 */ RT_UOFFSETOF(VMXVVMCS, u64GuestIdtrBase),
        /*    13 */ RT_UOFFSETOF(VMXVVMCS, u64GuestDr7),
        /*    14 */ RT_UOFFSETOF(VMXVVMCS, u64GuestRsp),
        /*    15 */ RT_UOFFSETOF(VMXVVMCS, u64GuestRip),
        /*    16 */ RT_UOFFSETOF(VMXVVMCS, u64GuestRFlags),
        /*    17 */ RT_UOFFSETOF(VMXVVMCS, u64GuestPendingDbgXcpts),
        /*    18 */ RT_UOFFSETOF(VMXVVMCS, u64GuestSysenterEsp),
        /*    19 */ RT_UOFFSETOF(VMXVVMCS, u64GuestSysenterEip),
        /*    20 */ RT_UOFFSETOF(VMXVVMCS, u64GuestSCetMsr),
        /*    21 */ RT_UOFFSETOF(VMXVVMCS, u64GuestSsp),
        /*    22 */ RT_UOFFSETOF(VMXVVMCS, u64GuestIntrSspTableAddrMsr),
        /* 23-27 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 31-34 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX
    },
    /* VMX_VMCSFIELD_WIDTH_NATURAL | VMX_VMCSFIELD_TYPE_HOST_STATE: */
    {
        /*     0 */ RT_UOFFSETOF(VMXVVMCS, u64HostCr0),
        /*     1 */ RT_UOFFSETOF(VMXVVMCS, u64HostCr3),
        /*     2 */ RT_UOFFSETOF(VMXVVMCS, u64HostCr4),
        /*     3 */ RT_UOFFSETOF(VMXVVMCS, u64HostFsBase),
        /*     4 */ RT_UOFFSETOF(VMXVVMCS, u64HostGsBase),
        /*     5 */ RT_UOFFSETOF(VMXVVMCS, u64HostTrBase),
        /*     6 */ RT_UOFFSETOF(VMXVVMCS, u64HostGdtrBase),
        /*     7 */ RT_UOFFSETOF(VMXVVMCS, u64HostIdtrBase),
        /*     8 */ RT_UOFFSETOF(VMXVVMCS, u64HostSysenterEsp),
        /*     9 */ RT_UOFFSETOF(VMXVVMCS, u64HostSysenterEip),
        /*    10 */ RT_UOFFSETOF(VMXVVMCS, u64HostRsp),
        /*    11 */ RT_UOFFSETOF(VMXVVMCS, u64HostRip),
        /*    12 */ RT_UOFFSETOF(VMXVVMCS, u64HostSCetMsr),
        /*    13 */ RT_UOFFSETOF(VMXVVMCS, u64HostSsp),
        /*    14 */ RT_UOFFSETOF(VMXVVMCS, u64HostIntrSspTableAddrMsr),
        /* 15-22 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 23-30 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX,
        /* 31-34 */ UINT16_MAX, UINT16_MAX, UINT16_MAX, UINT16_MAX
    }
};


/**
 * Gets a host selector from the VMCS.
 *
 * @param   pVmcs       Pointer to the virtual VMCS.
 * @param   iSelReg     The index of the segment register (X86_SREG_XXX).
 */
DECLINLINE(RTSEL) iemVmxVmcsGetHostSelReg(PCVMXVVMCS pVmcs, uint8_t iSegReg)
{
    Assert(iSegReg < X86_SREG_COUNT);
    RTSEL HostSel;
    uint8_t  const  uWidth     = VMX_VMCSFIELD_WIDTH_16BIT;
    uint8_t  const  uType      = VMX_VMCSFIELD_TYPE_HOST_STATE;
    uint8_t  const  uWidthType = (uWidth << 2) | uType;
    uint8_t  const  uIndex     = iSegReg + RT_BF_GET(VMX_VMCS16_HOST_ES_SEL, VMX_BF_VMCSFIELD_INDEX);
    Assert(uIndex <= VMX_V_VMCS_MAX_INDEX);
    uint16_t const  offField   = g_aoffVmcsMap[uWidthType][uIndex];
    uint8_t  const *pbVmcs     = (uint8_t *)pVmcs;
    uint8_t  const *pbField    = pbVmcs + offField;
    HostSel = *(uint16_t *)pbField;
    return HostSel;
}


/**
 * Sets a guest segment register in the VMCS.
 *
 * @param   pVmcs       Pointer to the virtual VMCS.
 * @param   iSegReg     The index of the segment register (X86_SREG_XXX).
 * @param   pSelReg     Pointer to the segment register.
 */
static void iemVmxVmcsSetGuestSegReg(PCVMXVVMCS pVmcs, uint8_t iSegReg, PCCPUMSELREG pSelReg) RT_NOEXCEPT
{
    Assert(pSelReg);
    Assert(iSegReg < X86_SREG_COUNT);

    /* Selector. */
    {
        uint8_t  const  uWidth     = VMX_VMCSFIELD_WIDTH_16BIT;
        uint8_t  const  uType      = VMX_VMCSFIELD_TYPE_GUEST_STATE;
        uint8_t  const  uWidthType = (uWidth << 2) | uType;
        uint8_t  const  uIndex     = iSegReg + RT_BF_GET(VMX_VMCS16_GUEST_ES_SEL, VMX_BF_VMCSFIELD_INDEX);
        Assert(uIndex <= VMX_V_VMCS_MAX_INDEX);
        uint16_t const  offField   = g_aoffVmcsMap[uWidthType][uIndex];
        uint8_t        *pbVmcs     = (uint8_t *)pVmcs;
        uint8_t        *pbField    = pbVmcs + offField;
        *(uint16_t *)pbField = pSelReg->Sel;
    }

    /* Limit. */
    {
        uint8_t  const  uWidth     = VMX_VMCSFIELD_WIDTH_32BIT;
        uint8_t  const  uType      = VMX_VMCSFIELD_TYPE_GUEST_STATE;
        uint8_t  const  uWidthType = (uWidth << 2) | uType;
        uint8_t  const  uIndex     = iSegReg + RT_BF_GET(VMX_VMCS32_GUEST_ES_LIMIT, VMX_BF_VMCSFIELD_INDEX);
        Assert(uIndex <= VMX_V_VMCS_MAX_INDEX);
        uint16_t const  offField   = g_aoffVmcsMap[uWidthType][uIndex];
        uint8_t        *pbVmcs     = (uint8_t *)pVmcs;
        uint8_t        *pbField    = pbVmcs + offField;
        *(uint32_t *)pbField = pSelReg->u32Limit;
    }

    /* Base. */
    {
        uint8_t  const  uWidth     = VMX_VMCSFIELD_WIDTH_NATURAL;
        uint8_t  const  uType      = VMX_VMCSFIELD_TYPE_GUEST_STATE;
        uint8_t  const  uWidthType = (uWidth << 2) | uType;
        uint8_t  const  uIndex     = iSegReg + RT_BF_GET(VMX_VMCS_GUEST_ES_BASE, VMX_BF_VMCSFIELD_INDEX);
        Assert(uIndex <= VMX_V_VMCS_MAX_INDEX);
        uint16_t const  offField   = g_aoffVmcsMap[uWidthType][uIndex];
        uint8_t  const *pbVmcs     = (uint8_t *)pVmcs;
        uint8_t  const *pbField    = pbVmcs + offField;
        *(uint64_t *)pbField = pSelReg->u64Base;
    }

    /* Attributes. */
    {
        uint32_t const fValidAttrMask = X86DESCATTR_TYPE | X86DESCATTR_DT  | X86DESCATTR_DPL | X86DESCATTR_P
                                      | X86DESCATTR_AVL  | X86DESCATTR_L   | X86DESCATTR_D   | X86DESCATTR_G
                                      | X86DESCATTR_UNUSABLE;
        uint8_t  const  uWidth     = VMX_VMCSFIELD_WIDTH_32BIT;
        uint8_t  const  uType      = VMX_VMCSFIELD_TYPE_GUEST_STATE;
        uint8_t  const  uWidthType = (uWidth << 2) | uType;
        uint8_t  const  uIndex     = iSegReg + RT_BF_GET(VMX_VMCS32_GUEST_ES_ACCESS_RIGHTS, VMX_BF_VMCSFIELD_INDEX);
        Assert(uIndex <= VMX_V_VMCS_MAX_INDEX);
        uint16_t const  offField   = g_aoffVmcsMap[uWidthType][uIndex];
        uint8_t        *pbVmcs     = (uint8_t *)pVmcs;
        uint8_t        *pbField    = pbVmcs + offField;
        *(uint32_t *)pbField       = pSelReg->Attr.u & fValidAttrMask;
    }
}


/**
 * Gets a guest segment register from the VMCS.
 *
 * @returns VBox status code.
 * @param   pVmcs       Pointer to the virtual VMCS.
 * @param   iSegReg     The index of the segment register (X86_SREG_XXX).
 * @param   pSelReg     Where to store the segment register (only updated when
 *                      VINF_SUCCESS is returned).
 *
 * @remarks Warning! This does not validate the contents of the retrieved segment
 *          register.
 */
static int iemVmxVmcsGetGuestSegReg(PCVMXVVMCS pVmcs, uint8_t iSegReg, PCPUMSELREG pSelReg) RT_NOEXCEPT
{
    Assert(pSelReg);
    Assert(iSegReg < X86_SREG_COUNT);

    /* Selector. */
    uint16_t u16Sel;
    {
        uint8_t  const  uWidth     = VMX_VMCSFIELD_WIDTH_16BIT;
        uint8_t  const  uType      = VMX_VMCSFIELD_TYPE_GUEST_STATE;
        uint8_t  const  uWidthType = (uWidth << 2) | uType;
        uint8_t  const  uIndex     = iSegReg + RT_BF_GET(VMX_VMCS16_GUEST_ES_SEL, VMX_BF_VMCSFIELD_INDEX);
        AssertReturn(uIndex <= VMX_V_VMCS_MAX_INDEX, VERR_IEM_IPE_3);
        uint16_t const  offField   = g_aoffVmcsMap[uWidthType][uIndex];
        uint8_t  const *pbVmcs     = (uint8_t *)pVmcs;
        uint8_t  const *pbField    = pbVmcs + offField;
        u16Sel = *(uint16_t *)pbField;
    }

    /* Limit. */
    uint32_t u32Limit;
    {
        uint8_t  const  uWidth     = VMX_VMCSFIELD_WIDTH_32BIT;
        uint8_t  const  uType      = VMX_VMCSFIELD_TYPE_GUEST_STATE;
        uint8_t  const  uWidthType = (uWidth << 2) | uType;
        uint8_t  const  uIndex     = iSegReg + RT_BF_GET(VMX_VMCS32_GUEST_ES_LIMIT, VMX_BF_VMCSFIELD_INDEX);
        AssertReturn(uIndex <= VMX_V_VMCS_MAX_INDEX, VERR_IEM_IPE_3);
        uint16_t const  offField   = g_aoffVmcsMap[uWidthType][uIndex];
        uint8_t  const *pbVmcs     = (uint8_t *)pVmcs;
        uint8_t  const *pbField    = pbVmcs + offField;
        u32Limit = *(uint32_t *)pbField;
    }

    /* Base. */
    uint64_t u64Base;
    {
        uint8_t  const  uWidth     = VMX_VMCSFIELD_WIDTH_NATURAL;
        uint8_t  const  uType      = VMX_VMCSFIELD_TYPE_GUEST_STATE;
        uint8_t  const  uWidthType = (uWidth << 2) | uType;
        uint8_t  const  uIndex     = iSegReg + RT_BF_GET(VMX_VMCS_GUEST_ES_BASE, VMX_BF_VMCSFIELD_INDEX);
        AssertReturn(uIndex <= VMX_V_VMCS_MAX_INDEX, VERR_IEM_IPE_3);
        uint16_t const  offField   = g_aoffVmcsMap[uWidthType][uIndex];
        uint8_t  const *pbVmcs     = (uint8_t *)pVmcs;
        uint8_t  const *pbField    = pbVmcs + offField;
        u64Base = *(uint64_t *)pbField;
        /** @todo NSTVMX: Should we zero out high bits here for 32-bit virtual CPUs? */
    }

    /* Attributes. */
    uint32_t u32Attr;
    {
        uint8_t  const  uWidth     = VMX_VMCSFIELD_WIDTH_32BIT;
        uint8_t  const  uType      = VMX_VMCSFIELD_TYPE_GUEST_STATE;
        uint8_t  const  uWidthType = (uWidth << 2) | uType;
        uint8_t  const  uIndex     = iSegReg + RT_BF_GET(VMX_VMCS32_GUEST_ES_ACCESS_RIGHTS, VMX_BF_VMCSFIELD_INDEX);
        AssertReturn(uIndex <= VMX_V_VMCS_MAX_INDEX, VERR_IEM_IPE_3);
        uint16_t const  offField   = g_aoffVmcsMap[uWidthType][uIndex];
        uint8_t  const *pbVmcs     = (uint8_t *)pVmcs;
        uint8_t  const *pbField    = pbVmcs + offField;
        u32Attr = *(uint32_t *)pbField;
    }

    pSelReg->Sel      = u16Sel;
    pSelReg->ValidSel = u16Sel;
    pSelReg->fFlags   = CPUMSELREG_FLAGS_VALID;
    pSelReg->u32Limit = u32Limit;
    pSelReg->u64Base  = u64Base;
    pSelReg->Attr.u   = u32Attr;
    return VINF_SUCCESS;
}


/**
 * Converts an IEM exception event type to a VMX event type.
 *
 * @returns The VMX event type.
 * @param   uVector     The interrupt / exception vector.
 * @param   fFlags      The IEM event flag (see IEM_XCPT_FLAGS_XXX).
 */
DECLINLINE(uint8_t) iemVmxGetEventType(uint32_t uVector, uint32_t fFlags)
{
    /* Paranoia (callers may use these interchangeably). */
    AssertCompile(VMX_EXIT_INT_INFO_TYPE_NMI          == VMX_IDT_VECTORING_INFO_TYPE_NMI);
    AssertCompile(VMX_EXIT_INT_INFO_TYPE_HW_XCPT      == VMX_IDT_VECTORING_INFO_TYPE_HW_XCPT);
    AssertCompile(VMX_EXIT_INT_INFO_TYPE_EXT_INT      == VMX_IDT_VECTORING_INFO_TYPE_EXT_INT);
    AssertCompile(VMX_EXIT_INT_INFO_TYPE_SW_XCPT      == VMX_IDT_VECTORING_INFO_TYPE_SW_XCPT);
    AssertCompile(VMX_EXIT_INT_INFO_TYPE_SW_INT       == VMX_IDT_VECTORING_INFO_TYPE_SW_INT);
    AssertCompile(VMX_EXIT_INT_INFO_TYPE_PRIV_SW_XCPT == VMX_IDT_VECTORING_INFO_TYPE_PRIV_SW_XCPT);
    AssertCompile(VMX_EXIT_INT_INFO_TYPE_NMI          == VMX_ENTRY_INT_INFO_TYPE_NMI);
    AssertCompile(VMX_EXIT_INT_INFO_TYPE_HW_XCPT      == VMX_ENTRY_INT_INFO_TYPE_HW_XCPT);
    AssertCompile(VMX_EXIT_INT_INFO_TYPE_EXT_INT      == VMX_ENTRY_INT_INFO_TYPE_EXT_INT);
    AssertCompile(VMX_EXIT_INT_INFO_TYPE_SW_XCPT      == VMX_ENTRY_INT_INFO_TYPE_SW_XCPT);
    AssertCompile(VMX_EXIT_INT_INFO_TYPE_SW_INT       == VMX_ENTRY_INT_INFO_TYPE_SW_INT);
    AssertCompile(VMX_EXIT_INT_INFO_TYPE_PRIV_SW_XCPT == VMX_ENTRY_INT_INFO_TYPE_PRIV_SW_XCPT);

    if (fFlags & IEM_XCPT_FLAGS_T_CPU_XCPT)
    {
        if (uVector == X86_XCPT_NMI)
            return VMX_EXIT_INT_INFO_TYPE_NMI;
        return VMX_EXIT_INT_INFO_TYPE_HW_XCPT;
    }

    if (fFlags & IEM_XCPT_FLAGS_T_SOFT_INT)
    {
        if (fFlags & (IEM_XCPT_FLAGS_BP_INSTR | IEM_XCPT_FLAGS_OF_INSTR))
            return VMX_EXIT_INT_INFO_TYPE_SW_XCPT;
        if (fFlags & IEM_XCPT_FLAGS_ICEBP_INSTR)
            return VMX_EXIT_INT_INFO_TYPE_PRIV_SW_XCPT;
        return VMX_EXIT_INT_INFO_TYPE_SW_INT;
    }

    Assert(fFlags & IEM_XCPT_FLAGS_T_EXT_INT);
    return VMX_EXIT_INT_INFO_TYPE_EXT_INT;
}


/**
 * Determines whether the guest is using PAE paging given the VMCS.
 *
 * @returns @c true if PAE paging mode is used, @c false otherwise.
 * @param   pVmcs       Pointer to the virtual VMCS.
 *
 * @warning Only use this prior to switching the guest-CPU state with the
 *          nested-guest CPU state!
 */
DECL_FORCE_INLINE(bool) iemVmxVmcsIsGuestPaePagingEnabled(PCVMXVVMCS pVmcs)
{
    return (   !(pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_IA32E_MODE_GUEST)
            &&  (pVmcs->u64GuestCr4.u & X86_CR4_PAE)
            &&  (pVmcs->u64GuestCr0.u & X86_CR0_PG));
}


/**
 * Sets the Exit qualification VMCS field.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   u64ExitQual     The Exit qualification.
 */
DECL_FORCE_INLINE(void) iemVmxVmcsSetExitQual(PVMCPUCC pVCpu, uint64_t u64ExitQual)
{
    pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64RoExitQual.u = u64ExitQual;
}


/**
 * Sets the VM-exit interruption information field.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uExitIntInfo    The VM-exit interruption information.
 */
DECL_FORCE_INLINE(void) iemVmxVmcsSetExitIntInfo(PVMCPUCC pVCpu, uint32_t uExitIntInfo)
{
    pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32RoExitIntInfo = uExitIntInfo;
}


/**
 * Sets the VM-exit interruption error code.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uErrCode    The error code.
 */
DECL_FORCE_INLINE(void) iemVmxVmcsSetExitIntErrCode(PVMCPUCC pVCpu, uint32_t uErrCode)
{
    pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32RoExitIntErrCode = uErrCode;
}


/**
 * Sets the IDT-vectoring information field.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uIdtVectorInfo  The IDT-vectoring information.
 */
DECL_FORCE_INLINE(void) iemVmxVmcsSetIdtVectoringInfo(PVMCPUCC pVCpu, uint32_t uIdtVectorInfo)
{
    pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32RoIdtVectoringInfo = uIdtVectorInfo;
}


/**
 * Sets the IDT-vectoring error code field.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uErrCode    The error code.
 */
DECL_FORCE_INLINE(void) iemVmxVmcsSetIdtVectoringErrCode(PVMCPUCC pVCpu, uint32_t uErrCode)
{
    pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32RoIdtVectoringErrCode = uErrCode;
}


/**
 * Sets the VM-exit guest-linear address VMCS field.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   uGuestLinearAddr    The VM-exit guest-linear address.
 */
DECL_FORCE_INLINE(void) iemVmxVmcsSetExitGuestLinearAddr(PVMCPUCC pVCpu, uint64_t uGuestLinearAddr)
{
    /* Bits 63:32 of guest-linear address MBZ if the guest isn't in long mode prior to the VM-exit. */
    Assert(CPUMIsGuestInLongModeEx(IEM_GET_CTX(pVCpu)) || !(uGuestLinearAddr & UINT64_C(0xffffffff00000000)));
    pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64RoGuestLinearAddr.u = uGuestLinearAddr;
}


/**
 * Sets the VM-exit guest-physical address VMCS field.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uGuestPhysAddr  The VM-exit guest-physical address.
 */
DECL_FORCE_INLINE(void) iemVmxVmcsSetExitGuestPhysAddr(PVMCPUCC pVCpu, uint64_t uGuestPhysAddr)
{
    pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64RoGuestPhysAddr.u = uGuestPhysAddr;
}


/**
 * Sets the VM-exit instruction length VMCS field.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The VM-exit instruction length in bytes.
 *
 * @remarks Callers may clear this field to 0. Hence, this function does not check
 *          the validity of the instruction length.
 */
DECL_FORCE_INLINE(void) iemVmxVmcsSetExitInstrLen(PVMCPUCC pVCpu, uint32_t cbInstr)
{
    pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32RoExitInstrLen = cbInstr;
}


/**
 * Sets the VM-exit instruction info. VMCS field.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uExitInstrInfo  The VM-exit instruction information.
 */
DECL_FORCE_INLINE(void) iemVmxVmcsSetExitInstrInfo(PVMCPUCC pVCpu, uint32_t uExitInstrInfo)
{
    pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32RoExitInstrInfo = uExitInstrInfo;
}


/**
 * Sets the guest pending-debug exceptions field.
 *
 * @param   pVCpu                   The cross context virtual CPU structure.
 * @param   uGuestPendingDbgXcpts   The guest pending-debug exceptions.
 */
DECL_FORCE_INLINE(void) iemVmxVmcsSetGuestPendingDbgXcpts(PVMCPUCC pVCpu, uint64_t uGuestPendingDbgXcpts)
{
    Assert(!(uGuestPendingDbgXcpts & VMX_VMCS_GUEST_PENDING_DEBUG_VALID_MASK));
    pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64GuestPendingDbgXcpts.u = uGuestPendingDbgXcpts;
}


/**
 * Implements VMSucceed for VMX instruction success.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
DECL_FORCE_INLINE(void) iemVmxVmSucceed(PVMCPUCC pVCpu)
{
    return CPUMSetGuestVmxVmSucceed(&pVCpu->cpum.GstCtx);
}


/**
 * Implements VMFailInvalid for VMX instruction failure.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
DECL_FORCE_INLINE(void) iemVmxVmFailInvalid(PVMCPUCC pVCpu)
{
    return CPUMSetGuestVmxVmFailInvalid(&pVCpu->cpum.GstCtx);
}


/**
 * Implements VMFail for VMX instruction failure.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   enmInsErr   The VM instruction error.
 */
DECL_FORCE_INLINE(void) iemVmxVmFail(PVMCPUCC pVCpu, VMXINSTRERR enmInsErr)
{
    return CPUMSetGuestVmxVmFail(&pVCpu->cpum.GstCtx, enmInsErr);
}


/**
 * Checks if the given auto-load/store MSR area count is valid for the
 * implementation.
 *
 * @returns @c true if it's within the valid limit, @c false otherwise.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uMsrCount   The MSR area count to check.
 */
DECL_FORCE_INLINE(bool) iemVmxIsAutoMsrCountValid(PCVMCPU pVCpu, uint32_t uMsrCount)
{
    uint64_t const u64VmxMiscMsr     = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64Misc;
    uint32_t const cMaxSupportedMsrs = VMX_MISC_MAX_MSRS(u64VmxMiscMsr);
    Assert(cMaxSupportedMsrs <= VMX_V_AUTOMSR_AREA_SIZE / sizeof(VMXAUTOMSR));
    if (uMsrCount <= cMaxSupportedMsrs)
        return true;
    return false;
}


/**
 * Flushes the current VMCS contents back to guest memory.
 *
 * @returns VBox status code.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
DECL_FORCE_INLINE(int) iemVmxWriteCurrentVmcsToGstMem(PVMCPUCC pVCpu)
{
    Assert(IEM_VMX_HAS_CURRENT_VMCS(pVCpu));
    int rc = PGMPhysSimpleWriteGCPhys(pVCpu->CTX_SUFF(pVM), IEM_VMX_GET_CURRENT_VMCS(pVCpu),
                                      &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs, sizeof(pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs));
    return rc;
}


/**
 * Populates the current VMCS contents from guest memory.
 *
 * @returns VBox status code.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
DECL_FORCE_INLINE(int) iemVmxReadCurrentVmcsFromGstMem(PVMCPUCC pVCpu)
{
    Assert(IEM_VMX_HAS_CURRENT_VMCS(pVCpu));
    int rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs,
                                     IEM_VMX_GET_CURRENT_VMCS(pVCpu), sizeof(pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs));
    return rc;
}


/**
 * Gets the instruction diagnostic for segment base checks during VM-entry of a
 * nested-guest.
 *
 * @param   iSegReg     The segment index (X86_SREG_XXX).
 */
static VMXVDIAG iemVmxGetDiagVmentrySegBase(unsigned iSegReg) RT_NOEXCEPT
{
    switch (iSegReg)
    {
        case X86_SREG_CS: return kVmxVDiag_Vmentry_GuestSegBaseCs;
        case X86_SREG_DS: return kVmxVDiag_Vmentry_GuestSegBaseDs;
        case X86_SREG_ES: return kVmxVDiag_Vmentry_GuestSegBaseEs;
        case X86_SREG_FS: return kVmxVDiag_Vmentry_GuestSegBaseFs;
        case X86_SREG_GS: return kVmxVDiag_Vmentry_GuestSegBaseGs;
        case X86_SREG_SS: return kVmxVDiag_Vmentry_GuestSegBaseSs;
        IEM_NOT_REACHED_DEFAULT_CASE_RET2(kVmxVDiag_Ipe_1);
    }
}


/**
 * Gets the instruction diagnostic for segment base checks during VM-entry of a
 * nested-guest that is in Virtual-8086 mode.
 *
 * @param   iSegReg     The segment index (X86_SREG_XXX).
 */
static VMXVDIAG iemVmxGetDiagVmentrySegBaseV86(unsigned iSegReg) RT_NOEXCEPT
{
    switch (iSegReg)
    {
        case X86_SREG_CS: return kVmxVDiag_Vmentry_GuestSegBaseV86Cs;
        case X86_SREG_DS: return kVmxVDiag_Vmentry_GuestSegBaseV86Ds;
        case X86_SREG_ES: return kVmxVDiag_Vmentry_GuestSegBaseV86Es;
        case X86_SREG_FS: return kVmxVDiag_Vmentry_GuestSegBaseV86Fs;
        case X86_SREG_GS: return kVmxVDiag_Vmentry_GuestSegBaseV86Gs;
        case X86_SREG_SS: return kVmxVDiag_Vmentry_GuestSegBaseV86Ss;
        IEM_NOT_REACHED_DEFAULT_CASE_RET2(kVmxVDiag_Ipe_2);
    }
}


/**
 * Gets the instruction diagnostic for segment limit checks during VM-entry of a
 * nested-guest that is in Virtual-8086 mode.
 *
 * @param   iSegReg     The segment index (X86_SREG_XXX).
 */
static VMXVDIAG iemVmxGetDiagVmentrySegLimitV86(unsigned iSegReg) RT_NOEXCEPT
{
    switch (iSegReg)
    {
        case X86_SREG_CS: return kVmxVDiag_Vmentry_GuestSegLimitV86Cs;
        case X86_SREG_DS: return kVmxVDiag_Vmentry_GuestSegLimitV86Ds;
        case X86_SREG_ES: return kVmxVDiag_Vmentry_GuestSegLimitV86Es;
        case X86_SREG_FS: return kVmxVDiag_Vmentry_GuestSegLimitV86Fs;
        case X86_SREG_GS: return kVmxVDiag_Vmentry_GuestSegLimitV86Gs;
        case X86_SREG_SS: return kVmxVDiag_Vmentry_GuestSegLimitV86Ss;
        IEM_NOT_REACHED_DEFAULT_CASE_RET2(kVmxVDiag_Ipe_3);
    }
}


/**
 * Gets the instruction diagnostic for segment attribute checks during VM-entry of a
 * nested-guest that is in Virtual-8086 mode.
 *
 * @param   iSegReg     The segment index (X86_SREG_XXX).
 */
static VMXVDIAG iemVmxGetDiagVmentrySegAttrV86(unsigned iSegReg) RT_NOEXCEPT
{
    switch (iSegReg)
    {
        case X86_SREG_CS: return kVmxVDiag_Vmentry_GuestSegAttrV86Cs;
        case X86_SREG_DS: return kVmxVDiag_Vmentry_GuestSegAttrV86Ds;
        case X86_SREG_ES: return kVmxVDiag_Vmentry_GuestSegAttrV86Es;
        case X86_SREG_FS: return kVmxVDiag_Vmentry_GuestSegAttrV86Fs;
        case X86_SREG_GS: return kVmxVDiag_Vmentry_GuestSegAttrV86Gs;
        case X86_SREG_SS: return kVmxVDiag_Vmentry_GuestSegAttrV86Ss;
        IEM_NOT_REACHED_DEFAULT_CASE_RET2(kVmxVDiag_Ipe_4);
    }
}


/**
 * Gets the instruction diagnostic for segment attributes reserved bits failure
 * during VM-entry of a nested-guest.
 *
 * @param   iSegReg     The segment index (X86_SREG_XXX).
 */
static VMXVDIAG iemVmxGetDiagVmentrySegAttrRsvd(unsigned iSegReg) RT_NOEXCEPT
{
    switch (iSegReg)
    {
        case X86_SREG_CS: return kVmxVDiag_Vmentry_GuestSegAttrRsvdCs;
        case X86_SREG_DS: return kVmxVDiag_Vmentry_GuestSegAttrRsvdDs;
        case X86_SREG_ES: return kVmxVDiag_Vmentry_GuestSegAttrRsvdEs;
        case X86_SREG_FS: return kVmxVDiag_Vmentry_GuestSegAttrRsvdFs;
        case X86_SREG_GS: return kVmxVDiag_Vmentry_GuestSegAttrRsvdGs;
        case X86_SREG_SS: return kVmxVDiag_Vmentry_GuestSegAttrRsvdSs;
        IEM_NOT_REACHED_DEFAULT_CASE_RET2(kVmxVDiag_Ipe_5);
    }
}


/**
 * Gets the instruction diagnostic for segment attributes descriptor-type
 * (code/segment or system) failure during VM-entry of a nested-guest.
 *
 * @param   iSegReg     The segment index (X86_SREG_XXX).
 */
static VMXVDIAG iemVmxGetDiagVmentrySegAttrDescType(unsigned iSegReg) RT_NOEXCEPT
{
    switch (iSegReg)
    {
        case X86_SREG_CS: return kVmxVDiag_Vmentry_GuestSegAttrDescTypeCs;
        case X86_SREG_DS: return kVmxVDiag_Vmentry_GuestSegAttrDescTypeDs;
        case X86_SREG_ES: return kVmxVDiag_Vmentry_GuestSegAttrDescTypeEs;
        case X86_SREG_FS: return kVmxVDiag_Vmentry_GuestSegAttrDescTypeFs;
        case X86_SREG_GS: return kVmxVDiag_Vmentry_GuestSegAttrDescTypeGs;
        case X86_SREG_SS: return kVmxVDiag_Vmentry_GuestSegAttrDescTypeSs;
        IEM_NOT_REACHED_DEFAULT_CASE_RET2(kVmxVDiag_Ipe_6);
    }
}


/**
 * Gets the instruction diagnostic for segment attributes descriptor-type
 * (code/segment or system) failure during VM-entry of a nested-guest.
 *
 * @param   iSegReg     The segment index (X86_SREG_XXX).
 */
static VMXVDIAG iemVmxGetDiagVmentrySegAttrPresent(unsigned iSegReg) RT_NOEXCEPT
{
    switch (iSegReg)
    {
        case X86_SREG_CS: return kVmxVDiag_Vmentry_GuestSegAttrPresentCs;
        case X86_SREG_DS: return kVmxVDiag_Vmentry_GuestSegAttrPresentDs;
        case X86_SREG_ES: return kVmxVDiag_Vmentry_GuestSegAttrPresentEs;
        case X86_SREG_FS: return kVmxVDiag_Vmentry_GuestSegAttrPresentFs;
        case X86_SREG_GS: return kVmxVDiag_Vmentry_GuestSegAttrPresentGs;
        case X86_SREG_SS: return kVmxVDiag_Vmentry_GuestSegAttrPresentSs;
        IEM_NOT_REACHED_DEFAULT_CASE_RET2(kVmxVDiag_Ipe_7);
    }
}


/**
 * Gets the instruction diagnostic for segment attribute granularity failure during
 * VM-entry of a nested-guest.
 *
 * @param   iSegReg     The segment index (X86_SREG_XXX).
 */
static VMXVDIAG iemVmxGetDiagVmentrySegAttrGran(unsigned iSegReg) RT_NOEXCEPT
{
    switch (iSegReg)
    {
        case X86_SREG_CS: return kVmxVDiag_Vmentry_GuestSegAttrGranCs;
        case X86_SREG_DS: return kVmxVDiag_Vmentry_GuestSegAttrGranDs;
        case X86_SREG_ES: return kVmxVDiag_Vmentry_GuestSegAttrGranEs;
        case X86_SREG_FS: return kVmxVDiag_Vmentry_GuestSegAttrGranFs;
        case X86_SREG_GS: return kVmxVDiag_Vmentry_GuestSegAttrGranGs;
        case X86_SREG_SS: return kVmxVDiag_Vmentry_GuestSegAttrGranSs;
        IEM_NOT_REACHED_DEFAULT_CASE_RET2(kVmxVDiag_Ipe_8);
    }
}

/**
 * Gets the instruction diagnostic for segment attribute DPL/RPL failure during
 * VM-entry of a nested-guest.
 *
 * @param   iSegReg     The segment index (X86_SREG_XXX).
 */
static VMXVDIAG iemVmxGetDiagVmentrySegAttrDplRpl(unsigned iSegReg) RT_NOEXCEPT
{
    switch (iSegReg)
    {
        case X86_SREG_CS: return kVmxVDiag_Vmentry_GuestSegAttrDplRplCs;
        case X86_SREG_DS: return kVmxVDiag_Vmentry_GuestSegAttrDplRplDs;
        case X86_SREG_ES: return kVmxVDiag_Vmentry_GuestSegAttrDplRplEs;
        case X86_SREG_FS: return kVmxVDiag_Vmentry_GuestSegAttrDplRplFs;
        case X86_SREG_GS: return kVmxVDiag_Vmentry_GuestSegAttrDplRplGs;
        case X86_SREG_SS: return kVmxVDiag_Vmentry_GuestSegAttrDplRplSs;
        IEM_NOT_REACHED_DEFAULT_CASE_RET2(kVmxVDiag_Ipe_9);
    }
}


/**
 * Gets the instruction diagnostic for segment attribute type accessed failure
 * during VM-entry of a nested-guest.
 *
 * @param   iSegReg     The segment index (X86_SREG_XXX).
 */
static VMXVDIAG iemVmxGetDiagVmentrySegAttrTypeAcc(unsigned iSegReg) RT_NOEXCEPT
{
    switch (iSegReg)
    {
        case X86_SREG_CS: return kVmxVDiag_Vmentry_GuestSegAttrTypeAccCs;
        case X86_SREG_DS: return kVmxVDiag_Vmentry_GuestSegAttrTypeAccDs;
        case X86_SREG_ES: return kVmxVDiag_Vmentry_GuestSegAttrTypeAccEs;
        case X86_SREG_FS: return kVmxVDiag_Vmentry_GuestSegAttrTypeAccFs;
        case X86_SREG_GS: return kVmxVDiag_Vmentry_GuestSegAttrTypeAccGs;
        case X86_SREG_SS: return kVmxVDiag_Vmentry_GuestSegAttrTypeAccSs;
        IEM_NOT_REACHED_DEFAULT_CASE_RET2(kVmxVDiag_Ipe_10);
    }
}


/**
 * Saves the guest control registers, debug registers and some MSRs are part of
 * VM-exit.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static void iemVmxVmexitSaveGuestControlRegsMsrs(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    /*
     * Saves the guest control registers, debug registers and some MSRs.
     * See Intel spec. 27.3.1 "Saving Control Registers, Debug Registers and MSRs".
     */
    PVMXVVMCS pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;

    /* Save control registers. */
    pVmcs->u64GuestCr0.u = pVCpu->cpum.GstCtx.cr0;
    pVmcs->u64GuestCr3.u = pVCpu->cpum.GstCtx.cr3;
    pVmcs->u64GuestCr4.u = pVCpu->cpum.GstCtx.cr4;

    /* Save SYSENTER CS, ESP, EIP. */
    pVmcs->u32GuestSysenterCS    = pVCpu->cpum.GstCtx.SysEnter.cs;
    if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode)
    {
        pVmcs->u64GuestSysenterEsp.u = pVCpu->cpum.GstCtx.SysEnter.esp;
        pVmcs->u64GuestSysenterEip.u = pVCpu->cpum.GstCtx.SysEnter.eip;
    }
    else
    {
        pVmcs->u64GuestSysenterEsp.s.Lo = pVCpu->cpum.GstCtx.SysEnter.esp;
        pVmcs->u64GuestSysenterEip.s.Lo = pVCpu->cpum.GstCtx.SysEnter.eip;
    }

    /* Save debug registers (DR7 and IA32_DEBUGCTL MSR). */
    if (pVmcs->u32ExitCtls & VMX_EXIT_CTLS_SAVE_DEBUG)
    {
        pVmcs->u64GuestDr7.u = pVCpu->cpum.GstCtx.dr[7];
        /** @todo NSTVMX: Support IA32_DEBUGCTL MSR */
    }

    /* Save PAT MSR. */
    if (pVmcs->u32ExitCtls & VMX_EXIT_CTLS_SAVE_PAT_MSR)
        pVmcs->u64GuestPatMsr.u = pVCpu->cpum.GstCtx.msrPAT;

    /* Save EFER MSR. */
    if (pVmcs->u32ExitCtls & VMX_EXIT_CTLS_SAVE_EFER_MSR)
        pVmcs->u64GuestEferMsr.u = pVCpu->cpum.GstCtx.msrEFER;

    /* We don't support clearing IA32_BNDCFGS MSR yet. */
    Assert(!(pVmcs->u32ExitCtls & VMX_EXIT_CTLS_CLEAR_BNDCFGS_MSR));

    /* Nothing to do for SMBASE register - We don't support SMM yet. */
}


/**
 * Saves the guest force-flags in preparation of entering the nested-guest.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static void iemVmxVmentrySaveNmiBlockingFF(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    /* We shouldn't be called multiple times during VM-entry. */
    Assert(pVCpu->cpum.GstCtx.hwvirt.fSavedInhibit == 0);

    /* MTF should not be set outside VMX non-root mode. */
    Assert(!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_MTF));

    /*
     * Preserve the required force-flags.
     *
     * We cache and clear force-flags that would affect the execution of the
     * nested-guest. Cached flags are then restored while returning to the guest
     * if necessary.
     *
     *   - VMCPU_FF_INHIBIT_INTERRUPTS need not be cached as it only affects
     *     interrupts until the completion of the current VMLAUNCH/VMRESUME
     *     instruction. Interrupt inhibition for any nested-guest instruction
     *     is supplied by the guest-interruptibility state VMCS field and will
     *     be set up as part of loading the guest state. Technically
     *     blocking-by-STI is possible with VMLAUNCH/VMRESUME but we currently
     *     disallow it since we can't distinguish it from blocking-by-MovSS
     *     and no nested-hypervisor we care about uses STI immediately
     *     followed by VMLAUNCH/VMRESUME.
     *
     *   - VMCPU_FF_BLOCK_NMIS needs to be cached as VM-exits caused before
     *     successful VM-entry (due to invalid guest-state) need to continue
     *     blocking NMIs if it was in effect before VM-entry.
     *
     *   - MTF need not be preserved as it's used only in VMX non-root mode and
     *     is supplied through the VM-execution controls.
     *
     * The remaining FFs (e.g. timers, APIC updates) can stay in place so that
     * we will be able to generate interrupts that may cause VM-exits for
     * the nested-guest.
     */
    pVCpu->cpum.GstCtx.hwvirt.fSavedInhibit = pVCpu->cpum.GstCtx.eflags.uBoth & CPUMCTX_INHIBIT_NMI;
}


/**
 * Restores the guest force-flags in preparation of exiting the nested-guest.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static void iemVmxVmexitRestoreNmiBlockingFF(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    /** @todo r=bird: why aren't we clearing the nested guest flags first here?
     *        If there is some other code doing that already, it would be great
     *        to point to it here... */
    pVCpu->cpum.GstCtx.eflags.uBoth        |= pVCpu->cpum.GstCtx.hwvirt.fSavedInhibit;
    pVCpu->cpum.GstCtx.hwvirt.fSavedInhibit = 0;
}


/**
 * Performs the VMX transition to/from VMX non-root mode.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The length of the current instruction.
 */
static int iemVmxTransition(PVMCPUCC pVCpu, uint8_t cbInstr) RT_NOEXCEPT
{
    /*
     * Inform PGM about paging mode changes.
     * We include X86_CR0_PE because PGM doesn't handle paged-real mode yet,
     * see comment in iemMemPageTranslateAndCheckAccess().
     */
    int rc = PGMChangeMode(pVCpu, pVCpu->cpum.GstCtx.cr0 | X86_CR0_PE, pVCpu->cpum.GstCtx.cr4, pVCpu->cpum.GstCtx.msrEFER,
                           true /* fForce */);
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else
        return rc;

    /* Invalidate IEM TLBs now that we've forced a PGM mode change. */
    IEMTlbInvalidateAllGlobal(pVCpu);

    /* Inform CPUM (recompiler), can later be removed. */
    CPUMSetChangedFlags(pVCpu, CPUM_CHANGED_ALL);

    /* Re-initialize IEM cache/state after the drastic mode switch. */
    iemReInitExec(pVCpu, cbInstr);
    return rc;
}


/**
 * Calculates the current VMX-preemption timer value.
 *
 * @returns The current VMX-preemption timer value.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static uint32_t iemVmxCalcPreemptTimer(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    /*
     * Assume the following:
     * PreemptTimerShift = 5
     * VmcsPreemptTimer  = 2 (i.e. need to decrement by 1 every 2 * RT_BIT(5) = 20000 TSC ticks)
     * EntryTick         = 50000 (TSC at time of VM-entry)
     *
     * CurTick   Delta    PreemptTimerVal
     * ----------------------------------
     *  60000    10000    2
     *  80000    30000    1
     *  90000    40000    0  -> VM-exit.
     *
     * If Delta >= VmcsPreemptTimer * RT_BIT(PreemptTimerShift) cause a VMX-preemption timer VM-exit.
     * The saved VMX-preemption timer value is calculated as follows:
     * PreemptTimerVal = VmcsPreemptTimer - (Delta / (VmcsPreemptTimer * RT_BIT(PreemptTimerShift)))
     * E.g.:
     *  Delta  = 10000
     *    Tmp    = 10000 / (2 * 10000) = 0.5
     *    NewPt  = 2 - 0.5 = 2
     *  Delta  = 30000
     *    Tmp    = 30000 / (2 * 10000) = 1.5
     *    NewPt  = 2 - 1.5 = 1
     *  Delta  = 40000
     *    Tmp    = 40000 / 20000 = 2
     *    NewPt  = 2 - 2 = 0
     */
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_HWVIRT);
    uint32_t const uVmcsPreemptVal = pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32PreemptTimer;
    if (uVmcsPreemptVal > 0)
    {
        uint64_t const uCurTick      = TMCpuTickGetNoCheck(pVCpu);
        uint64_t const uEntryTick    = pVCpu->cpum.GstCtx.hwvirt.vmx.uEntryTick;
        uint64_t const uDelta        = uCurTick - uEntryTick;
        uint32_t const uPreemptTimer = uVmcsPreemptVal
                                     - ASMDivU64ByU32RetU32(uDelta, uVmcsPreemptVal * RT_BIT(VMX_V_PREEMPT_TIMER_SHIFT));
        return uPreemptTimer;
    }
    return 0;
}


/**
 * Saves guest segment registers, GDTR, IDTR, LDTR, TR as part of VM-exit.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static void iemVmxVmexitSaveGuestSegRegs(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    /*
     * Save guest segment registers, GDTR, IDTR, LDTR, TR.
     * See Intel spec 27.3.2 "Saving Segment Registers and Descriptor-Table Registers".
     */
    /* CS, SS, ES, DS, FS, GS. */
    PVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    for (unsigned iSegReg = 0; iSegReg < X86_SREG_COUNT; iSegReg++)
    {
        PCCPUMSELREG pSelReg = &pVCpu->cpum.GstCtx.aSRegs[iSegReg];
        if (!pSelReg->Attr.n.u1Unusable)
            iemVmxVmcsSetGuestSegReg(pVmcs, iSegReg, pSelReg);
        else
        {
            /*
             * For unusable segments the attributes are undefined except for CS and SS.
             * For the rest we don't bother preserving anything but the unusable bit.
             */
            switch (iSegReg)
            {
                case X86_SREG_CS:
                    pVmcs->GuestCs          = pSelReg->Sel;
                    pVmcs->u64GuestCsBase.u = pSelReg->u64Base;
                    pVmcs->u32GuestCsLimit  = pSelReg->u32Limit;
                    pVmcs->u32GuestCsAttr   = pSelReg->Attr.u & (  X86DESCATTR_L | X86DESCATTR_D | X86DESCATTR_G
                                                                 | X86DESCATTR_UNUSABLE);
                    break;

                case X86_SREG_SS:
                    pVmcs->GuestSs        = pSelReg->Sel;
                    if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode)
                        pVmcs->u64GuestSsBase.u &= UINT32_C(0xffffffff);
                    pVmcs->u32GuestSsAttr = pSelReg->Attr.u & (X86DESCATTR_DPL | X86DESCATTR_UNUSABLE);
                    break;

                case X86_SREG_DS:
                    pVmcs->GuestDs        = pSelReg->Sel;
                    if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode)
                        pVmcs->u64GuestDsBase.u &= UINT32_C(0xffffffff);
                    pVmcs->u32GuestDsAttr = X86DESCATTR_UNUSABLE;
                    break;

                case X86_SREG_ES:
                    pVmcs->GuestEs        = pSelReg->Sel;
                    if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode)
                        pVmcs->u64GuestEsBase.u &= UINT32_C(0xffffffff);
                    pVmcs->u32GuestEsAttr = X86DESCATTR_UNUSABLE;
                    break;

                case X86_SREG_FS:
                    pVmcs->GuestFs          = pSelReg->Sel;
                    pVmcs->u64GuestFsBase.u = pSelReg->u64Base;
                    pVmcs->u32GuestFsAttr   = X86DESCATTR_UNUSABLE;
                    break;

                case X86_SREG_GS:
                    pVmcs->GuestGs          = pSelReg->Sel;
                    pVmcs->u64GuestGsBase.u = pSelReg->u64Base;
                    pVmcs->u32GuestGsAttr   = X86DESCATTR_UNUSABLE;
                    break;
            }
        }
    }

    /* Segment attribute bits 31:17 and 11:8 MBZ. */
    uint32_t const fValidAttrMask = X86DESCATTR_TYPE | X86DESCATTR_DT  | X86DESCATTR_DPL | X86DESCATTR_P
                                  | X86DESCATTR_AVL  | X86DESCATTR_L   | X86DESCATTR_D   | X86DESCATTR_G
                                  | X86DESCATTR_UNUSABLE;
    /* LDTR. */
    {
        PCCPUMSELREG pSelReg = &pVCpu->cpum.GstCtx.ldtr;
        pVmcs->GuestLdtr          = pSelReg->Sel;
        pVmcs->u64GuestLdtrBase.u = pSelReg->u64Base;
        Assert(X86_IS_CANONICAL(pSelReg->u64Base));
        pVmcs->u32GuestLdtrLimit  = pSelReg->u32Limit;
        pVmcs->u32GuestLdtrAttr   = pSelReg->Attr.u & fValidAttrMask;
    }

    /* TR. */
    {
        PCCPUMSELREG pSelReg = &pVCpu->cpum.GstCtx.tr;
        pVmcs->GuestTr          = pSelReg->Sel;
        pVmcs->u64GuestTrBase.u = pSelReg->u64Base;
        pVmcs->u32GuestTrLimit  = pSelReg->u32Limit;
        pVmcs->u32GuestTrAttr   = pSelReg->Attr.u & fValidAttrMask;
    }

    /* GDTR. */
    pVmcs->u64GuestGdtrBase.u = pVCpu->cpum.GstCtx.gdtr.pGdt;
    pVmcs->u32GuestGdtrLimit  = pVCpu->cpum.GstCtx.gdtr.cbGdt;

    /* IDTR. */
    pVmcs->u64GuestIdtrBase.u = pVCpu->cpum.GstCtx.idtr.pIdt;
    pVmcs->u32GuestIdtrLimit  = pVCpu->cpum.GstCtx.idtr.cbIdt;
}


/**
 * Saves guest non-register state as part of VM-exit.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uExitReason     The VM-exit reason.
 */
static void iemVmxVmexitSaveGuestNonRegState(PVMCPUCC pVCpu, uint32_t uExitReason) RT_NOEXCEPT
{
    /*
     * Save guest non-register state.
     * See Intel spec. 27.3.4 "Saving Non-Register State".
     */
    PVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;

    /*
     * Activity state.
     * Most VM-exits will occur in the active state. However, if the first instruction
     * following the VM-entry is a HLT instruction, and the MTF VM-execution control is set,
     * the VM-exit will be from the HLT activity state.
     *
     * See Intel spec. 25.5.2 "Monitor Trap Flag".
     */
    /** @todo NSTVMX: Does triple-fault VM-exit reflect a shutdown activity state or
     *        not? */
    EMSTATE const enmActivityState = EMGetState(pVCpu);
    switch (enmActivityState)
    {
        case EMSTATE_HALTED:    pVmcs->u32GuestActivityState = VMX_VMCS_GUEST_ACTIVITY_HLT;     break;
        default:                pVmcs->u32GuestActivityState = VMX_VMCS_GUEST_ACTIVITY_ACTIVE;  break;
    }

    /*
     * Interruptibility-state.
     */
    /* NMI. */
    pVmcs->u32GuestIntrState = 0;
    if (pVmcs->u32PinCtls & VMX_PIN_CTLS_VIRT_NMI)
    {
        if (pVCpu->cpum.GstCtx.hwvirt.vmx.fVirtNmiBlocking)
            pVmcs->u32GuestIntrState |= VMX_VMCS_GUEST_INT_STATE_BLOCK_NMI;
    }
    else
    {
        if (CPUMAreInterruptsInhibitedByNmi(&pVCpu->cpum.GstCtx))
            pVmcs->u32GuestIntrState |= VMX_VMCS_GUEST_INT_STATE_BLOCK_NMI;
    }

    /* Blocking-by-STI or blocking-by-MovSS. */
    uint32_t fInhibitShw;
    if (!CPUMIsInInterruptShadowWithUpdateEx(&pVCpu->cpum.GstCtx, &fInhibitShw))
    { /* probable */}
    else
    {
        if (pVCpu->cpum.GstCtx.rip == pVCpu->cpum.GstCtx.uRipInhibitInt)
        {
            /*
             * We must ensure only one of these bits is set.
             * Our emulation can have both set (perhaps because AMD doesn't distinguish
             * between the two?). Hence, the 'else' with blocking-by-MovSS taking priority
             * since it blocks more. Nested Ubuntu 22.04.2 running inside a Hyper-V enabled
             * Windows Server 2008 R2 guest runs into this issue.
             *
             * See Intel spec. 26.3.1.5 "Checks on Guest Non-Register State".
             */
            if (fInhibitShw & CPUMCTX_INHIBIT_SHADOW_SS)
                pVmcs->u32GuestIntrState |= VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS;
            else
            {
                Assert(fInhibitShw & CPUMCTX_INHIBIT_SHADOW_STI);
                pVmcs->u32GuestIntrState |= VMX_VMCS_GUEST_INT_STATE_BLOCK_STI;
            }
        }
    }
    /* Nothing to do for SMI/enclave. We don't support enclaves or SMM yet. */

    /*
     * Pending debug exceptions.
     *
     * For VM-exits where it is not applicable, we can safely zero out the field.
     * For VM-exits where it is applicable, it's expected to be updated by the caller already.
     */
    if (    uExitReason != VMX_EXIT_INIT_SIGNAL
        &&  uExitReason != VMX_EXIT_SMI
        &&  uExitReason != VMX_EXIT_ERR_MACHINE_CHECK
        && !VMXIsVmexitTrapLike(uExitReason))
    {
        /** @todo NSTVMX: also must exclude VM-exits caused by debug exceptions when
         *        block-by-MovSS is in effect. */
        pVmcs->u64GuestPendingDbgXcpts.u = 0;
    }

    /*
     * Save the VMX-preemption timer value back into the VMCS if the feature is enabled.
     *
     * For VMX-preemption timer VM-exits, we should have already written back 0 if the
     * feature is supported back into the VMCS, and thus there is nothing further to do here.
     */
    if (   uExitReason != VMX_EXIT_PREEMPT_TIMER
        && (pVmcs->u32ExitCtls & VMX_EXIT_CTLS_SAVE_PREEMPT_TIMER))
        pVmcs->u32PreemptTimer = iemVmxCalcPreemptTimer(pVCpu);

    /*
     * Save the guest PAE PDPTEs.
     */
    if (   !CPUMIsGuestInPAEModeEx(&pVCpu->cpum.GstCtx)
        || !(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_EPT))
    {
        /*
         * Without EPT or when the nested-guest is not using PAE paging, the values saved
         * in the VMCS during VM-exit are undefined. We zero them here for consistency.
         */
        pVmcs->u64GuestPdpte0.u = 0;
        pVmcs->u64GuestPdpte1.u = 0;
        pVmcs->u64GuestPdpte2.u = 0;
        pVmcs->u64GuestPdpte3.u = 0;
    }
    else
    {
        /*
         * With EPT and when the nested-guest is using PAE paging, we update the PDPTEs from
         * the nested-guest CPU context. Both IEM (Mov CRx) and hardware-assisted execution
         * of the nested-guest is expected to have updated them.
         */
        pVmcs->u64GuestPdpte0.u = pVCpu->cpum.GstCtx.aPaePdpes[0].u;
        pVmcs->u64GuestPdpte1.u = pVCpu->cpum.GstCtx.aPaePdpes[1].u;
        pVmcs->u64GuestPdpte2.u = pVCpu->cpum.GstCtx.aPaePdpes[2].u;
        pVmcs->u64GuestPdpte3.u = pVCpu->cpum.GstCtx.aPaePdpes[3].u;
    }

    /* Clear PGM's copy of the EPT pointer for added safety. */
    if (pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_EPT)
        PGMSetGuestEptPtr(pVCpu, 0 /* uEptPtr */);
}


/**
 * Saves the guest-state as part of VM-exit.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uExitReason     The VM-exit reason.
 */
static void iemVmxVmexitSaveGuestState(PVMCPUCC pVCpu, uint32_t uExitReason) RT_NOEXCEPT
{
    iemVmxVmexitSaveGuestControlRegsMsrs(pVCpu);
    iemVmxVmexitSaveGuestSegRegs(pVCpu);

    pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64GuestRip.u    = pVCpu->cpum.GstCtx.rip;
    pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64GuestRsp.u    = pVCpu->cpum.GstCtx.rsp;
    pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64GuestRFlags.u = pVCpu->cpum.GstCtx.rflags.u;  /** @todo NSTVMX: Check RFLAGS.RF handling. */

    iemVmxVmexitSaveGuestNonRegState(pVCpu, uExitReason);
}


/**
 * Saves the guest MSRs into the VM-exit MSR-store area as part of VM-exit.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uExitReason     The VM-exit reason (for diagnostic purposes).
 */
static int iemVmxVmexitSaveGuestAutoMsrs(PVMCPUCC pVCpu, uint32_t uExitReason) RT_NOEXCEPT
{
    /*
     * Save guest MSRs.
     * See Intel spec. 27.4 "Saving MSRs".
     */
    PVMXVVMCS const    pVmcs      = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    const char * const pszFailure = "VMX-abort";

    /*
     * The VM-exit MSR-store area address need not be a valid guest-physical address if the
     * VM-exit MSR-store count is 0. If this is the case, bail early without reading it.
     * See Intel spec. 24.7.2 "VM-Exit Controls for MSRs".
     */
    uint32_t const cMsrs = RT_MIN(pVmcs->u32ExitMsrStoreCount, RT_ELEMENTS(pVCpu->cpum.GstCtx.hwvirt.vmx.aExitMsrStoreArea));
    if (!cMsrs)
        return VINF_SUCCESS;

    /*
     * Verify the MSR auto-store count. Physical CPUs can behave unpredictably if the count
     * is exceeded including possibly raising #MC exceptions during VMX transition. Our
     * implementation causes a VMX-abort followed by a triple-fault.
     */
    bool const fIsMsrCountValid = iemVmxIsAutoMsrCountValid(pVCpu, cMsrs);
    if (fIsMsrCountValid)
    { /* likely */ }
    else
        IEM_VMX_VMEXIT_FAILED_RET(pVCpu, uExitReason, pszFailure, kVmxVDiag_Vmexit_MsrStoreCount);

    /*
     * Optimization if the nested hypervisor is using the same guest-physical page for both
     * the VM-entry MSR-load area as well as the VM-exit MSR store area.
     */
    PVMXAUTOMSR    pMsrArea;
    RTGCPHYS const GCPhysVmEntryMsrLoadArea = pVmcs->u64AddrEntryMsrLoad.u;
    RTGCPHYS const GCPhysVmExitMsrStoreArea = pVmcs->u64AddrExitMsrStore.u;
    if (GCPhysVmEntryMsrLoadArea == GCPhysVmExitMsrStoreArea)
        pMsrArea = pVCpu->cpum.GstCtx.hwvirt.vmx.aEntryMsrLoadArea;
    else
    {
        int rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &pVCpu->cpum.GstCtx.hwvirt.vmx.aExitMsrStoreArea[0],
                                         GCPhysVmExitMsrStoreArea, cMsrs * sizeof(VMXAUTOMSR));
        if (RT_SUCCESS(rc))
            pMsrArea = pVCpu->cpum.GstCtx.hwvirt.vmx.aExitMsrStoreArea;
        else
        {
            AssertMsgFailed(("VM-exit: Failed to read MSR auto-store area at %#RGp, rc=%Rrc\n", GCPhysVmExitMsrStoreArea, rc));
            IEM_VMX_VMEXIT_FAILED_RET(pVCpu, uExitReason, pszFailure, kVmxVDiag_Vmexit_MsrStorePtrReadPhys);
        }
    }

    /*
     * Update VM-exit MSR store area.
     */
    PVMXAUTOMSR pMsr = pMsrArea;
    for (uint32_t idxMsr = 0; idxMsr < cMsrs; idxMsr++, pMsr++)
    {
        if (   !pMsr->u32Reserved
            &&  pMsr->u32Msr != MSR_IA32_SMBASE
            &&  pMsr->u32Msr >> 8 != MSR_IA32_X2APIC_START >> 8)
        {
            VBOXSTRICTRC rcStrict = CPUMQueryGuestMsr(pVCpu, pMsr->u32Msr, &pMsr->u64Value);
            if (rcStrict == VINF_SUCCESS)
                continue;

            /*
             * If we're in ring-0, we cannot handle returns to ring-3 at this point and continue VM-exit.
             * If any nested hypervisor loads MSRs that require ring-3 handling, we cause a VMX-abort
             * recording the MSR index in the auxiliary info. field and indicated further by our
             * own, specific diagnostic code. Later, we can try implement handling of the MSR in ring-0
             * if possible, or come up with a better, generic solution.
             */
            pVCpu->cpum.GstCtx.hwvirt.vmx.uAbortAux = pMsr->u32Msr;
            VMXVDIAG const enmDiag = rcStrict == VINF_CPUM_R3_MSR_READ
                                   ? kVmxVDiag_Vmexit_MsrStoreRing3
                                   : kVmxVDiag_Vmexit_MsrStore;
            IEM_VMX_VMEXIT_FAILED_RET(pVCpu, uExitReason, pszFailure, enmDiag);
        }
        else
        {
            pVCpu->cpum.GstCtx.hwvirt.vmx.uAbortAux = pMsr->u32Msr;
            IEM_VMX_VMEXIT_FAILED_RET(pVCpu, uExitReason, pszFailure, kVmxVDiag_Vmexit_MsrStoreRsvd);
        }
    }

    /*
     * Commit the VM-exit MSR store are to guest memory.
     */
    int rc = PGMPhysSimpleWriteGCPhys(pVCpu->CTX_SUFF(pVM), GCPhysVmExitMsrStoreArea, pMsrArea, cMsrs * sizeof(VMXAUTOMSR));
    if (RT_SUCCESS(rc))
        return VINF_SUCCESS;

    NOREF(uExitReason);
    NOREF(pszFailure);

    AssertMsgFailed(("VM-exit: Failed to write MSR auto-store area at %#RGp, rc=%Rrc\n", GCPhysVmExitMsrStoreArea, rc));
    IEM_VMX_VMEXIT_FAILED_RET(pVCpu, uExitReason, pszFailure, kVmxVDiag_Vmexit_MsrStorePtrWritePhys);
}


/**
 * Performs a VMX abort (due to an fatal error during VM-exit).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   enmAbort    The VMX abort reason.
 */
static VBOXSTRICTRC iemVmxAbort(PVMCPUCC pVCpu, VMXABORT enmAbort) RT_NOEXCEPT
{
    /*
     * Perform the VMX abort.
     * See Intel spec. 27.7 "VMX Aborts".
     */
    LogFunc(("enmAbort=%u (%s) -> RESET\n", enmAbort, VMXGetAbortDesc(enmAbort)));

    /* We don't support SMX yet. */
    pVCpu->cpum.GstCtx.hwvirt.vmx.enmAbort = enmAbort;
    if (IEM_VMX_HAS_CURRENT_VMCS(pVCpu))
    {
        RTGCPHYS const GCPhysVmcs  = IEM_VMX_GET_CURRENT_VMCS(pVCpu);
        uint32_t const offVmxAbort = RT_UOFFSETOF(VMXVVMCS, enmVmxAbort);
        PGMPhysSimpleWriteGCPhys(pVCpu->CTX_SUFF(pVM), GCPhysVmcs + offVmxAbort, &enmAbort, sizeof(enmAbort));
    }

    return VINF_EM_TRIPLE_FAULT;
}


/**
 * Loads host control registers, debug registers and MSRs as part of VM-exit.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static void iemVmxVmexitLoadHostControlRegsMsrs(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    /*
     * Load host control registers, debug registers and MSRs.
     * See Intel spec. 27.5.1 "Loading Host Control Registers, Debug Registers, MSRs".
     */
    PCVMXVVMCS const pVmcs           = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    bool const       fHostInLongMode = RT_BOOL(pVmcs->u32ExitCtls & VMX_EXIT_CTLS_HOST_ADDR_SPACE_SIZE);

    /* CR0. */
    {
        /* Bits 63:32, 28:19, 17, 15:6, ET, CD, NW and CR0 fixed bits are not modified. */
        uint64_t const fCr0IgnMask   = VMX_EXIT_HOST_CR0_IGNORE_MASK;
        uint64_t const uHostCr0      = pVmcs->u64HostCr0.u;
        uint64_t const uGuestCr0     = pVCpu->cpum.GstCtx.cr0;
        uint64_t const uValidHostCr0 = (uHostCr0 & ~fCr0IgnMask) | (uGuestCr0 & fCr0IgnMask);

        /* Verify we have not modified CR0 fixed bits in VMX operation. */
#ifdef VBOX_STRICT
        uint64_t const uCr0Mb1 = iemVmxGetCr0Fixed0(pVCpu, true /* fVmxNonRootMode */);
        bool const fUx         = RT_BOOL(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_UNRESTRICTED_GUEST);
        AssertMsg(   (uValidHostCr0 & uCr0Mb1) == uCr0Mb1
                  && (uValidHostCr0 & ~VMX_V_CR0_FIXED1) == 0,
                  ("host=%#RX64 guest=%#RX64 mb1=%#RX64 valid_host_cr0=%#RX64 fUx=%RTbool\n",
                   uHostCr0, uGuestCr0, uCr0Mb1, uValidHostCr0, fUx));
#endif
        Assert(!(uValidHostCr0 >> 32));
        CPUMSetGuestCR0(pVCpu, uValidHostCr0);
    }

    /* CR4. */
    {
        /* CR4 fixed bits are not modified. */
        uint64_t const uCr4Mb1       = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64Cr4Fixed0;
        uint64_t const uCr4Mb0       = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64Cr4Fixed1;
        uint64_t const uHostCr4      = pVmcs->u64HostCr4.u;
        uint64_t       uValidHostCr4 = (uHostCr4 & uCr4Mb0) | uCr4Mb1;
        if (fHostInLongMode)
            uValidHostCr4 |= X86_CR4_PAE;
        else
            uValidHostCr4 &= ~(uint64_t)X86_CR4_PCIDE;

        /* Verify we have not modified CR4 fixed bits in VMX non-root operation. */
        AssertMsg(   (uValidHostCr4 &  uCr4Mb1) == uCr4Mb1
                  && (uValidHostCr4 & ~uCr4Mb0) == 0,
                  ("host=%#RX64 guest=%#RX64, uCr4Mb1=%#RX64 uCr4Mb0=%#RX64 valid_host_cr4=%#RX64\n",
                   uHostCr4, pVCpu->cpum.GstCtx.cr4, uCr4Mb1, uCr4Mb0, uValidHostCr4));
        CPUMSetGuestCR4(pVCpu, uValidHostCr4);
    }

    /* CR3 (host value validated while checking host-state during VM-entry). */
    pVCpu->cpum.GstCtx.cr3 = pVmcs->u64HostCr3.u;

    /* DR7. */
    pVCpu->cpum.GstCtx.dr[7] = X86_DR7_INIT_VAL;

    /** @todo NSTVMX: Support IA32_DEBUGCTL MSR */

    /* Save SYSENTER CS, ESP, EIP (host value validated while checking host-state during VM-entry). */
    pVCpu->cpum.GstCtx.SysEnter.eip = pVmcs->u64HostSysenterEip.u;
    pVCpu->cpum.GstCtx.SysEnter.esp = pVmcs->u64HostSysenterEsp.u;
    pVCpu->cpum.GstCtx.SysEnter.cs  = pVmcs->u32HostSysenterCs;

    /* FS, GS bases are loaded later while we load host segment registers. */

    /* EFER MSR (host value validated while checking host-state during VM-entry). */
    if (pVmcs->u32ExitCtls & VMX_EXIT_CTLS_LOAD_EFER_MSR)
        pVCpu->cpum.GstCtx.msrEFER = pVmcs->u64HostEferMsr.u;
    else if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode)
    {
        if (fHostInLongMode)
            pVCpu->cpum.GstCtx.msrEFER |=  (MSR_K6_EFER_LMA | MSR_K6_EFER_LME);
        else
            pVCpu->cpum.GstCtx.msrEFER &= ~(MSR_K6_EFER_LMA | MSR_K6_EFER_LME);
    }

    /* We don't support IA32_PERF_GLOBAL_CTRL MSR yet. */

    /* PAT MSR (host value is validated while checking host-state during VM-entry). */
    if (pVmcs->u32ExitCtls & VMX_EXIT_CTLS_LOAD_PAT_MSR)
        pVCpu->cpum.GstCtx.msrPAT = pVmcs->u64HostPatMsr.u;

    /* We don't support IA32_BNDCFGS MSR yet. */
}


/**
 * Loads host segment registers, GDTR, IDTR, LDTR and TR as part of VM-exit.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static void iemVmxVmexitLoadHostSegRegs(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    /*
     * Load host segment registers, GDTR, IDTR, LDTR and TR.
     * See Intel spec. 27.5.2 "Loading Host Segment and Descriptor-Table Registers".
     *
     * Warning! Be careful to not touch fields that are reserved by VT-x,
     * e.g. segment limit high bits stored in segment attributes (in bits 11:8).
     */
    PCVMXVVMCS const pVmcs           = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    bool const       fHostInLongMode = RT_BOOL(pVmcs->u32ExitCtls & VMX_EXIT_CTLS_HOST_ADDR_SPACE_SIZE);

    /* CS, SS, ES, DS, FS, GS. */
    for (unsigned iSegReg = 0; iSegReg < X86_SREG_COUNT; iSegReg++)
    {
        RTSEL const HostSel   = iemVmxVmcsGetHostSelReg(pVmcs, iSegReg);
        bool const  fUnusable = RT_BOOL(HostSel == 0);
        PCPUMSELREG pSelReg   = &pVCpu->cpum.GstCtx.aSRegs[iSegReg];

        /* Selector. */
        pSelReg->Sel      = HostSel;
        pSelReg->ValidSel = HostSel;
        pSelReg->fFlags   = CPUMSELREG_FLAGS_VALID;

        /* Limit. */
        pSelReg->u32Limit = 0xffffffff;

        /* Base. */
        pSelReg->u64Base = 0;

        /* Attributes. */
        if (iSegReg == X86_SREG_CS)
        {
            pSelReg->Attr.n.u4Type        = X86_SEL_TYPE_CODE | X86_SEL_TYPE_READ | X86_SEL_TYPE_ACCESSED;
            pSelReg->Attr.n.u1DescType    = 1;
            pSelReg->Attr.n.u2Dpl         = 0;
            pSelReg->Attr.n.u1Present     = 1;
            pSelReg->Attr.n.u1Long        = fHostInLongMode;
            pSelReg->Attr.n.u1DefBig      = !fHostInLongMode;
            pSelReg->Attr.n.u1Granularity = 1;
            Assert(!pSelReg->Attr.n.u1Unusable);
            Assert(!fUnusable);
        }
        else
        {
            pSelReg->Attr.n.u4Type        = X86_SEL_TYPE_RW | X86_SEL_TYPE_ACCESSED;
            pSelReg->Attr.n.u1DescType    = 1;
            pSelReg->Attr.n.u2Dpl         = 0;
            pSelReg->Attr.n.u1Present     = 1;
            pSelReg->Attr.n.u1DefBig      = 1;
            pSelReg->Attr.n.u1Granularity = 1;
            pSelReg->Attr.n.u1Unusable    = fUnusable;
        }
    }

    /* FS base. */
    if (   !pVCpu->cpum.GstCtx.fs.Attr.n.u1Unusable
        ||  fHostInLongMode)
    {
        Assert(X86_IS_CANONICAL(pVmcs->u64HostFsBase.u));
        pVCpu->cpum.GstCtx.fs.u64Base = pVmcs->u64HostFsBase.u;
    }

    /* GS base. */
    if (   !pVCpu->cpum.GstCtx.gs.Attr.n.u1Unusable
        ||  fHostInLongMode)
    {
        Assert(X86_IS_CANONICAL(pVmcs->u64HostGsBase.u));
        pVCpu->cpum.GstCtx.gs.u64Base = pVmcs->u64HostGsBase.u;
    }

    /* TR. */
    Assert(X86_IS_CANONICAL(pVmcs->u64HostTrBase.u));
    Assert(!pVCpu->cpum.GstCtx.tr.Attr.n.u1Unusable);
    pVCpu->cpum.GstCtx.tr.Sel                  = pVmcs->HostTr;
    pVCpu->cpum.GstCtx.tr.ValidSel             = pVmcs->HostTr;
    pVCpu->cpum.GstCtx.tr.fFlags               = CPUMSELREG_FLAGS_VALID;
    pVCpu->cpum.GstCtx.tr.u32Limit             = X86_SEL_TYPE_SYS_386_TSS_LIMIT_MIN;
    pVCpu->cpum.GstCtx.tr.u64Base              = pVmcs->u64HostTrBase.u;
    pVCpu->cpum.GstCtx.tr.Attr.n.u4Type        = X86_SEL_TYPE_SYS_386_TSS_BUSY;
    pVCpu->cpum.GstCtx.tr.Attr.n.u1DescType    = 0;
    pVCpu->cpum.GstCtx.tr.Attr.n.u2Dpl         = 0;
    pVCpu->cpum.GstCtx.tr.Attr.n.u1Present     = 1;
    pVCpu->cpum.GstCtx.tr.Attr.n.u1DefBig      = 0;
    pVCpu->cpum.GstCtx.tr.Attr.n.u1Granularity = 0;

    /* LDTR (Warning! do not touch the base and limits here). */
    pVCpu->cpum.GstCtx.ldtr.Sel                = 0;
    pVCpu->cpum.GstCtx.ldtr.ValidSel           = 0;
    pVCpu->cpum.GstCtx.ldtr.fFlags             = CPUMSELREG_FLAGS_VALID;
    pVCpu->cpum.GstCtx.ldtr.Attr.u             = X86DESCATTR_UNUSABLE;

    /* GDTR. */
    Assert(X86_IS_CANONICAL(pVmcs->u64HostGdtrBase.u));
    pVCpu->cpum.GstCtx.gdtr.pGdt  = pVmcs->u64HostGdtrBase.u;
    pVCpu->cpum.GstCtx.gdtr.cbGdt = 0xffff;

    /* IDTR.*/
    Assert(X86_IS_CANONICAL(pVmcs->u64HostIdtrBase.u));
    pVCpu->cpum.GstCtx.idtr.pIdt  = pVmcs->u64HostIdtrBase.u;
    pVCpu->cpum.GstCtx.idtr.cbIdt = 0xffff;
}


/**
 * Loads the host MSRs from the VM-exit MSR-load area as part of VM-exit.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uExitReason     The VMX instruction name (for logging purposes).
 */
static int iemVmxVmexitLoadHostAutoMsrs(PVMCPUCC pVCpu, uint32_t uExitReason) RT_NOEXCEPT
{
    /*
     * Load host MSRs.
     * See Intel spec. 27.6 "Loading MSRs".
     */
    PCVMXVVMCS const   pVmcs      = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    const char * const pszFailure = "VMX-abort";

    /*
     * The VM-exit MSR-load area address need not be a valid guest-physical address if the
     * VM-exit MSR load count is 0. If this is the case, bail early without reading it.
     * See Intel spec. 24.7.2 "VM-Exit Controls for MSRs".
     */
    uint32_t const cMsrs = RT_MIN(pVmcs->u32ExitMsrLoadCount, RT_ELEMENTS(pVCpu->cpum.GstCtx.hwvirt.vmx.aExitMsrLoadArea));
    if (!cMsrs)
        return VINF_SUCCESS;

    /*
     * Verify the MSR auto-load count. Physical CPUs can behave unpredictably if the count
     * is exceeded including possibly raising #MC exceptions during VMX transition. Our
     * implementation causes a VMX-abort followed by a triple-fault.
     */
    bool const fIsMsrCountValid = iemVmxIsAutoMsrCountValid(pVCpu, cMsrs);
    if (fIsMsrCountValid)
    { /* likely */ }
    else
        IEM_VMX_VMEXIT_FAILED_RET(pVCpu, uExitReason, pszFailure, kVmxVDiag_Vmexit_MsrLoadCount);

    RTGCPHYS const GCPhysVmExitMsrLoadArea = pVmcs->u64AddrExitMsrLoad.u;
    int rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &pVCpu->cpum.GstCtx.hwvirt.vmx.aExitMsrLoadArea[0],
                                     GCPhysVmExitMsrLoadArea, cMsrs * sizeof(VMXAUTOMSR));
    if (RT_SUCCESS(rc))
    {
        PCVMXAUTOMSR pMsr = pVCpu->cpum.GstCtx.hwvirt.vmx.aExitMsrLoadArea;
        for (uint32_t idxMsr = 0; idxMsr < cMsrs; idxMsr++, pMsr++)
        {
            if (   !pMsr->u32Reserved
                &&  pMsr->u32Msr != MSR_K8_FS_BASE
                &&  pMsr->u32Msr != MSR_K8_GS_BASE
                &&  pMsr->u32Msr != MSR_K6_EFER
                &&  pMsr->u32Msr != MSR_IA32_SMM_MONITOR_CTL
                &&  pMsr->u32Msr >> 8 != MSR_IA32_X2APIC_START >> 8)
            {
                VBOXSTRICTRC rcStrict = CPUMSetGuestMsr(pVCpu, pMsr->u32Msr, pMsr->u64Value);
                if (rcStrict == VINF_SUCCESS)
                    continue;

                /*
                 * If we're in ring-0, we cannot handle returns to ring-3 at this point and continue VM-exit.
                 * If any nested hypervisor loads MSRs that require ring-3 handling, we cause a VMX-abort
                 * recording the MSR index in the auxiliary info. field and indicated further by our
                 * own, specific diagnostic code. Later, we can try implement handling of the MSR in ring-0
                 * if possible, or come up with a better, generic solution.
                 */
                pVCpu->cpum.GstCtx.hwvirt.vmx.uAbortAux = pMsr->u32Msr;
                VMXVDIAG const enmDiag = rcStrict == VINF_CPUM_R3_MSR_WRITE
                                       ? kVmxVDiag_Vmexit_MsrLoadRing3
                                       : kVmxVDiag_Vmexit_MsrLoad;
                IEM_VMX_VMEXIT_FAILED_RET(pVCpu, uExitReason, pszFailure, enmDiag);
            }
            else
                IEM_VMX_VMEXIT_FAILED_RET(pVCpu, uExitReason, pszFailure, kVmxVDiag_Vmexit_MsrLoadRsvd);
        }
    }
    else
    {
        AssertMsgFailed(("VM-exit: Failed to read MSR auto-load area at %#RGp, rc=%Rrc\n", GCPhysVmExitMsrLoadArea, rc));
        IEM_VMX_VMEXIT_FAILED_RET(pVCpu, uExitReason, pszFailure, kVmxVDiag_Vmexit_MsrLoadPtrReadPhys);
    }

    NOREF(uExitReason);
    NOREF(pszFailure);
    return VINF_SUCCESS;
}


/**
 * Loads the host state as part of VM-exit.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uExitReason     The VM-exit reason (for logging purposes).
 */
static VBOXSTRICTRC iemVmxVmexitLoadHostState(PVMCPUCC pVCpu, uint32_t uExitReason) RT_NOEXCEPT
{
    /*
     * Load host state.
     * See Intel spec. 27.5 "Loading Host State".
     */
    PCVMXVVMCS const pVmcs           = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    bool const       fHostInLongMode = RT_BOOL(pVmcs->u32ExitCtls & VMX_EXIT_CTLS_HOST_ADDR_SPACE_SIZE);

    /* We cannot return from a long-mode guest to a host that is not in long mode. */
    if (    CPUMIsGuestInLongMode(pVCpu)
        && !fHostInLongMode)
    {
        Log(("VM-exit from long-mode guest to host not in long-mode -> VMX-Abort\n"));
        return iemVmxAbort(pVCpu, VMXABORT_HOST_NOT_IN_LONG_MODE);
    }

    /*
     * Check host PAE PDPTEs prior to loading the host state.
     * See Intel spec. 26.5.4 "Checking and Loading Host Page-Directory-Pointer-Table Entries".
     */
    if (   (pVmcs->u64HostCr4.u & X86_CR4_PAE)
        && !fHostInLongMode
        && (   !CPUMIsGuestInPAEModeEx(&pVCpu->cpum.GstCtx)
            || pVmcs->u64HostCr3.u != pVCpu->cpum.GstCtx.cr3))
    {
        int const rc = PGMGstMapPaePdpesAtCr3(pVCpu, pVmcs->u64HostCr3.u);
        if (RT_SUCCESS(rc))
        { /* likely*/ }
        else
        {
            IEM_VMX_VMEXIT_FAILED(pVCpu, uExitReason, "VMX-abort", kVmxVDiag_Vmexit_HostPdpte);
            return iemVmxAbort(pVCpu, VMXBOART_HOST_PDPTE);
        }
    }

    iemVmxVmexitLoadHostControlRegsMsrs(pVCpu);
    iemVmxVmexitLoadHostSegRegs(pVCpu);

    /*
     * Load host RIP, RSP and RFLAGS.
     * See Intel spec. 27.5.3 "Loading Host RIP, RSP and RFLAGS"
     */
    pVCpu->cpum.GstCtx.rip      = pVmcs->u64HostRip.u;
    pVCpu->cpum.GstCtx.rsp      = pVmcs->u64HostRsp.u;
    pVCpu->cpum.GstCtx.rflags.u = X86_EFL_1;

    /* Clear address range monitoring. */
    EMMonitorWaitClear(pVCpu);

    /* Perform the VMX transition (PGM updates). */
    VBOXSTRICTRC rcStrict = iemVmxTransition(pVCpu, 0 /*cbInstr - whatever*/);
    if (rcStrict == VINF_SUCCESS)
    { /* likely */ }
    else if (RT_SUCCESS(rcStrict))
    {
        Log3(("VM-exit: iemVmxTransition returns %Rrc (uExitReason=%u) -> Setting passup status\n", VBOXSTRICTRC_VAL(rcStrict),
              uExitReason));
        rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
    }
    else
    {
        Log3(("VM-exit: iemVmxTransition failed! rc=%Rrc (uExitReason=%u)\n", VBOXSTRICTRC_VAL(rcStrict), uExitReason));
        return VBOXSTRICTRC_VAL(rcStrict);
    }

    Assert(rcStrict == VINF_SUCCESS);

    /* Load MSRs from the VM-exit auto-load MSR area. */
    int rc = iemVmxVmexitLoadHostAutoMsrs(pVCpu, uExitReason);
    if (RT_FAILURE(rc))
    {
        Log(("VM-exit failed while loading host MSRs -> VMX-Abort\n"));
        return iemVmxAbort(pVCpu, VMXABORT_LOAD_HOST_MSR);
    }
    return VINF_SUCCESS;
}


/**
 * Gets VM-exit instruction information along with any displacement for an
 * instruction VM-exit.
 *
 * @returns The VM-exit instruction information.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uExitReason     The VM-exit reason.
 * @param   uInstrId        The VM-exit instruction identity (VMXINSTRID_XXX).
 * @param   pGCPtrDisp      Where to store the displacement field. Optional, can be
 *                          NULL.
 */
static uint32_t iemVmxGetExitInstrInfo(PVMCPUCC pVCpu, uint32_t uExitReason, VMXINSTRID uInstrId, PRTGCPTR pGCPtrDisp) RT_NOEXCEPT
{
    RTGCPTR          GCPtrDisp;
    VMXEXITINSTRINFO ExitInstrInfo;
    ExitInstrInfo.u = 0;

    /*
     * Get and parse the ModR/M byte from our decoded opcodes.
     */
    uint8_t bRm;
    uint8_t const offModRm = pVCpu->iem.s.offModRm;
    IEM_MODRM_GET_U8(pVCpu, bRm, offModRm);
    if ((bRm & X86_MODRM_MOD_MASK) == (3 << X86_MODRM_MOD_SHIFT))
    {
        /*
         * ModR/M indicates register addressing.
         *
         * The primary/secondary register operands are reported in the iReg1 or iReg2
         * fields depending on whether it is a read/write form.
         */
        uint8_t idxReg1;
        uint8_t idxReg2;
        if (!VMXINSTRID_IS_MODRM_PRIMARY_OP_W(uInstrId))
        {
            idxReg1 = ((bRm >> X86_MODRM_REG_SHIFT) & X86_MODRM_REG_SMASK) | pVCpu->iem.s.uRexReg;
            idxReg2 = (bRm & X86_MODRM_RM_MASK) | pVCpu->iem.s.uRexB;
        }
        else
        {
            idxReg1 = (bRm & X86_MODRM_RM_MASK) | pVCpu->iem.s.uRexB;
            idxReg2 = ((bRm >> X86_MODRM_REG_SHIFT) & X86_MODRM_REG_SMASK) | pVCpu->iem.s.uRexReg;
        }
        ExitInstrInfo.All.u2Scaling       = 0;
        ExitInstrInfo.All.iReg1           = idxReg1;
        ExitInstrInfo.All.u3AddrSize      = pVCpu->iem.s.enmEffAddrMode;
        ExitInstrInfo.All.fIsRegOperand   = 1;
        ExitInstrInfo.All.uOperandSize    = pVCpu->iem.s.enmEffOpSize;
        ExitInstrInfo.All.iSegReg         = 0;
        ExitInstrInfo.All.iIdxReg         = 0;
        ExitInstrInfo.All.fIdxRegInvalid  = 1;
        ExitInstrInfo.All.iBaseReg        = 0;
        ExitInstrInfo.All.fBaseRegInvalid = 1;
        ExitInstrInfo.All.iReg2           = idxReg2;

        /* Displacement not applicable for register addressing. */
        GCPtrDisp = 0;
    }
    else
    {
        /*
         * ModR/M indicates memory addressing.
         */
        uint8_t  uScale        = 0;
        bool     fBaseRegValid = false;
        bool     fIdxRegValid  = false;
        uint8_t  iBaseReg      = 0;
        uint8_t  iIdxReg       = 0;
        if (pVCpu->iem.s.enmEffAddrMode == IEMMODE_16BIT)
        {
            /*
             * Parse the ModR/M, displacement for 16-bit addressing mode.
             * See Intel instruction spec. Table 2-1. "16-Bit Addressing Forms with the ModR/M Byte".
             */
            uint16_t u16Disp = 0;
            uint8_t const offDisp = offModRm + sizeof(bRm);
            if ((bRm & (X86_MODRM_MOD_MASK | X86_MODRM_RM_MASK)) == 6)
            {
                /* Displacement without any registers. */
                IEM_DISP_GET_U16(pVCpu, u16Disp, offDisp);
            }
            else
            {
                /* Register (index and base). */
                switch (bRm & X86_MODRM_RM_MASK)
                {
                    case 0: fBaseRegValid = true; iBaseReg = X86_GREG_xBX; fIdxRegValid = true; iIdxReg = X86_GREG_xSI; break;
                    case 1: fBaseRegValid = true; iBaseReg = X86_GREG_xBX; fIdxRegValid = true; iIdxReg = X86_GREG_xDI; break;
                    case 2: fBaseRegValid = true; iBaseReg = X86_GREG_xBP; fIdxRegValid = true; iIdxReg = X86_GREG_xSI; break;
                    case 3: fBaseRegValid = true; iBaseReg = X86_GREG_xBP; fIdxRegValid = true; iIdxReg = X86_GREG_xDI; break;
                    case 4: fIdxRegValid  = true; iIdxReg  = X86_GREG_xSI; break;
                    case 5: fIdxRegValid  = true; iIdxReg  = X86_GREG_xDI; break;
                    case 6: fBaseRegValid = true; iBaseReg = X86_GREG_xBP; break;
                    case 7: fBaseRegValid = true; iBaseReg = X86_GREG_xBX; break;
                }

                /* Register + displacement. */
                switch ((bRm >> X86_MODRM_MOD_SHIFT) & X86_MODRM_MOD_SMASK)
                {
                    case 0:                                                  break;
                    case 1: IEM_DISP_GET_S8_SX_U16(pVCpu, u16Disp, offDisp); break;
                    case 2: IEM_DISP_GET_U16(pVCpu, u16Disp, offDisp);       break;
                    default:
                    {
                        /* Register addressing, handled at the beginning. */
                        AssertMsgFailed(("ModR/M %#x implies register addressing, memory addressing expected!", bRm));
                        break;
                    }
                }
            }

            Assert(!uScale);                    /* There's no scaling/SIB byte for 16-bit addressing. */
            GCPtrDisp = (int16_t)u16Disp;       /* Sign-extend the displacement. */
        }
        else if (pVCpu->iem.s.enmEffAddrMode == IEMMODE_32BIT)
        {
            /*
             * Parse the ModR/M, SIB, displacement for 32-bit addressing mode.
             * See Intel instruction spec. Table 2-2. "32-Bit Addressing Forms with the ModR/M Byte".
             */
            uint32_t u32Disp = 0;
            if ((bRm & (X86_MODRM_MOD_MASK | X86_MODRM_RM_MASK)) == 5)
            {
                /* Displacement without any registers. */
                uint8_t const offDisp = offModRm + sizeof(bRm);
                IEM_DISP_GET_U32(pVCpu, u32Disp, offDisp);
            }
            else
            {
                /* Register (and perhaps scale, index and base). */
                uint8_t offDisp = offModRm + sizeof(bRm);
                iBaseReg = (bRm & X86_MODRM_RM_MASK);
                if (iBaseReg == 4)
                {
                    /* An SIB byte follows the ModR/M byte, parse it. */
                    uint8_t bSib;
                    uint8_t const offSib = offModRm + sizeof(bRm);
                    IEM_SIB_GET_U8(pVCpu, bSib, offSib);

                    /* A displacement may follow SIB, update its offset. */
                    offDisp += sizeof(bSib);

                    /* Get the scale. */
                    uScale = (bSib >> X86_SIB_SCALE_SHIFT) & X86_SIB_SCALE_SMASK;

                    /* Get the index register. */
                    iIdxReg = (bSib >> X86_SIB_INDEX_SHIFT) & X86_SIB_INDEX_SMASK;
                    fIdxRegValid = RT_BOOL(iIdxReg != 4);

                    /* Get the base register. */
                    iBaseReg = bSib & X86_SIB_BASE_MASK;
                    fBaseRegValid = true;
                    if (iBaseReg == 5)
                    {
                        if ((bRm & X86_MODRM_MOD_MASK) == 0)
                        {
                            /* Mod is 0 implies a 32-bit displacement with no base. */
                            fBaseRegValid = false;
                            IEM_DISP_GET_U32(pVCpu, u32Disp, offDisp);
                        }
                        else
                        {
                            /* Mod is not 0 implies an 8-bit/32-bit displacement (handled below) with an EBP base. */
                            iBaseReg = X86_GREG_xBP;
                        }
                    }
                }

                /* Register + displacement. */
                switch ((bRm >> X86_MODRM_MOD_SHIFT) & X86_MODRM_MOD_SMASK)
                {
                    case 0: /* Handled above */                              break;
                    case 1: IEM_DISP_GET_S8_SX_U32(pVCpu, u32Disp, offDisp); break;
                    case 2: IEM_DISP_GET_U32(pVCpu, u32Disp, offDisp);       break;
                    default:
                    {
                        /* Register addressing, handled at the beginning. */
                        AssertMsgFailed(("ModR/M %#x implies register addressing, memory addressing expected!", bRm));
                        break;
                    }
                }
            }

            GCPtrDisp = (int32_t)u32Disp;       /* Sign-extend the displacement. */
        }
        else
        {
            Assert(pVCpu->iem.s.enmEffAddrMode == IEMMODE_64BIT);

            /*
             * Parse the ModR/M, SIB, displacement for 64-bit addressing mode.
             * See Intel instruction spec. 2.2 "IA-32e Mode".
             */
            uint64_t u64Disp = 0;
            bool const fRipRelativeAddr = RT_BOOL((bRm & (X86_MODRM_MOD_MASK | X86_MODRM_RM_MASK)) == 5);
            if (fRipRelativeAddr)
            {
                /*
                 * RIP-relative addressing mode.
                 *
                 * The displacement is 32-bit signed implying an offset range of +/-2G.
                 * See Intel instruction spec. 2.2.1.6 "RIP-Relative Addressing".
                 */
                uint8_t const offDisp = offModRm + sizeof(bRm);
                IEM_DISP_GET_S32_SX_U64(pVCpu, u64Disp, offDisp);
            }
            else
            {
                uint8_t offDisp = offModRm + sizeof(bRm);

                /*
                 * Register (and perhaps scale, index and base).
                 *
                 * REX.B extends the most-significant bit of the base register. However, REX.B
                 * is ignored while determining whether an SIB follows the opcode. Hence, we
                 * shall OR any REX.B bit -after- inspecting for an SIB byte below.
                 *
                 * See Intel instruction spec. Table 2-5. "Special Cases of REX Encodings".
                 */
                iBaseReg = (bRm & X86_MODRM_RM_MASK);
                if (iBaseReg == 4)
                {
                    /* An SIB byte follows the ModR/M byte, parse it. Displacement (if any) follows SIB. */
                    uint8_t bSib;
                    uint8_t const offSib = offModRm + sizeof(bRm);
                    IEM_SIB_GET_U8(pVCpu, bSib, offSib);

                    /* Displacement may follow SIB, update its offset. */
                    offDisp += sizeof(bSib);

                    /* Get the scale. */
                    uScale = (bSib >> X86_SIB_SCALE_SHIFT) & X86_SIB_SCALE_SMASK;

                    /* Get the index. */
                    iIdxReg = ((bSib >> X86_SIB_INDEX_SHIFT) & X86_SIB_INDEX_SMASK) | pVCpu->iem.s.uRexIndex;
                    fIdxRegValid = RT_BOOL(iIdxReg != 4);   /* R12 -can- be used as an index register. */

                    /* Get the base. */
                    iBaseReg = (bSib & X86_SIB_BASE_MASK);
                    fBaseRegValid = true;
                    if (iBaseReg == 5)
                    {
                        if ((bRm & X86_MODRM_MOD_MASK) == 0)
                        {
                            /* Mod is 0 implies a signed 32-bit displacement with no base. */
                            IEM_DISP_GET_S32_SX_U64(pVCpu, u64Disp, offDisp);
                        }
                        else
                        {
                            /* Mod is non-zero implies an 8-bit/32-bit displacement (handled below) with RBP or R13 as base. */
                            iBaseReg = pVCpu->iem.s.uRexB ? X86_GREG_x13 : X86_GREG_xBP;
                        }
                    }
                }
                iBaseReg |= pVCpu->iem.s.uRexB;

                /* Register + displacement. */
                switch ((bRm >> X86_MODRM_MOD_SHIFT) & X86_MODRM_MOD_SMASK)
                {
                    case 0: /* Handled above */                               break;
                    case 1: IEM_DISP_GET_S8_SX_U64(pVCpu, u64Disp, offDisp);  break;
                    case 2: IEM_DISP_GET_S32_SX_U64(pVCpu, u64Disp, offDisp); break;
                    default:
                    {
                        /* Register addressing, handled at the beginning. */
                        AssertMsgFailed(("ModR/M %#x implies register addressing, memory addressing expected!", bRm));
                        break;
                    }
                }
            }

            GCPtrDisp = fRipRelativeAddr ? pVCpu->cpum.GstCtx.rip + u64Disp : u64Disp;
        }

        /*
         * The primary or secondary register operand is reported in iReg2 depending
         * on whether the primary operand is in read/write form.
         */
        uint8_t idxReg2;
        if (!VMXINSTRID_IS_MODRM_PRIMARY_OP_W(uInstrId))
        {
            idxReg2 = bRm & X86_MODRM_RM_MASK;
            if (pVCpu->iem.s.enmEffAddrMode == IEMMODE_64BIT)
                idxReg2 |= pVCpu->iem.s.uRexB;
        }
        else
        {
            idxReg2 = (bRm >> X86_MODRM_REG_SHIFT) & X86_MODRM_REG_SMASK;
            if (pVCpu->iem.s.enmEffAddrMode == IEMMODE_64BIT)
                 idxReg2 |= pVCpu->iem.s.uRexReg;
        }
        ExitInstrInfo.All.u2Scaling      = uScale;
        ExitInstrInfo.All.iReg1          = 0;   /* Not applicable for memory addressing. */
        ExitInstrInfo.All.u3AddrSize     = pVCpu->iem.s.enmEffAddrMode;
        ExitInstrInfo.All.fIsRegOperand  = 0;
        ExitInstrInfo.All.uOperandSize   = pVCpu->iem.s.enmEffOpSize;
        ExitInstrInfo.All.iSegReg        = pVCpu->iem.s.iEffSeg;
        ExitInstrInfo.All.iIdxReg        = iIdxReg;
        ExitInstrInfo.All.fIdxRegInvalid = !fIdxRegValid;
        ExitInstrInfo.All.iBaseReg       = iBaseReg;
        ExitInstrInfo.All.iIdxReg        = !fBaseRegValid;
        ExitInstrInfo.All.iReg2          = idxReg2;
    }

    /*
     * Handle exceptions to the norm for certain instructions.
     * (e.g. some instructions convey an instruction identity in place of iReg2).
     */
    switch (uExitReason)
    {
        case VMX_EXIT_GDTR_IDTR_ACCESS:
        {
            Assert(VMXINSTRID_IS_VALID(uInstrId));
            Assert(VMXINSTRID_GET_ID(uInstrId) == (uInstrId & 0x3));
            ExitInstrInfo.GdtIdt.u2InstrId = VMXINSTRID_GET_ID(uInstrId);
            ExitInstrInfo.GdtIdt.u2Undef0  = 0;
            break;
        }

        case VMX_EXIT_LDTR_TR_ACCESS:
        {
            Assert(VMXINSTRID_IS_VALID(uInstrId));
            Assert(VMXINSTRID_GET_ID(uInstrId) == (uInstrId & 0x3));
            ExitInstrInfo.LdtTr.u2InstrId = VMXINSTRID_GET_ID(uInstrId);
            ExitInstrInfo.LdtTr.u2Undef0 = 0;
            break;
        }

        case VMX_EXIT_RDRAND:
        case VMX_EXIT_RDSEED:
        {
            Assert(VMXINSTRID_IS_VALID(uInstrId));
            Assert(VMXINSTRID_GET_ID(uInstrId) == (uInstrId & 0x3));
            Assert(GCPtrDisp == 0);
            Assert(ExitInstrInfo.RdrandRdseed.u2OperandSize != 3);
            ExitInstrInfo.RdrandRdseed.u3Undef0  = 0;
            ExitInstrInfo.RdrandRdseed.u4Undef0  = 0;
            ExitInstrInfo.RdrandRdseed.u19Undef0 = 0;
            break;
        }
    }

    /* Update displacement and return the constructed VM-exit instruction information field. */
    if (pGCPtrDisp)
        *pGCPtrDisp = GCPtrDisp;

    return ExitInstrInfo.u;
}


/**
 * VMX VM-exit handler.
 *
 * @returns Strict VBox status code.
 * @retval VINF_VMX_VMEXIT when the VM-exit is successful.
 * @retval VINF_EM_TRIPLE_FAULT when VM-exit is unsuccessful and leads to a
 *         triple-fault.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uExitReason     The VM-exit reason.
 * @param   u64ExitQual     The Exit qualification.
 *
 * @remarks We need not necessarily have completed VM-entry before a VM-exit is
 *          called. Failures during VM-entry can cause VM-exits as well, so we
 *          -cannot- assert we're in VMX non-root mode here.
 */
VBOXSTRICTRC iemVmxVmexit(PVMCPUCC pVCpu, uint32_t uExitReason, uint64_t u64ExitQual) RT_NOEXCEPT
{
# if defined(VBOX_WITH_NESTED_HWVIRT_ONLY_IN_IEM) && !defined(IN_RING3)
    RT_NOREF3(pVCpu, uExitReason, u64ExitQual);
    AssertMsgFailed(("VM-exit should only be invoked from ring-3 when nested-guest executes only in ring-3!\n"));
    return VERR_IEM_IPE_7;
# else
    PVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;

    /* Just count this as an exit and be done with that. */
    pVCpu->iem.s.cPotentialExits++;

    /*
     * Import all the guest-CPU state.
     *
     * HM on returning to guest execution would have to reset up a whole lot of state
     * anyway, (e.g., VM-entry/VM-exit controls) and we do not ever import a part of
     * the state and flag reloading the entire state on re-entry. So import the entire
     * state here, see HMNotifyVmxNstGstVmexit() for more comments.
     */
    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_ALL);

    /*
     * Ensure VM-entry interruption information valid bit is cleared.
     *
     * We do it here on every VM-exit so that even premature VM-exits (e.g. those caused
     * by invalid-guest state or machine-check exceptions) also clear this bit.
     *
     * See Intel spec. 27.2 "Recording VM-exit Information And Updating VM-entry control fields".
     */
    if (VMX_ENTRY_INT_INFO_IS_VALID(pVmcs->u32EntryIntInfo))
        pVmcs->u32EntryIntInfo &= ~VMX_ENTRY_INT_INFO_VALID;

    /*
     * Update the VM-exit reason and Exit qualification.
     * Other VMCS read-only data fields are expected to be updated by the caller already.
     */
    pVmcs->u32RoExitReason = uExitReason;
    pVmcs->u64RoExitQual.u = u64ExitQual;

    Log2(("vmexit: reason=%u qual=%#RX64 cs:rip=%04x:%08RX64 cr0=%#RX64 cr3=%#RX64 cr4=%#RX64 eflags=%#RX32\n", uExitReason,
          pVmcs->u64RoExitQual.u, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.cr0,
          pVCpu->cpum.GstCtx.cr3, pVCpu->cpum.GstCtx.cr4, pVCpu->cpum.GstCtx.eflags.u));

    /*
     * Update the IDT-vectoring information fields if the VM-exit is triggered during delivery of an event.
     * See Intel spec. 27.2.4 "Information for VM Exits During Event Delivery".
     */
    {
        uint8_t    uVector;
        uint32_t   fFlags;
        uint32_t   uErrCode;
        bool const fInEventDelivery = IEMGetCurrentXcpt(pVCpu, &uVector, &fFlags, &uErrCode, NULL /* puCr2 */);
        if (fInEventDelivery)
        {
            /*
             * A VM-exit is not considered to occur during event delivery when the VM-exit is
             * caused by a triple-fault or the original event results in a double-fault that
             * causes the VM exit directly (exception bitmap). Therefore, we must not set the
             * original event information into the IDT-vectoring information fields.
             *
             * See Intel spec. 27.2.4 "Information for VM Exits During Event Delivery".
             */
            if (   uExitReason != VMX_EXIT_TRIPLE_FAULT
                && (   uExitReason != VMX_EXIT_XCPT_OR_NMI
                    || !VMX_EXIT_INT_INFO_IS_XCPT_DF(pVmcs->u32RoExitIntInfo)))
            {
                uint8_t  const uIdtVectoringType = iemVmxGetEventType(uVector, fFlags);
                uint8_t  const fErrCodeValid     = RT_BOOL(fFlags & IEM_XCPT_FLAGS_ERR);
                uint32_t const uIdtVectoringInfo = RT_BF_MAKE(VMX_BF_IDT_VECTORING_INFO_VECTOR,         uVector)
                                                 | RT_BF_MAKE(VMX_BF_IDT_VECTORING_INFO_TYPE,           uIdtVectoringType)
                                                 | RT_BF_MAKE(VMX_BF_IDT_VECTORING_INFO_ERR_CODE_VALID, fErrCodeValid)
                                                 | RT_BF_MAKE(VMX_BF_IDT_VECTORING_INFO_VALID,          1);
                iemVmxVmcsSetIdtVectoringInfo(pVCpu, uIdtVectoringInfo);
                iemVmxVmcsSetIdtVectoringErrCode(pVCpu, uErrCode);
                Log2(("vmexit: idt_info=%#RX32 idt_err_code=%#RX32 cr2=%#RX64\n", uIdtVectoringInfo, uErrCode,
                      pVCpu->cpum.GstCtx.cr2));
            }
        }
    }

    /* The following VMCS fields should always be zero since we don't support injecting SMIs into a guest. */
    Assert(pVmcs->u64RoIoRcx.u == 0);
    Assert(pVmcs->u64RoIoRsi.u == 0);
    Assert(pVmcs->u64RoIoRdi.u == 0);
    Assert(pVmcs->u64RoIoRip.u == 0);

    /*
     * Save the guest state back into the VMCS.
     * We only need to save the state when the VM-entry was successful.
     */
    bool const fVmentryFailed = VMX_EXIT_REASON_HAS_ENTRY_FAILED(uExitReason);
    if (!fVmentryFailed)
    {
        /* We should not cause an NMI-window/interrupt-window VM-exit when injecting events as part of VM-entry. */
        if (!CPUMIsGuestVmxInterceptEvents(&pVCpu->cpum.GstCtx))
        {
            Assert(uExitReason != VMX_EXIT_NMI_WINDOW);
            Assert(uExitReason != VMX_EXIT_INT_WINDOW);
        }

        /* For exception or NMI VM-exits, the VM-exit interruption info. field must be valid. */
        Assert(uExitReason != VMX_EXIT_XCPT_OR_NMI || VMX_EXIT_INT_INFO_IS_VALID(pVmcs->u32RoExitIntInfo));

        /* For external interrupts that occur while "acknowledge interrupt on exit" VM-exit is set,
           the VM-exit interruption info. field must be valid. */
        Assert(   uExitReason != VMX_EXIT_EXT_INT
               || !(pVmcs->u32ExitCtls & VMX_EXIT_CTLS_ACK_EXT_INT)
               || VMX_EXIT_INT_INFO_IS_VALID(pVmcs->u32RoExitIntInfo));

        /*
         * If we support storing EFER.LMA into IA32e-mode guest field on VM-exit, we need to do that now.
         * See Intel spec. 27.2 "Recording VM-exit Information And Updating VM-entry Control".
         *
         * It is not clear from the Intel spec. if this is done only when VM-entry succeeds.
         * If a VM-exit happens before loading guest EFER, we risk restoring the host EFER.LMA
         * as guest-CPU state would not been modified. Hence for now, we do this only when
         * the VM-entry succeeded.
         */
        /** @todo r=ramshankar: Figure out if this bit gets set to host EFER.LMA on real
         *        hardware when VM-exit fails during VM-entry (e.g. VERR_VMX_INVALID_GUEST_STATE). */
        if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fVmxExitSaveEferLma)
        {
            if (pVCpu->cpum.GstCtx.msrEFER & MSR_K6_EFER_LMA)
                pVmcs->u32EntryCtls |= VMX_ENTRY_CTLS_IA32E_MODE_GUEST;
            else
                pVmcs->u32EntryCtls &= ~VMX_ENTRY_CTLS_IA32E_MODE_GUEST;
        }

        /*
         * The rest of the high bits of the VM-exit reason are only relevant when the VM-exit
         * occurs in enclave mode/SMM which we don't support yet.
         *
         * If we ever add support for it, we can pass just the lower bits to the functions
         * below, till then an assert should suffice.
         */
        Assert(!RT_HI_U16(uExitReason));

        /* Save the guest state into the VMCS and restore guest MSRs from the auto-store guest MSR area. */
        iemVmxVmexitSaveGuestState(pVCpu, uExitReason);
        int rc = iemVmxVmexitSaveGuestAutoMsrs(pVCpu, uExitReason);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
            return iemVmxAbort(pVCpu, VMXABORT_SAVE_GUEST_MSRS);

        /* Clear any saved NMI-blocking state so we don't assert on next VM-entry (if it was in effect on the previous one). */
        pVCpu->cpum.GstCtx.hwvirt.fSavedInhibit &= ~CPUMCTX_INHIBIT_NMI;
    }
    else
    {
        /* Restore the NMI-blocking state if VM-entry failed due to invalid guest state or while loading MSRs. */
        uint32_t const uExitReasonBasic = VMX_EXIT_REASON_BASIC(uExitReason);
        if (   uExitReasonBasic == VMX_EXIT_ERR_INVALID_GUEST_STATE
            || uExitReasonBasic == VMX_EXIT_ERR_MSR_LOAD)
            iemVmxVmexitRestoreNmiBlockingFF(pVCpu);
    }

    /*
     * Stop any running VMX-preemption timer if necessary.
     */
    if (pVmcs->u32PinCtls & VMX_PIN_CTLS_PREEMPT_TIMER)
        CPUMStopGuestVmxPremptTimer(pVCpu);

    /*
     * Clear the state of "NMI unblocked due to IRET" as otherwise we risk
     * reporting a stale state on a subsequent VM-exit. This state will be
     * re-established while emulating IRET in VMX non-root mode.
     */
    pVCpu->cpum.GstCtx.hwvirt.vmx.fNmiUnblockingIret = false;

    /*
     * Clear any pending VMX nested-guest force-flags.
     * These force-flags have no effect on (outer) guest execution and will
     * be re-evaluated and setup on the next nested-guest VM-entry.
     */
    VMCPU_FF_CLEAR_MASK(pVCpu, VMCPU_FF_VMX_ALL_MASK);

    /*
     * We're no longer in nested-guest execution mode.
     *
     * It is important to do this prior to loading the host state because
     * PGM looks at fInVmxNonRootMode to determine if it needs to perform
     * second-level address translation while switching to host CR3.
     */
    pVCpu->cpum.GstCtx.hwvirt.vmx.fInVmxNonRootMode = false;

    /* Restore the host (outer guest) state. */
    VBOXSTRICTRC rcStrict = iemVmxVmexitLoadHostState(pVCpu, uExitReason);
    if (RT_SUCCESS(rcStrict))
    {
        Assert(rcStrict == VINF_SUCCESS);
        rcStrict = VINF_VMX_VMEXIT;
    }
    else
        Log(("vmexit: Loading host-state failed. uExitReason=%u rc=%Rrc\n", uExitReason, VBOXSTRICTRC_VAL(rcStrict)));

    /*
     * Restore non-zero Secondary-processor based VM-execution controls
     * when the "activate secondary controls" bit was not set.
     */
    if (pVmcs->u32RestoreProcCtls2)
    {
        Assert(!(pVmcs->u32ProcCtls & VMX_PROC_CTLS_USE_SECONDARY_CTLS));
        pVmcs->u32ProcCtls2        = pVmcs->u32RestoreProcCtls2;
        pVmcs->u32RestoreProcCtls2 = 0;
    }

    if (VM_IS_HM_ENABLED(pVCpu->CTX_SUFF(pVM)))
    {
        /* Notify HM that the current VMCS fields have been modified. */
        HMNotifyVmxNstGstCurrentVmcsChanged(pVCpu);

        /* Notify HM that we've completed the VM-exit. */
        HMNotifyVmxNstGstVmexit(pVCpu);
    }

#  if defined(VBOX_WITH_NESTED_HWVIRT_ONLY_IN_IEM) && defined(IN_RING3)
    /* Revert any IEM-only nested-guest execution policy, otherwise return rcStrict. */
    Log(("vmexit: Disabling IEM-only EM execution policy!\n"));
    int rcSched = EMR3SetExecutionPolicy(pVCpu->CTX_SUFF(pVM)->pUVM, EMEXECPOLICY_IEM_ALL, false);
    if (rcSched != VINF_SUCCESS)
        iemSetPassUpStatus(pVCpu, rcSched);
#  endif
    return rcStrict;
# endif
}


/**
 * VMX VM-exit handler for VM-exits due to instruction execution.
 *
 * This is intended for instructions where the caller provides all the relevant
 * VM-exit information.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pExitInfo       Pointer to the VM-exit information.
 */
static VBOXSTRICTRC iemVmxVmexitInstrWithInfo(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo) RT_NOEXCEPT
{
    /*
     * For instructions where any of the following fields are not applicable:
     *   - Exit qualification must be cleared.
     *   - VM-exit instruction info. is undefined.
     *   - Guest-linear address is undefined.
     *   - Guest-physical address is undefined.
     *
     * The VM-exit instruction length is mandatory for all VM-exits that are caused by
     * instruction execution. For VM-exits that are not due to instruction execution this
     * field is undefined.
     *
     * In our implementation in IEM, all undefined fields are generally cleared. However,
     * if the caller supplies information (from say the physical CPU directly) it is
     * then possible that the undefined fields are not cleared.
     *
     * See Intel spec. 27.2.1 "Basic VM-Exit Information".
     * See Intel spec. 27.2.4 "Information for VM Exits Due to Instruction Execution".
     */
    Assert(pExitInfo);
    AssertMsg(pExitInfo->uReason <= VMX_EXIT_MAX, ("uReason=%u\n", pExitInfo->uReason));
    AssertMsg(pExitInfo->cbInstr >= 1 && pExitInfo->cbInstr <= 15,
              ("uReason=%u cbInstr=%u\n", pExitInfo->uReason, pExitInfo->cbInstr));

    /* Update all the relevant fields from the VM-exit instruction information struct. */
    iemVmxVmcsSetExitInstrInfo(pVCpu, pExitInfo->InstrInfo.u);
    iemVmxVmcsSetExitGuestLinearAddr(pVCpu, pExitInfo->u64GuestLinearAddr);
    iemVmxVmcsSetExitGuestPhysAddr(pVCpu, pExitInfo->u64GuestPhysAddr);
    iemVmxVmcsSetExitInstrLen(pVCpu, pExitInfo->cbInstr);

    /* Perform the VM-exit. */
    return iemVmxVmexit(pVCpu, pExitInfo->uReason, pExitInfo->u64Qual);
}


/**
 * VMX VM-exit handler for VM-exits due to instruction execution.
 *
 * This is intended for instructions that only provide the VM-exit instruction
 * length.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uExitReason     The VM-exit reason.
 * @param   cbInstr         The instruction length in bytes.
 */
VBOXSTRICTRC iemVmxVmexitInstr(PVMCPUCC pVCpu, uint32_t uExitReason, uint8_t cbInstr) RT_NOEXCEPT
{
#ifdef VBOX_STRICT
    /*
     * To prevent us from shooting ourselves in the foot.
     * The follow instructions should convey more than just the instruction length.
     */
    switch (uExitReason)
    {
        case VMX_EXIT_INVEPT:
        case VMX_EXIT_INVPCID:
        case VMX_EXIT_INVVPID:
        case VMX_EXIT_LDTR_TR_ACCESS:
        case VMX_EXIT_GDTR_IDTR_ACCESS:
        case VMX_EXIT_VMCLEAR:
        case VMX_EXIT_VMPTRLD:
        case VMX_EXIT_VMPTRST:
        case VMX_EXIT_VMREAD:
        case VMX_EXIT_VMWRITE:
        case VMX_EXIT_VMXON:
        case VMX_EXIT_XRSTORS:
        case VMX_EXIT_XSAVES:
        case VMX_EXIT_RDRAND:
        case VMX_EXIT_RDSEED:
        case VMX_EXIT_IO_INSTR:
            AssertMsgFailedReturn(("Use iemVmxVmexitInstrNeedsInfo for uExitReason=%u\n", uExitReason), VERR_IEM_IPE_5);
            break;
    }
#endif

    VMXVEXITINFO const ExitInfo = VMXVEXITINFO_INIT_WITH_INSTR_LEN(uExitReason, cbInstr);
    return iemVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
}


/**
 * Interface for HM and EM to emulate VM-exit due to a triple-fault.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecVmxVmexitTripleFault(PVMCPUCC pVCpu)
{
    VBOXSTRICTRC rcStrict = iemVmxVmexit(pVCpu, VMX_EXIT_TRIPLE_FAULT, 0 /* u64ExitQual */);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemExecStatusCodeFiddling(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate VM-exit due to startup-IPI (SIPI).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   uVector         The SIPI vector.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecVmxVmexitStartupIpi(PVMCPUCC pVCpu, uint8_t uVector)
{
    VBOXSTRICTRC rcStrict = iemVmxVmexit(pVCpu, VMX_EXIT_SIPI, uVector);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemExecStatusCodeFiddling(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate a VM-exit.
 *
 * If a specialized version of a VM-exit handler exists, that must be used instead.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   uExitReason     The VM-exit reason.
 * @param   u64ExitQual     The Exit qualification.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecVmxVmexit(PVMCPUCC pVCpu, uint32_t uExitReason, uint64_t u64ExitQual)
{
    VBOXSTRICTRC rcStrict = iemVmxVmexit(pVCpu, uExitReason, u64ExitQual);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemExecStatusCodeFiddling(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate a VM-exit due to an instruction.
 *
 * This is meant to be used for those instructions that VMX provides additional
 * decoding information beyond just the instruction length!
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   pExitInfo   Pointer to the VM-exit information.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecVmxVmexitInstrWithInfo(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo)
{
    VBOXSTRICTRC rcStrict = iemVmxVmexitInstrWithInfo(pVCpu, pExitInfo);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemExecStatusCodeFiddling(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate a VM-exit due to an instruction.
 *
 * This is meant to be used for those instructions that VMX provides only the
 * instruction length.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   pExitInfo   Pointer to the VM-exit information.
 * @param   cbInstr     The instruction length in bytes.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecVmxVmexitInstr(PVMCPUCC pVCpu, uint32_t uExitReason, uint8_t cbInstr)
{
    VBOXSTRICTRC rcStrict = iemVmxVmexitInstr(pVCpu, uExitReason, cbInstr);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemExecStatusCodeFiddling(pVCpu, rcStrict);
}


/**
 * VMX VM-exit handler for VM-exits due to instruction execution.
 *
 * This is intended for instructions that have a ModR/M byte and update the VM-exit
 * instruction information and Exit qualification fields.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uExitReason     The VM-exit reason.
 * @param   uInstrid        The instruction identity (VMXINSTRID_XXX).
 * @param   cbInstr         The instruction length in bytes.
 *
 * @remarks Do not use this for INS/OUTS instruction.
 */
VBOXSTRICTRC iemVmxVmexitInstrNeedsInfo(PVMCPUCC pVCpu, uint32_t uExitReason, VMXINSTRID uInstrId, uint8_t cbInstr) RT_NOEXCEPT
{
#ifdef VBOX_STRICT
    /*
    * To prevent us from shooting ourselves in the foot.
    * The follow instructions convey specific info that require using their respective handlers.
     */
    switch (uExitReason)
    {
        case VMX_EXIT_INVEPT:
        case VMX_EXIT_INVPCID:
        case VMX_EXIT_INVVPID:
        case VMX_EXIT_LDTR_TR_ACCESS:
        case VMX_EXIT_GDTR_IDTR_ACCESS:
        case VMX_EXIT_VMCLEAR:
        case VMX_EXIT_VMPTRLD:
        case VMX_EXIT_VMPTRST:
        case VMX_EXIT_VMREAD:
        case VMX_EXIT_VMWRITE:
        case VMX_EXIT_VMXON:
        case VMX_EXIT_XRSTORS:
        case VMX_EXIT_XSAVES:
        case VMX_EXIT_RDRAND:
        case VMX_EXIT_RDSEED:
            break;
        default:
            AssertMsgFailedReturn(("Use instruction-specific handler\n"), VERR_IEM_IPE_5);
            break;
    }
#endif

    /*
     * Update the Exit qualification field with displacement bytes.
     * See Intel spec. 27.2.1 "Basic VM-Exit Information".
     */
    /* Construct the VM-exit instruction information. */
    RTGCPTR            GCPtrDisp;
    uint32_t const     uInstrInfo = iemVmxGetExitInstrInfo(pVCpu, uExitReason, uInstrId, &GCPtrDisp);

    VMXVEXITINFO const ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO(uExitReason, GCPtrDisp, uInstrInfo, cbInstr);
    return iemVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
}


/**
 * VMX VM-exit handler for VM-exits due to INVLPG.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   GCPtrPage   The guest-linear address of the page being invalidated.
 * @param   cbInstr     The instruction length in bytes.
 */
VBOXSTRICTRC iemVmxVmexitInstrInvlpg(PVMCPUCC pVCpu, RTGCPTR GCPtrPage, uint8_t cbInstr) RT_NOEXCEPT
{
    VMXVEXITINFO const ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN(VMX_EXIT_INVLPG, GCPtrPage, cbInstr);
    Assert(IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode || !RT_HI_U32(ExitInfo.u64Qual));
    return iemVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
}


/**
 * VMX VM-exit handler for VM-exits due to LMSW.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uGuestCr0       The current guest CR0.
 * @param   pu16NewMsw      The machine-status word specified in LMSW's source
 *                          operand. This will be updated depending on the VMX
 *                          guest/host CR0 mask if LMSW is not intercepted.
 * @param   GCPtrEffDst     The guest-linear address of the source operand in case
 *                          of a memory operand. For register operand, pass
 *                          NIL_RTGCPTR.
 * @param   cbInstr         The instruction length in bytes.
 */
VBOXSTRICTRC iemVmxVmexitInstrLmsw(PVMCPUCC pVCpu, uint32_t uGuestCr0, uint16_t *pu16NewMsw,
                                   RTGCPTR GCPtrEffDst, uint8_t cbInstr) RT_NOEXCEPT
{
    Assert(pu16NewMsw);

    uint16_t const uNewMsw = *pu16NewMsw;
    if (CPUMIsGuestVmxLmswInterceptSet(&pVCpu->cpum.GstCtx, uNewMsw))
    {
        Log2(("lmsw: Guest intercept -> VM-exit\n"));
        bool const fMemOperand = RT_BOOL(GCPtrEffDst != NIL_RTGCPTR);
        VMXVEXITINFO ExitInfo
            = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN(VMX_EXIT_MOV_CRX,
                                                          RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_REGISTER,  0) /* CR0 */
                                                        | RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_LMSW_OP,   fMemOperand)
                                                        | RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_LMSW_DATA, uNewMsw)
                                                        | RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_ACCESS,  VMX_EXIT_QUAL_CRX_ACCESS_LMSW),
                                                        cbInstr);
        if (fMemOperand)
        {
            Assert(IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode || !RT_HI_U32(GCPtrEffDst));
            ExitInfo.u64GuestLinearAddr = GCPtrEffDst;
        }
        return iemVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
    }

    /*
     * If LMSW did not cause a VM-exit, any CR0 bits in the range 0:3 that is set in the
     * CR0 guest/host mask must be left unmodified.
     *
     * See Intel Spec. 25.3 "Changes To Instruction Behavior In VMX Non-root Operation".
     */
    uint32_t const fGstHostMask     = pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64Cr0Mask.u;
    uint32_t const fGstHostLmswMask = fGstHostMask & (X86_CR0_PE | X86_CR0_MP | X86_CR0_EM | X86_CR0_TS);
    *pu16NewMsw = (uGuestCr0 & fGstHostLmswMask) | (uNewMsw & ~fGstHostLmswMask);

    return VINF_VMX_INTERCEPT_NOT_ACTIVE;
}


/**
 * VMX VM-exit handler for VM-exits due to CLTS.
 *
 * @returns Strict VBox status code.
 * @retval VINF_VMX_MODIFIES_BEHAVIOR if the CLTS instruction did not cause a
 *         VM-exit but must not modify the guest CR0.TS bit.
 * @retval VINF_VMX_INTERCEPT_NOT_ACTIVE if the CLTS instruction did not cause a
 *         VM-exit and modification to the guest CR0.TS bit is allowed (subject to
 *         CR0 fixed bits in VMX operation).
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The instruction length in bytes.
 */
VBOXSTRICTRC iemVmxVmexitInstrClts(PVMCPUCC pVCpu, uint8_t cbInstr) RT_NOEXCEPT
{
    /*
     * If CR0.TS is owned by the host:
     *   - If CR0.TS is set in the read-shadow, we must cause a VM-exit.
     *   - If CR0.TS is cleared in the read-shadow, no VM-exit is caused and the
     *     CLTS instruction completes without clearing CR0.TS.
     *
     * See Intel spec. 25.1.3 "Instructions That Cause VM Exits Conditionally".
     */
    uint32_t const fGstHostMask = pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64Cr0Mask.u;
    if (fGstHostMask & X86_CR0_TS)
    {
        if (pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64Cr0ReadShadow.u & X86_CR0_TS)
        {
            Log2(("clts: Guest intercept -> VM-exit\n"));
            VMXVEXITINFO const ExitInfo
                = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN(VMX_EXIT_MOV_CRX,
                                                              RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_REGISTER, 0) /* CR0 */
                                                            | RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_ACCESS,
                                                                         VMX_EXIT_QUAL_CRX_ACCESS_CLTS),
                                                            cbInstr);
            return iemVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
        }
        return VINF_VMX_MODIFIES_BEHAVIOR;
    }

    /*
     * If CR0.TS is not owned by the host, the CLTS instructions operates normally
     * and may modify CR0.TS (subject to CR0 fixed bits in VMX operation).
     */
    return VINF_VMX_INTERCEPT_NOT_ACTIVE;
}


/**
 * VMX VM-exit handler for VM-exits due to 'Mov CR0,GReg' and 'Mov CR4,GReg'
 * (CR0/CR4 write).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   iCrReg      The control register (either CR0 or CR4).
 * @param   uGuestCrX   The current guest CR0/CR4.
 * @param   puNewCrX    Pointer to the new CR0/CR4 value. Will be updated if no
 *                      VM-exit is caused.
 * @param   iGReg       The general register from which the CR0/CR4 value is being
 *                      loaded.
 * @param   cbInstr     The instruction length in bytes.
 */
VBOXSTRICTRC iemVmxVmexitInstrMovToCr0Cr4(PVMCPUCC pVCpu, uint8_t iCrReg, uint64_t *puNewCrX,
                                          uint8_t iGReg, uint8_t cbInstr) RT_NOEXCEPT
{
    Assert(puNewCrX);
    Assert(iCrReg == 0 || iCrReg == 4);
    Assert(iGReg < X86_GREG_COUNT);

    uint64_t const uNewCrX = *puNewCrX;
    if (CPUMIsGuestVmxMovToCr0Cr4InterceptSet(&pVCpu->cpum.GstCtx, iCrReg, uNewCrX))
    {
        Log2(("mov_Cr_Rd: (CR%u) Guest intercept -> VM-exit\n", iCrReg));
        VMXVEXITINFO const ExitInfo
            = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN(VMX_EXIT_MOV_CRX,
                                                          RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_REGISTER, iCrReg)
                                                        | RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_GENREG,   iGReg)
                                                        | RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_ACCESS, VMX_EXIT_QUAL_CRX_ACCESS_WRITE),
                                                        cbInstr);
        return iemVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
    }

    /*
     * If the Mov-to-CR0/CR4 did not cause a VM-exit, any bits owned by the host
     * must not be modified the instruction.
     *
     * See Intel Spec. 25.3 "Changes To Instruction Behavior In VMX Non-root Operation".
     */
    uint64_t uGuestCrX;
    uint64_t fGstHostMask;
    if (iCrReg == 0)
    {
        IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0);
        uGuestCrX    = pVCpu->cpum.GstCtx.cr0;
        fGstHostMask = pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64Cr0Mask.u;
    }
    else
    {
        IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR4);
        uGuestCrX    = pVCpu->cpum.GstCtx.cr4;
        fGstHostMask = pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64Cr4Mask.u;
    }

    *puNewCrX = (uGuestCrX & fGstHostMask) | (*puNewCrX & ~fGstHostMask);
    return VINF_VMX_INTERCEPT_NOT_ACTIVE;
}


/**
 * VMX VM-exit handler for VM-exits due to 'Mov GReg,CR3' (CR3 read).
 *
 * @returns VBox strict status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   iGReg       The general register to which the CR3 value is being stored.
 * @param   cbInstr     The instruction length in bytes.
 */
VBOXSTRICTRC iemVmxVmexitInstrMovFromCr3(PVMCPUCC pVCpu, uint8_t iGReg, uint8_t cbInstr) RT_NOEXCEPT
{
    Assert(iGReg < X86_GREG_COUNT);
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR3);

    /*
     * If the CR3-store exiting control is set, we must cause a VM-exit.
     * See Intel spec. 25.1.3 "Instructions That Cause VM Exits Conditionally".
     */
    if (pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ProcCtls & VMX_PROC_CTLS_CR3_STORE_EXIT)
    {
        Log2(("mov_Rd_Cr: (CR3) Guest intercept -> VM-exit\n"));
        VMXVEXITINFO const ExitInfo
            = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN(VMX_EXIT_MOV_CRX,
                                                          RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_REGISTER, 3) /* CR3 */
                                                        | RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_GENREG,   iGReg)
                                                        | RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_ACCESS, VMX_EXIT_QUAL_CRX_ACCESS_READ),
                                                        cbInstr);
        return iemVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
    }
    return VINF_VMX_INTERCEPT_NOT_ACTIVE;
}


/**
 * VMX VM-exit handler for VM-exits due to 'Mov CR3,GReg' (CR3 write).
 *
 * @returns VBox strict status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uNewCr3     The new CR3 value.
 * @param   iGReg       The general register from which the CR3 value is being
 *                      loaded.
 * @param   cbInstr     The instruction length in bytes.
 */
VBOXSTRICTRC iemVmxVmexitInstrMovToCr3(PVMCPUCC pVCpu, uint64_t uNewCr3, uint8_t iGReg, uint8_t cbInstr) RT_NOEXCEPT
{
    Assert(iGReg < X86_GREG_COUNT);

    /*
     * If the CR3-load exiting control is set and the new CR3 value does not
     * match any of the CR3-target values in the VMCS, we must cause a VM-exit.
     *
     * See Intel spec. 25.1.3 "Instructions That Cause VM Exits Conditionally".
     */
    if (CPUMIsGuestVmxMovToCr3InterceptSet(pVCpu, uNewCr3))
    {
        Log2(("mov_Cr_Rd: (CR3) Guest intercept -> VM-exit\n"));
        VMXVEXITINFO const ExitInfo
            = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN(VMX_EXIT_MOV_CRX,
                                                           RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_REGISTER, 3) /* CR3 */
                                                         | RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_GENREG,   iGReg)
                                                         | RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_ACCESS,
                                                                      VMX_EXIT_QUAL_CRX_ACCESS_WRITE),
                                                         cbInstr);
        return iemVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
    }
    return VINF_VMX_INTERCEPT_NOT_ACTIVE;
}


/**
 * VMX VM-exit handler for VM-exits due to 'Mov GReg,CR8' (CR8 read).
 *
 * @returns VBox strict status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   iGReg       The general register to which the CR8 value is being stored.
 * @param   cbInstr     The instruction length in bytes.
 */
VBOXSTRICTRC iemVmxVmexitInstrMovFromCr8(PVMCPUCC pVCpu, uint8_t iGReg, uint8_t cbInstr) RT_NOEXCEPT
{
    Assert(iGReg < X86_GREG_COUNT);

    /*
     * If the CR8-store exiting control is set, we must cause a VM-exit.
     * See Intel spec. 25.1.3 "Instructions That Cause VM Exits Conditionally".
     */
    if (pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ProcCtls & VMX_PROC_CTLS_CR8_STORE_EXIT)
    {
        Log2(("mov_Rd_Cr: (CR8) Guest intercept -> VM-exit\n"));
        VMXVEXITINFO const ExitInfo
            = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN(VMX_EXIT_MOV_CRX,
                                                          RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_REGISTER, 8) /* CR8 */
                                                        | RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_GENREG,   iGReg)
                                                        | RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_ACCESS, VMX_EXIT_QUAL_CRX_ACCESS_READ),
                                                        cbInstr);
        return iemVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
    }
    return VINF_VMX_INTERCEPT_NOT_ACTIVE;
}


/**
 * VMX VM-exit handler for VM-exits due to 'Mov CR8,GReg' (CR8 write).
 *
 * @returns VBox strict status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   iGReg       The general register from which the CR8 value is being
 *                      loaded.
 * @param   cbInstr     The instruction length in bytes.
 */
VBOXSTRICTRC iemVmxVmexitInstrMovToCr8(PVMCPUCC pVCpu, uint8_t iGReg, uint8_t cbInstr) RT_NOEXCEPT
{
    Assert(iGReg < X86_GREG_COUNT);

    /*
     * If the CR8-load exiting control is set, we must cause a VM-exit.
     * See Intel spec. 25.1.3 "Instructions That Cause VM Exits Conditionally".
     */
    if (pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ProcCtls & VMX_PROC_CTLS_CR8_LOAD_EXIT)
    {
        Log2(("mov_Cr_Rd: (CR8) Guest intercept -> VM-exit\n"));
        VMXVEXITINFO const ExitInfo
            = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN(VMX_EXIT_MOV_CRX,
                                                          RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_REGISTER, 8) /* CR8 */
                                                        | RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_GENREG,   iGReg)
                                                        | RT_BF_MAKE(VMX_BF_EXIT_QUAL_CRX_ACCESS, VMX_EXIT_QUAL_CRX_ACCESS_WRITE),
                                                        cbInstr);
        return iemVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
    }
    return VINF_VMX_INTERCEPT_NOT_ACTIVE;
}


/**
 * VMX VM-exit handler for VM-exits due to 'Mov DRx,GReg' (DRx write) and 'Mov
 * GReg,DRx' (DRx read).
 *
 * @returns VBox strict status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uInstrid    The instruction identity (VMXINSTRID_MOV_TO_DRX or
 *                      VMXINSTRID_MOV_FROM_DRX).
 * @param   iDrReg      The debug register being accessed.
 * @param   iGReg       The general register to/from which the DRx value is being
 *                      store/loaded.
 * @param   cbInstr     The instruction length in bytes.
 */
VBOXSTRICTRC iemVmxVmexitInstrMovDrX(PVMCPUCC pVCpu, VMXINSTRID uInstrId, uint8_t iDrReg,
                                     uint8_t iGReg, uint8_t cbInstr) RT_NOEXCEPT
{
    Assert(iDrReg <= 7);
    Assert(uInstrId == VMXINSTRID_MOV_TO_DRX || uInstrId == VMXINSTRID_MOV_FROM_DRX);
    Assert(iGReg < X86_GREG_COUNT);

    if (pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ProcCtls & VMX_PROC_CTLS_MOV_DR_EXIT)
    {
        VMXVEXITINFO const ExitInfo
            = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN(VMX_EXIT_MOV_DRX,
                                                          RT_BF_MAKE(VMX_BF_EXIT_QUAL_DRX_REGISTER,  iDrReg)
                                                        | RT_BF_MAKE(VMX_BF_EXIT_QUAL_DRX_GENREG,    iGReg)
                                                        | RT_BF_MAKE(VMX_BF_EXIT_QUAL_DRX_DIRECTION,
                                                                     uInstrId == VMXINSTRID_MOV_TO_DRX
                                                                     ? VMX_EXIT_QUAL_DRX_DIRECTION_WRITE
                                                                     : VMX_EXIT_QUAL_DRX_DIRECTION_READ),
                                                        cbInstr);
        return iemVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
    }

    return VINF_VMX_INTERCEPT_NOT_ACTIVE;
}


/**
 * VMX VM-exit handler for VM-exits due to I/O instructions (IN and OUT).
 *
 * @returns VBox strict status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uInstrId    The VM-exit instruction identity (VMXINSTRID_IO_IN or
 *                      VMXINSTRID_IO_OUT).
 * @param   u16Port     The I/O port being accessed.
 * @param   fImm        Whether the I/O port was encoded using an immediate operand
 *                      or the implicit DX register.
 * @param   cbAccess    The size of the I/O access in bytes (1, 2 or 4 bytes).
 * @param   cbInstr     The instruction length in bytes.
 */
VBOXSTRICTRC iemVmxVmexitInstrIo(PVMCPUCC pVCpu, VMXINSTRID uInstrId, uint16_t u16Port,
                                 bool fImm, uint8_t cbAccess, uint8_t cbInstr) RT_NOEXCEPT
{
    Assert(uInstrId == VMXINSTRID_IO_IN || uInstrId == VMXINSTRID_IO_OUT);
    Assert(cbAccess == 1 || cbAccess == 2 || cbAccess == 4);

    if (CPUMIsGuestVmxIoInterceptSet(pVCpu, u16Port, cbAccess))
    {
        VMXVEXITINFO const ExitInfo
            = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN(VMX_EXIT_IO_INSTR,
                                                          RT_BF_MAKE(VMX_BF_EXIT_QUAL_IO_WIDTH,     cbAccess - 1)
                                                        | RT_BF_MAKE(VMX_BF_EXIT_QUAL_IO_ENCODING,  fImm)
                                                        | RT_BF_MAKE(VMX_BF_EXIT_QUAL_IO_PORT,      u16Port)
                                                        | RT_BF_MAKE(VMX_BF_EXIT_QUAL_IO_DIRECTION,
                                                                     uInstrId == VMXINSTRID_IO_IN
                                                                     ? VMX_EXIT_QUAL_IO_DIRECTION_IN
                                                                     : VMX_EXIT_QUAL_IO_DIRECTION_OUT),
                                                        cbInstr);
        return iemVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
    }
    return VINF_VMX_INTERCEPT_NOT_ACTIVE;
}


/**
 * VMX VM-exit handler for VM-exits due to string I/O instructions (INS and OUTS).
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uInstrId        The VM-exit instruction identity (VMXINSTRID_IO_INS or
 *                          VMXINSTRID_IO_OUTS).
 * @param   u16Port         The I/O port being accessed.
 * @param   cbAccess        The size of the I/O access in bytes (1, 2 or 4 bytes).
 * @param   fRep            Whether the instruction has a REP prefix or not.
 * @param   ExitInstrInfo   The VM-exit instruction info. field.
 * @param   cbInstr         The instruction length in bytes.
 */
VBOXSTRICTRC iemVmxVmexitInstrStrIo(PVMCPUCC pVCpu, VMXINSTRID uInstrId, uint16_t u16Port, uint8_t cbAccess,
                                    bool fRep, VMXEXITINSTRINFO ExitInstrInfo, uint8_t cbInstr) RT_NOEXCEPT
{
    Assert(uInstrId == VMXINSTRID_IO_INS || uInstrId == VMXINSTRID_IO_OUTS);
    Assert(cbAccess == 1 || cbAccess == 2 || cbAccess == 4);
    Assert(ExitInstrInfo.StrIo.iSegReg < X86_SREG_COUNT);
    Assert(ExitInstrInfo.StrIo.u3AddrSize == 0 || ExitInstrInfo.StrIo.u3AddrSize == 1 || ExitInstrInfo.StrIo.u3AddrSize == 2);
    Assert(uInstrId != VMXINSTRID_IO_INS || ExitInstrInfo.StrIo.iSegReg == X86_SREG_ES);

    if (CPUMIsGuestVmxIoInterceptSet(pVCpu, u16Port, cbAccess))
    {
        /*
         * Figure out the guest-linear address and the direction bit (INS/OUTS).
         */
        /** @todo r=ramshankar: Is there something in IEM that already does this? */
        static uint64_t const s_auAddrSizeMasks[] = { UINT64_C(0xffff), UINT64_C(0xffffffff), UINT64_C(0xffffffffffffffff) };
        uint8_t const  iSegReg       = ExitInstrInfo.StrIo.iSegReg;
        uint8_t const  uAddrSize     = ExitInstrInfo.StrIo.u3AddrSize;
        uint64_t const uAddrSizeMask = s_auAddrSizeMasks[uAddrSize];

        uint32_t uDirection;
        uint64_t uGuestLinearAddr;
        if (uInstrId == VMXINSTRID_IO_INS)
        {
            uDirection       = VMX_EXIT_QUAL_IO_DIRECTION_IN;
            uGuestLinearAddr = pVCpu->cpum.GstCtx.aSRegs[iSegReg].u64Base + (pVCpu->cpum.GstCtx.rdi & uAddrSizeMask);
        }
        else
        {
            uDirection       = VMX_EXIT_QUAL_IO_DIRECTION_OUT;
            uGuestLinearAddr = pVCpu->cpum.GstCtx.aSRegs[iSegReg].u64Base + (pVCpu->cpum.GstCtx.rsi & uAddrSizeMask);
        }

        /*
         * If the segment is unusable, the guest-linear address in undefined.
         * We shall clear it for consistency.
         *
         * See Intel spec. 27.2.1 "Basic VM-Exit Information".
         */
        if (pVCpu->cpum.GstCtx.aSRegs[iSegReg].Attr.n.u1Unusable)
            uGuestLinearAddr = 0;

        VMXVEXITINFO const ExitInfo
            = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_INFO_AND_LIN_ADDR(VMX_EXIT_IO_INSTR,
                                                                        RT_BF_MAKE(VMX_BF_EXIT_QUAL_IO_WIDTH,   cbAccess - 1)
                                                                      | RT_BF_MAKE(VMX_BF_EXIT_QUAL_IO_DIRECTION, uDirection)
                                                                      | RT_BF_MAKE(VMX_BF_EXIT_QUAL_IO_IS_STRING, 1)
                                                                      | RT_BF_MAKE(VMX_BF_EXIT_QUAL_IO_IS_REP,    fRep)
                                                                      | RT_BF_MAKE(VMX_BF_EXIT_QUAL_IO_ENCODING,
                                                                                   VMX_EXIT_QUAL_IO_ENCODING_DX)
                                                                      | RT_BF_MAKE(VMX_BF_EXIT_QUAL_IO_PORT,      u16Port),
                                                                      IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fVmxInsOutInfo
                                                                      ? ExitInstrInfo.u : 0,
                                                                      cbInstr,
                                                                      uGuestLinearAddr);
        return iemVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
    }

    return VINF_VMX_INTERCEPT_NOT_ACTIVE;
}


/**
 * VMX VM-exit handler for VM-exits due to MWAIT.
 *
 * @returns VBox strict status code.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   fMonitorHwArmed     Whether the address-range monitor hardware is armed.
 * @param   cbInstr             The instruction length in bytes.
 */
VBOXSTRICTRC iemVmxVmexitInstrMwait(PVMCPUCC pVCpu, bool fMonitorHwArmed, uint8_t cbInstr) RT_NOEXCEPT
{
    VMXVEXITINFO const ExitInfo = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN(VMX_EXIT_MWAIT, fMonitorHwArmed, cbInstr);
    return iemVmxVmexitInstrWithInfo(pVCpu, &ExitInfo);
}


/**
 * VMX VM-exit handler for VM-exits due to PAUSE.
 *
 * @returns VBox strict status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The instruction length in bytes.
 */
static VBOXSTRICTRC iemVmxVmexitInstrPause(PVMCPUCC pVCpu, uint8_t cbInstr) RT_NOEXCEPT
{
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;

    /*
     * The PAUSE VM-exit is controlled by the "PAUSE exiting" control and the
     * "PAUSE-loop exiting" control.
     *
     * The PLE-Gap is the maximum number of TSC ticks between two successive executions of
     * the PAUSE instruction before we cause a VM-exit. The PLE-Window is the maximum amount
     * of TSC ticks the guest is allowed to execute in a pause loop before we must cause
     * a VM-exit.
     *
     * See Intel spec. 24.6.13 "Controls for PAUSE-Loop Exiting".
     * See Intel spec. 25.1.3 "Instructions That Cause VM Exits Conditionally".
     */
    bool fIntercept = false;
    if (pVmcs->u32ProcCtls & VMX_PROC_CTLS_PAUSE_EXIT)
        fIntercept = true;
    else if (   (pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_PAUSE_LOOP_EXIT)
             && IEM_GET_CPL(pVCpu) == 0)
    {
        IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_HWVIRT);

        /*
         * A previous-PAUSE-tick value of 0 is used to identify the first time
         * execution of a PAUSE instruction after VM-entry at CPL 0. We must
         * consider this to be the first execution of PAUSE in a loop according
         * to the Intel.
         *
         * All subsequent records for the previous-PAUSE-tick we ensure that it
         * cannot be zero by OR'ing 1 to rule out the TSC wrap-around cases at 0.
         */
        uint64_t *puFirstPauseLoopTick = &pVCpu->cpum.GstCtx.hwvirt.vmx.uFirstPauseLoopTick;
        uint64_t *puPrevPauseTick      = &pVCpu->cpum.GstCtx.hwvirt.vmx.uPrevPauseTick;
        uint64_t const  uTick          = TMCpuTickGet(pVCpu);
        uint32_t const  uPleGap        = pVmcs->u32PleGap;
        uint32_t const  uPleWindow     = pVmcs->u32PleWindow;
        if (   *puPrevPauseTick == 0
            || uTick - *puPrevPauseTick > uPleGap)
            *puFirstPauseLoopTick = uTick;
        else if (uTick - *puFirstPauseLoopTick > uPleWindow)
            fIntercept = true;

        *puPrevPauseTick = uTick | 1;
    }

    if (fIntercept)
        return iemVmxVmexitInstr(pVCpu, VMX_EXIT_PAUSE, cbInstr);

    return VINF_VMX_INTERCEPT_NOT_ACTIVE;
}


/**
 * VMX VM-exit handler for VM-exits due to task switches.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   enmTaskSwitch   The cause of the task switch.
 * @param   SelNewTss       The selector of the new TSS.
 * @param   cbInstr         The instruction length in bytes.
 */
VBOXSTRICTRC iemVmxVmexitTaskSwitch(PVMCPUCC pVCpu, IEMTASKSWITCH enmTaskSwitch, RTSEL SelNewTss, uint8_t cbInstr) RT_NOEXCEPT
{
    /*
     * Task-switch VM-exits are unconditional and provide the Exit qualification.
     *
     * If the cause of the task switch is due to execution of CALL, IRET or the JMP
     * instruction or delivery of the exception generated by one of these instructions
     * lead to a task switch through a task gate in the IDT, we need to provide the
     * VM-exit instruction length. Any other means of invoking a task switch VM-exit
     * leaves the VM-exit instruction length field undefined.
     *
     * See Intel spec. 25.2 "Other Causes Of VM Exits".
     * See Intel spec. 27.2.4 "Information for VM Exits Due to Instruction Execution".
     */
    Assert(cbInstr <= 15);

    uint8_t uType;
    switch (enmTaskSwitch)
    {
        case IEMTASKSWITCH_CALL:        uType = VMX_EXIT_QUAL_TASK_SWITCH_TYPE_CALL; break;
        case IEMTASKSWITCH_IRET:        uType = VMX_EXIT_QUAL_TASK_SWITCH_TYPE_IRET; break;
        case IEMTASKSWITCH_JUMP:        uType = VMX_EXIT_QUAL_TASK_SWITCH_TYPE_JMP;  break;
        case IEMTASKSWITCH_INT_XCPT:    uType = VMX_EXIT_QUAL_TASK_SWITCH_TYPE_IDT;  break;
        IEM_NOT_REACHED_DEFAULT_CASE_RET();
    }

    uint64_t const u64ExitQual = RT_BF_MAKE(VMX_BF_EXIT_QUAL_TASK_SWITCH_NEW_TSS, SelNewTss)
                               | RT_BF_MAKE(VMX_BF_EXIT_QUAL_TASK_SWITCH_SOURCE,  uType);
    iemVmxVmcsSetExitInstrLen(pVCpu, cbInstr);
    return iemVmxVmexit(pVCpu, VMX_EXIT_TASK_SWITCH, u64ExitQual);
}


/**
 * VMX VM-exit handler for trap-like VM-exits.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pExitInfo       Pointer to the VM-exit information.
 * @param   pExitEventInfo  Pointer to the VM-exit event information.
 */
static VBOXSTRICTRC iemVmxVmexitTrapLikeWithInfo(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo) RT_NOEXCEPT
{
    Assert(VMXIsVmexitTrapLike(pExitInfo->uReason));
    iemVmxVmcsSetGuestPendingDbgXcpts(pVCpu, pExitInfo->u64GuestPendingDbgXcpts);
    return iemVmxVmexit(pVCpu, pExitInfo->uReason, pExitInfo->u64Qual);
}


/**
 * Interface for HM and EM to emulate a trap-like VM-exit (MTF, APIC-write,
 * Virtualized-EOI, TPR-below threshold).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pExitInfo       Pointer to the VM-exit information.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecVmxVmexitTrapLike(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo)
{
    Assert(pExitInfo);
    VBOXSTRICTRC rcStrict = iemVmxVmexitTrapLikeWithInfo(pVCpu, pExitInfo);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemExecStatusCodeFiddling(pVCpu, rcStrict);
}


/**
 * VMX VM-exit handler for VM-exits due to task switches.
 *
 * This is intended for task switches where the caller provides all the relevant
 * VM-exit information.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pExitInfo       Pointer to the VM-exit information.
 * @param   pExitEventInfo  Pointer to the VM-exit event information.
 */
static VBOXSTRICTRC iemVmxVmexitTaskSwitchWithInfo(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo,
                                                   PCVMXVEXITEVENTINFO pExitEventInfo) RT_NOEXCEPT
{
    Assert(pExitInfo->uReason == VMX_EXIT_TASK_SWITCH);
    iemVmxVmcsSetExitInstrLen(pVCpu, pExitInfo->cbInstr);
    iemVmxVmcsSetIdtVectoringInfo(pVCpu, pExitEventInfo->uIdtVectoringInfo);
    iemVmxVmcsSetIdtVectoringErrCode(pVCpu, pExitEventInfo->uIdtVectoringErrCode);
    return iemVmxVmexit(pVCpu, VMX_EXIT_TASK_SWITCH, pExitInfo->u64Qual);
}


/**
 * Interface for HM and EM to emulate a VM-exit due to a task switch.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pExitInfo       Pointer to the VM-exit information.
 * @param   pExitEventInfo  Pointer to the VM-exit event information.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecVmxVmexitTaskSwitch(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo, PCVMXVEXITEVENTINFO pExitEventInfo)
{
    Assert(pExitInfo);
    Assert(pExitEventInfo);
    Assert(pExitInfo->uReason == VMX_EXIT_TASK_SWITCH);
    VBOXSTRICTRC rcStrict = iemVmxVmexitTaskSwitchWithInfo(pVCpu, pExitInfo, pExitEventInfo);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemExecStatusCodeFiddling(pVCpu, rcStrict);
}


/**
 * VMX VM-exit handler for VM-exits due to expiring of the preemption timer.
 *
 * @returns VBox strict status code.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VBOXSTRICTRC iemVmxVmexitPreemptTimer(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    Assert(VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_PREEMPT_TIMER));
    Assert(pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32PinCtls & VMX_PIN_CTLS_PREEMPT_TIMER);

    /* Import the hardware virtualization state (for nested-guest VM-entry TSC-tick). */
    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_HWVIRT);

    /* Save the VMX-preemption timer value (of 0) back in to the VMCS if the CPU supports this feature. */
    if (pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ExitCtls & VMX_EXIT_CTLS_SAVE_PREEMPT_TIMER)
        pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32PreemptTimer = 0;

    /* Cause the VMX-preemption timer VM-exit. The Exit qualification MBZ. */
    return iemVmxVmexit(pVCpu, VMX_EXIT_PREEMPT_TIMER, 0 /* u64ExitQual */);
}


/**
 * Interface for HM and EM to emulate VM-exit due to expiry of the preemption timer.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecVmxVmexitPreemptTimer(PVMCPUCC pVCpu)
{
    VBOXSTRICTRC rcStrict = iemVmxVmexitPreemptTimer(pVCpu);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemExecStatusCodeFiddling(pVCpu, rcStrict);
}


/**
 * VMX VM-exit handler for VM-exits due to external interrupts.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uVector         The external interrupt vector (pass 0 if the interrupt
 *                          is still pending since we typically won't know the
 *                          vector).
 * @param   fIntPending     Whether the external interrupt is pending or
 *                          acknowledged in the interrupt controller.
 */
static VBOXSTRICTRC iemVmxVmexitExtInt(PVMCPUCC pVCpu, uint8_t uVector, bool fIntPending) RT_NOEXCEPT
{
    Assert(!fIntPending || uVector == 0);

    /* The VM-exit is subject to "External interrupt exiting" being set. */
    if (pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32PinCtls & VMX_PIN_CTLS_EXT_INT_EXIT)
    {
        if (fIntPending)
        {
            /*
             * If the interrupt is pending and we don't need to acknowledge the
             * interrupt on VM-exit, cause the VM-exit immediately.
             *
             * See Intel spec 25.2 "Other Causes Of VM Exits".
             */
            if (!(pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ExitCtls & VMX_EXIT_CTLS_ACK_EXT_INT))
                return iemVmxVmexit(pVCpu, VMX_EXIT_EXT_INT, 0 /* u64ExitQual */);

            /*
             * If the interrupt is pending and we -do- need to acknowledge the interrupt
             * on VM-exit, postpone VM-exit till after the interrupt controller has been
             * acknowledged that the interrupt has been consumed. Callers would have to call
             * us again after getting the vector (and ofc, with fIntPending with false).
             */
            return VINF_VMX_INTERCEPT_NOT_ACTIVE;
        }

        /*
         * If the interrupt is no longer pending (i.e. it has been acknowledged) and the
         * "External interrupt exiting" and "Acknowledge interrupt on VM-exit" controls are
         * all set, we need to record the vector of the external interrupt in the
         * VM-exit interruption information field. Otherwise, mark this field as invalid.
         *
         * See Intel spec. 27.2.2 "Information for VM Exits Due to Vectored Events".
         */
        uint32_t uExitIntInfo;
        if (pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ExitCtls & VMX_EXIT_CTLS_ACK_EXT_INT)
        {
            bool const fNmiUnblocking = pVCpu->cpum.GstCtx.hwvirt.vmx.fNmiUnblockingIret;
            uExitIntInfo = RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_VECTOR,           uVector)
                         | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_TYPE,             VMX_EXIT_INT_INFO_TYPE_EXT_INT)
                         | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_NMI_UNBLOCK_IRET, fNmiUnblocking)
                         | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_VALID,            1);
        }
        else
            uExitIntInfo = 0;
        iemVmxVmcsSetExitIntInfo(pVCpu, uExitIntInfo);

        /*
         * Cause the VM-exit whether or not the vector has been stored
         * in the VM-exit interruption-information field.
         */
        return iemVmxVmexit(pVCpu, VMX_EXIT_EXT_INT, 0 /* u64ExitQual */);
    }

    return VINF_VMX_INTERCEPT_NOT_ACTIVE;
}


/**
 * Interface for HM and EM to emulate VM-exit due to external interrupts.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   uVector         The external interrupt vector (pass 0 if the external
 *                          interrupt is still pending).
 * @param   fIntPending     Whether the external interrupt is pending or
 *                          acknowledged in the interrupt controller.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecVmxVmexitExtInt(PVMCPUCC pVCpu, uint8_t uVector, bool fIntPending)
{
    VBOXSTRICTRC rcStrict = iemVmxVmexitExtInt(pVCpu, uVector, fIntPending);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemExecStatusCodeFiddling(pVCpu, rcStrict);
}


/**
 * VMX VM-exit handler for VM-exits due to a double fault caused during delivery of
 * an event.
 *
 * @returns VBox strict status code.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VBOXSTRICTRC iemVmxVmexitEventDoubleFault(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    uint32_t const fXcptBitmap = pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32XcptBitmap;
    if (fXcptBitmap & RT_BIT(X86_XCPT_DF))
    {
        /*
         * The NMI-unblocking due to IRET field need not be set for double faults.
         * See Intel spec. 31.7.1.2 "Resuming Guest Software After Handling An Exception".
         */
        uint32_t const uExitIntInfo   = RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_VECTOR,           X86_XCPT_DF)
                                      | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_TYPE,             VMX_EXIT_INT_INFO_TYPE_HW_XCPT)
                                      | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_ERR_CODE_VALID,   1)
                                      | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_NMI_UNBLOCK_IRET, 0)
                                      | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_VALID,            1);
        iemVmxVmcsSetExitIntInfo(pVCpu, uExitIntInfo);
        return iemVmxVmexit(pVCpu, VMX_EXIT_XCPT_OR_NMI, 0 /* u64ExitQual */);
    }

    return VINF_VMX_INTERCEPT_NOT_ACTIVE;
}


/**
 * VMX VM-exit handler for VM-exit due to delivery of an events.
 *
 * This is intended for VM-exit due to exceptions or NMIs where the caller provides
 * all the relevant VM-exit information.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pExitInfo       Pointer to the VM-exit information.
 * @param   pExitEventInfo  Pointer to the VM-exit event information.
 */
static VBOXSTRICTRC iemVmxVmexitEventWithInfo(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo, PCVMXVEXITEVENTINFO pExitEventInfo) RT_NOEXCEPT
{
    Assert(pExitInfo);
    Assert(pExitEventInfo);
    Assert(pExitInfo->uReason == VMX_EXIT_XCPT_OR_NMI);
    Assert(VMX_EXIT_INT_INFO_IS_VALID(pExitEventInfo->uExitIntInfo));

    iemVmxVmcsSetExitInstrLen(pVCpu, pExitInfo->cbInstr);
    iemVmxVmcsSetExitIntInfo(pVCpu, pExitEventInfo->uExitIntInfo);
    iemVmxVmcsSetExitIntErrCode(pVCpu, pExitEventInfo->uExitIntErrCode);
    iemVmxVmcsSetIdtVectoringInfo(pVCpu, pExitEventInfo->uIdtVectoringInfo);
    iemVmxVmcsSetIdtVectoringErrCode(pVCpu, pExitEventInfo->uIdtVectoringErrCode);
    return iemVmxVmexit(pVCpu, VMX_EXIT_XCPT_OR_NMI, pExitInfo->u64Qual);
}


/**
 * Interface for HM and EM to emulate VM-exit due to NMIs.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecVmxVmexitXcptNmi(PVMCPUCC pVCpu)
{
    VMXVEXITINFO const      ExitInfo      = VMXVEXITINFO_INIT_ONLY_REASON(VMX_EXIT_XCPT_OR_NMI);
    VMXVEXITEVENTINFO const ExitEventInfo = VMXVEXITEVENTINFO_INIT_ONLY_INT(  RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_VALID, 1)
                                                                            | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_TYPE,
                                                                                         VMX_EXIT_INT_INFO_TYPE_NMI)
                                                                            | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_VECTOR,
                                                                                         X86_XCPT_NMI),
                                                                            0);
    VBOXSTRICTRC rcStrict = iemVmxVmexitEventWithInfo(pVCpu, &ExitInfo, &ExitEventInfo);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemExecStatusCodeFiddling(pVCpu, rcStrict);
}


/**
 * Interface for HM and EM to emulate VM-exit due to exceptions.
 *
 * Exception includes NMIs, software exceptions (those generated by INT3 or
 * INTO) and privileged software exceptions (those generated by INT1/ICEBP).
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pExitInfo       Pointer to the VM-exit information.
 * @param   pExitEventInfo  Pointer to the VM-exit event information.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecVmxVmexitXcpt(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo, PCVMXVEXITEVENTINFO pExitEventInfo)
{
    Assert(pExitInfo);
    Assert(pExitEventInfo);
    VBOXSTRICTRC rcStrict = iemVmxVmexitEventWithInfo(pVCpu, pExitInfo, pExitEventInfo);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemExecStatusCodeFiddling(pVCpu, rcStrict);
}


/**
 * VMX VM-exit handler for VM-exits due to delivery of an event.
 *
 * @returns VBox strict status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uVector     The interrupt / exception vector.
 * @param   fFlags      The flags (see IEM_XCPT_FLAGS_XXX).
 * @param   uErrCode    The error code associated with the event.
 * @param   uCr2        The CR2 value in case of a \#PF exception.
 * @param   cbInstr     The instruction length in bytes.
 */
VBOXSTRICTRC iemVmxVmexitEvent(PVMCPUCC pVCpu, uint8_t uVector, uint32_t fFlags, uint32_t uErrCode,
                               uint64_t uCr2, uint8_t cbInstr) RT_NOEXCEPT
{
    /*
     * If the event is being injected as part of VM-entry, it is -not- subject to event
     * intercepts in the nested-guest. However, secondary exceptions that occur during
     * injection of any event -are- subject to event interception.
     *
     * See Intel spec. 26.5.1.2 "VM Exits During Event Injection".
     */
    if (!CPUMIsGuestVmxInterceptEvents(&pVCpu->cpum.GstCtx))
    {
        /*
         * If the event is a virtual-NMI (which is an NMI being inject during VM-entry)
         * virtual-NMI blocking must be set in effect rather than physical NMI blocking.
         *
         * See Intel spec. 24.6.1 "Pin-Based VM-Execution Controls".
         */
        if (   uVector == X86_XCPT_NMI
            && (fFlags & IEM_XCPT_FLAGS_T_CPU_XCPT)
            && (pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32PinCtls & VMX_PIN_CTLS_VIRT_NMI))
            pVCpu->cpum.GstCtx.hwvirt.vmx.fVirtNmiBlocking = true;
        else
            Assert(!pVCpu->cpum.GstCtx.hwvirt.vmx.fVirtNmiBlocking);

        CPUMSetGuestVmxInterceptEvents(&pVCpu->cpum.GstCtx, true);
        return VINF_VMX_INTERCEPT_NOT_ACTIVE;
    }

    /*
     * We are injecting an external interrupt, check if we need to cause a VM-exit now.
     * If not, the caller will continue delivery of the external interrupt as it would
     * normally. The interrupt is no longer pending in the interrupt controller at this
     * point.
     */
    if (fFlags & IEM_XCPT_FLAGS_T_EXT_INT)
    {
        Assert(!VMX_IDT_VECTORING_INFO_IS_VALID(pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32RoIdtVectoringInfo));
        return iemVmxVmexitExtInt(pVCpu, uVector, false /* fIntPending */);
    }

    /*
     * Evaluate intercepts for hardware exceptions, software exceptions (#BP, #OF),
     * and privileged software exceptions (#DB generated by INT1/ICEBP) and software
     * interrupts.
     */
    Assert(fFlags & (IEM_XCPT_FLAGS_T_CPU_XCPT | IEM_XCPT_FLAGS_T_SOFT_INT));
    bool fIntercept;
    if (   !(fFlags & IEM_XCPT_FLAGS_T_SOFT_INT)
        ||  (fFlags & (IEM_XCPT_FLAGS_BP_INSTR | IEM_XCPT_FLAGS_OF_INSTR | IEM_XCPT_FLAGS_ICEBP_INSTR)))
        fIntercept = CPUMIsGuestVmxXcptInterceptSet(&pVCpu->cpum.GstCtx, uVector, uErrCode);
    else
    {
        /* Software interrupts cannot be intercepted and therefore do not cause a VM-exit. */
        fIntercept = false;
    }

    /*
     * Now that we've determined whether the event causes a VM-exit, we need to construct the
     * relevant VM-exit information and cause the VM-exit.
     */
    if (fIntercept)
    {
        Assert(!(fFlags & IEM_XCPT_FLAGS_T_EXT_INT));

        /* Construct the rest of the event related information fields and cause the VM-exit. */
        uint64_t u64ExitQual;
        if (uVector == X86_XCPT_PF)
        {
            Assert(fFlags & IEM_XCPT_FLAGS_CR2);
            u64ExitQual = uCr2;
        }
        else if (uVector == X86_XCPT_DB)
        {
            IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_DR6);
            u64ExitQual = pVCpu->cpum.GstCtx.dr[6] & VMX_VMCS_EXIT_QUAL_VALID_MASK;
        }
        else
            u64ExitQual = 0;

        uint8_t  const fNmiUnblocking = pVCpu->cpum.GstCtx.hwvirt.vmx.fNmiUnblockingIret;
        bool     const fErrCodeValid  = RT_BOOL(fFlags & IEM_XCPT_FLAGS_ERR);
        uint8_t  const uIntInfoType   = iemVmxGetEventType(uVector, fFlags);
        uint32_t const uExitIntInfo   = RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_VECTOR,           uVector)
                                      | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_TYPE,             uIntInfoType)
                                      | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_ERR_CODE_VALID,   fErrCodeValid)
                                      | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_NMI_UNBLOCK_IRET, fNmiUnblocking)
                                      | RT_BF_MAKE(VMX_BF_EXIT_INT_INFO_VALID,            1);
        iemVmxVmcsSetExitIntInfo(pVCpu, uExitIntInfo);
        iemVmxVmcsSetExitIntErrCode(pVCpu, uErrCode);

        /*
         * For VM-exits due to software exceptions (those generated by INT3 or INTO) or privileged
         * software exceptions (those generated by INT1/ICEBP) we need to supply the VM-exit instruction
         * length.
         */
        if (   (fFlags & IEM_XCPT_FLAGS_T_SOFT_INT)
            || (fFlags & (IEM_XCPT_FLAGS_BP_INSTR | IEM_XCPT_FLAGS_OF_INSTR | IEM_XCPT_FLAGS_ICEBP_INSTR)))
            iemVmxVmcsSetExitInstrLen(pVCpu, cbInstr);
        else
            iemVmxVmcsSetExitInstrLen(pVCpu, 0);

        return iemVmxVmexit(pVCpu, VMX_EXIT_XCPT_OR_NMI, u64ExitQual);
    }

    return VINF_VMX_INTERCEPT_NOT_ACTIVE;
}


/**
 * VMX VM-exit handler for EPT misconfiguration.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   GCPhysAddr      The physical address causing the EPT misconfiguration.
 *                          This need not be page aligned (e.g. nested-guest in real
 *                          mode).
 */
static VBOXSTRICTRC iemVmxVmexitEptMisconfig(PVMCPUCC pVCpu, RTGCPHYS GCPhysAddr) RT_NOEXCEPT
{
    iemVmxVmcsSetExitGuestPhysAddr(pVCpu, GCPhysAddr);
    return iemVmxVmexit(pVCpu, VMX_EXIT_EPT_MISCONFIG, 0 /* u64ExitQual */);
}


/**
 * VMX VM-exit handler for EPT misconfiguration.
 *
 * This is intended for EPT misconfigurations where the caller provides all the
 * relevant VM-exit information.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   GCPhysAddr      The physical address causing the EPT misconfiguration.
 *                          This need not be page aligned (e.g. nested-guest in real
 *                          mode).
 * @param   pExitEventInfo  Pointer to the VM-exit event information.
 */
static VBOXSTRICTRC iemVmxVmexitEptMisconfigWithInfo(PVMCPUCC pVCpu, RTGCPHYS GCPhysAddr, PCVMXVEXITEVENTINFO pExitEventInfo) RT_NOEXCEPT
{
    Assert(pExitEventInfo);
    Assert(!VMX_EXIT_INT_INFO_IS_VALID(pExitEventInfo->uExitIntInfo));
    iemVmxVmcsSetIdtVectoringInfo(pVCpu, pExitEventInfo->uIdtVectoringInfo);
    iemVmxVmcsSetIdtVectoringErrCode(pVCpu, pExitEventInfo->uIdtVectoringErrCode);
    iemVmxVmcsSetExitGuestPhysAddr(pVCpu, GCPhysAddr);
    return iemVmxVmexit(pVCpu, VMX_EXIT_EPT_MISCONFIG, 0 /* u64ExitQual */);
}


/**
 * Interface for HM and EM to emulate a VM-exit due to an EPT misconfiguration.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   GCPhysAddr      The nested-guest physical address causing the EPT
 *                          misconfiguration.
 * @param   pExitEventInfo  Pointer to the VM-exit event information.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecVmxVmexitEptMisconfig(PVMCPUCC pVCpu, RTGCPHYS GCPhysAddr, PCVMXVEXITEVENTINFO pExitEventInfo)
{
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK | CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = iemVmxVmexitEptMisconfigWithInfo(pVCpu, GCPhysAddr, pExitEventInfo);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * VMX VM-exit handler for EPT violation.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   fAccess             The access causing the EPT violation, IEM_ACCESS_XXX.
 * @param   fSlatFail           The SLAT failure info, IEM_SLAT_FAIL_XXX.
 * @param   fEptAccess          The EPT paging structure bits.
 * @param   GCPhysAddr          The physical address causing the EPT violation. This
 *                              need not be page aligned (e.g. nested-guest in real
 *                              mode).
 * @param   fIsLinearAddrValid  Whether translation of a linear address caused this
 *                              EPT violation. If @c false, GCPtrAddr must be 0.
 * @param   GCPtrAddr           The linear address causing the EPT violation.
 * @param   cbInstr             The VM-exit instruction length.
 */
static VBOXSTRICTRC iemVmxVmexitEptViolation(PVMCPUCC pVCpu, uint32_t fAccess, uint32_t fSlatFail,
                                             uint64_t fEptAccess, RTGCPHYS GCPhysAddr, bool fIsLinearAddrValid,
                                             uint64_t GCPtrAddr, uint8_t cbInstr) RT_NOEXCEPT
{
    /*
     * If the linear address isn't valid (can happen when loading PDPTEs
     * as part of MOV CR execution) the linear address field is undefined.
     * While we can leave it this way, it's preferable to zero it for consistency.
     */
    Assert(fIsLinearAddrValid || GCPtrAddr == 0);

    uint64_t const fCaps = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64EptVpidCaps;
    bool const fSupportsAccessDirty = RT_BOOL(fCaps & MSR_IA32_VMX_EPT_VPID_CAP_ACCESS_DIRTY);

    uint32_t const fDataRdMask     = IEM_ACCESS_WHAT_MASK | IEM_ACCESS_TYPE_READ;
    uint32_t const fDataWrMask     = IEM_ACCESS_WHAT_MASK | IEM_ACCESS_TYPE_WRITE;
    uint32_t const fInstrMask      = IEM_ACCESS_WHAT_MASK | IEM_ACCESS_TYPE_EXEC;
    bool const fDataRead           = ((fAccess & fDataRdMask) == IEM_ACCESS_DATA_R) | fSupportsAccessDirty;
    bool const fDataWrite          = ((fAccess & fDataWrMask) == IEM_ACCESS_DATA_W) | fSupportsAccessDirty;
    bool const fInstrFetch         = ((fAccess & fInstrMask)  == IEM_ACCESS_INSTRUCTION);
    bool const fEptRead            = RT_BOOL(fEptAccess & EPT_E_READ);
    bool const fEptWrite           = RT_BOOL(fEptAccess & EPT_E_WRITE);
    bool const fEptExec            = RT_BOOL(fEptAccess & EPT_E_EXECUTE);
    bool const fNmiUnblocking      = pVCpu->cpum.GstCtx.hwvirt.vmx.fNmiUnblockingIret;
    bool const fIsLinearToPhysAddr = fIsLinearAddrValid & RT_BOOL(fSlatFail & IEM_SLAT_FAIL_LINEAR_TO_PHYS_ADDR);

    uint64_t const u64ExitQual = RT_BF_MAKE(VMX_BF_EXIT_QUAL_EPT_ACCESS_READ,         fDataRead)
                               | RT_BF_MAKE(VMX_BF_EXIT_QUAL_EPT_ACCESS_WRITE,        fDataWrite)
                               | RT_BF_MAKE(VMX_BF_EXIT_QUAL_EPT_ACCESS_INSTR_FETCH,  fInstrFetch)
                               | RT_BF_MAKE(VMX_BF_EXIT_QUAL_EPT_ENTRY_READ,          fEptRead)
                               | RT_BF_MAKE(VMX_BF_EXIT_QUAL_EPT_ENTRY_WRITE,         fEptWrite)
                               | RT_BF_MAKE(VMX_BF_EXIT_QUAL_EPT_ENTRY_EXECUTE,       fEptExec)
                               | RT_BF_MAKE(VMX_BF_EXIT_QUAL_EPT_LINEAR_ADDR_VALID,   fIsLinearAddrValid)
                               | RT_BF_MAKE(VMX_BF_EXIT_QUAL_EPT_LINEAR_TO_PHYS_ADDR, fIsLinearToPhysAddr)
                               | RT_BF_MAKE(VMX_BF_EXIT_QUAL_EPT_NMI_UNBLOCK_IRET,    fNmiUnblocking);

#ifdef VBOX_STRICT
    uint64_t const fMiscCaps  = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64Misc;
    uint32_t const fProcCtls2 = pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ProcCtls2;
    Assert(!(fCaps & MSR_IA32_VMX_EPT_VPID_CAP_ADVEXITINFO_EPT_VIOLATION));   /* Advanced VM-exit info. not supported */
    Assert(!(fCaps & MSR_IA32_VMX_EPT_VPID_CAP_SUPER_SHW_STACK));             /* Supervisor shadow stack control not supported. */
    Assert(!(RT_BF_GET(fMiscCaps, VMX_BF_MISC_INTEL_PT)));                    /* Intel PT not supported. */
    Assert(!(fProcCtls2 & VMX_PROC_CTLS2_MODE_BASED_EPT_PERM));               /* Mode-based execute control not supported. */
#endif

    iemVmxVmcsSetExitGuestPhysAddr(pVCpu, GCPhysAddr);
    iemVmxVmcsSetExitGuestLinearAddr(pVCpu, GCPtrAddr);
    iemVmxVmcsSetExitInstrLen(pVCpu, cbInstr);

    return iemVmxVmexit(pVCpu, VMX_EXIT_EPT_VIOLATION, u64ExitQual);
}


/**
 * VMX VM-exit handler for EPT violation.
 *
 * This is intended for EPT violations where the caller provides all the
 * relevant VM-exit information.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pExitInfo       Pointer to the VM-exit information.
 * @param   pExitEventInfo  Pointer to the VM-exit event information.
 */
static VBOXSTRICTRC iemVmxVmexitEptViolationWithInfo(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo,
                                                     PCVMXVEXITEVENTINFO pExitEventInfo) RT_NOEXCEPT
{
    Assert(pExitInfo);
    Assert(pExitEventInfo);
    Assert(pExitInfo->uReason == VMX_EXIT_EPT_VIOLATION);
    Assert(!VMX_EXIT_INT_INFO_IS_VALID(pExitEventInfo->uExitIntInfo));

    iemVmxVmcsSetIdtVectoringInfo(pVCpu, pExitEventInfo->uIdtVectoringInfo);
    iemVmxVmcsSetIdtVectoringErrCode(pVCpu, pExitEventInfo->uIdtVectoringErrCode);

    iemVmxVmcsSetExitGuestPhysAddr(pVCpu, pExitInfo->u64GuestPhysAddr);
    if (pExitInfo->u64Qual & VMX_BF_EXIT_QUAL_EPT_LINEAR_ADDR_VALID_MASK)
        iemVmxVmcsSetExitGuestLinearAddr(pVCpu, pExitInfo->u64GuestLinearAddr);
    else
        iemVmxVmcsSetExitGuestLinearAddr(pVCpu, 0);
    iemVmxVmcsSetExitInstrLen(pVCpu, pExitInfo->cbInstr);
    return iemVmxVmexit(pVCpu, VMX_EXIT_EPT_VIOLATION, pExitInfo->u64Qual);
}


/**
 * Interface for HM and EM to emulate a VM-exit due to an EPT violation.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pExitInfo       Pointer to the VM-exit information.
 * @param   pExitEventInfo  Pointer to the VM-exit event information.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecVmxVmexitEptViolation(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo,
                                                        PCVMXVEXITEVENTINFO pExitEventInfo)
{
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK | CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = iemVmxVmexitEptViolationWithInfo(pVCpu, pExitInfo, pExitEventInfo);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * VMX VM-exit handler for EPT-induced VM-exits.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pWalk       The page walk info.
 * @param   fAccess     The access causing the EPT event, IEM_ACCESS_XXX.
 * @param   fSlatFail   Additional SLAT info, IEM_SLAT_FAIL_XXX.
 * @param   cbInstr     The VM-exit instruction length if applicable. Pass 0 if not
 *                      applicable.
 */
VBOXSTRICTRC iemVmxVmexitEpt(PVMCPUCC pVCpu, PPGMPTWALKFAST pWalk, uint32_t fAccess, uint32_t fSlatFail, uint8_t cbInstr) RT_NOEXCEPT
{
    Assert(pWalk->fInfo & PGM_WALKINFO_IS_SLAT);
    Assert(pWalk->fFailed & PGM_WALKFAIL_EPT);
    Assert(!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fVmxEptXcptVe);          /* #VE exceptions not supported. */
    Assert(!(pWalk->fFailed & PGM_WALKFAIL_EPT_VIOLATION_CONVERTIBLE)); /* Without #VE, convertible violations not possible. */

    if (pWalk->fFailed & PGM_WALKFAIL_EPT_VIOLATION)
    {
        LogFlow(("EptViolation: cs:rip=%04x:%08RX64 fAccess=%#RX32\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, fAccess));
        uint64_t const fEptAccess = (pWalk->fEffective & PGM_PTATTRS_EPT_MASK) >> PGM_PTATTRS_EPT_SHIFT;
        return iemVmxVmexitEptViolation(pVCpu, fAccess, fSlatFail, fEptAccess, pWalk->GCPhysNested,
                                        RT_BOOL(pWalk->fInfo & PGM_WALKINFO_IS_LINEAR_ADDR_VALID),
                                        pWalk->GCPtr, cbInstr);
    }

    LogFlow(("EptMisconfig: cs:rip=%04x:%08RX64 fAccess=%#RX32\n", pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, fAccess));
    Assert(pWalk->fFailed & PGM_WALKFAIL_EPT_MISCONFIG);
    return iemVmxVmexitEptMisconfig(pVCpu, pWalk->GCPhysNested);
}


/**
 * VMX VM-exit handler for APIC accesses.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   offAccess   The offset of the register being accessed.
 * @param   fAccess     The type of access, see IEM_ACCESS_XXX.
 */
static VBOXSTRICTRC iemVmxVmexitApicAccess(PVMCPUCC pVCpu, uint16_t offAccess, uint32_t fAccess) RT_NOEXCEPT
{
    VMXAPICACCESS enmAccess;
    bool const fInEventDelivery = IEMGetCurrentXcpt(pVCpu, NULL, NULL, NULL, NULL);
    if (fInEventDelivery)
        enmAccess = VMXAPICACCESS_LINEAR_EVENT_DELIVERY;
    else if ((fAccess & (IEM_ACCESS_WHAT_MASK | IEM_ACCESS_TYPE_MASK)) == IEM_ACCESS_INSTRUCTION)
        enmAccess = VMXAPICACCESS_LINEAR_INSTR_FETCH;
    else if (fAccess & IEM_ACCESS_TYPE_WRITE)
        enmAccess = VMXAPICACCESS_LINEAR_WRITE;
    else
        enmAccess = VMXAPICACCESS_LINEAR_READ;

    uint64_t const u64ExitQual = RT_BF_MAKE(VMX_BF_EXIT_QUAL_APIC_ACCESS_OFFSET, offAccess)
                               | RT_BF_MAKE(VMX_BF_EXIT_QUAL_APIC_ACCESS_TYPE,   enmAccess);
    return iemVmxVmexit(pVCpu, VMX_EXIT_APIC_ACCESS, u64ExitQual);
}


/**
 * VMX VM-exit handler for APIC accesses.
 *
 * This is intended for APIC accesses where the caller provides all the
 * relevant VM-exit information.
 *
 * @returns VBox strict status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pExitInfo       Pointer to the VM-exit information.
 * @param   pExitEventInfo  Pointer to the VM-exit event information.
 */
static VBOXSTRICTRC iemVmxVmexitApicAccessWithInfo(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo,
                                                   PCVMXVEXITEVENTINFO pExitEventInfo) RT_NOEXCEPT
{
    /* VM-exit interruption information should not be valid for APIC-access VM-exits. */
    Assert(!VMX_EXIT_INT_INFO_IS_VALID(pExitEventInfo->uExitIntInfo));
    Assert(pExitInfo->uReason == VMX_EXIT_APIC_ACCESS);
    iemVmxVmcsSetExitIntInfo(pVCpu, 0);
    iemVmxVmcsSetExitIntErrCode(pVCpu, 0);
    iemVmxVmcsSetExitInstrLen(pVCpu, pExitInfo->cbInstr);
    iemVmxVmcsSetIdtVectoringInfo(pVCpu, pExitEventInfo->uIdtVectoringInfo);
    iemVmxVmcsSetIdtVectoringErrCode(pVCpu, pExitEventInfo->uIdtVectoringErrCode);
    return iemVmxVmexit(pVCpu, VMX_EXIT_APIC_ACCESS, pExitInfo->u64Qual);
}


/**
 * Interface for HM and EM to virtualize memory-mapped APIC accesses.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_VMX_MODIFIES_BEHAVIOR if the memory access was virtualized.
 * @retval  VINF_VMX_VMEXIT if the access causes a VM-exit.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pExitInfo       Pointer to the VM-exit information.
 * @param   pExitEventInfo  Pointer to the VM-exit event information.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecVmxVmexitApicAccess(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo, PCVMXVEXITEVENTINFO pExitEventInfo)
{
    Assert(pExitInfo);
    Assert(pExitEventInfo);
    VBOXSTRICTRC rcStrict = iemVmxVmexitApicAccessWithInfo(pVCpu, pExitInfo, pExitEventInfo);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemExecStatusCodeFiddling(pVCpu, rcStrict);
}


/**
 * VMX VM-exit handler for APIC-write VM-exits.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   offApic     The write to the virtual-APIC page offset that caused this
 *                      VM-exit.
 */
static VBOXSTRICTRC iemVmxVmexitApicWrite(PVMCPUCC pVCpu, uint16_t offApic) RT_NOEXCEPT
{
    Assert(offApic < XAPIC_OFF_END + 4);
    /* Write only bits 11:0 of the APIC offset into the Exit qualification field. */
    offApic &= UINT16_C(0xfff);
    return iemVmxVmexit(pVCpu, VMX_EXIT_APIC_WRITE, offApic);
}


/**
 * Clears any pending virtual-APIC write emulation.
 *
 * @returns The virtual-APIC offset that was written before clearing it.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
DECLINLINE(uint16_t) iemVmxVirtApicClearPendingWrite(PVMCPUCC pVCpu)
{
    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_HWVIRT);
    uint16_t const offVirtApicWrite = pVCpu->cpum.GstCtx.hwvirt.vmx.offVirtApicWrite;
    pVCpu->cpum.GstCtx.hwvirt.vmx.offVirtApicWrite = 0;
    Assert(VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_APIC_WRITE));
    VMCPU_FF_CLEAR(pVCpu, VMCPU_FF_VMX_APIC_WRITE);
    return offVirtApicWrite;
}


/**
 * Reads a 32-bit register from the virtual-APIC page at the given offset.
 *
 * @returns The register from the virtual-APIC page.
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   offReg  The offset of the register being read.
 */
uint32_t iemVmxVirtApicReadRaw32(PVMCPUCC pVCpu, uint16_t offReg) RT_NOEXCEPT
{
    Assert(offReg <= VMX_V_VIRT_APIC_SIZE - sizeof(uint32_t));

    uint32_t       uReg           = 0;
    RTGCPHYS const GCPhysVirtApic = pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64AddrVirtApic.u;
    int rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &uReg, GCPhysVirtApic + offReg, sizeof(uReg));
    AssertMsgStmt(RT_SUCCESS(rc),
                  ("Failed to read %u bytes at offset %#x of the virtual-APIC page at %#RGp: %Rrc\n",
                   sizeof(uReg), offReg, GCPhysVirtApic, rc),
                  uReg = 0);
    return uReg;
}


/**
 * Reads a 64-bit register from the virtual-APIC page at the given offset.
 *
 * @returns The register from the virtual-APIC page.
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   offReg  The offset of the register being read.
 */
static uint64_t iemVmxVirtApicReadRaw64(PVMCPUCC pVCpu, uint16_t offReg) RT_NOEXCEPT
{
    Assert(offReg <= VMX_V_VIRT_APIC_SIZE - sizeof(uint64_t));

    uint64_t       uReg           = 0;
    RTGCPHYS const GCPhysVirtApic = pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64AddrVirtApic.u;
    int rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &uReg, GCPhysVirtApic + offReg, sizeof(uReg));
    AssertMsgStmt(RT_SUCCESS(rc),
                  ("Failed to read %u bytes at offset %#x of the virtual-APIC page at %#RGp: %Rrc\n",
                   sizeof(uReg), offReg, GCPhysVirtApic, rc),
                  uReg = 0);
    return uReg;
}


/**
 * Writes a 32-bit register to the virtual-APIC page at the given offset.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   offReg  The offset of the register being written.
 * @param   uReg    The register value to write.
 */
void iemVmxVirtApicWriteRaw32(PVMCPUCC pVCpu, uint16_t offReg, uint32_t uReg) RT_NOEXCEPT
{
    Assert(offReg <= VMX_V_VIRT_APIC_SIZE - sizeof(uint32_t));

    RTGCPHYS const GCPhysVirtApic = pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64AddrVirtApic.u;
    int rc = PGMPhysSimpleWriteGCPhys(pVCpu->CTX_SUFF(pVM), GCPhysVirtApic + offReg, &uReg, sizeof(uReg));
    AssertMsgRC(rc, ("Failed to write %u bytes at offset %#x of the virtual-APIC page at %#RGp: %Rrc\n",
                     sizeof(uReg), offReg, GCPhysVirtApic, rc));
}


/**
 * Writes a 64-bit register to the virtual-APIC page at the given offset.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   offReg  The offset of the register being written.
 * @param   uReg    The register value to write.
 */
static void iemVmxVirtApicWriteRaw64(PVMCPUCC pVCpu, uint16_t offReg, uint64_t uReg) RT_NOEXCEPT
{
    Assert(offReg <= VMX_V_VIRT_APIC_SIZE - sizeof(uint64_t));

    RTGCPHYS const GCPhysVirtApic = pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64AddrVirtApic.u;
    int rc = PGMPhysSimpleWriteGCPhys(pVCpu->CTX_SUFF(pVM), GCPhysVirtApic + offReg, &uReg, sizeof(uReg));
    AssertMsgRC(rc, ("Failed to write %u bytes at offset %#x of the virtual-APIC page at %#RGp: %Rrc\n",
                     sizeof(uReg), offReg, GCPhysVirtApic, rc));
}


/**
 * Sets the vector in a virtual-APIC 256-bit sparse register.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   offReg      The offset of the 256-bit spare register.
 * @param   uVector     The vector to set.
 *
 * @remarks This is based on our APIC device code.
 */
static void iemVmxVirtApicSetVectorInReg(PVMCPUCC pVCpu, uint16_t offReg, uint8_t uVector) RT_NOEXCEPT
{
    /* Determine the vector offset within the chunk. */
    uint16_t const offVector = (uVector & UINT32_C(0xe0)) >> 1;

    /* Read the chunk at the offset. */
    uint32_t uReg;
    RTGCPHYS const GCPhysVirtApic = pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64AddrVirtApic.u;
    int rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &uReg, GCPhysVirtApic + offReg + offVector, sizeof(uReg));
    if (RT_SUCCESS(rc))
    {
        /* Modify the chunk. */
        uint16_t const idxVectorBit = uVector & UINT32_C(0x1f);
        uReg |= RT_BIT(idxVectorBit);

        /* Write the chunk. */
        rc = PGMPhysSimpleWriteGCPhys(pVCpu->CTX_SUFF(pVM), GCPhysVirtApic + offReg + offVector, &uReg, sizeof(uReg));
        AssertMsgRC(rc, ("Failed to set vector %#x in 256-bit register at %#x of the virtual-APIC page at %#RGp: %Rrc\n",
                         uVector, offReg, GCPhysVirtApic, rc));
    }
    else
        AssertMsgFailed(("Failed to get vector %#x in 256-bit register at %#x of the virtual-APIC page at %#RGp: %Rrc\n",
                         uVector, offReg, GCPhysVirtApic, rc));
}


/**
 * Clears the vector in a virtual-APIC 256-bit sparse register.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   offReg      The offset of the 256-bit spare register.
 * @param   uVector     The vector to clear.
 *
 * @remarks This is based on our APIC device code.
 */
static void iemVmxVirtApicClearVectorInReg(PVMCPUCC pVCpu, uint16_t offReg, uint8_t uVector) RT_NOEXCEPT
{
    /* Determine the vector offset within the chunk. */
    uint16_t const offVector      = (uVector & UINT32_C(0xe0)) >> 1;

    /* Read the chunk at the offset. */
    uint32_t uReg;
    RTGCPHYS const GCPhysVirtApic = pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64AddrVirtApic.u;
    int rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &uReg, GCPhysVirtApic + offReg + offVector, sizeof(uReg));
    if (RT_SUCCESS(rc))
    {
        /* Modify the chunk. */
        uint16_t const idxVectorBit = uVector & UINT32_C(0x1f);
        uReg &= ~RT_BIT(idxVectorBit);

        /* Write the chunk. */
        rc = PGMPhysSimpleWriteGCPhys(pVCpu->CTX_SUFF(pVM), GCPhysVirtApic + offReg + offVector, &uReg, sizeof(uReg));
        AssertMsgRC(rc, ("Failed to clear vector %#x in 256-bit register at %#x of the virtual-APIC page at %#RGp: %Rrc\n",
                         uVector, offReg, GCPhysVirtApic, rc));
    }
    else
        AssertMsgFailed(("Failed to get vector %#x in 256-bit register at %#x of the virtual-APIC page at %#RGp: %Rrc\n",
                         uVector, offReg, GCPhysVirtApic, rc));
}


/**
 * Checks if a memory access to the APIC-access page must causes an APIC-access
 * VM-exit.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   offAccess   The offset of the register being accessed.
 * @param   cbAccess    The size of the access in bytes.
 * @param   fAccess     The type of access, see IEM_ACCESS_XXX.
 *
 * @remarks This must not be used for MSR-based APIC-access page accesses!
 * @sa      iemVmxVirtApicAccessMsrWrite, iemVmxVirtApicAccessMsrRead.
 */
static bool iemVmxVirtApicIsMemAccessIntercepted(PVMCPUCC pVCpu, uint16_t offAccess, size_t cbAccess, uint32_t fAccess) RT_NOEXCEPT
{
    Assert(cbAccess > 0);
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;

    /*
     * We must cause a VM-exit if any of the following are true:
     *   - TPR shadowing isn't active.
     *   - The access size exceeds 32-bits.
     *   - The access is not contained within low 4 bytes of a 16-byte aligned offset.
     *
     * See Intel spec. 29.4.2 "Virtualizing Reads from the APIC-Access Page".
     * See Intel spec. 29.4.3.1 "Determining Whether a Write Access is Virtualized".
     */
    if (   !(pVmcs->u32ProcCtls & VMX_PROC_CTLS_USE_TPR_SHADOW)
        || cbAccess > sizeof(uint32_t)
        || ((offAccess + cbAccess - 1) & 0xc)
        || offAccess >= XAPIC_OFF_END + 4)
        return true;

    /*
     * If the access is part of an operation where we have already
     * virtualized a virtual-APIC write, we must cause a VM-exit.
     */
    if (VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_APIC_WRITE))
        return true;

    /*
     * Check write accesses to the APIC-access page that cause VM-exits.
     */
    if (fAccess & IEM_ACCESS_TYPE_WRITE)
    {
        if (pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_APIC_REG_VIRT)
        {
            /*
             * With APIC-register virtualization, a write access to any of the
             * following registers are virtualized. Accessing any other register
             * causes a VM-exit.
             */
            uint16_t const offAlignedAccess = offAccess & 0xfffc;
            switch (offAlignedAccess)
            {
                case XAPIC_OFF_ID:
                case XAPIC_OFF_TPR:
                case XAPIC_OFF_EOI:
                case XAPIC_OFF_LDR:
                case XAPIC_OFF_DFR:
                case XAPIC_OFF_SVR:
                case XAPIC_OFF_ESR:
                case XAPIC_OFF_ICR_LO:
                case XAPIC_OFF_ICR_HI:
                case XAPIC_OFF_LVT_TIMER:
                case XAPIC_OFF_LVT_THERMAL:
                case XAPIC_OFF_LVT_PERF:
                case XAPIC_OFF_LVT_LINT0:
                case XAPIC_OFF_LVT_LINT1:
                case XAPIC_OFF_LVT_ERROR:
                case XAPIC_OFF_TIMER_ICR:
                case XAPIC_OFF_TIMER_DCR:
                    break;
                default:
                    return true;
            }
        }
        else if (pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_INT_DELIVERY)
        {
            /*
             * With virtual-interrupt delivery, a write access to any of the
             * following registers are virtualized. Accessing any other register
             * causes a VM-exit.
             *
             * Note! The specification does not allow writing to offsets in-between
             * these registers (e.g. TPR + 1 byte) unlike read accesses.
             */
            switch (offAccess)
            {
                case XAPIC_OFF_TPR:
                case XAPIC_OFF_EOI:
                case XAPIC_OFF_ICR_LO:
                    break;
                default:
                    return true;
            }
        }
        else
        {
            /*
             * Without APIC-register virtualization or virtual-interrupt delivery,
             * only TPR accesses are virtualized.
             */
            if (offAccess == XAPIC_OFF_TPR)
            { /* likely */ }
            else
                return true;
        }
    }
    else
    {
        /*
         * Check read accesses to the APIC-access page that cause VM-exits.
         */
        if (pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_APIC_REG_VIRT)
        {
            /*
             * With APIC-register virtualization, a read access to any of the
             * following registers are virtualized. Accessing any other register
             * causes a VM-exit.
             */
            uint16_t const offAlignedAccess = offAccess & 0xfffc;
            switch (offAlignedAccess)
            {
                /** @todo r=ramshankar: What about XAPIC_OFF_LVT_CMCI? */
                case XAPIC_OFF_ID:
                case XAPIC_OFF_VERSION:
                case XAPIC_OFF_TPR:
                case XAPIC_OFF_EOI:
                case XAPIC_OFF_LDR:
                case XAPIC_OFF_DFR:
                case XAPIC_OFF_SVR:
                case XAPIC_OFF_ISR0:    case XAPIC_OFF_ISR1:    case XAPIC_OFF_ISR2:    case XAPIC_OFF_ISR3:
                case XAPIC_OFF_ISR4:    case XAPIC_OFF_ISR5:    case XAPIC_OFF_ISR6:    case XAPIC_OFF_ISR7:
                case XAPIC_OFF_TMR0:    case XAPIC_OFF_TMR1:    case XAPIC_OFF_TMR2:    case XAPIC_OFF_TMR3:
                case XAPIC_OFF_TMR4:    case XAPIC_OFF_TMR5:    case XAPIC_OFF_TMR6:    case XAPIC_OFF_TMR7:
                case XAPIC_OFF_IRR0:    case XAPIC_OFF_IRR1:    case XAPIC_OFF_IRR2:    case XAPIC_OFF_IRR3:
                case XAPIC_OFF_IRR4:    case XAPIC_OFF_IRR5:    case XAPIC_OFF_IRR6:    case XAPIC_OFF_IRR7:
                case XAPIC_OFF_ESR:
                case XAPIC_OFF_ICR_LO:
                case XAPIC_OFF_ICR_HI:
                case XAPIC_OFF_LVT_TIMER:
                case XAPIC_OFF_LVT_THERMAL:
                case XAPIC_OFF_LVT_PERF:
                case XAPIC_OFF_LVT_LINT0:
                case XAPIC_OFF_LVT_LINT1:
                case XAPIC_OFF_LVT_ERROR:
                case XAPIC_OFF_TIMER_ICR:
                case XAPIC_OFF_TIMER_DCR:
                    break;
                default:
                    return true;
            }
        }
        else
        {
            /* Without APIC-register virtualization, only TPR accesses are virtualized. */
            if (offAccess == XAPIC_OFF_TPR)
            { /* likely */ }
            else
                return true;
        }
    }

    /* The APIC access is virtualized, does not cause a VM-exit. */
    return false;
}


/**
 * Virtualizes a memory-based APIC access by certain instructions even though they
 * do not use the address to access memory.
 *
 * This is for instructions like MONITOR, CLFLUSH, CLFLUSHOPT, ENTER which may cause
 * page-faults but do not use the address to access memory.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pGCPhysAccess   Pointer to the guest-physical address accessed.
 * @param   cbAccess        The size of the access in bytes.
 * @param   fAccess         The type of access, see IEM_ACCESS_XXX.
 */
VBOXSTRICTRC iemVmxVirtApicAccessUnused(PVMCPUCC pVCpu, PRTGCPHYS pGCPhysAccess, size_t cbAccess, uint32_t fAccess) RT_NOEXCEPT
{
    Assert(pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_APIC_ACCESS);
    Assert(pGCPhysAccess);

    RTGCPHYS const GCPhysAccess = *pGCPhysAccess & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;
    RTGCPHYS const GCPhysApic   = pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u64AddrApicAccess.u;
    Assert(!(GCPhysApic & GUEST_PAGE_OFFSET_MASK));

    if (GCPhysAccess == GCPhysApic)
    {
        uint16_t const offAccess = *pGCPhysAccess & GUEST_PAGE_OFFSET_MASK;
        bool const fIntercept = iemVmxVirtApicIsMemAccessIntercepted(pVCpu, offAccess, cbAccess, fAccess);
        if (fIntercept)
            return iemVmxVmexitApicAccess(pVCpu, offAccess, fAccess);

        *pGCPhysAccess = GCPhysApic | offAccess;
        return VINF_VMX_MODIFIES_BEHAVIOR;
    }

    return VINF_VMX_INTERCEPT_NOT_ACTIVE;
}


/**
 * Virtualizes a memory-based APIC access.
 *
 * @returns VBox strict status code.
 * @retval VINF_VMX_MODIFIES_BEHAVIOR if the access was virtualized.
 * @retval VINF_VMX_VMEXIT if the access causes a VM-exit.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   offAccess   The offset of the register being accessed (within the
 *                      APIC-access page).
 * @param   cbAccess    The size of the access in bytes.
 * @param   pvData      Pointer to the data being written or where to store the data
 *                      being read.
 * @param   fAccess     The type of access, see IEM_ACCESS_XXX.
 */
static VBOXSTRICTRC iemVmxVirtApicAccessMem(PVMCPUCC pVCpu, uint16_t offAccess, size_t cbAccess,
                                            void *pvData, uint32_t fAccess) RT_NOEXCEPT
{
    Assert(pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_APIC_ACCESS);
    Assert(pvData);

    bool const fIntercept = iemVmxVirtApicIsMemAccessIntercepted(pVCpu, offAccess, cbAccess, fAccess);
    if (fIntercept)
        return iemVmxVmexitApicAccess(pVCpu, offAccess, fAccess);

    if (fAccess & IEM_ACCESS_TYPE_WRITE)
    {
        /*
         * A write access to the APIC-access page that is virtualized (rather than
         * causing a VM-exit) writes data to the virtual-APIC page.
         */
        uint32_t const u32Data = *(uint32_t *)pvData;
        iemVmxVirtApicWriteRaw32(pVCpu, offAccess, u32Data);

        /*
         * Record the currently updated APIC offset, as we need this later for figuring
         * out whether to perform TPR, EOI or self-IPI virtualization as well as well
         * as for supplying the exit qualification when causing an APIC-write VM-exit.
         *
         * After completion of the current operation, we need to perform TPR virtualization,
         * EOI virtualization or APIC-write VM-exit depending on which register was written.
         *
         * The current operation may be a REP-prefixed string instruction, execution of any
         * other instruction, or delivery of an event through the IDT.
         *
         * Thus things like clearing bytes 3:1 of the VTPR, clearing VEOI are not to be
         * performed now but later after completion of the current operation.
         *
         * See Intel spec. 29.4.3.2 "APIC-Write Emulation".
         */
        iemVmxVirtApicSetPendingWrite(pVCpu, offAccess);

        LogFlowFunc(("Write access at offset %#x not intercepted -> Wrote %#RX32\n", offAccess, u32Data));
    }
    else
    {
        /*
         * A read access from the APIC-access page that is virtualized (rather than
         * causing a VM-exit) returns data from the virtual-APIC page.
         *
         * See Intel spec. 29.4.2 "Virtualizing Reads from the APIC-Access Page".
         */
        Assert(fAccess & IEM_ACCESS_TYPE_READ);

        Assert(cbAccess <= 4);
        Assert(offAccess < XAPIC_OFF_END + 4);
        static uint32_t const s_auAccessSizeMasks[] = { 0, 0xff, 0xffff, 0xffffff, 0xffffffff };

        uint32_t u32Data = iemVmxVirtApicReadRaw32(pVCpu, offAccess);
        u32Data &= s_auAccessSizeMasks[cbAccess];
        *(uint32_t *)pvData = u32Data;

        LogFlowFunc(("Read access at offset %#x not intercepted -> Read %#RX32\n", offAccess, u32Data));
    }

    return VINF_VMX_MODIFIES_BEHAVIOR;
}


/**
 * Virtualizes an MSR-based APIC read access.
 *
 * @returns VBox strict status code.
 * @retval  VINF_VMX_MODIFIES_BEHAVIOR if the MSR read was virtualized.
 * @retval  VINF_VMX_INTERCEPT_NOT_ACTIVE if the MSR read access must be
 *          handled by the x2APIC device.
 * @retval  VERR_OUT_RANGE if the MSR read was supposed to be virtualized but was
 *          not within the range of valid MSRs, caller must raise \#GP(0).
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idMsr       The x2APIC MSR being read.
 * @param   pu64Value   Where to store the read x2APIC MSR value (only valid when
 *                      VINF_VMX_MODIFIES_BEHAVIOR is returned).
 */
static VBOXSTRICTRC iemVmxVirtApicAccessMsrRead(PVMCPUCC pVCpu, uint32_t idMsr, uint64_t *pu64Value) RT_NOEXCEPT
{
    Assert(pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_X2APIC_MODE);
    Assert(pu64Value);

    if (pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ProcCtls2 & VMX_PROC_CTLS2_APIC_REG_VIRT)
    {
        if (   idMsr >= MSR_IA32_X2APIC_START
            && idMsr <= MSR_IA32_X2APIC_END)
        {
            uint16_t const offReg   = (idMsr & 0xff) << 4;
            uint64_t const u64Value = iemVmxVirtApicReadRaw64(pVCpu, offReg);
            *pu64Value = u64Value;
            return VINF_VMX_MODIFIES_BEHAVIOR;
        }
        return VERR_OUT_OF_RANGE;
    }

    if (idMsr == MSR_IA32_X2APIC_TPR)
    {
        uint16_t const offReg   = (idMsr & 0xff) << 4;
        uint64_t const u64Value = iemVmxVirtApicReadRaw64(pVCpu, offReg);
        *pu64Value = u64Value;
        return VINF_VMX_MODIFIES_BEHAVIOR;
    }

    return VINF_VMX_INTERCEPT_NOT_ACTIVE;
}


/**
 * Virtualizes an MSR-based APIC write access.
 *
 * @returns VBox strict status code.
 * @retval  VINF_VMX_MODIFIES_BEHAVIOR if the MSR write was virtualized.
 * @retval  VERR_OUT_RANGE if the MSR read was supposed to be virtualized but was
 *          not within the range of valid MSRs, caller must raise \#GP(0).
 * @retval  VINF_VMX_INTERCEPT_NOT_ACTIVE if the MSR must be written normally.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   idMsr       The x2APIC MSR being written.
 * @param   u64Value    The value of the x2APIC MSR being written.
 */
static VBOXSTRICTRC iemVmxVirtApicAccessMsrWrite(PVMCPUCC pVCpu, uint32_t idMsr, uint64_t u64Value) RT_NOEXCEPT
{
    /*
     * Check if the access is to be virtualized.
     * See Intel spec. 29.5 "Virtualizing MSR-based APIC Accesses".
     */
    if (   idMsr == MSR_IA32_X2APIC_TPR
        || (   (pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_INT_DELIVERY)
            && (   idMsr == MSR_IA32_X2APIC_EOI
                || idMsr == MSR_IA32_X2APIC_SELF_IPI)))
    {
        /* Validate the MSR write depending on the register. */
        switch (idMsr)
        {
            case MSR_IA32_X2APIC_TPR:
            case MSR_IA32_X2APIC_SELF_IPI:
            {
                if (u64Value & UINT64_C(0xffffffffffffff00))
                    return VERR_OUT_OF_RANGE;
                break;
            }
            case MSR_IA32_X2APIC_EOI:
            {
                if (u64Value != 0)
                    return VERR_OUT_OF_RANGE;
                break;
            }
        }

        /* Write the MSR to the virtual-APIC page. */
        uint16_t const offReg = (idMsr & 0xff) << 4;
        iemVmxVirtApicWriteRaw64(pVCpu, offReg, u64Value);

        /*
         * Record the currently updated APIC offset, as we need this later for figuring
         * out whether to perform TPR, EOI or self-IPI virtualization as well as well
         * as for supplying the exit qualification when causing an APIC-write VM-exit.
         */
        iemVmxVirtApicSetPendingWrite(pVCpu, offReg);

        return VINF_VMX_MODIFIES_BEHAVIOR;
    }

    return VINF_VMX_INTERCEPT_NOT_ACTIVE;
}


/**
 * Interface for HM and EM to virtualize x2APIC MSR accesses.
 *
 * @returns Strict VBox status code.
 * @retval  VINF_VMX_MODIFIES_BEHAVIOR if the MSR access was virtualized.
 * @retval  VINF_VMX_INTERCEPT_NOT_ACTIVE if the MSR access must be handled by
 *          the x2APIC device.
 * @retval  VERR_OUT_RANGE if the caller must raise \#GP(0).
 *
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   idMsr       The MSR being read.
 * @param   pu64Value   Pointer to the value being written or where to store the
 *                      value being read.
 * @param   fWrite      Whether this is an MSR write or read access.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecVmxVirtApicAccessMsr(PVMCPUCC pVCpu, uint32_t idMsr, uint64_t *pu64Value, bool fWrite)
{
    Assert(pu64Value);

    VBOXSTRICTRC rcStrict;
    if (fWrite)
        rcStrict = iemVmxVirtApicAccessMsrWrite(pVCpu, idMsr, *pu64Value);
    else
        rcStrict = iemVmxVirtApicAccessMsrRead(pVCpu, idMsr, pu64Value);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemExecStatusCodeFiddling(pVCpu, rcStrict);

}


/**
 * Finds the most significant set bit in a virtual-APIC 256-bit sparse register.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS when the highest set bit is found.
 * @retval  VERR_NOT_FOUND when no bit is set.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   offReg          The offset of the APIC 256-bit sparse register.
 * @param   pidxHighestBit  Where to store the highest bit (most significant bit)
 *                          set in the register. Only valid when VINF_SUCCESS is
 *                          returned.
 *
 * @remarks The format of the 256-bit sparse register here mirrors that found in
 *          real APIC hardware.
 */
static int iemVmxVirtApicGetHighestSetBitInReg(PVMCPUCC pVCpu, uint16_t offReg, uint8_t *pidxHighestBit)
{
    Assert(offReg < XAPIC_OFF_END + 4);
    Assert(pidxHighestBit);

    /*
     * There are 8 contiguous fragments (of 16-bytes each) in the sparse register.
     * However, in each fragment only the first 4 bytes are used.
     */
    uint8_t const cFrags = 8;
    for (int8_t iFrag = cFrags; iFrag >= 0; iFrag--)
    {
        uint16_t const offFrag = iFrag * 16;
        uint32_t const u32Frag = iemVmxVirtApicReadRaw32(pVCpu, offReg + offFrag);
        if (!u32Frag)
            continue;

        unsigned idxHighestBit = ASMBitLastSetU32(u32Frag);
        Assert(idxHighestBit > 0);
        --idxHighestBit;
        Assert(idxHighestBit <= UINT8_MAX);
        *pidxHighestBit = idxHighestBit;
        return VINF_SUCCESS;
    }
    return VERR_NOT_FOUND;
}


/**
 * Evaluates pending virtual interrupts.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static void iemVmxEvalPendingVirtIntrs(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    Assert(pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_INT_DELIVERY);

    if (!(pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ProcCtls & VMX_PROC_CTLS_INT_WINDOW_EXIT))
    {
        uint8_t const uRvi = RT_LO_U8(pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u16GuestIntStatus);
        uint8_t const uPpr = iemVmxVirtApicReadRaw32(pVCpu, XAPIC_OFF_PPR);

        if ((uRvi >> 4) > (uPpr >> 4))
        {
            Log2(("eval_virt_intrs: uRvi=%#x uPpr=%#x - Signalling pending interrupt\n", uRvi, uPpr));
            VMCPU_FF_SET(pVCpu, VMCPU_FF_INTERRUPT_NESTED_GUEST);
        }
        else
            Log2(("eval_virt_intrs: uRvi=%#x uPpr=%#x - Nothing to do\n", uRvi, uPpr));
    }
}


/**
 * Performs PPR virtualization.
 *
 * @returns VBox strict status code.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static void iemVmxPprVirtualization(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    Assert(pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ProcCtls & VMX_PROC_CTLS_USE_TPR_SHADOW);
    Assert(pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_INT_DELIVERY);

    /*
     * PPR virtualization is caused in response to a VM-entry, TPR-virtualization,
     * or EOI-virtualization.
     *
     * See Intel spec. 29.1.3 "PPR Virtualization".
     */
    uint8_t const uTpr = iemVmxVirtApicReadRaw32(pVCpu, XAPIC_OFF_TPR);
    uint8_t const uSvi = RT_HI_U8(pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u16GuestIntStatus) & 0xf0;

    uint32_t uPpr;
    if ((uTpr & 0xf0) >= uSvi)
        uPpr = uTpr;
    else
        uPpr = uSvi;

    Log2(("ppr_virt: uTpr=%#x uSvi=%#x uPpr=%#x\n", uTpr, uSvi, uPpr));
    iemVmxVirtApicWriteRaw32(pVCpu, XAPIC_OFF_PPR, uPpr);
}


/**
 * Performs VMX TPR virtualization.
 *
 * @returns VBox strict status code.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static VBOXSTRICTRC iemVmxTprVirtualization(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    Assert(pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ProcCtls & VMX_PROC_CTLS_USE_TPR_SHADOW);

    /*
     * We should have already performed the virtual-APIC write to the TPR offset
     * in the virtual-APIC page. We now perform TPR virtualization.
     *
     * See Intel spec. 29.1.2 "TPR Virtualization".
     */
    if (!(pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_INT_DELIVERY))
    {
        uint32_t const uTprThreshold = pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.u32TprThreshold;
        uint32_t const uTpr          = iemVmxVirtApicReadRaw32(pVCpu, XAPIC_OFF_TPR);

        /*
         * If the VTPR falls below the TPR threshold, we must cause a VM-exit.
         * See Intel spec. 29.1.2 "TPR Virtualization".
         */
        if (((uTpr >> 4) & 0xf) < uTprThreshold)
        {
            Log2(("tpr_virt: uTpr=%u uTprThreshold=%u -> VM-exit\n", uTpr, uTprThreshold));
            return iemVmxVmexit(pVCpu, VMX_EXIT_TPR_BELOW_THRESHOLD, 0 /* u64ExitQual */);
        }
    }
    else
    {
        iemVmxPprVirtualization(pVCpu);
        iemVmxEvalPendingVirtIntrs(pVCpu);
    }

    return VINF_SUCCESS;
}


/**
 * Checks whether an EOI write for the given interrupt vector causes a VM-exit or
 * not.
 *
 * @returns @c true if the EOI write is intercepted, @c false otherwise.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   uVector     The interrupt that was acknowledged using an EOI.
 */
static bool iemVmxIsEoiInterceptSet(PCVMCPU pVCpu, uint8_t uVector) RT_NOEXCEPT
{
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    Assert(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_INT_DELIVERY);

    if (uVector < 64)
        return RT_BOOL(pVmcs->u64EoiExitBitmap0.u & RT_BIT_64(uVector));
    if (uVector < 128)
        return RT_BOOL(pVmcs->u64EoiExitBitmap1.u & RT_BIT_64(uVector));
    if (uVector < 192)
        return RT_BOOL(pVmcs->u64EoiExitBitmap2.u & RT_BIT_64(uVector));
    return RT_BOOL(pVmcs->u64EoiExitBitmap3.u & RT_BIT_64(uVector));
}


/**
 * Performs EOI virtualization.
 *
 * @returns VBox strict status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 */
static VBOXSTRICTRC iemVmxEoiVirtualization(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    PVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    Assert(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_INT_DELIVERY);

    /*
     * Clear the interrupt guest-interrupt as no longer in-service (ISR)
     * and get the next guest-interrupt that's in-service (if any).
     *
     * See Intel spec. 29.1.4 "EOI Virtualization".
     */
    uint8_t const uRvi = RT_LO_U8(pVmcs->u16GuestIntStatus);
    uint8_t const uSvi = RT_HI_U8(pVmcs->u16GuestIntStatus);
    Log2(("eoi_virt: uRvi=%#x uSvi=%#x\n", uRvi, uSvi));

    uint8_t uVector = uSvi;
    iemVmxVirtApicClearVectorInReg(pVCpu, XAPIC_OFF_ISR0, uVector);

    uVector = 0;
    iemVmxVirtApicGetHighestSetBitInReg(pVCpu, XAPIC_OFF_ISR0, &uVector);

    if (uVector)
        Log2(("eoi_virt: next interrupt %#x\n", uVector));
    else
        Log2(("eoi_virt: no interrupt pending in ISR\n"));

    /* Update guest-interrupt status SVI (leave RVI portion as it is) in the VMCS. */
    pVmcs->u16GuestIntStatus = RT_MAKE_U16(uRvi, uVector);

    iemVmxPprVirtualization(pVCpu);
    if (iemVmxIsEoiInterceptSet(pVCpu, uVector))
        return iemVmxVmexit(pVCpu, VMX_EXIT_VIRTUALIZED_EOI, uVector);
    iemVmxEvalPendingVirtIntrs(pVCpu);
    return VINF_SUCCESS;
}


/**
 * Performs self-IPI virtualization.
 *
 * @returns VBox strict status code.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static VBOXSTRICTRC iemVmxSelfIpiVirtualization(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    PVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    Assert(pVmcs->u32ProcCtls & VMX_PROC_CTLS_USE_TPR_SHADOW);

    /*
     * We should have already performed the virtual-APIC write to the self-IPI offset
     * in the virtual-APIC page. We now perform self-IPI virtualization.
     *
     * See Intel spec. 29.1.5 "Self-IPI Virtualization".
     */
    uint8_t const uVector = iemVmxVirtApicReadRaw32(pVCpu, XAPIC_OFF_ICR_LO);
    Log2(("self_ipi_virt: uVector=%#x\n", uVector));
    iemVmxVirtApicSetVectorInReg(pVCpu, XAPIC_OFF_IRR0, uVector);
    uint8_t const uRvi = RT_LO_U8(pVmcs->u16GuestIntStatus);
    uint8_t const uSvi = RT_HI_U8(pVmcs->u16GuestIntStatus);
    if (uVector > uRvi)
        pVmcs->u16GuestIntStatus = RT_MAKE_U16(uVector, uSvi);
    iemVmxEvalPendingVirtIntrs(pVCpu);
    return VINF_SUCCESS;
}


/**
 * Performs VMX APIC-write emulation.
 *
 * @returns VBox strict status code.
 * @param   pVCpu   The cross context virtual CPU structure.
 */
VBOXSTRICTRC iemVmxApicWriteEmulation(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;

    /* Import the virtual-APIC write offset (part of the hardware-virtualization state). */
    IEM_CTX_IMPORT_RET(pVCpu, CPUMCTX_EXTRN_HWVIRT);

    /*
     * Perform APIC-write emulation based on the virtual-APIC register written.
     * See Intel spec. 29.4.3.2 "APIC-Write Emulation".
     */
    uint16_t const offApicWrite = iemVmxVirtApicClearPendingWrite(pVCpu);
    VBOXSTRICTRC rcStrict;
    switch (offApicWrite)
    {
        case XAPIC_OFF_TPR:
        {
            /* Clear bytes 3:1 of the VTPR and perform TPR virtualization. */
            uint32_t uTpr = iemVmxVirtApicReadRaw32(pVCpu, XAPIC_OFF_TPR);
            uTpr         &= UINT32_C(0x000000ff);
            iemVmxVirtApicWriteRaw32(pVCpu, XAPIC_OFF_TPR, uTpr);
            Log2(("iemVmxApicWriteEmulation: TPR write %#x\n", uTpr));
            rcStrict = iemVmxTprVirtualization(pVCpu);
            break;
        }

        case XAPIC_OFF_EOI:
        {
            if (pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_INT_DELIVERY)
            {
                /* Clear VEOI and perform EOI virtualization. */
                iemVmxVirtApicWriteRaw32(pVCpu, XAPIC_OFF_EOI, 0);
                Log2(("iemVmxApicWriteEmulation: EOI write\n"));
                rcStrict = iemVmxEoiVirtualization(pVCpu);
            }
            else
                rcStrict = iemVmxVmexitApicWrite(pVCpu, offApicWrite);
            break;
        }

        case XAPIC_OFF_ICR_LO:
        {
            if (pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_INT_DELIVERY)
            {
                /* If the ICR_LO is valid, write it and perform self-IPI virtualization. */
                uint32_t const uIcrLo    = iemVmxVirtApicReadRaw32(pVCpu, XAPIC_OFF_TPR);
                uint32_t const fIcrLoMb0 = UINT32_C(0xfffbb700);
                uint32_t const fIcrLoMb1 = UINT32_C(0x000000f0);
                if (   !(uIcrLo & fIcrLoMb0)
                    &&  (uIcrLo & fIcrLoMb1))
                {
                    Log2(("iemVmxApicWriteEmulation: Self-IPI virtualization with vector %#x\n", (uIcrLo & 0xff)));
                    rcStrict = iemVmxSelfIpiVirtualization(pVCpu);
                }
                else
                    rcStrict = iemVmxVmexitApicWrite(pVCpu, offApicWrite);
            }
            else
                rcStrict = iemVmxVmexitApicWrite(pVCpu, offApicWrite);
            break;
        }

        case XAPIC_OFF_ICR_HI:
        {
            /* Clear bytes 2:0 of VICR_HI. No other virtualization or VM-exit must occur. */
            uint32_t uIcrHi  = iemVmxVirtApicReadRaw32(pVCpu, XAPIC_OFF_ICR_HI);
            uIcrHi          &= UINT32_C(0xff000000);
            iemVmxVirtApicWriteRaw32(pVCpu, XAPIC_OFF_ICR_HI, uIcrHi);
            rcStrict = VINF_SUCCESS;
            break;
        }

        default:
        {
            /* Writes to any other virtual-APIC register causes an APIC-write VM-exit. */
            rcStrict = iemVmxVmexitApicWrite(pVCpu, offApicWrite);
            break;
        }
    }

    return rcStrict;
}


/**
 * Interface for HM and EM to perform an APIC-write emulation which may cause a
 * VM-exit.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecVmxVmexitApicWrite(PVMCPUCC pVCpu)
{
    VBOXSTRICTRC rcStrict = iemVmxApicWriteEmulation(pVCpu);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemExecStatusCodeFiddling(pVCpu, rcStrict);
}


/**
 * Checks guest control registers, debug registers and MSRs as part of VM-entry.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pszInstr    The VMX instruction name (for logging purposes).
 */
DECLINLINE(int) iemVmxVmentryCheckGuestControlRegsMsrs(PVMCPUCC pVCpu, const char *pszInstr)
{
    /*
     * Guest Control Registers, Debug Registers, and MSRs.
     * See Intel spec. 26.3.1.1 "Checks on Guest Control Registers, Debug Registers, and MSRs".
     */
    PCVMXVVMCS const   pVmcs              = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    const char * const pszFailure         = "VM-exit";
    bool const         fUnrestrictedGuest = RT_BOOL(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_UNRESTRICTED_GUEST);

    /* CR0 reserved bits. */
    {
        /* CR0 MB1 bits. */
        uint64_t const u64Cr0Fixed0 = iemVmxGetCr0Fixed0(pVCpu, true /* fVmxNonRootMode */);
        if ((pVmcs->u64GuestCr0.u & u64Cr0Fixed0) == u64Cr0Fixed0)
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestCr0Fixed0);

        /* CR0 MBZ bits. */
        uint64_t const u64Cr0Fixed1 = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64Cr0Fixed1;
        if (!(pVmcs->u64GuestCr0.u & ~u64Cr0Fixed1))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestCr0Fixed1);

        /* Without unrestricted guest support, VT-x supports does not support unpaged protected mode. */
        if (   !fUnrestrictedGuest
            &&  (pVmcs->u64GuestCr0.u & X86_CR0_PG)
            && !(pVmcs->u64GuestCr0.u & X86_CR0_PE))
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestCr0PgPe);
    }

    /* CR4 reserved bits. */
    {
        /* CR4 MB1 bits. */
        uint64_t const u64Cr4Fixed0 = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64Cr4Fixed0;
        if ((pVmcs->u64GuestCr4.u & u64Cr4Fixed0) == u64Cr4Fixed0)
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestCr4Fixed0);

        /* CR4 MBZ bits. */
        uint64_t const u64Cr4Fixed1 = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64Cr4Fixed1;
        if (!(pVmcs->u64GuestCr4.u & ~u64Cr4Fixed1))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestCr4Fixed1);
    }

    /* DEBUGCTL MSR. */
    if (   !(pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_LOAD_DEBUG)
        || !(pVmcs->u64GuestDebugCtlMsr.u & ~MSR_IA32_DEBUGCTL_VALID_MASK_INTEL))
    { /* likely */ }
    else
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestDebugCtl);

    /* 64-bit CPU checks. */
    bool const fGstInLongMode = RT_BOOL(pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_IA32E_MODE_GUEST);
    if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode)
    {
        if (fGstInLongMode)
        {
            /* PAE must be set. */
            if (   (pVmcs->u64GuestCr0.u & X86_CR0_PG)
                && (pVmcs->u64GuestCr0.u & X86_CR4_PAE))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestPae);
        }
        else
        {
            /* PCIDE should not be set. */
            if (!(pVmcs->u64GuestCr4.u & X86_CR4_PCIDE))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestPcide);
        }

        /* CR3. */
        if (!(pVmcs->u64GuestCr3.u >> IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cMaxPhysAddrWidth))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestCr3);

        /* DR7. */
        if (   !(pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_LOAD_DEBUG)
            || !(pVmcs->u64GuestDr7.u & X86_DR7_MBZ_MASK))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestDr7);

        /* SYSENTER ESP and SYSENTER EIP. */
        if (   X86_IS_CANONICAL(pVmcs->u64GuestSysenterEsp.u)
            && X86_IS_CANONICAL(pVmcs->u64GuestSysenterEip.u))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSysenterEspEip);
    }

    /* We don't support IA32_PERF_GLOBAL_CTRL MSR yet. */
    Assert(!(pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_LOAD_PERF_MSR));

    /* PAT MSR. */
    if (   !(pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_LOAD_PAT_MSR)
        ||  CPUMIsPatMsrValid(pVmcs->u64GuestPatMsr.u))
    { /* likely */ }
    else
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestPatMsr);

    /* EFER MSR. */
    if (pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_LOAD_EFER_MSR)
    {
        uint64_t const uValidEferMask = CPUMGetGuestEferMsrValidMask(pVCpu->CTX_SUFF(pVM));
        if (!(pVmcs->u64GuestEferMsr.u & ~uValidEferMask))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestEferMsrRsvd);

        bool const fGstLma = RT_BOOL(pVmcs->u64GuestEferMsr.u & MSR_K6_EFER_LMA);
        bool const fGstLme = RT_BOOL(pVmcs->u64GuestEferMsr.u & MSR_K6_EFER_LME);
        if (   fGstLma == fGstInLongMode
            && (   !(pVmcs->u64GuestCr0.u & X86_CR0_PG)
                || fGstLma == fGstLme))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestEferMsr);
    }

    /* We don't support IA32_BNDCFGS MSR yet. */
    Assert(!(pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_LOAD_BNDCFGS_MSR));

    NOREF(pszInstr);
    NOREF(pszFailure);
    return VINF_SUCCESS;
}


/**
 * Checks guest segment registers, LDTR and TR as part of VM-entry.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pszInstr    The VMX instruction name (for logging purposes).
 */
DECLINLINE(int) iemVmxVmentryCheckGuestSegRegs(PVMCPUCC pVCpu, const char *pszInstr)
{
    /*
     * Segment registers.
     * See Intel spec. 26.3.1.2 "Checks on Guest Segment Registers".
     */
    PCVMXVVMCS const   pVmcs              = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    const char * const pszFailure         = "VM-exit";
    bool const         fGstInV86Mode      = RT_BOOL(pVmcs->u64GuestRFlags.u & X86_EFL_VM);
    bool const         fUnrestrictedGuest = RT_BOOL(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_UNRESTRICTED_GUEST);
    bool const         fGstInLongMode     = RT_BOOL(pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_IA32E_MODE_GUEST);

    /* Selectors. */
    if (   !fGstInV86Mode
        && !fUnrestrictedGuest
        && (pVmcs->GuestSs & X86_SEL_RPL) != (pVmcs->GuestCs & X86_SEL_RPL))
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegSelCsSsRpl);

    for (unsigned iSegReg = 0; iSegReg < X86_SREG_COUNT; iSegReg++)
    {
        CPUMSELREG SelReg;
        int rc = iemVmxVmcsGetGuestSegReg(pVmcs, iSegReg, &SelReg);
        if (RT_LIKELY(rc == VINF_SUCCESS))
        { /* likely */ }
        else
            return rc;

        /*
         * Virtual-8086 mode checks.
         */
        if (fGstInV86Mode)
        {
            /* Base address. */
            if (SelReg.u64Base == (uint64_t)SelReg.Sel << 4)
            { /* likely */ }
            else
            {
                VMXVDIAG const enmDiag = iemVmxGetDiagVmentrySegBaseV86(iSegReg);
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, enmDiag);
            }

            /* Limit. */
            if (SelReg.u32Limit == 0xffff)
            { /* likely */ }
            else
            {
                VMXVDIAG const enmDiag = iemVmxGetDiagVmentrySegLimitV86(iSegReg);
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, enmDiag);
            }

            /* Attribute. */
            if (SelReg.Attr.u == 0xf3)
            { /* likely */ }
            else
            {
                VMXVDIAG const enmDiag = iemVmxGetDiagVmentrySegAttrV86(iSegReg);
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, enmDiag);
            }

            /* We're done; move to checking the next segment. */
            continue;
        }

        /* Checks done by 64-bit CPUs. */
        if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode)
        {
            /* Base address. */
            if (   iSegReg == X86_SREG_FS
                || iSegReg == X86_SREG_GS)
            {
                if (X86_IS_CANONICAL(SelReg.u64Base))
                { /* likely */ }
                else
                {
                    VMXVDIAG const enmDiag = iemVmxGetDiagVmentrySegBase(iSegReg);
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, enmDiag);
                }
            }
            else if (iSegReg == X86_SREG_CS)
            {
                if (!RT_HI_U32(SelReg.u64Base))
                { /* likely */ }
                else
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegBaseCs);
            }
            else
            {
                if (   SelReg.Attr.n.u1Unusable
                    || !RT_HI_U32(SelReg.u64Base))
                { /* likely */ }
                else
                {
                    VMXVDIAG const enmDiag = iemVmxGetDiagVmentrySegBase(iSegReg);
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, enmDiag);
                }
            }
        }

        /*
         * Checks outside Virtual-8086 mode.
         */
        uint8_t const uSegType     =  SelReg.Attr.n.u4Type;
        uint8_t const fCodeDataSeg =  SelReg.Attr.n.u1DescType;
        uint8_t const fUsable      = !SelReg.Attr.n.u1Unusable;
        uint8_t const uDpl         =  SelReg.Attr.n.u2Dpl;
        uint8_t const fPresent     =  SelReg.Attr.n.u1Present;
        uint8_t const uGranularity =  SelReg.Attr.n.u1Granularity;
        uint8_t const uDefBig      =  SelReg.Attr.n.u1DefBig;
        uint8_t const fSegLong     =  SelReg.Attr.n.u1Long;

        /* Code or usable segment. */
        if (   iSegReg == X86_SREG_CS
            || fUsable)
        {
            /* Reserved bits (bits 31:17 and bits 11:8). */
            if (!(SelReg.Attr.u & 0xfffe0f00))
            { /* likely */ }
            else
            {
                VMXVDIAG const enmDiag = iemVmxGetDiagVmentrySegAttrRsvd(iSegReg);
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, enmDiag);
            }

            /* Descriptor type. */
            if (fCodeDataSeg)
            { /* likely */ }
            else
            {
                VMXVDIAG const enmDiag = iemVmxGetDiagVmentrySegAttrDescType(iSegReg);
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, enmDiag);
            }

            /* Present. */
            if (fPresent)
            { /* likely */ }
            else
            {
                VMXVDIAG const enmDiag = iemVmxGetDiagVmentrySegAttrPresent(iSegReg);
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, enmDiag);
            }

            /* Granularity. */
            if (   ((SelReg.u32Limit & 0x00000fff) == 0x00000fff || !uGranularity)
                && ((SelReg.u32Limit & 0xfff00000) == 0x00000000 ||  uGranularity))
            { /* likely */ }
            else
            {
                VMXVDIAG const enmDiag = iemVmxGetDiagVmentrySegAttrGran(iSegReg);
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, enmDiag);
            }
        }

        if (iSegReg == X86_SREG_CS)
        {
            /* Segment Type and DPL. */
            if (   uSegType == (X86_SEL_TYPE_RW | X86_SEL_TYPE_ACCESSED)
                && fUnrestrictedGuest)
            {
                if (uDpl == 0)
                { /* likely */ }
                else
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrCsDplZero);
            }
            else if (   uSegType == (X86_SEL_TYPE_CODE | X86_SEL_TYPE_ACCESSED)
                     || uSegType == (X86_SEL_TYPE_CODE | X86_SEL_TYPE_READ | X86_SEL_TYPE_ACCESSED))
            {
                X86DESCATTR AttrSs; AttrSs.u = pVmcs->u32GuestSsAttr;
                if (uDpl == AttrSs.n.u2Dpl)
                { /* likely */ }
                else
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrCsDplEqSs);
            }
            else if ((uSegType & (X86_SEL_TYPE_CODE | X86_SEL_TYPE_CONF | X86_SEL_TYPE_ACCESSED))
                              == (X86_SEL_TYPE_CODE | X86_SEL_TYPE_CONF | X86_SEL_TYPE_ACCESSED))
            {
                X86DESCATTR AttrSs; AttrSs.u = pVmcs->u32GuestSsAttr;
                if (uDpl <= AttrSs.n.u2Dpl)
                { /* likely */ }
                else
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrCsDplLtSs);
            }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrCsType);

            /* Def/Big. */
            if (   fGstInLongMode
                && fSegLong)
            {
                if (uDefBig == 0)
                { /* likely */ }
                else
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrCsDefBig);
            }
        }
        else if (iSegReg == X86_SREG_SS)
        {
            /* Segment Type. */
            if (   !fUsable
                || uSegType == (X86_SEL_TYPE_RW   | X86_SEL_TYPE_ACCESSED)
                || uSegType == (X86_SEL_TYPE_DOWN | X86_SEL_TYPE_RW | X86_SEL_TYPE_ACCESSED))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrSsType);

            /* DPL. */
            if (!fUnrestrictedGuest)
            {
                if (uDpl == (SelReg.Sel & X86_SEL_RPL))
                { /* likely */ }
                else
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrSsDplEqRpl);
            }
            X86DESCATTR AttrCs; AttrCs.u = pVmcs->u32GuestCsAttr;
            if (   AttrCs.n.u4Type == (X86_SEL_TYPE_RW | X86_SEL_TYPE_ACCESSED)
                || !(pVmcs->u64GuestCr0.u & X86_CR0_PE))
            {
                if (uDpl == 0)
                { /* likely */ }
                else
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrSsDplZero);
            }
        }
        else
        {
            /* DS, ES, FS, GS. */
            if (fUsable)
            {
                /* Segment type. */
                if (uSegType & X86_SEL_TYPE_ACCESSED)
                { /* likely */ }
                else
                {
                    VMXVDIAG const enmDiag = iemVmxGetDiagVmentrySegAttrTypeAcc(iSegReg);
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, enmDiag);
                }

                if (   !(uSegType & X86_SEL_TYPE_CODE)
                    ||  (uSegType & X86_SEL_TYPE_READ))
                { /* likely */ }
                else
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrCsTypeRead);

                /* DPL. */
                if (   !fUnrestrictedGuest
                    && uSegType <= (X86_SEL_TYPE_CODE | X86_SEL_TYPE_READ | X86_SEL_TYPE_ACCESSED))
                {
                    if (uDpl >= (SelReg.Sel & X86_SEL_RPL))
                    { /* likely */ }
                    else
                    {
                        VMXVDIAG const enmDiag = iemVmxGetDiagVmentrySegAttrDplRpl(iSegReg);
                        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, enmDiag);
                    }
                }
            }
        }
    }

    /*
     * LDTR.
     */
    {
        CPUMSELREG Ldtr;
        Ldtr.Sel      = pVmcs->GuestLdtr;
        Ldtr.u32Limit = pVmcs->u32GuestLdtrLimit;
        Ldtr.u64Base  = pVmcs->u64GuestLdtrBase.u;
        Ldtr.Attr.u   = pVmcs->u32GuestLdtrAttr;

        if (!Ldtr.Attr.n.u1Unusable)
        {
            /* Selector. */
            if (!(Ldtr.Sel & X86_SEL_LDT))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegSelLdtr);

            /* Base. */
            if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode)
            {
                if (X86_IS_CANONICAL(Ldtr.u64Base))
                { /* likely */ }
                else
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegBaseLdtr);
            }

            /* Attributes. */
            /* Reserved bits (bits 31:17 and bits 11:8). */
            if (!(Ldtr.Attr.u & 0xfffe0f00))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrLdtrRsvd);

            if (Ldtr.Attr.n.u4Type == X86_SEL_TYPE_SYS_LDT)
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrLdtrType);

            if (!Ldtr.Attr.n.u1DescType)
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrLdtrDescType);

            if (Ldtr.Attr.n.u1Present)
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrLdtrPresent);

            if (   ((Ldtr.u32Limit & 0x00000fff) == 0x00000fff || !Ldtr.Attr.n.u1Granularity)
                && ((Ldtr.u32Limit & 0xfff00000) == 0x00000000 ||  Ldtr.Attr.n.u1Granularity))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrLdtrGran);
        }
    }

    /*
     * TR.
     */
    {
        CPUMSELREG Tr;
        Tr.Sel      = pVmcs->GuestTr;
        Tr.u32Limit = pVmcs->u32GuestTrLimit;
        Tr.u64Base  = pVmcs->u64GuestTrBase.u;
        Tr.Attr.u   = pVmcs->u32GuestTrAttr;

        /* Selector. */
        if (!(Tr.Sel & X86_SEL_LDT))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegSelTr);

        /* Base. */
        if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode)
        {
            if (X86_IS_CANONICAL(Tr.u64Base))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegBaseTr);
        }

        /* Attributes. */
        /* Reserved bits (bits 31:17 and bits 11:8). */
        if (!(Tr.Attr.u & 0xfffe0f00))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrTrRsvd);

        if (!Tr.Attr.n.u1Unusable)
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrTrUnusable);

        if (   Tr.Attr.n.u4Type == X86_SEL_TYPE_SYS_386_TSS_BUSY
            || (   !fGstInLongMode
                && Tr.Attr.n.u4Type == X86_SEL_TYPE_SYS_286_TSS_BUSY))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrTrType);

        if (!Tr.Attr.n.u1DescType)
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrTrDescType);

        if (Tr.Attr.n.u1Present)
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrTrPresent);

        if (   ((Tr.u32Limit & 0x00000fff) == 0x00000fff || !Tr.Attr.n.u1Granularity)
            && ((Tr.u32Limit & 0xfff00000) == 0x00000000 ||  Tr.Attr.n.u1Granularity))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestSegAttrTrGran);
    }

    NOREF(pszInstr);
    NOREF(pszFailure);
    return VINF_SUCCESS;
}


/**
 * Checks guest GDTR and IDTR as part of VM-entry.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pszInstr    The VMX instruction name (for logging purposes).
 */
DECLINLINE(int) iemVmxVmentryCheckGuestGdtrIdtr(PVMCPUCC pVCpu,  const char *pszInstr)
{
    /*
     * GDTR and IDTR.
     * See Intel spec. 26.3.1.3 "Checks on Guest Descriptor-Table Registers".
     */
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    const char *const pszFailure = "VM-exit";

    if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode)
    {
        /* Base. */
        if (X86_IS_CANONICAL(pVmcs->u64GuestGdtrBase.u))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestGdtrBase);

        if (X86_IS_CANONICAL(pVmcs->u64GuestIdtrBase.u))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestIdtrBase);
    }

    /* Limit. */
    if (!RT_HI_U16(pVmcs->u32GuestGdtrLimit))
    { /* likely */ }
    else
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestGdtrLimit);

    if (!RT_HI_U16(pVmcs->u32GuestIdtrLimit))
    { /* likely */ }
    else
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestIdtrLimit);

    NOREF(pszInstr);
    NOREF(pszFailure);
    return VINF_SUCCESS;
}


/**
 * Checks guest RIP and RFLAGS as part of VM-entry.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pszInstr    The VMX instruction name (for logging purposes).
 */
DECLINLINE(int) iemVmxVmentryCheckGuestRipRFlags(PVMCPUCC pVCpu, const char *pszInstr)
{
    /*
     * RIP and RFLAGS.
     * See Intel spec. 26.3.1.4 "Checks on Guest RIP and RFLAGS".
     */
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    const char *const pszFailure = "VM-exit";
    bool const fGstInLongMode    = RT_BOOL(pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_IA32E_MODE_GUEST);

    /* RIP. */
    if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode)
    {
        X86DESCATTR AttrCs;
        AttrCs.u = pVmcs->u32GuestCsAttr;
        if (   !fGstInLongMode
            || !AttrCs.n.u1Long)
        {
            if (!RT_HI_U32(pVmcs->u64GuestRip.u))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestRipRsvd);
        }

        if (   fGstInLongMode
            && AttrCs.n.u1Long)
        {
            Assert(IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cMaxLinearAddrWidth == 48);   /* Canonical. */
            if (   IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cMaxLinearAddrWidth < 64
                && X86_IS_CANONICAL(pVmcs->u64GuestRip.u))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestRip);
        }
    }

    /* RFLAGS (bits 63:22 (or 31:22), bits 15, 5, 3 are reserved, bit 1 MB1). */
    uint64_t const uGuestRFlags = IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode ? pVmcs->u64GuestRFlags.u
                                : pVmcs->u64GuestRFlags.s.Lo;
    if (   !(uGuestRFlags & ~(X86_EFL_LIVE_MASK | X86_EFL_RA1_MASK))
        &&  (uGuestRFlags & X86_EFL_RA1_MASK) == X86_EFL_RA1_MASK)
    { /* likely */ }
    else
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestRFlagsRsvd);

    if (!(uGuestRFlags & X86_EFL_VM))
    { /* likely */ }
    else
    {
        if (   fGstInLongMode
            || !(pVmcs->u64GuestCr0.u & X86_CR0_PE))
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestRFlagsVm);
    }

    if (VMX_ENTRY_INT_INFO_IS_EXT_INT(pVmcs->u32EntryIntInfo))
    {
        if (uGuestRFlags & X86_EFL_IF)
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestRFlagsIf);
    }

    NOREF(pszInstr);
    NOREF(pszFailure);
    return VINF_SUCCESS;
}


/**
 * Checks guest non-register state as part of VM-entry.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pszInstr    The VMX instruction name (for logging purposes).
 */
DECLINLINE(int) iemVmxVmentryCheckGuestNonRegState(PVMCPUCC pVCpu,  const char *pszInstr)
{
    /*
     * Guest non-register state.
     * See Intel spec. 26.3.1.5 "Checks on Guest Non-Register State".
     */
    PVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    const char *const pszFailure = "VM-exit";

    /*
     * Activity state.
     */
    uint64_t const u64GuestVmxMiscMsr = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64Misc;
    uint32_t const fActivityStateMask = RT_BF_GET(u64GuestVmxMiscMsr, VMX_BF_MISC_ACTIVITY_STATES);
    if (!(pVmcs->u32GuestActivityState & fActivityStateMask))
    { /* likely */ }
    else
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestActStateRsvd);

    X86DESCATTR AttrSs; AttrSs.u = pVmcs->u32GuestSsAttr;
    if (   !AttrSs.n.u2Dpl
        || pVmcs->u32GuestActivityState != VMX_VMCS_GUEST_ACTIVITY_HLT)
    { /* likely */ }
    else
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestActStateSsDpl);

    if (   pVmcs->u32GuestIntrState == VMX_VMCS_GUEST_INT_STATE_BLOCK_STI
        || pVmcs->u32GuestIntrState == VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS)
    {
        if (pVmcs->u32GuestActivityState == VMX_VMCS_GUEST_ACTIVITY_ACTIVE)
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestActStateStiMovSs);
    }

    if (VMX_ENTRY_INT_INFO_IS_VALID(pVmcs->u32EntryIntInfo))
    {
        uint8_t const uType   = VMX_ENTRY_INT_INFO_TYPE(pVmcs->u32EntryIntInfo);
        uint8_t const uVector = VMX_ENTRY_INT_INFO_VECTOR(pVmcs->u32EntryIntInfo);
        AssertCompile(VMX_V_GUEST_ACTIVITY_STATE_MASK == (VMX_VMCS_GUEST_ACTIVITY_HLT | VMX_VMCS_GUEST_ACTIVITY_SHUTDOWN));
        switch (pVmcs->u32GuestActivityState)
        {
            case VMX_VMCS_GUEST_ACTIVITY_HLT:
            {
                if (   uType == VMX_ENTRY_INT_INFO_TYPE_EXT_INT
                    || uType == VMX_ENTRY_INT_INFO_TYPE_NMI
                    || (   uType == VMX_ENTRY_INT_INFO_TYPE_HW_XCPT
                        && (   uVector == X86_XCPT_DB
                            || uVector == X86_XCPT_MC))
                    || (   uType   == VMX_ENTRY_INT_INFO_TYPE_OTHER_EVENT
                        && uVector == VMX_ENTRY_INT_INFO_VECTOR_MTF))
                { /* likely */ }
                else
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestActStateHlt);
                break;
            }

            case VMX_VMCS_GUEST_ACTIVITY_SHUTDOWN:
            {
                if (   uType == VMX_ENTRY_INT_INFO_TYPE_NMI
                    || (   uType   == VMX_ENTRY_INT_INFO_TYPE_HW_XCPT
                        && uVector == X86_XCPT_MC))
                { /* likely */ }
                else
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestActStateShutdown);
                break;
            }

            case VMX_VMCS_GUEST_ACTIVITY_ACTIVE:
            default:
                break;
        }
    }

    /*
     * Interruptibility state.
     */
    if (!(pVmcs->u32GuestIntrState & ~VMX_VMCS_GUEST_INT_STATE_MASK))
    { /* likely */ }
    else
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestIntStateRsvd);

    if ((pVmcs->u32GuestIntrState & (VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS | VMX_VMCS_GUEST_INT_STATE_BLOCK_STI))
                                 != (VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS | VMX_VMCS_GUEST_INT_STATE_BLOCK_STI))
    { /* likely */ }
    else
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestIntStateStiMovSs);

    if (    (pVmcs->u64GuestRFlags.u & X86_EFL_IF)
        || !(pVmcs->u32GuestIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_STI))
    { /* likely */ }
    else
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestIntStateRFlagsSti);

    if (VMX_ENTRY_INT_INFO_IS_VALID(pVmcs->u32EntryIntInfo))
    {
        uint8_t const uType = VMX_ENTRY_INT_INFO_TYPE(pVmcs->u32EntryIntInfo);
        if (uType == VMX_ENTRY_INT_INFO_TYPE_EXT_INT)
        {
            if (!(pVmcs->u32GuestIntrState & (VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS | VMX_VMCS_GUEST_INT_STATE_BLOCK_STI)))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestIntStateExtInt);
        }
        else if (uType == VMX_ENTRY_INT_INFO_TYPE_NMI)
        {
            if (!(pVmcs->u32GuestIntrState & (VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS | VMX_VMCS_GUEST_INT_STATE_BLOCK_STI)))
            { /* likely */ }
            else
            {
                /*
                 * We don't support injecting NMIs when blocking-by-STI would be in effect.
                 * We update the Exit qualification only when blocking-by-STI is set
                 * without blocking-by-MovSS being set. Although in practise it  does not
                 * make much difference since the order of checks are implementation defined.
                 */
                if (!(pVmcs->u32GuestIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS))
                    iemVmxVmcsSetExitQual(pVCpu, VMX_ENTRY_FAIL_QUAL_NMI_INJECT);
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestIntStateNmi);
            }

            if (   !(pVmcs->u32PinCtls & VMX_PIN_CTLS_VIRT_NMI)
                || !(pVmcs->u32GuestIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_NMI))
            { /* likely */ }
            else
               IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestIntStateVirtNmi);
        }
    }

    /* We don't support SMM yet. So blocking-by-SMIs must not be set. */
    if (!(pVmcs->u32GuestIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_SMI))
    { /* likely */ }
    else
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestIntStateSmi);

    /* We don't support SGX yet. So enclave-interruption must not be set. */
    if (!(pVmcs->u32GuestIntrState & VMX_VMCS_GUEST_INT_STATE_ENCLAVE))
    { /* likely */ }
    else
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestIntStateEnclave);

    /*
     * Pending debug exceptions.
     */
    uint64_t const uPendingDbgXcpts = IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode
                                    ? pVmcs->u64GuestPendingDbgXcpts.u
                                    : pVmcs->u64GuestPendingDbgXcpts.s.Lo;
    if (!(uPendingDbgXcpts & ~VMX_VMCS_GUEST_PENDING_DEBUG_VALID_MASK))
    { /* likely */ }
    else
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestPndDbgXcptRsvd);

    if (   (pVmcs->u32GuestIntrState & (VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS | VMX_VMCS_GUEST_INT_STATE_BLOCK_STI))
        || pVmcs->u32GuestActivityState == VMX_VMCS_GUEST_ACTIVITY_HLT)
    {
        if (   (pVmcs->u64GuestRFlags.u & X86_EFL_TF)
            && !(pVmcs->u64GuestDebugCtlMsr.u & MSR_IA32_DEBUGCTL_BTF)
            && !(uPendingDbgXcpts & VMX_VMCS_GUEST_PENDING_DEBUG_XCPT_BS))
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestPndDbgXcptBsTf);

        if (   (   !(pVmcs->u64GuestRFlags.u & X86_EFL_TF)
                ||  (pVmcs->u64GuestDebugCtlMsr.u & MSR_IA32_DEBUGCTL_BTF))
            && (uPendingDbgXcpts & VMX_VMCS_GUEST_PENDING_DEBUG_XCPT_BS))
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestPndDbgXcptBsNoTf);
    }

    /* We don't support RTM (Real-time Transactional Memory) yet. */
    if (!(uPendingDbgXcpts & VMX_VMCS_GUEST_PENDING_DEBUG_RTM))
    { /* likely */ }
    else
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestPndDbgXcptRtm);

    /*
     * VMCS link pointer.
     */
    if (pVmcs->u64VmcsLinkPtr.u != UINT64_C(0xffffffffffffffff))
    {
        RTGCPHYS const GCPhysShadowVmcs = pVmcs->u64VmcsLinkPtr.u;
        /* We don't support SMM yet (so VMCS link pointer cannot be the current VMCS). */
        if (GCPhysShadowVmcs != IEM_VMX_GET_CURRENT_VMCS(pVCpu))
        { /* likely */ }
        else
        {
            iemVmxVmcsSetExitQual(pVCpu, VMX_ENTRY_FAIL_QUAL_VMCS_LINK_PTR);
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_VmcsLinkPtrCurVmcs);
        }

        /* Validate the address. */
        if (   !(GCPhysShadowVmcs & X86_PAGE_4K_OFFSET_MASK)
            && !(GCPhysShadowVmcs >> IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cVmxMaxPhysAddrWidth)
            &&  PGMPhysIsGCPhysNormal(pVCpu->CTX_SUFF(pVM), GCPhysShadowVmcs))
        { /* likely */ }
        else
        {
            iemVmxVmcsSetExitQual(pVCpu, VMX_ENTRY_FAIL_QUAL_VMCS_LINK_PTR);
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_AddrVmcsLinkPtr);
        }
    }

    NOREF(pszInstr);
    NOREF(pszFailure);
    return VINF_SUCCESS;
}


#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
/**
 * Checks guest PDPTEs as part of VM-entry.
 *
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pszInstr        The VMX instruction name (for logging purposes).
 */
static int iemVmxVmentryCheckGuestPdptes(PVMCPUCC pVCpu, const char *pszInstr) RT_NOEXCEPT
{
    /*
     * Guest PDPTEs.
     * See Intel spec. 26.3.1.5 "Checks on Guest Page-Directory-Pointer-Table Entries".
     */
    PVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    const char * const pszFailure = "VM-exit";

    /*
     * When EPT is used, we need to validate the PAE PDPTEs provided in the VMCS.
     * Otherwise, we load any PAE PDPTEs referenced by CR3 at a later point.
     */
    if (   iemVmxVmcsIsGuestPaePagingEnabled(pVmcs)
        && (pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_EPT))
    {
        /* Get PDPTEs from the VMCS. */
        X86PDPE aPaePdptes[X86_PG_PAE_PDPE_ENTRIES];
        aPaePdptes[0].u = pVmcs->u64GuestPdpte0.u;
        aPaePdptes[1].u = pVmcs->u64GuestPdpte1.u;
        aPaePdptes[2].u = pVmcs->u64GuestPdpte2.u;
        aPaePdptes[3].u = pVmcs->u64GuestPdpte3.u;

        /* Check validity of the PDPTEs. */
        if (PGMGstArePaePdpesValid(pVCpu, &aPaePdptes[0]))
        { /* likely */ }
        else
        {
            iemVmxVmcsSetExitQual(pVCpu, VMX_ENTRY_FAIL_QUAL_PDPTE);
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestPdpte);
        }
    }

    NOREF(pszFailure);
    NOREF(pszInstr);
    return VINF_SUCCESS;
}
#endif  /* VBOX_WITH_NESTED_HWVIRT_VMX_EPT */


/**
 * Checks guest-state as part of VM-entry.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   pszInstr        The VMX instruction name (for logging purposes).
 */
static int iemVmxVmentryCheckGuestState(PVMCPUCC pVCpu, const char *pszInstr) RT_NOEXCEPT
{
    int rc = iemVmxVmentryCheckGuestControlRegsMsrs(pVCpu, pszInstr);
    if (RT_SUCCESS(rc))
    {
        rc = iemVmxVmentryCheckGuestSegRegs(pVCpu, pszInstr);
        if (RT_SUCCESS(rc))
        {
            rc = iemVmxVmentryCheckGuestGdtrIdtr(pVCpu, pszInstr);
            if (RT_SUCCESS(rc))
            {
                rc = iemVmxVmentryCheckGuestRipRFlags(pVCpu, pszInstr);
                if (RT_SUCCESS(rc))
                {
                    rc = iemVmxVmentryCheckGuestNonRegState(pVCpu, pszInstr);
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
                    if (RT_SUCCESS(rc))
                        rc = iemVmxVmentryCheckGuestPdptes(pVCpu, pszInstr);
#endif
                }
            }
        }
    }
    return rc;
}


/**
 * Checks host-state as part of VM-entry.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pszInstr    The VMX instruction name (for logging purposes).
 */
static int iemVmxVmentryCheckHostState(PVMCPUCC pVCpu, const char *pszInstr) RT_NOEXCEPT
{
    /*
     * Host Control Registers and MSRs.
     * See Intel spec. 26.2.2 "Checks on Host Control Registers and MSRs".
     */
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    const char * const pszFailure = "VMFail";

    /* CR0 reserved bits. */
    {
        /* CR0 MB1 bits. */
        uint64_t const u64Cr0Fixed0 = iemVmxGetCr0Fixed0(pVCpu, true /* fVmxNonRootMode */);
        if ((pVmcs->u64HostCr0.u & u64Cr0Fixed0) == u64Cr0Fixed0)
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostCr0Fixed0);

        /* CR0 MBZ bits. */
        uint64_t const u64Cr0Fixed1 = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64Cr0Fixed1;
        if (!(pVmcs->u64HostCr0.u & ~u64Cr0Fixed1))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostCr0Fixed1);
    }

    /* CR4 reserved bits. */
    {
        /* CR4 MB1 bits. */
        uint64_t const u64Cr4Fixed0 = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64Cr4Fixed0;
        if ((pVmcs->u64HostCr4.u & u64Cr4Fixed0) == u64Cr4Fixed0)
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostCr4Fixed0);

        /* CR4 MBZ bits. */
        uint64_t const u64Cr4Fixed1 = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64Cr4Fixed1;
        if (!(pVmcs->u64HostCr4.u & ~u64Cr4Fixed1))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostCr4Fixed1);
    }

    if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode)
    {
        /* CR3 reserved bits. */
        if (!(pVmcs->u64HostCr3.u >> IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cMaxPhysAddrWidth))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostCr3);

        /* SYSENTER ESP and SYSENTER EIP. */
        if (   X86_IS_CANONICAL(pVmcs->u64HostSysenterEsp.u)
            && X86_IS_CANONICAL(pVmcs->u64HostSysenterEip.u))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostSysenterEspEip);
    }

    /* We don't support IA32_PERF_GLOBAL_CTRL MSR yet. */
    Assert(!(pVmcs->u32ExitCtls & VMX_EXIT_CTLS_LOAD_PERF_MSR));

    /* PAT MSR. */
    if (   !(pVmcs->u32ExitCtls & VMX_EXIT_CTLS_LOAD_PAT_MSR)
        ||  CPUMIsPatMsrValid(pVmcs->u64HostPatMsr.u))
    { /* likely */ }
    else
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostPatMsr);

    /* EFER MSR. */
    bool const     fHostInLongMode = RT_BOOL(pVmcs->u32ExitCtls & VMX_EXIT_CTLS_HOST_ADDR_SPACE_SIZE);
    uint64_t const uValidEferMask  = CPUMGetGuestEferMsrValidMask(pVCpu->CTX_SUFF(pVM));
    if (pVmcs->u32ExitCtls & VMX_EXIT_CTLS_LOAD_EFER_MSR)
    {
        if (!(pVmcs->u64HostEferMsr.u & ~uValidEferMask))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostEferMsrRsvd);

        bool const fHostLma = RT_BOOL(pVmcs->u64HostEferMsr.u & MSR_K6_EFER_LMA);
        bool const fHostLme = RT_BOOL(pVmcs->u64HostEferMsr.u & MSR_K6_EFER_LME);
        if (   fHostInLongMode == fHostLma
            && fHostInLongMode == fHostLme)
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostEferMsr);
    }

    /*
     * Host Segment and Descriptor-Table Registers.
     * See Intel spec. 26.2.3 "Checks on Host Segment and Descriptor-Table Registers".
     */
    /* Selector RPL and TI. */
    if (   !(pVmcs->HostCs & (X86_SEL_RPL | X86_SEL_LDT))
        && !(pVmcs->HostSs & (X86_SEL_RPL | X86_SEL_LDT))
        && !(pVmcs->HostDs & (X86_SEL_RPL | X86_SEL_LDT))
        && !(pVmcs->HostEs & (X86_SEL_RPL | X86_SEL_LDT))
        && !(pVmcs->HostFs & (X86_SEL_RPL | X86_SEL_LDT))
        && !(pVmcs->HostGs & (X86_SEL_RPL | X86_SEL_LDT))
        && !(pVmcs->HostTr & (X86_SEL_RPL | X86_SEL_LDT)))
    { /* likely */ }
    else
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostSel);

    /* CS and TR selectors cannot be 0. */
    if (   pVmcs->HostCs
        && pVmcs->HostTr)
    { /* likely */ }
    else
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostCsTr);

    /* SS cannot be 0 if 32-bit host. */
    if (   fHostInLongMode
        || pVmcs->HostSs)
    { /* likely */ }
    else
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostSs);

    if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode)
    {
        /* FS, GS, GDTR, IDTR, TR base address. */
        if (   X86_IS_CANONICAL(pVmcs->u64HostFsBase.u)
            && X86_IS_CANONICAL(pVmcs->u64HostGsBase.u)
            && X86_IS_CANONICAL(pVmcs->u64HostGdtrBase.u)
            && X86_IS_CANONICAL(pVmcs->u64HostIdtrBase.u)
            && X86_IS_CANONICAL(pVmcs->u64HostTrBase.u))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostSegBase);
    }

    /*
     * Host address-space size for 64-bit CPUs.
     * See Intel spec. 26.2.4 "Checks Related to Address-Space Size".
     */
    bool const fGstInLongMode = RT_BOOL(pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_IA32E_MODE_GUEST);
    if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode)
    {
        bool const fCpuInLongMode = CPUMIsGuestInLongMode(pVCpu);

        /* Logical processor in IA-32e mode. */
        if (fCpuInLongMode)
        {
            if (fHostInLongMode)
            {
                /* PAE must be set. */
                if (pVmcs->u64HostCr4.u & X86_CR4_PAE)
                { /* likely */ }
                else
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostCr4Pae);

                /* RIP must be canonical. */
                if (X86_IS_CANONICAL(pVmcs->u64HostRip.u))
                { /* likely */ }
                else
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostRip);
            }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostLongMode);
        }
        else
        {
            /* Logical processor is outside IA-32e mode. */
            if (   !fGstInLongMode
                && !fHostInLongMode)
            {
                /* PCIDE should not be set. */
                if (!(pVmcs->u64HostCr4.u & X86_CR4_PCIDE))
                { /* likely */ }
                else
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostCr4Pcide);

                /* The high 32-bits of RIP MBZ. */
                if (!pVmcs->u64HostRip.s.Hi)
                { /* likely */ }
                else
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostRipRsvd);
            }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostGuestLongMode);
        }
    }
    else
    {
        /* Host address-space size for 32-bit CPUs. */
        if (   !fGstInLongMode
            && !fHostInLongMode)
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_HostGuestLongModeNoCpu);
    }

    NOREF(pszInstr);
    NOREF(pszFailure);
    return VINF_SUCCESS;
}


#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
/**
 * Checks the EPT pointer VMCS field as part of VM-entry.
 *
 * @returns VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uEptPtr         The EPT pointer to check.
 * @param   penmVmxDiag     Where to store the diagnostic reason on failure (not
 *                          updated on success). Optional, can be NULL.
 */
static int iemVmxVmentryCheckEptPtr(PVMCPUCC pVCpu, uint64_t uEptPtr, VMXVDIAG *penmVmxDiag) RT_NOEXCEPT
{
    VMXVDIAG enmVmxDiag;

    /* Reserved bits. */
    uint8_t const  cMaxPhysAddrWidth = IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cMaxPhysAddrWidth;
    uint64_t const fValidMask        = VMX_EPTP_VALID_MASK & ~(UINT64_MAX << cMaxPhysAddrWidth);
    if (uEptPtr & fValidMask)
    {
        /* Memory Type. */
        uint64_t const fCaps    = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64EptVpidCaps;
        uint8_t const  fMemType = RT_BF_GET(uEptPtr, VMX_BF_EPTP_MEMTYPE);
        if (   (   fMemType == VMX_EPTP_MEMTYPE_WB
                && RT_BF_GET(fCaps, VMX_BF_EPT_VPID_CAP_MEMTYPE_WB))
            || (   fMemType == VMX_EPTP_MEMTYPE_UC
                && RT_BF_GET(fCaps, VMX_BF_EPT_VPID_CAP_MEMTYPE_UC)))
        {
            /*
             * Page walk length (PML4).
             * Intel used to specify bit 7 of IA32_VMX_EPT_VPID_CAP as page walk length
             * of 5 but that seems to be removed from the latest specs. leaving only PML4
             * as the maximum supported page-walk level hence we hardcode it as 3 (1 less than 4)
             */
            Assert(RT_BF_GET(fCaps, VMX_BF_EPT_VPID_CAP_PAGE_WALK_LENGTH_4));
            if (RT_BF_GET(uEptPtr, VMX_BF_EPTP_PAGE_WALK_LENGTH) == 3)
            {
                /* Access and dirty bits support in EPT structures. */
                if (   !RT_BF_GET(uEptPtr, VMX_BF_EPTP_ACCESS_DIRTY)
                    ||  RT_BF_GET(fCaps, VMX_BF_EPT_VPID_CAP_ACCESS_DIRTY))
                    return VINF_SUCCESS;

                enmVmxDiag = kVmxVDiag_Vmentry_EptpAccessDirty;
            }
            else
                enmVmxDiag = kVmxVDiag_Vmentry_EptpPageWalkLength;
        }
        else
            enmVmxDiag = kVmxVDiag_Vmentry_EptpMemType;
    }
    else
        enmVmxDiag = kVmxVDiag_Vmentry_EptpRsvd;

    if (penmVmxDiag)
        *penmVmxDiag = enmVmxDiag;
    return VERR_VMX_VMENTRY_FAILED;
}
#endif


/**
 * Checks VMCS controls fields as part of VM-entry.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pszInstr    The VMX instruction name (for logging purposes).
 *
 * @remarks This may update secondary-processor based VM-execution control fields
 *          in the current VMCS if necessary.
 */
static int iemVmxVmentryCheckCtls(PVMCPUCC pVCpu, const char *pszInstr) RT_NOEXCEPT
{
    PVMXVVMCS pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    const char * const pszFailure = "VMFail";
    bool const fVmxTrueMsrs       = RT_BOOL(pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64Basic & VMX_BF_BASIC_TRUE_CTLS_MASK);

    /*
     * VM-execution controls.
     * See Intel spec. 26.2.1.1 "VM-Execution Control Fields".
     */
    {
        /* Pin-based VM-execution controls. */
        {
            VMXCTLSMSR const PinCtls = fVmxTrueMsrs ? pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.TruePinCtls
                                                    : pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.PinCtls;
            if (!(~pVmcs->u32PinCtls & PinCtls.n.allowed0))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_PinCtlsDisallowed0);

            if (!(pVmcs->u32PinCtls & ~PinCtls.n.allowed1))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_PinCtlsAllowed1);
        }

        /* Processor-based VM-execution controls. */
        {
            VMXCTLSMSR const ProcCtls = fVmxTrueMsrs ? pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.TrueProcCtls
                                                     : pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.ProcCtls;
            if (!(~pVmcs->u32ProcCtls & ProcCtls.n.allowed0))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_ProcCtlsDisallowed0);

            if (!(pVmcs->u32ProcCtls & ~ProcCtls.n.allowed1))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_ProcCtlsAllowed1);
        }

        /* Secondary processor-based VM-execution controls. */
        if (pVmcs->u32ProcCtls & VMX_PROC_CTLS_USE_SECONDARY_CTLS)
        {
            VMXCTLSMSR const ProcCtls2 = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.ProcCtls2;
            if (!(~pVmcs->u32ProcCtls2 & ProcCtls2.n.allowed0))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_ProcCtls2Disallowed0);

            if (!(pVmcs->u32ProcCtls2 & ~ProcCtls2.n.allowed1))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_ProcCtls2Allowed1);
        }
        else if (pVmcs->u32ProcCtls2)
        {
            /*
             * If the "activate secondary controls" is clear, then the secondary processor-based VM-execution controls
             * is treated as 0.
             *
             * See Intel spec. 26.2.1.1 "VM-Execution Control Fields".
             *
             * Since this is a rather rare occurrence (only observed for a few VM-entries with Microsoft Hyper-V
             * enabled Windows Server 2008 R2 guest), it's not worth changing every place that reads this control to
             * also check the "activate secondary controls" bit. Instead, we temporarily save the guest programmed
             * control here, zero out the value the rest of our code uses and restore the guest programmed value
             * on VM-exit.
             */
            pVmcs->u32RestoreProcCtls2 = pVmcs->u32ProcCtls2;
            pVmcs->u32ProcCtls2        = 0;
        }

        /* CR3-target count. */
        if (pVmcs->u32Cr3TargetCount <= VMX_V_CR3_TARGET_COUNT)
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_Cr3TargetCount);

        /* I/O bitmaps physical addresses. */
        if (pVmcs->u32ProcCtls & VMX_PROC_CTLS_USE_IO_BITMAPS)
        {
            RTGCPHYS const GCPhysIoBitmapA = pVmcs->u64AddrIoBitmapA.u;
            if (   !(GCPhysIoBitmapA & X86_PAGE_4K_OFFSET_MASK)
                && !(GCPhysIoBitmapA >> IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cVmxMaxPhysAddrWidth)
                &&  PGMPhysIsGCPhysNormal(pVCpu->CTX_SUFF(pVM), GCPhysIoBitmapA))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_AddrIoBitmapA);

            RTGCPHYS const GCPhysIoBitmapB = pVmcs->u64AddrIoBitmapB.u;
            if (   !(GCPhysIoBitmapB & X86_PAGE_4K_OFFSET_MASK)
                && !(GCPhysIoBitmapB >> IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cVmxMaxPhysAddrWidth)
                &&  PGMPhysIsGCPhysNormal(pVCpu->CTX_SUFF(pVM), GCPhysIoBitmapB))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_AddrIoBitmapB);
        }

        /* MSR bitmap physical address. */
        if (pVmcs->u32ProcCtls & VMX_PROC_CTLS_USE_MSR_BITMAPS)
        {
            RTGCPHYS const GCPhysMsrBitmap = pVmcs->u64AddrMsrBitmap.u;
            if (   !(GCPhysMsrBitmap & X86_PAGE_4K_OFFSET_MASK)
                && !(GCPhysMsrBitmap >> IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cVmxMaxPhysAddrWidth)
                &&  PGMPhysIsGCPhysNormal(pVCpu->CTX_SUFF(pVM), GCPhysMsrBitmap))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_AddrMsrBitmap);
        }

        /* TPR shadow related controls. */
        if (pVmcs->u32ProcCtls & VMX_PROC_CTLS_USE_TPR_SHADOW)
        {
            /* Virtual-APIC page physical address. */
            RTGCPHYS const GCPhysVirtApic = pVmcs->u64AddrVirtApic.u;
            if (   !(GCPhysVirtApic & X86_PAGE_4K_OFFSET_MASK)
                && !(GCPhysVirtApic >> IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cVmxMaxPhysAddrWidth)
                &&  PGMPhysIsGCPhysNormal(pVCpu->CTX_SUFF(pVM), GCPhysVirtApic))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_AddrVirtApicPage);

            /* TPR threshold bits 31:4 MBZ without virtual-interrupt delivery. */
            if (   !(pVmcs->u32TprThreshold & ~VMX_TPR_THRESHOLD_MASK)
                ||  (pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_INT_DELIVERY))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_TprThresholdRsvd);

            /* The rest done XXX document */
        }
        else
        {
            if (   !(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_X2APIC_MODE)
                && !(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_APIC_REG_VIRT)
                && !(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_INT_DELIVERY))
            { /* likely */ }
            else
            {
                if (pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_X2APIC_MODE)
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_VirtX2ApicTprShadow);
                if (pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_APIC_REG_VIRT)
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_ApicRegVirt);
                Assert(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_INT_DELIVERY);
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_VirtIntDelivery);
            }
        }

        /* NMI exiting and virtual-NMIs. */
        if (    (pVmcs->u32PinCtls & VMX_PIN_CTLS_NMI_EXIT)
            || !(pVmcs->u32PinCtls & VMX_PIN_CTLS_VIRT_NMI))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_VirtNmi);

        /* Virtual-NMIs and NMI-window exiting. */
        if (    (pVmcs->u32PinCtls & VMX_PIN_CTLS_VIRT_NMI)
            || !(pVmcs->u32ProcCtls & VMX_PROC_CTLS_NMI_WINDOW_EXIT))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_NmiWindowExit);

        /* Virtualize APIC accesses. */
        if (pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_APIC_ACCESS)
        {
            /* APIC-access physical address. */
            RTGCPHYS const GCPhysApicAccess = pVmcs->u64AddrApicAccess.u;
            if (   !(GCPhysApicAccess & X86_PAGE_4K_OFFSET_MASK)
                && !(GCPhysApicAccess >> IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cVmxMaxPhysAddrWidth)
                &&  PGMPhysIsGCPhysNormal(pVCpu->CTX_SUFF(pVM), GCPhysApicAccess))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_AddrApicAccess);

            /*
             * Disallow APIC-access page and virtual-APIC page from being the same address.
             * Note! This is not an Intel requirement, but one imposed by our implementation.
             * This is done primarily to simplify recursion scenarios while redirecting accesses
             * between the APIC-access page and the virtual-APIC page. If any nested hypervisor
             * requires this, we can implement it later
             */
            if (pVmcs->u32ProcCtls & VMX_PROC_CTLS_USE_TPR_SHADOW)
            {
                RTGCPHYS const GCPhysVirtApic = pVmcs->u64AddrVirtApic.u;
                if (GCPhysVirtApic != GCPhysApicAccess)
                { /* likely */ }
                else
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_AddrApicAccessEqVirtApic);
            }
        }

        /* Virtualize-x2APIC mode is mutually exclusive with virtualize-APIC accesses. */
        if (   !(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_X2APIC_MODE)
            || !(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_APIC_ACCESS))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_VirtX2ApicVirtApic);

        /* Virtual-interrupt delivery requires external interrupt exiting. */
        if (   !(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_INT_DELIVERY)
            ||  (pVmcs->u32PinCtls & VMX_PIN_CTLS_EXT_INT_EXIT))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_VirtX2ApicVirtApic);

        /* VPID. */
        if (   !(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VPID)
            || pVmcs->u16Vpid != 0)
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_Vpid);

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
        /* Extended-Page-Table Pointer (EPTP). */
        if (pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_EPT)
        {
            VMXVDIAG enmVmxDiag;
            int const rc = iemVmxVmentryCheckEptPtr(pVCpu, pVmcs->u64EptPtr.u, &enmVmxDiag);
            if (RT_SUCCESS(rc))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET_2(pVCpu, pszInstr, pszFailure, enmVmxDiag, rc);
        }
#else
        Assert(!(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_EPT));
        Assert(!(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_UNRESTRICTED_GUEST));
#endif
        Assert(!(pVmcs->u32PinCtls & VMX_PIN_CTLS_POSTED_INT));             /* We don't support posted interrupts yet. */
        Assert(!(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_PML));                /* We don't support PML yet. */
        Assert(!(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VMFUNC));             /* We don't support VM functions yet. */
        Assert(!(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_EPT_XCPT_VE));        /* We don't support EPT-violation #VE yet. */
        Assert(!(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_TSC_SCALING));        /* We don't support TSC-scaling yet. */

        /* VMCS shadowing. */
        if (pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VMCS_SHADOWING)
        {
            /* VMREAD-bitmap physical address. */
            RTGCPHYS const GCPhysVmreadBitmap = pVmcs->u64AddrVmreadBitmap.u;
            if (   !(GCPhysVmreadBitmap & X86_PAGE_4K_OFFSET_MASK)
                && !(GCPhysVmreadBitmap >> IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cVmxMaxPhysAddrWidth)
                &&  PGMPhysIsGCPhysNormal(pVCpu->CTX_SUFF(pVM), GCPhysVmreadBitmap))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_AddrVmreadBitmap);

            /* VMWRITE-bitmap physical address. */
            RTGCPHYS const GCPhysVmwriteBitmap = pVmcs->u64AddrVmreadBitmap.u;
            if (   !(GCPhysVmwriteBitmap & X86_PAGE_4K_OFFSET_MASK)
                && !(GCPhysVmwriteBitmap >> IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cVmxMaxPhysAddrWidth)
                &&  PGMPhysIsGCPhysNormal(pVCpu->CTX_SUFF(pVM), GCPhysVmwriteBitmap))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_AddrVmwriteBitmap);
        }
    }

    /*
     * VM-exit controls.
     * See Intel spec. 26.2.1.2 "VM-Exit Control Fields".
     */
    {
        VMXCTLSMSR const ExitCtls = fVmxTrueMsrs ? pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.TrueExitCtls
                                                 : pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.ExitCtls;
        if (!(~pVmcs->u32ExitCtls & ExitCtls.n.allowed0))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_ExitCtlsDisallowed0);

        if (!(pVmcs->u32ExitCtls & ~ExitCtls.n.allowed1))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_ExitCtlsAllowed1);

        /* Save preemption timer without activating it. */
        if (    (pVmcs->u32PinCtls & VMX_PIN_CTLS_PREEMPT_TIMER)
            || !(pVmcs->u32ProcCtls & VMX_EXIT_CTLS_SAVE_PREEMPT_TIMER))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_SavePreemptTimer);

        /* VM-exit MSR-store count and VM-exit MSR-store area address. */
        if (pVmcs->u32ExitMsrStoreCount)
        {
            if (   !(pVmcs->u64AddrExitMsrStore.u & VMX_AUTOMSR_OFFSET_MASK)
                && !(pVmcs->u64AddrExitMsrStore.u >> IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cVmxMaxPhysAddrWidth)
                &&  PGMPhysIsGCPhysNormal(pVCpu->CTX_SUFF(pVM), pVmcs->u64AddrExitMsrStore.u))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_AddrExitMsrStore);
        }

        /* VM-exit MSR-load count and VM-exit MSR-load area address. */
        if (pVmcs->u32ExitMsrLoadCount)
        {
            if (   !(pVmcs->u64AddrExitMsrLoad.u & VMX_AUTOMSR_OFFSET_MASK)
                && !(pVmcs->u64AddrExitMsrLoad.u >> IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cVmxMaxPhysAddrWidth)
                &&  PGMPhysIsGCPhysNormal(pVCpu->CTX_SUFF(pVM), pVmcs->u64AddrExitMsrLoad.u))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_AddrExitMsrLoad);
        }
    }

    /*
     * VM-entry controls.
     * See Intel spec. 26.2.1.3 "VM-Entry Control Fields".
     */
    {
        VMXCTLSMSR const EntryCtls = fVmxTrueMsrs ? pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.TrueEntryCtls
                                                  : pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.EntryCtls;
        if (!(~pVmcs->u32EntryCtls & EntryCtls.n.allowed0))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_EntryCtlsDisallowed0);

        if (!(pVmcs->u32EntryCtls & ~EntryCtls.n.allowed1))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_EntryCtlsAllowed1);

        /* Event injection. */
        uint32_t const uIntInfo = pVmcs->u32EntryIntInfo;
        if (RT_BF_GET(uIntInfo, VMX_BF_ENTRY_INT_INFO_VALID))
        {
            /* Type and vector. */
            uint8_t const uType   = RT_BF_GET(uIntInfo, VMX_BF_ENTRY_INT_INFO_TYPE);
            uint8_t const uVector = RT_BF_GET(uIntInfo, VMX_BF_ENTRY_INT_INFO_VECTOR);
            uint8_t const uRsvd   = RT_BF_GET(uIntInfo, VMX_BF_ENTRY_INT_INFO_RSVD_12_30);
            if (   !uRsvd
                && VMXIsEntryIntInfoTypeValid(IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fVmxMonitorTrapFlag, uType)
                && VMXIsEntryIntInfoVectorValid(uVector, uType))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_EntryIntInfoTypeVecRsvd);

            /* Exception error code. */
            if (RT_BF_GET(uIntInfo, VMX_BF_ENTRY_INT_INFO_ERR_CODE_VALID))
            {
                /* Delivery possible only in Unrestricted-guest mode when CR0.PE is set. */
                if (   !(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_UNRESTRICTED_GUEST)
                    ||  (pVmcs->u64GuestCr0.s.Lo & X86_CR0_PE))
                { /* likely */ }
                else
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_EntryIntInfoErrCodePe);

                /* Exceptions that provide an error code. */
                if (uType == VMX_ENTRY_INT_INFO_TYPE_HW_XCPT)
                {
                    if (   uVector == X86_XCPT_DF
                        || uVector == X86_XCPT_TS
                        || uVector == X86_XCPT_NP
                        || uVector == X86_XCPT_SS
                        || uVector == X86_XCPT_GP
                        || uVector == X86_XCPT_PF
                        || uVector == X86_XCPT_AC)
                    { /* likely */ }
                    else
                        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_EntryIntInfoErrCodeVec);
                }

                /* Exception error-code reserved bits. */
                if (!(pVmcs->u32EntryXcptErrCode & ~VMX_ENTRY_INT_XCPT_ERR_CODE_VALID_MASK))
                { /* likely */ }
                else
                    IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_EntryXcptErrCodeRsvd);

                /* Injecting a software interrupt, software exception or privileged software exception. */
                if (   uType == VMX_ENTRY_INT_INFO_TYPE_SW_INT
                    || uType == VMX_ENTRY_INT_INFO_TYPE_SW_XCPT
                    || uType == VMX_ENTRY_INT_INFO_TYPE_PRIV_SW_XCPT)
                {
                    /* Instruction length must be in the range 0-15. */
                    if (pVmcs->u32EntryInstrLen <= VMX_ENTRY_INSTR_LEN_MAX)
                    { /* likely */ }
                    else
                        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_EntryInstrLen);

                    /* However, instruction length of 0 is allowed only when its CPU feature is present. */
                    if (   pVmcs->u32EntryInstrLen != 0
                        || IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fVmxEntryInjectSoftInt)
                    { /* likely */ }
                    else
                        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_EntryInstrLenZero);
                }
            }
        }

        /* VM-entry MSR-load count and VM-entry MSR-load area address. */
        if (pVmcs->u32EntryMsrLoadCount)
        {
            if (   !(pVmcs->u64AddrEntryMsrLoad.u & VMX_AUTOMSR_OFFSET_MASK)
                && !(pVmcs->u64AddrEntryMsrLoad.u >> IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cVmxMaxPhysAddrWidth)
                &&  PGMPhysIsGCPhysNormal(pVCpu->CTX_SUFF(pVM), pVmcs->u64AddrEntryMsrLoad.u))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_AddrEntryMsrLoad);
        }

        Assert(!(pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_ENTRY_TO_SMM));           /* We don't support SMM yet. */
        Assert(!(pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_DEACTIVATE_DUAL_MON));    /* We don't support dual-monitor treatment yet. */
    }

    NOREF(pszInstr);
    NOREF(pszFailure);
    return VINF_SUCCESS;
}


/**
 * Loads the guest control registers, debug register and some MSRs as part of
 * VM-entry.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static void iemVmxVmentryLoadGuestControlRegsMsrs(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    /*
     * Load guest control registers, debug registers and MSRs.
     * See Intel spec. 26.3.2.1 "Loading Guest Control Registers, Debug Registers and MSRs".
     */
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;

    IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR0);
    uint64_t const uGstCr0 = (pVmcs->u64GuestCr0.u   & ~VMX_ENTRY_GUEST_CR0_IGNORE_MASK)
                           | (pVCpu->cpum.GstCtx.cr0 &  VMX_ENTRY_GUEST_CR0_IGNORE_MASK);
    pVCpu->cpum.GstCtx.cr0 = uGstCr0;
    pVCpu->cpum.GstCtx.cr4 = pVmcs->u64GuestCr4.u;
    pVCpu->cpum.GstCtx.cr3 = pVmcs->u64GuestCr3.u;

    if (pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_LOAD_DEBUG)
        pVCpu->cpum.GstCtx.dr[7] = (pVmcs->u64GuestDr7.u & ~VMX_ENTRY_GUEST_DR7_MBZ_MASK) | VMX_ENTRY_GUEST_DR7_MB1_MASK;

    pVCpu->cpum.GstCtx.SysEnter.eip = pVmcs->u64GuestSysenterEip.s.Lo;
    pVCpu->cpum.GstCtx.SysEnter.esp = pVmcs->u64GuestSysenterEsp.s.Lo;
    pVCpu->cpum.GstCtx.SysEnter.cs  = pVmcs->u32GuestSysenterCS;

    if (IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fLongMode)
    {
        /* FS base and GS base are loaded  while loading the rest of the guest segment registers. */

        /* EFER MSR. */
        if (!(pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_LOAD_EFER_MSR))
        {
            IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_EFER);
            uint64_t const uHostEfer      = pVCpu->cpum.GstCtx.msrEFER;
            bool const     fGstInLongMode = RT_BOOL(pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_IA32E_MODE_GUEST);
            bool const     fGstPaging     = RT_BOOL(uGstCr0 & X86_CR0_PG);
            if (fGstInLongMode)
            {
                /* If the nested-guest is in long mode, LMA and LME are both set. */
                Assert(fGstPaging);
                pVCpu->cpum.GstCtx.msrEFER = uHostEfer | (MSR_K6_EFER_LMA | MSR_K6_EFER_LME);
            }
            else
            {
                /*
                 * If the nested-guest is outside long mode:
                 *   - With paging:    LMA is cleared, LME is cleared.
                 *   - Without paging: LMA is cleared, LME is left unmodified.
                 */
                uint64_t const fLmaLmeMask = MSR_K6_EFER_LMA | (fGstPaging ? MSR_K6_EFER_LME : 0);
                pVCpu->cpum.GstCtx.msrEFER = uHostEfer & ~fLmaLmeMask;
            }
        }
        /* else: see below. */
    }

    /* PAT MSR. */
    if (pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_LOAD_PAT_MSR)
        pVCpu->cpum.GstCtx.msrPAT = pVmcs->u64GuestPatMsr.u;

    /* EFER MSR. */
    if (pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_LOAD_EFER_MSR)
        pVCpu->cpum.GstCtx.msrEFER = pVmcs->u64GuestEferMsr.u;

    /* We don't support IA32_PERF_GLOBAL_CTRL MSR yet. */
    Assert(!(pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_LOAD_PERF_MSR));

    /* We don't support IA32_BNDCFGS MSR yet. */
    Assert(!(pVmcs->u32EntryCtls & VMX_ENTRY_CTLS_LOAD_BNDCFGS_MSR));

    /* Nothing to do for SMBASE register - We don't support SMM yet. */
}


/**
 * Loads the guest segment registers, GDTR, IDTR, LDTR and TR as part of VM-entry.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static void iemVmxVmentryLoadGuestSegRegs(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    /*
     * Load guest segment registers, GDTR, IDTR, LDTR and TR.
     * See Intel spec. 26.3.2.2 "Loading Guest Segment Registers and Descriptor-Table Registers".
     */
    /* CS, SS, ES, DS, FS, GS. */
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    for (unsigned iSegReg = 0; iSegReg < X86_SREG_COUNT; iSegReg++)
    {
        PCPUMSELREG pGstSelReg = &pVCpu->cpum.GstCtx.aSRegs[iSegReg];
        CPUMSELREG  VmcsSelReg;
        int rc = iemVmxVmcsGetGuestSegReg(pVmcs, iSegReg, &VmcsSelReg);
        AssertRC(rc); NOREF(rc);
        if (!(VmcsSelReg.Attr.u & X86DESCATTR_UNUSABLE))
        {
            pGstSelReg->Sel      = VmcsSelReg.Sel;
            pGstSelReg->ValidSel = VmcsSelReg.Sel;
            pGstSelReg->fFlags   = CPUMSELREG_FLAGS_VALID;
            pGstSelReg->u64Base  = VmcsSelReg.u64Base;
            pGstSelReg->u32Limit = VmcsSelReg.u32Limit;
            pGstSelReg->Attr.u   = VmcsSelReg.Attr.u;
        }
        else
        {
            pGstSelReg->Sel      = VmcsSelReg.Sel;
            pGstSelReg->ValidSel = VmcsSelReg.Sel;
            pGstSelReg->fFlags   = CPUMSELREG_FLAGS_VALID;
            switch (iSegReg)
            {
                case X86_SREG_CS:
                    pGstSelReg->u64Base  = VmcsSelReg.u64Base;
                    pGstSelReg->u32Limit = VmcsSelReg.u32Limit;
                    pGstSelReg->Attr.u   = VmcsSelReg.Attr.u;
                    break;

                case X86_SREG_SS:
                    pGstSelReg->u64Base  = VmcsSelReg.u64Base & UINT32_C(0xfffffff0);
                    pGstSelReg->u32Limit = 0;
                    pGstSelReg->Attr.u   = (VmcsSelReg.Attr.u & X86DESCATTR_DPL) | X86DESCATTR_D | X86DESCATTR_UNUSABLE;
                    break;

                case X86_SREG_ES:
                case X86_SREG_DS:
                    pGstSelReg->u64Base  = 0;
                    pGstSelReg->u32Limit = 0;
                    pGstSelReg->Attr.u   = X86DESCATTR_UNUSABLE;
                    break;

                case X86_SREG_FS:
                case X86_SREG_GS:
                    pGstSelReg->u64Base  = VmcsSelReg.u64Base;
                    pGstSelReg->u32Limit = 0;
                    pGstSelReg->Attr.u   = X86DESCATTR_UNUSABLE;
                    break;
            }
            Assert(pGstSelReg->Attr.n.u1Unusable);
        }
    }

    /* LDTR. */
    pVCpu->cpum.GstCtx.ldtr.Sel      = pVmcs->GuestLdtr;
    pVCpu->cpum.GstCtx.ldtr.ValidSel = pVmcs->GuestLdtr;
    pVCpu->cpum.GstCtx.ldtr.fFlags   = CPUMSELREG_FLAGS_VALID;
    if (!(pVmcs->u32GuestLdtrAttr & X86DESCATTR_UNUSABLE))
    {
        pVCpu->cpum.GstCtx.ldtr.u64Base  = pVmcs->u64GuestLdtrBase.u;
        pVCpu->cpum.GstCtx.ldtr.u32Limit = pVmcs->u32GuestLdtrLimit;
        pVCpu->cpum.GstCtx.ldtr.Attr.u   = pVmcs->u32GuestLdtrAttr;
    }
    else
    {
        pVCpu->cpum.GstCtx.ldtr.u64Base  = 0;
        pVCpu->cpum.GstCtx.ldtr.u32Limit = 0;
        pVCpu->cpum.GstCtx.ldtr.Attr.u   = X86DESCATTR_UNUSABLE;
    }

    /* TR. */
    Assert(!(pVmcs->u32GuestTrAttr & X86DESCATTR_UNUSABLE));
    pVCpu->cpum.GstCtx.tr.Sel      = pVmcs->GuestTr;
    pVCpu->cpum.GstCtx.tr.ValidSel = pVmcs->GuestTr;
    pVCpu->cpum.GstCtx.tr.fFlags   = CPUMSELREG_FLAGS_VALID;
    pVCpu->cpum.GstCtx.tr.u64Base  = pVmcs->u64GuestTrBase.u;
    pVCpu->cpum.GstCtx.tr.u32Limit = pVmcs->u32GuestTrLimit;
    pVCpu->cpum.GstCtx.tr.Attr.u   = pVmcs->u32GuestTrAttr;

    /* GDTR. */
    pVCpu->cpum.GstCtx.gdtr.cbGdt = pVmcs->u32GuestGdtrLimit;
    pVCpu->cpum.GstCtx.gdtr.pGdt  = pVmcs->u64GuestGdtrBase.u;

    /* IDTR. */
    pVCpu->cpum.GstCtx.idtr.cbIdt = pVmcs->u32GuestIdtrLimit;
    pVCpu->cpum.GstCtx.idtr.pIdt  = pVmcs->u64GuestIdtrBase.u;
}


/**
 * Loads the guest MSRs from the VM-entry MSR-load area as part of VM-entry.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pszInstr    The VMX instruction name (for logging purposes).
 */
static int iemVmxVmentryLoadGuestAutoMsrs(PVMCPUCC pVCpu, const char *pszInstr) RT_NOEXCEPT
{
    /*
     * Load guest MSRs.
     * See Intel spec. 26.4 "Loading MSRs".
     */
    PVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    const char *const pszFailure = "VM-exit";

    /*
     * The VM-entry MSR-load area address need not be a valid guest-physical address if the
     * VM-entry MSR load count is 0. If this is the case, bail early without reading it.
     * See Intel spec. 24.8.2 "VM-Entry Controls for MSRs".
     */
    uint32_t const cMsrs = RT_MIN(pVmcs->u32EntryMsrLoadCount, RT_ELEMENTS(pVCpu->cpum.GstCtx.hwvirt.vmx.aEntryMsrLoadArea));
    if (!cMsrs)
        return VINF_SUCCESS;

    /*
     * Verify the MSR auto-load count. Physical CPUs can behave unpredictably if the count is
     * exceeded including possibly raising #MC exceptions during VMX transition. Our
     * implementation shall fail VM-entry with an VMX_EXIT_ERR_MSR_LOAD VM-exit.
     */
    bool const fIsMsrCountValid = iemVmxIsAutoMsrCountValid(pVCpu, cMsrs);
    if (fIsMsrCountValid)
    { /* likely */ }
    else
    {
        iemVmxVmcsSetExitQual(pVCpu, VMX_V_AUTOMSR_AREA_SIZE / sizeof(VMXAUTOMSR));
        IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_MsrLoadCount);
    }

    RTGCPHYS const GCPhysVmEntryMsrLoadArea = pVmcs->u64AddrEntryMsrLoad.u;
    int rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &pVCpu->cpum.GstCtx.hwvirt.vmx.aEntryMsrLoadArea[0],
                                     GCPhysVmEntryMsrLoadArea, cMsrs * sizeof(VMXAUTOMSR));
    if (RT_SUCCESS(rc))
    {
        PCVMXAUTOMSR pMsr = &pVCpu->cpum.GstCtx.hwvirt.vmx.aEntryMsrLoadArea[0];
        for (uint32_t idxMsr = 0; idxMsr < cMsrs; idxMsr++, pMsr++)
        {
            if (   !pMsr->u32Reserved
                &&  pMsr->u32Msr != MSR_K8_FS_BASE
                &&  pMsr->u32Msr != MSR_K8_GS_BASE
                &&  pMsr->u32Msr != MSR_K6_EFER
                &&  pMsr->u32Msr != MSR_IA32_SMM_MONITOR_CTL
                &&  pMsr->u32Msr >> 8 != MSR_IA32_X2APIC_START >> 8)
            {
                VBOXSTRICTRC rcStrict = CPUMSetGuestMsr(pVCpu, pMsr->u32Msr, pMsr->u64Value);
                if (rcStrict == VINF_SUCCESS)
                    continue;

                /*
                 * If we're in ring-0, we cannot handle returns to ring-3 at this point and continue VM-entry.
                 * If any nested hypervisor loads MSRs that require ring-3 handling, we cause a VM-entry failure
                 * recording the MSR index in the Exit qualification (as per the Intel spec.) and indicated
                 * further by our own, specific diagnostic code. Later, we can try implement handling of the
                 * MSR in ring-0 if possible, or come up with a better, generic solution.
                 */
                iemVmxVmcsSetExitQual(pVCpu, idxMsr);
                VMXVDIAG const enmDiag = rcStrict == VINF_CPUM_R3_MSR_WRITE
                                       ? kVmxVDiag_Vmentry_MsrLoadRing3
                                       : kVmxVDiag_Vmentry_MsrLoad;
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, enmDiag);
            }
            else
            {
                iemVmxVmcsSetExitQual(pVCpu, idxMsr);
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_MsrLoadRsvd);
            }
        }
    }
    else
    {
        AssertMsgFailed(("%s: Failed to read MSR auto-load area at %#RGp, rc=%Rrc\n", pszInstr, GCPhysVmEntryMsrLoadArea, rc));
        IEM_VMX_VMENTRY_FAILED_RET_2(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_MsrLoadPtrReadPhys, rc);
    }

    NOREF(pszInstr);
    NOREF(pszFailure);
    return VINF_SUCCESS;
}


/**
 * Loads the guest-state non-register state as part of VM-entry.
 *
 * @returns VBox status code.
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   pszInstr    The VMX instruction name (for logging purposes).
 *
 * @remarks This must be called only after loading the nested-guest register state
 *          (especially nested-guest RIP).
 */
static int iemVmxVmentryLoadGuestNonRegState(PVMCPUCC pVCpu, const char *pszInstr) RT_NOEXCEPT
{
    /*
     * Load guest non-register state.
     * See Intel spec. 26.6 "Special Features of VM Entry"
     */
    const char *const pszFailure  = "VM-exit";
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;

    /*
     * If VM-entry is not vectoring, block-by-STI and block-by-MovSS state must be loaded.
     * If VM-entry is vectoring, there is no block-by-STI or block-by-MovSS.
     *
     * See Intel spec. 26.6.1 "Interruptibility State".
     */
    bool const fEntryVectoring = VMXIsVmentryVectoring(pVmcs->u32EntryIntInfo, NULL /* puEntryIntInfoType */);
    if (   !fEntryVectoring
        && (pVmcs->u32GuestIntrState & (VMX_VMCS_GUEST_INT_STATE_BLOCK_STI | VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS)))
        CPUMSetInInterruptShadowEx(&pVCpu->cpum.GstCtx, pVmcs->u64GuestRip.u);
    else
        Assert(!CPUMIsInInterruptShadow(&pVCpu->cpum.GstCtx));

    /* NMI blocking. */
    if (pVmcs->u32GuestIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_NMI)
    {
        if (pVmcs->u32PinCtls & VMX_PIN_CTLS_VIRT_NMI)
        {
            pVCpu->cpum.GstCtx.hwvirt.vmx.fVirtNmiBlocking = true;
            Assert(!CPUMAreInterruptsInhibitedByNmi(&pVCpu->cpum.GstCtx));
        }
        else
        {
            pVCpu->cpum.GstCtx.hwvirt.vmx.fVirtNmiBlocking = false;
            CPUMSetInterruptInhibitingByNmi(&pVCpu->cpum.GstCtx);
        }
    }
    else
    {
        pVCpu->cpum.GstCtx.hwvirt.vmx.fVirtNmiBlocking = false;
        Assert(!CPUMAreInterruptsInhibitedByNmi(&pVCpu->cpum.GstCtx));
    }

    /* SMI blocking is irrelevant. We don't support SMIs yet. */

    /*
     * Set PGM's copy of the EPT pointer.
     * The EPTP has already been validated while checking guest state.
     *
     * It is important to do this prior to mapping PAE PDPTEs (below).
     */
    if (pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_EPT)
        PGMSetGuestEptPtr(pVCpu, pVmcs->u64EptPtr.u);

    /*
     * Load the guest's PAE PDPTEs.
     */
    if (!iemVmxVmcsIsGuestPaePagingEnabled(pVmcs))
    {
        /*
         * When PAE paging is not used we clear the PAE PDPTEs for safety
         * in case we might be switching from a PAE host to a non-PAE guest.
         */
        pVCpu->cpum.GstCtx.aPaePdpes[0].u = 0;
        pVCpu->cpum.GstCtx.aPaePdpes[1].u = 0;
        pVCpu->cpum.GstCtx.aPaePdpes[2].u = 0;
        pVCpu->cpum.GstCtx.aPaePdpes[3].u = 0;
    }
    else if (pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_EPT)
    {
        /*
         * With EPT and the nested-guest using PAE paging, we've already validated the PAE PDPTEs
         * while checking the guest state. We can load them into the nested-guest CPU state now.
         * They'll later be used while mapping CR3 and the PAE PDPTEs.
         */
        pVCpu->cpum.GstCtx.aPaePdpes[0].u = pVmcs->u64GuestPdpte0.u;
        pVCpu->cpum.GstCtx.aPaePdpes[1].u = pVmcs->u64GuestPdpte1.u;
        pVCpu->cpum.GstCtx.aPaePdpes[2].u = pVmcs->u64GuestPdpte2.u;
        pVCpu->cpum.GstCtx.aPaePdpes[3].u = pVmcs->u64GuestPdpte3.u;
    }
    else
    {
        /*
         * Without EPT and the nested-guest using PAE paging, we must load the PAE PDPTEs
         * referenced by CR3. This involves loading (and mapping) CR3 and validating them now.
         */
        int const rc = PGMGstMapPaePdpesAtCr3(pVCpu, pVmcs->u64GuestCr3.u);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
        {
            iemVmxVmcsSetExitQual(pVCpu, VMX_ENTRY_FAIL_QUAL_PDPTE);
            IEM_VMX_VMENTRY_FAILED_RET_2(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_GuestPdpte, rc);
        }
    }

    /* VPID is irrelevant. We don't support VPID yet. */

    /* Clear address-range monitoring. */
    EMMonitorWaitClear(pVCpu);

    return VINF_SUCCESS;
}


/**
 * Loads the guest VMCS referenced state (such as MSR bitmaps, I/O bitmaps etc).
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 * @param   pszInstr    The VMX instruction name (for logging purposes).
 *
 * @remarks This assumes various VMCS related data structure pointers have already
 *          been verified prior to calling this function.
 */
static int iemVmxVmentryLoadGuestVmcsRefState(PVMCPUCC pVCpu, const char *pszInstr) RT_NOEXCEPT
{
    const char *const pszFailure  = "VM-exit";
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;

    /*
     * Virtualize APIC accesses.
     */
    if (pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_APIC_ACCESS)
    {
        /* APIC-access physical address. */
        RTGCPHYS const GCPhysApicAccess = pVmcs->u64AddrApicAccess.u;

        /*
         * Register the handler for the APIC-access page.
         *
         * We don't deregister the APIC-access page handler during the VM-exit as a different
         * nested-VCPU might be using the same guest-physical address for its APIC-access page.
         *
         * We leave the page registered until the first access that happens outside VMX non-root
         * mode. Guest software is allowed to access structures such as the APIC-access page
         * only when no logical processor with a current VMCS references it in VMX non-root mode,
         * otherwise it can lead to unpredictable behavior including guest triple-faults.
         *
         * See Intel spec. 24.11.4 "Software Access to Related Structures".
         */
        /** @todo r=bird: The lazy deregistration of the page is potentially slightly
         *        problematic, as the guest may cause us to create lots of access
         *        handler entries.  However, any slowdown or similar effects should
         *        only ever affect the guest itself, so not a big issue.  Though, I
         *        wish there was most recently used approach or something to tracking
         *        these... */
        PVMCC pVM = pVCpu->CTX_SUFF(pVM);
        int rc = PGMHandlerPhysicalRegisterVmxApicAccessPage(pVM, GCPhysApicAccess, pVM->iem.s.hVmxApicAccessPage);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET_2(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_AddrApicAccessHandlerReg, rc);
    }

    /*
     * VMCS shadowing.
     */
    if (pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VMCS_SHADOWING)
    {
        /* Read the VMREAD-bitmap. */
        RTGCPHYS const GCPhysVmreadBitmap = pVmcs->u64AddrVmreadBitmap.u;
        int rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &pVCpu->cpum.GstCtx.hwvirt.vmx.abVmreadBitmap[0],
                                         GCPhysVmreadBitmap, sizeof(pVCpu->cpum.GstCtx.hwvirt.vmx.abVmreadBitmap));
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET_2(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_VmreadBitmapPtrReadPhys, rc);

        /* Read the VMWRITE-bitmap. */
        RTGCPHYS const GCPhysVmwriteBitmap = pVmcs->u64AddrVmwriteBitmap.u;
        rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &pVCpu->cpum.GstCtx.hwvirt.vmx.abVmwriteBitmap[0],
                                     GCPhysVmwriteBitmap, sizeof(pVCpu->cpum.GstCtx.hwvirt.vmx.abVmwriteBitmap));
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET_2(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_VmwriteBitmapPtrReadPhys, rc);
    }

    /*
     * I/O bitmaps.
     */
    if (pVmcs->u32ProcCtls & VMX_PROC_CTLS_USE_IO_BITMAPS)
    {
        /* Read the IO bitmap A. */
        RTGCPHYS const GCPhysIoBitmapA = pVmcs->u64AddrIoBitmapA.u;
        int rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &pVCpu->cpum.GstCtx.hwvirt.vmx.abIoBitmap[0],
                                         GCPhysIoBitmapA, VMX_V_IO_BITMAP_A_SIZE);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET_2(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_IoBitmapAPtrReadPhys, rc);

        /* Read the IO bitmap B. */
        RTGCPHYS const GCPhysIoBitmapB = pVmcs->u64AddrIoBitmapB.u;
        rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &pVCpu->cpum.GstCtx.hwvirt.vmx.abIoBitmap[VMX_V_IO_BITMAP_A_SIZE],
                                     GCPhysIoBitmapB, VMX_V_IO_BITMAP_B_SIZE);
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET_2(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_IoBitmapBPtrReadPhys, rc);
    }

    /*
     * TPR shadow and Virtual-APIC page.
     */
    if (pVmcs->u32ProcCtls & VMX_PROC_CTLS_USE_TPR_SHADOW)
    {
        /* Verify TPR threshold and VTPR when both virtualize-APIC accesses and virtual-interrupt delivery aren't used. */
        if (   !(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_APIC_ACCESS)
            && !(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VIRT_INT_DELIVERY))
        {
            /* Read the VTPR from the virtual-APIC page. */
            RTGCPHYS const GCPhysVirtApic = pVmcs->u64AddrVirtApic.u;
            uint8_t u8VTpr;
            int rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &u8VTpr, GCPhysVirtApic + XAPIC_OFF_TPR, sizeof(u8VTpr));
            if (RT_SUCCESS(rc))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET_2(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_VirtApicPagePtrReadPhys, rc);

            /* Bits 3:0 of the TPR-threshold must not be greater than bits 7:4 of VTPR. */
            if ((uint8_t)RT_BF_GET(pVmcs->u32TprThreshold, VMX_BF_TPR_THRESHOLD_TPR) <= (u8VTpr & 0xf0))
            { /* likely */ }
            else
                IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_TprThresholdVTpr);
        }
    }

    /*
     * VMCS link pointer.
     */
    if (pVmcs->u64VmcsLinkPtr.u != UINT64_C(0xffffffffffffffff))
    {
        /* Read the VMCS-link pointer from guest memory. */
        RTGCPHYS const GCPhysShadowVmcs = pVmcs->u64VmcsLinkPtr.u;
        int rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &pVCpu->cpum.GstCtx.hwvirt.vmx.ShadowVmcs,
                                         GCPhysShadowVmcs, sizeof(pVCpu->cpum.GstCtx.hwvirt.vmx.ShadowVmcs));
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
        {
            iemVmxVmcsSetExitQual(pVCpu, VMX_ENTRY_FAIL_QUAL_VMCS_LINK_PTR);
            IEM_VMX_VMENTRY_FAILED_RET_2(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_VmcsLinkPtrReadPhys, rc);
        }

        /* Verify the VMCS revision specified by the guest matches what we reported to the guest. */
        if (pVCpu->cpum.GstCtx.hwvirt.vmx.ShadowVmcs.u32VmcsRevId.n.u31RevisionId == VMX_V_VMCS_REVISION_ID)
        { /* likely */ }
        else
        {
            iemVmxVmcsSetExitQual(pVCpu, VMX_ENTRY_FAIL_QUAL_VMCS_LINK_PTR);
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_VmcsLinkPtrRevId);
        }

        /* Verify the shadow bit is set if VMCS shadowing is enabled . */
        if (   !(pVmcs->u32ProcCtls2 & VMX_PROC_CTLS2_VMCS_SHADOWING)
            || pVCpu->cpum.GstCtx.hwvirt.vmx.ShadowVmcs.u32VmcsRevId.n.fIsShadowVmcs)
        { /* likely */ }
        else
        {
            iemVmxVmcsSetExitQual(pVCpu, VMX_ENTRY_FAIL_QUAL_VMCS_LINK_PTR);
            IEM_VMX_VMENTRY_FAILED_RET(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_VmcsLinkPtrShadow);
        }

        /* Update our cache of the guest physical address of the shadow VMCS. */
        pVCpu->cpum.GstCtx.hwvirt.vmx.GCPhysShadowVmcs = GCPhysShadowVmcs;
    }

    /*
     * MSR bitmap.
     */
    if (pVmcs->u32ProcCtls & VMX_PROC_CTLS_USE_MSR_BITMAPS)
    {
        /* Read the MSR bitmap. */
        RTGCPHYS const GCPhysMsrBitmap = pVmcs->u64AddrMsrBitmap.u;
        int rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &pVCpu->cpum.GstCtx.hwvirt.vmx.abMsrBitmap[0],
                                         GCPhysMsrBitmap, sizeof(pVCpu->cpum.GstCtx.hwvirt.vmx.abMsrBitmap));
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
            IEM_VMX_VMENTRY_FAILED_RET_2(pVCpu, pszInstr, pszFailure, kVmxVDiag_Vmentry_MsrBitmapPtrReadPhys, rc);
    }

    NOREF(pszFailure);
    NOREF(pszInstr);
    return VINF_SUCCESS;
}


/**
 * Loads the guest-state as part of VM-entry.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pszInstr    The VMX instruction name (for logging purposes).
 *
 * @remarks This must be done after all the necessary steps prior to loading of
 *          guest-state (e.g. checking various VMCS state).
 */
static int iemVmxVmentryLoadGuestState(PVMCPUCC pVCpu, const char *pszInstr) RT_NOEXCEPT
{
    /* Load guest control registers, MSRs (that are directly part of the VMCS). */
    iemVmxVmentryLoadGuestControlRegsMsrs(pVCpu);

    /* Load guest segment registers. */
    iemVmxVmentryLoadGuestSegRegs(pVCpu);

    /*
     * Load guest RIP, RSP and RFLAGS.
     * See Intel spec. 26.3.2.3 "Loading Guest RIP, RSP and RFLAGS".
     */
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    pVCpu->cpum.GstCtx.rsp      = pVmcs->u64GuestRsp.u;
    pVCpu->cpum.GstCtx.rip      = pVmcs->u64GuestRip.u;
    pVCpu->cpum.GstCtx.rflags.u = pVmcs->u64GuestRFlags.u;

    /* Initialize the PAUSE-loop controls as part of VM-entry. */
    pVCpu->cpum.GstCtx.hwvirt.vmx.uFirstPauseLoopTick = 0;
    pVCpu->cpum.GstCtx.hwvirt.vmx.uPrevPauseTick      = 0;

    /* Load guest non-register state (such as interrupt shadows, NMI blocking etc). */
    int rc = iemVmxVmentryLoadGuestNonRegState(pVCpu, pszInstr);
    if (rc == VINF_SUCCESS)
    { /* likely */ }
    else
        return rc;

    /* Load VMX related structures and state referenced by the VMCS. */
    rc = iemVmxVmentryLoadGuestVmcsRefState(pVCpu, pszInstr);
    if (rc == VINF_SUCCESS)
    { /* likely */ }
    else
        return rc;

    NOREF(pszInstr);
    return VINF_SUCCESS;
}


/**
 * Returns whether there are is a pending debug exception on VM-entry.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pszInstr    The VMX instruction name (for logging purposes).
 */
static bool iemVmxVmentryIsPendingDebugXcpt(PVMCPUCC pVCpu, const char *pszInstr) RT_NOEXCEPT
{
    /*
     * Pending debug exceptions.
     * See Intel spec. 26.6.3 "Delivery of Pending Debug Exceptions after VM Entry".
     */
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    Assert(pVmcs);

    bool fPendingDbgXcpt = RT_BOOL(pVmcs->u64GuestPendingDbgXcpts.u & (  VMX_VMCS_GUEST_PENDING_DEBUG_XCPT_BS
                                                                       | VMX_VMCS_GUEST_PENDING_DEBUG_XCPT_EN_BP));
    if (fPendingDbgXcpt)
    {
        uint8_t uEntryIntInfoType;
        bool const fEntryVectoring = VMXIsVmentryVectoring(pVmcs->u32EntryIntInfo, &uEntryIntInfoType);
        if (fEntryVectoring)
        {
            switch (uEntryIntInfoType)
            {
                case VMX_ENTRY_INT_INFO_TYPE_EXT_INT:
                case VMX_ENTRY_INT_INFO_TYPE_NMI:
                case VMX_ENTRY_INT_INFO_TYPE_HW_XCPT:
                case VMX_ENTRY_INT_INFO_TYPE_PRIV_SW_XCPT:
                    fPendingDbgXcpt = false;
                    break;

                case VMX_ENTRY_INT_INFO_TYPE_SW_XCPT:
                {
                    /*
                     * Whether the pending debug exception for software exceptions other than
                     * #BP and #OF is delivered after injecting the exception or is discard
                     * is CPU implementation specific. We will discard them (easier).
                     */
                    uint8_t const uVector = VMX_ENTRY_INT_INFO_VECTOR(pVmcs->u32EntryIntInfo);
                    if (   uVector != X86_XCPT_BP
                        && uVector != X86_XCPT_OF)
                        fPendingDbgXcpt = false;
                    RT_FALL_THRU();
                }
                case VMX_ENTRY_INT_INFO_TYPE_SW_INT:
                {
                    if (!(pVmcs->u32GuestIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS))
                        fPendingDbgXcpt = false;
                    break;
                }
            }
        }
        else
        {
            /*
             * When the VM-entry is not vectoring but there is blocking-by-MovSS, whether the
             * pending debug exception is held pending or is discarded is CPU implementation
             * specific. We will discard them (easier).
             */
            if (pVmcs->u32GuestIntrState & VMX_VMCS_GUEST_INT_STATE_BLOCK_MOVSS)
                fPendingDbgXcpt = false;

            /* There's no pending debug exception in the shutdown or wait-for-SIPI state. */
            if (pVmcs->u32GuestActivityState & (VMX_VMCS_GUEST_ACTIVITY_SHUTDOWN | VMX_VMCS_GUEST_ACTIVITY_SIPI_WAIT))
                fPendingDbgXcpt = false;
        }
    }

    NOREF(pszInstr);
    return fPendingDbgXcpt;
}


/**
 * Set up the monitor-trap flag (MTF).
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pszInstr    The VMX instruction name (for logging purposes).
 */
static void iemVmxVmentrySetupMtf(PVMCPUCC pVCpu, const char *pszInstr) RT_NOEXCEPT
{
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    Assert(pVmcs);
    if (pVmcs->u32ProcCtls & VMX_PROC_CTLS_MONITOR_TRAP_FLAG)
    {
        VMCPU_FF_SET(pVCpu, VMCPU_FF_VMX_MTF);
        Log(("%s: Monitor-trap flag set on VM-entry\n", pszInstr));
    }
    else
        Assert(!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_MTF));
    NOREF(pszInstr);
}


/**
 * Sets up NMI-window exiting.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pszInstr    The VMX instruction name (for logging purposes).
 */
static void iemVmxVmentrySetupNmiWindow(PVMCPUCC pVCpu, const char *pszInstr) RT_NOEXCEPT
{
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    Assert(pVmcs);
    if (pVmcs->u32ProcCtls & VMX_PROC_CTLS_NMI_WINDOW_EXIT)
    {
        Assert(pVmcs->u32PinCtls & VMX_PIN_CTLS_VIRT_NMI);
        VMCPU_FF_SET(pVCpu, VMCPU_FF_VMX_NMI_WINDOW);
        Log(("%s: NMI-window set on VM-entry\n", pszInstr));
    }
    else
        Assert(!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_NMI_WINDOW));
    NOREF(pszInstr);
}


/**
 * Sets up interrupt-window exiting.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pszInstr    The VMX instruction name (for logging purposes).
 */
static void iemVmxVmentrySetupIntWindow(PVMCPUCC pVCpu, const char *pszInstr) RT_NOEXCEPT
{
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    Assert(pVmcs);
    if (pVmcs->u32ProcCtls & VMX_PROC_CTLS_INT_WINDOW_EXIT)
    {
        VMCPU_FF_SET(pVCpu, VMCPU_FF_VMX_INT_WINDOW);
        Log(("%s: Interrupt-window set on VM-entry\n", pszInstr));
    }
    else
        Assert(!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_INT_WINDOW));
    NOREF(pszInstr);
}


/**
 * Set up the VMX-preemption timer.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pszInstr    The VMX instruction name (for logging purposes).
 */
static void iemVmxVmentrySetupPreemptTimer(PVMCPUCC pVCpu, const char *pszInstr) RT_NOEXCEPT
{
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    Assert(pVmcs);
    if (pVmcs->u32PinCtls & VMX_PIN_CTLS_PREEMPT_TIMER)
    {
        /*
         * If the timer is 0, we must cause a VM-exit before executing the first
         * nested-guest instruction. So we can flag as though the timer has already
         * expired and we will check and cause a VM-exit at the right priority elsewhere
         * in the code.
         */
        uint64_t uEntryTick;
        uint32_t const uPreemptTimer = pVmcs->u32PreemptTimer;
        if (uPreemptTimer)
        {
            int rc = CPUMStartGuestVmxPremptTimer(pVCpu, uPreemptTimer, VMX_V_PREEMPT_TIMER_SHIFT, &uEntryTick);
            AssertRC(rc);
            Log(("%s: VM-entry set up VMX-preemption timer at %#RX64\n", pszInstr, uEntryTick));
        }
        else
        {
            uEntryTick = TMCpuTickGetNoCheck(pVCpu);
            VMCPU_FF_SET(pVCpu, VMCPU_FF_VMX_PREEMPT_TIMER);
            Log(("%s: VM-entry set up VMX-preemption timer at %#RX64 to expire immediately!\n", pszInstr, uEntryTick));
        }

        pVCpu->cpum.GstCtx.hwvirt.vmx.uEntryTick = uEntryTick;
    }
    else
        Assert(!VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_VMX_PREEMPT_TIMER));

    NOREF(pszInstr);
}


/**
 * Injects an event using TRPM given a VM-entry interruption info and related
 * fields.
 *
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   pszInstr            The VMX instruction name (for logging purposes).
 * @param   uEntryIntInfo       The VM-entry interruption info.
 * @param   uErrCode            The error code associated with the event if any.
 * @param   cbInstr             The VM-entry instruction length (for software
 *                              interrupts and software exceptions). Pass 0
 *                              otherwise.
 * @param   GCPtrFaultAddress   The guest CR2 if this is a \#PF event.
 */
static void iemVmxVmentryInjectTrpmEvent(PVMCPUCC pVCpu, const char *pszInstr, uint32_t uEntryIntInfo, uint32_t uErrCode,
                                         uint32_t cbInstr, RTGCUINTPTR GCPtrFaultAddress) RT_NOEXCEPT
{
    Assert(VMX_ENTRY_INT_INFO_IS_VALID(uEntryIntInfo));

    uint8_t const   uType        = VMX_ENTRY_INT_INFO_TYPE(uEntryIntInfo);
    uint8_t const   uVector      = VMX_ENTRY_INT_INFO_VECTOR(uEntryIntInfo);
    TRPMEVENT const enmTrpmEvent = HMVmxEventTypeToTrpmEventType(uEntryIntInfo);

    Assert(uType != VMX_ENTRY_INT_INFO_TYPE_OTHER_EVENT);

    int rc = TRPMAssertTrap(pVCpu, uVector, enmTrpmEvent);
    AssertRC(rc);
    Log(("%s: Injecting: vector=%#x type=%#x (%s)\n", pszInstr, uVector, uType, VMXGetEntryIntInfoTypeDesc(uType)));

    if (VMX_ENTRY_INT_INFO_IS_ERROR_CODE_VALID(uEntryIntInfo))
    {
        TRPMSetErrorCode(pVCpu, uErrCode);
        Log(("%s: Injecting: err_code=%#x\n", pszInstr, uErrCode));
    }

    if (VMX_ENTRY_INT_INFO_IS_XCPT_PF(uEntryIntInfo))
    {
        TRPMSetFaultAddress(pVCpu, GCPtrFaultAddress);
        Log(("%s: Injecting: fault_addr=%RGp\n", pszInstr, GCPtrFaultAddress));
    }
    else
    {
        switch (uType)
        {
            case VMX_ENTRY_INT_INFO_TYPE_PRIV_SW_XCPT:
                TRPMSetTrapDueToIcebp(pVCpu);
                Log(("%s: Injecting: icebp\n", pszInstr));
                RT_FALL_THRU();
            case VMX_ENTRY_INT_INFO_TYPE_SW_INT:
            case VMX_ENTRY_INT_INFO_TYPE_SW_XCPT:
                TRPMSetInstrLength(pVCpu, cbInstr);
                Log(("%s: Injecting: instr_len=%u\n", pszInstr, cbInstr));
                break;
        }
    }

    NOREF(pszInstr);
}


/**
 * Performs event injection (if any) as part of VM-entry.
 *
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   pszInstr    The VMX instruction name (for logging purposes).
 */
static void iemVmxVmentryInjectEvent(PVMCPUCC pVCpu, const char *pszInstr) RT_NOEXCEPT
{
    PVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;

    /*
     * Inject events.
     * The event that is going to be made pending for injection is not subject to VMX intercepts,
     * thus we flag ignoring of intercepts. However, recursive exceptions if any during delivery
     * of the current event -are- subject to intercepts, hence this flag will be flipped during
     * the actually delivery of this event.
     *
     * See Intel spec. 26.5 "Event Injection".
     */
    uint32_t const uEntryIntInfo      = pVmcs->u32EntryIntInfo;
    bool const     fEntryIntInfoValid = VMX_ENTRY_INT_INFO_IS_VALID(uEntryIntInfo);

    CPUMSetGuestVmxInterceptEvents(&pVCpu->cpum.GstCtx, !fEntryIntInfoValid);
    if (fEntryIntInfoValid)
    {
        if (VMX_ENTRY_INT_INFO_TYPE(uEntryIntInfo) != VMX_ENTRY_INT_INFO_TYPE_OTHER_EVENT)
            iemVmxVmentryInjectTrpmEvent(pVCpu, pszInstr, uEntryIntInfo, pVmcs->u32EntryXcptErrCode, pVmcs->u32EntryInstrLen,
                                         pVCpu->cpum.GstCtx.cr2);
        else
        {
            Assert(VMX_ENTRY_INT_INFO_VECTOR(uEntryIntInfo) == VMX_ENTRY_INT_INFO_VECTOR_MTF);
            VMCPU_FF_SET(pVCpu, VMCPU_FF_VMX_MTF);
        }

        /*
         * We need to clear the VM-entry interruption information field's valid bit on VM-exit.
         *
         * However, we do it here on VM-entry as well because while it isn't visible to guest
         * software until VM-exit, when and if HM looks at the VMCS to continue nested-guest
         * execution using hardware-assisted VMX, it will not try to inject the event again.
         *
         * See Intel spec. 24.8.3 "VM-Entry Controls for Event Injection".
         */
        pVmcs->u32EntryIntInfo &= ~VMX_ENTRY_INT_INFO_VALID;
    }
    else
    {
        /*
         * Inject any pending guest debug exception.
         * Unlike injecting events, this #DB injection on VM-entry is subject to #DB VMX intercept.
         * See Intel spec. 26.6.3 "Delivery of Pending Debug Exceptions after VM Entry".
         */
        bool const fPendingDbgXcpt = iemVmxVmentryIsPendingDebugXcpt(pVCpu, pszInstr);
        if (fPendingDbgXcpt)
        {
            uint32_t const uDbgXcptInfo = RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VECTOR, X86_XCPT_DB)
                                        | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_TYPE,   VMX_ENTRY_INT_INFO_TYPE_HW_XCPT)
                                        | RT_BF_MAKE(VMX_BF_ENTRY_INT_INFO_VALID,  1);
            iemVmxVmentryInjectTrpmEvent(pVCpu, pszInstr, uDbgXcptInfo, 0 /* uErrCode */, pVmcs->u32EntryInstrLen,
                                         0 /* GCPtrFaultAddress */);
        }
    }

    NOREF(pszInstr);
}


/**
 * Initializes all read-only VMCS fields as part of VM-entry.
 *
 * @param   pVCpu   The cross context virtual CPU structure.
 */
static void iemVmxVmentryInitReadOnlyFields(PVMCPUCC pVCpu) RT_NOEXCEPT
{
    /*
     * Any VMCS field which we do not establish on every VM-exit but may potentially
     * be used on the VM-exit path of a nested hypervisor -and- is not explicitly
     * specified to be undefined, needs to be initialized here.
     *
     * Thus, it is especially important to clear the Exit qualification field
     * since it must be zero for VM-exits where it is not used. Similarly, the
     * VM-exit interruption information field's valid bit needs to be cleared for
     * the same reasons.
     */
    PVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    Assert(pVmcs);

    /* 16-bit (none currently). */
    /* 32-bit. */
    pVmcs->u32RoVmInstrError        = 0;
    pVmcs->u32RoExitReason          = 0;
    pVmcs->u32RoExitIntInfo         = 0;
    pVmcs->u32RoExitIntErrCode      = 0;
    pVmcs->u32RoIdtVectoringInfo    = 0;
    pVmcs->u32RoIdtVectoringErrCode = 0;
    pVmcs->u32RoExitInstrLen        = 0;
    pVmcs->u32RoExitInstrInfo       = 0;

    /* 64-bit. */
    pVmcs->u64RoGuestPhysAddr.u     = 0;

    /* Natural-width. */
    pVmcs->u64RoExitQual.u          = 0;
    pVmcs->u64RoIoRcx.u             = 0;
    pVmcs->u64RoIoRsi.u             = 0;
    pVmcs->u64RoIoRdi.u             = 0;
    pVmcs->u64RoIoRip.u             = 0;
    pVmcs->u64RoGuestLinearAddr.u   = 0;
}


/**
 * VMLAUNCH/VMRESUME instruction execution worker.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The instruction length in bytes.
 * @param   uInstrId    The instruction identity (VMXINSTRID_VMLAUNCH or
 *                      VMXINSTRID_VMRESUME).
 *
 * @remarks Common VMX instruction checks are already expected to by the caller,
 *          i.e. CR4.VMXE, Real/V86 mode, EFER/CS.L checks.
 */
static VBOXSTRICTRC iemVmxVmlaunchVmresume(PVMCPUCC pVCpu, uint8_t cbInstr, VMXINSTRID uInstrId) RT_NOEXCEPT
{
# if defined(VBOX_WITH_NESTED_HWVIRT_ONLY_IN_IEM) && !defined(IN_RING3)
    RT_NOREF3(pVCpu, cbInstr, uInstrId);
    return VINF_EM_RAW_EMULATE_INSTR;
# else
    Assert(   uInstrId == VMXINSTRID_VMLAUNCH
           || uInstrId == VMXINSTRID_VMRESUME);
    const char * const pszInstr = uInstrId == VMXINSTRID_VMRESUME ? "vmresume" : "vmlaunch";

    /* Nested-guest intercept. */
    if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
        return iemVmxVmexitInstr(pVCpu, uInstrId == VMXINSTRID_VMRESUME ? VMX_EXIT_VMRESUME : VMX_EXIT_VMLAUNCH, cbInstr);

    Assert(IEM_VMX_IS_ROOT_MODE(pVCpu));

    /*
     * Basic VM-entry checks.
     * The order of the CPL, current and shadow VMCS and block-by-MovSS are important.
     * The checks following that do not have to follow a specific order.
     *
     * See Intel spec. 26.1 "Basic VM-entry Checks".
     */

    /* CPL. */
    if (IEM_GET_CPL(pVCpu) == 0)
    { /* likely */ }
    else
    {
        Log(("%s: CPL %u -> #GP(0)\n", pszInstr, IEM_GET_CPL(pVCpu)));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmentry_Cpl;
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /* Current VMCS valid. */
    if (IEM_VMX_HAS_CURRENT_VMCS(pVCpu))
    { /* likely */ }
    else
    {
        Log(("%s: VMCS pointer %#RGp invalid -> VMFailInvalid\n", pszInstr, IEM_VMX_GET_CURRENT_VMCS(pVCpu)));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmentry_PtrInvalid;
        iemVmxVmFailInvalid(pVCpu);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /* Current VMCS is not a shadow VMCS. */
    PVMXVVMCS pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    if (!pVmcs->u32VmcsRevId.n.fIsShadowVmcs)
    { /* likely */ }
    else
    {
        Log(("%s: VMCS pointer %#RGp is a shadow VMCS -> VMFailInvalid\n", pszInstr, IEM_VMX_GET_CURRENT_VMCS(pVCpu)));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmentry_PtrShadowVmcs;
        iemVmxVmFailInvalid(pVCpu);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /** @todo Distinguish block-by-MovSS from block-by-STI. Currently we
     *        use block-by-STI here which is not quite correct. */
    if (!CPUMIsInInterruptShadowWithUpdate(&pVCpu->cpum.GstCtx))
    { /* likely */ }
    else
    {
        Log(("%s: VM entry with events blocked by MOV SS -> VMFail\n", pszInstr));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmentry_BlocKMovSS;
        iemVmxVmFail(pVCpu, VMXINSTRERR_VMENTRY_BLOCK_MOVSS);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    if (uInstrId == VMXINSTRID_VMLAUNCH)
    {
        /* VMLAUNCH with non-clear VMCS. */
        if (pVmcs->fVmcsState == VMX_V_VMCS_LAUNCH_STATE_CLEAR)
        { /* likely */ }
        else if (pVmcs->fVmcsState == VMX_V_VMCS_LAUNCH_STATE_CLEAR_LEGACY)
        {
            /* Convert legacy launch-state value to current value, see @bugref{10318#c114} for reasons.*/
            pVmcs->fVmcsState = VMX_V_VMCS_LAUNCH_STATE_CLEAR;
            Log(("vmlaunch: Updated legacy 'VMCLEAR' VMCS launch-state bit to current\n"));
        }
        else
        {
            Log(("vmlaunch: VMLAUNCH with non-clear VMCS %RGp -> VMFail\n", pVCpu->cpum.GstCtx.hwvirt.vmx.GCPhysVmcs));
            pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmentry_VmcsClear;
            iemVmxVmFail(pVCpu, VMXINSTRERR_VMLAUNCH_NON_CLEAR_VMCS);
            return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }
    }
    else
    {
        /* VMRESUME with non-launched VMCS. */
        if (pVmcs->fVmcsState == VMX_V_VMCS_LAUNCH_STATE_LAUNCHED)
        { /* likely */ }
        else
        {
            Log(("vmresume: VMRESUME with non-launched VMCS -> VMFail\n"));
            pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmentry_VmcsLaunch;
            iemVmxVmFail(pVCpu, VMXINSTRERR_VMRESUME_NON_LAUNCHED_VMCS);
            return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }
    }

    /*
     * We are allowed to cache VMCS related data structures (such as I/O bitmaps, MSR bitmaps)
     * while entering VMX non-root mode. We do some of this while checking VM-execution
     * controls. The nested hypervisor should not make assumptions and cannot expect
     * predictable behavior if changes to these structures are made in guest memory while
     * executing in VMX non-root mode. As far as VirtualBox is concerned, the guest cannot
     * modify them anyway as we cache them in host memory.
     *
     * See Intel spec. 24.11.4 "Software Access to Related Structures".
     */
    Assert(IEM_VMX_HAS_CURRENT_VMCS(pVCpu));

    int rc = iemVmxVmentryCheckCtls(pVCpu, pszInstr);
    if (RT_SUCCESS(rc))
    {
        rc = iemVmxVmentryCheckHostState(pVCpu, pszInstr);
        if (RT_SUCCESS(rc))
        {
            /*
             * Initialize read-only VMCS fields before VM-entry since we don't update all of them
             * for every VM-exit. This needs to be done before invoking a VM-exit (even those
             * ones that may occur during VM-entry below).
             */
            iemVmxVmentryInitReadOnlyFields(pVCpu);

            /*
             * Blocking of NMIs need to be restored if VM-entry fails due to invalid-guest state.
             * So we save the VMCPU_FF_BLOCK_NMI force-flag here so we can restore it on
             * VM-exit when required.
             * See Intel spec. 26.7 "VM-entry Failures During or After Loading Guest State"
             */
            iemVmxVmentrySaveNmiBlockingFF(pVCpu);

            rc = iemVmxVmentryCheckGuestState(pVCpu, pszInstr);
            if (RT_SUCCESS(rc))
            {
                /*
                 * We've now entered nested-guest execution.
                 *
                 * It is important do this prior to loading the guest state because
                 * as part of loading the guest state, PGM (and perhaps other components
                 * in the future) relies on detecting whether VMX non-root mode has been
                 * entered.
                 */
                pVCpu->cpum.GstCtx.hwvirt.vmx.fInVmxNonRootMode = true;

                rc = iemVmxVmentryLoadGuestState(pVCpu, pszInstr);
                if (RT_SUCCESS(rc))
                {
                    rc = iemVmxVmentryLoadGuestAutoMsrs(pVCpu, pszInstr);
                    if (RT_SUCCESS(rc))
                    {
                        Assert(rc != VINF_CPUM_R3_MSR_WRITE);

                        /* VMLAUNCH instruction must update the VMCS launch state. */
                        if (uInstrId == VMXINSTRID_VMLAUNCH)
                            pVmcs->fVmcsState = VMX_V_VMCS_LAUNCH_STATE_LAUNCHED;

                        /* Perform the VMX transition (PGM updates). */
                        VBOXSTRICTRC rcStrict = iemVmxTransition(pVCpu, cbInstr);
                        if (rcStrict == VINF_SUCCESS)
                        { /* likely */ }
                        else if (RT_SUCCESS(rcStrict))
                        {
                            Log3(("%s: iemVmxTransition returns %Rrc -> Setting passup status\n", pszInstr,
                                  VBOXSTRICTRC_VAL(rcStrict)));
                            rcStrict = iemSetPassUpStatus(pVCpu, rcStrict);
                        }
                        else
                        {
                            Log3(("%s: iemVmxTransition failed! rc=%Rrc\n", pszInstr, VBOXSTRICTRC_VAL(rcStrict)));
                            return rcStrict;
                        }

                        /* Paranoia. */
                        Assert(rcStrict == VINF_SUCCESS);

                        /*
                         * The priority of potential VM-exits during VM-entry is important.
                         * The priorities of VM-exits and events are listed from highest
                         * to lowest as follows:
                         *
                         * 1.  Event injection.
                         * 2.  Trap on task-switch (T flag set in TSS).
                         * 3.  TPR below threshold / APIC-write.
                         * 4.  SMI, INIT.
                         * 5.  MTF exit.
                         * 6.  Debug-trap exceptions (EFLAGS.TF), pending debug exceptions.
                         * 7.  VMX-preemption timer.
                         * 9.  NMI-window exit.
                         * 10. NMI injection.
                         * 11. Interrupt-window exit.
                         * 12. Virtual-interrupt injection.
                         * 13. Interrupt injection.
                         * 14. Process next instruction (fetch, decode, execute).
                         */

                        /* Setup VMX-preemption timer. */
                        iemVmxVmentrySetupPreemptTimer(pVCpu, pszInstr);

                        /* Setup monitor-trap flag. */
                        iemVmxVmentrySetupMtf(pVCpu, pszInstr);

                        /* Setup NMI-window exiting. */
                        iemVmxVmentrySetupNmiWindow(pVCpu, pszInstr);

                        /* Setup interrupt-window exiting. */
                        iemVmxVmentrySetupIntWindow(pVCpu, pszInstr);

                        /*
                         * Inject any event that the nested hypervisor wants to inject.
                         * Note! We cannot immediately perform the event injection here as we may have
                         *       pending PGM operations to perform due to switching page tables and/or
                         *       mode.
                         */
                        iemVmxVmentryInjectEvent(pVCpu, pszInstr);

# if defined(VBOX_WITH_NESTED_HWVIRT_ONLY_IN_IEM) && defined(IN_RING3)
                        /* Reschedule to IEM-only execution of the nested-guest. */
                        LogFlow(("%s: Enabling IEM-only EM execution policy!\n", pszInstr));
                        int rcSched = EMR3SetExecutionPolicy(pVCpu->CTX_SUFF(pVM)->pUVM, EMEXECPOLICY_IEM_ALL, true);
                        if (rcSched != VINF_SUCCESS)
                            iemSetPassUpStatus(pVCpu, rcSched);
# endif

                        /* Finally, done. */
                        Log2(("vmentry: %s: cs:rip=%04x:%08RX64 cr0=%#RX64 (%#RX64) cr4=%#RX64 (%#RX64) efer=%#RX64 (%#RX64)\n",
                              pszInstr, pVCpu->cpum.GstCtx.cs.Sel, pVCpu->cpum.GstCtx.rip, pVCpu->cpum.GstCtx.cr0,
                              pVmcs->u64Cr0ReadShadow.u, pVCpu->cpum.GstCtx.cr4, pVmcs->u64Cr4ReadShadow.u,
                              pVCpu->cpum.GstCtx.msrEFER, pVmcs->u64GuestEferMsr.u));
                        return VINF_SUCCESS;
                    }
                    return iemVmxVmexit(pVCpu, VMX_EXIT_ERR_MSR_LOAD | VMX_EXIT_REASON_ENTRY_FAILED, pVmcs->u64RoExitQual.u);
                }
            }
            return iemVmxVmexit(pVCpu, VMX_EXIT_ERR_INVALID_GUEST_STATE | VMX_EXIT_REASON_ENTRY_FAILED, pVmcs->u64RoExitQual.u);
        }

        iemVmxVmFail(pVCpu, VMXINSTRERR_VMENTRY_INVALID_HOST_STATE);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    iemVmxVmFail(pVCpu, VMXINSTRERR_VMENTRY_INVALID_CTLS);
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
# endif
}


/**
 * Interface for HM and EM to emulate the VMLAUNCH/VMRESUME instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   cbInstr         The instruction length in bytes.
 * @param   uInstrId        The instruction ID (VMXINSTRID_VMLAUNCH or
 *                          VMXINSTRID_VMRESUME).
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedVmlaunchVmresume(PVMCPUCC pVCpu, uint8_t cbInstr, VMXINSTRID uInstrId)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 3);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_VMX_VMENTRY_MASK);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = iemVmxVmlaunchVmresume(pVCpu, cbInstr, uInstrId);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Checks whether an RDMSR or WRMSR instruction for the given MSR is intercepted
 * (causes a VM-exit)  or not.
 *
 * @returns @c true if the instruction is intercepted, @c false otherwise.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   uExitReason     The VM-exit reason (VMX_EXIT_RDMSR or
 *                          VMX_EXIT_WRMSR).
 * @param   idMsr           The MSR.
 */
bool iemVmxIsRdmsrWrmsrInterceptSet(PCVMCPU pVCpu, uint32_t uExitReason, uint32_t idMsr) RT_NOEXCEPT
{
    Assert(IEM_VMX_IS_NON_ROOT_MODE(pVCpu));
    Assert(   uExitReason == VMX_EXIT_RDMSR
           || uExitReason == VMX_EXIT_WRMSR);

    /* Consult the MSR bitmap if the feature is supported. */
    PCVMXVVMCS const pVmcs = &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs;
    Assert(pVmcs);
    if (pVmcs->u32ProcCtls & VMX_PROC_CTLS_USE_MSR_BITMAPS)
    {
        uint32_t const fMsrpm = CPUMGetVmxMsrPermission(pVCpu->cpum.GstCtx.hwvirt.vmx.abMsrBitmap, idMsr);
        if (uExitReason == VMX_EXIT_RDMSR)
            return RT_BOOL(fMsrpm & VMXMSRPM_EXIT_RD);
        return RT_BOOL(fMsrpm & VMXMSRPM_EXIT_WR);
    }

    /* Without MSR bitmaps, all MSR accesses are intercepted. */
    return true;
}


/**
 * VMREAD instruction execution worker that does not perform any validation checks.
 *
 * Callers are expected to have performed the necessary checks and to ensure the
 * VMREAD will succeed.
 *
 * @param   pVmcs           Pointer to the virtual VMCS.
 * @param   pu64Dst         Where to write the VMCS value.
 * @param   u64VmcsField    The VMCS field.
 *
 * @remarks May be called with interrupts disabled.
 */
static void iemVmxVmreadNoCheck(PCVMXVVMCS pVmcs, uint64_t *pu64Dst, uint64_t u64VmcsField) RT_NOEXCEPT
{
    VMXVMCSFIELD VmcsField;
    VmcsField.u = u64VmcsField;
    uint8_t  const uWidth     = RT_BF_GET(VmcsField.u, VMX_BF_VMCSFIELD_WIDTH);
    uint8_t  const uType      = RT_BF_GET(VmcsField.u, VMX_BF_VMCSFIELD_TYPE);
    uint8_t  const uWidthType = (uWidth << 2) | uType;
    uint8_t  const uIndex     = RT_BF_GET(VmcsField.u, VMX_BF_VMCSFIELD_INDEX);
    Assert(uIndex <= VMX_V_VMCS_MAX_INDEX);
    uint16_t const offField   = g_aoffVmcsMap[uWidthType][uIndex];
    AssertMsg(offField < VMX_V_VMCS_SIZE, ("off=%u field=%#RX64 width=%#x type=%#x index=%#x (%u)\n", offField, u64VmcsField,
                                           uWidth, uType, uIndex, uIndex));
    AssertCompile(VMX_V_SHADOW_VMCS_SIZE == VMX_V_VMCS_SIZE);

    /*
     * Read the VMCS component based on the field's effective width.
     *
     * The effective width is 64-bit fields adjusted to 32-bits if the access-type
     * indicates high bits (little endian).
     *
     * Note! The caller is responsible to trim the result and update registers
     * or memory locations are required. Here we just zero-extend to the largest
     * type (i.e. 64-bits).
     */
    uint8_t const *pbVmcs    = (uint8_t const *)pVmcs;
    uint8_t const *pbField   = pbVmcs + offField;
    uint8_t const  uEffWidth = VMXGetVmcsFieldWidthEff(VmcsField.u);
    switch (uEffWidth)
    {
        case VMX_VMCSFIELD_WIDTH_64BIT:
        case VMX_VMCSFIELD_WIDTH_NATURAL: *pu64Dst = *(uint64_t const *)pbField; break;
        case VMX_VMCSFIELD_WIDTH_32BIT:   *pu64Dst = *(uint32_t const *)pbField; break;
        case VMX_VMCSFIELD_WIDTH_16BIT:   *pu64Dst = *(uint16_t const *)pbField; break;
    }
}


/**
 * Interface for HM and EM to read a VMCS field from the nested-guest VMCS.
 *
 * It is ASSUMED the caller knows what they're doing. No VMREAD instruction checks
 * are performed. Bounds checks are strict builds only.
 *
 * @param   pVmcs           Pointer to the virtual VMCS.
 * @param   u64VmcsField    The VMCS field.
 * @param   pu64Dst         Where to store the VMCS value.
 *
 * @remarks May be called with interrupts disabled.
 * @todo    This should probably be moved to CPUM someday.
 */
VMM_INT_DECL(void) IEMReadVmxVmcsField(PCVMXVVMCS pVmcs, uint64_t u64VmcsField, uint64_t *pu64Dst)
{
    AssertPtr(pVmcs);
    AssertPtr(pu64Dst);
    iemVmxVmreadNoCheck(pVmcs, pu64Dst, u64VmcsField);
}


/**
 * VMREAD common (memory/register) instruction execution worker.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   cbInstr         The instruction length in bytes.
 * @param   pu64Dst         Where to write the VMCS value (only updated when
 *                          VINF_SUCCESS is returned).
 * @param   u64VmcsField    The VMCS field.
 * @param   pExitInfo       Pointer to the VM-exit information. Optional, can be
 *                          NULL.
 */
static VBOXSTRICTRC iemVmxVmreadCommon(PVMCPUCC pVCpu, uint8_t cbInstr, uint64_t *pu64Dst,
                                       uint64_t u64VmcsField, PCVMXVEXITINFO pExitInfo) RT_NOEXCEPT
{
    /* Nested-guest intercept. */
    if (   IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
        && CPUMIsGuestVmxVmreadVmwriteInterceptSet(pVCpu, VMX_EXIT_VMREAD, u64VmcsField))
    {
        if (pExitInfo)
            return iemVmxVmexitInstrWithInfo(pVCpu, pExitInfo);
        return iemVmxVmexitInstrNeedsInfo(pVCpu, VMX_EXIT_VMREAD, VMXINSTRID_VMREAD, cbInstr);
    }

    /* CPL. */
    if (IEM_GET_CPL(pVCpu) == 0)
    { /* likely */ }
    else
    {
        Log(("vmread: CPL %u -> #GP(0)\n", IEM_GET_CPL(pVCpu)));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmread_Cpl;
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    pVCpu->iem.s.cPotentialExits++;

    /* VMCS pointer in root mode. */
    if (   !IEM_VMX_IS_ROOT_MODE(pVCpu)
        ||  IEM_VMX_HAS_CURRENT_VMCS(pVCpu))
    { /* likely */ }
    else
    {
        Log(("vmread: VMCS pointer %#RGp invalid -> VMFailInvalid\n", IEM_VMX_GET_CURRENT_VMCS(pVCpu)));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmread_PtrInvalid;
        iemVmxVmFailInvalid(pVCpu);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /* VMCS-link pointer in non-root mode. */
    if (   !IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
        ||  IEM_VMX_HAS_SHADOW_VMCS(pVCpu))
    { /* likely */ }
    else
    {
        Log(("vmread: VMCS-link pointer %#RGp invalid -> VMFailInvalid\n", IEM_VMX_GET_SHADOW_VMCS(pVCpu)));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmread_LinkPtrInvalid;
        iemVmxVmFailInvalid(pVCpu);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /* Supported VMCS field. */
    if (CPUMIsGuestVmxVmcsFieldValid(pVCpu->CTX_SUFF(pVM), u64VmcsField))
    { /* likely */ }
    else
    {
        Log(("vmread: VMCS field %#RX64 invalid -> VMFail\n", u64VmcsField));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmread_FieldInvalid;
        pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = u64VmcsField;
        iemVmxVmFail(pVCpu, VMXINSTRERR_VMREAD_INVALID_COMPONENT);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /*
     * Reading from the current or shadow VMCS.
     */
    PCVMXVVMCS pVmcs = !IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
                     ? &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs
                     : &pVCpu->cpum.GstCtx.hwvirt.vmx.ShadowVmcs;
    iemVmxVmreadNoCheck(pVmcs, pu64Dst, u64VmcsField);
    Log4(("vmread %#RX64 => %#RX64\n", u64VmcsField, *pu64Dst));
    return VINF_SUCCESS;
}


/**
 * VMREAD (64-bit register) instruction execution worker.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   cbInstr         The instruction length in bytes.
 * @param   pu64Dst         Where to store the VMCS field's value.
 * @param   u64VmcsField    The VMCS field.
 * @param   pExitInfo       Pointer to the VM-exit information. Optional, can be
 *                          NULL.
 */
static VBOXSTRICTRC iemVmxVmreadReg64(PVMCPUCC pVCpu, uint8_t cbInstr, uint64_t *pu64Dst,
                                      uint64_t u64VmcsField, PCVMXVEXITINFO pExitInfo)  RT_NOEXCEPT
{
    VBOXSTRICTRC rcStrict = iemVmxVmreadCommon(pVCpu, cbInstr, pu64Dst, u64VmcsField, pExitInfo);
    if (rcStrict == VINF_SUCCESS)
    {
        iemVmxVmSucceed(pVCpu);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    Log(("vmread/reg: iemVmxVmreadCommon failed rc=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/**
 * VMREAD (32-bit register) instruction execution worker.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   cbInstr         The instruction length in bytes.
 * @param   pu32Dst         Where to store the VMCS field's value.
 * @param   u32VmcsField    The VMCS field.
 * @param   pExitInfo       Pointer to the VM-exit information. Optional, can be
 *                          NULL.
 */
static VBOXSTRICTRC iemVmxVmreadReg32(PVMCPUCC pVCpu, uint8_t cbInstr, uint32_t *pu32Dst,
                                      uint64_t u32VmcsField, PCVMXVEXITINFO pExitInfo) RT_NOEXCEPT
{
    uint64_t u64Dst;
    VBOXSTRICTRC rcStrict = iemVmxVmreadCommon(pVCpu, cbInstr, &u64Dst, u32VmcsField, pExitInfo);
    if (rcStrict == VINF_SUCCESS)
    {
        *pu32Dst = u64Dst;
        iemVmxVmSucceed(pVCpu);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    Log(("vmread/reg: iemVmxVmreadCommon failed rc=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/**
 * VMREAD (memory) instruction execution worker.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   cbInstr         The instruction length in bytes.
 * @param   iEffSeg         The effective segment register to use with @a u64Val.
 *                          Pass UINT8_MAX if it is a register access.
 * @param   GCPtrDst        The guest linear address to store the VMCS field's
 *                          value.
 * @param   u64VmcsField    The VMCS field.
 * @param   pExitInfo       Pointer to the VM-exit information. Optional, can be
 *                          NULL.
 */
static VBOXSTRICTRC iemVmxVmreadMem(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iEffSeg, RTGCPTR GCPtrDst,
                                    uint64_t u64VmcsField, PCVMXVEXITINFO pExitInfo) RT_NOEXCEPT
{
    uint64_t u64Dst;
    VBOXSTRICTRC rcStrict = iemVmxVmreadCommon(pVCpu, cbInstr, &u64Dst, u64VmcsField, pExitInfo);
    if (rcStrict == VINF_SUCCESS)
    {
        /*
         * Write the VMCS field's value to the location specified in guest-memory.
         */
        if (IEM_IS_64BIT_CODE(pVCpu))
            rcStrict = iemMemStoreDataU64(pVCpu, iEffSeg, GCPtrDst, u64Dst);
        else
            rcStrict = iemMemStoreDataU32(pVCpu, iEffSeg, GCPtrDst, u64Dst);
        if (rcStrict == VINF_SUCCESS)
        {
            iemVmxVmSucceed(pVCpu);
            return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }

        Log(("vmread/mem: Failed to write to memory operand at %#RGv, rc=%Rrc\n", GCPtrDst, VBOXSTRICTRC_VAL(rcStrict)));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmread_PtrMap;
        pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPtrDst;
        return rcStrict;
    }

    Log(("vmread/mem: iemVmxVmreadCommon failed rc=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
    return rcStrict;
}


/**
 * Interface for HM and EM to emulate the VMREAD instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pExitInfo       Pointer to the VM-exit information.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedVmread(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(pExitInfo->cbInstr, 3);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK | CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI);
    Assert(pExitInfo);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);

    VBOXSTRICTRC   rcStrict;
    uint8_t const  cbInstr       = pExitInfo->cbInstr;
    bool const     fIs64BitMode  = RT_BOOL(IEM_IS_64BIT_CODE(pVCpu));
    uint64_t const u64FieldEnc   = fIs64BitMode
                                 ? iemGRegFetchU64(pVCpu, pExitInfo->InstrInfo.VmreadVmwrite.iReg2)
                                 : iemGRegFetchU32(pVCpu, pExitInfo->InstrInfo.VmreadVmwrite.iReg2);
    if (pExitInfo->InstrInfo.VmreadVmwrite.fIsRegOperand)
    {
        if (fIs64BitMode)
        {
            uint64_t *pu64Dst = iemGRegRefU64(pVCpu, pExitInfo->InstrInfo.VmreadVmwrite.iReg1);
            rcStrict = iemVmxVmreadReg64(pVCpu, cbInstr, pu64Dst, u64FieldEnc, pExitInfo);
        }
        else
        {
            uint32_t *pu32Dst = iemGRegRefU32(pVCpu, pExitInfo->InstrInfo.VmreadVmwrite.iReg1);
            rcStrict = iemVmxVmreadReg32(pVCpu, cbInstr, pu32Dst, u64FieldEnc, pExitInfo);
        }
    }
    else
    {
        RTGCPTR const GCPtrDst = pExitInfo->GCPtrEffAddr;
        uint8_t const iEffSeg  = pExitInfo->InstrInfo.VmreadVmwrite.iSegReg;
        rcStrict = iemVmxVmreadMem(pVCpu, cbInstr, iEffSeg, GCPtrDst, u64FieldEnc, pExitInfo);
    }
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * VMWRITE instruction execution worker that does not perform any validation
 * checks.
 *
 * Callers are expected to have performed the necessary checks and to ensure the
 * VMWRITE will succeed.
 *
 * @param   pVmcs           Pointer to the virtual VMCS.
 * @param   u64Val          The value to write.
 * @param   u64VmcsField    The VMCS field.
 *
 * @remarks May be called with interrupts disabled.
 */
static void iemVmxVmwriteNoCheck(PVMXVVMCS pVmcs, uint64_t u64Val, uint64_t u64VmcsField) RT_NOEXCEPT
{
    VMXVMCSFIELD VmcsField;
    VmcsField.u = u64VmcsField;
    uint8_t  const uWidth     = RT_BF_GET(VmcsField.u, VMX_BF_VMCSFIELD_WIDTH);
    uint8_t  const uType      = RT_BF_GET(VmcsField.u, VMX_BF_VMCSFIELD_TYPE);
    uint8_t  const uWidthType = (uWidth << 2) | uType;
    uint8_t  const uIndex     = RT_BF_GET(VmcsField.u, VMX_BF_VMCSFIELD_INDEX);
    Assert(uIndex <= VMX_V_VMCS_MAX_INDEX);
    uint16_t const offField   = g_aoffVmcsMap[uWidthType][uIndex];
    Assert(offField < VMX_V_VMCS_SIZE);
    AssertCompile(VMX_V_SHADOW_VMCS_SIZE == VMX_V_VMCS_SIZE);

    /*
     * Write the VMCS component based on the field's effective width.
     *
     * The effective width is 64-bit fields adjusted to 32-bits if the access-type
     * indicates high bits (little endian).
     */
    uint8_t      *pbVmcs    = (uint8_t *)pVmcs;
    uint8_t      *pbField   = pbVmcs + offField;
    uint8_t const uEffWidth = VMXGetVmcsFieldWidthEff(VmcsField.u);
    switch (uEffWidth)
    {
        case VMX_VMCSFIELD_WIDTH_64BIT:
        case VMX_VMCSFIELD_WIDTH_NATURAL: *(uint64_t *)pbField = u64Val; break;
        case VMX_VMCSFIELD_WIDTH_32BIT:   *(uint32_t *)pbField = u64Val; break;
        case VMX_VMCSFIELD_WIDTH_16BIT:   *(uint16_t *)pbField = u64Val; break;
    }
}


/**
 * Interface for HM and EM to write a VMCS field in the nested-guest VMCS.
 *
 * It is ASSUMED the caller knows what they're doing. No VMWRITE instruction checks
 * are performed. Bounds checks are strict builds only.
 *
 * @param   pVmcs           Pointer to the virtual VMCS.
 * @param   u64VmcsField    The VMCS field.
 * @param   u64Val          The value to write.
 *
 * @remarks May be called with interrupts disabled.
 * @todo    This should probably be moved to CPUM someday.
 */
VMM_INT_DECL(void) IEMWriteVmxVmcsField(PVMXVVMCS pVmcs, uint64_t u64VmcsField, uint64_t u64Val)
{
    AssertPtr(pVmcs);
    iemVmxVmwriteNoCheck(pVmcs, u64Val, u64VmcsField);
}


/**
 * VMWRITE instruction execution worker.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   cbInstr         The instruction length in bytes.
 * @param   iEffSeg         The effective segment register to use with @a u64Val.
 *                          Pass UINT8_MAX if it is a register access.
 * @param   u64Val          The value to write (or guest linear address to the
 *                          value), @a iEffSeg will indicate if it's a memory
 *                          operand.
 * @param   u64VmcsField    The VMCS field.
 * @param   pExitInfo       Pointer to the VM-exit information. Optional, can be
 *                          NULL.
 */
static VBOXSTRICTRC iemVmxVmwrite(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iEffSeg, uint64_t u64Val,
                                  uint64_t u64VmcsField, PCVMXVEXITINFO pExitInfo) RT_NOEXCEPT
{
    /* Nested-guest intercept. */
    if (   IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
        && CPUMIsGuestVmxVmreadVmwriteInterceptSet(pVCpu, VMX_EXIT_VMWRITE, u64VmcsField))
    {
        if (pExitInfo)
            return iemVmxVmexitInstrWithInfo(pVCpu, pExitInfo);
        return iemVmxVmexitInstrNeedsInfo(pVCpu, VMX_EXIT_VMWRITE, VMXINSTRID_VMWRITE, cbInstr);
    }

    /* CPL. */
    if (IEM_GET_CPL(pVCpu) == 0)
    { /* likely */ }
    else
    {
        Log(("vmwrite: CPL %u -> #GP(0)\n", IEM_GET_CPL(pVCpu)));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmwrite_Cpl;
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    pVCpu->iem.s.cPotentialExits++;

    /* VMCS pointer in root mode. */
    if (   !IEM_VMX_IS_ROOT_MODE(pVCpu)
        ||  IEM_VMX_HAS_CURRENT_VMCS(pVCpu))
    { /* likely */ }
    else
    {
        Log(("vmwrite: VMCS pointer %#RGp invalid -> VMFailInvalid\n", IEM_VMX_GET_CURRENT_VMCS(pVCpu)));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmwrite_PtrInvalid;
        iemVmxVmFailInvalid(pVCpu);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /* VMCS-link pointer in non-root mode. */
    if (   !IEM_VMX_IS_NON_ROOT_MODE(pVCpu)
        ||  IEM_VMX_HAS_SHADOW_VMCS(pVCpu))
    { /* likely */ }
    else
    {
        Log(("vmwrite: VMCS-link pointer %#RGp invalid -> VMFailInvalid\n", IEM_VMX_GET_SHADOW_VMCS(pVCpu)));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmwrite_LinkPtrInvalid;
        iemVmxVmFailInvalid(pVCpu);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /* If the VMWRITE instruction references memory, access the specified memory operand. */
    bool const fIsRegOperand = iEffSeg == UINT8_MAX;
    if (!fIsRegOperand)
    {
        /* Read the value from the specified guest memory location. */
        VBOXSTRICTRC  rcStrict;
        RTGCPTR const GCPtrVal = u64Val;
        if (IEM_IS_64BIT_CODE(pVCpu))
            rcStrict = iemMemFetchDataU64(pVCpu, &u64Val, iEffSeg, GCPtrVal);
        else
            rcStrict = iemMemFetchDataU32_ZX_U64(pVCpu, &u64Val, iEffSeg, GCPtrVal);
        if (RT_UNLIKELY(rcStrict != VINF_SUCCESS))
        {
            Log(("vmwrite: Failed to read value from memory operand at %#RGv, rc=%Rrc\n", GCPtrVal, VBOXSTRICTRC_VAL(rcStrict)));
            pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmwrite_PtrMap;
            pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPtrVal;
            return rcStrict;
        }
    }
    else
        Assert(!pExitInfo || pExitInfo->InstrInfo.VmreadVmwrite.fIsRegOperand);

    /* Supported VMCS field. */
    if (CPUMIsGuestVmxVmcsFieldValid(pVCpu->CTX_SUFF(pVM), u64VmcsField))
    { /* likely */ }
    else
    {
        Log(("vmwrite: VMCS field %#RX64 invalid -> VMFail\n", u64VmcsField));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmwrite_FieldInvalid;
        pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = u64VmcsField;
        iemVmxVmFail(pVCpu, VMXINSTRERR_VMWRITE_INVALID_COMPONENT);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /* Read-only VMCS field. */
    bool const fIsFieldReadOnly = VMXIsVmcsFieldReadOnly(u64VmcsField);
    if (   !fIsFieldReadOnly
        ||  IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fVmxVmwriteAll)
    { /* likely */ }
    else
    {
        Log(("vmwrite: Write to read-only VMCS component %#RX64 -> VMFail\n", u64VmcsField));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmwrite_FieldRo;
        pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = u64VmcsField;
        iemVmxVmFail(pVCpu, VMXINSTRERR_VMWRITE_RO_COMPONENT);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /*
     * Write to the current or shadow VMCS.
     */
    bool const fInVmxNonRootMode = IEM_VMX_IS_NON_ROOT_MODE(pVCpu);
    PVMXVVMCS pVmcs = !fInVmxNonRootMode
                    ? &pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs
                    : &pVCpu->cpum.GstCtx.hwvirt.vmx.ShadowVmcs;
    iemVmxVmwriteNoCheck(pVmcs, u64Val, u64VmcsField);
    Log4(("vmwrite %#RX64 <= %#RX64\n", u64VmcsField, u64Val));

    if (   !fInVmxNonRootMode
        && VM_IS_HM_ENABLED(pVCpu->CTX_SUFF(pVM)))
    {
        /* Notify HM that the VMCS content might have changed. */
        HMNotifyVmxNstGstCurrentVmcsChanged(pVCpu);
    }

    iemVmxVmSucceed(pVCpu);
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Interface for HM and EM to emulate the VMWRITE instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pExitInfo       Pointer to the VM-exit information.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedVmwrite(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(pExitInfo->cbInstr, 3);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK | CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI);
    Assert(pExitInfo);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);

    uint64_t u64Val;
    uint8_t  iEffSeg;
    if (pExitInfo->InstrInfo.VmreadVmwrite.fIsRegOperand)
    {
        u64Val  = iemGRegFetchU64(pVCpu, pExitInfo->InstrInfo.VmreadVmwrite.iReg1);
        iEffSeg = UINT8_MAX;
    }
    else
    {
        u64Val  = pExitInfo->GCPtrEffAddr;
        iEffSeg = pExitInfo->InstrInfo.VmreadVmwrite.iSegReg;
    }
    uint8_t const  cbInstr     = pExitInfo->cbInstr;
    uint64_t const u64FieldEnc = IEM_IS_64BIT_CODE(pVCpu)
                               ? iemGRegFetchU64(pVCpu, pExitInfo->InstrInfo.VmreadVmwrite.iReg2)
                               : iemGRegFetchU32(pVCpu, pExitInfo->InstrInfo.VmreadVmwrite.iReg2);
    VBOXSTRICTRC rcStrict = iemVmxVmwrite(pVCpu, cbInstr, iEffSeg, u64Val, u64FieldEnc, pExitInfo);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * VMCLEAR instruction execution worker.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The instruction length in bytes.
 * @param   iEffSeg     The effective segment register to use with @a GCPtrVmcs.
 * @param   GCPtrVmcs   The linear address of the VMCS pointer.
 * @param   pExitInfo   Pointer to the VM-exit information. Optional, can be NULL.
 *
 * @remarks Common VMX instruction checks are already expected to by the caller,
 *          i.e. VMX operation, CR4.VMXE, Real/V86 mode, EFER/CS.L checks.
 */
static VBOXSTRICTRC iemVmxVmclear(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iEffSeg,
                                  RTGCPHYS GCPtrVmcs, PCVMXVEXITINFO pExitInfo) RT_NOEXCEPT
{
    /* Nested-guest intercept. */
    if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        if (pExitInfo)
            return iemVmxVmexitInstrWithInfo(pVCpu, pExitInfo);
        return iemVmxVmexitInstrNeedsInfo(pVCpu, VMX_EXIT_VMCLEAR, VMXINSTRID_NONE, cbInstr);
    }

    Assert(IEM_VMX_IS_ROOT_MODE(pVCpu));

    /* CPL. */
    if (IEM_GET_CPL(pVCpu) == 0)
    { /* likely */ }
    else
    {
        Log(("vmclear: CPL %u -> #GP(0)\n", IEM_GET_CPL(pVCpu)));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmclear_Cpl;
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /* Get the VMCS pointer from the location specified by the source memory operand. */
    RTGCPHYS GCPhysVmcs;
    VBOXSTRICTRC rcStrict = iemMemFetchDataU64(pVCpu, &GCPhysVmcs, iEffSeg, GCPtrVmcs);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
    { /* likely */ }
    else
    {
        Log(("vmclear: Failed to read VMCS physaddr from %#RGv, rc=%Rrc\n", GCPtrVmcs, VBOXSTRICTRC_VAL(rcStrict)));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmclear_PtrMap;
        pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPtrVmcs;
        return rcStrict;
    }

    /* VMCS pointer alignment. */
    if (!(GCPhysVmcs & X86_PAGE_4K_OFFSET_MASK))
    { /* likely */ }
    else
    {
        Log(("vmclear: VMCS pointer not page-aligned -> VMFail()\n"));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmclear_PtrAlign;
        pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPhysVmcs;
        iemVmxVmFail(pVCpu, VMXINSTRERR_VMCLEAR_INVALID_PHYSADDR);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /* VMCS physical-address width limits. */
    if (!(GCPhysVmcs >> IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cVmxMaxPhysAddrWidth))
    { /* likely */ }
    else
    {
        Log(("vmclear: VMCS pointer extends beyond physical-address width -> VMFail()\n"));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmclear_PtrWidth;
        pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPhysVmcs;
        iemVmxVmFail(pVCpu, VMXINSTRERR_VMCLEAR_INVALID_PHYSADDR);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /* VMCS is not the VMXON region. */
    if (GCPhysVmcs != pVCpu->cpum.GstCtx.hwvirt.vmx.GCPhysVmxon)
    { /* likely */ }
    else
    {
        Log(("vmclear: VMCS pointer cannot be identical to VMXON region pointer -> VMFail()\n"));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmclear_PtrVmxon;
        pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPhysVmcs;
        iemVmxVmFail(pVCpu, VMXINSTRERR_VMCLEAR_VMXON_PTR);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /* Ensure VMCS is not MMIO, ROM etc. This is not an Intel requirement but a
       restriction imposed by our implementation. */
    if (PGMPhysIsGCPhysNormal(pVCpu->CTX_SUFF(pVM), GCPhysVmcs))
    { /* likely */ }
    else
    {
        Log(("vmclear: VMCS not normal memory -> VMFail()\n"));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmclear_PtrAbnormal;
        pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPhysVmcs;
        iemVmxVmFail(pVCpu, VMXINSTRERR_VMCLEAR_INVALID_PHYSADDR);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /*
     * VMCLEAR allows committing and clearing any valid VMCS pointer.
     *
     * If the current VMCS is the one being cleared, set its state to 'clear' and commit
     * to guest memory. Otherwise, set the state of the VMCS referenced in guest memory
     * to 'clear'.
     */
    uint8_t const fVmcsLaunchStateClear = VMX_V_VMCS_LAUNCH_STATE_CLEAR;
    if (   IEM_VMX_HAS_CURRENT_VMCS(pVCpu)
        && IEM_VMX_GET_CURRENT_VMCS(pVCpu) == GCPhysVmcs)
    {
        pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.fVmcsState = fVmcsLaunchStateClear;
        iemVmxWriteCurrentVmcsToGstMem(pVCpu);
        IEM_VMX_CLEAR_CURRENT_VMCS(pVCpu);
    }
    else
    {
        AssertCompileMemberSize(VMXVVMCS, fVmcsState, sizeof(fVmcsLaunchStateClear));
        rcStrict = PGMPhysSimpleWriteGCPhys(pVCpu->CTX_SUFF(pVM), GCPhysVmcs + RT_UOFFSETOF(VMXVVMCS, fVmcsState),
                                            (const void *)&fVmcsLaunchStateClear, sizeof(fVmcsLaunchStateClear));
        if (RT_FAILURE(rcStrict))
            return rcStrict;
    }

    iemVmxVmSucceed(pVCpu);
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Interface for HM and EM to emulate the VMCLEAR instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pExitInfo       Pointer to the VM-exit information.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedVmclear(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo)
{
    Assert(pExitInfo);
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(pExitInfo->cbInstr, 3);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK | CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);

    uint8_t const iEffSeg   = pExitInfo->InstrInfo.VmxXsave.iSegReg;
    uint8_t const cbInstr   = pExitInfo->cbInstr;
    RTGCPTR const GCPtrVmcs = pExitInfo->GCPtrEffAddr;
    VBOXSTRICTRC rcStrict = iemVmxVmclear(pVCpu, cbInstr, iEffSeg, GCPtrVmcs, pExitInfo);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * VMPTRST instruction execution worker.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The instruction length in bytes.
 * @param   iEffSeg     The effective segment register to use with @a GCPtrVmcs.
 * @param   GCPtrVmcs   The linear address of where to store the current VMCS
 *                      pointer.
 * @param   pExitInfo   Pointer to the VM-exit information. Optional, can be NULL.
 *
 * @remarks Common VMX instruction checks are already expected to by the caller,
 *          i.e. VMX operation, CR4.VMXE, Real/V86 mode, EFER/CS.L checks.
 */
static VBOXSTRICTRC iemVmxVmptrst(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iEffSeg,
                                  RTGCPHYS GCPtrVmcs, PCVMXVEXITINFO pExitInfo) RT_NOEXCEPT
{
    /* Nested-guest intercept. */
    if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        if (pExitInfo)
            return iemVmxVmexitInstrWithInfo(pVCpu, pExitInfo);
        return iemVmxVmexitInstrNeedsInfo(pVCpu, VMX_EXIT_VMPTRST, VMXINSTRID_NONE, cbInstr);
    }

    Assert(IEM_VMX_IS_ROOT_MODE(pVCpu));

    /* CPL. */
    if (IEM_GET_CPL(pVCpu) == 0)
    { /* likely */ }
    else
    {
        Log(("vmptrst: CPL %u -> #GP(0)\n", IEM_GET_CPL(pVCpu)));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmptrst_Cpl;
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /* Set the VMCS pointer to the location specified by the destination memory operand. */
    AssertCompile(NIL_RTGCPHYS == ~(RTGCPHYS)0U);
    VBOXSTRICTRC rcStrict = iemMemStoreDataU64(pVCpu, iEffSeg, GCPtrVmcs, IEM_VMX_GET_CURRENT_VMCS(pVCpu));
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
    {
        iemVmxVmSucceed(pVCpu);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    Log(("vmptrst: Failed to store VMCS pointer to memory at destination operand %#Rrc\n", VBOXSTRICTRC_VAL(rcStrict)));
    pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmptrst_PtrMap;
    pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPtrVmcs;
    return rcStrict;
}


/**
 * Interface for HM and EM to emulate the VMPTRST instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pExitInfo       Pointer to the VM-exit information.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedVmptrst(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo)
{
    Assert(pExitInfo);
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(pExitInfo->cbInstr, 3);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK | CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);

    uint8_t const iEffSeg   = pExitInfo->InstrInfo.VmxXsave.iSegReg;
    uint8_t const cbInstr   = pExitInfo->cbInstr;
    RTGCPTR const GCPtrVmcs = pExitInfo->GCPtrEffAddr;
    VBOXSTRICTRC rcStrict = iemVmxVmptrst(pVCpu, cbInstr, iEffSeg, GCPtrVmcs, pExitInfo);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * VMPTRLD instruction execution worker.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The instruction length in bytes.
 * @param   GCPtrVmcs   The linear address of the current VMCS pointer.
 * @param   pExitInfo   Pointer to the VM-exit information. Optional, can be NULL.
 *
 * @remarks Common VMX instruction checks are already expected to by the caller,
 *          i.e. VMX operation, CR4.VMXE, Real/V86 mode, EFER/CS.L checks.
 */
static VBOXSTRICTRC iemVmxVmptrld(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iEffSeg,
                                  RTGCPHYS GCPtrVmcs, PCVMXVEXITINFO pExitInfo) RT_NOEXCEPT
{
    /* Nested-guest intercept. */
    if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        if (pExitInfo)
            return iemVmxVmexitInstrWithInfo(pVCpu, pExitInfo);
        return iemVmxVmexitInstrNeedsInfo(pVCpu, VMX_EXIT_VMPTRLD, VMXINSTRID_NONE, cbInstr);
    }

    Assert(IEM_VMX_IS_ROOT_MODE(pVCpu));

    /* CPL. */
    if (IEM_GET_CPL(pVCpu) == 0)
    { /* likely */ }
    else
    {
        Log(("vmptrld: CPL %u -> #GP(0)\n", IEM_GET_CPL(pVCpu)));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmptrld_Cpl;
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /* Get the VMCS pointer from the location specified by the source memory operand. */
    RTGCPHYS GCPhysVmcs;
    VBOXSTRICTRC rcStrict = iemMemFetchDataU64(pVCpu, &GCPhysVmcs, iEffSeg, GCPtrVmcs);
    if (RT_LIKELY(rcStrict == VINF_SUCCESS))
    { /* likely */ }
    else
    {
        Log(("vmptrld: Failed to read VMCS physaddr from %#RGv, rc=%Rrc\n", GCPtrVmcs, VBOXSTRICTRC_VAL(rcStrict)));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmptrld_PtrMap;
        pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPtrVmcs;
        return rcStrict;
    }

    /* VMCS pointer alignment. */
    if (!(GCPhysVmcs & X86_PAGE_4K_OFFSET_MASK))
    { /* likely */ }
    else
    {
        Log(("vmptrld: VMCS pointer not page-aligned -> VMFail()\n"));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmptrld_PtrAlign;
        pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPhysVmcs;
        iemVmxVmFail(pVCpu, VMXINSTRERR_VMPTRLD_INVALID_PHYSADDR);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /* VMCS physical-address width limits. */
    if (!(GCPhysVmcs >> IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cVmxMaxPhysAddrWidth))
    { /* likely */ }
    else
    {
        Log(("vmptrld: VMCS pointer extends beyond physical-address width -> VMFail()\n"));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmptrld_PtrWidth;
        pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPhysVmcs;
        iemVmxVmFail(pVCpu, VMXINSTRERR_VMPTRLD_INVALID_PHYSADDR);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /* VMCS is not the VMXON region. */
    if (GCPhysVmcs != pVCpu->cpum.GstCtx.hwvirt.vmx.GCPhysVmxon)
    { /* likely */ }
    else
    {
        Log(("vmptrld: VMCS pointer cannot be identical to VMXON region pointer -> VMFail()\n"));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmptrld_PtrVmxon;
        pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPhysVmcs;
        iemVmxVmFail(pVCpu, VMXINSTRERR_VMPTRLD_VMXON_PTR);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /* Ensure VMCS is not MMIO, ROM etc. This is not an Intel requirement but a
       restriction imposed by our implementation. */
    if (PGMPhysIsGCPhysNormal(pVCpu->CTX_SUFF(pVM), GCPhysVmcs))
    { /* likely */ }
    else
    {
        Log(("vmptrld: VMCS not normal memory -> VMFail()\n"));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmptrld_PtrAbnormal;
        pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPhysVmcs;
        iemVmxVmFail(pVCpu, VMXINSTRERR_VMPTRLD_INVALID_PHYSADDR);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /* Read just the VMCS revision from the VMCS. */
    VMXVMCSREVID VmcsRevId;
    int rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &VmcsRevId, GCPhysVmcs, sizeof(VmcsRevId));
    if (RT_SUCCESS(rc))
    { /* likely */ }
    else
    {
        Log(("vmptrld: Failed to read revision identifier from VMCS at %#RGp, rc=%Rrc\n", GCPhysVmcs, rc));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmptrld_RevPtrReadPhys;
        pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPhysVmcs;
        return rc;
    }

    /*
     * Verify the VMCS revision specified by the guest matches what we reported to the guest.
     * Verify the VMCS is not a shadow VMCS, if the VMCS shadowing feature is supported.
     */
    if (   VmcsRevId.n.u31RevisionId == VMX_V_VMCS_REVISION_ID
        && (   !VmcsRevId.n.fIsShadowVmcs
            ||  IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fVmxVmcsShadowing))
    { /* likely */ }
    else
    {
        if (VmcsRevId.n.u31RevisionId != VMX_V_VMCS_REVISION_ID)
        {
            Log(("vmptrld: VMCS revision mismatch, expected %#RX32 got %#RX32, GCPtrVmcs=%#RGv GCPhysVmcs=%#RGp -> VMFail()\n",
                 VMX_V_VMCS_REVISION_ID, VmcsRevId.n.u31RevisionId, GCPtrVmcs, GCPhysVmcs));
            pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmptrld_VmcsRevId;
        }
        else
        {
            Log(("vmptrld: Shadow VMCS -> VMFail()\n"));
            pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmptrld_ShadowVmcs;
        }
        iemVmxVmFail(pVCpu, VMXINSTRERR_VMPTRLD_INCORRECT_VMCS_REV);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /*
     * We cache only the current VMCS in CPUMCTX. Therefore, VMPTRLD should always flush
     * the cache of an existing, current VMCS back to guest memory before loading a new,
     * different current VMCS.
     */
    if (IEM_VMX_GET_CURRENT_VMCS(pVCpu) != GCPhysVmcs)
    {
        if (IEM_VMX_HAS_CURRENT_VMCS(pVCpu))
        {
            iemVmxWriteCurrentVmcsToGstMem(pVCpu);
            IEM_VMX_CLEAR_CURRENT_VMCS(pVCpu);
        }

        /* Set the new VMCS as the current VMCS and read it from guest memory. */
        IEM_VMX_SET_CURRENT_VMCS(pVCpu, GCPhysVmcs);
        rc = iemVmxReadCurrentVmcsFromGstMem(pVCpu);
        if (RT_SUCCESS(rc))
        {
            /* Notify HM that a new, current VMCS is loaded. */
            if (VM_IS_HM_ENABLED(pVCpu->CTX_SUFF(pVM)))
                HMNotifyVmxNstGstCurrentVmcsChanged(pVCpu);

            /* Convert legacy launch-state value to current value, see @bugref{10318#c114} for reasons.*/
            if (pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.fVmcsState == VMX_V_VMCS_LAUNCH_STATE_CLEAR_LEGACY)
            {
                pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.fVmcsState = VMX_V_VMCS_LAUNCH_STATE_CLEAR;
                Log(("vmptrld: Updated legacy 'VMCLEAR' VMCS launch-state bit to current\n"));
            }
        }
        else
        {
            Log(("vmptrld: Failed to read VMCS at %#RGp, rc=%Rrc\n", GCPhysVmcs, rc));
            pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmptrld_PtrReadPhys;
            pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPhysVmcs;
            return rc;
        }
    }
    else if (   IEM_VMX_HAS_CURRENT_VMCS(pVCpu)
             && pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.fVmcsState == VMX_V_VMCS_LAUNCH_STATE_CLEAR_LEGACY)
    {
        /* Convert legacy launch-state value to current value, see @bugref{10318#c114} for reasons.*/
        pVCpu->cpum.GstCtx.hwvirt.vmx.Vmcs.fVmcsState = VMX_V_VMCS_LAUNCH_STATE_CLEAR;
        Log(("vmptrld: Updated legacy VMCLEAR launch-state bit to current\n"));
    }

    Assert(IEM_VMX_HAS_CURRENT_VMCS(pVCpu));
    iemVmxVmSucceed(pVCpu);
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Interface for HM and EM to emulate the VMPTRLD instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pExitInfo       Pointer to the VM-exit information.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedVmptrld(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo)
{
    Assert(pExitInfo);
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(pExitInfo->cbInstr, 3);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK | CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);

    uint8_t const iEffSeg   = pExitInfo->InstrInfo.VmxXsave.iSegReg;
    uint8_t const cbInstr   = pExitInfo->cbInstr;
    RTGCPTR const GCPtrVmcs = pExitInfo->GCPtrEffAddr;
    VBOXSTRICTRC rcStrict = iemVmxVmptrld(pVCpu, cbInstr, iEffSeg, GCPtrVmcs, pExitInfo);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * INVVPID instruction execution worker.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   cbInstr             The instruction length in bytes.
 * @param   iEffSeg             The segment of the invvpid descriptor.
 * @param   GCPtrInvvpidDesc    The address of invvpid descriptor.
 * @param   u64InvvpidType      The invalidation type.
 * @param   pExitInfo           Pointer to the VM-exit information. Optional, can be
 *                              NULL.
 *
 * @remarks Common VMX instruction checks are already expected to by the caller,
 *          i.e. VMX operation, CR4.VMXE, Real/V86 mode, EFER/CS.L checks.
 */
VBOXSTRICTRC iemVmxInvvpid(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iEffSeg, RTGCPTR GCPtrInvvpidDesc,
                           uint64_t u64InvvpidType, PCVMXVEXITINFO pExitInfo) RT_NOEXCEPT
{
    /* Check if INVVPID instruction is supported, otherwise raise #UD. */
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fVmxVpid)
        return iemRaiseUndefinedOpcode(pVCpu);

    /* Nested-guest intercept. */
    if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        if (pExitInfo)
            return iemVmxVmexitInstrWithInfo(pVCpu, pExitInfo);
        return iemVmxVmexitInstrNeedsInfo(pVCpu, VMX_EXIT_INVVPID, VMXINSTRID_NONE, cbInstr);
    }

    /* CPL. */
    if (IEM_GET_CPL(pVCpu) != 0)
    {
        Log(("invvpid: CPL != 0 -> #GP(0)\n"));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /*
     * Validate INVVPID invalidation type.
     *
     * The instruction specifies exactly ONE of the supported invalidation types.
     *
     * Each of the types has a bit in IA32_VMX_EPT_VPID_CAP MSR specifying if it is
     * supported. In theory, it's possible for a CPU to not support flushing individual
     * addresses but all the other types or any other combination. We do not take any
     * shortcuts here by assuming the types we currently expose to the guest.
     */
    uint64_t const fCaps = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64EptVpidCaps;
    bool const fInvvpidSupported           = RT_BF_GET(fCaps, VMX_BF_EPT_VPID_CAP_INVVPID);
    bool const fTypeIndivAddr              = RT_BF_GET(fCaps, VMX_BF_EPT_VPID_CAP_INVVPID_INDIV_ADDR);
    bool const fTypeSingleCtx              = RT_BF_GET(fCaps, VMX_BF_EPT_VPID_CAP_INVVPID_SINGLE_CTX);
    bool const fTypeAllCtx                 = RT_BF_GET(fCaps, VMX_BF_EPT_VPID_CAP_INVVPID_ALL_CTX);
    bool const fTypeSingleCtxRetainGlobals = RT_BF_GET(fCaps, VMX_BF_EPT_VPID_CAP_INVVPID_SINGLE_CTX_RETAIN_GLOBALS);

    bool afSupportedTypes[4];
    afSupportedTypes[0] = fTypeIndivAddr;
    afSupportedTypes[1] = fTypeSingleCtx;
    afSupportedTypes[2] = fTypeAllCtx;
    afSupportedTypes[3] = fTypeSingleCtxRetainGlobals;

    if (   fInvvpidSupported
        && !(u64InvvpidType & ~(uint64_t)VMX_INVVPID_VALID_MASK)
        && afSupportedTypes[u64InvvpidType & 3])
    { /* likely */ }
    else
    {
        Log(("invvpid: invalid/unsupported invvpid type %#x -> VMFail\n", u64InvvpidType));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Invvpid_TypeInvalid;
        pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = u64InvvpidType;
        iemVmxVmFail(pVCpu, VMXINSTRERR_INVEPT_INVVPID_INVALID_OPERAND);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /*
     * Fetch the invvpid descriptor from guest memory.
     */
    RTUINT128U uDesc;
    VBOXSTRICTRC rcStrict = iemMemFetchDataU128(pVCpu, &uDesc, iEffSeg, GCPtrInvvpidDesc);
    if (rcStrict == VINF_SUCCESS)
    {
        /*
         * Validate the descriptor.
         */
        if (uDesc.s.Lo <= 0xffff)
        { /* likely */ }
        else
        {
            Log(("invvpid: reserved bits set in invvpid descriptor %#RX64 -> #GP(0)\n", uDesc.s.Lo));
            pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Invvpid_DescRsvd;
            pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = uDesc.s.Lo;
            iemVmxVmFail(pVCpu, VMXINSTRERR_INVEPT_INVVPID_INVALID_OPERAND);
            return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }

        IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR3);
        RTGCUINTPTR64 const GCPtrInvAddr = uDesc.s.Hi;
        uint16_t      const uVpid        = uDesc.Words.w0;
        uint64_t      const uCr3         = pVCpu->cpum.GstCtx.cr3;
        switch (u64InvvpidType)
        {
            case VMXTLBFLUSHVPID_INDIV_ADDR:
            {
                if (uVpid != 0)
                {
                    if (IEM_IS_CANONICAL(GCPtrInvAddr))
                    {
                        /* Invalidate mappings for the linear address tagged with VPID. */
                        /** @todo PGM support for VPID? Currently just flush everything. */
                        PGMFlushTLB(pVCpu, uCr3, true /* fGlobal */);
                        iemVmxVmSucceed(pVCpu);
                    }
                    else
                    {
                        Log(("invvpid: invalidation address %#RGP is not canonical -> VMFail\n", GCPtrInvAddr));
                        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Invvpid_Type0InvalidAddr;
                        pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPtrInvAddr;
                        iemVmxVmFail(pVCpu, VMXINSTRERR_INVEPT_INVVPID_INVALID_OPERAND);
                    }
                }
                else
                {
                    Log(("invvpid: invalid VPID %#x for invalidation type %u -> VMFail\n", uVpid, u64InvvpidType));
                    pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Invvpid_Type0InvalidVpid;
                    pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = u64InvvpidType;
                    iemVmxVmFail(pVCpu, VMXINSTRERR_INVEPT_INVVPID_INVALID_OPERAND);
                }
                break;
            }

            case VMXTLBFLUSHVPID_SINGLE_CONTEXT:
            {
                if (uVpid != 0)
                {
                    /* Invalidate all mappings with VPID. */
                    /** @todo PGM support for VPID? Currently just flush everything. */
                    PGMFlushTLB(pVCpu, uCr3, true /* fGlobal */);
                    iemVmxVmSucceed(pVCpu);
                }
                else
                {
                    Log(("invvpid: invalid VPID %#x for invalidation type %u -> VMFail\n", uVpid, u64InvvpidType));
                    pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Invvpid_Type1InvalidVpid;
                    pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = u64InvvpidType;
                    iemVmxVmFail(pVCpu, VMXINSTRERR_INVEPT_INVVPID_INVALID_OPERAND);
                }
                break;
            }

            case VMXTLBFLUSHVPID_ALL_CONTEXTS:
            {
                /* Invalidate all mappings with non-zero VPIDs. */
                /** @todo PGM support for VPID? Currently just flush everything. */
                PGMFlushTLB(pVCpu, uCr3, true /* fGlobal */);
                iemVmxVmSucceed(pVCpu);
                break;
            }

            case VMXTLBFLUSHVPID_SINGLE_CONTEXT_RETAIN_GLOBALS:
            {
                if (uVpid != 0)
                {
                    /* Invalidate all mappings with VPID except global translations. */
                    /** @todo PGM support for VPID? Currently just flush everything. */
                    PGMFlushTLB(pVCpu, uCr3, true /* fGlobal */);
                    iemVmxVmSucceed(pVCpu);
                }
                else
                {
                    Log(("invvpid: invalid VPID %#x for invalidation type %u -> VMFail\n", uVpid, u64InvvpidType));
                    pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Invvpid_Type3InvalidVpid;
                    pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = uVpid;
                    iemVmxVmFail(pVCpu, VMXINSTRERR_INVEPT_INVVPID_INVALID_OPERAND);
                }
                break;
            }
            IEM_NOT_REACHED_DEFAULT_CASE_RET();
        }
        rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }
    return rcStrict;
}


/**
 * Interface for HM and EM to emulate the INVVPID instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pExitInfo       Pointer to the VM-exit information.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedInvvpid(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(pExitInfo->cbInstr, 4);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK | CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI);
    Assert(pExitInfo);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);

    uint8_t const  iEffSeg          = pExitInfo->InstrInfo.Inv.iSegReg;
    uint8_t const  cbInstr          = pExitInfo->cbInstr;
    RTGCPTR const  GCPtrInvvpidDesc = pExitInfo->GCPtrEffAddr;
    uint64_t const u64InvvpidType   = IEM_IS_64BIT_CODE(pVCpu)
                                    ? iemGRegFetchU64(pVCpu, pExitInfo->InstrInfo.Inv.iReg2)
                                    : iemGRegFetchU32(pVCpu, pExitInfo->InstrInfo.Inv.iReg2);
    VBOXSTRICTRC rcStrict = iemVmxInvvpid(pVCpu, cbInstr, iEffSeg, GCPtrInvvpidDesc, u64InvvpidType, pExitInfo);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT

/**
 * INVEPT instruction execution worker.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu               The cross context virtual CPU structure.
 * @param   cbInstr             The instruction length in bytes.
 * @param   iEffSeg             The segment of the invept descriptor.
 * @param   GCPtrInveptDesc     The address of invept descriptor.
 * @param   u64InveptType       The invalidation type.
 * @param   pExitInfo           Pointer to the VM-exit information. Optional, can be
 *                              NULL.
 *
 * @remarks Common VMX instruction checks are already expected to by the caller,
 *          i.e. VMX operation, CR4.VMXE, Real/V86 mode, EFER/CS.L checks.
 */
static VBOXSTRICTRC iemVmxInvept(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iEffSeg, RTGCPTR GCPtrInveptDesc,
                                 uint64_t u64InveptType, PCVMXVEXITINFO pExitInfo) RT_NOEXCEPT
{
    /* Check if EPT is supported, otherwise raise #UD. */
    if (!IEM_GET_GUEST_CPU_FEATURES(pVCpu)->fVmxEpt)
        return iemRaiseUndefinedOpcode(pVCpu);

    /* Nested-guest intercept. */
    if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        if (pExitInfo)
            return iemVmxVmexitInstrWithInfo(pVCpu, pExitInfo);
        return iemVmxVmexitInstrNeedsInfo(pVCpu, VMX_EXIT_INVEPT, VMXINSTRID_NONE, cbInstr);
    }

    /* CPL. */
    if (IEM_GET_CPL(pVCpu) != 0)
    {
        Log(("invept: CPL != 0 -> #GP(0)\n"));
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /*
     * Validate INVEPT invalidation type.
     *
     * The instruction specifies exactly ONE of the supported invalidation types.
     *
     * Each of the types has a bit in IA32_VMX_EPT_VPID_CAP MSR specifying if it is
     * supported. In theory, it's possible for a CPU to not support flushing individual
     * addresses but all the other types or any other combination. We do not take any
     * shortcuts here by assuming the types we currently expose to the guest.
     */
    uint64_t const fCaps = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64EptVpidCaps;
    bool const fInveptSupported = RT_BF_GET(fCaps, VMX_BF_EPT_VPID_CAP_INVEPT);
    bool const fTypeSingleCtx   = RT_BF_GET(fCaps, VMX_BF_EPT_VPID_CAP_INVEPT_SINGLE_CTX);
    bool const fTypeAllCtx      = RT_BF_GET(fCaps, VMX_BF_EPT_VPID_CAP_INVEPT_ALL_CTX);

    bool afSupportedTypes[4];
    afSupportedTypes[0] = false;
    afSupportedTypes[1] = fTypeSingleCtx;
    afSupportedTypes[2] = fTypeAllCtx;
    afSupportedTypes[3] = false;

    if (   fInveptSupported
        && !(u64InveptType & ~(uint64_t)VMX_INVEPT_VALID_MASK)
        && afSupportedTypes[u64InveptType & 3])
    { /* likely */ }
    else
    {
        Log(("invept: invalid/unsupported invvpid type %#x -> VMFail\n", u64InveptType));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Invept_TypeInvalid;
        pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = u64InveptType;
        iemVmxVmFail(pVCpu, VMXINSTRERR_INVEPT_INVVPID_INVALID_OPERAND);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /*
     * Fetch the invept descriptor from guest memory.
     */
    RTUINT128U uDesc;
    VBOXSTRICTRC rcStrict = iemMemFetchDataU128(pVCpu, &uDesc, iEffSeg, GCPtrInveptDesc);
    if (rcStrict == VINF_SUCCESS)
    {
        /*
         * Validate the descriptor.
         *
         * The Intel spec. does not explicit say the INVEPT instruction fails when reserved
         * bits in the descriptor are set, but it -does- for INVVPID. Until we test on real
         * hardware, it's assumed INVEPT behaves the same as INVVPID in this regard. It's
         * better to be strict in our emulation until proven otherwise.
         *
         * UPDATE: Hyper-V enabled Windows 10 Pro guests do NOT clear the reserved bits in
         * the descriptor. Hence, I've disabled this check for now, see @bugref{10318#c122}.
         */
#if 0
        if (uDesc.s.Hi)
        {
            Log(("invept: reserved bits set in invept descriptor %#RX64 -> VMFail\n", uDesc.s.Hi));
            pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Invept_DescRsvd;
            pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = uDesc.s.Hi;
            iemVmxVmFail(pVCpu, VMXINSTRERR_INVEPT_INVVPID_INVALID_OPERAND);
            return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }
#endif

        /*
         * Flush TLB mappings based on the EPT type.
         */
        if (u64InveptType == VMXTLBFLUSHEPT_SINGLE_CONTEXT)
        {
            uint64_t const GCPhysEptPtr = uDesc.s.Lo;
            int const rc = iemVmxVmentryCheckEptPtr(pVCpu, GCPhysEptPtr, NULL /* enmDiag */);
            if (RT_SUCCESS(rc))
            { /* likely */ }
            else
            {
                Log(("invept: EPTP invalid %#RX64 -> VMFail\n", GCPhysEptPtr));
                pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Invept_EptpInvalid;
                pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPhysEptPtr;
                iemVmxVmFail(pVCpu, VMXINSTRERR_INVEPT_INVVPID_INVALID_OPERAND);
                return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
            }
        }

        /** @todo PGM support for EPT tags? Currently just flush everything. */
        IEM_CTX_ASSERT(pVCpu, CPUMCTX_EXTRN_CR3);
        uint64_t const uCr3 = pVCpu->cpum.GstCtx.cr3;
        PGMFlushTLB(pVCpu, uCr3, true /* fGlobal */);

        iemVmxVmSucceed(pVCpu);
        rcStrict = iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    return rcStrict;
}


/**
 * Interface for HM and EM to emulate the INVEPT instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pExitInfo       Pointer to the VM-exit information.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedInvept(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(pExitInfo->cbInstr, 4);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK | CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI);
    Assert(pExitInfo);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);

    uint8_t const  iEffSeg          = pExitInfo->InstrInfo.Inv.iSegReg;
    uint8_t const  cbInstr          = pExitInfo->cbInstr;
    RTGCPTR const  GCPtrInveptDesc  = pExitInfo->GCPtrEffAddr;
    uint64_t const u64InveptType    = IEM_IS_64BIT_CODE(pVCpu)
                                    ? iemGRegFetchU64(pVCpu, pExitInfo->InstrInfo.Inv.iReg2)
                                    : iemGRegFetchU32(pVCpu, pExitInfo->InstrInfo.Inv.iReg2);
    VBOXSTRICTRC rcStrict = iemVmxInvept(pVCpu, cbInstr, iEffSeg, GCPtrInveptDesc, u64InveptType, pExitInfo);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}

#endif /* VBOX_WITH_NESTED_HWVIRT_VMX_EPT */

/**
 * VMXON instruction execution worker.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure.
 * @param   cbInstr     The instruction length in bytes.
 * @param   iEffSeg     The effective segment register to use with @a
 *                      GCPtrVmxon.
 * @param   GCPtrVmxon  The linear address of the VMXON pointer.
 * @param   pExitInfo   Pointer to the VM-exit information. Optional, can be NULL.
 *
 * @remarks Common VMX instruction checks are already expected to by the caller,
 *          i.e. CR4.VMXE, Real/V86 mode, EFER/CS.L checks.
 */
static VBOXSTRICTRC iemVmxVmxon(PVMCPUCC pVCpu, uint8_t cbInstr, uint8_t iEffSeg,
                                RTGCPHYS GCPtrVmxon, PCVMXVEXITINFO pExitInfo) RT_NOEXCEPT
{
    if (!IEM_VMX_IS_ROOT_MODE(pVCpu))
    {
        /* CPL. */
        if (IEM_GET_CPL(pVCpu) == 0)
        { /* likely */ }
        else
        {
            Log(("vmxon: CPL %u -> #GP(0)\n", IEM_GET_CPL(pVCpu)));
            pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmxon_Cpl;
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }

        /* A20M (A20 Masked) mode. */
        if (PGMPhysIsA20Enabled(pVCpu))
        { /* likely */ }
        else
        {
            Log(("vmxon: A20M mode -> #GP(0)\n"));
            pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmxon_A20M;
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }

        /* CR0. */
        {
            /*
             * CR0 MB1 bits.
             *
             * We use VMX_V_CR0_FIXED0 below to ensure CR0.PE and CR0.PG are always set
             * while executing VMXON. CR0.PE and CR0.PG are only allowed to be clear
             * when the guest running in VMX non-root mode with unrestricted-guest control
             * enabled in the VMCS.
             */
            uint64_t const uCr0Fixed0 = VMX_V_CR0_FIXED0;
            if ((pVCpu->cpum.GstCtx.cr0 & uCr0Fixed0) == uCr0Fixed0)
            { /* likely */ }
            else
            {
                Log(("vmxon: CR0 fixed0 bits cleared -> #GP(0)\n"));
                pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmxon_Cr0Fixed0;
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }

            /* CR0 MBZ bits. */
            uint64_t const uCr0Fixed1 = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64Cr0Fixed1;
            if (!(pVCpu->cpum.GstCtx.cr0 & ~uCr0Fixed1))
            { /* likely */ }
            else
            {
                Log(("vmxon: CR0 fixed1 bits set -> #GP(0)\n"));
                pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmxon_Cr0Fixed1;
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }
        }

        /* CR4. */
        {
            /* CR4 MB1 bits. */
            uint64_t const uCr4Fixed0 = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64Cr4Fixed0;
            if ((pVCpu->cpum.GstCtx.cr4 & uCr4Fixed0) == uCr4Fixed0)
            { /* likely */ }
            else
            {
                Log(("vmxon: CR4 fixed0 bits cleared -> #GP(0)\n"));
                pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmxon_Cr4Fixed0;
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }

            /* CR4 MBZ bits. */
            uint64_t const uCr4Fixed1 = pVCpu->cpum.GstCtx.hwvirt.vmx.Msrs.u64Cr4Fixed1;
            if (!(pVCpu->cpum.GstCtx.cr4 & ~uCr4Fixed1))
            { /* likely */ }
            else
            {
                Log(("vmxon: CR4 fixed1 bits set -> #GP(0)\n"));
                pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmxon_Cr4Fixed1;
                return iemRaiseGeneralProtectionFault0(pVCpu);
            }
        }

        /* Feature control MSR's LOCK and VMXON bits. */
        uint64_t const uMsrFeatCtl = CPUMGetGuestIa32FeatCtrl(pVCpu);
        if ((uMsrFeatCtl & (MSR_IA32_FEATURE_CONTROL_LOCK | MSR_IA32_FEATURE_CONTROL_VMXON))
                        == (MSR_IA32_FEATURE_CONTROL_LOCK | MSR_IA32_FEATURE_CONTROL_VMXON))
        { /* likely */ }
        else
        {
            Log(("vmxon: Feature control lock bit or VMXON bit cleared -> #GP(0)\n"));
            pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmxon_MsrFeatCtl;
            return iemRaiseGeneralProtectionFault0(pVCpu);
        }

        /* Get the VMXON pointer from the location specified by the source memory operand. */
        RTGCPHYS GCPhysVmxon;
        VBOXSTRICTRC rcStrict = iemMemFetchDataU64(pVCpu, &GCPhysVmxon, iEffSeg, GCPtrVmxon);
        if (RT_LIKELY(rcStrict == VINF_SUCCESS))
        { /* likely */ }
        else
        {
            Log(("vmxon: Failed to read VMXON region physaddr from %#RGv, rc=%Rrc\n", GCPtrVmxon, VBOXSTRICTRC_VAL(rcStrict)));
            pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmxon_PtrMap;
            pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPtrVmxon;
            return rcStrict;
        }

        /* VMXON region pointer alignment. */
        if (!(GCPhysVmxon & X86_PAGE_4K_OFFSET_MASK))
        { /* likely */ }
        else
        {
            Log(("vmxon: VMXON region pointer not page-aligned -> VMFailInvalid\n"));
            pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmxon_PtrAlign;
            pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPhysVmxon;
            iemVmxVmFailInvalid(pVCpu);
            return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }

        /* VMXON physical-address width limits. */
        if (!(GCPhysVmxon >> IEM_GET_GUEST_CPU_FEATURES(pVCpu)->cVmxMaxPhysAddrWidth))
        { /* likely */ }
        else
        {
            Log(("vmxon: VMXON region pointer extends beyond physical-address width -> VMFailInvalid\n"));
            pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmxon_PtrWidth;
            pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPhysVmxon;
            iemVmxVmFailInvalid(pVCpu);
            return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }

        /* Ensure VMXON region is not MMIO, ROM etc. This is not an Intel requirement but a
           restriction imposed by our implementation. */
        if (PGMPhysIsGCPhysNormal(pVCpu->CTX_SUFF(pVM), GCPhysVmxon))
        { /* likely */ }
        else
        {
            Log(("vmxon: VMXON region not normal memory -> VMFailInvalid\n"));
            pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag  = kVmxVDiag_Vmxon_PtrAbnormal;
            pVCpu->cpum.GstCtx.hwvirt.vmx.uDiagAux = GCPhysVmxon;
            iemVmxVmFailInvalid(pVCpu);
            return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }

        /* Read the VMCS revision ID from the VMXON region. */
        VMXVMCSREVID VmcsRevId;
        int rc = PGMPhysSimpleReadGCPhys(pVCpu->CTX_SUFF(pVM), &VmcsRevId, GCPhysVmxon, sizeof(VmcsRevId));
        if (RT_SUCCESS(rc))
        { /* likely */ }
        else
        {
            Log(("vmxon: Failed to read VMXON region at %#RGp, rc=%Rrc\n", GCPhysVmxon, rc));
            pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmxon_PtrReadPhys;
            return rc;
        }

        /* Verify the VMCS revision specified by the guest matches what we reported to the guest. */
        if (RT_LIKELY(VmcsRevId.u == VMX_V_VMCS_REVISION_ID))
        { /* likely */ }
        else
        {
            /* Revision ID mismatch. */
            if (!VmcsRevId.n.fIsShadowVmcs)
            {
                Log(("vmxon: VMCS revision mismatch, expected %#RX32 got %#RX32 -> VMFailInvalid\n", VMX_V_VMCS_REVISION_ID,
                     VmcsRevId.n.u31RevisionId));
                pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmxon_VmcsRevId;
                iemVmxVmFailInvalid(pVCpu);
                return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
            }

            /* Shadow VMCS disallowed. */
            Log(("vmxon: Shadow VMCS -> VMFailInvalid\n"));
            pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmxon_ShadowVmcs;
            iemVmxVmFailInvalid(pVCpu);
            return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
        }

        /*
         * Record that we're in VMX operation, block INIT, block and disable A20M.
         */
        pVCpu->cpum.GstCtx.hwvirt.vmx.GCPhysVmxon    = GCPhysVmxon;
        IEM_VMX_CLEAR_CURRENT_VMCS(pVCpu);
        pVCpu->cpum.GstCtx.hwvirt.vmx.fInVmxRootMode = true;

        /* Clear address-range monitoring. */
        EMMonitorWaitClear(pVCpu);
        /** @todo NSTVMX: Intel PT. */

        iemVmxVmSucceed(pVCpu);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }
    if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        /* Nested-guest intercept. */
        if (pExitInfo)
            return iemVmxVmexitInstrWithInfo(pVCpu, pExitInfo);
        return iemVmxVmexitInstrNeedsInfo(pVCpu, VMX_EXIT_VMXON, VMXINSTRID_NONE, cbInstr);
    }

    Assert(IEM_VMX_IS_ROOT_MODE(pVCpu));

    /* CPL. */
    if (IEM_GET_CPL(pVCpu) > 0)
    {
        Log(("vmxon: In VMX root mode: CPL %u -> #GP(0)\n", IEM_GET_CPL(pVCpu)));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmxon_VmxRootCpl;
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /* VMXON when already in VMX root mode. */
    iemVmxVmFail(pVCpu, VMXINSTRERR_VMXON_IN_VMXROOTMODE);
    pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmxon_VmxAlreadyRoot;
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Interface for HM and EM to emulate the VMXON instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu           The cross context virtual CPU structure of the calling EMT.
 * @param   pExitInfo       Pointer to the VM-exit information.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedVmxon(PVMCPUCC pVCpu, PCVMXVEXITINFO pExitInfo)
{
    Assert(pExitInfo);
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(pExitInfo->cbInstr, 3);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_MEM_MASK | CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);

    uint8_t const iEffSeg    = pExitInfo->InstrInfo.VmxXsave.iSegReg;
    uint8_t const cbInstr    = pExitInfo->cbInstr;
    RTGCPTR const GCPtrVmxon = pExitInfo->GCPtrEffAddr;
    VBOXSTRICTRC rcStrict = iemVmxVmxon(pVCpu, cbInstr, iEffSeg, GCPtrVmxon, pExitInfo);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Implements 'VMXOFF'.
 *
 * @remarks Common VMX instruction checks are already expected to by the caller,
 *          i.e. CR4.VMXE, Real/V86 mode, EFER/CS.L checks.
 */
IEM_CIMPL_DEF_0(iemCImpl_vmxoff)
{
    /* Nested-guest intercept. */
    if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
        return iemVmxVmexitInstr(pVCpu, VMX_EXIT_VMXOFF, cbInstr);

    /* CPL. */
    if (IEM_GET_CPL(pVCpu) == 0)
    { /* likely */ }
    else
    {
        Log(("vmxoff: CPL %u -> #GP(0)\n", IEM_GET_CPL(pVCpu)));
        pVCpu->cpum.GstCtx.hwvirt.vmx.enmDiag = kVmxVDiag_Vmxoff_Cpl;
        return iemRaiseGeneralProtectionFault0(pVCpu);
    }

    /* Dual monitor treatment of SMIs and SMM. */
    uint64_t const fSmmMonitorCtl = CPUMGetGuestIa32SmmMonitorCtl(pVCpu);
    if (!(fSmmMonitorCtl & MSR_IA32_SMM_MONITOR_VALID))
    { /* likely */ }
    else
    {
        iemVmxVmFail(pVCpu, VMXINSTRERR_VMXOFF_DUAL_MON);
        return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
    }

    /* Record that we're no longer in VMX root operation, block INIT, block and disable A20M. */
    pVCpu->cpum.GstCtx.hwvirt.vmx.fInVmxRootMode = false;
    Assert(!pVCpu->cpum.GstCtx.hwvirt.vmx.fInVmxNonRootMode);

    if (fSmmMonitorCtl & MSR_IA32_SMM_MONITOR_VMXOFF_UNBLOCK_SMI)
    { /** @todo NSTVMX: Unblock SMI. */ }

    EMMonitorWaitClear(pVCpu);
    /** @todo NSTVMX: Unblock and enable A20M. */

    iemVmxVmSucceed(pVCpu);
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * Interface for HM and EM to emulate the VMXOFF instruction.
 *
 * @returns Strict VBox status code.
 * @param   pVCpu       The cross context virtual CPU structure of the calling EMT.
 * @param   cbInstr     The instruction length in bytes.
 * @thread  EMT(pVCpu)
 */
VMM_INT_DECL(VBOXSTRICTRC) IEMExecDecodedVmxoff(PVMCPUCC pVCpu, uint8_t cbInstr)
{
    IEMEXEC_ASSERT_INSTR_LEN_RETURN(cbInstr, 3);
    IEM_CTX_ASSERT(pVCpu, IEM_CPUMCTX_EXTRN_EXEC_DECODED_NO_MEM_MASK | CPUMCTX_EXTRN_INHIBIT_INT | CPUMCTX_EXTRN_INHIBIT_NMI);

    iemInitExec(pVCpu, 0 /*fExecOpts*/);
    VBOXSTRICTRC rcStrict = IEM_CIMPL_CALL_0(iemCImpl_vmxoff);
    Assert(!pVCpu->iem.s.cActiveMappings);
    return iemUninitExecAndFiddleStatusAndMaybeReenter(pVCpu, rcStrict);
}


/**
 * Implements 'VMXON'.
 */
IEM_CIMPL_DEF_2(iemCImpl_vmxon, uint8_t, iEffSeg, RTGCPTR, GCPtrVmxon)
{
    return iemVmxVmxon(pVCpu, cbInstr, iEffSeg, GCPtrVmxon, NULL /* pExitInfo */);
}


/**
 * Implements 'VMLAUNCH'.
 */
IEM_CIMPL_DEF_0(iemCImpl_vmlaunch)
{
    return iemVmxVmlaunchVmresume(pVCpu, cbInstr, VMXINSTRID_VMLAUNCH);
}


/**
 * Implements 'VMRESUME'.
 */
IEM_CIMPL_DEF_0(iemCImpl_vmresume)
{
    return iemVmxVmlaunchVmresume(pVCpu, cbInstr, VMXINSTRID_VMRESUME);
}


/**
 * Implements 'VMPTRLD'.
 */
IEM_CIMPL_DEF_2(iemCImpl_vmptrld, uint8_t, iEffSeg, RTGCPTR, GCPtrVmcs)
{
    return iemVmxVmptrld(pVCpu, cbInstr, iEffSeg, GCPtrVmcs, NULL /* pExitInfo */);
}


/**
 * Implements 'VMPTRST'.
 */
IEM_CIMPL_DEF_2(iemCImpl_vmptrst, uint8_t, iEffSeg, RTGCPTR, GCPtrVmcs)
{
    return iemVmxVmptrst(pVCpu, cbInstr, iEffSeg, GCPtrVmcs, NULL /* pExitInfo */);
}


/**
 * Implements 'VMCLEAR'.
 */
IEM_CIMPL_DEF_2(iemCImpl_vmclear, uint8_t, iEffSeg, RTGCPTR, GCPtrVmcs)
{
    return iemVmxVmclear(pVCpu, cbInstr, iEffSeg, GCPtrVmcs, NULL /* pExitInfo */);
}


/**
 * Implements 'VMWRITE' register.
 */
IEM_CIMPL_DEF_2(iemCImpl_vmwrite_reg, uint64_t, u64Val, uint64_t, u64VmcsField)
{
    return iemVmxVmwrite(pVCpu, cbInstr, UINT8_MAX /* iEffSeg */, u64Val, u64VmcsField, NULL /* pExitInfo */);
}


/**
 * Implements 'VMWRITE' memory.
 */
IEM_CIMPL_DEF_3(iemCImpl_vmwrite_mem, uint8_t, iEffSeg, RTGCPTR, GCPtrVal, uint32_t, u64VmcsField)
{
    return iemVmxVmwrite(pVCpu, cbInstr, iEffSeg, GCPtrVal, u64VmcsField,  NULL /* pExitInfo */);
}


/**
 * Implements 'VMREAD' register (64-bit).
 */
IEM_CIMPL_DEF_2(iemCImpl_vmread_reg64, uint64_t *, pu64Dst, uint64_t, u64VmcsField)
{
    return iemVmxVmreadReg64(pVCpu, cbInstr, pu64Dst, u64VmcsField, NULL /* pExitInfo */);
}


/**
 * Implements 'VMREAD' register (32-bit).
 */
IEM_CIMPL_DEF_2(iemCImpl_vmread_reg32, uint64_t *, pu64Dst, uint32_t, u32VmcsField)
{
    VBOXSTRICTRC const rcStrict = iemVmxVmreadReg32(pVCpu, cbInstr, (uint32_t *)pu64Dst, u32VmcsField, NULL /* pExitInfo */);
    /* Zero the high part of the register on success. */
    if (rcStrict == VINF_SUCCESS)
        *pu64Dst = (uint32_t)*pu64Dst;
    return rcStrict;
}


/**
 * Implements 'VMREAD' memory, 64-bit register.
 */
IEM_CIMPL_DEF_3(iemCImpl_vmread_mem_reg64, uint8_t, iEffSeg, RTGCPTR, GCPtrDst, uint32_t, u64VmcsField)
{
    return iemVmxVmreadMem(pVCpu, cbInstr, iEffSeg, GCPtrDst, u64VmcsField, NULL /* pExitInfo */);
}


/**
 * Implements 'VMREAD' memory, 32-bit register.
 */
IEM_CIMPL_DEF_3(iemCImpl_vmread_mem_reg32, uint8_t, iEffSeg, RTGCPTR, GCPtrDst, uint32_t, u32VmcsField)
{
    return iemVmxVmreadMem(pVCpu, cbInstr, iEffSeg, GCPtrDst, u32VmcsField, NULL /* pExitInfo */);
}


/**
 * Implements 'INVVPID'.
 */
IEM_CIMPL_DEF_3(iemCImpl_invvpid, uint8_t, iEffSeg, RTGCPTR, GCPtrInvvpidDesc, uint64_t, uInvvpidType)
{
    return iemVmxInvvpid(pVCpu, cbInstr, iEffSeg, GCPtrInvvpidDesc, uInvvpidType, NULL /* pExitInfo */);
}


#if defined(VBOX_WITH_NESTED_HWVIRT_VMX_EPT) || defined(VBOX_WITH_IEM_RECOMPILER) /* HACK ALERT: Linking trick. */
/**
 * Implements 'INVEPT'.
 */
IEM_CIMPL_DEF_3(iemCImpl_invept, uint8_t, iEffSeg, RTGCPTR, GCPtrInveptDesc, uint64_t, uInveptType)
{
# ifdef VBOX_WITH_NESTED_HWVIRT_VMX_EPT
    return iemVmxInvept(pVCpu, cbInstr, iEffSeg, GCPtrInveptDesc, uInveptType, NULL /* pExitInfo */);
# else
    RT_NOREF(pVCpu, cbInstr, iEffSeg, GCPtrInveptDesc, uInveptType);
    AssertFailedReturn(VERR_IEM_ASPECT_NOT_IMPLEMENTED);
# endif
}
#endif


/**
 * Implements VMX's implementation of PAUSE.
 */
IEM_CIMPL_DEF_0(iemCImpl_vmx_pause)
{
    if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
    {
        VBOXSTRICTRC rcStrict = iemVmxVmexitInstrPause(pVCpu, cbInstr);
        if (rcStrict != VINF_VMX_INTERCEPT_NOT_ACTIVE)
            return rcStrict;
    }

    /*
     * Outside VMX non-root operation or if the PAUSE instruction does not cause
     * a VM-exit, the instruction operates normally.
     */
    return iemRegAddToRipAndFinishingClearingRF(pVCpu, cbInstr);
}


/**
 * @callback_method_impl{FNPGMPHYSHANDLER, VMX APIC-access page accesses}
 *
 * @remarks The @a uUser argument is currently unused.
 */
DECLCALLBACK(VBOXSTRICTRC) iemVmxApicAccessPageHandler(PVMCC pVM, PVMCPUCC pVCpu, RTGCPHYS GCPhysFault, void *pvPhys,
                                                       void *pvBuf, size_t cbBuf, PGMACCESSTYPE enmAccessType,
                                                       PGMACCESSORIGIN enmOrigin, uint64_t uUser)
{
    RT_NOREF3(pvPhys, enmOrigin, uUser);

    RTGCPHYS const GCPhysAccessBase = GCPhysFault & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;
    if (CPUMIsGuestInVmxNonRootMode(IEM_GET_CTX(pVCpu)))
    {
        Assert(CPUMIsGuestVmxProcCtls2Set(IEM_GET_CTX(pVCpu), VMX_PROC_CTLS2_VIRT_APIC_ACCESS));
        Assert(CPUMGetGuestVmxApicAccessPageAddrEx(IEM_GET_CTX(pVCpu)) == GCPhysAccessBase);

        uint32_t const fAccess   = enmAccessType == PGMACCESSTYPE_WRITE ? IEM_ACCESS_DATA_W : IEM_ACCESS_DATA_R;
        uint16_t const offAccess = GCPhysFault & GUEST_PAGE_OFFSET_MASK;

        LogFlowFunc(("Fault at %#RGp (cbBuf=%u fAccess=%#x)\n", GCPhysFault, cbBuf, fAccess));
        VBOXSTRICTRC rcStrict = iemVmxVirtApicAccessMem(pVCpu, offAccess, cbBuf, pvBuf, fAccess);
        if (RT_FAILURE(rcStrict))
            return rcStrict;

        /* Any access on this APIC-access page has been handled, caller should not carry out the access. */
        return VINF_SUCCESS;
    }

    LogFunc(("Accessed outside VMX non-root mode, deregistering page handler for %#RGp\n", GCPhysAccessBase));
    int rc = PGMHandlerPhysicalDeregister(pVM, GCPhysAccessBase);
    if (RT_FAILURE(rc))
        return rc;

    /* Instruct the caller of this handler to perform the read/write as normal memory. */
    return VINF_PGM_HANDLER_DO_DEFAULT;
}


# ifndef IN_RING3
/**
 * @callback_method_impl{FNPGMRZPHYSPFHANDLER,
 *      \#PF access handler callback for guest VMX APIC-access page.}
 */
DECLCALLBACK(VBOXSTRICTRC) iemVmxApicAccessPagePfHandler(PVMCC pVM, PVMCPUCC pVCpu, RTGCUINT uErr, PCPUMCTX pCtx,
                                                         RTGCPTR pvFault, RTGCPHYS GCPhysFault, uint64_t uUser)

{
    RT_NOREF3(pVM, pCtx, uUser);

    /*
     * Handle the VMX APIC-access page only when the guest is in VMX non-root mode.
     * Otherwise we must deregister the page and allow regular RAM access.
     * Failing to do so lands us with endless EPT VM-exits.
     */
    RTGCPHYS const GCPhysPage = GCPhysFault & ~(RTGCPHYS)GUEST_PAGE_OFFSET_MASK;
    if (CPUMIsGuestInVmxNonRootMode(IEM_GET_CTX(pVCpu)))
    {
        Assert(CPUMIsGuestVmxProcCtls2Set(IEM_GET_CTX(pVCpu), VMX_PROC_CTLS2_VIRT_APIC_ACCESS));
        Assert(CPUMGetGuestVmxApicAccessPageAddrEx(IEM_GET_CTX(pVCpu)) == GCPhysPage);

        /*
         * Check if the access causes an APIC-access VM-exit.
         */
        uint32_t fAccess;
        if (uErr & X86_TRAP_PF_ID)
            fAccess = IEM_ACCESS_INSTRUCTION;
        else if (uErr & X86_TRAP_PF_RW)
            fAccess = IEM_ACCESS_DATA_W;
        else
            fAccess = IEM_ACCESS_DATA_R;

        RTGCPHYS const GCPhysNestedFault = (RTGCPHYS)pvFault;
        uint16_t const offAccess         = GCPhysNestedFault & GUEST_PAGE_OFFSET_MASK;
        bool const fIntercept = iemVmxVirtApicIsMemAccessIntercepted(pVCpu, offAccess, 1 /* cbAccess */, fAccess);
        LogFlowFunc(("#PF at %#RGp (GCPhysNestedFault=%#RGp offAccess=%#x)\n", GCPhysFault, GCPhysNestedFault, offAccess));
        if (fIntercept)
        {
            /*
             * Query the source VM-exit (from the execution engine) that caused this access
             * within the APIC-access page. Currently only HM is supported.
             */
            AssertMsg(VM_IS_HM_ENABLED(pVM),
                      ("VM-exit auxiliary info. fetching not supported for execution engine %d\n", pVM->bMainExecutionEngine));

            HMEXITAUX HmExitAux;
            RT_ZERO(HmExitAux);
            int const rc = HMR0GetExitAuxInfo(pVCpu, &HmExitAux, HMVMX_READ_EXIT_INSTR_LEN
                                                               | HMVMX_READ_EXIT_QUALIFICATION
                                                               | HMVMX_READ_IDT_VECTORING_INFO
                                                               | HMVMX_READ_IDT_VECTORING_ERROR_CODE);
            AssertRC(rc);

            /*
             * Verify the VM-exit reason must be an EPT violation.
             * Other accesses should go through the other handler (iemVmxApicAccessPageHandler).
             * Refer to @bugref{10092#c33s} for a more detailed explanation.
             */
            AssertMsgReturn(HmExitAux.Vmx.uReason == VMX_EXIT_EPT_VIOLATION,
                            ("Unexpected call to APIC-access page #PF handler for %#RGp offAcesss=%u uErr=%#RGx uReason=%u\n",
                             GCPhysPage, offAccess, uErr, HmExitAux.Vmx.uReason), VERR_IEM_IPE_7);

            /*
             * Construct the virtual APIC-access VM-exit.
             */
            VMXAPICACCESS enmAccess;
            if (HmExitAux.Vmx.u64Qual & VMX_EXIT_QUAL_EPT_LINEAR_ADDR_VALID)
            {
                if (VMX_IDT_VECTORING_INFO_IS_VALID(HmExitAux.Vmx.uIdtVectoringInfo))
                    enmAccess = VMXAPICACCESS_LINEAR_EVENT_DELIVERY;
                else if (fAccess == IEM_ACCESS_INSTRUCTION)
                    enmAccess = VMXAPICACCESS_LINEAR_INSTR_FETCH;
                else if (fAccess & IEM_ACCESS_TYPE_WRITE)
                    enmAccess = VMXAPICACCESS_LINEAR_WRITE;
                else
                    enmAccess = VMXAPICACCESS_LINEAR_READ;

                /* For linear-address accesss the instruction length must be valid. */
                AssertMsg(HmExitAux.Vmx.cbInstr > 0,
                          ("Invalid APIC-access VM-exit instruction length. cbInstr=%u\n", HmExitAux.Vmx.cbInstr));
            }
            else
            {
                if (VMX_IDT_VECTORING_INFO_IS_VALID(HmExitAux.Vmx.uIdtVectoringInfo))
                    enmAccess = VMXAPICACCESS_PHYSICAL_EVENT_DELIVERY;
                else
                {
                    /** @todo How to distinguish between monitoring/trace vs other instructions
                     *        here? */
                    enmAccess = VMXAPICACCESS_PHYSICAL_INSTR;
                }

                /* For physical accesses the instruction length is undefined, we zero it for safety and consistency. */
                HmExitAux.Vmx.cbInstr = 0;
            }

            /*
             * Raise the APIC-access VM-exit.
             */
            LogFlowFunc(("Raising APIC-access VM-exit from #PF handler at offset %#x\n", offAccess));
            VMXVEXITINFO const ExitInfo
                = VMXVEXITINFO_INIT_WITH_QUAL_AND_INSTR_LEN(VMX_EXIT_APIC_ACCESS,
                                                              RT_BF_MAKE(VMX_BF_EXIT_QUAL_APIC_ACCESS_OFFSET, offAccess)
                                                            | RT_BF_MAKE(VMX_BF_EXIT_QUAL_APIC_ACCESS_TYPE,   enmAccess),
                                                            HmExitAux.Vmx.cbInstr);
            VMXVEXITEVENTINFO const ExitEventInfo = VMXVEXITEVENTINFO_INIT_ONLY_IDT(HmExitAux.Vmx.uIdtVectoringInfo,
                                                                                    HmExitAux.Vmx.uIdtVectoringErrCode);
            VBOXSTRICTRC const      rcStrict      = iemVmxVmexitApicAccessWithInfo(pVCpu, &ExitInfo, &ExitEventInfo);
            return iemExecStatusCodeFiddling(pVCpu, rcStrict);
        }

        /*
         * The access isn't intercepted, which means it needs to be virtualized.
         *
         * This requires emulating the instruction because we need the bytes being
         * read/written by the instruction not just the offset being accessed within
         * the APIC-access page (which we derive from the faulting address).
         */
        LogFlowFunc(("Access at offset %#x not intercepted -> VINF_EM_RAW_EMULATE_INSTR\n", offAccess));
        return VINF_EM_RAW_EMULATE_INSTR;
    }

    /** @todo This isn't ideal but works for now as nested-hypervisors generally play
     *        nice because the spec states that this page should be modified only when
     *        no CPU refers to it VMX non-root mode. Nonetheless, we could use an atomic
     *        reference counter to ensure the aforementioned condition before
     *        de-registering the page.  */
    LogFunc(("Accessed outside VMX non-root mode, deregistering page handler for %#RGp\n", GCPhysPage));
    int const rc = PGMHandlerPhysicalDeregister(pVM, GCPhysPage);
    if (RT_FAILURE(rc))
        return rc;

    return VINF_SUCCESS;
}
# endif /* !IN_RING3 */

#endif /* VBOX_WITH_NESTED_HWVIRT_VMX */


/**
 * Implements 'VMCALL'.
 */
IEM_CIMPL_DEF_0(iemCImpl_vmcall)
{
    pVCpu->iem.s.cPotentialExits++;

#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
    /* Nested-guest intercept. */
    if (IEM_VMX_IS_NON_ROOT_MODE(pVCpu))
        return iemVmxVmexitInstr(pVCpu, VMX_EXIT_VMCALL, cbInstr);
#endif

    /* Join forces with vmmcall. */
    return IEM_CIMPL_CALL_1(iemCImpl_Hypercall, OP_VMCALL);
}

