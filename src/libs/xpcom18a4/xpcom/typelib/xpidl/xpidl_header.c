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
 * Generate XPCOM headers from XPIDL.
 */
#include <iprt/assert.h>
#include <iprt/path.h>

#include "xpidl.h"
#include <ctype.h>

static void write_indent(FILE *outfile)
{
    fputs("  ", outfile);
}


static void
write_classname_iid_define(FILE *file, const char *className)
{
    const char *iidName;
    if (className[0] == 'n' && className[1] == 's') {
        /* backcompat naming styles */
        fputs("NS_", file);
        iidName = className + 2;
    } else {
        iidName = className;
    }
    while (*iidName)
        fputc(toupper(*iidName++), file);
    fputs("_IID", file);
}


static int xpidlHdrWriteIdlType(PCXPIDLNODE pNd, FILE *pFile)
{
    if (pNd->enmType == kXpidlNdType_BaseType)
    {
        switch (pNd->u.enmBaseType)
        {
            case kXpidlType_Void:
                fputs("void", pFile);
                break;
            case kXpidlType_Boolean:
                fputs("boolean", pFile);
                break;
            case kXpidlType_Octet:
                fputs("octet", pFile);
                break;
            case kXpidlType_Char:
                fputs("char", pFile);
                break;
            case kXpidlType_Wide_Char:
                fputs("wchar", pFile); /* wchar_t? */
                break;
            case kXpidlType_Short:
                fputs("short", pFile);
                break;
            case kXpidlType_Long:
                fputs("long", pFile);
                break;
            case kXpidlType_Long_Long:
                fputs("long long", pFile);
                break;
            case kXpidlType_Unsigned_Short:
                fputs("unsigned short", pFile);
                break;
            case kXpidlType_Unsigned_Long:
                fputs("unsigned long", pFile);
                break;
            case kXpidlType_Unsigned_Long_Long:
                fputs("unsigned long long", pFile);
                break;
            case kXpidlType_String:
                fputs("string", pFile);
                break;
            case kXpidlType_Wide_String:
                fputs("wstring", pFile);
                break;
            case kXpidlType_Double:
                fputs("double", pFile);
                break;
            case kXpidlType_Float:
                fputs("float", pFile);
                break;
            default:
                AssertReleaseFailed();
        }
    }
    else
    {
        Assert(pNd->enmType == kXpidlNdType_Identifier);
        fputs(pNd->u.pszIde, pFile);
    }

    return VINF_SUCCESS;
}

static int xpidlHdrWriteType(PCXPIDLNODE pNd, FILE *outfile)
{
    if (pNd->enmType == kXpidlNdType_BaseType)
    {
        switch (pNd->u.enmBaseType)
        {
            case kXpidlType_Void:
                fputs("void", outfile);
                break;
            case kXpidlType_Boolean:
                fputs("PRBool", outfile);
                break;
            case kXpidlType_Octet:
                fputs("PRUint8", outfile);
                break;
            case kXpidlType_Char:
                fputs("char", outfile);
                break;
            case kXpidlType_Wide_Char:
                fputs("PRUnichar", outfile); /* wchar_t? */
                break;
            case kXpidlType_Short:
                fputs("PRInt16", outfile);
                break;
            case kXpidlType_Long:
                fputs("PRInt32", outfile);
                break;
            case kXpidlType_Long_Long:
                fputs("PRInt64", outfile);
                break;
            case kXpidlType_Unsigned_Short:
                fputs("PRUint16", outfile);
                break;
            case kXpidlType_Unsigned_Long:
                fputs("PRUint32", outfile);
                break;
            case kXpidlType_Unsigned_Long_Long:
                fputs("PRUint64", outfile);
                break;
            case kXpidlType_String:
                fputs("char *", outfile);
                break;
            case kXpidlType_Wide_String:
                fputs("PRUnichar *", outfile);
                break;
            case kXpidlType_Double:
                fputs("double", outfile);
                break;
            case kXpidlType_Float:
                fputs("float", outfile);
                break;
            default:
                AssertReleaseFailed();
        }
    }
    else
    {
        Assert(   pNd->enmType == kXpidlNdType_Identifier
               && pNd->pNdTypeRef);

        PCXPIDLNODE pNdType = pNd->pNdTypeRef;
        if (pNdType->enmType == kXpidlNdType_Native)
        {
            if (   xpidlNodeAttrFind(pNdType, "domstring")
                || xpidlNodeAttrFind(pNdType, "astring"))
                fputs("nsAString", outfile);
            else if (xpidlNodeAttrFind(pNdType, "utf8string"))
                fputs("nsACString", outfile);
            else if (xpidlNodeAttrFind(pNdType, "cstring"))
                fputs("nsACString", outfile);
            else
                fputs(pNdType->u.Native.pszNative, outfile);

            if (xpidlNodeAttrFind(pNdType, "ptr"))
                fputs(" *", outfile);
            else if (xpidlNodeAttrFind(pNdType, "ref"))
                fputs(" &", outfile);
        }
        else
            fputs(pNd->u.pszIde, outfile);

        if (UP_IS_AGGREGATE(pNd))
            fputs(" *", outfile);
    }

    return VINF_SUCCESS;
}


