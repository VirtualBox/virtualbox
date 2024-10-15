/* $Id: VDScript.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox HDD container test utility - scripting engine.
 */

/*
 * Copyright (C) 2013-2024 Oracle and/or its affiliates.
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

/** @page pg_vd_script   VDScript - Simple scripting language for VD I/O testing.
 *
 * This component implements a very simple scripting language to make testing the VD
 * library more flexible and testcases faster to implement without the need to recompile
 * everything after it changed.
 * The language is a small subset of the C language. It doesn't support unions, structs,
 * global variables, typedefed types or pointers (yet). It also adds a boolean and a string type.
 * Strings are immutable and only to print messages from the script.
 * There are also not the default types like int or unsigned because theire ranges are architecture
 * dependent. Instead VDScript uses uint8_t, int8_t, ... as primitive types.
 *
 * Why inventing a completely new language?
 *
 * Well it is not a completely new language to start with, it is a subset of C and the
 * language can be extended later on to reach the full C language later on.
 * Second, there is no static typed scripting language I like which could be implemented
 * and finally because I can ;)
 * The code implementing the scripting engine is designed to be easily incorporated into other
 * code. Could be used as a scripting language for the VBox debugger for example or in the scm
 * tool to automatically rewrite C code using the AST VDSCript generates...
 *
 * The syntax of VDSCript is derived from the C syntax. The syntax of C in BNF was taken
 * from: http://www.csci.csusb.edu/dick/samples/c.syntax.html
 * and: http://slps.github.com/zoo/c/iso-9899-tc3.html
 * and: http://www.open-std.org/jtc1/sc22/WG14/www/docs/n1256.pdf
 */

#define LOGGROUP LOGGROUP_DEFAULT
#include <iprt/ctype.h>
#include <iprt/errcore.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/stream.h>
#include <iprt/string.h>

#include <VBox/log.h>

#include "VDScriptAst.h"
#include "VDScriptInternal.h"

/**
 * VD script token class.
 */
typedef enum VDTOKENCLASS
{
    /** Invalid. */
    VDTOKENCLASS_INVALID = 0,
    /** Identifier class. */
    VDTOKENCLASS_IDENTIFIER,
    /** Numerical constant. */
    VDTOKENCLASS_NUMCONST,
    /** String constant. */
    VDTOKENCLASS_STRINGCONST,
    /** Operators */
    VDTOKENCLASS_OPERATORS,
    /** Reserved keyword */
    VDTOKENCLASS_KEYWORD,
    /** Punctuator */
    VDTOKENCLASS_PUNCTUATOR,
    /** End of stream */
    VDTOKENCLASS_EOS,
    /** 32bit hack. */
    VDTOKENCLASS_32BIT_HACK = 0x7fffffff
} VDTOKENCLASS;
/** Pointer to a token class. */
typedef VDTOKENCLASS *PVDTOKENCLASS;

/**
 * Keyword types.
 */
typedef enum VDSCRIPTTOKENKEYWORD
{
    VDSCRIPTTOKENKEYWORD_INVALID = 0,
    VDSCRIPTTOKENKEYWORD_CONTINUE,
    VDSCRIPTTOKENKEYWORD_REGISTER,
    VDSCRIPTTOKENKEYWORD_RESTRICT,
    VDSCRIPTTOKENKEYWORD_VOLATILE,
    VDSCRIPTTOKENKEYWORD_TYPEDEF,
    VDSCRIPTTOKENKEYWORD_DEFAULT,
    VDSCRIPTTOKENKEYWORD_EXTERN,
    VDSCRIPTTOKENKEYWORD_STATIC,
    VDSCRIPTTOKENKEYWORD_RETURN,
    VDSCRIPTTOKENKEYWORD_SWITCH,
    VDSCRIPTTOKENKEYWORD_STRUCT,
    VDSCRIPTTOKENKEYWORD_WHILE,
    VDSCRIPTTOKENKEYWORD_BREAK,
    VDSCRIPTTOKENKEYWORD_CONST,
    VDSCRIPTTOKENKEYWORD_FALSE,
    VDSCRIPTTOKENKEYWORD_TRUE,
    VDSCRIPTTOKENKEYWORD_ELSE,
    VDSCRIPTTOKENKEYWORD_CASE,
    VDSCRIPTTOKENKEYWORD_AUTO,
    VDSCRIPTTOKENKEYWORD_FOR,
    VDSCRIPTTOKENKEYWORD_IF,
    VDSCRIPTTOKENKEYWORD_DO,
    VDSCRIPTTOKENKEYWORD_32BIT_HACK = 0x7fffffff
} VDSCRIPTTOKENKEYWORD;
/** Pointer to a keyword type. */
typedef VDSCRIPTTOKENKEYWORD *PVDSCRIPTTOKENKEYWORD;

/**
 * VD script token.
 */
typedef struct VDSCRIPTTOKEN
{
    /** Token class. */
    VDTOKENCLASS enmClass;
    /** Token position in the source buffer. */
    VDSRCPOS     Pos;
    /** Data based on the token class. */
    union
    {
        /** Identifier. */
        struct
        {
            /** Pointer to the start of the identifier. */
            const char *pszIde;
            /** Number of characters for the identifier excluding the null terminator. */
            size_t      cchIde;
        } Ide;
        /** Numerical constant. */
        struct
        {
            uint64_t u64;
        } NumConst;
        /** String constant */
        struct
        {
            /** Pointer to the start of the string constant. */
            const char *pszString;
            /** Number of characters of the string, including the null terminator. */
            size_t      cchString;
        } StringConst;
        /** Operator */
        struct
        {
            /** The operator string. */
            char aszOp[4]; /** Maximum of 3 for >>= + null terminator. */
        } Operator;
        /** Keyword. */
        struct
        {
            /** The keyword type. */
            VDSCRIPTTOKENKEYWORD enmKeyword;
        } Keyword;
        /** Punctuator. */
        struct
        {
            /** The punctuator in question. */
            char                 chPunctuator;
        } Punctuator;
    } Class;
} VDSCRIPTTOKEN;
/** Pointer to a script token. */
typedef VDSCRIPTTOKEN *PVDSCRIPTTOKEN;
/** Pointer to a const script token. */
typedef const VDSCRIPTTOKEN *PCVDSCRIPTTOKEN;

/**
 * Tokenizer state.
 */
typedef struct VDTOKENIZER
{
    /** Char buffer to read from. */
    const char    *pszInput;
    /** Current position ininput buffer. */
    VDSRCPOS       Pos;
    /** Token 1. */
    VDSCRIPTTOKEN  Token1;
    /** Token 2. */
    VDSCRIPTTOKEN  Token2;
    /** Pointer to the current active token. */
    PVDSCRIPTTOKEN pTokenCurr;
    /** The next token in the input stream (used for peeking). */
    PVDSCRIPTTOKEN pTokenNext;
} VDTOKENIZER;

/**
 * Operators entry.
 */
typedef struct VDSCRIPTOP
{
    /** Operator string. */
    const char *pszOp;
    /** Size of the operator in characters without zero terminator. */
    size_t      cchOp;
} VDSCRIPTOP;
/** Pointer to a script operator. */
typedef VDSCRIPTOP *PVDSCRIPTOP;

/**
 * Known operators array, sort from higest character count to lowest.
 */
static VDSCRIPTOP g_aScriptOps[] =
{
    {">>=", 3},
    {"<<=", 3},
    {"+=",  2},
    {"-=",  2},
    {"/=",  2},
    {"%=",  2},
    {"&=",  2},
    {"|=",  2},
    {"^=",  2},
    {"&&",  2},
    {"||",  2},
    {"<<",  2},
    {">>",  2},
    {"++",  2},
    {"--",  2},
    {"==",  2},
    {"!=",  2},
    {">=",  2},
    {"<=",  2},
    {"->",  2},
    {"=",   1},
    {"+",   1},
    {"-",   1},
    {"*",   1},
    {"/",   1},
    {"%",   1},
    {"|",   1},
    {"&",   1},
    {"^",   1},
    {"<",   1},
    {">",   1},
    {"!",   1},
    {"~",   1},
    {".",   1}
};

/**
 * Known punctuators.
 */
static VDSCRIPTOP g_aScriptPunctuators[] =
{
    {"(", 1},
    {")", 1},
    {"{", 1},
    {"}", 1},
    {",", 1},
    {";", 1},
};

/**
 * Keyword entry.
 */
typedef struct VDSCRIPTKEYWORD
{
    /** Keyword string. */
    const char          *pszKeyword;
    /** Size of the string in characters without zero terminator. */
    size_t               cchKeyword;
    /** Keyword type. */
    VDSCRIPTTOKENKEYWORD enmKeyword;
} VDSCRIPTKEYWORD;
/** */
typedef VDSCRIPTKEYWORD *PVDSCRIPTKEYWORD;

/**
 * Known keywords.
 */
static VDSCRIPTKEYWORD g_aKeywords[] =
{
    {RT_STR_TUPLE("continue"), VDSCRIPTTOKENKEYWORD_CONTINUE},
    {RT_STR_TUPLE("register"), VDSCRIPTTOKENKEYWORD_REGISTER},
    {RT_STR_TUPLE("restrict"), VDSCRIPTTOKENKEYWORD_RESTRICT},
    {RT_STR_TUPLE("volatile"), VDSCRIPTTOKENKEYWORD_VOLATILE},
    {RT_STR_TUPLE("typedef"),  VDSCRIPTTOKENKEYWORD_TYPEDEF},
    {RT_STR_TUPLE("default"),  VDSCRIPTTOKENKEYWORD_DEFAULT},
    {RT_STR_TUPLE("extern"),   VDSCRIPTTOKENKEYWORD_EXTERN},
    {RT_STR_TUPLE("static"),   VDSCRIPTTOKENKEYWORD_STATIC},
    {RT_STR_TUPLE("return"),   VDSCRIPTTOKENKEYWORD_RETURN},
    {RT_STR_TUPLE("switch"),   VDSCRIPTTOKENKEYWORD_SWITCH},
    {RT_STR_TUPLE("struct"),   VDSCRIPTTOKENKEYWORD_STRUCT},
    {RT_STR_TUPLE("while"),    VDSCRIPTTOKENKEYWORD_WHILE},
    {RT_STR_TUPLE("break"),    VDSCRIPTTOKENKEYWORD_BREAK},
    {RT_STR_TUPLE("const"),    VDSCRIPTTOKENKEYWORD_CONST},
    {RT_STR_TUPLE("false"),    VDSCRIPTTOKENKEYWORD_FALSE},
    {RT_STR_TUPLE("true"),     VDSCRIPTTOKENKEYWORD_TRUE},
    {RT_STR_TUPLE("else"),     VDSCRIPTTOKENKEYWORD_ELSE},
    {RT_STR_TUPLE("case"),     VDSCRIPTTOKENKEYWORD_CASE},
    {RT_STR_TUPLE("auto"),     VDSCRIPTTOKENKEYWORD_AUTO},
    {RT_STR_TUPLE("for"),      VDSCRIPTTOKENKEYWORD_FOR},
    {RT_STR_TUPLE("if"),       VDSCRIPTTOKENKEYWORD_IF},
    {RT_STR_TUPLE("do"),       VDSCRIPTTOKENKEYWORD_DO}
};

static int vdScriptParseCompoundStatement(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTSTMT *ppAstNodeCompound);
static int vdScriptParseStatement(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTSTMT *ppAstNodeStmt);
static int vdScriptParseExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr);
static int vdScriptParseAssignmentExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr);
static int vdScriptParseCastExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr);
#if 0 /* unused */
static int vdScriptParseConstExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr);
#endif

