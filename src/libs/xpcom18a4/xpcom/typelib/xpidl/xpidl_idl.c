/* $Id: xpidl_idl.c 109507 2025-05-12 17:53:56Z vadim.galitsyn@oracle.com $ */
/** @file
 * VBox xpidl clone - IDL parsing.
 */

/*
 * Copyright (C) 2025 Oracle and/or its affiliates.
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
#include <iprt/cdefs.h>
#include <iprt/errcore.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>

#include "xpidl.h"


typedef enum XPIDLKEYWORD
{
    kXpidlKeyword_Invalid = 0,
    kXpidlKeyword_Include,
    kXpidlKeyword_Typedef,
    kXpidlKeyword_Void,
    kXpidlKeyword_Char,
    kXpidlKeyword_Wide_Char,
    kXpidlKeyword_Unsigned,
    kXpidlKeyword_Long,
    kXpidlKeyword_Short,
    kXpidlKeyword_Boolean,
    kXpidlKeyword_Octet,
    kXpidlKeyword_String,
    kXpidlKeyword_Wide_String,
    kXpidlKeyword_Double,
    kXpidlKeyword_Float,
    kXpidlKeyword_Native,
    kXpidlKeyword_Interface,
    kXpidlKeyword_Readonly,
    kXpidlKeyword_Attribute,
    kXpidlKeyword_In,
    kXpidlKeyword_Out,
    kXpidlKeyword_InOut,
    kXpidlKeyword_Const,
    kXpidlKeyword_32Bit_Hack = 0x7fffffff
} XPIDLKEYWORD;
typedef const XPIDLKEYWORD *PCXPIDLKEYWORD;
typedef XPIDLKEYWORD *PXPIDLKEYWORD;

static DECLCALLBACK(int) xpidlIdlLexParseNumberIdentifierOrUuid(RTSCRIPTLEX hScriptLex, char ch, PRTSCRIPTLEXTOKEN pToken, void *pvUser);


static const char *s_aszSingleStart[] =
{
    "//",
    NULL
};


static const char *s_aszMultiStart[] =
{
    "/*",
    "%{C++",
    "%{ C++",
    NULL
};


static const char *s_aszMultiEnd[] =
{
    "*/",
    "%}",
    "%}",
    NULL
};


static const RTSCRIPTLEXTOKMATCH s_aMatches[] =
{
    { RT_STR_TUPLE("#include"),                 RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Include     },

    { RT_STR_TUPLE("void"),                     RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Void        },
    { RT_STR_TUPLE("char"),                     RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Char        },
    { RT_STR_TUPLE("long"),                     RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Long        },
    { RT_STR_TUPLE("wchar"),                    RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Wide_Char   },
    { RT_STR_TUPLE("wstring"),                  RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Wide_String },
    { RT_STR_TUPLE("boolean"),                  RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Boolean     },
    { RT_STR_TUPLE("double"),                   RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Double      },
    { RT_STR_TUPLE("float"),                    RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Float       },
    { RT_STR_TUPLE("octet"),                    RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Octet       },
    { RT_STR_TUPLE("short"),                    RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Short       },
    { RT_STR_TUPLE("string"),                   RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_String      },
    { RT_STR_TUPLE("unsigned"),                 RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Unsigned    },

    { RT_STR_TUPLE("typedef"),                  RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Typedef     },
    { RT_STR_TUPLE("native"),                   RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Native      },
    { RT_STR_TUPLE("interface"),                RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Interface   },
    { RT_STR_TUPLE("readonly"),                 RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Readonly    },
    { RT_STR_TUPLE("attribute"),                RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Attribute   },
    { RT_STR_TUPLE("in"),                       RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_In          },
    { RT_STR_TUPLE("inout"),                    RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_InOut       },
    { RT_STR_TUPLE("out"),                      RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Out         },
    { RT_STR_TUPLE("const"),                    RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  kXpidlKeyword_Const       },

    { RT_STR_TUPLE(","),                        RTSCRIPTLEXTOKTYPE_PUNCTUATOR, false, ',' },
    { RT_STR_TUPLE("["),                        RTSCRIPTLEXTOKTYPE_PUNCTUATOR, false, '[' },
    { RT_STR_TUPLE("]"),                        RTSCRIPTLEXTOKTYPE_PUNCTUATOR, false, ']' },
    { RT_STR_TUPLE("{"),                        RTSCRIPTLEXTOKTYPE_PUNCTUATOR, false, '{' },
    { RT_STR_TUPLE("}"),                        RTSCRIPTLEXTOKTYPE_PUNCTUATOR, false, '}' },
    { RT_STR_TUPLE("("),                        RTSCRIPTLEXTOKTYPE_PUNCTUATOR, false, '(' },
    { RT_STR_TUPLE(")"),                        RTSCRIPTLEXTOKTYPE_PUNCTUATOR, false, ')' },
    { RT_STR_TUPLE(";"),                        RTSCRIPTLEXTOKTYPE_PUNCTUATOR, false, ';' },
    { RT_STR_TUPLE("="),                        RTSCRIPTLEXTOKTYPE_PUNCTUATOR, false, '=' },
    { RT_STR_TUPLE(":"),                        RTSCRIPTLEXTOKTYPE_PUNCTUATOR, false, ':' },
    { NULL, 0,                                  RTSCRIPTLEXTOKTYPE_INVALID,    false, 0 }
};


static const RTSCRIPTLEXRULE s_aRules[] =
{
    { '\"', '\"',  RTSCRIPT_LEX_RULE_CONSUME, RTScriptLexScanStringLiteralC,          NULL}, /** @todo This is not correct. */
    { '0',  '9',   RTSCRIPT_LEX_RULE_DEFAULT, xpidlIdlLexParseNumberIdentifierOrUuid, NULL},
    { 'a',  'z',   RTSCRIPT_LEX_RULE_DEFAULT, xpidlIdlLexParseNumberIdentifierOrUuid, NULL},
    { 'A',  'Z',   RTSCRIPT_LEX_RULE_DEFAULT, xpidlIdlLexParseNumberIdentifierOrUuid, NULL},
    { '_',  '_',   RTSCRIPT_LEX_RULE_DEFAULT, RTScriptLexScanIdentifier,              NULL},
    { '\0', '\0',  RTSCRIPT_LEX_RULE_DEFAULT, NULL,                                   NULL}
};


static const RTSCRIPTLEXCFG g_IdlLexCfg =
{
    /** pszName */
    "IDL",
    /** pszDesc */
    "IDL lexer",
    /** fFlags */
    RTSCRIPT_LEX_CFG_F_COMMENTS_AS_TOKENS,
    /** pszWhitespace */
    NULL,
    /** pszNewline */
    NULL,
    /** papszCommentMultiStart */
    s_aszMultiStart,
    /** papszCommentMultiEnd */
    s_aszMultiEnd,
    /** papszCommentSingleStart */
    s_aszSingleStart,
    /** paTokMatches */
    s_aMatches,
    /** paRules */
    s_aRules,
    /** pfnProdDef */
    NULL,
    /** pfnProdDefUser */
    NULL
};


