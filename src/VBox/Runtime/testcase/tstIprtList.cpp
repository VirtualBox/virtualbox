/* $Id: tstIprtList.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - RTCList/RTCMTList.
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
#include <iprt/cpp/mtlist.h>

#include <iprt/cpp/ministring.h>
#include <iprt/test.h>
#include <iprt/rand.h>
#include <iprt/thread.h>
#include <iprt/time.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Used for the string test. */
static const char *g_apszTestStrings[] =
{
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit.",
    "Vestibulum non turpis vel metus pellentesque tincidunt at id massa.",
    "Cras quis erat sed nulla ullamcorper molestie.",
    "Mauris ac elit turpis, id pulvinar diam.",
    "Nulla quis dolor dolor, in ultrices diam.",
    "Vivamus ac quam non ipsum vehicula tempor ac ac arcu.",
    "Aenean posuere lacus blandit erat semper eu iaculis ante eleifend.",
    "Donec quis quam a lacus interdum sollicitudin quis eu est.",
    "Morbi sed nisi a arcu commodo convallis.",
    "Aenean molestie condimentum velit, non mattis magna ultricies quis.",
    "Nulla id velit at mauris gravida mattis.",
    "Phasellus viverra velit eu urna semper in porta arcu sollicitudin.",
    "Pellentesque consequat turpis et tortor hendrerit id tempor ipsum lacinia.",
    "Cras iaculis nulla quis risus pulvinar eget tempor lectus placerat.",
    "Nullam in nulla sed sapien euismod euismod.",
    "Morbi in tortor at magna sagittis fermentum ut eu nunc.",
    "Nulla vitae ante sit amet dui molestie sagittis lacinia quis tellus.",
    "Proin iaculis lorem ultricies metus bibendum tincidunt.",
    "Sed gravida purus id risus sollicitudin ac porta orci vestibulum.",
    "Duis quis purus non ligula consectetur cursus eu interdum erat.",
    "Nullam non nunc in elit volutpat tempor in nec metus.",
    "Aliquam id purus eget enim luctus molestie.",
    "Sed id elit nec elit luctus scelerisque.",
    "Suspendisse viverra leo non ligula congue ac luctus nisl vulputate.",
    "Nulla dignissim lobortis nunc, eu tempus ipsum luctus sed.",
    "Integer vel lacus lacus, quis condimentum felis.",
    "Nulla ut lacus ac lacus gravida ultrices id sed ipsum.",
    "Etiam non purus ut augue fermentum consequat.",
    "Nam sit amet eros quis nibh blandit lacinia non posuere lectus.",
    "Sed sit amet ipsum et dolor sagittis facilisis.",
    "Ut congue nisi lacus, vel ultrices est.",
    "Donec vel erat ut justo hendrerit sodales eu eget libero.",
    "Integer a ipsum ac nunc eleifend congue convallis a urna.",
    "Sed vel eros eu lectus imperdiet vehicula.",
    "Vivamus eget turpis sed erat dapibus varius eget eu nulla.",
    "Nam id nulla non elit eleifend commodo sed ac est.",
    "Integer pulvinar dolor sodales velit pulvinar et facilisis eros scelerisque.",
    "Ut mattis arcu ut libero imperdiet in rhoncus augue sodales.",
    "Ut luctus turpis ligula, id dapibus felis.",
    "Nullam sit amet sapien eget tellus hendrerit vestibulum eget in odio.",
    "Phasellus non orci vitae mi placerat semper.",
    "Quisque pharetra aliquet velit, quis tempor magna porttitor nec.",
    "Praesent porta neque felis, vehicula facilisis odio.",
    "Maecenas ultricies ipsum eu velit laoreet faucibus.",
    "Mauris et nunc leo, et euismod quam.",
    "Phasellus a felis et justo fringilla lacinia.",
    "Vestibulum eget augue ante, ac viverra neque.",
    "Mauris pellentesque ligula quis metus elementum venenatis.",
    "Curabitur eu neque tellus, non porta sapien.",
    "Ut mattis metus id enim aliquam laoreet et sed tortor.",
    "Aenean quis nulla vitae nulla auctor lobortis a egestas turpis.",
    "Praesent vitae ante a urna porta placerat non nec eros.",
    "Donec quis neque eros, placerat adipiscing turpis.",
    "Cras sit amet sapien risus, quis euismod arcu.",
    "Integer volutpat massa eros, ac gravida mi.",
    "Nunc vitae nunc sagittis diam vulputate suscipit.",
    "Suspendisse quis mauris bibendum mauris aliquet pulvinar.",
    "Donec volutpat vestibulum ligula, eget interdum tortor malesuada sit amet.",
    "Mauris hendrerit dui non nibh varius sit amet fringilla orci pretium.",
    "Phasellus a quam tellus, auctor lacinia sapien.",
    "Sed dapibus leo vitae neque faucibus id porttitor sapien ultricies.",
    "Maecenas euismod elit nec tortor sagittis pretium.",
    "Ut tincidunt risus at erat fermentum sit amet molestie ante lacinia.",
    "Nulla non leo nec lacus sollicitudin lobortis a a nisl.",
    "Nunc vulputate erat vel libero elementum a interdum turpis malesuada.",
    "Morbi id libero turpis, a lobortis dolor.",
    "Donec vehicula imperdiet lorem, non pretium nulla tempus ut.",
    "Morbi lacinia massa id nunc tempus in blandit risus blandit.",
    "Sed feugiat orci id ipsum suscipit quis fringilla enim rutrum.",
    "Mauris suscipit lobortis urna, vel dictum justo iaculis ac.",
    "In rhoncus lectus tristique nunc blandit gravida placerat turpis rutrum.",
    "Aliquam pellentesque ornare justo, sed hendrerit metus mattis a.",
    "Nam aliquet lorem congue nisl blandit posuere.",
    "Sed lobortis interdum ipsum, ac cursus erat lacinia in.",
    "Maecenas vel tortor vel lorem facilisis interdum.",
    "Aenean porttitor massa enim, eget dignissim est.",
    "Nullam id libero lacus, mattis feugiat risus.",
    "Fusce et dolor at eros ornare auctor malesuada vel ipsum.",
    "Donec at massa sit amet lorem pellentesque interdum at ac lacus.",
    "Praesent suscipit velit at justo suscipit eu vestibulum ligula interdum.",
    "Aenean id justo nulla, vitae vulputate diam.",
    "Fusce pellentesque leo quis orci pulvinar at pellentesque tellus dictum.",
    "Ut facilisis purus at enim varius vulputate.",
    "Donec malesuada bibendum sapien, sed pretium nisi cursus quis.",
    "Mauris porttitor diam ut sapien pretium egestas.",
    "Vestibulum ut justo eu libero semper convallis vitae et velit.",
    "Quisque eleifend dapibus ligula, eu tincidunt massa rutrum at.",
    "Sed euismod diam eget enim suscipit dictum.",
    "Mauris fermentum orci eu nunc venenatis in sollicitudin tellus vestibulum.",
    "Vivamus faucibus consequat turpis, lobortis vehicula lectus gravida eget.",
    "Curabitur eu erat eu mi interdum scelerisque.",
    "Morbi consequat molestie nulla, imperdiet elementum augue sagittis vel.",
    "Sed ullamcorper velit suscipit arcu egestas quis commodo est hendrerit.",
    "Proin vitae velit ut enim sollicitudin ultrices.",
    "Curabitur posuere euismod lacus, sed volutpat erat adipiscing sit amet.",
    "Cras sit amet sem lorem, in cursus augue.",
    "Sed fermentum ultricies orci, quis hendrerit risus imperdiet et.",
    "Proin nec arcu interdum ipsum molestie vestibulum.",
    "Nulla quis quam non sem pretium scelerisque et eu velit.",
    "Donec eu tellus nisl, ac vehicula tortor."
};


