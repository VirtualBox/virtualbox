/* $Id: tstDBGCParser.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGC Testcase - Command Parser.
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
#include <VBox/dbg.h>
#include "../DBGCInternal.h"

#include <iprt/string.h>
#include <iprt/test.h>
#include <VBox/err.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(bool) tstDBGCBackInput(PCDBGCIO pBack, uint32_t cMillies);
static DECLCALLBACK(int)  tstDBGCBackRead(PCDBGCIO pBack, void *pvBuf, size_t cbBuf, size_t *pcbRead);
static DECLCALLBACK(int)  tstDBGCBackWrite(PCDBGCIO pBack, const void *pvBuf, size_t cbBuf, size_t *pcbWritten);
static DECLCALLBACK(void) tstDBGCBackSetReady(PCDBGCIO pBack, bool fReady);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** The test handle. */
static RTTEST g_hTest = NIL_RTTEST;

/** The DBGC I/O structure for use in this testcase. */
static DBGCIO g_tstBack =
{
    NULL, /**pfnDestroy*/
    tstDBGCBackInput,
    tstDBGCBackRead,
    tstDBGCBackWrite,
    NULL, /**pfnPktBegin*/
    NULL, /**pfnPktEnd*/
    tstDBGCBackSetReady
};
/** For keeping track of output prefixing. */
static bool     g_fPendingPrefix = true;
/** Pointer to the current input position. */
const char     *g_pszInput = NULL;
/** The output of the last command. */
static char     g_szOutput[1024];
/** The current offset into g_szOutput. */
static size_t   g_offOutput = 0;


/**
 * Checks if there is input.
 *
 * @returns true if there is input ready.
 * @returns false if there not input ready.
 * @param   pBack       Pointer to the backend structure supplied by
 *                      the backend. The backend can use this to find
 *                      it's instance data.
 * @param   cMillies    Number of milliseconds to wait on input data.
 */
static DECLCALLBACK(bool) tstDBGCBackInput(PCDBGCIO pBack, uint32_t cMillies)
{
    return g_pszInput != NULL
       && *g_pszInput != '\0';
}


/**
 * Read input.
 *
 * @returns VBox status code.
 * @param   pBack       Pointer to the backend structure supplied by
 *                      the backend. The backend can use this to find
 *                      it's instance data.
 * @param   pvBuf       Where to put the bytes we read.
 * @param   cbBuf       Maximum nymber of bytes to read.
 * @param   pcbRead     Where to store the number of bytes actually read.
 *                      If NULL the entire buffer must be filled for a
 *                      successful return.
 */
static DECLCALLBACK(int) tstDBGCBackRead(PCDBGCIO pBack, void *pvBuf, size_t cbBuf, size_t *pcbRead)
{
    if (g_pszInput && *g_pszInput)
    {
        size_t cb = strlen(g_pszInput);
        if (cb > cbBuf)
            cb = cbBuf;
        *pcbRead = cb;
        memcpy(pvBuf, g_pszInput, cb);
        g_pszInput += cb;
    }
    else
        *pcbRead = 0;
    return VINF_SUCCESS;
}


/**
 * Write (output).
 *
 * @returns VBox status code.
 * @param   pBack       Pointer to the backend structure supplied by
 *                      the backend. The backend can use this to find
 *                      it's instance data.
 * @param   pvBuf       What to write.
 * @param   cbBuf       Number of bytes to write.
 * @param   pcbWritten  Where to store the number of bytes actually written.
 *                      If NULL the entire buffer must be successfully written.
 */
static DECLCALLBACK(int) tstDBGCBackWrite(PCDBGCIO pBack, const void *pvBuf, size_t cbBuf, size_t *pcbWritten)
{
    const char *pch = (const char *)pvBuf;
    if (pcbWritten)
        *pcbWritten = cbBuf;
    while (cbBuf-- > 0)
    {
        /* screen/log output */
        if (g_fPendingPrefix)
        {
            RTTestPrintfNl(g_hTest, RTTESTLVL_ALWAYS, "OUTPUT: ");
            g_fPendingPrefix = false;
        }
        if (*pch == '\n')
            g_fPendingPrefix = true;
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "%c", *pch);

        /* buffer output */
        if (g_offOutput < sizeof(g_szOutput) - 1)
        {
            g_szOutput[g_offOutput++] = *pch;
            g_szOutput[g_offOutput] = '\0';
        }

        /* advance */
        pch++;
    }
    return VINF_SUCCESS;
}


/**
 * Ready / busy notification.
 *
 * @param   pBack       Pointer to the backend structure supplied by
 *                      the backend. The backend can use this to find
 *                      it's instance data.
 * @param   fReady      Whether it's ready (true) or busy (false).
 */
static DECLCALLBACK(void) tstDBGCBackSetReady(PCDBGCIO pBack, bool fReady)
{
}


/**
 * Completes the output, making sure that we're in
 * the 1 position of a new line.
 */
static void tstCompleteOutput(void)
{
    if (!g_fPendingPrefix)
        RTTestPrintf(g_hTest, RTTESTLVL_ALWAYS, "\n");
    g_fPendingPrefix = true;
}


/**
 * Checks if two DBGC variables are identical
 *
 * @returns
 * @param   pVar1               .
 * @param   pVar2               .
 */