/**
 * Returns whether the tokenizer reached the end of the stream.
 *
 * @returns true if the tokenizer reached the end of stream marker
 *          false otherwise.
 * @param   pTokenizer    The tokenizer state.
 */
DECLINLINE(bool) vdScriptTokenizerIsEos(PVDTOKENIZER pTokenizer)
{
    return *pTokenizer->pszInput == '\0';
}

/**
 * Skip one character in the input stream.
 *
 * @param   pTokenizer    The tokenizer state.
 */
DECLINLINE(void) vdScriptTokenizerSkipCh(PVDTOKENIZER pTokenizer)
{
    pTokenizer->pszInput++;
    pTokenizer->Pos.iChStart++;
    pTokenizer->Pos.iChEnd++;
}

/**
 * Returns the next char in the input buffer without advancing it.
 *
 * @returns Next character in the input buffer.
 * @param   pTokenizer    The tokenizer state.
 */
DECLINLINE(char) vdScriptTokenizerPeekCh(PVDTOKENIZER pTokenizer)
{
    return   vdScriptTokenizerIsEos(pTokenizer)
           ? '\0'
           : *(pTokenizer->pszInput + 1);
}

/**
 * Returns the next character in the input buffer advancing the internal
 * position.
 *
 * @returns Next character in the stream.
 * @param   pTokenizer     The tokenizer state.
 */
DECLINLINE(char) vdScriptTokenizerGetCh(PVDTOKENIZER pTokenizer)
{
    char ch;

    if (vdScriptTokenizerIsEos(pTokenizer))
        ch = '\0';
    else
        ch = *pTokenizer->pszInput;

    return ch;
}

/**
 * Sets a new line for the tokenizer.
 *
 * @param   pTokenizer    The tokenizer state.
 */
DECLINLINE(void) vdScriptTokenizerNewLine(PVDTOKENIZER pTokenizer, unsigned cSkip)
{
    pTokenizer->pszInput += cSkip;
    pTokenizer->Pos.iLine++;
    pTokenizer->Pos.iChStart = 1;
    pTokenizer->Pos.iChEnd   = 1;
}

/**
 * Checks whether the current position in the input stream is a new line
 * and skips it.
 *
 * @returns Flag whether there was a new line at the current position
 *          in the input buffer.
 * @param   pTokenizer    The tokenizer state.
 */
DECLINLINE(bool) vdScriptTokenizerIsSkipNewLine(PVDTOKENIZER pTokenizer)
{
    bool fNewline = true;

    if (   vdScriptTokenizerGetCh(pTokenizer) == '\r'
        && vdScriptTokenizerPeekCh(pTokenizer) == '\n')
        vdScriptTokenizerNewLine(pTokenizer, 2);
    else if (vdScriptTokenizerGetCh(pTokenizer) == '\n')
        vdScriptTokenizerNewLine(pTokenizer, 1);
    else
        fNewline = false;

    return fNewline;
}

/**
 * Skips a multi line comment.
 *
 * @param   pTokenizer    The tokenizer state.
 */
DECLINLINE(void) vdScriptTokenizerSkipComment(PVDTOKENIZER pTokenizer)
{
    while (   !vdScriptTokenizerIsEos(pTokenizer)
           && (   vdScriptTokenizerGetCh(pTokenizer) != '*'
               || vdScriptTokenizerPeekCh(pTokenizer) != '/'))
    {
        if (!vdScriptTokenizerIsSkipNewLine(pTokenizer))
            vdScriptTokenizerSkipCh(pTokenizer);
    }

    if (!vdScriptTokenizerIsEos(pTokenizer))
        vdScriptTokenizerSkipCh(pTokenizer);
    if (!vdScriptTokenizerIsEos(pTokenizer))
        vdScriptTokenizerSkipCh(pTokenizer);
}

/**
 * Skip all whitespace starting from the current input buffer position.
 * Skips all present comments too.
 *
 * @param   pTokenizer    The tokenizer state.
 */
DECLINLINE(void) vdScriptTokenizerSkipWhitespace(PVDTOKENIZER pTokenizer)
{
    while (!vdScriptTokenizerIsEos(pTokenizer))
    {
        while (   vdScriptTokenizerGetCh(pTokenizer) == ' '
               || vdScriptTokenizerGetCh(pTokenizer) == '\t')
            vdScriptTokenizerSkipCh(pTokenizer);

        if (   !vdScriptTokenizerIsEos(pTokenizer)
            && !vdScriptTokenizerIsSkipNewLine(pTokenizer))
        {
            if (   vdScriptTokenizerGetCh(pTokenizer) == '/'
                && vdScriptTokenizerPeekCh(pTokenizer) == '*')
            {
                vdScriptTokenizerSkipCh(pTokenizer);
                vdScriptTokenizerSkipCh(pTokenizer);
                vdScriptTokenizerSkipComment(pTokenizer);
            }
            else
                break; /* Skipped everything, next is some real content. */
        }
    }
}

/**
 * Get an identifier token from the tokenizer.
 *
 * @param   pTokenizer    The tokenizer state.
 * @param   pToken        The uninitialized token.
 */
static void vdScriptTokenizerGetIdeOrKeyword(PVDTOKENIZER pTokenizer, PVDSCRIPTTOKEN pToken)
{
    char ch;
    unsigned cchIde = 0;
    bool fIsKeyword = false;
    const char *pszIde = pTokenizer->pszInput;

    pToken->Pos      = pTokenizer->Pos;

    Assert(RT_C_IS_ALPHA(*pszIde) || *pszIde == '_' );

    do
    {
        cchIde++;
        vdScriptTokenizerSkipCh(pTokenizer);
        ch = vdScriptTokenizerGetCh(pTokenizer);
    }
    while (RT_C_IS_ALNUM(ch) || ch == '_');

    /* Check whether we got an identifier or an reserved keyword. */
    for (unsigned i = 0; i < RT_ELEMENTS(g_aKeywords); i++)
    {
        if (!RTStrNCmp(g_aKeywords[i].pszKeyword, pszIde, g_aKeywords[i].cchKeyword))
        {
            fIsKeyword = true;
            pToken->enmClass = VDTOKENCLASS_KEYWORD;
            pToken->Class.Keyword.enmKeyword = g_aKeywords[i].enmKeyword;
            break;
        }
    }

    if (!fIsKeyword)
    {
        pToken->enmClass = VDTOKENCLASS_IDENTIFIER;
        pToken->Class.Ide.pszIde = pszIde;
        pToken->Class.Ide.cchIde = cchIde;
    }
    pToken->Pos.iChEnd      += cchIde;
}

/**
 * Get a numerical constant from the tokenizer.
 *
 * @param   pTokenizer    The tokenizer state.
 * @param   pToken        The uninitialized token.
 */
static void vdScriptTokenizerGetNumberConst(PVDTOKENIZER pTokenizer, PVDSCRIPTTOKEN pToken)
{
    char *pszNext = NULL;

    Assert(RT_C_IS_DIGIT(vdScriptTokenizerGetCh(pTokenizer)));

    /* Let RTStrToUInt64Ex() do all the work, looks C compliant :). */
    pToken->enmClass = VDTOKENCLASS_NUMCONST;
    int rc = RTStrToUInt64Ex(pTokenizer->pszInput, &pszNext, 0, &pToken->Class.NumConst.u64);
    Assert(RT_SUCCESS(rc) || rc == VWRN_TRAILING_CHARS || rc == VWRN_TRAILING_SPACES); NOREF(rc);
    /** @todo Handle number to big, throw a warning */

    unsigned cchNumber = pszNext - pTokenizer->pszInput;
    for (unsigned i = 0; i < cchNumber; i++)
        vdScriptTokenizerSkipCh(pTokenizer);

    /* Check for a supported suffix, supported are K|M|G. */
    if (vdScriptTokenizerGetCh(pTokenizer) == 'K')
    {
        pToken->Class.NumConst.u64 *= _1K;
        vdScriptTokenizerSkipCh(pTokenizer);
    }
    else if (vdScriptTokenizerGetCh(pTokenizer) == 'M')
    {
        pToken->Class.NumConst.u64 *= _1M;
        vdScriptTokenizerSkipCh(pTokenizer);
    }
    else if (vdScriptTokenizerGetCh(pTokenizer) == 'G')
    {
        pToken->Class.NumConst.u64 *= _1G;
        vdScriptTokenizerSkipCh(pTokenizer);
    }
    else if (vdScriptTokenizerGetCh(pTokenizer) == 'T')
    {
        pToken->Class.NumConst.u64 *= _1T;
        vdScriptTokenizerSkipCh(pTokenizer);
    }
}

/**
 * Parses a string constant.
 *
 * @param   pTokenizer    The tokenizer state.
 * @param   pToken        The uninitialized token.
 *
 * @remarks No escape sequences allowed at this time.
 */
static void vdScriptTokenizerGetStringConst(PVDTOKENIZER pTokenizer, PVDSCRIPTTOKEN pToken)
{
    unsigned cchStr = 0;

    Assert(vdScriptTokenizerGetCh(pTokenizer) == '\"');
    vdScriptTokenizerSkipCh(pTokenizer); /* Skip " */

    pToken->enmClass = VDTOKENCLASS_STRINGCONST;
    pToken->Pos      = pTokenizer->Pos;
    pToken->Class.StringConst.pszString = pTokenizer->pszInput;

    while (vdScriptTokenizerGetCh(pTokenizer) != '\"')
    {
        cchStr++;
        vdScriptTokenizerSkipCh(pTokenizer);
    }

    vdScriptTokenizerSkipCh(pTokenizer); /* Skip closing " */

    pToken->Class.StringConst.cchString = cchStr;
    pToken->Pos.iChEnd                 += cchStr;
}

/**
 * Get the end of stream token.
 *
 * @param   pTokenizer    The tokenizer state.
 * @param   pToken        The uninitialized token.
 */
static void vdScriptTokenizerGetEos(PVDTOKENIZER pTokenizer, PVDSCRIPTTOKEN pToken)
{
    Assert(vdScriptTokenizerGetCh(pTokenizer) == '\0');

    pToken->enmClass = VDTOKENCLASS_EOS;
    pToken->Pos      = pTokenizer->Pos;
}

/**
 * Get operator or punctuator token.
 *
 * @param   pTokenizer    The tokenizer state.
 * @param   pToken        The uninitialized token.
 */
static void vdScriptTokenizerGetOperatorOrPunctuator(PVDTOKENIZER pTokenizer, PVDSCRIPTTOKEN pToken)
{
    bool fOpFound = false;

    pToken->enmClass = VDTOKENCLASS_INVALID;
    pToken->Pos      = pTokenizer->Pos;

    /*
     * Use table based approach here, not the fastest solution but enough for our purpose
     * for now.
     */
    for (unsigned i = 0; i < RT_ELEMENTS(g_aScriptOps); i++)
    {
        if (!RTStrNCmp(g_aScriptOps[i].pszOp, pTokenizer->pszInput, g_aScriptOps[i].cchOp))
        {
            memset(pToken->Class.Operator.aszOp, 0, sizeof(pToken->Class.Operator.aszOp));

            int rc = RTStrCopy(pToken->Class.Operator.aszOp, sizeof(pToken->Class.Operator.aszOp), g_aScriptOps[i].pszOp);
            AssertRC(rc);

            pToken->enmClass = VDTOKENCLASS_OPERATORS;
            pToken->Pos.iChEnd += (unsigned)g_aScriptOps[i].cchOp;

            /** @todo Make this prettier. */
            for (unsigned j = 0; j < g_aScriptOps[i].cchOp; j++)
                vdScriptTokenizerSkipCh(pTokenizer);
            fOpFound = true;
            break;
        }
    }

    if (!fOpFound)
    {
        for (unsigned i = 0; i < RT_ELEMENTS(g_aScriptPunctuators); i++)
        {
            if (!RTStrNCmp(g_aScriptPunctuators[i].pszOp, pTokenizer->pszInput, g_aScriptPunctuators[i].cchOp))
            {
                pToken->Pos.iChEnd += (unsigned)g_aScriptPunctuators[i].cchOp;
                pToken->enmClass = VDTOKENCLASS_PUNCTUATOR;
                pToken->Class.Punctuator.chPunctuator = *g_aScriptPunctuators[i].pszOp;

                vdScriptTokenizerSkipCh(pTokenizer);
                break;
            }
        }
    }
}

