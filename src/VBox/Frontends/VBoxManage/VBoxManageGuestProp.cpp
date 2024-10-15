/* $Id: VBoxManageGuestProp.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - Implementation of guestproperty command.
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
#include "VBoxManage.h"

#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/VirtualBox.h>

#include <VBox/log.h>
#include <iprt/asm.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/time.h>
#include <iprt/thread.h>

#ifdef USE_XPCOM_QUEUE
# include <sys/select.h>
# include <errno.h>
#endif

#ifdef RT_OS_DARWIN
# include <CoreFoundation/CFRunLoop.h>
#endif

using namespace com;

DECLARE_TRANSLATION_CONTEXT(GuestProp);


static RTEXITCODE handleGetGuestProperty(HandlerArg *a)
{
    HRESULT hrc = S_OK;

    setCurrentSubcommand(HELP_SCOPE_GUESTPROPERTY_GET);

    bool verbose = false;
    if (    a->argc == 3
        &&  (   !strcmp(a->argv[2], "--verbose")
             || !strcmp(a->argv[2], "-verbose")))
        verbose = true;
    else if (a->argc != 2)
        return errorSyntax(GuestProp::tr("Incorrect parameters"));

    ComPtr<IMachine> machine;
    CHECK_ERROR(a->virtualBox, FindMachine(Bstr(a->argv[0]).raw(),
                                           machine.asOutParam()));
    if (machine)
    {
        /* open a session for the VM - new or existing */
        CHECK_ERROR_RET(machine, LockMachine(a->session, LockType_Shared), RTEXITCODE_FAILURE);

        /* get the mutable session machine */
        a->session->COMGETTER(Machine)(machine.asOutParam());

        Bstr value;
        LONG64 i64Timestamp;
        Bstr flags;
        CHECK_ERROR(machine, GetGuestProperty(Bstr(a->argv[1]).raw(),
                                              value.asOutParam(),
                                              &i64Timestamp, flags.asOutParam()));
        if (value.isEmpty())
            RTPrintf(GuestProp::tr("No value set!\n"));
        else
            RTPrintf(GuestProp::tr("Value: %ls\n"), value.raw());
        if (!value.isEmpty() && verbose)
        {
            RTPrintf(GuestProp::tr("Timestamp: %lld\n"), i64Timestamp);
            RTPrintf(GuestProp::tr("Flags: %ls\n"), flags.raw());
        }
    }
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static RTEXITCODE handleSetGuestProperty(HandlerArg *a)
{
    HRESULT hrc = S_OK;

    setCurrentSubcommand(HELP_SCOPE_GUESTPROPERTY_SET);

    /*
     * Check the syntax.  We can deduce the correct syntax from the number of
     * arguments.
     */
    bool usageOK = true;
    const char *pszName = NULL;
    const char *pszValue = NULL;
    const char *pszFlags = NULL;
    if (a->argc == 3)
        pszValue = a->argv[2];
    else if (a->argc == 4)
        usageOK = false;
    else if (a->argc == 5)
    {
        pszValue = a->argv[2];
        if (   strcmp(a->argv[3], "--flags")
            && strcmp(a->argv[3], "-flags"))
            usageOK = false;
        pszFlags = a->argv[4];
    }
    else if (a->argc != 2)
        usageOK = false;
    if (!usageOK)
        return errorSyntax(GuestProp::tr("Incorrect parameters"));
    /* This is always needed. */
    pszName = a->argv[1];

    ComPtr<IMachine> machine;
    CHECK_ERROR(a->virtualBox, FindMachine(Bstr(a->argv[0]).raw(),
                                           machine.asOutParam()));
    if (machine)
    {
        /* open a session for the VM - new or existing */
        CHECK_ERROR_RET(machine, LockMachine(a->session, LockType_Shared), RTEXITCODE_FAILURE);

        /* get the mutable session machine */
        a->session->COMGETTER(Machine)(machine.asOutParam());

        if (!pszFlags)
            CHECK_ERROR(machine, SetGuestPropertyValue(Bstr(pszName).raw(),
                                                       Bstr(pszValue).raw()));
        else
            CHECK_ERROR(machine, SetGuestProperty(Bstr(pszName).raw(),
                                                  Bstr(pszValue).raw(),
                                                  Bstr(pszFlags).raw()));

        if (SUCCEEDED(hrc))
            CHECK_ERROR(machine, SaveSettings());

        a->session->UnlockMachine();
    }
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static RTEXITCODE handleDeleteGuestProperty(HandlerArg *a)
{
    HRESULT hrc = S_OK;

    setCurrentSubcommand(HELP_SCOPE_GUESTPROPERTY_UNSET);

    /*
     * Check the syntax.  We can deduce the correct syntax from the number of
     * arguments.
     */
    bool usageOK = true;
    const char *pszName = NULL;
    if (a->argc != 2)
        usageOK = false;
    if (!usageOK)
        return errorSyntax(GuestProp::tr("Incorrect parameters"));
    /* This is always needed. */
    pszName = a->argv[1];

    ComPtr<IMachine> machine;
    CHECK_ERROR(a->virtualBox, FindMachine(Bstr(a->argv[0]).raw(),
                                           machine.asOutParam()));
    if (machine)
    {
        /* open a session for the VM - new or existing */
        CHECK_ERROR_RET(machine, LockMachine(a->session, LockType_Shared), RTEXITCODE_FAILURE);

        /* get the mutable session machine */
        a->session->COMGETTER(Machine)(machine.asOutParam());

        CHECK_ERROR(machine, DeleteGuestProperty(Bstr(pszName).raw()));

        if (SUCCEEDED(hrc))
            CHECK_ERROR(machine, SaveSettings());

        a->session->UnlockMachine();
    }
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

/**
 * Enumerates the properties in the guest property store.
 *
 * @returns 0 on success, 1 on failure
 * @note see the command line API description for parameters
 */
static RTEXITCODE handleEnumGuestProperty(HandlerArg *a)
{
    setCurrentSubcommand(HELP_SCOPE_GUESTPROPERTY_ENUMERATE);

    /*
     * Parse arguments.
     *
     * The old syntax was a little boinkers.  The --patterns argument just
     * indicates that the rest of the arguments are options.  Sort of like '--'.
     * This has been normalized a little now, by accepting patterns w/o a
     * preceding --pattern argument via the  VINF_GETOPT_NOT_OPTION.
     * Though, the first non-option is always the VM name.
     */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--old-format",          'o',      RTGETOPT_REQ_NOTHING },
        { "--sort",                's',      RTGETOPT_REQ_NOTHING },
        { "--unsort",              'u',      RTGETOPT_REQ_NOTHING },
        { "--timestamp",           't',      RTGETOPT_REQ_NOTHING },
        { "--ts",                  't',      RTGETOPT_REQ_NOTHING },
        { "--no-timestamp",        'T',      RTGETOPT_REQ_NOTHING },
        { "--abs",                 'a',      RTGETOPT_REQ_NOTHING },
        { "--absolute",            'a',      RTGETOPT_REQ_NOTHING },
        { "--rel",                 'r',      RTGETOPT_REQ_NOTHING },
        { "--relative",            'r',      RTGETOPT_REQ_NOTHING },
        { "--no-ts",               'T',      RTGETOPT_REQ_NOTHING },
        { "--flags",               'f',      RTGETOPT_REQ_NOTHING },
        { "--no-flags",            'F',      RTGETOPT_REQ_NOTHING },
        /* unnecessary legacy: */
        { "--patterns",            'p',      RTGETOPT_REQ_STRING  },
        { "-patterns",             'p',      RTGETOPT_REQ_STRING  },
    };

    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0);

    const char *pszVmNameOrUuid = NULL;
    Utf8Str     strPatterns;
    bool        fSort = true;
    bool        fNewStyle = true;
    bool        fTimestamp = true;
    bool        fAbsTime = true;
    bool        fFlags = true;

    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        /* For options that require an argument, ValueUnion has received the value. */
        switch (ch)
        {
            case VINF_GETOPT_NOT_OPTION:
                /* The first one is the VM name. */
                if (!pszVmNameOrUuid)
                {
                    pszVmNameOrUuid = ValueUnion.psz;
                    break;
                }
                /* Everything else would be patterns by the new syntax. */
                RT_FALL_THROUGH();
            case 'p':
                if (strPatterns.isNotEmpty())
                    if (RT_FAILURE(strPatterns.appendNoThrow(',')))
                        return RTMsgErrorExitFailure("out of memory!");
                if (RT_FAILURE(strPatterns.appendNoThrow(ValueUnion.psz)))
                    return RTMsgErrorExitFailure("out of memory!");
                break;

            case 'o':
                fNewStyle = false;
                break;

            case 's':
                fSort = true;
                break;
            case 'u':
                fSort = false;
                break;

            case 't':
                fTimestamp = true;
                break;
            case 'T':
                fTimestamp = false;
                break;

            case 'a':
                fAbsTime = true;
                break;
            case 'r':
                fAbsTime = false;
                break;

            case 'f':
                fFlags = true;
                break;
            case 'F':
                fFlags = false;
                break;

            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    }

    /* Only the VM name is required. */
    if (!pszVmNameOrUuid)
        return errorSyntax(GuestProp::tr("No VM name or UUID was specified"));

    /*
     * Make the actual call to Main.
     */
    ComPtr<IMachine> machine;
    CHECK_ERROR2I_RET(a->virtualBox, FindMachine(Bstr(pszVmNameOrUuid).raw(), machine.asOutParam()), RTEXITCODE_FAILURE);

    /* open a session for the VM - new or existing */
    CHECK_ERROR2I_RET(machine, LockMachine(a->session, LockType_Shared), RTEXITCODE_FAILURE);

    /* get the mutable session machine */
    a->session->COMGETTER(Machine)(machine.asOutParam());

    com::SafeArray<BSTR> names;
    com::SafeArray<BSTR> values;
    com::SafeArray<LONG64> timestamps;
    com::SafeArray<BSTR> flags;
    CHECK_ERROR2I_RET(machine, EnumerateGuestProperties(Bstr(strPatterns).raw(),
                                                        ComSafeArrayAsOutParam(names),
                                                        ComSafeArrayAsOutParam(values),
                                                        ComSafeArrayAsOutParam(timestamps),
                                                        ComSafeArrayAsOutParam(flags)),
                      RTEXITCODE_FAILURE);

    size_t const cEntries = names.size();
    if (cEntries == 0)
        RTPrintf(GuestProp::tr("No properties found.\n"));
    else
    {
        /* Whether we sort it or not, we work it via a indirect index: */
        size_t *paidxSorted = (size_t *)RTMemAlloc(sizeof(paidxSorted[0]) * cEntries);
        if (!paidxSorted)
            return RTMsgErrorExitFailure("out of memory!");
        for (size_t i = 0; i < cEntries; i++)
            paidxSorted[i] = i;

        /* Do the sorting: */
        if (fSort && cEntries > 1)
            for (size_t i = 0; i < cEntries - 1; i++)
                for (size_t j = 0; j < cEntries - i - 1; j++)
                    if (RTUtf16Cmp(names[paidxSorted[j]], names[paidxSorted[j + 1]]) > 0)
                    {
                        size_t iTmp = paidxSorted[j];
                        paidxSorted[j] = paidxSorted[j + 1];
                        paidxSorted[j + 1] = iTmp;
                    }

        if (fNewStyle)
        {
            /* figure the width of the main columns: */
            size_t cwcMaxName  = 1;
            size_t cwcMaxValue = 1;
            for (size_t i = 0; i < cEntries; ++i)
            {
                size_t cwcName = RTUtf16Len(names[i]);
                cwcMaxName = RT_MAX(cwcMaxName, cwcName);
                size_t cwcValue = RTUtf16Len(values[i]);
                cwcMaxValue = RT_MAX(cwcMaxValue, cwcValue);
            }
            cwcMaxName  = RT_MIN(cwcMaxName, 48);
            cwcMaxValue = RT_MIN(cwcMaxValue, 28);

            /* Get the current time for relative time formatting: */
            RTTIMESPEC Now;
            RTTimeNow(&Now);

            /* Print the table: */
            for (size_t iSorted = 0; iSorted < cEntries; ++iSorted)
            {
                size_t const i = paidxSorted[iSorted];
                char            szTime[80];
                if (fTimestamp)
                {
                    RTTIMESPEC TimestampTS;
                    RTTimeSpecSetNano(&TimestampTS, timestamps[i]);
                    if (fAbsTime)
                    {
                        RTTIME Timestamp;
                        RTTimeToStringEx(RTTimeExplode(&Timestamp, &TimestampTS), &szTime[2], sizeof(szTime) - 2, 3);
                    }
                    else
                    {
                        RTTIMESPEC DurationTS = Now;
                        RTTimeFormatDurationEx(&szTime[2], sizeof(szTime) - 2, RTTimeSpecSub(&DurationTS, &TimestampTS), 3);
                    }
                    szTime[0] = '@';
                    szTime[1] = ' ';
                }
                else
                    szTime[0] = '\0';

                static RTUTF16 s_wszEmpty[] = { 0 };
                PCRTUTF16 const pwszFlags = fFlags ? flags[i] : s_wszEmpty;

                int cchOut = RTPrintf("%-*ls = '%ls'", cwcMaxName, names[i], values[i]);
                if (fTimestamp || *pwszFlags)
                {
                    size_t const cwcWidth      = cwcMaxName + cwcMaxValue + 6;
                    size_t const cwcValPadding = (unsigned)cchOut < cwcWidth ? cwcWidth - (unsigned)cchOut : 1;
                    RTPrintf("%*s%s%s%ls\n", cwcValPadding, "", szTime, *pwszFlags ? " " : "", pwszFlags);
                }
                else
                    RTPrintf("\n");
            }
        }
        else
            for (size_t iSorted = 0; iSorted < cEntries; ++iSorted)
            {
                size_t const i = paidxSorted[iSorted];
                RTPrintf(GuestProp::tr("Name: %ls, value: %ls, timestamp: %lld, flags: %ls\n"),
                         names[i], values[i], timestamps[i], flags[i]);
            }
        RTMemFree(paidxSorted);
    }

    return RTEXITCODE_SUCCESS;
}

