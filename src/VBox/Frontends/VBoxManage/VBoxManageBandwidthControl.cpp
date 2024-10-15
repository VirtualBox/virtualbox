/* $Id: VBoxManageBandwidthControl.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - The bandwidth control related commands.
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
#include <VBox/com/com.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/VirtualBox.h>

#include <iprt/err.h>
#include <iprt/path.h>
#include <iprt/param.h>
#include <iprt/string.h>
#include <iprt/ctype.h>
#include <iprt/stream.h>
#include <iprt/getopt.h>
#include <VBox/log.h>

#include "VBoxManage.h"
using namespace com;

DECLARE_TRANSLATION_CONTEXT(BWControl);

// funcs
///////////////////////////////////////////////////////////////////////////////


/**
 * Parses a string in the following format "n[k|m|g|K|M|G]". Stores the value
 * of n expressed in bytes to *pLimit. k meas kilobit, while K means kilobyte.
 *
 * @returns Error message or NULL if successful.
 * @param   pcszLimit       The string to parse.
 * @param   pLimit          Where to store the result.
 */
static const char *parseLimit(const char *pcszLimit, int64_t *pLimit)
{
    int iMultiplier = _1M;
    char *pszNext = NULL;
    int vrc = RTStrToInt64Ex(pcszLimit, &pszNext, 10, pLimit);

    switch (vrc)
    {
        case VINF_SUCCESS:
            break;
        case VWRN_NUMBER_TOO_BIG:
            return BWControl::tr("Limit is too big\n");
        case VWRN_TRAILING_CHARS:
            switch (*pszNext)
            {
                case 'G': iMultiplier = _1G;       break;
                case 'M': iMultiplier = _1M;       break;
                case 'K': iMultiplier = _1K;       break;
                case 'g': iMultiplier = 125000000; break;
                case 'm': iMultiplier = 125000;    break;
                case 'k': iMultiplier = 125;       break;
                default:  return BWControl::tr("Invalid unit suffix. Valid suffixes are: k, m, g, K, M, G\n");
            }
            break;
        case VWRN_TRAILING_SPACES:
            return BWControl::tr("Trailing spaces in limit!\n");
        case VERR_NO_DIGITS:
            return BWControl::tr("No digits in limit specifier\n");
        default:
            return BWControl::tr("Invalid limit specifier\n");
    }
    if (*pLimit < 0)
        return BWControl::tr("Limit cannot be negative\n");
    if (*pLimit > INT64_MAX / iMultiplier)
        return BWControl::tr("Limit is too big\n");
    *pLimit *= iMultiplier;

    return NULL;
}

/**
 * Handles the 'bandwidthctl myvm add' sub-command.
 * @returns Exit code.
 * @param   a               The handler argument package.
 * @param   bwCtrl          Reference to the bandwidth control interface.
 */
static RTEXITCODE handleBandwidthControlAdd(HandlerArg *a, ComPtr<IBandwidthControl> &bwCtrl)
{
    HRESULT hrc = S_OK;
    static const RTGETOPTDEF g_aBWCtlAddOptions[] =
        {
            { "--type",   't', RTGETOPT_REQ_STRING },
            { "--limit",  'l', RTGETOPT_REQ_STRING }
        };

    setCurrentSubcommand(HELP_SCOPE_BANDWIDTHCTL_ADD);

    Bstr name(a->argv[2]);
    if (name.isEmpty())
    {
        errorArgument(BWControl::tr("Bandwidth group name must not be empty!\n"));
        return RTEXITCODE_FAILURE;
    }

    const char *pszType  = NULL;
    int64_t cMaxBytesPerSec = INT64_MAX;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, g_aBWCtlAddOptions,
                 RT_ELEMENTS(g_aBWCtlAddOptions), 3, RTGETOPTINIT_FLAGS_NO_STD_OPTS);

    while (   SUCCEEDED(hrc)
           && (c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 't':   // bandwidth group type
            {
                if (ValueUnion.psz)
                    pszType = ValueUnion.psz;
                else
                    hrc = E_FAIL;
                break;
            }

            case 'l': // limit
            {
                if (ValueUnion.psz)
                {
                    const char *pcszError = parseLimit(ValueUnion.psz, &cMaxBytesPerSec);
                    if (pcszError)
                    {
                        errorArgument(pcszError);
                        return RTEXITCODE_FAILURE;
                    }
                }
                else
                    hrc = E_FAIL;
                break;
            }

            default:
            {
                errorGetOpt(c, &ValueUnion);
                hrc = E_FAIL;
                break;
            }
        }
    }

    BandwidthGroupType_T enmType;

    if (!RTStrICmp(pszType, "disk"))
        enmType = BandwidthGroupType_Disk;
    else if (!RTStrICmp(pszType, "network"))
        enmType = BandwidthGroupType_Network;
    else
    {
        errorArgument(BWControl::tr("Invalid bandwidth group type\n"));
        return RTEXITCODE_FAILURE;
    }

    CHECK_ERROR2I_RET(bwCtrl, CreateBandwidthGroup(name.raw(), enmType, (LONG64)cMaxBytesPerSec), RTEXITCODE_FAILURE);

    return RTEXITCODE_SUCCESS;
}

