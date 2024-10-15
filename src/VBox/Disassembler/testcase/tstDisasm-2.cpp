/* $Id: tstDisasm-2.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * Testcase - Generic Disassembler Tool.
 */

/*
 * Copyright (C) 2008-2024 Oracle and/or its affiliates.
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
#include <VBox/dis.h>
#include <VBox/err.h>
#include <iprt/alloc.h>
#include <iprt/assert.h>
#include <iprt/initterm.h>
#include <iprt/getopt.h>
#include <iprt/file.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/ctype.h>


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
typedef enum { kAsmStyle_Default, kAsmStyle_yasm, kAsmStyle_masm, kAsmStyle_gas, kAsmStyle_invalid } ASMSTYLE;
typedef enum { kUndefOp_Fail, kUndefOp_All, kUndefOp_DefineByte, kUndefOp_End } UNDEFOPHANDLING;

typedef struct MYDISSTATE
{
    DISSTATE        Dis;
    uint64_t        uAddress;           /**< The current instruction address. */
    uint8_t        *pbInstr;            /**< The current instruction (pointer). */
    uint32_t        cbInstr;            /**< The size of the current instruction. */
    bool            fUndefOp;           /**< Whether the current instruction is really an undefined opcode.*/
    UNDEFOPHANDLING enmUndefOp;         /**< How to treat undefined opcodes. */
    int             rc;                 /**< Set if we hit EOF. */
    size_t          cbLeft;             /**< The number of bytes left. (read) */
    uint8_t        *pbNext;             /**< The next byte. (read) */
    uint64_t        uNextAddr;          /**< The address of the next byte. (read) */
    char            szLine[256];        /**< The disassembler text output. */
} MYDISSTATE;
typedef MYDISSTATE *PMYDISSTATE;



/**
 * Default style.
 *
 * @param   pState      The disassembler state.
 */
static void MyDisasDefaultFormatter(PMYDISSTATE pState)
{
    RTPrintf("%s", pState->szLine);
}


/**
 * Yasm style.
 *
 * @param   pState      The disassembler state.
 */
static void MyDisasYasmFormatter(PMYDISSTATE pState)
{
    char szTmp[256];
#if 0
    /* a very quick hack. */
    strcpy(szTmp, RTStrStripL(strchr(pState->szLine, ':') + 1));

    char *psz = strrchr(szTmp, '[');
    *psz = '\0';
    RTStrStripR(szTmp);

    psz = strstr(szTmp, " ptr ");
    if (psz)
        memset(psz, ' ', 5);

    char *pszEnd = strchr(szTmp, '\0');
    while (pszEnd - &szTmp[0] < 71)
        *pszEnd++ = ' ';
    *pszEnd = '\0';

#else
    size_t cch = DISFormatYasmEx(&pState->Dis, szTmp, sizeof(szTmp),
                                 DIS_FMT_FLAGS_STRICT | DIS_FMT_FLAGS_ADDR_RIGHT | DIS_FMT_FLAGS_ADDR_COMMENT
                                 | DIS_FMT_FLAGS_BYTES_RIGHT | DIS_FMT_FLAGS_BYTES_COMMENT | DIS_FMT_FLAGS_BYTES_SPACED,
                                 NULL, NULL);
    Assert(cch < sizeof(szTmp));
    while (cch < 71)
        szTmp[cch++] = ' ';
    szTmp[cch] = '\0';
#endif

    RTPrintf("    %s ; %s", szTmp, pState->szLine);
}


/**
 * Masm style.
 *
 * @param   pState      The disassembler state.
 */
static void MyDisasMasmFormatter(PMYDISSTATE pState)
{
    RTPrintf("masm not implemented: %s", pState->szLine);
}


/**
 * This is a temporary workaround for catching a few illegal opcodes
 * that the disassembler is currently letting thru, just enough to make
 * the assemblers happy.
 *
 * We're too close to a release to dare mess with these things now as
 * they may consequences for performance and let alone introduce bugs.
 *
 * @returns true if it's valid. false if it isn't.
 *
 * @param   pDis        The disassembler output.
 */
