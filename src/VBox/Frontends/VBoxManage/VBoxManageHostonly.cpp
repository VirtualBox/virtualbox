/* $Id: VBoxManageHostonly.cpp 106320 2024-10-15 12:08:41Z klaus.espenlaub@oracle.com $ */
/** @file
 * VBoxManage - Implementation of hostonlyif command.
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

#include <iprt/cidr.h>
#include <iprt/param.h>
#include <iprt/path.h>
#include <iprt/stream.h>
#include <iprt/string.h>
#include <iprt/net.h>
#include <iprt/getopt.h>
#include <iprt/ctype.h>

#include <VBox/log.h>

#include "VBoxManage.h"

DECLARE_TRANSLATION_CONTEXT(HostOnly);


using namespace com;

static const RTGETOPTDEF g_aHostOnlyCreateOptions[] =
{
    { "--machinereadable",  'M', RTGETOPT_REQ_NOTHING },
};

#if defined(VBOX_WITH_NETFLT) && !defined(RT_OS_SOLARIS)
static RTEXITCODE handleCreate(HandlerArg *a)
{
    /*
     * Parse input.
     */
    bool fMachineReadable = false;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, g_aHostOnlyCreateOptions,
                 RT_ELEMENTS(g_aHostOnlyCreateOptions), 1, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'M':   // --machinereadable
                fMachineReadable = true;
                break;

            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /*
     * Do the work.
     */
    ComPtr<IHost> host;
    CHECK_ERROR2I_RET(a->virtualBox, COMGETTER(Host)(host.asOutParam()), RTEXITCODE_FAILURE);

    ComPtr<IHostNetworkInterface> hif;
    ComPtr<IProgress> progress;

    CHECK_ERROR2I_RET(host, CreateHostOnlyNetworkInterface(hif.asOutParam(), progress.asOutParam()), RTEXITCODE_FAILURE);

    if (fMachineReadable)
    {
        progress->WaitForCompletion(10000); /* Ten seconds should probably be enough. */
        CHECK_PROGRESS_ERROR_RET(progress, (""), RTEXITCODE_FAILURE);
    }
    else
    {
        /*HRESULT hrc =*/ showProgress(progress);
        CHECK_PROGRESS_ERROR_RET(progress, (HostOnly::tr("Failed to create the host-only adapter")), RTEXITCODE_FAILURE);
    }

    Bstr bstrName;
    CHECK_ERROR2I(hif, COMGETTER(Name)(bstrName.asOutParam()));

    if (fMachineReadable)
        RTPrintf("%ls", bstrName.raw());
    else
        RTPrintf(HostOnly::tr("Interface '%ls' was successfully created\n"), bstrName.raw());
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE  handleRemove(HandlerArg *a)
{
    /*
     * Parse input.
     */
    const char *pszName = NULL;
    int ch;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, NULL, 0, 1, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((ch = RTGetOpt(&GetState, &ValueUnion)) != 0)
        switch (ch)
        {
            case VINF_GETOPT_NOT_OPTION:
                if (pszName)
                    return errorSyntax(HostOnly::tr("Only one interface name can be specified"));
                pszName = ValueUnion.psz;
                break;

            default:
                return errorGetOpt(ch, &ValueUnion);
        }
    if (!pszName)
        return errorSyntax(HostOnly::tr("No interface name was specified"));

    /*
     * Do the work.
     */
    ComPtr<IHost> host;
    CHECK_ERROR2I_RET(a->virtualBox, COMGETTER(Host)(host.asOutParam()), RTEXITCODE_FAILURE);

    ComPtr<IHostNetworkInterface> hif;
    CHECK_ERROR2I_RET(host, FindHostNetworkInterfaceByName(Bstr(pszName).raw(), hif.asOutParam()), RTEXITCODE_FAILURE);

    Bstr guid;
    CHECK_ERROR2I_RET(hif, COMGETTER(Id)(guid.asOutParam()), RTEXITCODE_FAILURE);

    ComPtr<IProgress> progress;
    CHECK_ERROR2I_RET(host, RemoveHostOnlyNetworkInterface(guid.raw(), progress.asOutParam()), RTEXITCODE_FAILURE);

    /*HRESULT hrc =*/ showProgress(progress);
    CHECK_PROGRESS_ERROR_RET(progress, (HostOnly::tr("Failed to remove the host-only adapter")), RTEXITCODE_FAILURE);

    return RTEXITCODE_SUCCESS;
}
#endif

