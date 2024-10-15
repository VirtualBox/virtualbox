/* $Id: ntBldSymDb.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * IPRT - RTDirCreateUniqueNumbered, generic implementation.
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


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#include <iprt/win/windows.h>
#include <iprt/win/dbghelp.h>

#include <iprt/alloca.h>
#include <iprt/dir.h>
#include <iprt/file.h>
#include <iprt/err.h>
#include <iprt/getopt.h>
#include <iprt/initterm.h>
#include <iprt/list.h>
#include <iprt/mem.h>
#include <iprt/message.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/utf16.h>

#include "r0drv/nt/symdb.h"


/*********************************************************************************************************************************
*   Structures and Typedefs                                                                                                      *
*********************************************************************************************************************************/
/** A structure member we're interested in. */
typedef struct MYMEMBER
{
    /** The member name. */
    const char * const          pszName;
    /** Reserved.  */
    uint32_t    const           fFlags;
    /** The offset of the member. UINT32_MAX if not found. */
    uint32_t                    off;
    /** The size of the member. */
    uint32_t                    cb;
    /** Alternative names, optional.
     * This is a string of zero terminated strings, ending with an zero length
     * string (or double '\\0' if you like). */
    const char * const          pszzAltNames;
} MYMEMBER;
/** Pointer to a member we're interested. */
typedef MYMEMBER *PMYMEMBER;

/** Members we're interested in. */
typedef struct MYSTRUCT
{
    /** The structure name. */
    const char * const          pszName;
    /** Array of members we're interested in. */
    MYMEMBER                   *paMembers;
    /** The number of members we're interested in. */
    uint32_t const              cMembers;
    /** Reserved.  */
    uint32_t const              fFlags;
} MYSTRUCT;

/** Architecture. */
typedef enum MYARCH
{
    MYARCH_X86,
    MYARCH_AMD64,
    MYARCH_DETECT
} MYARCH;

/** Set of structures for one kernel. */
typedef struct MYSET
{
    /** The list entry. */
    RTLISTNODE      ListEntry;
    /** The source PDB. */
    char           *pszPdb;
    /** The OS version we've harvested structs for */
    RTNTSDBOSVER    OsVerInfo;
    /** The architecture. */
    MYARCH          enmArch;
    /** The structures and their member. */
    MYSTRUCT        aStructs[1];
} MYSET;
/** Pointer a set of structures for one kernel. */
typedef MYSET *PMYSET;


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/** Verbosity level (-v, --verbose). */
static uint32_t g_iOptVerbose = 1;
/** Set if we should force ahead despite errors. */
static bool     g_fOptForce = false;

/** The members of the KPRCB structure that we're interested in. */
static MYMEMBER g_aKprcbMembers[] =
{
    { "QuantumEnd",         0,  UINT32_MAX, UINT32_MAX, NULL },
    { "DpcQueueDepth",      0,  UINT32_MAX, UINT32_MAX, "DpcData[0].DpcQueueDepth\0" },
    { "VendorString",       0,  UINT32_MAX, UINT32_MAX, NULL },
};

/** The structures we're interested in. */
static MYSTRUCT g_aStructs[] =
{
    { "_KPRCB", &g_aKprcbMembers[0], RT_ELEMENTS(g_aKprcbMembers), 0 },
};

/** List of data we've found. This is sorted by version info. */
static RTLISTANCHOR g_SetList;





/**
 * For debug/verbose output.
 *
 * @param   pszFormat           The format string.
 * @param   ...                 The arguments referenced in the format string.
 */
static void MyDbgPrintf(const char *pszFormat, ...)
{
    if (g_iOptVerbose > 1)
    {
        va_list va;
        va_start(va, pszFormat);
        RTPrintf("debug: ");
        RTPrintfV(pszFormat, va);
        va_end(va);
    }
}


/**
 * Returns the name we wish to use in the C code.
 * @returns Structure name.
 * @param   pStruct             The structure descriptor.
 */
static const char *figureCStructName(MYSTRUCT const *pStruct)
{
    const char *psz = pStruct->pszName;
    while (*psz == '_')
        psz++;
    return psz;
}


/**
 * Returns the name we wish to use in the C code.
 * @returns Member name.
 * @param   pMember             The member descriptor.
 */
static const char *figureCMemberName(MYMEMBER const *pMember)
{
    return pMember->pszName;
}


/**
 * Creates a MYSET with copies of all the data and inserts it into the
 * g_SetList in a orderly fashion.
 *
 * @param   pOut        The output stream.
 */
