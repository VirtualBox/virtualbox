/* $Id: script.h 109507 2025-05-12 17:53:56Z vadim.galitsyn@oracle.com $ */
/** @file
 * IPRT - RTScript, Script language support in IPRT.
 */

/*
 * Copyright (C) 2024 Oracle and/or its affiliates.
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

#ifndef IPRT_INCLUDED_script_h
#define IPRT_INCLUDED_script_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif

#include <iprt/cdefs.h>
#include <iprt/types.h>
#include <iprt/strcache.h>
#include <iprt/scriptbase.h>
//#include <iprt/scriptast.h>

RT_C_DECLS_BEGIN

/** @defgroup grp_rt_script    RTScript - IPRT scripting language support
 * @ingroup grp_rt
 *
 * The scripting APIs provide a simple framework to implement simple scripting
 * languages. They are meant to provide building blocks which can be put together
 * in an easy way to add scripting capablities to the software using it.
 *
 * The API is oriented on the traditional compiler pipeline providing sub APIs for the following
 * parts:
 *      - RTScriptLex*:   For building a lexer to generate tokens from an input character stream.
 *      - RTScriptTs*:    A simple type system providing a way to get type storage sizes and alignments.
 *      - RTScriptPs*:    For maintaining the required state for the complete script and provide
 *                        a way to check for correct typing.
 *      - RTScriptAst*:   Providing helpers and definitions for the abstract syntax tree.
 *      - RTScriptParse*: For building parsers which generate ASTs.
 *      - RTScriptVm*:    Providing a simple bytecode VM which takes a checked program state
 *                        converting it into bytecode and executing it.
 *
 * Note: Only RTScriptLex is partially implemented right now!
 * @{
 */

/** @defgroup grp_rt_script_lex  Scripting lexer support
 *
 * This part provides support for lexing input and generating tokens which can be
 * digested by a parser.
 *
 * @{
 */

/**
 * The lexer token type.
 */
typedef enum RTSCRIPTLEXTOKTYPE
{
    /** Invalid type. */
    RTSCRIPTLEXTOKTYPE_INVALID = 0,
    /** Identifier. */
    RTSCRIPTLEXTOKTYPE_IDENTIFIER,
    /** Numerical constant. */
    RTSCRIPTLEXTOKTYPE_NUMBER,
    /** String literal. */
    RTSCRIPTLEXTOKTYPE_STRINGLIT,
    /** An operator (unary or binary). */
    RTSCRIPTLEXTOKTYPE_OPERATOR,
    /** Some predefined keyword. */
    RTSCRIPTLEXTOKTYPE_KEYWORD,
    /** Some punctuator. */
    RTSCRIPTLEXTOKTYPE_PUNCTUATOR,
    /** A single line comment. */
    RTSCRIPTLEXTOKTYPE_COMMENT_SINGLE_LINE,
    /** A multi line comment. */
    RTSCRIPTLEXTOKTYPE_COMMENT_MULTI_LINE,
    /** Special error token, conveying an error message from the lexer. */
    RTSCRIPTLEXTOKTYPE_ERROR,
    /** End of stream token. */
    RTSCRIPTLEXTOKTYPE_EOS
} RTSCRIPTLEXTOKTYPE;
/** Pointer to a lexer token type. */
typedef RTSCRIPTLEXTOKTYPE *PRTSCRIPTLEXTOKTYPE;


/**
 * Lexer token number type.
 */
typedef enum RTSCRIPTLEXTOKNUMTYPE
{
    /** Invalid token number type. */
    RTSCRIPTLEXTOKNUMTYPE_INVALID = 0,
    /** Natural number (all positive upwards including 0). */
    RTSCRIPTLEXTOKNUMTYPE_NATURAL,
    /** Integers (natural numbers and their additive inverse). */
    RTSCRIPTLEXTOKNUMTYPE_INTEGER,
    /** Real numbers. */
    RTSCRIPTLEXTOKNUMTYPE_REAL
} RTSCRIPTLEXTOKNUMTYPE;


/**
 * Lexer exact token match descriptor.
 */
typedef struct RTSCRIPTLEXTOKMATCH
{
    /** Matching string. */
    const char         *pszMatch;
    /** Size if of the matching string in characters excluding the zero terminator. */
    size_t             cchMatch;
    /** Resulting token type. */
    RTSCRIPTLEXTOKTYPE enmTokType;
    /** Whether the token can be the beginning of an identifer
     * and to check whether the identifer has a longer match. */
    bool               fMaybeIdentifier;
    /** User defined value when the token matched. */
    uint64_t           u64Val;
} RTSCRIPTLEXTOKMATCH;
/** Pointer to a lexer exact token match descriptor. */
typedef RTSCRIPTLEXTOKMATCH *PRTSCRIPTLEXTOKMATCH;
/** Pointer to a const lexer exact token match descriptor. */
typedef const RTSCRIPTLEXTOKMATCH *PCRTSCRIPTLEXTOKMATCH;


/**
 * Lexer token.
 */
