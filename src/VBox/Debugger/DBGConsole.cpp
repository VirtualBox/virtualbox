/* $Id: DBGConsole.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGC - Debugger Console.
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


/** @page pg_dbgc                       DBGC - The Debug Console
 *
 * The debugger console is an early attempt to make some interactive
 * debugging facilities for the VirtualBox VMM.  It was initially only
 * accessible thru a telnet session in debug builds.  Later it was hastily built
 * into the VBoxDbg module with a very simple Qt wrapper around it.
 *
 * The current state is that it's by default shipped with all standard
 * VirtualBox builds.  The GUI component is by default accessible in all
 * non-release builds, while release builds require extra data, environment or
 * command line options to make it visible.
 *
 * Now, even if we ship it with all standard builds we would like it to remain
 * an optional feature that can be omitted when building VirtualBox.  Therefore,
 * all external code interfacing DBGC need to be enclosed in
 * \#ifdef VBOX_WITH_DEBUGGER blocks. This is mandatory for components that
 * register external commands.
 *
 *
 * @section sec_dbgc_op                 Operation
 *
 * The console will process commands in a manner similar to the OS/2 and Windows
 * kernel debuggers.  This means ';' is a command separator and that when
 * possible we'll use the same command names as these two uses.  As an
 * alternative we intent to provide a set of gdb-like commands as well and let
 * the user decide which should take precedence.
 *
 *
 * @subsection sec_dbg_op_numbers       Numbers
 *
 * Numbers are hexadecimal unless specified with a prefix indicating
 * elsewise. Prefixes:
 *      - '0x' - hexadecimal.
 *      - '0n' - decimal
 *      - '0t' - octal.
 *      - '0y' - binary.
 *
 * Some of the  prefixes are a bit uncommon, the reason for this that the
 * typical binary prefix '0b' can also be a hexadecimal value since no prefix or
 * suffix is required for such values. Ditto for '0n' and '0' for decimal and
 * octal.
 *
 * The '`' can be used in the numeric value to separate parts as the user
 * wishes.  Generally, though the debugger may use it in output as thousand
 * separator in decimal numbers and 32-bit separator in hex numbers.
 *
 * For historical reasons, a 'h' suffix is suffered on hex numbers.  Unlike most
 * assemblers, a leading 0 before a-f is not required with the 'h' suffix.
 *
 * The prefix '0i' can be used instead of '0n', as it was the early decimal
 * prefix employed by DBGC.  It's being deprecated and may be removed later.
 *
 *
 * @subsection sec_dbg_op_strings       Strings and Symbols
 *
 * The debugger will try to guess, convert or promote what the type of an
 * argument to a command, function or operator based on the input description of
 * the receiver.  If the user wants to make it clear to the debugger that
 * something is a string, put it inside double quotes.  Symbols should use
 * single quotes, though we're current still a bit flexible on this point.
 *
 * If you need to put a quote character inside the quoted text, you escape it by
 * repating it once: echo "printf(""hello world"");"
 *
 *
 * @subsection sec_dbg_op_address       Addressing modes
 *
 *      - Default is flat. For compatibility '%' also means flat.
 *      - Segmented addresses are specified selector:offset.
 *      - Physical addresses are specified using '%%'.
 *      - The default target for the addressing is the guest context, the '#'
 *        will override this and set it to the host.
 *        Note that several operations won't work on host addresses.
 *
 * The '%', '%%' and '#' prefixes is implemented as unary operators, while ':'
 * is a binary operator.  Operator precedence takes care of evaluation order.
 *
 *
 * @subsection sec_dbg_op_c_operators   C/C++ Operators
 *
 * Most unary and binary arithmetic, comparison, logical and bitwise C/C++
 * operators are supported by the debugger, with the same precedence rules of
 * course.  There is one notable change made due to the unary '%' and '%%'
 * operators, and that is that the modulo (remainder) operator is called 'mod'
 * instead of '%'.  This saves a lot of trouble separating argument.
 *
 * There are no assignment operators.  Instead some simple global variable space
 * is provided thru the 'set' and 'unset' commands and the unary '$' operator.
 *
 *
 * @subsection sec_dbg_op_registers     Registers
 *
 * All registers and their sub-fields exposed by the DBGF API are accessible via
 * the '\@' operator.  A few CPU register are accessible directly (as symbols)
 * without using the '\@' operator.  Hypervisor registers are accessible by
 * prefixing the register name with a dot ('.').
 *
 *
 * @subsection sec_dbg_op_commands      Commands
 *
 * Commands names are case sensitive. By convention they are lower cased, starts
 * with a letter but may contain digits and underscores afterwards.  Operators
 * are not allowed in the name (not even part of it), as we would risk
 * misunderstanding it otherwise.
 *
 * Commands returns a status code.
 *
 * The '.' prefix indicates the set of external commands. External commands are
 * command registered by VMM components.
 *
 *
 * @subsection sec_dbg_op_functions     Functions
 *
 * Functions are similar to commands, but return a variable and can only be used
 * as part of an expression making up the argument of a command, function,
 * operator or language statement (if we get around to implement that).
 *
 *
 * @section sec_dbgc_logging            Logging
 *
 * The idea is to be able to pass thru debug and release logs to the console
 * if the user so wishes. This feature requires some kind of hook into the
 * logger instance and while this was sketched it hasn't yet been implemented
 * (dbgcProcessLog and DBGC::fLog).
 *
 * This feature has not materialized and probably never will.
 *
 *
 * @section sec_dbgc_linking            Linking and API
 *
 * The DBGC code is linked into the VBoxVMM module.
 *
 * IMachineDebugger may one day be extended with a DBGC interface so we can work
 * with DBGC remotely without requiring TCP.  Some questions about callbacks
 * (for output) and security (you may wish to restrict users from debugging a
 * VM) needs to be answered first though.
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_DBGC
#include <VBox/dbg.h>
#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/vmapi.h> /* VMR3GetVM() */
#include <VBox/vmm/hm.h>    /* HMR3IsEnabled */
#include <VBox/vmm/nem.h>   /* NEMR3IsEnabled */
#include <VBox/err.h>
#include <VBox/log.h>

#include <iprt/asm.h>
#include <iprt/assert.h>
#include <iprt/file.h>
#include <iprt/mem.h>
#include <iprt/path.h>
#include <iprt/string.h>