static void generateHeader(PRTSTREAM pOut)
{
    const char *pszYear = __DATE__;
    pszYear += strlen(pszYear) - 4;
    RTStrmPrintf(pOut,
                 "/* $" "I" "d" ": $ */\n" /* avoid it being expanded */
                 "/** @file\n"
                 " * IPRT - NT kernel type helpers - Autogenerated, do NOT edit.\n"
                 " */\n"
                 "\n"
                 "/*\n"
                 " * Copyright (C) 2013-%s Oracle and/or its affiliates.\n"
                 " *\n"
                 " * This file is part of VirtualBox base platform packages, as\n"
                 " * available from https://www.virtualbox.org.\n"
                 " *\n"
                 " * This program is free software; you can redistribute it and/or\n"
                 " * modify it under the terms of the GNU General Public License\n"
                 " * as published by the Free Software Foundation, in version 3 of the\n"
                 " * License.\n"
                 " *\n"
                 " * This program is distributed in the hope that it will be useful, but\n"
                 " * WITHOUT ANY WARRANTY; without even the implied warranty of\n"
                 " * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU\n"
                 " * General Public License for more details.\n"
                 " *\n"
                 " * You should have received a copy of the GNU General Public License\n"
                 " * along with this program; if not, see <https://www.gnu.org/licenses>.\n"
                 " *\n"
                 " * The contents of this file may alternatively be used under the terms\n"
                 " * of the Common Development and Distribution License Version 1.0\n"
                 " * (CDDL), a copy of it is provided in the \"COPYING.CDDL\" file included\n"
                 " * in the VirtualBox distribution, in which case the provisions of the\n"
                 " * CDDL are applicable instead of those of the GPL.\n"
                 " *\n"
                 " * You may elect to license modified versions of this file under the\n"
                 " * terms and conditions of either the GPL or the CDDL or both.\n"
                 " *\n"
                 " * SPDX-License-Identifier: GPL-3.0-only OR CDDL-1.0\n"
                 " */\n"
                 "\n"
                 "\n"
                 "#ifndef IPRT_INCLUDED_SRC_nt_symdbdata_h\n"
                 "#define IPRT_INCLUDED_SRC_nt_symdbdata_h\n"
                 "\n"
                 "#include \"r0drv/nt/symdb.h\"\n"
                 "\n"
                 , pszYear);

    /*
     * Generate types.
     */
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aStructs); i++)
    {
        const char *pszStructName = figureCStructName(&g_aStructs[i]);

        RTStrmPrintf(pOut,
                     "typedef struct RTNTSDBTYPE_%s\n"
                     "{\n",
                     pszStructName);
        PMYMEMBER paMembers = g_aStructs[i].paMembers;
        for (uint32_t j = 0; j < g_aStructs->cMembers; j++)
        {
            const char *pszMemName = figureCMemberName(&paMembers[j]);
            RTStrmPrintf(pOut,
                         "    uint32_t off%s;\n"
                         "    uint32_t cb%s;\n",
                         pszMemName, pszMemName);
        }

        RTStrmPrintf(pOut,
                     "} RTNTSDBTYPE_%s;\n"
                     "\n",
                     pszStructName);
    }

    RTStrmPrintf(pOut,
                 "\n"
                 "typedef struct RTNTSDBSET\n"
                 "{\n"
                 "    RTNTSDBOSVER%-20s OsVerInfo;\n", "");
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aStructs); i++)
    {
        const char *pszStructName = figureCStructName(&g_aStructs[i]);
        RTStrmPrintf(pOut, "    RTNTSDBTYPE_%-20s %s;\n", pszStructName, pszStructName);
    }
    RTStrmPrintf(pOut,
                 "} RTNTSDBSET;\n"
                 "typedef RTNTSDBSET const *PCRTNTSDBSET;\n"
                 "\n");

    /*
     * Output the data.
     */
    RTStrmPrintf(pOut,
                 "\n"
                 "#ifndef RTNTSDB_NO_DATA\n"
                 "const RTNTSDBSET g_artNtSdbSets[] = \n"
                 "{\n");
    PMYSET pSet;
    RTListForEach(&g_SetList, pSet, MYSET, ListEntry)
    {
        const char *pszArch = pSet->enmArch == MYARCH_AMD64 ? "AMD64" : "X86";
        RTStrmPrintf(pOut,
                     "# ifdef RT_ARCH_%s\n"
                     "    {   /* Source: %s */\n"
                     "        /*.OsVerInfo = */\n"
                     "        {\n"
                     "            /* .uMajorVer = */ %u,\n"
                     "            /* .uMinorVer = */ %u,\n"
                     "            /* .fChecked  = */ %s,\n"
                     "            /* .fSmp      = */ %s,\n"
                     "            /* .uCsdNo    = */ %u,\n"
                     "            /* .uBuildNo  = */ %u,\n"
                     "        },\n",
                     pszArch,
                     pSet->pszPdb,
                     pSet->OsVerInfo.uMajorVer,
                     pSet->OsVerInfo.uMinorVer,
                     pSet->OsVerInfo.fChecked ? "true" : "false",
                     pSet->OsVerInfo.fSmp     ? "true" : "false",
                     pSet->OsVerInfo.uCsdNo,
                     pSet->OsVerInfo.uBuildNo);
        for (uint32_t i = 0; i < RT_ELEMENTS(pSet->aStructs); i++)
        {
            const char *pszStructName = figureCStructName(&pSet->aStructs[i]);
            RTStrmPrintf(pOut,
                         "        /* .%s = */\n"
                         "        {\n", pszStructName);
            PMYMEMBER paMembers = pSet->aStructs[i].paMembers;
            for (uint32_t j = 0; j < pSet->aStructs[i].cMembers; j++)
            {
                const char *pszMemName = figureCMemberName(&paMembers[j]);
                RTStrmPrintf(pOut,
                             "            /* .off%-25s = */ %#06x,\n"
                             "            /* .cb%-26s = */ %#06x,\n",
                             pszMemName, paMembers[j].off,
                             pszMemName, paMembers[j].cb);
            }
            RTStrmPrintf(pOut,
                         "        },\n");
        }
        RTStrmPrintf(pOut,
                     "    },\n"
                     "# endif\n"
                     );
    }

    RTStrmPrintf(pOut,
                 "};\n"
                 "#endif /* !RTNTSDB_NO_DATA */\n"
                 "\n");

    RTStrmPrintf(pOut, "\n#endif\n\n");
}


/**
 * Creates a MYSET with copies of all the data and inserts it into the
 * g_SetList in a orderly fashion.
 *
 * @returns Fully complained exit code.
 * @param   pOsVerInfo      The OS version info.
 * @param   enmArch         The NT architecture of the incoming PDB.
 * @param   pszPdb          The PDB file name.
 */