typedef struct RTSCRIPTLEXTOKEN
{
    /** Token type. */
    RTSCRIPTLEXTOKTYPE           enmType;
    /** Position in the source buffer where the token started matching. */
    RTSCRIPTPOS                  PosStart;
    /** Position in the source buffer where the token ended matching. */
    RTSCRIPTPOS                  PosEnd;
    /** Data based on the token type. */
    union
    {
        /** Identifier. */
        struct
        {
            /** Pointer to the start of the identifier. */
            const char           *pszIde;
            /** Number of characters for the identifier excluding the null terminator. */
            size_t               cchIde;
        } Id;
        /** Numerical constant. */
        struct
        {
            /** Flag whether the number is negative. */
            RTSCRIPTLEXTOKNUMTYPE enmType;
            /** Optimal storage size for the value (1, 2, 4 or 8 bytes). */
            uint32_t              cBytes;
            /** Type dependent data. */
            union
            {
                /** For natural numbers. */
                uint64_t          u64;
                /** For integer numbers. */
                int64_t           i64;
                /** Real numbers. */
                RTFLOAT64U        r64;
            } Type;
        } Number;
        /** String literal */
        struct
        {
            /** Pointer to the start of the string constant. */
            const char            *pszString;
            /** Number of characters of the string, including the null terminator. */
            size_t                cchString;
        } StringLit;
        /** Operator */
        struct
        {
            /** Pointer to the operator descriptor. */
            PCRTSCRIPTLEXTOKMATCH pOp;
        } Operator;
        /** Keyword. */
        struct
        {
            /** Pointer to the keyword descriptor. */
            PCRTSCRIPTLEXTOKMATCH pKeyword;
        } Keyword;
        /** Punctuator. */
        struct
        {
            /** Pointer to the matched punctuator descriptor. */
            PCRTSCRIPTLEXTOKMATCH pPunctuator;
        } Punctuator;
        /** Comment */
        struct
        {
            /** Pointer to the start of the comment (including the symbols starting the comment). */
            const char            *pszComment;
            /** Number of characters of the comment, including the null terminator. */
            size_t                cchComment;
        } Comment;
        /** Error. */
        struct
        {
            /** Pointer to the internal error info structure, readonly. */
            PCRTERRINFO           pErr;
        } Error;
    } Type;
} RTSCRIPTLEXTOKEN;
/** Pointer to a script token. */
typedef RTSCRIPTLEXTOKEN *PRTSCRIPTLEXTOKEN;
/** Pointer to a const script token. */
typedef const RTSCRIPTLEXTOKEN *PCRTSCRIPTLEXTOKEN;


/** Opaque lexer handle. */
typedef struct RTSCRIPTLEXINT *RTSCRIPTLEX;
/** Pointer to a lexer handle. */
typedef RTSCRIPTLEX *PRTSCRIPTLEX;


/**
 * Production rule callback.
 *
 * @returns IPRT status code.
 * @param   hScriptLex    The lexer handle.
 * @param   ch            The character which caused the production rule to be called.
 * @param   pToken        The token to fill in.
 * @param   pvUser        Opaque user data.
 */
typedef DECLCALLBACKTYPE(int, FNRTSCRIPTLEXPROD,(RTSCRIPTLEX hScriptLex, char ch, PRTSCRIPTLEXTOKEN pToken, void *pvUser));
/** Pointer to a production rule callback. */
typedef FNRTSCRIPTLEXPROD *PFNRTSCRIPTLEXPROD;

/**
 * Lexer rule.
 */
typedef struct RTSCRIPTLEXRULE
{
    /** First character for starting the production rule. */
    char                 chStart;
    /** Last character for starting the production rule. */
    char                 chEnd;
    /** Flags for this rule. */
    uint32_t             fFlags;
    /** The producer to call. */
    PFNRTSCRIPTLEXPROD   pfnProd;
    /** Opaque user data passed to the production rule. */
    void                 *pvUser;
} RTSCRIPTLEXRULE;
/** Pointer to a lexer rule. */
typedef RTSCRIPTLEXRULE *PRTSCRIPTLEXRULE;
/** Pointer to a const lexer rule. */
typedef const RTSCRIPTLEXRULE *PCRTSCRIPTLEXRULE;

/** Default rule flags. */
#define RTSCRIPT_LEX_RULE_DEFAULT        0
/** Consume the first character before calling the producer. */
#define RTSCRIPT_LEX_RULE_CONSUME RT_BIT(0)

/**
 * Lexer config.
 */
typedef struct RTSCRIPTLEXCFG
{
    /** Config name. */
    const char            *pszName;
    /** Config description. */
    const char            *pszDesc;
    /** Flags for the lexer. */
    uint32_t              fFlags;
    /** Set of whitespace characters, excluding newline. NULL indicates default characters.
     * " " | "\t". */
    const char            *pszWhitespace;
    /** Set of newline characters, NULL indicates
     * default characters including "\n" | "\r\n". */
    const char            **papszNewline;
    /** Start for a multiline comment, NULL indicates no support for multi line comments. */
    const char            **papszCommentMultiStart;
    /** End for a multiline comment, NULL indicates no support for multi line comments. */
    const char            **papszCommentMultiEnd;
    /** Start of single line comment, NULL indicates no support for single line comments. */
    const char            **papszCommentSingleStart;
    /** Exact token match descriptor table, optional. Must be terminated with a NULL entry. */
    PCRTSCRIPTLEXTOKMATCH paTokMatches;
    /** Pointer to the rule table, optional. */
    PCRTSCRIPTLEXRULE     paRules;
    /** The default rule to call when no other rule applies, optional. */
    PFNRTSCRIPTLEXPROD    pfnProdDef;
    /** Opaque user data for default production rule. */
    void                  *pvProdDefUser;
} RTSCRIPTLEXCFG;
/** Pointer to a lexer config. */
typedef RTSCRIPTLEXCFG *PRTSCRIPTLEXCFG;
/** Pointer to a const lexer config. */
typedef const RTSCRIPTLEXCFG *PCRTSCRIPTLEXCFG;

