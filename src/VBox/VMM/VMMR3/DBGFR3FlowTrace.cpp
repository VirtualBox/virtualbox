/* $Id: DBGFR3FlowTrace.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, Guest Execution Flow Tracing.
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


/** @page pg_dbgf_flow DBGFR3FlowTrace - Flow Trace Interface
 *
 * @todo
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DBGF
#include <VBox/vmm/dbgfflowtrace.h>
#include "DBGFInternal.h"
#include <VBox/vmm/mm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/vmm/vm.h>
#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/assert.h>
#include <iprt/semaphore.h>
#include <iprt/list.h>
#include <iprt/time.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/



/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/

/** Pointer to the internal trace module instance data. */
typedef struct DBGFFLOWTRACEMODINT *PDBGFFLOWTRACEMODINT;
/** Pointer to a trace module probe location. */
typedef struct DBGFFLOWTRACEMODPROBELOC *PDBGFFLOWTRACEMODPROBELOC;

/**
 * Internal probe instance data.
 */
typedef struct DBGFFLOWTRACEPROBEINT
{
    /** External and internal references hold. */
    volatile uint32_t           cRefs;
    /** Trace modules referencing this probe. */
    volatile uint32_t           cRefsMod;
    /** The user mode VM handle. */
    PUVM                        pUVM;
    /** Description of this probe. */
    char                        *pszDescr;
    /** Overall memory consumed for this probe for each invocation. */
    size_t                      cbProbe;
    /** Number of entries for this probe. */
    uint32_t                    cEntries;
    /** Maximum number of entries the array can hold. */
    uint32_t                    cEntriesMax;
    /** Pointer to the probe entry array. */
    PDBGFFLOWTRACEPROBEENTRY    paEntries;
} DBGFFLOWTRACEPROBEINT;
/** Pointer to the internal probe instance data. */
typedef DBGFFLOWTRACEPROBEINT *PDBGFFLOWTRACEPROBEINT;
/** Pointer to a constant internal probe instance data. */
typedef const DBGFFLOWTRACEPROBEINT *PCDBGFFLOWTRACEPROBEINT;

/**
 * Record collected for one probe hit.
 */
typedef struct DBGFFLOWTRACERECORDINT
{
    /** Data list node. */
    RTLISTNODE                  NdRecord;
    /** The probe instance the record was created for. */
    PDBGFFLOWTRACEPROBEINT      pProbe;
    /** The common probe instance data was collected for. */
    PDBGFFLOWTRACEPROBEINT      pProbeCmn;
    /** Address of the probe location. */
    DBGFADDRESS                 AddrProbe;
    /** Reference counter. */
    volatile uint32_t           cRefs;
    /** CPU ID this data was collected on. */
    VMCPUID                     idCpu;
    /** Sequence number for this data. */
    uint64_t                    u64SeqNo;
    /** Timestamp in nanoseconds when the data was collected. */
    uint64_t                    u64TsCollected;
    /** Pointer to the values for the common probe if available. */
    PDBGFFLOWTRACEPROBEVAL      paValCmn;
    /** The probe values collected - size defined
     * by the number of entries in the probe. */
    DBGFFLOWTRACEPROBEVAL       aVal[1];
} DBGFFLOWTRACERECORDINT;
/** Pointer to one collected probe data. */
typedef DBGFFLOWTRACERECORDINT *PDBGFFLOWTRACERECORDINT;

/**
 * Trace module state.
 */
typedef enum DBGFFLOWTRACEMODSTATE
{
    /** Invalid state. */
    DBGFFLOWTRACEMODSTATE_INVALID = 0,
    /** The module was created. */
    DBGFFLOWTRACEMODSTATE_CREATED,
    /** The module is active, no probes can be added. */
    DBGFFLOWTRACEMODSTATE_ENABLED,
    /** The VM is destroyed but there are still references to the module,
     * functionality is limited (query records only). */
    DBGFFLOWTRACEMODSTATE_VM_DESTROYED,
    /** The trace module is destroyed. */
    DBGFFLOWTRACEMODSTATE_DESTROYED,
    /** 32bit hack. */
    DBGFFLOWTRACEMODSTATE_32BIT_HACK = 0x7fffffff
} DBGFFLOWTRACEMODSTATE;

/**
 * Internal trace module instance data.
 */
typedef struct DBGFFLOWTRACEMODINT
{
    /** References hold for this trace module. */
    volatile uint32_t                cRefs;
    /** The user mode VM handle. */
    PUVM                             pUVM;
    /** CPU ID the module is for. */
    VMCPUID                          idCpu;
    /** The DBGF owner handle. */
    DBGFBPOWNER                      hBpOwner;
    /** State of the trace module. */
    volatile DBGFFLOWTRACEMODSTATE   enmState;
    /** Next free sequence number. */
    volatile uint64_t                u64SeqNoNext;
    /** Optional ocmmon probe describing data to collect. */
    PDBGFFLOWTRACEPROBEINT           pProbeCmn;
    /** Flags whether to record only a limited amount of data as indicated
     * by cHitsLeft. */
    bool                             fLimit;
    /** Number of hits left until the module is disabled automatically. */
    volatile uint32_t                cHitsLeft;
    /** Number of records to keep before evicting the oldest one. */
    uint32_t                         cRecordsMax;
    /** Number of records collected in this module. */
    volatile uint32_t                cRecords;
    /** Number of probes in this trace module. */
    uint32_t                         cProbes;
    /** List of probes active for this module - DBGFFLOWTRACEMODPROBELOC. */
    RTLISTANCHOR                     LstProbes;
    /** List of collected data for this module. */
    RTLISTANCHOR                     LstRecords;
    /** Semaphore protecting access to the probe and record list. */
    RTSEMFASTMUTEX                   hMtx;
} DBGFFLOWTRACEMODINT;
/** Pointer to a const internal trace module instance data. */
typedef const DBGFFLOWTRACEMODINT *PCDBGFFLOWTRACEMODINT;

/**
 * Trace module probe location data.
 */
typedef struct DBGFFLOWTRACEMODPROBELOC
{
    /** List node for the list of probes. */
    RTLISTNODE                       NdProbes;
    /** The owning trace module. */
    PDBGFFLOWTRACEMODINT             pTraceMod;
    /** The probe instance. */
    PDBGFFLOWTRACEPROBEINT           pProbe;
    /** Address of the probe location. */
    DBGFADDRESS                      AddrProbe;
    /** The DBGF breakpoint handle. */
    DBGFBP                           hBp;
    /** Flags controlling the collection behavior for the probe. */
    uint32_t                         fFlags;
} DBGFFLOWTRACEMODPROBELOC;


/**
 * Flow trace report state.
 */
typedef struct DBGFFLOWTRACEREPORTINT
{
    /** The user mode VM handle. */
    PUVM                            pUVM;
    /** Reference count. */
    volatile uint32_t               cRefs;
    /** Number of records. */
    uint32_t                        cRecords;
    /** Array with handle of records - variable in size. */
    PDBGFFLOWTRACERECORDINT         apRec[1];
} DBGFFLOWTRACEMODREPORTINT;
/** Pointer to the internal flow trace report state. */
typedef DBGFFLOWTRACEREPORTINT *PDBGFFLOWTRACEREPORTINT;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Creates a new trace record.
 *
 * @returns Pointer to the create flow trace record or NULL if out of memory.
 * @param   pProbeLoc               The probe location to allocate the record for.
 * @param   idCpu                   The CPU ID this record was created for.
 * @param   ppbBuf                  Where to store the pointer to the data buffer for this probe.
 * @param   ppbBufCmn               Where to store the pointer to the data buffer for the common probe
 *                                  if available.
 */
static PDBGFFLOWTRACERECORDINT dbgfR3FlowTraceRecordCreate(PDBGFFLOWTRACEMODPROBELOC pProbeLoc, VMCPUID idCpu,
                                                           uint8_t **ppbBuf, uint8_t **ppbBufCmn)
{
    PDBGFFLOWTRACEMODINT pTraceMod = pProbeLoc->pTraceMod;
    PCDBGFFLOWTRACEPROBEINT pProbe = pProbeLoc->pProbe;
    PCDBGFFLOWTRACEPROBEINT pProbeCmn = pTraceMod->pProbeCmn;
    size_t cbProbeBuf = pProbe->cbProbe;
    if (pProbeCmn)
        cbProbeBuf += pProbeCmn->cbProbe;

    *ppbBuf = NULL;
    *ppbBufCmn = NULL;

    PDBGFFLOWTRACERECORDINT pRecord = (PDBGFFLOWTRACERECORDINT)MMR3HeapAllocZU(pTraceMod->pUVM, MM_TAG_DBGF_FLOWTRACE,
                                                                               sizeof(DBGFFLOWTRACERECORDINT) + cbProbeBuf);
    if (RT_LIKELY(pRecord))
    {
        DBGFR3FlowTraceProbeRetain(pProbeLoc->pProbe);
        if (pProbeLoc->pTraceMod->pProbeCmn)
            DBGFR3FlowTraceProbeRetain(pProbeLoc->pTraceMod->pProbeCmn);

        pRecord->pProbe         = pProbeLoc->pProbe;
        pRecord->pProbeCmn      = pProbeLoc->pTraceMod->pProbeCmn;
        pRecord->AddrProbe      = pProbeLoc->AddrProbe;
        pRecord->cRefs          = 1;
        pRecord->idCpu          = idCpu;
        pRecord->u64SeqNo       = ASMAtomicIncU64(&pTraceMod->u64SeqNoNext);
        pRecord->u64TsCollected = RTTimeNanoTS();
        pRecord->paValCmn       = NULL;

        *ppbBuf = (uint8_t *)&pRecord->aVal[pProbe->cEntries];

        if (pProbeCmn)
        {
            size_t offValCmn = pProbe->cbProbe - pProbe->cEntries * sizeof(DBGFFLOWTRACEPROBEVAL);
            pRecord->paValCmn = (PDBGFFLOWTRACEPROBEVAL)(*ppbBuf + offValCmn);
            *ppbBufCmn = (uint8_t *)&pRecord->paValCmn[pProbeCmn->cEntries];
        }
    }

    return pRecord;
}