/**
 * Does a list test.
 *
 * @param   T1          The list type.
 * @param   T2          The input type
 * @param   pcszDesc    The test description.
 * @param   paTestData  Pointer to the array with the test input data.
 * @param   cTestItems  The size of the input data.
 */
template<template <class, typename> class L, typename T1, typename T2, typename T3>
static void test1(const char *pcszDesc, T3 paTestData[], size_t cTestItems)
{
    RTTestISubF("%s with size of %u (items=%u)", pcszDesc, sizeof(T1), cTestItems);

    /*
     * Construction
     */

    /* Create a test list */
    L<T1, T2> testList;

    const size_t defCap = L<T1, T2>::kDefaultCapacity;
    RTTESTI_CHECK(testList.isEmpty());
    RTTESTI_CHECK(testList.size()     == 0);
    RTTESTI_CHECK(testList.capacity() == defCap);

    /*
     * Adding
     */

    /* Add the second half of the test data */
    size_t cAdded = 1;

    /* Start adding the second half of our test list */
    for (size_t i = cTestItems / 2; i < cTestItems; ++i, ++cAdded)
    {
        testList.append(paTestData[i]);
        RTTESTI_CHECK_RETV(testList.size()    == cAdded);
        RTTESTI_CHECK(testList.at(0)          == paTestData[cTestItems / 2]);
        RTTESTI_CHECK(testList[0]             == paTestData[cTestItems / 2]);
        RTTESTI_CHECK(testList.first()        == paTestData[cTestItems / 2]);
        RTTESTI_CHECK(testList.at(cAdded - 1) == paTestData[i]);
        RTTESTI_CHECK(testList[cAdded - 1]    == paTestData[i]);
        RTTESTI_CHECK(testList.last()         == paTestData[i]);
    }

    /* Check that all is correctly appended. */
    RTTESTI_CHECK_RETV(testList.size()        == cTestItems / 2);
    RTTESTI_CHECK_RETV(testList.isEmpty()     == false);
    for (size_t i = 0; i < testList.size(); ++i)
        RTTESTI_CHECK(testList.at(i) == paTestData[cTestItems / 2 + i]);

    /* Start prepending the first half of our test list. Iterate reverse to get
     * the correct sorting back. */
    for (size_t i = cTestItems / 2; i > 0; --i, ++cAdded)
    {
        testList.prepend(paTestData[i - 1]);
        RTTESTI_CHECK_RETV(testList.size()    == cAdded);
        RTTESTI_CHECK(testList.at(0)          == paTestData[i - 1]);
        RTTESTI_CHECK(testList[0]             == paTestData[i - 1]);
        RTTESTI_CHECK(testList.first()        == paTestData[i - 1]);
        RTTESTI_CHECK(testList.at(cAdded - 1) == paTestData[cTestItems - 1]);
        RTTESTI_CHECK(testList[cAdded - 1]    == paTestData[cTestItems - 1]);
        RTTESTI_CHECK(testList.last()         == paTestData[cTestItems - 1]);
    }

    /* Check that all is correctly prepended. */
    RTTESTI_CHECK_RETV(testList.size()        == cTestItems);
    RTTESTI_CHECK_RETV(testList.isEmpty()     == false);
    for (size_t i = 0; i < testList.size(); ++i)
        RTTESTI_CHECK(testList.at(i) == paTestData[i]);

    /*
     * Contains
     */
    L<T1, T2> testList2;

    /* Check full list. */
    RTTESTI_CHECK( testList.contains(paTestData[0]));
    RTTESTI_CHECK( testList.contains(paTestData[cTestItems / 2]));
    RTTESTI_CHECK( testList.contains(paTestData[cTestItems - 1]));
    RTTESTI_CHECK(!testList.contains(T1()));
    /* Check empty list. */
    RTTESTI_CHECK(!testList2.contains(paTestData[0]));
    RTTESTI_CHECK(!testList2.contains(paTestData[cTestItems / 2]));
    RTTESTI_CHECK(!testList2.contains(paTestData[cTestItems - 1]));
    RTTESTI_CHECK(!testList2.contains(T1()));

    /*
     * Copy operator
     */
    L<T1, T2> testList3(testList);

    /* Check that all is correctly appended. */
    RTTESTI_CHECK_RETV(testList3.size() == cTestItems);
    for (size_t i = 0; i < testList3.size(); ++i)
        RTTESTI_CHECK(testList3.at(i) == paTestData[i]);

    /*
     * "=" operator
     */
    L<T1, T2> testList4;
    testList4 = testList;

    /* Check that all is correctly appended. */
    RTTESTI_CHECK_RETV(testList4.size() == cTestItems);
    for (size_t i = 0; i < testList4.size(); ++i)
        RTTESTI_CHECK(testList4.at(i) == paTestData[i]);

    /*
     * Append list
     */
    testList3.append(testList4);

    /* Check that all is correctly appended. */
    RTTESTI_CHECK_RETV(testList3.size() == cTestItems * 2);
    for (size_t i = 0; i < testList3.size(); ++i)
        RTTESTI_CHECK(testList3.at(i) == paTestData[i % cTestItems]);

    /*
     * Prepend list
     */
    testList3.prepend(testList4);

    /* Check that all is correctly appended. */
    RTTESTI_CHECK_RETV(testList3.size() == cTestItems * 3);
    for (size_t i = 0; i < testList3.size(); ++i)
        RTTESTI_CHECK(testList3.at(i) == paTestData[i % cTestItems]);

    /*
     * "value" method
     */
    for (size_t i = 0; i < testList3.size(); ++i)
        RTTESTI_CHECK(testList3.value(i)       == paTestData[i % cTestItems]);
    for (size_t i = 0; i < testList3.size(); ++i)
        RTTESTI_CHECK(testList3.value(i, T1()) == paTestData[i % cTestItems]);
    RTTESTI_CHECK(testList3.value(testList3.size() + 1) == T1());       /* Invalid index */
    RTTESTI_CHECK(testList3.value(testList3.size() + 1, T1()) == T1()); /* Invalid index */

    /*
     * operator[] (reading)
     */
    for (size_t i = 0; i < testList.size(); ++i)
        RTTESTI_CHECK(testList[i] == paTestData[i]);

    /*
     * operator[] (writing)
     *
     * Replace with inverted array.
     */
    for (size_t i = 0; i < cTestItems; ++i)
        testList[i] = paTestData[cTestItems - i - 1];
    RTTESTI_CHECK_RETV(testList.size() == cTestItems);
    for (size_t i = 0; i < testList.size(); ++i)
        RTTESTI_CHECK(testList[i] == paTestData[cTestItems - i - 1]);

    /*
     * Replace
     *
     * Replace with inverted array (Must be original array when finished).
     */
    for (size_t i = 0; i < cTestItems; ++i)
        testList.replace(i, paTestData[i]);
    RTTESTI_CHECK_RETV(testList.size() == cTestItems);
    for (size_t i = 0; i < testList.size(); ++i)
        RTTESTI_CHECK(testList[i] == paTestData[i]);

    /*
     * Removing
     */

    /* Remove Range */
    testList3.removeRange(cTestItems, cTestItems * 2);
    RTTESTI_CHECK_RETV(testList3.size() == cTestItems * 2);
    for (size_t i = 0; i < testList3.size(); ++i)
        RTTESTI_CHECK(testList3.at(i) == paTestData[i % cTestItems]);

    /* Remove the first half (reverse) */
    size_t cRemoved = 1;
    for (size_t i = cTestItems / 2; i > 0; --i, ++cRemoved)
    {
        testList.removeAt(i - 1);
        RTTESTI_CHECK_RETV(testList.size() == cTestItems - cRemoved);
    }
    RTTESTI_CHECK_RETV(testList.size() == cTestItems / 2);

    /* Check that all is correctly removed and only the second part of the list
     * is still there. */
    for (size_t i = 0; i < testList.size(); ++i)
        RTTESTI_CHECK(testList.at(i) == paTestData[cTestItems / 2 + i]);

    /*
     * setCapacity
     */
    testList.setCapacity(cTestItems * 5);
    RTTESTI_CHECK(testList.capacity()  == cTestItems * 5);
    RTTESTI_CHECK_RETV(testList.size() == cTestItems / 2);

    /* As the capacity just increased, we should still have all entries from
     * the previous list. */
    for (size_t i = 0; i < testList.size(); ++i)
        RTTESTI_CHECK(testList.at(i) == paTestData[cTestItems / 2 + i]);

    /* Decrease the capacity so it will be smaller than the count of items in
     * the list. The list should be shrink automatically, but the remaining
     * items should be still valid. */
    testList.setCapacity(cTestItems / 4);
    RTTESTI_CHECK_RETV(testList.size() == cTestItems / 4);
    RTTESTI_CHECK(testList.capacity()  == cTestItems / 4);
    for (size_t i = 0; i < testList.size(); ++i)
        RTTESTI_CHECK(testList.at(i) == paTestData[cTestItems / 2 + i]);

    /* Clear all */
    testList.clear();
    RTTESTI_CHECK_RETV(testList.isEmpty());
    RTTESTI_CHECK_RETV(testList.size() == 0);
    RTTESTI_CHECK(testList.capacity()  == defCap);


    /* Copy empty lists. */
    L<T1, T2> testList5(testList);
    RTTESTI_CHECK_RETV(testList5.isEmpty());
    RTTESTI_CHECK_RETV(testList5.size() == 0);
    RTTESTI_CHECK(testList5.capacity()  == 0);

    testList5.append(paTestData[0]);
    testList5 = testList;
    RTTESTI_CHECK_RETV(testList5.isEmpty());
    RTTESTI_CHECK_RETV(testList5.size() == 0);
    RTTESTI_CHECK(testList5.capacity()  == 0);

    /*
     * Negative testing.
     */
    bool fMayPanic = RTAssertMayPanic();
    bool fQuiet    = RTAssertAreQuiet();
    RTAssertSetMayPanic(false);
    RTAssertSetQuiet(true);

    L<T1, T2> testList6;
    for (size_t i = 0; i < cTestItems; ++i)
        testList6.insert(i, paTestData[i]);
    RTTESTI_CHECK(testList6.size() == cTestItems);

    /* Insertion beyond the end of the array ends up at the end. */
    size_t cBefore = testList6.size();
    testList6.insert(cBefore + 3, paTestData[0]);
    RTTESTI_CHECK(testList6.size() == cBefore + 1);
    RTTESTI_CHECK(testList6.at(cBefore) == paTestData[0]);

    cBefore = testList6.size();
    L<T1, T2> testList7(testList6);
    testList6.insert(testList6.size() + 42, testList7);
    RTTESTI_CHECK(testList6.size() == cBefore + testList7.size());

    /* Inserting, appending or prepending a list to itself is not supported. */
    cBefore = testList6.size();
    testList6.insert(3, testList6);
    RTTESTI_CHECK(testList6.size() == cBefore);

    cBefore = testList6.size();
    testList6.append(testList6);
    RTTESTI_CHECK(testList6.size() == cBefore);

    cBefore = testList6.size();
    testList6.prepend(testList6);
    RTTESTI_CHECK(testList6.size() == cBefore);

    /* Replace does nothing if the index is bad. */
    cBefore = testList6.size();
    testList6.replace(cBefore, testList6[6]);
    RTTESTI_CHECK(testList6.size() == cBefore);

    cBefore = testList6.size();
    testList6.replace(cBefore + 64, testList6[6]);
    RTTESTI_CHECK(testList6.size() == cBefore);

    /* Indexing beyond the array returns the last element. */
    cBefore = testList6.size();
    RTTESTI_CHECK(testList6[cBefore] == testList6.last());
    RTTESTI_CHECK(testList6[cBefore + 42] == testList6.last());

    RTTESTI_CHECK(&testList6[cBefore]      == &testList6[cBefore - 1]);
    RTTESTI_CHECK(&testList6[cBefore + 42] == &testList6[cBefore - 1]);

    /* removeAt does nothing if the index is bad. */
    cBefore = testList6.size();
    testList6.removeAt(cBefore);
    RTTESTI_CHECK(testList6.size() == cBefore);

    cBefore = testList6.size();
    testList6.removeAt(cBefore + 42);
    RTTESTI_CHECK(testList6.size() == cBefore);

    L<T1, T2> testListEmpty1; RTTESTI_CHECK(!testListEmpty1.size());
    testListEmpty1.removeFirst();
    RTTESTI_CHECK(!testListEmpty1.size());

    testListEmpty1.removeLast();
    RTTESTI_CHECK(!testListEmpty1.size());

    testListEmpty1.removeAt(128);
    RTTESTI_CHECK(!testListEmpty1.size());

    /* removeRange interprets indexes beyond the end as the end of array (asserted). */
    testListEmpty1.removeRange(42, 128);
    RTTESTI_CHECK(!testListEmpty1.size());

    cBefore = testList6.size();
    testList6.removeRange(cBefore, cBefore);
    RTTESTI_CHECK(testList6.size() == cBefore);

    cBefore = testList6.size();
    testList6.removeRange(cBefore + 12, cBefore + 128);
    RTTESTI_CHECK(testList6.size() == cBefore);

    /* If end is less or equal to the start, nothing is done. */
    testListEmpty1.removeRange(128, 0);
    RTTESTI_CHECK(!testListEmpty1.size());

    cBefore = testList6.size();
    testList6.removeRange(cBefore, 0);
    RTTESTI_CHECK(testList6.size() == cBefore);

    cBefore = testList6.size();
    testList6.removeRange(0, 0);
    RTTESTI_CHECK(testList6.size() == cBefore);

    cBefore = testList6.size();
    testList6.removeRange(0, 0);
    RTTESTI_CHECK(testList6.size() == cBefore);

    RTAssertSetQuiet(fQuiet);
    RTAssertSetMayPanic(fMayPanic);
}