/**
 * Enumerates the properties in the guest property store.
 *
 * @returns 0 on success, 1 on failure
 * @note see the command line API description for parameters
 */
static RTEXITCODE handleWaitGuestProperty(HandlerArg *a)
{
    setCurrentSubcommand(HELP_SCOPE_GUESTPROPERTY_WAIT);

    /*
     * Handle arguments
     */
    bool        fFailOnTimeout = false;
    const char *pszPatterns    = NULL;
    uint32_t    cMsTimeout     = RT_INDEFINITE_WAIT;
    bool        usageOK        = true;
    if (a->argc < 2)
        usageOK = false;
    else
        pszPatterns = a->argv[1];
    ComPtr<IMachine> machine;
    HRESULT hrc;
    CHECK_ERROR(a->virtualBox, FindMachine(Bstr(a->argv[0]).raw(),
                                           machine.asOutParam()));
    if (!machine)
        usageOK = false;
    for (int i = 2; usageOK && i < a->argc; ++i)
    {
        if (   !strcmp(a->argv[i], "--timeout")
            || !strcmp(a->argv[i], "-timeout"))
        {
            if (   i + 1 >= a->argc
                || RTStrToUInt32Full(a->argv[i + 1], 10, &cMsTimeout) != VINF_SUCCESS)
                usageOK = false;
            else
                ++i;
        }
        else if (!strcmp(a->argv[i], "--fail-on-timeout"))
            fFailOnTimeout = true;
        else
            usageOK = false;
    }
    if (!usageOK)
        return errorSyntax(GuestProp::tr("Incorrect parameters"));

    /*
     * Set up the event listener and wait until found match or timeout.
     */
    Bstr aMachStrGuid;
    machine->COMGETTER(Id)(aMachStrGuid.asOutParam());
    Guid aMachGuid(aMachStrGuid);
    ComPtr<IEventSource> es;
    CHECK_ERROR(a->virtualBox, COMGETTER(EventSource)(es.asOutParam()));
    ComPtr<IEventListener> listener;
    CHECK_ERROR(es, CreateListener(listener.asOutParam()));
    com::SafeArray <VBoxEventType_T> eventTypes(1);
    eventTypes.push_back(VBoxEventType_OnGuestPropertyChanged);
    CHECK_ERROR(es, RegisterListener(listener, ComSafeArrayAsInParam(eventTypes), false));

    uint64_t u64Started = RTTimeMilliTS();
    bool fSignalled = false;
    do
    {
        unsigned cMsWait;
        if (cMsTimeout == RT_INDEFINITE_WAIT)
            cMsWait = 1000;
        else
        {
            uint64_t cMsElapsed = RTTimeMilliTS() - u64Started;
            if (cMsElapsed >= cMsTimeout)
                break; /* timed out */
            cMsWait = RT_MIN(1000, cMsTimeout - (uint32_t)cMsElapsed);
        }

        ComPtr<IEvent> ev;
        hrc = es->GetEvent(listener, cMsWait, ev.asOutParam());
        if (ev) /** @todo r=andy Why not using SUCCEEDED(hrc) here? */
        {
            VBoxEventType_T aType;
            hrc = ev->COMGETTER(Type)(&aType);
            switch (aType)
            {
                case VBoxEventType_OnGuestPropertyChanged:
                {
                    ComPtr<IGuestPropertyChangedEvent> gpcev = ev;
                    Assert(gpcev);
                    Bstr aNextStrGuid;
                    gpcev->COMGETTER(MachineId)(aNextStrGuid.asOutParam());
                    if (aMachGuid != Guid(aNextStrGuid))
                        continue;
                    Bstr aNextName;
                    gpcev->COMGETTER(Name)(aNextName.asOutParam());
                    if (RTStrSimplePatternMultiMatch(pszPatterns, RTSTR_MAX,
                                                     Utf8Str(aNextName).c_str(), RTSTR_MAX, NULL))
                    {
                        Bstr aNextValue, aNextFlags;
                        BOOL aNextWasDeleted;
                        gpcev->COMGETTER(Value)(aNextValue.asOutParam());
                        gpcev->COMGETTER(Flags)(aNextFlags.asOutParam());
                        gpcev->COMGETTER(FWasDeleted)(&aNextWasDeleted);
                        if (aNextWasDeleted)
                            RTPrintf(GuestProp::tr("Property %ls was deleted\n"), aNextName.raw());
                        else
                            RTPrintf(GuestProp::tr("Name: %ls, value: %ls, flags: %ls\n"),
                                     aNextName.raw(), aNextValue.raw(), aNextFlags.raw());
                        fSignalled = true;
                    }
                    break;
                }
                default:
                     AssertFailed();
            }
        }
    } while (!fSignalled);

    es->UnregisterListener(listener);

    RTEXITCODE rcExit = RTEXITCODE_SUCCESS;
    if (!fSignalled)
    {
        RTMsgError(GuestProp::tr("Time out or interruption while waiting for a notification."));
        if (fFailOnTimeout)
            /* Hysterical rasins: We always returned 2 here, which now translates to syntax error... Which is bad. */
            rcExit = RTEXITCODE_SYNTAX;
    }
    return rcExit;
}

