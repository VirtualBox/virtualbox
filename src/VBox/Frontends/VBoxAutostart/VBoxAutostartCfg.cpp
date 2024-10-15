/* $Id: VBoxAutostartCfg.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxAutostart - VirtualBox Autostart service, configuration parser.
 */

/*
 * Copyright (C) 2012-2024 Oracle and/or its affiliates.
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
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/process.h>
#include <iprt/stream.h>
#include <iprt/string.h>

#include "VBoxAutostart.h"


/*********************************************************************************************************************************
*   Constants And Macros, Structures and Typedefs                                                                                *
*********************************************************************************************************************************/

/**
 * Token type.
 */
typedef enum CFGTOKENTYPE
{
    /** Invalid token type. */
    CFGTOKENTYPE_INVALID = 0,
    /** Identifier. */
    CFGTOKENTYPE_ID,
    /** Comma. */
    CFGTOKENTYPE_COMMA,
    /** Equal sign. */
    CFGTOKENTYPE_EQUAL,
    /** Open curly brackets. */
    CFGTOKENTYPE_CURLY_OPEN,
    /** Closing curly brackets. */
    CFGTOKENTYPE_CURLY_CLOSING,
    /** End of file. */
    CFGTOKENTYPE_EOF,
    /** 32bit hack. */
    CFGTOKENTYPE_32BIT_HACK = 0x7fffffff
} CFGTOKENTYPE;
/** Pointer to a token type. */
typedef CFGTOKENTYPE *PCFGTOKENTYPE;
/** Pointer to a const token type. */
typedef const CFGTOKENTYPE *PCCFGTOKENTYPE;

/**
 * A token.
 */
typedef struct CFGTOKEN
{
    /** Type of the token. */
    CFGTOKENTYPE    enmType;
    /** Line number of the token. */
    unsigned        iLine;
    /** Starting character of the token in the stream. */
    size_t          cchStart;
    /** Type dependen token data. */
    union
    {
        /** Data for the ID type. */
        struct
        {
            /** Size of the id in characters, excluding the \0 terminator. */
            size_t  cchToken;
            /** Token data, variable size (given by cchToken member). */
            char    achToken[1];
        } Id;
    } u;
} CFGTOKEN;
/** Pointer to a token. */
typedef CFGTOKEN *PCFGTOKEN;
/** Pointer to a const token. */
typedef const CFGTOKEN *PCCFGTOKEN;

/**
 * Tokenizer instance data for the config data.
 */
typedef struct CFGTOKENIZER
{
    /** Config file handle. */
    PRTSTREAM hStrmConfig;
    /** String buffer for the current line we are operating in. */
    char      *pszLine;
    /** Size of the string buffer. */
    size_t     cbLine;
    /** Current position in the line. */
    char      *pszLineCurr;
    /** Current line in the config file. */
    unsigned   iLine;
    /** Current character of the line. */
    size_t     cchCurr;
    /** Flag whether the end of the config stream is reached. */
    bool       fEof;
    /** Pointer to the next token in the stream (used to peek). */
    PCFGTOKEN  pTokenNext;
} CFGTOKENIZER, *PCFGTOKENIZER;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/

/**
 * Free a config token.
 *
 * @param   pCfgTokenizer    The config tokenizer.
 * @param   pToken           The token to free.
 */
static void autostartConfigTokenFree(PCFGTOKENIZER pCfgTokenizer, PCFGTOKEN pToken)
{
    NOREF(pCfgTokenizer);
    RTMemFree(pToken);
}

/**
 * Reads the next line from the config stream.
 *
 * @returns VBox status code.
 * @param   pCfgTokenizer    The config tokenizer.
 */