#include "DBGCInternal.h"
#include "DBGPlugIns.h"


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static int dbgcProcessLog(PDBGC pDbgc);


/**
 * Resolves a symbol (or tries to do so at least).
 *
 * @returns 0 on success.
 * @returns VBox status on failure.
 * @param   pDbgc       The debug console instance.
 * @param   pszSymbol   The symbol name.
 * @param   enmType     The result type.  Specifying DBGCVAR_TYPE_GC_FAR may
 *                      cause failure, avoid it.
 * @param   pResult     Where to store the result.
 */
int dbgcSymbolGet(PDBGC pDbgc, const char *pszSymbol, DBGCVARTYPE enmType, PDBGCVAR pResult)
{
    int rc;

    /*
     * Builtin?
     */
    PCDBGCSYM pSymDesc = dbgcLookupRegisterSymbol(pDbgc, pszSymbol);
    if (pSymDesc)
    {
        if (!pSymDesc->pfnGet)
            return VERR_DBGC_PARSE_WRITEONLY_SYMBOL;
        return pSymDesc->pfnGet(pSymDesc, &pDbgc->CmdHlp, enmType, pResult);
    }

    /*
     * A typical register? (Guest only)
     */
    static const char s_szSixLetterRegisters[] =
        "rflags;eflags;"
    ;
    static const char s_szThreeLetterRegisters[] =
        "eax;rax;"     "r10;" "r8d;r8w;r8b;"  "cr0;"  "dr0;"
        "ebx;rbx;"     "r11;" "r9d;r9w;r8b;"          "dr1;"
        "ecx;rcx;"     "r12;"                 "cr2;"  "dr2;"
        "edx;rdx;"     "r13;"                 "cr3;"  "dr3;"
        "edi;rdi;dil;" "r14;"                 "cr4;"  "dr4;"
        "esi;rsi;sil;" "r15;"                 "cr8;"
        "ebp;rbp;"
        "esp;rsp;"                                    "dr6;"
        "rip;eip;"                                    "dr7;"
        "efl;"
    ;
    static const char s_szTwoLetterRegisters[] =
        "ax;al;ah;"           "r8;"
        "bx;bl;bh;"           "r9;"
        "cx;cl;ch;"    "cs;"
        "dx;dl;dh;"    "ds;"
        "di;"          "es;"
        "si;"          "fs;"
        "bp;"          "gs;"
        "sp;"          "ss;"
        "ip;"
    ;
    const char  *pszRegSym = *pszSymbol == '.' ? pszSymbol + 1 : pszSymbol;
    size_t const cchRegSym = strlen(pszRegSym);
    if (    (cchRegSym == 2 && strstr(s_szTwoLetterRegisters,   pszRegSym))
        ||  (cchRegSym == 3 && strstr(s_szThreeLetterRegisters, pszRegSym))
        ||  (cchRegSym == 6 && strstr(s_szSixLetterRegisters,   pszRegSym)))
    {
        if (!strchr(pszSymbol, ';'))
        {
            DBGCVAR Var;
            DBGCVAR_INIT_SYMBOL(&Var, pszSymbol);
            rc = dbgcOpRegister(pDbgc, &Var, DBGCVAR_CAT_ANY, pResult);
            if (RT_SUCCESS(rc))
                return DBGCCmdHlpConvert(&pDbgc->CmdHlp, pResult, enmType, false /*fConvSyms*/, pResult);
        }
    }

    /*
     * Ask PDM.
     */
    /** @todo resolve symbols using PDM. */

    /*
     * Ask the debug info manager.
     */
    RTDBGSYMBOL Symbol;
    rc = DBGFR3AsSymbolByName(pDbgc->pUVM, pDbgc->hDbgAs, pszSymbol, &Symbol, NULL);
    if (RT_SUCCESS(rc))
    {
        /*
         * Default return is a flat gc address.
         */
        DBGCVAR_INIT_GC_FLAT(pResult, Symbol.Value);
        if (Symbol.cb)
            DBGCVAR_SET_RANGE(pResult, DBGCVAR_RANGE_BYTES, Symbol.cb);

        switch (enmType)
        {
            /* nothing to do. */
            case DBGCVAR_TYPE_GC_FLAT:
            case DBGCVAR_TYPE_ANY:
                return VINF_SUCCESS;

            /* impossible at the moment. */
            case DBGCVAR_TYPE_GC_FAR:
                return VERR_DBGC_PARSE_CONVERSION_FAILED;

            /* simply make it numeric. */
            case DBGCVAR_TYPE_NUMBER:
                pResult->enmType = DBGCVAR_TYPE_NUMBER;
                pResult->u.u64Number = Symbol.Value;
                return VINF_SUCCESS;

            /* cast it. */
            case DBGCVAR_TYPE_GC_PHYS:
            case DBGCVAR_TYPE_HC_FLAT:
            case DBGCVAR_TYPE_HC_PHYS:
                return DBGCCmdHlpConvert(&pDbgc->CmdHlp, pResult, enmType, false /*fConvSyms*/, pResult);

            default:
                AssertMsgFailed(("Internal error enmType=%d\n", enmType));
                return VERR_INVALID_PARAMETER;
        }
    }

    return VERR_DBGC_PARSE_NOT_IMPLEMENTED;
}


/**
 * Process all commands currently in the buffer.
 *
 * @returns VBox status code. Any error indicates the termination of the console session.
 * @param   pDbgc       Debugger console instance data.
 * @param   fNoExecute  Indicates that no commands should actually be executed.
 */