static RTEXITCODE saveStructures(PRTNTSDBOSVER pOsVerInfo, MYARCH enmArch, const char *pszPdb)
{
    /*
     * Allocate one big chunk, figure it's size once.
     */
    static size_t s_cbNeeded = 0;
    if (s_cbNeeded == 0)
    {
        s_cbNeeded = RT_UOFFSETOF(MYSET, aStructs[RT_ELEMENTS(g_aStructs)]);
        for (uint32_t i = 0; i < RT_ELEMENTS(g_aStructs); i++)
            s_cbNeeded += sizeof(MYMEMBER) * g_aStructs[i].cMembers;
    }

    size_t cbPdb = strlen(pszPdb) + 1;
    PMYSET pSet = (PMYSET)RTMemAlloc(s_cbNeeded + cbPdb);
    if (!pSet)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Out of memory!\n");

    /*
     * Copy over the data.
     */
    pSet->enmArch = enmArch;
    memcpy(&pSet->OsVerInfo, pOsVerInfo, sizeof(pSet->OsVerInfo));
    memcpy(&pSet->aStructs[0], g_aStructs, sizeof(g_aStructs));

    PMYMEMBER pDst = (PMYMEMBER)&pSet->aStructs[RT_ELEMENTS(g_aStructs)];
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aStructs); i++)
    {
        pSet->aStructs[i].paMembers = pDst;
        memcpy(pDst, g_aStructs[i].paMembers, g_aStructs[i].cMembers * sizeof(*pDst));
        pDst += g_aStructs[i].cMembers;
    }

    pSet->pszPdb = (char *)pDst;
    memcpy(pDst, pszPdb, cbPdb);

    /*
     * Link it.
     */
    PMYSET pInsertBefore;
    RTListForEach(&g_SetList, pInsertBefore, MYSET, ListEntry)
    {
        int iDiff = rtNtOsVerInfoCompare(&pInsertBefore->OsVerInfo, &pSet->OsVerInfo);
        if (iDiff >= 0)
        {
            if (iDiff > 0 || pInsertBefore->enmArch > pSet->enmArch)
            {
                RTListNodeInsertBefore(&pInsertBefore->ListEntry, &pSet->ListEntry);
                return RTEXITCODE_SUCCESS;
            }
        }
    }

    RTListAppend(&g_SetList, &pSet->ListEntry);
    return RTEXITCODE_SUCCESS;
}


/**
 * Checks that we found everything.
 *
 * @returns Fully complained exit code.
 */
static RTEXITCODE checkThatWeFoundEverything(void)
{
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aStructs); i++)
    {
        PMYMEMBER paMembers = g_aStructs[i].paMembers;
        uint32_t  j         = g_aStructs[i].cMembers;
        while (j-- > 0)
        {
            if (paMembers[j].off == UINT32_MAX)
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, " Missing %s::%s\n", g_aStructs[i].pszName, paMembers[j].pszName);
        }
    }
    return rcExit;
}


/**
 * Matches the member against what we're looking for.
 *
 * @returns Number of hits.
 * @param   cWantedMembers  The number members in paWantedMembers.
 * @param   paWantedMembers The members we're looking for.
 * @param   pszPrefix       The member name prefix.
 * @param   pszMember       The member name.
 * @param   offMember       The member offset.
 * @param   cbMember        The member size.
 */
static uint32_t matchUpStructMembers(unsigned cWantedMembers, PMYMEMBER paWantedMembers,
                                     const char *pszPrefix, const char *pszMember,
                                     uint32_t offMember, uint32_t cbMember)
{
    size_t   cchPrefix = strlen(pszPrefix);
    uint32_t cHits     = 0;
    uint32_t iMember   = cWantedMembers;
    while (iMember-- > 0)
    {
        if (   !strncmp(pszPrefix, paWantedMembers[iMember].pszName, cchPrefix)
            && !strcmp(pszMember, paWantedMembers[iMember].pszName + cchPrefix))
        {
            paWantedMembers[iMember].off = offMember;
            paWantedMembers[iMember].cb  = cbMember;
            cHits++;
        }
        else if (paWantedMembers[iMember].pszzAltNames)
        {
            char const *pszCur = paWantedMembers[iMember].pszzAltNames;
            while (*pszCur)
            {
                size_t cchCur = strlen(pszCur);
                if (   !strncmp(pszPrefix, pszCur, cchPrefix)
                    && !strcmp(pszMember, pszCur + cchPrefix))
                {
                    paWantedMembers[iMember].off = offMember;
                    paWantedMembers[iMember].cb  = cbMember;
                    cHits++;
                    break;
                }
                pszCur += cchCur + 1;
            }
        }
    }
    return cHits;
}


#if 0
/**
 * Resets the writable structure members prior to processing a PDB.
 *
 * While processing the PDB, will fill in the sizes and offsets of what we find.
 * Afterwards we'll use look for reset values to see that every structure and
 * member was located successfully.
 */
static void resetMyStructs(void)
{
    for (uint32_t i = 0; i < RT_ELEMENTS(g_aStructs); i++)
    {
        PMYMEMBER paMembers = g_aStructs[i].paMembers;
        uint32_t  j         = g_aStructs[i].cMembers;
        while (j-- > 0)
        {
            paMembers[j].off = UINT32_MAX;
            paMembers[j].cb  = UINT32_MAX;
        }
    }
}
#endif


/**
 * Find members in the specified structure type (@a idxType).
 *
 * @returns Fully bitched exit code.
 * @param   hFake           Fake process handle.
 * @param   uModAddr        The module address.
 * @param   idxType         The type index of the structure which members we're
 *                          going to process.
 * @param   cWantedMembers  The number of wanted members.
 * @param   paWantedMembers The wanted members.  This will be modified.
 * @param   offDisp         Displacement when calculating member offsets.
 * @param   pszStructNm     The top level structure name.
 * @param   pszPrefix       The member name prefix.
 * @param   pszLogTag       The log tag.
 */