/* define RTCList here to see what happens without MT support ;)
 * (valgrind is the preferred tool to check). */
#define MTTEST_LIST_TYPE            RTCMTList
#define MTTEST_TYPE                 uint32_t
#define MTTEST_ITEMS                1000
#define MTTEST_ITEMS_NOT_REMOVED    100

static RTSEMEVENTMULTI g_hEvtMtTest = NIL_RTSEMEVENTMULTI;

/**
 * Thread for prepending items to a shared list.
 *
 * @param   hSelf       The thread handle.
 * @param   pvUser      The provided user data.
 */
static DECLCALLBACK(int) MtTest1ThreadProc(RTTHREAD hSelf, void *pvUser)
{
    MTTEST_LIST_TYPE<MTTEST_TYPE> *pTestList = (MTTEST_LIST_TYPE<MTTEST_TYPE> *)pvUser;
    RT_NOREF_PV(hSelf);
    RTSemEventMultiWait(g_hEvtMtTest, RT_MS_1MIN);

    /* Prepend new items at the start of the list. */
    for (size_t i = 0; i < MTTEST_ITEMS; ++i)
        pTestList->prepend(0x0);

    return VINF_SUCCESS;
}

/**
 * Thread for appending items to a shared list.
 *
 * @param   hSelf       The thread handle.
 * @param   pvUser      The provided user data.
 */
