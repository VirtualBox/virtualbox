/* $Id: VBoxCPP.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBox Build Tool - A mini C Preprocessor.
 *
 * Purposes to which this preprocessor will be put:
 *      - Preprocessig vm.h into dtrace/lib/vm.d so we can access the VM
 *        structure (as well as substructures) from DTrace without having
 *        to handcraft it all.
 *      - Removing \#ifdefs relating to a new feature that has become
 *        stable and no longer needs \#ifdef'ing.
 *      - Pretty printing preprocessor directives.  This will be used by
 *        SCM.
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
#include <VBox/VBoxTpG.h>

#include <iprt/alloca.h>
#include <iprt/assert.h>
#include <iprt/asm.h>
#include <iprt/ctype.h>
#include <iprt/err.h>
#include <iprt/file.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>

#include "scmstream.h"


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** The bitmap type. */
#define VBCPP_BITMAP_TYPE                   uint64_t
/** The bitmap size as a multiple of VBCPP_BITMAP_TYPE. */
#define VBCPP_BITMAP_SIZE                   (128 / 64)
/** Checks if a bit is set. */
#define VBCPP_BITMAP_IS_SET(a_bm, a_ch)     ASMBitTest(a_bm, (a_ch) & 0x7f)
/** Sets a bit. */
#define VBCPP_BITMAP_SET(a_bm, a_ch)        ASMBitSet(a_bm, (a_ch) & 0x7f)
/** Empties the bitmap. */
#define VBCPP_BITMAP_EMPTY(a_bm)            do { (a_bm)[0] = 0; (a_bm)[1] = 0; } while (0)
/** Joins to bitmaps by OR'ing their values.. */
#define VBCPP_BITMAP_OR(a_bm1, a_bm2)       do { (a_bm1)[0] |= (a_bm2)[0]; (a_bm1)[1] |= (a_bm2)[1]; } while (0)


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** Pointer to the C preprocessor instance data. */
typedef struct VBCPP *PVBCPP;


/**
 * Variable string buffer (very simple version of SCMSTREAM).
 */
typedef struct VBCPPSTRBUF
{
    /** The preprocessor instance (for error reporting). */
    struct VBCPP   *pThis;
    /** The length of the string in the buffer. */
    size_t          cchBuf;
    /** The string storage. */
    char           *pszBuf;
    /** Allocated buffer space. */
    size_t          cbBufAllocated;
} VBCPPSTRBUF;
/** Pointer to a variable string buffer. */
typedef VBCPPSTRBUF *PVBCPPSTRBUF;


/**
 * The preprocessor mode.
 */
typedef enum VBCPPMODE
{
    kVBCppMode_Invalid = 0,
    kVBCppMode_Standard,
    kVBCppMode_Selective,
    kVBCppMode_SelectiveD,
    kVBCppMode_End
} VBCPPMODE;


/**
 * A macro (aka define).
 */
typedef struct VBCPPMACRO
{
    /** The string space core. */
    RTSTRSPACECORE      Core;
#if 0
    /** For linking macros that have the fExpanding flag set. */
    struct VBCPPMACRO  *pUpExpanding;
#endif
    /** Whether it's a function. */
    bool                fFunction;
    /** Variable argument count. */
    bool                fVarArg;
    /** Set if originating on the command line. */
    bool                fCmdLine;
    /** Set if this macro is currently being expanded and should not be
     * recursively applied. */
    bool                fExpanding;
    /** The number of known arguments. */
    uint32_t            cArgs;
    /** Pointer to a list of argument names. */
    const char        **papszArgs;
    /** Lead character bitmap for the argument names. */
    VBCPP_BITMAP_TYPE   bmArgs[VBCPP_BITMAP_SIZE];
    /** The value length. */
    size_t              cchValue;
    /** The define value.  (This is followed by the name and arguments.) */
    char                szValue[1];
} VBCPPMACRO;
/** Pointer to a macro. */
typedef VBCPPMACRO *PVBCPPMACRO;


/**
 * Macro expansion data.
 */
typedef struct VBCPPMACROEXP
{
    /** The expansion buffer. */
    VBCPPSTRBUF     StrBuf;
#if 0
    /** List of expanding macros (Stack). */
    PVBCPPMACRO     pMacroStack;
#endif
    /** The input stream (in case we want to look for parameter lists). */
    PSCMSTREAM      pStrmInput;
    /** Array of argument values.  Used when expanding function style macros.  */
    char          **papszArgs;
    /** The number of argument values current in papszArgs. */
    uint32_t        cArgs;
    /** The number of argument values papszArgs can currently hold  */
    uint32_t        cArgsAlloced;
} VBCPPMACROEXP;
/** Pointer to macro expansion data. */
typedef VBCPPMACROEXP *PVBCPPMACROEXP;


/**
 * The vbcppMacroExpandReScan mode of operation.
 */
typedef enum VBCPPMACRORESCANMODE
{
    /** Invalid mode.  */
    kMacroReScanMode_Invalid = 0,
    /** Normal expansion mode. */
    kMacroReScanMode_Normal,
    /** Replaces known macros and heeds the 'defined' operator. */
    kMacroReScanMode_Expression,
    /** End of valid modes. */
    kMacroReScanMode_End
} VBCPPMACRORESCANMODE;


/**
 * Expression node type.
 */
typedef enum VBCPPEXPRKIND
{
    kVBCppExprKind_Invalid = 0,
    kVBCppExprKind_Unary,
    kVBCppExprKind_Binary,
    kVBCppExprKind_Ternary,
    kVBCppExprKind_SignedValue,
    kVBCppExprKind_UnsignedValue,
    kVBCppExprKind_UndefMacroCall,
    kVBCppExprKind_End
} VBCPPEXPRKIND;


/** Macro used for the precedence field. */
#define VBCPPOP_PRECEDENCE(a_iPrecedence)   ((a_iPrecedence) << 8)
/** Mask for getting the precedence field value. */
#define VBCPPOP_PRECEDENCE_MASK             0xff00
/** Operator associativity - Left to right. */
#define VBCPPOP_L2R                         (1 << 16)
/** Operator associativity - Right to left. */
#define VBCPPOP_R2L                         (2 << 16)

/**
 * Unary operators.
 */
typedef enum VBCPPUNARYOP
{
    kVBCppUnaryOp_Invalid = 0,
    kVBCppUnaryOp_Pluss             = VBCPPOP_R2L | VBCPPOP_PRECEDENCE( 3) |  5,
    kVBCppUnaryOp_Minus             = VBCPPOP_R2L | VBCPPOP_PRECEDENCE( 3) |  6,
    kVBCppUnaryOp_LogicalNot        = VBCPPOP_R2L | VBCPPOP_PRECEDENCE( 3) |  7,
    kVBCppUnaryOp_BitwiseNot        = VBCPPOP_R2L | VBCPPOP_PRECEDENCE( 3) |  8,
    kVBCppUnaryOp_Parenthesis       = VBCPPOP_R2L | VBCPPOP_PRECEDENCE(15) |  9,
    kVBCppUnaryOp_End
} VBCPPUNARYOP;

/**
 * Binary operators.
 */
typedef enum VBCPPBINARYOP
{
    kVBCppBinary_Invalid = 0,
    kVBCppBinary_Multiplication     = VBCPPOP_L2R | VBCPPOP_PRECEDENCE( 5) |  2,
    kVBCppBinary_Division           = VBCPPOP_L2R | VBCPPOP_PRECEDENCE( 5) |  4,
    kVBCppBinary_Modulo             = VBCPPOP_L2R | VBCPPOP_PRECEDENCE( 5) |  5,
    kVBCppBinary_Addition           = VBCPPOP_L2R | VBCPPOP_PRECEDENCE( 6) |  6,
    kVBCppBinary_Subtraction        = VBCPPOP_L2R | VBCPPOP_PRECEDENCE( 6) |  7,
    kVBCppBinary_LeftShift          = VBCPPOP_L2R | VBCPPOP_PRECEDENCE( 7) |  8,
    kVBCppBinary_RightShift         = VBCPPOP_L2R | VBCPPOP_PRECEDENCE( 7) |  9,
    kVBCppBinary_LessThan           = VBCPPOP_L2R | VBCPPOP_PRECEDENCE( 8) | 10,
    kVBCppBinary_LessThanOrEqual    = VBCPPOP_L2R | VBCPPOP_PRECEDENCE( 8) | 11,
    kVBCppBinary_GreaterThan        = VBCPPOP_L2R | VBCPPOP_PRECEDENCE( 8) | 12,
    kVBCppBinary_GreaterThanOrEqual = VBCPPOP_L2R | VBCPPOP_PRECEDENCE( 8) | 13,
    kVBCppBinary_EqualTo            = VBCPPOP_L2R | VBCPPOP_PRECEDENCE( 9) | 14,
    kVBCppBinary_NotEqualTo         = VBCPPOP_L2R | VBCPPOP_PRECEDENCE( 9) | 15,
    kVBCppBinary_BitwiseAnd         = VBCPPOP_L2R | VBCPPOP_PRECEDENCE(10) | 16,
    kVBCppBinary_BitwiseXor         = VBCPPOP_L2R | VBCPPOP_PRECEDENCE(11) | 17,
    kVBCppBinary_BitwiseOr          = VBCPPOP_L2R | VBCPPOP_PRECEDENCE(12) | 18,
    kVBCppBinary_LogicalAnd         = VBCPPOP_L2R | VBCPPOP_PRECEDENCE(13) | 19,
    kVBCppBinary_LogicalOr          = VBCPPOP_L2R | VBCPPOP_PRECEDENCE(14) | 20,
    kVBCppBinary_End
} VBCPPBINARYOP;

/** The precedence of the ternary operator (expr ? true : false). */
#define VBCPPTERNAROP_PRECEDENCE   VBCPPOP_PRECEDENCE(16)


/** Pointer to an expression parsing node. */
typedef struct VBCPPEXPR *PVBCPPEXPR;
/**
 * Expression parsing node.
 */
typedef struct VBCPPEXPR
{
    /** Parent expression. */
    PVBCPPEXPR          pParent;
    /** Whether the expression is complete or not. */
    bool                fComplete;
    /** The kind of expression. */
    VBCPPEXPRKIND       enmKind;
    /** Kind specific content. */
    union
    {
        /** kVBCppExprKind_Unary */
        struct
        {
            VBCPPUNARYOP    enmOperator;
            PVBCPPEXPR      pArg;
        } Unary;

        /** kVBCppExprKind_Binary */
        struct
        {
            VBCPPBINARYOP   enmOperator;
            PVBCPPEXPR      pLeft;
            PVBCPPEXPR      pRight;
        } Binary;

        /** kVBCppExprKind_Ternary */
        struct
        {
            PVBCPPEXPR      pExpr;
            PVBCPPEXPR      pTrue;
            PVBCPPEXPR      pFalse;
        } Ternary;

        /** kVBCppExprKind_SignedValue */
        struct
        {
            int64_t         s64;
        } SignedValue;

        /** kVBCppExprKind_UnsignedValue */
        struct
        {
            uint64_t        u64;
        } UnsignedValue;

        /** kVBCppExprKind_UndefMacroCall */
        struct
        {
            char           *pszName;
            size_t          cArgs;
            PVBCPPEXPR     *papArgs;
        } UndefMacroCall;
    } u;
} VBCPPEXPR;


/**
 * Operator return statuses.
 */
typedef enum VBCPPEXPRRET
{
    kExprRet_Error = -1,
    kExprRet_Ok = 0,
    kExprRet_UnaryOperator,
    kExprRet_Value,
    kExprRet_EndOfExpr,
    kExprRet_End
} VBCPPEXPRRET;

/**
 * Expression parser context.
 */
typedef struct VBCPPEXPRPARSER
{
    /** The current expression posistion. */
    const char         *pszCur;
    /** The root node. */
    PVBCPPEXPR          pRoot;
    /** The current expression node. */
    PVBCPPEXPR          pCur;
    /** Where to insert the next expression. */
    PVBCPPEXPR         *ppCur;
    /** The expression. */
    const char         *pszExpr;
    /** The number of undefined macros we've encountered while parsing. */
    size_t              cUndefined;
    /** Pointer to the C preprocessor instance. */
    PVBCPP              pThis;
} VBCPPEXPRPARSER;
/** Pointer to an expression parser context. */
typedef VBCPPEXPRPARSER *PVBCPPEXPRPARSER;


/**
 * Evaluation result.
 */
typedef enum VBCPPEVAL
{
    kVBCppEval_Invalid = 0,
    kVBCppEval_True,
    kVBCppEval_False,
    kVBCppEval_Undecided,
    kVBCppEval_End
} VBCPPEVAL;


/**
 * The condition kind.
 */
typedef enum VBCPPCONDKIND
{
    kVBCppCondKind_Invalid = 0,
    /** \#if expr  */
    kVBCppCondKind_If,
    /** \#ifdef define  */
    kVBCppCondKind_IfDef,
    /** \#ifndef define  */
    kVBCppCondKind_IfNDef,
    /** \#elif expr */
    kVBCppCondKind_ElIf,
    /** The end of valid values. */
    kVBCppCondKind_End
} VBCPPCONDKIND;


/**
 * Conditional stack entry.
 */
typedef struct VBCPPCOND
{
    /** The next conditional on the stack. */
    struct VBCPPCOND   *pUp;
    /** The kind of conditional. This changes on encountering \#elif. */
    VBCPPCONDKIND       enmKind;
    /** Evaluation result. */
    VBCPPEVAL           enmResult;
    /** The evaluation result of the whole stack. */
    VBCPPEVAL           enmStackResult;

    /** Whether we've seen the last else. */
    bool                fSeenElse;
    /** Set if we have an else if which has already been decided. */
    bool                fElIfDecided;
    /** The nesting level of this condition. */
    uint16_t            iLevel;
    /** The nesting level of this condition wrt the ones we keep. */
    uint16_t            iKeepLevel;

    /** The condition string. (Points within the stream buffer.) */
    const char         *pchCond;
    /** The condition length. */
    size_t              cchCond;
} VBCPPCOND;
/** Pointer to a conditional stack entry. */
typedef VBCPPCOND *PVBCPPCOND;


/**
 * Input buffer stack entry.
 */
typedef struct VBCPPINPUT
{
    /** Pointer to the next input on the stack. */
    struct VBCPPINPUT  *pUp;
    /** The input stream. */
    SCMSTREAM           StrmInput;
    /** Pointer into szName to the part which was specified. */
    const char         *pszSpecified;
    /** The input file name with include path. */
    char                szName[1];
} VBCPPINPUT;
/** Pointer to a input buffer stack entry */
typedef VBCPPINPUT *PVBCPPINPUT;


/**
 * The action to take with \#include.
 */
typedef enum VBCPPINCLUDEACTION
{
    kVBCppIncludeAction_Invalid = 0,
    kVBCppIncludeAction_Include,
    kVBCppIncludeAction_PassThru,
    kVBCppIncludeAction_Drop,
    kVBCppIncludeAction_End
} VBCPPINCLUDEACTION;


/**
 * C Preprocessor instance data.
 */
typedef struct VBCPP
{
    /** @name Options
     * @{ */
    /** The preprocessing mode. */
    VBCPPMODE           enmMode;
    /** Whether to keep comments. */
    bool                fKeepComments;
    /** Whether to respect source defines. */
    bool                fRespectSourceDefines;
    /** Whether to let source defines overrides the ones on the command
     *  line. */
    bool                fAllowRedefiningCmdLineDefines;
    /** Whether to pass thru defines. */
    bool                fPassThruDefines;
    /** Whether to allow undecided conditionals. */
    bool                fUndecidedConditionals;
    /** Whether to pass thru D pragmas. */
    bool                fPassThruPragmaD;
    /** Whether to pass thru STD pragmas. */
    bool                fPassThruPragmaSTD;
    /** Whether to pass thru other pragmas. */
    bool                fPassThruPragmaOther;
    /** Whether to pass thru \#error directives or execute them. */
    bool                fPassThruError;
    /** Whether to remove dropped lines from the output. */
    bool                fRemoveDroppedLines;
    /** Whether to preforme line splicing.
     * @todo implement line splicing  */
    bool                fLineSplicing;
    /** What to do about include files. */
    VBCPPINCLUDEACTION  enmIncludeAction;

    /** The number of include directories. */
    uint32_t            cIncludes;
    /** Array of directories to search for include files. */
    char              **papszIncludes;

    /** The name of the input file. */
    const char         *pszInput;
    /** The name of the output file. NULL if stdout. */
    const char         *pszOutput;
    /** @} */

    /** The define string space. */
    RTSTRSPACE          StrSpace;
    /** The string space holding explicitly undefined macros for selective
     * preprocessing runs. */
    RTSTRSPACE          UndefStrSpace;
    /** Indicates whether a C-word might need expansion.
     * The bitmap is indexed by C-word lead character.  Bits that are set
     * indicates that the lead character is used in a \#define that we know and
     * should expand. */
    VBCPP_BITMAP_TYPE   bmDefined[VBCPP_BITMAP_SIZE];

    /** The current depth of the conditional stack. */
    uint32_t            cCondStackDepth;
    /** Conditional stack. */
    PVBCPPCOND          pCondStack;
    /** The current condition evaluates to kVBCppEval_False, don't output. */
    bool                fIf0Mode;
    /** Just dropped a line and should maybe drop the current line. */
    bool                fJustDroppedLine;

    /** Whether the current line could be a preprocessor line.
     * This is set when EOL is encountered and cleared again when a
     * non-comment-or-space character is encountered.  See vbcppPreprocess. */
    bool                fMaybePreprocessorLine;

    /** The input stack depth */
    uint32_t            cInputStackDepth;
    /** The input buffer stack. */
    PVBCPPINPUT         pInputStack;

    /** The output stream. */
    SCMSTREAM           StrmOutput;

    /** The status of the whole job, as far as we know. */
    RTEXITCODE          rcExit;
    /** Whether StrmOutput is valid (for vbcppTerm). */
    bool                fStrmOutputValid;
} VBCPP;


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static VBCPPEXPRRET vbcppExprParseUnaryOrValue(PVBCPPEXPRPARSER pParser);
static PVBCPPMACRO  vbcppMacroLookup(PVBCPP pThis, const char *pszDefine, size_t cchDefine);
static RTEXITCODE   vbcppMacroExpandIt(PVBCPP pThis, PVBCPPMACROEXP pExp, size_t offMacro, PVBCPPMACRO pMacro, size_t offParameters);
static RTEXITCODE   vbcppMacroExpandReScan(PVBCPP pThis, PVBCPPMACROEXP pExp, VBCPPMACRORESCANMODE enmMode,
                                           size_t *pcReplacements, size_t *pcDefinedUnknown);
static void         vbcppMacroExpandCleanup(PVBCPPMACROEXP pExp);



/*
 *
 *
 * Message Handling.
 * Message Handling.
 * Message Handling.
 * Message Handling.
 * Message Handling.
 *
 *
 */


/**
 * Displays an error message.
 *
 * @returns RTEXITCODE_FAILURE
 * @param   pThis               The C preprocessor instance.
 * @param   pszMsg              The message.
 * @param   va                  Message arguments.
 */
static RTEXITCODE vbcppErrorV(PVBCPP pThis, const char *pszMsg, va_list va)
{
    NOREF(pThis);
    if (pThis->pInputStack)
    {
        PSCMSTREAM pStrm = &pThis->pInputStack->StrmInput;

        size_t const off     = ScmStreamTell(pStrm);
        size_t const iLine   = ScmStreamTellLine(pStrm);
        ScmStreamSeekByLine(pStrm, iLine);
        size_t const offLine = ScmStreamTell(pStrm);

        RTPrintf("%s:%d:%zd: error: %N.\n", pThis->pInputStack->szName, iLine + 1, off - offLine + 1, pszMsg, va);

        size_t cchLine;
        SCMEOL enmEof;
        const char *pszLine = ScmStreamGetLineByNo(pStrm, iLine, &cchLine, &enmEof);
        if (pszLine)
            RTPrintf("  %.*s\n"
                     "  %*s^\n",
                     cchLine, pszLine, off - offLine, "");

        ScmStreamSeekAbsolute(pStrm, off);
    }
    else
        RTMsgErrorV(pszMsg, va);
    return pThis->rcExit = RTEXITCODE_FAILURE;
}


/**
 * Displays an error message.
 *
 * @returns RTEXITCODE_FAILURE
 * @param   pThis               The C preprocessor instance.
 * @param   pszMsg              The message.
 * @param   ...                 Message arguments.
 */
static RTEXITCODE vbcppError(PVBCPP pThis, const char *pszMsg, ...)
{
    va_list va;
    va_start(va, pszMsg);
    RTEXITCODE rcExit = vbcppErrorV(pThis, pszMsg, va);
    va_end(va);
    return rcExit;
}


/**
 * Displays an error message.
 *
 * @returns RTEXITCODE_FAILURE
 * @param   pThis               The C preprocessor instance.
 * @param   pszPos              Pointer to the offending character.
 * @param   pszMsg              The message.
 * @param   ...                 Message arguments.
 */
static RTEXITCODE vbcppErrorPos(PVBCPP pThis, const char *pszPos, const char *pszMsg, ...)
{
    NOREF(pszPos); NOREF(pThis);
    va_list va;
    va_start(va, pszMsg);
    //RTMsgErrorV(pszMsg, va);
    RTEXITCODE rcExit = vbcppErrorV(pThis, pszMsg, va);
    va_end(va);
    return rcExit;
}







/*
 *
 *
 * Variable String Buffers.
 * Variable String Buffers.
 * Variable String Buffers.
 * Variable String Buffers.
 * Variable String Buffers.
 *
 *
 */


/**
 * Initializes a string buffer.
 *
 * @param   pStrBuf             The buffer structure to initialize.
 * @param   pThis               The C preprocessor instance.
 */
static void vbcppStrBufInit(PVBCPPSTRBUF pStrBuf, PVBCPP pThis)
{
    pStrBuf->pThis              = pThis;
    pStrBuf->cchBuf             = 0;
    pStrBuf->cbBufAllocated     = 0;
    pStrBuf->pszBuf             = NULL;
}


/**
 * Deletes a string buffer.
 *
 * @param   pStrBuf             Pointer to the string buffer.
 */
static void vbcppStrBufDelete(PVBCPPSTRBUF pStrBuf)
{
    RTMemFree(pStrBuf->pszBuf);
    pStrBuf->pszBuf = NULL;
}


/**
 * Ensures that sufficient bufferspace is available, growing the buffer if
 * necessary.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pStrBuf             Pointer to the string buffer.
 * @param   cbMin               The minimum buffer size.
 */
static RTEXITCODE vbcppStrBufGrow(PVBCPPSTRBUF pStrBuf, size_t cbMin)
{
    if (pStrBuf->cbBufAllocated >= cbMin)
        return RTEXITCODE_SUCCESS;

    size_t cbNew = pStrBuf->cbBufAllocated * 2;
    if (cbNew < cbMin)
        cbNew = RT_ALIGN_Z(cbMin, _1K);
    void *pv = RTMemRealloc(pStrBuf->pszBuf, cbNew);
    if (!pv)
        return vbcppError(pStrBuf->pThis, "out of memory (%zu bytes)", cbNew);

    pStrBuf->pszBuf         = (char *)pv;
    pStrBuf->cbBufAllocated = cbNew;
    return RTEXITCODE_SUCCESS;
}


/**
 * Appends a substring.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pStrBuf             Pointer to the string buffer.
 * @param   pchSrc              Pointer to the first character in the substring.
 * @param   cchSrc              The length of the substring.
 */
static RTEXITCODE vbcppStrBufAppendN(PVBCPPSTRBUF pStrBuf, const char *pchSrc, size_t cchSrc)
{
    size_t cchBuf = pStrBuf->cchBuf;
    if (cchBuf + cchSrc + 1 > pStrBuf->cbBufAllocated)
    {
        RTEXITCODE rcExit = vbcppStrBufGrow(pStrBuf, cchBuf + cchSrc + 1);
        if (rcExit != RTEXITCODE_SUCCESS)
            return rcExit;
    }

    memcpy(&pStrBuf->pszBuf[cchBuf], pchSrc, cchSrc);
    cchBuf += cchSrc;
    pStrBuf->pszBuf[cchBuf] = '\0';
    pStrBuf->cchBuf = cchBuf;

    return RTEXITCODE_SUCCESS;
}


/**
 * Appends a character.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pStrBuf             Pointer to the string buffer.
 * @param   ch                  The charater to append.
 */
static RTEXITCODE vbcppStrBufAppendCh(PVBCPPSTRBUF pStrBuf, char ch)
{
    size_t cchBuf = pStrBuf->cchBuf;
    if (cchBuf + 2 > pStrBuf->cbBufAllocated)
    {
        RTEXITCODE rcExit = vbcppStrBufGrow(pStrBuf, cchBuf + 2);
        if (rcExit != RTEXITCODE_SUCCESS)
            return rcExit;
    }

    pStrBuf->pszBuf[cchBuf++] = ch;
    pStrBuf->pszBuf[cchBuf] = '\0';
    pStrBuf->cchBuf = cchBuf;

    return RTEXITCODE_SUCCESS;
}


/**
 * Appends a string to the buffer.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pStrBuf             Pointer to the string buffer.
 * @param   psz                 The string to append.
 */
