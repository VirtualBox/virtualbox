/* $Id: NEMR3Native-linux-armv8.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * NEM - Native execution manager, native ring-3 Linux backend arm64 version.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_NEM
#define VMCPU_INCL_CPUM_GST_CTX
#include <VBox/vmm/nem.h>
#include <VBox/vmm/iem.h>
#include <VBox/vmm/em.h>
#include <VBox/vmm/gic.h>
#include <VBox/vmm/pdm.h>
#include <VBox/vmm/trpm.h>
#include "NEMInternal.h"
#include <VBox/vmm/vmcc.h>

#include <iprt/alloca.h>
#include <iprt/string.h>
#include <iprt/system.h>
#include <iprt/armv8.h>

#include <iprt/formats/arm-psci.h>

#include <errno.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <sys/mman.h>
#include <linux/kvm.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** Core register group. */
#define KVM_ARM64_REG_CORE_GROUP  UINT64_C(0x6030000000100000)
/** System register group. */
#define KVM_ARM64_REG_SYS_GROUP   UINT64_C(0x6030000000130000)
/** System register group. */
#define KVM_ARM64_REG_SIMD_GROUP  UINT64_C(0x6040000000100050)
/** FP register group. */
#define KVM_ARM64_REG_FP_GROUP    UINT64_C(0x6020000000100000)

#define KVM_ARM64_REG_CORE_CREATE(a_idReg)   (KVM_ARM64_REG_CORE_GROUP | ((uint64_t)(a_idReg) & 0xffff))
#define KVM_ARM64_REG_GPR(a_iGpr)            KVM_ARM64_REG_CORE_CREATE((a_iGpr) << 1)
#define KVM_ARM64_REG_SP_EL0                 KVM_ARM64_REG_CORE_CREATE(0x3e)
#define KVM_ARM64_REG_PC                     KVM_ARM64_REG_CORE_CREATE(0x40)
#define KVM_ARM64_REG_PSTATE                 KVM_ARM64_REG_CORE_CREATE(0x42)
#define KVM_ARM64_REG_SP_EL1                 KVM_ARM64_REG_CORE_CREATE(0x44)
#define KVM_ARM64_REG_ELR_EL1                KVM_ARM64_REG_CORE_CREATE(0x46)
#define KVM_ARM64_REG_SPSR_EL1               KVM_ARM64_REG_CORE_CREATE(0x48)
#define KVM_ARM64_REG_SPSR_ABT               KVM_ARM64_REG_CORE_CREATE(0x4a)
#define KVM_ARM64_REG_SPSR_UND               KVM_ARM64_REG_CORE_CREATE(0x4c)
#define KVM_ARM64_REG_SPSR_IRQ               KVM_ARM64_REG_CORE_CREATE(0x4e)
#define KVM_ARM64_REG_SPSR_FIQ               KVM_ARM64_REG_CORE_CREATE(0x50)

/** This maps to our IPRT representation of system register IDs, yay! */
#define KVM_ARM64_REG_SYS_CREATE(a_idSysReg) (KVM_ARM64_REG_SYS_GROUP  | ((uint64_t)(a_idSysReg) & 0xffff))

#define KVM_ARM64_REG_SIMD_CREATE(a_iVecReg) (KVM_ARM64_REG_SIMD_GROUP | (((uint64_t)(a_iVecReg) << 2) & 0xffff))