static int autostartConfigTokenizerReadNextLine(PCFGTOKENIZER pCfgTokenizer)
{
    int rc = VINF_SUCCESS;

    if (pCfgTokenizer->fEof)
        return VERR_EOF;

    do
    {
        rc = RTStrmGetLine(pCfgTokenizer->hStrmConfig, pCfgTokenizer->pszLine,
                           pCfgTokenizer->cbLine);
        if (rc == VERR_BUFFER_OVERFLOW)
        {
            char *pszTmp;

            pCfgTokenizer->cbLine += 128;
            pszTmp = (char *)RTMemRealloc(pCfgTokenizer->pszLine, pCfgTokenizer->cbLine);
            if (pszTmp)
                pCfgTokenizer->pszLine = pszTmp;
            else
                rc = VERR_NO_MEMORY;
        }
    } while (rc == VERR_BUFFER_OVERFLOW);

    if (   RT_SUCCESS(rc)
        || rc == VERR_EOF)
    {
        pCfgTokenizer->iLine++;
        pCfgTokenizer->cchCurr = 1;
        pCfgTokenizer->pszLineCurr = pCfgTokenizer->pszLine;
        if (rc == VERR_EOF)
            pCfgTokenizer->fEof = true;
    }

    return rc;
}

/**
 * Get the next token from the config stream and create a token structure.
 *
 * @returns VBox status code.
 * @param   pCfgTokenizer    The config tokenizer data.
 * @param   pCfgTokenUse     Allocated token structure to use or NULL to allocate
 *                           a new one. It will bee freed if an error is encountered.
 * @param   ppCfgToken       Where to store the pointer to the next token on success.
 */
static int autostartConfigTokenizerCreateToken(PCFGTOKENIZER pCfgTokenizer,
                                               PCFGTOKEN pCfgTokenUse, PCFGTOKEN *ppCfgToken)
{
    const char *pszToken = NULL;
    size_t cchToken = 1;
    size_t cchAdvance = 0;
    CFGTOKENTYPE enmType = CFGTOKENTYPE_INVALID;
    int rc = VINF_SUCCESS;

    for (;;)
    {
        pszToken = pCfgTokenizer->pszLineCurr;

        /* Skip all spaces. */
        while (RT_C_IS_BLANK(*pszToken))
        {
            pszToken++;
            cchAdvance++;
        }

        /* Check if we have to read a new line. */
        if (   *pszToken == '\0'
            || *pszToken == '#')
        {
            rc = autostartConfigTokenizerReadNextLine(pCfgTokenizer);
            if (rc == VERR_EOF)
            {
                enmType = CFGTOKENTYPE_EOF;
                rc = VINF_SUCCESS;
                break;
            }
            else if (RT_FAILURE(rc))
                break;
            /* start from the beginning. */
            cchAdvance = 0;
        }
        else if (*pszToken == '=')
        {
            enmType = CFGTOKENTYPE_EQUAL;
            break;
        }
        else if (*pszToken == ',')
        {
            enmType = CFGTOKENTYPE_COMMA;
            break;
        }
        else if (*pszToken == '{')
        {
            enmType = CFGTOKENTYPE_CURLY_OPEN;
            break;
        }
        else if (*pszToken == '}')
        {
            enmType = CFGTOKENTYPE_CURLY_CLOSING;
            break;
        }
        else
        {
            const char *pszTmp = pszToken;
            cchToken = 0;
            enmType = CFGTOKENTYPE_ID;

            /* Get the complete token. */
            while (   RT_C_IS_ALNUM(*pszTmp)
                   || *pszTmp == '_'
                   || *pszTmp == '.')
            {
                pszTmp++;
                cchToken++;
            }
            break;
        }
    }

    Assert(RT_FAILURE(rc) || enmType != CFGTOKENTYPE_INVALID);

    if (RT_SUCCESS(rc))
    {
        /* Free the given token if it is an ID or the current one is an ID token. */
        if (   pCfgTokenUse
            && (   pCfgTokenUse->enmType == CFGTOKENTYPE_ID
                || enmType == CFGTOKENTYPE_ID))
        {
            autostartConfigTokenFree(pCfgTokenizer, pCfgTokenUse);
            pCfgTokenUse = NULL;
        }

        if (!pCfgTokenUse)
        {
            size_t cbToken = sizeof(CFGTOKEN);
            if (enmType == CFGTOKENTYPE_ID)
                cbToken += (cchToken + 1) * sizeof(char);

            pCfgTokenUse = (PCFGTOKEN)RTMemAllocZ(cbToken);
            if (!pCfgTokenUse)
                rc = VERR_NO_MEMORY;
        }

        if (RT_SUCCESS(rc))
        {
            /* Copy token data. */
            pCfgTokenUse->enmType  = enmType;
            pCfgTokenUse->cchStart = pCfgTokenizer->cchCurr;
            pCfgTokenUse->iLine    = pCfgTokenizer->iLine;
            if (enmType == CFGTOKENTYPE_ID)
            {
                pCfgTokenUse->u.Id.cchToken = cchToken;
                memcpy(pCfgTokenUse->u.Id.achToken, pszToken, cchToken);
            }
        }
        else if (pCfgTokenUse)
            autostartConfigTokenFree(pCfgTokenizer, pCfgTokenUse);

        if (RT_SUCCESS(rc))
        {
            /* Set new position in config stream. */
            pCfgTokenizer->pszLineCurr += cchToken + cchAdvance;
            pCfgTokenizer->cchCurr     += cchToken + cchAdvance;
            *ppCfgToken                 = pCfgTokenUse;
        }
    }

    return rc;
}

