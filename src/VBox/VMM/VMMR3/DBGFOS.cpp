/* $Id: DBGFOS.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, Guest OS Diggers.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <VBox/vmm/mm.h>
#include <VBox/vmm/vmm.h>
#include "DBGFInternal.h"
#include <VBox/vmm/uvm.h>
#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/assert.h>
#include <iprt/thread.h>
#include <iprt/param.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

#define DBGF_OS_READ_LOCK(pUVM) \
    do { int rcLock = RTCritSectRwEnterShared(&pUVM->dbgf.s.CritSect); AssertRC(rcLock); } while (0)
#define DBGF_OS_READ_UNLOCK(pUVM) \
    do { int rcLock = RTCritSectRwLeaveShared(&pUVM->dbgf.s.CritSect); AssertRC(rcLock); } while (0)

#define DBGF_OS_WRITE_LOCK(pUVM) \
    do { int rcLock = RTCritSectRwEnterExcl(&pUVM->dbgf.s.CritSect); AssertRC(rcLock); } while (0)
#define DBGF_OS_WRITE_UNLOCK(pUVM) \
    do { int rcLock = RTCritSectRwLeaveExcl(&pUVM->dbgf.s.CritSect); AssertRC(rcLock); } while (0)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * EMT interface wrappers.
 *
 * The diggers expects to be called on an EMT.  To avoid the debugger+Main having
 *
 * Since the user (debugger/Main) shouldn't be calling directly into the digger code, but rather
 */
typedef struct DBGFOSEMTWRAPPER
{
    /** Pointer to the next list entry. */
    struct DBGFOSEMTWRAPPER    *pNext;
    /** The interface type.   */
    DBGFOSINTERFACE             enmIf;
    /** The digger interface pointer. */
    union
    {
        /** Generic void pointer. */
        void                    *pv;
        /** DBGFOSINTERFACE_DMESG.*/
        PDBGFOSIDMESG           pDmesg;
        /** DBGFOSINTERFACE_WINNT.*/
        PDBGFOSIWINNT           pWinNt;
    } uDigger;
    /** The user mode VM handle. */
    PUVM                        pUVM;
    /** The wrapper interface union (consult enmIf). */
    union
    {
        /** DBGFOSINTERFACE_DMESG.*/
        DBGFOSIDMESG            Dmesg;
        /** DBGFOSINTERFACE_WINNT.*/
        DBGFOSIWINNT            WinNt;
    } uWrapper;
} DBGFOSEMTWRAPPER;
/** Pointer to an EMT interface wrapper.   */
typedef DBGFOSEMTWRAPPER *PDBGFOSEMTWRAPPER;


/**
 * Internal init routine called by DBGFR3Init().
 *
 * @returns VBox status code.
 * @param   pUVM    The user mode VM handle.
 */
int dbgfR3OSInit(PUVM pUVM)
{
    RT_NOREF_PV(pUVM);
    return VINF_SUCCESS;
}


/**
 * Internal cleanup routine called by DBGFR3Term(), part 1.
 *
 * @param   pUVM    The user mode VM handle.
 */
void dbgfR3OSTermPart1(PUVM pUVM)
{
    DBGF_OS_WRITE_LOCK(pUVM);

    /*
     * Terminate the current one.
     */
    if (pUVM->dbgf.s.pCurOS)
    {
        pUVM->dbgf.s.pCurOS->pReg->pfnTerm(pUVM, VMMR3GetVTable(), pUVM->dbgf.s.pCurOS->abData);
        pUVM->dbgf.s.pCurOS = NULL;
    }

    DBGF_OS_WRITE_UNLOCK(pUVM);
}


/**
 * Internal cleanup routine called by DBGFR3Term(), part 2.
 *
 * @param   pUVM    The user mode VM handle.
 */
void dbgfR3OSTermPart2(PUVM pUVM)
{
    DBGF_OS_WRITE_LOCK(pUVM);

    /* This shouldn't happen. */
    AssertStmt(!pUVM->dbgf.s.pCurOS, dbgfR3OSTermPart1(pUVM));

    /*
     * Destroy all the instances.
     */
    while (pUVM->dbgf.s.pOSHead)
    {
        PDBGFOS pOS = pUVM->dbgf.s.pOSHead;
        pUVM->dbgf.s.pOSHead = pOS->pNext;
        if (pOS->pReg->pfnDestruct)
            pOS->pReg->pfnDestruct(pUVM, VMMR3GetVTable(), pOS->abData);

        PDBGFOSEMTWRAPPER pFree = pOS->pWrapperHead;
        while ((pFree = pOS->pWrapperHead) != NULL)
        {
            pOS->pWrapperHead = pFree->pNext;
            pFree->pNext = NULL;
            MMR3HeapFree(pFree);
        }

        MMR3HeapFree(pOS);
    }

    DBGF_OS_WRITE_UNLOCK(pUVM);
}


