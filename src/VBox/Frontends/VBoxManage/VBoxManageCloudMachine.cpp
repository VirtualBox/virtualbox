/* $Id: VBoxManageCloudMachine.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManageCloudMachine - The cloud machine related commands.
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

#include "VBoxManage.h"

#include <VBox/log.h>

#include <VBox/com/ErrorInfo.h>
#include <VBox/com/Guid.h>
#include <VBox/com/errorprint.h>

#include <algorithm>
#include <vector>

DECLARE_TRANSLATION_CONTEXT(CloudMachine);


struct CMachineHandlerArg
  : public HandlerArg
{
    ComPtr<ICloudClient> pClient;

    const char *pcszSpec;   /* RTGETOPTUNION::psz, points inside argv */
    enum { GUESS, ID, NAME } enmSpecKind;
    ComPtr<ICloudMachine> pMachine;

    explicit CMachineHandlerArg(const HandlerArg &a)
      : HandlerArg(a), pcszSpec(NULL), enmSpecKind(GUESS) {}
};


static int selectCloudProvider(ComPtr<ICloudProvider> &pProvider,
                               const ComPtr<IVirtualBox> &pVirtualBox,
                               const char *pszProviderName);
static int selectCloudProfile(ComPtr<ICloudProfile> &pProfile,
                              const ComPtr<ICloudProvider> &pProvider,
                              const char *pszProviderName);
static int getCloudClient(CMachineHandlerArg &a,
                          const char *pcszProviderName,
                          const char *pcszProfileName);

static HRESULT getMachineList(com::SafeIfaceArray<ICloudMachine> &aMachines,
                              const ComPtr<ICloudClient> &pClient);

static HRESULT getMachineBySpec(CMachineHandlerArg *a);
static HRESULT getMachineById(CMachineHandlerArg *a);
static HRESULT getMachineByName(CMachineHandlerArg *a);
static HRESULT getMachineByGuess(CMachineHandlerArg *a);

static int checkMachineSpecArgument(CMachineHandlerArg *a,
                                    int ch, const RTGETOPTUNION &Val);


static RTEXITCODE handleCloudMachineImpl(CMachineHandlerArg *a, int iFirst);

static RTEXITCODE handleCloudMachineStart(CMachineHandlerArg *a, int iFirst);
static RTEXITCODE handleCloudMachineReboot(CMachineHandlerArg *a, int iFirst);
static RTEXITCODE handleCloudMachineReset(CMachineHandlerArg *a, int iFirst);
static RTEXITCODE handleCloudMachineShutdown(CMachineHandlerArg *a, int iFirst);
static RTEXITCODE handleCloudMachinePowerdown(CMachineHandlerArg *a, int iFirst);
static RTEXITCODE handleCloudMachineTerminate(CMachineHandlerArg *a, int iFirst);

static RTEXITCODE handleCloudMachineConsoleHistory(CMachineHandlerArg *a, int iFirst);

static RTEXITCODE listCloudMachinesImpl(CMachineHandlerArg *a, int iFirst);
static RTEXITCODE handleCloudMachineInfo(CMachineHandlerArg *a, int iFirst);

static HRESULT printMachineInfo(const ComPtr<ICloudMachine> &pMachine);
static HRESULT printFormValue(const ComPtr<IFormValue> &pValue);



/*
 * This is a temporary hack as I don't want to refactor "cloud"
 * handling right now, as it's not yet clear to me what is the
 * direction that we want to take with it.
 *
 * The problem with the way "cloud" command handling is currently
 * written is that it's a bit schizophrenic about whether we have
 * multiple cloud providers or not.  OTOH it insists on --provider
 * being mandatory, on the other it hardcodes the list of available
 * subcommands, though in principle those can vary from provider to
 * provider.  If we do want to support multiple providers we might
 * need to come up with a way to allow an extpack provider to supply
 * its own VBoxManage command handler for "cloud" based on --provider
 * as the selector.
 *
 * Processing of --provider and --profile should not be postponed
 * until the leaf command handler, but rather happen immediately, so
 * do this here at our earliest opportunity (without actually doing it
 * in handleCloud).
 */
RTEXITCODE
handleCloudMachine(HandlerArg *a, int iFirst,
                   const char *pcszProviderName,
                   const char *pcszProfileName)
{
    CMachineHandlerArg handlerArg(*a);
    int vrc = getCloudClient(handlerArg, pcszProviderName, pcszProfileName);
    if (RT_FAILURE(vrc))
        return RTEXITCODE_FAILURE;

    return handleCloudMachineImpl(&handlerArg, iFirst);
}


/*
 * Select cloud provider to use based on the --provider option to the
 * "cloud" command.  The option is not mandatory if only a single
 * provider is available.
 */
static int
selectCloudProvider(ComPtr<ICloudProvider> &pProvider,
                    const ComPtr<IVirtualBox> &pVirtualBox,
                    const char *pcszProviderName)
{
    HRESULT hrc;

    ComPtr<ICloudProviderManager> pCloudProviderManager;
    CHECK_ERROR2_RET(hrc, pVirtualBox,
        COMGETTER(CloudProviderManager)(pCloudProviderManager.asOutParam()),
            VERR_GENERAL_FAILURE);


    /*
     * If the provider is explicitly specified, just look it up and
     * return.
     */
    if (pcszProviderName != NULL)
    {
        /*
         * Should we also provide a way to specify the provider also
         * by its id?  Is it even useful?  If so, should we use a
         * different option or check if the provider name looks like
         * an id and used a different getter?
         */
        CHECK_ERROR2_RET(hrc, pCloudProviderManager,
            GetProviderByShortName(com::Bstr(pcszProviderName).raw(),
                                   pProvider.asOutParam()),
                VERR_NOT_FOUND);

        return VINF_SUCCESS;
    }


    /*
     * We have only one provider and it's not clear if we will ever
     * have more than one.  Forcing the user to explicitly specify the
     * only provider available is not very nice.  So try to be
     * friendly.
     */
    com::SafeIfaceArray<ICloudProvider> aProviders;
    CHECK_ERROR2_RET(hrc, pCloudProviderManager,
        COMGETTER(Providers)(ComSafeArrayAsOutParam(aProviders)),
            VERR_GENERAL_FAILURE);

    if (aProviders.size() == 0)
    {
        RTMsgError(CloudMachine::tr("cloud: no providers available"));
        return VERR_NOT_FOUND;
    }

    if (aProviders.size() > 1)
    {
        RTMsgError(CloudMachine::tr("cloud: multiple providers available,"
                                    " '--provider' option is required"));
        return VERR_MISSING;
    }

    /* Do RTMsgInfo telling the user which one was selected? */
    pProvider = aProviders[0];
    return VINF_SUCCESS;
}