/** Default lexer config flags. */
#define RTSCRIPT_LEX_CFG_F_DEFAULT                0
/** Case insensitive lexing, keywords and so on must be used lowercase to match
 * as the lexer will convert everything to lowercase internally. */
#define RTSCRIPT_LEX_CFG_F_CASE_INSENSITIVE_LOWER RT_BIT(0)
/** Case insensitive lexing, keywords and so on must be used uppercase to match
 * as the lexer will convert everything to uppercase internally. */
#define RTSCRIPT_LEX_CFG_F_CASE_INSENSITIVE_UPPER RT_BIT(1)
/** Comments are not skipped but passed back as tokens. */
#define RTSCRIPT_LEX_CFG_F_COMMENTS_AS_TOKENS     RT_BIT(2)


/** Default character conversions (converting to lower case when the case insensitive flag is set). */
#define RTSCRIPT_LEX_CONV_F_DEFAULT        0
/** Don't apply any conversion but just return the character as read from the input. */
#define RTSCRIPT_LEX_CONV_F_NOTHING        RT_BIT(0)

/**
 * Lexer reader callback.
 *
 * @returns IPRT status code.
 * @retval  VINF_EOF if the end of the input was reached.
 * @param   hScriptLex      The lexer handle.
 * @param   offBuf          Offset from the start to read from.
 * @param   pchBuf          Where to store the read data.
 * @param   cchBuf          How much to read at maxmimum.
 * @param   pcchRead        Where to store the amount of bytes read.
 * @param   pvUser          Opaque user data passed when creating the lexer.
 */
typedef DECLCALLBACKTYPE(int, FNRTSCRIPTLEXRDR, (RTSCRIPTLEX hScriptLex, size_t offBuf, char *pchBuf, size_t cchBuf,
                                                 size_t *pcchRead, void *pvUser));
/** Pointer to a lexer reader callback. */
typedef FNRTSCRIPTLEXRDR *PFNRTSCRIPTLEXRDR;


/**
 * Lexer destructor callback.
 *
 * @param   hScriptLex      The lexer handle.
 * @param   pvUser          Opaque user data passed when creating the lexer.
 */
typedef DECLCALLBACKTYPE(void, FNRTSCRIPTLEXDTOR,(RTSCRIPTLEX hScriptLex, void *pvUser));
/** Pointer to a lexer destructor callback. */
typedef FNRTSCRIPTLEXDTOR *PFNRTSCRIPTLEXDTOR;


/**
 * Creates a new lexer with the given reader and config.
 *
 * @returns IPRT status code.
 * @param   phScriptLex            Where to store the handle to the lexer on success.
 * @param   pfnReader              The read to use for reading chunks of the input.
 * @param   pfnDtor                Destructor callback to call when the lexer is destroyed.
 * @param   pvUser                 Opaque user data passed to reader.
 * @param   cchBuf                 Buffer hint, if 0 a default is chosen.
 * @param   phStrCacheId           Where to store the pointer to the string cache containing all
 *                                 scanned identifiers on success, optional.
 *                                 If not NULL the string cache must be freed by the caller when not used
 *                                 anymore.
 * @param   phStrCacheStringLit    Where to store the pointer to the string cache containing all
 *                                 scanned string literals on success, optional.
 *                                 If not NULL the string cache must be freed by the caller when not used
 *                                 anymore.
 * @param   phStrCacheComments     Where to store the pointer to the string cache containing all
 *                                 comments on success when RTSCRIPT_LEX_CFG_F_COMMENTS_AS_TOKENS
 *                                 is given, optional.
 *                                 If not NULL the string cache must be freed by the caller when not used
 *                                 anymore.
 * @param   pCfg                   The lexer config to use for identifying the different tokens.
 */
RTDECL(int) RTScriptLexCreateFromReader(PRTSCRIPTLEX phScriptLex, PFNRTSCRIPTLEXRDR pfnReader,
                                        PFNRTSCRIPTLEXDTOR pfnDtor, void *pvUser,
                                        size_t cchBuf, PRTSTRCACHE phStrCacheId, PRTSTRCACHE phStrCacheStringLit,
                                        PRTSTRCACHE phStrCacheComments, PCRTSCRIPTLEXCFG pCfg);


/**
 * Creates a new lexer for the given input string and config.
 *
 * @returns IPRT status code.
 * @param   phScriptLex            Where to store the handle to the lexer on success.
 * @param   pszSrc                 The input string to scan.
 * @param   phStrCacheId           Where to store the pointer to the string cache containing all
 *                                 scanned identifiers on success, optional.
 *                                 If not NULL the string cache must be freed by the caller when not used
 *                                 anymore.
 * @param   phStrCacheStringLit    Where to store the pointer to the string cache containing all
 *                                 scanned string literals on success, optional.
 *                                 If not NULL the string cache must be freed by the caller when not used
 *                                 anymore.
 * @param   phStrCacheComments     Where to store the pointer to the string cache containing all
 *                                 comments on success when RTSCRIPT_LEX_CFG_F_COMMENTS_AS_TOKENS
 *                                 is given, optional.
 *                                 If not NULL the string cache must be freed by the caller when not used
 *                                 anymore.
 * @param   pCfg                   The lexer config to use for identifying the different tokens.
 */
RTDECL(int) RTScriptLexCreateFromString(PRTSCRIPTLEX phScriptLex, const char *pszSrc, PRTSTRCACHE phStrCacheId,
                                        PRTSTRCACHE phStrCacheStringLit, PRTSTRCACHE phStrCacheComments,
                                        PCRTSCRIPTLEXCFG pCfg);


