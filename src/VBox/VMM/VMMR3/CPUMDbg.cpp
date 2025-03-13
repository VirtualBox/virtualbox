/* $Id: CPUMDbg.cpp 108740 2025-03-13 11:17:16Z knut.osmundsen@oracle.com $ */
/** @file
 * CPUM - CPU Monitor / Manager, Debugger & Debugging APIs.
 */

/*
 * Copyright (C) 2010-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DBGF
#include <VBox/vmm/cpum.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/apic.h>
#include "CPUMInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/thread.h>
#include <iprt/string.h>
#include <iprt/uint128.h>


/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegGet_Generic(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    PVMCPU      pVCpu   = (PVMCPU)pvUser;
    void const *pv      = (uint8_t const *)&pVCpu->cpum + pDesc->offRegister;

    VMCPU_ASSERT_EMT(pVCpu);

    switch (pDesc->enmType)
    {
        case DBGFREGVALTYPE_U8:        pValue->u8   = *(uint8_t  const *)pv; return VINF_SUCCESS;
        case DBGFREGVALTYPE_U16:       pValue->u16  = *(uint16_t const *)pv; return VINF_SUCCESS;
        case DBGFREGVALTYPE_U32:       pValue->u32  = *(uint32_t const *)pv; return VINF_SUCCESS;
        case DBGFREGVALTYPE_U64:       pValue->u64  = *(uint64_t const *)pv; return VINF_SUCCESS;
        case DBGFREGVALTYPE_U128:      pValue->u128 = *(PCRTUINT128U    )pv; return VINF_SUCCESS;
        case DBGFREGVALTYPE_U256:      pValue->u256 = *(PCRTUINT256U    )pv; return VINF_SUCCESS;
        case DBGFREGVALTYPE_U512:      pValue->u512 = *(PCRTUINT512U    )pv; return VINF_SUCCESS;
        default:
            AssertMsgFailedReturn(("%d %s\n", pDesc->enmType, pDesc->pszName), VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }
}


/**
 * @interface_method_impl{DBGFREGDESC,pfnSet}
 */
