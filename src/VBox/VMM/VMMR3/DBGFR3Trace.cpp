/* $Id: DBGFR3Trace.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * DBGF - Debugger Facility, Tracing.
 */

/*
 * Copyright (C) 2011-2024 Oracle and/or its affiliates.
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
#define LOG_GROUP LOG_GROUP_DBGF
#include <VBox/vmm/dbgftrace.h>
#include <VBox/vmm/cfgm.h>
#include <VBox/vmm/mm.h>
#include <VBox/vmm/pdmapi.h>
#include "DBGFInternal.h"
#include <VBox/vmm/vm.h>
#include "VMMTracing.h"

#include <VBox/err.h>
#include <VBox/log.h>
#include <VBox/param.h>

#include <iprt/assert.h>
#include <iprt/ctype.h>
#include <iprt/trace.h>


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static DECLCALLBACK(void) dbgfR3TraceInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs);


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
/**
 * VMM trace point group translation table.
 */
static const struct
{
    /** The group name. */
    const char *pszName;
    /** The name length. */
    uint32_t    cchName;
    /** The mask. */
    uint32_t    fMask;
}   g_aVmmTpGroups[] =
{
    {  RT_STR_TUPLE("em"), VMMTPGROUP_EM },
    {  RT_STR_TUPLE("hm"), VMMTPGROUP_HM },
    {  RT_STR_TUPLE("tm"), VMMTPGROUP_TM },
};


/**
 * Initializes the tracing.
 *
 * @returns VBox status code
 * @param   pVM         The cross context VM structure.
 * @param   cbEntry     The trace entry size.
 * @param   cEntries    The number of entries.
 */
static int dbgfR3TraceEnable(PVM pVM, uint32_t cbEntry, uint32_t cEntries)
{
    /*
     * Don't enable it twice.
     */
    if (pVM->hTraceBufR3 != NIL_RTTRACEBUF)
        return VERR_ALREADY_EXISTS;

    /*
     * Resolve default parameter values.
     */
    int rc;
    if (!cbEntry)
    {
        rc = CFGMR3QueryU32Def(CFGMR3GetChild(CFGMR3GetRoot(pVM), "DBGF"), "TraceBufEntrySize", &cbEntry, 128);
        AssertRCReturn(rc, rc);
    }
    if (!cEntries)
    {
        rc = CFGMR3QueryU32Def(CFGMR3GetChild(CFGMR3GetRoot(pVM), "DBGF"), "TraceBufEntries", &cEntries, 4096);
        AssertRCReturn(rc, rc);
    }

    /*
     * Figure the required size.
     */
    RTTRACEBUF  hTraceBuf;
    size_t      cbBlock = 0;
    rc = RTTraceBufCarve(&hTraceBuf, cEntries, cbEntry, 0 /*fFlags*/, NULL, &cbBlock);
    if (rc != VERR_BUFFER_OVERFLOW)
    {
        AssertReturn(!RT_SUCCESS_NP(rc), VERR_IPE_UNEXPECTED_INFO_STATUS);
        return rc;
    }

    /*
     * Allocate a hyper heap block and carve a trace buffer out of it.
     *
     * Note! We ASSUME that the returned trace buffer handle has the same value
     *       as the heap block.
     */
    cbBlock = RT_ALIGN_Z(cbBlock, HOST_PAGE_SIZE);
    RTR0PTR pvBlockR0 = NIL_RTR0PTR;
    void   *pvBlockR3 = NULL;
    rc = SUPR3PageAllocEx(cbBlock >> HOST_PAGE_SHIFT, 0, &pvBlockR3, &pvBlockR0, NULL);
    if (RT_FAILURE(rc))
        return rc;

    rc = RTTraceBufCarve(&hTraceBuf, cEntries, cbEntry, 0 /*fFlags*/, pvBlockR3, &cbBlock);
    AssertRCReturn(rc, rc);
    AssertReleaseReturn(hTraceBuf == (RTTRACEBUF)pvBlockR3, VERR_INTERNAL_ERROR_3);
    AssertReleaseReturn((void *)hTraceBuf == pvBlockR3, VERR_INTERNAL_ERROR_3);

    pVM->hTraceBufR3 = hTraceBuf;
    pVM->hTraceBufR0 = pvBlockR0;
    return VINF_SUCCESS;
}