static bool g_fParsingAttributes = false;
static bool g_fRequiredUuid      = false;

static int xpidlParseIdl(PXPIDLPARSE pThis, PXPIDLINPUT pInput, PRTLISTANCHOR pLstIncludePaths);


static DECLCALLBACK(int) xpidlIdlLexParseNumberIdentifierOrUuid(RTSCRIPTLEX hScriptLex, char ch, PRTSCRIPTLEXTOKEN pToken, void *pvUser)
{
    RT_NOREF(pvUser);
    if (g_fRequiredUuid)
    {
        g_fRequiredUuid = false;
        /* Scan as an identifier. */
        static const char *g_aszIdeCharSetUuid = "abcdefABCDEF01234567809-";
        RTScriptLexConsumeCh(hScriptLex);
        return RTScriptLexScanIdentifier(hScriptLex, ch, pToken, (void *)g_aszIdeCharSetUuid);
    }
    else if (ch >= '0' && ch <= '9')
        return RTScriptLexScanNumber(hScriptLex, 0 /*uBase*/, false /*fAllowReal*/, pToken);

    RTScriptLexConsumeCh(hScriptLex);
    return RTScriptLexScanIdentifier(hScriptLex, ch, pToken, NULL);
}


/**
 * Create a new lexer from the given filename, possibly searching the include paths.
 *
 * @returns IPRT status code.
 * @param   pszFilename             The filename to read.
 * @param   pLstIncludePaths        The list of include paths to search for relative filenames.
 * @param   phIdlLex                Where to store the handle to the lexer on success.
 */
static int xpidlCreateLexerFromFilename(const char *pszFilename, PRTLISTANCHOR pLstIncludePaths,
                                        PRTSCRIPTLEX phIdlLex)
{
    char szPath[RTPATH_MAX];

    if (pszFilename[0] != '/')
    {
        PCXPIDLINCLUDEDIR pIt;
        RTListForEach(pLstIncludePaths, pIt, XPIDLINCLUDEDIR, NdIncludes)
        {
            ssize_t cch = RTStrPrintf2(&szPath[0], sizeof(szPath), "%s%c%s",
                                       pIt->pszPath, RTPATH_SLASH, pszFilename);
            if (cch <= 0)
                return VERR_BUFFER_OVERFLOW;

            if (RTFileExists(szPath))
            {
                pszFilename = szPath;
                break;
            }
        }
    }

    return RTScriptLexCreateFromFile(phIdlLex, pszFilename, NULL /*phStrCacheId*/,
                                     NULL /*phStrCacheStringLit*/, NULL /*phStrCacheComments*/,
                                     &g_IdlLexCfg);
}


static int xpidlParseError(PXPIDLPARSE pThis, PXPIDLINPUT pInput, PCRTSCRIPTLEXTOKEN pTok, int rc, const char *pszFmt, ...)
{
    RT_NOREF(pInput);

    va_list Args;
    va_start(Args, pszFmt);
    RT_NOREF(pTok);
    rc = RTErrInfoSetV(&pThis->ErrInfo.Core, rc, pszFmt, Args);
    va_end(Args);
    return rc;
}


static int xpidlParseSkipComments(PXPIDLPARSE pThis, PXPIDLINPUT pInput, bool *pfRawBlock)
{
    for (;;)
    {
        PCRTSCRIPTLEXTOKEN pTok;
        int rc = RTScriptLexQueryToken(pInput->hIdlLex, &pTok);
        if (RT_FAILURE(rc))
            return xpidlParseError(pThis, pInput, pTok, rc, "Lexer: Failed to query string literal token with %Rrc", rc);

        if (   pTok->enmType != RTSCRIPTLEXTOKTYPE_COMMENT_SINGLE_LINE
            && pTok->enmType != RTSCRIPTLEXTOKTYPE_COMMENT_MULTI_LINE)
            return VINF_SUCCESS;

        /* Make sure we don't miss any %{C++ %} blocks. */
        if (   !strncmp(pTok->Type.Comment.pszComment, "%{C++", sizeof("%{C++") - 1)
            || !strncmp(pTok->Type.Comment.pszComment, "%{ C++", sizeof("%{ C++") - 1))
        {
            if (pfRawBlock)
                *pfRawBlock = true; 
            return VINF_SUCCESS;
        }

        RTScriptLexConsumeToken(pInput->hIdlLex);
    }

    return VINF_SUCCESS;
}


static int xpidlLexerConsumeIfStringLit(PXPIDLPARSE pThis, PXPIDLINPUT pInput, const char **ppszStrLit)
{
    PCRTSCRIPTLEXTOKEN pTok;
    int rc = RTScriptLexQueryToken(pInput->hIdlLex, &pTok);
    if (RT_FAILURE(rc))
        return xpidlParseError(pThis, pInput, NULL /*pTok*/, rc, "Lexer: Failed to query string literal token with %Rrc", rc);

    if (pTok->enmType == RTSCRIPTLEXTOKTYPE_STRINGLIT)
    {
        *ppszStrLit = pTok->Type.StringLit.pszString;
        RTScriptLexConsumeToken(pInput->hIdlLex);
        return VINF_SUCCESS;
    }

    return VINF_SUCCESS;
}


static int xpidlLexerConsumeIfKeyword(PXPIDLPARSE pThis, PXPIDLINPUT pInput, XPIDLKEYWORD enmKeyword, bool *pfConsumed)
{
    PCRTSCRIPTLEXTOKEN pTok;
    int rc = RTScriptLexQueryToken(pInput->hIdlLex, &pTok);
    if (RT_FAILURE(rc))
        return xpidlParseError(pThis, pInput, NULL, rc, "Lexer: Failed to query keyword token with %Rrc", rc);

    if (   pTok->enmType == RTSCRIPTLEXTOKTYPE_KEYWORD
        && pTok->Type.Keyword.pKeyword->u64Val == (uint64_t)enmKeyword)
    {
        RTScriptLexConsumeToken(pInput->hIdlLex);
        *pfConsumed = true;
        return VINF_SUCCESS;
    }

    *pfConsumed = false;
    return VINF_SUCCESS;
}


static int xpidlLexerConsumeIfKeywordInList(PXPIDLPARSE pThis, PXPIDLINPUT pInput,
                                            PCXPIDLKEYWORD paenmKeywords, PXPIDLKEYWORD penmKeyword)
{
    PCRTSCRIPTLEXTOKEN pTok;
    int rc = RTScriptLexQueryToken(pInput->hIdlLex, &pTok);
    if (RT_FAILURE(rc))
        return xpidlParseError(pThis, pInput, NULL, rc, "Lexer: Failed to query keyword token with %Rrc", rc);

    if (pTok->enmType == RTSCRIPTLEXTOKTYPE_KEYWORD)
    {
        unsigned i = 0;
        do
        {
            if (pTok->Type.Keyword.pKeyword->u64Val == (uint64_t)paenmKeywords[i])
            {
                RTScriptLexConsumeToken(pInput->hIdlLex);
                *penmKeyword = paenmKeywords[i];
                return VINF_SUCCESS;
            }

            i++;
        } while (paenmKeywords[i] != kXpidlKeyword_Invalid);
    }

    *penmKeyword = kXpidlKeyword_Invalid;
    return VINF_SUCCESS;
}