/*
 * Select cloud profile to use based on the --profile option to the
 * "cloud" command.  The option is not mandatory if only a single
 * profile exists.
 */
static int
selectCloudProfile(ComPtr<ICloudProfile> &pProfile,
                   const ComPtr<ICloudProvider> &pProvider,
                   const char *pcszProfileName)
{
    HRESULT hrc;

    /*
     * If the profile is explicitly specified, just look it up and
     * return.
     */
    if (pcszProfileName != NULL)
    {
        CHECK_ERROR2_RET(hrc, pProvider,
            GetProfileByName(com::Bstr(pcszProfileName).raw(),
                             pProfile.asOutParam()),
                VERR_NOT_FOUND);

        return VINF_SUCCESS;
    }


    /*
     * If the user has just one profile for this provider, don't force
     * them to specify it.  I'm not entirely sure about this one,
     * actually.  It's nice for interactive use, but it might be not
     * forward compatible if used in a script and then when another
     * profile is created the script starts failing.  I'd say, give
     * them enough rope...
     */
    com::SafeIfaceArray<ICloudProfile> aProfiles;
    CHECK_ERROR2_RET(hrc, pProvider,
        COMGETTER(Profiles)(ComSafeArrayAsOutParam(aProfiles)),
            VERR_GENERAL_FAILURE);

    if (aProfiles.size() == 0)
    {
        RTMsgError(CloudMachine::tr("cloud: no profiles exist"));
        return VERR_NOT_FOUND;
    }

    if (aProfiles.size() > 1)
    {
        RTMsgError(CloudMachine::tr("cloud: multiple profiles exist, '--profile' option is required"));
        return VERR_MISSING;
    }

    /* Do RTMsgInfo telling the user which one was selected? */
    pProfile = aProfiles[0];
    return VINF_SUCCESS;
}


static int
getCloudClient(CMachineHandlerArg &a,
                const char *pcszProviderName,
                const char *pcszProfileName)
{
    ComPtr<ICloudProvider> pProvider;
    int vrc = selectCloudProvider(pProvider, a.virtualBox, pcszProviderName);
    if (RT_FAILURE(vrc))
        return vrc;

    ComPtr<ICloudProfile> pProfile;
    vrc = selectCloudProfile(pProfile, pProvider, pcszProfileName);
    if (RT_FAILURE(vrc))
        return vrc;

    ComPtr<ICloudClient> pCloudClient;
    CHECK_ERROR2I_RET(pProfile, CreateCloudClient(pCloudClient.asOutParam()), VERR_GENERAL_FAILURE);

    a.pClient = pCloudClient;
    return VINF_SUCCESS;
}


static HRESULT
getMachineList(com::SafeIfaceArray<ICloudMachine> &aMachines,
               const ComPtr<ICloudClient> &pClient)
{
    HRESULT hrc;

    ComPtr<IProgress> pListProgress;
    CHECK_ERROR2_RET(hrc, pClient,
        ReadCloudMachineList(pListProgress.asOutParam()),
            hrc);

    hrc = showProgress(pListProgress, SHOW_PROGRESS_NONE);
    if (FAILED(hrc))
        return hrc;

    CHECK_ERROR2_RET(hrc, pClient,
        COMGETTER(CloudMachineList)(ComSafeArrayAsOutParam(aMachines)),
            hrc);

    return S_OK;
}


static HRESULT
getMachineById(CMachineHandlerArg *a)
{
    HRESULT hrc;

    ComPtr<ICloudMachine> pMachine;
    CHECK_ERROR2_RET(hrc, a->pClient,
        GetCloudMachine(com::Bstr(a->pcszSpec).raw(),
                        pMachine.asOutParam()), hrc);

    ComPtr<IProgress> pRefreshProgress;
    CHECK_ERROR2_RET(hrc, pMachine,
        Refresh(pRefreshProgress.asOutParam()), hrc);

    hrc = showProgress(pRefreshProgress, SHOW_PROGRESS_NONE);
    if (FAILED(hrc))
        return hrc;

    a->pMachine = pMachine;
    return S_OK;
}


static HRESULT
getMachineByName(CMachineHandlerArg *a)
{
    HRESULT hrc;

    com::SafeIfaceArray<ICloudMachine> aMachines;
    hrc = getMachineList(aMachines, a->pClient);
    if (FAILED(hrc))
        return hrc;

    const size_t cMachines = aMachines.size();
    if (cMachines == 0)
        return VBOX_E_OBJECT_NOT_FOUND;

    ComPtr<ICloudMachine> pMachineFound;
    for (size_t i = 0; i < cMachines; ++i)
    {
        const ComPtr<ICloudMachine> pMachine = aMachines[i];

        com::Bstr bstrName;
        CHECK_ERROR2_RET(hrc, pMachine,
            COMGETTER(Name)(bstrName.asOutParam()),
                hrc);

        if (!bstrName.equals(a->pcszSpec))
            continue;

        if (pMachineFound.isNull())
            pMachineFound = pMachine;
        else
        {
            com::Bstr bstrId1, bstrId2;
            CHECK_ERROR2_RET(hrc, pMachineFound,
                COMGETTER(Id)(bstrId1.asOutParam()),
                    hrc);
            CHECK_ERROR2_RET(hrc, pMachine,
                COMGETTER(Id)(bstrId2.asOutParam()),
                    hrc);

            RTMsgError(CloudMachine::tr("ambiguous name: %ls and %ls"), bstrId1.raw(), bstrId2.raw());
            return VBOX_E_OBJECT_NOT_FOUND;
        }
    }

    if (pMachineFound.isNull())
        return VBOX_E_OBJECT_NOT_FOUND;

    a->pMachine = pMachineFound;
    return S_OK;
}


