/* $Id: tstRTJson.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - JSON API.
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
#include <iprt/json.h>

#include <iprt/err.h>
#include <iprt/string.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static const char g_szJson[] =
    "{\n"
    "    \"integer\": 100,\n"
    "    \"number\": 22.22,\n"
    "    \"string\": \"test\",\n"
    "    \"array\": [1, 2, 3, 4, 5, \"6\"],\n"
    "    \"subobject\":\n"
    "        {\n"
    "            \"false\": false,\n"
    "            \"true\": true,\n"
    "            \"null\": null\n"
    "        }\n"
    "}\n";

/**
 * Some basic tests to detect malformed JSON.
 */
static void tstBasic(RTTEST hTest)
{
    RTTestSub(hTest, "Basic valid/malformed tests");
    static struct
    {
        const char *pszJson;
        int         iRcResult;
    } const aTests[] =
    {
        { "",              VERR_JSON_MALFORMED },
        { ",",             VERR_JSON_MALFORMED },
        { ":",             VERR_JSON_MALFORMED },
        { "   \n\t{",      VERR_JSON_MALFORMED },
        { "}",             VERR_JSON_MALFORMED },
        { "[",             VERR_JSON_MALFORMED },
        { "]",             VERR_JSON_MALFORMED },
        { "[ \"test\" : ", VERR_JSON_MALFORMED },
        { "null",          VINF_SUCCESS },
        { "true",          VINF_SUCCESS },
        { "false",         VINF_SUCCESS },
        { "100",           VINF_SUCCESS },
        { "\"test\"",      VINF_SUCCESS },
        { "{ }",           VINF_SUCCESS },
        { "[ ]",           VINF_SUCCESS },
        { "[ 100, 200 ]",  VINF_SUCCESS },
        { "{ \"1\": 1 }",  VINF_SUCCESS },
        { "{ \"1\": 1, \"2\": 2 }", VINF_SUCCESS },
        { "20", VINF_SUCCESS },
        { "-20", VINF_SUCCESS },
        { "{\"positive\":20}", VINF_SUCCESS },
        { "{\"negative\":-20}", VINF_SUCCESS },
        { "\"\\u0001\"", VINF_SUCCESS },
        { "\"\\u000\"", VERR_JSON_INVALID_UTF16_ESCAPE_SEQUENCE },
        { "\"\\u00\"", VERR_JSON_INVALID_UTF16_ESCAPE_SEQUENCE },
        { "\"\\u0\"", VERR_JSON_INVALID_UTF16_ESCAPE_SEQUENCE },
        { "\"\\u\"", VERR_JSON_INVALID_UTF16_ESCAPE_SEQUENCE },
        { "\"\\uGhKl\"", VERR_JSON_INVALID_UTF16_ESCAPE_SEQUENCE },
        { "\"\\u0000z\"", VERR_JSON_INVALID_CODEPOINT },
        { "\"\\uffff\"", VERR_JSON_INVALID_CODEPOINT },
        { "\"\\ufffe\"", VERR_JSON_INVALID_CODEPOINT },
        { "\"\\ufffd\"", VINF_SUCCESS},
        { "\"\\ufffd1\"", VINF_SUCCESS},
        { "\"\\ufffd12\"", VINF_SUCCESS},
        { "\"\\uD801\\udC37\\ud852\\uDf62\"", VINF_SUCCESS },  /* U+10437 U+24B62 */
        { "\"\\uD801 \\udC37\"", VERR_JSON_MISSING_SURROGATE_PAIR },
        { "\"\\uD801udC37\"", VERR_JSON_MISSING_SURROGATE_PAIR },
        { "\"\\uD801\"", VERR_JSON_MISSING_SURROGATE_PAIR },
        { "\"\\uD801\\\"", VERR_JSON_MISSING_SURROGATE_PAIR },
        { "\"\\uD801\\u\"", VERR_JSON_INVALID_UTF16_ESCAPE_SEQUENCE },
        { "\"\\uD801\\ud\"", VERR_JSON_INVALID_UTF16_ESCAPE_SEQUENCE },
        { "\"\\uD801\\udc\"", VERR_JSON_INVALID_UTF16_ESCAPE_SEQUENCE },
        { "\"\\uD801\\udc3\"", VERR_JSON_INVALID_UTF16_ESCAPE_SEQUENCE },
        { "\"\\uD801\\uDc37\"", VINF_SUCCESS},
        { "\"\\uDbff\\uDfff\"", VINF_SUCCESS},
        { "\"\\t\\n\\b\\f\\r\\\\\\/\"", VINF_SUCCESS},
    };
    for (unsigned iTest = 0; iTest < RT_ELEMENTS(aTests); iTest++)
    {
        RTERRINFOSTATIC ErrInfo;
        RTJSONVAL hJsonVal = NIL_RTJSONVAL;
        int rc = RTJsonParseFromString(&hJsonVal, aTests[iTest].pszJson, RTErrInfoInitStatic(&ErrInfo));
        if (rc != aTests[iTest].iRcResult)
        {
            if (RTErrInfoIsSet(&ErrInfo.Core))
                RTTestFailed(hTest, "RTJsonParseFromString() for \"%s\" failed, expected %Rrc got %Rrc\n%s",
                             aTests[iTest].pszJson, aTests[iTest].iRcResult, rc, ErrInfo.Core.pszMsg);
            else
                RTTestFailed(hTest, "RTJsonParseFromString() for \"%s\" failed, expected %Rrc got %Rrc",
                             aTests[iTest].pszJson, aTests[iTest].iRcResult, rc);
        }
        else if (rc == VERR_JSON_MALFORMED && !RTErrInfoIsSet(&ErrInfo.Core))
            RTTestFailed(hTest, "RTJsonParseFromString() did not return error info for \"%s\" failed", aTests[iTest].pszJson);
        if (RT_SUCCESS(rc))
        {
            if (hJsonVal != NIL_RTJSONVAL)
                RTJsonValueRelease(hJsonVal);
            else
                RTTestFailed(hTest, "RTJsonParseFromString() returned success but no value\n");
        }
        else if (hJsonVal != NIL_RTJSONVAL)
            RTTestFailed(hTest, "RTJsonParseFromString() failed but a JSON value was returned\n");
    }
}

