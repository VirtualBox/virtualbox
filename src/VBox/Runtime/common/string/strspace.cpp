/* $Id: strspace.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - Unique String Spaces.
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
#include <iprt/string.h>
#include "internal/iprt.h"

#include <iprt/assert.h>
#include "internal/strhash.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/*
 * AVL configuration.
 */
#define KAVL_DECL(a_Type)           static a_Type
#define KAVL_FN(a)                  rtstrspace##a
#define KAVL_MAX_STACK              27  /* Up to 2^24 nodes. */
#define KAVL_EQUAL_ALLOWED          1
#define KAVLNODECORE                RTSTRSPACECORE
#define PKAVLNODECORE               PRTSTRSPACECORE
#define PPKAVLNODECORE              PPRTSTRSPACECORE
#define KAVLKEY                     uint32_t
#define PKAVLKEY                    uint32_t *

#define PKAVLCALLBACK               PFNRTSTRSPACECALLBACK

/*
 * AVL Compare macros
 */
#define KAVL_G(key1, key2)          (key1 >  key2)
#define KAVL_E(key1, key2)          (key1 == key2)
#define KAVL_NE(key1, key2)         (key1 != key2)


/*
 * Include the code.
 */
#define SSToDS(ptr) ptr
#define KMAX RT_MAX
#define kASSERT Assert
#include "../table/avl_Base.cpp.h"
#include "../table/avl_Get.cpp.h"
#include "../table/avl_DoWithAll.cpp.h"
#include "../table/avl_Destroy.cpp.h"



RTDECL(bool) RTStrSpaceInsert(PRTSTRSPACE pStrSpace, PRTSTRSPACECORE pStr)
{
    pStr->Key = sdbm(pStr->pszString, &pStr->cchString);
    PRTSTRSPACECORE pMatch = KAVL_FN(Get)(pStrSpace, pStr->Key);
    if (!pMatch)
        return KAVL_FN(Insert)(pStrSpace, pStr);

    /* Check for clashes. */
    for (PRTSTRSPACECORE pCur = pMatch; pCur; pCur = pCur->pList)
        if (    pCur->cchString == pStr->cchString
            &&  !memcmp(pCur->pszString, pStr->pszString, pStr->cchString))
            return false;
    pStr->pList = pMatch->pList;
    pMatch->pList = pStr;
    return true;
}
RT_EXPORT_SYMBOL(RTStrSpaceInsert);


RTDECL(PRTSTRSPACECORE) RTStrSpaceRemove(PRTSTRSPACE pStrSpace, const char *pszString)
{
    size_t  cchString;
    KAVLKEY Key = sdbm(pszString, &cchString);
    PRTSTRSPACECORE pCur = KAVL_FN(Get)(pStrSpace, Key);
    if (!pCur)
        return NULL;

    /* find the right one. */
    PRTSTRSPACECORE pPrev = NULL;
    for (; pCur; pPrev = pCur, pCur = pCur->pList)
        if (    pCur->cchString == cchString
            && !memcmp(pCur->pszString, pszString, cchString))
            break;
    if (pCur)
    {
        if (pPrev)
            /* simple, it's in the linked list. */
            pPrev->pList = pCur->pList;
        else
        {
            /* in the tree. remove and reinsert list head. */
            PRTSTRSPACECORE pInsert = pCur->pList;
            pCur->pList = NULL;
            pCur = KAVL_FN(Remove)(pStrSpace, Key);
            Assert(pCur);
            if (pInsert)
            {
                PRTSTRSPACECORE pList = pInsert->pList;
                bool fRc = KAVL_FN(Insert)(pStrSpace, pInsert);
                Assert(fRc); NOREF(fRc);
                pInsert->pList = pList;
            }
        }
    }

    return pCur;
}
RT_EXPORT_SYMBOL(RTStrSpaceRemove);


RTDECL(PRTSTRSPACECORE) RTStrSpaceGet(PRTSTRSPACE pStrSpace, const char *pszString)
{
    size_t  cchString;
    KAVLKEY Key = sdbm(pszString, &cchString);
    PRTSTRSPACECORE pCur = KAVL_FN(Get)(pStrSpace, Key);
    if (!pCur)
        return NULL;

    /* Linear search. */
    for (; pCur; pCur = pCur->pList)
        if (    pCur->cchString == cchString
            && !memcmp(pCur->pszString, pszString, cchString))
            return pCur;
    return NULL;
}
RT_EXPORT_SYMBOL(RTStrSpaceGet);


RTDECL(PRTSTRSPACECORE) RTStrSpaceGetN(PRTSTRSPACE pStrSpace, const char *pszString, size_t cchMax)
{
    size_t  cchString;
    KAVLKEY Key = sdbmN(pszString, cchMax, &cchString);
    PRTSTRSPACECORE pCur = KAVL_FN(Get)(pStrSpace, Key);
    if (!pCur)
        return NULL;

    /* Linear search. */
    for (; pCur; pCur = pCur->pList)
        if (    pCur->cchString == cchString
            && !memcmp(pCur->pszString, pszString, cchString))
            return pCur;
    return NULL;
}
RT_EXPORT_SYMBOL(RTStrSpaceGetN);


RTDECL(int) RTStrSpaceEnumerate(PRTSTRSPACE pStrSpace, PFNRTSTRSPACECALLBACK pfnCallback, void *pvUser)
{
    return KAVL_FN(DoWithAll)(pStrSpace, true, pfnCallback, pvUser);
}
RT_EXPORT_SYMBOL(RTStrSpaceEnumerate);


RTDECL(int) RTStrSpaceDestroy(PRTSTRSPACE pStrSpace, PFNRTSTRSPACECALLBACK pfnCallback, void *pvUser)
{
    return KAVL_FN(Destroy)(pStrSpace, pfnCallback, pvUser);
}
RT_EXPORT_SYMBOL(RTStrSpaceDestroy);

