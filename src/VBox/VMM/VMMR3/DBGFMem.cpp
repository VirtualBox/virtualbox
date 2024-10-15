/* $Id: DBGFMem.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, Memory Methods.
 */

/*
 * Copyright (C) 2007-2024 Oracle and/or its affiliates.
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
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/pgm.h>
#include <VBox/vmm/selm.h>
#include <VBox/vmm/hm.h>
#include "DBGFInternal.h"
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/vmm/mm.h>



/**
 * Scan guest memory for an exact byte string.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   idCpu       The ID of the CPU context to search in.
 * @param   pAddress    Where to store the mixed address.
 * @param   puAlign     The alignment restriction imposed on the search result.
 * @param   pcbRange    The number of bytes to scan. Passed as a pointer because
 *                      it may be 64-bit.
 * @param   pabNeedle   What to search for - exact search.
 * @param   cbNeedle    Size of the search byte string.
 * @param   pHitAddress Where to put the address of the first hit.
 */
static DECLCALLBACK(int) dbgfR3MemScan(PUVM pUVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, PCRTGCUINTPTR pcbRange,
                                       RTGCUINTPTR *puAlign, const uint8_t *pabNeedle, size_t cbNeedle, PDBGFADDRESS pHitAddress)
{
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    Assert(idCpu == VMMGetCpuId(pVM));

    /*
     * Validate the input we use, PGM does the rest.
     */
    RTGCUINTPTR cbRange = *pcbRange;
    if (!DBGFR3AddrIsValid(pUVM, pAddress))
        return VERR_INVALID_POINTER;
    if (!RT_VALID_PTR(pHitAddress))
        return VERR_INVALID_POINTER;

    /*
     * Select DBGF worker by addressing mode.
     */
    int     rc;
    PVMCPU  pVCpu   = VMMGetCpuById(pVM, idCpu);
    PGMMODE enmMode = PGMGetGuestMode(pVCpu);
    if (    !PGMMODE_WITH_PAGING(enmMode)
        ||  DBGFADDRESS_IS_PHYS(pAddress)
        )
    {
        RTGCPHYS GCPhysAlign = *puAlign;
        if (GCPhysAlign != *puAlign)
            return VERR_OUT_OF_RANGE;
        RTGCPHYS PhysHit;
        rc = PGMR3DbgScanPhysical(pVM, pAddress->FlatPtr, cbRange, GCPhysAlign, pabNeedle, cbNeedle, &PhysHit);
        if (RT_SUCCESS(rc))
            DBGFR3AddrFromPhys(pUVM, pHitAddress, PhysHit);
    }
    else
    {
#if GC_ARCH_BITS > 32
        if (    (   pAddress->FlatPtr >= _4G
                 || pAddress->FlatPtr + cbRange > _4G)
            &&  !PGMMODE_IS_64BIT_MODE(enmMode))
            return VERR_DBGF_MEM_NOT_FOUND;
#endif
        RTGCUINTPTR GCPtrHit;
        rc = PGMR3DbgScanVirtual(pVM, pVCpu, pAddress->FlatPtr, cbRange, *puAlign, pabNeedle, cbNeedle, &GCPtrHit);
        if (RT_SUCCESS(rc))
            DBGFR3AddrFromFlat(pUVM, pHitAddress, GCPtrHit);
    }

    return rc;
}


/**
 * Scan guest memory for an exact byte string.
 *
 * @returns VBox status codes:
 * @retval  VINF_SUCCESS and *pGCPtrHit on success.
 * @retval  VERR_DBGF_MEM_NOT_FOUND if not found.
 * @retval  VERR_INVALID_POINTER if any of the pointer arguments are invalid.
 * @retval  VERR_INVALID_ARGUMENT if any other arguments are invalid.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   idCpu       The ID of the CPU context to search in.
 * @param   pAddress    Where to store the mixed address.
 * @param   cbRange     The number of bytes to scan.
 * @param   uAlign      The alignment restriction imposed on the result.
 *                      Usually set to 1.
 * @param   pvNeedle    What to search for - exact search.
 * @param   cbNeedle    Size of the search byte string.
 * @param   pHitAddress Where to put the address of the first hit.
 *
 * @thread  Any thread.
 */