bool DBGCVarAreIdentical(PCDBGCVAR pVar1, PCDBGCVAR pVar2)
{
    if (!pVar1)
        return false;
    if (pVar1 == pVar2)
        return true;

    if (pVar1->enmType != pVar2->enmType)
        return false;
    switch (pVar1->enmType)
    {
        case DBGCVAR_TYPE_GC_FLAT:
            if (pVar1->u.GCFlat != pVar2->u.GCFlat)
                return false;
            break;
        case DBGCVAR_TYPE_GC_FAR:
            if (pVar1->u.GCFar.off != pVar2->u.GCFar.off)
                return false;
            if (pVar1->u.GCFar.sel != pVar2->u.GCFar.sel)
                return false;
            break;
        case DBGCVAR_TYPE_GC_PHYS:
            if (pVar1->u.GCPhys != pVar2->u.GCPhys)
                return false;
            break;
        case DBGCVAR_TYPE_HC_FLAT:
            if (pVar1->u.pvHCFlat != pVar2->u.pvHCFlat)
                return false;
            break;
        case DBGCVAR_TYPE_HC_PHYS:
            if (pVar1->u.HCPhys != pVar2->u.HCPhys)
                return false;
            break;
        case DBGCVAR_TYPE_NUMBER:
            if (pVar1->u.u64Number != pVar2->u.u64Number)
                return false;
            break;
        case DBGCVAR_TYPE_STRING:
        case DBGCVAR_TYPE_SYMBOL:
            if (RTStrCmp(pVar1->u.pszString, pVar2->u.pszString) != 0)
                return false;
            break;
        default:
            AssertFailedReturn(false);
    }

    if (pVar1->enmRangeType != pVar2->enmRangeType)
        return false;
    switch (pVar1->enmRangeType)
    {
        case DBGCVAR_RANGE_NONE:
            break;

        case DBGCVAR_RANGE_ELEMENTS:
        case DBGCVAR_RANGE_BYTES:
            if (pVar1->u64Range != pVar2->u64Range)
                return false;
            break;
        default:
            AssertFailedReturn(false);
    }

    return true;
}

/**
 * Tries one command string.
 * @param   pDbgc           Pointer to the debugger instance.
 * @param   pszCmds         The command to test.
 * @param   rcCmd           The expected result.
 * @param   fNoExecute      When set, the command is not executed.
 * @param   pszExpected     Expected output.  This does not need to include all
 *                          of the output, just the start of it.  Thus the
 *                          prompt can be omitted.
 * @param   cArgs           The number of expected arguments. -1 if we don't
 *                          want to check the parsed arguments.
 * @param   va              Info about expected parsed arguments. For each
 *                          argument a DBGCVARTYPE, value (depends on type),
 *                          DBGCVARRANGETYPE and optionally range value.
 */
static void tstTryExV(PDBGC pDbgc, const char *pszCmds, int rcCmd, bool fNoExecute, const char *pszExpected,
                      int32_t cArgs, va_list va)
{
    RT_ZERO(g_szOutput);
    g_offOutput = 0;
    g_pszInput = pszCmds;
    if (strchr(pszCmds, '\0')[-1] == '\n')
        RTTestPrintfNl(g_hTest, RTTESTLVL_ALWAYS, "RUNNING: %s", pszCmds);
    else
        RTTestPrintfNl(g_hTest, RTTESTLVL_ALWAYS, "RUNNING: %s\n", pszCmds);

    pDbgc->rcCmd = VERR_INTERNAL_ERROR;
    dbgcProcessInput(pDbgc, fNoExecute);
    tstCompleteOutput();

    if (pDbgc->rcCmd != rcCmd)
        RTTestFailed(g_hTest, "rcCmd=%Rrc expected =%Rrc\n", pDbgc->rcCmd, rcCmd);
    else if (   !fNoExecute
             && pszExpected
             && strncmp(pszExpected, g_szOutput, strlen(pszExpected)))
        RTTestFailed(g_hTest, "Wrong output - expected \"%s\"", pszExpected);

    if (cArgs >= 0)
    {
        PCDBGCVAR paArgs = pDbgc->aArgs;
        for (int32_t iArg = 0; iArg < cArgs; iArg++)
        {
            DBGCVAR ExpectedArg;
            ExpectedArg.enmType = (DBGCVARTYPE)va_arg(va, int/*DBGCVARTYPE*/);
            switch (ExpectedArg.enmType)
            {
                case DBGCVAR_TYPE_GC_FLAT:  ExpectedArg.u.GCFlat    = va_arg(va, RTGCPTR); break;
                case DBGCVAR_TYPE_GC_FAR:   ExpectedArg.u.GCFar.sel = va_arg(va, int /*RTSEL*/);
                                            ExpectedArg.u.GCFar.off = va_arg(va, uint32_t); break;
                case DBGCVAR_TYPE_GC_PHYS:  ExpectedArg.u.GCPhys    = va_arg(va, RTGCPHYS); break;
                case DBGCVAR_TYPE_HC_FLAT:  ExpectedArg.u.pvHCFlat  = va_arg(va, void *); break;
                case DBGCVAR_TYPE_HC_PHYS:  ExpectedArg.u.HCPhys    = va_arg(va, RTHCPHYS); break;
                case DBGCVAR_TYPE_NUMBER:   ExpectedArg.u.u64Number = va_arg(va, uint64_t); break;
                case DBGCVAR_TYPE_STRING:   ExpectedArg.u.pszString = va_arg(va, const char *); break;
                case DBGCVAR_TYPE_SYMBOL:   ExpectedArg.u.pszString = va_arg(va, const char *); break;
                default:
                    RTTestFailed(g_hTest, "enmType=%u iArg=%u\n", ExpectedArg.enmType, iArg);
                    ExpectedArg.u.u64Number = 0;
                    break;
            }
            ExpectedArg.enmRangeType = (DBGCVARRANGETYPE)va_arg(va, int /*DBGCVARRANGETYPE*/);
            switch (ExpectedArg.enmRangeType)
            {
                case DBGCVAR_RANGE_NONE:        ExpectedArg.u64Range = 0; break;
                case DBGCVAR_RANGE_ELEMENTS:    ExpectedArg.u64Range = va_arg(va, uint64_t); break;
                case DBGCVAR_RANGE_BYTES:       ExpectedArg.u64Range = va_arg(va, uint64_t); break;
                    default:
                        RTTestFailed(g_hTest, "enmRangeType=%u iArg=%u\n", ExpectedArg.enmRangeType, iArg);
                        ExpectedArg.u64Range = 0;
                        break;
            }

            if (!DBGCVarAreIdentical(&ExpectedArg, &paArgs[iArg]))
                RTTestFailed(g_hTest,
                             "Arg #%u\n"
                             "actual:   enmType=%u u64=%#RX64 enmRangeType=%u u64Range=%#RX64\n"
                             "expected: enmType=%u u64=%#RX64 enmRangeType=%u u64Range=%#RX64\n",
                             iArg,
                             paArgs[iArg].enmType, paArgs[iArg].u.u64Number, paArgs[iArg].enmRangeType, paArgs[iArg].u64Range,
                             ExpectedArg.enmType, ExpectedArg.u.u64Number, ExpectedArg.enmRangeType, ExpectedArg.u64Range);
        }
    }
}