#define KVM_ARM64_REG_FP_CREATE(a_idReg)     (KVM_ARM64_REG_FP_GROUP   | ((uint64_t)(a_idReg) & 0xffff))
#define KVM_ARM64_REG_FP_FPSR                KVM_ARM64_REG_FP_CREATE(0xd4)
#define KVM_ARM64_REG_FP_FPCR                KVM_ARM64_REG_FP_CREATE(0xd5)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The general registers. */
static const struct
{
    uint64_t    idKvmReg;
    uint32_t    fCpumExtrn;
    uint32_t    offCpumCtx;
} s_aCpumRegs[] =
{
#define CPUM_GREG_EMIT_X0_X3(a_Idx)  { KVM_ARM64_REG_GPR(a_Idx), CPUMCTX_EXTRN_X ## a_Idx, RT_UOFFSETOF(CPUMCTX, aGRegs[a_Idx].x) }
#define CPUM_GREG_EMIT_X4_X28(a_Idx) { KVM_ARM64_REG_GPR(a_Idx), CPUMCTX_EXTRN_X4_X28,     RT_UOFFSETOF(CPUMCTX, aGRegs[a_Idx].x) }
    CPUM_GREG_EMIT_X0_X3(0),
    CPUM_GREG_EMIT_X0_X3(1),
    CPUM_GREG_EMIT_X0_X3(2),
    CPUM_GREG_EMIT_X0_X3(3),
    CPUM_GREG_EMIT_X4_X28(4),
    CPUM_GREG_EMIT_X4_X28(5),
    CPUM_GREG_EMIT_X4_X28(6),
    CPUM_GREG_EMIT_X4_X28(7),
    CPUM_GREG_EMIT_X4_X28(8),
    CPUM_GREG_EMIT_X4_X28(9),
    CPUM_GREG_EMIT_X4_X28(10),
    CPUM_GREG_EMIT_X4_X28(11),
    CPUM_GREG_EMIT_X4_X28(12),
    CPUM_GREG_EMIT_X4_X28(13),
    CPUM_GREG_EMIT_X4_X28(14),
    CPUM_GREG_EMIT_X4_X28(15),
    CPUM_GREG_EMIT_X4_X28(16),
    CPUM_GREG_EMIT_X4_X28(17),
    CPUM_GREG_EMIT_X4_X28(18),
    CPUM_GREG_EMIT_X4_X28(19),
    CPUM_GREG_EMIT_X4_X28(20),
    CPUM_GREG_EMIT_X4_X28(21),
    CPUM_GREG_EMIT_X4_X28(22),
    CPUM_GREG_EMIT_X4_X28(23),
    CPUM_GREG_EMIT_X4_X28(24),
    CPUM_GREG_EMIT_X4_X28(25),
    CPUM_GREG_EMIT_X4_X28(26),
    CPUM_GREG_EMIT_X4_X28(27),
    CPUM_GREG_EMIT_X4_X28(28),
    { KVM_ARM64_REG_GPR(29), CPUMCTX_EXTRN_FP,   RT_UOFFSETOF(CPUMCTX, aGRegs[29].x) },
    { KVM_ARM64_REG_GPR(30), CPUMCTX_EXTRN_LR,   RT_UOFFSETOF(CPUMCTX, aGRegs[30].x) },
    { KVM_ARM64_REG_PC,      CPUMCTX_EXTRN_PC,   RT_UOFFSETOF(CPUMCTX, Pc.u64)       },
#undef CPUM_GREG_EMIT_X0_X3
#undef CPUM_GREG_EMIT_X4_X28
};
/** SIMD/FP registers. */
static const struct
{
    uint64_t    idKvmReg;
    uint32_t    offCpumCtx;
} s_aCpumFpRegs[] =
{
#define CPUM_VREG_EMIT(a_Idx)  { KVM_ARM64_REG_SIMD_CREATE(a_Idx), RT_UOFFSETOF(CPUMCTX, aVRegs[a_Idx].v) }
    CPUM_VREG_EMIT(0),
    CPUM_VREG_EMIT(1),
    CPUM_VREG_EMIT(2),
    CPUM_VREG_EMIT(3),
    CPUM_VREG_EMIT(4),
    CPUM_VREG_EMIT(5),
    CPUM_VREG_EMIT(6),
    CPUM_VREG_EMIT(7),
    CPUM_VREG_EMIT(8),
    CPUM_VREG_EMIT(9),
    CPUM_VREG_EMIT(10),
    CPUM_VREG_EMIT(11),
    CPUM_VREG_EMIT(12),
    CPUM_VREG_EMIT(13),
    CPUM_VREG_EMIT(14),
    CPUM_VREG_EMIT(15),
    CPUM_VREG_EMIT(16),
    CPUM_VREG_EMIT(17),
    CPUM_VREG_EMIT(18),
    CPUM_VREG_EMIT(19),
    CPUM_VREG_EMIT(20),
    CPUM_VREG_EMIT(21),
    CPUM_VREG_EMIT(22),
    CPUM_VREG_EMIT(23),
    CPUM_VREG_EMIT(24),
    CPUM_VREG_EMIT(25),
    CPUM_VREG_EMIT(26),
    CPUM_VREG_EMIT(27),
    CPUM_VREG_EMIT(28),
    CPUM_VREG_EMIT(29),
    CPUM_VREG_EMIT(30),
    CPUM_VREG_EMIT(31)
#undef CPUM_VREG_EMIT
};
/** System registers. */
static const struct
{
    uint64_t    idKvmReg;
    uint32_t    fCpumExtrn;
    uint32_t    offCpumCtx;
} s_aCpumSysRegs[] =
{
    { KVM_ARM64_REG_SP_EL0,                                          CPUMCTX_EXTRN_SP,               RT_UOFFSETOF(CPUMCTX, aSpReg[0].u64)    },
    { KVM_ARM64_REG_SP_EL1,                                          CPUMCTX_EXTRN_SP,               RT_UOFFSETOF(CPUMCTX, aSpReg[1].u64)    },
    { KVM_ARM64_REG_SPSR_EL1,                                        CPUMCTX_EXTRN_SPSR,             RT_UOFFSETOF(CPUMCTX, Spsr.u64)         },
    { KVM_ARM64_REG_ELR_EL1,                                         CPUMCTX_EXTRN_ELR,              RT_UOFFSETOF(CPUMCTX, Elr.u64)          },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_SCTRL_EL1),      CPUMCTX_EXTRN_SCTLR_TCR_TTBR,   RT_UOFFSETOF(CPUMCTX, Sctlr.u64)        },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_TCR_EL1),        CPUMCTX_EXTRN_SCTLR_TCR_TTBR,   RT_UOFFSETOF(CPUMCTX, Tcr.u64)          },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_TTBR0_EL1),      CPUMCTX_EXTRN_SCTLR_TCR_TTBR,   RT_UOFFSETOF(CPUMCTX, Ttbr0.u64)        },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_TTBR1_EL1),      CPUMCTX_EXTRN_SCTLR_TCR_TTBR,   RT_UOFFSETOF(CPUMCTX, Ttbr1.u64)        },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_VBAR_EL1),       CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, VBar.u64)         },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_AFSR0_EL1),      CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Afsr0.u64)        },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_AFSR1_EL1),      CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Afsr1.u64)        },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_AMAIR_EL1),      CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Amair.u64)        },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_CNTKCTL_EL1),    CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, CntKCtl.u64)      },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_CONTEXTIDR_EL1), CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, ContextIdr.u64)   },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_CPACR_EL1),      CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Cpacr.u64)        },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_CSSELR_EL1),     CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Csselr.u64)       },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_ESR_EL1),        CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Esr.u64)          },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_FAR_EL1),        CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Far.u64)          },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_MAIR_EL1),       CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Mair.u64)         },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_PAR_EL1),        CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, Par.u64)          },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_TPIDRRO_EL0),    CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, TpIdrRoEl0.u64)   },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_TPIDR_EL0),      CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, aTpIdr[0].u64)    },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_TPIDR_EL1),      CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, aTpIdr[1].u64)    },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_MDCCINT_EL1),    CPUMCTX_EXTRN_SYSREG_MISC,      RT_UOFFSETOF(CPUMCTX, MDccInt.u64)      }
};
/** Debug system registers. */
static const struct
{
    uint64_t    idKvmReg;
    uint32_t    offCpumCtx;
} s_aCpumDbgRegs[] =
{
#define CPUM_DBGREG_EMIT(a_BorW, a_Idx) \
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_DBG ## a_BorW ## CRn_EL1(a_Idx)), RT_UOFFSETOF(CPUMCTX, a ## a_BorW ## p[a_Idx].Ctrl.u64) }, \
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_DBG ## a_BorW ## VRn_EL1(a_Idx)), RT_UOFFSETOF(CPUMCTX, a ## a_BorW ## p[a_Idx].Value.u64) }
    /* Breakpoint registers. */
    CPUM_DBGREG_EMIT(B, 0),
    CPUM_DBGREG_EMIT(B, 1),
    CPUM_DBGREG_EMIT(B, 2),
    CPUM_DBGREG_EMIT(B, 3),
    CPUM_DBGREG_EMIT(B, 4),
    CPUM_DBGREG_EMIT(B, 5),
    CPUM_DBGREG_EMIT(B, 6),
    CPUM_DBGREG_EMIT(B, 7),
    CPUM_DBGREG_EMIT(B, 8),
    CPUM_DBGREG_EMIT(B, 9),
    CPUM_DBGREG_EMIT(B, 10),
    CPUM_DBGREG_EMIT(B, 11),
    CPUM_DBGREG_EMIT(B, 12),
    CPUM_DBGREG_EMIT(B, 13),
    CPUM_DBGREG_EMIT(B, 14),
    CPUM_DBGREG_EMIT(B, 15),
    /* Watchpoint registers. */
    CPUM_DBGREG_EMIT(W, 0),
    CPUM_DBGREG_EMIT(W, 1),
    CPUM_DBGREG_EMIT(W, 2),
    CPUM_DBGREG_EMIT(W, 3),
    CPUM_DBGREG_EMIT(W, 4),
    CPUM_DBGREG_EMIT(W, 5),
    CPUM_DBGREG_EMIT(W, 6),
    CPUM_DBGREG_EMIT(W, 7),
    CPUM_DBGREG_EMIT(W, 8),
    CPUM_DBGREG_EMIT(W, 9),
    CPUM_DBGREG_EMIT(W, 10),
    CPUM_DBGREG_EMIT(W, 11),
    CPUM_DBGREG_EMIT(W, 12),
    CPUM_DBGREG_EMIT(W, 13),
    CPUM_DBGREG_EMIT(W, 14),
    CPUM_DBGREG_EMIT(W, 15),
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_MDSCR_EL1), RT_UOFFSETOF(CPUMCTX, Mdscr.u64) }
#undef CPUM_DBGREG_EMIT
};
/** PAuth key system registers. */
static const struct
{
    uint64_t    idKvmReg;
    uint32_t    offCpumCtx;
} s_aCpumPAuthKeyRegs[] =
{
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_APDAKeyLo_EL1), RT_UOFFSETOF(CPUMCTX, Apda.Low.u64)  },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_APDAKeyHi_EL1), RT_UOFFSETOF(CPUMCTX, Apda.High.u64) },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_APDBKeyLo_EL1), RT_UOFFSETOF(CPUMCTX, Apdb.Low.u64)  },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_APDBKeyHi_EL1), RT_UOFFSETOF(CPUMCTX, Apdb.High.u64) },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_APGAKeyLo_EL1), RT_UOFFSETOF(CPUMCTX, Apga.Low.u64)  },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_APGAKeyHi_EL1), RT_UOFFSETOF(CPUMCTX, Apga.High.u64) },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_APIAKeyLo_EL1), RT_UOFFSETOF(CPUMCTX, Apia.Low.u64)  },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_APIAKeyHi_EL1), RT_UOFFSETOF(CPUMCTX, Apia.High.u64) },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_APIBKeyLo_EL1), RT_UOFFSETOF(CPUMCTX, Apib.Low.u64)  },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_APIBKeyHi_EL1), RT_UOFFSETOF(CPUMCTX, Apib.High.u64) }
};
/** ID registers. */
static const struct
{
    uint64_t         idKvmReg;
    uint32_t         offIdStruct;
} s_aIdRegs[] =
{
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_ID_AA64DFR0_EL1),       RT_UOFFSETOF(CPUMIDREGS, u64RegIdAa64Dfr0El1)  },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_ID_AA64DFR1_EL1),       RT_UOFFSETOF(CPUMIDREGS, u64RegIdAa64Dfr1El1)  },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_ID_AA64ISAR0_EL1),      RT_UOFFSETOF(CPUMIDREGS, u64RegIdAa64Isar0El1) },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_ID_AA64ISAR1_EL1),      RT_UOFFSETOF(CPUMIDREGS, u64RegIdAa64Isar1El1) },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_ID_AA64MMFR0_EL1),      RT_UOFFSETOF(CPUMIDREGS, u64RegIdAa64Mmfr0El1) },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_ID_AA64MMFR1_EL1),      RT_UOFFSETOF(CPUMIDREGS, u64RegIdAa64Mmfr1El1) },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_ID_AA64MMFR2_EL1),      RT_UOFFSETOF(CPUMIDREGS, u64RegIdAa64Mmfr2El1) },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_ID_AA64PFR0_EL1),       RT_UOFFSETOF(CPUMIDREGS, u64RegIdAa64Pfr0El1)  },
    { KVM_ARM64_REG_SYS_CREATE(ARMV8_AARCH64_SYSREG_ID_AA64PFR1_EL1),       RT_UOFFSETOF(CPUMIDREGS, u64RegIdAa64Pfr1El1)  }
};



/* Forward declarations of things called by the template. */
static int nemR3LnxInitSetupVm(PVM pVM, PRTERRINFO pErrInfo);


/* Instantiate the common bits we share with the x86 KVM backend. */
#include "NEMR3NativeTemplate-linux.cpp.h"


/**
 * Queries and logs the supported register list from KVM.
 *
 * @returns VBox status code.
 * @param   fdVCpu              The file descriptor number of vCPU 0.
 */
static int nemR3LnxLogRegList(int fdVCpu)
{
    struct KVM_REG_LIST
    {
        uint64_t cRegs;
        uint64_t aRegs[1024];
    } RegList; RT_ZERO(RegList);

    RegList.cRegs = RT_ELEMENTS(RegList.aRegs);
    int rcLnx = ioctl(fdVCpu, KVM_GET_REG_LIST, &RegList);
    if (rcLnx != 0)
        return RTErrConvertFromErrno(errno);

    LogRel(("NEM: KVM vCPU registers:\n"));

    for (uint32_t i = 0; i < RegList.cRegs; i++)
        LogRel(("NEM:   %36s: %#RX64\n", "Unknown" /** @todo */, RegList.aRegs[i]));

    return VINF_SUCCESS;
}