/**
 * Creates a new lexer from the given filename and config.
 *
 * @returns IPRT status code.
 * @param   phScriptLex            Where to store the handle to the lexer on success.
 * @param   pszFilename            The filename of the input.
 * @param   phStrCacheId           Where to store the pointer to the string cache containing all
 *                                 scanned identifiers on success, optional.
 *                                 If not NULL the string cache must be freed by the caller when not used
 *                                 anymore.
 * @param   phStrCacheStringLit    Where to store the pointer to the string cache containing all
 *                                 scanned string literals on success, optional.
 *                                 If not NULL the string cache must be freed by the caller when not used
 *                                 anymore.
 * @param   phStrCacheComments     Where to store the pointer to the string cache containing all
 *                                 comments on success when RTSCRIPT_LEX_CFG_F_COMMENTS_AS_TOKENS
 *                                 is given, optional.
 *                                 If not NULL the string cache must be freed by the caller when not used
 *                                 anymore.
 * @param   pCfg                   The lexer config to use for identifying the different tokens.
 */
RTDECL(int) RTScriptLexCreateFromFile(PRTSCRIPTLEX phScriptLex, const char *pszFilename, PRTSTRCACHE phStrCacheId,
                                      PRTSTRCACHE phStrCacheStringLit, PRTSTRCACHE phStrCacheComments,
                                      PCRTSCRIPTLEXCFG pCfg);


/**
 * Destroys the given lexer handle.
 *
 * @param   hScriptLex             The lexer handle to destroy.
 */
RTDECL(void) RTScriptLexDestroy(RTSCRIPTLEX hScriptLex);


/**
 * Queries the current identified token.
 *
 * @returns IPRT status code.
 * @param   hScriptLex             The lexer handle.
 * @param   ppToken                Where to store the pointer to the token on success.
 */
RTDECL(int) RTScriptLexQueryToken(RTSCRIPTLEX hScriptLex, PCRTSCRIPTLEXTOKEN *ppToken);


/**
 * Returns the current token type.
 *
 * @returns Current token type.
 * @param   hScriptLex             The lexer handle.
 */
RTDECL(RTSCRIPTLEXTOKTYPE) RTScriptLexGetTokenType(RTSCRIPTLEX hScriptLex);


/**
 * Returns the next token type.
 *
 * @returns Next token type.
 * @param   hScriptLex             The lexer handle.
 */
RTDECL(RTSCRIPTLEXTOKTYPE) RTScriptLexPeekNextTokenType(RTSCRIPTLEX hScriptLex);


/**
 * Consumes the current token and moves to the next one.
 *
 * @returns Pointer to the next token.
 * @param   hScriptLex             The lexer handle.
 */
RTDECL(PCRTSCRIPTLEXTOKEN) RTScriptLexConsumeToken(RTSCRIPTLEX hScriptLex);


/**
 * Consumes the current input characters and moves to the next one.
 *
 * @returns Returns the next character in the input stream.
 * @retval  0 indicates end of stream.
 * @param   hScriptLex             The lexer handle.
 */
RTDECL(char) RTScriptLexConsumeCh(RTSCRIPTLEX hScriptLex);


/**
 * Consumes the current input characters and moves to the next one - extended version.
 *
 * @returns Returns the next character in the input stream.
 * @retval  0 indicates end of stream.
 * @param   hScriptLex             The lexer handle.
 * @param   fFlags                 Flags controlling some basic conversions of characters,
 *                                 combination of RTSCRIPT_LEX_CONV_F_*.
 */
RTDECL(char) RTScriptLexConsumeChEx(RTSCRIPTLEX hScriptLex, uint32_t fFlags);


/**
 * Returns the character at the curren input position.
 *
 * @returns Character at the current position in the input
 * @retval  0 indicates end of stream.
 * @param   hScriptLex             The lexer handle.
 */
RTDECL(char) RTScriptLexGetCh(RTSCRIPTLEX hScriptLex);


/**
 * Returns the character at the curren input position - extended version.
 *
 * @returns Character at the current position in the input
 * @retval  0 indicates end of stream.
 * @param   hScriptLex             The lexer handle.
 * @param   fFlags                 Flags controlling some basic conversions of characters,
 *                                 combination of RTSCRIPT_LEX_CONV_F_*.
 */
RTDECL(char) RTScriptLexGetChEx(RTSCRIPTLEX hScriptLex, uint32_t fFlags);


/**
 * Returns the current character in the input without moving to the next one.
 *
 * @returns Returns the current character.
 * @retval  0 indicates end of stream.
 * @param   hScriptLex             The lexer handle.
 * @param   idx                    Offset to peek at, 0 behaves like rtScriptLexGetCh().
 */
RTDECL(char) RTScriptLexPeekCh(RTSCRIPTLEX hScriptLex, unsigned idx);


/**
 * Returns the current character in the input without moving to the next one - extended version.
 *
 * @returns Returns the current character.
 * @retval  0 indicates end of stream.
 * @param   hScriptLex             The lexer handle.
 * @param   idx                    Offset to peek at, 0 behaves like rtScriptLexGetCh().
 * @param   fFlags                 Flags controlling some basic conversions of characters,
 *                                 combination of RTSCRIPT_LEX_CONV_F_*.
 */
RTDECL(char) RTScriptLexPeekChEx(RTSCRIPTLEX hScriptLex, unsigned idx, uint32_t fFlags);


/**
 * Skips everything declared as whitespace, including comments and newlines.
 *
 * @param   hScriptLex             The lexer handle.
 */
RTDECL(void) RTScriptLexSkipWhitespace(RTSCRIPTLEX hScriptLex);