/**
 * Tries one command string.
 *
 * @param   pDbgc           Pointer to the debugger instance.
 * @param   pszCmds         The command to test.
 * @param   rcCmd           The expected result.
 * @param   fNoExecute      When set, the command is not executed.
 * @param   pszExpected     Expected output.  This does not need to include all
 *                          of the output, just the start of it.  Thus the
 *                          prompt can be omitted.
 * @param   cArgs           The number of expected arguments. -1 if we don't
 *                          want to check the parsed arguments.
 * @param   ...             Info about expected parsed arguments. For each
 *                          argument a DBGCVARTYPE, value (depends on type),
 *                          DBGCVARRANGETYPE and optionally range value.
 */
static void tstTryEx(PDBGC pDbgc, const char *pszCmds, int rcCmd, bool fNoExecute, const char *pszExpected, int32_t cArgs, ...)
{
    va_list va;
    va_start(va, cArgs);
    tstTryExV(pDbgc, pszCmds, rcCmd, fNoExecute, pszExpected, cArgs, va);
    va_end(va);
}


/**
 * Tries one command string without executing it.
 *
 * @param   pDbgc           Pointer to the debugger instance.
 * @param   pszCmds         The command to test.
 * @param   rcCmd           The expected result.
 */
static void tstTry(PDBGC pDbgc, const char *pszCmds, int rcCmd)
{
    return tstTryEx(pDbgc, pszCmds, rcCmd, true /*fNoExecute*/, NULL, -1);
}


#ifdef SOME_UNUSED_FUNCTION
/**
 * Tries to execute one command string.
 * @param   pDbgc           Pointer to the debugger instance.
 * @param   pszCmds         The command to test.
 * @param   rcCmd           The expected result.
 * @param   pszExpected     Expected output.  This does not need to include all
 *                          of the output, just the start of it.  Thus the
 *                          prompt can be omitted.
 */
static void tstTryExec(PDBGC pDbgc, const char *pszCmds, int rcCmd, const char *pszExpected)
{
    return tstTryEx(pDbgc, pszCmds, rcCmd, false /*fNoExecute*/, pszExpected, -1);
}
#endif


/**
 * Test an operator on an expression resulting a plain number.
 *
 * @param   pDbgc           Pointer to the debugger instance.
 * @param   pszExpr         The express to test.
 * @param   u64Expect       The expected result.
 */
static void tstNumOp(PDBGC pDbgc, const char *pszExpr, uint64_t u64Expect)
{
    char szCmd[80];
    RTStrPrintf(szCmd, sizeof(szCmd), "format %s\n", pszExpr);

    char szExpected[80];
    RTStrPrintf(szExpected, sizeof(szExpected),
                "Number: hex %llx  dec 0i%lld  oct 0t%llo", u64Expect, u64Expect, u64Expect);

    return tstTryEx(pDbgc, szCmd, VINF_SUCCESS, false /*fNoExecute*/, szExpected, -1);
}


/*
 *
 * CodeView emulation commands.
 * CodeView emulation commands.
 * CodeView emulation commands.
 *
 */