/**
 * Read the next token from the tokenizer stream.
 *
 * @param   pTokenizer    The tokenizer to read from.
 * @param   pToken        Uninitialized token to fill the token data into.
 */
static void vdScriptTokenizerReadNextToken(PVDTOKENIZER pTokenizer, PVDSCRIPTTOKEN pToken)
{
    /* Skip all eventually existing whitespace, newlines and comments first. */
    vdScriptTokenizerSkipWhitespace(pTokenizer);

    char ch = vdScriptTokenizerGetCh(pTokenizer);
    if (RT_C_IS_ALPHA(ch) || ch == '_')
        vdScriptTokenizerGetIdeOrKeyword(pTokenizer, pToken);
    else if (RT_C_IS_DIGIT(ch))
        vdScriptTokenizerGetNumberConst(pTokenizer, pToken);
    else if (ch == '\"')
        vdScriptTokenizerGetStringConst(pTokenizer, pToken);
    else if (ch == '\0')
        vdScriptTokenizerGetEos(pTokenizer, pToken);
    else
        vdScriptTokenizerGetOperatorOrPunctuator(pTokenizer, pToken);
}

/**
 * Create a new tokenizer.
 *
 * @returns Pointer to the new tokenizer state on success.
 *          NULL if out of memory.
 * @param   pszInput    The input to create the tokenizer for.
 */
static PVDTOKENIZER vdScriptTokenizerCreate(const char *pszInput)
{
    PVDTOKENIZER pTokenizer = (PVDTOKENIZER)RTMemAllocZ(sizeof(VDTOKENIZER));
    if (pTokenizer)
    {
        pTokenizer->pszInput     = pszInput;
        pTokenizer->Pos.iLine    = 1;
        pTokenizer->Pos.iChStart = 1;
        pTokenizer->Pos.iChEnd   = 1;
        pTokenizer->pTokenCurr   = &pTokenizer->Token1;
        pTokenizer->pTokenNext   = &pTokenizer->Token2;
        /* Fill the tokenizer with two first tokens. */
        vdScriptTokenizerReadNextToken(pTokenizer, pTokenizer->pTokenCurr);
        vdScriptTokenizerReadNextToken(pTokenizer, pTokenizer->pTokenNext);
    }

    return pTokenizer;
}

#if 0 /** @todo unused */
/**
 * Destroys a given tokenizer state.
 *
 * @param   pTokenizer    The tokenizer to destroy.
 */
static void vdScriptTokenizerDestroy(PVDTOKENIZER pTokenizer)
{
    RTMemFree(pTokenizer);
}
#endif

/**
 * Get the current token in the input stream.
 *
 * @returns Pointer to the next token in the stream.
 * @param   pTokenizer    The tokenizer to destroy.
 */
DECLINLINE(PCVDSCRIPTTOKEN) vdScriptTokenizerGetToken(PVDTOKENIZER pTokenizer)
{
    return pTokenizer->pTokenCurr;
}

/**
 * Get the class of the current token.
 *
 * @returns Class of the current token.
 * @param   pTokenizer    The tokenizer state.
 */
DECLINLINE(VDTOKENCLASS) vdScriptTokenizerGetTokenClass(PVDTOKENIZER pTokenizer)
{
    return pTokenizer->pTokenCurr->enmClass;
}

/**
 * Returns the token class of the next token in the stream.
 *
 * @returns Token class of the next token.
 * @param   pTokenizer    The tokenizer state.
 */
DECLINLINE(VDTOKENCLASS) vdScriptTokenizerPeekNextClass(PVDTOKENIZER pTokenizer)
{
    return pTokenizer->pTokenNext->enmClass;
}

/**
 * Consume the current token advancing to the next in the stream.
 *
 * @param   pTokenizer    The tokenizer state.
 */
static void vdScriptTokenizerConsume(PVDTOKENIZER pTokenizer)
{
    PVDSCRIPTTOKEN  pTokenTmp = pTokenizer->pTokenCurr;

    /* Switch next token to current token and read in the next token. */
    pTokenizer->pTokenCurr = pTokenizer->pTokenNext;
    pTokenizer->pTokenNext = pTokenTmp;
    vdScriptTokenizerReadNextToken(pTokenizer, pTokenizer->pTokenNext);
}

/**
 * Check whether the next token in the input stream is a punctuator and matches the given
 * character.
 *
 * @returns true if the token matched.
 *          false otherwise.
 * @param   pTokenizer    The tokenizer state.
 * @param   chCheck       The punctuator to check against.
 */
static bool vdScriptTokenizerIsPunctuatorEqual(PVDTOKENIZER pTokenizer, char chCheck)
{
    PCVDSCRIPTTOKEN pToken = vdScriptTokenizerGetToken(pTokenizer);

    if (   pToken->enmClass == VDTOKENCLASS_PUNCTUATOR
        && pToken->Class.Punctuator.chPunctuator == chCheck)
        return true;

    return false;
}

/**
 * Check whether the next token in the input stream is a punctuator and matches the given
 * character and skips it.
 *
 * @returns true if the token matched and was skipped.
 *          false otherwise.
 * @param   pTokenizer    The tokenizer state.
 * @param   chCheck       The punctuator to check against.
 */
static bool vdScriptTokenizerSkipIfIsPunctuatorEqual(PVDTOKENIZER pTokenizer, char chCheck)
{
    bool fEqual = vdScriptTokenizerIsPunctuatorEqual(pTokenizer, chCheck);
    if (fEqual)
        vdScriptTokenizerConsume(pTokenizer);

    return fEqual;
}

/**
 * Check whether the next token in the input stream is a keyword and matches the given
 * keyword.
 *
 * @returns true if the token matched.
 *          false otherwise.
 * @param   pTokenizer    The tokenizer state.
 * @param   enmKey        The keyword to check against.
 */
static bool vdScriptTokenizerIsKeywordEqual(PVDTOKENIZER pTokenizer, VDSCRIPTTOKENKEYWORD enmKeyword)
{
    PCVDSCRIPTTOKEN pToken = vdScriptTokenizerGetToken(pTokenizer);

    if (   pToken->enmClass == VDTOKENCLASS_KEYWORD
        && pToken->Class.Keyword.enmKeyword == enmKeyword)
        return true;

    return false;
}

/**
 * Check whether the next token in the input stream is a keyword and matches the given
 * keyword and skips it.
 *
 * @returns true if the token matched and was skipped.
 *          false otherwise.
 * @param   pTokenizer    The tokenizer state.
 * @param   enmKey        The keyword to check against.
 */
static bool vdScriptTokenizerSkipIfIsKeywordEqual(PVDTOKENIZER pTokenizer, VDSCRIPTTOKENKEYWORD enmKeyword)
{
    bool fEqual = vdScriptTokenizerIsKeywordEqual(pTokenizer, enmKeyword);
    if (fEqual)
        vdScriptTokenizerConsume(pTokenizer);

    return fEqual;
}

/**
 * Check whether the next token in the input stream is a keyword and matches the given
 * keyword.
 *
 * @returns true if the token matched.
 *          false otherwise.
 * @param   pTokenizer    The tokenizer state.
 * @param   pszOp         The operation to check against.
 */
static bool vdScriptTokenizerIsOperatorEqual(PVDTOKENIZER pTokenizer, const char *pszOp)
{
    PCVDSCRIPTTOKEN pToken = vdScriptTokenizerGetToken(pTokenizer);

    if (   pToken->enmClass == VDTOKENCLASS_OPERATORS
        && !RTStrCmp(pToken->Class.Operator.aszOp, pszOp))
        return true;

    return false;
}

/**
 * Check whether the next token in the input stream is an operator and matches the given
 * keyword and skips it.
 *
 * @returns true if the token matched and was skipped.
 *          false otherwise.
 * @param   pTokenizer    The tokenizer state.
 * @param   pszOp         The operation to check against.
 */
static bool vdScriptTokenizerSkipIfIsOperatorEqual(PVDTOKENIZER pTokenizer, const char *pszOp)
{
    bool fEqual = vdScriptTokenizerIsOperatorEqual(pTokenizer, pszOp);
    if (fEqual)
        vdScriptTokenizerConsume(pTokenizer);

    return fEqual;
}

/**
 * Record an error while parsing.
 *
 * @returns VBox status code passed.
 */
static int vdScriptParserError(PVDSCRIPTCTXINT pThis, int rc, RT_SRC_POS_DECL, const char *pszFmt, ...)
{
    RT_NOREF1(pThis); RT_SRC_POS_NOREF();
    va_list va;
    va_start(va, pszFmt);
    RTPrintfV(pszFmt, va);
    va_end(va);
    return rc;
}

/**
 * Puts the next identifier AST node on the stack.
 *
 * @returns VBox status code.
 * @param   pThis         The script context.
 * @param   ppAstNodeIde  Where to store the identifier AST node on success.
 */
static int vdScriptParseIde(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTIDE *ppAstNodeIde)
{
    int rc = VINF_SUCCESS;
    PCVDSCRIPTTOKEN pToken;

    LogFlowFunc(("pThis=%p ppAstNodeIde=%p\n", pThis, ppAstNodeIde));

    pToken = vdScriptTokenizerGetToken(pThis->pTokenizer);
    if (pToken->enmClass != VDTOKENCLASS_IDENTIFIER)
        rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected identifer got...\n");
    else
    {
        /* Create new AST node and push onto stack. */
        PVDSCRIPTASTIDE pAstNodeIde = vdScriptAstNodeIdeAlloc(pToken->Class.Ide.cchIde);
        if (pAstNodeIde)
        {
            rc = RTStrCopyEx(pAstNodeIde->aszIde, pToken->Class.Ide.cchIde + 1, pToken->Class.Ide.pszIde, pToken->Class.Ide.cchIde);
            AssertRC(rc);
            pAstNodeIde->cchIde = (unsigned)pToken->Class.Ide.cchIde;

            *ppAstNodeIde = pAstNodeIde;
            vdScriptTokenizerConsume(pThis->pTokenizer);
        }
        else
            rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating identifier AST node\n");
    }

    LogFlowFunc(("returns %Rrc\n", rc));
    return rc;
}

/**
 * Parse a primary expression.
 *
 * @returns VBox status code.
 * @param   pThis                The  script context.
 * @param   ppAstNodeExpr        Where to store the primary expression on success.
 */
static int vdScriptParsePrimaryExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pThis=%p ppAstNodeExpr=%p\n", pThis, ppAstNodeExpr));

    if (vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, '('))
    {
        rc = vdScriptParseExpression(pThis, ppAstNodeExpr);
        if (RT_SUCCESS(rc)
            && !vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ')'))
            rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \")\", got ...\n");
    }
    else
    {
        PVDSCRIPTASTEXPR pExpr = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
        if (pExpr)
        {
            if (vdScriptTokenizerGetTokenClass(pThis->pTokenizer) == VDTOKENCLASS_IDENTIFIER)
            {
                PVDSCRIPTASTIDE pIde = NULL;
                rc = vdScriptParseIde(pThis, &pIde);
                if (RT_SUCCESS(rc))
                {
                    pExpr->enmType = VDSCRIPTEXPRTYPE_PRIMARY_IDENTIFIER;
                    pExpr->pIde = pIde;
                }
            }
            else if (vdScriptTokenizerGetTokenClass(pThis->pTokenizer) == VDTOKENCLASS_NUMCONST)
            {
                PCVDSCRIPTTOKEN pToken = vdScriptTokenizerGetToken(pThis->pTokenizer);
                pExpr->enmType = VDSCRIPTEXPRTYPE_PRIMARY_NUMCONST;
                pExpr->u64 = pToken->Class.NumConst.u64;
                vdScriptTokenizerConsume(pThis->pTokenizer);
            }
            else if (vdScriptTokenizerGetTokenClass(pThis->pTokenizer) == VDTOKENCLASS_STRINGCONST)
            {
                PCVDSCRIPTTOKEN pToken = vdScriptTokenizerGetToken(pThis->pTokenizer);
                pExpr->enmType = VDSCRIPTEXPRTYPE_PRIMARY_STRINGCONST;
                pExpr->pszStr = RTStrDupN(pToken->Class.StringConst.pszString, pToken->Class.StringConst.cchString);
                vdScriptTokenizerConsume(pThis->pTokenizer);

                if (!pExpr->pszStr)
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating string\n");
            }
            else if (vdScriptTokenizerGetTokenClass(pThis->pTokenizer) == VDTOKENCLASS_KEYWORD)
            {
                PCVDSCRIPTTOKEN pToken = vdScriptTokenizerGetToken(pThis->pTokenizer);
                pExpr->enmType = VDSCRIPTEXPRTYPE_PRIMARY_BOOLEAN;

                if (pToken->Class.Keyword.enmKeyword == VDSCRIPTTOKENKEYWORD_TRUE)
                    pExpr->f = true;
                else if (pToken->Class.Keyword.enmKeyword == VDSCRIPTTOKENKEYWORD_FALSE)
                    pExpr->f = false;
                else
                    rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Unexpected keyword, expected true or false\n");
                vdScriptTokenizerConsume(pThis->pTokenizer);
            }
            else
                rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \"(\" | identifier | constant | string, got ...\n");

            if (RT_FAILURE(rc))
                vdScriptAstNodeFree(&pExpr->Core);
            else
                *ppAstNodeExpr = pExpr;
        }
        else
            rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Parse an argument list for a function call.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   pFnCall              The function call AST node.
 */
static int vdScriptParseFnCallArgumentList(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR pFnCall)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTASTEXPR pExpr = NULL;

    LogFlowFunc(("pThis=%p pFnCall=%p\n", pThis, pFnCall));

    rc = vdScriptParseAssignmentExpression(pThis, &pExpr);
    if (RT_SUCCESS(rc))
    {
        RTListAppend(&pFnCall->FnCall.ListArgs, &pExpr->Core.ListNode);
        while (vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ','))
        {
            rc = vdScriptParseAssignmentExpression(pThis, &pExpr);
            if (RT_SUCCESS(rc))
                RTListAppend(&pFnCall->FnCall.ListArgs, &pExpr->Core.ListNode);
            else
                break;
        }
        if (   RT_SUCCESS(rc)
            && !vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ')'))
            rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \")\", got ...\n");
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Parse a postfix expression.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeExpr        Where to store the expression AST node on success.
 *
 * @note Syntax:
 *      postfix-expression:
 *          primary-expression
 *          postfix-expression ( argument-expression )
 *          postfix-expression ++
 *          postfix-expression --
 *          postfix-expression .  identifier
 *          postfix-expression -> identifier
 * @note: Not supported so far are:
 *          ( type-name ) { initializer-list }
 *          ( type-name ) { initializer-list , }
 */
static int vdScriptParsePostfixExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTASTEXPR pExpr = NULL;

    LogFlowFunc(("pThis=%p ppAstNodeExpr=%p\n", pThis, ppAstNodeExpr));

    rc = vdScriptParsePrimaryExpression(pThis, &pExpr);
    if (RT_SUCCESS(rc))
    {
        while (true)
        {
            PVDSCRIPTASTEXPR pExprNew = NULL;

            if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "++"))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                {
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_POSTFIX_INCREMENT;
                    pExprNew->pExpr = pExpr;
                    pExpr = pExprNew;
                }
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "--"))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                {
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_POSTFIX_DECREMENT;
                    pExprNew->pExpr = pExpr;
                    pExpr = pExprNew;
                }
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "->"))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                {
                    PVDSCRIPTASTIDE pIde = NULL;
                    rc = vdScriptParseIde(pThis, &pIde);
                    if (RT_SUCCESS(rc))
                    {
                        pExprNew->enmType = VDSCRIPTEXPRTYPE_POSTFIX_DEREFERENCE;
                        pExprNew->Deref.pIde = pIde;
                        pExprNew->Deref.pExpr = pExpr;
                        pExpr = pExprNew;
                    }
                    else
                        vdScriptAstNodeFree(&pExprNew->Core);
                }
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "."))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                {
                    PVDSCRIPTASTIDE pIde = NULL;
                    rc = vdScriptParseIde(pThis, &pIde);
                    if (RT_SUCCESS(rc))
                    {
                        pExprNew->enmType = VDSCRIPTEXPRTYPE_POSTFIX_DOT;
                        pExprNew->Deref.pIde = pIde;
                        pExprNew->Deref.pExpr = pExpr;
                        pExpr = pExprNew;
                    }
                    else
                        vdScriptAstNodeFree(&pExprNew->Core);
                }
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, '('))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                {
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_POSTFIX_FNCALL;
                    RTListInit(&pExprNew->FnCall.ListArgs);
                    if (!vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ')'))
                        rc = vdScriptParseFnCallArgumentList(pThis, pExprNew);
                    pExprNew->FnCall.pFnIde = pExpr;
                    pExpr = pExprNew;
                }
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else
                break;

            if (RT_FAILURE(rc))
                break;
        }

        if (RT_SUCCESS(rc))
            *ppAstNodeExpr = pExpr;
        else
            vdScriptAstNodeFree(&pExpr->Core);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Parse an unary expression.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeExpr        Where to store the expression AST node on success.
 *
 * @note Syntax:
 *      unary-expression:
 *          postfix-expression
 *          ++ unary-expression
 *          -- unary-expression
 *          + cast-expression
 *          - cast-expression
 *          ~ cast-expression
 *          ! cast-expression
 *          & cast-expression
 *          * cast-expression
 */
static int vdScriptParseUnaryExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTASTEXPR pExpr = NULL;
    PVDSCRIPTASTEXPR pExprTop = NULL;

    LogFlowFunc(("pThis=%p ppAstNodeExpr=%p\n", pThis, ppAstNodeExpr));

    /** @todo Think about a more beautiful way of parsing this. */
    while (true)
    {
        bool fQuit = false;
        bool fCastExprFollows = false;
        PVDSCRIPTASTEXPR pExprNew = NULL;
        VDSCRIPTEXPRTYPE enmType = VDSCRIPTEXPRTYPE_INVALID;

        if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "++"))
            enmType = VDSCRIPTEXPRTYPE_UNARY_INCREMENT;
        else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "--"))
            enmType = VDSCRIPTEXPRTYPE_UNARY_DECREMENT;
        else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "+"))
        {
            enmType = VDSCRIPTEXPRTYPE_UNARY_POSSIGN;
            fCastExprFollows = true;
        }
        else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "-"))
        {
            enmType = VDSCRIPTEXPRTYPE_UNARY_NEGSIGN;
            fCastExprFollows = true;
        }
        else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "~"))
        {
            enmType = VDSCRIPTEXPRTYPE_UNARY_INVERT;
            fCastExprFollows = true;
        }
        else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "!"))
        {
            enmType = VDSCRIPTEXPRTYPE_UNARY_NEGATE;
            fCastExprFollows = true;
        }
        else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "&"))
        {
            enmType = VDSCRIPTEXPRTYPE_UNARY_REFERENCE;
            fCastExprFollows = true;
        }
        else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "*"))
        {
            enmType = VDSCRIPTEXPRTYPE_UNARY_DEREFERENCE;
            fCastExprFollows = true;
        }

        if (enmType != VDSCRIPTEXPRTYPE_INVALID)
        {
            pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
            if (pExprNew)
                pExprNew->enmType = enmType;
            else
                rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");

            if (   RT_SUCCESS(rc)
                && fCastExprFollows)
            {
                PVDSCRIPTASTEXPR pCastExpr = NULL;

                rc = vdScriptParseCastExpression(pThis, &pCastExpr);
                if (RT_SUCCESS(rc))
                    pExprNew->pExpr = pCastExpr;
                else
                    vdScriptAstNodeFree(&pExprNew->Core);
                fQuit = true;
            }
        }
        else
        {
            /* Must be a postfix expression. */
            rc = vdScriptParsePostfixExpression(pThis, &pExprNew);
            fQuit = true;
        }

        if (RT_SUCCESS(rc))
        {
            if (!pExprTop)
            {
                pExprTop = pExprNew;
                pExpr = pExprNew;
            }
            else
            {
                pExpr->pExpr = pExprNew;
                pExpr = pExprNew;
            }
            if (fQuit)
                break;
        }
        else
            break;
    }

    if (RT_SUCCESS(rc))
        *ppAstNodeExpr = pExprTop;
    else if (pExprTop)
        vdScriptAstNodeFree(&pExprTop->Core);

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

#if 0 /* unused */
/**
 * Parse a storage class specifier.
 *
 * @param   pThis                The script context.
 * @param   penmStorageClass     Where to return the parsed storage classe.
 *                               Contains VDSCRIPTASTSTORAGECLASS_INVALID if no
 *                               valid storage class specifier was found.
 *
 * @note Syntax:
 *      typedef
 *      extern
 *      static
 *      auto
 *      register
 */
static void vdScriptParseStorageClassSpecifier(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTSTORAGECLASS penmStorageClass)
{
    *penmStorageClass = VDSCRIPTASTSTORAGECLASS_INVALID;

    if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_TYPEDEF))
        *penmStorageClass = VDSCRIPTASTSTORAGECLASS_TYPEDEF;
    else if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_EXTERN))
        *penmStorageClass = VDSCRIPTASTSTORAGECLASS_EXTERN;
    else if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_STATIC))
        *penmStorageClass = VDSCRIPTASTSTORAGECLASS_STATIC;
    else if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_AUTO))
        *penmStorageClass = VDSCRIPTASTSTORAGECLASS_AUTO;
    else if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_REGISTER))
        *penmStorageClass = VDSCRIPTASTSTORAGECLASS_REGISTER;
}
#endif /* unused */

#if 0 /* unused */
/**
 * Parse a type qualifier.
 *
 * @param   pThis                The script context.
 * @param   penmTypeQualifier    Where to return the parsed type qualifier.
 *                               Contains VDSCRIPTASTTYPEQUALIFIER_INVALID if no
 *                               valid type qualifier was found.
 *
 * @note Syntax:
 *      const
 *      restrict
 *      volatile
 */
