/* $Id: manifest2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Manifest, the core.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include "internal/iprt.h"
#include <iprt/manifest.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/param.h>
#include <iprt/md5.h>
#include <iprt/sha.h>
#include <iprt/string.h>
#include <iprt/vfs.h>

#include "internal/magics.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/**
 * Manifest attribute.
 *
 * Used both for entries and manifest attributes.
 */
typedef struct RTMANIFESTATTR
{
    /** The string space core (szName). */
    RTSTRSPACECORE      StrCore;
    /** The property value. */
    char               *pszValue;
    /** The attribute type if applicable, RTMANIFEST_ATTR_UNKNOWN if not. */
    uint32_t            fType;
    /** Whether it was visited by the equals operation or not. */
    bool                fVisited;
    /** The normalized property name that StrCore::pszString points at. */
    RT_FLEXIBLE_ARRAY_EXTENSION
    char                szName[RT_FLEXIBLE_ARRAY];
} RTMANIFESTATTR;
/** Pointer to a manifest attribute. */
typedef RTMANIFESTATTR *PRTMANIFESTATTR;


/**
 * Manifest entry.
 */
typedef struct RTMANIFESTENTRY
{
    /** The string space core (szName). */
    RTSTRSPACECORE      StrCore;
    /** The entry attributes (hashes, checksums, size, etc) -
     *  RTMANIFESTATTR. */
    RTSTRSPACE          Attributes;
    /** The number of attributes. */
    uint32_t            cAttributes;
    /** Whether it was visited by the equals operation or not. */
    bool                fVisited;
    /** The normalized entry name that StrCore::pszString points at. */
    char                szName[RT_FLEXIBLE_ARRAY_NESTED];
} RTMANIFESTENTRY;
/** Pointer to a manifest entry. */
typedef RTMANIFESTENTRY *PRTMANIFESTENTRY;


/**
 * Manifest handle data.
 */
typedef struct RTMANIFESTINT
{
    /** Magic value (RTMANIFEST_MAGIC). */
    uint32_t            u32Magic;
    /** The number of references to this manifest. */
    uint32_t volatile   cRefs;
    /** String space of the entries covered by this manifest -
     *  RTMANIFESTENTRY. */
    RTSTRSPACE          Entries;
    /** The number of entries. */
    uint32_t            cEntries;
    /** The entry for the manifest itself. */
    RTMANIFESTENTRY     SelfEntry;
} RTMANIFESTINT;

/** The value of RTMANIFESTINT::u32Magic. */
#define RTMANIFEST_MAGIC    UINT32_C(0x99998866)

/**
 * Argument package passed to rtManifestWriteStdAttr by rtManifestWriteStdEntry
 * and RTManifestWriteStandard.
 */
typedef struct RTMANIFESTWRITESTDATTR
{
    /** The entry name. */
    const char     *pszEntry;
    /** The output I/O stream. */
    RTVFSIOSTREAM   hVfsIos;
} RTMANIFESTWRITESTDATTR;


/**
 * Argument package used by RTManifestEqualsEx to pass its arguments to the
 * enumeration callback functions.
 */
typedef struct RTMANIFESTEQUALS
{
    /** Name of entries to ignore. */
    const char * const *papszIgnoreEntries;
    /** Name of attributes to ignore. */
    const char * const *papszIgnoreAttrs;
    /** Flags governing the comparision. */
    uint32_t            fFlags;
    /** Where to return an error message (++) on failure.  Can be NULL. */
    char               *pszError;
    /** The size of the buffer pszError points to.  Can be 0. */
    size_t              cbError;

    /** Pointer to the 2nd manifest. */
    RTMANIFESTINT      *pThis2;

    /** The number of ignored entries from the 1st manifest. */
    uint32_t            cIgnoredEntries2;
    /** The number of entries processed from the 2nd manifest. */
    uint32_t            cEntries2;

    /** The number of ignored attributes from the 1st manifest. */
    uint32_t            cIgnoredAttributes1;
    /** The number of ignored attributes from the 1st manifest. */
    uint32_t            cIgnoredAttributes2;
    /** The number of attributes processed from the 2nd manifest. */
    uint32_t            cAttributes2;
    /** Pointer to the string space to get matching attributes from. */
    PRTSTRSPACE         pAttributes2;
    /** The name of the current entry.
     * Points to an empty string it's the manifest attributes. */
    const char         *pszCurEntry;
} RTMANIFESTEQUALS;
/** Pointer to an RTManifestEqualEx argument packet. */
typedef RTMANIFESTEQUALS *PRTMANIFESTEQUALS;

/**
 * Argument package used by rtManifestQueryAttrWorker to pass its search
 * criteria to rtManifestQueryAttrEnumCallback and get a result back.
 */
typedef struct RTMANIFESTQUERYATTRARGS
{
    /** The attribute types we're hunting for. */
    uint32_t        fType;
    /** What we've found. */
    PRTMANIFESTATTR pAttr;
} RTMANIFESTQUERYATTRARGS;
/** Pointer to a rtManifestQueryAttrEnumCallback argument packet. */
typedef RTMANIFESTQUERYATTRARGS *PRTMANIFESTQUERYATTRARGS;


RTDECL(int) RTManifestCreate(uint32_t fFlags, PRTMANIFEST phManifest)
{
    AssertReturn(!fFlags, VERR_INVALID_PARAMETER);
    AssertPtr(phManifest);

    RTMANIFESTINT *pThis = (RTMANIFESTINT *)RTMemAlloc(RT_UOFFSETOF(RTMANIFESTINT, SelfEntry.szName[1]));
    if (!pThis)
        return VERR_NO_MEMORY;

    pThis->u32Magic     = RTMANIFEST_MAGIC;
    pThis->cRefs        = 1;
    pThis->Entries      = NULL;
    pThis->cEntries     = 0;
    pThis->SelfEntry.StrCore.pszString = "main";
    pThis->SelfEntry.StrCore.cchString = 4;
    pThis->SelfEntry.Attributes        = NULL;
    pThis->SelfEntry.cAttributes       = 0;
    pThis->SelfEntry.fVisited          = false;
    pThis->SelfEntry.szName[0]         = '\0';

    *phManifest = pThis;
    return VINF_SUCCESS;
}


RTDECL(uint32_t) RTManifestRetain(RTMANIFEST hManifest)
{
    RTMANIFESTINT *pThis = hManifest;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTMANIFEST_MAGIC, UINT32_MAX);

    uint32_t cRefs = ASMAtomicIncU32(&pThis->cRefs);
    Assert(cRefs > 1 && cRefs < _1M);

    return cRefs;
}


/**
 * @callback_method_impl{FNRTSTRSPACECALLBACK, Destroys RTMANIFESTATTR.}
 */