static RTEXITCODE vbcppStrBufAppend(PVBCPPSTRBUF pStrBuf, const char *psz)
{
    return vbcppStrBufAppendN(pStrBuf, psz, strlen(psz));
}


/**
 * Gets the last char in the buffer.
 *
 * @returns Last character, 0 if empty.
 * @param   pStrBuf             Pointer to the string buffer.
 */
static char vbcppStrBufLastCh(PVBCPPSTRBUF pStrBuf)
{
    if (!pStrBuf->cchBuf)
        return '\0';
    return pStrBuf->pszBuf[pStrBuf->cchBuf - 1];
}







/*
 *
 *
 * C Identifier/Word Parsing.
 * C Identifier/Word Parsing.
 * C Identifier/Word Parsing.
 * C Identifier/Word Parsing.
 * C Identifier/Word Parsing.
 *
 *
 */


/**
 * Checks if the given character is a valid C identifier lead character.
 *
 * @returns true / false.
 * @param   ch                  The character to inspect.
 */
DECLINLINE(bool) vbcppIsCIdentifierLeadChar(char ch)
{
    return RT_C_IS_ALPHA(ch)
        || ch == '_';
}


/**
 * Checks if the given character is a valid C identifier character.
 *
 * @returns true / false.
 * @param   ch                  The character to inspect.
 */
DECLINLINE(bool) vbcppIsCIdentifierChar(char ch)
{
    return RT_C_IS_ALNUM(ch)
        || ch == '_';
}



/**
 *
 * @returns @c true if valid, @c false if not. Error message already displayed
 *          on failure.
 * @param   pThis           The C preprocessor instance.
 * @param   pchIdentifier   The start of the identifier to validate.
 * @param   cchIdentifier   The length of the identifier. RTSTR_MAX if not
 *                          known.
 */
static bool vbcppValidateCIdentifier(PVBCPP pThis, const char *pchIdentifier, size_t cchIdentifier)
{
    if (cchIdentifier == RTSTR_MAX)
        cchIdentifier = strlen(pchIdentifier);

    if (cchIdentifier == 0)
    {
        vbcppErrorPos(pThis, pchIdentifier, "Zero length identifier");
        return false;
    }

    if (!vbcppIsCIdentifierLeadChar(*pchIdentifier))
    {
        vbcppErrorPos(pThis, pchIdentifier, "Bad lead chararacter in identifier: '%.*s'", cchIdentifier, pchIdentifier);
        return false;
    }

    for (size_t off = 1; off < cchIdentifier; off++)
    {
        if (!vbcppIsCIdentifierChar(pchIdentifier[off]))
        {
            vbcppErrorPos(pThis, pchIdentifier + off, "Illegal chararacter in identifier: '%.*s' (#%zu)", cchIdentifier, pchIdentifier, off + 1);
            return false;
        }
    }

    return true;
}

#if 0

/**
 * Checks if the given character is valid C punctuation.
 *
 * @returns true / false.
 * @param   ch                  The character to inspect.
 */
DECLINLINE(bool) vbcppIsCPunctuationLeadChar(char ch)
{
    switch (ch)
    {
        case '!':
        case '#':
        case '%':
        case '&':
        case '(':
        case ')':
        case '*':
        case '+':
        case ',':
        case '-':
        case '.':
        case '/':
        case ':':
        case ';':
        case '<':
        case '=':
        case '>':
        case '?':
        case '[':
        case ']':
        case '^':
        case '{':
        case '|':
        case '}':
        case '~':
            return true;
        default:
            return false;
    }
}


/**
 * Checks if the given string start with valid C punctuation.
 *
 * @returns 0 if not, otherwise the length of the punctuation.
 * @param   pch                 The which start we should evaluate.
 * @param   cchMax              The maximum string length.
 */
static size_t vbcppIsCPunctuationLeadChar(const char *psz, size_t cchMax)
{
    if (!cchMax)
        return 0;

    switch (psz[0])
    {
        case '!':
        case '*':
        case '/':
        case '=':
        case '^':
            if (cchMax >= 2 && psz[1] == '=')
                return 2;
            return 1;

        case '#':
            if (cchMax >= 2 && psz[1] == '#')
                return 2;
            return 1;

        case '%':
            if (cchMax >= 2 && (psz[1] == '=' || psz[1] == '>'))
                return 2;
            if (cchMax >= 2 && psz[1] == ':')
            {
                if (cchMax >= 4 && psz[2] == '%' && psz[3] == ':')
                    return 4;
                return 2;
            }
            return 1;

        case '&':
            if (cchMax >= 2 && (psz[1] == '=' || psz[1] == '&'))
                return 2;
            return 1;

        case '(':
        case ')':
        case ',':
        case '?':
        case '[':
        case ']':
        case '{':
        case '}':
            return 1;

        case '+':
            if (cchMax >= 2 && (psz[1] == '=' || psz[1] == '+'))
                return 2;
            return 1;

        case '-':
            if (cchMax >= 2 && (psz[1] == '=' || psz[1] == '-' || psz[1] == '>'))
                return 2;
            return 1;

        case ':':
            if (cchMax >= 2 && psz[1] == '>')
                return 2;
            return 1;

        case ';':
            return 1;

        case '<':
            if (cchMax >= 2 && psz[1] == '<')
            {
                if (cchMax >= 3 && psz[2] == '=')
                    return 3;
                return 2;
            }
            if (cchMax >= 2 && (psz[1] == '=' || psz[1] == ':' || psz[1] == '%'))
                return 2;
            return 1;

        case '.':
            if (cchMax >= 3 && psz[1] == '.' && psz[2] == '.')
                return 3;
            return 1;

        case '>':
            if (cchMax >= 2 && psz[1] == '>')
            {
                if (cchMax >= 3 && psz[2] == '=')
                    return 3;
                return 2;
            }
            if (cchMax >= 2 && psz[1] == '=')
                return 2;
            return 1;

        case '|':
            if (cchMax >= 2 && (psz[1] == '=' || psz[1] == '|'))
                return 2;
            return 1;

        case '~':
            return 1;

        default:
            return 0;
    }
}

#endif





/*
 *
 *
 * Output
 * Output
 * Output
 * Output
 * Output
 *
 *
 */


/**
 * Outputs a character.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   ch                  The character to output.
 */
static RTEXITCODE vbcppOutputCh(PVBCPP pThis, char ch)
{
    int rc = ScmStreamPutCh(&pThis->StrmOutput, ch);
    if (RT_SUCCESS(rc))
        return RTEXITCODE_SUCCESS;
    return vbcppError(pThis, "Output error: %Rrc", rc);
}


/**
 * Outputs a string.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pch                 The string.
 * @param   cch                 The number of characters to write.
 */
static RTEXITCODE vbcppOutputWrite(PVBCPP pThis, const char *pch, size_t cch)
{
    int rc = ScmStreamWrite(&pThis->StrmOutput, pch, cch);
    if (RT_SUCCESS(rc))
        return RTEXITCODE_SUCCESS;
    return vbcppError(pThis, "Output error: %Rrc", rc);
}


static RTEXITCODE vbcppOutputComment(PVBCPP pThis, PSCMSTREAM pStrmInput, size_t offStart, size_t cchOutputted,
                                     size_t cchMinIndent)
{
    RT_NOREF_PV(cchMinIndent); /** @todo  cchMinIndent */

    size_t offCur = ScmStreamTell(pStrmInput);
    if (offStart < offCur)
    {
        int rc = ScmStreamSeekAbsolute(pStrmInput, offStart);
        AssertRCReturn(rc, vbcppError(pThis, "Input seek error: %Rrc", rc));

        /*
         * Use the same indent, if possible.
         */
        size_t cchIndent = offStart - ScmStreamTellOffsetOfLine(pStrmInput, ScmStreamTellLine(pStrmInput));
        if (cchOutputted < cchIndent)
            rc = ScmStreamPrintf(&pThis->StrmOutput, "%*s", cchIndent - cchOutputted, "");
        else
            rc = ScmStreamPutCh(&pThis->StrmOutput, ' ');
        if (RT_FAILURE(rc))
            return vbcppError(pThis, "Output error: %Rrc", rc);

        /*
         * Copy the bytes.
         */
        while (ScmStreamTell(pStrmInput) < offCur)
        {
            unsigned ch = ScmStreamGetCh(pStrmInput);
            if (ch == ~(unsigned)0)
                return vbcppError(pThis, "Input error: %Rrc", rc);
            rc = ScmStreamPutCh(&pThis->StrmOutput, ch);
            if (RT_FAILURE(rc))
                return vbcppError(pThis, "Output error: %Rrc", rc);
        }
    }

    return RTEXITCODE_SUCCESS;
}





/*
 *
 *
 * Input
 * Input
 * Input
 * Input
 * Input
 *
 *
 */


#if 0 /* unused */
/**
 * Skips white spaces, including escaped new-lines.
 *
 * @param   pStrmInput          The input stream.
 */
static void vbcppProcessSkipWhiteAndEscapedEol(PSCMSTREAM pStrmInput)
{
    unsigned chPrev = ~(unsigned)0;
    unsigned ch;
    while ((ch = ScmStreamPeekCh(pStrmInput)) != ~(unsigned)0)
    {
        if (ch == '\r' || ch == '\n')
        {
            if (chPrev != '\\')
                break;
            chPrev = ch;
            ScmStreamSeekByLine(pStrmInput, ScmStreamTellLine(pStrmInput) + 1);
        }
        else if (RT_C_IS_SPACE(ch))
        {
            chPrev = ch;
            ch = ScmStreamGetCh(pStrmInput);
            Assert(ch == chPrev);
        }
        else
            break;
    }
}
#endif


/**
 * Skips white spaces, escaped new-lines and multi line comments.
 *
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 */
static RTEXITCODE vbcppProcessSkipWhiteEscapedEolAndComments(PVBCPP pThis, PSCMSTREAM pStrmInput)
{
    unsigned chPrev = ~(unsigned)0;
    unsigned ch;
    while ((ch = ScmStreamPeekCh(pStrmInput)) != ~(unsigned)0)
    {
        if (!RT_C_IS_SPACE(ch))
        {
            /* Multi-line Comment? */
            if (ch != '/')
                break;                  /* most definitely, not. */

            size_t offSaved = ScmStreamTell(pStrmInput);
            ScmStreamGetCh(pStrmInput);
            if (ScmStreamPeekCh(pStrmInput) != '*')
            {
                ScmStreamSeekAbsolute(pStrmInput, offSaved);
                break;              /* no */
            }

            /* Skip to the end of the comment. */
            while ((ch = ScmStreamGetCh(pStrmInput)) != ~(unsigned)0)
            {
                if (ch == '*')
                {
                    ch = ScmStreamGetCh(pStrmInput);
                    if (ch == '/')
                        break;
                    if (ch == ~(unsigned)0)
                        break;
                }
            }
            if (ch == ~(unsigned)0)
                return vbcppError(pThis, "unterminated multi-line comment");
            chPrev = '/';
        }
        /* New line (also matched by RT_C_IS_SPACE). */
        else if (ch == '\r' || ch == '\n')
        {
            /* Stop if not escaped. */
            if (chPrev != '\\')
                break;
            chPrev = ch;
            ScmStreamSeekByLine(pStrmInput, ScmStreamTellLine(pStrmInput) + 1);
        }
        /* Real space char. */
        else
        {
            chPrev = ch;
            ch = ScmStreamGetCh(pStrmInput);
            Assert(ch == chPrev);
        }
    }
    return RTEXITCODE_SUCCESS;
}


/**
 * Skips white spaces, escaped new-lines, and multi line comments, then checking
 * that we're at the end of a line.
 *
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 */
static RTEXITCODE vbcppProcessSkipWhiteEscapedEolAndCommentsCheckEol(PVBCPP pThis, PSCMSTREAM pStrmInput)
{
    RTEXITCODE rcExit = vbcppProcessSkipWhiteEscapedEolAndComments(pThis, pStrmInput);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        unsigned ch = ScmStreamPeekCh(pStrmInput);
        if (   ch != ~(unsigned)0
            && ch != '\r'
            && ch != '\n')
            rcExit = vbcppError(pThis, "Did not expected anything more on this line");
    }
    return rcExit;
}


/**
 * Skips white spaces.
 *
 * @returns The current location upon return.
 * @param   pStrmInput          The input stream.
 */
static size_t vbcppProcessSkipWhite(PSCMSTREAM pStrmInput)
{
    unsigned ch;
    while ((ch = ScmStreamPeekCh(pStrmInput)) != ~(unsigned)0)
    {
        if (!RT_C_IS_SPACE(ch) || ch == '\r' || ch == '\n')
            break;
        unsigned chCheck = ScmStreamGetCh(pStrmInput);
        AssertBreak(chCheck == ch);
    }
    return ScmStreamTell(pStrmInput);
}


/**
 * Looks for a left parenthesis in the input stream.
 *
 * Used during macro expansion.  Will ignore comments, newlines and other
 * whitespace.
 *
 * @retval  true if found. The stream position at opening parenthesis.
 * @retval  false if not found. The stream position is unchanged.
 *
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 */
static bool vbcppInputLookForLeftParenthesis(PVBCPP pThis, PSCMSTREAM pStrmInput)
{
    size_t offSaved = ScmStreamTell(pStrmInput);
    /*RTEXITCODE rcExit =*/ vbcppProcessSkipWhiteEscapedEolAndComments(pThis, pStrmInput);
    unsigned ch = ScmStreamPeekCh(pStrmInput);
    if (ch == '(')
        return true;

    int rc = ScmStreamSeekAbsolute(pStrmInput, offSaved);
    AssertFatalRC(rc);
    return false;
}


/**
 * Skips input until the real end of the current directive line has been
 * reached.
 *
 * This includes multiline comments starting on the same line
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 * @param   poffComment         Where to note down the position of the final
 *                              comment. Optional.
 */
static RTEXITCODE vbcppInputSkipToEndOfDirectiveLine(PVBCPP pThis, PSCMSTREAM pStrmInput, size_t *poffComment)
{
    if (poffComment)
        *poffComment = ~(size_t)0;

    RTEXITCODE  rcExit      = RTEXITCODE_SUCCESS;
    bool        fInComment  = false;
    unsigned    chPrev      = 0;
    unsigned    ch;
    while ((ch = ScmStreamPeekCh(pStrmInput)) != ~(unsigned)0)
    {
        if (ch == '\r' || ch == '\n')
        {
            if (chPrev == '\\')
            {
                ScmStreamSeekByLine(pStrmInput, ScmStreamTellLine(pStrmInput) + 1);
                continue;
            }
            if (!fInComment)
                break;
            /* The expression continues after multi-line comments. Cool. :-) */
        }
        else if (!fInComment)
        {
            if (chPrev == '/' && ch == '*' )
            {
                fInComment = true;
                if (poffComment)
                    *poffComment = ScmStreamTell(pStrmInput) - 1;
            }
            else if (chPrev == '/' && ch == '/')
            {
                if (poffComment)
                    *poffComment = ScmStreamTell(pStrmInput) - 1;
                rcExit = vbcppProcessSkipWhiteEscapedEolAndComments(pThis, pStrmInput);
                break;                  /* done */
            }
        }
        else if (ch == '/' && chPrev == '*')
            fInComment = false;

        /* advance */
        chPrev = ch;
        ch = ScmStreamGetCh(pStrmInput); Assert(ch == chPrev);
    }
    return rcExit;
}


/**
 * Processes a multi-line comment.
 *
 * Must either string the comment or keep it. If the latter, we must refrain
 * from replacing C-words in it.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 */
static RTEXITCODE vbcppProcessMultiLineComment(PVBCPP pThis, PSCMSTREAM pStrmInput)
{
    /* The open comment sequence. */
    ScmStreamGetCh(pStrmInput);         /* '*' */
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    if (   pThis->fKeepComments
        && !pThis->fIf0Mode)
        rcExit = vbcppOutputWrite(pThis, "/*", 2);

    /* The comment.*/
    unsigned ch;
    while (   rcExit == RTEXITCODE_SUCCESS
           && (ch = ScmStreamGetCh(pStrmInput)) != ~(unsigned)0 )
    {
        if (ch == '*')
        {
            /* Closing sequence? */
            unsigned ch2 = ScmStreamPeekCh(pStrmInput);
            if (ch2 == '/')
            {
                ScmStreamGetCh(pStrmInput);
                if (   pThis->fKeepComments
                    && !pThis->fIf0Mode)
                    rcExit = vbcppOutputWrite(pThis, "*/", 2);
                break;
            }
        }

        if (ch == '\r' || ch == '\n')
        {
            if (   (   pThis->fKeepComments
                    && !pThis->fIf0Mode)
                || !pThis->fRemoveDroppedLines
                || !ScmStreamIsAtStartOfLine(&pThis->StrmOutput))
                rcExit = vbcppOutputCh(pThis, ch);
            pThis->fJustDroppedLine       = false;
            pThis->fMaybePreprocessorLine = true;
        }
        else if (   pThis->fKeepComments
                 && !pThis->fIf0Mode)
            rcExit = vbcppOutputCh(pThis, ch);

        if (rcExit != RTEXITCODE_SUCCESS)
            break;
    }
    return rcExit;
}


/**
 * Processes a single line comment.
 *
 * Must either string the comment or keep it. If the latter, we must refrain
 * from replacing C-words in it.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 */
static RTEXITCODE vbcppProcessOneLineComment(PVBCPP pThis, PSCMSTREAM pStrmInput)
{
    RTEXITCODE  rcExit = RTEXITCODE_SUCCESS;
    SCMEOL      enmEol;
    size_t      cchLine;
    const char *pszLine = ScmStreamGetLine(pStrmInput, &cchLine, &enmEol); Assert(pszLine);
    pszLine--; cchLine++;               /* unfetching the first slash. */
    for (;;)
    {
        if (   pThis->fKeepComments
            && !pThis->fIf0Mode)
            rcExit = vbcppOutputWrite(pThis, pszLine, cchLine + enmEol);
        else if (   !pThis->fIf0Mode
                 || !pThis->fRemoveDroppedLines
                 || !ScmStreamIsAtStartOfLine(&pThis->StrmOutput) )
            rcExit = vbcppOutputWrite(pThis, pszLine + cchLine, enmEol);
        if (rcExit != RTEXITCODE_SUCCESS)
            break;
        if (   cchLine == 0
            || pszLine[cchLine - 1] != '\\')
            break;

        pszLine = ScmStreamGetLine(pStrmInput, &cchLine, &enmEol);
        if (!pszLine)
            break;
    }
    pThis->fJustDroppedLine       = false;
    pThis->fMaybePreprocessorLine = true;
    return rcExit;
}


/**
 * Processes a double quoted string.
 *
 * Must not replace any C-words in strings.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 */
static RTEXITCODE vbcppProcessStringLitteral(PVBCPP pThis, PSCMSTREAM pStrmInput)
{
    RTEXITCODE rcExit = vbcppOutputCh(pThis, '"');
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        bool fEscaped = false;
        for (;;)
        {
            unsigned ch = ScmStreamGetCh(pStrmInput);
            if (ch == ~(unsigned)0)
            {
                rcExit = vbcppError(pThis, "Unterminated double quoted string");
                break;
            }

            rcExit = vbcppOutputCh(pThis, ch);
            if (rcExit != RTEXITCODE_SUCCESS)
                break;

            if (ch == '"' && !fEscaped)
                break;
            fEscaped = !fEscaped && ch == '\\';
        }
    }
    return rcExit;
}


/**
 * Processes a single quoted constant.
 *
 * Must not replace any C-words in character constants.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 */
static RTEXITCODE vbcppProcessCharacterConstant(PVBCPP pThis, PSCMSTREAM pStrmInput)
{
    RTEXITCODE rcExit = vbcppOutputCh(pThis, '\'');
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        bool fEscaped = false;
        for (;;)
        {
            unsigned ch = ScmStreamGetCh(pStrmInput);
            if (ch == ~(unsigned)0)
            {
                rcExit = vbcppError(pThis, "Unterminated singled quoted string");
                break;
            }

            rcExit = vbcppOutputCh(pThis, ch);
            if (rcExit != RTEXITCODE_SUCCESS)
                break;

            if (ch == '\'' && !fEscaped)
                break;
            fEscaped = !fEscaped && ch == '\\';
        }
    }
    return rcExit;
}


/**
 * Processes a integer or floating point number constant.
 *
 * Must not replace the type suffix.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 * @param   chFirst             The first character.
 */
static RTEXITCODE vbcppProcessNumber(PVBCPP pThis, PSCMSTREAM pStrmInput, char chFirst)
{
    RTEXITCODE rcExit = vbcppOutputCh(pThis, chFirst);

    unsigned ch;
    while (   rcExit == RTEXITCODE_SUCCESS
           && (ch = ScmStreamPeekCh(pStrmInput)) != ~(unsigned)0)
    {
        if (   !vbcppIsCIdentifierChar(ch)
            && ch != '.')
            break;

        unsigned ch2 = ScmStreamGetCh(pStrmInput);
        AssertBreakStmt(ch2 == ch, rcExit = vbcppError(pThis, "internal error"));
        rcExit = vbcppOutputCh(pThis, ch);
    }

    return rcExit;
}


/**
 * Processes a identifier, possibly replacing it with a definition.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 */
static RTEXITCODE vbcppProcessIdentifier(PVBCPP pThis, PSCMSTREAM pStrmInput)
{
    RTEXITCODE  rcExit;
    size_t      cchDefine;
    const char *pchDefine = ScmStreamCGetWordM1(pStrmInput, &cchDefine);
    AssertReturn(pchDefine, vbcppError(pThis, "Internal error in ScmStreamCGetWordM1"));

    /*
     * Does this look like a define we know?
     */
    PVBCPPMACRO pMacro = vbcppMacroLookup(pThis, pchDefine, cchDefine);
    if (   pMacro
        && (   !pMacro->fFunction
            || vbcppInputLookForLeftParenthesis(pThis, pStrmInput)) )
    {
        /*
         * Expand it.
         */
        VBCPPMACROEXP ExpCtx;
#if 0
        ExpCtx.pMacroStack    = NULL;
#endif
        ExpCtx.pStrmInput     = pStrmInput;
        ExpCtx.papszArgs      = NULL;
        ExpCtx.cArgs          = 0;
        ExpCtx.cArgsAlloced   = 0;
        vbcppStrBufInit(&ExpCtx.StrBuf, pThis);
        rcExit = vbcppStrBufAppendN(&ExpCtx.StrBuf, pchDefine, cchDefine);
        if (rcExit == RTEXITCODE_SUCCESS)
            rcExit = vbcppMacroExpandIt(pThis, &ExpCtx, 0 /* offset */, pMacro, cchDefine);
        if (rcExit == RTEXITCODE_SUCCESS)
            rcExit = vbcppMacroExpandReScan(pThis, &ExpCtx, kMacroReScanMode_Normal, NULL, NULL);
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            /*
             * Insert it into the output stream.  Make sure there is a
             * whitespace following it.
             */
            int rc = ScmStreamWrite(&pThis->StrmOutput, ExpCtx.StrBuf.pszBuf, ExpCtx.StrBuf.cchBuf);
            if (RT_SUCCESS(rc))
            {
                unsigned chAfter = ScmStreamPeekCh(pStrmInput);
                if (chAfter != ~(unsigned)0 && !RT_C_IS_SPACE(chAfter))
                    rcExit = vbcppOutputCh(pThis, ' ');
            }
            else
                rcExit = vbcppError(pThis, "Output error: %Rrc", rc);
        }
        vbcppMacroExpandCleanup(&ExpCtx);
    }
    else
    {
        /*
         * Not a macro or a function-macro name match but no invocation, just
         * output the text unchanged.
         */
        int rc = ScmStreamWrite(&pThis->StrmOutput, pchDefine, cchDefine);
        if (RT_SUCCESS(rc))
            rcExit = RTEXITCODE_SUCCESS;
        else
            rcExit = vbcppError(pThis, "Output error: %Rrc", rc);
    }
    return rcExit;
}







/*
 *
 *
 * D E F I N E S   /   M A C R O S
 * D E F I N E S   /   M A C R O S
 * D E F I N E S   /   M A C R O S
 * D E F I N E S   /   M A C R O S
 * D E F I N E S   /   M A C R O S
 *
 *
 */


/**
 * Checks if a define exists.
 *
 * @returns true or false.
 * @param   pThis               The C preprocessor instance.
 * @param   pszDefine           The define name and optionally the argument
 *                              list.
 * @param   cchDefine           The length of the name. RTSTR_MAX is ok.
 */
static bool vbcppMacroExists(PVBCPP pThis, const char *pszDefine, size_t cchDefine)
{
    return cchDefine > 0
        && VBCPP_BITMAP_IS_SET(pThis->bmDefined, *pszDefine)
        && RTStrSpaceGetN(&pThis->StrSpace, pszDefine, cchDefine) != NULL;
}


/**
 * Looks up a define.
 *
 * @returns Pointer to the define if found, NULL if not.
 * @param   pThis               The C preprocessor instance.
 * @param   pszDefine           The define name and optionally the argument
 *                              list.
 * @param   cchDefine           The length of the name. RTSTR_MAX is ok.
 */
static PVBCPPMACRO vbcppMacroLookup(PVBCPP pThis, const char *pszDefine, size_t cchDefine)
{
    if (!cchDefine)
        return NULL;
    if (!VBCPP_BITMAP_IS_SET(pThis->bmDefined, *pszDefine))
        return NULL;
    return (PVBCPPMACRO)RTStrSpaceGetN(&pThis->StrSpace, pszDefine, cchDefine);
}


