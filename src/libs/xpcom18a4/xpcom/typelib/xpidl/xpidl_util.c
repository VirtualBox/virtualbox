/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* ***** BEGIN LICENSE BLOCK *****
 * Version: NPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Netscape Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/NPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is 
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or 
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the NPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the NPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

/*
 * Utility functions called by various backends.
 */ 
#include <iprt/assert.h>
#include <iprt/string.h>

#include "xpidl.h"

/* XXXbe static */ char OOM[] = "ERROR: out of memory\n";

void *
xpidl_malloc(size_t nbytes)
{
    void *p = malloc(nbytes);
    if (!p) {
        fputs(OOM, stderr);
        exit(1);
    }
    return p;
}

char *
xpidl_strdup(const char *s)
{
    char *ns = RTStrDup(s);
    if (!ns)
    {
        fputs(OOM, stderr);
        exit(1);
    }
    return ns;
}


/*
 * Print an iid to into a supplied buffer; the buffer should be at least
 * UUID_LENGTH bytes.
 */
bool
xpidl_sprint_iid(nsID *id, char iidbuf[])
{
    int printed;

    printed = sprintf(iidbuf,
                       "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                       (PRUint32) id->m0, (PRUint32) id->m1,(PRUint32) id->m2,
                       (PRUint32) id->m3[0], (PRUint32) id->m3[1],
                       (PRUint32) id->m3[2], (PRUint32) id->m3[3],
                       (PRUint32) id->m3[4], (PRUint32) id->m3[5],
                       (PRUint32) id->m3[6], (PRUint32) id->m3[7]);

#ifdef SPRINTF_RETURNS_STRING
    return (printed && strlen((char *)printed) == 36);
#else
    return (printed == 36);
#endif
}

/* We only parse the {}-less format. */
static const char nsIDFmt2[] =
  "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x";

/*
 * Parse a uuid string into an nsID struct.  We cannot link against libxpcom,
 * so we re-implement nsID::Parse here.
 */
bool
xpidl_parse_iid(nsID *id, const char *str)
{
    PRInt32 count = 0;
    PRInt32 n1, n2, n3[8];
    PRInt32 n0, i;

    XPT_ASSERT(str != NULL);
    
    if (strlen(str) != 36) {
        return false;
    }
     
#ifdef DEBUG_shaver_iid
    fprintf(stderr, "parsing iid   %s\n", str);
#endif

    count = sscanf(str, nsIDFmt2,
                   (uint32_t *)&n0, (uint32_t *)&n1, (uint32_t *)&n2,
                   (uint32_t *)&n3[0],(uint32_t *)&n3[1],(uint32_t *)&n3[2],(uint32_t *)&n3[3],
                   (uint32_t *)&n3[4],(uint32_t *)&n3[5],(uint32_t *)&n3[6],(uint32_t *)&n3[7]);

    id->m0 = (PRInt32) n0;
    id->m1 = (PRInt16) n1;
    id->m2 = (PRInt16) n2;
    for (i = 0; i < 8; i++) {
      id->m3[i] = (PRInt8) n3[i];
    }

#ifdef DEBUG_shaver_iid
    if (count == 11) {
        fprintf(stderr, "IID parsed to ");
        print_IID(id, stderr);
        fputs("\n", stderr);
    }
#endif
    return (count == 11);
}

DECLHIDDEN(int) verify_const_declaration(PCXPIDLNODE pNd, PRTERRINFO pErrInfo)
{
    /* const must be inside an interface definition. */
    if (   !pNd->pParent
        || pNd->pParent->enmType != kXpidlNdType_Interface_Def)
        return xpidlIdlError(pErrInfo, pNd, VERR_INVALID_STATE,
                             "const declaration \'%s\' outside interface",
                             pNd->u.Const.pszName);

    /* Could be a typedef; try to map it to the real type. */
    PCXPIDLNODE pNdType = find_underlying_type(pNd->u.Const.pNdTypeSpec);
    pNdType = pNdType ? pNdType : pNd->u.Const.pNdTypeSpec;
    if (   pNdType->enmType != kXpidlNdType_BaseType
        || (   pNdType->u.enmBaseType != kXpidlType_Short
            && pNdType->u.enmBaseType != kXpidlType_Long
            && pNdType->u.enmBaseType != kXpidlType_Unsigned_Short
            && pNdType->u.enmBaseType != kXpidlType_Unsigned_Long))
        return xpidlIdlError(pErrInfo, pNd, VERR_INVALID_STATE,
                             "const declaration \'%s\' must be of type short or long",
                             pNd->u.Const.pszName);

    return VINF_SUCCESS;
}


