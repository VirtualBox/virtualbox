/* $Id: GCM.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * GCM - Guest Compatibility Manager.
 */

/*
 * Copyright (C) 2022-2024 Oracle and/or its affiliates.
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

/** @page pg_gcm        GCM - The Guest Compatibility Manager
 *
 * The Guest Compatibility Manager provides run-time compatibility fixes for
 * certain known guest bugs.
 *
 * @see grp_gcm
 *
 *
 * @section sec_gcm_fixer   Fixers
 *
 * A GCM fixer implements a collection of run-time helpers/patches suitable for
 * a specific guest type. Several fixers can be active at the same time; for
 * example OS/2 or Windows 9x need their own fixers, but can also runs DOS
 * applications which need DOS-specific fixers.
 *
 * The concept of fixers exists to reduce the number of false positives to a
 * minimum. Heuristics are used to decide whether a particular fix should be
 * applied or not; restricting the number of applicable fixes minimizes the
 * chance that a fix could be misapplied.
 *
 * The fixers are invisible to a guest. It is not expected that the set of
 * active fixers would be changed during the lifetime of the VM.
 *
 *
 * @subsection sec_gcm_fixer_div_by_zero  Division By Zero
 *
 * A common problem is division by zero caused by a software timing loop which
 * cannot deal with fast CPUs (where "fast" very much depends on the era when
 * the software was written). A fixer intercepts division by zero, recognizes
 * known register contents and code sequence, modifies one or more registers to
 * avoid a divide error, and restarts the instruction.
 *
 */


/*********************************************************************************************************************************
*   Header Files                                                                                                                 *
*********************************************************************************************************************************/
#define LOG_GROUP LOG_GROUP_GCM
#include <VBox/vmm/gcm.h>
#include <VBox/vmm/ssm.h>
#include "GCMInternal.h"
#include <VBox/vmm/vm.h>

#include <VBox/log.h>
#include <VBox/err.h>

#include <iprt/string.h>


/*********************************************************************************************************************************
*   Global Variables                                                                                                             *
*********************************************************************************************************************************/
typedef enum
{
    /** Invalid zero value. */
    kGcmLoadAct_Invalid = 0,
    /** The fixer is set up at VM config and has additional state/whatever that
     *  prevents it from being reconfigured during state load. */
    kGcmLoadAct_NoReconfigFail,
    /** The fixer is set up at VM config but it doesn't really matter too
     *  much whether we keep using the VM config instead of the saved state. */
    kGcmLoadAct_NoReconfigIgnore,
    /** The fixer state is only checked at runtime, so it's no problem to
     * reconfigure it during state load. */
    kGcmLoadAct_Reconfigurable,
    kGcmLoadAct_End
} GCMLOADACTION;
/** Fixer flag configuration names. */
static struct
{
    const char   *pszName;
    uint8_t       cchName;
    uint8_t       uBit;
    GCMLOADACTION enmLoadAction;
} const g_aGcmFixerIds[] =
{
    { RT_STR_TUPLE("DivByZeroDOS"),   GCMFIXER_DBZ_DOS_BIT,         kGcmLoadAct_NoReconfigIgnore },
    { RT_STR_TUPLE("DivByZeroOS2"),   GCMFIXER_DBZ_OS2_BIT,         kGcmLoadAct_NoReconfigIgnore },
    { RT_STR_TUPLE("DivByZeroWin9x"), GCMFIXER_DBZ_WIN9X_BIT,       kGcmLoadAct_NoReconfigIgnore },
    { RT_STR_TUPLE("MesaVmsvgaDrv"),  GCMFIXER_MESA_VMSVGA_DRV_BIT, kGcmLoadAct_Reconfigurable   },
};

/** Max g_aGcmFixerIds::cchName value. */
#define GCM_FIXER_ID_MAX_NAME_LEN       30

/** Max size of the gcmFixerIdsToString output. */
#define GCM_FIXER_SET_MAX_STRING_SIZE   (2 + (GCM_FIXER_ID_MAX_NAME_LEN + 2) * (RT_ELEMENTS(g_aGcmFixerIds) + 1) + 2)


