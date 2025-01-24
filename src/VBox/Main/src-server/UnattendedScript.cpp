/* $Id: UnattendedScript.cpp 108048 2025-01-24 08:38:17Z alexander.eichner@oracle.com $ */
/** @file
 * Classes for reading/parsing/saving scripts for unattended installation.
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_MAIN_UNATTENDED
#include "LoggingNew.h"
#include "VirtualBoxBase.h"
#include "AutoCaller.h"
#include <VBox/com/ErrorInfo.h>

#include "UnattendedScript.h"
#include "UnattendedImpl.h"

#include <iprt/err.h>
#include <iprt/md5.h>
#include <iprt/sha.h>

#include <iprt/ctype.h>
#include <iprt/file.h>
#include <iprt/rand.h> /* For RTCrShaCrypt salt generation. */
#include <iprt/vfs.h>
#include <iprt/getopt.h>
#include <iprt/path.h>

#include <iprt/crypto/shacrypt.h>

using namespace std;

#ifdef VBOX_WITH_UNATTENDED


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
static const char g_szPrefix[]           = "@@VBOX_";
static const char g_szPrefixInsert[]     = "@@VBOX_INSERT";
static const char g_szPrefixInsertXxx[]  = "@@VBOX_INSERT_";
static const char g_szPrefixInsertExpr[] = "@@VBOX_INSERT[";
static const char g_szPrefixCond[]       = "@@VBOX_COND";
static const char g_szPrefixCondXxx[]    = "@@VBOX_COND_";
static const char g_szPrefixCondExpr[]   = "@@VBOX_COND[";
static const char g_szPrefixCondElse[]   = "@@VBOX_COND_ELSE@@";
static const char g_szPrefixCondEnd[]    = "@@VBOX_COND_END@@";
static const char g_szPrefixSplitter[]   = "@@VBOX_SPLITTER";


/*********************************************************************************************************************************
*   UnattendedScriptTemplate Implementation                                                                                      *
*********************************************************************************************************************************/

UnattendedScriptTemplate::UnattendedScriptTemplate(Unattended *pUnattended, const char *pszDefaultTemplateFilename,
                                                   const char *pszDefaultFilename)
    : BaseTextScript(pUnattended, pszDefaultTemplateFilename, pszDefaultFilename), mpUnattended(pUnattended)
{
}

