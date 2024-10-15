/* $Id: tstVector.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - Vector container structure.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#include <iprt/test.h>
#include <iprt/vector.h>

#include <stdlib.h>  /* For realloc */

/** Counter of the number of delete calls made so far */
static unsigned s_cDeleteCalls = 0;

/** Record the argument of the delete function here. */
static void *s_apvDeleteArg[10];

/** Dummy delete function for vector-of-void pointer elements */
static void deletePVoid(void **ppv)
{
    if (s_cDeleteCalls < RT_ELEMENTS(s_apvDeleteArg))
        s_apvDeleteArg[s_cDeleteCalls] = *ppv;
    ++s_cDeleteCalls;
}

/** Dummy delete by value function for vector-of-void pointer elements */
static void deletePVoidValue(void *pv)
{
    if (s_cDeleteCalls < RT_ELEMENTS(s_apvDeleteArg))
        s_apvDeleteArg[s_cDeleteCalls] = pv;
    ++s_cDeleteCalls;
}

/* Start by instantiating each function once for syntax checking */
#ifdef __clang__
# pragma GCC diagnostic ignored "-Wunused-function" /* https://llvm.org/bugs/show_bug.cgi?id=22712 */
#endif
RTVEC_DECL_STRUCT(tstInstance, void *)
RTVEC_DECL_STRUCT(tstInstance2, void *)

RTVEC_DECLFN_DELETE_ADAPTER_ID(tstInstance, void *)
RTVEC_DECLFN_DELETE_ADAPTER_TO_VALUE(tstInstance, void *)

RTVEC_DECLFN_SIZE(tstInstance, void *)
RTVEC_DECLFN_RESERVE(tstInstance, void *, rtvecReallocDefTag)
RTVEC_DECLFN_BEGIN(tstInstance, void *)
RTVEC_DECLFN_END(tstInstance, void *)
RTVEC_DECLFN_PUSHBACK(tstInstance, void *)
RTVEC_DECLFN_POPBACK(tstInstance)
RTVEC_DECLFN_POPBACK_DELETE(tstInstance2, void *, deletePVoid, tstInstanceDeleteAdapterId)
RTVEC_DECLFN_CLEAR(tstInstance)
RTVEC_DECLFN_CLEAR_DELETE(tstInstance2, deletePVoid, tstInstanceDeleteAdapterId)
RTVEC_DECLFN_DETACH(tstInstance, void *)

RTVEC_DECL(tstSimple, void *)

static void testVectorSimple(void)
{
    RTTestISub("Vector structure, no cleanup callback");

    struct tstSimple myVec = RTVEC_INITIALIZER;
    void **ppvVal;

    RTTESTI_CHECK(tstSimpleSize(&myVec) == 0);

    ppvVal = tstSimplePushBack(&myVec);
    /* AssertPtrReturnVoid(ppvVal); */
    RTTESTI_CHECK(ppvVal == tstSimpleBegin(&myVec));
    RTTESTI_CHECK(ppvVal + 1 == tstSimpleEnd(&myVec));
    RTTESTI_CHECK(tstSimpleSize(&myVec) == 1);
    *ppvVal = (void *)1;

    ppvVal = tstSimplePushBack(&myVec);
    /* AssertPtrReturnVoid(ppvVal); */
    RTTESTI_CHECK(ppvVal - 1 == tstSimpleBegin(&myVec));
    RTTESTI_CHECK(ppvVal + 1 == tstSimpleEnd(&myVec));
    RTTESTI_CHECK(tstSimpleSize(&myVec) == 2);
    RTTESTI_CHECK(ppvVal[-1] == (void *)1);

    *ppvVal = (void *)3;
    ppvVal = tstSimplePushBack(&myVec);
    /* AssertPtrReturnVoid(ppvVal); */
    RTTESTI_CHECK(ppvVal - 2 == tstSimpleBegin(&myVec));
    RTTESTI_CHECK(ppvVal + 1 == tstSimpleEnd(&myVec));
    RTTESTI_CHECK(tstSimpleSize(&myVec) == 3);
    RTTESTI_CHECK(ppvVal[-2] == (void *)1);
    RTTESTI_CHECK(ppvVal[-1] == (void *)3);

    tstSimplePopBack(&myVec);
    RTTESTI_CHECK(tstSimpleBegin(&myVec) + 2 == tstSimpleEnd(&myVec));
    RTTESTI_CHECK(*tstSimpleBegin(&myVec) == (void *)1);
    RTTESTI_CHECK(*(tstSimpleEnd(&myVec) - 1) == (void *)3);

    tstSimpleClear(&myVec);
    RTTESTI_CHECK(tstSimpleBegin(&myVec) == tstSimpleEnd(&myVec));
    ppvVal = tstSimplePushBack(&myVec);
    /* AssertPtrReturnVoid(ppvVal); */
    RTTESTI_CHECK(ppvVal == tstSimpleBegin(&myVec));
    RTTESTI_CHECK(ppvVal + 1 == tstSimpleEnd(&myVec));

    tstSimpleClear(&myVec);
    ppvVal = tstSimplePushBack(&myVec);
    /* AssertPtrReturnVoid(ppvVal); */
    *ppvVal = (void *)1;
    ppvVal = tstSimplePushBack(&myVec);
    /* AssertPtrReturnVoid(ppvVal); */
    *ppvVal = (void *)3;
    ppvVal = tstSimplePushBack(&myVec);
    /* AssertPtrReturnVoid(ppvVal); */
    *ppvVal = (void *)2;
    ppvVal = tstSimpleDetach(&myVec);
    RTTESTI_CHECK(tstSimpleBegin(&myVec) == NULL);
    RTTESTI_CHECK(tstSimpleSize(&myVec) == 0);
    RTTESTI_CHECK(ppvVal[0] == (void *)1);
    RTTESTI_CHECK(ppvVal[1] == (void *)3);
    RTTESTI_CHECK(ppvVal[2] == (void *)2);

    RTMemFree(ppvVal);  /** @todo there is no delete vector thing. */
}