/**
 * EMT worker function for DBGFR3OSRegister.
 *
 * @returns VBox status code.
 * @param   pUVM    The user mode VM handle.
 * @param   pReg    The registration structure.
 */
static DECLCALLBACK(int) dbgfR3OSRegister(PUVM pUVM, PDBGFOSREG pReg)
{
    /* more validations. */
    DBGF_OS_READ_LOCK(pUVM);
    PDBGFOS pOS;
    for (pOS = pUVM->dbgf.s.pOSHead; pOS; pOS = pOS->pNext)
        if (!strcmp(pOS->pReg->szName, pReg->szName))
        {
            DBGF_OS_READ_UNLOCK(pUVM);
            Log(("dbgfR3OSRegister: %s -> VERR_ALREADY_LOADED\n", pReg->szName));
            return VERR_ALREADY_LOADED;
        }
    DBGF_OS_READ_UNLOCK(pUVM);

    /*
     * Allocate a new structure, call the constructor and link it into the list.
     */
    pOS = (PDBGFOS)MMR3HeapAllocZU(pUVM, MM_TAG_DBGF_OS, RT_UOFFSETOF_DYN(DBGFOS, abData[pReg->cbData]));
    AssertReturn(pOS, VERR_NO_MEMORY);
    pOS->pReg = pReg;

    int rc = pOS->pReg->pfnConstruct(pUVM, VMMR3GetVTable(), pOS->abData);
    if (RT_SUCCESS(rc))
    {
        DBGF_OS_WRITE_LOCK(pUVM);
        pOS->pNext = pUVM->dbgf.s.pOSHead;
        pUVM->dbgf.s.pOSHead = pOS;
        DBGF_OS_WRITE_UNLOCK(pUVM);
    }
    else
    {
        if (pOS->pReg->pfnDestruct)
            pOS->pReg->pfnDestruct(pUVM, VMMR3GetVTable(), pOS->abData);
        MMR3HeapFree(pOS);
    }

    return VINF_SUCCESS;
}


/**
 * Registers a guest OS digger.
 *
 * This will instantiate an instance of the digger and add it
 * to the list for us in the next call to DBGFR3OSDetect().
 *
 * @returns VBox status code.
 * @param   pUVM    The user mode VM handle.
 * @param   pReg    The registration structure.
 * @thread  Any.
 */
VMMR3DECL(int) DBGFR3OSRegister(PUVM pUVM, PCDBGFOSREG pReg)
{
    /*
     * Validate intput.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);

    AssertPtrReturn(pReg, VERR_INVALID_POINTER);
    AssertReturn(pReg->u32Magic == DBGFOSREG_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(pReg->u32EndMagic == DBGFOSREG_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(!pReg->fFlags, VERR_INVALID_PARAMETER);
    AssertReturn(pReg->cbData < _2G, VERR_INVALID_PARAMETER);
    AssertReturn(pReg->szName[0], VERR_INVALID_NAME);
    AssertReturn(RTStrEnd(&pReg->szName[0], sizeof(pReg->szName)), VERR_INVALID_NAME);
    AssertPtrReturn(pReg->pfnConstruct, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pReg->pfnDestruct, VERR_INVALID_POINTER);
    AssertPtrReturn(pReg->pfnProbe, VERR_INVALID_POINTER);
    AssertPtrReturn(pReg->pfnInit, VERR_INVALID_POINTER);
    AssertPtrReturn(pReg->pfnRefresh, VERR_INVALID_POINTER);
    AssertPtrReturn(pReg->pfnTerm, VERR_INVALID_POINTER);
    AssertPtrReturn(pReg->pfnQueryVersion, VERR_INVALID_POINTER);
    AssertPtrReturn(pReg->pfnQueryInterface, VERR_INVALID_POINTER);

    /*
     * Pass it on to EMT(0).
     */
    return VMR3ReqPriorityCallWaitU(pUVM, 0 /*idDstCpu*/, (PFNRT)dbgfR3OSRegister, 2, pUVM, pReg);
}