static DECLCALLBACK(int) rtManifestDestroyAttribute(PRTSTRSPACECORE pStr, void *pvUser)
{
    PRTMANIFESTATTR pAttr = RT_FROM_MEMBER(pStr, RTMANIFESTATTR, StrCore);
    RTStrFree(pAttr->pszValue);
    pAttr->pszValue = NULL;
    RTMemFree(pAttr);
    NOREF(pvUser);
    return 0;
}


/**
 * @callback_method_impl{FNRTSTRSPACECALLBACK, Destroys RTMANIFESTENTRY.}
 */
static DECLCALLBACK(int) rtManifestDestroyEntry(PRTSTRSPACECORE pStr, void *pvUser)
{
    PRTMANIFESTENTRY pEntry = RT_FROM_MEMBER(pStr, RTMANIFESTENTRY, StrCore);
    RTStrSpaceDestroy(&pEntry->Attributes, rtManifestDestroyAttribute, pvUser);
    RTMemFree(pEntry);
    return 0;
}


RTDECL(uint32_t) RTManifestRelease(RTMANIFEST hManifest)
{
    RTMANIFESTINT *pThis = hManifest;
    if (pThis == NIL_RTMANIFEST)
        return 0;
    AssertPtrReturn(pThis, UINT32_MAX);
    AssertReturn(pThis->u32Magic == RTMANIFEST_MAGIC, UINT32_MAX);

    uint32_t cRefs = ASMAtomicDecU32(&pThis->cRefs);
    Assert(cRefs < _1M);
    if (!cRefs)
    {
        ASMAtomicWriteU32(&pThis->u32Magic, ~RTMANIFEST_MAGIC);
        RTStrSpaceDestroy(&pThis->Entries, rtManifestDestroyEntry,pThis);
        RTStrSpaceDestroy(&pThis->SelfEntry.Attributes, rtManifestDestroyAttribute, pThis);
        RTMemFree(pThis);
    }

    return cRefs;
}


RTDECL(int) RTManifestDup(RTMANIFEST hManifestSrc, PRTMANIFEST phManifestDst)
{
    RTMANIFESTINT *pThis = hManifestSrc;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTMANIFEST_MAGIC, VERR_INVALID_HANDLE);
    AssertPtr(phManifestDst);

    RT_NOREF_PV(phManifestDst); /** @todo implement cloning. */

    return VERR_NOT_IMPLEMENTED;
}


/**
 * @callback_method_impl{FNRTSTRSPACECALLBACK, Prepare equals operation}
 */
static DECLCALLBACK(int) rtManifestAttributeClearVisited(PRTSTRSPACECORE pStr, void *pvUser)
{
    PRTMANIFESTATTR pAttr = RT_FROM_MEMBER(pStr, RTMANIFESTATTR, StrCore);
    pAttr->fVisited = false;
    NOREF(pvUser);
    return 0;
}


/**
 * @callback_method_impl{FNRTSTRSPACECALLBACK, Prepare equals operation}
 */
static DECLCALLBACK(int) rtManifestEntryClearVisited(PRTSTRSPACECORE pStr, void *pvUser)
{
    PRTMANIFESTENTRY pEntry = RT_FROM_MEMBER(pStr, RTMANIFESTENTRY, StrCore);
    RTStrSpaceEnumerate(&pEntry->Attributes, rtManifestAttributeClearVisited, NULL);
    pEntry->fVisited = false;
    NOREF(pvUser);
    return 0;
}


/**
 * @callback_method_impl{FNRTSTRSPACECALLBACK, Finds the first missing}
 */
static DECLCALLBACK(int) rtManifestAttributeFindMissing2(PRTSTRSPACECORE pStr, void *pvUser)
{
    PRTMANIFESTEQUALS pEquals = (PRTMANIFESTEQUALS)pvUser;
    PRTMANIFESTATTR   pAttr   = RT_FROM_MEMBER(pStr, RTMANIFESTATTR, StrCore);

    /*
     * Already visited?
     */
    if (pAttr->fVisited)
        return 0;

    /*
     * Ignore this entry?
     */
    char const * const *ppsz = pEquals->papszIgnoreAttrs;
    if (ppsz)
    {
        while (*ppsz)
        {
            if (!strcmp(*ppsz, pAttr->szName))
                return 0;
            ppsz++;
        }
    }

    /*
     * Gotcha!
     */
    if (*pEquals->pszCurEntry)
        RTStrPrintf(pEquals->pszError, pEquals->cbError,
                    "Attribute '%s' on '%s' was not found in the 1st manifest",
                    pAttr->szName, pEquals->pszCurEntry);
    else
        RTStrPrintf(pEquals->pszError, pEquals->cbError, "Attribute '%s' was not found in the 1st manifest", pAttr->szName);
    return VERR_NOT_EQUAL;
}


/**
 * @callback_method_impl{FNRTSTRSPACECALLBACK, Finds the first missing}
 */
static DECLCALLBACK(int) rtManifestEntryFindMissing2(PRTSTRSPACECORE pStr, void *pvUser)
{
    PRTMANIFESTEQUALS pEquals = (PRTMANIFESTEQUALS)pvUser;
    PRTMANIFESTENTRY  pEntry  = RT_FROM_MEMBER(pStr, RTMANIFESTENTRY, StrCore);

    /*
     * Already visited?
     */
    if (pEntry->fVisited)
        return 0;

    /*
     * Ignore this entry?
     */
    char const * const *ppsz = pEquals->papszIgnoreEntries;
    if (ppsz)
    {
        while (*ppsz)
        {
            if (!strcmp(*ppsz, pEntry->StrCore.pszString))
                return 0;
            ppsz++;
        }
    }

    /*
     * Gotcha!
     */
    RTStrPrintf(pEquals->pszError, pEquals->cbError, "'%s' was not found in the 1st manifest", pEntry->StrCore.pszString);
    return VERR_NOT_EQUAL;
}


/**
 * @callback_method_impl{FNRTSTRSPACECALLBACK, Compares attributes}
 */