static int dbgcProcessCommands(PDBGC pDbgc, bool fNoExecute)
{
    /** @todo Replace this with a sh/ksh/csh/rexx like toplevel language that
     *        allows doing function, loops, if, cases, and such. */
    int rc = VINF_SUCCESS;
    while (pDbgc->cInputLines)
    {
        /*
         * Empty the log buffer if we're hooking the log.
         */
        if (pDbgc->fLog)
        {
            rc = dbgcProcessLog(pDbgc);
            if (RT_FAILURE(rc))
                break;
        }

        if (pDbgc->iRead == pDbgc->iWrite)
        {
            AssertMsgFailed(("The input buffer is empty while cInputLines=%d!\n", pDbgc->cInputLines));
            pDbgc->cInputLines = 0;
            return 0;
        }

        /*
         * Copy the command to the parse buffer.
         */
        char    chQuote = 0;
        char    ch;
        char   *psz = &pDbgc->achInput[pDbgc->iRead];
        char   *pszTrg = &pDbgc->achScratch[0];
        AssertCompile(sizeof(pDbgc->achScratch) > sizeof(pDbgc->achInput));
        while ((ch = *psz++) != '\0')
        {
            /* ';' and '\n' are termination characters, except for when they are
               inside quotes.  So, track quoting. */
            if (ch == '"' || ch == '\'')
                chQuote = chQuote == ch ? 0 : chQuote == 0 ? ch : chQuote;
            else if ((ch == ';' || ch == '\n') && chQuote == 0)
                break;

            *pszTrg = ch;

            if (psz == &pDbgc->achInput[sizeof(pDbgc->achInput)])
                psz = &pDbgc->achInput[0];

            /** @todo r=bird: off by one issue here? */
            if (psz == &pDbgc->achInput[pDbgc->iWrite])
            {
                AssertMsgFailed(("The buffer contains no commands while cInputLines=%d!\n", pDbgc->cInputLines));
                pDbgc->cInputLines = 0;
                pDbgc->iRead = pDbgc->iWrite;
                return 0;
            }

            pszTrg++;
        }
        *pszTrg = '\0';

        /*
         * Advance the buffer.
         */
        pDbgc->iRead = psz - &pDbgc->achInput[0];
        if (ch == '\n')
            pDbgc->cInputLines--;

        /*
         * Parse and execute this command.
         */
        pDbgc->pszScratch = pszTrg + 1;
        pDbgc->iArg       = 0;
        rc = dbgcEvalCommand(pDbgc, &pDbgc->achScratch[0], pszTrg - &pDbgc->achScratch[0], fNoExecute);
        if (   rc == VERR_DBGC_QUIT
            || rc == VWRN_DBGC_CMD_PENDING)
            break;
        rc = VINF_SUCCESS; /* ignore other statuses */
    }

    return rc;
}


/**
 * Handle input buffer overflow.
 *
 * Will read any available input looking for a '\n' to reset the buffer on.
 *
 * @returns VBox status code.
 * @param   pDbgc   Debugger console instance data.
 */
static int dbgcInputOverflow(PDBGC pDbgc)
{
    /*
     * Assert overflow status and reset the input buffer.
     */
    if (!pDbgc->fInputOverflow)
    {
        pDbgc->fInputOverflow = true;
        pDbgc->iRead = pDbgc->iWrite = 0;
        pDbgc->cInputLines = 0;
        pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "Input overflow!!\n");
    }

    /*
     * Eat input till no more or there is a '\n'.
     * When finding a '\n' we'll continue normal processing.
     */
    while (pDbgc->pIo->pfnInput(pDbgc->pIo, 0))
    {
        size_t cbRead;
        int rc = pDbgc->pIo->pfnRead(pDbgc->pIo, &pDbgc->achInput[0], sizeof(pDbgc->achInput) - 1, &cbRead);
        if (RT_FAILURE(rc))
            return rc;
        char *psz = (char *)memchr(&pDbgc->achInput[0], '\n', cbRead);
        if (psz)
        {
            pDbgc->fInputOverflow = false;
            pDbgc->iRead = psz - &pDbgc->achInput[0] + 1;
            pDbgc->iWrite = (unsigned)cbRead;
            pDbgc->cInputLines = 0;
            break;
        }
    }

    return 0;
}


/**
 * Read input and do some preprocessing.
 *
 * @returns VBox status code.
 *          In addition to the iWrite and achInput, cInputLines is maintained.
 *          In case of an input overflow the fInputOverflow flag will be set.
 * @param   pDbgc   Debugger console instance data.
 */
static int dbgcInputRead(PDBGC pDbgc)
{
    /*
     * We have ready input.
     * Read it till we don't have any or we have a full input buffer.
     */
    int     rc = 0;
    do
    {
        /*
         * More available buffer space?
         */
        size_t cbLeft;
        if (pDbgc->iWrite > pDbgc->iRead)
            cbLeft = sizeof(pDbgc->achInput) - pDbgc->iWrite - (pDbgc->iRead == 0);
        else
            cbLeft = pDbgc->iRead - pDbgc->iWrite - 1;
        if (!cbLeft)
        {
            /* overflow? */
            if (!pDbgc->cInputLines)
                rc = dbgcInputOverflow(pDbgc);
            break;
        }

        /*
         * Read one char and interpret it.
         */
        char    achRead[128];
        size_t  cbRead;
        rc = pDbgc->pIo->pfnRead(pDbgc->pIo, &achRead[0], RT_MIN(cbLeft, sizeof(achRead)), &cbRead);
        if (RT_FAILURE(rc))
            return rc;
        char *psz = &achRead[0];
        while (cbRead-- > 0)
        {
            char ch = *psz++;
            switch (ch)
            {
                /*
                 * Ignore.
                 */
                case '\0':
                case '\r':
                case '\a':
                    break;

                /*
                 * Backspace.
                 */
                case '\b':
                    Log2(("DBGC: backspace\n"));
                    if (pDbgc->iRead != pDbgc->iWrite)
                    {
                        unsigned iWriteUndo = pDbgc->iWrite;
                        if (pDbgc->iWrite)
                            pDbgc->iWrite--;
                        else
                            pDbgc->iWrite = sizeof(pDbgc->achInput) - 1;

                        if (pDbgc->achInput[pDbgc->iWrite] == '\n')
                            pDbgc->iWrite = iWriteUndo;
                    }
                    break;

                /*
                 * Add char to buffer.
                 */
                case '\t':
                case '\n':
                case ';':
                    switch (ch)
                    {
                        case '\t': ch = ' '; break;
                        case '\n': pDbgc->cInputLines++; break;
                    }
                    RT_FALL_THRU();
                default:
                    Log2(("DBGC: ch=%02x\n", (unsigned char)ch));
                    pDbgc->achInput[pDbgc->iWrite] = ch;
                    if (++pDbgc->iWrite >= sizeof(pDbgc->achInput))
                        pDbgc->iWrite = 0;
                    break;
            }
        }

        /* Terminate it to make it easier to read in the debugger. */
        pDbgc->achInput[pDbgc->iWrite] = '\0';
    } while (pDbgc->pIo->pfnInput(pDbgc->pIo, 0));

    return rc;
}


