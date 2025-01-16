/* $Id: DBGFReg.cpp 107888 2025-01-16 00:28:32Z knut.osmundsen@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, Register Methods.
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
#include <VBox/vmm/dbgf.h>
#include "DBGFInternal.h"
#include <VBox/vmm/mm.h>
#include <VBox/vmm/vm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/param.h>
#include <VBox/err.h>
#include <VBox/log.h>
#include <iprt/ctype.h>
#include <iprt/string.h>
#include <iprt/uint128.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Locks the register database for writing. */
#define DBGF_REG_DB_LOCK_WRITE(pUVM) \
    do { \
        int rcSem = RTSemRWRequestWrite((pUVM)->dbgf.s.hRegDbLock, RT_INDEFINITE_WAIT); \
        AssertRC(rcSem); \
    } while (0)

/** Unlocks the register database after writing. */
#define DBGF_REG_DB_UNLOCK_WRITE(pUVM) \
    do { \
        int rcSem = RTSemRWReleaseWrite((pUVM)->dbgf.s.hRegDbLock); \
        AssertRC(rcSem); \
    } while (0)

/** Locks the register database for reading. */
#define DBGF_REG_DB_LOCK_READ(pUVM) \
    do { \
        int rcSem = RTSemRWRequestRead((pUVM)->dbgf.s.hRegDbLock, RT_INDEFINITE_WAIT); \
        AssertRC(rcSem); \
    } while (0)

/** Unlocks the register database after reading. */
#define DBGF_REG_DB_UNLOCK_READ(pUVM) \
    do { \
        int rcSem = RTSemRWReleaseRead((pUVM)->dbgf.s.hRegDbLock); \
        AssertRC(rcSem); \
    } while (0)


/** The max length of a set, register or sub-field name. */
#define DBGF_REG_MAX_NAME       40


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Register set registration record type.
 */
typedef enum DBGFREGSETTYPE
{
    /** Invalid zero value. */
    DBGFREGSETTYPE_INVALID = 0,
    /** CPU record. */
    DBGFREGSETTYPE_CPU,
    /** Device record. */
    DBGFREGSETTYPE_DEVICE,
    /** End of valid record types. */
    DBGFREGSETTYPE_END
} DBGFREGSETTYPE;


/**
 * Register set registration record.
 */
typedef struct DBGFREGSET
{
    /** String space core. */
    RTSTRSPACECORE          Core;
    /** The registration record type. */
    DBGFREGSETTYPE          enmType;
    /** The user argument for the callbacks. */
    union
    {
        /** The CPU view. */
        PVMCPU              pVCpu;
        /** The device view. */
        PPDMDEVINS          pDevIns;
        /** The general view. */
        void               *pv;
    } uUserArg;

    /** The register descriptors. */
    PCDBGFREGDESC           paDescs;
    /** The number of register descriptors. */
    uint32_t                cDescs;

    /** Array of lookup records.
     * The first part of the array runs parallel to paDescs, the rest are
     * covering for aliases and bitfield variations.  It's done this way to
     * simplify the query all operations. */
    struct DBGFREGLOOKUP   *paLookupRecs;
    /** The number of lookup records. */
    uint32_t                cLookupRecs;

    /** The register name prefix. */
    char                    szPrefix[1];
} DBGFREGSET;
/** Pointer to a register registration record. */
typedef DBGFREGSET *PDBGFREGSET;
/** Pointer to a const register registration record. */
typedef DBGFREGSET const *PCDBGFREGSET;


/**
 * Register lookup record.
 */
typedef struct DBGFREGLOOKUP
{
    /** The string space core. */
    RTSTRSPACECORE      Core;
    /** Pointer to the set. */
    PCDBGFREGSET        pSet;
    /** Pointer to the register descriptor. */
    PCDBGFREGDESC       pDesc;
    /** If an alias this points to the alias descriptor, NULL if not. */
    PCDBGFREGALIAS      pAlias;
    /** If a sub-field this points to the sub-field descriptor, NULL if not. */
    PCDBGFREGSUBFIELD   pSubField;
} DBGFREGLOOKUP;
/** Pointer to a register lookup record. */
typedef DBGFREGLOOKUP *PDBGFREGLOOKUP;
/** Pointer to a const register lookup record. */
typedef DBGFREGLOOKUP const *PCDBGFREGLOOKUP;


/**
 * Argument packet from DBGFR3RegNmQueryAll to dbgfR3RegNmQueryAllWorker.
 */
typedef struct DBGFR3REGNMQUERYALLARGS
{
    /** The output register array. */
    PDBGFREGENTRYNM paRegs;
    /** The number of entries in the output array. */
    size_t          cRegs;
    /** The current register number when enumerating the string space.
     * @remarks Only used by EMT(0). */
    size_t          iReg;
} DBGFR3REGNMQUERYALLARGS;
/** Pointer to a dbgfR3RegNmQueryAllWorker argument packet. */
typedef DBGFR3REGNMQUERYALLARGS *PDBGFR3REGNMQUERYALLARGS;


/**
 * Argument packet passed by DBGFR3RegPrintfV to dbgfR3RegPrintfCbOutput and
 * dbgfR3RegPrintfCbFormat.
 */
typedef struct DBGFR3REGPRINTFARGS
{
    /** The user mode VM handle. */
    PUVM        pUVM;
    /** The target CPU. */
    VMCPUID     idCpu;
    /** Set if we're looking at guest registers. */
    bool        fGuestRegs;
    /** The output buffer. */
    char       *pszBuf;
    /** The format string. */
    const char *pszFormat;
    /** The va list with format arguments. */
    va_list     va;

    /** The current buffer offset. */
    size_t      offBuf;
    /** The amount of buffer space left, not counting the terminator char. */
    size_t      cchLeftBuf;
    /** The status code of the whole operation.  First error is return,
     * subsequent ones are suppressed. */
    int         rc;
} DBGFR3REGPRINTFARGS;
/** Pointer to a DBGFR3RegPrintfV argument packet. */
typedef DBGFR3REGPRINTFARGS *PDBGFR3REGPRINTFARGS;



/**
 * Initializes the register database.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 */
int dbgfR3RegInit(PUVM pUVM)
{
    int  rc   = VINF_SUCCESS;
    if (!pUVM->dbgf.s.fRegDbInitialized)
    {
        rc = RTSemRWCreate(&pUVM->dbgf.s.hRegDbLock);
        pUVM->dbgf.s.fRegDbInitialized = RT_SUCCESS(rc);
    }
    return rc;
}


/**
 * Terminates the register database.
 *
 * @param   pUVM                The user mode VM handle.
 */
void dbgfR3RegTerm(PUVM pUVM)
{
    RTSemRWDestroy(pUVM->dbgf.s.hRegDbLock);
    pUVM->dbgf.s.hRegDbLock = NIL_RTSEMRW;
    pUVM->dbgf.s.fRegDbInitialized = false;
}


/**
 * Validates a register name.
 *
 * This is used for prefixes, aliases and field names.
 *
 * @returns true if valid, false if not.
 * @param   pszName             The register name to validate.
 * @param   chDot               Set to '.' if accepted, otherwise 0.
 */
static bool dbgfR3RegIsNameValid(const char *pszName, char chDot)
{
    const char *psz = pszName;
    if (!RT_C_IS_ALPHA(*psz))
        return false;
    char ch;
    while ((ch = *++psz))
        if (   !RT_C_IS_LOWER(ch)
            && !RT_C_IS_DIGIT(ch)
            && ch != '_'
            && ch != chDot)
            return false;
    if (psz - pszName > DBGF_REG_MAX_NAME)
        return false;
    return true;
}


/**
 * Common worker for registering a register set.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   paRegisters         The register descriptors.
 * @param   enmType             The set type.
 * @param   pvUserArg           The user argument for the callbacks.
 * @param   pszPrefix           The name prefix.
 * @param   iInstance           The instance number to be appended to @a
 *                              pszPrefix when creating the set name.
 */
static int dbgfR3RegRegisterCommon(PUVM pUVM, PCDBGFREGDESC paRegisters, DBGFREGSETTYPE enmType, void *pvUserArg,
                                   const char *pszPrefix, uint32_t iInstance)
{
    /*
     * Validate input.
     */
    /* The name components. */
    AssertMsgReturn(dbgfR3RegIsNameValid(pszPrefix, 0), ("%s\n", pszPrefix), VERR_INVALID_NAME);
    const char  *psz             = RTStrEnd(pszPrefix, RTSTR_MAX);
    bool const   fNeedUnderscore = RT_C_IS_DIGIT(psz[-1]);
    size_t const cchPrefix       = psz - pszPrefix + fNeedUnderscore;
    AssertMsgReturn(cchPrefix < RT_SIZEOFMEMB(DBGFREGSET, szPrefix) - 4 - 1, ("%s\n", pszPrefix), VERR_INVALID_NAME);

    AssertMsgReturn(iInstance <= 9999, ("%d\n", iInstance), VERR_INVALID_NAME);

    /* The descriptors. */
    uint32_t cLookupRecs = 0;
    uint32_t iDesc;
    for (iDesc = 0; paRegisters[iDesc].pszName != NULL; iDesc++)
    {
        AssertMsgReturn(dbgfR3RegIsNameValid(paRegisters[iDesc].pszName, 0), ("%s (#%u)\n", paRegisters[iDesc].pszName, iDesc), VERR_INVALID_NAME);

        if (enmType == DBGFREGSETTYPE_CPU)
#if defined(VBOX_VMM_TARGET_ARMV8)
            /** @todo This needs a general solution to avoid architecture dependent stuff here. */
            AssertMsgReturn(iDesc < (unsigned)DBGFREG_END,
                            ("%d iDesc=%d\n", paRegisters[iDesc].enmReg, iDesc),
                            VERR_INVALID_PARAMETER);
#else
            AssertMsgReturn(iDesc < (unsigned)DBGFREG_END && (unsigned)paRegisters[iDesc].enmReg == iDesc,
                            ("%d iDesc=%d\n", paRegisters[iDesc].enmReg, iDesc),
                            VERR_INVALID_PARAMETER);
#endif
        else
            AssertReturn(paRegisters[iDesc].enmReg == DBGFREG_END, VERR_INVALID_PARAMETER);
        AssertReturn(   paRegisters[iDesc].enmType > DBGFREGVALTYPE_INVALID
                     && paRegisters[iDesc].enmType < DBGFREGVALTYPE_END, VERR_INVALID_PARAMETER);
        AssertMsgReturn(!(paRegisters[iDesc].fFlags & ~DBGFREG_FLAGS_READ_ONLY),
                        ("%#x (#%u)\n", paRegisters[iDesc].fFlags, iDesc),
                        VERR_INVALID_PARAMETER);
        AssertPtrReturn(paRegisters[iDesc].pfnGet, VERR_INVALID_PARAMETER);
        AssertReturn(RT_VALID_PTR(paRegisters[iDesc].pfnSet) || (paRegisters[iDesc].fFlags & DBGFREG_FLAGS_READ_ONLY),
                     VERR_INVALID_PARAMETER);

        uint32_t        iAlias    = 0;
        PCDBGFREGALIAS  paAliases = paRegisters[iDesc].paAliases;
        if (paAliases)
        {
            AssertPtrReturn(paAliases, VERR_INVALID_PARAMETER);
            for (; paAliases[iAlias].pszName; iAlias++)
            {
                AssertMsgReturn(dbgfR3RegIsNameValid(paAliases[iAlias].pszName, 0), ("%s (%s)\n", paAliases[iAlias].pszName, paRegisters[iDesc].pszName), VERR_INVALID_NAME);
                AssertReturn(   paAliases[iAlias].enmType > DBGFREGVALTYPE_INVALID
                             && paAliases[iAlias].enmType < DBGFREGVALTYPE_END, VERR_INVALID_PARAMETER);
            }
        }

        uint32_t          iSubField   = 0;
        PCDBGFREGSUBFIELD paSubFields = paRegisters[iDesc].paSubFields;
        if (paSubFields)
        {
            AssertPtrReturn(paSubFields, VERR_INVALID_PARAMETER);
            for (; paSubFields[iSubField].pszName; iSubField++)
            {
                AssertMsgReturn(dbgfR3RegIsNameValid(paSubFields[iSubField].pszName, '.'), ("%s (%s)\n", paSubFields[iSubField].pszName, paRegisters[iDesc].pszName), VERR_INVALID_NAME);
                AssertReturn(paSubFields[iSubField].iFirstBit + paSubFields[iSubField].cBits <= 128, VERR_INVALID_PARAMETER);
                AssertReturn(paSubFields[iSubField].cBits + paSubFields[iSubField].cShift <= 128, VERR_INVALID_PARAMETER);
                AssertPtrNullReturn(paSubFields[iSubField].pfnGet, VERR_INVALID_POINTER);
                AssertPtrNullReturn(paSubFields[iSubField].pfnSet, VERR_INVALID_POINTER);
            }
        }

        cLookupRecs += (1 + iAlias) * (1 + iSubField);
    }

    /* Check the instance number of the CPUs. */
    AssertReturn(enmType != DBGFREGSETTYPE_CPU || iInstance < pUVM->cCpus, VERR_INVALID_CPU_ID);

    /*
     * Allocate a new record and all associated lookup records.
     */
    size_t cbRegSet = RT_UOFFSETOF_DYN(DBGFREGSET, szPrefix[cchPrefix + 4 + 1]);
    cbRegSet = RT_ALIGN_Z(cbRegSet, 32);
    size_t const offLookupRecArray = cbRegSet;
    cbRegSet += cLookupRecs * sizeof(DBGFREGLOOKUP);

    PDBGFREGSET pRegSet = (PDBGFREGSET)MMR3HeapAllocZU(pUVM, MM_TAG_DBGF_REG, cbRegSet);
    if (!pRegSet)
        return VERR_NO_MEMORY;

    /*
     * Initialize the new record.
     */
    pRegSet->Core.pszString = pRegSet->szPrefix;
    pRegSet->enmType        = enmType;
    pRegSet->uUserArg.pv    = pvUserArg;
    pRegSet->paDescs        = paRegisters;
    pRegSet->cDescs         = iDesc;
    pRegSet->cLookupRecs    = cLookupRecs;
    pRegSet->paLookupRecs   = (PDBGFREGLOOKUP)((uintptr_t)pRegSet + offLookupRecArray);
    if (fNeedUnderscore)
        RTStrPrintf(pRegSet->szPrefix, cchPrefix + 4 + 1, "%s_%u", pszPrefix, iInstance);
    else
        RTStrPrintf(pRegSet->szPrefix, cchPrefix + 4 + 1, "%s%u", pszPrefix, iInstance);

    /*
     * Initialize the lookup records. See DBGFREGSET::paLookupRecs.
     */
    char szName[DBGF_REG_MAX_NAME * 3 + 16];
    strcpy(szName, pRegSet->szPrefix);
    char *pszReg = strchr(szName, '\0');
    *pszReg++ = '.';

    /* Array parallel to the descriptors. */
    int             rc = VINF_SUCCESS;
    PDBGFREGLOOKUP  pLookupRec = &pRegSet->paLookupRecs[0];
    for (iDesc = 0; paRegisters[iDesc].pszName != NULL && RT_SUCCESS(rc); iDesc++)
    {
        strcpy(pszReg, paRegisters[iDesc].pszName);
        pLookupRec->Core.pszString = MMR3HeapStrDupU(pUVM, MM_TAG_DBGF_REG, szName);
        if (!pLookupRec->Core.pszString)
            rc = VERR_NO_STR_MEMORY;
        pLookupRec->pSet      = pRegSet;
        pLookupRec->pDesc     = &paRegisters[iDesc];
        pLookupRec->pAlias    = NULL;
        pLookupRec->pSubField = NULL;
        pLookupRec++;
    }

    /* Aliases and sub-fields. */
    for (iDesc = 0; paRegisters[iDesc].pszName != NULL && RT_SUCCESS(rc); iDesc++)
    {
        PCDBGFREGALIAS  pCurAlias  = NULL; /* first time we add sub-fields for the real name. */
        PCDBGFREGALIAS  pNextAlias = paRegisters[iDesc].paAliases;
        const char     *pszRegName = paRegisters[iDesc].pszName;
        while (RT_SUCCESS(rc))
        {
            /* Add sub-field records. */
            PCDBGFREGSUBFIELD paSubFields = paRegisters[iDesc].paSubFields;
            if (paSubFields)
            {
                size_t cchReg = strlen(pszRegName);
                memcpy(pszReg, pszRegName, cchReg);
                char *pszSub = &pszReg[cchReg];
                *pszSub++ = '.';
                for (uint32_t iSubField = 0; paSubFields[iSubField].pszName && RT_SUCCESS(rc); iSubField++)
                {
                    strcpy(pszSub, paSubFields[iSubField].pszName);
                    pLookupRec->Core.pszString = MMR3HeapStrDupU(pUVM, MM_TAG_DBGF_REG, szName);
                    if (!pLookupRec->Core.pszString)
                        rc = VERR_NO_STR_MEMORY;
                    pLookupRec->pSet      = pRegSet;
                    pLookupRec->pDesc     = &paRegisters[iDesc];
                    pLookupRec->pAlias    = pCurAlias;
                    pLookupRec->pSubField = &paSubFields[iSubField];
                    pLookupRec++;
                }
            }

            /* Advance to the next alias. */
            pCurAlias = pNextAlias++;
            if (!pCurAlias)
                break;
            pszRegName = pCurAlias->pszName;
            if (!pszRegName)
                break;

            /* The alias record. */
            strcpy(pszReg, pszRegName);
            pLookupRec->Core.pszString = MMR3HeapStrDupU(pUVM, MM_TAG_DBGF_REG, szName);
            if (!pLookupRec->Core.pszString)
                rc = VERR_NO_STR_MEMORY;
            pLookupRec->pSet      = pRegSet;
            pLookupRec->pDesc     = &paRegisters[iDesc];
            pLookupRec->pAlias    = pCurAlias;
            pLookupRec->pSubField = NULL;
            pLookupRec++;
        }
    }
    Assert(pLookupRec == &pRegSet->paLookupRecs[pRegSet->cLookupRecs]);

    if (RT_SUCCESS(rc))
    {
        /*
         * Insert the record into the register set string space and optionally into
         * the CPU register set cache.
         */
        DBGF_REG_DB_LOCK_WRITE(pUVM);

        bool fInserted = RTStrSpaceInsert(&pUVM->dbgf.s.RegSetSpace, &pRegSet->Core);
        if (fInserted)
        {
            pUVM->dbgf.s.cRegs += pRegSet->cDescs;
            if (enmType == DBGFREGSETTYPE_CPU)
            {
                if (!strcmp(pszPrefix, "cpu"))
                {
                    if (!pUVM->dbgf.s.cPerCpuRegs)
                        pUVM->dbgf.s.cPerCpuRegs = pRegSet->cDescs;
                    else
                        AssertLogRelMsgStmt(pUVM->dbgf.s.cPerCpuRegs == pRegSet->cDescs,
                                            ("%d vs %d\n", pUVM->dbgf.s.cPerCpuRegs, pRegSet->cDescs),
                                            pUVM->dbgf.s.cPerCpuRegs = RT_MAX(pRegSet->cDescs, pUVM->dbgf.s.cPerCpuRegs));
                    pUVM->aCpus[iInstance].dbgf.s.pGuestRegSet = pRegSet;
                }
                else
                {
                    Assert(!strcmp(pszPrefix, "hypercpu"));
                    if (!pUVM->dbgf.s.cPerCpuHyperRegs)
                        pUVM->dbgf.s.cPerCpuHyperRegs = pRegSet->cDescs;
                    else
                        AssertLogRelMsgStmt(pUVM->dbgf.s.cPerCpuHyperRegs == pRegSet->cDescs,
                                            ("%d vs %d\n", pUVM->dbgf.s.cPerCpuHyperRegs, pRegSet->cDescs),
                                            pUVM->dbgf.s.cPerCpuHyperRegs = RT_MAX(pRegSet->cDescs, pUVM->dbgf.s.cPerCpuHyperRegs));
                    pUVM->aCpus[iInstance].dbgf.s.pHyperRegSet = pRegSet;
                }
            }

            PDBGFREGLOOKUP  paLookupRecs = pRegSet->paLookupRecs;
            uint32_t        iLookupRec   = pRegSet->cLookupRecs;
            while (iLookupRec-- > 0)
            {
                bool fInserted2 = RTStrSpaceInsert(&pUVM->dbgf.s.RegSpace, &paLookupRecs[iLookupRec].Core);
                AssertMsg(fInserted2, ("'%s'", paLookupRecs[iLookupRec].Core.pszString)); NOREF(fInserted2);
            }

            DBGF_REG_DB_UNLOCK_WRITE(pUVM);
            return VINF_SUCCESS;
        }

        DBGF_REG_DB_UNLOCK_WRITE(pUVM);
        rc = VERR_DUPLICATE;
    }

    /*
     * Bail out.
     */
    for (uint32_t i = 0; i < pRegSet->cLookupRecs; i++)
        MMR3HeapFree((char *)pRegSet->paLookupRecs[i].Core.pszString);
    MMR3HeapFree(pRegSet);

    return rc;
}


