/* $Id: VBoxCompilerPlugInsGcc.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * gccplugin - GCC plugin for checking IPRT format strings.
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
#include <stdio.h>
#include <iprt/cdefs.h>
#include <iprt/stdarg.h>

#if RT_GNUC_PREREQ(5, 1)
# include "gcc-plugin.h"
# include "plugin-version.h"
#endif
#if __GNUC__ == 4 && __GNUC_MINOR__ == 5
# include "gmp.h"
extern "C" {
#endif
#if __GNUC__ == 4 && __GNUC_MINOR__ == 5
# include "coretypes.h"
#endif
#include "plugin.h"
#include "basic-block.h"
#include "tree.h"
#include "tree-pass.h"
#include "tree-pretty-print.h"
#if __GNUC__ == 5 && __GNUC_MINOR__ == 4
# include "tree-ssa-alias.h"
# include "gimple-expr.h"
#endif
#include "gimple.h"
#if RT_GNUC_PREREQ(4, 9)
# include "gimple-iterator.h"
# include "context.h" /* for g */
#endif
#include "cp/cp-tree.h"
#if RT_GNUC_PREREQ(9, 4)
# include "stringpool.h"
# include "attribs.h"
#endif
#if __GNUC__ == 4 && __GNUC_MINOR__ == 5
}
#endif

#include "VBoxCompilerPlugIns.h"


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** License indicator. */
int plugin_is_GPL_compatible;


/*********************************************************************************************************************************
*   Defined Constants And Macros                                                                                                 *
*********************************************************************************************************************************/
/** Convencience macro not present in earlier gcc versions. */
#ifndef VAR_P
# define VAR_P(a_hNode) (TREE_CODE(a_hNode) == VAR_DECL)
#endif
/** Replacement for the 4.9.0 get_tree_code_name function. */
#if !RT_GNUC_PREREQ(4, 9)
# define get_tree_code_name(a_enmTreeCode) (tree_code_name[a_enmTreeCode])
#endif


/** For use with messages.
 * @todo needs some more work... Actually, seems we're a bit handicapped by
 *       working on gimplified stuff. */
#define MY_LOC(a_hPreferred, a_pState) EXPR_LOC_OR_LOC(a_hPreferred, (a_pState)->hFmtLoc)

/** @name Compatibility glue
 * @{ */
#if __GNUC__ == 4 && __GNUC_MINOR__ == 5
# define linemap_location_from_macro_expansion_p(a, b) false
#endif
#if __GNUC__ == 4 && __GNUC_MINOR__ == 5
static tree gimple_call_fntype(gimple hStmt)
{
    tree hDecl = gimple_call_fndecl(hStmt);
    if (hDecl)
        return TREE_TYPE(hDecl);
    hDecl = gimple_call_fn(hStmt);
    if (TREE_CODE(hDecl) == OBJ_TYPE_REF)
        hDecl = OBJ_TYPE_REF_EXPR(hDecl);
    if (DECL_P(hDecl))
    {
        tree hType = TREE_TYPE(hDecl);
        if (POINTER_TYPE_P(hType))
            hType = TREE_TYPE(hType);
        return hType;
    }
    return NULL_TREE; /* caller bitches about this*/
}
#endif

///* Integer to HOST_WIDE_INT conversion fun. */
//#if RT_GNUC_PREREQ(4, 6)
//# define MY_INT_FITS_SHWI(hNode)    (hNode).fits_shwi()
//# define MY_INT_TO_SHWI(hNode)      (hNode).to_shwi()
//#else
//# define MY_INT_FITS_SHWI(hNode)    double_int_fits_in_shwi_p(hNode)
//# define MY_INT_TO_SHWI(hNode)      double_int_to_shwi(hNode)
//#endif

/* Integer to HOST_WIDE_INT conversion fun. */
#if RT_GNUC_PREREQ(5, 1)
# define MY_DOUBLE_INT_FITS_SHWI(hNode)     tree_fits_shwi_p(hNode)
# define MY_DOUBLE_INT_TO_SHWI(hNode)       tree_to_shwi(hNode)
#elif RT_GNUC_PREREQ(4, 6)
# define MY_DOUBLE_INT_FITS_SHWI(hNode)     (TREE_INT_CST(hNode).fits_shwi())
# define MY_DOUBLE_INT_TO_SHWI(hNode)       (TREE_INT_CST(hNode).to_shwi())
#else
# define MY_DOUBLE_INT_FITS_SHWI(hNode)     double_int_fits_in_shwi_p(TREE_INT_CST(hNode))
# define MY_DOUBLE_INT_TO_SHWI(hNode)       double_int_to_shwi(TREE_INT_CST(hNode))
#endif

#ifndef EXPR_LOC_OR_LOC
# define EXPR_LOC_OR_LOC(a,b) (b)
#endif
/** @} */


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** gimple statement handle.   */
#if RT_GNUC_PREREQ(6, 0)
typedef const gimple *MY_GIMPLE_HSTMT_T;
#else
typedef gimple        MY_GIMPLE_HSTMT_T;
#endif


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static bool             MyPassGateCallback(void);
static unsigned int     MyPassExecuteCallback(void);
static unsigned int     MyPassExecuteCallbackWithFunction(struct function *pFun);
static tree             AttributeHandlerFormat(tree *, tree, tree, int, bool *);
static tree             AttributeHandlerCallReq(tree *, tree, tree, int, bool *);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Plug-in info. */
static const struct plugin_info g_PlugInInfo =
{
    version: "0.0.0-ALPHA",
    help   : "Implements the __iprt_format__ attribute for checking format strings and arguments."
};

#if RT_GNUC_PREREQ(4, 9)

/** My pass. */
static const pass_data g_MyPassData =
{
    type                    : GIMPLE_PASS,
    name                    : "*iprt-format-checks", /* asterisk = no dump */
# if RT_GNUC_PREREQ(9, 4)
    optinfo_flags           : OPTGROUP_NONE,
# else
    optinfo_flags           : 0,
# endif
    tv_id                   : TV_NONE,
    properties_required     : 0,
    properties_provided     : 0,
    properties_destroyed    : 0,
    todo_flags_start        : 0,
    todo_flags_finish       : 0,
};

class MyPass : public gimple_opt_pass
{
public:
    MyPass(gcc::context *pCtx) : gimple_opt_pass(g_MyPassData, pCtx)
    { }

    virtual bool gate(function *pFun)
    {
        NOREF(pFun);
        return MyPassGateCallback();
    }