/**
 * EMT worker function for DBGFR3OSDeregister.
 *
 * @returns VBox status code.
 * @param   pUVM    The user mode VM handle.
 * @param   pReg    The registration structure.
 */
static DECLCALLBACK(int) dbgfR3OSDeregister(PUVM pUVM, PDBGFOSREG pReg)
{
    /*
     * Unlink it.
     */
    bool    fWasCurOS = false;
    PDBGFOS pOSPrev   = NULL;
    PDBGFOS pOS;
    DBGF_OS_WRITE_LOCK(pUVM);
    for (pOS = pUVM->dbgf.s.pOSHead; pOS; pOSPrev = pOS, pOS = pOS->pNext)
        if (pOS->pReg == pReg)
        {
            if (pOSPrev)
                pOSPrev->pNext = pOS->pNext;
            else
                pUVM->dbgf.s.pOSHead = pOS->pNext;
            if (pUVM->dbgf.s.pCurOS == pOS)
            {
                pUVM->dbgf.s.pCurOS = NULL;
                fWasCurOS = true;
            }
            break;
        }
    DBGF_OS_WRITE_UNLOCK(pUVM);
    if (!pOS)
    {
        Log(("DBGFR3OSDeregister: %s -> VERR_NOT_FOUND\n", pReg->szName));
        return VERR_NOT_FOUND;
    }

    /*
     * Terminate it if it was the current OS, then invoke the
     * destructor and clean up.
     */
    if (fWasCurOS)
        pOS->pReg->pfnTerm(pUVM, VMMR3GetVTable(), pOS->abData);
    if (pOS->pReg->pfnDestruct)
        pOS->pReg->pfnDestruct(pUVM, VMMR3GetVTable(), pOS->abData);

    PDBGFOSEMTWRAPPER pFree = pOS->pWrapperHead;
    while ((pFree = pOS->pWrapperHead) != NULL)
    {
        pOS->pWrapperHead = pFree->pNext;
        pFree->pNext = NULL;
        MMR3HeapFree(pFree);
    }

    MMR3HeapFree(pOS);

    return VINF_SUCCESS;
}


/**
 * Deregisters a guest OS digger previously registered by DBGFR3OSRegister.
 *
 * @returns VBox status code.
 *
 * @param   pUVM    The user mode VM handle.
 * @param   pReg    The registration structure.
 * @thread  Any.
 */
VMMR3DECL(int)  DBGFR3OSDeregister(PUVM pUVM, PCDBGFOSREG pReg)
{
    /*
     * Validate input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(pReg, VERR_INVALID_POINTER);
    AssertReturn(pReg->u32Magic == DBGFOSREG_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(pReg->u32EndMagic == DBGFOSREG_MAGIC, VERR_INVALID_MAGIC);
    AssertReturn(RTStrEnd(&pReg->szName[0], sizeof(pReg->szName)), VERR_INVALID_NAME);

    DBGF_OS_READ_LOCK(pUVM);
    PDBGFOS pOS;
    for (pOS = pUVM->dbgf.s.pOSHead; pOS; pOS = pOS->pNext)
        if (pOS->pReg == pReg)
            break;
    DBGF_OS_READ_UNLOCK(pUVM);

    if (!pOS)
    {
        Log(("DBGFR3OSDeregister: %s -> VERR_NOT_FOUND\n", pReg->szName));
        return VERR_NOT_FOUND;
    }

    /*
     * Pass it on to EMT(0).
     */
    return VMR3ReqPriorityCallWaitU(pUVM, 0 /*idDstCpu*/, (PFNRT)dbgfR3OSDeregister, 2, pUVM, pReg);
}


/**
 * EMT worker function for DBGFR3OSDetect.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if successfully detected.
 * @retval  VINF_DBGF_OS_NOT_DETCTED if we cannot figure it out.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   pszName     Where to store the OS name. Empty string if not detected.
 * @param   cchName     Size of the buffer.
 */