static RTEXITCODE findMembers(HANDLE hFake, uint64_t uModAddr, uint32_t idxType,
                              uint32_t cWantedMembers, PMYMEMBER paWantedMembers,
                              uint32_t offDisp, const char *pszStructNm, const char *pszPrefix, const char *pszLogTag)
{
    RTEXITCODE  rcExit   = RTEXITCODE_SUCCESS;

    DWORD cChildren = 0;
    if (!SymGetTypeInfo(hFake, uModAddr, idxType, TI_GET_CHILDRENCOUNT, &cChildren))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: TI_GET_CHILDRENCOUNT failed on _KPRCB: %u\n", pszLogTag, GetLastError());

    MyDbgPrintf(" %s: cChildren=%u (%#x)\n", pszStructNm, cChildren);
    TI_FINDCHILDREN_PARAMS *pChildren;
    pChildren = (TI_FINDCHILDREN_PARAMS *)alloca(RT_UOFFSETOF_DYN(TI_FINDCHILDREN_PARAMS, ChildId[cChildren]));
    pChildren->Start = 0;
    pChildren->Count = cChildren;
    if (!SymGetTypeInfo(hFake, uModAddr, idxType, TI_FINDCHILDREN, pChildren))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: TI_FINDCHILDREN failed on _KPRCB: %u\n", pszLogTag, GetLastError());

    for (uint32_t i = 0; i < cChildren; i++)
    {
        //MyDbgPrintf(" %s: child#%u: TypeIndex=%u\n", pszStructNm, i, pChildren->ChildId[i]);
        IMAGEHLP_SYMBOL_TYPE_INFO enmErr;
        PWCHAR      pwszMember = NULL;
        uint32_t    idxRefType = 0;
        uint32_t    offMember = 0;
        uint64_t    cbMember = 0;
        uint32_t    cMemberChildren = 0;
        if (   SymGetTypeInfo(hFake, uModAddr, pChildren->ChildId[i], enmErr = TI_GET_SYMNAME, &pwszMember)
            && SymGetTypeInfo(hFake, uModAddr, pChildren->ChildId[i], enmErr = TI_GET_OFFSET, &offMember)
            && SymGetTypeInfo(hFake, uModAddr, pChildren->ChildId[i], enmErr = TI_GET_TYPE, &idxRefType)
            && SymGetTypeInfo(hFake, uModAddr, idxRefType, enmErr = TI_GET_LENGTH, &cbMember)
            && SymGetTypeInfo(hFake, uModAddr, idxRefType, enmErr = TI_GET_CHILDRENCOUNT, &cMemberChildren)
            )
        {
            offMember += offDisp;

            char *pszMember;
            int rc = RTUtf16ToUtf8(pwszMember, &pszMember);
            if (RT_SUCCESS(rc))
            {
                matchUpStructMembers(cWantedMembers, paWantedMembers, pszPrefix, pszMember, offMember, cbMember);

                /*
                 * Gather more info and do some debug printing. We'll use some
                 * of this info below when recursing into sub-structures
                 * and arrays.
                 */
                uint32_t fNested      = 0; SymGetTypeInfo(hFake, uModAddr, idxRefType, TI_GET_NESTED, &fNested);
                uint32_t uDataKind    = 0; SymGetTypeInfo(hFake, uModAddr, idxRefType, TI_GET_DATAKIND, &uDataKind);
                uint32_t uBaseType    = 0; SymGetTypeInfo(hFake, uModAddr, idxRefType, TI_GET_BASETYPE, &uBaseType);
                uint32_t uMembTag     = 0; SymGetTypeInfo(hFake, uModAddr, pChildren->ChildId[i], TI_GET_SYMTAG, &uMembTag);
                uint32_t uBaseTag     = 0; SymGetTypeInfo(hFake, uModAddr, idxRefType, TI_GET_SYMTAG, &uBaseTag);
                uint32_t cElements    = 0; SymGetTypeInfo(hFake, uModAddr, idxRefType, TI_GET_COUNT, &cElements);
                uint32_t idxArrayType = 0; SymGetTypeInfo(hFake, uModAddr, idxRefType, TI_GET_ARRAYINDEXTYPEID, &idxArrayType);
                MyDbgPrintf(" %#06x LB %#06llx %c%c %2d %2d %2d %2d %2d %4d %s::%s%s\n",
                            offMember, cbMember,
                            cMemberChildren > 0 ? 'c' : '-',
                            fNested        != 0 ? 'n' : '-',
                            uDataKind,
                            uBaseType,
                            uMembTag,
                            uBaseTag,
                            cElements,
                            idxArrayType,
                            pszStructNm,
                            pszPrefix,
                            pszMember);

                /*
                 * Recurse into children.
                 */
                if (cMemberChildren > 0)
                {
                    size_t cbNeeded = strlen(pszMember) + strlen(pszPrefix) + sizeof(".");
                    char *pszSubPrefix = (char *)RTMemTmpAlloc(cbNeeded);
                    if (pszSubPrefix)
                    {
                        strcat(strcat(strcpy(pszSubPrefix, pszPrefix), pszMember), ".");
                        RTEXITCODE rcExit2 = findMembers(hFake, uModAddr, idxRefType, cWantedMembers,
                                                         paWantedMembers, offMember,
                                                         pszStructNm,
                                                         pszSubPrefix,
                                                         pszLogTag);
                        if (rcExit2 != RTEXITCODE_SUCCESS)
                            rcExit = rcExit2;
                        RTMemTmpFree(pszSubPrefix);
                    }
                    else
                        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "out of memory\n");
                }
                /*
                 * Recurse into arrays too.
                 */
                else if (cElements > 0 && idxArrayType > 0)
                {
                    BOOL fRc;
                    uint32_t idxElementRefType = 0;
                    fRc = SymGetTypeInfo(hFake, uModAddr, idxRefType, TI_GET_TYPE, &idxElementRefType); Assert(fRc);
                    uint64_t cbElement = cbMember / cElements;
                    fRc = SymGetTypeInfo(hFake, uModAddr, idxElementRefType, TI_GET_LENGTH, &cbElement); Assert(fRc);
                    MyDbgPrintf("idxArrayType=%u idxElementRefType=%u cbElement=%u\n", idxArrayType, idxElementRefType, cbElement);

                    size_t cbNeeded = strlen(pszMember) + strlen(pszPrefix) + sizeof("[xxxxxxxxxxxxxxxx].");
                    char *pszSubPrefix = (char *)RTMemTmpAlloc(cbNeeded);
                    if (pszSubPrefix)
                    {
                        for (uint32_t iElement = 0; iElement < cElements; iElement++)
                        {
                            RTStrPrintf(pszSubPrefix, cbNeeded, "%s%s[%u].", pszPrefix, pszMember, iElement);
                            RTEXITCODE rcExit2 = findMembers(hFake, uModAddr, idxElementRefType, cWantedMembers,
                                                             paWantedMembers,
                                                             offMember + iElement * cbElement,
                                                             pszStructNm,
                                                             pszSubPrefix,
                                                             pszLogTag);
                            if (rcExit2 != RTEXITCODE_SUCCESS)
                                rcExit = rcExit2;
                        }
                        RTMemTmpFree(pszSubPrefix);
                    }
                    else
                        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "out of memory\n");
                }

                RTStrFree(pszMember);
            }
            else
                rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: RTUtf16ToUtf8 failed on %s child#%u: %Rrc\n",
                                        pszLogTag, pszStructNm, i, rc);
        }
        /* TI_GET_OFFSET fails on bitfields, so just ignore+skip those. */
        else if (enmErr != TI_GET_OFFSET || GetLastError() != ERROR_INVALID_FUNCTION)
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: SymGetTypeInfo(,,,%d,) failed on %s child#%u: %u\n",
                                    pszLogTag, enmErr, pszStructNm, i, GetLastError());
        LocalFree(pwszMember);
    } /* For each child. */

    return rcExit;
}