HRESULT UnattendedScriptTemplate::saveToString(Utf8Str &rStrDst)
{
    RTEXPREVAL hEvaluator = NIL_RTEXPREVAL;
    int vrc = RTExprEvalCreate(&hEvaluator, 0, "unattended", this, UnattendedScriptTemplate::queryVariableForExpr);
    AssertRCReturn(vrc, mpSetError->setErrorVrc(vrc));

    struct
    {
        bool    fSavedOutputting;
    }           aConds[8];
    unsigned    cConds      = 0;
    bool        fOutputting = true;
    HRESULT     hrc         = E_FAIL;
    size_t      offTemplate = 0;
    size_t      cchTemplate = mStrScriptFullContent.length();
    rStrDst.setNull();
    for (;;)
    {
        /*
         * Find the next placeholder and add any text before it to the output.
         */
        size_t offPlaceholder = mStrScriptFullContent.find(g_szPrefix, offTemplate);
        size_t cchToCopy = offPlaceholder != RTCString::npos ? offPlaceholder - offTemplate : cchTemplate - offTemplate;
        if (cchToCopy > 0)
        {
            if (fOutputting)
            {
                try
                {
                    rStrDst.append(mStrScriptFullContent, offTemplate , cchToCopy);
                }
                catch (std::bad_alloc &)
                {
                    hrc = E_OUTOFMEMORY;
                    break;
                }
            }
            offTemplate += cchToCopy;
        }

        /*
         * Process placeholder.
         */
        if (offPlaceholder != RTCString::npos)
        {
            /*
             * First we must find the end of the placeholder string.
             */
            size_t const cchMaxPlaceholder = RT_MIN(cchTemplate - offPlaceholder, _1K);
            const char  *pszPlaceholder    = mStrScriptFullContent.c_str() + offPlaceholder;
            size_t       cchPlaceholder    = sizeof(g_szPrefix) - 1;
            char         ch;
            while (   cchPlaceholder < cchMaxPlaceholder
                   && (ch = pszPlaceholder[cchPlaceholder]) != '\0'
                   && (RT_C_IS_PRINT(ch) || RT_C_IS_SPACE(ch))
                   && ch != '@')
                cchPlaceholder++;

            if (   offPlaceholder + cchPlaceholder < cchTemplate
                && pszPlaceholder[cchPlaceholder] == '@')
            {
                cchPlaceholder++;
                if (   offPlaceholder + cchPlaceholder < cchTemplate
                    && pszPlaceholder[cchPlaceholder] == '@')
                    cchPlaceholder++;
            }

            if (   pszPlaceholder[cchPlaceholder - 1] != '@'
                || pszPlaceholder[cchPlaceholder - 2] != '@'
                || (   strncmp(pszPlaceholder, RT_STR_TUPLE(g_szPrefixInsert))   != 0
                    && strncmp(pszPlaceholder, RT_STR_TUPLE(g_szPrefixCond))     != 0
                    && strncmp(pszPlaceholder, RT_STR_TUPLE(g_szPrefixSplitter)) != 0 ) )
            {
                hrc = mpSetError->setError(E_FAIL, tr("Malformed or too long template placeholder '%.*s'"),
                                           cchPlaceholder, pszPlaceholder);
                break;
            }

            offTemplate += cchPlaceholder;

            /*
             * @@VBOX_INSERT_XXX@@:
             */
            if (strncmp(pszPlaceholder, RT_STR_TUPLE(g_szPrefixInsertXxx)) == 0)
            {
                /*
                 * Get the placeholder value and add it to the output.
                 */
                RTCString strValue;
                hrc = getReplacement(pszPlaceholder, cchPlaceholder, fOutputting, strValue);
                if (SUCCEEDED(hrc))
                {
                    if (fOutputting)
                    {
                        try
                        {
                            rStrDst.append(strValue);
                        }
                        catch (std::bad_alloc &)
                        {
                            hrc = E_OUTOFMEMORY;
                            break;
                        }
                    }
                }
                else
                    break;
            }
            /*
             * @@VBOX_INSERT[expr]@@:
             * @@VBOX_INSERT[expr]SH@@:
             * @@VBOX_INSERT[expr]ELEMENT@@:
             * @@VBOX_INSERT[expr]ATTRIB_DQ@@:
             */
            else if (strncmp(pszPlaceholder, RT_STR_TUPLE(g_szPrefixInsertExpr)) == 0)
            {
                /*
                 * Get the placeholder value and add it to the output.
                 */
                char *pszValue = NULL;
                hrc = getReplacementForExpr(hEvaluator, pszPlaceholder, cchPlaceholder, fOutputting, &pszValue);
                if (SUCCEEDED(hrc))
                {
                    if (fOutputting && pszValue)
                    {
                        try
                        {
                            rStrDst.append(pszValue);
                        }
                        catch (std::bad_alloc &)
                        {
                            hrc = E_OUTOFMEMORY;
                            break;
                        }
                    }
                    RTStrFree(pszValue);
                }
                else
                    break;
            }
            /*
             * @@VBOX_COND_END@@: Pop one item of the conditional stack.
             */
            else if (strncmp(pszPlaceholder, RT_STR_TUPLE(g_szPrefixCondEnd)) == 0)
            {
                if (cConds > 0)
                {
                    cConds--;
                    fOutputting = aConds[cConds].fSavedOutputting;
                }
                else
                {
                    hrc = mpSetError->setErrorBoth(E_FAIL, VERR_PARSE_ERROR,
                                                   tr("%s without @@VBOX_COND_XXX@@ at offset %zu (%#zx)"),
                                                   g_szPrefixCondEnd, offPlaceholder, offPlaceholder);
                    break;
                }
            }
            /*
             * @@VBOX_COND_ELSE@@: Flip the output setting of the current condition.
             */
            else if (strncmp(pszPlaceholder, RT_STR_TUPLE(g_szPrefixCondElse)) == 0)
            {
                if (cConds > 0)
                    fOutputting = !fOutputting;
                else
                {
                    hrc = mpSetError->setErrorBoth(E_FAIL, VERR_PARSE_ERROR,
                                                   tr("%s without @@VBOX_COND_XXX@@ at offset %zu (%#zx)"),
                                                   g_szPrefixCondElse, offPlaceholder, offPlaceholder);
                    break;
                }
            }
            /*
             * @@VBOX_COND_XXX@@: Push the previous outputting state and combine it with the
             *                    one from the condition.
             */
            else if (strncmp(pszPlaceholder, RT_STR_TUPLE(g_szPrefixCondXxx)) == 0)
            {
                if (cConds + 1 < RT_ELEMENTS(aConds))
                {
                    aConds[cConds].fSavedOutputting = fOutputting;
                    bool fNewOutputting = fOutputting;
                    hrc = getConditional(pszPlaceholder, cchPlaceholder, &fNewOutputting);
                    if (SUCCEEDED(hrc))
                        fOutputting = fOutputting && fNewOutputting;
                    else
                        break;
                    cConds++;
                }
                else
                {
                    hrc = mpSetError->setErrorBoth(E_FAIL, VERR_PARSE_ERROR,
                                                   tr("Too deep conditional nesting at offset %zu (%#zx)"),
                                                   offPlaceholder, offPlaceholder);
                    break;
                }
            }
            /*
             * @@VBOX_COND[expr]@@: Push the previous outputting state and combine it with the
             *                      one from the condition.
             */
            else if (strncmp(pszPlaceholder, RT_STR_TUPLE(g_szPrefixCondExpr)) == 0)
            {
                if (cConds + 1 < RT_ELEMENTS(aConds))
                {
                    aConds[cConds].fSavedOutputting = fOutputting;
                    bool fNewOutputting = fOutputting;
                    hrc = resolveConditionalExpr(hEvaluator, pszPlaceholder, cchPlaceholder, &fNewOutputting);
                    if (SUCCEEDED(hrc))
                        fOutputting = fOutputting && fNewOutputting;
                    else
                        break;
                    cConds++;
                }
                else
                {
                    hrc = mpSetError->setErrorBoth(E_FAIL, VERR_PARSE_ERROR,
                                                   tr("Too deep conditional nesting at offset %zu (%#zx)"),
                                                   offPlaceholder, offPlaceholder);
                    break;
                }
            }
            /*
             * @@VBOX_SPLITTER_START/END[filename]@@: Ignored in this pass.
             */
            else
            {
                Assert(strncmp(pszPlaceholder, RT_STR_TUPLE(g_szPrefixSplitter)) == 0);
                if (fOutputting)
                {
                    try
                    {
                        rStrDst.append(pszPlaceholder, cchPlaceholder);
                    }
                    catch (std::bad_alloc &)
                    {
                        hrc = E_OUTOFMEMORY;
                        break;
                    }
                }
            }
        }

        /*
         * Done?
         */
        if (offTemplate >= cchTemplate)
        {
            if (cConds == 0)
            {
                RTExprEvalRelease(hEvaluator);
                return S_OK;
            }
            if (cConds == 1)
                hrc = mpSetError->setErrorBoth(E_FAIL, VERR_PARSE_ERROR, tr("Missing @@VBOX_COND_END@@"));
            else
                hrc = mpSetError->setErrorBoth(E_FAIL, VERR_PARSE_ERROR, tr("Missing %u @@VBOX_COND_END@@"), cConds);
            break;
        }
    }

    /* failed */
    rStrDst.setNull();
    RTExprEvalRelease(hEvaluator);
    return hrc;
}