VMMR3DECL(int) DBGFR3MemScan(PUVM pUVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, RTGCUINTPTR cbRange, RTGCUINTPTR uAlign,
                             const void *pvNeedle, size_t cbNeedle, PDBGFADDRESS pHitAddress)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(idCpu < pUVM->cCpus, VERR_INVALID_CPU_ID);
    return VMR3ReqPriorityCallWaitU(pUVM, idCpu, (PFNRT)dbgfR3MemScan, 8,
                                    pUVM, idCpu, pAddress, &cbRange, &uAlign, pvNeedle, cbNeedle, pHitAddress);

}


/**
 * Read guest memory.
 *
 * @returns VBox status code.
 * @param   pUVM        The user mode VM handle.
 * @param   idCpu       The ID of the CPU context to read memory from.
 * @param   pAddress    Where to start reading.
 * @param   pvBuf       Where to store the data we've read.
 * @param   cbRead      The number of bytes to read.
 */
static DECLCALLBACK(int) dbgfR3MemRead(PUVM pUVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, void *pvBuf, size_t cbRead)
{
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    Assert(idCpu == VMMGetCpuId(pVM));

    /*
     * Validate the input we use, PGM does the rest.
     */
    if (!DBGFR3AddrIsValid(pUVM, pAddress))
        return VERR_INVALID_POINTER;
    if (!RT_VALID_PTR(pvBuf))
        return VERR_INVALID_POINTER;

    /*
     * Select PGM worker by addressing mode.
     */
    int rc;
    PVMCPU  pVCpu   = VMMGetCpuById(pVM, idCpu);
    PGMMODE enmMode = PGMGetGuestMode(pVCpu);
    if (    !PGMMODE_WITH_PAGING(enmMode)
        ||  DBGFADDRESS_IS_PHYS(pAddress))
        rc = PGMPhysSimpleReadGCPhys(pVM, pvBuf, pAddress->FlatPtr, cbRead);
    else
    {
#if GC_ARCH_BITS > 32
        if (    (   pAddress->FlatPtr >= _4G
                 || pAddress->FlatPtr + cbRead > _4G)
            &&  !PGMMODE_IS_64BIT_MODE(enmMode))
            return VERR_PAGE_TABLE_NOT_PRESENT;
#endif
        rc = PGMPhysSimpleReadGCPtr(pVCpu, pvBuf, pAddress->FlatPtr, cbRead);
    }
    return rc;
}


/**
 * Read guest memory.
 *
 * @returns VBox status code.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   idCpu       The ID of the source CPU context (for the address).
 * @param   pAddress    Where to start reading.
 * @param   pvBuf       Where to store the data we've read.
 * @param   cbRead      The number of bytes to read.
 */
VMMR3DECL(int) DBGFR3MemRead(PUVM pUVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, void *pvBuf, size_t cbRead)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(idCpu < pUVM->cCpus, VERR_INVALID_CPU_ID);

    if ((pAddress->fFlags & DBGFADDRESS_FLAGS_TYPE_MASK) == DBGFADDRESS_FLAGS_RING0)
    {
        AssertCompile(sizeof(RTHCUINTPTR) <= sizeof(pAddress->FlatPtr));
        VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, VERR_INVALID_VM_HANDLE);
        return VMMR3ReadR0Stack(pUVM->pVM, idCpu, (RTHCUINTPTR)pAddress->FlatPtr, pvBuf, cbRead);
    }
    return VMR3ReqPriorityCallWaitU(pUVM, idCpu, (PFNRT)dbgfR3MemRead, 5, pUVM, idCpu, pAddress, pvBuf, cbRead);
}


/**
 * Read a zero terminated string from guest memory.
 *
 * @returns VBox status code.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   idCpu       The ID of the source CPU context (for the address).
 * @param   pAddress    Where to start reading.
 * @param   pszBuf      Where to store the string.
 * @param   cchBuf      The size of the buffer.
 */