/**
 * Reads input, parses it and executes commands on '\n'.
 *
 * @returns VBox status code.
 * @param   pDbgc       Debugger console instance data.
 * @param   fNoExecute  Indicates that no commands should actually be executed.
 */
int dbgcProcessInput(PDBGC pDbgc, bool fNoExecute)
{
    /*
     * We know there's input ready, so let's read it first.
     */
    int rc = dbgcInputRead(pDbgc);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Now execute any ready commands.
     */
    if (pDbgc->cInputLines)
    {
        pDbgc->pIo->pfnSetReady(pDbgc->pIo, false);
        pDbgc->fReady = false;
        rc = dbgcProcessCommands(pDbgc, fNoExecute);
        if (RT_SUCCESS(rc) && rc != VWRN_DBGC_CMD_PENDING)
            pDbgc->fReady = true;

        if (    RT_SUCCESS(rc)
            &&  pDbgc->iRead == pDbgc->iWrite
            &&  pDbgc->fReady)
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "VBoxDbg> ");

        if (    RT_SUCCESS(rc)
            &&  pDbgc->fReady)
            pDbgc->pIo->pfnSetReady(pDbgc->pIo, true);
    }
    /*
     * else - we have incomplete line, so leave it in the buffer and
     * wait for more input.
     *
     * Windows telnet client is in "character at a time" mode by
     * default and putty sends eol as a separate packet that will be
     * most likely read separately from the command line it
     * terminates.
     */

    return rc;
}


/**
 * Gets the event context identifier string.
 * @returns Read only string.
 * @param   enmCtx          The context.
 */
DECLHIDDEN(const char *) dbgcGetEventCtx(DBGFEVENTCTX enmCtx)
{
    switch (enmCtx)
    {
        case DBGFEVENTCTX_RAW:      return "raw";
        case DBGFEVENTCTX_REM:      return "rem";
        case DBGFEVENTCTX_HM:       return "hwaccl";
        case DBGFEVENTCTX_HYPER:    return "hyper";
        case DBGFEVENTCTX_OTHER:    return "other";

        case DBGFEVENTCTX_INVALID:  return "!Invalid Event Ctx!";
        default:
            AssertMsgFailed(("enmCtx=%d\n", enmCtx));
            return "!Unknown Event Ctx!";
    }
}


/**
 * Looks up a generic debug event.
 *
 * @returns Pointer to DBGCSXEVT structure if found, otherwise NULL.
 * @param   enmType     The possibly generic event to find the descriptor for.
 */
DECLHIDDEN(PCDBGCSXEVT) dbgcEventLookup(DBGFEVENTTYPE enmType)
{
    uint32_t i = g_cDbgcSxEvents;
    while (i-- > 0)
        if (g_aDbgcSxEvents[i].enmType == enmType)
            return &g_aDbgcSxEvents[i];
    return NULL;
}


/**
 * Processes debugger events.
 *
 * @returns VBox status code.
 * @param   pDbgc   DBGC Instance data.
 * @param   pEvent  Pointer to event data.
 */