static DECLCALLBACK(int) cpumR3RegSet_Generic(void *pvUser, PCDBGFREGDESC pDesc, PCDBGFREGVAL pValue, PCDBGFREGVAL pfMask)
{
    PVMCPU      pVCpu = (PVMCPU)pvUser;
    void       *pv    = (uint8_t *)&pVCpu->cpum + pDesc->offRegister;

    VMCPU_ASSERT_EMT(pVCpu);

    switch (pDesc->enmType)
    {
        case DBGFREGVALTYPE_U8:
            *(uint8_t *)pv &= ~pfMask->u8;
            *(uint8_t *)pv |= pValue->u8 & pfMask->u8;
            return VINF_SUCCESS;

        case DBGFREGVALTYPE_U16:
            *(uint16_t *)pv &= ~pfMask->u16;
            *(uint16_t *)pv |= pValue->u16 & pfMask->u16;
            return VINF_SUCCESS;

        case DBGFREGVALTYPE_U32:
            *(uint32_t *)pv &= ~pfMask->u32;
            *(uint32_t *)pv |= pValue->u32 & pfMask->u32;
            return VINF_SUCCESS;

        case DBGFREGVALTYPE_U64:
            *(uint64_t *)pv &= ~pfMask->u64;
            *(uint64_t *)pv |= pValue->u64 & pfMask->u64;
            return VINF_SUCCESS;

        case DBGFREGVALTYPE_U128:
        {
            RTUINT128U Val;
            RTUInt128AssignAnd((PRTUINT128U)pv, RTUInt128AssignBitwiseNot(RTUInt128Assign(&Val, &pfMask->u128)));
            RTUInt128AssignOr((PRTUINT128U)pv, RTUInt128AssignAnd(RTUInt128Assign(&Val, &pValue->u128), &pfMask->u128));
            return VINF_SUCCESS;
        }

        default:
            AssertMsgFailedReturn(("%d %s\n", pDesc->enmType, pDesc->pszName), VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }
}


/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegGet_XStateGeneric(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    PVMCPU      pVCpu   = (PVMCPU)pvUser;
    void const *pv      = (uint8_t const *)&pVCpu->cpum.s.Guest.XState + pDesc->offRegister;

    VMCPU_ASSERT_EMT(pVCpu);

    switch (pDesc->enmType)
    {
        case DBGFREGVALTYPE_U8:        pValue->u8   = *(uint8_t  const *)pv; return VINF_SUCCESS;
        case DBGFREGVALTYPE_U16:       pValue->u16  = *(uint16_t const *)pv; return VINF_SUCCESS;
        case DBGFREGVALTYPE_U32:       pValue->u32  = *(uint32_t const *)pv; return VINF_SUCCESS;
        case DBGFREGVALTYPE_U64:       pValue->u64  = *(uint64_t const *)pv; return VINF_SUCCESS;
        case DBGFREGVALTYPE_U128:      pValue->u128 = *(PCRTUINT128U    )pv; return VINF_SUCCESS;
        default:
            AssertMsgFailedReturn(("%d %s\n", pDesc->enmType, pDesc->pszName), VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }
}


/**
 * @interface_method_impl{DBGFREGDESC,pfnSet}
 */
static DECLCALLBACK(int) cpumR3RegSet_XStateGeneric(void *pvUser, PCDBGFREGDESC pDesc, PCDBGFREGVAL pValue, PCDBGFREGVAL pfMask)
{
    PVMCPU      pVCpu = (PVMCPU)pvUser;
    void       *pv    = (uint8_t *)&pVCpu->cpum.s.Guest.XState + pDesc->offRegister;

    VMCPU_ASSERT_EMT(pVCpu);

    switch (pDesc->enmType)
    {
        case DBGFREGVALTYPE_U8:
            *(uint8_t *)pv &= ~pfMask->u8;
            *(uint8_t *)pv |= pValue->u8 & pfMask->u8;
            return VINF_SUCCESS;

        case DBGFREGVALTYPE_U16:
            *(uint16_t *)pv &= ~pfMask->u16;
            *(uint16_t *)pv |= pValue->u16 & pfMask->u16;
            return VINF_SUCCESS;

        case DBGFREGVALTYPE_U32:
            *(uint32_t *)pv &= ~pfMask->u32;
            *(uint32_t *)pv |= pValue->u32 & pfMask->u32;
            return VINF_SUCCESS;

        case DBGFREGVALTYPE_U64:
            *(uint64_t *)pv &= ~pfMask->u64;
            *(uint64_t *)pv |= pValue->u64 & pfMask->u64;
            return VINF_SUCCESS;

        case DBGFREGVALTYPE_U128:
        {
            RTUINT128U Val;
            RTUInt128AssignAnd((PRTUINT128U)pv, RTUInt128AssignBitwiseNot(RTUInt128Assign(&Val, &pfMask->u128)));
            RTUInt128AssignOr((PRTUINT128U)pv, RTUInt128AssignAnd(RTUInt128Assign(&Val, &pValue->u128), &pfMask->u128));
            return VINF_SUCCESS;
        }

        default:
            AssertMsgFailedReturn(("%d %s\n", pDesc->enmType, pDesc->pszName), VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }
}



/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegSet_seg(void *pvUser, PCDBGFREGDESC pDesc, PCDBGFREGVAL pValue, PCDBGFREGVAL pfMask)
{
    /** @todo perform a selector load, updating hidden selectors and stuff. */
    NOREF(pvUser); NOREF(pDesc); NOREF(pValue); NOREF(pfMask);
    return VERR_NOT_IMPLEMENTED;
}


/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegGet_gdtr(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    PVMCPU          pVCpu = (PVMCPU)pvUser;
    VBOXGDTR const *pGdtr = (VBOXGDTR const *)((uint8_t const *)&pVCpu->cpum + pDesc->offRegister);

    VMCPU_ASSERT_EMT(pVCpu);
    Assert(pDesc->enmType == DBGFREGVALTYPE_DTR);

    pValue->dtr.u32Limit  = pGdtr->cbGdt;
    pValue->dtr.u64Base   = pGdtr->pGdt;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegSet_gdtr(void *pvUser, PCDBGFREGDESC pDesc, PCDBGFREGVAL pValue, PCDBGFREGVAL pfMask)
{
    RT_NOREF(pfMask);

    PVMCPU    pVCpu = (PVMCPU)pvUser;
    VBOXGDTR *pGdtr = (VBOXGDTR *)((uint8_t *)&pVCpu->cpum + pDesc->offRegister);

    VMCPU_ASSERT_EMT(pVCpu);
    Assert(pDesc->enmType == DBGFREGVALTYPE_DTR);

    pGdtr->cbGdt = pValue->dtr.u32Limit;
    pGdtr->pGdt  = pValue->dtr.u64Base;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegGet_idtr(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    PVMCPU          pVCpu = (PVMCPU)pvUser;
    VBOXIDTR const *pIdtr = (VBOXIDTR const *)((uint8_t const *)&pVCpu->cpum + pDesc->offRegister);

    VMCPU_ASSERT_EMT(pVCpu);
    Assert(pDesc->enmType == DBGFREGVALTYPE_DTR);

    pValue->dtr.u32Limit  = pIdtr->cbIdt;
    pValue->dtr.u64Base   = pIdtr->pIdt;
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegSet_idtr(void *pvUser, PCDBGFREGDESC pDesc, PCDBGFREGVAL pValue, PCDBGFREGVAL pfMask)
{
    RT_NOREF(pfMask);

    PVMCPU    pVCpu = (PVMCPU)pvUser;
    VBOXIDTR *pIdtr = (VBOXIDTR *)((uint8_t *)&pVCpu->cpum + pDesc->offRegister);

    VMCPU_ASSERT_EMT(pVCpu);
    Assert(pDesc->enmType == DBGFREGVALTYPE_DTR);

    pIdtr->cbIdt = pValue->dtr.u32Limit;
    pIdtr->pIdt = pValue->dtr.u64Base;
    return VINF_SUCCESS;
}


/**
 * Determins the tag register value for a CPU register when the FPU state
 * format is FXSAVE.
 *
 * @returns The tag register value.
 * @param   pFpu                Pointer to the guest FPU.
 * @param   iReg                The register number (0..7).
 */
DECLINLINE(uint16_t) cpumR3RegCalcFpuTagFromFxSave(PCX86FXSTATE pFpu, unsigned iReg)
{
    /*
     * See table 11-1 in the AMD docs.
     */
    if (!(pFpu->FTW & RT_BIT_32(iReg)))
        return 3; /* b11 - empty */

    uint16_t const uExp  = pFpu->aRegs[iReg].au16[4];
    if (uExp == 0)
    {
        if (pFpu->aRegs[iReg].au64[0] == 0) /* J & M == 0 */
            return 1; /* b01 - zero */
        return 2; /* b10 - special */
    }

    if (uExp == UINT16_C(0xffff))
        return 2; /* b10 - special */

    if (!(pFpu->aRegs[iReg].au64[0] >> 63)) /* J == 0 */
        return 2; /* b10 - special */

    return 0; /* b00 - valid (normal) */
}


/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegGet_ftw(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    PVMCPU          pVCpu   = (PVMCPU)pvUser;
    PCX86FXSTATE    pFpu    = (PCX86FXSTATE)((uint8_t const *)&pVCpu->cpum + pDesc->offRegister);

    VMCPU_ASSERT_EMT(pVCpu);
    Assert(pDesc->enmType == DBGFREGVALTYPE_U16);

    pValue->u16 =  cpumR3RegCalcFpuTagFromFxSave(pFpu, 0)
                | (cpumR3RegCalcFpuTagFromFxSave(pFpu, 1) <<  2)
                | (cpumR3RegCalcFpuTagFromFxSave(pFpu, 2) <<  4)
                | (cpumR3RegCalcFpuTagFromFxSave(pFpu, 3) <<  6)
                | (cpumR3RegCalcFpuTagFromFxSave(pFpu, 4) <<  8)
                | (cpumR3RegCalcFpuTagFromFxSave(pFpu, 5) << 10)
                | (cpumR3RegCalcFpuTagFromFxSave(pFpu, 6) << 12)
                | (cpumR3RegCalcFpuTagFromFxSave(pFpu, 7) << 14);
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegSet_ftw(void *pvUser, PCDBGFREGDESC pDesc, PCDBGFREGVAL pValue, PCDBGFREGVAL pfMask)
{
    NOREF(pvUser); NOREF(pDesc); NOREF(pValue); NOREF(pfMask);
    return VERR_DBGF_READ_ONLY_REGISTER;
}

#if 0 /* unused */

/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegGet_Dummy(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    RT_NOREF_PV(pvUser);
    switch (pDesc->enmType)
    {
        case DBGFREGVALTYPE_U8:        pValue->u8   = 0; return VINF_SUCCESS;
        case DBGFREGVALTYPE_U16:       pValue->u16  = 0; return VINF_SUCCESS;
        case DBGFREGVALTYPE_U32:       pValue->u32  = 0; return VINF_SUCCESS;
        case DBGFREGVALTYPE_U64:       pValue->u64  = 0; return VINF_SUCCESS;
        case DBGFREGVALTYPE_U128:
            RT_ZERO(pValue->u128);
            return VINF_SUCCESS;
        case DBGFREGVALTYPE_DTR:
            pValue->dtr.u32Limit = 0;
            pValue->dtr.u64Base  = 0;
            return VINF_SUCCESS;
        case DBGFREGVALTYPE_R80:
            RT_ZERO(pValue->r80Ex);
            return VINF_SUCCESS;
        default:
            AssertMsgFailedReturn(("%d %s\n", pDesc->enmType, pDesc->pszName), VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }
}


/**
 * @interface_method_impl{DBGFREGDESC,pfnSet}
 */
static DECLCALLBACK(int) cpumR3RegSet_Dummy(void *pvUser, PCDBGFREGDESC pDesc, PCDBGFREGVAL pValue, PCDBGFREGVAL pfMask)
{
    NOREF(pvUser); NOREF(pDesc); NOREF(pValue); NOREF(pfMask);
    return VERR_DBGF_READ_ONLY_REGISTER;
}

#endif /* unused */

/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegGet_ymm(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    PVMCPU      pVCpu   = (PVMCPU)pvUser;
    uint32_t    iReg    = pDesc->offRegister;

    Assert(pDesc->enmType == DBGFREGVALTYPE_U256);
    VMCPU_ASSERT_EMT(pVCpu);

    if (iReg < 16)
    {
        pValue->u256.DQWords.dqw0 = pVCpu->cpum.s.Guest.XState.x87.aXMM[iReg].uXmm;
        pValue->u256.DQWords.dqw1 = pVCpu->cpum.s.Guest.XState.u.YmmHi.aYmmHi[iReg].uXmm;
        return VINF_SUCCESS;
    }
    return VERR_NOT_IMPLEMENTED;
}


/**
 * @interface_method_impl{DBGFREGDESC,pfnSet}
 */
static DECLCALLBACK(int) cpumR3RegSet_ymm(void *pvUser, PCDBGFREGDESC pDesc, PCDBGFREGVAL pValue, PCDBGFREGVAL pfMask)
{
    PVMCPU      pVCpu = (PVMCPU)pvUser;
    uint32_t    iReg  = pDesc->offRegister;

    Assert(pDesc->enmType == DBGFREGVALTYPE_U256);
    VMCPU_ASSERT_EMT(pVCpu);

    if (iReg < 16)
    {
        RTUINT128U Val;
        RTUInt128AssignAnd(&pVCpu->cpum.s.Guest.XState.x87.aXMM[iReg].uXmm,
                           RTUInt128AssignBitwiseNot(RTUInt128Assign(&Val, &pfMask->u256.DQWords.dqw0)));
        RTUInt128AssignOr(&pVCpu->cpum.s.Guest.XState.u.YmmHi.aYmmHi[iReg].uXmm,
                          RTUInt128AssignAnd(RTUInt128Assign(&Val, &pValue->u128), &pfMask->u128));

    }
    return VERR_NOT_IMPLEMENTED;
}


/*
 *
 * Guest register access functions.
 *
 */

/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegGstGet_crX(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    PVMCPU pVCpu = (PVMCPU)pvUser;
    VMCPU_ASSERT_EMT(pVCpu);

    uint64_t u64Value;
    int rc = CPUMGetGuestCRx(pVCpu, pDesc->offRegister, &u64Value);
    if (rc == VERR_PDM_NO_APIC_INSTANCE) /* CR8 might not be available, see @bugref{8868}.*/
        u64Value = 0;
    else
        AssertRCReturn(rc, rc);
    switch (pDesc->enmType)
    {
        case DBGFREGVALTYPE_U64:    pValue->u64 = u64Value; break;
        case DBGFREGVALTYPE_U32:    pValue->u32 = (uint32_t)u64Value; break;
        default:
            AssertFailedReturn(VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegGstSet_crX(void *pvUser, PCDBGFREGDESC pDesc, PCDBGFREGVAL pValue, PCDBGFREGVAL pfMask)
{
    int         rc;
    PVMCPU      pVCpu = (PVMCPU)pvUser;

    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * Calculate the new value.
     */
    uint64_t u64Value;
    uint64_t fMask;
    uint64_t fMaskMax;
    switch (pDesc->enmType)
    {
        case DBGFREGVALTYPE_U64:
            u64Value = pValue->u64;
            fMask    = pfMask->u64;
            fMaskMax = UINT64_MAX;
            break;
        case DBGFREGVALTYPE_U32:
            u64Value = pValue->u32;
            fMask    = pfMask->u32;
            fMaskMax = UINT32_MAX;
            break;
        default:
            AssertFailedReturn(VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }
    if (fMask != fMaskMax)
    {
        uint64_t u64FullValue;
        rc = CPUMGetGuestCRx(pVCpu, pDesc->offRegister, &u64FullValue);
        if (RT_FAILURE(rc))
            return rc;
        u64Value = (u64FullValue & ~fMask)
                 | (u64Value     &  fMask);
    }

    /*
     * Perform the assignment.
     */
    switch (pDesc->offRegister)
    {
        case 0: rc = CPUMSetGuestCR0(pVCpu, u64Value); break;
        case 2: rc = CPUMSetGuestCR2(pVCpu, u64Value); break;
        case 3: rc = CPUMSetGuestCR3(pVCpu, u64Value); break;
        case 4: rc = CPUMSetGuestCR4(pVCpu, u64Value); break;
        case 8: rc = APICSetTpr(pVCpu, (uint8_t)(u64Value << 4)); break;
        default:
            AssertFailedReturn(VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }
    return rc;
}


/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegGstGet_drX(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    PVMCPU pVCpu = (PVMCPU)pvUser;
    VMCPU_ASSERT_EMT(pVCpu);

    uint64_t u64Value;
    int rc = CPUMGetGuestDRx(pVCpu, pDesc->offRegister, &u64Value);
    AssertRCReturn(rc, rc);
    switch (pDesc->enmType)
    {
        case DBGFREGVALTYPE_U64:    pValue->u64 = u64Value; break;
        case DBGFREGVALTYPE_U32:    pValue->u32 = (uint32_t)u64Value; break;
        default:
            AssertFailedReturn(VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }
    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegGstSet_drX(void *pvUser, PCDBGFREGDESC pDesc, PCDBGFREGVAL pValue, PCDBGFREGVAL pfMask)
{
    int         rc;
    PVMCPU      pVCpu = (PVMCPU)pvUser;

    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * Calculate the new value.
     */
    uint64_t u64Value;
    uint64_t fMask;
    uint64_t fMaskMax;
    switch (pDesc->enmType)
    {
        case DBGFREGVALTYPE_U64:
            u64Value = pValue->u64;
            fMask    = pfMask->u64;
            fMaskMax = UINT64_MAX;
            break;
        case DBGFREGVALTYPE_U32:
            u64Value = pValue->u32;
            fMask    = pfMask->u32;
            fMaskMax = UINT32_MAX;
            break;
        default:
            AssertFailedReturn(VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }
    if (fMask != fMaskMax)
    {
        uint64_t u64FullValue;
        rc = CPUMGetGuestDRx(pVCpu, pDesc->offRegister, &u64FullValue);
        if (RT_FAILURE(rc))
            return rc;
        u64Value = (u64FullValue & ~fMask)
                 | (u64Value     &  fMask);
    }

    /*
     * Perform the assignment.
     */
    return CPUMSetGuestDRx(pVCpu, pDesc->offRegister, u64Value);
}


/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegGstGet_msr(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    PVMCPU pVCpu = (PVMCPU)pvUser;
    VMCPU_ASSERT_EMT(pVCpu);

    uint64_t u64Value;
    VBOXSTRICTRC rcStrict = CPUMQueryGuestMsr(pVCpu, pDesc->offRegister, &u64Value);
    if (rcStrict == VINF_SUCCESS)
    {
        switch (pDesc->enmType)
        {
            case DBGFREGVALTYPE_U64:    pValue->u64 = u64Value; break;
            case DBGFREGVALTYPE_U32:    pValue->u32 = (uint32_t)u64Value; break;
            case DBGFREGVALTYPE_U16:    pValue->u16 = (uint16_t)u64Value; break;
            default:
                AssertFailedReturn(VERR_IPE_NOT_REACHED_DEFAULT_CASE);
        }
        return VBOXSTRICTRC_VAL(rcStrict);
    }

    /** @todo what to do about errors? */
    Assert(RT_FAILURE_NP(rcStrict));
    return VBOXSTRICTRC_VAL(rcStrict);
}


/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegGstSet_msr(void *pvUser, PCDBGFREGDESC pDesc, PCDBGFREGVAL pValue, PCDBGFREGVAL pfMask)
{
    PVMCPU pVCpu = (PVMCPU)pvUser;

    VMCPU_ASSERT_EMT(pVCpu);

    /*
     * Calculate the new value.
     */
    uint64_t u64Value;
    uint64_t fMask;
    uint64_t fMaskMax;
    switch (pDesc->enmType)
    {
        case DBGFREGVALTYPE_U64:
            u64Value = pValue->u64;
            fMask    = pfMask->u64;
            fMaskMax = UINT64_MAX;
            break;
        case DBGFREGVALTYPE_U32:
            u64Value = pValue->u32;
            fMask    = pfMask->u32;
            fMaskMax = UINT32_MAX;
            break;
        case DBGFREGVALTYPE_U16:
            u64Value = pValue->u16;
            fMask    = pfMask->u16;
            fMaskMax = UINT16_MAX;
            break;
        default:
            AssertFailedReturn(VERR_IPE_NOT_REACHED_DEFAULT_CASE);
    }
    if (fMask != fMaskMax)
    {
        uint64_t u64FullValue;
        VBOXSTRICTRC rcStrict = CPUMQueryGuestMsr(pVCpu, pDesc->offRegister, &u64FullValue);
        if (rcStrict != VINF_SUCCESS)
        {
            AssertRC(RT_FAILURE_NP(rcStrict));
            return VBOXSTRICTRC_VAL(rcStrict);
        }
        u64Value = (u64FullValue & ~fMask)
                 | (u64Value     &  fMask);
    }

    /*
     * Perform the assignment.
     */
    VBOXSTRICTRC rcStrict = CPUMSetGuestMsr(pVCpu, pDesc->offRegister, u64Value);
    if (rcStrict == VINF_SUCCESS)
        return VINF_SUCCESS;
    AssertRC(RT_FAILURE_NP(rcStrict));
    return VBOXSTRICTRC_VAL(rcStrict);
}


/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegGstGet_stN(void *pvUser, PCDBGFREGDESC pDesc, PDBGFREGVAL pValue)
{
    PVMCPU pVCpu = (PVMCPU)pvUser;
    VMCPU_ASSERT_EMT(pVCpu);
    Assert(pDesc->enmType == DBGFREGVALTYPE_R80);

    PX86FXSTATE pFpuCtx = &pVCpu->cpum.s.Guest.XState.x87;
    unsigned iReg = (pFpuCtx->FSW >> 11) & 7;
    iReg += pDesc->offRegister;
    iReg &= 7;
    pValue->r80Ex = pFpuCtx->aRegs[iReg].r80Ex;

    return VINF_SUCCESS;
}


/**
 * @interface_method_impl{DBGFREGDESC,pfnGet}
 */
static DECLCALLBACK(int) cpumR3RegGstSet_stN(void *pvUser, PCDBGFREGDESC pDesc, PCDBGFREGVAL pValue, PCDBGFREGVAL pfMask)
{
    NOREF(pvUser); NOREF(pDesc); NOREF(pValue); NOREF(pfMask);
    return VERR_NOT_IMPLEMENTED;
}



/*
 * Set up aliases.
 */
#define CPUMREGALIAS_STD(Name, psz32, psz16, psz8)  \
    static DBGFREGALIAS const g_aCpumRegAliases_##Name[] = \
    { \
        { psz32, DBGFREGVALTYPE_U32     }, \
        { psz16, DBGFREGVALTYPE_U16     }, \
        { psz8,  DBGFREGVALTYPE_U8      }, \
        { NULL,  DBGFREGVALTYPE_INVALID } \
    }
CPUMREGALIAS_STD(rax,  "eax",   "ax",   "al");
CPUMREGALIAS_STD(rcx,  "ecx",   "cx",   "cl");
CPUMREGALIAS_STD(rdx,  "edx",   "dx",   "dl");
CPUMREGALIAS_STD(rbx,  "ebx",   "bx",   "bl");
CPUMREGALIAS_STD(rsp,  "esp",   "sp",   NULL);
CPUMREGALIAS_STD(rbp,  "ebp",   "bp",   NULL);
CPUMREGALIAS_STD(rsi,  "esi",   "si",  "sil");
CPUMREGALIAS_STD(rdi,  "edi",   "di",  "dil");
CPUMREGALIAS_STD(r8,   "r8d",  "r8w",  "r8b");
CPUMREGALIAS_STD(r9,   "r9d",  "r9w",  "r9b");
CPUMREGALIAS_STD(r10, "r10d", "r10w", "r10b");
CPUMREGALIAS_STD(r11, "r11d", "r11w", "r11b");
CPUMREGALIAS_STD(r12, "r12d", "r12w", "r12b");
CPUMREGALIAS_STD(r13, "r13d", "r13w", "r13b");
CPUMREGALIAS_STD(r14, "r14d", "r14w", "r14b");
CPUMREGALIAS_STD(r15, "r15d", "r15w", "r15b");
CPUMREGALIAS_STD(rip, "eip",   "ip",    NULL);
CPUMREGALIAS_STD(rflags, "eflags", "flags", NULL);
#undef CPUMREGALIAS_STD

static DBGFREGALIAS const g_aCpumRegAliases_fpuip[] =
{
    { "fpuip16", DBGFREGVALTYPE_U16  },
    { NULL, DBGFREGVALTYPE_INVALID }
};

static DBGFREGALIAS const g_aCpumRegAliases_fpudp[] =
{
    { "fpudp16", DBGFREGVALTYPE_U16  },
    { NULL, DBGFREGVALTYPE_INVALID }
};

static DBGFREGALIAS const g_aCpumRegAliases_cr0[] =
{
    { "msw", DBGFREGVALTYPE_U16  },
    { NULL, DBGFREGVALTYPE_INVALID }
};

/*
 * Sub fields.
 */
/** Sub-fields for the (hidden) segment attribute register. */
static DBGFREGSUBFIELD const g_aCpumRegFields_seg[] =
{
    DBGFREGSUBFIELD_RW("type",   0,   4,  0),
    DBGFREGSUBFIELD_RW("s",      4,   1,  0),
    DBGFREGSUBFIELD_RW("dpl",    5,   2,  0),
    DBGFREGSUBFIELD_RW("p",      7,   1,  0),
    DBGFREGSUBFIELD_RW("avl",   12,   1,  0),
    DBGFREGSUBFIELD_RW("l",     13,   1,  0),
    DBGFREGSUBFIELD_RW("d",     14,   1,  0),
    DBGFREGSUBFIELD_RW("g",     15,   1,  0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the flags register. */
static DBGFREGSUBFIELD const g_aCpumRegFields_rflags[] =
{
    DBGFREGSUBFIELD_RW("cf",     0,   1,  0),
    DBGFREGSUBFIELD_RW("pf",     2,   1,  0),
    DBGFREGSUBFIELD_RW("af",     4,   1,  0),
    DBGFREGSUBFIELD_RW("zf",     6,   1,  0),
    DBGFREGSUBFIELD_RW("sf",     7,   1,  0),
    DBGFREGSUBFIELD_RW("tf",     8,   1,  0),
    DBGFREGSUBFIELD_RW("if",     9,   1,  0),
    DBGFREGSUBFIELD_RW("df",    10,   1,  0),
    DBGFREGSUBFIELD_RW("of",    11,   1,  0),
    DBGFREGSUBFIELD_RW("iopl",  12,   2,  0),
    DBGFREGSUBFIELD_RW("nt",    14,   1,  0),
    DBGFREGSUBFIELD_RW("rf",    16,   1,  0),
    DBGFREGSUBFIELD_RW("vm",    17,   1,  0),
    DBGFREGSUBFIELD_RW("ac",    18,   1,  0),
    DBGFREGSUBFIELD_RW("vif",   19,   1,  0),
    DBGFREGSUBFIELD_RW("vip",   20,   1,  0),
    DBGFREGSUBFIELD_RW("id",    21,   1,  0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the FPU control word register. */
static DBGFREGSUBFIELD const g_aCpumRegFields_fcw[] =
{
    DBGFREGSUBFIELD_RW("im",     1,   1,  0),
    DBGFREGSUBFIELD_RW("dm",     2,   1,  0),
    DBGFREGSUBFIELD_RW("zm",     3,   1,  0),
    DBGFREGSUBFIELD_RW("om",     4,   1,  0),
    DBGFREGSUBFIELD_RW("um",     5,   1,  0),
    DBGFREGSUBFIELD_RW("pm",     6,   1,  0),
    DBGFREGSUBFIELD_RW("pc",     8,   2,  0),
    DBGFREGSUBFIELD_RW("rc",    10,   2,  0),
    DBGFREGSUBFIELD_RW("x",     12,   1,  0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the FPU status word register. */
static DBGFREGSUBFIELD const g_aCpumRegFields_fsw[] =
{
    DBGFREGSUBFIELD_RW("ie",     0,   1,  0),
    DBGFREGSUBFIELD_RW("de",     1,   1,  0),
    DBGFREGSUBFIELD_RW("ze",     2,   1,  0),
    DBGFREGSUBFIELD_RW("oe",     3,   1,  0),
    DBGFREGSUBFIELD_RW("ue",     4,   1,  0),
    DBGFREGSUBFIELD_RW("pe",     5,   1,  0),
    DBGFREGSUBFIELD_RW("se",     6,   1,  0),
    DBGFREGSUBFIELD_RW("es",     7,   1,  0),
    DBGFREGSUBFIELD_RW("c0",     8,   1,  0),
    DBGFREGSUBFIELD_RW("c1",     9,   1,  0),
    DBGFREGSUBFIELD_RW("c2",    10,   1,  0),
    DBGFREGSUBFIELD_RW("top",   11,   3,  0),
    DBGFREGSUBFIELD_RW("c3",    14,   1,  0),
    DBGFREGSUBFIELD_RW("b",     15,   1,  0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the FPU tag word register. */
static DBGFREGSUBFIELD const g_aCpumRegFields_ftw[] =
{
    DBGFREGSUBFIELD_RW("tag0",   0,   2,  0),
    DBGFREGSUBFIELD_RW("tag1",   2,   2,  0),
    DBGFREGSUBFIELD_RW("tag2",   4,   2,  0),
    DBGFREGSUBFIELD_RW("tag3",   6,   2,  0),
    DBGFREGSUBFIELD_RW("tag4",   8,   2,  0),
    DBGFREGSUBFIELD_RW("tag5",  10,   2,  0),
    DBGFREGSUBFIELD_RW("tag6",  12,   2,  0),
    DBGFREGSUBFIELD_RW("tag7",  14,   2,  0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the Multimedia Extensions Control and Status Register. */
static DBGFREGSUBFIELD const g_aCpumRegFields_mxcsr[] =
{
    DBGFREGSUBFIELD_RW("ie",     0,   1,  0),
    DBGFREGSUBFIELD_RW("de",     1,   1,  0),
    DBGFREGSUBFIELD_RW("ze",     2,   1,  0),
    DBGFREGSUBFIELD_RW("oe",     3,   1,  0),
    DBGFREGSUBFIELD_RW("ue",     4,   1,  0),
    DBGFREGSUBFIELD_RW("pe",     5,   1,  0),
    DBGFREGSUBFIELD_RW("daz",    6,   1,  0),
    DBGFREGSUBFIELD_RW("im",     7,   1,  0),
    DBGFREGSUBFIELD_RW("dm",     8,   1,  0),
    DBGFREGSUBFIELD_RW("zm",     9,   1,  0),
    DBGFREGSUBFIELD_RW("om",    10,   1,  0),
    DBGFREGSUBFIELD_RW("um",    11,   1,  0),
    DBGFREGSUBFIELD_RW("pm",    12,   1,  0),
    DBGFREGSUBFIELD_RW("rc",    13,   2,  0),
    DBGFREGSUBFIELD_RW("fz",    14,   1,  0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the FPU tag word register. */
static DBGFREGSUBFIELD const g_aCpumRegFields_stN[] =
{
    DBGFREGSUBFIELD_RW("man",    0,  64,  0),
    DBGFREGSUBFIELD_RW("exp",   64,  15,  0),
    DBGFREGSUBFIELD_RW("sig",   79,   1,  0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the MMX registers. */
static DBGFREGSUBFIELD const g_aCpumRegFields_mmN[] =
{
    DBGFREGSUBFIELD_RW("dw0",    0,  32,  0),
    DBGFREGSUBFIELD_RW("dw1",   32,  32,  0),
    DBGFREGSUBFIELD_RW("w0",     0,  16,  0),
    DBGFREGSUBFIELD_RW("w1",    16,  16,  0),
    DBGFREGSUBFIELD_RW("w2",    32,  16,  0),
    DBGFREGSUBFIELD_RW("w3",    48,  16,  0),
    DBGFREGSUBFIELD_RW("b0",     0,   8,  0),
    DBGFREGSUBFIELD_RW("b1",     8,   8,  0),
    DBGFREGSUBFIELD_RW("b2",    16,   8,  0),
    DBGFREGSUBFIELD_RW("b3",    24,   8,  0),
    DBGFREGSUBFIELD_RW("b4",    32,   8,  0),
    DBGFREGSUBFIELD_RW("b5",    40,   8,  0),
    DBGFREGSUBFIELD_RW("b6",    48,   8,  0),
    DBGFREGSUBFIELD_RW("b7",    56,   8,  0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the XMM registers. */
static DBGFREGSUBFIELD const g_aCpumRegFields_xmmN[] =
{
    DBGFREGSUBFIELD_RW("r0",      0,     32,  0),
    DBGFREGSUBFIELD_RW("r0.man",  0+ 0,  23,  0),
    DBGFREGSUBFIELD_RW("r0.exp",  0+23,   8,  0),
    DBGFREGSUBFIELD_RW("r0.sig",  0+31,   1,  0),
    DBGFREGSUBFIELD_RW("r1",     32,     32,  0),
    DBGFREGSUBFIELD_RW("r1.man", 32+ 0,  23,  0),
    DBGFREGSUBFIELD_RW("r1.exp", 32+23,   8,  0),
    DBGFREGSUBFIELD_RW("r1.sig", 32+31,   1,  0),
    DBGFREGSUBFIELD_RW("r2",     64,     32,  0),
    DBGFREGSUBFIELD_RW("r2.man", 64+ 0,  23,  0),
    DBGFREGSUBFIELD_RW("r2.exp", 64+23,   8,  0),
    DBGFREGSUBFIELD_RW("r2.sig", 64+31,   1,  0),
    DBGFREGSUBFIELD_RW("r3",     96,     32,  0),
    DBGFREGSUBFIELD_RW("r3.man", 96+ 0,  23,  0),
    DBGFREGSUBFIELD_RW("r3.exp", 96+23,   8,  0),
    DBGFREGSUBFIELD_RW("r3.sig", 96+31,   1,  0),
    DBGFREGSUBFIELD_TERMINATOR()
};

#if 0 /* needs special accessor, too lazy for that now. */
/** Sub-fields for the YMM registers. */
static DBGFREGSUBFIELD const g_aCpumRegFields_ymmN[] =
{
    DBGFREGSUBFIELD_RW("r0",       0,     32,  0),
    DBGFREGSUBFIELD_RW("r0.man",   0+ 0,  23,  0),
    DBGFREGSUBFIELD_RW("r0.exp",   0+23,   8,  0),
    DBGFREGSUBFIELD_RW("r0.sig",   0+31,   1,  0),
    DBGFREGSUBFIELD_RW("r1",      32,     32,  0),
    DBGFREGSUBFIELD_RW("r1.man",  32+ 0,  23,  0),
    DBGFREGSUBFIELD_RW("r1.exp",  32+23,   8,  0),
    DBGFREGSUBFIELD_RW("r1.sig",  32+31,   1,  0),
    DBGFREGSUBFIELD_RW("r2",      64,     32,  0),
    DBGFREGSUBFIELD_RW("r2.man",  64+ 0,  23,  0),
    DBGFREGSUBFIELD_RW("r2.exp",  64+23,   8,  0),
    DBGFREGSUBFIELD_RW("r2.sig",  64+31,   1,  0),
    DBGFREGSUBFIELD_RW("r3",      96,     32,  0),
    DBGFREGSUBFIELD_RW("r3.man",  96+ 0,  23,  0),
    DBGFREGSUBFIELD_RW("r3.exp",  96+23,   8,  0),
    DBGFREGSUBFIELD_RW("r3.sig",  96+31,   1,  0),
    DBGFREGSUBFIELD_RW("r4",     128,     32,  0),
    DBGFREGSUBFIELD_RW("r4.man", 128+ 0,  23,  0),
    DBGFREGSUBFIELD_RW("r4.exp", 128+23,   8,  0),
    DBGFREGSUBFIELD_RW("r4.sig", 128+31,   1,  0),
    DBGFREGSUBFIELD_RW("r5",     160,     32,  0),
    DBGFREGSUBFIELD_RW("r5.man", 160+ 0,  23,  0),
    DBGFREGSUBFIELD_RW("r5.exp", 160+23,   8,  0),
    DBGFREGSUBFIELD_RW("r5.sig", 160+31,   1,  0),
    DBGFREGSUBFIELD_RW("r6",     192,     32,  0),
    DBGFREGSUBFIELD_RW("r6.man", 192+ 0,  23,  0),
    DBGFREGSUBFIELD_RW("r6.exp", 192+23,   8,  0),
    DBGFREGSUBFIELD_RW("r6.sig", 192+31,   1,  0),
    DBGFREGSUBFIELD_RW("r7",     224,     32,  0),
    DBGFREGSUBFIELD_RW("r7.man", 224+ 0,  23,  0),
    DBGFREGSUBFIELD_RW("r7.exp", 224+23,   8,  0),
    DBGFREGSUBFIELD_RW("r7.sig", 224+31,   1,  0),
    DBGFREGSUBFIELD_TERMINATOR()
};
#endif

/** Sub-fields for the CR0 register. */
static DBGFREGSUBFIELD const g_aCpumRegFields_cr0[] =
{
    DBGFREGSUBFIELD_RW("pe",      0,      1,  0),
    DBGFREGSUBFIELD_RW("mp",      1,      1,  0),
    DBGFREGSUBFIELD_RW("em",      2,      1,  0),
    DBGFREGSUBFIELD_RW("ts",      3,      1,  0),
    DBGFREGSUBFIELD_RO("et",      4,      1,  0),
    DBGFREGSUBFIELD_RW("ne",      5,      1,  0),
    DBGFREGSUBFIELD_RW("wp",     16,      1,  0),
    DBGFREGSUBFIELD_RW("am",     18,      1,  0),
    DBGFREGSUBFIELD_RW("nw",     29,      1,  0),
    DBGFREGSUBFIELD_RW("cd",     30,      1,  0),
    DBGFREGSUBFIELD_RW("pg",     31,      1,  0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the CR3 register. */
static DBGFREGSUBFIELD const g_aCpumRegFields_cr3[] =
{
    DBGFREGSUBFIELD_RW("pwt",     3,      1,  0),
    DBGFREGSUBFIELD_RW("pcd",     4,      1,  0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the CR4 register. */
static DBGFREGSUBFIELD const g_aCpumRegFields_cr4[] =
{
    DBGFREGSUBFIELD_RW("vme",     0,      1,  0),
    DBGFREGSUBFIELD_RW("pvi",     1,      1,  0),
    DBGFREGSUBFIELD_RW("tsd",     2,      1,  0),
    DBGFREGSUBFIELD_RW("de",      3,      1,  0),
    DBGFREGSUBFIELD_RW("pse",     4,      1,  0),
    DBGFREGSUBFIELD_RW("pae",     5,      1,  0),
    DBGFREGSUBFIELD_RW("mce",     6,      1,  0),
    DBGFREGSUBFIELD_RW("pge",     7,      1,  0),
    DBGFREGSUBFIELD_RW("pce",     8,      1,  0),
    DBGFREGSUBFIELD_RW("osfxsr",  9,      1,  0),
    DBGFREGSUBFIELD_RW("osxmmeexcpt", 10, 1,  0),
    DBGFREGSUBFIELD_RW("vmxe",   13,      1,  0),
    DBGFREGSUBFIELD_RW("smxe",   14,      1,  0),
    DBGFREGSUBFIELD_RW("pcide",  17,      1,  0),
    DBGFREGSUBFIELD_RW("osxsave", 18,     1,  0),
    DBGFREGSUBFIELD_RW("smep",   20,      1,  0),
    DBGFREGSUBFIELD_RW("smap",   21,      1,  0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the DR6 register. */
static DBGFREGSUBFIELD const g_aCpumRegFields_dr6[] =
{
    DBGFREGSUBFIELD_RW("b0",      0,      1,  0),
    DBGFREGSUBFIELD_RW("b1",      1,      1,  0),
    DBGFREGSUBFIELD_RW("b2",      2,      1,  0),
    DBGFREGSUBFIELD_RW("b3",      3,      1,  0),
    DBGFREGSUBFIELD_RW("bd",     13,      1,  0),
    DBGFREGSUBFIELD_RW("bs",     14,      1,  0),
    DBGFREGSUBFIELD_RW("bt",     15,      1,  0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the DR7 register. */
static DBGFREGSUBFIELD const g_aCpumRegFields_dr7[] =
{
    DBGFREGSUBFIELD_RW("l0",      0,      1,  0),
    DBGFREGSUBFIELD_RW("g0",      1,      1,  0),
    DBGFREGSUBFIELD_RW("l1",      2,      1,  0),
    DBGFREGSUBFIELD_RW("g1",      3,      1,  0),
    DBGFREGSUBFIELD_RW("l2",      4,      1,  0),
    DBGFREGSUBFIELD_RW("g2",      5,      1,  0),
    DBGFREGSUBFIELD_RW("l3",      6,      1,  0),
    DBGFREGSUBFIELD_RW("g3",      7,      1,  0),
    DBGFREGSUBFIELD_RW("le",      8,      1,  0),
    DBGFREGSUBFIELD_RW("ge",      9,      1,  0),
    DBGFREGSUBFIELD_RW("gd",     13,      1,  0),
    DBGFREGSUBFIELD_RW("rw0",    16,      2,  0),
    DBGFREGSUBFIELD_RW("len0",   18,      2,  0),
    DBGFREGSUBFIELD_RW("rw1",    20,      2,  0),
    DBGFREGSUBFIELD_RW("len1",   22,      2,  0),
    DBGFREGSUBFIELD_RW("rw2",    24,      2,  0),
    DBGFREGSUBFIELD_RW("len2",   26,      2,  0),
    DBGFREGSUBFIELD_RW("rw3",    28,      2,  0),
    DBGFREGSUBFIELD_RW("len3",   30,      2,  0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the CR_PAT MSR. */
static DBGFREGSUBFIELD const g_aCpumRegFields_apic_base[] =
{
    DBGFREGSUBFIELD_RW("bsp",     8,      1,  0),
    DBGFREGSUBFIELD_RW("ge",      9,      1,  0),
    DBGFREGSUBFIELD_RW("base",    12,    20, 12),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the CR_PAT MSR. */
static DBGFREGSUBFIELD const g_aCpumRegFields_cr_pat[] =
{
    /** @todo  */
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the PERF_STATUS MSR. */
static DBGFREGSUBFIELD const g_aCpumRegFields_perf_status[] =
{
    /** @todo  */
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the EFER MSR. */
static DBGFREGSUBFIELD const g_aCpumRegFields_efer[] =
{
    /** @todo  */
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the STAR MSR. */
static DBGFREGSUBFIELD const g_aCpumRegFields_star[] =
{
    /** @todo  */
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the CSTAR MSR. */
static DBGFREGSUBFIELD const g_aCpumRegFields_cstar[] =
{
    /** @todo  */
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the LSTAR MSR. */
static DBGFREGSUBFIELD const g_aCpumRegFields_lstar[] =
{
    /** @todo  */
    DBGFREGSUBFIELD_TERMINATOR()
};

#if 0 /** @todo */
/** Sub-fields for the SF_MASK MSR. */
static DBGFREGSUBFIELD const g_aCpumRegFields_sf_mask[] =
{
    /** @todo  */
    DBGFREGSUBFIELD_TERMINATOR()
};
#endif

/** Sub-fields for the IA32_ARCH_CAP MSR. */
static DBGFREGSUBFIELD const g_aCpumRegFields_msr_ia32_arch_cap[] =
{
    DBGFREGSUBFIELD_RW("rdcl_no",                   0, 1, 0),
    DBGFREGSUBFIELD_RW("ibrs_all",                  1, 1, 0),
    DBGFREGSUBFIELD_RW("rsbo",                      2, 1, 0),
    DBGFREGSUBFIELD_RW("vmm_need_not_flush_l1d",    3, 1, 0),
    DBGFREGSUBFIELD_RW("ssb_no",                    4, 1, 0),
    DBGFREGSUBFIELD_RW("mds_no",                    5, 1, 0),
    DBGFREGSUBFIELD_RW("if_pschange_mc_no",         6, 1, 0),
    DBGFREGSUBFIELD_RW("tsx_ctrl",                  7, 1, 0),
    DBGFREGSUBFIELD_RW("taa_no",                    8, 1, 0),
    DBGFREGSUBFIELD_RW("misc_package_ctrls",       10, 1, 0),
    DBGFREGSUBFIELD_RW("energy_filtering_ctl",     11, 1, 0),
    DBGFREGSUBFIELD_RW("doitm",                    12, 1, 0),
    DBGFREGSUBFIELD_RW("sbdr_ssdp_no",             13, 1, 0),
    DBGFREGSUBFIELD_RW("fbsdp_no",                 14, 1, 0),
    DBGFREGSUBFIELD_RW("psdp_no",                  15, 1, 0),
    DBGFREGSUBFIELD_RW("fb_clear",                 17, 1, 0),
    DBGFREGSUBFIELD_RW("fb_clear_ctrl",            18, 1, 0),
    DBGFREGSUBFIELD_RW("rrsba",                    19, 1, 0),
    DBGFREGSUBFIELD_RW("bhi_no",                   20, 1, 0),
    DBGFREGSUBFIELD_RW("xapic_disable_status",     21, 1, 0),
    DBGFREGSUBFIELD_RW("overclocking_status",      23, 1, 0),
    DBGFREGSUBFIELD_RW("pbrsb_no",                 24, 1, 0),
    DBGFREGSUBFIELD_RW("gds_ctrl",                 25, 1, 0),
    DBGFREGSUBFIELD_RW("gds_no",                   26, 1, 0),
    DBGFREGSUBFIELD_RW("rfds_no",                  27, 1, 0),
    DBGFREGSUBFIELD_RW("rfds_clear",               28, 1, 0),
    DBGFREGSUBFIELD_RW("ign_umonitor_support",     29, 1, 0),
    DBGFREGSUBFIELD_RW("mon_umon_mitig_support",   30, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};

/** Sub-fields for the IA32_SPEC_CTRL MSR. */
static DBGFREGSUBFIELD const g_aCpumRegFields_msr_ia32_spec_ctrl[] =
{
    DBGFREGSUBFIELD_RW("ibrs",          0, 1, 0),
    DBGFREGSUBFIELD_RW("stibp",         1, 1, 0),
    DBGFREGSUBFIELD_RW("ssbd",          2, 1, 0),
    DBGFREGSUBFIELD_RW("ipred_dis_u",   3, 1, 0),
    DBGFREGSUBFIELD_RW("ipred_dis_s",   4, 1, 0),
    DBGFREGSUBFIELD_RW("rrsba_dis_u",   5, 1, 0),
    DBGFREGSUBFIELD_RW("rrsba_dis_s",   6, 1, 0),
    DBGFREGSUBFIELD_RW("psfd",          7, 1, 0),
    DBGFREGSUBFIELD_RW("ddpd_u",        8, 1, 0),
    DBGFREGSUBFIELD_RW("bhi_dis_s",    10, 1, 0),
    DBGFREGSUBFIELD_TERMINATOR()
};


/** @name Macros for producing register descriptor table entries.
 * @{ */
#define CPU_REG_EX_AS(a_szName, a_RegSuff, a_TypeSuff, a_offRegister, a_pfnGet, a_pfnSet, a_paAliases, a_paSubFields) \
    { a_szName, DBGFREG_##a_RegSuff, DBGFREGVALTYPE_##a_TypeSuff, 0 /*fFlags*/, a_offRegister, a_pfnGet, a_pfnSet, a_paAliases, a_paSubFields }

#define CPU_REG_REG(UName, LName) \
    CPU_REG_RW_AS(#LName,           UName,          U64, LName,                 cpumR3RegGet_Generic, cpumR3RegSet_Generic, g_aCpumRegAliases_##LName,  NULL)

#define CPU_REG_SEG(UName, LName) \
    CPU_REG_RW_AS(#LName,           UName,          U16, LName.Sel,             cpumR3RegGet_Generic, cpumR3RegSet_seg,     NULL,                       NULL                ), \
    CPU_REG_RW_AS(#LName "_attr",   UName##_ATTR,   U32, LName.Attr.u,          cpumR3RegGet_Generic, cpumR3RegSet_Generic, NULL,                       g_aCpumRegFields_seg), \
    CPU_REG_RW_AS(#LName "_base",   UName##_BASE,   U64, LName.u64Base,         cpumR3RegGet_Generic, cpumR3RegSet_Generic, NULL,                       NULL                ), \
    CPU_REG_RW_AS(#LName "_lim",    UName##_LIMIT,  U32, LName.u32Limit,        cpumR3RegGet_Generic, cpumR3RegSet_Generic, NULL,                       NULL                )

#define CPU_REG_MM(n) \
    CPU_REG_XS_RW_AS("mm" #n,       MM##n,          U64, x87.aRegs[n].mmx, cpumR3RegGet_XStateGeneric, cpumR3RegSet_XStateGeneric, NULL,                g_aCpumRegFields_mmN)

#define CPU_REG_XMM(n) \
    CPU_REG_XS_RW_AS("xmm" #n,      XMM##n,         U128, x87.aXMM[n].xmm, cpumR3RegGet_XStateGeneric, cpumR3RegSet_XStateGeneric, NULL,                g_aCpumRegFields_xmmN)

#define CPU_REG_YMM(n) \
    { "ymm" #n, DBGFREG_YMM##n, DBGFREGVALTYPE_U256, 0 /*fFlags*/, n,   cpumR3RegGet_ymm, cpumR3RegSet_ymm, NULL /*paAliases*/, NULL /*paSubFields*/ }

/** @} */


/**
 * The guest register descriptors.
 */
static DBGFREGDESC const g_aCpumRegGstDescs[] =
{
#define CPU_REG_RW_AS(a_szName, a_RegSuff, a_TypeSuff, a_CpumCtxMemb, a_pfnGet, a_pfnSet, a_paAliases, a_paSubFields) \
    { a_szName, DBGFREG_##a_RegSuff, DBGFREGVALTYPE_##a_TypeSuff, 0 /*fFlags*/,            (uint32_t)RT_UOFFSETOF(CPUMCPU, Guest.a_CpumCtxMemb), a_pfnGet, a_pfnSet, a_paAliases, a_paSubFields }
#define CPU_REG_RO_AS(a_szName, a_RegSuff, a_TypeSuff, a_CpumCtxMemb, a_pfnGet, a_pfnSet, a_paAliases, a_paSubFields) \
    { a_szName, DBGFREG_##a_RegSuff, DBGFREGVALTYPE_##a_TypeSuff, DBGFREG_FLAGS_READ_ONLY, (uint32_t)RT_UOFFSETOF(CPUMCPU, Guest.a_CpumCtxMemb), a_pfnGet, a_pfnSet, a_paAliases, a_paSubFields }
#define CPU_REG_XS_RW_AS(a_szName, a_RegSuff, a_TypeSuff, a_XStateMemb, a_pfnGet, a_pfnSet, a_paAliases, a_paSubFields) \
    { a_szName, DBGFREG_##a_RegSuff, DBGFREGVALTYPE_##a_TypeSuff, 0 /*fFlags*/,            (uint32_t)RT_UOFFSETOF(X86XSAVEAREA, a_XStateMemb),   a_pfnGet, a_pfnSet, a_paAliases, a_paSubFields }
#define CPU_REG_XS_RO_AS(a_szName, a_RegSuff, a_TypeSuff, a_XStateMemb, a_pfnGet, a_pfnSet, a_paAliases, a_paSubFields) \
    { a_szName, DBGFREG_##a_RegSuff, DBGFREGVALTYPE_##a_TypeSuff, DBGFREG_FLAGS_READ_ONLY, (uint32_t)RT_UOFFSETOF(X86XSAVEAREA, a_XStateMemb), a_pfnGet, a_pfnSet, a_paAliases, a_paSubFields }
#define CPU_REG_MSR(a_szName, UName, a_TypeSuff, a_paSubFields) \
    CPU_REG_EX_AS(a_szName,         MSR_##UName,    a_TypeSuff, MSR_##UName,    cpumR3RegGstGet_msr,  cpumR3RegGstSet_msr,  NULL,                       a_paSubFields)
#define CPU_REG_ST(n) \
    CPU_REG_EX_AS("st" #n,          ST##n,          R80, n,                     cpumR3RegGstGet_stN,  cpumR3RegGstSet_stN,  NULL,                       g_aCpumRegFields_stN)

    CPU_REG_REG(RAX, rax),
    CPU_REG_REG(RCX, rcx),
    CPU_REG_REG(RDX, rdx),
    CPU_REG_REG(RBX, rbx),
    CPU_REG_REG(RSP, rsp),
    CPU_REG_REG(RBP, rbp),
    CPU_REG_REG(RSI, rsi),
    CPU_REG_REG(RDI, rdi),
    CPU_REG_REG(R8,   r8),
    CPU_REG_REG(R9,   r9),
    CPU_REG_REG(R10, r10),
    CPU_REG_REG(R11, r11),
    CPU_REG_REG(R12, r12),
    CPU_REG_REG(R13, r13),
    CPU_REG_REG(R14, r14),
    CPU_REG_REG(R15, r15),
    CPU_REG_SEG(CS, cs),
    CPU_REG_SEG(DS, ds),
    CPU_REG_SEG(ES, es),
    CPU_REG_SEG(FS, fs),
    CPU_REG_SEG(GS, gs),
    CPU_REG_SEG(SS, ss),
    CPU_REG_REG(RIP, rip),
    CPU_REG_RW_AS("rflags",         RFLAGS,         U64, rflags,         cpumR3RegGet_Generic,         cpumR3RegSet_Generic,         g_aCpumRegAliases_rflags,   g_aCpumRegFields_rflags ),
    CPU_REG_XS_RW_AS("fcw",         FCW,            U16, x87.FCW,        cpumR3RegGet_XStateGeneric,   cpumR3RegSet_XStateGeneric,   NULL,                       g_aCpumRegFields_fcw    ),
    CPU_REG_XS_RW_AS("fsw",         FSW,            U16, x87.FSW,        cpumR3RegGet_XStateGeneric,   cpumR3RegSet_XStateGeneric,   NULL,                       g_aCpumRegFields_fsw    ),
    CPU_REG_XS_RO_AS("ftw",         FTW,            U16, x87,            cpumR3RegGet_ftw,             cpumR3RegSet_ftw,             NULL,                       g_aCpumRegFields_ftw    ),
    CPU_REG_XS_RW_AS("fop",         FOP,            U16, x87.FOP,        cpumR3RegGet_XStateGeneric,   cpumR3RegSet_XStateGeneric,   NULL,                       NULL                    ),
    CPU_REG_XS_RW_AS("fpuip",       FPUIP,          U32, x87.FPUIP,      cpumR3RegGet_XStateGeneric,   cpumR3RegSet_XStateGeneric,   g_aCpumRegAliases_fpuip,    NULL                    ),
    CPU_REG_XS_RW_AS("fpucs",       FPUCS,          U16, x87.CS,         cpumR3RegGet_XStateGeneric,   cpumR3RegSet_XStateGeneric,   NULL,                       NULL                    ),
    CPU_REG_XS_RW_AS("fpudp",       FPUDP,          U32, x87.FPUDP,      cpumR3RegGet_XStateGeneric,   cpumR3RegSet_XStateGeneric,   g_aCpumRegAliases_fpudp,    NULL                    ),
    CPU_REG_XS_RW_AS("fpuds",       FPUDS,          U16, x87.DS,         cpumR3RegGet_XStateGeneric,   cpumR3RegSet_XStateGeneric,   NULL,                       NULL                    ),
    CPU_REG_XS_RW_AS("mxcsr",       MXCSR,          U32, x87.MXCSR,      cpumR3RegGet_XStateGeneric,   cpumR3RegSet_XStateGeneric,   NULL,                       g_aCpumRegFields_mxcsr  ),
    CPU_REG_XS_RW_AS("mxcsr_mask",  MXCSR_MASK,     U32, x87.MXCSR_MASK, cpumR3RegGet_XStateGeneric,   cpumR3RegSet_XStateGeneric,   NULL,                       g_aCpumRegFields_mxcsr  ),
    CPU_REG_ST(0),
    CPU_REG_ST(1),
    CPU_REG_ST(2),
    CPU_REG_ST(3),
    CPU_REG_ST(4),
    CPU_REG_ST(5),
    CPU_REG_ST(6),
    CPU_REG_ST(7),
    CPU_REG_MM(0),
    CPU_REG_MM(1),
    CPU_REG_MM(2),
    CPU_REG_MM(3),
    CPU_REG_MM(4),
    CPU_REG_MM(5),
    CPU_REG_MM(6),
    CPU_REG_MM(7),
    CPU_REG_XMM(0),
    CPU_REG_XMM(1),
    CPU_REG_XMM(2),
    CPU_REG_XMM(3),
    CPU_REG_XMM(4),
    CPU_REG_XMM(5),
    CPU_REG_XMM(6),
    CPU_REG_XMM(7),
    CPU_REG_XMM(8),
    CPU_REG_XMM(9),
    CPU_REG_XMM(10),
    CPU_REG_XMM(11),
    CPU_REG_XMM(12),
    CPU_REG_XMM(13),
    CPU_REG_XMM(14),
    CPU_REG_XMM(15),
    CPU_REG_YMM(0),
    CPU_REG_YMM(1),
    CPU_REG_YMM(2),
    CPU_REG_YMM(3),
    CPU_REG_YMM(4),
    CPU_REG_YMM(5),
    CPU_REG_YMM(6),
    CPU_REG_YMM(7),
    CPU_REG_YMM(8),
    CPU_REG_YMM(9),
    CPU_REG_YMM(10),
    CPU_REG_YMM(11),
    CPU_REG_YMM(12),
    CPU_REG_YMM(13),
    CPU_REG_YMM(14),
    CPU_REG_YMM(15),
    CPU_REG_RW_AS("gdtr_base",      GDTR_BASE,      U64, gdtr.pGdt,             cpumR3RegGet_Generic,   cpumR3RegSet_Generic,   NULL,                       NULL                    ),
    CPU_REG_RW_AS("gdtr_lim",       GDTR_LIMIT,     U16, gdtr.cbGdt,            cpumR3RegGet_Generic,   cpumR3RegSet_Generic,   NULL,                       NULL                    ),
    CPU_REG_RW_AS("idtr_base",      IDTR_BASE,      U64, idtr.pIdt,             cpumR3RegGet_Generic,   cpumR3RegSet_Generic,   NULL,                       NULL                    ),
    CPU_REG_RW_AS("idtr_lim",       IDTR_LIMIT,     U16, idtr.cbIdt,            cpumR3RegGet_Generic,   cpumR3RegSet_Generic,   NULL,                       NULL                    ),
    CPU_REG_SEG(LDTR, ldtr),
    CPU_REG_SEG(TR, tr),
    CPU_REG_EX_AS("cr0",            CR0,            U32, 0,                     cpumR3RegGstGet_crX,    cpumR3RegGstSet_crX,    g_aCpumRegAliases_cr0,      g_aCpumRegFields_cr0    ),
    CPU_REG_EX_AS("cr2",            CR2,            U64, 2,                     cpumR3RegGstGet_crX,    cpumR3RegGstSet_crX,    NULL,                       NULL                    ),
    CPU_REG_EX_AS("cr3",            CR3,            U64, 3,                     cpumR3RegGstGet_crX,    cpumR3RegGstSet_crX,    NULL,                       g_aCpumRegFields_cr3    ),
    CPU_REG_EX_AS("cr4",            CR4,            U32, 4,                     cpumR3RegGstGet_crX,    cpumR3RegGstSet_crX,    NULL,                       g_aCpumRegFields_cr4    ),
    CPU_REG_EX_AS("cr8",            CR8,            U32, 8,                     cpumR3RegGstGet_crX,    cpumR3RegGstSet_crX,    NULL,                       NULL                    ),
    CPU_REG_EX_AS("dr0",            DR0,            U64, 0,                     cpumR3RegGstGet_drX,    cpumR3RegGstSet_drX,    NULL,                       NULL                    ),
    CPU_REG_EX_AS("dr1",            DR1,            U64, 1,                     cpumR3RegGstGet_drX,    cpumR3RegGstSet_drX,    NULL,                       NULL                    ),
    CPU_REG_EX_AS("dr2",            DR2,            U64, 2,                     cpumR3RegGstGet_drX,    cpumR3RegGstSet_drX,    NULL,                       NULL                    ),
    CPU_REG_EX_AS("dr3",            DR3,            U64, 3,                     cpumR3RegGstGet_drX,    cpumR3RegGstSet_drX,    NULL,                       NULL                    ),
    CPU_REG_EX_AS("dr6",            DR6,            U32, 6,                     cpumR3RegGstGet_drX,    cpumR3RegGstSet_drX,    NULL,                       g_aCpumRegFields_dr6    ),
    CPU_REG_EX_AS("dr7",            DR7,            U32, 7,                     cpumR3RegGstGet_drX,    cpumR3RegGstSet_drX,    NULL,                       g_aCpumRegFields_dr7    ),
    CPU_REG_MSR("apic_base",     IA32_APICBASE,     U32, g_aCpumRegFields_apic_base  ),
    CPU_REG_MSR("pat",           IA32_CR_PAT,       U64, g_aCpumRegFields_cr_pat     ),
    CPU_REG_MSR("perf_status",   IA32_PERF_STATUS,  U64, g_aCpumRegFields_perf_status),
    CPU_REG_MSR("sysenter_cs",   IA32_SYSENTER_CS,  U16, NULL                        ),
    CPU_REG_MSR("sysenter_eip",  IA32_SYSENTER_EIP, U64, NULL                        ),
    CPU_REG_MSR("sysenter_esp",  IA32_SYSENTER_ESP, U64, NULL                        ),
    CPU_REG_MSR("tsc",           IA32_TSC,          U64, NULL                        ),
    CPU_REG_MSR("msr_ia32_arch_cap",  IA32_ARCH_CAPABILITIES,   U64, g_aCpumRegFields_msr_ia32_arch_cap),
    CPU_REG_MSR("msr_ia32_spec_ctrl", IA32_SPEC_CTRL,           U64, g_aCpumRegFields_msr_ia32_spec_ctrl),
    CPU_REG_MSR("efer",          K6_EFER,           U32, g_aCpumRegFields_efer       ),
    CPU_REG_MSR("star",          K6_STAR,           U64, g_aCpumRegFields_star       ),
    CPU_REG_MSR("cstar",         K8_CSTAR,          U64, g_aCpumRegFields_cstar      ),
    CPU_REG_MSR("msr_fs_base",   K8_FS_BASE,        U64, NULL                        ),
    CPU_REG_MSR("msr_gs_base",   K8_GS_BASE,        U64, NULL                        ),
    CPU_REG_MSR("krnl_gs_base",  K8_KERNEL_GS_BASE, U64, NULL                        ),
    CPU_REG_MSR("lstar",         K8_LSTAR,          U64, g_aCpumRegFields_lstar      ),
    CPU_REG_MSR("sf_mask",       K8_SF_MASK,        U64, NULL                        ),
    CPU_REG_MSR("tsc_aux",       K8_TSC_AUX,        U64, NULL                        ),
    CPU_REG_EX_AS("ah",             AH,             U8,  RT_OFFSETOF(CPUMCPU, Guest.rax) + 1, cpumR3RegGet_Generic, cpumR3RegSet_Generic, NULL,             NULL                    ),
    CPU_REG_EX_AS("ch",             CH,             U8,  RT_OFFSETOF(CPUMCPU, Guest.rcx) + 1, cpumR3RegGet_Generic, cpumR3RegSet_Generic, NULL,             NULL                    ),
    CPU_REG_EX_AS("dh",             DH,             U8,  RT_OFFSETOF(CPUMCPU, Guest.rdx) + 1, cpumR3RegGet_Generic, cpumR3RegSet_Generic, NULL,             NULL                    ),
    CPU_REG_EX_AS("bh",             BH,             U8,  RT_OFFSETOF(CPUMCPU, Guest.rbx) + 1, cpumR3RegGet_Generic, cpumR3RegSet_Generic, NULL,             NULL                    ),
    CPU_REG_RW_AS("gdtr",           GDTR,           DTR, gdtr,                  cpumR3RegGet_gdtr,        cpumR3RegSet_gdtr,    NULL,                       NULL                    ),
    CPU_REG_RW_AS("idtr",           IDTR,           DTR, idtr,                  cpumR3RegGet_idtr,        cpumR3RegSet_idtr,    NULL,                       NULL                    ),
    DBGFREGDESC_TERMINATOR()

#undef CPU_REG_RW_AS
#undef CPU_REG_RO_AS
#undef CPU_REG_MSR
#undef CPU_REG_ST
};


/**
 * Initializes the debugger related sides of the CPUM component.
 *
 * Called by CPUMR3Init.
 *
 * @returns VBox status code.
 * @param   pVM                 The cross context VM structure.
 */
int cpumR3DbgInit(PVM pVM)
{
    for (VMCPUID idCpu = 0; idCpu < pVM->cCpus; idCpu++)
    {
        int rc = DBGFR3RegRegisterCpu(pVM, pVM->apCpusR3[idCpu], g_aCpumRegGstDescs, true /*fGuestRegs*/);
        AssertLogRelRCReturn(rc, rc);
    }

    return VINF_SUCCESS;
}