/**
 * Destroys the given record.
 *
 * @param   pRecord                 The record to destroy.
 */
static void dbgfR3FlowTraceRecordDestroy(PDBGFFLOWTRACERECORDINT pRecord)
{
    DBGFR3FlowTraceProbeRelease(pRecord->pProbe);
    pRecord->pProbe = NULL;
    MMR3HeapFree(pRecord);
}


/**
 * Creates a new flow trace report which can hold the given amount o records.
 *
 * @returns Pointer to the newly created report state or NULL if out of memory.
 * @param   pUVM                    The usermode VM handle.
 * @param   cRecords                Number of records the report shoudld be able to hold.
 */
static PDBGFFLOWTRACEREPORTINT dbgfR3FlowTraceReportCreate(PUVM pUVM, uint32_t cRecords)
{
    PDBGFFLOWTRACEREPORTINT pReport = (PDBGFFLOWTRACEREPORTINT)MMR3HeapAllocZU(pUVM, MM_TAG_DBGF_FLOWTRACE,
                                                                               RT_UOFFSETOF_DYN(DBGFFLOWTRACEREPORTINT, apRec[cRecords]));
    if (RT_LIKELY(pReport))
    {
        pReport->pUVM     = pUVM;
        pReport->cRefs    = 1;
        pReport->cRecords = cRecords;
    }

    return pReport;
}


/**
 * Destroys the given report releasing all references hold to the containing records.
 *
 * @param   pReport                 The report to destroy.
 */
static void dbgfR3FlowTraceReportDestroy(PDBGFFLOWTRACEREPORTINT pReport)
{
    for (uint32_t i = 0; i < pReport->cRecords; i++)
        DBGFR3FlowTraceRecordRelease(pReport->apRec[i]);
    MMR3HeapFree(pReport);
}


/**
 * Queries the given register and returns the value as a guest pointer.
 *
 * @returns VBox status code.
 * @param   pUVM                    The usermode VM handle.
 * @param   idCpu                   VM CPU identifier.
 * @param   pszReg                  The register name to query.
 * @param   pGCPtr                  Where to store the register value on success.
 */
static int dbgfR3FlowTraceModProbeQueryRegAsGCPtr(PUVM pUVM, VMCPUID idCpu, const char *pszReg,
                                                  PRTGCPTR pGCPtr)
{
    DBGFREGVAL Val;
    DBGFREGVALTYPE enmValType;
    int rc = DBGFR3RegNmQuery(pUVM, idCpu, pszReg, &Val, &enmValType);
    if (RT_SUCCESS(rc))
    {
        switch (enmValType)
        {
            case DBGFREGVALTYPE_U8:
                *pGCPtr = Val.u8;
                break;
            case DBGFREGVALTYPE_U16:
                *pGCPtr = Val.u16;
                break;
            case DBGFREGVALTYPE_U32:
                *pGCPtr = Val.u32;
                break;
            case DBGFREGVALTYPE_U64:
                *pGCPtr = Val.u64;
                break;
            case DBGFREGVALTYPE_U128:
            case DBGFREGVALTYPE_R80:
            case DBGFREGVALTYPE_DTR:
            default:
                rc = VERR_INVALID_PARAMETER;
        }
    }

    return rc;
}


/**
 * Resolves the guest address from an indirect memory probe entry.
 *
 * @returns VBox status code.
 * @param   pUVM                    The usermode VM handle.
 * @param   idCpu                   VM CPU identifier.
 * @param   pEntry                  The probe entry.
 * @param   pAddr                   Where to store the address on success.
 */
static int dbgfR3FlowTraceModProbeResolveIndirectAddr(PUVM pUVM, VMCPUID idCpu, PDBGFFLOWTRACEPROBEENTRY pEntry,
                                                      PDBGFADDRESS pAddr)
{
    Assert(pEntry->enmType == DBGFFLOWTRACEPROBEENTRYTYPE_INDIRECT_MEM);

    RTGCPTR GCPtrBase = 0;
    RTGCPTR GCPtrIndex = 0;
    int rc = dbgfR3FlowTraceModProbeQueryRegAsGCPtr(pUVM, idCpu, pEntry->Type.IndirectMem.RegBase.pszName,
                                                    &GCPtrBase);
    if (   RT_SUCCESS(rc)
        && pEntry->Type.IndirectMem.RegIndex.pszName)
        rc = dbgfR3FlowTraceModProbeQueryRegAsGCPtr(pUVM, idCpu, pEntry->Type.IndirectMem.RegIndex.pszName,
                                                    &GCPtrIndex);
    if (RT_SUCCESS(rc))
    {
        RTGCPTR GCPtr = GCPtrBase + GCPtrIndex * pEntry->Type.IndirectMem.uScale;
        DBGFR3AddrFromFlat(pUVM, pAddr, GCPtr);
        if (pEntry->Type.IndirectMem.iOffset > 0)
            DBGFR3AddrAdd(pAddr, pEntry->Type.IndirectMem.iOffset);
        else if (pEntry->Type.IndirectMem.iOffset < 0)
            DBGFR3AddrSub(pAddr, -pEntry->Type.IndirectMem.iOffset);
    }

    return rc;
}


/**
 * Destroys the given flow trace module freeing all allocated resources.
 *
 * @param   pThis                   The flow trace module instance data.
 */
static void dbgfR3FlowTraceModDestroy(PDBGFFLOWTRACEMODINT pThis)
{
    if (ASMAtomicReadU32((volatile uint32_t *)&pThis->enmState) == DBGFFLOWTRACEMODSTATE_ENABLED)
    {
        int rc = DBGFR3FlowTraceModDisable(pThis);
        AssertRC(rc);
    }

    Assert(   pThis->enmState == DBGFFLOWTRACEMODSTATE_CREATED
           || pThis->enmState == DBGFFLOWTRACEMODSTATE_VM_DESTROYED);

    /* Do the cleanup under the semaphore. */
    RTSemFastMutexRequest(pThis->hMtx);
    if (pThis->pProbeCmn)
        DBGFR3FlowTraceProbeRelease(pThis->pProbeCmn);

    PDBGFFLOWTRACEMODPROBELOC pIt, pItNext;
    RTListForEachSafe(&pThis->LstProbes, pIt, pItNext, DBGFFLOWTRACEMODPROBELOC, NdProbes)
    {
        RTListNodeRemove(&pIt->NdProbes);
        ASMAtomicDecU32(&pIt->pProbe->cRefsMod);
        DBGFR3FlowTraceProbeRelease(pIt->pProbe);
        MMR3HeapFree(pIt);
    }

    PDBGFFLOWTRACERECORDINT pRecIt, pRecItNext;
    RTListForEachSafe(&pThis->LstRecords, pRecIt, pRecItNext, DBGFFLOWTRACERECORDINT, NdRecord)
    {
        RTListNodeRemove(&pRecIt->NdRecord);
        DBGFR3FlowTraceRecordRelease(pRecIt);
    }

    DBGFR3BpOwnerDestroy(pThis->pUVM, pThis->hBpOwner);
    RTSemFastMutexRelease(pThis->hMtx);
    RTSemFastMutexDestroy(pThis->hMtx);
    MMR3HeapFree(pThis);
}


/**
 * Checks whether the given basic block and address intersect.
 *
 * @returns true if they intersect, false otherwise.
 * @param   pAddr               The address to check for.
 * @param   pAddrStart          The start address.
 * @param   pAddrLast           The last address.
 */
static bool dbgfR3FlowTraceAddrIntersect(PDBGFADDRESS pAddr, PDBGFADDRESS pAddrStart,
                                         PDBGFADDRESS pAddrLast)
{
    return    (pAddrStart->Sel == pAddr->Sel)
           && (pAddrStart->off <= pAddr->off)
           && (pAddrLast->off >= pAddr->off);
}


/**
 * Matches a single value against a given filter value.
 *
 * @returns Flag whether the value matches against the single value.
 * @param   pVal                    The value to match.
 * @param   pValFilter              The value filter to match against.
 */
static bool dbgfR3FlowTraceRecordMatchSingleValue(PCDBGFFLOWTRACEPROBEVAL pVal,
                                                  PCDBGFFLOWTRACEPROBEVAL pValFilter)
{
    if (pVal->pProbeEntry->enmType != pValFilter->pProbeEntry->enmType)
        return false;

    switch (pVal->pProbeEntry->enmType)
    {
        case DBGFFLOWTRACEPROBEENTRYTYPE_REG:
        {
            if (pVal->Type.Reg.enmType != pValFilter->Type.Reg.enmType)
                return false;

            if (strcmp(pVal->Type.Reg.pszName, pValFilter->Type.Reg.pszName))
                return false;

            switch (pVal->Type.Reg.enmType)
            {
                case DBGFREGVALTYPE_U8:
                    if (pVal->Type.Reg.Val.u8 != pValFilter->Type.Reg.Val.u8)
                        return false;
                    break;
                case DBGFREGVALTYPE_U16:
                    if (pVal->Type.Reg.Val.u16 != pValFilter->Type.Reg.Val.u16)
                        return false;
                    break;
                case DBGFREGVALTYPE_U32:
                    if (pVal->Type.Reg.Val.u32 != pValFilter->Type.Reg.Val.u32)
                        return false;
                    break;
                case DBGFREGVALTYPE_U64:
                    if (pVal->Type.Reg.Val.u64 != pValFilter->Type.Reg.Val.u64)
                        return false;
                    break;
                case DBGFREGVALTYPE_U128:
                    if (memcmp(&pVal->Type.Reg.Val.u128, &pValFilter->Type.Reg.Val.u128,
                               sizeof(RTUINT128U)))
                        return false;
                    break;
                case DBGFREGVALTYPE_R80:
                    if (memcmp(&pVal->Type.Reg.Val.r80Ex, &pValFilter->Type.Reg.Val.r80Ex,
                               sizeof(RTFLOAT80U2)))
                        return false;
                    break;
                case DBGFREGVALTYPE_DTR:
                    if (   pVal->Type.Reg.Val.dtr.u64Base != pValFilter->Type.Reg.Val.dtr.u64Base
                        || pVal->Type.Reg.Val.dtr.u32Limit != pValFilter->Type.Reg.Val.dtr.u32Limit)
                        return false;
                    break;
                default:
                    AssertFailed();
                    return false;
            }
            break;
        }
        case DBGFFLOWTRACEPROBEENTRYTYPE_CONST_MEM:
        case DBGFFLOWTRACEPROBEENTRYTYPE_INDIRECT_MEM:
            if (   memcmp(&pVal->Type.Mem.Addr, &pValFilter->Type.Mem.Addr,
                          sizeof(DBGFADDRESS))
                || pVal->Type.Mem.cbBuf != pValFilter->Type.Mem.cbBuf
                || memcmp(pVal->Type.Mem.pvBuf, pValFilter->Type.Mem.pvBuf,
                          pValFilter->Type.Mem.cbBuf))
                return false;
            break;
        default:
            AssertFailed();
            return false;
    }

    return true;
}