/*
 * Try to find the machine refered by pcszWhatever.  If the look up by
 * id fails we might want to fallback to look up by name, b/c someone
 * might want to use a uuid as a display name of a machine.  But cloud
 * lookups are not fast, so that would be incurring performance
 * penalty for typos or for machines that are gone.  Should provide
 * explicit --id/--name options instead.
 */
static HRESULT
getMachineByGuess(CMachineHandlerArg *a)
{
    HRESULT hrc;

    RTUUID Uuid;
    int vrc = RTUuidFromStr(&Uuid, a->pcszSpec);
    if (RT_SUCCESS(vrc))
        hrc = getMachineById(a);
    else
        hrc = getMachineByName(a);

    if (FAILED(hrc))
        return hrc;

    return S_OK;
}



/*
 * RTGETOPTINIT_FLAGS_NO_STD_OPTS recognizes both --help and --version
 * and we don't want the latter.  It's easier to add one line of this
 * macro to the s_aOptions initializers than to filter out --version.
 */
#define CLOUD_MACHINE_RTGETOPTDEF_HELP                                      \
        { "--help",         'h',                    RTGETOPT_REQ_NOTHING }, \
        { "-help",          'h',                    RTGETOPT_REQ_NOTHING }, \
        { "help",           'h',                    RTGETOPT_REQ_NOTHING }, \
        { "-?",             'h',                    RTGETOPT_REQ_NOTHING }

static RTEXITCODE
errThereCanBeOnlyOne()
{
    return RTMsgErrorExit(RTEXITCODE_SYNTAX,
                          CloudMachine::tr("only one machine can be specified"));
}


#define CLOUD_MACHINE_RTGETOPTDEF_MACHINE               \
        { "--id",       'i',    RTGETOPT_REQ_STRING },  \
        { "--name",     'n',    RTGETOPT_REQ_STRING }


/*
 * Almost all the cloud machine commands take a machine argument, so
 * factor out the code to fish it out from the command line.
 *
 * ch - option should be processed by the caller.
 * VINF_SUCCESS - option was processed.
 * VERR_PARSE_ERROR - RTEXITCODE_SYNTAX
 * Other IPRT errors - RTEXITCODE_FAILURE
 */
static int
checkMachineSpecArgument(CMachineHandlerArg *a,
                         int ch, const RTGETOPTUNION &Val)
{
    int vrc;

    switch (ch)
    {
        /*
         * Note that we don't used RTGETOPT_REQ_UUID here as it would
         * be too limiting.  First, we need the original string for
         * the API call, not the UUID, and second, if the UUID has bad
         * forward RTGetOptPrintError doesn't have access to the
         * option argument for the error message.  So do the format
         * check ourselves.
         */
        case 'i':               /* --id */
        {
            const char *pcszId = Val.psz;

            if (a->pcszSpec != NULL)
            {
                errThereCanBeOnlyOne();
                return VERR_PARSE_ERROR;
            }

            RTUUID Uuid;
            vrc = RTUuidFromStr(&Uuid, pcszId);
            if (RT_FAILURE(vrc))
            {
                RTMsgError(CloudMachine::tr("not a valid uuid: %s"), pcszId);
                return VERR_PARSE_ERROR;
            }

            a->pcszSpec = pcszId;
            a->enmSpecKind = CMachineHandlerArg::ID;
            return VINF_SUCCESS;
        }

        case 'n':               /* --name */
        {
            const char *pcszName = Val.psz;

            if (a->pcszSpec != NULL)
            {
                errThereCanBeOnlyOne();
                return VERR_PARSE_ERROR;
            }

            a->pcszSpec = pcszName;
            a->enmSpecKind = CMachineHandlerArg::NAME;
            return VINF_SUCCESS;
        }

        /*
         * Plain word (no dash/es).  This must name a machine, though
         * we have to guess whether it's an id or a name.
         */
        case VINF_GETOPT_NOT_OPTION:
        {
            const char *pcszNameOrId = Val.psz;

            if (a->pcszSpec != NULL)
            {
                errThereCanBeOnlyOne();
                return VERR_PARSE_ERROR;
            }

            a->pcszSpec = pcszNameOrId;
            a->enmSpecKind = CMachineHandlerArg::GUESS;
            return VINF_SUCCESS;
        }

        /* might as well do it here */
        case 'h':               /* --help */
        {
            printHelp(g_pStdOut);
            return VINF_CALLBACK_RETURN;
        }
    }

    /* let the caller deal with it */
    return VINF_NOT_SUPPORTED;
}


static HRESULT
getMachineBySpec(CMachineHandlerArg *a)
{
    HRESULT hrc = E_FAIL;

    if (a->pcszSpec == NULL)
    {
        RTMsgErrorExit(RTEXITCODE_SYNTAX, CloudMachine::tr("machine not specified"));
        return E_FAIL;
    }

    if (a->pcszSpec[0] == '\0')
    {
        RTMsgError(CloudMachine::tr("machine name is empty"));
        return E_FAIL;
    }

    switch (a->enmSpecKind)
    {
        case CMachineHandlerArg::ID:
            hrc = getMachineById(a);
            if (FAILED(hrc))
            {
                if (hrc == VBOX_E_OBJECT_NOT_FOUND)
                    RTMsgError(CloudMachine::tr("unable to find machine with id %s"), a->pcszSpec);
                return hrc;
            }
            break;

        case CMachineHandlerArg::NAME:
            hrc = getMachineByName(a);
            if (FAILED(hrc))
            {
                if (hrc == VBOX_E_OBJECT_NOT_FOUND)
                    RTMsgError(CloudMachine::tr("unable to find machine with name %s"), a->pcszSpec);
                return hrc;
            }
            break;

        case CMachineHandlerArg::GUESS:
            hrc = getMachineByGuess(a);
            if (FAILED(hrc))
            {
                if (hrc == VBOX_E_OBJECT_NOT_FOUND)
                    RTMsgError(CloudMachine::tr("unable to find machine %s"), a->pcszSpec);
                return hrc;
            }
            break;
    }

    /* switch was exhaustive (and successful) */
    AssertReturn(SUCCEEDED(hrc), E_FAIL);
    return S_OK;
}