static uint32_t vbcppMacroLookupArg(PVBCPPMACRO pMacro, const char *pchName, size_t cchName)
{
    Assert(cchName > 0);

    char const ch = *pchName;
    for (uint32_t i = 0; i < pMacro->cArgs; i++)
        if (   pMacro->papszArgs[i][0] == ch
            && !strncmp(pMacro->papszArgs[i], pchName, cchName)
            && pMacro->papszArgs[i][cchName] == '\0')
            return i;

    if (   pMacro->fVarArg
        && cchName == sizeof("__VA_ARGS__") - 1
        && !strncmp(pchName, "__VA_ARGS__", sizeof("__VA_ARGS__") - 1) )
        return pMacro->cArgs;

    return UINT32_MAX;
}


static RTEXITCODE vbcppMacroExpandReplace(PVBCPP pThis, PVBCPPMACROEXP pExp, size_t off, size_t cchToReplace,
                                          const char *pchReplacement, size_t cchReplacement)
{
    RT_NOREF_PV(pThis);

    /*
     * Figure how much space we actually need.
     * (Hope this whitespace stuff is correct...)
     */
    bool const fLeadingSpace            = off > 0
                                       && !RT_C_IS_SPACE(pExp->StrBuf.pszBuf[off - 1]);
    bool const fTrailingSpace           = off + cchToReplace < pExp->StrBuf.cchBuf
                                       && !RT_C_IS_SPACE(pExp->StrBuf.pszBuf[off + cchToReplace]);
    size_t const cchActualReplacement   = fLeadingSpace + cchReplacement + fTrailingSpace;

    /*
     * Adjust the buffer size and contents.
     */
    if (cchActualReplacement > cchToReplace)
    {
        size_t const offMore = cchActualReplacement - cchToReplace;

        /* Ensure enough buffer space. */
        size_t cbMinBuf = offMore + pExp->StrBuf.cchBuf + 1;
        RTEXITCODE rcExit = vbcppStrBufGrow(&pExp->StrBuf, cbMinBuf);
        if (rcExit != RTEXITCODE_SUCCESS)
            return rcExit;

        /* Push the chars following the replacement area down to make room. */
        memmove(&pExp->StrBuf.pszBuf[off + cchToReplace + offMore],
                &pExp->StrBuf.pszBuf[off + cchToReplace],
                pExp->StrBuf.cchBuf - off - cchToReplace + 1);
        pExp->StrBuf.cchBuf += offMore;

    }
    else if (cchActualReplacement < cchToReplace)
    {
        size_t const offLess = cchToReplace - cchActualReplacement;

        /* Pull the chars following the replacement area up. */
        memmove(&pExp->StrBuf.pszBuf[off + cchToReplace - offLess],
                &pExp->StrBuf.pszBuf[off + cchToReplace],
                pExp->StrBuf.cchBuf - off - cchToReplace + 1);
        pExp->StrBuf.cchBuf -= offLess;
    }

    /*
     * Insert the replacement string.
     */
    char *pszCur = &pExp->StrBuf.pszBuf[off];
    if (fLeadingSpace)
        *pszCur++ = ' ';
    memcpy(pszCur, pchReplacement, cchReplacement);
    if (fTrailingSpace)
        *pszCur++ = ' ';

    Assert(strlen(pExp->StrBuf.pszBuf) == pExp->StrBuf.cchBuf);

    return RTEXITCODE_SUCCESS;
}


static unsigned vbcppMacroExpandPeekCh(PVBCPPMACROEXP pExp, size_t *poff)
{
    size_t off = *poff;
    if (off >= pExp->StrBuf.cchBuf)
        return pExp->pStrmInput ? ScmStreamPeekCh(pExp->pStrmInput) : ~(unsigned)0;
    return pExp->StrBuf.pszBuf[off];
}


static unsigned vbcppMacroExpandGetCh(PVBCPPMACROEXP pExp, size_t *poff)
{
    size_t off = *poff;
    if (off >= pExp->StrBuf.cchBuf)
        return pExp->pStrmInput ? ScmStreamGetCh(pExp->pStrmInput) : ~(unsigned)0;
    *poff = off + 1;
    return pExp->StrBuf.pszBuf[off];
}


static RTEXITCODE vbcppMacroExpandSkipEolEx(PVBCPP pThis, PVBCPPMACROEXP pExp, size_t *poff, unsigned chFirst)
{
    if (chFirst == '\r')
    {
        unsigned ch2 = vbcppMacroExpandPeekCh(pExp, poff);
        if (ch2 == '\n')
        {
            ch2 = ScmStreamGetCh(pExp->pStrmInput);
            AssertReturn(ch2 == '\n', vbcppError(pThis, "internal error"));
        }
    }
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE vbcppMacroExpandSkipEol(PVBCPP pThis, PVBCPPMACROEXP pExp, size_t *poff)
{
    unsigned ch = vbcppMacroExpandGetCh(pExp, poff);
    AssertReturn(ch == '\r' || ch == '\n', vbcppError(pThis, "internal error"));
    return vbcppMacroExpandSkipEolEx(pThis, pExp, poff, ch);
}


static RTEXITCODE vbcppMacroExpandSkipCommentLine(PVBCPP pThis, PVBCPPMACROEXP pExp, size_t *poff)
{
    unsigned ch = vbcppMacroExpandGetCh(pExp, poff);
    AssertReturn(ch == '/', vbcppError(pThis, "Internal error - expected '/' got '%c'", ch));

    unsigned chPrev = 0;
    while ((ch = vbcppMacroExpandGetCh(pExp, poff)) != ~(unsigned)0)
    {
        if (ch == '\r' || ch == '\n')
        {
            RTEXITCODE rcExit = vbcppMacroExpandSkipEolEx(pThis, pExp, poff, ch);
            if (rcExit != RTEXITCODE_SUCCESS)
                return rcExit;
            if (chPrev != '\\')
                break;
        }

        chPrev = ch;
    }
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE vbcppMacroExpandSkipComment(PVBCPP pThis, PVBCPPMACROEXP pExp, size_t *poff)
{
    unsigned ch = vbcppMacroExpandGetCh(pExp, poff);
    AssertReturn(ch == '*', vbcppError(pThis, "Internal error - expected '*' got '%c'", ch));

    unsigned chPrev2 = 0;
    unsigned chPrev  = 0;
    while ((ch = vbcppMacroExpandGetCh(pExp, poff)) != ~(unsigned)0)
    {
        if (ch == '/' && chPrev == '*')
            break;

        if (ch == '\r' || ch == '\n')
        {
            RTEXITCODE rcExit = vbcppMacroExpandSkipEolEx(pThis, pExp, poff, ch);
            if (rcExit != RTEXITCODE_SUCCESS)
                return rcExit;
            if (chPrev == '\\')
            {
                chPrev = chPrev2;       /* for line splicing */
                continue;
            }
        }

        chPrev2 = chPrev;
        chPrev  = ch;
    }
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE vbcppMacroExpandGrowArgArray(PVBCPP pThis, PVBCPPMACROEXP pExp, uint32_t cMinArgs)
{
    if (cMinArgs > pExp->cArgsAlloced)
    {
        void *pv = RTMemRealloc(pExp->papszArgs, cMinArgs * sizeof(char *));
        if (!pv)
            return vbcppError(pThis, "out of memory");
        pExp->papszArgs = (char **)pv;
        pExp->cArgsAlloced = cMinArgs;
    }
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE vbcppMacroExpandAddEmptyParameter(PVBCPP pThis, PVBCPPMACROEXP pExp)
{
    RTEXITCODE rcExit = vbcppMacroExpandGrowArgArray(pThis, pExp, pExp->cArgs + 1);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        char *pszArg = (char *)RTMemAllocZ(1);
        if (pszArg)
            pExp->papszArgs[pExp->cArgs++] = pszArg;
        else
            rcExit = vbcppError(pThis, "out of memory");
    }
    return rcExit;
}


static RTEXITCODE vbcppMacroExpandGatherParameters(PVBCPP pThis, PVBCPPMACROEXP pExp, size_t *poff, uint32_t cArgsHint)
{
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;

    /*
     * Free previous argument values.
     */
    while (pExp->cArgs > 0)
    {
        RTMemFree(pExp->papszArgs[--pExp->cArgs]);
        pExp->papszArgs[pExp->cArgs] = NULL;
    }

    /*
     * The current character should be an opening parenthsis.
     */
    unsigned    ch = vbcppMacroExpandGetCh(pExp, poff);
    if (ch != '(')
        return vbcppError(pThis, "Internal error - expected '(', found '%c' (#x)", ch, ch);

    /*
     * Parse the argument list.
     */
    char        chQuote      = 0;
    size_t      cbArgAlloc   = 0;
    size_t      cchArg       = 0;
    char       *pszArg       = NULL;
    size_t      cParentheses = 1;
    unsigned    chPrev       = 0;
    while ((ch = vbcppMacroExpandGetCh(pExp, poff)) != ~(unsigned)0)
    {
/** @todo check for '#directives'! */
        if (ch == ')' && !chQuote)
        {
            Assert(cParentheses >= 1);
            cParentheses--;

            /* The end? */
            if (!cParentheses)
            {
                if (cchArg)
                    while (cchArg > 0 && RT_C_IS_SPACE(pszArg[cchArg - 1]))
                        pszArg[--cchArg] = '\0';
                else if (pExp->cArgs || cArgsHint > 0)
                    rcExit = vbcppMacroExpandAddEmptyParameter(pThis, pExp);
                break;
            }
        }
        else if (ch == '('  && !chQuote)
            cParentheses++;
        else if (ch == ',' && cParentheses == 1 && !chQuote)
        {
            /* End of one argument, start of the next. */
            if (cchArg)
                while (cchArg > 0 && RT_C_IS_SPACE(pszArg[cchArg - 1]))
                    pszArg[--cchArg] = '\0';
            else
            {
                rcExit = vbcppMacroExpandAddEmptyParameter(pThis, pExp);
                if (rcExit != RTEXITCODE_SUCCESS)
                    break;
            }

            cbArgAlloc = 0;
            cchArg     = 0;
            pszArg     = NULL;
            continue;
        }
        else if (ch == '/' && !chQuote)
        {
            /* Comment? */
            unsigned ch2 = vbcppMacroExpandPeekCh(pExp, poff);
            /** @todo This ain't right wrt line splicing. */
            if (ch2 == '/' || ch2 == '*')
            {
                if (ch2 == '/')
                    rcExit = vbcppMacroExpandSkipCommentLine(pThis, pExp, poff);
                else
                    rcExit = vbcppMacroExpandSkipComment(pThis, pExp, poff);
                if (rcExit != RTEXITCODE_SUCCESS)
                    break;
                continue;
            }
        }
        else if (ch == '"')
        {
            if (!chQuote)
                chQuote = '"';
            else if (chPrev != '\\')
                chQuote = 0;
        }
        else if (ch == '\'')
        {
            if (!chQuote)
                chQuote = '\'';
            else if (chPrev != '\\')
                chQuote = 0;
        }
        else if (ch == '\\')
        {
            /* Splice lines? */
            unsigned ch2 = vbcppMacroExpandPeekCh(pExp, poff);
            if (ch2 == '\r' || ch2 == '\n')
            {
                rcExit = vbcppMacroExpandSkipEol(pThis, pExp, poff);
                if (rcExit != RTEXITCODE_SUCCESS)
                    break;
                continue;
            }
        }
        else if (cchArg == 0 && RT_C_IS_SPACE(ch))
            continue; /* ignore spaces leading up to an argument value */

        /* Append the character to the argument value, adding the argument
           to the output array if it's first character in it. */
        if (cchArg + 1 >= cbArgAlloc)
        {
            /* Add argument to the vector. */
            if (!cchArg)
            {
                rcExit = vbcppMacroExpandGrowArgArray(pThis, pExp, RT_MAX(pExp->cArgs + 1, cArgsHint));
                if (rcExit != RTEXITCODE_SUCCESS)
                    break;
                pExp->papszArgs[pExp->cArgs++] = pszArg;
            }

            /* Resize the argument value buffer. */
            cbArgAlloc = cbArgAlloc ? cbArgAlloc * 2 : 16;
            pszArg = (char *)RTMemRealloc(pszArg, cbArgAlloc);
            if (!pszArg)
            {
                rcExit = vbcppError(pThis, "out of memory");
                break;
            }
            pExp->papszArgs[pExp->cArgs - 1] = pszArg;
        }

        pszArg[cchArg++] = ch;
        pszArg[cchArg]   = '\0';
    }

    /*
     * Check that we're leaving on good terms.
     */
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        if (cParentheses)
            rcExit = vbcppError(pThis, "Missing ')'");
    }

    return rcExit;
}


/**
 * Expands the arguments referenced in the macro value.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE + msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pExp                The expansion context.
 * @param   pMacro              The macro.  Must be a function macro.
 * @param   pStrBuf             String buffer containing the result. The caller
 *                              should initialize and destroy this!
 */
static RTEXITCODE vbcppMacroExpandValueWithArguments(PVBCPP pThis, PVBCPPMACROEXP pExp, PVBCPPMACRO pMacro,
                                                     PVBCPPSTRBUF pStrBuf)
{
    Assert(pMacro->fFunction);

    /*
     * Empty?
     */
    if (   !pMacro->cchValue
        || (pMacro->cchValue == 1 && pMacro->szValue[0] == '#'))
        return RTEXITCODE_SUCCESS;

    /*
     * Parse the value.
     */
    RTEXITCODE  rcExit    = RTEXITCODE_SUCCESS;
    const char *pszSrc    = pMacro->szValue;
    const char *pszSrcSeq;
    char        ch;
    while ((ch = *pszSrc++) != '\0')
    {
        Assert(ch != '\r'); Assert(ch != '\n'); /* probably not true atm. */
        if (ch == '#')
        {
            if (*pszSrc == '#')
            {
                /* Concatenate operator. */
                rcExit = vbcppError(pThis, "The '##' operatore is not yet implemented");
            }
            else
            {
                /* Stringify macro argument. */
                rcExit = vbcppError(pThis, "The '#' operatore is not yet implemented");
            }
            return rcExit;
        }
        else if (ch == '"')
        {
            /* String litteral. */
            pszSrcSeq = pszSrc - 1;
            while ((ch = *pszSrc++) != '"')
            {
                if (ch == '\\')
                    ch = *pszSrc++;
                if (ch == '\0')
                {
                    rcExit = vbcppError(pThis, "String litteral is missing closing quote (\").");
                    break;
                }
            }
            if (rcExit != RTEXITCODE_SUCCESS)
                break;
            rcExit = vbcppStrBufAppendN(pStrBuf, pszSrcSeq, pszSrc - pszSrcSeq);
        }
        else if (ch == '\'')
        {
            /* Character constant. */
            pszSrcSeq = pszSrc - 1;
            while ((ch = *pszSrc++) != '\'')
            {
                if (ch == '\\')
                    ch = *pszSrc++;
                if (ch == '\0')
                {
                    rcExit = vbcppError(pThis, "Character constant is missing closing quote (').");
                    break;
                }
            }
            rcExit = vbcppStrBufAppendN(pStrBuf, pszSrcSeq, pszSrc - pszSrcSeq);
        }
        else if (RT_C_IS_DIGIT(ch))
        {
            /* Process numerical constants correctly (i.e. don't mess with the suffix). */
            pszSrcSeq = pszSrc - 1;
            while (   (ch = *pszSrc) != '\0'
                   && (   vbcppIsCIdentifierChar(ch)
                       || ch == '.') )
                pszSrc++;
            rcExit = vbcppStrBufAppendN(pStrBuf, pszSrcSeq, pszSrc - pszSrcSeq);
        }
        else if (RT_C_IS_SPACE(ch))
        {
            /* join spaces */
            if (RT_C_IS_SPACE(vbcppStrBufLastCh(pStrBuf)))
                continue;
            rcExit = vbcppStrBufAppendCh(pStrBuf, ch);
        }
        else if (vbcppIsCIdentifierLeadChar(ch))
        {
            /* Something we should replace? */
            pszSrcSeq = pszSrc - 1;
            while (   (ch = *pszSrc) != '\0'
                   && vbcppIsCIdentifierChar(ch))
                pszSrc++;
            size_t      cchDefine = pszSrc - pszSrcSeq;
            uint32_t    iArg;
            if (   VBCPP_BITMAP_IS_SET(pMacro->bmArgs, *pszSrcSeq)
                && (iArg = vbcppMacroLookupArg(pMacro, pszSrcSeq, cchDefine)) != UINT32_MAX)
            {
                /** @todo check out spaces here! */
                if (iArg < pMacro->cArgs)
                {
                    Assert(iArg < pExp->cArgs);
                    rcExit = vbcppStrBufAppend(pStrBuf, pExp->papszArgs[iArg]);
                    if (*pExp->papszArgs[iArg] != '\0' && rcExit == RTEXITCODE_SUCCESS)
                        rcExit = vbcppStrBufAppendCh(pStrBuf, ' ');
                }
                else
                {
                    /* __VA_ARGS__ */
                    if (iArg < pExp->cArgs)
                    {
                        for (;;)
                        {
                            rcExit = vbcppStrBufAppend(pStrBuf, pExp->papszArgs[iArg]);
                            if (rcExit != RTEXITCODE_SUCCESS)
                                break;
                            iArg++;
                            if (iArg >= pExp->cArgs)
                                break;
                            rcExit = vbcppStrBufAppendCh(pStrBuf, ',');
                            if (rcExit != RTEXITCODE_SUCCESS)
                                break;
                        }
                    }
                    if (rcExit == RTEXITCODE_SUCCESS)
                        rcExit = vbcppStrBufAppendCh(pStrBuf, ' ');
                }
            }
            /* Not an argument needing replacing. */
            else
                rcExit = vbcppStrBufAppendN(pStrBuf, pszSrcSeq, cchDefine);
        }
        else
        {
            rcExit = vbcppStrBufAppendCh(pStrBuf, ch);
        }
    }

    return rcExit;
}



/**
 * Expands the given macro.
 *
 * Caller already checked if a function macro should be expanded, i.e. whether
 * there is a parameter list.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE + msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pExp                The expansion context.
 * @param   offMacro            Offset into the expansion buffer of the macro
 *                              invocation.
 * @param   pMacro              The macro.
 * @param   offParameters       The start of the parameter list if applicable.
 *                              Ignored if not function macro.  If the
 *                              parameter list starts at the current stream
 *                              position shall be at the end of the expansion
 *                              buffer.
 */
static RTEXITCODE vbcppMacroExpandIt(PVBCPP pThis, PVBCPPMACROEXP pExp, size_t offMacro, PVBCPPMACRO pMacro,
                                     size_t offParameters)
{
    RTEXITCODE rcExit;
    Assert(offMacro + pMacro->Core.cchString <= pExp->StrBuf.cchBuf);
    Assert(!pMacro->fExpanding);

    /*
     * Function macros are kind of difficult...
     */
    if (pMacro->fFunction)
    {
        rcExit = vbcppMacroExpandGatherParameters(pThis, pExp, &offParameters, pMacro->cArgs + pMacro->fVarArg);
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            if (pExp->cArgs > pMacro->cArgs && !pMacro->fVarArg)
                rcExit = vbcppError(pThis, "Too many arguments to macro '%s' - found %u, expected %u",
                                    pMacro->Core.pszString, pExp->cArgs, pMacro->cArgs);
            else if (pExp->cArgs < pMacro->cArgs)
                rcExit = vbcppError(pThis, "Too few arguments to macro '%s' - found %u, expected %u",
                                    pMacro->Core.pszString, pExp->cArgs, pMacro->cArgs);
        }
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            VBCPPSTRBUF ValueBuf;
            vbcppStrBufInit(&ValueBuf, pThis);
            rcExit = vbcppMacroExpandValueWithArguments(pThis, pExp, pMacro, &ValueBuf);
            if (rcExit == RTEXITCODE_SUCCESS)
                rcExit = vbcppMacroExpandReplace(pThis, pExp, offMacro, offParameters - offMacro,
                                                 ValueBuf.pszBuf, ValueBuf.cchBuf);
            vbcppStrBufDelete(&ValueBuf);
        }
    }
    /*
     * Object-like macros are easy. :-)
     */
    else
        rcExit = vbcppMacroExpandReplace(pThis, pExp, offMacro, pMacro->Core.cchString, pMacro->szValue, pMacro->cchValue);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
#if 0 /* wrong */
        /*
         * Push the macro onto the stack.
         */
        pMacro->fExpanding   = true;
        pMacro->pUpExpanding = pExp->pMacroStack;
        pExp->pMacroStack    = pMacro;
#endif
    }

    return rcExit;
}


/**
 * Looks for a left parenthesis in the macro expansion buffer and the input
 * stream.
 *
 * @retval  true if found. The stream position at opening parenthesis.
 * @retval  false if not found. The stream position is unchanged.
 *
 * @param   pThis               The C preprocessor instance.
 * @param   pExp                The expansion context.
 * @param   poff                The current offset in the expansion context.
 *                              Will be updated on success.
 *
 * @sa vbcppInputLookForLeftParenthesis
 */
static bool vbcppMacroExpandLookForLeftParenthesis(PVBCPP pThis, PVBCPPMACROEXP pExp, size_t *poff)
{
    /*
     * Search the buffer first. (No comments there.)
     */
    size_t off = *poff;
    while (off < pExp->StrBuf.cchBuf)
    {
        char ch = pExp->StrBuf.pszBuf[off];
        if (!RT_C_IS_SPACE(ch))
        {
            if (ch == '(')
            {
                *poff = off;
                return true;
            }
            return false;
        }
        off++;
    }

    /*
     * Reached the end of the buffer, continue searching in the stream.
     */
    PSCMSTREAM pStrmInput = pExp->pStrmInput;
    size_t     offSaved   = ScmStreamTell(pStrmInput);
    /*RTEXITCODE rcExit = */ vbcppProcessSkipWhiteEscapedEolAndComments(pThis, pStrmInput);
    unsigned ch = ScmStreamPeekCh(pStrmInput);
    if (ch == '(')
    {
        *poff = pExp->StrBuf.cchBuf;
        return true;
    }

    int rc = ScmStreamSeekAbsolute(pStrmInput, offSaved);
    AssertFatalRC(rc);
    return false;
}


/**
 * Implements the 'defined' unary operator for \#if and \#elif expressions.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE + msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pExp                The expansion context.
 * @param   offStart            The expansion buffer offset where the 'defined'
 *                              occurs.
 * @param   poff                Where to store the offset at which the re-scan
 *                              shall resume upon return.
 */
static RTEXITCODE vbcppMacroExpandDefinedOperator(PVBCPP pThis, PVBCPPMACROEXP pExp, size_t offStart, size_t *poff)
{
    Assert(!pExp->pStrmInput); /* offset usage below. */

    /*
     * Skip white space.
     */
    unsigned ch;
    while ((ch = vbcppMacroExpandGetCh(pExp, poff)) != ~(unsigned)0)
        if (!RT_C_IS_SPACE(ch))
            break;
    bool const fWithParenthesis = ch == '(';
    if (fWithParenthesis)
        while ((ch = vbcppMacroExpandGetCh(pExp, poff)) != ~(unsigned)0)
            if (!RT_C_IS_SPACE(ch))
                break;

    /*
     * Macro identifier.
     */
    if (!vbcppIsCIdentifierLeadChar(ch))
        return vbcppError(pThis, "Expected macro name after 'defined' operator");

    size_t const offDefine = *poff - 1;
    while ((ch = vbcppMacroExpandGetCh(pExp, poff)) != ~(unsigned)0)
        if (!vbcppIsCIdentifierChar(ch))
            break;
    size_t const cchDefine = *poff - offDefine - 1;

    /*
     * Check for closing parenthesis.
     */
    if (fWithParenthesis)
    {
        while (RT_C_IS_SPACE(ch))
            ch = vbcppMacroExpandGetCh(pExp, poff);
        if (ch != ')')
            return vbcppError(pThis, "Expected closing parenthesis after macro name");
    }

    /*
     * Do the job.
     */
    const char *pszResult = vbcppMacroExists(pThis, &pExp->StrBuf.pszBuf[offDefine], cchDefine)
                          ? "1" : "0";
    RTEXITCODE  rcExit    = vbcppMacroExpandReplace(pThis, pExp, offStart, *poff - offStart, pszResult, 1);
    *poff = offStart + 1;
    return rcExit;
}


/**
 * Re-scan the expanded macro.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE + msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pExp                The expansion context.
 * @param   enmMode             The re-scan mode.
 * @param   pcReplacements      Where to return the number of replacements
 *                              performed.  Optional.
 * @param   pcDefinedUnknown    Where to return the number of defined() checks
 *                              on unknown macros.  Optional.
 */