    virtual unsigned int execute(function *pFun)
    {
        NOREF(pFun);
        return MyPassExecuteCallbackWithFunction(pFun);
    }
};

#else /* < 4.9.0 */

/** My pass. */
static struct gimple_opt_pass g_MyPass =
{
    pass:
    {
        type                    : GIMPLE_PASS,
        name                    : "*iprt-format-and-callreq-checks", /* asterisk = no dump */
# if RT_GNUC_PREREQ(4, 6)
        optinfo_flags           : 0,
# endif
        gate                    : MyPassGateCallback,
        execute                 : MyPassExecuteCallback,
        sub                     : NULL,
        next                    : NULL,
        static_pass_number      : 0,
        tv_id                   : TV_NONE,
        properties_required     : 0,
        properties_provided     : 0,
        properties_destroyed    : 0,
        todo_flags_start        : 0,
        todo_flags_finish       : 0,
    }
};

/** The registration info for my pass. */
static const struct register_pass_info  g_MyPassInfo =
{
    pass                        : &g_MyPass.pass,
    reference_pass_name         : "ssa",
    ref_pass_instance_number    : 1,
    pos_op                      : PASS_POS_INSERT_BEFORE,
};

#endif /* < 4.9.0 */


/** Attribute specifications. */
static const struct attribute_spec g_aAttribSpecs[] =
{
    {
        name                    : "iprt_format",
        min_length              : 2,
        max_length              : 2,
        decl_required           : false,
        type_required           : true,
        function_type_required  : true,
// gcc 6.3 at least moves this field to after "handler", and with 8.3 it is back
#if RT_GNUC_PREREQ(4, 6) && !(RT_GNUC_PREREQ(6, 3) && !RT_GNUC_PREREQ(8, 0))
        affects_type_identity   : false,
#endif
        handler                 : AttributeHandlerFormat,
#if RT_GNUC_PREREQ(6, 3) && !RT_GNUC_PREREQ(8, 0)
        affects_type_identity   : false,
#endif
#if RT_GNUC_PREREQ(8, 0)
        exclude                 : NULL,
#endif
    },
    {
        name                    : "iprt_format_maybe_null",
        min_length              : 2,
        max_length              : 2,
        decl_required           : false,
        type_required           : true,
        function_type_required  : true,
#if RT_GNUC_PREREQ(4, 6) && !(RT_GNUC_PREREQ(6, 3) && !RT_GNUC_PREREQ(8, 0))
        affects_type_identity   : false,
#endif
        handler                 : AttributeHandlerFormat,
#if RT_GNUC_PREREQ(6, 3) && !RT_GNUC_PREREQ(8, 0)
        affects_type_identity   : false,
#endif
#if RT_GNUC_PREREQ(8, 0)
        exclude                 : NULL,
#endif
    },
    {
        name                    : "iprt_callreq",
        min_length              : 3,
        max_length              : 3,
        decl_required           : false,
        type_required           : true,
        function_type_required  : true,
#if RT_GNUC_PREREQ(4, 6) && !(RT_GNUC_PREREQ(6, 3) && !RT_GNUC_PREREQ(8, 0))
        affects_type_identity   : false,
#endif
        handler                 : AttributeHandlerCallReq,
#if RT_GNUC_PREREQ(6, 3) && !RT_GNUC_PREREQ(8, 0)
        affects_type_identity   : false,
#endif
#if RT_GNUC_PREREQ(8, 0)
        exclude                 : NULL,
#endif
    }
};


#ifdef DEBUG

/**
 * Debug function for printing the scope of a decl.
 * @param   hDecl               Declaration to print scope for.
 */
static void dprintScope(tree hDecl)
{
# if 0 /* later? */
    tree hScope = CP_DECL_CONTEXT(hDecl);
    if (hScope == global_namespace)
        return;
    if (TREE_CODE(hScope) == RECORD_TYPE)
        hScope = TYPE_NAME(hScope);

    /* recurse */
    dprintScope(hScope);

    /* name the scope. */
    dprintf("::%s", DECL_NAME(hScope) ? IDENTIFIER_POINTER(DECL_NAME(hScope)) : "<noname>");
# endif
}


/**
 * Debug function for printing a declaration.
 * @param   hDecl               The declaration to print.
 */
static void dprintDecl(tree hDecl)
{
    enum tree_code const    enmDeclCode = TREE_CODE(hDecl);
    tree const              hType       = TREE_TYPE(hDecl);
    enum tree_code const    enmTypeCode = hType ? TREE_CODE(hType) : (enum tree_code)-1;
# if 0
    if (   enmTypeCode == RECORD_TYPE
        && enmDeclCode == TYPE_DECL
        && DECL_ARTIFICIAL(hDecl))
        dprint_class(hType);
# endif

    dprintf("%s ", get_tree_code_name(enmDeclCode));
    dprintScope(hDecl);
    dprintf("::%s", DECL_NAME(hDecl) ? IDENTIFIER_POINTER(DECL_NAME(hDecl)) : "<noname>");
    if (hType)
        dprintf(" type %s", get_tree_code_name(enmTypeCode));
    dprintf(" @%s:%d", DECL_SOURCE_FILE(hDecl),  DECL_SOURCE_LINE(hDecl));
}

#endif /* DEBUG */


static location_t MyGetLocationPlusColumnOffset(location_t hLoc, unsigned int offColumn)
{
    /*
     * Skip NOOPs, reserved locations and macro expansion.
     */
    if (   offColumn != 0
        && hLoc >= RESERVED_LOCATION_COUNT
        && !linemap_location_from_macro_expansion_p(line_table, hLoc))
    {
#if __GNUC__ >= 5 /** @todo figure this... */
        /*
         * There is an API for doing this, nice.
         */
        location_t hNewLoc = linemap_position_for_loc_and_offset(line_table, hLoc, offColumn);
        if (hNewLoc && hNewLoc != hLoc)
        {
            dprintf("MyGetLocationPlusColumnOffset: hNewLoc=%#x hLoc=%#x offColumn=%u\n", hNewLoc, hLoc, offColumn);
            return hNewLoc;
        }

#elif __GNUC_MINOR__ > 5
        /*
         * Have to do the job ourselves, it seems.  This is a bit hairy...
         */
        line_map const *pMap = NULL;
        location_t hLoc2 = linemap_resolve_location(line_table, hLoc, LRK_SPELLING_LOCATION, &pMap);
        if (hLoc2)
            hLoc = hLoc2;

        /* Guard against wrap arounds and overlaps. */
        if (   hLoc + offColumn > MAP_START_LOCATION(pMap) /** @todo Use MAX_SOURCE_LOCATION? */
            && (   pMap == LINEMAPS_LAST_ORDINARY_MAP(line_table)
                || hLoc + offColumn < MAP_START_LOCATION((pMap + 1))))
        {
            /* Calc new column and check that it's within the valid range. */
            unsigned int uColumn = SOURCE_COLUMN(pMap, hLoc) + offColumn;
            if (uColumn < RT_BIT_32(ORDINARY_MAP_NUMBER_OF_COLUMN_BITS(pMap)))
            {
                /* Try add the position.  If we get a valid result, replace the location. */
                source_location hNewLoc = linemap_position_for_line_and_column((line_map *)pMap, SOURCE_LINE(pMap, hLoc), uColumn);
                if (   hNewLoc <= line_table->highest_location
                    && linemap_lookup(line_table, hNewLoc) != NULL)
                {
                    dprintf("MyGetLocationPlusColumnOffset: hNewLoc=%#x hLoc=%#x offColumn=%u uColumn=%u\n",
                            hNewLoc, hLoc, offColumn, uColumn);
                    return hNewLoc;
                }
            }
        }
#endif
    }
    dprintf("MyGetLocationPlusColumnOffset: taking fallback\n");
    return hLoc;
}


