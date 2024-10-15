/* $Id: DBGFR3Type.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, Type Management.
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


/** @page pg_dbgf_type    DBGFType - Type Management
 *
 * The type management system is intended to ease retrieval of values from
 * structures in the guest OS without having to take care of the size of pointers.
 *
 * @todo r=bird: We need to join this up with modules and address spaces.  It
 *       cannot be standalone like this.  Also, it must be comming from IPRT as
 *       there is no point in duplicating code (been there, done that with
 *       symbols and debug info already).  This unfortunately means we need to
 *       find some common way of abstracting DWARF and Codeview type info so we
 *       can extend those debug info parsers to make type information available.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DBGF
#include <VBox/vmm/dbgf.h>
#include "DBGFInternal.h"
#include <VBox/vmm/mm.h>
#include <VBox/vmm/uvm.h>
#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/assert.h>
#include <iprt/thread.h>
#include <iprt/param.h>


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/

/** Locks the type database for writing. */
#define DBGF_TYPE_DB_LOCK_WRITE(pUVM) \
    do { \
        int rcSem = RTSemRWRequestWrite((pUVM)->dbgf.s.hTypeDbLock, RT_INDEFINITE_WAIT); \
        AssertRC(rcSem); \
    } while (0)

/** Unlocks the type database after writing. */
#define DBGF_TYPE_DB_UNLOCK_WRITE(pUVM) \
    do { \
        int rcSem = RTSemRWReleaseWrite((pUVM)->dbgf.s.hTypeDbLock); \
        AssertRC(rcSem); \
    } while (0)

/** Locks the type database for reading. */
#define DBGF_TYPE_DB_LOCK_READ(pUVM) \
    do { \
        int rcSem = RTSemRWRequestRead((pUVM)->dbgf.s.hTypeDbLock, RT_INDEFINITE_WAIT); \
        AssertRC(rcSem); \
    } while (0)

/** Unlocks the type database after reading. */
#define DBGF_TYPE_DB_UNLOCK_READ(pUVM) \
    do { \
        int rcSem = RTSemRWReleaseRead((pUVM)->dbgf.s.hTypeDbLock); \
        AssertRC(rcSem); \
    } while (0)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * DBGF registered type.
 */
typedef struct DBGFTYPE
{
    /** String space core. */
    RTSTRSPACECORE          Core;
    /** Pointer to the registration structure, NULL means builtin type. */
    PCDBGFTYPEREG           pReg;
    /** How often the type is referenced by other types. */
    volatile uint32_t       cRefs;
    /** Size of the type. */
    size_t                  cbType;
    /** Builtin type if pReg is NULL (otherwise it is invalid). */
    DBGFTYPEBUILTIN         enmTypeBuiltin;
} DBGFTYPE;
/** Pointer to a DBGF type. */
typedef DBGFTYPE *PDBGFTYPE;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int dbgfR3TypeParseBufferByType(PUVM pUVM, PDBGFTYPE pType, uint8_t *pbBuf, size_t cbBuf,
                                       PDBGFTYPEVAL *ppVal, size_t *pcbParsed);


/**
 * Looks up a type by the identifier.
 *
 * @returns Pointer to the type structure on success, NULL otherwise.
 * @param   pUVM                The user mode VM handle.
 * @param   pszType             The type identifier.
 */
static PDBGFTYPE dbgfR3TypeLookup(PUVM pUVM, const char *pszType)
{
    PRTSTRSPACE pTypeSpace = &pUVM->dbgf.s.TypeSpace;
    return (PDBGFTYPE)RTStrSpaceGet(pTypeSpace, pszType);
}


/**
 * Calculate the size of the given type.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pType               The type to calculate the size for.
 * @param   fCalcNested         Flag whether to calculate the size for nested
 *                              structs if the sizes are 0.
 */