static DECLCALLBACK(int) rtManifestAttributeCompare(PRTSTRSPACECORE pStr, void *pvUser)
{
    PRTMANIFESTEQUALS pEquals = (PRTMANIFESTEQUALS)pvUser;
    PRTMANIFESTATTR   pAttr1  = RT_FROM_MEMBER(pStr, RTMANIFESTATTR, StrCore);
    PRTMANIFESTATTR   pAttr2;

    Assert(!pAttr1->fVisited);
    pAttr1->fVisited = true;

    /*
     * Ignore this entry?
     */
    char const * const *ppsz = pEquals->papszIgnoreAttrs;
    if (ppsz)
    {
        while (*ppsz)
        {
            if (!strcmp(*ppsz, pAttr1->szName))
            {
                pAttr2 = (PRTMANIFESTATTR)RTStrSpaceGet(pEquals->pAttributes2, pAttr1->szName);
                if (pAttr2)
                {
                    Assert(!pAttr2->fVisited);
                    pAttr2->fVisited = true;
                    pEquals->cIgnoredAttributes2++;
                }
                pEquals->cIgnoredAttributes1++;
                return 0;
            }
            ppsz++;
        }
    }

    /*
     * Find the matching attribute.
     */
    pAttr2 = (PRTMANIFESTATTR)RTStrSpaceGet(pEquals->pAttributes2, pAttr1->szName);
    if (!pAttr2)
    {
        if (pEquals->fFlags & RTMANIFEST_EQUALS_IGN_MISSING_ATTRS)
            return 0;

        if (*pEquals->pszCurEntry)
            RTStrPrintf(pEquals->pszError, pEquals->cbError,
                        "Attribute '%s' on '%s' was not found in the 2nd manifest",
                        pAttr1->szName, pEquals->pszCurEntry);
        else
            RTStrPrintf(pEquals->pszError, pEquals->cbError, "Attribute '%s' was not found in the 2nd manifest", pAttr1->szName);
        return VERR_NOT_EQUAL;
    }

    Assert(!pAttr2->fVisited);
    pAttr2->fVisited = true;
    pEquals->cAttributes2++;

    /*
     * Compare them.
     */
    if (RTStrICmp(pAttr1->pszValue, pAttr2->pszValue))
    {
        if (*pEquals->pszCurEntry)
            RTStrPrintf(pEquals->pszError, pEquals->cbError,
                        "Attribute '%s' on '%s' does not match ('%s' vs. '%s')",
                        pAttr1->szName, pEquals->pszCurEntry, pAttr1->pszValue, pAttr2->pszValue);
        else
            RTStrPrintf(pEquals->pszError, pEquals->cbError,
                        "Attribute '%s' does not match ('%s' vs. '%s')",
                        pAttr1->szName, pAttr1->pszValue, pAttr2->pszValue);
        return VERR_NOT_EQUAL;
    }

    return 0;
}


/**
 * @callback_method_impl{FNRTSTRSPACECALLBACK, Prepare equals operation}
 */
DECLINLINE (int) rtManifestEntryCompare2(PRTMANIFESTEQUALS pEquals, PRTMANIFESTENTRY pEntry1, PRTMANIFESTENTRY pEntry2)
{
    /*
     * Compare the attributes.  It's a bit ugly with all this counting, but
     * how else to efficiently implement RTMANIFEST_EQUALS_IGN_MISSING_ATTRS?
     */
    pEquals->cIgnoredAttributes1 = 0;
    pEquals->cIgnoredAttributes2 = 0;
    pEquals->cAttributes2        = 0;
    pEquals->pszCurEntry         = &pEntry2->szName[0];
    pEquals->pAttributes2        = &pEntry2->Attributes;
    int rc = RTStrSpaceEnumerate(&pEntry1->Attributes, rtManifestAttributeCompare, pEquals);
    if (RT_SUCCESS(rc))
    {
        /*
         * Check that we matched all that is required.
         */
        if (   pEquals->cAttributes2 + pEquals->cIgnoredAttributes2 != pEntry2->cAttributes
            && (  !(pEquals->fFlags & RTMANIFEST_EQUALS_IGN_MISSING_ATTRS)
                || pEquals->cIgnoredAttributes1 == pEntry1->cAttributes))
            rc = RTStrSpaceEnumerate(&pEntry2->Attributes, rtManifestAttributeFindMissing2, pEquals);
    }
    return rc;
}


/**
 * @callback_method_impl{FNRTSTRSPACECALLBACK, Prepare equals operation}
 */
static DECLCALLBACK(int) rtManifestEntryCompare(PRTSTRSPACECORE pStr, void *pvUser)
{
    PRTMANIFESTEQUALS pEquals = (PRTMANIFESTEQUALS)pvUser;
    PRTMANIFESTENTRY  pEntry1 = RT_FROM_MEMBER(pStr, RTMANIFESTENTRY, StrCore);
    PRTMANIFESTENTRY  pEntry2;

    /*
     * Ignore this entry?
     */
    char const * const *ppsz = pEquals->papszIgnoreEntries;
    if (ppsz)
    {
        while (*ppsz)
        {
            if (!strcmp(*ppsz, pStr->pszString))
            {
                pEntry2 = (PRTMANIFESTENTRY)RTStrSpaceGet(&pEquals->pThis2->Entries, pStr->pszString);
                if (pEntry2)
                {
                    pEntry2->fVisited = true;
                    pEquals->cIgnoredEntries2++;
                }
                pEntry1->fVisited = true;
                return 0;
            }
            ppsz++;
        }
    }

    /*
     * Try find the entry in the other manifest.
     */
    pEntry2 = (PRTMANIFESTENTRY)RTStrSpaceGet(&pEquals->pThis2->Entries, pEntry1->StrCore.pszString);
    if (!pEntry2)
    {
        if (!(pEquals->fFlags & RTMANIFEST_EQUALS_IGN_MISSING_ENTRIES_2ND))
        {
            RTStrPrintf(pEquals->pszError, pEquals->cbError, "'%s' not found in the 2nd manifest", pEntry1->StrCore.pszString);
            return VERR_NOT_EQUAL;
        }
        pEntry1->fVisited = true;
        return VINF_SUCCESS;
    }

    Assert(!pEntry1->fVisited);
    Assert(!pEntry2->fVisited);
    pEntry1->fVisited = true;
    pEntry2->fVisited = true;
    pEquals->cEntries2++;

    return rtManifestEntryCompare2(pEquals, pEntry1, pEntry2);
}