/**
 * Lookup up structures and members in the given module.
 *
 * @returns Fully bitched exit code.
 * @param   hFake           Fake process handle.
 * @param   uModAddr        The module address.
 * @param   pszLogTag       The log tag.
 * @param   pszPdb          The full PDB path.
 * @param   pOsVerInfo      The OS version info for altering the error handling
 *                          for older OSes.
 */
static RTEXITCODE findStructures(HANDLE hFake, uint64_t uModAddr, const char *pszLogTag, const char *pszPdb,
                                 PCRTNTSDBOSVER pOsVerInfo)
{
    RTEXITCODE   rcExit   = RTEXITCODE_SUCCESS;
    PSYMBOL_INFO pSymInfo = (PSYMBOL_INFO)alloca(sizeof(*pSymInfo));
    for (uint32_t iStruct = 0; iStruct < RT_ELEMENTS(g_aStructs); iStruct++)
    {
        pSymInfo->SizeOfStruct = sizeof(*pSymInfo);
        pSymInfo->MaxNameLen   = 0;
        if (!SymGetTypeFromName(hFake, uModAddr, g_aStructs[iStruct].pszName, pSymInfo))
        {
            if (!(pOsVerInfo->uMajorVer == 5 && pOsVerInfo->uMinorVer == 0) /* w2k */)
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "%s: Failed to find _KPRCB: %u\n", pszPdb, GetLastError());
            RTMsgInfo("%s: Skipping - failed to find _KPRCB: %u\n", pszPdb, GetLastError());
            return RTEXITCODE_SKIPPED;
        }

        MyDbgPrintf(" %s: TypeIndex=%u\n", g_aStructs[iStruct].pszName, pSymInfo->TypeIndex);
        MyDbgPrintf(" %s: Size=%u (%#x)\n", g_aStructs[iStruct].pszName, pSymInfo->Size, pSymInfo->Size);

        rcExit = findMembers(hFake, uModAddr, pSymInfo->TypeIndex,
                             g_aStructs[iStruct].cMembers, g_aStructs[iStruct].paMembers, 0 /* offDisp */,
                             g_aStructs[iStruct].pszName, "", pszLogTag);
        if (rcExit != RTEXITCODE_SUCCESS)
            return rcExit;
    } /* for each struct we want */
    return rcExit;
}


#if 0 /* unused */
static bool strIEndsWith(const char *pszString, const char *pszSuffix)
{
    size_t cchString = strlen(pszString);
    size_t cchSuffix = strlen(pszSuffix);
    if (cchString < cchSuffix)
        return false;
    return RTStrICmp(pszString + cchString - cchSuffix, pszSuffix) == 0;
}
#endif


/**
 * Use various hysterics to figure out the OS version details from the PDB path.
 *
 * This ASSUMES quite a bunch of things:
 *      -# Working on unpacked symbol packages. This does not work for
 *         windbg symbol stores/caches.
 *      -# The symbol package has been unpacked into a directory with the same
 *       name as the symbol package (sans suffixes).
 *
 * @returns Fully complained exit code.
 * @param   pszPdb              The path to the PDB.
 * @param   pVerInfo            Where to return the version info.
 * @param   penmArch            Where to return the architecture.
 */