/**
 * Handles the 'bandwidthctl myvm set' sub-command.
 * @returns Exit code.
 * @param   a               The handler argument package.
 * @param   bwCtrl          Reference to the bandwidth control interface.
 */
static RTEXITCODE handleBandwidthControlSet(HandlerArg *a, ComPtr<IBandwidthControl> &bwCtrl)
{
    HRESULT hrc = S_OK;
    static const RTGETOPTDEF g_aBWCtlAddOptions[] =
        {
            { "--limit",  'l', RTGETOPT_REQ_STRING }
        };

    setCurrentSubcommand(HELP_SCOPE_BANDWIDTHCTL_SET);

    Bstr name(a->argv[2]);
    int64_t cMaxBytesPerSec = INT64_MAX;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, g_aBWCtlAddOptions,
                 RT_ELEMENTS(g_aBWCtlAddOptions), 3, RTGETOPTINIT_FLAGS_NO_STD_OPTS);

    while (   SUCCEEDED(hrc)
           && (c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'l': // limit
            {
                if (ValueUnion.psz)
                {
                    const char *pcszError = parseLimit(ValueUnion.psz, &cMaxBytesPerSec);
                    if (pcszError)
                    {
                        errorArgument(pcszError);
                        return RTEXITCODE_FAILURE;
                    }
                }
                else
                    hrc = E_FAIL;
                break;
            }

            default:
            {
                errorGetOpt(c, &ValueUnion);
                hrc = E_FAIL;
                break;
            }
        }
    }


    if (cMaxBytesPerSec != INT64_MAX)
    {
        ComPtr<IBandwidthGroup> bwGroup;
        CHECK_ERROR2I_RET(bwCtrl, GetBandwidthGroup(name.raw(), bwGroup.asOutParam()), RTEXITCODE_FAILURE);
        if (SUCCEEDED(hrc))
        {
            CHECK_ERROR2I_RET(bwGroup, COMSETTER(MaxBytesPerSec)((LONG64)cMaxBytesPerSec), RTEXITCODE_FAILURE);
        }
    }

    return RTEXITCODE_SUCCESS;
}

/**
 * Handles the 'bandwidthctl myvm remove' sub-command.
 * @returns Exit code.
 * @param   a               The handler argument package.
 * @param   bwCtrl          Reference to the bandwidth control interface.
 */
static RTEXITCODE handleBandwidthControlRemove(HandlerArg *a, ComPtr<IBandwidthControl> &bwCtrl)
{
    setCurrentSubcommand(HELP_SCOPE_BANDWIDTHCTL_REMOVE);

    Bstr name(a->argv[2]);
    CHECK_ERROR2I_RET(bwCtrl, DeleteBandwidthGroup(name.raw()), RTEXITCODE_FAILURE);
    return RTEXITCODE_SUCCESS;
}