RTDECL(int) RTManifestEqualsEx(RTMANIFEST hManifest1, RTMANIFEST hManifest2, const char * const *papszIgnoreEntries,
                               const char * const *papszIgnoreAttrs, uint32_t fFlags, char *pszError, size_t cbError)
{
    /*
     * Validate input.
     */
    AssertPtrNullReturn(pszError, VERR_INVALID_POINTER);
    if (pszError && cbError)
        *pszError = '\0';
    RTMANIFESTINT *pThis1 = hManifest1;
    RTMANIFESTINT *pThis2 = hManifest2;
    if (pThis1 != NIL_RTMANIFEST)
    {
        AssertPtrReturn(pThis1, VERR_INVALID_HANDLE);
        AssertReturn(pThis1->u32Magic == RTMANIFEST_MAGIC, VERR_INVALID_HANDLE);
    }
    if (pThis2 != NIL_RTMANIFEST)
    {
        AssertPtrReturn(pThis2, VERR_INVALID_HANDLE);
        AssertReturn(pThis2->u32Magic == RTMANIFEST_MAGIC, VERR_INVALID_HANDLE);
    }
    AssertReturn(!(fFlags & ~RTMANIFEST_EQUALS_VALID_MASK), VERR_INVALID_PARAMETER);

    /*
     * The simple cases.
     */
    if (pThis1 == pThis2)
        return VINF_SUCCESS;
    if (pThis1 == NIL_RTMANIFEST || pThis2 == NIL_RTMANIFEST)
        return VERR_NOT_EQUAL;

    /*
     * Since we have to use callback style enumeration, we have to mark the
     * entries and attributes to make sure we've covered them all.
     */
    RTStrSpaceEnumerate(&pThis1->Entries, rtManifestEntryClearVisited, NULL);
    RTStrSpaceEnumerate(&pThis2->Entries, rtManifestEntryClearVisited, NULL);
    RTStrSpaceEnumerate(&pThis1->SelfEntry.Attributes, rtManifestAttributeClearVisited, NULL);
    RTStrSpaceEnumerate(&pThis2->SelfEntry.Attributes, rtManifestAttributeClearVisited, NULL);

    RTMANIFESTEQUALS Equals;
    Equals.pThis2               = pThis2;
    Equals.fFlags               = fFlags;
    Equals.papszIgnoreEntries   = papszIgnoreEntries;
    Equals.papszIgnoreAttrs     = papszIgnoreAttrs;
    Equals.pszError             = pszError;
    Equals.cbError              = cbError;

    Equals.cIgnoredEntries2     = 0;
    Equals.cEntries2            = 0;
    Equals.cIgnoredAttributes1  = 0;
    Equals.cIgnoredAttributes2  = 0;
    Equals.cAttributes2         = 0;
    Equals.pAttributes2         = NULL;
    Equals.pszCurEntry          = NULL;

    int rc = rtManifestEntryCompare2(&Equals, &pThis1->SelfEntry, &pThis2->SelfEntry);
    if (RT_SUCCESS(rc))
        rc = RTStrSpaceEnumerate(&pThis1->Entries, rtManifestEntryCompare, &Equals);
    if (RT_SUCCESS(rc))
    {
        /*
         * Did we cover all entries of the 2nd manifest?
         */
        if (Equals.cEntries2 + Equals.cIgnoredEntries2 != pThis2->cEntries)
            rc = RTStrSpaceEnumerate(&pThis1->Entries, rtManifestEntryFindMissing2, &Equals);
    }

    return rc;
}


RTDECL(int) RTManifestEquals(RTMANIFEST hManifest1, RTMANIFEST hManifest2)
{
    return RTManifestEqualsEx(hManifest1, hManifest2,
                              NULL /*papszIgnoreEntries*/, NULL /*papszIgnoreAttrs*/,
                              0 /*fFlags*/, NULL, 0);
}


/**
 * Translates a attribyte type to a attribute name.
 *
 * @returns Attribute name for fFlags, NULL if not translatable.
 * @param   fType   The type flags.  Only one bit should be set.
 */
static const char *rtManifestTypeToAttrName(uint32_t fType)
{
    switch (fType)
    {
        case RTMANIFEST_ATTR_SIZE:      return "SIZE";
        case RTMANIFEST_ATTR_MD5:       return "MD5";
        case RTMANIFEST_ATTR_SHA1:      return "SHA1";
        case RTMANIFEST_ATTR_SHA256:    return "SHA256";
        case RTMANIFEST_ATTR_SHA512:    return "SHA512";
        default:                        return NULL;
    }
}


/**
 * Worker common to RTManifestSetAttr and RTManifestEntrySetAttr.
 *
 * @returns IPRT status code.
 * @param   pEntry              Pointer to the entry.
 * @param   pszAttr             The name of the attribute to add.
 * @param   pszValue            The value string.
 * @param   fType               The attribute type type.
 */
static int rtManifestSetAttrWorker(PRTMANIFESTENTRY pEntry, const char *pszAttr, const char *pszValue, uint32_t fType)
{
    char *pszValueCopy;
    int rc = RTStrDupEx(&pszValueCopy, pszValue);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Does the attribute exist already?
     */
    AssertCompileMemberOffset(RTMANIFESTATTR, StrCore, 0);
    PRTMANIFESTATTR pAttr = (PRTMANIFESTATTR)RTStrSpaceGet(&pEntry->Attributes, pszAttr);
    if (pAttr)
    {
        RTStrFree(pAttr->pszValue);
        pAttr->pszValue = pszValueCopy;
        pAttr->fType    = fType;
    }
    else
    {
        size_t const cbName = strlen(pszAttr) + 1;
        pAttr = (PRTMANIFESTATTR)RTMemAllocVar(RT_UOFFSETOF_DYN(RTMANIFESTATTR, szName[cbName]));
        if (!pAttr)
        {
            RTStrFree(pszValueCopy);
            return VERR_NO_MEMORY;
        }
        memcpy(pAttr->szName, pszAttr, cbName);
        pAttr->StrCore.pszString = pAttr->szName;
        pAttr->StrCore.cchString = cbName - 1;
        pAttr->pszValue = pszValueCopy;
        pAttr->fType    = fType;
        if (RT_UNLIKELY(!RTStrSpaceInsert(&pEntry->Attributes, &pAttr->StrCore)))
        {
            AssertFailed();
            RTStrFree(pszValueCopy);
            RTMemFree(pAttr);
            return VERR_INTERNAL_ERROR_4;
        }
        pEntry->cAttributes++;
    }

    return VINF_SUCCESS;
}


RTDECL(int) RTManifestSetAttr(RTMANIFEST hManifest, const char *pszAttr, const char *pszValue, uint32_t fType)
{
    RTMANIFESTINT *pThis = hManifest;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTMANIFEST_MAGIC, VERR_INVALID_HANDLE);
    AssertPtr(pszValue);
    AssertReturn(RT_IS_POWER_OF_TWO(fType) && fType < RTMANIFEST_ATTR_END, VERR_INVALID_PARAMETER);
    if (!pszAttr)
        pszAttr = rtManifestTypeToAttrName(fType);
    AssertPtr(pszAttr);

    return rtManifestSetAttrWorker(&pThis->SelfEntry, pszAttr, pszValue, fType);
}


/**
 * Worker common to RTManifestUnsetAttr and RTManifestEntryUnsetAttr.
 *
 * @returns IPRT status code.
 * @param   pEntry              Pointer to the entry.
 * @param   pszAttr             The name of the attribute to remove.
 */
static int rtManifestUnsetAttrWorker(PRTMANIFESTENTRY pEntry, const char *pszAttr)
{
    PRTSTRSPACECORE pStrCore = RTStrSpaceRemove(&pEntry->Attributes, pszAttr);
    if (!pStrCore)
        return VWRN_NOT_FOUND;
    pEntry->cAttributes--;
    rtManifestDestroyAttribute(pStrCore, NULL);
    return VINF_SUCCESS;
}