static DECLCALLBACK(int) dbgfR3MemReadString(PUVM pUVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, char *pszBuf, size_t cchBuf)
{
    /*
     * Validate the input we use, PGM does the rest.
     */
    if (!DBGFR3AddrIsValid(pUVM, pAddress))
        return VERR_INVALID_POINTER;
    if (!RT_VALID_PTR(pszBuf))
        return VERR_INVALID_POINTER;

    /*
     * Let dbgfR3MemRead do the job.
     */
    int rc = dbgfR3MemRead(pUVM, idCpu, pAddress, pszBuf, cchBuf);

    /*
     * Make sure the result is terminated and that overflow is signaled.
     * This may look a bit reckless with the rc but, it should be fine.
     */
    if (!RTStrEnd(pszBuf, cchBuf))
    {
        pszBuf[cchBuf - 1] = '\0';
        rc = VINF_BUFFER_OVERFLOW;
    }
    /*
     * Handle partial reads (not perfect).
     */
    else if (RT_FAILURE(rc))
    {
        if (pszBuf[0])
            rc = VINF_SUCCESS;
    }

    return rc;
}


/**
 * Read a zero terminated string from guest memory.
 *
 * @returns VBox status code.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   idCpu       The ID of the source CPU context (for the address).
 * @param   pAddress    Where to start reading.
 * @param   pszBuf      Where to store the string.
 * @param   cchBuf      The size of the buffer.
 */
VMMR3DECL(int) DBGFR3MemReadString(PUVM pUVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, char *pszBuf, size_t cchBuf)
{
    /*
     * Validate and zero output.
     */
    if (!RT_VALID_PTR(pszBuf))
        return VERR_INVALID_POINTER;
    if (cchBuf <= 0)
        return VERR_INVALID_PARAMETER;
    memset(pszBuf, 0, cchBuf);
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(idCpu < pUVM->cCpus, VERR_INVALID_CPU_ID);

    /*
     * Pass it on to the EMT.
     */
    return VMR3ReqPriorityCallWaitU(pUVM, idCpu, (PFNRT)dbgfR3MemReadString, 5, pUVM, idCpu, pAddress, pszBuf, cchBuf);
}


/**
 * Writes guest memory.
 *
 * @returns VBox status code.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   idCpu       The ID of the target CPU context (for the address).
 * @param   pAddress    Where to start writing.
 * @param   pvBuf       The data to write.
 * @param   cbWrite     The number of bytes to write.
 */
static DECLCALLBACK(int) dbgfR3MemWrite(PUVM pUVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, void const *pvBuf, size_t cbWrite)
{
    /*
     * Validate the input we use, PGM does the rest.
     */
    if (!DBGFR3AddrIsValid(pUVM, pAddress))
        return VERR_INVALID_POINTER;
    if (!RT_VALID_PTR(pvBuf))
        return VERR_INVALID_POINTER;
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    /*
     * Select PGM function by addressing mode.
     */
    int rc;
    PVMCPU  pVCpu   = VMMGetCpuById(pVM, idCpu);
    PGMMODE enmMode = PGMGetGuestMode(pVCpu);
    if (   !PGMMODE_WITH_PAGING(enmMode)
        || DBGFADDRESS_IS_PHYS(pAddress))
        rc = PGMPhysSimpleWriteGCPhys(pVM, pAddress->FlatPtr, pvBuf, cbWrite);
    else
    {
#if GC_ARCH_BITS > 32
        if (    (   pAddress->FlatPtr >= _4G
                 || pAddress->FlatPtr + cbWrite > _4G)
            &&  !PGMMODE_IS_64BIT_MODE(enmMode))
            return VERR_PAGE_TABLE_NOT_PRESENT;
#endif
        rc = PGMPhysSimpleWriteGCPtr(pVCpu, pAddress->FlatPtr, pvBuf, cbWrite);
    }
    return rc;
}


/**
 * Read guest memory.
 *
 * @returns VBox status code.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   idCpu       The ID of the target CPU context (for the address).
 * @param   pAddress    Where to start writing.
 * @param   pvBuf       The data to write.
 * @param   cbWrite     The number of bytes to write.
 */