DECLHIDDEN(int) verify_attribute_declaration(PCXPIDLNODE pNd, PRTERRINFO pErrInfo)
{
    Assert(pNd->enmType == kXpidlNdType_Attribute);

    /*
     * We don't support attributes named IID, conflicts with static GetIID 
     * member. The conflict is due to certain compilers (VC++) choosing a
     * different vtable order, placing GetIID at the beginning regardless
     * of it's placement
     */
    if (!strcmp(pNd->u.Attribute.pszName, "IID"))
        return xpidlIdlError(pErrInfo, pNd, VERR_INVALID_STATE,
                             "Attributes named IID not supported, causes vtable ordering problems");

    /* 
     * Verify that we've been called on an interface, and decide if the
     * interface was marked [scriptable].
     */
    bool fScriptable;
    if (   pNd->pParent
        && pNd->pParent->enmType == kXpidlNdType_Interface_Def)
        fScriptable = (xpidlNodeAttrFind(pNd->pParent, "scriptable") != NULL);
    else
        return xpidlIdlError(pErrInfo, pNd, VERR_INVALID_STATE,
                             "verify_attribute_declaration called on a non-interface?");

    /*
     * If the interface isn't scriptable, or the attribute is marked noscript,
     * there's no need to check.
     */
    if (!fScriptable || !xpidlNodeAttrFind(pNd, "scriptable"))
        return VINF_SUCCESS;

    /*
     * If it should be scriptable, check that the type is non-native. nsid,
     * domstring, utf8string, cstring, astring are exempted.
     */
    PCXPIDLNODE pNdType = find_underlying_type(pNd->u.Attribute.pNdTypeSpec);
    pNdType = pNdType ? pNdType : pNd->u.Attribute.pNdTypeSpec;
    if (pNdType)
    {
        if (pNdType->enmType == kXpidlNdType_Native &&
            xpidlNodeAttrFind(pNdType, "nsid") == NULL &&
            xpidlNodeAttrFind(pNdType, "domstring") == NULL &&
            xpidlNodeAttrFind(pNdType, "utf8string") == NULL &&
            xpidlNodeAttrFind(pNdType, "cstring") == NULL &&
            xpidlNodeAttrFind(pNdType, "astring") == NULL)
            return xpidlIdlError(pErrInfo, pNd, VERR_INVALID_STATE,
                                 "attributes in [scriptable] interfaces that are "
                                 "non-scriptable because they refer to native "
                                 "types must be marked [noscript]");

        /*
         * We currently don't support properties of type nsid that aren't 
         * pointers or references, unless they are marked [notxpcom} and 
         * must be read-only 
         */
        if (   (   xpidlNodeAttrFind(pNd, "notxpcom") == NULL
                || pNd->u.Attribute.fReadonly)
            && xpidlNodeAttrFind(pNdType,"nsid") != NULL
            && xpidlNodeAttrFind(pNdType,"ptr") == NULL
            && xpidlNodeAttrFind(pNdType,"ref") == NULL)
            return xpidlIdlError(pErrInfo, pNd, VERR_INVALID_STATE,
                                 "Feature not currently supported: "
                                 "attributes with a type of nsid must be marked either [ptr] or [ref], or "
                                 "else must be marked [notxpcom] and must be read-only\n");
    }

    return VINF_SUCCESS;
}


/*
 * Find the underlying type of an identifier typedef.
 */
DECLHIDDEN(PCXPIDLNODE) find_underlying_type(PCXPIDLNODE pNd)
{
    if (pNd->enmType == kXpidlNdType_BaseType)
        return pNd;
    if (pNd == NULL || pNd->enmType != kXpidlNdType_Identifier)
        return NULL;

    AssertPtr(pNd->pNdTypeRef);
    pNd = pNd->pNdTypeRef;
    if (pNd->enmType == kXpidlNdType_Typedef)
        pNd = pNd->u.Typedef.pNodeTypeSpec;
    return pNd;
}