RTDECL(int) RTManifestUnsetAttr(RTMANIFEST hManifest, const char *pszAttr)
{
    RTMANIFESTINT *pThis = hManifest;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTMANIFEST_MAGIC, VERR_INVALID_HANDLE);
    AssertPtr(pszAttr);

    return rtManifestUnsetAttrWorker(&pThis->SelfEntry, pszAttr);
}


/**
 * Callback employed by rtManifestQueryAttrWorker to search by attribute type.
 *
 * @returns VINF_SUCCESS or VINF_CALLBACK_RETURN.
 * @param   pStr                The attribute string node.
 * @param   pvUser              The argument package.
 */
static DECLCALLBACK(int) rtManifestQueryAttrEnumCallback(PRTSTRSPACECORE pStr, void *pvUser)
{
    PRTMANIFESTATTR             pAttr = (PRTMANIFESTATTR)pStr;
    PRTMANIFESTQUERYATTRARGS    pArgs = (PRTMANIFESTQUERYATTRARGS)pvUser;

    if (pAttr->fType & pArgs->fType)
    {
        pArgs->pAttr = pAttr;
        return VINF_CALLBACK_RETURN;
    }
    return VINF_SUCCESS;
}


/**
 * Worker common to RTManifestQueryAttr and RTManifestEntryQueryAttr.
 *
 * @returns IPRT status code.
 * @param   pEntry              The entry.
 * @param   pszAttr             The attribute name.  If NULL, it will be
 *                              selected by @a fType alone.
 * @param   fType               The attribute types the entry should match. Pass
 *                              Pass RTMANIFEST_ATTR_ANY match any.  If more
 *                              than one is given, the first matching one is
 *                              returned.
 * @param   pszValue            Where to return value.
 * @param   cbValue             The size of the buffer @a pszValue points to.
 * @param   pfType              Where to return the attribute type value.
 */
static int rtManifestQueryAttrWorker(PRTMANIFESTENTRY pEntry, const char *pszAttr, uint32_t fType,
                                     char *pszValue, size_t cbValue, uint32_t *pfType)
{
    /*
     * Find the requested attribute.
     */
    PRTMANIFESTATTR pAttr;
    if (pszAttr)
    {
        /* By name. */
        pAttr = (PRTMANIFESTATTR)RTStrSpaceGet(&pEntry->Attributes, pszAttr);
        if (!pAttr)
            return VERR_MANIFEST_ATTR_NOT_FOUND;
        if (!(pAttr->fType & fType))
            return VERR_MANIFEST_ATTR_TYPE_MISMATCH;
    }
    else
    {
        /* By type. */
        RTMANIFESTQUERYATTRARGS Args;
        Args.fType = fType;
        Args.pAttr = NULL;
        int rc = RTStrSpaceEnumerate(&pEntry->Attributes, rtManifestQueryAttrEnumCallback, &Args);
        AssertRCReturn(rc, rc);
        pAttr = Args.pAttr;
        if (!pAttr)
            return VERR_MANIFEST_ATTR_TYPE_NOT_FOUND;
    }

    /*
     * Set the return values.
     */
    if (cbValue || pszValue)
    {
        size_t cbNeeded = strlen(pAttr->pszValue) + 1;
        if (cbNeeded > cbValue)
            return VERR_BUFFER_OVERFLOW;
        memcpy(pszValue, pAttr->pszValue, cbNeeded);
    }

    if (pfType)
        *pfType = pAttr->fType;

    return VINF_SUCCESS;
}


RTDECL(int) RTManifestQueryAttr(RTMANIFEST hManifest, const char *pszAttr, uint32_t fType,
                                char *pszValue, size_t cbValue, uint32_t *pfType)
{
    RTMANIFESTINT *pThis = hManifest;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTMANIFEST_MAGIC, VERR_INVALID_HANDLE);
    AssertPtrNull(pszAttr);
    AssertPtr(pszValue);

    return rtManifestQueryAttrWorker(&pThis->SelfEntry, pszAttr, fType, pszValue, cbValue, pfType);
}


/**
 * Callback employed by RTManifestQueryAllAttrTypes to collect attribute types.
 *
 * @returns VINF_SUCCESS.
 * @param   pStr                The attribute string node.
 * @param   pvUser              Pointer to type flags (uint32_t).
 */
static DECLCALLBACK(int) rtManifestQueryAllAttrTypesEnumAttrCallback(PRTSTRSPACECORE pStr, void *pvUser)
{
    PRTMANIFESTATTR pAttr   = (PRTMANIFESTATTR)pStr;
    uint32_t       *pfTypes = (uint32_t *)pvUser;
    *pfTypes |= pAttr->fType;
    return VINF_SUCCESS;
}


/**
 * Callback employed by RTManifestQueryAllAttrTypes to collect attribute types
 * for an entry.
 *
 * @returns VINF_SUCCESS.
 * @param   pStr                The attribute string node.
 * @param   pvUser              Pointer to type flags (uint32_t).
 */
static DECLCALLBACK(int) rtManifestQueryAllAttrTypesEnumEntryCallback(PRTSTRSPACECORE pStr, void *pvUser)
{
    PRTMANIFESTENTRY pEntry = RT_FROM_MEMBER(pStr, RTMANIFESTENTRY, StrCore);
    return RTStrSpaceEnumerate(&pEntry->Attributes, rtManifestQueryAllAttrTypesEnumAttrCallback, pvUser);
}


RTDECL(int) RTManifestQueryAllAttrTypes(RTMANIFEST hManifest, bool fEntriesOnly, uint32_t *pfTypes)
{
    RTMANIFESTINT *pThis = hManifest;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTMANIFEST_MAGIC, VERR_INVALID_HANDLE);
    AssertPtr(pfTypes);

    *pfTypes = 0;
    int rc = RTStrSpaceEnumerate(&pThis->Entries, rtManifestQueryAllAttrTypesEnumEntryCallback, pfTypes);
    if (RT_SUCCESS(rc) && fEntriesOnly)
        rc = rtManifestQueryAllAttrTypesEnumAttrCallback(&pThis->SelfEntry.StrCore, pfTypes);
    return VINF_SUCCESS;
}


/**
 * Validates the name entry.
 *
 * @returns IPRT status code.
 * @param   pszEntry            The entry name to validate.
 * @param   pfNeedNormalization Where to return whether it needs normalization
 *                              or not.  Optional.
 * @param   pcchEntry           Where to return the length.  Optional.
 */
static int rtManifestValidateNameEntry(const char *pszEntry, bool *pfNeedNormalization, size_t *pcchEntry)
{
    int         rc;
    bool        fNeedNormalization = false;
    const char *pszCur             = pszEntry;

    for (;;)
    {
        RTUNICP uc;
        rc = RTStrGetCpEx(&pszCur, &uc);
        if (RT_FAILURE(rc))
            return rc;
        if (!uc)
            break;
        if (uc == '\\')
            fNeedNormalization = true;
        else if (uc < 32 || uc == ':' || uc == '(' || uc == ')')
            return VERR_INVALID_NAME;
    }

    if (pfNeedNormalization)
        *pfNeedNormalization = fNeedNormalization;

    size_t cchEntry = pszCur - pszEntry - 1;
    if (!cchEntry)
        rc = VERR_INVALID_NAME;
    if (pcchEntry)
        *pcchEntry = cchEntry;

    return rc;
}


