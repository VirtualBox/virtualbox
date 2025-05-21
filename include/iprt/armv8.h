/** @file
 * IPRT - ARMv8 (AArch64 and AArch32) Structures and Definitions.
 */

/*
 * Copyright (C) 2023-2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_armv8_h
#define IPRT_INCLUDED_armv8_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#ifndef VBOX_FOR_DTRACE_LIB
# include <iprt/cdefs.h>
# ifndef RT_IN_ASSEMBLER
#  include <iprt/types.h>
#  include <iprt/assert.h>
# endif
# include <iprt/assertcompile.h>
#else
# pragma D depends_on library vbox-types.d
#endif

/** @defgroup grp_rt_armv8   ARMv8 Types and Definitions
 * @ingroup grp_rt
 * @{
 */

/** @name The AArch64 general purpose register encoding.
 * @{ */
#define ARMV8_A64_REG_X0                            0
#define ARMV8_A64_REG_X1                            1
#define ARMV8_A64_REG_X2                            2
#define ARMV8_A64_REG_X3                            3
#define ARMV8_A64_REG_X4                            4
#define ARMV8_A64_REG_X5                            5
#define ARMV8_A64_REG_X6                            6
#define ARMV8_A64_REG_X7                            7
#define ARMV8_A64_REG_X8                            8
#define ARMV8_A64_REG_X9                            9
#define ARMV8_A64_REG_X10                           10
#define ARMV8_A64_REG_X11                           11
#define ARMV8_A64_REG_X12                           12
#define ARMV8_A64_REG_X13                           13
#define ARMV8_A64_REG_X14                           14
#define ARMV8_A64_REG_X15                           15
#define ARMV8_A64_REG_X16                           16
#define ARMV8_A64_REG_X17                           17
#define ARMV8_A64_REG_X18                           18
#define ARMV8_A64_REG_X19                           19
#define ARMV8_A64_REG_X20                           20
#define ARMV8_A64_REG_X21                           21
#define ARMV8_A64_REG_X22                           22
#define ARMV8_A64_REG_X23                           23
#define ARMV8_A64_REG_X24                           24
#define ARMV8_A64_REG_X25                           25
#define ARMV8_A64_REG_X26                           26
#define ARMV8_A64_REG_X27                           27
#define ARMV8_A64_REG_X28                           28
#define ARMV8_A64_REG_X29                           29
#define ARMV8_A64_REG_X30                           30
/** @} */

/** @name The AArch64 32-bit general purpose register names.
 * @{ */
#define ARMV8_A64_REG_W0                            ARMV8_A64_REG_X0
#define ARMV8_A64_REG_W1                            ARMV8_A64_REG_X1
#define ARMV8_A64_REG_W2                            ARMV8_A64_REG_X2
#define ARMV8_A64_REG_W3                            ARMV8_A64_REG_X3
#define ARMV8_A64_REG_W4                            ARMV8_A64_REG_X4
#define ARMV8_A64_REG_W5                            ARMV8_A64_REG_X5
#define ARMV8_A64_REG_W6                            ARMV8_A64_REG_X6
#define ARMV8_A64_REG_W7                            ARMV8_A64_REG_X7
#define ARMV8_A64_REG_W8                            ARMV8_A64_REG_X8
#define ARMV8_A64_REG_W9                            ARMV8_A64_REG_X9
#define ARMV8_A64_REG_W10                           ARMV8_A64_REG_X10
#define ARMV8_A64_REG_W11                           ARMV8_A64_REG_X11
#define ARMV8_A64_REG_W12                           ARMV8_A64_REG_X12
#define ARMV8_A64_REG_W13                           ARMV8_A64_REG_X13
#define ARMV8_A64_REG_W14                           ARMV8_A64_REG_X14
#define ARMV8_A64_REG_W15                           ARMV8_A64_REG_X15
#define ARMV8_A64_REG_W16                           ARMV8_A64_REG_X16
#define ARMV8_A64_REG_W17                           ARMV8_A64_REG_X17
#define ARMV8_A64_REG_W18                           ARMV8_A64_REG_X18
#define ARMV8_A64_REG_W19                           ARMV8_A64_REG_X19
#define ARMV8_A64_REG_W20                           ARMV8_A64_REG_X20
#define ARMV8_A64_REG_W21                           ARMV8_A64_REG_X21
#define ARMV8_A64_REG_W22                           ARMV8_A64_REG_X22
#define ARMV8_A64_REG_W23                           ARMV8_A64_REG_X23
#define ARMV8_A64_REG_W24                           ARMV8_A64_REG_X24
#define ARMV8_A64_REG_W25                           ARMV8_A64_REG_X25
#define ARMV8_A64_REG_W26                           ARMV8_A64_REG_X26
#define ARMV8_A64_REG_W27                           ARMV8_A64_REG_X27
#define ARMV8_A64_REG_W28                           ARMV8_A64_REG_X28
#define ARMV8_A64_REG_W29                           ARMV8_A64_REG_X29
#define ARMV8_A64_REG_W30                           ARMV8_A64_REG_X30
/** @} */

/** @name The AArch64 NEON scalar register encoding.
 * @{ */
#define ARMV8_A64_REG_Q0                            0
#define ARMV8_A64_REG_Q1                            1
#define ARMV8_A64_REG_Q2                            2
#define ARMV8_A64_REG_Q3                            3
#define ARMV8_A64_REG_Q4                            4
#define ARMV8_A64_REG_Q5                            5
#define ARMV8_A64_REG_Q6                            6
#define ARMV8_A64_REG_Q7                            7
#define ARMV8_A64_REG_Q8                            8
#define ARMV8_A64_REG_Q9                            9
#define ARMV8_A64_REG_Q10                           10
#define ARMV8_A64_REG_Q11                           11
#define ARMV8_A64_REG_Q12                           12
#define ARMV8_A64_REG_Q13                           13
#define ARMV8_A64_REG_Q14                           14
#define ARMV8_A64_REG_Q15                           15
#define ARMV8_A64_REG_Q16                           16
#define ARMV8_A64_REG_Q17                           17
#define ARMV8_A64_REG_Q18                           18
#define ARMV8_A64_REG_Q19                           19
#define ARMV8_A64_REG_Q20                           20
#define ARMV8_A64_REG_Q21                           21
#define ARMV8_A64_REG_Q22                           22
#define ARMV8_A64_REG_Q23                           23
#define ARMV8_A64_REG_Q24                           24
#define ARMV8_A64_REG_Q25                           25
#define ARMV8_A64_REG_Q26                           26
#define ARMV8_A64_REG_Q27                           27
#define ARMV8_A64_REG_Q28                           28
#define ARMV8_A64_REG_Q29                           29
#define ARMV8_A64_REG_Q30                           30
#define ARMV8_A64_REG_Q31                           31
/** @} */

/** @name The AArch64 NEON vector register encoding.
 * @{ */
#define ARMV8_A64_REG_V0                            ARMV8_A64_REG_Q0
#define ARMV8_A64_REG_V1                            ARMV8_A64_REG_Q1
#define ARMV8_A64_REG_V2                            ARMV8_A64_REG_Q2
#define ARMV8_A64_REG_V3                            ARMV8_A64_REG_Q3
#define ARMV8_A64_REG_V4                            ARMV8_A64_REG_Q4
#define ARMV8_A64_REG_V5                            ARMV8_A64_REG_Q5
#define ARMV8_A64_REG_V6                            ARMV8_A64_REG_Q6
#define ARMV8_A64_REG_V7                            ARMV8_A64_REG_Q7
#define ARMV8_A64_REG_V8                            ARMV8_A64_REG_Q8
#define ARMV8_A64_REG_V9                            ARMV8_A64_REG_Q9
#define ARMV8_A64_REG_V10                           ARMV8_A64_REG_Q10
#define ARMV8_A64_REG_V11                           ARMV8_A64_REG_Q11
#define ARMV8_A64_REG_V12                           ARMV8_A64_REG_Q12
#define ARMV8_A64_REG_V13                           ARMV8_A64_REG_Q13
#define ARMV8_A64_REG_V14                           ARMV8_A64_REG_Q14
#define ARMV8_A64_REG_V15                           ARMV8_A64_REG_Q15
#define ARMV8_A64_REG_V16                           ARMV8_A64_REG_Q16
#define ARMV8_A64_REG_V17                           ARMV8_A64_REG_Q17
#define ARMV8_A64_REG_V18                           ARMV8_A64_REG_Q18
#define ARMV8_A64_REG_V19                           ARMV8_A64_REG_Q19
#define ARMV8_A64_REG_V20                           ARMV8_A64_REG_Q20
#define ARMV8_A64_REG_V21                           ARMV8_A64_REG_Q21
#define ARMV8_A64_REG_V22                           ARMV8_A64_REG_Q22
#define ARMV8_A64_REG_V23                           ARMV8_A64_REG_Q23
#define ARMV8_A64_REG_V24                           ARMV8_A64_REG_Q24
#define ARMV8_A64_REG_V25                           ARMV8_A64_REG_Q25
#define ARMV8_A64_REG_V26                           ARMV8_A64_REG_Q26
#define ARMV8_A64_REG_V27                           ARMV8_A64_REG_Q27
#define ARMV8_A64_REG_V28                           ARMV8_A64_REG_Q28
#define ARMV8_A64_REG_V29                           ARMV8_A64_REG_Q29
#define ARMV8_A64_REG_V30                           ARMV8_A64_REG_Q30
#define ARMV8_A64_REG_V31                           ARMV8_A64_REG_Q31
/** @} */

/** @name The AArch64 register 31.
 * @note Register 31 typically refers to the zero register, but can also in
 *       select case (by instruction and opecode field) refer the to stack
 *       pointer of the current exception level.  ARM typically uses \<Xn|SP\>
 *       to indicate that register 31 is taken as SP, if just \<Xn\> is used
 *       31 will be the zero register.
 * @{ */
/** The stack pointer. */
#define ARMV8_A64_REG_SP                            31
/** The zero register.  Reads as zero, writes ignored. */
#define ARMV8_A64_REG_XZR                           31
/** The zero register, the 32-bit register name. */
#define ARMV8_A64_REG_WZR                           ARMV8_A64_REG_XZR
/** @} */

/** @name AArch64 register aliases
 * @{ */
/** The link register is typically mapped to x30 as that's the default pick of
 *  the RET instruction. */
#define ARMV8_A64_REG_LR                            ARMV8_A64_REG_X30
/** Frame base pointer is typically mapped to x29. */
#define ARMV8_A64_REG_BP                            ARMV8_A64_REG_X29
/** @} */


/** @name System register encoding.
 * @{
 */
/** Mask for the op0 part of an MSR/MRS instruction */
#define ARMV8_AARCH64_SYSREG_OP0_MASK               (RT_BIT_32(19) | RT_BIT_32(20))
/** Shift for the op0 part of an MSR/MRS instruction */
#define ARMV8_AARCH64_SYSREG_OP0_SHIFT              19
/** Returns the op0 part of the given MRS/MSR instruction. */
#define ARMV8_AARCH64_SYSREG_OP0_GET(a_MsrMrsInsn)  (((a_MsrMrsInsn) & ARMV8_AARCH64_SYSREG_OP0_MASK) >> ARMV8_AARCH64_SYSREG_OP0_SHIFT)
/** Mask for the op1 part of an MSR/MRS instruction */
#define ARMV8_AARCH64_SYSREG_OP1_MASK               (RT_BIT_32(16) | RT_BIT_32(17) | RT_BIT_32(18))
/** Shift for the op1 part of an MSR/MRS instruction */
#define ARMV8_AARCH64_SYSREG_OP1_SHIFT              16
/** Returns the op1 part of the given MRS/MSR instruction. */
#define ARMV8_AARCH64_SYSREG_OP1_GET(a_MsrMrsInsn)  (((a_MsrMrsInsn) & ARMV8_AARCH64_SYSREG_OP1_MASK) >> ARMV8_AARCH64_SYSREG_OP1_SHIFT)
/** Mask for the CRn part of an MSR/MRS instruction */
#define ARMV8_AARCH64_SYSREG_CRN_MASK               (  RT_BIT_32(12) | RT_BIT_32(13) | RT_BIT_32(14) \
                                                     | RT_BIT_32(15) )
/** Shift for the CRn part of an MSR/MRS instruction */
#define ARMV8_AARCH64_SYSREG_CRN_SHIFT              12
/** Returns the CRn part of the given MRS/MSR instruction. */
#define ARMV8_AARCH64_SYSREG_CRN_GET(a_MsrMrsInsn)  (((a_MsrMrsInsn) & ARMV8_AARCH64_SYSREG_CRN_MASK) >> ARMV8_AARCH64_SYSREG_CRN_SHIFT)
/** Mask for the CRm part of an MSR/MRS instruction */
#define ARMV8_AARCH64_SYSREG_CRM_MASK               (  RT_BIT_32(8) | RT_BIT_32(9) | RT_BIT_32(10) \
                                                     | RT_BIT_32(11) )
/** Shift for the CRm part of an MSR/MRS instruction */
#define ARMV8_AARCH64_SYSREG_CRM_SHIFT              8
/** Returns the CRn part of the given MRS/MSR instruction. */
#define ARMV8_AARCH64_SYSREG_CRM_GET(a_MsrMrsInsn)  (((a_MsrMrsInsn) & ARMV8_AARCH64_SYSREG_CRM_MASK) >> ARMV8_AARCH64_SYSREG_CRM_SHIFT)
/** Mask for the op2 part of an MSR/MRS instruction */
#define ARMV8_AARCH64_SYSREG_OP2_MASK               (RT_BIT_32(5) | RT_BIT_32(6) | RT_BIT_32(7))
/** Shift for the op2 part of an MSR/MRS instruction */
#define ARMV8_AARCH64_SYSREG_OP2_SHIFT              5
/** Returns the op2 part of the given MRS/MSR instruction. */
#define ARMV8_AARCH64_SYSREG_OP2_GET(a_MsrMrsInsn)  (((a_MsrMrsInsn) & ARMV8_AARCH64_SYSREG_OP2_MASK) >> ARMV8_AARCH64_SYSREG_OP2_SHIFT)
/** Mask for all system register encoding relevant fields in an MRS/MSR instruction. */
#define ARMV8_AARCH64_SYSREG_MASK                   (  ARMV8_AARCH64_SYSREG_OP0_MASK | ARMV8_AARCH64_SYSREG_OP1_MASK \
                                                     | ARMV8_AARCH64_SYSREG_CRN_MASK | ARMV8_AARCH64_SYSREG_CRN_MASK \
                                                     | ARMV8_AARCH64_SYSREG_OP2_MASK)
/** @} */

/** @name Mapping of op0:op1:CRn:CRm:op2 to a system register ID. This is
 * IPRT specific and not part of the ARMv8 specification.
 * @{  */
#define ARMV8_AARCH64_SYSREG_ID_CREATE(a_Op0, a_Op1, a_CRn, a_CRm, a_Op2) \
    (uint16_t)(  (((a_Op0) & 0x3) << 14) \
               | (((a_Op1) & 0x7) << 11) \
               | (((a_CRn) & 0xf) <<  7) \
               | (((a_CRm) & 0xf) <<  3) \
               |  ((a_Op2) & 0x7))

/** Extract op0 from an IPRT system register ID value. */
#define ARMV8_AARCH64_SYSREG_ID_GET_OP0(a_idSysReg) (((a_idSysReg) >> 14) & 0x3)
/** Extract op1 from an IPRT system register ID value. */
#define ARMV8_AARCH64_SYSREG_ID_GET_OP1(a_idSysReg) (((a_idSysReg) >> 11) & 0x7)
/** Extract CRn from an IPRT system register ID value. */
#define ARMV8_AARCH64_SYSREG_ID_GET_CRN(a_idSysReg) (((a_idSysReg) >>  7) & 0xf)
/** Extract CRm from an IPRT system register ID value. */
#define ARMV8_AARCH64_SYSREG_ID_GET_CRM(a_idSysReg) (((a_idSysReg) >>  3) & 0xf)
/** Extract op2 from an IPRT system register ID value. */
#define ARMV8_AARCH64_SYSREG_ID_GET_OP2(a_idSysReg) ( (a_idSysReg)        & 0x7)

/** Returns the internal system register ID from the given MRS/MSR instruction. */
#define ARMV8_AARCH64_SYSREG_ID_FROM_MRS_MSR(a_MsrMrsInsn) \
    ARMV8_AARCH64_SYSREG_ID_CREATE(ARMV8_AARCH64_SYSREG_OP0_GET(a_MsrMrsInsn), \
                                   ARMV8_AARCH64_SYSREG_OP1_GET(a_MsrMrsInsn), \
                                   ARMV8_AARCH64_SYSREG_CRN_GET(a_MsrMrsInsn), \
                                   ARMV8_AARCH64_SYSREG_CRM_GET(a_MsrMrsInsn), \
                                   ARMV8_AARCH64_SYSREG_OP2_GET(a_MsrMrsInsn))
/** Encodes the given system register ID in the given MSR/MRS instruction. */
#define ARMV8_AARCH64_SYSREG_ID_ENCODE_IN_MRS_MSR(a_MsrMrsInsn, a_SysregId) \
    ((a_MsrMrsInsn) = ((a_MsrMrsInsn) & ~ARMV8_AARCH64_SYSREG_MASK) | (a_SysregId << ARMV8_AARCH64_SYSREG_OP2_SHIFT))
/** @} */


/** @name System register IDs.
 * @{ */
/** OSDTRRX_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_OSDTRRX_EL1            ARMV8_AARCH64_SYSREG_ID_CREATE(2, 0, 0, 0, 2)
/** MDSCR_EL1 - RW. */
#define ARMV8_AARCH64_SYSREG_MDSCR_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(2, 0, 0, 2, 2)
/** DBGBVR<0..15>_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_DBGBVRn_EL1(a_Id)      ARMV8_AARCH64_SYSREG_ID_CREATE(2, 0, 0, (a_Id), 4)
/** DBGBCR<0..15>_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_DBGBCRn_EL1(a_Id)      ARMV8_AARCH64_SYSREG_ID_CREATE(2, 0, 0, (a_Id), 5)
/** DBGWVR<0..15>_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_DBGWVRn_EL1(a_Id)      ARMV8_AARCH64_SYSREG_ID_CREATE(2, 0, 0, (a_Id), 6)
/** DBGWCR<0..15>_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_DBGWCRn_EL1(a_Id)      ARMV8_AARCH64_SYSREG_ID_CREATE(2, 0, 0, (a_Id), 7)
/** MDCCINT_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_MDCCINT_EL1            ARMV8_AARCH64_SYSREG_ID_CREATE(2, 0, 0, 2, 0)
/** OSDTRTX_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_OSDTRTX_EL1            ARMV8_AARCH64_SYSREG_ID_CREATE(2, 0, 0, 3, 2)
/** OSECCR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_OSECCR_EL1             ARMV8_AARCH64_SYSREG_ID_CREATE(2, 0, 0, 6, 2)
/** MDRAR_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_MDRAR_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(2, 0, 1, 0, 0)
/** OSLAR_EL1 register - WO. */
#define ARMV8_AARCH64_SYSREG_OSLAR_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(2, 0, 1, 0, 4)
/** OSLSR_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_OSLSR_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(2, 0, 1, 1, 4)
/** OSDLR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_OSDLR_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(2, 0, 1, 3, 4)

/** TRCDEVARCH register - RO. */
#define ARMV8_AARCH64_SYSREG_TRCDEVARCH             ARMV8_AARCH64_SYSREG_ID_CREATE(2, 1, 7, 15, 6)

/** MIDR_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_MIDR_EL1               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 0, 0)
/** MIPDR_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_MPIDR_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 0, 5)
/** REVIDR_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_REVIDR_EL1             ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 0, 6)

/** ID_PFR0_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_PFR0_EL1            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 1, 0)
/** ID_PFR1_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_PFR1_EL1            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 1, 1)
/** ID_DFR0_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_DFR0_EL1            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 1, 2)
/** ID_AFR0_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AFR0_EL1            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 1, 3)
/** ID_MMFR0_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_MMFR0_EL1           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 1, 4)
/** ID_MMFR1_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_MMFR1_EL1           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 1, 5)
/** ID_MMFR2_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_MMFR2_EL1           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 1, 6)
/** ID_MMFR3_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_MMFR3_EL1           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 1, 7)

/** ID_ISAR0_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_ISAR0_EL1           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 2, 0)
/** ID_ISAR1_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_ISAR1_EL1           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 2, 1)
/** ID_ISAR2_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_ISAR2_EL1           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 2, 2)
/** ID_ISAR3_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_ISAR3_EL1           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 2, 3)
/** ID_ISAR4_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_ISAR4_EL1           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 2, 4)
/** ID_ISAR5_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_ISAR5_EL1           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 2, 5)
/** ID_MMFR4_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_MMFR4_EL1           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 2, 6)
/** ID_ISAR6_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_ISAR6_EL1           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 2, 7)

/** MVFR0_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_MVFR0_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 3, 0)
/** MVFR1_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_MVFR1_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 3, 1)
/** MVFR2_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_MVFR2_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 3, 2)
/* Reserved, RAZ:                                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 3, 3) */
/** ID_PFR2_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_PFR2_EL1            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 3, 4)
/** ID_DFR1_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_DFR1_EL1            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 3, 5)
/** ID_MMFR5_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_MMFR5_EL1           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 3, 6)
/* Reserved, RAZ:                                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 3, 7) */

/** ID_AA64PFR0_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64PFR0_EL1        ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 0)
/** ID_AA64PFR1_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64PFR1_EL1        ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 1)
/** ID_AA64PFR2_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64PFR2_EL1        ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 2)
/* Reserved, RAZ:                                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 3) */
/** ID_AA64ZFR0_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64ZFR0_EL1        ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 4)
/** ID_AA64SMFR0_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64SMFR0_EL1       ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 5)
/* Reserved, RAZ:                                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 6) */
/** ID_AA64FPFR0_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64FPFR0_EL1       ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 4, 7)

/** ID_AA64DFR0_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64DFR0_EL1        ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 0)
/** ID_AA64DFR0_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64DFR1_EL1        ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 1)
/** ID_AA64DFR0_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64DFR2_EL1        ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 2)
/* Reserved, RAZ:                                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 3) */
/** ID_AA64AFR0_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64AFR0_EL1        ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 4)
/** ID_AA64AFR1_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64AFR1_EL1        ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 5)
/* Reserved, RAZ:                                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 6) */
/* Reserved, RAZ:                                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 5, 7) */

/** ID_AA64ISAR0_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64ISAR0_EL1       ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 0)
/** ID_AA64ISAR1_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64ISAR1_EL1       ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 1)
/** ID_AA64ISAR2_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64ISAR2_EL1       ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 2)
/** ID_AA64ISAR3_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64ISAR3_EL1       ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 3)
/* Reserved, RAZ:                                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 4) */
/* Reserved, RAZ:                                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 5) */
/* Reserved, RAZ:                                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 6) */
/* Reserved, RAZ:                                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 6, 7) */

/** ID_AA64MMFR0_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64MMFR0_EL1       ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 0)
/** ID_AA64MMFR1_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64MMFR1_EL1       ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 1)
/** ID_AA64MMFR2_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64MMFR2_EL1       ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 2)
/** ID_AA64MMFR3_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64MMFR3_EL1       ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 3)
/** ID_AA64MMFR4_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ID_AA64MMFR4_EL1       ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 4)
/* Reserved, RAZ:                                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 5) */
/* Reserved, RAZ:                                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 6) */
/* Reserved, RAZ:                                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 0, 7, 7) */

/** SCTRL_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_SCTRL_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 1, 0, 0)
/** ACTRL_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ACTRL_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 1, 0, 1)
/** CPACR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_CPACR_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 1, 0, 2)
/** RGSR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_RGSR_EL1               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 1, 0, 5)
/** GCR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_GCR_EL1                ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 1, 0, 6)

/** ZCR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ZCR_EL1                ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 1, 2, 0)
/** TRFCR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_TRFCR_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 1, 2, 1)
/** SMPRI_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_SMPRI_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 1, 2, 4)
/** SMCR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_SMCR_EL1               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 1, 2, 6)

/** TTBR0_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_TTBR0_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 2, 0, 0)
/** TTBR1_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_TTBR1_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 2, 0, 1)
/** TCR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_TCR_EL1                ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 2, 0, 2)

/** APIAKeyLo_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_APIAKeyLo_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 2, 1, 0)
/** APIAKeyHi_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_APIAKeyHi_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 2, 1, 1)
/** APIBKeyLo_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_APIBKeyLo_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 2, 1, 2)
/** APIBKeyHi_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_APIBKeyHi_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 2, 1, 3)

/** APDAKeyLo_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_APDAKeyLo_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 2, 2, 0)
/** APDAKeyHi_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_APDAKeyHi_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 2, 2, 1)
/** APDBKeyLo_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_APDBKeyLo_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 2, 2, 2)
/** APDBKeyHi_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_APDBKeyHi_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 2, 2, 3)

/** APGAKeyLo_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_APGAKeyLo_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 2, 3, 0)
/** APGAKeyHi_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_APGAKeyHi_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 2, 3, 1)

/** SPSR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_SPSR_EL1               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 4, 0, 0)
/** ELR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ELR_EL1                ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 4, 0, 1)

/** SP_EL0 register - RW. */
#define ARMV8_AARCH64_SYSREG_SP_EL0                 ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 4, 1, 0)

/** PSTATE.SPSel value. */
#define ARMV8_AARCH64_SYSREG_SPSEL                  ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 4, 2, 0)
/** PSTATE.CurrentEL value. */
#define ARMV8_AARCH64_SYSREG_CURRENTEL              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 4, 2, 2)
/** PSTATE.PAN value. */
#define ARMV8_AARCH64_SYSREG_PAN                    ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 4, 2, 3)
/** PSTATE.UAO value. */
#define ARMV8_AARCH64_SYSREG_UAO                    ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 4, 2, 4)

/** PSTATE.ALLINT value. */
#define ARMV8_AARCH64_SYSREG_ALLINT                 ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 4, 3, 0)

/** ICC_PMR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ICC_PMR_EL1            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 4, 6, 0)

/** AFSR0_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_AFSR0_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 5, 1, 0)
/** AFSR1_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_AFSR1_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 5, 1, 1)

/** ESR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ESR_EL1                ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 5, 2, 0)

/** ERRIDR_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ERRIDR_EL1             ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 5, 3, 0)
/** ERRSELR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ERRSELR_EL1            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 5, 3, 1)

/** FAR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_FAR_EL1                ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 6, 0, 0)

/** PAR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_PAR_EL1                ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 7, 4, 0)

/** PMSIDR_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_PMSIDR_EL1             ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 9, 9, 7)

/** PMBIDR_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_PMBIDR_EL1             ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 9, 10, 7)

/** TRBIDR_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_TRBIDR_EL1             ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 9, 11, 7)

/** PMINTENCLR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_PMINTENCLR_EL1         ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 9, 14, 2)

/** PMMIR_EL1 register RO.   */
#define ARMV8_AARCH64_SYSREG_PMMIR_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 9, 14, 6)

/** MAIR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_MAIR_EL1               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 10, 2, 0)

/** AMAIR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_AMAIR_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 10, 3, 0)

/** MPAMIDR_EL1 register - RO - FEAT_MPAM. */
#define ARMV8_AARCH64_SYSREG_MPAMIDR_EL1            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 10, 4, 4)
/** MPAMBWIDR_EL1 register - RO - FEAT_MPAM_PE_BW_CTRL. */
#define ARMV8_AARCH64_SYSREG_MPAMBWIDR_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 10, 4, 5)

/** VBAR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_VBAR_EL1               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 0, 0)

/** ICC_IAR0_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ICC_IAR0_EL1           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 8, 0)
/** ICC_EOIR0_EL1 register - WO. */
#define ARMV8_AARCH64_SYSREG_ICC_EOIR0_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 8, 1)
/** ICC_HPPIR0_EL1 register - WO. */
#define ARMV8_AARCH64_SYSREG_ICC_HPPIR0_EL1         ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 8, 2)
/** ICC_BPR0_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ICC_BPR0_EL1           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 8, 3)
/** ICC_AP0R0_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ICC_AP0R0_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 8, 4)
/** ICC_AP0R1_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ICC_AP0R1_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 8, 5)
/** ICC_AP0R2_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ICC_AP0R2_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 8, 6)
/** ICC_AP0R3_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ICC_AP0R3_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 8, 7)

/** ICC_AP1R0_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ICC_AP1R0_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 9, 0)
/** ICC_AP1R1_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ICC_AP1R1_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 9, 1)
/** ICC_AP1R2_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ICC_AP1R2_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 9, 2)
/** ICC_AP1R3_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ICC_AP1R3_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 9, 3)
/** ICC_NMIAR1_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ICC_NMIAR1_EL1         ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 9, 5)

/** ICC_DIR_EL1 register - WO. */
#define ARMV8_AARCH64_SYSREG_ICC_DIR_EL1            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 11, 1)
/** ICC_RPR_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ICC_RPR_EL1            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 11, 3)
/** ICC_SGI1R_EL1 register - WO. */
#define ARMV8_AARCH64_SYSREG_ICC_SGI1R_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 11, 5)
/** ICC_ASGI1R_EL1 register - WO. */
#define ARMV8_AARCH64_SYSREG_ICC_ASGI1R_EL1         ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 11, 6)
/** ICC_SGI0R_EL1 register - WO. */
#define ARMV8_AARCH64_SYSREG_ICC_SGI0R_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 11, 7)

/** ICC_IAR1_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ICC_IAR1_EL1           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 12, 0)
/** ICC_EOIR1_EL1 register - WO. */
#define ARMV8_AARCH64_SYSREG_ICC_EOIR1_EL1          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 12, 1)
/** ICC_HPPIR1_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_ICC_HPPIR1_EL1         ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 12, 2)
/** ICC_BPR1_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ICC_BPR1_EL1           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 12, 3)
/** ICC_CTLR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ICC_CTLR_EL1           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 12, 4)
/** ICC_SRE_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ICC_SRE_EL1            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 12, 5)
/** ICC_IGRPEN0_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ICC_IGRPEN0_EL1        ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 12, 6)
/** ICC_IGRPEN1_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_ICC_IGRPEN1_EL1        ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 12, 12, 7)

/** CONTEXTIDR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_CONTEXTIDR_EL1         ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 13,  0, 1)
/** TPIDR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_TPIDR_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 13,  0, 4)

/** CNTKCTL_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTKCTL_EL1            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 0, 14,  1, 0)

/** CCSIDR_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_CCSIDR_EL1             ARMV8_AARCH64_SYSREG_ID_CREATE(3, 1,  0,  0, 0)
/** CLIDR_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_CLIDR_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 1,  0,  0, 1)
/** CCSIDR2_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_CCSIDR2_EL1            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 1,  0,  0, 2)
/** GMID_EL1 register - RO - FEAT_MTE2. */
#define ARMV8_AARCH64_SYSREG_GMID_EL1               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 1,  0,  0, 4)
/** SMIDR_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_SMIDR_EL1              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 1,  0,  0, 6)
/** AIDR_EL1 register - RO. */
#define ARMV8_AARCH64_SYSREG_AIDR_EL1               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 1,  0,  0, 7)

/** CSSELR_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_CSSELR_EL1             ARMV8_AARCH64_SYSREG_ID_CREATE(3, 2,  0,  0, 0)

/** CTR_EL0 - Cache Type Register - RO. */
#define ARMV8_AARCH64_SYSREG_CTR_EL0                ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 0, 0, 1)
/** DCZID_EL0 - Data Cache Zero ID Register - RO. */
#define ARMV8_AARCH64_SYSREG_DCZID_EL0              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 0, 0, 7)


/** NZCV - Status Flags - ??. */
#define ARMV8_AARCH64_SYSREG_NZCV                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 4, 2, 0)
/** DAIF - Interrupt Mask Bits - ??. */
#define ARMV8_AARCH64_SYSREG_DAIF                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 4, 2, 1)
/** SVCR - Streaming Vector Control Register - ??. */
#define ARMV8_AARCH64_SYSREG_SVCR                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 4, 2, 2)
/** DIT - Data Independent Timing - ??. */
#define ARMV8_AARCH64_SYSREG_DIT                    ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 4, 2, 5)
/** SSBS - Speculative Store Bypass Safe - ??. */
#define ARMV8_AARCH64_SYSREG_SSBS                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 4, 2, 6)
/** TCO - Tag Check Override - ??. */
#define ARMV8_AARCH64_SYSREG_TCO                    ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 4, 2, 7)

/** FPCR register - RW. */
#define ARMV8_AARCH64_SYSREG_FPCR                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 4, 4, 0)
/** FPSR register - RW. */
#define ARMV8_AARCH64_SYSREG_FPSR                   ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 4, 4, 1)

/** PMCR_EL0 register - RW. */
#define ARMV8_AARCH64_SYSREG_PMCR_EL0               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 9,  12, 0)
/** PMCNTENSET_EL0 register - RW. */
#define ARMV8_AARCH64_SYSREG_PMCNTENSET_EL0         ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 9,  12, 1)
/** PMCNTENCLR_EL0 register - RW. */
#define ARMV8_AARCH64_SYSREG_PMCNTENCLR_EL0         ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 9,  12, 2)
/** PMOVSCLR_EL0 register - RW. */
#define ARMV8_AARCH64_SYSREG_PMOVSCLR_EL0           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 9,  12, 3)

/** PMCCNTR_EL0 register - RW. */
#define ARMV8_AARCH64_SYSREG_PMCCNTR_EL0            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 9,  13, 0)

/** PMUSERENR_EL0 register - RW. */
#define ARMV8_AARCH64_SYSREG_PMUSERENR_EL0          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 9,  14, 0)

/** PMCCFILTR_EL0 register - RW. */
#define ARMV8_AARCH64_SYSREG_PMCCFILTR_EL0          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 14, 15, 7)

/** ICC_SRE_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_ICC_SRE_EL2            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 12,  9, 5)

/** TPIDR_EL0 register - RW. */
#define ARMV8_AARCH64_SYSREG_TPIDR_EL0              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 13,  0, 2)
/** TPIDRRO_EL0 register - RO. */
#define ARMV8_AARCH64_SYSREG_TPIDRRO_EL0            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 13,  0, 3)

/** CNTFRQ_EL0 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTFRQ_EL0             ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 14,  0, 0)
/** CNTVCT_EL0 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTVCT_EL0             ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 14,  0, 2)

/** CNTP_TVAL_EL0 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTP_TVAL_EL0          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 14,  2, 0)
/** CNTP_CTL_EL0 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTP_CTL_EL0           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 14,  2, 1)
/** CNTP_CVAL_EL0 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTP_CVAL_EL0          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 14,  2, 2)

/** CNTV_CTL_EL0 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTV_CTL_EL0           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 3, 14,  3, 1)

/** VPIDR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_VPIDR_EL2              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  0,  0, 0)
/** VMPIDR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_VMPIDR_EL2             ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  0,  0, 5)

/** SCTLR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_SCTLR_EL2              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  1,  0, 0)
/** ACTLR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_ACTLR_EL2              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  1,  0, 1)

/** HCR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_HCR_EL2                ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  1,  1, 0)
/** MDCR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_MDCR_EL2               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  1,  1, 1)
/** CPTR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_CPTR_EL2               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  1,  1, 2)
/** HSTR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_HSTR_EL2               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  1,  1, 3)
/** HFGRTR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_HFGRTR_EL2             ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  1,  1, 4)
/** HFGWTR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_HFGWTR_EL2             ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  1,  1, 5)
/** HFGITR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_HFGITR_EL2             ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  1,  1, 6)
/** HACR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_HACR_EL2               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  1,  1, 7)

/** ZCR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_ZCR_EL2                ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  1,  2, 0)
/** TRFCR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_TRFCR_EL2              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  1,  2, 1)
/** HCRX_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_HCRX_EL2               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  1,  2, 2)

/** SDER32_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_SDER32_EL2             ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  1,  3, 0)

/** TTBR0_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_TTBR0_EL2              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  2,  0, 0)
/** TTBR1_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_TTBR1_EL2              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  2,  0, 1)
/** TCR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_TCR_EL2                ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  2,  0, 2)

/** VTTBR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_VTTBR_EL2              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  2,  1, 0)
/** VTCR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_VTCR_EL2               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  2,  1, 2)

/** VNCR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_VNCR_EL2               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  2,  2, 0)

/** VSTTBR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_VSTTBR_EL2             ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  2,  6, 0)
/** VSTCR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_VSTCR_EL2              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  2,  6, 2)

/** DACR32_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_DACR32_EL2             ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  3,  0, 0)

/** HDFGRTR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_HDFGRTR_EL2            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  3,  1, 4)
/** HDFGWTR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_HDFGWTR_EL2            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  3,  1, 5)
/** HAFGRTR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_HAFGRTR_EL2            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  3,  1, 6)

/** SPSR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_SPSR_EL2               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  4,  0, 0)
/** ELR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_ELR_EL2                ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  4,  0, 1)

/** SP_EL1 register - RW. */
#define ARMV8_AARCH64_SYSREG_SP_EL1                 ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  4,  1, 0)

/** IFSR32_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_IFSR32_EL2             ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  5,  0, 1)

/** AFSR0_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_AFSR0_EL2              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  5,  1, 0)
/** AFSR1_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_AFSR1_EL2              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  5,  1, 1)

/** ESR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_ESR_EL2                ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  5,  2, 0)
/** VSESR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_VSESR_EL2              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  5,  2, 3)

/** FPEXC32_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_FPEXC32_EL2            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  5,  3, 0)

/** TFSR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_TFSR_EL2               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  5,  6, 0)

/** FAR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_FAR_EL2                ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  6,  0, 0)
/** HPFAR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_HPFAR_EL2              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  6,  0, 4)

/** PMSCR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_PMSCR_EL2              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4,  9,  9, 0)

/** MAIR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_MAIR_EL2               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 10,  2, 0)

/** AMAIR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_AMAIR_EL2              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 10,  3, 0)

/** MPAMHCR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_MPAMHCR_EL2            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 10,  4, 0)
/** MPAMVPMV_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_MPAMVPMV_EL2           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 10,  4, 1)

/** MPAM2_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_MPAM2_EL2              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 10,  5, 0)

/** MPAMVPM0_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_MPAMVPM0_EL2           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 10,  6, 0)
/** MPAMVPM1_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_MPAMVPM1_EL2           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 10,  6, 1)
/** MPAMVPM2_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_MPAMVPM2_EL2           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 10,  6, 2)
/** MPAMVPM3_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_MPAMVPM3_EL2           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 10,  6, 3)
/** MPAMVPM4_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_MPAMVPM4_EL2           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 10,  6, 4)
/** MPAMVPM5_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_MPAMVPM5_EL2           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 10,  6, 5)
/** MPAMVPM6_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_MPAMVPM6_EL2           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 10,  6, 6)
/** MPAMVPM7_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_MPAMVPM7_EL2           ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 10,  6, 7)

/** VBAR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_VBAR_EL2               ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 12,  0, 0)
/** RVBAR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_RVBAR_EL2              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 12,  0, 1)
/** RMR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_RMR_EL2                ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 12,  0, 2)

/** VDISR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_VDISR_EL2              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 12,  1, 1)

/** ICH_VTR_EL2 register - RO. */
#define ARMV8_AARCH64_SYSREG_ICH_VTR_EL2            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 12, 11, 1)

/** CONTEXTIDR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_CONTEXTIDR_EL2         ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 13,  0, 1)
/** TPIDR_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_TPIDR_EL2              ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 13,  0, 2)
/** SCXTNUM_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_SCXTNUM_EL2            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 13,  0, 7)

/** CNTVOFF_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTVOFF_EL2            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 14,  0, 3)
/** CNTPOFF_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTPOFF_EL2            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 14,  0, 6)

/** CNTHCTL_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTHCTL_EL2            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 14,  1, 0)

/** CNTHP_TVAL_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTHP_TVAL_EL2         ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 14,  2, 0)
/** CNTHP_CTL_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTHP_CTL_EL2          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 14,  2, 1)
/** CNTHP_CVAL_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTHP_CVAL_EL2         ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 14,  2, 2)

/** CNTHV_TVAL_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTHV_TVAL_EL2         ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 14,  3, 0)
/** CNTHV_CTL_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTHV_CTL_EL2          ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 14,  3, 1)
/** CNTHV_CVAL_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTHV_CVAL_EL2         ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 14,  3, 2)

/** CNTHVS_TVAL_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTHVS_TVAL_EL2        ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 14,  4, 0)
/** CNTHVS_CTL_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTHVS_CTL_EL2         ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 14,  4, 1)
/** CNTHVS_CVAL_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTHVS_CVAL_EL2        ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 14,  4, 2)

/** CNTHPS_TVAL_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTHPS_TVAL_EL2        ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 14,  5, 0)
/** CNTHPS_CTL_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTHPS_CTL_EL2         ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 14,  5, 1)
/** CNTHPS_CVAL_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_CNTHPS_CVAL_EL2        ARMV8_AARCH64_SYSREG_ID_CREATE(3, 4, 14,  5, 2)

/** SP_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_SP_EL2                 ARMV8_AARCH64_SYSREG_ID_CREATE(3, 6,  4,  1, 0)

/** SP_EL2 register - RW. */
#define ARMV8_AARCH64_SYSREG_ICC_SRE_EL3            ARMV8_AARCH64_SYSREG_ID_CREATE(3, 6, 12, 12, 5)
/** @} */


#ifndef RT_IN_ASSEMBLER
/**
 * SPSR_EL2 (according to chapter C5.2.19)
 */
typedef union ARMV8SPSREL2
{
    /** The plain unsigned view. */
    uint64_t        u;
    /** The 8-bit view. */
    uint8_t         au8[8];
    /** The 16-bit view. */
    uint16_t        au16[4];
    /** The 32-bit view. */
    uint32_t        au32[2];
    /** The 64-bit view. */
    uint64_t        u64;
} ARMV8SPSREL2;
/** Pointer to SPSR_EL2. */
typedef ARMV8SPSREL2 *PARMV8SPSREL2;
/** Pointer to const SPSR_EL2. */
typedef const ARMV8SPSREL2 *PCXARMV8SPSREL2;
#endif /* !RT_IN_ASSEMBLER */


/** @name SPSR_EL2 (When exception is taken from AArch64 state)
 * @{
 */
/** Bit 0 - 3 - M - AArch64 Exception level and selected stack pointer. */
#define ARMV8_SPSR_EL2_AARCH64_M                    (RT_BIT_64(0) | RT_BIT_64(1) | RT_BIT_64(2) | RT_BIT_64(3))
#define ARMV8_SPSR_EL2_AARCH64_GET_M(a_Spsr)        ((a_Spsr) & ARMV8_SPSR_EL2_AARCH64_M)
/** Bit 0 - SP - Selected stack pointer. */
#define ARMV8_SPSR_EL2_AARCH64_SP                   RT_BIT_64(0)
#define ARMV8_SPSR_EL2_AARCH64_SP_BIT               0
/** Bit 1 - Reserved (read as zero). */
#define ARMV8_SPSR_EL2_AARCH64_RSVD_1               RT_BIT_64(1)
/** Bit 2 - 3 - EL - Exception level. */
#define ARMV8_SPSR_EL2_AARCH64_EL                   (RT_BIT_64(2) | RT_BIT_64(3))
#define ARMV8_SPSR_EL2_AARCH64_EL_SHIFT             2
#define ARMV8_SPSR_EL2_AARCH64_GET_EL(a_Spsr)       (((a_Spsr) >> ARMV8_SPSR_EL2_AARCH64_EL_SHIFT) & 3)
#define ARMV8_SPSR_EL2_AARCH64_SET_EL(a_El)         ((a_El) << ARMV8_SPSR_EL2_AARCH64_EL_SHIFT)
/** Bit 4 - M[4] - Execution state (0 means AArch64, when 1 this contains a AArch32 state). */
#define ARMV8_SPSR_EL2_AARCH64_M4                   RT_BIT_64(4)
#define ARMV8_SPSR_EL2_AARCH64_M4_BIT               4
/** Bit 5 - T - T32 instruction set state (only valid when ARMV8_SPSR_EL2_AARCH64_M4 is set). */
#define ARMV8_SPSR_EL2_AARCH64_T                    RT_BIT_64(5)
#define ARMV8_SPSR_EL2_AARCH64_T_BIT                5
/** Bit 6 - F - FIQ interrupt mask. */
#define ARMV8_SPSR_EL2_AARCH64_F                    RT_BIT_64(6)
#define ARMV8_SPSR_EL2_AARCH64_F_BIT                6
/** Bit 7 - I - IRQ interrupt mask. */
#define ARMV8_SPSR_EL2_AARCH64_I                    RT_BIT_64(7)
#define ARMV8_SPSR_EL2_AARCH64_I_BIT                7
/** Bit 8 - A - SError interrupt mask. */
#define ARMV8_SPSR_EL2_AARCH64_A                    RT_BIT_64(8)
#define ARMV8_SPSR_EL2_AARCH64_A_BIT                8
/** Bit 9 - D - Debug Exception mask. */
#define ARMV8_SPSR_EL2_AARCH64_D                    RT_BIT_64(9)
#define ARMV8_SPSR_EL2_AARCH64_D_BIT                9
/** Bit 10 - 11 - BTYPE - Branch Type indicator. */
#define ARMV8_SPSR_EL2_AARCH64_BYTPE                (RT_BIT_64(10) | RT_BIT_64(11))
#define ARMV8_SPSR_EL2_AARCH64_BYTPE_SHIFT          10
#define ARMV8_SPSR_EL2_AARCH64_GET_BYTPE(a_Spsr)    (((a_Spsr) >> ARMV8_SPSR_EL2_AARCH64_BYTPE_SHIFT) & 3)
/** Bit 12 - SSBS - Speculative Store Bypass. */
#define ARMV8_SPSR_EL2_AARCH64_SSBS                 RT_BIT_64(12)
#define ARMV8_SPSR_EL2_AARCH64_SSBS_BIT             12
/** Bit 13 - ALLINT - All IRQ or FIQ interrupts mask. */
#define ARMV8_SPSR_EL2_AARCH64_ALLINT               RT_BIT_64(13)
#define ARMV8_SPSR_EL2_AARCH64_ALLINT_BIT           13
/** Bit 14 - 19 - Reserved (read as zero). */
#define ARMV8_SPSR_EL2_AARCH64_RSVD_14_19           (  RT_BIT_64(14) | RT_BIT_64(15) | RT_BIT_64(16) \
                                                     | RT_BIT_64(17) | RT_BIT_64(18) | RT_BIT_64(19))
/** Bit 20 - IL - Illegal Execution State flag. */
#define ARMV8_SPSR_EL2_AARCH64_IL                   RT_BIT_64(20)
#define ARMV8_SPSR_EL2_AARCH64_IL_BIT               20
/** Bit 21 - SS - Software Step flag. */
#define ARMV8_SPSR_EL2_AARCH64_SS                   RT_BIT_64(21)
#define ARMV8_SPSR_EL2_AARCH64_SS_BIT               21
/** Bit 22 - PAN - Privileged Access Never flag. */
#define ARMV8_SPSR_EL2_AARCH64_PAN                  RT_BIT_64(25)
#define ARMV8_SPSR_EL2_AARCH64_PAN_BIT              22
/** Bit 23 - UAO - User Access Override flag. */
#define ARMV8_SPSR_EL2_AARCH64_UAO                  RT_BIT_64(23)
#define ARMV8_SPSR_EL2_AARCH64_UAO_BIT              23
/** Bit 24 - DIT - Data Independent Timing flag. */
#define ARMV8_SPSR_EL2_AARCH64_DIT                  RT_BIT_64(24)
#define ARMV8_SPSR_EL2_AARCH64_DIT_BIT              24
/** Bit 25 - TCO - Tag Check Override flag. */
#define ARMV8_SPSR_EL2_AARCH64_TCO                  RT_BIT_64(25)
#define ARMV8_SPSR_EL2_AARCH64_TCO_BIT              25
/** Bit 26 - 27 - Reserved (read as zero). */
#define ARMV8_SPSR_EL2_AARCH64_RSVD_26_27           (RT_BIT_64(26) | RT_BIT_64(27))
/** Bit 28 - V - Overflow condition flag. */
#define ARMV8_SPSR_EL2_AARCH64_V                    RT_BIT_64(28)
#define ARMV8_SPSR_EL2_AARCH64_V_BIT                28
/** Bit 29 - C - Carry condition flag. */
#define ARMV8_SPSR_EL2_AARCH64_C                    RT_BIT_64(29)
#define ARMV8_SPSR_EL2_AARCH64_C_BIT                29
/** Bit 30 - Z - Zero condition flag. */
#define ARMV8_SPSR_EL2_AARCH64_Z                    RT_BIT_64(30)
#define ARMV8_SPSR_EL2_AARCH64_Z_BIT                30
/** Bit 31 - N - Negative condition flag. */
#define ARMV8_SPSR_EL2_AARCH64_N                    RT_BIT_64(31)
#define ARMV8_SPSR_EL2_AARCH64_N_BIT                31
/** Bit 32 - 63 - Reserved (read as zero). */
#define ARMV8_SPSR_EL2_AARCH64_RSVD_32_63           (UINT64_C(0xffffffff00000000))
/** Checks whether the given SPSR value contains a AARCH64 execution state. */
#define ARMV8_SPSR_EL2_IS_AARCH64_STATE(a_Spsr)     (!((a_Spsr) & ARMV8_SPSR_EL2_AARCH64_M4))
/** @} */

/** @name Aarch64 Exception levels
 * @{ */
/** Exception Level 0 - User mode. */
#define ARMV8_AARCH64_EL_0                          0
/** Exception Level 1 - Supervisor mode. */
#define ARMV8_AARCH64_EL_1                          1
/** Exception Level 2 - Hypervisor mode. */
#define ARMV8_AARCH64_EL_2                          2
/** @} */


/** @name ESR_EL2 (Exception Syndrome Register, EL2)
 * @{
 */
/** Bit 0 - 24 - ISS - Instruction Specific Syndrome, encoding depends on the exception class. */
#define ARMV8_ESR_EL2_ISS                           UINT64_C(0x1ffffff)
#define ARMV8_ESR_EL2_ISS_GET(a_Esr)                ((a_Esr) & ARMV8_ESR_EL2_ISS)
/** Bit 25 - IL - Instruction length for synchronous exception (0 means 16-bit instruction, 1 32-bit instruction). */
#define ARMV8_ESR_EL2_IL                            RT_BIT_64(25)
#define ARMV8_ESR_EL2_IL_BIT                        25
#define ARMV8_ESR_EL2_IL_IS_32BIT(a_Esr)            RT_BOOL((a_Esr) & ARMV8_ESR_EL2_IL)
#define ARMV8_ESR_EL2_IL_IS_16BIT(a_Esr)            (!((a_Esr) & ARMV8_ESR_EL2_IL))
/** Bit 26 - 31 - EC - Exception class, indicates reason for the exception that this register holds information about. */
#define ARMV8_ESR_EL2_EC                            (  RT_BIT_64(26) | RT_BIT_64(27) | RT_BIT_64(28) \
                                                     | RT_BIT_64(29) | RT_BIT_64(30) | RT_BIT_64(31))
#define ARMV8_ESR_EL2_EC_GET(a_Esr)                 (((a_Esr) & ARMV8_ESR_EL2_EC) >> 26)
/** Bit 32 - 36 - ISS2 - Only valid when FEAT_LS64_V and/or FEAT_LS64_ACCDATA is present. */
#define ARMV8_ESR_EL2_ISS2                          (  RT_BIT_64(32) | RT_BIT_64(33) | RT_BIT_64(34) \
                                                     | RT_BIT_64(35) | RT_BIT_64(36))
#define ARMV8_ESR_EL2_ISS2_GET(a_Esr)               (((a_Esr) & ARMV8_ESR_EL2_ISS2) >> 32)
/** @} */


/** @name ESR_EL2 Exception Classes (EC)
 * @{ */
/** Unknown exception reason. */
#define ARMV8_ESR_EL2_EC_UNKNOWN                                UINT32_C(0)
/** Trapped WF* instruction. */
#define ARMV8_ESR_EL2_EC_TRAPPED_WFX                            UINT32_C(1)
/** AArch32 - Trapped MCR or MRC access (coproc == 0b1111) not reported through ARMV8_ESR_EL2_EC_UNKNOWN. */
#define ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_MCR_MRC_COPROC_15      UINT32_C(3)
/** AArch32 - Trapped MCRR or MRRC access (coproc == 0b1111) not reported through ARMV8_ESR_EL2_EC_UNKNOWN. */
#define ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_MCRR_MRRC_COPROC15     UINT32_C(4)
/** AArch32 - Trapped MCR or MRC access (coproc == 0b1110). */
#define ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_MCR_MRC_COPROC_14      UINT32_C(5)
/** AArch32 - Trapped LDC or STC access. */
#define ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_LDC_STC                UINT32_C(6)
/** AArch32 - Trapped access to SME, SVE or Advanced SIMD or floating point fnunctionality. */
#define ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_SME_SVE_NEON           UINT32_C(7)
/** AArch32 - Trapped VMRS access not reported using ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_SME_SVE_NEON. */
#define ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_VMRS                   UINT32_C(8)
/** AArch32 - Trapped pointer authentication instruction. */
#define ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_PA_INSN                UINT32_C(9)
/** FEAT_LS64 - Exception from LD64B or ST64B instruction. */
#define ARMV8_ESR_EL2_EC_LS64_EXCEPTION                         UINT32_C(10)
/** AArch32 - Trapped MRRC access (coproc == 0b1110). */
#define ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_MRRC_COPROC14          UINT32_C(12)
/** FEAT_BTI - Branch Target Exception. */
#define ARMV8_ESR_EL2_EC_BTI_BRANCH_TARGET_EXCEPTION            UINT32_C(13)
/** Illegal Execution State. */
#define ARMV8_ESR_EL2_ILLEGAL_EXECUTION_STATE                   UINT32_C(14)
/** AArch32 - SVC instruction execution. */
#define ARMV8_ESR_EL2_EC_AARCH32_SVC_INSN                       UINT32_C(17)
/** AArch32 - HVC instruction execution. */
#define ARMV8_ESR_EL2_EC_AARCH32_HVC_INSN                       UINT32_C(18)
/** AArch32 - SMC instruction execution. */
#define ARMV8_ESR_EL2_EC_AARCH32_SMC_INSN                       UINT32_C(19)
/** AArch64 - SVC instruction execution. */
#define ARMV8_ESR_EL2_EC_AARCH64_SVC_INSN                       UINT32_C(21)
/** AArch64 - HVC instruction execution. */
#define ARMV8_ESR_EL2_EC_AARCH64_HVC_INSN                       UINT32_C(22)
/** AArch64 - SMC instruction execution. */
#define ARMV8_ESR_EL2_EC_AARCH64_SMC_INSN                       UINT32_C(23)
/** AArch64 - Trapped MSR, MRS or System instruction execution in AArch64 state. */
#define ARMV8_ESR_EL2_EC_AARCH64_TRAPPED_SYS_INSN               UINT32_C(24)
/** FEAT_SVE - Access to SVE vunctionality not reported using ARMV8_ESR_EL2_EC_UNKNOWN. */
#define ARMV8_ESR_EL2_EC_SVE_TRAPPED                            UINT32_C(25)
/** FEAT_PAuth and FEAT_NV - Trapped ERET, ERETAA or ERTAB instruction. */
#define ARMV8_ESR_EL2_EC_PAUTH_NV_TRAPPED_ERET_ERETAA_ERETAB    UINT32_C(26)
/** FEAT_TME - Exception from TSTART instruction. */
#define ARMV8_ESR_EL2_EC_TME_TSTART_INSN_EXCEPTION              UINT32_C(27)
/** FEAT_FPAC - Exception from a Pointer Authentication instruction failure. */
#define ARMV8_ESR_EL2_EC_FPAC_PA_INSN_FAILURE_EXCEPTION         UINT32_C(28)
/** FEAT_SME - Access to SME functionality trapped. */
#define ARMV8_ESR_EL2_EC_SME_TRAPPED_SME_ACCESS                 UINT32_C(29)
/** FEAT_RME - Exception from Granule Protection Check. */
#define ARMV8_ESR_EL2_EC_RME_GRANULE_PROT_CHECK_EXCEPTION       UINT32_C(30)
/** Instruction Abort from a lower Exception level. */
#define ARMV8_ESR_EL2_INSN_ABORT_FROM_LOWER_EL                  UINT32_C(32)
/** Instruction Abort from the same Exception level. */
#define ARMV8_ESR_EL2_INSN_ABORT_FROM_EL2                       UINT32_C(33)
/** PC alignment fault exception. */
#define ARMV8_ESR_EL2_PC_ALIGNMENT_EXCEPTION                    UINT32_C(34)
/** Data Abort from a lower Exception level. */
#define ARMV8_ESR_EL2_DATA_ABORT_FROM_LOWER_EL                  UINT32_C(36)
/** Data Abort from the same Exception level (or access associated with VNCR_EL2). */
#define ARMV8_ESR_EL2_DATA_ABORT_FROM_EL2                       UINT32_C(37)
/** SP alignment fault exception. */
#define ARMV8_ESR_EL2_SP_ALIGNMENT_EXCEPTION                    UINT32_C(38)
/** FEAT_MOPS - Memory Operation Exception. */
#define ARMV8_ESR_EL2_EC_MOPS_EXCEPTION                         UINT32_C(39)
/** AArch32 - Trapped floating point exception. */
#define ARMV8_ESR_EL2_EC_AARCH32_TRAPPED_FP_EXCEPTION           UINT32_C(40)
/** AArch64 - Trapped floating point exception. */
#define ARMV8_ESR_EL2_EC_AARCH64_TRAPPED_FP_EXCEPTION           UINT32_C(44)
/** SError interrupt. */
#define ARMV8_ESR_EL2_SERROR_INTERRUPT                          UINT32_C(47)
/** Breakpoint Exception from a lower Exception level. */
#define ARMV8_ESR_EL2_BKPT_EXCEPTION_FROM_LOWER_EL              UINT32_C(48)
/** Breakpoint Exception from the same Exception level. */
#define ARMV8_ESR_EL2_BKPT_EXCEPTION_FROM_EL2                   UINT32_C(49)
/** Software Step Exception from a lower Exception level. */
#define ARMV8_ESR_EL2_SS_EXCEPTION_FROM_LOWER_EL                UINT32_C(50)
/** Software Step Exception from the same Exception level. */
#define ARMV8_ESR_EL2_SS_EXCEPTION_FROM_EL2                     UINT32_C(51)
/** Watchpoint Exception from a lower Exception level. */
#define ARMV8_ESR_EL2_WATCHPOINT_EXCEPTION_FROM_LOWER_EL        UINT32_C(52)
/** Watchpoint Exception from the same Exception level. */
#define ARMV8_ESR_EL2_WATCHPOINT_EXCEPTION_FROM_EL2             UINT32_C(53)
/** AArch32 - BKPT instruction execution. */
#define ARMV8_ESR_EL2_EC_AARCH32_BKPT_INSN                      UINT32_C(56)
/** AArch32 - Vector Catch exception. */
#define ARMV8_ESR_EL2_EC_AARCH32_VEC_CATCH_EXCEPTION            UINT32_C(58)
/** AArch64 - BRK instruction execution. */
#define ARMV8_ESR_EL2_EC_AARCH64_BRK_INSN                       UINT32_C(60)
/** @} */


/** @name ISS encoding for Data Abort exceptions.
 * @{ */
/** Bit 0 - 5 - DFSC - Data Fault Status Code. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC                             (  RT_BIT_32(0) | RT_BIT_32(1) | RT_BIT_32(2) \
                                                                 | RT_BIT_32(3) | RT_BIT_32(4) | RT_BIT_32(5))
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_GET(a_Iss)                  ((a_Iss) & ARMV8_EC_ISS_DATA_ABRT_DFSC)
/** Bit 6 - WnR - Write not Read. */
#define ARMV8_EC_ISS_DATA_ABRT_WNR                              RT_BIT_32(6)
#define ARMV8_EC_ISS_DATA_ABRT_WNR_BIT                          6
/** Bit 7 - S1PTW - Stage 2 translation fault for an access made for a stage 1 translation table walk. */
#define ARMV8_EC_ISS_DATA_ABRT_S1PTW                            RT_BIT_32(7)
#define ARMV8_EC_ISS_DATA_ABRT_S1PTW_BIT                        7
/** Bit 8 - CM - Cache maintenance instruction. */
#define ARMV8_EC_ISS_DATA_ABRT_CM                               RT_BIT_32(8)
#define ARMV8_EC_ISS_DATA_ABRT_CM_BIT                           8
/** Bit 9 - EA - External abort type. */
#define ARMV8_EC_ISS_DATA_ABRT_EA                               RT_BIT_32(9)
#define ARMV8_EC_ISS_DATA_ABRT_EA_BIT                           9
/** Bit 10 - FnV - FAR not Valid. */
#define ARMV8_EC_ISS_DATA_ABRT_FNV                              RT_BIT_32(10)
#define ARMV8_EC_ISS_DATA_ABRT_FNV_BIT                          10
/** Bit 11 - 12 - LST - Load/Store Type. */
#define ARMV8_EC_ISS_DATA_ABRT_LST                              (RT_BIT_32(11) | RT_BIT_32(12))
#define ARMV8_EC_ISS_DATA_ABRT_LST_GET(a_Iss)                   (((a_Iss) & ARMV8_EC_ISS_DATA_ABRT_LST) >> 11)
/** Bit 13 - VNCR - Fault came from use of VNCR_EL2 register by EL1 code. */
#define ARMV8_EC_ISS_DATA_ABRT_VNCR                             RT_BIT_32(13)
#define ARMV8_EC_ISS_DATA_ABRT_VNCR_BIT                         13
/** Bit 14 - AR - Acquire/Release semantics. */
#define ARMV8_EC_ISS_DATA_ABRT_AR                               RT_BIT_32(14)
#define ARMV8_EC_ISS_DATA_ABRT_AR_BIT                           14
/** Bit 15 - SF - Sixty Four bit general-purpose register transfer (only when ISV is 1). */
#define ARMV8_EC_ISS_DATA_ABRT_SF                               RT_BIT_32(15)
#define ARMV8_EC_ISS_DATA_ABRT_SF_BIT                           15
/** Bit 16 - 20 - SRT - Syndrome Register Transfer. */
#define ARMV8_EC_ISS_DATA_ABRT_SRT                              (  RT_BIT_32(16) | RT_BIT_32(17) | RT_BIT_32(18) \
                                                                 | RT_BIT_32(19) | RT_BIT_32(20))
#define ARMV8_EC_ISS_DATA_ABRT_SRT_GET(a_Iss)                   (((a_Iss) & ARMV8_EC_ISS_DATA_ABRT_SRT) >> 16)
/** Bit 21 - SSE - Syndrome Sign Extend. */
#define ARMV8_EC_ISS_DATA_ABRT_SSE                              RT_BIT_32(21)
#define ARMV8_EC_ISS_DATA_ABRT_SSE_BIT                          21
/** Bit 22 - 23 - SAS - Syndrome Access Size. */
#define ARMV8_EC_ISS_DATA_ABRT_SAS                              (RT_BIT_32(22) | RT_BIT_32(23))
#define ARMV8_EC_ISS_DATA_ABRT_SAS_GET(a_Iss)                   (((a_Iss) & ARMV8_EC_ISS_DATA_ABRT_SAS) >> 22)
/** Bit 24 - ISV - Instruction Syndrome Valid. */
#define ARMV8_EC_ISS_DATA_ABRT_ISV                              RT_BIT_32(24)
#define ARMV8_EC_ISS_DATA_ABRT_ISV_BIT                          24
/** @} */


/** @name Data Fault Status Code (DFSC).
 * @{ */
/** Address size fault, level 0 of translation or translation table base register. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_ADDR_SIZE_FAULT_LVL0        0
/** Address size fault, level 1. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_ADDR_SIZE_FAULT_LVL1        1
/** Address size fault, level 2. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_ADDR_SIZE_FAULT_LVL2        2
/** Address size fault, level 3. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_ADDR_SIZE_FAULT_LVL3        3
/** Translation fault, level 0. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_TRANSLATION_FAULT_LVL0      4
/** Translation fault, level 1. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_TRANSLATION_FAULT_LVL1      5
/** Translation fault, level 2. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_TRANSLATION_FAULT_LVL2      6
/** Translation fault, level 3. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_TRANSLATION_FAULT_LVL3      7
/** FEAT_LPA2 - Access flag fault, level 0. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_ACCESS_FLAG_FAULT_LVL0      8
/** Access flag fault, level 1. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_ACCESS_FLAG_FAULT_LVL1      9
/** Access flag fault, level 2. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_ACCESS_FLAG_FAULT_LVL2      10
/** Access flag fault, level 3. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_ACCESS_FLAG_FAULT_LVL3      11
/** FEAT_LPA2 - Permission fault, level 0. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_PERMISSION_FAULT_LVL0       12
/** Permission fault, level 1. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_PERMISSION_FAULT_LVL1       13
/** Permission fault, level 2. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_PERMISSION_FAULT_LVL2       14
/** Permission fault, level 3. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_PERMISSION_FAULT_LVL3       15
/** Synchronous External abort, not a translation table walk or hardware update of translation table. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_SYNC_EXTERNAL               16
/** FEAT_MTE2 - Synchronous Tag Check Fault. */
#define ARMV8_EC_ISS_DATA_ABRT_DFSC_MTE2_SYNC_TAG_CHK_FAULT     17
/** @todo Do the rest (lazy developer). */
/** @} */


/** @name SAS encoding.
 * @{ */
/** Byte access. */
#define ARMV8_EC_ISS_DATA_ABRT_SAS_BYTE                         0
/** Halfword access (uint16_t). */
#define ARMV8_EC_ISS_DATA_ABRT_SAS_HALFWORD                     1
/** Word access (uint32_t). */
#define ARMV8_EC_ISS_DATA_ABRT_SAS_WORD                         2
/** Doubleword access (uint64_t). */
#define ARMV8_EC_ISS_DATA_ABRT_SAS_DWORD                        3
/** @} */


/** @name ISS encoding for trapped MSR, MRS or System instruction exceptions.
 * @{ */
/** Bit 0 - Direction flag. */
#define ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_DIRECTION         RT_BIT_32(0)
#define ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_DIRECTION_IS_READ(a_Iss) RT_BOOL((a_Iss) & ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_DIRECTION)
/** Bit 1 - 4 - CRm value from the instruction. */
#define ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_CRM               (  RT_BIT_32(1) | RT_BIT_32(2) | RT_BIT_32(3) \
                                                                 | RT_BIT_32(4))
#define ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_CRM_GET(a_Iss)    (((a_Iss) & ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_CRM) >> 1)
/** Bit 5 - 9 - Rt value from the instruction. */
#define ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_RT                (  RT_BIT_32(5) | RT_BIT_32(6) | RT_BIT_32(7) \
                                                                 | RT_BIT_32(8) | RT_BIT_32(9))
#define ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_RT_GET(a_Iss)     (((a_Iss) & ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_RT) >> 5)
/** Bit 10 - 13 - CRn value from the instruction. */
#define ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_CRN               (  RT_BIT_32(10) | RT_BIT_32(11) | RT_BIT_32(12) \
                                                                 | RT_BIT_32(13))
#define ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_CRN_GET(a_Iss)    (((a_Iss) & ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_CRN) >> 10)
/** Bit 14 - 16 - Op2 value from the instruction. */
#define ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_OP1               (RT_BIT_32(14) | RT_BIT_32(15) | RT_BIT_32(16))
#define ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_OP1_GET(a_Iss)    (((a_Iss) & ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_OP1) >> 14)
/** Bit 17 - 19 - Op2 value from the instruction. */
#define ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_OP2               (RT_BIT_32(17) | RT_BIT_32(18) | RT_BIT_32(19))
#define ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_OP2_GET(a_Iss)    (((a_Iss) & ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_OP2) >> 17)
/** Bit 20 - 21 - Op0 value from the instruction. */
#define ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_OP0               (RT_BIT_32(20) | RT_BIT_32(21))
#define ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_OP0_GET(a_Iss)    (((a_Iss) & ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_OP0) >> 20)
/** Bit 22 - 24 - Reserved. */
#define ARMV8_EC_ISS_AARCH64_TRAPPED_SYS_INSN_RSVD              (RT_BIT_32(22) | RT_BIT_32(23) | RT_BIT_32(24))
/** @} */


/** @name ISS encoding for trapped HVC instruction exceptions.
 * @{ */
/** Bit 0 - 15 - imm16 value of the instruction. */
#define ARMV8_EC_ISS_AARCH64_TRAPPED_HVC_INSN_IMM               (UINT16_C(0xffff))
#define ARMV8_EC_ISS_AARCH64_TRAPPED_HVC_INSN_IMM_GET(a_Iss)    ((a_Iss) & ARMV8_EC_ISS_AARCH64_TRAPPED_HVC_INSN_IMM)
/** @} */


/** @name TCR_EL1 - Translation Control Register (EL1)
 * @{
 */
/** Bit 0 - 5 - Size offset of the memory region addressed by TTBR0_EL1 (2^(64-T0SZ)). */
#define ARMV8_TCR_EL1_AARCH64_T0SZ_MASK                         (  RT_BIT_64(0) | RT_BIT_64(1) | RT_BIT_64(2) \
                                                                 | RT_BIT_64(3) | RT_BIT_64(4) | RT_BIT_64(5))
#define ARMV8_TCR_EL1_AARCH64_T0SZ_SHIFT                        0
#define ARMV8_TCR_EL1_AARCH64_T0SZ                              ARMV8_TCR_EL1_AARCH64_T0SZ_MASK
#define ARMV8_TCR_EL1_AARCH64_T0SZ_GET(a_Tcr)                   ((a_Tcr) & ARMV8_TCR_EL1_AARCH64_T1SZ)
/** Bit 7 - Translation table walk disable for translations using TTBR0_EL1. */
#define ARMV8_TCR_EL1_AARCH64_EPD0                              RT_BIT_64(7)
#define ARMV8_TCR_EL1_AARCH64_EPD0_BIT                          7
/** Bit 8 - 9 - Inner cacheability attribute for memory associated with translation table walks using TTBR0_EL1. */
#define ARMV8_TCR_EL1_AARCH64_IRGN0                             (RT_BIT_64(8) | RT_BIT_64(9))
#define ARMV8_TCR_EL1_AARCH64_IRGN0_GET(a_Tcr)                  (((a_Tcr) & ARMV8_TCR_EL1_AARCH64_IRGN0) >> 8)
/** Non cacheable. */
# define ARMV8_TCR_EL1_AARCH64_IRGN0_NON_CACHEABLE              0
/** Write-Back, Read-Allocate, Write-Allocate Cacheable. */
# define ARMV8_TCR_EL1_AARCH64_IRGN0_WB_RA_WA                   1
/** Write-Through, Read-Allocate, No Write-Allocate Cacheable. */
# define ARMV8_TCR_EL1_AARCH64_IRGN0_WT_RA_NWA                  2
/** Write-Back, Read-Allocate, No Write-Allocate Cacheable. */
# define ARMV8_TCR_EL1_AARCH64_IRGN0_WB_RA_NWA                  3
/** Bit 27 - 26 - Outer cacheability attribute for memory associated with translation table walks using TTBR0_EL1. */
#define ARMV8_TCR_EL1_AARCH64_ORGN0                             (RT_BIT_64(10) | RT_BIT_64(11))
#define ARMV8_TCR_EL1_AARCH64_ORGN0_GET(a_Tcr)                  (((a_Tcr) & ARMV8_TCR_EL1_AARCH64_ORGN0) >> 10)
/** Non cacheable. */
# define ARMV8_TCR_EL1_AARCH64_ORGN0_NON_CACHEABLE              0
/** Write-Back, Read-Allocate, Write-Allocate Cacheable. */
# define ARMV8_TCR_EL1_AARCH64_ORGN0_WB_RA_WA                   1
/** Write-Through, Read-Allocate, No Write-Allocate Cacheable. */
# define ARMV8_TCR_EL1_AARCH64_ORGN0_WT_RA_NWA                  2
/** Write-Back, Read-Allocate, No Write-Allocate Cacheable. */
# define ARMV8_TCR_EL1_AARCH64_ORGN0_WB_RA_NWA                  3
/** Bit 12 - 13 - Shareability attribute memory associated with translation table walks using TTBR0_EL1. */
#define ARMV8_TCR_EL1_AARCH64_SH0                               (RT_BIT_64(12) | RT_BIT_64(13))
#define ARMV8_TCR_EL1_AARCH64_SH0_GET(a_Tcr)                    (((a_Tcr) & ARMV8_TCR_EL1_AARCH64_SH0) >> 12)
/** Non shareable. */
# define ARMV8_TCR_EL1_AARCH64_SH0_NON_SHAREABLE                0
/** Invalid value. */
# define ARMV8_TCR_EL1_AARCH64_SH0_INVALID                      1
/** Outer Shareable. */
# define ARMV8_TCR_EL1_AARCH64_SH0_OUTER_SHAREABLE              2
/** Inner Shareable. */
# define ARMV8_TCR_EL1_AARCH64_SH0_INNER_SHAREABLE              3
/** Bit 14 - 15 - Translation Granule Size for TTBR0_EL1. */
#define ARMV8_TCR_EL1_AARCH64_TG0_MASK                          (RT_BIT_64(14) | RT_BIT_64(15))
#define ARMV8_TCR_EL1_AARCH64_TG0_SHIFT                         14
#define ARMV8_TCR_EL1_AARCH64_TG0                               ARMV8_TCR_EL1_AARCH64_TG0_MASK
#define ARMV8_TCR_EL1_AARCH64_TG0_GET(a_Tcr)                    (((a_Tcr) & ARMV8_TCR_EL1_AARCH64_TG0) >> ARMV8_TCR_EL1_AARCH64_TG0_SHIFT)
/** Invalid granule size. */
# define ARMV8_TCR_EL1_AARCH64_TG0_INVALID                      0
/** 16KiB granule size (shifted down). */
# define ARMV8_TCR_EL1_AARCH64_TG0_16KB                         1
/** 4KiB granule size (shifted down). */
# define ARMV8_TCR_EL1_AARCH64_TG0_4KB                          2
/** 64KiB granule size (shifted down). */
# define ARMV8_TCR_EL1_AARCH64_TG0_64KB                         3
/** Bit 16 - 21 - Size offset of the memory region addressed by TTBR1_EL1 (2^(64-T1SZ)). */
#define ARMV8_TCR_EL1_AARCH64_T1SZ_MASK                         (  RT_BIT_64(16) | RT_BIT_64(17) | RT_BIT_64(18) \
                                                                 | RT_BIT_64(19) | RT_BIT_64(20) | RT_BIT_64(21))
#define ARMV8_TCR_EL1_AARCH64_T1SZ_SHIFT                        16
#define ARMV8_TCR_EL1_AARCH64_T1SZ                              ARMV8_TCR_EL1_AARCH64_T1SZ_MASK
#define ARMV8_TCR_EL1_AARCH64_T1SZ_GET(a_Tcr)                   (((a_Tcr) & ARMV8_TCR_EL1_AARCH64_T1SZ) >> ARMV8_TCR_EL1_AARCH64_T1SZ_SHIFT)
/** Bit 22 - Selects whether TTBR0_EL1 (0) or TTBR1_EL1 (1) defines the ASID. */
#define ARMV8_TCR_EL1_AARCH64_A1                                RT_BIT_64(22)
#define ARMV8_TCR_EL1_AARCH64_A1_BIT                            22
/** Bit 23 - Translation table walk disable for translations using TTBR1_EL1. */
#define ARMV8_TCR_EL1_AARCH64_EPD1                              RT_BIT_64(23)
#define ARMV8_TCR_EL1_AARCH64_EPD1_BIT                          23
/** Bit 24 - 25 - Inner cacheability attribute for memory associated with translation table walks using TTBR1_EL1. */
#define ARMV8_TCR_EL1_AARCH64_IRGN1                             (RT_BIT_64(24) | RT_BIT_64(25))
#define ARMV8_TCR_EL1_AARCH64_IRGN1_GET(a_Tcr)                  (((a_Tcr) & ARMV8_TCR_EL1_AARCH64_IRGN1) >> 26)
/** Non cacheable. */
# define ARMV8_TCR_EL1_AARCH64_IRGN1_NON_CACHEABLE              0
/** Write-Back, Read-Allocate, Write-Allocate Cacheable. */
# define ARMV8_TCR_EL1_AARCH64_IRGN1_WB_RA_WA                   1
/** Write-Through, Read-Allocate, No Write-Allocate Cacheable. */
# define ARMV8_TCR_EL1_AARCH64_IRGN1_WT_RA_NWA                  2
/** Write-Back, Read-Allocate, No Write-Allocate Cacheable. */
# define ARMV8_TCR_EL1_AARCH64_IRGN1_WB_RA_NWA                  3
/** Bit 27 - 26 - Outer cacheability attribute for memory associated with translation table walks using TTBR1_EL1. */
#define ARMV8_TCR_EL1_AARCH64_ORGN1                             (RT_BIT_64(26) | RT_BIT_64(27))
#define ARMV8_TCR_EL1_AARCH64_ORGN1_GET(a_Tcr)                  (((a_Tcr) & ARMV8_TCR_EL1_AARCH64_ORGN1) >> 26)
/** Non cacheable. */
# define ARMV8_TCR_EL1_AARCH64_ORGN1_NON_CACHEABLE              0
/** Write-Back, Read-Allocate, Write-Allocate Cacheable. */
# define ARMV8_TCR_EL1_AARCH64_ORGN1_WB_RA_WA                   1
/** Write-Through, Read-Allocate, No Write-Allocate Cacheable. */
# define ARMV8_TCR_EL1_AARCH64_ORGN1_WT_RA_NWA                  2
/** Write-Back, Read-Allocate, No Write-Allocate Cacheable. */
# define ARMV8_TCR_EL1_AARCH64_ORGN1_WB_RA_NWA                  3
/** Bit 28 - 29 - Shareability attribute memory associated with translation table walks using TTBR1_EL1. */
#define ARMV8_TCR_EL1_AARCH64_SH1                               (RT_BIT_64(28) | RT_BIT_64(29))
#define ARMV8_TCR_EL1_AARCH64_SH1_GET(a_Tcr)                    (((a_Tcr) & ARMV8_TCR_EL1_AARCH64_SH1) >> 28)
/** Non shareable. */
# define ARMV8_TCR_EL1_AARCH64_SH1_NON_SHAREABLE                0
/** Invalid value. */
# define ARMV8_TCR_EL1_AARCH64_SH1_INVALID                      1
/** Outer Shareable. */
# define ARMV8_TCR_EL1_AARCH64_SH1_OUTER_SHAREABLE              2
/** Inner Shareable. */
# define ARMV8_TCR_EL1_AARCH64_SH1_INNER_SHAREABLE              3
/** Bit 30 - 31 - Translation Granule Size for TTBR1_EL1. */
#define ARMV8_TCR_EL1_AARCH64_TG1_MASK                          (RT_BIT_64(30) | RT_BIT_64(31))
#define ARMV8_TCR_EL1_AARCH64_TG1_SHIFT                         30
#define ARMV8_TCR_EL1_AARCH64_TG1                               ARMV8_TCR_EL1_AARCH64_TG1_MASK
#define ARMV8_TCR_EL1_AARCH64_TG1_GET(a_Tcr)                    (((a_Tcr) & ARMV8_TCR_EL1_AARCH64_TG1) >> 30)
/** Invalid granule size. */
# define ARMV8_TCR_EL1_AARCH64_TG1_INVALID                      0
/** 16KiB granule size. */
# define ARMV8_TCR_EL1_AARCH64_TG1_16KB                         1
/** 4KiB granule size. */
# define ARMV8_TCR_EL1_AARCH64_TG1_4KB                          2
/** 64KiB granule size. */
# define ARMV8_TCR_EL1_AARCH64_TG1_64KB                         3
/** Bit 32 - 34 - Intermediate Physical Address Size. */
#define ARMV8_TCR_EL1_AARCH64_IPS                               (RT_BIT_64(32) | RT_BIT_64(33) | RT_BIT_64(34))
#define ARMV8_TCR_EL1_AARCH64_IPS_GET(a_Tcr)                    (((a_Tcr) & ARMV8_TCR_EL1_AARCH64_IPS) >> 32)
/** IPA - 32 bits, 4GiB. */
# define ARMV8_TCR_EL1_AARCH64_IPS_32BITS                       0
/** IPA - 36 bits, 64GiB. */
# define ARMV8_TCR_EL1_AARCH64_IPS_36BITS                       1
/** IPA - 40 bits, 1TiB. */
# define ARMV8_TCR_EL1_AARCH64_IPS_40BITS                       2
/** IPA - 42 bits, 4TiB. */
# define ARMV8_TCR_EL1_AARCH64_IPS_42BITS                       3
/** IPA - 44 bits, 16TiB. */
# define ARMV8_TCR_EL1_AARCH64_IPS_44BITS                       4
/** IPA - 48 bits, 256TiB. */
# define ARMV8_TCR_EL1_AARCH64_IPS_48BITS                       5
/** IPA - 52 bits, 4PiB. */
# define ARMV8_TCR_EL1_AARCH64_IPS_52BITS                       6
/** Bit 36 - ASID Size (0 - 8 bit, 1 - 16 bit). */
#define ARMV8_TCR_EL1_AARCH64_AS                                RT_BIT_64(36)
#define ARMV8_TCR_EL1_AARCH64_AS_BIT                            36
/** Bit 37 - Top Byte Ignore for translations from TTBR0_EL1. */
#define ARMV8_TCR_EL1_AARCH64_TBI0                              RT_BIT_64(37)
#define ARMV8_TCR_EL1_AARCH64_TBI0_BIT                          37
/** Bit 38 - Top Byte Ignore for translations from TTBR1_EL1. */
#define ARMV8_TCR_EL1_AARCH64_TBI1                              RT_BIT_64(38)
#define ARMV8_TCR_EL1_AARCH64_TBI1_BIT                          38
/** Bit 39 - Hardware Access flag update in stage 1 translations from EL0 and EL1. */
#define ARMV8_TCR_EL1_AARCH64_HA                                RT_BIT_64(39)
#define ARMV8_TCR_EL1_AARCH64_HA_BIT                            39
/** Bit 40 - Hardware management of dirty state in stage 1 translations from EL0 and EL1. */
#define ARMV8_TCR_EL1_AARCH64_HD                                RT_BIT_64(40)
#define ARMV8_TCR_EL1_AARCH64_HD_BIT                            40
/** Bit 41 - Hierarchical Permission Disables for TTBR0_EL1. */
#define ARMV8_TCR_EL1_AARCH64_HPD0                              RT_BIT_64(41)
#define ARMV8_TCR_EL1_AARCH64_HPD0_BIT                          41
/** Bit 42 - Hierarchical Permission Disables for TTBR1_EL1. */
#define ARMV8_TCR_EL1_AARCH64_HPD1                              RT_BIT_64(42)
#define ARMV8_TCR_EL1_AARCH64_HPD1_BIT                          42
/** Bit 43 - Bit[59] Hardware Use for translations using TTBR0_EL1. */
#define ARMV8_TCR_EL1_AARCH64_HWU059                            RT_BIT_64(43)
#define ARMV8_TCR_EL1_AARCH64_HWU059_BIT                        43
/** Bit 44 - Bit[60] Hardware Use for translations using TTBR0_EL1. */
#define ARMV8_TCR_EL1_AARCH64_HWU060                            RT_BIT_64(44)
#define ARMV8_TCR_EL1_AARCH64_HWU060_BIT                        44
/** Bit 46 - Bit[61] Hardware Use for translations using TTBR0_EL1. */
#define ARMV8_TCR_EL1_AARCH64_HWU061                            RT_BIT_64(45)
#define ARMV8_TCR_EL1_AARCH64_HWU061_BIT                        45
/** Bit 46 - Bit[62] Hardware Use for translations using TTBR0_EL1. */
#define ARMV8_TCR_EL1_AARCH64_HWU062                            RT_BIT_64(46)
#define ARMV8_TCR_EL1_AARCH64_HWU062_BIT                        46
/** Bit 47 - Bit[59] Hardware Use for translations using TTBR1_EL1. */
#define ARMV8_TCR_EL1_AARCH64_HWU159                            RT_BIT_64(47)
#define ARMV8_TCR_EL1_AARCH64_HWU159_BIT                        47
/** Bit 48 - Bit[60] Hardware Use for translations using TTBR1_EL1. */
#define ARMV8_TCR_EL1_AARCH64_HWU160                            RT_BIT_64(48)
#define ARMV8_TCR_EL1_AARCH64_HWU160_BIT                        48
/** Bit 49 - Bit[61] Hardware Use for translations using TTBR1_EL1. */
#define ARMV8_TCR_EL1_AARCH64_HWU161                            RT_BIT_64(49)
#define ARMV8_TCR_EL1_AARCH64_HWU161_BIT                        49
/** Bit 50 - Bit[62] Hardware Use for translations using TTBR1_EL1. */
#define ARMV8_TCR_EL1_AARCH64_HWU162                            RT_BIT_64(50)
#define ARMV8_TCR_EL1_AARCH64_HWU162_BIT                        50
/** Bit 51 - Control the use of the top byte of instruction addresses for address matching for translations using TTBR0_EL1. */
#define ARMV8_TCR_EL1_AARCH64_TBID0                             RT_BIT_64(51)
#define ARMV8_TCR_EL1_AARCH64_TBID0_BIT                         51
/** Bit 52 - Control the use of the top byte of instruction addresses for address matching for translations using TTBR1_EL1. */
#define ARMV8_TCR_EL1_AARCH64_TBID1                             RT_BIT_64(52)
#define ARMV8_TCR_EL1_AARCH64_TBID1_BIT                         52
/** Bit 53 - Non fault translation table walk disable for stage 1 translations using TTBR0_EL1. */
#define ARMV8_TCR_EL1_AARCH64_NFD0                              RT_BIT_64(53)
#define ARMV8_TCR_EL1_AARCH64_NFD0_BIT                          53
/** Bit 54 - Non fault translation table walk disable for stage 1 translations using TTBR1_EL1. */
#define ARMV8_TCR_EL1_AARCH64_NFD1                              RT_BIT_64(54)
#define ARMV8_TCR_EL1_AARCH64_NFD1_BIT                          54
/** Bit 55 - Faulting Control for Unprivileged access to any address translated by TTBR0_EL1. */
#define ARMV8_TCR_EL1_AARCH64_E0PD0                             RT_BIT_64(55)
#define ARMV8_TCR_EL1_AARCH64_E0PD0_BIT                         55
/** Bit 56 - Faulting Control for Unprivileged access to any address translated by TTBR1_EL1. */
#define ARMV8_TCR_EL1_AARCH64_E0PD1                             RT_BIT_64(56)
#define ARMV8_TCR_EL1_AARCH64_E0PD1_BIT                         56
/** Bit 57 - TCMA0 */
#define ARMV8_TCR_EL1_AARCH64_TCMA0                             RT_BIT_64(57)
#define ARMV8_TCR_EL1_AARCH64_TCMA0_BIT                         57
/** Bit 58 - TCMA1 */
#define ARMV8_TCR_EL1_AARCH64_TCMA1                             RT_BIT_64(58)
#define ARMV8_TCR_EL1_AARCH64_TCMA1_BIT                         58
/** Bit 59 - Data Sharing(?). */
#define ARMV8_TCR_EL1_AARCH64_DS                                RT_BIT_64(59)
#define ARMV8_TCR_EL1_AARCH64_DS_BIT                            59
/** @} */


/** @name TTBR<0,1>_EL1 - Translation Table Base Register <0,1> (EL1)
 * @{
 */
/** Bit 0 - Common not Private (FEAT_TTCNP). */
#define ARMV8_TTBR_EL1_AARCH64_CNP                              RT_BIT_64(0)
#define ARMV8_TTBR_EL1_AARCH64_CNP_BIT                          0
/** Bit 1 - 47 - Translation table base address. */
#define ARMV8_TTBR_EL1_AARCH64_BADDR                            UINT64_C(0x0000fffffffffffe)
#define ARMV8_TTBR_EL1_AARCH64_BADDR_GET(a_Ttbr)                ((a_Ttbr) & ARMV8_TTBR_EL1_AARCH64_BADDR)
/** Bit 48 - 63 - ASID. */
#define ARMV8_TTBR_EL1_AARCH64_ASID                             UINT64_C(0xffff000000000000)
#define ARMV8_TTBR_EL1_AARCH64_ASID_GET(a_Ttbr)                 (((a_Ttbr) & ARMV8_TTBR_EL1_AARCH64_ASID) >> 48)
/** @} */


/** @name MDSCR_EL1 - MOnitor Debug System Control Register (EL1).
 * @{ */
/** Bit 0 - SS - Software step control bit. */
#define ARMV8_MDSCR_EL1_AARCH64_SS                              RT_BIT_64(ARMV8_MDSCR_EL1_AARCH64_SS_BIT)
#define ARMV8_MDSCR_EL1_AARCH64_SS_BIT                          0
/** Bit 6 - ERR. */
#define ARMV8_MDSCR_EL1_AARCH64_ERR                             RT_BIT_64(ARMV8_MDSCR_EL1_AARCH64_ERR_BIT)
#define ARMV8_MDSCR_EL1_AARCH64_ERR_BIT                         6
/** Bit 12 - TDCC. */
#define ARMV8_MDSCR_EL1_AARCH64_TDCC                            RT_BIT_64(ARMV8_MDSCR_EL1_AARCH64_TDCC_BIT)
#define ARMV8_MDSCR_EL1_AARCH64_TDCC_BIT                        12
/** Bit 13 - KDE - Kernel Debugging Enabled. */
#define ARMV8_MDSCR_EL1_AARCH64_KDE                             RT_BIT_64(ARMV8_MDSCR_EL1_AARCH64_KDE_BIT)
#define ARMV8_MDSCR_EL1_AARCH64_KDE_BIT                         13
/** Bit 14 - HDE. */
#define ARMV8_MDSCR_EL1_AARCH64_HDE                             RT_BIT_64(ARMV8_MDSCR_EL1_AARCH64_HDE_BIT)
#define ARMV8_MDSCR_EL1_AARCH64_HDE_BIT                         14
/** Bit 15 - MDE. */
#define ARMV8_MDSCR_EL1_AARCH64_MDE                             RT_BIT_64(ARMV8_MDSCR_EL1_AARCH64_MDE_BIT)
#define ARMV8_MDSCR_EL1_AARCH64_MDE_BIT                         15
/** Bit 19 - SC2. */
#define ARMV8_MDSCR_EL1_AARCH64_SC2                             RT_BIT_64(ARMV8_MDSCR_EL1_AARCH64_SC2_BIT)
#define ARMV8_MDSCR_EL1_AARCH64_SC2_BIT                         19
/** Bit 21 - TDA. */
#define ARMV8_MDSCR_EL1_AARCH64_TDA                             RT_BIT_64(ARMV8_MDSCR_EL1_AARCH64_TDA_BIT)
#define ARMV8_MDSCR_EL1_AARCH64_TDA_BIT                         21
/** Bits 23:22 - INTdis. */
#define ARMV8_MDSCR_EL1_AARCH64_INTDIS_MASK                     UINT64_C(0x00c00000)
#define ARMV8_MDSCR_EL1_AARCH64_INTDIS_SHIFT                    22
/** Bit 26 - TXU. */
#define ARMV8_MDSCR_EL1_AARCH64_TXU                             RT_BIT_64(ARMV8_MDSCR_EL1_AARCH64_TXU_BIT)
#define ARMV8_MDSCR_EL1_AARCH64_TXU_BIT                         26
/** Bit 29 - TXfull. */
#define ARMV8_MDSCR_EL1_AARCH64_TXFULL                          RT_BIT_64(ARMV8_MDSCR_EL1_AARCH64_TXFULL_BIT)
#define ARMV8_MDSCR_EL1_AARCH64_TXFULL_BIT                      29
/** Bit 30 - RXfull. */
#define ARMV8_MDSCR_EL1_AARCH64_RXFULL                          RT_BIT_64(ARMV8_MDSCR_EL1_AARCH64_RXFULL_BIT)
#define ARMV8_MDSCR_EL1_AARCH64_RXFULL_BIT                      30
/** Bit 31 - TFO. */
#define ARMV8_MDSCR_EL1_AARCH64_TFO                             RT_BIT_64(ARMV8_MDSCR_EL1_AARCH64_TFO_BIT)
#define ARMV8_MDSCR_EL1_AARCH64_TFO_BIT                         31
/** Bit 32 - EMBWE. */
#define ARMV8_MDSCR_EL1_AARCH64_EMBWE                           RT_BIT_64(ARMV8_MDSCR_EL1_AARCH64_EMBWE_BIT)
#define ARMV8_MDSCR_EL1_AARCH64_EMBWE_BIT                       32
/** Bit 33 - TTA. */
#define ARMV8_MDSCR_EL1_AARCH64_TTA                             RT_BIT_64(ARMV8_MDSCR_EL1_AARCH64_TTA_BIT)
#define ARMV8_MDSCR_EL1_AARCH64_TTA_BIT                         33
/** Bit 34 - EnSPM. */
#define ARMV8_MDSCR_EL1_AARCH64_ENSPM                           RT_BIT_64(ARMV8_MDSCR_EL1_AARCH64_ENSPM_BIT)
#define ARMV8_MDSCR_EL1_AARCH64_ENSPM_BIT                       34
/** Bit 35 - EHBWE. */
#define ARMV8_MDSCR_EL1_AARCH64_EHBWE                           RT_BIT_64(ARMV8_MDSCR_EL1_AARCH64_EHBWE_BIT)
#define ARMV8_MDSCR_EL1_AARCH64_EHBWE_BIT                       35
/** Bit 50 - EnSTEPOP. */
#define ARMV8_MDSCR_EL1_AARCH64_ENSTEPOP                        RT_BIT_64(ARMV8_MDSCR_EL1_AARCH64_ENSTEPOP_BIT)
#define ARMV8_MDSCR_EL1_AARCH64_ENSTEPOP_BIT                    50
/** @} */


/** @name ICC_PMR_EL1 - Interrupt Controller Interrupt Priority Mask Register
 * @{ */
/** Bit 0 - 7 - Priority - The priority mask level for the CPU interface. */
#define ARMV8_ICC_PMR_EL1_AARCH64_PRIORITY                      UINT64_C(0xff)
#define ARMV8_ICC_PMR_EL1_AARCH64_PRIORITY_GET(a_Pmr)           ((a_Pmr) & ARMV8_ICC_PMR_EL1_AARCH64_PRIORITY)
#define ARMV8_ICC_PMR_EL1_AARCH64_PRIORITY_SET(a_Prio)          ((a_Prio) & ARMV8_ICC_PMR_EL1_AARCH64_PRIORITY)
/** @} */


/** @name ICC_BPR0_EL1 - The group priority for Group 0 interrupts.
 * @{ */
/** Bit 0 - 2 - BinaryPoint - Controls how the 8-bit interrupt priority field is split into a group priority and subpriority field. */
#define ARMV8_ICC_BPR0_EL1_AARCH64_BINARYPOINT                  (RT_BIT_64(0) | RT_BIT_64(1) | RT_BIT_64(2))
#define ARMV8_ICC_BPR0_EL1_AARCH64_BINARYPOINT_GET(a_Bpr0)      ((a_Bpr0) & ARMV8_ICC_BPR0_EL1_AARCH64_BINARYPOINT)
#define ARMV8_ICC_BPR0_EL1_AARCH64_BINARYPOINT_SET(a_BinaryPt)  ((a_BinaryPt) & ARMV8_ICC_BPR0_EL1_AARCH64_BINARYPOINT)
/** @} */


/** @name ICC_BPR1_EL1 - The group priority for Group 1 interrupts.
 * @{ */
/** Bit 0 - 2 - BinaryPoint - Controls how the 8-bit interrupt priority field is split into a group priority and subpriority field. */
#define ARMV8_ICC_BPR1_EL1_AARCH64_BINARYPOINT                  (RT_BIT_64(0) | RT_BIT_64(1) | RT_BIT_64(2))
#define ARMV8_ICC_BPR1_EL1_AARCH64_BINARYPOINT_GET(a_Bpr1)      ((a_Bpr1) & ARMV8_ICC_BPR1_EL1_AARCH64_BINARYPOINT)
#define ARMV8_ICC_BPR1_EL1_AARCH64_BINARYPOINT_SET(a_BinaryPt)  ((a_BinaryPt) & ARMV8_ICC_BPR1_EL1_AARCH64_BINARYPOINT)
/** @} */


/** @name ICC_CTLR_EL1 - Interrupt Controller Control Register (EL1)
 * @{ */
/** Bit 0 - Common Binary Pointer Register - RW. */
#define ARMV8_ICC_CTLR_EL1_AARCH64_CBPR                         RT_BIT_64(0)
#define ARMV8_ICC_CTLR_EL1_AARCH64_CBPR_BIT                     0
/** Bit 1 - EOI mode for current security state, when set ICC_DIR_EL1 provides interrupt deactivation functionality - RW. */
#define ARMV8_ICC_CTLR_EL1_AARCH64_EOIMODE                      RT_BIT_64(1)
#define ARMV8_ICC_CTLR_EL1_AARCH64_EOIMODE_BIT                  1
/** Bit 7 - Priority Mask Hint Enable - RW (under circumstances). */
#define ARMV8_ICC_CTLR_EL1_AARCH64_PMHE                         RT_BIT_64(6)
#define ARMV8_ICC_CTLR_EL1_AARCH64_PMHE_BIT                     6
/** Bit 8 - 10 - Priority bits - RO. */
#define ARMV8_ICC_CTLR_EL1_AARCH64_PRIBITS                      (RT_BIT_64(8) | RT_BIT_64(9) | RT_BIT_64(10))
#define ARMV8_ICC_CTLR_EL1_AARCH64_PRIBITS_SET(a_PriBits)       (((a_PriBits) << 8) & ARMV8_ICC_CTLR_EL1_AARCH64_PRIBITS)
/** Bit 11 - 13 - Interrupt identifier bits - RO. */
#define ARMV8_ICC_CTLR_EL1_AARCH64_IDBITS                       (RT_BIT_64(11) | RT_BIT_64(12) | RT_BIT_64(13))
#define ARMV8_ICC_CTLR_EL1_AARCH64_IDBITS_SET(a_IdBits)         (((a_IdBits) << 11) & ARMV8_ICC_CTLR_EL1_AARCH64_IDBITS)
/** INTIDS are 16-bit wide. */
# define ARMV8_ICC_CTLR_EL1_AARCH64_IDBITS_16BITS               0
/** INTIDS are 24-bit wide. */
# define ARMV8_ICC_CTLR_EL1_AARCH64_IDBITS_24BITS               1
/** Bit 14 - SEI Supported - RO. */
#define ARMV8_ICC_CTLR_EL1_AARCH64_SEIS                         RT_BIT_64(14)
#define ARMV8_ICC_CTLR_EL1_AARCH64_SEIS_BIT                     14
/** Bit 15 - Affinity 3 Valid - RO. */
#define ARMV8_ICC_CTLR_EL1_AARCH64_A3V                          RT_BIT_64(15)
#define ARMV8_ICC_CTLR_EL1_AARCH64_A3V_BIT                      15
/** Bit 18 - Range Selector Support - RO. */
#define ARMV8_ICC_CTLR_EL1_AARCH64_RSS                          RT_BIT_64(18)
#define ARMV8_ICC_CTLR_EL1_AARCH64_RSS_BIT                      18
/** Bit 19 - Extended INTID range supported - RO. */
#define ARMV8_ICC_CTLR_EL1_AARCH64_EXTRANGE                     RT_BIT_64(19)
#define ARMV8_ICC_CTLR_EL1_AARCH64_EXTRANGE_BIT                 19
/** RW bits when GICD_CTLR.DS (disable-security) is set. */
#define ARMV8_ICC_CTLR_EL1_DS_RW                                (ARMV8_ICC_CTLR_EL1_AARCH64_CBPR | ARMV8_ICC_CTLR_EL1_AARCH64_EOIMODE)
/** RW bits when GICD_CTLR.DS (disable-security) is clear. */
#define ARMV8_ICC_CTLR_EL1_RW                                   (ARMV8_ICC_CTLR_EL1_AARCH64_CBPR | ARMV8_ICC_CTLR_EL1_AARCH64_EOIMODE | ARMV8_ICC_CTLR_EL1_AARCH64_PMHE)
/** All RO bits (including Res0). */
#define ARMV8_ICC_CTLR_EL1_RO                                   ~ARMV8_ICC_CTLR_EL1_RW
/** @} */


/** @name ICC_IGRPEN0_EL1 - Interrupt Controller Interrupt Group 0 Enable Register (EL1)
 * @{ */
/** Bit 0 - Enables Group 0 interrupts for the current Security state. */
#define ARMV8_ICC_IGRPEN0_EL1_AARCH64_ENABLE                    RT_BIT_64(0)
#define ARMV8_ICC_IGRPEN0_EL1_AARCH64_ENABLE_BIT                0
/** @} */


/** @name ICC_IGRPEN1_EL1 - Interrupt Controller Interrupt Group 1 Enable Register (EL1)
 * @{ */
/** Bit 0 - Enables Group 1 interrupts for the current Security state. */
#define ARMV8_ICC_IGRPEN1_EL1_AARCH64_ENABLE                    RT_BIT_64(0)
#define ARMV8_ICC_IGRPEN1_EL1_AARCH64_ENABLE_BIT                0
/** @} */


/** @name ICC_SGI1R_EL1 - Interrupt Controller Software Generated Interrupt Group 1 Register (EL1) - WO
 * @{ */
/** Bit 0 - 15 - Target List, the set of PEs for which SGI interrupts will be generated. */
#define ARMV8_ICC_SGI1R_EL1_AARCH64_TARGET_LIST                 (UINT64_C(0x000000000000ffff))
#define ARMV8_ICC_SGI1R_EL1_AARCH64_TARGET_LIST_GET(a_Sgi1R)    ((a_Sgi1R) & ARMV8_ICC_SGI1R_EL1_AARCH64_TARGET_LIST)
/** Bit 16 - 23 - The affinity 1 of the affinity path of the cluster for which SGI interrupts will be generated. */
#define ARMV8_ICC_SGI1R_EL1_AARCH64_AFF1                        (UINT64_C(0x00000000007f0000))
#define ARMV8_ICC_SGI1R_EL1_AARCH64_AFF1_GET(a_Sgi1R)           (((a_Sgi1R) & ARMV8_ICC_SGI1R_EL1_AARCH64_AFF1) >> 16)
/** Bit 24 - 27 - The INTID of the SGI. */
#define ARMV8_ICC_SGI1R_EL1_AARCH64_INTID                       (RT_BIT_64(24) | RT_BIT_64(25) | RT_BIT_64(26) | RT_BIT_64(27))
#define ARMV8_ICC_SGI1R_EL1_AARCH64_INTID_GET(a_Sgi1R)          (((a_Sgi1R) & ARMV8_ICC_SGI1R_EL1_AARCH64_INTID) >> 24)
/* Bit 28 - 31 - Reserved. */
/** Bit 32 - 39 - The affinity 2 of the affinity path of the cluster for which SGI interrupts will be generated. */
#define ARMV8_ICC_SGI1R_EL1_AARCH64_AFF2                        (UINT64_C(0x000000ff00000000))
#define ARMV8_ICC_SGI1R_EL1_AARCH64_AFF2_GET(a_Sgi1R)           (((a_Sgi1R) & ARMV8_ICC_SGI1R_EL1_AARCH64_AFF2) >> 32)
/** Bit 40 - Interrupt Routing Mode - 1 means interrupts to all PEs in the system excluding the generating PE. */
#define ARMV8_ICC_SGI1R_EL1_AARCH64_IRM                         RT_BIT_64(40)
#define ARMV8_ICC_SGI1R_EL1_AARCH64_IRM_BIT                     40
/* Bit 41 - 43 - Reserved. */
/** Bit 44 - 47 - Range selector. */
#define ARMV8_ICC_SGI1R_EL1_AARCH64_RS                          (RT_BIT_64(44) | RT_BIT_64(45) | RT_BIT_64(46) | RT_BIT_64(47))
#define ARMV8_ICC_SGI1R_EL1_AARCH64_RS_GET(a_Sgi1R)             (((a_Sgi1R) & ARMV8_ICC_SGI1R_EL1_AARCH64_RS) >> 44)
/** Bit 48 - 55 - The affinity 3 of the affinity path of the cluster for which SGI interrupts will be generated. */
#define ARMV8_ICC_SGI1R_EL1_AARCH64_AFF3                        (UINT64_C(0x00ff000000000000))
#define ARMV8_ICC_SGI1R_EL1_AARCH64_AFF3_GET(a_Sgi1R)           (((a_Sgi1R) & ARMV8_ICC_SGI1R_EL1_AARCH64_AFF3) >> 48)
/* Bit 56 - 63 - Reserved. */
/** @} */


/** @name CNTV_CTL_EL0 - Counter-timer Virtual Timer Control register.
 * @{ */
/** Bit 0 - Enables the timer. */
#define ARMV8_CNTV_CTL_EL0_AARCH64_ENABLE                       RT_BIT_64(0)
#define ARMV8_CNTV_CTL_EL0_AARCH64_ENABLE_BIT                   0
/** Bit 1 - Timer interrupt mask bit. */
#define ARMV8_CNTV_CTL_EL0_AARCH64_IMASK                        RT_BIT_64(1)
#define ARMV8_CNTV_CTL_EL0_AARCH64_IMASK_BIT                    1
/** Bit 2 - Timer status bit. */
#define ARMV8_CNTV_CTL_EL0_AARCH64_ISTATUS                      RT_BIT_64(2)
#define ARMV8_CNTV_CTL_EL0_AARCH64_ISTATUS_BIT                  2
/** @} */


/** @name OSLAR_EL1 - OS Lock Access Register.
 * @{ */
/** Bit 0 - The OS Lock status bit. */
#define ARMV8_OSLAR_EL1_AARCH64_OSLK                            RT_BIT_64(0)
#define ARMV8_OSLAR_EL1_AARCH64_OSLK_BIT                        0
/** @} */


/** @name OSLSR_EL1 - OS Lock Status Register.
 * @{ */
/** Bit 0 - OSLM[0] Bit 0 of OS Lock model implemented. */
#define ARMV8_OSLSR_EL1_AARCH64_OSLM0                           RT_BIT_64(0)
#define ARMV8_OSLSR_EL1_AARCH64_OSLM0_BIT                       0
/** Bit 1 - The OS Lock status bit. */
#define ARMV8_OSLSR_EL1_AARCH64_OSLK                            RT_BIT_64(1)
#define ARMV8_OSLSR_EL1_AARCH64_OSLK_BIT                        1
/** Bit 2 - Not 32-bit access. */
#define ARMV8_OSLSR_EL1_AARCH64_NTT                             RT_BIT_64(2)
#define ARMV8_OSLSR_EL1_AARCH64_NTT_BIT                         2
/** Bit 0 - OSLM[1] Bit 1 of OS Lock model implemented. */
#define ARMV8_OSLSR_EL1_AARCH64_OSLM1                           RT_BIT_64(3)
#define ARMV8_OSLSR_EL1_AARCH64_OSLM1_BIT                       3
/** @} */


/** @name ID_AA64ISAR0_EL1 - AArch64 Instruction Set Attribute Register 0.
 * @{ */
/* Bit 0 - 3 - Reserved. */
/** Bit 4 - 7 - Indicates support for AES instructions in AArch64 state. */
#define ARMV8_ID_AA64ISAR0_EL1_AES_MASK                         (RT_BIT_64(4) | RT_BIT_64(5) | RT_BIT_64(6) | RT_BIT_64(7))
#define ARMV8_ID_AA64ISAR0_EL1_AES_SHIFT                        4
/** No AES instructions implemented. */
# define ARMV8_ID_AA64ISAR0_EL1_AES_NOT_IMPL                    0
/** AES, AESD, AESMC and AESIMC instructions implemented (FEAT_AES). */
# define ARMV8_ID_AA64ISAR0_EL1_AES_SUPPORTED                   1
/** AES, AESD, AESMC and AESIMC instructions implemented and PMULL and PMULL2 instructions operating on 64bit source elements (FEAT_PMULL). */
# define ARMV8_ID_AA64ISAR0_EL1_AES_SUPPORTED_PMULL             2
/** Bit 8 - 11 - Indicates support for SHA1 instructions in AArch64 state. */
#define ARMV8_ID_AA64ISAR0_EL1_SHA1_MASK                        (RT_BIT_64(8) | RT_BIT_64(9) | RT_BIT_64(10) | RT_BIT_64(11))
#define ARMV8_ID_AA64ISAR0_EL1_SHA1_SHIFT                       8
/** No SHA1 instructions implemented. */
# define ARMV8_ID_AA64ISAR0_EL1_SHA1_NOT_IMPL                   0
/** SHA1C, SHA1P, SHA1M, SHA1H, SHA1SU0 and SHA1SU1 instructions implemented (FEAT_SHA1). */
# define ARMV8_ID_AA64ISAR0_EL1_SHA1_SUPPORTED                  1
/** Bit 12 - 15 - Indicates support for SHA2 instructions in AArch64 state. */
#define ARMV8_ID_AA64ISAR0_EL1_SHA2_MASK                        (RT_BIT_64(12) | RT_BIT_64(13) | RT_BIT_64(14) | RT_BIT_64(15))
#define ARMV8_ID_AA64ISAR0_EL1_SHA2_SHIFT                       12
/** No SHA2 instructions implemented. */
# define ARMV8_ID_AA64ISAR0_EL1_SHA2_NOT_IMPL                   0
/** SHA256 instructions implemented (FEAT_SHA256). */
# define ARMV8_ID_AA64ISAR0_EL1_SHA2_SUPPORTED_SHA256           1
/** SHA256 and SHA512 instructions implemented (FEAT_SHA512). */
# define ARMV8_ID_AA64ISAR0_EL1_SHA2_SUPPORTED_SHA256_SHA512    2
/** Bit 16 - 19 - Indicates support for CRC32 instructions in AArch64 state. */
#define ARMV8_ID_AA64ISAR0_EL1_CRC32_MASK                       (RT_BIT_64(16) | RT_BIT_64(17) | RT_BIT_64(18) | RT_BIT_64(19))
#define ARMV8_ID_AA64ISAR0_EL1_CRC32_SHIFT                      16
/** No CRC32 instructions implemented. */
# define ARMV8_ID_AA64ISAR0_EL1_CRC32_NOT_IMPL                  0
/** CRC32 instructions implemented (FEAT_CRC32). */
# define ARMV8_ID_AA64ISAR0_EL1_CRC32_SUPPORTED                 1
/** Bit 20 - 23 - Indicates support for Atomic instructions in AArch64 state. */
#define ARMV8_ID_AA64ISAR0_EL1_ATOMIC_MASK                      (RT_BIT_64(20) | RT_BIT_64(21) | RT_BIT_64(22) | RT_BIT_64(23))
#define ARMV8_ID_AA64ISAR0_EL1_ATOMIC_SHIFT                     20
/** No Atomic instructions implemented. */
# define ARMV8_ID_AA64ISAR0_EL1_ATOMIC_NOT_IMPL                 0
/** Atomic instructions implemented (FEAT_LSE). */
# define ARMV8_ID_AA64ISAR0_EL1_ATOMIC_SUPPORTED                2
/** Bit 24 - 27 - Indicates support for TME instructions. */
#define ARMV8_ID_AA64ISAR0_EL1_TME_MASK                         (RT_BIT_64(24) | RT_BIT_64(25) | RT_BIT_64(26) | RT_BIT_64(27))
#define ARMV8_ID_AA64ISAR0_EL1_TME_SHIFT                        24
/** TME instructions are not implemented. */
# define ARMV8_ID_AA64ISAR0_EL1_TME_NOT_IMPL                    0
/** TME instructions are implemented. */
# define ARMV8_ID_AA64ISAR0_EL1_TME_SUPPORTED                   1
/** Bit 28 - 31 - Indicates support for SQRDMLAH and SQRDMLSH instructions in AArch64 state. */
#define ARMV8_ID_AA64ISAR0_EL1_RDM_MASK                         (RT_BIT_64(28) | RT_BIT_64(29) | RT_BIT_64(30) | RT_BIT_64(31))
#define ARMV8_ID_AA64ISAR0_EL1_RDM_SHIFT                        28
/** No RDMA instructions implemented. */
# define ARMV8_ID_AA64ISAR0_EL1_RDM_NOT_IMPL                    0
/** SQRDMLAH and SQRDMLSH instructions implemented (FEAT_RDM). */
# define ARMV8_ID_AA64ISAR0_EL1_RDM_SUPPORTED                   1
/** Bit 32 - 35 - Indicates support for SHA3 instructions in AArch64 state. */
#define ARMV8_ID_AA64ISAR0_EL1_SHA3_MASK                        (RT_BIT_64(32) | RT_BIT_64(33) | RT_BIT_64(34) | RT_BIT_64(35))
#define ARMV8_ID_AA64ISAR0_EL1_SHA3_SHIFT                       32
/** No SHA3 instructions implemented. */
# define ARMV8_ID_AA64ISAR0_EL1_SHA3_NOT_IMPL                   0
/** EOR3, RAX1, XAR and BCAX instructions implemented (FEAT_SHA3). */
# define ARMV8_ID_AA64ISAR0_EL1_SHA3_SUPPORTED                  1
/** Bit 36 - 39 - Indicates support for SM3 instructions in AArch64 state. */
#define ARMV8_ID_AA64ISAR0_EL1_SM3_MASK                         (RT_BIT_64(36) | RT_BIT_64(37) | RT_BIT_64(38) | RT_BIT_64(39))
#define ARMV8_ID_AA64ISAR0_EL1_SM3_SHIFT                        36
/** No SM3 instructions implemented. */
# define ARMV8_ID_AA64ISAR0_EL1_SM3_NOT_IMPL                    0
/** SM3 instructions implemented (FEAT_SM3). */
# define ARMV8_ID_AA64ISAR0_EL1_SM3_SUPPORTED                   1
/** Bit 40 - 43 - Indicates support for SM4 instructions in AArch64 state. */
#define ARMV8_ID_AA64ISAR0_EL1_SM4_MASK                         (RT_BIT_64(40) | RT_BIT_64(41) | RT_BIT_64(42) | RT_BIT_64(43))
#define ARMV8_ID_AA64ISAR0_EL1_SM4_SHIFT                        40
/** No SM4 instructions implemented. */
# define ARMV8_ID_AA64ISAR0_EL1_SM4_NOT_IMPL                    0
/** SM4 instructions implemented (FEAT_SM4). */
# define ARMV8_ID_AA64ISAR0_EL1_SM4_SUPPORTED                   1
/** Bit 44 - 47 - Indicates support for Dot Product instructions in AArch64 state. */
#define ARMV8_ID_AA64ISAR0_EL1_DP_MASK                          (RT_BIT_64(44) | RT_BIT_64(45) | RT_BIT_64(46) | RT_BIT_64(47))
#define ARMV8_ID_AA64ISAR0_EL1_DP_SHIFT                         44
/** No Dot Product instructions implemented. */
# define ARMV8_ID_AA64ISAR0_EL1_DP_NOT_IMPL                     0
/** UDOT and SDOT instructions implemented (FEAT_DotProd). */
# define ARMV8_ID_AA64ISAR0_EL1_DP_SUPPORTED                    1
/** Bit 48 - 51 - Indicates support for FMLAL and FMLSL instructions. */
#define ARMV8_ID_AA64ISAR0_EL1_FHM_MASK                         (RT_BIT_64(48) | RT_BIT_64(49) | RT_BIT_64(50) | RT_BIT_64(51))
#define ARMV8_ID_AA64ISAR0_EL1_FHM_SHIFT                        48
/** FMLAL and FMLSL instructions are not implemented. */
# define ARMV8_ID_AA64ISAR0_EL1_FHM_NOT_IMPL                    0
/** FMLAL and FMLSL instructions are implemented (FEAT_FHM). */
# define ARMV8_ID_AA64ISAR0_EL1_FHM_SUPPORTED                   1
/** Bit 52 - 55 - Indicates support for flag manipulation instructions. */
#define ARMV8_ID_AA64ISAR0_EL1_TS_MASK                          (RT_BIT_64(52) | RT_BIT_64(53) | RT_BIT_64(54) | RT_BIT_64(55))
#define ARMV8_ID_AA64ISAR0_EL1_TS_SHIFT                         52
/** No flag manipulation instructions implemented. */
# define ARMV8_ID_AA64ISAR0_EL1_TS_NOT_IMPL                     0
/** CFINV, RMIF, SETF16 and SETF8 instrutions are implemented (FEAT_FlagM). */
# define ARMV8_ID_AA64ISAR0_EL1_TS_SUPPORTED                    1
/** CFINV, RMIF, SETF16, SETF8, AXFLAG and XAFLAG instrutions are implemented (FEAT_FlagM2). */
# define ARMV8_ID_AA64ISAR0_EL1_TS_SUPPORTED_2                  2
/** Bit 56 - 59 - Indicates support for Outer Shareable and TLB range maintenance instructions. */
#define ARMV8_ID_AA64ISAR0_EL1_TLB_MASK                         (RT_BIT_64(56) | RT_BIT_64(57) | RT_BIT_64(58) | RT_BIT_64(59))
#define ARMV8_ID_AA64ISAR0_EL1_TLB_SHIFT                        56
/** Outer Sahreable and TLB range maintenance instructions are not implemented. */
# define ARMV8_ID_AA64ISAR0_EL1_TLB_NOT_IMPL                    0
/** Outer Shareable TLB maintenance instructions are implemented (FEAT_TLBIOS). */
# define ARMV8_ID_AA64ISAR0_EL1_TLB_SUPPORTED                   1
/** Outer Shareable and TLB range maintenance instructions are implemented (FEAT_TLBIRANGE). */
# define ARMV8_ID_AA64ISAR0_EL1_TLB_SUPPORTED_RANGE             2
/** Bit 60 - 63 - Indicates support for Random Number instructons in AArch64 state. */
#define ARMV8_ID_AA64ISAR0_EL1_RNDR_MASK                        (RT_BIT_64(60) | RT_BIT_64(61) | RT_BIT_64(62) | RT_BIT_64(63))
#define ARMV8_ID_AA64ISAR0_EL1_RNDR_SHIFT                       60
/** No Random Number instructions implemented. */
# define ARMV8_ID_AA64ISAR0_EL1_RNDR_NOT_IMPL                   0
/** RNDR and RDNRRS registers are implemented . */
# define ARMV8_ID_AA64ISAR0_EL1_RNDR_SUPPORTED                  1
/** @} */


/** @name ID_AA64ISAR1_EL1 - AArch64 Instruction Set Attribute Register 0.
 * @{ */
/** Bit 0 - 3 - Indicates support for Data Persistence writeback instructions in AArch64 state. */
#define ARMV8_ID_AA64ISAR1_EL1_DPB_MASK                         (RT_BIT_64(0) | RT_BIT_64(1) | RT_BIT_64(2) | RT_BIT_64(3))
#define ARMV8_ID_AA64ISAR1_EL1_DPB_SHIFT                        0
/** DC CVAP not supported. */
# define ARMV8_ID_AA64ISAR1_EL1_DPB_NOT_IMPL                    0
/** DC CVAP supported (FEAT_DPB). */
# define ARMV8_ID_AA64ISAR1_EL1_DPB_SUPPORTED                   1
/** DC CVAP and DC CVADP supported (FEAT_DPB2). */
# define ARMV8_ID_AA64ISAR1_EL1_DPB_SUPPORTED_2                 2
/** Bit 4 - 7 - Indicates whether QARMA5 algorithm is implemented in the PE for address authentication. */
#define ARMV8_ID_AA64ISAR1_EL1_APA_MASK                         (RT_BIT_64(4) | RT_BIT_64(5) | RT_BIT_64(6) | RT_BIT_64(7))
#define ARMV8_ID_AA64ISAR1_EL1_APA_SHIFT                        4
/** Address Authentication using the QARMA5 algorithm is not implemented. */
# define ARMV8_ID_AA64ISAR1_EL1_APA_NOT_IMPL                    0
/** Address Authentication using the QARMA5 algorithm is implemented (FEAT_PAuth, FEAT_PACQARMA5). */
# define ARMV8_ID_AA64ISAR1_EL1_APA_SUPPORTED_PAUTH             1
/** Address Authentication using the QARMA5 algorithm is implemented and enhanced PAC is supported (FEAT_EPAC, FEAT_PACQARMA5). */
# define ARMV8_ID_AA64ISAR1_EL1_APA_SUPPORTED_EPAC              2
/** Address Authentication using the QARMA5 algorithm is implemented and enhanced PAC 2 is supported (FEAT_PAuth2, FEAT_PACQARMA5). */
# define ARMV8_ID_AA64ISAR1_EL1_APA_SUPPORTED_PAUTH2            3
/** Address Authentication using the QARMA5 algorithm is implemented and enhanced PAC 2 and FPAC are supported (FEAT_FPAC, FEAT_PACQARMA5). */
# define ARMV8_ID_AA64ISAR1_EL1_APA_SUPPORTED_FPAC              4
/** Address Authentication using the QARMA5 algorithm is implemented and enhanced PAC 2 and combined FPAC are supported (FEAT_FPACCOMBINE, FEAT_PACQARMA5). */
# define ARMV8_ID_AA64ISAR1_EL1_APA_SUPPORTED_FPACCOMBINE       5
/** Bit 8 - 11 - Indicates whether an implementation defined algorithm is implemented in the PE for address authentication. */
#define ARMV8_ID_AA64ISAR1_EL1_API_MASK                         (RT_BIT_64(8) | RT_BIT_64(9) | RT_BIT_64(10) | RT_BIT_64(11))
#define ARMV8_ID_AA64ISAR1_EL1_API_SHIFT                        8
/** Address Authentication using the QARMA5 algorithm is not implemented. */
# define ARMV8_ID_AA64ISAR1_EL1_API_NOT_IMPL                    0
/** Address Authentication using the QARMA5 algorithm is implemented (FEAT_PAuth, FEAT_PACIMP). */
# define ARMV8_ID_AA64ISAR1_EL1_API_SUPPORTED_PAUTH             1
/** Address Authentication using the QARMA5 algorithm is implemented and enhanced PAC is supported (FEAT_EPAC, FEAT_PACIMP). */
# define ARMV8_ID_AA64ISAR1_EL1_API_SUPPORTED_EPAC              2
/** Address Authentication using the QARMA5 algorithm is implemented and enhanced PAC 2 is supported (FEAT_PAuth2, FEAT_PACIMP). */
# define ARMV8_ID_AA64ISAR1_EL1_API_SUPPORTED_PAUTH2            3
/** Address Authentication using the QARMA5 algorithm is implemented and enhanced PAC 2 and FPAC are supported (FEAT_FPAC, FEAT_PACIMP). */
# define ARMV8_ID_AA64ISAR1_EL1_API_SUPPORTED_FPAC              4
/** Address Authentication using the QARMA5 algorithm is implemented and enhanced PAC 2 and combined FPAC are supported (FEAT_FPACCOMBINE, FEAT_PACIMP). */
# define ARMV8_ID_AA64ISAR1_EL1_API_SUPPORTED_FPACCOMBINE       5
/** Bit 12 - 15 - Indicates support for JavaScript conversion from double precision floating values to integers in AArch64 state. */
#define ARMV8_ID_AA64ISAR1_EL1_FJCVTZS_MASK                     (RT_BIT_64(12) | RT_BIT_64(13) | RT_BIT_64(14) | RT_BIT_64(15))
#define ARMV8_ID_AA64ISAR1_EL1_FJCVTZS_SHIFT                    12
/** No FJCVTZS instruction implemented. */
# define ARMV8_ID_AA64ISAR1_EL1_FJCVTZS_NOT_IMPL                0
/** FJCVTZS instruction implemented (FEAT_JSCVT). */
# define ARMV8_ID_AA64ISAR1_EL1_FJCVTZS_SUPPORTED               1
/** Bit 16 - 19 - Indicates support for CRC32 instructions in AArch64 state. */
#define ARMV8_ID_AA64ISAR1_EL1_FCMA_MASK                        (RT_BIT_64(16) | RT_BIT_64(17) | RT_BIT_64(18) | RT_BIT_64(19))
#define ARMV8_ID_AA64ISAR1_EL1_FCMA_SHIFT                       16
/** No FCMLA and FCADD instructions implemented. */
# define ARMV8_ID_AA64ISAR1_EL1_FCMA_NOT_IMPL                   0
/** FCMLA and FCADD instructions implemented (FEAT_FCMA). */
# define ARMV8_ID_AA64ISAR1_EL1_FCMA_SUPPORTED                  1
/** Bit 20 - 23 - Indicates support for weaker release consistency, RCpc, based model. */
#define ARMV8_ID_AA64ISAR1_EL1_LRCPC_MASK                       (RT_BIT_64(20) | RT_BIT_64(21) | RT_BIT_64(22) | RT_BIT_64(23))
#define ARMV8_ID_AA64ISAR1_EL1_LRCPC_SHIFT                      20
/** No RCpc instructions implemented. */
# define ARMV8_ID_AA64ISAR1_EL1_LRCPC_NOT_IMPL                  0
/** The no offset LDAPR, LDAPRB and LDAPRH instructions are implemented (FEAT_LRCPC). */
# define ARMV8_ID_AA64ISAR1_EL1_LRCPC_SUPPORTED                 1
/** The no offset LDAPR, LDAPRB, LDAPRH, LDAPR and STLR instructions are implemented (FEAT_LRCPC2). */
# define ARMV8_ID_AA64ISAR1_EL1_LRCPC_SUPPORTED_2               2
/** Bit 24 - 27 - Indicates whether the QARMA5 algorithm is implemented in the PE for generic code authentication in AArch64 state. */
#define ARMV8_ID_AA64ISAR1_EL1_GPA_MASK                         (RT_BIT_64(24) | RT_BIT_64(25) | RT_BIT_64(26) | RT_BIT_64(27))
#define ARMV8_ID_AA64ISAR1_EL1_GPA_SHIFT                        24
/** Generic Authentication using the QARMA5 algorithm is not implemented. */
# define ARMV8_ID_AA64ISAR1_EL1_GPA_NOT_IMPL                    0
/** Generic Authentication using the QARMA5 algorithm is implemented (FEAT_PACQARMA5). */
# define ARMV8_ID_AA64ISAR1_EL1_GPA_SUPPORTED                   1
/** Bit 28 - 31 - Indicates whether an implementation defined algorithm is implemented in the PE for generic code authentication in AArch64 state. */
#define ARMV8_ID_AA64ISAR1_EL1_GPI_MASK                         (RT_BIT_64(28) | RT_BIT_64(29) | RT_BIT_64(30) | RT_BIT_64(31))
#define ARMV8_ID_AA64ISAR1_EL1_GPI_SHIFT                        28
/** Generic Authentication using an implementation defined algorithm is not implemented. */
# define ARMV8_ID_AA64ISAR1_EL1_GPI_NOT_IMPL                    0
/** Generic Authentication using an implementation defined algorithm is implemented (FEAT_PACIMP). */
# define ARMV8_ID_AA64ISAR1_EL1_GPI_SUPPORTED                   1
/** Bit 32 - 35 - Indicates support for SHA3 instructions in AArch64 state. */
#define ARMV8_ID_AA64ISAR1_EL1_FRINTTS_MASK                     (RT_BIT_64(32) | RT_BIT_64(33) | RT_BIT_64(34) | RT_BIT_64(35))
#define ARMV8_ID_AA64ISAR1_EL1_FRINTTS_SHIFT                    32
/** FRINT32Z, FRINT32X, FRINT64Z and FRINT64X instructions are not implemented. */
# define ARMV8_ID_AA64ISAR1_EL1_FRINTTS_NOT_IMPL                0
/** FRINT32Z, FRINT32X, FRINT64Z and FRINT64X instructions are implemented (FEAT_FRINTTS). */
# define ARMV8_ID_AA64ISAR1_EL1_FRINTTS_SUPPORTED               1
/** Bit 36 - 39 - Indicates support for SB instructions in AArch64 state. */
#define ARMV8_ID_AA64ISAR1_EL1_SB_MASK                          (RT_BIT_64(36) | RT_BIT_64(37) | RT_BIT_64(38) | RT_BIT_64(39))
#define ARMV8_ID_AA64ISAR1_EL1_SB_SHIFT                         36
/** No SB instructions implemented. */
# define ARMV8_ID_AA64ISAR1_EL1_SB_NOT_IMPL                     0
/** SB instructions implemented (FEAT_SB). */
# define ARMV8_ID_AA64ISAR1_EL1_SB_SUPPORTED                    1
/** Bit 40 - 43 - Indicates support for prediction invalidation instructions in AArch64 state. */
#define ARMV8_ID_AA64ISAR1_EL1_SPECRES_MASK                     (RT_BIT_64(40) | RT_BIT_64(41) | RT_BIT_64(42) | RT_BIT_64(43))
#define ARMV8_ID_AA64ISAR1_EL1_SPECRES_SHIFT                    40
/** Prediction invalidation instructions are not implemented. */
# define ARMV8_ID_AA64ISAR1_EL1_SPECRES_NOT_IMPL                0
/** Prediction invalidation instructions are implemented (FEAT_SPECRES). */
# define ARMV8_ID_AA64ISAR1_EL1_SPECRES_SUPPORTED               1
/** Bit 44 - 47 - Indicates support for Advanced SIMD and Floating-point BFloat16 instructions in AArch64 state. */
#define ARMV8_ID_AA64ISAR1_EL1_BF16_MASK                        (RT_BIT_64(44) | RT_BIT_64(45) | RT_BIT_64(46) | RT_BIT_64(47))
#define ARMV8_ID_AA64ISAR1_EL1_BF16_SHIFT                       44
/** BFloat16 instructions are not implemented. */
# define ARMV8_ID_AA64ISAR1_EL1_BF16_NOT_IMPL                   0
/** BFCVT, BFCVTN, BFCVTN2, BFDOT, BFMLALB, BFMLALT and BFMMLA instructions are implemented (FEAT_BF16). */
# define ARMV8_ID_AA64ISAR1_EL1_BF16_SUPPORTED_BF16             1
/** BFCVT, BFCVTN, BFCVTN2, BFDOT, BFMLALB, BFMLALT and BFMMLA instructions are implemented and FPCR.EBF is supported (FEAT_EBF16). */
# define ARMV8_ID_AA64ISAR1_EL1_BF16_SUPPORTED_EBF16            2
/** Bit 48 - 51 - Indicates support for Data Gathering Hint instructions. */
#define ARMV8_ID_AA64ISAR1_EL1_DGH_MASK                         (RT_BIT_64(48) | RT_BIT_64(49) | RT_BIT_64(50) | RT_BIT_64(51))
#define ARMV8_ID_AA64ISAR1_EL1_DGH_SHIFT                        48
/** Data Gathering Hint instructions are not implemented. */
# define ARMV8_ID_AA64ISAR1_EL1_DGH_NOT_IMPL                    0
/** Data Gathering Hint instructions are implemented (FEAT_DGH). */
# define ARMV8_ID_AA64ISAR1_EL1_DGH_SUPPORTED                   1
/** Bit 52 - 55 - Indicates support for Advanced SIMD and Floating-point Int8 matri multiplication instructions. */
#define ARMV8_ID_AA64ISAR1_EL1_I8MM_MASK                        (RT_BIT_64(52) | RT_BIT_64(53) | RT_BIT_64(54) | RT_BIT_64(55))
#define ARMV8_ID_AA64ISAR1_EL1_I8MM_SHIFT                       52
/** No Int8 matrix multiplication instructions implemented. */
# define ARMV8_ID_AA64ISAR1_EL1_I8MM_NOT_IMPL                   0
/** SMMLA, SUDOT, UMMLA, USMMLA and USDOT instrutions are implemented (FEAT_I8MM). */
# define ARMV8_ID_AA64ISAR1_EL1_I8MM_SUPPORTED                  1
/** Bit 56 - 59 - Indicates support for the XS attribute, the TLBI and DSB insturctions with the nXS qualifier in AArch64 state. */
#define ARMV8_ID_AA64ISAR1_EL1_XS_MASK                          (RT_BIT_64(56) | RT_BIT_64(57) | RT_BIT_64(58) | RT_BIT_64(59))
#define ARMV8_ID_AA64ISAR1_EL1_XS_SHIFT                         56
/** The XS attribute and the TLBI and DSB instructions with the nXS qualifier are not supported. */
# define ARMV8_ID_AA64ISAR1_EL1_XS_NOT_IMPL                     0
/** The XS attribute and the TLBI and DSB instructions with the nXS qualifier are supported (FEAT_XS). */
# define ARMV8_ID_AA64ISAR1_EL1_XS_SUPPORTED                    1
/** Bit 60 - 63 - Indicates support LD64B and ST64B* instructons and the ACCDATA_EL1 register. */
#define ARMV8_ID_AA64ISAR1_EL1_LS64_MASK                        (RT_BIT_64(60) | RT_BIT_64(61) | RT_BIT_64(62) | RT_BIT_64(63))
#define ARMV8_ID_AA64ISAR1_EL1_LS64_SHIFT                       60
/** The LD64B, ST64B, ST64BV and ST64BV0 instructions, the ACCDATA_EL1 register and associated traps are not supported. */
# define ARMV8_ID_AA64ISAR1_EL1_LS64_NOT_IMPL                   0
/** The LD64B and ST64B instructions are supported (FEAT_LS64). */
# define ARMV8_ID_AA64ISAR1_EL1_LS64_SUPPORTED                  1
/** The LD64B, ST64B, ST64BV and associated traps are not supported (FEAT_LS64_V). */
# define ARMV8_ID_AA64ISAR1_EL1_LS64_SUPPORTED_V                2
/** The LD64B, ST64B, ST64BV and ST64BV0 instructions, the ACCDATA_EL1 register and associated traps are supported (FEAT_LS64_ACCDATA). */
# define ARMV8_ID_AA64ISAR1_EL1_LS64_SUPPORTED_ACCDATA          3
/** @} */


/** @name ID_AA64ISAR2_EL1 - AArch64 Instruction Set Attribute Register 0.
 * @{ */
/** Bit 0 - 3 - Indicates support for WFET and WFIT instructions in AArch64 state. */
#define ARMV8_ID_AA64ISAR2_EL1_WFXT_MASK                        (RT_BIT_64(0) | RT_BIT_64(1) | RT_BIT_64(2) | RT_BIT_64(3))
#define ARMV8_ID_AA64ISAR2_EL1_WFXT_SHIFT                       0
/** WFET and WFIT are not supported. */
# define ARMV8_ID_AA64ISAR2_EL1_WFXT_NOT_IMPL                   0
/** WFET and WFIT are supported (FEAT_WFxT). */
# define ARMV8_ID_AA64ISAR2_EL1_WFXT_SUPPORTED                  2
/** Bit 4 - 7 - Indicates support for 12 bits of mantissa in reciprocal and reciprocal square root instructions in AArch64 state, when FPCR.AH is 1. */
#define ARMV8_ID_AA64ISAR2_EL1_RPRES_MASK                       (RT_BIT_64(4) | RT_BIT_64(5) | RT_BIT_64(6) | RT_BIT_64(7))
#define ARMV8_ID_AA64ISAR2_EL1_RPRES_SHIFT                      4
/** Reciprocal and reciprocal square root estimates give 8 bits of mantissa when FPCR.AH is 1. */
# define ARMV8_ID_AA64ISAR2_EL1_RPRES_NOT_IMPL                  0
/** Reciprocal and reciprocal square root estimates give 12 bits of mantissa when FPCR.AH is 1 (FEAT_RPRES). */
# define ARMV8_ID_AA64ISAR2_EL1_RPRES_SUPPORTED                 1
/** Bit 8 - 11 - Indicates whether the QARMA3 algorithm is implemented in the PE for generic code authentication in AArch64 state. */
#define ARMV8_ID_AA64ISAR2_EL1_GPA3_MASK                        (RT_BIT_64(8) | RT_BIT_64(9) | RT_BIT_64(10) | RT_BIT_64(11))
#define ARMV8_ID_AA64ISAR2_EL1_GPA3_SHIFT                       8
/** Generic Authentication using the QARMA3 algorithm is not implemented. */
# define ARMV8_ID_AA64ISAR2_EL1_GPA3_NOT_IMPL                   0
/** Generic Authentication using the QARMA3 algorithm is implemented (FEAT_PACQARMA3). */
# define ARMV8_ID_AA64ISAR2_EL1_GPA3_SUPPORTED                  1
/** Bit 12 - 15 - Indicates whether QARMA3 algorithm is implemented in the PE for address authentication. */
#define ARMV8_ID_AA64ISAR2_EL1_APA3_MASK                        (RT_BIT_64(12) | RT_BIT_64(13) | RT_BIT_64(14) | RT_BIT_64(15))
#define ARMV8_ID_AA64ISAR2_EL1_APA3_SHIFT                       12
/** Address Authentication using the QARMA3 algorithm is not implemented. */
# define ARMV8_ID_AA64ISAR2_EL1_APA3_NOT_IMPL                   0
/** Address Authentication using the QARMA5 algorithm is implemented (FEAT_PAuth, FEAT_PACQARMA3). */
# define ARMV8_ID_AA64ISAR2_EL1_APA3_SUPPORTED_PAUTH            1
/** Address Authentication using the QARMA5 algorithm is implemented and enhanced PAC is supported (FEAT_EPAC, FEAT_PACQARMA3). */
# define ARMV8_ID_AA64ISAR2_EL1_APA3_SUPPORTED_EPAC             2
/** Address Authentication using the QARMA5 algorithm is implemented and enhanced PAC 2 is supported (FEAT_PAuth2, FEAT_PACQARMA3). */
# define ARMV8_ID_AA64ISAR2_EL1_APA3_SUPPORTED_PAUTH2           3
/** Address Authentication using the QARMA5 algorithm is implemented and enhanced PAC 2 and FPAC are supported (FEAT_FPAC, FEAT_PACQARMA3). */
# define ARMV8_ID_AA64ISAR2_EL1_APA3_SUPPORTED_FPAC             4
/** Address Authentication using the QARMA5 algorithm is implemented and enhanced PAC 2 and combined FPAC are supported (FEAT_FPACCOMBINE, FEAT_PACQARMA3). */
# define ARMV8_ID_AA64ISAR2_EL1_APA3_SUPPORTED_FPACCOMBINE      5
/** Bit 16 - 19 - Indicates support for Memory Copy and Memory Set instructions in AArch64 state. */
#define ARMV8_ID_AA64ISAR2_EL1_MOPS_MASK                        (RT_BIT_64(16) | RT_BIT_64(17) | RT_BIT_64(18) | RT_BIT_64(19))
#define ARMV8_ID_AA64ISAR2_EL1_MOPS_SHIFT                       16
/** No Memory Copy and Memory Set instructions implemented. */
# define ARMV8_ID_AA64ISAR2_EL1_MOPS_NOT_IMPL                   0
/** Memory Copy and Memory Set instructions implemented (FEAT_MOPS). */
# define ARMV8_ID_AA64ISAR2_EL1_MOPS_SUPPORTED                  1
/** Bit 20 - 23 - Indicates support for weaker release consistency, RCpc, based model. */
#define ARMV8_ID_AA64ISAR2_EL1_BC_MASK                          (RT_BIT_64(20) | RT_BIT_64(21) | RT_BIT_64(22) | RT_BIT_64(23))
#define ARMV8_ID_AA64ISAR2_EL1_BC_SHIFT                         20
/** BC instruction is not implemented. */
# define ARMV8_ID_AA64ISAR2_EL1_BC_NOT_IMPL                     0
/** BC instruction is implemented (FEAT_HBC). */
# define ARMV8_ID_AA64ISAR2_EL1_BC_SUPPORTED                    1
/** Bit 24 - 27 - Indicates whether the ConstPACField() functions used as part of PAC additions returns FALSE or TRUE. */
#define ARMV8_ID_AA64ISAR2_EL1_PACFRAC_MASK                     (RT_BIT_64(24) | RT_BIT_64(25) | RT_BIT_64(26) | RT_BIT_64(27))
#define ARMV8_ID_AA64ISAR2_EL1_PACFRAC_SHIFT                    24
/** ConstPACField() returns FALSE. */
# define ARMV8_ID_AA64ISAR2_EL1_PACFRAC_FALSE                   0
/** ConstPACField() returns TRUE (FEAT_CONSTPACFIELD). */
# define ARMV8_ID_AA64ISAR2_EL1_PACFRAC_TRUE                    1
/* Bit 28 - 63 - Reserved. */
/** @} */


/** @name ID_AA64PFR0_EL1 - AArch64 Processor Feature Register 0.
 * @{ */
/** Bit 0 - 3 - EL0 Exception level handling. */
#define ARMV8_ID_AA64PFR0_EL1_EL0_MASK                          (RT_BIT_64(0) | RT_BIT_64(1) | RT_BIT_64(2) | RT_BIT_64(3))
#define ARMV8_ID_AA64PFR0_EL1_EL0_SHIFT                         0
/** EL0 can be executed in AArch64 state only. */
# define ARMV8_ID_AA64PFR0_EL1_EL0_AARCH64_ONLY                 1
/** EL0 can be executed in AArch64 and AArch32 state. */
# define ARMV8_ID_AA64PFR0_EL1_EL0_AARCH64_AARCH32              2
/** Bit 4 - 7 - EL1 Exception level handling. */
#define ARMV8_ID_AA64PFR0_EL1_EL1_MASK                          (RT_BIT_64(4) | RT_BIT_64(5) | RT_BIT_64(6) | RT_BIT_64(7))
#define ARMV8_ID_AA64PFR0_EL1_EL1_SHIFT                         4
/** EL1 can be executed in AArch64 state only. */
# define ARMV8_ID_AA64PFR0_EL1_EL1_AARCH64_ONLY                 1
/** EL1 can be executed in AArch64 and AArch32 state. */
# define ARMV8_ID_AA64PFR0_EL1_EL1_AARCH64_AARCH32              2
/** Bit 8 - 11 - EL2 Exception level handling. */
#define ARMV8_ID_AA64PFR0_EL1_EL2_MASK                          (RT_BIT_64(8) | RT_BIT_64(9) | RT_BIT_64(10) | RT_BIT_64(11))
#define ARMV8_ID_AA64PFR0_EL1_EL2_SHIFT                         8
/** EL2 is not implemented. */
# define ARMV8_ID_AA64PFR0_EL1_EL2_NOT_IMPL                     0
/** EL2 can be executed in AArch64 state only. */
# define ARMV8_ID_AA64PFR0_EL1_EL2_AARCH64_ONLY                 1
/** EL2 can be executed in AArch64 and AArch32 state. */
# define ARMV8_ID_AA64PFR0_EL1_EL2_AARCH64_AARCH32              2
/** Bit 12 - 15 - EL3 Exception level handling. */
#define ARMV8_ID_AA64PFR0_EL1_EL3_MASK                          (RT_BIT_64(12) | RT_BIT_64(13) | RT_BIT_64(14) | RT_BIT_64(15))
#define ARMV8_ID_AA64PFR0_EL1_EL3_SHIFT                         12
/** EL3 is not implemented. */
# define ARMV8_ID_AA64PFR0_EL1_EL3_NOT_IMPL                     0
/** EL3 can be executed in AArch64 state only. */
# define ARMV8_ID_AA64PFR0_EL1_EL3_AARCH64_ONLY                 1
/** EL3 can be executed in AArch64 and AArch32 state. */
# define ARMV8_ID_AA64PFR0_EL1_EL3_AARCH64_AARCH32              2
/** Bit 16 - 19 - Floating-point support. */
#define ARMV8_ID_AA64PFR0_EL1_FP_MASK                           (RT_BIT_64(16) | RT_BIT_64(17) | RT_BIT_64(18) | RT_BIT_64(19))
#define ARMV8_ID_AA64PFR0_EL1_FP_SHIFT                          16
/** Floating-point is implemented and support single and double precision. */
# define ARMV8_ID_AA64PFR0_EL1_FP_IMPL_SP_DP                    0
/** Floating-point is implemented and support single, double and half precision. */
# define ARMV8_ID_AA64PFR0_EL1_FP_IMPL_SP_DP_HP                 1
/** Floating-point is not implemented. */
# define ARMV8_ID_AA64PFR0_EL1_FP_NOT_IMPL                      0xf
/** Bit 20 - 23 - Advanced SIMD support. */
#define ARMV8_ID_AA64PFR0_EL1_ADVSIMD_MASK                      (RT_BIT_64(20) | RT_BIT_64(21) | RT_BIT_64(22) | RT_BIT_64(23))
#define ARMV8_ID_AA64PFR0_EL1_ADVSIMD_SHIFT                     20
/** Advanced SIMD is implemented and support single and double precision. */
# define ARMV8_ID_AA64PFR0_EL1_ADVSIMD_IMPL_SP_DP               0
/** Advanced SIMD is implemented and support single, double and half precision. */
# define ARMV8_ID_AA64PFR0_EL1_ADVSIMD_IMPL_SP_DP_HP            1
/** Advanced SIMD is not implemented. */
# define ARMV8_ID_AA64PFR0_EL1_ADVSIMD_NOT_IMPL                 0xf
/** Bit 24 - 27 - System register GIC CPU interface support. */
#define ARMV8_ID_AA64PFR0_EL1_GIC_MASK                          (RT_BIT_64(24) | RT_BIT_64(25) | RT_BIT_64(26) | RT_BIT_64(27))
#define ARMV8_ID_AA64PFR0_EL1_GIC_SHIFT                         24
/** GIC CPU interface system registers are not implemented. */
# define ARMV8_ID_AA64PFR0_EL1_GIC_NOT_IMPL                     0
/** System register interface to versions 3.0 and 4.0 of the GIC CPU interface is supported. */
# define ARMV8_ID_AA64PFR0_EL1_GIC_V3_V4                        1
/** System register interface to version 4.1 of the GIC CPU interface is supported. */
# define ARMV8_ID_AA64PFR0_EL1_GIC_V4_1                         3
/** Bit 28 - 31 - RAS Extension version. */
#define ARMV8_ID_AA64PFR0_EL1_RAS_MASK                          (RT_BIT_64(28) | RT_BIT_64(29) | RT_BIT_64(30) | RT_BIT_64(31))
#define ARMV8_ID_AA64PFR0_EL1_RAS_SHIFT                         28
/** No RAS extension. */
# define ARMV8_ID_AA64PFR0_EL1_RAS_NOT_IMPL                     0
/** RAS Extension implemented. */
# define ARMV8_ID_AA64PFR0_EL1_RAS_SUPPORTED                    1
/** FEAT_RASv1p1 implemented. */
# define ARMV8_ID_AA64PFR0_EL1_RAS_V1P1                         2
/** Bit 32 - 35 - Scalable Vector Extension (SVE) support. */
#define ARMV8_ID_AA64PFR0_EL1_SVE_MASK                          (RT_BIT_64(32) | RT_BIT_64(33) | RT_BIT_64(34) | RT_BIT_64(35))
#define ARMV8_ID_AA64PFR0_EL1_SVE_SHIFT                         32
/** SVE is not supported. */
# define ARMV8_ID_AA64PFR0_EL1_SVE_NOT_IMPL                     0
/** SVE is supported. */
# define ARMV8_ID_AA64PFR0_EL1_SVE_SUPPORTED                    1
/** Bit 36 - 39 - Secure EL2 support. */
#define ARMV8_ID_AA64PFR0_EL1_SEL2_MASK                         (RT_BIT_64(36) | RT_BIT_64(37) | RT_BIT_64(38) | RT_BIT_64(39))
#define ARMV8_ID_AA64PFR0_EL1_SEL2_SHIFT                        36
/** Secure EL2 is not supported. */
# define ARMV8_ID_AA64PFR0_EL1_SEL2_NOT_IMPL                    0
/** Secure EL2 is implemented. */
# define ARMV8_ID_AA64PFR0_EL1_SEL2_SUPPORTED                   1
/** Bit 40 - 43 - MPAM support. */
#define ARMV8_ID_AA64PFR0_EL1_MPAM_MASK                         (RT_BIT_64(40) | RT_BIT_64(41) | RT_BIT_64(42) | RT_BIT_64(43))
#define ARMV8_ID_AA64PFR0_EL1_MPAM_SHIFT                        40
/** MPAM extension major version number is 0. */
# define ARMV8_ID_AA64PFR0_EL1_MPAM_MAJOR_V0                    0
/** MPAM extension major version number is 1. */
# define ARMV8_ID_AA64PFR0_EL1_MPAM_MAJOR_V1                    1
/** Bit 44 - 47 - Activity Monitor Extension support. */
#define ARMV8_ID_AA64PFR0_EL1_AMU_MASK                          (RT_BIT_64(44) | RT_BIT_64(45) | RT_BIT_64(46) | RT_BIT_64(47))
#define ARMV8_ID_AA64PFR0_EL1_AMU_SHIFT                         44
/** Activity Monitor extension is not implemented. */
# define ARMV8_ID_AA64PFR0_EL1_AMU_NOT_IMPL                     0
/** Activity Monitor extension is implemented as of FEAT_AMUv1. */
# define ARMV8_ID_AA64PFR0_EL1_AMU_V1                           1
/** Activity Monitor extension is implemented as of FEAT_AMUv1p1 including virtualization support. */
# define ARMV8_ID_AA64PFR0_EL1_AMU_V1P1                         2
/** Bit 48 - 51 - Data Independent Timing support. */
#define ARMV8_ID_AA64PFR0_EL1_DIT_MASK                          (RT_BIT_64(48) | RT_BIT_64(49) | RT_BIT_64(50) | RT_BIT_64(51))
#define ARMV8_ID_AA64PFR0_EL1_DIT_SHIFT                         48
/** AArch64 does not guarantee constant execution time of any instructions. */
# define ARMV8_ID_AA64PFR0_EL1_DIT_NOT_IMPL                     0
/** AArch64 provides the PSTATE.DIT mechanism to guarantee constant execution time of certain instructions (FEAT_DIT). */
# define ARMV8_ID_AA64PFR0_EL1_DIT_SUPPORTED                    1
/** Bit 52 - 55 - Realm Management Extension support. */
#define ARMV8_ID_AA64PFR0_EL1_RME_MASK                          (RT_BIT_64(52) | RT_BIT_64(53) | RT_BIT_64(54) | RT_BIT_64(55))
#define ARMV8_ID_AA64PFR0_EL1_RME_SHIFT                         52
/** Realm Management Extension not implemented. */
# define ARMV8_ID_AA64PFR0_EL1_RME_NOT_IMPL                     0
/** RMEv1 is implemented (FEAT_RME). */
# define ARMV8_ID_AA64PFR0_EL1_RME_SUPPORTED                    1
/** Bit 56 - 59 - Speculative use out of context branch targets support. */
#define ARMV8_ID_AA64PFR0_EL1_CSV2_MASK                         (RT_BIT_64(56) | RT_BIT_64(57) | RT_BIT_64(58) | RT_BIT_64(59))
#define ARMV8_ID_AA64PFR0_EL1_CSV2_SHIFT                        56
/** Implementation does not disclose whether FEAT_CSV2 is implemented. */
# define ARMV8_ID_AA64PFR0_EL1_CSV2_NOT_EXPOSED                 0
/** FEAT_CSV2 is implemented. */
# define ARMV8_ID_AA64PFR0_EL1_CSV2_SUPPORTED                   1
/** FEAT_CSV2_2 is implemented. */
# define ARMV8_ID_AA64PFR0_EL1_CSV2_2_SUPPORTED                 2
/** FEAT_CSV2_3 is implemented. */
# define ARMV8_ID_AA64PFR0_EL1_CSV2_3_SUPPORTED                 3
/** Bit 60 - 63 - Speculative use of faulting data support. */
#define ARMV8_ID_AA64PFR0_EL1_CSV3_MASK                         (RT_BIT_64(60) | RT_BIT_64(61) | RT_BIT_64(62) | RT_BIT_64(63))
#define ARMV8_ID_AA64PFR0_EL1_CSV3_SHIFT                        60
/** Implementation does not disclose whether data loaded under speculation with a permission or domain fault can be used. */
# define ARMV8_ID_AA64PFR0_EL1_CSV3_NOT_EXPOSED                 0
/** FEAT_CSV3 is supported . */
# define ARMV8_ID_AA64PFR0_EL1_CSV3_SUPPORTED                   1
/** @} */


/** @name ID_AA64PFR1_EL1 - AArch64 Processor Feature Register 1.
 * @{ */
/** Bit 0 - 3 - Branch Target Identification support. */
#define ARMV8_ID_AA64PFR1_EL1_BT_MASK                           (RT_BIT_64(0) | RT_BIT_64(1) | RT_BIT_64(2) | RT_BIT_64(3))
#define ARMV8_ID_AA64PFR1_EL1_BT_SHIFT                          0
/** The Branch Target Identification mechanism is not implemented. */
# define ARMV8_ID_AA64PFR1_EL1_BT_NOT_IMPL                      0
/** The Branch Target Identifcation mechanism is implemented. */
# define ARMV8_ID_AA64PFR1_EL1_BT_SUPPORTED                     1
/** Bit 4 - 7 - Speculative Store Bypassing control support. */
#define ARMV8_ID_AA64PFR1_EL1_SSBS_MASK                         (RT_BIT_64(4) | RT_BIT_64(5) | RT_BIT_64(6) | RT_BIT_64(7))
#define ARMV8_ID_AA64PFR1_EL1_SSBS_SHIFT                        4
/** AArch64 provides no mechanism to control the use of Speculative Store Bypassing. */
# define ARMV8_ID_AA64PFR1_EL1_SSBS_NOT_IMPL                    0
/** AArch64 provides the PSTATE.SSBS mechanism to mark regions that are Speculative Store Bypass Safe. */
# define ARMV8_ID_AA64PFR1_EL1_SSBS_SUPPORTED                   1
/** AArch64 provides the PSTATE.SSBS mechanism to mark regions that are Speculative Store Bypass Safe and adds MSR and MRS instructions
 * to directly read and write the PSTATE.SSBS field. */
# define ARMV8_ID_AA64PFR1_EL1_SSBS_SUPPORTED_MSR_MRS           2
/** Bit 8 - 11 - Memory Tagging Extension support. */
#define ARMV8_ID_AA64PFR1_EL1_MTE_MASK                          (RT_BIT_64(8) | RT_BIT_64(9) | RT_BIT_64(10) | RT_BIT_64(11))
#define ARMV8_ID_AA64PFR1_EL1_MTE_SHIFT                         8
/** MTE is not implemented. */
# define ARMV8_ID_AA64PFR1_EL1_MTE_NOT_IMPL                     0
/** Instruction only Memory Tagging Extensions implemented. */
# define ARMV8_ID_AA64PFR1_EL1_MTE_INSN_ONLY                    1
/** Full Memory Tagging Extension implemented. */
# define ARMV8_ID_AA64PFR1_EL1_MTE_FULL                         2
/** Full Memory Tagging Extension with asymmetric Tag Check Fault handling implemented. */
# define ARMV8_ID_AA64PFR1_EL1_MTE_FULL_ASYM_TAG_FAULT_CHK      3
/** Bit 12 - 15 - RAS Extension fractional field. */
#define ARMV8_ID_AA64PFR1_EL1_RASFRAC_MASK                      (RT_BIT_64(12) | RT_BIT_64(13) | RT_BIT_64(14) | RT_BIT_64(15))
#define ARMV8_ID_AA64PFR1_EL1_RASFRAC_SHIFT                     12
/** RAS Extension is implemented. */
# define ARMV8_ID_AA64PFR1_EL1_RASFRAC_IMPL                     0
/** FEAT_RASv1p1 is implemented. */
# define ARMV8_ID_AA64PFR1_EL1_RASFRAC_RASV1P1                  1
/** Bit 16 - 19 - MPAM minor version number. */
#define ARMV8_ID_AA64PFR1_EL1_MPAMFRAC_MASK                     (RT_BIT_64(16) | RT_BIT_64(17) | RT_BIT_64(18) | RT_BIT_64(19))
#define ARMV8_ID_AA64PFR1_EL1_MPAMFRAC_SHIFT                    16
/** The minor version of number of the MPAM extension is 0. */
# define ARMV8_ID_AA64PFR1_EL1_MPAMFRAC_0                       0
/** The minor version of number of the MPAM extension is 1. */
# define ARMV8_ID_AA64PFR1_EL1_MPAMFRAC_1                       1
/* Bit 20 - 23 - Reserved. */
/** Bit 24 - 27 - Scalable Matrix Extension support. */
#define ARMV8_ID_AA64PFR1_EL1_SME_MASK                          (RT_BIT_64(24) | RT_BIT_64(25) | RT_BIT_64(26) | RT_BIT_64(27))
#define ARMV8_ID_AA64PFR1_EL1_SME_SHIFT                         24
/** Scalable Matrix Extensions are not implemented. */
# define ARMV8_ID_AA64PFR1_EL1_SME_NOT_IMPL                     0
/** Scalable Matrix Extensions are implemented (FEAT_SME). */
# define ARMV8_ID_AA64PFR1_EL1_SME_SUPPORTED                    1
/** Scalable Matrix Extensions are implemented + SME2 ZT0 register(FEAT_SME2). */
# define ARMV8_ID_AA64PFR1_EL1_SME_SME2                         2
/** Bit 28 - 31 - Random Number trap to EL3 support. */
#define ARMV8_ID_AA64PFR1_EL1_RNDRTRAP_MASK                     (RT_BIT_64(28) | RT_BIT_64(29) | RT_BIT_64(30) | RT_BIT_64(31))
#define ARMV8_ID_AA64PFR1_EL1_RNDRTRAP_SHIFT                    28
/** Trapping of RNDR and RNDRRS to EL3 is not supported. */
# define ARMV8_ID_AA64PFR1_EL1_RNDRTRAP_NOT_IMPL                0
/** Trapping of RNDR and RDNRRS to EL3 is supported. */
# define ARMV8_ID_AA64PFR1_EL1_RNDRTRAP_SUPPORTED               1
/** Bit 32 - 35 - CSV2 fractional field. */
#define ARMV8_ID_AA64PFR1_EL1_CSV2FRAC_MASK                     (RT_BIT_64(32) | RT_BIT_64(33) | RT_BIT_64(34) | RT_BIT_64(35))
#define ARMV8_ID_AA64PFR1_EL1_CSV2FRAC_SHIFT                    32
/** Either CSV2 not exposed or implementation does not expose whether FEAT_CSV2_1p1 is implemented. */
# define ARMV8_ID_AA64PFR1_EL1_CSV2FRAC_NOT_EXPOSED             0
/** FEAT_CSV2_1p1 is implemented. */
# define ARMV8_ID_AA64PFR1_EL1_CSV2FRAC_1P1                     1
/** FEAT_CSV2_1p2 is implemented. */
# define ARMV8_ID_AA64PFR1_EL1_CSV2FRAC_1P2                     2
/** Bit 36 - 39 - Non-maskable Interrupt support. */
#define ARMV8_ID_AA64PFR1_EL1_NMI_MASK                          (RT_BIT_64(36) | RT_BIT_64(37) | RT_BIT_64(38) | RT_BIT_64(39))
#define ARMV8_ID_AA64PFR1_EL1_NMI_SHIFT                         36
/** SCTLR_ELx.{SPINTMASK, NMI} and PSTATE.ALLINT and associated instructions are not supported. */
# define ARMV8_ID_AA64PFR1_EL1_NMI_NOT_IMPL                     0
/** SCTLR_ELx.{SPINTMASK, NMI} and PSTATE.ALLINT and associated instructions are supported (FEAT_NMI). */
# define ARMV8_ID_AA64PFR1_EL1_NMI_SUPPORTED                    1
/** @} */


/** @name ID_AA64MMFR0_EL1 - AArch64 Memory Model Feature Register 0.
 * @{ */
/** Bit 0 - 3 - Physical Address range supported. */
#define ARMV8_ID_AA64MMFR0_EL1_PARANGE_MASK                     (RT_BIT_64(0) | RT_BIT_64(1) | RT_BIT_64(2) | RT_BIT_64(3))
#define ARMV8_ID_AA64MMFR0_EL1_PARANGE_SHIFT                    0
/** Physical Address range is 32 bits, 4GiB. */
# define ARMV8_ID_AA64MMFR0_EL1_PARANGE_32BITS                  0
/** Physical Address range is 36 bits, 64GiB. */
# define ARMV8_ID_AA64MMFR0_EL1_PARANGE_36BITS                  1
/** Physical Address range is 40 bits, 1TiB. */
# define ARMV8_ID_AA64MMFR0_EL1_PARANGE_40BITS                  2
/** Physical Address range is 42 bits, 4TiB. */
# define ARMV8_ID_AA64MMFR0_EL1_PARANGE_42BITS                  3
/** Physical Address range is 44 bits, 16TiB. */
# define ARMV8_ID_AA64MMFR0_EL1_PARANGE_44BITS                  4
/** Physical Address range is 48 bits, 256TiB. */
# define ARMV8_ID_AA64MMFR0_EL1_PARANGE_48BITS                  5
/** Physical Address range is 52 bits, 4PiB. */
# define ARMV8_ID_AA64MMFR0_EL1_PARANGE_52BITS                  6
/** Bit 4 - 7 - Number of ASID bits. */
#define ARMV8_ID_AA64MMFR0_EL1_ASIDBITS_MASK                    (RT_BIT_64(4) | RT_BIT_64(5) | RT_BIT_64(6) | RT_BIT_64(7))
#define ARMV8_ID_AA64MMFR0_EL1_ASIDBITS_SHIFT                   4
/** ASID bits is 8. */
# define ARMV8_ID_AA64MMFR0_EL1_ASIDBITS_8                      0
/** ASID bits is 16. */
# define ARMV8_ID_AA64MMFR0_EL1_ASIDBITS_16                     2
/** Bit 8 - 11 - Indicates support for mixed-endian configuration. */
#define ARMV8_ID_AA64MMFR0_EL1_BIGEND_MASK                      (RT_BIT_64(8) | RT_BIT_64(9) | RT_BIT_64(10) | RT_BIT_64(11))
#define ARMV8_ID_AA64MMFR0_EL1_BIGEND_SHIFT                     8
/** No mixed-endian support. */
# define ARMV8_ID_AA64MMFR0_EL1_BIGEND_NOT_IMPL                 0
/** Mixed-endian supported. */
# define ARMV8_ID_AA64MMFR0_EL1_BIGEND_SUPPORTED                1
/** Bit 12 - 15 - Indicates support for a distinction between Secure and Non-secure Memory. */
#define ARMV8_ID_AA64MMFR0_EL1_SNSMEM_MASK                      (RT_BIT_64(12) | RT_BIT_64(13) | RT_BIT_64(14) | RT_BIT_64(15))
#define ARMV8_ID_AA64MMFR0_EL1_SNSMEM_SHIFT                     12
/** No distinction between Secure and Non-secure Memory supported. */
# define ARMV8_ID_AA64MMFR0_EL1_SNSMEM_NOT_IMPL                 0
/** Distinction between Secure and Non-secure Memory supported. */
# define ARMV8_ID_AA64MMFR0_EL1_SNSMEM_SUPPORTED                1
/** Bit 16 - 19 - Indicates support for mixed-endian at EL0 only. */
#define ARMV8_ID_AA64MMFR0_EL1_BIGENDEL0_MASK                   (RT_BIT_64(16) | RT_BIT_64(17) | RT_BIT_64(18) | RT_BIT_64(19))
#define ARMV8_ID_AA64MMFR0_EL1_BIGENDEL0_SHIFT                  16
/** No mixed-endian support at EL0. */
# define ARMV8_ID_AA64MMFR0_EL1_BIGENDEL0_NOT_IMPL              0
/** Mixed-endian support at EL0. */
# define ARMV8_ID_AA64MMFR0_EL1_BIGENDEL0_SUPPORTED             1
/** Bit 20 - 23 - Indicates support for 16KiB memory translation granule size. */
#define ARMV8_ID_AA64MMFR0_EL1_TGRAN16_MASK                     (RT_BIT_64(20) | RT_BIT_64(21) | RT_BIT_64(22) | RT_BIT_64(23))
#define ARMV8_ID_AA64MMFR0_EL1_TGRAN16_SHIFT                    20
/** 16KiB granule size not supported. */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN16_NOT_IMPL                0
/** 16KiB granule size is supported. */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN16_SUPPORTED               1
/** 16KiB granule size is supported and supports 52-bit input addresses and can describe 52-bit output addresses (FEAT_LPA2). */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN16_SUPPORTED_52BIT         2
/** Bit 24 - 27 - Indicates support for 64KiB memory translation granule size. */
#define ARMV8_ID_AA64MMFR0_EL1_TGRAN64_MASK                     (RT_BIT_64(24) | RT_BIT_64(25) | RT_BIT_64(26) | RT_BIT_64(27))
#define ARMV8_ID_AA64MMFR0_EL1_TGRAN64_SHIFT                    24
/** 64KiB granule supported. */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN64_SUPPORTED               0
/** 64KiB granule not supported. */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN64_NOT_IMPL                0xf
/** Bit 28 - 31 - Indicates support for 4KiB memory translation granule size. */
#define ARMV8_ID_AA64MMFR0_EL1_TGRAN4_MASK                      (RT_BIT_64(28) | RT_BIT_64(29) | RT_BIT_64(30) | RT_BIT_64(31))
#define ARMV8_ID_AA64MMFR0_EL1_TGRAN4_SHIFT                     28
/** 4KiB granule supported. */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN4_SUPPORTED                0
/** 4KiB granule size is supported and supports 52-bit input addresses and can describe 52-bit output addresses (FEAT_LPA2). */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN4_SUPPORTED_52BIT          1
/** 4KiB granule not supported. */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN4_NOT_IMPL                 0xf
/** Bit 32 - 35 - Indicates support for 16KiB granule size at stage 2. */
#define ARMV8_ID_AA64MMFR0_EL1_TGRAN16_2_MASK                   (RT_BIT_64(32) | RT_BIT_64(33) | RT_BIT_64(34) | RT_BIT_64(35))
#define ARMV8_ID_AA64MMFR0_EL1_TGRAN16_2_SHIFT                  32
/** Support for 16KiB granule at stage 2 is identified in the ID_AA64MMFR0_EL1.TGran16 field. */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN16_2_SUPPORT_BY_TGRAN16    0
/** 16KiB granule not supported at stage 2. */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN16_2_NOT_IMPL              1
/** 16KiB granule supported at stage 2. */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN16_2_SUPPORTED             2
/** 16KiB granule supported at stage 2 and supports 52-bit input addresses and can describe 52-bit output addresses (FEAT_LPA2). */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN16_2_SUPPORTED_52BIT       3
/** Bit 36 - 39 - Indicates support for 64KiB granule size at stage 2. */
#define ARMV8_ID_AA64MMFR0_EL1_TGRAN64_2_MASK                   (RT_BIT_64(36) | RT_BIT_64(37) | RT_BIT_64(38) | RT_BIT_64(39))
#define ARMV8_ID_AA64MMFR0_EL1_TGRAN64_2_SHIFT                  36
/** Support for 64KiB granule at stage 2 is identified in the ID_AA64MMFR0_EL1.TGran64 field. */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN64_2_SUPPORT_BY_TGRAN64    0
/** 64KiB granule not supported at stage 2. */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN64_2_NOT_IMPL              1
/** 64KiB granule supported at stage 2. */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN64_2_SUPPORTED             2
/** Bit 40 - 43 - Indicates HCRX_EL2 and its associated EL3 trap support. */
#define ARMV8_ID_AA64MMFR0_EL1_TGRAN4_2_MASK                    (RT_BIT_64(40) | RT_BIT_64(41) | RT_BIT_64(42) | RT_BIT_64(43))
#define ARMV8_ID_AA64MMFR0_EL1_TGRAN4_2_SHIFT                   40
/** Support for 4KiB granule at stage 2 is identified in the ID_AA64MMFR0_EL1.TGran4 field. */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN4_2_SUPPORT_BY_TGRAN16     0
/** 4KiB granule not supported at stage 2. */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN4_2_NOT_IMPL               1
/** 4KiB granule supported at stage 2. */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN4_2_SUPPORTED              2
/** 4KiB granule supported at stage 2 and supports 52-bit input addresses and can describe 52-bit output addresses (FEAT_LPA2). */
# define ARMV8_ID_AA64MMFR0_EL1_TGRAN4_2_SUPPORTED_52BIT        3
/** Bit 44 - 47 - Indicates support for disabling context synchronizing exception entry and exit. */
#define ARMV8_ID_AA64MMFR0_EL1_EXS_MASK                         (RT_BIT_64(44) | RT_BIT_64(45) | RT_BIT_64(46) | RT_BIT_64(47))
#define ARMV8_ID_AA64MMFR0_EL1_EXS_SHIFT                        44
/** All exception entries and exits are context synchronization events. */
# define ARMV8_ID_AA64MMFR0_EL1_EXS_NOT_IMPL                    0
/** Non-context synchronizing exception entry and exit are supported (FEAT_ExS). */
# define ARMV8_ID_AA64MMFR0_EL1_EXS_SUPPORTED                   1
/* Bit 48 - 55 - Reserved. */
/** Bit 56 - 59 - Indicates the presence of the Fine-Grained Trap controls. */
#define ARMV8_ID_AA64MMFR0_EL1_FGT_MASK                         (RT_BIT_64(56) | RT_BIT_64(57) | RT_BIT_64(58) | RT_BIT_64(59))
#define ARMV8_ID_AA64MMFR0_EL1_FGT_SHIFT                        56
/** Fine-grained trap controls are not implemented. */
# define ARMV8_ID_AA64MMFR0_EL1_FGT_NOT_IMPL                    0
/** Fine-grained trap controls are implemented (FEAT_FGT). */
# define ARMV8_ID_AA64MMFR0_EL1_FGT_SUPPORTED                   1
/** Bit 60 - 63 - Indicates the presence of Enhanced Counter Virtualization. */
#define ARMV8_ID_AA64MMFR0_EL1_ECV_MASK                         (RT_BIT_64(60) | RT_BIT_64(61) | RT_BIT_64(62) | RT_BIT_64(63))
#define ARMV8_ID_AA64MMFR0_EL1_ECV_SHIFT                        60
/** Enhanced Counter Virtualization is not implemented. */
# define ARMV8_ID_AA64MMFR0_EL1_ECV_NOT_IMPL                    0
/** Enhanced Counter Virtualization is implemented (FEAT_ECV). */
# define ARMV8_ID_AA64MMFR0_EL1_ECV_SUPPORTED                   1
/** Enhanced Counter Virtualization is implemented and includes support for CNTHCTL_EL2.ECV and CNTPOFF_EL2 (FEAT_ECV). */
# define ARMV8_ID_AA64MMFR0_EL1_ECV_SUPPORTED_2                 2
/** @} */


/** @name ID_AA64MMFR1_EL1 - AArch64 Memory Model Feature Register 1.
 * @{ */
/** Bit 0 - 3 - Hardware updates to Access flag and Dirty state in translation tables. */
#define ARMV8_ID_AA64MMFR1_EL1_HAFDBS_MASK                      (RT_BIT_64(0) | RT_BIT_64(1) | RT_BIT_64(2) | RT_BIT_64(3))
#define ARMV8_ID_AA64MMFR1_EL1_HAFDBS_SHIFT                     0
/** Hardware update of the Access flag and dirty state are not supported. */
# define ARMV8_ID_AA64MMFR1_EL1_HAFDBS_NOT_IMPL                 0
/** Support for hardware update of the Access flag for Block and Page descriptors. */
# define ARMV8_ID_AA64MMFR1_EL1_HAFDBS_SUPPORTED                1
/** Support for hardware update of the Access flag for Block and Page descriptors, hardware update of dirty state supported. */
# define ARMV8_ID_AA64MMFR1_EL1_HAFDBS_DIRTY_SUPPORTED          2
/** Bit 4 - 7 - EL1 Exception level handling. */
#define ARMV8_ID_AA64MMFR1_EL1_VMIDBITS_MASK                    (RT_BIT_64(4) | RT_BIT_64(5) | RT_BIT_64(6) | RT_BIT_64(7))
#define ARMV8_ID_AA64MMFR1_EL1_VMIDBITS_SHIFT                   4
/** VMID bits is 8. */
# define ARMV8_ID_AA64MMFR1_EL1_VMIDBITS_8                      0
/** VMID bits is 16 (FEAT_VMID16). */
# define ARMV8_ID_AA64MMFR1_EL1_VMIDBITS_16                     2
/** Bit 8 - 11 - Virtualization Host Extensions support. */
#define ARMV8_ID_AA64MMFR1_EL1_VHE_MASK                         (RT_BIT_64(8) | RT_BIT_64(9) | RT_BIT_64(10) | RT_BIT_64(11))
#define ARMV8_ID_AA64MMFR1_EL1_VHE_SHIFT                        8
/** Virtualization Host Extensions are not supported. */
# define ARMV8_ID_AA64MMFR1_EL1_VHE_NOT_IMPL                    0
/** Virtualization Host Extensions are supported. */
# define ARMV8_ID_AA64MMFR1_EL1_VHE_SUPPORTED                   1
/** Bit 12 - 15 - Hierarchical Permission Disables. */
#define ARMV8_ID_AA64MMFR1_EL1_HPDS_MASK                        (RT_BIT_64(12) | RT_BIT_64(13) | RT_BIT_64(14) | RT_BIT_64(15))
#define ARMV8_ID_AA64MMFR1_EL1_HPDS_SHIFT                       12
/** Disabling of hierarchical controls not supported. */
# define ARMV8_ID_AA64MMFR1_EL1_HPDS_NOT_IMPL                   0
/** Disabling of hierarchical controls supported (FEAT_HPDS). */
# define ARMV8_ID_AA64MMFR1_EL1_HPDS_SUPPORTED                  1
/** FEAT_HPDS + possible hardware allocation of bits[62:59] of the translation table descriptors from the final lookup level (FEAT_HPDS2). */
# define ARMV8_ID_AA64MMFR1_EL1_HPDS_SUPPORTED_2                2
/** Bit 16 - 19 - LORegions support. */
#define ARMV8_ID_AA64MMFR1_EL1_LO_MASK                          (RT_BIT_64(16) | RT_BIT_64(17) | RT_BIT_64(18) | RT_BIT_64(19))
#define ARMV8_ID_AA64MMFR1_EL1_LO_SHIFT                         16
/** LORegions not supported. */
# define ARMV8_ID_AA64MMFR1_EL1_LO_NOT_IMPL                     0
/** LORegions supported. */
# define ARMV8_ID_AA64MMFR1_EL1_LO_SUPPORTED                    1
/** Bit 20 - 23 - Privileged Access Never support. */
#define ARMV8_ID_AA64MMFR1_EL1_PAN_MASK                         (RT_BIT_64(20) | RT_BIT_64(21) | RT_BIT_64(22) | RT_BIT_64(23))
#define ARMV8_ID_AA64MMFR1_EL1_PAN_SHIFT                        20
/** PAN not supported. */
# define ARMV8_ID_AA64MMFR1_EL1_PAN_NOT_IMPL                    0
/** PAN supported (FEAT_PAN). */
# define ARMV8_ID_AA64MMFR1_EL1_PAN_SUPPORTED                   1
/** PAN supported and AT S1E1RP and AT S1E1WP instructions supported (FEAT_PAN2). */
# define ARMV8_ID_AA64MMFR1_EL1_PAN_SUPPORTED_2                 2
/** PAN supported and AT S1E1RP and AT S1E1WP instructions and SCTRL_EL1.EPAN and SCTRL_EL2.EPAN supported (FEAT_PAN3). */
# define ARMV8_ID_AA64MMFR1_EL1_PAN_SUPPORTED_3                 3
/** Bit 24 - 27 - Describes whether the PE can generate SError interrupt exceptions. */
#define ARMV8_ID_AA64MMFR1_EL1_SPECSEI_MASK                     (RT_BIT_64(24) | RT_BIT_64(25) | RT_BIT_64(26) | RT_BIT_64(27))
#define ARMV8_ID_AA64MMFR1_EL1_SPECSEI_SHIFT                    24
/** The PE never generates an SError interrupt due to an External abort on a speculative read. */
# define ARMV8_ID_AA64MMFR1_EL1_SPECSEI_NOT_IMPL                0
/** The PE might generate an SError interrupt due to an External abort on a speculative read. */
# define ARMV8_ID_AA64MMFR1_EL1_SPECSEI_SUPPORTED               1
/** Bit 28 - 31 - Indicates support for execute-never control distinction by Exception level at stage 2. */
#define ARMV8_ID_AA64MMFR1_EL1_XNX_MASK                         (RT_BIT_64(28) | RT_BIT_64(29) | RT_BIT_64(30) | RT_BIT_64(31))
#define ARMV8_ID_AA64MMFR1_EL1_XNX_SHIFT                        28
/** Distinction between EL0 and EL1 execute-never control at stage 2 not supported. */
# define ARMV8_ID_AA64MMFR1_EL1_XNX_NOT_IMPL                    0
/** Distinction between EL0 and EL1 execute-never control at stage 2 supported (FEAT_XNX). */
# define ARMV8_ID_AA64MMFR1_EL1_XNX_SUPPORTED                   1
/** Bit 32 - 35 - Indicates support for the configurable delayed trapping of WFE. */
#define ARMV8_ID_AA64MMFR1_EL1_TWED_MASK                        (RT_BIT_64(32) | RT_BIT_64(33) | RT_BIT_64(34) | RT_BIT_64(35))
#define ARMV8_ID_AA64MMFR1_EL1_TWED_SHIFT                       32
/** Configurable delayed trapping of WFE is not supported. */
# define ARMV8_ID_AA64MMFR1_EL1_TWED_NOT_IMPL                   0
/** Configurable delayed trapping of WFE is supported (FEAT_TWED). */
# define ARMV8_ID_AA64MMFR1_EL1_TWED_SUPPORTED                  1
/** Bit 36 - 39 - Indicates support for Enhanced Translation Synchronization. */
#define ARMV8_ID_AA64MMFR1_EL1_ETS_MASK                         (RT_BIT_64(36) | RT_BIT_64(37) | RT_BIT_64(38) | RT_BIT_64(39))
#define ARMV8_ID_AA64MMFR1_EL1_ETS_SHIFT                        36
/** Enhanced Translation Synchronization is not supported. */
# define ARMV8_ID_AA64MMFR1_EL1_ETS_NOT_IMPL                    0
/** Enhanced Translation Synchronization is implemented. */
# define ARMV8_ID_AA64MMFR1_EL1_ETS_SUPPORTED                   1
/** Bit 40 - 43 - Indicates HCRX_EL2 and its associated EL3 trap support. */
#define ARMV8_ID_AA64MMFR1_EL1_HCX_MASK                         (RT_BIT_64(40) | RT_BIT_64(41) | RT_BIT_64(42) | RT_BIT_64(43))
#define ARMV8_ID_AA64MMFR1_EL1_HCX_SHIFT                        40
/** HCRX_EL2 and its associated EL3 trap are not supported. */
# define ARMV8_ID_AA64MMFR1_EL1_HCX_NOT_IMPL                    0
/** HCRX_EL2 and its associated EL3 trap are supported (FEAT_HCX). */
# define ARMV8_ID_AA64MMFR1_EL1_HCX_SUPPORTED                   1
/** Bit 44 - 47 - Indicates support for FPCR.{AH,FIZ,NEP}. */
#define ARMV8_ID_AA64MMFR1_EL1_AFP_MASK                         (RT_BIT_64(44) | RT_BIT_64(45) | RT_BIT_64(46) | RT_BIT_64(47))
#define ARMV8_ID_AA64MMFR1_EL1_AFP_SHIFT                        44
/** The FPCR.{AH,FIZ,NEP} fields are not supported. */
# define ARMV8_ID_AA64MMFR1_EL1_AFP_NOT_IMPL                    0
/** The FPCR.{AH,FIZ,NEP} fields are supported (FEAT_AFP). */
# define ARMV8_ID_AA64MMFR1_EL1_AFP_SUPPORTED                   1
/** Bit 48 - 51 - Indicates support for intermediate caching of translation table walks. */
#define ARMV8_ID_AA64MMFR1_EL1_NTLBPA_MASK                      (RT_BIT_64(48) | RT_BIT_64(49) | RT_BIT_64(50) | RT_BIT_64(51))
#define ARMV8_ID_AA64MMFR1_EL1_NTLBPA_SHIFT                     48
/** The intermediate caching of translation table walks might include non-coherent physical translation caches. */
# define ARMV8_ID_AA64MMFR1_EL1_NTLBPA_INCLUDE_NON_COHERENT     0
/** The intermediate caching of translation table walks does not include non-coherent physical translation caches (FEAT_nTLBPA). */
# define ARMV8_ID_AA64MMFR1_EL1_NTLBPA_INCLUDE_COHERENT_ONLY    1
/** Bit 52 - 55 - Indicates whether SCTLR_EL1.TIDCP and SCTLR_EL2.TIDCP are implemented in AArch64 state. */
#define ARMV8_ID_AA64MMFR1_EL1_TIDCP1_MASK                      (RT_BIT_64(52) | RT_BIT_64(53) | RT_BIT_64(54) | RT_BIT_64(55))
#define ARMV8_ID_AA64MMFR1_EL1_TIDCP1_SHIFT                     52
/** SCTLR_EL1.TIDCP and SCTLR_EL2.TIDCP bits are not implemented. */
# define ARMV8_ID_AA64MMFR1_EL1_TIDCP1_NOT_IMPL                 0
/** SCTLR_EL1.TIDCP and SCTLR_EL2.TIDCP bits are implemented (FEAT_TIDCP1). */
# define ARMV8_ID_AA64MMFR1_EL1_TIDCP1_SUPPORTED                1
/** Bit 56 - 59 - Indicates support for cache maintenance instruction permission. */
#define ARMV8_ID_AA64MMFR1_EL1_CMOW_MASK                        (RT_BIT_64(56) | RT_BIT_64(57) | RT_BIT_64(58) | RT_BIT_64(59))
#define ARMV8_ID_AA64MMFR1_EL1_CMOW_SHIFT                       56
/** SCTLR_EL1.CMOW, SCTLR_EL2.CMOW and HCRX_EL2.CMOW bits are not implemented. */
# define ARMV8_ID_AA64MMFR1_EL1_CMOW_NOT_IMPL                   0
/** SCTLR_EL1.CMOW, SCTLR_EL2.CMOW and HCRX_EL2.CMOW bits are implemented (FEAT_CMOW). */
# define ARMV8_ID_AA64MMFR1_EL1_CMOW_SUPPORTED                  1
/* Bit 60 - 63 - Reserved. */
/** @} */


/** @name ID_AA64MMFR2_EL1 - AArch64 Memory Model Feature Register 2.
 * @{ */
/** Bit 0 - 3 - Indicates support for Common not Private translations. */
#define ARMV8_ID_AA64MMFR2_EL1_CNP_MASK                         (RT_BIT_64(0) | RT_BIT_64(1) | RT_BIT_64(2) | RT_BIT_64(3))
#define ARMV8_ID_AA64MMFR2_EL1_CNP_SHIFT                        0
/** Common not Private translations are not supported. */
# define ARMV8_ID_AA64MMFR2_EL1_CNP_NOT_IMPL                    0
/** Support for Common not Private translations (FEAT_TTNCP). */
# define ARMV8_ID_AA64MMFR2_EL1_CNP_SUPPORTED                   1
/** Bit 4 - 7 - Indicates support for User Access Override. */
#define ARMV8_ID_AA64MMFR2_EL1_UAO_MASK                         (RT_BIT_64(4) | RT_BIT_64(5) | RT_BIT_64(6) | RT_BIT_64(7))
#define ARMV8_ID_AA64MMFR2_EL1_UAO_SHIFT                        4
/** User Access Override is not supported. */
# define ARMV8_ID_AA64MMFR2_EL1_UAO_NOT_IMPL                    0
/** User Access Override is supported (FEAT_UAO). */
# define ARMV8_ID_AA64MMFR2_EL1_UAO_SUPPORTED                   1
/** Bit 8 - 11 - Indicates support for LSMAOE and nTLSMD bits in SCTLR_ELx. */
#define ARMV8_ID_AA64MMFR2_EL1_LSM_MASK                         (RT_BIT_64(8) | RT_BIT_64(9) | RT_BIT_64(10) | RT_BIT_64(11))
#define ARMV8_ID_AA64MMFR2_EL1_LSM_SHIFT                        8
/** LSMAOE and nTLSMD bits are not supported. */
# define ARMV8_ID_AA64MMFR2_EL1_LSM_NOT_IMPL                    0
/** LSMAOE and nTLSMD bits are supported (FEAT_LSMAOC). */
# define ARMV8_ID_AA64MMFR2_EL1_LSM_SUPPORTED                   1
/** Bit 12 - 15 - Indicates support for the IESB bit in SCTLR_ELx registers. */
#define ARMV8_ID_AA64MMFR2_EL1_IESB_MASK                        (RT_BIT_64(12) | RT_BIT_64(13) | RT_BIT_64(14) | RT_BIT_64(15))
#define ARMV8_ID_AA64MMFR2_EL1_IESB_SHIFT                       12
/** IESB bit is not supported. */
# define ARMV8_ID_AA64MMFR2_EL1_IESB_NOT_IMPL                   0
/** IESB bit is supported (FEAT_IESB). */
# define ARMV8_ID_AA64MMFR2_EL1_IESB_SUPPORTED                  1
/** Bit 16 - 19 - Indicates support for larger virtual address. */
#define ARMV8_ID_AA64MMFR2_EL1_VARANGE_MASK                     (RT_BIT_64(16) | RT_BIT_64(17) | RT_BIT_64(18) | RT_BIT_64(19))
#define ARMV8_ID_AA64MMFR2_EL1_VARANGE_SHIFT                    16
/** Virtual address range is 48 bits. */
# define ARMV8_ID_AA64MMFR2_EL1_VARANGE_48BITS                  0
/** 52 bit virtual addresses supported for 64KiB granules (FEAT_LVA). */
# define ARMV8_ID_AA64MMFR2_EL1_VARANGE_52BITS_64KB_GRAN        1
/** Bit 20 - 23 - Revised CCSIDR_EL1 register format supported. */
#define ARMV8_ID_AA64MMFR2_EL1_CCIDX_MASK                       (RT_BIT_64(20) | RT_BIT_64(21) | RT_BIT_64(22) | RT_BIT_64(23))
#define ARMV8_ID_AA64MMFR2_EL1_CCIDX_SHIFT                      20
/** CCSIDR_EL1 register format is 32-bit. */
# define ARMV8_ID_AA64MMFR2_EL1_CCIDX_32BIT                     0
/** CCSIDR_EL1 register format is 64-bit (FEAT_CCIDX). */
# define ARMV8_ID_AA64MMFR2_EL1_CCIDX_64BIT                     1
/** Bit 24 - 27 - Indicates support for nested virtualization. */
#define ARMV8_ID_AA64MMFR2_EL1_NV_MASK                          (RT_BIT_64(24) | RT_BIT_64(25) | RT_BIT_64(26) | RT_BIT_64(27))
#define ARMV8_ID_AA64MMFR2_EL1_NV_SHIFT                         24
/** Nested virtualization is not supported. */
# define ARMV8_ID_AA64MMFR2_EL1_NV_NOT_IMPL                     0
/** The HCR_EL2.{AT,NV1,NV} bits are implemented (FEAT_NV). */
# define ARMV8_ID_AA64MMFR2_EL1_NV_SUPPORTED                    1
/** The VNCR_EL2 register and HCR_EL2.{NV2,AT,NV1,NV} bits are implemented (FEAT_NV2). */
# define ARMV8_ID_AA64MMFR2_EL1_NV_SUPPORTED_2                  2
/** Bit 28 - 31 - Indicates support for small translation tables. */
#define ARMV8_ID_AA64MMFR2_EL1_ST_MASK                          (RT_BIT_64(28) | RT_BIT_64(29) | RT_BIT_64(30) | RT_BIT_64(31))
#define ARMV8_ID_AA64MMFR2_EL1_ST_SHIFT                         28
/** The maximum value of TCR_ELx.{T0SZ,T1SZ} is 39. */
# define ARMV8_ID_AA64MMFR2_EL1_ST_NOT_IMPL                     0
/** The maximum value of TCR_ELx.{T0SZ,T1SZ} is 48 for 4KiB and 16KiB, and 47 for 64KiB granules (FEAT_TTST). */
# define ARMV8_ID_AA64MMFR2_EL1_ST_SUPPORTED                    1
/** Bit 32 - 35 - Indicates support for unaligned single-copy atomicity and atomic functions. */
#define ARMV8_ID_AA64MMFR2_EL1_AT_MASK                          (RT_BIT_64(32) | RT_BIT_64(33) | RT_BIT_64(34) | RT_BIT_64(35))
#define ARMV8_ID_AA64MMFR2_EL1_AT_SHIFT                         32
/** Unaligned single-copy atomicity and atomic functions are not supported. */
# define ARMV8_ID_AA64MMFR2_EL1_AT_NOT_IMPL                     0
/** Unaligned single-copy atomicity and atomic functions are supported (FEAT_LSE2). */
# define ARMV8_ID_AA64MMFR2_EL1_AT_SUPPORTED                    1
/** Bit 36 - 39 - Indicates value of ESR_ELx.EC that reports an exception generated by a read access to the feature ID space. */
#define ARMV8_ID_AA64MMFR2_EL1_IDS_MASK                         (RT_BIT_64(36) | RT_BIT_64(37) | RT_BIT_64(38) | RT_BIT_64(39))
#define ARMV8_ID_AA64MMFR2_EL1_IDS_SHIFT                        36
/** ESR_ELx.EC is 0 for traps generated by a read access to the feature ID space. */
# define ARMV8_ID_AA64MMFR2_EL1_IDS_EC_0                        0
/** ESR_ELx.EC is 0x18 for traps generated by a read access to the feature ID space (FEAT_IDST). */
# define ARMV8_ID_AA64MMFR2_EL1_IDS_EC_18H                      1
/** Bit 40 - 43 - Indicates support for the HCR_EL2.FWB bit. */
#define ARMV8_ID_AA64MMFR2_EL1_FWB_MASK                         (RT_BIT_64(40) | RT_BIT_64(41) | RT_BIT_64(42) | RT_BIT_64(43))
#define ARMV8_ID_AA64MMFR2_EL1_FWB_SHIFT                        40
/** HCR_EL2.FWB bit is not supported. */
# define ARMV8_ID_AA64MMFR2_EL1_FWB_NOT_IMPL                    0
/** HCR_EL2.FWB bit is supported (FEAT_S2FWB). */
# define ARMV8_ID_AA64MMFR2_EL1_FWB_SUPPORTED                   1
/* Bit 44 - 47 - Reserved. */
/** Bit 48 - 51 - Indicates support for TTL field in address operations. */
#define ARMV8_ID_AA64MMFR2_EL1_TTL_MASK                         (RT_BIT_64(48) | RT_BIT_64(49) | RT_BIT_64(50) | RT_BIT_64(51))
#define ARMV8_ID_AA64MMFR2_EL1_TTL_SHIFT                        48
/** TLB maintenance instructions by address have bits [47:44] Res0. */
# define ARMV8_ID_AA64MMFR2_EL1_TTL_NOT_IMPL                    0
/** TLB maintenance instructions by address have bits [47:44] holding the TTL field (FEAT_TTL). */
# define ARMV8_ID_AA64MMFR2_EL1_TTL_SUPPORTED                   1
/** Bit 52 - 55 - Identification of the hardware requirements of the hardware to have break-before-make sequences when
 * changing block size for a translation. */
#define ARMV8_ID_AA64MMFR2_EL1_BBM_MASK                         (RT_BIT_64(52) | RT_BIT_64(53) | RT_BIT_64(54) | RT_BIT_64(55))
#define ARMV8_ID_AA64MMFR2_EL1_BBM_SHIFT                        52
/** Level 0 support for changing block size is supported (FEAT_BBM). */
# define ARMV8_ID_AA64MMFR2_EL1_BBM_LVL0                        0
/** Level 1 support for changing block size is supported (FEAT_BBM). */
# define ARMV8_ID_AA64MMFR2_EL1_BBM_LVL1                        1
/** Level 2 support for changing block size is supported (FEAT_BBM). */
# define ARMV8_ID_AA64MMFR2_EL1_BBM_LVL2                        2
/** Bit 56 - 59 - Indicates support for Enhanced Virtualization Traps. */
#define ARMV8_ID_AA64MMFR2_EL1_EVT_MASK                         (RT_BIT_64(56) | RT_BIT_64(57) | RT_BIT_64(58) | RT_BIT_64(59))
#define ARMV8_ID_AA64MMFR2_EL1_EVT_SHIFT                        56
/** Enhanced Virtualization Traps are not supported. */
# define ARMV8_ID_AA64MMFR2_EL1_EVT_NOT_IMPL                    0
/** Enhanced Virtualization Traps are supported (FEAT_EVT). */
# define ARMV8_ID_AA64MMFR2_EL1_EVT_SUPPORTED                   1
/** Enhanced Virtualization Traps are supported with additional traps (FEAT_EVT). */
# define ARMV8_ID_AA64MMFR2_EL1_EVT_SUPPORTED_2                 2
/** Bit 60 - 63 - Indicates support for E0PDx mechanism. */
#define ARMV8_ID_AA64MMFR2_EL1_E0PD_MASK                        (RT_BIT_64(60) | RT_BIT_64(61) | RT_BIT_64(62) | RT_BIT_64(63))
#define ARMV8_ID_AA64MMFR2_EL1_E0PD_SHIFT                       60
/** E0PDx mechanism is not supported. */
# define ARMV8_ID_AA64MMFR2_EL1_E0PD_NOT_IMPL                   0
/** E0PDx mechanism is supported (FEAT_E0PD). */
# define ARMV8_ID_AA64MMFR2_EL1_E0PD_SUPPORTED                  1
/** @} */


/** @name ID_AA64DFR0_EL1 - AArch64 Debug Feature Register 0.
 * @{ */
/** Bit 0 - 3 - Indicates the Debug Architecture version supported. */
#define ARMV8_ID_AA64DFR0_EL1_DEBUGVER_MASK                     (RT_BIT_64(0) | RT_BIT_64(1) | RT_BIT_64(2) | RT_BIT_64(3))
#define ARMV8_ID_AA64DFR0_EL1_DEBUGVER_SHIFT                    0
/** Armv8 debug architecture version. */
# define ARMV8_ID_AA64DFR0_EL1_DEBUGVER_ARMV8                   6
/** Armv8 debug architecture version with virtualization host extensions. */
# define ARMV8_ID_AA64DFR0_EL1_DEBUGVER_ARMV8_VHE               7
/** Armv8.2 debug architecture version (FEAT_Debugv8p2). */
# define ARMV8_ID_AA64DFR0_EL1_DEBUGVER_ARMV8p2                 8
/** Armv8.4 debug architecture version (FEAT_Debugv8p4). */
# define ARMV8_ID_AA64DFR0_EL1_DEBUGVER_ARMV8p4                 9
/** Armv8.8 debug architecture version (FEAT_Debugv8p8). */
# define ARMV8_ID_AA64DFR0_EL1_DEBUGVER_ARMV8p8                 10
/** Bit 4 - 7 - Indicates trace support. */
#define ARMV8_ID_AA64DFR0_EL1_TRACEVER_MASK                     (RT_BIT_64(4) | RT_BIT_64(5) | RT_BIT_64(6) | RT_BIT_64(7))
#define ARMV8_ID_AA64DFR0_EL1_TRACEVER_SHIFT                    4
/** Trace unit System registers not implemented. */
# define ARMV8_ID_AA64DFR0_EL1_TRACEVER_NOT_IMPL                0
/** Trace unit System registers supported. */
# define ARMV8_ID_AA64DFR0_EL1_TRACEVER_SUPPORTED               1
/** Bit 8 - 11 - Performance Monitors Extension version. */
#define ARMV8_ID_AA64DFR0_EL1_PMUVER_MASK                       (RT_BIT_64(8) | RT_BIT_64(9) | RT_BIT_64(10) | RT_BIT_64(11))
#define ARMV8_ID_AA64DFR0_EL1_PMUVER_SHIFT                      8
/** Performance Monitors Extension not supported. */
# define ARMV8_ID_AA64DFR0_EL1_PMUVER_NOT_IMPL                  0
/** Performance Monitors Extension v3 supported (FEAT_PMUv3). */
# define ARMV8_ID_AA64DFR0_EL1_PMUVER_SUPPORTED_V3              1
/** Performance Monitors Extension v3 supported (FEAT_PMUv3p1). */
# define ARMV8_ID_AA64DFR0_EL1_PMUVER_SUPPORTED_V3P1            4
/** Performance Monitors Extension v3 supported (FEAT_PMUv3p4). */
# define ARMV8_ID_AA64DFR0_EL1_PMUVER_SUPPORTED_V3P4            5
/** Performance Monitors Extension v3 supported (FEAT_PMUv3p5). */
# define ARMV8_ID_AA64DFR0_EL1_PMUVER_SUPPORTED_V3P5            6
/** Performance Monitors Extension v3 supported (FEAT_PMUv3p7). */
# define ARMV8_ID_AA64DFR0_EL1_PMUVER_SUPPORTED_V3P7            7
/** Performance Monitors Extension v3 supported (FEAT_PMUv3p8). */
# define ARMV8_ID_AA64DFR0_EL1_PMUVER_SUPPORTED_V3P8            8
/** Bit 12 - 15 - Number of breakpoints, minus 1. */
#define ARMV8_ID_AA64DFR0_EL1_BRPS_MASK                         (RT_BIT_64(12) | RT_BIT_64(13) | RT_BIT_64(14) | RT_BIT_64(15))
#define ARMV8_ID_AA64DFR0_EL1_BRPS_SHIFT                        12
/* Bit 16 - 19 - Reserved 0. */
/** Bit 20 - 23 - Number of watchpoints, minus 1. */
#define ARMV8_ID_AA64DFR0_EL1_WRPS_MASK                         (RT_BIT_64(20) | RT_BIT_64(21) | RT_BIT_64(22) | RT_BIT_64(23))
#define ARMV8_ID_AA64DFR0_EL1_WRPS_SHIFT                        20
/* Bit 24 - 27 - Reserved 0. */
/** Bit 28 - 31 - Number of context-aware breakpoints. */
#define ARMV8_ID_AA64DFR0_EL1_CTXCMPS_MASK                      (RT_BIT_64(28) | RT_BIT_64(29) | RT_BIT_64(30) | RT_BIT_64(31))
#define ARMV8_ID_AA64DFR0_EL1_CTXCMPS_SHIFT                     28
/** Bit 32 - 35 - Statistical Profiling Extension version. */
#define ARMV8_ID_AA64DFR0_EL1_PMSVER_MASK                       (RT_BIT_64(32) | RT_BIT_64(33) | RT_BIT_64(34) | RT_BIT_64(35))
#define ARMV8_ID_AA64DFR0_EL1_PMSVER_SHIFT                      32
/** Statistical Profiling Extension not implemented. */
# define ARMV8_ID_AA64DFR0_EL1_PMSVER_NOT_IMPL                  0
/** Statistical Profiling Extension supported (FEAT_SPE). */
# define ARMV8_ID_AA64DFR0_EL1_PMSVER_SUPPORTED                 1
/** Statistical Profiling Extension supported, version 1.1 (FEAT_SPEv1p1). */
# define ARMV8_ID_AA64DFR0_EL1_PMSVER_SUPPORTED_V1P1            2
/** Statistical Profiling Extension supported, version 1.2 (FEAT_SPEv1p2). */
# define ARMV8_ID_AA64DFR0_EL1_PMSVER_SUPPORTED_V1P2            3
/** Statistical Profiling Extension supported, version 1.2 (FEAT_SPEv1p3). */
# define ARMV8_ID_AA64DFR0_EL1_PMSVER_SUPPORTED_V1P3            4
/** Bit 36 - 39 - OS Double Lock implemented. */
#define ARMV8_ID_AA64DFR0_EL1_DOUBLELOCK_MASK                   (RT_BIT_64(36) | RT_BIT_64(37) | RT_BIT_64(38) | RT_BIT_64(39))
#define ARMV8_ID_AA64DFR0_EL1_DOUBLELOCK_SHIFT                  36
/** OS Double Lock is not implemented. */
# define ARMV8_ID_AA64DFR0_EL1_DOUBLELOCK_NOT_IMPL              0xf
/** OS Double Lock is supported (FEAT_DoubleLock). */
# define ARMV8_ID_AA64DFR0_EL1_DOUBLELOCK_SUPPORTED             0
/** Bit 40 - 43 - Indicates the Armv8.4 self-hosted Trace Extension. */
#define ARMV8_ID_AA64DFR0_EL1_TRACEFILT_MASK                    (RT_BIT_64(40) | RT_BIT_64(41) | RT_BIT_64(42) | RT_BIT_64(43))
#define ARMV8_ID_AA64DFR0_EL1_TRACEFILT_SHIFT                   40
/** Armv8.4 self-hosted Trace Extension not implemented. */
# define ARMV8_ID_AA64DFR0_EL1_TRACEFILT_NOT_IMPL               0
/** Armv8.4 self-hosted Trace Extension is supported (FEAT_TRF). */
# define ARMV8_ID_AA64DFR0_EL1_TRACEFILT_SUPPORTED              1
/** Bit 44 - 47 - Indicates support for the Trace Buffer Extension. */
#define ARMV8_ID_AA64DFR0_EL1_TRACEBUFFER_MASK                  (RT_BIT_64(44) | RT_BIT_64(45) | RT_BIT_64(46) | RT_BIT_64(47))
#define ARMV8_ID_AA64DFR0_EL1_TRACEBUFFER_SHIFT                 44
/** Trace Buffer Extension is not implemented. */
# define ARMV8_ID_AA64DFR0_EL1_TRACEBUFFER_NOT_IMPL             0
/** Trace Buffer Extension is supported (FEAT_TRBE). */
# define ARMV8_ID_AA64DFR0_EL1_TRACEBUFFER_SUPPORTED            1
/** Bit 48 - 51 - Indicates support for the multi-threaded PMU extension. */
#define ARMV8_ID_AA64DFR0_EL1_MTPMU_MASK                        (RT_BIT_64(48) | RT_BIT_64(49) | RT_BIT_64(50) | RT_BIT_64(51))
#define ARMV8_ID_AA64DFR0_EL1_MTPMU_SHIFT                       48
/** Multi-threaded PMU extension is not implemented. */
# define ARMV8_ID_AA64DFR0_EL1_MTPMU_NOT_IMPL                   0
/** Multi-threaded PMU extension is supported (FEAT_MTPMU). */
# define ARMV8_ID_AA64DFR0_EL1_MTPMU_SUPPORTED                  1
/** Multi-threaded PMU extension is not implemented. */
# define ARMV8_ID_AA64DFR0_EL1_MTPMU_NOT_IMPL_2                 0xf
/** Bit 52 - 55 - Indicates support for the Branch Record Buffer extension. */
#define ARMV8_ID_AA64DFR0_EL1_BRBE_MASK                         (RT_BIT_64(52) | RT_BIT_64(53) | RT_BIT_64(54) | RT_BIT_64(55))
#define ARMV8_ID_AA64DFR0_EL1_BRBE_SHIFT                        52
/** Branch Record Buffer extension is not implemented. */
# define ARMV8_ID_AA64DFR0_EL1_BRBE_NOT_IMPL                    0
/** Branch Record Buffer extension is supported (FEAT_BRBE). */
# define ARMV8_ID_AA64DFR0_EL1_BRBE_SUPPORTED                   1
/** Branch Record Buffer extension is supported and supports branch recording at EL3 (FEAT_BRBEv1p1). */
# define ARMV8_ID_AA64DFR0_EL1_BRBE_SUPPORTED_V1P1              2
/* Bit 56 - 59 - Reserved. */
/** Bit 60 - 63 - Indicates support for Zero PMU event counters for guest operating systems. */
#define ARMV8_ID_AA64DFR0_EL1_HPMN0_MASK                        (RT_BIT_64(60) | RT_BIT_64(61) | RT_BIT_64(62) | RT_BIT_64(63))
#define ARMV8_ID_AA64DFR0_EL1_HPMN0_SHIFT                       60
/** Setting MDCE_EL2.HPMN to zero has CONSTRAINED UNPREDICTABLE behavior. */
# define ARMV8_ID_AA64DFR0_EL1_HPMN0_NOT_IMPL                   0
/** Setting MDCE_EL2.HPMN to zero has defined behavior (FEAT_HPMN0). */
# define ARMV8_ID_AA64DFR0_EL1_HPMN0_SUPPORTED                  1
/** @} */


/** @name FPCR - AArch64 Floating Point Control Register.
 * @{ */
/** Bit 0 - Flush Inputs to Zero when FEAT_AFP is supported. */
#define ARMV8_FPCR_FIZ                                          RT_BIT_64(0)
#define ARMV8_FPCR_FIZ_BIT                                      0
/** Bit 1 - Alternate Handling of floating-point numbers when FEAT_AFP is supported. */
#define ARMV8_FPCR_AH                                           RT_BIT_64(1)
#define ARMV8_FPCR_AH_BIT                                       1
/** Bit 2 - Controls how the output elements other than the lowest element of the vector are determined for
 * Advanced SIMD scalar instructions, when FEAT_AFP is supported. */
#define ARMV8_FPCR_NEP                                          RT_BIT_64(2)
#define ARMV8_FPCR_NEP_BIT                                      2
/* Bit 3 - 7 - Reserved.*/
/** Bit 8 - Invalid Operation floating-point exception trap enable. */
#define ARMV8_FPCR_IOE                                          RT_BIT_64(8)
#define ARMV8_FPCR_IOE_BIT                                      8
/** Bit 9 - Divide by Zero floating-point exception trap enable. */
#define ARMV8_FPCR_DZE                                          RT_BIT_64(9)
#define ARMV8_FPCR_DZE_BIT                                      9
/** Bit 10 - Overflow floating-point exception trap enable. */
#define ARMV8_FPCR_OFE                                          RT_BIT_64(10)
#define ARMV8_FPCR_OFE_BIT                                      10
/** Bit 11 - Underflow floating-point exception trap enable. */
#define ARMV8_FPCR_UFE                                          RT_BIT_64(11)
#define ARMV8_FPCR_UFE_BIT                                      11
/** Bit 12 - Inexact floating-point exception trap enable. */
#define ARMV8_FPCR_IXE                                          RT_BIT_64(12)
#define ARMV8_FPCR_IXE_BIT                                      12
/** Bit 13 - Controls numeric behavior of BFloat16 dot productions calculations performed,
 * supported when FEAT_EBF16 is supported. */
#define ARMV8_FPCR_EBF                                          RT_BIT_64(13)
#define ARMV8_FPCR_EBF_BIT                                      13
/* Bit 14 - Reserved */
/** Bit 15 - Input Denormal floating-point exception trap enable. */
#define ARMV8_FPCR_IDE                                          RT_BIT_64(15)
#define ARMV8_FPCR_IDE_BIT                                      15
/* Bit 16 - 18 - Reserved for AArch64 (Len field for AArch32). */
/** Bit 19 - Flushing denormalized numbers to zero control bit on half-precision data-processing instructions,
 * available when FEAT_FP16 is supported. */
#define ARMV8_FPCR_FZ16                                         RT_BIT_64(19)
#define ARMV8_FPCR_FZ16_BIT                                       19
/* Bit 20 - 21 - Reserved for AArch64 (Stride field dor AArch32). */
/** Bit 22 - 23 - Rounding Mode control field. */
#define ARMV8_FPCR_RMODE_MASK                                   (RT_BIT_64(22) | RT_BIT_64(23))
#define ARMV8_FPCR_RMODE_SHIFT                                  22
/** Round to Nearest (RN) mode. */
# define ARMV8_FPCR_RMODE_RN                                    0
/** Round towards Plus Infinity (RP) mode. */
# define ARMV8_FPCR_RMODE_RP                                    1
/** Round towards Minus Infinity (RM) mode. */
# define ARMV8_FPCR_RMODE_RM                                    2
/** Round towards Zero (RZ) mode. */
# define ARMV8_FPCR_RMODE_RZ                                    3
/** Bit 24 - Flushing denormalized numbers to zero control bit. */
#define ARMV8_FPCR_FZ                                           RT_BIT_64(24)
#define ARMV8_FPCR_FZ_BIT                                       24
/** Bit 25 - Default NaN use for NaN propagation. */
#define ARMV8_FPCR_DN                                           RT_BIT_64(25)
#define ARMV8_FPCR_DN_BIT                                       25
/** Bit 26 - Alternative half-precision control bit. */
#define ARMV8_FPCR_AHP                                          RT_BIT_64(26)
#define ARMV8_FPCR_AHP_BIT                                      26
/* Bit 27 - 63 - Reserved. */
/** @} */


/** @name FPSR - AArch64 Floating Point Status Register.
 * @{ */
/** Bit 0 - Invalid Operation cumulative floating-point exception bit. */
#define ARMV8_FPSR_IOC                                          RT_BIT_64(0)
/** Bit 1 - Divide by Zero cumulative floating-point exception bit. */
#define ARMV8_FPSR_DZC                                          RT_BIT_64(1)
/** Bit 2 - Overflow cumulative floating-point exception bit. */
#define ARMV8_FPSR_OFC                                          RT_BIT_64(2)
/** Bit 3 - Underflow cumulative floating-point exception bit. */
#define ARMV8_FPSR_UFC                                          RT_BIT_64(3)
/** Bit 4 - Inexact cumulative floating-point exception bit. */
#define ARMV8_FPSR_IXC                                          RT_BIT_64(4)
/* Bit 5 - 6 - Reserved. */
/** Bit 7 - Input Denormal cumulative floating-point exception bit. */
#define ARMV8_FPSR_IDC                                          RT_BIT_64(7)
/* Bit 8 - 26 - Reserved. */
/** Bit 27 - Cumulative saturation bit, Advanced SIMD only. */
#define ARMV8_FPSR_QC                                           RT_BIT_64(27)
/* Bit 28 - 31 - NZCV bits for AArch32 floating point operations. */
/* Bit 32 - 63 - Reserved. */
/** @} */



/** @name SCTLR_EL1 - AArch64 System Control Register (EL1).
 * @{ */
/** Bit 0 - MMU enable for EL1 and EL0 stage 1 address translation. */
#define ARMV8_SCTLR_EL1_M                                       RT_BIT_64(0)
/** Bit 1 - Alignment check enable for EL1 and EL0. */
#define ARMV8_SCTLR_EL1_A                                       RT_BIT_64(1)
/** Bit 2 - Stage 1 cacheability control, for data accesses. */
#define ARMV8_SCTLR_EL1_C                                       RT_BIT_64(2)
/** Bit 3 - SP alignment check enable. */
#define ARMV8_SCTLR_EL1_SA                                      RT_BIT_64(3)
/** Bit 4 - SP alignment check enable for EL0. */
#define ARMV8_SCTLR_EL1_SA0                                     RT_BIT_64(4)
/** Bit 5 - System instruction memory barrier enable from AArch32 EL0. */
#define ARMV8_SCTLR_EL1_CP15BEN                                 RT_BIT_64(5)
/** Bit 6 - Non-aligned access enable. */
#define ARMV8_SCTLR_EL1_NAA                                     RT_BIT_64(6)
#define ARMV8_SCTLR_EL1_nAA                                     RT_BIT_64(6)
/** Bit 7 - IT disable, disables some uses of IT instructions at EL0 using AArch32. */
#define ARMV8_SCTLR_EL1_ITD                                     RT_BIT_64(7)
/** Bit 8 - SETEND instruction disable, disables SETEND instructions at EL0 using AArch32. */
#define ARMV8_SCTLR_EL1_SED                                     RT_BIT_64(8)
/** Bit 9 - User Mask Access. Traps EL0 execution of MSR and MRS instructions that access the PSTATE.{D,A,I,F} masks to EL1. */
#define ARMV8_SCTLR_EL1_UMA                                     RT_BIT_64(9)
/** Bit 10 - Enable EL0 acccess to the CFP*, DVP* and CPP* instructions if FEAT_SPECRES is supported. */
#define ARMV8_SCTLR_EL1_ENRCTX                                  RT_BIT_64(10)
#define ARMV8_SCTLR_EL1_EnRCTX                                  ARMV8_SCTLR_EL1_ENRCTX
/** Bit 11 - Exception Exit is Context Synchronizing (FEAT_ExS required). */
#define ARMV8_SCTLR_EL1_EOS                                     RT_BIT_64(11)
/** Bit 12 - Stage 1 instruction access cacheability control, for access at EL0 and EL1. */
#define ARMV8_SCTLR_EL1_I                                       RT_BIT_64(12)
/** @todo Finish (lazy developer). */
/** @} */


/** @name SCTLR_EL2 - AArch64 System Control Register (EL2).
 * @{ */
/** Bit 0 - MMU enable for EL2. */
#define ARMV8_SCTLR_EL2_M                                       RT_BIT_64(0)
/** Bit 1 - Alignment check enable. */
#define ARMV8_SCTLR_EL2_A                                       RT_BIT_64(1)
/** Bit 2 - Global enable for data and unified caches. */
#define ARMV8_SCTLR_EL2_C                                       RT_BIT_64(2)
/** Bit 3 - SP alignment check enable. */
#define ARMV8_SCTLR_EL2_SA                                      RT_BIT_64(3)
/** Bit 4 - SA0. */
#define ARMV8_SCTLR_EL2_SA0                                     RT_BIT_64(4)
/** Bit 5 - CP15BEN. */
#define ARMV8_SCTLR_EL2_CP15BEN                                 RT_BIT_64(5)
/** Bit 6 - nAA. */
#define ARMV8_SCTLR_EL2_NAA                                     RT_BIT_64(6)
/** Bit 7 - IDT. */
#define ARMV8_SCTLR_EL2_IDT                                     RT_BIT_64(7)
/** Bit 8 - SED. */
#define ARMV8_SCTLR_EL2_SED                                     RT_BIT_64(8)
/*  Bit 9 - RES0 (2024-12). */
/** Bit 10 - EnRCTX. */
#define ARMV8_SCTLR_EL2_ENRCTX                                  RT_BIT_64(10)
/** Bit 11 - EOS. */
#define ARMV8_SCTLR_EL2_EOS                                     RT_BIT_64(11)
/** Bit 12 - Instruction cache enable. */
#define ARMV8_SCTLR_EL2_I                                       RT_BIT_64(12)
/** Bit 13 - EnDB. */
#define ARMV8_SCTLR_EL2_ENDB                                    RT_BIT_64(13)
/** Bit 14 - DZE. */
#define ARMV8_SCTLR_EL2_DZE                                     RT_BIT_64(14)
/** Bit 15 - UCT. */
#define ARMV8_SCTLR_EL2_UCT                                     RT_BIT_64(15)
/** Bit 16 - nTWI. */
#define ARMV8_SCTLR_EL2_NTWI                                    RT_BIT_64(16)
/*  Bit 17 - RES0 (2024-12). */
/** Bit 18 - nTWE. */
#define ARMV8_SCTLR_EL2_NTWE                                    RT_BIT_64(18)
/** Bit 19 - Force treatment of all memory regions with write permissions as XN. */
#define ARMV8_SCTLR_EL2_WXN                                     RT_BIT_64(19)
/** Bit 20 - TSCXT. */
#define ARMV8_SCTLR_EL2_TSCXT                                   RT_BIT_64(20)
/** Bit 21 - IESB. */
#define ARMV8_SCTLR_EL2_IESB                                    RT_BIT_64(21)
/** Bit 22 - EIS. */
#define ARMV8_SCTLR_EL2_EIS                                     RT_BIT_64(22)
/** Bit 23 - SPAN. */
#define ARMV8_SCTLR_EL2_SPAN                                    RT_BIT_64(23)
/** Bit 24 - E0E. */
#define ARMV8_SCTLR_EL2_E0E                                     RT_BIT_64(24)
/** Bit 25 - Exception endianess - set means big endian, clear little endian. */
#define ARMV8_SCTLR_EL2_EE                                      RT_BIT_64(25)
/** @todo Finish (lazy developer). */
/** @} */


/** @name HCR_EL2 - AArch64 Hypervisor Configuration Register (EL2).
 * @{ */
#define ARMV8_HCR_EL2_VM                                        RT_BIT_64(0)
#define ARMV8_HCR_EL2_SWIO                                      RT_BIT_64(1)
#define ARMV8_HCR_EL2_PTW                                       RT_BIT_64(2)
#define ARMV8_HCR_EL2_FMO                                       RT_BIT_64(3)
#define ARMV8_HCR_EL2_IMO                                       RT_BIT_64(4)
#define ARMV8_HCR_EL2_AMO                                       RT_BIT_64(5)
#define ARMV8_HCR_EL2_VF                                        RT_BIT_64(6)
#define ARMV8_HCR_EL2_VI                                        RT_BIT_64(7)
#define ARMV8_HCR_EL2_VSE                                       RT_BIT_64(8)
#define ARMV8_HCR_EL2_FB                                        RT_BIT_64(9)
#define ARMV8_HCR_EL2_BSU_MASK                                  (RT_BIT_64(10) | RT_BIT_64(11))
#define ARMV8_HCR_EL2_DC                                        RT_BIT_64(12)
#define ARMV8_HCR_EL2_TWI                                       RT_BIT_64(13)
#define ARMV8_HCR_EL2_TWE                                       RT_BIT_64(14)
#define ARMV8_HCR_EL2_TID0                                      RT_BIT_64(15)
#define ARMV8_HCR_EL2_TID1                                      RT_BIT_64(16)
#define ARMV8_HCR_EL2_TID2                                      RT_BIT_64(17)
#define ARMV8_HCR_EL2_TID3                                      RT_BIT_64(18)
#define ARMV8_HCR_EL2_TSC                                       RT_BIT_64(19)
#define ARMV8_HCR_EL2_TIDCP                                     RT_BIT_64(20)
#define ARMV8_HCR_EL2_TACR                                      RT_BIT_64(21)
#define ARMV8_HCR_EL2_TSW                                       RT_BIT_64(22)
#define ARMV8_HCR_EL2_TDCP                                      RT_BIT_64(23)
#define ARMV8_HCR_EL2_TPU                                       RT_BIT_64(24)
#define ARMV8_HCR_EL2_TTLB                                      RT_BIT_64(25)
#define ARMV8_HCR_EL2_TVM                                       RT_BIT_64(26)
#define ARMV8_HCR_EL2_TGE                                       RT_BIT_64(27)
#define ARMV8_HCR_EL2_TDZ                                       RT_BIT_64(28)
#define ARMV8_HCR_EL2_HCD                                       RT_BIT_64(29)
#define ARMV8_HCR_EL2_TRVM                                      RT_BIT_64(30)
#define ARMV8_HCR_EL2_RW                                        RT_BIT_64(31)
#define ARMV8_HCR_EL2_CD                                        RT_BIT_64(32)
#define ARMV8_HCR_EL2_IC                                        RT_BIT_64(33)
#define ARMV8_HCR_EL2_E2H                                       RT_BIT_64(34)
#define ARMV8_HCR_EL2_TLOR                                      RT_BIT_64(35)
#define ARMV8_HCR_EL2_TERR                                      RT_BIT_64(36)
#define ARMV8_HCR_EL2_TEA                                       RT_BIT_64(37)
#define ARMV8_HCR_EL2_MIOCNCE                                   RT_BIT_64(38)
#define ARMV8_HCR_EL2_TME                                       RT_BIT_64(39)
#define ARMV8_HCR_EL2_APK                                       RT_BIT_64(40)
#define ARMV8_HCR_EL2_API                                       RT_BIT_64(41)
#define ARMV8_HCR_EL2_NV                                        RT_BIT_64(42)
#define ARMV8_HCR_EL2_NV1                                       RT_BIT_64(43)
#define ARMV8_HCR_EL2_AT                                        RT_BIT_64(44)
#define ARMV8_HCR_EL2_NV2                                       RT_BIT_64(45)
#define ARMV8_HCR_EL2_FWB                                       RT_BIT_64(46)
#define ARMV8_HCR_EL2_FIEN                                      RT_BIT_64(47)
#define ARMV8_HCR_EL2_GPF                                       RT_BIT_64(48)
#define ARMV8_HCR_EL2_TID4                                      RT_BIT_64(49)
#define ARMV8_HCR_EL2_TICAB                                     RT_BIT_64(50)
#define ARMV8_HCR_EL2_AMVOFFEN                                  RT_BIT_64(51)
#define ARMV8_HCR_EL2_TOCU                                      RT_BIT_64(52)
#define ARMV8_HCR_EL2_ENSCXT                                    RT_BIT_64(53)
#define ARMV8_HCR_EL2_TTLBIS                                    RT_BIT_64(54)
#define ARMV8_HCR_EL2_TTLBOS                                    RT_BIT_64(55)
#define ARMV8_HCR_EL2_ATA                                       RT_BIT_64(56)
#define ARMV8_HCR_EL2_DCT                                       RT_BIT_64(57)
#define ARMV8_HCR_EL2_TID5                                      RT_BIT_64(58)
#define ARMV8_HCR_EL2_TWEDEN                                    RT_BIT_64(59)
#define ARMV8_HCR_EL2_TWEDL_MASK                                UINT64_C(0xf000000000000000)
/** @} */


/** @name MDCR_EL2 - AArch64 Monitor Debug Configuration Register (EL2).
 * @{ */
#define ARMV8_MDCR_EL2_HPMN_MASK                                UINT64_C(0x1f)
#define ARMV8_MDCR_EL2_TPMCR                                    RT_BIT_64(5)
#define ARMV8_MDCR_EL2_TPM                                      RT_BIT_64(6)
#define ARMV8_MDCR_EL2_HPME                                     RT_BIT_64(7)
#define ARMV8_MDCR_EL2_TDE                                      RT_BIT_64(8)
#define ARMV8_MDCR_EL2_TDA                                      RT_BIT_64(9)
#define ARMV8_MDCR_EL2_TDOSA                                    RT_BIT_64(10)
#define ARMV8_MDCR_EL2_TDRA                                     RT_BIT_64(11)
#define ARMV8_MDCR_EL2_E2PB_MASK                                (RT_BIT_64(12) | RT_BIT_64(13))
#define ARMV8_MDCR_EL2_TPMS                                     RT_BIT_64(14)
#define ARMV8_MDCR_EL2_ENSPM                                    RT_BIT_64(15)
/*  Bit 16 - RES0 (2024-12) */
#define ARMV8_MDCR_EL2_HPMD                                     RT_BIT_64(17)
/*  Bit 18 - RES0 (2024-12) */
#define ARMV8_MDCR_EL2_TTRF                                     RT_BIT_64(19)
/*  Bits 22:20 - RES0 (2024-12) */
#define ARMV8_MDCR_EL2_HCCD                                     RT_BIT_64(23)
#define ARMV8_MDCR_EL2_E2TB_MASK                                (RT_BIT_64(24) | RT_BIT_64(25))
#define ARMV8_MDCR_EL2_HLP                                      RT_BIT_64(26)
#define ARMV8_MDCR_EL2_TDCC                                     RT_BIT_64(27)
#define ARMV8_MDCR_EL2_MTPME                                    RT_BIT_64(28)
#define ARMV8_MDCR_EL2_HPMFZO                                   RT_BIT_64(29)
#define ARMV8_MDCR_EL2_PMSSE_MASK                               (RT_BIT_64(30) | RT_BIT_64(31))
/*  Bits 35:32 - RES0 (2024-12) */
#define ARMV8_MDCR_EL2_HPMFZS                                   RT_BIT_64(36)
/*  Bits 39:37 - RES0 (2024-12) */
#define ARMV8_MDCR_EL2_PMEE_MASK                                (RT_BIT_64(40) | RT_BIT_64(41))
/*  Bit 42 - RES0 (2024-12) */
#define ARMV8_MDCR_EL2_EBWE                                     RT_BIT_64(43)
/*  Bits 49:44 - RES0 (2024-12) */
#define ARMV8_MDCR_EL2_ENSTEPOP                                 RT_BIT_64(50)
/*  Bits 63:51 - RES0 (2024-12) */
/** @} */


/** @defgroup grp_rt_armv8_vmsav864 VMSAv8-64 related definitions
 * @ingroup grp_rt_armv8
 * @{ */

#ifndef __ASSEMBLER__
/** A VMSAv8-64 descriptor. */
typedef uint64_t ARMV8VMSA64DESC;
/** Pointer to a VMSAv8-64 descriptor. */
typedef ARMV8VMSA64DESC *PARMV8VMSA64DESC;
/** Pointer to a const VMSAv8-64 descriptor. */
typedef const ARMV8VMSA64DESC *PCARMV8VMSA64DESC;
#endif


/** Bit 0 - Flag whether the table entry is valid. */
#define ARMV8_VMSA64_DESC_F_VALID                               RT_BIT_64(0)
#define ARMV8_VMSA64_DESC_F_VALID_BIT                           0
/** Bit 1 - Indicates the descriptor type depending on the current lookup level.
 * Basically when set it indicates to continue the lookup at the next level, or at the last level
 * that it is a page (not setting it at the last level is treated as an invalid descriptor).
 * If clear and not at the last lookup level the result will either be a large or gigantic page,
 * depending on the lookup level. */
#define ARMV8_VMSA64_DESC_F_TBL_OR_PG                           RT_BIT_64(1)
#define ARMV8_VMSA64_DESC_F_TBL_OR_PG_BIT                       1

/** @name Upper Attributes for block or page descriptors.
 * @{ */
/** Bit 54 - Execute never (XN) when only a single privilege level is supported by the translation regime. */
#define ARMV8_VMSA64_DESC_PG_OR_BLOCK_UATTR_1PRIV_XN_BIT            54
#define ARMV8_VMSA64_DESC_PG_OR_BLOCK_UATTR_1PRIV_XN                RT_BIT_64(ARMV8_VMSA64_DESC_PG_OR_BLOCK_UATTR_1PRIV_XN_BIT)
/** Bit 54 - Unprivileged execute never (UXN) when the translation regime supports two privilege levels. */
#define ARMV8_VMSA64_DESC_PG_OR_BLOCK_UATTR_2PRIV_UXN_BIT           54
#define ARMV8_VMSA64_DESC_PG_OR_BLOCK_UATTR_2PRIV_UXN               RT_BIT_64(ARMV8_VMSA64_DESC_PG_OR_BLOCK_UATTR_2PRIV_UXN_BIT)
/** Bit 54 - Privileged execute never (PXN) when the EL1&0 translation regime is active and HCR_EL2.{NV,NV1} is {1, 1}. */
#define ARMV8_VMSA64_DESC_PG_OR_BLOCK_UATTR_EL10_2PRIV_PXN_BIT      54
#define ARMV8_VMSA64_DESC_PG_OR_BLOCK_UATTR_EL10_2PRIV_PXN          RT_BIT_64(ARMV8_VMSA64_DESC_PG_OR_BLOCK_UATTR_EL10_2PRIV_PXN_BIT)
/** Bit 53 - Privileged execute neveer (PXN) when the translation regime supports two privilege levels. */
#define ARMV8_VMSA64_DESC_PG_OR_BLOCK_UATTR_2PRIV_PXN_BIT           53
#define ARMV8_VMSA64_DESC_PG_OR_BLOCK_UATTR_2PRIV_PXN               RT_BIT_64(ARMV8_VMSA64_DESC_PG_OR_BLOCK_UATTR_2PRIV_PXN_BIT)
/** @} */

/** @name Lower Attributes for block or page descriptors.
 * @{ */
/** Bit 10 - Access flag (AF). */
#define ARMV8_VMSA64_DESC_PG_OR_BLOCK_LATTR_AF_BIT                  10
#define ARMV8_VMSA64_DESC_PG_OR_BLOCK_LATTR_AF                      RT_BIT_64(ARMV8_VMSA64_DESC_PG_OR_BLOCK_LATTR_AF_BIT)
/** Bit 6 - 7 Access permissions (AP) (when indirect permissions are disables and stage1 translation supports two exception levels). */
#define ARMV8_VMSA64_DESC_PG_OR_BLOCK_LATTR_AP_MASK                 (RT_BIT_64(7) | RT_BIT_64(6))
#define ARMV8_VMSA64_DESC_PG_OR_BLOCK_LATTR_AP_SHIFT                6
/** Privileged Read and Write (PrivRead, PrivWrite). */
# define ARMV8_VMSA64_DESC_PG_OR_BLOCK_LATTR_AP_PRIV_RW             0
/** Unprivileged Read and Write (PrivRead, PrivWrite, UnprivRead, UnprivWrite). */
# define ARMV8_VMSA64_DESC_PG_OR_BLOCK_LATTR_AP_PRIV_RW_UNPRIV_RW   1
/** Privileged Read (PrivRead) */
# define ARMV8_VMSA64_DESC_PG_OR_BLOCK_LATTR_AP_PRIV_R              2
/** Privileged and Unprivileged Read (PrivRead, UnprivRead) */
# define ARMV8_VMSA64_DESC_PG_OR_BLOCK_LATTR_AP_PRIV_R_UNPRIV_R     3
/** @} */

/** @} */

#if (!defined(VBOX_FOR_DTRACE_LIB) && defined(__cplusplus) && !defined(ARMV8_WITHOUT_MK_INSTR)) || defined(DOXYGEN_RUNNING)
/** @defgroup grp_rt_armv8_mkinstr   Instruction Encoding Helpers
 * @ingroup grp_rt_armv8
 *
 * A few inlined functions and macros for assiting in encoding common ARMv8
 * instructions.
 *
 * @{ */

/** A64: Official NOP instruction. */
#define ARMV8_A64_INSTR_NOP         UINT32_C(0xd503201f)
/** A64: Return instruction. */
#define ARMV8_A64_INSTR_RET         UINT32_C(0xd65f03c0)
/** A64: Return instruction with LR pointer authentication using SP and key A. */
#define ARMV8_A64_INSTR_RETAA       UINT32_C(0xd65f0bff)
/** A64: Return instruction with LR pointer authentication using SP and key B. */
#define ARMV8_A64_INSTR_RETAB       UINT32_C(0xd65f0fff)
/** A64: Insert pointer authentication code into X17 using X16 and key B. */
#define ARMV8_A64_INSTR_PACIB1716   UINT32_C(0xd503215f)
/** A64: Insert pointer authentication code into LR using SP and key B. */
#define ARMV8_A64_INSTR_PACIBSP     UINT32_C(0xd503237f)
/** A64: Insert pointer authentication code into LR using XZR and key B. */
#define ARMV8_A64_INSTR_PACIBZ      UINT32_C(0xd503235f)
/** A64: Invert the carry flag (PSTATE.C). */
#define ARMV8_A64_INSTR_CFINV       UINT32_C(0xd500401f)


/** Memory barrier: Shareability domain. */
typedef enum
{
    kArm64InstMbReqDomain_OuterShareable = 0,
    kArm64InstMbReqDomain_Nonshareable,
    kArm64InstMbReqDomain_InnerShareable,
    kArm64InstMbReqDomain_FullSystem
} ARM64INSTRMBREQDOMAIN;

/** Memory barrier: Access type. */
typedef enum
{
    kArm64InstMbReqType_All0 = 0,   /**< Special. Only used with PSSBB and SSBB. */
    kArm64InstMbReqType_Reads,
    kArm64InstMbReqType_Writes,
    kArm64InstMbReqType_All
} ARM64INSTRMBREQTYPE;

/**
 * A64: DMB option
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrDmb(ARM64INSTRMBREQDOMAIN enmDomain = kArm64InstMbReqDomain_FullSystem,
                                               ARM64INSTRMBREQTYPE enmType = kArm64InstMbReqType_All)
{
    return UINT32_C(0xd50330bf)
         | ((uint32_t)enmDomain << 8)
         | ((uint32_t)enmType   << 10);
}


/**
 * A64: DSB option
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrDsb(ARM64INSTRMBREQDOMAIN enmDomain = kArm64InstMbReqDomain_FullSystem,
                                               ARM64INSTRMBREQTYPE enmType = kArm64InstMbReqType_All)
{
    return UINT32_C(0xd503309f)
         | ((uint32_t)enmDomain << 8)
         | ((uint32_t)enmType   << 10);
}


/**
 * A64: SSBB
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrSsbb(void)
{
    return Armv8A64MkInstrDsb(kArm64InstMbReqDomain_OuterShareable, kArm64InstMbReqType_All0);
}


/**
 * A64: PSSBB
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrPSsbb(void)
{
    return Armv8A64MkInstrDsb(kArm64InstMbReqDomain_Nonshareable, kArm64InstMbReqType_All0);
}


/**
 * A64: ISB option
 *
 * @note Only the default option selection is supported, all others are
 *       currently reserved.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrIsb(ARM64INSTRMBREQDOMAIN enmDomain = kArm64InstMbReqDomain_FullSystem,
                                               ARM64INSTRMBREQTYPE enmType = kArm64InstMbReqType_All)
{
    return UINT32_C(0xd50330df)
         | ((uint32_t)enmDomain << 8)
         | ((uint32_t)enmType   << 10);
}


typedef enum
{
    /** Add @a iImm7*sizeof(reg) to @a iBaseReg after the store/load,
     * and update the register. */
    kArm64InstrStLdPairType_PostIndex = 1,
    /** Add @a iImm7*sizeof(reg) to @a iBaseReg before the store/load,
     * but don't update the register. */
    kArm64InstrStLdPairType_Signed    = 2,
    /** Add @a iImm7*sizeof(reg) to @a iBaseReg before the store/load,
     * and update the register. */
    kArm64InstrStLdPairType_PreIndex  = 3
} ARM64INSTRSTLDPAIRTYPE;

/**
 * A64: Encodes either stp (store register pair) or ldp (load register pair).
 *
 * @returns The encoded instruction.
 * @param   fLoad       true for ldp, false of stp.
 * @param   u2Opc       When @a fSimdFp is @c false:
 *                          - 0 for 32-bit GPRs (Wt).
 *                          - 1 for encoding stgp or ldpsw.
 *                          - 2 for 64-bit GRPs (Xt).
 *                          - 3 illegal.
 *                      When @a fSimdFp is @c true:
 *                          - 0 for 32-bit SIMD&FP registers (St).
 *                          - 1 for 64-bit SIMD&FP registers (Dt).
 *                          - 2 for 128-bit SIMD&FP regsiters (Qt).
 * @param   enmType     The instruction variant wrt addressing and updating of the
 *                      addressing register.
 * @param   iReg1       The first register to store/load.
 * @param   iReg2       The second register to store/load.
 * @param   iBaseReg    The base register to use when addressing. SP is allowed.
 * @param   iImm7       Signed addressing immediate value scaled, range -64..63,
 *                      will be multiplied by the register size.
 * @param   fSimdFp     true for SIMD&FP registers, false for GPRs and
 *                      stgp/ldpsw instructions.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrStLdPair(bool fLoad, uint32_t u2Opc, ARM64INSTRSTLDPAIRTYPE enmType,
                                                    uint32_t iReg1, uint32_t iReg2, uint32_t iBaseReg, int32_t iImm7 = 0,
                                                    bool fSimdFp = false)
{
    Assert(u2Opc < 3); Assert(iReg1 <= 31); Assert(iReg2 <= 31); Assert(iBaseReg <= 31); Assert(iImm7 < 64 && iImm7 >= -64);
    return (u2Opc                              << 30)
         | UINT32_C(0x28000000) /* 0b101000000000000000000000000000 */
         | ((uint32_t)fSimdFp                  << 26) /* VR bit, see "Top-level encodings for A64" */
         | ((uint32_t)enmType                  << 23)
         | ((uint32_t)fLoad                    << 22)
         | (((uint32_t)iImm7 & UINT32_C(0x7f)) << 15)
         | (iReg2                              << 10)
         | (iBaseReg                           <<  5)
         | iReg1;
}


/** A64: ldp x1, x2, [x3]   */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrLdPairGpr(uint32_t iReg1, uint32_t iReg2, uint32_t iBaseReg, int32_t iImm7 = 0,
                                                     ARM64INSTRSTLDPAIRTYPE enmType = kArm64InstrStLdPairType_Signed,
                                                     bool f64Bit = true)
{
    return Armv8A64MkInstrStLdPair(true /*fLoad*/, f64Bit ? 2 : 0, enmType, iReg1, iReg2, iBaseReg, iImm7);
}


/** A64: stp x1, x2, [x3]   */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrStPairGpr(uint32_t iReg1, uint32_t iReg2, uint32_t iBaseReg, int32_t iImm7 = 0,
                                                     ARM64INSTRSTLDPAIRTYPE enmType = kArm64InstrStLdPairType_Signed,
                                                     bool f64Bit = true)
{
    return Armv8A64MkInstrStLdPair(false /*fLoad*/, f64Bit ? 2 : 0, enmType, iReg1, iReg2, iBaseReg, iImm7);
}


typedef enum                         /* Size VR Opc */
{                                    /*     \ | /   */
    kArmv8A64InstrLdStType_Mask_Size     = 0x300,
    kArmv8A64InstrLdStType_Mask_VR       = 0x010,
    kArmv8A64InstrLdStType_Mask_Opc      = 0x003,
    kArmv8A64InstrLdStType_Shift_Size    =     8,
    kArmv8A64InstrLdStType_Shift_VR      =     4,
    kArmv8A64InstrLdStType_Shift_Opc     =     0,

    kArmv8A64InstrLdStType_St_Byte       = 0x000,
    kArmv8A64InstrLdStType_Ld_Byte       = 0x001,
    kArmv8A64InstrLdStType_Ld_SignByte64 = 0x002,
    kArmv8A64InstrLdStType_Ld_SignByte32 = 0x003,

    kArmv8A64InstrLdStType_St_Half       = 0x100, /**< Half = 16-bit */
    kArmv8A64InstrLdStType_Ld_Half       = 0x101, /**< Half = 16-bit */
    kArmv8A64InstrLdStType_Ld_SignHalf64 = 0x102, /**< Half = 16-bit */
    kArmv8A64InstrLdStType_Ld_SignHalf32 = 0x103, /**< Half = 16-bit */

    kArmv8A64InstrLdStType_St_Word       = 0x200, /**< Word = 32-bit */
    kArmv8A64InstrLdStType_Ld_Word       = 0x201, /**< Word = 32-bit */
    kArmv8A64InstrLdStType_Ld_SignWord64 = 0x202, /**< Word = 32-bit */

    kArmv8A64InstrLdStType_St_Dword      = 0x300, /**< Dword = 64-bit */
    kArmv8A64InstrLdStType_Ld_Dword      = 0x301, /**< Dword = 64-bit */

    kArmv8A64InstrLdStType_Prefetch      = 0x302, /**< Not valid in all variations, check docs. */

    kArmv8A64InstrLdStType_St_Vr_Byte    = 0x010,
    kArmv8A64InstrLdStType_Ld_Vr_Byte    = 0x011,
    kArmv8A64InstrLdStType_St_Vr_128     = 0x012,
    kArmv8A64InstrLdStType_Ld_Vr_128     = 0x013,

    kArmv8A64InstrLdStType_St_Vr_Half    = 0x110, /**< Half = 16-bit */
    kArmv8A64InstrLdStType_Ld_Vr_Half    = 0x111, /**< Half = 16-bit */

    kArmv8A64InstrLdStType_St_Vr_Word    = 0x210, /**< Word = 32-bit */
    kArmv8A64InstrLdStType_Ld_Vr_Word    = 0x211, /**< Word = 32-bit */

    kArmv8A64InstrLdStType_St_Vr_Dword   = 0x310, /**< Dword = 64-bit */
    kArmv8A64InstrLdStType_Ld_Vr_Dword   = 0x311  /**< Dword = 64-bit */

} ARMV8A64INSTRLDSTTYPE;
/** Checks if a ARMV8A64INSTRLDSTTYPE value is a store operation or not. */
#define ARMV8A64INSTRLDSTTYPE_IS_STORE(a_enmLdStType) (((unsigned)a_enmLdStType & (unsigned)kArmv8A64InstrLdStType_Mask_Opc) == 0)


/**
 * A64: Encodes load/store with unscaled 9-bit signed immediate.
 *
 * @returns The encoded instruction.
 * @param   u32Opcode   The base opcode value.
 * @param   enmType     The load/store instruction type. Prefech valid (PRFUM)
 * @param   iReg        The register to load into / store.
 * @param   iBaseReg    The base register to use when addressing. SP is allowed.
 * @param   i9ImmDisp   The 9-bit signed addressing displacement. Unscaled.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrStLdImm9Ex(uint32_t u32Opcode, ARMV8A64INSTRLDSTTYPE enmType,
                                                      uint32_t iReg, uint32_t iBaseReg, int32_t i9ImmDisp = 0)
{
    Assert(i9ImmDisp >= -256 && i9ImmDisp < 256); Assert(iReg < 32); Assert(iBaseReg < 32);
    return u32Opcode
         | (((uint32_t)enmType & (uint32_t)kArmv8A64InstrLdStType_Mask_Size) << (30 - kArmv8A64InstrLdStType_Shift_Size))
         | (((uint32_t)enmType & (uint32_t)kArmv8A64InstrLdStType_Mask_VR)   << (26 - kArmv8A64InstrLdStType_Shift_VR))
         | (((uint32_t)enmType & (uint32_t)kArmv8A64InstrLdStType_Mask_Opc)  << (22 - kArmv8A64InstrLdStType_Shift_Opc))
         | (((uint32_t)i9ImmDisp & UINT32_C(0x1ff)) << 12)
         | (iBaseReg                                <<  5)
         | iReg;
}


/**
 * A64: Encodes load/store with unscaled 9-bit signed immediate.
 *
 * @returns The encoded instruction.
 * @param   enmType     The load/store instruction type. Prefech valid (PRFUM)
 * @param   iReg        The register to load into / store.
 * @param   iBaseReg    The base register to use when addressing. SP is allowed.
 * @param   i9ImmDisp   The 9-bit signed addressing displacement. Unscaled.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrSturLdur(ARMV8A64INSTRLDSTTYPE enmType,
                                                    uint32_t iReg, uint32_t iBaseReg, int32_t i9ImmDisp = 0)
{
                                                          /*    3         2         1         0 */
                                                          /*   10987654321098765432109876543210 */
    return Armv8A64MkInstrStLdImm9Ex(UINT32_C(0x38000000) /* 0b00111000000000000000000000000000 */,
                                     enmType, iReg, iBaseReg, i9ImmDisp);
}

/**
 * A64: Encodes load/store with unscaled 9-bit signed immediate, post-indexed.
 *
 * @returns The encoded instruction.
 * @param   enmType     The load/store instruction type. Prefech not valid.
 * @param   iReg        The register to load into / store.
 * @param   iBaseReg    The base register to use when addressing. SP is allowed.
 *                      Written back.
 * @param   i9ImmDisp   The 9-bit signed addressing displacement. Unscaled.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrStrLdrPostIndex9(ARMV8A64INSTRLDSTTYPE enmType,
                                                            uint32_t iReg, uint32_t iBaseReg, int32_t i9ImmDisp = 0)
{
    Assert(enmType != kArmv8A64InstrLdStType_Prefetch);   /*    3         2         1         0 */
                                                          /*   10987654321098765432109876543210 */
    return Armv8A64MkInstrStLdImm9Ex(UINT32_C(0x38000400) /* 0b00111000000000000000010000000000 */,
                                     enmType, iReg, iBaseReg, i9ImmDisp);
}

/**
 * A64: Encodes load/store with unscaled 9-bit signed immediate, pre-indexed
 *
 * @returns The encoded instruction.
 * @param   enmType     The load/store instruction type. Prefech valid (PRFUM)
 * @param   iReg        The register to load into / store.
 * @param   iBaseReg    The base register to use when addressing. SP is allowed.
 *                      Written back.
 * @param   i9ImmDisp   The 9-bit signed addressing displacement. Unscaled.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrStrLdrPreIndex9(ARMV8A64INSTRLDSTTYPE enmType,
                                                           uint32_t iReg, uint32_t iBaseReg, int32_t i9ImmDisp = 0)
{
    Assert(enmType != kArmv8A64InstrLdStType_Prefetch);   /*    3         2         1         0 */
                                                          /*   10987654321098765432109876543210 */
    return Armv8A64MkInstrStLdImm9Ex(UINT32_C(0x38000c00) /* 0b00111000000000000000110000000000 */,
                                     enmType, iReg, iBaseReg, i9ImmDisp);
}

/**
 * A64: Encodes unprivileged load/store with unscaled 9-bit signed immediate.
 *
 * @returns The encoded instruction.
 * @param   enmType     The load/store instruction type. Prefech not valid,
 *                      nor any SIMD&FP variants.
 * @param   iReg        The register to load into / store.
 * @param   iBaseReg    The base register to use when addressing. SP is allowed.
 * @param   i9ImmDisp   The 9-bit signed addressing displacement. Unscaled.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrSttrLdtr(ARMV8A64INSTRLDSTTYPE enmType,
                                                    uint32_t iReg, uint32_t iBaseReg, int32_t i9ImmDisp = 0)
{
    Assert(enmType != kArmv8A64InstrLdStType_Prefetch);
    Assert(!((uint32_t)enmType & (uint32_t)kArmv8A64InstrLdStType_Mask_VR));
                                                          /*    3         2         1         0 */
                                                          /*   10987654321098765432109876543210 */
    return Armv8A64MkInstrStLdImm9Ex(UINT32_C(0x38000800) /* 0b00111000000000000000100000000000 */,
                                     enmType, iReg, iBaseReg, i9ImmDisp);
}


/**
 * A64: Encodes load/store w/ scaled 12-bit unsigned address displacement.
 *
 * @returns The encoded instruction.
 * @param   enmType     The load/store instruction type. Prefech not valid,
 *                      nor any SIMD&FP variants.
 * @param   iReg        The register to load into / store.
 * @param   iBaseReg    The base register to use when addressing. SP is allowed.
 * @param   u12ImmDisp  Addressing displacement, scaled by size.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrStLdRUOff(ARMV8A64INSTRLDSTTYPE enmType,
                                                     uint32_t iReg, uint32_t iBaseReg, uint32_t u12ImmDisp)
{
    Assert(u12ImmDisp < 4096U);
    Assert(iReg < 32);          /*    3         2         1         0 */
    Assert(iBaseReg < 32);      /*   10987654321098765432109876543210 */
    return UINT32_C(0x39000000) /* 0b00111001000000000000000000000000 */
         | (((uint32_t)enmType & (uint32_t)kArmv8A64InstrLdStType_Mask_Size) << (30 - kArmv8A64InstrLdStType_Shift_Size))
         | (((uint32_t)enmType & (uint32_t)kArmv8A64InstrLdStType_Mask_VR)   << (26 - kArmv8A64InstrLdStType_Shift_VR))
         | (((uint32_t)enmType & (uint32_t)kArmv8A64InstrLdStType_Mask_Opc)  << (22 - kArmv8A64InstrLdStType_Shift_Opc))
         | (u12ImmDisp       << 10)
         | (iBaseReg         <<  5)
         | iReg;
}

typedef enum
{
    kArmv8A64InstrLdStExtend_Uxtw   = 2, /**< Zero-extend (32-bit) word. */
    kArmv8A64InstrLdStExtend_Lsl    = 3, /**< Shift left (64-bit). */
    kArmv8A64InstrLdStExtend_Sxtw   = 6, /**< Sign-extend (32-bit) word. */
    kArmv8A64InstrLdStExtend_Sxtx   = 7  /**< Sign-extend (64-bit) dword (to 128-bit SIMD&FP reg, presumably). */
} ARMV8A64INSTRLDSTEXTEND;

/**
 * A64: Encodes load/store w/ index register.
 *
 * @returns The encoded instruction.
 * @param   enmType     The load/store instruction type.
 * @param   iReg        The register to load into / store.
 * @param   iBaseReg    The base register to use when addressing. SP is allowed.
 * @param   iRegIndex   The index register.
 * @param   enmExtend   The extending to apply to @a iRegIndex.
 * @param   fShifted    Whether to shift the index. The shift amount corresponds
 *                      to the access size (thus irrelevant for byte accesses).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrStLdRegIdx(ARMV8A64INSTRLDSTTYPE enmType,
                                                      uint32_t iReg, uint32_t iBaseReg, uint32_t iRegIndex,
                                                      ARMV8A64INSTRLDSTEXTEND enmExtend = kArmv8A64InstrLdStExtend_Lsl,
                                                      bool fShifted = false)
{
    Assert(iRegIndex < 32);
    Assert(iReg < 32);          /*    3         2         1         0 */
    Assert(iBaseReg < 32);      /*   10987654321098765432109876543210 */
    return UINT32_C(0x38200800) /* 0b00111000001000000000100000000000 */
         | (((uint32_t)enmType & (uint32_t)kArmv8A64InstrLdStType_Mask_Size) << (30 - kArmv8A64InstrLdStType_Shift_Size))
         | (((uint32_t)enmType & (uint32_t)kArmv8A64InstrLdStType_Mask_VR)   << (26 - kArmv8A64InstrLdStType_Shift_VR))
         | (((uint32_t)enmType & (uint32_t)kArmv8A64InstrLdStType_Mask_Opc)  << (22 - kArmv8A64InstrLdStType_Shift_Opc))
         | (iRegIndex            << 16)
         | ((uint32_t)enmExtend  << 13)
         | ((uint32_t)fShifted   << 12)
         | (iBaseReg             <<  5)
         | iReg;
}

typedef enum                            /* VR  Opc */
{                                       /*   \ |   */
    kArmv8A64InstrLdrLitteral_Mask_Vr     = 0x10,
    kArmv8A64InstrLdrLitteral_Mask_Opc    = 0x03,
    kArmv8A64InstrLdrLitteral_Shift_Vr    = 4,
    kArmv8A64InstrLdrLitteral_Shift_Opc   = 0,

    kArmv8A64InstrLdrLitteral_Word        = 0x00,   /**< word = 32-bit */
    kArmv8A64InstrLdrLitteral_Dword       = 0x01,   /**< dword = 64-bit */
    kArmv8A64InstrLdrLitteral_SignWord64  = 0x02,   /**< Loads word, signextending it to 64-bit  */
    kArmv8A64InstrLdrLitteral_Prefetch    = 0x03,   /**< prfm */

    kArmv8A64InstrLdrLitteral_Vr_Word     = 0x10,   /**< word = 32-bit */
    kArmv8A64InstrLdrLitteral_Vr_Dword    = 0x11,   /**< dword = 64-bit */
    kArmv8A64InstrLdrLitteral_Vr_128      = 0x12
} ARMV8A64INSTRLDRLITTERAL;


/**
 * A64: Encodes load w/ a PC relative 19-bit signed immediate.
 *
 * @returns The encoded instruction.
 * @param   enmType     The load instruction type.
 * @param   iReg        The register to load into.
 * @param   i19Imm      The signed immediate value, multiplied by 4 regardless
 *                      of access size.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrLdrLitteral(ARMV8A64INSTRLDRLITTERAL enmType, uint32_t iReg, int32_t i19Imm)
{
    Assert(i19Imm >= -262144 && i19Imm < 262144);
    Assert(iReg < 32);          /*    3         2         1         0 */
                                /*   10987654321098765432109876543210 */
    return UINT32_C(0x30000000) /* 0b00110000000000000000000000000000 */
         | (((uint32_t)enmType & (uint32_t)kArmv8A64InstrLdrLitteral_Mask_Vr)  << (26 - kArmv8A64InstrLdrLitteral_Shift_Vr))
         | (((uint32_t)enmType & (uint32_t)kArmv8A64InstrLdrLitteral_Mask_Opc) << (30 - kArmv8A64InstrLdrLitteral_Shift_Opc))
         | (((uint32_t)i19Imm & UINT32_C(0x00ffffe0)) << 5)
         | iReg;
}


typedef enum
{
    kArmv8A64InstrMovWide_Not  = 0,     /**< MOVN - reg = ~(imm16 << hw*16; */
    kArmv8A64InstrMovWide_Zero = 2,     /**< MOVZ - reg =   imm16 << hw*16; */
    kArmv8A64InstrMovWide_Keep = 3      /**< MOVK - keep the other halfwords. */
} ARMV8A64INSTRMOVWIDE;

/**
 * A64: Encode a move wide immediate instruction.
 *
 * @returns The encoded instruction.
 * @param   enmType     The load instruction type.
 * @param   iRegDst     The register to mov the immediate into.
 * @param   uImm16      The immediate value.
 * @param   iHalfWord   Which of the 4 (@a f64Bit = true) or 2 register (16-bit)
 *                      half-words to target:
 *                          - 0 for bits 15:00,
 *                          - 1 for bits 31:16,
 *                          - 2 for bits 47:32 (f64Bit=true only),
 *                          - 3 for bits 63:48 (f64Bit=true only).
 * @param   f64Bit      true for 64-bit GPRs (default), @c false for 32-bit GPRs.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrMovWide(ARMV8A64INSTRMOVWIDE enmType, uint32_t iRegDst, uint32_t uImm16,
                                                   uint32_t iHalfWord = 0, bool f64Bit = true)
{
    Assert(iRegDst < 32U); Assert(uImm16 <= (uint32_t)UINT16_MAX); Assert(iHalfWord < 2U + (2U * f64Bit));
    return ((uint32_t)f64Bit    << 31)
         | ((uint32_t)enmType   << 29)
         | UINT32_C(0x12800000)
         | (iHalfWord           << 21)
         | (uImm16              << 5)
         | iRegDst;
}

/** A64: Encodes a MOVN instruction.
 * @see Armv8A64MkInstrMovWide for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrMovN(uint32_t iRegDst, uint32_t uImm16, uint32_t iHalfWord = 0, bool f64Bit = true)
{
    return Armv8A64MkInstrMovWide(kArmv8A64InstrMovWide_Not, iRegDst, uImm16, iHalfWord, f64Bit);
}

/** A64: Encodes a MOVZ instruction.
 * @see Armv8A64MkInstrMovWide for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrMovZ(uint32_t iRegDst, uint32_t uImm16, uint32_t iHalfWord = 0, bool f64Bit = true)
{
    return Armv8A64MkInstrMovWide(kArmv8A64InstrMovWide_Zero, iRegDst, uImm16, iHalfWord, f64Bit);
}

/** A64: Encodes a MOVK instruction.
 * @see Armv8A64MkInstrMovWide for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrMovK(uint32_t iRegDst, uint32_t uImm16, uint32_t iHalfWord = 0, bool f64Bit = true)
{
    return Armv8A64MkInstrMovWide(kArmv8A64InstrMovWide_Keep, iRegDst, uImm16, iHalfWord, f64Bit);
}


typedef enum
{
    kArmv8A64InstrShift_Lsl = 0,
    kArmv8A64InstrShift_Lsr,
    kArmv8A64InstrShift_Asr,
    kArmv8A64InstrShift_Ror
} ARMV8A64INSTRSHIFT;


/**
 * A64: Encodes a logical instruction with a shifted 2nd register operand.
 *
 * @returns The encoded instruction.
 * @param   u2Opc           The logical operation to perform.
 * @param   fNot            Whether to complement the 2nd operand.
 * @param   iRegResult      The output register.
 * @param   iReg1           The 1st register operand.
 * @param   iReg2Shifted    The 2nd register operand, to which the optional
 *                          shifting is applied.
 * @param   f64Bit          true for 64-bit GPRs (default), @c false for 32-bit
 *                          GPRs.
 * @param   offShift6       The shift amount (default: none).
 * @param   enmShift        The shift operation (default: LSL).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrLogicalShiftedReg(uint32_t u2Opc, bool fNot,
                                                             uint32_t iRegResult, uint32_t iReg1, uint32_t iReg2Shifted,
                                                             bool f64Bit, uint32_t offShift6, ARMV8A64INSTRSHIFT enmShift)
{
    Assert(u2Opc < 4); Assert(offShift6 < (f64Bit ? UINT32_C(64) : UINT32_C(32)));
    Assert(iRegResult < 32); Assert(iReg1 < 32); Assert(iReg2Shifted < 32);
    return ((uint32_t)f64Bit << 31)
         | (u2Opc << 29)
         | UINT32_C(0x0a000000)
         | ((uint32_t)enmShift << 22)
         | ((uint32_t)fNot     << 21)
         | (iReg2Shifted       << 16)
         | (offShift6          << 10)
         | (iReg1              <<  5)
         | iRegResult;
}


/** A64: Encodes an AND instruction.
 * @see Armv8A64MkInstrLogicalShiftedReg for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrAnd(uint32_t iRegResult, uint32_t iReg1, uint32_t iReg2Shifted, bool f64Bit = true,
                                               uint32_t offShift6 = 0, ARMV8A64INSTRSHIFT enmShift = kArmv8A64InstrShift_Lsl)
{
    return Armv8A64MkInstrLogicalShiftedReg(0, false /*fNot*/, iRegResult, iReg1, iReg2Shifted, f64Bit, offShift6, enmShift);
}


/** A64: Encodes an BIC instruction.
 * @see Armv8A64MkInstrLogicalShiftedReg for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrBic(uint32_t iRegResult, uint32_t iReg1, uint32_t iReg2Shifted, bool f64Bit = true,
                                               uint32_t offShift6 = 0, ARMV8A64INSTRSHIFT enmShift = kArmv8A64InstrShift_Lsl)
{
    return Armv8A64MkInstrLogicalShiftedReg(0, true /*fNot*/, iRegResult, iReg1, iReg2Shifted, f64Bit, offShift6, enmShift);
}


/** A64: Encodes an ORR instruction.
 * @see Armv8A64MkInstrLogicalShiftedReg for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrOrr(uint32_t iRegResult, uint32_t iReg1, uint32_t iReg2Shifted, bool f64Bit = true,
                                               uint32_t offShift6 = 0, ARMV8A64INSTRSHIFT enmShift = kArmv8A64InstrShift_Lsl)
{
    return Armv8A64MkInstrLogicalShiftedReg(1, false /*fNot*/, iRegResult, iReg1, iReg2Shifted, f64Bit, offShift6, enmShift);
}


/** A64: Encodes an MOV instruction.
 * This is an alias for "orr dst, xzr, src".  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrMov(uint32_t iRegResult, uint32_t idxRegSrc, bool f64Bit = true)
{
    return Armv8A64MkInstrOrr(iRegResult, ARMV8_A64_REG_XZR, idxRegSrc, f64Bit);
}


/** A64: Encodes an ORN instruction.
 * @see Armv8A64MkInstrLogicalShiftedReg for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrOrn(uint32_t iRegResult, uint32_t iReg1, uint32_t iReg2Shifted, bool f64Bit = true,
                                               uint32_t offShift6 = 0, ARMV8A64INSTRSHIFT enmShift = kArmv8A64InstrShift_Lsl)
{
    return Armv8A64MkInstrLogicalShiftedReg(1, true /*fNot*/, iRegResult, iReg1, iReg2Shifted, f64Bit, offShift6, enmShift);
}


/** A64: Encodes an EOR instruction.
 * @see Armv8A64MkInstrLogicalShiftedReg for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrEor(uint32_t iRegResult, uint32_t iReg1, uint32_t iReg2Shifted, bool f64Bit = true,
                                               uint32_t offShift6 = 0, ARMV8A64INSTRSHIFT enmShift = kArmv8A64InstrShift_Lsl)
{
    return Armv8A64MkInstrLogicalShiftedReg(2, false /*fNot*/, iRegResult, iReg1, iReg2Shifted, f64Bit, offShift6, enmShift);
}


/** A64: Encodes an EON instruction.
 * @see Armv8A64MkInstrLogicalShiftedReg for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrEon(uint32_t iRegResult, uint32_t iReg1, uint32_t iReg2Shifted, bool f64Bit = true,
                                               uint32_t offShift6 = 0, ARMV8A64INSTRSHIFT enmShift = kArmv8A64InstrShift_Lsl)
{
    return Armv8A64MkInstrLogicalShiftedReg(2, true /*fNot*/, iRegResult, iReg1, iReg2Shifted, f64Bit, offShift6, enmShift);
}


/** A64: Encodes an ANDS instruction.
 * @see Armv8A64MkInstrLogicalShiftedReg for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrAnds(uint32_t iRegResult, uint32_t iReg1, uint32_t iReg2Shifted, bool f64Bit = true,
                                                uint32_t offShift6 = 0, ARMV8A64INSTRSHIFT enmShift = kArmv8A64InstrShift_Lsl)
{
    return Armv8A64MkInstrLogicalShiftedReg(3, false /*fNot*/, iRegResult, iReg1, iReg2Shifted, f64Bit, offShift6, enmShift);
}


/** A64: Encodes an BICS instruction.
 * @see Armv8A64MkInstrLogicalShiftedReg for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrBics(uint32_t iRegResult, uint32_t iReg1, uint32_t iReg2Shifted, bool f64Bit = true,
                                                uint32_t offShift6 = 0, ARMV8A64INSTRSHIFT enmShift = kArmv8A64InstrShift_Lsl)
{
    return Armv8A64MkInstrLogicalShiftedReg(3, true /*fNot*/, iRegResult, iReg1, iReg2Shifted, f64Bit, offShift6, enmShift);
}



/*
 * Data processing instructions with two source register operands.
 */


/** A64: Encodes an SUBP instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrSubP(uint32_t iRegResult, uint32_t iRegMinuend, uint32_t iRegSubtrahend)
{
    Assert(iRegResult < 32);  Assert(iRegMinuend < 32); Assert(iRegSubtrahend < 32);
    return UINT32_C(0x80000000)
         | UINT32_C(0x1ac00000)
         | (UINT32_C(0)      << 10)
         | (iRegSubtrahend   << 16)
         | (iRegMinuend      << 5)
         | iRegResult;
}


/** A64: Encodes an SUBPS instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrSubPS(uint32_t iRegResult, uint32_t iRegMinuend, uint32_t iRegSubtrahend)
{
    Assert(iRegResult < 32);  Assert(iRegMinuend < 32); Assert(iRegSubtrahend < 32);
    return UINT32_C(0x80000000)
         | UINT32_C(0x20000000)
         | UINT32_C(0x1ac00000)
         | (UINT32_C(0)      << 10)
         | (iRegSubtrahend   << 16)
         | (iRegMinuend      << 5)
         | iRegResult;
}


/** A64: Encodes an UDIV instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrUDiv(uint32_t iRegResult, uint32_t iRegDividend, uint32_t iRegDivisor, bool f64Bit = true)
{
    Assert(iRegResult < 32);  Assert(iRegDividend < 32); Assert(iRegDivisor < 32);
    return ((uint32_t)f64Bit << 31)
         | UINT32_C(0x1ac00000)
         | (UINT32_C(2)      << 10)
         | (iRegDivisor      << 16)
         | (iRegDividend     << 5)
         | iRegResult;
}


/** A64: Encodes an SDIV instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrSDiv(uint32_t iRegResult, uint32_t iRegDividend, uint32_t iRegDivisor, bool f64Bit = true)
{
    Assert(iRegResult < 32);  Assert(iRegDividend < 32); Assert(iRegDivisor < 32);
    return ((uint32_t)f64Bit << 31)
         | UINT32_C(0x1ac00000)
         | (UINT32_C(3)      << 10)
         | (iRegDivisor      << 16)
         | (iRegDividend     << 5)
         | iRegResult;
}


/** A64: Encodes an IRG instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrIrg(uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2)
{
    Assert(iRegResult < 32);  Assert(iRegSrc1 < 32); Assert(iRegSrc2 < 32);
    return UINT32_C(0x80000000)
         | UINT32_C(0x1ac00000)
         | (UINT32_C(4)      << 10)
         | (iRegSrc2         << 16)
         | (iRegSrc1         << 5)
         | iRegResult;
}


/** A64: Encodes a GMI instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrGmi(uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2)
{
    Assert(iRegResult < 32);  Assert(iRegSrc1 < 32); Assert(iRegSrc2 < 32);
    return UINT32_C(0x80000000)
         | UINT32_C(0x1ac00000)
         | (UINT32_C(5)      << 10)
         | (iRegSrc2         << 16)
         | (iRegSrc1         << 5)
         | iRegResult;
}


/** A64: Encodes an LSLV instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrLslv(uint32_t iRegResult, uint32_t iRegSrc, uint32_t iRegCount, bool f64Bit = true)
{
    Assert(iRegResult < 32);  Assert(iRegSrc < 32); Assert(iRegCount < 32);
    return ((uint32_t)f64Bit << 31)
         | UINT32_C(0x1ac00000)
         | (UINT32_C(8)      << 10)
         | (iRegCount        << 16)
         | (iRegSrc          << 5)
         | iRegResult;
}


/** A64: Encodes an LSRV instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrLsrv(uint32_t iRegResult, uint32_t iRegSrc, uint32_t iRegCount, bool f64Bit = true)
{
    Assert(iRegResult < 32);  Assert(iRegSrc < 32); Assert(iRegCount < 32);
    return ((uint32_t)f64Bit << 31)
         | UINT32_C(0x1ac00000)
         | (UINT32_C(9)      << 10)
         | (iRegCount        << 16)
         | (iRegSrc          << 5)
         | iRegResult;
}


/** A64: Encodes an ASRV instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrAsrv(uint32_t iRegResult, uint32_t iRegSrc, uint32_t iRegCount, bool f64Bit = true)
{
    Assert(iRegResult < 32);  Assert(iRegSrc < 32); Assert(iRegCount < 32);
    return ((uint32_t)f64Bit << 31)
         | UINT32_C(0x1ac00000)
         | (UINT32_C(10)     << 10)
         | (iRegCount        << 16)
         | (iRegSrc          << 5)
         | iRegResult;
}


/** A64: Encodes a RORV instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrRorv(uint32_t iRegResult, uint32_t iRegSrc, uint32_t iRegCount, bool f64Bit = true)
{
    Assert(iRegResult < 32);  Assert(iRegSrc < 32); Assert(iRegCount < 32);
    return ((uint32_t)f64Bit << 31)
         | UINT32_C(0x1ac00000)
         | (UINT32_C(11)     << 10)
         | (iRegCount        << 16)
         | (iRegSrc          << 5)
         | iRegResult;
}


/** A64: Encodes a PACGA instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrPacga(uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2)
{
    Assert(iRegResult < 32);  Assert(iRegSrc1 < 32); Assert(iRegSrc2 < 32);
    return UINT32_C(0x80000000)
         | UINT32_C(0x1ac00000)
         | (UINT32_C(12)     << 10)
         | (iRegSrc2         << 16)
         | (iRegSrc1         << 5)
         | iRegResult;
}


/** A64: Encodes a CRC32* instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCrc32(uint32_t iRegResult, uint32_t iRegCrc, uint32_t iRegValue, uint32_t uSize)
{
    Assert(iRegResult < 32);  Assert(iRegCrc < 32); Assert(iRegValue < 32); Assert(uSize < 4);
    return ((uint32_t)(uSize == 3) << 31)
         | UINT32_C(0x1ac00000)
         | (UINT32_C(16)           << 10)
         | (uSize                  << 10)
         | (iRegValue              << 16)
         | (iRegCrc                << 5)
         | iRegResult;
}


/** A64: Encodes a CRC32B instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCrc32B(uint32_t iRegResult, uint32_t iRegCrc, uint32_t iRegValue)
{
    return Armv8A64MkInstrCrc32(iRegResult, iRegCrc, iRegValue, 0);
}


/** A64: Encodes a CRC32H instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCrc32H(uint32_t iRegResult, uint32_t iRegCrc, uint32_t iRegValue)
{
    return Armv8A64MkInstrCrc32(iRegResult, iRegCrc, iRegValue, 1);
}


/** A64: Encodes a CRC32W instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCrc32W(uint32_t iRegResult, uint32_t iRegCrc, uint32_t iRegValue)
{
    return Armv8A64MkInstrCrc32(iRegResult, iRegCrc, iRegValue, 2);
}


/** A64: Encodes a CRC32X instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCrc32X(uint32_t iRegResult, uint32_t iRegCrc, uint32_t iRegValue)
{
    return Armv8A64MkInstrCrc32(iRegResult, iRegCrc, iRegValue, 3);
}


/** A64: Encodes a CRC32C* instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCrc32c(uint32_t iRegResult, uint32_t iRegCrc, uint32_t iRegValue, uint32_t uSize)
{
    Assert(iRegResult < 32);  Assert(iRegCrc < 32); Assert(iRegValue < 32); Assert(uSize < 4);
    return ((uint32_t)(uSize == 3) << 31)
         | UINT32_C(0x1ac00000)
         | (UINT32_C(20)           << 10)
         | (uSize                  << 10)
         | (iRegValue              << 16)
         | (iRegCrc                << 5)
         | iRegResult;
}


/** A64: Encodes a CRC32B instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCrc32cB(uint32_t iRegResult, uint32_t iRegCrc, uint32_t iRegValue)
{
    return Armv8A64MkInstrCrc32c(iRegResult, iRegCrc, iRegValue, 0);
}


/** A64: Encodes a CRC32CH instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCrc32cH(uint32_t iRegResult, uint32_t iRegCrc, uint32_t iRegValue)
{
    return Armv8A64MkInstrCrc32c(iRegResult, iRegCrc, iRegValue, 1);
}


/** A64: Encodes a CRC32CW instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCrc32cW(uint32_t iRegResult, uint32_t iRegCrc, uint32_t iRegValue)
{
    return Armv8A64MkInstrCrc32c(iRegResult, iRegCrc, iRegValue, 2);
}


/** A64: Encodes a CRC32CX instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCrc32cX(uint32_t iRegResult, uint32_t iRegCrc, uint32_t iRegValue)
{
    return Armv8A64MkInstrCrc32c(iRegResult, iRegCrc, iRegValue, 3);
}


/** A64: Encodes an SMAX instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrSMax(uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2, bool f64Bit = true)
{
    Assert(iRegResult < 32);  Assert(iRegSrc1 < 32); Assert(iRegSrc2 < 32);
    return ((uint32_t)f64Bit << 31)
         | UINT32_C(0x1ac00000)
         | (UINT32_C(24)     << 10)
         | (iRegSrc2         << 16)
         | (iRegSrc1         << 5)
         | iRegResult;
}


/** A64: Encodes an UMAX instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrUMax(uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2, bool f64Bit = true)
{
    Assert(iRegResult < 32);  Assert(iRegSrc1 < 32); Assert(iRegSrc2 < 32);
    return ((uint32_t)f64Bit << 31)
         | UINT32_C(0x1ac00000)
         | (UINT32_C(25)     << 10)
         | (iRegSrc2         << 16)
         | (iRegSrc1         << 5)
         | iRegResult;
}


/** A64: Encodes an SMIN instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrSMin(uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2, bool f64Bit = true)
{
    Assert(iRegResult < 32);  Assert(iRegSrc1 < 32); Assert(iRegSrc2 < 32);
    return ((uint32_t)f64Bit << 31)
         | UINT32_C(0x1ac00000)
         | (UINT32_C(26)     << 10)
         | (iRegSrc2         << 16)
         | (iRegSrc1         << 5)
         | iRegResult;
}


/** A64: Encodes an UMIN instruction. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrUMin(uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2, bool f64Bit = true)
{
    Assert(iRegResult < 32);  Assert(iRegSrc1 < 32); Assert(iRegSrc2 < 32);
    return ((uint32_t)f64Bit << 31)
         | UINT32_C(0x1ac00000)
         | (UINT32_C(27)     << 10)
         | (iRegSrc2         << 16)
         | (iRegSrc1         << 5)
         | iRegResult;
}


# ifdef IPRT_INCLUDED_asm_h /* don't want this to be automatically included here. */

/**
 * Converts immS and immR values (to logical instructions) to a 32-bit mask.
 *
 * @returns The decoded mask.
 * @param   uImm6SizeLen    The immS value from the instruction.  (No N part
 *                          here, as that must be zero for instructions
 *                          operating on 32-bit wide registers.)
 * @param   uImm6Rotations  The immR value from the instruction.
 */
DECLINLINE(uint32_t) Armv8A64ConvertImmRImmS2Mask32(uint32_t uImm6SizeLen, uint32_t uImm6Rotations)
{
    Assert(uImm6SizeLen < 64); Assert(uImm6Rotations < 64);

    /* Determine the element size. */
    unsigned const cBitsElementLog2 = ASMBitLastSetU32(uImm6SizeLen ^ 0x3f) - 1U;
    Assert(cBitsElementLog2 + 1U != 0U);

    unsigned const cBitsElement = RT_BIT_32(cBitsElementLog2);
    Assert(uImm6Rotations < cBitsElement);

    /* Extract the number of bits set to 1: */
    unsigned const cBitsSetTo1  = (uImm6SizeLen & (cBitsElement - 1U)) + 1;
    Assert(cBitsSetTo1 < cBitsElement);
    uint32_t const uElement     = RT_BIT_32(cBitsSetTo1) - 1U;

    /* Produce the unrotated pattern. */
    static const uint32_t s_auReplicate[]
        = { UINT32_MAX, UINT32_MAX / 3, UINT32_MAX / 15, UINT32_MAX / 255, UINT32_MAX / 65535, 1 };
    uint32_t const uPattern     = s_auReplicate[cBitsElementLog2] * uElement;

    /* Rotate it and return. */
    return ASMRotateRightU32(uPattern, uImm6Rotations & (cBitsElement - 1U));
}


/**
 * Converts N+immS and immR values (to logical instructions) to a 64-bit mask.
 *
 * @returns The decoded mask.
 * @param   uImm7SizeLen    The N:immS value from the instruction.
 * @param   uImm6Rotations  The immR value from the instruction.
 */
DECLINLINE(uint64_t) Armv8A64ConvertImmRImmS2Mask64(uint32_t uImm7SizeLen, uint32_t uImm6Rotations)
{
    Assert(uImm7SizeLen < 128); Assert(uImm6Rotations < 64);

    /* Determine the element size. */
    unsigned const cBitsElementLog2 = ASMBitLastSetU32(uImm7SizeLen ^ 0x3f) - 1U;
    Assert(cBitsElementLog2 + 1U != 0U);

    unsigned const cBitsElement = RT_BIT_32(cBitsElementLog2);
    Assert(uImm6Rotations < cBitsElement);

    /* Extract the number of bits set to 1: */
    unsigned const cBitsSetTo1  = (uImm7SizeLen & (cBitsElement - 1U)) + 1;
    Assert(cBitsSetTo1 < cBitsElement);
    uint64_t const uElement     = RT_BIT_64(cBitsSetTo1) - 1U;

    /* Produce the unrotated pattern. */
    static const uint64_t s_auReplicate[]
        = { UINT64_MAX, UINT64_MAX / 3, UINT64_MAX / 15, UINT64_MAX / 255, UINT64_MAX / 65535, UINT64_MAX / UINT32_MAX, 1 };
    uint64_t const uPattern     = s_auReplicate[cBitsElementLog2] * uElement;

    /* Rotate it and return. */
    return ASMRotateRightU64(uPattern, uImm6Rotations & (cBitsElement - 1U));
}


/**
 * Variant of Armv8A64ConvertImmRImmS2Mask64 where the N bit is separate from
 * the immS value.
 */
DECLINLINE(uint64_t) Armv8A64ConvertImmRImmS2Mask64(uint32_t uN, uint32_t uImm6SizeLen, uint32_t uImm6Rotations)
{
    return Armv8A64ConvertImmRImmS2Mask64((uN << 6) | uImm6SizeLen, uImm6Rotations);
}


/**
 * Helper for Armv8A64MkInstrLogicalImm and friends that tries to convert a
 * 32-bit bitmask to a set of immediates for those instructions.
 *
 * @returns true if successful, false if not.
 * @param   fMask           The mask value to convert.
 * @param   puImm6SizeLen   Where to return the immS part (N is always zero for
 *                          32-bit wide masks).
 * @param   puImm6Rotations Where to return the immR.
 */
DECLINLINE(bool) Armv8A64ConvertMask32ToImmRImmS(uint32_t fMask, uint32_t *puImm6SizeLen, uint32_t *puImm6Rotations)
{
    /* Fend off 0 and UINT32_MAX as these cannot be represented. */
    if ((uint32_t)(fMask + 1U) <= 1)
        return false;

    /* Rotate the value will we get all 1s at the bottom and the zeros at the top. */
    unsigned const cRor = ASMCountTrailingZerosU32(fMask);
    unsigned const cRol = ASMCountLeadingZerosU32(~fMask);
    if (cRor)
        fMask = ASMRotateRightU32(fMask, cRor);
    else
        fMask = ASMRotateLeftU32(fMask, cRol);
    Assert(fMask & RT_BIT_32(0));
    Assert(!(fMask & RT_BIT_32(31)));

    /* Count the trailing ones and leading zeros. */
    unsigned const cOnes  = ASMCountTrailingZerosU32(~fMask);
    unsigned const cZeros = ASMCountLeadingZerosU32(fMask);

    /* The potential element length is then the sum of the two above. */
    unsigned const cBitsElement = cOnes + cZeros;
    if (!RT_IS_POWER_OF_TWO(cBitsElement) || cBitsElement < 2)
        return false;

    /* Special case: 32 bits element size. Since we're done here. */
    if (cBitsElement == 32)
        *puImm6SizeLen = cOnes - 1;
    else
    {
        /* Extract the element bits and check that these are replicated in the whole pattern. */
        uint32_t const uElement         = RT_BIT_32(cOnes) - 1U;
        unsigned const cBitsElementLog2 = ASMBitFirstSetU32(cBitsElement) - 1;

        static const uint32_t s_auReplicate[]
            = { UINT32_MAX, UINT32_MAX / 3, UINT32_MAX / 15, UINT32_MAX / 255, UINT32_MAX / 65535, 1 };
        if (s_auReplicate[cBitsElementLog2] * uElement == fMask)
            *puImm6SizeLen = (cOnes - 1) | ((0x3e << cBitsElementLog2) & 0x3f);
        else
            return false;
    }
    *puImm6Rotations = cRor ? cBitsElement - cRor : cRol;

    return true;
}


/**
 * Helper for Armv8A64MkInstrLogicalImm and friends that tries to convert a
 * 64-bit bitmask to a set of immediates for those instructions.
 *
 * @returns true if successful, false if not.
 * @param   fMask           The mask value to convert.
 * @param   puImm7SizeLen   Where to return the N:immS part.
 * @param   puImm6Rotations Where to return the immR.
 */
DECLINLINE(bool) Armv8A64ConvertMask64ToImmRImmS(uint64_t fMask, uint32_t *puImm7SizeLen, uint32_t *puImm6Rotations)
{
    /* Fend off 0 and UINT64_MAX as these cannot be represented. */
    if ((uint64_t)(fMask + 1U) <= 1)
        return false;

    /* Rotate the value will we get all 1s at the bottom and the zeros at the top. */
    unsigned const cRor = ASMCountTrailingZerosU64(fMask);
    unsigned const cRol = ASMCountLeadingZerosU64(~fMask);
    if (cRor)
        fMask = ASMRotateRightU64(fMask, cRor);
    else
        fMask = ASMRotateLeftU64(fMask, cRol);
    Assert(fMask & RT_BIT_64(0));
    Assert(!(fMask & RT_BIT_64(63)));

    /* Count the trailing ones and leading zeros. */
    unsigned const cOnes  = ASMCountTrailingZerosU64(~fMask);
    unsigned const cZeros = ASMCountLeadingZerosU64(fMask);

    /* The potential element length is then the sum of the two above. */
    unsigned const cBitsElement = cOnes + cZeros;
    if (!RT_IS_POWER_OF_TWO(cBitsElement) || cBitsElement < 2)
        return false;

    /* Special case: 64 bits element size. Since we're done here. */
    if (cBitsElement == 64)
        *puImm7SizeLen = (cOnes - 1) | 0x40 /*N*/;
    else
    {
        /* Extract the element bits and check that these are replicated in the whole pattern. */
        uint64_t const uElement         = RT_BIT_64(cOnes) - 1U;
        unsigned const cBitsElementLog2 = ASMBitFirstSetU64(cBitsElement) - 1;

        static const uint64_t s_auReplicate[]
            = { UINT64_MAX, UINT64_MAX / 3, UINT64_MAX / 15, UINT64_MAX / 255, UINT64_MAX / 65535, UINT64_MAX / UINT32_MAX, 1 };
        if (s_auReplicate[cBitsElementLog2] * uElement == fMask)
            *puImm7SizeLen = (cOnes - 1) | ((0x3e << cBitsElementLog2) & 0x3f);
        else
            return false;
    }
    *puImm6Rotations = cRor ? cBitsElement - cRor : cRol;

    return true;
}

# endif /* IPRT_INCLUDED_asm_h */

/**
 * A64: Encodes a logical instruction with an complicated immediate mask.
 *
 * The @a uImm7SizeLen parameter specifies two things:
 *      1. the element size and
 *      2. the number of bits set to 1 in the pattern.
 *
 * The element size is extracted by NOT'ing bits 5:0 (excludes the N bit at the
 * top) and using the position of the first bit set as a power of two.
 *
 * | N | 5 | 4 | 3 | 2 | 1 | 0 | element size |
 * |---|---|---|---|---|---|---|--------------|
 * | 0 | 1 | 1 | 1 | 1 | 0 | x | 2 bits       |
 * | 0 | 1 | 1 | 1 | 0 | x | x | 4 bits       |
 * | 0 | 1 | 1 | 0 | x | x | x | 8 bits       |
 * | 0 | 1 | 0 | x | x | x | x | 16 bits      |
 * | 0 | 0 | x | x | x | x | x | 32 bits      |
 * | 1 | x | x | x | x | x | x | 64 bits      |
 *
 * The 'x' forms the number of 1 bits in the pattern, minus one (i.e.
 * there is always one zero bit in the pattern).
 *
 * The @a uImm6Rotations parameter specifies how many bits to the right,
 * the element pattern is rotated. The rotation count must be less than the
 * element bit count (size).
 *
 * @returns The encoded instruction.
 * @param   u2Opc           The logical operation to perform.
 * @param   iRegResult      The output register.
 * @param   iRegSrc         The 1st register operand.
 * @param   uImm7SizeLen    The size/pattern length.  We've combined the 1-bit N
 *                          field at the top of the 6-bit 'imms' field.
 *
 * @param   uImm6Rotations  The rotation count.
 * @param   f64Bit          true for 64-bit GPRs, @c false for 32-bit GPRs.
 * @see https://dinfuehr.github.io/blog/encoding-of-immediate-values-on-aarch64/
 *      https://gist.githubusercontent.com/dinfuehr/51a01ac58c0b23e4de9aac313ed6a06a/raw/1892a274aa3238d55f83eec5b3828da2aec5f229/aarch64-logical-immediates.txt
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrLogicalImm(uint32_t u2Opc, uint32_t iRegResult, uint32_t iRegSrc,
                                                      uint32_t uImm7SizeLen, uint32_t uImm6Rotations, bool f64Bit)
{
    Assert(u2Opc < 4); Assert(uImm7SizeLen < (f64Bit ? UINT32_C(0x7f) : UINT32_C(0x3f)));
    Assert(uImm6Rotations <= UINT32_C(0x3f)); Assert(iRegResult < 32); Assert(iRegSrc < 32);
    return ((uint32_t)f64Bit                << 31)
         | (u2Opc                           << 29)
         | UINT32_C(0x12000000)
         | ((uImm7SizeLen & UINT32_C(0x40)) << (22 - 6))
         | (uImm6Rotations                  << 16)
         | ((uImm7SizeLen & UINT32_C(0x3f)) << 10)
         | (iRegSrc                         <<  5)
         | iRegResult;
}


/** A64: Encodes an AND instruction w/ complicated immediate mask.
 * @see Armv8A64MkInstrLogicalImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrAndImm(uint32_t iRegResult, uint32_t iRegSrc,
                                                  uint32_t uImm7SizeLen, uint32_t uImm6Rotations = 0, bool f64Bit = true)
{
    return Armv8A64MkInstrLogicalImm(0, iRegResult, iRegSrc, uImm7SizeLen, uImm6Rotations, f64Bit);
}


/** A64: Encodes an ORR instruction w/ complicated immediate mask.
 * @see Armv8A64MkInstrLogicalImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrOrrImm(uint32_t iRegResult, uint32_t iRegSrc,
                                                  uint32_t uImm7SizeLen, uint32_t uImm6Rotations = 0, bool f64Bit = true)
{
    return Armv8A64MkInstrLogicalImm(1, iRegResult, iRegSrc, uImm7SizeLen, uImm6Rotations, f64Bit);
}


/** A64: Encodes an EOR instruction w/ complicated immediate mask.
 * @see Armv8A64MkInstrLogicalImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrEorImm(uint32_t iRegResult, uint32_t iRegSrc,
                                                  uint32_t uImm7SizeLen, uint32_t uImm6Rotations = 0, bool f64Bit = true)
{
    return Armv8A64MkInstrLogicalImm(2, iRegResult, iRegSrc, uImm7SizeLen, uImm6Rotations, f64Bit);
}


/** A64: Encodes an ANDS instruction w/ complicated immediate mask.
 * @see Armv8A64MkInstrLogicalImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrAndsImm(uint32_t iRegResult, uint32_t iRegSrc,
                                                   uint32_t uImm7SizeLen, uint32_t uImm6Rotations = 0, bool f64Bit = true)
{
    return Armv8A64MkInstrLogicalImm(3, iRegResult, iRegSrc, uImm7SizeLen, uImm6Rotations, f64Bit);
}


/** A64: Encodes an TST instruction w/ complicated immediate mask.
 * @see Armv8A64MkInstrLogicalImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrTstImm(uint32_t iRegSrc,
                                                  uint32_t uImm7SizeLen, uint32_t uImm6Rotations = 0, bool f64Bit = true)
{
    return Armv8A64MkInstrAndsImm(ARMV8_A64_REG_XZR, iRegSrc, uImm7SizeLen, uImm6Rotations, f64Bit);
}


/**
 * A64: Encodes a bitfield instruction.
 *
 * @returns The encoded instruction.
 * @param   u2Opc           The bitfield operation to perform.
 * @param   iRegResult      The output register.
 * @param   iRegSrc         The 1st register operand.
 * @param   cImm6Ror        The right rotation count.
 * @param   uImm6S          The leftmost bit to be moved.
 * @param   f64Bit          true for 64-bit GPRs, @c false for 32-bit GPRs.
 * @param   uN1             This must match @a f64Bit for all instructions
 *                          currently specified.
 * @see https://dinfuehr.github.io/blog/encoding-of-immediate-values-on-aarch64/
 *      https://gist.githubusercontent.com/dinfuehr/51a01ac58c0b23e4de9aac313ed6a06a/raw/1892a274aa3238d55f83eec5b3828da2aec5f229/aarch64-logical-immediates.txt
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrBitfieldImm(uint32_t u2Opc, uint32_t iRegResult, uint32_t iRegSrc,
                                                       uint32_t cImm6Ror, uint32_t uImm6S, bool f64Bit, uint32_t uN1)
{
    Assert(cImm6Ror <= (f64Bit ? UINT32_C(0x3f) : UINT32_C(0x1f))); Assert(iRegResult < 32); Assert(u2Opc < 4);
    Assert(uImm6S   <= (f64Bit ? UINT32_C(0x3f) : UINT32_C(0x1f))); Assert(iRegSrc    < 32); Assert(uN1 <= (unsigned)f64Bit);
    return ((uint32_t)f64Bit   << 31)
         | (u2Opc              << 29)
         | UINT32_C(0x13000000)
         | (uN1                << 22)
         | (cImm6Ror           << 16)
         | (uImm6S             << 10)
         | (iRegSrc            <<  5)
         | iRegResult;
}


/** A64: Encodes a SBFM instruction.
 * @see Armv8A64MkInstrBitfieldImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrSbfm(uint32_t iRegResult, uint32_t iRegSrc, uint32_t cImm6Ror, uint32_t uImm6S,
                                                bool f64Bit = true, uint32_t uN1 = UINT32_MAX)
{
    return Armv8A64MkInstrBitfieldImm(0, iRegResult, iRegSrc, cImm6Ror, uImm6S, f64Bit, uN1 == UINT32_MAX ? f64Bit : uN1);
}


/** A64: Encodes a SXTB instruction (sign-extend 8-bit value to 32/64-bit).
 * @see Armv8A64MkInstrBitfieldImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrSxtb(uint32_t iRegResult, uint32_t iRegSrc, bool f64Bit = true)
{
    return Armv8A64MkInstrSbfm(iRegResult, iRegSrc, 0, 7, f64Bit);
}


/** A64: Encodes a SXTH instruction (sign-extend 16-bit value to 32/64-bit).
 * @see Armv8A64MkInstrBitfieldImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrSxth(uint32_t iRegResult, uint32_t iRegSrc, bool f64Bit = true)
{
    return Armv8A64MkInstrSbfm(iRegResult, iRegSrc, 0, 15, f64Bit);
}


/** A64: Encodes a SXTH instruction (sign-extend 32-bit value to 64-bit).
 * @see Armv8A64MkInstrBitfieldImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrSxtw(uint32_t iRegResult, uint32_t iRegSrc)
{
    return Armv8A64MkInstrSbfm(iRegResult, iRegSrc, 0, 31, true /*f64Bit*/);
}


/** A64: Encodes an ASR instruction w/ immediate shift value.
 * @see Armv8A64MkInstrBitfieldImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrAsrImm(uint32_t iRegResult, uint32_t iRegSrc, uint32_t cShift, bool f64Bit = true)
{
    uint32_t const cWidth = f64Bit ? 63 : 31;
    Assert(cShift > 0); Assert(cShift <= cWidth);
    return Armv8A64MkInstrBitfieldImm(0, iRegResult, iRegSrc, cShift, cWidth /*uImm6S*/, f64Bit, f64Bit);
}


/** A64: Encodes a BFM instruction.
 * @see Armv8A64MkInstrBitfieldImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrBfm(uint32_t iRegResult, uint32_t iRegSrc, uint32_t cImm6Ror, uint32_t uImm6S,
                                                  bool f64Bit = true, uint32_t uN1 = UINT32_MAX)
{
    return Armv8A64MkInstrBitfieldImm(1, iRegResult, iRegSrc, cImm6Ror, uImm6S, f64Bit, uN1 == UINT32_MAX ? f64Bit : uN1);
}


/** A64: Encodes a BFI instruction (insert).
 * @see Armv8A64MkInstrBitfieldImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrBfi(uint32_t iRegResult, uint32_t iRegSrc,
                                               uint32_t offFirstBit, uint32_t cBitsWidth, bool f64Bit = true)
{
    Assert(cBitsWidth > 0U); Assert(cBitsWidth < (f64Bit ? 64U : 32U)); Assert(offFirstBit < (f64Bit ? 64U : 32U));
    return Armv8A64MkInstrBfm(iRegResult, iRegSrc, (uint32_t)-(int32_t)offFirstBit & (f64Bit ? 0x3f : 0x1f),
                              cBitsWidth - 1, f64Bit);
}


/** A64: Encodes a BFC instruction (clear).
 * @see Armv8A64MkInstrBitfieldImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrBfc(uint32_t iRegResult,
                                               uint32_t offFirstBit, uint32_t cBitsWidth, bool f64Bit = true)
{
    return Armv8A64MkInstrBfi(iRegResult, ARMV8_A64_REG_XZR, offFirstBit, cBitsWidth, f64Bit);
}


/** A64: Encodes a BFXIL instruction (insert low).
 * @see Armv8A64MkInstrBitfieldImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrBfxil(uint32_t iRegResult, uint32_t iRegSrc,
                                                 uint32_t offFirstBit, uint32_t cBitsWidth, bool f64Bit = true)
{
    Assert(cBitsWidth > 0U); Assert(cBitsWidth < (f64Bit ? 64U : 32U)); Assert(offFirstBit < (f64Bit ? 64U : 32U));
    Assert(offFirstBit + cBitsWidth <= (f64Bit ? 64U : 32U));
    return Armv8A64MkInstrBfm(iRegResult, iRegSrc, (uint32_t)offFirstBit, offFirstBit + cBitsWidth - 1, f64Bit);
}


/** A64: Encodes an UBFM instruction.
 * @see Armv8A64MkInstrBitfieldImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrUbfm(uint32_t iRegResult, uint32_t iRegSrc, uint32_t cImm6Ror, uint32_t uImm6S,
                                                bool f64Bit = true, uint32_t uN1 = UINT32_MAX)
{
    return Armv8A64MkInstrBitfieldImm(2, iRegResult, iRegSrc, cImm6Ror, uImm6S, f64Bit, uN1 == UINT32_MAX ? f64Bit : uN1);
}


/** A64: Encodes an UBFX instruction (zero extending extract).
 * @see Armv8A64MkInstrBitfieldImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrUbfx(uint32_t iRegResult, uint32_t iRegSrc,
                                                uint32_t offFirstBit, uint32_t cBitsWidth, bool f64Bit = true)
{
    return Armv8A64MkInstrUbfm(iRegResult, iRegSrc, offFirstBit, offFirstBit + cBitsWidth - 1, f64Bit);
}


/** A64: Encodes an UBFIZ instruction (zero extending extract from bit zero,
 *  shifted into destination).
 * @see Armv8A64MkInstrBitfieldImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrUbfiz(uint32_t iRegResult, uint32_t iRegSrc,
                                                 uint32_t offFirstBitDst, uint32_t cBitsWidth, bool f64Bit = true)
{
    uint32_t fMask = f64Bit ? 0x3f : 0x1f;
    return Armv8A64MkInstrUbfm(iRegResult, iRegSrc, -(int32_t)offFirstBitDst & fMask, cBitsWidth - 1, f64Bit);
}


/** A64: Encodes an LSL instruction w/ immediate shift value.
 * @see Armv8A64MkInstrBitfieldImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrLslImm(uint32_t iRegResult, uint32_t iRegSrc, uint32_t cShift, bool f64Bit = true)
{
    uint32_t const cWidth = f64Bit ? 63 : 31;
    Assert(cShift > 0); Assert(cShift <= cWidth);
    return Armv8A64MkInstrBitfieldImm(2, iRegResult, iRegSrc, (uint32_t)(0 - cShift) & cWidth,
                                      cWidth - cShift /*uImm6S*/, f64Bit, f64Bit);
}


/** A64: Encodes an LSR instruction w/ immediate shift value.
 * @see Armv8A64MkInstrBitfieldImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrLsrImm(uint32_t iRegResult, uint32_t iRegSrc, uint32_t cShift, bool f64Bit = true)
{
    uint32_t const cWidth = f64Bit ? 63 : 31;
    Assert(cShift > 0); Assert(cShift <= cWidth);
    return Armv8A64MkInstrBitfieldImm(2, iRegResult, iRegSrc, cShift, cWidth /*uImm6S*/, f64Bit, f64Bit);
}


/** A64: Encodes an UXTB instruction - zero extend byte (8-bit).
 * @see Armv8A64MkInstrBitfieldImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrUxtb(uint32_t iRegResult, uint32_t iRegSrc, bool f64Bit = false)
{
    return Armv8A64MkInstrBitfieldImm(2, iRegResult, iRegSrc, 0, 7, f64Bit, f64Bit);
}


/** A64: Encodes an UXTH instruction - zero extend half word (16-bit).
 * @see Armv8A64MkInstrBitfieldImm for parameter details.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrUxth(uint32_t iRegResult, uint32_t iRegSrc, bool f64Bit = false)
{
    return Armv8A64MkInstrBitfieldImm(2, iRegResult, iRegSrc, 0, 15, f64Bit, f64Bit);
}


/**
 * A64: Encodes an EXTR instruction with an immediate.
 *
 * @returns The encoded instruction.
 * @param   iRegResult  The register to store the result in. ZR is valid.
 * @param   iRegLow     The register holding the least significant bits in the
 *                      extraction. ZR is valid.
 * @param   iRegHigh    The register holding the most significant bits in the
 *                      extraction. ZR is valid.
 * @param   uLsb        The bit number of the least significant bit, or where in
 *                      @a iRegLow to start the
 *                      extraction.
 * @param   f64Bit      true for 64-bit GRPs (default), false for 32-bit GPRs.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrExtrImm(uint32_t iRegResult, uint32_t iRegLow, uint32_t iRegHigh, uint32_t uLsb,
                                                   bool f64Bit = true)
{
    Assert(uLsb < (uint32_t)(f64Bit ? 64 : 32)); Assert(iRegHigh < 32); Assert(iRegLow < 32); Assert(iRegResult < 32);
    return ((uint32_t)f64Bit       << 31)
         | UINT32_C(0x13800000)
         | ((uint32_t)f64Bit       << 22) /*N*/
         | (iRegHigh               << 16)
         | (uLsb                   << 10)
         | (iRegLow                <<  5)
         | iRegResult;
}


/** A64: Rotates the value of a register (alias for EXTR). */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrRorImm(uint32_t iRegResult, uint32_t iRegSrc, uint32_t cShift, bool f64Bit = true)
{
    return Armv8A64MkInstrExtrImm(iRegResult, iRegSrc, iRegSrc, cShift, f64Bit);
}


/**
 * A64: Encodes either add, adds, sub or subs with unsigned 12-bit immediate.
 *
 * @returns The encoded instruction.
 * @param   fSub                    true for sub and subs, false for add and
 *                                  adds.
 * @param   iRegResult              The register to store the result in.
 *                                  SP is valid when @a fSetFlags = false,
 *                                  and ZR is valid otherwise.
 * @param   iRegSrc                 The register containing the augend (@a fSub
 *                                  = false) or minuend (@a fSub = true).  SP is
 *                                  a valid registers for all variations.
 * @param   uImm12AddendSubtrahend  The addend (@a fSub = false) or subtrahend
 *                                  (@a fSub = true).
 * @param   f64Bit                  true for 64-bit GRPs (default), false for
 *                                  32-bit GPRs.
 * @param   fSetFlags               Whether to set flags (adds / subs) or not
 *                                  (add / sub - default).
 * @param   fShift12                Whether to shift uImm12AddendSubtrahend 12
 *                                  bits to the left, or not (default).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrAddSubUImm12(bool fSub, uint32_t iRegResult, uint32_t iRegSrc,
                                                        uint32_t uImm12AddendSubtrahend, bool f64Bit = true,
                                                        bool fSetFlags = false, bool fShift12 = false)
{
    Assert(uImm12AddendSubtrahend < 4096); Assert(iRegSrc < 32); Assert(iRegResult < 32);
    return ((uint32_t)f64Bit       << 31)
         | ((uint32_t)fSub         << 30)
         | ((uint32_t)fSetFlags    << 29)
         | UINT32_C(0x11000000)
         | ((uint32_t)fShift12     << 22)
         | (uImm12AddendSubtrahend << 10)
         | (iRegSrc                <<  5)
         | iRegResult;
}


/** Alias for sub zxr, reg, \#uimm12.  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCmpUImm12(uint32_t iRegSrc, uint32_t uImm12Comprahend,
                                                     bool f64Bit = true, bool fShift12 = false)
{
    return Armv8A64MkInstrAddSubUImm12(true /*fSub*/, ARMV8_A64_REG_XZR, iRegSrc, uImm12Comprahend,
                                       f64Bit, true /*fSetFlags*/, fShift12);
}


/** ADD dst, src, \#uimm12  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrAddUImm12(uint32_t iRegResult, uint32_t iRegSrc, uint32_t uImm12Addend,
                                                     bool f64Bit = true, bool fSetFlags = false, bool fShift12 = false)
{
    return Armv8A64MkInstrAddSubUImm12(false /*fSub*/, iRegResult, iRegSrc, uImm12Addend, f64Bit, fSetFlags, fShift12);
}


/** SUB dst, src, \#uimm12  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrSubUImm12(uint32_t iRegResult, uint32_t iRegSrc, uint32_t uImm12Subtrahend,
                                                     bool f64Bit = true, bool fSetFlags = false, bool fShift12 = false)
{
    return Armv8A64MkInstrAddSubUImm12(true /*fSub*/, iRegResult, iRegSrc, uImm12Subtrahend, f64Bit, fSetFlags, fShift12);
}


/**
 * A64: Encodes either add, adds, sub or subs with shifted register.
 *
 * @returns The encoded instruction.
 * @param   fSub                    true for sub and subs, false for add and
 *                                  adds.
 * @param   iRegResult              The register to store the result in.
 *                                  SP is NOT valid, but ZR is.
 * @param   iRegSrc1                The register containing the augend (@a fSub
 *                                  = false) or minuend (@a fSub = true).
 *                                  SP is NOT valid, but ZR is.
 * @param   iRegSrc2                The register containing the addened (@a fSub
 *                                  = false) or subtrahend (@a fSub = true).
 *                                  SP is NOT valid, but ZR is.
 * @param   f64Bit                  true for 64-bit GRPs (default), false for
 *                                  32-bit GPRs.
 * @param   fSetFlags               Whether to set flags (adds / subs) or not
 *                                  (add / sub - default).
 * @param   cShift                  The shift count to apply to @a iRegSrc2.
 * @param   enmShift                The shift type to apply to the @a iRegSrc2
 *                                  register. kArmv8A64InstrShift_Ror is
 *                                  reserved.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrAddSubReg(bool fSub, uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2,
                                                     bool f64Bit = true, bool fSetFlags = false, uint32_t cShift = 0,
                                                     ARMV8A64INSTRSHIFT enmShift = kArmv8A64InstrShift_Lsl)
{
    Assert(iRegResult < 32); Assert(iRegSrc1 < 32); Assert(iRegSrc2 < 32);
    Assert(cShift < (f64Bit ? 64U : 32U)); Assert(enmShift != kArmv8A64InstrShift_Ror);

    return ((uint32_t)f64Bit       << 31)
         | ((uint32_t)fSub         << 30)
         | ((uint32_t)fSetFlags    << 29)
         | UINT32_C(0x0b000000)
         | ((uint32_t)enmShift     << 22)
         | (iRegSrc2               << 16)
         | (cShift                 << 10)
         | (iRegSrc1               <<  5)
         | iRegResult;
}


/** Alias for sub zxr, reg1, reg2 [, LSL/LSR/ASR/ROR \#xx].  */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCmpReg(uint32_t iRegSrc1, uint32_t iRegSrc2, bool f64Bit = true, uint32_t cShift = 0,
                                                  ARMV8A64INSTRSHIFT enmShift = kArmv8A64InstrShift_Lsl)
{
    return Armv8A64MkInstrAddSubReg(true /*fSub*/, ARMV8_A64_REG_XZR, iRegSrc1, iRegSrc2,
                                    f64Bit, true /*fSetFlags*/, cShift, enmShift);
}


/** ADD dst, reg1, reg2 [, LSL/LSR/ASR/ROR \#xx] */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrAddReg(uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2,
                                                  bool f64Bit = true, bool fSetFlags = false, uint32_t cShift = 0,
                                                  ARMV8A64INSTRSHIFT enmShift = kArmv8A64InstrShift_Lsl)
{
    return Armv8A64MkInstrAddSubReg(false /*fSub*/, iRegResult, iRegSrc1, iRegSrc2, f64Bit, fSetFlags, cShift, enmShift);
}


/** SUB dst, reg1, reg2 [, LSL/LSR/ASR/ROR \#xx] */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrSubReg(uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2,
                                                  bool f64Bit = true, bool fSetFlags = false, uint32_t cShift = 0,
                                                  ARMV8A64INSTRSHIFT enmShift = kArmv8A64InstrShift_Lsl)
{
    return Armv8A64MkInstrAddSubReg(true /*fSub*/, iRegResult, iRegSrc1, iRegSrc2, f64Bit, fSetFlags, cShift, enmShift);
}


/** NEG dst */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrNeg(uint32_t iRegResult, bool f64Bit = true, bool fSetFlags = false)
{
    return Armv8A64MkInstrAddSubReg(true /*fSub*/, iRegResult, ARMV8_A64_REG_XZR, iRegResult, f64Bit, fSetFlags);
}


/** Extension option for 'extended register' instructions. */
typedef enum ARMV8A64INSTREXTEND
{
    kArmv8A64InstrExtend_UxtB = 0,
    kArmv8A64InstrExtend_UxtH,
    kArmv8A64InstrExtend_UxtW,
    kArmv8A64InstrExtend_UxtX,
    kArmv8A64InstrExtend_SxtB,
    kArmv8A64InstrExtend_SxtH,
    kArmv8A64InstrExtend_SxtW,
    kArmv8A64InstrExtend_SxtX,
    /** The default is either UXTW or UXTX depending on whether the instruction
     *  is in 32-bit or 64-bit mode.  Thus, this needs to be resolved according
     *  to the f64Bit value. */
    kArmv8A64InstrExtend_Default
} ARMV8A64INSTREXTEND;


/**
 * A64: Encodes either add, adds, sub or subs with extended register encoding.
 *
 * @returns The encoded instruction.
 * @param   fSub                    true for sub and subs, false for add and
 *                                  adds.
 * @param   iRegResult              The register to store the result in.
 *                                  SP is NOT valid, but ZR is.
 * @param   iRegSrc1                The register containing the augend (@a fSub
 *                                  = false) or minuend (@a fSub = true).
 *                                  SP is valid, but ZR is NOT.
 * @param   iRegSrc2                The register containing the addened (@a fSub
 *                                  = false) or subtrahend (@a fSub = true).
 *                                  SP is NOT valid, but ZR is.
 * @param   f64Bit                  true for 64-bit GRPs (default), false for
 *                                  32-bit GPRs.
 * @param   fSetFlags               Whether to set flags (adds / subs) or not
 *                                  (add / sub - default).
 * @param   enmExtend               The type of extension to apply to @a
 *                                  iRegSrc2.
 * @param   cShift                  The left shift count to apply to @a iRegSrc2
 *                                  after enmExtend processing is done.
 *                                  Max shift is 4 for some reason.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrAddSubRegExtend(bool fSub, uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2,
                                                           bool f64Bit = true, bool fSetFlags = false,
                                                           ARMV8A64INSTREXTEND enmExtend = kArmv8A64InstrExtend_Default,
                                                           uint32_t cShift = 0)
{
    if (enmExtend == kArmv8A64InstrExtend_Default)
        enmExtend = f64Bit ? kArmv8A64InstrExtend_UxtW : kArmv8A64InstrExtend_UxtX;
    Assert(iRegResult < 32); Assert(iRegSrc1 < 32); Assert(iRegSrc2 < 32); Assert(cShift <= 4);

    return ((uint32_t)f64Bit       << 31)
         | ((uint32_t)fSub         << 30)
         | ((uint32_t)fSetFlags    << 29)
         | UINT32_C(0x0b200000)
         | (iRegSrc2               << 16)
         | ((uint32_t)enmExtend    << 13)
         | (cShift                 << 10)
         | (iRegSrc1               <<  5)
         | iRegResult;
}


/**
 * A64: Encodes either adc, adcs, sbc or sbcs with two source registers.
 *
 * @returns The encoded instruction.
 * @param   fSub                    true for sbc and sbcs, false for adc and
 *                                  adcs.
 * @param   iRegResult              The register to store the result in. SP is
 *                                  NOT valid, but ZR is.
 * @param   iRegSrc1                The register containing the augend (@a fSub
 *                                  = false) or minuend (@a fSub = true).
 *                                  SP is NOT valid, but ZR is.
 * @param   iRegSrc2                The register containing the addened (@a fSub
 *                                  = false) or subtrahend (@a fSub = true).
 *                                  SP is NOT valid, but ZR is.
 * @param   f64Bit                  true for 64-bit GRPs (default), false for
 *                                  32-bit GPRs.
 * @param   fSetFlags               Whether to set flags (adds / subs) or not
 *                                  (add / sub - default).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrAdcSbc(bool fSub, uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2,
                                                  bool f64Bit = true, bool fSetFlags = false)
{
    Assert(iRegResult < 32); Assert(iRegSrc1 < 32); Assert(iRegSrc2 < 32);

    return ((uint32_t)f64Bit       << 31)
         | ((uint32_t)fSub         << 30)
         | ((uint32_t)fSetFlags    << 29)
         | UINT32_C(0x1a000000)
         | (iRegSrc2               << 16)
         | (iRegSrc1               <<  5)
         | iRegResult;
}


/** ADC dst, reg1, reg2 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrAdc(uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2,
                                               bool f64Bit = true, bool fSetFlags = false)
{
    return Armv8A64MkInstrAdcSbc(false /*fSub*/, iRegResult, iRegSrc1, iRegSrc2, f64Bit, fSetFlags);
}


/** ADCS dst, reg1, reg2 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrAdcs(uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2, bool f64Bit = true)
{
    return Armv8A64MkInstrAdcSbc(false /*fSub*/, iRegResult, iRegSrc1, iRegSrc2, f64Bit, true /*fSetFlags*/);
}


/** SBC dst, reg1, reg2 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrSbc(uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2,
                                               bool f64Bit = true, bool fSetFlags = false)
{
    return Armv8A64MkInstrAdcSbc(true /*fSub*/, iRegResult, iRegSrc1, iRegSrc2, f64Bit, fSetFlags);
}


/** SBCS dst, reg1, reg2 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrSbcs(uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2, bool f64Bit = true)
{
    return Armv8A64MkInstrAdcSbc(true /*fSub*/, iRegResult, iRegSrc1, iRegSrc2, f64Bit, true /*fSetFlags*/);
}


/**
 * A64: Encodes a B (unconditional branch w/ imm) instruction.
 *
 * @returns The encoded instruction.
 * @param   iImm26      Signed number of instruction to jump (i.e. *4).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrB(int32_t iImm26)
{
    Assert(iImm26 >= -67108864 && iImm26 < 67108864);
    return UINT32_C(0x14000000) | ((uint32_t)iImm26 & UINT32_C(0x3ffffff));
}


/**
 * A64: Encodes a BL (unconditional call w/ imm) instruction.
 *
 * @returns The encoded instruction.
 * @param   iImm26      Signed number of instruction to jump (i.e. *4).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrBl(int32_t iImm26)
{
    return Armv8A64MkInstrB(iImm26) | RT_BIT_32(31);
}


/**
 * A64: Encodes a BR (unconditional branch w/ register) instruction.
 *
 * @returns The encoded instruction.
 * @param   iReg                    The register containing the target address.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrBr(uint32_t iReg)
{
    Assert(iReg < 32);
    return UINT32_C(0xd61f0000) | (iReg <<  5);
}


/**
 * A64: Encodes a BLR instruction.
 *
 * @returns The encoded instruction.
 * @param   iReg                    The register containing the target address.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrBlr(uint32_t iReg)
{
    return Armv8A64MkInstrBr(iReg) | RT_BIT_32(21);
}


/**
 * A64: Encodes CBZ and CBNZ (conditional branch w/ immediate) instructions.
 *
 * @returns The encoded instruction.
 * @param   fJmpIfNotZero   false to jump if register is zero, true to jump if
 *                          its not zero.
 * @param   iImm19          Signed number of instruction to jump (i.e. *4).
 * @param   iReg            The GPR to check for zero / non-zero value.
 * @param   f64Bit          true for 64-bit register, false for 32-bit.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCbzCbnz(bool fJmpIfNotZero, int32_t iImm19, uint32_t iReg, bool f64Bit = true)
{
    Assert(iReg < 32); Assert(iImm19 >= -262144 && iImm19 < 262144);
    return ((uint32_t)f64Bit             << 31)
         | UINT32_C(0x34000000)
         | ((uint32_t)fJmpIfNotZero      << 24)
         | (((uint32_t)iImm19 & 0x7ffff) <<  5)
         | iReg;
}


/** A64: Encodes the CBZ instructions. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCbz(int32_t iImm19, uint32_t iReg, bool f64Bit = true)
{
    return Armv8A64MkInstrCbzCbnz(false /*fJmpIfNotZero*/, iImm19, iReg, f64Bit);
}


/** A64: Encodes the CBNZ instructions. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCbnz(int32_t iImm19, uint32_t iReg, bool f64Bit = true)
{
    return Armv8A64MkInstrCbzCbnz(true /*fJmpIfNotZero*/, iImm19, iReg, f64Bit);
}


/**
 * A64: Encodes TBZ and TBNZ (conditional branch w/ immediate) instructions.
 *
 * @returns The encoded instruction.
 * @param   fJmpIfNotZero   false to jump if register is zero, true to jump if
 *                          its not zero.
 * @param   iImm14          Signed number of instruction to jump (i.e. *4).
 * @param   iReg            The GPR to check for zero / non-zero value.
 * @param   iBitNo          The bit to test for.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrTbzTbnz(bool fJmpIfNotZero, int32_t iImm14, uint32_t iReg, uint32_t iBitNo)
{
    Assert(iReg < 32); Assert(iImm14 >= -8192 && iImm14 < 8192); Assert(iBitNo < 64);
    return ((uint32_t)(iBitNo & 0x20)   << (31-5))
         | UINT32_C(0x36000000)
         | ((uint32_t)fJmpIfNotZero     << 24)
         | ((iBitNo & 0x1f)             << 19)
         | (((uint32_t)iImm14 & 0x3fff) <<  5)
         | iReg;
}


/**
 * A64: Encodes TBZ (conditional branch w/ immediate) instructions.
 *
 * @returns The encoded instruction.
 * @param   iImm14          Signed number of instruction to jump (i.e. *4).
 * @param   iReg            The GPR to check for zero / non-zero value.
 * @param   iBitNo          The bit to test for.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrTbz(int32_t iImm14, uint32_t iReg, uint32_t iBitNo)
{
    return Armv8A64MkInstrTbzTbnz(false /*fJmpIfNotZero*/, iImm14, iReg, iBitNo);
}


/**
 * A64: Encodes TBNZ (conditional branch w/ immediate) instructions.
 *
 * @returns The encoded instruction.
 * @param   iImm14          Signed number of instruction to jump (i.e. *4).
 * @param   iReg            The GPR to check for zero / non-zero value.
 * @param   iBitNo          The bit to test for.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrTbnz(int32_t iImm14, uint32_t iReg, uint32_t iBitNo)
{
    return Armv8A64MkInstrTbzTbnz(true /*fJmpIfNotZero*/, iImm14, iReg, iBitNo);
}



/** Armv8 Condition codes.    */
typedef enum ARMV8INSTRCOND
{
    kArmv8InstrCond_Eq = 0,                     /**< 0 - Equal - Zero set. */
    kArmv8InstrCond_Ne,                         /**< 1 - Not equal - Zero clear. */

    kArmv8InstrCond_Cs,                         /**< 2 - Carry set (also known as 'HS'). */
    kArmv8InstrCond_Hs = kArmv8InstrCond_Cs,    /**< 2 - Unsigned higher or same. */
    kArmv8InstrCond_Cc,                         /**< 3 - Carry clear (also known as 'LO'). */
    kArmv8InstrCond_Lo = kArmv8InstrCond_Cc,    /**< 3 - Unsigned lower. */

    kArmv8InstrCond_Mi,                         /**< 4 - Negative result (minus). */
    kArmv8InstrCond_Pl,                         /**< 5 - Positive or zero result (plus). */

    kArmv8InstrCond_Vs,                         /**< 6 - Overflow set. */
    kArmv8InstrCond_Vc,                         /**< 7 - Overflow clear. */

    kArmv8InstrCond_Hi,                         /**< 8 - Unsigned higher. */
    kArmv8InstrCond_Ls,                         /**< 9 - Unsigned lower or same. */

    kArmv8InstrCond_Ge,                         /**< a - Signed greater or equal. */
    kArmv8InstrCond_Lt,                         /**< b - Signed less than. */

    kArmv8InstrCond_Gt,                         /**< c - Signed greater than. */
    kArmv8InstrCond_Le,                         /**< d - Signed less or equal. */

    kArmv8InstrCond_Al,                         /**< e - Condition is always true. */
    kArmv8InstrCond_Al1                         /**< f - Condition is always true. */
} ARMV8INSTRCOND;

/**
 * A64: Encodes conditional branch instruction w/ immediate target.
 *
 * @returns The encoded instruction.
 * @param   enmCond         The branch condition.
 * @param   iImm19          Signed number of instruction to jump (i.e. *4).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrBCond(ARMV8INSTRCOND enmCond, int32_t iImm19)
{
    Assert((unsigned)enmCond < 16);
    return UINT32_C(0x54000000)
         | (((uint32_t)iImm19 & 0x7ffff) << 5)
         | (uint32_t)enmCond;
}


/**
 * A64: Encodes the BRK instruction.
 *
 * @returns The encoded instruction.
 * @param   uImm16          Unsigned immediate value.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrBrk(uint32_t uImm16)
{
    Assert(uImm16 < _64K);
    return UINT32_C(0xd4200000)
         | (uImm16 << 5);
}

/** @name  RMA64_NZCV_F_XXX - readable NZCV mask for CCMP and friends.
 * @{ */
#define ARMA64_NZCV_F_N0_Z0_C0_V0     UINT32_C(0x0)
#define ARMA64_NZCV_F_N0_Z0_C0_V1     UINT32_C(0x1)
#define ARMA64_NZCV_F_N0_Z0_C1_V0     UINT32_C(0x2)
#define ARMA64_NZCV_F_N0_Z0_C1_V1     UINT32_C(0x3)
#define ARMA64_NZCV_F_N0_Z1_C0_V0     UINT32_C(0x4)
#define ARMA64_NZCV_F_N0_Z1_C0_V1     UINT32_C(0x5)
#define ARMA64_NZCV_F_N0_Z1_C1_V0     UINT32_C(0x6)
#define ARMA64_NZCV_F_N0_Z1_C1_V1     UINT32_C(0x7)

#define ARMA64_NZCV_F_N1_Z0_C0_V0     UINT32_C(0x8)
#define ARMA64_NZCV_F_N1_Z0_C0_V1     UINT32_C(0x9)
#define ARMA64_NZCV_F_N1_Z0_C1_V0     UINT32_C(0xa)
#define ARMA64_NZCV_F_N1_Z0_C1_V1     UINT32_C(0xb)
#define ARMA64_NZCV_F_N1_Z1_C0_V0     UINT32_C(0xc)
#define ARMA64_NZCV_F_N1_Z1_C0_V1     UINT32_C(0xd)
#define ARMA64_NZCV_F_N1_Z1_C1_V0     UINT32_C(0xe)
#define ARMA64_NZCV_F_N1_Z1_C1_V1     UINT32_C(0xf)
/** @} */

/**
 * A64: Encodes CCMP or CCMN with two register operands.
 *
 * @returns The encoded instruction.
 * @param   iRegSrc1    The 1st register. SP is NOT valid, but ZR is.
 * @param   iRegSrc2    The 2nd register. SP is NOT valid, but ZR is.
 * @param   fNzcv       The N, Z, C & V flags values to load if the condition
 *                      does not match. See RMA64_NZCV_F_XXX.
 * @param   enmCond     The condition guarding the compare.
 * @param   fCCmp       Set for CCMP (default), clear for CCMN.
 * @param   f64Bit      true for 64-bit GRPs (default), false for 32-bit GPRs.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCCmpCmnReg(uint32_t iRegSrc1, uint32_t iRegSrc2, uint32_t fNzcv,
                                                      ARMV8INSTRCOND enmCond, bool fCCmp = true, bool f64Bit = true)
{
    Assert(iRegSrc1 < 32); Assert(iRegSrc2 < 32); Assert(fNzcv < 16);

    return ((uint32_t)f64Bit       << 31)
         | ((uint32_t)fCCmp        << 30)
         | UINT32_C(0x3a400000)
         | (iRegSrc2               << 16)
         | ((uint32_t)enmCond      << 12)
         | (iRegSrc1               <<  5)
         | fNzcv;
}

/** CCMP w/ reg. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCCmpReg(uint32_t iRegSrc1, uint32_t iRegSrc2, uint32_t fNzcv,
                                                   ARMV8INSTRCOND enmCond, bool f64Bit = true)
{
    return Armv8A64MkInstrCCmpCmnReg(iRegSrc1, iRegSrc2, fNzcv, enmCond, true /*fCCmp*/, f64Bit);
}


/** CCMN w/ reg. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCCmnReg(uint32_t iRegSrc1, uint32_t iRegSrc2, uint32_t fNzcv,
                                                   ARMV8INSTRCOND enmCond, bool f64Bit = true)
{
    return Armv8A64MkInstrCCmpCmnReg(iRegSrc1, iRegSrc2, fNzcv, enmCond, false /*fCCmp*/, f64Bit);
}


/**
 * A64: Encodes CCMP or CCMN with register and 5-bit immediate.
 *
 * @returns The encoded instruction.
 * @param   iRegSrc     The register. SP is NOT valid, but ZR is.
 * @param   uImm5       The immediate, to compare iRegSrc with.
 * @param   fNzcv       The N, Z, C & V flags values to load if the condition
 *                      does not match. See RMA64_NZCV_F_XXX.
 * @param   enmCond     The condition guarding the compare.
 * @param   fCCmp       Set for CCMP (default), clear for CCMN.
 * @param   f64Bit      true for 64-bit GRPs (default), false for 32-bit GPRs.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCCmpCmnImm(uint32_t iRegSrc, uint32_t uImm5, uint32_t fNzcv, ARMV8INSTRCOND enmCond,
                                                      bool fCCmp = true, bool f64Bit = true)
{
    Assert(iRegSrc < 32); Assert(uImm5 < 32); Assert(fNzcv < 16);

    return ((uint32_t)f64Bit       << 31)
         | ((uint32_t)fCCmp        << 30)
         | UINT32_C(0x3a400800)
         | (uImm5                  << 16)
         | ((uint32_t)enmCond      << 12)
         | (iRegSrc                <<  5)
         | fNzcv;
}

/** CCMP w/ immediate. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCCmpImm(uint32_t iRegSrc, uint32_t uImm5, uint32_t fNzcv,
                                                   ARMV8INSTRCOND enmCond, bool f64Bit = true)
{
    return Armv8A64MkInstrCCmpCmnImm(iRegSrc, uImm5, fNzcv, enmCond, true /*fCCmp*/, f64Bit);
}


/** CCMN w/ immediate. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCCmnImm(uint32_t iRegSrc, uint32_t uImm5, uint32_t fNzcv,
                                                   ARMV8INSTRCOND enmCond, bool f64Bit = true)
{
    return Armv8A64MkInstrCCmpCmnImm(iRegSrc, uImm5, fNzcv, enmCond, false /*fCCmp*/, f64Bit);
}


/**
 * A64: Encodes CSEL, CSINC, CSINV and CSNEG (three registers)
 *
 * @returns The encoded instruction.
 * @param   uOp         Opcode bit 30.
 * @param   uOp2        Opcode bits 11:10.
 * @param   iRegResult  The result register. SP is NOT valid, but ZR is.
 * @param   iRegSrc1    The 1st source register. SP is NOT valid, but ZR is.
 * @param   iRegSrc2    The 2nd source register. SP is NOT valid, but ZR is.
 * @param   enmCond     The condition guarding the compare.
 * @param   f64Bit      true for 64-bit GRPs (default), false for 32-bit GPRs.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCondSelect(uint32_t uOp, uint32_t uOp2, uint32_t iRegResult, uint32_t iRegSrc1,
                                                      uint32_t iRegSrc2, ARMV8INSTRCOND enmCond, bool f64Bit = true)
{
    Assert(uOp <= 1); Assert(uOp2 <= 1); Assert(iRegResult < 32); Assert(iRegSrc1 < 32); Assert(iRegSrc2 < 32);

    return ((uint32_t)f64Bit       << 31)
         | (uOp                    << 30)
         | UINT32_C(0x1a800000)
         | (iRegSrc2               << 16)
         | ((uint32_t)enmCond      << 12)
         | (uOp2                   << 10)
         | (iRegSrc1               <<  5)
         | iRegResult;
}


/** A64: Encodes CSEL.
 * @see Armv8A64MkInstrCondSelect for details. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCSel(uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2,
                                                ARMV8INSTRCOND enmCond, bool f64Bit = true)
{
    return Armv8A64MkInstrCondSelect(0, 0, iRegResult, iRegSrc1, iRegSrc2, enmCond, f64Bit);
}


/** A64: Encodes CSINC.
 * @see Armv8A64MkInstrCondSelect for details. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCSInc(uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2,
                                                 ARMV8INSTRCOND enmCond, bool f64Bit = true)
{
    return Armv8A64MkInstrCondSelect(0, 1, iRegResult, iRegSrc1, iRegSrc2, enmCond, f64Bit);
}


/** A64: Encodes CSET.
 * @see Armv8A64MkInstrCondSelect for details. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCSet(uint32_t iRegResult, ARMV8INSTRCOND enmCond, bool f64Bit = true)
{
    Assert(enmCond != kArmv8InstrCond_Al && enmCond != kArmv8InstrCond_Al1);
    enmCond = (ARMV8INSTRCOND)((uint32_t)enmCond ^ 1);
    return Armv8A64MkInstrCSInc(iRegResult, ARMV8_A64_REG_XZR, ARMV8_A64_REG_XZR, enmCond, f64Bit);
}


/** A64: Encodes CSINV.
 * @see Armv8A64MkInstrCondSelect for details. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCSInv(uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2,
                                                 ARMV8INSTRCOND enmCond, bool f64Bit = true)
{
    return Armv8A64MkInstrCondSelect(1, 0, iRegResult, iRegSrc1, iRegSrc2, enmCond, f64Bit);
}

/** A64: Encodes CSETM.
 * @see Armv8A64MkInstrCondSelect for details. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCSetM(uint32_t iRegResult, ARMV8INSTRCOND enmCond, bool f64Bit = true)
{
    Assert(enmCond != kArmv8InstrCond_Al && enmCond != kArmv8InstrCond_Al1);
    enmCond = (ARMV8INSTRCOND)((uint32_t)enmCond ^ 1);
    return Armv8A64MkInstrCSInv(iRegResult, ARMV8_A64_REG_XZR, ARMV8_A64_REG_XZR, enmCond, f64Bit);
}


/** A64: Encodes CSNEG.
 * @see Armv8A64MkInstrCondSelect for details. */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrCSNeg(uint32_t iRegResult, uint32_t iRegSrc1, uint32_t iRegSrc2,
                                                 ARMV8INSTRCOND enmCond, bool f64Bit = true)
{
    return Armv8A64MkInstrCondSelect(1, 1, iRegResult, iRegSrc1, iRegSrc2, enmCond, f64Bit);
}


/**
 * A64: Encodes REV instruction.
 *
 * @returns The encoded instruction.
 * @param   iRegDst     The destination register. SP is NOT valid.
 * @param   iRegSrc     The source register. SP is NOT valid, but ZR is
 * @param   f64Bit      true for 64-bit GRPs (default), false for 32-bit GPRs.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrRev(uint32_t iRegDst, uint32_t iRegSrc, bool f64Bit = true)
{
    Assert(iRegDst < 32); Assert(iRegSrc < 32);

    return ((uint32_t)f64Bit       << 31)
         | UINT32_C(0x5ac00800)
         | ((uint32_t)f64Bit       << 10)
         | (iRegSrc                <<  5)
         | iRegDst;
}


/**
 * A64: Encodes REV16 instruction.
 *
 * @returns The encoded instruction.
 * @param   iRegDst     The destination register. SP is NOT valid.
 * @param   iRegSrc     The source register. SP is NOT valid, but ZR is
 * @param   f64Bit      true for 64-bit GRPs (default), false for 32-bit GPRs.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrRev16(uint32_t iRegDst, uint32_t iRegSrc, bool f64Bit = true)
{
    Assert(iRegDst < 32); Assert(iRegSrc < 32);

    return ((uint32_t)f64Bit       << 31)
         | UINT32_C(0x5ac00400)
         | (iRegSrc                <<  5)
         | iRegDst;
}


/**
 * A64: Encodes SETF8 & SETF16.
 *
 * @returns The encoded instruction.
 * @param   iRegResult  The register holding the result. SP is NOT valid.
 * @param   f16Bit      Set for SETF16, clear for SETF8.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrSetF8SetF16(uint32_t iRegResult, bool f16Bit)
{
    Assert(iRegResult < 32);

    return UINT32_C(0x3a00080d)
         | ((uint32_t)f16Bit       << 14)
         | (iRegResult             <<  5);
}


/**
 * A64: Encodes RMIF.
 *
 * @returns The encoded instruction.
 * @param   iRegSrc         The source register to get flags from.
 * @param   cRotateRight    The right rotate count (LSB bit offset).
 * @param   fMask           Mask of which flag bits to set:
 *                              - bit 0: V
 *                              - bit 1: C
 *                              - bit 2: Z
 *                              - bit 3: N
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrRmif(uint32_t iRegSrc, uint32_t cRotateRight, uint32_t fMask)
{
    Assert(iRegSrc < 32); Assert(cRotateRight < 64); Assert(fMask <= 0xf);

    return UINT32_C(0xba000400)
         | (cRotateRight << 15)
         | (iRegSrc      <<  5)
         | fMask;
}


/**
 * A64: Encodes MRS (for reading a system register into a GPR).
 *
 * @returns The encoded instruction.
 * @param   iRegDst     The register to put the result into. SP is NOT valid.
 * @param   idSysReg    The system register ID (ARMV8_AARCH64_SYSREG_XXX),
 *                      IPRT specific format, of the register to read.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrMrs(uint32_t iRegDst, uint32_t idSysReg)
{
    Assert(iRegDst < 32);
    Assert(idSysReg < RT_BIT_32(16) && (idSysReg & RT_BIT_32(15)));

    /* Note. The top bit of idSysReg must always be set and is also set in
             0xd5300000, otherwise we'll be encoding a different instruction. */
    return UINT32_C(0xd5300000)
         | (idSysReg << 5)
         | iRegDst;
}


/**
 * A64: Encodes MSR (for writing a GPR to a system register).
 *
 * @returns The encoded instruction.
 * @param   iRegSrc     The register which value to write. SP is NOT valid.
 * @param   idSysReg    The system register ID (ARMV8_AARCH64_SYSREG_XXX),
 *                      IPRT specific format, of the register to write.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkInstrMsr(uint32_t iRegSrc, uint32_t idSysReg)
{
    Assert(iRegSrc < 32);
    Assert(idSysReg < RT_BIT_32(16) && (idSysReg & RT_BIT_32(15)));

    /* Note. The top bit of idSysReg must always be set and is also set in
             0xd5100000, otherwise we'll be encoding a different instruction. */
    return UINT32_C(0xd5100000)
         | (idSysReg << 5)
         | iRegSrc;
}


/** @} */


/** @defgroup grp_rt_armv8_mkinstr_vec   Vector Instruction Encoding Helpers
 * @ingroup grp_rt_armv8_mkinstr
 *
 * A few inlined functions and macros for assisting in encoding common ARMv8
 * Neon/SIMD instructions.
 *
 * @{ */

/** Armv8 vector logical operation. */
typedef enum
{
    kArmv8VecInstrLogicOp_And = 0,                                             /**< AND */
    kArmv8VecInstrLogicOp_Bic =                                 RT_BIT_32(22), /**< BIC */
    kArmv8VecInstrLogicOp_Orr =                 RT_BIT_32(23),                 /**< ORR */
    kArmv8VecInstrLogicOp_Orn =                 RT_BIT_32(23) | RT_BIT_32(22), /**< ORN */
    kArmv8VecInstrLogicOp_Eor = RT_BIT_32(29),                                 /**< EOR */
    kArmv8VecInstrLogicOp_Bsl = RT_BIT_32(29) |                 RT_BIT_32(22), /**< BSL */
    kArmv8VecInstrLogicOp_Bit = RT_BIT_32(29) | RT_BIT_32(23),                 /**< BIT */
    kArmv8VecInstrLogicOp_Bif = RT_BIT_32(29) | RT_BIT_32(23) | RT_BIT_32(22)  /**< BIF */
} ARMV8INSTRVECLOGICOP;


/**
 * A64: Encodes logical instruction (vector, register).
 *
 * @returns The encoded instruction.
 * @param   enmOp       The operation to encode.
 * @param   iVecRegDst  The vector register to put the result into.
 * @param   iVecRegSrc1 The 1st source register.
 * @param   iVecRegSrc2 The 2nd source register.
 * @param   f128Bit     Flag whether this operates on the full 128-bit (true, default) of the vector register
 *                      or just the low 64-bit (false).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkVecInstrLogical(ARMV8INSTRVECLOGICOP enmOp, uint32_t iVecRegDst, uint32_t iVecRegSrc1, uint32_t iVecRegSrc2,
                                                      bool f128Bit = true)
{
    Assert(iVecRegDst < 32); Assert(iVecRegSrc1 < 32); Assert(iVecRegSrc2 < 32);

    return UINT32_C(0x0e201c00)
         | (uint32_t)enmOp
         | ((uint32_t)f128Bit << 30)
         | (iVecRegSrc2 << 16)
         | (iVecRegSrc1 << 5)
         | iVecRegDst;
}


/**
 * A64: Encodes ORR (vector, register).
 *
 * @returns The encoded instruction.
 * @param   iVecRegDst  The vector register to put the result into.
 * @param   iVecRegSrc1 The 1st source register.
 * @param   iVecRegSrc2 The 2nd source register.
 * @param   f128Bit     Flag whether this operates on the full 128-bit (true, default) of the vector register
 *                      or just the low 64-bit (false).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkVecInstrOrr(uint32_t iVecRegDst, uint32_t iVecRegSrc1, uint32_t iVecRegSrc2,
                                                  bool f128Bit = true)
{
    return Armv8A64MkVecInstrLogical(kArmv8VecInstrLogicOp_Orr, iVecRegDst, iVecRegSrc1, iVecRegSrc2, f128Bit);
}


/**
 * A64: Encodes EOR (vector, register).
 *
 * @returns The encoded instruction.
 * @param   iVecRegDst  The vector register to put the result into.
 * @param   iVecRegSrc1 The 1st source register.
 * @param   iVecRegSrc2 The 2nd source register.
 * @param   f128Bit     Flag whether this operates on the full 128-bit (true, default) of the vector register
 *                      or just the low 64-bit (false).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkVecInstrEor(uint32_t iVecRegDst, uint32_t iVecRegSrc1, uint32_t iVecRegSrc2,
                                                  bool f128Bit = true)
{
    return Armv8A64MkVecInstrLogical(kArmv8VecInstrLogicOp_Eor, iVecRegDst, iVecRegSrc1, iVecRegSrc2, f128Bit);
}


/**
 * A64: Encodes AND (vector, register).
 *
 * @returns The encoded instruction.
 * @param   iVecRegDst  The vector register to put the result into.
 * @param   iVecRegSrc1 The 1st source register.
 * @param   iVecRegSrc2 The 2nd source register.
 * @param   f128Bit     Flag whether this operates on the full 128-bit (true, default) of the vector register
 *                      or just the low 64-bit (false).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkVecInstrAnd(uint32_t iVecRegDst, uint32_t iVecRegSrc1, uint32_t iVecRegSrc2,
                                                  bool f128Bit = true)
{
    return Armv8A64MkVecInstrLogical(kArmv8VecInstrLogicOp_And, iVecRegDst, iVecRegSrc1, iVecRegSrc2, f128Bit);
}


/** Armv8 UMOV/INS vector element size.    */
typedef enum ARMV8INSTRUMOVINSSZ
{
    kArmv8InstrUmovInsSz_U8  = 0, /**< Byte. */
    kArmv8InstrUmovInsSz_U16 = 1, /**< Halfword. */
    kArmv8InstrUmovInsSz_U32 = 2, /**< 32-bit. */
    kArmv8InstrUmovInsSz_U64 = 3  /**< 64-bit (only valid when the destination is a 64-bit register. */
} ARMV8INSTRUMOVINSSZ;


/**
 * A64: Encodes UMOV (vector, register).
 *
 * @returns The encoded instruction.
 * @param   iRegDst     The register to put the result into.
 * @param   iVecRegSrc  The vector source register.
 * @param   idxElem     The element index.
 * @param   enmSz       Element size of the source vector register.
 * @param   fDst64Bit   Flag whether the destination register is 64-bit (true) or 32-bit (false).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkVecInstrUmov(uint32_t iRegDst, uint32_t iVecRegSrc, uint8_t idxElem,
                                                   ARMV8INSTRUMOVINSSZ enmSz = kArmv8InstrUmovInsSz_U64, bool fDst64Bit = true)
{
    Assert(iRegDst < 32); Assert(iVecRegSrc < 32);
    Assert((fDst64Bit && enmSz == kArmv8InstrUmovInsSz_U64) || (!fDst64Bit && enmSz != kArmv8InstrUmovInsSz_U64));
    Assert(   (enmSz == kArmv8InstrUmovInsSz_U8 && idxElem < 16)
           || (enmSz == kArmv8InstrUmovInsSz_U16 && idxElem < 8)
           || (enmSz == kArmv8InstrUmovInsSz_U32 && idxElem < 4)
           || (enmSz == kArmv8InstrUmovInsSz_U64 && idxElem < 2));

    return UINT32_C(0x0e003c00)
         | ((uint32_t)fDst64Bit << 30)
         | ((uint32_t)idxElem << (16 + enmSz + 1))
         | (RT_BIT_32(enmSz) << 16)
         | (iVecRegSrc << 5)
         | iRegDst;
}


/**
 * A64: Encodes INS (vector, register).
 *
 * @returns The encoded instruction.
 * @param   iVecRegDst  The vector register to put the result into.
 * @param   iRegSrc     The source register.
 * @param   idxElem     The element index for the destination.
 * @param   enmSz       Element size of the source vector register.
 *
 * @note This instruction assumes a 32-bit W<n> register for all non 64bit vector sizes.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkVecInstrIns(uint32_t iVecRegDst, uint32_t iRegSrc, uint8_t idxElem,
                                                  ARMV8INSTRUMOVINSSZ enmSz = kArmv8InstrUmovInsSz_U64)
{
    Assert(iRegSrc < 32); Assert(iVecRegDst < 32);
    Assert(   (enmSz == kArmv8InstrUmovInsSz_U8 && idxElem < 16)
           || (enmSz == kArmv8InstrUmovInsSz_U16 && idxElem < 8)
           || (enmSz == kArmv8InstrUmovInsSz_U32 && idxElem < 4)
           || (enmSz == kArmv8InstrUmovInsSz_U64 && idxElem < 2));

    return UINT32_C(0x4e001c00)
         | ((uint32_t)idxElem << (16 + enmSz + 1))
         | (RT_BIT_32(enmSz) << 16)
         | (iRegSrc << 5)
         | iVecRegDst;
}


/**
 * A64: Encodes DUP (vector, register).
 *
 * @returns The encoded instruction.
 * @param   iVecRegDst  The vector register to put the result into.
 * @param   iRegSrc     The source register (ZR is valid).
 * @param   enmSz       Element size of the source vector register.
 * @param   f128Bit     Flag whether the instruction operates on the whole 128-bit of the vector register (true) or
 *                      just the low 64-bit (false).
 *
 * @note This instruction assumes a 32-bit W<n> register for all non 64bit vector sizes.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkVecInstrDup(uint32_t iVecRegDst, uint32_t iRegSrc, ARMV8INSTRUMOVINSSZ enmSz,
                                                  bool f128Bit = true)
{
    Assert(iRegSrc < 32); Assert(iVecRegDst < 32);
    Assert(   (enmSz == kArmv8InstrUmovInsSz_U8)
           || (enmSz == kArmv8InstrUmovInsSz_U16)
           || (enmSz == kArmv8InstrUmovInsSz_U32)
           || (enmSz == kArmv8InstrUmovInsSz_U64));

    return UINT32_C(0x0e000c00)
         | ((uint32_t)f128Bit << 30)
         | (RT_BIT_32(enmSz) << 16)
         | (iRegSrc << 5)
         | iVecRegDst;
}


/** Armv8 vector compare to zero vector element size.    */
typedef enum ARMV8INSTRVECCMPZEROSZ
{
    kArmv8InstrCmpZeroSz_S8  = 0, /**< Byte. */
    kArmv8InstrCmpZeroSz_S16 = 1, /**< Halfword. */
    kArmv8InstrCmpZeroSz_S32 = 2, /**< 32-bit. */
    kArmv8InstrCmpZeroSz_S64 = 3  /**< 64-bit. */
} ARMV8INSTRVECCMPZEROSZ;


/** Armv8 vector compare to zero vector operation.    */
typedef enum ARMV8INSTRVECCMPZEROOP
{
    kArmv8InstrCmpZeroOp_Gt = 0,                            /**< Greater than. */
    kArmv8InstrCmpZeroOp_Ge = RT_BIT_32(29),                /**< Greater than or equal to. */
    kArmv8InstrCmpZeroOp_Eq = RT_BIT_32(12),                /**< Equal to. */
    kArmv8InstrCmpZeroOp_Le = RT_BIT_32(29) | RT_BIT_32(12) /**< Lower than or equal to. */
} ARMV8INSTRVECCMPZEROOP;


/**
 * A64: Encodes CMGT, CMGE, CMEQ or CMLE against zero (vector, register).
 *
 * @returns The encoded instruction.
 * @param   iVecRegDst  The vector register to put the result into.
 * @param   iVecRegSrc  The vector source register.
 * @param   enmSz       Vector element size.
 * @param   enmOp       The compare operation against to encode.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkVecInstrCmpToZero(uint32_t iVecRegDst, uint32_t iVecRegSrc, ARMV8INSTRVECCMPZEROSZ enmSz,
                                                        ARMV8INSTRVECCMPZEROOP enmOp)
{
    Assert(iVecRegDst < 32); Assert(iVecRegSrc < 32);

    return UINT32_C(0x5e208800)
         | ((uint32_t)enmSz << 22)
         | (RT_BIT_32(enmSz) << 16)
         | (iVecRegSrc << 5)
         | iVecRegDst
         | (uint32_t)enmOp;
}


/**
 * A64: Encodes CNT (vector, register).
 *
 * @returns The encoded instruction.
 * @param   iVecRegDst  The vector register to put the result into.
 * @param   iVecRegSrc  The vector source register.
 * @param   f128Bit     Flag whether this operates on the full 128-bit (true, default) of the vector register
 *                      or just the low 64-bit (false).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkVecInstrCnt(uint32_t iVecRegDst, uint32_t iVecRegSrc, bool f128Bit = true)
{
    Assert(iVecRegDst < 32); Assert(iVecRegSrc < 32);

    return UINT32_C(0x0e205800)
         | ((uint32_t)f128Bit << 30)
         | (iVecRegSrc << 5)
         | iVecRegDst;
}


/** Armv8 vector unsigned sum long across vector element size.    */
typedef enum ARMV8INSTRVECUADDLVSZ
{
    kArmv8InstrUAddLVSz_8B  = 0,                 /**<  8 x 8-bit. */
    kArmv8InstrUAddLVSz_16B = RT_BIT_32(30),     /**< 16 x 8-bit. */
    kArmv8InstrUAddLVSz_4H  = 1,                 /**<  4 x 16-bit. */
    kArmv8InstrUAddLVSz_8H  = RT_BIT_32(30) | 1, /**<  8 x 16-bit. */
    kArmv8InstrUAddLVSz_4S  = RT_BIT_32(30) | 2  /**<  4 x 32-bit. */
} ARMV8INSTRVECUADDLVSZ;


/**
 * A64: Encodes UADDLV (vector, register).
 *
 * @returns The encoded instruction.
 * @param   iVecRegDst  The vector register to put the result into.
 * @param   iVecRegSrc  The vector source register.
 * @param   enmSz       Element size.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkVecInstrUAddLV(uint32_t iVecRegDst, uint32_t iVecRegSrc, ARMV8INSTRVECUADDLVSZ enmSz)
{
    Assert(iVecRegDst < 32); Assert(iVecRegSrc < 32);

    return UINT32_C(0x2e303800)
         | ((uint32_t)enmSz)
         | (iVecRegSrc << 5)
         | iVecRegDst;
}


/** Armv8 USHR/USRA/URSRA/SSHR/SRSA/SSHR vector element size.    */
typedef enum ARMV8INSTRUSHIFTSZ
{
    kArmv8InstrShiftSz_U8  =  8,  /**< Byte. */
    kArmv8InstrShiftSz_U16 = 16,  /**< Halfword. */
    kArmv8InstrShiftSz_U32 = 32,  /**< 32-bit. */
    kArmv8InstrShiftSz_U64 = 64   /**< 64-bit. */
} ARMV8INSTRUSHIFTSZ;

/**
 * A64: Encodes USHR/USRA/URSRA/SSHR/SRSA/SSHR (vector, register).
 *
 * @returns The encoded instruction.
 * @param   iVecRegDst  The vector register to put the result into.
 * @param   iVecRegSrc  The vector source register.
 * @param   cShift      Number of bits to shift.
 * @param   enmSz       Element size.
 * @param   fUnsigned   Flag whether this a signed or unsigned shift,
 * @param   fRound      Flag whether this is the rounding shift variant.
 * @param   fAccum      Flag whether this is the accumulate shift variant.
 * @param   f128Bit     Flag whether this operates on the full 128-bit (true, default) of the vector register
 *                      or just the low 64-bit (false).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkVecInstrShrImm(uint32_t iVecRegDst, uint32_t iVecRegSrc, uint8_t cShift, ARMV8INSTRUSHIFTSZ enmSz,
                                                     bool fUnsigned = true, bool fRound = false, bool fAccum = false, bool f128Bit = true)
{
    Assert(iVecRegDst < 32); Assert(iVecRegSrc < 32);
    Assert(   cShift >= 1
           && (   (enmSz == kArmv8InstrShiftSz_U8 &&  cShift <= 8)
               || (enmSz == kArmv8InstrShiftSz_U16 && cShift <= 16)
               || (enmSz == kArmv8InstrShiftSz_U32 && cShift <= 32)
               || (enmSz == kArmv8InstrShiftSz_U64 && cShift <= 64)));

    return UINT32_C(0x0f000400)
         | ((uint32_t)f128Bit << 30)
         | ((uint32_t)fUnsigned << 29)
         | ((((uint32_t)enmSz << 1) - cShift) << 16)
         | ((uint32_t)fRound << 13)
         | ((uint32_t)fAccum << 12)
         | (iVecRegSrc << 5)
         | iVecRegDst;
}


/**
 * A64: Encodes SHL (vector, register).
 *
 * @returns The encoded instruction.
 * @param   iVecRegDst  The vector register to put the result into.
 * @param   iVecRegSrc  The vector source register.
 * @param   cShift      Number of bits to shift.
 * @param   enmSz       Element size.
 * @param   f128Bit     Flag whether this operates on the full 128-bit (true, default) of the vector register
 *                      or just the low 64-bit (false).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkVecInstrShlImm(uint32_t iVecRegDst, uint32_t iVecRegSrc, uint8_t cShift, ARMV8INSTRUSHIFTSZ enmSz,
                                                     bool f128Bit = true)
{
    Assert(iVecRegDst < 32); Assert(iVecRegSrc < 32);
    Assert(   (enmSz == kArmv8InstrShiftSz_U8 &&  cShift < 8)
           || (enmSz == kArmv8InstrShiftSz_U16 && cShift < 16)
           || (enmSz == kArmv8InstrShiftSz_U32 && cShift < 32)
           || (enmSz == kArmv8InstrShiftSz_U64 && cShift < 64));

    return UINT32_C(0x0f005400)
         | ((uint32_t)f128Bit << 30)
         | (((uint32_t)enmSz | cShift) << 16)
         | (iVecRegSrc << 5)
         | iVecRegDst;
}


/**
 * A64: Encodes SHLL/SHLL2/USHLL/USHLL2 (vector, register).
 *
 * @returns The encoded instruction.
 * @param   iVecRegDst  The vector register to put the result into.
 * @param   iVecRegSrc  The vector source register.
 * @param   cShift      Number of bits to shift.
 * @param   enmSz       Element size of the source vector register, the destination vector register
 *                      element size is twice as large, kArmv8InstrShiftSz_U64 is invalid.
 * @param   fUnsigned   Flag whether this is an unsigned shift left (true, default) or signed (false).
 * @param   fUpper      Flag whether this operates on the lower half (false, default) of the source vector register
 *                      or the upper half (true).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkVecInstrUShll(uint32_t iVecRegDst, uint32_t iVecRegSrc, uint8_t cShift, ARMV8INSTRUSHIFTSZ enmSz,
                                                    bool fUnsigned = true, bool fUpper = false)
{
    Assert(iVecRegDst < 32); Assert(iVecRegSrc < 32);
    Assert(   (enmSz == kArmv8InstrShiftSz_U8 &&  cShift < 8)
           || (enmSz == kArmv8InstrShiftSz_U16 && cShift < 16)
           || (enmSz == kArmv8InstrShiftSz_U32 && cShift < 32));

    return UINT32_C(0x0f00a400)
         | ((uint32_t)fUpper << 30)
         | ((uint32_t)fUnsigned << 29)
         | (((uint32_t)enmSz | cShift) << 16)
         | (iVecRegSrc << 5)
         | iVecRegDst;
}


/** Armv8 vector arith ops element size.    */
typedef enum ARMV8INSTRVECARITHSZ
{
    kArmv8VecInstrArithSz_8   = 0, /**<   8-bit. */
    kArmv8VecInstrArithSz_16  = 1, /**<  16-bit. */
    kArmv8VecInstrArithSz_32  = 2, /**<  32-bit. */
    kArmv8VecInstrArithSz_64  = 3  /**<  64-bit. */
} ARMV8INSTRVECARITHSZ;


/** Armv8 vector arithmetic operation. */
typedef enum
{
    kArmv8VecInstrArithOp_Add           =                 RT_BIT_32(15),                                               /**< ADD   */
    kArmv8VecInstrArithOp_Sub           = RT_BIT_32(29) | RT_BIT_32(15),                                               /**< SUB   */
    kArmv8VecInstrArithOp_UnsignSat_Add = RT_BIT_32(29) |                                               RT_BIT_32(11), /**< UQADD */
    kArmv8VecInstrArithOp_UnsignSat_Sub = RT_BIT_32(29) |                 RT_BIT_32(13)               | RT_BIT_32(11), /**< UQSUB */
    kArmv8VecInstrArithOp_SignSat_Add   =                                                               RT_BIT_32(11), /**< SQADD */
    kArmv8VecInstrArithOp_SignSat_Sub   =                                 RT_BIT_32(13)               | RT_BIT_32(11), /**< SQSUB */
    kArmv8VecInstrArithOp_Mul           =                 RT_BIT_32(15) |               RT_BIT_32(12) | RT_BIT_32(11)  /**< MUL   */
} ARMV8INSTRVECARITHOP;


/**
 * A64: Encodes an arithmetic operation (vector, register).
 *
 * @returns The encoded instruction.
 * @param   enmOp       The operation to encode.
 * @param   iVecRegDst  The vector register to put the result into.
 * @param   iVecRegSrc1 The first vector source register.
 * @param   iVecRegSrc2 The second vector source register.
 * @param   enmSz       Element size.
 * @param   f128Bit     Flag whether this operates on the full 128-bit (true, default) of the vector register
 *                      or just the low 64-bit (false).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkVecInstrArithOp(ARMV8INSTRVECARITHOP enmOp, uint32_t iVecRegDst, uint32_t iVecRegSrc1, uint32_t iVecRegSrc2,
                                                      ARMV8INSTRVECARITHSZ enmSz, bool f128Bit = true)
{
    Assert(iVecRegDst < 32); Assert(iVecRegSrc1 < 32); Assert(iVecRegSrc2 < 32);

    return UINT32_C(0x0e200400)
         | (uint32_t)enmOp
         | ((uint32_t)f128Bit << 30)
         | ((uint32_t)enmSz   << 22)
         | (iVecRegSrc2 << 16)
         | (iVecRegSrc1 << 5)
         | iVecRegDst;
}


/** Armv8 vector compare operation. */
typedef enum ARMV8VECINSTRCMPOP
{
                             /*   U           insn[15:10]     */
    kArmv8VecInstrCmpOp_Gt =                 UINT32_C(0x3400), /**< Greater than     (>)  (signed) */
    kArmv8VecInstrCmpOp_Ge =                 UINT32_C(0x3c00), /**< Greater or equal (>=) (signed) */
    kArmv8VecInstrCmpOp_Hi = RT_BIT_32(29) | UINT32_C(0x3400), /**< Greater than     (>)  (unsigned) */
    kArmv8VecInstrCmpOp_Hs = RT_BIT_32(29) | UINT32_C(0x3c00), /**< Greater or equal (>=) (unsigned) */
    kArmv8VecInstrCmpOp_Eq = RT_BIT_32(29) | UINT32_C(0x8c00)  /**< Equal            (==) (unsigned) */
} ARMV8VECINSTRCMPOP;

/**
 * A64: Encodes CMEQ/CMGE/CMGT/CMHI/CMHS (register variant) (vector, register).
 *
 * @returns The encoded instruction.
 * @param   enmOp       The operation to perform.
 * @param   iVecRegDst  The vector register to put the result into.
 * @param   iVecRegSrc1 The first vector source register.
 * @param   iVecRegSrc2 The second vector source register.
 * @param   enmSz       Element size.
 * @param   f128Bit     Flag whether this operates on the full 128-bit (true, default) of the vector register
 *                      or just the low 64-bit (false).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkVecInstrCmp(ARMV8VECINSTRCMPOP enmOp, uint32_t iVecRegDst, uint32_t iVecRegSrc1, uint32_t iVecRegSrc2,
                                                  ARMV8INSTRVECARITHSZ enmSz, bool f128Bit = true)
{
    Assert(iVecRegDst < 32); Assert(iVecRegSrc1 < 32); Assert(iVecRegSrc2 < 32);

    return UINT32_C(0x0e200000)
         | ((uint32_t)f128Bit << 30)
         | ((uint32_t)enmSz   << 22)
         | (iVecRegSrc2 << 16)
         | ((uint32_t)enmOp)
         | (iVecRegSrc1 << 5)
         | iVecRegDst;
}


/** Armv8 vector compare against zero operation. */
typedef enum ARMV8VECINSTRCMPZEROOP
{
                                  /*   U           insn[15:10]     */
    kArmv8VecInstrCmpZeroOp_Gt =                 UINT32_C(0x8800), /**< Greater than zero        (>)  (signed) */
    kArmv8VecInstrCmpZeroOp_Eq =                 UINT32_C(0x9800), /**< Equal to zero            (==) */
    kArmv8VecInstrCmpZeroOp_Lt =                 UINT32_C(0xa800), /**< Lower than zero          (>=) (signed) */
    kArmv8VecInstrCmpZeroOp_Ge = RT_BIT_32(29) | UINT32_C(0x8800), /**< Greater or equal to zero (>=) (signed) */
    kArmv8VecInstrCmpZeroOp_Le = RT_BIT_32(29) | UINT32_C(0x9800)  /**< Lower or equal to zero   (<=) (signed) */
} ARMV8VECINSTRCMPZEROOP;

/**
 * A64: Encodes CMEQ/CMGE/CMGT/CMLE/CMLT (zero variant) (vector, register).
 *
 * @returns The encoded instruction.
 * @param   enmOp       The operation to perform.
 * @param   iVecRegDst  The vector register to put the result into.
 * @param   iVecRegSrc  The first vector source register.
 * @param   enmSz       Element size.
 * @param   f128Bit     Flag whether this operates on the full 128-bit (true, default) of the vector register
 *                      or just the low 64-bit (false).
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkVecInstrCmpAgainstZero(ARMV8VECINSTRCMPOP enmOp, uint32_t iVecRegDst, uint32_t iVecRegSrc,
                                                             ARMV8INSTRVECARITHSZ enmSz, bool f128Bit = true)
{
    Assert(iVecRegDst < 32); Assert(iVecRegSrc < 32);

    return UINT32_C(0x0e200000)
         | ((uint32_t)f128Bit << 30)
         | ((uint32_t)enmSz   << 22)
         | ((uint32_t)enmOp)
         | (iVecRegSrc << 5)
         | iVecRegDst;
}


/** Armv8 [Signed,Unsigned] Extract {Unsigned} operation. */
typedef enum
{
    kArmv8VecInstrQxtnOp_Sqxtn  =                 RT_BIT_32(14),                /**< SQXTN  */
    kArmv8VecInstrQxtnOp_Sqxtun = RT_BIT_32(29) |                RT_BIT_32(13), /**< SQXTUN */
    kArmv8VecInstrQxtnOp_Uqxtn  = RT_BIT_32(29) | RT_BIT_32(14)                 /**< UQXTN  */
} ARMV8INSTRVECQXTNOP;

/**
 * A64: Encodes SQXTN/SQXTN2/UQXTN/UQXTN2/SQXTUN/SQXTUN2 (vector, register).
 *
 * @returns The encoded instruction.
 * @param   enmOp       The operation to perform.
 * @param   fUpper      Flag whether to write the result to the lower (false) or upper (true) half of the destinatiom register.
 * @param   iVecRegDst  The vector register to put the result into.
 * @param   iVecRegSrc  The first vector source register.
 * @param   enmSz       Element size.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkVecInstrQxtn(ARMV8INSTRVECQXTNOP enmOp, bool fUpper, uint32_t iVecRegDst, uint32_t iVecRegSrc, ARMV8INSTRVECARITHSZ enmSz)
{
    Assert(iVecRegDst < 32); Assert(iVecRegSrc < 32);

    return UINT32_C(0x0e210800)
         | ((uint32_t)enmOp)
         | ((uint32_t)fUpper    << 30)
         | ((uint32_t)enmSz     << 22)
         | (iVecRegSrc << 5)
         | iVecRegDst;
}


/** Armv8 floating point size. */
typedef enum
{
    kArmv8VecInstrFpSz_2x_Single = 0,                             /**< 2x single precision values in the low 64-bit of the 128-bit register. */
    kArmv8VecInstrFpSz_4x_Single = RT_BIT_32(30),                 /**< 4x single precision values in the 128-bit register. */
    kArmv8VecInstrFpSz_2x_Double = RT_BIT_32(30) | RT_BIT_32(22)  /**< 2x double precision values in the 128-bit register. */
} ARMV8INSTRVECFPSZ;


/** Armv8 3 operand floating point operation. */
typedef enum
{
                                        /*   insn[29]          insn[23]      insn[15:11]     */
    kArmv8VecInstrFpOp_Add               =                                 UINT32_C(0xd000), /**< FADD    */
    kArmv8VecInstrFpOp_Sub               =                 RT_BIT_32(23) | UINT32_C(0xd000), /**< FADD    */
    kArmv8VecInstrFpOp_AddPairwise       = RT_BIT_32(29) |                 UINT32_C(0xd000), /**< FADDP   */
    kArmv8VecInstrFpOp_Mul               = RT_BIT_32(29) |                 UINT32_C(0xd800), /**< FMUL    */
    kArmv8VecInstrFpOp_Div               = RT_BIT_32(29) |                 UINT32_C(0xf800), /**< FDIV    */

    kArmv8VecInstrFpOp_Max               =                                 UINT32_C(0xf000), /**< FMAX    */
    kArmv8VecInstrFpOp_MaxNumber         =                                 UINT32_C(0xc000), /**< FMAXNM  */
    kArmv8VecInstrFpOp_MaxNumberPairwise = RT_BIT_32(29) |                 UINT32_C(0xc000), /**< FMAXNMP */
    kArmv8VecInstrFpOp_MaxPairwise       = RT_BIT_32(29) |                 UINT32_C(0xf000), /**< FMAXP   */

    kArmv8VecInstrFpOp_Min               =                 RT_BIT_32(23) | UINT32_C(0xf000), /**< FMIN    */
    kArmv8VecInstrFpOp_MinNumber         =                 RT_BIT_32(23) | UINT32_C(0xc000), /**< FMINNM  */
    kArmv8VecInstrFpOp_MinNumberPairwise = RT_BIT_32(29) | RT_BIT_32(23) | UINT32_C(0xc000), /**< FMINNMP */
    kArmv8VecInstrFpOp_MinPairwise       = RT_BIT_32(29) | RT_BIT_32(23) | UINT32_C(0xf000), /**< FMINP   */

    kArmv8VecInstrFpOp_Fmla              =                                 UINT32_C(0xc800), /**< FMLA    */
    kArmv8VecInstrFpOp_Fmls              =                 RT_BIT_32(23) | UINT32_C(0xc800)  /**< FMLS    */
} ARMV8INSTRVECFPOP;

/**
 * A64: Encodes a 3 operand floating point operation (vector, register).
 *
 * @returns The encoded instruction.
 * @param   enmOp       The operation to perform.
 * @param   enmSz       The size to operate on.
 * @param   iVecRegDst  The vector register to put the result into.
 * @param   iVecRegSrc1 The first vector source register.
 * @param   iVecRegSrc2 The second vector source register.
 */
DECL_FORCE_INLINE(uint32_t) Armv8A64MkVecInstrFp3Op(ARMV8INSTRVECFPOP enmOp, ARMV8INSTRVECFPSZ enmSz, uint32_t iVecRegDst,
                                                    uint32_t iVecRegSrc1, uint32_t iVecRegSrc2)
{
    Assert(iVecRegDst < 32); Assert(iVecRegSrc1 < 32); Assert(iVecRegSrc2 < 32);

    return UINT32_C(0x0e200400)
         | ((uint32_t)enmOp)
         | ((uint32_t)enmSz)
         | (iVecRegSrc2 << 16)
         | (iVecRegSrc1 << 5)
         | iVecRegDst;
}


/** @} */

#endif /* !dtrace && __cplusplus */

/** @} */

#endif /* !IPRT_INCLUDED_armv8_h */