static void testCodeView_ba(PDBGC pDbgc)
{
    RTTestISub("codeview - ba");
    tstTry(pDbgc, "ba x 1 0f000:0000\n", VINF_SUCCESS);
    tstTry(pDbgc, "ba x 1 0f000:0000 0\n", VINF_SUCCESS);
    tstTry(pDbgc, "ba x 1 0f000:0000 0 ~0\n", VINF_SUCCESS);
    tstTry(pDbgc, "ba x 1 0f000:0000 0 ~0 \"command\"\n", VINF_SUCCESS);
    tstTry(pDbgc, "ba x 1 0f000:0000 0 ~0 \"command\" too_many\n", VERR_DBGC_PARSE_TOO_MANY_ARGUMENTS);
    tstTry(pDbgc, "ba x 1\n", VERR_DBGC_PARSE_TOO_FEW_ARGUMENTS);

    tstTryEx(pDbgc, "ba x 1 0f000:1234 5 1000 \"command\"\n", VINF_SUCCESS,
             true /*fNoExecute*/,  NULL /*pszExpected*/, 6 /*cArgs*/,
             DBGCVAR_TYPE_STRING, "x",                          DBGCVAR_RANGE_BYTES, UINT64_C(1),
             DBGCVAR_TYPE_NUMBER, UINT64_C(1),                  DBGCVAR_RANGE_NONE,
             DBGCVAR_TYPE_GC_FAR, 0xf000, UINT32_C(0x1234),     DBGCVAR_RANGE_NONE,
             DBGCVAR_TYPE_NUMBER, UINT64_C(0x5),                DBGCVAR_RANGE_NONE,
             DBGCVAR_TYPE_NUMBER, UINT64_C(0x1000),             DBGCVAR_RANGE_NONE,
             DBGCVAR_TYPE_STRING, "command",                    DBGCVAR_RANGE_BYTES, UINT64_C(7));

    tstTryEx(pDbgc, "ba x 1 %0f000:1234 5 1000 \"command\"\n", VINF_SUCCESS,
             true /*fNoExecute*/,  NULL /*pszExpected*/, 6 /*cArgs*/,
             DBGCVAR_TYPE_STRING, "x",                          DBGCVAR_RANGE_BYTES, UINT64_C(1),
             DBGCVAR_TYPE_NUMBER, UINT64_C(1),                  DBGCVAR_RANGE_NONE,
             DBGCVAR_TYPE_GC_FLAT, UINT64_C(0xf1234),           DBGCVAR_RANGE_NONE,
             DBGCVAR_TYPE_NUMBER, UINT64_C(0x5),                DBGCVAR_RANGE_NONE,
             DBGCVAR_TYPE_NUMBER, UINT64_C(0x1000),             DBGCVAR_RANGE_NONE,
             DBGCVAR_TYPE_STRING, "command",                    DBGCVAR_RANGE_BYTES, UINT64_C(7));

    tstTry(pDbgc, "ba x 1 bad:bad 5 1000 \"command\"\n", VINF_SUCCESS);
    tstTry(pDbgc, "ba x 1 %bad:bad 5 1000 \"command\"\n", VERR_DBGC_PARSE_CONVERSION_FAILED);

    tstTryEx(pDbgc, "ba f 1 0f000:1234 5 1000 \"command\"\n", VINF_SUCCESS,
             true /*fNoExecute*/,  NULL /*pszExpected*/, 6 /*cArgs*/,
             DBGCVAR_TYPE_STRING, "f",                          DBGCVAR_RANGE_BYTES, UINT64_C(1),
             DBGCVAR_TYPE_NUMBER, UINT64_C(1),                  DBGCVAR_RANGE_NONE,
             DBGCVAR_TYPE_GC_FAR, 0xf000, UINT32_C(0x1234),     DBGCVAR_RANGE_NONE,
             DBGCVAR_TYPE_NUMBER, UINT64_C(0x5),                DBGCVAR_RANGE_NONE,
             DBGCVAR_TYPE_NUMBER, UINT64_C(0x1000),             DBGCVAR_RANGE_NONE,
             DBGCVAR_TYPE_STRING, "command",                    DBGCVAR_RANGE_BYTES, UINT64_C(7));

    tstTry(pDbgc, "ba x 1 0f000:1234 qnx 1000 \"command\"\n",   VERR_DBGC_PARSE_TOO_MANY_ARGUMENTS);
    tstTry(pDbgc, "ba x 1 0f000:1234 5 qnx \"command\"\n",      VERR_DBGC_PARSE_TOO_MANY_ARGUMENTS);
    tstTry(pDbgc, "ba x qnx 0f000:1234 5 1000 \"command\"\n",   VERR_DBGC_PARSE_INVALID_NUMBER);
    tstTry(pDbgc, "ba x 1 qnx 5 1000 \"command\"\n",            VERR_DBGC_PARSE_INVALID_NUMBER);
}


static void testCodeView_bc(PDBGC pDbgc)
{
    RTTestISub("codeview - bc");
}


static void testCodeView_bd(PDBGC pDbgc)
{
    RTTestISub("codeview - bc");
}


static void testCodeView_be(PDBGC pDbgc)
{
    RTTestISub("codeview - be");
}


static void testCodeView_bl(PDBGC pDbgc)
{
    RTTestISub("codeview - bl");
}


static void testCodeView_bp(PDBGC pDbgc)
{
    RTTestISub("codeview - bp");
}


static void testCodeView_br(PDBGC pDbgc)
{
    RTTestISub("codeview - br");
}


static void testCodeView_d(PDBGC pDbgc)
{
    RTTestISub("codeview - d");
}


static void testCodeView_da(PDBGC pDbgc)
{
    RTTestISub("codeview - da");
}


static void testCodeView_db(PDBGC pDbgc)
{
    RTTestISub("codeview - db");
}


static void testCodeView_dd(PDBGC pDbgc)
{
    RTTestISub("codeview - dd");
}


static void testCodeView_dg(PDBGC pDbgc)
{
    RTTestISub("codeview - dg");
}


static void testCodeView_dga(PDBGC pDbgc)
{
    RTTestISub("codeview - dga");
}


static void testCodeView_di(PDBGC pDbgc)
{
    RTTestISub("codeview - di");
}


static void testCodeView_dia(PDBGC pDbgc)
{
    RTTestISub("codeview - dia");
}


static void testCodeView_dl(PDBGC pDbgc)
{
    RTTestISub("codeview - dl");
}


static void testCodeView_dla(PDBGC pDbgc)
{
    RTTestISub("codeview - dla");
}


static void testCodeView_dpd(PDBGC pDbgc)
{
    RTTestISub("codeview - dpd");
}


static void testCodeView_dpda(PDBGC pDbgc)
{
    RTTestISub("codeview - dpda");
}


static void testCodeView_dpdb(PDBGC pDbgc)
{
    RTTestISub("codeview - dpdb");
}


static void testCodeView_dpdg(PDBGC pDbgc)
{
    RTTestISub("codeview - dpdg");
}


static void testCodeView_dpdh(PDBGC pDbgc)
{
    RTTestISub("codeview - dpdh");
}


static void testCodeView_dph(PDBGC pDbgc)
{
    RTTestISub("codeview - dph");
}


