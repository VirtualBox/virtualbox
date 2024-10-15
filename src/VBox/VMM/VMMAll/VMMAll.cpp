/* $Id: VMMAll.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VMM All Contexts.
 */

/*
 * Copyright (C) 2006-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_VMM
#include <VBox/vmm/vmm.h>
#include "VMMInternal.h"
#include <VBox/vmm/vmcc.h>
#ifdef IN_RING0
# include <VBox/vmm/gvm.h>
#endif
#include <VBox/vmm/hm.h>
#include <VBox/vmm/vmcpuset.h>
#include <VBox/param.h>
#include <iprt/thread.h>
#include <iprt/mp.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** User counter for the vmmInitFormatTypes function (pro forma). */
static volatile uint32_t g_cFormatTypeUsers = 0;


/**
 * Helper that formats a decimal number in the range 0..9999.
 *
 * @returns The length of the formatted number.
 * @param   pszBuf              Output buffer with sufficient space.
 * @param   uNumber             The number to format.
 */
static unsigned vmmFormatTypeShortNumber(char *pszBuf, uint32_t uNumber)
{
    unsigned  off = 0;
    if (uNumber >= 10)
    {
        if (uNumber >= 100)
        {
            if (uNumber >= 1000)
                pszBuf[off++] = ((uNumber / 1000) % 10) + '0';
            pszBuf[off++] = ((uNumber / 100) % 10) + '0';
        }
        pszBuf[off++] = ((uNumber / 10) % 10) + '0';
    }
    pszBuf[off++] = (uNumber % 10) + '0';
    pszBuf[off] = '\0';
    return off;
}


/**
 * @callback_method_impl{FNRTSTRFORMATTYPE, vmsetcpu}
 */
static DECLCALLBACK(size_t) vmmFormatTypeVmCpuSet(PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                                                  const char *pszType, void const *pvValue,
                                                  int cchWidth, int cchPrecision, unsigned fFlags,
                                                  void *pvUser)
{
    NOREF(pszType); NOREF(cchWidth); NOREF(cchPrecision); NOREF(fFlags);

    PCVMCPUSET  pSet   = (PCVMCPUSET)pvValue;
    uint32_t    cCpus  = 0;
    uint32_t    iCpu   = RT_ELEMENTS(pSet->au32Bitmap) * 32;
    while (iCpu--)
        if (VMCPUSET_IS_PRESENT(pSet, iCpu))
            cCpus++;

    char szTmp[32];
    AssertCompile(RT_ELEMENTS(pSet->au32Bitmap) * 32 < 999);
    if (cCpus == 1)
    {
        iCpu = RT_ELEMENTS(pSet->au32Bitmap) * 32;
        while (iCpu--)
            if (VMCPUSET_IS_PRESENT(pSet, iCpu))
            {
                szTmp[0] = 'c';
                szTmp[1] = 'p';
                szTmp[2] = 'u';
                return pfnOutput(pvArgOutput, szTmp, 3 + vmmFormatTypeShortNumber(&szTmp[3], iCpu));
            }
        cCpus = 0;
    }
    if (cCpus == 0)
        return pfnOutput(pvArgOutput, RT_STR_TUPLE("<empty>"));
    if (cCpus == RT_ELEMENTS(pSet->au32Bitmap) * 32)
        return pfnOutput(pvArgOutput, RT_STR_TUPLE("<full>"));

    /*
     * Print cpus that are present: {1,2,7,9 ... }
     */
    size_t cchRet = pfnOutput(pvArgOutput, "{", 1);

    cCpus = 0;
    iCpu  = 0;
    while (iCpu < RT_ELEMENTS(pSet->au32Bitmap) * 32)
    {
        if (VMCPUSET_IS_PRESENT(pSet, iCpu))
        {
            /* Output the first cpu number. */
            int off = 0;
            if (cCpus != 0)
                szTmp[off++] = ',';
            cCpus++;
            off += vmmFormatTypeShortNumber(&szTmp[off], iCpu);

            /* Check for sequence. */
            uint32_t const iStart = ++iCpu;
            while (   iCpu < RT_ELEMENTS(pSet->au32Bitmap) * 32
                   && VMCPUSET_IS_PRESENT(pSet, iCpu))
            {
                iCpu++;
                cCpus++;
            }
            if (iCpu != iStart)
            {
                szTmp[off++] = '-';
                off += vmmFormatTypeShortNumber(&szTmp[off], iCpu);
            }

            /* Terminate and output. */
            szTmp[off] = '\0';
            cchRet += pfnOutput(pvArgOutput, szTmp, off);
        }
        iCpu++;
    }

    cchRet += pfnOutput(pvArgOutput, "}", 1);
    NOREF(pvUser);
    return cchRet;
}