static void vdScriptParseTypeQualifier(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTTYPEQUALIFIER penmTypeQualifier)
{
    *penmTypeQualifier = VDSCRIPTASTTYPEQUALIFIER_INVALID;

    if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_CONST))
        *penmTypeQualifier = VDSCRIPTASTTYPEQUALIFIER_CONST;
    else if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_RESTRICT))
        *penmTypeQualifier = VDSCRIPTASTTYPEQUALIFIER_RESTRICT;
    else if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_VOLATILE))
        *penmTypeQualifier = VDSCRIPTASTTYPEQUALIFIER_VOLATILE;
}
#endif /* unused */

#if 0
/**
 * Parse a struct or union specifier.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstTypeSpec        Where to store the type specifier AST node on success.
 * @param   enmTypeSpecifier     The type specifier to identify whete this is a struct or a union.
 */
static int vdScriptParseStructOrUnionSpecifier(PVDSCRIPTCTXINT pThis, , enmTypeSpecifier)
{
    int rc = VINF_SUCCESS;

    return rc;
}

/**
 * Parse a type specifier.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstTypeSpec        Where to store the type specifier AST node on success.
 *
 * @note Syntax:
 *      struct-or-union-specifier
 *      enum-specifier
 *      typedef-name (identifier: includes void, bool, uint8_t, int8_t, ... for basic integer types)
 */
static int vdScriptParseTypeSpecifier(PVDSCRIPTCTXINT pThis, )
{
    int rc = VINF_SUCCESS;

    if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_STRUCT))
        rc = vdScriptParseStructOrUnionSpecifier(pThis, , VDSCRIPTASTTYPESPECIFIER_STRUCT);
    else if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_UNION))
        rc = vdScriptParseStructOrUnionSpecifier(pThis, , VDSCRIPTASTTYPESPECIFIER_UNION);
    else
    {
        PVDSCRIPTASTIDE pIde = NULL;

        rc = vdScriptParseIde(pThis, &pIde);
        if (RT_SUCCESS(rc))
        {
            AssertMsgFailed(("TODO\n")); /* Parse identifier. */
        }
    }

    return rc;
}
#endif

/**
 * Parse a cast expression.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeExpr        Where to store the expression AST node on success.
 *
 * @note Syntax:
 *      cast-expression:
 *          unary-expression
 *          ( type-name ) cast-expression
 */
static int vdScriptParseCastExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pThis=%p ppAstNodeExpr=%p\n", pThis, ppAstNodeExpr));

#if 0
    if (vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, '('))
    {
        PVDSCRIPTASTTYPE pTypeName = NULL;
        rc = vdScriptParseTypeName(pThis, &pTypeName);
        if (   RT_SUCCESS(rc)
            && vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ')'))
        {
            PVDSCRIPTASTEXPR pExpr = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
            if (pExpr)
            {
                pExpr->enmType = VDSCRIPTEXPRTYPE_CAST;
                rc = vdScriptParseCastExpression(pThis, &pExpr->Cast.pExpr); /** @todo Kill recursion. */
                if (RT_SUCCESS(rc))
                    pExpr->Cast.pTypeName = pTypeName;
                else
                    vdScriptAstNodeFree(&pExpr->Core);
            }
            else
                rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");

            if (RT_FAILURE(rc))
                vdScriptAstNodeFree(&pTypeName->Core);
        }
        else if (RT_SUCCESS(rc))
            rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \")\", got ...\n");
    }
    else
#endif
        rc = vdScriptParseUnaryExpression(pThis, ppAstNodeExpr);

    return rc;
}

/**
 * Parse a multiplicative expression.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeExpr        Where to store the expression AST node on success.
 *
 * @note Syntax:
 *      multiplicative-expression:
 *          cast-expression
 *          multiplicative-expression * cast-expression
 *          multiplicative-expression / cast-expression
 *          multiplicative-expression % cast-expression
 */
static int vdScriptParseMultiplicativeExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTASTEXPR pExpr = NULL;

    LogFlowFunc(("pThis=%p ppAstNodeExpr=%p\n", pThis, ppAstNodeExpr));

    rc = vdScriptParseCastExpression(pThis, &pExpr);
    if (RT_SUCCESS(rc))
    {
        while (RT_SUCCESS(rc))
        {
            VDSCRIPTEXPRTYPE enmType = VDSCRIPTEXPRTYPE_INVALID;
            PVDSCRIPTASTEXPR pExprNew = NULL;

            if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "*"))
                enmType = VDSCRIPTEXPRTYPE_MULTIPLICATION;
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "/"))
                enmType = VDSCRIPTEXPRTYPE_DIVISION;
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "%"))
                enmType = VDSCRIPTEXPRTYPE_MODULUS;
            else
                break;

            pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
            if (pExprNew)
                pExprNew->enmType = enmType;
            else
            {
                rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
                break;
            }

            pExprNew->BinaryOp.pLeftExpr = pExpr;
            pExpr = pExprNew;
            rc = vdScriptParseCastExpression(pThis, &pExprNew);
            if (RT_SUCCESS(rc))
                pExpr->BinaryOp.pRightExpr = pExprNew;
        }

        if (RT_SUCCESS(rc))
            *ppAstNodeExpr = pExpr;
        else
            vdScriptAstNodeFree(&pExpr->Core);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Parse a additive expression.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeExpr        Where to store the expression AST node on success.
 *
 * @note Syntax:
 *      additive-expression:
 *          multiplicative-expression
 *          additive-expression + multiplicative-expression
 *          additive-expression - multiplicative-expression
 */
static int vdScriptParseAdditiveExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTASTEXPR pExpr = NULL;

    LogFlowFunc(("pThis=%p ppAstNodeExpr=%p\n", pThis, ppAstNodeExpr));

    rc = vdScriptParseMultiplicativeExpression(pThis, &pExpr);
    if (RT_SUCCESS(rc))
    {
        PVDSCRIPTASTEXPR pExprNew = NULL;
        while (RT_SUCCESS(rc))
        {
            if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "+"))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_ADDITION;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "-"))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_SUBTRACTION;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else
                break;

            if (RT_FAILURE(rc))
                break;

            pExprNew->BinaryOp.pLeftExpr = pExpr;
            pExpr = pExprNew;
            rc = vdScriptParseMultiplicativeExpression(pThis, &pExprNew);
            if (RT_SUCCESS(rc))
                pExpr->BinaryOp.pRightExpr = pExprNew;
        }

        if (RT_SUCCESS(rc))
            *ppAstNodeExpr = pExpr;
        else
            vdScriptAstNodeFree(&pExpr->Core);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Parse a shift expression.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeExpr        Where to store the expression AST node on success.
 *
 * @note Syntax:
 *      shift-expression:
 *          additive-expression
 *          shift-expression << additive-expression
 *          shift-expression >> additive-expression
 */
static int vdScriptParseShiftExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTASTEXPR pExpr = NULL;

    LogFlowFunc(("pThis=%p ppAstNodeExpr=%p\n", pThis, ppAstNodeExpr));

    rc = vdScriptParseAdditiveExpression(pThis, &pExpr);
    if (RT_SUCCESS(rc))
    {
        PVDSCRIPTASTEXPR pExprNew = NULL;
        while (RT_SUCCESS(rc))
        {
            if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "<<"))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_LSL;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, ">>"))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_LSR;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else
                break;

            if (RT_FAILURE(rc))
                break;

            pExprNew->BinaryOp.pLeftExpr = pExpr;
            pExpr = pExprNew;
            rc = vdScriptParseAdditiveExpression(pThis, &pExprNew);
            if (RT_SUCCESS(rc))
                pExpr->BinaryOp.pRightExpr = pExprNew;
        }

        if (RT_SUCCESS(rc))
            *ppAstNodeExpr = pExpr;
        else
            vdScriptAstNodeFree(&pExpr->Core);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Parse a relational expression.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeExpr        Where to store the expression AST node on success.
 *
 * @note Syntax:
 *      relational-expression:
 *          shift-expression
 *          relational-expression < shift-expression
 *          relational-expression > shift-expression
 *          relational-expression >= shift-expression
 *          relational-expression <= shift-expression
 */
static int vdScriptParseRelationalExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTASTEXPR pExpr = NULL;

    LogFlowFunc(("pThis=%p ppAstNodeExpr=%p\n", pThis, ppAstNodeExpr));

    rc = vdScriptParseShiftExpression(pThis, &pExpr);
    if (RT_SUCCESS(rc))
    {
        PVDSCRIPTASTEXPR pExprNew = NULL;
        while (RT_SUCCESS(rc))
        {
            if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "<"))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_LOWER;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, ">"))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_HIGHER;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, ">="))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_HIGHEREQUAL;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "<="))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_LOWEREQUAL;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else
                break;

            if (RT_FAILURE(rc))
                break;

            pExprNew->BinaryOp.pLeftExpr = pExpr;
            pExpr = pExprNew;
            rc = vdScriptParseShiftExpression(pThis, &pExprNew);
            if (RT_SUCCESS(rc))
                pExpr->BinaryOp.pRightExpr = pExprNew;
        }

        if (RT_SUCCESS(rc))
            *ppAstNodeExpr = pExpr;
        else
            vdScriptAstNodeFree(&pExpr->Core);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Parse a equality expression.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeExpr        Where to store the expression AST node on success.
 *
 * @note Syntax:
 *      equality-expression:
 *          relational-expression
 *          equality-expression == relational-expression
 *          equality-expression != relational-expression
 */
static int vdScriptParseEqualityExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTASTEXPR pExpr = NULL;

    LogFlowFunc(("pThis=%p ppAstNodeExpr=%p\n", pThis, ppAstNodeExpr));

    rc = vdScriptParseRelationalExpression(pThis, &pExpr);
    if (RT_SUCCESS(rc))
    {
        PVDSCRIPTASTEXPR pExprNew = NULL;
        while (RT_SUCCESS(rc))
        {
            if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "=="))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_EQUAL;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "!="))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_NOTEQUAL;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else
                break;

            if (RT_FAILURE(rc))
                break;

            pExprNew->BinaryOp.pLeftExpr = pExpr;
            pExpr = pExprNew;
            rc = vdScriptParseRelationalExpression(pThis, &pExprNew);
            if (RT_SUCCESS(rc))
                pExpr->BinaryOp.pRightExpr = pExprNew;
        }

        if (RT_SUCCESS(rc))
            *ppAstNodeExpr = pExpr;
        else
            vdScriptAstNodeFree(&pExpr->Core);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Parse a bitwise and expression.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeExpr        Where to store the expression AST node on success.
 *
 * @note Syntax:
 *      and-expression:
 *          equality-expression
 *          and-expression & equality-expression
 */
static int vdScriptParseBitwiseAndExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTASTEXPR pExpr = NULL;

    LogFlowFunc(("pThis=%p ppAstNodeExpr=%p\n", pThis, ppAstNodeExpr));

    rc = vdScriptParseEqualityExpression(pThis, &pExpr);
    if (RT_SUCCESS(rc))
    {
        PVDSCRIPTASTEXPR pExprNew = NULL;
        while (   RT_SUCCESS(rc)
               && vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "&"))
        {
            pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
            if (pExprNew)
            {
                pExprNew->enmType = VDSCRIPTEXPRTYPE_EQUAL;
                pExprNew->BinaryOp.pLeftExpr = pExpr;
                pExpr = pExprNew;
                rc = vdScriptParseEqualityExpression(pThis, &pExprNew);
                if (RT_SUCCESS(rc))
                    pExpr->BinaryOp.pRightExpr = pExprNew;
            }
            else
                rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
        }

        if (RT_SUCCESS(rc))
            *ppAstNodeExpr = pExpr;
        else
            vdScriptAstNodeFree(&pExpr->Core);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Parse a bitwise xor expression.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeExpr        Where to store the expression AST node on success.
 *
 * @note Syntax:
 *      xor-expression:
 *          and-expression
 *          xor-expression ^ equality-expression
 */
static int vdScriptParseBitwiseXorExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTASTEXPR pExpr = NULL;

    LogFlowFunc(("pThis=%p ppAstNodeExpr=%p\n", pThis, ppAstNodeExpr));

    rc = vdScriptParseBitwiseAndExpression(pThis, &pExpr);
    if (RT_SUCCESS(rc))
    {
        PVDSCRIPTASTEXPR pExprNew = NULL;
        while (   RT_SUCCESS(rc)
               && vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "^"))
        {
            pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
            if (pExprNew)
            {
                pExprNew->enmType = VDSCRIPTEXPRTYPE_BITWISE_XOR;
                pExprNew->BinaryOp.pLeftExpr = pExpr;
                pExpr = pExprNew;
                rc = vdScriptParseBitwiseAndExpression(pThis, &pExprNew);
                if (RT_SUCCESS(rc))
                    pExpr->BinaryOp.pRightExpr = pExprNew;
            }
            else
                rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
        }

        if (RT_SUCCESS(rc))
            *ppAstNodeExpr = pExpr;
        else
            vdScriptAstNodeFree(&pExpr->Core);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Parse a bitwise or expression.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeExpr        Where to store the expression AST node on success.
 *
 * @note Syntax:
 *      or-expression:
 *          xor-expression
 *          or-expression | xor-expression
 */
static int vdScriptParseBitwiseOrExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTASTEXPR pExpr = NULL;

    LogFlowFunc(("pThis=%p ppAstNodeExpr=%p\n", pThis, ppAstNodeExpr));

    rc = vdScriptParseBitwiseXorExpression(pThis, &pExpr);
    if (RT_SUCCESS(rc))
    {
        PVDSCRIPTASTEXPR pExprNew = NULL;
        while (   RT_SUCCESS(rc)
               && vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "|"))
        {
            pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
            if (pExprNew)
            {
                pExprNew->enmType = VDSCRIPTEXPRTYPE_BITWISE_OR;
                pExprNew->BinaryOp.pLeftExpr = pExpr;
                pExpr = pExprNew;
                rc = vdScriptParseBitwiseXorExpression(pThis, &pExprNew);
                if (RT_SUCCESS(rc))
                    pExpr->BinaryOp.pRightExpr = pExprNew;
            }
            else
                rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
        }

        if (RT_SUCCESS(rc))
            *ppAstNodeExpr = pExpr;
        else
            vdScriptAstNodeFree(&pExpr->Core);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Parse a logical and expression.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeExpr        Where to store the expression AST node on success.
 *
 * @note Syntax:
 *      logical-and-expression:
 *          or-expression
 *          logical-and-expression | or-expression
 */
static int vdScriptParseLogicalAndExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTASTEXPR pExpr = NULL;

    LogFlowFunc(("pThis=%p ppAstNodeExpr=%p\n", pThis, ppAstNodeExpr));

    rc = vdScriptParseBitwiseOrExpression(pThis, &pExpr);
    if (RT_SUCCESS(rc))
    {
        PVDSCRIPTASTEXPR pExprNew = NULL;
        while (   RT_SUCCESS(rc)
               && vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "&&"))
        {
            pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
            if (pExprNew)
            {
                pExprNew->enmType = VDSCRIPTEXPRTYPE_LOGICAL_AND;
                pExprNew->BinaryOp.pLeftExpr = pExpr;
                pExpr = pExprNew;
                rc = vdScriptParseBitwiseOrExpression(pThis, &pExprNew);
                if (RT_SUCCESS(rc))
                    pExpr->BinaryOp.pRightExpr = pExprNew;
            }
            else
                rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
        }

        if (RT_SUCCESS(rc))
            *ppAstNodeExpr = pExpr;
        else
            vdScriptAstNodeFree(&pExpr->Core);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Parse a logical or expression.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeExpr        Where to store the expression AST node on success.
 *
 * @note Syntax:
 *      logical-or-expression:
 *          logical-and-expression
 *          logical-or-expression | logical-and-expression
 */
static int vdScriptParseLogicalOrExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTASTEXPR pExpr = NULL;

    LogFlowFunc(("pThis=%p ppAstNodeExpr=%p\n", pThis, ppAstNodeExpr));

    rc = vdScriptParseLogicalAndExpression(pThis, &pExpr);
    if (RT_SUCCESS(rc))
    {
        PVDSCRIPTASTEXPR pExprNew = NULL;
        while (   RT_SUCCESS(rc)
               && vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "||"))
        {
            pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
            if (pExprNew)
            {
                pExprNew->enmType = VDSCRIPTEXPRTYPE_LOGICAL_OR;
                pExprNew->BinaryOp.pLeftExpr = pExpr;
                pExpr = pExprNew;
                rc = vdScriptParseLogicalAndExpression(pThis, &pExprNew);
                if (RT_SUCCESS(rc))
                    pExpr->BinaryOp.pRightExpr = pExprNew;
            }
            else
                rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
        }

        if (RT_SUCCESS(rc))
            *ppAstNodeExpr = pExpr;
        else
            vdScriptAstNodeFree(&pExpr->Core);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Parse a conditional expression.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeExpr        Where to store the expression AST node on success.
 *
 * @note: VDScript doesn't support logical-or-expression ? expression : conditional-expression
 *        so a conditional expression is equal to a logical-or-expression.
 */
static int vdScriptParseCondExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr)
{
    return vdScriptParseLogicalOrExpression(pThis, ppAstNodeExpr);
}

#if 0 /* unused */
/**
 * Parse a constant expression.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeExpr        Where to store the expression AST node on success.
 *
 * @note Syntax:
 *      constant-expression:
 *          conditional-expression
 */
static int vdScriptParseConstExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr)
{
    return vdScriptParseCondExpression(pThis, ppAstNodeExpr);
}
#endif

/**
 * Parse an assignment expression.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeExpr        Where to store the expression AST node on success.
 *
 * @note Syntax:
 *      assignment-expression:
 *          conditional-expression
 *          unary-expression assignment-operator assignment-expression
 */
static int vdScriptParseAssignmentExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTASTEXPR pExpr;

    LogFlowFunc(("pThis=%p ppAstNodeExpr=%p\n", pThis, ppAstNodeExpr));

    rc = vdScriptParseLogicalOrExpression(pThis, &pExpr);
    if (RT_SUCCESS(rc))
    {
        PVDSCRIPTASTEXPR pExprNew = NULL;
        while (RT_SUCCESS(rc))
        {
            if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "="))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_ASSIGN;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "*="))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_ASSIGN_MULT;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "/="))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_ASSIGN_DIV;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "%="))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_ASSIGN_MOD;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "+="))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_ASSIGN_ADD;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "-="))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_ASSIGN_SUB;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "<<="))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_ASSIGN_LSL;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, ">>="))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_ASSIGN_LSR;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "&="))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_ASSIGN_AND;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "^="))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_ASSIGN_XOR;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else if (vdScriptTokenizerSkipIfIsOperatorEqual(pThis->pTokenizer, "|="))
            {
                pExprNew = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
                if (pExprNew)
                    pExprNew->enmType = VDSCRIPTEXPRTYPE_ASSIGN_OR;
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
            }
            else
                break;

            if (RT_SUCCESS(rc))
                break;

            pExprNew->BinaryOp.pLeftExpr = pExpr;
            pExpr = pExprNew;
            rc = vdScriptParseLogicalOrExpression(pThis, &pExprNew);
            if (RT_SUCCESS(rc))
                pExpr->BinaryOp.pRightExpr = pExprNew;
        }

        if (RT_SUCCESS(rc))
            *ppAstNodeExpr = pExpr;
        else
            vdScriptAstNodeFree(&pExpr->Core);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Parse an expression.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeExpr        Where to store the expression AST node on success.
 *
 * @note Syntax:
 *      expression:
 *          assignment-expression
 *          expression , assignment-expression
 */
static int vdScriptParseExpression(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTEXPR *ppAstNodeExpr)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTASTEXPR pAssignExpr = NULL;

    LogFlowFunc(("pThis=%p ppAstNodeExpr=%p\n", pThis, ppAstNodeExpr));

    rc = vdScriptParseAssignmentExpression(pThis, &pAssignExpr);
    if (   RT_SUCCESS(rc)
        && vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ','))
    {
        PVDSCRIPTASTEXPR pListAssignExpr = (PVDSCRIPTASTEXPR)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_EXPRESSION);
        if (pListAssignExpr)
        {
            pListAssignExpr->enmType = VDSCRIPTEXPRTYPE_ASSIGNMENT_LIST;
            RTListInit(&pListAssignExpr->ListExpr);
            RTListAppend(&pListAssignExpr->ListExpr, &pAssignExpr->Core.ListNode);
            do
            {
                rc = vdScriptParseAssignmentExpression(pThis, &pAssignExpr);
                if (RT_SUCCESS(rc))
                    RTListAppend(&pListAssignExpr->ListExpr, &pAssignExpr->Core.ListNode);
            } while (   RT_SUCCESS(rc)
                     && vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ','));

            if (RT_FAILURE(rc))
                vdScriptAstNodeFree(&pListAssignExpr->Core);
            else
                *ppAstNodeExpr = pListAssignExpr;
        }
        else
            rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating expression AST node\n");
    }
    else if (RT_SUCCESS(rc))
        *ppAstNodeExpr = pAssignExpr;

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Parse an if statement.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   pAstNodeIf           Uninitialized if AST node.
 *
 * @note The caller skipped the "if" token already.
 */
static int vdScriptParseIf(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTIF pAstNodeIf)
{
    int rc = VINF_SUCCESS;

    if (vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, '('))
    {
        PVDSCRIPTASTEXPR pCondExpr = NULL;
        rc = vdScriptParseExpression(pThis, &pCondExpr);
        if (RT_SUCCESS(rc))
        {
            if (vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ')'))
            {
                PVDSCRIPTASTSTMT pStmt = NULL;
                PVDSCRIPTASTSTMT pElseStmt = NULL;
                rc = vdScriptParseStatement(pThis, &pStmt);
                if (   RT_SUCCESS(rc)
                    && vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_ELSE))
                    rc = vdScriptParseStatement(pThis, &pElseStmt);

                if (RT_SUCCESS(rc))
                {
                    pAstNodeIf->pCond     = pCondExpr;
                    pAstNodeIf->pTrueStmt = pStmt;
                    pAstNodeIf->pElseStmt = pElseStmt;
                }
                else if (pStmt)
                    vdScriptAstNodeFree(&pStmt->Core);
            }
            else
                rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \")\", got ...\n");

            if (RT_FAILURE(rc))
                vdScriptAstNodeFree(&pCondExpr->Core);
        }
    }
    else
        rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \"(\", got ...\n");

    return rc;
}

/**
 * Parse a switch statement.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   pAstNodeSwitch       Uninitialized switch AST node.
 *
 * @note The caller skipped the "switch" token already.
 */