static const RTGETOPTDEF g_aHostOnlyIPOptions[]
    = {
        { "--dhcp",             'd', RTGETOPT_REQ_NOTHING },
        { "-dhcp",              'd', RTGETOPT_REQ_NOTHING },    // deprecated
        { "--ip",               'a', RTGETOPT_REQ_STRING },
        { "-ip",                'a', RTGETOPT_REQ_STRING },     // deprecated
        { "--netmask",          'm', RTGETOPT_REQ_STRING },
        { "-netmask",           'm', RTGETOPT_REQ_STRING },     // deprecated
        { "--ipv6",             'b', RTGETOPT_REQ_STRING },
        { "-ipv6",              'b', RTGETOPT_REQ_STRING },     // deprecated
        { "--netmasklengthv6",  'l', RTGETOPT_REQ_UINT8 },
        { "-netmasklengthv6",   'l', RTGETOPT_REQ_UINT8 }       // deprecated
      };

static RTEXITCODE handleIpConfig(HandlerArg *a)
{
    bool        fDhcp = false;
    bool        fNetmasklengthv6 = false;
    uint32_t    uNetmasklengthv6 = UINT32_MAX;
    const char *pszIpv6 = NULL;
    const char *pszIp = NULL;
    const char *pszNetmask = NULL;
    const char *pszName = NULL;

    int c;
    RTGETOPTUNION ValueUnion;
    RTGETOPTSTATE GetState;
    RTGetOptInit(&GetState, a->argc, a->argv, g_aHostOnlyIPOptions, RT_ELEMENTS(g_aHostOnlyIPOptions),
                 1, RTGETOPTINIT_FLAGS_NO_STD_OPTS);
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'd':   // --dhcp
                fDhcp = true;
                break;
            case 'a':   // --ip
                if (pszIp)
                    RTMsgWarning(HostOnly::tr("The --ip option is specified more than once"));
                pszIp = ValueUnion.psz;
                break;
            case 'm':   // --netmask
                if (pszNetmask)
                    RTMsgWarning(HostOnly::tr("The --netmask option is specified more than once"));
                pszNetmask = ValueUnion.psz;
                break;
            case 'b':   // --ipv6
                if (pszIpv6)
                    RTMsgWarning(HostOnly::tr("The --ipv6 option is specified more than once"));
                pszIpv6 = ValueUnion.psz;
                break;
            case 'l':   // --netmasklengthv6
                if (fNetmasklengthv6)
                    RTMsgWarning(HostOnly::tr("The --netmasklengthv6 option is specified more than once"));
                fNetmasklengthv6 = true;
                uNetmasklengthv6 = ValueUnion.u8;
                break;
            case VINF_GETOPT_NOT_OPTION:
                if (pszName)
                    return errorSyntax(HostOnly::tr("Only one interface name can be specified"));
                pszName = ValueUnion.psz;
                break;
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    /* parameter sanity check */
    if (fDhcp && (fNetmasklengthv6 || pszIpv6 || pszIp || pszNetmask))
        return errorSyntax(HostOnly::tr("You can not use --dhcp with static ip configuration parameters: --ip, --netmask, --ipv6 and --netmasklengthv6."));
    if ((pszIp || pszNetmask) && (fNetmasklengthv6 || pszIpv6))
        return errorSyntax(HostOnly::tr("You can not use ipv4 configuration (--ip and --netmask) with ipv6 (--ipv6 and --netmasklengthv6) simultaneously."));

    ComPtr<IHost> host;
    CHECK_ERROR2I_RET(a->virtualBox, COMGETTER(Host)(host.asOutParam()), RTEXITCODE_FAILURE);

    ComPtr<IHostNetworkInterface> hif;
    CHECK_ERROR2I_RET(host, FindHostNetworkInterfaceByName(Bstr(pszName).raw(), hif.asOutParam()), RTEXITCODE_FAILURE);
    if (hif.isNull())
        return errorArgument(HostOnly::tr("Could not find interface '%s'"), pszName);

    if (fDhcp)
        CHECK_ERROR2I_RET(hif, EnableDynamicIPConfig(), RTEXITCODE_FAILURE);
    else if (pszIp)
    {
        if (!pszNetmask)
            pszNetmask = "255.255.255.0"; /* ?? */
        CHECK_ERROR2I_RET(hif, EnableStaticIPConfig(Bstr(pszIp).raw(), Bstr(pszNetmask).raw()), RTEXITCODE_FAILURE);
    }
    else if (pszIpv6)
    {
        BOOL fIpV6Supported;
        CHECK_ERROR2I_RET(hif, COMGETTER(IPV6Supported)(&fIpV6Supported), RTEXITCODE_FAILURE);
        if (!fIpV6Supported)
        {
            RTMsgError(HostOnly::tr("IPv6 setting is not supported for this adapter"));
            return RTEXITCODE_FAILURE;
        }

        if (uNetmasklengthv6 == UINT32_MAX)
            uNetmasklengthv6 = 64; /* ?? */
        CHECK_ERROR2I_RET(hif, EnableStaticIPConfigV6(Bstr(pszIpv6).raw(), (ULONG)uNetmasklengthv6), RTEXITCODE_FAILURE);
    }
    else
        return errorSyntax(HostOnly::tr("Neither -dhcp nor -ip nor -ipv6 was specfified"));

    return RTEXITCODE_SUCCESS;
}