/**
 * Sets the given attribute in KVM to the given value.
 *
 * @returns VBox status code.
 * @param   pVM             The VM instance.
 * @param   u32Grp          The device attribute group being set.
 * @param   u32Attr         The actual attribute inside the group being set.
 * @param   pvAttrVal       Where the attribute value to set.
 * @param   pszAttribute    Attribute description for logging.
 * @param   pErrInfo        Optional error information.
 */
static int nemR3LnxSetAttribute(PVM pVM, uint32_t u32Grp, uint32_t u32Attr, const void *pvAttrVal, const char *pszAttribute,
                                PRTERRINFO pErrInfo)
{
    struct kvm_device_attr DevAttr;

    DevAttr.flags = 0;
    DevAttr.group = u32Grp;
    DevAttr.attr  = u32Attr;
    DevAttr.addr  = (uintptr_t)pvAttrVal;
    int rcLnx = ioctl(pVM->nem.s.fdVm, KVM_HAS_DEVICE_ATTR, &DevAttr);
    if (rcLnx < 0)
        return RTErrInfoSetF(pErrInfo, RTErrConvertFromErrno(errno),
                             N_("KVM error: KVM doesn't support setting the attribute \"%s\" (%d)"),
                             pszAttribute, errno);

    rcLnx = ioctl(pVM->nem.s.fdVm, KVM_SET_DEVICE_ATTR, &DevAttr);
    if (rcLnx < 0)
        return RTErrInfoSetF(pErrInfo, RTErrConvertFromErrno(errno),
                             N_("KVM error: Setting the attribute \"%s\" for KVM failed (%d)"),
                             pszAttribute, errno);

    return VINF_SUCCESS;
}


DECL_FORCE_INLINE(int) nemR3LnxKvmSetQueryReg(PVMCPUCC pVCpu, bool fQuery, uint64_t idKvmReg, const void *pv)
{
    struct kvm_one_reg Reg;
    Reg.id   = idKvmReg;
    Reg.addr = (uintptr_t)pv;

    /*
     * Who thought that this API was a good idea? Supporting to query/set just one register
     * at a time is horribly inefficient.
     */
    int rcLnx = ioctl(pVCpu->nem.s.fdVCpu, fQuery ? KVM_GET_ONE_REG : KVM_SET_ONE_REG, &Reg);
    if (!rcLnx)
        return 0;

    return RTErrConvertFromErrno(-rcLnx);
}

DECLINLINE(int) nemR3LnxKvmQueryRegU64(PVMCPUCC pVCpu, uint64_t idKvmReg, uint64_t *pu64)
{
    return nemR3LnxKvmSetQueryReg(pVCpu, true /*fQuery*/, idKvmReg, pu64);
}


DECLINLINE(int) nemR3LnxKvmQueryRegU32(PVMCPUCC pVCpu, uint64_t idKvmReg, uint32_t *pu32)
{
    return nemR3LnxKvmSetQueryReg(pVCpu, true /*fQuery*/, idKvmReg, pu32);
}


DECLINLINE(int) nemR3LnxKvmQueryRegPV(PVMCPUCC pVCpu, uint64_t idKvmReg, void *pv)
{
    return nemR3LnxKvmSetQueryReg(pVCpu, true /*fQuery*/, idKvmReg, pv);
}


DECLINLINE(int) nemR3LnxKvmSetRegU64(PVMCPUCC pVCpu, uint64_t idKvmReg, const uint64_t *pu64)
{
    return nemR3LnxKvmSetQueryReg(pVCpu, false /*fQuery*/, idKvmReg, pu64);
}


DECLINLINE(int) nemR3LnxKvmSetRegU32(PVMCPUCC pVCpu, uint64_t idKvmReg, const uint32_t *pu32)
{
    return nemR3LnxKvmSetQueryReg(pVCpu, false /*fQuery*/, idKvmReg, pu32);
}


DECLINLINE(int) nemR3LnxKvmSetRegPV(PVMCPUCC pVCpu, uint64_t idKvmReg, const void *pv)
{
    return nemR3LnxKvmSetQueryReg(pVCpu, false /*fQuery*/, idKvmReg, pv);
}


/**
 * Does the early setup of a KVM VM.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 * @param   pErrInfo            Where to always return error info.
 */
static int nemR3LnxInitSetupVm(PVM pVM, PRTERRINFO pErrInfo)
{
    AssertReturn(pVM->nem.s.fdVm != -1, RTErrInfoSet(pErrInfo, VERR_WRONG_ORDER, "Wrong initalization order"));

    /*
     * Create the VCpus.
     */
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];

        /* Create it. */
        pVCpu->nem.s.fdVCpu = ioctl(pVM->nem.s.fdVm, KVM_CREATE_VCPU, (unsigned long)idCpu);
        if (pVCpu->nem.s.fdVCpu < 0)
            return RTErrInfoSetF(pErrInfo, VERR_NEM_VM_CREATE_FAILED, "KVM_CREATE_VCPU failed for VCpu #%u: %d", idCpu, errno);

        /* Map the KVM_RUN area. */
        pVCpu->nem.s.pRun = (struct kvm_run *)mmap(NULL, pVM->nem.s.cbVCpuMmap, PROT_READ | PROT_WRITE, MAP_SHARED,
                                                   pVCpu->nem.s.fdVCpu, 0 /*offset*/);
        if ((void *)pVCpu->nem.s.pRun == MAP_FAILED)
            return RTErrInfoSetF(pErrInfo, VERR_NEM_VM_CREATE_FAILED, "mmap failed for VCpu #%u: %d", idCpu, errno);

        /* Initialize the vCPU. */
        struct kvm_vcpu_init VCpuInit; RT_ZERO(VCpuInit);
        VCpuInit.target = KVM_ARM_TARGET_GENERIC_V8;
        /** @todo Enable features. */
        if (ioctl(pVCpu->nem.s.fdVCpu, KVM_ARM_VCPU_INIT, &VCpuInit) != 0)
            return RTErrInfoSetF(pErrInfo, VERR_NEM_VM_CREATE_FAILED, "KVM_ARM_VCPU_INIT failed for VCpu #%u: %d", idCpu, errno);

#if 0
        uint32_t fFeatures = 0; /** @todo SVE */
        if (ioctl(pVCpu->nem.s.fdVCpu, KVM_ARM_VCPU_FINALIZE, &fFeatures) != 0)
            return RTErrInfoSetF(pErrInfo, VERR_NEM_VM_CREATE_FAILED, "KVM_ARM_VCPU_FINALIZE failed for VCpu #%u: %d", idCpu, errno);
#endif

        if (idCpu == 0)
        {
            /* Query the supported register list and log it. */
            int rc = nemR3LnxLogRegList(pVCpu->nem.s.fdVCpu);
            if (RT_FAILURE(rc))
                return RTErrInfoSetF(pErrInfo, VERR_NEM_VM_CREATE_FAILED, "Querying the supported register list failed with %Rrc", rc);

            /* Need to query the ID registers and populate CPUM. */
            CPUMIDREGS IdRegs; RT_ZERO(IdRegs);
            for (uint32_t i = 0; i < RT_ELEMENTS(s_aIdRegs); i++)
            {
                uint64_t *pu64 = (uint64_t *)((uint8_t *)&IdRegs + s_aIdRegs[i].offIdStruct);
                rc = nemR3LnxKvmQueryRegU64(pVCpu, s_aIdRegs[i].idKvmReg, pu64);
                if (RT_FAILURE(rc))
                    return VMSetError(pVM, VERR_NEM_VM_CREATE_FAILED, RT_SRC_POS,
                                      "Querying register %#x failed: %Rrc", s_aIdRegs[i].idKvmReg, rc);
            }

            rc = CPUMR3PopulateFeaturesByIdRegisters(pVM, &IdRegs);
            if (RT_FAILURE(rc))
                return rc;
        }
    }

    /*
     * Setup the SMCCC filter to get exits for PSCI related
     * guest calls (to support SMP, power off and reset).
     */
    struct kvm_smccc_filter SmcccPsciFilter; RT_ZERO(SmcccPsciFilter);
    SmcccPsciFilter.base         = ARM_PSCI_FUNC_ID_CREATE_FAST_64(ARM_PSCI_FUNC_ID_PSCI_VERSION);
    SmcccPsciFilter.nr_functions = ARM_PSCI_FUNC_ID_CREATE_FAST_64(ARM_PSCI_FUNC_ID_SYSTEM_RESET) - SmcccPsciFilter.base + 1;
    SmcccPsciFilter.action       = KVM_SMCCC_FILTER_FWD_TO_USER;
    int rc = nemR3LnxSetAttribute(pVM, KVM_ARM_VM_SMCCC_CTRL, KVM_ARM_VM_SMCCC_FILTER, &SmcccPsciFilter,
                                  "KVM_ARM_VM_SMCCC_FILTER", pErrInfo);
    if (RT_FAILURE(rc))
        return rc;

    SmcccPsciFilter.base         = ARM_PSCI_FUNC_ID_CREATE_FAST_32(ARM_PSCI_FUNC_ID_PSCI_VERSION);
    SmcccPsciFilter.nr_functions = ARM_PSCI_FUNC_ID_CREATE_FAST_32(ARM_PSCI_FUNC_ID_SYSTEM_RESET) - SmcccPsciFilter.base + 1;
    SmcccPsciFilter.action       = KVM_SMCCC_FILTER_FWD_TO_USER;
    rc = nemR3LnxSetAttribute(pVM, KVM_ARM_VM_SMCCC_CTRL, KVM_ARM_VM_SMCCC_FILTER, &SmcccPsciFilter,
                              "KVM_ARM_VM_SMCCC_FILTER", pErrInfo);
    if (RT_FAILURE(rc))
        return rc;

    return VINF_SUCCESS;
}


