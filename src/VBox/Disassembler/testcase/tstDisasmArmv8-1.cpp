/* $Id: tstDisasmArmv8-1.cpp 109507 2025-05-12 17:53:56Z vadim.galitsyn@oracle.com $ */
/** @file
 * VBox disassembler - Testcase for ARMv8 A64
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
 * SPDX-License-Identifier: GPL-3.0-only
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define VBOX_DIS_WITH_ARMV8
#include <VBox/dis.h>
#include <VBox/err.h>
#include <iprt/test.h>
#include <iprt/ctype.h>
#include <iprt/string.h>
#include <iprt/err.h>
#include <iprt/script.h>
#include <iprt/sg.h>
#include <iprt/stream.h>

#ifdef TST_DISASM_WITH_CAPSTONE_DISASSEMBLER
# include "/opt/homebrew/include/capstone/capstone.h"
#endif

#include "tstDisasmArmv8-1-tests.h"

typedef struct TESTRDR
{
    RTSGSEG aSegs[3];
    RTSGBUF SgBuf;
} TESTRDR;
typedef TESTRDR *PTESTRDR;

DECLASM(int) TestProcA64(void);
DECLASM(int) TestProcA64_EndProc(void);


static DECLCALLBACK(int) rtScriptLexParseNumber(RTSCRIPTLEX hScriptLex, char ch, PRTSCRIPTLEXTOKEN pToken, void *pvUser)
{
    RT_NOREF(ch, pvUser);
    return RTScriptLexScanNumber(hScriptLex, 0 /*uBase*/, false /*fAllowReal*/, pToken);
}


static const char *s_aszSingleStart[] =
{
    ";",
    NULL
};


static const char *s_aszMultiStart[] =
{
    "/*",
    NULL
};


static const char *s_aszMultiEnd[] =
{
    "*/",
    NULL
};


static const RTSCRIPTLEXTOKMATCH s_aMatches[] =
{
    /* Begin of stuff which will get ignored in the semantic matching. */
    { RT_STR_TUPLE(".private_extern"),          RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  0 },
    { RT_STR_TUPLE("_testproca64"),             RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  0 },
    { RT_STR_TUPLE("_testproca64_endproc"),     RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  0 },
    { RT_STR_TUPLE(":"),                        RTSCRIPTLEXTOKTYPE_KEYWORD,    true,  0 },
    /* End of stuff which will get ignored in the semantic matching. */

    { RT_STR_TUPLE(","),                        RTSCRIPTLEXTOKTYPE_PUNCTUATOR, false, 0 },
    { RT_STR_TUPLE("."),                        RTSCRIPTLEXTOKTYPE_PUNCTUATOR, false, 0 },
    { RT_STR_TUPLE("["),                        RTSCRIPTLEXTOKTYPE_PUNCTUATOR, false, 0 },
    { RT_STR_TUPLE("]"),                        RTSCRIPTLEXTOKTYPE_PUNCTUATOR, false, 0 },
    { RT_STR_TUPLE("!"),                        RTSCRIPTLEXTOKTYPE_PUNCTUATOR, false, 0 },
    { NULL, 0,                                  RTSCRIPTLEXTOKTYPE_INVALID,    false, 0 }
};


static const RTSCRIPTLEXRULE s_aRules[] =
{
    { '#',  '#',   RTSCRIPT_LEX_RULE_CONSUME, rtScriptLexParseNumber,             NULL},
    { '0',  '9',   RTSCRIPT_LEX_RULE_CONSUME, rtScriptLexParseNumber,             NULL},
    { 'a',  'z',   RTSCRIPT_LEX_RULE_CONSUME, RTScriptLexScanIdentifier,          NULL},
    { 'A',  'Z',   RTSCRIPT_LEX_RULE_CONSUME, RTScriptLexScanIdentifier,          NULL},
    { '_',  '_',   RTSCRIPT_LEX_RULE_CONSUME, RTScriptLexScanIdentifier,          NULL},
    { '\0', '\0',  RTSCRIPT_LEX_RULE_DEFAULT, NULL,                               NULL}
};