static int xpidlLexerConsumeIfIdentifier(PXPIDLPARSE pThis, PXPIDLINPUT pInput, bool fAllowKeywords, const char **ppszIde)
{
    PCRTSCRIPTLEXTOKEN pTok;
    int rc = RTScriptLexQueryToken(pInput->hIdlLex, &pTok);
    if (RT_FAILURE(rc))
        return xpidlParseError(pThis, pInput, NULL, rc, "Lexer: Failed to query string literal token with %Rrc", rc);

    if (pTok->enmType == RTSCRIPTLEXTOKTYPE_IDENTIFIER)
    {
        *ppszIde = pTok->Type.Id.pszIde;

        /*
         * HACK HACK HACK for UUIDs because they can start with digits but are not numbers.
         *
         * This must be done here before consuming the token because the lexer alwas peeks
         * ahead and the format for UUIDs is 'uuid(<UUID>)' so activating this hack afterwards
         * would make the lexer try to fill the next token with the wrong content (or most
         * likely fail).
         *
         * This assumes that uuid will not be used standalone elsewhere in the attributes.
         */
        if (   g_fParsingAttributes
            && !strcmp(pTok->Type.Id.pszIde, "uuid"))
            g_fRequiredUuid = true;

        RTScriptLexConsumeToken(pInput->hIdlLex);
        return VINF_SUCCESS;
    }
    else if (   fAllowKeywords
             && pTok->enmType == RTSCRIPTLEXTOKTYPE_KEYWORD)
    {
        *ppszIde = pTok->Type.Keyword.pKeyword->pszMatch;
        RTScriptLexConsumeToken(pInput->hIdlLex);
        return VINF_SUCCESS;
    }

    return VINF_SUCCESS;
}


static int xpidlLexerConsumeIfPunctuator(PXPIDLPARSE pThis, PXPIDLINPUT pInput,
                                         char chPunctuator, bool *pfConsumed)
{
    PCRTSCRIPTLEXTOKEN pTok;
    int rc = RTScriptLexQueryToken(pInput->hIdlLex, &pTok);
    if (RT_FAILURE(rc))
        return xpidlParseError(pThis, pInput, NULL, rc, "Lexer: Failed to query punctuator token with %Rrc", rc);

    if (   pTok->enmType == RTSCRIPTLEXTOKTYPE_PUNCTUATOR
        && (char)pTok->Type.Keyword.pKeyword->u64Val == chPunctuator)
    {
        RTScriptLexConsumeToken(pInput->hIdlLex);
        *pfConsumed = true;
        return VINF_SUCCESS;
    }

    *pfConsumed = false;
    return VINF_SUCCESS;
}


static int xpidlLexerConsumeIfNatural(PXPIDLPARSE pThis, PXPIDLINPUT pInput, uint64_t *pu64, bool *pfConsumed)
{
    PCRTSCRIPTLEXTOKEN pTok;
    int rc = RTScriptLexQueryToken(pInput->hIdlLex, &pTok);
    if (RT_FAILURE(rc))
        return xpidlParseError(pThis, pInput, NULL, rc, "Lexer: Failed to query punctuator token with %Rrc", rc);

    if (   pTok->enmType == RTSCRIPTLEXTOKTYPE_NUMBER
        && pTok->Type.Number.enmType == RTSCRIPTLEXTOKNUMTYPE_NATURAL)
    {
        *pfConsumed = true;
        *pu64 = pTok->Type.Number.Type.u64;
        RTScriptLexConsumeToken(pInput->hIdlLex);
        return VINF_SUCCESS;
    }

    *pfConsumed = false;
    return VINF_SUCCESS;
}

/* Some parser helper macros. */
#define XPIDL_PARSE_STRING_LIT(a_pszStrLit) \
    const char *a_pszStrLit = NULL; \
    do { \
        int rc2 = xpidlLexerConsumeIfStringLit(pThis, pInput, &a_pszStrLit); \
        if (RT_FAILURE(rc2)) \
            return rc2; \
        if (!a_pszStrLit) \
            return xpidlParseError(pThis, pInput, NULL, VERR_INVALID_PARAMETER, "Parser: Expected a string literal"); \
    } while(0)


#define XPIDL_PARSE_KEYWORD(a_enmKeyword, a_pszKeyword) \
    do { \
        bool fConsumed2 = false; \
        int rc2 = xpidlLexerConsumeIfKeyword(pThis, pInput, a_enmKeyword, &fConsumed2); \
        if (RT_FAILURE(rc2)) \
            return rc2; \
        if (!fConsumed2) \
            return xpidlParseError(pThis, pInput, NULL, VERR_INVALID_PARAMETER, "Parser: Expected keyword '%s'", a_pszKeyword); \
    } while(0)


#define XPIDL_PARSE_OPTIONAL_KEYWORD(a_fConsumed, a_enmKeyword) \
    bool a_fConsumed = false; \
    do { \
        int rc2 = xpidlLexerConsumeIfKeyword(pThis, pInput, a_enmKeyword, &a_fConsumed); \
        if (RT_FAILURE(rc2)) \
            return rc2; \
    } while(0)


#define XPIDL_PARSE_OPTIONAL_KEYWORD_LIST(a_enmKeyword, a_aenmKeywordList, a_enmDefault) \
    XPIDLKEYWORD a_enmKeyword = kXpidlKeyword_Invalid; \
    do { \
        int rc2 = xpidlLexerConsumeIfKeywordInList(pThis, pInput, a_aenmKeywordList, &a_enmKeyword); \
        if (RT_FAILURE(rc2)) \
            return rc2; \
        if (a_enmKeyword == kXpidlKeyword_Invalid) \
            a_enmKeyword = a_enmDefault; \
    } while(0)


#define XPIDL_PARSE_KEYWORD_LIST(a_enmKeyword, a_aenmKeywordList) \
    XPIDLKEYWORD a_enmKeyword = kXpidlKeyword_Invalid; \
    do { \
        int rc2 = xpidlLexerConsumeIfKeywordInList(pThis, pInput, a_aenmKeywordList, &a_enmKeyword); \
        if (RT_FAILURE(rc2)) \
            return rc2; \
        if (a_enmKeyword == kXpidlKeyword_Invalid) \
            return xpidlParseError(pThis, pInput, NULL, VERR_INVALID_PARAMETER, "Parser: Unexpected keyword found"); \
    } while(0)


#define XPIDL_PARSE_IDENTIFIER(a_pszIde) \
    const char *a_pszIde = NULL; \
    do { \
        int rc2 = xpidlLexerConsumeIfIdentifier(pThis, pInput, false /*fAllowKeywords*/, &a_pszIde); \
        if (RT_FAILURE(rc2)) \
            return rc2; \
        if (!a_pszIde) \
            return xpidlParseError(pThis, pInput, NULL, VERR_INVALID_PARAMETER, "Parser: Expected an identifier"); \
    } while(0)