static int dbgcProcessEvent(PDBGC pDbgc, PCDBGFEVENT pEvent)
{
    /*
     * Flush log first.
     */
    if (pDbgc->fLog)
    {
        int rc = dbgcProcessLog(pDbgc);
        if (RT_FAILURE(rc))
            return rc;
    }

    /*
     * Process the event.
     */
    pDbgc->pszScratch = &pDbgc->achInput[0];
    pDbgc->iArg       = 0;
    bool fPrintPrompt = true;
    int rc = VINF_SUCCESS;
    VMCPUID const idCpuSaved = pDbgc->idCpu;
    switch (pEvent->enmType)
    {
        /*
         * The first part is events we have initiated with commands.
         */
        case DBGFEVENT_HALT_DONE:
        {
            /** @todo add option to suppress this on CPUs that aren't selected (like
             *        fRegTerse). */
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event/%u: CPU %u has halted! (%s)\n",
                                         pEvent->idCpu, pEvent->idCpu, dbgcGetEventCtx(pEvent->enmCtx));
            if (RT_SUCCESS(rc))
                rc = DBGCCmdHlpRegPrintf(&pDbgc->CmdHlp, pEvent->idCpu, -1, pDbgc->fRegTerse);
            break;
        }


        /*
         * The second part is events which can occur at any time.
         */
        case DBGFEVENT_FATAL_ERROR:
        {
            pDbgc->idCpu = pEvent->idCpu;
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbf event/%u: Fatal error! (%s)\n",
                                         pEvent->idCpu, dbgcGetEventCtx(pEvent->enmCtx));
            if (RT_SUCCESS(rc))
                rc = DBGCCmdHlpRegPrintf(&pDbgc->CmdHlp, pEvent->idCpu, -1, pDbgc->fRegTerse);
            break;
        }

        case DBGFEVENT_BREAKPOINT:
        case DBGFEVENT_BREAKPOINT_IO:
        case DBGFEVENT_BREAKPOINT_MMIO:
        case DBGFEVENT_BREAKPOINT_HYPER:
        {
            pDbgc->idCpu = pEvent->idCpu;
            rc = dbgcBpExec(pDbgc, pEvent->u.Bp.hBp);
            switch (rc)
            {
                case VERR_DBGC_BP_NOT_FOUND:
                    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event/%u: Unknown breakpoint %u! (%s)\n",
                                                 pEvent->idCpu, pEvent->u.Bp.hBp, dbgcGetEventCtx(pEvent->enmCtx));
                    break;

                case VINF_DBGC_BP_NO_COMMAND:
                    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event/%u: Breakpoint %u! (%s)\n",
                                                 pEvent->idCpu, pEvent->u.Bp.hBp, dbgcGetEventCtx(pEvent->enmCtx));
                    break;

                case VINF_BUFFER_OVERFLOW:
                    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event/%u: Breakpoint %u! Command too long to execute! (%s)\n",
                                                 pEvent->idCpu, pEvent->u.Bp.hBp, dbgcGetEventCtx(pEvent->enmCtx));
                    break;

                default:
                    break;
            }
            if (RT_SUCCESS(rc) && DBGFR3IsHalted(pDbgc->pUVM, pEvent->idCpu))
            {
                rc = DBGCCmdHlpRegPrintf(&pDbgc->CmdHlp, pEvent->idCpu, -1, pDbgc->fRegTerse);

                /* Set the resume flag to ignore the breakpoint when resuming execution. */
                if (   RT_SUCCESS(rc)
                    && pEvent->enmType == DBGFEVENT_BREAKPOINT)
                    rc = pDbgc->CmdHlp.pfnExec(&pDbgc->CmdHlp, "r eflags.rf = 1");
            }
            else
                pDbgc->idCpu = idCpuSaved;
            break;
        }

        case DBGFEVENT_STEPPED:
        case DBGFEVENT_STEPPED_HYPER:
        {
            if (!pDbgc->cMultiStepsLeft || pEvent->idCpu != idCpuSaved)
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event/%u: Single step! (%s)\n",
                                             pEvent->idCpu, dbgcGetEventCtx(pEvent->enmCtx));
            else
                pDbgc->cMultiStepsLeft -= 1;
            if (RT_SUCCESS(rc))
            {
                if (pDbgc->fStepTraceRegs)
                    rc = DBGCCmdHlpRegPrintf(&pDbgc->CmdHlp, pEvent->idCpu, -1, pDbgc->fRegTerse);
                else
                {
                    char szCmd[80];
                    if (DBGFR3CpuIsIn64BitCode(pDbgc->pUVM, pDbgc->idCpu))
                        rc = DBGFR3RegPrintf(pDbgc->pUVM, pDbgc->idCpu, szCmd, sizeof(szCmd), "u %016VR{rip} L 0");
                    else if (DBGFR3CpuIsInV86Code(pDbgc->pUVM, pDbgc->idCpu))
                        rc = DBGFR3RegPrintf(pDbgc->pUVM, pDbgc->idCpu, szCmd, sizeof(szCmd), "uv86 %04VR{cs}:%08VR{eip} L 0");
                    else
                        rc = DBGFR3RegPrintf(pDbgc->pUVM, pDbgc->idCpu, szCmd, sizeof(szCmd), "u %04VR{cs}:%08VR{eip} L 0");
                    if (RT_SUCCESS(rc))
                        rc = pDbgc->CmdHlp.pfnExec(&pDbgc->CmdHlp, "%s", szCmd);
                }
            }

            /* If multi-stepping, take the next step: */
            if (pDbgc->cMultiStepsLeft > 0 && pEvent->idCpu == idCpuSaved)
            {
                int rc2 = DBGFR3StepEx(pDbgc->pUVM, pDbgc->idCpu, DBGF_STEP_F_INTO, NULL, NULL, 0, pDbgc->uMultiStepStrideLength);
                if (RT_SUCCESS(rc2))
                    fPrintPrompt = false;
                else
                    DBGCCmdHlpFailRc(&pDbgc->CmdHlp, pDbgc->pMultiStepCmd, rc2, "DBGFR3StepEx(,,DBGF_STEP_F_INTO,) failed");
            }
            else
                pDbgc->idCpu = pEvent->idCpu;
            break;
        }

        case DBGFEVENT_ASSERTION_HYPER:
        {
            pDbgc->idCpu = pEvent->idCpu;
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                                         "\ndbgf event/%u: Hypervisor Assertion! (%s)\n"
                                         "%s"
                                         "%s"
                                         "\n",
                                         pEvent->idCpu,
                                         dbgcGetEventCtx(pEvent->enmCtx),
                                         pEvent->u.Assert.pszMsg1,
                                         pEvent->u.Assert.pszMsg2);
            if (RT_SUCCESS(rc))
                rc = DBGCCmdHlpRegPrintf(&pDbgc->CmdHlp, pEvent->idCpu, -1, pDbgc->fRegTerse);
            break;
        }

        case DBGFEVENT_DEV_STOP:
        {
            pDbgc->idCpu = pEvent->idCpu;
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                                         "\n"
                                         "dbgf event/%u: DBGFSTOP (%s)\n"
                                         "File:     %s\n"
                                         "Line:     %d\n"
                                         "Function: %s\n",
                                         pEvent->idCpu,
                                         dbgcGetEventCtx(pEvent->enmCtx),
                                         pEvent->u.Src.pszFile,
                                         pEvent->u.Src.uLine,
                                         pEvent->u.Src.pszFunction);
            if (RT_SUCCESS(rc) && pEvent->u.Src.pszMessage && *pEvent->u.Src.pszMessage)
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                                             "Message:  %s\n",
                                             pEvent->u.Src.pszMessage);
            if (RT_SUCCESS(rc))
                rc = DBGCCmdHlpRegPrintf(&pDbgc->CmdHlp, pEvent->idCpu, -1, pDbgc->fRegTerse);
            break;
        }


        case DBGFEVENT_INVALID_COMMAND:
        {
            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf/dbgc error: Invalid command event!\n");
            break;
        }

        case DBGFEVENT_POWERING_OFF:
        {
            pDbgc->fReady = false;
            pDbgc->pIo->pfnSetReady(pDbgc->pIo, false);
            pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\nVM is powering off!\n");
            fPrintPrompt = false;
            rc = VERR_GENERAL_FAILURE;
            break;
        }


        default:
        {
            /*
             * Probably a generic event. Look it up to find its name.
             */
            PCDBGCSXEVT pEvtDesc = dbgcEventLookup(pEvent->enmType);
            if (pEvtDesc)
            {
                if (pEvtDesc->enmKind == kDbgcSxEventKind_Interrupt)
                {
                    Assert(pEvtDesc->pszDesc);
                    Assert(pEvent->u.Generic.cArgs == 1);
                    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event/%u: %s no %#llx! (%s)\n",
                                                 pEvent->idCpu, pEvtDesc->pszDesc, pEvent->u.Generic.auArgs[0], pEvtDesc->pszName);
                }
                else if (pEvtDesc->fFlags & DBGCSXEVT_F_BUGCHECK)
                {
                    Assert(pEvent->u.Generic.cArgs >= 5);
                    char szDetails[512];
                    DBGFR3FormatBugCheck(pDbgc->pUVM, szDetails, sizeof(szDetails), pEvent->u.Generic.auArgs[0],
                                         pEvent->u.Generic.auArgs[1], pEvent->u.Generic.auArgs[2],
                                         pEvent->u.Generic.auArgs[3], pEvent->u.Generic.auArgs[4]);
                    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event/%u: %s %s%s!\n%s", pEvent->idCpu,
                                                 pEvtDesc->pszName, pEvtDesc->pszDesc ? "- " : "",
                                                 pEvtDesc->pszDesc ? pEvtDesc->pszDesc : "", szDetails);
                }
                else if (   (pEvtDesc->fFlags & DBGCSXEVT_F_TAKE_ARG)
                         || pEvent->u.Generic.cArgs > 1
                         || (   pEvent->u.Generic.cArgs == 1
                             && pEvent->u.Generic.auArgs[0] != 0))
                {
                    if (pEvtDesc->pszDesc)
                        rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event/%u: %s - %s!",
                                                     pEvent->idCpu, pEvtDesc->pszName, pEvtDesc->pszDesc);
                    else
                        rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event/%u: %s!",
                                                     pEvent->idCpu, pEvtDesc->pszName);
                    if (RT_SUCCESS(rc))
                    {
                        if (pEvent->u.Generic.cArgs <= 1)
                            rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, " arg=%#llx\n", pEvent->u.Generic.auArgs[0]);
                        else
                        {
                            for (uint32_t i = 0; i < pEvent->u.Generic.cArgs; i++)
                                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, " args[%u]=%#llx", i, pEvent->u.Generic.auArgs[i]);
                            if (RT_SUCCESS(rc))
                                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\n");
                        }
                    }
                }
                else
                {
                    if (pEvtDesc->pszDesc)
                        rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event/%u: %s - %s!\n",
                                                     pEvent->idCpu, pEvtDesc->pszName, pEvtDesc->pszDesc);
                    else
                        rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf event/%u: %s!\n",
                                                     pEvent->idCpu, pEvtDesc->pszName);
                }
            }
            else
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\ndbgf/dbgc error: Unknown event %d on CPU %u!\n",
                                             pEvent->enmType, pEvent->idCpu);
            break;
        }
    }

    /*
     * Prompt, anyone?
     */
    if (fPrintPrompt && RT_SUCCESS(rc))
    {
        /** @todo add CPU indicator to the prompt if an SMP VM? */
        rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "VBoxDbg> ");
        pDbgc->fReady = true;
        if (RT_SUCCESS(rc))
            pDbgc->pIo->pfnSetReady(pDbgc->pIo, true);
        pDbgc->cMultiStepsLeft = 0;
    }

    return rc;
}