/**
 * Initializes the tracing.
 *
 * @returns VBox status code
 * @param   pVM                 The cross context VM structure.
 */
int dbgfR3TraceInit(PVM pVM)
{
    /*
     * Initialize the trace buffer handles.
     */
    Assert(NIL_RTTRACEBUF == (RTTRACEBUF)NULL);
    pVM->hTraceBufR3 = NIL_RTTRACEBUF;
    pVM->hTraceBufR0 = NIL_RTR0PTR;

    /*
     * Check the config and enable tracing if requested.
     */
    PCFGMNODE pDbgfNode = CFGMR3GetChild(CFGMR3GetRoot(pVM), "DBGF");
#if defined(DEBUG) || defined(RTTRACE_ENABLED)
    bool const          fDefault        = false;
    const char * const  pszConfigDefault = "";
#else
    bool const          fDefault        = false;
    const char * const  pszConfigDefault = "";
#endif
    bool                fTracingEnabled;
    int rc = CFGMR3QueryBoolDef(pDbgfNode, "TracingEnabled", &fTracingEnabled, fDefault);
    AssertRCReturn(rc, rc);
    if (fTracingEnabled)
    {
        rc = dbgfR3TraceEnable(pVM, 0, 0);
        if (RT_SUCCESS(rc))
        {
            if (pDbgfNode)
            {
                char       *pszTracingConfig;
                rc = CFGMR3QueryStringAllocDef(pDbgfNode, "TracingConfig", &pszTracingConfig, pszConfigDefault);
                if (RT_SUCCESS(rc))
                {
                    rc = DBGFR3TraceConfig(pVM, pszTracingConfig);
                    if (RT_FAILURE(rc))
                        rc = VMSetError(pVM, rc, RT_SRC_POS, "TracingConfig=\"%s\" -> %Rrc", pszTracingConfig, rc);
                    MMR3HeapFree(pszTracingConfig);
                }
            }
            else
            {
                rc = DBGFR3TraceConfig(pVM, pszConfigDefault);
                if (RT_FAILURE(rc))
                    rc = VMSetError(pVM, rc, RT_SRC_POS, "TracingConfig=\"%s\" (default) -> %Rrc", pszConfigDefault, rc);
            }
        }
    }

    /*
     * Register a debug info item that will dump the trace buffer content.
     */
    if (RT_SUCCESS(rc))
        rc = DBGFR3InfoRegisterInternal(pVM, "tracebuf", "Display the trace buffer content. No arguments.", dbgfR3TraceInfo);

    return rc;
}


/**
 * Terminates the tracing.
 *
 * @param   pVM                 The cross context VM structure.
 */
void dbgfR3TraceTerm(PVM pVM)
{
    /* nothing to do */
    NOREF(pVM);
}


/**
 * Relocates the trace buffer handle in RC.
 *
 * @param   pVM                 The cross context VM structure.
 */
void dbgfR3TraceRelocate(PVM pVM)
{
    RT_NOREF(pVM);
}


/**
 * Change the traceing configuration of the VM.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS
 * @retval  VERR_NOT_FOUND if any of the trace point groups mentioned in the
 *          config string cannot be found. (Or if the string cannot be made
 *          sense of.)  No change made.
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_POINTER
 *
 * @param   pVM         The cross context VM structure.
 * @param   pszConfig   The configuration change specification.
 *
 *                      Trace point group names, optionally prefixed by a '-' to
 *                      indicate that the group is being disabled. A special
 *                      group 'all' can be used to enable or disable all trace
 *                      points.
 *
 *                      Drivers, devices and USB devices each have their own
 *                      trace point group which can be accessed by prefixing
 *                      their official PDM name by 'drv', 'dev' or 'usb'
 *                      respectively.
 */
