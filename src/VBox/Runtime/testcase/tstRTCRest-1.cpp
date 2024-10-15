/* $Id: tstRTCRest-1.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT Testcase - REST C++ classes.
 */

/*
 * Copyright (C) 2018-2024 Oracle and/or its affiliates.
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
#include <iprt/cpp/restbase.h>
#include <iprt/cpp/restarray.h>
#include <iprt/cpp/reststringmap.h>
#include <iprt/cpp/restclient.h>
#include <iprt/cpp/restoutput.h>

#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/message.h>
#include <iprt/string.h>
#include <iprt/test.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
static RTTEST g_hTest;


static char *toJson(RTCRestObjectBase const *pObj)
{
    RTCString str;
    RTCRestOutputToString Dst(&str, false);
    pObj->serializeAsJson(Dst);

    static char s_szReturnBuffer[4096];
    RTStrCopy(s_szReturnBuffer, sizeof(s_szReturnBuffer), str.c_str());
    return s_szReturnBuffer;
}


static int deserializeFromJson(RTCRestObjectBase *pObj, const char *pszJson, PRTERRINFOSTATIC pErrInfo, const char *pszName)
{
    RTJSONVAL hValue;
    RTTESTI_CHECK_RC_OK_RET(RTJsonParseFromString(&hValue, pszJson, pErrInfo ? RTErrInfoInitStatic(pErrInfo) : NULL), rcCheck);
    RTCRestJsonPrimaryCursor Cursor(hValue, pszName, pErrInfo ? RTErrInfoInitStatic(pErrInfo) : NULL);
    return pObj->deserializeFromJson(Cursor.m_Cursor);
}


static int fromString(RTCRestObjectBase *pObj, const char *pszString, PRTERRINFOSTATIC pErrInfo, const char *pszName)
{
    RTCString strValue(pszString);
    return pObj->fromString(strValue, pszName, pErrInfo ? RTErrInfoInitStatic(pErrInfo) : NULL);
}


static void testBool(void)
{
    RTTestSub(g_hTest, "RTCRestBool");

    {
        RTCRestBool obj1;
        RTTESTI_CHECK(obj1.m_fValue == false);
        RTTESTI_CHECK(obj1.isNull() == false);
        RTTESTI_CHECK(strcmp(obj1.typeName(), "bool") == 0);
        RTTESTI_CHECK(obj1.typeClass() == RTCRestObjectBase::kTypeClass_Bool);
    }

    {
        RTCRestBool obj2(true);
        RTTESTI_CHECK(obj2.m_fValue == true);
        RTTESTI_CHECK(obj2.isNull() == false);
    }

    {
        RTCRestBool obj2(false);
        RTTESTI_CHECK(obj2.m_fValue == false);
        RTTESTI_CHECK(obj2.isNull() == false);
    }

    {
        /* Value assignments: */
        RTCRestBool obj3;
        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true);
        obj3.assignValue(true);
        RTTESTI_CHECK(obj3.m_fValue == true);
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true);
        obj3.assignValue(false);
        RTTESTI_CHECK(obj3.m_fValue == false);
        RTTESTI_CHECK(obj3.isNull() == false);

        obj3.assignValue(true);
        RTTESTI_CHECK(obj3.m_fValue == true);
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.resetToDefault(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.m_fValue == false);
        RTTESTI_CHECK(obj3.isNull() == false);

        obj3.assignValue(true);
        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK_RC(obj3.resetToDefault(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.m_fValue == false);
        RTTESTI_CHECK(obj3.isNull() == false);

        /* Copy assignments: */
        RTCRestBool obj3True(true);
        RTTESTI_CHECK(obj3True.m_fValue == true);
        RTTESTI_CHECK(obj3True.isNull() == false);
        RTCRestBool obj3False(false);
        RTTESTI_CHECK(obj3False.m_fValue == false);
        RTTESTI_CHECK(obj3False.isNull() == false);
        RTCRestBool obj3Null;
        obj3Null.setNull();
        RTTESTI_CHECK(obj3Null.m_fValue == false);
        RTTESTI_CHECK(obj3Null.isNull() == true);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK_RC(obj3.assignCopy(obj3True), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.m_fValue == true);
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.assignCopy(obj3Null), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.m_fValue == false);
        RTTESTI_CHECK(obj3.isNull() == true);

        RTTESTI_CHECK_RC(obj3.assignCopy(obj3False), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.m_fValue == false);
        RTTESTI_CHECK(obj3.isNull() == false);

        obj3 = obj3Null;
        RTTESTI_CHECK(obj3.m_fValue == false);
        RTTESTI_CHECK(obj3.isNull() == true);

        obj3 = obj3True;
        RTTESTI_CHECK(obj3.m_fValue == true);
        RTTESTI_CHECK(obj3.isNull() == false);

        obj3 = obj3Null;
        RTTESTI_CHECK(obj3.m_fValue == false);
        RTTESTI_CHECK(obj3.isNull() == true);

        obj3 = obj3False;
        RTTESTI_CHECK(obj3.m_fValue == false);
        RTTESTI_CHECK(obj3.isNull() == false);

        /* setNull implies resetToDefault: */
        obj3 = obj3True;
        RTTESTI_CHECK(obj3.m_fValue == true);
        RTTESTI_CHECK(obj3.isNull() == false);
        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true);
        RTTESTI_CHECK(obj3.m_fValue == false);

        /* Copy constructors: */
        {
            RTCRestBool obj3a(obj3True);
            RTTESTI_CHECK(obj3a.m_fValue == true);
            RTTESTI_CHECK(obj3a.isNull() == false);
        }
        {
            RTCRestBool obj3b(obj3False);
            RTTESTI_CHECK(obj3b.m_fValue == false);
            RTTESTI_CHECK(obj3b.isNull() == false);
        }
        {
            RTCRestBool obj3c(obj3Null);
            RTTESTI_CHECK(obj3c.m_fValue == false);
            RTTESTI_CHECK(obj3c.isNull() == true);
        }

        /* Serialization to json: */
        const char *pszJson = toJson(&obj3True);
        RTTESTI_CHECK_MSG(strcmp(pszJson, "true") == 0, ("pszJson=%s\n", pszJson));
        pszJson = toJson(&obj3False);
        RTTESTI_CHECK_MSG(strcmp(pszJson, "false") == 0, ("pszJson=%s\n", pszJson));
        pszJson = toJson(&obj3Null);
        RTTESTI_CHECK_MSG(strcmp(pszJson, "null") == 0, ("pszJson=%s\n", pszJson));

        /* Serialization to string. */
        RTCString str;
        str = "lead-in:";
        RTTESTI_CHECK_RC(obj3True.toString(&str, RTCRestObjectBase::kToString_Append), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("lead-in:true"), ("str=%s\n", str.c_str()));
        RTTESTI_CHECK_RC(obj3True.toString(&str), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("true"), ("str=%s\n", str.c_str()));

        str = "lead-in:";
        RTTESTI_CHECK_RC(obj3False.toString(&str, RTCRestObjectBase::kToString_Append), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("lead-in:false"), ("str=%s\n", str.c_str()));
        RTTESTI_CHECK_RC(obj3False.toString(&str), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("false"), ("str=%s\n", str.c_str()));

        str = "lead-in:";
        RTTESTI_CHECK_RC(obj3Null.toString(&str, RTCRestObjectBase::kToString_Append), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("lead-in:null"), ("str=%s\n", str.c_str()));
        RTTESTI_CHECK_RC(obj3Null.toString(&str), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("null"), ("str=%s\n", str.c_str()));
    }

    /* deserialize: */
    RTERRINFOSTATIC ErrInfo;
    {
        RTCRestBool obj4;
        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "false", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_fValue == false);
        RTTESTI_CHECK(obj4.isNull() == false);

        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "true", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_fValue == true);
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "null", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_fValue == false);
        RTTESTI_CHECK(obj4.isNull() == true);

        /* object goes to default state on failure: */
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "0", &ErrInfo, RT_XSTR(__LINE__)), VERR_REST_WRONG_JSON_TYPE_FOR_BOOL);
        RTTESTI_CHECK(obj4.m_fValue == false);
        RTTESTI_CHECK(obj4.isNull() == false);
        RTTESTI_CHECK(RTErrInfoIsSet(&ErrInfo.Core));

        obj4.assignValue(true);
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"false\"", &ErrInfo, RT_XSTR(__LINE__)), VERR_REST_WRONG_JSON_TYPE_FOR_BOOL);
        RTTESTI_CHECK(obj4.m_fValue == false);
        RTTESTI_CHECK(obj4.isNull() == false);
        RTTESTI_CHECK(RTErrInfoIsSet(&ErrInfo.Core));

        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "[ null ]", NULL, RT_XSTR(__LINE__)), VERR_REST_WRONG_JSON_TYPE_FOR_BOOL);
        RTTESTI_CHECK(obj4.m_fValue == false);
        RTTESTI_CHECK(obj4.isNull() == false);

        obj4.setNull();
        RTTESTI_CHECK_RC(fromString(&obj4, "true", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_fValue == true);
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(fromString(&obj4, "false", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_fValue == false);
        RTTESTI_CHECK(obj4.isNull() == false);

        obj4.m_fValue = true;
        RTTESTI_CHECK_RC(fromString(&obj4, "null", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_fValue == false);
        RTTESTI_CHECK(obj4.isNull() == true);

        obj4.setNull();
        RTTESTI_CHECK_RC(fromString(&obj4, " TrUe ", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_fValue == true);
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(fromString(&obj4, "\tfAlSe;", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_fValue == false);
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(fromString(&obj4, "\r\nfAlSe\n;", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_fValue == false);
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(fromString(&obj4, "\r\tNuLl\n;", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_fValue == false);
        RTTESTI_CHECK(obj4.isNull() == true);

        RTTESTI_CHECK_RC(fromString(&obj4, "1", &ErrInfo, RT_XSTR(__LINE__)), VERR_REST_UNABLE_TO_PARSE_STRING_AS_BOOL);
        RTTESTI_CHECK(RTErrInfoIsSet(&ErrInfo.Core));

        RTTESTI_CHECK_RC(fromString(&obj4, "0", NULL, RT_XSTR(__LINE__)), VERR_REST_UNABLE_TO_PARSE_STRING_AS_BOOL);
    }
}

class Int64Constants
{
public:
    Int64Constants() {}
    const char *getSubName()  const { return "RTCRestInt64"; }
    int64_t     getMin()      const { return INT64_MIN; }
    const char *getMinStr()   const { return "-9223372036854775808"; }
    int64_t     getMax()      const { return INT64_MAX; }
    const char *getMaxStr()   const { return "9223372036854775807"; }
    const char *getTypeName() const { return "int64_t"; }
    RTCRestObjectBase::kTypeClass getTypeClass() const { return RTCRestObjectBase::kTypeClass_Int64; }
};

class Int32Constants
{
public:
    Int32Constants() { }
    const char *getSubName()  const { return "RTCRestInt32"; }
    int32_t     getMin()      const { return INT32_MIN; }
    const char *getMinStr()   const { return "-2147483648"; }
    int32_t     getMax()      const { return INT32_MAX; }
    const char *getMaxStr()   const { return "2147483647"; }
    const char *getTypeName() const { return "int32_t"; }
    RTCRestObjectBase::kTypeClass getTypeClass() const { return RTCRestObjectBase::kTypeClass_Int32; }
};

class Int16Constants
{
public:
    Int16Constants() { }
    const char *getSubName()  const { return "RTCRestInt16"; }
    int16_t     getMin()      const { return INT16_MIN; }
    const char *getMinStr()   const { return "-32768"; }
    int16_t     getMax()      const { return INT16_MAX; }
    const char *getMaxStr()   const { return "32767"; }
    const char *getTypeName() const { return "int16_t"; }
    RTCRestObjectBase::kTypeClass getTypeClass() const { return RTCRestObjectBase::kTypeClass_Int16; }
};

template<typename RestType, typename IntType, typename ConstantClass>
void testInteger(void)
{
    ConstantClass const Consts;
    RTTestSub(g_hTest, Consts.getSubName());

    {
        RestType obj1;
        RTTESTI_CHECK(obj1.m_iValue == 0);
        RTTESTI_CHECK(obj1.isNull() == false);
        RTTESTI_CHECK(strcmp(obj1.typeName(), Consts.getTypeName()) == 0);
        RTTESTI_CHECK(obj1.typeClass() == Consts.getTypeClass());
    }

    {
        RestType obj2(2398);
        RTTESTI_CHECK(obj2.m_iValue == 2398);
        RTTESTI_CHECK(obj2.isNull() == false);
    }

    {
        RestType obj2(-7345);
        RTTESTI_CHECK(obj2.m_iValue == -7345);
        RTTESTI_CHECK(obj2.isNull() == false);
    }

    {
        /* Value assignments: */
        RestType obj3;
        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true);
        RTTESTI_CHECK(obj3.m_iValue == 0);
        obj3.assignValue(-1);
        RTTESTI_CHECK(obj3.m_iValue == -1);
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true);
        obj3.assignValue(42);
        RTTESTI_CHECK(obj3.m_iValue == 42);
        RTTESTI_CHECK(obj3.isNull() == false);

        obj3.assignValue(Consts.getMax());
        RTTESTI_CHECK(obj3.m_iValue == Consts.getMax());
        RTTESTI_CHECK(obj3.isNull() == false);

        obj3.assignValue(Consts.getMin());
        RTTESTI_CHECK(obj3.m_iValue == Consts.getMin());
        RTTESTI_CHECK(obj3.isNull() == false);

        /* Reset to default: */
        RTTESTI_CHECK_RC(obj3.resetToDefault(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.m_iValue == 0);
        RTTESTI_CHECK(obj3.isNull() == false);

        obj3.assignValue(42);
        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK_RC(obj3.resetToDefault(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.m_iValue == 0);
        RTTESTI_CHECK(obj3.isNull() == false);

        /* Copy assignments: */
        RestType obj3Max(Consts.getMax());
        RTTESTI_CHECK(obj3Max.m_iValue == Consts.getMax());
        RTTESTI_CHECK(obj3Max.isNull() == false);
        RestType obj3Min(Consts.getMin());
        RTTESTI_CHECK(obj3Min.m_iValue == Consts.getMin());
        RTTESTI_CHECK(obj3Min.isNull() == false);
        RestType obj3Null;
        obj3Null.setNull();
        RTTESTI_CHECK(obj3Null.m_iValue == 0);
        RTTESTI_CHECK(obj3Null.isNull() == true);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK_RC(obj3.assignCopy(obj3Max), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.m_iValue == Consts.getMax());
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.assignCopy(obj3Null), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.m_iValue == 0);
        RTTESTI_CHECK(obj3.isNull() == true);

        RTTESTI_CHECK_RC(obj3.assignCopy(obj3Min), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.m_iValue == Consts.getMin());
        RTTESTI_CHECK(obj3.isNull() == false);

        obj3 = obj3Null;
        RTTESTI_CHECK(obj3.m_iValue == 0);
        RTTESTI_CHECK(obj3.isNull() == true);

        obj3 = obj3Max;
        RTTESTI_CHECK(obj3.m_iValue == Consts.getMax());
        RTTESTI_CHECK(obj3.isNull() == false);

        obj3 = obj3Null;
        RTTESTI_CHECK(obj3.m_iValue == 0);
        RTTESTI_CHECK(obj3.isNull() == true);

        obj3 = obj3Min;
        RTTESTI_CHECK(obj3.m_iValue == Consts.getMin());
        RTTESTI_CHECK(obj3.isNull() == false);

        /* setNull implies resetToDefault: */
        obj3 = obj3Max;
        RTTESTI_CHECK(obj3.m_iValue == Consts.getMax());
        RTTESTI_CHECK(obj3.isNull() == false);
        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true);
        RTTESTI_CHECK(obj3.m_iValue == 0);

        /* Copy constructors: */
        {
            RestType obj3a(obj3Max);
            RTTESTI_CHECK(obj3a.m_iValue == Consts.getMax());
            RTTESTI_CHECK(obj3a.isNull() == false);
        }
        {
            RestType obj3b(obj3Min);
            RTTESTI_CHECK(obj3b.m_iValue == Consts.getMin());
            RTTESTI_CHECK(obj3b.isNull() == false);
        }
        {
            RestType obj3c(obj3Null);
            RTTESTI_CHECK(obj3c.m_iValue == 0);
            RTTESTI_CHECK(obj3c.isNull() == true);
        }

        /* Serialization to json: */
        const char *pszJson = toJson(&obj3Max);
        RTTESTI_CHECK_MSG(strcmp(pszJson, Consts.getMaxStr()) == 0, ("pszJson=%s\n", pszJson));
        pszJson = toJson(&obj3Min);
        RTTESTI_CHECK_MSG(strcmp(pszJson, Consts.getMinStr()) == 0, ("pszJson=%s\n", pszJson));
        pszJson = toJson(&obj3Null);
        RTTESTI_CHECK_MSG(strcmp(pszJson, "null") == 0, ("pszJson=%s\n", pszJson));

        /* Serialization to string. */
        RTCString str;
        RTCString strExpect;
        str = "lead-in:";
        RTTESTI_CHECK_RC(obj3Max.toString(&str, RTCRestObjectBase::kToString_Append), VINF_SUCCESS);
        strExpect.printf("lead-in:%s", Consts.getMaxStr());
        RTTESTI_CHECK_MSG(str.equals(strExpect), ("str=%s strExpect=%s\n", str.c_str(), strExpect.c_str()));
        RTTESTI_CHECK_RC(obj3Max.toString(&str), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals(Consts.getMaxStr()), ("str=%s\n", str.c_str()));

        str = "lead-in:";
        RTTESTI_CHECK_RC(obj3Min.toString(&str, RTCRestObjectBase::kToString_Append), VINF_SUCCESS);
        strExpect.printf("lead-in:%s", Consts.getMinStr());
        RTTESTI_CHECK_MSG(str.equals(strExpect), ("str=%s strExpect=%s\n", str.c_str(), strExpect.c_str()));
        RTTESTI_CHECK_RC(obj3Min.toString(&str), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals(Consts.getMinStr()), ("str=%s\n", str.c_str()));

        str = "lead-in:";
        RTTESTI_CHECK_RC(obj3Null.toString(&str, RTCRestObjectBase::kToString_Append), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("lead-in:null"), ("str=%s\n", str.c_str()));
        RTTESTI_CHECK_RC(obj3Null.toString(&str), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("null"), ("str=%s\n", str.c_str()));
    }

    /* deserialize: */
    RTERRINFOSTATIC ErrInfo;
    {
        /* from json: */
        RestType obj4;
        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "42", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_iValue == 42);
        RTTESTI_CHECK(obj4.isNull() == false);

        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "-22", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_iValue == -22);
        RTTESTI_CHECK(obj4.isNull() == false);

        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, Consts.getMaxStr(), &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_iValue == Consts.getMax());
        RTTESTI_CHECK(obj4.isNull() == false);

        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, Consts.getMinStr(), &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_iValue == Consts.getMin());
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "null", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_iValue == 0);
        RTTESTI_CHECK(obj4.isNull() == true);

        /* object goes to default state on failure: */
        obj4.assignValue(Consts.getMin());
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "0.0", &ErrInfo, RT_XSTR(__LINE__)), VERR_REST_WRONG_JSON_TYPE_FOR_INTEGER);
        RTTESTI_CHECK(obj4.m_iValue == 0);
        RTTESTI_CHECK(obj4.isNull() == false);
        RTTESTI_CHECK(RTErrInfoIsSet(&ErrInfo.Core));

        obj4.assignValue(Consts.getMax());
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"false\"", &ErrInfo, RT_XSTR(__LINE__)), VERR_REST_WRONG_JSON_TYPE_FOR_INTEGER);
        RTTESTI_CHECK(obj4.m_iValue == 0);
        RTTESTI_CHECK(obj4.isNull() == false);
        RTTESTI_CHECK(RTErrInfoIsSet(&ErrInfo.Core));

        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "[ null ]", NULL, RT_XSTR(__LINE__)), VERR_REST_WRONG_JSON_TYPE_FOR_INTEGER);
        RTTESTI_CHECK(obj4.m_iValue == 0);
        RTTESTI_CHECK(obj4.isNull() == false);

        /* from string: */
        obj4.setNull();
        RTTESTI_CHECK_RC(fromString(&obj4, "22", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_iValue == 22);
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(fromString(&obj4, "-42", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_iValue == -42);
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(fromString(&obj4, Consts.getMaxStr(), &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_iValue == Consts.getMax());
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(fromString(&obj4, Consts.getMinStr(), &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_iValue == Consts.getMin());
        RTTESTI_CHECK(obj4.isNull() == false);

        obj4.m_iValue = 33;
        RTTESTI_CHECK_RC(fromString(&obj4, "null", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_iValue == 0);
        RTTESTI_CHECK(obj4.isNull() == true);

        obj4.m_iValue = 33;
        RTTESTI_CHECK_RC(fromString(&obj4, " nULl;", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_iValue == 0);
        RTTESTI_CHECK(obj4.isNull() == true);

        obj4.setNull();
        RTTESTI_CHECK_RC(fromString(&obj4, " 0x42 ", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_iValue == 0x42);
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(fromString(&obj4, "\t010\t", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_iValue == 8);
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(fromString(&obj4, "\r\t0X4FDB\t", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_iValue == 0x4fdb);
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(fromString(&obj4, "1.1", &ErrInfo, RT_XSTR(__LINE__)), VERR_TRAILING_CHARS);
        RTTESTI_CHECK(RTErrInfoIsSet(&ErrInfo.Core));

        RTTESTI_CHECK_RC(fromString(&obj4, "false", NULL, RT_XSTR(__LINE__)), VERR_NO_DIGITS);
    }
}


static void testDouble(void)
{
    RTTestSub(g_hTest, "RTCRestDouble");
#define TST_DBL_MAX                 (1.79769313486231571e+308)
#define TST_DBL_MIN                 (2.22507385850720138e-308)
#define TST_DBL_MAX_STRING1         "1.79769313486231571e+308"
#define TST_DBL_MAX_STRING2         "1.7976931348623157e+308"
#define TST_DBL_MAX_EQUAL(a_psz)    ( strcmp(a_psz, TST_DBL_MAX_STRING1) == 0 || strcmp(a_psz, TST_DBL_MAX_STRING2) == 0 )
#define TST_DBL_MIN_STRING1         "2.22507385850720138e-308"
#define TST_DBL_MIN_STRING2         "2.2250738585072014e-308"
#define TST_DBL_MIN_EQUAL(a_psz)    ( strcmp(a_psz, TST_DBL_MIN_STRING1) == 0 || strcmp(a_psz, TST_DBL_MIN_STRING2) == 0 )

    {
        RTCRestDouble obj1;
        RTTESTI_CHECK(obj1.m_rdValue == 0.0);
        RTTESTI_CHECK(obj1.isNull() == false);
        RTTESTI_CHECK(strcmp(obj1.typeName(), "double") == 0);
        RTTESTI_CHECK(obj1.typeClass() == RTCRestObjectBase::kTypeClass_Double);
    }

    {
        RTCRestDouble obj2(2398.1);
        RTTESTI_CHECK(obj2.m_rdValue == 2398.1);
        RTTESTI_CHECK(obj2.isNull() == false);
    }

    {
        RTCRestDouble obj2(-7345.2);
        RTTESTI_CHECK(obj2.m_rdValue == -7345.2);
        RTTESTI_CHECK(obj2.isNull() == false);
    }

    {
        /* Value assignments: */
        RTCRestDouble obj3;
        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true);
        RTTESTI_CHECK(obj3.m_rdValue == 0.0);
        obj3.assignValue(-1.0);
        RTTESTI_CHECK(obj3.m_rdValue == -1.0);
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true);
        obj3.assignValue(42.42);
        RTTESTI_CHECK(obj3.m_rdValue == 42.42);
        RTTESTI_CHECK(obj3.isNull() == false);

        obj3.assignValue(TST_DBL_MAX);
        RTTESTI_CHECK(obj3.m_rdValue == TST_DBL_MAX);
        RTTESTI_CHECK(obj3.isNull() == false);

        obj3.assignValue(TST_DBL_MIN);
        RTTESTI_CHECK(obj3.m_rdValue == TST_DBL_MIN);
        RTTESTI_CHECK(obj3.isNull() == false);

        /* Reset to default: */
        RTTESTI_CHECK_RC(obj3.resetToDefault(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.m_rdValue == 0.0);
        RTTESTI_CHECK(obj3.isNull() == false);

        obj3.assignValue(42);
        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK_RC(obj3.resetToDefault(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.m_rdValue == 0.0);
        RTTESTI_CHECK(obj3.isNull() == false);

        /* Copy assignments: */
        RTCRestDouble obj3Max(TST_DBL_MAX);
        RTTESTI_CHECK(obj3Max.m_rdValue == TST_DBL_MAX);
        RTTESTI_CHECK(obj3Max.isNull() == false);
        RTCRestDouble obj3Min(TST_DBL_MIN);
        RTTESTI_CHECK(obj3Min.m_rdValue == TST_DBL_MIN);
        RTTESTI_CHECK(obj3Min.isNull() == false);
        RTCRestDouble obj3Null;
        obj3Null.setNull();
        RTTESTI_CHECK(obj3Null.m_rdValue == 0.0);
        RTTESTI_CHECK(obj3Null.isNull() == true);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK_RC(obj3.assignCopy(obj3Max), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.m_rdValue == TST_DBL_MAX);
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.assignCopy(obj3Null), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.m_rdValue == 0.0);
        RTTESTI_CHECK(obj3.isNull() == true);

        RTTESTI_CHECK_RC(obj3.assignCopy(obj3Min), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.m_rdValue == TST_DBL_MIN);
        RTTESTI_CHECK(obj3.isNull() == false);

        obj3 = obj3Null;
        RTTESTI_CHECK(obj3.m_rdValue == 0.0);
        RTTESTI_CHECK(obj3.isNull() == true);

        obj3 = obj3Max;
        RTTESTI_CHECK(obj3.m_rdValue == TST_DBL_MAX);
        RTTESTI_CHECK(obj3.isNull() == false);

        obj3 = obj3Null;
        RTTESTI_CHECK(obj3.m_rdValue == 0.0);
        RTTESTI_CHECK(obj3.isNull() == true);

        obj3 = obj3Min;
        RTTESTI_CHECK(obj3.m_rdValue == TST_DBL_MIN);
        RTTESTI_CHECK(obj3.isNull() == false);

        /* setNull implies resetToDefault: */
        obj3 = obj3Max;
        RTTESTI_CHECK(obj3.m_rdValue == TST_DBL_MAX);
        RTTESTI_CHECK(obj3.isNull() == false);
        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true);
        RTTESTI_CHECK(obj3.m_rdValue == 0.0);

        /* Copy constructors: */
        {
            RTCRestDouble obj3a(obj3Max);
            RTTESTI_CHECK(obj3a.m_rdValue == TST_DBL_MAX);
            RTTESTI_CHECK(obj3a.isNull() == false);
        }
        {
            RTCRestDouble obj3b(obj3Min);
            RTTESTI_CHECK(obj3b.m_rdValue == TST_DBL_MIN);
            RTTESTI_CHECK(obj3b.isNull() == false);
        }
        {
            RTCRestDouble obj3c(obj3Null);
            RTTESTI_CHECK(obj3c.m_rdValue == 0.0);
            RTTESTI_CHECK(obj3c.isNull() == true);
        }

        /* Serialization to json: */
        const char *pszJson = toJson(&obj3Max);
        RTTESTI_CHECK_MSG(TST_DBL_MAX_EQUAL(pszJson), ("pszJson=%s\n", pszJson));
        pszJson = toJson(&obj3Min);
        RTTESTI_CHECK_MSG(TST_DBL_MIN_EQUAL(pszJson), ("pszJson=%s\n", pszJson));
        pszJson = toJson(&obj3Null);
        RTTESTI_CHECK_MSG(strcmp(pszJson, "null") == 0, ("pszJson=%s\n", pszJson));

        /* Serialization to string. */
        RTCString str;
        RTCString strExpect1;
        RTCString strExpect2;
        str = "lead-in:";
        RTTESTI_CHECK_RC(obj3Max.toString(&str, RTCRestObjectBase::kToString_Append), VINF_SUCCESS);
        strExpect1.printf("lead-in:%s", TST_DBL_MAX_STRING1);
        strExpect2.printf("lead-in:%s", TST_DBL_MAX_STRING2);
        RTTESTI_CHECK_MSG(str.equals(strExpect1) || str.equals(strExpect2),
                          ("str=%s strExpect1=%s strExpect2=%s\n", str.c_str(), strExpect1.c_str(), strExpect2.c_str()));
        RTTESTI_CHECK_RC(obj3Max.toString(&str), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(TST_DBL_MAX_EQUAL(str.c_str()), ("str=%s\n", str.c_str()));

        str = "lead-in:";
        RTTESTI_CHECK_RC(obj3Min.toString(&str, RTCRestObjectBase::kToString_Append), VINF_SUCCESS);
        strExpect1.printf("lead-in:%s", TST_DBL_MIN_STRING1);
        strExpect2.printf("lead-in:%s", TST_DBL_MIN_STRING2);
        RTTESTI_CHECK_MSG(str.equals(strExpect1) || str.equals(strExpect2),
                          ("str=%s strExpect1=%s strExpect2=%s\n", str.c_str(), strExpect1.c_str(), strExpect2.c_str()));
        RTTESTI_CHECK_RC(obj3Min.toString(&str), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(TST_DBL_MIN_EQUAL(str.c_str()), ("str=%s\n", str.c_str()));

        str = "lead-in:";
        RTTESTI_CHECK_RC(obj3Null.toString(&str, RTCRestObjectBase::kToString_Append), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("lead-in:null"), ("str=%s\n", str.c_str()));
        RTTESTI_CHECK_RC(obj3Null.toString(&str), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("null"), ("str=%s\n", str.c_str()));
    }

    /* deserialize: */
    RTERRINFOSTATIC ErrInfo;
    {
        /* Some linux systems and probably all Solaris fail to parse the longer MIN string, so just detect and skip. */
        bool fGroksMinString = true;
#if defined(RT_OS_LINUX) || defined(RT_OS_SOLARIS)
        RTJSONVAL hTmpValue = NIL_RTJSONVAL;
        int rcTmp = RTJsonParseFromString(&hTmpValue, TST_DBL_MIN_STRING1, NULL);
        RTJsonValueRelease(hTmpValue);
        if (rcTmp == VERR_INVALID_PARAMETER || rcTmp == VERR_OUT_OF_RANGE)
            fGroksMinString = false;
#endif

        /* from json: */
        RTCRestDouble obj4;
        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "42.42", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_rdValue == 42.42);
        RTTESTI_CHECK(obj4.isNull() == false);

        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "-22.22", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_rdValue == -22.22);
        RTTESTI_CHECK(obj4.isNull() == false);

        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, TST_DBL_MAX_STRING1, &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_rdValue == TST_DBL_MAX);
        RTTESTI_CHECK(obj4.isNull() == false);

        if (fGroksMinString)
        {
            obj4.setNull();
            RTTESTI_CHECK_RC(deserializeFromJson(&obj4, TST_DBL_MIN_STRING1, &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
            RTTESTI_CHECK(obj4.m_rdValue == TST_DBL_MIN);
            RTTESTI_CHECK(obj4.isNull() == false);
        }

        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "null", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_rdValue == 0.0);
        RTTESTI_CHECK(obj4.isNull() == true);

        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "14323", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_rdValue == 14323.0);
        RTTESTI_CHECK(obj4.isNull() == false);

        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "-234875", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_rdValue == -234875.0);
        RTTESTI_CHECK(obj4.isNull() == false);

        /* object goes to default state on failure: */
        obj4.assignValue(TST_DBL_MIN);
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "false", &ErrInfo, RT_XSTR(__LINE__)), VERR_REST_WRONG_JSON_TYPE_FOR_DOUBLE);
        RTTESTI_CHECK(obj4.m_rdValue == 0.0);
        RTTESTI_CHECK(obj4.isNull() == false);
        RTTESTI_CHECK(RTErrInfoIsSet(&ErrInfo.Core));

        obj4.assignValue(TST_DBL_MAX);
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"false\"", &ErrInfo, RT_XSTR(__LINE__)), VERR_REST_WRONG_JSON_TYPE_FOR_DOUBLE);
        RTTESTI_CHECK(obj4.m_rdValue == 0.0);
        RTTESTI_CHECK(obj4.isNull() == false);
        RTTESTI_CHECK(RTErrInfoIsSet(&ErrInfo.Core));

        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "[ null ]", NULL, RT_XSTR(__LINE__)), VERR_REST_WRONG_JSON_TYPE_FOR_DOUBLE);
        RTTESTI_CHECK(obj4.m_rdValue == 0.0);
        RTTESTI_CHECK(obj4.isNull() == false);

        /* from string: */
        obj4.setNull();
        RTTESTI_CHECK_RC(fromString(&obj4, "22.42", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_rdValue == 22.42);
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(fromString(&obj4, "-42.22", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_rdValue == -42.22);
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(fromString(&obj4, TST_DBL_MAX_STRING1, &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_rdValue == TST_DBL_MAX);
        RTTESTI_CHECK(obj4.isNull() == false);

        if (fGroksMinString)
        {
            RTTESTI_CHECK_RC(fromString(&obj4, TST_DBL_MIN_STRING1, &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
            RTTESTI_CHECK(obj4.m_rdValue == TST_DBL_MIN);
            RTTESTI_CHECK(obj4.isNull() == false);
        }

        obj4.m_rdValue = 33.33;
        RTTESTI_CHECK_RC(fromString(&obj4, "null", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_rdValue == 0.0);
        RTTESTI_CHECK(obj4.isNull() == true);

        obj4.m_rdValue = 33.33;
        RTTESTI_CHECK_RC(fromString(&obj4, " nULl;", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_rdValue == 0.0);
        RTTESTI_CHECK(obj4.isNull() == true);

        obj4.setNull();
        RTTESTI_CHECK_RC(fromString(&obj4, " 42.22 ", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_rdValue == 42.22);
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(fromString(&obj4, "\t010\t", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_rdValue ==10.0);
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(fromString(&obj4, "\r\t03495.344\t\r\n", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_rdValue == 3495.344);
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(fromString(&obj4, "1.1;", &ErrInfo, RT_XSTR(__LINE__)), VERR_TRAILING_CHARS);
        RTTESTI_CHECK(RTErrInfoIsSet(&ErrInfo.Core));

        RTTESTI_CHECK_RC(fromString(&obj4, "false", NULL, RT_XSTR(__LINE__)), VERR_NO_DIGITS);

#if (!defined(RT_OS_WINDOWS) && !defined(VBOX_SOLARIS_WITHOUT_XPG6_ENABLED)) || RT_MSC_PREREQ(RT_MSC_VER_VS2015)
        RTTESTI_CHECK_RC(fromString(&obj4, " 0x42 ", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.m_rdValue == 66.0);
#else
        RTTESTI_CHECK_RC(fromString(&obj4, " 0x42 ", &ErrInfo, RT_XSTR(__LINE__)), VERR_TRAILING_CHARS);
        RTTESTI_CHECK(obj4.m_rdValue == 0.0);
#endif
        RTTESTI_CHECK(obj4.isNull() == false);
    }
}


static void testString(const char *pszDummy, ...)
{
    RTTestSub(g_hTest, "RTCRestString");

    {
        RTCRestString obj1;
        RTTESTI_CHECK(obj1.isEmpty());
        RTTESTI_CHECK(obj1.isNull() == false);
        RTTESTI_CHECK(strcmp(obj1.typeName(), "RTCString") == 0);
        RTTESTI_CHECK(obj1.typeClass() == RTCRestObjectBase::kTypeClass_String);
    }

    {
        RTCRestString obj2(RTCString("2398.1"));
        RTTESTI_CHECK(obj2 == "2398.1");
        RTTESTI_CHECK(obj2.isNull() == false);
    }

    {
        RTCRestString obj2("-7345.2");
        RTTESTI_CHECK(obj2 == "-7345.2");
        RTTESTI_CHECK(obj2.isNull() == false);
    }

    {
        /* Value assignments: */
        RTCRestString obj3;
        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true);
        RTTESTI_CHECK(obj3.isEmpty());
        obj3 = "-1.0";
        RTTESTI_CHECK(obj3 == "-1.0");
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        obj3 = RTCString("-2.0");
        RTTESTI_CHECK(obj3 == "-2.0");
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        obj3 = RTCRestString("-3.0");
        RTTESTI_CHECK(obj3 == "-3.0");
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        RTTESTI_CHECK_RC(obj3.assignNoThrow(RTCRestString("4.0")), VINF_SUCCESS);
        RTTESTI_CHECK(obj3 == "4.0");
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        RTTESTI_CHECK_RC(obj3.assignNoThrow("-4.0"), VINF_SUCCESS);
        RTTESTI_CHECK(obj3 == "-4.0");
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        RTTESTI_CHECK_RC(obj3.assignNoThrow(RTCRestString("0123456789"), 3, 5), VINF_SUCCESS);
        RTTESTI_CHECK(obj3 == "34567");
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        RTTESTI_CHECK_RC(obj3.assignNoThrow("0123456789", 4), VINF_SUCCESS);
        RTTESTI_CHECK(obj3 == "0123");
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        RTTESTI_CHECK_RC(obj3.assignNoThrow(8, 'x'), VINF_SUCCESS);
        RTTESTI_CHECK(obj3 == "xxxxxxxx");
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        RTTESTI_CHECK_RC(obj3.printfNoThrow("%d%s%d", 42, "asdf", 22), VINF_SUCCESS);
        RTTESTI_CHECK(obj3 == "42asdf22");
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        va_list va;
        va_start(va, pszDummy);
        RTTESTI_CHECK_RC(obj3.printfVNoThrow("asdf", va), VINF_SUCCESS);
        va_end(va);
        RTTESTI_CHECK(obj3 == "asdf");
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        obj3.assign(RTCRestString("4.0"));
        RTTESTI_CHECK(obj3 == "4.0");
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        obj3.assign("-4.0");
        RTTESTI_CHECK(obj3 == "-4.0");
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        obj3.assign(RTCRestString("0123456789"), 3, 5);
        RTTESTI_CHECK(obj3 == "34567");
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        obj3.assign("0123456789", 4);
        RTTESTI_CHECK(obj3 == "0123");
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        obj3.assign(8, 'x');
        RTTESTI_CHECK(obj3 == "xxxxxxxx");
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        obj3.printf("%d%s%d", 42, "asdf", 22);
        RTTESTI_CHECK(obj3 == "42asdf22");
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        va_start(va, pszDummy);
        obj3.printfV("asdf", va);
        va_end(va);
        RTTESTI_CHECK(obj3 == "asdf");
        RTTESTI_CHECK(obj3.isNull() == false);

        /* Reset to default: */
        RTTESTI_CHECK_RC(obj3.resetToDefault(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isEmpty());
        RTTESTI_CHECK(obj3.isNull() == false);

        obj3 = "1";
        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK_RC(obj3.resetToDefault(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isEmpty());
        RTTESTI_CHECK(obj3.isNull() == false);

        /* Copy assignments: */
        RTCRestString const obj3Max("max");
        RTTESTI_CHECK(obj3Max == "max");
        RTTESTI_CHECK(obj3Max.isNull() == false);
        RTCRestString obj3Null;
        obj3Null.setNull();
        RTTESTI_CHECK(obj3Null.isEmpty());
        RTTESTI_CHECK(obj3Null.isNull() == true);
        RTCRestString obj3Empty;
        RTTESTI_CHECK(obj3Empty.isEmpty());
        RTTESTI_CHECK(obj3Empty.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        RTTESTI_CHECK_RC(obj3.assignCopy(obj3Max), VINF_SUCCESS);
        RTTESTI_CHECK(obj3 == "max");
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        RTTESTI_CHECK_RC(obj3.assignCopy(obj3Null), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isEmpty());
        RTTESTI_CHECK(obj3.isNull() == true);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        RTTESTI_CHECK_RC(obj3.assignCopy(obj3Empty), VINF_SUCCESS);
        RTTESTI_CHECK(obj3 == "");
        RTTESTI_CHECK(obj3.isEmpty());
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        RTTESTI_CHECK_RC(obj3.assignCopy(RTCString("11.0")), VINF_SUCCESS);
        RTTESTI_CHECK(obj3 == "11.0");
        RTTESTI_CHECK(obj3.isNull() == false);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true && obj3.isEmpty());
        RTTESTI_CHECK_RC(obj3.assignCopy("12.0"), VINF_SUCCESS);
        RTTESTI_CHECK(obj3 == "12.0");
        RTTESTI_CHECK(obj3.isNull() == false);

        /* setNull implies resetToDefault: */
        obj3 = obj3Max;
        RTTESTI_CHECK(obj3 == "max");
        RTTESTI_CHECK(obj3.isNull() == false);
        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(obj3.isNull() == true);
        RTTESTI_CHECK(obj3.isEmpty());

        /* Copy constructors: */
        {
            RTCRestString obj3a(obj3Max);
            RTTESTI_CHECK(obj3a == "max");
            RTTESTI_CHECK(obj3a.isNull() == false);
        }
        {
            RTCRestString const obj3c(obj3Null);
            RTTESTI_CHECK(obj3c.isEmpty());
            RTTESTI_CHECK(obj3c.isNull() == true);
        }

        /* Serialization to json: */
        const char *pszJson = toJson(&obj3Max);
        RTTESTI_CHECK_MSG(strcmp(pszJson, "\"max\"") == 0, ("pszJson=%s\n", pszJson));
        pszJson = toJson(&obj3Null);
        RTTESTI_CHECK_MSG(strcmp(pszJson, "null") == 0, ("pszJson=%s\n", pszJson));
        pszJson = toJson(&obj3Empty);
        RTTESTI_CHECK_MSG(strcmp(pszJson, "\"\"") == 0, ("pszJson=%s\n", pszJson));

        /* Serialization to string. */
        RTCString str;
        RTCString strExpect;
        str = "lead-in:";
        RTTESTI_CHECK_RC(obj3Max.toString(&str, RTCRestObjectBase::kToString_Append), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("lead-in:max"), ("str=%s\n", str.c_str()));
        RTTESTI_CHECK_RC(obj3Max.toString(&str), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("max"), ("str=%s\n", str.c_str()));

        str = "lead-in:";
        RTTESTI_CHECK_RC(obj3Empty.toString(&str, RTCRestObjectBase::kToString_Append), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("lead-in:"), ("str=%s\n", str.c_str()));
        RTTESTI_CHECK_RC(obj3Empty.toString(&str), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals(""), ("str=%s\n", str.c_str()));

        str = "lead-in:";
        RTTESTI_CHECK_RC(obj3Null.toString(&str, RTCRestObjectBase::kToString_Append), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("lead-in:"), ("str=%s\n", str.c_str()));
        RTTESTI_CHECK_RC(obj3Null.toString(&str), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals(""), ("str=%s\n", str.c_str()));
    }

    /* deserialize: */
    RTERRINFOSTATIC ErrInfo;
    {
        /* from json: */
        RTCRestString obj4;
        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"42.42\"", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4 == "42.42");
        RTTESTI_CHECK(obj4.isNull() == false);

        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"-22.22\"", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4 == "-22.22");
        RTTESTI_CHECK(obj4.isNull() == false);

        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"maximum\"", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4 == "maximum");
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "null", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4.isEmpty());
        RTTESTI_CHECK(obj4.isNull() == true);

        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"\\u0020\"", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4 == " ");
        RTTESTI_CHECK(obj4.isNull() == false);

        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"\\u004f\\u004D\\u0047\\u0021 :-)\"",
                                             &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4 == "OMG! :-)");
        RTTESTI_CHECK(obj4.isNull() == false);

        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"42:\\uD801\\udC37\\ud852\\uDf62:42\"",  /* U+10437 U+24B62 */
                                             &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4 == "42:" "\xf0\x90\x90\xb7" "\xf0\xa4\xad\xa2" ":42");
        RTTESTI_CHECK(obj4.isNull() == false);

        /* object goes to default state on failure: */
        obj4 = "asdf";
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "false", &ErrInfo, RT_XSTR(__LINE__)), VERR_REST_WRONG_JSON_TYPE_FOR_STRING);
        RTTESTI_CHECK(obj4.isEmpty());
        RTTESTI_CHECK(obj4.isNull() == false);
        RTTESTI_CHECK(RTErrInfoIsSet(&ErrInfo.Core));

        obj4 = "asdf";
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "1", &ErrInfo, RT_XSTR(__LINE__)), VERR_REST_WRONG_JSON_TYPE_FOR_STRING);
        RTTESTI_CHECK(obj4.isEmpty());
        RTTESTI_CHECK(obj4.isNull() == false);
        RTTESTI_CHECK(RTErrInfoIsSet(&ErrInfo.Core));

        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "[ null ]", NULL, RT_XSTR(__LINE__)), VERR_REST_WRONG_JSON_TYPE_FOR_STRING);
        RTTESTI_CHECK(obj4.isEmpty());
        RTTESTI_CHECK(obj4.isNull() == false);

        /* from string: */
        obj4.setNull();
        RTTESTI_CHECK_RC(fromString(&obj4, "22.42", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4 == "22.42");
        RTTESTI_CHECK(obj4.isNull() == false);

        RTTESTI_CHECK_RC(fromString(&obj4, "-42.22", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4 == "-42.22");
        RTTESTI_CHECK(obj4.isNull() == false);

        obj4.setNull();
        RTTESTI_CHECK_RC(fromString(&obj4, "null", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        RTTESTI_CHECK(obj4 == "null");
        RTTESTI_CHECK(obj4.isNull() == false);
    }
}


static void testDate()
{
    RTTestSub(g_hTest, "RTCRestDate");
    int64_t const iRecent    = INT64_C(1536580687739632500);
    int64_t const iRecentSec = INT64_C(1536580687000000000);
    RTTIMESPEC TimeSpec;

#define CHECK_DATE(a_obj, a_fNull, a_fOkay, a_i64Nano, a_sz, a_fUtc) \
    do { \
        RTTESTI_CHECK((a_obj).isOkay() == (a_fOkay)); \
        if ((a_obj).getEpochNano() != (a_i64Nano)) \
            RTTestIFailed("line " RT_XSTR(__LINE__) ": getEpochNano=%RI64, expected %RI64", (a_obj).getEpochNano(), (int64_t)(a_i64Nano)); \
        if (!(a_obj).getString().equals(a_sz)) \
            RTTestIFailed("line " RT_XSTR(__LINE__) ": getString=%s, expected %s", (a_obj).getString().c_str(), a_sz); \
        RTTESTI_CHECK((a_obj).isUtc() == (a_fUtc)); \
        RTTESTI_CHECK((a_obj).isNull() == (a_fNull)); \
    } while (0)
#define CHECK_DATE_FMT(a_obj, a_fNull, a_fOkay, a_i64Nano, a_sz, a_fUtc, a_enmFormat) \
    do { \
        CHECK_DATE(a_obj, a_fNull, a_fOkay, a_i64Nano, a_sz, a_fUtc); \
        if ((a_obj).getFormat() != (a_enmFormat)) \
            RTTestIFailed("line " RT_XSTR(__LINE__) ": getFormat=%d, expected %d (%s)", (a_obj).getFormat(), (a_enmFormat), #a_enmFormat); \
    } while (0)

    {
        RTCRestDate obj1;
        CHECK_DATE(obj1, true, false, 0, "", true);
        RTTESTI_CHECK(strcmp(obj1.typeName(), "RTCRestDate") == 0);
        RTTESTI_CHECK(obj1.typeClass() == RTCRestObjectBase::kTypeClass_Date);
    }

    {
        /* Value assignments: */
        RTCRestDate obj3;
        RTTESTI_CHECK_RC(obj3.assignValue(RTTimeSpecSetNano(&TimeSpec, 0), RTCRestDate::kFormat_Rfc3339), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, 0, "1970-01-01T00:00:00Z", true);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        CHECK_DATE(obj3, true, false, 0, "", true);
        RTTESTI_CHECK_RC(obj3.assignValueRfc3339(RTTimeSpecSetNano(&TimeSpec, 0)), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, 0, "1970-01-01T00:00:00Z", true);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        CHECK_DATE(obj3, true, false, 0, "", true);
        RTTESTI_CHECK_RC(obj3.assignValueRfc2822(RTTimeSpecSetNano(&TimeSpec, 0)), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, 0, "Thu, 1 Jan 1970 00:00:00 -0000", true);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK_RC(obj3.assignValueRfc7131(RTTimeSpecSetNano(&TimeSpec, 0)), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, 0, "Thu, 1 Jan 1970 00:00:00 GMT", true);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK_RC(obj3.assignValue(RTTimeSpecSetNano(&TimeSpec, 0), RTCRestDate::kFormat_Rfc2822), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, 0, "Thu, 1 Jan 1970 00:00:00 -0000", true);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK_RC(obj3.assignValue(RTTimeSpecSetNano(&TimeSpec, 0), RTCRestDate::kFormat_Rfc7131), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, 0, "Thu, 1 Jan 1970 00:00:00 GMT", true);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK_RC(obj3.assignValue(RTTimeSpecSetNano(&TimeSpec, 0), RTCRestDate::kFormat_Rfc3339_Fraction_9), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, 0, "1970-01-01T00:00:00.000000000Z", true);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK_RC(obj3.assignValue(RTTimeSpecSetNano(&TimeSpec, 0), RTCRestDate::kFormat_Rfc3339_Fraction_6), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, 0, "1970-01-01T00:00:00.000000Z", true);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK_RC(obj3.assignValue(RTTimeSpecSetNano(&TimeSpec, 0), RTCRestDate::kFormat_Rfc3339_Fraction_3), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, 0, "1970-01-01T00:00:00.000Z", true);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK_RC(obj3.assignValue(RTTimeSpecSetNano(&TimeSpec, 0), RTCRestDate::kFormat_Rfc3339_Fraction_2), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, 0, "1970-01-01T00:00:00.00Z", true);

        /* Format changes: */
        RTTESTI_CHECK_RC(obj3.assignValue(RTTimeSpecSetNano(&TimeSpec, INT64_C(59123456789)), RTCRestDate::kFormat_Rfc3339_Fraction_9), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, INT64_C(59123456789), "1970-01-01T00:00:59.123456789Z", true);
        RTTESTI_CHECK_RC(obj3.setFormat(RTCRestDate::kFormat_Rfc2822), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, INT64_C(59123456789), "Thu, 1 Jan 1970 00:00:59 -0000", true);
        RTTESTI_CHECK_RC(obj3.setFormat(RTCRestDate::kFormat_Rfc7131), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, INT64_C(59123456789), "Thu, 1 Jan 1970 00:00:59 GMT", true);
        RTTESTI_CHECK_RC(obj3.setFormat(RTCRestDate::kFormat_Rfc3339), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, INT64_C(59123456789), "1970-01-01T00:00:59Z", true);
        RTTESTI_CHECK_RC(obj3.setFormat(RTCRestDate::kFormat_Rfc3339_Fraction_2), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, INT64_C(59123456789), "1970-01-01T00:00:59.12Z", true);
        RTTESTI_CHECK_RC(obj3.setFormat(RTCRestDate::kFormat_Rfc3339_Fraction_3), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, INT64_C(59123456789), "1970-01-01T00:00:59.123Z", true);
        RTTESTI_CHECK_RC(obj3.setFormat(RTCRestDate::kFormat_Rfc3339_Fraction_6), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, INT64_C(59123456789), "1970-01-01T00:00:59.123456Z", true);
        RTTESTI_CHECK_RC(obj3.setFormat(RTCRestDate::kFormat_Rfc3339_Fraction_9), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, INT64_C(59123456789), "1970-01-01T00:00:59.123456789Z", true);

        /* Reset to default and setNull works identically: */
        RTTESTI_CHECK_RC(obj3.resetToDefault(), VINF_SUCCESS);
        CHECK_DATE(obj3, true, false, 0, "", true);

        RTTESTI_CHECK_RC(obj3.assignValue(RTTimeSpecSetNano(&TimeSpec, 0), RTCRestDate::kFormat_Rfc3339_Fraction_2), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, 0, "1970-01-01T00:00:00.00Z", true);
        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        CHECK_DATE(obj3, true, false, 0, "", true);

        /* Copy assignments: */
        RTCRestDate obj3Epoch_3339_9;
        RTTESTI_CHECK_RC(obj3Epoch_3339_9.assignValue(RTTimeSpecSetNano(&TimeSpec, 0), RTCRestDate::kFormat_Rfc3339_Fraction_9), VINF_SUCCESS);
        CHECK_DATE(obj3Epoch_3339_9, false, true, 0, "1970-01-01T00:00:00.000000000Z", true);

        RTCRestDate obj3Epoch_7131;
        RTTESTI_CHECK_RC(obj3Epoch_7131.assignValue(RTTimeSpecSetNano(&TimeSpec, 0), RTCRestDate::kFormat_Rfc7131), VINF_SUCCESS);
        CHECK_DATE(obj3Epoch_7131, false, true, 0, "Thu, 1 Jan 1970 00:00:00 GMT", true);

        RTCRestDate obj3Recent_3339;
        RTTESTI_CHECK_RC(obj3Recent_3339.assignValue(RTTimeSpecSetNano(&TimeSpec, iRecent), RTCRestDate::kFormat_Rfc3339), VINF_SUCCESS);
        CHECK_DATE(obj3Recent_3339, false, true, iRecent, "2018-09-10T11:58:07Z", true);

        RTCRestDate obj3Recent_2822;
        RTTESTI_CHECK_RC(obj3Recent_2822.assignValue(RTTimeSpecSetNano(&TimeSpec, iRecent), RTCRestDate::kFormat_Rfc2822), VINF_SUCCESS);
        CHECK_DATE(obj3Recent_2822, false, true, iRecent, "Mon, 10 Sep 2018 11:58:07 -0000", true);

        RTCRestDate const obj3Null;
        CHECK_DATE(obj3Null, true, false, 0, "", true);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK_RC(obj3.assignCopy(obj3Epoch_3339_9), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, 0, "1970-01-01T00:00:00.000000000Z", true);

        RTTESTI_CHECK_RC(obj3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK_RC(obj3.assignCopy(obj3Epoch_7131), VINF_SUCCESS);
        CHECK_DATE(obj3, false, true, 0, "Thu, 1 Jan 1970 00:00:00 GMT", true);

        RTTESTI_CHECK_RC(obj3.assignCopy(obj3Recent_3339), VINF_SUCCESS);
        CHECK_DATE(obj3Recent_2822, false, true, iRecent, "Mon, 10 Sep 2018 11:58:07 -0000", true);

        RTTESTI_CHECK_RC(obj3.assignCopy(obj3Null), VINF_SUCCESS);
        CHECK_DATE(obj3, true, false, 0, "", true);

        obj3 = obj3Recent_2822;
        CHECK_DATE(obj3Recent_2822, false, true, iRecent, "Mon, 10 Sep 2018 11:58:07 -0000", true);

        obj3 = obj3Epoch_3339_9;
        CHECK_DATE(obj3, false, true, 0, "1970-01-01T00:00:00.000000000Z", true);

        obj3 = obj3Null;
        CHECK_DATE(obj3, true, false, 0, "", true);

        /* Copy constructors: */
        {
            RTCRestDate obj3a(obj3Epoch_3339_9);
            CHECK_DATE(obj3a, false, true, 0, "1970-01-01T00:00:00.000000000Z", true);
        }
        {
            RTCRestDate obj3b(obj3Epoch_7131);
            CHECK_DATE(obj3b, false, true, 0, "Thu, 1 Jan 1970 00:00:00 GMT", true);
        }
        {
            RTCRestDate obj3c(obj3Recent_3339);
            CHECK_DATE(obj3Recent_3339, false, true, iRecent, "2018-09-10T11:58:07Z", true);
        }
        {
            RTCRestDate obj3d(obj3Recent_2822);
            CHECK_DATE(obj3d, false, true, iRecent, "Mon, 10 Sep 2018 11:58:07 -0000", true);
        }
        {
            RTCRestDate obj3e(obj3Null);
            CHECK_DATE(obj3e, true, false, 0, "", true);
        }

        /* Serialization to json: */
        const char *pszJson = toJson(&obj3Epoch_3339_9);
        RTTESTI_CHECK_MSG(strcmp(pszJson, "\"1970-01-01T00:00:00.000000000Z\"") == 0, ("pszJson=%s\n", pszJson));
        pszJson = toJson(&obj3Epoch_7131);
        RTTESTI_CHECK_MSG(strcmp(pszJson, "\"Thu, 1 Jan 1970 00:00:00 GMT\"") == 0, ("pszJson=%s\n", pszJson));
        pszJson = toJson(&obj3Recent_3339);
        RTTESTI_CHECK_MSG(strcmp(pszJson, "\"2018-09-10T11:58:07Z\"") == 0, ("pszJson=%s\n", pszJson));
        pszJson = toJson(&obj3Recent_2822);
        RTTESTI_CHECK_MSG(strcmp(pszJson, "\"Mon, 10 Sep 2018 11:58:07 -0000\"") == 0, ("pszJson=%s\n", pszJson));
        pszJson = toJson(&obj3Null);
        RTTESTI_CHECK_MSG(strcmp(pszJson, "null") == 0, ("pszJson=%s\n", pszJson));

        /* Serialization to string. */
        RTCString str;
        str = "lead-in:";
        RTTESTI_CHECK_RC(obj3Epoch_7131.toString(&str, RTCRestObjectBase::kToString_Append), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("lead-in:Thu, 1 Jan 1970 00:00:00 GMT"), ("str=%s\n", str.c_str()));
        RTTESTI_CHECK_RC(obj3Epoch_7131.toString(&str), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("Thu, 1 Jan 1970 00:00:00 GMT"), ("str=%s\n", str.c_str()));

        str = "lead-in:";
        RTTESTI_CHECK_RC(obj3Recent_3339.toString(&str, RTCRestObjectBase::kToString_Append), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("lead-in:2018-09-10T11:58:07Z"), ("str=%s\n", str.c_str()));
        RTTESTI_CHECK_RC(obj3Recent_3339.toString(&str), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("2018-09-10T11:58:07Z"), ("str=%s\n", str.c_str()));

        str = "lead-in:";
        RTTESTI_CHECK_RC(obj3Null.toString(&str, RTCRestObjectBase::kToString_Append), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("lead-in:null"), ("str=%s\n", str.c_str()));
        RTTESTI_CHECK_RC(obj3Null.toString(&str), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(str.equals("null"), ("str=%s\n", str.c_str()));
    }

    /* deserialize: */
    RTERRINFOSTATIC ErrInfo;
    {
        RTCRestDate obj4;
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"Thu, 1 Jan 1970 00:00:00 GMT\"", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, false, true, 0, "Thu, 1 Jan 1970 00:00:00 GMT", true, RTCRestDate::kFormat_Rfc7131);

        obj4.setNull();
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"Thu, 1 Jan 1970 00:00:00.0000 GMT\"", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, false, true, 0, "Thu, 1 Jan 1970 00:00:00.0000 GMT", true, RTCRestDate::kFormat_Rfc7131);

        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"1 Jan 1970 00:00:00 GMT\"", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, false, true, 0, "1 Jan 1970 00:00:00 GMT", true, RTCRestDate::kFormat_Rfc7131);

        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"1 Jan 1970 00:00:00\"", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, false, true, 0, "1 Jan 1970 00:00:00", false, RTCRestDate::kFormat_Rfc2822);

        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"1 Jan 070 00:00:00\"", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, false, true, 0, "1 Jan 070 00:00:00", false, RTCRestDate::kFormat_Rfc2822);

        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"2018-09-10T11:58:07Z\"", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, false, true, iRecentSec , "2018-09-10T11:58:07Z", true, RTCRestDate::kFormat_Rfc3339);

        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"1 Jan 70 00:00:00\"", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, false, true, 0, "1 Jan 70 00:00:00", false, RTCRestDate::kFormat_Rfc2822);

        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"2018-09-10T11:58:07.739632500Z\"", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, false, true, iRecent, "2018-09-10T11:58:07.739632500Z", true, RTCRestDate::kFormat_Rfc3339_Fraction_9);

        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "null", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, true, false, 0, "", true, RTCRestDate::kFormat_Rfc3339_Fraction_9);

        /* object goes to default state if not string and to non-okay if string: */
        RTTESTI_CHECK_RC(obj4.assignValue(RTTimeSpecSetNano(&TimeSpec, iRecent), RTCRestDate::kFormat_Rfc2822), VINF_SUCCESS);
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "true", &ErrInfo, RT_XSTR(__LINE__)), VERR_REST_WRONG_JSON_TYPE_FOR_DATE);
        CHECK_DATE_FMT(obj4, true, false, 0, "", true, RTCRestDate::kFormat_Rfc2822);

        RTTESTI_CHECK_RC(obj4.assignValue(RTTimeSpecSetNano(&TimeSpec, iRecent), RTCRestDate::kFormat_Rfc2822), VINF_SUCCESS);
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"string\"", &ErrInfo, RT_XSTR(__LINE__)), VWRN_REST_UNABLE_TO_DECODE_DATE);
        CHECK_DATE_FMT(obj4, false, false, 0, "string", false, RTCRestDate::kFormat_Rfc2822);

        RTTESTI_CHECK_RC(obj4.assignValue(RTTimeSpecSetNano(&TimeSpec, iRecent), RTCRestDate::kFormat_Rfc2822), VINF_SUCCESS);
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "\"0x199 string\"", &ErrInfo, RT_XSTR(__LINE__)), VWRN_REST_UNABLE_TO_DECODE_DATE);
        CHECK_DATE_FMT(obj4, false, false, 0, "0x199 string", false, RTCRestDate::kFormat_Rfc2822);

        RTTESTI_CHECK_RC(obj4.assignValue(RTTimeSpecSetNano(&TimeSpec, iRecent), RTCRestDate::kFormat_Rfc2822), VINF_SUCCESS);
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "[ null ]", &ErrInfo, RT_XSTR(__LINE__)), VERR_REST_WRONG_JSON_TYPE_FOR_DATE);
        CHECK_DATE_FMT(obj4, true, false, 0, "", true, RTCRestDate::kFormat_Rfc2822);

        RTTESTI_CHECK_RC(obj4.assignValue(RTTimeSpecSetNano(&TimeSpec, iRecent), RTCRestDate::kFormat_Rfc2822), VINF_SUCCESS);
        RTTESTI_CHECK_RC(deserializeFromJson(&obj4, "{ \"foo\": 1 }", &ErrInfo, RT_XSTR(__LINE__)), VERR_REST_WRONG_JSON_TYPE_FOR_DATE);
        CHECK_DATE_FMT(obj4, true, false, 0, "", true, RTCRestDate::kFormat_Rfc2822);

        /* From string: */
        obj4.setNull();
        RTTESTI_CHECK_RC(fromString(&obj4, "Thu, 1 Jan 1970 00:00:00 GMT", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, false, true, 0, "Thu, 1 Jan 1970 00:00:00 GMT", true, RTCRestDate::kFormat_Rfc7131);

        RTTESTI_CHECK_RC(fromString(&obj4, "Mon, 10 Sep 2018 11:58:07 -0000", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, false, true, iRecentSec, "Mon, 10 Sep 2018 11:58:07 -0000", true, RTCRestDate::kFormat_Rfc2822);

        RTTESTI_CHECK_RC(fromString(&obj4, "\t\n\rnull;\r\n\t", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, true, false, 0, "", true, RTCRestDate::kFormat_Rfc2822);

        RTTESTI_CHECK_RC(fromString(&obj4, "Mon, 10 Sep 2018 11:58:07 +0000", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, false, true, iRecentSec, "Mon, 10 Sep 2018 11:58:07 +0000", false, RTCRestDate::kFormat_Rfc2822);

        RTTESTI_CHECK_RC(fromString(&obj4, "1970-01-01T00:00:00.000000000Z", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, false, true, 0, "1970-01-01T00:00:00.000000000Z", true, RTCRestDate::kFormat_Rfc3339_Fraction_9);

        RTTESTI_CHECK_RC(fromString(&obj4, "10 Sep 2018 11:58:07 -0000", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, false, true, iRecentSec, "10 Sep 2018 11:58:07 -0000", true, RTCRestDate::kFormat_Rfc2822);

        RTTESTI_CHECK_RC(fromString(&obj4, "null", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, true, false, 0, "", true, RTCRestDate::kFormat_Rfc2822);

        RTTESTI_CHECK_RC(fromString(&obj4, "Mon, 10 Sep 18 11:58:07 -0000", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, false, true, iRecentSec, "Mon, 10 Sep 18 11:58:07 -0000", true, RTCRestDate::kFormat_Rfc2822);

        RTTESTI_CHECK_RC(fromString(&obj4, "fa;se", &ErrInfo, RT_XSTR(__LINE__)), VERR_REST_UNABLE_TO_DECODE_DATE);
        CHECK_DATE_FMT(obj4, false, false, 0, "fa;se", false, RTCRestDate::kFormat_Rfc2822);

        RTTESTI_CHECK_RC(fromString(&obj4, "10 Sep 18 11:58:07", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, false, true, iRecentSec, "10 Sep 18 11:58:07", false, RTCRestDate::kFormat_Rfc2822);

        obj4.setNull();
        RTTESTI_CHECK_RC(fromString(&obj4, "10 Sep 118 11:58:07", &ErrInfo, RT_XSTR(__LINE__)), VINF_SUCCESS);
        CHECK_DATE_FMT(obj4, false, true, iRecentSec, "10 Sep 118 11:58:07", false, RTCRestDate::kFormat_Rfc2822);
    }
}


/** Wraps RTCRestInt16 to check for leaks.    */
class MyRestInt16 : public RTCRestInt16
{
public:
    static size_t s_cInstances;
    MyRestInt16() : RTCRestInt16() { s_cInstances++; /*printf("%p: default %02u; cInstances %zu\n", this, m_iValue, s_cInstances);*/  }
    MyRestInt16(MyRestInt16 const &a_rThat) : RTCRestInt16(a_rThat) { s_cInstances++; /*printf("%p: copy    %02u; cInstances %zu\n", this, m_iValue, s_cInstances);*/ }
    MyRestInt16(int16_t a_iValue) : RTCRestInt16(a_iValue) { s_cInstances++; /*printf("%p: value   %02u; cInstances %zu\n", this, m_iValue, s_cInstances);*/ }
    ~MyRestInt16() { s_cInstances--; /*printf("%p: delete  %02u; cInstances %zu\n", this, m_iValue, s_cInstances);*/ }
    virtual RTCRestObjectBase *baseClone() const RT_NOEXCEPT RT_OVERRIDE { /*printf("clone\n");*/ return new MyRestInt16(*this); }
};

size_t MyRestInt16::s_cInstances = 0;


static void verifyArray(RTCRestArray<MyRestInt16> const &rArray, int iLine, unsigned cElements, ...)
{
    if (rArray.size() != cElements)
        RTTestIFailed("line %u: size() -> %zu, expected %u", iLine, rArray.size(), cElements);
    va_list va;
    va_start(va, cElements);
    for (unsigned i = 0; i < cElements; i++)
    {
        int iExpected = va_arg(va, int);
        if (rArray.at(i)->m_iValue != iExpected)
            RTTestIFailed("line %u: element #%u: %d, expected %d", iLine, i, rArray.at(i)->m_iValue, iExpected);
    }
    va_end(va);
}


static void testArray()
{
    RTTestSub(g_hTest, "RTCRestArray");

    {
        RTCRestArray<RTCRestBool> obj1;
        RTTESTI_CHECK(obj1.size() == 0);
        RTTESTI_CHECK(obj1.isEmpty() == true);
        RTTESTI_CHECK(obj1.isNull() == false);
        RTTESTI_CHECK(strcmp(obj1.typeName(), "RTCRestArray<ElementType>") == 0);
        RTTESTI_CHECK(obj1.typeClass() == RTCRestObjectBase::kTypeClass_Array);
    }

    /* Some random order insertion and manipulations: */
    {
        RTCRestArray<MyRestInt16> Arr2;
        RTCRestArray<MyRestInt16> const *pConstArr2 = &Arr2;

        RTTESTI_CHECK_RC(Arr2.insert(0, new MyRestInt16(3)), VINF_SUCCESS);
        verifyArray(Arr2, __LINE__, 1,  3);
        RTTESTI_CHECK_RC(Arr2.append(  new MyRestInt16(7)), VINF_SUCCESS);
        verifyArray(Arr2, __LINE__, 2,  3, 7);
        RTTESTI_CHECK_RC(Arr2.insert(1, new MyRestInt16(5)), VINF_SUCCESS);
        verifyArray(Arr2, __LINE__, 3,  3, 5, 7);
        RTTESTI_CHECK_RC(Arr2.insert(2, new MyRestInt16(6)), VINF_SUCCESS);
        verifyArray(Arr2, __LINE__, 4,  3, 5, 6, 7);
        RTTESTI_CHECK_RC(Arr2.prepend(  new MyRestInt16(0)), VINF_SUCCESS);
        verifyArray(Arr2, __LINE__, 5,  0, 3, 5, 6, 7);
        RTTESTI_CHECK_RC(Arr2.append(   new MyRestInt16(9)), VINF_SUCCESS);
        verifyArray(Arr2, __LINE__, 6,  0, 3, 5, 6, 7, 9);
        RTTESTI_CHECK_RC(Arr2.insert(5, new MyRestInt16(8)), VINF_SUCCESS);
        verifyArray(Arr2, __LINE__, 7,  0, 3, 5, 6, 7, 8, 9);
        RTTESTI_CHECK_RC(Arr2.insert(1, new MyRestInt16(1)), VINF_SUCCESS);
        verifyArray(Arr2, __LINE__, 8,  0, 1, 3, 5, 6, 7, 8, 9);
        RTTESTI_CHECK_RC(Arr2.insert(3, new MyRestInt16(4)), VINF_SUCCESS);
        verifyArray(Arr2, __LINE__, 9,  0, 1, 3, 4, 5, 6, 7, 8, 9);
        RTTESTI_CHECK_RC(Arr2.insert(2, new MyRestInt16(2)), VINF_SUCCESS);
        verifyArray(Arr2, __LINE__, 10, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
        RTTESTI_CHECK_MSG(MyRestInt16::s_cInstances == Arr2.size(), ("%zu vs %zu\n", MyRestInt16::s_cInstances, Arr2.size()));

        RTTESTI_CHECK(Arr2.size() == 10);

        for (size_t i = 0; i < Arr2.size(); i++)
        {
            MyRestInt16 *pCur = Arr2.at(i);
            RTTESTI_CHECK(pCur->m_iValue == (int16_t)i);

            MyRestInt16 const *pCur2 = pConstArr2->at(i);
            RTTESTI_CHECK(pCur2->m_iValue == (int16_t)i);
        }

        RTTESTI_CHECK_RC(Arr2.replace(2, new MyRestInt16(22)), VWRN_ALREADY_EXISTS);
        verifyArray(Arr2, __LINE__, 10, 0, 1, 22, 3, 4, 5, 6, 7, 8, 9);

        RTTESTI_CHECK_RC(Arr2.replace(7, new MyRestInt16(77)), VWRN_ALREADY_EXISTS);
        verifyArray(Arr2, __LINE__, 10, 0, 1, 22, 3, 4, 5, 6, 77, 8, 9);

        RTTESTI_CHECK_RC(Arr2.replace(10, new MyRestInt16(10)), VINF_SUCCESS);
        verifyArray(Arr2, __LINE__, 11, 0, 1, 22, 3, 4, 5, 6, 77, 8, 9, 10);

        RTTESTI_CHECK_RC(Arr2.replaceCopy(2, MyRestInt16(2)), VWRN_ALREADY_EXISTS);
        verifyArray(Arr2, __LINE__, 11, 0, 1, 2, 3, 4, 5, 6, 77, 8, 9, 10);
        RTTESTI_CHECK_MSG(MyRestInt16::s_cInstances == Arr2.size(), ("%zu vs %zu\n", MyRestInt16::s_cInstances, Arr2.size()));

        /* copy constructor: */
        {
            RTCRestArray<MyRestInt16> const Arr2Copy(Arr2);
            verifyArray(Arr2Copy, __LINE__, 11, 0, 1, 2, 3, 4, 5, 6, 77, 8, 9, 10);
        }
        verifyArray(Arr2, __LINE__, 11, 0, 1, 2, 3, 4, 5, 6, 77, 8, 9, 10);
        RTTESTI_CHECK_MSG(MyRestInt16::s_cInstances == Arr2.size(), ("%zu vs %zu\n", MyRestInt16::s_cInstances, Arr2.size()));

        {
            RTCRestArray<MyRestInt16> Arr2Copy2(Arr2);
            verifyArray(Arr2Copy2, __LINE__, 11, 0, 1, 2, 3, 4, 5, 6, 77, 8, 9, 10);
            RTTESTI_CHECK_RC(Arr2Copy2.removeAt(7), VINF_SUCCESS);
            verifyArray(Arr2Copy2, __LINE__, 10, 0, 1, 2, 3, 4, 5, 6, 8, 9, 10);
        }
        verifyArray(Arr2, __LINE__, 11, 0, 1, 2, 3, 4, 5, 6, 77, 8, 9, 10);
        RTTESTI_CHECK_MSG(MyRestInt16::s_cInstances == Arr2.size(), ("%zu vs %zu\n", MyRestInt16::s_cInstances, Arr2.size()));

        /* copy method + clear: */
        {
            RTCRestArray<MyRestInt16> Arr2Copy3;
            RTTESTI_CHECK_RC(Arr2Copy3.assignCopy(Arr2), VINF_SUCCESS);
            verifyArray(Arr2Copy3, __LINE__, 11, 0, 1, 2, 3, 4, 5, 6, 77, 8, 9, 10);
            Arr2Copy3.at(3)->m_iValue = 33;
            verifyArray(Arr2Copy3, __LINE__, 11, 0, 1, 2, 33, 4, 5, 6, 77, 8, 9, 10);
            Arr2Copy3.clear();
            verifyArray(Arr2Copy3, __LINE__, 0);
            RTTESTI_CHECK_MSG(MyRestInt16::s_cInstances == Arr2.size(), ("%zu vs %zu\n", MyRestInt16::s_cInstances, Arr2.size()));
        }
        verifyArray(Arr2, __LINE__, 11, 0, 1, 2, 3, 4, 5, 6, 77, 8, 9, 10);
        RTTESTI_CHECK_MSG(MyRestInt16::s_cInstances == Arr2.size(), ("%zu vs %zu\n", MyRestInt16::s_cInstances, Arr2.size()));

        /* Check setNull and resetToDefaults with copies: */
        {
            RTCRestArray<MyRestInt16> Arr2Copy4(Arr2);
            verifyArray(Arr2Copy4, __LINE__, 11, 0, 1, 2, 3, 4, 5, 6, 77, 8, 9, 10);

            RTTESTI_CHECK_RC(Arr2Copy4.setNull(), VINF_SUCCESS);
            verifyArray(Arr2Copy4, __LINE__, 0);
            RTTESTI_CHECK_MSG(MyRestInt16::s_cInstances == Arr2.size(), ("%zu vs %zu\n", MyRestInt16::s_cInstances, Arr2.size()));
            RTTESTI_CHECK(Arr2Copy4.isNull() == true);

            RTTESTI_CHECK_RC(Arr2Copy4.resetToDefault(), VINF_SUCCESS);
            RTTESTI_CHECK(Arr2Copy4.isNull() == false);
            verifyArray(Arr2Copy4, __LINE__, 0);
        }
        verifyArray(Arr2, __LINE__, 11, 0, 1, 2, 3, 4, 5, 6, 77, 8, 9, 10);
        {
            RTCRestArray<MyRestInt16> Arr2Copy5(Arr2);
            verifyArray(Arr2Copy5, __LINE__, 11, 0, 1, 2, 3, 4, 5, 6, 77, 8, 9, 10);

            RTTESTI_CHECK_RC(Arr2Copy5.resetToDefault(), VINF_SUCCESS);
            verifyArray(Arr2Copy5, __LINE__, 0);
            RTTESTI_CHECK_MSG(MyRestInt16::s_cInstances == Arr2.size(), ("%zu vs %zu\n", MyRestInt16::s_cInstances, Arr2.size()));
            RTTESTI_CHECK(Arr2Copy5.isNull() == false);

            RTTESTI_CHECK_RC(Arr2Copy5.setNull(), VINF_SUCCESS);
            RTTESTI_CHECK(Arr2Copy5.isNull() == true);

            RTTESTI_CHECK_RC(Arr2Copy5.append(new MyRestInt16(100)), VINF_SUCCESS);
            RTTESTI_CHECK(Arr2Copy5.isNull() == false);
            verifyArray(Arr2Copy5, __LINE__, 1, 100);
            RTTESTI_CHECK_MSG(MyRestInt16::s_cInstances == Arr2.size() + 1, ("%zu vs %zu\n", MyRestInt16::s_cInstances, Arr2.size() + 1));
        }
        verifyArray(Arr2, __LINE__, 11, 0, 1, 2, 3, 4, 5, 6, 77, 8, 9, 10);

        RTTESTI_CHECK_MSG(MyRestInt16::s_cInstances == Arr2.size(), ("%zu vs %zu\n", MyRestInt16::s_cInstances, Arr2.size()));
    }
    RTTESTI_CHECK_MSG(MyRestInt16::s_cInstances == 0, ("%zu\n", MyRestInt16::s_cInstances));

    {
        RTCRestArray<RTCRestInt64> Arr3;
        RTCRestArray<RTCRestInt64> const *pConstArr3 = &Arr3;

        /* Insert a range of numbers into a int64 array. */
        for (int64_t i = 0; i < _64K; i++)
        {
            if (i & 1)
            {
                RTCRestInt64 toCopy(i);
                if (i & 2)
                    RTTESTI_CHECK_RC(Arr3.insertCopy(i, toCopy), VINF_SUCCESS);
                else
                    RTTESTI_CHECK_RC(Arr3.appendCopy(toCopy), VINF_SUCCESS);
            }
            else
            {
                RTCRestInt64 *pDirect = new RTCRestInt64(i);
                if (i & 2)
                    RTTESTI_CHECK_RC(Arr3.insert(i, pDirect), VINF_SUCCESS);
                else
                    RTTESTI_CHECK_RC(Arr3.append(pDirect), VINF_SUCCESS);
            }
            RTTESTI_CHECK(Arr3.size() == (size_t)i + 1);
            RTTESTI_CHECK(Arr3.isEmpty() == false);
        }

        /* Verify insertions: */
        size_t cElements = _64K;
        RTTESTI_CHECK(Arr3.size() == cElements);

        for (int64_t i = 0; i < _64K; i++)
        {
            RTCRestInt64 *pCur = Arr3.at(i);
            RTTESTI_CHECK(pCur->m_iValue == i);

            RTCRestInt64 const *pCur2 = pConstArr3->at(i);
            RTTESTI_CHECK(pCur2->m_iValue == i);
        }
        RTTESTI_CHECK(Arr3.first()->m_iValue == 0);
        RTTESTI_CHECK(Arr3.last()->m_iValue == _64K - 1);
        RTTESTI_CHECK(pConstArr3->first()->m_iValue == 0);
        RTTESTI_CHECK(pConstArr3->last()->m_iValue == _64K - 1);

        /* Remove every 3rd element: */
        RTTESTI_CHECK(Arr3.size() == cElements);
        for (int64_t i = _64K - 1; i >= 0; i -= 3)
        {
            RTTESTI_CHECK_RC(Arr3.removeAt(i), VINF_SUCCESS);
            cElements--;
            RTTESTI_CHECK(Arr3.size() == cElements);
        }

        /* Verify after removal: */
        for (int64_t i = 0, iValue = 0; i < (ssize_t)Arr3.size(); i++, iValue++)
        {
            if ((iValue % 3) == 0)
                iValue++;
            RTTESTI_CHECK_MSG(Arr3.at(i)->m_iValue == iValue, ("%RI64: %RI64 vs %RI64\n", i, Arr3.at(i)->m_iValue, iValue));
        }

        /* Clear it and we're done: */
        Arr3.clear();
        RTTESTI_CHECK(Arr3.size() == 0);
        RTTESTI_CHECK(Arr3.isEmpty() == true);
    }

    {
        RTCRestArray<RTCRestInt32> Arr4;

        /* Insert a range of numbers into a int32 array, in reverse order. */
        for (int32_t i = 0; i < 2048; i++)
        {
            if (i & 1)
            {
                RTCRestInt32 toCopy(i);
                if (i & 2)
                    RTTESTI_CHECK_RC(Arr4.insertCopy(0, toCopy), VINF_SUCCESS);
                else
                    RTTESTI_CHECK_RC(Arr4.prependCopy(toCopy), VINF_SUCCESS);
            }
            else
            {
                RTCRestInt32 *pDirect = new RTCRestInt32(i);
                if (i & 2)
                    RTTESTI_CHECK_RC(Arr4.insert(0, pDirect), VINF_SUCCESS);
                else
                    RTTESTI_CHECK_RC(Arr4.prepend(pDirect), VINF_SUCCESS);
            }
            RTTESTI_CHECK((ssize_t)Arr4.size() == i + 1);
            RTTESTI_CHECK(Arr4.isEmpty() == false);
        }

        for (int32_t i = 0, iValue = (int32_t)Arr4.size() - 1; i < (ssize_t)Arr4.size(); i++, iValue--)
            RTTESTI_CHECK_MSG(Arr4.at(i)->m_iValue == iValue, ("%RI32: %RI32 vs %RI32\n", i, Arr4.at(i)->m_iValue, iValue));

        for (int32_t i = 0; i < 512; i++)
            RTTESTI_CHECK_RC(Arr4.removeAt(0), VINF_SUCCESS);
        RTTESTI_CHECK(Arr4.size() == 1536);

        for (int32_t i = 0; i < 512; i++)
            RTTESTI_CHECK_RC(Arr4.removeAt(~(size_t)0), VINF_SUCCESS);
        RTTESTI_CHECK(Arr4.size() == 1024);

        for (int32_t i = 0, iValue = 1535; i < (ssize_t)Arr4.size(); i++, iValue--)
            RTTESTI_CHECK_MSG(Arr4.at(i)->m_iValue == iValue, ("%RI32: %RI32 vs %RI32\n", i, Arr4.at(i)->m_iValue, iValue));
    }
}


static void verifyMap(RTCRestStringMap<MyRestInt16> const &rMap, int iLine, unsigned cEntries, ...)
{
    if (rMap.size() != cEntries)
        RTTestIFailed("line %u: size() -> %zu, expected %u", iLine, rMap.size(), cEntries);
    if (rMap.isEmpty() != (cEntries ? false : true))
        RTTestIFailed("line %u: isEmpty() -> %RTbool, with %u entries", iLine, rMap.isEmpty(), cEntries);

    va_list va;
    va_start(va, cEntries);
    for (unsigned i = 0; i < cEntries; i++)
    {
        const char *pszKey = va_arg(va, const char *);
        int         iValue = va_arg(va, int);
        if (   rMap.containsKey(pszKey) != true
            || rMap.containsKey(RTCString(pszKey)) != true
            || rMap.get(pszKey) == NULL
            || rMap.get(RTCString(pszKey)) == NULL)
            RTTestIFailed("line %u: entry '%s' not found!", iLine, pszKey);
        else if (rMap.get(pszKey)->m_iValue != iValue)
            RTTestIFailed("line %u: entry '%s' value mismatch: %d, expected %d",
                          iLine, pszKey, rMap.get(pszKey)->m_iValue, iValue);
        RTTESTI_CHECK(rMap.get(pszKey) == rMap.get(RTCString(pszKey)));
    }
    va_end(va);
    RTTESTI_CHECK(rMap.isNull() == false);

    uint64_t fFound = 0;
    for (RTCRestStringMapBase::ConstIterator it = rMap.begin(); it != rMap.end(); ++it)
    {
        MyRestInt16 const *pObj = (MyRestInt16 const *)it.getValue();
        RTTESTI_CHECK(RT_VALID_PTR(pObj));

        bool fFoundIt = false;
        va_start(va, cEntries);
        for (unsigned i = 0; i < cEntries; i++)
        {
            const char *pszKey = va_arg(va, const char *);
            int         iValue = va_arg(va, int);
            if (it.getKey().equals(pszKey))
            {
                if (fFound & RT_BIT_64(i))
                    RTTestIFailed("line %u: base enum: entry '%s' returned more than once!", iLine, pszKey);
                if (pObj->m_iValue != iValue)
                    RTTestIFailed("line %u: base enum: entry '%s' value mismatch: %d, expected %d",
                                  iLine, pszKey, pObj->m_iValue, iValue);
                fFound |= RT_BIT_64(i);
                fFoundIt = true;
                va_end(va);
                return;
            }
        }
        va_end(va);
        if (!fFoundIt)
            RTTestIFailed("line %u: base enum: entry '%s' not expected!", iLine, it.getKey().c_str());
    }
}


static void testStringMap(void)
{
    RTTestSub(g_hTest, "RTCRestMap");

    {
        RTCRestStringMap<RTCRestString> obj1;
        RTTESTI_CHECK(obj1.size() == 0);
        RTTESTI_CHECK(obj1.isEmpty() == true);
        RTTESTI_CHECK(obj1.isNull() == false);
        RTTESTI_CHECK(strcmp(obj1.typeName(), "RTCRestStringMap<ValueType>") == 0);
        RTTESTI_CHECK(obj1.typeClass() == RTCRestObjectBase::kTypeClass_StringMap);
    }

    /* Basic operations: */
    {
        MyRestInt16::s_cInstances = 0;
        RTCRestStringMap<MyRestInt16> Map2;
        verifyMap(Map2, __LINE__, 0);

        RTTESTI_CHECK_RC(Map2.putCopy("0x0004", MyRestInt16(4)), VINF_SUCCESS);
        verifyMap(Map2, __LINE__, 1, "0x0004", 4);
        RTTESTI_CHECK(MyRestInt16::s_cInstances == 1);
        RTTESTI_CHECK_RC(Map2.put("0x0001", new MyRestInt16(1)), VINF_SUCCESS);
        verifyMap(Map2, __LINE__, 2, "0x0004",4, "0x0001",1);
        RTTESTI_CHECK_RC(Map2.put("0x0003", new MyRestInt16(3)), VINF_SUCCESS);
        verifyMap(Map2, __LINE__, 3, "0x0004",4, "0x0001",1, "0x0003",3);
        RTTESTI_CHECK_RC(Map2.put("0x0002", new MyRestInt16(2)), VINF_SUCCESS);
        verifyMap(Map2, __LINE__, 4, "0x0004",4, "0x0001",1, "0x0003",3, "0x0002",2);
        RTTESTI_CHECK(MyRestInt16::s_cInstances == 4);
        RTTESTI_CHECK_RC(Map2.put("0x0000", new MyRestInt16(0)), VINF_SUCCESS);
        verifyMap(Map2, __LINE__, 5, "0x0004",4, "0x0001",1, "0x0003",3, "0x0002",2, "0x0000",0);
        RTTESTI_CHECK(MyRestInt16::s_cInstances == 5);
        RTTESTI_CHECK_RC(Map2.putCopy("towel", MyRestInt16(42)), VINF_SUCCESS);
        verifyMap(Map2, __LINE__, 6, "0x0004",4, "0x0001",1, "0x0003",3, "0x0002",2, "0x0000",0, "towel",42);
        RTTESTI_CHECK(MyRestInt16::s_cInstances == 6);

        RTTESTI_CHECK(Map2.containsKey("0x0005") == false);
        RTTESTI_CHECK(Map2.get("0x0005") == NULL);

        RTTESTI_CHECK(Map2.remove("0x0003") == true);
        verifyMap(Map2, __LINE__, 5, "0x0004",4, "0x0001",1, "0x0002",2, "0x0000",0, "towel",42);
        RTTESTI_CHECK(MyRestInt16::s_cInstances == 5);

        RTTESTI_CHECK(Map2.remove("0x0003") == false);
        verifyMap(Map2, __LINE__, 5, "0x0004",4, "0x0001",1, "0x0002",2, "0x0000",0, "towel",42);
        RTTESTI_CHECK(MyRestInt16::s_cInstances == 5);

        RTCRestObjectBase *pNewBase = NULL;
        RTTESTI_CHECK_RC(Map2.putNewValue(&pNewBase, "putNewValue"), VINF_SUCCESS);
        ((MyRestInt16 *)pNewBase)->m_iValue = 88;
        RTTESTI_CHECK(MyRestInt16::s_cInstances == 6);
        verifyMap(Map2, __LINE__, 6, "0x0004",4, "0x0001",1, "0x0002",2, "0x0000",0, "towel",42, "putNewValue",88);

        pNewBase = NULL;
        RTTESTI_CHECK_RC(Map2.putNewValue(&pNewBase, RTCString("putNewValue")), VERR_ALREADY_EXISTS);
        verifyMap(Map2, __LINE__, 6, "0x0004",4, "0x0001",1, "0x0002",2, "0x0000",0, "towel",42, "putNewValue",88);
        pNewBase = NULL;
        RTTESTI_CHECK_RC(Map2.putNewValue(&pNewBase, RTCString("putNewValue"), true /*a_fReplace*/), VWRN_ALREADY_EXISTS);
        verifyMap(Map2, __LINE__, 6, "0x0004",4, "0x0001",1, "0x0002",2, "0x0000",0, "towel",42, "putNewValue",0);
        RTTESTI_CHECK(MyRestInt16::s_cInstances == 6);

        /* Make copy and remove all: */
        {
            RTCRestStringMap<MyRestInt16> Map2Copy1;

            RTTESTI_CHECK_RC(Map2Copy1.assignCopy(Map2), VINF_SUCCESS);
            verifyMap(Map2Copy1, __LINE__, 6, "0x0004",4, "0x0001",1, "0x0002",2, "0x0000",0, "towel",42, "putNewValue",0);
            RTTESTI_CHECK(MyRestInt16::s_cInstances == 12);

            RTTESTI_CHECK(Map2Copy1.remove("0x0004") == true);
            verifyMap(Map2Copy1, __LINE__, 5, "0x0001",1, "0x0002",2, "0x0000",0, "towel",42, "putNewValue",0);
            RTTESTI_CHECK(MyRestInt16::s_cInstances == 11);

            RTTESTI_CHECK(Map2Copy1.remove("putNewValue") == true);
            verifyMap(Map2Copy1, __LINE__, 4, "0x0001",1, "0x0002",2, "0x0000",0, "towel",42);
            RTTESTI_CHECK(MyRestInt16::s_cInstances == 10);

            RTTESTI_CHECK(Map2Copy1.remove("towel") == true);
            verifyMap(Map2Copy1, __LINE__, 3, "0x0001",1, "0x0002",2, "0x0000",0);
            RTTESTI_CHECK(MyRestInt16::s_cInstances == 9);

            RTTESTI_CHECK(Map2Copy1.remove("0x0002") == true);
            verifyMap(Map2Copy1, __LINE__, 2, "0x0001",1, "0x0000",0);
            RTTESTI_CHECK(MyRestInt16::s_cInstances == 8);

            RTTESTI_CHECK(Map2Copy1.remove("0x0000") == true);
            verifyMap(Map2Copy1, __LINE__, 1, "0x0001",1);
            RTTESTI_CHECK(MyRestInt16::s_cInstances == 7);

            RTTESTI_CHECK(Map2Copy1.remove("0x0001") == true);
            verifyMap(Map2Copy1, __LINE__, 0);
            RTTESTI_CHECK(Map2Copy1.isEmpty() == true);
            RTTESTI_CHECK(MyRestInt16::s_cInstances == 6);
        }
        verifyMap(Map2, __LINE__, 6, "0x0004",4, "0x0001",1, "0x0002",2, "0x0000",0, "towel",42, "putNewValue",0);

        /* Make copy and use clear: */
        {
            RTCRestStringMap<MyRestInt16> Map2Copy2(Map2);
            verifyMap(Map2Copy2, __LINE__, 6, "0x0004",4, "0x0001",1, "0x0002",2, "0x0000",0, "towel",42, "putNewValue",0);
            RTTESTI_CHECK(MyRestInt16::s_cInstances == 12);
            Map2Copy2.clear();
            verifyMap(Map2Copy2, __LINE__, 0);
            RTTESTI_CHECK(Map2Copy2.isEmpty() == true);
            RTTESTI_CHECK(MyRestInt16::s_cInstances == 6);
        }
        verifyMap(Map2, __LINE__, 6, "0x0004",4, "0x0001",1, "0x0002",2, "0x0000",0, "towel",42, "putNewValue",0);

        /* Make copy and reset to default: */
        {
            RTCRestStringMap<MyRestInt16> Map2Copy3(Map2);
            verifyMap(Map2Copy3, __LINE__, 6, "0x0004",4, "0x0001",1, "0x0002",2, "0x0000",0, "towel",42, "putNewValue",0);
            RTTESTI_CHECK(MyRestInt16::s_cInstances == 12);
            RTTESTI_CHECK_RC(Map2Copy3.resetToDefault(), VINF_SUCCESS);
            verifyMap(Map2Copy3, __LINE__, 0);
            RTTESTI_CHECK(Map2Copy3.isEmpty() == true);
            RTTESTI_CHECK(MyRestInt16::s_cInstances == 6);
        }
        verifyMap(Map2, __LINE__, 6, "0x0004",4, "0x0001",1, "0x0002",2, "0x0000",0, "towel",42, "putNewValue",0);

        /* Make copy and set to null: */
        {
            RTCRestStringMap<MyRestInt16> Map2Copy4;
            Map2Copy4 = Map2;
            verifyMap(Map2Copy4, __LINE__, 6, "0x0004",4, "0x0001",1, "0x0002",2, "0x0000",0, "towel",42, "putNewValue",0);
            RTTESTI_CHECK(MyRestInt16::s_cInstances == 12);
            RTTESTI_CHECK_RC(Map2Copy4.setNull(), VINF_SUCCESS);
            RTTESTI_CHECK(Map2Copy4.size() == 0);
            RTTESTI_CHECK(Map2Copy4.isEmpty() == true);
            RTTESTI_CHECK(Map2Copy4.isNull() == true);
            RTTESTI_CHECK(MyRestInt16::s_cInstances == 6);
        }
        verifyMap(Map2, __LINE__, 6, "0x0004",4, "0x0001",1, "0x0002",2, "0x0000",0, "towel",42, "putNewValue",0);
    }
    RTTESTI_CHECK(MyRestInt16::s_cInstances == 0);

    /* Check that null indicator is reset when it should: */
    {
        RTCRestStringMap<MyRestInt16> Map3;
        Map3.setNull();
        RTTESTI_CHECK_RC(Map3.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(Map3.size() == 0);
        RTTESTI_CHECK(Map3.isEmpty() == true);
        RTTESTI_CHECK(Map3.isNull() == true);
        RTTESTI_CHECK_RC(Map3.putCopy("not-null-anymore", MyRestInt16(1)), VINF_SUCCESS);
        verifyMap(Map3, __LINE__, 1, "not-null-anymore",1);
    }
    RTTESTI_CHECK(MyRestInt16::s_cInstances == 0);

    {
        RTCRestStringMap<MyRestInt16> Map4;
        Map4.setNull();
        RTTESTI_CHECK_RC(Map4.setNull(), VINF_SUCCESS);
        RTTESTI_CHECK(Map4.size() == 0);
        RTTESTI_CHECK(Map4.isEmpty() == true);
        RTTESTI_CHECK(Map4.isNull() == true);
        RTCRestObjectBase *pNewBase = NULL;
        RTTESTI_CHECK_RC(Map4.putNewValue(&pNewBase, "putNewValue"), VINF_SUCCESS);
        verifyMap(Map4, __LINE__, 1, "putNewValue",0);
    }
    RTTESTI_CHECK(MyRestInt16::s_cInstances == 0);
}


class TestRequest : public RTCRestClientRequestBase
{
public:
    RTCRestString m_strValue;
    RTCRestInt64  m_iValue;
    RTCRestArray<RTCRestString> m_Array;
    RTCRestStringMap<RTCRestString> m_Map;
    /** @todo add more attributes. */

    TestRequest(const char *a_pszValue, int64_t a_iValue, unsigned a_cElements, ...)
        : RTCRestClientRequestBase()
        , m_strValue(a_pszValue)
        , m_iValue(a_iValue)
    {
        m_fIsSet = UINT64_MAX;
        va_list va;
        va_start(va, a_cElements);
        for (unsigned i = 0; i < a_cElements; i++)
            m_Array.append(new RTCRestString(va_arg(va, const char *)));
        va_end(va);
    }

    int resetToDefault() RT_NOEXCEPT RT_OVERRIDE
    {
        m_strValue = "";
        m_iValue = 0;
        return m_Array.resetToDefault();
    }

    const char *getOperationName() const RT_NOEXCEPT RT_OVERRIDE
    {
        return "Test";
    }

    int xmitPrepare(RTCString *a_pStrPath, RTCString *a_pStrQuery, RTHTTP a_hHttp, RTCString *a_pStrBody) const RT_NOEXCEPT RT_OVERRIDE
    {
        RT_NOREF(a_pStrPath, a_pStrQuery, a_hHttp, a_pStrBody);
        return VINF_SUCCESS;
    }

    void xmitComplete(int a_rcStatus, RTHTTP a_hHttp) const RT_NOEXCEPT RT_OVERRIDE
    {
        RT_NOREF(a_rcStatus, a_hHttp);
    }

    void testPath(const char *a_pszExpected)
    {
        static PATHPARAMDESC const s_aParams[] =
        {
            { RT_STR_TUPLE("{string}"), 0, 0 },
            { RT_STR_TUPLE("{integer}"), 0, 0 },
            { RT_STR_TUPLE("{array}"), 0, 0 },
        };
        PATHPARAMSTATE aState[] = { { &m_strValue, 0 }, { &m_iValue, 0 }, { &m_Array, 0 } };
        RTCString strPath;
        RTTESTI_CHECK_RC(doPathParameters(&strPath, RT_STR_TUPLE("my/{integer}/{string}/array:{array}/path"),
                                          s_aParams, aState, RT_ELEMENTS(aState)), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(strPath.equals(a_pszExpected), ("actual: %s\nexpect: %s\n", strPath.c_str(), a_pszExpected));
    }

    void testQuery(const char *a_pszCsv,
                   const char *a_pszSsv,
                   const char *a_pszTsv,
                   const char *a_pszPipes,
                   const char *a_pszMulti)
    {
        QUERYPARAMDESC aParams[] =
        {
            { "string", 0, true, 0 },
            { "integer", 0, true, 0 },
            { "array", 0, true, 0 },
        };

        RTCRestObjectBase const *apObjects[] =  { &m_strValue,  &m_iValue, &m_Array };
        RTCString strQuery;
        RTTESTI_CHECK_RC(doQueryParameters(&strQuery, aParams, apObjects, RT_ELEMENTS(apObjects)), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(strQuery.equals(a_pszCsv), ("actual: %s\nexpect: %s\n", strQuery.c_str(), a_pszCsv));

        strQuery.setNull();
        aParams[2].fFlags = RTCRestObjectBase::kCollectionFormat_csv;
        RTTESTI_CHECK_RC(doQueryParameters(&strQuery, aParams, apObjects, RT_ELEMENTS(apObjects)), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(strQuery.equals(a_pszCsv), ("actual: %s\nexpect: %s\n", strQuery.c_str(), a_pszCsv));

        strQuery.setNull();
        aParams[2].fFlags = RTCRestObjectBase::kCollectionFormat_ssv;
        RTTESTI_CHECK_RC(doQueryParameters(&strQuery, aParams, apObjects, RT_ELEMENTS(apObjects)), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(strQuery.equals(a_pszSsv), ("actual: %s\nexpect: %s\n", strQuery.c_str(), a_pszSsv));

        strQuery.setNull();
        aParams[2].fFlags = RTCRestObjectBase::kCollectionFormat_tsv;
        RTTESTI_CHECK_RC(doQueryParameters(&strQuery, aParams, apObjects, RT_ELEMENTS(apObjects)), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(strQuery.equals(a_pszTsv), ("actual: %s\nexpect: %s\n", strQuery.c_str(), a_pszTsv));

        strQuery.setNull();
        aParams[2].fFlags = RTCRestObjectBase::kCollectionFormat_pipes;
        RTTESTI_CHECK_RC(doQueryParameters(&strQuery, aParams, apObjects, RT_ELEMENTS(apObjects)), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(strQuery.equals(a_pszPipes), ("actual: %s\nexpect: %s\n", strQuery.c_str(), a_pszPipes));

        strQuery.setNull();
        aParams[2].fFlags = RTCRestObjectBase::kCollectionFormat_multi;
        RTTESTI_CHECK_RC(doQueryParameters(&strQuery, aParams, apObjects, RT_ELEMENTS(apObjects)), VINF_SUCCESS);
        RTTESTI_CHECK_MSG(strQuery.equals(a_pszMulti), ("actual: %s\nexpect: %s\n", strQuery.c_str(), a_pszMulti));
    }

    void testHeader(unsigned a_cHeaders, ...)
    {
        HEADERPARAMDESC aParams[] =
        {
            { "x-string", 0, true, 0, false },
            { "x-integer", 0, true, 0, false },
            { "x-array", 0, true, 0, false },
            { "x-map-", 0, true, 0, true },
        };
        RTCRestObjectBase const *apObjects[] =  { &m_strValue,  &m_iValue, &m_Array, &m_Map };
        RTHTTP hHttp = NIL_RTHTTP;
        RTTESTI_CHECK_RC(RTHttpCreate(&hHttp), VINF_SUCCESS);
        RTTESTI_CHECK_RC(doHeaderParameters(hHttp, aParams, apObjects, RT_ELEMENTS(apObjects)), VINF_SUCCESS);
        RTTESTI_CHECK(RTHttpGetHeaderCount(hHttp) == a_cHeaders);
        va_list va;
        va_start(va, a_cHeaders);
        for (size_t i = 0; i < a_cHeaders; i++)
        {
            const char *pszField  = va_arg(va, const char *);
            const char *pszValue  = va_arg(va, const char *);
            const char *pszActual = RTHttpGetHeader(hHttp, pszField, RTSTR_MAX);
            RTTESTI_CHECK_MSG(RTStrCmp(pszActual, pszValue) == 0,
                              ("Header '%s' value is '%s' rather than '%s'", pszField, pszActual, pszValue));
        }
        va_end(va);
        RTTESTI_CHECK_RC(RTHttpDestroy(hHttp), VINF_SUCCESS);
    }
};


static void testClientRequestBase()
{
    RTTestSub(g_hTest, "RTCRestClientRequestBase");
    {
        TestRequest Req1("this-is-a-string", 123456789, 5, "1", "22", "333", "444", "555");
        Req1.testPath("my/123456789/this-is-a-string/array:1%2C22%2C333%2C444%2C555/path");
        Req1.testQuery("?string=this-is-a-string&integer=123456789&array=1%2C22%2C333%2C444%2C555",
                       "?string=this-is-a-string&integer=123456789&array=1%2022%20333%20444%20555",
                       "?string=this-is-a-string&integer=123456789&array=1%0922%09333%09444%09555",
                       "?string=this-is-a-string&integer=123456789&array=1%7C22%7C333%7C444%7C555",
                       "?string=this-is-a-string&integer=123456789&array=1&array=22&array=333&array=444&array=555");
        Req1.testHeader(3, "x-string","this-is-a-string", "x-integer","123456789", "x-array","1,22,333,444,555");
    }
    {
        TestRequest Req2(";'[]", 42, 3, "null", "foo", "bar");
        RTTESTI_CHECK_RC(Req2.m_Map.put("stuff-1", new RTCRestString("stuffy-value-1")), VINF_SUCCESS);
        RTTESTI_CHECK_RC(Req2.m_Map.put("stuff-2", new RTCRestString("stuffy-value-2")), VINF_SUCCESS);
        RTTESTI_CHECK_RC(Req2.m_Map.put("2222", new RTCRestString("33")), VINF_SUCCESS);
        Req2.testPath("my/42/%3B%27%5B%5D/array:null%2Cfoo%2Cbar/path");
        Req2.testQuery("?string=%3B%27%5B%5D&integer=42&array=null%2Cfoo%2Cbar",
                       "?string=%3B%27%5B%5D&integer=42&array=null%20foo%20bar",
                       "?string=%3B%27%5B%5D&integer=42&array=null%09foo%09bar",
                       "?string=%3B%27%5B%5D&integer=42&array=null%7Cfoo%7Cbar",
                       "?string=%3B%27%5B%5D&integer=42&array=null&array=foo&array=bar");
        Req2.testHeader(6, "x-string",";'[]", "x-integer","42", "x-array","null,foo,bar",
                        "x-map-stuff-1","stuffy-value-1",
                        "x-map-stuff-2","stuffy-value-2",
                        "x-map-2222","33");
    }
}


class TestResponse : public RTCRestClientResponseBase
{
public:
    RTCRestArray<RTCRestString>         *m_pArray;
    RTCRestStringMap<RTCRestString>     *m_pMap;
    RTCRestInt64                        *m_pInteger;
    RTCRestString                       *m_pStrContentType;

    TestResponse() : m_pArray(NULL), m_pMap(NULL), m_pInteger(NULL), m_pStrContentType(NULL)
    { }

    ~TestResponse()
    {
        if (m_pStrContentType)
            delete m_pStrContentType;
        if (m_pInteger)
            delete m_pInteger;
        if (m_pMap)
            delete m_pMap;
        if (m_pArray)
            delete m_pArray;
    }

    const char *getOperationName() const RT_NOEXCEPT RT_OVERRIDE
    {
        return "Test";
    }

protected:
    virtual int consumeHeader(uint32_t a_uMatchWord, const char *a_pchField, size_t a_cchField,
                              const char *a_pchValue, size_t a_cchValue) RT_NOEXCEPT RT_OVERRIDE
    {
        int rc = RTCRestClientResponseBase::consumeHeader(a_uMatchWord, a_pchField, a_cchField, a_pchValue, a_cchValue);
        AssertRCReturn(rc, rc);

#define MATCH_FIELD(a_sz) (sizeof(a_sz) - 1 == a_cchField && RTStrNICmpAscii(a_pchField, RT_STR_TUPLE(a_sz)) == 0)
        if (MATCH_FIELD("x-array"))
        {
            if (!m_pArray)
            {
                m_pArray = new (std::nothrow) RTCRestArray<RTCRestString>();
                AssertReturn(m_pArray, VERR_NO_MEMORY);
                return deserializeHeader(m_pArray, a_pchValue, a_cchValue, RTCRestObjectBase::kCollectionFormat_csv, "x-array");
            }
        }
        else if (a_cchField >= sizeof("x-map-") - 1 && RTStrNICmpAscii(a_pchField, RT_STR_TUPLE("x-map-")) == 0)
        {
            if (!m_pMap)
            {
                m_pMap = new (std::nothrow) RTCRestStringMap<RTCRestString>();
                AssertReturn(m_pMap, VERR_NO_MEMORY);
            }
            return deserializeHeaderIntoMap(m_pMap, a_pchField + 6, a_cchField - 6, a_pchValue, a_cchValue, 0, "x-map-");
        }
        else if (MATCH_FIELD("x-integer"))
        {
            if (!m_pInteger)
            {
                m_pInteger = new (std::nothrow) RTCRestInt64();
                AssertReturn(m_pInteger, VERR_NO_MEMORY);
                return deserializeHeader(m_pInteger, a_pchValue, a_cchValue, 0, "x-integer");
            }
        }
        else if (MATCH_FIELD("content-type"))
        {
            if (!m_pStrContentType)
            {
                m_pStrContentType = new (std::nothrow) RTCRestString();
                AssertReturn(m_pStrContentType, VERR_NO_MEMORY);
                return deserializeHeader(m_pStrContentType, a_pchValue, a_cchValue, 0, "content-type");
            }
        }
        else
            return VWRN_NOT_FOUND;
        RT_NOREF(a_uMatchWord);
        return addError(VERR_ALREADY_EXISTS, "Already have field '%.*s'!", a_cchField, a_pchField);
    }

public:
    int pushHeader(const char *pszField, const char *pszValue)
    {
        size_t const cchField = strlen(pszField);
        void *pvFieldCopy = RTTestGuardedAllocTail(g_hTest, cchField);
        RTTESTI_CHECK_RET(pvFieldCopy, VERR_NO_MEMORY);
        memcpy(pvFieldCopy, pszField, cchField);

        size_t const cchValue = strlen(pszValue);
        void *pvValueCopy = RTTestGuardedAllocTail(g_hTest, cchValue);
        RTTESTI_CHECK_RET(pvValueCopy, VERR_NO_MEMORY);
        memcpy(pvValueCopy, pszValue, cchValue);

        uint32_t uWord = RTHTTP_MAKE_HDR_MATCH_WORD(cchField,
                                                    cchField >= 1 ? RT_C_TO_LOWER(pszField[0]) : 0,
                                                    cchField >= 2 ? RT_C_TO_LOWER(pszField[1]) : 0,
                                                    cchField >= 3 ? RT_C_TO_LOWER(pszField[2]) : 0);
        int rc = consumeHeader(uWord, (const char *)pvFieldCopy, cchField, (const char *)pvValueCopy, cchValue);
        RTTestGuardedFree(g_hTest, pvValueCopy);
        RTTestGuardedFree(g_hTest, pvFieldCopy);
        return rc;
    }
};


static void testClientResponseBase()
{
    RTTestSub(g_hTest, "RTCRestClientResponseBase");
    {
        TestResponse Resp1;
        RTTESTI_CHECK_RC(Resp1.pushHeader("content-type", "application/json; charset=utf-8"), VINF_SUCCESS);
        RTTESTI_CHECK(Resp1.getContentType().equals("application/json; charset=utf-8"));
        RTTESTI_CHECK(Resp1.m_pStrContentType && Resp1.m_pStrContentType->equals("application/json; charset=utf-8"));

        RTTESTI_CHECK_RC(Resp1.pushHeader("content-typ2", "oopsy daisy"), VWRN_NOT_FOUND);
        RTTESTI_CHECK_RC(Resp1.pushHeader("content-type2", "oopsy daisy"), VWRN_NOT_FOUND);
        RTTESTI_CHECK(Resp1.getContentType().equals("application/json; charset=utf-8"));
        RTTESTI_CHECK(Resp1.m_pStrContentType && Resp1.m_pStrContentType->equals("application/json; charset=utf-8"));

        RTTESTI_CHECK_RC(Resp1.pushHeader("x-integer", "398679406"), VINF_SUCCESS);
        RTTESTI_CHECK(Resp1.m_pInteger && Resp1.m_pInteger->m_iValue == 398679406);

        RTTESTI_CHECK_RC(Resp1.pushHeader("x-array", "zero,one,two,three"), VINF_SUCCESS);
        RTTESTI_CHECK(Resp1.m_pArray && Resp1.m_pArray->size() == 4);

        RTTESTI_CHECK_RC(Resp1.pushHeader("x-map-", "empty-key"), VINF_SUCCESS);
        RTTESTI_CHECK(Resp1.m_pMap && Resp1.m_pMap->size() == 1 && Resp1.m_pMap->get("") != NULL && Resp1.m_pMap->get("")->equals("empty-key"));

        RTTESTI_CHECK_RC(Resp1.pushHeader("x-map-42", "key-is-42"), VINF_SUCCESS);
        RTTESTI_CHECK(Resp1.m_pMap && Resp1.m_pMap->size() == 2 && Resp1.m_pMap->get("42") != NULL && Resp1.m_pMap->get("42")->equals("key-is-42"));
    }
}


int main()
{
    RTEXITCODE rcExit = RTTestInitAndCreate("tstRTCRest-1", &g_hTest);
    if (rcExit == RTEXITCODE_SUCCESS )
    {
        testBool();
        testInteger<RTCRestInt64, int64_t, Int64Constants>();
        testInteger<RTCRestInt32, int32_t, Int32Constants>();
        testInteger<RTCRestInt16, int16_t, Int16Constants>();
        testDouble();
        testString("dummy", 1, 2);
        testDate();
        testArray();
        testStringMap();
        testClientRequestBase();
        testClientResponseBase();

        rcExit = RTTestSummaryAndDestroy(g_hTest);
    }
    return rcExit;
}