/** @defgroup grp_rt_script_lex_builtin    Builtin helpers to scan numbers, string literals, ...
 * @{ */

/**
 * Produces a numerical constant token from the number starting at the current position in the
 * input stream on success or an appropriate error token.
 *
 * @returns IPRT status code.
 * @param   hScriptLex    The lexer handle.
 * @param   uBase         The base to parse the number in.
 * @param   fAllowReal    Flag whether to allow parsing real numbers using the following
 *                        layout [+-][0-9]*[.][e][+-][0-9]*.
 * @param   pTok          The token to fill in.
 */
RTDECL(int) RTScriptLexScanNumber(RTSCRIPTLEX hScriptLex, uint8_t uBase, bool fAllowReal,
                                  PRTSCRIPTLEXTOKEN pTok);

/**
 * Production rule to create an identifier token with the given set of allowed characters.
 *
 * @returns IPRT status code.
 * @param   hScriptLex    The lexer handle.
 * @param   ch            The first character of the identifier.
 * @param   pTok          The token to fill in.
 * @param   pvUser        Opaque user data, must point to the allowed set of characters for identifiers as a
 *                        zero terminated string. NULL will revert to a default set of characters including
 *                        [_a-zA-Z0-9]*
 *
 * @note This version will allow a maximum identifier length of 512 characters (should be plenty).
 *       More characters will produce an error token. Must be used with the RTSCRIPT_LEX_RULE_CONSUME
 *       flag for the first character.
 */
RTDECL(int) RTScriptLexScanIdentifier(RTSCRIPTLEX hScriptLex, char ch, PRTSCRIPTLEXTOKEN pTok, void *pvUser);


/**
 * Production rule to scan string literals conforming to the C standard.
 *
 * @returns IPRT status code.
 * @param   hScriptLex    The lexer handle.
 * @param   ch            The character starting the rule, unused.
 * @param   pTok          The token to fill in.
 * @param   pvUser        Opaque user data, unused.
 *
 * @note The RTSCRIPT_LEX_RULE_CONSUME must be used (or omitted) such that the current character
 *       in the input denotes the start of the string literal. The resulting literal is added to the respective
 *       cache on success.
 */
RTDECL(int) RTScriptLexScanStringLiteralC(RTSCRIPTLEX hScriptLex, char ch, PRTSCRIPTLEXTOKEN pTok, void *pvUser);


/**
 * Production rule to scan string literals for pascal like languages, without support for escape
 * sequences and where a ' is denoted by ''.
 *
 * @returns IPRT status code.
 * @param   hScriptLex    The lexer handle.
 * @param   ch            The character starting the rule, unused.
 * @param   pTok          The token to fill in.
 * @param   pvUser        Opaque user data, unused.
 *
 * @note The RTSCRIPT_LEX_RULE_CONSUME must be used (or omitted) such that the current character
 *       in the input denotes the start of the string literal. The resulting literal is added to the respective
 *       cache on success.
 */
RTDECL(int) RTScriptLexScanStringLiteralPascal(RTSCRIPTLEX hScriptLex, char ch, PRTSCRIPTLEXTOKEN pTok, void *pvUser);


/**
 * Produces an error token with the given message, used for custom lexer rule implementations.
 *
 * @returns IPRT status code.
 * @param   hScriptLex             The lexer handle.
 * @param   pTok                   The token to fill.
 * @param   rc                     The status code to use in the message.
 * @param   pszMsg                 The format string for the error message.
 * @param   ...                    Arguments to the format string.
 */
RTDECL(int) RTScriptLexProduceTokError(RTSCRIPTLEX hScriptLex, PRTSCRIPTLEXTOKEN pTok, int rc, const char *pszMsg, ...);


/**
 * Produces an identifier token with the given identifier, used for custom lexer rule implementations.
 *
 * @returns IPRT status code.
 * @param   hScriptLex             The lexer handle.
 * @param   pTok                   The token to fill.
 * @param   pszIde                 The identifier to add.
 * @param   cchIde                 Number of characters in the identifier.
 */
RTDECL(int) RTScriptLexProduceTokIde(RTSCRIPTLEX hScriptLex, PRTSCRIPTLEXTOKEN pTok, const char *pszIde, size_t cchIde);
/** @} */

/** @} */


#if 0 /* Later, maybe */

/** @defgroup grp_rt_script_typesys Scripting language type system API.
 *
 * @{
 */

/**
 * Type class.
 */
typedef enum RTSCRIPTTSTYPECLASS
{
    /** Invalid. */
    RTSCRIPTTSTYPECLASS_INVALID = 0,
    /** A native type. */
    RTSCRIPTTSTYPECLASS_NATIVE,
    /** Array type. */
    RTSCRIPTTSTYPECLASS_ARRAY,
    /** Struct type. */
    RTSCRIPTTSTYPECLASS_STRUCT,
    /** Union type. */
    RTSCRIPTTSTYPECLASS_UNION,
    /** Function type. */
    RTSCRIPTTSTYPECLASS_FUNCTION,
    /** Pointer type. */
    RTSCRIPTTSTYPECLASS_POINTER,
    /** Alias for another type. */
    RTSCRIPTTSTYPECLASS_ALIAS
} RTSCRIPTTSTYPECLASS;


/** Pointer to a type descriptor. */
typedef struct RTSCRIPTTSTYPDESC *PRTSCRIPTTSTYPDESC;
/** Pointer to a const type descriptor. */
typedef const RTSCRIPTTSTYPDESC *PCRTSCRIPTTSTYPDESC;