#define XPIDL_PARSE_IDENTIFIER_ALLOW_KEYWORDS(a_pszIde) \
    const char *a_pszIde = NULL; \
    do { \
        int rc2 = xpidlLexerConsumeIfIdentifier(pThis, pInput, true /*fAllowKeywords*/, &a_pszIde); \
        if (RT_FAILURE(rc2)) \
            return rc2; \
        if (!a_pszIde) \
            return xpidlParseError(pThis, pInput, NULL, VERR_INVALID_PARAMETER, "Parser: Expected an identifier"); \
    } while(0)


#define XPIDL_PARSE_IDENTIFIER_EXT(a_pszIde) \
    do { \
        int rc2 = xpidlLexerConsumeIfIdentifier(pThis, pInput, false /*fAllowKeywords*/, &a_pszIde); \
        if (RT_FAILURE(rc2)) \
            return rc2; \
        if (!a_pszIde) \
            return xpidlParseError(pThis, pInput, NULL, VERR_INVALID_PARAMETER, "Parser: Expected an identifier"); \
    } while(0)


#define XPIDL_PARSE_PUNCTUATOR(a_chPunctuator) \
    do { \
        bool fConsumed2 = false; \
        int rc2 = xpidlLexerConsumeIfPunctuator(pThis, pInput, a_chPunctuator, &fConsumed2); \
        if (RT_FAILURE(rc2)) \
            return rc2; \
        if (!fConsumed2) \
            return xpidlParseError(pThis, pInput, NULL, VERR_INVALID_PARAMETER, "Parser: Expected punctuator '%c'", a_chPunctuator); \
    } while(0)


#define XPIDL_PARSE_OPTIONAL_PUNCTUATOR(a_fConsumed, a_chPunctuator) \
    do { \
        int rc2 = xpidlLexerConsumeIfPunctuator(pThis, pInput, a_chPunctuator, &a_fConsumed); \
        if (RT_FAILURE(rc2)) \
            return rc2; \
    } while(0)


#define XPIDL_PARSE_NATURAL(a_u64) \
    uint64_t a_u64 = 0; \
    do { \
        bool fConsumed2 = false; \
        int rc2 = xpidlLexerConsumeIfNatural(pThis, pInput, &a_u64, &fConsumed2); \
        if (RT_FAILURE(rc2)) \
            return rc2; \
        if (!fConsumed2) \
            return xpidlParseError(pThis, pInput, NULL, VERR_INVALID_PARAMETER, "Parser: Expected a natural number"); \
    } while(0)


static PXPIDLINPUT xpidlInputCreate(const char *pszFilename, PRTLISTANCHOR pLstIncludePaths)
{
    RTSCRIPTLEX hIdlLex = NULL;
    int rc = xpidlCreateLexerFromFilename(pszFilename, pLstIncludePaths, &hIdlLex);
    if (RT_FAILURE(rc))
        return NULL;

    PXPIDLINPUT pInput = (PXPIDLINPUT)xpidl_malloc(sizeof (*pInput));
    if (!pInput)
    {
        RTScriptLexDestroy(hIdlLex);
        return NULL;
    }

    RTListInit(&pInput->LstIncludes);
    pInput->hIdlLex     = hIdlLex;
    pInput->pszFilename = xpidl_strdup(pszFilename);
    pInput->pszBasename = RTPathFilename(pInput->pszFilename);
    return pInput;
}


static PXPIDLNODE xpidlNodeCreateWithAttrs(PXPIDLPARSE pThis, PXPIDLNODE pParent, PXPIDLINPUT pInput, XPIDLNDTYPE enmType,
                                           PXPIDLATTR paAttrs, uint32_t cAttrs)
{
    PXPIDLNODE pNode = (PXPIDLNODE)RTMemAllocZ(RT_UOFFSETOF_DYN(XPIDLNODE, aAttrs[cAttrs]));
    if (pNode)
    {
        pNode->pParent = pParent;
        pNode->pInput  = pInput;
        pNode->enmType = enmType;
        pNode->cAttrs  = cAttrs;
        switch (enmType)
        {
            case kXpidlNdType_Interface_Def:
                RTListInit(&pNode->u.If.LstBody);
                break;
            case kXpidlNdType_Method:
                RTListInit(&pNode->u.Method.LstParams);
                break;
            default:
                break;
        }

        if (cAttrs)
            memcpy(&pNode->aAttrs[0], &paAttrs[0], cAttrs * sizeof(pThis->aAttrs[0]));
    }
    else
        xpidlParseError(pThis, pInput, NULL, VERR_NO_MEMORY, "Failed to allocate node of type %u\n", enmType);

    return pNode;
}


static PXPIDLNODE xpidlNodeCreate(PXPIDLPARSE pThis, PXPIDLNODE pParent, PXPIDLINPUT pInput, XPIDLNDTYPE enmType)
{
    return xpidlNodeCreateWithAttrs(pThis, pParent, pInput, enmType, NULL /*paAttrs*/, 0 /*cAttrs*/);
}


static PCXPIDLNODE xpidlParseFindType(PXPIDLPARSE pThis, const char *pszName)
{
    PCXPIDLNODE pIfFwd = NULL;
    PCXPIDLNODE pIt;
    RTListForEach(&pThis->LstNodes, pIt, XPIDLNODE, NdLst)
    {
        switch (pIt->enmType)
        {
            case kXpidlNdType_Typedef:
                if (!strcmp(pszName, pIt->u.Typedef.pszName))
                    return pIt;
                break;
            case kXpidlNdType_Native:
                if (!strcmp(pszName, pIt->u.Native.pszName))
                    return pIt;
                break;
            case kXpidlNdType_Interface_Forward_Decl:
                if (!strcmp(pszName, pIt->u.pszIfFwdName))
                    pIfFwd = pIt; /* We will try finding the real definition before returning the forward declaration. */
                break;
            case kXpidlNdType_Interface_Def:
                if (!strcmp(pszName, pIt->u.If.pszIfName))
                    return pIt;
                break;
            default:
                break;
        }
    }

    return pIfFwd;
}


static int xpidlParseAttributes(PXPIDLPARSE pThis, PXPIDLINPUT pInput, PXPIDLATTR paAttrs, uint32_t cAttrsMax, uint32_t *pcAttrs)
{
    g_fParsingAttributes = true;
    uint32_t cAttrs = 0;
    for (;;)
    {
        bool fConsumed = false;
        const char *pszVal = NULL;

        XPIDL_PARSE_IDENTIFIER_ALLOW_KEYWORDS(pszAttr); /* For const for example. */
        XPIDL_PARSE_OPTIONAL_PUNCTUATOR(fConsumed, '(');
        if (fConsumed)
        {
            XPIDL_PARSE_IDENTIFIER_EXT(pszVal);
            g_fRequiredUuid = false;
            XPIDL_PARSE_PUNCTUATOR(')');
        }

        if (cAttrs == cAttrsMax)
            return xpidlParseError(pThis, pInput, NULL, VERR_INVALID_PARAMETER,
                                   "Too many attributes in attributes list, maximum is %u", cAttrsMax);

        paAttrs[cAttrs].pszName = pszAttr;
        paAttrs[cAttrs].pszVal  = pszVal;
        cAttrs++;

        /* No ',' means end of attribute list. */
        XPIDL_PARSE_OPTIONAL_PUNCTUATOR(fConsumed, ',');
        if (!fConsumed)
            break;
    }
    *pcAttrs = cAttrs;
    g_fParsingAttributes = false;

    XPIDL_PARSE_PUNCTUATOR(']');
    return VINF_SUCCESS;
}