#if 0
DECLINLINE(int) MyGetLineLength(const char *pszLine)
{
    if (pszLine)
    {
        const char *pszEol = strpbrk(pszLine, "\n\r");
        if (!pszEol)
            pszEol = strchr(pszLine, '\0');
        return (int)(pszEol - pszLine);
    }
    return 0;
}
#endif

static location_t MyGetFormatStringLocation(PVFMTCHKSTATE pState, const char *pszLoc)
{
    location_t hLoc = pState->hFmtLoc;
#if RT_GNUC_PREREQ(4,6)
    intptr_t   offString = pszLoc - pState->pszFmt;
    if (   offString >= 0
        && !linemap_location_from_macro_expansion_p(line_table, hLoc))
    {
        unsigned            uCol    = 1 + offString;
# if 0 /* apparently not needed */
        expanded_location   XLoc    = expand_location_to_spelling_point(hLoc);
# if RT_GNUC_PREREQ(10,0)
        char_span           Span    = location_get_source_line(XLoc.file, XLoc.line);
        const char         *pszLine = Span.m_ptr; /** @todo if enabled */
        int                 cchLine = (int)Span.m_n_elts;
# elif RT_GNUC_PREREQ(6,0)
        int                 cchLine = 0;
        const char         *pszLine = location_get_source_line(XLoc.file, XLoc.line, &cchLine);
# elif RT_GNUC_PREREQ(5,0)
        int                 cchLine = 0;
        const char         *pszLine = location_get_source_line(XLoc, &cchLine);
# else
        const char         *pszLine = location_get_source_line(XLoc);
        int                 cchLine = MyGetLineLength(pszLine);
# endif
        if (pszLine)
        {
            /** @todo Adjust the position by parsing the source. */
            pszLine += XLoc.column - 1;
            cchLine -= XLoc.column - 1;
        }
# endif

        hLoc = MyGetLocationPlusColumnOffset(hLoc, uCol);
    }
#endif
    return hLoc;
}


/**
 * Non-recursive worker for MyCheckFormatRecursive.
 *
 * This will attempt to result @a hFmtArg into a string literal which it then
 * passes on to MyCheckFormatString for the actual analyzis.
 *
 * @param   pState              The format string checking state.
 * @param   hFmtArg             The format string node.
 */
DECL_NO_INLINE(static, void) MyCheckFormatNonRecursive(PVFMTCHKSTATE pState, tree hFmtArg)
{
    dprintf("checker: hFmtArg=%p %s\n", hFmtArg, get_tree_code_name(TREE_CODE(hFmtArg)));

    /*
     * Try resolve variables into constant strings.
     */
    if (VAR_P(hFmtArg))
    {
        hFmtArg = decl_constant_value(hFmtArg);
        STRIP_NOPS(hFmtArg); /* Used as argument and assigned call result. */
        dprintf("checker1: variable => hFmtArg=%p %s\n", hFmtArg, get_tree_code_name(TREE_CODE(hFmtArg)));
    }

    /*
     * Fend off NULLs.
     */
    if (integer_zerop(hFmtArg))
    {
        if (pState->fMaybeNull)
            VFmtChkVerifyEndOfArgs(pState, 0);
        else
            error_at(MY_LOC(hFmtArg, pState), "Format string should not be NULL");
    }
    /*
     * Need address expression to get any further.
     */
    else if (TREE_CODE(hFmtArg) != ADDR_EXPR)
        dprintf("checker1: Not address expression (%s)\n", get_tree_code_name(TREE_CODE(hFmtArg)));
    else
    {
        pState->hFmtLoc = EXPR_LOC_OR_LOC(hFmtArg, pState->hFmtLoc);
        hFmtArg = TREE_OPERAND(hFmtArg, 0);

        /*
         * Deal with fixed string indexing, if possible.
         */
        HOST_WIDE_INT off = 0;
        if (   TREE_CODE(hFmtArg) == ARRAY_REF
            && MY_DOUBLE_INT_FITS_SHWI(TREE_OPERAND(hFmtArg, 1))
            && MY_DOUBLE_INT_FITS_SHWI(TREE_OPERAND(hFmtArg, 1)) )
        {
            off = MY_DOUBLE_INT_TO_SHWI(TREE_OPERAND(hFmtArg, 1));
            if (off < 0)
            {
                dprintf("checker1: ARRAY_REF, off=%ld\n", off);
                return;
            }
            hFmtArg = TREE_OPERAND(hFmtArg, 0);
            dprintf("checker1: ARRAY_REF => hFmtArg=%p %s, off=%ld\n", hFmtArg, get_tree_code_name(TREE_CODE(hFmtArg)), off);
        }

        /*
         * Deal with static const char g_szFmt[] = "qwerty";  Take care as
         * the actual string constant may not necessarily include the terminator.
         */
        tree hArraySize = NULL_TREE;
        if (   VAR_P(hFmtArg)
            && TREE_CODE(TREE_TYPE(hFmtArg)) == ARRAY_TYPE)
        {
            tree hArrayInitializer = decl_constant_value(hFmtArg);
            if (   hArrayInitializer != hFmtArg
                && TREE_CODE(hArrayInitializer) == STRING_CST)
            {
                hArraySize = DECL_SIZE_UNIT(hFmtArg);
                hFmtArg = hArrayInitializer;
            }
        }

        /*
         * Are we dealing with a string literal now?
         */
        if (TREE_CODE(hFmtArg) != STRING_CST)
            dprintf("checker1: Not string literal (%s)\n", get_tree_code_name(TREE_CODE(hFmtArg)));
        else if (TYPE_MAIN_VARIANT(TREE_TYPE(TREE_TYPE(hFmtArg))) != char_type_node)
            warning_at(pState->hFmtLoc, 0, "expected 'char' type string literal");
        else
        {
            /*
             * Yes we are, so get the pointer to the string and its length.
             */
            const char *pszFmt = TREE_STRING_POINTER(hFmtArg);
            int         cchFmt = TREE_STRING_LENGTH(hFmtArg);

            /* Adjust cchFmt to the initialized array size if appropriate. */
            if (hArraySize != NULL_TREE)
            {
                if (TREE_CODE(hArraySize) != INTEGER_CST)
                    warning_at(pState->hFmtLoc, 0, "Expected integer array size (not %s)", get_tree_code_name(TREE_CODE(hArraySize)));
                else if (!MY_DOUBLE_INT_FITS_SHWI(hArraySize))
                    warning_at(pState->hFmtLoc, 0, "Unexpected integer overflow in array size constant");
                else
                {
                    HOST_WIDE_INT cbArray = MY_DOUBLE_INT_TO_SHWI(hArraySize);
                    if (   cbArray <= 0
                        || cbArray != (int)cbArray)
                        warning_at(pState->hFmtLoc, 0, "Unexpected integer array size constant value: %ld", cbArray);
                    else if (cchFmt > cbArray)
                    {
                        dprintf("checker1: cchFmt=%d => cchFmt=%ld (=cbArray)\n", cchFmt, cbArray);
                        cchFmt = (int)cbArray;
                    }
                }
            }

            /* Apply the offset, if given. */
            if (off)
            {
                if (off >= cchFmt)
                {
                    dprintf("checker1: off=%ld  >=  cchFmt=%d -> skipping\n", off, cchFmt);
                    return;
                }
                pszFmt += off;
                cchFmt -= (int)off;
            }

            /*
             * Check for unterminated strings.
             */
            if (   cchFmt < 1
                || pszFmt[cchFmt - 1] != '\0')
                warning_at(pState->hFmtLoc, 0, "Unterminated format string (cchFmt=%d)", cchFmt);
            /*
             * Call worker to check the actual string.
             */
            else
                MyCheckFormatCString(pState, pszFmt);
        }
    }
}