static int dbgfR3TypeCalcSize(PUVM pUVM, PDBGFTYPE pType, bool fCalcNested)
{
    int rc = VINF_SUCCESS;

    /* Builtin types are never recalculated. */
    if (pType->pReg)
    {
        switch (pType->pReg->enmVariant)
        {
            case DBGFTYPEVARIANT_STRUCT:
            {
                size_t cbType = 0;

                /* Go through the members and update size. */
                for (uint32_t i = 0; i < pType->pReg->cMembers && RT_SUCCESS(rc); i++)
                {
                    PCDBGFTYPEREGMEMBER pMember = &pType->pReg->paMembers[i];

                    if (pMember->fFlags & DBGFTYPEREGMEMBER_F_POINTER)
                    {
                        /* Use the current pointer size. */
                        PDBGFTYPE pTypeMember = dbgfR3TypeLookup(pUVM, "ptr_t");
                        if (RT_LIKELY(pTypeMember))
                        {
                            if (pMember->fFlags & DBGFTYPEREGMEMBER_F_ARRAY)
                                cbType += pMember->cElements * pTypeMember->cbType;
                            else
                                cbType += pTypeMember->cbType;
                        }
                    }
                    else
                    {
                        PDBGFTYPE pTypeMember = dbgfR3TypeLookup(pUVM, pMember->pszType);
                        if (RT_LIKELY(pTypeMember))
                        {
                            if (   pTypeMember->cbType == 0
                                && fCalcNested)
                                rc = dbgfR3TypeCalcSize(pUVM, pTypeMember, fCalcNested);

                            if (RT_SUCCESS(rc))
                            {
                                if (pMember->fFlags & DBGFTYPEREGMEMBER_F_ARRAY)
                                    cbType += pMember->cElements * pTypeMember->cbType;
                                else
                                    cbType += pTypeMember->cbType;
                            }
                        }
                        else
                            rc = VERR_INVALID_STATE;
                    }
                }

                if (RT_SUCCESS(rc))
                    pType->cbType = cbType;
                break;
            }

            case DBGFTYPEVARIANT_UNION:
            {
                /* Get size of the biggest member and use that one. */
                size_t cbType = 0;

                for (uint32_t i = 0; i < pType->pReg->cMembers && RT_SUCCESS(rc); i++)
                {
                    PCDBGFTYPEREGMEMBER pMember = &pType->pReg->paMembers[i];

                    if (pMember->fFlags & DBGFTYPEREGMEMBER_F_POINTER)
                    {
                        /* Use the current pointer size. */
                        PDBGFTYPE pTypeMember = dbgfR3TypeLookup(pUVM, "ptr_t");
                        if (RT_LIKELY(pTypeMember))
                        {
                            if (pMember->fFlags & DBGFTYPEREGMEMBER_F_ARRAY)
                                cbType = RT_MAX(cbType, pMember->cElements * pTypeMember->cbType);
                            else
                                cbType = RT_MAX(cbType, pTypeMember->cbType);
                        }
                    }
                    else
                    {
                        PDBGFTYPE pTypeMember = dbgfR3TypeLookup(pUVM, pMember->pszType);
                        if (RT_LIKELY(pTypeMember))
                        {
                            if (   pTypeMember->cbType == 0
                                && fCalcNested)
                                rc = dbgfR3TypeCalcSize(pUVM, pTypeMember, fCalcNested);

                            if (RT_SUCCESS(rc))
                            {
                                if (pMember->fFlags & DBGFTYPEREGMEMBER_F_ARRAY)
                                    cbType = RT_MAX(cbType, pMember->cElements * pTypeMember->cbType);
                                else
                                    cbType = RT_MAX(cbType, pTypeMember->cbType);
                            }
                        }
                        else
                            rc = VERR_INVALID_STATE;
                    }
                }

                if (RT_SUCCESS(rc))
                    pType->cbType = cbType;
                break;
            }

            case DBGFTYPEVARIANT_ALIAS:
            {
                /* Get the size of the alias. */
                PDBGFTYPE pAliased = dbgfR3TypeLookup(pUVM, pType->pReg->pszAliasedType);
                if (RT_LIKELY(pAliased))
                {
                    if (   pAliased->cbType == 0
                        && fCalcNested)
                        rc = dbgfR3TypeCalcSize(pUVM, pAliased, fCalcNested);

                    if (RT_SUCCESS(rc))
                        pType->cbType = pAliased->cbType;
                }
                else
                    rc = VERR_INVALID_STATE;
                break;
            }

            default:
                AssertMsgFailedReturn(("Invalid type variant: %d", pType->pReg->enmVariant), VERR_INVALID_STATE);
        }
    }

    return rc;
}


/**
 * Callback for clearing the size of all non built-in types.
 *
 * @returns VBox status code.
 * @param   pStr                The type structure.
 * @param   pvUser              The user mode VM handle.
 */
static DECLCALLBACK(int) dbgfR3TypeTraverseClearSize(PRTSTRSPACECORE pStr, void *pvUser)
{
    PDBGFTYPE pType = (PDBGFTYPE)pStr;

    if (pType->pReg)
        pType->cbType = 0;

    NOREF(pvUser);
    return VINF_SUCCESS;
}


/**
 * Callback for calculating the size of all non built-in types.
 *
 * @returns VBox status code.
 * @param   pStr                The type structure.
 * @param   pvUser              The user mode VM handle.
 */
static DECLCALLBACK(int) dbgfR3TypeTraverseCalcSize(PRTSTRSPACECORE pStr, void *pvUser)
{
    PDBGFTYPE pType = (PDBGFTYPE)pStr;

    if (    pType->pReg
        && !pType->cbType)
        dbgfR3TypeCalcSize((PUVM)pvUser, pType, true /* fCalcNested */);

    return VINF_SUCCESS;
}


/**
 * Recalculate the sizes of all registered non builtin types.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 */
static int dbgfR3TypeRecalculateAllSizes(PUVM pUVM)
{
    int rc = VINF_SUCCESS;

    /*
     * Clear the sizes of all non builtin types to 0 first so we know which type we
     * visited later on.
     */
    rc = RTStrSpaceEnumerate(&pUVM->dbgf.s.TypeSpace, dbgfR3TypeTraverseClearSize, pUVM);
    if (RT_SUCCESS(rc))
    {
        /* Now recalculate the size. */
        rc = RTStrSpaceEnumerate(&pUVM->dbgf.s.TypeSpace, dbgfR3TypeTraverseCalcSize, pUVM);
    }

    return rc;
}

/**
 * Validates a given type registration.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pReg                The type registration structure.
 */
static int dbgfR3TypeValidate(PUVM pUVM, PCDBGFTYPEREG pReg)
{
    int rc = VINF_SUCCESS;

    switch (pReg->enmVariant)
    {
        case DBGFTYPEVARIANT_ALIAS:
            if (   pReg->cMembers > 0
                || pReg->paMembers
                || !pReg->pszAliasedType)
                rc = VERR_INVALID_PARAMETER;
            else
            {
                PDBGFTYPE pAlias = dbgfR3TypeLookup(pUVM, pReg->pszAliasedType);
                if (RT_UNLIKELY(!pAlias))
                    rc = VERR_NOT_FOUND;
            }
            break;
        case DBGFTYPEVARIANT_STRUCT:
        case DBGFTYPEVARIANT_UNION:
            if (!pReg->pszAliasedType)
            {
                for (uint32_t i = 0; i < pReg->cMembers; i++)
                {
                    PCDBGFTYPEREGMEMBER pMember = &pReg->paMembers[i];

                    /* Use the current pointer size. */
                    PDBGFTYPE pTypeMember = dbgfR3TypeLookup(pUVM, pMember->pszType);
                    if (RT_UNLIKELY(!pTypeMember))
                    {
                        rc = VERR_NOT_FOUND;
                        break;
                    }

                    if (pMember->fFlags & DBGFTYPEREGMEMBER_F_ARRAY)
                    {
                        if (pMember->cElements == 0)
                            rc = VERR_INVALID_PARAMETER;
                    }
                    else if (pMember->cElements != 0)
                        rc = VERR_INVALID_PARAMETER;
                }
            }
            else
                rc = VERR_INVALID_PARAMETER;
            break;
        default:
            AssertMsgFailedBreakStmt(("Invalid type variant: %d", pReg->enmVariant),
                                     rc = VERR_INVALID_PARAMETER);
    }

    return rc;
}