/**
 * Type descriptor.
 */
typedef struct RTSCRIPTTSTYPDESC
{
    /** Type class */
    RTSCRIPTTSTYPECLASS            enmClass;
    /** Identifier for this type. */
    const char                     *pszIdentifier;
    /** Class dependent data. */
    union
    {
        /** Native type. */
        struct
        {
            /** The native type. */
            RTSCRIPTNATIVETYPE     enmTypeNative;
            /** Alignment for the native type in bits - 0 for default alignment. */
            uint32_t               cBitsAlign;
        } Native;
        /** Array type. */
        struct
        {
            /** Type identifier. */
            const char             *pszTypeIde;
            /** Number of elements. */
            uint32_t               cElems;
        } Array;
        /** Struct/Union type. */
        struct
        {
            /* Flag whether this is packed. */
            bool                   fPacked;
            /** Number of members. */
            uint32_t               cMembers;
            /** Pointer to the array of member identifiers for each member. */
            const char             **papszMember;
            /** Pointer to the array of type identifiers for each member. */
            const char             **papszMemberType;
        } UnionStruct;
        /** Function type. */
        struct
        {
            /** Return type - NULL for no return type. */
            const char             *pszTypeRet;
            /** Number of typed arguments. */
            uint32_t               cArgsTyped;
            /** Pointer to the array of type identifiers for the arguments. */
            const char             **papszMember;
            /** Flag whether variable arguments are used. */
            bool                   fVarArgs;
        } Function;
        /** Pointer. */
        struct
        {
            /** The type identifier the pointer references. */
            const char             *pszTypeIde;
        } Pointer;
        /** Pointer. */
        struct
        {
            /** The type identifier the alias references. */
            const char             *pszTypeIde;
        } Alias;
    } Class;
} RTSCRIPTTSTYPDESC;


/** Opaque type system handle. */
typedef struct RTSCRIPTTSINT *RTSCRIPTTS;
/** Pointer to an opaque type system handle. */
typedef RTSCRIPTTS *PRTSCRIPTTS;


/** Opaque type system type. */
typedef struct RTSCRIPTTSTYPEINT *RTSCRIPTTSTYPE;
/** Pointer to an opaque type system type. */
typedef RTSCRIPTTSTYPE *PRTSCRIPTTSTYPE;


/**
 * Create a new empty type system.
 *
 * @returns IPRT status code.
 * @param   phScriptTs         Where to store the handle to the type system on success.
 * @param   hScriptTsParent    Parent type system to get declarations from. NULL if no parent.
 * @param   enmPtrType         Native pointer type (only unsigned integer types allowed).
 * @param   cPtrAlignBits      The native alignment of a pointer storage location.
 */
RTDECL(int) RTScriptTsCreate(PRTSCRIPTTS phScriptTs, RTSCRIPTTS hScriptTsParent,
                             RTSCRIPTNATIVETYPE enmPtrType, uint32_t cPtrAlignBits);


/**
 * Retains a reference to the given type system.
 *
 * @returns New reference count.
 * @param   hScriptTs              Type system handle.
 */
RTDECL(uint32_t) RTScriptTsRetain(RTSCRIPTTS hScriptTs);


/**
 * Releases a reference to the given type system.
 *
 * @returns New reference count, on 0 the type system is destroyed.
 * @param   hScriptTs              Type system handle.
 */
RTDECL(uint32_t) RTScriptTsRelease(RTSCRIPTTS hScriptTs);


/**
 * Dumps the content of the type system.
 *
 * @returns IPRT status code.
 * @param   hScriptTs         Type system handle.
 */
RTDECL(int) RTScriptTsDump(RTSCRIPTTS hScriptTs);


/**
 * Add several types to the type system from the given descriptor array.
 *
 * @returns IPRT status code.
 * @param   hScriptTs              Type system handle.
 * @param   paTypeDescs            Pointer to the array of type descriptors.
 * @param   cTypeDescs             Number of entries in the array.
 */
RTDECL(int) RTScriptTsAdd(RTSCRIPTTS hScriptTs, PCRTSCRIPTTSTYPDESC paTypeDescs,
                          uint32_t cTypeDescs);


/**
 * Removes the given types from the type system.
 *
 * @returns IPRT status code.
 * @param   hScriptTs              Type system handle.
 * @param   papszTypes             Array of type identifiers to remove. Array terminated
 *                                 with a NULL entry.
 */
RTDECL(int) RTScriptTsRemove(RTSCRIPTTS hScriptTs, const char **papszTypes);


/**
 * Queries the given type returning the type handle on success.
 *
 * @returns IPRT status code.
 * @retval  VERR_NOT_FOUND if the type could not be found.
 * @param   hScriptTs              Type system handle.
 * @param   pszType                The type identifier to look for.
 * @param   phType                 Where to store the handle to the type on success.
 */
RTDECL(int) RTScriptTsQueryType(RTSCRIPTTS hScriptTs, const char *pszType,
                                PRTSCRIPTTSTYPE phType);


/**
 * Retains the given type reference.
 *
 * @returns New reference count.
 * @param   hScriptTsType          Type system type handle.
 */
RTDECL(uint32_t) RTScriptTsTypeRetain(RTSCRIPTTSTYPE hScriptTsType);


/**
 * Releases the given type reference.
 *
 * @returns New reference count.
 * @param   hScriptTsType          Type system type handle.
 */
RTDECL(uint32_t) RTScriptTsTypeRelease(RTSCRIPTTSTYPE hScriptTsType);