static int vdScriptParseSwitch(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTSWITCH pAstNodeSwitch)
{
    int rc = VINF_SUCCESS;

    if (vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, '('))
    {
        PVDSCRIPTASTEXPR pExpr = NULL;

        rc = vdScriptParseExpression(pThis, &pExpr);
        if (   RT_SUCCESS(rc)
            && vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ')'))
        {
            PVDSCRIPTASTSTMT pStmt = NULL;
            rc = vdScriptParseStatement(pThis, &pStmt);
            if (RT_SUCCESS(rc))
            {
                pAstNodeSwitch->pCond = pExpr;
                pAstNodeSwitch->pStmt = pStmt;
            }
            else
                vdScriptAstNodeFree(&pExpr->Core);
        }
        else if (RT_SUCCESS(rc))
        {
            rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \")\", got ...\n");
            vdScriptAstNodeFree(&pExpr->Core);
        }
    }
    else
        rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \"(\", got ...\n");

    return rc;
}

/**
 * Parse a while or do ... while statement.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   pAstNodeWhile        Uninitialized while AST node.
 *
 * @note The caller skipped the "while" or "do" token already.
 */
static int vdScriptParseWhile(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTWHILE pAstNodeWhile, bool fDoWhile)
{
    int rc = VINF_SUCCESS;

    pAstNodeWhile->fDoWhile = fDoWhile;

    if (fDoWhile)
    {
        PVDSCRIPTASTSTMT pStmt = NULL;
        rc = vdScriptParseStatement(pThis, &pStmt);
        if (   RT_SUCCESS(rc)
            && vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_WHILE))
        {
            if (vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, '('))
            {
                PVDSCRIPTASTEXPR pExpr = NULL;

                rc = vdScriptParseExpression(pThis, &pExpr);
                if (   RT_SUCCESS(rc)
                    && vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ')'))
                {
                    if (vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ';'))
                    {
                        pAstNodeWhile->pCond = pExpr;
                        pAstNodeWhile->pStmt = pStmt;
                    }
                    else
                        rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \";\", got ...\n");
                }
                else if (RT_SUCCESS(rc))
                {
                    rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \")\", got ...\n");
                    vdScriptAstNodeFree(&pExpr->Core);
                }
            }
            else
                rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \"(\", got ...\n");
        }
        else if (RT_SUCCESS(rc))
            rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \"while\", got ...\n");

        if (   RT_FAILURE(rc)
            && pStmt)
            vdScriptAstNodeFree(&pStmt->Core);
    }
    else
    {
        if (vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, '('))
        {
            PVDSCRIPTASTEXPR pExpr = NULL;

            rc = vdScriptParseExpression(pThis, &pExpr);
            if (   RT_SUCCESS(rc)
                && vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ')'))
            {
                PVDSCRIPTASTSTMT pStmt = NULL;
                rc = vdScriptParseStatement(pThis, &pStmt);
                if (RT_SUCCESS(rc))
                {
                    pAstNodeWhile->pCond = pExpr;
                    pAstNodeWhile->pStmt = pStmt;
                }
                else
                    vdScriptAstNodeFree(&pExpr->Core);
            }
            else if (RT_SUCCESS(rc))
            {
                rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \")\", got ...\n");
                vdScriptAstNodeFree(&pExpr->Core);
            }
        }
        else
            rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \"(\", got ...\n");
    }

    return rc;
}

/**
 * Parse a for statement.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   pAstNodeFor          Uninitialized for AST node.
 *
 * @note The caller skipped the "for" token already.
 */
static int vdScriptParseFor(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTFOR pAstNodeFor)
{
    int rc = VINF_SUCCESS;

    if (vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, '('))
    {
        PVDSCRIPTASTEXPR pExprStart = NULL;
        PVDSCRIPTASTEXPR pExprCond = NULL;
        PVDSCRIPTASTEXPR pExpr3 = NULL;

        rc = vdScriptParseExpression(pThis, &pExprStart);
        if (   RT_SUCCESS(rc)
            && vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ';'))
        {
            rc = vdScriptParseExpression(pThis, &pExprCond);
            if (   RT_SUCCESS(rc)
                && vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ';'))
            {
                rc = vdScriptParseExpression(pThis, &pExpr3);
                if (   RT_SUCCESS(rc)
                    && vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ')'))
                {
                    PVDSCRIPTASTSTMT pStmt = NULL;
                    rc = vdScriptParseStatement(pThis, &pStmt);
                    if (RT_SUCCESS(rc))
                    {
                        pAstNodeFor->pExprStart = pExprStart;
                        pAstNodeFor->pExprCond  = pExprCond;
                        pAstNodeFor->pExpr3     = pExpr3;
                        pAstNodeFor->pStmt      = pStmt;
                    }
                }
            }
            else if (RT_SUCCESS(rc))
                rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \";\", got ...\n");
        }
        else if (RT_SUCCESS(rc))
            rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \";\", got ...\n");

        if (RT_FAILURE(rc))
        {
            if (pExprStart)
                vdScriptAstNodeFree(&pExprStart->Core);
            if (pExprCond)
                vdScriptAstNodeFree(&pExprCond->Core);
            if (pExpr3)
                vdScriptAstNodeFree(&pExpr3->Core);
        }
    }
    else
        rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \"(\", got ...\n");

    return rc;
}

/**
 * Parse a declaration.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeDecl        Where to store the declaration AST node on success.
 */
static int vdScriptParseDeclaration(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTDECL *ppAstNodeDecl)
{
    int rc = VERR_NOT_IMPLEMENTED;
    RT_NOREF2(pThis, ppAstNodeDecl);
    return rc;
}

/**
 * Parse a statement.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeStmt        Where to store the statement AST node on success.
 */
static int vdScriptParseStatement(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTSTMT *ppAstNodeStmt)
{
    int rc = VINF_SUCCESS;

    /* Shortcut for a new compound statement. */
    if (vdScriptTokenizerIsPunctuatorEqual(pThis->pTokenizer, '{'))
        rc = vdScriptParseCompoundStatement(pThis, ppAstNodeStmt);
    else
    {
        PVDSCRIPTASTSTMT pAstNodeStmt = (PVDSCRIPTASTSTMT)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_STATEMENT);

        if (pAstNodeStmt)
        {

            if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_DEFAULT))
            {
                if (vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ':'))
                {
                    PVDSCRIPTASTSTMT pAstNodeStmtDef = NULL;
                    pAstNodeStmt->enmStmtType = VDSCRIPTSTMTTYPE_DEFAULT;
                    rc = vdScriptParseStatement(pThis, &pAstNodeStmtDef);
                    if (RT_SUCCESS(rc))
                        pAstNodeStmt->pStmt = pAstNodeStmtDef;
                }
                else
                    rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \":\", got ...\n");
            }
            else if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_CASE))
            {
                PVDSCRIPTASTEXPR pAstNodeExpr = NULL;
                rc = vdScriptParseCondExpression(pThis, &pAstNodeExpr);
                if (RT_SUCCESS(rc))
                {
                    if (vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ':'))
                    {
                        PVDSCRIPTASTSTMT pAstNodeCaseStmt = NULL;
                        rc = vdScriptParseStatement(pThis, &pAstNodeCaseStmt);
                        if (RT_SUCCESS(rc))
                        {
                            pAstNodeStmt->enmStmtType = VDSCRIPTSTMTTYPE_CASE;
                            pAstNodeStmt->Case.pExpr  = pAstNodeExpr;
                            pAstNodeStmt->Case.pStmt  = pAstNodeCaseStmt;
                        }
                    }
                    else
                        rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \":\", got ...\n");

                    if (RT_FAILURE(rc))
                        vdScriptAstNodeFree(&pAstNodeExpr->Core);
                }
            }
            else if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_IF))
            {
                pAstNodeStmt->enmStmtType = VDSCRIPTSTMTTYPE_IF;
                rc = vdScriptParseIf(pThis, &pAstNodeStmt->If);
            }
            else if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_SWITCH))
            {
                pAstNodeStmt->enmStmtType = VDSCRIPTSTMTTYPE_SWITCH;
                rc = vdScriptParseSwitch(pThis, &pAstNodeStmt->Switch);
            }
            else if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_WHILE))
            {
                pAstNodeStmt->enmStmtType = VDSCRIPTSTMTTYPE_WHILE;
                rc = vdScriptParseWhile(pThis, &pAstNodeStmt->While, false /* fDoWhile */);
            }
            else if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_DO))
            {
                pAstNodeStmt->enmStmtType = VDSCRIPTSTMTTYPE_WHILE;
                rc = vdScriptParseWhile(pThis, &pAstNodeStmt->While, true /* fDoWhile */);
            }
            else if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_FOR))
            {
                pAstNodeStmt->enmStmtType = VDSCRIPTSTMTTYPE_FOR;
                rc = vdScriptParseFor(pThis, &pAstNodeStmt->For);
            }
            else if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_CONTINUE))
            {
                pAstNodeStmt->enmStmtType = VDSCRIPTSTMTTYPE_CONTINUE;
                if (!vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ';'))
                    rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \";\", got ...\n");
            }
            else if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_BREAK))
            {
                pAstNodeStmt->enmStmtType = VDSCRIPTSTMTTYPE_BREAK;
                if (!vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ';'))
                    rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \";\", got ...\n");
            }
            else if (vdScriptTokenizerSkipIfIsKeywordEqual(pThis->pTokenizer, VDSCRIPTTOKENKEYWORD_RETURN))
            {
                pAstNodeStmt->enmStmtType = VDSCRIPTSTMTTYPE_RETURN;
                if (!vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ';'))
                {
                    rc = vdScriptParseExpression(pThis, &pAstNodeStmt->pExpr);
                    if (   RT_SUCCESS(rc)
                        && !vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ';'))
                        rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \";\", got ...\n");
                }
                else
                    pAstNodeStmt->pExpr = NULL; /* No expression for return. */
            }
            else
            {
                /* Must be an expression. */
                pAstNodeStmt->enmStmtType = VDSCRIPTSTMTTYPE_EXPRESSION;
                rc = vdScriptParseExpression(pThis, &pAstNodeStmt->pExpr);
                if (   RT_SUCCESS(rc)
                    && !vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ';'))
                    rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \";\", got ...\n");
            }

            if (RT_SUCCESS(rc))
                *ppAstNodeStmt = pAstNodeStmt;
            else
                vdScriptAstNodeFree(&pAstNodeStmt->Core);
        }
        else
            rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory creating statement node\n");
    }

    return rc;
}

/**
 * Parses a compound statement.
 *
 * @returns VBox status code.
 * @param   pThis                The script context.
 * @param   ppAstNodeCompound    Where to store the compound AST node on success.
 */
static int vdScriptParseCompoundStatement(PVDSCRIPTCTXINT pThis, PVDSCRIPTASTSTMT *ppAstNodeCompound)
{
    int rc = VINF_SUCCESS;

    if (vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, '{'))
    {
        PVDSCRIPTASTSTMT pAstNodeCompound = (PVDSCRIPTASTSTMT)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_STATEMENT);
        if (pAstNodeCompound)
        {
            pAstNodeCompound->enmStmtType = VDSCRIPTSTMTTYPE_COMPOUND;
            RTListInit(&pAstNodeCompound->Compound.ListDecls);
            RTListInit(&pAstNodeCompound->Compound.ListStmts);
            while (!vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, '}'))
            {
                /*
                 * Check whether we have a declaration or a statement.
                 * For now we assume that 2 identifier tokens specify a declaration
                 * (type + variable name). Having two consecutive identifers is not possible
                 * for a statement.
                 */
                if (   vdScriptTokenizerGetTokenClass(pThis->pTokenizer) == VDTOKENCLASS_IDENTIFIER
                    && vdScriptTokenizerPeekNextClass(pThis->pTokenizer) == VDTOKENCLASS_IDENTIFIER)
                {
                    PVDSCRIPTASTDECL pAstNodeDecl = NULL;
                    rc = vdScriptParseDeclaration(pThis, &pAstNodeDecl);
                    if (RT_SUCCESS(rc))
                        RTListAppend(&pAstNodeCompound->Compound.ListDecls, &pAstNodeDecl->Core.ListNode);
                }
                else
                {
                    PVDSCRIPTASTSTMT pAstNodeStmt = NULL;
                    rc = vdScriptParseStatement(pThis, &pAstNodeStmt);
                    if (RT_SUCCESS(rc))
                        RTListAppend(&pAstNodeCompound->Compound.ListStmts, &pAstNodeStmt->Core.ListNode);
                }

                if (RT_FAILURE(rc))
                    break;
            }

            if (RT_SUCCESS(rc))
                *ppAstNodeCompound = pAstNodeCompound;
            else
                vdScriptAstNodeFree(&pAstNodeCompound->Core);
        }
        else
            rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory creating compound statement node\n");
    }
    else
        rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \"{\" got...\n");


    return rc;
}