static RTEXITCODE FigurePdbVersionInfo(const char *pszPdb, PRTNTSDBOSVER pVerInfo, MYARCH *penmArch)
{
    /*
     * Split the path.
     */
    union
    {
        RTPATHSPLIT Split;
        uint8_t     abPad[RTPATH_MAX + 1024];
    } u;
    int rc = RTPathSplit(pszPdb, &u.Split, sizeof(u), 0);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTPathSplit failed on '%s': %Rrc", pszPdb, rc);
    if (!(u.Split.fProps & RTPATH_PROP_FILENAME))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTPATH_PROP_FILENAME not set for: '%s'", pszPdb);
    const char *pszFilename = u.Split.apszComps[u.Split.cComps - 1];

    /*
     * SMP or UNI kernel?
     */
    if (   !RTStrICmp(pszFilename, "ntkrnlmp.pdb")
        || !RTStrICmp(pszFilename, "ntkrpamp.pdb")
       )
        pVerInfo->fSmp = true;
    else if (   !RTStrICmp(pszFilename, "ntoskrnl.pdb")
             || !RTStrICmp(pszFilename, "ntkrnlpa.pdb")
            )
        pVerInfo->fSmp = false;
    else
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Doesn't recognize the filename '%s'...", pszFilename);

    /*
     * Look for symbol pack names in the path. This is stuff like:
     *  - WindowsVista.6002.090410-1830.x86fre
     *  - WindowsVista.6002.090410-1830.amd64chk
     *  - Windows_Win7.7600.16385.090713-1255.X64CHK
     *  - Windows_Win7SP1.7601.17514.101119-1850.AMD64FRE
     *  - Windows_Win8.9200.16384.120725-1247.X86CHK
     *  - en_windows_8_1_symbols_debug_checked_x64_2712568
     */
    uint32_t i = u.Split.cComps - 1;
    while (i-- > 0)
    {
        static struct
        {
            const char *pszPrefix;
            size_t      cchPrefix;
            uint8_t     uMajorVer;
            uint8_t     uMinorVer;
            uint8_t     uCsdNo;
            uint32_t    uBuildNo;   /**< UINT32_MAX means the number immediately after the prefix. */
        } const s_aSymPacks[] =
        {
            { RT_STR_TUPLE("w2kSP1SYM"),                        5, 0, 1, 2195 },
            { RT_STR_TUPLE("w2ksp2srp1"),                       5, 0, 2, 2195 },
            { RT_STR_TUPLE("w2ksp2sym"),                        5, 0, 2, 2195 },
            { RT_STR_TUPLE("w2ksp3sym"),                        5, 0, 3, 2195 },
            { RT_STR_TUPLE("w2ksp4sym"),                        5, 0, 4, 2195 },
            { RT_STR_TUPLE("Windows2000-KB891861"),             5, 0, 4, 2195 },
            { RT_STR_TUPLE("windowsxp"),                        5, 1, 0, 2600 },
            { RT_STR_TUPLE("xpsp1sym"),                         5, 1, 1, 2600 },
            { RT_STR_TUPLE("WindowsXP-KB835935-SP2-"),          5, 1, 2, 2600 },
            { RT_STR_TUPLE("WindowsXP-KB936929-SP3-"),          5, 1, 3, 2600 },
            { RT_STR_TUPLE("Windows2003."),                     5, 2, 0, 3790 },
            { RT_STR_TUPLE("Windows2003_sp1."),                 5, 2, 1, 3790 },
            { RT_STR_TUPLE("WindowsServer2003-KB933548-v1"),    5, 2, 1, 3790 },
            { RT_STR_TUPLE("WindowsVista.6000."),               6, 0, 0, 6000 },
            { RT_STR_TUPLE("Windows_Longhorn.6001."),           6, 0, 1, 6001 }, /* incl w2k8 */
            { RT_STR_TUPLE("WindowsVista.6002."),               6, 0, 2, 6002 }, /* incl w2k8 */
            { RT_STR_TUPLE("Windows_Winmain.7000"),             6, 1, 0, 7000 }, /* Beta */
            { RT_STR_TUPLE("Windows_Winmain.7100"),             6, 1, 0, 7100 }, /* RC */
            { RT_STR_TUPLE("Windows_Win7.7600"),                6, 1, 0, 7600 }, /* RC */
            { RT_STR_TUPLE("Windows_Win7SP1.7601"),             6, 1, 1, 7601 }, /* RC */
            { RT_STR_TUPLE("Windows_Winmain.8102"),             6, 2, 0, 8102 }, /* preview */
            { RT_STR_TUPLE("Windows_Winmain.8250"),             6, 2, 0, 8250 }, /* beta */
            { RT_STR_TUPLE("Windows_Winmain.8400"),             6, 2, 0, 8400 }, /* RC */
            { RT_STR_TUPLE("Windows_Win8.9200"),                6, 2, 0, 9200 }, /* RTM */
            { RT_STR_TUPLE("en_windows_8_1"),                   6, 3, 0, 9600 }, /* RTM */
            { RT_STR_TUPLE("en_windows_10_symbols_"),          10, 0, 0,10240 }, /* RTM */
            { RT_STR_TUPLE("en_windows_10_symbols_"),          10, 0, 0,10240 }, /* RTM */
            { RT_STR_TUPLE("en_windows_10_17134_"),            10, 0, 0,17134 }, /* 1803 */
        };

        const char *pszComp  = u.Split.apszComps[i];
        uint32_t    iSymPack = RT_ELEMENTS(s_aSymPacks);
        while (iSymPack-- > 0)
            if (!RTStrNICmp(pszComp, s_aSymPacks[iSymPack].pszPrefix, s_aSymPacks[iSymPack].cchPrefix))
                break;
        if (iSymPack >= RT_ELEMENTS(s_aSymPacks))
            continue;

        pVerInfo->uMajorVer = s_aSymPacks[iSymPack].uMajorVer;
        pVerInfo->uMinorVer = s_aSymPacks[iSymPack].uMinorVer;
        pVerInfo->uCsdNo    = s_aSymPacks[iSymPack].uCsdNo;
        pVerInfo->fChecked  = false;
        pVerInfo->uBuildNo  = s_aSymPacks[iSymPack].uBuildNo;

        /* Parse build number if necessary. */
        if (s_aSymPacks[iSymPack].uBuildNo == UINT32_MAX)
        {
            char *pszNext;
            rc = RTStrToUInt32Ex(pszComp + s_aSymPacks[iSymPack].cchPrefix, &pszNext, 10, &pVerInfo->uBuildNo);
            if (RT_FAILURE(rc))
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to decode build number in '%s': %Rrc", pszComp, rc);
            if (*pszNext != '.' && *pszNext != '_' && *pszNext != '-')
                return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to decode build number in '%s': '%c'", pszComp, *pszNext);
        }

        /* Look for build arch and checked/free. */
        if (   RTStrIStr(pszComp, ".x86.chk.")
            || RTStrIStr(pszComp, ".x86chk.")
            || RTStrIStr(pszComp, "_x86_chk_")
            || RTStrIStr(pszComp, "_x86chk_")
            || RTStrIStr(pszComp, "-x86-DEBUG")
            || (RTStrIStr(pszComp, "-x86-") && RTStrIStr(pszComp, "-DEBUG"))
            || RTStrIStr(pszComp, "_debug_checked_x86")
           )
        {
            pVerInfo->fChecked = true;
            *penmArch = MYARCH_X86;
        }
        else if (   RTStrIStr(pszComp, ".amd64.chk.")
                 || RTStrIStr(pszComp, ".amd64chk.")
                 || RTStrIStr(pszComp, ".x64.chk.")
                 || RTStrIStr(pszComp, ".x64chk.")
                 || RTStrIStr(pszComp, "_debug_checked_x64")
                )
        {
            pVerInfo->fChecked = true;
            *penmArch = MYARCH_AMD64;
        }
        else if (   RTStrIStr(pszComp, ".amd64.fre.")
                 || RTStrIStr(pszComp, ".amd64fre.")
                 || RTStrIStr(pszComp, ".x64.fre.")
                 || RTStrIStr(pszComp, ".x64fre.")
                )
        {
            pVerInfo->fChecked = false;
            *penmArch = MYARCH_AMD64;
        }
        else if (   RTStrIStr(pszComp, "DEBUG")
                 || RTStrIStr(pszComp, "_chk")
                 )
        {
            pVerInfo->fChecked = true;
            *penmArch = MYARCH_X86;
        }
        else if (RTStrIStr(pszComp, "_x64"))
        {
            pVerInfo->fChecked = false;
            *penmArch = MYARCH_AMD64;
        }
        else
        {
            pVerInfo->fChecked = false;
            *penmArch = MYARCH_X86;
        }
        return RTEXITCODE_SUCCESS;
    }

    return RTMsgErrorExit(RTEXITCODE_FAILURE, "Giving up on '%s'...\n", pszPdb);
}