static DECLCALLBACK(int) MtTest2ThreadProc(RTTHREAD hSelf, void *pvUser)
{
    MTTEST_LIST_TYPE<MTTEST_TYPE> *pTestList = (MTTEST_LIST_TYPE<MTTEST_TYPE> *)pvUser;
    RT_NOREF_PV(hSelf);
    RTSemEventMultiWait(g_hEvtMtTest, RT_MS_1MIN);

    /* Append new items at the end of the list. */
    for (size_t i = 0; i < MTTEST_ITEMS; ++i)
        pTestList->append(0xFFFFFFFF);

    return VINF_SUCCESS;
}

/** Returns an index that is safe from the removal thread. */
static uint32_t MtTestSafeRandomIndex(MTTEST_LIST_TYPE<MTTEST_TYPE> *pTestList)
{
    uint32_t cItems = (uint32_t)pTestList->size();
    if (cItems > MTTEST_ITEMS)
    {
        cItems -= MTTEST_ITEMS;
        if (cItems < MTTEST_ITEMS_NOT_REMOVED)
            cItems = MTTEST_ITEMS_NOT_REMOVED;
    }
    else if (cItems > MTTEST_ITEMS_NOT_REMOVED)
        cItems = MTTEST_ITEMS_NOT_REMOVED;
    else if (cItems <= 1)
        return 0;
    return RTRandU32Ex(0, cItems - 1);
}