/**
 * Deal recursively with special format string constructs.
 *
 * This will call MyCheckFormatNonRecursive to validate each format string.
 *
 * @param   pState              The format string checking state.
 * @param   hFmtArg             The format string node.
 */
static void MyCheckFormatRecursive(PVFMTCHKSTATE pState, tree hFmtArg)
{
    /*
     * Catch wrong attribute use.
     */
    if (hFmtArg == NULL_TREE)
        error_at(pState->hFmtLoc, "IPRT format attribute is probably used incorrectly (hFmtArg is NULL)");
    /*
     * NULL format strings may cause crashes.
     */
    else if (integer_zerop(hFmtArg))
    {
        if (pState->fMaybeNull)
            VFmtChkVerifyEndOfArgs(pState, 0);
        else
            error_at(MY_LOC(hFmtArg, pState), "Format string should not be NULL");
    }
    /*
     * Check both branches of a ternary operator.
     */
    else if (TREE_CODE(hFmtArg) == COND_EXPR)
    {
        MyCheckFormatRecursive(pState, TREE_OPERAND(hFmtArg, 1));
        MyCheckFormatRecursive(pState, TREE_OPERAND(hFmtArg, 2));
    }
    /*
     * Strip coercion.
     */
    else if (   CONVERT_EXPR_P(hFmtArg)
             && TYPE_PRECISION(TREE_TYPE(hFmtArg)) == TYPE_PRECISION(TREE_TYPE(TREE_OPERAND(hFmtArg, 0))) )
        MyCheckFormatRecursive(pState, TREE_OPERAND(hFmtArg, 0));
    /*
     * We're good, hand it to the non-recursive worker.
     */
    else
        MyCheckFormatNonRecursive(pState, hFmtArg);
}


/**
 * Check VMR3ReqCallU stuff.
 *
 * @param   pState              The checking state.
 */