/**
 * Process one PDB.
 *
 * @returns Fully bitched exit code.
 * @param   pszPdb              The path to the PDB.
 */
static RTEXITCODE processPdb(const char *pszPdb)
{
    /*
     * We need the size later on, so get that now and present proper IPRT error
     * info if the file is missing or inaccessible.
     */
    RTFSOBJINFO ObjInfo;
    int rc = RTPathQueryInfoEx(pszPdb, &ObjInfo, RTFSOBJATTRADD_NOTHING, RTPATH_F_FOLLOW_LINK);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTPathQueryInfo fail on '%s': %Rrc\n", pszPdb, rc);

    /*
     * Figure the windows version details for the given PDB.
     */
    MYARCH       enmArch;
    RTNTSDBOSVER OsVerInfo;
    RTEXITCODE rcExit = FigurePdbVersionInfo(pszPdb, &OsVerInfo, &enmArch);
    if (rcExit != RTEXITCODE_SUCCESS)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Failed to figure the OS version info for '%s'.\n'", pszPdb);

    /*
     * Create a fake handle and open the PDB.
     */
    static uintptr_t s_iHandle = 0;
    HANDLE hFake = (HANDLE)++s_iHandle;
    if (!SymInitialize(hFake, NULL, FALSE))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "SymInitialied failed: %u\n", GetLastError());

    uint64_t uModAddr = UINT64_C(0x1000000);
    uModAddr = SymLoadModuleEx(hFake, NULL /*hFile*/, pszPdb, NULL /*pszModuleName*/,
                               uModAddr, ObjInfo.cbObject, NULL /*pData*/, 0 /*fFlags*/);
    if (uModAddr != 0)
    {
        MyDbgPrintf("*** uModAddr=%#llx \"%s\" ***\n", uModAddr, pszPdb);

        char szLogTag[32];
        RTStrCopy(szLogTag, sizeof(szLogTag), RTPathFilename(pszPdb));

        /*
         * Find the structures.
         */
        rcExit = findStructures(hFake, uModAddr, szLogTag, pszPdb, &OsVerInfo);
        if (rcExit == RTEXITCODE_SUCCESS)
            rcExit = checkThatWeFoundEverything();
        if (rcExit == RTEXITCODE_SUCCESS)
        {
            /*
             * Save the details for later when we produce the header.
             */
            rcExit = saveStructures(&OsVerInfo, enmArch, pszPdb);
        }
    }
    else
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "SymLoadModuleEx failed: %u\n", GetLastError());

    if (!SymCleanup(hFake))
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "SymCleanup failed: %u\n", GetLastError());

    if (rcExit == RTEXITCODE_SKIPPED)
        rcExit = RTEXITCODE_SUCCESS;
    return rcExit;
}


/** The size of the directory entry buffer we're using.  */
#define MY_DIRENTRY_BUF_SIZE (sizeof(RTDIRENTRYEX) + RTPATH_MAX)

/**
 * Checks if the name is of interest to us.
 *
 * @returns true/false.
 * @param   pszName             The name.
 * @param   cchName             The length of the name.
 */
static bool isInterestingName(const char *pszName, size_t cchName)
{
    static struct { const char *psz; size_t cch; } const s_aNames[] =
    {
        { RT_STR_TUPLE("ntoskrnl.pdb") },
        { RT_STR_TUPLE("ntkrnlmp.pdb") },
        { RT_STR_TUPLE("ntkrnlpa.pdb") },
        { RT_STR_TUPLE("ntkrpamp.pdb") },
    };

    if (   cchName == s_aNames[0].cch
        && (pszName[0] == 'n' || pszName[0] == 'N')
        && (pszName[1] == 't' || pszName[1] == 'T')
       )
    {
        int i = RT_ELEMENTS(s_aNames);
        while (i-- > 0)
            if (   s_aNames[i].cch == cchName
                && !RTStrICmp(s_aNames[i].psz, pszName))
                return true;
    }
    return false;
}


/**
 * Recursively processes relevant files in the specified directory.
 *
 * @returns Fully complained exit code.
 * @param   pszDir              Pointer to the directory buffer.
 * @param   cchDir              The length of pszDir in pszDir.
 * @param   pDirEntry           Pointer to the directory buffer.
 * @param   iLogDepth           The logging depth.
 */