/**
 * Returns the class of the given type handle.
 *
 * @returns Type class for the given type handle.
 * @param   hScriptTsType          Type system type handle.
 */
RTDECL(RTSCRIPTTSTYPECLASS) RTScriptTsTypeGetClass(RTSCRIPTTSTYPE hScriptTsType);


/**
 * Returns the identifier of the given type handle.
 *
 * @returns Pointer to the identifier of the given type handle.
 * @param   hScriptTsType          Type system type handle.
 */
RTDECL(const char *) RTScriptTsTypeGetIdentifier(RTSCRIPTTSTYPE hScriptTsType);


/**
 * Returns the storage size of the given type in bits.
 *
 * @returns Size of the type in bits described by the given handle.
 * @param   hScriptTsType          Type system type handle.
 */
RTDECL(size_t) RTScriptTsTypeGetBitCount(RTSCRIPTTSTYPE hScriptTsType);


/**
 * Returns the necessary alignment of the given type in bits.
 *
 * @returns Alignmebt of the type in bits described by the given handle.
 * @param   hScriptTsType          Type system type handle.
 */
RTDECL(size_t) RTScriptTsTypeGetAlignmentBitCount(RTSCRIPTTSTYPE hScriptTsType);


/**
 * Return the native type for the given native type.
 *
 * @returns Native type enum.
 * @param   hScriptTsType          Type system type handle.
 */
RTDECL(RTSCRIPTNATIVETYPE) RTScriptTsTypeNativeGetType(RTSCRIPTTSTYPE hScriptTsType);


/**
 * Return the number of elements for the given array type.
 *
 * @returns Number of elements.
 * @param   hScriptTsType          Type system type handle.
 */
RTDECL(uint32_t) RTScriptTsTypeArrayGetElemCount(RTSCRIPTTSTYPE hScriptTsType);


/**
 * Return the type handle of element type for the given array type.
 *
 * @returns Number of elements.
 * @param   hScriptTsType          Type system type handle.
 */
RTDECL(RTSCRIPTTSTYPE) RTScriptTsTypeArrayGetElemType(RTSCRIPTTSTYPE hScriptTsType);


/**
 * Returns whether the given union/struct type is packed.
 *
 * @returns Number of elements.
 * @param   hScriptTsType          Type system type handle.
 */
RTDECL(bool) RTScriptTsTypeStructUnionGetPacked(RTSCRIPTTSTYPE hScriptTsType);

RTDECL(uint32_t) RTScriptTsTypeStructUnionGetMemberCount(RTSCRIPTTSTYPE hScriptTsType);

RTDECL(int) RTScriptTsTypeStructUnionQueryMember(RTSCRIPTTSTYPE hScriptTsType, uint32_t idxMember, uint32_t *poffMember,
                                                 uint32_t *pcMemberBits, PRTSCRIPTTSTYPE phScriptTsTypeMember);

RTDECL(RTSCRIPTTSTYPE) RTScriptTsTypeFunctionGetRetType(RTSCRIPTTSTYPE hScriptTsType);

RTDECL(uint32_t) RTScriptTsTypeFunctionGetTypedArgCount(RTSCRIPTTSTYPE hScriptTsType);

RTDECL(bool) RTScriptTsTypeFunctionUsesVarArgs(RTSCRIPTTSTYPE hScriptTsType);

RTDECL(RTSCRIPTTSTYPE) RTScriptTsTypeFunctionGetArgType(RTSCRIPTTSTYPE hScriptTsType, uint32_t idxArg);

RTDECL(RTSCRIPTTSTYPE) RTScriptTsTypePointerGetRefType(RTSCRIPTTSTYPE hScriptTsType);

RTDECL(RTSCRIPTTSTYPE) RTScriptTsTypeAliasGetAliasedType(RTSCRIPTTSTYPE hScriptTsType);

RTDECL(bool) RTScriptTsTypeEquals(RTSCRIPTTSTYPE hScriptTsType1, RTSCRIPTTSTYPE hScriptTsType2);

RTDECL(bool) RTScriptTsTypeEqualsByOneName(RTSCRIPTTS hScriptTs, const char *pszType1, RTSCRIPTTSTYPE hScriptTsType2);

RTDECL(bool) RTScriptTsTypeEqualsByTwoNames(RTSCRIPTTS hScriptTs, const char *pszType1, const char *pszType2);

/** @} */



/** @defgroup grp_rt_script_ps    Scripting program state API
 *
 * @{
 */

/** Opaque program state handle. */
typedef struct RTSCRIPTPSINT *RTSCRIPTPS;
/** Pointer to a program state handle. */
typedef RTSCRIPTPS *PRTSCRIPTPS;

RTDECL(int) RTScriptPsCreate(PRTSCRIPTPS phScriptPs, const char *pszId);

RTDECL(uint32_t) RTScriptPsRetain(RTSCRIPTPS hScriptPs);

RTDECL(uint32_t) RTScriptPsRelease(RTSCRIPTPS hScriptPs);

RTDECL(int) RTScriptPsDump(RTSCRIPTPS hScriptPs);

RTDECL(int) RTScriptPsBuildFromAst(RTSCRIPTPS hScriptPs, PRTSCRIPTASTCOMPILEUNIT pAstCompileUnit);

RTDECL(int) RTScriptPsCheckConsistency(RTSCRIPTPS hScriptPs);

/** @} */


/** @defgroup grp_rt_script_parse    Scripting parsing API
 *
 * @{
 */