/**
 * Thread for inserting items to a shared list.
 *
 * @param   hSelf       The thread handle.
 * @param   pvUser      The provided user data.
 */
static DECLCALLBACK(int) MtTest3ThreadProc(RTTHREAD hSelf, void *pvUser)
{
    MTTEST_LIST_TYPE<MTTEST_TYPE> *pTestList = (MTTEST_LIST_TYPE<MTTEST_TYPE> *)pvUser;
    RT_NOREF_PV(hSelf);
    RTSemEventMultiWait(g_hEvtMtTest, RT_MS_1MIN);

    /* Insert new items in the middle of the list. */
    for (size_t i = 0; i < MTTEST_ITEMS; ++i)
        pTestList->insert(MtTestSafeRandomIndex(pTestList), 0xF0F0F0F0);

    return VINF_SUCCESS;
}

/**
 * Thread for reading items from a shared list.
 *
 * @param   hSelf       The thread handle.
 * @param   pvUser      The provided user data.
 */
static DECLCALLBACK(int) MtTest4ThreadProc(RTTHREAD hSelf, void *pvUser)
{
    MTTEST_LIST_TYPE<MTTEST_TYPE> *pTestList = (MTTEST_LIST_TYPE<MTTEST_TYPE> *)pvUser;
    RT_NOREF_PV(hSelf);
    RTSemEventMultiWait(g_hEvtMtTest, RT_MS_1MIN);

    MTTEST_TYPE a;
    /* Try to read C items from random places. */
    for (size_t i = 0; i < MTTEST_ITEMS; ++i)
    {
        /* Make sure there is at least one item in the list. */
        while (pTestList->isEmpty())
            RTThreadYield();
        a = pTestList->at(MtTestSafeRandomIndex(pTestList));
    }

    return VINF_SUCCESS;
}