/*
 * cloud machine [--id id | --name name] command ...
 *
 * We allow machine to be specified after "machine" but only with an
 * explicit option for the obvious reason.  We will also check for
 * these options and machine spec as a plain words argument after the
 * command word, so user can use either of:
 *
 *   cloud machine --name foo start
 *   cloud machine start --name foo
 *   cloud machine start foo
 *
 * This will accept e.g.  cloud machine --name foo list ... b/c we
 * don't yet know that it's "list" that is coming, so commands that
 * don't take machine argument check that separately when called.  One
 * side effect of this is that specifying several machines or using a
 * syntactically invalid id will be reported as such, not as an
 * unknown option, but that's a relatively minor nit.
 */
static RTEXITCODE
handleCloudMachineImpl(CMachineHandlerArg *a, int iFirst)
{
    enum
    {
        kMachineIota = 1000,
        kMachine_ConsoleHistory,
        kMachine_Info,
        kMachine_List,
        kMachine_Powerdown,
        kMachine_Reboot,
        kMachine_Reset,
        kMachine_Shutdown,
        kMachine_Start,
        kMachine_Terminate,
    };

    // setCurrentSubcommand(HELP_SCOPE_CLOUD_MACHINE);
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "console-history",    kMachine_ConsoleHistory,    RTGETOPT_REQ_NOTHING },
        { "consolehistory",     kMachine_ConsoleHistory,    RTGETOPT_REQ_NOTHING },
        { "info",               kMachine_Info,              RTGETOPT_REQ_NOTHING },
        { "list",               kMachine_List,              RTGETOPT_REQ_NOTHING },
        { "powerdown",          kMachine_Powerdown,         RTGETOPT_REQ_NOTHING },
        { "reboot",             kMachine_Reboot,            RTGETOPT_REQ_NOTHING },
        { "reset",              kMachine_Reset,             RTGETOPT_REQ_NOTHING },
        { "shutdown",           kMachine_Shutdown,          RTGETOPT_REQ_NOTHING },
        { "start",              kMachine_Start,             RTGETOPT_REQ_NOTHING },
        { "terminate",          kMachine_Terminate,         RTGETOPT_REQ_NOTHING },
        CLOUD_MACHINE_RTGETOPTDEF_MACHINE,
        CLOUD_MACHINE_RTGETOPTDEF_HELP
    };

    RTGETOPTSTATE OptState;
    int vrc = RTGetOptInit(&OptState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions),
                           iFirst, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    AssertRCReturn(vrc, RTMsgErrorExit(RTEXITCODE_INIT, CloudMachine::tr("cloud machine: RTGetOptInit: %Rra"), vrc));

    int ch;
    RTGETOPTUNION Val;
    while ((ch = RTGetOpt(&OptState, &Val)) != 0)
    {
        if (RT_FAILURE(ch))
            return RTGetOptPrintError(ch, &Val);

        /*
         * Check for an unknown word first: checkMachineSpecArgument()
         * would try to interpret that as a machine id/name.
         */
        if (ch == VINF_GETOPT_NOT_OPTION)
            return RTMsgErrorExit(RTEXITCODE_SYNTAX,
                                  CloudMachine::tr("Invalid sub-command: %s"), Val.psz);

        /*
         * Allow --id/--name after "machine", before the command.
         * Also handles --help.
         */
        vrc = checkMachineSpecArgument(a, ch, Val);
        if (vrc == VINF_SUCCESS)
            continue;
        if (vrc == VINF_CALLBACK_RETURN)
            return RTEXITCODE_SUCCESS;
        if (vrc == VERR_PARSE_ERROR)
            return RTEXITCODE_SYNTAX;

        /*
         * Dispatch to command implementation ([ab]use getopt to do
         * string comparisons for us).
         */
        switch (ch)
        {
            case kMachine_ConsoleHistory:
                return handleCloudMachineConsoleHistory(a, OptState.iNext);

            case kMachine_Info:
                return handleCloudMachineInfo(a, OptState.iNext);

            case kMachine_List:
                return listCloudMachinesImpl(a, OptState.iNext);

            case kMachine_Powerdown:
                return handleCloudMachinePowerdown(a, OptState.iNext);

            case kMachine_Reboot:
                return handleCloudMachineReboot(a, OptState.iNext);

            case kMachine_Reset:
                return handleCloudMachineReset(a, OptState.iNext);

            case kMachine_Shutdown:
                return handleCloudMachineShutdown(a, OptState.iNext);

            case kMachine_Start:
                return handleCloudMachineStart(a, OptState.iNext);

            case kMachine_Terminate:
                return handleCloudMachineTerminate(a, OptState.iNext);

            default:            /* should never happen */
                return RTMsgErrorExit(RTEXITCODE_INIT,
                                      CloudMachine::tr("cloud machine: internal error: %d"), ch);
        }
    }

    return RTMsgErrorExit(RTEXITCODE_SYNTAX,
                          CloudMachine::tr("cloud machine: command required\n"
                                           "Try '--help' for more information."));
}


/*
 * cloud list machines
 *
 * The "cloud list" prefix handling is in VBoxManageCloud.cpp, so this
 * function is not static.  See handleCloudMachine() for the
 * explanation early provider/profile lookup.
 */
