/* $Id: DBGCInternal.h 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGC - Debugger Console, Internal Header File.
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

#ifndef DEBUGGER_INCLUDED_SRC_DBGCInternal_h
#define DEBUGGER_INCLUDED_SRC_DBGCInternal_h
#ifndef RT_WITHOUT_PRAGMA_ONCE
# pragma once
#endif


/*******************************************************************************
*   Header Files                                                               *
*******************************************************************************/
#include <VBox/dbg.h>
#include <VBox/err.h>
#include <VBox/vmm/dbgf.h>
#include <VBox/vmm/dbgfflowtrace.h>

#include <iprt/list.h>

/*******************************************************************************
*   Structures and Typedefs                                                    *
*******************************************************************************/

/**
 * Debugger console per breakpoint data.
 */
typedef struct DBGCBP
{
    /** Pointer to the next breakpoint in the list. */
    struct DBGCBP  *pNext;
    /** The breakpoint identifier. */
    uint32_t        iBp;
    /** The size of the command. */
    size_t          cchCmd;
    /** The command to execute when the breakpoint is hit. */
    char            szCmd[1];
} DBGCBP;
/** Pointer to a breakpoint. */
typedef DBGCBP *PDBGCBP;


typedef enum DBGCEVTSTATE
{
    kDbgcEvtState_Invalid = 0,
    kDbgcEvtState_Disabled,
    kDbgcEvtState_Enabled,
    kDbgcEvtState_Notify
} DBGCEVTSTATE;

/**
 * Debugger console per event configuration.
 */
typedef struct DBGCEVTCFG
{
    /** The event state. */
    DBGCEVTSTATE    enmState;
    /** The size of the command. */
    size_t          cchCmd;
    /** The command to execute when the event occurs. */
    char            szCmd[1];
} DBGCEVTCFG;
/** Pointer to a event configuration. */
typedef DBGCEVTCFG *PDBGCEVTCFG;
/** Pointer to a const event configuration. */
typedef DBGCEVTCFG const *PCDBGCEVTCFG;


/**
 * Named variable.
 *
 * Always allocated from heap in one single block.
 */
typedef struct DBGCNAMEDVAR
{
    /** The variable. */
    DBGCVAR     Var;
    /** Its name. */
    char        szName[1];
} DBGCNAMEDVAR;
/** Pointer to named variable. */
typedef DBGCNAMEDVAR *PDBGCNAMEDVAR;


/**
 * Debugger console per trace flow data.
 */
typedef struct DBGCTFLOW
{
    /** Node for the trace flow module list. */
    RTLISTNODE       NdTraceFlow;
    /** Handle of the DGF trace flow module. */
    DBGFFLOWTRACEMOD hTraceFlowMod;
    /** The control flow graph for the module. */
    DBGFFLOW         hFlow;
    /** The trace flow module identifier. */
    uint32_t         iTraceFlowMod;
} DBGCTFLOW;
/** Pointer to the per trace flow data. */
typedef DBGCTFLOW *PDBGCTFLOW;


/**
 * Debugger console status
 */
typedef enum DBGCSTATUS
{
    /** Normal status, .*/
    DBGC_HALTED

} DBGCSTATUS;


/**
 * Debugger console instance data.
 */