static void testCodeView_dphg(PDBGC pDbgc)
{
    RTTestISub("codeview - dphg");
}


static void testCodeView_dphh(PDBGC pDbgc)
{
    RTTestISub("codeview - dphh");
}


static void testCodeView_dq(PDBGC pDbgc)
{
    RTTestISub("codeview - dq");
}


static void testCodeView_dt(PDBGC pDbgc)
{
    RTTestISub("codeview - dt");
}


static void testCodeView_dt16(PDBGC pDbgc)
{
    RTTestISub("codeview - dt16");
}


static void testCodeView_dt32(PDBGC pDbgc)
{
    RTTestISub("codeview - dt32");
}


static void testCodeView_dt64(PDBGC pDbgc)
{
    RTTestISub("codeview - dt64");
}


static void testCodeView_dw(PDBGC pDbgc)
{
    RTTestISub("codeview - dw");
}


static void testCodeView_eb(PDBGC pDbgc)
{
    RTTestISub("codeview - eb");
}


static void testCodeView_ew(PDBGC pDbgc)
{
    RTTestISub("codeview - ew");
}


static void testCodeView_ed(PDBGC pDbgc)
{
    RTTestISub("codeview - ed");
}


static void testCodeView_eq(PDBGC pDbgc)
{
    RTTestISub("codeview - eq");
}


static void testCodeView_g(PDBGC pDbgc)
{
    RTTestISub("codeview - g");
}


static void testCodeView_k(PDBGC pDbgc)
{
    RTTestISub("codeview - k");
}


static void testCodeView_kg(PDBGC pDbgc)
{
    RTTestISub("codeview - kg");
}


static void testCodeView_kh(PDBGC pDbgc)
{
    RTTestISub("codeview - kh");
}


static void testCodeView_lm(PDBGC pDbgc)
{
    RTTestISub("codeview - lm");
}


static void testCodeView_lmo(PDBGC pDbgc)
{
    RTTestISub("codeview - lmo");
}


static void testCodeView_ln(PDBGC pDbgc)
{
    RTTestISub("codeview - ln");
}


static void testCodeView_ls(PDBGC pDbgc)
{
    RTTestISub("codeview - ls");
}


static void testCodeView_m(PDBGC pDbgc)
{
    RTTestISub("codeview - m");
}


static void testCodeView_r(PDBGC pDbgc)
{
    RTTestISub("codeview - r");
}


static void testCodeView_rg(PDBGC pDbgc)
{
    RTTestISub("codeview - rg");
}


static void testCodeView_rg32(PDBGC pDbgc)
{
    RTTestISub("codeview - rg32");
}


static void testCodeView_rg64(PDBGC pDbgc)
{
    RTTestISub("codeview - rg64");
}


static void testCodeView_rh(PDBGC pDbgc)
{
    RTTestISub("codeview - rh");
}


static void testCodeView_rt(PDBGC pDbgc)
{
    RTTestISub("codeview - rt");
}


static void testCodeView_s(PDBGC pDbgc)
{
    RTTestISub("codeview - s");
}


static void testCodeView_sa(PDBGC pDbgc)
{
    RTTestISub("codeview - sa");
}


static void testCodeView_sb(PDBGC pDbgc)
{
    RTTestISub("codeview - sb");
}


static void testCodeView_sd(PDBGC pDbgc)
{
    RTTestISub("codeview - sd");
}


static void testCodeView_sq(PDBGC pDbgc)
{
    RTTestISub("codeview - sq");
}


static void testCodeView_su(PDBGC pDbgc)
{
    RTTestISub("codeview - su");
}


static void testCodeView_sw(PDBGC pDbgc)
{
    RTTestISub("codeview - sw");
}


static void testCodeView_t(PDBGC pDbgc)
{
    RTTestISub("codeview - t");
}


static void testCodeView_y(PDBGC pDbgc)
{
    RTTestISub("codeview - y");
}


static void testCodeView_u64(PDBGC pDbgc)
{
    RTTestISub("codeview - u64");
}


static void testCodeView_u32(PDBGC pDbgc)
{
    RTTestISub("codeview - u32");
}


static void testCodeView_u16(PDBGC pDbgc)
{
    RTTestISub("codeview - u16");
}


static void testCodeView_uv86(PDBGC pDbgc)
{
    RTTestISub("codeview - uv86");
}


/*
 * Common commands.
 */

static void testCommon_bye_exit_quit(PDBGC pDbgc)
{
    RTTestISub("common - bye/exit/quit");
    /* These have the same parameter descriptor and handler, the command really
       just has a couple of aliases.*/
    tstTry(pDbgc, "bye\n", VINF_SUCCESS);
    tstTry(pDbgc, "bye x\n", VERR_DBGC_PARSE_TOO_MANY_ARGUMENTS);
    tstTry(pDbgc, "bye 1\n", VERR_DBGC_PARSE_TOO_MANY_ARGUMENTS);
    tstTry(pDbgc, "bye %bad:bad\n", VERR_DBGC_PARSE_TOO_MANY_ARGUMENTS);
    tstTry(pDbgc, "exit\n", VINF_SUCCESS);
    tstTry(pDbgc, "quit\n", VINF_SUCCESS);
}


static void testCommon_cpu(PDBGC pDbgc)
{
    RTTestISub("common - cpu");
    tstTry(pDbgc, "cpu\n", VINF_SUCCESS);
    tstTry(pDbgc, "cpu 1\n", VINF_SUCCESS);
    tstTry(pDbgc, "cpu 1 1\n", VERR_DBGC_PARSE_TOO_MANY_ARGUMENTS);
    tstTry(pDbgc, "cpu emt\n", VERR_DBGC_PARSE_INVALID_NUMBER);
    tstTry(pDbgc, "cpu @eax\n", VINF_SUCCESS);
    tstTry(pDbgc, "cpu %bad:bad\n", VERR_DBGC_PARSE_CONVERSION_FAILED);
    tstTry(pDbgc, "cpu '1'\n", VERR_DBGC_PARSE_INVALID_NUMBER);
}