/**
 * Retains or releases the reference counters to referenced types for the given
 * type registration structure.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pReg                The type registration structure.
 * @param   fRetain             Flag whether to retain or release references.
 */
static int dbgfR3TypeUpdateRefCnts(PUVM pUVM, PCDBGFTYPEREG pReg, bool fRetain)
{
    int rc = VINF_SUCCESS;

    switch (pReg->enmVariant)
    {
        case DBGFTYPEVARIANT_ALIAS:
        {
            AssertPtr(pReg->pszAliasedType);

            PDBGFTYPE pAlias = dbgfR3TypeLookup(pUVM, pReg->pszAliasedType);
            AssertPtr(pAlias);

            if (fRetain)
                pAlias->cRefs++;
            else
                pAlias->cRefs--;
            break;
        }
        case DBGFTYPEVARIANT_STRUCT:
        case DBGFTYPEVARIANT_UNION:
        {
            for (uint32_t i = 0; i < pReg->cMembers; i++)
            {
                PCDBGFTYPEREGMEMBER pMember = &pReg->paMembers[i];

                /* Use the current pointer size. */
                PDBGFTYPE pTypeMember = dbgfR3TypeLookup(pUVM, pMember->pszType);
                AssertPtr(pTypeMember);

                if (fRetain)
                    pTypeMember->cRefs++;
                else
                    pTypeMember->cRefs--;
            }
            break;
        }
        default:
            AssertMsgFailedBreakStmt(("Invalid type variant: %d", pReg->enmVariant),
                                     rc = VERR_INVALID_PARAMETER);
    }

    return rc;
}


/**
 * Registers a single type in the database.
 *
 * @returns VBox status code.
 * @retval VERR_ALREADY_EXISTS if the type exists already.
 * @param   pUVM                The user mode VM handle.
 * @param   pReg                The type registration structure.
 */
static int dbgfR3TypeRegister(PUVM pUVM, PCDBGFTYPEREG pReg)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pUVM=%#p pReg=%#p{%s}\n", pUVM, pReg, pReg->pszType));

    if (dbgfR3TypeLookup(pUVM, pReg->pszType) == NULL)
    {
        rc = dbgfR3TypeValidate(pUVM, pReg);
        if (RT_SUCCESS(rc))
        {
            PDBGFTYPE pType = (PDBGFTYPE)MMR3HeapAllocZU(pUVM, MM_TAG_DBGF_TYPE, sizeof(DBGFTYPE));
            if (RT_LIKELY(pType))
            {
                pType->Core.pszString = pReg->pszType;
                pType->pReg           = pReg;
                pType->cRefs          = 0;
                pType->enmTypeBuiltin = DBGFTYPEBUILTIN_INVALID;
                rc = dbgfR3TypeCalcSize(pUVM, pType, false /* fCalcNested */);
                if (RT_SUCCESS(rc))
                {
                    rc = dbgfR3TypeUpdateRefCnts(pUVM, pReg, true /* fRetain */);
                    if (RT_SUCCESS(rc))
                    {
                        bool fSucc = RTStrSpaceInsert(&pUVM->dbgf.s.TypeSpace, &pType->Core);
                        Assert(fSucc);
                        if (!fSucc)
                        {
                            dbgfR3TypeUpdateRefCnts(pUVM, pReg, false /* fRetain */);
                            rc = VERR_ALREADY_EXISTS;
                        }
                    }
                }

                if (RT_FAILURE(rc))
                    MMR3HeapFree(pType);
            }
            else
                rc = VERR_NO_MEMORY;
        }
    }
    else
        rc = VERR_ALREADY_EXISTS;

    LogFlowFunc(("-> rc=%Rrc\n", rc));
    return rc;
}


/**
 * Registers a new built-in type
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   enmTypeBuiltin      The builtin type enum.
 * @param   cbType              Size of the type in bytes.
 * @param   pszType             The type identifier for the builtin type.
 */