VMMDECL(int) DBGFR3TraceConfig(PVM pVM, const char *pszConfig)
{
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(pszConfig, VERR_INVALID_POINTER);
    if (pVM->hTraceBufR3 == NIL_RTTRACEBUF)
        return VERR_DBGF_NO_TRACE_BUFFER;

    /*
     * We do this in two passes, the first pass just validates the input string
     * and the second applies the changes.
     */
    for (uint32_t uPass = 0; uPass < 1; uPass++)
    {
        char ch;
        while ((ch = *pszConfig) != '\0')
        {
            if (RT_C_IS_SPACE(ch))
                continue;

            /*
             * Operation prefix.
             */
            bool fNo = false;
            do
            {
                if (ch == 'n' && pszConfig[1] == 'o')
                {
                    fNo = !fNo;
                    pszConfig++;
                }
                else if (ch == '+')
                    fNo = false;
                else if (ch == '-' || ch == '!' || ch == '~')
                    fNo = !fNo;
                else
                    break;
            } while ((ch = *++pszConfig) != '\0');
            if (ch == '\0')
                break;

            /*
             * Extract the name.
             */
            const char *pszName = pszConfig;
            while (   ch != '\0'
                   && !RT_C_IS_SPACE(ch)
                   && !RT_C_IS_PUNCT(ch))
                ch = *++pszConfig;
            size_t const cchName = pszConfig - pszName;

            /*
             * 'all' - special group that enables or disables all trace points.
             */
            if (cchName == 3 && !strncmp(pszName, "all", 3))
            {
                if (uPass != 0)
                {
                    uint32_t iCpu = pVM->cCpus;
                    if (!fNo)
                        while (iCpu-- > 0)
                            pVM->apCpusR3[iCpu]->fTraceGroups = UINT32_MAX;
                    else
                        while (iCpu-- > 0)
                            pVM->apCpusR3[iCpu]->fTraceGroups = 0;
                    PDMR3TracingConfig(pVM, NULL, 0, !fNo, uPass > 0);
                }
            }
            else
            {
                /*
                 * A specific group, try the VMM first then PDM.
                 */
                uint32_t i = RT_ELEMENTS(g_aVmmTpGroups);
                while (i-- > 0)
                    if (   g_aVmmTpGroups[i].cchName == cchName
                        && !strncmp(g_aVmmTpGroups[i].pszName, pszName, cchName))
                    {
                        if (uPass != 0)
                        {
                            uint32_t iCpu = pVM->cCpus;
                            if (!fNo)
                                while (iCpu-- > 0)
                                    pVM->apCpusR3[iCpu]->fTraceGroups |= g_aVmmTpGroups[i].fMask;
                            else
                                while (iCpu-- > 0)
                                    pVM->apCpusR3[iCpu]->fTraceGroups &= ~g_aVmmTpGroups[i].fMask;
                        }
                        break;
                    }

                if (i == UINT32_MAX)
                {
                    int rc = PDMR3TracingConfig(pVM, pszName, cchName, !fNo, uPass > 0);
                    if (RT_FAILURE(rc))
                        return rc;
                }
            }
        }
    }

    return VINF_SUCCESS;
}


/**
 * Query the trace configuration specification string.
 *
 * @returns VBox status code.
 * @retval  VINF_SUCCESS
 * @retval  VERR_INVALID_VM_HANDLE
 * @retval  VERR_INVALID_POINTER
 * @retval  VERR_BUFFER_OVERFLOW if the buffer is too small. Buffer will be
 *          empty.

 * @param   pVM                 The cross context VM structure.
 * @param   pszConfig           Pointer to the output buffer.
 * @param   cbConfig            The size of the output buffer.
 */