RTVEC_DECL_DELETE(tstDelete, void *, deletePVoid)

static void testVectorDelete(void)
{
    RTTestISub("Vector structure with cleanup by pointer callback");

    struct tstDelete myVec = RTVEC_INITIALIZER;
    void **ppvVal;

    ppvVal = tstDeletePushBack(&myVec);
    /* AssertPtrReturnVoid(ppvVal); */
    *ppvVal = (void *)1;
    ppvVal = tstDeletePushBack(&myVec);
    /* AssertPtrReturnVoid(ppvVal); */
    *ppvVal = (void *)3;
    ppvVal = tstDeletePushBack(&myVec);
    /* AssertPtrReturnVoid(ppvVal); */
    *ppvVal = (void *)2;

    s_cDeleteCalls = 0;
    tstDeletePopBack(&myVec);
    RTTESTI_CHECK(s_cDeleteCalls == 1);
    RTTESTI_CHECK(s_apvDeleteArg[0] == (void *)2);
    RTTESTI_CHECK(tstDeleteBegin(&myVec) + 2 == tstDeleteEnd(&myVec));
    RTTESTI_CHECK(*tstDeleteBegin(&myVec) == (void *)1);
    RTTESTI_CHECK(*(tstDeleteEnd(&myVec) - 1) == (void *)3);

    s_cDeleteCalls = 0;
    tstDeleteClear(&myVec);
    RTTESTI_CHECK(s_cDeleteCalls == 2);
    RTTESTI_CHECK(s_apvDeleteArg[0] == (void *)1);
    RTTESTI_CHECK(s_apvDeleteArg[1] == (void *)3);
    RTTESTI_CHECK(tstDeleteBegin(&myVec) == tstDeleteEnd(&myVec));
    ppvVal = tstDeletePushBack(&myVec);
    /* AssertPtrReturnVoid(ppvVal); */
    RTTESTI_CHECK(ppvVal == tstDeleteBegin(&myVec));
    RTTESTI_CHECK(ppvVal + 1 == tstDeleteEnd(&myVec));

    ppvVal = tstDeleteDetach(&myVec); /** @todo no delete myVec */
    RTMemFree(ppvVal);
}

RTVEC_DECL_DELETE_BY_VALUE(tstDeleteValue, void *, deletePVoidValue)

static void testVectorDeleteValue(void)
{
    RTTestISub("Vector structure with cleanup by value callback");

    struct tstDeleteValue myVec = RTVEC_INITIALIZER;
    void **ppvVal;

    ppvVal = tstDeleteValuePushBack(&myVec);
    /* AssertPtrReturnVoid(ppvVal); */
    *ppvVal = (void *)1;
    ppvVal = tstDeleteValuePushBack(&myVec);
    /* AssertPtrReturnVoid(ppvVal); */
    *ppvVal = (void *)3;
    ppvVal = tstDeleteValuePushBack(&myVec);
    /* AssertPtrReturnVoid(ppvVal); */
    *ppvVal = (void *)2;

    s_cDeleteCalls = 0;
    tstDeleteValuePopBack(&myVec);
    RTTESTI_CHECK(s_cDeleteCalls == 1);
    RTTESTI_CHECK(s_apvDeleteArg[0] == (void *)2);
    RTTESTI_CHECK(   tstDeleteValueBegin(&myVec) + 2
                  == tstDeleteValueEnd(&myVec));
    RTTESTI_CHECK(*tstDeleteValueBegin(&myVec) == (void *)1);
    RTTESTI_CHECK(*(tstDeleteValueEnd(&myVec) - 1) == (void *)3);

    s_cDeleteCalls = 0;
    tstDeleteValueClear(&myVec);
    RTTESTI_CHECK(s_cDeleteCalls == 2);
    RTTESTI_CHECK(s_apvDeleteArg[0] == (void *)1);
    RTTESTI_CHECK(s_apvDeleteArg[1] == (void *)3);
    RTTESTI_CHECK(tstDeleteValueBegin(&myVec) == tstDeleteValueEnd(&myVec));
    ppvVal = tstDeleteValuePushBack(&myVec);
    /* AssertPtrReturnVoid(ppvVal); */
    RTTESTI_CHECK(ppvVal == tstDeleteValueBegin(&myVec));
    RTTESTI_CHECK(ppvVal + 1 == tstDeleteValueEnd(&myVec));

    ppvVal = tstDeleteValueDetach(&myVec); /** @todo no delete myVec */
    RTMemFree(ppvVal);
}



int main()
{
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstVector", &hTest);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    testVectorSimple();
    testVectorDelete();
    testVectorDeleteValue();

    return RTTestSummaryAndDestroy(hTest);
}