static bool MyDisasIsValidInstruction(DISSTATE const *pDis)
{
    switch (pDis->pCurInstr->uOpcode)
    {
        /* These doesn't take memory operands. */
        case OP_MOV_CR:
        case OP_MOV_DR:
        case OP_MOV_TR:
            if (pDis->x86.ModRM.Bits.Mod != 3)
                return false;
            break;

         /* The 0x8f /0 variant of this instruction doesn't get its /r value verified. */
        case OP_POP:
            if (    pDis->x86.bOpCode == 0x8f
                &&  pDis->x86.ModRM.Bits.Reg != 0)
                return false;
            break;

        /* The 0xc6 /0 and 0xc7 /0 variants of this instruction don't get their /r values verified. */
        case OP_MOV:
            if (    (   pDis->x86.bOpCode == 0xc6
                     || pDis->x86.bOpCode == 0xc7)
                &&  pDis->x86.ModRM.Bits.Reg != 0)
                return false;
            break;

        default:
            break;
    }

    return true;
}


/**
 * @interface_method_impl{FNDISREADBYTES}
 */
static DECLCALLBACK(int) MyDisasInstrRead(PDISSTATE pDis, uint8_t offInstr, uint8_t cbMinRead, uint8_t cbMaxRead)
{
    RT_NOREF1(cbMaxRead);
    PMYDISSTATE pState   = (PMYDISSTATE)pDis;
    RTUINTPTR   uSrcAddr = pState->Dis.uInstrAddr + offInstr;
    if (RT_LIKELY(   pState->uNextAddr == uSrcAddr
                  && pState->cbLeft >= cbMinRead))
    {
        /*
         * Straight forward reading.
         */
        //size_t cbToRead    = cbMaxRead;
        size_t cbToRead    = cbMinRead;
        memcpy(&pState->Dis.Instr.ab[offInstr], pState->pbNext, cbToRead);
        pState->Dis.cbCachedInstr = offInstr + (uint8_t)cbToRead;
        pState->pbNext    += cbToRead;
        pState->cbLeft    -= cbToRead;
        pState->uNextAddr += cbToRead;
        return VINF_SUCCESS;
    }

    if (pState->uNextAddr == uSrcAddr)
    {
        /*
         * Reading too much.
         */
        if (pState->cbLeft > 0)
        {
            memcpy(&pState->Dis.Instr.ab[offInstr], pState->pbNext, pState->cbLeft);
            offInstr          += (uint8_t)pState->cbLeft;
            cbMinRead         -= (uint8_t)pState->cbLeft;
            pState->pbNext    += pState->cbLeft;
            pState->uNextAddr += pState->cbLeft;
            pState->cbLeft     = 0;
        }
        memset(&pState->Dis.Instr.ab[offInstr], 0xcc, cbMinRead);
        pState->rc = VERR_EOF;
    }
    else
    {
        /*
         * Non-sequential read, that's an error.
         */
        RTStrmPrintf(g_pStdErr, "Reading before current instruction!\n");
        memset(&pState->Dis.Instr.ab[offInstr], 0x90, cbMinRead);
        pState->rc = VERR_INTERNAL_ERROR;
    }
    pState->Dis.cbCachedInstr = offInstr + cbMinRead;
    return pState->rc;
}


/**
 * Disassembles a block of memory.
 *
 * @returns VBox status code.
 * @param   argv0           Program name (for errors and warnings).
 * @param   enmCpuMode      The cpu mode to disassemble in.
 * @param   uAddress        The address we're starting to disassemble at.
 * @param   uHighlightAddr  The address of the instruction that should be
 *                          highlighted.  Pass UINT64_MAX to keep quiet.
 * @param   pbFile          Where to start disassemble.
 * @param   cbFile          How much to disassemble.
 * @param   enmStyle        The assembly output style.
 * @param   fListing        Whether to print in a listing like mode.
 * @param   enmUndefOp      How to deal with undefined opcodes.
 */