static RTEXITCODE processDirSub(char *pszDir, size_t cchDir, PRTDIRENTRYEX pDirEntry, int iLogDepth)
{
    Assert(cchDir > 0); Assert(pszDir[cchDir] == '\0');

    /* Make sure we've got some room in the path, to save us extra work further down. */
    if (cchDir + 3 >= RTPATH_MAX)
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Path too long: '%s'\n", pszDir);

    /* Open directory. */
    RTDIR hDir;
    int rc = RTDirOpen(&hDir, pszDir);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTDirOpen failed on '%s': %Rrc\n", pszDir, rc);

    /* Ensure we've got a trailing slash (there is space for it see above). */
    if (!RTPATH_IS_SEP(pszDir[cchDir - 1]))
    {
        pszDir[cchDir++] = RTPATH_SLASH;
        pszDir[cchDir]   = '\0';
    }

    /*
     * Process the files and subdirs.
     */
    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    for (;;)
    {
        /* Get the next directory. */
        size_t cbDirEntry = MY_DIRENTRY_BUF_SIZE;
        rc = RTDirReadEx(hDir, pDirEntry, &cbDirEntry, RTFSOBJATTRADD_UNIX, RTPATH_F_ON_LINK);
        if (RT_FAILURE(rc))
            break;

        /* Skip the dot and dot-dot links. */
        if (RTDirEntryExIsStdDotLink(pDirEntry))
            continue;

        /* Check length. */
        if (pDirEntry->cbName + cchDir + 3 >= RTPATH_MAX)
        {
            rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "Path too long: '%s' in '%.*s'\n", pDirEntry->szName, cchDir, pszDir);
            break;
        }

        if (RTFS_IS_FILE(pDirEntry->Info.Attr.fMode))
        {
            /*
             * Process debug info files of interest.
             */
            if (isInterestingName(pDirEntry->szName, pDirEntry->cbName))
            {
                memcpy(&pszDir[cchDir], pDirEntry->szName, pDirEntry->cbName + 1);
                RTEXITCODE rcExit2 = processPdb(pszDir);
                if (rcExit2 != RTEXITCODE_SUCCESS)
                    rcExit = rcExit2;
            }
        }
        else if (RTFS_IS_DIRECTORY(pDirEntry->Info.Attr.fMode))
        {
            /*
             * Recurse into the subdirectory.  In order to speed up Win7+
             * symbol pack traversals, we skip directories with ".pdb" suffixes
             * unless they match any of the .pdb files we're looking for.
             *
             * Note! When we get back pDirEntry will be invalid.
             */
            if (   pDirEntry->cbName <= 4
                || RTStrICmp(&pDirEntry->szName[pDirEntry->cbName - 4], ".pdb")
                || isInterestingName(pDirEntry->szName, pDirEntry->cbName))
            {
                memcpy(&pszDir[cchDir], pDirEntry->szName, pDirEntry->cbName + 1);
                if (iLogDepth > 0)
                    RTMsgInfo("%s%s ...\n", pszDir, RTPATH_SLASH_STR);
                RTEXITCODE rcExit2 = processDirSub(pszDir, cchDir + pDirEntry->cbName, pDirEntry, iLogDepth - 1);
                if (rcExit2 != RTEXITCODE_SUCCESS)
                    rcExit = rcExit2;
            }
        }
    }
    if (rc != VERR_NO_MORE_FILES)
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "RTDirReadEx failed: %Rrc\npszDir=%.*s", rc, cchDir, pszDir);

    rc = RTDirClose(hDir);
    if (RT_FAILURE(rc))
        rcExit = RTMsgErrorExit(RTEXITCODE_FAILURE, "RTDirClose failed: %Rrc\npszDir=%.*s", rc, cchDir, pszDir);
    return rcExit;
}


/**
 * Recursively processes relevant files in the specified directory.
 *
 * @returns Fully complained exit code.
 * @param   pszDir              The directory to search.
 */
static RTEXITCODE processDir(const char *pszDir)
{
    char szPath[RTPATH_MAX];
    int rc = RTPathAbs(pszDir, szPath, sizeof(szPath));
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "RTPathAbs failed on '%s': %Rrc\n", pszDir, rc);

    union
    {
        uint8_t         abPadding[MY_DIRENTRY_BUF_SIZE];
        RTDIRENTRYEX    DirEntry;
    } uBuf;
    return processDirSub(szPath, strlen(szPath), &uBuf.DirEntry, g_iOptVerbose);
}


int main(int argc, char **argv)
{
    int rc = RTR3InitExe(argc, &argv, 0 /*fFlags*/);
    if (RT_FAILURE(rc))
        return RTMsgInitFailure(rc);

    RTListInit(&g_SetList);

    /*
     * Parse options.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--force",            'f', RTGETOPT_REQ_NOTHING },
        { "--output",           'o', RTGETOPT_REQ_STRING  },
        { "--verbose",          'v', RTGETOPT_REQ_NOTHING },
        { "--quiet",            'q', RTGETOPT_REQ_NOTHING },
    };

    RTEXITCODE  rcExit      = RTEXITCODE_SUCCESS;
    const char *pszOutput   = "-";

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, argc, argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1,
                 RTGETOPTINIT_FLAGS_OPTS_FIRST);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (ch)
        {
            case 'f':
                g_fOptForce = true;
                break;

            case 'v':
                g_iOptVerbose++;
                break;

            case 'q':
                g_iOptVerbose++;
                break;

            case 'o':
                pszOutput = ValueUnion.psz;
                break;

            case 'V':
                RTPrintf("$Revision: 106320 $");
                break;

            case 'h':
                RTPrintf("usage: %s [-v|--verbose] [-q|--quiet] [-f|--force] [-o|--output <file.h>] <dir1|pdb1> [...]\n"
                         "   or: %s [-V|--version]\n"
                         "   or: %s [-h|--help]\n",
                         argv[0], argv[0], argv[0]);
                return RTEXITCODE_SUCCESS;

            case VINF_GETOPT_NOT_OPTION:
            {
                RTEXITCODE rcExit2;
                if (RTFileExists(ValueUnion.psz))
                    rcExit2 = processPdb(ValueUnion.psz);
                else
                    rcExit2 = processDir(ValueUnion.psz);
                if (rcExit2 != RTEXITCODE_SUCCESS)
                {
                    if (!g_fOptForce)
                        return rcExit2;
                    rcExit = rcExit2;
                }
                break;
            }

            default:
                return RTGetOptPrintError(ch, &ValueUnion);
        }
    }
    if (RTListIsEmpty(&g_SetList))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "No usable debug files found.\n");

    /*
     * Generate the output.
     */
    PRTSTREAM pOut = g_pStdOut;
    if (strcmp(pszOutput, "-"))
    {
        rc = RTStrmOpen(pszOutput, "w", &pOut);
        if (RT_FAILURE(rc))
            return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error opening '%s' for writing: %Rrc\n", pszOutput, rc);
    }

    generateHeader(pOut);

    if (pOut != g_pStdOut)
        rc = RTStrmClose(pOut);
    else
        rc = RTStrmFlush(pOut);
    if (RT_FAILURE(rc))
        return RTMsgErrorExit(RTEXITCODE_FAILURE, "Error %s '%s': %Rrc\n", pszOutput,
                              pOut != g_pStdOut ? "closing" : "flushing", rc);
    return rcExit;
}