/*
 * param generation:
 * in string foo        -->     nsString *foo
 * out string foo       -->     nsString **foo;
 * inout string foo     -->     nsString **foo;
 */

/* If notype is true, just write the param name. */
static int write_param(PCXPIDLNODE pNd, FILE *pFile)
{
    PCXPIDLNODE pNdTypeSpec = find_underlying_type(pNd->u.Param.pNdTypeSpec);
    bool is_in = pNd->u.Param.enmDir == kXpidlDirection_In;
    /* in string, wstring, nsid, domstring, utf8string, cstring and 
     * astring any explicitly marked [const] are const 
     */

    if (is_in &&
        (xpidlNdIsStringType(pNdTypeSpec) ||
         xpidlNodeAttrFind(pNd, "const") ||
         xpidlNodeAttrFind(pNdTypeSpec, "nsid") ||
         xpidlNodeAttrFind(pNdTypeSpec, "domstring")  ||
         xpidlNodeAttrFind(pNdTypeSpec, "utf8string") ||
         xpidlNodeAttrFind(pNdTypeSpec, "cstring")    ||
         xpidlNodeAttrFind(pNdTypeSpec, "astring"))) {
        fputs("const ", pFile);
    }
    else if (   pNd->u.Param.enmDir == kXpidlDirection_Out
             && xpidlNodeAttrFind(pNd, "shared")) {
        fputs("const ", pFile);
    }

    int rc = xpidlHdrWriteType(pNd->u.Param.pNdTypeSpec, pFile);
    AssertRC(rc);
    if (RT_FAILURE(rc))
        return rc;

    /* unless the type ended in a *, add a space */
    if (!STARRED_TYPE(pNdTypeSpec))
        fputc(' ', pFile);

    /* out and inout params get a bonus '*' (unless this is type that has a 
     * 'dipper' class that is passed in to receive 'out' data) 
     */
    if (   !is_in
        && !DIPPER_TYPE(pNdTypeSpec))
        fputc('*', pFile);

    /* arrays get a bonus * too */
    /* XXX Should this be a leading '*' or a trailing "[]" ?*/
    if (xpidlNodeAttrFind(pNd, "array"))
        fputc('*', pFile);

    fputs(pNd->u.Param.pszName, pFile);
    return VINF_SUCCESS;
}


/*
 * Shared between the interface class declaration and the NS_DECL_IFOO macro
 * provided to aid declaration of implementation classes.  
 */