/*********************************************************************************************************************************
*   Internal Functions                                                                                                           *
*********************************************************************************************************************************/
static FNSSMINTSAVEEXEC  gcmR3Save;
static FNSSMINTLOADEXEC  gcmR3Load;
static char *gcmFixerIdsToString(char *pszDst, size_t cbDst, uint32_t fFixerIds, bool fInSpacePrefixedParenthesis) RT_NOEXCEPT;


/**
 * Initializes the GCM.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) GCMR3Init(PVM pVM)
{
    LogFlow(("GCMR3Init\n"));

    /*
     * Assert alignment and sizes.
     */
    AssertCompile(sizeof(pVM->gcm.s) <= sizeof(pVM->gcm.padding));

    /*
     * Register the saved state data unit.
     */
    int rc = SSMR3RegisterInternal(pVM, "GCM", 0 /* uInstance */, GCM_SAVED_STATE_VERSION, sizeof(GCM),
                                   NULL /* pfnLivePrep */, NULL /* pfnLiveExec */, NULL /* pfnLiveVote*/,
                                   NULL /* pfnSavePrep */, gcmR3Save,              NULL /* pfnSaveDone */,
                                   NULL /* pfnLoadPrep */, gcmR3Load,              NULL /* pfnLoadDone */);
    if (RT_FAILURE(rc))
        return rc;

    /*
     * Read & validate configuration.
     */
    /* Assemble valid value names for CFMGR3ValidateConfig. */
    char   szValidValues[GCM_FIXER_SET_MAX_STRING_SIZE];
    size_t offValidValues = 0;
    for (unsigned i = 0; i < RT_ELEMENTS(g_aGcmFixerIds); i++)
    {
        Assert(g_aGcmFixerIds[i].cchName > 0 && g_aGcmFixerIds[i].cchName <= GCM_FIXER_ID_MAX_NAME_LEN);

        AssertReturn(offValidValues + g_aGcmFixerIds[i].cchName + 2 <= sizeof(szValidValues), VERR_INTERNAL_ERROR_2);
        if (offValidValues)
            szValidValues[offValidValues++] = '|';
        memcpy(&szValidValues[offValidValues], g_aGcmFixerIds[i].pszName, g_aGcmFixerIds[i].cchName);
        offValidValues += g_aGcmFixerIds[i].cchName;
    }
    szValidValues[offValidValues] = '\0';

    /* Validate the configuration. */
    PCFGMNODE pCfgNode = CFGMR3GetChild(CFGMR3GetRoot(pVM), "GCM/");
    rc = CFGMR3ValidateConfig(pCfgNode,
                              "/GCM/",              /* pszNode (for error msgs) */
                              szValidValues,
                              "",                   /* pszValidNodes */
                              "GCM",                /* pszWho */
                              0);                   /* uInstance */
    if (RT_FAILURE(rc))
        return rc;

    /* Read the configuration. */
    pVM->gcm.s.fFixerSet = 0;
    for (unsigned i = 0; i < RT_ELEMENTS(g_aGcmFixerIds); i++)
    {
        bool fEnabled = false;
        rc = CFGMR3QueryBoolDef(pCfgNode, g_aGcmFixerIds[i].pszName, &fEnabled, false);
        if (RT_FAILURE(rc))
            return VMR3SetError(pVM->pUVM, rc, RT_SRC_POS, "Error reading /GCM/%s as boolean: %Rrc",
                                g_aGcmFixerIds[i].pszName, rc);
        if (fEnabled)
            pVM->gcm.s.fFixerSet = RT_BIT_32(g_aGcmFixerIds[i].uBit);
    }

#if 0 /* development override */
    pVM->gcm.s.fFixerSet = GCMFIXER_DBZ_OS2 | GCMFIXER_DBZ_DOS | GCMFIXER_DBZ_WIN9X;