/**
 * Handles the 'bandwidthctl myvm list' sub-command.
 * @returns Exit code.
 * @param   a               The handler argument package.
 * @param   bwCtrl          Reference to the bandwidth control interface.
 */
static RTEXITCODE handleBandwidthControlList(HandlerArg *pArgs, ComPtr<IBandwidthControl> &rptrBWControl)
{
    static const RTGETOPTDEF g_aOptions[] =
    {
        { "--machinereadable",  'M', RTGETOPT_REQ_NOTHING },
    };

    setCurrentSubcommand(HELP_SCOPE_BANDWIDTHCTL_LIST);
    VMINFO_DETAILS enmDetails = VMINFO_STANDARD;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, pArgs->argc, pArgs->argv, g_aOptions, RT_ELEMENTS(g_aOptions), 2 /*iArg*/, 0 /*fFlags*/);
    while ((c = RTGetOpt(&GetState, &ValueUnion)))
    {
        switch (c)
        {
            case 'M':
                enmDetails = VMINFO_MACHINEREADABLE;
                break;
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    if (FAILED(showBandwidthGroups(rptrBWControl, enmDetails)))
        return RTEXITCODE_FAILURE;

    return RTEXITCODE_SUCCESS;
}


/**
 * Handles the 'bandwidthctl' command.
 * @returns Exit code.
 * @param   a               The handler argument package.
 */
RTEXITCODE handleBandwidthControl(HandlerArg *a)
{
    HRESULT hrc = S_OK;
    ComPtr<IMachine> machine;
    ComPtr<IBandwidthControl> bwCtrl;

    if (a->argc < 2)
        return errorSyntax(BWControl::tr("Too few parameters"));
    else if (a->argc > 7)
        return errorSyntax(BWControl::tr("Too many parameters"));

    /* try to find the given machine */
    CHECK_ERROR_RET(a->virtualBox, FindMachine(Bstr(a->argv[0]).raw(),
                                               machine.asOutParam()), RTEXITCODE_FAILURE);

    /* open a session for the VM (new or shared) */
    CHECK_ERROR_RET(machine, LockMachine(a->session, LockType_Shared), RTEXITCODE_FAILURE);
    SessionType_T st;
    CHECK_ERROR_RET(a->session, COMGETTER(Type)(&st), RTEXITCODE_FAILURE);
    bool fRunTime = (st == SessionType_Shared);

    /* get the mutable session machine */
    a->session->COMGETTER(Machine)(machine.asOutParam());
    hrc = machine->COMGETTER(BandwidthControl)(bwCtrl.asOutParam());
    if (FAILED(hrc)) goto leave; /** @todo r=andy Argh!! */

    if (!strcmp(a->argv[1], "add"))
    {
        if (fRunTime)
        {
            errorArgument(BWControl::tr("Bandwidth groups cannot be created while the VM is running\n"));
            goto leave;
        }
        hrc = handleBandwidthControlAdd(a, bwCtrl) == RTEXITCODE_SUCCESS ? S_OK : E_FAIL;
    }
    else if (!strcmp(a->argv[1], "remove"))
    {
        if (fRunTime)
        {
            errorArgument(BWControl::tr("Bandwidth groups cannot be deleted while the VM is running\n"));
            goto leave;
        }
        hrc = handleBandwidthControlRemove(a, bwCtrl) == RTEXITCODE_SUCCESS ? S_OK : E_FAIL;
    }
    else if (!strcmp(a->argv[1], "set"))
        hrc = handleBandwidthControlSet(a, bwCtrl) == RTEXITCODE_SUCCESS ? S_OK : E_FAIL;
    else if (!strcmp(a->argv[1], "list"))
        hrc = handleBandwidthControlList(a, bwCtrl) == RTEXITCODE_SUCCESS ? S_OK : E_FAIL;
    else
    {
        errorSyntax(BWControl::tr("Invalid parameter '%s'"), a->argv[1]);
        hrc = E_FAIL;
    }

    /* commit changes */
    if (SUCCEEDED(hrc))
        CHECK_ERROR(machine, SaveSettings());

leave:
    /* it's important to always close sessions */
    a->session->UnlockMachine();

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}