static RTEXITCODE vbcppMacroExpandReScan(PVBCPP pThis, PVBCPPMACROEXP pExp, VBCPPMACRORESCANMODE enmMode,
                                         size_t *pcReplacements, size_t *pcDefinedUnknown)
{
    RTEXITCODE  rcExit          = RTEXITCODE_SUCCESS;
    size_t      cReplacements   = 0;
    size_t      cDefinedUnknown = 0;
    size_t      off             = 0;
    unsigned    ch;
    while (   off < pExp->StrBuf.cchBuf
           && (ch = vbcppMacroExpandGetCh(pExp, &off)) != ~(unsigned)0)
    {
        /*
         * String litteral or character constant.
         */
        if (ch == '\'' || ch == '"')
        {
            unsigned const chEndQuote = ch;
            while (   off < pExp->StrBuf.cchBuf
                   && (ch = vbcppMacroExpandGetCh(pExp, &off)) != ~(unsigned)0)
            {
                if (ch == '\\')
                {
                    ch = vbcppMacroExpandGetCh(pExp, &off);
                    if (ch == ~(unsigned)0)
                        break;
                }
                else if (ch == chEndQuote)
                    break;
            }
            if (ch == ~(unsigned)0)
                return vbcppError(pThis, "Missing end quote (%c)", chEndQuote);
        }
        /*
         * Number constant.
         */
        else if (   RT_C_IS_DIGIT(ch)
                 || (   ch == '.'
                     && off + 1 < pExp->StrBuf.cchBuf
                     && RT_C_IS_DIGIT(vbcppMacroExpandPeekCh(pExp, &off))
                    )
                )
        {
            while (   off < pExp->StrBuf.cchBuf
                   && (ch = vbcppMacroExpandPeekCh(pExp, &off)) != ~(unsigned)0
                   && vbcppIsCIdentifierChar(ch) )
                vbcppMacroExpandGetCh(pExp, &off);
        }
        /*
         * Something that can be replaced?
         */
        else if (vbcppIsCIdentifierLeadChar(ch))
        {
            size_t offDefine = off - 1;
            while (   off < pExp->StrBuf.cchBuf
                   && (ch = vbcppMacroExpandPeekCh(pExp, &off)) != ~(unsigned)0
                   && vbcppIsCIdentifierChar(ch) )
                vbcppMacroExpandGetCh(pExp, &off);
            size_t cchDefine = off - offDefine;

            PVBCPPMACRO pMacro = vbcppMacroLookup(pThis, &pExp->StrBuf.pszBuf[offDefine], cchDefine);
            if (   pMacro
                && (   !pMacro->fFunction
                    || vbcppMacroExpandLookForLeftParenthesis(pThis, pExp, &off)) )
            {
                cReplacements++;
                rcExit = vbcppMacroExpandIt(pThis, pExp, offDefine, pMacro, off);
                off = offDefine;
            }
            else
            {
                if (   !pMacro
                         && enmMode == kMacroReScanMode_Expression
                         && cchDefine == sizeof("defined") - 1
                         && !strncmp(&pExp->StrBuf.pszBuf[offDefine], "defined", cchDefine))
                {
                    cReplacements++;
                    cDefinedUnknown++;
                    rcExit = vbcppMacroExpandDefinedOperator(pThis, pExp, offDefine, &off);
                }
                else
                    off = offDefine + cchDefine;
            }
        }
        else
        {
            Assert(RT_C_IS_SPACE(ch) || RT_C_IS_PUNCT(ch));
            Assert(ch != '\r' && ch != '\n');
        }
    }

    if (pcReplacements)
        *pcReplacements = cReplacements;
    if (pcDefinedUnknown)
        *pcDefinedUnknown = cDefinedUnknown;
    return rcExit;
}


/**
 * Cleans up the expansion context.
 *
 * This involves clearing VBCPPMACRO::fExpanding and VBCPPMACRO::pUpExpanding,
 * and freeing the memory resources associated with the expansion context.
 *
 * @param   pExp                The expansion context.
 */
static void vbcppMacroExpandCleanup(PVBCPPMACROEXP pExp)
{
#if 0
    while (pExp->pMacroStack)
    {
        PVBCPPMACRO pMacro = pExp->pMacroStack;
        pExp->pMacroStack = pMacro->pUpExpanding;

        pMacro->fExpanding   = false;
        pMacro->pUpExpanding = NULL;
    }
#endif

    while (pExp->cArgs > 0)
    {
        RTMemFree(pExp->papszArgs[--pExp->cArgs]);
        pExp->papszArgs[pExp->cArgs] = NULL;
    }

    RTMemFree(pExp->papszArgs);
    pExp->papszArgs = NULL;

    vbcppStrBufDelete(&pExp->StrBuf);
}



/**
 * Frees a define.
 *
 * @returns VINF_SUCCESS (used when called by RTStrSpaceDestroy)
 * @param   pStr                Pointer to the VBCPPMACRO::Core member.
 * @param   pvUser              Unused.
 */
static DECLCALLBACK(int) vbcppMacroFree(PRTSTRSPACECORE pStr, void *pvUser)
{
    RTMemFree(pStr);
    NOREF(pvUser);
    return VINF_SUCCESS;
}


/**
 * Removes a define.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE + msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pszDefine           The define name, no argument list or anything.
 * @param   cchDefine           The length of the name. RTSTR_MAX is ok.
 * @param   fExplicitUndef      Explicit undefinition, that is, in a selective
 *                              preprocessing run it will evaluate to undefined.
 */
static RTEXITCODE vbcppMacroUndef(PVBCPP pThis, const char *pszDefine, size_t cchDefine, bool fExplicitUndef)
{
    PRTSTRSPACECORE pHit = RTStrSpaceGetN(&pThis->StrSpace, pszDefine, cchDefine);
    if (pHit)
    {
        RTStrSpaceRemove(&pThis->StrSpace, pHit->pszString);
        vbcppMacroFree(pHit, NULL);
    }

    if (fExplicitUndef)
    {
        if (cchDefine == RTSTR_MAX)
            cchDefine = strlen(pszDefine);

        PRTSTRSPACECORE pStr = (PRTSTRSPACECORE)RTMemAlloc(sizeof(*pStr) + cchDefine + 1);
        if (!pStr)
            return vbcppError(pThis, "out of memory");
        char *pszDst = (char *)(pStr + 1);
        pStr->pszString = pszDst;
        memcpy(pszDst, pszDefine, cchDefine);
        pszDst[cchDefine] = '\0';
        if (!RTStrSpaceInsert(&pThis->UndefStrSpace, pStr))
            RTMemFree(pStr);
    }

    return RTEXITCODE_SUCCESS;
}


/**
 * Inserts a define (rejecting and freeing it in some case).
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE + msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pMacro              The define to insert.
 */
static RTEXITCODE vbcppMacroInsert(PVBCPP pThis, PVBCPPMACRO pMacro)
{
    /*
     * Reject illegal macro names.
     */
    if (!strcmp(pMacro->Core.pszString, "defined"))
    {
        RTEXITCODE rcExit = vbcppError(pThis, "Cannot use '%s' as a macro name", pMacro->Core.pszString);
        vbcppMacroFree(&pMacro->Core, NULL);
        return rcExit;
    }

    /*
     * Ignore in source-file defines when doing selective preprocessing.
     */
    if (   !pThis->fRespectSourceDefines
        && !pMacro->fCmdLine)
    {
        /* Ignore*/
        vbcppMacroFree(&pMacro->Core, NULL);
        return RTEXITCODE_SUCCESS;
    }

    /*
     * Insert it and update the lead character hint bitmap.
     */
    if (RTStrSpaceInsert(&pThis->StrSpace, &pMacro->Core))
        VBCPP_BITMAP_SET(pThis->bmDefined, *pMacro->Core.pszString);
    else
    {
        /*
         * Duplicate. When doing selective D preprocessing, let the command
         * line take precendece.
         */
        PVBCPPMACRO pOld = (PVBCPPMACRO)RTStrSpaceGet(&pThis->StrSpace, pMacro->Core.pszString); Assert(pOld);
        if (   pThis->fAllowRedefiningCmdLineDefines
            || pMacro->fCmdLine == pOld->fCmdLine)
        {
            if (pMacro->fCmdLine)
                RTMsgWarning("Redefining '%s'", pMacro->Core.pszString);

            RTStrSpaceRemove(&pThis->StrSpace, pOld->Core.pszString);
            vbcppMacroFree(&pOld->Core, NULL);

            bool fRc = RTStrSpaceInsert(&pThis->StrSpace, &pMacro->Core);
            Assert(fRc); NOREF(fRc);
        }
        else
        {
            RTMsgWarning("Ignoring redefinition of '%s'", pMacro->Core.pszString);
            vbcppMacroFree(&pMacro->Core, NULL);
        }
    }

    return RTEXITCODE_SUCCESS;
}


/**
 * Adds a define.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE + msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pszDefine           The define name, no parameter list.
 * @param   cchDefine           The length of the name.
 * @param   pszParams           The parameter list.
 * @param   cchParams           The length of the parameter list.
 * @param   pszValue            The value.
 * @param   cchDefine           The length of the value.
 * @param   fCmdLine            Set if originating on the command line.
 */
static RTEXITCODE vbcppMacroAddFn(PVBCPP pThis, const char *pszDefine, size_t cchDefine,
                                  const char *pszParams, size_t cchParams,
                                  const char *pszValue, size_t cchValue,
                                  bool fCmdLine)

{
    Assert(RTStrNLen(pszDefine, cchDefine) == cchDefine);
    Assert(RTStrNLen(pszParams, cchParams) == cchParams);
    Assert(RTStrNLen(pszValue,  cchValue)  == cchValue);

    /*
     * Determin the number of arguments and how much space their names
     * requires.  Performing syntax validation while parsing.
     */
    bool     fVariadic   = false;
    size_t   cchArgNames = 0;
    uint32_t cArgs       = 0;
    for (size_t off = 0; off < cchParams; off++)
    {
        /* Skip blanks and maybe one comma. */
        bool fIgnoreComma = cArgs != 0;
        while (off < cchParams)
        {
            if (   !RT_C_IS_SPACE(pszParams[off])
                && (   pszParams[off] != '\\'
                    || (pszParams[off + 1] != '\n' && pszParams[off + 1] != '\r')))
            {
                if (pszParams[off] != ',' || !fIgnoreComma)
                {
                    if (vbcppIsCIdentifierLeadChar(pszParams[off]))
                        break;

                    /* Check for variadic macro argument, just parsing it for now. */
                    if (   pszParams[off]     == '.'
                        && off + 3 <= cchParams
                        && pszParams[off + 1] == '.'
                        && pszParams[off + 2] == '.')
                    {
                        size_t const offSaved = off;
                        off += 3;
                        while (off < cchParams && RT_C_IS_SPACE(pszParams[off]))
                            off++;
                        if (off == cchParams)
                        {
                            cArgs++;
                            cchArgNames += sizeof("...");
                            fVariadic = true;
                            break;
                        }
                        off = offSaved;
                    }
                    return vbcppErrorPos(pThis, &pszParams[off], "Unexpected character (%#x, off=%zu)", pszParams[off], off);
                }
                fIgnoreComma = false;
            }
            off++;
        }
        if (off >= cchParams)
            break;

        /* Found and argument. First character is already validated. */
        cArgs++;
        cchArgNames += 2;
        off++;
        while (   off < cchParams
               && vbcppIsCIdentifierChar(pszParams[off]))
            off++, cchArgNames++;
    }

    /*
     * Allocate a structure.
     */
    size_t    cbDef = RT_UOFFSETOF_DYN(VBCPPMACRO, szValue[cchValue + 1 + cchDefine + 1 + cchArgNames])
                    + sizeof(const char *) * cArgs;
    cbDef = RT_ALIGN_Z(cbDef, sizeof(const char *));
    PVBCPPMACRO pMacro  = (PVBCPPMACRO)RTMemAlloc(cbDef);
    if (!pMacro)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "out of memory");

    char *pszDst = &pMacro->szValue[cchValue + 1];
    pMacro->Core.pszString = pszDst;
    memcpy(pszDst, pszDefine, cchDefine);
    pszDst += cchDefine;
    *pszDst++ = '\0';
    pMacro->fFunction   = true;
    pMacro->fVarArg     = fVariadic;
    pMacro->fCmdLine    = fCmdLine;
    pMacro->fExpanding  = false;
    pMacro->cArgs       = cArgs;
    pMacro->papszArgs   = (const char **)((uintptr_t)pMacro + cbDef - sizeof(const char *) * cArgs);
    VBCPP_BITMAP_EMPTY(pMacro->bmArgs);
    pMacro->cchValue    = cchValue;
    memcpy(pMacro->szValue, pszValue, cchValue);
    pMacro->szValue[cchValue] = '\0';

    /*
     * Set up the arguments.
     */
    uint32_t iArg = 0;
    for (size_t off = 0; off < cchParams; off++)
    {
        /* Skip blanks and maybe one comma. */
        bool fIgnoreComma = cArgs != 0;
        while (off < cchParams)
        {
            if (   !RT_C_IS_SPACE(pszParams[off])
                && (   pszParams[off] != '\\'
                    || (pszParams[off + 1] != '\n' && pszParams[off + 1] != '\r')))
            {
                if (pszParams[off] != ',' || !fIgnoreComma)
                    break;
                fIgnoreComma = false;
            }
            off++;
        }
        if (off >= cchParams)
            break;

        /* Found and argument. First character is already validated. */
        pMacro->papszArgs[iArg] = pszDst;
        if (!fVariadic || iArg + 1 < cArgs)
        {
            VBCPP_BITMAP_SET(pMacro->bmArgs, pszParams[off]);
            do
            {
                *pszDst++ = pszParams[off++];
            } while (   off < cchParams
                     && vbcppIsCIdentifierChar(pszParams[off]));
        }
        else
        {
            Assert(strncmp(&pszParams[off], "...", 3) == 0);
            VBCPP_BITMAP_SET(pMacro->bmArgs, '_'); /* __VA_ARGS__ */
            off += 3;
            *pszDst++ = '.';
            *pszDst++ = '.';
            *pszDst++ = '.';
        }
        *pszDst++ = '\0';
        iArg++;
    }
    Assert((uintptr_t)pszDst <= (uintptr_t)pMacro->papszArgs);

    return vbcppMacroInsert(pThis, pMacro);
}


/**
 * Adds a define.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE + msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pszDefine           The define name and optionally the argument
 *                              list.
 * @param   cchDefine           The length of the name. RTSTR_MAX is ok.
 * @param   pszValue            The value.
 * @param   cchDefine           The length of the value. RTSTR_MAX is ok.
 * @param   fCmdLine            Set if originating on the command line.
 */
static RTEXITCODE vbcppMacroAdd(PVBCPP pThis, const char *pszDefine, size_t cchDefine,
                                const char *pszValue, size_t cchValue, bool fCmdLine)
{
    /*
     * We need the lengths. Trim the input.
     */
    if (cchDefine == RTSTR_MAX)
        cchDefine = strlen(pszDefine);
    while (cchDefine > 0 && RT_C_IS_SPACE(*pszDefine))
        pszDefine++, cchDefine--;
    while (cchDefine > 0 && RT_C_IS_SPACE(pszDefine[cchDefine - 1]))
        cchDefine--;
    if (!cchDefine)
        return vbcppErrorPos(pThis, pszDefine, "The define has no name");

    if (cchValue == RTSTR_MAX)
        cchValue = strlen(pszValue);
    while (cchValue > 0 && RT_C_IS_SPACE(*pszValue))
        pszValue++, cchValue--;
    while (cchValue > 0 && RT_C_IS_SPACE(pszValue[cchValue - 1]))
        cchValue--;

    /*
     * Arguments make the job a bit more annoying.  Handle that elsewhere
     */
    const char *pszParams = (const char *)memchr(pszDefine, '(', cchDefine);
    if (pszParams)
    {
        size_t cchParams = pszDefine + cchDefine - pszParams;
        cchDefine -= cchParams;
        if (!vbcppValidateCIdentifier(pThis, pszDefine, cchDefine))
            return RTEXITCODE_FAILURE;
        if (pszParams[cchParams - 1] != ')')
            return vbcppErrorPos(pThis, pszParams + cchParams - 1, "Missing closing parenthesis");
        pszParams++;
        cchParams -= 2;
        return vbcppMacroAddFn(pThis, pszDefine, cchDefine, pszParams, cchParams, pszValue, cchValue, fCmdLine);
    }

    /*
     * Simple define, no arguments.
     */
    if (!vbcppValidateCIdentifier(pThis, pszDefine, cchDefine))
        return RTEXITCODE_FAILURE;

    PVBCPPMACRO pMacro = (PVBCPPMACRO)RTMemAlloc(RT_UOFFSETOF_DYN(VBCPPMACRO, szValue[cchValue + 1 + cchDefine + 1]));
    if (!pMacro)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "out of memory");

    pMacro->Core.pszString = &pMacro->szValue[cchValue + 1];
    memcpy((char *)pMacro->Core.pszString, pszDefine, cchDefine);
    ((char *)pMacro->Core.pszString)[cchDefine] = '\0';
    pMacro->fFunction   = false;
    pMacro->fVarArg     = false;
    pMacro->fCmdLine    = fCmdLine;
    pMacro->fExpanding  = false;
    pMacro->cArgs       = 0;
    pMacro->papszArgs   = NULL;
    VBCPP_BITMAP_EMPTY(pMacro->bmArgs);
    pMacro->cchValue    = cchValue;
    memcpy(pMacro->szValue, pszValue, cchValue);
    pMacro->szValue[cchValue] = '\0';

    return vbcppMacroInsert(pThis, pMacro);
}


/**
 * Tries to convert a define into an inline D constant.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pMacro              The macro.
 */
static RTEXITCODE vbcppMacroTryConvertToInlineD(PVBCPP pThis, PVBCPPMACRO pMacro)
{
    AssertReturn(pMacro, vbcppError(pThis, "Internal error"));
    if (pMacro->fFunction)
        return RTEXITCODE_SUCCESS;

    /*
     * Do some simple macro resolving. (Mostly to make x86.h work.)
     */
    const char *pszDefine = pMacro->Core.pszString;
    const char *pszValue  = pMacro->szValue;
    size_t      cchValue  = pMacro->cchValue;

    unsigned    i = 0;
    PVBCPPMACRO pMacro2;
    while (   i < 10
           && cchValue > 0
           && vbcppIsCIdentifierLeadChar(*pszValue)
           && (pMacro2 = vbcppMacroLookup(pThis, pszValue, cchValue)) != NULL
           && !pMacro2->fFunction )
    {
        pszValue = pMacro2->szValue;
        cchValue = pMacro2->cchValue;
        i++;
    }

    if (!pMacro->cchValue)
        return RTEXITCODE_SUCCESS;


    /*
     * A lone value?
     */
    ssize_t  cch = 0;
    uint64_t u64;
    char    *pszNext;
    int rc = RTStrToUInt64Ex(pszValue, &pszNext, 0, &u64);
    if (RT_SUCCESS(rc))
    {
        if (   rc == VWRN_TRAILING_SPACES
            || rc == VWRN_NEGATIVE_UNSIGNED
            || rc == VWRN_NUMBER_TOO_BIG)
            return RTEXITCODE_SUCCESS;
        const char *pszType;
        if (rc == VWRN_TRAILING_CHARS)
        {
            if (!strcmp(pszNext, "u") || !strcmp(pszNext, "U"))
                pszType = "uint32_t";
            else if (!strcmp(pszNext, "ul") || !strcmp(pszNext, "UL"))
                pszType = "uintptr_t";
            else if (!strcmp(pszNext, "ull") || !strcmp(pszNext, "ULL"))
                pszType = "uint64_t";
            else
                pszType = NULL;
        }
        else if (u64 <= UINT8_MAX)
            pszType = "uint8_t";
        else if (u64 <= UINT16_MAX)
            pszType = "uint16_t";
        else if (u64 <= UINT32_MAX)
            pszType = "uint32_t";
        else
            pszType = "uint64_t";
        if (!pszType)
            return RTEXITCODE_SUCCESS;
        cch = ScmStreamPrintf(&pThis->StrmOutput, "inline %s %s = %.*s;\n",
                              pszType, pszDefine, pszNext - pszValue, pszValue);
    }
    /*
     * A value wrapped in a constant macro?
     */
    else if (   (pszNext = (char *)strchr(pszValue, '(')) != NULL
             && pszValue[cchValue - 1] == ')' )
    {
        size_t      cchPrefix = pszNext - pszValue;
        size_t      cchInnerValue  = cchValue - cchPrefix - 2;
        const char *pchInnerValue  = &pszValue[cchPrefix + 1];
        while (cchInnerValue > 0 && RT_C_IS_SPACE(*pchInnerValue))
            cchInnerValue--, pchInnerValue++;
        while (cchInnerValue > 0 && RT_C_IS_SPACE(pchInnerValue[cchInnerValue - 1]))
            cchInnerValue--;
        if (!cchInnerValue || !RT_C_IS_XDIGIT(*pchInnerValue))
            return RTEXITCODE_SUCCESS;

        rc = RTStrToUInt64Ex(pchInnerValue, &pszNext, 0, &u64);
        if (   RT_FAILURE(rc)
            || rc == VWRN_TRAILING_SPACES
            || rc == VWRN_NEGATIVE_UNSIGNED
            || rc == VWRN_NUMBER_TOO_BIG)
            return RTEXITCODE_SUCCESS;

        const char *pszType;
#define MY_MATCH_STR(a_sz)  (sizeof(a_sz) - 1 == cchPrefix && !strncmp(pszValue, a_sz, sizeof(a_sz) - 1))
        if (MY_MATCH_STR("UINT8_C"))
            pszType = "uint8_t";
        else if (MY_MATCH_STR("UINT16_C"))
            pszType = "uint16_t";
        else if (MY_MATCH_STR("UINT32_C"))
            pszType = "uint32_t";
        else if (MY_MATCH_STR("UINT64_C"))
            pszType = "uint64_t";
        else
            pszType = NULL;
        if (pszType)
            cch = ScmStreamPrintf(&pThis->StrmOutput, "inline %s %s = %.*s;\n",
                                  pszType, pszDefine, cchInnerValue, pchInnerValue);
        else if (MY_MATCH_STR("RT_BIT") || MY_MATCH_STR("RT_BIT_32"))
            cch = ScmStreamPrintf(&pThis->StrmOutput, "inline uint32_t %s = 1U << %llu;\n",
                                  pszDefine, u64);
        else if (MY_MATCH_STR("RT_BIT_64"))
            cch = ScmStreamPrintf(&pThis->StrmOutput, "inline uint64_t %s = 1ULL << %llu;\n",
                                  pszDefine, u64);
        else
            return RTEXITCODE_SUCCESS;
#undef MY_MATCH_STR
    }
    /* Dunno what this is... */
    else
        return RTEXITCODE_SUCCESS;

    /*
     * Check for output error and clear the output suppression indicator.
     */
    if (cch < 0)
        return vbcppError(pThis, "Output error");

    pThis->fJustDroppedLine = false;
    return RTEXITCODE_SUCCESS;
}



/**
 * Processes a abbreviated line number directive.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 * @param   offStart            The stream position where the directive
 *                              started (for pass thru).
 */
static RTEXITCODE vbcppDirectiveDefine(PVBCPP pThis, PSCMSTREAM pStrmInput, size_t offStart)
{
    RT_NOREF_PV(offStart);

    /*
     * Parse it.
     */
    RTEXITCODE rcExit = vbcppProcessSkipWhiteEscapedEolAndComments(pThis, pStrmInput);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        size_t      cchDefine;
        const char *pchDefine = ScmStreamCGetWord(pStrmInput, &cchDefine);
        if (pchDefine)
        {
            /* If it's a function style define, parse out the parameter list. */
            size_t      cchParams = 0;
            const char *pchParams = NULL;
            unsigned    ch = ScmStreamPeekCh(pStrmInput);
            if (ch == '(')
            {
                ScmStreamGetCh(pStrmInput);
                pchParams = ScmStreamGetCur(pStrmInput);

                unsigned chPrev = ch;
                while ((ch = ScmStreamPeekCh(pStrmInput)) != ~(unsigned)0)
                {
                    if (ch == '\r' || ch == '\n')
                    {
                        if (chPrev != '\\')
                        {
                            rcExit = vbcppError(pThis, "Missing ')'");
                            break;
                        }
                        ScmStreamSeekByLine(pStrmInput, ScmStreamTellLine(pStrmInput) + 1);
                    }
                    if (ch == ')')
                    {
                        cchParams = ScmStreamGetCur(pStrmInput) - pchParams;
                        ScmStreamGetCh(pStrmInput);
                        break;
                    }
                    chPrev = ch;
                    ScmStreamGetCh(pStrmInput);
                }
            }
            /* The simple kind. */
            else if (!RT_C_IS_SPACE(ch) && ch != ~(unsigned)0)
                rcExit = vbcppError(pThis, "Expected whitespace after macro name");

            /* Parse out the value. */
            if (rcExit == RTEXITCODE_SUCCESS)
                rcExit = vbcppProcessSkipWhiteEscapedEolAndComments(pThis, pStrmInput);
            if (rcExit == RTEXITCODE_SUCCESS)
            {
                size_t      offValue = ScmStreamTell(pStrmInput);
                const char *pchValue = ScmStreamGetCur(pStrmInput);
                unsigned    chPrev = ch;
                while ((ch = ScmStreamPeekCh(pStrmInput)) != ~(unsigned)0)
                {
                    if (ch == '\r' || ch == '\n')
                    {
                        if (chPrev != '\\')
                            break;
                        ScmStreamSeekByLine(pStrmInput, ScmStreamTellLine(pStrmInput) + 1);
                    }
                    chPrev = ScmStreamGetCh(pStrmInput);
                }
                size_t cchValue = ScmStreamGetCur(pStrmInput) - pchValue;

                /*
                 * Execute.
                 */
                if (pchParams)
                    rcExit = vbcppMacroAddFn(pThis, pchDefine, cchDefine, pchParams, cchParams, pchValue, cchValue, false);
                else
                    rcExit = vbcppMacroAdd(pThis, pchDefine, cchDefine, pchValue, cchValue, false);

                /*
                 * Pass thru?
                 */
                if (   rcExit == RTEXITCODE_SUCCESS
                    && pThis->fPassThruDefines)
                {
                    unsigned cchIndent = pThis->pCondStack ? pThis->pCondStack->iKeepLevel : 0;
                    ssize_t  cch;
                    if (pchParams)
                        cch = ScmStreamPrintf(&pThis->StrmOutput, "#%*sdefine %.*s(%.*s)",
                                              cchIndent, "", cchDefine, pchDefine, cchParams, pchParams);
                    else
                        cch = ScmStreamPrintf(&pThis->StrmOutput, "#%*sdefine %.*s",
                                              cchIndent, "", cchDefine, pchDefine);
                    if (cch > 0)
                        vbcppOutputComment(pThis, pStrmInput, offValue, cch, 1);
                    else
                        rcExit = vbcppError(pThis, "output error");
                }
                else if (   rcExit == RTEXITCODE_SUCCESS
                         && pThis->enmMode == kVBCppMode_SelectiveD)
                    rcExit = vbcppMacroTryConvertToInlineD(pThis, vbcppMacroLookup(pThis, pchDefine, cchDefine));
                else
                    pThis->fJustDroppedLine = true;
            }
        }
    }
    return rcExit;
}