/**
 * Destroys the given config tokenizer.
 *
 * @param   pCfgTokenizer    The config tokenizer to destroy.
 */
static void autostartConfigTokenizerDestroy(PCFGTOKENIZER pCfgTokenizer)
{
    if (pCfgTokenizer->pszLine)
        RTMemFree(pCfgTokenizer->pszLine);
    if (pCfgTokenizer->hStrmConfig)
        RTStrmClose(pCfgTokenizer->hStrmConfig);
    if (pCfgTokenizer->pTokenNext)
        RTMemFree(pCfgTokenizer->pTokenNext);
    RTMemFree(pCfgTokenizer);
}

/**
 * Creates the config tokenizer from the given filename.
 *
 * @returns VBox status code.
 * @param   pszFilename    Config filename.
 * @param   ppCfgTokenizer Where to store the pointer to the config tokenizer on
 *                         success.
 */
static int autostartConfigTokenizerCreate(const char *pszFilename, PCFGTOKENIZER *ppCfgTokenizer)
{
    int rc = VINF_SUCCESS;
    PCFGTOKENIZER pCfgTokenizer = (PCFGTOKENIZER)RTMemAllocZ(sizeof(CFGTOKENIZER));

    if (pCfgTokenizer)
    {
        pCfgTokenizer->iLine = 0;
        pCfgTokenizer->cbLine = 128;
        pCfgTokenizer->pszLine = (char *)RTMemAllocZ(pCfgTokenizer->cbLine);
        if (pCfgTokenizer->pszLine)
        {
            rc = RTStrmOpen(pszFilename, "r", &pCfgTokenizer->hStrmConfig);
            if (RT_SUCCESS(rc))
            {
                rc = autostartConfigTokenizerReadNextLine(pCfgTokenizer);
                if (RT_SUCCESS(rc))
                    rc = autostartConfigTokenizerCreateToken(pCfgTokenizer, NULL,
                                                             &pCfgTokenizer->pTokenNext);
            }
        }
        else
            rc = VERR_NO_MEMORY;
    }
    else
        rc = VERR_NO_MEMORY;

    if (RT_SUCCESS(rc))
        *ppCfgTokenizer = pCfgTokenizer;
    else if (   RT_FAILURE(rc)
             && pCfgTokenizer)
        autostartConfigTokenizerDestroy(pCfgTokenizer);

    return rc;
}

/**
 * Return the next token from the config stream.
 *
 * @returns VBox status code.
 * @param   pCfgTokenizer   The config tokenizer.
 * @param   ppCfgToken      Where to store the next token.
 */
static int autostartConfigTokenizerGetNextToken(PCFGTOKENIZER pCfgTokenizer,
                                                PCFGTOKEN *ppCfgToken)
{
    *ppCfgToken = pCfgTokenizer->pTokenNext;
    return autostartConfigTokenizerCreateToken(pCfgTokenizer, NULL, &pCfgTokenizer->pTokenNext);
}

/**
 * Returns a stringified version of the token type.
 *
 * @returns Stringified version of the token type.
 * @param   enmType         Token type.
 */
static const char *autostartConfigTokenTypeToStr(CFGTOKENTYPE enmType)
{
    switch (enmType)
    {
        case CFGTOKENTYPE_COMMA:
            return ",";
        case CFGTOKENTYPE_EQUAL:
            return "=";
        case CFGTOKENTYPE_CURLY_OPEN:
            return "{";
        case CFGTOKENTYPE_CURLY_CLOSING:
            return "}";
        case CFGTOKENTYPE_EOF:
            return "<EOF>";
        case CFGTOKENTYPE_ID:
            return "<Identifier>";
        default:
            AssertFailed();
            return "<Invalid>";
    }
    /* not reached */
}