static void MyCheckCallReq(MY_GIMPLE_HSTMT_T const hStmt, location_t const hStmtLoc,
                           long const idxFn, long const idxArgc, long const idxArgs, unsigned const cCallArgs)
{
#ifdef DEBUG
    expanded_location const XLoc = expand_location_to_spelling_point(hStmtLoc);
    dprintf("checker-call: cCallArgs=%#lx - %s:%u\n", cCallArgs, XLoc.file ? XLoc.file : "<nofile>", XLoc.line);
#endif

    /*
     * Catch incorrect attribute use.
     */
    if (idxFn > cCallArgs)
    {
        error_at(hStmtLoc, "Incorrect function argument number (first param) in IPRT call attribute: %lu (max %u)",
                 idxFn, cCallArgs);
        return;
    }
    if (idxArgc > cCallArgs)
    {
        error_at(hStmtLoc, "Incorrect argument count argument number (second param) in IPRT call attribute: %lu (max %u)",
                 idxArgc, cCallArgs);
        return;
    }
    if (idxArgs > cCallArgs + 1)
    {
        error_at(hStmtLoc, "Incorrect arguments argument number (last param) in IPRT call attribute: %lu (max %u)",
                 idxArgs, cCallArgs + 1);
        return;
    }

    /*
     * Get the argument count argument and check how it compares to cCallArgs if constant.
     */
    tree hArgCount = gimple_call_arg(hStmt, idxArgc - 1);
    if (!hArgCount)
    {
        error_at(hStmtLoc, "Failed to get the parameter counter argument (%lu)", idxArgc);
        return;
    }
    HOST_WIDE_INT cArgsValue = -1;
    if (TREE_CODE(hArgCount) == INTEGER_CST)
    {
        if (!MY_DOUBLE_INT_FITS_SHWI(hArgCount))
            error_at(hStmtLoc, "Unexpected integer overflow in argument count constant");
        else
        {
            cArgsValue = MY_DOUBLE_INT_TO_SHWI(hArgCount);
            if (   cArgsValue < 0
                || cArgsValue != (int)cArgsValue)
                error_at(hStmtLoc, "Unexpected integer argument count constant value: %ld", cArgsValue);
            else
            {
                /* HARD CODED MAX and constants */
                dprintf("checker-call: cArgsValue=%#lx\n", cArgsValue);
                if (   cArgsValue <= 9
                    || ((cArgsValue & 0x8000) && ((unsigned)cArgsValue & ~(unsigned)0x8000) <= 15) )
                { /* likely */ }
                else
                    error_at(hStmtLoc, "Bad integer argument count constant value: %lx (max is 9, or 15 with flag)",
                             cArgsValue);

                cArgsValue = (unsigned)cArgsValue & ~(unsigned)0x8000;
                if (idxArgs != 0 && idxArgs + cArgsValue - 1 != (long)cCallArgs)
                    error_at(hStmtLoc, "Argument count %ld starting at %ld (1-based) does not match argument count: %u",
                             cArgsValue, idxArgs, cCallArgs);
            }
        }
    }

    /*
     * Get the function pointer statement and strip any coercion.
     */
    tree const hFnArg = gimple_call_arg(hStmt, idxFn - 1);
    if (!hFnArg)
    {
        error_at(hStmtLoc, "Failed to get function argument (%lu)", idxFn);
        return;
    }
    tree hFnType = 0;
    if (TREE_CODE(hFnArg) == SSA_NAME)
    {
        dprintf("  * hFnArg=%p %s(%d) %s (SSA)\n", hFnArg, get_tree_code_name(TREE_CODE(hFnArg)), TREE_CODE(hFnArg),
                SSA_NAME_IDENTIFIER(hFnArg) ? IDENTIFIER_POINTER(SSA_NAME_IDENTIFIER(hFnArg)) : "<unnamed-ssa-name>");
        hFnType = TREE_TYPE(hFnArg);
        if (hFnType && TREE_CODE(hFnType) != POINTER_TYPE)
        {
            error_at(hStmtLoc, "Expected pointer_type for hFnType=%p, not %s(%d)",
                     hFnType, get_tree_code_name(TREE_CODE(hFnType)), TREE_CODE(hFnType));
            return;
        }
        if (hFnType)
            hFnType = TREE_TYPE(hFnType);
    }
    else
    {
        dprintf("  * hFnArg=%p %s(%d)\n", hFnArg, get_tree_code_name(TREE_CODE(hFnArg)), TREE_CODE(hFnArg));
        tree const hFnDecl = gimple_call_addr_fndecl(hFnArg);
        if (hFnDecl)
        {
#ifdef DEBUG
            dprintDecl(hFnDecl);
            dprintf("\n");
#endif
            hFnType = TREE_TYPE(hFnDecl);
        }
    }
    if (hFnType)
    {
        dprintf("  * hFnType=%p %s(%d)\n", hFnType, get_tree_code_name(TREE_CODE(hFnType)), TREE_CODE(hFnType));
        if (TREE_CODE(hFnType) != FUNCTION_TYPE)
        {
            error_at(hStmtLoc, "Expected function_type for hFnType=%p, not %s(%d)",
                     hFnType, get_tree_code_name(TREE_CODE(hFnType)), TREE_CODE(hFnType));
            return;
        }

        /* Count the arguments. */
        unsigned   cFnTypeArgs = 0;
        tree const hArgList = TYPE_ARG_TYPES(hFnType);
        dprintf("  * hArgList=%p %s(%d)\n", hFnType, get_tree_code_name(TREE_CODE(hArgList)), TREE_CODE(hArgList));
        if (!hArgList)
            error_at(hStmtLoc, "Using unprototyped function (arg %lu)?", idxFn);
        else if (hArgList != void_list_node)
            for (tree hArg = hArgList; hArg && hArg != void_list_node; hArg = TREE_CHAIN(hArg))
                cFnTypeArgs++;

        /* Check that it matches the argument value. */
        if ((long)cFnTypeArgs != cArgsValue && cArgsValue >= 0)
            error_at(hStmtLoc, "Function prototype takes %u arguments, but %u specified in the call request",
                     cFnTypeArgs, (unsigned)cArgsValue);

        /*
         * Check that there are no oversized arguments in the function type.
         * If there are more than 9 arguments, also check the size of the lasts
         * ones, up to but not including the last argument.
         */
        HOST_WIDE_INT const cPtrBits = MY_DOUBLE_INT_TO_SHWI(TYPE_SIZE(TREE_TYPE(null_pointer_node)));
        HOST_WIDE_INT const cIntBits = MY_DOUBLE_INT_TO_SHWI(TYPE_SIZE(integer_type_node));
        unsigned  iArg = 0;
        for (tree hArg = hArgList; hArg && hArg != void_list_node; hArg = TREE_CHAIN(hArg), iArg++)
        {
            tree const          hArgType = TREE_VALUE(hArg);
            HOST_WIDE_INT const cArgBits = MY_DOUBLE_INT_TO_SHWI(TYPE_SIZE(hArgType));
            if (cArgBits > cPtrBits)
                error_at(hStmtLoc, "Argument #%u (1-based) is too large: %lu bits, max %lu", iArg + 1, cArgBits, cPtrBits);
            if (cArgBits != cPtrBits && iArg >= 8 && iArg != cFnTypeArgs - 1)
                error_at(hStmtLoc, "Argument #%u (1-based) must be pointer sized: %lu bits, expect %lu",
                         iArg + 1, cArgBits, cPtrBits);

            if (idxArgs > 0)
            {
                tree const hArgPassed     = gimple_call_arg(hStmt, idxArgs - 1 + iArg);
                tree       hArgPassedType = TREE_TYPE(hArgPassed);

                HOST_WIDE_INT const cArgPassedBits = MY_DOUBLE_INT_TO_SHWI(TYPE_SIZE(hArgPassedType));
                if (   cArgPassedBits != cArgBits
                    && (   cArgPassedBits != cIntBits /* expected promotion target is INT */
                        || cArgBits       >  cIntBits /* no promotion required */ ))
                    error_at(hStmtLoc,
                             "Argument #%u (1-based) passed to call request differs in size from target type: %lu bits, expect %lu",
                             iArg + 1, cArgPassedBits, cArgBits);
            }
        }

        /** @todo check that the arguments passed in the VMR3ReqXxxx call matches.   */
    }
    else if (idxArgs != 0)
        error_at(hStmtLoc, "Failed to get function declaration (%lu) for non-va_list call", idxFn);
}


#if !RT_GNUC_PREREQ(4, 9)
/**
 * Execute my pass.
 * @returns Flags indicates stuff todo, we return 0.
 */