#endif

    /*
     * Log what's enabled.
     */
    LogRel(("GCM: Initialized - Fixer bits: %#x%s\n", pVM->gcm.s.fFixerSet,
            gcmFixerIdsToString(szValidValues, sizeof(szValidValues), pVM->gcm.s.fFixerSet, true)));

    return VINF_SUCCESS;
}


/**
 * Converts the fixer ID set to a string for logging and error reporting.
 */
static char *gcmFixerIdsToString(char *pszDst, size_t cbDst, uint32_t fFixerIds, bool fInSpacePrefixedParenthesis) RT_NOEXCEPT
{
    AssertReturn(cbDst > 0, NULL);
    *pszDst = '\0';

    size_t offDst = 0;
    for (unsigned i = 0; i < RT_ELEMENTS(g_aGcmFixerIds); i++)
        if (fFixerIds & RT_BIT_32(g_aGcmFixerIds[i].uBit))
        {
            AssertReturn(offDst + g_aGcmFixerIds[i].cchName + 4 <= cbDst, pszDst);
            if (offDst)
            {
                pszDst[offDst++] = ',';
                pszDst[offDst++] = ' ';
            }
            else if (fInSpacePrefixedParenthesis)
            {
                pszDst[offDst++] = ' ';
                pszDst[offDst++] = '(';
            }
            memcpy(&pszDst[offDst], g_aGcmFixerIds[i].pszName, g_aGcmFixerIds[i].cchName);
            offDst += g_aGcmFixerIds[i].cchName;
            pszDst[offDst] = '\0';

            fFixerIds &= ~RT_BIT_32(g_aGcmFixerIds[i].uBit);
            if (!fFixerIds)
                break;
        }

    if (fFixerIds)
    {
        char         szTmp[64];
        size_t const cchTmp = RTStrPrintf(szTmp, sizeof(szTmp), "%#x", fFixerIds);
        AssertReturn(offDst + cchTmp + 4 <= cbDst, pszDst);
        if (offDst)
        {
            pszDst[offDst++] = ',';
            pszDst[offDst++] = ' ';
        }
        else if (fInSpacePrefixedParenthesis)
        {
            pszDst[offDst++] = ' ';
            pszDst[offDst++] = '(';
        }
        memcpy(&pszDst[offDst], szTmp, cchTmp);
        offDst += cchTmp;
        pszDst[offDst] = '\0';
    }

    if (offDst && fInSpacePrefixedParenthesis)
    {
        pszDst[offDst++] = ')';
        pszDst[offDst] = '\0';
    }
    return pszDst;
}


/**
 * @callback_method_impl{FNSSMINTSAVEEXEC}
 */
static DECLCALLBACK(int) gcmR3Save(PVM pVM, PSSMHANDLE pSSM)
{
    AssertReturn(pVM,  VERR_INVALID_PARAMETER);
    AssertReturn(pSSM, VERR_SSM_INVALID_STATE);

    /*
     * At present there is only configuration to save.
     */
    return SSMR3PutU32(pSSM, pVM->gcm.s.fFixerSet);
}


/**
 * @callback_method_impl{FNSSMINTLOADEXEC}
 */