RTEXITCODE
listCloudMachines(HandlerArg *a, int iFirst,
                  const char *pcszProviderName,
                  const char *pcszProfileName)
{
    CMachineHandlerArg handlerArg(*a);
    int vrc = getCloudClient(handlerArg, pcszProviderName, pcszProfileName);
    if (RT_FAILURE(vrc))
        return RTEXITCODE_FAILURE;

    return listCloudMachinesImpl(&handlerArg, iFirst);
}


/*
 * cloud machine list   # convenience alias
 * cloud list machines  # see above
 */
static RTEXITCODE
listCloudMachinesImpl(CMachineHandlerArg *a, int iFirst)
{
    // setCurrentSubcommand(HELP_SCOPE_CLOUD_MACHINE_LIST);
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--long",         'l',                    RTGETOPT_REQ_NOTHING },
        { "--sort",         's',                    RTGETOPT_REQ_NOTHING },
        CLOUD_MACHINE_RTGETOPTDEF_HELP
    };

    enum kFormatEnum { kFormat_Short, kFormat_Long };
    kFormatEnum enmFormat = kFormat_Short;

    enum kSortOrderEnum { kSortOrder_None, kSortOrder_Name, kSortOrder_Id };
    kSortOrderEnum enmSortOrder = kSortOrder_None;

    if (a->pcszSpec != NULL)
        return RTMsgErrorExit(RTEXITCODE_SYNTAX,
                              CloudMachine::tr("cloud machine list: unexpected machine argument"));


    RTGETOPTSTATE OptState;
    int vrc = RTGetOptInit(&OptState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions),
                           iFirst, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    AssertRCReturn(vrc, RTMsgErrorExit(RTEXITCODE_INIT, CloudMachine::tr("cloud machine list: RTGetOptInit: %Rra"), vrc));

    int ch;
    RTGETOPTUNION Val;
    while ((ch = RTGetOpt(&OptState, &Val)) != 0)
    {
        switch (ch)
        {
            case 'l':
                enmFormat = kFormat_Long;
                break;

            case 's':
                /** @todo optional argument to select the sort key? */
                enmSortOrder = kSortOrder_Name;
                break;

            case 'h':           /* --help */
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;


            case VINF_GETOPT_NOT_OPTION:
                return RTMsgErrorExit(RTEXITCODE_SYNTAX,
                                      CloudMachine::tr("Invalid sub-command: %s"), Val.psz);

            default:
                return RTGetOptPrintError(ch, &Val);
        }
    }

    com::SafeIfaceArray<ICloudMachine> aMachines;
    HRESULT hrc = getMachineList(aMachines, a->pClient);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    const size_t cMachines = aMachines.size();
    if (cMachines == 0)
        return RTEXITCODE_SUCCESS;


    /*
     * Get names/ids that we need for the short output and to sort the
     * list.
     */
    std::vector<ComPtr<ICloudMachine> > vMachines(cMachines);
    std::vector<com::Bstr> vBstrNames(cMachines);
    std::vector<com::Bstr> vBstrIds(cMachines);
    for (size_t i = 0; i < cMachines; ++i)
    {
        vMachines[i] = aMachines[i];

        CHECK_ERROR2_RET(hrc, vMachines[i],
            COMGETTER(Name)(vBstrNames[i].asOutParam()),
                RTEXITCODE_FAILURE);

        CHECK_ERROR2_RET(hrc, vMachines[i],
            COMGETTER(Id)(vBstrIds[i].asOutParam()),
                RTEXITCODE_FAILURE);
    }


    /*
     * Sort the list if necessary.  The sort is indirect via an
     * intermediate array of indexes.
     */
    std::vector<size_t> vIndexes(cMachines);
    for (size_t i = 0; i < cMachines; ++i)
        vIndexes[i] = i;

    if (enmSortOrder != kSortOrder_None)
    {
        struct SortBy {
            const std::vector<com::Bstr> &ks;
            SortBy(const std::vector<com::Bstr> &aKeys) : ks(aKeys) {}
            bool operator() (size_t l, size_t r) { return ks[l] < ks[r]; }
        };

        std::sort(vIndexes.begin(), vIndexes.end(),
                  SortBy(enmSortOrder == kSortOrder_Name
                         ? vBstrNames : vBstrIds));
    }


    if (enmFormat == kFormat_Short)
    {
        for (size_t i = 0; i < cMachines; ++i)
        {
            const size_t idx = vIndexes[i];
            const com::Bstr &bstrId = vBstrIds[idx];
            const com::Bstr &bstrName = vBstrNames[idx];

            RTPrintf("%ls %ls\n", bstrId.raw(), bstrName.raw());
        }
    }
    else // kFormat_Long
    {
        for (size_t i = 0; i < cMachines; ++i)
        {
            const size_t idx = vIndexes[i];
            const ComPtr<ICloudMachine> &pMachine = vMachines[idx];

            if (i != 0)
                RTPrintf("\n");
            printMachineInfo(pMachine);
        }
    }

    return RTEXITCODE_SUCCESS;
}


/*
 * cloud showvminfo "id"
 *
 * Alias for "cloud machine info" that tries to match the local vm
 * counterpart.
 */
RTEXITCODE
handleCloudShowVMInfo(HandlerArg *a, int iFirst,
                      const char *pcszProviderName,
                      const char *pcszProfileName)
{
    CMachineHandlerArg handlerArg(*a);
    int vrc = getCloudClient(handlerArg, pcszProviderName, pcszProfileName);
    if (RT_FAILURE(vrc))
        return RTEXITCODE_FAILURE;

    return handleCloudMachineInfo(&handlerArg, iFirst);
}


/*
 * cloud machine info "id" ...
 */