HRESULT UnattendedScriptTemplate::getReplacement(const char *pachPlaceholder, size_t cchPlaceholder,
                                                 bool fOutputting, RTCString &rValue)
{
    /*
     * Check for an escaping suffix.  Drop the '@@'.
     */
    kEvalEscaping_T enmEscaping;
#define PLACEHOLDER_ENDS_WITH(a_szSuffix) \
        (   cchPlaceholder > sizeof(a_szSuffix) - 1U \
         && memcmp(&pachPlaceholder[cchPlaceholder - sizeof(a_szSuffix) + 1U], a_szSuffix, sizeof(a_szSuffix) - 1U) == 0)
    if (PLACEHOLDER_ENDS_WITH("_SH@@"))
    {
        cchPlaceholder -= 3 + 2;
        enmEscaping = kValueEscaping_Bourne;
    }
    else if (PLACEHOLDER_ENDS_WITH("_ELEMENT@@"))
    {
        cchPlaceholder -= 8 + 2;
        enmEscaping = kValueEscaping_XML_Element;
    }
    else if (PLACEHOLDER_ENDS_WITH("_ATTRIB_DQ@@"))
    {
        cchPlaceholder -= 10 + 2;
        enmEscaping = kValueEscaping_XML_Attribute_Double_Quotes;
    }
    else
    {
        Assert(PLACEHOLDER_ENDS_WITH("@@"));
        cchPlaceholder -= 2;
        enmEscaping = kValueEscaping_None;
    }
#undef PLACEHOLDER_ENDS_WITH

    /*
     * Resolve and escape the value.
     */
    HRESULT hrc;
    try
    {
        Utf8Str     strTmp;
        const char *pszReadOnlyValue = NULL;
        int vrc = queryVariable(pachPlaceholder + sizeof(g_szPrefixInsertXxx) - 1,
                                cchPlaceholder  - sizeof(g_szPrefixInsertXxx) + 1,
                                strTmp, fOutputting ? &pszReadOnlyValue : NULL);
        if (RT_SUCCESS(vrc))
        {
            if (fOutputting)
            {
                Assert(pszReadOnlyValue != NULL);
                switch (enmEscaping)
                {
                    case kValueEscaping_None:
                        rValue = pszReadOnlyValue;
                        return S_OK;

                    case kValueEscaping_Bourne:
                    case kValueEscaping_XML_Element:
                    case kValueEscaping_XML_Attribute_Double_Quotes:
                    {
                        switch (enmEscaping)
                        {
                            case kValueEscaping_Bourne:
                            {
                                const char * const papszArgs[2] = { pszReadOnlyValue, NULL };
                                char              *pszEscaped   = NULL;
                                vrc = RTGetOptArgvToString(&pszEscaped, papszArgs, RTGETOPTARGV_CNV_QUOTE_BOURNE_SH);
                                if (RT_SUCCESS(vrc))
                                {
                                    try
                                    {
                                        rValue = pszEscaped;
                                        RTStrFree(pszEscaped);
                                        return S_OK;
                                    }
                                    catch (std::bad_alloc &)
                                    {
                                        hrc = E_OUTOFMEMORY;
                                    }
                                    RTStrFree(pszEscaped);
                                }
                                else
                                    hrc = mpSetError->setErrorVrc(vrc);
                                break;
                            }

                            case kValueEscaping_XML_Element:
                                rValue.printf("%RMes", pszReadOnlyValue);
                                return S_OK;

                            case kValueEscaping_XML_Attribute_Double_Quotes:
                            {
                                RTCString strTmp2;
                                strTmp2.printf("%RMas", pszReadOnlyValue);
                                rValue = RTCString(strTmp2, 1, strTmp2.length() - 2);
                                return S_OK;
                            }

                            default:
                                hrc = E_FAIL;
                                break;
                        }
                        break;
                    }

                    default:
                        AssertFailedStmt(hrc = E_FAIL);
                        break;
                }
            }
            else
                hrc = S_OK;
        }
        else
            hrc = E_FAIL;
    }
    catch (std::bad_alloc &)
    {
        hrc = E_OUTOFMEMORY;
    }
    rValue.setNull();
    return hrc;
}