static unsigned int     MyPassExecuteCallback(void)
{
    return MyPassExecuteCallbackWithFunction(cfun);
}
#endif

/**
 * Execute my pass.
 * @returns Flags indicates stuff todo, we return 0.
 */
static unsigned int     MyPassExecuteCallbackWithFunction(struct function *pFun)
{
    dprintf("MyPassExecuteCallback:\n");

    /*
     * Enumerate the basic blocks.
     */
    basic_block hBasicBlock;
    FOR_EACH_BB_FN(hBasicBlock, pFun)
    {
        dprintf(" hBasicBlock=%p\n", hBasicBlock);

        /*
         * Enumerate the statements in the current basic block.
         *
         * We're interested in calls to functions with the __iprt_format__,
         * iprt_format_maybe_null and __iprt_callreq__ attributes.
         */
        for (gimple_stmt_iterator hStmtItr = gsi_start_bb(hBasicBlock); !gsi_end_p(hStmtItr); gsi_next(&hStmtItr))
        {
#if RT_GNUC_PREREQ(6, 0)
            const gimple * const    hStmt   = gsi_stmt(hStmtItr);
#else
            gimple const            hStmt   = gsi_stmt(hStmtItr);
#endif

            enum gimple_code const  enmCode = gimple_code(hStmt);
#ifdef DEBUG
            unsigned const          cOps    = gimple_num_ops(hStmt);
            dprintf("   hStmt=%p %s (%d) ops=%d\n", hStmt, gimple_code_name[enmCode], enmCode, cOps);
            for (unsigned iOp = 0; iOp < cOps; iOp++)
            {
                tree const hOp = gimple_op(hStmt, iOp);
                if (hOp)
                    dprintf("     %02d: %p, code %s(%d)\n", iOp, hOp, get_tree_code_name(TREE_CODE(hOp)), TREE_CODE(hOp));
                else
                    dprintf("     %02d: NULL_TREE\n", iOp);
            }
#endif
            if (enmCode == GIMPLE_CALL)
            {
                /*
                 * Check if the function type has the __iprt_format__ attribute.
                 */
                tree const hFn = gimple_call_fn(hStmt);
                dprintf("     hFn    =%p %s(%d); args=%d\n",
                        hFn, hFn ? get_tree_code_name(TREE_CODE(hFn)) : NULL, hFn ? TREE_CODE(hFn) : - 1,
                        gimple_call_num_args(hStmt));
#ifdef DEBUG
                if (hFn && DECL_P(hFn))
                    dprintf("     hFn is decl: %s %s:%d\n",
                            DECL_NAME(hFn) ? IDENTIFIER_POINTER(DECL_NAME(hFn)) : "<unamed>",
                            DECL_SOURCE_FILE(hFn), DECL_SOURCE_LINE(hFn));
#endif
                tree const hFnDecl = gimple_call_fndecl(hStmt);
                if (hFnDecl)
                    dprintf("     hFnDecl=%p %s(%d) %s type=%p %s:%d\n",
                            hFnDecl, get_tree_code_name(TREE_CODE(hFnDecl)), TREE_CODE(hFnDecl),
                            DECL_NAME(hFnDecl) ? IDENTIFIER_POINTER(DECL_NAME(hFnDecl)) : "<unamed>",
                            TREE_TYPE(hFnDecl), DECL_SOURCE_FILE(hFnDecl), DECL_SOURCE_LINE(hFnDecl));
                tree const hFnType = gimple_call_fntype(hStmt);
                if (hFnType == NULL_TREE)
                {
                    if (   hFnDecl == NULL_TREE
                        && gimple_call_internal_p(hStmt) /* va_arg() kludge */)
                        continue;
                    error_at(gimple_location(hStmt), "Failed to resolve function type [fn=%s fndecl=%s]",
                             hFn ? get_tree_code_name(TREE_CODE(hFn)) : "<null>",
                             hFnDecl ? get_tree_code_name(TREE_CODE(hFnDecl)) : "<null>");
                }
                else if (POINTER_TYPE_P(hFnType))
                    error_at(gimple_location(hStmt), "Got a POINTER_TYPE when expecting a function type [fn=%s]",
                             get_tree_code_name(TREE_CODE(hFn)));
                if (hFnType)
                    dprintf("     hFnType=%p %s(%d) %s\n", hFnType, get_tree_code_name(TREE_CODE(hFnType)), TREE_CODE(hFnType),
                              TYPE_NAME(hFnType) && DECL_NAME(TYPE_NAME(hFnType))
                            ? IDENTIFIER_POINTER(DECL_NAME(TYPE_NAME(hFnType))) : "<unamed>");

                tree const hAttrCall      = hFnType ? lookup_attribute("iprt_callreq", TYPE_ATTRIBUTES(hFnType)) : NULL_TREE;
                tree const hAttrFmt       = hFnType ? lookup_attribute("iprt_format",  TYPE_ATTRIBUTES(hFnType)) : NULL_TREE;
                tree const hAttrFmtMaybe0 = hFnType ? lookup_attribute("iprt_format_maybe_null", TYPE_ATTRIBUTES(hFnType)) : NULL_TREE;
                if (hAttrCall || hAttrFmt || hAttrFmtMaybe0)
                {
                    /*
                     * Yeah, it has one of the attributes!
                     */
                    unsigned const cCallArgs = gimple_call_num_args(hStmt);
                    tree           hAttrArgs;
                    if (hAttrCall)
                    {
                        hAttrArgs          = TREE_VALUE(hAttrCall);
                        long const idxFn   = MY_DOUBLE_INT_TO_SHWI(TREE_VALUE(hAttrArgs));
                        long const idxArgc = MY_DOUBLE_INT_TO_SHWI(TREE_VALUE(TREE_CHAIN(hAttrArgs)));
                        long const idxArgs  = MY_DOUBLE_INT_TO_SHWI(TREE_VALUE(TREE_CHAIN(TREE_CHAIN(hAttrArgs))));
                        dprintf("     %s() __iprt_callreq__(idxFn=%ld, idxArgc=%ld, idxArgs=%ld)\n",
                                hFnDecl && DECL_NAME(hFnDecl) ? IDENTIFIER_POINTER(DECL_NAME(hFnDecl)) : "<unamed>",
                                idxFn, idxArgc, idxArgs);
                        MyCheckCallReq(hStmt, gimple_location(hStmt), idxFn, idxArgc, idxArgs, cCallArgs);
                    }
                    else
                    {
                        hAttrArgs           = hAttrFmt ? TREE_VALUE(hAttrFmt) : TREE_VALUE(hAttrFmtMaybe0);
                        VFMTCHKSTATE State;
                        State.hStmt         = hStmt;
                        State.hFmtLoc       = gimple_location(hStmt);
                        State.fMaybeNull    = hAttrFmt ? false : true;
                        State.iFmt          = MY_DOUBLE_INT_TO_SHWI(TREE_VALUE(hAttrArgs));
                        State.iArgs         = MY_DOUBLE_INT_TO_SHWI(TREE_VALUE(TREE_CHAIN(hAttrArgs)));
                        State.pszFmt        = NULL;
                        dprintf("     %s() __iprt_format%s__(iFmt=%ld, iArgs=%ld)\n",
                                hFnDecl && DECL_NAME(hFnDecl) ? IDENTIFIER_POINTER(DECL_NAME(hFnDecl)) : "<unamed>",
                                State.fMaybeNull ? "_maybe_null" : "", State.iFmt, State.iArgs);
                        if (cCallArgs >= State.iFmt)
                            MyCheckFormatRecursive(&State, gimple_call_arg(hStmt, State.iFmt - 1));
                        else
                            error_at(gimple_location(hStmt),
                                     "Call has only %d arguments; %s() format string is argument #%lu (1-based), thus missing",
                                     cCallArgs, DECL_NAME(hFnDecl) ? IDENTIFIER_POINTER(DECL_NAME(hFnDecl)) : "<unamed>",
                                     State.iFmt);
                    }
                }
            }
        }
    }
    return 0;
}