static PCXPIDLNODE find_named_parameter(PCXPIDLNODE pNdMethod, const char *param_name)
{
    PCXPIDLNODE pIt;
    RTListForEach(&pNdMethod->u.Method.LstParams, pIt, XPIDLNODE, NdLst)
    {
        Assert(pIt->enmType == kXpidlNdType_Parameter);
        if (!strcmp(pIt->u.Param.pszName, param_name))
            return pIt;
    }
    return NULL;
}

typedef enum ParamAttrType {
    IID_IS,
    LENGTH_IS,
    SIZE_IS
} ParamAttrType;

/*
 * Check that parameters referred to by attributes such as size_is exist and
 * refer to parameters of the appropriate type.
 */
static int check_param_attribute(PCXPIDLNODE pNdMethod, PCXPIDLNODE pNdParam,
                                 ParamAttrType whattocheck, PRTERRINFO pErrInfo)
{
    const char *attr_name;
    const char *needed_type;

    if (whattocheck == IID_IS) {
        attr_name = "iid_is";
        needed_type = "IID";
    } else if (whattocheck == LENGTH_IS) {
        attr_name = "length_is";
        needed_type = "unsigned long (or PRUint32)";
    } else if (whattocheck == SIZE_IS) {
        attr_name = "size_is";
        needed_type = "unsigned long (or PRUint32)";
    } else {
        AssertMsgFailed(("asked to check an unknown attribute type!"));
        return VINF_SUCCESS;
    }
    
    PCXPIDLATTR pAttr = xpidlNodeAttrFind(pNdParam, attr_name);
    if (pAttr != NULL)
    {
        const char *referred_name = pAttr->pszVal;

        PCXPIDLNODE pNdParamRef = find_named_parameter(pNdMethod,
                                                       referred_name);
        if (!pNdParamRef)
            return xpidlIdlError(pErrInfo, pNdParam, VERR_INVALID_STATE,
                                 "attribute [%s(%s)] refers to missing parameter \"%s\"",
                                 attr_name, referred_name, referred_name);

        if (pNdParamRef == pNdParam)
            return xpidlIdlError(pErrInfo, pNdParam, VERR_INVALID_STATE,
                                 "attribute [%s(%s)] refers to it's own parameter",
                                 attr_name, referred_name);
        
        PCXPIDLNODE pNdTypeSpec = find_underlying_type(pNdParamRef->u.Param.pNdTypeSpec);
        pNdTypeSpec = pNdTypeSpec ? pNdTypeSpec : pNdParamRef->u.Param.pNdTypeSpec;
        if (whattocheck == IID_IS)
        {
            /* require IID type */
            if (!xpidlNodeAttrFind(pNdTypeSpec, "nsid"))
                return xpidlIdlError(pErrInfo, pNdParamRef, VERR_INVALID_STATE,
                               "target \"%s\" of [%s(%s)] attribute "
                               "must be of %s type",
                               referred_name, attr_name, referred_name,
                               needed_type);
        }
        else if (whattocheck == LENGTH_IS || whattocheck == SIZE_IS)
        {
            PCXPIDLNODE pNdType = find_underlying_type(pNdTypeSpec);
            pNdType = pNdType ? pNdType : pNdTypeSpec;

            if (   pNdType->enmType != kXpidlNdType_BaseType
                || pNdType->u.enmBaseType != kXpidlType_Unsigned_Long)
                return xpidlIdlError(pErrInfo, pNdParamRef, VERR_INVALID_STATE,
                                     "target \"%s\" of [%s(%s)] attribute "
                                     "must be of %s type",
                                     referred_name, attr_name, referred_name,
                                     needed_type);
        }
    }

    return VINF_SUCCESS;
}


/*
 * Common method verification code, called by *op_dcl in the various backends.
 */