/**
 * Processes a abbreviated line number directive.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 * @param   offStart            The stream position where the directive
 *                              started (for pass thru).
 */
static RTEXITCODE vbcppDirectiveUndef(PVBCPP pThis, PSCMSTREAM pStrmInput, size_t offStart)
{
    RT_NOREF_PV(offStart);

    /*
     * Parse it.
     */
    RTEXITCODE rcExit = vbcppProcessSkipWhiteEscapedEolAndComments(pThis, pStrmInput);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        size_t      cchDefine;
        const char *pchDefine = ScmStreamCGetWord(pStrmInput, &cchDefine);
        if (pchDefine)
        {
            size_t offMaybeComment = vbcppProcessSkipWhite(pStrmInput);
            rcExit = vbcppProcessSkipWhiteEscapedEolAndCommentsCheckEol(pThis, pStrmInput);
            if (rcExit == RTEXITCODE_SUCCESS)
            {
                /*
                 * Take action.
                 */
                PVBCPPMACRO pMacro = vbcppMacroLookup(pThis, pchDefine, cchDefine);
                if (    pMacro
                    &&  pThis->fRespectSourceDefines
                    &&  (   !pMacro->fCmdLine
                         || pThis->fAllowRedefiningCmdLineDefines ) )
                {
                    RTStrSpaceRemove(&pThis->StrSpace, pMacro->Core.pszString);
                    vbcppMacroFree(&pMacro->Core, NULL);
                }

                /*
                 * Pass thru.
                 */
                if (pThis->fPassThruDefines)
                {
                    unsigned cchIndent = pThis->pCondStack ? pThis->pCondStack->iKeepLevel : 0;
                    ssize_t  cch = ScmStreamPrintf(&pThis->StrmOutput, "#%*sundef %.*s",
                                                   cchIndent, "", cchDefine, pchDefine);
                    if (cch > 0)
                        vbcppOutputComment(pThis, pStrmInput, offMaybeComment, cch, 1);
                    else
                        rcExit = vbcppError(pThis, "output error");
                }

            }
        }
        else
            rcExit = vbcppError(pThis, "Malformed #ifndef");
    }
    return rcExit;

}





/*
 *
 *
 * C O N D I T I O N A L S
 * C O N D I T I O N A L S
 * C O N D I T I O N A L S
 * C O N D I T I O N A L S
 * C O N D I T I O N A L S
 *
 *
 */


/**
 * Combines current stack result with the one being pushed.
 *
 * @returns Combined result.
 * @param   enmEvalPush         The result of the condition being pushed.
 * @param   enmEvalStack        The current stack result.
 */
static VBCPPEVAL vbcppCondCombine(VBCPPEVAL enmEvalPush, VBCPPEVAL enmEvalStack)
{
    if (enmEvalStack == kVBCppEval_False)
        return kVBCppEval_False;
    return enmEvalPush;
}


/**
 * Pushes an conditional onto the stack.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The current input stream.
 * @param   offStart            Not currently used, using @a pchCondition and
 *                              @a cchCondition instead.
 * @param   enmKind             The kind of conditional.
 * @param   enmResult           The result of the evaluation.
 * @param   pchCondition        The raw condition.
 * @param   cchCondition        The length of @a pchCondition.
 */
static RTEXITCODE vbcppCondPush(PVBCPP pThis, PSCMSTREAM pStrmInput, size_t offStart,
                                VBCPPCONDKIND enmKind, VBCPPEVAL enmResult,
                                const char *pchCondition, size_t cchCondition)
{
    RT_NOREF_PV(offStart); RT_NOREF_PV(pStrmInput);


    if (pThis->cCondStackDepth >= _64K)
        return vbcppError(pThis, "Too many nested #if/#ifdef/#ifndef statements");

    /*
     * Allocate a new entry and push it.
     */
    PVBCPPCOND pCond = (PVBCPPCOND)RTMemAlloc(sizeof(*pCond));
    if (!pCond)
        return vbcppError(pThis, "out of memory");

    PVBCPPCOND pUp = pThis->pCondStack;
    pCond->enmKind          = enmKind;
    pCond->enmResult        = enmResult;
    pCond->enmStackResult   = pUp ? vbcppCondCombine(enmResult, pUp->enmStackResult) : enmResult;
    pCond->fSeenElse        = false;
    pCond->fElIfDecided     = enmResult == kVBCppEval_True;
    pCond->iLevel           = pThis->cCondStackDepth;
    pCond->iKeepLevel       = (pUp ? pUp->iKeepLevel : 0) + (enmResult == kVBCppEval_Undecided);
    pCond->pchCond          = pchCondition;
    pCond->cchCond          = cchCondition;

    pCond->pUp              = pThis->pCondStack;
    pThis->pCondStack       = pCond;
    pThis->fIf0Mode         = pCond->enmStackResult == kVBCppEval_False;

    /*
     * Do pass thru.
     */
    if (   !pThis->fIf0Mode
        && enmResult == kVBCppEval_Undecided)
    {
        /** @todo this is stripping comments of \#ifdef and \#ifndef atm. */
        const char *pszDirective;
        switch (enmKind)
        {
            case kVBCppCondKind_If:     pszDirective = "if"; break;
            case kVBCppCondKind_IfDef:  pszDirective = "ifdef"; break;
            case kVBCppCondKind_IfNDef: pszDirective = "ifndef"; break;
            case kVBCppCondKind_ElIf:   pszDirective = "elif"; break;
            default: AssertFailedReturn(RTEXITCODE_FAILURE);
        }
        ssize_t cch = ScmStreamPrintf(&pThis->StrmOutput, "#%*s%s %.*s",
                                      pCond->iKeepLevel - 1, "", pszDirective, cchCondition, pchCondition);
        if (cch < 0)
            return vbcppError(pThis, "Output error %Rrc", (int)cch);
    }
    else
        pThis->fJustDroppedLine = true;

    return RTEXITCODE_SUCCESS;
}


/**
 * Recursively destroys the expression tree.
 *
 * @param   pExpr               The root of the expression tree to destroy.
 */
static void vbcppExprDestoryTree(PVBCPPEXPR pExpr)
{
    if (!pExpr)
        return;

    switch (pExpr->enmKind)
    {
        case kVBCppExprKind_Unary:
            vbcppExprDestoryTree(pExpr->u.Unary.pArg);
            break;
        case kVBCppExprKind_Binary:
            vbcppExprDestoryTree(pExpr->u.Binary.pLeft);
            vbcppExprDestoryTree(pExpr->u.Binary.pRight);
            break;
        case kVBCppExprKind_Ternary:
            vbcppExprDestoryTree(pExpr->u.Ternary.pExpr);
            vbcppExprDestoryTree(pExpr->u.Ternary.pTrue);
            vbcppExprDestoryTree(pExpr->u.Ternary.pFalse);
            break;
        case kVBCppExprKind_SignedValue:
        case kVBCppExprKind_UnsignedValue:
            break;
        case kVBCppExprKind_UndefMacroCall:
            pExpr->u.UndefMacroCall.pszName = NULL;
            for (size_t i = 0; i < pExpr->u.UndefMacroCall.cArgs; i++)
                vbcppExprDestoryTree(pExpr->u.UndefMacroCall.papArgs[i]);
            RTMemFree(pExpr->u.UndefMacroCall.papArgs);
            RTMemFree(pExpr->u.UndefMacroCall.pszName);
            break;
        default:
            AssertFailed();
            return;
    }
    RTMemFree(pExpr);
}


/**
 * Report error during expression parsing.
 *
 * @returns kExprRet_Error
 * @param   pParser             The parser instance.
 * @param   pszMsg              The error message.
 * @param   ...                 Format arguments.
 */
static VBCPPEXPRRET vbcppExprParseError(PVBCPPEXPRPARSER pParser, const char *pszMsg, ...)
{
    va_list va;
    va_start(va, pszMsg);
    vbcppErrorV(pParser->pThis, pszMsg, va);
    va_end(va);
    return kExprRet_Error;
}


/**
 * Skip white space.
 *
 * @param   pParser             The parser instance.
 */
static void vbcppExprParseSkipWhiteSpace(PVBCPPEXPRPARSER pParser)
{
    while (RT_C_IS_SPACE(*pParser->pszCur))
        pParser->pszCur++;
}


/**
 * Allocate a new
 *
 * @returns Pointer to the node. NULL+msg on failure.
 * @param   pParser             The parser instance.
 */
static PVBCPPEXPR vbcppExprParseAllocNode(PVBCPPEXPRPARSER pParser)
{
    PVBCPPEXPR pExpr = (PVBCPPEXPR)RTMemAllocZ(sizeof(*pExpr));
    if (!pExpr)
        vbcppExprParseError(pParser, "out of memory (expression node)");
    return pExpr;
}


/**
 * Checks if we're currently in an call to an undefined macro.
 *
 * This will walk up the expression chain and check for a call node while also
 * taking parenthesis nodes into account.
 *
 * @returns true if we are, false if we aren't.
 * @param   pParser             The parser instance.
 */
static bool vbcppExprParseIsInUndefCall(PVBCPPEXPRPARSER pParser)
{
    PVBCPPEXPR pExpr = pParser->pCur;
    while (pExpr)
    {
        if (pExpr->enmKind == kVBCppExprKind_UndefMacroCall)
            return true;
        if (   pExpr->enmKind == kVBCppExprKind_Unary
            && pExpr->u.Unary.enmOperator == kVBCppUnaryOp_Parenthesis)
               break;
        pExpr = pExpr->pParent;
    }
    return false;
}


/**
 * Looks for right parentheses, comma and/or end of expression.
 *
 * (The comma is only if we're in the context of a undefined macro call.)
 *
 * @returns Expression status.
 * @retval  kExprRet_Ok
 * @retval  kExprRet_Error with msg.
 * @retval  kExprRet_EndOfExpr
 * @param   pParser             The parser instance.
 */
static VBCPPEXPRRET vbcppExprParseMaybeRParenOrEoe(PVBCPPEXPRPARSER pParser)
{
    Assert(!pParser->ppCur);
    for (;;)
    {
        vbcppExprParseSkipWhiteSpace(pParser);
        char const ch = *pParser->pszCur;
        if (ch == '\0')
        {
            /* Unwind making sure we don't have any incomplete parentheses or
               incomplete call expressions on the stack. */
            PVBCPPEXPR pCur = pParser->pCur;
            Assert(pCur);
            for (;;)
            {
                if (!pCur->fComplete)
                {
                    if (   pCur->enmKind == kVBCppExprKind_Unary
                        && pCur->u.Unary.enmOperator == kVBCppUnaryOp_Parenthesis)
                        return vbcppExprParseError(pParser, "Missing right parenthesis");
                    if (pCur->enmKind == kVBCppExprKind_UndefMacroCall)
                        return vbcppExprParseError(pParser, "Missing right parenthesis for undefined macro call");
                }
                if (pCur->pParent)
                    pCur = pCur->pParent;
                else
                {
                    pParser->pCur  = pCur;
                    pParser->ppCur = NULL;
                    return kExprRet_EndOfExpr;
                }
            }
        }

        if (ch != ')' && (ch != ',' || !vbcppExprParseIsInUndefCall(pParser))) /** @todo just immediate?  */
            break;
        pParser->pszCur++;

        PVBCPPEXPR pCur = pParser->pCur;
        while (   pCur
               && (   pCur->enmKind != kVBCppExprKind_UndefMacroCall
                   || pCur->fComplete /*?*/)
               && (   pCur->enmKind != kVBCppExprKind_Unary
                   || pCur->u.Unary.enmOperator != kVBCppUnaryOp_Parenthesis
                   || pCur->fComplete))
        {
            switch (pCur->enmKind)
            {
                case kVBCppExprKind_SignedValue:
                case kVBCppExprKind_UnsignedValue:
                    Assert(pCur->fComplete);
                    break;
                case kVBCppExprKind_Unary:
                    AssertReturn(pCur->u.Unary.pArg, vbcppExprParseError(pParser, "internal error"));
                    pCur->fComplete = true;
                    break;
                case kVBCppExprKind_Binary:
                    AssertReturn(pCur->u.Binary.pLeft, vbcppExprParseError(pParser, "internal error"));
                    AssertReturn(pCur->u.Binary.pRight, vbcppExprParseError(pParser, "internal error"));
                    pCur->fComplete = true;
                    break;
                case kVBCppExprKind_Ternary:
#if 1 /** @todo Check out the ternary operator implementation. */
                    return vbcppExprParseError(pParser, "The ternary operator is not implemented");
#else
                    Assert(pCur->u.Ternary.pExpr);
                    if (!pCur->u.Ternary.pTrue)
                        return vbcppExprParseError(pParser, "?!?!?");
                    if (!pCur->u.Ternary.pFalse)
                        return vbcppExprParseError(pParser, "?!?!?!?");
                    pCur->fComplete = true;
#endif
                    break;
                case kVBCppExprKind_UndefMacroCall:
                    break;
                default:
                    return vbcppExprParseError(pParser, "Internal error (enmKind=%d)", pCur->enmKind);
            }
            pCur = pCur->pParent;
        }
        if (!pCur)
            return vbcppExprParseError(pParser, "Right parenthesis without a left one");

        /*
         * If we got down to an undefined macro call, we make it the current
         * parser expression and return EndOfExpr.  The call expression is
         * completed if we hit a ')'.
         */
        if (pCur->enmKind == kVBCppExprKind_UndefMacroCall)
        {
            pCur->fComplete = ch == ')';
            pParser->pCur  = pCur;
            pParser->ppCur = NULL;
            return kExprRet_EndOfExpr;
        }

        /*
         * Complete the parenthesis expression and make it current.
         */
        Assert(   pCur->enmKind == kVBCppExprKind_Unary
               && pCur->u.Unary.enmOperator == kVBCppUnaryOp_Parenthesis);
        AssertReturn(pCur->u.Unary.pArg, vbcppExprParseError(pParser, "internal error"));
        pCur->fComplete = true;
        pParser->pCur  = pCur;
        pParser->ppCur = NULL;
    }

    return kExprRet_Ok;
}


/**
 * Parses an binary operator.
 *
 * @returns Expression status.
 * @retval  kExprRet_Ok
 * @retval  kExprRet_Error with msg.
 * @param   pParser             The parser instance.
 */
static VBCPPEXPRRET vbcppExprParseBinaryOperator(PVBCPPEXPRPARSER pParser)
{
    /*
     * Binary or ternary operator should follow now.
     */
    VBCPPBINARYOP enmOp;
    char ch = *pParser->pszCur;
    switch (ch)
    {
        case '*':
            if (pParser->pszCur[1] == '=')
                return vbcppExprParseError(pParser, "The assignment by product operator is not valid in a preprocessor expression");
            enmOp = kVBCppBinary_Multiplication;
            break;
        case '/':
            if (pParser->pszCur[1] == '=')
                return vbcppExprParseError(pParser, "The assignment by quotient operator is not valid in a preprocessor expression");
            enmOp = kVBCppBinary_Division;
            break;
        case '%':
            if (pParser->pszCur[1] == '=')
                return vbcppExprParseError(pParser, "The assignment by remainder operator is not valid in a preprocessor expression");
            enmOp = kVBCppBinary_Modulo;
            break;
        case '+':
            if (pParser->pszCur[1] == '=')
                return vbcppExprParseError(pParser, "The assignment by sum operator is not valid in a preprocessor expression");
            enmOp = kVBCppBinary_Addition;
            break;
        case '-':
            if (pParser->pszCur[1] == '=')
                return vbcppExprParseError(pParser, "The assignment by difference operator is not valid in a preprocessor expression");
            enmOp = kVBCppBinary_Subtraction;
            break;
        case '<':
            enmOp = kVBCppBinary_LessThan;
            if (pParser->pszCur[1] == '=')
            {
                pParser->pszCur++;
                enmOp = kVBCppBinary_LessThanOrEqual;
            }
            else if (pParser->pszCur[1] == '<')
            {
                pParser->pszCur++;
                if (pParser->pszCur[1] == '=')
                    return vbcppExprParseError(pParser, "The assignment by bitwise left shift operator is not valid in a preprocessor expression");
                enmOp = kVBCppBinary_LeftShift;
            }
            break;
        case '>':
            enmOp = kVBCppBinary_GreaterThan;
            if (pParser->pszCur[1] == '=')
            {
                pParser->pszCur++;
                enmOp = kVBCppBinary_GreaterThanOrEqual;
            }
            else if (pParser->pszCur[1] == '<')
            {
                pParser->pszCur++;
                if (pParser->pszCur[1] == '=')
                    return vbcppExprParseError(pParser, "The assignment by bitwise right shift operator is not valid in a preprocessor expression");
                enmOp = kVBCppBinary_LeftShift;
            }
            break;
        case '=':
            if (pParser->pszCur[1] != '=')
                return vbcppExprParseError(pParser, "The assignment operator is not valid in a preprocessor expression");
            pParser->pszCur++;
            enmOp = kVBCppBinary_EqualTo;
            break;

        case '!':
            if (pParser->pszCur[1] != '=')
                return vbcppExprParseError(pParser, "Expected binary operator, found the unary operator logical NOT");
            pParser->pszCur++;
            enmOp = kVBCppBinary_NotEqualTo;
            break;

        case '&':
            if (pParser->pszCur[1] == '=')
                return vbcppExprParseError(pParser, "The assignment by bitwise AND operator is not valid in a preprocessor expression");
            if (pParser->pszCur[1] == '&')
            {
                pParser->pszCur++;
                enmOp = kVBCppBinary_LogicalAnd;
            }
            else
                enmOp = kVBCppBinary_BitwiseAnd;
            break;
        case '^':
            if (pParser->pszCur[1] == '=')
                return vbcppExprParseError(pParser, "The assignment by bitwise XOR operator is not valid in a preprocessor expression");
            enmOp = kVBCppBinary_BitwiseXor;
            break;
        case '|':
            if (pParser->pszCur[1] == '=')
                return vbcppExprParseError(pParser, "The assignment by bitwise AND operator is not valid in a preprocessor expression");
            if (pParser->pszCur[1] == '|')
            {
                pParser->pszCur++;
                enmOp = kVBCppBinary_LogicalOr;
            }
            else
                enmOp = kVBCppBinary_BitwiseOr;
            break;
        case '~':
            return vbcppExprParseError(pParser, "Expected binary operator, found the unary operator bitwise NOT");

        case ':':
        case '?':
            return vbcppExprParseError(pParser, "The ternary operator is not yet implemented");

        default:
            return vbcppExprParseError(pParser, "Expected binary operator, found '%.20s'", pParser->pszCur);
    }
    pParser->pszCur++;

    /*
     * Create a binary operator node.
     */
    PVBCPPEXPR pExpr = vbcppExprParseAllocNode(pParser);
    if (!pExpr)
        return kExprRet_Error;
    pExpr->fComplete            = true;
    pExpr->enmKind              = kVBCppExprKind_Binary;
    pExpr->u.Binary.enmOperator = enmOp;
    pExpr->u.Binary.pLeft       = NULL;
    pExpr->u.Binary.pRight      = NULL;

    /*
     * Back up the tree until we find our spot.
     */
    PVBCPPEXPR *ppPlace = NULL;
    PVBCPPEXPR  pChild  = pParser->pCur;
    PVBCPPEXPR  pParent = pChild->pParent;
    while (pParent)
    {
        if (pParent->enmKind == kVBCppExprKind_Unary)
        {
            if (pParent->u.Unary.enmOperator == kVBCppUnaryOp_Parenthesis)
            {
                ppPlace = &pParent->u.Unary.pArg;
                break;
            }
            AssertReturn(pParent->u.Unary.pArg, vbcppExprParseError(pParser, "internal error"));
            pParent->fComplete = true;
        }
        else if (pParent->enmKind == kVBCppExprKind_Binary)
        {
            AssertReturn(pParent->u.Binary.pLeft, vbcppExprParseError(pParser, "internal error"));
            AssertReturn(pParent->u.Binary.pRight, vbcppExprParseError(pParser, "internal error"));
            if ((pParent->u.Binary.enmOperator & VBCPPOP_PRECEDENCE_MASK) >= (enmOp & VBCPPOP_PRECEDENCE_MASK))
            {
                AssertReturn(pChild, vbcppExprParseError(pParser, "internal error"));

                if (pParent->u.Binary.pRight == pChild)
                    ppPlace = &pParent->u.Binary.pRight;
                else
                    ppPlace = &pParent->u.Binary.pLeft;
                AssertReturn(*ppPlace == pChild, vbcppExprParseError(pParser, "internal error"));
                break;
            }
            pParent->fComplete = true;
        }
        else if (pParent->enmKind == kVBCppExprKind_Ternary)
        {
            return vbcppExprParseError(pParser, "The ternary operator is not implemented");
        }
        else if (pParent->enmKind == kVBCppExprKind_UndefMacroCall)
        {
            ppPlace = &pParent->u.UndefMacroCall.papArgs[pParent->u.UndefMacroCall.cArgs];
            break;
        }
        else
            AssertReturn(   pParent->enmKind == kVBCppExprKind_SignedValue
                         || pParent->enmKind == kVBCppExprKind_UnsignedValue,
                         vbcppExprParseError(pParser, "internal error"));

        /* Up on level */
        pChild  = pParent;
        pParent = pParent->pParent;
    }

    /*
     * Do the rotation.
     */
    Assert(pChild);
    Assert(pChild->pParent == pParent);
    pChild->pParent       = pExpr;

    pExpr->u.Binary.pLeft = pChild;
    pExpr->pParent        = pParent;

    if (!pParent)
        pParser->pRoot    = pExpr;
    else
        *ppPlace          = pExpr;

    pParser->ppCur = &pExpr->u.Binary.pRight;
    pParser->pCur  = pExpr;

    return kExprRet_Ok;
}


/**
 * Deals with right paretheses or/and end of expression, looks for binary
 * operators.
 *
 * @returns Expression status.
 * @retval  kExprRet_Ok if binary operator was found processed.
 * @retval  kExprRet_Error with msg.
 * @retval  kExprRet_EndOfExpr
 * @param   pParser             The parser instance.
 */
static VBCPPEXPRRET vbcppExprParseBinaryOrEoeOrRparen(PVBCPPEXPRPARSER pParser)
{
    VBCPPEXPRRET enmRet = vbcppExprParseMaybeRParenOrEoe(pParser);
    if (enmRet != kExprRet_Ok)
        return enmRet;
    return vbcppExprParseBinaryOperator(pParser);
}


/**
 * Worker for vbcppExprParseIdentifier that parses a call to an undefined macro
 * in selective mode.
 *
 * @returns Expression status.
 * @retval  kExprRet_Ok if binary operator was found processed.
 * @retval  kExprRet_Error with msg.
 * @retval  kExprRet_EndOfExpr
 * @param   pParser             The parser instance.
 * @param   pszMacro            The start of the macro name.
 * @param   cchMacro            The length of the macro name.
 */