typedef struct DBGC
{
    /** Command helpers. */
    DBGCCMDHLP          CmdHlp;
    /** Wrappers for DBGF output. */
    DBGFINFOHLP         DbgfOutputHlp;
    /** Pointer to I/O callback structure. */
    PCDBGCIO            pIo;

    /**
     * Output a bunch of characters.
     *
     * @returns VBox status code.
     * @param   pvUser      Opaque user data from DBGC::pvOutputUser.
     * @param   pachChars   Pointer to an array of utf-8 characters.
     * @param   cbChars     Number of bytes in the character array pointed to by pachChars.
     */
    DECLR3CALLBACKMEMBER(int, pfnOutput, (void *pvUser, const char *pachChars, size_t cbChars));
    /** Opqaue user data passed to DBGC::pfnOutput. */
    void                *pvOutputUser;

    /** Pointer to the current VM. */
    PVM                 pVM;
    /** The user mode handle of the current VM. */
    PUVM                pUVM;
    /** The ID of current virtual CPU. */
    VMCPUID             idCpu;
    /** The current address space handle. */
    RTDBGAS             hDbgAs;
    /** The current debugger emulation. */
    const char         *pszEmulation;
    /** Pointer to the commands for the current debugger emulation. */
    PCDBGCCMD           paEmulationCmds;
    /** The number of commands paEmulationCmds points to. */
    uint32_t            cEmulationCmds;
    /** Pointer to the functions for the current debugger emulation. */
    PCDBGCFUNC          paEmulationFuncs;
    /** The number of functions paEmulationFuncs points to. */
    uint32_t            cEmulationFuncs;
    /** Log indicator. (If set we're writing the log to the console.) */
    bool                fLog;

    /** Counter use to suppress the printing of the headers. */
    uint8_t             cPagingHierarchyDumps;
    /** Indicates whether the register are terse or sparse. */
    bool                fRegTerse;

    /** @name Stepping
     * @{ */
    /** Whether to display registers when tracing. */
    bool                fStepTraceRegs;
    /** Number of multi-steps left, zero if not multi-stepping.   */
    uint32_t            cMultiStepsLeft;
    /** The multi-step stride length. */
    uint32_t            uMultiStepStrideLength;
    /** The active multi-step command. */
    PCDBGCCMD           pMultiStepCmd;
    /** @} */

    /** Current disassembler position. */
    DBGCVAR             DisasmPos;
    /** The flags that goes with DisasmPos. */
    uint32_t            fDisasm;
    /** Current source position. (flat GC) */
    DBGCVAR             SourcePos;
    /** Current memory dump position. */
    DBGCVAR             DumpPos;
    /** Size of the previous dump element. */
    unsigned            cbDumpElement;
    /** Points to DisasmPos, SourcePos or DumpPos depending on which was
     *  used last. */
    PCDBGCVAR           pLastPos;

    /** Number of variables in papVars. */
    unsigned            cVars;
    /** Array of global variables.
     * Global variables can be referenced using the $ operator and set
     * and unset using command with those names. */
    PDBGCNAMEDVAR      *papVars;

    /** The list of breakpoints. (singly linked) */
    PDBGCBP             pFirstBp;
    /** The list of known trace flow modules. */
    RTLISTANCHOR        LstTraceFlowMods;

    /** Software interrupt events. */
    PDBGCEVTCFG         apSoftInts[256];
    /** Hardware interrupt events. */
    PDBGCEVTCFG         apHardInts[256];
    /** Selectable events (first few entries are unused). */
    PDBGCEVTCFG         apEventCfgs[DBGFEVENT_END];

    /** Save search pattern. */
    uint8_t             abSearch[256];
    /** The length of the search pattern. */
    uint32_t            cbSearch;
    /** The search unit */
    uint32_t            cbSearchUnit;
    /** The max hits. */
    uint64_t            cMaxSearchHits;
    /** The address to resume searching from. */
    DBGFADDRESS         SearchAddr;
    /** What's left of the original search range. */
    RTGCUINTPTR         cbSearchRange;

    /** @name Parsing and Execution
     * @{ */

    /** Input buffer. */
    char                achInput[2048];
    /** To ease debugging. */
    unsigned            uInputZero;
    /** Write index in the input buffer. */
    unsigned            iWrite;
    /** Read index in the input buffer. */
    unsigned            iRead;
    /** The number of lines in the buffer. */
    unsigned            cInputLines;
    /** Indicates that we have a buffer overflow condition.
     * This means that input is ignored up to the next newline. */
    bool                fInputOverflow;
    /** Indicates whether or we're ready for input. */
    bool                fReady;
    /** Scratch buffer position. */
    char               *pszScratch;
    /** Scratch buffer. */
    char                achScratch[16384];
    /** Argument array position. */
    unsigned            iArg;
    /** Array of argument variables. */
    DBGCVAR             aArgs[100];

    /** rc from the last dbgcHlpPrintfV(). */
    int                 rcOutput;
    /** The last character we wrote. */
    char                chLastOutput;

    /** rc from the last command. */
    int                 rcCmd;
    /** @} */

    /** The command history file (not yet implemented). */
    char               *pszHistoryFile;
    /** The global debugger init script. */
    char               *pszGlobalInitScript;
    /** The per VM debugger init script. */
    char               *pszLocalInitScript;
} DBGC;
/** Pointer to debugger console instance data. */
typedef DBGC *PDBGC;

/** Converts a Command Helper pointer to a pointer to DBGC instance data. */
#define DBGC_CMDHLP2DBGC(pCmdHlp)   ( (PDBGC)((uintptr_t)(pCmdHlp) - RT_UOFFSETOF(DBGC, CmdHlp)) )


/**
 * Chunk of external commands.
 */