static void testCommon_echo(PDBGC pDbgc)
{
    RTTestISub("common - echo");
    tstTry(pDbgc, "echo\n", VERR_DBGC_PARSE_TOO_FEW_ARGUMENTS);
    tstTry(pDbgc, "echo 1\n", VINF_SUCCESS);
    tstTryEx(pDbgc, "echo 1 2 3  4 5   6\n", VINF_SUCCESS, false, "1 2 3 4 5 6", -1);

    /* The idea here is that since the prefered input is a string, we
       definitely won't be confused by the number like beginning. */
    tstTryEx(pDbgc, "echo 1234567890abcdefghijklmn\n", VINF_SUCCESS, false, "1234567890abcdefghijklmn", -1);

    /* The idea here is that we'll perform the + operation and then convert the
       result to a string (hex). */
    tstTryEx(pDbgc, "echo 1 + 1\n", VINF_SUCCESS, false, "2", -1);
    tstTryEx(pDbgc, "echo \"1 + 1\"\n", VINF_SUCCESS, false, "1 + 1", -1);

    tstTryEx(pDbgc, "echo 0i10 + 6\n", VINF_SUCCESS, false, "10", -1);
    tstTryEx(pDbgc, "echo \"0i10 + 6\"\n", VINF_SUCCESS, false, "0i10 + 6", -1);

    tstTryEx(pDbgc, "echo %f000:0010\n", VINF_SUCCESS, false, "%00000000000f0010", -1);
    tstTryEx(pDbgc, "echo \"%f000:0010\"\n", VINF_SUCCESS, false, "%f000:0010", -1);

    tstTry(pDbgc, "echo %bad:bad\n", VERR_DBGC_PARSE_CONVERSION_FAILED);
}


static void testCommon_format(PDBGC pDbgc)
{
    RTTestISub("common - format");
}


static void testCommon_detect(PDBGC pDbgc)
{
    RTTestISub("common - detect");
}


static void testCommon_harakiri(PDBGC pDbgc)
{
    RTTestISub("common - harakiri");
}


static void testCommon_help(PDBGC pDbgc)
{
    RTTestISub("common - help");
}


static void testCommon_info(PDBGC pDbgc)
{
    RTTestISub("common - info");
    tstTry(pDbgc, "info 12fg\n", VINF_SUCCESS);
    tstTry(pDbgc, "info fflags argument\n", VINF_SUCCESS);
}


static void testCommon_loadimage(PDBGC pDbgc)
{
    RTTestISub("common - loadimage");
}


static void testCommon_loadmap(PDBGC pDbgc)
{
    RTTestISub("common - loadmap");
}


static void testCommon_loadplugin(PDBGC pDbgc)
{
    RTTestISub("common - loadplugin");
}


static void testCommon_loadseg(PDBGC pDbgc)
{
    RTTestISub("common - loadseg");
}


static void testCommon_loadsyms(PDBGC pDbgc)
{
    RTTestISub("common - loadsyms");
}


static void testCommon_loadvars(PDBGC pDbgc)
{
    RTTestISub("common - loadvars");
}


static void testCommon_log(PDBGC pDbgc)
{
    RTTestISub("common - log");
}


static void testCommon_logdest(PDBGC pDbgc)
{
    RTTestISub("common - logdest");
}


static void testCommon_logflags(PDBGC pDbgc)
{
    RTTestISub("common - logflags");
}


static void testCommon_runscript(PDBGC pDbgc)
{
    RTTestISub("common - runscript");
}


static void testCommon_set(PDBGC pDbgc)
{
    RTTestISub("common - set");
}


static void testCommon_showplugins(PDBGC pDbgc)
{
    RTTestISub("common - showplugins");
}


static void testCommon_showvars(PDBGC pDbgc)
{
    RTTestISub("common - showvars");
}


static void testCommon_stop(PDBGC pDbgc)
{
    RTTestISub("common - stop");
}


static void testCommon_unloadplugin(PDBGC pDbgc)
{
    RTTestISub("common - unloadplugin");
}


static void testCommon_unset(PDBGC pDbgc)
{
    RTTestISub("common - unset");
}


static void testCommon_writecore(PDBGC pDbgc)
{
    RTTestISub("common - writecore");
}



/*
 * Basic tests.
 */

static void testBasicsOddCases(PDBGC pDbgc)
{
    RTTestISub("Odd cases");
    tstTry(pDbgc, "r @rax\n", VINF_SUCCESS);
    tstTry(pDbgc, "r @eax\n", VINF_SUCCESS);
    tstTry(pDbgc, "r @ah\n", VINF_SUCCESS);
    tstTry(pDbgc, "r @notavalidregister\n", VERR_DBGF_REGISTER_NOT_FOUND);
}