HRESULT UnattendedScriptTemplate::getReplacementForExpr(RTEXPREVAL hEvaluator, const char *pachPlaceholder, size_t cchPlaceholder,
                                                        bool fOutputting, char **ppszValue) RT_NOEXCEPT
{
    /*
     * Process the tail of the placeholder to figure out the escaping rules.
     *
     * @@VBOX_INSERT[expr]@@:
     * @@VBOX_INSERT[expr]SH@@:
     * @@VBOX_INSERT[expr]ELEMENT@@:
     * @@VBOX_INSERT[expr]ATTRIB_DQ@@:
     */
    kEvalEscaping_T enmEscaping;
#define PLACEHOLDER_ENDS_WITH(a_szSuffix) \
        (   cchPlaceholder > sizeof(a_szSuffix) - 1U \
         && memcmp(&pachPlaceholder[cchPlaceholder - sizeof(a_szSuffix) + 1U], a_szSuffix, sizeof(a_szSuffix) - 1U) == 0)
    if (PLACEHOLDER_ENDS_WITH("]SH@@"))
    {
        cchPlaceholder -= sizeof("]SH@@") - 1;
        enmEscaping = kValueEscaping_Bourne;
    }
    else if (PLACEHOLDER_ENDS_WITH("]ELEMENT@@"))
    {
        cchPlaceholder -= sizeof("]ELEMENT@@") - 1;
        enmEscaping = kValueEscaping_XML_Element;
    }
    else if (PLACEHOLDER_ENDS_WITH("]ATTRIB_DQ@@"))
    {
        cchPlaceholder -= sizeof("]ATTRIB_DQ@@") - 1;
        enmEscaping = kValueEscaping_XML_Attribute_Double_Quotes;
    }
    else if (PLACEHOLDER_ENDS_WITH("]@@"))
    {
        cchPlaceholder -= sizeof("]@@") - 1;
        enmEscaping = kValueEscaping_None;
    }
    else
        return mpSetError->setErrorBoth(E_FAIL, VERR_PARSE_ERROR, tr("Malformed @@VBOX_INSERT[expr]@@: Missing ']' (%.*s)"),
                                        cchPlaceholder, pachPlaceholder);
#undef PLACEHOLDER_ENDS_WITH

    /* The placeholder prefix length.  The expression is from cchPrefix to cchPlaceholder. */
    size_t const cchPrefix = sizeof(g_szPrefixInsertExpr) - 1;
    Assert(pachPlaceholder[cchPrefix - 1] == '[');

    /*
     * Evaluate the expression.  We do this regardless of fOutput for now.
     */
    RTERRINFOSTATIC ErrInfo;
    char *pszValue = NULL;
    int vrc = RTExprEvalToString(hEvaluator, &pachPlaceholder[cchPrefix], cchPlaceholder - cchPrefix, &pszValue,
                                 RTErrInfoInitStatic(&ErrInfo));
    LogFlowFunc(("RTExprEvalToString(%.*s) -> %Rrc pszValue=%s\n",
                 cchPlaceholder - cchPrefix, &pachPlaceholder[cchPrefix], vrc, pszValue));
    if (RT_SUCCESS(vrc))
    {
        if (fOutputting)
        {
            switch (enmEscaping)
            {
                case kValueEscaping_None:
                    *ppszValue = pszValue;
                    pszValue = NULL;
                    break;

                case kValueEscaping_Bourne:
                {
                    const char * const papszArgs[2] = { pszValue, NULL };
                    vrc = RTGetOptArgvToString(ppszValue, papszArgs, RTGETOPTARGV_CNV_QUOTE_BOURNE_SH);
                    break;
                }

                case kValueEscaping_XML_Element:
                    vrc = RTStrAPrintf(ppszValue, "%RMes", pszValue);
                    break;

                case kValueEscaping_XML_Attribute_Double_Quotes:
                    vrc = RTStrAPrintf(ppszValue, "%RMas", pszValue);
                    if (RT_SUCCESS(vrc))
                    {
                        /* drop the quotes */
                        char *pszRet = *ppszValue;
                        size_t const cchRet = strlen(pszRet) - 2;
                        memmove(pszRet, &pszRet[1], cchRet);
                        pszRet[cchRet] = '\0';
                    }
                    break;

                default:
                    AssertFailedStmt(vrc = VERR_IPE_NOT_REACHED_DEFAULT_CASE);
                    break;
            }
            RTStrFree(pszValue);
            if (RT_FAILURE(vrc))
                return mpSetError->setErrorVrc(vrc);
        }
        else
        {
            *ppszValue = NULL;
            RTStrFree(pszValue);
        }
    }
    else
        return mpSetError->setErrorBoth(E_FAIL, vrc, tr("Expression evaluation error for '%.*s': %#RTeic"),
                                        cchPlaceholder, pachPlaceholder, &ErrInfo.Core);
    return S_OK;
}

HRESULT UnattendedScriptTemplate::resolveConditionalExpr(RTEXPREVAL hEvaluator, const char *pachPlaceholder,
                                                         size_t cchPlaceholder, bool *pfOutputting) RT_NOEXCEPT
{
    /*
     * Check the placeholder tail: @@VBOX_COND[expr]@@
     */
    static const char s_szTail[] = "]@@";
    if (memcmp(&pachPlaceholder[cchPlaceholder - sizeof(s_szTail) + 1], RT_STR_TUPLE(s_szTail)) != 0)
        return mpSetError->setErrorBoth(E_FAIL, VERR_PARSE_ERROR, tr("Malformed @@VBOX_COND[expr]@@: Missing ']' (%.*s)"),
                                        cchPlaceholder, pachPlaceholder);
    Assert(pachPlaceholder[sizeof(g_szPrefixCondExpr) - 2 ] == '[');

    /*
     * Evaluate the expression.
     */
    RTERRINFOSTATIC    ErrInfo;
    const char * const pchExpr = &pachPlaceholder[sizeof(g_szPrefixCondExpr) - 1];
    size_t const       cchExpr = cchPlaceholder - sizeof(g_szPrefixCondExpr) + 1 - sizeof(s_szTail) + 1;
    int vrc = RTExprEvalToBool(hEvaluator, pchExpr, cchExpr, pfOutputting, RTErrInfoInitStatic(&ErrInfo));
    LogFlowFunc(("RTExprEvalToBool(%.*s) -> %Rrc *pfOutputting=%s\n", cchExpr, pchExpr, vrc, *pfOutputting));
    if (RT_SUCCESS(vrc))
        return S_OK;
    return mpSetError->setErrorBoth(E_FAIL, vrc, tr("Expression evaluation error for '%.*s': %#RTeic"),
                                    cchPlaceholder, pachPlaceholder, &ErrInfo.Core);
}