static VBCPPEXPRRET vbcppExprParseUndefMacroCall(PVBCPPEXPRPARSER pParser, const char *pszMacro, size_t cchMacro)
{
    /*
     * Treat it as a call to an undefined macro function.
     */
    /* Create a node. */
    PVBCPPEXPR const pExpr = vbcppExprParseAllocNode(pParser);
    if (!pExpr)
        return kExprRet_Error;
    pExpr->enmKind                  = kVBCppExprKind_UndefMacroCall;
    pExpr->fComplete                = false;
    pExpr->u.UndefMacroCall.cArgs   = 0;
    pExpr->u.UndefMacroCall.papArgs = NULL;
    pExpr->u.UndefMacroCall.pszName = RTStrDupN(pszMacro, cchMacro);
    AssertReturn(pExpr->u.UndefMacroCall.pszName, vbcppExprParseError(pParser, "out of memory"));

    /* Link it. */
    pExpr->pParent  = pParser->pCur;
    pParser->pCur   = pExpr;
    *pParser->ppCur = pExpr;
    pParser->ppCur  = NULL;

    /*
     * Parse the argument list.
     */
    pParser->pszCur++;
    for (size_t iArg = 0 ; ; iArg++)
    {
        /*
         * Prepare the next argument expression pointer.
         */
        if (!(iArg % 16))
        {
            void *pvNew = RTMemRealloc(pExpr->u.UndefMacroCall.papArgs,
                                       sizeof(pExpr->u.UndefMacroCall.papArgs[0]) * (iArg + 16));
            AssertPtrReturn(pvNew, vbcppExprParseError(pParser, "out of memory"));
            pExpr->u.UndefMacroCall.papArgs = (PVBCPPEXPR *)pvNew;
        }
        pExpr->u.UndefMacroCall.papArgs[iArg] = NULL;
        pParser->ppCur = &pExpr->u.UndefMacroCall.papArgs[iArg];

        /*
         * Do the parsing.
         */
        for (;;)
        {
            /*
             * Eat unary operators until we hit a value or end of argument/call.
             */
            VBCPPEXPRRET enmRet;
            do
                enmRet = vbcppExprParseUnaryOrValue(pParser);
            while (enmRet == kExprRet_UnaryOperator);
            if (enmRet == kExprRet_EndOfExpr)
                break;
            if (enmRet == kExprRet_Error)
                return enmRet;
            AssertReturn(enmRet == kExprRet_Value, vbcppExprParseError(pParser, "Expected value (enmRet=%d)", enmRet));

            /*
             * Non-unary operator, right parenthesis or end of argument/call is up next.
             */
            enmRet = vbcppExprParseBinaryOrEoeOrRparen(pParser);
            if (enmRet == kExprRet_EndOfExpr)
                break;
            if (enmRet == kExprRet_Error)
                return enmRet;
            AssertReturn(enmRet == kExprRet_Ok, vbcppExprParseError(pParser, "Expected value (enmRet=%d)", enmRet));
        }

        /*
         * Append the argument and skip past the comma or right parenthesis.
         */
        if (pExpr->u.UndefMacroCall.papArgs[iArg] != NULL || !pExpr->fComplete)
            pExpr->u.UndefMacroCall.cArgs = iArg + 1;

        Assert(pParser->pCur == pExpr);
        if (pExpr->fComplete)
            break;
    }

    pParser->ppCur = NULL;
    return kExprRet_Value;
}


/**
 * Parses an identifier in the expression, replacing it by 0.
 *
 * All known identifiers has already been replaced by their macro values, so
 * what's left are unknown macros.  These are replaced by 0.
 *
 * @returns Expression status.
 * @retval  kExprRet_Value
 * @retval  kExprRet_Error with msg.
 * @param   pParser             The parser instance.
 */
static VBCPPEXPRRET vbcppExprParseIdentifier(PVBCPPEXPRPARSER pParser)
{
/** @todo don't increment if it's an actively undefined macro. Need to revise
 *        the expression related code wrt selective preprocessing. */
    pParser->cUndefined++;

    /* Find the end. */
    const char *pszMacro = pParser->pszCur;
    const char *pszNext  = pszMacro + 1;
    while (vbcppIsCIdentifierChar(*pszNext))
        pszNext++;
    size_t const cchMacro = pszNext - pszMacro;

    /* Skip spaces and check for parenthesis. */
    pParser->pszCur = pszNext;
    vbcppExprParseSkipWhiteSpace(pParser);
    if (*pParser->pszCur == '(')
    {
        if (pParser->pThis->enmMode == kVBCppMode_Selective)
            return vbcppExprParseUndefMacroCall(pParser, pszMacro, cchMacro);
        return vbcppExprParseError(pParser, "%sUnknown unary operator '%.*s'",
                                   pParser->pThis->enmMode != kVBCppMode_Standard ? "TODO selective: " : "",
                                   cchMacro, pszMacro);
    }

    /* Create a signed value node. */
    PVBCPPEXPR pExpr = vbcppExprParseAllocNode(pParser);
    if (!pExpr)
        return kExprRet_Error;
    pExpr->fComplete            = true;
    pExpr->enmKind              = kVBCppExprKind_UnsignedValue;
    pExpr->u.UnsignedValue.u64  = 0;

    /* Link it. */
    pExpr->pParent              = pParser->pCur;
    pParser->pCur               = pExpr;
    *pParser->ppCur             = pExpr;
    pParser->ppCur              = NULL;

    return kExprRet_Value;
}


/**
 * Parses an numeric constant in the expression.
 *
 * @returns Expression status.
 * @retval  kExprRet_Value
 * @retval  kExprRet_Error with msg.
 * @param   pParser             The parser instance.
 */
static VBCPPEXPRRET vbcppExprParseNumber(PVBCPPEXPRPARSER pParser)
{
    bool        fSigned;
    char       *pszNext;
    uint64_t    u64;
    char        ch    = *pParser->pszCur++;
    char        ch2   = *pParser->pszCur;
    if (   ch == '0'
        && (ch2 == 'x' || ch2 == 'X'))
    {
        ch2 = *++pParser->pszCur;
        if (!RT_C_IS_XDIGIT(ch2))
            return vbcppExprParseError(pParser, "Expected hex digit following '0x'");
        int rc = RTStrToUInt64Ex(pParser->pszCur, &pszNext, 16, &u64);
        if (   RT_FAILURE(rc)
            || rc == VWRN_NUMBER_TOO_BIG)
            return vbcppExprParseError(pParser, "Invalid hex value '%.20s...' (%Rrc)", pParser->pszCur, rc);
        fSigned = false;
    }
    else if (ch == '0')
    {
        int rc = RTStrToUInt64Ex(pParser->pszCur - 1, &pszNext, 8, &u64);
        if (   RT_FAILURE(rc)
            || rc == VWRN_NUMBER_TOO_BIG)
            return vbcppExprParseError(pParser, "Invalid octal value '%.20s...' (%Rrc)", pParser->pszCur, rc);
        fSigned = u64 > (uint64_t)INT64_MAX ? false : true;
    }
    else
    {
        int rc = RTStrToUInt64Ex(pParser->pszCur - 1, &pszNext, 10, &u64);
        if (   RT_FAILURE(rc)
            || rc == VWRN_NUMBER_TOO_BIG)
            return vbcppExprParseError(pParser, "Invalid decimal value '%.20s...' (%Rrc)", pParser->pszCur, rc);
        fSigned = u64 > (uint64_t)INT64_MAX ? false : true;
    }

    /* suffix. */
    if (vbcppIsCIdentifierLeadChar(*pszNext))
    {
        size_t cchSuffix = 1;
        while (vbcppIsCIdentifierLeadChar(pszNext[cchSuffix]))
            cchSuffix++;

        if (cchSuffix == 1 && (*pszNext == 'u' || *pszNext == 'U'))
            fSigned = false;
        else if (   cchSuffix == 1
                 && (*pszNext == 'l' || *pszNext == 'L'))
            fSigned = true;
        else if (   cchSuffix == 2
                 && (!strncmp(pszNext, "ul", 2) || !strncmp(pszNext, "UL", 2)))
            fSigned = false;
        else if (   cchSuffix == 2
                 && (!strncmp(pszNext, "ll", 2) || !strncmp(pszNext, "LL", 2)))
            fSigned = true;
        else if (   cchSuffix == 3
                 && (!strncmp(pszNext, "ull", 3) || !strncmp(pszNext, "ULL", 3)))
            fSigned = false;
        else
            return vbcppExprParseError(pParser, "Invalid number suffix '%.*s'", cchSuffix, pszNext);

        pszNext += cchSuffix;
    }
    pParser->pszCur = pszNext;

    /* Create a signed value node. */
    PVBCPPEXPR pExpr = vbcppExprParseAllocNode(pParser);
    if (!pExpr)
        return kExprRet_Error;
    pExpr->fComplete            = true;
    if (fSigned)
    {
        pExpr->enmKind              = kVBCppExprKind_SignedValue;
        pExpr->u.SignedValue.s64    = (int64_t)u64;
    }
    else
    {
        pExpr->enmKind              = kVBCppExprKind_UnsignedValue;
        pExpr->u.UnsignedValue.u64  = u64;
    }

    /* Link it. */
    pExpr->pParent              = pParser->pCur;
    pParser->pCur               = pExpr;
    *pParser->ppCur             = pExpr;
    pParser->ppCur              = NULL;

    return kExprRet_Value;
}


/**
 * Parses an character constant in the expression.
 *
 * @returns Expression status.
 * @retval  kExprRet_Value
 * @retval  kExprRet_Error with msg.
 * @param   pParser             The parser instance.
 */
static VBCPPEXPRRET vbcppExprParseCharacterConstant(PVBCPPEXPRPARSER pParser)
{
    Assert(*pParser->pszCur == '\'');
    pParser->pszCur++;
    char ch2 = *pParser->pszCur++;
    if (ch2 == '\'')
        return vbcppExprParseError(pParser, "Empty character constant");
    int64_t s64;
    if (ch2 == '\\')
    {
        ch2 = *pParser->pszCur++;
        switch (ch2)
        {
            case '0': s64 = 0x00; break;
            case 'n': s64 = 0x0d; break;
            case 'r': s64 = 0x0a; break;
            case 't': s64 = 0x09; break;
            default:
                return vbcppExprParseError(pParser, "Escape character '%c' is not implemented", ch2);
        }
    }
    else
        s64 = ch2;
    if (*pParser->pszCur != '\'')
        return vbcppExprParseError(pParser, "Character constant contains more than one character");

    /* Create a signed value node. */
    PVBCPPEXPR pExpr = vbcppExprParseAllocNode(pParser);
    if (!pExpr)
        return kExprRet_Error;
    pExpr->fComplete            = true;
    pExpr->enmKind              = kVBCppExprKind_SignedValue;
    pExpr->u.SignedValue.s64    = s64;

    /* Link it. */
    pExpr->pParent              = pParser->pCur;
    pParser->pCur               = pExpr;
    *pParser->ppCur             = pExpr;
    pParser->ppCur              = NULL;

    return kExprRet_Value;
}


/**
 * Parses a unary operator or a value.
 *
 * @returns Expression status.
 * @retval  kExprRet_Value if value was found and processed.
 * @retval  kExprRet_UnaryOperator if an unary operator was found and processed.
 * @retval  kExprRet_Error with msg.
 * @retval  kExprRet_EndOfExpr if reached ',' or ')' if in an undefined call.
 * @param   pParser             The parser instance.
 */
static VBCPPEXPRRET vbcppExprParseUnaryOrValue(PVBCPPEXPRPARSER pParser)
{
    vbcppExprParseSkipWhiteSpace(pParser);
    char ch = *pParser->pszCur;
    if (ch == '\0')
        return vbcppExprParseError(pParser, "Premature end of expression");

    /*
     * Value?
     */
    if (ch == '\'')
        return vbcppExprParseCharacterConstant(pParser);
    if (RT_C_IS_DIGIT(ch))
        return vbcppExprParseNumber(pParser);
    if (ch == '"')
        return vbcppExprParseError(pParser, "String litteral");
    if (vbcppIsCIdentifierLeadChar(ch))
        return vbcppExprParseIdentifier(pParser);

    /*
     * Operator?
     */
    VBCPPUNARYOP enmOperator;
    if (ch == '+')
    {
        enmOperator = kVBCppUnaryOp_Pluss;
        if (pParser->pszCur[1] == '+')
            return vbcppExprParseError(pParser, "The prefix increment operator is not valid in a preprocessor expression");
    }
    else if (ch == '-')
    {
        enmOperator = kVBCppUnaryOp_Minus;
        if (pParser->pszCur[1] == '-')
            return vbcppExprParseError(pParser, "The prefix decrement operator is not valid in a preprocessor expression");
    }
    else if (ch == '!')
        enmOperator = kVBCppUnaryOp_LogicalNot;
    else if (ch == '~')
        enmOperator = kVBCppUnaryOp_BitwiseNot;
    else if (ch == '(')
        enmOperator = kVBCppUnaryOp_Parenthesis;
    else if ((ch == ',' || ch == ')') && pParser->pCur->enmKind == kVBCppExprKind_UndefMacroCall)
    {
        pParser->pszCur++;
        pParser->pCur->fComplete = ch == ')';
        return kExprRet_EndOfExpr;
    }
    else
        return vbcppExprParseError(pParser, "Unexpected token '%.*s'", 32, pParser->pszCur - 1);
    pParser->pszCur++;

    /* Create an operator node. */
    PVBCPPEXPR pExpr = vbcppExprParseAllocNode(pParser);
    if (!pExpr)
        return kExprRet_Error;
    pExpr->fComplete            = false;
    pExpr->enmKind              = kVBCppExprKind_Unary;
    pExpr->u.Unary.enmOperator  = enmOperator;
    pExpr->u.Unary.pArg         = NULL;

    /* Link it into the tree. */
    pExpr->pParent              = pParser->pCur;
    pParser->pCur               = pExpr;
    *pParser->ppCur             = pExpr;
    pParser->ppCur              = &pExpr->u.Unary.pArg;

    return kExprRet_UnaryOperator;
}


/**
 * Parses an expanded preprocessor expression.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pszExpr             The expression to parse.
 * @param   cchExpr             The length of the expression in case we need it.
 * @param   ppExprTree          Where to return the parse tree.
 * @param   pcUndefined         Where to return the number of unknown undefined
 *                              macros.  Optional.
 */
static RTEXITCODE vbcppExprParse(PVBCPP pThis, char *pszExpr, size_t cchExpr, PVBCPPEXPR *ppExprTree, size_t *pcUndefined)
{
    RTEXITCODE rcExit = RTEXITCODE_FAILURE;
    NOREF(cchExpr);

    /*
     * Initialize the parser context structure.
     */
    VBCPPEXPRPARSER Parser;
    Parser.pszCur       = pszExpr;
    Parser.pRoot        = NULL;
    Parser.pCur         = NULL;
    Parser.ppCur        = &Parser.pRoot;
    Parser.pszExpr      = pszExpr;
    Parser.cUndefined   = 0;
    Parser.pThis        = pThis;

    /*
     * Do the parsing.
     */
    VBCPPEXPRRET enmRet;
    for (;;)
    {
        /*
         * Eat unary operators until we hit a value.
         */
        do
            enmRet = vbcppExprParseUnaryOrValue(&Parser);
        while (enmRet == kExprRet_UnaryOperator);
        if (enmRet == kExprRet_Error)
            break;
        AssertBreakStmt(enmRet == kExprRet_Value, enmRet = vbcppExprParseError(&Parser, "Expected value (enmRet=%d)", enmRet));

        /*
         * Non-unary operator, right parenthesis or end of expression is up next.
         */
        enmRet = vbcppExprParseBinaryOrEoeOrRparen(&Parser);
        if (enmRet == kExprRet_Error)
            break;
        if (enmRet == kExprRet_EndOfExpr)
        {
            /** @todo check if there are any open parentheses. */
            rcExit = RTEXITCODE_SUCCESS;
            break;
        }
        AssertBreakStmt(enmRet == kExprRet_Ok, enmRet = vbcppExprParseError(&Parser, "Expected value (enmRet=%d)", enmRet));
    }

    if (rcExit != RTEXITCODE_SUCCESS)
    {
        vbcppExprDestoryTree(Parser.pRoot);
        return rcExit;
    }

    if (pcUndefined)
        *pcUndefined = Parser.cUndefined;
    *ppExprTree = Parser.pRoot;
    return rcExit;
}


/**
 * Checks if an expression value value is evaluates to @c true or @c false.
 *
 * @returns @c true or @c false.
 * @param   pExpr               The value expression.
 */
static bool vbcppExprIsExprTrue(PVBCPPEXPR pExpr)
{
    Assert(pExpr->enmKind == kVBCppExprKind_SignedValue || pExpr->enmKind == kVBCppExprKind_UnsignedValue);

    return pExpr->enmKind == kVBCppExprKind_SignedValue
         ? pExpr->u.SignedValue.s64   != 0
         : pExpr->u.UnsignedValue.u64 != 0;
}


/**
 * Evalutes a parse (sub-)tree.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pRoot               The root of the parse (sub-)tree.
 * @param   pResult             Where to store the result value.
 */
static RTEXITCODE vbcppExprEvaluteTree(PVBCPP pThis, PVBCPPEXPR pRoot, PVBCPPEXPR pResult)
{
    RTEXITCODE rcExit;
    switch (pRoot->enmKind)
    {
        case kVBCppExprKind_SignedValue:
            pResult->enmKind                = kVBCppExprKind_SignedValue;
            pResult->u.SignedValue.s64      = pRoot->u.SignedValue.s64;
            return RTEXITCODE_SUCCESS;

        case kVBCppExprKind_UnsignedValue:
            pResult->enmKind                = kVBCppExprKind_UnsignedValue;
            pResult->u.UnsignedValue.u64    = pRoot->u.UnsignedValue.u64;
            return RTEXITCODE_SUCCESS;

        case kVBCppExprKind_Unary:
            rcExit = vbcppExprEvaluteTree(pThis, pRoot->u.Unary.pArg, pResult);
            if (rcExit != RTEXITCODE_SUCCESS)
                return rcExit;

            /* Apply the unary operator to the value */
            switch (pRoot->u.Unary.enmOperator)
            {
                case kVBCppUnaryOp_Minus:
                    if (pResult->enmKind == kVBCppExprKind_SignedValue)
                        pResult->u.SignedValue.s64   = -pResult->u.SignedValue.s64;
                    else
                        pResult->u.UnsignedValue.u64 = (uint64_t)-(int64_t)pResult->u.UnsignedValue.u64;
                    break;

                case kVBCppUnaryOp_LogicalNot:
                    if (pResult->enmKind == kVBCppExprKind_SignedValue)
                        pResult->u.SignedValue.s64   = !pResult->u.SignedValue.s64;
                    else
                        pResult->u.UnsignedValue.u64 = !pResult->u.UnsignedValue.u64;
                    break;

                case kVBCppUnaryOp_BitwiseNot:
                    if (pResult->enmKind == kVBCppExprKind_SignedValue)
                        pResult->u.SignedValue.s64   = ~pResult->u.SignedValue.s64;
                    else
                        pResult->u.UnsignedValue.u64 = ~pResult->u.UnsignedValue.u64;
                    break;

                case kVBCppUnaryOp_Pluss:
                case kVBCppUnaryOp_Parenthesis:
                    /* do nothing. */
                    break;

                default:
                    return vbcppError(pThis, "Internal error: u.Unary.enmOperator=%d", pRoot->u.Unary.enmOperator);
            }
            return RTEXITCODE_SUCCESS;

        case kVBCppExprKind_Binary:
        {
            /* Always evalute the left side. */
            rcExit = vbcppExprEvaluteTree(pThis, pRoot->u.Binary.pLeft, pResult);
            if (rcExit != RTEXITCODE_SUCCESS)
                return rcExit;

            /* If logical AND or OR we can sometimes skip evaluting the right side. */
            if (   pRoot->u.Binary.enmOperator == kVBCppBinary_LogicalAnd
                && !vbcppExprIsExprTrue(pResult))
                return RTEXITCODE_SUCCESS;

            if (   pRoot->u.Binary.enmOperator == kVBCppBinary_LogicalOr
                && vbcppExprIsExprTrue(pResult))
                return RTEXITCODE_SUCCESS;

            /* Evalute the right side. */
            VBCPPEXPR Result2;
            rcExit = vbcppExprEvaluteTree(pThis, pRoot->u.Binary.pRight, &Result2);
            if (rcExit != RTEXITCODE_SUCCESS)
                return rcExit;

            /* If one of them is unsigned, promote the other to unsigned as well. */
            if (   pResult->enmKind == kVBCppExprKind_UnsignedValue
                && Result2.enmKind  == kVBCppExprKind_SignedValue)
            {
                Result2.enmKind              = kVBCppExprKind_UnsignedValue;
                Result2.u.UnsignedValue.u64  = Result2.u.SignedValue.s64;
            }
            else if (   pResult->enmKind == kVBCppExprKind_SignedValue
                     && Result2.enmKind  == kVBCppExprKind_UnsignedValue)
            {
                pResult->enmKind             = kVBCppExprKind_UnsignedValue;
                pResult->u.UnsignedValue.u64 = pResult->u.SignedValue.s64;
            }

            /* Perform the operation. */
            if (pResult->enmKind == kVBCppExprKind_UnsignedValue)
            {
                switch (pRoot->u.Binary.enmOperator)
                {
                    case kVBCppBinary_Multiplication:
                        pResult->u.UnsignedValue.u64 *= Result2.u.UnsignedValue.u64;
                        break;
                    case kVBCppBinary_Division:
                        if (!Result2.u.UnsignedValue.u64)
                            return vbcppError(pThis, "Divide by zero");
                        pResult->u.UnsignedValue.u64 /= Result2.u.UnsignedValue.u64;
                        break;
                    case kVBCppBinary_Modulo:
                        if (!Result2.u.UnsignedValue.u64)
                            return vbcppError(pThis, "Divide by zero");
                        pResult->u.UnsignedValue.u64 %= Result2.u.UnsignedValue.u64;
                        break;
                    case kVBCppBinary_Addition:
                        pResult->u.UnsignedValue.u64 += Result2.u.UnsignedValue.u64;
                        break;
                    case kVBCppBinary_Subtraction:
                        pResult->u.UnsignedValue.u64 -= Result2.u.UnsignedValue.u64;
                        break;
                    case kVBCppBinary_LeftShift:
                        pResult->u.UnsignedValue.u64 <<= Result2.u.UnsignedValue.u64;
                        break;
                    case kVBCppBinary_RightShift:
                        pResult->u.UnsignedValue.u64 >>= Result2.u.UnsignedValue.u64;
                        break;
                    case kVBCppBinary_LessThan:
                        pResult->u.UnsignedValue.u64 = pResult->u.UnsignedValue.u64 < Result2.u.UnsignedValue.u64;
                        break;
                    case kVBCppBinary_LessThanOrEqual:
                        pResult->u.UnsignedValue.u64 = pResult->u.UnsignedValue.u64 <= Result2.u.UnsignedValue.u64;
                        break;
                    case kVBCppBinary_GreaterThan:
                        pResult->u.UnsignedValue.u64 = pResult->u.UnsignedValue.u64 > Result2.u.UnsignedValue.u64;
                        break;
                    case kVBCppBinary_GreaterThanOrEqual:
                        pResult->u.UnsignedValue.u64 = pResult->u.UnsignedValue.u64 >= Result2.u.UnsignedValue.u64;
                        break;
                    case kVBCppBinary_EqualTo:
                        pResult->u.UnsignedValue.u64 = pResult->u.UnsignedValue.u64 == Result2.u.UnsignedValue.u64;
                        break;
                    case kVBCppBinary_NotEqualTo:
                        pResult->u.UnsignedValue.u64 = pResult->u.UnsignedValue.u64 != Result2.u.UnsignedValue.u64;
                        break;
                    case kVBCppBinary_BitwiseAnd:
                        pResult->u.UnsignedValue.u64 &= Result2.u.UnsignedValue.u64;
                        break;
                    case kVBCppBinary_BitwiseXor:
                        pResult->u.UnsignedValue.u64 ^= Result2.u.UnsignedValue.u64;
                        break;
                    case kVBCppBinary_BitwiseOr:
                        pResult->u.UnsignedValue.u64 |= Result2.u.UnsignedValue.u64;
                        break;
                    case kVBCppBinary_LogicalAnd:
                        pResult->u.UnsignedValue.u64 = pResult->u.UnsignedValue.u64 && Result2.u.UnsignedValue.u64;
                        break;
                    case kVBCppBinary_LogicalOr:
                        pResult->u.UnsignedValue.u64 = pResult->u.UnsignedValue.u64 || Result2.u.UnsignedValue.u64;
                        break;
                    default:
                        return vbcppError(pThis, "Internal error: u.Binary.enmOperator=%d", pRoot->u.Binary.enmOperator);
                }
            }
            else
            {
                switch (pRoot->u.Binary.enmOperator)
                {
                    case kVBCppBinary_Multiplication:
                        pResult->u.SignedValue.s64 *= Result2.u.SignedValue.s64;
                        break;
                    case kVBCppBinary_Division:
                        if (!Result2.u.SignedValue.s64)
                            return vbcppError(pThis, "Divide by zero");
                        pResult->u.SignedValue.s64 /= Result2.u.SignedValue.s64;
                        break;
                    case kVBCppBinary_Modulo:
                        if (!Result2.u.SignedValue.s64)
                            return vbcppError(pThis, "Divide by zero");
                        pResult->u.SignedValue.s64 %= Result2.u.SignedValue.s64;
                        break;
                    case kVBCppBinary_Addition:
                        pResult->u.SignedValue.s64 += Result2.u.SignedValue.s64;
                        break;
                    case kVBCppBinary_Subtraction:
                        pResult->u.SignedValue.s64 -= Result2.u.SignedValue.s64;
                        break;
                    case kVBCppBinary_LeftShift:
                        pResult->u.SignedValue.s64 <<= Result2.u.SignedValue.s64;
                        break;
                    case kVBCppBinary_RightShift:
                        pResult->u.SignedValue.s64 >>= Result2.u.SignedValue.s64;
                        break;
                    case kVBCppBinary_LessThan:
                        pResult->u.SignedValue.s64 = pResult->u.SignedValue.s64 < Result2.u.SignedValue.s64;
                        break;
                    case kVBCppBinary_LessThanOrEqual:
                        pResult->u.SignedValue.s64 = pResult->u.SignedValue.s64 <= Result2.u.SignedValue.s64;
                        break;
                    case kVBCppBinary_GreaterThan:
                        pResult->u.SignedValue.s64 = pResult->u.SignedValue.s64 > Result2.u.SignedValue.s64;
                        break;
                    case kVBCppBinary_GreaterThanOrEqual:
                        pResult->u.SignedValue.s64 = pResult->u.SignedValue.s64 >= Result2.u.SignedValue.s64;
                        break;
                    case kVBCppBinary_EqualTo:
                        pResult->u.SignedValue.s64 = pResult->u.SignedValue.s64 == Result2.u.SignedValue.s64;
                        break;
                    case kVBCppBinary_NotEqualTo:
                        pResult->u.SignedValue.s64 = pResult->u.SignedValue.s64 != Result2.u.SignedValue.s64;
                        break;
                    case kVBCppBinary_BitwiseAnd:
                        pResult->u.SignedValue.s64 &= Result2.u.SignedValue.s64;
                        break;
                    case kVBCppBinary_BitwiseXor:
                        pResult->u.SignedValue.s64 ^= Result2.u.SignedValue.s64;
                        break;
                    case kVBCppBinary_BitwiseOr:
                        pResult->u.SignedValue.s64 |= Result2.u.SignedValue.s64;
                        break;
                    case kVBCppBinary_LogicalAnd:
                        pResult->u.SignedValue.s64 = pResult->u.SignedValue.s64 && Result2.u.SignedValue.s64;
                        break;
                    case kVBCppBinary_LogicalOr:
                        pResult->u.SignedValue.s64 = pResult->u.SignedValue.s64 || Result2.u.SignedValue.s64;
                        break;
                    default:
                        return vbcppError(pThis, "Internal error: u.Binary.enmOperator=%d", pRoot->u.Binary.enmOperator);
                }
            }
            return rcExit;
        }

        case kVBCppExprKind_Ternary:
            rcExit = vbcppExprEvaluteTree(pThis, pRoot->u.Ternary.pExpr, pResult);
            if (rcExit != RTEXITCODE_SUCCESS)
                return rcExit;
            if (vbcppExprIsExprTrue(pResult))
                return vbcppExprEvaluteTree(pThis, pRoot->u.Ternary.pTrue, pResult);
            return vbcppExprEvaluteTree(pThis, pRoot->u.Ternary.pFalse, pResult);

        default:
            return vbcppError(pThis, "Internal error: enmKind=%d", pRoot->enmKind);
    }
}