static void testBasicsOperators(PDBGC pDbgc)
{
    RTTestISub("Operators");
    tstNumOp(pDbgc, "1",                                        1);
    tstNumOp(pDbgc, "1",                                        1);
    tstNumOp(pDbgc, "1",                                        1);

    tstNumOp(pDbgc, "+1",                                       1);
    tstNumOp(pDbgc, "++++++1",                                  1);

    tstNumOp(pDbgc, "-1",                                       UINT64_MAX);
    tstNumOp(pDbgc, "--1",                                      1);
    tstNumOp(pDbgc, "---1",                                     UINT64_MAX);
    tstNumOp(pDbgc, "----1",                                    1);

    tstNumOp(pDbgc, "~0",                                       UINT64_MAX);
    tstNumOp(pDbgc, "~1",                                       UINT64_MAX-1);
    tstNumOp(pDbgc, "~~0",                                      0);
    tstNumOp(pDbgc, "~~1",                                      1);

    tstNumOp(pDbgc, "!1",                                       0);
    tstNumOp(pDbgc, "!0",                                       1);
    tstNumOp(pDbgc, "!42",                                      0);
    tstNumOp(pDbgc, "!!42",                                     1);
    tstNumOp(pDbgc, "!!!42",                                    0);
    tstNumOp(pDbgc, "!!!!42",                                   1);

    tstNumOp(pDbgc, "1 +1",                                     2);
    tstNumOp(pDbgc, "1 + 1",                                    2);
    tstNumOp(pDbgc, "1+1",                                      2);
    tstNumOp(pDbgc, "1+ 1",                                     2);

    tstNumOp(pDbgc, "1 - 1",                                    0);
    tstNumOp(pDbgc, "99 - 90",                                  9);

    tstNumOp(pDbgc, "2 * 2",                                    4);

    tstNumOp(pDbgc, "2 / 2",                                    1);
    tstNumOp(pDbgc, "2 / 0",                                    UINT64_MAX);
    tstNumOp(pDbgc, "0i1024 / 0i4",                             256);

    tstNumOp(pDbgc, "8 mod 7",                                  1);

    tstNumOp(pDbgc, "1<<1",                                     2);
    tstNumOp(pDbgc, "1<<0i32",                                  UINT64_C(0x0000000100000000));
    tstNumOp(pDbgc, "1<<0i48",                                  UINT64_C(0x0001000000000000));
    tstNumOp(pDbgc, "1<<0i63",                                  UINT64_C(0x8000000000000000));

    tstNumOp(pDbgc, "fedcba0987654321>>0i04",                   UINT64_C(0x0fedcba098765432));
    tstNumOp(pDbgc, "fedcba0987654321>>0i32",                   UINT64_C(0xfedcba09));
    tstNumOp(pDbgc, "fedcba0987654321>>0i48",                   UINT64_C(0x0000fedc));

    tstNumOp(pDbgc, "0ef & 4",                                  4);
    tstNumOp(pDbgc, "01234567891 & fff",                        UINT64_C(0x00000000891));
    tstNumOp(pDbgc, "01234567891 & ~fff",                       UINT64_C(0x01234567000));

    tstNumOp(pDbgc, "1 | 1",                                    1);
    tstNumOp(pDbgc, "0 | 4",                                    4);
    tstNumOp(pDbgc, "4 | 0",                                    4);
    tstNumOp(pDbgc, "4 | 4",                                    4);
    tstNumOp(pDbgc, "1 | 4 | 2",                                7);

    tstNumOp(pDbgc, "1 ^ 1",                                    0);
    tstNumOp(pDbgc, "1 ^ 0",                                    1);
    tstNumOp(pDbgc, "0 ^ 1",                                    1);
    tstNumOp(pDbgc, "3 ^ 1",                                    2);
    tstNumOp(pDbgc, "7 ^ 3",                                    4);

    tstNumOp(pDbgc, "7 || 3",                                   1);
    tstNumOp(pDbgc, "1 || 0",                                   1);
    tstNumOp(pDbgc, "0 || 1",                                   1);
    tstNumOp(pDbgc, "0 || 0",                                   0);

    tstNumOp(pDbgc, "0 && 0",                                   0);
    tstNumOp(pDbgc, "1 && 0",                                   0);
    tstNumOp(pDbgc, "0 && 1",                                   0);
    tstNumOp(pDbgc, "1 && 1",                                   1);
    tstNumOp(pDbgc, "4 && 1",                                   1);
}


static void testBasicsFundametalParsing(PDBGC pDbgc)
{
    RTTestISub("Fundamental parsing");
    tstTry(pDbgc, "stop\n", VINF_SUCCESS);
    tstTry(pDbgc, "format 1\n", VINF_SUCCESS);
    tstTry(pDbgc, "format \n", VERR_DBGC_PARSE_TOO_FEW_ARGUMENTS);
    tstTry(pDbgc, "format 0 1 23 4\n", VERR_DBGC_PARSE_TOO_MANY_ARGUMENTS);
    tstTry(pDbgc, "format 'x'\n", VINF_SUCCESS);
    tstTry(pDbgc, "format 'x' 'x'\n", VERR_DBGC_PARSE_TOO_MANY_ARGUMENTS);
    tstTry(pDbgc, "format 'x''x'\n", VINF_SUCCESS);
    tstTry(pDbgc, "format 'x'\"x\"\n", VERR_DBGC_PARSE_EXPECTED_BINARY_OP);
    tstTry(pDbgc, "format 'x'1\n", VERR_DBGC_PARSE_EXPECTED_BINARY_OP);
    tstTry(pDbgc, "format (1)1\n", VERR_DBGC_PARSE_EXPECTED_BINARY_OP);
    tstTry(pDbgc, "format (1)(1)\n", VERR_DBGC_PARSE_EXPECTED_BINARY_OP);
    tstTry(pDbgc, "format (1)''\n", VERR_DBGC_PARSE_EXPECTED_BINARY_OP);
    tstTry(pDbgc, "format nosuchfunction(1)\n", VERR_DBGC_PARSE_FUNCTION_NOT_FOUND);
    tstTry(pDbgc, "format nosuchfunction(1,2,3)\n", VERR_DBGC_PARSE_FUNCTION_NOT_FOUND);
    tstTry(pDbgc, "format nosuchfunction()\n", VERR_DBGC_PARSE_FUNCTION_NOT_FOUND);
    tstTry(pDbgc, "format randu32()\n", VINF_SUCCESS);
    tstTryEx(pDbgc, "format %0\n", VINF_SUCCESS, false, "Guest flat address: %00000000", -1);
    tstTryEx(pDbgc, "format %eax\n", VINF_SUCCESS, false, "Guest flat address: %cafebabe", -1);
    tstTry(pDbgc, "sa 3 23 4 'q' \"21123123\" 'b' \n", VINF_SUCCESS);
    tstTry(pDbgc, "sa 3,23, 4,'q' ,\"21123123\" , 'b' \n", VINF_SUCCESS);
}