/**
 * Registers a set of registers for a CPU.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   pVCpu           The cross context virtual CPU structure.
 * @param   paRegisters     The register descriptors.
 * @param   fGuestRegs      Set if it's the guest registers, clear if
 *                          hypervisor registers.
 */
VMMR3_INT_DECL(int) DBGFR3RegRegisterCpu(PVM pVM, PVMCPU pVCpu, PCDBGFREGDESC paRegisters, bool fGuestRegs)
{
    PUVM pUVM = pVM->pUVM;
    if (!pUVM->dbgf.s.fRegDbInitialized)
    {
        int rc = dbgfR3RegInit(pUVM);
        if (RT_FAILURE(rc))
            return rc;
    }

    AssertReturn(fGuestRegs, VERR_RAW_MODE_NOT_SUPPORTED);
    return dbgfR3RegRegisterCommon(pUVM, paRegisters, DBGFREGSETTYPE_CPU, pVCpu, fGuestRegs ? "cpu" : "hypercpu", pVCpu->idCpu);
}


/**
 * Registers a set of registers for a device.
 *
 * @returns VBox status code.
 * @param   pVM             The cross context VM structure.
 * @param   paRegisters     The register descriptors.
 * @param   pDevIns         The device instance. This will be the callback user
 *                          argument.
 * @param   pszPrefix       The device name.
 * @param   iInstance       The device instance.
 */
VMMR3_INT_DECL(int) DBGFR3RegRegisterDevice(PVM pVM, PCDBGFREGDESC paRegisters, PPDMDEVINS pDevIns,
                                            const char *pszPrefix, uint32_t iInstance)
{
    AssertPtrReturn(paRegisters, VERR_INVALID_POINTER);
    AssertPtrReturn(pDevIns, VERR_INVALID_POINTER);
    AssertPtrReturn(pszPrefix, VERR_INVALID_POINTER);

    return dbgfR3RegRegisterCommon(pVM->pUVM, paRegisters, DBGFREGSETTYPE_DEVICE, pDevIns, pszPrefix, iInstance);
}


/**
 * Clears the register value variable.
 *
 * @param   pValue              The variable to clear.
 */
DECLINLINE(void) dbgfR3RegValClear(PDBGFREGVAL pValue)
{
    pValue->au64[0] = 0;
    pValue->au64[1] = 0;
    pValue->au64[2] = 0;
    pValue->au64[3] = 0;
    pValue->au64[4] = 0;
    pValue->au64[5] = 0;
    pValue->au64[6] = 0;
    pValue->au64[7] = 0;
}


/**
 * Sets a 80-bit floating point variable to a 64-bit unsigned interger value.
 *
 * @param   pValue              The value.
 * @param   u64                 The integer value.
 */
DECLINLINE(void) dbgfR3RegValR80SetU64(PDBGFREGVAL pValue, uint64_t u64)
{
    /** @todo fixme  */
    pValue->r80.s.fSign       = 0;
    pValue->r80.s.uExponent   = 16383;
    pValue->r80.s.uMantissa   = u64;
}


/**
 * Sets a 80-bit floating point variable to a 64-bit unsigned interger value.
 *
 * @param   pValue              The value.
 * @param   u128                The integer value.
 */
DECLINLINE(void) dbgfR3RegValR80SetU128(PDBGFREGVAL pValue, RTUINT128U u128)
{
    /** @todo fixme  */
    pValue->r80.s.fSign       = 0;
    pValue->r80.s.uExponent   = 16383;
    pValue->r80.s.uMantissa   = u128.s.Lo;
}


/**
 * Get a 80-bit floating point variable as a 64-bit unsigned integer.
 *
 * @returns 64-bit unsigned integer.
 * @param   pValue              The value.
 */
DECLINLINE(uint64_t) dbgfR3RegValR80GetU64(PCDBGFREGVAL pValue)
{
    /** @todo stupid, stupid MSC. */
    return pValue->r80.s.uMantissa;
}


/**
 * Get a 80-bit floating point variable as a 128-bit unsigned integer.
 *
 * @returns 128-bit unsigned integer.
 * @param   pValue              The value.
 */
DECLINLINE(RTUINT128U) dbgfR3RegValR80GetU128(PCDBGFREGVAL pValue)
{
    /** @todo stupid, stupid MSC. */
    RTUINT128U uRet;
#if 0
    uRet.s.Lo = (uint64_t)InVal.lrd;
    uRet.s.Hi = (uint64_t)InVal.lrd / _4G / _4G;
#else
    uRet.s.Lo = pValue->r80.s.uMantissa;
    uRet.s.Hi = 0;
#endif
    return uRet;
}


/**
 * Performs a cast between register value types.
 *
 * @retval  VINF_SUCCESS
 * @retval  VINF_DBGF_ZERO_EXTENDED_REGISTER
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 *
 * @param   pValue              The value to cast (input + output).
 * @param   enmFromType         The input value.
 * @param   enmToType           The desired output value.
 */