static RTEXITCODE
handleCloudMachineInfo(CMachineHandlerArg *a, int iFirst)
{
    enum
    {
        kMachineInfoIota = 1000,
        kMachineInfo_Details,
    };

    // setCurrentSubcommand(HELP_SCOPE_CLOUD_MACHINE_INFO);
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--details", kMachineInfo_Details, RTGETOPT_REQ_NOTHING },
        CLOUD_MACHINE_RTGETOPTDEF_MACHINE,
        CLOUD_MACHINE_RTGETOPTDEF_HELP
    };

    RTGETOPTSTATE OptState;
    int vrc = RTGetOptInit(&OptState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions),
                           iFirst, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    AssertRCReturn(vrc, RTMsgErrorExit(RTEXITCODE_INIT, "RTGetOptInit: %Rra", vrc));

    int ch;
    RTGETOPTUNION Val;
    while ((ch = RTGetOpt(&OptState, &Val)) != 0)
    {
        vrc = checkMachineSpecArgument(a, ch, Val);
        if (vrc == VINF_SUCCESS)
            continue;
        if (vrc == VINF_CALLBACK_RETURN)
            return RTEXITCODE_SUCCESS;
        if (vrc == VERR_PARSE_ERROR)
            return RTEXITCODE_SYNTAX;

        switch (ch)
        {
            case kMachineInfo_Details:
                /* currently no-op */
                break;

            default:
                return RTGetOptPrintError(ch, &Val);
        }
    }

    HRESULT hrc = getMachineBySpec(a);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    /* end of boilerplate */


    hrc = printMachineInfo(a->pMachine);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    return RTEXITCODE_SUCCESS;
}


static HRESULT
printMachineInfo(const ComPtr<ICloudMachine> &pMachine)
{
    HRESULT hrc;

    com::Bstr bstrId;
    CHECK_ERROR2_RET(hrc, pMachine,
        COMGETTER(Id)(bstrId.asOutParam()),
            hrc);
    RTPrintf("UUID: %ls\n", bstrId.raw());


    /*
     * Check if the machine is accessible and print the error
     * message if not.
     */
    BOOL fAccessible = FALSE;
    CHECK_ERROR2_RET(hrc, pMachine,
        COMGETTER(Accessible)(&fAccessible), hrc);

    if (!fAccessible)
    {
        RTMsgError(CloudMachine::tr("machine is not accessible")); // XXX: Id?

        ComPtr<IVirtualBoxErrorInfo> pErrorInfo;
        CHECK_ERROR2_RET(hrc, pMachine,
            COMGETTER(AccessError)(pErrorInfo.asOutParam()), hrc);

        while (!pErrorInfo.isNull())
        {
            com::Bstr bstrText;
            CHECK_ERROR2_RET(hrc, pErrorInfo,
                COMGETTER(Text)(bstrText.asOutParam()), hrc);
            RTStrmPrintf(g_pStdErr, "%ls\n", bstrText.raw());

            CHECK_ERROR2_RET(hrc, pErrorInfo,
                COMGETTER(Next)(pErrorInfo.asOutParam()), hrc);
        }

        return E_FAIL;
    }


    /*
     * The machine seems to be ok, print its details.
     */
    CloudMachineState_T enmState;
    CHECK_ERROR2_RET(hrc, pMachine,
        COMGETTER(State)(&enmState),
            hrc);
    switch (enmState) {
        case CloudMachineState_Invalid:
            RTPrintf(CloudMachine::tr("State: Invalid (%RU32)\n"), CloudMachineState_Invalid);
            break;

        case CloudMachineState_Provisioning:
            RTPrintf(CloudMachine::tr("State: Provisioning (%RU32)\n"), CloudMachineState_Provisioning);
            break;

        case CloudMachineState_Running:
            RTPrintf(CloudMachine::tr("State: Running (%RU32)\n"), CloudMachineState_Running);
            break;

        case CloudMachineState_Starting:
            RTPrintf(CloudMachine::tr("State: Starting (%RU32)\n"), CloudMachineState_Starting);
            break;

        case CloudMachineState_Stopping:
            RTPrintf(CloudMachine::tr("State: Stopping (%RU32)\n"), CloudMachineState_Stopping);
            break;

        case CloudMachineState_Stopped:
            RTPrintf(CloudMachine::tr("State: Stopped (%RU32)\n"), CloudMachineState_Stopped);
            break;

        case CloudMachineState_CreatingImage:
            RTPrintf(CloudMachine::tr("State: CreatingImage (%RU32)\n"), CloudMachineState_CreatingImage);
            break;

        case CloudMachineState_Terminating:
            RTPrintf(CloudMachine::tr("State: Terminating (%RU32)\n"), CloudMachineState_Terminating);
            break;

        case CloudMachineState_Terminated:
            RTPrintf(CloudMachine::tr("State: Terminated (%RU32)\n"), CloudMachineState_Terminated);
            break;

        default:
            RTPrintf(CloudMachine::tr("State: Unknown state (%RU32)\n"), enmState);
    }

    ComPtr<IForm> pDetails;
    CHECK_ERROR2_RET(hrc, pMachine,
        GetDetailsForm(pDetails.asOutParam()), hrc);

    if (RT_UNLIKELY(pDetails.isNull()))
    {
        RTMsgError(CloudMachine::tr("null details")); /* better error message? */
        return E_FAIL;
    }

    com::SafeIfaceArray<IFormValue> aValues;
    CHECK_ERROR2_RET(hrc, pDetails,
        COMGETTER(Values)(ComSafeArrayAsOutParam(aValues)), hrc);
    for (size_t i = 0; i < aValues.size(); ++i)
    {
        hrc = printFormValue(aValues[i]);
        if (FAILED(hrc))
            return hrc;
    }

    return S_OK;
}