/**
 * Prints any log lines from the log buffer.
 *
 * The caller must not call function this unless pDbgc->fLog is set.
 *
 * @returns VBox status code. (output related)
 * @param   pDbgc   Debugger console instance data.
 */
static int dbgcProcessLog(PDBGC pDbgc)
{
    /** @todo */
    NOREF(pDbgc);
    return 0;
}

/** @callback_method_impl{FNRTDBGCFGLOG} */
static DECLCALLBACK(void) dbgcDbgCfgLogCallback(RTDBGCFG hDbgCfg, uint32_t iLevel, const char *pszMsg, void *pvUser)
{
    /** @todo Add symbol noise setting.  */
    NOREF(hDbgCfg); NOREF(iLevel);
    PDBGC pDbgc = (PDBGC)pvUser;
    pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "%s", pszMsg);
}


/**
 * Run the debugger console.
 *
 * @returns VBox status code.
 * @param   pDbgc   Pointer to the debugger console instance data.
 */
int dbgcRun(PDBGC pDbgc)
{
    /*
     * We're ready for commands now.
     */
    pDbgc->fReady = true;
    pDbgc->pIo->pfnSetReady(pDbgc->pIo, true);

    /*
     * Main Debugger Loop.
     *
     * This loop will either block on waiting for input or on waiting on
     * debug events. If we're forwarding the log we cannot wait for long
     * before we must flush the log.
     */
    int rc;
    for (;;)
    {
        rc = VERR_SEM_OUT_OF_TURN;
        if (pDbgc->pUVM)
            rc = DBGFR3QueryWaitable(pDbgc->pUVM);

        if (RT_SUCCESS(rc))
        {
            /*
             * Wait for a debug event.
             */
            DBGFEVENT Event;
            rc = DBGFR3EventWait(pDbgc->pUVM, pDbgc->fLog ? 1 : 32, &Event);
            if (RT_SUCCESS(rc))
            {
                rc = dbgcProcessEvent(pDbgc, &Event);
                if (RT_FAILURE(rc))
                    break;
            }
            else if (rc != VERR_TIMEOUT)
                break;

            /*
             * Check for input.
             */
            if (pDbgc->pIo->pfnInput(pDbgc->pIo, 0))
            {
                rc = dbgcProcessInput(pDbgc, false /* fNoExecute */);
                if (RT_FAILURE(rc))
                    break;
            }
        }
        else if (rc == VERR_SEM_OUT_OF_TURN)
        {
            /*
             * Wait for input. If Logging is enabled we'll only wait very briefly.
             */
            if (pDbgc->pIo->pfnInput(pDbgc->pIo, pDbgc->fLog ? 1 : 1000))
            {
                rc = dbgcProcessInput(pDbgc, false /* fNoExecute */);
                if (RT_FAILURE(rc))
                    break;
            }
        }
        else
            break;

        /*
         * Forward log output.
         */
        if (pDbgc->fLog)
        {
            rc = dbgcProcessLog(pDbgc);
            if (RT_FAILURE(rc))
                break;
        }
    }

    return rc;
}


/**
 * Run the init scripts, if present.
 *
 * @param   pDbgc               The console instance.
 */
static void dbgcRunInitScripts(PDBGC pDbgc)
{
    /*
     * Do the global one, if it exists.
     */
    if (    pDbgc->pszGlobalInitScript
        && *pDbgc->pszGlobalInitScript != '\0'
        &&  RTFileExists(pDbgc->pszGlobalInitScript))
        dbgcEvalScript(pDbgc, pDbgc->pszGlobalInitScript, true /*fAnnounce*/);

    /*
     * Then do the local one, if it exists.
     */
    if (    pDbgc->pszLocalInitScript
        && *pDbgc->pszLocalInitScript != '\0'
        &&  RTFileExists(pDbgc->pszLocalInitScript))
        dbgcEvalScript(pDbgc, pDbgc->pszLocalInitScript, true /*fAnnounce*/);
}