int main()
{
    /*
     * Init.
     */
    int rc = RTTestInitAndCreate("tstDBGCParser", &g_hTest);
    if (rc)
        return rc;
    RTTestBanner(g_hTest);

    /*
     * Create a DBGC instance.
     */
    RTTestSub(g_hTest, "dbgcCreate");
    PDBGC pDbgc;
    rc = dbgcCreate(&pDbgc, &g_tstBack, 0);
    if (RT_SUCCESS(rc))
    {
        pDbgc->pVM = (PVM)pDbgc;
        pDbgc->pUVM = (PUVM)pDbgc;
        rc = dbgcProcessInput(pDbgc, true /* fNoExecute */);
        tstCompleteOutput();
        if (RT_SUCCESS(rc))
        {
            /*
             * Perform basic tests first.
             */
            testBasicsFundametalParsing(pDbgc);
            if (RTTestErrorCount(g_hTest) == 0)
                testBasicsOperators(pDbgc);
            if (RTTestErrorCount(g_hTest) == 0)
                testBasicsOddCases(pDbgc);

            /*
             * Test commands.
             */
            if (RTTestErrorCount(g_hTest) == 0)
            {
                testCodeView_ba(pDbgc);
                testCodeView_bc(pDbgc);
                testCodeView_bd(pDbgc);
                testCodeView_be(pDbgc);
                testCodeView_bl(pDbgc);
                testCodeView_bp(pDbgc);
                testCodeView_br(pDbgc);
                testCodeView_d(pDbgc);
                testCodeView_da(pDbgc);
                testCodeView_db(pDbgc);
                testCodeView_dd(pDbgc);
                testCodeView_dg(pDbgc);
                testCodeView_dga(pDbgc);
                testCodeView_di(pDbgc);
                testCodeView_dia(pDbgc);
                testCodeView_dl(pDbgc);
                testCodeView_dla(pDbgc);
                testCodeView_dpd(pDbgc);
                testCodeView_dpda(pDbgc);
                testCodeView_dpdb(pDbgc);
                testCodeView_dpdg(pDbgc);
                testCodeView_dpdh(pDbgc);
                testCodeView_dph(pDbgc);
                testCodeView_dphg(pDbgc);
                testCodeView_dphh(pDbgc);
                testCodeView_dq(pDbgc);
                testCodeView_dt(pDbgc);
                testCodeView_dt16(pDbgc);
                testCodeView_dt32(pDbgc);
                testCodeView_dt64(pDbgc);
                testCodeView_dw(pDbgc);
                testCodeView_eb(pDbgc);
                testCodeView_ew(pDbgc);
                testCodeView_ed(pDbgc);
                testCodeView_eq(pDbgc);
                testCodeView_g(pDbgc);
                testCodeView_k(pDbgc);
                testCodeView_kg(pDbgc);
                testCodeView_kh(pDbgc);
                testCodeView_lm(pDbgc);
                testCodeView_lmo(pDbgc);
                testCodeView_ln(pDbgc);
                testCodeView_ls(pDbgc);
                testCodeView_m(pDbgc);
                testCodeView_r(pDbgc);
                testCodeView_rg(pDbgc);
                testCodeView_rg32(pDbgc);
                testCodeView_rg64(pDbgc);
                testCodeView_rh(pDbgc);
                testCodeView_rt(pDbgc);
                testCodeView_s(pDbgc);
                testCodeView_sa(pDbgc);
                testCodeView_sb(pDbgc);
                testCodeView_sd(pDbgc);
                testCodeView_sq(pDbgc);
                testCodeView_su(pDbgc);
                testCodeView_sw(pDbgc);
                testCodeView_t(pDbgc);
                testCodeView_y(pDbgc);
                testCodeView_u64(pDbgc);
                testCodeView_u32(pDbgc);
                testCodeView_u16(pDbgc);
                testCodeView_uv86(pDbgc);

                testCommon_bye_exit_quit(pDbgc);
                testCommon_cpu(pDbgc);
                testCommon_echo(pDbgc);
                testCommon_format(pDbgc);
                testCommon_detect(pDbgc);
                testCommon_harakiri(pDbgc);
                testCommon_help(pDbgc);
                testCommon_info(pDbgc);
                testCommon_loadimage(pDbgc);
                testCommon_loadmap(pDbgc);
                testCommon_loadplugin(pDbgc);
                testCommon_loadseg(pDbgc);
                testCommon_loadsyms(pDbgc);
                testCommon_loadvars(pDbgc);
                testCommon_log(pDbgc);
                testCommon_logdest(pDbgc);
                testCommon_logflags(pDbgc);
                testCommon_runscript(pDbgc);
                testCommon_set(pDbgc);
                testCommon_showplugins(pDbgc);
                testCommon_showvars(pDbgc);
                testCommon_stop(pDbgc);
                testCommon_unloadplugin(pDbgc);
                testCommon_unset(pDbgc);
                testCommon_writecore(pDbgc);
            }
        }

        dbgcDestroy(pDbgc);
    }

    /*
     * Summary
     */
    return RTTestSummaryAndDestroy(g_hTest);
}