static int dbgfR3RegValCast(PDBGFREGVAL pValue, DBGFREGVALTYPE enmFromType, DBGFREGVALTYPE enmToType)
{
    DBGFREGVAL const InVal = *pValue;
    dbgfR3RegValClear(pValue);

    /* Note! No default cases here as gcc warnings about missing enum values
             are desired. */
    switch (enmFromType)
    {
        case DBGFREGVALTYPE_U8:
            switch (enmToType)
            {
                case DBGFREGVALTYPE_U8:     pValue->u8        = InVal.u8; return VINF_SUCCESS;
                case DBGFREGVALTYPE_U16:    pValue->u16       = InVal.u8; return VINF_DBGF_ZERO_EXTENDED_REGISTER;
                case DBGFREGVALTYPE_U32:    pValue->u32       = InVal.u8; return VINF_DBGF_ZERO_EXTENDED_REGISTER;
                case DBGFREGVALTYPE_U64:    pValue->u64       = InVal.u8; return VINF_DBGF_ZERO_EXTENDED_REGISTER;
                case DBGFREGVALTYPE_U128:   pValue->u128.s.Lo = InVal.u8; return VINF_DBGF_ZERO_EXTENDED_REGISTER;
                case DBGFREGVALTYPE_U256:   pValue->u256.Words.w0 = InVal.u8; return VINF_DBGF_ZERO_EXTENDED_REGISTER;
                case DBGFREGVALTYPE_U512:   pValue->u512.Words.w0 = InVal.u8; return VINF_DBGF_ZERO_EXTENDED_REGISTER;
                case DBGFREGVALTYPE_R80:    dbgfR3RegValR80SetU64(pValue, InVal.u8); return VINF_DBGF_ZERO_EXTENDED_REGISTER;
                case DBGFREGVALTYPE_DTR:                                  return VERR_DBGF_UNSUPPORTED_CAST;

                case DBGFREGVALTYPE_32BIT_HACK:
                case DBGFREGVALTYPE_END:
                case DBGFREGVALTYPE_INVALID:
                    break;
            }
            break;

        case DBGFREGVALTYPE_U16:
            switch (enmToType)
            {
                case DBGFREGVALTYPE_U8:     pValue->u8        = InVal.u16;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U16:    pValue->u16       = InVal.u16;  return VINF_SUCCESS;
                case DBGFREGVALTYPE_U32:    pValue->u32       = InVal.u16;  return VINF_DBGF_ZERO_EXTENDED_REGISTER;
                case DBGFREGVALTYPE_U64:    pValue->u64       = InVal.u16;  return VINF_DBGF_ZERO_EXTENDED_REGISTER;
                case DBGFREGVALTYPE_U128:   pValue->u128.s.Lo = InVal.u16;  return VINF_DBGF_ZERO_EXTENDED_REGISTER;
                case DBGFREGVALTYPE_U256:   pValue->u256.Words.w0 = InVal.u16;  return VINF_DBGF_ZERO_EXTENDED_REGISTER;
                case DBGFREGVALTYPE_U512:   pValue->u512.Words.w0 = InVal.u16;  return VINF_DBGF_ZERO_EXTENDED_REGISTER;
                case DBGFREGVALTYPE_R80:    dbgfR3RegValR80SetU64(pValue, InVal.u16); return VINF_DBGF_ZERO_EXTENDED_REGISTER;
                case DBGFREGVALTYPE_DTR:                                    return VERR_DBGF_UNSUPPORTED_CAST;

                case DBGFREGVALTYPE_32BIT_HACK:
                case DBGFREGVALTYPE_END:
                case DBGFREGVALTYPE_INVALID:
                    break;
            }
            break;

        case DBGFREGVALTYPE_U32:
            switch (enmToType)
            {
                case DBGFREGVALTYPE_U8:     pValue->u8        = InVal.u32;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U16:    pValue->u16       = InVal.u32;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U32:    pValue->u32       = InVal.u32;  return VINF_SUCCESS;
                case DBGFREGVALTYPE_U64:    pValue->u64       = InVal.u32;  return VINF_DBGF_ZERO_EXTENDED_REGISTER;
                case DBGFREGVALTYPE_U128:   pValue->u128.s.Lo = InVal.u32;  return VINF_DBGF_ZERO_EXTENDED_REGISTER;
                case DBGFREGVALTYPE_U256:   pValue->u256.DWords.dw0 = InVal.u32;  return VINF_DBGF_ZERO_EXTENDED_REGISTER;
                case DBGFREGVALTYPE_U512:   pValue->u512.DWords.dw0 = InVal.u32;  return VINF_DBGF_ZERO_EXTENDED_REGISTER;
                case DBGFREGVALTYPE_R80:    dbgfR3RegValR80SetU64(pValue, InVal.u32); return VINF_DBGF_ZERO_EXTENDED_REGISTER;
                case DBGFREGVALTYPE_DTR:                                    return VERR_DBGF_UNSUPPORTED_CAST;

                case DBGFREGVALTYPE_32BIT_HACK:
                case DBGFREGVALTYPE_END:
                case DBGFREGVALTYPE_INVALID:
                    break;
            }
            break;

        case DBGFREGVALTYPE_U64:
            switch (enmToType)
            {
                case DBGFREGVALTYPE_U8:     pValue->u8        = InVal.u64;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U16:    pValue->u16       = InVal.u64;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U32:    pValue->u32       = InVal.u64;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U64:    pValue->u64       = InVal.u64;  return VINF_SUCCESS;
                case DBGFREGVALTYPE_U128:   pValue->u128.s.Lo = InVal.u64;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U256:   pValue->u256.QWords.qw0 = InVal.u64;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U512:   pValue->u512.QWords.qw0 = InVal.u64;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_R80:    dbgfR3RegValR80SetU64(pValue, InVal.u64); return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_DTR:                                    return VERR_DBGF_UNSUPPORTED_CAST;

                case DBGFREGVALTYPE_32BIT_HACK:
                case DBGFREGVALTYPE_END:
                case DBGFREGVALTYPE_INVALID:
                    break;
            }
            break;

        case DBGFREGVALTYPE_U128:
            switch (enmToType)
            {
                case DBGFREGVALTYPE_U8:     pValue->u8        = InVal.u128.s.Lo;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U16:    pValue->u16       = InVal.u128.s.Lo;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U32:    pValue->u32       = InVal.u128.s.Lo;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U64:    pValue->u64       = InVal.u128.s.Lo;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U128:   pValue->u128      = InVal.u128;       return VINF_SUCCESS;
                case DBGFREGVALTYPE_U256:   pValue->u256.DQWords.dqw0 = InVal.u128; return VINF_SUCCESS;
                case DBGFREGVALTYPE_U512:   pValue->u512.DQWords.dqw0 = InVal.u128; return VINF_SUCCESS;
                case DBGFREGVALTYPE_R80:    dbgfR3RegValR80SetU128(pValue, InVal.u128); return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_DTR:                                          return VERR_DBGF_UNSUPPORTED_CAST;

                case DBGFREGVALTYPE_32BIT_HACK:
                case DBGFREGVALTYPE_END:
                case DBGFREGVALTYPE_INVALID:
                    break;
            }
            break;

        case DBGFREGVALTYPE_U256:
            switch (enmToType)
            {
                case DBGFREGVALTYPE_U8:     pValue->u8        = InVal.u256.Words.w0;        return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U16:    pValue->u16       = InVal.u256.Words.w0;        return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U32:    pValue->u32       = InVal.u256.DWords.dw0;      return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U64:    pValue->u64       = InVal.u256.QWords.qw0;      return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U128:   pValue->u128      = InVal.u256.DQWords.dqw0;    return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U256:   pValue->u256      = InVal.u256;                 return VINF_SUCCESS;
                case DBGFREGVALTYPE_U512:   pValue->u512.OWords.ow0 = InVal.u256;           return VINF_SUCCESS;
                case DBGFREGVALTYPE_R80:    dbgfR3RegValR80SetU128(pValue, InVal.u256.DQWords.dqw0); return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_DTR:                                                    return VERR_DBGF_UNSUPPORTED_CAST;

                case DBGFREGVALTYPE_32BIT_HACK:
                case DBGFREGVALTYPE_END:
                case DBGFREGVALTYPE_INVALID:
                    break;
            }
            break;

        case DBGFREGVALTYPE_U512:
            switch (enmToType)
            {
                case DBGFREGVALTYPE_U8:     pValue->u8        = InVal.u512.Words.w0;        return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U16:    pValue->u16       = InVal.u512.Words.w0;        return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U32:    pValue->u32       = InVal.u512.DWords.dw0;      return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U64:    pValue->u64       = InVal.u512.QWords.qw0;      return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U128:   pValue->u128      = InVal.u512.DQWords.dqw0;    return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U256:   pValue->u256      = InVal.u512.OWords.ow0;      return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U512:   pValue->u512      = InVal.u512;                 return VINF_SUCCESS;
                case DBGFREGVALTYPE_R80:    dbgfR3RegValR80SetU128(pValue, InVal.u512.DQWords.dqw0); return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_DTR:                                                    return VERR_DBGF_UNSUPPORTED_CAST;

                case DBGFREGVALTYPE_32BIT_HACK:
                case DBGFREGVALTYPE_END:
                case DBGFREGVALTYPE_INVALID:
                    break;
            }
            break;

        case DBGFREGVALTYPE_R80:
            switch (enmToType)
            {
                case DBGFREGVALTYPE_U8:     pValue->u8        = (uint8_t )dbgfR3RegValR80GetU64(&InVal);  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U16:    pValue->u16       = (uint16_t)dbgfR3RegValR80GetU64(&InVal);  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U32:    pValue->u32       = (uint32_t)dbgfR3RegValR80GetU64(&InVal);  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U64:    pValue->u64       = (uint64_t)dbgfR3RegValR80GetU64(&InVal);  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U128:   pValue->u128      = dbgfR3RegValR80GetU128(&InVal);           return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U256:   pValue->u256.DQWords.dqw0 = dbgfR3RegValR80GetU128(&InVal);   return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U512:   pValue->u512.DQWords.dqw0 = dbgfR3RegValR80GetU128(&InVal);   return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_R80:    pValue->r80       = InVal.r80;          return VINF_SUCCESS;
                case DBGFREGVALTYPE_DTR:                                            return VERR_DBGF_UNSUPPORTED_CAST;

                case DBGFREGVALTYPE_32BIT_HACK:
                case DBGFREGVALTYPE_END:
                case DBGFREGVALTYPE_INVALID:
                    break;
            }
            break;

        case DBGFREGVALTYPE_DTR:
            switch (enmToType)
            {
                case DBGFREGVALTYPE_U8:     pValue->u8        = InVal.dtr.u64Base;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U16:    pValue->u16       = InVal.dtr.u64Base;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U32:    pValue->u32       = InVal.dtr.u64Base;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U64:    pValue->u64       = InVal.dtr.u64Base;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U128:   pValue->u128.s.Lo = InVal.dtr.u64Base;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U256:   pValue->u256.QWords.qw0 = InVal.dtr.u64Base;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_U512:   pValue->u512.QWords.qw0 = InVal.dtr.u64Base;  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_R80:    dbgfR3RegValR80SetU64(pValue, InVal.dtr.u64Base);  return VINF_DBGF_TRUNCATED_REGISTER;
                case DBGFREGVALTYPE_DTR:    pValue->dtr       = InVal.dtr;          return VINF_SUCCESS;

                case DBGFREGVALTYPE_32BIT_HACK:
                case DBGFREGVALTYPE_END:
                case DBGFREGVALTYPE_INVALID:
                    break;
            }
            break;

        case DBGFREGVALTYPE_INVALID:
        case DBGFREGVALTYPE_END:
        case DBGFREGVALTYPE_32BIT_HACK:
            break;
    }

    AssertMsgFailed(("%d / %d\n", enmFromType, enmToType));
    return VERR_DBGF_UNSUPPORTED_CAST;
}


/**
 * Worker for the CPU register queries.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 * @retval  VINF_DBGF_ZERO_EXTENDED_REGISTER
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               The virtual CPU ID.
 * @param   enmReg              The register to query.
 * @param   enmType             The desired return type.
 * @param   fGuestRegs          Query guest CPU registers if set (true),
 *                              hypervisor CPU registers if clear (false).
 * @param   pValue              Where to return the register value.
 */
static DECLCALLBACK(int) dbgfR3RegCpuQueryWorkerOnCpu(PUVM pUVM, VMCPUID idCpu, DBGFREG enmReg, DBGFREGVALTYPE enmType,
                                                      bool fGuestRegs, PDBGFREGVAL pValue)
{
    int rc = VINF_SUCCESS;
    DBGF_REG_DB_LOCK_READ(pUVM);

    /*
     * Look up the register set of the specified CPU.
     */
    PDBGFREGSET pSet = fGuestRegs
                     ? pUVM->aCpus[idCpu].dbgf.s.pGuestRegSet
                     : pUVM->aCpus[idCpu].dbgf.s.pHyperRegSet;
    if (RT_LIKELY(pSet))
    {
        /*
         * Look up the register and get the register value.
         */
        if (RT_LIKELY(pSet->cDescs > (size_t)enmReg))
        {
            PCDBGFREGDESC pDesc = &pSet->paDescs[enmReg];

            pValue->au64[0] = pValue->au64[1] = 0;
            rc = pDesc->pfnGet(pSet->uUserArg.pv, pDesc, pValue);
            if (RT_SUCCESS(rc))
            {
                /*
                 * Do the cast if the desired return type doesn't match what
                 * the getter returned.
                 */
                if (pDesc->enmType == enmType)
                    rc = VINF_SUCCESS;
                else
                    rc = dbgfR3RegValCast(pValue, pDesc->enmType, enmType);
            }
        }
        else
            rc = VERR_DBGF_REGISTER_NOT_FOUND;
    }
    else
        rc = VERR_INVALID_CPU_ID;

    DBGF_REG_DB_UNLOCK_READ(pUVM);
    return rc;
}


/**
 * Internal worker for the CPU register query functions.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 * @retval  VINF_DBGF_ZERO_EXTENDED_REGISTER
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               The virtual CPU ID.  Can be OR'ed with
 *                              DBGFREG_HYPER_VMCPUID.
 * @param   enmReg              The register to query.
 * @param   enmType             The desired return type.
 * @param   pValue              Where to return the register value.
 */
static int dbgfR3RegCpuQueryWorker(PUVM pUVM, VMCPUID idCpu, DBGFREG enmReg, DBGFREGVALTYPE enmType, PDBGFREGVAL pValue)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, VERR_INVALID_VM_HANDLE);
    AssertMsgReturn(enmReg >= DBGFREG_AL && enmReg <= DBGFREG_END, ("%d\n", enmReg), VERR_INVALID_PARAMETER);

    bool const fGuestRegs = !(idCpu & DBGFREG_HYPER_VMCPUID);
    idCpu &= ~DBGFREG_HYPER_VMCPUID;
    AssertReturn(idCpu < pUVM->cCpus, VERR_INVALID_CPU_ID);

    return VMR3ReqPriorityCallWaitU(pUVM, idCpu, (PFNRT)dbgfR3RegCpuQueryWorkerOnCpu, 6,
                                    pUVM, idCpu, enmReg, enmType, fGuestRegs, pValue);
}


/**
 * Queries a 8-bit CPU register value.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               The target CPU ID. Can be OR'ed with
 *                              DBGFREG_HYPER_VMCPUID.
 * @param   enmReg              The register that's being queried.
 * @param   pu8                 Where to store the register value.
 */
VMMR3DECL(int) DBGFR3RegCpuQueryU8(PUVM pUVM, VMCPUID idCpu, DBGFREG enmReg, uint8_t *pu8)
{
    DBGFREGVAL Value;
    int rc = dbgfR3RegCpuQueryWorker(pUVM, idCpu, enmReg, DBGFREGVALTYPE_U8, &Value);
    if (RT_SUCCESS(rc))
        *pu8 = Value.u8;
    else
        *pu8 = 0;
    return rc;
}


/**
 * Queries a 16-bit CPU register value.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 * @retval  VINF_DBGF_ZERO_EXTENDED_REGISTER
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               The target CPU ID.  Can be OR'ed with
 *                              DBGFREG_HYPER_VMCPUID.
 * @param   enmReg              The register that's being queried.
 * @param   pu16                Where to store the register value.
 */
VMMR3DECL(int) DBGFR3RegCpuQueryU16(PUVM pUVM, VMCPUID idCpu, DBGFREG enmReg, uint16_t *pu16)
{
    DBGFREGVAL Value;
    int rc = dbgfR3RegCpuQueryWorker(pUVM, idCpu, enmReg, DBGFREGVALTYPE_U16, &Value);
    if (RT_SUCCESS(rc))
        *pu16 = Value.u16;
    else
        *pu16 = 0;
    return rc;
}


/**
 * Queries a 32-bit CPU register value.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 * @retval  VINF_DBGF_ZERO_EXTENDED_REGISTER
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               The target CPU ID.  Can be OR'ed with
 *                              DBGFREG_HYPER_VMCPUID.
 * @param   enmReg              The register that's being queried.
 * @param   pu32                Where to store the register value.
 */
VMMR3DECL(int) DBGFR3RegCpuQueryU32(PUVM pUVM, VMCPUID idCpu, DBGFREG enmReg, uint32_t *pu32)
{
    DBGFREGVAL Value;
    int rc = dbgfR3RegCpuQueryWorker(pUVM, idCpu, enmReg, DBGFREGVALTYPE_U32, &Value);
    if (RT_SUCCESS(rc))
        *pu32 = Value.u32;
    else
        *pu32 = 0;
    return rc;
}


/**
 * Queries a 64-bit CPU register value.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 * @retval  VINF_DBGF_ZERO_EXTENDED_REGISTER
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               The target CPU ID.  Can be OR'ed with
 *                              DBGFREG_HYPER_VMCPUID.
 * @param   enmReg              The register that's being queried.
 * @param   pu64                Where to store the register value.
 */
VMMR3DECL(int) DBGFR3RegCpuQueryU64(PUVM pUVM, VMCPUID idCpu, DBGFREG enmReg, uint64_t *pu64)
{
    DBGFREGVAL Value;
    int rc = dbgfR3RegCpuQueryWorker(pUVM, idCpu, enmReg, DBGFREGVALTYPE_U64, &Value);
    if (RT_SUCCESS(rc))
        *pu64 = Value.u64;
    else
        *pu64 = 0;
    return rc;
}


/**
 * Queries a descriptor table register value.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 * @retval  VINF_DBGF_ZERO_EXTENDED_REGISTER
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               The target CPU ID.  Can be OR'ed with
 *                              DBGFREG_HYPER_VMCPUID.
 * @param   enmReg              The register that's being queried.
 * @param   pu64Base            Where to store the register base value.
 * @param   pu16Limit           Where to store the register limit value.
 */
VMMR3DECL(int) DBGFR3RegCpuQueryXdtr(PUVM pUVM, VMCPUID idCpu, DBGFREG enmReg, uint64_t *pu64Base, uint16_t *pu16Limit)
{
    DBGFREGVAL Value;
    int rc = dbgfR3RegCpuQueryWorker(pUVM, idCpu, enmReg, DBGFREGVALTYPE_DTR, &Value);
    if (RT_SUCCESS(rc))
    {
        *pu64Base  = Value.dtr.u64Base;
        *pu16Limit = Value.dtr.u32Limit;
    }
    else
    {
        *pu64Base  = 0;
        *pu16Limit = 0;
    }
    return rc;
}


#if 0 /* rewrite / remove */

/**
 * Wrapper around CPUMQueryGuestMsr for dbgfR3RegCpuQueryBatchWorker.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 *
 * @param   pVCpu               The cross context virtual CPU structure of the calling EMT.
 * @param   pReg                The where to store the register value and
 *                              size.
 * @param   idMsr               The MSR to get.
 */
static void dbgfR3RegGetMsrBatch(PVMCPU pVCpu, PDBGFREGENTRY pReg, uint32_t idMsr)
{
    pReg->enmType = DBGFREGVALTYPE_U64;
    int rc = CPUMQueryGuestMsr(pVCpu, idMsr, &pReg->Val.u64);
    if (RT_FAILURE(rc))
    {
        AssertMsg(rc == VERR_CPUM_RAISE_GP_0, ("%Rrc\n", rc));
        pReg->Val.u64 = 0;
    }
}