static int MyDisasmBlock(const char *argv0, DISCPUMODE enmCpuMode, uint64_t uAddress,
                         uint64_t uHighlightAddr, uint8_t *pbFile, size_t cbFile,
                         ASMSTYLE enmStyle, bool fListing, UNDEFOPHANDLING enmUndefOp)
{
    RT_NOREF1(fListing);

    /*
     * Initialize the CPU context.
     */
    MYDISSTATE State;
    State.uAddress = uAddress;
    State.pbInstr = pbFile;
    State.cbInstr = 0;
    State.enmUndefOp = enmUndefOp;
    State.rc = VINF_SUCCESS;
    State.cbLeft = cbFile;
    State.pbNext = pbFile;
    State.uNextAddr = uAddress;

    void (*pfnFormatter)(PMYDISSTATE pState);
    switch (enmStyle)
    {
        case kAsmStyle_Default:
            pfnFormatter = MyDisasDefaultFormatter;
            break;

        case kAsmStyle_yasm:
            RTPrintf("    BITS %d\n", enmCpuMode == DISCPUMODE_16BIT ? 16 : enmCpuMode == DISCPUMODE_32BIT ? 32 : 64);
            pfnFormatter = MyDisasYasmFormatter;
            break;

        case kAsmStyle_masm:
            pfnFormatter = MyDisasMasmFormatter;
            break;

        default:
            AssertFailedReturn(VERR_INTERNAL_ERROR);
    }

    /*
     * The loop.
     */
    int rcRet = VINF_SUCCESS;
    while (State.cbLeft > 0)
    {
        /*
         * Disassemble it.
         */
        State.cbInstr = 0;
        State.cbLeft += State.pbNext - State.pbInstr;
        State.uNextAddr = State.uAddress;
        State.pbNext = State.pbInstr;

        int rc = DISInstrToStrWithReader(State.uAddress, enmCpuMode, MyDisasInstrRead, &State,
                                         &State.Dis, &State.cbInstr, State.szLine, sizeof(State.szLine));
        if (    RT_SUCCESS(rc)
            ||  (   (   rc == VERR_DIS_INVALID_OPCODE
                     || rc == VERR_DIS_GEN_FAILURE)
                 &&  State.enmUndefOp == kUndefOp_DefineByte))
        {
            State.fUndefOp = rc == VERR_DIS_INVALID_OPCODE
                          || rc == VERR_DIS_GEN_FAILURE
                          || State.Dis.pCurInstr->uOpcode == OP_INVALID
                          || State.Dis.pCurInstr->uOpcode == OP_ILLUD2
                          || (   State.enmUndefOp == kUndefOp_DefineByte
                              && !MyDisasIsValidInstruction(&State.Dis));
            if (State.fUndefOp && State.enmUndefOp == kUndefOp_DefineByte)
            {
                if (!State.cbInstr)
                {
                    State.Dis.Instr.ab[0] = 0;
                    State.Dis.pfnReadBytes(&State.Dis, 0, 1, 1);
                    State.cbInstr = 1;
                }
                RTPrintf("    db");
                for (unsigned off = 0; off < State.cbInstr; off++)
                    RTPrintf(off ? ", %03xh" : " %03xh", State.Dis.Instr.ab[off]);
                RTPrintf("    ; %s\n", State.szLine);
            }
            else if (!State.fUndefOp && State.enmUndefOp == kUndefOp_All)
            {
                RTPrintf("%s: error at %#RX64: unexpected valid instruction (op=%d)\n", argv0, State.uAddress, State.Dis.pCurInstr->uOpcode);
                pfnFormatter(&State);
                rcRet = VERR_GENERAL_FAILURE;
            }
            else if (State.fUndefOp && State.enmUndefOp == kUndefOp_Fail)
            {
                RTPrintf("%s: error at %#RX64: undefined opcode (op=%d)\n", argv0, State.uAddress, State.Dis.pCurInstr->uOpcode);
                pfnFormatter(&State);
                rcRet = VERR_GENERAL_FAILURE;
            }
            else
            {
                /* Use db for odd encodings that we can't make the assembler use. */
                if (    State.enmUndefOp == kUndefOp_DefineByte
                    &&  DISFormatYasmIsOddEncoding(&State.Dis))
                {
                    RTPrintf("    db");
                    for (unsigned off = 0; off < State.cbInstr; off++)
                        RTPrintf(off ? ", %03xh" : " %03xh", State.Dis.Instr.ab[off]);
                    RTPrintf(" ; ");
                }

                pfnFormatter(&State);
            }
        }
        else
        {
            State.cbInstr = State.pbNext - State.pbInstr;
            if (!State.cbLeft)
                RTPrintf("%s: error at %#RX64: read beyond the end (%Rrc)\n", argv0, State.uAddress, rc);
            else if (State.cbInstr)
                RTPrintf("%s: error at %#RX64: %Rrc cbInstr=%d\n", argv0, State.uAddress, rc, State.cbInstr);
            else
            {
                RTPrintf("%s: error at %#RX64: %Rrc cbInstr=%d!\n", argv0, State.uAddress, rc, State.cbInstr);
                if (rcRet == VINF_SUCCESS)
                    rcRet = rc;
                break;
            }
        }

        /* Highlight this instruction? */
        if (uHighlightAddr - State.uAddress < State.cbInstr)
            RTPrintf("; ^^^^^^^^^^^^^^^^^^^^^\n");

        /* Check that the size-only mode returns the smae size on success. */
        if (RT_SUCCESS(rc))
        {
            uint32_t cbInstrOnly = 32;
            uint8_t  abInstr[sizeof(State.Dis.Instr.ab)];
            memcpy(abInstr, State.Dis.Instr.ab, sizeof(State.Dis.Instr.ab));
            int rcOnly = DISInstrWithPrefetchedBytes(State.uAddress, enmCpuMode, 0 /*fFilter - none */,
                                                     abInstr, State.Dis.cbCachedInstr, MyDisasInstrRead, &State,
                                                     &State.Dis, &cbInstrOnly);
            if (   rcOnly != rc
                || cbInstrOnly != State.cbInstr)
            {
                RTPrintf("; Instruction size only check failed rc=%Rrc cbInstrOnly=%#x exepcted %Rrc and %#x\n",
                         rcOnly, cbInstrOnly, rc, State.cbInstr);
                rcRet = VERR_GENERAL_FAILURE;
                break;
            }
        }

        /* next */
        State.uAddress += State.cbInstr;
        State.pbInstr += State.cbInstr;
    }

    return rcRet;
}