/**
 * Parses a function definition from the given tokenizer.
 *
 * @returns VBox status code.
 * @param   pThis         The script context.
 */
static int vdScriptParseAddFnDef(PVDSCRIPTCTXINT pThis)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTASTIDE pRetType = NULL;
    PVDSCRIPTASTIDE pFnIde = NULL;

    LogFlowFunc(("pThis=%p\n", pThis));

    /* Put return type on the stack. */
    rc = vdScriptParseIde(pThis, &pRetType);
    if (RT_SUCCESS(rc))
    {
        /* Function name */
        rc = vdScriptParseIde(pThis, &pFnIde);
        if (RT_SUCCESS(rc))
        {
            if (vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, '('))
            {
                PVDSCRIPTASTFN pAstNodeFn = (PVDSCRIPTASTFN)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_FUNCTION);

                if (pAstNodeFn)
                {
                    pAstNodeFn->pFnIde   = pFnIde;
                    pAstNodeFn->pRetType = pRetType;
                    RTListInit(&pAstNodeFn->ListArgs);

                    pFnIde = NULL;
                    pRetType = NULL;

                    /* Parse parameter list, create empty parameter list AST node and put it on the stack. */
                    while (!vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ')'))
                    {
                        PVDSCRIPTASTIDE pArgType = NULL;
                        PVDSCRIPTASTIDE pArgIde = NULL;
                        /* Parse two identifiers, first one is the type, second the name. */
                        rc = vdScriptParseIde(pThis, &pArgType);
                        if (RT_SUCCESS(rc))
                            rc = vdScriptParseIde(pThis, &pArgIde);

                        if (RT_SUCCESS(rc))
                        {
                            PVDSCRIPTASTFNARG pAstNodeFnArg = (PVDSCRIPTASTFNARG)vdScriptAstNodeAlloc(VDSCRIPTASTCLASS_FUNCTIONARG);
                            if (pAstNodeFnArg)
                            {
                                pAstNodeFnArg->pArgIde = pArgIde;
                                pAstNodeFnArg->pType   = pArgType;
                                RTListAppend(&pAstNodeFn->ListArgs, &pAstNodeFnArg->Core.ListNode);
                                pAstNodeFn->cArgs++;
                            }
                            else
                                rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating function argument AST node\n");
                        }

                        if (RT_FAILURE(rc))
                        {
                            if (pArgType)
                                vdScriptAstNodeFree(&pArgType->Core);
                            if (pArgIde)
                                vdScriptAstNodeFree(&pArgIde->Core);
                        }

                        if (   !vdScriptTokenizerSkipIfIsPunctuatorEqual(pThis->pTokenizer, ',')
                            && !vdScriptTokenizerIsPunctuatorEqual(pThis->pTokenizer, ')'))
                        {
                            rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \",\" or \")\" got...\n");
                            break;
                        }
                    }

                    /* Parse the compound or statement block now. */
                    if (RT_SUCCESS(rc))
                    {
                        PVDSCRIPTASTSTMT pAstCompound = NULL;

                        rc = vdScriptParseCompoundStatement(pThis, &pAstCompound);
                        if (RT_SUCCESS(rc))
                        {
                            /*
                             * Link compound statement block to function AST node and add it to the
                             * list of functions.
                             */
                            pAstNodeFn->pCompoundStmts = pAstCompound;
                            RTListAppend(&pThis->ListAst, &pAstNodeFn->Core.ListNode);

                            PVDSCRIPTFN pFn = (PVDSCRIPTFN)RTMemAllocZ(sizeof(VDSCRIPTFN));
                            if (pFn)
                            {
                                pFn->Core.pszString = pAstNodeFn->pFnIde->aszIde;
                                pFn->Core.cchString = strlen(pFn->Core.pszString);
                                pFn->fExternal      = false;
                                pFn->Type.Internal.pAstFn = pAstNodeFn;
                                /** @todo Parameters. */
                                RTStrSpaceInsert(&pThis->hStrSpaceFn, &pFn->Core);
                            }
                            else
                                rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Out of memory allocating memory for function\n");
                        }
                    }

                    if (RT_FAILURE(rc))
                        vdScriptAstNodeFree(&pAstNodeFn->Core);
                }
                else
                    rc = vdScriptParserError(pThis, VERR_NO_MEMORY, RT_SRC_POS, "Parser: Out of memory allocating function AST node\n");
            }
            else
                rc = vdScriptParserError(pThis, VERR_INVALID_PARAMETER, RT_SRC_POS, "Parser: Expected \"(\" got...\n");
        }
    }

    if (RT_FAILURE(rc))
    {
        if (pRetType)
            vdScriptAstNodeFree(&pRetType->Core);
        if (pFnIde)
            vdScriptAstNodeFree(&pFnIde->Core);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

/**
 * Parses the script from the given tokenizer.
 *
 * @returns VBox status code.
 * @param   pThis         The script context.
 */
static int vdScriptParseFromTokenizer(PVDSCRIPTCTXINT pThis)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("pThis=%p\n", pThis));

    /* This is a very very simple LL(1) parser, don't expect much from it for now :). */
    while (   RT_SUCCESS(rc)
           && !vdScriptTokenizerIsEos(pThis->pTokenizer))
        rc = vdScriptParseAddFnDef(pThis);

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

DECLHIDDEN(int) VDScriptCtxCreate(PVDSCRIPTCTX phScriptCtx)
{
    int rc = VINF_SUCCESS;

    LogFlowFunc(("phScriptCtx=%p\n", phScriptCtx));

    AssertPtrReturn(phScriptCtx, VERR_INVALID_POINTER);

    PVDSCRIPTCTXINT pThis = (PVDSCRIPTCTXINT)RTMemAllocZ(sizeof(VDSCRIPTCTXINT));
    if (pThis)
    {
        pThis->hStrSpaceFn         = NULL;
        RTListInit(&pThis->ListAst);
        *phScriptCtx = pThis;
    }
    else
        rc = VINF_SUCCESS;

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

static DECLCALLBACK(int) vdScriptCtxDestroyFnSpace(PRTSTRSPACECORE pStr, void *pvUser)
{
    NOREF(pvUser);

    /*
     * Just free the whole structure, the AST for internal functions will be
     * destroyed later.
     */
    RTMemFree(pStr);
    return VINF_SUCCESS;
}

DECLHIDDEN(void) VDScriptCtxDestroy(VDSCRIPTCTX hScriptCtx)
{
    PVDSCRIPTCTXINT pThis = hScriptCtx;

    AssertPtrReturnVoid(pThis);

    LogFlowFunc(("hScriptCtx=%p\n", pThis));

    RTStrSpaceDestroy(&pThis->hStrSpaceFn, vdScriptCtxDestroyFnSpace, NULL);

    /* Go through list of function ASTs and destroy them. */
    PVDSCRIPTASTCORE pIter;
    PVDSCRIPTASTCORE pIterNext;
    RTListForEachSafe(&pThis->ListAst, pIter, pIterNext, VDSCRIPTASTCORE, ListNode)
    {
        RTListNodeRemove(&pIter->ListNode);
        RTListInit(&pIter->ListNode);
        vdScriptAstNodeFree(pIter);
    }

    RTMemFree(pThis);
}

DECLHIDDEN(int) VDScriptCtxCallbacksRegister(VDSCRIPTCTX hScriptCtx, PCVDSCRIPTCALLBACK paCallbacks,
                                             unsigned cCallbacks, void *pvUser)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTCTXINT pThis = hScriptCtx;

    LogFlowFunc(("hScriptCtx=%p paCallbacks=%p cCallbacks=%u pvUser=%p\n",
                 pThis, paCallbacks, cCallbacks, pvUser));

    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertPtrReturn(paCallbacks, VERR_INVALID_POINTER);
    AssertReturn(cCallbacks > 0, VERR_INVALID_PARAMETER);

    /** @todo Unregister already registered callbacks in case of an error. */
    do
    {
        PVDSCRIPTFN pFn = NULL;

        if (RTStrSpaceGet(&pThis->hStrSpaceFn, paCallbacks->pszFnName))
        {
            rc = VERR_DUPLICATE;
            break;
        }

        pFn = (PVDSCRIPTFN)RTMemAllocZ(RT_UOFFSETOF_DYN(VDSCRIPTFN, aenmArgTypes[paCallbacks->cArgs]));
        if (!pFn)
        {
            rc = VERR_NO_MEMORY;
            break;
        }

        /** @todo Validate argument and returns types. */
        pFn->Core.pszString            = paCallbacks->pszFnName;
        pFn->Core.cchString            = strlen(pFn->Core.pszString);
        pFn->fExternal                 = true;
        pFn->Type.External.pfnCallback = paCallbacks->pfnCallback;
        pFn->Type.External.pvUser      = pvUser;
        pFn->enmTypeRetn               = paCallbacks->enmTypeReturn;
        pFn->cArgs                     = paCallbacks->cArgs;

        for (unsigned i = 0; i < paCallbacks->cArgs; i++)
            pFn->aenmArgTypes[i] = paCallbacks->paArgs[i];

        RTStrSpaceInsert(&pThis->hStrSpaceFn, &pFn->Core);
        cCallbacks--;
        paCallbacks++;
    }
    while (cCallbacks);

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

DECLHIDDEN(int) VDScriptCtxLoadScript(VDSCRIPTCTX hScriptCtx, const char *pszScript)
{
    int rc = VINF_SUCCESS;
    PVDSCRIPTCTXINT pThis = hScriptCtx;

    LogFlowFunc(("hScriptCtx=%p pszScript=%p\n", pThis, pszScript));

    AssertPtrReturn(pThis, VERR_INVALID_POINTER);
    AssertPtrReturn(pszScript, VERR_INVALID_POINTER);

    PVDTOKENIZER pTokenizer = vdScriptTokenizerCreate(pszScript);
    if (pTokenizer)
    {
        pThis->pTokenizer = pTokenizer;
        rc = vdScriptParseFromTokenizer(pThis);
        pThis->pTokenizer = NULL;
        RTMemFree(pTokenizer);
    }

    LogFlowFunc(("returns rc=%Rrc\n", rc));
    return rc;
}

DECLHIDDEN(int) VDScriptCtxCallFn(VDSCRIPTCTX hScriptCtx, const char *pszFnCall,
                                  PVDSCRIPTARG paArgs, unsigned cArgs)
{
    PVDSCRIPTCTXINT pThis = hScriptCtx;
    VDSCRIPTARG Ret;
    return vdScriptCtxInterprete(pThis, pszFnCall, paArgs, cArgs, &Ret);
}