static HRESULT
printFormValue(const ComPtr<IFormValue> &pValue)
{
    HRESULT hrc;

    BOOL fVisible = FALSE;
    CHECK_ERROR2_RET(hrc, pValue,
        COMGETTER(Visible)(&fVisible), hrc);
    if (!fVisible)
        return S_OK;


    com::Bstr bstrLabel;
    CHECK_ERROR2_RET(hrc, pValue,
        COMGETTER(Label)(bstrLabel.asOutParam()), hrc);

    FormValueType_T enmType;
    CHECK_ERROR2_RET(hrc, pValue,
        COMGETTER(Type)(&enmType), hrc);

    switch (enmType)
    {
        case FormValueType_Boolean:
        {
            ComPtr<IBooleanFormValue> pBoolValue;
            hrc = pValue.queryInterfaceTo(pBoolValue.asOutParam());
            if (FAILED(hrc))
            {
                RTStrmPrintf(g_pStdErr,
                             CloudMachine::tr("%ls: unable to convert to boolean value\n"),
                             bstrLabel.raw());
                break;
            }

            BOOL fSelected;
            hrc = pBoolValue->GetSelected(&fSelected);
            if (FAILED(hrc))
            {
                RTStrmPrintf(g_pStdOut,
                    "%ls: %Rhra", bstrLabel.raw(), hrc);
                break;
            }

            RTPrintf("%ls: %RTbool\n",
                bstrLabel.raw(), RT_BOOL(fSelected));
            break;
        }

        case FormValueType_String:
        {
            ComPtr<IStringFormValue> pStrValue;
            hrc = pValue.queryInterfaceTo(pStrValue.asOutParam());
            if (FAILED(hrc))
            {
                RTStrmPrintf(g_pStdErr,
                             CloudMachine::tr("%ls: unable to convert to string value\n"),
                             bstrLabel.raw());
                break;
            }

            /*
             * GUI hack: if clipboard string is set, it contains
             * untruncated long value, usually full OCID, so check it
             * first.  Make this selectable with an option?
             */
            com::Bstr bstrValue;
            hrc = pStrValue->COMGETTER(ClipboardString)(bstrValue.asOutParam());
            if (FAILED(hrc))
            {
                RTStrmPrintf(g_pStdOut,
                    "%ls: %Rhra", bstrLabel.raw(), hrc);
                break;
            }

            if (bstrValue.isEmpty())
            {
                hrc = pStrValue->GetString(bstrValue.asOutParam());
                if (FAILED(hrc))
                {
                    RTStrmPrintf(g_pStdOut,
                        "%ls: %Rhra", bstrLabel.raw(), hrc);
                    break;
                }
            }

            RTPrintf("%ls: %ls\n",
                bstrLabel.raw(), bstrValue.raw());
            break;
        }

        case FormValueType_RangedInteger:
        {
            ComPtr<IRangedIntegerFormValue> pIntValue;
            hrc = pValue.queryInterfaceTo(pIntValue.asOutParam());
            if (FAILED(hrc))
            {
                RTStrmPrintf(g_pStdErr,
                             CloudMachine::tr("%ls: unable to convert to integer value\n"),
                             bstrLabel.raw());
                break;
            }

            LONG lValue;
            hrc = pIntValue->GetInteger(&lValue);
            if (FAILED(hrc))
            {
                RTStrmPrintf(g_pStdOut,
                    "%ls: %Rhra", bstrLabel.raw(), hrc);
                break;
            }

            RTPrintf("%ls: %RI64\n",
                bstrLabel.raw(), (int64_t)lValue);
            break;
        }

        case FormValueType_Choice:
        {
            ComPtr<IChoiceFormValue> pChoiceValue;
            hrc = pValue.queryInterfaceTo(pChoiceValue.asOutParam());
            if (FAILED(hrc))
            {
                RTStrmPrintf(g_pStdErr,
                             CloudMachine::tr("%ls: unable to convert to choice value\n"),
                             bstrLabel.raw());
                break;
            }

            com::SafeArray<BSTR> aValues;
            hrc = pChoiceValue->COMGETTER(Values)(ComSafeArrayAsOutParam(aValues));
            if (FAILED(hrc))
            {
                RTStrmPrintf(g_pStdOut,
                             CloudMachine::tr("%ls: values: %Rhra"),
                             bstrLabel.raw(), hrc);
                break;
            }

            LONG idxSelected = -1;
            hrc = pChoiceValue->GetSelectedIndex(&idxSelected);
            if (FAILED(hrc))
            {
                RTStrmPrintf(g_pStdOut,
                             CloudMachine::tr("%ls: selectedIndex: %Rhra"),
                             bstrLabel.raw(), hrc);
                break;
            }

            if (idxSelected < 0 || (size_t)idxSelected >  aValues.size())
            {
                RTStrmPrintf(g_pStdOut,
                             CloudMachine::tr("%ls: selected index %RI64 out of range [0, %zu)\n"),
                             bstrLabel.raw(), (int64_t)idxSelected, aValues.size());
                break;
            }

            RTPrintf("%ls: %ls\n",
                bstrLabel.raw(), aValues[idxSelected]);
            break;
        }

        default:
        {
            RTStrmPrintf(g_pStdOut, CloudMachine::tr("unknown value type %RU32\n"), enmType);
            break;
        }
    }

    return S_OK;
}


/*
 * Boilerplate code to get machine by name/id from the arguments.
 * Shared by action subcommands b/c they currently don't have any
 * extra options (but we can't use this for e.g. "info" that has
 * --details).
 */
static RTEXITCODE
getMachineFromArgs(CMachineHandlerArg *a, int iFirst)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        CLOUD_MACHINE_RTGETOPTDEF_MACHINE,
        CLOUD_MACHINE_RTGETOPTDEF_HELP
    };

    RTGETOPTSTATE OptState;
    int vrc = RTGetOptInit(&OptState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions),
                           iFirst, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    AssertRCReturn(vrc, RTMsgErrorExit(RTEXITCODE_INIT, /* internal error */ "RTGetOptInit: %Rra", vrc));

    int ch;
    RTGETOPTUNION Val;
    while ((ch = RTGetOpt(&OptState, &Val)) != 0)
    {
        vrc = checkMachineSpecArgument(a, ch, Val);
        if (vrc == VINF_SUCCESS)
            continue;
        if (vrc == VINF_CALLBACK_RETURN)
            return RTEXITCODE_SUCCESS;
        if (vrc == VERR_PARSE_ERROR)
            return RTEXITCODE_SYNTAX;

        switch (ch)
        {
            /* no other options currently */
            default:
                return RTGetOptPrintError(ch, &Val);
        }
    }

    HRESULT hrc = getMachineBySpec(a);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    return RTEXITCODE_SUCCESS;
}