/**
 * Reads the CFGM configuration of the DBGC.
 *
 * Popuplates the PDBGC::pszHistoryFile, PDBGC::pszGlobalInitScript and
 * PDBGC::pszLocalInitScript members.
 *
 * @returns VBox status code.
 * @param   pDbgc               The console instance.
 * @param   pUVM                The user mode VM handle.
 */
static int dbgcReadConfig(PDBGC pDbgc, PUVM pUVM)
{
    /*
     * Get and validate the configuration node.
     */
    PCFGMNODE pNode = CFGMR3GetChild(CFGMR3GetRootU(pUVM), "DBGC");
    int rc = CFGMR3ValidateConfig(pNode, "/DBGC/",
                                  "Enabled|"
                                  "HistoryFile|"
                                  "LocalInitScript|"
                                  "GlobalInitScript|",
                                  "*", "DBGC", 0);
    AssertRCReturn(rc, rc);

    /*
     * Query the values.
     */
    char szHomeDefault[RTPATH_MAX];
    rc = RTPathUserHome(szHomeDefault, sizeof(szHomeDefault) - 32);
    AssertLogRelRCReturn(rc, rc);
    size_t cchHome = strlen(szHomeDefault);

    /** @cfgm{/DBGC/HistoryFile, string, ${HOME}/.vboxdbgc-history}
     * The command history file of the VBox debugger. */
    rc = RTPathAppend(szHomeDefault, sizeof(szHomeDefault), ".vboxdbgc-history");
    AssertLogRelRCReturn(rc, rc);

    char szPath[RTPATH_MAX];
    rc = CFGMR3QueryStringDef(pNode, "HistoryFile", szPath, sizeof(szPath), szHomeDefault);
    AssertLogRelRCReturn(rc, rc);

    pDbgc->pszHistoryFile = RTStrDup(szPath);
    AssertReturn(pDbgc->pszHistoryFile, VERR_NO_STR_MEMORY);

    /** @cfgm{/DBGC/GlobalInitFile, string, ${HOME}/.vboxdbgc-init}
     * The global init script of the VBox debugger. */
    szHomeDefault[cchHome] = '\0';
    rc = RTPathAppend(szHomeDefault, sizeof(szHomeDefault), ".vboxdbgc-init");
    AssertLogRelRCReturn(rc, rc);

    rc = CFGMR3QueryStringDef(pNode, "GlobalInitScript", szPath, sizeof(szPath), szHomeDefault);
    AssertLogRelRCReturn(rc, rc);

    pDbgc->pszGlobalInitScript = RTStrDup(szPath);
    AssertReturn(pDbgc->pszGlobalInitScript, VERR_NO_STR_MEMORY);

    /** @cfgm{/DBGC/LocalInitFile, string, none}
     * The VM local init script of the VBox debugger. */
    rc = CFGMR3QueryString(pNode, "LocalInitScript", szPath, sizeof(szPath));
    if (RT_SUCCESS(rc))
    {
        pDbgc->pszLocalInitScript = RTStrDup(szPath);
        AssertReturn(pDbgc->pszLocalInitScript, VERR_NO_STR_MEMORY);
    }
    else
    {
        AssertLogRelReturn(rc == VERR_CFGM_VALUE_NOT_FOUND || rc == VERR_CFGM_NO_PARENT, rc);
        pDbgc->pszLocalInitScript = NULL;
    }

    return VINF_SUCCESS;
}


/**
 * @copydoc DBGC::pfnOutput
 */
static DECLCALLBACK(int) dbgcOutputNative(void *pvUser, const char *pachChars, size_t cbChars)
{
    PDBGC pDbgc = (PDBGC)pvUser;
    return pDbgc->pIo->pfnWrite(pDbgc->pIo, pachChars, cbChars, NULL /*pcbWritten*/);
}


/**
 * Creates a a new instance.
 *
 * @returns VBox status code.
 * @param   ppDbgc      Where to store the pointer to the instance data.
 * @param   pIo         Pointer to the I/O callback table.
 * @param   fFlags      The flags.
 */
int dbgcCreate(PDBGC *ppDbgc, PCDBGCIO pIo, unsigned fFlags)
{
    /*
     * Validate input.
     */
    AssertPtrReturn(pIo, VERR_INVALID_POINTER);
    AssertMsgReturn(!fFlags, ("%#x", fFlags), VERR_INVALID_PARAMETER);

    /*
     * Allocate and initialize.
     */
    PDBGC pDbgc = (PDBGC)RTMemAllocZ(sizeof(*pDbgc));
    if (!pDbgc)
        return VERR_NO_MEMORY;

    dbgcInitCmdHlp(pDbgc);
    pDbgc->pIo              = pIo;
    pDbgc->pfnOutput        = dbgcOutputNative;
    pDbgc->pvOutputUser     = pDbgc;
    pDbgc->pVM              = NULL;
    pDbgc->pUVM             = NULL;
    pDbgc->idCpu            = 0;
    pDbgc->hDbgAs           = DBGF_AS_GLOBAL;
    pDbgc->pszEmulation     = "CodeView/WinDbg";
    pDbgc->paEmulationCmds  = &g_aCmdsCodeView[0];
    pDbgc->cEmulationCmds   = g_cCmdsCodeView;
    pDbgc->paEmulationFuncs = &g_aFuncsCodeView[0];
    pDbgc->cEmulationFuncs  = g_cFuncsCodeView;
    //pDbgc->fLog             = false;
    pDbgc->fRegTerse        = true;
    pDbgc->fStepTraceRegs   = true;
    //pDbgc->cPagingHierarchyDumps = 0;
    //pDbgc->DisasmPos        = {0};
    //pDbgc->SourcePos        = {0};
    //pDbgc->DumpPos          = {0};
    pDbgc->pLastPos          = &pDbgc->DisasmPos;
    //pDbgc->cbDumpElement    = 0;
    //pDbgc->cVars            = 0;
    //pDbgc->paVars           = NULL;
    //pDbgc->pPlugInHead      = NULL;
    //pDbgc->pFirstBp         = NULL;
    RTListInit(&pDbgc->LstTraceFlowMods);
    //pDbgc->abSearch         = {0};
    //pDbgc->cbSearch         = 0;
    pDbgc->cbSearchUnit       = 1;
    pDbgc->cMaxSearchHits     = 1;
    //pDbgc->SearchAddr       = {0};
    //pDbgc->cbSearchRange    = 0;

    //pDbgc->uInputZero       = 0;
    //pDbgc->iRead            = 0;
    //pDbgc->iWrite           = 0;
    //pDbgc->cInputLines      = 0;
    //pDbgc->fInputOverflow   = false;
    pDbgc->fReady           = true;
    pDbgc->pszScratch       = &pDbgc->achScratch[0];
    //pDbgc->iArg             = 0;
    //pDbgc->rcOutput         = 0;
    //pDbgc->rcCmd            = 0;

    //pDbgc->pszHistoryFile       = NULL;
    //pDbgc->pszGlobalInitScript  = NULL;
    //pDbgc->pszLocalInitScript   = NULL;

    dbgcEvalInit();

    *ppDbgc = pDbgc;
    return VINF_SUCCESS;
}