typedef struct DBGCEXTCMDS
{
    /** Number of commands descriptors. */
    unsigned            cCmds;
    /** Pointer to array of command descriptors. */
    PCDBGCCMD           paCmds;
    /** Pointer to the next chunk. */
    struct DBGCEXTCMDS *pNext;
} DBGCEXTCMDS;
/** Pointer to chunk of external commands. */
typedef DBGCEXTCMDS *PDBGCEXTCMDS;


/**
 * Chunk of external functions.
 */
typedef struct DBGCEXTFUNCS
{
    /** Number of functions descriptors. */
    uint32_t            cFuncs;
    /** Pointer to array of functions descriptors. */
    PCDBGCFUNC          paFuncs;
    /** Pointer to the next chunk. */
    struct DBGCEXTFUNCS *pNext;
} DBGCEXTFUNCS;
/** Pointer to chunk of external functions. */
typedef DBGCEXTFUNCS *PDBGCEXTFUNCS;



/**
 * Unary operator handler function.
 *
 * @returns 0 on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg        The argument.
 * @param   enmCat      The desired result category. Can be ignored.
 * @param   pResult     Where to store the result.
 */
typedef DECLCALLBACKTYPE(int, FNDBGCOPUNARY,(PDBGC pDbgc, PCDBGCVAR pArg, DBGCVARCAT enmCat, PDBGCVAR pResult));
/** Pointer to a unary operator handler function. */
typedef FNDBGCOPUNARY *PFNDBGCOPUNARY;


/**
 * Binary operator handler function.
 *
 * @returns 0 on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pDbgc       Debugger console instance data.
 * @param   pArg1       The first argument.
 * @param   pArg2       The 2nd argument.
 * @param   pResult     Where to store the result.
 */
typedef DECLCALLBACKTYPE(int, FNDBGCOPBINARY,(PDBGC pDbgc, PCDBGCVAR pArg1, PCDBGCVAR pArg2, PDBGCVAR pResult));
/** Pointer to a binary operator handler function. */
typedef FNDBGCOPBINARY *PFNDBGCOPBINARY;


/**
 * Operator descriptor.
 */
typedef struct DBGCOP
{
    /** Operator mnemonic. */
    char            szName[4];
    /** Length of name. */
    const unsigned  cchName;
    /** Whether or not this is a binary operator.
     * Unary operators are evaluated right-to-left while binary are left-to-right. */
    bool            fBinary;
    /** Precedence level. */
    unsigned        iPrecedence;
    /** Unary operator handler. */
    PFNDBGCOPUNARY  pfnHandlerUnary;
    /** Binary operator handler. */
    PFNDBGCOPBINARY pfnHandlerBinary;
    /** The category of the 1st argument.
     * Set to DBGCVAR_CAT_ANY if anything goes. */
    DBGCVARCAT      enmCatArg1;
    /** The category of the 2nd argument.
     * Set to DBGCVAR_CAT_ANY if anything goes. */
    DBGCVARCAT      enmCatArg2;
    /** Operator description. */
    const char     *pszDescription;
} DBGCOP;
/** Pointer to an operator descriptor. */
typedef DBGCOP *PDBGCOP;
/** Pointer to a const operator descriptor. */
typedef const DBGCOP *PCDBGCOP;



/** Pointer to symbol descriptor. */
typedef struct DBGCSYM *PDBGCSYM;
/** Pointer to const symbol descriptor. */
typedef const struct DBGCSYM *PCDBGCSYM;

/**
 * Get builtin symbol.
 *
 * @returns 0 on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pSymDesc    Pointer to the symbol descriptor.
 * @param   pCmdHlp     Pointer to the command callback structure.
 * @param   enmType     The result type.
 * @param   pResult     Where to store the result.
 */
typedef DECLCALLBACKTYPE(int, FNDBGCSYMGET,(PCDBGCSYM pSymDesc, PDBGCCMDHLP pCmdHlp, DBGCVARTYPE enmType, PDBGCVAR pResult));
/** Pointer to get function for a builtin symbol. */
typedef FNDBGCSYMGET *PFNDBGCSYMGET;

/**
 * Set builtin symbol.
 *
 * @returns 0 on success.
 * @returns VBox evaluation / parsing error code on failure.
 *          The caller does the bitching.
 * @param   pSymDesc    Pointer to the symbol descriptor.
 * @param   pCmdHlp     Pointer to the command callback structure.
 * @param   pValue      The value to assign the symbol.
 */