/**
 * Returns a stringified version of the token.
 *
 * @returns Stringified version of the token type.
 * @param   pToken         Token.
 */
static const char *autostartConfigTokenToString(PCFGTOKEN pToken)
{
    if (pToken->enmType == CFGTOKENTYPE_ID)
        return pToken->u.Id.achToken;
    else
        return autostartConfigTokenTypeToStr(pToken->enmType);
}

/**
 * Returns the length of the token in characters (without zero terminator).
 *
 * @returns Token length.
 * @param   pToken          Token.
 */
static size_t autostartConfigTokenGetLength(PCFGTOKEN pToken)
{
    switch (pToken->enmType)
    {
        case CFGTOKENTYPE_COMMA:
        case CFGTOKENTYPE_EQUAL:
        case CFGTOKENTYPE_CURLY_OPEN:
        case CFGTOKENTYPE_CURLY_CLOSING:
            return 1;
        case CFGTOKENTYPE_EOF:
            return 0;
        case CFGTOKENTYPE_ID:
            return strlen(pToken->u.Id.achToken);
        default:
            AssertFailed();
            return 0;
    }
    /* not reached */
}

/**
 * Log unexpected token error.
 *
 * @returns VBox status code (VERR_INVALID_PARAMETER).
 * @param   pToken          The token which caused the error.
 * @param   pszExpected     String of the token which was expected.
 */
static int autostartConfigTokenizerMsgUnexpectedToken(PCFGTOKEN pToken, const char *pszExpected)
{
    return autostartSvcLogErrorRc(VERR_INVALID_PARAMETER, "Unexpected token '%s' at %d:%d.%d, expected '%s'",
                                autostartConfigTokenToString(pToken),
                                pToken->iLine, pToken->cchStart,
                                pToken->cchStart + autostartConfigTokenGetLength(pToken) - 1, pszExpected);
}

/**
 * Verfies a token and consumes it.
 *
 * @returns VBox status code.
 * @param   pCfgTokenizer    The config tokenizer.
 * @param   pszTokenCheck    The token to check for.
 */
static int autostartConfigTokenizerCheckAndConsume(PCFGTOKENIZER pCfgTokenizer, CFGTOKENTYPE enmType)
{
    int rc = VINF_SUCCESS;
    PCFGTOKEN pCfgToken = NULL;

    rc = autostartConfigTokenizerGetNextToken(pCfgTokenizer, &pCfgToken);
    if (RT_SUCCESS(rc))
    {
        if (pCfgToken->enmType != enmType)
            return autostartConfigTokenizerMsgUnexpectedToken(pCfgToken, autostartConfigTokenTypeToStr(enmType));

        autostartConfigTokenFree(pCfgTokenizer, pCfgToken);
    }
    return rc;
}

/**
 * Consumes the next token in the stream.
 *
 * @returns VBox status code.
 * @param   pCfgTokenizer    Tokenizer instance data.
 */
static int autostartConfigTokenizerConsume(PCFGTOKENIZER pCfgTokenizer)
{
    int rc = VINF_SUCCESS;
    PCFGTOKEN pCfgToken = NULL;

    rc = autostartConfigTokenizerGetNextToken(pCfgTokenizer, &pCfgToken);
    if (RT_SUCCESS(rc))
        autostartConfigTokenFree(pCfgTokenizer, pCfgToken);

    return rc;
}

/**
 * Returns the start of the next token without consuming it.
 *
 * @returns The next token without consuming it.
 * @param   pCfgTokenizer    Tokenizer instance data.
 */
DECLINLINE(PCFGTOKEN) autostartConfigTokenizerPeek(PCFGTOKENIZER pCfgTokenizer)
{
    return pCfgTokenizer->pTokenNext;
}

/**
 * Check whether the next token is equal to the given one.
 *
 * @returns true if the next token in the stream is equal to the given one
 *          false otherwise.
 * @param   pszToken    The token to check for.
 */