DECLHIDDEN(int) verify_method_declaration(PCXPIDLNODE pNd, PRTERRINFO pErrInfo)
{
    Assert(pNd->enmType == kXpidlNdType_Method);
    bool notxpcom;
    bool scriptable_interface;
    bool scriptable_method;
    bool seen_retval = false;

    /*
     * We don't support attributes named IID, conflicts with static GetIID 
     * member. The conflict is due to certain compilers (VC++) choosing a
     * different vtable order, placing GetIID at the beginning regardless
     * of it's placement
     */
    if (!strcmp(pNd->u.Method.pszName, "GetIID"))
        return xpidlIdlError(pErrInfo, pNd, VERR_INVALID_STATE,
                             "Methods named GetIID not supported, causes vtable "
                             "ordering problems");

    /* 
     * Verify that we've been called on an interface, and decide if the
     * interface was marked [scriptable].
     */
    if (   pNd->pParent
        && pNd->pParent->enmType == kXpidlNdType_Interface_Def)
        scriptable_interface = (xpidlNodeAttrFind(pNd->pParent, "scriptable") != NULL);
    else
        return xpidlIdlError(pErrInfo, pNd, VERR_INVALID_STATE,
                             "verify_method_declaration called on a non-interface?");

    /*
     * Require that any method in an interface marked as [scriptable], that
     * *isn't* scriptable because it refers to some native type, be marked
     * [noscript] or [notxpcom].
     *
     * Also check that iid_is points to nsid, and length_is, size_is points
     * to unsigned long.
     */
    notxpcom = xpidlNodeAttrFind(pNd, "notxpcom") != NULL;

    scriptable_method =    scriptable_interface
                        && !notxpcom
                        && xpidlNodeAttrFind(pNd, "noscript") == NULL;

    /* Loop through the parameters and check. */
    PCXPIDLNODE pIt;
    RTListForEach(&pNd->u.Method.LstParams, pIt, XPIDLNODE, NdLst)
    {
        int rc;

        PCXPIDLNODE pNdTypeSpec = find_underlying_type(pIt->u.Param.pNdTypeSpec);
        pNdTypeSpec = pNdTypeSpec ? pNdTypeSpec : pIt->u.Param.pNdTypeSpec;
        
        /*
         * Reject this method if it should be scriptable and some parameter is
         * native that isn't marked with either nsid, domstring, utf8string, 
         * cstring, astring or iid_is.
         */
        if (   scriptable_method
            && pNdTypeSpec->enmType == kXpidlNdType_Native
            && xpidlNodeAttrFind(pNdTypeSpec, "nsid") == NULL
            && xpidlNodeAttrFind(pIt, "iid_is") == NULL
            && xpidlNodeAttrFind(pNdTypeSpec, "domstring") == NULL
            && xpidlNodeAttrFind(pNdTypeSpec, "utf8string") == NULL
            && xpidlNodeAttrFind(pNdTypeSpec, "cstring") == NULL
            && xpidlNodeAttrFind(pNdTypeSpec, "astring") == NULL)
            return xpidlIdlError(pErrInfo, pIt, VERR_INVALID_STATE,
                           "methods in [scriptable] interfaces that are "
                           "non-scriptable because they refer to native "
                           "types (parameter \"%s\") must be marked "
                           "[noscript]", pIt->u.Param.pszName);

        /* 
         * nsid's parameters that aren't ptr's or ref's are not currently 
         * supported in xpcom or non-xpcom (marked with [notxpcom]) methods 
         * as input parameters
         */
        if (   !(   notxpcom
                 && pIt->u.Param.enmDir != kXpidlDirection_In)
            && xpidlNodeAttrFind(pNdTypeSpec, "nsid") != NULL
            && xpidlNodeAttrFind(pNdTypeSpec, "ptr") == NULL
            && xpidlNodeAttrFind(pNdTypeSpec, "ref") == NULL) 
            return xpidlIdlError(pErrInfo, pNd, VERR_INVALID_STATE,
                                 "Feature currently not supported: "
                                 "parameter \"%s\" is of type nsid and "
                                 "must be marked either [ptr] or [ref] "
                                 "or method \"%s\" must be marked [notxpcom] "
                                 "and must not be an input parameter",
                                 pIt->u.Param.pszName, pNd->u.Method.pszName);

        /*
         * Sanity checks on return values.
         */
        if (xpidlNodeAttrFind(pIt, "retval") != NULL)
        {
            if (!RTListNodeIsLast(&pNd->u.Method.LstParams, &pIt->NdLst))
                return xpidlIdlError(pErrInfo, pNd, VERR_INVALID_STATE,
                                     "only the last parameter can be marked [retval]");

            if (   pNd->u.Method.pNdTypeSpecRet->enmType != kXpidlNdType_BaseType
                || pNd->u.Method.pNdTypeSpecRet->u.enmBaseType != kXpidlType_Void)
                return xpidlIdlError(pErrInfo, pNd, VERR_INVALID_STATE,
                                     "can't have [retval] with non-void return type");

            /* In case XPConnect relaxes the retval-is-last restriction. */
            if (seen_retval)
                return xpidlIdlError(pErrInfo, pNd, VERR_INVALID_STATE,
                                     "can't have more than one [retval] parameter");

            seen_retval = true;
        }

        /*
         * Confirm that [shared] attributes are only used with string, wstring,
         * or native (but not nsid, domstring, utf8string, cstring or astring) 
         * and can't be used with [array].
         */
        if (xpidlNodeAttrFind(pIt, "shared") != NULL)
        {
            if (xpidlNodeAttrFind(pIt, "array") != NULL)
                return xpidlIdlError(pErrInfo, pIt, VERR_INVALID_STATE,
                                     "[shared] parameter \"%s\" cannot be of array type",
                                     pIt->u.Param.pszName);

            if (   !(   pNdTypeSpec->enmType == kXpidlNdType_BaseType
                     && (   pNdTypeSpec->u.enmBaseType == kXpidlType_String
                         || pNdTypeSpec->u.enmBaseType == kXpidlType_Wide_String))
                && !(   pNdTypeSpec->enmType == kXpidlNdType_Native
                     && (   !xpidlNodeAttrFind(pNdTypeSpec, "nsid")
                         && !xpidlNodeAttrFind(pNdTypeSpec, "domstring")
                         && !xpidlNodeAttrFind(pNdTypeSpec, "utf8string")
                         && !xpidlNodeAttrFind(pNdTypeSpec, "cstring")
                         && !xpidlNodeAttrFind(pNdTypeSpec, "astring"))))
                return xpidlIdlError(pErrInfo, pIt, VERR_INVALID_STATE,
                                     "[shared] parameter \"%s\" must be of type "
                                     "string, wstring or native", pIt->u.Param.pszName);
        }

        /*
         * inout is not allowed with "domstring", "UTF8String", "CString" 
         * and "AString" types
         */
        if (   pIt->u.Param.enmDir == kXpidlDirection_InOut
            && pNdTypeSpec->enmType == kXpidlNdType_Native
            && (   xpidlNodeAttrFind(pNdTypeSpec, "domstring")  != NULL
                || xpidlNodeAttrFind(pNdTypeSpec, "utf8string") != NULL
                || xpidlNodeAttrFind(pNdTypeSpec, "cstring")    != NULL
                || xpidlNodeAttrFind(pNdTypeSpec, "astring")    != NULL))
            return xpidlIdlError(pErrInfo, pIt, VERR_INVALID_STATE,
                                 "[domstring], [utf8string], [cstring], [astring] "
                                 "types cannot be used as inout parameters");

        /*
         * arrays of domstring, utf8string, cstring, astring types not allowed
         */
        if (   xpidlNodeAttrFind(pIt, "array")
            && pNdTypeSpec->enmType == kXpidlNdType_Native
            && (   xpidlNodeAttrFind(pNdTypeSpec, "domstring")  != NULL
                || xpidlNodeAttrFind(pNdTypeSpec, "utf8string") != NULL
                || xpidlNodeAttrFind(pNdTypeSpec, "cstring")    != NULL
                || xpidlNodeAttrFind(pNdTypeSpec, "astring")    != NULL))
            return xpidlIdlError(pErrInfo, pIt, VERR_INVALID_STATE,
                           "[domstring], [utf8string], [cstring], [astring] "
                           "types cannot be used in array parameters");

        rc = check_param_attribute(pNd, pIt, IID_IS, pErrInfo);
        if (RT_FAILURE(rc))
            return rc;
        rc = check_param_attribute(pNd, pIt, LENGTH_IS, pErrInfo);
        if (RT_FAILURE(rc))
            return rc;
        rc = check_param_attribute(pNd, pIt, SIZE_IS, pErrInfo);
        if (RT_FAILURE(rc))
            return rc;
    }

    PCXPIDLNODE pNdTypeSpec = find_underlying_type(pNd->u.Method.pNdTypeSpecRet);
    pNdTypeSpec = pNdTypeSpec ? pNdTypeSpec : pIt->u.Method.pNdTypeSpecRet;

    /* XXX q: can return type be nsid? */
    /* Native return type? */
    if (   scriptable_method
        && pNdTypeSpec->enmType == kXpidlNdType_Native
        && xpidlNodeAttrFind(pNdTypeSpec, "nsid") == NULL
        && xpidlNodeAttrFind(pNdTypeSpec, "domstring") == NULL
        && xpidlNodeAttrFind(pNdTypeSpec, "utf8string") == NULL
        && xpidlNodeAttrFind(pNdTypeSpec, "cstring") == NULL
        && xpidlNodeAttrFind(pNdTypeSpec, "astring") == NULL)
        return xpidlIdlError(pErrInfo, pIt, VERR_INVALID_STATE,
                             "methods in [scriptable] interfaces that are "
                             "non-scriptable because they return native "
                             "types must be marked [noscript]");

    /* 
     * nsid's parameters that aren't ptr's or ref's are not currently 
     * supported in xpcom
     */
    if (   !notxpcom
        && pNdTypeSpec->enmType == kXpidlNdType_Native
        && xpidlNodeAttrFind(pNdTypeSpec, "nsid") != NULL
        && xpidlNodeAttrFind(pNdTypeSpec, "ptr") == NULL
        && xpidlNodeAttrFind(pNdTypeSpec, "ref") == NULL) 
        return xpidlIdlError(pErrInfo, pIt, VERR_INVALID_STATE,
                             "Feature currently not supported: "
                             "return value is of type nsid and "
                             "must be marked either [ptr] or [ref], "
                             "or else method \"%s\" must be marked [notxpcom] ",
                             pNd->u.Method.pszName);

    return VINF_SUCCESS;
}