/**
 * Thread for replacing items in a shared list.
 *
 * @param   hSelf       The thread handle.
 * @param   pvUser      The provided user data.
 */
static DECLCALLBACK(int) MtTest5ThreadProc(RTTHREAD hSelf, void *pvUser)
{
    MTTEST_LIST_TYPE<MTTEST_TYPE> *pTestList = (MTTEST_LIST_TYPE<MTTEST_TYPE> *)pvUser;
    RT_NOREF_PV(hSelf);
    RTSemEventMultiWait(g_hEvtMtTest, RT_MS_1MIN);

    /* Try to replace C items from random places. */
    for (size_t i = 0; i < MTTEST_ITEMS; ++i)
    {
        /* Make sure there is at least one item in the list. */
        while (pTestList->isEmpty())
            RTThreadYield();
        pTestList->replace(MtTestSafeRandomIndex(pTestList), 0xFF00FF00);
    }

    return VINF_SUCCESS;
}

/**
 * Thread for erasing items from a shared list.
 *
 * @param   hSelf       The thread handle.
 * @param   pvUser      The provided user data.
 */
static DECLCALLBACK(int) MtTest6ThreadProc(RTTHREAD hSelf, void *pvUser)
{
    MTTEST_LIST_TYPE<MTTEST_TYPE> *pTestList = (MTTEST_LIST_TYPE<MTTEST_TYPE> *)pvUser;
    RT_NOREF_PV(hSelf);
    RTSemEventMultiWait(g_hEvtMtTest, RT_MS_1MIN);

    /* Try to delete items from random places. */
    for (size_t i = 0; i < MTTEST_ITEMS; ++i)
    {
        /* Removal is racing thread 4 and 5, so, make sure we don't */
        while (pTestList->size() <= MTTEST_ITEMS_NOT_REMOVED)
            RTThreadYield();
        pTestList->removeAt(RTRandU32Ex(0, (uint32_t)pTestList->size() - 1));
    }

    return VINF_SUCCESS;
}