/**
 * Gate callback for my pass that indicates whether it should execute or not.
 * @returns true to execute.
 */
static bool             MyPassGateCallback(void)
{
    dprintf("MyPassGateCallback:\n");
    return true;
}


/**
 * Validate the use of an format attribute.
 *
 * @returns ??
 * @param   phOnNode        The node the attribute is being used on.
 * @param   hAttrName       The attribute name.
 * @param   hAttrArgs       The attribute arguments.
 * @param   fFlags          Some kind of flags...
 * @param   pfDontAddAttrib Whether to add the attribute to this node or not.
 */
static tree AttributeHandlerFormat(tree *phOnNode, tree hAttrName, tree hAttrArgs, int fFlags, bool *pfDontAddAttrib)
{
    dprintf("AttributeHandlerFormat: name=%s fFlags=%#x", IDENTIFIER_POINTER(hAttrName), fFlags);
    long iFmt  = MY_DOUBLE_INT_TO_SHWI(TREE_VALUE(hAttrArgs));
    long iArgs = MY_DOUBLE_INT_TO_SHWI(TREE_VALUE(TREE_CHAIN(hAttrArgs)));
    dprintf(" iFmt=%ld iArgs=%ld", iFmt, iArgs);

    tree hType = *phOnNode;
    dprintf(" hType=%p %s(%d)\n", hType, get_tree_code_name(TREE_CODE(hType)), TREE_CODE(hType));

    if (pfDontAddAttrib)
        *pfDontAddAttrib = false;
    return NULL_TREE;
}


/**
 * Validate the use of an call request attribute.
 *
 * @returns ??
 * @param   phOnNode        The node the attribute is being used on.
 * @param   hAttrName       The attribute name.
 * @param   hAttrArgs       The attribute arguments.
 * @param   fFlags          Some kind of flags...
 * @param   pfDontAddAttrib Whether to add the attribute to this node or not.
 */
static tree AttributeHandlerCallReq(tree *phOnNode, tree hAttrName, tree hAttrArgs, int fFlags, bool *pfDontAddAttrib)
{
    dprintf("AttributeHandlerCallReq: name=%s fFlags=%#x", IDENTIFIER_POINTER(hAttrName), fFlags);
    long iFn   = MY_DOUBLE_INT_TO_SHWI(TREE_VALUE(hAttrArgs));
    long iArgc = MY_DOUBLE_INT_TO_SHWI(TREE_VALUE(TREE_CHAIN(hAttrArgs)));
    long iArgs = MY_DOUBLE_INT_TO_SHWI(TREE_VALUE(TREE_CHAIN(TREE_CHAIN(hAttrArgs))));
    dprintf(" iFn=%ld iArgc=%ld iArgs=%ld", iFn, iArgc, iArgs);

    tree hType = *phOnNode;
    dprintf(" hType=%p %s(%d)\n", hType, get_tree_code_name(TREE_CODE(hType)), TREE_CODE(hType));

    if (pfDontAddAttrib)
        *pfDontAddAttrib = false;
    return NULL_TREE;
}


/**
 * Called when we can register attributes.
 *
 * @param   pvEventData         Ignored.
 * @param   pvUser              Ignored.
 */
static void RegisterAttributesEvent(void *pvEventData, void *pvUser)
{
    NOREF(pvEventData); NOREF(pvUser);
    dprintf("RegisterAttributesEvent: pvEventData=%p\n", pvEventData);

    for (unsigned i = 0; i < RT_ELEMENTS(g_aAttribSpecs); i++)
        register_attribute(&g_aAttribSpecs[i]);
}


/**
 * The plug-in entry point.
 *
 * @returns 0 to indicate success?
 * @param   pPlugInInfo         Plugin info structure.
 * @param   pGccVer             GCC Version.
 */
int plugin_init(plugin_name_args *pPlugInInfo, plugin_gcc_version *pGccVer)
{
    dprintf("plugin_init: %s\n", pPlugInInfo->full_name);
    dprintf("gcc version: basever=%s datestamp=%s devphase=%s revision=%s\n",
            pGccVer->basever, pGccVer->datestamp, pGccVer->devphase, pGccVer->revision);

    /* Ask for callback in which we may register the attribute. */
    register_callback(pPlugInInfo->base_name, PLUGIN_ATTRIBUTES, RegisterAttributesEvent, NULL /*pvUser*/);

    /* Register our pass. */
#if RT_GNUC_PREREQ(4, 9)
    /** The registration info for my pass. */
    struct register_pass_info MyPassInfo;
    MyPassInfo.pass                     = new MyPass(g);
    MyPassInfo.reference_pass_name      = "ssa";
    MyPassInfo.ref_pass_instance_number = 1;
    MyPassInfo.pos_op                   = PASS_POS_INSERT_BEFORE;
    register_callback(pPlugInInfo->base_name, PLUGIN_PASS_MANAGER_SETUP, NULL, &MyPassInfo);
#else
    register_callback(pPlugInInfo->base_name, PLUGIN_PASS_MANAGER_SETUP, NULL, (void *)&g_MyPassInfo);
#endif

    /* Register plug-in info. */
    register_callback(pPlugInInfo->base_name, PLUGIN_INFO, NULL, (void *)&g_PlugInInfo);

    return 0;
}




