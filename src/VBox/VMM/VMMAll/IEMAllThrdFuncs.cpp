/* $Id: IEMAllThrdFuncs.cpp 107184 2024-11-26 22:32:44Z knut.osmundsen@oracle.com $ */
/** @file
 * IEM - Instruction Decoding and Emulation, Threaded Functions.
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
#ifndef LOG_GROUP /* defined when included by tstIEMCheckMc.cpp */
# define LOG_GROUP LOG_GROUP_IEM
#endif
#define VMCPU_INCL_CPUM_GST_CTX
#define IEM_WITH_OPAQUE_DECODER_STATE
#include <VBox/vmm/iem.h>
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/apic.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/iom.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/hm.h>
#include <VBox/vmm/nem.h>
#include <VBox/vmm/gim.h>
#ifdef VBOX_WITH_NESTED_HWVIRT_SVM
# include <VBox/vmm/em.h>
# include <VBox/vmm/hm_svm.h>
#endif
#ifdef VBOX_WITH_NESTED_HWVIRT_VMX
# include <VBox/vmm/hmvmxinline.h>
#endif
#include <VBox/vmm/tm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/dbgftrace.h>
#include "IEMInternal.h"
#include <VBox/vmm/vmcc.h>
#include <VBox/log.h>
#include <VBox/err.h>
#include <VBox/param.h>
#include <VBox/dis.h>
#include <VBox/disopcode-x86-amd64.h>
#include <iprt/asm-math.h>
#include <iprt/assert.h>
#include <iprt/string.h>
#include <iprt/x86.h>

#include "IEMInline.h"
#include "IEMMc.h"

#include "IEMThreadedFunctions.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** Variant of IEM_MC_ADVANCE_RIP_AND_FINISH with instruction length as param
 *  and only used when we're in 16-bit code on a pre-386 CPU. */
#define IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC16(a_cbInstr, a_rcNormal) \
    return iemRegAddToIp16AndFinishingNoFlags(pVCpu, a_cbInstr, a_rcNormal)

/** Variant of IEM_MC_ADVANCE_RIP_AND_FINISH with instruction length as param
 *  and used for 16-bit and 32-bit code on 386 and later CPUs. */
#define IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC32(a_cbInstr, a_rcNormal) \
    return iemRegAddToEip32AndFinishingNoFlags(pVCpu, a_cbInstr, a_rcNormal)

/** Variant of IEM_MC_ADVANCE_RIP_AND_FINISH with instruction length as param
 *  and only used when we're in 64-bit code. */
#define IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC64(a_cbInstr, a_rcNormal) \
    return iemRegAddToRip64AndFinishingNoFlags(pVCpu, a_cbInstr, a_rcNormal)


/** Variant of IEM_MC_ADVANCE_RIP_AND_FINISH with instruction length as param
 *  and only used when we're in 16-bit code on a pre-386 CPU and we need to
 *  check and clear flags. */
#define IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_cbInstr, a_rcNormal) \
    return iemRegAddToIp16AndFinishingClearingRF(pVCpu, a_cbInstr, a_rcNormal)

/** Variant of IEM_MC_ADVANCE_RIP_AND_FINISH with instruction length as param
 *  and used for 16-bit and 32-bit code on 386 and later CPUs and we need to
 *  check and clear flags. */
#define IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_cbInstr, a_rcNormal) \
    return iemRegAddToEip32AndFinishingClearingRF(pVCpu, a_cbInstr, a_rcNormal)

/** Variant of IEM_MC_ADVANCE_RIP_AND_FINISH with instruction length as param
 *  and only used when we're in 64-bit code and we need to check and clear
 *  flags. */
#define IEM_MC_ADVANCE_RIP_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_cbInstr, a_rcNormal) \
    return iemRegAddToRip64AndFinishingClearingRF(pVCpu, a_cbInstr, a_rcNormal)

#undef  IEM_MC_ADVANCE_RIP_AND_FINISH