/**
 * Does a multi-threading list test. Several list additions, reading, replacing
 * and erasing are done simultaneous.
 *
 */
static void test2()
{
    RTTestISubF("MT test with 6 threads (%u tests per thread).", MTTEST_ITEMS);

    MTTEST_LIST_TYPE<MTTEST_TYPE>  testList;
    RTTHREAD                    ahThreads[6];
    static struct CLANG11WEIRDNESS { PFNRTTHREAD pfn; } aThreads[6] =
    {
        {MtTest1ThreadProc}, {MtTest2ThreadProc}, {MtTest3ThreadProc}, {MtTest4ThreadProc}, {MtTest5ThreadProc}, {MtTest6ThreadProc}
    };

    RTTESTI_CHECK_RC_RETV(RTSemEventMultiCreate(&g_hEvtMtTest), VINF_SUCCESS);

    for (unsigned i = 0; i < RT_ELEMENTS(ahThreads); i++)
    {
        RTTESTI_CHECK_RC_RETV(RTThreadCreateF(&ahThreads[i], aThreads[i].pfn, &testList, 0,
                                              RTTHREADTYPE_DEFAULT, RTTHREADFLAGS_WAITABLE, "mttest%u", i), VINF_SUCCESS);
    }

    RTTESTI_CHECK_RC(RTSemEventMultiSignal(g_hEvtMtTest), VINF_SUCCESS);
    uint64_t tsMsDeadline = RTTimeMilliTS() + RT_MS_1MIN;
    for (unsigned i = 0; i < RT_ELEMENTS(ahThreads); i++)
    {
        uint64_t tsNow = RTTimeMilliTS();
        uint32_t cWait = tsNow > tsMsDeadline ? 5000 : tsMsDeadline - tsNow;
        RTTESTI_CHECK_RC(RTThreadWait(ahThreads[i], cWait, NULL), VINF_SUCCESS);
    }
    RTTESTI_CHECK_RC(RTSemEventMultiDestroy(g_hEvtMtTest), VINF_SUCCESS);
    g_hEvtMtTest = NIL_RTSEMEVENTMULTI;

    RTTESTI_CHECK_RETV(testList.size() == MTTEST_ITEMS * 2);
    for (size_t i = 0; i < testList.size(); ++i)
    {
        uint32_t a = testList.at(i);
        RTTESTI_CHECK(a == 0x0 || a == 0xFFFFFFFF || a == 0xF0F0F0F0 || a == 0xFF00FF00);
    }
}