int nemR3NativeInitCompleted(PVM pVM, VMINITCOMPLETED enmWhat)
{
    /*
     * Make RTThreadPoke work again (disabled for avoiding unnecessary
     * critical section issues in ring-0).
     */
    if (enmWhat == VMINITCOMPLETED_RING3)
        VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE, nemR3LnxFixThreadPoke, NULL);

    return VINF_SUCCESS;
}


/*********************************************************************************************************************************
*   CPU State                                                                                                                    *
*********************************************************************************************************************************/

/**
 * Sets the given general purpose register to the given value.
 *
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 * @param   uReg            The register index.
 * @param   f64BitReg       Flag whether to operate on a 64-bit or 32-bit register.
 * @param   fSignExtend     Flag whether to sign extend the value.
 * @param   u64Val          The value.
 */
DECLINLINE(void) nemR3LnxSetGReg(PVMCPU pVCpu, uint8_t uReg, bool f64BitReg, bool fSignExtend, uint64_t u64Val)
{
    AssertReturnVoid(uReg < 31);

    if (f64BitReg)
        pVCpu->cpum.GstCtx.aGRegs[uReg].x = fSignExtend ? (int64_t)u64Val : u64Val;
    else
        pVCpu->cpum.GstCtx.aGRegs[uReg].w = fSignExtend ? (int32_t)u64Val : u64Val; /** @todo Does this clear the upper half on real hardware? */

    /* Mark the register as not extern anymore. */
    switch (uReg)
    {
        case 0:
            pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_X0;
            break;
        case 1:
            pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_X1;
            break;
        case 2:
            pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_X2;
            break;
        case 3:
            pVCpu->cpum.GstCtx.fExtrn &= ~CPUMCTX_EXTRN_X3;
            break;
        default:
            AssertRelease(!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_X4_X28));
            /** @todo We need to import all missing registers in order to clear this flag (or just set it in HV from here). */
    }
}


/**
 * Gets the given general purpose register and returns the value.
 *
 * @returns Value from the given register.
 * @param   pVCpu           The cross context virtual CPU structure of the
 *                          calling EMT.
 * @param   uReg            The register index.
 */
DECLINLINE(uint64_t) nemR3LnxGetGReg(PVMCPU pVCpu, uint8_t uReg)
{
    AssertReturn(uReg <= ARMV8_AARCH64_REG_ZR, 0);

    if (uReg == ARMV8_AARCH64_REG_ZR)
        return 0;

    /** @todo Import the register if extern. */
    //AssertRelease(!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_GPRS_MASK));

    return pVCpu->cpum.GstCtx.aGRegs[uReg].x;
}

/**
 * Worker that imports selected state from KVM.
 */
static int nemHCLnxImportState(PVMCPUCC pVCpu, uint64_t fWhat, PCPUMCTX pCtx)
{
    fWhat &= pVCpu->cpum.GstCtx.fExtrn;
    if (!fWhat)
        return VINF_SUCCESS;

#if 0
    hv_return_t hrc = hv_vcpu_get_sys_reg(pVCpu->nem.s.hVCpu, HV_SYS_REG_CNTV_CTL_EL0, &pVCpu->cpum.GstCtx.CntvCtlEl0);
    if (hrc == HV_SUCCESS)
        hrc = hv_vcpu_get_sys_reg(pVCpu->nem.s.hVCpu, HV_SYS_REG_CNTV_CVAL_EL0, &pVCpu->cpum.GstCtx.CntvCValEl0);
#endif

    int rc = VINF_SUCCESS;
    if (fWhat & (CPUMCTX_EXTRN_GPRS_MASK | CPUMCTX_EXTRN_FP | CPUMCTX_EXTRN_LR | CPUMCTX_EXTRN_PC))
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumRegs); i++)
        {
            if (s_aCpumRegs[i].fCpumExtrn & fWhat)
            {
                uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumRegs[i].offCpumCtx);
                rc |= nemR3LnxKvmQueryRegU64(pVCpu, s_aCpumRegs[i].idKvmReg, pu64);
            }
        }
    }

    if (   rc == VINF_SUCCESS
        && (fWhat & CPUMCTX_EXTRN_FPCR))
    {
        uint32_t u32Tmp;
        rc |= nemR3LnxKvmQueryRegU32(pVCpu, KVM_ARM64_REG_FP_FPCR, &u32Tmp);
        if (rc == VINF_SUCCESS)
            pVCpu->cpum.GstCtx.fpcr = u32Tmp;
    }

    if (   rc == VINF_SUCCESS
        && (fWhat & CPUMCTX_EXTRN_FPSR))
    {
        uint32_t u32Tmp;
        rc |= nemR3LnxKvmQueryRegU32(pVCpu, KVM_ARM64_REG_FP_FPSR, &u32Tmp);
        if (rc == VINF_SUCCESS)
            pVCpu->cpum.GstCtx.fpsr = u32Tmp;
    }

    if (   rc == VINF_SUCCESS
        && (fWhat & CPUMCTX_EXTRN_V0_V31))
    {
        /* SIMD/FP registers. */
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumFpRegs); i++)
        {
            void *pu128 = (void *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumFpRegs[i].offCpumCtx);
            rc |= nemR3LnxKvmQueryRegPV(pVCpu, s_aCpumFpRegs[i].idKvmReg, pu128);
        }
    }

    if (   rc == VINF_SUCCESS
        && (fWhat & CPUMCTX_EXTRN_SYSREG_DEBUG))
    {
        /* Debug registers. */
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumDbgRegs); i++)
        {
            uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumDbgRegs[i].offCpumCtx);
            rc |= nemR3LnxKvmQueryRegU64(pVCpu, s_aCpumDbgRegs[i].idKvmReg, pu64);
        }
    }

    if (   rc == VINF_SUCCESS
        && (fWhat & CPUMCTX_EXTRN_SYSREG_PAUTH_KEYS))
    {
        /* PAuth registers. */
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumPAuthKeyRegs); i++)
        {
            uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumPAuthKeyRegs[i].offCpumCtx);
            rc |= nemR3LnxKvmQueryRegU64(pVCpu, s_aCpumPAuthKeyRegs[i].idKvmReg, pu64);
        }
    }

    if (   rc == VINF_SUCCESS
        && (fWhat & (CPUMCTX_EXTRN_SPSR | CPUMCTX_EXTRN_ELR | CPUMCTX_EXTRN_SP | CPUMCTX_EXTRN_SCTLR_TCR_TTBR | CPUMCTX_EXTRN_SYSREG_MISC)))
    {
        /* System registers. */
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumSysRegs); i++)
        {
            if (s_aCpumSysRegs[i].fCpumExtrn & fWhat)
            {
                uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumSysRegs[i].offCpumCtx);
                rc |= nemR3LnxKvmQueryRegU64(pVCpu, s_aCpumSysRegs[i].idKvmReg, pu64);
            }
        }
    }

    if (   rc == VINF_SUCCESS
        && (fWhat & CPUMCTX_EXTRN_PSTATE))
    {
        uint64_t u64Tmp;
        rc |= nemR3LnxKvmQueryRegU64(pVCpu, KVM_ARM64_REG_PSTATE, &u64Tmp);
        if (rc == VINF_SUCCESS)
            pVCpu->cpum.GstCtx.fPState = (uint32_t)u64Tmp;

    }

    /*
     * Update the external mask.
     */
    pCtx->fExtrn &= ~fWhat;
    pVCpu->cpum.GstCtx.fExtrn &= ~fWhat;
    if (!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_ALL))
        pVCpu->cpum.GstCtx.fExtrn = 0;

    return VINF_SUCCESS;
}


/**
 * Interface for importing state on demand (used by IEM).
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context CPU structure.
 * @param   fWhat       What to import, CPUMCTX_EXTRN_XXX.
 */
VMM_INT_DECL(int) NEMImportStateOnDemand(PVMCPUCC pVCpu, uint64_t fWhat)
{
    STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnDemand);
    return nemHCLnxImportState(pVCpu, fWhat, &pVCpu->cpum.GstCtx);
}


/**
 * Exports state to KVM.
 */