VMMDECL(int) DBGFR3TraceQueryConfig(PVM pVM, char *pszConfig, size_t cbConfig)
{
    VM_ASSERT_VALID_EXT_RETURN(pVM, VERR_INVALID_VM_HANDLE);
    AssertPtrReturn(pszConfig, VERR_INVALID_POINTER);
    if (cbConfig < 1)
        return VERR_BUFFER_OVERFLOW;
    *pszConfig = '\0';

    if (pVM->hTraceBufR3 == NIL_RTTRACEBUF)
        return VERR_DBGF_NO_TRACE_BUFFER;

    int             rc           = VINF_SUCCESS;
    uint32_t const  fTraceGroups = pVM->apCpusR3[0]->fTraceGroups;
    if (   fTraceGroups == UINT32_MAX
        && PDMR3TracingAreAll(pVM, true /*fEnabled*/))
        rc = RTStrCopy(pszConfig, cbConfig, "all");
    else if (   fTraceGroups == 0
             && PDMR3TracingAreAll(pVM, false /*fEnabled*/))
        rc = RTStrCopy(pszConfig, cbConfig, "-all");
    else
    {
        char   *pszDst = pszConfig;
        size_t  cbDst  = cbConfig;
        uint32_t i = RT_ELEMENTS(g_aVmmTpGroups);
        while (i-- > 0)
            if (g_aVmmTpGroups[i].fMask & fTraceGroups)
            {
                size_t cchThis = g_aVmmTpGroups[i].cchName + (pszDst != pszConfig);
                if (cchThis >= cbDst)
                {
                    rc = VERR_BUFFER_OVERFLOW;
                    break;
                }
                if (pszDst != pszConfig)
                {
                    *pszDst = ' ';
                    memcpy(pszDst + 1, g_aVmmTpGroups[i].pszName, g_aVmmTpGroups[i].cchName + 1);
                }
                else
                    memcpy(pszDst, g_aVmmTpGroups[i].pszName, g_aVmmTpGroups[i].cchName + 1);
                pszDst += cchThis;
                cbDst  -= cchThis;
            }

        if (RT_SUCCESS(rc))
            rc = PDMR3TracingQueryConfig(pVM, pszDst, cbDst);
    }

    if (RT_FAILURE(rc))
        *pszConfig = '\0';
    return rc;
}


/**
 * @callback_method_impl{FNRTTRACEBUFCALLBACK}
 */
static DECLCALLBACK(int)
dbgfR3TraceInfoDumpEntry(RTTRACEBUF hTraceBuf, uint32_t iEntry, uint64_t NanoTS, RTCPUID idCpu, const char *pszMsg, void *pvUser)
{
    PCDBGFINFOHLP pHlp = (PCDBGFINFOHLP)pvUser;
    pHlp->pfnPrintf(pHlp, "#%04u/%'llu/%02x: %s\n", iEntry, NanoTS, idCpu, pszMsg);
    NOREF(hTraceBuf);
    return VINF_SUCCESS;
}


/**
 * @callback_method_impl{FNDBGFHANDLERINT, Info handler for displaying the trace buffer content.}
 */
static DECLCALLBACK(void) dbgfR3TraceInfo(PVM pVM, PCDBGFINFOHLP pHlp, const char *pszArgs)
{
    RTTRACEBUF hTraceBuf = pVM->hTraceBufR3;
    if (hTraceBuf == NIL_RTTRACEBUF)
        pHlp->pfnPrintf(pHlp, "Tracing is disabled\n");
    else
    {
        pHlp->pfnPrintf(pHlp, "Trace buffer %p - %u entries of %u bytes\n",
                        hTraceBuf, RTTraceBufGetEntryCount(hTraceBuf), RTTraceBufGetEntrySize(hTraceBuf));
        RTTraceBufEnumEntries(hTraceBuf, dbgfR3TraceInfoDumpEntry, (void *)pHlp);
    }
    NOREF(pszArgs);
}