static int write_method_signature(PCXPIDLNODE pNd, FILE *pFile, bool fDecl)
{
    bool no_generated_args = true;
    bool op_notxpcom = (xpidlNodeAttrFind(pNd, "notxpcom") != NULL);

    if (fDecl)
    {
        if (op_notxpcom) {
            fputs("NS_IMETHOD_(", pFile);
            int rc = xpidlHdrWriteType(pNd->u.Method.pNdTypeSpecRet, pFile);
            if (RT_FAILURE(rc))
                return rc;
            fputc(')', pFile);
        } else {
            fputs("NS_IMETHOD", pFile);
        }
        fputc(' ', pFile);
    }

    const char *pszName = pNd->u.Method.pszName;
    fprintf(pFile, "%c%s(", toupper(*pszName), pszName + 1);

    PCXPIDLNODE pIt;
    RTListForEach(&pNd->u.Method.LstParams, pIt, XPIDLNODE, NdLst)
    {
        if (fDecl)
        {
            int rc = write_param(pIt, pFile);
            if (RT_FAILURE(rc))
                return rc;
        }
        else
            fputs(pIt->u.Param.pszName, pFile);

        if (!RTListNodeIsLast(&pNd->u.Method.LstParams, &pIt->NdLst))
            fputs(", ", pFile);
        no_generated_args = false;
    }

    /* make IDL return value into trailing out argument */
    if (   pNd->u.Method.pNdTypeSpecRet
        && (   pNd->u.Method.pNdTypeSpecRet->enmType != kXpidlNdType_BaseType
            || pNd->u.Method.pNdTypeSpecRet->u.enmBaseType != kXpidlType_Void)
        && !op_notxpcom)
    {
        if (!no_generated_args)
            fputs(", ", pFile);
        XPIDLNODE Nd;
        Nd.enmType = kXpidlNdType_Parameter;
        Nd.u.Param.pszName = "_retval";
        Nd.u.Param.enmDir = kXpidlDirection_Out;
        Nd.u.Param.pNdTypeSpec = pNd->u.Method.pNdTypeSpecRet;
        Nd.cAttrs = 0;
        int rc = write_param(&Nd, pFile);
        if (RT_FAILURE(rc))
        {
            AssertFailed();
            return VERR_INVALID_PARAMETER;
        }

#if 0
        if (op->f_varargs)
            fputs(", ", pFile);
#endif
        no_generated_args = false;
    }

#if 0 /** @todo No varargs allowed. */
    /* varargs go last */
    if (op->f_varargs) {
        if (mode == AS_DECL || mode == AS_IMPL) {
            fputs("nsVarArgs *", pFile);
        }
        fputs("_varargs", pFile);
        no_generated_args = FALSE;
    }
#endif

    /*
     * If generated method has no arguments, output 'void' to avoid C legacy
     * behavior of disabling type checking.
     */
    if (no_generated_args && fDecl)
        fputs("void", pFile);

    fputc(')', pFile);
    return VINF_SUCCESS;
}


static void xpidlHdrWriteIdlAttrs(PCXPIDLNODE pIt, FILE *pFile)
{
    if (pIt->cAttrs)
    {
        fputs("[", pFile);
        uint32_t i;
        for (i = 0; i < pIt->cAttrs; i++)
        {
            if (pIt->aAttrs[i].pszVal)
                fprintf(pFile, "%s (%s)", pIt->aAttrs[i].pszName, pIt->aAttrs[i].pszVal);
            else
                fprintf(pFile, "%s", pIt->aAttrs[i].pszName);

            if (i < pIt->cAttrs - 1)
                fputs(", ", pFile);
        }
        fputs("] ", pFile);
    }
}


static int xpidlHdrWriteMethod(PCXPIDLNODE pNd, FILE *pFile, PRTERRINFO pErrInfo)
{
    /*
     * Verify that e.g. non-scriptable methods in [scriptable] interfaces
     * are declared so.  Do this in a separate verification pass?
     */
    int rc = verify_method_declaration(pNd, pErrInfo);
    if (RT_FAILURE(rc))
        return rc;

    /* Dump the signature as a comment. */
    write_indent(pFile);
    fputs("/* ", pFile);
    xpidlHdrWriteIdlAttrs(pNd, pFile);
    xpidlHdrWriteIdlType(pNd->u.Method.pNdTypeSpecRet, pFile);
    fprintf(pFile, " %s (", pNd->u.Method.pszName);
    PCXPIDLNODE pIt;
    RTListForEach(&pNd->u.Method.LstParams, pIt, XPIDLNODE, NdLst)
    {
        xpidlHdrWriteIdlAttrs(pIt, pFile);
        if (pIt->u.Param.enmDir == kXpidlDirection_In)
            fputs("in ", pFile);
        else if (pIt->u.Param.enmDir == kXpidlDirection_Out)
            fputs("out ", pFile);
        else
            fputs("inout ", pFile);

        xpidlHdrWriteIdlType(pIt->u.Param.pNdTypeSpec, pFile);
        fprintf(pFile, " %s", pIt->u.Param.pszName);

        if (!RTListNodeIsLast(&pNd->u.Method.LstParams, &pIt->NdLst))
            fputs(", ", pFile);
    }
    fputs("); */\n", pFile);

    write_indent(pFile);
    rc = write_method_signature(pNd, pFile, true /*fDecl*/);
    if (RT_FAILURE(rc))
        return rc;
    fputs(" = 0;\n\n", pFile);

    return VINF_SUCCESS;
}