/*static */ DECLCALLBACK(int)
UnattendedScriptTemplate::queryVariableForExpr(const char *pchName, size_t cchName, void *pvUser, char **ppszValue) RT_NOEXCEPT
{
    UnattendedScriptTemplate *pThis = (UnattendedScriptTemplate *)pvUser;
    int vrc;
    try
    {
        const char *pszReadOnlyValue = NULL;
        Utf8Str     strTmp;
        vrc = pThis->queryVariable(pchName, cchName, strTmp, ppszValue ? &pszReadOnlyValue : NULL);
        if (ppszValue)
        {
            if (RT_SUCCESS(vrc))
                vrc = RTStrDupEx(ppszValue, pszReadOnlyValue);
            else
                *ppszValue = NULL;
        }
    }
    catch (std::bad_alloc &)
    {
        vrc = VERR_NO_MEMORY;
        *ppszValue = NULL;
    }
    return vrc;
}

/* static */
int UnattendedScriptTemplate::shaCryptGenerateSalt(char *pszSalt, size_t cchSalt)
{
#ifdef IN_TST_UNATTENDED_SCRIPT
    /* Use a fixed salt to predict the hashing result with the testcases. */
    return RTStrCopy(pszSalt, cchSalt + 1, "testcase123");
#else
    return RTCrShaCryptGenerateSalt(pszSalt, cchSalt);
#endif
}