/**
 * Normalizes a entry name.
 *
 * @param   pszEntry            The entry name to normalize.
 */
static void rtManifestNormalizeEntry(char *pszEntry)
{
    char  ch;
    while ((ch = *pszEntry))
    {
        if (ch == '\\')
            *pszEntry = '/';
        pszEntry++;
    }
}


/**
 * Gets an entry.
 *
 * @returns IPRT status code.
 * @param   pThis               The manifest to work with.
 * @param   pszEntry            The entry name.
 * @param   fNeedNormalization  Whether rtManifestValidateNameEntry said it
 *                              needed normalization.
 * @param   cchEntry            The length of the name.
 * @param   ppEntry             Where to return the entry pointer on success.
 */
static int rtManifestGetEntry(RTMANIFESTINT *pThis, const char *pszEntry, bool fNeedNormalization, size_t cchEntry,
                              PRTMANIFESTENTRY *ppEntry)
{
    PRTMANIFESTENTRY pEntry;

    AssertCompileMemberOffset(RTMANIFESTATTR, StrCore, 0);
    if (!fNeedNormalization)
        pEntry = (PRTMANIFESTENTRY)RTStrSpaceGet(&pThis->Entries, pszEntry);
    else
    {
        char *pszCopy = (char *)RTMemTmpAlloc(cchEntry + 1);
        if (RT_UNLIKELY(!pszCopy))
            return VERR_NO_TMP_MEMORY;
        memcpy(pszCopy, pszEntry, cchEntry + 1);
        rtManifestNormalizeEntry(pszCopy);

        pEntry = (PRTMANIFESTENTRY)RTStrSpaceGet(&pThis->Entries, pszCopy);
        RTMemTmpFree(pszCopy);
    }

    *ppEntry = pEntry;
    return pEntry ? VINF_SUCCESS : VERR_NOT_FOUND;
}


RTDECL(int) RTManifestEntrySetAttr(RTMANIFEST hManifest, const char *pszEntry, const char *pszAttr,
                                   const char *pszValue, uint32_t fType)
{
    RTMANIFESTINT *pThis = hManifest;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTMANIFEST_MAGIC, VERR_INVALID_HANDLE);
    AssertPtr(pszEntry);
    AssertPtr(pszValue);
    AssertReturn(RT_IS_POWER_OF_TWO(fType) && fType < RTMANIFEST_ATTR_END, VERR_INVALID_PARAMETER);
    if (!pszAttr)
        pszAttr = rtManifestTypeToAttrName(fType);
    AssertPtr(pszAttr);

    bool    fNeedNormalization;
    size_t  cchEntry;
    int rc = rtManifestValidateNameEntry(pszEntry, &fNeedNormalization, &cchEntry);
    AssertRCReturn(rc, rc);

    /*
     * Resolve the entry, adding one if necessary.
     */
    PRTMANIFESTENTRY pEntry;
    rc = rtManifestGetEntry(pThis, pszEntry, fNeedNormalization, cchEntry, &pEntry);
    if (rc == VERR_NOT_FOUND)
    {
        pEntry = (PRTMANIFESTENTRY)RTMemAlloc(RT_UOFFSETOF_DYN(RTMANIFESTENTRY, szName[cchEntry + 1]));
        if (!pEntry)
            return VERR_NO_MEMORY;

        pEntry->StrCore.cchString = cchEntry;
        pEntry->StrCore.pszString = pEntry->szName;
        pEntry->Attributes = NULL;
        pEntry->cAttributes = 0;
        memcpy(pEntry->szName, pszEntry, cchEntry + 1);
        if (fNeedNormalization)
            rtManifestNormalizeEntry(pEntry->szName);

        if (!RTStrSpaceInsert(&pThis->Entries, &pEntry->StrCore))
        {
            RTMemFree(pEntry);
            return VERR_INTERNAL_ERROR_4;
        }
        pThis->cEntries++;
    }
    else if (RT_FAILURE(rc))
        return rc;

    return rtManifestSetAttrWorker(pEntry, pszAttr, pszValue, fType);
}


RTDECL(int) RTManifestEntryUnsetAttr(RTMANIFEST hManifest, const char *pszEntry, const char *pszAttr)
{
    RTMANIFESTINT *pThis = hManifest;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTMANIFEST_MAGIC, VERR_INVALID_HANDLE);
    AssertPtr(pszEntry);
    AssertPtr(pszAttr);

    bool    fNeedNormalization;
    size_t  cchEntry;
    int rc = rtManifestValidateNameEntry(pszEntry, &fNeedNormalization, &cchEntry);
    AssertRCReturn(rc, rc);

    /*
     * Resolve the entry and hand it over to the worker.
     */
    PRTMANIFESTENTRY pEntry;
    rc = rtManifestGetEntry(pThis, pszEntry, fNeedNormalization, cchEntry, &pEntry);
    if (RT_SUCCESS(rc))
        rc = rtManifestUnsetAttrWorker(pEntry, pszAttr);
    return rc;
}


RTDECL(int) RTManifestEntryQueryAttr(RTMANIFEST hManifest, const char *pszEntry, const char *pszAttr, uint32_t fType,
                                     char *pszValue, size_t cbValue, uint32_t *pfType)
{
    RTMANIFESTINT *pThis = hManifest;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTMANIFEST_MAGIC, VERR_INVALID_HANDLE);
    AssertPtr(pszEntry);
    AssertPtrNull(pszAttr);
    AssertPtr(pszValue);

    /*
     * Look up the entry.
     */
    bool    fNeedNormalization;
    size_t  cchEntry;
    int rc = rtManifestValidateNameEntry(pszEntry, &fNeedNormalization, &cchEntry);
    AssertRCReturn(rc, rc);

    PRTMANIFESTENTRY pEntry;
    rc = rtManifestGetEntry(pThis, pszEntry, fNeedNormalization, cchEntry, &pEntry);
    if (RT_SUCCESS(rc))
        rc = rtManifestQueryAttrWorker(pEntry, pszAttr, fType, pszValue, cbValue, pfType);
    return rc;
}