static DECLCALLBACK(int) dbgfR3RegCpuQueryBatchWorker(PUVM pUVM, VMCPUID idCpu, PDBGFREGENTRY paRegs, size_t cRegs)
{
#if 0
    PVMCPU    pVCpu = &pUVM->pVM->aCpus[idCpu];
    PCCPUMCTX pCtx  = CPUMQueryGuestCtxPtr(pVCpu);

    PDBGFREGENTRY pReg = paRegs - 1;
    while (cRegs-- > 0)
    {
        pReg++;
        pReg->Val.au64[0] = 0;
        pReg->Val.au64[1] = 0;

        DBGFREG const enmReg = pReg->enmReg;
        AssertMsgReturn(enmReg >= 0 && enmReg <= DBGFREG_END, ("%d (%#x)\n", enmReg, enmReg), VERR_DBGF_REGISTER_NOT_FOUND);
        if (enmReg != DBGFREG_END)
        {
            PCDBGFREGDESC pDesc = &g_aDbgfRegDescs[enmReg];
            if (!pDesc->pfnGet)
            {
                PCRTUINT128U pu = (PCRTUINT128U)((uintptr_t)pCtx + pDesc->offCtx);
                pReg->enmType = pDesc->enmType;
                switch (pDesc->enmType)
                {
                    case DBGFREGVALTYPE_U8:     pReg->Val.u8   = pu->au8[0];   break;
                    case DBGFREGVALTYPE_U16:    pReg->Val.u16  = pu->au16[0];  break;
                    case DBGFREGVALTYPE_U32:    pReg->Val.u32  = pu->au32[0];  break;
                    case DBGFREGVALTYPE_U64:    pReg->Val.u64  = pu->au64[0];  break;
                    case DBGFREGVALTYPE_U128:
                        pReg->Val.au64[0] = pu->au64[0];
                        pReg->Val.au64[1] = pu->au64[1];
                        break;
                    case DBGFREGVALTYPE_R80:
                        pReg->Val.au64[0] = pu->au64[0];
                        pReg->Val.au16[5] = pu->au16[5];
                        break;
                    default:
                        AssertMsgFailedReturn(("%s %d\n", pDesc->pszName, pDesc->enmType), VERR_IPE_NOT_REACHED_DEFAULT_CASE);
                }
            }
            else
            {
                int rc = pDesc->pfnGet(pVCpu, pDesc, pCtx, &pReg->Val.u);
                if (RT_FAILURE(rc))
                    return rc;
            }
        }
    }
    return VINF_SUCCESS;
#else
    return VERR_NOT_IMPLEMENTED;
#endif
}


/**
 * Query a batch of registers.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               The target CPU ID.  Can be OR'ed with
 *                              DBGFREG_HYPER_VMCPUID.
 * @param   paRegs              Pointer to an array of @a cRegs elements.  On
 *                              input the enmReg members indicates which
 *                              registers to query.  On successful return the
 *                              other members are set.  DBGFREG_END can be used
 *                              as a filler.
 * @param   cRegs               The number of entries in @a paRegs.
 */
VMMR3DECL(int) DBGFR3RegCpuQueryBatch(PUVM pUVM, VMCPUID idCpu, PDBGFREGENTRY paRegs, size_t cRegs)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, NULL);
    VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, NULL);
    AssertReturn(idCpu < pUVM->cCpus, VERR_INVALID_CPU_ID);
    if (!cRegs)
        return VINF_SUCCESS;
    AssertReturn(cRegs < _1M, VERR_OUT_OF_RANGE);
    AssertPtrReturn(paRegs, VERR_INVALID_POINTER);
    size_t iReg = cRegs;
    while (iReg-- > 0)
    {
        DBGFREG enmReg = paRegs[iReg].enmReg;
        AssertMsgReturn(enmReg < DBGFREG_END && enmReg >= DBGFREG_AL, ("%d (%#x)", enmReg, enmReg), VERR_DBGF_REGISTER_NOT_FOUND);
    }

    return VMR3ReqCallWaitU(pUVM, idCpu, (PFNRT)dbgfR3RegCpuQueryBatchWorker, 4, pUVM, idCpu, paRegs, cRegs);
}


/**
 * Query all registers for a Virtual CPU.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               The target CPU ID.  Can be OR'ed with
 *                              DBGFREG_HYPER_VMCPUID.
 * @param   paRegs              Pointer to an array of @a cRegs elements.
 *                              These will be filled with the CPU register
 *                              values. Overflowing entries will be set to
 *                              DBGFREG_END.  The returned registers can be
 *                              accessed by using the DBGFREG values as index.
 * @param   cRegs               The number of entries in @a paRegs.  The
 *                              recommended value is DBGFREG_ALL_COUNT.
 */
VMMR3DECL(int) DBGFR3RegCpuQueryAll(PUVM pUVM, VMCPUID idCpu, PDBGFREGENTRY paRegs, size_t cRegs)
{
    /*
     * Validate input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, NULL);
    VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, NULL);
    AssertReturn(idCpu < pUVM->cCpus, VERR_INVALID_CPU_ID);
    if (!cRegs)
        return VINF_SUCCESS;
    AssertReturn(cRegs < _1M, VERR_OUT_OF_RANGE);
    AssertPtrReturn(paRegs, VERR_INVALID_POINTER);

    /*
     * Convert it into a batch query (lazy bird).
     */
    unsigned iReg = 0;
    while (iReg < cRegs && iReg < DBGFREG_ALL_COUNT)
    {
        paRegs[iReg].enmReg = (DBGFREG)iReg;
        iReg++;
    }
    while (iReg < cRegs)
        paRegs[iReg++].enmReg = DBGFREG_END;

    return VMR3ReqCallWaitU(pUVM, idCpu, (PFNRT)dbgfR3RegCpuQueryBatchWorker, 4, pUVM, idCpu, paRegs, cRegs);
}

#endif /* rewrite or remove? */

/**
 * Gets the name of a register.
 *
 * @returns Pointer to read-only register name (lower case).  NULL if the
 *          parameters are invalid.
 *
 * @param   pUVM                The user mode VM handle.
 * @param   enmReg              The register identifier.
 * @param   enmType             The register type.  This is for sort out
 *                              aliases.  Pass DBGFREGVALTYPE_INVALID to get
 *                              the standard name.
 */
VMMR3DECL(const char *) DBGFR3RegCpuName(PUVM pUVM, DBGFREG enmReg, DBGFREGVALTYPE enmType)
{
    AssertReturn(enmReg >= DBGFREG_AL && enmReg < DBGFREG_END, NULL);
    AssertReturn(enmType >= DBGFREGVALTYPE_INVALID && enmType < DBGFREGVALTYPE_END, NULL);
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, NULL);
    VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, NULL);

    PCDBGFREGSET    pSet    = pUVM->aCpus[0].dbgf.s.pGuestRegSet;
    if (RT_UNLIKELY(!pSet))
        return NULL;

    PCDBGFREGDESC   pDesc   = &pSet->paDescs[enmReg];
    PCDBGFREGALIAS  pAlias  = pDesc->paAliases;
    if (   pAlias
        && pDesc->enmType != enmType
        && enmType != DBGFREGVALTYPE_INVALID)
    {
        while (pAlias->pszName)
        {
            if (pAlias->enmType == enmType)
                return pAlias->pszName;
            pAlias++;
        }
    }

    return pDesc->pszName;
}


/**
 * Fold the string to lower case and copy it into the destination buffer.
 *
 * @returns Number of folder characters, -1 on overflow.
 * @param   pszSrc              The source string.
 * @param   cchSrc              How much to fold and copy.
 * @param   pszDst              The output buffer.
 * @param   cbDst               The size of the output buffer.
 */
static ssize_t dbgfR3RegCopyToLower(const char *pszSrc, size_t cchSrc, char *pszDst, size_t cbDst)
{
    ssize_t cchFolded = 0;
    char    ch;
    while (cchSrc-- > 0 && (ch = *pszSrc++))
    {
        if (RT_UNLIKELY(cbDst <= 1))
            return -1;
        cbDst--;

        char chLower = RT_C_TO_LOWER(ch);
        cchFolded += chLower != ch;
        *pszDst++ = chLower;
    }
    if (RT_UNLIKELY(!cbDst))
        return -1;
    *pszDst = '\0';
    return cchFolded;
}


/**
 * Resolves the register name.
 *
 * @returns Lookup record.
 * @param   pUVM                The user mode VM handle.
 * @param   idDefCpu            The default CPU ID set.
 * @param   pszReg              The register name.
 * @param   fGuestRegs          Default to guest CPU registers if set, the
 *                              hypervisor CPU registers if clear.
 */
static PCDBGFREGLOOKUP dbgfR3RegResolve(PUVM pUVM, VMCPUID idDefCpu, const char *pszReg, bool fGuestRegs)
{
    DBGF_REG_DB_LOCK_READ(pUVM);

    /* Try looking up the name without any case folding or cpu prefixing. */
    PRTSTRSPACE pRegSpace = &pUVM->dbgf.s.RegSpace;
    PCDBGFREGLOOKUP pLookupRec = (PCDBGFREGLOOKUP)RTStrSpaceGet(pRegSpace, pszReg);
    if (!pLookupRec)
    {
        char szName[DBGF_REG_MAX_NAME * 4 + 16];

        /* Lower case it and try again. */
        ssize_t cchFolded = dbgfR3RegCopyToLower(pszReg, RTSTR_MAX, szName, sizeof(szName) - DBGF_REG_MAX_NAME);
        if (cchFolded > 0)
            pLookupRec = (PCDBGFREGLOOKUP)RTStrSpaceGet(pRegSpace, szName);
        if (   !pLookupRec
            && cchFolded >= 0
            && idDefCpu != VMCPUID_ANY)
        {
            /* Prefix it with the specified CPU set. */
            size_t cchCpuSet = RTStrPrintf(szName, sizeof(szName), fGuestRegs ? "cpu%u." : "hypercpu%u.", idDefCpu);
            dbgfR3RegCopyToLower(pszReg, RTSTR_MAX, &szName[cchCpuSet], sizeof(szName) - cchCpuSet);
            pLookupRec = (PCDBGFREGLOOKUP)RTStrSpaceGet(pRegSpace, szName);
        }
    }

    DBGF_REG_DB_UNLOCK_READ(pUVM);
    return pLookupRec;
}


/**
 * Validates the register name.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS if the register was found.
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND if not found.
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idDefCpu            The default CPU.
 * @param   pszReg              The registe name.
 */
VMMR3DECL(int) DBGFR3RegNmValidate(PUVM pUVM, VMCPUID idDefCpu, const char *pszReg)
{
    /*
     * Validate input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn((idDefCpu & ~DBGFREG_HYPER_VMCPUID) < pUVM->cCpus || idDefCpu == VMCPUID_ANY, VERR_INVALID_CPU_ID);
    AssertPtrReturn(pszReg, VERR_INVALID_POINTER);

    /*
     * Resolve the register.
     */
    bool fGuestRegs = true;
    if ((idDefCpu & DBGFREG_HYPER_VMCPUID) && idDefCpu != VMCPUID_ANY)
    {
        fGuestRegs = false;
        idDefCpu &= ~DBGFREG_HYPER_VMCPUID;
    }

    PCDBGFREGLOOKUP pLookupRec = dbgfR3RegResolve(pUVM, idDefCpu, pszReg, fGuestRegs);
    if (!pLookupRec)
        return VERR_DBGF_REGISTER_NOT_FOUND;
    return VINF_SUCCESS;
}


/**
 * On CPU worker for the register queries, used by dbgfR3RegNmQueryWorker and
 * dbgfR3RegPrintfCbFormatNormal.
 *
 * @returns VBox status code.
 *
 * @param   pUVM                The user mode VM handle.
 * @param   pLookupRec          The register lookup record.
 * @param   enmType             The desired return type.
 * @param   pValue              Where to return the register value.
 * @param   penmType            Where to store the register value type.
 *                              Optional.
 */
static DECLCALLBACK(int) dbgfR3RegNmQueryWorkerOnCpu(PUVM pUVM, PCDBGFREGLOOKUP pLookupRec, DBGFREGVALTYPE enmType,
                                                     PDBGFREGVAL pValue, PDBGFREGVALTYPE penmType)
{
    PCDBGFREGDESC       pDesc        = pLookupRec->pDesc;
    PCDBGFREGSET        pSet         = pLookupRec->pSet;
    PCDBGFREGSUBFIELD   pSubField    = pLookupRec->pSubField;
    DBGFREGVALTYPE      enmValueType = pDesc->enmType;
    int                 rc;

    NOREF(pUVM);

    /*
     * Get the register or sub-field value.
     */
    dbgfR3RegValClear(pValue);
    if (!pSubField)
    {
        rc = pDesc->pfnGet(pSet->uUserArg.pv, pDesc, pValue);
        if (   pLookupRec->pAlias
            && pLookupRec->pAlias->enmType != enmValueType
            && RT_SUCCESS(rc))
        {
            rc = dbgfR3RegValCast(pValue, enmValueType, pLookupRec->pAlias->enmType);
            enmValueType = pLookupRec->pAlias->enmType;
        }
    }
    else
    {
        if (pSubField->pfnGet)
        {
            rc = pSubField->pfnGet(pSet->uUserArg.pv, pSubField, &pValue->u128);
            enmValueType = DBGFREGVALTYPE_U128;
        }
        else
        {
            rc = pDesc->pfnGet(pSet->uUserArg.pv, pDesc, pValue);
            if (   pLookupRec->pAlias
                && pLookupRec->pAlias->enmType != enmValueType
                && RT_SUCCESS(rc))
            {
                rc = dbgfR3RegValCast(pValue, enmValueType, pLookupRec->pAlias->enmType);
                enmValueType = pLookupRec->pAlias->enmType;
            }
            if (RT_SUCCESS(rc))
            {
                rc = dbgfR3RegValCast(pValue, enmValueType, DBGFREGVALTYPE_U128);
                if (RT_SUCCESS(rc))
                {
                    RTUInt128AssignShiftLeft(&pValue->u128, -pSubField->iFirstBit);
                    RTUInt128AssignAndNFirstBits(&pValue->u128, pSubField->cBits);
                    if (pSubField->cShift)
                        RTUInt128AssignShiftLeft(&pValue->u128, pSubField->cShift);
                }
            }
        }
        if (RT_SUCCESS(rc))
        {
            unsigned const cBits = pSubField->cBits + pSubField->cShift;
            if (cBits <= 8)
                enmValueType = DBGFREGVALTYPE_U8;
            else if (cBits <= 16)
                enmValueType = DBGFREGVALTYPE_U16;
            else if (cBits <= 32)
                enmValueType = DBGFREGVALTYPE_U32;
            else if (cBits <= 64)
                enmValueType = DBGFREGVALTYPE_U64;
            else
                enmValueType = DBGFREGVALTYPE_U128;
            rc = dbgfR3RegValCast(pValue, DBGFREGVALTYPE_U128, enmValueType);
        }
    }
    if (RT_SUCCESS(rc))
    {
        /*
         * Do the cast if the desired return type doesn't match what
         * the getter returned.
         */
        if (   enmValueType == enmType
            || enmType == DBGFREGVALTYPE_END)
        {
            rc = VINF_SUCCESS;
            if (penmType)
                *penmType = enmValueType;
        }
        else
        {
            rc = dbgfR3RegValCast(pValue, enmValueType, enmType);
            if (penmType)
                *penmType = RT_SUCCESS(rc) ? enmType : enmValueType;
        }
    }

    return rc;
}


/**
 * Worker for the register queries.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 * @retval  VINF_DBGF_ZERO_EXTENDED_REGISTER
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idDefCpu            The virtual CPU ID for the default CPU register
 *                              set.  Can be OR'ed with DBGFREG_HYPER_VMCPUID.
 * @param   pszReg              The register to query.
 * @param   enmType             The desired return type.
 * @param   pValue              Where to return the register value.
 * @param   penmType            Where to store the register value type.
 *                              Optional.
 */
static int dbgfR3RegNmQueryWorker(PUVM pUVM, VMCPUID idDefCpu, const char *pszReg, DBGFREGVALTYPE enmType,
                                  PDBGFREGVAL pValue, PDBGFREGVALTYPE penmType)
{
    /*
     * Validate input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn((idDefCpu & ~DBGFREG_HYPER_VMCPUID) < pUVM->cCpus || idDefCpu == VMCPUID_ANY, VERR_INVALID_CPU_ID);
    AssertPtrReturn(pszReg, VERR_INVALID_POINTER);

    Assert(enmType > DBGFREGVALTYPE_INVALID && enmType <= DBGFREGVALTYPE_END);
    AssertPtr(pValue);

    /*
     * Resolve the register and call the getter on the relevant CPU.
     */
    bool fGuestRegs = true;
    if ((idDefCpu & DBGFREG_HYPER_VMCPUID) && idDefCpu != VMCPUID_ANY)
    {
        fGuestRegs = false;
        idDefCpu &= ~DBGFREG_HYPER_VMCPUID;
    }
    PCDBGFREGLOOKUP pLookupRec = dbgfR3RegResolve(pUVM, idDefCpu, pszReg, fGuestRegs);
    if (pLookupRec)
    {
        if (pLookupRec->pSet->enmType == DBGFREGSETTYPE_CPU)
            idDefCpu = pLookupRec->pSet->uUserArg.pVCpu->idCpu;
        else if (idDefCpu != VMCPUID_ANY)
            idDefCpu &= ~DBGFREG_HYPER_VMCPUID;
        return VMR3ReqPriorityCallWaitU(pUVM, idDefCpu, (PFNRT)dbgfR3RegNmQueryWorkerOnCpu, 5,
                                        pUVM, pLookupRec, enmType, pValue, penmType);
    }
    return VERR_DBGF_REGISTER_NOT_FOUND;
}