static DECLCALLBACK(int) gcmR3Load(PVM pVM, PSSMHANDLE pSSM, uint32_t uVersion, uint32_t uPass)
{
    if (uPass != SSM_PASS_FINAL)
        return VINF_SUCCESS;
    if (uVersion != GCM_SAVED_STATE_VERSION)
        return VERR_SSM_UNSUPPORTED_DATA_UNIT_VERSION;

    /*
     * Load configuration and check it aginst the current (live migration,
     * general paranoia).
     */
    uint32_t fFixerSet = 0;
    int rc = SSMR3GetU32(pSSM, &fFixerSet);
    AssertRCReturn(rc, rc);

    if (fFixerSet == pVM->gcm.s.fFixerSet)
        return VINF_SUCCESS;

    /*
     * Check if we can reconfigure to the loaded fixer set.
     */
    bool     fSuccess     = true;
    uint32_t fNewFixerSet = fFixerSet;
    uint32_t fDiffSet     = fFixerSet ^ pVM->gcm.s.fFixerSet;
    while (fDiffSet)
    {
        unsigned const uBit  = ASMBitFirstSetU32(fDiffSet) - 1U;
        unsigned       idxEntry;
        for (idxEntry = 0; idxEntry < RT_ELEMENTS(g_aGcmFixerIds); idxEntry++)
            if (g_aGcmFixerIds[idxEntry].uBit == uBit)
                break;
        if (idxEntry < RT_ELEMENTS(g_aGcmFixerIds))
        {
            switch (g_aGcmFixerIds[idxEntry].enmLoadAction)
            {
                case kGcmLoadAct_Reconfigurable:
                    if (fFixerSet & RT_BIT_32(uBit))
                        LogRel(("GCM: Enabling %s (loading state).\n", g_aGcmFixerIds[idxEntry].pszName));
                    else
                        LogRel(("GCM: Disabling %s (loading state).\n", g_aGcmFixerIds[idxEntry].pszName));
                    break;

                case kGcmLoadAct_NoReconfigIgnore:
                    if (fFixerSet & RT_BIT_32(uBit))
                        LogRel(("GCM: %s is disabled in VM config but enabled in saved state being loaded, keeping it disabled as configured.\n",
                                g_aGcmFixerIds[idxEntry].pszName));
                    else
                        LogRel(("GCM: %s is enabled in VM config but disabled in saved state being loaded, keeping it enabled as configured.\n",
                                g_aGcmFixerIds[idxEntry].pszName));
                    fNewFixerSet &= ~RT_BIT_32(uBit);
                    fNewFixerSet |= pVM->gcm.s.fFixerSet & RT_BIT_32(uBit);
                    break;

                default:
                    AssertFailed();
                    RT_FALL_THRU();
                case kGcmLoadAct_NoReconfigFail:
                    if (fFixerSet & RT_BIT_32(uBit))
                        LogRel(("GCM: Error! %s is disabled in VM config but enabled in saved state being loaded!\n",
                                g_aGcmFixerIds[idxEntry].pszName));
                    else
                        LogRel(("GCM: Error! %s is enabled in VM config but disabled in saved state being loaded!\n",
                                g_aGcmFixerIds[idxEntry].pszName));
                    fSuccess = false;
                    break;
            }
        }
        else
        {
            /* For max flexibility, we just ignore unknown fixers. */
            LogRel(("GCM: Warning! Ignoring unknown fixer ID set in saved state: %#x (bit %u)\n", RT_BIT_32(uBit), uBit));
            fNewFixerSet &= ~RT_BIT_32(uBit);
        }
        fDiffSet &= ~RT_BIT_32(uBit);
    }
    if (fSuccess)
    {
        pVM->gcm.s.fFixerSet = fNewFixerSet;
        return VINF_SUCCESS;
    }

    char szTmp1[GCM_FIXER_SET_MAX_STRING_SIZE];
    char szTmp2[GCM_FIXER_SET_MAX_STRING_SIZE];
    return SSMR3SetCfgError(pSSM, RT_SRC_POS, N_("Saved GCM fixer set %#x%s differs from the configured one (%#x%s)."),
                            fFixerSet, gcmFixerIdsToString(szTmp1, sizeof(szTmp1), fFixerSet, true),
                            pVM->gcm.s.fFixerSet, gcmFixerIdsToString(szTmp2, sizeof(szTmp2), pVM->gcm.s.fFixerSet, true));
}


/**
 * Terminates the GCM.
 *
 * Termination means cleaning up and freeing all resources,
 * the VM itself is, at this point, powered off or suspended.
 *
 * @returns VBox status code.
 * @param   pVM         The cross context VM structure.
 */
VMMR3_INT_DECL(int) GCMR3Term(PVM pVM)
{
    RT_NOREF(pVM);
    return VINF_SUCCESS;
}


/**
 * The VM is being reset.
 *
 * Do whatever fixer-specific resetting that needs to be done.
 *
 * @param   pVM     The cross context VM structure.
 */
VMMR3_INT_DECL(void) GCMR3Reset(PVM pVM)
{
    RT_NOREF(pVM);
}