static int nemHCLnxExportState(PVM pVM, PVMCPU pVCpu, PCPUMCTX pCtx)
{
    uint64_t const fExtrn = ~pCtx->fExtrn & CPUMCTX_EXTRN_ALL;
    Assert((~fExtrn & CPUMCTX_EXTRN_ALL) != CPUMCTX_EXTRN_ALL); RT_NOREF(fExtrn);

    RT_NOREF(pVM);
    int rc = VINF_SUCCESS;
    if (   (pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_GPRS_MASK | CPUMCTX_EXTRN_FP | CPUMCTX_EXTRN_LR | CPUMCTX_EXTRN_PC))
        !=                              (CPUMCTX_EXTRN_GPRS_MASK | CPUMCTX_EXTRN_FP | CPUMCTX_EXTRN_LR | CPUMCTX_EXTRN_PC))
    {
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumRegs); i++)
        {
            if (!(s_aCpumRegs[i].fCpumExtrn & pVCpu->cpum.GstCtx.fExtrn))
            {
                const uint64_t *pu64 = (const uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumRegs[i].offCpumCtx);
                rc |= nemR3LnxKvmSetRegU64(pVCpu, s_aCpumRegs[i].idKvmReg, pu64);
            }
        }
    }

    if (   rc == VINF_SUCCESS
        && !(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_FPCR))
    {
        uint32_t u32Tmp = pVCpu->cpum.GstCtx.fpcr;
        rc |= nemR3LnxKvmSetRegU32(pVCpu, KVM_ARM64_REG_FP_FPCR, &u32Tmp);
    }

    if (   rc == VINF_SUCCESS
        && !(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_FPSR))
    {
        uint32_t u32Tmp = pVCpu->cpum.GstCtx.fpsr;
        rc |= nemR3LnxKvmSetRegU32(pVCpu, KVM_ARM64_REG_FP_FPSR, &u32Tmp);
    }

    if (   rc == VINF_SUCCESS
        && !(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_V0_V31))
    {
        /* SIMD/FP registers. */
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumFpRegs); i++)
        {
            void *pu128 = (void *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumFpRegs[i].offCpumCtx);
            rc |= nemR3LnxKvmSetRegPV(pVCpu, s_aCpumFpRegs[i].idKvmReg, pu128);
        }
    }

    if (   rc == VINF_SUCCESS
        && !(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_SYSREG_DEBUG))
    {
        /* Debug registers. */
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumDbgRegs); i++)
        {
            uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumDbgRegs[i].offCpumCtx);
            rc |= nemR3LnxKvmSetRegU64(pVCpu, s_aCpumDbgRegs[i].idKvmReg, pu64);
        }
    }

    if (   rc == VINF_SUCCESS
        && !(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_SYSREG_PAUTH_KEYS))
    {
        /* PAuth registers. */
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumPAuthKeyRegs); i++)
        {
            uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumPAuthKeyRegs[i].offCpumCtx);
            rc |= nemR3LnxKvmSetRegU64(pVCpu, s_aCpumPAuthKeyRegs[i].idKvmReg, pu64);
        }
    }

    if (   rc == VINF_SUCCESS
        &&     (pVCpu->cpum.GstCtx.fExtrn & (CPUMCTX_EXTRN_SPSR | CPUMCTX_EXTRN_ELR | CPUMCTX_EXTRN_SP | CPUMCTX_EXTRN_SCTLR_TCR_TTBR | CPUMCTX_EXTRN_SYSREG_MISC))
            !=                              (CPUMCTX_EXTRN_SPSR | CPUMCTX_EXTRN_ELR | CPUMCTX_EXTRN_SP | CPUMCTX_EXTRN_SCTLR_TCR_TTBR | CPUMCTX_EXTRN_SYSREG_MISC))
    {
        /* System registers. */
        for (uint32_t i = 0; i < RT_ELEMENTS(s_aCpumSysRegs); i++)
        {
            if (!(s_aCpumSysRegs[i].fCpumExtrn & pVCpu->cpum.GstCtx.fExtrn))
            {
                uint64_t *pu64 = (uint64_t *)((uint8_t *)&pVCpu->cpum.GstCtx + s_aCpumSysRegs[i].offCpumCtx);
                rc |= nemR3LnxKvmSetRegU64(pVCpu, s_aCpumSysRegs[i].idKvmReg, pu64);
            }
        }
    }

    if (   rc == VINF_SUCCESS
        && !(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_PSTATE))
    {
        uint64_t u64Tmp = pVCpu->cpum.GstCtx.fPState;
        rc = nemR3LnxKvmSetRegU64(pVCpu, KVM_ARM64_REG_PSTATE, &u64Tmp);
    }

    /*
     * KVM now owns all the state.
     */
    pCtx->fExtrn = CPUMCTX_EXTRN_KEEPER_NEM | CPUMCTX_EXTRN_ALL;
    return VINF_SUCCESS;
}


/**
 * Query the CPU tick counter and optionally the TSC_AUX MSR value.
 *
 * @returns VBox status code.
 * @param   pVCpu       The cross context CPU structure.
 * @param   pcTicks     Where to return the CPU tick count.
 * @param   puAux       Where to return the TSC_AUX register value.
 */
VMM_INT_DECL(int) NEMHCQueryCpuTick(PVMCPUCC pVCpu, uint64_t *pcTicks, uint32_t *puAux)
{
    STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatQueryCpuTick);
    // KVM_GET_CLOCK?
    RT_NOREF(pVCpu, pcTicks, puAux);
    return VINF_SUCCESS;
}


/**
 * Resumes CPU clock (TSC) on all virtual CPUs.
 *
 * This is called by TM when the VM is started, restored, resumed or similar.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context CPU structure of the calling EMT.
 * @param   uPausedTscValue The TSC value at the time of pausing.
 */
VMM_INT_DECL(int) NEMHCResumeCpuTickOnAll(PVMCC pVM, PVMCPUCC pVCpu, uint64_t uPausedTscValue)
{
    // KVM_SET_CLOCK?
    RT_NOREF(pVM, pVCpu, uPausedTscValue);
    return VINF_SUCCESS;
}


VMM_INT_DECL(uint32_t) NEMHCGetFeatures(PVMCC pVM)
{
    RT_NOREF(pVM);
    return NEM_FEAT_F_NESTED_PAGING
         | NEM_FEAT_F_FULL_GST_EXEC;
}



/*********************************************************************************************************************************
*   Execution                                                                                                                    *
*********************************************************************************************************************************/


VMMR3_INT_DECL(bool) NEMR3CanExecuteGuest(PVM pVM, PVMCPU pVCpu)
{
    RT_NOREF(pVM, pVCpu);
    Assert(VM_IS_NEM_ENABLED(pVM));
    return true;
}


bool nemR3NativeSetSingleInstruction(PVM pVM, PVMCPU pVCpu, bool fEnable)
{
    NOREF(pVM); NOREF(pVCpu); NOREF(fEnable);
    return false;
}


void nemR3NativeNotifyFF(PVM pVM, PVMCPU pVCpu, uint32_t fFlags)
{
    int rc = RTThreadPoke(pVCpu->hThread);
    LogFlow(("nemR3NativeNotifyFF: #%u -> %Rrc\n", pVCpu->idCpu, rc));
    AssertRC(rc);
    RT_NOREF(pVM, fFlags);
}


DECLHIDDEN(bool) nemR3NativeNotifyDebugEventChanged(PVM pVM, bool fUseDebugLoop)
{
    RT_NOREF(pVM, fUseDebugLoop);
    return false;
}


DECLHIDDEN(bool) nemR3NativeNotifyDebugEventChangedPerCpu(PVM pVM, PVMCPU pVCpu, bool fUseDebugLoop)
{
    RT_NOREF(pVM, pVCpu, fUseDebugLoop);
    return false;
}


DECL_FORCE_INLINE(int) nemR3LnxKvmUpdateIntrState(PVM pVM, PVMCPU pVCpu, bool fIrq, bool fAsserted)
{
    struct kvm_irq_level IrqLvl;

    LogFlowFunc(("pVM=%p pVCpu=%p fIrq=%RTbool fAsserted=%RTbool\n",
                 pVM, pVCpu, fIrq, fAsserted));

    IrqLvl.irq   =   ((uint32_t)KVM_ARM_IRQ_TYPE_CPU << 24)        /* Directly drives CPU interrupt lines. */
                   | (pVCpu->idCpu & 0xff) << 16
                   | (fIrq ? 0 : 1);
    IrqLvl.level = fAsserted ? 1 : 0;
    int rcLnx = ioctl(pVM->nem.s.fdVm, KVM_IRQ_LINE, &IrqLvl);
    AssertReturn(rcLnx == 0, VERR_NEM_IPE_9);

    return VINF_SUCCESS;
}


/**
 * Deals with pending interrupt FFs prior to executing guest code.
 */
static VBOXSTRICTRC nemHCLnxHandleInterruptFF(PVM pVM, PVMCPU pVCpu)
{
    LogFlowFunc(("pVCpu=%p{.idCpu=%u} fIrq=%RTbool fFiq=%RTbool\n",
                 pVCpu, pVCpu->idCpu,
                 VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_IRQ),
                 VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_FIQ)));

    bool fIrq = VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_IRQ);
    bool fFiq = VMCPU_FF_IS_SET(pVCpu, VMCPU_FF_INTERRUPT_FIQ);

    /* Update the pending interrupt state. */
    if (fIrq != pVCpu->nem.s.fIrqLastSeen)
    {
        int rc = nemR3LnxKvmUpdateIntrState(pVM, pVCpu, true /*fIrq*/, fIrq);
        AssertRCReturn(rc, VERR_NEM_IPE_9);
        pVCpu->nem.s.fIrqLastSeen = fIrq;
    }

    if (fFiq != pVCpu->nem.s.fIrqLastSeen)
    {
        int rc = nemR3LnxKvmUpdateIntrState(pVM, pVCpu, false /*fIrq*/, fFiq);
        AssertRCReturn(rc, VERR_NEM_IPE_9);
        pVCpu->nem.s.fFiqLastSeen = fFiq;
    }

    return VINF_SUCCESS;
}