/**
 * Queries a descriptor table register value.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idDefCpu            The default target CPU ID, VMCPUID_ANY if not
 *                              applicable. Can be OR'ed with
 *                              DBGFREG_HYPER_VMCPUID.
 * @param   pszReg              The register that's being queried.  Except for
 *                              CPU registers, this must be on the form
 *                              "set.reg[.sub]".
 * @param   pValue              Where to store the register value.
 * @param   penmType            Where to store the register value type.
 */
VMMR3DECL(int) DBGFR3RegNmQuery(PUVM pUVM, VMCPUID idDefCpu, const char *pszReg, PDBGFREGVAL pValue, PDBGFREGVALTYPE penmType)
{
    return dbgfR3RegNmQueryWorker(pUVM, idDefCpu, pszReg, DBGFREGVALTYPE_END, pValue, penmType);
}


/**
 * Queries a 8-bit register value.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idDefCpu            The default target CPU ID, VMCPUID_ANY if not
 *                              applicable. Can be OR'ed with
 *                              DBGFREG_HYPER_VMCPUID.
 * @param   pszReg              The register that's being queried.  Except for
 *                              CPU registers, this must be on the form
 *                              "set.reg[.sub]".
 * @param   pu8                 Where to store the register value.
 */
VMMR3DECL(int) DBGFR3RegNmQueryU8(PUVM pUVM, VMCPUID idDefCpu, const char *pszReg, uint8_t *pu8)
{
    DBGFREGVAL Value;
    int rc = dbgfR3RegNmQueryWorker(pUVM, idDefCpu, pszReg, DBGFREGVALTYPE_U8, &Value, NULL);
    if (RT_SUCCESS(rc))
        *pu8 = Value.u8;
    else
        *pu8 = 0;
    return rc;
}


/**
 * Queries a 16-bit register value.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 * @retval  VINF_DBGF_ZERO_EXTENDED_REGISTER
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idDefCpu            The default target CPU ID, VMCPUID_ANY if not
 *                              applicable.  Can be OR'ed with
 *                              DBGFREG_HYPER_VMCPUID.
 * @param   pszReg              The register that's being queried.  Except for
 *                              CPU registers, this must be on the form
 *                              "set.reg[.sub]".
 * @param   pu16                Where to store the register value.
 */
VMMR3DECL(int) DBGFR3RegNmQueryU16(PUVM pUVM, VMCPUID idDefCpu, const char *pszReg, uint16_t *pu16)
{
    DBGFREGVAL Value;
    int rc = dbgfR3RegNmQueryWorker(pUVM, idDefCpu, pszReg, DBGFREGVALTYPE_U16, &Value, NULL);
    if (RT_SUCCESS(rc))
        *pu16 = Value.u16;
    else
        *pu16 = 0;
    return rc;
}


/**
 * Queries a 32-bit register value.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 * @retval  VINF_DBGF_ZERO_EXTENDED_REGISTER
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idDefCpu            The default target CPU ID, VMCPUID_ANY if not
 *                              applicable.  Can be OR'ed with
 *                              DBGFREG_HYPER_VMCPUID.
 * @param   pszReg              The register that's being queried.  Except for
 *                              CPU registers, this must be on the form
 *                              "set.reg[.sub]".
 * @param   pu32                Where to store the register value.
 */
VMMR3DECL(int) DBGFR3RegNmQueryU32(PUVM pUVM, VMCPUID idDefCpu, const char *pszReg, uint32_t *pu32)
{
    DBGFREGVAL Value;
    int rc = dbgfR3RegNmQueryWorker(pUVM, idDefCpu, pszReg, DBGFREGVALTYPE_U32, &Value, NULL);
    if (RT_SUCCESS(rc))
        *pu32 = Value.u32;
    else
        *pu32 = 0;
    return rc;
}


/**
 * Queries a 64-bit register value.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 * @retval  VINF_DBGF_ZERO_EXTENDED_REGISTER
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idDefCpu            The default target CPU ID, VMCPUID_ANY if not
 *                              applicable.  Can be OR'ed with
 *                              DBGFREG_HYPER_VMCPUID.
 * @param   pszReg              The register that's being queried.  Except for
 *                              CPU registers, this must be on the form
 *                              "set.reg[.sub]".
 * @param   pu64                Where to store the register value.
 */
VMMR3DECL(int) DBGFR3RegNmQueryU64(PUVM pUVM, VMCPUID idDefCpu, const char *pszReg, uint64_t *pu64)
{
    DBGFREGVAL Value;
    int rc = dbgfR3RegNmQueryWorker(pUVM, idDefCpu, pszReg, DBGFREGVALTYPE_U64, &Value, NULL);
    if (RT_SUCCESS(rc))
        *pu64 = Value.u64;
    else
        *pu64 = 0;
    return rc;
}


/**
 * Queries a 128-bit register value.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 * @retval  VINF_DBGF_ZERO_EXTENDED_REGISTER
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idDefCpu            The default target CPU ID, VMCPUID_ANY if not
 *                              applicable.  Can be OR'ed with
 *                              DBGFREG_HYPER_VMCPUID.
 * @param   pszReg              The register that's being queried.  Except for
 *                              CPU registers, this must be on the form
 *                              "set.reg[.sub]".
 * @param   pu128               Where to store the register value.
 */
VMMR3DECL(int) DBGFR3RegNmQueryU128(PUVM pUVM, VMCPUID idDefCpu, const char *pszReg, PRTUINT128U pu128)
{
    DBGFREGVAL Value;
    int rc = dbgfR3RegNmQueryWorker(pUVM, idDefCpu, pszReg, DBGFREGVALTYPE_U128, &Value, NULL);
    if (RT_SUCCESS(rc))
        *pu128 = Value.u128;
    else
        pu128->s.Hi = pu128->s.Lo = 0;
    return rc;
}


#if 0
/**
 * Queries a long double register value.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 * @retval  VINF_DBGF_ZERO_EXTENDED_REGISTER
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idDefCpu            The default target CPU ID, VMCPUID_ANY if not
 *                              applicable.  Can be OR'ed with
 *                              DBGFREG_HYPER_VMCPUID.
 * @param   pszReg              The register that's being queried.  Except for
 *                              CPU registers, this must be on the form
 *                              "set.reg[.sub]".
 * @param   plrd                Where to store the register value.
 */
VMMR3DECL(int) DBGFR3RegNmQueryLrd(PUVM pUVM, VMCPUID idDefCpu, const char *pszReg, long double *plrd)
{
    DBGFREGVAL Value;
    int rc = dbgfR3RegNmQueryWorker(pUVM, idDefCpu, pszReg, DBGFREGVALTYPE_R80, &Value, NULL);
    if (RT_SUCCESS(rc))
        *plrd = Value.lrd;
    else
        *plrd = 0;
    return rc;
}
#endif


/**
 * Queries a descriptor table register value.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 * @retval  VINF_DBGF_ZERO_EXTENDED_REGISTER
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idDefCpu            The default target CPU ID, VMCPUID_ANY if not
 *                              applicable.  Can be OR'ed with
 *                              DBGFREG_HYPER_VMCPUID.
 * @param   pszReg              The register that's being queried.  Except for
 *                              CPU registers, this must be on the form
 *                              "set.reg[.sub]".
 * @param   pu64Base            Where to store the register base value.
 * @param   pu16Limit           Where to store the register limit value.
 */
VMMR3DECL(int) DBGFR3RegNmQueryXdtr(PUVM pUVM, VMCPUID idDefCpu, const char *pszReg, uint64_t *pu64Base, uint16_t *pu16Limit)
{
    DBGFREGVAL Value;
    int rc = dbgfR3RegNmQueryWorker(pUVM, idDefCpu, pszReg, DBGFREGVALTYPE_DTR, &Value, NULL);
    if (RT_SUCCESS(rc))
    {
        *pu64Base  = Value.dtr.u64Base;
        *pu16Limit = Value.dtr.u32Limit;
    }
    else
    {
        *pu64Base  = 0;
        *pu16Limit = 0;
    }
    return rc;
}


/**
 * Gets the number of bits in value of type @a enmValType.
 */
static unsigned dbgfR3RegGetBitsForValType(DBGFREGVALTYPE enmValType)
{
    switch (enmValType)
    {
        case DBGFREGVALTYPE_U8:     return 8;
        case DBGFREGVALTYPE_U16:    return 16;
        case DBGFREGVALTYPE_U32:    return 32;
        case DBGFREGVALTYPE_U64:    return 64;
        case DBGFREGVALTYPE_U128:   return 128;
        case DBGFREGVALTYPE_U256:   return 256;
        case DBGFREGVALTYPE_U512:   return 512;
        case DBGFREGVALTYPE_R80:    return 80;
        case DBGFREGVALTYPE_DTR:    return 80;
        /* no default, want gcc warnings */
        case DBGFREGVALTYPE_32BIT_HACK:
        case DBGFREGVALTYPE_END:
        case DBGFREGVALTYPE_INVALID:
            break;
    }
    return 512;
}


/**
 * On CPU worker for the extended register queries, used by DBGFR3RegNmQueryEx.
 *
 * @returns VBox status code.
 *
 * @param   pUVM                The user mode VM handle.
 * @param   pLookupRec          The register lookup record.
 * @param   fFlags              DBGFR3REG_QUERY_EX_F_XXX
 * @param   paRegs              Where to return the register values.
 * @param   cRegs               The number of register values to return.
 *                              The caller has checked that this is sufficient
 *                              to store the entire result.
 */
static DECLCALLBACK(int) dbgfR3RegNmQueryExWorkerOnCpu(PUVM pUVM, PCDBGFREGLOOKUP pLookupRec, uint32_t fFlags,
                                                       PDBGFREGENTRYNM paRegs, size_t cRegs)
{
    PCDBGFREGDESC       pDesc = pLookupRec->pDesc;
    PCDBGFREGSET        pSet  = pLookupRec->pSet;
    Assert(!pLookupRec->pSubField);
    NOREF(pUVM);

    /*
     * The register first.
     */
    AssertReturn(cRegs > 0, VERR_BUFFER_OVERFLOW);
    dbgfR3RegValClear(&paRegs[0].Val);
    paRegs[0].pszName   = pLookupRec->Core.pszString;
    paRegs[0].enmType   = pDesc->enmType;
    paRegs[0].u.uInfo   = 0;
    paRegs[0].u.s.fMain = true;
    int rc = pDesc->pfnGet(pSet->uUserArg.pv, pDesc, &paRegs[0].Val);
    AssertMsg(rc == VINF_SUCCESS || rc == VERR_CPUM_RAISE_GP_0, ("rc=%Rrc reg=%s\n", rc, paRegs[0].pszName));
    if (RT_FAILURE(rc))
        return rc;
    DBGFREGVAL const MainValue = paRegs[0].Val;
    uint32_t iReg = 1;

    /* If it's a alias we looked up we may have to do some casting and
       restricting the number of bits included in the sub-fields. */
    unsigned cMaxBits = sizeof(paRegs[0].Val) * 8;
    if (pLookupRec->pAlias)
    {
        paRegs[0].enmType    = pLookupRec->pAlias->enmType;
        paRegs[0].u.uInfo    = 0;
        paRegs[0].u.s.fAlias = true;
        if (paRegs[0].enmType != pDesc->enmType)
        {
            dbgfR3RegValCast(&paRegs[0].Val, pDesc->enmType, paRegs[0].enmType);
            cMaxBits = dbgfR3RegGetBitsForValType(paRegs[0].enmType);
        }

        /* Add the main value as the 2nd entry. */
        paRegs[iReg].pszName  = pDesc->pszName;
        paRegs[iReg].enmType  = pDesc->enmType;
        paRegs[iReg].Val      = MainValue;
        paRegs[iReg].u.uInfo   = 0;
        paRegs[iReg].u.s.fMain = true;
        iReg++;
    }

    /*
     * (Other) Aliases.
     */
    if (   (fFlags & DBGFR3REG_QUERY_EX_F_ALIASES)
        && pDesc->paAliases)
    {
        PCDBGFREGALIAS const paAliases = pDesc->paAliases;
        for (uint32_t i = 0; paAliases[i].pszName != NULL; i++)
            if (&paAliases[i] != pLookupRec->pAlias )
            {
                AssertReturn(iReg < cRegs, VERR_BUFFER_OVERFLOW);
                paRegs[iReg].pszName    = paAliases[i].pszName;
                paRegs[iReg].enmType    = paAliases[i].enmType;
                paRegs[iReg].u.uInfo    = 0;
                paRegs[iReg].u.s.fAlias = true;
                paRegs[iReg].Val        = MainValue;
                dbgfR3RegValCast(&paRegs[iReg].Val, pDesc->enmType, paAliases[i].enmType);
                iReg++;
            }
    }

    /*
     * Subfields.
     */
    if (   (fFlags & DBGFR3REG_QUERY_EX_F_SUBFIELDS)
        && pDesc->paSubFields)
    {
        PCDBGFREGSUBFIELD const paSubFields = pDesc->paSubFields;
        for (uint32_t i = 0; paSubFields[i].pszName != NULL; i++)
            if (paSubFields[i].iFirstBit < cMaxBits || paSubFields[i].pfnGet)
            {
                AssertReturn(iReg < cRegs, VERR_BUFFER_OVERFLOW);
                int rc2;
                paRegs[iReg].pszName       = paSubFields[i].pszName;
                paRegs[iReg].u.uInfo       = 0;
                paRegs[iReg].u.s.fSubField = true;
                paRegs[iReg].u.s.cBits     = paSubFields[i].cBits + paSubFields[i].cShift;
                if (paSubFields[i].pfnGet)
                {
                    dbgfR3RegValClear(&paRegs[iReg].Val);
                    rc2 = paSubFields[i].pfnGet(pSet->uUserArg.pv, &paSubFields[i], &paRegs[iReg].Val.u128);
                }
                else
                {
                    paRegs[iReg].Val = MainValue;
                    rc2 = dbgfR3RegValCast(&paRegs[iReg].Val, pDesc->enmType, DBGFREGVALTYPE_U128);
                    if (RT_SUCCESS(rc2))
                    {
                        RTUInt128AssignShiftLeft(&paRegs[iReg].Val.u128, -paSubFields[i].iFirstBit);
                        RTUInt128AssignAndNFirstBits(&paRegs[iReg].Val.u128, paSubFields[i].cBits);
                        if (paSubFields[i].cShift)
                            RTUInt128AssignShiftLeft(&paRegs[iReg].Val.u128, paSubFields[i].cShift);
                    }
                }
                if (RT_SUCCESS(rc2))
                {
                    unsigned const cBits = paSubFields[i].cBits + paSubFields[i].cShift;
                    if (cBits <= 8)
                        paRegs[iReg].enmType = DBGFREGVALTYPE_U8;
                    else if (cBits <= 16)
                        paRegs[iReg].enmType = DBGFREGVALTYPE_U16;
                    else if (cBits <= 32)
                        paRegs[iReg].enmType = DBGFREGVALTYPE_U32;
                    else if (cBits <= 64)
                        paRegs[iReg].enmType = DBGFREGVALTYPE_U64;
                    else
                        paRegs[iReg].enmType = DBGFREGVALTYPE_U128;
                    rc2 = dbgfR3RegValCast(&paRegs[iReg].Val, DBGFREGVALTYPE_U128, paRegs[iReg].enmType);
                }
                if (RT_SUCCESS(rc2))
                    iReg++;
                else
                    rc = rc2;
            }
    }
    return rc;
}