/*
 *
 * Functions used by the common code.
 * Functions used by the common code.
 * Functions used by the common code.
 *
 */

void VFmtChkWarnFmt(PVFMTCHKSTATE pState, const char *pszLoc, const char *pszFormat, ...)
{
    char szTmp[1024];
    va_list va;
    va_start(va, pszFormat);
    vsnprintf(szTmp, sizeof(szTmp), pszFormat, va);
    va_end(va);

    /* display the warning. */
    warning_at(MyGetFormatStringLocation(pState, pszLoc), 0, "%s", szTmp);
}


void VFmtChkErrFmt(PVFMTCHKSTATE pState, const char *pszLoc, const char *pszFormat, ...)
{
    char szTmp[1024];
    va_list va;
    va_start(va, pszFormat);
    vsnprintf(szTmp, sizeof(szTmp), pszFormat, va);
    va_end(va);

    /* display the warning. */
    error_at(MyGetFormatStringLocation(pState, pszLoc), "%s", szTmp);
}



void VFmtChkVerifyEndOfArgs(PVFMTCHKSTATE pState, unsigned iArg)
{
    dprintf("VFmtChkVerifyEndOfArgs: iArg=%u iArgs=%ld cArgs=%u\n", iArg, pState->iArgs, gimple_call_num_args(pState->hStmt));
    if (pState->iArgs > 0)
    {
        iArg += pState->iArgs - 1;
        unsigned cArgs = gimple_call_num_args(pState->hStmt);
        if (iArg == cArgs)
        { /* fine */ }
        else if (iArg < cArgs)
        {
            tree hArg = gimple_call_arg(pState->hStmt, iArg);
            if (cArgs - iArg > 1)
                error_at(MY_LOC(hArg, pState), "%u extra arguments not consumed by format string", cArgs - iArg);
            else if (   TREE_CODE(hArg) != INTEGER_CST
                     || !MY_DOUBLE_INT_FITS_SHWI(hArg)
                     || MY_DOUBLE_INT_TO_SHWI(hArg) != -99) /* ignore final dummy argument: ..., -99); */
                error_at(MY_LOC(hArg, pState), "one extra argument not consumed by format string");
        }
        /* This should be handled elsewhere, but just in case. */
        else if (iArg - 1 == cArgs)
            error_at(pState->hFmtLoc, "one argument too few");
        else
            error_at(pState->hFmtLoc, "%u arguments too few", iArg - cArgs);
    }
}


bool VFmtChkRequirePresentArg(PVFMTCHKSTATE pState, const char *pszLoc, unsigned iArg, const char *pszMessage)
{
    if (pState->iArgs > 0)
    {
        iArg += pState->iArgs - 1;
        unsigned cArgs = gimple_call_num_args(pState->hStmt);
        if (iArg >= cArgs)
        {
            VFmtChkErrFmt(pState, pszLoc, "Missing argument! %s", pszMessage);
            return false;
        }

        tree hArg  = gimple_call_arg(pState->hStmt, iArg);
        tree hType = TREE_TYPE(hArg);
        dprintf("arg%u: hArg=%p [%s] hType=%p [%s] cls=%s\n", iArg, hArg, get_tree_code_name(TREE_CODE(hArg)),
                hType, get_tree_code_name(TREE_CODE(hType)), tree_code_class_strings[TREE_CODE_CLASS(TREE_CODE(hType))]);
        dprintf("      nm=%p\n", TYPE_NAME(hType));
        dprintf("      cBits=%p %s value=%ld\n", TYPE_SIZE(hType), get_tree_code_name(TREE_CODE(TYPE_SIZE(hType))),
                MY_DOUBLE_INT_TO_SHWI(TYPE_SIZE(hType)) );
        dprintf("      unit=%p %s value=%ld\n", TYPE_SIZE_UNIT(hType), get_tree_code_name(TREE_CODE(TYPE_SIZE_UNIT(hType))),
                MY_DOUBLE_INT_TO_SHWI(TYPE_SIZE_UNIT(hType)) );
        tree hTypeNm = TYPE_NAME(hType);
        if (hTypeNm)
            dprintf("      typenm=%p %s '%s'\n", hTypeNm, get_tree_code_name(TREE_CODE(hTypeNm)),
                    IDENTIFIER_POINTER(DECL_NAME(hTypeNm)));
    }
    return true;
}


bool VFmtChkRequireIntArg(PVFMTCHKSTATE pState, const char *pszLoc, unsigned iArg, const char *pszMessage)
{
    if (VFmtChkRequirePresentArg(pState, pszLoc, iArg, pszMessage))
    {
        /** @todo type check.  */
        return true;
    }
    return false;
}


bool VFmtChkRequireStringArg(PVFMTCHKSTATE pState, const char *pszLoc, unsigned iArg, const char *pszMessage)
{
    if (VFmtChkRequirePresentArg(pState, pszLoc, iArg, pszMessage))
    {
        /** @todo type check.  */
        return true;
    }
    return false;
}


bool VFmtChkRequireVaListPtrArg(PVFMTCHKSTATE pState, const char *pszLoc, unsigned iArg, const char *pszMessage)
{
    if (VFmtChkRequirePresentArg(pState, pszLoc, iArg, pszMessage))
    {
        /** @todo type check.  */
        return true;
    }
    return false;
}


void VFmtChkHandleReplacementFormatString(PVFMTCHKSTATE pState, const char *pszPctM, unsigned iArg)
{
    if (pState->iArgs > 0)
    {
        pState->iFmt        = pState->iArgs + iArg;
        pState->iArgs       = pState->iFmt  + 1;
        pState->fMaybeNull  = false;
        MyCheckFormatRecursive(pState, gimple_call_arg(pState->hStmt, pState->iFmt - 1));
    }
}


const char  *VFmtChkGetFmtLocFile(PVFMTCHKSTATE pState)
{
    return LOCATION_FILE(pState->hFmtLoc);
}


unsigned int VFmtChkGetFmtLocLine(PVFMTCHKSTATE pState)
{
    return LOCATION_LINE(pState->hFmtLoc);
}


unsigned int VFmtChkGetFmtLocColumn(PVFMTCHKSTATE pState)
{
#ifdef LOCATION_COLUMN
    return LOCATION_COLUMN(pState->hFmtLoc);
#else
    return 1;
#endif
}