#if 0
/**
 * Handles KVM_EXIT_INTERNAL_ERROR.
 */
static VBOXSTRICTRC nemR3LnxHandleInternalError(PVMCPU pVCpu, struct kvm_run *pRun)
{
    Log(("NEM: KVM_EXIT_INTERNAL_ERROR! suberror=%#x (%d) ndata=%u data=%.*Rhxs\n", pRun->internal.suberror,
         pRun->internal.suberror, pRun->internal.ndata, sizeof(pRun->internal.data), &pRun->internal.data[0]));

    /*
     * Deal with each suberror, returning if we don't want IEM to handle it.
     */
    switch (pRun->internal.suberror)
    {
        case KVM_INTERNAL_ERROR_EMULATION:
        {
            EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_INTERNAL_ERROR_EMULATION),
                             pRun->s.regs.regs.rip + pRun->s.regs.sregs.cs.base, ASMReadTSC());
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitInternalErrorEmulation);
            break;
        }

        case KVM_INTERNAL_ERROR_SIMUL_EX:
        case KVM_INTERNAL_ERROR_DELIVERY_EV:
        case KVM_INTERNAL_ERROR_UNEXPECTED_EXIT_REASON:
        default:
        {
            EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_INTERNAL_ERROR_FATAL),
                             pRun->s.regs.regs.rip + pRun->s.regs.sregs.cs.base, ASMReadTSC());
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitInternalErrorFatal);
            const char *pszName;
            switch (pRun->internal.suberror)
            {
                case KVM_INTERNAL_ERROR_EMULATION:              pszName = "KVM_INTERNAL_ERROR_EMULATION"; break;
                case KVM_INTERNAL_ERROR_SIMUL_EX:               pszName = "KVM_INTERNAL_ERROR_SIMUL_EX"; break;
                case KVM_INTERNAL_ERROR_DELIVERY_EV:            pszName = "KVM_INTERNAL_ERROR_DELIVERY_EV"; break;
                case KVM_INTERNAL_ERROR_UNEXPECTED_EXIT_REASON: pszName = "KVM_INTERNAL_ERROR_UNEXPECTED_EXIT_REASON"; break;
                default:                                        pszName = "unknown"; break;
            }
            LogRel(("NEM: KVM_EXIT_INTERNAL_ERROR! suberror=%#x (%s) ndata=%u data=%.*Rhxs\n", pRun->internal.suberror, pszName,
                    pRun->internal.ndata, sizeof(pRun->internal.data), &pRun->internal.data[0]));
            return VERR_NEM_IPE_0;
        }
    }

    /*
     * Execute instruction in IEM and try get on with it.
     */
    Log2(("nemR3LnxHandleInternalError: Executing instruction at %04x:%08RX64 in IEM\n",
          pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip));
    VBOXSTRICTRC rcStrict = nemHCLnxImportState(pVCpu,
                                                IEM_CPUMCTX_EXTRN_MUST_MASK | CPUMCTX_EXTRN_INHIBIT_INT
                                                 | CPUMCTX_EXTRN_INHIBIT_NMI,
                                                &pVCpu->cpum.GstCtx, pRun);
    if (RT_SUCCESS(rcStrict))
        rcStrict = IEMExecOne(pVCpu);
    return rcStrict;
}
#endif


/**
 * Handles KVM_EXIT_MMIO.
 */
static VBOXSTRICTRC nemHCLnxHandleExitMmio(PVMCC pVM, PVMCPUCC pVCpu, struct kvm_run *pRun)
{
    /*
     * Input validation.
     */
    Assert(pRun->mmio.len <= sizeof(pRun->mmio.data));
    Assert(pRun->mmio.is_write <= 1);

#if 0
    /*
     * We cannot easily act on the exit history here, because the MMIO port
     * exit is stateful and the instruction will be completed in the next
     * KVM_RUN call.  There seems no way to circumvent this.
     */
    EMHistoryAddExit(pVCpu,
                     pRun->mmio.is_write
                     ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MMIO_WRITE)
                     : EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MMIO_READ),
                     pRun->s.regs.regs.pc, ASMReadTSC());
#else
    RT_NOREF(pVCpu);
#endif

    /*
     * Do the requested job.
     */
    VBOXSTRICTRC rcStrict;
    if (pRun->mmio.is_write)
    {
        rcStrict = PGMPhysWrite(pVM, pRun->mmio.phys_addr, pRun->mmio.data, pRun->mmio.len, PGMACCESSORIGIN_HM);
        Log4(("MmioExit/%u:WRITE %#x LB %u, %.*Rhxs -> rcStrict=%Rrc\n",
              pVCpu->idCpu,
              pRun->mmio.phys_addr, pRun->mmio.len, pRun->mmio.len, pRun->mmio.data, VBOXSTRICTRC_VAL(rcStrict) ));
    }
    else
    {
        rcStrict = PGMPhysRead(pVM, pRun->mmio.phys_addr, pRun->mmio.data, pRun->mmio.len, PGMACCESSORIGIN_HM);
        Log4(("MmioExit/%u: READ %#x LB %u -> %.*Rhxs rcStrict=%Rrc\n",
              pVCpu->idCpu,
              pRun->mmio.phys_addr, pRun->mmio.len, pRun->mmio.len, pRun->mmio.data, VBOXSTRICTRC_VAL(rcStrict) ));
    }
    return rcStrict;
}


/**
 * Handles KVM_EXIT_HYPERCALL.
 */
static VBOXSTRICTRC nemHCLnxHandleExitHypercall(PVMCC pVM, PVMCPUCC pVCpu, struct kvm_run *pRun)
{
#if 0
    /*
     * We cannot easily act on the exit history here, because the MMIO port
     * exit is stateful and the instruction will be completed in the next
     * KVM_RUN call.  There seems no way to circumvent this.
     */
    EMHistoryAddExit(pVCpu,
                     pRun->mmio.is_write
                     ? EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MMIO_WRITE)
                     : EMEXIT_MAKE_FT(EMEXIT_F_KIND_EM, EMEXITTYPE_MMIO_READ),
                     pRun->s.regs.regs.pc, ASMReadTSC());
#else
    RT_NOREF(pVCpu);
#endif

    /*
     * Do the requested job.
     */
    VBOXSTRICTRC rcStrict = VINF_SUCCESS;

    /** @todo Raise exception to EL1 if PSCI not configured. */
    /** @todo Need a generic mechanism here to pass this to, GIM maybe?. */
    uint32_t uFunId = pRun->hypercall.nr;
    bool fHvc64 = RT_BOOL(uFunId & ARM_SMCCC_FUNC_ID_64BIT); RT_NOREF(fHvc64);
    uint32_t uEntity = ARM_SMCCC_FUNC_ID_ENTITY_GET(uFunId);
    uint32_t uFunNum = ARM_SMCCC_FUNC_ID_NUM_GET(uFunId);
    if (uEntity == ARM_SMCCC_FUNC_ID_ENTITY_STD_SEC_SERVICE)
    {
        rcStrict = nemHCLnxImportState(pVCpu, CPUMCTX_EXTRN_X0 | CPUMCTX_EXTRN_X1 | CPUMCTX_EXTRN_X2 | CPUMCTX_EXTRN_X3,
                                       &pVCpu->cpum.GstCtx);
        if (rcStrict != VINF_SUCCESS)
            return rcStrict;

        switch (uFunNum)
        {
            case ARM_PSCI_FUNC_ID_PSCI_VERSION:
                nemR3LnxSetGReg(pVCpu, ARMV8_AARCH64_REG_X0, false /*f64BitReg*/, false /*fSignExtend*/, ARM_PSCI_FUNC_ID_PSCI_VERSION_SET(1, 2));
                break;
            case ARM_PSCI_FUNC_ID_SYSTEM_OFF:
                rcStrict = VMR3PowerOff(pVM->pUVM);
                break;
            case ARM_PSCI_FUNC_ID_SYSTEM_RESET:
            case ARM_PSCI_FUNC_ID_SYSTEM_RESET2:
            {
                bool fHaltOnReset;
                int rc = CFGMR3QueryBool(CFGMR3GetChild(CFGMR3GetRoot(pVM), "PDM"), "HaltOnReset", &fHaltOnReset);
                if (RT_SUCCESS(rc) && fHaltOnReset)
                {
                    Log(("nemHCLnxHandleExitHypercall: Halt On Reset!\n"));
                    rcStrict = VINF_EM_HALT;
                }
                else
                {
                    /** @todo pVM->pdm.s.fResetFlags = fFlags; */
                    VM_FF_SET(pVM, VM_FF_RESET);
                    rcStrict = VINF_EM_RESET;
                }
                break;
            }
            case ARM_PSCI_FUNC_ID_CPU_ON:
            {
                uint64_t u64TgtCpu      = nemR3LnxGetGReg(pVCpu, ARMV8_AARCH64_REG_X1);
                RTGCPHYS GCPhysExecAddr = nemR3LnxGetGReg(pVCpu, ARMV8_AARCH64_REG_X2);
                uint64_t u64CtxId       = nemR3LnxGetGReg(pVCpu, ARMV8_AARCH64_REG_X3);
                VMMR3CpuOn(pVM, u64TgtCpu & 0xff, GCPhysExecAddr, u64CtxId);
                nemR3LnxSetGReg(pVCpu, ARMV8_AARCH64_REG_X0, true /*f64BitReg*/, false /*fSignExtend*/, ARM_PSCI_STS_SUCCESS);
                break;
            }
            case ARM_PSCI_FUNC_ID_PSCI_FEATURES:
            {
                uint32_t u32FunNum = (uint32_t)nemR3LnxGetGReg(pVCpu, ARMV8_AARCH64_REG_X1);
                switch (u32FunNum)
                {
                    case ARM_PSCI_FUNC_ID_PSCI_VERSION:
                    case ARM_PSCI_FUNC_ID_SYSTEM_OFF:
                    case ARM_PSCI_FUNC_ID_SYSTEM_RESET:
                    case ARM_PSCI_FUNC_ID_SYSTEM_RESET2:
                    case ARM_PSCI_FUNC_ID_CPU_ON:
                        nemR3LnxSetGReg(pVCpu, ARMV8_AARCH64_REG_X0,
                                        false /*f64BitReg*/, false /*fSignExtend*/,
                                        (uint64_t)ARM_PSCI_STS_SUCCESS);
                        break;
                    default:
                        nemR3LnxSetGReg(pVCpu, ARMV8_AARCH64_REG_X0,
                                        false /*f64BitReg*/, false /*fSignExtend*/,
                                        (uint64_t)ARM_PSCI_STS_NOT_SUPPORTED);
                }
                break;
            }
            default:
                nemR3LnxSetGReg(pVCpu, ARMV8_AARCH64_REG_X0, false /*f64BitReg*/, false /*fSignExtend*/, (uint64_t)ARM_PSCI_STS_NOT_SUPPORTED);
        }
    }
    else
        nemR3LnxSetGReg(pVCpu, ARMV8_AARCH64_REG_X0, false /*f64BitReg*/, false /*fSignExtend*/, (uint64_t)ARM_PSCI_STS_NOT_SUPPORTED);


    return rcStrict;
}