/**
 * Queries a register with aliases and/or sub-fields.
 *
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_BUFFER_OVERFLOW w/ *pcRegs set to the required size.
 *          No other data returned.
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 * @retval  VINF_DBGF_ZERO_EXTENDED_REGISTER
 *
 * @param   pUVM        The user mode VM handle.
 * @param   idDefCpu    The default target CPU ID, VMCPUID_ANY if not
 *                      applicable.  Can be OR'ed with DBGFREG_HYPER_VMCPUID.
 * @param   pszReg      The register that's being queried.  Except for CPU
 *                      registers, this must be on the form "set.reg[.sub]".
 * @param   fFlags      DBGFR3REG_QUERY_EX_F_XXX
 * @param   paRegs
 * @param   pcRegs      On input this is the size of the paRegs buffer.
 *                      On successful return this is set to the number of
 *                      registers returned.  This is set to the required number
 *                      of register entries when VERR_BUFFER_OVERFLOW is
 *                      returned.
 */
VMMR3DECL(int) DBGFR3RegNmQueryEx(PUVM pUVM, VMCPUID idDefCpu, const char *pszReg, uint32_t fFlags,
                                  PDBGFREGENTRYNM paRegs, size_t *pcRegs)
{
    /*
     * Validate input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn((idDefCpu & ~DBGFREG_HYPER_VMCPUID) < pUVM->cCpus || idDefCpu == VMCPUID_ANY, VERR_INVALID_CPU_ID);
    AssertPtrReturn(pszReg, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~DBGFR3REG_QUERY_EX_F_VALID_MASK), VERR_INVALID_FLAGS);
    AssertPtrReturn(pcRegs, VERR_INVALID_POINTER);
    AssertPtrNullReturn(paRegs, VERR_INVALID_POINTER);

    /*
     * Resolve the register and call the getter on the relevant CPU.
     */
    bool fGuestRegs = true;
    if ((idDefCpu & DBGFREG_HYPER_VMCPUID) && idDefCpu != VMCPUID_ANY)
    {
        fGuestRegs = false;
        idDefCpu &= ~DBGFREG_HYPER_VMCPUID;
    }
    PCDBGFREGLOOKUP pLookupRec = dbgfR3RegResolve(pUVM, idDefCpu, pszReg, fGuestRegs);
    if (pLookupRec)
    {
        /*
         * Determine how many register values we'd be returning.
         */
        size_t cRegs = 1; /* we always return the direct hit. */

        if (   (fFlags & DBGFR3REG_QUERY_EX_F_ALIASES)
            && !pLookupRec->pSubField
            && pLookupRec->pDesc->paAliases)
        {
            PCDBGFREGALIAS const paAliases = pLookupRec->pDesc->paAliases;
            for (uint32_t i = 0; paAliases[i].pszName != NULL; i++)
                cRegs++;
        }
        else if (pLookupRec->pAlias)
            cRegs++;

        if (   (fFlags & DBGFR3REG_QUERY_EX_F_SUBFIELDS)
            && !pLookupRec->pSubField
            && pLookupRec->pDesc->paSubFields)
        {
            unsigned const cMaxBits = !pLookupRec->pAlias ? sizeof(paRegs[0].Val) * 8
                                    : dbgfR3RegGetBitsForValType(pLookupRec->pAlias->enmType);
            PCDBGFREGSUBFIELD const paSubFields = pLookupRec->pDesc->paSubFields;
            for (uint32_t i = 0; paSubFields[i].pszName != NULL; i++)
                if (paSubFields[i].iFirstBit < cMaxBits || paSubFields[i].pfnGet)
                    cRegs++;
        }

        /*
         * Did the caller provide sufficient room for the register values, then
         * retrieve the register on the specified CPU.
         */
        if (paRegs && *pcRegs >= cRegs)
        {
            *pcRegs = cRegs;

            if (pLookupRec->pSet->enmType == DBGFREGSETTYPE_CPU)
                idDefCpu = pLookupRec->pSet->uUserArg.pVCpu->idCpu;
            else if (idDefCpu != VMCPUID_ANY)
                idDefCpu &= ~DBGFREG_HYPER_VMCPUID;

            /* If we hit a sub-field we'll just use the regular worker to get it. */
            if (!pLookupRec->pSubField)
                return VMR3ReqPriorityCallWaitU(pUVM, idDefCpu, (PFNRT)dbgfR3RegNmQueryExWorkerOnCpu, 5,
                                                pUVM, pLookupRec, fFlags, paRegs, cRegs);
            Assert(cRegs == 1);
            paRegs[0].pszName       = pLookupRec->Core.pszString;
            paRegs[0].enmType       = DBGFREGVALTYPE_END;
            paRegs[0].u.uInfo       = 0;
            paRegs[0].u.s.cBits     = pLookupRec->pSubField->cBits + pLookupRec->pSubField->cShift;
            paRegs[0].u.s.fSubField = true;
            dbgfR3RegValClear(&paRegs[0].Val);
            return VMR3ReqPriorityCallWaitU(pUVM, idDefCpu, (PFNRT)dbgfR3RegNmQueryWorkerOnCpu, 5,
                                            pUVM, pLookupRec, DBGFREGVALTYPE_END, &paRegs[0].Val, &paRegs[0].enmType);
        }
        *pcRegs = cRegs;
        return VERR_BUFFER_OVERFLOW;
    }
    return VERR_DBGF_REGISTER_NOT_FOUND;

}


/// @todo VMMR3DECL(int) DBGFR3RegNmQueryBatch(PUVM pUVM,VMCPUID idDefCpu, DBGFREGENTRYNM paRegs, size_t cRegs);


/**
 * Gets the number of registers returned by DBGFR3RegNmQueryAll.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pcRegs              Where to return the register count.
 */
VMMR3DECL(int) DBGFR3RegNmQueryAllCount(PUVM pUVM, size_t *pcRegs)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    *pcRegs = pUVM->dbgf.s.cRegs;
    return VINF_SUCCESS;
}


/**
 * Pad register entries.
 *
 * @param   paRegs          The output array.
 * @param   cRegs           The size of the output array.
 * @param   iReg            The first register to pad.
 * @param   cRegsToPad      The number of registers to pad.
 */
static void dbgfR3RegNmQueryAllPadEntries(PDBGFREGENTRYNM paRegs, size_t cRegs, size_t iReg, size_t cRegsToPad)
{
    if (iReg < cRegs)
    {
        size_t iEndReg = iReg + cRegsToPad;
        if (iEndReg > cRegs)
            iEndReg = cRegs;
        while (iReg < iEndReg)
        {
            paRegs[iReg].pszName = NULL;
            paRegs[iReg].enmType = DBGFREGVALTYPE_END;
            paRegs[iReg].u.uInfo = 0;
            dbgfR3RegValClear(&paRegs[iReg].Val);
            iReg++;
        }
    }
}


/**
 * Query all registers in a set.
 *
 * @param   pSet            The set.
 * @param   cRegsToQuery    The number of registers to query.
 * @param   paRegs          The output array.
 * @param   cRegs           The size of the output array.
 */
static void dbgfR3RegNmQueryAllInSet(PCDBGFREGSET pSet, size_t cRegsToQuery, PDBGFREGENTRYNM paRegs, size_t cRegs)
{
    if (cRegsToQuery > pSet->cDescs)
        cRegsToQuery = pSet->cDescs;
    if (cRegsToQuery > cRegs)
        cRegsToQuery = cRegs;

    for (size_t iReg = 0; iReg < cRegsToQuery; iReg++)
    {
        paRegs[iReg].enmType   = pSet->paDescs[iReg].enmType;
        paRegs[iReg].pszName   = pSet->paLookupRecs[iReg].Core.pszString;
        paRegs[iReg].u.uInfo   = 0;
        paRegs[iReg].u.s.fMain = true;
        dbgfR3RegValClear(&paRegs[iReg].Val);
        int rc2 = pSet->paDescs[iReg].pfnGet(pSet->uUserArg.pv, &pSet->paDescs[iReg], &paRegs[iReg].Val);
        AssertMsg(rc2 == VINF_SUCCESS || rc2 == VERR_CPUM_RAISE_GP_0, ("rc2=%Rrc iReg=%u %s\n", rc2, iReg, paRegs[iReg].pszName));
        if (RT_FAILURE(rc2))
            dbgfR3RegValClear(&paRegs[iReg].Val);
    }
}


/**
 * @callback_method_impl{FNRTSTRSPACECALLBACK, Worker used by
 *                      dbgfR3RegNmQueryAllWorker}
 */
static DECLCALLBACK(int)  dbgfR3RegNmQueryAllEnum(PRTSTRSPACECORE pStr, void *pvUser)
{
    PCDBGFREGSET pSet = (PCDBGFREGSET)pStr;
    if (pSet->enmType != DBGFREGSETTYPE_CPU)
    {
        PDBGFR3REGNMQUERYALLARGS pArgs  = (PDBGFR3REGNMQUERYALLARGS)pvUser;
        if (pArgs->iReg < pArgs->cRegs)
            dbgfR3RegNmQueryAllInSet(pSet, pSet->cDescs, &pArgs->paRegs[pArgs->iReg], pArgs->cRegs - pArgs->iReg);
        pArgs->iReg += pSet->cDescs;
    }

    return 0;
}


/**
 * @callback_method_impl{FNVMMEMTRENDEZVOUS, Worker used by DBGFR3RegNmQueryAll}
 */
static DECLCALLBACK(VBOXSTRICTRC) dbgfR3RegNmQueryAllWorker(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    PDBGFR3REGNMQUERYALLARGS    pArgs  = (PDBGFR3REGNMQUERYALLARGS)pvUser;
    PDBGFREGENTRYNM             paRegs = pArgs->paRegs;
    size_t const                cRegs  = pArgs->cRegs;
    PUVM                        pUVM   = pVM->pUVM;
    PUVMCPU                     pUVCpu = pVCpu->pUVCpu;

    DBGF_REG_DB_LOCK_READ(pUVM);

    /*
     * My guest CPU registers.
     */
    size_t iCpuReg = pVCpu->idCpu * pUVM->dbgf.s.cPerCpuRegs;
    if (pUVCpu->dbgf.s.pGuestRegSet)
    {
        if (iCpuReg < cRegs)
            dbgfR3RegNmQueryAllInSet(pUVCpu->dbgf.s.pGuestRegSet, pUVM->dbgf.s.cPerCpuRegs, &paRegs[iCpuReg], cRegs - iCpuReg);
    }
    else
        dbgfR3RegNmQueryAllPadEntries(paRegs, cRegs, iCpuReg, pUVM->dbgf.s.cPerCpuRegs);

    /*
     * My hypervisor CPU registers.
     */
    iCpuReg = pUVM->cCpus * pUVM->dbgf.s.cPerCpuRegs + pUVCpu->idCpu * pUVM->dbgf.s.cPerCpuHyperRegs;
    if (pUVCpu->dbgf.s.pHyperRegSet)
    {
        if (iCpuReg < cRegs)
            dbgfR3RegNmQueryAllInSet(pUVCpu->dbgf.s.pHyperRegSet, pUVM->dbgf.s.cPerCpuHyperRegs, &paRegs[iCpuReg], cRegs - iCpuReg);
    }
    else
        dbgfR3RegNmQueryAllPadEntries(paRegs, cRegs, iCpuReg, pUVM->dbgf.s.cPerCpuHyperRegs);

    /*
     * The primary CPU does all the other registers.
     */
    if (pUVCpu->idCpu == 0)
    {
        pArgs->iReg = pUVM->cCpus * (pUVM->dbgf.s.cPerCpuRegs + pUVM->dbgf.s.cPerCpuHyperRegs);
        RTStrSpaceEnumerate(&pUVM->dbgf.s.RegSetSpace, dbgfR3RegNmQueryAllEnum, pArgs);
        dbgfR3RegNmQueryAllPadEntries(paRegs, cRegs, pArgs->iReg, cRegs);
    }

    DBGF_REG_DB_UNLOCK_READ(pUVM);
    return VINF_SUCCESS; /* Ignore errors. */
}


/**
 * Queries all register.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   paRegs              The output register value array.  The register
 *                              name string is read only and shall not be freed
 *                              or modified.
 * @param   cRegs               The number of entries in @a paRegs.  The
 *                              correct size can be obtained by calling
 *                              DBGFR3RegNmQueryAllCount.
 */
VMMR3DECL(int) DBGFR3RegNmQueryAll(PUVM pUVM, PDBGFREGENTRYNM paRegs, size_t cRegs)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = pUVM->pVM;
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(paRegs, VERR_INVALID_POINTER);
    AssertReturn(cRegs > 0, VERR_OUT_OF_RANGE);

    DBGFR3REGNMQUERYALLARGS Args;
    Args.paRegs = paRegs;
    Args.cRegs  = cRegs;

    return VMMR3EmtRendezvous(pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ALL_AT_ONCE, dbgfR3RegNmQueryAllWorker, &Args);
}


/**
 * On CPU worker for the register modifications, used by DBGFR3RegNmSet.
 *
 * @returns VBox status code.
 *
 * @param   pUVM                The user mode VM handle.
 * @param   pLookupRec          The register lookup record. Maybe be modified,
 *                              so please pass a copy of the user's one.
 * @param   pValue              The new register value.
 * @param   pMask               Indicate which bits to modify.
 */
static DECLCALLBACK(int) dbgfR3RegNmSetWorkerOnCpu(PUVM pUVM, PDBGFREGLOOKUP pLookupRec,
                                                   PCDBGFREGVAL pValue, PCDBGFREGVAL pMask)
{
    RT_NOREF_PV(pUVM);
    PCDBGFREGSUBFIELD pSubField = pLookupRec->pSubField;
    if (pSubField && pSubField->pfnSet)
        return pSubField->pfnSet(pLookupRec->pSet->uUserArg.pv, pSubField, pValue->u128, pMask->u128);
    return pLookupRec->pDesc->pfnSet(pLookupRec->pSet->uUserArg.pv, pLookupRec->pDesc, pValue, pMask);
}


/**
 * Worker for the register setting.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 * @retval  VINF_DBGF_ZERO_EXTENDED_REGISTER
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idDefCpu            The virtual CPU ID for the default CPU register
 *                              set.  Can be OR'ed with DBGFREG_HYPER_VMCPUID.
 * @param   pszReg              The register to query.
 * @param   pValue              The value to set
 * @param   enmType             How to interpret the value in @a pValue.
 */