static int xpidlHdrWriteAttrAccessor(PCXPIDLNODE pNd, FILE *pFile, bool getter, bool fDecl)
{
    const char *pszName = pNd->u.Attribute.pszName;

    if (fDecl)
        fputs("NS_IMETHOD ", pFile);
    fprintf(pFile, "%cet%c%s(",
            getter ? 'G' : 'S',
            toupper(*pszName), pszName + 1);
    if (fDecl)
    {
        /* Setters for string, wstring, nsid, domstring, utf8string, 
         * cstring and astring get const. 
         */
        PCXPIDLNODE pNdTypeSpec = find_underlying_type(pNd->u.Attribute.pNdTypeSpec);

        if (!getter &&
            (xpidlNdIsStringType(pNdTypeSpec) ||
             xpidlNodeAttrFind(pNdTypeSpec, "nsid") ||
             xpidlNodeAttrFind(pNdTypeSpec, "domstring")  ||
             xpidlNodeAttrFind(pNdTypeSpec, "utf8string") ||
             xpidlNodeAttrFind(pNdTypeSpec, "cstring")    ||
             xpidlNodeAttrFind(pNdTypeSpec, "astring")))
            fputs("const ", pFile);

        int rc = xpidlHdrWriteType(pNd->u.Attribute.pNdTypeSpec, pFile);
        if (RT_FAILURE(rc))
            return rc;
        fprintf(pFile, "%s%s",
                (STARRED_TYPE(pNdTypeSpec) ? "" : " "),
                (getter && !DIPPER_TYPE(pNdTypeSpec) ? "*" : ""));
    }
    fprintf(pFile, "a%c%s)", toupper(pszName[0]), pszName + 1);
    return VINF_SUCCESS;
}

static int xpidlHdrWriteAttribute(PCXPIDLNODE pNd, FILE *pFile, PRTERRINFO pErrInfo)
{
    int rc = verify_attribute_declaration(pNd, pErrInfo);
    if (RT_FAILURE(rc))
        return rc;

    /* Write the attribute as a comment. */
    write_indent(pFile);
    if (pNd->u.Attribute.fReadonly)
        fputs("/* readonly attribute ", pFile);
    else
        fputs("/* attribute ", pFile);
    xpidlHdrWriteIdlType(pNd->u.Attribute.pNdTypeSpec, pFile);
    fprintf(pFile, " %s; */\n", pNd->u.Attribute.pszName);

    write_indent(pFile);
    rc = xpidlHdrWriteAttrAccessor(pNd, pFile, true, true /*fDecl*/);
    if (RT_FAILURE(rc))
        return rc;
    fputs(" = 0;\n", pFile);

    if (!pNd->u.Attribute.fReadonly) {
        write_indent(pFile);
        rc = xpidlHdrWriteAttrAccessor(pNd, pFile, false, true /*fDecl*/);
        if (RT_FAILURE(rc))
            return rc;
        fputs(" = 0;\n", pFile);
    }
    fputc('\n', pFile);

    return VINF_SUCCESS;
}


static int xpidlHdrWriteConst(PCXPIDLNODE pNd, FILE *pFile, PRTERRINFO pErrInfo)
{
    int rc = verify_const_declaration(pNd, pErrInfo);
    if (RT_FAILURE(rc))
        return rc;

    write_indent(pFile);
    fprintf(pFile, "enum { %s = ", pNd->u.Const.pszName);
#ifdef RT_OS_DARWIN
    fprintf(pFile, "%lluU", pNd->u.Const.u64Const);
#else
    fprintf(pFile, "%luU", pNd->u.Const.u64Const);
#endif
    fprintf(pFile, " };\n\n");

    return VINF_SUCCESS;
}