int UnattendedScriptTemplate::queryVariable(const char *pchName, size_t cchName, Utf8Str &rstrTmp, const char **ppszValue)
{
#define IS_MATCH(a_szMatch) \
        (cchName == sizeof(a_szMatch) - 1U && memcmp(pchName, a_szMatch, sizeof(a_szMatch) - 1U) == 0)

/** Uses the RTCrShaCrypt APIs to hash and crypt data. Uses a randomized salt + (recommended) default rounds.
 * @todo r=bird: Iff a template needs the salted password more than once,
 *        this approach will not deliver the same value.
 */
#define SHACRYPT_AND_ASSIGN(a_szKey, a_fnCrypt, a_cchMaxResult) \
        do { \
            if (ppszValue) \
            { \
                char szSalt[RT_SHACRYPT_SALT_MAX_LEN + 1]; \
                int vrc = UnattendedScriptTemplate::shaCryptGenerateSalt(szSalt, sizeof(szSalt) - 1); \
                AssertRCReturnStmt(vrc, mpSetError->setErrorBoth(E_FAIL, vrc, tr("RTCrShaCryptGenerateSalt failed: %Rrc"), vrc), \
                                   vrc); \
                rstrTmp.reserve(a_cchMaxResult); \
                vrc = a_fnCrypt(a_szKey, szSalt, RT_SHACRYPT_ROUNDS_DEFAULT, rstrTmp.mutableRaw(), a_cchMaxResult); \
                AssertRCReturnStmt(vrc, mpSetError->setErrorBoth(E_FAIL, vrc, tr(#a_fnCrypt " failed: %Rrc"), vrc), vrc); \
                rstrTmp.jolt(); \
                pszValue = rstrTmp.c_str(); \
            } \
        } while (0)

    const char *pszValue = NULL;

    /*
     * Variables
     */
    if (IS_MATCH("USER_LOGIN"))
        pszValue = mpUnattended->i_getUser().c_str();
    else if (IS_MATCH("USER_PASSWORD"))
        pszValue = mpUnattended->i_getUserPassword().c_str();
    else if (IS_MATCH("USER_PASSWORD_SHACRYPT512"))
        SHACRYPT_AND_ASSIGN(mpUnattended->i_getUserPassword().c_str(), RTCrShaCrypt512, RT_SHACRYPT_512_MAX_SIZE);
    else if (IS_MATCH("ROOT_PASSWORD"))
        pszValue = mpUnattended->i_getAdminPassword().c_str();
    else if (IS_MATCH("ROOT_PASSWORD_SHACRYPT512"))
        SHACRYPT_AND_ASSIGN(mpUnattended->i_getAdminPassword().c_str(), RTCrShaCrypt512, RT_SHACRYPT_512_MAX_SIZE);
    else if (IS_MATCH("USER_FULL_NAME"))
        pszValue = mpUnattended->i_getFullUserName().c_str();
    else if (IS_MATCH("PRODUCT_KEY"))
        pszValue = mpUnattended->i_getProductKey().c_str();
    else if (IS_MATCH("POST_INSTALL_COMMAND"))
        pszValue = mpUnattended->i_getPostInstallCommand().c_str();
    else if (IS_MATCH("AUXILIARY_INSTALL_DIR"))
        pszValue = mpUnattended->i_getAuxiliaryInstallDir().c_str();
    else if (IS_MATCH("IMAGE_INDEX"))
        pszValue = rstrTmp.printf("%u", mpUnattended->i_getImageIndex()).c_str();
    else if (IS_MATCH("OS_ARCH"))
        pszValue = mpUnattended->i_isGuestOs64Bit() ? "amd64" : "x86";
    else if (IS_MATCH("OS_ARCH2"))
        pszValue = mpUnattended->i_isGuestOs64Bit() ? "x86_64" : "x86";
    else if (IS_MATCH("OS_ARCH3"))
        pszValue = mpUnattended->i_isGuestOs64Bit() ? "x86_64" : "i386";
    else if (IS_MATCH("OS_ARCH4"))
        pszValue = mpUnattended->i_isGuestOs64Bit() ? "x86_64" : "i486";
    else if (IS_MATCH("OS_ARCH6"))
        pszValue = mpUnattended->i_isGuestOs64Bit() ? "x86_64" : "i686";
    else if (IS_MATCH("GUEST_OS_VERSION"))
        pszValue = mpUnattended->i_getDetectedOSVersion().c_str();
    else if (IS_MATCH("GUEST_OS_MAJOR_VERSION"))
    {
        Utf8Str const &rstrOsVer = mpUnattended->i_getDetectedOSVersion();
        size_t offDot = rstrOsVer.find('.');
        if (offDot > 0 && offDot != Utf8Str::npos)
            pszValue = rstrTmp.assign(rstrOsVer, 0, offDot).c_str(); /* caller catches std::bad_alloc */
        else if (!ppszValue)
            return VERR_NOT_FOUND;
        else
        {
            mpSetError->setErrorBoth(E_FAIL, VERR_NO_DATA, tr("Unknown guest OS major version '%s'"), rstrOsVer.c_str());
            return VERR_NO_DATA;
        }
    }
    else if (IS_MATCH("TIME_ZONE_UX"))
        pszValue = mpUnattended->i_getTimeZoneInfo()
                 ? mpUnattended->i_getTimeZoneInfo()->pszUnixName : mpUnattended->i_getTimeZone().c_str();
    else if (IS_MATCH("TIME_ZONE_WIN_NAME"))
    {
        PCRTTIMEZONEINFO pInfo = mpUnattended->i_getTimeZoneInfo();
        if (pInfo)
            pszValue = pInfo->pszWindowsName ? pInfo->pszWindowsName : "GMT";
        else
            pszValue = mpUnattended->i_getTimeZone().c_str();
    }
    else if (IS_MATCH("TIME_ZONE_WIN_INDEX"))
    {
        PCRTTIMEZONEINFO pInfo = mpUnattended->i_getTimeZoneInfo();
        if (pInfo)
            pszValue = rstrTmp.printf("%u", pInfo->idxWindows ? pInfo->idxWindows : 85 /*GMT*/).c_str();
        else
            pszValue = mpUnattended->i_getTimeZone().c_str();
    }
    else if (IS_MATCH("LOCALE"))
        pszValue = mpUnattended->i_getLocale().c_str();
    else if (IS_MATCH("DASH_LOCALE"))
    {
        Assert(mpUnattended->i_getLocale()[2] == '_');
        pszValue = rstrTmp.assign(mpUnattended->i_getLocale()).replace(2, 1, "-").c_str();
    }
    else if (IS_MATCH("LANGUAGE"))
        pszValue = mpUnattended->i_getLanguage().c_str();
    else if (IS_MATCH("COUNTRY"))
        pszValue = mpUnattended->i_getCountry().c_str();
    else if (IS_MATCH("HOSTNAME_FQDN"))
        pszValue = mpUnattended->i_getHostname().c_str();
    else if (IS_MATCH("HOSTNAME_WITHOUT_DOMAIN"))
        pszValue = rstrTmp.assign(mpUnattended->i_getHostname(), 0, mpUnattended->i_getHostname().find(".")).c_str();
    else if (IS_MATCH("HOSTNAME_WITHOUT_DOMAIN_MAX_15"))
        pszValue = rstrTmp.assign(mpUnattended->i_getHostname(), 0, RT_MIN(mpUnattended->i_getHostname().find("."), 15)).c_str();
    else if (IS_MATCH("HOSTNAME_DOMAIN"))
        pszValue = rstrTmp.assign(mpUnattended->i_getHostname(), mpUnattended->i_getHostname().find(".") + 1).c_str();
    else if (IS_MATCH("PROXY"))
        pszValue = mpUnattended->i_getProxy().c_str();
    else if (IS_MATCH("ADDITIONS_INSTALL_PACKAGE_NAME"))
        pszValue = mpUnattended->i_getAdditionsInstallPackage().c_str();
    /*
     * Indicator variables.
     */
    else if (IS_MATCH("IS_INSTALLING_ADDITIONS"))
        pszValue = mpUnattended->i_getInstallGuestAdditions() ? "1" : "0";
    else if (IS_MATCH("IS_USER_LOGIN_ADMINISTRATOR"))
        pszValue = mpUnattended->i_getUser().compare("Administrator", RTCString::CaseInsensitive) == 0 ? "1" : "0";
    else if (IS_MATCH("IS_INSTALLING_TEST_EXEC_SERVICE"))
        pszValue = mpUnattended->i_getInstallTestExecService() ? "1" : "0";
    else if (IS_MATCH("HAS_POST_INSTALL_COMMAND"))
        pszValue = mpUnattended->i_getPostInstallCommand().isNotEmpty() ? "1" : "0";
    else if (IS_MATCH("HAS_PRODUCT_KEY"))
        pszValue = mpUnattended->i_getProductKey().isNotEmpty() ? "1" : "0";
    else if (IS_MATCH("IS_MINIMAL_INSTALLATION"))
        pszValue = mpUnattended->i_isMinimalInstallation() ? "1" : "0";
    else if (IS_MATCH("IS_FIRMWARE_UEFI"))
        pszValue = mpUnattended->i_isFirmwareEFI() ? "1" : "0";
    else if (IS_MATCH("IS_RTC_USING_UTC"))
        pszValue = mpUnattended->i_isRtcUsingUtc() ? "1" : "0";
    else if (IS_MATCH("HAS_PROXY"))
        pszValue = mpUnattended->i_getProxy().isNotEmpty() ? "1" : "0";
    else if (IS_MATCH("AVOID_UPDATES_OVER_NETWORK"))
        pszValue = mpUnattended->i_getAvoidUpdatesOverNetwork() ? "1" : "0";
    /*
     * Unknown variable.
     */
    else if (!ppszValue)
        return VERR_NOT_FOUND;
    else
    {
        mpSetError->setErrorBoth(E_FAIL, VERR_NOT_FOUND, tr("Unknown variable '%.*s'"), cchName, pchName);
        return VERR_NO_DATA;
    }
    if (ppszValue)
        *ppszValue = pszValue;
    return VINF_SUCCESS;
}

HRESULT UnattendedScriptTemplate::getConditional(const char *pachPlaceholder, size_t cchPlaceholder, bool *pfOutputting)
{
#define IS_PLACEHOLDER_MATCH(a_szMatch) \
        (   cchPlaceholder == sizeof("@@VBOX_COND_" a_szMatch "@@") - 1U \
         && memcmp(pachPlaceholder, "@@VBOX_COND_" a_szMatch "@@", sizeof("@@VBOX_COND_" a_szMatch "@@") - 1U) == 0)

    /* Install Guest Additions: */
    if (IS_PLACEHOLDER_MATCH("IS_INSTALLING_ADDITIONS"))
        *pfOutputting = mpUnattended->i_getInstallGuestAdditions();
    else if (IS_PLACEHOLDER_MATCH("IS_NOT_INSTALLING_ADDITIONS"))
        *pfOutputting = !mpUnattended->i_getInstallGuestAdditions();
    /* User == Administrator: */
    else if (IS_PLACEHOLDER_MATCH("IS_USER_LOGIN_ADMINISTRATOR"))
        *pfOutputting = mpUnattended->i_getUser().compare("Administrator", RTCString::CaseInsensitive) == 0;
    else if (IS_PLACEHOLDER_MATCH("IS_USER_LOGIN_NOT_ADMINISTRATOR"))
        *pfOutputting = mpUnattended->i_getUser().compare("Administrator", RTCString::CaseInsensitive) != 0;
    /* Install TXS: */
    else if (IS_PLACEHOLDER_MATCH("IS_INSTALLING_TEST_EXEC_SERVICE"))
        *pfOutputting = mpUnattended->i_getInstallTestExecService();
    else if (IS_PLACEHOLDER_MATCH("IS_NOT_INSTALLING_TEST_EXEC_SERVICE"))
        *pfOutputting = !mpUnattended->i_getInstallTestExecService();
    /* Post install command: */
    else if (IS_PLACEHOLDER_MATCH("HAS_POST_INSTALL_COMMAND"))
        *pfOutputting = mpUnattended->i_getPostInstallCommand().isNotEmpty();
    else if (IS_PLACEHOLDER_MATCH("HAS_NO_POST_INSTALL_COMMAND"))
        *pfOutputting = mpUnattended->i_getPostInstallCommand().isEmpty();
    /* Product key: */
    else if (IS_PLACEHOLDER_MATCH("HAS_PRODUCT_KEY"))
        *pfOutputting = mpUnattended->i_getProductKey().isNotEmpty();
    else if (IS_PLACEHOLDER_MATCH("HAS_NO_PRODUCT_KEY"))
        *pfOutputting = mpUnattended->i_getProductKey().isEmpty();
    /* Minimal installation: */
    else if (IS_PLACEHOLDER_MATCH("IS_MINIMAL_INSTALLATION"))
        *pfOutputting = mpUnattended->i_isMinimalInstallation();
    else if (IS_PLACEHOLDER_MATCH("IS_NOT_MINIMAL_INSTALLATION"))
        *pfOutputting = !mpUnattended->i_isMinimalInstallation();
    /* Is firmware UEFI: */
    else if (IS_PLACEHOLDER_MATCH("IS_FIRMWARE_UEFI"))
        *pfOutputting = mpUnattended->i_isFirmwareEFI();
    else if (IS_PLACEHOLDER_MATCH("IS_NOT_FIRMWARE_UEFI"))
        *pfOutputting = !mpUnattended->i_isFirmwareEFI();
    /* Is RTC using UTC (i.e. set to UTC time on startup): */
    else if (IS_PLACEHOLDER_MATCH("IS_RTC_USING_UTC"))
        *pfOutputting = mpUnattended->i_isRtcUsingUtc();
    else if (IS_PLACEHOLDER_MATCH("IS_NOT_RTC_USING_UTC"))
        *pfOutputting = !mpUnattended->i_isRtcUsingUtc();
    else if (IS_PLACEHOLDER_MATCH("HAS_PROXY"))
        *pfOutputting = mpUnattended->i_getProxy().isNotEmpty();
    else if (IS_PLACEHOLDER_MATCH("AVOID_UPDATES_OVER_NETWORK"))
        *pfOutputting = mpUnattended->i_getAvoidUpdatesOverNetwork();
    else
        return mpSetError->setErrorBoth(E_FAIL, VERR_NOT_FOUND, tr("Unknown conditional placeholder '%.*s'"),
                                        cchPlaceholder, pachPlaceholder);
    return S_OK;
#undef IS_PLACEHOLDER_MATCH
}

#endif /* VBOX_WITH_UNATTENDED */
#if 0 /* Keeping this a reference */


/*********************************************************************************************************************************
*   UnattendedSUSEXMLScript Implementation                                                                                       *
*********************************************************************************************************************************/

HRESULT UnattendedSUSEXMLScript::parse()
{
    HRESULT hrc = UnattendedXMLScript::parse();
    if (SUCCEEDED(hrc))
    {
        /*
         * Check that we've got the right root element type.
         */
        const xml::ElementNode *pelmRoot = mDoc.getRootElement();
        if (   pelmRoot
            && strcmp(pelmRoot->getName(), "profile") == 0)
        {
            /*
             * Work thought the sections.
             */
            try
            {
                LoopThruSections(pelmRoot);
                hrc = S_OK;
            }
            catch (std::bad_alloc &)
            {
                hrc = E_OUTOFMEMORY;
            }
        }
        else if (pelmRoot)
            hrc = mpSetError->setError(E_FAIL, tr("XML document root element is '%s' instead of 'profile'"),
                                       pelmRoot->getName());
        else
            hrc = mpSetError->setError(E_FAIL, tr("Missing XML root element"));
    }
    return hrc;
}

HRESULT UnattendedSUSEXMLScript::setFieldInElement(xml::ElementNode *pElement, const DataId enmDataId, const Utf8Str &rStrValue)
{
    /*
     * Don't set empty values.
     */
    if (rStrValue.isEmpty())
    {
        Utf8Str strProbableValue;
        try
        {
            strProbableValue = createProbableValue(enmDataId, pElement);
        }
        catch (std::bad_alloc &)
        {
            return E_OUTOFMEMORY;
        }
        return UnattendedXMLScript::setFieldInElement(pElement, enmDataId, strProbableValue);
    }
    return UnattendedXMLScript::setFieldInElement(pElement, enmDataId, rStrValue);
}

HRESULT UnattendedSUSEXMLScript::LoopThruSections(const xml::ElementNode *pelmRoot)
{
    xml::NodesLoop loopChildren(*pelmRoot);
    const xml::ElementNode *pelmOuterLoop;
    while ((pelmOuterLoop = loopChildren.forAllNodes()) != NULL)
    {
        const char *pcszElemName = pelmOuterLoop->getName();
        if (!strcmp(pcszElemName, "users"))
        {
            xml::NodesLoop loopUsers(*pelmOuterLoop);
            const xml::ElementNode *pelmUser;
            while ((pelmUser = loopUsers.forAllNodes()) != NULL)
            {
                HRESULT hrc = HandleUserAccountsSection(pelmUser);
                if (FAILED(hrc))
                    return hrc;
            }
        }
    }
    return S_OK;
}

HRESULT UnattendedSUSEXMLScript::HandleUserAccountsSection(const xml::ElementNode *pelmSection)
{
    xml::NodesLoop loopUser(*pelmSection);

    const xml::ElementNode *pelmCur;
    while ((pelmCur = loopUser.forAllNodes()) != NULL)
    {
        const char *pszValue = pelmCur->getValue();
#ifdef LOG_ENABLED
        if (!RTStrCmp(pelmCur->getName(), "uid"))
            LogRelFunc(("UnattendedSUSEXMLScript::HandleUserAccountsSection profile/users/%s/%s = %s\n",
                        pelmSection->getName(), pelmCur->getName(), pszValue));
#endif

        if (!RTStrCmp(pszValue, "$homedir"))
            mNodesForCorrectionMap.insert(make_pair(USERHOMEDIR_ID, pelmCur));

        if (!RTStrCmp(pszValue, "$user"))
            mNodesForCorrectionMap.insert(make_pair(USERNAME_ID, pelmCur));

        if (!RTStrCmp(pszValue, "$password"))
            mNodesForCorrectionMap.insert(make_pair(USERPASSWORD_ID, pelmCur));
    }
    return S_OK;
}

Utf8Str UnattendedSUSEXMLScript::createProbableValue(const DataId enmDataId, const xml::ElementNode *pCurElem)
{
    const xml::ElementNode *pElem = pCurElem;

    switch (enmDataId)
    {
        case USERHOMEDIR_ID:
//          if ((pElem = pElem->findChildElement("home")))
//          {
                return createProbableUserHomeDir(pElem);
//          }
            break;
        default:
            break;
    }

    return Utf8Str::Empty;
}

Utf8Str UnattendedSUSEXMLScript::createProbableUserHomeDir(const xml::ElementNode *pCurElem)
{
    Utf8Str strCalcValue;
    const xml::ElementNode *pElem = pCurElem->findNextSibilingElement("username");
    if (pElem)
    {
        const char *pszValue = pElem->getValue();
        strCalcValue = "/home/";
        strCalcValue.append(pszValue);
    }

    return strCalcValue;
}
#endif /* just for reference */