VMMR3DECL(int) DBGFR3RegNmSet(PUVM pUVM, VMCPUID idDefCpu, const char *pszReg, PCDBGFREGVAL pValue, DBGFREGVALTYPE enmType)
{
    /*
     * Validate input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn((idDefCpu & ~DBGFREG_HYPER_VMCPUID) < pUVM->cCpus || idDefCpu == VMCPUID_ANY, VERR_INVALID_CPU_ID);
    AssertPtrReturn(pszReg, VERR_INVALID_POINTER);
    AssertReturn(enmType > DBGFREGVALTYPE_INVALID && enmType < DBGFREGVALTYPE_END, VERR_INVALID_PARAMETER);
    AssertPtrReturn(pValue, VERR_INVALID_PARAMETER);

    /*
     * Resolve the register and check that it is writable.
     */
    bool fGuestRegs = true;
    if ((idDefCpu & DBGFREG_HYPER_VMCPUID) && idDefCpu != VMCPUID_ANY)
    {
        fGuestRegs = false;
        idDefCpu &= ~DBGFREG_HYPER_VMCPUID;
    }
    PCDBGFREGLOOKUP pLookupRec = dbgfR3RegResolve(pUVM, idDefCpu, pszReg, fGuestRegs);
    if (pLookupRec)
    {
        PCDBGFREGDESC       pDesc        = pLookupRec->pDesc;
        PCDBGFREGSET        pSet         = pLookupRec->pSet;
        PCDBGFREGSUBFIELD   pSubField    = pLookupRec->pSubField;

        if (  !(pDesc->fFlags & DBGFREG_FLAGS_READ_ONLY)
            && (pSubField
                ?    !(pSubField->fFlags & DBGFREGSUBFIELD_FLAGS_READ_ONLY)
                  && (pSubField->pfnSet != NULL || pDesc->pfnSet != NULL)
                : pDesc->pfnSet != NULL) )
        {
            /*
             * Calculate the modification mask and cast the input value to the
             * type of the target register.
             */
            DBGFREGVAL Mask  = DBGFREGVAL_INITIALIZE_ZERO;
            DBGFREGVAL Value = DBGFREGVAL_INITIALIZE_ZERO;
            switch (enmType)
            {
                case DBGFREGVALTYPE_U8:
                    Value.u8  = pValue->u8;
                    Mask.u8   = UINT8_MAX;
                    break;
                case DBGFREGVALTYPE_U16:
                    Value.u16 = pValue->u16;
                    Mask.u16  = UINT16_MAX;
                    break;
                case DBGFREGVALTYPE_U32:
                    Value.u32 = pValue->u32;
                    Mask.u32  = UINT32_MAX;
                    break;
                case DBGFREGVALTYPE_U64:
                    Value.u64 = pValue->u64;
                    Mask.u64  = UINT64_MAX;
                    break;
                case DBGFREGVALTYPE_U128:
                    Value.u128 = pValue->u128;
                    Mask.u128.s.Lo = UINT64_MAX;
                    Mask.u128.s.Hi = UINT64_MAX;
                    break;
                case DBGFREGVALTYPE_U256:
                    Value.u256 = pValue->u256;
                    Mask.u256.QWords.qw0 = UINT64_MAX;
                    Mask.u256.QWords.qw1 = UINT64_MAX;
                    Mask.u256.QWords.qw2 = UINT64_MAX;
                    Mask.u256.QWords.qw3 = UINT64_MAX;
                    break;
                case DBGFREGVALTYPE_U512:
                    Value.u512 = pValue->u512;
                    Mask.u512.QWords.qw0 = UINT64_MAX;
                    Mask.u512.QWords.qw1 = UINT64_MAX;
                    Mask.u512.QWords.qw2 = UINT64_MAX;
                    Mask.u512.QWords.qw3 = UINT64_MAX;
                    Mask.u512.QWords.qw4 = UINT64_MAX;
                    Mask.u512.QWords.qw5 = UINT64_MAX;
                    Mask.u512.QWords.qw6 = UINT64_MAX;
                    Mask.u512.QWords.qw7 = UINT64_MAX;
                    break;
                case DBGFREGVALTYPE_R80:
#ifdef RT_COMPILER_WITH_80BIT_LONG_DOUBLE
                    Value.r80Ex.lrd = pValue->r80Ex.lrd;
#else
                    Value.r80Ex.au64[0] = pValue->r80Ex.au64[0];
                    Value.r80Ex.au16[4] = pValue->r80Ex.au16[4];
#endif
                    Value.r80Ex.au64[0] = UINT64_MAX;
                    Value.r80Ex.au16[4] = UINT16_MAX;
                    break;
                case DBGFREGVALTYPE_DTR:
                    Value.dtr.u32Limit = pValue->dtr.u32Limit;
                    Value.dtr.u64Base  = pValue->dtr.u64Base;
                    Mask.dtr.u32Limit  = UINT32_MAX;
                    Mask.dtr.u64Base   = UINT64_MAX;
                    break;
                case DBGFREGVALTYPE_32BIT_HACK:
                case DBGFREGVALTYPE_END:
                case DBGFREGVALTYPE_INVALID:
                    AssertFailedReturn(VERR_INTERNAL_ERROR_3);
            }

            int rc = VINF_SUCCESS;
            DBGFREGVALTYPE enmRegType = pDesc->enmType;
            if (pSubField)
            {
                unsigned const cBits = pSubField->cBits + pSubField->cShift;
                if (cBits <= 8)
                    enmRegType = DBGFREGVALTYPE_U8;
                else if (cBits <= 16)
                    enmRegType = DBGFREGVALTYPE_U16;
                else if (cBits <= 32)
                    enmRegType = DBGFREGVALTYPE_U32;
                else if (cBits <= 64)
                    enmRegType = DBGFREGVALTYPE_U64;
                else if (cBits <= 128)
                    enmRegType = DBGFREGVALTYPE_U128;
                else if (cBits <= 256)
                    enmRegType = DBGFREGVALTYPE_U256;
                else
                    enmRegType = DBGFREGVALTYPE_U512;
            }
            else if (pLookupRec->pAlias)
            {
                /* Restrict the input to the size of the alias register. */
                DBGFREGVALTYPE enmAliasType = pLookupRec->pAlias->enmType;
                if (enmAliasType != enmType)
                {
                    rc = dbgfR3RegValCast(&Value, enmType, enmAliasType);
                    if (RT_FAILURE(rc))
                        return rc;
                    dbgfR3RegValCast(&Mask, enmType, enmAliasType);
                    enmType = enmAliasType;
                }
            }

            if (enmType != enmRegType)
            {
                int rc2 = dbgfR3RegValCast(&Value, enmType, enmRegType);
                if (RT_FAILURE(rc2))
                    return rc2;
                if (rc2 != VINF_SUCCESS && rc == VINF_SUCCESS)
                    rc2 = VINF_SUCCESS;
                dbgfR3RegValCast(&Mask, enmType, enmRegType);
            }

            /*
             * Subfields needs some extra processing if there is no subfield
             * setter, since we'll be feeding it to the normal register setter
             * instead.  The mask and value must be shifted and truncated to the
             * subfield position.
             */
            if (pSubField && !pSubField->pfnSet)
            {
                /* The shift factor is for displaying a subfield value
                   2**cShift times larger than the stored value.  We have
                   to undo this before adjusting value and mask.  */
                if (pSubField->cShift)
                {
                    /* Warn about trunction of the lower bits that get
                       shifted out below. */
                    if (rc == VINF_SUCCESS)
                    {
                        DBGFREGVAL Value2 = Value;
                        RTUInt128AssignAndNFirstBits(&Value2.u128, -pSubField->cShift);
                        if (!RTUInt128BitAreAllClear(&Value2.u128))
                            rc = VINF_DBGF_TRUNCATED_REGISTER;
                    }
                    RTUInt128AssignShiftRight(&Value.u128, pSubField->cShift);
                }

                RTUInt128AssignAndNFirstBits(&Value.u128, pSubField->cBits);
                if (rc == VINF_SUCCESS && RTUInt128IsNotEqual(&Value.u128, &Value.u128))
                    rc = VINF_DBGF_TRUNCATED_REGISTER;
                RTUInt128AssignAndNFirstBits(&Mask.u128,  pSubField->cBits);

                RTUInt128AssignShiftLeft(&Value.u128, pSubField->iFirstBit);
                RTUInt128AssignShiftLeft(&Mask.u128,  pSubField->iFirstBit);
            }

            /*
             * Do the actual work on an EMT.
             */
            if (pSet->enmType == DBGFREGSETTYPE_CPU)
                idDefCpu = pSet->uUserArg.pVCpu->idCpu;
            else if (idDefCpu != VMCPUID_ANY)
                idDefCpu &= ~DBGFREG_HYPER_VMCPUID;

            int rc2 = VMR3ReqPriorityCallWaitU(pUVM, idDefCpu, (PFNRT)dbgfR3RegNmSetWorkerOnCpu, 4,
                                               pUVM, pLookupRec, &Value, &Mask);

            if (rc == VINF_SUCCESS || RT_FAILURE(rc2))
                rc = rc2;
            return rc;
        }
        return VERR_DBGF_READ_ONLY_REGISTER;
    }
    return VERR_DBGF_REGISTER_NOT_FOUND;
}


/**
 * Set a given set of registers.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_CPU_ID
 * @retval  VERR_DBGF_REGISTER_NOT_FOUND
 * @retval  VERR_DBGF_UNSUPPORTED_CAST
 * @retval  VINF_DBGF_TRUNCATED_REGISTER
 * @retval  VINF_DBGF_ZERO_EXTENDED_REGISTER
 *
 * @param   pUVM                The user mode VM handle.
 * @param   idDefCpu            The virtual CPU ID for the default CPU register
 *                              set.  Can be OR'ed with DBGFREG_HYPER_VMCPUID.
 * @param   paRegs              The array of registers to set.
 * @param   cRegs               Number of registers in the array.
 *
 * @todo This is a _very_ lazy implementation by a lazy developer, some semantics
  *      need to be figured out before the real implementation especially how and
  *      when errors and informational status codes like VINF_DBGF_TRUNCATED_REGISTER
  *      should be returned (think of an error right in the middle of the batch, should we
  *      save the state and roll back?).
 */
VMMR3DECL(int) DBGFR3RegNmSetBatch(PUVM pUVM, VMCPUID idDefCpu, PCDBGFREGENTRYNM paRegs, size_t cRegs)
{
    /*
     * Validate input.
     */
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    VM_ASSERT_VALID_EXT_RETURN(pUVM->pVM, VERR_INVALID_VM_HANDLE);
    AssertReturn((idDefCpu & ~DBGFREG_HYPER_VMCPUID) < pUVM->cCpus || idDefCpu == VMCPUID_ANY, VERR_INVALID_CPU_ID);
    AssertPtrReturn(paRegs, VERR_INVALID_PARAMETER);
    AssertReturn(cRegs > 0, VERR_INVALID_PARAMETER);

    for (uint32_t i = 0; i < cRegs; i++)
    {
        int rc = DBGFR3RegNmSet(pUVM, idDefCpu, paRegs[i].pszName, &paRegs[i].Val, paRegs[i].enmType);
        if (RT_FAILURE(rc))
            return rc;
    }

    return VINF_SUCCESS;
}


/**
 * Internal worker for DBGFR3RegFormatValue, cbBuf is sufficent.
 *
 * @copydoc DBGFR3RegFormatValueEx
 */
DECLINLINE(ssize_t) dbgfR3RegFormatValueInt(char *pszBuf, size_t cbBuf, PCDBGFREGVAL pValue, DBGFREGVALTYPE enmType,
                                            unsigned uBase, signed int cchWidth, signed int cchPrecision, uint32_t fFlags)
{
    switch (enmType)
    {
        case DBGFREGVALTYPE_U8:
            return RTStrFormatU8(pszBuf, cbBuf, pValue->u8, uBase, cchWidth, cchPrecision, fFlags);
        case DBGFREGVALTYPE_U16:
            return RTStrFormatU16(pszBuf, cbBuf, pValue->u16, uBase, cchWidth, cchPrecision, fFlags);
        case DBGFREGVALTYPE_U32:
            return RTStrFormatU32(pszBuf, cbBuf, pValue->u32, uBase, cchWidth, cchPrecision, fFlags);
        case DBGFREGVALTYPE_U64:
            return RTStrFormatU64(pszBuf, cbBuf, pValue->u64, uBase, cchWidth, cchPrecision, fFlags);
        case DBGFREGVALTYPE_U128:
            return RTStrFormatU128(pszBuf, cbBuf, &pValue->u128, uBase, cchWidth, cchPrecision, fFlags);
        case DBGFREGVALTYPE_U256:
            return RTStrFormatU256(pszBuf, cbBuf, &pValue->u256, uBase, cchWidth, cchPrecision, fFlags);
        case DBGFREGVALTYPE_U512:
            return RTStrFormatU512(pszBuf, cbBuf, &pValue->u512, uBase, cchWidth, cchPrecision, fFlags);
        case DBGFREGVALTYPE_R80:
            return RTStrFormatR80u2(pszBuf, cbBuf, &pValue->r80Ex, cchWidth, cchPrecision, fFlags);
        case DBGFREGVALTYPE_DTR:
        {
            ssize_t cch = RTStrFormatU64(pszBuf, cbBuf, pValue->dtr.u64Base,
                                         16, 2+16, 0, RTSTR_F_SPECIAL | RTSTR_F_ZEROPAD);
            AssertReturn(cch > 0, VERR_DBGF_REG_IPE_1);
            pszBuf[cch++] = ':';
            cch += RTStrFormatU64(&pszBuf[cch], cbBuf - cch, pValue->dtr.u32Limit,
                                  16, 4, 0, RTSTR_F_ZEROPAD | RTSTR_F_32BIT);
            return cch;
        }

        case DBGFREGVALTYPE_32BIT_HACK:
        case DBGFREGVALTYPE_END:
        case DBGFREGVALTYPE_INVALID:
            break;
        /* no default, want gcc warnings */
    }

    RTStrPrintf(pszBuf, cbBuf, "!enmType=%d!", enmType);
    return VERR_DBGF_REG_IPE_2;
}


/**
 * Format a register value, extended version.
 *
 * @returns The number of bytes returned, VERR_BUFFER_OVERFLOW on failure.
 * @param   pszBuf          The output buffer.
 * @param   cbBuf           The size of the output buffer.
 * @param   pValue          The value to format.
 * @param   enmType         The value type.
 * @param   uBase           The base (ignored if not applicable).
 * @param   cchWidth        The width if RTSTR_F_WIDTH is set, otherwise
 *                          ignored.
 * @param   cchPrecision    The width if RTSTR_F_PRECISION is set, otherwise
 *                          ignored.
 * @param   fFlags          String formatting flags, RTSTR_F_XXX.
 */
VMMR3DECL(ssize_t) DBGFR3RegFormatValueEx(char *pszBuf, size_t cbBuf, PCDBGFREGVAL pValue, DBGFREGVALTYPE enmType,
                                          unsigned uBase, signed int cchWidth, signed int cchPrecision, uint32_t fFlags)
{
    /*
     * Format to temporary buffer using worker shared with dbgfR3RegPrintfCbFormatNormal.
     */
    char szTmp[160];
    ssize_t cchOutput = dbgfR3RegFormatValueInt(szTmp, sizeof(szTmp), pValue, enmType, uBase, cchWidth, cchPrecision, fFlags);
    if (cchOutput > 0)
    {
        if ((size_t)cchOutput < cbBuf)
            memcpy(pszBuf, szTmp, cchOutput + 1);
        else
        {
            if (cbBuf)
            {
                memcpy(pszBuf, szTmp, cbBuf - 1);  /* (parfait is wrong about out of bound read here) */
                pszBuf[cbBuf - 1] = '\0';
            }
            cchOutput = VERR_BUFFER_OVERFLOW;
        }
    }
    return cchOutput;
}


/**
 * Format a register value as hexadecimal and with default width according to
 * the type.
 *
 * @returns The number of bytes returned, VERR_BUFFER_OVERFLOW on failure.
 * @param   pszBuf          The output buffer.
 * @param   cbBuf           The size of the output buffer.
 * @param   pValue          The value to format.
 * @param   enmType         The value type.
 * @param   fSpecial        Same as RTSTR_F_SPECIAL.
 */