VMMR3DECL(int) DBGFR3MemWrite(PUVM pUVM, VMCPUID idCpu, PCDBGFADDRESS pAddress, void const *pvBuf, size_t cbWrite)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(idCpu < pUVM->cCpus, VERR_INVALID_CPU_ID);
    return VMR3ReqPriorityCallWaitU(pUVM, idCpu, (PFNRT)dbgfR3MemWrite, 5, pUVM, idCpu, pAddress, pvBuf, cbWrite);
}


#if !defined(VBOX_VMM_TARGET_ARMV8)
/**
 * Worker for DBGFR3SelQueryInfo that calls into SELM.
 */
static DECLCALLBACK(int) dbgfR3SelQueryInfo(PUVM pUVM, VMCPUID idCpu, RTSEL Sel, uint32_t fFlags, PDBGFSELINFO pSelInfo)
{
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    /*
     * Make the query.
     */
    PVMCPU pVCpu = VMMGetCpuById(pVM, idCpu);
    VMCPU_ASSERT_EMT(pVCpu);
    int rc = SELMR3GetSelectorInfo(pVCpu, Sel, pSelInfo);

    /*
     * 64-bit mode HACKS for making data and stack selectors wide open when
     * queried. This is voodoo magic.
     */
    if (fFlags & DBGFSELQI_FLAGS_DT_ADJ_64BIT_MODE)
    {
        /* Expand 64-bit data and stack selectors. The check is a bit bogus... */
        if (    RT_SUCCESS(rc)
            &&  (pSelInfo->fFlags & (  DBGFSELINFO_FLAGS_LONG_MODE | DBGFSELINFO_FLAGS_REAL_MODE | DBGFSELINFO_FLAGS_PROT_MODE
                                     | DBGFSELINFO_FLAGS_GATE      | DBGFSELINFO_FLAGS_HYPER
                                     | DBGFSELINFO_FLAGS_INVALID   | DBGFSELINFO_FLAGS_NOT_PRESENT))
                 == DBGFSELINFO_FLAGS_LONG_MODE
            &&  pSelInfo->cbLimit != ~(RTGCPTR)0
            &&  CPUMIsGuestIn64BitCode(pVCpu) )
        {
            pSelInfo->GCPtrBase = 0;
            pSelInfo->cbLimit   = ~(RTGCPTR)0;
        }
        else if (   Sel == 0
                 && CPUMIsGuestIn64BitCode(pVCpu))
        {
            pSelInfo->GCPtrBase = 0;
            pSelInfo->cbLimit   = ~(RTGCPTR)0;
            pSelInfo->Sel       = 0;
            pSelInfo->SelGate   = 0;
            pSelInfo->fFlags    = DBGFSELINFO_FLAGS_LONG_MODE;
            pSelInfo->u.Raw64.Gen.u1Present  = 1;
            pSelInfo->u.Raw64.Gen.u1Long     = 1;
            pSelInfo->u.Raw64.Gen.u1DescType = 1;
            rc = VINF_SUCCESS;
        }
    }
    return rc;
}
#endif


/**
 * Gets information about a selector.
 *
 * Intended for the debugger mostly and will prefer the guest
 * descriptor tables over the shadow ones.
 *
 * @returns VBox status code, the following are the common ones.
 * @retval  VINF_SUCCESS on success.
 * @retval  VERR_INVALID_SELECTOR if the selector isn't fully inside the
 *          descriptor table.
 * @retval  VERR_SELECTOR_NOT_PRESENT if the LDT is invalid or not present. This
 *          is not returned if the selector itself isn't present, you have to
 *          check that for yourself (see DBGFSELINFO::fFlags).
 * @retval  VERR_PAGE_TABLE_NOT_PRESENT or VERR_PAGE_NOT_PRESENT if the
 *          pagetable or page backing the selector table wasn't present.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   idCpu       The ID of the virtual CPU context.
 * @param   Sel         The selector to get info about.
 * @param   fFlags      Flags, see DBGFQSEL_FLAGS_*.
 * @param   pSelInfo    Where to store the information. This will always be
 *                      updated.
 *
 * @remarks This is a wrapper around SELMR3GetSelectorInfo and
 *          SELMR3GetShadowSelectorInfo.
 */