/**
 * Evalutes the expression.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pszExpr             The expression.
 * @param   cchExpr             The length of the expression.
 * @param   cReplacements       The number of replacements.
 * @param   cDefinedUnknown     The number of defined(UNKNOWN) cases.  (Relevant
 *                              for selective modes.)
 * @param   penmResult          Where to store the result.
 */
static RTEXITCODE vbcppExprEval(PVBCPP pThis, char *pszExpr, size_t cchExpr, size_t cReplacements, size_t cDefinedUnknown,
                                VBCPPEVAL *penmResult)
{
    Assert(strlen(pszExpr) == cchExpr);
    RT_NOREF_PV(cReplacements);

    size_t      cUndefined;
    PVBCPPEXPR  pExprTree;
    RTEXITCODE  rcExit = vbcppExprParse(pThis, pszExpr, cchExpr, &pExprTree, &cUndefined);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        if (   (!cUndefined && !cDefinedUnknown)
            || pThis->enmMode == kVBCppMode_SelectiveD
            || pThis->enmMode == kVBCppMode_Standard)
        {
            VBCPPEXPR Result;
            rcExit = vbcppExprEvaluteTree(pThis, pExprTree, &Result);
            if (rcExit == RTEXITCODE_SUCCESS)
            {
                if (vbcppExprIsExprTrue(&Result))
                    *penmResult = kVBCppEval_True;
                else
                    *penmResult = kVBCppEval_False;
            }
        }
        else
            *penmResult = kVBCppEval_Undecided;
    }
    return rcExit;
}


static RTEXITCODE vbcppExtractSkipCommentLine(PVBCPP pThis, PSCMSTREAM pStrmInput)
{
    RT_NOREF_PV(pThis);

    unsigned chPrev = ScmStreamGetCh(pStrmInput); Assert(chPrev == '/');
    unsigned ch;
    while ((ch = ScmStreamPeekCh(pStrmInput)) != ~(unsigned)0)
    {
        if (ch == '\r' || ch == '\n')
        {
            if (chPrev != '\\')
                break;
            ScmStreamSeekByLine(pStrmInput, ScmStreamTellLine(pStrmInput) + 1);
            chPrev = ch;
        }
        else
        {
            chPrev = ScmStreamGetCh(pStrmInput);
            Assert(chPrev == ch);
        }
    }
    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE vbcppExtractSkipComment(PVBCPP pThis, PSCMSTREAM pStrmInput)
{
    unsigned ch = ScmStreamGetCh(pStrmInput); Assert(ch == '*');
    while ((ch = ScmStreamGetCh(pStrmInput)) != ~(unsigned)0)
    {
        if (ch == '*')
        {
            ch = ScmStreamGetCh(pStrmInput);
            if (ch == '/')
                return RTEXITCODE_SUCCESS;
        }
    }
    return vbcppError(pThis, "Expected '*/'");
}


static RTEXITCODE vbcppExtractQuotedString(PVBCPP pThis, PSCMSTREAM pStrmInput, PVBCPPSTRBUF pStrBuf,
                                           char chOpen, char chClose)
{
    unsigned ch = ScmStreamGetCh(pStrmInput);
    Assert(ch == (unsigned)chOpen);

    RTEXITCODE rcExit = vbcppStrBufAppendCh(pStrBuf, chOpen);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;

    for (;;)
    {
        ch = ScmStreamGetCh(pStrmInput);
        if (ch == '\\')
        {
            ch = ScmStreamGetCh(pStrmInput);
            if (ch == ~(unsigned)0)
                break;
            rcExit = vbcppStrBufAppendCh(pStrBuf, '\\');
            if (rcExit == RTEXITCODE_SUCCESS)
                rcExit = vbcppStrBufAppendCh(pStrBuf, ch);
            if (rcExit != RTEXITCODE_SUCCESS)
                return rcExit;
        }
        else if (ch != ~(unsigned)0)
        {
            rcExit = vbcppStrBufAppendCh(pStrBuf, ch);
            if (rcExit != RTEXITCODE_SUCCESS)
                return rcExit;
            if (ch == (unsigned)chClose)
                return RTEXITCODE_SUCCESS;
        }
        else
            break;
    }

    return vbcppError(pThis, "File ended with an open character constant");
}


/**
 * Extracts a line from the stream, stripping it for comments and maybe
 * optimzing some of the whitespace.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 * @param   pStrBuf             Where to store the extracted line. Caller must
 *                              initialize this prior to the call an delete it
 *                              after use (even on failure).
 * @param   poffComment         Where to note down the position of the final
 *                              comment. Optional.
 */
static RTEXITCODE vbcppExtractDirectiveLine(PVBCPP pThis, PSCMSTREAM pStrmInput, PVBCPPSTRBUF pStrBuf, size_t *poffComment)
{
    size_t      offComment  = ~(size_t)0;
    unsigned    ch;
    while ((ch = ScmStreamPeekCh(pStrmInput)) != ~(unsigned)0)
    {
        RTEXITCODE rcExit;
        if (ch == '/')
        {
            /* Comment? */
            unsigned ch2 = ScmStreamGetCh(pStrmInput); Assert(ch == ch2); NOREF(ch2);
            ch = ScmStreamPeekCh(pStrmInput);
            if (ch == '*')
            {
                offComment = ScmStreamTell(pStrmInput) - 1;
                rcExit = vbcppExtractSkipComment(pThis, pStrmInput);
            }
            else if (ch == '/')
            {
                offComment = ScmStreamTell(pStrmInput) - 1;
                rcExit = vbcppExtractSkipCommentLine(pThis, pStrmInput);
            }
            else
                rcExit = vbcppStrBufAppendCh(pStrBuf, '/');
        }
        else if (ch == '\'')
        {
            offComment = ~(size_t)0;
            rcExit = vbcppExtractQuotedString(pThis, pStrmInput, pStrBuf, '\'', '\'');
        }
        else if (ch == '"')
        {
            offComment = ~(size_t)0;
            rcExit = vbcppExtractQuotedString(pThis, pStrmInput, pStrBuf, '"', '"');
        }
        else if (ch == '\r' || ch == '\n')
            break; /* done */
        else if (   RT_C_IS_SPACE(ch)
                 && (   RT_C_IS_SPACE(vbcppStrBufLastCh(pStrBuf))
                     || vbcppStrBufLastCh(pStrBuf) == '\0') )
        {
            unsigned ch2 = ScmStreamGetCh(pStrmInput);
            Assert(ch == ch2); NOREF(ch2);
            rcExit = RTEXITCODE_SUCCESS;
        }
        else
        {
            unsigned ch2 = ScmStreamGetCh(pStrmInput); Assert(ch == ch2);

            /* Escaped newline? */
            if (   ch == '\\'
                && (   (ch2 = ScmStreamPeekCh(pStrmInput)) == '\r'
                    || ch2 == '\n'))
            {
                ScmStreamSeekByLine(pStrmInput, ScmStreamTellLine(pStrmInput) + 1);
                rcExit = RTEXITCODE_SUCCESS;
            }
            else
            {
                offComment = ~(size_t)0;
                rcExit = vbcppStrBufAppendCh(pStrBuf, ch);
            }
        }
        if (rcExit != RTEXITCODE_SUCCESS)
            return rcExit;
    }

    if (poffComment)
        *poffComment = offComment;
    return RTEXITCODE_SUCCESS;
}


/**
 * Processes a abbreviated line number directive.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 * @param   offStart            The stream position where the directive
 *                              started (for pass thru).
 * @param   enmKind             The kind of directive we're processing.
 */
static RTEXITCODE vbcppDirectiveIfOrElif(PVBCPP pThis, PSCMSTREAM pStrmInput, size_t offStart,
                                         VBCPPCONDKIND enmKind)
{
    /*
     * Check for missing #if if #elif.
     */
    if (   enmKind == kVBCppCondKind_ElIf
        && !pThis->pCondStack )
        return vbcppError(pThis, "#elif without #if");

    /*
     * Extract the expression string.
     */
    const char         *pchCondition = ScmStreamGetCur(pStrmInput);
    size_t              offComment;
    VBCPPMACROEXP       ExpCtx;
#if 0
    ExpCtx.pMacroStack    = NULL;
#endif
    ExpCtx.pStrmInput     = NULL;
    ExpCtx.papszArgs      = NULL;
    ExpCtx.cArgs          = 0;
    ExpCtx.cArgsAlloced   = 0;
    vbcppStrBufInit(&ExpCtx.StrBuf, pThis);
    RTEXITCODE  rcExit = vbcppExtractDirectiveLine(pThis, pStrmInput, &ExpCtx.StrBuf, &offComment);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        if (RT_C_IS_SPACE(*pchCondition))
            pchCondition++;
        size_t const    cchCondition = ScmStreamGetCur(pStrmInput) - pchCondition;

        /*
         * Expand known macros in it.
         */
        size_t          cReplacements;
        size_t          cDefinedUnknown;
        rcExit = vbcppMacroExpandReScan(pThis, &ExpCtx, kMacroReScanMode_Expression, &cReplacements, &cDefinedUnknown);
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            /*
             * Strip it and check that it's not empty.
             */
            char   *pszExpr = ExpCtx.StrBuf.pszBuf;
            size_t  cchExpr = ExpCtx.StrBuf.cchBuf;
            while (cchExpr > 0 && RT_C_IS_SPACE(*pszExpr))
                pszExpr++, cchExpr--;

            while (cchExpr > 0 && RT_C_IS_SPACE(pszExpr[cchExpr - 1]))
            {
                pszExpr[--cchExpr] = '\0';
                ExpCtx.StrBuf.cchBuf--;
            }
            if (cchExpr)
            {
                /*
                 * Now, evalute the expression.
                 */
                VBCPPEVAL enmResult;
                rcExit = vbcppExprEval(pThis, pszExpr, cchExpr, cReplacements, cDefinedUnknown, &enmResult);
                if (rcExit == RTEXITCODE_SUCCESS)
                {
                    /*
                     * Take action.
                     */
                    if (enmKind != kVBCppCondKind_ElIf)
                        rcExit = vbcppCondPush(pThis, pStrmInput, offComment, enmKind, enmResult,
                                               pchCondition, cchCondition);
                    else
                    {
                        PVBCPPCOND pCond = pThis->pCondStack;
                        if (   pCond->enmResult != kVBCppEval_Undecided
                            && (   !pCond->pUp
                                || pCond->pUp->enmStackResult == kVBCppEval_True))
                        {
                            Assert(enmResult == kVBCppEval_True || enmResult == kVBCppEval_False);
                            if (   pCond->enmResult == kVBCppEval_False
                                && enmResult        == kVBCppEval_True
                                && !pCond->fElIfDecided)
                            {
                                pCond->enmStackResult = kVBCppEval_True;
                                pCond->fElIfDecided   = true;
                            }
                            else
                                pCond->enmStackResult = kVBCppEval_False;
                            pThis->fIf0Mode = pCond->enmStackResult == kVBCppEval_False;
                        }
                        pCond->enmKind   = kVBCppCondKind_ElIf;
                        pCond->enmResult = enmResult;
                        pCond->pchCond   = pchCondition;
                        pCond->cchCond   = cchCondition;

                        /*
                         * Do #elif pass thru.
                         */
                        if (   !pThis->fIf0Mode
                            && pCond->enmResult == kVBCppEval_Undecided)
                        {
                            ssize_t cch = ScmStreamPrintf(&pThis->StrmOutput, "#%*selif", pCond->iKeepLevel - 1, "");
                            if (cch > 0)
                                rcExit = vbcppOutputComment(pThis, pStrmInput, offStart, cch, 1);
                            else
                                rcExit = vbcppError(pThis, "Output error %Rrc", (int)cch);
                        }
                        else
                            pThis->fJustDroppedLine = true;
                    }
                }
            }
            else
                rcExit = vbcppError(pThis, "Empty #if expression");
        }
    }
    vbcppMacroExpandCleanup(&ExpCtx);
    return rcExit;
}


/**
 * Processes a abbreviated line number directive.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 * @param   offStart            The stream position where the directive
 *                              started (for pass thru).
 */
static RTEXITCODE vbcppDirectiveIfDef(PVBCPP pThis, PSCMSTREAM pStrmInput, size_t offStart)
{
    /*
     * Parse it.
     */
    RTEXITCODE rcExit = vbcppProcessSkipWhiteEscapedEolAndComments(pThis, pStrmInput);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        size_t      cchDefine;
        const char *pchDefine = ScmStreamCGetWord(pStrmInput, &cchDefine);
        if (pchDefine)
        {
            rcExit = vbcppProcessSkipWhiteEscapedEolAndCommentsCheckEol(pThis, pStrmInput);
            if (rcExit == RTEXITCODE_SUCCESS)
            {
                /*
                 * Evaluate it.
                 */
                VBCPPEVAL enmEval;
                if (vbcppMacroExists(pThis, pchDefine, cchDefine))
                    enmEval = kVBCppEval_True;
                else if (   !pThis->fUndecidedConditionals
                         || RTStrSpaceGetN(&pThis->UndefStrSpace, pchDefine, cchDefine) != NULL)
                    enmEval = kVBCppEval_False;
                else
                    enmEval = kVBCppEval_Undecided;
                rcExit = vbcppCondPush(pThis, pStrmInput, offStart, kVBCppCondKind_IfDef, enmEval,
                                       pchDefine, cchDefine);
            }
        }
        else
            rcExit = vbcppError(pThis, "Malformed #ifdef");
    }
    return rcExit;
}


/**
 * Processes a abbreviated line number directive.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 * @param   offStart            The stream position where the directive
 *                              started (for pass thru).
 */
static RTEXITCODE vbcppDirectiveIfNDef(PVBCPP pThis, PSCMSTREAM pStrmInput, size_t offStart)
{
    /*
     * Parse it.
     */
    RTEXITCODE rcExit = vbcppProcessSkipWhiteEscapedEolAndComments(pThis, pStrmInput);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        size_t      cchDefine;
        const char *pchDefine = ScmStreamCGetWord(pStrmInput, &cchDefine);
        if (pchDefine)
        {
            rcExit = vbcppProcessSkipWhiteEscapedEolAndCommentsCheckEol(pThis, pStrmInput);
            if (rcExit == RTEXITCODE_SUCCESS)
            {
                /*
                 * Evaluate it.
                 */
                VBCPPEVAL enmEval;
                if (vbcppMacroExists(pThis, pchDefine, cchDefine))
                    enmEval = kVBCppEval_False;
                else if (   !pThis->fUndecidedConditionals
                         || RTStrSpaceGetN(&pThis->UndefStrSpace, pchDefine, cchDefine) != NULL)
                    enmEval = kVBCppEval_True;
                else
                    enmEval = kVBCppEval_Undecided;
                rcExit = vbcppCondPush(pThis, pStrmInput, offStart, kVBCppCondKind_IfNDef, enmEval,
                                       pchDefine, cchDefine);
            }
        }
        else
            rcExit = vbcppError(pThis, "Malformed #ifndef");
    }
    return rcExit;
}


/**
 * Processes a abbreviated line number directive.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 * @param   offStart            The stream position where the directive
 *                              started (for pass thru).
 */
static RTEXITCODE vbcppDirectiveElse(PVBCPP pThis, PSCMSTREAM pStrmInput, size_t offStart)
{
    /*
     * Nothing to parse, just comment positions to find and note down.
     */
    offStart = vbcppProcessSkipWhite(pStrmInput);
    RTEXITCODE rcExit = vbcppProcessSkipWhiteEscapedEolAndCommentsCheckEol(pThis, pStrmInput);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /*
         * Execute.
         */
        PVBCPPCOND pCond = pThis->pCondStack;
        if (pCond)
        {
            if (!pCond->fSeenElse)
            {
                pCond->fSeenElse = true;
                if (   pCond->enmResult != kVBCppEval_Undecided
                    && (   !pCond->pUp
                        || pCond->pUp->enmStackResult == kVBCppEval_True))
                {
                    if (   pCond->enmResult == kVBCppEval_True
                        || pCond->fElIfDecided)

                        pCond->enmStackResult = kVBCppEval_False;
                    else
                        pCond->enmStackResult = kVBCppEval_True;
                    pThis->fIf0Mode = pCond->enmStackResult == kVBCppEval_False;
                }

                /*
                 * Do pass thru.
                 */
                if (   !pThis->fIf0Mode
                    && pCond->enmResult == kVBCppEval_Undecided)
                {
                    ssize_t cch = ScmStreamPrintf(&pThis->StrmOutput, "#%*selse", pCond->iKeepLevel - 1, "");
                    if (cch > 0)
                        rcExit = vbcppOutputComment(pThis, pStrmInput, offStart, cch, 2);
                    else
                        rcExit = vbcppError(pThis, "Output error %Rrc", (int)cch);
                }
                else
                    pThis->fJustDroppedLine = true;
            }
            else
                rcExit = vbcppError(pThis, "Double #else or/and missing #endif");
        }
        else
            rcExit = vbcppError(pThis, "#else without #if");
    }
    return rcExit;
}


/**
 * Processes a abbreviated line number directive.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 * @param   offStart            The stream position where the directive
 *                              started (for pass thru).
 */
static RTEXITCODE vbcppDirectiveEndif(PVBCPP pThis, PSCMSTREAM pStrmInput, size_t offStart)
{
    /*
     * Nothing to parse, just comment positions to find and note down.
     */
    offStart = vbcppProcessSkipWhite(pStrmInput);
    RTEXITCODE rcExit = vbcppProcessSkipWhiteEscapedEolAndCommentsCheckEol(pThis, pStrmInput);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /*
         * Execute.
         */
        PVBCPPCOND pCond = pThis->pCondStack;
        if (pCond)
        {
            pThis->pCondStack = pCond->pUp;
            pThis->fIf0Mode   = pCond->pUp && pCond->pUp->enmStackResult == kVBCppEval_False;

            /*
             * Do pass thru.
             */
            if (   !pThis->fIf0Mode
                && pCond->enmResult == kVBCppEval_Undecided)
            {
                ssize_t cch = ScmStreamPrintf(&pThis->StrmOutput, "#%*sendif", pCond->iKeepLevel - 1, "");
                if (cch > 0)
                    rcExit = vbcppOutputComment(pThis, pStrmInput, offStart, cch, 1);
                else
                    rcExit = vbcppError(pThis, "Output error %Rrc", (int)cch);
            }
            else
                pThis->fJustDroppedLine = true;
        }
        else
            rcExit = vbcppError(pThis, "#endif without #if");
    }
    return rcExit;
}





/*
 *
 *
 * Misc Directives
 * Misc Directives
 * Misc Directives
 * Misc Directives
 *
 *
 */


/**
 * Adds an include directory.
 *
 * @returns Program exit code, with error message on failure.
 * @param   pThis               The C preprocessor instance.
 * @param   pszDir              The directory to add.
 */
static RTEXITCODE vbcppAddInclude(PVBCPP pThis, const char *pszDir)
{
    uint32_t cIncludes = pThis->cIncludes;
    if (cIncludes >= _64K)
        return vbcppError(pThis, "Too many include directories");

    void *pv = RTMemRealloc(pThis->papszIncludes, (cIncludes + 1) * sizeof(char **));
    if (!pv)
        return vbcppError(pThis, "No memory for include directories");
    pThis->papszIncludes = (char **)pv;

    int rc = RTStrDupEx(&pThis->papszIncludes[cIncludes], pszDir);
    if (RT_FAILURE(rc))
        return vbcppError(pThis, "No string memory for include directories");

    pThis->cIncludes = cIncludes + 1;
    return RTEXITCODE_SUCCESS;
}


/**
 * Processes a abbreviated line number directive.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 * @param   offStart            The stream position where the directive
 *                              started (for pass thru).
 */
static RTEXITCODE vbcppDirectiveInclude(PVBCPP pThis, PSCMSTREAM pStrmInput, size_t offStart)
{
    RT_NOREF_PV(offStart);

    /*
     * Parse it.
     */
    RTEXITCODE rcExit = vbcppProcessSkipWhiteEscapedEolAndComments(pThis, pStrmInput);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        size_t      cchFileSpec = 0;
        const char *pchFileSpec = NULL;
        const char *pchFilename = NULL;

        unsigned ch = ScmStreamPeekCh(pStrmInput);
        unsigned chType = ch;
        if (ch == '"' || ch == '<')
        {
            ScmStreamGetCh(pStrmInput);
            pchFileSpec = pchFilename = ScmStreamGetCur(pStrmInput);
            unsigned chEnd  = chType == '<' ? '>' : '"';
            while (   (ch = ScmStreamGetCh(pStrmInput)) != ~(unsigned)0
                   &&  ch != chEnd)
            {
                if (ch == '\r' || ch == '\n')
                {
                    rcExit = vbcppError(pThis, "Multi-line include file specfications are not supported");
                    break;
                }
            }

            if (rcExit == RTEXITCODE_SUCCESS)
            {
                if (ch != ~(unsigned)0)
                    cchFileSpec = ScmStreamGetCur(pStrmInput) - pchFilename - 1;
                else
                    rcExit = vbcppError(pThis, "Expected '%c'", chType);
            }
        }
        else if (vbcppIsCIdentifierLeadChar(ch))
        {
            //pchFileSpec = ScmStreamCGetWord(pStrmInput, &cchFileSpec);
            rcExit = vbcppError(pThis, "Including via a define is not implemented yet");
        }
        else
            rcExit = vbcppError(pThis, "Malformed include directive");

        /*
         * Take down the location of the next non-white space, in case we need
         * to pass thru the directive further down. Then skip to the end of the
         * line.
         */
        size_t const offIncEnd = vbcppProcessSkipWhite(pStrmInput);
        if (rcExit == RTEXITCODE_SUCCESS)
            rcExit = vbcppProcessSkipWhiteEscapedEolAndCommentsCheckEol(pThis, pStrmInput);

        if (rcExit == RTEXITCODE_SUCCESS)
        {
            /*
             * Execute it.
             */
            if (pThis->enmIncludeAction == kVBCppIncludeAction_Include)
            {
                /** @todo Search for the include file and push it onto the input stack.
                 *  Not difficult, just unnecessary rigth now. */
                rcExit = vbcppError(pThis, "Sorry, includes are not yet implemented");
            }
            else if (pThis->enmIncludeAction == kVBCppIncludeAction_PassThru)
            {
                /* Pretty print the passthru. */
                unsigned cchIndent = pThis->pCondStack ? pThis->pCondStack->iKeepLevel : 0;
                ssize_t  cch;
                if (chType == '<')
                    cch = ScmStreamPrintf(&pThis->StrmOutput, "#%*sinclude <%.*s>",
                                          cchIndent, "", cchFileSpec, pchFileSpec);
                else if (chType == '"')
                    cch = ScmStreamPrintf(&pThis->StrmOutput, "#%*sinclude \"%.*s\"",
                                          cchIndent, "", cchFileSpec, pchFileSpec);
                else
                    cch = ScmStreamPrintf(&pThis->StrmOutput, "#%*sinclude %.*s",
                                          cchIndent, "", cchFileSpec, pchFileSpec);
                if (cch > 0)
                    rcExit = vbcppOutputComment(pThis, pStrmInput, offIncEnd, cch, 1);
                else
                    rcExit = vbcppError(pThis, "Output error %Rrc", (int)cch);

            }
            else
            {
                Assert(pThis->enmIncludeAction == kVBCppIncludeAction_Drop);
                pThis->fJustDroppedLine = true;
            }
        }
    }
    return rcExit;
}