RTDECL(int) RTManifestEntryAdd(RTMANIFEST hManifest, const char *pszEntry)
{
    RTMANIFESTINT *pThis = hManifest;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTMANIFEST_MAGIC, VERR_INVALID_HANDLE);
    AssertPtr(pszEntry);

    bool    fNeedNormalization;
    size_t  cchEntry;
    int rc = rtManifestValidateNameEntry(pszEntry, &fNeedNormalization, &cchEntry);
    AssertRCReturn(rc, rc);

    /*
     * Only add one if it does not already exist.
     */
    PRTMANIFESTENTRY pEntry;
    rc = rtManifestGetEntry(pThis, pszEntry, fNeedNormalization, cchEntry, &pEntry);
    if (rc == VERR_NOT_FOUND)
    {
        pEntry = (PRTMANIFESTENTRY)RTMemAlloc(RT_UOFFSETOF_DYN(RTMANIFESTENTRY, szName[cchEntry + 1]));
        if (pEntry)
        {
            pEntry->StrCore.cchString = cchEntry;
            pEntry->StrCore.pszString = pEntry->szName;
            pEntry->Attributes = NULL;
            pEntry->cAttributes = 0;
            memcpy(pEntry->szName, pszEntry, cchEntry + 1);
            if (fNeedNormalization)
                rtManifestNormalizeEntry(pEntry->szName);

            if (RTStrSpaceInsert(&pThis->Entries, &pEntry->StrCore))
            {
                pThis->cEntries++;
                rc = VINF_SUCCESS;
            }
            else
            {
                RTMemFree(pEntry);
                rc = VERR_INTERNAL_ERROR_4;
            }
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else if (RT_SUCCESS(rc))
        rc = VWRN_ALREADY_EXISTS;

    return rc;
}


RTDECL(int) RTManifestEntryRemove(RTMANIFEST hManifest, const char *pszEntry)
{
    RTMANIFESTINT *pThis = hManifest;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTMANIFEST_MAGIC, VERR_INVALID_HANDLE);
    AssertPtr(pszEntry);

    bool    fNeedNormalization;
    size_t  cchEntry;
    int rc = rtManifestValidateNameEntry(pszEntry, &fNeedNormalization, &cchEntry);
    AssertRCReturn(rc, rc);

    /*
     * Look it up before removing it.
     */
    PRTMANIFESTENTRY pEntry;
    rc = rtManifestGetEntry(pThis, pszEntry, fNeedNormalization, cchEntry, &pEntry);
    if (RT_SUCCESS(rc))
    {
        PRTSTRSPACECORE pStrCore = RTStrSpaceRemove(&pThis->Entries, pEntry->StrCore.pszString);
        AssertReturn(pStrCore, VERR_INTERNAL_ERROR_3);
        pThis->cEntries--;
        rtManifestDestroyEntry(pStrCore, pThis);
    }

    return rc;
}


RTDECL(bool) RTManifestEntryExists(RTMANIFEST hManifest, const char *pszEntry)
{
    RTMANIFESTINT *pThis = hManifest;
    AssertPtrReturn(pThis, false);
    AssertReturn(pThis->u32Magic == RTMANIFEST_MAGIC, false);
    AssertPtr(pszEntry);

    bool    fNeedNormalization;
    size_t  cchEntry;
    int rc = rtManifestValidateNameEntry(pszEntry, &fNeedNormalization, &cchEntry);
    AssertRCReturn(rc, false);

    /*
     * Check if it exists.
     */
    PRTMANIFESTENTRY pEntry;
    rc = rtManifestGetEntry(pThis, pszEntry, fNeedNormalization, cchEntry, &pEntry);
    return RT_SUCCESS_NP(rc);
}


/**
 * Reads a line from a VFS I/O stream.
 *
 * @todo    Replace this with a buffered I/O stream layer.
 *
 * @returns IPRT status code.  VERR_EOF when trying to read beyond the stream
 *          end.
 * @param   hVfsIos             The I/O stream to read from.
 * @param   pszLine             Where to store what we've read.
 * @param   cbLine              The number of bytes to read.
 */
static int rtManifestReadLine(RTVFSIOSTREAM hVfsIos, char *pszLine, size_t cbLine)
{
    /* This is horribly slow right now, but it's not a biggy as the input is
       usually cached in memory somewhere... */
    *pszLine = '\0';
    while (cbLine > 1)
    {
        char ch;
        int rc = RTVfsIoStrmRead(hVfsIos, &ch, 1, true /*fBLocking*/, NULL);
        if (RT_FAILURE(rc))
            return rc;

        /* \r\n */
        if (ch == '\r')
        {
            if (cbLine <= 2)
            {
                pszLine[0] = ch;
                pszLine[1] = '\0';
                return VINF_BUFFER_OVERFLOW;
            }

            rc = RTVfsIoStrmRead(hVfsIos, &ch, 1, true /*fBLocking*/, NULL);
            if (RT_SUCCESS(rc) && ch == '\n')
                return VINF_SUCCESS;
            pszLine[0] = '\r';
            pszLine[1] = ch;
            pszLine[2] = '\0';
            if (RT_FAILURE(rc))
                return rc == VERR_EOF ? VINF_EOF : rc;
        }

        /* \n */
        if (ch == '\n')
            return VINF_SUCCESS;

        /* add character. */
        pszLine[0] = ch;
        pszLine[1] = '\0';

        /* advance */
        pszLine++;
        cbLine--;
    }

    return VINF_BUFFER_OVERFLOW;
}


