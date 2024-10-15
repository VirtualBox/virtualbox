/* $Id: VBoxManageCloud.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManageCloud - The cloud related commands.
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

#include <VBox/com/com.h>
#include <VBox/com/string.h>
#include <VBox/com/Guid.h>
#include <VBox/com/array.h>
#include <VBox/com/ErrorInfo.h>
#include <VBox/com/errorprint.h>
#include <VBox/com/VirtualBox.h>

#include <iprt/ctype.h>
#include <iprt/getopt.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/thread.h>
#include <iprt/uuid.h>
#include <iprt/file.h>
#include <iprt/http.h>
#include <VBox/log.h>

#include <iprt/cpp/path.h>

#include "VBoxManage.h"

#include <list>
#include <vector>

using namespace com;//at least for Bstr

DECLARE_TRANSLATION_CONTEXT(Cloud);
DECLARE_TRANSLATION_CONTEXT(CloudMachine);

/**
 * Common Cloud options.
 */
typedef struct
{
    struct {
        const char     *pszProviderName;
        ComPtr<ICloudProvider> pCloudProvider;
    }provider;
    struct {
        const char     *pszProfileName;
        ComPtr<ICloudProfile> pCloudProfile;
    }profile;

} CLOUDCOMMONOPT;
typedef CLOUDCOMMONOPT *PCLOUDCOMMONOPT;

static HRESULT checkAndSetCommonOptions(HandlerArg *a, PCLOUDCOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    Bstr bstrProvider(pCommonOpts->provider.pszProviderName);
    Bstr bstrProfile(pCommonOpts->profile.pszProfileName);

    /* check for required options */
    if (bstrProvider.isEmpty())
    {
        errorSyntax(Cloud::tr("Parameter --provider is required"));
        return E_FAIL;
    }
    if (bstrProfile.isEmpty())
    {
        errorSyntax(Cloud::tr("Parameter --profile is required"));
        return E_FAIL;
    }

    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;
    ComPtr<ICloudProviderManager> pCloudProviderManager;
    CHECK_ERROR2_RET(hrc, pVirtualBox,
                     COMGETTER(CloudProviderManager)(pCloudProviderManager.asOutParam()),
                     RTEXITCODE_FAILURE);

    ComPtr<ICloudProvider> pCloudProvider;
    CHECK_ERROR2_RET(hrc, pCloudProviderManager,
                     GetProviderByShortName(bstrProvider.raw(), pCloudProvider.asOutParam()),
                     RTEXITCODE_FAILURE);
    pCommonOpts->provider.pCloudProvider = pCloudProvider;

    ComPtr<ICloudProfile> pCloudProfile;
    CHECK_ERROR2_RET(hrc, pCloudProvider,
                     GetProfileByName(bstrProfile.raw(), pCloudProfile.asOutParam()),
                     RTEXITCODE_FAILURE);
    pCommonOpts->profile.pCloudProfile = pCloudProfile;

    return hrc;
}


/**
 * List all available cloud instances for the specified cloud provider.
 * Available cloud instance is one which state whether "running" or "stopped".
 *
 * @returns RTEXITCODE
 * @param a is the list of passed arguments
 * @param iFirst is the position of the first unparsed argument in the arguments list
 * @param pCommonOpts is a pointer to the structure CLOUDCOMMONOPT with some common
 * arguments which have been already parsed before
 */