/**
 * Registers the VMM wide format types.
 *
 * Called by VMMR3Init, VMMR0Init and VMMRCInit.
 */
int vmmInitFormatTypes(void)
{
    int rc = VINF_SUCCESS;
    if (ASMAtomicIncU32(&g_cFormatTypeUsers) == 1)
        rc = RTStrFormatTypeRegister("vmcpuset", vmmFormatTypeVmCpuSet, NULL);
    return rc;
}


/**
 * Counterpart to vmmInitFormatTypes, called by VMMR3Term and VMMR0Term.
 */
void vmmTermFormatTypes(void)
{
    if (ASMAtomicDecU32(&g_cFormatTypeUsers) == 0)
        RTStrFormatTypeDeregister("vmcpuset");
}


/**
 * Gets the ID of the virtual CPU associated with the calling thread.
 *
 * @returns The CPU ID. NIL_VMCPUID if the thread isn't an EMT.
 *
 * @param   pVM         The cross context VM structure.
 * @internal
 */
VMMDECL(VMCPUID) VMMGetCpuId(PVMCC pVM)
{
#if defined(IN_RING3)
    return VMR3GetVMCPUId(pVM);

#elif defined(IN_RING0)
    PVMCPUCC pVCpu = GVMMR0GetGVCpuByGVMandEMT(pVM, NIL_RTNATIVETHREAD);
    return pVCpu ? pVCpu->idCpu : NIL_VMCPUID;

#else /* RC: Always EMT(0) */
    NOREF(pVM);
    return 0;
#endif
}


/**
 * Returns the VMCPU of the calling EMT.
 *
 * @returns The VMCPU pointer. NULL if not an EMT.
 *
 * @param   pVM         The cross context VM structure.
 * @internal
 */
VMMDECL(PVMCPUCC) VMMGetCpu(PVMCC pVM)
{
#ifdef IN_RING3
    VMCPUID idCpu = VMR3GetVMCPUId(pVM);
    if (idCpu == NIL_VMCPUID)
        return NULL;
    Assert(idCpu < pVM->cCpus);
    return VMCC_GET_CPU(pVM, idCpu);

#elif defined(IN_RING0)
    return GVMMR0GetGVCpuByGVMandEMT(pVM, NIL_RTNATIVETHREAD);

#else /* RC: Always EMT(0) */
    RT_NOREF(pVM);
    return &g_VCpu0;
#endif /* IN_RING0 */
}


/**
 * Returns the VMCPU of the first EMT thread.
 *
 * @returns The VMCPU pointer.
 * @param   pVM         The cross context VM structure.
 * @internal
 */
VMMDECL(PVMCPUCC) VMMGetCpu0(PVMCC pVM)
{
    Assert(pVM->cCpus == 1);
    return VMCC_GET_CPU_0(pVM);
}


/**
 * Returns the VMCPU of the specified virtual CPU.
 *
 * @returns The VMCPU pointer. NULL if idCpu is invalid.
 *
 * @param   pVM         The cross context VM structure.
 * @param   idCpu       The ID of the virtual CPU.
 * @internal
 */
VMMDECL(PVMCPUCC) VMMGetCpuById(PVMCC pVM, RTCPUID idCpu)
{
    AssertReturn(idCpu < pVM->cCpus, NULL);
    return VMCC_GET_CPU(pVM, idCpu);
}


/**
 * Gets the VBOX_SVN_REV.
 *
 * This is just to avoid having to compile a bunch of big files
 * and requires less Makefile mess.
 *
 * @returns VBOX_SVN_REV.
 */
VMM_INT_DECL(uint32_t) VMMGetSvnRev(void)
{
    return VBOX_SVN_REV;
}


/**
 * Returns the build type for matching components.
 *
 * @returns Build type value.
 */
uint32_t vmmGetBuildType(void)
{
    uint32_t uRet = 0xbeef0000;
#ifdef DEBUG
    uRet |= RT_BIT_32(0);
#endif
#ifdef VBOX_WITH_STATISTICS
    uRet |= RT_BIT_32(1);
#endif
    return uRet;
}

