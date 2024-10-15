/* $Id: tstRTList.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - List interface.
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
#include <iprt/list.h>

#include <iprt/errcore.h>
#include <iprt/mem.h>
#include <iprt/string.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef struct LISTELEM
{
    /** Test data */
    unsigned    idx;
    /** Node */
    RTLISTNODE  Node;
} LISTELEM, *PLISTELEM;


static void tstRTListOrder(RTTEST hTest, PRTLISTNODE pList, unsigned cElements,
                           unsigned idxFirst, unsigned idxLast, unsigned idxStep)
{
    RTTEST_CHECK(hTest, RTListIsEmpty(pList) == false);
    RTTEST_CHECK(hTest, RTListGetFirst(pList, LISTELEM, Node) != NULL);
    RTTEST_CHECK(hTest, RTListGetLast(pList, LISTELEM, Node) != NULL);
    if (cElements > 1)
        RTTEST_CHECK(hTest, RTListGetLast(pList, LISTELEM, Node) != RTListGetFirst(pList, LISTELEM, Node));
    else
        RTTEST_CHECK(hTest, RTListGetLast(pList, LISTELEM, Node) == RTListGetFirst(pList, LISTELEM, Node));

    /* Check that the order is right. */
    PLISTELEM pNode = RTListGetFirst(pList, LISTELEM, Node);
    for (unsigned i = idxFirst; i < idxLast; i += idxStep)
    {
        RTTEST_CHECK(hTest, pNode->idx == i);
        pNode = RTListNodeGetNext(&pNode->Node, LISTELEM, Node);
    }

    RTTEST_CHECK(hTest, pNode->idx == idxLast);
    RTTEST_CHECK(hTest, RTListGetLast(pList, LISTELEM, Node) == pNode);
    RTTEST_CHECK(hTest, RTListNodeIsLast(pList, &pNode->Node) == true);

    /* Check reverse order */
    pNode = RTListGetLast(pList, LISTELEM, Node);
    for (unsigned i = idxLast; i > idxFirst; i -= idxStep)
    {
        RTTEST_CHECK(hTest, pNode->idx == i);
        pNode = RTListNodeGetPrev(&pNode->Node, LISTELEM, Node);
    }

    RTTEST_CHECK(hTest, pNode->idx == idxFirst);
    RTTEST_CHECK(hTest, RTListGetFirst(pList, LISTELEM, Node) == pNode);
    RTTEST_CHECK(hTest, RTListNodeIsFirst(pList, &pNode->Node) == true);

    /* The list enumeration. */
    unsigned idx = idxFirst;
    RTListForEach(pList, pNode, LISTELEM, Node)
    {
        RTTEST_CHECK_RETV(hTest, idx == pNode->idx);
        idx += idxStep;
    }
    RTTEST_CHECK_MSG_RETV(hTest, idx == idxLast + idxStep || (idx == idxFirst && idxFirst == idxLast),
                          (hTest, "idx=%u idxFirst=%u idxLast=%u idxStep=%u\n", idx, idxFirst, idxLast, idxStep));

    idx = idxLast;
    RTListForEachReverse(pList, pNode, LISTELEM, Node)
    {
        RTTEST_CHECK_RETV(hTest, idx == pNode->idx);
        idx -= idxStep;
    }
    RTTEST_CHECK_MSG_RETV(hTest, idx == idxFirst - idxStep || (idx == idxLast && idxFirst == idxLast),
                          (hTest, "idx=%u idxFirst=%u idxLast=%u idxStep=%u\n", idx, idxFirst, idxLast, idxStep));
}