RTEXITCODE handleHostonlyIf(HandlerArg *a)
{
    if (a->argc < 1)
        return errorSyntax(HostOnly::tr("No sub-command specified"));

    RTEXITCODE rcExit;
    if (!strcmp(a->argv[0], "ipconfig"))
    {
        setCurrentSubcommand(HELP_SCOPE_HOSTONLYIF_IPCONFIG);
        rcExit = handleIpConfig(a);
    }
#if defined(VBOX_WITH_NETFLT) && !defined(RT_OS_SOLARIS)
    else if (!strcmp(a->argv[0], "create"))
    {
        setCurrentSubcommand(HELP_SCOPE_HOSTONLYIF_CREATE);
        rcExit = handleCreate(a);
    }
    else if (!strcmp(a->argv[0], "remove"))
    {
        setCurrentSubcommand(HELP_SCOPE_HOSTONLYIF_REMOVE);
        rcExit = handleRemove(a);
    }
#endif
    else
        rcExit = errorSyntax(HostOnly::tr("Unknown sub-command '%s'"), a->argv[0]);
    return rcExit;
}

#ifdef VBOX_WITH_VMNET
struct HostOnlyNetworkOptions
{
    bool fEnable;
    bool fDisable;
    Bstr bstrNetworkId;
    Bstr bstrNetworkName;
    Bstr bstrNetworkMask;
    Bstr bstrLowerIp;
    Bstr bstrUpperIp;
    /* Initialize fEnable and fDisable */
    HostOnlyNetworkOptions() : fEnable(false), fDisable(false) {};
};
typedef struct HostOnlyNetworkOptions HOSTONLYNETOPT;