static DECLCALLBACK(int) dbgfR3OSDetect(PUVM pUVM, char *pszName, size_t cchName)
{
    /*
     * Cycle thru the detection routines.
     */
    DBGF_OS_WRITE_LOCK(pUVM);

    PDBGFOS const pOldOS = pUVM->dbgf.s.pCurOS;
    pUVM->dbgf.s.pCurOS = NULL;

    for (PDBGFOS pNewOS = pUVM->dbgf.s.pOSHead; pNewOS; pNewOS = pNewOS->pNext)
        if (pNewOS->pReg->pfnProbe(pUVM, VMMR3GetVTable(), pNewOS->abData))
        {
            int rc;
            pUVM->dbgf.s.pCurOS = pNewOS;
            if (pOldOS == pNewOS)
                rc = pNewOS->pReg->pfnRefresh(pUVM, VMMR3GetVTable(), pNewOS->abData);
            else
            {
                if (pOldOS)
                    pOldOS->pReg->pfnTerm(pUVM, VMMR3GetVTable(), pNewOS->abData);
                rc = pNewOS->pReg->pfnInit(pUVM, VMMR3GetVTable(), pNewOS->abData);
            }
            if (pszName && cchName)
                strncat(pszName, pNewOS->pReg->szName, cchName);

            DBGF_OS_WRITE_UNLOCK(pUVM);
            return rc;
        }

    /* not found */
    if (pOldOS)
        pOldOS->pReg->pfnTerm(pUVM, VMMR3GetVTable(), pOldOS->abData);

    DBGF_OS_WRITE_UNLOCK(pUVM);
    return VINF_DBGF_OS_NOT_DETCTED;
}


/**
 * Detects the guest OS and try dig out symbols and useful stuff.
 *
 * When called the 2nd time, symbols will be updated that if the OS
 * is the same.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if successfully detected.
 * @retval  VINF_DBGF_OS_NOT_DETCTED if we cannot figure it out.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   pszName     Where to store the OS name. Empty string if not detected.
 * @param   cchName     Size of the buffer.
 * @thread  Any.
 */
VMMR3DECL(int) DBGFR3OSDetect(PUVM pUVM, char *pszName, size_t cchName)
{
    AssertPtrNullReturn(pszName, VERR_INVALID_POINTER);
    if (pszName && cchName)
        *pszName = '\0';
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);

    /*
     * Pass it on to EMT(0).
     */
    return VMR3ReqPriorityCallWaitU(pUVM, 0 /*idDstCpu*/, (PFNRT)dbgfR3OSDetect, 3, pUVM, pszName, cchName);
}


/**
 * EMT worker function for DBGFR3OSQueryNameAndVersion
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   pszName         Where to store the OS name. Optional.
 * @param   cchName         The size of the name buffer.
 * @param   pszVersion      Where to store the version string. Optional.
 * @param   cchVersion      The size of the version buffer.
 */
static DECLCALLBACK(int) dbgfR3OSQueryNameAndVersion(PUVM pUVM, char *pszName, size_t cchName, char *pszVersion, size_t cchVersion)
{
    /*
     * Any known OS?
     */
    DBGF_OS_READ_LOCK(pUVM);

    if (pUVM->dbgf.s.pCurOS)
    {
        int rc = VINF_SUCCESS;
        if (pszName && cchName)
        {
            size_t cch = strlen(pUVM->dbgf.s.pCurOS->pReg->szName);
            if (cchName > cch)
                memcpy(pszName, pUVM->dbgf.s.pCurOS->pReg->szName, cch + 1);
            else
            {
                memcpy(pszName, pUVM->dbgf.s.pCurOS->pReg->szName, cchName - 1);
                pszName[cchName - 1] = '\0';
                rc = VINF_BUFFER_OVERFLOW;
            }
        }

        if (pszVersion && cchVersion)
        {
            int rc2 = pUVM->dbgf.s.pCurOS->pReg->pfnQueryVersion(pUVM, VMMR3GetVTable(),  pUVM->dbgf.s.pCurOS->abData,
                                                                 pszVersion, cchVersion);
            if (RT_FAILURE(rc2) || rc == VINF_SUCCESS)
                rc = rc2;
        }

        DBGF_OS_READ_UNLOCK(pUVM);
        return rc;
    }

    DBGF_OS_READ_UNLOCK(pUVM);
    return VERR_DBGF_OS_NOT_DETCTED;
}


/**
 * Queries the name and/or version string for the guest OS.
 *
 * It goes without saying that this querying is done using the current
 * guest OS digger and not additions or user configuration.
 *
 * @returns VBox status code.
 * @param   pUVM            The user mode VM handle.
 * @param   pszName         Where to store the OS name. Optional.
 * @param   cchName         The size of the name buffer.
 * @param   pszVersion      Where to store the version string. Optional.
 * @param   cchVersion      The size of the version buffer.
 * @thread  Any.
 */