VMMR3DECL(int) DBGFR3SelQueryInfo(PUVM pUVM, VMCPUID idCpu, RTSEL Sel, uint32_t fFlags, PDBGFSELINFO pSelInfo)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(idCpu < pUVM->cCpus, VERR_INVALID_CPU_ID);
    AssertReturn(!(fFlags & ~(DBGFSELQI_FLAGS_DT_GUEST | DBGFSELQI_FLAGS_DT_ADJ_64BIT_MODE)), VERR_INVALID_PARAMETER);

    /* Clear the return data here on this thread. */
    memset(pSelInfo, 0, sizeof(*pSelInfo));

#if defined(VBOX_VMM_TARGET_ARMV8)
    RT_NOREF(Sel);
    return VERR_NOT_SUPPORTED;
#else
    /*
     * Dispatch the request to a worker running on the target CPU.
     */
    return VMR3ReqPriorityCallWaitU(pUVM, idCpu, (PFNRT)dbgfR3SelQueryInfo, 5, pUVM, idCpu, Sel, fFlags, pSelInfo);
#endif
}


/**
 * Validates a CS selector.
 *
 * @returns VBox status code.
 * @param   pSelInfo    Pointer to the selector information for the CS selector.
 * @param   SelCPL      The selector defining the CPL (SS).
 */
VMMDECL(int) DBGFR3SelInfoValidateCS(PCDBGFSELINFO pSelInfo, RTSEL SelCPL)
{
    /*
     * Check if present.
     */
    if (pSelInfo->u.Raw.Gen.u1Present)
    {
        /*
         * Type check.
         */
        if (    pSelInfo->u.Raw.Gen.u1DescType == 1
            &&  (pSelInfo->u.Raw.Gen.u4Type & X86_SEL_TYPE_CODE))
        {
            /*
             * Check level.
             */
            unsigned uLevel = RT_MAX(SelCPL & X86_SEL_RPL, pSelInfo->Sel & X86_SEL_RPL);
            if (    !(pSelInfo->u.Raw.Gen.u4Type & X86_SEL_TYPE_CONF)
                ?   uLevel <= pSelInfo->u.Raw.Gen.u2Dpl
                :   uLevel >= pSelInfo->u.Raw.Gen.u2Dpl /* hope I got this right now... */
                    )
                return VINF_SUCCESS;
            return VERR_INVALID_RPL;
        }
        return VERR_NOT_CODE_SELECTOR;
    }
    return VERR_SELECTOR_NOT_PRESENT;
}


/**
 * Converts a PGM paging mode to a set of DBGFPGDMP_XXX flags.
 *
 * @returns Flags. UINT32_MAX if the mode is invalid (asserted).
 * @param   enmMode             The mode.
 */
static uint32_t dbgfR3PagingDumpModeToFlags(PGMMODE enmMode)
{
    switch (enmMode)
    {
#if !defined(VBOX_VMM_TARGET_ARMV8)
        case PGMMODE_32_BIT:
            return DBGFPGDMP_FLAGS_PSE;
        case PGMMODE_PAE:
            return DBGFPGDMP_FLAGS_PSE | DBGFPGDMP_FLAGS_PAE;
        case PGMMODE_PAE_NX:
            return DBGFPGDMP_FLAGS_PSE | DBGFPGDMP_FLAGS_PAE | DBGFPGDMP_FLAGS_NXE;
        case PGMMODE_AMD64:
            return DBGFPGDMP_FLAGS_PSE | DBGFPGDMP_FLAGS_PAE | DBGFPGDMP_FLAGS_LME;
        case PGMMODE_AMD64_NX:
            return DBGFPGDMP_FLAGS_PSE | DBGFPGDMP_FLAGS_PAE | DBGFPGDMP_FLAGS_LME | DBGFPGDMP_FLAGS_NXE;
        case PGMMODE_NESTED_32BIT:
            return DBGFPGDMP_FLAGS_NP | DBGFPGDMP_FLAGS_PSE;
        case PGMMODE_NESTED_PAE:
            return DBGFPGDMP_FLAGS_NP | DBGFPGDMP_FLAGS_PSE | DBGFPGDMP_FLAGS_PAE | DBGFPGDMP_FLAGS_NXE;
        case PGMMODE_NESTED_AMD64:
            return DBGFPGDMP_FLAGS_NP | DBGFPGDMP_FLAGS_PSE | DBGFPGDMP_FLAGS_PAE | DBGFPGDMP_FLAGS_LME | DBGFPGDMP_FLAGS_NXE;
        case PGMMODE_EPT:
            return DBGFPGDMP_FLAGS_EPT;
        case PGMMODE_NONE:
            return 0;
        default:
            AssertFailedReturn(UINT32_MAX);
#else
        case PGMMODE_NONE:
            return 0;
        default:
            AssertFailedReturn(UINT32_MAX);
#endif
    }
}