static VBOXSTRICTRC nemHCLnxHandleExit(PVMCC pVM, PVMCPUCC pVCpu, struct kvm_run *pRun, bool *pfStatefulExit)
{
    STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitTotal);

    if (pVCpu->nem.s.fIrqDeviceLvls != pRun->s.regs.device_irq_level)
    {
        uint64_t fChanged = pVCpu->nem.s.fIrqDeviceLvls ^ pRun->s.regs.device_irq_level;

        if (fChanged & KVM_ARM_DEV_EL1_VTIMER)
        {
            TMCpuSetVTimerNextActivation(pVCpu, UINT64_MAX);
            GICPpiSet(pVCpu, pVM->nem.s.u32GicPpiVTimer, RT_BOOL(pRun->s.regs.device_irq_level & KVM_ARM_DEV_EL1_VTIMER));
        }

        if (fChanged & KVM_ARM_DEV_EL1_PTIMER)
        {
            //TMCpuSetVTimerNextActivation(pVCpu, UINT64_MAX);
            GICPpiSet(pVCpu, pVM->nem.s.u32GicPpiVTimer, RT_BOOL(pRun->s.regs.device_irq_level & KVM_ARM_DEV_EL1_PTIMER));
        }

        pVCpu->nem.s.fIrqDeviceLvls = pRun->s.regs.device_irq_level;
    }

    switch (pRun->exit_reason)
    {
        case KVM_EXIT_EXCEPTION:
            AssertFailed();
            break;

        case KVM_EXIT_MMIO:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitMmio);
            *pfStatefulExit = true;
            return nemHCLnxHandleExitMmio(pVM, pVCpu, pRun);

        case KVM_EXIT_INTR: /* EINTR */
            //EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_INTERRUPTED),
            //                 pRun->s.regs.regs.pc, ASMReadTSC());
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitIntr);
            Log5(("Intr/%u\n", pVCpu->idCpu));
            return VINF_SUCCESS;

        case KVM_EXIT_HYPERCALL:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitHypercall);
            return nemHCLnxHandleExitHypercall(pVM, pVCpu, pRun);

#if 0
        case KVM_EXIT_DEBUG:
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatExitDebug);
            AssertFailed();
            break;

        case KVM_EXIT_SYSTEM_EVENT:
            AssertFailed();
            break;

        case KVM_EXIT_DIRTY_RING_FULL:
            AssertFailed();
            break;
        case KVM_EXIT_AP_RESET_HOLD:
            AssertFailed();
            break;


        case KVM_EXIT_SHUTDOWN:
            AssertFailed();
            break;

        case KVM_EXIT_FAIL_ENTRY:
            LogRel(("NEM: KVM_EXIT_FAIL_ENTRY! hardware_entry_failure_reason=%#x cpu=%#x\n",
                    pRun->fail_entry.hardware_entry_failure_reason, pRun->fail_entry.cpu));
            EMHistoryAddExit(pVCpu, EMEXIT_MAKE_FT(EMEXIT_F_KIND_NEM, NEMEXITTYPE_FAILED_ENTRY),
                             pRun->s.regs.regs.pc, ASMReadTSC());
            return VERR_NEM_IPE_1;

        case KVM_EXIT_INTERNAL_ERROR:
            /* we're counting sub-reasons inside the function. */
            return nemR3LnxHandleInternalError(pVCpu, pRun);
#endif

        /*
         * Foreign and unknowns.
         */
#if 0
        case KVM_EXIT_IO:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_IO on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_NMI:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_NMI on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_EPR:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_EPR on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_WATCHDOG:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_WATCHDOG on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_ARM_NISV:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_ARM_NISV on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_S390_STSI:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_S390_STSI on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_S390_TSCH:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_S390_TSCH on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_OSI:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_OSI on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_PAPR_HCALL:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_PAPR_HCALL on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_S390_UCONTROL:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_S390_UCONTROL on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_DCR:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_DCR on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_S390_SIEIC:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_S390_SIEIC on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_S390_RESET:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_S390_RESET on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_UNKNOWN:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_UNKNOWN on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
        case KVM_EXIT_XEN:
            AssertLogRelMsgFailedReturn(("KVM_EXIT_XEN on VCpu #%u at %RX64!\n", pVCpu->idCpu, pRun->s.regs.pc), VERR_NEM_IPE_1);
#endif
        default:
            AssertLogRelMsgFailedReturn(("Unknown exit reason %u on VCpu #%u!\n", pRun->exit_reason, pVCpu->idCpu), VERR_NEM_IPE_1);
    }
    RT_NOREF(pVM, pVCpu);
    return VERR_NOT_IMPLEMENTED;
}


VBOXSTRICTRC nemR3NativeRunGC(PVM pVM, PVMCPU pVCpu)
{
    /*
     * Try switch to NEM runloop state.
     */
    if (VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED_EXEC_NEM, VMCPUSTATE_STARTED))
    { /* likely */ }
    else
    {
        VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED_EXEC_NEM, VMCPUSTATE_STARTED_EXEC_NEM_CANCELED);
        LogFlow(("NEM/%u: returning immediately because canceled\n", pVCpu->idCpu));
        return VINF_SUCCESS;
    }

    /*
     * The run loop.
     */
    struct kvm_run * const  pRun                = pVCpu->nem.s.pRun;
    const bool              fSingleStepping     = DBGFIsStepping(pVCpu);
    VBOXSTRICTRC            rcStrict            = VINF_SUCCESS;
    bool                    fStatefulExit       = false;  /* For MMIO and IO exits. */
    for (unsigned iLoop = 0;; iLoop++)
    {
        /*
         * Sync the interrupt state.
         */
        rcStrict = nemHCLnxHandleInterruptFF(pVM, pVCpu);
        if (rcStrict == VINF_SUCCESS)
        { /* likely */ }
        else
        {
            LogFlow(("NEM/%u: breaking: nemHCLnxHandleInterruptFF -> %Rrc\n", pVCpu->idCpu, VBOXSTRICTRC_VAL(rcStrict) ));
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnStatus);
            break;
        }

        /*
         * Ensure KVM has the whole state.
         */
        if ((pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_ALL) != CPUMCTX_EXTRN_ALL)
        {
            int rc2 = nemHCLnxExportState(pVM, pVCpu, &pVCpu->cpum.GstCtx);
            AssertRCReturn(rc2, rc2);
        }

        /*
         * Poll timers and run for a bit.
         *
         * With the VID approach (ring-0 or ring-3) we can specify a timeout here,
         * so we take the time of the next timer event and uses that as a deadline.
         * The rounding heuristics are "tuned" so that rhel5 (1K timer) will boot fine.
         */
        /** @todo See if we cannot optimize this TMTimerPollGIP by only redoing
         *        the whole polling job when timers have changed... */
        uint64_t       offDeltaIgnored;
        uint64_t const nsNextTimerEvt = TMTimerPollGIP(pVM, pVCpu, &offDeltaIgnored); NOREF(nsNextTimerEvt);
        if (   !VM_FF_IS_ANY_SET(pVM, VM_FF_EMT_RENDEZVOUS | VM_FF_TM_VIRTUAL_SYNC)
            && !VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_HM_TO_R3_MASK))
        {
            if (VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED_EXEC_NEM_WAIT, VMCPUSTATE_STARTED_EXEC_NEM))
            {
                //LogFlow(("NEM/%u: Entry @ %04x:%08RX64 IF=%d EFL=%#RX64 SS:RSP=%04x:%08RX64 cr0=%RX64\n",
                //         pVCpu->idCpu, pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip,
                //         !!(pRun->s.regs.regs.rflags & X86_EFL_IF), pRun->s.regs.regs.rflags,
                //         pRun->s.regs.sregs.ss.selector, pRun->s.regs.regs.rsp, pRun->s.regs.sregs.cr0));
                TMNotifyStartOfExecution(pVM, pVCpu);

                int rcLnx = ioctl(pVCpu->nem.s.fdVCpu, KVM_RUN, 0UL);

                VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED_EXEC_NEM, VMCPUSTATE_STARTED_EXEC_NEM_WAIT);
                TMNotifyEndOfExecution(pVM, pVCpu, ASMReadTSC());