static RTEXITCODE createUpdateHostOnlyNetworkParse(HandlerArg *a, HOSTONLYNETOPT& options)
{
    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--id",             'i', RTGETOPT_REQ_STRING  },
        { "--name",           'n', RTGETOPT_REQ_STRING  },
        { "--netmask",        'm', RTGETOPT_REQ_STRING  },
        { "--lower-ip",       'l', RTGETOPT_REQ_STRING  },
        { "--lowerip",        'l', RTGETOPT_REQ_STRING  },
        { "--upper-ip",       'u', RTGETOPT_REQ_STRING  },
        { "--upperip",        'u', RTGETOPT_REQ_STRING  },
        { "--enable",         'e', RTGETOPT_REQ_NOTHING },
        { "--disable",        'd', RTGETOPT_REQ_NOTHING },
    };

    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1 /* iFirst */, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'i':
                options.bstrNetworkId = ValueUnion.psz;
                break;
            case 'n':
                options.bstrNetworkName = ValueUnion.psz;
                break;
            case 'm':
                options.bstrNetworkMask = ValueUnion.psz;
                break;
            case 'l':
                options.bstrLowerIp = ValueUnion.psz;
                break;
            case 'u':
                options.bstrUpperIp = ValueUnion.psz;
                break;
            case 'e':
                options.fEnable = true;
                break;
            case 'd':
                options.fDisable = true;
                break;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }
    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE createUpdateHostOnlyNetworkCommon(ComPtr<IHostOnlyNetwork> hostOnlyNetwork, HOSTONLYNETOPT& options)
{
    HRESULT hrc = S_OK;

    if (options.bstrNetworkId.isNotEmpty())
    {
        CHECK_ERROR2_RET(hrc, hostOnlyNetwork, COMSETTER(Id)(options.bstrNetworkId.raw()), RTEXITCODE_FAILURE);
    }
    if (options.bstrNetworkName.isNotEmpty())
    {
        CHECK_ERROR2_RET(hrc, hostOnlyNetwork, COMSETTER(NetworkName)(options.bstrNetworkName.raw()), RTEXITCODE_FAILURE);
    }
    if (options.bstrNetworkMask.isNotEmpty())
    {
        CHECK_ERROR2_RET(hrc, hostOnlyNetwork, COMSETTER(NetworkMask)(options.bstrNetworkMask.raw()), RTEXITCODE_FAILURE);
    }
    if (options.bstrLowerIp.isNotEmpty())
    {
        CHECK_ERROR2_RET(hrc, hostOnlyNetwork, COMSETTER(LowerIP)(options.bstrLowerIp.raw()), RTEXITCODE_FAILURE);
    }
    if (options.bstrUpperIp.isNotEmpty())
    {
        CHECK_ERROR2_RET(hrc, hostOnlyNetwork, COMSETTER(UpperIP)(options.bstrUpperIp.raw()), RTEXITCODE_FAILURE);
    }
    if (options.fEnable)
    {
        CHECK_ERROR2_RET(hrc, hostOnlyNetwork, COMSETTER(Enabled)(TRUE), RTEXITCODE_FAILURE);
    }
    if (options.fDisable)
    {
        CHECK_ERROR2_RET(hrc, hostOnlyNetwork, COMSETTER(Enabled)(FALSE), RTEXITCODE_FAILURE);
    }

    return RTEXITCODE_SUCCESS;
}

static RTEXITCODE handleNetAdd(HandlerArg *a)
{
    HRESULT hrc = S_OK;

    HOSTONLYNETOPT options;
    hrc = createUpdateHostOnlyNetworkParse(a, options);

    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;
    ComPtr<IHostOnlyNetwork> hostOnlyNetwork;

    if (options.bstrNetworkName.isEmpty())
        return errorArgument(HostOnly::tr("The --name parameter must be specified"));
    if (options.bstrNetworkMask.isEmpty())
        return errorArgument(HostOnly::tr("The --netmask parameter must be specified"));
    if (options.bstrLowerIp.isEmpty())
        return errorArgument(HostOnly::tr("The --lower-ip parameter must be specified"));
    if (options.bstrUpperIp.isEmpty())
        return errorArgument(HostOnly::tr("The --upper-ip parameter must be specified"));

    CHECK_ERROR2_RET(hrc, pVirtualBox,
                     CreateHostOnlyNetwork(options.bstrNetworkName.raw(), hostOnlyNetwork.asOutParam()),
                     RTEXITCODE_FAILURE);
    return createUpdateHostOnlyNetworkCommon(hostOnlyNetwork, options);
}