VMMR3DECL(int) DBGFR3OSQueryNameAndVersion(PUVM pUVM, char *pszName, size_t cchName, char *pszVersion, size_t cchVersion)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertPtrNullReturn(pszName, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pszVersion, VERR_INVALID_POINTER);

    /*
     * Initialize the output up front.
     */
    if (pszName && cchName)
        *pszName = '\0';
    if (pszVersion && cchVersion)
        *pszVersion = '\0';

    /*
     * Pass it on to EMT(0).
     */
    return VMR3ReqPriorityCallWaitU(pUVM, 0 /*idDstCpu*/, (PFNRT)dbgfR3OSQueryNameAndVersion, 5,
                                    pUVM, pszName, cchName, pszVersion, cchVersion);
}


/**
 * @interface_method_impl{DBGFOSIDMESG,pfnQueryKernelLog, Generic EMT wrapper.}
 */
static DECLCALLBACK(int) dbgfR3OSEmtIDmesg_QueryKernelLog(PDBGFOSIDMESG pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, uint32_t fFlags,
                                                          uint32_t cMessages, char *pszBuf, size_t cbBuf, size_t *pcbActual)
{
    PDBGFOSEMTWRAPPER pWrapper = RT_FROM_MEMBER(pThis, DBGFOSEMTWRAPPER, uWrapper.Dmesg);
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(pUVM == pWrapper->pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(!fFlags, VERR_INVALID_FLAGS);
    AssertReturn(cMessages > 0, VERR_INVALID_PARAMETER);
    if (cbBuf)
        AssertPtrReturn(pszBuf, VERR_INVALID_POINTER);
    AssertPtrNullReturn(pcbActual, VERR_INVALID_POINTER);

    return VMR3ReqPriorityCallWaitU(pWrapper->pUVM, 0 /*idDstCpu*/,
                                   (PFNRT)pWrapper->uDigger.pDmesg->pfnQueryKernelLog, 8,
                                    pWrapper->uDigger.pDmesg, pUVM, pVMM, fFlags, cMessages, pszBuf, cbBuf, pcbActual);

}


/**
 * @interface_method_impl{DBGFOSIWINNT,pfnQueryVersion, Generic EMT wrapper.}
 */
static DECLCALLBACK(int) dbgfR3OSEmtIWinNt_QueryVersion(PDBGFOSIWINNT pThis, PUVM pUVM, PCVMMR3VTABLE pVMM, uint32_t *puVersMajor,
                                                        uint32_t *puVersMinor, uint32_t *puBuildNumber, bool *pf32Bit)
{
    PDBGFOSEMTWRAPPER pWrapper = RT_FROM_MEMBER(pThis, DBGFOSEMTWRAPPER, uWrapper.WinNt);
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(pUVM == pWrapper->pUVM, VERR_INVALID_VM_HANDLE);

    return VMR3ReqPriorityCallWaitU(pWrapper->pUVM, 0 /*idDstCpu*/,
                                   (PFNRT)pWrapper->uDigger.pWinNt->pfnQueryVersion, 7,
                                    pWrapper->uDigger.pWinNt, pUVM, pVMM, puVersMajor, puVersMinor,
                                    puBuildNumber, pf32Bit);
}


/**
 * @interface_method_impl{DBGFOSIWINNT,pfnQueryKernelPtrs, Generic EMT wrapper.}
 */
static DECLCALLBACK(int) dbgfR3OSEmtIWinNt_QueryKernelPtrs(PDBGFOSIWINNT pThis, PUVM pUVM, PCVMMR3VTABLE pVMM,
                                                           PRTGCUINTPTR pGCPtrKernBase, PRTGCUINTPTR pGCPtrPsLoadedModuleList)
{
    PDBGFOSEMTWRAPPER pWrapper = RT_FROM_MEMBER(pThis, DBGFOSEMTWRAPPER, uWrapper.WinNt);
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(pUVM == pWrapper->pUVM, VERR_INVALID_VM_HANDLE);

    return VMR3ReqPriorityCallWaitU(pWrapper->pUVM, 0 /*idDstCpu*/,
                                   (PFNRT)pWrapper->uDigger.pWinNt->pfnQueryKernelPtrs, 5,
                                    pWrapper->uDigger.pWinNt, pUVM, pVMM, pGCPtrKernBase, pGCPtrPsLoadedModuleList);
}


/**
 * @interface_method_impl{DBGFOSIWINNT,pfnQueryKpcrForVCpu, Generic EMT wrapper.}
 */
static DECLCALLBACK(int) dbgfR3OSEmtIWinNt_QueryKpcrForVCpu(struct DBGFOSIWINNT *pThis, PUVM pUVM, PCVMMR3VTABLE pVMM,
                                                            VMCPUID idCpu, PRTGCUINTPTR pKpcr, PRTGCUINTPTR pKpcrb)
{
    PDBGFOSEMTWRAPPER pWrapper = RT_FROM_MEMBER(pThis, DBGFOSEMTWRAPPER, uWrapper.WinNt);
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(pUVM == pWrapper->pUVM, VERR_INVALID_VM_HANDLE);

    return VMR3ReqPriorityCallWaitU(pWrapper->pUVM, 0 /*idDstCpu*/,
                                    (PFNRT)pWrapper->uDigger.pWinNt->pfnQueryKpcrForVCpu, 6,
                                    pWrapper->uDigger.pWinNt, pUVM, pVMM, idCpu, pKpcr, pKpcrb);
}


/**
 * @interface_method_impl{DBGFOSIWINNT,pfnQueryCurThrdForVCpu, Generic EMT wrapper.}
 */
static DECLCALLBACK(int) dbgfR3OSEmtIWinNt_QueryCurThrdForVCpu(struct DBGFOSIWINNT *pThis, PUVM pUVM, PCVMMR3VTABLE pVMM,
                                                               VMCPUID idCpu, PRTGCUINTPTR pCurThrd)
{
    PDBGFOSEMTWRAPPER pWrapper = RT_FROM_MEMBER(pThis, DBGFOSEMTWRAPPER, uWrapper.WinNt);
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(pUVM == pWrapper->pUVM, VERR_INVALID_VM_HANDLE);

    return VMR3ReqPriorityCallWaitU(pWrapper->pUVM, 0 /*idDstCpu*/,
                                    (PFNRT)pWrapper->uDigger.pWinNt->pfnQueryCurThrdForVCpu, 5,
                                    pWrapper->uDigger.pWinNt, pUVM, pVMM, idCpu, pCurThrd);
}


/**
 * EMT worker for DBGFR3OSQueryInterface.
 *
 * @param   pUVM            The user mode VM handle.
 * @param   enmIf           The interface identifier.
 * @param   ppvIf           Where to store the interface pointer on success.
 */
static DECLCALLBACK(void) dbgfR3OSQueryInterface(PUVM pUVM, DBGFOSINTERFACE enmIf, void **ppvIf)
{
    AssertPtrReturnVoid(ppvIf);
    *ppvIf = NULL;
    AssertReturnVoid(enmIf > DBGFOSINTERFACE_INVALID && enmIf < DBGFOSINTERFACE_END);
    UVM_ASSERT_VALID_EXT_RETURN_VOID(pUVM);

    /*
     * Forward the query to the current OS.
     */
    DBGF_OS_READ_LOCK(pUVM);
    PDBGFOS pOS = pUVM->dbgf.s.pCurOS;
    if (pOS)
    {
        void *pvDiggerIf;
        pvDiggerIf = pOS->pReg->pfnQueryInterface(pUVM, VMMR3GetVTable(), pUVM->dbgf.s.pCurOS->abData, enmIf);
        if (pvDiggerIf)
        {
            /*
             * Do we have an EMT wrapper for this interface already?
             *
             * We ASSUME the interfaces are static and not dynamically allocated
             * for each QueryInterface call.
             */
            PDBGFOSEMTWRAPPER pWrapper = pOS->pWrapperHead;
            while (   pWrapper != NULL
                   && (   pWrapper->uDigger.pv != pvDiggerIf
                       && pWrapper->enmIf      != enmIf) )
                pWrapper = pWrapper->pNext;
            if (pWrapper)
            {
                *ppvIf = &pWrapper->uWrapper;
                DBGF_OS_READ_UNLOCK(pUVM);
                return;
            }
            DBGF_OS_READ_UNLOCK(pUVM);

            /*
             * Create a wrapper.
             */
            int rc = MMR3HeapAllocExU(pUVM, MM_TAG_DBGF_OS, sizeof(*pWrapper), (void **)&pWrapper);
            if (RT_FAILURE(rc))
                return;
            pWrapper->uDigger.pv = pvDiggerIf;
            pWrapper->pUVM       = pUVM;
            pWrapper->enmIf      = enmIf;
            switch (enmIf)
            {
                case DBGFOSINTERFACE_DMESG:
                    pWrapper->uWrapper.Dmesg.u32Magic          = DBGFOSIDMESG_MAGIC;
                    pWrapper->uWrapper.Dmesg.pfnQueryKernelLog = dbgfR3OSEmtIDmesg_QueryKernelLog;
                    pWrapper->uWrapper.Dmesg.u32EndMagic       = DBGFOSIDMESG_MAGIC;
                    break;
                case DBGFOSINTERFACE_WINNT:
                    pWrapper->uWrapper.WinNt.u32Magic               = DBGFOSIWINNT_MAGIC;
                    pWrapper->uWrapper.WinNt.pfnQueryVersion        = dbgfR3OSEmtIWinNt_QueryVersion;
                    pWrapper->uWrapper.WinNt.pfnQueryKernelPtrs     = dbgfR3OSEmtIWinNt_QueryKernelPtrs;
                    pWrapper->uWrapper.WinNt.pfnQueryKpcrForVCpu    = dbgfR3OSEmtIWinNt_QueryKpcrForVCpu;
                    pWrapper->uWrapper.WinNt.pfnQueryCurThrdForVCpu = dbgfR3OSEmtIWinNt_QueryCurThrdForVCpu;
                    pWrapper->uWrapper.WinNt.u32EndMagic            = DBGFOSIWINNT_MAGIC;
                    break;
                default:
                    AssertFailed();
                    MMR3HeapFree(pWrapper);
                    return;
            }

            DBGF_OS_WRITE_LOCK(pUVM);
            if (pUVM->dbgf.s.pCurOS == pOS)
            {
                pWrapper->pNext = pOS->pWrapperHead;
                pOS->pWrapperHead = pWrapper;
                *ppvIf = &pWrapper->uWrapper;
                DBGF_OS_WRITE_UNLOCK(pUVM);
            }
            else
            {
                DBGF_OS_WRITE_UNLOCK(pUVM);
                MMR3HeapFree(pWrapper);
            }
            return;
        }
    }
    DBGF_OS_READ_UNLOCK(pUVM);
}


/**
 * Query an optional digger interface.
 *
 * @returns Pointer to the digger interface on success, NULL if the interfaces isn't
 *          available or no active guest OS digger.
 * @param   pUVM            The user mode VM handle.
 * @param   enmIf           The interface identifier.
 * @thread  Any.
 */
VMMR3DECL(void *) DBGFR3OSQueryInterface(PUVM pUVM, DBGFOSINTERFACE enmIf)
{
    AssertMsgReturn(enmIf > DBGFOSINTERFACE_INVALID && enmIf < DBGFOSINTERFACE_END, ("%d\n", enmIf), NULL);

    /*
     * Pass it on to an EMT.
     */
    void *pvIf = NULL;
    VMR3ReqPriorityCallVoidWaitU(pUVM, VMCPUID_ANY, (PFNRT)dbgfR3OSQueryInterface, 3, pUVM, enmIf, &pvIf);
    return pvIf;
}



/**
 * Internal wrapper for calling DBGFOSREG::pfnStackUnwindAssist.
 */
int dbgfR3OSStackUnwindAssist(PUVM pUVM, VMCPUID idCpu, PDBGFSTACKFRAME pFrame, PRTDBGUNWINDSTATE pState,
                              PCCPUMCTX pInitialCtx, RTDBGAS hAs, uint64_t *puScratch)
{
    int rc = VINF_SUCCESS;
    if (pUVM->dbgf.s.pCurOS)
    {
        ASMCompilerBarrier();
        DBGF_OS_READ_LOCK(pUVM);
        PDBGFOS pOS = pUVM->dbgf.s.pCurOS;
        if (pOS)
            rc = pOS->pReg->pfnStackUnwindAssist(pUVM, VMMR3GetVTable(), pUVM->dbgf.s.pCurOS->abData, idCpu, pFrame,
                                                 pState, pInitialCtx, hAs, puScratch);
        DBGF_OS_READ_UNLOCK(pUVM);
    }
    return rc;
}