/**
 * Checks that methods not indended for the given type return the correct error.
 */
static void tstCorrectnessRcForInvalidType(RTTEST hTest, RTJSONVAL hJsonVal, RTJSONVALTYPE enmType)
{
    bool fSavedMayPanic = RTAssertSetMayPanic(false);
    bool fSavedQuiet    = RTAssertSetQuiet(true);

    if (   enmType != RTJSONVALTYPE_OBJECT
        && enmType != RTJSONVALTYPE_ARRAY)
    {
        /* The iterator API should return errors. */
        RTJSONIT hJsonIt = NIL_RTJSONIT;
        RTTEST_CHECK_RC(hTest, RTJsonIteratorBegin(hJsonVal, &hJsonIt), VERR_JSON_VALUE_INVALID_TYPE);
    }

    if (enmType != RTJSONVALTYPE_ARRAY)
    {
        /* The Array access methods should return errors. */
        uint32_t cItems = 0;
        RTJSONVAL hJsonValItem = NIL_RTJSONVAL;
        RTTEST_CHECK(hTest, RTJsonValueGetArraySize(hJsonVal) == 0);
        RTTEST_CHECK_RC(hTest, RTJsonValueQueryArraySize(hJsonVal, &cItems), VERR_JSON_VALUE_INVALID_TYPE);
        RTTEST_CHECK_RC(hTest, RTJsonValueQueryByIndex(hJsonVal, 0, &hJsonValItem), VERR_JSON_VALUE_INVALID_TYPE);
    }

    if (enmType != RTJSONVALTYPE_OBJECT)
    {
        /* The object access methods should return errors. */
        RTJSONVAL hJsonValMember = NIL_RTJSONVAL;
        RTTEST_CHECK_RC(hTest, RTJsonValueQueryByName(hJsonVal, "test", &hJsonValMember), VERR_JSON_VALUE_INVALID_TYPE);
    }

    if (enmType != RTJSONVALTYPE_INTEGER)
    {
        int64_t i64Num = 0;
        RTTEST_CHECK_RC(hTest, RTJsonValueQueryInteger(hJsonVal, &i64Num), VERR_JSON_VALUE_INVALID_TYPE);
    }

    if (enmType != RTJSONVALTYPE_NUMBER)
    {
        double rdNum = 0.0;
        RTTEST_CHECK_RC(hTest, RTJsonValueQueryNumber(hJsonVal, &rdNum), VERR_JSON_VALUE_INVALID_TYPE);
    }

    if (enmType != RTJSONVALTYPE_STRING)
    {
        const char *psz = NULL;
        RTTEST_CHECK(hTest, RTJsonValueGetString(hJsonVal) == NULL);
        RTTEST_CHECK_RC(hTest, RTJsonValueQueryString(hJsonVal, &psz), VERR_JSON_VALUE_INVALID_TYPE);
    }

    RTAssertSetMayPanic(fSavedMayPanic);
    RTAssertSetQuiet(fSavedQuiet);
}