static int xpidlHdrWriteInterface(PCXPIDLNODE pNd, FILE *pFile, PRTERRINFO pErrInfo)
{
    char *classNameUpper = NULL;
    char *cp;
    struct nsID id;
    char iid_parsed[UUID_LENGTH];

    int rc = verify_interface_declaration(pNd, pErrInfo);
    if (RT_FAILURE(rc))
        return rc;

#define FAIL    do {/*AssertFailed();*/ goto out;} while(0)

    fprintf(pFile, "\n/* starting interface:    %s */\n", pNd->u.If.pszIfName);

    AssertRelease(!xpidlNodeAttrFind(pNd, "namespace")); /* Not supported right now. */

    PCXPIDLATTR pAttrIid = xpidlNodeAttrFind(pNd, "uuid");
    if (pAttrIid)
    {
        AssertPtr(pAttrIid->pszVal);

        /* Redundant, but a better error than 'cannot parse.' */
        if (strlen(pAttrIid->pszVal) != 36)
        {
            rc = xpidlIdlError(pErrInfo, pNd, VERR_INVALID_STATE,
                               "IID %s is the wrong length", pAttrIid->pszVal);
            FAIL;
        }

        /*
         * Parse uuid and then output resulting nsID to string, to validate
         * uuid and normalize resulting .h files.
         */
        if (!xpidl_parse_iid(&id, pAttrIid->pszVal))
        {
            rc = xpidlIdlError(pErrInfo, pNd, VERR_INVALID_STATE,
                               "cannot parse IID %s\n", pAttrIid->pszVal);
            FAIL;
        }
        if (!xpidl_sprint_iid(&id, iid_parsed))
        {
            rc = xpidlIdlError(pErrInfo, pNd, VERR_INVALID_STATE,
                               "error formatting IID %s\n", pAttrIid->pszVal);
            FAIL;
        }

        /* #define NS_ISUPPORTS_IID_STR "00000000-0000-0000-c000-000000000046" */
        fputs("#define ", pFile);
        write_classname_iid_define(pFile, pNd->u.If.pszIfName);
        fprintf(pFile, "_STR \"%s\"\n", iid_parsed);
        fputc('\n', pFile);

        /* #define NS_ISUPPORTS_IID { {0x00000000 .... 0x46 }} */
        fprintf(pFile, "#define ");
        write_classname_iid_define(pFile, pNd->u.If.pszIfName);
        fprintf(pFile, " \\\n"
                "  {0x%.8x, 0x%.4x, 0x%.4x, \\\n"
                "    { 0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x, "
                "0x%.2x, 0x%.2x, 0x%.2x, 0x%.2x }}\n",
                id.m0, id.m1, id.m2,
                id.m3[0], id.m3[1], id.m3[2], id.m3[3],
                id.m3[4], id.m3[5], id.m3[6], id.m3[7]);
        fputc('\n', pFile);
    }
    else
    {
        rc = xpidlIdlError(pErrInfo, pNd, VERR_INVALID_STATE,
                           "interface %s lacks a uuid attribute\n", pNd->u.If.pszIfName);
        FAIL;
    }

    /*
     * NS_NO_VTABLE is defined in nsISupportsUtils.h, and defined on windows
     * to __declspec(novtable) on windows.  This optimization is safe
     * whenever the constructor calls no virtual methods.  Writing in IDL
     * almost guarantees this, except for the case when a %{C++ block occurs in
     * the interface.  We detect that case, and emit a macro call that disables
     * the optimization.
     */
    bool keepvtable = false;
    PCXPIDLNODE pIt;
    RTListForEach(&pNd->u.If.LstBody, pIt, XPIDLNODE, NdLst)
    {
        if (pIt->enmType == kXpidlNdType_RawBlock)
        {
            keepvtable = true;
            break;
        }
    }


    /* The interface declaration itself. */
    fprintf(pFile,
            "class %s%s",
            (keepvtable ? "" : "NS_NO_VTABLE "), pNd->u.If.pszIfName);
    
    if (pNd->u.If.pszIfInherit) {
        fputs(" : ", pFile);
        fprintf(pFile, "public %s", pNd->u.If.pszIfInherit);
    }
    fputs(" {\n"
          " public: \n\n", pFile);

    fputs("  NS_DEFINE_STATIC_IID_ACCESSOR(", pFile);
    write_classname_iid_define(pFile, pNd->u.If.pszIfName);
    fputs(")\n\n", pFile);

    RTListForEach(&pNd->u.If.LstBody, pIt, XPIDLNODE, NdLst)
    {
        switch (pIt->enmType)
        {
            case kXpidlNdType_Const:
                rc = xpidlHdrWriteConst(pIt, pFile, pErrInfo);
                if (RT_FAILURE(rc))
                    FAIL;
                break;
            case kXpidlNdType_Attribute:
                rc = xpidlHdrWriteAttribute(pIt, pFile, pErrInfo);
                if (RT_FAILURE(rc))
                    FAIL;
                break;
            case kXpidlNdType_Method:
                rc = xpidlHdrWriteMethod(pIt, pFile, pErrInfo);
                if (RT_FAILURE(rc))
                    FAIL;
                break;
            case kXpidlNdType_RawBlock:
                fprintf(pFile, "%.*s", (int)pIt->u.RawBlock.cchRaw, pIt->u.RawBlock.pszRaw);
                break;
            default:
                rc = VERR_INVALID_PARAMETER;
                FAIL;
        }
    }

    fputs("};\n", pFile);
    fputc('\n', pFile);

    /*
     * #define NS_DECL_NSIFOO - create method prototypes that can be used in
     * class definitions that support this interface.
     *
     * Walk the tree explicitly to prototype a reworking of xpidl to get rid of
     * the callback mechanism.
     */
    fputs("/* Use this macro when declaring classes that implement this "
          "interface. */\n", pFile);
    fputs("#define NS_DECL_", pFile);
    classNameUpper = xpidl_strdup(pNd->u.If.pszIfName);
    if (!classNameUpper)
        FAIL;

    for (cp = classNameUpper; *cp != '\0'; cp++)
        *cp = toupper(*cp);
    fprintf(pFile, "%s \\\n", classNameUpper);
    if (RTListIsEmpty(&pNd->u.If.LstBody))
    {
        write_indent(pFile);
        fputs("/* no methods! */\n", pFile);
    }

    RTListForEach(&pNd->u.If.LstBody, pIt, XPIDLNODE, NdLst)
    {
        switch (pIt->enmType)
        {
            case kXpidlNdType_Const:
            case kXpidlNdType_RawBlock:
                /* ignore it here; it doesn't contribute to the macro. */
                continue;
            case kXpidlNdType_Attribute:
            {
                write_indent(pFile);
                rc = xpidlHdrWriteAttrAccessor(pIt, pFile, true, true /*fDecl*/);
                if (RT_FAILURE(rc))
                    FAIL;
                if (!pIt->u.Attribute.fReadonly)
                {
                    fputs(" NS_OVERRIDE; \\\n", pFile); /* Terminate the previous one. */
                    write_indent(pFile);
                    rc = xpidlHdrWriteAttrAccessor(pIt, pFile, false, true /*fDecl*/);
                    if (RT_FAILURE(rc))
                        FAIL;
                    /* '; \n' at end will clean up. */
                }
                break;
            }
            case kXpidlNdType_Method:
                write_indent(pFile);
                rc = write_method_signature(pIt, pFile, true /*fDecl*/);
                if (RT_FAILURE(rc))
                    FAIL;
                break;
            default:
                rc = VERR_INVALID_PARAMETER;
                FAIL;
        }

        if (!RTListNodeIsLast(&pNd->u.If.LstBody, &pIt->NdLst))
            fprintf(pFile, " NS_OVERRIDE; \\\n");
        else
            fprintf(pFile, " NS_OVERRIDE; \n");
    }
    fputc('\n', pFile);

    /* XXX abstract above and below into one function? */
    /*
     * #define NS_FORWARD_NSIFOO - create forwarding methods that can delegate
     * behavior from in implementation to another object.  As generated by
     * idlc.
     */
    fprintf(pFile,
            "/* Use this macro to declare functions that forward the "
            "behavior of this interface to another object. */\n"
            "#define NS_FORWARD_%s(_to) \\\n",
            classNameUpper);
    if (RTListIsEmpty(&pNd->u.If.LstBody))
    {
        write_indent(pFile);
        fputs("/* no methods! */\n", pFile);
    }

    RTListForEach(&pNd->u.If.LstBody, pIt, XPIDLNODE, NdLst)
    {
        switch (pIt->enmType)
        {
            case kXpidlNdType_Const:
            case kXpidlNdType_RawBlock:
                continue;
            case kXpidlNdType_Attribute:
            {
                write_indent(pFile);
                rc = xpidlHdrWriteAttrAccessor(pIt, pFile, true, true /*fDecl*/);
                if (RT_FAILURE(rc))
                    FAIL;
                fputs(" { return _to ", pFile);
                rc = xpidlHdrWriteAttrAccessor(pIt, pFile, true, false /*fDecl*/);
                if (RT_FAILURE(rc))
                    FAIL;
                if (!pIt->u.Attribute.fReadonly)
                {
                    fputs("; } \\\n", pFile); /* Terminate the previous one. */
                    write_indent(pFile);
                    rc = xpidlHdrWriteAttrAccessor(pIt, pFile, false, true /*fDecl*/);
                    if (RT_FAILURE(rc))
                        FAIL;
                    fputs(" { return _to ", pFile);
                    rc = xpidlHdrWriteAttrAccessor(pIt, pFile, false, false /*fDecl*/);
                    if (RT_FAILURE(rc))
                        FAIL;
                    /* '; } \n' at end will clean up. */
                }
                break;
            }
            case kXpidlNdType_Method:
                write_indent(pFile);
                rc = write_method_signature(pIt, pFile, true /*fDecl*/);
                if (RT_FAILURE(rc))
                    FAIL;
                fputs(" { return _to ", pFile);
                rc = write_method_signature(pIt, pFile, false /*fDecl*/);
                if (RT_FAILURE(rc))
                    FAIL;
                break;
            default:
                rc = VERR_INVALID_PARAMETER;
                FAIL;
        }

        if (!RTListNodeIsLast(&pNd->u.If.LstBody, &pIt->NdLst))
            fprintf(pFile, "; } \\\n");
        else
            fprintf(pFile, "; } \n");
    }
    fputc('\n', pFile);


    /* XXX abstract above and below into one function? */
    /*
     * #define NS_FORWARD_SAFE_NSIFOO - create forwarding methods that can delegate
     * behavior from in implementation to another object.  As generated by
     * idlc.
     */
    fprintf(pFile,
            "/* Use this macro to declare functions that forward the "
            "behavior of this interface to another object in a safe way. */\n"
            "#define NS_FORWARD_SAFE_%s(_to) \\\n",
            classNameUpper);
    if (RTListIsEmpty(&pNd->u.If.LstBody))
    {
        write_indent(pFile);
        fputs("/* no methods! */\n", pFile);
    }

    RTListForEach(&pNd->u.If.LstBody, pIt, XPIDLNODE, NdLst)
    {
        switch (pIt->enmType)
        {
            case kXpidlNdType_Const:
            case kXpidlNdType_RawBlock:
                continue;
            case kXpidlNdType_Attribute:
            {
                write_indent(pFile);
                rc = xpidlHdrWriteAttrAccessor(pIt, pFile, true, true /*fDecl*/);
                if (RT_FAILURE(rc))
                    FAIL;
                fputs(" { return !_to ? NS_ERROR_NULL_POINTER : _to->", pFile);
                rc = xpidlHdrWriteAttrAccessor(pIt, pFile, true, false /*fDecl*/);
                if (RT_FAILURE(rc))
                    FAIL;
                if (!pIt->u.Attribute.fReadonly)
                {
                    fputs("; } \\\n", pFile); /* Terminate the previous one. */
                    write_indent(pFile);
                    rc = xpidlHdrWriteAttrAccessor(pIt, pFile, false, true /*fDecl*/);
                    if (RT_FAILURE(rc))
                        FAIL;
                    fputs(" { return !_to ? NS_ERROR_NULL_POINTER : _to->", pFile);
                    rc = xpidlHdrWriteAttrAccessor(pIt, pFile, false, false /*fDecl*/);
                    if (RT_FAILURE(rc))
                        FAIL;
                    /* '; } \n' at end will clean up. */
                }
                break;
            }
            case kXpidlNdType_Method:
                write_indent(pFile);
                rc = write_method_signature(pIt, pFile, true /*fDecl*/);
                if (RT_FAILURE(rc))
                    FAIL;
                fputs(" { return !_to ? NS_ERROR_NULL_POINTER : _to->", pFile);
                rc = write_method_signature(pIt, pFile, false /*fDecl*/);
                if (RT_FAILURE(rc))
                    FAIL;
                break;
            default:
                rc = VERR_INVALID_PARAMETER;
                FAIL;
        }

        if (!RTListNodeIsLast(&pNd->u.If.LstBody, &pIt->NdLst))
            fprintf(pFile, "; } \\\n");
        else
            fprintf(pFile, "; } \n");
    }
    fputc('\n', pFile);

#undef FAIL

out:
    if (classNameUpper)
        free(classNameUpper);
    return rc;
}