DECLINLINE(bool) autostartConfigTokenizerPeekIsEqual(PCFGTOKENIZER pCfgTokenizer, CFGTOKENTYPE enmType)
{
    PCFGTOKEN pToken = autostartConfigTokenizerPeek(pCfgTokenizer);
    return pToken->enmType == enmType;
}

/**
 * Parse a key value node and returns the AST.
 *
 * @returns VBox status code.
 * @param   pCfgTokenizer    The tokenizer for the config stream.
 * @param   pszKey           The key for the pair.
 * @param   ppCfgAst         Where to store the resulting AST on success.
 */
static int autostartConfigParseValue(PCFGTOKENIZER pCfgTokenizer, const char *pszKey,
                                     PCFGAST *ppCfgAst)
{
    int rc = VINF_SUCCESS;
    PCFGTOKEN pToken = NULL;

    rc = autostartConfigTokenizerGetNextToken(pCfgTokenizer, &pToken);
    if (   RT_SUCCESS(rc)
        && pToken->enmType == CFGTOKENTYPE_ID)
    {
        PCFGAST pCfgAst = NULL;

        pCfgAst = (PCFGAST)RTMemAllocZ(RT_UOFFSETOF_DYN(CFGAST, u.KeyValue.aszValue[pToken->u.Id.cchToken + 1]));
        if (!pCfgAst)
            return VERR_NO_MEMORY;

        pCfgAst->enmType = CFGASTNODETYPE_KEYVALUE;
        pCfgAst->pszKey  = RTStrDup(pszKey);
        if (!pCfgAst->pszKey)
        {
            RTMemFree(pCfgAst);
            return VERR_NO_MEMORY;
        }

        memcpy(pCfgAst->u.KeyValue.aszValue, pToken->u.Id.achToken, pToken->u.Id.cchToken);
        pCfgAst->u.KeyValue.cchValue = pToken->u.Id.cchToken;
        *ppCfgAst = pCfgAst;
    }
    else
        rc = autostartConfigTokenizerMsgUnexpectedToken(pToken, "non reserved token");

    return rc;
}

/**
 * Parses a compound node constructing the AST and returning it on success.
 *
 * @returns VBox status code.
 * @param   pCfgTokenizer    The tokenizer for the config stream.
 * @param   pszScopeId       The scope ID of the compound node.
 * @param   ppCfgAst         Where to store the resulting AST on success.
 */
static int autostartConfigParseCompoundNode(PCFGTOKENIZER pCfgTokenizer, const char *pszScopeId,
                                            PCFGAST *ppCfgAst)
{
    unsigned cAstNodesMax = 10;
    PCFGAST pCfgAst = (PCFGAST)RTMemAllocZ(RT_UOFFSETOF_DYN(CFGAST, u.Compound.apAstNodes[cAstNodesMax]));
    if (!pCfgAst)
        return VERR_NO_MEMORY;

    pCfgAst->enmType = CFGASTNODETYPE_COMPOUND;
    pCfgAst->u.Compound.cAstNodes = 0;
    pCfgAst->pszKey  = RTStrDup(pszScopeId);
    if (!pCfgAst->pszKey)
    {
        RTMemFree(pCfgAst);
        return VERR_NO_MEMORY;
    }

    int rc = VINF_SUCCESS;
    do
    {
        PCFGTOKEN pToken = NULL;
        PCFGAST pAstNode = NULL;

        if (   autostartConfigTokenizerPeekIsEqual(pCfgTokenizer, CFGTOKENTYPE_CURLY_CLOSING)
            || autostartConfigTokenizerPeekIsEqual(pCfgTokenizer, CFGTOKENTYPE_EOF))
            break;

        rc = autostartConfigTokenizerGetNextToken(pCfgTokenizer, &pToken);
        if (   RT_SUCCESS(rc)
            && pToken->enmType == CFGTOKENTYPE_ID)
        {
            /* Next must be a = token in all cases at this place. */
            rc = autostartConfigTokenizerCheckAndConsume(pCfgTokenizer, CFGTOKENTYPE_EQUAL);
            if (RT_SUCCESS(rc))
            {
                /* Check whether this is a compound node. */
                if (autostartConfigTokenizerPeekIsEqual(pCfgTokenizer, CFGTOKENTYPE_CURLY_OPEN))
                {
                    rc = autostartConfigTokenizerConsume(pCfgTokenizer);
                    if (RT_SUCCESS(rc))
                        rc = autostartConfigParseCompoundNode(pCfgTokenizer, pToken->u.Id.achToken,
                                                              &pAstNode);

                    if (RT_SUCCESS(rc))
                        rc = autostartConfigTokenizerCheckAndConsume(pCfgTokenizer, CFGTOKENTYPE_CURLY_CLOSING);
                }
                else
                    rc = autostartConfigParseValue(pCfgTokenizer, pToken->u.Id.achToken,
                                                   &pAstNode);
            }
        }
        else if (RT_SUCCESS(rc))
            rc = autostartConfigTokenizerMsgUnexpectedToken(pToken, "non reserved token");

        /* Add to the current compound node. */
        if (RT_SUCCESS(rc))
        {
            if (pCfgAst->u.Compound.cAstNodes >= cAstNodesMax)
            {
                cAstNodesMax += 10;

                PCFGAST pCfgAstNew = (PCFGAST)RTMemRealloc(pCfgAst, RT_UOFFSETOF_DYN(CFGAST, u.Compound.apAstNodes[cAstNodesMax]));
                if (!pCfgAstNew)
                    rc = VERR_NO_MEMORY;
                else
                    pCfgAst = pCfgAstNew;
            }

            if (RT_SUCCESS(rc))
            {
                pCfgAst->u.Compound.apAstNodes[pCfgAst->u.Compound.cAstNodes] = pAstNode;
                pCfgAst->u.Compound.cAstNodes++;
            }
        }

        autostartConfigTokenFree(pCfgTokenizer, pToken);

    } while (RT_SUCCESS(rc));

    if (RT_SUCCESS(rc))
        *ppCfgAst = pCfgAst;
    else
        autostartConfigAstDestroy(pCfgAst);

    return rc;
}