RTDECL(int) RTManifestReadStandardEx(RTMANIFEST hManifest, RTVFSIOSTREAM hVfsIos, char *pszErr, size_t cbErr)
{
    /*
     * Validate input.
     */
    AssertPtrNull(pszErr);
    if (pszErr && cbErr)
        *pszErr = '\0';
    RTMANIFESTINT *pThis = hManifest;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTMANIFEST_MAGIC, VERR_INVALID_HANDLE);

    /*
     * Process the stream line by line.
     */
    uint32_t iLine = 0;
    for (;;)
    {
        /*
         * Read a line from the input stream.
         */
        iLine++;
        char szLine[RTPATH_MAX + RTSHA512_DIGEST_LEN + 32];
        int rc = rtManifestReadLine(hVfsIos, szLine, sizeof(szLine));
        if (RT_FAILURE(rc))
        {
            if (rc == VERR_EOF)
                return VINF_SUCCESS;
            RTStrPrintf(pszErr, cbErr, "Error reading line #%u: %Rrc", iLine, rc);
            return rc;
        }
        if (rc != VINF_SUCCESS)
        {
            RTStrPrintf(pszErr, cbErr, "Line number %u is too long", iLine);
            return VERR_OUT_OF_RANGE;
        }

        /*
         * Strip it and skip if empty.
         */
        char *psz = RTStrStrip(szLine);
        if (!*psz)
            continue;

        /*
         * Read the attribute name.
         */
        char ch;
        const char * const pszAttr = psz;
        do
            psz++;
        while (!RT_C_IS_BLANK((ch = *psz)) && ch && ch != '(');
        if (ch)
            *psz++ = '\0';

        /*
         * The entry name is enclosed in parenthesis and followed by a '='.
         */
        if (ch != '(')
        {
            psz = RTStrStripL(psz);
            ch = *psz++;
            if (ch != '(')
            {
                RTStrPrintf(pszErr, cbErr, "Expected '(' after %zu on line %u", psz - szLine - 1, iLine);
                return VERR_PARSE_ERROR;
            }
        }
        const char * const pszName = psz;
        while ((ch = *psz) != '\0')
        {
            if (ch == ')')
            {
                char *psz2 = RTStrStripL(psz + 1);
                if (*psz2 == '=')
                {
                    *psz = '\0';
                    psz = psz2;
                    break;
                }
            }
            psz++;
        }

        if (*psz != '=')
        {
            RTStrPrintf(pszErr, cbErr, "Expected ')=' at %zu on line %u", psz - szLine, iLine);
            return VERR_PARSE_ERROR;
        }

        /*
         * The value.
         */
        psz = RTStrStrip(psz + 1);
        const char * const pszValue = psz;
        if (!*psz)
        {
            RTStrPrintf(pszErr, cbErr, "Expected value at %zu on line %u", psz - szLine, iLine);
            return VERR_PARSE_ERROR;
        }

        /*
         * Detect attribute type and sanity check the value.
         */
        uint32_t fType = RTMANIFEST_ATTR_UNKNOWN;
        static const struct
        {
            const char *pszAttr;
            uint32_t    fType;
            unsigned    cBits;
            unsigned    uBase;
        } s_aDecAttrs[] =
        {
            { "SIZE", RTMANIFEST_ATTR_SIZE, 64,  10}
        };
        for (unsigned i = 0; i < RT_ELEMENTS(s_aDecAttrs); i++)
            if (!strcmp(s_aDecAttrs[i].pszAttr, pszAttr))
            {
                fType = s_aDecAttrs[i].fType;
                rc = RTStrToUInt64Full(pszValue, s_aDecAttrs[i].uBase, NULL);
                if (rc != VINF_SUCCESS)
                {
                    RTStrPrintf(pszErr, cbErr, "Malformed value ('%s') at %zu on line %u: %Rrc", pszValue, psz - szLine, iLine, rc);
                    return VERR_PARSE_ERROR;
                }
                break;
            }

        if (fType == RTMANIFEST_ATTR_UNKNOWN)
        {
            static const struct
            {
                const char *pszAttr;
                uint32_t    fType;
                unsigned    cchHex;
            } s_aHexAttrs[] =
            {
                { "MD5",        RTMANIFEST_ATTR_MD5,        RTMD5_DIGEST_LEN    },
                { "SHA1",       RTMANIFEST_ATTR_SHA1,       RTSHA1_DIGEST_LEN   },
                { "SHA256",     RTMANIFEST_ATTR_SHA256,     RTSHA256_DIGEST_LEN },
                { "SHA512",     RTMANIFEST_ATTR_SHA512,     RTSHA512_DIGEST_LEN }
            };
            for (unsigned i = 0; i < RT_ELEMENTS(s_aHexAttrs); i++)
                if (!strcmp(s_aHexAttrs[i].pszAttr, pszAttr))
                {
                    fType = s_aHexAttrs[i].fType;
                    for (unsigned off = 0; off < s_aHexAttrs[i].cchHex; off++)
                        if (!RT_C_IS_XDIGIT(pszValue[off]))
                        {
                            RTStrPrintf(pszErr, cbErr, "Expected hex digit at %zu on line %u (value '%s', pos %u)",
                                        pszValue - szLine + off, iLine, pszValue, off);
                            return VERR_PARSE_ERROR;
                        }
                    break;
                }
        }

        /*
         * Finally, add it.
         */
        rc = RTManifestEntrySetAttr(hManifest, pszName, pszAttr, pszValue, fType);
        if (RT_FAILURE(rc))
        {
            RTStrPrintf(pszErr, cbErr, "RTManifestEntrySetAttr(,'%s','%s', '%s', %#x) failed on line %u: %Rrc",
                        pszName, pszAttr, pszValue, fType, iLine, rc);
            return rc;
        }
    }
}

RTDECL(int) RTManifestReadStandard(RTMANIFEST hManifest, RTVFSIOSTREAM hVfsIos)
{
    return RTManifestReadStandardEx(hManifest, hVfsIos, NULL, 0);
}


/**
 * @callback_method_impl{FNRTSTRSPACECALLBACK, Writes RTMANIFESTATTR.}
 */
static DECLCALLBACK(int) rtManifestWriteStdAttr(PRTSTRSPACECORE pStr, void *pvUser)
{
    PRTMANIFESTATTR         pAttr = RT_FROM_MEMBER(pStr, RTMANIFESTATTR, StrCore);
    RTMANIFESTWRITESTDATTR *pArgs = (RTMANIFESTWRITESTDATTR *)pvUser;
    char    szLine[RTPATH_MAX + RTSHA512_DIGEST_LEN + 32];
    size_t  cchLine = RTStrPrintf(szLine, sizeof(szLine), "%s (%s) = %s\n", pAttr->szName, pArgs->pszEntry, pAttr->pszValue);
    if (cchLine >= sizeof(szLine) - 1)
        return VERR_BUFFER_OVERFLOW;
    return RTVfsIoStrmWrite(pArgs->hVfsIos, szLine, cchLine, true /*fBlocking*/, NULL);
}


/**
 * @callback_method_impl{FNRTSTRSPACECALLBACK, Writes RTMANIFESTENTRY.}
 */
static DECLCALLBACK(int) rtManifestWriteStdEntry(PRTSTRSPACECORE pStr, void *pvUser)
{
    PRTMANIFESTENTRY pEntry = RT_FROM_MEMBER(pStr, RTMANIFESTENTRY, StrCore);

    RTMANIFESTWRITESTDATTR Args;
    Args.hVfsIos  = (RTVFSIOSTREAM)pvUser;
    Args.pszEntry = pStr->pszString;
    return RTStrSpaceEnumerate(&pEntry->Attributes, rtManifestWriteStdAttr, &Args);
}


RTDECL(int) RTManifestWriteStandard(RTMANIFEST hManifest, RTVFSIOSTREAM hVfsIos)
{
    RTMANIFESTINT *pThis = hManifest;
    AssertPtrReturn(pThis, VERR_INVALID_HANDLE);
    AssertReturn(pThis->u32Magic == RTMANIFEST_MAGIC, VERR_INVALID_HANDLE);

    RTMANIFESTWRITESTDATTR Args;
    Args.hVfsIos  = hVfsIos;
    Args.pszEntry = "main";
    int rc = RTStrSpaceEnumerate(&pThis->SelfEntry.Attributes, rtManifestWriteStdAttr, &Args);
    if (RT_SUCCESS(rc))
        rc = RTStrSpaceEnumerate(&pThis->Entries, rtManifestWriteStdEntry, hVfsIos);
    return rc;
}