/**
 * Destroys a DBGC instance created by dbgcCreate.
 *
 * @param   pDbgc   Pointer to the debugger console instance data.
 */
void dbgcDestroy(PDBGC pDbgc)
{
    AssertPtr(pDbgc);

    /* Disable log hook. */
    if (pDbgc->fLog)
    {

    }

    /* Detach from the VM. */
    if (pDbgc->pUVM)
        DBGFR3Detach(pDbgc->pUVM);

    /* Free config strings. */
    RTStrFree(pDbgc->pszGlobalInitScript);
    pDbgc->pszGlobalInitScript = NULL;
    RTStrFree(pDbgc->pszLocalInitScript);
    pDbgc->pszLocalInitScript = NULL;
    RTStrFree(pDbgc->pszHistoryFile);
    pDbgc->pszHistoryFile = NULL;

    /* Finally, free the instance memory. */
    RTMemFree(pDbgc);
}


/**
 * Make a console instance.
 *
 * This will not return until either an 'exit' command is issued or a error code
 * indicating connection loss is encountered.
 *
 * @returns VINF_SUCCESS if console termination caused by the 'exit' command.
 * @returns The VBox status code causing the console termination.
 *
 * @param   pUVM        The user mode VM handle.
 * @param   pIo         Pointer to the I/O callback structure. This must contain
 *                      a full set of function pointers to service the console.
 * @param   fFlags      Reserved, must be zero.
 * @remarks A forced termination of the console is easiest done by forcing the
 *          callbacks to return fatal failures.
 */
DBGDECL(int) DBGCCreate(PUVM pUVM, PCDBGCIO pIo, unsigned fFlags)
{
    /*
     * Validate input.
     */
    AssertPtrNullReturn(pUVM, VERR_INVALID_VM_HANDLE);
    PVM pVM = NULL;
    if (pUVM)
    {
        pVM = VMR3GetVM(pUVM);
        AssertPtrReturn(pVM, VERR_INVALID_VM_HANDLE);
    }

    /*
     * Allocate and initialize instance data
     */
    PDBGC pDbgc;
    int rc = dbgcCreate(&pDbgc, pIo, fFlags);
    if (RT_FAILURE(rc))
        return rc;
    if (!HMR3IsEnabled(pUVM) && !NEMR3IsEnabled(pUVM))
        pDbgc->hDbgAs = DBGF_AS_RC_AND_GC_GLOBAL;

    /*
     * Print welcome message.
     */
    rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                                 "Welcome to the VirtualBox Debugger!\n");

    /*
     * Attach to the specified VM.
     */
    if (RT_SUCCESS(rc) && pUVM)
    {
        rc = dbgcReadConfig(pDbgc, pUVM);
        if (RT_SUCCESS(rc))
        {
            rc = DBGFR3Attach(pUVM);
            if (RT_SUCCESS(rc))
            {
                pDbgc->pVM   = pVM;
                pDbgc->pUVM  = pUVM;
                pDbgc->idCpu = 0;
                rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL,
                                             "Current VM is %08x, CPU #%u\n" /** @todo get and print the VM name! */
                                             , pDbgc->pVM, pDbgc->idCpu);
            }
            else
                rc = pDbgc->CmdHlp.pfnVBoxError(&pDbgc->CmdHlp, rc, "When trying to attach to VM %p\n", pDbgc->pVM);
        }
        else
            rc = pDbgc->CmdHlp.pfnVBoxError(&pDbgc->CmdHlp, rc, "Error reading configuration\n");
    }

    /*
     * Load plugins.
     */
    if (RT_SUCCESS(rc))
    {
        if (pVM)
            DBGFR3PlugInLoadAll(pDbgc->pUVM);
        dbgcEventInit(pDbgc);
        dbgcRunInitScripts(pDbgc);

        rc = pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "VBoxDbg> ");
        if (RT_SUCCESS(rc))
        {
            /*
             * Set debug config log callback.
             */
            RTDBGCFG    hDbgCfg = DBGFR3AsGetConfig(pUVM);
            if (   hDbgCfg != NIL_RTDBGCFG
                && RTDbgCfgRetain(hDbgCfg) != UINT32_MAX)
            {
                int rc2 = RTDbgCfgSetLogCallback(hDbgCfg, dbgcDbgCfgLogCallback, pDbgc);
                if (RT_FAILURE(rc2))
                {
                    hDbgCfg = NIL_RTDBGCFG;
                    RTDbgCfgRelease(hDbgCfg);
                }
            }
            else
                hDbgCfg = NIL_RTDBGCFG;


            /*
             * Run the debugger main loop.
             */
            rc = dbgcRun(pDbgc);


            /*
             * Remove debug config log callback.
             */
            if (hDbgCfg != NIL_RTDBGCFG)
            {
                RTDbgCfgSetLogCallback(hDbgCfg, NULL, NULL);
                RTDbgCfgRelease(hDbgCfg);
            }
        }
        dbgcEventTerm(pDbgc);
    }
    else
        pDbgc->CmdHlp.pfnPrintf(&pDbgc->CmdHlp, NULL, "\nDBGCCreate error: %Rrc\n", rc);


    /*
     * Cleanup console debugger session.
     */
    dbgcDestroy(pDbgc);
    return rc == VERR_DBGC_QUIT ? VINF_SUCCESS : rc;
}