/**
 * Tests the array accessors.
 */
static void tstArray(RTTEST hTest, RTJSONVAL hJsonVal)
{
    uint32_t cItems = 0;
    RTTEST_CHECK(hTest, RTJsonValueGetArraySize(hJsonVal) == 6);
    RTTEST_CHECK_RC_OK(hTest, RTJsonValueQueryArraySize(hJsonVal, &cItems));
    RTTEST_CHECK(hTest, cItems == RTJsonValueGetArraySize(hJsonVal));

    for (uint32_t i = 1; i <= 5; i++)
    {
        int64_t i64Num = 0;
        RTJSONVAL hJsonValItem = NIL_RTJSONVAL;
        RTTEST_CHECK_RC_OK_RETV(hTest, RTJsonValueQueryByIndex(hJsonVal, i - 1, &hJsonValItem));
        RTTEST_CHECK(hTest, RTJsonValueGetType(hJsonValItem) == RTJSONVALTYPE_INTEGER);
        RTTEST_CHECK_RC_OK_RETV(hTest, RTJsonValueQueryInteger(hJsonValItem, &i64Num));
        RTTEST_CHECK(hTest, i64Num == (int64_t)i);
        RTTEST_CHECK(hTest, RTJsonValueRelease(hJsonValItem) == 1);
    }

    /* Last should be string. */
    const char *pszStr = NULL;
    RTJSONVAL hJsonValItem = NIL_RTJSONVAL;
    RTTEST_CHECK_RC_OK_RETV(hTest, RTJsonValueQueryByIndex(hJsonVal, 5, &hJsonValItem));
    RTTEST_CHECK(hTest, RTJsonValueGetType(hJsonValItem) == RTJSONVALTYPE_STRING);
    RTTEST_CHECK_RC_OK_RETV(hTest, RTJsonValueQueryString(hJsonValItem, &pszStr));
    RTTEST_CHECK(hTest, RTJsonValueGetString(hJsonValItem) == pszStr);
    RTTEST_CHECK(hTest, strcmp(pszStr, "6") == 0);
    RTTEST_CHECK(hTest, RTJsonValueRelease(hJsonValItem) == 1);
}

/**
 * Tests the iterator API for the given JSON array or object value.
 */