static int dbgfR3TypeRegisterBuiltin(PUVM pUVM, DBGFTYPEBUILTIN enmTypeBuiltin,
                                     size_t cbType, const char *pszType)
{
    LogFlowFunc(("pUVM=%#p enmBuiltin=%d pszType=%s\n", pUVM, enmTypeBuiltin, pszType));

    AssertReturn(!dbgfR3TypeLookup(pUVM, pszType), VERR_INVALID_STATE);

    int rc = VINF_SUCCESS;
    PDBGFTYPE pType = (PDBGFTYPE)MMR3HeapAllocZU(pUVM, MM_TAG_DBGF_TYPE, sizeof(DBGFTYPE));
    if (RT_LIKELY(pType))
    {
        pType->Core.pszString = pszType;
        pType->pReg           = NULL;
        pType->cRefs          = 0;
        pType->cbType         = cbType;
        pType->enmTypeBuiltin = enmTypeBuiltin;
        bool fSucc = RTStrSpaceInsert(&pUVM->dbgf.s.TypeSpace, &pType->Core);
        Assert(fSucc);
        if (!fSucc)
            rc = VERR_ALREADY_EXISTS;

        if (RT_FAILURE(rc))
            MMR3HeapFree(pType);
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * Registers builtin types.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 */
static int dbgfTypeRegisterBuiltinTypes(PUVM pUVM)
{
    int rc = dbgfR3TypeRegisterBuiltin(pUVM, DBGFTYPEBUILTIN_UINT8,  sizeof(uint8_t),  "uint8_t");
    if (RT_SUCCESS(rc))
        rc = dbgfR3TypeRegisterBuiltin(pUVM, DBGFTYPEBUILTIN_INT8,   sizeof(int8_t),   "int8_t");
    if (RT_SUCCESS(rc))
        rc = dbgfR3TypeRegisterBuiltin(pUVM, DBGFTYPEBUILTIN_UINT16, sizeof(uint16_t), "uint16_t");
    if (RT_SUCCESS(rc))
        rc = dbgfR3TypeRegisterBuiltin(pUVM, DBGFTYPEBUILTIN_INT16,  sizeof(int16_t),  "int16_t");
    if (RT_SUCCESS(rc))
        rc = dbgfR3TypeRegisterBuiltin(pUVM, DBGFTYPEBUILTIN_UINT32, sizeof(uint32_t), "uint32_t");
    if (RT_SUCCESS(rc))
        rc = dbgfR3TypeRegisterBuiltin(pUVM, DBGFTYPEBUILTIN_INT32,  sizeof(int32_t),  "int32_t");
    if (RT_SUCCESS(rc))
        rc = dbgfR3TypeRegisterBuiltin(pUVM, DBGFTYPEBUILTIN_UINT64, sizeof(uint64_t), "uint64_t");
    if (RT_SUCCESS(rc))
        rc = dbgfR3TypeRegisterBuiltin(pUVM, DBGFTYPEBUILTIN_INT64,  sizeof(int64_t),  "int64_t");
    if (RT_SUCCESS(rc))
        rc = dbgfR3TypeRegisterBuiltin(pUVM, DBGFTYPEBUILTIN_PTR32,  sizeof(uint32_t), "ptr32_t");
    if (RT_SUCCESS(rc))
        rc = dbgfR3TypeRegisterBuiltin(pUVM, DBGFTYPEBUILTIN_PTR64,  sizeof(uint64_t), "ptr64_t");
    if (RT_SUCCESS(rc))
        rc = dbgfR3TypeRegisterBuiltin(pUVM, DBGFTYPEBUILTIN_PTR,                   0, "ptr_t");
    if (RT_SUCCESS(rc))
        rc = dbgfR3TypeRegisterBuiltin(pUVM, DBGFTYPEBUILTIN_SIZE,                  0, "size_t");

    return rc;
}


/**
 * Parses a single entry for a given type and assigns the value from the byte buffer
 * to the value entry.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pMember             The type member.
 * @param   pValEntry           The value entry holding the value on success.
 * @param   pbBuf               The raw byte buffer.
 * @param   cbBuf               Size of the byte buffer.
 * @param   pcbParsed           Where to store the amount of consumed bytes on success.
 */
static int dbgfR3TypeParseEntry(PUVM pUVM, PCDBGFTYPEREGMEMBER pMember, PDBGFTYPEVALENTRY pValEntry,
                                uint8_t *pbBuf, size_t cbBuf, size_t *pcbParsed)
{
    int rc = VINF_SUCCESS;
    PDBGFTYPE pTypeMember = dbgfR3TypeLookup(pUVM, pMember->pszType);
    uint32_t cValBufs = 1;
    size_t cbParsed = 0;
    PDBGFTYPEVALBUF pValBuf = &pValEntry->Buf.Val;

    AssertPtrReturn(pTypeMember, VERR_INVALID_STATE);

    if (pMember->fFlags & DBGFTYPEREGMEMBER_F_ARRAY)
    {
        cValBufs = pMember->cElements;
        pValBuf = (PDBGFTYPEVALBUF)MMR3HeapAllocZU(pUVM, MM_TAG_DBGF_TYPE, cValBufs * sizeof(DBGFTYPEVALBUF));
        if (RT_UNLIKELY(!pValBuf))
            rc = VERR_NO_MEMORY;

        pValEntry->Buf.pVal = pValBuf;
        pValEntry->cEntries = cValBufs;
        pValEntry->cbType   = pTypeMember->cbType;
    }

    if (RT_SUCCESS(rc))
    {
        for (uint32_t iValBuf = 0; iValBuf < cValBufs && RT_SUCCESS(rc); iValBuf++)
        {
            size_t cbThisParsed = 0;

            if (pTypeMember->pReg)
            {
                /* Compound or aliased type */
                rc = dbgfR3TypeParseBufferByType(pUVM, pTypeMember, pbBuf, cbBuf,
                                                 &pValBuf->pVal, &cbThisParsed);
                if (RT_SUCCESS(rc))
                    pValEntry->enmType = DBGFTYPEBUILTIN_COMPOUND;
            }
            else
            {
                void *pvVal = NULL;

                switch (pTypeMember->enmTypeBuiltin)
                {
                    case DBGFTYPEBUILTIN_UINT8:
                        pvVal = &pValBuf->u8;
                        cbThisParsed = 1;
                        break;
                    case DBGFTYPEBUILTIN_INT8:
                        pvVal = &pValBuf->i8;
                        cbThisParsed = 1;
                        break;
                    case DBGFTYPEBUILTIN_UINT16:
                        pvVal = &pValBuf->u16;
                        cbThisParsed = 2;
                        break;
                    case DBGFTYPEBUILTIN_INT16:
                        pvVal = &pValBuf->i16;
                        cbThisParsed = 2;
                        break;
                    case DBGFTYPEBUILTIN_UINT32:
                        pvVal = &pValBuf->u32;
                        cbThisParsed = 4;
                        break;
                    case DBGFTYPEBUILTIN_INT32:
                        pvVal = &pValBuf->i32;
                        cbThisParsed = 4;
                        break;
                    case DBGFTYPEBUILTIN_UINT64:
                        pvVal = &pValBuf->u64;
                        cbThisParsed = 8;
                        break;
                    case DBGFTYPEBUILTIN_INT64:
                        pvVal = &pValBuf->i64;
                        cbThisParsed = 8;
                        break;
                    case DBGFTYPEBUILTIN_PTR32:
                        pvVal = &pValBuf->GCPtr;
                        cbThisParsed = 4;
                        break;
                    case DBGFTYPEBUILTIN_PTR64:
                        pvVal = &pValBuf->GCPtr;
                        cbThisParsed = 8;
                        break;
                    case DBGFTYPEBUILTIN_PTR:
                        pvVal = &pValBuf->GCPtr;
                        cbThisParsed = pTypeMember->cbType;
                        break;
                    case DBGFTYPEBUILTIN_SIZE:
                        pvVal = &pValBuf->size;
                        cbThisParsed = pTypeMember->cbType;
                        break;
                    case DBGFTYPEBUILTIN_FLOAT32:
                    case DBGFTYPEBUILTIN_FLOAT64:
                    case DBGFTYPEBUILTIN_COMPOUND:
                    default:
                        AssertMsgFailedBreakStmt(("Invalid built-in type specified: %d\n", pTypeMember->enmTypeBuiltin),
                                                 rc = VERR_INVALID_STATE);
                }

                if (RT_SUCCESS(rc))
                {
                    pValEntry->enmType = pTypeMember->enmTypeBuiltin;
                    if (cbBuf >= cbThisParsed)
                        memcpy(pvVal, pbBuf, cbThisParsed);
                    else
                        rc = VERR_BUFFER_OVERFLOW;
                }
            }

            pValBuf++;

            cbParsed += cbThisParsed;
            pbBuf    += cbThisParsed;
            cbBuf    -= cbThisParsed;
        }
    }

    if (   RT_FAILURE(rc)
        && cValBufs > 1)
        MMR3HeapFree(pValBuf);

    if (RT_SUCCESS(rc))
    {
        pValEntry->cEntries = cValBufs;
        *pcbParsed = cbParsed;
    }

    return rc;
}


/**
 * Parses the given byte buffer and returns the value based no the type information.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pType               The type information.
 * @param   pbBuf               The byte buffer to parse.
 * @param   cbBuf               Size of the buffer.
 * @param   ppVal               Where to store the pointer to the value on success.
 * @param   pcbParsed           How many bytes of the buffer we consumed.
 */
static int dbgfR3TypeParseBufferByType(PUVM pUVM, PDBGFTYPE pType, uint8_t *pbBuf, size_t cbBuf,
                                       PDBGFTYPEVAL *ppVal, size_t *pcbParsed)
{
    int rc = VINF_SUCCESS;
    uint32_t cEntries = pType->pReg ? pType->pReg->cMembers : 1;
    PDBGFTYPEVAL pVal = (PDBGFTYPEVAL)MMR3HeapAllocZU(pUVM, MM_TAG_DBGF_TYPE,
                                                      RT_UOFFSETOF_DYN(DBGFTYPEVAL, aEntries[cEntries]));
    if (RT_LIKELY(pVal))
    {
        size_t cbParsed = 0;

        pVal->pTypeReg = pType->pReg;
        for (uint32_t i = 0; i < cEntries && RT_SUCCESS(rc); i++)
        {
            PCDBGFTYPEREGMEMBER pMember = &pType->pReg->paMembers[i];
            PDBGFTYPEVALENTRY pValEntry = &pVal->aEntries[i];
            rc = dbgfR3TypeParseEntry(pUVM, pMember, pValEntry, pbBuf, cbBuf, &cbParsed);
            if (RT_SUCCESS(rc))
            {
                pbBuf += cbParsed;
                cbBuf -= cbParsed;
            }
        }

        if (RT_SUCCESS(rc))
        {
            pVal->cEntries = cEntries;
            *pcbParsed = cbParsed;
            *ppVal     = pVal;
        }
        else
            MMR3HeapFree(pVal); /** @todo Leak for embedded structs. */
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


/**
 * Dumps one level of a typed value.
 *
 * @returns VBox status code.
 * @param   pVal                The value to dump.
 * @param   iLvl                The current level.
 * @param   cLvlMax             The maximum level.
 * @param   pfnDump             The dumper callback.
 * @param   pvUser              The opaque user data to pass to the dumper callback.
 */
static int dbgfR3TypeValDump(PDBGFTYPEVAL pVal, uint32_t iLvl, uint32_t cLvlMax,
                             PFNDBGFR3TYPEVALDUMP pfnDump, void *pvUser)
{
    int rc = VINF_SUCCESS;
    PCDBGFTYPEREG pType = pVal->pTypeReg;

    for (uint32_t i = 0; i < pVal->cEntries && rc == VINF_SUCCESS; i++)
    {
        PCDBGFTYPEREGMEMBER pTypeMember = &pType->paMembers[i];
        PDBGFTYPEVALENTRY pValEntry = &pVal->aEntries[i];
        PDBGFTYPEVALBUF pValBuf = pValEntry->cEntries > 1 ? pValEntry->Buf.pVal : &pValEntry->Buf.Val;

        rc = pfnDump(0 /* off */, pTypeMember->pszName, iLvl, pValEntry->enmType, pValEntry->cbType,
                     pValBuf, pValEntry->cEntries, pvUser);
        if (   rc == VINF_SUCCESS
            && pValEntry->enmType == DBGFTYPEBUILTIN_COMPOUND
            && iLvl < cLvlMax)
        {
            /* Print embedded structs. */
            for (uint32_t iValBuf = 0; iValBuf < pValEntry->cEntries && rc == VINF_SUCCESS; iValBuf++)
                rc = dbgfR3TypeValDump(pValBuf[iValBuf].pVal, iLvl + 1, cLvlMax, pfnDump, pvUser);
        }
    }

    return rc;
}


/**
 * Dumps one level of a type.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pType               The type to dump.
 * @param   iLvl                The current level.
 * @param   cLvlMax             The maximum level.
 * @param   pfnDump             The dumper callback.
 * @param   pvUser              The opaque user data to pass to the dumper callback.
 */
static int dbgfR3TypeDump(PUVM pUVM, PDBGFTYPE pType, uint32_t iLvl, uint32_t cLvlMax,
                          PFNDBGFR3TYPEDUMP pfnDump, void *pvUser)
{
    int rc = VINF_SUCCESS;
    PCDBGFTYPEREG pTypeReg = pType->pReg;

    switch (pTypeReg->enmVariant)
    {
        case DBGFTYPEVARIANT_ALIAS:
            rc = VERR_NOT_IMPLEMENTED;
            break;
        case DBGFTYPEVARIANT_STRUCT:
        case DBGFTYPEVARIANT_UNION:
            for (uint32_t i = 0; i < pTypeReg->cMembers && rc == VINF_SUCCESS; i++)
            {
                PCDBGFTYPEREGMEMBER pTypeMember = &pTypeReg->paMembers[i];
                PDBGFTYPE pTypeResolved = dbgfR3TypeLookup(pUVM, pTypeMember->pszType);

                rc = pfnDump(0 /* off */, pTypeMember->pszName, iLvl, pTypeMember->pszType,
                             pTypeMember->fFlags, pTypeMember->cElements, pvUser);
                if (   rc == VINF_SUCCESS
                    && pTypeResolved->pReg
                    && iLvl < cLvlMax)
                {
                    /* Print embedded structs. */
                    rc = dbgfR3TypeDump(pUVM, pTypeResolved, iLvl + 1, cLvlMax, pfnDump, pvUser);
                }
            }
            break;
        default:
            AssertMsgFailed(("Invalid type variant: %u\n", pTypeReg->enmVariant));
            rc = VERR_INVALID_STATE;
    }

    return rc;
}


/**
 * Initializes the type database.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 */
DECLHIDDEN(int)  dbgfR3TypeInit(PUVM pUVM)
{
    int  rc   = VINF_SUCCESS;
    if (!pUVM->dbgf.s.fTypeDbInitialized)
    {
        rc = RTSemRWCreate(&pUVM->dbgf.s.hTypeDbLock);
        if (RT_SUCCESS(rc))
        {
            rc = dbgfTypeRegisterBuiltinTypes(pUVM);
            if (RT_FAILURE(rc))
            {
                RTSemRWDestroy(pUVM->dbgf.s.hTypeDbLock);
                pUVM->dbgf.s.hTypeDbLock = NIL_RTSEMRW;
            }
        }
        pUVM->dbgf.s.fTypeDbInitialized = RT_SUCCESS(rc);
    }
    return rc;
}


/**
 * Terminates the type database.
 *
 * @param   pUVM                The user mode VM handle.
 */
DECLHIDDEN(void) dbgfR3TypeTerm(PUVM pUVM)
{
    RTSemRWDestroy(pUVM->dbgf.s.hTypeDbLock);
    pUVM->dbgf.s.hTypeDbLock = NIL_RTSEMRW;
    pUVM->dbgf.s.fTypeDbInitialized = false;
}


/**
 * Registers a new type for lookup.
 *
 * @returns VBox status code.
 * @retval  VERR_ALREADY_EXISTS if the type exists already.
 * @param   pUVM                The user mode VM handle.
 * @param   cTypes              Number of types to register.
 * @param   paTypes             The array of type registration structures to register.
 */
VMMR3DECL(int) DBGFR3TypeRegister(PUVM pUVM, uint32_t cTypes, PCDBGFTYPEREG paTypes)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertReturn(cTypes > 0, VERR_INVALID_PARAMETER);
    AssertPtrReturn(paTypes, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    if (!pUVM->dbgf.s.fTypeDbInitialized)
    {
        rc = dbgfR3TypeInit(pUVM);
        if (RT_FAILURE(rc))
            return rc;
    }

    DBGF_TYPE_DB_LOCK_WRITE(pUVM);
    for (uint32_t i = 0; i < cTypes && RT_SUCCESS(rc); i++)
    {
        rc = dbgfR3TypeRegister(pUVM, &paTypes[i]);
        if (   RT_FAILURE(rc)
            && i > 0)
        {
            /* Deregister types in reverse order. */
            do
            {
                int rc2 = DBGFR3TypeDeregister(pUVM, paTypes[i].pszType);
                AssertRC(rc2);
                i--;
            } while (i > 0);

            break;
        }
    }
    DBGF_TYPE_DB_UNLOCK_WRITE(pUVM);

    return rc;
}


/**
 * Deregisters a previously registered type.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if the type is not known.
 * @retval  VERR_RESOURCE_IN_USE if the type is used by another type.
 * @param   pUVM                The user mode VM handle.
 * @param   pszType             The type identifier to deregister.
 */
VMMR3DECL(int) DBGFR3TypeDeregister(PUVM pUVM, const char *pszType)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(pszType, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    if (!pUVM->dbgf.s.fTypeDbInitialized)
    {
        rc = dbgfR3TypeInit(pUVM);
        if (RT_FAILURE(rc))
            return rc;
    }

    DBGF_TYPE_DB_LOCK_WRITE(pUVM);
    PDBGFTYPE pType = dbgfR3TypeLookup(pUVM, pszType);
    if (pType)
    {
        if (!pType->cRefs)
        {

        }
        else
            rc = VERR_RESOURCE_IN_USE;
    }
    else
        rc = VERR_NOT_FOUND;
    DBGF_TYPE_DB_UNLOCK_WRITE(pUVM);

    return rc;
}


/**
 * Return the type registration structure for the given type identifier.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if the type is not known.
 * @param   pUVM                The user mode VM handle.
 * @param   pszType             The type identifier to get the registration structure from.
 * @param   ppTypeReg           Where to store the type registration structure on success.
 */
VMMR3DECL(int) DBGFR3TypeQueryReg(PUVM pUVM, const char *pszType, PCDBGFTYPEREG *ppTypeReg)
{
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(pszType, VERR_INVALID_POINTER);
    AssertPtrReturn(ppTypeReg, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    if (!pUVM->dbgf.s.fTypeDbInitialized)
    {
        rc = dbgfR3TypeInit(pUVM);
        if (RT_FAILURE(rc))
            return rc;
    }

    DBGF_TYPE_DB_LOCK_READ(pUVM);
    PDBGFTYPE pType = dbgfR3TypeLookup(pUVM, pszType);
    if (pType)
        *ppTypeReg = pType->pReg;
    else
        rc = VERR_NOT_FOUND;
    DBGF_TYPE_DB_UNLOCK_READ(pUVM);

    LogFlowFunc(("-> rc=%Rrc\n", rc));
    return rc;
}


/**
 * Queries the size a given type would occupy in memory.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if the type is not known.
 * @param   pUVM                The user mode VM handle.
 * @param   pszType             The type identifier.
 * @param   pcbType             Where to store the amount of memory occupied in bytes.
 */
VMMR3DECL(int) DBGFR3TypeQuerySize(PUVM pUVM, const char *pszType, size_t *pcbType)
{
    LogFlowFunc(("pUVM=%#p pszType=%s pcbType=%#p\n", pUVM, pszType, pcbType));
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(pszType, VERR_INVALID_POINTER);
    AssertPtrReturn(pcbType, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    if (!pUVM->dbgf.s.fTypeDbInitialized)
    {
        rc = dbgfR3TypeInit(pUVM);
        if (RT_FAILURE(rc))
            return rc;
    }

    DBGF_TYPE_DB_LOCK_READ(pUVM);
    PDBGFTYPE pType = dbgfR3TypeLookup(pUVM, pszType);
    if (pType)
        *pcbType = pType->cbType;
    else
        rc = VERR_NOT_FOUND;
    DBGF_TYPE_DB_UNLOCK_READ(pUVM);

    LogFlowFunc(("-> rc=%Rrc\n", rc));
    return rc;
}


/**
 * Sets the size of the given type in bytes.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if the type is not known.
 * @retval  VERR_NOT_SUPPORTED if changing the size of this type is not supported.
 * @param   pUVM                The user mode VM handle.
 * @param   pszType             The type identifier.
 * @param   cbType              The size of the type in bytes.
 *
 * @note: This currently works only for the builtin pointer type without the explicit
 *        size (ptr_t or DBGFTYPEBUILTIN_PTR).
 */
VMMR3DECL(int) DBGFR3TypeSetSize(PUVM pUVM, const char *pszType, size_t cbType)
{
    LogFlowFunc(("pUVM=%#p pszType=%s cbType=%zu\n", pUVM, pszType, cbType));
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(pszType, VERR_INVALID_POINTER);
    AssertReturn(cbType > 0, VERR_INVALID_PARAMETER);

    int rc = VINF_SUCCESS;
    if (!pUVM->dbgf.s.fTypeDbInitialized)
    {
        rc = dbgfR3TypeInit(pUVM);
        if (RT_FAILURE(rc))
            return rc;
    }

    DBGF_TYPE_DB_LOCK_WRITE(pUVM);
    PDBGFTYPE pType = dbgfR3TypeLookup(pUVM, pszType);
    if (pType)
    {
        if (   !pType->pReg
            && (   pType->enmTypeBuiltin == DBGFTYPEBUILTIN_PTR
                || pType->enmTypeBuiltin == DBGFTYPEBUILTIN_SIZE))
        {
            if (pType->cbType != cbType)
            {
                pType->cbType = cbType;
                rc = dbgfR3TypeRecalculateAllSizes(pUVM);
            }
        }
        else
            rc = VERR_NOT_SUPPORTED;
    }
    else
        rc = VERR_NOT_FOUND;
    DBGF_TYPE_DB_UNLOCK_WRITE(pUVM);

    LogFlowFunc(("-> rc=%Rrc\n", rc));
    return rc;
}


/**
 * Dumps the type information of the given type.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pszType             The type identifier.
 * @param   fFlags              Flags to control the dumping (reserved, MBZ).
 * @param   cLvlMax             Maximum levels to nest.
 * @param   pfnDump             The dumper callback.
 * @param   pvUser              Opaque user data.
 */
VMMR3DECL(int) DBGFR3TypeDumpEx(PUVM pUVM, const char *pszType, uint32_t fFlags,
                                uint32_t cLvlMax, PFNDBGFR3TYPEDUMP pfnDump, void *pvUser)
{
    LogFlowFunc(("pUVM=%#p pszType=%s fFlags=%#x cLvlMax=%u pfnDump=%#p pvUser=%#p\n",
                 pUVM, pszType, fFlags, cLvlMax, pfnDump, pvUser));
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(pszType, VERR_INVALID_POINTER);
    AssertPtrReturn(pfnDump, VERR_INVALID_POINTER);
    RT_NOREF_PV(fFlags);

    int rc = VINF_SUCCESS;
    if (!pUVM->dbgf.s.fTypeDbInitialized)
    {
        rc = dbgfR3TypeInit(pUVM);
        if (RT_FAILURE(rc))
            return rc;
    }

    DBGF_TYPE_DB_LOCK_READ(pUVM);
    PDBGFTYPE pType = dbgfR3TypeLookup(pUVM, pszType);
    if (pType)
        rc = dbgfR3TypeDump(pUVM, pType, 0 /* iLvl */, cLvlMax, pfnDump, pvUser);
    else
        rc = VERR_NOT_FOUND;
    DBGF_TYPE_DB_UNLOCK_READ(pUVM);

    LogFlowFunc(("-> rc=%Rrc\n", rc));
    return rc;
}


/**
 * Returns the value of a memory buffer at the given address formatted for the given
 * type.
 *
 * @returns VBox status code.
 * @retval  VERR_NOT_FOUND if the type is not known.
 * @param   pUVM                The user mode VM handle.
 * @param   pAddress            The address to start reading from.
 * @param   pszType             The type identifier.
 * @param   ppVal               Where to store the pointer to the value structure
 *                              on success.
 */
VMMR3DECL(int) DBGFR3TypeQueryValByType(PUVM pUVM, PCDBGFADDRESS pAddress, const char *pszType,
                                        PDBGFTYPEVAL *ppVal)
{
    LogFlowFunc(("pUVM=%#p pAddress=%#p pszType=%s ppVal=%#p\n", pUVM, pAddress, pszType, ppVal));
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(pAddress, VERR_INVALID_POINTER);
    AssertPtrReturn(pszType, VERR_INVALID_POINTER);
    AssertPtrReturn(ppVal, VERR_INVALID_POINTER);

    int rc = VINF_SUCCESS;
    if (!pUVM->dbgf.s.fTypeDbInitialized)
    {
        rc = dbgfR3TypeInit(pUVM);
        if (RT_FAILURE(rc))
            return rc;
    }

    DBGF_TYPE_DB_LOCK_READ(pUVM);
    PDBGFTYPE pType = dbgfR3TypeLookup(pUVM, pszType);
    if (pType)
    {
        uint8_t *pbBuf = (uint8_t *)MMR3HeapAllocZU(pUVM, MM_TAG_DBGF_TYPE, pType->cbType);
        if (RT_LIKELY(pbBuf))
        {
            rc = DBGFR3MemRead(pUVM, 0 /*idCpu*/, pAddress, pbBuf, pType->cbType);
            if (RT_SUCCESS(rc))
            {
                /* Parse the buffer based on the type. */
                size_t cbParsed = 0;
                rc = dbgfR3TypeParseBufferByType(pUVM, pType, pbBuf, pType->cbType,
                                                 ppVal, &cbParsed);
            }

            MMR3HeapFree(pbBuf);
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_NOT_FOUND;
    DBGF_TYPE_DB_UNLOCK_READ(pUVM);

    LogFlowFunc(("-> rc=%Rrc\n", rc));
    return rc;
}


/**
 * Frees all acquired resources of a value previously obtained with
 * DBGFR3TypeQueryValByType().
 *
 * @param   pVal                The value to free.
 */
VMMR3DECL(void) DBGFR3TypeValFree(PDBGFTYPEVAL pVal)
{
    AssertPtrReturnVoid(pVal);

    for (uint32_t i = 0; i < pVal->cEntries; i++)
    {
        PDBGFTYPEVALENTRY pValEntry = &pVal->aEntries[i];
        PDBGFTYPEVALBUF pValBuf = pValEntry->cEntries > 1 ? pValEntry->Buf.pVal : &pValEntry->Buf.Val;

        if (pValEntry->enmType == DBGFTYPEBUILTIN_COMPOUND)
            for (uint32_t iBuf = 0; iBuf < pValEntry->cEntries; iBuf++)
                DBGFR3TypeValFree(pValBuf->pVal);

        if (pValEntry->cEntries > 1)
            MMR3HeapFree(pValEntry->Buf.pVal);
    }

    MMR3HeapFree(pVal);
}


/**
 * Reads the guest memory with the given type and dumps the content of the type.
 *
 * @returns VBox status code.
 * @param   pUVM                The user mode VM handle.
 * @param   pAddress            The address to start reading from.
 * @param   pszType             The type identifier.
 * @param   fFlags              Flags for tweaking (reserved, must be zero).
 * @param   cLvlMax             Maximum number of levels to expand embedded structs.
 * @param   pfnDump             The dumper callback.
 * @param   pvUser              The opaque user data to pass to the callback.
 */
VMMR3DECL(int) DBGFR3TypeValDumpEx(PUVM pUVM, PCDBGFADDRESS pAddress, const char *pszType, uint32_t fFlags,
                                   uint32_t cLvlMax, FNDBGFR3TYPEVALDUMP pfnDump, void *pvUser)
{
    LogFlowFunc(("pUVM=%#p pAddress=%#p pszType=%s fFlags=%#x pfnDump=%#p pvUser=%#p\n",
                 pUVM, pAddress, pszType, fFlags,pfnDump, pvUser));
    UVM_ASSERT_VALID_EXT_RETURN(pUVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(pAddress, VERR_INVALID_POINTER);
    AssertPtrReturn(pszType, VERR_INVALID_POINTER);
    AssertPtrReturn(pfnDump, VERR_INVALID_POINTER);
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);
    AssertReturn(cLvlMax >= 1, VERR_INVALID_PARAMETER);

    PDBGFTYPEVAL pVal = NULL;
    int rc = DBGFR3TypeQueryValByType(pUVM, pAddress, pszType, &pVal);
    if (RT_SUCCESS(rc))
    {
        rc = dbgfR3TypeValDump(pVal, 0 /* iLvl */, cLvlMax, pfnDump, pvUser);
        DBGFR3TypeValFree(pVal);
    }

    LogFlowFunc(("-> rc=%Rrc\n", rc));
    return rc;
}