VMMR3DECL(ssize_t) DBGFR3RegFormatValue(char *pszBuf, size_t cbBuf, PCDBGFREGVAL pValue, DBGFREGVALTYPE enmType, bool fSpecial)
{
    int cchWidth = 0;
    switch (enmType)
    {
        case DBGFREGVALTYPE_U8:     cchWidth = 2   + fSpecial*2; break;
        case DBGFREGVALTYPE_U16:    cchWidth = 4   + fSpecial*2; break;
        case DBGFREGVALTYPE_U32:    cchWidth = 8   + fSpecial*2; break;
        case DBGFREGVALTYPE_U64:    cchWidth = 16  + fSpecial*2; break;
        case DBGFREGVALTYPE_U128:   cchWidth = 32  + fSpecial*2; break;
        case DBGFREGVALTYPE_U256:   cchWidth = 64  + fSpecial*2; break;
        case DBGFREGVALTYPE_U512:   cchWidth = 128 + fSpecial*2; break;
        case DBGFREGVALTYPE_R80:    cchWidth = 0; break;
        case DBGFREGVALTYPE_DTR:    cchWidth = 16+1+4 + fSpecial*2; break;

        case DBGFREGVALTYPE_32BIT_HACK:
        case DBGFREGVALTYPE_END:
        case DBGFREGVALTYPE_INVALID:
            break;
        /* no default, want gcc warnings */
    }
    uint32_t fFlags = RTSTR_F_ZEROPAD;
    if (fSpecial)
        fFlags |= RTSTR_F_SPECIAL;
    if (cchWidth != 0)
        fFlags |= RTSTR_F_WIDTH;
    return DBGFR3RegFormatValueEx(pszBuf, cbBuf, pValue, enmType, 16, cchWidth, 0, fFlags);
}


/**
 * Format a register using special hacks as well as sub-field specifications
 * (the latter isn't implemented yet).
 */
static size_t
dbgfR3RegPrintfCbFormatField(PDBGFR3REGPRINTFARGS pThis, PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                             PCDBGFREGLOOKUP pLookupRec, int cchWidth, int cchPrecision, unsigned fFlags)
{
    char szTmp[160];

    NOREF(cchWidth); NOREF(cchPrecision); NOREF(fFlags);

    /*
     * Retrieve the register value.
     */
    DBGFREGVAL      Value;
    DBGFREGVALTYPE  enmType;
    int rc = dbgfR3RegNmQueryWorkerOnCpu(pThis->pUVM, pLookupRec, DBGFREGVALTYPE_END, &Value, &enmType);
    if (RT_FAILURE(rc))
    {
        ssize_t cchDefine = RTErrQueryDefine(rc, szTmp, sizeof(szTmp), true /*fFailIfUnknown*/);
        if (cchDefine <= 0)
            cchDefine = RTStrPrintf(szTmp, sizeof(szTmp), "rc=%d", rc);
        return pfnOutput(pvArgOutput, szTmp, cchDefine);
    }

    char *psz = szTmp;

    /*
     * Special case: Format eflags.
     */
    if (   pLookupRec->pSet->enmType == DBGFREGSETTYPE_CPU
        && pLookupRec->pDesc->enmReg == DBGFREG_RFLAGS
        && pLookupRec->pSubField     == NULL)
    {
        rc = dbgfR3RegValCast(&Value, enmType, DBGFREGVALTYPE_U32);
        AssertRC(rc);
        uint32_t const efl = Value.u32;

        /* the iopl */
        psz += RTStrPrintf(psz, sizeof(szTmp) / 2, "iopl=%u ", X86_EFL_GET_IOPL(efl));

        /* add flags */
        static const struct
        {
            const char *pszSet;
            const char *pszClear;
            uint32_t fFlag;
        } aFlags[] =
        {
            { "vip",NULL, X86_EFL_VIP },
            { "vif",NULL, X86_EFL_VIF },
            { "ac", NULL, X86_EFL_AC },
            { "vm", NULL, X86_EFL_VM },
            { "rf", NULL, X86_EFL_RF },
            { "nt", NULL, X86_EFL_NT },
            { "ov", "nv", X86_EFL_OF },
            { "dn", "up", X86_EFL_DF },
            { "ei", "di", X86_EFL_IF },
            { "tf", NULL, X86_EFL_TF },
            { "ng", "pl", X86_EFL_SF },
            { "zr", "nz", X86_EFL_ZF },
            { "ac", "na", X86_EFL_AF },
            { "po", "pe", X86_EFL_PF },
            { "cy", "nc", X86_EFL_CF },
        };
        for (unsigned i = 0; i < RT_ELEMENTS(aFlags); i++)
        {
            const char *pszAdd = aFlags[i].fFlag & efl ? aFlags[i].pszSet : aFlags[i].pszClear;
            if (pszAdd)
            {
                *psz++ = *pszAdd++;
                *psz++ = *pszAdd++;
                if (*pszAdd)
                    *psz++ = *pszAdd++;
                *psz++ = ' ';
            }
        }

        /* drop trailing space */
        psz--;
    }
    else
    {
        /*
         * General case.
         */
        AssertMsgFailed(("Not implemented: %s\n", pLookupRec->Core.pszString));
        return pfnOutput(pvArgOutput, pLookupRec->Core.pszString, pLookupRec->Core.cchString);
    }

    /* Output the string. */
    return pfnOutput(pvArgOutput, szTmp, psz - &szTmp[0]);
}


/**
 * Formats a register having parsed up to the register name.
 */
static size_t
dbgfR3RegPrintfCbFormatNormal(PDBGFR3REGPRINTFARGS pThis, PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                              PCDBGFREGLOOKUP pLookupRec, unsigned uBase, int cchWidth, int cchPrecision, unsigned fFlags)
{
    char szTmp[160];

    /*
     * Get the register value.
     */
    DBGFREGVAL      Value;
    DBGFREGVALTYPE  enmType;
    int rc = dbgfR3RegNmQueryWorkerOnCpu(pThis->pUVM, pLookupRec, DBGFREGVALTYPE_END, &Value, &enmType);
    if (RT_FAILURE(rc))
    {
        ssize_t cchDefine = RTErrQueryDefine(rc, szTmp, sizeof(szTmp), true /*fFailIfUnknown*/);
        if (cchDefine <= 0)
            cchDefine = RTStrPrintf(szTmp, sizeof(szTmp), "rc=%d", rc);
        return pfnOutput(pvArgOutput, szTmp, cchDefine);
    }

    /*
     * Format the value.
     */
    ssize_t cchOutput = dbgfR3RegFormatValueInt(szTmp, sizeof(szTmp), &Value, enmType, uBase, cchWidth, cchPrecision, fFlags);
    if (RT_UNLIKELY(cchOutput <= 0))
    {
        AssertFailed();
        return pfnOutput(pvArgOutput, "internal-error", sizeof("internal-error") - 1);
    }
    return pfnOutput(pvArgOutput, szTmp, cchOutput);
}


/**
 * @callback_method_impl{FNSTRFORMAT}
 */
static DECLCALLBACK(size_t)
dbgfR3RegPrintfCbFormat(void *pvArg, PFNRTSTROUTPUT pfnOutput, void *pvArgOutput,
                        const char **ppszFormat, va_list *pArgs, int cchWidth,
                        int cchPrecision, unsigned fFlags, char chArgSize)
{
    NOREF(pArgs); NOREF(chArgSize);

    /*
     * Parse the format type and hand the job to the appropriate worker.
     */
    PDBGFR3REGPRINTFARGS pThis = (PDBGFR3REGPRINTFARGS)pvArg;
    const char *pszFormat = *ppszFormat;
    if (    pszFormat[0] != 'V'
        ||  pszFormat[1] != 'R')
    {
        AssertMsgFailed(("'%s'\n", pszFormat));
        return 0;
    }
    unsigned offCurly = 2;
    if (pszFormat[offCurly] != '{')
    {
        AssertMsgReturn(pszFormat[offCurly], ("'%s'\n", pszFormat), 0);
        offCurly++;
        AssertMsgReturn(pszFormat[offCurly] == '{', ("'%s'\n", pszFormat), 0);
    }
    const char *pachReg = &pszFormat[offCurly + 1];

    /*
     * The end and length of the register.
     */
    const char *pszEnd = strchr(pachReg, '}');
    AssertMsgReturn(pszEnd, ("Missing closing curly bracket: '%s'\n", pszFormat), 0);
    size_t const cchReg = pszEnd - pachReg;

    /*
     * Look up the register - same as dbgfR3RegResolve, except for locking and
     * input string termination.
     */
    PRTSTRSPACE pRegSpace = &pThis->pUVM->dbgf.s.RegSpace;
    /* Try looking up the name without any case folding or cpu prefixing. */
    PCDBGFREGLOOKUP pLookupRec = (PCDBGFREGLOOKUP)RTStrSpaceGetN(pRegSpace, pachReg, cchReg);
    if (!pLookupRec)
    {
        /* Lower case it and try again. */
        char szName[DBGF_REG_MAX_NAME * 4 + 16];
        ssize_t cchFolded = dbgfR3RegCopyToLower(pachReg, cchReg, szName, sizeof(szName) - DBGF_REG_MAX_NAME);
        if (cchFolded > 0)
            pLookupRec = (PCDBGFREGLOOKUP)RTStrSpaceGet(pRegSpace, szName);
        if (   !pLookupRec
            && cchFolded >= 0
            && pThis->idCpu != VMCPUID_ANY)
        {
            /* Prefix it with the specified CPU set. */
            size_t cchCpuSet = RTStrPrintf(szName, sizeof(szName), pThis->fGuestRegs ? "cpu%u." : "hypercpu%u.", pThis->idCpu);
            dbgfR3RegCopyToLower(pachReg, cchReg, &szName[cchCpuSet], sizeof(szName) - cchCpuSet);
            pLookupRec = (PCDBGFREGLOOKUP)RTStrSpaceGet(pRegSpace, szName);
        }
    }
    AssertMsgReturn(pLookupRec, ("'%s'\n", pszFormat), 0);
    AssertMsgReturn(   pLookupRec->pSet->enmType != DBGFREGSETTYPE_CPU
                    || pLookupRec->pSet->uUserArg.pVCpu->idCpu == pThis->idCpu,
                    ("'%s' idCpu=%u, pSet/cpu=%u\n", pszFormat, pThis->idCpu, pLookupRec->pSet->uUserArg.pVCpu->idCpu),
                    0);

    /*
     * Commit the parsed format string.  Up to this point it is nice to know
     * what register lookup failed and such, so we've delayed comitting.
     */
    *ppszFormat = pszEnd + 1;

    /*
     * Call the responsible worker.
     */
    switch (pszFormat[offCurly - 1])
    {
        case 'R': /* %VR{} */
        case 'X': /* %VRX{} */
            return dbgfR3RegPrintfCbFormatNormal(pThis, pfnOutput, pvArgOutput, pLookupRec,
                                                 16, cchWidth, cchPrecision, fFlags);
        case 'U':
            return dbgfR3RegPrintfCbFormatNormal(pThis, pfnOutput, pvArgOutput, pLookupRec,
                                                 10, cchWidth, cchPrecision, fFlags);
        case 'O':
            return dbgfR3RegPrintfCbFormatNormal(pThis, pfnOutput, pvArgOutput, pLookupRec,
                                                 8, cchWidth, cchPrecision, fFlags);
        case 'B':
            return dbgfR3RegPrintfCbFormatNormal(pThis, pfnOutput, pvArgOutput, pLookupRec,
                                                 2, cchWidth, cchPrecision, fFlags);
        case 'F':
            return dbgfR3RegPrintfCbFormatField(pThis, pfnOutput, pvArgOutput, pLookupRec, cchWidth, cchPrecision, fFlags);
        default:
            AssertFailed();
            return 0;
    }
}



/**
 * @callback_method_impl{FNRTSTROUTPUT}
 */
static DECLCALLBACK(size_t)
dbgfR3RegPrintfCbOutput(void *pvArg, const char *pachChars, size_t cbChars)
{
    PDBGFR3REGPRINTFARGS    pArgs    = (PDBGFR3REGPRINTFARGS)pvArg;
    size_t                  cbToCopy = cbChars;
    if (cbToCopy >= pArgs->cchLeftBuf)
    {
        if (RT_SUCCESS(pArgs->rc))
            pArgs->rc = VERR_BUFFER_OVERFLOW;
        cbToCopy = pArgs->cchLeftBuf;
    }
    if (cbToCopy > 0)
    {
        memcpy(&pArgs->pszBuf[pArgs->offBuf], pachChars, cbToCopy);
        pArgs->offBuf     += cbToCopy;
        pArgs->cchLeftBuf -= cbToCopy;
        pArgs->pszBuf[pArgs->offBuf] = '\0';
    }
    return cbToCopy;
}


/**
 * On CPU worker for the register formatting, used by DBGFR3RegPrintfV.
 *
 * @returns VBox status code.
 *
 * @param   pArgs               The argument package and state.
 */
static DECLCALLBACK(int) dbgfR3RegPrintfWorkerOnCpu(PDBGFR3REGPRINTFARGS pArgs)
{
    DBGF_REG_DB_LOCK_READ(pArgs->pUVM);
    RTStrFormatV(dbgfR3RegPrintfCbOutput, pArgs, dbgfR3RegPrintfCbFormat, pArgs, pArgs->pszFormat, pArgs->va);
    DBGF_REG_DB_UNLOCK_READ(pArgs->pUVM);
    return pArgs->rc;
}


/**
 * Format a registers.
 *
 * This is restricted to registers from one CPU, that specified by @a idCpu.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               The CPU ID of any CPU registers that may be
 *                              printed, pass VMCPUID_ANY if not applicable.
 * @param   pszBuf              The output buffer.
 * @param   cbBuf               The size of the output buffer.
 * @param   pszFormat           The format string.  Register names are given by
 *                              %VR{name}, they take no arguments.
 * @param   va                  Other format arguments.
 */
VMMR3DECL(int) DBGFR3RegPrintfV(PUVM pUVM, VMCPUID idCpu, char *pszBuf, size_t cbBuf, const char *pszFormat, va_list va)
{
    AssertPtrReturn(pszBuf, VERR_INVALID_POINTER);
    AssertReturn(cbBuf > 0, VERR_BUFFER_OVERFLOW);
    *pszBuf = '\0';

    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn((idCpu & ~DBGFREG_HYPER_VMCPUID) < pUVM->cCpus || idCpu == VMCPUID_ANY, VERR_INVALID_CPU_ID);
    AssertPtrReturn(pszFormat, VERR_INVALID_POINTER);

    /*
     * Set up an argument package and execute the formatting on the
     * specified CPU.
     */
    DBGFR3REGPRINTFARGS Args;
    Args.pUVM       = pUVM;
    Args.idCpu      = idCpu != VMCPUID_ANY ? idCpu & ~DBGFREG_HYPER_VMCPUID : idCpu;
    Args.fGuestRegs = idCpu != VMCPUID_ANY && !(idCpu & DBGFREG_HYPER_VMCPUID);
    Args.pszBuf     = pszBuf;
    Args.pszFormat  = pszFormat;
    va_copy(Args.va, va);
    Args.offBuf     = 0;
    Args.cchLeftBuf = cbBuf - 1;
    Args.rc         = VINF_SUCCESS;
    int rc = VMR3ReqPriorityCallWaitU(pUVM, Args.idCpu, (PFNRT)dbgfR3RegPrintfWorkerOnCpu, 1, &Args);
    va_end(Args.va);
    return rc;
}


/**
 * Format a registers.
 *
 * This is restricted to registers from one CPU, that specified by @a idCpu.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   idCpu               The CPU ID of any CPU registers that may be
 *                              printed, pass VMCPUID_ANY if not applicable.
 * @param   pszBuf              The output buffer.
 * @param   cbBuf               The size of the output buffer.
 * @param   pszFormat           The format string.  Register names are given by
 *                              %VR{name}, %VRU{name}, %VRO{name} and
 *                              %VRB{name}, which are hexadecimal, (unsigned)
 *                              decimal, octal and binary representation.  None
 *                              of these types takes any arguments.
 * @param   ...                 Other format arguments.
 */
VMMR3DECL(int) DBGFR3RegPrintf(PUVM pUVM, VMCPUID idCpu, char *pszBuf, size_t cbBuf, const char *pszFormat, ...)
{
    va_list va;
    va_start(va, pszFormat);
    int rc = DBGFR3RegPrintfV(pUVM, idCpu, pszBuf, cbBuf, pszFormat, va);
    va_end(va);
    return rc;
}