static void tstRTListCreate(RTTEST hTest, unsigned cElements)
{
    RTTestISubF("Creating and moving - %u elements", cElements);
    Assert(cElements > 0);

    RTLISTANCHOR ListHead;

    RTListInit(&ListHead);
    RTTEST_CHECK(hTest, RTListIsEmpty(&ListHead) == true);
    RTTEST_CHECK(hTest, RTListGetFirst(&ListHead, LISTELEM, Node) == NULL);
    RTTEST_CHECK(hTest, RTListGetLast(&ListHead, LISTELEM, Node) == NULL);

    /* Create the list */
    for (unsigned i = 0; i< cElements; i++)
    {
        PLISTELEM pNode = (PLISTELEM)RTMemAlloc(sizeof(LISTELEM));

        pNode->idx = i;
        pNode->Node.pPrev = NULL;
        pNode->Node.pNext = NULL;
        RTListAppend(&ListHead, &pNode->Node);
    }

    tstRTListOrder(hTest, &ListHead, cElements, 0, cElements-1, 1);

    /* Move the list to a new one. */
    RTLISTANCHOR ListHeadNew;
    RTListMove(&ListHeadNew, &ListHead);

    RTTEST_CHECK(hTest, RTListIsEmpty(&ListHead) == true);
    RTTEST_CHECK(hTest, RTListGetFirst(&ListHead, LISTELEM, Node) == NULL);
    RTTEST_CHECK(hTest, RTListGetLast(&ListHead, LISTELEM, Node) == NULL);

    tstRTListOrder(hTest, &ListHeadNew, cElements, 0, cElements-1, 1);

    /*
     * Safe iteration w/ removal.
     */
    RTTestISubF("Safe iteration w/ removal - %u elements", cElements);

    /* Move it element by element. */
    PLISTELEM pNode, pSafe;
    RTListForEachSafe(&ListHeadNew, pNode, pSafe, LISTELEM, Node)
    {
        RTListNodeRemove(&pNode->Node);
        RTListAppend(&ListHead, &pNode->Node);
    }
    RTTESTI_CHECK(RTListIsEmpty(&ListHeadNew) == true);
    tstRTListOrder(hTest, &ListHead, cElements, 0, cElements-1, 1);

    /* And the other way. */
    RTListForEachReverseSafe(&ListHead, pNode, pSafe, LISTELEM, Node)
    {
        RTListNodeRemove(&pNode->Node);
        RTListPrepend(&ListHeadNew, &pNode->Node);
    }
    RTTESTI_CHECK(RTListIsEmpty(&ListHead) == true);
    tstRTListOrder(hTest, &ListHeadNew, cElements, 0, cElements-1, 1);

    /*
     * Remove elements now.
     */
    if (cElements > 1)
    {
        /* Remove every second */
        RTTestISubF("Remove every second node - %u elements", cElements);

        pNode = RTListGetFirst(&ListHeadNew, LISTELEM, Node);
        for (unsigned i = 0; i < cElements; i++)
        {
            PLISTELEM pNext = RTListNodeGetNext(&pNode->Node, LISTELEM, Node);

            if (!(pNode->idx % 2))
            {
                RTListNodeRemove(&pNode->Node);
                RTMemFree(pNode);
            }

            pNode = pNext;
        }

        bool fElementsEven = (cElements % 2) == 0;
        unsigned idxEnd = fElementsEven ? cElements - 1 : cElements - 2;

        cElements /= 2;
        tstRTListOrder(hTest, &ListHeadNew, cElements, 1, idxEnd, 2);
    }

    /* Remove the rest now. */
    RTTestISubF("Remove all nodes - %u elements", cElements);
    pNode = RTListGetFirst(&ListHeadNew, LISTELEM, Node);
    for (unsigned i = 0; i < cElements; i++)
    {
        PLISTELEM pNext = RTListNodeGetNext(&pNode->Node, LISTELEM, Node);

        RTListNodeRemove(&pNode->Node);
        RTMemFree(pNode);
        pNode = pNext;
    }

    /* List should be empty again */
    RTTEST_CHECK(hTest, RTListIsEmpty(&ListHeadNew) == true);
    RTTEST_CHECK(hTest, RTListGetFirst(&ListHeadNew, LISTELEM, Node) == NULL);
    RTTEST_CHECK(hTest, RTListGetLast(&ListHeadNew, LISTELEM, Node) == NULL);
}

int main()
{
    RTTEST hTest;
    int rc = RTTestInitAndCreate("tstRTList", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    tstRTListCreate(hTest,   1);
    tstRTListCreate(hTest,   2);
    tstRTListCreate(hTest,   3);
    tstRTListCreate(hTest,  99);
    tstRTListCreate(hTest, 100);
    tstRTListCreate(hTest, 101);

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