/**
 * Processes a abbreviated line number directive.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 * @param   offStart            The stream position where the directive
 *                              started (for pass thru).
 */
static RTEXITCODE vbcppDirectivePragma(PVBCPP pThis, PSCMSTREAM pStrmInput, size_t offStart)
{
    RT_NOREF_PV(offStart);

    /*
     * Parse out the first word.
     */
    RTEXITCODE rcExit = vbcppProcessSkipWhiteEscapedEolAndComments(pThis, pStrmInput);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        size_t      cchPragma;
        const char *pchPragma = ScmStreamCGetWord(pStrmInput, &cchPragma);
        if (pchPragma)
        {
            size_t const off2nd = vbcppProcessSkipWhite(pStrmInput);
            size_t       offComment;
            rcExit = vbcppInputSkipToEndOfDirectiveLine(pThis, pStrmInput, &offComment);
            if (rcExit == RTEXITCODE_SUCCESS)
            {
                /*
                 * What to do about this
                 */
                bool fPassThru = false;
                if (   cchPragma  == 1
                    && *pchPragma == 'D')
                    fPassThru = pThis->fPassThruPragmaD;
                else if (    cchPragma == 3
                         &&  !strncmp(pchPragma, "STD", 3))
                    fPassThru = pThis->fPassThruPragmaSTD;
                else
                    fPassThru = pThis->fPassThruPragmaOther;
                if (fPassThru)
                {
                    unsigned cchIndent = pThis->pCondStack ? pThis->pCondStack->iKeepLevel : 0;
                    ssize_t  cch = ScmStreamPrintf(&pThis->StrmOutput, "#%*spragma %.*s",
                                                   cchIndent, "", cchPragma, pchPragma);
                    if (cch > 0)
                        rcExit = vbcppOutputComment(pThis, pStrmInput, off2nd, cch, 1);
                    else
                        rcExit = vbcppError(pThis, "output error");
                }
                else
                    pThis->fJustDroppedLine = true;
            }
        }
        else
            rcExit = vbcppError(pThis, "Malformed #pragma");
    }

    return rcExit;
}


/**
 * Processes an error directive.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 * @param   offStart            The stream position where the directive
 *                              started (for pass thru).
 */
static RTEXITCODE vbcppDirectiveError(PVBCPP pThis, PSCMSTREAM pStrmInput, size_t offStart)
{
    RT_NOREF(offStart);

    /*
     * Parse out the message.
     */
    size_t const off1st = vbcppProcessSkipWhite(pStrmInput);
    size_t       offComment;
    RTEXITCODE rcExit = vbcppInputSkipToEndOfDirectiveLine(pThis, pStrmInput, &offComment);
    if (rcExit == RTEXITCODE_SUCCESS)
    {
        /*
         * What to do about this
         */
        if (pThis->fPassThruError)
        {
            unsigned cchIndent = pThis->pCondStack ? pThis->pCondStack->iKeepLevel : 0;
            ssize_t  cch = ScmStreamPrintf(&pThis->StrmOutput, "#%*serror", cchIndent, "");
            if (cch > 0)
                rcExit = vbcppOutputComment(pThis, pStrmInput, off1st, cch, 1);
            else
                rcExit = vbcppError(pThis, "output error");
            return rcExit;
        }
        return vbcppError(pThis, "Hit an #error");
    }
    RT_NOREF_PV(offStart);
    vbcppError(pThis, "Malformed #error");
    return vbcppError(pThis, "Hit an #error");
}


/**
 * Processes a abbreviated line number directive.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 * @param   offStart            The stream position where the directive
 *                              started (for pass thru).
 */
static RTEXITCODE vbcppDirectiveLineNo(PVBCPP pThis, PSCMSTREAM pStrmInput, size_t offStart)
{
    RT_NOREF_PV(offStart);
    RT_NOREF_PV(pStrmInput);
    return vbcppError(pThis, "Not implemented: %s", __FUNCTION__);
}


/**
 * Processes a abbreviated line number directive.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 */
static RTEXITCODE vbcppDirectiveLineNoShort(PVBCPP pThis, PSCMSTREAM pStrmInput)
{
    RT_NOREF_PV(pStrmInput);
    return vbcppError(pThis, "Not implemented: %s", __FUNCTION__);
}


/**
 * Handles a preprocessor directive.
 *
 * @returns RTEXITCODE_SUCCESS or RTEXITCODE_FAILURE+msg.
 * @param   pThis               The C preprocessor instance.
 * @param   pStrmInput          The input stream.
 */
static RTEXITCODE vbcppProcessDirective(PVBCPP pThis, PSCMSTREAM pStrmInput)
{
    /*
     * Get the directive and do a string switch on it.
     */
    RTEXITCODE  rcExit = vbcppProcessSkipWhiteEscapedEolAndComments(pThis, pStrmInput);
    if (rcExit != RTEXITCODE_SUCCESS)
        return rcExit;
    size_t      cchDirective;
    const char *pchDirective = ScmStreamCGetWord(pStrmInput, &cchDirective);
    if (pchDirective)
    {
        size_t const offStart = ScmStreamTell(pStrmInput);
#define IS_DIRECTIVE(a_sz) ( sizeof(a_sz) - 1 == cchDirective && memcmp(pchDirective, a_sz, sizeof(a_sz) - 1) == 0)
        if (IS_DIRECTIVE("if"))
            rcExit = vbcppDirectiveIfOrElif(pThis, pStrmInput, offStart, kVBCppCondKind_If);
        else if (IS_DIRECTIVE("elif"))
            rcExit = vbcppDirectiveIfOrElif(pThis, pStrmInput, offStart, kVBCppCondKind_ElIf);
        else if (IS_DIRECTIVE("ifdef"))
            rcExit = vbcppDirectiveIfDef(pThis, pStrmInput, offStart);
        else if (IS_DIRECTIVE("ifndef"))
            rcExit = vbcppDirectiveIfNDef(pThis, pStrmInput, offStart);
        else if (IS_DIRECTIVE("else"))
            rcExit = vbcppDirectiveElse(pThis, pStrmInput, offStart);
        else if (IS_DIRECTIVE("endif"))
            rcExit = vbcppDirectiveEndif(pThis, pStrmInput, offStart);
        else if (!pThis->fIf0Mode)
        {
            if (IS_DIRECTIVE("include"))
                rcExit = vbcppDirectiveInclude(pThis, pStrmInput, offStart);
            else if (IS_DIRECTIVE("define"))
                rcExit = vbcppDirectiveDefine(pThis, pStrmInput, offStart);
            else if (IS_DIRECTIVE("undef"))
                rcExit = vbcppDirectiveUndef(pThis, pStrmInput, offStart);
            else if (IS_DIRECTIVE("pragma"))
                rcExit = vbcppDirectivePragma(pThis, pStrmInput, offStart);
            else if (IS_DIRECTIVE("error"))
                rcExit = vbcppDirectiveError(pThis, pStrmInput, offStart);
            else if (IS_DIRECTIVE("line"))
                rcExit = vbcppDirectiveLineNo(pThis, pStrmInput, offStart);
            else
                rcExit = vbcppError(pThis, "Unknown preprocessor directive '#%.*s'", cchDirective, pchDirective);
        }
#undef IS_DIRECTIVE
    }
    else if (!pThis->fIf0Mode)
    {
        /* Could it be a # <num> "file" directive? */
        unsigned ch = ScmStreamPeekCh(pStrmInput);
        if (RT_C_IS_DIGIT(ch))
            rcExit = vbcppDirectiveLineNoShort(pThis, pStrmInput);
        else
            rcExit = vbcppError(pThis, "Malformed preprocessor directive");
    }
    return rcExit;
}


/*
 *
 *
 * M a i n   b o d y.
 * M a i n   b o d y.
 * M a i n   b o d y.
 * M a i n   b o d y.
 * M a i n   b o d y.
 *
 *
 */


/**
 * Does the actually preprocessing of the input file.
 *
 * @returns Exit code.
 * @param   pThis               The C preprocessor instance.
 */
static RTEXITCODE vbcppPreprocess(PVBCPP pThis)
{
    RTEXITCODE  rcExit = RTEXITCODE_SUCCESS;

    /*
     * Parse.
     */
    while (pThis->pInputStack)
    {
        pThis->fMaybePreprocessorLine = true;

        PSCMSTREAM  pStrmInput = &pThis->pInputStack->StrmInput;
        unsigned    ch;
        while ((ch = ScmStreamGetCh(pStrmInput)) != ~(unsigned)0)
        {
            if (ch == '/')
            {
                ch = ScmStreamPeekCh(pStrmInput);
                if (ch == '*')
                    rcExit = vbcppProcessMultiLineComment(pThis, pStrmInput);
                else if (ch == '/')
                    rcExit = vbcppProcessOneLineComment(pThis, pStrmInput);
                else
                {
                    pThis->fMaybePreprocessorLine = false;
                    if (!pThis->fIf0Mode)
                        rcExit = vbcppOutputCh(pThis, '/');
                }
            }
            else if (ch == '#' && pThis->fMaybePreprocessorLine)
            {
                rcExit = vbcppProcessDirective(pThis, pStrmInput);
                pStrmInput = &pThis->pInputStack->StrmInput;
            }
            else if (ch == '\r' || ch == '\n')
            {
                if (   (   !pThis->fIf0Mode
                        && !pThis->fJustDroppedLine)
                    || !pThis->fRemoveDroppedLines
                    || !ScmStreamIsAtStartOfLine(&pThis->StrmOutput))
                    rcExit = vbcppOutputCh(pThis, ch);
                pThis->fJustDroppedLine       = false;
                pThis->fMaybePreprocessorLine = true;
            }
            else if (RT_C_IS_SPACE(ch))
            {
                if (!pThis->fIf0Mode)
                    rcExit = vbcppOutputCh(pThis, ch);
            }
            else
            {
                pThis->fMaybePreprocessorLine = false;
                if (!pThis->fIf0Mode)
                {
                    if (ch == '"')
                        rcExit = vbcppProcessStringLitteral(pThis, pStrmInput);
                    else if (ch == '\'')
                        rcExit = vbcppProcessCharacterConstant(pThis, pStrmInput);
                    else if (vbcppIsCIdentifierLeadChar(ch))
                        rcExit = vbcppProcessIdentifier(pThis, pStrmInput);
                    else if (RT_C_IS_DIGIT(ch))
                        rcExit = vbcppProcessNumber(pThis, pStrmInput, ch);
                    else
                        rcExit = vbcppOutputCh(pThis, ch);
                }
            }
            if (rcExit != RTEXITCODE_SUCCESS)
                break;
        }

        /*
         * Check for errors.
         */
        if (rcExit != RTEXITCODE_SUCCESS)
            break;

        /*
         * Pop the input stack.
         */
        PVBCPPINPUT pPopped = pThis->pInputStack;
        pThis->pInputStack = pPopped->pUp;
        RTMemFree(pPopped);
    }

    return rcExit;
}


/**
 * Opens the input and output streams.
 *
 * @returns Exit code.
 * @param   pThis               The C preprocessor instance.
 */
static RTEXITCODE vbcppOpenStreams(PVBCPP pThis)
{
    const char * const pszInput = pThis->pszInput ? pThis->pszInput : "stdin";
    size_t const       cchName  = strlen(pszInput);
    PVBCPPINPUT const  pInput   = (PVBCPPINPUT)RTMemAlloc(RT_UOFFSETOF_DYN(VBCPPINPUT, szName[cchName + 1]));
    if (!pInput)
        return vbcppError(pThis, "out of memory");
    pInput->pUp          = pThis->pInputStack;
    pInput->pszSpecified = pInput->szName;
    memcpy(pInput->szName, pszInput, cchName + 1);
    pThis->pInputStack   = pInput;
    int rc;
    if (pThis->pszInput)
    {
        rc = ScmStreamInitForReading(&pInput->StrmInput, pThis->pszInput);
        if (RT_FAILURE(rc))
            return vbcppError(pThis, "ScmStreamInitForReading returned %Rrc when opening and reading in input file (%s)",
                              rc, pThis->pszInput);
    }
    else
    {
        rc = ScmStreamInitForReadingFromStdInput(&pInput->StrmInput);
        if (RT_FAILURE(rc))
            return vbcppError(pThis, "ScmStreamInitForReadingFromStdInput returned %Rrc", rc);

    }

    rc = ScmStreamInitForWriting(&pThis->StrmOutput, &pInput->StrmInput);
    if (RT_FAILURE(rc))
        return vbcppError(pThis, "ScmStreamInitForWriting returned %Rrc", rc);

    pThis->fStrmOutputValid = true;
    return RTEXITCODE_SUCCESS;
}


/**
 * Changes the preprocessing mode.
 *
 * @param   pThis               The C preprocessor instance.
 * @param   enmMode             The new mode.
 */
static void vbcppSetMode(PVBCPP pThis, VBCPPMODE enmMode)
{
    switch (enmMode)
    {
        case kVBCppMode_Standard:
            pThis->fKeepComments                    = false;
            pThis->fRespectSourceDefines            = true;
            pThis->fAllowRedefiningCmdLineDefines   = true;
            pThis->fPassThruDefines                 = false;
            pThis->fUndecidedConditionals           = false;
            pThis->fPassThruPragmaD                 = false;
            pThis->fPassThruPragmaSTD               = true;
            pThis->fPassThruPragmaOther             = true;
            pThis->fPassThruError                   = false;
            pThis->fRemoveDroppedLines              = false;
            pThis->fLineSplicing                    = true;
            pThis->enmIncludeAction                 = kVBCppIncludeAction_Include;
            break;

        case kVBCppMode_Selective:
            pThis->fKeepComments                    = true;
            pThis->fRespectSourceDefines            = false;
            pThis->fAllowRedefiningCmdLineDefines   = false;
            pThis->fPassThruDefines                 = true;
            pThis->fUndecidedConditionals           = true;
            pThis->fPassThruPragmaD                 = true;
            pThis->fPassThruPragmaSTD               = true;
            pThis->fPassThruPragmaOther             = true;
            pThis->fPassThruError                   = true;
            pThis->fRemoveDroppedLines              = true;
            pThis->fLineSplicing                    = false;
            pThis->enmIncludeAction                 = kVBCppIncludeAction_PassThru;
            break;

        case kVBCppMode_SelectiveD:
            pThis->fKeepComments                    = true;
            pThis->fRespectSourceDefines            = true;
            pThis->fAllowRedefiningCmdLineDefines   = false;
            pThis->fPassThruDefines                 = false;
            pThis->fUndecidedConditionals           = false;
            pThis->fPassThruPragmaD                 = true;
            pThis->fPassThruPragmaSTD               = false;
            pThis->fPassThruPragmaOther             = false;
            pThis->fPassThruError                   = false;
            pThis->fRemoveDroppedLines              = true;
            pThis->fLineSplicing                    = false;
            pThis->enmIncludeAction                 = kVBCppIncludeAction_Drop;
            break;

        default:
            AssertFailedReturnVoid();
    }
    pThis->enmMode = enmMode;
}


/**
 * Parses the command line options.
 *
 * @returns Program exit code. Exit on non-success or if *pfExit is set.
 * @param   pThis               The C preprocessor instance.
 * @param   argc                The argument count.
 * @param   argv                The argument vector.
 * @param   pfExit              Pointer to the exit indicator.
 */
static RTEXITCODE vbcppParseOptions(PVBCPP pThis, int argc, char **argv, bool *pfExit)
{
    RTEXITCODE rcExit;

    *pfExit = false;

    /*
     * Option config.
     */
    static RTGETOPTDEF const s_aOpts[] =
    {
        { "--define",                   'D',                    RTGETOPT_REQ_STRING },
        { "--include-dir",              'I',                    RTGETOPT_REQ_STRING },
        { "--undefine",                 'U',                    RTGETOPT_REQ_STRING },
        { "--keep-comments",            'C',                    RTGETOPT_REQ_NOTHING },
        { "--strip-comments",           'c',                    RTGETOPT_REQ_NOTHING },
        { "--D-strip",                  'd',                    RTGETOPT_REQ_NOTHING },
#define OPT_MODE    1000
        { "--mode",                     OPT_MODE,               RTGETOPT_REQ_STRING },
    };

    RTGETOPTUNION   ValueUnion;
    RTGETOPTSTATE   GetOptState;
    int rc = RTGetOptInit(&GetOptState, argc, argv, &s_aOpts[0], RT_ELEMENTS(s_aOpts), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    AssertReleaseRCReturn(rc, RTEXITCODE_FAILURE);

    /*
     * Process the options.
     */
    while ((rc = RTGetOpt(&GetOptState, &ValueUnion)) != 0)
    {
        switch (rc)
        {
            case 'c':
                pThis->fKeepComments = false;
                break;

            case 'C':
                pThis->fKeepComments = false;
                break;

            case 'd':
                vbcppSetMode(pThis, kVBCppMode_SelectiveD);
                break;

            case OPT_MODE:
                if (   strcmp(ValueUnion.psz, "standard") == 0
                    || strcmp(ValueUnion.psz, "std") == 0)
                    vbcppSetMode(pThis, kVBCppMode_Standard);
                else if (   strcmp(ValueUnion.psz, "selective") == 0
                         || strcmp(ValueUnion.psz, "sel") == 0)
                    vbcppSetMode(pThis, kVBCppMode_Selective);
                else if (   strcmp(ValueUnion.psz, "selective-D") == 0
                         || strcmp(ValueUnion.psz, "D-strip") == 0)
                    vbcppSetMode(pThis, kVBCppMode_SelectiveD);
                else
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "Unknown mode '%s'!", ValueUnion.psz);
                break;

            case 'D':
            {
                const char *pszEqual = strchr(ValueUnion.psz, '=');
                if (pszEqual)
                    rcExit = vbcppMacroAdd(pThis, ValueUnion.psz, pszEqual - ValueUnion.psz, pszEqual + 1, RTSTR_MAX, true);
                else
                    rcExit = vbcppMacroAdd(pThis, ValueUnion.psz, RTSTR_MAX, "1", 1, true);
                if (rcExit != RTEXITCODE_SUCCESS)
                    return rcExit;
                break;
            }

            case 'I':
                rcExit = vbcppAddInclude(pThis, ValueUnion.psz);
                if (rcExit != RTEXITCODE_SUCCESS)
                    return rcExit;
                break;

            case 'U':
                rcExit = vbcppMacroUndef(pThis, ValueUnion.psz, RTSTR_MAX, true);
                if (rcExit != RTEXITCODE_SUCCESS)
                    return rcExit;
                break;

            case 'h':
                RTPrintf("Usage: VBoxCPP [options] <input.c> [output.ii]\n"
                         "\n"
                         "Options:\n"
                         "  --mode={mode}\n"
                         "    Sets the processing mode and asscoiated defaults:\n"
                         "      - selective   / sel: (default)\n"
                         "          Selectively applying the defines from the command line\n"
                         "          and preserving the rest.\n"
                         "      - selective-D / D-strip:\n"
                         "          Selectively applying the defines from the command line\n"
                         "          and removing things DTrace doesn't like.\n"
                         "      - standard    / std:\n"
                         "          Standard C preprocesing.  This is incomplete as, among other\n"
                         "          things, include processing has not yet been implemented.\n"
                         "  --D-strip\n"
                         "    Alias for --mode=selective-D\n"
                         "  --define={macro}[=val], -D{macro}[=val]\n"
                         "    Defines a macro.\n"
                         "  --undefine={macro}, -U{macro}\n"
                         "    Undefines a macro.\n"
                         "  --include-dir={dir}, -I{dir}\n"
                         "    Adds {dir} to the list of include directories that should\n"
                         "    be searched for header files."
                         "  --keep-comments, -C\n"
                         "    Keep comments unchanged in the output.\n"
                         "  --strip-comments, -c\n"
                         "    Strip comments in the output. (default)\n"
                         "\n"
                         "This is not a complete C preprocessor implementation (yet), as its use is\n"
                         "mainly to selectively apply and eliminate defines and/or things are DTrace\n"
                         "finds problematic.  Among the missing features are:\n"
                         "  - Support for ternary expressions.\n"
                         "  - Including header files (in standard mode).\n"
                         "  - +++\n"
                         );
                *pfExit = true;
                return RTEXITCODE_SUCCESS;

            case 'V':
            {
                /* The following is assuming that svn does it's job here. */
                static const char s_szRev[] = "$Revision: 106320 $";
                const char *psz = RTStrStripL(strchr(s_szRev, ' '));
                RTPrintf("r%.*s\n", strchr(psz, ' ') - psz, psz);
                *pfExit = true;
                return RTEXITCODE_SUCCESS;
            }

            case VINF_GETOPT_NOT_OPTION:
                if (!pThis->pszInput)
                    pThis->pszInput = ValueUnion.psz;
                else if (!pThis->pszOutput)
                    pThis->pszOutput = ValueUnion.psz;
                else
                    return RTMsgErrorExit(RTEXITCODE_SYNTAX, "too many file arguments");
                break;


            /*
             * Errors and bugs.
             */
            default:
                return RTGetOptPrintError(rc, &ValueUnion);
        }
    }

    return RTEXITCODE_SUCCESS;
}


/**
 * Terminates the preprocessor.
 *
 * This may return failure if an error was delayed.
 *
 * @returns Exit code.
 * @param   pThis               The C preprocessor instance.
 */
static RTEXITCODE vbcppTerm(PVBCPP pThis)
{
    /*
     * Flush the output first.
     */
    if (pThis->fStrmOutputValid)
    {
        if (pThis->pszOutput)
        {
            int rc = ScmStreamWriteToFile(&pThis->StrmOutput, "%s", pThis->pszOutput);
            if (RT_FAILURE(rc))
                vbcppError(pThis, "ScmStreamWriteToFile failed with %Rrc when writing '%s'", rc, pThis->pszOutput);
        }
        else
        {
            int rc = ScmStreamWriteToStdOut(&pThis->StrmOutput);
            if (RT_FAILURE(rc))
                vbcppError(pThis, "ScmStreamWriteToStdOut failed with %Rrc", rc);
        }
    }

    /*
     * Cleanup.
     */
    while (pThis->pInputStack)
    {
        ScmStreamDelete(&pThis->pInputStack->StrmInput);
        void *pvFree = pThis->pInputStack;
        pThis->pInputStack = pThis->pInputStack->pUp;
        RTMemFree(pvFree);
    }

    ScmStreamDelete(&pThis->StrmOutput);

    RTStrSpaceDestroy(&pThis->StrSpace, vbcppMacroFree, NULL);
    pThis->StrSpace = NULL;

    uint32_t i = pThis->cIncludes;
    while (i-- > 0)
        RTStrFree(pThis->papszIncludes[i]);
    RTMemFree(pThis->papszIncludes);
    pThis->papszIncludes = NULL;

    return pThis->rcExit;
}


/**
 * Initializes the C preprocessor instance data.
 *
 * @param   pThis               The C preprocessor instance data.
 */
static void vbcppInit(PVBCPP pThis)
{
    vbcppSetMode(pThis, kVBCppMode_Selective);
    pThis->cIncludes        = 0;
    pThis->papszIncludes    = NULL;
    pThis->pszInput         = NULL;
    pThis->pszOutput        = NULL;
    pThis->StrSpace         = NULL;
    pThis->UndefStrSpace    = NULL;
    pThis->cCondStackDepth  = 0;
    pThis->pCondStack       = NULL;
    pThis->fIf0Mode         = false;
    pThis->fJustDroppedLine = false;
    pThis->fMaybePreprocessorLine = true;
    VBCPP_BITMAP_EMPTY(pThis->bmDefined);
    pThis->cCondStackDepth  = 0;
    pThis->pInputStack      = NULL;
    RT_ZERO(pThis->StrmOutput);
    pThis->rcExit           = RTEXITCODE_SUCCESS;
    pThis->fStrmOutputValid = false;
}



int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    /*
     * Do the job.  The code says it all.
     */
    VBCPP This;
    vbcppInit(&This);
    bool fExit;
    RTEXITCODE rcExit = vbcppParseOptions(&This, argc, argv, &fExit);
    if (!fExit && rcExit == RTEXITCODE_SUCCESS)
    {
        rcExit = vbcppOpenStreams(&This);
        if (rcExit == RTEXITCODE_SUCCESS)
            rcExit = vbcppPreprocess(&This);
    }

    if (rcExit == RTEXITCODE_SUCCESS)
        rcExit = vbcppTerm(&This);
    else
        vbcppTerm(&This);
    return rcExit;
}