int main()
{
    /* How many integer test items should be created. */
    static const size_t s_cTestCount = 1000;

    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstIprtList", &hTest);
    if (rcExit)
        return rcExit;
    RTTestBanner(hTest);

    /*
     * Native types.
     */
    uint8_t au8TestInts[s_cTestCount];
    for (size_t i = 0; i < RT_ELEMENTS(au8TestInts); ++i)
        au8TestInts[i] = (uint8_t)RTRandU32Ex(1, UINT8_MAX);
    test1<RTCList,   uint8_t, uint8_t, uint8_t>("ST: Native type", au8TestInts, RT_ELEMENTS(au8TestInts));
    test1<RTCMTList, uint8_t, uint8_t, uint8_t>("MT: Native type", au8TestInts, RT_ELEMENTS(au8TestInts));

    uint16_t au16TestInts[s_cTestCount];
    for (size_t i = 0; i < RT_ELEMENTS(au16TestInts); ++i)
        au16TestInts[i] = (uint16_t)RTRandU32Ex(1, UINT16_MAX);
    test1<RTCList,   uint16_t, uint16_t, uint16_t>("ST: Native type", au16TestInts, RT_ELEMENTS(au16TestInts));
    test1<RTCMTList, uint16_t, uint16_t, uint16_t>("MT: Native type", au16TestInts, RT_ELEMENTS(au16TestInts));

    uint32_t au32TestInts[s_cTestCount];
    for (size_t i = 0; i < RT_ELEMENTS(au32TestInts); ++i)
        au32TestInts[i] = RTRandU32Ex(1, UINT32_MAX);
    test1<RTCList,   uint32_t, uint32_t, uint32_t>("ST: Native type", au32TestInts, RT_ELEMENTS(au32TestInts));
    test1<RTCMTList, uint32_t, uint32_t, uint32_t>("MT: Native type", au32TestInts, RT_ELEMENTS(au32TestInts));

    /*
     * Specialized type.
     */
    uint64_t au64TestInts[s_cTestCount];
    for (size_t i = 0; i < RT_ELEMENTS(au64TestInts); ++i)
        au64TestInts[i] = RTRandU64Ex(1, UINT64_MAX);
    test1<RTCList,   uint64_t, uint64_t, uint64_t>("ST: Specialized type", au64TestInts, RT_ELEMENTS(au64TestInts));
    test1<RTCMTList, uint64_t, uint64_t, uint64_t>("MT: Specialized type", au64TestInts, RT_ELEMENTS(au64TestInts));

    /*
     * Big size type (translate to internal pointer list).
     */
    test1<RTCList,   RTCString, RTCString *, const char *>("ST: Class type", g_apszTestStrings, RT_ELEMENTS(g_apszTestStrings));
    test1<RTCMTList, RTCString, RTCString *, const char *>("MT: Class type", g_apszTestStrings, RT_ELEMENTS(g_apszTestStrings));

    /*
     * Multi-threading test.
     */
    test2();

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