/** Variant of IEM_MC_REL_JMP_S8_AND_FINISH with instruction length as extra
 *  parameter, for use in 16-bit code on a pre-386 CPU. */
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC16(a_i8, a_cbInstr, a_rcNormal) \
    return iemRegIp16RelativeJumpS8AndFinishNoFlags(pVCpu, a_cbInstr, (a_i8), a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S8_AND_FINISH with instruction length and operand
 * size as extra parameters, for use in 16-bit and 32-bit code on 386 and
 * later CPUs. */
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC32(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal) \
    return iemRegEip32RelativeJumpS8AndFinishNoFlags(pVCpu, a_cbInstr, (a_i8), a_enmEffOpSize, a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S8_AND_FINISH with instruction length and operand
 * size as extra parameters, for use in flat 32-bit code on 386 and later
 * CPUs. */
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC32_FLAT(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal) \
    return iemRegEip32RelativeJumpS8FlatAndFinishNoFlags(pVCpu, a_cbInstr, (a_i8), a_enmEffOpSize, a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S8_AND_FINISH with instruction length and operand
 * size as extra parameters, for use in 64-bit code. */
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC64(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal) \
    return iemRegRip64RelativeJumpS8AndFinishNoFlags(pVCpu, a_cbInstr, (a_i8), a_enmEffOpSize, a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S8_AND_FINISH with instruction length and operand
 * size as extra parameters, for use in 64-bit code jumping within a page. */
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC64_INTRAPG(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal) \
    return iemRegRip64RelativeJumpS8IntraPgAndFinishNoFlags(pVCpu, a_cbInstr, (a_i8), a_enmEffOpSize, a_rcNormal)


/** Variant of IEM_MC_REL_JMP_S8_AND_FINISH with instruction length as extra
 *  parameter, for use in 16-bit code on a pre-386 CPU and we need to check and
 *  clear flags. */
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_i8, a_cbInstr, a_rcNormal) \
    return iemRegIp16RelativeJumpS8AndFinishClearingRF(pVCpu, a_cbInstr, (a_i8), a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S8_AND_FINISH with instruction length and operand
 * size as extra parameters, for use in 16-bit and 32-bit code on 386 and
 * later CPUs and we need to check and clear flags. */
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal) \
    return iemRegEip32RelativeJumpS8AndFinishClearingRF(pVCpu, a_cbInstr, (a_i8), a_enmEffOpSize, a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S8_AND_FINISH with instruction length and operand
 * size as extra parameters, for use in flat 32-bit code on 386 and later
 * CPUs and we need to check and clear flags. */
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC32_FLAT_WITH_FLAGS(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal) \
    return iemRegEip32RelativeJumpS8FlatAndFinishClearingRF(pVCpu, a_cbInstr, (a_i8), a_enmEffOpSize, a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S8_AND_FINISH with instruction length and operand
 * size as extra parameters, for use in 64-bit code and we need to check and
 * clear flags. */
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal) \
    return iemRegRip64RelativeJumpS8AndFinishClearingRF(pVCpu, a_cbInstr, (a_i8), a_enmEffOpSize, a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S8_AND_FINISH with instruction length and operand
 * size as extra parameters, for use in 64-bit code jumping within a page and we
 * need to check and clear flags. */
#define IEM_MC_REL_JMP_S8_AND_FINISH_THREADED_PC64_INTRAPG_WITH_FLAGS(a_i8, a_cbInstr, a_enmEffOpSize, a_rcNormal) \
    return iemRegRip64RelativeJumpS8IntraPgAndFinishClearingRF(pVCpu, a_cbInstr, (a_i8), a_enmEffOpSize, a_rcNormal)

#undef  IEM_MC_REL_JMP_S8_AND_FINISH


/** Variant of IEM_MC_REL_JMP_S16_AND_FINISH with instruction length as
 *  param, for use in 16-bit code on a pre-386 CPU. */
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC16(a_i16, a_cbInstr, a_rcNormal) \
    return iemRegEip32RelativeJumpS16AndFinishNoFlags(pVCpu, a_cbInstr, (a_i16), a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S16_AND_FINISH with instruction length as
 *  param, for use in 16-bit and 32-bit code on 386 and later CPUs. */
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC32(a_i16, a_cbInstr, a_rcNormal) \
    return iemRegEip32RelativeJumpS16AndFinishNoFlags(pVCpu, a_cbInstr, (a_i16), a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S16_AND_FINISH with instruction length as
 *  param, for use in flat 32-bit code on 386 and later CPUs. */
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC32_FLAT(a_i16, a_cbInstr, a_rcNormal) \
    return iemRegEip32RelativeJumpS16FlatAndFinishNoFlags(pVCpu, a_cbInstr, (a_i16), a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S16_AND_FINISH with instruction length as
 *  param, for use in 64-bit code. */
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC64(a_i16, a_cbInstr, a_rcNormal) \
    return iemRegRip64RelativeJumpS16AndFinishNoFlags(pVCpu, a_cbInstr, (a_i16), a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S16_AND_FINISH with instruction length as
 *  param, for use in 64-bit code jumping with a page.
 * @note No special function for this, there is nothing to save here.  */
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC64_INTRAPG(a_i16, a_cbInstr, a_rcNormal) \
    return iemRegRip64RelativeJumpS16AndFinishNoFlags(pVCpu, a_cbInstr, (a_i16), a_rcNormal)


/** Variant of IEM_MC_REL_JMP_S16_AND_FINISH with instruction length as
 *  param, for use in 16-bit code on a pre-386 CPU and we need to check and
 *  clear flags. */
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_i16, a_cbInstr, a_rcNormal) \
    return iemRegEip32RelativeJumpS16AndFinishClearingRF(pVCpu, a_cbInstr, (a_i16), a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S16_AND_FINISH with instruction length as
 *  param, for use in 16-bit and 32-bit code on 386 and later CPUs and we need
 *  to check and clear flags. */
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_i16, a_cbInstr, a_rcNormal) \
    return iemRegEip32RelativeJumpS16AndFinishClearingRF(pVCpu, a_cbInstr, (a_i16), a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S16_AND_FINISH with instruction length as
 *  param, for use in flat 32-bit code on 386 and later CPUs and we need
 *  to check and clear flags. */
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC32_FLAT_WITH_FLAGS(a_i16, a_cbInstr, a_rcNormal) \
    return iemRegEip32RelativeJumpS16FlatAndFinishClearingRF(pVCpu, a_cbInstr, (a_i16), a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S16_AND_FINISH with instruction length as
 *  param, for use in 64-bit code and we need to check and clear flags. */
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_i16, a_cbInstr, a_rcNormal) \
    return iemRegRip64RelativeJumpS16AndFinishClearingRF(pVCpu, a_cbInstr, (a_i16), a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S16_AND_FINISH with instruction length as
 *  param, for use in 64-bit code jumping within a page and we need to check and
 *  clear flags.
 * @note No special function for this, there is nothing to save here.  */
#define IEM_MC_REL_JMP_S16_AND_FINISH_THREADED_PC64_INTRAPG_WITH_FLAGS(a_i16, a_cbInstr, a_rcNormal) \
    return iemRegRip64RelativeJumpS16AndFinishClearingRF(pVCpu, a_cbInstr, (a_i16), a_rcNormal)

#undef  IEM_MC_REL_JMP_S16_AND_FINISH


/** Variant of IEM_MC_REL_JMP_S32_AND_FINISH with instruction length as
 *  an extra parameter - dummy for pre-386 variations not eliminated by the
 *  python script. */
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC16(a_i32, a_cbInstr, a_rcNormal) \
    do { RT_NOREF(pVCpu, a_i32, a_cbInstr, a_rcNormal); AssertFailedReturn(VERR_IEM_IPE_9); } while (0)

/** Variant of IEM_MC_REL_JMP_S32_AND_FINISH with instruction length as
 *  an extra parameter, for use in 16-bit and 32-bit code on 386+. */
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC32(a_i32, a_cbInstr, a_rcNormal) \
    return iemRegEip32RelativeJumpS32AndFinishNoFlags(pVCpu, a_cbInstr, (a_i32), a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S32_AND_FINISH with instruction length as
 *  an extra parameter, for use in flat 32-bit code on 386+. */
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC32_FLAT(a_i32, a_cbInstr, a_rcNormal) \
    return iemRegEip32RelativeJumpS32FlatAndFinishNoFlags(pVCpu, a_cbInstr, (a_i32), a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S32_AND_FINISH with instruction length as
 *  an extra parameter, for use in 64-bit code. */
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC64(a_i32, a_cbInstr, a_rcNormal) \
    return iemRegRip64RelativeJumpS32AndFinishNoFlags(pVCpu, a_cbInstr, (a_i32), a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S32_AND_FINISH with instruction length as
 *  an extra parameter, for use in 64-bit code jumping within a page. */
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC64_INTRAPG(a_i32, a_cbInstr, a_rcNormal) \
    return iemRegRip64RelativeJumpS32IntraPgAndFinishNoFlags(pVCpu, a_cbInstr, (a_i32), a_rcNormal)


/** Variant of IEM_MC_REL_JMP_S32_AND_FINISH with instruction length as
 *  an extra parameter - dummy for pre-386 variations not eliminated by the
 *  python script. */
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_i32, a_cbInstr, a_rcNormal) \
    do { RT_NOREF(pVCpu, a_i32, a_cbInstr, a_rcNormal); AssertFailedReturn(VERR_IEM_IPE_9); } while (0)

/** Variant of IEM_MC_REL_JMP_S32_AND_FINISH with instruction length as
 *  an extra parameter, for use in 16-bit and 32-bit code on 386+ and we need
 *  to check and clear flags. */
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_i32, a_cbInstr, a_rcNormal) \
    return iemRegEip32RelativeJumpS32AndFinishClearingRF(pVCpu, a_cbInstr, (a_i32), a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S32_AND_FINISH with instruction length as
 *  an extra parameter, for use in flat 32-bit code on 386+ and we need
 *  to check and clear flags. */
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC32_FLAT_WITH_FLAGS(a_i32, a_cbInstr, a_rcNormal) \
    return iemRegEip32RelativeJumpS32FlatAndFinishClearingRF(pVCpu, a_cbInstr, (a_i32), a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S32_AND_FINISH with instruction length as
 *  an extra parameter, for use in 64-bit code and we need to check and clear
 *  flags. */
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_i32, a_cbInstr, a_rcNormal) \
    return iemRegRip64RelativeJumpS32AndFinishClearingRF(pVCpu, a_cbInstr, (a_i32), a_rcNormal)

/** Variant of IEM_MC_REL_JMP_S32_AND_FINISH with instruction length as
 *  an extra parameter, for use in 64-bit code jumping within a page and we need
 *  to check and clear flags. */
#define IEM_MC_REL_JMP_S32_AND_FINISH_THREADED_PC64_INTRAPG_WITH_FLAGS(a_i32, a_cbInstr, a_rcNormal) \
    return iemRegRip64RelativeJumpS32IntraPgAndFinishClearingRF(pVCpu, a_cbInstr, (a_i32), a_rcNormal)

#undef  IEM_MC_REL_JMP_S32_AND_FINISH



/** Variant of IEM_MC_SET_RIP_U16_AND_FINISH for pre-386 targets. */
#define IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC16(a_u16NewIP) \
    return iemRegRipJumpU16AndFinishNoFlags((pVCpu), (a_u16NewIP))

/** Variant of IEM_MC_SET_RIP_U16_AND_FINISH for 386+ targets. */
#define IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC32(a_u16NewIP) \
    return iemRegRipJumpU16AndFinishNoFlags((pVCpu), (a_u16NewIP))

/** Variant of IEM_MC_SET_RIP_U16_AND_FINISH for use in 64-bit code. */
#define IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC64(a_u16NewIP) \
    return iemRegRipJumpU16AndFinishNoFlags((pVCpu), (a_u16NewIP))

/** Variant of IEM_MC_SET_RIP_U16_AND_FINISH for pre-386 targets that checks and
 *  clears flags. */
#define IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_u16NewIP) \
    return iemRegRipJumpU16AndFinishClearingRF((pVCpu), (a_u16NewIP), 0 /* cbInstr - not used */)

/** Variant of IEM_MC_SET_RIP_U16_AND_FINISH for 386+ targets that checks and
 *  clears flags. */
#define IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_u16NewIP) \
    return iemRegRipJumpU16AndFinishClearingRF((pVCpu), (a_u16NewIP), 0 /* cbInstr - not used */)

/** Variant of IEM_MC_SET_RIP_U16_AND_FINISH for use in 64-bit code that checks and
 *  clears flags. */
#define IEM_MC_SET_RIP_U16_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_u16NewIP) \
    return iemRegRipJumpU16AndFinishClearingRF((pVCpu), (a_u16NewIP), 0 /* cbInstr - not used */)

#undef IEM_MC_SET_RIP_U16_AND_FINISH


/** Variant of IEM_MC_SET_RIP_U32_AND_FINISH for 386+ targets. */
#define IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC32(a_u32NewEIP) \
    return iemRegRipJumpU32AndFinishNoFlags((pVCpu), (a_u32NewEIP))

/** Variant of IEM_MC_SET_RIP_U32_AND_FINISH for use in 64-bit code. */
#define IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC64(a_u32NewEIP) \
    return iemRegRipJumpU32AndFinishNoFlags((pVCpu), (a_u32NewEIP))

/** Variant of IEM_MC_SET_RIP_U32_AND_FINISH for 386+ targets that checks and
 *  clears flags. */
#define IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_u32NewEIP) \
    return iemRegRipJumpU32AndFinishClearingRF((pVCpu), (a_u32NewEIP), 0 /* cbInstr - not used */)

/** Variant of IEM_MC_SET_RIP_U32_AND_FINISH for use in 64-bit code that checks
 *  and clears flags. */
#define IEM_MC_SET_RIP_U32_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_u32NewEIP) \
    return iemRegRipJumpU32AndFinishClearingRF((pVCpu), (a_u32NewEIP), 0 /* cbInstr - not used */)

#undef IEM_MC_SET_RIP_U32_AND_FINISH


/** Variant of IEM_MC_SET_RIP_U64_AND_FINISH for use in 64-bit code. */
#define IEM_MC_SET_RIP_U64_AND_FINISH_THREADED_PC64(a_u32NewEIP) \
    return iemRegRipJumpU64AndFinishNoFlags((pVCpu), (a_u32NewEIP))

/** Variant of IEM_MC_SET_RIP_U64_AND_FINISH for use in 64-bit code that checks
 *  and clears flags. */
#define IEM_MC_SET_RIP_U64_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_u32NewEIP) \
    return iemRegRipJumpU64AndFinishClearingRF((pVCpu), (a_u32NewEIP), 0 /* cbInstr - not used */)

#undef IEM_MC_SET_RIP_U64_AND_FINISH


/** Variant of IEM_MC_REL_CALL_S16_AND_FINISH with instruction length as
 *  param, for use in 16-bit code on a pre-386 CPU. */
#define IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC16(a_i16, a_cbInstr) \
    return iemRegRipRelativeCallS16AndFinishNoFlags(pVCpu, a_cbInstr, (a_i16))

/** Variant of IEM_MC_REL_CALL_S16_AND_FINISH with instruction length as
 *  param, for use in 16-bit and 32-bit code on 386 and later CPUs. */
#define IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC32(a_i16, a_cbInstr) \
    return iemRegRipRelativeCallS16AndFinishNoFlags(pVCpu, a_cbInstr, (a_i16))

/** Variant of IEM_MC_REL_CALL_S16_AND_FINISH with instruction length as
 *  param, for use in 64-bit code. */
#define IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC64(a_i16, a_cbInstr) \
    return iemRegRipRelativeCallS16AndFinishNoFlags(pVCpu, a_cbInstr, (a_i16))


/** Variant of IEM_MC_REL_CALL_S16_AND_FINISH with instruction length as
 *  param, for use in 16-bit code on a pre-386 CPU and we need to check and
 *  clear flags. */
#define IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_i16, a_cbInstr) \
    return iemRegRipRelativeCallS16AndFinishClearingRF(pVCpu, a_cbInstr, (a_i16))

/** Variant of IEM_MC_REL_CALL_S16_AND_FINISH with instruction length as
 *  param, for use in 16-bit and 32-bit code on 386 and later CPUs and we need
 *  to check and clear flags. */
#define IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_i16, a_cbInstr) \
    return iemRegRipRelativeCallS16AndFinishClearingRF(pVCpu, a_cbInstr, (a_i16))

/** Variant of IEM_MC_REL_CALL_S16_AND_FINISH with instruction length as
 *  param, for use in 64-bit code and we need to check and clear flags. */
#define IEM_MC_REL_CALL_S16_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_i16, a_cbInstr) \
    return iemRegRipRelativeCallS16AndFinishClearingRF(pVCpu, a_cbInstr, (a_i16))

#undef  IEM_MC_REL_CALL_S16_AND_FINISH


/** Variant of IEM_MC_REL_JMP_S32_AND_FINISH with instruction length as
 *  an extra parameter - dummy for pre-386 variations not eliminated by the
 *  python script. */
#define IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC16(a_i32, a_cbInstr) \
    do { RT_NOREF(pVCpu, a_i32, a_cbInstr); AssertFailedReturn(VERR_IEM_IPE_9); } while (0)

/** Variant of IEM_MC_REL_JMP_S32_AND_FINISH with instruction length as
 *  an extra parameter, for use in 16-bit and 32-bit code on 386+. */
#define IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC32(a_i32, a_cbInstr) \
    return iemRegEip32RelativeCallS32AndFinishNoFlags(pVCpu, a_cbInstr, (a_i32))

/** Variant of IEM_MC_REL_JMP_S32_AND_FINISH with instruction length as
 *  an extra parameter, for use in 64-bit code on 386+. */
#define IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC64(a_i32, a_cbInstr) \
    do { RT_NOREF(pVCpu, a_i32, a_cbInstr); AssertFailedReturn(VERR_IEM_IPE_9); } while (0)

/** Variant of IEM_MC_REL_CALL_S32_AND_FINISH with instruction length as
 *  an extra parameter - dummy for pre-386 variations not eliminated by the
 *  python script. */
#define IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_i32, a_cbInstr) \
    do { RT_NOREF(pVCpu, a_i32, a_cbInstr); AssertFailedReturn(VERR_IEM_IPE_9); } while (0)

/** Variant of IEM_MC_REL_CALL_S32_AND_FINISH with instruction length as
 *  an extra parameter, for use in 16-bit and 32-bit code on 386+ and we need
 *  to check and clear flags. */
#define IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_i32, a_cbInstr) \
    return iemRegEip32RelativeCallS32AndFinishClearingRF(pVCpu, a_cbInstr, (a_i32))

/** Variant of IEM_MC_REL_CALL_S32_AND_FINISH with instruction length as
 *  an extra parameter, for use in 64-bit code on 386+ and we need
 *  to check and clear flags - dummy for variations not eliminated by the python script. */
#define IEM_MC_REL_CALL_S32_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_i32, a_cbInstr) \
    do { RT_NOREF(pVCpu, a_i32, a_cbInstr); AssertFailedReturn(VERR_IEM_IPE_9); } while (0)


#undef  IEM_MC_REL_CALL_S32_AND_FINISH


/** Variant of IEM_MC_REL_CALL_S64_AND_FINISH with instruction length as
 *  an extra parameter, for use in 32-bit code. */
#define IEM_MC_REL_CALL_S64_AND_FINISH_THREADED_PC32(a_i64, a_cbInstr) \
    do { RT_NOREF(pVCpu, a_i64, a_cbInstr); AssertFailedReturn(VERR_IEM_IPE_9); } while (0)

/** Variant of IEM_MC_REL_CALL_S64_AND_FINISH with instruction length as
 *  an extra parameter, for use in 64-bit code. */
#define IEM_MC_REL_CALL_S64_AND_FINISH_THREADED_PC64(a_i64, a_cbInstr) \
    return iemRegRip64RelativeCallS64AndFinishNoFlags(pVCpu, a_cbInstr, (a_i64))


/** Variant of IEM_MC_REL_CALL_S64_AND_FINISH with instruction length as
 *  an extra parameter, for use in 16-bit and 32-bit code on 386+ and we need
 *  to check and clear flags - dummy for variations not eliminated by the python script. */
#define IEM_MC_REL_CALL_S64_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_i64, a_cbInstr) \
    do { RT_NOREF(pVCpu, a_i64, a_cbInstr); AssertFailedReturn(VERR_IEM_IPE_9); } while (0)

/** Variant of IEM_MC_REL_CALL_S64_AND_FINISH with instruction length as
 *  an extra parameter, for use in 64-bit code and we need to check and clear
 *  flags. */
#define IEM_MC_REL_CALL_S64_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_i64, a_cbInstr) \
    return iemRegRip64RelativeCallS64AndFinishClearingRF(pVCpu, a_cbInstr, (a_i64))

#undef  IEM_MC_REL_CALL_S64_AND_FINISH


/** Variant of IEM_MC_IND_CALL_U16_AND_FINISH for pre-386 targets. */
#define IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC16(a_u16NewIP, a_cbInstr) \
    return iemRegIp16IndirectCallU16AndFinishNoFlags((pVCpu), a_cbInstr, (a_u16NewIP))

/** Variant of IEM_MC_IND_CALL_U16_AND_FINISH for 386+ targets. */
#define IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC32(a_u16NewIP, a_cbInstr) \
    return iemRegEip32IndirectCallU16AndFinishNoFlags((pVCpu), a_cbInstr, (a_u16NewIP))

/** Variant of IEM_MC_IND_CALL_U16_AND_FINISH for use in 64-bit code. */
#define IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC64(a_u16NewIP, a_cbInstr) \
    do { RT_NOREF(pVCpu, a_u16NewIP, a_cbInstr); AssertFailedReturn(VERR_IEM_IPE_9); } while (0)

/** Variant of IEM_MC_IND_CALL_U16_AND_FINISH for pre-386 targets that checks and
 *  clears flags. */
#define IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_u16NewIP, a_cbInstr) \
    return iemRegIp16IndirectCallU16AndFinishClearingRF((pVCpu), a_cbInstr, (a_u16NewIP))

/** Variant of IEM_MC_IND_CALL_U16_AND_FINISH for 386+ targets that checks and
 *  clears flags. */
#define IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_u16NewIP, a_cbInstr) \
    return iemRegEip32IndirectCallU16AndFinishClearingRF((pVCpu), a_cbInstr, (a_u16NewIP))

/** Variant of IEM_MC_IND_CALL_U16_AND_FINISH for use in 64-bit code that checks and
 *  clears flags. */
#define IEM_MC_IND_CALL_U16_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_u16NewIP, a_cbInstr) \
    do { RT_NOREF(pVCpu, a_u16NewIP, a_cbInstr); AssertFailedReturn(VERR_IEM_IPE_9); } while (0)

#undef IEM_MC_IND_CALL_U16_AND_FINISH


/** Variant of IEM_MC_IND_CALL_U32_AND_FINISH for 386+ targets. */
#define IEM_MC_IND_CALL_U32_AND_FINISH_THREADED_PC32(a_u32NewEIP, a_cbInstr) \
    return iemRegEip32IndirectCallU32AndFinishNoFlags((pVCpu), a_cbInstr, (a_u32NewEIP))

/** Variant of IEM_MC_IND_CALL_U32_AND_FINISH for use in 64-bit code. */
#define IEM_MC_IND_CALL_U32_AND_FINISH_THREADED_PC64(a_u32NewEIP, a_cbInstr) \
    do { RT_NOREF(pVCpu, a_u32NewEIP, a_cbInstr); AssertFailedReturn(VERR_IEM_IPE_9); } while (0)

/** Variant of IEM_MC_IND_CALL_U32_AND_FINISH for 386+ targets that checks and
 *  clears flags. */
#define IEM_MC_IND_CALL_U32_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_u32NewEIP, a_cbInstr) \
    return iemRegEip32IndirectCallU32AndFinishClearingRF((pVCpu), a_cbInstr, (a_u32NewEIP))

/** Variant of IEM_MC_IND_CALL_U32_AND_FINISH for use in 64-bit code that checks
 *  and clears flags. */
#define IEM_MC_IND_CALL_U32_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_u32NewEIP, a_cbInstr) \
    do { RT_NOREF(pVCpu, a_u32NewEIP, a_cbInstr); AssertFailedReturn(VERR_IEM_IPE_9); } while (0)

#undef IEM_MC_IND_CALL_U32_AND_FINISH


/** Variant of IEM_MC_IND_CALL_U64_AND_FINISH for use in 64-bit code. */
#define IEM_MC_IND_CALL_U64_AND_FINISH_THREADED_PC64(a_u32NewRIP, a_cbInstr) \
    return iemRegRip64IndirectCallU64AndFinishNoFlags((pVCpu), a_cbInstr, (a_u32NewRIP))

/** Variant of IEM_MC_IND_CALL_U64_AND_FINISH for use in 64-bit code that checks
 *  and clears flags. */
#define IEM_MC_IND_CALL_U64_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_u32NewRIP, a_cbInstr) \
    return iemRegRip64IndirectCallU64AndFinishClearingRF((pVCpu), a_cbInstr, (a_u32NewRIP))

#undef IEM_MC_IND_CALL_U64_AND_FINISH


/** Variant of IEM_MC_RETN_AND_FINISH for pre-386 targets. */
#define IEM_MC_RETN_AND_FINISH_THREADED_PC16(a_cbPopArgs, a_cbInstr) \
    return iemRegRipNearReturnAndFinishNoFlags((pVCpu), a_cbInstr, (a_cbPopArgs), IEMMODE_16BIT)

/** Variant of IEM_MC_RETN_AND_FINISH for 386+ targets. */
#define IEM_MC_RETN_AND_FINISH_THREADED_PC32(a_cbPopArgs, a_cbInstr, a_enmEffOpSize) \
    return iemRegRipNearReturnAndFinishNoFlags((pVCpu), a_cbInstr, (a_cbPopArgs), (a_enmEffOpSize))

/** Variant of IEM_MC_RETN_AND_FINISH for use in 64-bit code. */
#define IEM_MC_RETN_AND_FINISH_THREADED_PC64(a_cbPopArgs, a_cbInstr, a_enmEffOpSize) \
    return iemRegRipNearReturnAndFinishNoFlags((pVCpu), a_cbInstr, (a_cbPopArgs), (a_enmEffOpSize))

/** Variant of IEM_MC_RETN_AND_FINISH for pre-386 targets that checks and
 *  clears flags. */
#define IEM_MC_RETN_AND_FINISH_THREADED_PC16_WITH_FLAGS(a_cbPopArgs, a_cbInstr) \
    return iemRegRipNearReturnAndFinishClearingRF((pVCpu), a_cbInstr, (a_cbPopArgs), IEMMODE_16BIT)

/** Variant of IEM_MC_RETN_AND_FINISH for 386+ targets that checks and
 *  clears flags. */
#define IEM_MC_RETN_AND_FINISH_THREADED_PC32_WITH_FLAGS(a_cbPopArgs, a_cbInstr, a_enmEffOpSize) \
    return iemRegRipNearReturnAndFinishClearingRF((pVCpu), a_cbInstr, (a_cbPopArgs), (a_enmEffOpSize))

/** Variant of IEM_MC_RETN_AND_FINISH for use in 64-bit code that checks and
 *  clears flags. */
#define IEM_MC_RETN_AND_FINISH_THREADED_PC64_WITH_FLAGS(a_cbPopArgs, a_cbInstr, a_enmEffOpSize) \
    return iemRegRipNearReturnAndFinishClearingRF((pVCpu), a_cbInstr, (a_cbPopArgs), (a_enmEffOpSize))

#undef IEM_MC_RETN_AND_FINISH


/** Variant of IEM_MC_CALC_RM_EFF_ADDR with additional parameters, 16-bit. */
#define IEM_MC_CALC_RM_EFF_ADDR_THREADED_16(a_GCPtrEff, a_bRm, a_u16Disp) \
    (a_GCPtrEff) = iemOpHlpCalcRmEffAddrThreadedAddr16(pVCpu, a_bRm, a_u16Disp)

/** Variant of IEM_MC_CALC_RM_EFF_ADDR with additional parameters, 32-bit. */
#define IEM_MC_CALC_RM_EFF_ADDR_THREADED_32(a_GCPtrEff, a_bRm, a_uSibAndRspOffset, a_u32Disp) \
    (a_GCPtrEff) = iemOpHlpCalcRmEffAddrThreadedAddr32(pVCpu, a_bRm, a_uSibAndRspOffset, a_u32Disp)

/** Variant of IEM_MC_CALC_RM_EFF_ADDR with additional parameters. */
#define IEM_MC_CALC_RM_EFF_ADDR_THREADED_64(a_GCPtrEff, a_bRmEx, a_uSibAndRspOffset, a_u32Disp, a_cbImm) \
    (a_GCPtrEff) = iemOpHlpCalcRmEffAddrThreadedAddr64(pVCpu, a_bRmEx, a_uSibAndRspOffset, a_u32Disp, a_cbImm)

/** Variant of IEM_MC_CALC_RM_EFF_ADDR with additional parameters. */
#define IEM_MC_CALC_RM_EFF_ADDR_THREADED_64_FSGS(a_GCPtrEff, a_bRmEx, a_uSibAndRspOffset, a_u32Disp, a_cbImm) \
    (a_GCPtrEff) = iemOpHlpCalcRmEffAddrThreadedAddr64(pVCpu, a_bRmEx, a_uSibAndRspOffset, a_u32Disp, a_cbImm)

/** Variant of IEM_MC_CALC_RM_EFF_ADDR with additional parameters.
 * @todo How did that address prefix thing work for 64-bit code again? */
#define IEM_MC_CALC_RM_EFF_ADDR_THREADED_64_ADDR32(a_GCPtrEff, a_bRmEx, a_uSibAndRspOffset, a_u32Disp, a_cbImm) \
    (a_GCPtrEff) = (uint32_t)iemOpHlpCalcRmEffAddrThreadedAddr64(pVCpu, a_bRmEx, a_uSibAndRspOffset, a_u32Disp, a_cbImm)

#undef  IEM_MC_CALC_RM_EFF_ADDR


/** Variant of IEM_MC_CALL_CIMPL_1 with explicit instruction length parameter. */
#define IEM_MC_CALL_CIMPL_1_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0) \
    return (a_pfnCImpl)(pVCpu, (a_cbInstr), a0)
#undef  IEM_MC_CALL_CIMPL_1

/** Variant of IEM_MC_CALL_CIMPL_2 with explicit instruction length parameter. */
#define IEM_MC_CALL_CIMPL_2_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1) \
    return (a_pfnCImpl)(pVCpu, (a_cbInstr), a0, a1)
#undef  IEM_MC_CALL_CIMPL_2

/** Variant of IEM_MC_CALL_CIMPL_3 with explicit instruction length parameter. */
#define IEM_MC_CALL_CIMPL_3_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2) \
    return (a_pfnCImpl)(pVCpu, (a_cbInstr), a0, a1, a2)
#undef  IEM_MC_CALL_CIMPL_3

/** Variant of IEM_MC_CALL_CIMPL_4 with explicit instruction length parameter. */
#define IEM_MC_CALL_CIMPL_4_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2, a3) \
    return (a_pfnCImpl)(pVCpu, (a_cbInstr), a0, a1, a2, a3)
#undef  IEM_MC_CALL_CIMPL_4

/** Variant of IEM_MC_CALL_CIMPL_5 with explicit instruction length parameter. */
#define IEM_MC_CALL_CIMPL_5_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2, a3, a4) \
    return (a_pfnCImpl)(pVCpu, (a_cbInstr), a0, a1, a2, a3, a4)
#undef  IEM_MC_CALL_CIMPL_5


/** Variant of IEM_MC_DEFER_TO_CIMPL_0_RET with explicit instruction
 * length parameter. */
#define IEM_MC_DEFER_TO_CIMPL_0_RET_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl) \
    return (a_pfnCImpl)(pVCpu, (a_cbInstr))
#undef  IEM_MC_DEFER_TO_CIMPL_0_RET

/** Variant of IEM_MC_DEFER_TO_CIMPL_1_RET with explicit instruction
 * length parameter. */
#define IEM_MC_DEFER_TO_CIMPL_1_RET_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0) \
    return (a_pfnCImpl)(pVCpu, (a_cbInstr), a0)
#undef  IEM_MC_DEFER_TO_CIMPL_1_RET

/** Variant of IEM_MC_CALL_CIMPL_2 with explicit instruction length parameter. */
#define IEM_MC_DEFER_TO_CIMPL_2_RET_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1) \
    return (a_pfnCImpl)(pVCpu, (a_cbInstr), a0, a1)
#undef  IEM_MC_DEFER_TO_CIMPL_2_RET

/** Variant of IEM_MC_DEFER_TO_CIMPL_3 with explicit instruction length
 *  parameter. */
#define IEM_MC_DEFER_TO_CIMPL_3_RET_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2) \
    return (a_pfnCImpl)(pVCpu, (a_cbInstr), a0, a1, a2)
#undef  IEM_MC_DEFER_TO_CIMPL_3_RET

/** Variant of IEM_MC_DEFER_TO_CIMPL_4 with explicit instruction length
 *  parameter. */
#define IEM_MC_DEFER_TO_CIMPL_4_RET_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2, a3) \
    return (a_pfnCImpl)(pVCpu, (a_cbInstr), a0, a1, a2, a3)
#undef  IEM_MC_DEFER_TO_CIMPL_4_RET

/** Variant of IEM_MC_DEFER_TO_CIMPL_5 with explicit instruction length
 *  parameter. */
#define IEM_MC_DEFER_TO_CIMPL_5_RET_THREADED(a_cbInstr, a_fFlags, a_fGstShwFlush, a_pfnCImpl, a0, a1, a2, a3, a4) \
    return (a_pfnCImpl)(pVCpu, (a_cbInstr), a0, a1, a2, a3, a4)
#undef  IEM_MC_DEFER_TO_CIMPL_5_RET


/** Variant of IEM_MC_FETCH_GREG_U8 with extended (20) register index. */
#define IEM_MC_FETCH_GREG_U8_THREADED(a_u8Dst, a_iGRegEx) \
    (a_u8Dst) = iemGRegFetchU8Ex(pVCpu, (a_iGRegEx))

/** Variant of IEM_MC_FETCH_GREG_U8_ZX_U16 with extended (20) register index. */
#define IEM_MC_FETCH_GREG_U8_ZX_U16_THREADED(a_u16Dst, a_iGRegEx) \
    (a_u16Dst) = iemGRegFetchU8Ex(pVCpu, (a_iGRegEx))

/** Variant of IEM_MC_FETCH_GREG_U8_ZX_U32 with extended (20) register index. */
#define IEM_MC_FETCH_GREG_U8_ZX_U32_THREADED(a_u32Dst, a_iGRegEx) \
    (a_u32Dst) = iemGRegFetchU8Ex(pVCpu, (a_iGRegEx))

/** Variant of IEM_MC_FETCH_GREG_U8_ZX_U64 with extended (20) register index. */
#define IEM_MC_FETCH_GREG_U8_ZX_U64_THREADED(a_u64Dst, a_iGRegEx) \
    (a_u64Dst) = iemGRegFetchU8Ex(pVCpu, (a_iGRegEx))

/** Variant of IEM_MC_FETCH_GREG_U8_SX_U16 with extended (20) register index. */
#define IEM_MC_FETCH_GREG_U8_SX_U16_THREADED(a_u16Dst, a_iGRegEx) \
    (a_u16Dst) = (int8_t)iemGRegFetchU8Ex(pVCpu, (a_iGRegEx))

/** Variant of IEM_MC_FETCH_GREG_U8_SX_U32 with extended (20) register index. */
#define IEM_MC_FETCH_GREG_U8_SX_U32_THREADED(a_u32Dst, a_iGRegEx) \
    (a_u32Dst) = (int8_t)iemGRegFetchU8Ex(pVCpu, (a_iGRegEx))
#undef IEM_MC_FETCH_GREG_U8_SX_U32

/** Variant of IEM_MC_FETCH_GREG_U8_SX_U64 with extended (20) register index. */
#define IEM_MC_FETCH_GREG_U8_SX_U64_THREADED(a_u64Dst, a_iGRegEx) \
    (a_u64Dst) = (int8_t)iemGRegFetchU8Ex(pVCpu, (a_iGRegEx))
#undef IEM_MC_FETCH_GREG_U8_SX_U64

/** Variant of IEM_MC_STORE_GREG_U8 with extended (20) register index. */
#define IEM_MC_STORE_GREG_U8_THREADED(a_iGRegEx, a_u8Value) \
    *iemGRegRefU8Ex(pVCpu, (a_iGRegEx)) = (a_u8Value)
#undef IEM_MC_STORE_GREG_U8

/** Variant of IEM_MC_STORE_GREG_U8_CONST with extended (20) register index. */
#define IEM_MC_STORE_GREG_U8_CONST_THREADED(a_iGRegEx, a_u8Value) \
    *iemGRegRefU8Ex(pVCpu, (a_iGRegEx)) = (a_u8Value)
#undef IEM_MC_STORE_GREG_U8

/** Variant of IEM_MC_REF_GREG_U8 with extended (20) register index. */
#define IEM_MC_REF_GREG_U8_THREADED(a_pu8Dst, a_iGRegEx) \
    (a_pu8Dst) = iemGRegRefU8Ex(pVCpu, (a_iGRegEx))
#undef IEM_MC_REF_GREG_U8

/** Variant of IEM_MC_REF_GREG_U8_CONST with extended (20) register index. */
#define IEM_MC_REF_GREG_U8_CONST_THREADED(a_pu8Dst, a_iGRegEx) \
    (a_pu8Dst) = iemGRegRefU8Ex(pVCpu, (a_iGRegEx))
#undef IEM_MC_REF_GREG_U8

/** Variant of IEM_MC_ADD_GREG_U8_TO_LOCAL with extended (20) register index. */
#define IEM_MC_ADD_GREG_U8_TO_LOCAL_THREADED(a_u8Value, a_iGRegEx) \
    do { (a_u8Value) += iemGRegFetchU8Ex(pVCpu, (a_iGRegEx)); } while (0)
#undef IEM_MC_ADD_GREG_U8_TO_LOCAL

/** Variant of IEM_MC_AND_GREG_U8 with extended (20) register index. */
#define IEM_MC_AND_GREG_U8_THREADED(a_iGRegEx, a_u8Value) \
    *iemGRegRefU8Ex(pVCpu, (a_iGRegEx)) &= (a_u8Value)
#undef IEM_MC_AND_GREG_U8

/** Variant of IEM_MC_OR_GREG_U8 with extended (20) register index. */
#define IEM_MC_OR_GREG_U8_THREADED(a_iGRegEx, a_u8Value) \
    *iemGRegRefU8Ex(pVCpu, (a_iGRegEx)) |= (a_u8Value)
#undef IEM_MC_OR_GREG_U8


/** For asserting that only declared output flags changed. */
#ifndef VBOX_STRICT
# define IEM_MC_ASSERT_EFLAGS(a_fEflInput, a_fEflOutput) ((void)0)
#else
# undef IEM_MC_REF_EFLAGS_EX
# define IEM_MC_REF_EFLAGS_EX(a_pEFlags, a_fEflInput, a_fEflOutput) \
        uint32_t const fEflAssert = pVCpu->cpum.GstCtx.eflags.uBoth; \
        IEM_MC_REF_EFLAGS(a_pEFlags)
# define IEM_MC_ASSERT_EFLAGS(a_fEflInput, a_fEflOutput) \
        AssertMsg((pVCpu->cpum.GstCtx.eflags.uBoth & ~(a_fEflOutput)) == (fEflAssert & ~(a_fEflOutput)), \
                  ("now %#x (%#x), was %#x (%#x) - diff %#x; a_fEflOutput=%#x\n", \
                  (pVCpu->cpum.GstCtx.eflags.uBoth & ~(a_fEflOutput)), pVCpu->cpum.GstCtx.eflags.uBoth, \
                  (fEflAssert & ~(a_fEflOutput)), fEflAssert, \
                   (pVCpu->cpum.GstCtx.eflags.uBoth ^ fEflAssert) & ~(a_fEflOutput), a_fEflOutput))
#endif



/**
 * Calculates the effective address of a ModR/M memory operand, 16-bit
 * addressing variant.
 *
 * Meant to be used via IEM_MC_CALC_RM_EFF_ADDR_THREADED_ADDR16.
 *
 * @returns The effective address.
 * @param   pVCpu               The cross context virtual CPU structure of the calling thread.
 * @param   bRm                 The ModRM byte.
 * @param   u16Disp             The displacement byte/word, if any.
 *                              RIP relative addressing.
 */
static RTGCPTR iemOpHlpCalcRmEffAddrThreadedAddr16(PVMCPUCC pVCpu, uint8_t bRm, uint16_t u16Disp) RT_NOEXCEPT
{
    Log5(("iemOpHlpCalcRmEffAddrThreadedAddr16: bRm=%#x u16Disp=%#x\n", bRm, u16Disp));
    Assert(!IEM_IS_64BIT_CODE(pVCpu));

    /* Handle the disp16 form with no registers first. */
    if ((bRm & (X86_MODRM_MOD_MASK | X86_MODRM_RM_MASK)) == 6)
    {
        Log5(("iemOpHlpCalcRmEffAddrThreadedAddr16: EffAddr=%#010RGv\n", (RTGCPTR)u16Disp));
        return u16Disp;
    }

    /* Get the displacment. */
    /** @todo we can eliminate this step by making u16Disp have this value
     *        already! */
    uint16_t u16EffAddr;
    switch ((bRm >> X86_MODRM_MOD_SHIFT) & X86_MODRM_MOD_SMASK)
    {
        case 0:  u16EffAddr = 0;                        break;
        case 1:  u16EffAddr = (int16_t)(int8_t)u16Disp; break;
        case 2:  u16EffAddr = u16Disp;                  break;
        default: AssertFailedStmt(u16EffAddr = 0);
    }

    /* Add the base and index registers to the disp. */
    switch (bRm & X86_MODRM_RM_MASK)
    {
        case 0: u16EffAddr += pVCpu->cpum.GstCtx.bx + pVCpu->cpum.GstCtx.si; break;
        case 1: u16EffAddr += pVCpu->cpum.GstCtx.bx + pVCpu->cpum.GstCtx.di; break;
        case 2: u16EffAddr += pVCpu->cpum.GstCtx.bp + pVCpu->cpum.GstCtx.si; break;
        case 3: u16EffAddr += pVCpu->cpum.GstCtx.bp + pVCpu->cpum.GstCtx.di; break;
        case 4: u16EffAddr += pVCpu->cpum.GstCtx.si; break;
        case 5: u16EffAddr += pVCpu->cpum.GstCtx.di; break;
        case 6: u16EffAddr += pVCpu->cpum.GstCtx.bp; break;
        case 7: u16EffAddr += pVCpu->cpum.GstCtx.bx; break;
    }

    Log5(("iemOpHlpCalcRmEffAddrThreadedAddr16: EffAddr=%#010RGv\n", (RTGCPTR)u16EffAddr));
    return u16EffAddr;
}


/**
 * Calculates the effective address of a ModR/M memory operand, 32-bit
 * addressing variant.
 *
 * Meant to be used via IEM_MC_CALC_RM_EFF_ADDR_THREADED_ADDR32 and
 * IEM_MC_CALC_RM_EFF_ADDR_THREADED_ADDR32FLAT.
 *
 * @returns The effective address.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 * @param   bRm                 The ModRM byte.
 * @param   uSibAndRspOffset    Two parts:
 *                                - The first 8 bits make up the SIB byte.
 *                                - The next 8 bits are the fixed RSP/ESP offse
 *                                  in case of a pop [xSP].
 * @param   u32Disp             The displacement byte/dword, if any.
 */
static RTGCPTR iemOpHlpCalcRmEffAddrThreadedAddr32(PVMCPUCC pVCpu, uint8_t bRm, uint32_t uSibAndRspOffset,
                                                   uint32_t u32Disp) RT_NOEXCEPT
{
    Log5(("iemOpHlpCalcRmEffAddrThreadedAddr32: bRm=%#x uSibAndRspOffset=%#x u32Disp=%#x\n", bRm, uSibAndRspOffset, u32Disp));

    /* Handle the disp32 form with no registers first. */
    if ((bRm & (X86_MODRM_MOD_MASK | X86_MODRM_RM_MASK)) == 5)
    {
        Log5(("iemOpHlpCalcRmEffAddrThreadedAddr32: EffAddr=%#010RGv\n", (RTGCPTR)u32Disp));
        return u32Disp;
    }

    /* Get the register (or SIB) value. */
    uint32_t u32EffAddr;
#ifdef _MSC_VER
    u32EffAddr = 0;/* MSC uninitialized variable analysis is too simple, it seems. */
#endif
    switch (bRm & X86_MODRM_RM_MASK)
    {
        case 0: u32EffAddr = pVCpu->cpum.GstCtx.eax; break;
        case 1: u32EffAddr = pVCpu->cpum.GstCtx.ecx; break;
        case 2: u32EffAddr = pVCpu->cpum.GstCtx.edx; break;
        case 3: u32EffAddr = pVCpu->cpum.GstCtx.ebx; break;
        case 4: /* SIB */
        {
            /* Get the index and scale it. */
            switch ((uSibAndRspOffset >> X86_SIB_INDEX_SHIFT) & X86_SIB_INDEX_SMASK)
            {
                case 0: u32EffAddr = pVCpu->cpum.GstCtx.eax; break;
                case 1: u32EffAddr = pVCpu->cpum.GstCtx.ecx; break;
                case 2: u32EffAddr = pVCpu->cpum.GstCtx.edx; break;
                case 3: u32EffAddr = pVCpu->cpum.GstCtx.ebx; break;
                case 4: u32EffAddr = 0; /*none */ break;
                case 5: u32EffAddr = pVCpu->cpum.GstCtx.ebp; break;
                case 6: u32EffAddr = pVCpu->cpum.GstCtx.esi; break;
                case 7: u32EffAddr = pVCpu->cpum.GstCtx.edi; break;
            }
            u32EffAddr <<= (uSibAndRspOffset >> X86_SIB_SCALE_SHIFT) & X86_SIB_SCALE_SMASK;

            /* add base */
            switch (uSibAndRspOffset & X86_SIB_BASE_MASK)
            {
                case 0: u32EffAddr += pVCpu->cpum.GstCtx.eax; break;
                case 1: u32EffAddr += pVCpu->cpum.GstCtx.ecx; break;
                case 2: u32EffAddr += pVCpu->cpum.GstCtx.edx; break;
                case 3: u32EffAddr += pVCpu->cpum.GstCtx.ebx; break;
                case 4:
                    u32EffAddr += pVCpu->cpum.GstCtx.esp;
                    u32EffAddr += uSibAndRspOffset >> 8;
                    break;
                case 5:
                    if ((bRm & X86_MODRM_MOD_MASK) != 0)
                        u32EffAddr += pVCpu->cpum.GstCtx.ebp;
                    else
                        u32EffAddr += u32Disp;
                    break;
                case 6: u32EffAddr += pVCpu->cpum.GstCtx.esi; break;
                case 7: u32EffAddr += pVCpu->cpum.GstCtx.edi; break;
            }
            break;
        }
        case 5: u32EffAddr = pVCpu->cpum.GstCtx.ebp; break;
        case 6: u32EffAddr = pVCpu->cpum.GstCtx.esi; break;
        case 7: u32EffAddr = pVCpu->cpum.GstCtx.edi; break;
    }

    /* Get and add the displacement. */
    switch ((bRm >> X86_MODRM_MOD_SHIFT) & X86_MODRM_MOD_SMASK)
    {
        case 0: break;
        case 1: u32EffAddr += (int8_t)u32Disp; break;
        case 2: u32EffAddr += u32Disp; break;
        default: AssertFailed();
    }

    Log5(("iemOpHlpCalcRmEffAddrThreadedAddr32: EffAddr=%#010RGv\n", (RTGCPTR)u32EffAddr));
    return u32EffAddr;
}


/**
 * Calculates the effective address of a ModR/M memory operand.
 *
 * Meant to be used via IEM_MC_CALC_RM_EFF_ADDR_THREADED_ADDR64.
 *
 * @returns The effective address.
 * @param   pVCpu               The cross context virtual CPU structure of the
 *                              calling thread.
 * @param   bRmEx               The ModRM byte but with bit 3 set to REX.B and
 *                              bit 4 to REX.X.  The two bits are part of the
 *                              REG sub-field, which isn't needed in this
 *                              function.
 * @param   uSibAndRspOffset    Two parts:
 *                                - The first 8 bits make up the SIB byte.
 *                                - The next 8 bits are the fixed RSP/ESP offset
 *                                  in case of a pop [xSP].
 * @param   u32Disp             The displacement byte/word/dword, if any.
 * @param   cbInstr             The size of the fully decoded instruction. Used
 *                              for RIP relative addressing.
 * @todo combine cbInstr and cbImm!
 */
static RTGCPTR iemOpHlpCalcRmEffAddrThreadedAddr64(PVMCPUCC pVCpu, uint8_t bRmEx, uint32_t uSibAndRspOffset,
                                                   uint32_t u32Disp, uint8_t cbInstr) RT_NOEXCEPT
{
    Log5(("iemOpHlpCalcRmEffAddrThreadedAddr64: bRmEx=%#x\n", bRmEx));
    Assert(IEM_IS_64BIT_CODE(pVCpu));

    uint64_t u64EffAddr;

    /* Handle the rip+disp32 form with no registers first. */
    if ((bRmEx & (X86_MODRM_MOD_MASK | X86_MODRM_RM_MASK)) == 5)
    {
        u64EffAddr = (int32_t)u32Disp;
        u64EffAddr += pVCpu->cpum.GstCtx.rip + cbInstr;
    }
    else
    {
        /* Get the register (or SIB) value. */
#ifdef _MSC_VER
        u64EffAddr = 0; /* MSC uninitialized variable analysis is too simple, it seems. */
#endif
        switch (bRmEx & (X86_MODRM_RM_MASK | 0x8)) /* bRmEx[bit 3] = REX.B */
        {
            default:
            case  0: u64EffAddr = pVCpu->cpum.GstCtx.rax; break;
            case  1: u64EffAddr = pVCpu->cpum.GstCtx.rcx; break;
            case  2: u64EffAddr = pVCpu->cpum.GstCtx.rdx; break;
            case  3: u64EffAddr = pVCpu->cpum.GstCtx.rbx; break;
            case  5: u64EffAddr = pVCpu->cpum.GstCtx.rbp; break;
            case  6: u64EffAddr = pVCpu->cpum.GstCtx.rsi; break;
            case  7: u64EffAddr = pVCpu->cpum.GstCtx.rdi; break;
            case  8: u64EffAddr = pVCpu->cpum.GstCtx.r8;  break;
            case  9: u64EffAddr = pVCpu->cpum.GstCtx.r9;  break;
            case 10: u64EffAddr = pVCpu->cpum.GstCtx.r10; break;
            case 11: u64EffAddr = pVCpu->cpum.GstCtx.r11; break;
            case 13: u64EffAddr = pVCpu->cpum.GstCtx.r13; break;
            case 14: u64EffAddr = pVCpu->cpum.GstCtx.r14; break;
            case 15: u64EffAddr = pVCpu->cpum.GstCtx.r15; break;
            /* SIB */
            case 4:
            case 12:
            {
                /* Get the index and scale it. */
                switch (  ((uSibAndRspOffset >> X86_SIB_INDEX_SHIFT) & X86_SIB_INDEX_SMASK)
                        | ((bRmEx & 0x10) >> 1)) /* bRmEx[bit 4] = REX.X */
                {
                    case  0: u64EffAddr = pVCpu->cpum.GstCtx.rax; break;
                    case  1: u64EffAddr = pVCpu->cpum.GstCtx.rcx; break;
                    case  2: u64EffAddr = pVCpu->cpum.GstCtx.rdx; break;
                    case  3: u64EffAddr = pVCpu->cpum.GstCtx.rbx; break;
                    case  4: u64EffAddr = 0; /*none */ break;
                    case  5: u64EffAddr = pVCpu->cpum.GstCtx.rbp; break;
                    case  6: u64EffAddr = pVCpu->cpum.GstCtx.rsi; break;
                    case  7: u64EffAddr = pVCpu->cpum.GstCtx.rdi; break;
                    case  8: u64EffAddr = pVCpu->cpum.GstCtx.r8;  break;
                    case  9: u64EffAddr = pVCpu->cpum.GstCtx.r9;  break;
                    case 10: u64EffAddr = pVCpu->cpum.GstCtx.r10; break;
                    case 11: u64EffAddr = pVCpu->cpum.GstCtx.r11; break;
                    case 12: u64EffAddr = pVCpu->cpum.GstCtx.r12; break;
                    case 13: u64EffAddr = pVCpu->cpum.GstCtx.r13; break;
                    case 14: u64EffAddr = pVCpu->cpum.GstCtx.r14; break;
                    case 15: u64EffAddr = pVCpu->cpum.GstCtx.r15; break;
                }
                u64EffAddr <<= (uSibAndRspOffset >> X86_SIB_SCALE_SHIFT) & X86_SIB_SCALE_SMASK;

                /* add base */
                switch ((uSibAndRspOffset & X86_SIB_BASE_MASK) | (bRmEx & 0x8)) /* bRmEx[bit 3] = REX.B */
                {
                    case  0: u64EffAddr += pVCpu->cpum.GstCtx.rax; break;
                    case  1: u64EffAddr += pVCpu->cpum.GstCtx.rcx; break;
                    case  2: u64EffAddr += pVCpu->cpum.GstCtx.rdx; break;
                    case  3: u64EffAddr += pVCpu->cpum.GstCtx.rbx; break;
                    case  4:
                        u64EffAddr += pVCpu->cpum.GstCtx.rsp;
                        u64EffAddr += uSibAndRspOffset >> 8;
                        break;
                    case  6: u64EffAddr += pVCpu->cpum.GstCtx.rsi; break;
                    case  7: u64EffAddr += pVCpu->cpum.GstCtx.rdi; break;
                    case  8: u64EffAddr += pVCpu->cpum.GstCtx.r8;  break;
                    case  9: u64EffAddr += pVCpu->cpum.GstCtx.r9;  break;
                    case 10: u64EffAddr += pVCpu->cpum.GstCtx.r10; break;
                    case 11: u64EffAddr += pVCpu->cpum.GstCtx.r11; break;
                    case 12: u64EffAddr += pVCpu->cpum.GstCtx.r12; break;
                    case 14: u64EffAddr += pVCpu->cpum.GstCtx.r14; break;
                    case 15: u64EffAddr += pVCpu->cpum.GstCtx.r15; break;
                    /* complicated encodings */
                    case 5:
                        if ((bRmEx & X86_MODRM_MOD_MASK) != 0)
                            u64EffAddr += pVCpu->cpum.GstCtx.rbp;
                        else
                            u64EffAddr += (int32_t)u32Disp;
                        break;
                    case 13:
                        if ((bRmEx & X86_MODRM_MOD_MASK) != 0)
                            u64EffAddr += pVCpu->cpum.GstCtx.r13;
                        else
                            u64EffAddr += (int32_t)u32Disp;
                        break;
                }
                break;
            }
        }

        /* Get and add the displacement. */
        switch ((bRmEx >> X86_MODRM_MOD_SHIFT) & X86_MODRM_MOD_SMASK)
        {
            case 0: break;
            case 1: u64EffAddr += (int8_t)u32Disp; break;
            case 2: u64EffAddr += (int32_t)u32Disp; break;
            default: AssertFailed();
        }
    }

    Log5(("iemOpHlpCalcRmEffAddrThreadedAddr64: EffAddr=%#010RGv\n", u64EffAddr));
    return u64EffAddr;
}


/*
 * The threaded functions.
 */
#include "IEMThreadedFunctions.cpp.h"