/**
 * Converts a hex char to a number.
 *
 * @returns 0..15 on success, -1 on failure.
 * @param   ch      The character.
 */
static int HexDigitToNum(char ch)
{
    switch (ch)
    {
        case '0': return 0;
        case '1': return 1;
        case '2': return 2;
        case '3': return 3;
        case '4': return 4;
        case '5': return 5;
        case '6': return 6;
        case '7': return 7;
        case '8': return 8;
        case '9': return 9;
        case 'A':
        case 'a': return 0xa;
        case 'B':
        case 'b': return 0xb;
        case 'C':
        case 'c': return 0xc;
        case 'D':
        case 'd': return 0xd;
        case 'E':
        case 'e': return 0xe;
        case 'F':
        case 'f': return 0xf;
        default:
            RTPrintf("error: Invalid hex digit '%c'\n", ch);
            return -1;
    }
}

/**
 * Prints usage info.
 *
 * @returns 1.
 * @param   argv0       The program name.
 */
static int Usage(const char *argv0)
{
    RTStrmPrintf(g_pStdErr,
"usage: %s [options] <file1> [file2..fileN]\n"
"   or: %s [options] <-x|--hex-bytes> <hex byte> [more hex..]\n"
"   or: %s <--help|-h>\n"
"\n"
"Options:\n"
"  --address|-a <address>\n"
"    The base address. Default: 0\n"
"  --max-bytes|-b <bytes>\n"
"    The maximum number of bytes to disassemble. Default: 1GB\n"
"  --cpumode|-c <16|32|64>\n"
"    The cpu mode. Default: 32\n"
"  --listing|-l, --no-listing|-L\n"
"    Enables or disables listing mode. Default: --no-listing\n"
"  --offset|-o <offset>\n"
"    The file offset at which to start disassembling. Default: 0\n"
"  --style|-s <default|yasm|masm>\n"
"    The assembly output style. Default: default\n"
"  --undef-op|-u <fail|all|db>\n"
"    How to treat undefined opcodes. Default: fail\n"
             , argv0, argv0, argv0);
    return 1;
}