/**
 * Access the guest property store.
 *
 * @returns 0 on success, 1 on failure
 * @note see the command line API description for parameters
 */
RTEXITCODE handleGuestProperty(HandlerArg *a)
{
    if (a->argc == 0)
        return errorNoSubcommand();

    /** @todo This command does not follow the syntax where the <uuid|vmname>
     * comes between the command and subcommand.  The commands controlvm,
     * snapshot and debugvm puts it between.
     */

    const char * const pszSubCmd = a->argv[0];
    a->argc -= 1;
    a->argv += 1;

    /* switch (cmd) */
    if (strcmp(pszSubCmd, "get") == 0)
        return handleGetGuestProperty(a);
    if (strcmp(pszSubCmd, "set") == 0)
        return handleSetGuestProperty(a);
    if (strcmp(pszSubCmd, "delete") == 0 || strcmp(pszSubCmd, "unset") == 0)
        return handleDeleteGuestProperty(a);
    if (strcmp(pszSubCmd, "enumerate") == 0 || strcmp(pszSubCmd, "enum") == 0)
        return handleEnumGuestProperty(a);
    if (strcmp(pszSubCmd, "wait") == 0)
        return handleWaitGuestProperty(a);

    /* default: */
    return errorUnknownSubcommand(pszSubCmd);
}