DECL_HIDDEN_CALLBACK(int) xpidl_header_dispatch(FILE *pFile, PCXPIDLINPUT pInput, PCXPIDLPARSE pParse, PRTERRINFO pErrInfo)
{
    char *define = RTPathFilename(pInput->pszBasename);
    fprintf(pFile, "/*\n * DO NOT EDIT.  THIS FILE IS GENERATED FROM"
            " %s.idl\n */\n", pInput->pszBasename);
    fprintf(pFile,
            "\n#ifndef __gen_%s_h__\n"
            "#define __gen_%s_h__\n",
            define, define);

    if (!RTListIsEmpty(&pInput->LstIncludes))
    {
        fputc('\n', pFile);
        PCXPIDLINPUT pIt;
        RTListForEach(&pInput->LstIncludes, pIt, XPIDLINPUT, NdInclude)
        {
            char *dot = strrchr(pIt->pszBasename, '.');
            if (dot != NULL)
                *dot = '\0';
            

            /* begin include guard */            
            fprintf(pFile,
                    "\n#ifndef __gen_%s_h__\n",
                     pIt->pszBasename);

            fprintf(pFile, "#include \"%s.h\"\n", pIt->pszBasename);

            fprintf(pFile, "#endif\n");
        }

        fputc('\n', pFile);
    }
    /*
     * Support IDL files that don't include a root IDL file that defines
     * NS_NO_VTABLE.
     */
    fprintf(pFile,
            "/* For IDL files that don't want to include root IDL files. */\n"
            "#ifndef NS_NO_VTABLE\n"
            "#define NS_NO_VTABLE\n"
            "#endif\n");
    
    PCXPIDLNODE pNd;
    RTListForEach(&pParse->LstNodes, pNd, XPIDLNODE, NdLst)
    {
        /* Only output nodes from the first level input and not for any includes. */
        if (pNd->pInput != pInput)
            continue;

        int rc = VINF_SUCCESS;
        switch (pNd->enmType)
        {
            case kXpidlNdType_RawBlock:
            {
                fprintf(pFile, "%.*s", (int)pNd->u.RawBlock.cchRaw, pNd->u.RawBlock.pszRaw);
                break;
            }
            case kXpidlNdType_Interface_Forward_Decl:
            {
                fprintf(pFile, "class %s; /* forward declaration */\n\n", pNd->u.pszIfFwdName);
                break;
            }
            case kXpidlNdType_Interface_Def:
            {
                rc = xpidlHdrWriteInterface(pNd, pFile, pErrInfo);
                break;
            }
            case kXpidlNdType_Typedef:
            {
                fprintf(pFile, "typedef ");
                rc = xpidlHdrWriteType(pNd->u.Typedef.pNodeTypeSpec, pFile);
                if (RT_SUCCESS(rc))
                    fprintf(pFile, " %s;\n\n", pNd->u.Typedef.pszName);
                break;
            }
            default: /* Ignore */
                break;
        }
        if (RT_FAILURE(rc))
            return rc;
    }

    fprintf(pFile, "\n#endif /* __gen_%s_h__ */\n", define);
    return VINF_SUCCESS;
}