/**
 * Creates a program state from the given pascal input source.
 *
 * @returns IPRT status code.
 * @param   phScriptPs             Where to store the handle to the program state on success.
 * @param   pszId                  The program state ID.
 * @param   pszSrc                 The input to parse.
 * @param   pErrInfo               Where to store error information, optional.
 */
RTDECL(int) RTScriptParsePascalFromString(PRTSCRIPTPS phScriptPs, const char *pszId, const char *pszSrc,
                                          PRTERRINFO pErrInfo);

/** @} */


/** @defgroup grp_rt_script_vm    Scripting bytecode VM API.
 *
 * @{
 */

/**
 * Data value (for return values and arguments).
 */
typedef struct RTSCRIPTVMVAL
{
    /** The value type. */
    RTSCRIPTNATIVETYPE    enmType;
    /** Value, dependent on type. */
    union
    {
        int8_t           i8;
        uint8_t          u8;
        int16_t          i16;
        uint16_t         u16;
        int32_t          i32;
        uint32_t         u32;
        int64_t          i64;
        uint64_t         u64;
        RTFLOAT64U       r64;
    } u;
} RTSCRIPTVMVAL;
/** Pointer to a VM value. */
typedef RTSCRIPTVMVAL *PRTSCRIPTVMVAL;
/** Pointer to a const value. */
typedef const RTSCRIPTVMVAL *PCRTSCRIPTVMVAL;

/** Opaque VM state handle. */
typedef struct RTSCRIPTVMINT *RTSCRIPTVM;
/** Pointer to a VM state handle. */
typedef RTSCRIPTVM *PRTSCRIPTVM;
/** Opaque VM execution context handle. */
typedef struct RTSCRIPTVMCTXINT *RTSCRIPTVMCTX;
/** Pointer to an opaque VM execution context handle. */
typedef RTSCRIPTVMCTX *PRTSCRIPTVMCTX;

/**
 * Creates a new script VM.
 *
 * @returns IPRT status code.
 * @param   phScriptVm             Where to store the VM handle on success.
 */
RTDECL(int) RTScriptVmCreate(PRTSCRIPTVM phScriptVm);


/**
 * Retains the VM reference.
 *
 * @returns New reference count.
 * @param   hScriptVm              The VM handle to retain.
 */
RTDECL(uint32_t) RTScriptVmRetain(RTSCRIPTVM hScriptVm);


/**
 * Releases the VM reference.
 *
 * @returns New reference count, on 0 the VM is destroyed.
 * @param   hScriptVm              The VM handle to release.
 */
RTDECL(uint32_t) RTScriptVmRelease(RTSCRIPTVM hScriptVm);


/**
 * Adds the given program state to the VM making it available for execution.
 *
 * @returns IPRT status code.
 * @param   hScriptVm              The VM handle.
 * @param   hScriptPs              The program state to add.
 * @param   pErrInfo               Where to store error information on failure.
 */
RTDECL(int) RTScriptVmAddPs(RTSCRIPTVM hScriptVm, RTSCRIPTPS hScriptPs, PRTERRINFO pErrInfo);


/**
 * Creates a new execution context for running code in the VM.
 *
 * @returns IPRT status code.
 * @param   hScriptVm              The VM handle.
 * @param   phScriptVmCtx          Where to store the execution context handle on success.
 * @param   cbStack                Size of the stack in bytes, 0 if unlimited.
 */
RTDECL(int) RTScriptVmExecCtxCreate(RTSCRIPTVM hScriptVm, PRTSCRIPTVMCTX phScriptVmCtx, size_t cbStack);


/**
 * Retains the VM execution context reference.
 *
 * @returns New reference count.
 * @param   hScriptVmCtx           The VM execution context handle to retain.
 */
RTDECL(uint32_t) RTScriptVmExecCtxRetain(RTSCRIPTVMCTX hScriptVmCtx);


/**
 * Releases a VM execution context reference.
 *
 * @returns New reference count, on 0 the execution context is destroyed.
 * @param   hScriptVmCtx           The VM execution context handle to release.
 */
RTDECL(uint32_t) RTScriptVmExecCtxRelease(RTSCRIPTVMCTX hScriptVmCtx);


/**
 * Sets the initial state for execution.
 *
 * @returns IPRT status code.
 * @param   hScriptVmCtx           The VM execution context handle.
 * @param   pszFn                  The method to execute, NULL for the main program if existing.
 * @param   paArgs                 Arguments to supply to the executed method.
 * @param   cArgs                  Number of arguments supplied.
 */
RTDECL(int) RTScriptVmExecCtxInit(RTSCRIPTVMCTX hScriptVmCtx, const char *pszFn,
                                  PCRTSCRIPTVMVAL paArgs, uint32_t cArgs);


/**
 * Continues executing the current state.
 *
 * @returns IPRT status code.
 * @param   hScriptVmCtx           The VM execution context handle.
 * @param   msTimeout              Maximum amount of time to execute, RT_INDEFINITE_WAIT
 *                                 for an unrestricted amount of time.
 * @param   pRetVal                Where to store the return value on success, optional.
 */
RTDECL(int) RTScriptVmExecCtxRun(RTSCRIPTVMCTX hScriptVmCtx, RTMSINTERVAL msTimeout,
                                 PRTSCRIPTVMVAL pRetVal);


/**
 * Interrupts the current execution.
 *
 * @returns IPRT status code.
 * @param   hScriptVmCtx           The VM execution context handle.
 */
RTDECL(int) RTScriptVmExecCtxInterrupt(RTSCRIPTVMCTX hScriptVmCtx);



/** @} */
#endif

/** @} */

RT_C_DECLS_END

#endif /* !IPRT_INCLUDED_script_h */