typedef DECLCALLBACKTYPE(int, FNDBGCSYMSET,(PCDBGCSYM pSymDesc, PDBGCCMDHLP pCmdHlp, PCDBGCVAR pValue));
/** Pointer to set function for a builtin symbol. */
typedef FNDBGCSYMSET *PFNDBGCSYMSET;


/**
 * Symbol description (for builtin symbols).
 */
typedef struct DBGCSYM
{
    /** Symbol name. */
    const char     *pszName;
    /** Get function. */
    PFNDBGCSYMGET   pfnGet;
    /** Set function. (NULL if readonly) */
    PFNDBGCSYMSET   pfnSet;
    /** User data. */
    unsigned        uUser;
} DBGCSYM;


/** Selectable debug event kind. */
typedef enum
{
    kDbgcSxEventKind_Plain,
    kDbgcSxEventKind_Interrupt
} DBGCSXEVENTKIND;

/**
 * Selectable debug event name / type lookup table entry.
 *
 * This also contains the default setting and an alternative name.
 */
typedef struct DBGCSXEVT
{
    /** The event type. */
    DBGFEVENTTYPE   enmType;
    /** The event name. */
    const char     *pszName;
    /** Alternative event name (optional). */
    const char     *pszAltNm;
    /** The kind of event. */
    DBGCSXEVENTKIND enmKind;
    /** The default state. */
    DBGCEVTSTATE    enmDefault;
    /** Flags, DBGCSXEVT_F_XXX. */
    uint32_t        fFlags;
    /** Description for use when reporting the event, optional. */
    const char     *pszDesc;
} DBGCSXEVT;
/** Pointer to a constant selectable debug event descriptor. */
typedef DBGCSXEVT const *PCDBGCSXEVT;

/** @name DBGCSXEVT_F_XXX
 * @{ */
#define DBGCSXEVT_F_TAKE_ARG        RT_BIT_32(0)
/** Windows bugcheck, should take 5 arguments. */
#define DBGCSXEVT_F_BUGCHECK        RT_BIT_32(1)
/** @} */


/**
 * Control flow graph basic block dumper state
 */
typedef struct DBGCFLOWBBDUMP
{
    /** The basic block referenced. */
    DBGFFLOWBB              hFlowBb;
    /** Cached start address. */
    DBGFADDRESS             AddrStart;
    /** Target address. */
    DBGFADDRESS             AddrTarget;
    /** Width of the basic block in chars. */
    uint32_t                cchWidth;
    /** Height of the basic block in chars. */
    uint32_t                cchHeight;
    /** X coordinate of the start. */
    uint32_t                uStartX;
    /** Y coordinate of the start. */
    uint32_t                uStartY;
} DBGCFLOWBBDUMP;
/** Pointer to the control flow graph basic block dump state. */
typedef DBGCFLOWBBDUMP *PDBGCFLOWBBDUMP;


/**
 * Control flow graph branch table dumper state.
 */
typedef struct DBGCFLOWBRANCHTBLDUMP
{
    /** The branch table referenced. */
    DBGFFLOWBRANCHTBL       hFlowBranchTbl;
    /** Cached start address. */
    DBGFADDRESS             AddrStart;
    /** Width of the branch table in chars. */
    uint32_t                cchWidth;
    /** Height of the branch table in chars. */
    uint32_t                cchHeight;
    /** X coordinate of the start. */
    uint32_t                uStartX;
    /** Y coordinate of the start. */
    uint32_t                uStartY;
} DBGCFLOWBRANCHTBLDUMP;
/** Pointer to control flow graph branch table state. */
typedef DBGCFLOWBRANCHTBLDUMP *PDBGCFLOWBRANCHTBLDUMP;


/*******************************************************************************
*   Internal Functions                                                         *
*******************************************************************************/
int     dbgcBpAdd(PDBGC pDbgc, RTUINT iBp, const char *pszCmd);
int     dbgcBpUpdate(PDBGC pDbgc, RTUINT iBp, const char *pszCmd);
int     dbgcBpDelete(PDBGC pDbgc, RTUINT iBp);
PDBGCBP dbgcBpGet(PDBGC pDbgc, RTUINT iBp);
int     dbgcBpExec(PDBGC pDbgc, RTUINT iBp);

DECLHIDDEN(PDBGCTFLOW) dbgcFlowTraceModGet(PDBGC pDbgc, uint32_t iTraceFlowMod);
DECLHIDDEN(int) dbgcFlowTraceModAdd(PDBGC pDbgc, DBGFFLOWTRACEMOD hFlowTraceMod, DBGFFLOW hFlow, uint32_t *piId);
DECLHIDDEN(int) dbgcFlowTraceModDelete(PDBGC pDbgc, uint32_t iFlowTraceMod);