/**
 * Matches the given values against the filter values returning a flag whether they match.
 *
 * @returns Flag whether the given values match the filter.
 * @param   paVal                   Pointer to the array of values.
 * @param   cVals                   Number of values in the array.
 * @param   paValFilter             Pointer to the filter values.
 * @param   cValsFilter             Number of entries in the filter values.
 */
static bool dbgfR3FlowTraceRecordMatchValues(PCDBGFFLOWTRACEPROBEVAL paVal, uint32_t cVals,
                                             PCDBGFFLOWTRACEPROBEVAL paValFilter, uint32_t cValsFilter)
{
    bool fMatch = false;

    /*
     * The order in which the filters and values are doesn't need to match but for every filter
     * there should be at least one entry matching.
     */
    while (   cValsFilter-- > 0
           && fMatch)
    {
        for (uint32_t i = 0; i < cVals; i++)
        {
            fMatch = dbgfR3FlowTraceRecordMatchSingleValue(&paVal[i], paValFilter);
            if (fMatch)
                break;
        }
        paValFilter++;
    }

    return fMatch;
}


/**
 * Checks the given record against the given filter, returning whether the filter
 * matches.
 *
 * @returns Flag whether the record matches the given filter.
 * @param   pRecord                 The record to check.
 * @param   pFilter                 The filter to check against.
 */
static bool dbgfR3FlowTraceRecordMatchSingleFilter(PDBGFFLOWTRACERECORDINT pRecord,
                                                   PDBGFFLOWTRACEREPORTFILTER pFilter)
{
    bool fMatch = false;

    switch (pFilter->enmType)
    {
        case DBGFFLOWTRACEREPORTFILTERTYPE_SEQ_NUM:
        {
            if (   pRecord->u64SeqNo >= pFilter->Type.SeqNo.u64SeqNoFirst
                && pRecord->u64SeqNo <= pFilter->Type.SeqNo.u64SeqNoLast)
                fMatch = true;
            break;
        }
        case DBGFFLOWTRACEREPORTFILTERTYPE_TIMESTAMP:
        {
            if (   pRecord->u64TsCollected >= pFilter->Type.Timestamp.u64TsFirst
                && pRecord->u64TsCollected <= pFilter->Type.Timestamp.u64TsLast)
                fMatch = true;
            break;
        }
        case DBGFFLOWTRACEREPORTFILTERTYPE_ADDR:
        {
            if (dbgfR3FlowTraceAddrIntersect(&pRecord->AddrProbe,
                                             &pFilter->Type.Addr.AddrStart,
                                             &pFilter->Type.Addr.AddrLast))
                fMatch = true;
            break;
        }
        case DBGFFLOWTRACEREPORTFILTERTYPE_VMCPU_ID:
        {
            if (   pRecord->idCpu >= pFilter->Type.VCpuId.idCpuStart
                && pRecord->idCpu <= pFilter->Type.VCpuId.idCpuLast)
                fMatch = true;
            break;
        }
        case DBGFFLOWTRACEREPORTFILTERTYPE_PROBE_DATA:
        {
            if (pFilter->Type.ProbeData.fValCmn)
            {
                if (pRecord->paValCmn)
                {
                    PCDBGFFLOWTRACEPROBEINT pProbeCmn = pRecord->pProbeCmn;
                    AssertPtr(pProbeCmn);

                    fMatch = dbgfR3FlowTraceRecordMatchValues(pRecord->paValCmn, pProbeCmn->cEntries,
                                                              pFilter->Type.ProbeData.paVal,
                                                              pFilter->Type.ProbeData.cVals);
                }
            }
            else
                fMatch = dbgfR3FlowTraceRecordMatchValues(&pRecord->aVal[0], pRecord->pProbe->cEntries,
                                                          pFilter->Type.ProbeData.paVal, pFilter->Type.ProbeData.cVals);
            break;
        }
        default:
            AssertMsgFailed(("Invalid filter type %u!\n", pFilter->enmType));
    }

    return fMatch;
}


/**
 * Checks the given record against the given filters.
 *
 * @returns Flag whether the record matches the filters.
 * @param   pRecord                 The record to check.
 * @param   paFilters               Array of filters to check.
 * @param   cFilters                Number of filters in the array.
 * @param   enmOp                   How the record should match against the filters.
 */
static bool dbgfR3FlowTraceDoesRecordMatchFilter(PDBGFFLOWTRACERECORDINT pRecord,
                                                 PDBGFFLOWTRACEREPORTFILTER paFilters,
                                                 uint32_t cFilters, DBGFFLOWTRACEREPORTFILTEROP enmOp)
{
    bool fMatch = false;

    if (enmOp == DBGFFLOWTRACEREPORTFILTEROP_AND)
    {
        fMatch = true;
        while (cFilters-- > 0)
        {
            if (!dbgfR3FlowTraceRecordMatchSingleFilter(pRecord, &paFilters[cFilters]))
            {
                fMatch = false;
                break;
            }
        }
    }
    else if (enmOp == DBGFFLOWTRACEREPORTFILTEROP_OR)
    {
        while (cFilters-- > 0)
        {
            if (dbgfR3FlowTraceRecordMatchSingleFilter(pRecord, &paFilters[cFilters]))
            {
                fMatch = true;
                break;
            }
        }
    }
    else
        AssertMsgFailed(("Invalid filter operation %u!\n", enmOp));

    return fMatch;
}


/**
 * Collects all the data specified in the given probe.
 *
 * @returns Flag whether to enter the debugger.
 * @param   pUVM                    The user mode VM handle.
 * @param   idCpu                   The virtual CPU ID.
 * @param   pTraceMod               The trace module instance.
 * @param   pAddrProbe              Location of the probe, NULL if a common probe.
 * @param   pProbe                  The probe instance.
 * @param   pVal                    Pointer to the array of values to fill.
 * @param   pbBuf                   Poitner to the memory buffer holding additional data.
 */
static bool dbgfR3FlowTraceModProbeCollectData(PUVM pUVM, VMCPUID idCpu,
                                               PDBGFFLOWTRACEMODINT pTraceMod,
                                               PCDBGFADDRESS pAddrProbe,
                                               PDBGFFLOWTRACEPROBEINT pProbe,
                                               PDBGFFLOWTRACEPROBEVAL pVal, uint8_t *pbBuf)
{
    bool fDbgDefer = false;

    for (uint32_t i = 0; i < pProbe->cEntries; i++)
    {
        int rc;
        PDBGFFLOWTRACEPROBEENTRY pEntry = &pProbe->paEntries[i];

        pVal->pProbeEntry = pEntry;

        switch (pEntry->enmType)
        {
            case DBGFFLOWTRACEPROBEENTRYTYPE_REG:
                rc = DBGFR3RegNmQuery(pUVM, idCpu, pEntry->Type.Reg.pszName,
                                      &pVal->Type.Reg.Val, &pVal->Type.Reg.enmType);
                AssertRC(rc);
                pVal->Type.Reg.pszName = pEntry->Type.Reg.pszName;
                break;
            case DBGFFLOWTRACEPROBEENTRYTYPE_INDIRECT_MEM:
            {
                DBGFADDRESS Addr;
                rc = dbgfR3FlowTraceModProbeResolveIndirectAddr(pUVM, idCpu, pEntry, &Addr);
                if (RT_SUCCESS(rc))
                {
                    pVal->Type.Mem.pvBuf = pbBuf;
                    pVal->Type.Mem.cbBuf = pEntry->Type.IndirectMem.cbMem;
                    pVal->Type.Mem.Addr  = Addr;
                    rc = DBGFR3MemRead(pUVM, idCpu, &pVal->Type.Mem.Addr, pbBuf,
                                       pVal->Type.Mem.cbBuf);
                    AssertRC(rc);
                    pbBuf += pVal->Type.Mem.cbBuf;
                }
                break;
            }
            case DBGFFLOWTRACEPROBEENTRYTYPE_CONST_MEM:
                pVal->Type.Mem.pvBuf = pbBuf;
                pVal->Type.Mem.cbBuf = pEntry->Type.ConstMem.cbMem;
                pVal->Type.Mem.Addr  = pEntry->Type.ConstMem.AddrMem;
                rc = DBGFR3MemRead(pUVM, idCpu, &pVal->Type.Mem.Addr, pbBuf,
                                   pVal->Type.Mem.cbBuf);
                AssertRC(rc);
                pbBuf += pVal->Type.Mem.cbBuf;
                break;
            case DBGFFLOWTRACEPROBEENTRYTYPE_CALLBACK:
                rc = pEntry->Type.Callback.pfnCallback(pUVM, idCpu, pTraceMod,
                                                       pAddrProbe, pProbe, pEntry,
                                                       pEntry->Type.Callback.pvUser);
                break;
            case DBGFFLOWTRACEPROBEENTRYTYPE_DEBUGGER:
                fDbgDefer = true;
                break;
            default:
                AssertFailed();
        }

        pVal++;
    }

    return fDbgDefer;
}


/**
 * @callback_method_impl{FNDBGFBPHIT}
 */