static const RTSCRIPTLEXCFG s_LexCfg =
{
    /** pszName */
    "ARMv8Dis",
    /** pszDesc */
    "ARMv8 disassembler lexer",
    /** fFlags */
    RTSCRIPT_LEX_CFG_F_CASE_INSENSITIVE_LOWER,
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


static DECLCALLBACK(int) testDisasmLexerRead(RTSCRIPTLEX hScriptLex, size_t offBuf, char *pchCur,
                                             size_t cchBuf, size_t *pcchRead, void *pvUser)
{
    RT_NOREF(hScriptLex, offBuf);

    PTESTRDR pRdr = (PTESTRDR)pvUser;
    size_t cbCopied = RTSgBufCopyToBuf(&pRdr->SgBuf, pchCur, cchBuf * sizeof(char));

    *pcchRead = cbCopied * sizeof(char);
    if (!cbCopied)
        return VINF_EOF;

    return VINF_SUCCESS;
}


static void testDisas(const char *pszSub, uint8_t const *pabInstrs, uintptr_t uEndPtr, DISCPUMODE enmDisCpuMode,
                      const unsigned char *pbSrc, unsigned cbSrc)
{
    RTTestISub(pszSub);

    RTSCRIPTLEX hLexSource = NULL;
    TESTRDR Rdr;

    Rdr.aSegs[0].pvSeg = (void *)pbSrc;
    Rdr.aSegs[0].cbSeg = cbSrc;
    RTSgBufInit(&Rdr.SgBuf, &Rdr.aSegs[0], 1);
    int rc = RTScriptLexCreateFromReader(&hLexSource, testDisasmLexerRead,
                                         NULL /*pfnDtor*/, &Rdr /*pvUser*/, cbSrc,
                                         NULL /*phStrCacheId*/, NULL /*phStrCacheStringLit*/,
                                         NULL /*phStrCacheComments*/, &s_LexCfg);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
    if (RT_FAILURE(rc))
        return; /* Can't do our work if this fails. */

    PCRTSCRIPTLEXTOKEN pTokSource;
    rc = RTScriptLexQueryToken(hLexSource, &pTokSource);
    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

    size_t const cbInstrs = uEndPtr - (uintptr_t)pabInstrs;
    for (size_t off = 0; off < cbInstrs;)
    {
        DISSTATE        Dis;
        uint32_t        cb = 1;
#ifndef DIS_CORE_ONLY
        uint32_t const  cErrBefore = RTTestIErrorCount();
        char            szOutput[256] = {0};

        /*
         * Can't use DISInstrToStr() here as it would add addresses and opcode bytes
         * which would trip the semantic matching later on.
         */
        rc = DISInstrEx((uintptr_t)&pabInstrs[off], enmDisCpuMode, DISOPTYPE_ALL,
                        NULL /*pfnReadBytes*/, NULL /*pvUser*/, &Dis, &cb);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        RTTESTI_CHECK(cb == Dis.cbInstr);
        RTTESTI_CHECK(cb == sizeof(uint32_t));

        if (RT_SUCCESS(rc))
        {
            size_t cch = 0;

            switch (enmDisCpuMode)
            {
                case DISCPUMODE_ARMV8_A64:
                case DISCPUMODE_ARMV8_A32:
                case DISCPUMODE_ARMV8_T32:
                    cch = DISFormatArmV8Ex(&Dis, &szOutput[0], sizeof(szOutput),
                                           DIS_FMT_FLAGS_RELATIVE_BRANCH,
                                           NULL /*pfnGetSymbol*/, NULL /*pvUser*/);
                    break;
                default:
                    AssertReleaseFailed(); /* Testcase error. */
                    break;
            }

            szOutput[cch] = '\0';
            RTStrStripR(szOutput);
            RTTESTI_CHECK(szOutput[0]);
            if (szOutput[0])
            {
                /* Build the lexer and compare that it semantically is equal to the source input. */
                RTSCRIPTLEX hLexDis = NULL;
                rc = RTScriptLexCreateFromString(&hLexDis, szOutput, NULL /*phStrCacheId*/,
                                                 NULL /*phStrCacheStringLit*/, NULL /*phStrCacheComments*/,
                                                 &s_LexCfg);
                RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
                if (RT_SUCCESS(rc))
                {
                    PCRTSCRIPTLEXTOKEN pTokDis;
                    rc = RTScriptLexQueryToken(hLexDis, &pTokDis);
                    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

                    /*
                     * Skip over any keyword tokens in the source lexer because these
                     * are not part of the disassembly.
                     */
                    while (pTokSource->enmType == RTSCRIPTLEXTOKTYPE_KEYWORD)
                        pTokSource = RTScriptLexConsumeToken(hLexSource);

                    /* Now compare the token streams until we hit EOS in the disassembly lexer. */
                    do
                    {
                        RTTESTI_CHECK(pTokSource->enmType == pTokDis->enmType);
                        if (pTokSource->enmType == pTokDis->enmType)
                        {
                            switch (pTokSource->enmType)
                            {
                                case RTSCRIPTLEXTOKTYPE_IDENTIFIER:
                                {
                                    int iCmp = strcmp(pTokSource->Type.Id.pszIde, pTokDis->Type.Id.pszIde);
                                    RTTESTI_CHECK(!iCmp);
                                    if (iCmp)
                                        RTTestIFailureDetails("<IDE{%u.%u, %u.%u}, %s != %s>\n",
                                                              pTokSource->PosStart.iLine, pTokSource->PosStart.iCh,
                                                              pTokSource->PosEnd.iLine, pTokSource->PosEnd.iCh,
                                                              pTokSource->Type.Id.pszIde, pTokDis->Type.Id.pszIde);
                                    break;
                                }
                                case RTSCRIPTLEXTOKTYPE_NUMBER:
                                    RTTESTI_CHECK(pTokSource->Type.Number.enmType == pTokDis->Type.Number.enmType);
                                    if (pTokSource->Type.Number.enmType == pTokDis->Type.Number.enmType)
                                    {
                                        switch (pTokSource->Type.Number.enmType)
                                        {
                                            case RTSCRIPTLEXTOKNUMTYPE_NATURAL:
                                            {
                                                RTTESTI_CHECK(pTokSource->Type.Number.Type.u64 == pTokDis->Type.Number.Type.u64);
                                                if (pTokSource->Type.Number.Type.u64 != pTokDis->Type.Number.Type.u64)
                                                    RTTestIFailureDetails("<NUM{%u.%u, %u.%u} %RU64 != %RU64>\n",
                                                                          pTokSource->PosStart.iLine, pTokSource->PosStart.iCh,
                                                                          pTokSource->PosEnd.iLine, pTokSource->PosEnd.iCh,
                                                                          pTokSource->Type.Number.Type.u64, pTokDis->Type.Number.Type.u64);
                                                break;
                                            }
                                            case RTSCRIPTLEXTOKNUMTYPE_INTEGER:
                                            {
                                                RTTESTI_CHECK(pTokSource->Type.Number.Type.i64 == pTokDis->Type.Number.Type.i64);
                                                if (pTokSource->Type.Number.Type.i64 != pTokDis->Type.Number.Type.i64)
                                                    RTTestIFailureDetails("<NUM{%u.%u, %u.%u} %RI64 != %RI64>\n",
                                                                          pTokSource->PosStart.iLine, pTokSource->PosStart.iCh,
                                                                          pTokSource->PosEnd.iLine, pTokSource->PosEnd.iCh,
                                                                          pTokSource->Type.Number.Type.i64, pTokDis->Type.Number.Type.i64);
                                                break;
                                            }
                                        case RTSCRIPTLEXTOKNUMTYPE_REAL:
                                            default:
                                                AssertReleaseFailed();
                                        }
                                    }
                                    else
                                        RTTestIFailureDetails("<NUM{%u.%u, %u.%u} %u != %u>\n",
                                                              pTokSource->PosStart.iLine, pTokSource->PosStart.iCh,
                                                              pTokSource->PosEnd.iLine, pTokSource->PosEnd.iCh,
                                                              pTokSource->Type.Number.enmType, pTokDis->Type.Number.enmType);
                                    break;
                                case RTSCRIPTLEXTOKTYPE_PUNCTUATOR:
                                {
                                    int iCmp = strcmp(pTokSource->Type.Punctuator.pPunctuator->pszMatch,
                                                      pTokDis->Type.Punctuator.pPunctuator->pszMatch);
                                    RTTESTI_CHECK(!iCmp);
                                    if (iCmp)
                                        RTTestIFailureDetails("<PUNCTUATOR{%u.%u, %u.%u}, %s != %s>\n",
                                                              pTokSource->PosStart.iLine, pTokSource->PosStart.iCh,
                                                              pTokSource->PosEnd.iLine, pTokSource->PosEnd.iCh,
                                                              pTokSource->Type.Punctuator.pPunctuator->pszMatch,
                                                              pTokDis->Type.Punctuator.pPunctuator->pszMatch);
                                    break;
                                }

                                /* These should never occur and indicate an issue in the lexer. */
                                case RTSCRIPTLEXTOKTYPE_KEYWORD:
                                    RTTestIFailureDetails("<KEYWORD{%u.%u, %u.%u}, %s>\n",
                                                          pTokSource->PosStart.iLine, pTokSource->PosStart.iCh,
                                                          pTokSource->PosEnd.iLine, pTokSource->PosEnd.iCh,
                                                          pTokSource->Type.Keyword.pKeyword->pszMatch);
                                    break;
                                case RTSCRIPTLEXTOKTYPE_STRINGLIT:
                                    RTTestIFailureDetails("<STRINGLIT{%u.%u, %u.%u}, %s>\n",
                                                          pTokSource->PosStart.iLine, pTokSource->PosStart.iCh,
                                                          pTokSource->PosEnd.iLine, pTokSource->PosEnd.iCh,
                                                          pTokSource->Type.StringLit.pszString);
                                    break;
                                case RTSCRIPTLEXTOKTYPE_OPERATOR:
                                    RTTestIFailureDetails("<OPERATOR{%u.%u, %u.%u}, %s>\n",
                                                          pTokSource->PosStart.iLine, pTokSource->PosStart.iCh,
                                                          pTokSource->PosEnd.iLine, pTokSource->PosEnd.iCh,
                                                          pTokSource->Type.Operator.pOp->pszMatch);
                                    break;
                                case RTSCRIPTLEXTOKTYPE_INVALID:
                                    RTTestIFailureDetails("<INVALID>\n");
                                    break;
                                case RTSCRIPTLEXTOKTYPE_ERROR:
                                    RTTestIFailureDetails("<ERROR{%u.%u, %u.%u}> %s\n",
                                                          pTokSource->PosStart.iLine, pTokSource->PosStart.iCh,
                                                          pTokSource->PosEnd.iLine, pTokSource->PosEnd.iCh,
                                                          pTokSource->Type.Error.pErr->pszMsg);
                                    break;
                                case RTSCRIPTLEXTOKTYPE_EOS:
                                    RTTestIFailureDetails("<EOS>\n");
                                    break;
                                default:
                                    AssertFailed();
                            }
                        }
                        else
                            RTTestIFailureDetails("pTokSource->enmType=%u pTokDis->enmType=%u\n",
                                                  pTokSource->enmType, pTokDis->enmType);

                        /*
                         * Abort on error as the streams are now out of sync and matching will not work
                         * anymore producing lots of noise.
                         */
                        if (cErrBefore != RTTestIErrorCount())
                            break;

                        /* Advance to the next token. */
                        pTokDis = RTScriptLexConsumeToken(hLexDis);
                        Assert(pTokDis);

                        pTokSource = RTScriptLexConsumeToken(hLexSource);
                        Assert(pTokSource);
                    } while (pTokDis->enmType != RTSCRIPTLEXTOKTYPE_EOS);

                    RTScriptLexDestroy(hLexDis);
                }
            }
            if (cErrBefore != RTTestIErrorCount())
            {
                RTTestIFailureDetails("rc=%Rrc, off=%#x (%u) cbInstr=%u enmDisCpuMode=%d\n",
                                      rc, off, off, Dis.cbInstr, enmDisCpuMode);
                RTTestIPrintf(RTTESTLVL_ALWAYS, "%s\n", szOutput);
                break;
            }

            /* Do the output formatting again, now with all the addresses and opcode bytes. */
            DISFormatArmV8Ex(&Dis, szOutput, sizeof(szOutput),
                               DIS_FMT_FLAGS_BYTES_LEFT | DIS_FMT_FLAGS_BYTES_BRACKETS | DIS_FMT_FLAGS_BYTES_SPACED
                             | DIS_FMT_FLAGS_RELATIVE_BRANCH | DIS_FMT_FLAGS_ADDR_LEFT,
                             NULL /*pfnGetSymbol*/, NULL /*pvUser*/);
            RTStrStripR(szOutput);
            RTTESTI_CHECK(szOutput[0]);
            RTTestIPrintf(RTTESTLVL_ALWAYS, "%s\n", szOutput);
        }
        else
            break;

        /* Check with size-only. */
        uint32_t        cbOnly = 1;
        DISSTATE        DisOnly;
        rc = DISInstrWithPrefetchedBytes((uintptr_t)&pabInstrs[off], enmDisCpuMode,  0 /*fFilter - none */,
                                         Dis.Instr.ab, Dis.cbCachedInstr, NULL, NULL, &DisOnly, &cbOnly);

        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        RTTESTI_CHECK(cbOnly == DisOnly.cbInstr);
        RTTESTI_CHECK_MSG(cbOnly == cb, ("%#x vs %#x\n", cbOnly, cb));

#else  /* DIS_CORE_ONLY */
        rc = DISInstr(&pabInstrs[off], enmDisCpuMode,  &Dis, &cb);
        RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
        RTTESTI_CHECK(cb == Dis.cbInstr);
#endif /* DIS_CORE_ONLY */

        off += cb;
    }

    RTScriptLexDestroy(hLexSource);
}


#if defined(TST_DISASM_WITH_CAPSTONE_DISASSEMBLER) && !defined(DIS_CORE_ONLY)
/**
 * Testcase generating all possible 32-bit instruction values and checking our disassembler
 * for compliance against the capstone disassembler (based on LLVM).
 */
static void testDisasComplianceAgaistCapstone(void)
{
    /** @todo SMP */

    csh    hDisasm = ~(size_t)0;
    cs_err rcCs    = cs_open(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN, &hDisasm);
    AssertMsgReturnVoid(rcCs == CS_ERR_OK, ("%d (%#x)\n", rcCs, rcCs));

    char szOutput[256] = {0};

    for (uint32_t u32Insn = 0; u32Insn < UINT32_MAX; u32Insn++)
    {
        cs_insn *pInstr;
        size_t   cInstrs = cs_disasm(hDisasm, (const uint8_t *)&u32Insn, sizeof(u32Insn),
                                     (uintptr_t)&u32Insn, 1, &pInstr);

        DISSTATE        Dis;
        uint32_t        cb = 1;

        /*
         * Can't use DISInstrToStr() here as it would add addresses and opcode bytes
         * which would trip the semantic matching later on.
         */
        int rc = DISInstrEx((uintptr_t)&u32Insn, DISCPUMODE_ARMV8_A64, DISOPTYPE_ALL,
                            NULL /*pfnReadBytes*/, NULL /*pvUser*/, &Dis, &cb);
        if (rc == VERR_DIS_INVALID_OPCODE)
        {
            /* Check whether capstone could successfully disassembler the instruction. */
            if (cInstrs)
            {
                RTTestIFailureDetails("%#08RX32: rcDis==VERR_DIS_INVALID_OPCODE, capstone=%s %s\n",
                                      u32Insn, pInstr->mnemonic, pInstr->op_str);
            }
            /* else: Invalid encoding from both disassemblers, continue. */
        }
        else
        {
            RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
            RTTESTI_CHECK(cb == Dis.cbInstr);
            RTTESTI_CHECK(cb == sizeof(uint32_t));

            size_t cch = DISFormatArmV8Ex(&Dis, &szOutput[0], sizeof(szOutput),
                                          DIS_FMT_FLAGS_RELATIVE_BRANCH,
                                          NULL /*pfnGetSymbol*/, NULL /*pvUser*/);
            Assert(cch);

            szOutput[cch] = '\0';
            RTStrStripR(szOutput);
            RTTESTI_CHECK(szOutput[0]);

            if (cInstrs > 0)
            {
                /* Compare semantically. */

                RTSCRIPTLEX hLexCapstone = NULL;
                TESTRDR Rdr;

                Rdr.aSegs[0].pvSeg = pInstr->mnemonic;
                Rdr.aSegs[0].cbSeg = strlen(pInstr->mnemonic);
                Rdr.aSegs[1].pvSeg = (void *)" ";
                Rdr.aSegs[1].cbSeg = 1;
                Rdr.aSegs[2].pvSeg = pInstr->op_str;
                Rdr.aSegs[2].cbSeg = strlen(pInstr->op_str);
                RTSgBufInit(&Rdr.SgBuf, &Rdr.aSegs[0], 3);
                rc = RTScriptLexCreateFromReader(&hLexCapstone, testDisasmLexerRead,
                                                 NULL /*pfnDtor*/, &Rdr /*pvUser*/, 0 /*cchBuf*/,
                                                 NULL /*phStrCacheId*/, NULL /*phStrCacheStringLit*/,
                                                 NULL /*phStrCacheComments*/, &s_LexCfg);
                RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

                /* Build the lexer and compare that it semantically is equal to the source input. */
                RTSCRIPTLEX hLexDis = NULL;
                rc = RTScriptLexCreateFromString(&hLexDis, szOutput, NULL /*phStrCacheId*/,
                                                 NULL /*phStrCacheStringLit*/, NULL /*phStrCacheComments*/,
                                                 &s_LexCfg);
                RTTESTI_CHECK_RC(rc, VINF_SUCCESS);
                if (RT_SUCCESS(rc))
                {
                    PCRTSCRIPTLEXTOKEN pTokDis;
                    rc = RTScriptLexQueryToken(hLexDis, &pTokDis);
                    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

                    PCRTSCRIPTLEXTOKEN pTokCapstone;
                    rc = RTScriptLexQueryToken(hLexCapstone, &pTokCapstone);
                    RTTESTI_CHECK_RC(rc, VINF_SUCCESS);

                    /* Now compare the token streams until we hit EOS in the disassembly lexer. */
                    bool fFailed = false;
                    do
                    {
                        if (pTokCapstone->enmType == pTokDis->enmType)
                        {
                            switch (pTokCapstone->enmType)
                            {
                                case RTSCRIPTLEXTOKTYPE_IDENTIFIER:
                                {
                                    int iCmp = strcmp(pTokCapstone->Type.Id.pszIde, pTokDis->Type.Id.pszIde);
                                    if (iCmp)
                                        fFailed = true;
                                    break;
                                }
                                case RTSCRIPTLEXTOKTYPE_NUMBER:
                                    if (pTokCapstone->Type.Number.enmType == pTokDis->Type.Number.enmType)
                                    {
                                        switch (pTokCapstone->Type.Number.enmType)
                                        {
                                            case RTSCRIPTLEXTOKNUMTYPE_NATURAL:
                                            {
                                                if (pTokCapstone->Type.Number.Type.u64 != pTokDis->Type.Number.Type.u64)
                                                    fFailed = true;
                                                break;
                                            }
                                            case RTSCRIPTLEXTOKNUMTYPE_INTEGER:
                                            {
                                                if (pTokCapstone->Type.Number.Type.i64 != pTokDis->Type.Number.Type.i64)
                                                    fFailed = true;
                                                break;
                                            }
                                        case RTSCRIPTLEXTOKNUMTYPE_REAL:
                                            default:
                                                AssertReleaseFailed();
                                        }
                                    }
                                    else
                                        fFailed = true;
                                    break;
                                case RTSCRIPTLEXTOKTYPE_PUNCTUATOR:
                                {
                                    int iCmp = strcmp(pTokCapstone->Type.Punctuator.pPunctuator->pszMatch,
                                                      pTokDis->Type.Punctuator.pPunctuator->pszMatch);
                                    if (iCmp)
                                        fFailed = true;
                                    break;
                                }

                                /* These should never occur and indicate an issue in the lexer. */
                                case RTSCRIPTLEXTOKTYPE_KEYWORD:
                                case RTSCRIPTLEXTOKTYPE_STRINGLIT:
                                case RTSCRIPTLEXTOKTYPE_OPERATOR:
                                case RTSCRIPTLEXTOKTYPE_INVALID:
                                case RTSCRIPTLEXTOKTYPE_ERROR:
                                case RTSCRIPTLEXTOKTYPE_EOS:
                                    fFailed = true;
                                    break;
                                default:
                                    AssertFailed();
                            }
                        }
                        else
                            fFailed = true;

                        /* Abort on error. */
                        if (fFailed)
                            break;

                        /* Advance to the next token. */
                        pTokDis = RTScriptLexConsumeToken(hLexDis);
                        Assert(pTokDis);

                        pTokCapstone = RTScriptLexConsumeToken(hLexCapstone);
                        Assert(pTokCapstone);
                    } while (   pTokDis->enmType != RTSCRIPTLEXTOKTYPE_EOS
                             || pTokCapstone->enmType != RTSCRIPTLEXTOKTYPE_EOS);

                    if (fFailed)
                        RTTestIFailureDetails("%#08RX32: rcDis=%s, capstone=%s %s\n",
                                              u32Insn, szOutput, pInstr->mnemonic, pInstr->op_str);
                }

                RTScriptLexDestroy(hLexCapstone);
                RTScriptLexDestroy(hLexDis);
            }
            else
            {
                RTTestIFailureDetails("%#08RX32: Dis=%s, capstone=disassembly failure\n",
                                      u32Insn, szOutput);
            }
        }
    }

    /* Cleanup. */
    cs_close(&hDisasm);
}
#endif

int main(int argc, char **argv)
{
    RT_NOREF2(argc, argv);
    RTTEST hTest;
    RTEXITCODE rcExit = RTTestInitAndCreate("tstDisasm", &hTest);
    if (rcExit)
        return rcExit;
    RTTestBanner(hTest);

    static const struct
    {
        const char              *pszDesc;
        uint8_t const           *pbStart;
        uintptr_t               uEndPtr;
        DISCPUMODE              enmCpuMode;
        const unsigned char     *pbSrc;
        unsigned                cbSrc;
    } aSnippets[] =
    {
#ifndef RT_OS_OS2
        { "64-bit",     (uint8_t const *)(uintptr_t)TestProcA64, (uintptr_t)&TestProcA64_EndProc, DISCPUMODE_ARMV8_A64,
          g_abtstDisasmArmv8_1, g_cbtstDisasmArmv8_1 },
#endif
    };

    for (unsigned i = 0; i < RT_ELEMENTS(aSnippets); i++)
        testDisas(aSnippets[i].pszDesc, aSnippets[i].pbStart, aSnippets[i].uEndPtr, aSnippets[i].enmCpuMode,
                  aSnippets[i].pbSrc, aSnippets[i].cbSrc);

#if defined(TST_DISASM_WITH_CAPSTONE_DISASSEMBLER) && !defined(DIS_CORE_ONLY)
    testDisasComplianceAgaistCapstone();
#endif

    return RTTestSummaryAndDestroy(hTest);
}