static RTEXITCODE handleNetModify(HandlerArg *a)
{
    HRESULT hrc = S_OK;

    HOSTONLYNETOPT options;
    hrc = createUpdateHostOnlyNetworkParse(a, options);

    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;
    ComPtr<IHostOnlyNetwork> hostOnlyNetwork;

    if (options.bstrNetworkName.isNotEmpty())
    {
        CHECK_ERROR2_RET(hrc, pVirtualBox,
                        FindHostOnlyNetworkByName(options.bstrNetworkName.raw(), hostOnlyNetwork.asOutParam()),
                        RTEXITCODE_FAILURE);
    }
    else if (options.bstrNetworkId.isNotEmpty())
    {
        CHECK_ERROR2_RET(hrc, pVirtualBox,
                        FindHostOnlyNetworkById(options.bstrNetworkId.raw(), hostOnlyNetwork.asOutParam()),
                        RTEXITCODE_FAILURE);
    }
    else
        return errorArgument(HostOnly::tr("Either --name or --id parameter must be specified"));

    return createUpdateHostOnlyNetworkCommon(hostOnlyNetwork, options);
}

static RTEXITCODE handleNetRemove(HandlerArg *a)
{
    HRESULT hrc = S_OK;

    static const RTGETOPTDEF s_aOptions[] =
    {
        { "--id",             'i', RTGETOPT_REQ_STRING },
        { "--name",           'n', RTGETOPT_REQ_STRING },
    };

    RTGETOPTSTATE GetState;
    RTGETOPTUNION ValueUnion;
    int vrc = RTGetOptInit(&GetState, a->argc, a->argv, s_aOptions, RT_ELEMENTS(s_aOptions), 1 /* iFirst */, 0);
    AssertRCReturn(vrc, RTEXITCODE_FAILURE);

    Bstr strNetworkId, strNetworkName;

    int c;
    while ((c = RTGetOpt(&GetState, &ValueUnion)) != 0)
    {
        switch (c)
        {
            case 'i':
                strNetworkId=ValueUnion.psz;
                break;
            case 'n':
                strNetworkName=ValueUnion.psz;
                break;
            case VINF_GETOPT_NOT_OPTION:
                return errorUnknownSubcommand(ValueUnion.psz);
            default:
                return errorGetOpt(c, &ValueUnion);
        }
    }

    ComPtr<IVirtualBox> pVirtualBox = a->virtualBox;
    ComPtr<IHostOnlyNetwork> hostOnlyNetwork;

    if (!strNetworkName.isEmpty())
    {
        CHECK_ERROR2_RET(hrc, pVirtualBox,
                        FindHostOnlyNetworkByName(strNetworkName.raw(), hostOnlyNetwork.asOutParam()),
                        RTEXITCODE_FAILURE);
    }
    else if (!strNetworkId.isEmpty())
    {
        CHECK_ERROR2_RET(hrc, pVirtualBox,
                        FindHostOnlyNetworkById(strNetworkId.raw(), hostOnlyNetwork.asOutParam()),
                        RTEXITCODE_FAILURE);
    }
    else
        return errorArgument(HostOnly::tr("Either --name or --id parameter must be specified"));

    CHECK_ERROR2_RET(hrc, pVirtualBox,
                    RemoveHostOnlyNetwork(hostOnlyNetwork),
                    RTEXITCODE_FAILURE);
    return RTEXITCODE_SUCCESS;
}

RTEXITCODE handleHostonlyNet(HandlerArg *a)
{
    if (a->argc < 1)
        return errorSyntax(HostOnly::tr("No sub-command specified"));

    RTEXITCODE rcExit;
    if (!strcmp(a->argv[0], "add"))
    {
        setCurrentSubcommand(HELP_SCOPE_HOSTONLYNET_ADD);
        rcExit = handleNetAdd(a);
    }
    else if (!strcmp(a->argv[0], "modify"))
    {
        setCurrentSubcommand(HELP_SCOPE_HOSTONLYNET_MODIFY);
        rcExit = handleNetModify(a);
    }
    else if (!strcmp(a->argv[0], "remove"))
    {
        setCurrentSubcommand(HELP_SCOPE_HOSTONLYNET_REMOVE);
        rcExit = handleNetRemove(a);
    }
    else
        rcExit = errorSyntax(HostOnly::tr("Unknown sub-command '%s'"), a->argv[0]);
    return rcExit;
}
#endif /* VBOX_WITH_VMNET */