static DECLCALLBACK(VBOXSTRICTRC) dbgfR3FlowTraceModProbeFiredWorker(PVM pVM, VMCPUID idCpu, void *pvUserBp, DBGFBP hBp, PCDBGFBPPUB pBpPub, uint16_t fFlags)
{
    RT_NOREF(pVM, hBp, pBpPub, fFlags);
    LogFlowFunc(("pVM=%#p idCpu=%u pvUserBp=%#p hBp=%#x pBpPub=%p\n",
                 pVM, idCpu, pvUserBp, hBp, pBpPub));

    PDBGFFLOWTRACEMODPROBELOC pProbeLoc = (PDBGFFLOWTRACEMODPROBELOC)pvUserBp;
    PDBGFFLOWTRACEPROBEINT pProbe = pProbeLoc->pProbe;
    PDBGFFLOWTRACEMODINT pTraceMod = pProbeLoc->pTraceMod;
    bool fDisabledModule = false;
    bool fDbgDefer = false;

    /* Check whether the trace module is still active and we are tracing the correct VCPU. */
    if (ASMAtomicReadU32((volatile uint32_t *)&pTraceMod->enmState) != DBGFFLOWTRACEMODSTATE_ENABLED
        || (   idCpu != pTraceMod->idCpu
            && pTraceMod->idCpu != VMCPUID_ANY))
        return VINF_SUCCESS;

    if (   pTraceMod->fLimit
        && ASMAtomicReadU32(&pTraceMod->cHitsLeft))
    {
        uint32_t cHitsLeftNew = ASMAtomicDecU32(&pTraceMod->cHitsLeft);
        if (cHitsLeftNew > cHitsLeftNew + 1) /* Underflow => reached the limit. */
        {
            ASMAtomicIncU32(&pTraceMod->cHitsLeft);
            return VINF_SUCCESS;
        }

        if (!cHitsLeftNew)
        {
            /* We got the last record, disable the trace module. */
            fDisabledModule = ASMAtomicCmpXchgU32((volatile uint32_t *)&pTraceMod->enmState, DBGFFLOWTRACEMODSTATE_CREATED,
                                                  DBGFFLOWTRACEMODSTATE_ENABLED);
        }
    }

    uint8_t *pbBuf = NULL;
    uint8_t *pbBufCmn = NULL;
    PDBGFFLOWTRACERECORDINT pRecord = dbgfR3FlowTraceRecordCreate(pProbeLoc, idCpu, &pbBuf, &pbBufCmn);
    if (pRecord)
    {
        fDbgDefer = dbgfR3FlowTraceModProbeCollectData(pTraceMod->pUVM, idCpu, pTraceMod, &pProbeLoc->AddrProbe, pProbe,
                                                       &pRecord->aVal[0], pbBuf);
        if (pTraceMod->pProbeCmn)
            fDbgDefer = dbgfR3FlowTraceModProbeCollectData(pTraceMod->pUVM, idCpu, pTraceMod, NULL, pTraceMod->pProbeCmn,
                                                           pRecord->paValCmn, pbBufCmn);

        RTSemFastMutexRequest(pTraceMod->hMtx);
        uint32_t cRecordsNew = ASMAtomicIncU32(&pTraceMod->cRecords);
        RTListAppend(&pTraceMod->LstRecords, &pRecord->NdRecord);
        if (   (cRecordsNew > pTraceMod->cRecordsMax)
            && pTraceMod->cRecordsMax > 0)
        {
            /* Get the first record and destroy it. */
            pRecord = RTListRemoveFirst(&pTraceMod->LstRecords, DBGFFLOWTRACERECORDINT, NdRecord);
            AssertPtr(pRecord);
            DBGFR3FlowTraceRecordRelease(pRecord);
            ASMAtomicDecU32(&pTraceMod->cRecords);
        }
        RTSemFastMutexRelease(pTraceMod->hMtx);
    }

    if (fDisabledModule)
    {
        int rc = DBGFR3FlowTraceModDisable(pTraceMod);
        AssertRC(rc);
    }

    return fDbgDefer ? VINF_DBGF_BP_HALT : VINF_SUCCESS;
}


/**
 * Worker for DBGFR3FlowTraceModEnable(), doing the work in an EMT rendezvous point to
 * ensure no probe is hit in an inconsistent state.
 *
 * @returns Strict VBox status code.
 * @param   pVM                     The VM instance data.
 * @param   pVCpu                   The virtual CPU we execute on.
 * @param   pvUser                  Opaque user data.
 */
static DECLCALLBACK(VBOXSTRICTRC) dbgfR3FlowTraceModEnableWorker(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    RT_NOREF2(pVM, pVCpu);
    PDBGFFLOWTRACEMODINT pThis = (PDBGFFLOWTRACEMODINT)pvUser;
    PDBGFFLOWTRACEMODPROBELOC pProbeLoc = NULL;
    int rc = VINF_SUCCESS;

    pThis->enmState = DBGFFLOWTRACEMODSTATE_ENABLED;

    RTListForEach(&pThis->LstProbes, pProbeLoc, DBGFFLOWTRACEMODPROBELOC, NdProbes)
    {
        uint16_t fBpFlags = DBGF_BP_F_ENABLED;

        if (pProbeLoc->fFlags & DBGF_FLOW_TRACE_PROBE_ADD_F_BEFORE_EXEC)
            fBpFlags |= DBGF_BP_F_HIT_EXEC_BEFORE;
        if (pProbeLoc->fFlags & DBGF_FLOW_TRACE_PROBE_ADD_F_AFTER_EXEC)
            fBpFlags |= DBGF_BP_F_HIT_EXEC_AFTER;

        rc = DBGFR3BpSetInt3Ex(pThis->pUVM, pThis->hBpOwner, pProbeLoc,
                               0 /*idSrcCpu*/, &pProbeLoc->AddrProbe, fBpFlags,
                               0 /*iHitTrigger*/, ~0ULL /*iHitDisable*/, &pProbeLoc->hBp);
        if (RT_FAILURE(rc))
            break;
    }

    if (RT_FAILURE(rc))
        pThis->enmState = DBGFFLOWTRACEMODSTATE_CREATED;

    return rc;
}


/**
 * Worker for DBGFR3FlowTraceModDisable(), doing the work in an EMT rendezvous point to
 * ensure no probe is hit in an inconsistent state.
 *
 * @returns Struct VBox status code.
 * @param   pVM                     The VM instance data.
 * @param   pVCpu                   The virtual CPU we execute on.
 * @param   pvUser                  Opaque user data.
 */
static DECLCALLBACK(VBOXSTRICTRC) dbgfR3FlowTraceModDisableWorker(PVM pVM, PVMCPU pVCpu, void *pvUser)
{
    RT_NOREF2(pVM, pVCpu);
    PDBGFFLOWTRACEMODINT pThis = (PDBGFFLOWTRACEMODINT)pvUser;
    PDBGFFLOWTRACEMODPROBELOC pProbeLoc = NULL;
    int rc = VINF_SUCCESS;

    pThis->enmState = DBGFFLOWTRACEMODSTATE_CREATED;

    RTListForEach(&pThis->LstProbes, pProbeLoc, DBGFFLOWTRACEMODPROBELOC, NdProbes)
    {
        rc = DBGFR3BpClear(pThis->pUVM, pProbeLoc->hBp);
        AssertRC(rc);
    }

    return rc;
}


/**
 * Checks whether both addresses are equal.
 *
 * @returns true if both addresses point to the same location, false otherwise.
 * @param   pAddr1              First address.
 * @param   pAddr2              Second address.
 */
static bool dbgfR3FlowTraceAddrEqual(PCDBGFADDRESS pAddr1, PCDBGFADDRESS pAddr2)
{
    return    pAddr1->Sel == pAddr2->Sel
           && pAddr1->off == pAddr2->off;
}


/**
 * Returns the probe location pointer at the given address for the given trace module.
 *
 * @returns Pointer to the probe location or NULL if there is no probe at the given location.
 * @param   pThis                   The flow trace module instance data.
 * @param   pAddrProbe              Address of the probe to check.
 */
static PDBGFFLOWTRACEMODPROBELOC dbgfR3TraceModGetProbeLocAtAddr(PDBGFFLOWTRACEMODINT pThis, PCDBGFADDRESS pAddrProbe)
{
    RTSemFastMutexRequest(pThis->hMtx);

    PDBGFFLOWTRACEMODPROBELOC pIt;
    RTListForEach(&pThis->LstProbes, pIt, DBGFFLOWTRACEMODPROBELOC, NdProbes)
    {
        if (dbgfR3FlowTraceAddrEqual(&pIt->AddrProbe, pAddrProbe))
        {
            RTSemFastMutexRelease(pThis->hMtx);
            return pIt;
        }
    }
    RTSemFastMutexRelease(pThis->hMtx);
    return NULL;
}


/**
 * Cleans up any allocated resources for each entry in the given probe for the given range.
 *
 * @param   pProbe                  The probe instance.
 * @param   idxStart                Start index to clean up.
 * @param   cEntries                How many entries to clean up.
 */
static void dbgfR3ProbeEntryCleanup(PDBGFFLOWTRACEPROBEINT pProbe, uint32_t idxStart, uint32_t cEntries)
{
    AssertReturnVoid(pProbe->cEntriesMax >= idxStart + cEntries);

    for (uint32_t i = idxStart; i < idxStart + cEntries; i++)
    {
        PDBGFFLOWTRACEPROBEENTRY pEntry = &pProbe->paEntries[i];

        switch (pEntry->enmType)
        {
            case DBGFFLOWTRACEPROBEENTRYTYPE_REG:
                if (pEntry->Type.Reg.pszName)
                    MMR3HeapFree((void *)pEntry->Type.Reg.pszName);
                pEntry->Type.Reg.pszName = NULL;
                break;
            case DBGFFLOWTRACEPROBEENTRYTYPE_CONST_MEM:
                pEntry->Type.ConstMem.cbMem = 0;
                break;
            case DBGFFLOWTRACEPROBEENTRYTYPE_INDIRECT_MEM:
                pEntry->Type.IndirectMem.uScale = 0;
                pEntry->Type.IndirectMem.cbMem  = 0;
                if (pEntry->Type.IndirectMem.RegBase.pszName)
                    MMR3HeapFree((void *)pEntry->Type.IndirectMem.RegBase.pszName);
                if (pEntry->Type.IndirectMem.RegIndex.pszName)
                    MMR3HeapFree((void *)pEntry->Type.IndirectMem.RegIndex.pszName);
                pEntry->Type.IndirectMem.RegBase.pszName = NULL;
                pEntry->Type.IndirectMem.RegIndex.pszName = NULL;
                break;
            case DBGFFLOWTRACEPROBEENTRYTYPE_CALLBACK:
                pEntry->Type.Callback.pfnCallback = NULL;
                pEntry->Type.Callback.pvUser      = NULL;
                break;
            case DBGFFLOWTRACEPROBEENTRYTYPE_DEBUGGER:
                break;
            default:
                AssertFailed();
        }
    }
}