void    dbgcEvalInit(void);
int     dbgcEvalSub(PDBGC pDbgc, char *pszExpr, size_t cchExpr, DBGCVARCAT enmCategory, PDBGCVAR pResult);
int     dbgcEvalCommand(PDBGC pDbgc, char *pszCmd, size_t cchCmd, bool fNoExecute);
int     dbgcEvalCommands(PDBGC pDbgc, char *pszCmds, size_t cchCmds, bool fNoExecute);
int     dbgcEvalScript(PDBGC pDbgc, const char *pszFilename, bool fAnnounce);

int     dbgcSymbolGet(PDBGC pDbgc, const char *pszSymbol, DBGCVARTYPE enmType, PDBGCVAR pResult);
PCDBGCSYM   dbgcLookupRegisterSymbol(PDBGC pDbgc, const char *pszSymbol);
PCDBGCOP    dbgcOperatorLookup(PDBGC pDbgc, const char *pszExpr, bool fPreferBinary, char chPrev);
PCDBGCCMD   dbgcCommandLookup(PDBGC pDbgc, const char *pachName, size_t cchName, bool fExternal);
PCDBGCFUNC  dbgcFunctionLookup(PDBGC pDbgc, const char *pachName, size_t cchName, bool fExternal);

DECLCALLBACK(int) dbgcOpRegister(PDBGC pDbgc, PCDBGCVAR pArg, DBGCVARCAT enmCat, PDBGCVAR pResult);
DECLCALLBACK(int) dbgcOpAddrFlat(PDBGC pDbgc, PCDBGCVAR pArg, DBGCVARCAT enmCat, PDBGCVAR pResult);
DECLCALLBACK(int) dbgcOpAddrHost(PDBGC pDbgc, PCDBGCVAR pArg, DBGCVARCAT enmCat, PDBGCVAR pResult);
DECLCALLBACK(int) dbgcOpAddrPhys(PDBGC pDbgc, PCDBGCVAR pArg, DBGCVARCAT enmCat, PDBGCVAR pResult);
DECLCALLBACK(int) dbgcOpAddrHostPhys(PDBGC pDbgc, PCDBGCVAR pArg, DBGCVARCAT enmCat, PDBGCVAR pResult);

void    dbgcInitCmdHlp(PDBGC pDbgc);

void    dbgcEventInit(PDBGC pDbgc);
void    dbgcEventTerm(PDBGC pDbgc);

/** Console ASCII screen handle. */
typedef struct DBGCSCREENINT *DBGCSCREEN;
/** Pointer to ASCII screen handle. */
typedef DBGCSCREEN *PDBGCSCREEN;

/**
 * ASCII screen blit callback.
 *
 * @returns VBox status code. Any non VINF_SUCCESS status code will abort the dumping.
 *
 * @param   psz             The string to dump
 * @param   pvUser          Opaque user data.
 */
typedef DECLCALLBACKTYPE(int, FNDGCSCREENBLIT,(const char *psz, void *pvUser));
/** Pointer to a FNDGCSCREENBLIT. */
typedef FNDGCSCREENBLIT *PFNDGCSCREENBLIT;

/**
 * ASCII screen supported colors.
 */
typedef enum DBGCSCREENCOLOR
{
    /** Invalid color. */
    DBGCSCREENCOLOR_INVALID = 0,
    /** Default color of the terminal. */
    DBGCSCREENCOLOR_DEFAULT,
    /** Black. */
    DBGCSCREENCOLOR_BLACK,
    DBGCSCREENCOLOR_BLACK_BRIGHT,
    /** Red. */
    DBGCSCREENCOLOR_RED,
    DBGCSCREENCOLOR_RED_BRIGHT,
    /** Green. */
    DBGCSCREENCOLOR_GREEN,
    DBGCSCREENCOLOR_GREEN_BRIGHT,
    /** Yellow. */
    DBGCSCREENCOLOR_YELLOW,
    DBGCSCREENCOLOR_YELLOW_BRIGHT,
    /** Blue. */
    DBGCSCREENCOLOR_BLUE,
    DBGCSCREENCOLOR_BLUE_BRIGHT,
    /** Magenta. */
    DBGCSCREENCOLOR_MAGENTA,
    DBGCSCREENCOLOR_MAGENTA_BRIGHT,
    /** Cyan. */
    DBGCSCREENCOLOR_CYAN,
    DBGCSCREENCOLOR_CYAN_BRIGHT,
    /** White. */
    DBGCSCREENCOLOR_WHITE,
    DBGCSCREENCOLOR_WHITE_BRIGHT
} DBGCSCREENCOLOR;
/** Pointer to a screen color. */
typedef DBGCSCREENCOLOR *PDBGCSCREENCOLOR;