/**
 * EMT worker for DBGFR3PagingDumpEx.
 *
 * @returns VBox status code.
 * @param   pUVM            The shared VM handle.
 * @param   idCpu           The current CPU ID.
 * @param   fFlags          The flags, DBGFPGDMP_FLAGS_XXX.  Valid.
 * @param   pcr3            The CR3 to use (unless we're getting the current
 *                          state, see @a fFlags).
 * @param   pu64FirstAddr   The first address.
 * @param   pu64LastAddr    The last address.
 * @param   cMaxDepth       The depth.
 * @param   pHlp            The output callbacks.
 */
static DECLCALLBACK(int) dbgfR3PagingDumpEx(PUVM pUVM, VMCPUID idCpu, uint32_t fFlags, uint64_t *pcr3,
                                            uint64_t *pu64FirstAddr, uint64_t *pu64LastAddr,
                                            uint32_t cMaxDepth, PCDBGFINFOHLP pHlp)
{
    /*
     * Implement dumping both context by means of recursion.
     */
    if ((fFlags & (DBGFPGDMP_FLAGS_GUEST | DBGFPGDMP_FLAGS_SHADOW)) == (DBGFPGDMP_FLAGS_GUEST | DBGFPGDMP_FLAGS_SHADOW))
    {
        int rc1 = dbgfR3PagingDumpEx(pUVM, idCpu, fFlags & ~DBGFPGDMP_FLAGS_GUEST,
                                     pcr3, pu64FirstAddr, pu64LastAddr, cMaxDepth, pHlp);
        int rc2 = dbgfR3PagingDumpEx(pUVM, idCpu, fFlags & ~DBGFPGDMP_FLAGS_SHADOW,
                                     pcr3, pu64FirstAddr, pu64LastAddr, cMaxDepth, pHlp);
        return RT_FAILURE(rc1) ? rc1 : rc2;
    }

    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);

    /*
     * Get the current CR3/mode if required.
     */
    uint64_t cr3 = *pcr3;
    if (fFlags & (DBGFPGDMP_FLAGS_CURRENT_CR3 | DBGFPGDMP_FLAGS_CURRENT_MODE))
    {
        PVMCPU pVCpu = pVM->apCpusR3[idCpu];
        if (fFlags & DBGFPGDMP_FLAGS_SHADOW)
        {
            if (PGMGetShadowMode(pVCpu) == PGMMODE_NONE)
            {
                pHlp->pfnPrintf(pHlp, "Shadow paging mode is 'none' (NEM)\n");
                return VINF_SUCCESS;
            }

#if !defined(VBOX_VMM_TARGET_ARMV8)
            if (fFlags & DBGFPGDMP_FLAGS_CURRENT_CR3)
                cr3 = PGMGetHyperCR3(pVCpu);
#endif
            if (fFlags & DBGFPGDMP_FLAGS_CURRENT_MODE)
                fFlags |= dbgfR3PagingDumpModeToFlags(PGMGetShadowMode(pVCpu));
        }
        else
        {
#if defined(VBOX_VMM_TARGET_ARMV8)
            AssertReleaseFailed();
#else
            if (fFlags & DBGFPGDMP_FLAGS_CURRENT_CR3)
                cr3 = CPUMGetGuestCR3(pVCpu);
            if (fFlags & DBGFPGDMP_FLAGS_CURRENT_MODE)
            {
                AssertCompile(DBGFPGDMP_FLAGS_PSE == X86_CR4_PSE);      AssertCompile(DBGFPGDMP_FLAGS_PAE == X86_CR4_PAE);
                fFlags |= CPUMGetGuestCR4(pVCpu)  & (X86_CR4_PSE | X86_CR4_PAE);
                AssertCompile(DBGFPGDMP_FLAGS_LME == MSR_K6_EFER_LME);  AssertCompile(DBGFPGDMP_FLAGS_NXE == MSR_K6_EFER_NXE);
                fFlags |= CPUMGetGuestEFER(pVCpu) & (MSR_K6_EFER_LME | MSR_K6_EFER_NXE);
            }
#endif
        }
    }
    fFlags &= ~(DBGFPGDMP_FLAGS_CURRENT_MODE | DBGFPGDMP_FLAGS_CURRENT_CR3);

    /*
     * Call PGM to do the real work.
     */
    int rc;
    if (fFlags & DBGFPGDMP_FLAGS_SHADOW)
        rc = PGMR3DumpHierarchyShw(pVM, cr3, fFlags, *pu64FirstAddr, *pu64LastAddr, cMaxDepth, pHlp);
    else
        rc = PGMR3DumpHierarchyGst(pVM, cr3, fFlags, *pu64FirstAddr, *pu64LastAddr, cMaxDepth, pHlp);
    return rc;
}