/**
 * Destroys the given flow trace probe freeing all allocated resources.
 *
 * @param   pProbe                  The flow trace probe instance data.
 */
static void dbgfR3FlowTraceProbeDestroy(PDBGFFLOWTRACEPROBEINT pProbe)
{
    dbgfR3ProbeEntryCleanup(pProbe, 0, pProbe->cEntries);
    MMR3HeapFree(pProbe->paEntries);
    MMR3HeapFree(pProbe);
}


/**
 * Ensures that the given probe has the given amount of additional entries available,
 * increasing the size if necessary.
 *
 * @returns VBox status code.
 * @retval  VERR_NO_MEMORY if increasing the size failed due to an out of memory condition.
 * @param   pProbe                  The probe insatnce.
 * @param   cEntriesAdd             Number of additional entries required.
 */
static int dbgfR3ProbeEnsureSize(PDBGFFLOWTRACEPROBEINT pProbe, uint32_t cEntriesAdd)
{
    uint32_t cEntriesNew = pProbe->cEntries + cEntriesAdd;
    int rc = VINF_SUCCESS;

    if (pProbe->cEntriesMax < cEntriesNew)
    {
        PDBGFFLOWTRACEPROBEENTRY paEntriesNew;
        if (!pProbe->cEntriesMax)
            paEntriesNew = (PDBGFFLOWTRACEPROBEENTRY)MMR3HeapAllocZU(pProbe->pUVM, MM_TAG_DBGF_FLOWTRACE,
                                                                     cEntriesNew * sizeof(DBGFFLOWTRACEPROBEENTRY));
        else
            paEntriesNew = (PDBGFFLOWTRACEPROBEENTRY)MMR3HeapRealloc(pProbe->paEntries,
                                                                     cEntriesNew * sizeof(DBGFFLOWTRACEPROBEENTRY));
        if (RT_LIKELY(paEntriesNew))
        {
            pProbe->paEntries   = paEntriesNew;
            pProbe->cEntriesMax = cEntriesNew;
        }
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}


/**
 * Duplicates a probe registry entry.
 * @returns VBox status code.
 * @param   pUVM                    The usermode VM handle.
 * @param   pDst                    Where to copy the entry to.
 * @param   pSrc                    What to copy.
 */
static int dbgfR3ProbeEntryRegDup(PUVM pUVM, PDBGFFLOWTRACEPROBEENTRYREG pDst, PCDBGFFLOWTRACEPROBEENTRYREG pSrc)
{
    int rc = VINF_SUCCESS;

    pDst->enmType = pSrc->enmType;
    pDst->pszName = MMR3HeapStrDupU(pUVM, MM_TAG_DBGF_FLOWTRACE, pSrc->pszName);
    if (!pDst->pszName)
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * Duplicates a given probe entry in the given destination doing a deep copy (strings are duplicated).
 *
 * @returns VBox status code.
 * @param   pUVM                    The usermode VM handle.
 * @param   pDst                    Where to copy the entry to.
 * @param   pSrc                    What to copy.
 */
static int dbgfR3ProbeEntryDup(PUVM pUVM, PDBGFFLOWTRACEPROBEENTRY pDst, PCDBGFFLOWTRACEPROBEENTRY pSrc)
{
    int rc = VINF_SUCCESS;

    pDst->enmType = pSrc->enmType;
    pDst->pszDesc = NULL;
    if (pSrc->pszDesc)
    {
        pDst->pszDesc = MMR3HeapStrDupU(pUVM, MM_TAG_DBGF_FLOWTRACE, pSrc->pszDesc);
        if (!pDst->pszDesc)
            rc = VERR_NO_MEMORY;
    }

    if (RT_SUCCESS(rc))
    {
        switch (pDst->enmType)
        {
            case DBGFFLOWTRACEPROBEENTRYTYPE_REG:
                rc = dbgfR3ProbeEntryRegDup(pUVM, &pDst->Type.Reg, &pSrc->Type.Reg);
                break;
            case DBGFFLOWTRACEPROBEENTRYTYPE_CONST_MEM:
                pDst->Type.ConstMem.AddrMem = pSrc->Type.ConstMem.AddrMem;
                pDst->Type.ConstMem.cbMem   = pSrc->Type.ConstMem.cbMem;
                break;
            case DBGFFLOWTRACEPROBEENTRYTYPE_INDIRECT_MEM:
                pDst->Type.IndirectMem.uScale  = pSrc->Type.IndirectMem.uScale;
                pDst->Type.IndirectMem.cbMem   = pSrc->Type.IndirectMem.cbMem;
                pDst->Type.IndirectMem.iOffset = pSrc->Type.IndirectMem.iOffset;
                rc = dbgfR3ProbeEntryRegDup(pUVM, &pDst->Type.IndirectMem.RegBase, &pSrc->Type.IndirectMem.RegBase);
                if (   RT_SUCCESS(rc)
                    && pDst->Type.IndirectMem.RegIndex.pszName)
                {
                    rc = dbgfR3ProbeEntryRegDup(pUVM, &pDst->Type.IndirectMem.RegIndex, &pSrc->Type.IndirectMem.RegIndex);
                    if (RT_FAILURE(rc))
                        MMR3HeapFree((void *)pDst->Type.IndirectMem.RegBase.pszName);
                }
                break;
            case DBGFFLOWTRACEPROBEENTRYTYPE_CALLBACK:
                pDst->Type.Callback.pfnCallback = pSrc->Type.Callback.pfnCallback;
                pDst->Type.Callback.pvUser      = pSrc->Type.Callback.pvUser;
                break;
            case DBGFFLOWTRACEPROBEENTRYTYPE_DEBUGGER:
                break;
            default:
                rc = VERR_INVALID_PARAMETER;
        }
    }

    if (   RT_FAILURE(rc)
        && pDst->pszDesc)
    {
        MMR3HeapFree((void *)pDst->pszDesc);
        pDst->pszDesc = NULL;
    }

    return rc;
}


/**
 * Recalculates the size occupied by the data of this probe for each invocation.
 *
 * @param   pProbe                  The probe instance.
 */
static void dbgfR3ProbeRecalcSize(PDBGFFLOWTRACEPROBEINT pProbe)
{
    size_t cbProbe = 0;

    for (uint32_t i = 0; i < pProbe->cEntries; i++)
    {
        PDBGFFLOWTRACEPROBEENTRY pEntry = &pProbe->paEntries[i];

        cbProbe += sizeof(DBGFFLOWTRACEPROBEVAL);

        switch (pEntry->enmType)
        {
            case DBGFFLOWTRACEPROBEENTRYTYPE_CONST_MEM:
                cbProbe += pEntry->Type.ConstMem.cbMem;
                break;
            case DBGFFLOWTRACEPROBEENTRYTYPE_INDIRECT_MEM:
                cbProbe += pEntry->Type.IndirectMem.cbMem;
                break;
            case DBGFFLOWTRACEPROBEENTRYTYPE_CALLBACK:
            case DBGFFLOWTRACEPROBEENTRYTYPE_REG:
            case DBGFFLOWTRACEPROBEENTRYTYPE_DEBUGGER:
                break;
            default:
                AssertFailed();
        }
    }

    pProbe->cbProbe = cbProbe;
}


/**
 * Creates a new empty flow trace module.
 *
 * @returns VBox status code.
 * @param   pUVM                    The usermode VM handle.
 * @param   idCpu                   CPU ID the module is for, use VMCPUID_ANY for any CPU.
 * @param   hFlowTraceProbeCommon   Optional probe handle of data to capture regardless of the actual
 *                                  probe.
 * @param   phFlowTraceMod          Where to store the handle to the created module on success.
 */
VMMR3DECL(int) DBGFR3FlowTraceModCreate(PUVM pUVM, VMCPUID idCpu,
                                        DBGFFLOWTRACEPROBE hFlowTraceProbeCommon,
                                        PDBGFFLOWTRACEMOD phFlowTraceMod)
{
    int rc = VINF_SUCCESS;
    PDBGFFLOWTRACEMODINT pThis = (PDBGFFLOWTRACEMODINT)MMR3HeapAllocZU(pUVM, MM_TAG_DBGF_FLOWTRACE,
                                                                       sizeof(DBGFFLOWTRACEMODINT));
    if (RT_LIKELY(pThis))
    {
        pThis->cRefs        = 1;
        pThis->pUVM         = pUVM;
        pThis->idCpu        = idCpu;
        pThis->enmState     = DBGFFLOWTRACEMODSTATE_CREATED;
        pThis->u64SeqNoNext = 0;
        pThis->cHitsLeft    = 0;
        pThis->cRecordsMax  = 0;
        pThis->cRecords     = 0;
        pThis->cProbes      = 0;
        RTListInit(&pThis->LstProbes);
        RTListInit(&pThis->LstRecords);

        rc = RTSemFastMutexCreate(&pThis->hMtx);
        if (RT_SUCCESS(rc))
        {
            rc = DBGFR3BpOwnerCreate(pUVM, dbgfR3FlowTraceModProbeFiredWorker, NULL /*pfnBpIoHit*/, &pThis->hBpOwner);
            if (RT_SUCCESS(rc))
            {
                PDBGFFLOWTRACEPROBEINT pProbe = hFlowTraceProbeCommon;
                if (pProbe)
                {
                    DBGFR3FlowTraceProbeRetain(pProbe);
                    pThis->pProbeCmn = pProbe;
                }
            }

            *phFlowTraceMod = pThis;
        }

        if (RT_FAILURE(rc))
            MMR3HeapFree(pThis);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * Create a new flow trace module from the given control flow graph adding the given probes
 * at the entries, exits and branches.
 *
 * @returns VBox status code.
 * @param   pUVM                    The usermode VM handle.
 * @param   idCpu                   CPU ID the module is for, use VMCPUID_ANY for any CPU.
 * @param   hFlow                   Control flow graph handle to use.
 * @param   hFlowTraceProbeCommon   Optional probe handle of data to capture regardless of the actual
 *                                  probe.
 * @param   hFlowTraceProbeEntry    Probe handle to use for all entry blocks.
 * @param   hFlowTraceProbeRegular  Probe handle to use for all branches.
 * @param   hFlowTraceProbeExit     Probe handle to use for all exits.
 * @param   phFlowTraceMod          Where to store the handle to the created module on success.
 */
VMMR3DECL(int) DBGFR3FlowTraceModCreateFromFlowGraph(PUVM pUVM, VMCPUID idCpu, DBGFFLOW hFlow,
                                                     DBGFFLOWTRACEPROBE hFlowTraceProbeCommon,
                                                     DBGFFLOWTRACEPROBE hFlowTraceProbeEntry,
                                                     DBGFFLOWTRACEPROBE hFlowTraceProbeRegular,
                                                     DBGFFLOWTRACEPROBE hFlowTraceProbeExit,
                                                     PDBGFFLOWTRACEMOD phFlowTraceMod)
{
    DBGFFLOWIT hFlowIt;
    int rc = DBGFR3FlowItCreate(hFlow, DBGFFLOWITORDER_BY_ADDR_LOWEST_FIRST, &hFlowIt);
    if (RT_SUCCESS(rc))
    {
        DBGFFLOWTRACEMOD hFlowTraceMod;
        rc = DBGFR3FlowTraceModCreate(pUVM, idCpu, hFlowTraceProbeCommon, &hFlowTraceMod);
        if (RT_SUCCESS(rc))
        {
            DBGFFLOWBB hFlowBb = DBGFR3FlowItNext(hFlowIt);
            while (hFlowBb && RT_SUCCESS(rc))
            {
                uint32_t fFlags = DBGFR3FlowBbGetFlags(hFlowBb);

                if (!(fFlags & (DBGF_FLOW_BB_F_EMPTY | DBGF_FLOW_BB_F_INCOMPLETE_ERR)))
                {
                    DBGFADDRESS AddrInstr;

                    if (fFlags & DBGF_FLOW_BB_F_ENTRY)
                    {
                        rc = DBGFR3FlowBbQueryInstr(hFlowBb, 0, &AddrInstr, NULL, NULL);
                        AssertRC(rc);

                        rc = DBGFR3FlowTraceModAddProbe(hFlowTraceMod, &AddrInstr, hFlowTraceProbeEntry,
                                                        DBGF_FLOW_TRACE_PROBE_ADD_F_BEFORE_EXEC);
                    }
                    else
                    {
                        DBGFFLOWBBENDTYPE enmType = DBGFR3FlowBbGetType(hFlowBb);
                        uint32_t cInstr = enmType == DBGFFLOWBBENDTYPE_EXIT ? DBGFR3FlowBbGetInstrCount(hFlowBb) - 1 : 0;
                        rc = DBGFR3FlowBbQueryInstr(hFlowBb, cInstr, &AddrInstr, NULL, NULL);
                        if (RT_SUCCESS(rc))
                        {
                            if (enmType == DBGFFLOWBBENDTYPE_EXIT)
                                rc = DBGFR3FlowTraceModAddProbe(hFlowTraceMod, &AddrInstr, hFlowTraceProbeExit,
                                                                DBGF_FLOW_TRACE_PROBE_ADD_F_AFTER_EXEC);
                            else
                                rc = DBGFR3FlowTraceModAddProbe(hFlowTraceMod, &AddrInstr, hFlowTraceProbeRegular,
                                                                DBGF_FLOW_TRACE_PROBE_ADD_F_BEFORE_EXEC);
                        }
                    }
                }

                hFlowBb = DBGFR3FlowItNext(hFlowIt);
            }

            if (RT_FAILURE(rc))
                DBGFR3FlowTraceModRelease(hFlowTraceMod);
            else
                *phFlowTraceMod = hFlowTraceMod;
        }

        DBGFR3FlowItDestroy(hFlowIt);
    }

    return rc;
}


/**
 * Retain a reference to the given flow trace module.
 *
 * @returns New reference count.
 * @param   hFlowTraceMod           Flow trace module handle.
 */
VMMR3DECL(uint32_t) DBGFR3FlowTraceModRetain(DBGFFLOWTRACEMOD hFlowTraceMod)
{
    PDBGFFLOWTRACEMODINT pThis = hFlowTraceMod;
    AssertPtrReturn(pThis, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M, ("%#x %p\n", cRefs, pThis));
    return cRefs;
}


/**
 * Release a reference of the given flow trace module.
 *
 * @returns New reference count, on 0 the module is destroyed and all containing records
 *          are deleted.
 * @param   hFlowTraceMod           Flow trace module handle.
 */
VMMR3DECL(uint32_t) DBGFR3FlowTraceModRelease(DBGFFLOWTRACEMOD hFlowTraceMod)
{
    PDBGFFLOWTRACEMODINT pThis = hFlowTraceMod;
    if (!pThis)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    AssertMsg(cRefs < _1M, ("%#x %p\n", cRefs, pThis));
    if (cRefs == 0)
        dbgfR3FlowTraceModDestroy(pThis);
    return cRefs;
}


/**
 * Enables and arms all probes in the given flow trace module.
 *
 * @returns VBox status code.
 * @param   hFlowTraceMod           Flow trace module handle.
 * @param   cHits                   Number of hits inside this module until the module is disabled
 *                                  automatically, 0 if not to disable automatically.
 * @param   cRecordsMax             Maximum number of records to keep until the oldest is evicted.
 */
VMMR3DECL(int) DBGFR3FlowTraceModEnable(DBGFFLOWTRACEMOD hFlowTraceMod, uint32_t cHits, uint32_t cRecordsMax)
{
    PDBGFFLOWTRACEMODINT pThis = hFlowTraceMod;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->cProbes > 0, VERR_INVALID_STATE);
    AssertReturn(pThis->enmState == DBGFFLOWTRACEMODSTATE_CREATED, VERR_INVALID_STATE);

    pThis->cHitsLeft   = cHits;
    pThis->cRecordsMax = cRecordsMax;

    return VMMR3EmtRendezvous(pThis->pUVM->pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ONCE,
                              dbgfR3FlowTraceModEnableWorker, pThis);
}


/**
 * Disables all probes in the given flow trace module.
 *
 * @returns VBox status code.
 * @param   hFlowTraceMod           Flow trace module handle.
 */
VMMR3DECL(int) DBGFR3FlowTraceModDisable(DBGFFLOWTRACEMOD hFlowTraceMod)
{
    PDBGFFLOWTRACEMODINT pThis = hFlowTraceMod;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->enmState == DBGFFLOWTRACEMODSTATE_ENABLED, VERR_INVALID_STATE);

    return VMMR3EmtRendezvous(pThis->pUVM->pVM, VMMEMTRENDEZVOUS_FLAGS_TYPE_ONCE,
                              dbgfR3FlowTraceModDisableWorker, pThis);
}


/**
 * Returns a report containing all existing records in the given flow trace module.
 *
 * @returns VBox status code.
 * @param   hFlowTraceMod           Flow trace module handle.
 * @param   phFlowTraceReport       Where to store the flow trace report handle on success.
 */
VMMR3DECL(int) DBGFR3FlowTraceModQueryReport(DBGFFLOWTRACEMOD hFlowTraceMod,
                                             PDBGFFLOWTRACEREPORT phFlowTraceReport)
{
    PDBGFFLOWTRACEMODINT pThis = hFlowTraceMod;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(phFlowTraceReport, VERR_INVALID_POINTER);

    /** @todo Locking. */
    int rc = VINF_SUCCESS;
    PDBGFFLOWTRACEREPORTINT pReport = dbgfR3FlowTraceReportCreate(pThis->pUVM, pThis->cRecords);
    if (RT_LIKELY(pReport))
    {
        PDBGFFLOWTRACERECORDINT pIt;
        uint32_t idx = 0;

        RTSemFastMutexRequest(pThis->hMtx);
        RTListForEach(&pThis->LstRecords, pIt, DBGFFLOWTRACERECORDINT, NdRecord)
        {
            DBGFR3FlowTraceRecordRetain(pIt);
            pReport->apRec[idx++] = pIt;
        }
        RTSemFastMutexRelease(pThis->hMtx);

        *phFlowTraceReport = pReport;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * Clears all records contained in the flow trace module.
 *
 * @returns VBox status code.
 * @param   hFlowTraceMod           Flow trace module handle.
 */
VMMR3DECL(int) DBGFR3FlowTraceModClear(DBGFFLOWTRACEMOD hFlowTraceMod)
{
    PDBGFFLOWTRACEMODINT pThis = hFlowTraceMod;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);

    RTSemFastMutexRequest(pThis->hMtx);
    RTLISTANCHOR LstTmp;
    RTListMove(&LstTmp, &pThis->LstRecords);
    ASMAtomicWriteU32(&pThis->cRecords, 0);
    RTSemFastMutexRelease(pThis->hMtx);

    PDBGFFLOWTRACERECORDINT pIt, pItNext;
    RTListForEachSafe(&LstTmp, pIt, pItNext, DBGFFLOWTRACERECORDINT, NdRecord)
    {
        RTListNodeRemove(&pIt->NdRecord);
        DBGFR3FlowTraceRecordRelease(pIt);
    }

    return VINF_SUCCESS;
}


/**
 * Adds a new probe to the given flow trace module.
 *
 * @returns VBox status code
 * @retval VERR_INVALID_STATE if the probe is active or was destroyed already.
 * @retval VERR_ALREADY_EXISTS if there is already a probe at the specified location.
 * @param   hFlowTraceMod           Flow trace module handle.
 * @param   pAddrProbe              Guest address to insert the probe at.
 * @param   hFlowTraceProbe         The handle of the probe to insert.
 * @param   fFlags                  Combination of DBGF_FLOW_TRACE_PROBE_ADD_F_*.
 */
VMMR3DECL(int) DBGFR3FlowTraceModAddProbe(DBGFFLOWTRACEMOD hFlowTraceMod, PCDBGFADDRESS pAddrProbe,
                                          DBGFFLOWTRACEPROBE hFlowTraceProbe, uint32_t fFlags)
{
    PDBGFFLOWTRACEMODINT pThis = hFlowTraceMod;
    PDBGFFLOWTRACEPROBEINT pProbe = hFlowTraceProbe;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertPtrReturn(pProbe, VERR_INVALID_HANDLE);
    AssertPtrReturn(pAddrProbe, VERR_INVALID_POINTER);
    AssertReturn(!(fFlags & ~DBGF_FLOW_TRACE_PROBE_ADD_F_VALID_MASK), VERR_INVALID_PARAMETER);
    AssertReturn(pThis->enmState == DBGFFLOWTRACEMODSTATE_CREATED, VERR_INVALID_STATE);

    int rc = VINF_SUCCESS;
    PDBGFFLOWTRACEMODPROBELOC pProbeLoc = dbgfR3TraceModGetProbeLocAtAddr(pThis, pAddrProbe);
    if (!pProbeLoc)
    {
        pProbeLoc = (PDBGFFLOWTRACEMODPROBELOC)MMR3HeapAllocZU(pThis->pUVM, MM_TAG_DBGF_FLOWTRACE,
                                                               sizeof(DBGFFLOWTRACEMODPROBELOC));
        if (RT_LIKELY(pProbeLoc))
        {
            pProbeLoc->pTraceMod = pThis;
            pProbeLoc->pProbe    = pProbe;
            pProbeLoc->AddrProbe = *pAddrProbe;
            pProbeLoc->fFlags    = fFlags;
            ASMAtomicIncU32(&pProbe->cRefs);
            ASMAtomicIncU32(&pProbe->cRefsMod);
            RTSemFastMutexRequest(pThis->hMtx);
            RTListAppend(&pThis->LstProbes, &pProbeLoc->NdProbes);
            pThis->cProbes++;
            RTSemFastMutexRelease(pThis->hMtx);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_ALREADY_EXISTS;

    return rc;
}


/**
 * Creates a new empty probe.
 *
 * @returns VBox status code.
 * @param   pUVM                    The usermode VM handle.
 * @param   pszDescr                Description of the probe, optional.
 * @param   phFlowTraceProbe        Where to store the probe handle on success.
 */
VMMR3DECL(int) DBGFR3FlowTraceProbeCreate(PUVM pUVM, const char *pszDescr, PDBGFFLOWTRACEPROBE phFlowTraceProbe)
{
    int rc = VINF_SUCCESS;
    PDBGFFLOWTRACEPROBEINT pProbe = (PDBGFFLOWTRACEPROBEINT)MMR3HeapAllocZU(pUVM, MM_TAG_DBGF_FLOWTRACE,
                                                                            sizeof(DBGFFLOWTRACEPROBEINT));
    if (RT_LIKELY(pProbe))
    {
        pProbe->cRefs       = 1;
        pProbe->cRefsMod    = 0;
        pProbe->pUVM        = pUVM;
        pProbe->cbProbe     = 0;
        pProbe->cEntries    = 0;
        pProbe->cEntriesMax = 0;
        pProbe->paEntries   = NULL;
        pProbe->pszDescr    = NULL;
        if (pszDescr)
        {
            pProbe->pszDescr = MMR3HeapStrDupU(pUVM, MM_TAG_DBGF_FLOWTRACE, pszDescr);
            if (!pProbe->pszDescr)
            {
                MMR3HeapFree(pProbe);
                rc = VERR_NO_MEMORY;
            }
        }

        if (RT_SUCCESS(rc))
            *phFlowTraceProbe = pProbe;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * Retains a reference to the probe.
 *
 * @returns New reference count.
 * @param   hFlowTraceProbe         Flow trace probe handle.
 */
VMMR3DECL(uint32_t) DBGFR3FlowTraceProbeRetain(DBGFFLOWTRACEPROBE hFlowTraceProbe)
{
    PDBGFFLOWTRACEPROBEINT pProbe = hFlowTraceProbe;
    AssertPtrReturn(pProbe, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pProbe->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M, ("%#x %p\n", cRefs, pProbe));
    return cRefs;
}


/**
 * Release a probe reference.
 *
 * @returns New reference count, on 0 the probe is destroyed.
 * @param   hFlowTraceProbe         Flow trace probe handle.
 */
VMMR3DECL(uint32_t) DBGFR3FlowTraceProbeRelease(DBGFFLOWTRACEPROBE hFlowTraceProbe)
{
    PDBGFFLOWTRACEPROBEINT pProbe = hFlowTraceProbe;
    if (!pProbe)
        return 0;
    AssertPtrReturn(pProbe, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pProbe->cRefs);
    AssertMsg(cRefs < _1M, ("%#x %p\n", cRefs, pProbe));
    if (cRefs == 0)
        dbgfR3FlowTraceProbeDestroy(pProbe);
    return cRefs;
}


/**
 * Adds new data to log in the given probe.
 *
 * @returns VBox status code.
 * @retval  VERR_INVALID_STATE if the probe is already part of a trace module and it is not
 *          possible to add new entries at this point.
 * @param   hFlowTraceProbe         Flow trace probe handle.
 * @param   paEntries               Pointer to the array of entry descriptors.
 * @param   cEntries                Number of entries in the array.
 */
VMMR3DECL(int) DBGFR3FlowTraceProbeEntriesAdd(DBGFFLOWTRACEPROBE hFlowTraceProbe,
                                              PCDBGFFLOWTRACEPROBEENTRY paEntries, uint32_t cEntries)
{
    PDBGFFLOWTRACEPROBEINT pProbe = hFlowTraceProbe;
    AssertPtrReturn(pProbe, VERR_INVALID_HANDLE);
    AssertPtrReturn(paEntries, VERR_INVALID_POINTER);
    AssertReturn(cEntries > 0, VERR_INVALID_PARAMETER);
    AssertReturn(!pProbe->cRefsMod, VERR_INVALID_STATE);

    int rc = dbgfR3ProbeEnsureSize(pProbe, cEntries);
    if (RT_SUCCESS(rc))
    {
        uint32_t idxEntry;

        for (idxEntry = 0; idxEntry < cEntries && RT_SUCCESS(rc); idxEntry++)
        {
            PCDBGFFLOWTRACEPROBEENTRY pEntry = &paEntries[idxEntry];
            PDBGFFLOWTRACEPROBEENTRY pProbeEntry = &pProbe->paEntries[pProbe->cEntries + idxEntry];

            rc = dbgfR3ProbeEntryDup(pProbe->pUVM, pProbeEntry, pEntry);
        }

        if (RT_FAILURE(rc))
            dbgfR3ProbeEntryCleanup(pProbe, pProbe->cEntries, idxEntry + 1);
        else
        {
            pProbe->cEntries += cEntries;
            dbgfR3ProbeRecalcSize(pProbe);
        }
    }

    return rc;
}


/**
 * Retains a reference to the given flow trace report.
 *
 * @returns New reference count.
 * @param   hFlowTraceReport        Flow trace report handle.
 */
VMMR3DECL(uint32_t) DBGFR3FlowTraceReportRetain(DBGFFLOWTRACEREPORT hFlowTraceReport)
{
    PDBGFFLOWTRACEREPORTINT pReport = hFlowTraceReport;
    AssertPtrReturn(pReport, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pReport->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M, ("%#x %p\n", cRefs, pReport));
    return cRefs;
}


/**
 * Releases a reference of the given flow trace report.
 *
 * @returns New reference count, on 0 the report is destroyed.
 * @param   hFlowTraceReport        Flow trace report handle.
 */
VMMR3DECL(uint32_t) DBGFR3FlowTraceReportRelease(DBGFFLOWTRACEREPORT hFlowTraceReport)
{
    PDBGFFLOWTRACEREPORTINT pReport = hFlowTraceReport;
    if (!pReport)
        return 0;
    AssertPtrReturn(pReport, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pReport->cRefs);
    AssertMsg(cRefs < _1M, ("%#x %p\n", cRefs, pReport));
    if (cRefs == 0)
        dbgfR3FlowTraceReportDestroy(pReport);
    return cRefs;
}


/**
 * Returns the number of records in the given report.
 *
 * @returns Number of records.
 * @param   hFlowTraceReport        Flow trace report handle.
 */
VMMR3DECL(uint32_t) DBGFR3FlowTraceReportGetRecordCount(DBGFFLOWTRACEREPORT hFlowTraceReport)
{
    PDBGFFLOWTRACEREPORTINT pReport = hFlowTraceReport;
    AssertPtrReturn(pReport, 0);

    return pReport->cRecords;
}


/**
 * Queries the specified record contained in the given report.
 *
 * @returns VBox status code.
 * @param   hFlowTraceReport        Flow trace report handle.
 * @param   idxRec                  The record index to query.
 * @param   phFlowTraceRec          Where to store the retained handle of the record on success.
 */
VMMR3DECL(int) DBGFR3FlowTraceReportQueryRecord(DBGFFLOWTRACEREPORT hFlowTraceReport, uint32_t idxRec, PDBGFFLOWTRACERECORD phFlowTraceRec)
{
    PDBGFFLOWTRACEREPORTINT pReport = hFlowTraceReport;
    AssertPtrReturn(pReport, 0);
    AssertPtrReturn(phFlowTraceRec, VERR_INVALID_POINTER);
    AssertReturn(idxRec < pReport->cRecords, VERR_INVALID_PARAMETER);

    DBGFR3FlowTraceRecordRetain(pReport->apRec[idxRec]);
    *phFlowTraceRec = pReport->apRec[idxRec];
    return VINF_SUCCESS;
}


/**
 * Filters the given flow trace report by the given criterias and returns a filtered report.
 *
 * @returns VBox status code.
 * @param   hFlowTraceReport          Flow trace report handle.
 * @param   fFlags                    Combination of DBGF_FLOW_TRACE_REPORT_FILTER_F_*.
 * @param   paFilters                 Pointer to the array of filters.
 * @param   cFilters                  Number of entries in the filter array.
 * @param   enmOp                     How the filters are connected to each other.
 * @param   phFlowTraceReportFiltered Where to return the handle to the report containing the
 *                                    filtered records on success.
 */
VMMR3DECL(int) DBGFR3FlowTraceReportQueryFiltered(DBGFFLOWTRACEREPORT hFlowTraceReport, uint32_t fFlags,
                                                  PDBGFFLOWTRACEREPORTFILTER paFilters, uint32_t cFilters,
                                                  DBGFFLOWTRACEREPORTFILTEROP enmOp,
                                                  PDBGFFLOWTRACEREPORT phFlowTraceReportFiltered)
{
    PDBGFFLOWTRACEREPORTINT pReport = hFlowTraceReport;
    AssertPtrReturn(pReport, VERR_INVALID_HANDLE);
    AssertReturn(!(fFlags & DBGF_FLOW_TRACE_REPORT_FILTER_F_VALID), VERR_INVALID_PARAMETER);
    AssertPtrReturn(paFilters, VERR_INVALID_POINTER);
    AssertReturn(cFilters > 0, VERR_INVALID_PARAMETER);
    AssertReturn(enmOp > DBGFFLOWTRACEREPORTFILTEROP_INVALID && enmOp <= DBGFFLOWTRACEREPORTFILTEROP_OR,
                 VERR_INVALID_PARAMETER);
    AssertPtrReturn(phFlowTraceReportFiltered, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    PDBGFFLOWTRACEREPORTINT pReportFiltered = dbgfR3FlowTraceReportCreate(pReport->pUVM, pReport->cRecords);
    if (RT_LIKELY(pReportFiltered))
    {
        uint32_t idxFiltered = 0;

        for (uint32_t i = 0; i < pReport->cRecords; i++)
        {
            PDBGFFLOWTRACERECORDINT pCur = pReport->apRec[i];
            bool fRecFilterMatch = dbgfR3FlowTraceDoesRecordMatchFilter(pCur, paFilters, cFilters, enmOp);

            if (   (   fRecFilterMatch
                    && !(fFlags & DBGF_FLOW_TRACE_REPORT_FILTER_F_REVERSE))
                || (   !fRecFilterMatch
                    && (fFlags & DBGF_FLOW_TRACE_REPORT_FILTER_F_REVERSE)))
            {
                DBGFR3FlowTraceRecordRetain(pCur);
                pReportFiltered->apRec[idxFiltered++] = pCur;
            }
        }

        pReportFiltered->cRecords = idxFiltered;
        *phFlowTraceReportFiltered = pReportFiltered;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * Enumerates all records in the given flow trace report calling the supplied
 * enumeration callback.
 *
 * @returns VBox status code, return value of pfnEnum on error.
 * @param   hFlowTraceReport          Flow trace report handle.
 * @param   pfnEnum                   The callback to call for every record.
 * @param   pvUser                    Opaque user data to pass to the callback.
 */
VMMR3DECL(int) DBGFR3FlowTraceReportEnumRecords(DBGFFLOWTRACEREPORT hFlowTraceReport,
                                                PFNDBGFFLOWTRACEREPORTENUMCLBK pfnEnum,
                                                void *pvUser)
{
    PDBGFFLOWTRACEREPORTINT pReport = hFlowTraceReport;
    AssertPtrReturn(pReport, VERR_INVALID_HANDLE);

    int rc = VINF_SUCCESS;
    for (uint32_t i = 0; i < pReport->cRecords && RT_SUCCESS(rc); i++)
        rc = pfnEnum(pReport, pReport->apRec[i], pvUser);

    return rc;
}


/**
 * Retains a reference to the given flow trace record handle.
 *
 * @returns New reference count.
 * @param   hFlowTraceRecord        The record handle to retain.
 */
VMMR3DECL(uint32_t) DBGFR3FlowTraceRecordRetain(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    PDBGFFLOWTRACERECORDINT pRecord = hFlowTraceRecord;
    AssertPtrReturn(pRecord, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pRecord->cRefs);
    AssertMsg(cRefs > 1 && cRefs < _1M, ("%#x %p\n", cRefs, pRecord));
    return cRefs;
}


/**
 * Releases a reference of the given flow trace record.
 *
 * @returns New reference count, on 0 the record is destroyed.
 * @param   hFlowTraceRecord        Flow trace record handle.
 */
VMMR3DECL(uint32_t) DBGFR3FlowTraceRecordRelease(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    PDBGFFLOWTRACERECORDINT pRecord = hFlowTraceRecord;
    if (!pRecord)
        return 0;
    AssertPtrReturn(pRecord, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pRecord->cRefs);
    AssertMsg(cRefs < _1M, ("%#x %p\n", cRefs, pRecord));
    if (cRefs == 0)
        dbgfR3FlowTraceRecordDestroy(pRecord);
    return cRefs;
}


/**
 * Gets the sequence number of the given record handle.
 *
 * @returns Sequence number.
 * @param   hFlowTraceRecord        Flow trace record handle.
 */
VMMR3DECL(uint64_t) DBGFR3FlowTraceRecordGetSeqNo(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    PDBGFFLOWTRACERECORDINT pRecord = hFlowTraceRecord;
    AssertPtrReturn(pRecord, 0);

    return pRecord->u64SeqNo;
}


/**
 * Returns the timestamp when the record was created.
 *
 * @returns Timestamp in nano seconds.
 * @param   hFlowTraceRecord        Flow trace record handle.
 */
VMMR3DECL(uint64_t) DBGFR3FlowTraceRecordGetTimestamp(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    PDBGFFLOWTRACERECORDINT pRecord = hFlowTraceRecord;
    AssertPtrReturn(pRecord, 0);

    return pRecord->u64TsCollected;
}


/**
 * Gets the address in the guest the record was created.
 *
 * @returns Pointer to the address containing the guest location the record was created at.
 * @param   hFlowTraceRecord        Flow trace record handle.
 * @param   pAddr                   Where to store the guest address.
 */
VMMR3DECL(PDBGFADDRESS) DBGFR3FlowTraceRecordGetAddr(DBGFFLOWTRACERECORD hFlowTraceRecord, PDBGFADDRESS pAddr)
{
    PDBGFFLOWTRACERECORDINT pRecord = hFlowTraceRecord;
    AssertPtrReturn(pRecord, NULL);
    AssertPtrReturn(pAddr, NULL);

    *pAddr = pRecord->AddrProbe;
    return pAddr;
}


/**
 * Returns the handle to the probe for the given record.
 *
 * @returns Handle to the probe.
 * @param   hFlowTraceRecord        Flow trace record handle.
 */
VMMR3DECL(DBGFFLOWTRACEPROBE) DBGFR3FlowTraceRecordGetProbe(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    PDBGFFLOWTRACERECORDINT pRecord = hFlowTraceRecord;
    AssertPtrReturn(pRecord, NULL);

    DBGFR3FlowTraceProbeRetain(pRecord->pProbe);
    return pRecord->pProbe;
}


/**
 * Returns the number of values contained in the record.
 *
 * @returns Number of values in the record.
 * @param   hFlowTraceRecord        Flow trace record handle.
 */
VMMR3DECL(uint32_t) DBGFR3FlowTraceRecordGetValCount(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    PDBGFFLOWTRACERECORDINT pRecord = hFlowTraceRecord;
    AssertPtrReturn(pRecord, 0);

    return pRecord->pProbe->cEntries;
}


/**
 * Returns the number of values contained in the record.
 *
 * @returns Number of values in the record.
 * @param   hFlowTraceRecord        Flow trace record handle.
 */
VMMR3DECL(uint32_t) DBGFR3FlowTraceRecordGetValCommonCount(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    PDBGFFLOWTRACERECORDINT pRecord = hFlowTraceRecord;
    AssertPtrReturn(pRecord, 0);

    return pRecord->pProbeCmn ? pRecord->pProbeCmn->cEntries : 0;
}


/**
 * Returns the values for the given record.
 *
 * @returns Pointer to the array of values.
 * @param   hFlowTraceRecord        Flow trace record handle.
 */
VMMR3DECL(PCDBGFFLOWTRACEPROBEVAL) DBGFR3FlowTraceRecordGetVals(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    PDBGFFLOWTRACERECORDINT pRecord = hFlowTraceRecord;
    AssertPtrReturn(pRecord, NULL);

    return &pRecord->aVal[0];
}


/**
 * Returns data collected by the common probe for the trace module this record is in if one
 * is active.
 *
 * @returns Pointer to the array of common probe values or NULL if no common probe was specified
 *          for the trace module.
 * @param   hFlowTraceRecord        Flow trace record handle.
 */
VMMR3DECL(PCDBGFFLOWTRACEPROBEVAL) DBGFR3FlowTraceRecordGetValsCommon(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    PDBGFFLOWTRACERECORDINT pRecord = hFlowTraceRecord;
    AssertPtrReturn(pRecord, NULL);

    return pRecord->paValCmn;
}


/**
 * Returns the vCPU ID the record was created on.
 *
 * @returns vCPU ID.
 * @param   hFlowTraceRecord        Flow trace record handle.
 */
VMMR3DECL(VMCPUID) DBGFR3FlowTraceRecordGetCpuId(DBGFFLOWTRACERECORD hFlowTraceRecord)
{
    PDBGFFLOWTRACERECORDINT pRecord = hFlowTraceRecord;
    AssertPtrReturn(pRecord, VMCPUID_ANY);

    return pRecord->idCpu;
}