static RTEXITCODE listCloudInstances(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--compartment-id", 'c', RTGETOPT_REQ_STRING },
        { "--state",          's', RTGETOPT_REQ_STRING },
        { "help",             'h', RTGETOPT_REQ_NOTHING },
        { "--help",           'h', RTGETOPT_REQ_NOTHING }
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    Utf8Str strCompartmentId;
    com::SafeArray<CloudMachineState_T> machineStates;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'c':
                strCompartmentId = ValueUnion.psz;
                break;

            case 's':
            {
                const char * const pszState = ValueUnion.psz;

                if (RTStrICmp(pszState, "creatingimage") == 0)
                    machineStates.push_back(CloudMachineState_CreatingImage);
                else if (RTStrICmp(pszState, "paused") == 0) /* XXX */
                    machineStates.push_back(CloudMachineState_Stopped);
                else if (RTStrICmp(pszState, "provisioning") == 0)
                    machineStates.push_back(CloudMachineState_Provisioning);
                else if (RTStrICmp(pszState, "running") == 0)
                    machineStates.push_back(CloudMachineState_Running);
                else if (RTStrICmp(pszState, "starting") == 0)
                    machineStates.push_back(CloudMachineState_Starting);
                else if (RTStrICmp(pszState, "stopped") == 0)
                    machineStates.push_back(CloudMachineState_Stopped);
                else if (RTStrICmp(pszState, "stopping") == 0)
                    machineStates.push_back(CloudMachineState_Stopping);
                else if (RTStrICmp(pszState, "terminated") == 0)
                    machineStates.push_back(CloudMachineState_Terminated);
                else if (RTStrICmp(pszState, "terminating") == 0)
                    machineStates.push_back(CloudMachineState_Terminating);
                else
                    return errorArgument(Cloud::tr("Unknown cloud instance state \"%s\""), pszState);
                break;
            }
            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    HRESULT hrc = S_OK;

    /* Delayed check. It allows us to print help information.*/
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;

    ComPtr<ICloudProviderManager> pCloudProviderManager;
    CHECK_ERROR2_RET(hrc, pVirtualBox,
                     COMGETTER(CloudProviderManager)(pCloudProviderManager.asOutParam()),
                     RTEXITCODE_FAILURE);

    ComPtr<ICloudProvider> pCloudProvider;
    CHECK_ERROR2_RET(hrc, pCloudProviderManager,
                     GetProviderByShortName(Bstr(pCommonOpts->provider.pszProviderName).raw(), pCloudProvider.asOutParam()),
                     RTEXITCODE_FAILURE);

    ComPtr<ICloudProfile> pCloudProfile;
    CHECK_ERROR2_RET(hrc, pCloudProvider,
                     GetProfileByName(Bstr(pCommonOpts->profile.pszProfileName).raw(), pCloudProfile.asOutParam()),
                     RTEXITCODE_FAILURE);

    if (strCompartmentId.isNotEmpty())
    {
        CHECK_ERROR2_RET(hrc, pCloudProfile,
                         SetProperty(Bstr("compartment").raw(), Bstr(strCompartmentId).raw()),
                         RTEXITCODE_FAILURE);
    }
    else
    {
        RTPrintf(Cloud::tr("Parameter \'compartment\' is empty or absent.\n"
                           "Trying to get the compartment from the passed cloud profile \'%s\'\n"),
                 pCommonOpts->profile.pszProfileName);
        Bstr bStrCompartmentId;
        CHECK_ERROR2_RET(hrc, pCloudProfile,
                         GetProperty(Bstr("compartment").raw(), bStrCompartmentId.asOutParam()),
                         RTEXITCODE_FAILURE);
        strCompartmentId = bStrCompartmentId;
        if (strCompartmentId.isNotEmpty())
            RTPrintf(Cloud::tr("Found the compartment \'%s\':\n"), strCompartmentId.c_str());
        else
            return errorSyntax(Cloud::tr("Parameter --compartment-id is required"));
    }

    Bstr bstrProfileName;
    pCloudProfile->COMGETTER(Name)(bstrProfileName.asOutParam());

    ComObjPtr<ICloudClient> oCloudClient;
    CHECK_ERROR2_RET(hrc, pCloudProfile,
                     CreateCloudClient(oCloudClient.asOutParam()),
                     RTEXITCODE_FAILURE);

    ComPtr<IStringArray> pVMNamesHolder;
    ComPtr<IStringArray> pVMIdsHolder;
    com::SafeArray<BSTR> arrayVMNames;
    com::SafeArray<BSTR> arrayVMIds;
    ComPtr<IProgress> pProgress;

    RTPrintf(Cloud::tr("Reply is in the form \'instance name\' = \'instance id\'\n"));

    CHECK_ERROR2_RET(hrc, oCloudClient,
                     ListInstances(ComSafeArrayAsInParam(machineStates),
                                   pVMNamesHolder.asOutParam(),
                                   pVMIdsHolder.asOutParam(),
                                   pProgress.asOutParam()),
                     RTEXITCODE_FAILURE);
    showProgress(pProgress);
    CHECK_PROGRESS_ERROR_RET(pProgress, (Cloud::tr("Failed to list instances")), RTEXITCODE_FAILURE);

    CHECK_ERROR2_RET(hrc,
        pVMNamesHolder, COMGETTER(Values)(ComSafeArrayAsOutParam(arrayVMNames)),
            RTEXITCODE_FAILURE);
    CHECK_ERROR2_RET(hrc,
        pVMIdsHolder, COMGETTER(Values)(ComSafeArrayAsOutParam(arrayVMIds)),
            RTEXITCODE_FAILURE);

    RTPrintf(Cloud::tr("The list of the instances for the cloud profile \'%ls\'\nand compartment \'%s\':\n"),
             bstrProfileName.raw(), strCompartmentId.c_str());
    size_t cIds = arrayVMIds.size();
    size_t cNames = arrayVMNames.size();
    for (size_t k = 0; k < cNames; k++)
    {
        Bstr value;
        if (k < cIds)
            value = arrayVMIds[k];
        RTPrintf("\t%ls = %ls\n", arrayVMNames[k], value.raw());
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/**
 * List all available cloud images for the specified cloud provider.
 *
 * @returns RTEXITCODE
 * @param a is the list of passed arguments
 * @param iFirst is the position of the first unparsed argument in the arguments list
 * @param pCommonOpts is a pointer to the structure CLOUDCOMMONOPT with some common
 * arguments which have been already parsed before
 */
static RTEXITCODE listCloudImages(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--compartment-id", 'c', RTGETOPT_REQ_STRING },
        { "--state",          's', RTGETOPT_REQ_STRING },
        { "help",             'h', RTGETOPT_REQ_NOTHING },
        { "--help",           'h', RTGETOPT_REQ_NOTHING }
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    Utf8Str strCompartmentId;
    com::SafeArray<CloudImageState_T> imageStates;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'c':
                strCompartmentId = ValueUnion.psz;
                break;

            case 's':
            {
                const char * const pszState = ValueUnion.psz;

                if (RTStrICmp(pszState, "available") == 0)
                    imageStates.push_back(CloudImageState_Available);
                else if (RTStrICmp(pszState, "deleted") == 0)
                    imageStates.push_back(CloudImageState_Deleted);
                else if (RTStrICmp(pszState, "disabled") == 0)
                    imageStates.push_back(CloudImageState_Disabled);
                else if (RTStrICmp(pszState, "exporting") == 0)
                    imageStates.push_back(CloudImageState_Exporting);
                else if (RTStrICmp(pszState, "importing") == 0)
                    imageStates.push_back(CloudImageState_Importing);
                else if (RTStrICmp(pszState, "provisioning") == 0)
                    imageStates.push_back(CloudImageState_Provisioning);
                else
                    return errorArgument(Cloud::tr("Unknown cloud image state \"%s\""), pszState);
                break;
            }
            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }


    HRESULT hrc = S_OK;

    /* Delayed check. It allows us to print help information.*/
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;

    ComPtr<ICloudProviderManager> pCloudProviderManager;
    CHECK_ERROR2_RET(hrc, pVirtualBox,
                     COMGETTER(CloudProviderManager)(pCloudProviderManager.asOutParam()),
                     RTEXITCODE_FAILURE);

    ComPtr<ICloudProvider> pCloudProvider;
    CHECK_ERROR2_RET(hrc, pCloudProviderManager,
                     GetProviderByShortName(Bstr(pCommonOpts->provider.pszProviderName).raw(), pCloudProvider.asOutParam()),
                     RTEXITCODE_FAILURE);

    ComPtr<ICloudProfile> pCloudProfile;
    CHECK_ERROR2_RET(hrc, pCloudProvider,
                     GetProfileByName(Bstr(pCommonOpts->profile.pszProfileName).raw(), pCloudProfile.asOutParam()),
                     RTEXITCODE_FAILURE);

    if (strCompartmentId.isNotEmpty())
    {
        CHECK_ERROR2_RET(hrc, pCloudProfile,
                         SetProperty(Bstr("compartment").raw(), Bstr(strCompartmentId).raw()),
                         RTEXITCODE_FAILURE);
    }
    else
    {
        RTPrintf(Cloud::tr("Parameter \'compartment\' is empty or absent.\n"
                           "Trying to get the compartment from the passed cloud profile \'%s\'\n"),
                 pCommonOpts->profile.pszProfileName);
        Bstr bStrCompartmentId;
        CHECK_ERROR2_RET(hrc, pCloudProfile,
                         GetProperty(Bstr("compartment").raw(), bStrCompartmentId.asOutParam()),
                         RTEXITCODE_FAILURE);
        strCompartmentId = bStrCompartmentId;
        if (strCompartmentId.isNotEmpty())
            RTPrintf(Cloud::tr("Found the compartment \'%s\':\n"), strCompartmentId.c_str());
        else
            return errorSyntax(Cloud::tr("Parameter --compartment-id is required"));
    }

    Bstr bstrProfileName;
    pCloudProfile->COMGETTER(Name)(bstrProfileName.asOutParam());

    ComObjPtr<ICloudClient> oCloudClient;
    CHECK_ERROR2_RET(hrc, pCloudProfile,
                     CreateCloudClient(oCloudClient.asOutParam()),
                     RTEXITCODE_FAILURE);

    ComPtr<IStringArray> pVMNamesHolder;
    ComPtr<IStringArray> pVMIdsHolder;
    com::SafeArray<BSTR> arrayVMNames;
    com::SafeArray<BSTR> arrayVMIds;
    ComPtr<IProgress> pProgress;

    RTPrintf(Cloud::tr("Reply is in the form \'image name\' = \'image id\'\n"));
    CHECK_ERROR2_RET(hrc, oCloudClient,
                     ListImages(ComSafeArrayAsInParam(imageStates),
                                pVMNamesHolder.asOutParam(),
                                pVMIdsHolder.asOutParam(),
                                pProgress.asOutParam()),
                     RTEXITCODE_FAILURE);
    showProgress(pProgress);
    CHECK_PROGRESS_ERROR_RET(pProgress, (Cloud::tr("Failed to list images")), RTEXITCODE_FAILURE);

    CHECK_ERROR2_RET(hrc,
        pVMNamesHolder, COMGETTER(Values)(ComSafeArrayAsOutParam(arrayVMNames)),
            RTEXITCODE_FAILURE);
    CHECK_ERROR2_RET(hrc,
        pVMIdsHolder, COMGETTER(Values)(ComSafeArrayAsOutParam(arrayVMIds)),
            RTEXITCODE_FAILURE);

    RTPrintf(Cloud::tr("The list of the images for the cloud profile \'%ls\'\nand compartment \'%s\':\n"),
             bstrProfileName.raw(), strCompartmentId.c_str());
    size_t cNames = arrayVMNames.size();
    size_t cIds = arrayVMIds.size();
    for (size_t k = 0; k < cNames; k++)
    {
        Bstr value;
        if (k < cIds)
            value = arrayVMIds[k];
        RTPrintf("\t%ls = %ls\n", arrayVMNames[k], value.raw());
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/**
 * List all available cloud vnic attachments for the specified cloud provider.
 *
 * @returns RTEXITCODE
 * @param a is the list of passed arguments
 * @param iFirst is the position of the first unparsed argument in the arguments list
 * @param pCommonOpts is a pointer to the structure CLOUDCOMMONOPT with some common
 * arguments which have been already parsed before
 */
static RTEXITCODE listCloudVnicAttachments(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--compartment-id", 'c', RTGETOPT_REQ_STRING },
        { "--filter",         'f', RTGETOPT_REQ_STRING },/*instanceId=<id>, vnicId=<id>, domainName=<name>*/
        { "help",             'h', RTGETOPT_REQ_NOTHING },
        { "--help",           'h', RTGETOPT_REQ_NOTHING }
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    com::SafeArray<BSTR> parameters;
    Utf8Str strCompartmentId;
    Utf8Str filterList;
    HRESULT hrc = S_OK;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'c':
                strCompartmentId = ValueUnion.psz;
                Bstr(Utf8Str("compartmentId=").append(ValueUnion.psz)).detachTo(parameters.appendedRaw());
                break;

            case 'f':
                filterList.append(ValueUnion.psz).append(",");
                Bstr(Utf8Str(ValueUnion.psz)).detachTo(parameters.appendedRaw());
                break;
            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    RTPrintf(Cloud::tr("Filters: \'%s\' \n"), filterList.c_str());

    /* Delayed check. It allows us to print help information.*/
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;

    ComPtr<ICloudProviderManager> pCloudProviderManager;
    CHECK_ERROR2_RET(hrc, pVirtualBox,
                     COMGETTER(CloudProviderManager)(pCloudProviderManager.asOutParam()),
                     RTEXITCODE_FAILURE);

    ComPtr<ICloudProvider> pCloudProvider;
    CHECK_ERROR2_RET(hrc, pCloudProviderManager,
                     GetProviderByShortName(Bstr(pCommonOpts->provider.pszProviderName).raw(), pCloudProvider.asOutParam()),
                     RTEXITCODE_FAILURE);

    ComPtr<ICloudProfile> pCloudProfile;
    CHECK_ERROR2_RET(hrc, pCloudProvider,
                     GetProfileByName(Bstr(pCommonOpts->profile.pszProfileName).raw(), pCloudProfile.asOutParam()),
                     RTEXITCODE_FAILURE);

    if (strCompartmentId.isNotEmpty())
    {
        CHECK_ERROR2_RET(hrc, pCloudProfile,
                         SetProperty(Bstr("compartment").raw(), Bstr(strCompartmentId).raw()),
                         RTEXITCODE_FAILURE);
    }
    else
    {
        RTPrintf(Cloud::tr("Parameter \'compartment\' is empty or absent.\n"
                 "Trying to get the compartment from the passed cloud profile \'%s\'\n"), pCommonOpts->profile.pszProfileName);
        Bstr bStrCompartmentId;
        CHECK_ERROR2_RET(hrc, pCloudProfile,
                         GetProperty(Bstr("compartment").raw(), bStrCompartmentId.asOutParam()),
                         RTEXITCODE_FAILURE);
        strCompartmentId = bStrCompartmentId;
        if (strCompartmentId.isNotEmpty())
            RTPrintf(Cloud::tr("Found the compartment \'%s\':\n"), strCompartmentId.c_str());
        else
            return errorArgument(Cloud::tr("Parameter --compartment-id is required."));
    }

    Bstr bstrProfileName;
    pCloudProfile->COMGETTER(Name)(bstrProfileName.asOutParam());

    ComObjPtr<ICloudClient> oCloudClient;
    CHECK_ERROR2_RET(hrc, pCloudProfile,
                     CreateCloudClient(oCloudClient.asOutParam()),
                     RTEXITCODE_FAILURE);

    ComPtr<IStringArray> pVnicAttachmentIdsHolder;
    ComPtr<IStringArray> pVnicIdsHolder;
    com::SafeArray<BSTR> arrayVnicAttachmentIds;
    com::SafeArray<BSTR> arrayVnicIds;
    ComPtr<IProgress> pProgress;

    RTPrintf(Cloud::tr("Reply is in the form \'Vnic attachment <id>\': \n\t \'Vnic <id>\'\n"));
    CHECK_ERROR2_RET(hrc, oCloudClient,
                     ListVnicAttachments(ComSafeArrayAsInParam(parameters),
                                         pVnicAttachmentIdsHolder.asOutParam(),
                                         pVnicIdsHolder.asOutParam(),
                                         pProgress.asOutParam()),
                     RTEXITCODE_FAILURE);
    showProgress(pProgress);
    CHECK_PROGRESS_ERROR_RET(pProgress, (Cloud::tr("Failed to list Vnic attachments")), RTEXITCODE_FAILURE);

    CHECK_ERROR2_RET(hrc,
        pVnicAttachmentIdsHolder, COMGETTER(Values)(ComSafeArrayAsOutParam(arrayVnicAttachmentIds)),
            RTEXITCODE_FAILURE);
    CHECK_ERROR2_RET(hrc,
        pVnicIdsHolder, COMGETTER(Values)(ComSafeArrayAsOutParam(arrayVnicIds)),
            RTEXITCODE_FAILURE);

    RTPrintf(Cloud::tr("The list of the Vnic attachments:\n"));
    size_t cVnicAttchIds = arrayVnicAttachmentIds.size();
    size_t cVnicIds = arrayVnicIds.size();

    if (cVnicAttchIds == 0)
        RTPrintf(Cloud::tr("\tEmpty\n"));
    else
    {
        Bstr value;
        for (size_t k = 0; k < cVnicAttchIds; k++)
        {
            if (k < cVnicIds)
                value = arrayVnicIds[k];
            RTPrintf(Cloud::tr("Vnic attachment id [%ls]:\n\t Vnic id - %ls\n"), arrayVnicAttachmentIds[k], value.raw());
        }
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


/**
 * General function which handles the "list" commands
 *
 * @returns RTEXITCODE
 * @param a is the list of passed arguments
 * @param iFirst is the position of the first unparsed argument in the arguments list
 * @param pCommonOpts is a pointer to the structure CLOUDCOMMONOPT with some common
 * arguments which have been already parsed before
 */
static RTEXITCODE handleCloudLists(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    enum
    {
        kCloudListIota = 1000,
        kCloudList_Images,
        kCloudList_Instances,
        kCloudList_Machines,
        kCloudList_Networks,
        kCloudList_Objects,
        kCloudList_Subnets,
        kCloudList_Vcns,
        kCloudList_VnicAttachments,
    };

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "images",              kCloudList_Images,    RTGETOPT_REQ_NOTHING },
        { "instances",           kCloudList_Instances, RTGETOPT_REQ_NOTHING },
        { "machines",            kCloudList_Machines,  RTGETOPT_REQ_NOTHING },
        { "networks",            kCloudList_Networks,  RTGETOPT_REQ_NOTHING },
        { "objects",             kCloudList_Objects,   RTGETOPT_REQ_NOTHING },
        { "subnets",             kCloudList_Subnets,   RTGETOPT_REQ_NOTHING },
        { "vcns",                kCloudList_Vcns,      RTGETOPT_REQ_NOTHING },
        { "vms",                 kCloudList_Machines,  RTGETOPT_REQ_NOTHING },
        { "vnicattachments",     kCloudList_VnicAttachments, RTGETOPT_REQ_NOTHING },

        { "help",                'h',                  RTGETOPT_REQ_NOTHING },
        { "-?",                  'h',                  RTGETOPT_REQ_NOTHING },
        { "-help",               'h',                  RTGETOPT_REQ_NOTHING },
        { "--help",              'h',                  RTGETOPT_REQ_NOTHING },
    };

    if (a->argc == iFirst)
    {
        RTPrintf(Cloud::tr("Empty command parameter list, show help.\n"));
        printHelp(g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case kCloudList_Images:
                setCurrentSubcommand(HELP_SCOPE_CLOUD_LIST_IMAGES);
                return listCloudImages(a, GetState.iNext, pCommonOpts);

            case kCloudList_Instances:
                setCurrentSubcommand(HELP_SCOPE_CLOUD_LIST_INSTANCES);
                return listCloudInstances(a, GetState.iNext, pCommonOpts);
            case kCloudList_Machines:
                return listCloudMachines(a, GetState.iNext,
                                         pCommonOpts->provider.pszProviderName,
                                         pCommonOpts->profile.pszProfileName);

            case kCloudList_VnicAttachments:
                setCurrentSubcommand(HELP_SCOPE_CLOUD_LIST_VNICATTACHMENTS);
                return listCloudVnicAttachments(a, GetState.iNext, pCommonOpts);

            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;

            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    return errorNoSubcommand();
}


static RTEXITCODE createCloudInstance(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    enum
    {
        kInstanceIota = 1000,
        kInstance_ShapeCpu,
        kInstance_ShapeMemory,
    };

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--image-id",       'i', RTGETOPT_REQ_STRING },
        { "--boot-volume-id", 'v', RTGETOPT_REQ_STRING },
        { "--display-name",   'n', RTGETOPT_REQ_STRING },
        { "--launch-mode",    'm', RTGETOPT_REQ_STRING },
        { "--shape",          's', RTGETOPT_REQ_STRING },
        { "--shape-cpus",     kInstance_ShapeCpu,       RTGETOPT_REQ_UINT32 },
        { "--shape-memory",   kInstance_ShapeMemory,    RTGETOPT_REQ_UINT32 },
        { "--domain-name",    'd', RTGETOPT_REQ_STRING },
        { "--boot-disk-size", 'b', RTGETOPT_REQ_STRING },
        { "--publicip",       'p', RTGETOPT_REQ_STRING },
        { "--subnet",         't', RTGETOPT_REQ_STRING },
        { "--privateip",      'P', RTGETOPT_REQ_STRING },
        { "--launch",         'l', RTGETOPT_REQ_STRING },
        { "--public-ssh-key", 'k', RTGETOPT_REQ_STRING },
        { "--cloud-init-script-path", 'c', RTGETOPT_REQ_STRING },
        { "help",             'h', RTGETOPT_REQ_NOTHING },
        { "--help",           'h', RTGETOPT_REQ_NOTHING }
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    if (a->argc == iFirst)
    {
        RTPrintf(Cloud::tr("Empty command parameter list, show help.\n"));
        printHelp(g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    ComPtr<IAppliance> pAppliance;
    CHECK_ERROR2_RET(hrc, a->virtualBox, CreateAppliance(pAppliance.asOutParam()), RTEXITCODE_FAILURE);
    ULONG vsdNum = 1;
    CHECK_ERROR2_RET(hrc, pAppliance, CreateVirtualSystemDescriptions(1, &vsdNum), RTEXITCODE_FAILURE);
    com::SafeIfaceArray<IVirtualSystemDescription> virtualSystemDescriptions;
    CHECK_ERROR2_RET(hrc, pAppliance,
                     COMGETTER(VirtualSystemDescriptions)(ComSafeArrayAsOutParam(virtualSystemDescriptions)),
                     RTEXITCODE_FAILURE);
    ComPtr<IVirtualSystemDescription> pVSD = virtualSystemDescriptions[0];

    Utf8Str strDisplayName, strImageId, strBootVolumeId, strPublicSSHKey;
    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'i':
                strImageId = ValueUnion.psz;
                pVSD->AddDescription(VirtualSystemDescriptionType_CloudImageId,
                                     Bstr(ValueUnion.psz).raw(), NULL);
                break;

            case 'v':
                strBootVolumeId = ValueUnion.psz;
                pVSD->AddDescription(VirtualSystemDescriptionType_CloudBootVolumeId,
                                     Bstr(ValueUnion.psz).raw(), NULL);
                break;
            case 'n':
                strDisplayName = ValueUnion.psz;
                pVSD->AddDescription(VirtualSystemDescriptionType_Name,
                                     Bstr(ValueUnion.psz).raw(), NULL);
                break;
            case 'm':
                pVSD->AddDescription(VirtualSystemDescriptionType_CloudOCILaunchMode,
                                     Bstr(ValueUnion.psz).raw(), NULL);
                break;

            case 's':
                pVSD->AddDescription(VirtualSystemDescriptionType_CloudInstanceShape,
                                     Bstr(ValueUnion.psz).raw(), NULL);
                break;

            case kInstance_ShapeCpu:
                pVSD->AddDescription(VirtualSystemDescriptionType_CloudShapeCpus,
                                     BstrFmt("%RI32", ValueUnion.u32).raw(), NULL);
                break;

            case kInstance_ShapeMemory:
                pVSD->AddDescription(VirtualSystemDescriptionType_CloudShapeMemory,
                                     BstrFmt("%RI32", ValueUnion.u32).raw(), NULL);
                break;

            case 'd':
                pVSD->AddDescription(VirtualSystemDescriptionType_CloudDomain,
                                     Bstr(ValueUnion.psz).raw(), NULL);
                break;
            case 'b':
                pVSD->AddDescription(VirtualSystemDescriptionType_CloudBootDiskSize,
                                     Bstr(ValueUnion.psz).raw(), NULL);
                break;
            case 'p':
                pVSD->AddDescription(VirtualSystemDescriptionType_CloudPublicIP,
                                     Bstr(ValueUnion.psz).raw(), NULL);
                break;
            case 'P':
                pVSD->AddDescription(VirtualSystemDescriptionType_CloudPrivateIP,
                                     Bstr(ValueUnion.psz).raw(), NULL);
                break;
            case 't':
                pVSD->AddDescription(VirtualSystemDescriptionType_CloudOCISubnet,
                                     Bstr(ValueUnion.psz).raw(), NULL);
                break;
            case 'l':
                {
                    Utf8Str strLaunch(ValueUnion.psz);
                    if (strLaunch.isNotEmpty() && (strLaunch.equalsIgnoreCase("true") || strLaunch.equalsIgnoreCase("false")))
                        pVSD->AddDescription(VirtualSystemDescriptionType_CloudLaunchInstance,
                                             Bstr(ValueUnion.psz).raw(), NULL);
                    break;
                }
            case 'k':
                strPublicSSHKey = ValueUnion.psz;
                pVSD->AddDescription(VirtualSystemDescriptionType_CloudPublicSSHKey,
                                     Bstr(ValueUnion.psz).raw(), NULL);
                break;
            case 'c':
                pVSD->AddDescription(VirtualSystemDescriptionType_CloudInitScriptPath,
                                     Bstr(ValueUnion.psz).raw(), NULL);
                break;
            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* Delayed check. It allows us to print help information.*/
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    if (strPublicSSHKey.isEmpty())
        RTPrintf(Cloud::tr("Warning!!! Public SSH key doesn't present in the passed arguments...\n"));

    if (strImageId.isNotEmpty() && strBootVolumeId.isNotEmpty())
        return errorArgument(Cloud::tr("Parameters --image-id and --boot-volume-id are mutually exclusive. "
                                       "Only one of them must be presented."));

    if (strImageId.isEmpty() && strBootVolumeId.isEmpty())
        return errorArgument(Cloud::tr("Missing parameter --image-id or --boot-volume-id. One of them must be presented."));

    ComPtr<ICloudProfile> pCloudProfile = pCommonOpts->profile.pCloudProfile;

    pVSD->AddDescription(VirtualSystemDescriptionType_CloudProfileName,
                         Bstr(pCommonOpts->profile.pszProfileName).raw(),
                         NULL);

    ComObjPtr<ICloudClient> oCloudClient;
    CHECK_ERROR2_RET(hrc, pCloudProfile,
                     CreateCloudClient(oCloudClient.asOutParam()),
                     RTEXITCODE_FAILURE);

    ComPtr<IStringArray> infoArray;
    com::SafeArray<BSTR> pStrInfoArray;
    ComPtr<IProgress> pProgress;

#if 0
        /*
         * OCI API returns an error during an instance creation if the image isn't available
         * or in the inappropriate state. So the check can be omitted.
         */
        RTPrintf(Cloud::tr("Checking the cloud image with id \'%s\'...\n"), strImageId.c_str());
        CHECK_ERROR2_RET(hrc, oCloudClient,
                         GetImageInfo(Bstr(strImageId).raw(),
                                      infoArray.asOutParam(),
                                      pProgress.asOutParam()),
                         RTEXITCODE_FAILURE);

        hrc = showProgress(pProgress);
        CHECK_PROGRESS_ERROR_RET(pProgress, (Cloud::tr("Checking the cloud image failed")), RTEXITCODE_FAILURE);

        pProgress.setNull();
#endif

    if (strImageId.isNotEmpty())
        RTPrintf(Cloud::tr("Creating cloud instance with name \'%s\' from the image \'%s\'...\n"),
                 strDisplayName.c_str(), strImageId.c_str());
    else
        RTPrintf(Cloud::tr("Creating cloud instance with name \'%s\' from the boot volume \'%s\'...\n"),
                 strDisplayName.c_str(), strBootVolumeId.c_str());

    CHECK_ERROR2_RET(hrc, oCloudClient, LaunchVM(pVSD, pProgress.asOutParam()), RTEXITCODE_FAILURE);

    hrc = showProgress(pProgress);
    CHECK_PROGRESS_ERROR_RET(pProgress, (Cloud::tr("Creating cloud instance failed")), RTEXITCODE_FAILURE);

    if (SUCCEEDED(hrc))
        RTPrintf(Cloud::tr("Cloud instance was created successfully\n"));

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


static RTEXITCODE cloneCloudInstance(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    enum
    {
        kInstanceIota = 1000,
        kInstance_ShapeCpu,
        kInstance_ShapeMemory,
    };

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--id",           'i', RTGETOPT_REQ_STRING },
        { "--clone-name",   'n', RTGETOPT_REQ_STRING },

        { "help",           'h', RTGETOPT_REQ_NOTHING },
        { "--help",         'h', RTGETOPT_REQ_NOTHING }
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    if (a->argc == iFirst)
    {
        RTPrintf(Cloud::tr("Empty command parameter list, show help.\n"));
        printHelp(g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    Utf8Str strCloneName, strInstanceId;
    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'i':
                strInstanceId = ValueUnion.psz;

                break;
            case 'n':
                strCloneName = ValueUnion.psz;
                break;
            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* Delayed check. It allows us to print help information.*/
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    if (strInstanceId.isEmpty())
        return errorArgument(Cloud::tr("Missing parameter --id."));

//  if (strCloneName.isEmpty())
//      return errorArgument(Cloud::tr("Missing parameter --clone-name."));

    ComPtr<ICloudProfile> pCloudProfile = pCommonOpts->profile.pCloudProfile;

    ComObjPtr<ICloudClient> oCloudClient;
    CHECK_ERROR2_RET(hrc, pCloudProfile,
                     CreateCloudClient(oCloudClient.asOutParam()),
                     RTEXITCODE_FAILURE);

    ComPtr<IProgress> pProgress;
    ComPtr<ICloudMachine> pClonedMachine;

    RTPrintf(Cloud::tr("Cloning cloud instance with Id \'%s\'...\n"), strInstanceId.c_str());

    CHECK_ERROR2_RET(hrc, oCloudClient, CloneInstance(Bstr(strInstanceId).raw(),
                                                      Bstr(strCloneName).raw(),
                                                      pClonedMachine.asOutParam(),
                                                      pProgress.asOutParam()), RTEXITCODE_FAILURE);

    hrc = showProgress(pProgress);
    CHECK_PROGRESS_ERROR_RET(pProgress, (Cloud::tr("Cloning cloud instance failed")), RTEXITCODE_FAILURE);

    Bstr strAttr;
    pClonedMachine->COMGETTER(Id)(strAttr.asOutParam());
    RTPrintf(Cloud::tr("Cloned instance UUID in VirtualBox: %ls\n"), strAttr.raw());
    strAttr.setNull();
    pClonedMachine->COMGETTER(Name)(strAttr.asOutParam());
    RTPrintf(Cloud::tr("Cloned instance name: %ls\n"), strAttr.raw());

    if (SUCCEEDED(hrc))
        RTPrintf(Cloud::tr("Cloud instance was cloned successfully\n"));

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static RTEXITCODE updateCloudInstance(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    RT_NOREF(a);
    RT_NOREF(iFirst);
    RT_NOREF(pCommonOpts);
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE showCloudInstanceInfo(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--id",   'i', RTGETOPT_REQ_STRING },
        { "help",   'h', RTGETOPT_REQ_NOTHING },
        { "--help", 'h', RTGETOPT_REQ_NOTHING }
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    if (a->argc == iFirst)
    {
        RTPrintf(Cloud::tr("Empty command parameter list, show help.\n"));
        printHelp(g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    Utf8Str strInstanceId;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'i':
            {
                if (strInstanceId.isNotEmpty())
                    return errorArgument(Cloud::tr("Duplicate parameter: --id"));

                strInstanceId = ValueUnion.psz;
                if (strInstanceId.isEmpty())
                    return errorArgument(Cloud::tr("Empty parameter: --id"));

                break;
            }
            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* Delayed check. It allows us to print help information.*/
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    if (strInstanceId.isEmpty())
        return errorArgument(Cloud::tr("Missing parameter: --id"));

    ComPtr<ICloudProfile> pCloudProfile = pCommonOpts->profile.pCloudProfile;

    ComObjPtr<ICloudClient> oCloudClient;
    CHECK_ERROR2_RET(hrc, pCloudProfile,
                     CreateCloudClient(oCloudClient.asOutParam()),
                     RTEXITCODE_FAILURE);
    RTPrintf(Cloud::tr("Getting information about cloud instance with id %s...\n"), strInstanceId.c_str());
    RTPrintf(Cloud::tr("Reply is in the form \'setting name\' = \'value\'\n"));

    ComPtr<IAppliance> pAppliance;
    CHECK_ERROR2_RET(hrc, a->virtualBox, CreateAppliance(pAppliance.asOutParam()), RTEXITCODE_FAILURE);

    com::SafeIfaceArray<IVirtualSystemDescription> vsdArray;
    ULONG requestedVSDnums = 1;
    ULONG newVSDnums = 0;
    CHECK_ERROR2_RET(hrc, pAppliance, CreateVirtualSystemDescriptions(requestedVSDnums, &newVSDnums), RTEXITCODE_FAILURE);
    if (requestedVSDnums != newVSDnums)
        return RTEXITCODE_FAILURE;

    CHECK_ERROR2_RET(hrc, pAppliance, COMGETTER(VirtualSystemDescriptions)(ComSafeArrayAsOutParam(vsdArray)), RTEXITCODE_FAILURE);
    ComPtr<IVirtualSystemDescription> instanceDescription = vsdArray[0];

    ComPtr<IProgress> progress;
    CHECK_ERROR2_RET(hrc, oCloudClient,
                     GetInstanceInfo(Bstr(strInstanceId).raw(), instanceDescription, progress.asOutParam()),
                     RTEXITCODE_FAILURE);

    hrc = showProgress(progress);
    CHECK_PROGRESS_ERROR_RET(progress, (Cloud::tr("Getting information about cloud instance failed")), RTEXITCODE_FAILURE);

    RTPrintf(Cloud::tr("Cloud instance info (provider '%s'):\n"),
             pCommonOpts->provider.pszProviderName);

    struct vsdHReadable {
        VirtualSystemDescriptionType_T vsdType;
        Utf8Str strFound;
        Utf8Str strNotFound;
    };

    const size_t vsdHReadableArraySize = 15;//the number of items in the vsdHReadableArray
    vsdHReadable vsdHReadableArray[vsdHReadableArraySize] = {
        {VirtualSystemDescriptionType_CloudDomain, Cloud::tr("Availability domain = %ls\n"), Cloud::tr("Availability domain wasn't found\n")},
        {VirtualSystemDescriptionType_Name, Cloud::tr("Instance displayed name = %ls\n"), Cloud::tr("Instance displayed name wasn't found\n")},
        {VirtualSystemDescriptionType_CloudInstanceState, Cloud::tr("Instance state = %ls\n"), Cloud::tr("Instance state wasn't found\n")},
        {VirtualSystemDescriptionType_CloudInstanceId, Cloud::tr("Instance Id = %ls\n"), Cloud::tr("Instance Id wasn't found\n")},
        {VirtualSystemDescriptionType_CloudInstanceDisplayName, Cloud::tr("Instance name = %ls\n"), Cloud::tr("Instance name wasn't found\n")},
        {VirtualSystemDescriptionType_CloudImageId, Cloud::tr("Bootable image Id = %ls\n"),
            Cloud::tr("Image Id whom the instance is booted up wasn't found\n")},
        {VirtualSystemDescriptionType_CloudInstanceShape, Cloud::tr("Shape of the instance = %ls\n"),
            Cloud::tr("The shape of the instance wasn't found\n")},
        {VirtualSystemDescriptionType_OS, Cloud::tr("Type of guest OS = %ls\n"), Cloud::tr("Type of guest OS wasn't found\n")},
        {VirtualSystemDescriptionType_Memory, Cloud::tr("RAM = %ls MB\n"), Cloud::tr("Value for RAM wasn't found\n")},
        {VirtualSystemDescriptionType_CPU, Cloud::tr("CPUs = %ls\n"), Cloud::tr("Numbers of CPUs weren't found\n")},
        {VirtualSystemDescriptionType_CloudPublicIP, Cloud::tr("Instance public IP = %ls\n"), Cloud::tr("Public IP wasn't found\n")},
        {VirtualSystemDescriptionType_Miscellaneous, "%ls\n", Cloud::tr("Miscellanious wasn't found\n")},
        {VirtualSystemDescriptionType_CloudInstanceFreeFormTags, "%ls\n", Cloud::tr("Free-form tags weren't found\n")},
        {VirtualSystemDescriptionType_CloudInstanceMetadata, "%ls\n", Cloud::tr("Metadata was't found\n")},
        {VirtualSystemDescriptionType_CloudInitScriptPath, "Cloud-init script: \n\t%ls\n", Cloud::tr("Cloud-init script wasn't found\n")}
    };

    com::SafeArray<VirtualSystemDescriptionType_T> retTypes;
    com::SafeArray<BSTR> aRefs;
    com::SafeArray<BSTR> aOvfValues;
    com::SafeArray<BSTR> aVBoxValues;
    com::SafeArray<BSTR> aExtraConfigValues;

    for (size_t i=0; i<vsdHReadableArraySize ; ++i)
    {
        hrc = instanceDescription->GetDescriptionByType(vsdHReadableArray[i].vsdType,
                                                        ComSafeArrayAsOutParam(retTypes),
                                                        ComSafeArrayAsOutParam(aRefs),
                                                        ComSafeArrayAsOutParam(aOvfValues),
                                                        ComSafeArrayAsOutParam(aVBoxValues),
                                                        ComSafeArrayAsOutParam(aExtraConfigValues));
        if (FAILED(hrc) || aVBoxValues.size() == 0)
            LogRel((vsdHReadableArray[i].strNotFound.c_str()));
        else
        {
            LogRel(("Size is %d", aVBoxValues.size()));
            for (size_t j = 0; j<aVBoxValues.size(); ++j)
            {
                RTPrintf(vsdHReadableArray[i].strFound.c_str(), aVBoxValues[j]);
            }
        }

        retTypes.setNull();
        aRefs.setNull();
        aOvfValues.setNull();
        aVBoxValues.setNull();
        aExtraConfigValues.setNull();
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


static RTEXITCODE cloudInstanceMetricList(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--id",           'i', RTGETOPT_REQ_STRING },
        { "help",           'h', RTGETOPT_REQ_NOTHING },
        { "--help",         'h', RTGETOPT_REQ_NOTHING }
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    if (a->argc == iFirst)
    {
        RTPrintf(Cloud::tr("Empty command parameter list, show help.\n"));
        printHelp(g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    Utf8Str strInstanceId;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'i':
            {
                if (strInstanceId.isNotEmpty())
                    return errorArgument(Cloud::tr("Duplicate parameter: --id"));

                strInstanceId = ValueUnion.psz;
                if (strInstanceId.isEmpty())
                    return errorArgument(Cloud::tr("Empty parameter: --id"));

                break;
            }
            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* Delayed check. It allows us to print help information.*/
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    if (strInstanceId.isEmpty())
        return errorArgument(Cloud::tr("Missing parameter: --id"));

    ComPtr<ICloudProfile> pCloudProfile = pCommonOpts->profile.pCloudProfile;

    ComObjPtr<ICloudClient> oCloudClient;
    CHECK_ERROR2_RET(hrc, pCloudProfile,
                     CreateCloudClient(oCloudClient.asOutParam()),
                     RTEXITCODE_FAILURE);
    RTPrintf(Cloud::tr("Getting list of metric names for the cloud instance with id %s\n"), strInstanceId.c_str());
    RTPrintf(Cloud::tr("Reply is the list, each metric name is placed on a separate line as \' - <metric name>\'\n"));

    ComPtr<IProgress> progress;

    CHECK_ERROR2_RET(hrc, oCloudClient, ReadCloudMachineList(progress.asOutParam()), RTEXITCODE_FAILURE);

    /* First step*/
    RTPrintf(Cloud::tr("First, reading the cloud machines list...\n"));
    hrc = showProgress(progress);
    CHECK_PROGRESS_ERROR_RET(progress, (Cloud::tr("Reading the cloud machines list failed")), RTEXITCODE_FAILURE);

    com::SafeIfaceArray<ICloudMachine> aMachines;
    CHECK_ERROR2_RET(hrc, oCloudClient,
                     COMGETTER(CloudMachineList)(ComSafeArrayAsOutParam(aMachines)),
                     RTEXITCODE_FAILURE);

    const size_t cMachines = aMachines.size();
    if (cMachines == 0)
        return RTEXITCODE_SUCCESS;

    std::vector<ComPtr<ICloudMachine> > vMachines(cMachines);
    std::vector<com::Bstr> vBstrCloudIds(cMachines);
    std::vector<com::Bstr> vBstrIds(cMachines);

    com::Bstr bstrFoundMachineVBoxId;

    for (size_t i = 0; i < cMachines; ++i)
    {
        vMachines[i] = aMachines[i];

        CHECK_ERROR2_RET(hrc, vMachines[i],
                         COMGETTER(CloudId)(vBstrCloudIds[i].asOutParam()),
                         RTEXITCODE_FAILURE);

        Utf8Str strCurrMachineCloudId(vBstrCloudIds[i]);
        if (strCurrMachineCloudId.equals(strInstanceId))
        {
            CHECK_ERROR2_RET(hrc, vMachines[i],
                             COMGETTER(Id)(vBstrIds[i].asOutParam()),
                             RTEXITCODE_FAILURE);
            bstrFoundMachineVBoxId = vBstrIds[i];
            break;
        }
    }

    if (bstrFoundMachineVBoxId.isEmpty())
    {
        RTPrintf("The passed cloud instance Id \'%s\' WASN'T found among the cloud machines "
                 "of user \'%s\' registered with VirtualBox\n",
                 strInstanceId.c_str(), pCommonOpts->profile.pszProfileName);
        return RTEXITCODE_FAILURE;
    }

    /* Second step*/
    RTPrintf(Cloud::tr("Second, refresh information about cloud instance ...\n"));
    ComPtr<ICloudMachine> pMachine;
    CHECK_ERROR2_RET(hrc, oCloudClient,
        GetCloudMachine(bstrFoundMachineVBoxId.raw(), pMachine.asOutParam()), RTEXITCODE_FAILURE);

    ComPtr<IProgress> pRefreshProgress;
    CHECK_ERROR2_RET(hrc, pMachine, Refresh(pRefreshProgress.asOutParam()), RTEXITCODE_FAILURE);

    hrc = showProgress(pRefreshProgress);
    CHECK_PROGRESS_ERROR_RET(pRefreshProgress, (Cloud::tr("Refreshing information about cloud instance failed")), RTEXITCODE_FAILURE);

    /* Third step*/
    RTPrintf(Cloud::tr("Third, getting information about cloud instance metric names...\n"));
    ComPtr<IProgress> progress2;
    ComPtr<IStringArray> returnMetricNames;
    CHECK_ERROR2_RET(hrc, pMachine, ListMetricNames(returnMetricNames.asOutParam(), progress2.asOutParam()),  RTEXITCODE_FAILURE);

    hrc = showProgress(progress2);
    CHECK_PROGRESS_ERROR_RET(progress, (Cloud::tr("Getting information about cloud instance metrics failed")), RTEXITCODE_FAILURE);

    com::SafeArray<BSTR> metricNamesArray;
    CHECK_ERROR2_RET(hrc,
                     returnMetricNames, COMGETTER(Values)(ComSafeArrayAsOutParam(metricNamesArray)),
                     RTEXITCODE_FAILURE);

    RTPrintf(Cloud::tr("Available metric names:\n"));
    size_t cMetricNamesArraySize = metricNamesArray.size();

    if (cMetricNamesArraySize == 0)
    {
        RTPrintf(Cloud::tr("\tThe list of metric names is empty. It may mean that an appropriate service wasn't run on the instance.\n"));
        return RTEXITCODE_FAILURE;
    }
    else
    {
        Bstr value;
        for (size_t k = 0; k < cMetricNamesArraySize; ++k)
        {
            value = metricNamesArray[k];
            MetricType_T aMetricType;
            CHECK_ERROR2_RET(hrc, oCloudClient, GetMetricTypeByName(value.raw(), &aMetricType), RTEXITCODE_FAILURE);

            if (SUCCEEDED(hrc))
                RTPrintf(Cloud::tr(" - %ls\n"), value.raw());
        }
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


static RTEXITCODE cloudInstanceMetricData(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--id",           'i', RTGETOPT_REQ_STRING },
        { "--metric-name",  'n', RTGETOPT_REQ_STRING },
        { "--metric-points",'p', RTGETOPT_REQ_STRING },
        { "help",           'h', RTGETOPT_REQ_NOTHING },
        { "--help",         'h', RTGETOPT_REQ_NOTHING }
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    if (a->argc == iFirst)
    {
        RTPrintf(Cloud::tr("Empty command parameter list, show help.\n"));
        printHelp(g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    Utf8Str strInstanceId;
    Utf8Str strMetricName;
    Utf8Str strMetricPoints;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'i':
            {
                if (strInstanceId.isNotEmpty())
                    return errorArgument(Cloud::tr("Duplicate parameter: --id"));

                strInstanceId = ValueUnion.psz;
                if (strInstanceId.isEmpty())
                    return errorArgument(Cloud::tr("Empty parameter: --id"));

                break;
            }
            case 'n':
            {
                if (strMetricName.isNotEmpty())
                    return errorArgument(Cloud::tr("Duplicate parameter: --metric-name"));

                strMetricName = ValueUnion.psz;
                if (strMetricName.isEmpty())
                    return errorArgument(Cloud::tr("Empty parameter: --metric-name"));

                break;
            }
            case 'p':
            {
                if (strMetricPoints.isNotEmpty())
                    return errorArgument(Cloud::tr("Duplicate parameter: --metric-points"));

                strMetricPoints = ValueUnion.psz;
                if (strMetricPoints.isEmpty())
                    return errorArgument(Cloud::tr("Empty parameter: --metric-points"));

                break;
            }
            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* Delayed check. It allows us to print help information.*/
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    if (strInstanceId.isEmpty())
        return errorArgument(Cloud::tr("Missing parameter: --id"));
    if (strMetricName.isEmpty())
        return errorArgument(Cloud::tr("Missing parameter: --metric-name"));

    unsigned long metricPoins;
    strMetricPoints.isEmpty() ? metricPoins = 1 : metricPoins = RTStrToUInt32(strMetricPoints.c_str());

    ComPtr<ICloudProfile> pCloudProfile = pCommonOpts->profile.pCloudProfile;

    ComObjPtr<ICloudClient> oCloudClient;
    CHECK_ERROR2_RET(hrc, pCloudProfile,
                     CreateCloudClient(oCloudClient.asOutParam()),
                     RTEXITCODE_FAILURE);
    RTPrintf(Cloud::tr("Getting %s metric for the cloud instance with id %s...\n"), strMetricName.c_str(), strInstanceId.c_str());
    RTPrintf(Cloud::tr("Reply is in the form \'%s[Rfc2822 time format]\' = \'value\'\n"), strMetricName.c_str());

    /* Check the requested metric type */
    MetricType_T aMetricType;
    CHECK_ERROR2_RET(hrc, oCloudClient, GetMetricTypeByName(com::Bstr(strMetricName.c_str()).raw(), &aMetricType), RTEXITCODE_FAILURE);

    /* First step*/
    RTPrintf(Cloud::tr("First, reading the cloud machines list...\n"));
    ComPtr<IProgress> progress;
    CHECK_ERROR2_RET(hrc, oCloudClient, ReadCloudMachineList(progress.asOutParam()), RTEXITCODE_FAILURE);

    hrc = showProgress(progress);
    CHECK_PROGRESS_ERROR_RET(progress, (Cloud::tr("Reading the cloud machines list failed")), RTEXITCODE_FAILURE);

    com::SafeIfaceArray<ICloudMachine> aMachines;
    CHECK_ERROR2_RET(hrc, oCloudClient,
                     COMGETTER(CloudMachineList)(ComSafeArrayAsOutParam(aMachines)),
                     RTEXITCODE_FAILURE);

    const size_t cMachines = aMachines.size();
    if (cMachines == 0)
        return RTEXITCODE_SUCCESS;

    std::vector<ComPtr<ICloudMachine> > vMachines(cMachines);
    std::vector<com::Bstr> vBstrCloudIds(cMachines);
    std::vector<com::Bstr> vBstrIds(cMachines);

    com::Bstr bstrFoundMachineVBoxId;

    for (size_t i = 0; i < cMachines; ++i)
    {
        vMachines[i] = aMachines[i];

        CHECK_ERROR2_RET(hrc, vMachines[i],
                         COMGETTER(CloudId)(vBstrCloudIds[i].asOutParam()),
                         RTEXITCODE_FAILURE);

        Utf8Str strCurrMachineCloudId(vBstrCloudIds[i]);
        if (strCurrMachineCloudId.equals(strInstanceId))
        {
            CHECK_ERROR2_RET(hrc, vMachines[i],
                             COMGETTER(Id)(vBstrIds[i].asOutParam()),
                             RTEXITCODE_FAILURE);
            bstrFoundMachineVBoxId = vBstrIds[i];
            break;
        }
    }

    if (bstrFoundMachineVBoxId.isEmpty())
    {
        RTPrintf("The passed cloud instance Id \'%s\' WASN'T found among the cloud machines "
                 "of user \'%s\' registered with VirtualBox\n",
                 strInstanceId.c_str(), pCommonOpts->profile.pszProfileName);
        return RTEXITCODE_FAILURE;
    }

    /* Second step*/
    RTPrintf(Cloud::tr("Second, refresh information about cloud instance ...\n"));
    ComPtr<ICloudMachine> pMachine;
    CHECK_ERROR2_RET(hrc, oCloudClient,
        GetCloudMachine(bstrFoundMachineVBoxId.raw(), pMachine.asOutParam()), RTEXITCODE_FAILURE);

    ComPtr<IProgress> pRefreshProgress;
    CHECK_ERROR2_RET(hrc, pMachine, Refresh(pRefreshProgress.asOutParam()), RTEXITCODE_FAILURE);

    hrc = showProgress(pRefreshProgress);
    CHECK_PROGRESS_ERROR_RET(pRefreshProgress, (Cloud::tr("Refreshing information about cloud instance failed")), RTEXITCODE_FAILURE);

    /* Third step*/
    RTPrintf(Cloud::tr("Third, request metric data from cloud instance ...\n"));
    ComPtr<IProgress> progress2;
    ComPtr<IStringArray> returnDataValues;
    ComPtr<IStringArray> returnDataTimestamps;
    ComPtr<IStringArray> returnMeasureUnits;

    CHECK_ERROR2_RET(hrc, pMachine, EnumerateMetricData(aMetricType,
                                                        metricPoins,
                                                        returnDataValues.asOutParam(),
                                                        returnDataTimestamps.asOutParam(),
                                                        returnMeasureUnits.asOutParam(),
                                                        progress2.asOutParam()),  RTEXITCODE_FAILURE);

    hrc = showProgress(progress2);
    CHECK_PROGRESS_ERROR_RET(progress2, (Cloud::tr("Getting metric data failed")), RTEXITCODE_FAILURE);

    com::SafeArray<BSTR> dataValueArray;
    CHECK_ERROR2_RET(hrc,
                     returnDataValues, COMGETTER(Values)(ComSafeArrayAsOutParam(dataValueArray)),
                     RTEXITCODE_FAILURE);

    com::SafeArray<BSTR> dataTimestampArray;
    CHECK_ERROR2_RET(hrc,
                     returnDataTimestamps, COMGETTER(Values)(ComSafeArrayAsOutParam(dataTimestampArray)),
                     RTEXITCODE_FAILURE);

    com::SafeArray<BSTR> measureUnitArray;
    CHECK_ERROR2_RET(hrc,
                     returnMeasureUnits, COMGETTER(Values)(ComSafeArrayAsOutParam(measureUnitArray)),
                     RTEXITCODE_FAILURE);

    size_t cDataValueArraySize = dataValueArray.size();
    Bstr unit = cDataValueArraySize == 0 ? Bstr("unknown units") : measureUnitArray[0];
    RTPrintf(Cloud::tr("The %s metric values (in %ls):\n"), strMetricName.c_str(), unit.raw());

    if (cDataValueArraySize == 0)
        RTPrintf(Cloud::tr("\tThe list of metric data is empty. It may mean that an appropriate service wasn't run on the instance.\n"));
    else
    {
        Bstr value;
        for (size_t k = 0; k < cDataValueArraySize; ++k)
        {
            value = dataValueArray[k];
            Utf8Str strTimestamp(dataTimestampArray[k]);
            RTPrintf(Cloud::tr("%d: %s[%s] = %ls\n"), k, strMetricName.c_str(), strTimestamp.c_str(), value.raw());
        }
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


static RTEXITCODE startCloudInstance(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--id",   'i', RTGETOPT_REQ_STRING },
        { "help",   'h', RTGETOPT_REQ_NOTHING },
        { "--help", 'h', RTGETOPT_REQ_NOTHING }
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    if (a->argc == iFirst)
    {
        RTPrintf(Cloud::tr("Empty command parameter list, show help.\n"));
        printHelp(g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    Utf8Str strInstanceId;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'i':
            {
                if (strInstanceId.isNotEmpty())
                    return errorArgument(Cloud::tr("Duplicate parameter: --id"));

                strInstanceId = ValueUnion.psz;
                if (strInstanceId.isEmpty())
                    return errorArgument(Cloud::tr("Empty parameter: --id"));

                break;
            }
            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* Delayed check. It allows us to print help information.*/
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    if (strInstanceId.isEmpty())
        return errorArgument(Cloud::tr("Missing parameter: --id"));

    ComPtr<ICloudProfile> pCloudProfile = pCommonOpts->profile.pCloudProfile;

    ComObjPtr<ICloudClient> oCloudClient;
    CHECK_ERROR2_RET(hrc, pCloudProfile,
                     CreateCloudClient(oCloudClient.asOutParam()),
                     RTEXITCODE_FAILURE);
    RTPrintf(Cloud::tr("Starting cloud instance with id %s...\n"), strInstanceId.c_str());

    ComPtr<IProgress> progress;
    CHECK_ERROR2_RET(hrc, oCloudClient,
                     StartInstance(Bstr(strInstanceId).raw(), progress.asOutParam()),
                     RTEXITCODE_FAILURE);
    hrc = showProgress(progress);
    CHECK_PROGRESS_ERROR_RET(progress, (Cloud::tr("Starting the cloud instance failed")), RTEXITCODE_FAILURE);

    if (SUCCEEDED(hrc))
        RTPrintf(Cloud::tr("Cloud instance with id %s (provider = '%s', profile = '%s') was started\n"),
                 strInstanceId.c_str(),
                 pCommonOpts->provider.pszProviderName,
                 pCommonOpts->profile.pszProfileName);

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static RTEXITCODE pauseCloudInstance(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--id",   'i', RTGETOPT_REQ_STRING },
        { "help",   'h', RTGETOPT_REQ_NOTHING },
        { "--help", 'h', RTGETOPT_REQ_NOTHING }
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    if (a->argc == iFirst)
    {
        RTPrintf(Cloud::tr("Empty command parameter list, show help.\n"));
        printHelp(g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    Utf8Str strInstanceId;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'i':
            {
                if (strInstanceId.isNotEmpty())
                    return errorArgument(Cloud::tr("Duplicate parameter: --id"));

                strInstanceId = ValueUnion.psz;
                if (strInstanceId.isEmpty())
                    return errorArgument(Cloud::tr("Empty parameter: --id"));

                break;
            }
            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* Delayed check. It allows us to print help information.*/
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    if (strInstanceId.isEmpty())
        return errorArgument(Cloud::tr("Missing parameter: --id"));

    ComPtr<ICloudProfile> pCloudProfile = pCommonOpts->profile.pCloudProfile;

    ComObjPtr<ICloudClient> oCloudClient;
    CHECK_ERROR2_RET(hrc, pCloudProfile,
                     CreateCloudClient(oCloudClient.asOutParam()),
                     RTEXITCODE_FAILURE);
    RTPrintf(Cloud::tr("Pausing cloud instance with id %s...\n"), strInstanceId.c_str());

    ComPtr<IProgress> progress;
    CHECK_ERROR2_RET(hrc, oCloudClient,
                     PauseInstance(Bstr(strInstanceId).raw(), progress.asOutParam()),
                     RTEXITCODE_FAILURE);
    hrc = showProgress(progress);
    CHECK_PROGRESS_ERROR_RET(progress, (Cloud::tr("Pause the cloud instance failed")), RTEXITCODE_FAILURE);

    if (SUCCEEDED(hrc))
        RTPrintf(Cloud::tr("Cloud instance with id %s (provider = '%s', profile = '%s') was paused\n"),
                 strInstanceId.c_str(),
                 pCommonOpts->provider.pszProviderName,
                 pCommonOpts->profile.pszProfileName);

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static RTEXITCODE terminateCloudInstance(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--id",   'i', RTGETOPT_REQ_STRING },
        { "help",   'h', RTGETOPT_REQ_NOTHING },
        { "--help", 'h', RTGETOPT_REQ_NOTHING }
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    if (a->argc == iFirst)
    {
        RTPrintf(Cloud::tr("Empty command parameter list, show help.\n"));
        printHelp(g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    Utf8Str strInstanceId;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'i':
            {
                if (strInstanceId.isNotEmpty())
                    return errorArgument(Cloud::tr("Duplicate parameter: --id"));

                strInstanceId = ValueUnion.psz;
                if (strInstanceId.isEmpty())
                    return errorArgument(Cloud::tr("Empty parameter: --id"));

                break;
            }
            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* Delayed check. It allows us to print help information.*/
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    if (strInstanceId.isEmpty())
        return errorArgument(Cloud::tr("Missing parameter: --id"));


    ComPtr<ICloudProfile> pCloudProfile = pCommonOpts->profile.pCloudProfile;

    ComObjPtr<ICloudClient> oCloudClient;
    CHECK_ERROR2_RET(hrc, pCloudProfile,
                     CreateCloudClient(oCloudClient.asOutParam()),
                     RTEXITCODE_FAILURE);
    RTPrintf(Cloud::tr("Terminating cloud instance with id %s...\n"), strInstanceId.c_str());

    ComPtr<IProgress> progress;
    CHECK_ERROR2_RET(hrc, oCloudClient,
                     TerminateInstance(Bstr(strInstanceId).raw(), progress.asOutParam()),
                     RTEXITCODE_FAILURE);
    hrc = showProgress(progress);
    CHECK_PROGRESS_ERROR_RET(progress, (Cloud::tr("Termination the cloud instance failed")), RTEXITCODE_FAILURE);

    if (SUCCEEDED(hrc))
        RTPrintf(Cloud::tr("Cloud instance with id %s (provider = '%s', profile = '%s') was terminated\n"),
                 strInstanceId.c_str(),
                 pCommonOpts->provider.pszProviderName,
                 pCommonOpts->profile.pszProfileName);

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static RTEXITCODE resetCloudInstance(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--id",   'i', RTGETOPT_REQ_STRING },
        { "help",   'h', RTGETOPT_REQ_NOTHING },
        { "--help", 'h', RTGETOPT_REQ_NOTHING }
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    if (a->argc == iFirst)
    {
        RTPrintf(Cloud::tr("Empty command parameter list, show help.\n"));
        printHelp(g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    Utf8Str strInstanceId;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'i':
            {
                if (strInstanceId.isNotEmpty())
                    return errorArgument(Cloud::tr("Duplicate parameter: --id"));

                strInstanceId = ValueUnion.psz;
                if (strInstanceId.isEmpty())
                    return errorArgument(Cloud::tr("Empty parameter: --id"));

                break;
            }
            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* Delayed check. It allows us to print help information.*/
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    if (strInstanceId.isEmpty())
        return errorArgument(Cloud::tr("Missing parameter: --id"));

    ComPtr<ICloudProfile> pCloudProfile = pCommonOpts->profile.pCloudProfile;

    ComObjPtr<ICloudClient> oCloudClient;
    CHECK_ERROR2_RET(hrc, pCloudProfile,
                     CreateCloudClient(oCloudClient.asOutParam()),
                     RTEXITCODE_FAILURE);
    RTPrintf(Cloud::tr("Reset cloud instance with id %s...\n"), strInstanceId.c_str());

    ComPtr<IProgress> progress;
    CHECK_ERROR2_RET(hrc, oCloudClient,
                     ResetInstance(Bstr(strInstanceId).raw(), progress.asOutParam()),
                     RTEXITCODE_FAILURE);
    hrc = showProgress(progress);
    CHECK_PROGRESS_ERROR_RET(progress, (Cloud::tr("Reset the cloud instance failed")), RTEXITCODE_FAILURE);

    if (SUCCEEDED(hrc))
        RTPrintf(Cloud::tr("Cloud instance with id %s (provider = '%s', profile = '%s') was reset\n"),
                 strInstanceId.c_str(),
                 pCommonOpts->provider.pszProviderName,
                 pCommonOpts->profile.pszProfileName);

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static RTEXITCODE handleCloudInstance(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    enum
    {
        kCloudInstanceIota = 1000,
        kCloudInstance_Create,
        kCloudInstance_Info,
        kCloudInstance_Pause,
        kCloudInstance_Start,
        kCloudInstance_Terminate,
        kCloudInstance_Update,
        kCloudInstance_Reset,
        kCloudInstance_Clone,
        kCloudInstance_MetricList,
        kCloudInstance_MetricData,
    };

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "create",         kCloudInstance_Create,    RTGETOPT_REQ_NOTHING },
        { "info",           kCloudInstance_Info,      RTGETOPT_REQ_NOTHING },
        { "pause",          kCloudInstance_Pause,     RTGETOPT_REQ_NOTHING },
        { "start",          kCloudInstance_Start,     RTGETOPT_REQ_NOTHING },
        { "terminate",      kCloudInstance_Terminate, RTGETOPT_REQ_NOTHING },
        { "update",         kCloudInstance_Update,    RTGETOPT_REQ_NOTHING },
        { "reset",          kCloudInstance_Reset,     RTGETOPT_REQ_NOTHING },
        { "clone",          kCloudInstance_Clone,     RTGETOPT_REQ_NOTHING },
        { "metriclist",     kCloudInstance_MetricList,RTGETOPT_REQ_NOTHING },
        { "metricdata",     kCloudInstance_MetricData,RTGETOPT_REQ_NOTHING },

        { "help",           'h',                      RTGETOPT_REQ_NOTHING },
        { "-?",             'h',                      RTGETOPT_REQ_NOTHING },
        { "-help",          'h',                      RTGETOPT_REQ_NOTHING },
        { "--help",         'h',                      RTGETOPT_REQ_NOTHING },
    };

    if (a->argc == iFirst)
    {
        RTPrintf(Cloud::tr("Empty command parameter list, show help.\n"));
        printHelp(g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            /* Sub-commands: */
            case kCloudInstance_Create:
                setCurrentSubcommand(HELP_SCOPE_CLOUD_INSTANCE_CREATE);
                return createCloudInstance(a, GetState.iNext, pCommonOpts);

            case kCloudInstance_Start:
                setCurrentSubcommand(HELP_SCOPE_CLOUD_INSTANCE_START);
                return startCloudInstance(a, GetState.iNext, pCommonOpts);

            case kCloudInstance_Pause:
                setCurrentSubcommand(HELP_SCOPE_CLOUD_INSTANCE_PAUSE);
                return pauseCloudInstance(a, GetState.iNext, pCommonOpts);

            case kCloudInstance_Info:
                setCurrentSubcommand(HELP_SCOPE_CLOUD_INSTANCE_INFO);
                return showCloudInstanceInfo(a, GetState.iNext, pCommonOpts);

            case kCloudInstance_Update:
//              setCurrentSubcommand(HELP_SCOPE_CLOUD_INSTANCE_UPDATE);
                return updateCloudInstance(a, GetState.iNext, pCommonOpts);

            case kCloudInstance_Terminate:
                setCurrentSubcommand(HELP_SCOPE_CLOUD_INSTANCE_TERMINATE);
                return terminateCloudInstance(a, GetState.iNext, pCommonOpts);

            case kCloudInstance_Reset:
                setCurrentSubcommand(HELP_SCOPE_CLOUD_INSTANCE_RESET);
                return resetCloudInstance(a, GetState.iNext, pCommonOpts);

            case kCloudInstance_Clone:
                setCurrentSubcommand(HELP_SCOPE_CLOUD_INSTANCE_CLONE);
                return cloneCloudInstance(a, GetState.iNext, pCommonOpts);

            case kCloudInstance_MetricData:
                setCurrentSubcommand(HELP_SCOPE_CLOUD_INSTANCE_METRICDATA);
                return cloudInstanceMetricData(a, GetState.iNext, pCommonOpts);

            case kCloudInstance_MetricList:
                setCurrentSubcommand(HELP_SCOPE_CLOUD_INSTANCE_METRICLIST);
                return cloudInstanceMetricList(a, GetState.iNext, pCommonOpts);

            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;

            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    return errorNoSubcommand();
}


static RTEXITCODE createCloudImage(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--object-name",    'o', RTGETOPT_REQ_STRING },
        { "--bucket-name",    'b', RTGETOPT_REQ_STRING },
        { "--compartment-id", 'c', RTGETOPT_REQ_STRING },
        { "--instance-id",    'i', RTGETOPT_REQ_STRING },
        { "--display-name",   'd', RTGETOPT_REQ_STRING },
        { "--launch-mode",    'm', RTGETOPT_REQ_STRING },
        { "help",             'h', RTGETOPT_REQ_NOTHING },
        { "--help",           'h', RTGETOPT_REQ_NOTHING }
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    if (a->argc == iFirst)
    {
        RTPrintf(Cloud::tr("Empty command parameter list, show help.\n"));
        printHelp(g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    Utf8Str strCompartmentId;
    Utf8Str strInstanceId;
    Utf8Str strDisplayName;
    Utf8Str strBucketName;
    Utf8Str strObjectName;
    com::SafeArray<BSTR>  parameters;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'c':
                strCompartmentId=ValueUnion.psz;
                Bstr(Utf8Str("compartment-id=").append(ValueUnion.psz)).detachTo(parameters.appendedRaw());
                break;
            case 'i':
                strInstanceId=ValueUnion.psz;
                Bstr(Utf8Str("instance-id=").append(ValueUnion.psz)).detachTo(parameters.appendedRaw());
                break;
            case 'd':
                strDisplayName=ValueUnion.psz;
                Bstr(Utf8Str("display-name=").append(ValueUnion.psz)).detachTo(parameters.appendedRaw());
                break;
            case 'o':
                strObjectName=ValueUnion.psz;
                Bstr(Utf8Str("object-name=").append(ValueUnion.psz)).detachTo(parameters.appendedRaw());
                break;
            case 'b':
                strBucketName=ValueUnion.psz;
                Bstr(Utf8Str("bucket-name=").append(ValueUnion.psz)).detachTo(parameters.appendedRaw());
                break;
            case 'm':
                strBucketName=ValueUnion.psz;
                Bstr(Utf8Str("launch-mode=").append(ValueUnion.psz)).detachTo(parameters.appendedRaw());
                break;
            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* Delayed check. It allows us to print help information.*/
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    if (strInstanceId.isNotEmpty() && strObjectName.isNotEmpty())
        return errorArgument(Cloud::tr("Conflicting parameters: --instance-id and --object-name can't be used together. Choose one."));

    ComPtr<ICloudProfile> pCloudProfile = pCommonOpts->profile.pCloudProfile;

    ComObjPtr<ICloudClient> oCloudClient;
    CHECK_ERROR2_RET(hrc, pCloudProfile,
                     CreateCloudClient(oCloudClient.asOutParam()),
                     RTEXITCODE_FAILURE);
    if (strInstanceId.isNotEmpty())
        RTPrintf(Cloud::tr("Creating cloud image with name \'%s\' from the instance \'%s\'...\n"),
                 strDisplayName.c_str(), strInstanceId.c_str());
    else
        RTPrintf(Cloud::tr("Creating cloud image with name \'%s\' from the object \'%s\' in the bucket \'%s\'...\n"),
                 strDisplayName.c_str(), strObjectName.c_str(), strBucketName.c_str());

    ComPtr<IProgress> progress;
    CHECK_ERROR2_RET(hrc, oCloudClient,
                     CreateImage(ComSafeArrayAsInParam(parameters), progress.asOutParam()),
                     RTEXITCODE_FAILURE);
    hrc = showProgress(progress);
    CHECK_PROGRESS_ERROR_RET(progress, (Cloud::tr("Creating cloud image failed")), RTEXITCODE_FAILURE);

    if (SUCCEEDED(hrc))
        RTPrintf(Cloud::tr("Cloud image was created successfully\n"));

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


static RTEXITCODE exportCloudImage(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--id",             'i', RTGETOPT_REQ_STRING },
        { "--bucket-name",    'b', RTGETOPT_REQ_STRING },
        { "--object-name",    'o', RTGETOPT_REQ_STRING },
        { "--display-name",   'd', RTGETOPT_REQ_STRING },
        { "--launch-mode",    'm', RTGETOPT_REQ_STRING },
        { "help",             'h', RTGETOPT_REQ_NOTHING },
        { "--help",           'h', RTGETOPT_REQ_NOTHING }
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    if (a->argc == iFirst)
    {
        RTPrintf(Cloud::tr("Empty command parameter list, show help.\n"));
        printHelp(g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    Utf8Str strImageId;   /* XXX: this is vbox "image", i.e. medium */
    Utf8Str strBucketName;
    Utf8Str strObjectName;
    Utf8Str strDisplayName;
    Utf8Str strLaunchMode;
    com::SafeArray<BSTR> parameters;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'b': /* --bucket-name */
            {
                if (strBucketName.isNotEmpty())
                    return errorArgument(Cloud::tr("Duplicate parameter: --bucket-name"));

                strBucketName = ValueUnion.psz;
                if (strBucketName.isEmpty())
                    return errorArgument(Cloud::tr("Empty parameter: --bucket-name"));

                break;
            }

            case 'o': /* --object-name */
            {
                if (strObjectName.isNotEmpty())
                    return errorArgument(Cloud::tr("Duplicate parameter: --object-name"));

                strObjectName = ValueUnion.psz;
                if (strObjectName.isEmpty())
                    return errorArgument(Cloud::tr("Empty parameter: --object-name"));

                break;
            }

            case 'i': /* --id */
            {
                if (strImageId.isNotEmpty())
                    return errorArgument(Cloud::tr("Duplicate parameter: --id"));

                strImageId = ValueUnion.psz;
                if (strImageId.isEmpty())
                    return errorArgument(Cloud::tr("Empty parameter: --id"));

                break;
            }

            case 'd': /* --display-name */
            {
                if (strDisplayName.isNotEmpty())
                    return errorArgument(Cloud::tr("Duplicate parameter: --display-name"));

                strDisplayName = ValueUnion.psz;
                if (strDisplayName.isEmpty())
                    return errorArgument(Cloud::tr("Empty parameter: --display-name"));

                break;
            }

            case 'm': /* --launch-mode */
            {
                if (strLaunchMode.isNotEmpty())
                    return errorArgument(Cloud::tr("Duplicate parameter: --launch-mode"));

                strLaunchMode = ValueUnion.psz;
                if (strLaunchMode.isEmpty())
                    return errorArgument(Cloud::tr("Empty parameter: --launch-mode"));

                break;
            }

            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;

            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* Delayed check. It allows us to print help information.*/
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    if (strImageId.isNotEmpty())
        BstrFmt("image-id=%s", strImageId.c_str()).detachTo(parameters.appendedRaw());
    else
        return errorArgument(Cloud::tr("Missing parameter: --id"));

    if (strBucketName.isNotEmpty())
        BstrFmt("bucket-name=%s", strBucketName.c_str()).detachTo(parameters.appendedRaw());
    else
        return errorArgument(Cloud::tr("Missing parameter: --bucket-name"));

    if (strObjectName.isNotEmpty())
        BstrFmt("object-name=%s", strObjectName.c_str()).detachTo(parameters.appendedRaw());

    if (strDisplayName.isNotEmpty())
        BstrFmt("display-name=%s", strDisplayName.c_str()).detachTo(parameters.appendedRaw());

    if (strLaunchMode.isNotEmpty())
        BstrFmt("launch-mode=%s", strLaunchMode.c_str()).detachTo(parameters.appendedRaw());


    ComPtr<ICloudProfile> pCloudProfile = pCommonOpts->profile.pCloudProfile;

    ComObjPtr<ICloudClient> oCloudClient;
    CHECK_ERROR2_RET(hrc, pCloudProfile,
                     CreateCloudClient(oCloudClient.asOutParam()),
                     RTEXITCODE_FAILURE);

    if (strObjectName.isNotEmpty())
        RTPrintf(Cloud::tr("Exporting image \'%s\' to the Cloud with name \'%s\'...\n"),
                 strImageId.c_str(), strObjectName.c_str());
    else
        RTPrintf(Cloud::tr("Exporting image \'%s\' to the Cloud with default name\n"),
                 strImageId.c_str());

    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;
    SafeIfaceArray<IMedium> aImageList;
    CHECK_ERROR2_RET(hrc, pVirtualBox,
                     COMGETTER(HardDisks)(ComSafeArrayAsOutParam(aImageList)),
                     RTEXITCODE_FAILURE);

    ComPtr<IMedium> pImage;
    size_t cImages = aImageList.size();
    bool fFound = false;
    for (size_t i = 0; i < cImages; ++i)
    {
        pImage = aImageList[i];
        Bstr bstrImageId;
        hrc = pImage->COMGETTER(Id)(bstrImageId.asOutParam());
        if (FAILED(hrc))
            continue;

        com::Guid imageId(bstrImageId);

        if (!imageId.isValid() || imageId.isZero())
            continue;

        if (!strImageId.compare(imageId.toString()))
        {
            fFound = true;
            RTPrintf(Cloud::tr("Image %s was found\n"), strImageId.c_str());
            break;
        }
    }

    if (!fFound)
    {
        RTPrintf(Cloud::tr("Process of exporting the image to the Cloud was interrupted. The image wasn't found.\n"));
        return RTEXITCODE_FAILURE;
    }

    ComPtr<IProgress> progress;
    CHECK_ERROR2_RET(hrc, oCloudClient,
                     ExportImage(pImage, ComSafeArrayAsInParam(parameters), progress.asOutParam()),
                     RTEXITCODE_FAILURE);
    hrc = showProgress(progress);
    CHECK_PROGRESS_ERROR_RET(progress, (Cloud::tr("Export the image to the Cloud failed")), RTEXITCODE_FAILURE);

    if (SUCCEEDED(hrc))
        RTPrintf(Cloud::tr("Export the image to the Cloud was successfull\n"));

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static RTEXITCODE importCloudImage(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--id",             'i', RTGETOPT_REQ_STRING },
        { "--bucket-name",    'b', RTGETOPT_REQ_STRING },
        { "--object-name",    'o', RTGETOPT_REQ_STRING },
        { "help",             'h', RTGETOPT_REQ_NOTHING },
        { "--help",           'h', RTGETOPT_REQ_NOTHING }
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    if (a->argc == iFirst)
    {
        RTPrintf(Cloud::tr("Empty command parameter list, show help.\n"));
        printHelp(g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    Utf8Str strImageId;
    Utf8Str strCompartmentId;
    Utf8Str strBucketName;
    Utf8Str strObjectName;
    Utf8Str strDisplayName;
    com::SafeArray<BSTR>  parameters;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'i':
                strImageId=ValueUnion.psz;
                break;
            case 'b':
                strBucketName=ValueUnion.psz;
                Bstr(Utf8Str("bucket-name=").append(ValueUnion.psz)).detachTo(parameters.appendedRaw());
                break;
            case 'o':
                strObjectName=ValueUnion.psz;
                Bstr(Utf8Str("object-name=").append(ValueUnion.psz)).detachTo(parameters.appendedRaw());
                break;
            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* Delayed check. It allows us to print help information.*/
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    ComPtr<ICloudProfile> pCloudProfile = pCommonOpts->profile.pCloudProfile;

    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;
    ComObjPtr<ICloudClient> oCloudClient;
    CHECK_ERROR2_RET(hrc, pCloudProfile,
                     CreateCloudClient(oCloudClient.asOutParam()),
                     RTEXITCODE_FAILURE);
    RTPrintf(Cloud::tr("Creating an object \'%s\' from the cloud image \'%s\'...\n"), strObjectName.c_str(), strImageId.c_str());

    ComPtr<IProgress> progress;
    CHECK_ERROR2_RET(hrc, oCloudClient,
                     ImportImage(Bstr(strImageId).raw(), ComSafeArrayAsInParam(parameters), progress.asOutParam()),
                     RTEXITCODE_FAILURE);
    hrc = showProgress(progress);
    CHECK_PROGRESS_ERROR_RET(progress, (Cloud::tr("Cloud image import failed")), RTEXITCODE_FAILURE);

    if (SUCCEEDED(hrc))
    {
        RTPrintf(Cloud::tr("Cloud image was imported successfully. Find the downloaded object with the name %s "
                           "in the system temp folder (find the possible environment variables like TEMP, TMP and etc.)\n"),
                 strObjectName.c_str());
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static RTEXITCODE showCloudImageInfo(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--id", 'i', RTGETOPT_REQ_STRING },
        { "help",   'h', RTGETOPT_REQ_NOTHING },
        { "--help", 'h', RTGETOPT_REQ_NOTHING }
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    if (a->argc == iFirst)
    {
        RTPrintf(Cloud::tr("Empty command parameter list, show help.\n"));
        printHelp(g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    Utf8Str strImageId;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'i':
                strImageId = ValueUnion.psz;
                break;
            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* Delayed check. It allows us to print help information.*/
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    ComPtr<ICloudProfile> pCloudProfile = pCommonOpts->profile.pCloudProfile;

    ComObjPtr<ICloudClient> oCloudClient;
    CHECK_ERROR2_RET(hrc, pCloudProfile,
                     CreateCloudClient(oCloudClient.asOutParam()),
                     RTEXITCODE_FAILURE);
    RTPrintf(Cloud::tr("Getting information about the cloud image with id \'%s\'...\n"), strImageId.c_str());

    ComPtr<IStringArray> infoArray;
    com::SafeArray<BSTR> pStrInfoArray;
    ComPtr<IProgress> pProgress;

    RTPrintf(Cloud::tr("Reply is in the form \'image property\' = \'value\'\n"));
    CHECK_ERROR2_RET(hrc, oCloudClient,
                     GetImageInfo(Bstr(strImageId).raw(),
                                  infoArray.asOutParam(),
                                  pProgress.asOutParam()),
                     RTEXITCODE_FAILURE);

    hrc = showProgress(pProgress);
    CHECK_PROGRESS_ERROR_RET(pProgress, (Cloud::tr("Getting information about the cloud image failed")), RTEXITCODE_FAILURE);

    CHECK_ERROR2_RET(hrc,
                     infoArray, COMGETTER(Values)(ComSafeArrayAsOutParam(pStrInfoArray)),
                     RTEXITCODE_FAILURE);

    RTPrintf(Cloud::tr("General information about the image:\n"));
    size_t cParamNames = pStrInfoArray.size();
    for (size_t k = 0; k < cParamNames; k++)
    {
        Utf8Str data(pStrInfoArray[k]);
        RTPrintf("\t%s\n", data.c_str());
    }

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static RTEXITCODE updateCloudImage(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    RT_NOREF(a);
    RT_NOREF(iFirst);
    RT_NOREF(pCommonOpts);
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE deleteCloudImage(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--id", 'i', RTGETOPT_REQ_STRING },
        { "help",   'h', RTGETOPT_REQ_NOTHING },
        { "--help", 'h', RTGETOPT_REQ_NOTHING }
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);
    if (a->argc == iFirst)
    {
        RTPrintf(Cloud::tr("Empty command parameter list, show help.\n"));
        printHelp(g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    Utf8Str strImageId;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'i':
            {
                if (strImageId.isNotEmpty())
                    return errorArgument(Cloud::tr("Duplicate parameter: --id"));

                strImageId = ValueUnion.psz;
                if (strImageId.isEmpty())
                    return errorArgument(Cloud::tr("Empty parameter: --id"));

                break;
            }

            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* Delayed check. It allows us to print help information.*/
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    if (strImageId.isEmpty())
        return errorArgument(Cloud::tr("Missing parameter: --id"));


    ComPtr<ICloudProfile> pCloudProfile = pCommonOpts->profile.pCloudProfile;

    ComObjPtr<ICloudClient> oCloudClient;
    CHECK_ERROR2_RET(hrc, pCloudProfile,
                     CreateCloudClient(oCloudClient.asOutParam()),
                     RTEXITCODE_FAILURE);
    RTPrintf(Cloud::tr("Deleting cloud image with id %s...\n"), strImageId.c_str());

    ComPtr<IProgress> progress;
    CHECK_ERROR2_RET(hrc, oCloudClient,
                     DeleteImage(Bstr(strImageId).raw(), progress.asOutParam()),
                     RTEXITCODE_FAILURE);
    hrc = showProgress(progress);
    CHECK_PROGRESS_ERROR_RET(progress, (Cloud::tr("Deleting cloud image failed")), RTEXITCODE_FAILURE);

    if (SUCCEEDED(hrc))
        RTPrintf(Cloud::tr("Cloud image was deleted successfully\n"));

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}

static RTEXITCODE handleCloudImage(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    enum
    {
        kCloudImageIota = 1000,
        kCloudImage_Create,
        kCloudImage_Delete,
        kCloudImage_Export,
        kCloudImage_Import,
        kCloudImage_Info,
        kCloudImage_Update,
    };

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "create",         kCloudImage_Create,     RTGETOPT_REQ_NOTHING },
        { "delete",         kCloudImage_Delete,     RTGETOPT_REQ_NOTHING },
        { "export",         kCloudImage_Export,     RTGETOPT_REQ_NOTHING },
        { "import",         kCloudImage_Import,     RTGETOPT_REQ_NOTHING },
        { "info",           kCloudImage_Info,       RTGETOPT_REQ_NOTHING },
        { "update",         kCloudImage_Update,     RTGETOPT_REQ_NOTHING },

        { "help",                'h',               RTGETOPT_REQ_NOTHING },
        { "-?",                  'h',               RTGETOPT_REQ_NOTHING },
        { "-help",               'h',               RTGETOPT_REQ_NOTHING },
        { "--help",              'h',               RTGETOPT_REQ_NOTHING },
    };

    if (a->argc == iFirst)
    {
        RTPrintf(Cloud::tr("Empty command parameter list, show help.\n"));
        printHelp(g_pStdOut);
        return RTEXITCODE_SUCCESS;
    }

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            /* Sub-commands: */
            case kCloudImage_Create:
                setCurrentSubcommand(HELP_SCOPE_CLOUD_IMAGE_CREATE);
                return createCloudImage(a, GetState.iNext, pCommonOpts);

            case kCloudImage_Export:
                setCurrentSubcommand(HELP_SCOPE_CLOUD_IMAGE_EXPORT);
                return exportCloudImage(a, GetState.iNext, pCommonOpts);

            case kCloudImage_Import:
                setCurrentSubcommand(HELP_SCOPE_CLOUD_IMAGE_IMPORT);
                return importCloudImage(a, GetState.iNext, pCommonOpts);

            case kCloudImage_Info:
                setCurrentSubcommand(HELP_SCOPE_CLOUD_IMAGE_INFO);
                return showCloudImageInfo(a, GetState.iNext, pCommonOpts);

            case kCloudImage_Update:
//              setCurrentSubcommand(HELP_SCOPE_CLOUD_IMAGE_UPDATE);
                return updateCloudImage(a, GetState.iNext, pCommonOpts);

            case kCloudImage_Delete:
                setCurrentSubcommand(HELP_SCOPE_CLOUD_IMAGE_DELETE);
                return deleteCloudImage(a, GetState.iNext, pCommonOpts);

            case 'h':
                printHelp(g_pStdOut);
                return RTEXITCODE_SUCCESS;

            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    return errorNoSubcommand();
}

#ifdef VBOX_WITH_CLOUD_NET
struct CloudNetworkOptions
{
    BOOL fEnable;
    BOOL fDisable;
    Bstr strNetworkId;
    Bstr strNetworkName;
};
typedef struct CloudNetworkOptions CLOUDNETOPT;
typedef CLOUDNETOPT *PCLOUDNETOPT;

static RTEXITCODE createUpdateCloudNetworkCommon(ComPtr<ICloudNetwork> cloudNetwork, CLOUDNETOPT& options, PCLOUDCOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    Bstr strProvider = pCommonOpts->provider.pszProviderName;
    Bstr strProfile = pCommonOpts->profile.pszProfileName;

    if (options.fEnable)
    {
        CHECK_ERROR2_RET(hrc, cloudNetwork, COMSETTER(Enabled)(TRUE), RTEXITCODE_FAILURE);
    }
    if (options.fDisable)
    {
        CHECK_ERROR2_RET(hrc, cloudNetwork, COMSETTER(Enabled)(FALSE), RTEXITCODE_FAILURE);
    }
    if (options.strNetworkId.isNotEmpty())
    {
        CHECK_ERROR2_RET(hrc, cloudNetwork, COMSETTER(NetworkId)(options.strNetworkId.raw()), RTEXITCODE_FAILURE);
    }
    if (strProvider.isNotEmpty())
    {
        CHECK_ERROR2_RET(hrc, cloudNetwork, COMSETTER(Provider)(strProvider.raw()), RTEXITCODE_FAILURE);
    }
    if (strProfile.isNotEmpty())
    {
        CHECK_ERROR2_RET(hrc, cloudNetwork, COMSETTER(Profile)(strProfile.raw()), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE createCloudNetwork(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    /* Required parameters, the rest is handled in update */
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--disable",        'd', RTGETOPT_REQ_NOTHING },
        { "--enable",         'e', RTGETOPT_REQ_NOTHING },
        { "--network-id",     'i', RTGETOPT_REQ_STRING },
        { "--name",           'n', RTGETOPT_REQ_STRING },
    };

    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    CLOUDNETOPT options;
    options.fEnable = FALSE;
    options.fDisable = FALSE;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'd':
                options.fDisable = TRUE;
                break;
            case 'e':
                options.fEnable = TRUE;
                break;
            case 'i':
                options.strNetworkId=ValueUnion.psz;
                break;
            case 'n':
                options.strNetworkName=ValueUnion.psz;
                break;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    if (options.strNetworkName.isEmpty())
        return errorArgument(Cloud::tr("Missing --name parameter"));
    if (options.strNetworkId.isEmpty())
        return errorArgument(Cloud::tr("Missing --network-id parameter"));

    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;

    ComPtr<ICloudNetwork> cloudNetwork;
    CHECK_ERROR2_RET(hrc, pVirtualBox,
                     CreateCloudNetwork(options.strNetworkName.raw(), cloudNetwork.asOutParam()),
                     RTEXITCODE_FAILURE);

    /* Fill out the created network */
    RTEXITCODE rc = createUpdateCloudNetworkCommon(cloudNetwork, options, pCommonOpts);
    if (RT_SUCCESS(rc))
        RTPrintf(Cloud::tr("Cloud network was created successfully\n"));

    return rc;
}


static RTEXITCODE showCloudNetworkInfo(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    RT_NOREF(pCommonOpts);
    HRESULT hrc = S_OK;
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--name",           'n', RTGETOPT_REQ_STRING },
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    Bstr strNetworkName;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'n':
                strNetworkName=ValueUnion.psz;
                break;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    if (strNetworkName.isEmpty())
        return errorArgument(Cloud::tr("Missing --name parameter"));

    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;
    ComPtr<ICloudNetwork> cloudNetwork;
    CHECK_ERROR2_RET(hrc, pVirtualBox,
                     FindCloudNetworkByName(strNetworkName.raw(), cloudNetwork.asOutParam()),
                     RTEXITCODE_FAILURE);

    RTPrintf(Cloud::tr("Name:            %ls\n"), strNetworkName.raw());
    BOOL fEnabled = FALSE;
    cloudNetwork->COMGETTER(Enabled)(&fEnabled);
    RTPrintf(Cloud::tr("State:           %s\n"), fEnabled ? Cloud::tr("Enabled") : Cloud::tr("Disabled"));
    Bstr Provider;
    cloudNetwork->COMGETTER(Provider)(Provider.asOutParam());
    RTPrintf(Cloud::tr("CloudProvider:   %ls\n"), Provider.raw());
    Bstr Profile;
    cloudNetwork->COMGETTER(Profile)(Profile.asOutParam());
    RTPrintf(Cloud::tr("CloudProfile:    %ls\n"), Profile.raw());
    Bstr NetworkId;
    cloudNetwork->COMGETTER(NetworkId)(NetworkId.asOutParam());
    RTPrintf(Cloud::tr("CloudNetworkId:  %ls\n"), NetworkId.raw());
    Bstr netName = BstrFmt("cloud-%ls", strNetworkName.raw());
    RTPrintf(Cloud::tr("VBoxNetworkName: %ls\n\n"), netName.raw());

    return RTEXITCODE_SUCCESS;
}


static RTEXITCODE updateCloudNetwork(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    HRESULT hrc = S_OK;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--disable",        'd', RTGETOPT_REQ_NOTHING },
        { "--enable",         'e', RTGETOPT_REQ_NOTHING },
        { "--network-id",     'i', RTGETOPT_REQ_STRING },
        { "--name",           'n', RTGETOPT_REQ_STRING },
    };

    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    CLOUDNETOPT options;
    options.fEnable = FALSE;
    options.fDisable = FALSE;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'd':
                options.fDisable = TRUE;
                break;
            case 'e':
                options.fEnable = TRUE;
                break;
            case 'i':
                options.strNetworkId=ValueUnion.psz;
                break;
            case 'n':
                options.strNetworkName=ValueUnion.psz;
                break;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    if (options.strNetworkName.isEmpty())
        return errorArgument(Cloud::tr("Missing --name parameter"));

    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;
    ComPtr<ICloudNetwork> cloudNetwork;
    CHECK_ERROR2_RET(hrc, pVirtualBox,
                     FindCloudNetworkByName(options.strNetworkName.raw(), cloudNetwork.asOutParam()),
                     RTEXITCODE_FAILURE);

    RTEXITCODE rc = createUpdateCloudNetworkCommon(cloudNetwork, options, pCommonOpts);
    if (RT_SUCCESS(rc))
        RTPrintf(Cloud::tr("Cloud network %ls was updated successfully\n"), options.strNetworkName.raw());

    return rc;
}


static RTEXITCODE deleteCloudNetwork(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    RT_NOREF(pCommonOpts);
    HRESULT hrc = S_OK;
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--name",           'n', RTGETOPT_REQ_STRING },
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    Bstr strNetworkName;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'n':
                strNetworkName=ValueUnion.psz;
                break;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    if (strNetworkName.isEmpty())
        return errorArgument(Cloud::tr("Missing --name parameter"));

    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;
    ComPtr<ICloudNetwork> cloudNetwork;
    CHECK_ERROR2_RET(hrc, pVirtualBox,
                     FindCloudNetworkByName(strNetworkName.raw(), cloudNetwork.asOutParam()),
                     RTEXITCODE_FAILURE);

    CHECK_ERROR2_RET(hrc, pVirtualBox,
                     RemoveCloudNetwork(cloudNetwork),
                     RTEXITCODE_FAILURE);

    if (SUCCEEDED(hrc))
        RTPrintf(Cloud::tr("Cloud network %ls was deleted successfully\n"), strNetworkName.raw());

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


static RTEXITCODE setupCloudNetworkEnv(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    RT_NOREF(pCommonOpts);
    HRESULT hrc = S_OK;
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--gateway-os-name",      'n', RTGETOPT_REQ_STRING },
        { "--gateway-os-version",   'v', RTGETOPT_REQ_STRING },
        { "--gateway-shape",        's', RTGETOPT_REQ_STRING },
        { "--tunnel-network-name",  't', RTGETOPT_REQ_STRING },
        { "--tunnel-network-range", 'r', RTGETOPT_REQ_STRING },
        { "--compartment-id",       'c', RTGETOPT_REQ_STRING }
    };
    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    Bstr strGatewayOsName;
    Bstr strGatewayOsVersion;
    Bstr strGatewayShape;
    Bstr strTunnelNetworkName;
    Bstr strTunnelNetworkRange;
    Bstr strCompartmentId;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'n':
                strGatewayOsName=ValueUnion.psz;
                break;
            case 'v':
                strGatewayOsVersion=ValueUnion.psz;
                break;
            case 's':
                strGatewayShape=ValueUnion.psz;
                break;
            case 't':
                strTunnelNetworkName=ValueUnion.psz;
                break;
            case 'r':
                strTunnelNetworkRange=ValueUnion.psz;
                break;
            case 'c':
                strCompartmentId=ValueUnion.psz;
                break;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* Delayed check. It allows us to print help information.*/
    hrc = checkAndSetCommonOptions(a, pCommonOpts);
    if (FAILED(hrc))
        return RTEXITCODE_FAILURE;

    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;

    RTPrintf(Cloud::tr("Setting up tunnel network in the cloud...\n"));

    ComPtr<ICloudProfile> pCloudProfile = pCommonOpts->profile.pCloudProfile;

    /* Use user-specified profile instead of default one. */
    if (strCompartmentId.isNotEmpty())
    {
        CHECK_ERROR2_RET(hrc, pCloudProfile,
                         SetProperty(Bstr("compartment").raw(), Bstr(strCompartmentId).raw()),
                         RTEXITCODE_FAILURE);
    }

    ComObjPtr<ICloudClient> oCloudClient;
    CHECK_ERROR2_RET(hrc, pCloudProfile,
                     CreateCloudClient(oCloudClient.asOutParam()),
                     RTEXITCODE_FAILURE);

    ComPtr<ICloudNetworkEnvironmentInfo> cloudNetworkEnv;
    ComPtr<IProgress> progress;
    CHECK_ERROR2_RET(hrc, oCloudClient,
                     SetupCloudNetworkEnvironment(strTunnelNetworkName.raw(), strTunnelNetworkRange.raw(),
                                                  strGatewayOsName.raw(), strGatewayOsVersion.raw(), strGatewayShape.raw(),
                                                  cloudNetworkEnv.asOutParam(), progress.asOutParam()),
                     RTEXITCODE_FAILURE);

    hrc = showProgress(progress);
    CHECK_PROGRESS_ERROR_RET(progress, (Cloud::tr("Setting up cloud network environment failed")), RTEXITCODE_FAILURE);

    Bstr tunnelNetworkId;
    hrc = cloudNetworkEnv->COMGETTER(TunnelNetworkId)(tunnelNetworkId.asOutParam());
    RTPrintf(Cloud::tr("Cloud network environment was set up successfully. Tunnel network id is: %ls\n"), tunnelNetworkId.raw());

    return SUCCEEDED(hrc) ? RTEXITCODE_SUCCESS : RTEXITCODE_FAILURE;
}


static RTEXITCODE handleCloudNetwork(HandlerArg *a, int iFirst, PCLOUDCOMMONOPT pCommonOpts)
{
    enum
    {
        kCloudNetworkIota = 1000,
        kCloudNetwork_Create,
        kCloudNetwork_Delete,
        kCloudNetwork_Info,
        kCloudNetwork_Setup,
        kCloudNetwork_Update,
    };

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "create",         kCloudNetwork_Create,   RTGETOPT_REQ_NOTHING },
        { "delete",         kCloudNetwork_Delete,   RTGETOPT_REQ_NOTHING },
        { "info",           kCloudNetwork_Info,     RTGETOPT_REQ_NOTHING },
        { "setup",          kCloudNetwork_Setup,    RTGETOPT_REQ_NOTHING },
        { "update",         kCloudNetwork_Update,   RTGETOPT_REQ_NOTHING },
    };

    if (a->argc < 1)
        return errorNoSubcommand();

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), iFirst, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            /* Sub-commands: */
            case kCloudNetwork_Create:
                return createCloudNetwork(a, GetState.iNext, pCommonOpts);

            case kCloudNetwork_Info:
                return showCloudNetworkInfo(a, GetState.iNext, pCommonOpts);

            case kCloudNetwork_Update:
                return updateCloudNetwork(a, GetState.iNext, pCommonOpts);

            case kCloudNetwork_Delete:
                return deleteCloudNetwork(a, GetState.iNext, pCommonOpts);

            case kCloudNetwork_Setup:
                return setupCloudNetworkEnv(a, GetState.iNext, pCommonOpts);

            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    return errorNoSubcommand();
}
#endif /* VBOX_WITH_CLOUD_NET */


RTEXITCODE handleCloud(HandlerArg *a)
{
    enum
    {
        kCloudIota = 1000,
        kCloud_Image,
        kCloud_Instance,
        kCloud_List,
        kCloud_Machine,
        kCloud_Network,
        kCloud_Object,
        kCloud_ShowVMInfo,
        kCloud_Volume,
    };

    static const RTGETOPTDEF s_aOptions[] =
    {
        /* common options */
        { "--provider",     'v',                    RTGETOPT_REQ_STRING },
        { "--profile",      'f',                    RTGETOPT_REQ_STRING },

        { "image",            kCloud_Image,         RTGETOPT_REQ_NOTHING },
        { "instance",         kCloud_Instance,      RTGETOPT_REQ_NOTHING },
        { "list",             kCloud_List,          RTGETOPT_REQ_NOTHING },
        { "machine",          kCloud_Machine,       RTGETOPT_REQ_NOTHING },
        { "network",          kCloud_Network,       RTGETOPT_REQ_NOTHING },
        { "object",           kCloud_Object,        RTGETOPT_REQ_NOTHING },
        { "showvminfo",       kCloud_ShowVMInfo,    RTGETOPT_REQ_NOTHING },
        { "volume",           kCloud_Volume,        RTGETOPT_REQ_NOTHING },
    };

    if (a->argc < 1)
        return errorNoSubcommand();

    RTGETOPTSTATE GetState;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 0, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    CLOUDCOMMONOPT   commonOpts = { {NULL, NULL}, {NULL, NULL} };
    int c;
    RTGETOPTUNION ValueUnion;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'v':   // --provider
                commonOpts.provider.pszProviderName = ValueUnion.psz;
                break;

            case 'f':   // --profile
                commonOpts.profile.pszProfileName = ValueUnion.psz;
                break;

            /* Sub-commands: */
            case kCloud_List:
                return handleCloudLists(a, GetState.iNext, &commonOpts);

            case kCloud_Image:
                return handleCloudImage(a, GetState.iNext, &commonOpts);

            case kCloud_Instance:
                return handleCloudInstance(a, GetState.iNext, &commonOpts);

#ifdef VBOX_WITH_CLOUD_NET
            case kCloud_Network:
                return handleCloudNetwork(a, GetState.iNext, &commonOpts);
#endif /* VBOX_WITH_CLOUD_NET */

            /* "cloud machine ..." handling is in VBoxManageCloudMachine.cpp */
            case kCloud_Machine:
                return handleCloudMachine(a, GetState.iNext,
                                          commonOpts.provider.pszProviderName,
                                          commonOpts.profile.pszProfileName);

            /* ... including aliases that mimic the local vm commands */
            case kCloud_ShowVMInfo:
                return handleCloudShowVMInfo(a, GetState.iNext,
                                             commonOpts.provider.pszProviderName,
                                             commonOpts.profile.pszProfileName);

            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    return errorNoSubcommand();
}