#if 0 //def LOG_ENABLED
                if (LogIsFlowEnabled())
                {
                    struct kvm_mp_state MpState = {UINT32_MAX};
                    ioctl(pVCpu->nem.s.fdVCpu, KVM_GET_MP_STATE, &MpState);
                    LogFlow(("NEM/%u: Exit  @ %04x:%08RX64 IF=%d EFL=%#RX64 CR8=%#x Reason=%#x IrqReady=%d Flags=%#x %#lx\n", pVCpu->idCpu,
                             pRun->s.regs.sregs.cs.selector, pRun->s.regs.regs.rip, pRun->if_flag,
                             pRun->s.regs.regs.rflags, pRun->s.regs.sregs.cr8, pRun->exit_reason,
                             pRun->ready_for_interrupt_injection, pRun->flags, MpState.mp_state));
                }
#endif
                fStatefulExit = false;
                if (RT_LIKELY(rcLnx == 0 || errno == EINTR))
                {
                    /*
                     * Deal with the exit.
                     */
                    rcStrict = nemHCLnxHandleExit(pVM, pVCpu, pRun, &fStatefulExit);
                    if (rcStrict == VINF_SUCCESS)
                    { /* hopefully likely */ }
                    else
                    {
                        LogFlow(("NEM/%u: breaking: nemHCLnxHandleExit -> %Rrc\n", pVCpu->idCpu, VBOXSTRICTRC_VAL(rcStrict) ));
                        STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnStatus);
                        break;
                    }
                }
                else
                {
                    int rc2 = RTErrConvertFromErrno(errno);
                    AssertLogRelMsgFailedReturn(("KVM_RUN failed: rcLnx=%d errno=%u rc=%Rrc\n", rcLnx, errno, rc2), rc2);
                }

                /*
                 * If no relevant FFs are pending, loop.
                 */
                if (   !VM_FF_IS_ANY_SET(   pVM,   !fSingleStepping ? VM_FF_HP_R0_PRE_HM_MASK    : VM_FF_HP_R0_PRE_HM_STEP_MASK)
                    && !VMCPU_FF_IS_ANY_SET(pVCpu, !fSingleStepping ? VMCPU_FF_HP_R0_PRE_HM_MASK : VMCPU_FF_HP_R0_PRE_HM_STEP_MASK) )
                { /* likely */ }
                else
                {

                    /** @todo Try handle pending flags, not just return to EM loops.  Take care
                     *        not to set important RCs here unless we've handled an exit. */
                    LogFlow(("NEM/%u: breaking: pending FF (%#x / %#RX64)\n",
                             pVCpu->idCpu, pVM->fGlobalForcedActions, (uint64_t)pVCpu->fLocalForcedActions));
                    STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnFFPost);
                    break;
                }
            }
            else
            {
                LogFlow(("NEM/%u: breaking: canceled %d (pre exec)\n", pVCpu->idCpu, VMCPU_GET_STATE(pVCpu) ));
                STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnCancel);
                break;
            }
        }
        else
        {
            LogFlow(("NEM/%u: breaking: pending FF (pre exec)\n", pVCpu->idCpu));
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatBreakOnFFPre);
            break;
        }
    } /* the run loop */


    /*
     * If the last exit was stateful, commit the state we provided before
     * returning to the EM loop so we have a consistent state and can safely
     * be rescheduled and whatnot.  This may require us to make multiple runs
     * for larger MMIO and I/O operations. Sigh^3.
     *
     * Note! There is no 'ing way to reset the kernel side completion callback
     *       for these stateful i/o exits.  Very annoying interface.
     */
    /** @todo check how this works with string I/O and string MMIO. */
    if (fStatefulExit && RT_SUCCESS(rcStrict))
    {
        STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatFlushExitOnReturn);
        uint32_t const uOrgExit = pRun->exit_reason;
        for (uint32_t i = 0; ; i++)
        {
            pRun->immediate_exit = 1;
            int rcLnx = ioctl(pVCpu->nem.s.fdVCpu, KVM_RUN, 0UL);
            Log(("NEM/%u: Flushed stateful exit -> %d/%d exit_reason=%d\n", pVCpu->idCpu, rcLnx, errno, pRun->exit_reason));
            if (rcLnx == -1 && errno == EINTR)
            {
                switch (i)
                {
                    case 0: STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatFlushExitOnReturn1Loop); break;
                    case 1: STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatFlushExitOnReturn2Loops); break;
                    case 2: STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatFlushExitOnReturn3Loops); break;
                    default: STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatFlushExitOnReturn4PlusLoops); break;
                }
                break;
            }
            AssertLogRelMsgBreakStmt(rcLnx == 0 && pRun->exit_reason == uOrgExit,
                                     ("rcLnx=%d errno=%d exit_reason=%d uOrgExit=%d\n", rcLnx, errno, pRun->exit_reason, uOrgExit),
                                     rcStrict = VERR_NEM_IPE_6);
            VBOXSTRICTRC rcStrict2 = nemHCLnxHandleExit(pVM, pVCpu, pRun, &fStatefulExit);
            if (rcStrict2 == VINF_SUCCESS || rcStrict2 == rcStrict)
            { /* likely */ }
            else if (RT_FAILURE(rcStrict2))
            {
                rcStrict = rcStrict2;
                break;
            }
            else
            {
                AssertLogRelMsgBreakStmt(rcStrict == VINF_SUCCESS,
                                         ("rcStrict=%Rrc rcStrict2=%Rrc\n", VBOXSTRICTRC_VAL(rcStrict), VBOXSTRICTRC_VAL(rcStrict2)),
                                         rcStrict = VERR_NEM_IPE_7);
                rcStrict = rcStrict2;
            }
        }
        pRun->immediate_exit = 0;
    }

    /*
     * If the CPU is running, make sure to stop it before we try sync back the
     * state and return to EM.  We don't sync back the whole state if we can help it.
     */
    if (!VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED, VMCPUSTATE_STARTED_EXEC_NEM))
        VMCPU_CMPXCHG_STATE(pVCpu, VMCPUSTATE_STARTED, VMCPUSTATE_STARTED_EXEC_NEM_CANCELED);

    if (pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_ALL)
    {
        /* Try anticipate what we might need. */
        uint64_t fImport = IEM_CPUMCTX_EXTRN_MUST_MASK /*?*/;
        if (   (rcStrict >= VINF_EM_FIRST && rcStrict <= VINF_EM_LAST)
            || RT_FAILURE(rcStrict))
            fImport = CPUMCTX_EXTRN_ALL;
        else if (VMCPU_FF_IS_ANY_SET(pVCpu, VMCPU_FF_INTERRUPT_IRQ | VMCPU_FF_INTERRUPT_FIQ))
            fImport |= IEM_CPUMCTX_EXTRN_XCPT_MASK;

        if (pVCpu->cpum.GstCtx.fExtrn & fImport)
        {
            int rc2 = nemHCLnxImportState(pVCpu, fImport, &pVCpu->cpum.GstCtx);
            if (RT_SUCCESS(rc2))
                pVCpu->cpum.GstCtx.fExtrn &= ~fImport;
            else if (RT_SUCCESS(rcStrict))
                rcStrict = rc2;
            if (!(pVCpu->cpum.GstCtx.fExtrn & CPUMCTX_EXTRN_ALL))
                pVCpu->cpum.GstCtx.fExtrn = 0;
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnReturn);
        }
        else
            STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnReturnSkipped);
    }
    else
    {
        pVCpu->cpum.GstCtx.fExtrn = 0;
        STAM_REL_COUNTER_INC(&pVCpu->nem.s.StatImportOnReturnSkipped);
    }

    LogFlow(("NEM/%u: %08RX64 => %Rrc\n", pVCpu->idCpu, pVCpu->cpum.GstCtx.Pc, VBOXSTRICTRC_VAL(rcStrict) ));
    return rcStrict;
}


/** @page pg_nem_linux_armv8 NEM/linux - Native Execution Manager, Linux.
 *
 * This is using KVM.
 *
 */