DECLHIDDEN(int) autostartParseConfig(const char *pszFilename, PCFGAST *ppCfgAst)
{
    PCFGTOKENIZER pCfgTokenizer = NULL;
    int rc = VINF_SUCCESS;
    PCFGAST pCfgAst = NULL;

    AssertPtrReturn(pszFilename, VERR_INVALID_POINTER);
    AssertPtrReturn(ppCfgAst, VERR_INVALID_POINTER);

    rc = autostartConfigTokenizerCreate(pszFilename, &pCfgTokenizer);
    if (RT_SUCCESS(rc))
    {
        rc = autostartConfigParseCompoundNode(pCfgTokenizer, "", &pCfgAst);
        if (RT_SUCCESS(rc))
            rc = autostartConfigTokenizerCheckAndConsume(pCfgTokenizer, CFGTOKENTYPE_EOF);
    }

    if (pCfgTokenizer)
        autostartConfigTokenizerDestroy(pCfgTokenizer);

    if (RT_SUCCESS(rc))
        *ppCfgAst = pCfgAst;

    return rc;
}

DECLHIDDEN(void) autostartConfigAstDestroy(PCFGAST pCfgAst)
{
    AssertPtrReturnVoid(pCfgAst);

    switch (pCfgAst->enmType)
    {
        case CFGASTNODETYPE_KEYVALUE:
        {
            RTMemFree(pCfgAst);
            break;
        }
        case CFGASTNODETYPE_COMPOUND:
        {
            for (unsigned i = 0; i < pCfgAst->u.Compound.cAstNodes; i++)
                autostartConfigAstDestroy(pCfgAst->u.Compound.apAstNodes[i]);
            RTMemFree(pCfgAst);
            break;
        }
        case CFGASTNODETYPE_LIST:
            RT_FALL_THROUGH();
        default:
            AssertMsgFailed(("Invalid AST node type %d\n", pCfgAst->enmType));
    }
}

DECLHIDDEN(PCFGAST) autostartConfigAstGetByName(PCFGAST pCfgAst, const char *pszName)
{
    if (!pCfgAst)
        return NULL;

    AssertReturn(pCfgAst->enmType == CFGASTNODETYPE_COMPOUND, NULL);

    for (unsigned i = 0; i < pCfgAst->u.Compound.cAstNodes; i++)
    {
        PCFGAST pNode = pCfgAst->u.Compound.apAstNodes[i];

        if (!RTStrCmp(pNode->pszKey, pszName))
            return pNode;
    }

    return NULL;
}