static int xpidlParseTypeSpec(PXPIDLPARSE pThis, PXPIDLINPUT pInput, PXPIDLNODE *ppNode)
{
    /* Need a keyword or an identifier. */
    static const XPIDLKEYWORD g_aenmTypeKeywordsStart[] =
    {
        kXpidlKeyword_Void,
        kXpidlKeyword_Char,
        kXpidlKeyword_Wide_Char,
        kXpidlKeyword_Unsigned,
        kXpidlKeyword_Long,
        kXpidlKeyword_Short,
        kXpidlKeyword_Boolean,
        kXpidlKeyword_Octet,
        kXpidlKeyword_String,
        kXpidlKeyword_Wide_String,
        kXpidlKeyword_Double,
        kXpidlKeyword_Float,
        kXpidlKeyword_Invalid
    };

    int rc = VINF_SUCCESS;
    XPIDL_PARSE_OPTIONAL_KEYWORD_LIST(enmType, g_aenmTypeKeywordsStart, kXpidlKeyword_Invalid);
    if (enmType != kXpidlKeyword_Invalid)
    {
        XPIDLTYPE enmBaseType = kXpidlType_Invalid;

        /* Unsigned, and long has more to follow. */
        switch (enmType)
        {
            case kXpidlKeyword_Void:
                enmBaseType = kXpidlType_Void;
                break;
            case kXpidlKeyword_Char:
                enmBaseType = kXpidlType_Char;
                break;
            case kXpidlKeyword_Wide_Char:
                enmBaseType = kXpidlType_Wide_Char;
                break;
            case kXpidlKeyword_Unsigned:
            {
                /*
                 * Possibilities:
                 *     unsigned short
                 *     unsigned long
                 *     unsigned long long
                 */
                static const XPIDLKEYWORD g_aenmUnsignedKeywords[] =
                {
                    kXpidlKeyword_Long,
                    kXpidlKeyword_Short,
                    kXpidlKeyword_Invalid
                };

                XPIDL_PARSE_KEYWORD_LIST(enmUnsignedType, g_aenmUnsignedKeywords);
                switch (enmUnsignedType)
                {
                    case kXpidlKeyword_Long:
                    {
                        /* Another long following? */
                        XPIDL_PARSE_OPTIONAL_KEYWORD(fConsumed, kXpidlKeyword_Long);
                        if (fConsumed)
                            enmBaseType = kXpidlType_Unsigned_Long_Long;
                        else
                            enmBaseType = kXpidlType_Unsigned_Long;
                        break;
                    }
                    case kXpidlKeyword_Short:
                        enmBaseType = kXpidlType_Unsigned_Short;
                        break;
                    default:
                        AssertReleaseFailed(); /* Impossible */
                }
                break;
            }
            case kXpidlKeyword_Long:
            {
                /* Another long can follow. */
                XPIDL_PARSE_OPTIONAL_KEYWORD(fConsumed, kXpidlKeyword_Long);
                if (fConsumed)
                    enmBaseType = kXpidlType_Long_Long;
                else
                    enmBaseType = kXpidlType_Long;
                break;
            }
            case kXpidlKeyword_Short:
                enmBaseType = kXpidlType_Short;
                break;
            case kXpidlKeyword_Boolean:
                enmBaseType = kXpidlType_Boolean;
                break;
            case kXpidlKeyword_Octet:
                enmBaseType = kXpidlType_Octet;
                break;
            case kXpidlKeyword_String:
                enmBaseType = kXpidlType_String;
                break;
            case kXpidlKeyword_Wide_String:
                enmBaseType = kXpidlType_Wide_String;
                break;
            case kXpidlKeyword_Double:
                enmBaseType = kXpidlType_Double;
                break;
            case kXpidlKeyword_Float:
                enmBaseType = kXpidlType_Float;
                break;
            default:
                AssertReleaseFailed();
        }

        PXPIDLNODE pNode = xpidlNodeCreate(pThis, NULL, pInput, kXpidlNdType_BaseType);
        if (pNode)
        {
            pNode->u.enmBaseType = enmBaseType;
            *ppNode = pNode;
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
    {
        /* Identifier */
        XPIDL_PARSE_IDENTIFIER(pszName);
        PCXPIDLNODE pNdTypeRef = xpidlParseFindType(pThis, pszName);
        if (!pNdTypeRef)
            return xpidlParseError(pThis, pInput, NULL, VERR_NOT_FOUND, "Unknown referenced type '%s'\n", pszName);

        PXPIDLNODE pNode = xpidlNodeCreate(pThis, NULL, pInput, kXpidlNdType_Identifier);
        if (pNode)
        {
            /* Try resolving the referenced type. */
            pNode->pNdTypeRef = pNdTypeRef;
            pNode->u.pszIde   = pszName;
            *ppNode = pNode;
        }
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}


static int xpidlParseConst(PXPIDLPARSE pThis, PXPIDLINPUT pInput, PXPIDLNODE pNdIf)
{
    int rc;
    PXPIDLNODE pNdConst = xpidlNodeCreate(pThis, pNdIf, pInput, kXpidlNdType_Const);
    if (pNdConst)
    {
        RTListAppend(&pNdIf->u.If.LstBody, &pNdConst->NdLst);

        PXPIDLNODE pNdTypeSpec = NULL;
        rc = xpidlParseTypeSpec(pThis, pInput, &pNdTypeSpec);
        if (RT_FAILURE(rc))
            return rc;
        pNdConst->u.Const.pNdTypeSpec = pNdTypeSpec;

        XPIDL_PARSE_IDENTIFIER(pszName); /* The parameter name is always required. */
        pNdConst->u.Const.pszName = pszName;

        XPIDL_PARSE_PUNCTUATOR('=');
        XPIDL_PARSE_NATURAL(u64);
        pNdConst->u.Const.u64Const = u64;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


static int xpidlParseAttribute(PXPIDLPARSE pThis, PXPIDLINPUT pInput, PXPIDLNODE pNdIf, bool fReadonly)
{
    int rc;
    PXPIDLNODE pNdConst = xpidlNodeCreateWithAttrs(pThis, pNdIf, pInput, kXpidlNdType_Attribute,
                                                   &pThis->aAttrs[0], pThis->cAttrs);
    if (pNdConst)
    {
        pThis->cAttrs = 0;
        RTListAppend(&pNdIf->u.If.LstBody, &pNdConst->NdLst);

        PXPIDLNODE pNdTypeSpec = NULL;
        rc = xpidlParseTypeSpec(pThis, pInput, &pNdTypeSpec);
        if (RT_FAILURE(rc))
            return rc;
        pNdConst->u.Attribute.pNdTypeSpec = pNdTypeSpec;

        XPIDL_PARSE_IDENTIFIER(pszName); /* The parameter name is always required. */
        pNdConst->u.Attribute.pszName = pszName;
        pNdConst->u.Attribute.fReadonly = fReadonly;
    }
    else
        rc = VERR_NO_MEMORY;

    return rc;
}


static int xpidlParseMethodParameters(PXPIDLPARSE pThis, PXPIDLINPUT pInput, PXPIDLNODE pNdMethod)
{
    for (;;)
    {
        /* Each parameter can have an attribute list. */
        bool fConsumed = false;
        XPIDLATTR aAttrs[32];
        uint32_t cAttrs = 0;

        XPIDL_PARSE_OPTIONAL_PUNCTUATOR(fConsumed, '[');
        if (fConsumed)
        {
            int rc = xpidlParseAttributes(pThis, pInput, &aAttrs[0], RT_ELEMENTS(aAttrs), &cAttrs);
            if (RT_FAILURE(rc))
                return rc;
        }

        /* Direction specifier. */
        static const XPIDLKEYWORD g_aenmDirectionKeywords[] =
        {
            kXpidlKeyword_In,
            kXpidlKeyword_InOut,
            kXpidlKeyword_Out,
            kXpidlKeyword_Invalid
        };
        XPIDL_PARSE_KEYWORD_LIST(enmDirection, g_aenmDirectionKeywords);

        PXPIDLNODE pNdParam = xpidlNodeCreateWithAttrs(pThis, pNdMethod, pInput, kXpidlNdType_Parameter,
                                                       &aAttrs[0], cAttrs);
        if (pNdParam)
        {
            RTListAppend(&pNdMethod->u.Method.LstParams, &pNdParam->NdLst);

            PXPIDLNODE pNdTypeSpec = NULL;
            int rc = xpidlParseTypeSpec(pThis, pInput, &pNdTypeSpec);
            if (RT_FAILURE(rc))
                return rc;
            pNdTypeSpec->pParent          = pNdParam;
            pNdParam->u.Param.pNdTypeSpec = pNdTypeSpec;

            XPIDL_PARSE_IDENTIFIER(pszName); /* The parameter name is always required. */
            pNdParam->u.Param.pszName = pszName;
            switch (enmDirection)
            {
                case kXpidlKeyword_In:
                    pNdParam->u.Param.enmDir = kXpidlDirection_In;
                    break;
                case kXpidlKeyword_InOut:
                    pNdParam->u.Param.enmDir = kXpidlDirection_InOut;
                    break;
                case kXpidlKeyword_Out:
                    pNdParam->u.Param.enmDir = kXpidlDirection_Out;
                    break;
                default:
                    AssertReleaseFailed(); /* Impossible */
            }
        }
        else
            return VERR_NO_MEMORY;

        /* No ',' means end of attribute list. */
        XPIDL_PARSE_OPTIONAL_PUNCTUATOR(fConsumed, ',');
        if (!fConsumed)
            break;
    }

    XPIDL_PARSE_PUNCTUATOR(')');
    return VINF_SUCCESS;
}


static int xpidlParseInterfaceBody(PXPIDLPARSE pThis, PXPIDLINPUT pInput, PXPIDLNODE pNdIf)
{
    for (;;)
    {
        bool fRawBlock = false;
        int rc = xpidlParseSkipComments(pThis, pInput, &fRawBlock);
        if (RT_FAILURE(rc))
            return rc;

        if (fRawBlock)
        {
            PCRTSCRIPTLEXTOKEN pTok;
            rc = RTScriptLexQueryToken(pInput->hIdlLex, &pTok);
            if (RT_FAILURE(rc))
                return xpidlParseError(pThis, pInput, NULL, rc, "Parser: Failed to query punctuator token with %Rrc", rc);

            size_t cchIntro =   !strncmp(pTok->Type.Comment.pszComment, "%{C++", sizeof("%{C++") - 1)
                              ? 6  /* Assumes a newline after %{C++ */
                              : 7; /* Assumes a newline after %{C++ */
            /* Create a new raw block node. */
            PXPIDLNODE pNode = xpidlNodeCreate(pThis, NULL, pInput, kXpidlNdType_RawBlock);
            if (pNode)
            {
                pNode->u.RawBlock.pszRaw = pTok->Type.Comment.pszComment + cchIntro;
                pNode->u.RawBlock.cchRaw = pTok->Type.Comment.cchComment - (cchIntro + 2 + 1); /* Start + end + zero terminator. */
                RTScriptLexConsumeToken(pInput->hIdlLex);
                RTListAppend(&pNdIf->u.If.LstBody, &pNode->NdLst);
            }
            else
                return xpidlParseError(pThis, pInput, pTok, VERR_NO_MEMORY, "Parser: Failed to allocate memory for raw block AST node");
        }

        /* A closing '}' means we reached the end of the interface body. */
        bool fConsumed = false;
        XPIDL_PARSE_OPTIONAL_PUNCTUATOR(fConsumed, '}');
        if (fConsumed)
            break;

        XPIDL_PARSE_OPTIONAL_PUNCTUATOR(fConsumed, '[');
        if (fConsumed)
        {
            if (pThis->cAttrs)
                return xpidlParseError(pThis, pInput, NULL, VERR_INVALID_PARAMETER,
                                       "Start of attribute list directly after an existing attribute list");

            rc = xpidlParseAttributes(pThis, pInput, &pThis->aAttrs[0], RT_ELEMENTS(pThis->aAttrs), &pThis->cAttrs);
        }
        if (RT_FAILURE(rc))
            break;

        /*
         * Select one of the following possibilities:
         *     readonly attribute <type spec> <name>;
         *     attribute <type spec> <name>;
         *     const <type spec> <name> = <value>;
         *     <type spec> <name> (...);
         */
        static const XPIDLKEYWORD g_aenmBodyKeywords[] =
        {
            kXpidlKeyword_Readonly,
            kXpidlKeyword_Attribute,
            kXpidlKeyword_Const,
            kXpidlKeyword_Invalid
        };

        XPIDL_PARSE_OPTIONAL_KEYWORD_LIST(enmStart, g_aenmBodyKeywords, kXpidlKeyword_Invalid);
        if (enmStart != kXpidlKeyword_Invalid)
        {
            if (enmStart == kXpidlKeyword_Const)
                rc = xpidlParseConst(pThis, pInput, pNdIf);
            else if (enmStart == kXpidlKeyword_Readonly)
            {
                XPIDL_PARSE_KEYWORD(kXpidlKeyword_Attribute, "attribute");
                rc = xpidlParseAttribute(pThis, pInput, pNdIf, true /*fReadonly*/);
            }
            else
            {
                Assert(enmStart == kXpidlKeyword_Attribute);
                rc = xpidlParseAttribute(pThis, pInput, pNdIf, false /*fReadonly*/);
            }
        }
        else
        {
            /* We need to parse a type spec. */
            PXPIDLNODE pNdRetType = NULL;

            rc = xpidlParseTypeSpec(pThis, pInput, &pNdRetType);
            if (RT_FAILURE(rc))
                return rc;

            PXPIDLNODE pNdMethod = xpidlNodeCreateWithAttrs(pThis, pNdIf, pInput, kXpidlNdType_Method,
                                                            &pThis->aAttrs[0], pThis->cAttrs);
            if (pNdMethod)
            {
                pThis->cAttrs = 0;
                RTListAppend(&pNdIf->u.If.LstBody, &pNdMethod->NdLst);

                pNdMethod->u.Method.pNdTypeSpecRet = pNdRetType;
                XPIDL_PARSE_IDENTIFIER(pszName); /* The method name is always required. */
                pNdMethod->u.Method.pszName = pszName;
                XPIDL_PARSE_PUNCTUATOR('(');

                XPIDL_PARSE_OPTIONAL_PUNCTUATOR(fConsumed, ')');
                if (!fConsumed)
                {
                    /* Parse the parameter spec. */
                    rc = xpidlParseMethodParameters(pThis, pInput, pNdMethod);
                    if (RT_FAILURE(rc))
                        return rc;
                }
            }
            else
                return VERR_NO_MEMORY;
        }
        if (RT_FAILURE(rc))
            return rc;

        XPIDL_PARSE_PUNCTUATOR(';');
    }

    XPIDL_PARSE_PUNCTUATOR(';');
    return VINF_SUCCESS;
}


static int xpidlParseInterface(PXPIDLPARSE pThis, PXPIDLINPUT pInput, PXPIDLNODE pParent)
{
    /*
     * We only support parsing a subset of what is actually possible:
     *     - Forward declarations
     *     - Actual interface definitions with at most a single parent inheriting from
     */
    XPIDL_PARSE_IDENTIFIER(pszName); /* The interface name is always required. */
    bool fConsumed = false;
    XPIDL_PARSE_OPTIONAL_PUNCTUATOR(fConsumed, ';');

    int rc = VINF_SUCCESS;
    if (!fConsumed)
    {
        const char *pszIfInherit = NULL;
        XPIDL_PARSE_OPTIONAL_PUNCTUATOR(fConsumed, ':');
        if (fConsumed)
            XPIDL_PARSE_IDENTIFIER_EXT(pszIfInherit);
        XPIDL_PARSE_PUNCTUATOR('{');

        PCXPIDLNODE pNdTypeRef = NULL;
        if (pszIfInherit)
        {
            pNdTypeRef = xpidlParseFindType(pThis, pszIfInherit);
            if (!pNdTypeRef)
                return xpidlParseError(pThis, pInput, NULL, VERR_NOT_FOUND, "Unknown referenced type '%s'\n", pszName);
        }

        /* Now for the fun part, parsing the body of the interface. */
        PXPIDLNODE pNode = xpidlNodeCreateWithAttrs(pThis, pParent, pInput, kXpidlNdType_Interface_Def,
                                                    &pThis->aAttrs[0], pThis->cAttrs);
        if (pNode)
        {
            pThis->cAttrs = 0;

            pNode->pNdTypeRef        = pNdTypeRef;
            pNode->u.If.pszIfName    = pszName;
            pNode->u.If.pszIfInherit = pszIfInherit;
            RTListAppend(&pThis->LstNodes, &pNode->NdLst);

            rc = xpidlParseInterfaceBody(pThis, pInput, pNode);
        }
        else
            rc = VERR_NO_MEMORY;

    }
    else
    {
        /* That was easy, just a forward declaration. */
        PXPIDLNODE pNode = xpidlNodeCreate(pThis, pParent, pInput, kXpidlNdType_Interface_Forward_Decl);
        if (pNode)
        {
            pNode->u.pszIfFwdName = pszName;
            RTListAppend(&pThis->LstNodes, &pNode->NdLst);
        }
        else
            rc = VERR_NO_MEMORY;
    }

    return rc;
}


static int xpidlParseKeyword(PXPIDLPARSE pThis, PXPIDLINPUT pInput, PXPIDLNODE pParent, PRTLISTANCHOR pLstIncludePaths,
                             PCRTSCRIPTLEXTOKMATCH pKeyword)
{
    RT_NOREF(pThis, pInput, pLstIncludePaths);
    int rc = VINF_SUCCESS;
    switch (pKeyword->u64Val)
    {
        case kXpidlKeyword_Include:
        {
            XPIDL_PARSE_STRING_LIT(pszFilename);
            /* Check whether this was parsed already. */
            PCXPIDLINPUT pIt;
            RTListForEach(&pThis->LstInputs, pIt, XPIDLINPUT, NdInput)
            {
                if (!strcmp(pIt->pszFilename, pszFilename))
                    return VINF_SUCCESS;
            }

            PXPIDLINPUT pInputNew = xpidlInputCreate(pszFilename, pLstIncludePaths);
            if (!pInputNew)
                return xpidlParseError(pThis, pInput, NULL, VERR_INVALID_PARAMETER, "Failed opening include file '%s'",
                                       pszFilename);

            RTListAppend(&pInput->LstIncludes, &pInputNew->NdInclude);
            RTListAppend(&pThis->LstInputs, &pInputNew->NdInput);
            rc = xpidlParseIdl(pThis, pInputNew, pLstIncludePaths);
            break;
        }
        case kXpidlKeyword_Typedef:
        {
            PXPIDLNODE pNdTypeSpec = NULL;
            rc = xpidlParseTypeSpec(pThis, pInput, &pNdTypeSpec);
            if (RT_FAILURE(rc))
                break;
            XPIDL_PARSE_IDENTIFIER(pszName);
            XPIDL_PARSE_PUNCTUATOR(';');

            PXPIDLNODE pNode = xpidlNodeCreate(pThis, pParent, pInput, kXpidlNdType_Typedef);
            if (pNode)
            {
                pNdTypeSpec->pParent = pNode;

                pNode->u.Typedef.pNodeTypeSpec = pNdTypeSpec;
                pNode->u.Typedef.pszName       = pszName;
                RTListAppend(&pThis->LstNodes, &pNode->NdLst);
            }
            else
                rc = VERR_NO_MEMORY;
            break;
        }
        case kXpidlKeyword_Native:
        {
            XPIDL_PARSE_IDENTIFIER(pszName);
            XPIDL_PARSE_PUNCTUATOR('(');
            XPIDL_PARSE_IDENTIFIER_ALLOW_KEYWORDS(pszNative); /* char is a keyword but also allowed */
            XPIDL_PARSE_PUNCTUATOR(')');
            XPIDL_PARSE_PUNCTUATOR(';');

            PXPIDLNODE pNode = xpidlNodeCreateWithAttrs(pThis, pParent, pInput, kXpidlNdType_Native,
                                                        &pThis->aAttrs[0], pThis->cAttrs);
            if (pNode)
            {
                pThis->cAttrs = 0;

                pNode->u.Native.pszName   = pszName;
                pNode->u.Native.pszNative = pszNative;
                RTListAppend(&pThis->LstNodes, &pNode->NdLst);
            }
            else
                rc = VERR_NO_MEMORY;
            break;
        }
        case kXpidlKeyword_Interface:
            rc = xpidlParseInterface(pThis, pInput, pParent);
            break;
        default:
            rc = xpidlParseError(pThis, pInput, NULL, VERR_INVALID_PARAMETER, "Unexpected keyword '%s' found",
                                 pKeyword->pszMatch);
    }
    return rc;
}


static int xpidlParseIdl(PXPIDLPARSE pThis, PXPIDLINPUT pInput, PRTLISTANCHOR pLstIncludePaths)
{
    /* Parse IDL file. */
    int rc;
    for (;;)
    {
        PCRTSCRIPTLEXTOKEN pTok;
        rc = RTScriptLexQueryToken(pInput->hIdlLex, &pTok);
        if (RT_FAILURE(rc))
            return xpidlParseError(pThis, pInput, NULL, rc, "Parser: Failed to query next token with %Rrc", rc);

        if (pTok->enmType == RTSCRIPTLEXTOKTYPE_EOS)
            break;

        /*
         * In this outer loop we can either get comments, keywords or [] for
         * attributes of following nodes.
         */
        switch (pTok->enmType)
        {
            case RTSCRIPTLEXTOKTYPE_COMMENT_SINGLE_LINE:
                RTScriptLexConsumeToken(pInput->hIdlLex); /* These get ignored entirely. */
                break;
            case RTSCRIPTLEXTOKTYPE_COMMENT_MULTI_LINE:
            {
                /* Could be a raw block, check that the string starts with %{C++. */
                if (   !strncmp(pTok->Type.Comment.pszComment, "%{C++", sizeof("%{C++") - 1)
                    || !strncmp(pTok->Type.Comment.pszComment, "%{ C++", sizeof("%{ C++") - 1))
                {
                    size_t cchIntro =   !strncmp(pTok->Type.Comment.pszComment, "%{C++", sizeof("%{C++") - 1)
                                      ? 6  /* Assumes a newline after %{C++ */
                                      : 7; /* Assumes a newline after %{C++ */
                    /* Create a new raw block node. */
                    PXPIDLNODE pNode = xpidlNodeCreate(pThis, NULL, pInput, kXpidlNdType_RawBlock);
                    if (pNode)
                    {
                        pNode->u.RawBlock.pszRaw = pTok->Type.Comment.pszComment + cchIntro;
                        pNode->u.RawBlock.cchRaw = pTok->Type.Comment.cchComment - (cchIntro + 2 + 1); /* Start + end + zero terminator. */
                        RTListAppend(&pThis->LstNodes, &pNode->NdLst);
                    }
                    else
                        rc = VERR_NO_MEMORY;
                }
                /* else: Regular multi line comment, gets ignored. */
                RTScriptLexConsumeToken(pInput->hIdlLex);
                break;
            }
            case RTSCRIPTLEXTOKTYPE_KEYWORD:
            {
                PCRTSCRIPTLEXTOKMATCH pKeyword = pTok->Type.Keyword.pKeyword;
                RTScriptLexConsumeToken(pInput->hIdlLex);
                rc = xpidlParseKeyword(pThis, pInput, NULL, pLstIncludePaths, pKeyword); /** @todo This allows too much */
                break;
            }
            case RTSCRIPTLEXTOKTYPE_PUNCTUATOR:
            {
                if (pTok->Type.Punctuator.pPunctuator->u64Val == '[')
                {
                    RTScriptLexConsumeToken(pInput->hIdlLex);

                    if (pThis->cAttrs)
                        return xpidlParseError(pThis, pInput, NULL, VERR_INVALID_PARAMETER,
                                               "Start of attribute list directly after an existing attribute list");

                    rc = xpidlParseAttributes(pThis, pInput, &pThis->aAttrs[0], RT_ELEMENTS(pThis->aAttrs), &pThis->cAttrs);
                }
                else
                    rc = xpidlParseError(pThis, pInput, pTok, VERR_INVALID_PARAMETER, "Unexpected punctuator found, expected '[', got '%c'",
                                         (char)pTok->Type.Punctuator.pPunctuator->u64Val);
                break;
            }
            case RTSCRIPTLEXTOKTYPE_ERROR:
                rc = xpidlParseError(pThis, pInput, pTok, VERR_INTERNAL_ERROR, "Internal lexer error: %s", pTok->Type.Error.pErr->pszMsg);
                break;
            default:
                rc = xpidlParseError(pThis, pInput, pTok, VERR_INVALID_PARAMETER, "Unexpected keyword found, expected raw block, keyword or '['");
                break;
        }

        if (RT_FAILURE(rc))
            break;
    }

    return rc;
}


int xpidl_process_idl(char *filename, PRTLISTANCHOR pLstIncludePaths,
                      char *file_basename, ModeData *mode)
{
    XPIDLPARSE ParseState;
    RTListInit(&ParseState.LstInputs);
    RTListInit(&ParseState.LstNodes);
    RTErrInfoInitStatic(&ParseState.ErrInfo);
    ParseState.cAttrs    = 0;

    PXPIDLINPUT pInput = xpidlInputCreate(filename, pLstIncludePaths);
    if (!pInput)
        return VERR_NO_MEMORY;

    RTListAppend(&ParseState.LstInputs, &pInput->NdInput);
    int rc = xpidlParseIdl(&ParseState, pInput, pLstIncludePaths);
    if (RT_SUCCESS(rc))
    {
        char *tmp, *outname, *real_outname = NULL;

        pInput->pszBasename = xpidl_strdup(filename);

        /* if basename has an .extension, truncate it. */
        tmp = strrchr(pInput->pszBasename, '.');
        if (tmp)
            *tmp = '\0';

        if (!file_basename)
            outname = xpidl_strdup(pInput->pszBasename);
        else
            outname = xpidl_strdup(file_basename);

        FILE *pFile = NULL;
        if (strcmp(outname, "-"))
        {
            const char *fopen_mode;
            char *out_basename;

            /* explicit_output_filename can't be true without a filename */
            if (explicit_output_filename) {
                real_outname = xpidl_strdup(outname);
            } else {

                if (!file_basename) {
                    out_basename = RTPathFilename(outname);
                } else {
                    out_basename = outname;
                }

                rc = RTStrAPrintf(&real_outname, "%s.%s", out_basename, mode->suffix);
                if (RT_FAILURE(rc))
                    return rc;

                if (out_basename != outname)
                    free(out_basename);
            }

            /* Use binary write for typelib mode */
            fopen_mode = (strcmp(mode->mode, "typelib")) ? "w" : "wb";
            pFile = fopen(real_outname, fopen_mode);
            if (!pFile) {
                perror("error opening output file");
                free(outname);
                return VERR_INVALID_PARAMETER;
            }
        }
        else
            pFile = stdout;

        rc = mode->dispatch(pFile, pInput, &ParseState, &ParseState.ErrInfo.Core);

        if (pFile != stdout)
            fclose(pFile);
        free(outname);
    }

    if (RT_FAILURE(rc))
        RTMsgError(ParseState.ErrInfo.Core.pszMsg);

    return rc;
}