static void tstIterator(RTTEST hTest, RTJSONVAL hJsonVal)
{
    RTJSONIT hJsonIt = NIL_RTJSONIT;
    int rc = RTJsonIteratorBegin(hJsonVal, &hJsonIt);
    RTTEST_CHECK(hTest, RT_SUCCESS(rc));
    if (RT_SUCCESS(rc))
    {
        const char *pszName = NULL;
        RTJSONVAL hJsonValMember = NIL_RTJSONVAL;
        rc = RTJsonIteratorQueryValue(hJsonIt, &hJsonValMember, &pszName);
        RTTEST_CHECK(hTest, RT_SUCCESS(rc));
        RTTEST_CHECK(hTest, pszName != NULL);
        RTTEST_CHECK(hTest, hJsonValMember != NIL_RTJSONVAL);
        while (RT_SUCCESS(rc))
        {
            RTJSONVALTYPE enmTypeMember = RTJsonValueGetType(hJsonValMember);
            tstCorrectnessRcForInvalidType(hTest, hJsonValMember, enmTypeMember);

            switch (enmTypeMember)
            {
                case RTJSONVALTYPE_OBJECT:
                    RTTEST_CHECK(hTest, strcmp(pszName, "subobject") == 0);
                    tstIterator(hTest, hJsonValMember);
                    break;
                case RTJSONVALTYPE_ARRAY:
                    RTTEST_CHECK(hTest, strcmp(pszName, "array") == 0);
                    tstArray(hTest, hJsonValMember);
                    break;
                case RTJSONVALTYPE_STRING:
                {
                    RTTEST_CHECK(hTest, strcmp(pszName, "string") == 0);
                    const char *pszStr = NULL;
                    RTTEST_CHECK_RC_OK(hTest, RTJsonValueQueryString(hJsonValMember, &pszStr));
                    RTTEST_CHECK(hTest, strcmp(pszStr, "test") == 0);
                    break;
                }
                case RTJSONVALTYPE_INTEGER:
                {
                    RTTEST_CHECK(hTest, strcmp(pszName, "integer") == 0);
                    int64_t i64Num = 0;
                    RTTEST_CHECK_RC_OK(hTest, RTJsonValueQueryInteger(hJsonValMember, &i64Num));
                    RTTEST_CHECK(hTest, i64Num == 100);
                    break;
                }
                case RTJSONVALTYPE_NUMBER:
                {
                    RTTEST_CHECK(hTest, strcmp(pszName, "number") == 0);
                    double rdNum = 0.0;
                    RTTEST_CHECK_RC_OK(hTest, RTJsonValueQueryNumber(hJsonValMember, &rdNum));
                    double const rdExpect = 22.22;
                    RTTEST_CHECK(hTest, rdNum == rdExpect);
                    break;
                }
                case RTJSONVALTYPE_NULL:
                    RTTEST_CHECK(hTest, strcmp(pszName, "null") == 0);
                    break;
                case RTJSONVALTYPE_TRUE:
                    RTTEST_CHECK(hTest, strcmp(pszName, "true") == 0);
                    break;
                case RTJSONVALTYPE_FALSE:
                    RTTEST_CHECK(hTest, strcmp(pszName, "false") == 0);
                    break;
                default:
                    RTTestFailed(hTest, "Invalid JSON value type %u returned\n", enmTypeMember);
            }

            RTTEST_CHECK(hTest, RTJsonValueRelease(hJsonValMember) == 1);
            rc = RTJsonIteratorNext(hJsonIt);
            RTTEST_CHECK(hTest, rc == VINF_SUCCESS || rc == VERR_JSON_ITERATOR_END);
            if (RT_SUCCESS(rc))
                RTTEST_CHECK_RC_OK(hTest, RTJsonIteratorQueryValue(hJsonIt, &hJsonValMember, &pszName));
        }
        RTJsonIteratorFree(hJsonIt);
    }
}

/**
 * Test that the parser returns the correct values for a valid JSON.
 */
static void tstCorrectness(RTTEST hTest)
{
    RTTestSub(hTest, "Correctness");

    RTJSONVAL hJsonVal = NIL_RTJSONVAL;
    RTTEST_CHECK_RC_OK_RETV(hTest, RTJsonParseFromString(&hJsonVal, g_szJson, NULL));

    if (hJsonVal != NIL_RTJSONVAL)
    {
        RTJSONVALTYPE enmType = RTJsonValueGetType(hJsonVal);
        if (enmType == RTJSONVALTYPE_OBJECT)
        {
            /* Excercise the other non object APIs to return VERR_JSON_VALUE_INVALID_TYPE. */
            tstCorrectnessRcForInvalidType(hTest, hJsonVal, enmType);
            tstIterator(hTest, hJsonVal);
        }
        else
            RTTestFailed(hTest, "RTJsonParseFromString() returned an invalid JSON value, expected OBJECT got %u\n", enmType);
        RTTEST_CHECK(hTest, RTJsonValueRelease(hJsonVal) == 0);
    }
    else
        RTTestFailed(hTest, "RTJsonParseFromString() returned success but no value\n");
}

int main(int argc, char **argv)
{
    RTTEST hTest;
    int rc = RTTestInitExAndCreate(argc, &argv, 0, "tstRTJson", &hTest);
    if (rc)
        return rc;
    RTTestBanner(hTest);

    tstBasic(hTest);
    tstCorrectness(hTest);
    for (int i = 1; i < argc; i++)
    {
        RTTestSubF(hTest, "file %Rbn", argv[i]);
        RTERRINFOSTATIC ErrInfo;
        RTJSONVAL       hFileValue = NIL_RTJSONVAL;
        rc = RTJsonParseFromFile(&hFileValue, argv[i], RTErrInfoInitStatic(&ErrInfo));
        if (RT_SUCCESS(rc))
            RTJsonValueRelease(hFileValue);
        else if (RTErrInfoIsSet(&ErrInfo.Core))
            RTTestFailed(hTest, "%Rrc - %s", rc, ErrInfo.Core.pszMsg);
        else
            RTTestFailed(hTest, "%Rrc", rc);
    }

    /*
     * Summary.
     */
    return RTTestSummaryAndDestroy(hTest);
}