/**
 * Dump paging structures.
 *
 * This API can be used to dump both guest and shadow structures.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   idCpu           The current CPU ID.
 * @param   fFlags          The flags, DBGFPGDMP_FLAGS_XXX.
 * @param   cr3             The CR3 to use (unless we're getting the current
 *                          state, see @a fFlags).
 * @param   u64FirstAddr    The address to start dumping at.
 * @param   u64LastAddr     The address to end dumping after.
 * @param   cMaxDepth       The depth.
 * @param   pHlp            The output callbacks.  Defaults to the debug log if
 *                          NULL.
 */
VMMDECL(int) DBGFR3PagingDumpEx(PUVM pUVM, VMCPUID idCpu, uint32_t fFlags, uint64_t cr3, uint64_t u64FirstAddr,
                                uint64_t u64LastAddr, uint32_t cMaxDepth, PCDBGFINFOHLP pHlp)
{
    /*
     * Input validation.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(idCpu < pUVM->cCpus, VERR_INVALID_CPU_ID);
    AssertReturn(!(fFlags & ~DBGFPGDMP_FLAGS_VALID_MASK), VERR_INVALID_FLAGS);
    AssertReturn(fFlags & (DBGFPGDMP_FLAGS_SHADOW | DBGFPGDMP_FLAGS_GUEST), VERR_INVALID_FLAGS);
    AssertReturn((fFlags & DBGFPGDMP_FLAGS_CURRENT_MODE) || (fFlags & DBGFPGDMP_FLAGS_MODE_MASK), VERR_INVALID_FLAGS);
    AssertReturn(   !(fFlags & DBGFPGDMP_FLAGS_EPT)
                 || !(fFlags & (DBGFPGDMP_FLAGS_LME | DBGFPGDMP_FLAGS_PAE | DBGFPGDMP_FLAGS_PSE | DBGFPGDMP_FLAGS_NXE))
                 , VERR_INVALID_FLAGS);
    AssertReturn(cMaxDepth, VERR_INVALID_PARAMETER);

    /*
     * Forward the request to the target CPU.
     */
    return VMR3ReqPriorityCallWaitU(pUVM, idCpu, (PFNRT)dbgfR3PagingDumpEx, 8,
                                    pUVM, idCpu, fFlags, &cr3, &u64FirstAddr, &u64LastAddr, cMaxDepth,
                                    pHlp ? pHlp : DBGFR3InfoLogHlp());
}