/*
 * Verify that a native declaration has an associated C++ expression, i.e. that
 * it's of the form native <idl-name>(<c++-name>)
 */
DECLHIDDEN(int) check_native(PCXPIDLNODE pNd, PRTERRINFO pErrInfo)
{
    Assert(pNd->enmType == kXpidlNdType_Native);

    /* require that native declarations give a native type */
    if (pNd->u.Native.pszNative) 
        return VINF_SUCCESS;

    return xpidlIdlError(pErrInfo, pNd, VERR_INVALID_STATE,
                         "``native %s;'' needs C++ type: ``native %s(<C++ type>);''",
                         pNd->u.Native.pszName, pNd->u.Native.pszName);
}


/*
 * Verify that the interface declaration is correct
 */
DECLHIDDEN(int) verify_interface_declaration(PCXPIDLNODE pNd, PRTERRINFO pErrInfo)
{
    /* 
     * If we have the scriptable attribute then make sure all of our direct
     * parents have it as well.
     * NOTE: We don't recurse since all interfaces will fall through here
     */
    if (xpidlNodeAttrFind(pNd, "scriptable"))
    {
        Assert(pNd->enmType == kXpidlNdType_Interface_Def);
        while (pNd->pNdTypeRef)
        {
            if (!xpidlNodeAttrFind(pNd->pNdTypeRef, "scriptable"))
                return xpidlIdlError(pErrInfo, pNd, VERR_INVALID_STATE,
                                     "%s is scriptable but inherits from the non-scriptable interface %s",
                                     pNd->u.If.pszIfName, pNd->u.If.pszIfInherit);

            pNd = pNd->pNdTypeRef;
        }
    }
    return VINF_SUCCESS;
}


DECLHIDDEN(PCXPIDLATTR) xpidlNodeAttrFind(PCXPIDLNODE pNd, const char *pszAttr)
{
    uint32_t i;
    for (i = 0; i < pNd->cAttrs; i++)
    {
        if (!strcmp(pNd->aAttrs[i].pszName, pszAttr))
            return &pNd->aAttrs[i];
    }

    return NULL;
}


DECLHIDDEN(int) xpidlIdlError(PRTERRINFO pErrInfo, PCXPIDLNODE pNd, int rc, const char *pszFmt, ...)
{
    RT_NOREF(pNd);

    va_list Args;
    va_start(Args, pszFmt);
    rc = RTErrInfoSetV(pErrInfo, rc, pszFmt, Args);
    va_end(Args);
    return rc;
}