int main(int argc, char **argv)
{
    RTR3InitExe(argc, &argv, 0);
    const char * const argv0 = RTPathFilename(argv[0]);

    /* options */
    uint64_t uAddress = 0;
    uint64_t uHighlightAddr = UINT64_MAX;
    ASMSTYLE enmStyle = kAsmStyle_Default;
    UNDEFOPHANDLING enmUndefOp = kUndefOp_Fail;
    bool fListing = true;
    DISCPUMODE enmCpuMode = DISCPUMODE_32BIT;
    RTFOFF off = 0;
    RTFOFF cbMax = _1G;
    bool fHexBytes = false;

    /*
     * Parse arguments.
     */
    static const RTGETOPTDEF g_aOptions[] =
    {
        { "--address",      'a', RTGETOPT_REQ_UINT64 },
        { "--cpumode",      'c', RTGETOPT_REQ_UINT32 },
        { "--bytes",        'b', RTGETOPT_REQ_INT64 },
        { "--listing",      'l', RTGETOPT_REQ_NOTHING },
        { "--no-listing",   'L', RTGETOPT_REQ_NOTHING },
        { "--offset",       'o', RTGETOPT_REQ_INT64 },
        { "--style",        's', RTGETOPT_REQ_STRING },
        { "--undef-op",     'u', RTGETOPT_REQ_STRING },
        { "--hex-bytes",    'x', RTGETOPT_REQ_NOTHING },
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, g_aOptions, RT_ELEMENTS(g_aOptions), 1, RTGETOPTINIT_FLAGS_OPTS_FIRST);
    while (   (ch = RTGetOpt(&GetState, &ValueUnion))
           && ch != VINF_GETOPT_NOT_OPTION)
    {
        switch (ch)
        {
            case 'a':
                uAddress = ValueUnion.u64;
                break;

            case 'b':
                cbMax = ValueUnion.i64;
                break;

            case 'c':
                if (ValueUnion.u32 == 16)
                    enmCpuMode = DISCPUMODE_16BIT;
                else if (ValueUnion.u32 == 32)
                    enmCpuMode = DISCPUMODE_32BIT;
                else if (ValueUnion.u32 == 64)
                    enmCpuMode = DISCPUMODE_64BIT;
                else
                {
                    RTStrmPrintf(g_pStdErr, "%s: Invalid CPU mode value %RU32\n", argv0, ValueUnion.u32);
                    return 1;
                }
                break;

            case 'h':
                return Usage(argv0);

            case 'l':
                fListing = true;
                break;

            case 'L':
                fListing = false;
                break;

            case 'o':
                off = ValueUnion.i64;
                break;

            case 's':
                if (!strcmp(ValueUnion.psz, "default"))
                    enmStyle = kAsmStyle_Default;
                else if (!strcmp(ValueUnion.psz, "yasm"))
                    enmStyle = kAsmStyle_yasm;
                else if (!strcmp(ValueUnion.psz, "masm"))
                {
                    //enmStyle = kAsmStyle_masm; Unused right now
                    RTStrmPrintf(g_pStdErr, "%s: masm style isn't implemented yet\n", argv0);
                    return 1;
                }
                else
                {
                    RTStrmPrintf(g_pStdErr, "%s: unknown assembly style: %s\n", argv0, ValueUnion.psz);
                    return 1;
                }
                break;

            case 'u':
                if (!strcmp(ValueUnion.psz, "fail"))
                    enmUndefOp = kUndefOp_Fail;
                else if (!strcmp(ValueUnion.psz, "all"))
                    enmUndefOp = kUndefOp_All;
                else if (!strcmp(ValueUnion.psz, "db"))
                    enmUndefOp = kUndefOp_DefineByte;
                else
                {
                    RTStrmPrintf(g_pStdErr, "%s: unknown undefined opcode handling method: %s\n", argv0, ValueUnion.psz);
                    return 1;
                }
                break;

            case 'x':
                fHexBytes = true;
                break;

            case 'V':
                RTPrintf("$Revision: 106320 $\n");
                return 0;

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    int iArg = GetState.iNext - 1; /** @todo Not pretty, add RTGetOptInit flag for this. */
    if (iArg >= argc)
        return Usage(argv0);

    int rc = VINF_SUCCESS;
    if (fHexBytes)
    {
        /*
         * Convert the remaining arguments from a hex byte string into
         * a buffer that we disassemble.
         */
        size_t      cb = 0;
        uint8_t    *pb = NULL;
        for ( ; iArg < argc; iArg++)
        {
            char ch2;
            const char *psz = argv[iArg];
            while (*psz)
            {
                /** @todo this stuff belongs in IPRT, same stuff as mac address reading. Could be reused for IPv6 with a different item size.*/
                /* skip white space, and for the benefit of linux panics '<' and '>'. */
                while (RT_C_IS_SPACE(ch2 = *psz) || ch2 == '<' || ch2 == '>' || ch2 == ',' || ch2 == ';')
                {
                    if (ch2 == '<')
                        uHighlightAddr = uAddress + cb;
                    psz++;
                }

                if (ch2 == '0' && (psz[1] == 'x' || psz[1] == 'X'))
                {
                    psz += 2;
                    ch2 = *psz;
                }

                if (!ch2)
                    break;

                /* one digit followed by a space or EOS, or two digits. */
                int iNum = HexDigitToNum(*psz++);
                if (iNum == -1)
                    return 1;
                if (!RT_C_IS_SPACE(ch2 = *psz) && ch2 != '\0' && ch2 != '>' && ch2 != ',' && ch2 != ';')
                {
                    int iDigit = HexDigitToNum(*psz++);
                    if (iDigit == -1)
                        return 1;
                    iNum = iNum * 16 + iDigit;
                }

                /* add the byte */
                if (!(cb % 4 /*64*/))
                {
                    pb = (uint8_t *)RTMemRealloc(pb, cb + 64);
                    if (!pb)
                    {
                        RTPrintf("%s: error: RTMemRealloc failed\n", argv[0]);
                        return 1;
                    }
                }
                pb[cb++] = (uint8_t)iNum;
            }
        }

        /*
         * Disassemble it.
         */
        rc = MyDisasmBlock(argv0, enmCpuMode, uAddress, uHighlightAddr, pb, cb, enmStyle, fListing, enmUndefOp);
    }
    else
    {
        /*
         * Process the files.
         */
        for ( ; iArg < argc; iArg++)
        {
            /*
             * Read the file into memory.
             */
            void   *pvFile;
            size_t  cbFile;
            rc = RTFileReadAllEx(argv[iArg], off, cbMax, RTFILE_RDALL_O_DENY_NONE, &pvFile, &cbFile);
            if (RT_FAILURE(rc))
            {
                RTStrmPrintf(g_pStdErr, "%s: %s: %Rrc\n", argv0, argv[iArg], rc);
                break;
            }

            /*
             * Disassemble it.
             */
            rc = MyDisasmBlock(argv0, enmCpuMode, uAddress, uHighlightAddr, (uint8_t *)pvFile, cbFile, enmStyle, fListing, enmUndefOp);
            RTFileReadAllFree(pvFile, cbFile);
            if (RT_FAILURE(rc))
                break;
        }
    }

    return RT_SUCCESS(rc) ? 0 : 1;
}