DECLHIDDEN(int)  dbgcScreenAsciiCreate(PDBGCSCREEN phScreen, uint32_t cchWidth, uint32_t cchHeight);
DECLHIDDEN(void) dbgcScreenAsciiDestroy(DBGCSCREEN hScreen);
DECLHIDDEN(int)  dbgcScreenAsciiBlit(DBGCSCREEN hScreen, PFNDGCSCREENBLIT pfnBlit, void *pvUser, bool fAddColors);
DECLHIDDEN(int)  dbgcScreenAsciiDrawLineVertical(DBGCSCREEN hScreen, uint32_t uX, uint32_t uStartY,
                                                 uint32_t uEndY, char ch, DBGCSCREENCOLOR enmColor);
DECLHIDDEN(int)  dbgcScreenAsciiDrawLineHorizontal(DBGCSCREEN hScreen, uint32_t uStartX, uint32_t uEndX,
                                                   uint32_t uY, char ch, DBGCSCREENCOLOR enmColor);
DECLHIDDEN(int)  dbgcScreenAsciiDrawCharacter(DBGCSCREEN hScreen, uint32_t uX, uint32_t uY, char ch,
                                              DBGCSCREENCOLOR enmColor);
DECLHIDDEN(int)  dbgcScreenAsciiDrawString(DBGCSCREEN hScreen, uint32_t uX, uint32_t uY, const char *pszText,
                                           DBGCSCREENCOLOR enmColor);

/* For tstDBGCParser: */
int     dbgcCreate(PDBGC *ppDbgc, PCDBGCIO pIo, unsigned fFlags);
int     dbgcRun(PDBGC pDbgc);
int     dbgcProcessInput(PDBGC pDbgc, bool fNoExecute);
void    dbgcDestroy(PDBGC pDbgc);

DECLHIDDEN(const char *) dbgcGetEventCtx(DBGFEVENTCTX enmCtx);
DECLHIDDEN(PCDBGCSXEVT) dbgcEventLookup(DBGFEVENTTYPE enmType);

DECL_HIDDEN_CALLBACK(int) dbgcGdbStubRunloop(PUVM pUVM, PCDBGCIO pIo, unsigned fFlags);
DECL_HIDDEN_CALLBACK(int) dbgcKdStubRunloop(PUVM pUVM, PCDBGCIO pIo, unsigned fFlags);


extern FNDBGCCMD dbgcCmdDumpImage;
extern FNDBGCCMD dbgcCmdNtRbTree;


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
extern const DBGCCMD    g_aDbgcCmds[];
extern const uint32_t   g_cDbgcCmds;
extern const DBGCFUNC   g_aDbgcFuncs[];
extern const uint32_t   g_cDbgcFuncs;
extern const DBGCCMD    g_aCmdsCodeView[];
extern const uint32_t   g_cCmdsCodeView;
extern const DBGCFUNC   g_aFuncsCodeView[];
extern const uint32_t   g_cFuncsCodeView;
extern const DBGCOP     g_aDbgcOps[];
extern const uint32_t   g_cDbgcOps;
extern const DBGCSXEVT  g_aDbgcSxEvents[];
extern const uint32_t   g_cDbgcSxEvents;


/*******************************************************************************
*   Defined Constants And Macros                                               *
*******************************************************************************/
/** Locks the g_pExtCmdsHead and g_pExtFuncsHead lists for reading. */
#define DBGCEXTLISTS_LOCK_RD()      do { } while (0)
/** Locks the g_pExtCmdsHead and g_pExtFuncsHead lists for writing. */
#define DBGCEXTLISTS_LOCK_WR()      do { } while (0)
/** UnLocks the g_pExtCmdsHead and g_pExtFuncsHead lists after reading. */
#define DBGCEXTLISTS_UNLOCK_RD()    do { } while (0)
/** UnLocks the g_pExtCmdsHead and g_pExtFuncsHead lists after writing. */
#define DBGCEXTLISTS_UNLOCK_WR()    do { } while (0)



#endif /* !DEBUGGER_INCLUDED_SRC_DBGCInternal_h */