/*
 * cloud machine start "id"
 */
static RTEXITCODE
handleCloudMachineStart(CMachineHandlerArg *a, int iFirst)
{
    HRESULT hrc;

    // setCurrentSubcommand(HELP_SCOPE_CLOUD_MACHINE_START);
    RTEXITCODE status = getMachineFromArgs(a, iFirst);
    if (status != RTEXITCODE_SUCCESS)
        return status;


    ComPtr<IProgress> pProgress;
    CHECK_ERROR2_RET(hrc, a->pMachine,
        PowerUp(pProgress.asOutParam()),
            RTEXITCODE_FAILURE);

    hrc = showProgress(pProgress, SHOW_PROGRESS_NONE);
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/*
 * cloud machine reboot "id"
 *     "Press" ACPI power button, then power the instance back up.
 */
static RTEXITCODE
handleCloudMachineReboot(CMachineHandlerArg *a, int iFirst)
{
    HRESULT hrc;

    // setCurrentSubcommand(HELP_SCOPE_CLOUD_MACHINE_REBOOT);
    RTEXITCODE status = getMachineFromArgs(a, iFirst);
    if (status != RTEXITCODE_SUCCESS)
        return status;


    ComPtr<IProgress> pProgress;
    CHECK_ERROR2_RET(hrc, a->pMachine,
        Reboot(pProgress.asOutParam()),
            RTEXITCODE_FAILURE);

    hrc = showProgress(pProgress, SHOW_PROGRESS_NONE);
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/*
 * cloud machine reset "id"
 *     Force power down machine, then power the instance back up.
 */
static RTEXITCODE
handleCloudMachineReset(CMachineHandlerArg *a, int iFirst)
{
    HRESULT hrc;

    // setCurrentSubcommand(HELP_SCOPE_CLOUD_MACHINE_RESET);
    RTEXITCODE status = getMachineFromArgs(a, iFirst);
    if (status != RTEXITCODE_SUCCESS)
        return status;


    ComPtr<IProgress> pProgress;
    CHECK_ERROR2_RET(hrc, a->pMachine,
        Reset(pProgress.asOutParam()),
            RTEXITCODE_FAILURE);

    hrc = showProgress(pProgress, SHOW_PROGRESS_NONE);
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/*
 * cloud machine shutdown "id"
 *     "Press" ACPI power button.
 */
static RTEXITCODE
handleCloudMachineShutdown(CMachineHandlerArg *a, int iFirst)
{
    HRESULT hrc;

    // setCurrentSubcommand(HELP_SCOPE_CLOUD_MACHINE_SHUTDOWN);
    RTEXITCODE status = getMachineFromArgs(a, iFirst);
    if (status != RTEXITCODE_SUCCESS)
        return status;


    ComPtr<IProgress> pProgress;
    CHECK_ERROR2_RET(hrc, a->pMachine,
        Shutdown(pProgress.asOutParam()),
            RTEXITCODE_FAILURE);

    hrc = showProgress(pProgress, SHOW_PROGRESS_NONE);
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/*
 * cloud machine powerdown "id"
 *     Yank the power cord.
 */
static RTEXITCODE
handleCloudMachinePowerdown(CMachineHandlerArg *a, int iFirst)
{
    HRESULT hrc;

    // setCurrentSubcommand(HELP_SCOPE_CLOUD_MACHINE_POWERDOWN);
    RTEXITCODE status = getMachineFromArgs(a, iFirst);
    if (status != RTEXITCODE_SUCCESS)
        return status;


    ComPtr<IProgress> pProgress;
    CHECK_ERROR2_RET(hrc, a->pMachine,
        PowerDown(pProgress.asOutParam()),
            RTEXITCODE_FAILURE);

    hrc = showProgress(pProgress, SHOW_PROGRESS_NONE);
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/*
 * cloud machine terminate "id"
 *     Discard the instance running this machine.
 */
static RTEXITCODE
handleCloudMachineTerminate(CMachineHandlerArg *a, int iFirst)
{
    HRESULT hrc;

    // setCurrentSubcommand(HELP_SCOPE_CLOUD_MACHINE_TERMINATE);
    RTEXITCODE status = getMachineFromArgs(a, iFirst);
    if (status != RTEXITCODE_SUCCESS)
        return status;


    ComPtr<IProgress> pProgress;
    CHECK_ERROR2_RET(hrc, a->pMachine,
        Terminate(pProgress.asOutParam()),
            RTEXITCODE_FAILURE);

    hrc = showProgress(pProgress, SHOW_PROGRESS_NONE);
    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/*
 * cloud machine console-history "id"
 */
static RTEXITCODE
handleCloudMachineConsoleHistory(CMachineHandlerArg *a, int iFirst)
{
    HRESULT hrc;

    // setCurrentSubcommand(HELP_SCOPE_CLOUD_MACHINE_CONSOLEHISTORY);
    RTEXITCODE status = getMachineFromArgs(a, iFirst);
    if (status != RTEXITCODE_SUCCESS)
        return status;


    ComPtr<IDataStream> pHistoryStream;
    ComPtr<IProgress> pHistoryProgress;
    CHECK_ERROR2_RET(hrc, a->pMachine,
        GetConsoleHistory(pHistoryStream.asOutParam(),
                          pHistoryProgress.asOutParam()),
            RTEXITCODE_FAILURE);

    hrc = showProgress(pHistoryProgress, SHOW_PROGRESS_NONE);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    bool fEOF = false;
    while (!fEOF)
    {
        com::SafeArray<BYTE> aChunk;
        CHECK_ERROR2_RET(hrc, pHistoryStream,
            Read(64 *_1K, 0, ComSafeArrayAsOutParam(aChunk)),
                RTEXITCODE_FAILURE);
        if (aChunk.size() == 0)
            break;

        RTStrmWrite(g_pStdOut, aChunk.raw(), aChunk.size());
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}
